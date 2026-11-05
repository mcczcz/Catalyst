#include "PlayerUseShelfBlockEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/level/block/ShelfBlock.h"
#include "mc/world/level/block/block_events/BlockPlayerInteractEvent.h"

namespace Catalyst {

void PlayerUseShelfBlockEvent::serialize(CompoundTag& nbt) const {
    ll::event::PlayerEvent::serialize(nbt);
    nbt["pos"] = ListTag{pos().x, pos().y, pos().z};
}

LL_TYPE_INSTANCE_HOOK(
    PlayerUseShelfBlockEventHook,
    ll::memory::HookPriority::Normal,
    ShelfBlock,
    &ShelfBlock::use,
    void,
    ::BlockEvents::BlockPlayerInteractEvent& eventData
) {
    auto& bus = ll::event::EventBus::getInstance();

    PlayerUseShelfBlockBeforeEvent beforeEvent(eventData.mPlayer, eventData.mPos);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(eventData);

    PlayerUseShelfBlockAfterEvent afterEvent(eventData.mPlayer, eventData.mPos);
    bus.publish(afterEvent);
}

CATALYST_HOOKED_EVENT_PAIR(PlayerUseShelfBlockBeforeEvent, PlayerUseShelfBlockAfterEvent, PlayerUseShelfBlockEventHook)

} // namespace Catalyst
