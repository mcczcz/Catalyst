#include "NetherPortalCreateEvent.h"

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/util/BaseGameVersion.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/PortalForcer.h"
#include "mc/world/level/PortalShape.h"
#include "mc/world/level/block/BedrockBlockNames.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/PortalAxis.h"
#include "mc/world/level/block/VanillaBlockTypeIds.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"
#include "mc/world/level/material/Material.h"
#include "mc/world/level/storage/LevelData.h"

/*

namespace std {

template <>
struct hash<::PortalRecord> {
    size_t operator()(::PortalRecord const& record) const noexcept {
        size_t hashValue = 1469598103934665603ull;
        auto   mix       = [&](unsigned int v) {
            hashValue ^= static_cast<size_t>(v);
            hashValue *= 1099511628211ull;
        };

        auto const& base = record.mBaseBlockPos.get();
        mix(static_cast<unsigned int>(base.x));
        mix(static_cast<unsigned int>(base.y));
        mix(static_cast<unsigned int>(base.z));
        mix(static_cast<unsigned char>(record.mSpan));
        mix(static_cast<unsigned char>(record.mXInc));
        mix(static_cast<unsigned char>(record.mZInc));
        return hashValue;
    }
};

template <>
struct equal_to<::PortalRecord> {
    bool operator()(::PortalRecord const& lhs, ::PortalRecord const& rhs) const noexcept {
        auto const& l = lhs.mBaseBlockPos.get();
        auto const& r = rhs.mBaseBlockPos.get();
        return l.x == r.x && l.y == r.y && l.z == r.z && lhs.mSpan == rhs.mSpan && lhs.mXInc == rhs.mXInc
            && lhs.mZInc == rhs.mZInc;
    }
};

} // namespace std

#include "ll/api/event/EventRefObjSerializer.h"
#include "mc/deps/nbt/CompoundTag.h"


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


static BlockPos getPortalInnerPosFromRecord(::PortalRecord const& record) {
    auto pos  = record.mBaseBlockPos.get();
    pos.x    += static_cast<int>(record.mXInc);
    pos.z    += static_cast<int>(record.mZInc);
    pos.y    += 1;
    return pos;
}

static BlockPos floorBlockPosFromVec3(::Vec3 const& pos) {
    return BlockPos{
        static_cast<int>(std::floor(pos.x)),
        static_cast<int>(std::floor(pos.y)),
        static_cast<int>(std::floor(pos.z))
    };
}

struct PortalCandidate {
    BlockPos pos{};
    int      stepX    = 1;
    int      stepZ    = 0;
    float    distSqr  = std::numeric_limits<float>::max();
    bool     selected = false;
};

static float distanceSqrToEntity(::Vec3 const& entityPos, ::BlockPos const& pos) {
    float dx = (static_cast<float>(pos.x) + 0.5f) - entityPos.x;
    float dy = (static_cast<float>(pos.y) + 0.5f) - entityPos.y;
    float dz = (static_cast<float>(pos.z) + 0.5f) - entityPos.z;
    return dx * dx + dy * dy + dz * dz;
}

static int floorMod(int value, int modulus) {
    int r = value % modulus;
    return r < 0 ? r + modulus : r;
}

static bool canPortalReplaceBlock(::BlockSource& source, ::BlockPos const& pos) {
    auto const& minVersion   = ::PortalForcer::MIN_PORTAL_REPLACE_BLOCK_FIX_VERSION();
    auto const& worldVersion = source.getLevel().getLevelData().getBaseGameVersion();
    if (!minVersion.isCompatibleWith(worldVersion)) {
        return source.isEmptyBlock(pos);
    }

    auto const& block = source.getBlock(pos);
    if (block.getMaterial().isSolid()) {
        return false;
    }

    auto const& extraBlock = source.getExtraBlock(pos);
    return !extraBlock.getMaterial().isSolid() && block.canBeBuiltOver(source, pos);
}

static int settleCandidateY(::BlockSource& source, int x, int y, int z, int minY) {
    if (!canPortalReplaceBlock(source, BlockPos{x, y, z})) {
        return std::numeric_limits<int>::min();
    }

    while (y > minY && canPortalReplaceBlock(source, BlockPos{x, y - 1, z})) {
        --y;
    }
    return y;
}

static void getStepFromOrientation4(int orientationId, int& stepX, int& stepZ) {
    int k2 = orientationId % 2;
    int l2 = 1 - k2;
    if (orientationId >= 2) {
        k2 = -k2;
        l2 = -l2;
    }
    stepX = k2;
    stepZ = l2;
}

static void normalizeStepAndOrigin(::BlockPos& innerBottomLeft, int& stepX, int& stepZ) {
    if (stepX < 0) {
        innerBottomLeft.x += stepX;
        stepX              = -stepX;
    }
    if (stepZ < 0) {
        innerBottomLeft.z += stepZ;
        stepZ              = -stepZ;
    }
}

static bool hasSolidMaterial(::BlockSource const& source, int x, int y, int z) {
    return source.getMaterial(x, y, z).mSolid;
}

static bool validatePortalVolumeFirstPass(
    ::BlockSource& source,
    int            candidateX,
    int            candidateY,
    int            candidateZ,
    int            orientationId
) {
    int stepX = 0;
    int stepZ = 0;
    getStepFromOrientation4(orientationId, stepX, stepZ);

    int depthX = stepZ;
    int depthZ = -stepX;

    for (int depth = 0; depth < 3; ++depth) {
        for (int width = 0; width < 4; ++width) {
            int blockX = candidateX + (width - 1) * stepX + depth * depthX;
            int blockZ = candidateZ + (width - 1) * stepZ + depth * depthZ;

            for (int h = -1; h < 4; ++h) {
                int blockY = candidateY + h;
                if (h < 0) {
                    if (!hasSolidMaterial(source, blockX, blockY, blockZ)) {
                        return false;
                    }
                } else if (!canPortalReplaceBlock(source, BlockPos{blockX, blockY, blockZ})) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool validatePortalVolumeSecondPass(
    ::BlockSource& source,
    int            candidateX,
    int            candidateY,
    int            candidateZ,
    int            orientationId
) {
    int stepX = orientationId;
    int stepZ = 1 - orientationId;

    for (int width = 0; width < 4; ++width) {
        int blockX = candidateX + (width - 1) * stepX;
        int blockZ = candidateZ + (width - 1) * stepZ;
        for (int h = -1; h < 4; ++h) {
            int blockY = candidateY + h;
            if (h < 0) {
                if (!hasSolidMaterial(source, blockX, blockY, blockZ)) {
                    return false;
                }
            } else if (!canPortalReplaceBlock(source, BlockPos{blockX, blockY, blockZ})) {
                return false;
            }
        }
    }
    return true;
}

static PortalCandidate
findPortalCandidate(::BlockSource& source, ::BlockPos const& center, ::Vec3 const& entityPos, int seed) {
    PortalCandidate best;
    int             minY = source.getMinHeight();
    int             maxY = source.getMaxHeight() - 1;
    if (minY > maxY) {
        return best;
    }

    constexpr int kSearchRadius = 16;

    // Phase 1: strict scan with all 4 orientation variants.
    for (int x = center.x - kSearchRadius; x <= center.x + kSearchRadius; ++x) {
        for (int z = center.z - kSearchRadius; z <= center.z + kSearchRadius; ++z) {
            for (int y = maxY; y >= minY; --y) {
                int settledY = settleCandidateY(source, x, y, z, minY);
                if (settledY == std::numeric_limits<int>::min()) {
                    continue;
                }

                for (int i = 0; i < 4; ++i) {
                    int orientationId = floorMod(seed + i, 4);
                    if (!validatePortalVolumeFirstPass(source, x, settledY, z, orientationId)) {
                        continue;
                    }

                    int stepX = 0;
                    int stepZ = 0;
                    getStepFromOrientation4(orientationId, stepX, stepZ);
                    BlockPos candidate{x, settledY, z};
                    float    d2 = distanceSqrToEntity(entityPos, candidate);
                    if (d2 < best.distSqr) {
                        best.pos      = candidate;
                        best.stepX    = stepX;
                        best.stepZ    = stepZ;
                        best.distSqr  = d2;
                        best.selected = true;
                    }
                }
            }
        }
    }

    if (best.selected) {
        normalizeStepAndOrigin(best.pos, best.stepX, best.stepZ);
        return best;
    }

    // Phase 2: relaxed scan with 2 orientation variants.
    for (int x = center.x - kSearchRadius; x <= center.x + kSearchRadius; ++x) {
        for (int z = center.z - kSearchRadius; z <= center.z + kSearchRadius; ++z) {
            for (int y = maxY; y >= minY; --y) {
                int settledY = settleCandidateY(source, x, y, z, minY);
                if (settledY == std::numeric_limits<int>::min()) {
                    continue;
                }

                for (int i = 0; i < 2; ++i) {
                    int orientationId = floorMod(seed + i, 2);
                    if (!validatePortalVolumeSecondPass(source, x, settledY, z, orientationId)) {
                        continue;
                    }

                    BlockPos candidate{x, settledY, z};
                    float    d2 = distanceSqrToEntity(entityPos, candidate);
                    if (d2 < best.distSqr) {
                        best.pos      = candidate;
                        best.stepX    = orientationId;
                        best.stepZ    = 1 - orientationId;
                        best.distSqr  = d2;
                        best.selected = true;
                    }
                }
            }
        }
    }

    if (best.selected) {
        normalizeStepAndOrigin(best.pos, best.stepX, best.stepZ);
    }
    return best;
}

static ::PortalAxis axisFromStep(int stepX, int stepZ) {
    if (stepX != 0) {
        return ::PortalAxis::X;
    }
    if (stepZ != 0) {
        return ::PortalAxis::Z;
    }
    return ::PortalAxis::Unknown;
}

static void placeFallbackSupport(::BlockSource& source, ::BlockPos const& innerBottomLeft, int stepX, int stepZ) {
    auto const& netherrack = ::BlockTypeRegistry::mBlockTypeRegistry().mValue.getDefaultBlockState(::VanillaBlockTypeIds::Netherrack(), true);
    int         depthX     = stepZ;
    int         depthZ     = -stepX;

    // Cross-shaped pattern: depth ±2 for inner columns (width 0,1),
    // depth ±1 for frame columns (width -1,2). Matches vanilla exactly.
    for (int depth = -2; depth <= 2; ++depth) {
        for (int width = -1; width <= 2; ++width) {
            if (std::abs(depth) == 2 && (width == -1 || width == 2)) {
                continue;
            }
            BlockPos p  = innerBottomLeft;
            p.x        += stepX * width + depthX * depth;
            p.z        += stepZ * width + depthZ * depth;
            p.y        -= 1;
            if (source.getBlock(p).isAir()) {
                source.setBlock(p, netherrack, 3, nullptr, ::BlockChangeContext{});
            }
        }
    }
}

static void
prepareForcedPlacementVolume(::BlockSource& source, ::BlockPos const& innerBottomLeft, int stepX, int stepZ) {
    auto const& obsidian = ::BlockTypeRegistry::mBlockTypeRegistry().mValue.getDefaultBlockState(::VanillaBlockTypeIds::Obsidian(), true);
    auto const& air      = ::BlockTypeRegistry::mBlockTypeRegistry().mValue.getDefaultBlockState(::BedrockBlockNames::Air(), false);
    int         depthX   = stepZ;
    int         depthZ   = -stepX;

    for (int depth = -1; depth <= 1; ++depth) {
        for (int w = 0; w < 2; ++w) {
            BlockPos p  = innerBottomLeft;
            p.x        += stepX * w + depthX * depth;
            p.z        += stepZ * w + depthZ * depth;

            BlockPos floor  = p;
            floor.y        -= 1;
            source.setBlock(floor, obsidian, 3, nullptr, ::BlockChangeContext{});

            for (int h = 0; h < 3; ++h) {
                BlockPos interior  = p;
                interior.y        += h;
                source.setBlock(interior, air, 3, nullptr, ::BlockChangeContext{});
            }
        }
    }
}

static void placePortalBlocks(::BlockSource& source, ::BlockPos const& innerBottomLeft, int stepX, int stepZ) {
    auto const& obsidian = ::BlockTypeRegistry::mBlockTypeRegistry().mValue.getDefaultBlockState(::VanillaBlockTypeIds::Obsidian(), true);
    auto const& portal   = ::BlockTypeRegistry::mBlockTypeRegistry().mValue.getDefaultBlockState(::VanillaBlockTypeIds::Portal(), true);

    auto        axis           = axisFromStep(stepX, stepZ);
    auto        portalWithAxis = portal.setState(::VanillaStates::PortalAxis(), static_cast<int>(axis));
    auto const& portalState    = portalWithAxis ? portalWithAxis.get() : portal;

    for (int w = -1; w <= 2; ++w) {
        for (int h = -1; h <= 3; ++h) {
            BlockPos p  = innerBottomLeft;
            p.x        += stepX * w;
            p.z        += stepZ * w;
            p.y        += h;

            bool frame = (w == -1 || w == 2 || h == -1 || h == 3);
            source.setBlock(p, frame ? obsidian : portalState, 2, nullptr, ::BlockChangeContext{});
        }
    }

    for (int w = -1; w <= 2; ++w) {
        for (int h = -1; h <= 3; ++h) {
            BlockPos p  = innerBottomLeft;
            p.x        += stepX * w;
            p.z        += stepZ * w;
            p.y        += h;
            source.updateNeighborsAt(p);
        }
    }
}

static int chooseFallbackY(::BlockSource const& source, int centerY) {
    int y = source.getMaxHeight() - 10;
    if (centerY <= y) {
        y = centerY > 70 ? centerY : 70;
    }
    return y;
}

static ::PortalRecord buildRecordFromShapeAndPlacement(
    ::PortalShape const& shape,
    ::BlockPos const&    placedInnerBottomLeft,
    int                  stepX,
    int                  stepZ
) {
    ::PortalRecord record{};
    auto           axis = shape.mAxis;
    if (axis != ::PortalAxis::X && axis != ::PortalAxis::Z) {
        axis = axisFromStep(stepX, stepZ);
    }
    int span = axis == ::PortalAxis::X ? 1 : 2;
    int xInc = axis == ::PortalAxis::X ? 1 : 0;
    int zInc = axis == ::PortalAxis::Z ? 1 : 0;

    auto innerBottomLeft  = shape.mBottomLeftValid ? shape.mBottomLeft.get() : placedInnerBottomLeft;
    auto base             = innerBottomLeft;
    base.x               -= xInc;
    base.z               -= zInc;
    if (shape.mBottomLeftValid && axis == ::PortalAxis::X) {
        base.x -= (shape.mWidth - 2);
    }
    base.y -= 1;

    record.mBaseBlockPos = base;
    record.mSpan         = static_cast<schar>(span);
    record.mXInc         = static_cast<schar>(xInc);
    record.mZInc         = static_cast<schar>(zInc);
    return record;
}

static std::vector<BlockPos> buildFallbackPortalBlocks(::BlockPos const& origin, int stepX, int stepZ) {
    std::vector<BlockPos> blocks;
    blocks.reserve(6);
    for (int w = 0; w < 2; ++w) {
        for (int h = 0; h < 3; ++h) {
            BlockPos p  = origin;
            p.x        += stepX * w;
            p.z        += stepZ * w;
            p.y        += h;
            blocks.emplace_back(p);
        }
    }
    return blocks;
}

static std::vector<BlockPos>
buildPortalBlocksFromShapeAndRecord(::PortalShape const& shape, ::PortalRecord const& record) {
    std::vector<BlockPos> blocks;

    int stepX = static_cast<int>(record.mXInc);
    int stepZ = static_cast<int>(record.mZInc);

    if (shape.mBottomLeftValid && shape.mWidth > 0 && shape.mHeight > 0) {
        auto bottomLeft = shape.mBottomLeft.get();

        if (stepX == 0 && stepZ == 0) {
            // Fallback: still provide stable coordinates even if direction cannot be inferred.
            stepX = 1;
        }

        blocks.reserve(static_cast<size_t>(shape.mWidth * shape.mHeight));
        for (int w = 0; w < shape.mWidth; ++w) {
            for (int h = 0; h < shape.mHeight; ++h) {
                BlockPos p  = bottomLeft;
                p.x        += stepX * w;
                p.y        += h;
                p.z        += stepZ * w;
                blocks.emplace_back(p);
            }
        }
    }

    if (!blocks.empty()) {
        return blocks;
    }

    // Vanilla-created nether portals are 2x3 interior; fallback to record-derived inner origin.
    BlockPos origin = getPortalInnerPosFromRecord(record);
    if (stepX == 0 && stepZ == 0) {
        stepX = 1;
    }
    blocks.reserve(6);
    for (int w = 0; w < 2; ++w) {
        for (int h = 0; h < 3; ++h) {
            BlockPos p  = origin;
            p.x        += stepX * w;
            p.y        += h;
            p.z        += stepZ * w;
            blocks.emplace_back(p);
        }
    }
    return blocks;
}

LL_TYPE_INSTANCE_HOOK(
    NetherPortalCreateEventHook,
    ll::memory::HookPriority::Normal,
    ::PortalForcer,
    &::PortalForcer::createPortal,
    ::PortalRecord const&,
    ::Actor const& entity,
    int            radius
) {
    auto&       source    = entity.getDimensionBlockSource();
    auto const& entityPos = entity.getPosition();
    BlockPos    centerPos = floorBlockPosFromVec3(entityPos);

    int seed = this->mRandom->nextInt();

    auto candidate = findPortalCandidate(source, centerPos, entityPos, seed);

    bool forcedPlacement = !candidate.selected;
    if (!candidate.selected) {
        int y              = chooseFallbackY(source, centerPos.y);
        candidate.pos      = BlockPos{centerPos.x, y, centerPos.z};
        candidate.stepX    = 0;
        candidate.stepZ    = 1;
        candidate.selected = true;
    }

    auto&                         bus = ll::event::EventBus::getInstance();
    NetherPortalCreateBeforeEvent beforeEvent(
        const_cast<Actor&>(entity),
        centerPos,
        candidate.pos,
        candidate.stepX,
        candidate.stepZ,
        forcedPlacement,
        radius
    );
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        static const PortalRecord emptyRecord{};
        return emptyRecord;
    }

    if (forcedPlacement) {
        prepareForcedPlacementVolume(source, candidate.pos, candidate.stepX, candidate.stepZ);
        placeFallbackSupport(source, candidate.pos, candidate.stepX, candidate.stepZ);
    }
    placePortalBlocks(source, candidate.pos, candidate.stepX, candidate.stepZ);

    auto        axis = axisFromStep(candidate.stepX, candidate.stepZ);
    PortalShape shape{source, candidate.pos, axis};
    shape.mAxis = axis;
    shape.evaluate(candidate.pos, source);

    auto  dimensionId  = source.getDimensionId();
    auto  builtRecord  = buildRecordFromShapeAndPlacement(shape, candidate.pos, candidate.stepX, candidate.stepZ);
    auto& recordSet    = this->mPortalRecords.get()[dimensionId];
    auto  insertRet    = recordSet.emplace(builtRecord);
    auto  iter         = insertRet.first;
    this->mDirty       = true;
    auto const& record = *iter;

    BlockPos portalPos = getPortalInnerPosFromRecord(record);

    auto portalBlocks = buildPortalBlocksFromShapeAndRecord(shape, record);
    if (portalBlocks.empty()) {
        portalBlocks = buildFallbackPortalBlocks(portalPos, candidate.stepX, candidate.stepZ);
    }
    auto bottomLeft = shape.mBottomLeftValid ? shape.mBottomLeft.get() : record.mBaseBlockPos.get();
    int  width      = shape.mBottomLeftValid ? shape.mWidth : 2;
    int  height     = shape.mBottomLeftValid ? shape.mHeight : 3;

    NetherPortalCreateAfterEvent afterEvent(
        const_cast<Actor&>(entity),
        centerPos,
        portalPos,
        record,
        bottomLeft,
        width,
        height,
        shape.mBottomLeftValid,
        std::move(portalBlocks),
        radius
    );
    bus.publish(afterEvent);

    return record;
}

CATALYST_HOOKED_EVENT_PAIR(NetherPortalCreateBeforeEvent, NetherPortalCreateAfterEvent, NetherPortalCreateEventHook)

} // namespace Catalyst
*/
