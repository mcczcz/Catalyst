#include "catalyst/mod/Gloabl.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/block/actor/BlockActor.h"
#include "mc/world/level/block/actor/HopperBlockActor.h"

#include "catalyst/event/EmitterRegistration.h"
#include "catalyst/event/actor/ActorOpenContainerEvent.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/EventRefObjSerializer.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/inventory/EnderChestContainer.h"
#include "mc/world/level/block/actor/BarrelBlockActor.h"
#include "mc/world/level/block/actor/BrewingStandBlockActor.h"
#include "mc/world/level/block/actor/FurnaceBlockActor.h"
#include "mc/world/level/block/actor/ShulkerBoxBlockActor.h"
#include "mc/world/level/storage/loot/LootResolver.h"
#include <cstdint>
#include <windows.h>
// 检查内存是否可读
static bool IsReadableMemory(const void* ptr) noexcept {
    MEMORY_BASIC_INFORMATION mbi{};
    if (::VirtualQuery(ptr, &mbi, sizeof(mbi)) == 0) {
        return false;
    }
    if (mbi.State != MEM_COMMIT) {
        return false;
    }
    const auto protect = mbi.Protect;
    if ((protect & PAGE_GUARD) != 0 || (protect & PAGE_NOACCESS) != 0) {
        return false;
    }
    const auto protectBase = protect & 0xff;
    return protectBase == PAGE_READONLY || protectBase == PAGE_READWRITE || protectBase == PAGE_WRITECOPY
        || protectBase == PAGE_EXECUTE_READ || protectBase == PAGE_EXECUTE_READWRITE
        || protectBase == PAGE_EXECUTE_WRITECOPY;
}

// 带页缓存的内存可读性检查（直接映射，O(1) 查找）
static bool IsReadableMemoryCached(const void* ptr) noexcept {
    // 64 槽直接映射缓存
    struct PageCache {
        uintptr_t page;
        bool      readable;
    };
    static thread_local PageCache cache[64] = {};

    const auto page  = reinterpret_cast<uintptr_t>(ptr) & ~static_cast<uintptr_t>(0xFFF);
    const auto index = (page >> 12) & 63; // 用页号低 6 位作为索引

    // 直接映射查找 - O(1)
    if (cache[index].page == page) {
        return cache[index].readable;
    }

    // 缓存未命中，执行检查
    bool readable = IsReadableMemory(ptr);
    cache[index]  = {page, readable};

    return readable;
}

// 安全检查 vftable
static bool SafeCheckVftable(void* ptr, void** expectedVftable) noexcept {
    auto p = reinterpret_cast<uintptr_t>(ptr);

    // 快速检查：过滤无效低地址和未对齐地址
    if (p < 0x10000 || (p & (alignof(void*) - 1)) != 0) {
        return false;
    }

    // 带缓存的内存检查
    if (!IsReadableMemoryCached(ptr)) {
        return false;
    }

    // 直接比较 vftable（内存已验证可读）
    return *reinterpret_cast<void***>(ptr) == expectedVftable;
}

void Catalyst::ActorOpenContainerEvent::serialize(CompoundTag& nbt) const {
    ll::event::ActorEvent::serialize(nbt);
    nbt["containerType"] = magic_enum::enum_name(containerType());
    nbt["blockActor"]    = ll::event::serializePtrObj(blockActor());
}

LL_TYPE_INSTANCE_HOOK(
    HopperBlockActorStartOpenHook,
    ll::memory::HookPriority::Normal,
    HopperBlockActor,
    &HopperBlockActor::$startOpen,
    void,
    ::Actor& actor
) {
    static void** expectedBlockActorVftable = HopperBlockActor::$vftableForBlockActor();

    if (SafeCheckVftable(this, expectedBlockActorVftable)) {
        auto* blockActor = static_cast<BlockActor*>(this);

        // 发布 Before 事件
        auto&                                   bus = ll::event::EventBus::getInstance();
        Catalyst::ActorOpenContainerBeforeEvent beforeEvent(actor, Catalyst::ContainerType::Hopper, blockActor);
        bus.publish(beforeEvent);

        if (beforeEvent.isCancelled()) {
            return;
        }
    }

    origin(actor);

    if (SafeCheckVftable(this, expectedBlockActorVftable)) {
        auto* blockActor = static_cast<BlockActor*>(this);

        // 发布 After 事件
        auto&                                  bus = ll::event::EventBus::getInstance();
        Catalyst::ActorOpenContainerAfterEvent afterEvent(actor, Catalyst::ContainerType::Hopper, blockActor);
        bus.publish(afterEvent);
    }
}

LL_TYPE_INSTANCE_HOOK(
    BarrelBlockActorStartOpenHook,
    ll::memory::HookPriority::Normal,
    BarrelBlockActor,
    &BarrelBlockActor::$startOpen,
    void,
    ::Actor& actor
) {
    auto* blockActor = static_cast<BlockActor*>(this);

    // 发布 Before 事件
    auto&                                   bus = ll::event::EventBus::getInstance();
    Catalyst::ActorOpenContainerBeforeEvent beforeEvent(actor, Catalyst::ContainerType::Barrel, blockActor);
    bus.publish(beforeEvent);

    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(actor);

    // 发布 After 事件
    Catalyst::ActorOpenContainerAfterEvent afterEvent(actor, Catalyst::ContainerType::Barrel, blockActor);
    bus.publish(afterEvent);
}

