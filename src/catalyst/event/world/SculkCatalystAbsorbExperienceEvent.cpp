#include "SculkCatalystAbsorbExperienceEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/SculkCatalystBlockActor.h"
#include "mc/world/events/gameevents/GameEvent.h"
#include "mc/world/events/gameevents/GameEventContext.h"
#include "mc/world/events/gameevents/game_event_config/GameEventType.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void SculkCatalystAbsorbExperienceEvent::serialize(CompoundTag& nbt) const {
    ll::event::world::LevelEvent::serialize(nbt);
    nbt["blockActor"] = ll::event::serializeRefObj(blockActor());
    nbt["actor"]      = ll::event::serializeRefObj(actor());
}


// 26.32 适配：SculkCatalystBlockActor::_tryConsumeOnDeathExperience 已被内联进
// handleGameEvent 的 EntityDie 分支。改为拦截该分支：仅当死亡实体确实携带
// 经验值时发布事件（原版以 getOnDeathExperience 为吸收门槛）；取消 Before
// 即跳过本次吸收，经验球照常掉落。
LL_TYPE_INSTANCE_HOOK(
    SculkCatalystAbsorbExperienceEventHook,
    ll::memory::HookPriority::Normal,
    SculkCatalystBlockActor,
    &SculkCatalystBlockActor::$handleGameEvent,
    void,
    ::GameEvent const&        gameEvent,
    ::GameEventContext const& gameEventContext,
    ::BlockSource&            region
) {
    if (gameEvent.mType == GameEventConfig::GameEventType::EntityDie) {
        auto* actor = gameEventContext.mSource;
        if (actor && actor->getOnDeathExperience() > 0) {
            auto& bus   = ll::event::EventBus::getInstance();
            auto& level = actor->getLevel();

            SculkCatalystAbsorbExperienceBeforeEvent beforeEvent(*this, level, *actor);
            bus.publish(beforeEvent);
            if (beforeEvent.isCancelled()) {
                return;
            }

            origin(gameEvent, gameEventContext, region);

            SculkCatalystAbsorbExperienceAfterEvent afterEvent(*this, level, *actor);
            bus.publish(afterEvent);
            return;
        }
    }

    origin(gameEvent, gameEventContext, region);
}

CATALYST_HOOKED_EVENT_PAIR(
    SculkCatalystAbsorbExperienceBeforeEvent,
    SculkCatalystAbsorbExperienceAfterEvent,
    SculkCatalystAbsorbExperienceEventHook
)

} // namespace Catalyst
