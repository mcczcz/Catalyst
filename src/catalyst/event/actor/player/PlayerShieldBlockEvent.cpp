#include "PlayerShieldBlockEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/EventRefObjSerializer.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/actor/HurtParameters.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

namespace Catalyst {

void PlayerShieldBlockEvent::serialize(CompoundTag& nbt) const {
    ll::event::PlayerEvent::serialize(nbt);
    nbt["source"]  = ll::event::serializeRefObj(source());
    nbt["damage"]  = damage();
    nbt["damager"] = ll::event::serializePtrObj(damager());
}

void PlayerShieldBlockAfterEvent::serialize(CompoundTag& nbt) const {
    PlayerShieldBlockEvent::serialize(nbt);
    nbt["result"] = result();
}


namespace {

struct ShieldBlockAttempt {
    enum class Phase { AwaitingCheck, PublishingBefore, Blocking, Complete };

    inline static thread_local ShieldBlockAttempt* current = nullptr;

    ShieldBlockAttempt*      parent = current;
    Player&                  player;
    ActorDamageSource const& source;
    float                    damage;
    Actor*                   damager = nullptr;
    Phase                    phase   = Phase::AwaitingCheck;

    ShieldBlockAttempt(Player& player, ActorDamageSource const& source, float damage)
    : player(player),
      source(source),
      damage(damage) {
        current = this;
    }

    ~ShieldBlockAttempt() { current = parent; }

    ShieldBlockAttempt(ShieldBlockAttempt const&)            = delete;
    ShieldBlockAttempt& operator=(ShieldBlockAttempt const&) = delete;

    void finish(bool blocked) {
        if (phase != Phase::Blocking) {
            return;
        }
        // Mark complete before publishing: listeners may query blocking/sleeping or cause another hit.
        phase = Phase::Complete;
        PlayerShieldBlockAfterEvent event(player, source, damage, damager, blocked);
        ll::event::EventBus::getInstance().publish(event);
    }
};

} // namespace

LL_TYPE_INSTANCE_HOOK(
    PlayerShieldBlockHurtHook,
    HookPriority::Normal,
    Player,
    &Player::$_hurt,
    ActorHurtResult,
    ActorDamageSource const& source,
    float                    damage,
    HurtParameters const&    hurtParameters
) {
    ShieldBlockAttempt attempt(*this, source, damage);
    auto               result = origin(source, damage, hurtParameters);
    // The block succeeded only if _hurt returned without entering its normal damage path.
    // ActorHurtResult itself also reports false for immunity, zero damage, etc.
    attempt.finish(true);
    return result;
}

LL_TYPE_INSTANCE_HOOK(
    PlayerShieldBlockCheckHook,
    HookPriority::Normal,
    Player,
    &Player::$isDamageBlocked,
    bool,
    ActorDamageSource const& source
) {
    auto* attempt = ShieldBlockAttempt::current;
    if (!attempt || &attempt->player != this || &attempt->source != &source
        || attempt->phase != ShieldBlockAttempt::Phase::AwaitingCheck) {
        return origin(source);
    }

    // _blockUsingShield is inlined into _hurt, but its initial virtual check still exists.
    // Keep failed attempts observable and let Before listeners change the blocking state.
    attempt->phase   = ShieldBlockAttempt::Phase::PublishingBefore;
    attempt->damager = getLevel().fetchEntity(source.getDamagingEntityUniqueID(), false);

    PlayerShieldBlockBeforeEvent beforeEvent(*this, source, attempt->damage, attempt->damager);
    ll::event::EventBus::getInstance().publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        attempt->phase = ShieldBlockAttempt::Phase::Complete;
        // Skip shield effects while allowing _hurt to continue applying the incoming damage.
        return false;
    }

    bool result    = origin(source);
    attempt->phase = ShieldBlockAttempt::Phase::Blocking;
    if (!result) {
        attempt->finish(false);
    }
    return result;
}

LL_TYPE_INSTANCE_HOOK(PlayerShieldBlockFallbackHook, HookPriority::Normal, Player, &Player::$isSleeping, bool) {
    // IDA: _hurt's first call after the inlined block is isSleeping, even when not asleep.
    // Piercing projectiles can reach it AFTER shield sound/durability effects; report false
    // here, before normal damage handling, rather than treating those effects as success.
    if (auto* attempt = ShieldBlockAttempt::current; attempt && &attempt->player == this) {
        attempt->finish(false);
    }
    return origin();
}

CATALYST_HOOKED_EVENT_PAIR(
    PlayerShieldBlockBeforeEvent,
    PlayerShieldBlockAfterEvent,
    PlayerShieldBlockHurtHook,
    PlayerShieldBlockCheckHook,
    PlayerShieldBlockFallbackHook
)

} // namespace Catalyst
