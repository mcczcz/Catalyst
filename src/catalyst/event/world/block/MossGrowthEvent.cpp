#include "MossGrowthEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/core/math/Random.h"
#include "mc/util/IntRange.h"
#include "mc/util/Random.h"
#include "mc/world/level/WorldBlockTarget.h"
#include "mc/world/level/levelgen/feature/IFeature.h"
#include "mc/world/level/levelgen/feature/VegetationPatchFeature.h"

#include "mc/deps/nbt/CompoundTag.h"

#include <cstring>

namespace Catalyst {

void MossGrowthEvent::serialize(CompoundTag& nbt) const {
    ll::event::world::WorldEvent::serialize(nbt);
    nbt["origin"]  = ListTag{origin().x, origin().y, origin().z};
    nbt["xRadius"] = xRadius();
    nbt["zRadius"] = zRadius();
}

namespace {

// place() rolls xRadius then zRadius from the context RNG before touching any block, via
// Random::nextInt(n) == _genRandInt32() % n. Roll them on a copy of the MT state so the event
// can report the real radii without advancing the world-gen generator.
int rollRadius(Core::Random& snapshot, IntRange const& range) {
    int const min = range.rangeMin;
    int const max = range.rangeMax;
    if (min >= max - 1) return min;
    return min + static_cast<int>(snapshot._genRandInt32(false) % static_cast<uint>(max - min));
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    MossGrowthEventHook,
    ll::memory::HookPriority::Normal,
    VegetationPatchFeature,
    &VegetationPatchFeature::$place,
    std::optional<BlockPos>,
    ::IFeature::PlacementContext const& context
) {
    auto& region = static_cast<WorldBlockTarget&>(static_cast<IBlockWorldGenAPI&>(context.mTarget)).mBlockSource;
    auto& bus    = ll::event::EventBus::getInstance();

    alignas(Core::Random) std::byte buf[sizeof(Core::Random)];
    std::memcpy(
        buf,
        static_cast<void const*>(&static_cast<Random&>(context.mRandom).mRandom->mObject),
        sizeof(Core::Random)
    );
    auto& snapshot = *reinterpret_cast<Core::Random*>(buf);

    int const xRadius = rollRadius(snapshot, mHorizontalRadius);
    int const zRadius = rollRadius(snapshot, mHorizontalRadius);

    MossGrowthBeforeEvent beforeEvent(region, context.mPos, xRadius, zRadius);
    bus.publish(beforeEvent);

    if (beforeEvent.isCancelled()) {
        return std::nullopt;
    }

    auto result = origin(context);

    MossGrowthAfterEvent afterEvent(region, context.mPos, xRadius, zRadius);
    bus.publish(afterEvent);

    return result;
}

CATALYST_HOOKED_EVENT_PAIR(
    MossGrowthBeforeEvent,
    MossGrowthAfterEvent,
    MossGrowthEventHook
)

} // namespace Catalyst
