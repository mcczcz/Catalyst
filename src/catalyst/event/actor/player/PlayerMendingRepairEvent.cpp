#include "PlayerMendingRepairEvent.h"

#include <algorithm>
#include <memory>
#include <optional>

#include "catalyst/mod/Gloabl.h"
#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/entity/components/SynchedActorDataComponent.h"
#include "mc/world/actor/ActorDataIDs.h"
#include "mc/world/actor/DataItem.h"
#include "mc/world/actor/item/ExperienceOrb.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/item/enchanting/EnchantUtils.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void PlayerMendingRepairEvent::serialize(CompoundTag& nbt) const {
    ll::event::PlayerEvent::serialize(nbt);
    nbt["orb"]          = ll::event::serializeRefObj(orb());
    nbt["containerId"]  = (int)containerId();
    nbt["slot"]         = slot();
    if (armorSlot().has_value()) {
        nbt["armorSlot"] = magic_enum::enum_name(armorSlot().value());
    }
    nbt["originalItem"] = ll::event::serializeRefObj(originalItem());
    nbt["repairedItem"] = ll::event::serializeRefObj(repairedItem());
    nbt["repairAmount"] = repairAmount();
    nbt["oldOrbValue"]  = oldOrbValue();
    nbt["newOrbValue"]  = newOrbValue();
}


namespace {

struct MendingTarget {
    ContainerID                                   containerId;
    int                                           slot;
    std::optional<SharedTypes::Legacy::ArmorSlot> armorSlot;
};

bool isSameStack(ItemStack const& lhs, ItemStack const& rhs) {
    return std::addressof(lhs) == std::addressof(rhs) || lhs.matchesItem(rhs);
}

bool isRepairableMendingItem(ItemStack const& item) {
    return item.mValid_DeprecatedSeeComment && !item.isNull()
        && item.mCount > 0 && EnchantUtils::hasEnchant(Enchant::Type::Mending, item)
        && item.isDamageableItem() && item.getDamageValue() > 0;
}

std::optional<MendingTarget> resolveMendingTarget(Player& player, ItemStack const& targetItem) {
    ItemStack const& selectedItem = player.getSelectedItem();
    if (isSameStack(targetItem, selectedItem)) {
        return MendingTarget{ContainerID::Inventory, player.getSelectedItemSlot(), std::nullopt};
    }

    ItemStack const& offhandItem = player.getOffhandSlot();
    if (isSameStack(targetItem, offhandItem)) {
        return MendingTarget{ContainerID::Offhand, 1, std::nullopt};
    }

    for (int slot = 0; slot < static_cast<int>(SharedTypes::Legacy::ArmorSlot::HumanoidCount); ++slot) {
        auto const       armorSlot = static_cast<SharedTypes::Legacy::ArmorSlot>(slot);
        ItemStack const& armorItem = player.getArmor(armorSlot);
        if (isSameStack(targetItem, armorItem)) {
            return MendingTarget{ContainerID::Armor, slot, armorSlot};
        }
    }

    return std::nullopt;
}

ItemStack const* getMendingTargetItem(Player& player, MendingTarget const& target) {
    switch (target.containerId) {
    case ContainerID::Inventory:
        return std::addressof(player.getSelectedItem());
    case ContainerID::Offhand:
        return std::addressof(player.getOffhandSlot());
    case ContainerID::Armor:
        if (!target.armorSlot) {
            return nullptr;
        }
        return std::addressof(player.getArmor(*target.armorSlot));
    default:
        return nullptr;
    }
}

DataItem* getOrbValueDataItem(ExperienceOrb& orb) {
    auto* synchedActorData = orb.mEntityData->mData.get();
    if (!synchedActorData) {
        return nullptr;
    }

    auto& items = synchedActorData->mData->mItemsArray;
    auto  index = static_cast<size_t>(ActorDataIDs::Value);
    if (index >= items->size() || !(*items)[index]) {
        return nullptr;
    }

    DataItem* dataItem = (*items)[index].get();
    if (!dataItem || !dataItem->getData<int>()) {
        return nullptr;
    }

    return dataItem;
}

int getOrbValue(ExperienceOrb& orb) {
    return orb.mEntityData->getInt(static_cast<ushort>(ActorDataIDs::Value));
}

bool setOrbValue(ExperienceOrb& orb, DataItem& dataItem, int value) {
    auto* synchedActorData = orb.mEntityData->mData.get();
    if (!synchedActorData) {
        return false;
    }

    auto dataRef = dataItem.getData<int>();
    if (!dataRef) {
        return false;
    }

    int newValue = std::max(0, value);
    if (*dataRef != newValue) {
        *dataRef = newValue;
        synchedActorData->mData->mDirtyFlags->set(static_cast<size_t>(ActorDataIDs::Value));
    }
    return true;
}

} // namespace

