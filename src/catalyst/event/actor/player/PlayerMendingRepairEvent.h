#pragma once

#include <optional>
#include <utility>

#include "ll/api/event/Cancellable.h"
#include "ll/api/event/player/PlayerEvent.h"
#include "mc/deps/shared_types/legacy/actor/ArmorSlot.h"
#include "mc/world/ContainerID.h"
#include "mc/world/item/ItemStack.h"

#include "catalyst/Macros.h"
/*
class ExperienceOrb;

namespace Catalyst {

class CATALYST_API PlayerMendingRepairEvent : public ll::event::PlayerEvent {
    ExperienceOrb& mOrb;
    ContainerID    mContainerId;
    int            mSlot;
    std::optional<SharedTypes::Legacy::ArmorSlot> mArmorSlot;
    ItemStack mOriginalItem;
    ItemStack mRepairedItem;
    int       mRepairAmount;
    int       mOldOrbValue;
    int       mNewOrbValue;

public:
    PlayerMendingRepairEvent(
        Player&                                     player,
        ExperienceOrb&                              orb,
        ContainerID                                 containerId,
        int                                         slot,
        std::optional<SharedTypes::Legacy::ArmorSlot> armorSlot,
        ItemStack                                  originalItem,
        ItemStack                                  repairedItem,
        int                                        repairAmount,
        int                                        oldOrbValue,
        int                                        newOrbValue
    )
    : PlayerEvent(player),
      mOrb(orb),
      mContainerId(containerId),
      mSlot(slot),
      mArmorSlot(armorSlot),
      mOriginalItem(std::move(originalItem)),
      mRepairedItem(std::move(repairedItem)),
      mRepairAmount(repairAmount),
      mOldOrbValue(oldOrbValue),
      mNewOrbValue(newOrbValue) {}

    void serialize(CompoundTag&) const override;

    ExperienceOrb& orb() const { return mOrb; }
    ContainerID    containerId() const { return mContainerId; }
    int            slot() const { return mSlot; }
    std::optional<SharedTypes::Legacy::ArmorSlot> armorSlot() const { return mArmorSlot; }
    ItemStack const& originalItem() const { return mOriginalItem; }
    ItemStack const& repairedItem() const { return mRepairedItem; }
    int              repairAmount() const { return mRepairAmount; }
    int              oldOrbValue() const { return mOldOrbValue; }
    int              newOrbValue() const { return mNewOrbValue; }
    int              consumedOrbValue() const { return mOldOrbValue - mNewOrbValue; }
};

class CATALYST_API PlayerMendingRepairBeforeEvent final : public ll::event::Cancellable<PlayerMendingRepairEvent> {
public:
    using Cancellable::Cancellable;
};

class CATALYST_API PlayerMendingRepairAfterEvent final : public PlayerMendingRepairEvent {
public:
    using PlayerMendingRepairEvent::PlayerMendingRepairEvent;
};

} // namespace Catalyst
*/