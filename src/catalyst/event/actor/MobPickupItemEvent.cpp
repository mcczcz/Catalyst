
#include "MobPickupItemEvent.h"

#include <optional>

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/gameplayhandlers/CoordinatorResult.h"
#include "mc/gameplayhandlers/GameplayHandlerResult.h"
#include "mc/gameplayhandlers/HandlerResult.h"
#include "mc/scripting/event_handlers/ScriptActorGameplayHandler.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/goal/PickupItemsGoal.h"
#include "mc/world/actor/item/ItemActor.h"
#include "mc/world/events/ActorBeforeAcquireItemEvent.h"
#include "mc/world/item/ItemStack.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void MobPickupItemEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::ActorEvent::serialize(nbt);
    nbt["itemActor"] = ll::event::serializeRefObj(itemActor());
}

namespace {

// PickupItemsGoal::_pickItemUp 已被内联进 tick，唯一可拦截点是其内部派发的
// ActorBeforeAcquireItemEvent。Before 事件在此放行后记录目标，供 tick 结束时判定是否真正拾取。
struct PendingPickup {
    ItemActor* item;
    uchar      count;
    bool       removed;
};
thread_local std::optional<PendingPickup> sPendingPickup;

} // namespace

LL_TYPE_INSTANCE_HOOK(
    MobPickupItemBeforeHook,
    HookPriority::Normal,
    ScriptActorGameplayHandler,
    &ScriptActorGameplayHandler::$handleEvent,
    GameplayHandlerResult<CoordinatorResult>,
    ::ActorBeforeAcquireItemEvent& event
) {
    auto result = origin(event);
    if (result.return_value == CoordinatorResult::Cancel) {
        return result;
    }

    Actor& actor = event.mActor;
    Actor& item  = event.mItem;
    // 同一事件也由 Player::take 与 HopperComponent::pullInItems 派发，只保留非玩家生物拾取掉落物的情况。
    if (actor.isPlayer() || !actor.hasCategory(ActorCategory::Mob) || !item.isType(ActorType::ItemEntity)) {
        return result;
    }
    auto& itemActor = static_cast<ItemActor&>(item);

    MobPickupItemBeforeEvent beforeEvent(actor, itemActor);
    ll::event::EventBus::getInstance().publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return {HandlerResult::BypassListeners, CoordinatorResult::Cancel};
    }

    sPendingPickup = PendingPickup{&itemActor, itemActor.mItem->mCount, itemActor.mRemoved};
    return result;
}

LL_TYPE_INSTANCE_HOOK(MobPickupItemTickHook, HookPriority::Normal, PickupItemsGoal, &PickupItemsGoal::$tick, void) {
    sPendingPickup.reset();
    origin();
    if (!sPendingPickup) {
        return;
    }
    auto pending = *sPendingPickup;
    sPendingPickup.reset();

    // Before 事件放行后仍可能因 mStopIfHoldingItem、可拾取数量为 0 等原因未拾取，以掉落物数量减少或被移除为准。
    auto& itemActor = *pending.item;
    if ((pending.removed || !itemActor.mRemoved) && itemActor.mItem->mCount >= pending.count) {
        return;
    }

    MobPickupItemAfterEvent afterEvent(this->mMob, itemActor);
    ll::event::EventBus::getInstance().publish(afterEvent);
}

CATALYST_HOOKED_EVENT_PAIR(MobPickupItemBeforeEvent, MobPickupItemAfterEvent, MobPickupItemBeforeHook, MobPickupItemTickHook)

} // namespace Catalyst
