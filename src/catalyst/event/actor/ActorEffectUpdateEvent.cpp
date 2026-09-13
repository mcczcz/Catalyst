#include "ActorEffectUpdateEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/EventRefObjSerializer.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/effect/MobEffectInstance.h"


namespace Catalyst {

void ActorEffectAddEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::ActorEvent::serialize(nbt);
    nbt["effect"] = ll::event::serializeRefObj(effect());
}

void ActorEffectUpdateEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::ActorEvent::serialize(nbt);
    nbt["effect"] = ll::event::serializeRefObj(effect());
}

void ActorEffectRemoveEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::ActorEvent::serialize(nbt);
    nbt["effect"] = ll::event::serializeRefObj(effect());
}


LL_TYPE_INSTANCE_HOOK(
    ActorEffectAddEventHook,
    ll::memory::HookPriority::Normal,
    Actor,
    &Actor::addEffect,
    void,
    ::MobEffectInstance const& effect
) {
    auto& bus = ll::event::EventBus::getInstance();

    ActorEffectAddBeforeEvent beforeEvent(*this, effect);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(effect);

    ActorEffectAddAfterEvent afterEvent(*this, effect);
    bus.publish(afterEvent);
}


LL_TYPE_INSTANCE_HOOK(
    ActorEffectRemoveEventHook,
    ll::memory::HookPriority::Normal,
    Actor,
    &Actor::$onEffectRemoved,
    void,
    ::MobEffectInstance& effect
) {
    auto& bus = ll::event::EventBus::getInstance();

    ActorEffectRemoveBeforeEvent beforeEvent(*this, effect);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(effect);

    ActorEffectRemoveAfterEvent afterEvent(*this, effect);
    bus.publish(afterEvent);
}

CATALYST_HOOKED_EVENT_PAIR(ActorEffectAddBeforeEvent, ActorEffectAddAfterEvent, ActorEffectAddEventHook)
CATALYST_HOOKED_EVENT_PAIR(ActorEffectRemoveBeforeEvent, ActorEffectRemoveAfterEvent, ActorEffectRemoveEventHook)

} // namespace Catalyst
