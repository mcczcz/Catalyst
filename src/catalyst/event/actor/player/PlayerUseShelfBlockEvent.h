#pragma once

#include "ll/api/event/Cancellable.h"
#include "ll/api/event/player/PlayerEvent.h"
#include "mc/world/level/BlockPos.h"

#include "catalyst/Macros.h"

namespace Catalyst {

class CATALYST_API PlayerUseShelfBlockEvent : public ll::event::PlayerEvent {
    BlockPos mPos;

public:
    constexpr PlayerUseShelfBlockEvent(Player& player, BlockPos pos) : PlayerEvent(player), mPos(pos) {}

    void serialize(CompoundTag&) const override;

    BlockPos const& pos() const { return mPos; }
};

class CATALYST_API PlayerUseShelfBlockBeforeEvent final : public ll::event::Cancellable<PlayerUseShelfBlockEvent> {
public:
    using Cancellable::Cancellable;
};

class CATALYST_API PlayerUseShelfBlockAfterEvent final : public PlayerUseShelfBlockEvent {
public:
    using PlayerUseShelfBlockEvent::PlayerUseShelfBlockEvent;
};

} // namespace Catalyst
