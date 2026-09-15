#include "PlayerStartSleepEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "catalyst/mod/Gloabl.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/actor/player/Player.h"


#include "mc/deps/nbt/CompoundTag.h"

namespace Catalyst {

void PlayerStartSleepEvent::serialize(CompoundTag& nbt) const {
    ll::event::PlayerEvent::serialize(nbt);
    nbt["bedBlockPos"] = ListTag{getBedBlockPos().x, getBedBlockPos().y, getBedBlockPos().z};
}

void PlayerStartSleepAfterEvent::serialize(CompoundTag& nbt) const {
    PlayerStartSleepEvent::serialize(nbt);
    nbt["result"] = magic_enum::enum_name(getResult());
}


LL_TYPE_INSTANCE_HOOK(
    PlayerStartSleepEventHook,
    ll::memory::HookPriority::Normal,
    Player,
    &Player::$startSleepInBed,
    ::BedSleepingResult,
    ::BlockPos const& bedBlockPos,
    bool              setsRespawn,
    float             sleepOffset
) {
    auto& bus           = ll::event::EventBus::getInstance();
    bool  canStartSleep = this->canStartSleepInBed();
    if (!canStartSleep) {
        return origin(bedBlockPos, setsRespawn, sleepOffset);
    }
    logger.debug(
        "PlayerStartSleepEvent: player={}, bedPos=({},{},{})",
        this->getRealName(),
        bedBlockPos.x,
        bedBlockPos.y,
        bedBlockPos.z
    );
    PlayerStartSleepBeforeEvent beforeEvent(*this, bedBlockPos);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return BedSleepingResult::OtherProblem;
    }

    BedSleepingResult result = origin(bedBlockPos, setsRespawn, sleepOffset);

    PlayerStartSleepAfterEvent afterEvent(*this, bedBlockPos, result);
    bus.publish(afterEvent);

    return result;
}

CATALYST_HOOKED_EVENT_PAIR(PlayerStartSleepBeforeEvent, PlayerStartSleepAfterEvent, PlayerStartSleepEventHook)

} // namespace Catalyst
