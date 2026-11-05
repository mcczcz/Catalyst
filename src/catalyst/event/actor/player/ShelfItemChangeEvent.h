#pragma once

#include "ll/api/event/Cancellable.h"
#include "ll/api/event/Event.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"

#include "catalyst/Macros.h"

class Player;

namespace Catalyst {

class CATALYST_API ShelfItemChangeEvent : public ll::event::Event {
public:
    enum class Action : int { Put = 0, Take = 1, Swap = 2 };

private:
    Player*  mPlayer;
    BlockPos mPos;
    Action   mAction;
    int      mSlot;
    ItemStack mBefore;
    ItemStack mAfter;

public:
    ShelfItemChangeEvent(
        Player*         player,
        BlockPos        pos,
        Action          action,
        int             slot,
        ItemStack const& before,
        ItemStack const& after
    )
    : mPlayer(player),
      mPos(pos),
      mAction(action),
      mSlot(slot),
      mBefore(before),
      mAfter(after) {}

    void serialize(CompoundTag&) const override;

    Player*         player() const { return mPlayer; }
    BlockPos const& pos() const { return mPos; }
    Action          action() const { return mAction; }
    int             slot() const { return mSlot; }
    ItemStack const& beforeItem() const { return mBefore; }
    ItemStack const& afterItem() const { return mAfter; }
};

class CATALYST_API ShelfItemChangeBeforeEvent final : public ll::event::Cancellable<ShelfItemChangeEvent> {
public:
    using Cancellable::Cancellable;
};

class CATALYST_API ShelfItemChangeAfterEvent final : public ShelfItemChangeEvent {
public:
    using ShelfItemChangeEvent::ShelfItemChangeEvent;
};

} // namespace Catalyst
