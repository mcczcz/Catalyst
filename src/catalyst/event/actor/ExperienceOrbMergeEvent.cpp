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

#include "ll/api/event/EventRefObjSerializer.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/DataItem.h"

/*
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
int getOrbValue(ExperienceOrb& orb) { return orb.mEntityData->getInt(static_cast<ushort>(ActorDataIDs::Value)); }

LL_TYPE_INSTANCE_HOOK(
    ExperienceOrbMergeEventHook,
    ll::memory::HookPriority::Normal,
    ExperienceOrb,
    &ExperienceOrb::_tryMergeExistingOrbs,
    void
) {
    auto& bus       = ll::event::EventBus::getInstance();
    auto& region    = this->getDimensionBlockSource();
    auto  range     = this->getAABB().cloneAndGrow(Vec3{0.5f, 0.5f, 0.5f});
    int   selfValue = getOrbValue(*this);
    auto  selfId    = this->getOrCreateUniqueID();

    auto xpOrbs = region.fetchEntities(ActorType::Experience, range, this, [](Actor*) { return true; });

    for (Actor* actor : xpOrbs) {
        if (!actor || actor->mRemoved) {
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

        ExperienceOrbMergeAfterEvent afterEvent(*this, *otherOrb, selfValue, oldPickupCount, this->mRandomPickupValue);
        bus.publish(afterEvent);
    }
}

CATALYST_HOOKED_EVENT_PAIR(ExperienceOrbMergeBeforeEvent, ExperienceOrbMergeAfterEvent, ExperienceOrbMergeEventHook)

} // namespace Catalyst
 */
