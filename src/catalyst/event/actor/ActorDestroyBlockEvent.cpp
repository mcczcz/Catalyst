#include "ActorDestroyBlockEvent.h"

#include "catalyst/mod/Gloabl.h"
#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/ActorCategory.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/ActorChangeContext.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void ActorDestroyBlockEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::ActorEvent::serialize(nbt);
    nbt["pos"]   = ListTag{pos().x, pos().y, pos().z};
    nbt["block"] = ll::event::serializeRefObj(block());
}


// 26.32 适配：ActorGriefingBlockEvent 不再通过 ActorEventCoordinator::sendEvent(
// ActorGameplayEvent<CoordinatorResult>) 集中派发（该重载已从运行时移除，生物破坏不再有原版否决通道）。
// 改为拦截 BlockSource::removeBlock 中携带 ActorChangeContext 的调用，
// 这是 26.32 中生物破坏方块的统一入口（烈焰人拆方块、末影龙撞墙、末影人搬方块等均经过此处）。
LL_TYPE_INSTANCE_HOOK(
    ActorDestroyBlockHook,
    ll::memory::HookPriority::Normal,
    ::BlockSource,
    &::BlockSource::$removeBlock,
    bool,
    ::BlockPos const&           pos,
    ::BlockChangeContext const& changeSourceContext
) {
    try {
        auto const* actorContext =
            std::get_if<::ActorChangeContext>(&changeSourceContext.mContextSource.get());
        Actor* actor = actorContext ? actorContext->mActorContext : nullptr;

        if (actor && actor->hasCategory(ActorCategory::Mob)) {
            auto& bus = ll::event::EventBus::getInstance();

            Block const& block = getBlock(pos);
            Vec3 const    blockPos(
                static_cast<float>(pos.x),
                static_cast<float>(pos.y),
                static_cast<float>(pos.z)
            );

            ActorDestroyBlockBeforeEvent beforeEvent(*actor, blockPos, block);
            bus.publish(beforeEvent);
            if (beforeEvent.isCancelled()) {
                return false;
            }

            auto result = origin(pos, changeSourceContext);

            ActorDestroyBlockAfterEvent afterEvent(*actor, blockPos, block);
            bus.publish(afterEvent);

            return result;
        }
        return origin(pos, changeSourceContext);
    } catch (const std::exception& e) {
        logger.warn("ActorDestroyBlockHook 发生异常: {}", e.what());
        return origin(pos, changeSourceContext);
    } catch (...) {
        logger.warn("ActorDestroyBlockHook 发生未知异常！");
        return origin(pos, changeSourceContext);
    }
}

CATALYST_HOOKED_EVENT_PAIR(
    ActorDestroyBlockBeforeEvent,
    ActorDestroyBlockAfterEvent,
    ActorDestroyBlockHook
)

} // namespace Catalyst
