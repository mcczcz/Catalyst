#include "PlayerInteractEntityEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"


#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void PlayerInteractEntityEvent::serialize(CompoundTag& nbt) const {
    ll::event::PlayerEvent::serialize(nbt);
    nbt["actor"]    = ll::event::serializeRefObj(actor());
    nbt["location"] = ListTag{location().x, location().y, location().z};
}

void PlayerInteractEntityAfterEvent::serialize(CompoundTag& nbt) const {
    PlayerInteractEntityEvent::serialize(nbt);
    nbt["resultSuccess"] = result().mSuccess;
    nbt["resultSwing"]   = result().mSwing;
}


LL_TYPE_INSTANCE_HOOK(
    InteractEntityHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::interact,
    InteractionResult,
    Actor&            actor,
    Vec3 const&       location
#ifdef LL_PLAT_S
    , HandSlot        handSlot
#endif
) {
    auto& bus = ll::event::EventBus::getInstance();

    PlayerInteractEntityBeforeEvent beforeEvent(*this, actor, location);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return InteractionResult{};
    }

    InteractionResult result = origin(actor, location
#ifdef LL_PLAT_S
        , handSlot
#endif
    );

    PlayerInteractEntityAfterEvent afterEvent(*this, actor, location, result);
    bus.publish(afterEvent);

    return result;
}

CATALYST_HOOKED_EVENT_PAIR(
    PlayerInteractEntityBeforeEvent,
    PlayerInteractEntityAfterEvent,
    InteractEntityHook
)

} // namespace Catalyst
