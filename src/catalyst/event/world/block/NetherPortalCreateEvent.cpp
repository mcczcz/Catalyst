#include "NetherPortalCreateEvent.h"

#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/util/BlockChange.h"
#include "mc/util/WorldChangeTransaction.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/ActorDimensionTransferer.h"
#include "mc/world/level/ChangeDimensionRequest.h"
#include "mc/world/level/PlayerDimensionTransferManager.h"
#include "mc/world/level/PlayerDimensionTransferer.h"
#include "mc/world/level/PortalShape.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/PortalAxis.h"
#include "mc/world/level/block/VanillaBlockTypeIds.h"
#include "mc/world/level/dimension/DimensionType.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

// 26.32 适配说明：
//
// BDS 26.32 移除了 PortalForcer::createPortal / force / _findPortal 的独立符号（逻辑被内联进
// 维度传送流程），传送门方块的生成收敛到 PortalShape::createPortalBlocks(WorldChangeTransaction&)。
// 因此本事件改为：
//   1. hook ActorDimensionTransferer::findTargetPositionAndSetPosition（含内联 force 的实体传送路径）
//      与 PlayerDimensionTransferer::setTransitionLocation / _playerChangeDimension（玩家传送路径），
//      用线程局部上下文记录“当前正在传送的 Actor”；
//   2. hook PortalShape::createPortalBlocks：上下文存在时发布 Before/After 事件；
//      上下文不存在（例如打火石点燃黑曜石框架等非传送触发的生成）则原样放行。
//
// 与旧版（hook createPortal 整体替换）的行为差异：
//   - 取消 Before 事件后不再调用原函数，传送门方块不会生成；但内联的传送逻辑仍会按
//     未放置的形状计算落点（无法在不反编译的前提下干预内联代码）。
//   - radius 无法从内联代码中取得，按原版搜索半径常量 16 上报。
//   - forcedPlacement 为尽力而为的推断：若事务在进入时已包含预置方块（强制放置路径的平台
//     准备），则判定为强制放置。

namespace Catalyst {

void NetherPortalCreateBeforeEvent::serialize(CompoundTag& nbt) const {
    Cancellable::serialize(nbt);
    nbt["centerPos"]       = ListTag{centerPos().x, centerPos().y, centerPos().z};
    nbt["plannedPos"]      = ListTag{plannedPos().x, plannedPos().y, plannedPos().z};
    nbt["stepX"]           = stepX();
    nbt["stepZ"]           = stepZ();
    nbt["forcedPlacement"] = forcedPlacement();
    nbt["radius"]          = radius();
}

void NetherPortalCreateAfterEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::ActorEvent::serialize(nbt);
    nbt["centerPos"]       = ListTag{centerPos().x, centerPos().y, centerPos().z};
    nbt["portalPos"]       = ListTag{portalPos().x, portalPos().y, portalPos().z};
    nbt["portalRecord"]    = ll::event::serializeRefObj(portalRecord());
    nbt["shapeBottomLeft"] = ListTag{shapeBottomLeft().x, shapeBottomLeft().y, shapeBottomLeft().z};
    nbt["shapeWidth"]      = shapeWidth();
    nbt["shapeHeight"]     = shapeHeight();
    nbt["shapeValid"]      = isShapeValid();
    nbt["radius"]          = radius();
    nbt["span"]            = span();
    nbt["xInc"]            = xInc();
    nbt["zInc"]            = zInc();
}

namespace {

// 原版在目标维度搜索既有传送门的半径（内联代码中的常量，此处仅用于事件上报）。
constexpr int kVanillaPortalSearchRadius = 16;

// ---- 线程局部上下文：正在通过传送门传送的实体 ----

struct PortalCreationContext {
    Actor* actor     = nullptr;
    Vec3   centerPos{};
};

PortalCreationContext& portalCreationContext() {
    static thread_local PortalCreationContext context;
    return context;
}

// RAII：进入传送路径时记录实体，退出（含嵌套）时恢复外层上下文。
class PortalCreationContextScope {
public:
    PortalCreationContextScope(Actor& actor, Vec3 const& centerPos) : mPrevious(portalCreationContext()) {
        auto& context   = portalCreationContext();
        context.actor   = &actor;
        context.centerPos = centerPos;
    }

    ~PortalCreationContextScope() { portalCreationContext() = mPrevious; }

