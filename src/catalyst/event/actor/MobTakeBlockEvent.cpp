#include "MobTakeBlockEvent.h"

#include <algorithm>
#include <memory>

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/base/ScopedValue.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/EventRefObjSerializer.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/ecs/WeakEntityRef.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/gameplayhandlers/ActorGameplayHandler.h"
#include "mc/gameplayhandlers/CoordinatorResult.h"
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
#include "mc/world/events/ActorEventCoordinator.h"
#include "mc/world/events/ActorGameplayEvent.h"
#include "mc/world/events/ActorGriefingBlockEvent.h"
#include "mc/world/events/BlockSourceHandle.h"
#include "mc/world/events/EventRef.h"
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

class BlockSourceHandleGuard {
    std::shared_ptr<BlockSourceHandle> mHandle = std::make_shared<BlockSourceHandle>();

public:
    explicit BlockSourceHandleGuard(BlockSource& source) {
        mHandle->mSource = &source;
        source.addListener(*mHandle);
    }

    ~BlockSourceHandleGuard() {
        // onSourceDestroyed 会清空 mSource，避免回调销毁方块源后访问悬空指针。
        if (auto* source = mHandle->mSource) {
            source->removeListener(*mHandle);
            mHandle->mSource = nullptr;
        }
    }

    BlockSourceHandleGuard(BlockSourceHandleGuard const&)            = delete;
    BlockSourceHandleGuard& operator=(BlockSourceHandleGuard const&) = delete;

    std::shared_ptr<BlockSourceHandle> const& get() const { return mHandle; }
};

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

    auto&    random = mob.mLevel ? mob.mLevel->getThreadRandom() : Random::getThreadLocal();
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
    // 空列表保持原版语义：允许任意非空气方块。
    if (!validBlocks.empty()
        && std::none_of(validBlocks.begin(), validBlocks.end(), [&](BlockDescriptor const& descriptor) {
               return descriptor.matches(block);
           })) {
        return;
    }

    if (mRequiresLineOfSight && !BlockUtils::canSee(mob, pos, ShapeType::Collision)) {
        return;
    }

    auto const mobRef = mob.getEntityContext().getWeakRef();
    auto&      bus    = ll::event::EventBus::getInstance();
    // 回调可能移除目标组件，提前保存 onTake，之后不再读取 goal 成员。
    auto const onTake = mOnTake.get();

    BlockSourceHandleGuard sourceHandle(source);
    auto const             getCurrentMob = [&]() {
        auto currentMob = mobRef.tryUnwrap<Mob>();
        if (!currentMob || currentMob->mRemoved || sourceHandle.get()->mSource != &source
            || &currentMob->getDimensionBlockSource() != &source) {
            return decltype(currentMob){};
        }
        return currentMob;
    };
    auto const canTake = [&]() { return getCurrentMob() && &source.getBlock(pos) == &block; };

    // 两种 Before 取消入口都必须在修改携带物、发送装备包和移除方块之前完成。
    MobTakeBlockBeforeEvent beforeEvent(mob, pos, block);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled() || !canTake()) {
        return;
    }

    auto currentMob = getCurrentMob();
    if (!currentMob->mLevel) {
        return;
    }
    auto& coordinator = currentMob->mLevel->getActorEventCoordinator();
    auto* handler     = coordinator.mActorGameplayHandler.get();
    // 对应原版无 gameplay handler 时直接结束本次 tick 的分支。
    if (!handler) {
        return;
    }

    ActorGriefingBlockEvent const                         griefingEvent{mobRef, &block, Vec3(pos), sourceHandle.get()};
    EventRef<ActorGameplayEvent<CoordinatorResult>> const eventRef(griefingEvent);
    if (coordinator._processEvent(handler, eventRef.get()) == CoordinatorResult::Cancel || !canTake()) {
        return;
    }

    currentMob = getCurrentMob();
    ItemStack carriedBlock;
    carriedBlock.reinit(block, 1);
    currentMob->setCarriedItem(carriedBlock);
    if (!canTake()) {
        return;
    }
    currentMob = getCurrentMob();
    MobEquipmentPacket packet(
        MobEquipmentPacketPayload(currentMob->getRuntimeID(), carriedBlock, 0, 0, ContainerID::Inventory)
    );
    currentMob->getDimension().sendPacketForEntity(*currentMob, packet, nullptr);
    if (!canTake()) {
        return;
    }
    currentMob = getCurrentMob();

    BlockChangeContext changeContext{};
    changeContext.mContextSource = ActorChangeContext{&*currentMob};
    bool const removeSucceeded   = source.removeBlock(pos, changeContext);
    currentMob                   = getCurrentMob();
    if (!currentMob) {
        return;
    }
    bool const removed = removeSucceeded && source.getBlock(pos).isAir();
    source.postGameEvent(&*currentMob, GameEventRegistry::blockDestroy(), pos, &block);

    currentMob = getCurrentMob();
    if (!currentMob) {
        return;
    }
    VariantParameterList triggerParams{};
    triggerParams.mSelf = &*currentMob;
    if (currentMob->mLevel && currentMob->mTargetId->rawID != -1) {
        triggerParams.mTarget = currentMob->mLevel->fetchEntity(currentMob->mTargetId, false);
    }
    triggerParams.mBlock = &pos;
    ActorDefinitionDescriptor::executeTrigger(*currentMob, onTake, triggerParams);

    // onTake 可能移除实体；After 只报告实际完成的方块移除。
    if (removed) {
        if (auto afterMob = mobRef.tryUnwrap<Mob>(); afterMob && !afterMob->mRemoved) {
            MobTakeBlockAfterEvent afterEvent(*afterMob, pos, block);
            bus.publish(afterEvent);
        }
    }
}

CATALYST_HOOKED_EVENT_PAIR(MobTakeBlockBeforeEvent, MobTakeBlockAfterEvent, TakeBlockGoalTickHook)

} // namespace Catalyst
