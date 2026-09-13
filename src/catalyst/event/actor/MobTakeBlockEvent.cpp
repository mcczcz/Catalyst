#include "MobTakeBlockEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/base/ScopedValue.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/EventRefObjSerializer.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/ecs/WeakEntityRef.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/packet/MobEquipmentPacket.h"
#include "mc/network/packet/MobEquipmentPacketPayload.h"
#include "mc/util/BlockUtils.h"
#include "mc/util/IntRange.h"
#include "mc/util/NamedMolangScript.h"
#include "mc/util/Random.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/ContainerID.h"
#include "mc/world/actor/ActorDefinitionDescriptor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/goal/TakeBlockGoal.h"
#include "mc/world/events/gameevents/GameEventRegistry.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/block/ActorChangeContext.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockDescriptor.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/feature/helpers/FeatureHelper.h"

namespace Catalyst {

void MobTakeBlockEvent::serialize(CompoundTag& nbt) const {
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

struct TakeBlockAttempt {
    TakeBlockAttempt* parent;
    Mob const*        mob;
};

thread_local TakeBlockAttempt* currentTakeBlockAttempt = nullptr;

} // namespace

LL_TYPE_INSTANCE_HOOK(TakeBlockGoalTickHook, HookPriority::Normal, TakeBlockGoal, &TakeBlockGoal::$tick, void) {
    Mob& mob = mMob;
    for (auto* attempt = currentTakeBlockAttempt; attempt; attempt = attempt->parent) {
        // 防止监听器再次调用同一生物的 tick，递归发布拾取事件。
        if (attempt->mob == &mob) {
            return;
        }
    }
    TakeBlockAttempt attempt{currentTakeBlockAttempt, &mob};
    ll::ScopedValue  scope(currentTakeBlockAttempt, &attempt);

    auto&    random = mob.mLevel ? mob.mLevel->getThreadRandom() : Random::mThreadLocalRandom();
    BlockPos pos(mob.getPosition());
    pos.x += rollRange(random, mXZRange);
    pos.y += rollRange(random, mYRange);
    pos.z += rollRange(random, mXZRange);

    auto&       source = mob.getDimensionBlockSource();
    auto const& block  = source.getBlock(pos);
    if (block.isAir()) {
        return;
    }

    auto const& validBlocks = mValidBlocks.get();
    // 原生列表交给 BDS 遍历；插件侧不按 sizeof(BlockDescriptor) 计算元素地址。
    // 空列表保持原版语义：允许任意非空气方块。
    if (!validBlocks.empty() && !FeatureHelper::passesAllowList(block, validBlocks)) {
        return;
    }
    if (mRequiresLineOfSight && !BlockUtils::canSee(mob, pos, ShapeType::Collision)) {
        return;
    }

    auto const mobRef = mob.getEntityContext().getWeakRef();
    auto&      bus    = ll::event::EventBus::getInstance();

    // 用 Catalyst Before 事件替代原生 ActorGriefingBlockEvent 的取消入口。
    // 直接在 tick 中发布，取消时不改变携带物或世界方块。
    MobTakeBlockBeforeEvent beforeEvent(mob, pos, block);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    auto currentMob = mobRef.tryUnwrap<Mob>();
    if (!currentMob || &currentMob->getDimensionBlockSource() != &source || &source.getBlock(pos) != &block) {
        return;
    }

    ItemStack carriedBlock;
    carriedBlock.reinit(block, 1);
    currentMob->setCarriedItem(carriedBlock);
    MobEquipmentPacket packet(
        MobEquipmentPacketPayload(currentMob->getRuntimeID(), carriedBlock, 0, 0, ContainerID::Inventory)
    );
    currentMob->getDimension().sendPacketForEntity(*currentMob, packet, nullptr);

    BlockChangeContext changeContext{};
    changeContext.mContextSource = ActorChangeContext{&*currentMob};
    bool const removed           = source.removeBlock(pos, changeContext) && source.getBlock(pos).isAir();
    source.postGameEvent(&*currentMob, GameEventRegistry::blockDestroy(), pos, &block);

    VariantParameterList triggerParams{};
    triggerParams.mSelf = &*currentMob;
    if (currentMob->mLevel && currentMob->mTargetId->rawID != -1) {
        triggerParams.mTarget = currentMob->mLevel->fetchEntity(currentMob->mTargetId, false);
    }
    triggerParams.mBlock = &pos;
    ActorDefinitionDescriptor::executeTrigger(*currentMob, mOnTake, triggerParams);

    // onTake 可能移除实体；After 只报告实际完成的方块移除。
    if (removed) {
        if (auto afterMob = mobRef.tryUnwrap<Mob>()) {
            MobTakeBlockAfterEvent afterEvent(*afterMob, pos, block);
            bus.publish(afterEvent);
        }
    }
}

CATALYST_HOOKED_EVENT_PAIR(MobTakeBlockBeforeEvent, MobTakeBlockAfterEvent, TakeBlockGoalTickHook)

} // namespace Catalyst