    PortalCreationContextScope(PortalCreationContextScope const&)            = delete;
    PortalCreationContextScope& operator=(PortalCreationContextScope const&) = delete;

private:
    PortalCreationContext mPrevious;
};

// ---- 几何辅助 ----

static BlockPos floorBlockPosFromVec3(::Vec3 const& pos) {
    return BlockPos{
        static_cast<int>(std::floor(pos.x)),
        static_cast<int>(std::floor(pos.y)),
        static_cast<int>(std::floor(pos.z))
    };
}

static void axisSteps(::PortalAxis axis, int& stepX, int& stepZ) {
    switch (axis) {
    case ::PortalAxis::X:
        stepX = 1;
        stepZ = 0;
        break;
    case ::PortalAxis::Z:
        stepX = 0;
        stepZ = 1;
        break;
    default:
        stepX = 0;
        stepZ = 0;
        break;
    }
}

static BlockPos getPortalInnerPosFromRecord(::PortalRecord const& record) {
    auto pos = record.mBaseBlockPos.get();
    pos.x += static_cast<int>(record.mXInc);
    pos.z += static_cast<int>(record.mZInc);
    pos.y += 1;
    return pos;
}

static uint64 getBlockNameHash(::Block const& block) {
    return block.mBlockType->mNameInfo.get().mFullName.get().mStrHash;
}

static bool isPortalBlock(::Block const* block) {
    return block != nullptr && getBlockNameHash(*block) == ::VanillaBlockTypeIds::Portal().mStrHash;
}

static bool transactionHasPreparedChanges(::WorldChangeTransaction const& transaction) {
    auto const* data = transaction.mData.get();
    return data != nullptr && !data->changes.get().empty();
}

static std::vector<BlockPos> collectPortalBlocksFromTransaction(::WorldChangeTransaction const& transaction) {
    std::vector<BlockPos> blocks;
    auto const*           data = transaction.mData.get();
    if (data == nullptr) {
        return blocks;
    }
    for (auto const& [pos, change] : data->changes.get()) {
        if (isPortalBlock(change.mNewBlock)) {
            blocks.emplace_back(pos);
        }
    }
    return blocks;
}

// 与原版 PortalRecord 编码一致：X 轴传送门 span=1/xInc=1，Z 轴 span=2/zInc=1，
// mBaseBlockPos 指向传送门内格左下角外侧的框架角块。
static ::PortalRecord buildRecordFromShape(::PortalShape const& shape) {
    auto axis = static_cast<::PortalAxis>(shape.mAxis);
    if (axis != ::PortalAxis::X && axis != ::PortalAxis::Z) {
        axis = ::PortalAxis::X;
    }
    int span = axis == ::PortalAxis::X ? 1 : 2;
    int xInc = axis == ::PortalAxis::X ? 1 : 0;
    int zInc = axis == ::PortalAxis::Z ? 1 : 0;

    auto innerBottomLeft = shape.mBottomLeft.get();
    auto base            = innerBottomLeft;
    base.x -= xInc;
    base.z -= zInc;
    if (shape.mBottomLeftValid && axis == ::PortalAxis::X) {
        base.x -= (shape.mWidth - 2);
    }
    base.y -= 1;

    ::PortalRecord record{};
    record.mBaseBlockPos = base;
    record.mSpan         = static_cast<schar>(span);
    record.mXInc         = static_cast<schar>(xInc);
    record.mZInc         = static_cast<schar>(zInc);
    return record;
}

static std::vector<BlockPos> buildPortalBlocksFromShapeAndRecord(
    ::PortalShape const&  shape,
    ::PortalRecord const& record
) {
    std::vector<BlockPos> blocks;

    int stepX = static_cast<int>(record.mXInc);
    int stepZ = static_cast<int>(record.mZInc);

    if (shape.mBottomLeftValid && shape.mWidth > 0 && shape.mHeight > 0) {
        auto bottomLeft = shape.mBottomLeft.get();

        if (stepX == 0 && stepZ == 0) {
            // 无法推断方向时仍提供稳定坐标。
            stepX = 1;
        }

        blocks.reserve(static_cast<size_t>(shape.mWidth * shape.mHeight));
        for (int w = 0; w < shape.mWidth; ++w) {
            for (int h = 0; h < shape.mHeight; ++h) {
                BlockPos p = bottomLeft;
                p.x += stepX * w;
                p.y += h;
                p.z += stepZ * w;
                blocks.emplace_back(p);
            }
        }
    }

    if (!blocks.empty()) {
        return blocks;
    }

    // 原版下界传送门内格为 2x3，退化为按记录推导的内格原点。
    BlockPos origin = getPortalInnerPosFromRecord(record);
    if (stepX == 0 && stepZ == 0) {
        stepX = 1;
    }
    blocks.reserve(6);
    for (int w = 0; w < 2; ++w) {
        for (int h = 0; h < 3; ++h) {
            BlockPos p = origin;
            p.x += stepX * w;
            p.y += h;
            p.z += stepZ * w;
            blocks.emplace_back(p);
        }
    }
    return blocks;
}

} // namespace

