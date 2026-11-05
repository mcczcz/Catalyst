#include "BlockFallEvent.h"

#include "catalyst/mod/Gloabl.h"
#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/deps/game_refs/WeakRef.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorDefinitionIdentifier.h"
#include "mc/world/actor/ActorFactory.h"
#include "mc/world/actor/item/FallingBlockActor.h"
#include "mc/world/level/ActorBlockSyncMessage.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/BedrockBlockNames.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/FallingBlock.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void BlockFallEvent::serialize(CompoundTag& nbt) const {
    ll::event::world::WorldEvent::serialize(nbt);
    nbt["pos"]      = ListTag{pos().x, pos().y, pos().z};
    nbt["block"]    = ll::event::serializeRefObj(block());
    nbt["creative"] = isCreative();
}


LL_TYPE_INSTANCE_HOOK(
    BlockFallEventHook5,
    ll::memory::HookPriority::High,//防ila
    FallingBlock,
    &FallingBlock::$startFalling,
    void,
    ::BlockSource&    region,
    ::BlockPos const& pos,
    ::Block const&    oldBlock,
    bool              creative
) {
    logger.info("BlockFallEventHook triggered for block at position ({}, {}, {})", pos.x, pos.y, pos.z);
    auto& bus = ll::event::EventBus::getInstance();

    // 触发 Before 事件
    BlockFallBeforeEvent beforeEvent(region, pos, oldBlock, creative);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    // 获取 Level 和 ActorFactory
    auto&         level        = region.getLevel();
    ActorFactory& actorFactory = level.getActorFactory();

    // 创建 FallingBlock 实体
    Vec3                      centerPos = pos.center();
    ActorDefinitionIdentifier identifier(
        static_cast<ActorType>(66), // FallingBlock 实体类型
        ""                          // 空名称
    );
    Vec2                    rotation(0.0f, 0.0f); // 零旋转
    OwnerPtr<EntityContext> entityContext = actorFactory.createSpawnedActor(
        identifier,
        nullptr,   // 无生成者
        centerPos, // 方块中心位置
        rotation   // 旋转角度
    );
    if (!entityContext) {
        return;
    }

    // 获取 Actor 指针
    Actor* actor = Actor::tryGetFromEntity(*entityContext, false);
    if (!actor) {
        logger.warn("Failed to get actor from entity context");
        return;
    }
    if (actor->mRemoved) {
        logger.warn("Actor is already removed");
        return;
    }

    // 配置 FallingBlockActor
    FallingBlockActor* fallingBlockActor = static_cast<FallingBlockActor*>(actor);
    fallingBlockActor->setFallingBlock(oldBlock, creative);

    // 设置 Actor 的 Level 引用
    fallingBlockActor->mLevel = &region.getLevel();

    // 获取实体唯一ID，用于方块同步消息
    ActorUniqueID const&  uniqueId = actor->getOrCreateUniqueID();
    ActorBlockSyncMessage syncMsg(uniqueId, ActorBlockSyncMessage::MessageId::Create);

    // 创建方块变更上下文
    BlockChangeContext changeContext{};
    changeContext.mContextSource = ActorChangeContext{actor};

    // 将原位置替换为空气方块
    BlockTypeRegistry&  registry = BlockTypeRegistry::mBlockTypeRegistry().mValue;
    const HashedString& airName  = BedrockBlockNames::Air();
    const Block&        airBlock = registry.getDefaultBlockState(airName, false);
    region.setBlock(pos, airBlock, 3, &syncMsg, changeContext);

    // 获取 FallingBlock 类型
    FallingBlock const* blockType = static_cast<FallingBlock const*>(&oldBlock.getBlockType());
    if (!blockType) {
        return;
    }

    // 触发上方方块更新 (y + 1)
    BlockPos abovePos{pos.x, pos.y + 1, pos.z};
    blockType->_tickBlocksAround2D(region, abovePos, oldBlock);

    // 触发下方方块更新 (y - 1)
    BlockPos belowPos{pos.x, pos.y - 1, pos.z};
    blockType->_tickBlocksAround2D(region, belowPos, oldBlock);

    // 将实体添加到世界
    region.getLevel().addEntity(region, entityContext);

    // 触发 After 事件
    BlockFallAfterEvent afterEvent(region, pos, oldBlock, creative);
    bus.publish(afterEvent);
}

CATALYST_HOOKED_EVENT_PAIR(
    BlockFallBeforeEvent,
    BlockFallAfterEvent,
    BlockFallEventHook5
)

} // namespace Catalyst
