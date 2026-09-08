#if defined(__clang__)
// The generated vector storage header requires this otherwise-unused type to be complete.
struct NamedMolangScript {};
#endif

#include "MobTakeBlockEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include <algorithm>
#include "mc/util/Random.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/actor/ActorDefinitionDescriptor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/goal/TakeBlockGoal.h"
#include "mc/world/events/gameevents/GameEventRegistry.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
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



LL_TYPE_INSTANCE_HOOK(TakeBlockGoalTickHook, HookPriority::Normal, TakeBlockGoal, &TakeBlockGoal::$tick, void) {
    auto&     level  = mMob.getLevel();
    auto&     random = level.getRandom();
    auto&     mobPos = mMob.getPosition();
    BlockPos  targetPos(mobPos);

    // 26.32 适配：Definition 嵌套结构已拍平到 TakeBlockGoal 自身成员
    auto& xzRange = mXZRange.get();
    auto& yRange  = mYRange.get();

    int xzMin = xzRange.rangeMin;
    int xzMax = xzRange.rangeMax;
    int yMin  = yRange.rangeMin;
    int yMax  = yRange.rangeMax;

    if (xzMin < xzMax) {
        targetPos.x += random.nextInt(xzMax - xzMin + 1) + xzMin;
    }
    if (yMin < yMax) {
        targetPos.y += random.nextInt(yMax - yMin + 1) + yMin;
    }
    if (xzMin < xzMax) {
        targetPos.z += random.nextInt(xzMax - xzMin + 1) + xzMin;
    }

    auto& blockSource = mMob.getDimensionBlockSource();
    auto& block       = blockSource.getBlock(targetPos);

    if (block.isAir()) {
        return;
    }

    auto& validBlocks = mValidBlocks.get();
    if (!validBlocks.empty()) {
        // 26.32 适配：静态 BlockDescriptor::anyMatch 已移除，改为逐个 matches
        bool matched = std::any_of(
            validBlocks.begin(),
            validBlocks.end(),
            [&block](BlockDescriptor const& desc) { return desc.matches(block); }
        );
        if (!matched) {
            return;
        }
    }

    if (mRequiresLineOfSight) {
        Vec3 blockCenter((float)targetPos.x, (float)targetPos.y, (float)targetPos.z);
        if (!mMob.canSee(blockCenter, ShapeType::Collision)) {
            return;
        }
    }

    auto& bus = ll::event::EventBus::getInstance();

    // 发布 BeforeEvent
    MobTakeBlockBeforeEvent beforeEvent(mMob, targetPos, block);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    // 26.32 适配：ActorGriefingBlockEvent 的原版否决派发已移除，Catalyst 的 Before 事件继续提供取消能力。

    // 创建物品并添加到生物背包
    // 26.32 适配：ItemStack(Block,int,CompoundTag*) 构造已移至 ItemStackBase，改用默认构造 + reinit
    ItemStack item;
    item.reinit(block, 1);
    mMob.add(item);

    // 移除方块
    BlockChangeContext changeContext{};
    changeContext.mContextSource = ActorChangeContext{&mMob};
    blockSource.removeBlock(targetPos, changeContext);

    // 发送游戏事件
    blockSource.postGameEvent(&mMob, GameEventRegistry::blockDestroy(), targetPos, &block);

    // 执行触发器
    VariantParameterList params;
    params.mSelf  = &mMob;
    params.mBlock = &targetPos;
    auto& trigger = mOnTake.get();
    ActorDefinitionDescriptor::executeTrigger(mMob, trigger, params);

    // 发布 AfterEvent
    MobTakeBlockAfterEvent afterEvent(mMob, targetPos, block);
    bus.publish(afterEvent);
}

CATALYST_HOOKED_EVENT_PAIR(
    MobTakeBlockBeforeEvent,
    MobTakeBlockAfterEvent,
    TakeBlockGoalTickHook
)

} // namespace Catalyst
