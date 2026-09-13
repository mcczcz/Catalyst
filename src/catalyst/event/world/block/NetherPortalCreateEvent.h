#pragma once

#include <vector>

#include "ll/api/event/Cancellable.h"
#include "ll/api/event/entity/ActorEvent.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/PortalRecord.h"

#include "catalyst/Macros.h"
/*
namespace Catalyst {

class CATALYST_API NetherPortalCreateBeforeEvent final
: public ll::event::Cancellable<ll::event::entity::ActorEvent> {
    BlockPos mCenterPos;
    BlockPos mPlannedPos;
    int      mStepX;
    int      mStepZ;
    bool     mForcedPlacement;
    int      mRadius;

public:
    NetherPortalCreateBeforeEvent(
        Actor&    actor,
        BlockPos  centerPos,
        BlockPos  plannedPos,
        int       stepX,
        int       stepZ,
        bool      forcedPlacement,
        int       radius
    )
    : Cancellable(actor),
      mCenterPos(centerPos),
      mPlannedPos(plannedPos),
      mStepX(stepX),
      mStepZ(stepZ),
      mForcedPlacement(forcedPlacement),
      mRadius(radius) {}

    void serialize(CompoundTag&) const override;

    BlockPos const& centerPos() const { return mCenterPos; }
    BlockPos const& plannedPos() const { return mPlannedPos; }
    int             stepX() const { return mStepX; }
    int             stepZ() const { return mStepZ; }
    bool            forcedPlacement() const { return mForcedPlacement; }
    int             radius() const { return mRadius; }
};

class CATALYST_API NetherPortalCreateAfterEvent final : public ll::event::entity::ActorEvent {
    BlockPos     mCenterPos;
    BlockPos     mPortalPos;
    PortalRecord mPortalRecord;
    BlockPos     mShapeBottomLeft;
    int          mShapeWidth;
    int          mShapeHeight;
    bool         mShapeValid;
    std::vector<BlockPos> mPortalBlocks;
    int          mRadius;

public:
    NetherPortalCreateAfterEvent(
        Actor&             actor,
        BlockPos           centerPos,
        BlockPos           portalPos,
        PortalRecord const& portalRecord,
        BlockPos           shapeBottomLeft,
        int                shapeWidth,
        int                shapeHeight,
        bool               shapeValid,
        std::vector<BlockPos> portalBlocks,
        int                radius
    )
    : ActorEvent(actor),
      mCenterPos(centerPos),
      mPortalPos(portalPos),
      mPortalRecord(portalRecord),
      mShapeBottomLeft(shapeBottomLeft),
      mShapeWidth(shapeWidth),
      mShapeHeight(shapeHeight),
      mShapeValid(shapeValid),
      mPortalBlocks(std::move(portalBlocks)),
      mRadius(radius) {}

    void serialize(CompoundTag&) const override;

    BlockPos const& centerPos() const { return mCenterPos; }
    BlockPos const& portalPos() const { return mPortalPos; }
    BlockPos const& portalBasePos() const { return mPortalRecord.mBaseBlockPos.get(); }
    BlockPos const& shapeBottomLeft() const { return mShapeBottomLeft; }
    PortalRecord const& portalRecord() const { return mPortalRecord; }
    std::vector<BlockPos> const& portalBlocks() const { return mPortalBlocks; }

    int radius() const { return mRadius; }
    int shapeWidth() const { return mShapeWidth; }
    int shapeHeight() const { return mShapeHeight; }
    bool isShapeValid() const { return mShapeValid; }
    int span() const { return static_cast<int>(mPortalRecord.mSpan); }
    int xInc() const { return static_cast<int>(mPortalRecord.mXInc); }
    int zInc() const { return static_cast<int>(mPortalRecord.mZInc); }
};

using NetherPortalCreateEvent = NetherPortalCreateAfterEvent;

} // namespace Catalyst
*/