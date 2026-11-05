#pragma once

#include "ll/api/event/Cancellable.h"
#include "ll/api/event/player/PlayerEvent.h"
#include "mc/world/level/BlockPos.h"

#include "catalyst/Macros.h"

namespace Catalyst {

class CATALYST_API PlayerUseChiseledBookshelfBlockEvent : public ll::event::PlayerEvent {
    BlockPos mPos;

public:
    constexpr PlayerUseChiseledBookshelfBlockEvent(Player& player, BlockPos pos) : PlayerEvent(player), mPos(pos) {}

    void serialize(CompoundTag&) const override;

    BlockPos const& pos() const { return mPos; }
};

class CATALYST_API PlayerUseChiseledBookshelfBlockBeforeEvent final
: public ll::event::Cancellable<PlayerUseChiseledBookshelfBlockEvent> {
public:
    using Cancellable::Cancellable;
};

class CATALYST_API PlayerUseChiseledBookshelfBlockAfterEvent final : public PlayerUseChiseledBookshelfBlockEvent {
public:
    using PlayerUseChiseledBookshelfBlockEvent::PlayerUseChiseledBookshelfBlockEvent;
};

} // namespace Catalyst
