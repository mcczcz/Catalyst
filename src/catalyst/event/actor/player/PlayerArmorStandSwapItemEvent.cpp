#include "PlayerArmorStandSwapItemEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/actor/ArmorStand.h"
#include "mc/world/actor/player/Player.h"

#ifdef LL_PLAT_S
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include "mc/entity/components_json_legacy/Interaction.h"
#endif

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void PlayerArmorStandSwapItemEvent::serialize(CompoundTag& nbt) const {
    ll::event::PlayerEvent::serialize(nbt);
    nbt["armorStand"] = ll::event::serializeRefObj(armorStand());
    nbt["slot"]       = magic_enum::enum_name(slot());
}


#ifdef LL_PLAT_S
LL_TYPE_INSTANCE_HOOK(
    ArmorStandSwapItemHook,
    HookPriority::Normal,
    ArmorStand,
    &ArmorStand::$getInteraction,
    Interaction,
    Player&     player,
    Vec3 const& location,
    HandSlot    handSlot
) {
    auto  interaction = origin(player, location, handSlot);
    auto& callback    = interaction.mCallback.get();
    if (!callback) {
        return interaction;
    }

    // 26.60: the equip callback captures player, armor stand, slot and hand.
    // Confirmed at 0x141BD0060 / 0x141D48F10 in the 26.60.6 server.
    std::uint64_t textData{};
    std::memcpy(&textData, &interaction.mInteractText, sizeof(textData));
    auto const* text = reinterpret_cast<char const*>(textData & 0x0000FFFFFFFFFFFFULL);
    if (!text || std::string_view(text, (textData >> 48) & 0x7F) != "action.interact.armorstand.equip") {
        return interaction;
    }

    struct EquipCapture {
        Player*                           player;
        ArmorStand*                       armorStand;
        SharedTypes::Legacy::EquipmentSlot slot;
        HandSlot                          handSlot;
    } capture{};
    static_assert(offsetof(EquipCapture, slot) == 16);
    static_assert(sizeof(EquipCapture) + sizeof(void*) <= sizeof(callback));
    std::memcpy(&capture, reinterpret_cast<std::byte const*>(&callback) + sizeof(void*), sizeof(capture));
    if (capture.player != &player || capture.armorStand != this || capture.handSlot != handSlot) {
        return interaction;
    }

    callback = [nativeCallback = std::move(callback), &player, armorStand = this, slot = capture.slot]() mutable {
        auto& bus = ll::event::EventBus::getInstance();
        PlayerArmorStandSwapItemBeforeEvent beforeEvent(player, *armorStand, slot);
        bus.publish(beforeEvent);
        if (beforeEvent.isCancelled()) {
            return;
        }

        std::move(nativeCallback)();

        PlayerArmorStandSwapItemAfterEvent afterEvent(player, *armorStand, slot);
        bus.publish(afterEvent);
    };
    return interaction;
}
#else
LL_TYPE_INSTANCE_HOOK(
    ArmorStandSwapItemHook,
    HookPriority::Normal,
    ArmorStand,
    &ArmorStand::_trySwapItem,
    bool,
    Player&                              player,
    ::SharedTypes::Legacy::EquipmentSlot slot
) {
    auto& bus = ll::event::EventBus::getInstance();

    PlayerArmorStandSwapItemBeforeEvent beforeEvent(player, *this, slot);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return false;
    }

    bool result = origin(player, slot);

    if (result) {
        PlayerArmorStandSwapItemAfterEvent afterEvent(player, *this, slot);
        bus.publish(afterEvent);
    }
    return result;
}
#endif

CATALYST_HOOKED_EVENT_PAIR(
    PlayerArmorStandSwapItemBeforeEvent,
    PlayerArmorStandSwapItemAfterEvent,
    ArmorStandSwapItemHook
)

} // namespace Catalyst
