#include "EnderDragonDestroyBlockEvent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <memory>

#include "catalyst/event/EmitterRegistration.h"
#include "catalyst/mod/Gloabl.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/deps/shared_types/v1_26_20/block/MaterialType.h"
#include "mc/gameplayhandlers/CoordinatorResult.h"
#include "mc/util/Random.h"
#include "mc/world/events/ActorEventCoordinator.h"
#include "mc/world/events/ActorGameplayEvent.h"
#include "mc/world/events/ActorGriefingBlockEvent.h"
#include "mc/world/events/BlockSourceHandle.h"
#include "mc/world/events/EventRef.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/ActorChangeContext.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/VanillaBlockTypeGroups.h"
#include "mc/world/level/block/VanillaBlockTypeIds.h"
#include "mc/world/level/material/Material.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"


#include "ll/api/event/EventRefObjSerializer.h"
#include "mc/deps/nbt/CompoundTag.h"


namespace Catalyst {

void EnderDragonDestroyBlockEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::MobEvent::serialize(nbt);
    nbt["pos"]   = ListTag{pos().x, pos().y, pos().z};
    nbt["block"] = ll::event::serializeRefObj(block());
}

void EnderDragonDestroyBlockAfterEvent::serialize(CompoundTag& nbt) const {
    EnderDragonDestroyBlockEvent::serialize(nbt);
    nbt["destroyed"] = destroyed();
}


namespace {

int floorToInt(float value) { return static_cast<int>(std::floor(value)); }

uint64 getBlockNameHash(Block const& block) { return block.mBlockType->mNameInfo.get().mFullName.get().mStrHash; }

bool isInBlockTypeGroup(uint64 blockNameHash, std::vector<std::reference_wrapper<HashedString const>> const& group) {
    return std::any_of(group.begin(), group.end(), [blockNameHash](std::reference_wrapper<HashedString const> ref) {
        return ref.get().mStrHash == blockNameHash;
    });
}

bool isDragonImmuneBlock(uint64 blockNameHash) {
    static const std::array<uint64, 19> protectedIds{
        VanillaBlockTypeIds::Bedrock().mStrHash,
        VanillaBlockTypeIds::Obsidian().mStrHash,
        VanillaBlockTypeIds::CryingObsidian().mStrHash,
        VanillaBlockTypeIds::RespawnAnchor().mStrHash,
        VanillaBlockTypeIds::EndStone().mStrHash,
        VanillaBlockTypeIds::EndPortalFrame().mStrHash,
        VanillaBlockTypeIds::EndPortal().mStrHash,
        VanillaBlockTypeIds::EndGateway().mStrHash,
        VanillaBlockTypeIds::IronBars().mStrHash,
        VanillaBlockTypeIds::ReinforcedDeepslate().mStrHash,
        VanillaBlockTypeIds::Barrier().mStrHash,
        VanillaBlockTypeIds::Allow().mStrHash,
        VanillaBlockTypeIds::Deny().mStrHash,
        VanillaBlockTypeIds::BorderBlock().mStrHash,
        VanillaBlockTypeIds::CommandBlock().mStrHash,
        VanillaBlockTypeIds::RepeatingCommandBlock().mStrHash,
        VanillaBlockTypeIds::ChainCommandBlock().mStrHash,
        VanillaBlockTypeIds::StructureBlock().mStrHash,
        VanillaBlockTypeIds::Jigsaw().mStrHash
    };

    if (std::find(protectedIds.begin(), protectedIds.end(), blockNameHash) != protectedIds.end()) {
        return true;
    }

    return isInBlockTypeGroup(blockNameHash, VanillaBlockTypeGroups::CopperBarsBlockIds());
}

class BlockSourceHandleGuard {
    BlockSource&                       mBlockSource;
    std::shared_ptr<BlockSourceHandle> mHandle;
    bool                               mAttached;

public:
    explicit BlockSourceHandleGuard(BlockSource& blockSource)
    : mBlockSource(blockSource),
      mHandle(std::make_shared<BlockSourceHandle>()),
      mAttached(false) {
        mHandle->mSource = &mBlockSource;
        mBlockSource.addListener(*mHandle);
        mAttached = true;
    }

    ~BlockSourceHandleGuard() {
        if (!mAttached) {
            return;
        }
        mBlockSource.removeListener(*mHandle);
        mHandle->mSource = nullptr;
        mAttached        = false;
    }

    std::shared_ptr<BlockSourceHandle> const& get() const { return mHandle; }
};

} // namespace

