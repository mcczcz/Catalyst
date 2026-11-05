#include "SculkBlockGrowthEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/BlockVolumeTarget.h"
#include "mc/world/level/IBlockWorldGenAPI.h"
#include "mc/world/level/TransactionalWorldBlockTarget.h"
#include "mc/world/level/WorldBlockTarget.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/SculkBlockBehavior.h"

#include "mc/deps/nbt/CompoundTag.h"

namespace Catalyst {

void SculkBlockGrowthEvent::serialize(CompoundTag& nbt) const {
    ll::event::world::WorldEvent::serialize(nbt);
    nbt["pos"] = ListTag{pos().x, pos().y, pos().z};
}

namespace {

struct SculkGrowthContext {
    ::IBlockWorldGenAPI* target;
    ::BlockSource*       region;
    ::BlockPos           pos;
};

thread_local SculkGrowthContext* currentContext = nullptr;

class ContextScope {
    SculkGrowthContext* mPrevious;

public:
    explicit ContextScope(SculkGrowthContext& context) : mPrevious(currentContext) { currentContext = &context; }
    ~ContextScope() { currentContext = mPrevious; }
};

bool isGrowthBlock(::Block const& block) {
    auto const& name = block.getTypeName();
    return name == "minecraft:sculk_sensor" || name == "minecraft:sculk_shrieker" || name == "sculk_sensor"
        || name == "sculk_shrieker";
}

bool isGrowthPosition(::BlockPos const& placed, ::BlockPos const& origin) {
    return placed.x == origin.x && placed.y == origin.y + 1 && placed.z == origin.z;
}

template <typename Forward>
bool dispatchGrowth(::BlockPos const& placedPos, ::Block const& newBlock, Forward&& forward) {
    auto* context = currentContext;
    if (context == nullptr || context->target == nullptr || !isGrowthBlock(newBlock)
        || !isGrowthPosition(placedPos, context->pos)) {
        return forward();
    }

    if (context->region == nullptr) {
        return forward();
    }

    auto& bus        = ll::event::EventBus::getInstance();
    auto  regionWeak = context->region->getWeakRef();

    SculkBlockGrowthBeforeEvent beforeEvent(*context->region, context->pos);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return false;
    }

    bool result = forward();

    auto lockedRegion = regionWeak.lock();
    if (lockedRegion) {
        SculkBlockGrowthAfterEvent afterEvent(*lockedRegion, context->pos);
        bus.publish(afterEvent);
    }
    return result;
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    SculkBlockGrowthChargeHook,
    ll::memory::HookPriority::Normal,
    SculkBlockBehavior,
    &SculkBlockBehavior::$attemptUseCharge,
    int,
    ::IBlockWorldGenAPI& target,
    ::BlockSource*       region,
    ::BlockPos const&    originPos,
    ::BlockPos const&    pos,
    int                  charge,
    int                  decay,
    ::Random&            random,
    ::SculkSpreader&     spreader,
    bool const           isWorldGen
) {
    SculkGrowthContext context{&target, region, originPos};
    ContextScope       scope(context);
    return origin(target, region, originPos, pos, charge, decay, random, spreader, isWorldGen);
}

#define CATALYST_SCULK_SET_BLOCK_HOOK(HookName, TargetType)                                                            \
    LL_TYPE_INSTANCE_HOOK(                                                                                             \
        HookName,                                                                                                      \
        ll::memory::HookPriority::Normal,                                                                              \
        TargetType,                                                                                                    \
        &TargetType::$setBlock,                                                                                        \
        bool,                                                                                                          \
        ::BlockPos const& pos,                                                                                         \
        ::Block const&    newBlock,                                                                                    \
        int               updateFlags                                                                                  \
    ) {                                                                                                                \
        if (currentContext == nullptr || currentContext->target != static_cast<::IBlockWorldGenAPI*>(this)) {          \
            return origin(pos, newBlock, updateFlags);                                                                 \
        }                                                                                                              \
        return dispatchGrowth(pos, newBlock, [&] { return origin(pos, newBlock, updateFlags); });                      \
    }

CATALYST_SCULK_SET_BLOCK_HOOK(SculkBlockGrowthWorldTargetHook, WorldBlockTarget)
CATALYST_SCULK_SET_BLOCK_HOOK(SculkBlockGrowthTransactionalTargetHook, TransactionalWorldBlockTarget)
CATALYST_SCULK_SET_BLOCK_HOOK(SculkBlockGrowthVolumeTargetHook, BlockVolumeTarget)

#undef CATALYST_SCULK_SET_BLOCK_HOOK

CATALYST_HOOKED_EVENT_PAIR(
    SculkBlockGrowthBeforeEvent,
    SculkBlockGrowthAfterEvent,
    SculkBlockGrowthChargeHook,
    SculkBlockGrowthWorldTargetHook,
    SculkBlockGrowthTransactionalTargetHook,
    SculkBlockGrowthVolumeTargetHook
)

} // namespace Catalyst
