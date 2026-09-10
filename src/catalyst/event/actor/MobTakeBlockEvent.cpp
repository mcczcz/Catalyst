#if defined(__clang__)
// The generated vector storage header requires this otherwise-unused type to be complete.
struct NamedMolangScript {};
#endif

#include "MobTakeBlockEvent.h"

#include <algorithm>

#include "catalyst/event/ActorGameplayEventDispatch.h"
#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/ecs/WeakEntityRef.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/gameplayhandlers/CoordinatorResult.h"
#include "mc/legacy/ActorUniqueID.h"
#include "mc/network/packet/MobEquipmentPacket.h"
#include "mc/network/packet/MobEquipmentPacketPayload.h"
#include "mc/util/BlockUtils.h"
#include "mc/util/IntRange.h"
#include "mc/util/Random.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/ContainerID.h"
#include "mc/world/actor/ActorDefinitionDescriptor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/goal/TakeBlockGoal.h"
#include "mc/world/events/ActorGameplayEvent.h"
#include "mc/world/events/ActorGriefingBlockEvent.h"
#include "mc/world/events/EventRef.h"
#include "mc/world/events/gameevents/GameEventRegistry.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ILevel.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/block/ActorChangeContext.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockDescriptor.h"
#include "mc/world/level/dimension/Dimension.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

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

} // namespace

LL_TYPE_INSTANCE_HOOK(
    MobTakeBlockEventHook,
    ll::memory::HookPriority::Normal,
    TakeBlockGoal,
    &TakeBlockGoal::$tick,
    void
) {
    Mob&  mob    = this->mMob;
    auto& random = mob.mLevel ? mob.mLevel->getThreadRandom() : Random::mThreadLocalRandom();

    BlockPos targetPos(mob.getPosition());
    targetPos.x += rollRange(random, this->mXZRange);
    targetPos.y += rollRange(random, this->mYRange);
    targetPos.z += rollRange(random, this->mXZRange);

    auto& blockSource = mob.getDimensionBlockSource();
    auto& block       = blockSource.getBlock(targetPos);
    if (block.isAir()) {
        return;
    }

    auto const& validBlocks = this->mValidBlocks.get();
    if (!validBlocks.empty()
        && std::none_of(validBlocks.begin(), validBlocks.end(), [&](BlockDescriptor const& descriptor) {
               return descriptor.matches(block);
           })) {
        return;
    }

    if (this->mRequiresLineOfSight && !BlockUtils::canSee(mob, targetPos, ShapeType::Collision)) {
        return;
    }

    auto& bus = ll::event::EventBus::getInstance();

    // 发布 BeforeEvent
    MobTakeBlockBeforeEvent beforeEvent(mob, targetPos, block);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    // 原版在此派发 ActorGriefingBlockEvent（脚本 API 可取消），handle 存活到函数结束。
    BlockSourceHandleGuard blockSourceHandle(blockSource);

    ActorGriefingBlockEvent const griefingEvent{
        mob.getEntityContext().getWeakRef(),
        &block,
        Vec3(static_cast<float>(targetPos.x), static_cast<float>(targetPos.y), static_cast<float>(targetPos.z)),
        blockSourceHandle.get()
    };
    EventRef<ActorGameplayEvent<CoordinatorResult>> const eventRef(griefingEvent);
    if (sendActorGameplayEvent(mob.getLevel(), eventRef) == CoordinatorResult::Cancel) {
        return;
    }

    // 方块变为手持物并同步装备包（ItemStack(Block const&, int, CompoundTag const*) 已不再导出，reinit 与之等价）
    ItemStack item;
    item.reinit(block, 1);
    mob.setCarriedItem(item);
    MobEquipmentPacket packet(MobEquipmentPacketPayload(mob.getRuntimeID(), item, 0, 0, ContainerID::Inventory));
    mob.getDimension().sendPacketForEntity(mob, packet, nullptr);

    // 移除方块
    BlockChangeContext changeContext{};
    changeContext.mContextSource = ActorChangeContext{&mob};
    blockSource.removeBlock(targetPos, changeContext);
    blockSource.postGameEvent(&mob, GameEventRegistry::blockDestroy(), targetPos, &block);

    // 执行 on_take 触发器
    VariantParameterList triggerParams{};
    triggerParams.mSelf = &mob;
    if (mob.mLevel && mob.mTargetId->rawID != -1) {
        triggerParams.mTarget = mob.mLevel->fetchEntity(mob.mTargetId, false);
    }
    triggerParams.mBlock = &targetPos;
    ActorDefinitionDescriptor::executeTrigger(mob, this->mOnTake, triggerParams);

    // 发布 AfterEvent
    MobTakeBlockAfterEvent afterEvent(mob, targetPos, block);
    bus.publish(afterEvent);
}

CATALYST_HOOKED_EVENT_PAIR(
    MobTakeBlockBeforeEvent,
    MobTakeBlockAfterEvent,
    MobTakeBlockEventHook
)

} // namespace Catalyst