LL_TYPE_INSTANCE_HOOK(
    EnderDragonCheckWallsHook,
    ll::memory::HookPriority::Normal,
    EnderDragon,
    &EnderDragon::_checkWalls,
    bool,
    ::AABB bb
) {
    try {
        auto& bus         = ll::event::EventBus::getInstance();
        auto& blockSource = this->getDimensionBlockSource();
        auto& level       = this->getLevel();

        int const minX = floorToInt(bb.min.x);
        int const minY = floorToInt(bb.min.y);
        int const minZ = floorToInt(bb.min.z);
        int const maxX = floorToInt(bb.max.x);
        int const maxY = floorToInt(bb.max.y);
        int const maxZ = floorToInt(bb.max.z);

        bool touchedIndestructibleOrCancelled = false;
        bool destroyedAnyBlock                = false;

        GameRuleId mobGriefingId;
        mobGriefingId.mValue   = static_cast<int>(GameRules::GameRulesIndex::MobGriefing);
        bool const mobGriefing = level.getGameRules().getBool(mobGriefingId, true);

        for (int x = minX; x <= maxX; ++x) {
            for (int y = minY; y <= maxY; ++y) {
                for (int z = minZ; z <= maxZ; ++z) {
                    BlockPos const pos(x, y, z);
                    auto const&    block         = blockSource.getBlock(pos);
                    uint64 const   blockNameHash = getBlockNameHash(block);

                    if (blockNameHash == VanillaBlockTypeIds::StructureVoid().mStrHash) {
                        continue;
                    }

                    if (isInBlockTypeGroup(blockNameHash, VanillaBlockTypeGroups::LightBlockIds())) {
                        continue;
                    }

                    auto const materialType = block.mBlockType->mMaterial.mType;
                    if (materialType == SharedTypes::v1_26_20::MaterialType::Air
                        || materialType == SharedTypes::v1_26_20::MaterialType::Fire) {
                        continue;
                    }

                    if (!mobGriefing || isDragonImmuneBlock(blockNameHash)) {
                        touchedIndestructibleOrCancelled = true;
                        continue;
                    }

                    EnderDragonDestroyBlockBeforeEvent beforeEvent(*this, pos, block);
                    bus.publish(beforeEvent);
                    if (beforeEvent.isCancelled()) {
                        touchedIndestructibleOrCancelled = true;
                        continue;
                    }

                    {
                        // 原版会传 BlockSourceHandle，且在事件处理期间作为 BlockSource 监听器存在。
                        // 用 RAII 保证所有分支都能解除监听，避免悬挂监听器导致 _blockChanged 崩溃。
                        BlockSourceHandleGuard blockSourceHandle(blockSource);

                        ActorGriefingBlockEvent const griefingEvent{
                            this->getEntityContext().getWeakRef(),
                            &block,
                            Vec3(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)),
                            blockSourceHandle.get()
                        };

                        auto& eventCoordinator = level.getActorEventCoordinator();
                        EventRef<ActorGameplayEvent<CoordinatorResult>> const eventRef(griefingEvent);

                        using SendEventFunc = CoordinatorResult (ActorEventCoordinator::*)(
                            EventRef<ActorGameplayEvent<CoordinatorResult>> const&
                        );
                        /*
                        if ((eventCoordinator.*static_cast<SendEventFunc>(&ActorEventCoordinator::sendEvent))(eventRef)
                            != CoordinatorResult::Continue) {
                            touchedIndestructibleOrCancelled = true;
                            continue;
                        }
                            */
                    }

                    BlockChangeContext changeContext{};
                    changeContext.mContextSource = ActorChangeContext{this};

                    bool const destroyed = blockSource.removeBlock(pos, changeContext);
                    destroyedAnyBlock    = destroyedAnyBlock || destroyed;

                    EnderDragonDestroyBlockAfterEvent afterEvent(*this, pos, block, destroyed);
                    bus.publish(afterEvent);
                }
            }
        }

        if (destroyedAnyBlock) {
            auto& random = level.getRandom();

            Vec3 effectPos{
                bb.min.x + random.nextFloat() * (bb.max.x - bb.min.x),
                bb.min.y + random.nextFloat() * (bb.max.y - bb.min.y),
                bb.min.z + random.nextFloat() * (bb.max.z - bb.min.z)
            };

            level.broadcastLocalEvent(blockSource, static_cast<SharedTypes::Legacy::LevelEvent>(2009), effectPos, 65);
        }

        return touchedIndestructibleOrCancelled;
    } catch (const std::exception& e) {
        logger.warn("EnderDragonCheckWallsHook 发生异常: {}", e.what());
        return origin(bb);
    } catch (...) {
        logger.warn("EnderDragonCheckWallsHook 发生未知异常！");
        return origin(bb);
    }
}

CATALYST_HOOKED_EVENT_PAIR(
    EnderDragonDestroyBlockBeforeEvent,
    EnderDragonDestroyBlockAfterEvent,
    EnderDragonCheckWallsHook
)

} // namespace Catalyst
