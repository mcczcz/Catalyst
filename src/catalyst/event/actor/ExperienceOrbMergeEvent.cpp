#include "ExperienceOrbMergeEvent.h"

#include <algorithm>
#include <cmath>

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorType.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/phys/AABB.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"
#include "mc/world/actor/DataItem.h"
namespace Catalyst {

void ExperienceOrbMergeEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::ActorEvent::serialize(nbt);
    nbt["sourceOrb"] = ll::event::serializeRefObj(sourceOrb());
    nbt["value"]     = value();
}

void ExperienceOrbMergeBeforeEvent::serialize(CompoundTag& nbt) const {
    Cancellable::serialize(nbt);
    nbt["mergedPickupCount"] = mergedPickupCount();
}

void ExperienceOrbMergeAfterEvent::serialize(CompoundTag& nbt) const {
    ExperienceOrbMergeEvent::serialize(nbt);
    nbt["oldPickupCount"] = oldPickupCount();
    nbt["newPickupCount"] = newPickupCount();
}
int getOrbValue(ExperienceOrb& orb) {
    return orb.mEntityData->getInt(static_cast<ushort>(ActorDataIDs::Value));
}

// 26.32 适配：ExperienceOrb::_tryMergeExistingOrbs（逐 tick 合并）已从原版移除
// （运行时仅保留出生时的静态 _tryMergeIntoExistingOrbs）。改为拦截 postNormalTick，
// 在其内追加原合并逻辑（带事件）；若原版内部仍内联合并，本逻辑幂等、不会重复合并。
LL_TYPE_INSTANCE_HOOK(
    ExperienceOrbMergeEventHook,
    ll::memory::HookPriority::Normal,
    ExperienceOrb,
    &ExperienceOrb::postNormalTick,
    void
) {
    origin();

    auto& bus    = ll::event::EventBus::getInstance();
    auto& region = this->getDimensionBlockSource();
    auto  range  = this->getAABB().cloneAndGrow(Vec3{0.5f, 0.5f, 0.5f});
    int   selfValue = getOrbValue(*this);
    auto  selfId    = this->getOrCreateUniqueID();

    // 26.32 适配：带类型的 fetchEntities 重载已移除，改用 fetchEntities2 并自行排除自身
    auto const& xpOrbs = region.fetchEntities2(ActorType::Experience, range, false);

    for (Actor* actor : xpOrbs) {
        if (!actor || actor == static_cast<Actor*>(this) || actor->mRemoved) {
            continue;
        }
        if (this->mRemoved) {
            return;
        }

        auto* otherOrb = static_cast<ExperienceOrb*>(actor);

        // Must have same value.
        if (getOrbValue(*otherOrb) != selfValue) {
            continue;
        }

        // Either: entity ID difference is a multiple of 40,
        // or: self was spawned this tick (mAge == 0) with 2.5% probability.
        auto  otherId      = otherOrb->getOrCreateUniqueID();
        int64 diff         = std::abs(selfId.rawID - otherId.rawID);
        bool  idCondition  = diff != 0 && diff % 40 == 0;
        bool  ageCondition = this->mAge == 0 && (std::rand() % 40 == 0);
        if (!idCondition && !ageCondition) {
            continue;
        }

        // Only the lower-ID orb processes the merge to avoid duplicates.
        if (selfId.rawID > otherId.rawID) {
            continue;
        }

        // this = target (lower ID, survives), otherOrb = source (higher ID, removed).
        int oldPickupCount    = this->mRandomPickupValue;
        int mergedPickupCount = oldPickupCount + otherOrb->mRandomPickupValue;

        ExperienceOrbMergeBeforeEvent beforeEvent(*this, *otherOrb, selfValue, mergedPickupCount);
        bus.publish(beforeEvent);
        if (beforeEvent.isCancelled() || otherOrb->mRemoved || this->mRemoved) {
            continue;
        }

        this->mRandomPickupValue = beforeEvent.mergedPickupCount();
        this->mAge               = std::min(this->mAge, otherOrb->mAge);
        otherOrb->remove();

        ExperienceOrbMergeAfterEvent afterEvent(
            *this,
            *otherOrb,
            selfValue,
            oldPickupCount,
            this->mRandomPickupValue
        );
        bus.publish(afterEvent);
    }
}

CATALYST_HOOKED_EVENT_PAIR(
    ExperienceOrbMergeBeforeEvent,
    ExperienceOrbMergeAfterEvent,
    ExperienceOrbMergeEventHook
)

} // namespace Catalyst
