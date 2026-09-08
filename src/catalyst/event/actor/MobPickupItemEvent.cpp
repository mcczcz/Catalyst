#include "MobPickupItemEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/item/ItemActor.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void MobPickupItemEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::ActorEvent::serialize(nbt);
    nbt["itemActor"] = ll::event::serializeRefObj(itemActor());
}


// 26.32 适配：PickupItemsGoal::_pickItemUp 已被内联进 tick，无法再单独 hook。
// 改为拦截 Actor::pickUpItem（生物/玩家从地面拾取物品的统一入口，26.20 中
// _pickItemUp 内部同样经由它完成拾取），并过滤掉玩家以保持原有事件语义。
LL_TYPE_INSTANCE_HOOK(
    MobPickupItemHook,
    HookPriority::Normal,
    Actor,
    &Actor::pickUpItem,
    void,
    ::ItemActor& itemActor,
    int         count
) {
    if (this->isPlayer()) {
        origin(itemActor, count);
        return;
    }

    auto& bus = ll::event::EventBus::getInstance();

    MobPickupItemBeforeEvent beforeEvent(*this, itemActor);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(itemActor, count);

    MobPickupItemAfterEvent afterEvent(*this, itemActor);
    bus.publish(afterEvent);
}

CATALYST_HOOKED_EVENT_PAIR(
    MobPickupItemBeforeEvent,
    MobPickupItemAfterEvent,
    MobPickupItemHook
)

} // namespace Catalyst
