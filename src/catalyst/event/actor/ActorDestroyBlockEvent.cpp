#include "ActorDestroyBlockEvent.h"

#include <type_traits>

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/EventRefObjSerializer.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/ecs/WeakEntityRef.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/gameplayhandlers/ActorGameplayHandler.h"
#include "mc/gameplayhandlers/CoordinatorResult.h"
#include "mc/world/events/ActorEventListener.h"
#include "mc/world/events/ActorGameplayEvent.h"
#include "mc/world/events/ActorGriefingBlockEvent.h"
#include "mc/world/events/EventCoordinatorPimpl.h"
#include "mc/world/level/block/Block.h"

namespace Catalyst {

void ActorDestroyBlockEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::ActorEvent::serialize(nbt);
    nbt["pos"]   = ListTag{pos().x, pos().y, pos().z};
    nbt["block"] = ll::event::serializeRefObj(block());
}

LL_TYPE_INSTANCE_HOOK(
    ActorDestroyBlockEventHook,
    HookPriority::Normal,
    EventCoordinatorPimpl<ActorEventListener>,
    &EventCoordinatorPimpl<ActorEventListener>::_processEvent,
    CoordinatorResult,
    ActorGameplayHandler*                        handler,
    ActorGameplayEvent<CoordinatorResult> const& event
)
try {
    return event.visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, Details::ValueOrRef<ActorGriefingBlockEvent const>>) {
            ActorGriefingBlockEvent const& griefingEvent = arg.value();
            auto                           actor         = griefingEvent.mActorContext->tryUnwrap<Actor>();
            if (!actor) {
                return origin(handler, event);
            }
            auto&                        bus = ll::event::EventBus::getInstance();
            ActorDestroyBlockBeforeEvent beforeEvent(*actor, griefingEvent.mPos, *griefingEvent.mBlock.get());
            bus.publish(beforeEvent);
            if (beforeEvent.isCancelled()) {
                origin(handler, event);
                return CoordinatorResult::Cancel;
            }
            auto result = origin(handler, event);
            if (result != CoordinatorResult::Cancel) {
                if (auto currentActor = griefingEvent.mActorContext->tryUnwrap<Actor>()) {
                    ActorDestroyBlockAfterEvent afterEvent(
                        *currentActor,
                        griefingEvent.mPos,
                        *griefingEvent.mBlock.get()
                    );
                    bus.publish(afterEvent);
                }
            }
            return result;
        }
        return origin(handler, event);
    });
} catch (...) {
    return origin(handler, event);
}

CATALYST_HOOKED_EVENT_PAIR(ActorDestroyBlockBeforeEvent, ActorDestroyBlockAfterEvent, ActorDestroyBlockEventHook)

} // namespace Catalyst
