#pragma once

#include "ll/api/event/Cancellable.h"
#include "ll/api/event/entity/ActorEvent.h"
#include "mc/world/actor/item/ExperienceOrb.h"

#include "catalyst/Macros.h"
/*
namespace Catalyst {

class CATALYST_API ExperienceOrbMergeEvent : public ll::event::entity::ActorEvent {
    ExperienceOrb& mSourceOrb;
    int            mValue;

public:
    constexpr ExperienceOrbMergeEvent(ExperienceOrb& targetOrb, ExperienceOrb& sourceOrb, int value)
    : ActorEvent(targetOrb),
      mSourceOrb(sourceOrb),
      mValue(value) {}

    void serialize(CompoundTag&) const override;

    ExperienceOrb& targetOrb() const { return static_cast<ExperienceOrb&>(self()); }
    ExperienceOrb& sourceOrb() const { return mSourceOrb; }
    int            value() const { return mValue; }
};

class CATALYST_API ExperienceOrbMergeBeforeEvent final : public ll::event::Cancellable<ExperienceOrbMergeEvent> {
    int mMergedPickupCount;

public:
    constexpr ExperienceOrbMergeBeforeEvent(
        ExperienceOrb& targetOrb,
        ExperienceOrb& sourceOrb,
        int            value,
        int            mergedPickupCount
    )
    : Cancellable(targetOrb, sourceOrb, value),
      mMergedPickupCount(mergedPickupCount) {}

    void serialize(CompoundTag&) const override;

    int  mergedPickupCount() const { return mMergedPickupCount; }
    void setMergedPickupCount(int count) { mMergedPickupCount = count; }
};

class CATALYST_API ExperienceOrbMergeAfterEvent final : public ExperienceOrbMergeEvent {
    int mOldPickupCount;
    int mNewPickupCount;

public:
    constexpr ExperienceOrbMergeAfterEvent(
        ExperienceOrb& targetOrb,
        ExperienceOrb& sourceOrb,
        int            value,
        int            oldPickupCount,
        int            newPickupCount
    )
    : ExperienceOrbMergeEvent(targetOrb, sourceOrb, value),
      mOldPickupCount(oldPickupCount),
      mNewPickupCount(newPickupCount) {}

    void serialize(CompoundTag&) const override;

    int oldPickupCount() const { return mOldPickupCount; }
    int newPickupCount() const { return mNewPickupCount; }
};

} // namespace Catalyst
*/