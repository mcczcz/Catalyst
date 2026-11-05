#pragma once

#include <utility>
#include <vector>

#include "ll/api/event/Cancellable.h"
#include "ll/api/event/entity/MobEvent.h"

#include "catalyst/Macros.h"

class ActorDamageSource;
class ItemStack;
class MobEffect;
/*
namespace Catalyst {

struct MobTotemResurrectEffect {
    MobEffect* effect;
    int        durationTicks;
    int        amplifier;
    bool       visible;
};

class CATALYST_API MobTotemResurrectEvent : public ll::event::entity::MobEvent {
    ActorDamageSource const&             mKillingDamage;
    ItemStack const&                     mTotem;
    bool                                 mHasTotem;
    std::vector<MobTotemResurrectEffect> mEffects;

public:
    MobTotemResurrectEvent(
        Mob&                                 mob,
        ActorDamageSource const&             killingDamage,
        ItemStack const&                     totem,
        bool                                 hasTotem,
        std::vector<MobTotemResurrectEffect> effects
    )
    : MobEvent(mob),
      mKillingDamage(killingDamage),
      mTotem(totem),
      mHasTotem(hasTotem),
      mEffects(std::move(effects)) {}

    void serialize(CompoundTag&) const override;

    ActorDamageSource const&                    killingDamage() const { return mKillingDamage; }
    ItemStack const&                            totem() const { return mTotem; }
    bool                                        hasTotem() const { return mHasTotem; }
    std::vector<MobTotemResurrectEffect>&       effects() { return mEffects; }
    std::vector<MobTotemResurrectEffect> const& effects() const { return mEffects; }
    void setEffects(std::vector<MobTotemResurrectEffect> effects) { mEffects = std::move(effects); }
};

class CATALYST_API MobTotemResurrectBeforeEvent final : public ll::event::Cancellable<MobTotemResurrectEvent> {
public:
    using Cancellable::Cancellable;
};

class CATALYST_API MobTotemResurrectAfterEvent final : public MobTotemResurrectEvent {
    bool mResult;

public:
    MobTotemResurrectAfterEvent(
        Mob&                                 mob,
        ActorDamageSource const&             killingDamage,
        ItemStack const&                     totem,
        bool                                 hasTotem,
        bool                                 result,
        std::vector<MobTotemResurrectEffect> effects
    )
    : MobTotemResurrectEvent(mob, killingDamage, totem, hasTotem, std::move(effects)),
      mResult(result) {}

    void serialize(CompoundTag&) const override;

    bool result() const { return mResult; }
};

} // namespace Catalyst
*/