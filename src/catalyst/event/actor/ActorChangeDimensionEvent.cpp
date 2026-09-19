#include "ActorChangeDimensionEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/ActorDimensionTransferRequest.h"
#include "mc\world\level\ActorDimensionTransferer.h"
#include "mc/world/level/block/BlockType.h"
#include "mc\world\level\BlockPalette.h"

namespace Catalyst {

void ActorChangeDimensionEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::ActorEvent::serialize(nbt);
    nbt["fromDimensionId"] = fromDimensionId();
    nbt["toDimensionId"]   = toDimensionId();
}

LL_TYPE_INSTANCE_HOOK(
    ActorChangeDimensionEventHook,
    HookPriority::Normal,
    Level,
    &Level::$entityChangeDimension,
    void,
    class Actor&                              entity,
    ActorDimensionTransferRequest const&     transferRequest
) {
    int fromId  = entity.getDimensionId();
    int toIdInt = static_cast<int>(transferRequest.mToId.get());

    auto& bus = ll::event::EventBus::getInstance();

    ActorChangeDimensionBeforeEvent beforeEvent(entity, fromId, toIdInt);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(entity, transferRequest);

    ActorChangeDimensionAfterEvent afterEvent(entity, fromId, toIdInt);
    bus.publish(afterEvent);
}

CATALYST_HOOKED_EVENT_PAIR(
    ActorChangeDimensionBeforeEvent,
    ActorChangeDimensionAfterEvent,
    ActorChangeDimensionEventHook
)

} // namespace Catalyst
