#include "MobTotemResurrectEvent.h"

#include <algorithm>
#include <optional>

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/deps/shared_types/legacy/actor/ActorDamageCause.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorEvent.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/attribute/AttributeBuffType.h"
#include "mc/world/attribute/AttributeInstanceForwarder.h"
#include "mc/world/attribute/InstantaneousAttributeBuff.h"
#include "mc/world/attribute/SharedAttributes.h"
#include "mc/world/attribute/ValidMutableAttributeWithContext.h"
#include "mc/world/effect/EffectDuration.h"
#include "mc/world/effect/MobEffect.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/Level.h"


#include "ll/api/event/EventRefObjSerializer.h"
#include "mc/deps/nbt/CompoundTag.h"

/*
namespace Catalyst {

void MobTotemResurrectEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::MobEvent::serialize(nbt);
    nbt["killingDamage"] = ll::event::serializeRefObj(killingDamage());
    nbt["totem"]         = ll::event::serializeRefObj(totem());
    nbt["hasTotem"]      = hasTotem();
    ListTag effectsList;
    for (auto const& eff : effects()) {
        CompoundTag effTag;
        effTag["effect"]        = ll::event::serializePtrObj(eff.effect);
        effTag["durationTicks"] = eff.durationTicks;
        effTag["amplifier"]     = eff.amplifier;
        effTag["visible"]       = eff.visible;
        effectsList.emplace_back(std::move(effTag));
    }
    nbt["effects"] = std::move(effectsList);
}

void MobTotemResurrectAfterEvent::serialize(CompoundTag& nbt) const {
    MobTotemResurrectEvent::serialize(nbt);
    nbt["result"] = result();
}


LL_TYPE_INSTANCE_HOOK(
    MobTotemResurrectHook,
    HookPriority::Normal,
    Mob,
    &Mob::checkTotemDeathProtection,
    std::optional<float>,
    ::ActorDamageSource const& killingDamage
) {
    auto const damageCause = killingDamage.getCause();
    if (damageCause == SharedTypes::Legacy::ActorDamageCause::Void
        || damageCause == SharedTypes::Legacy::ActorDamageCause::SelfDestruct) {
        return std::nullopt;
    }

    auto const& beforeTotem = this->getEquippedTotem();
    bool        hadTotem    = this->hasTotemEquipped();

    auto& bus = ll::event::EventBus::getInstance();

    std::vector<MobTotemResurrectEffect> totemEffects;
    if (hadTotem) {
        if (auto* regen = MobEffect::REGENERATION()) {
            totemEffects.push_back({regen, 900, 1, regen->mEffectVisible});
        }
        if (auto* absorption = MobEffect::ABSORPTION()) {
            totemEffects.push_back({absorption, 100, 1, absorption->mEffectVisible});
        }
        if (auto* fireResistance = MobEffect::FIRE_RESISTANCE()) {
            totemEffects.push_back({fireResistance, 800, 0, fireResistance->mEffectVisible});
        }
    }

    MobTotemResurrectBeforeEvent beforeEvent(*this, killingDamage, beforeTotem, hadTotem, std::move(totemEffects));
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return std::nullopt;
    }

    auto const  effectsToApply = beforeEvent.effects();
    auto const& totem          = this->getEquippedTotem();
    bool        hasTotem       = this->hasTotemEquipped();

    std::optional<float> result;
    if (hasTotem) {
        // Restore health to exactly 1.0f, same behavior as vanilla totem protection path.
        auto                       health        = this->getValidMutableAttribute(SharedAttributes::HEALTH());
        float const                currentHealth = health->getCurrentValue();
        InstantaneousAttributeBuff healthBuff(1.0f - currentHealth, AttributeBuffType::TotemOfUndying);
        result = health->addBuff(healthBuff);

        if (result) {
            this->removeAllEffects();

            auto applyTotemEffect = [this](MobTotemResurrectEffect const& config) {
                MobEffect* effect = config.effect;
                if (!effect) {
                    return;
                }
                EffectDuration duration{};
                duration.mValue = std::max(0, config.durationTicks);
                MobEffectInstance effectInstance(effect->mId, duration, std::max(0, config.amplifier));
                effectInstance.mEffectVisible = config.visible;
                this->addEffect(effectInstance);
            };

            for (auto const& effect : effectsToApply) {
                applyTotemEffect(effect);
            }

            this->setFallDistance(0.0f);

            auto& level = this->getLevel();
            level.broadcastActorEvent(*this, ActorEvent::TalismanActivate, 0, std::nullopt);
            level.broadcastLocalEvent(
                this->getDimensionBlockSource(),
                SharedTypes::Legacy::LevelEvent::SoundTotemUsed,
                this->getPosition(),
                0
            );

            this->consumeTotem();
        }
    }

    MobTotemResurrectAfterEvent afterEvent(*this, killingDamage, totem, hasTotem, result.has_value(), effectsToApply);
    bus.publish(afterEvent);

    return result;
}

CATALYST_HOOKED_EVENT_PAIR(MobTotemResurrectBeforeEvent, MobTotemResurrectAfterEvent, MobTotemResurrectHook)

} // namespace Catalyst
*/