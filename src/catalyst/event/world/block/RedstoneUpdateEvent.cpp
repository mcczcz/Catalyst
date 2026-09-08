#include "RedstoneUpdateEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/redstone/circuit/ChunkCircuitComponentList.h"
#include "mc/world/redstone/circuit/CircuitSceneGraph.h"
#include "mc/world/redstone/circuit/CircuitSystem.h"
#include "mc/world/redstone/circuit/components/BaseCircuitComponent.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void RedstoneUpdateEvent::serialize(CompoundTag& nbt) const {
    ll::event::world::WorldEvent::serialize(nbt);
    nbt["pos"]       = ListTag{pos().x, pos().y, pos().z};
    nbt["strength"]  = strength();
    nbt["firstTime"] = isFirstTime();
    nbt["component"] = ll::event::serializePtrObj(component());
}


LL_TYPE_INSTANCE_HOOK(
    RedstoneUpdateEventHook,
    ll::memory::HookPriority::Normal,
    CircuitSystem,
    &CircuitSystem::updateBlocks,
    void,
    ::BlockSource&    region,
    ::BlockPos const& chunkPos
) {
    if (this->mHasBeenEvaluated) {
        auto& componentsByChunk  = this->mSceneGraph->mActiveComponentsPerChunk;
        auto  chunkEntryIterator = componentsByChunk.find(chunkPos);

        if (chunkEntryIterator != componentsByChunk.end()) {
            auto&                                      bus                = ll::event::EventBus::getInstance();
            ChunkCircuitComponentList&                 chunkComponentList = chunkEntryIterator->second;
            std::vector<std::pair<BlockPos, BaseCircuitComponent*>> updatedComponents;

            // 26.32 起 CircuitSystem::updateIndividualBlock 已被内联进 updateBlocks，无法逐个调用。
            // 改为：Before 阶段先遍历发布事件，被取消的元件直接清掉 mNeedsUpdate 标记，
            // 让 origin 跳过它；未取消的记录下来，origin 执行完后再发布 After 事件。
            for (auto& listItem : *chunkComponentList.mComponents) {
                BaseCircuitComponent* component = listItem.mComponent;
                if (!component) continue;

                if (component->mNeedsUpdate && !component->mRemoved) {
                    int newStrength = component->getStrength();
                    if (newStrength != -1) {
                        RedstoneUpdateBeforeEvent
                            beforeEvent(region, listItem.mPos, newStrength, component->mIsFirstTime, component);
                        bus.publish(beforeEvent);

                        if (beforeEvent.isCancelled()) {
                            component->mNeedsUpdate = false;
                            component->mIsFirstTime  = false;
                        } else {
                            updatedComponents.emplace_back(listItem.mPos, component);
                        }
                    }
                }
            }

            origin(region, chunkPos);

            for (auto& [pos, component] : updatedComponents) {
                int newStrength = component->getStrength();
                if (newStrength != -1) {
                    RedstoneUpdateAfterEvent
                        afterEvent(region, pos, newStrength, component->mIsFirstTime, component);
                    bus.publish(afterEvent);
                }
            }
            return;
        }
    }

    origin(region, chunkPos);
}

CATALYST_HOOKED_EVENT_PAIR(
    RedstoneUpdateBeforeEvent,
    RedstoneUpdateAfterEvent,
    RedstoneUpdateEventHook
)

} // namespace Catalyst
