#include "ProjectileHitEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/EventRefObjSerializer.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/deps/vanilla_components/ActorTypeComponent.h"
#include "mc/entity/components/ActorGameTypeComponent.h"
#include "mc/entity/components/ActorRotationComponent.h"
#include "mc/entity/components_json_legacy/OnHitSubcomponent.h"
#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/entity/components_json_legacy/ProjectileDescription.h"
#include "mc/gameplayhandlers/CoordinatorResult.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageByActorSource.h"
#include "mc/world/actor/ActorDamageByChildActorSource.h"
#include "mc/world/actor/projectile/ThrownTrident.h"
#include "mc/world/events/ActorEventCoordinator.h"
#include "mc/world/events/ActorGameplayEvent.h"
#include "mc/world/events/EventRef.h"
#include "mc/world/events/ProjectileHitEvent.h"
#include "mc/world/events/gameevents/GameEventRegistry.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/enchanting/Enchant.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/GameType.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/BedrockBlockNames.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/VanillaBlockTypeGroups.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"
#include "mc/world/phys/HitResultType.h"
/*
namespace Catalyst {

namespace {

struct ProjectileHitEventBridge {
    Actor&    mProjectile;
    HitResult mHitResult;
};

static_assert(sizeof(ProjectileHitEventBridge) == sizeof(::ProjectileHitEvent));
static_assert(alignof(ProjectileHitEventBridge) == alignof(::ProjectileHitEvent));

CoordinatorResult sendProjectileHitEvent(Actor& projectile, HitResult const& hitResult) {
    ProjectileHitEventBridge                              bridge{projectile, hitResult};
    auto const&                                           event = reinterpret_cast<::ProjectileHitEvent const&>(bridge);
    EventRef<ActorGameplayEvent<CoordinatorResult>> const eventRef(event);

    auto& coordinator = projectile.getLevel().getActorEventCoordinator();
    using SendEventFn =
        CoordinatorResult (ActorEventCoordinator::*)(EventRef<ActorGameplayEvent<CoordinatorResult>> const&);
    return (coordinator.*static_cast<SendEventFn>(&ActorEventCoordinator::sendEvent))(eventRef);
}

void playProjectileHitSound(Actor& owner, SharedTypes::Legacy::LevelSoundEvent sound) {
    owner.playSound(sound, owner.getPosition(), -1);
}

Actor* resolveHitActor(HitResult const& hitResult) {
    return hitResult.getEntity();
}

bool shouldIgnoreHitEntity(Actor* hitActor) {
    return hitActor != nullptr && !hitActor->canInteractWithOtherEntitiesInGame();
}

bool tryIgniteTntOnHit(Actor& owner, BlockSource& region, BlockPos const& pos, Block const& block) {
    GameRuleId doTntExplodeId;
    doTntExplodeId.mValue = static_cast<int>(GameRules::GameRulesIndex::DoTntExplode);

    auto& gameRules = owner.getLevel().getGameRules();
    if (!gameRules.hasRule(doTntExplodeId) || !gameRules.getBool(doTntExplodeId, false)) {
        return false;
    }

    auto const& tntIds     = VanillaBlockTypeGroups::TntIds();
    auto const& blockName  = block.getBlockType().mNameInfo->mFullName.get();
    auto        isTntBlock = std::any_of(tntIds.begin(), tntIds.end(), [&](auto const& entry) {
        return entry.get().mStrHash == blockName.mStrHash;
    });
    if (!isTntBlock || !owner.isOnFire()) {
        return false;
    }

    auto ignitedBlock = block.setState(VanillaStates::ExplodeBit(), true);
    if (!ignitedBlock) {
        return false;
    }

    ignitedBlock->getBlockType().destroy(region, pos, *ignitedBlock, &owner);

    BlockChangeContext ctx;
    auto const&        air = BlockTypeRegistry::get().getDefaultBlockState(BedrockBlockNames::Air());
    region.setBlock(pos, air, 3, nullptr, ctx);
    return true;
}

bool shouldSkipEntityHitEffects(Actor& hitActor) {
    auto gameType = hitActor.getEntityContext().tryGetComponent<ActorGameTypeComponent>();
    if (!gameType) {
        return false;
    }

    if (gameType->mGameType == GameType::Creative) {
        return true;
    }

    return gameType->mGameType == GameType::Default && hitActor.getLevel().getDefaultGameType() == GameType::Creative;
}

bool applyStopOnHurtMotion(ProjectileComponent& component, Actor& owner, Actor* hitActor) {
    bool blocking = false;
    if (hitActor != nullptr) {
        hitActor->mChainedDamageEffects = false;
        blocking                        = hitActor->isBlocking();
    }

    if (!component.mStopOnHurt && !blocking) {
        return false;
    }

    if (!blocking) {
        auto& posDelta  = owner.mBuiltInComponents->mStateVectorComponent->mPosDelta.get();
        posDelta.x     *= -0.01f;
        posDelta.y     *= -0.1f;
        posDelta.z     *= -0.01f;
        return false;
    }

    auto look = hitActor->getViewVector(1.0f);
    if (auto length = look.length(); length >= 0.0001) {
        look = look / static_cast<float>(length);
    } else {
        look = Vec3::ZERO();
    }
    owner.mBuiltInComponents->mStateVectorComponent->mPosDelta = look * 0.5f;
    return true;
}

void handleReturningProjectileOwner(ProjectileComponent& component, Actor& owner, Actor* hitActor) {
    if (hitActor == nullptr) {
        return;
    }

    bool shouldRebindOwner = true;
    if (owner.getEntityTypeId() == ActorType::Trident) {
        auto& trident = static_cast<ThrownTrident&>(owner);
        auto  item    = trident._getPickupItem();
        if (!item.isNull()) {
            auto loyaltyLevel = EnchantUtils::getEnchantLevel(Enchant::Type::Loyalty, item);
            if (loyaltyLevel > 0) {
                shouldRebindOwner = false;
            }
        }
    }

    if (shouldRebindOwner) {
        owner.setOwner(hitActor->getOrCreateUniqueID());
    }

    component.mDamageOwner               = true;
    component.mWaitingForServerHitGround = true;
    component.mCachedHitResult           = HitResult{};
}

void tryReflectHitProjectileOnHurt(Actor& owner, Actor* hitActor) {
    if (hitActor == nullptr) {
        return;
    }

    auto hitProjectile = hitActor->getEntityContext().tryGetComponent<ProjectileComponent>();
    if (!hitProjectile) {
        return;
    }

    auto* sourceActor = owner.getLevel().fetchEntity(owner.getSourceUniqueID(), false);
    if (sourceActor != nullptr) {
        ActorDamageByChildActorSource damageSource(owner, *sourceActor,
SharedTypes::Legacy::ActorDamageCause::Projectile); hitProjectile->_tryReflectOnHurt(*hitActor, damageSource); } else {
        ActorDamageByActorSource damageSource(owner, SharedTypes::Legacy::ActorDamageCause::Projectile);
        hitProjectile->_tryReflectOnHurt(*hitActor, damageSource);
    }
}

void serializeHitEventCommon(CompoundTag& nbt, HitResult const& hitResult) {
    nbt["type"]        = magic_enum::enum_name(hitResult.mType);
    nbt["face"]        = hitResult.mFacing;
    nbt["blockPos"]    = ListTag{hitResult.mBlock.x, hitResult.mBlock.y, hitResult.mBlock.z};
    nbt["pos"]         = ListTag{hitResult.mPos.x, hitResult.mPos.y, hitResult.mPos.z};
    nbt["isHitLiquid"] = hitResult.mIsHitLiquid;
    if (auto* actor = hitResult.getEntity(); actor != nullptr) {
        nbt["hitActor"] = ll::event::serializePtrObj(actor);
    }
}

} // namespace

void ProjectileHitEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::ActorEvent::serialize(nbt);
    serializeHitEventCommon(nbt, hitResult());
}

LL_TYPE_INSTANCE_HOOK(
    ProjectileHitEventHook,
    ll::memory::HookPriority::Normal,
    ProjectileComponent,
    &ProjectileComponent::onHit,
    void,
    Actor&           owner,
    HitResult const& hitResult
) {
    auto& bus      = ll::event::EventBus::getInstance();
    auto* hitActor = resolveHitActor(hitResult);

    if (shouldIgnoreHitEntity(hitActor)) {
        return;
    }

    ProjectileHitBeforeEvent beforeEvent(owner, hitResult);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    mHitResult = hitResult;
    mBlock     = hitResult.mBlock;
    mHitFacing = hitResult.mFacing;
    mHitActor  = hitResult.mType == HitResultType::Entity;
    mHitWater  = hitResult.mIsHitLiquid;

    if (sendProjectileHitEvent(owner, mHitResult.get()) == CoordinatorResult::Cancel) {
        return;
    }

    auto&       result        = mHitResult.get();
    auto&       region        = owner.getDimensionBlockSource();
    auto const& affectedBlock = region.getBlock(result.mBlock);

    affectedBlock.getBlockType().onProjectileHit(region, result.mBlock, owner);
    tryIgniteTntOnHit(owner, region, result.mBlock, affectedBlock);

    if (_tryReflect(owner, owner.getLevel())) {
        return;
    }

    tryReflectHitProjectileOnHurt(owner, hitActor);

    for (auto* subcomponent : mOnHitCommands.get()) {
        if (subcomponent != nullptr) {
            subcomponent->doOnHitEffect(owner, *this);
        }
    }

    owner.postGameEvent(GameEventRegistry::projectileLand(), owner.getPosition(), &affectedBlock);

    if (result.mType == HitResultType::Entity) {
        if (hitActor == nullptr || !shouldSkipEntityHitEffects(*hitActor)) {
            playProjectileHitSound(owner, mHitEntitySound);

            if (applyStopOnHurtMotion(*this, owner, hitActor)) {
                handleReturningProjectileOwner(*this, owner, hitActor);
            }

            if (hitActor != nullptr && hitActor->hasType(ActorType::Mob)) {
                _handleLightningOnHit(owner);
            }
        }
    } else {
        playProjectileHitSound(owner, mHitGroundSound);
    }

    ProjectileHitAfterEvent afterEvent(owner, hitResult);
    bus.publish(afterEvent);
}

CATALYST_HOOKED_EVENT_PAIR(
    ProjectileHitBeforeEvent,
    ProjectileHitAfterEvent,
    ProjectileHitEventHook
)

} // namespace Catalyst

*/
