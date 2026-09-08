#include "ActorEffectUpdateEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/effect/MobEffectInstance.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void ActorEffectAddEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::ActorEvent::serialize(nbt);
    nbt["effect"] = ll::event::serializeRefObj(effect());
}

void ActorEffectUpdateEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::ActorEvent::serialize(nbt);
    nbt["effect"] = ll::event::serializeRefObj(effect());
}

void ActorEffectRemoveEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::ActorEvent::serialize(nbt);
    nbt["effect"] = ll::event::serializeRefObj(effect());
}


// 26.32 适配：Actor::onEffectUpdated 已从原版移除——重力刷新现有效果时
// 不再有独立回调，更新路径被内联进 Actor::addEffect。改为在 addEffect 钩子内
// 判定“该效果已存在”来派生 Update 事件（保持 26.20 的事件嵌套顺序：
// AddBefore → UpdateBefore → 更新 → UpdateAfter → AddAfter）。
LL_TYPE_INSTANCE_HOOK(
    ActorEffectAddEventHook,
    ll::memory::HookPriority::Normal,
    Actor,
    &Actor::addEffect,
    void,
    ::MobEffectInstance const& effect
) {
    auto& bus = ll::event::EventBus::getInstance();

    bool const isUpdate = this->getEffect(effect.mId) != nullptr;

    ActorEffectAddBeforeEvent beforeEvent(*this, effect);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    if (isUpdate) {
        MobEffectInstance& effectRef = const_cast<MobEffectInstance&>(effect);

        ActorEffectUpdateBeforeEvent updateBeforeEvent(*this, effectRef);
        bus.publish(updateBeforeEvent);
        if (updateBeforeEvent.isCancelled()) {
            return;
        }

        origin(effect);

        ActorEffectUpdateAfterEvent updateAfterEvent(*this, effectRef);
        bus.publish(updateAfterEvent);
    } else {
        origin(effect);
    }

    ActorEffectAddAfterEvent afterEvent(*this, effect);
    bus.publish(afterEvent);
}

LL_TYPE_INSTANCE_HOOK(
    ActorEffectRemoveEventHook,
    ll::memory::HookPriority::Normal,
    Actor,
    &Actor::$onEffectRemoved,
    void,
    ::MobEffectInstance& effect
) {
    auto& bus = ll::event::EventBus::getInstance();

    ActorEffectRemoveBeforeEvent beforeEvent(*this, effect);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(effect);

    ActorEffectRemoveAfterEvent afterEvent(*this, effect);
    bus.publish(afterEvent);
}

CATALYST_HOOKED_EVENT_PAIR(ActorEffectAddBeforeEvent, ActorEffectAddAfterEvent, ActorEffectAddEventHook)
CATALYST_HOOKED_EVENT_PAIR(ActorEffectUpdateBeforeEvent, ActorEffectUpdateAfterEvent, ActorEffectAddEventHook)
CATALYST_HOOKED_EVENT_PAIR(ActorEffectRemoveBeforeEvent, ActorEffectRemoveAfterEvent, ActorEffectRemoveEventHook)

} // namespace Catalyst
