#include "ShelfItemChangeEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/block/ShelfBlock.h"
#include "mc/world/level/block/actor/ShelfBlockActor.h"
#include "mc/world/level/block/block_events/BlockPlayerInteractEvent.h"

namespace Catalyst {

void ShelfItemChangeEvent::serialize(CompoundTag& nbt) const {
    ll::event::Event::serialize(nbt);
    nbt["pos"]    = ListTag{mPos.x, mPos.y, mPos.z};
    nbt["action"] = magic_enum::enum_name(mAction);
    nbt["slot"]   = mSlot;
    nbt["before"] = mBefore.getTypeName();
    nbt["after"]  = mAfter.getTypeName();
}

static ::Player* g_currentShelfUser = nullptr;

LL_TYPE_INSTANCE_HOOK(
    ShelfItemChangeUserTrackHook,
    ll::memory::HookPriority::Normal,
    ShelfBlock,
    &ShelfBlock::use,
    void,
    ::BlockEvents::BlockPlayerInteractEvent& eventData
) {
    g_currentShelfUser = &eventData.mPlayer;
    origin(eventData);
    g_currentShelfUser = nullptr;
}

LL_TYPE_INSTANCE_HOOK(
    ShelfItemChangeWriteHook,
    ll::memory::HookPriority::Normal,
    ShelfBlockActor,
    &ShelfBlockActor::_setItemInternal,
    void,
    int slot, ::ItemStack const& item, bool isLoading, bool emitVibrations
) {
    static_cast<void>(emitVibrations);

    if (isLoading || !g_currentShelfUser) {
        origin(slot, item, isLoading, emitVibrations);
        return;
    }

    ::ItemStack beforeItem = this->getItem(slot);

    bool beforeEmpty = beforeItem.isNull();
    bool afterEmpty  = item.isNull();

    ShelfItemChangeEvent::Action action;
    if (!beforeEmpty && afterEmpty) {
        action = ShelfItemChangeEvent::Action::Take;
    } else if (beforeEmpty && !afterEmpty) {
        action = ShelfItemChangeEvent::Action::Put;
    } else if (!beforeEmpty && !afterEmpty) {
        action = ShelfItemChangeEvent::Action::Swap;
    } else {
        origin(slot, item, isLoading, emitVibrations);
        return;
    }

    auto& bus = ll::event::EventBus::getInstance();

    ShelfItemChangeBeforeEvent beforeEvent(g_currentShelfUser, this->mPosition.get(), action, slot, beforeItem, item);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(slot, item, isLoading, emitVibrations);

    ShelfItemChangeAfterEvent afterEvent(g_currentShelfUser, this->mPosition.get(), action, slot, beforeItem, item);
    bus.publish(afterEvent);
}

CATALYST_HOOKED_EVENT_PAIR(
    ShelfItemChangeBeforeEvent,
    ShelfItemChangeAfterEvent,
    ShelfItemChangeUserTrackHook,
    ShelfItemChangeWriteHook
)

} // namespace Catalyst
