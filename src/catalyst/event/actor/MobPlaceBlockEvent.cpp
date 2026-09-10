#if defined(__clang__)
// The generated vector storage header requires this otherwise-unused type to be complete.
struct NamedMolangScript {};
#endif

#include "MobPlaceBlockEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/packet/MobEquipmentPacket.h"
#include "mc/network/packet/MobEquipmentPacketPayload.h"
#include "mc/util/IntRange.h"
#include "mc/util/Random.h"
#include "mc/util/VariantParameterList.h"
#include "mc/util/VariantParameterListConst.h"
#include "mc/world/ContainerID.h"
#include "mc/world/actor/ActorDefinitionDescriptor.h"
#include "mc/world/actor/ActorFilterGroup.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/goal/PlaceBlockGoal.h"
#include "mc/world/events/gameevents/GameEventRegistry.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockDescriptor.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/CachedComponentData.h"
#include "mc/world/level/dimension/Dimension.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void MobPlaceBlockEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::MobEvent::serialize(nbt);
    nbt["pos"]   = ListTag{pos().x, pos().y, pos().z};
    nbt["block"] = ll::event::serializeRefObj(block());
}

namespace {

int rollRange(Random& random, IntRange const& range) {
    int value = range.rangeMin;
    if (range.rangeMax > range.rangeMin) {
        value += random.nextInt(range.rangeMax - range.rangeMin + 1);
    }
    return value;
}

// PlaceBlockGoal::_tryGetRandomPlaceBlock (inlined into tick)
Block const* tryGetRandomPlaceBlock(
    PlaceBlockGoal&                  goal,
    Mob const&                       mob,
    VariantParameterListConst const& params,
    Random&                          random
) {
    std::vector<PlaceBlockGoal::WeightedBlockDescriptor const*> candidates;
    for (auto const& weighted : goal.mRandomlyPlaceableBlocks.get()) {
        if (weighted.mFilter->evaluateActor(mob, params)) {
            candidates.push_back(&weighted);
        }
    }
    if (candidates.empty()) {
        return nullptr;
    }

    int totalWeight = 0;
    for (auto const* weighted : candidates) {
        totalWeight += weighted->mWeight;
    }

    int roll = totalWeight != 0 ? random.nextInt(totalWeight) : 0;

    auto it  = candidates.begin();
    roll    -= (*it)->mWeight;
    while (roll >= 0) {
        ++it;
        roll -= (*it)->mWeight;
    }
    return (*it)->mBlock->tryGetBlock();
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    MobPlaceBlockEventHook,
    ll::memory::HookPriority::Normal,
    PlaceBlockGoal,
    &PlaceBlockGoal::$tick,
    void
) {
    Mob&  mob    = this->mMob;
    auto& random = mob.getRandom();

    BlockPos targetPos(mob.getPosition());
    targetPos.x += rollRange(random, this->mXZRange);
    targetPos.y += rollRange(random, this->mYRange);
    targetPos.z += rollRange(random, this->mXZRange);

    auto& blockSource = mob.getDimensionBlockSource();
    auto& targetBlock = blockSource.getBlock(targetPos);

    // 检查目标位置是否为空气
    if (!targetBlock.isAir()) {
        return;
    }

    // 检查下方方块
    BlockPos belowPos  = targetPos;
    belowPos.y        -= 1;
    auto& belowBlock   = blockSource.getBlock(belowPos);

    if (belowBlock.isAir() || !belowBlock.mCachedComponentData->mIsSolid) {
        return;
    }

    auto& bus = ll::event::EventBus::getInstance();

    VariantParameterList triggerParams{};
    triggerParams.mSelf = &mob;
    if (mob.mLevel && mob.mTargetId->rawID != -1) {
        triggerParams.mTarget = mob.mLevel->fetchEntity(mob.mTargetId, false);
    }
    triggerParams.mBlock = &targetPos;

    if (this->mRandomlyPlaceableBlocks->empty()) {
        // 模式A: 使用携带的方块 (_tryPlaceCarriedBlock)
        auto const* toPlace = mob.getCarriedItem().mBlock;
        if (!toPlace || !toPlace->mBlockType->mayPlace(blockSource, targetPos)) {
            return;
        }

        // 发布 BeforeEvent
        MobPlaceBlockBeforeEvent beforeEvent(mob, targetPos, *toPlace);
        bus.publish(beforeEvent);
        if (beforeEvent.isCancelled()) {
            return;
        }

        mob.setCarriedItem(ItemStack::EMPTY_ITEM());
        MobEquipmentPacket packet(
            MobEquipmentPacketPayload(mob.getRuntimeID(), ItemStack::EMPTY_ITEM(), 0, 0, ContainerID::Inventory)
        );
        mob.getDimension().sendPacketForEntity(mob, packet, nullptr);

        BlockChangeContext changeContext{};
        blockSource.setBlock(targetPos, *toPlace, 3, nullptr, changeContext);
        blockSource.postGameEvent(&mob, GameEventRegistry::blockPlace(), targetPos, toPlace);

        ActorDefinitionDescriptor::executeTrigger(mob, this->mOnPlace, triggerParams);

        // 发布 AfterEvent
        auto& afterBlock = blockSource.getBlock(targetPos);
        if (!afterBlock.isAir()) {
            MobPlaceBlockAfterEvent afterEvent(mob, targetPos, afterBlock);
            bus.publish(afterEvent);
        }
    } else {
        // 模式B: 从加权列表随机挑选 (_tryGetRandomPlaceBlock)
        VariantParameterListConst blockPickParams{};
        blockPickParams.mSelf   = triggerParams.mSelf;
        blockPickParams.mOther  = triggerParams.mOther;
        blockPickParams.mPlayer = triggerParams.mPlayer;
        blockPickParams.mTarget = triggerParams.mTarget;
        blockPickParams.mParent = triggerParams.mParent;
        blockPickParams.mBaby   = triggerParams.mBaby;
        blockPickParams.mBlock  = triggerParams.mBlock;
        blockPickParams.mDamager = triggerParams.mDamager;
        blockPickParams.mHolder  = triggerParams.mHolder;

        auto const* randomBlock = tryGetRandomPlaceBlock(*this, mob, blockPickParams, random);
        if (!randomBlock) {
            return;
        }

        // 发布 BeforeEvent
        MobPlaceBlockBeforeEvent beforeEvent(mob, targetPos, *randomBlock);
        bus.publish(beforeEvent);
        if (beforeEvent.isCancelled()) {
            return;
        }

        BlockChangeContext changeContext{};
        blockSource.setBlock(targetPos, *randomBlock, 3, nullptr, changeContext);
        blockSource.postGameEvent(&mob, GameEventRegistry::blockPlace(), targetPos, randomBlock);

        ActorDefinitionDescriptor::executeTrigger(mob, this->mOnPlace, triggerParams);

        // 发布 AfterEvent
        auto& afterBlock = blockSource.getBlock(targetPos);
        if (!afterBlock.isAir()) {
            MobPlaceBlockAfterEvent afterEvent(mob, targetPos, afterBlock);
            bus.publish(afterEvent);
        }
    }
}

CATALYST_HOOKED_EVENT_PAIR(
    MobPlaceBlockBeforeEvent,
    MobPlaceBlockAfterEvent,
    MobPlaceBlockEventHook
)

} // namespace Catalyst
