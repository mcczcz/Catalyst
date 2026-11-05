#include "SculkCatalystAbsorbExperienceEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/events/gameevents/GameEvent.h"
#include "mc/world/events/gameevents/GameEventContext.h"
#include "mc/world/events/gameevents/game_event_config/GameEventType.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/SculkCatalystBlockActor.h"

#include "ll/api/event/EventRefObjSerializer.h"
#include "mc/deps/nbt/CompoundTag.h"


namespace Catalyst {

void SculkCatalystAbsorbExperienceEvent::serialize(CompoundTag& nbt) const {
    ll::event::world::LevelEvent::serialize(nbt);
    nbt["blockActor"] = ll::event::serializeRefObj(blockActor());
    nbt["actor"]      = ll::event::serializeRefObj(actor());
}


LL_TYPE_INSTANCE_HOOK(
    SculkCatalystAbsorbExperienceEventHook,
    ll::memory::HookPriority::Normal,
    SculkCatalystBlockActor,
    &SculkCatalystBlockActor::$handleGameEvent,
    void,
    GameEvent const&        gameEvent,
    GameEventContext const& gameEventContext,
    BlockSource&            region
) {
    auto* actor = gameEventContext.mSource;
    if (gameEvent.mType != GameEventConfig::GameEventType::EntityDie || actor == nullptr) {
        origin(gameEvent, gameEventContext, region);
        return;
    }

    auto& bus   = ll::event::EventBus::getInstance();
    auto& level = region.getLevel();

    SculkCatalystAbsorbExperienceBeforeEvent beforeEvent(*this, level, *actor);
    bus.publish(beforeEvent);

    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(gameEvent, gameEventContext, region);

    SculkCatalystAbsorbExperienceAfterEvent afterEvent(*this, level, *actor);
    bus.publish(afterEvent);
}

CATALYST_HOOKED_EVENT_PAIR(
    SculkCatalystAbsorbExperienceBeforeEvent,
    SculkCatalystAbsorbExperienceAfterEvent,
    SculkCatalystAbsorbExperienceEventHook
)

} // namespace Catalyst
