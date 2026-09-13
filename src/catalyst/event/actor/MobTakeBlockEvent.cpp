#include "MobTakeBlockEvent.h"

#include <algorithm>
#include <variant>

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/base/ScopedValue.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/EventRefObjSerializer.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/ecs/WeakEntityRef.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/util/BlockUtils.h"
#include "mc/util/NamedMolangScript.h"
#include "mc/util/VariantParameterList.h"
#include "mc/world/actor/ActorDefinitionDescriptor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/goal/TakeBlockGoal.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/ShapeType.h"
#include "mc/world/level/block/ActorChangeContext.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockDescriptor.h"

namespace Catalyst {

void MobTakeBlockEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::MobEvent::serialize(nbt);
    nbt["pos"]   = ListTag{pos().x, pos().y, pos().z};
    nbt["block"] = ll::event::serializeRefObj(block());
}

namespace {

enum class TakeBlockPhase {
    Selecting,
    Checking,
    AwaitingRemoval,
    Finished,
};

struct TakeBlockAttempt {
    TakeBlockAttempt*    parent;
    TakeBlockGoal&       goal;
    Mob&                 mob;
    BlockSource&         source;
    WeakEntityRef        mobRef;
    WeakRef<BlockSource> sourceRef;
    BlockPos             pos{};
    Block const*         block     = nullptr;
    TakeBlockPhase       phase     = TakeBlockPhase::Selecting;
    bool                 removed   = false;
    bool                 triggered = false;
};

thread_local TakeBlockAttempt* currentTakeBlockAttempt = nullptr;

} // namespace

LL_TYPE_INSTANCE_HOOK(TakeBlockGoalTickHook, HookPriority::Normal, TakeBlockGoal, &TakeBlockGoal::$tick, void) {
    Mob& mob = mMob;
    for (auto* attempt = currentTakeBlockAttempt; attempt; attempt = attempt->parent) {
        // Before 监听器重入同一生物时，不能绕过尚未确定的取消结果。
        if (&attempt->mob == &mob) {
            return;
        }
    }

    auto& source = mob.getDimensionBlockSource();
    TakeBlockAttempt
        attempt{currentTakeBlockAttempt, *this, mob, source, mob.getEntityContext().getWeakRef(), source.getWeakRef()};
    {
        ll::ScopedValue scope(currentTakeBlockAttempt, &attempt);
        // 保留原版随机选点、ActorGriefingBlockEvent、携带物/装备包和 mOnTake 的完整流程。
        origin();
    }

    if (attempt.removed && attempt.triggered) {
        if (auto currentMob = attempt.mobRef.tryUnwrap<Mob>()) {
            MobTakeBlockAfterEvent afterEvent(*currentMob, attempt.pos, *attempt.block);
            ll::event::EventBus::getInstance().publish(afterEvent);
        }
    }
}

LL_TYPE_INSTANCE_HOOK(
    MobTakeBlockTargetHook,
    HookPriority::Normal,
    BlockSource,
    &BlockSource::$getBlock,
    Block const&,
    BlockPos const& pos
) {
    auto* attempt = currentTakeBlockAttempt;
    if (!attempt || attempt->phase != TakeBlockPhase::Selecting || &attempt->source != this) {
        return origin(pos);
    }

    // tick 的首次方块读取就是随机选中的目标；嵌套读取和监听器操作不再拦截。
    attempt->phase    = TakeBlockPhase::Checking;
    auto const& block = origin(pos);
    if (block.isAir()) {
        return block;
    }

    auto const& validBlocks = attempt->goal.mValidBlocks.get();
    if (!validBlocks.empty() && std::none_of(validBlocks.begin(), validBlocks.end(), [&](auto const& descriptor) {
            return descriptor.matches(block);
        })) {
        return block;
    }
    if (attempt->goal.mRequiresLineOfSight && !BlockUtils::canSee(attempt->mob, pos, ShapeType::Collision)) {
        return block;
    }

    auto air = Block::tryGetFromRegistry("minecraft:air");
    if (!air) {
        return block;
    }

    attempt->pos   = pos;
    attempt->block = &block;
    MobTakeBlockBeforeEvent beforeEvent(attempt->mob, attempt->pos, block);
    ll::event::EventBus::getInstance().publish(beforeEvent);

    auto source = attempt->sourceRef.lock();
    if (beforeEvent.isCancelled() || !attempt->mobRef.tryUnwrap<Mob>() || !source
        || &source->getBlock(attempt->pos) != &block) {
        // 只替换本次读取的返回值，原版会在空气检查处退出，世界中的方块不受影响。
        attempt->phase = TakeBlockPhase::Finished;
        return *air;
    }

    attempt->phase = TakeBlockPhase::AwaitingRemoval;
    return block;
}

LL_TYPE_INSTANCE_HOOK(
    MobTakeBlockRemoveHook,
    HookPriority::Normal,
    BlockSource,
    &BlockSource::$removeBlock,
    bool,
    BlockPos const&           pos,
    BlockChangeContext const& context
) {
    auto*       attempt      = currentTakeBlockAttempt;
    auto const* actorContext = std::get_if<ActorChangeContext>(&context.mContextSource.get());
    if (!attempt || attempt->phase != TakeBlockPhase::AwaitingRemoval || &attempt->source != this || attempt->pos != pos
        || !actorContext || actorContext->mActorContext != &attempt->mob || &getBlock(pos) != attempt->block) {
        return origin(pos, context);
    }

    attempt->phase = TakeBlockPhase::Finished;
    bool result    = origin(pos, context);
    auto source    = attempt->sourceRef.lock();
    // 原生事件取消或 removeBlock 被其他插件拦截时，不发布 After。
    attempt->removed = result && source && source->getBlock(attempt->pos).isAir();
    return result;
}

LL_STATIC_HOOK(
    MobTakeBlockTriggerHook,
    HookPriority::Normal,
    &ActorDefinitionDescriptor::executeTrigger,
    bool,
    Actor&                        actor,
    ActorDefinitionTrigger const& trigger,
    VariantParameterList const&   params
) {
    auto* attempt = currentTakeBlockAttempt;
    if (attempt && attempt->phase == TakeBlockPhase::Finished && attempt->removed && &attempt->mob == &actor
        && &attempt->goal.mOnTake.get() == &trigger && params.mBlock && *params.mBlock == attempt->pos) {
        // 只有原版放行拿取后才执行 mOnTake；原生事件监听器自行移除方块不能算作拿取成功。
        attempt->triggered = true;
    }
    return origin(actor, trigger, params);
}

CATALYST_HOOKED_EVENT_PAIR(
    MobTakeBlockBeforeEvent,
    MobTakeBlockAfterEvent,
    TakeBlockGoalTickHook,
    MobTakeBlockTargetHook,
    MobTakeBlockRemoveHook,
    MobTakeBlockTriggerHook
)

} // namespace Catalyst
