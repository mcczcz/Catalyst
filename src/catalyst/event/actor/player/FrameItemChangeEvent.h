#pragma once

#include "ll/api/event/Cancellable.h"
#include "ll/api/event/Event.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"

#include "catalyst/Macros.h"

class Player;

namespace Catalyst {

class CATALYST_API FrameItemChangeEvent : public ll::event::Event {
public:
    enum class Action : int { Place = 0, Take = 1 };

private:
    Player*  mPlayer;
    BlockPos mPos;
    Action   mAction;
    ItemStack mItem;
    bool     mDrop;

public:
    FrameItemChangeEvent(Player* player, BlockPos pos, Action action, ItemStack const& item, bool drop)
    : mPlayer(player),
      mPos(pos),
      mAction(action),
      mItem(item),
      mDrop(drop) {}

    void serialize(CompoundTag&) const override;

    Player*         player() const { return mPlayer; }
    BlockPos const& pos() const { return mPos; }
    Action          action() const { return mAction; }
    ItemStack const& item() const { return mItem; }
    bool            drop() const { return mDrop; }
};

class CATALYST_API FrameItemChangeBeforeEvent final : public ll::event::Cancellable<FrameItemChangeEvent> {
public:
    using Cancellable::Cancellable;
};

class CATALYST_API FrameItemChangeAfterEvent final : public FrameItemChangeEvent {
public:
    using FrameItemChangeEvent::FrameItemChangeEvent;
};

} // namespace Catalyst