// ---- 事件核心 hook：传送门方块生成 ----

LL_TYPE_INSTANCE_HOOK(
    NetherPortalCreateEventHook,
    ll::memory::HookPriority::Normal,
    ::PortalShape,
    &::PortalShape::createPortalBlocks,
    void,
    ::WorldChangeTransaction& transaction
) {
    auto& context = portalCreationContext();
    if (context.actor == nullptr) {
        // 非维度传送触发生成（如打火石点燃框架），与本事件无关，直接放行。
        origin(transaction);
        return;
    }
    Actor& actor = *context.actor;

    BlockPos centerPos = floorBlockPosFromVec3(context.centerPos);
    BlockPos plannedPos = this->mBottomLeft.get();
    int      stepX      = 0;
    int      stepZ      = 0;
    axisSteps(static_cast<::PortalAxis>(this->mAxis), stepX, stepZ);
    bool forcedPlacement = transactionHasPreparedChanges(transaction);

    auto& bus = ll::event::EventBus::getInstance();
    NetherPortalCreateBeforeEvent beforeEvent(
        actor,
        centerPos,
        plannedPos,
        stepX,
        stepZ,
        forcedPlacement,
        kVanillaPortalSearchRadius
    );
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        // 取消：不生成传送门方块（传送落点仍由内联的原版逻辑决定）。
        return;
    }

    origin(transaction);

    auto        record    = buildRecordFromShape(*this);
    BlockPos    portalPos = getPortalInnerPosFromRecord(record);
    auto        portalBlocks = collectPortalBlocksFromTransaction(transaction);
    if (portalBlocks.empty()) {
        portalBlocks = buildPortalBlocksFromShapeAndRecord(*this, record);
    }

    NetherPortalCreateAfterEvent afterEvent(
        actor,
        centerPos,
        portalPos,
        record,
        this->mBottomLeft.get(),
        this->mWidth,
        this->mHeight,
        this->mBottomLeftValid,
        std::move(portalBlocks),
        kVanillaPortalSearchRadius
    );
    bus.publish(afterEvent);
}

// ---- 实体传送路径上下文 hook（含内联的 PortalForcer::force 逻辑）----

LL_TYPE_INSTANCE_HOOK(
    NetherPortalActorTransferContextHook,
    ll::memory::HookPriority::Normal,
    ::ActorDimensionTransferer,
    &::ActorDimensionTransferer::$findTargetPositionAndSetPosition,
    ::Vec3,
    ::Actor&                       actor,
    ::DimensionType                toId,
    ::DimensionType                fromId,
    ::IDimension const&            toDimension,
    ::PortalForcer const&          portalForcer,
    std::optional<::Vec3> const&   actorPosition
) {
    ::Vec3 center = actorPosition.value_or(actor.getPosition());
    PortalCreationContextScope scope(actor, center);
    return origin(actor, toId, fromId, toDimension, portalForcer, actorPosition);
}

// ---- 玩家传送路径上下文 hook ----

LL_TYPE_INSTANCE_HOOK(
    NetherPortalPlayerTransferContextHook,
    ll::memory::HookPriority::Normal,
    ::PlayerDimensionTransferer,
    &::PlayerDimensionTransferer::$setTransitionLocation,
    void,
    ::Player&                 player,
    ::ChangeDimensionRequest& changeRequest,
    ::Dimension&              toDimension
) {
    PortalCreationContextScope scope(player, changeRequest.mToLocation.get());
    origin(player, changeRequest, toDimension);
}

LL_TYPE_INSTANCE_HOOK(
    NetherPortalPlayerRequestContextHook,
    ll::memory::HookPriority::Normal,
    ::PlayerDimensionTransferManager,
    &::PlayerDimensionTransferManager::_playerChangeDimension,
    bool,
    ::Player&                 player,
    ::ChangeDimensionRequest& changeRequest
) {
    PortalCreationContextScope scope(player, changeRequest.mToLocation.get());
    return origin(player, changeRequest);
}

CATALYST_HOOKED_EVENT_PAIR(
    NetherPortalCreateBeforeEvent,
    NetherPortalCreateAfterEvent,
    NetherPortalCreateEventHook,
    NetherPortalActorTransferContextHook,
    NetherPortalPlayerTransferContextHook,
    NetherPortalPlayerRequestContextHook
)

} // namespace Catalyst
