#pragma once

#include "ll/api/event/Cancellable.h"
#include "ll/api/event/player/PlayerEvent.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"

#include "catalyst/Macros.h"

namespace Catalyst {

class CATALYST_API ChiseledBookshelfItemChangeEvent : public ll::event::PlayerEvent {
public:
    enum class Action : int { Put = 0, Take = 1 };

private:
    BlockPos  mPos;
    Action    mAction;
    int       mSlot;
    ItemStack mItem;

public:
    ChiseledBookshelfItemChangeEvent(Player& player, BlockPos pos, Action action, int slot, ItemStack const& item)
    : PlayerEvent(player),
      mPos(pos),
      mAction(action),
      mSlot(slot),
      mItem(item) {}

    void serialize(CompoundTag&) const override;

    BlockPos const&  pos() const { return mPos; }
    Action           action() const { return mAction; }
    int              slot() const { return mSlot; }
    ItemStack const& item() const { return mItem; }
};

class CATALYST_API ChiseledBookshelfItemChangeBeforeEvent final
: public ll::event::Cancellable<ChiseledBookshelfItemChangeEvent> {
public:
    using Cancellable::Cancellable;
};

class CATALYST_API ChiseledBookshelfItemChangeAfterEvent final : public ChiseledBookshelfItemChangeEvent {
public:
    using ChiseledBookshelfItemChangeEvent::ChiseledBookshelfItemChangeEvent;
};

} // namespace Catalyst
