#include "PlayerShieldBlockEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDamageSource.h"
#include "mc/world/actor/ActorHurtResult.h"
#include "mc/world/actor/HurtParameters.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/Level.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

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

// 26.32 适配：Player::_blockUsingShield 已被内联进 Player::_hurt（虚函数），
// 格挡判定改经 isDamageBlocked 完成，但该查询不携带伤害数值。
// 故用两个钩子配合：
// - PlayerShieldBlockDamageHook 钩住 _hurt，把本次伤害数值放入线程局部上下文；
// - PlayerShieldBlockEventHook  钩住 isDamageBlocked，在伤害流程内真正发生
//   格挡判定时发布 Before/After 事件（取消 Before = 不格挡，伤害正常生效）。
struct ShieldBlockContext {
    bool  active = false; // 是否处于 _hurt 处理期间
    float damage = 0.0f;  // 本次 _hurt 的伤害数值
};
ShieldBlockContext& shieldBlockContext() {
    static thread_local ShieldBlockContext ctx;
    return ctx;
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    PlayerShieldBlockDamageHook,
    HookPriority::Normal,
    Player,
    &Player::$_hurt,
    ::ActorHurtResult,
    ::ActorDamageSource const& source,
    float                      damage,
    ::HurtParameters const&    hurtParameters
) {
    auto& ctx = shieldBlockContext();
    if (ctx.active) {
        return origin(source, damage, hurtParameters);
    }
    ctx.active = true;
    ctx.damage = damage;
    auto result = origin(source, damage, hurtParameters);
    ctx.active  = false;
    return result;
}

LL_TYPE_INSTANCE_HOOK(
    PlayerShieldBlockEventHook,
    HookPriority::Normal,
    Player,
    &Player::$isDamageBlocked,
    bool,
    ::ActorDamageSource const& source
) {
    auto& ctx = shieldBlockContext();
    if (!ctx.active) {
        // 非伤害流程中的格挡能力查询（如 AI 预判），原样回答，不发布事件
        return origin(source);
    }

    bool wouldBlock = origin(source);
    if (!wouldBlock) {
        return false;
    }

    auto& bus = ll::event::EventBus::getInstance();

    auto   damagerId = source.getDamagingEntityUniqueID();
    Actor* damager   = this->getLevel().fetchEntity(damagerId, false);

    PlayerShieldBlockBeforeEvent beforeEvent(*this, source, ctx.damage, damager);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        // 取消格挡：回答“未被格挡”，_hurt 的内联逻辑会让本次伤害正常生效
        return false;
    }

    PlayerShieldBlockAfterEvent afterEvent(*this, source, ctx.damage, damager, true);
    bus.publish(afterEvent);

    return true;
}

CATALYST_HOOKED_EVENT_PAIR(
    PlayerShieldBlockBeforeEvent,
    PlayerShieldBlockAfterEvent,
    PlayerShieldBlockDamageHook,
    PlayerShieldBlockEventHook
)

} // namespace Catalyst
