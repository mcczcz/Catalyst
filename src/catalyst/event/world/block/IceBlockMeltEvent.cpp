#include "IceBlockMeltEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/level/block/BedrockBlockNames.h"
#include "mc/world/level/block/BrightnessPair.h"
#include "mc/world/level/block/VanillaBlockTypeIds.h"
#include "mc/world/level/block/IceBlock.h"
#include "mc/world/level/block/block_events/BlockRandomTickEvent.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/dimension/VanillaDimensions.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void IceBlockMeltEvent::serialize(CompoundTag& nbt) const {
    ll::event::world::WorldEvent::serialize(nbt);
    nbt["pos"]         = ListTag{pos().x, pos().y, pos().z};
    nbt["sourceBlock"] = ll::event::serializeRefObj(sourceBlock());
    nbt["meltedBlock"] = ll::event::serializeRefObj(meltedBlock());
    nbt["inNether"]    = isInNether();
}


// 26.32 起 vanilla 将融化逻辑（光照判定、Fizz 音效、蒸发粒子）内联进
// IceBlock::randomTick，原静态函数 _getMeltedBlockAndSendEvents 已无任何调用方，
// 旧 hook 挂在死代码上不会再触发。因此改为包裹 randomTick：
//
// 1. 复现 vanilla 的融化条件：方块光照 > 8（>= 9）。天空光与昼夜不参与判定
//    （与 Java 版"含天空光 > 11"不同，系 Bedrock 平台差异；条件经 26.32 实测
//    标定：方块光 9~14 融化、8 及以下不融化，正午 sky=12 + block<=8 亦不融化）。
// 2. 满足条件时先发布 Before 事件；被取消则直接跳过原调用（冰保持原状，
//    无需像旧实现那样手动 _blockChanged 同步——服务端从未改动方块）。
// 3. 未取消则交还 vanilla 执行融化（主世界→水，下界→空气 + Fizz + 蒸发粒子
//    均由 vanilla 自身代码完成，不再需要手工构造 ActorSoundIdentifier），
//    随后发布 After 事件。
LL_TYPE_INSTANCE_HOOK(
    IceBlockMeltHook,
    ll::memory::HookPriority::Normal,
    IceBlock,
    &IceBlock::randomTick,
    void,
    ::BlockEvents::BlockRandomTickEvent& eventData
) {
    // 浮冰不融化（26.32 实测浮冰不会进入随机刻，此处保险起见直接放行）
    // 光照不满足条件时同样直接放行
    if (this->mPacked || eventData.mRegion.getBrightnessPair(*eventData.mPos).block->mValue <= 8) {
        origin(eventData);
        return;
    }

    auto&       region = eventData.mRegion;
    auto const& pos    = *eventData.mPos;
    auto&       bus    = ll::event::EventBus::getInstance();

    auto const& sourceBlock = region.getBlock(pos);
    bool const  inNether    = region.getDimensionId() == VanillaDimensions::Nether();

    auto&       registry    = BlockTypeRegistry::mBlockTypeRegistry().mValue;
    auto const& meltedBlock = inNether ? registry.getDefaultBlockState(BedrockBlockNames::Air())
                                       : registry.getDefaultBlockState(VanillaBlockTypeIds::Water());

    IceBlockMeltBeforeEvent beforeEvent(region, pos, sourceBlock, meltedBlock, inNether);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return; // 跳过 vanilla 融化，冰保持原状
    }

    origin(eventData); // vanilla 完成融化（含水/空气转换与音效粒子）

    IceBlockMeltAfterEvent afterEvent(region, pos, sourceBlock, meltedBlock, inNether);
    bus.publish(afterEvent);
}

CATALYST_HOOKED_EVENT_PAIR(
    IceBlockMeltBeforeEvent,
    IceBlockMeltAfterEvent,
    IceBlockMeltHook
)

} // namespace Catalyst
