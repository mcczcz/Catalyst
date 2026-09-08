#include "ProjectileHitEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/EventRefObjSerializer.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/entity/components_json_legacy/ProjectileComponent.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/phys/HitResultType.h"

namespace Catalyst {

namespace {

Actor* resolveHitActor(HitResult const& hitResult) {
    return hitResult.getEntity();
}

bool shouldIgnoreHitEntity(Actor* hitActor) {
    return hitActor != nullptr && !hitActor->canInteractWithOtherEntitiesInGame();
}

void serializeHitEventCommon(CompoundTag& nbt, HitResult const& hitResult) {
    nbt["type"]        = magic_enum::enum_name(hitResult.mType);
    nbt["face"]        = hitResult.mFacing;
    nbt["blockPos"]    = ListTag{hitResult.mBlock.x, hitResult.mBlock.y, hitResult.mBlock.z};
    nbt["pos"]         = ListTag{hitResult.mPos.x, hitResult.mPos.y, hitResult.mPos.z};
    nbt["isHitLiquid"] = hitResult.mIsHitLiquid;
    if (auto* actor = hitResult.getEntity(); actor != nullptr) {
        nbt["hitActor"] = ll::event::serializePtrObj(actor);
    }
}

} // namespace

void ProjectileHitEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::ActorEvent::serialize(nbt);
    serializeHitEventCommon(nbt, hitResult());
}

// 26.32 适配：原版 ProjectileComponent::onHit 内部逻辑（原版事件派发、TNT 点燃、
// _tryReflect / _handleLightningOnHit 等私有函数）已在 26.32 中被内联或移除，
// 其中 ActorEventCoordinator::sendEvent 的 CoordinatorResult 重载已从运行时移除，
// 无法再用公共头文件复刻完整流程。因此回到 26.10 时期的包裹式 hook：
// 插件的 Before 事件可整体否决本次命中（跳过原版 onHit），否则原样调用原版实现，
// 由原版逻辑自行完成击中效果（声音、点燃、反弹、引雷等）。
LL_TYPE_INSTANCE_HOOK(
    ProjectileHitEventHook,
    ll::memory::HookPriority::Normal,
    ProjectileComponent,
    &ProjectileComponent::onHit,
    void,
    Actor&           owner,
    HitResult const& hitResult
) {
    auto* hitActor = resolveHitActor(hitResult);

    if (shouldIgnoreHitEntity(hitActor)) {
        return;
    }

    ProjectileHitBeforeEvent beforeEvent(owner, hitResult);
    ll::event::EventBus::getInstance().publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(owner, hitResult);

    ProjectileHitAfterEvent afterEvent(owner, hitResult);
    ll::event::EventBus::getInstance().publish(afterEvent);
}

CATALYST_HOOKED_EVENT_PAIR(
    ProjectileHitBeforeEvent,
    ProjectileHitAfterEvent,
    ProjectileHitEventHook
)

} // namespace Catalyst
