#include "ChiseledBookshelfItemChangeEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/level/block/ChiseledBookshelfBlock.h"
#include "mc/world/level/block/actor/ChiseledBookshelfBlockActor.h"

namespace Catalyst {

void ChiseledBookshelfItemChangeEvent::serialize(CompoundTag& nbt) const {
    ll::event::PlayerEvent::serialize(nbt);
    nbt["pos"]    = ListTag{mPos.x, mPos.y, mPos.z};
    nbt["action"] = magic_enum::enum_name(mAction);
    nbt["slot"]   = mSlot;
    nbt["item"]   = mItem.getTypeName();
}

LL_TYPE_INSTANCE_HOOK(
    ChiseledBookshelfItemChangePutHook,
    ll::memory::HookPriority::Normal,
    ChiseledBookshelfBlock,
    &ChiseledBookshelfBlock::_setBook,
    void,
    ::Player&                      player,
    ::ItemStack                    heldItem,
    ::ChiseledBookshelfBlockActor& bookshelfActor,
    int                            hitSlot
) {
    auto& bus = ll::event::EventBus::getInstance();

    ChiseledBookshelfItemChangeBeforeEvent beforeEvent(
        player,
        bookshelfActor.mPosition.get(),
        ChiseledBookshelfItemChangeEvent::Action::Put,
        hitSlot,
        heldItem
    );
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(player, heldItem, bookshelfActor, hitSlot);

    ChiseledBookshelfItemChangeAfterEvent afterEvent(
        player,
        bookshelfActor.mPosition.get(),
        ChiseledBookshelfItemChangeEvent::Action::Put,
        hitSlot,
        heldItem
    );
    bus.publish(afterEvent);
}

LL_TYPE_STATIC_HOOK(
    ChiseledBookshelfItemChangeTakeHook,
    ll::memory::HookPriority::Normal,
    ChiseledBookshelfBlock,
    &ChiseledBookshelfBlock::_retrieveBook,
    bool,
    ::Player&                      player,
    ::ChiseledBookshelfBlockActor& bookshelfActor,
    int                            hitSlot
) {
    auto& bus = ll::event::EventBus::getInstance();

    ::ItemStack takenItem = bookshelfActor.getItem(hitSlot);

    ChiseledBookshelfItemChangeBeforeEvent beforeEvent(
        player,
        bookshelfActor.mPosition.get(),
        ChiseledBookshelfItemChangeEvent::Action::Take,
        hitSlot,
        takenItem
    );
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return false;
    }

    bool result = origin(player, bookshelfActor, hitSlot);

    ChiseledBookshelfItemChangeAfterEvent afterEvent(
        player,
        bookshelfActor.mPosition.get(),
        ChiseledBookshelfItemChangeEvent::Action::Take,
        hitSlot,
        takenItem
    );
    bus.publish(afterEvent);

    return result;
}

CATALYST_HOOKED_EVENT_PAIR(
    ChiseledBookshelfItemChangeBeforeEvent,
    ChiseledBookshelfItemChangeAfterEvent,
    ChiseledBookshelfItemChangePutHook,
    ChiseledBookshelfItemChangeTakeHook
)

} // namespace Catalyst
