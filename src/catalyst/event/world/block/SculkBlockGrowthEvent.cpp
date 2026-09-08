#include "SculkBlockGrowthEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/level/block/SculkBlockBehavior.h"

#include "mc/deps/nbt/CompoundTag.h"

namespace Catalyst {

void SculkBlockGrowthEvent::serialize(CompoundTag& nbt) const {
    ll::event::world::WorldEvent::serialize(nbt);
    nbt["pos"] = ListTag{pos().x, pos().y, pos().z};
}


// 26.32 适配：SculkBlockBehavior::_placeGrowthAt（静态）已被内联进虚函数
// attemptUseCharge（幽匿电荷消耗并放置生长的入口）。改为拦截 attemptUseCharge：
// - region 为空的生成路径直接放行（与旧实现一致，不发事件）；
// - 取消 Before = 本次不放置生长、不消耗电荷（返回 0）；
// - origin 返回值 > 0 视为生长确实发生，此时发布 After。
LL_TYPE_INSTANCE_HOOK(
    SculkBlockGrowthHook,
    ll::memory::HookPriority::Normal,
    SculkBlockBehavior,
    &SculkBlockBehavior::$attemptUseCharge,
    int,
    ::IBlockWorldGenAPI& target,
    ::BlockSource*       region,
    ::BlockPos const&    originPos,
    ::BlockPos const&    pos,
    int                  charge,
    int                  unusedArg,
    ::Random&            random,
    ::SculkSpreader&     spreader,
    bool const           flag
) {
    if (region == nullptr) {
        // Some worldgen paths provide a null BlockSource pointer.
        // Preserve game behavior, but skip publishing events that require BlockSource&.
        return origin(target, region, originPos, pos, charge, unusedArg, random, spreader, flag);
    }

    auto& bus = ll::event::EventBus::getInstance();

    WeakRef<BlockSource> regionWeak = region->getWeakRef();

    SculkBlockGrowthBeforeEvent beforeEvent(*region, pos);
    bus.publish(beforeEvent);

    if (beforeEvent.isCancelled()) {
        return 0;
    }

    int result = origin(target, region, originPos, pos, charge, unusedArg, random, spreader, flag);

    if (result > 0) {
        auto lockedRegion = regionWeak.lock();
        if (!lockedRegion) {
            // attemptUseCharge may invalidate the original BlockSource on some worldgen paths.
            // Do not publish an AfterEvent with a dangling WorldEvent::blockSource().
            return result;
        }
        SculkBlockGrowthAfterEvent afterEvent(*lockedRegion, pos);
        bus.publish(afterEvent);
    }

    return result;
}

CATALYST_HOOKED_EVENT_PAIR(
    SculkBlockGrowthBeforeEvent,
    SculkBlockGrowthAfterEvent,
    SculkBlockGrowthHook
)

} // namespace Catalyst