// 26.32 适配：ExperienceOrb::_handleMending 已被内联进虚函数 playerTouch，
// 无法再单独钩取。改为钩住 playerTouch 整体：
// - 无需经验修复时直接放行原版逻辑（纯经验拾取，与 26.20 不触发 _handleMending 等价）；
// - 有可修复装备时按事件流程自行完成修复，再把剩余部分交还原版：
//   装备修满后原版 mending 找不到受损装备 → 剩余经验直接给玩家并消耗经验球；
//   经验球耗尽（新值为 0）时原版 mending 修复量为 0，不会再修复装备。
LL_TYPE_INSTANCE_HOOK(
    PlayerMendingRepairEventHook,
    ll::memory::HookPriority::Normal,
    ExperienceOrb,
    &ExperienceOrb::$playerTouch,
    void,
    Player& player
) {
    if (this->mRemoved) {
        return origin(player);
    }

    ItemStack const& originalTarget = EnchantUtils::getRandomDamagedItemWithMending(player);
    if (!isRepairableMendingItem(originalTarget)) {
        return origin(player);
    }

    auto target = resolveMendingTarget(player, originalTarget);
    if (!target) {
        logger.debug("PlayerMendingRepairEvent fallback to origin: targetMapped=false, orbValueData=unknown");
        return origin(player);
    }

    int oldDamage    = originalTarget.getDamageValue();
    int oldOrbValue  = getOrbValue(*this);
    int repairAmount = std::min(oldDamage, oldOrbValue * 2);
    if (repairAmount <= 0) {
        return origin(player);
    }

    ItemStack originalItem(originalTarget);
    ItemStack repairedItem(originalTarget);
    repairedItem.setDamageValue(static_cast<short>(std::max(0, oldDamage - repairAmount)));

    int   newOrbValue = std::max(0, oldOrbValue - repairAmount / 2);
    auto& bus = ll::event::EventBus::getInstance();

    PlayerMendingRepairBeforeEvent beforeEvent(
        player,
        *this,
        target->containerId,
        target->slot,
        target->armorSlot,
        ItemStack(originalItem),
        ItemStack(repairedItem),
        repairAmount,
        oldOrbValue,
        newOrbValue
    );
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        // 26.20 语义：取消 = 不修复装备，经验球价值仍全额转为玩家经验。
        // 26.32 中修复逻辑内联在 playerTouch 里，无法只跳过修复部分，
        // 故手动完成等价收尾：全额经验 + 移除经验球。
        player.addExperience(oldOrbValue);
        this->remove();
        return;
    }

    auto* orbValueDataItem = getOrbValueDataItem(*this);
    if (!orbValueDataItem) {
        logger.debug("PlayerMendingRepairEvent fallback to origin after BeforeEvent: orbValueData=false");
        origin(player);

        auto* currentItem = getMendingTargetItem(player, *target);
        if (!currentItem) {
            return;
        }

        ItemStack finalItem(*currentItem);
        int finalRepairAmount = std::max(
            0,
            static_cast<int>(originalItem.getDamageValue()) - static_cast<int>(finalItem.getDamageValue())
        );
        if (finalRepairAmount <= 0) {
            return;
        }

        PlayerMendingRepairAfterEvent afterEvent(
            player,
            *this,
            target->containerId,
            target->slot,
            target->armorSlot,
            std::move(originalItem),
            std::move(finalItem),
            finalRepairAmount,
            oldOrbValue,
            getOrbValue(*this)
        );
        bus.publish(afterEvent);
        return;
    }

    if (!setOrbValue(*this, *orbValueDataItem, newOrbValue)) {
        logger.debug("PlayerMendingRepairEvent fallback to origin after BeforeEvent: orbValueWrite=false");
        origin(player);
        return;
    }

    switch (target->containerId) {
    case ContainerID::Inventory:
        player.mTransactionManager->_createServerSideAction(originalTarget, repairedItem);
        player.setSelectedItem(repairedItem);
        break;
    case ContainerID::Offhand:
        player.setOffhandSlot(repairedItem);
        break;
    case ContainerID::Armor:
        if (!target->armorSlot) {
            return;
        }
        player.setDamagedArmor(*target->armorSlot, repairedItem);
        break;
    default:
        return;
    }

    auto* currentItem = getMendingTargetItem(player, *target);
    if (!currentItem) {
        origin(player);
        return;
    }

    ItemStack finalItem(*currentItem);
    int finalRepairAmount =
        std::max(0, static_cast<int>(originalItem.getDamageValue()) - static_cast<int>(finalItem.getDamageValue()));
    if (finalRepairAmount <= 0) {
        // 修复未能实际写回：交还原版（含原版修复逻辑）兜底
        origin(player);
        return;
    }
    int finalOrbValue = getOrbValue(*this);

    PlayerMendingRepairAfterEvent afterEvent(
        player,
        *this,
        target->containerId,
        target->slot,
        target->armorSlot,
        std::move(originalItem),
        std::move(finalItem),
        finalRepairAmount,
        oldOrbValue,
        finalOrbValue
    );
    bus.publish(afterEvent);

    // 剩余价值的处理与经验球的消亡交还原版 playerTouch 收尾：
    // - 装备已修满：原版 mending 找不到受损装备，直接把剩余经验给玩家后移除经验球；
    // - 经验球耗尽：原版 mending 计算出的修复量为 0，不会再修复装备。
    if (finalOrbValue > 0) {
        origin(player);
    } else {
        this->remove();
    }
}

CATALYST_HOOKED_EVENT_PAIR(
    PlayerMendingRepairBeforeEvent,
    PlayerMendingRepairAfterEvent,
    PlayerMendingRepairEventHook
)

} // namespace Catalyst

