#include "BlockExplodedEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/gameplayhandlers/CoordinatorResult.h"
#include "mc/world/events/BlockEventCoordinator.h"
#include "mc/world/events/EventRef.h"
#include "mc/world/events/ExplosionStartedEvent.h"
#include "mc/world/events/MutableBlockGameplayEvent.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Explosion.h"
#include "mc/world/level/block/Block.h"

#include "mc/deps/nbt/CompoundTag.h"

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

// 26.32 起 ExplosionStartedEvent 的析构函数被内联，无法再 hook $dtor。
// 改为 hook BlockEventCoordinator 的可变事件 sendEvent：ExplosionStartedEvent
// 现在经 MutableBlockGameplayEvent 变体分发，origin 返回后再过滤事件中的方块集合。
LL_TYPE_INSTANCE_HOOK(
    BlockExplodedHook2,
    ll::memory::HookPriority::Normal,
    BlockEventCoordinator,
    static_cast<::CoordinatorResult (BlockEventCoordinator::*)(
        ::EventRef<::MutableBlockGameplayEvent<::CoordinatorResult>>)>(
        &BlockEventCoordinator::sendEvent
    ),
    ::CoordinatorResult,
    ::EventRef<::MutableBlockGameplayEvent<::CoordinatorResult>> event
) {
    auto result = origin(event);

    if (!affectedBlocks || !region) {
        return result;
    }

    event.get().visit([&](auto&& arg) {
        using CurrentEventType = std::decay_t<decltype(arg.value())>;
        if constexpr (std::is_same_v<CurrentEventType, ExplosionStartedEvent>) {
            auto& startedEvent = arg.value();
            auto& bus          = ll::event::EventBus::getInstance();

            std::unordered_set<BlockPos> replaced;
            for (const auto& pos : startedEvent.mBlocks.get()) {
                BlockExplodedBeforeEvent beforeEvent(*region, pos);
                bus.publish(beforeEvent);
                if (!beforeEvent.isCancelled()) {
                    replaced.emplace(pos);
                }
            }

            startedEvent.mBlocks = replaced;
            *affectedBlocks      = replaced;

            for (const auto& pos : replaced) {
                BlockExplodedAfterEvent afterEvent(*region, pos);
                bus.publish(afterEvent);
            }
        }
    });

    return result;
}

CATALYST_HOOKED_EVENT_PAIR(
    BlockExplodedBeforeEvent,
    BlockExplodedAfterEvent,
    BlockExplodedHook1,
    BlockExplodedHook2
)

} // namespace Catalyst