LL_TYPE_INSTANCE_HOOK(
    ChestBlockActorStartOpenHook,
    ll::memory::HookPriority::Normal,
    ChestBlockActor,
    &ChestBlockActor::$startOpen,
    void,
    ::Actor& actor
) {
    auto* blockActor = static_cast<BlockActor*>(this);

    // 发布 Before 事件
    auto&                                   bus = ll::event::EventBus::getInstance();
    Catalyst::ActorOpenContainerBeforeEvent beforeEvent(actor, Catalyst::ContainerType::Chest, blockActor);
    bus.publish(beforeEvent);

    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(actor);

    // 发布 After 事件
    Catalyst::ActorOpenContainerAfterEvent afterEvent(actor, Catalyst::ContainerType::Chest, blockActor);
    bus.publish(afterEvent);
}

LL_TYPE_INSTANCE_HOOK(
    EnderChestContainerStartOpenHook,
    ll::memory::HookPriority::Normal,
    EnderChestContainer,
    &EnderChestContainer::$startOpen,
    void,
    ::Actor& actor
) {
    auto* blockActor = this->mActiveChest;

    // 发布 Before 事件
    auto&                                   bus = ll::event::EventBus::getInstance();
    Catalyst::ActorOpenContainerBeforeEvent beforeEvent(actor, Catalyst::ContainerType::EnderChest, blockActor);
    bus.publish(beforeEvent);

    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(actor);

    // 发布 After 事件
    Catalyst::ActorOpenContainerAfterEvent afterEvent(actor, Catalyst::ContainerType::EnderChest, blockActor);
    bus.publish(afterEvent);
}
/*
LL_TYPE_INSTANCE_HOOK(
    FurnaceBlockActorStartOpenHook,
    ll::memory::HookPriority::Normal,
    FurnaceBlockActor,
    &FurnaceBlockActor::$startOpen,
    void,
    ::Actor& actor
) {
    auto* blockActor = static_cast<BlockActor*>(this);

    // 发布 Before 事件
    auto&                                   bus = ll::event::EventBus::getInstance();
    Catalyst::ActorOpenContainerBeforeEvent beforeEvent(actor, Catalyst::ContainerType::Furnace, blockActor);
    bus.publish(beforeEvent);

    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(actor);

    // 发布 After 事件
    Catalyst::ActorOpenContainerAfterEvent afterEvent(actor, Catalyst::ContainerType::Furnace, blockActor);
    bus.publish(afterEvent);
}

LL_TYPE_INSTANCE_HOOK(
    ShulkerBoxBlockActorStartOpenHook,
    ll::memory::HookPriority::Normal,
    ShulkerBoxBlockActor,
    &ShulkerBoxBlockActor::$startOpen,
    void,
    ::Actor& actor
) {
    auto* blockActor = static_cast<BlockActor*>(this);

    // 发布 Before 事件
    auto&                                   bus = ll::event::EventBus::getInstance();
    Catalyst::ActorOpenContainerBeforeEvent beforeEvent(actor, Catalyst::ContainerType::ShulkerBox, blockActor);
    bus.publish(beforeEvent);

    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(actor);

    // 发布 After 事件
    Catalyst::ActorOpenContainerAfterEvent afterEvent(actor, Catalyst::ContainerType::ShulkerBox, blockActor);
    bus.publish(afterEvent);
}

LL_TYPE_INSTANCE_HOOK(
    BrewingStandBlockActorStartOpenHook,
    ll::memory::HookPriority::Normal,
    BrewingStandBlockActor,
    &BrewingStandBlockActor::$startOpen, // rva合并函数
    void,
    ::Actor& actor
) {
    // BrewingStandBlockActor 需要检查 vftable
    static void** expectedBlockActorVftable = BrewingStandBlockActor::$vftableForBlockActor();

    if (SafeCheckVftable(this, expectedBlockActorVftable)) {
        auto* blockActor = static_cast<BlockActor*>(this);

        // 发布 Before 事件
        auto&                                   bus = ll::event::EventBus::getInstance();
        Catalyst::ActorOpenContainerBeforeEvent beforeEvent(actor, Catalyst::ContainerType::BrewingStand, blockActor);
        bus.publish(beforeEvent);

        if (beforeEvent.isCancelled()) {
            return;
        }
    }

    origin(actor);

    if (SafeCheckVftable(this, expectedBlockActorVftable)) {
        auto* blockActor = static_cast<BlockActor*>(this);

        // 发布 After 事件
        auto&                                  bus = ll::event::EventBus::getInstance();
        Catalyst::ActorOpenContainerAfterEvent afterEvent(actor, Catalyst::ContainerType::BrewingStand, blockActor);
        bus.publish(afterEvent);
    }
}
*/
namespace Catalyst {

CATALYST_HOOKED_EVENT_PAIR(
    ActorOpenContainerBeforeEvent,
    ActorOpenContainerAfterEvent,
    HopperBlockActorStartOpenHook,
    BarrelBlockActorStartOpenHook,
    ChestBlockActorStartOpenHook,
    EnderChestContainerStartOpenHook
)

} // namespace Catalyst
