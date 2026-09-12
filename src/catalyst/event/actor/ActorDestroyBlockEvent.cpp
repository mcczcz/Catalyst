#include "ActorDestroyBlockEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/EventRefObjSerializer.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/ecs/WeakEntityRef.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/ActorChangeContext.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"

namespace Catalyst {

void ActorDestroyBlockEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::ActorEvent::serialize(nbt);
    nbt["pos"]   = ListTag{pos().x, pos().y, pos().z};
    nbt["block"] = ll::event::serializeRefObj(block());
}


namespace {

// Level::destroyBlock / BlockSource::removeBlock 最终都会进入 setBlock。
// 只在最外层发布事件，记录仅在这一次实际破坏调用期间有效。
struct DestructionAttempt {
    inline static thread_local DestructionAttempt* current = nullptr;

    DestructionAttempt* parent = current;
    BlockSource&        source;
    BlockPos            pos;
    Actor*              actor;
    Block const&        block;
    bool                executing = false;
    bool                removed   = false;

    DestructionAttempt(BlockSource& source, BlockPos const& pos, Actor* actor, Block const& block)
    : source(source),
      pos(pos),
      actor(actor),
      block(block) {
        current = this;
    }

    ~DestructionAttempt() { current = parent; }

    DestructionAttempt(DestructionAttempt const&)            = delete;
    DestructionAttempt& operator=(DestructionAttempt const&) = delete;

    bool matches(BlockSource& otherSource, BlockPos const& otherPos, Actor* otherActor, Block const& otherBlock) const {
        return &source == &otherSource && pos == otherPos && actor == otherActor && &block == &otherBlock;
    }
};

template <class Origin>
bool withBlockDestructionEvents(
    BlockSource&              source,
    BlockPos const&           pos,
    BlockChangeContext const& context,
    Origin&&                  origin
) {
    auto const* actorContext = std::get_if<ActorChangeContext>(&context.mContextSource.get());
    Actor*      actor        = actorContext ? actorContext->mActorContext : nullptr;
    if (!actor || source.getLevel().isClientSide()) {
        return origin();
    }

    auto const& block = source.getBlock(pos);
    if (block.isAir()) {
        return origin();
    }
    for (auto* attempt = DestructionAttempt::current; attempt; attempt = attempt->parent) {
        if (attempt->matches(source, pos, actor, block)) {
            // Before 监听器重入同一目标时，不能绕过尚未确定的取消结果。
            return attempt->executing ? origin() : false;
        }
    }

    auto       actorRef = actor->getEntityContext().getWeakRef();
    BlockPos   blockPos = pos;
    Vec3 const eventPos{static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)};
    auto&      bus = ll::event::EventBus::getInstance();

    bool result;
    bool removed;
    {
        DestructionAttempt           attempt(source, blockPos, actor, block);
        ActorDestroyBlockBeforeEvent beforeEvent(*actor, eventPos, block);
        bus.publish(beforeEvent);
        // 监听器可能已经移除实体或替换目标，不能继续破坏一个未经 Before 检查的新方块。
        if (beforeEvent.isCancelled() || !actorRef.tryUnwrap<Actor>() || &source.getBlock(blockPos) != &block) {
            return false;
        }
        attempt.executing = true;
        result            = origin();
        removed           = attempt.removed;
    }

    // 返回成功还不够：必须收到本次写入的实际变更，且目标仍为空气。
    if (result && removed && source.getBlock(blockPos).isAir()) {
        if (auto currentActor = actorRef.tryUnwrap<Actor>()) {
            ActorDestroyBlockAfterEvent afterEvent(*currentActor, eventPos, block);
            bus.publish(afterEvent);
        }
    }
    return result;
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    ActorDestroyBlockLevelHook,
    HookPriority::Normal,
    Level,
    &Level::$destroyBlock,
    bool,
    BlockSource&              source,
    BlockPos const&           pos,
    bool                      dropResources,
    BlockChangeContext const& context
) {
    // 在 Level 生成掉落物之前允许取消。
    return withBlockDestructionEvents(source, pos, context, [&] {
        return origin(source, pos, dropResources, context);
    });
}

LL_TYPE_INSTANCE_HOOK(
    ActorDestroyBlockRemoveHook,
    HookPriority::Normal,
    BlockSource,
    &BlockSource::$removeBlock,
    bool,
    BlockPos const&           pos,
    BlockChangeContext const& context
) {
    // 破门只会在进度完成、实际调用 removeBlock 时进入这里。
    return withBlockDestructionEvents(*this, pos, context, [&] { return origin(pos, context); });
}

LL_TYPE_INSTANCE_HOOK(
    ActorDestroyBlockSetHook,
    HookPriority::Normal,
    BlockSource,
    &BlockSource::setBlock,
    bool,
    BlockPos const&              pos,
    Block const&                 block,
    int                          updateFlags,
    std::shared_ptr<BlockActor>  blockEntity,
    ActorBlockSyncMessage const* syncMsg,
    BlockChangeContext const&    context
) {
    auto setBlock = [&] { return origin(pos, block, updateFlags, std::move(blockEntity), syncMsg, context); };
    // 只将非空气方块被移除算作破坏，放置、状态变化和方块转换不触发。
    return block.isAir() ? withBlockDestructionEvents(*this, pos, context, setBlock) : setBlock();
}

LL_TYPE_INSTANCE_HOOK(
    ActorDestroyBlockChangedHook,
    HookPriority::Normal,
    BlockSource,
    &BlockSource::_blockChanged,
    void,
    BlockPos const&              pos,
    uint                         layer,
    Block const&                 block,
    Block const&                 previousBlock,
    int                          updateFlags,
    bool                         fireEvent,
    ActorBlockSyncMessage const* syncMsg,
    Actor*                       blockChangeSource
) {
    // IDA: setBlock 在区块写入后传入实际的新旧方块；额外层及强制的无变化通知不算破坏。
    if (layer == 0 && blockChangeSource && block.isAir() && !previousBlock.isAir()) {
        for (auto* attempt = DestructionAttempt::current; attempt; attempt = attempt->parent) {
            if (attempt->matches(*this, pos, blockChangeSource, previousBlock)) {
                attempt->removed = true;
                break;
            }
        }
    }
    origin(pos, layer, block, previousBlock, updateFlags, fireEvent, syncMsg, blockChangeSource);
}

CATALYST_HOOKED_EVENT_PAIR(
    ActorDestroyBlockBeforeEvent,
    ActorDestroyBlockAfterEvent,
    ActorDestroyBlockLevelHook,
    ActorDestroyBlockRemoveHook,
    ActorDestroyBlockSetHook,
    ActorDestroyBlockChangedHook
)

} // namespace Catalyst
