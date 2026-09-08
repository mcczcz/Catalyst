#include "FrameItemChangeEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/block/actor/ItemFrameBlockActor.h"

namespace Catalyst {

void FrameItemChangeEvent::serialize(CompoundTag& nbt) const {
    ll::event::Event::serialize(nbt);
    nbt["pos"]    = ListTag{mPos.x, mPos.y, mPos.z};
    nbt["action"] = magic_enum::enum_name(mAction);
    nbt["item"]   = mItem.getTypeName();
    nbt["drop"]   = mDrop;
}

static ::Player* asPlayer(::Actor* actor) {
    if (actor && actor->isPlayer()) {
        return static_cast<::Player*>(actor);
    }
    return nullptr;
}

// 26.32 适配：ItemStack(ItemInstance) 转换构造已无符号。ItemInstance 仅含基类布局，
// 故默认构造后基类切片赋值即可得到等价 ItemStack（事件载荷不需要网络净 ID）
static ::ItemStack toStack(::ItemStackBase const& base) {
    ::ItemStack stack;
    static_cast<::ItemStackBase&>(stack) = base;
    return stack;
}

LL_TYPE_INSTANCE_HOOK(
    FrameItemChangePlaceHook,
    ll::memory::HookPriority::Normal,
    ItemFrameBlockActor,
    &ItemFrameBlockActor::setItem,
    void,
    ::BlockSource& region, ::ItemInstance const& item, ::Actor* entitySource
) {
    if (!entitySource) {
        origin(region, item, entitySource);
        return;
    }
    auto& bus = ll::event::EventBus::getInstance();

    ::ItemStack placedItem = toStack(item);

    FrameItemChangeBeforeEvent beforeEvent(
        asPlayer(entitySource),
        this->mPosition.get(),
        FrameItemChangeEvent::Action::Place,
        placedItem,
        false
    );
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(region, item, entitySource);

    FrameItemChangeAfterEvent afterEvent(
        asPlayer(entitySource),
        this->mPosition.get(),
        FrameItemChangeEvent::Action::Place,
        placedItem,
        false
    );
    bus.publish(afterEvent);
}

LL_TYPE_INSTANCE_HOOK(
    FrameItemChangeTakeHook,
    ll::memory::HookPriority::Normal,
    ItemFrameBlockActor,
    &ItemFrameBlockActor::dropFramedItem,
    void,
    ::BlockSource& region, bool dropItem, ::Actor* entitySource
) {
    if (!entitySource) {
        origin(region, dropItem, entitySource);
        return;
    }
    auto& bus = ll::event::EventBus::getInstance();

    ::ItemStack takenItem = toStack(this->mItem.get());

    FrameItemChangeBeforeEvent beforeEvent(
        asPlayer(entitySource),
        this->mPosition.get(),
        FrameItemChangeEvent::Action::Take,
        takenItem,
        dropItem
    );
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(region, dropItem, entitySource);

    FrameItemChangeAfterEvent afterEvent(
        asPlayer(entitySource),
        this->mPosition.get(),
        FrameItemChangeEvent::Action::Take,
        takenItem,
        dropItem
    );
    bus.publish(afterEvent);
}

CATALYST_HOOKED_EVENT_PAIR(
    FrameItemChangeBeforeEvent,
    FrameItemChangeAfterEvent,
    FrameItemChangePlaceHook,
    FrameItemChangeTakeHook
)

} // namespace Catalyst
