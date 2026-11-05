#include "BlockExplodedEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/events/ExplosionStartedEvent.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Explosion.h"
#include "mc/world/level/block/Block.h"

#include "mc/deps/nbt/CompoundTag.h"
/*
namespace Catalyst {

void BlockExplodedEvent::serialize(CompoundTag& nbt) const {
    ll::event::world::WorldEvent::serialize(nbt);
    nbt["pos"] = ListTag{pos().x, pos().y, pos().z};
}


std::unordered_set<::BlockPos>* affectedBlocks = nullptr;
BlockSource*                    region         = nullptr;

LL_TYPE_INSTANCE_HOOK(
    BlockExplodedHook1,
    ll::memory::HookPriority::Normal,
    Explosion,
    &Explosion::explode,
    bool,
    ::IRandom& random
) {
    affectedBlocks = &mAffectedBlocks.get();
    region         = &mRegion;
    auto res       = origin(random);
    affectedBlocks = nullptr;
    region         = nullptr;
    return res;
}

LL_TYPE_INSTANCE_HOOK(
    BlockExplodedHook2,
    ll::memory::HookPriority::Normal,
    ExplosionStartedEvent,
    &ExplosionStartedEvent::$dtor,
    void
) {
    if (!affectedBlocks || !region) {
        return origin();
    }

    auto&                        bus = ll::event::EventBus::getInstance();
    std::unordered_set<BlockPos> replaced;
    for (const auto& pos : *affectedBlocks) {
        BlockExplodedBeforeEvent beforeEvent(*region, pos);
        bus.publish(beforeEvent);
        if (!beforeEvent.isCancelled()) {
            replaced.emplace(pos);
        }
    }
    *affectedBlocks = replaced;

    origin();

    for (const auto& pos : replaced) {
        BlockExplodedAfterEvent afterEvent(*region, pos);
        bus.publish(afterEvent);
    }
}

CATALYST_HOOKED_EVENT_PAIR(BlockExplodedBeforeEvent, BlockExplodedAfterEvent, BlockExplodedHook1, BlockExplodedHook2)

} // namespace Catalyst
*/