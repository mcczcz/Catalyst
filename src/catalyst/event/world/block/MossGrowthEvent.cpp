#include "MossGrowthEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/level/WorldBlockTarget.h"
#include "mc/world/level/levelgen/feature/IFeature.h"
#include "mc/world/level/levelgen/feature/VegetationPatchFeature.h"
#include "mc/util/IntRange.h"

#include "mc/deps/nbt/CompoundTag.h"

namespace Catalyst {

void MossGrowthEvent::serialize(CompoundTag& nbt) const {
    ll::event::world::WorldEvent::serialize(nbt);
    nbt["origin"]  = ListTag{origin().x, origin().y, origin().z};
    nbt["xRadius"] = xRadius();
    nbt["zRadius"] = zRadius();
}


// 26.32 适配：VegetationPatchFeature::_placeGroundPatch 等辅助函数已全部被
// 内联进虚函数 place()。改为拦截 place() 本体：
// - patchOrigin 取自 PlacementContext 的位置；
// - x/z 半径按 mHorizontalRadius 配置区间的中值估算（事件字段仅作信息用途；
//   不预抽随机数，以免扰动原版 RNG 序列影响世界生成确定性）；
// - 取消 Before = 整个植被贴片不生成（place 返回 nullopt）；
// - place 成功返回时贴片必然已落地，此时发布 After。
LL_TYPE_INSTANCE_HOOK(
    MossGrowthEventHook,
    HookPriority::Normal,
    VegetationPatchFeature,
    &VegetationPatchFeature::$place,
    ::std::optional<::BlockPos>,
    ::IFeature::PlacementContext const& context
) {
    auto& region = static_cast<WorldBlockTarget&>(context.mTarget).mBlockSource;
    auto& bus    = ll::event::EventBus::getInstance();

    BlockPos const& patchOrigin = context.mPos;
    auto const&     range       = this->mHorizontalRadius.get();
    int const       radius      = (static_cast<int>(range.rangeMin) + static_cast<int>(range.rangeMax)) / 2;

    MossGrowthBeforeEvent beforeEvent(region, patchOrigin, radius, radius);
    bus.publish(beforeEvent);

    if (beforeEvent.isCancelled()) {
        return std::nullopt;
    }

    auto result = origin(context);

    if (result.has_value()) {
        MossGrowthAfterEvent afterEvent(region, patchOrigin, radius, radius);
        bus.publish(afterEvent);
    }

    return result;
}

CATALYST_HOOKED_EVENT_PAIR(
    MossGrowthBeforeEvent,
    MossGrowthAfterEvent,
    MossGrowthEventHook
)

} // namespace Catalyst
