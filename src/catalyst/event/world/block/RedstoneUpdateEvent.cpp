#include "RedstoneUpdateEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/EventRefObjSerializer.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/memory/Memory.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/block_events/BlockEventManager.h"
#include "mc/world/level/block/block_events/BlockRedstoneUpdateEvent.h"
#include "mc/world/level/block/block_events/EventType.h"
#include "mc/world/level/block/block_events/IBlockEventExecutor.h"
#include "mc/world/redstone/circuit/ChunkCircuitComponentList.h"
#include "mc/world/redstone/circuit/CircuitSceneGraph.h"
#include "mc/world/redstone/circuit/CircuitSystem.h"
#include "mc/world/redstone/circuit/components/BaseCircuitComponent.h"

namespace Catalyst {

void RedstoneUpdateEvent::serialize(CompoundTag& nbt) const {
    ll::event::world::WorldEvent::serialize(nbt);
    nbt["pos"]       = ListTag{pos().x, pos().y, pos().z};
    nbt["strength"]  = strength();
    nbt["firstTime"] = isFirstTime();
    nbt["component"] = ll::event::serializePtrObj(component());
}

namespace {

// BlockRedstoneUpdateEvent 的构造函数与 vftable 均未导出，按相同布局自行实现
struct BlockRedstoneUpdateEventImpl : BlockEvents::BlockEventBase {
    BlockSource& mRegion;
    short        mSignalLevel;
    short        mPreviousSignalLevel;
    bool         mIsFirstTime;

    BlockRedstoneUpdateEventImpl(
        BlockPos const& pos,
        BlockSource&    region,
        short           signalLevel,
        short           previousSignalLevel,
        bool            isFirstTime
    )
    : mRegion(region),
      mSignalLevel(signalLevel),
      mPreviousSignalLevel(previousSignalLevel),
      mIsFirstTime(isFirstTime) {
        std::construct_at(mPos.operator->(), pos);
    }

    BlockSource const& getBlockSource() const override { return mRegion; }
};
static_assert(sizeof(BlockRedstoneUpdateEventImpl) == sizeof(BlockEvents::BlockRedstoneUpdateEvent));
static_assert(sizeof(BlockEvents::BlockEventBase) == 24); // mRegion 紧随其后，位于偏移 24

// IBlockEventExecutor: [0]=dtor [1]=getEventType [2]=execute(BlockEventBase&)
constexpr ptrdiff_t kExecutorExecuteIndex = 2;

void dispatchRedstoneUpdate(
    BlockSource&          region,
    BlockPos const&       pos,
    BaseCircuitComponent& component,
    short                 newStrength,
    short                 oldStrength
) {
    Block const& block       = region.getBlock(pos);
    bool const   isFirstTime = component.mIsFirstTime;
    if (!isFirstTime || !component.mIgnoreFirstUpdate) {
        auto* executor = block.getBlockType().mEventManager->_tryGetExecutor(BlockEvents::EventType::RedstoneUpdate);
        if (executor) {
            BlockRedstoneUpdateEventImpl event{pos, region, newStrength, oldStrength, isFirstTime};
            ll::memory::virtualCall<void, void*>(executor, kExecutorExecuteIndex, &event);
        }
    }
    component.mIsFirstTime = false;
}

void processComponent(BlockSource& region, ChunkCircuitComponentList::Item& listItem, ll::event::EventBus& bus) {
    BaseCircuitComponent& component   = *listItem.mComponent;
    int const             newStrength = component.getStrength();
    short const           oldStrength = component.mOldStrength;

    component.setOldStrength((short)newStrength);
    if ((short)newStrength == -1) return;

    RedstoneUpdateBeforeEvent beforeEvent(region, listItem.mPos, newStrength, component.mIsFirstTime, &component);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) return;

    dispatchRedstoneUpdate(region, listItem.mPos, component, (short)newStrength, oldStrength);

    RedstoneUpdateAfterEvent afterEvent(region, listItem.mPos, newStrength, component.mIsFirstTime, &component);
    bus.publish(afterEvent);
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    RedstoneUpdateEventHook,
    ll::memory::HookPriority::Normal,
    CircuitSystem,
    &CircuitSystem::updateBlocks,
    void,
    ::BlockSource&    region,
    ::BlockPos const& chunkPos
) {
    if (!this->mHasBeenEvaluated) return;

    auto& componentsByChunk = this->mSceneGraph->mActiveComponentsPerChunk;
    if (componentsByChunk.empty()) return;

    auto chunkEntryIterator = componentsByChunk.find(chunkPos);
    if (chunkEntryIterator == componentsByChunk.end()) return;

    std::vector<ChunkCircuitComponentList::Item> secondaryUpdateQueue;
    auto&                                        bus = ll::event::EventBus::getInstance();

    for (auto& listItem : *chunkEntryIterator->second.mComponents) {
        BaseCircuitComponent* component = listItem.mComponent;
        if (!component) continue;
        if (!component->needsUpdate() || component->mRemoved) continue;

        component->mNeedsUpdate = false;
        if (component->isSecondaryPowered()) {
            secondaryUpdateQueue.push_back(listItem);
        } else {
            processComponent(region, listItem, bus);
        }
    }

    for (auto& listItem : secondaryUpdateQueue) {
        if (!listItem.mComponent) continue;
        processComponent(region, listItem, bus);
    }
}

CATALYST_HOOKED_EVENT_PAIR(RedstoneUpdateBeforeEvent, RedstoneUpdateAfterEvent, RedstoneUpdateEventHook)

} // namespace Catalyst
