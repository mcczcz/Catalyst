#include "PlayerDropItemEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/inventory/transaction/ComplexInventoryTransaction.h"
#include "mc/world/inventory/transaction/InventoryAction.h"
#include "mc/world/inventory/transaction/InventorySourceType.h"
#include "mc/world/inventory/transaction/InventoryTransaction.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void PlayerDropItemEvent::serialize(CompoundTag& nbt) const {
    PlayerEvent::serialize(nbt);
    nbt["item"] = ll::event::serializeRefObj(item());
    nbt["slot"] = slot();
}


/*
LL_AUTO_TYPE_INSTANCE_HOOK(
    PlayerDropItemEventHook1,
    HookPriority::Normal,
    Player,
    &Player::$drop,
    bool,
    ItemStack const& item,
    bool             randomly
) {
    auto& bus  = ll::event::EventBus::getInstance();
    int   slot = getSelectedItemSlot();

    PlayerDropItemBeforeEvent beforeEvent(*this, item, slot);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return false;
    }

    bool result = origin(item, randomly);

    if (result) {
        PlayerDropItemAfterEvent afterEvent(*this, item, slot);
        bus.publish(afterEvent);
    }
    return result;
}
*/
// Hook for inventory drag drop
LL_TYPE_INSTANCE_HOOK(
    PlayerDropItemEventHook2,
    HookPriority::Normal,
    ComplexInventoryTransaction,
    &ComplexInventoryTransaction::$handle,
    InventoryTransactionError,
    Player& player,
    bool    isSenderAuthority
) {
    if (mType == ComplexInventoryTransaction::Type::NormalTransaction) {
        InventorySource source{
            InventorySourceType::ContainerInventory,
            ContainerID::Inventory,
            InventorySource::InventorySourceFlags::NoFlag
        };
        // 26.32 适配：InventoryTransaction::getActions 已移除，直接查询公开的 mActions 映射
        auto const& actionsMap = mTransaction->mActions.get();
        auto const  actionsIt  = actionsMap.find(source);
        if (actionsIt != actionsMap.end() && actionsIt->second.size() == 1) {
            auto const&     actions = actionsIt->second;
            int              slot = actions[0].mSlot;
            ItemStack const& item = player.mInventory->mInventory->getItem(slot);

            auto& bus = ll::event::EventBus::getInstance();

            PlayerDropItemBeforeEvent beforeEvent(player, item, slot);
            bus.publish(beforeEvent);
            if (beforeEvent.isCancelled()) {
                return InventoryTransactionError::AuthorityMismatch;
            }

            InventoryTransactionError result = origin(player, isSenderAuthority);

            if (result == InventoryTransactionError::NoError) {
                PlayerDropItemAfterEvent afterEvent(player, item, slot);
                bus.publish(afterEvent);
            }
            return result;
        }
    }
    return origin(player, isSenderAuthority);
}

CATALYST_HOOKED_EVENT_PAIR(
    PlayerDropItemBeforeEvent,
    PlayerDropItemAfterEvent,
    PlayerDropItemEventHook2
)

} // namespace Catalyst
