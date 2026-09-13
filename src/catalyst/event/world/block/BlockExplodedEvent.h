#pragma once

#include "ll/api/event/Cancellable.h"
#include "ll/api/event/world/WorldEvent.h"
#include "mc/world/level/BlockPos.h"

#include "catalyst/Macros.h"
/*
namespace Catalyst {

class CATALYST_API BlockExplodedEvent : public ll::event::world::WorldEvent {
    BlockPos mPos;

public:
    constexpr BlockExplodedEvent(BlockSource& blockSource, BlockPos const& pos)
    : WorldEvent(blockSource),
      mPos(pos) {}

    void serialize(CompoundTag&) const override;

    BlockPos const& pos() const { return mPos; }
};

class CATALYST_API BlockExplodedBeforeEvent final : public ll::event::Cancellable<BlockExplodedEvent> {
public:
    using Cancellable::Cancellable;
};

class CATALYST_API BlockExplodedAfterEvent final : public BlockExplodedEvent {
public:
    using BlockExplodedEvent::BlockExplodedEvent;
};

} // namespace Catalyst
*/