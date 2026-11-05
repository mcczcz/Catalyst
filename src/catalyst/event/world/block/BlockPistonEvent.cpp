#include "BlockPistonEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "catalyst/mod/Gloabl.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/block/actor/PistonBlockActor.h"
#include "mc/world/level/block/actor/PistonState.h"
#include "mc/world/level/block/actor/component/IVanillaTickBlockActorComponent.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/redstone/circuit/CircuitSystem.h"
#include "mc/world/redstone/circuit/components/BaseCircuitComponent.h"


#include "mc/deps/nbt/CompoundTag.h"

namespace Catalyst {

void BlockPistonEvent::serialize(CompoundTag& nbt) const {
    ll::event::world::WorldEvent::serialize(nbt);
    nbt["pos"]       = ListTag{pos().x, pos().y, pos().z};
    nbt["action"]    = magic_enum::enum_name(action());
    nbt["direction"] = direction();
}

int getPistonStrength(CircuitSystem const& circuit, BlockPos const& pos) {
    // CircuitSystem::getStrength 已内联到 tick：只查正式组件表，已移除的组件不再提供信号。
    auto const& components = circuit.mSceneGraph->mAllComponents;
    auto        it         = components.find(pos);
    if (it == components.end() || !it->second || it->second->mRemoved) {
        return -1;
    }
    return it->second->getStrength();
}


enum class PistonStateEx : char {
    ExpandingCancelled  = 4,
    RetractingCancelled = 5,
};

LL_TYPE_INSTANCE_HOOK(
    PistonBlockEventHook,
    ll::memory::HookPriority::Normal,
    PistonBlockActor,
    &PistonBlockActor::$tick,
    void,
    ::BlockSource& region
) {
    // Windows tick 的 this 指向 tick 组件子对象；读取活塞成员前先恢复完整对象地址。
    // origin 仍使用 Hook 收到的原始 this，与原函数的调用约定保持一致。
    auto* piston = thisFor<IVanillaTickBlockActorComponent>();

    bool  extending  = false;
    bool  retracting = false;
    auto& dimension  = region.getDimension();
    auto& circuit    = dimension.mCircuitSystem;
    auto  pos        = piston->mPosition;
    auto  state      = piston->mState;

    // 与 Endstone 一致，仅在红石更新周期判断动作。
    if (dimension.mCircuitSystemTickRate < dimension.CIRCUIT_TICK_RATE || !circuit) {
        origin(region);
        return;
    }

    auto strength = getPistonStrength(*circuit, *pos);
    /*

    logger.info(
        "活塞位置: ({}, {}, {}), 当前状态: {}, 新状态: {}, 信号强度: {}",
        pos->x,
        pos->y,
        pos->z,
        (int)state,
        (int)piston->mNewState,
        strength
    );
*/
    // -1 表示尚无有效电路组件，不能当作断电触发收缩或重置取消状态。
    if (strength == -1) {
        origin(region);
        return;
    }

    if (strength > 0) {

        if (state == PistonState::Retracted && piston->mNewState == PistonState::Retracted) {
            extending = true;
            logger.debug("活塞准备伸展 - 位置: ({}, {}, {})", pos->x, pos->y, pos->z);
        } else if (
            state == PistonState::Expanded
            && static_cast<PistonStateEx>(piston->mNewState) == PistonStateEx::RetractingCancelled
        ) {
            piston->mNewState = PistonState::Expanded;
            logger.debug("重置活塞收缩取消状态");
        }
    } else {
        // 无红石信号时处理收缩 - 只在状态转换时触发
        if (state == PistonState::Expanded && piston->mNewState == PistonState::Expanded) {
            retracting = true;
            logger.debug("活塞准备收缩 - 位置: ({}, {}, {})", pos->x, pos->y, pos->z);
        } else if (
            state == PistonState::Retracted
            && static_cast<PistonStateEx>(piston->mNewState) == PistonStateEx::ExpandingCancelled
        ) {
            piston->mNewState = PistonState::Retracted;
            logger.debug("重置活塞伸展取消状态");
        }
    }

    if (extending || retracting) {
        auto& bus       = ll::event::EventBus::getInstance();
        auto& block     = region.getBlock(pos);
        auto  facing    = block.getState<int>(VanillaStates::FacingDirection());
        int   direction = facing.has_value() ? facing.value() : 0;

        PistonAction action = extending ? PistonAction::Extend : PistonAction::Retract;


        BlockPistonBeforeEvent beforeEvent(region, pos, action, direction);
        bus.publish(beforeEvent);

        logger.debug(
            "活塞{}事件 - 位置: ({}, {}, {}), 方向: {}",
            extending ? "伸展" : "收缩",
            pos->x,
            pos->y,
            pos->z,
            direction
        );

        if (beforeEvent.isCancelled()) {
            // 设置取消状态
            if (extending) {
                piston->mNewState = static_cast<PistonState>(PistonStateEx::ExpandingCancelled);
                logger.debug("活塞伸展被取消 - 位置: ({}, {}, {})", pos->x, pos->y, pos->z);
            } else {
                piston->mNewState = static_cast<PistonState>(PistonStateEx::RetractingCancelled);
                logger.debug("活塞收缩被取消 - 位置: ({}, {}, {})", pos->x, pos->y, pos->z);
            }
            origin(region);
            return;
        }


        origin(region);

        BlockPistonAfterEvent afterEvent(region, pos, action, direction);
        bus.publish(afterEvent);

        logger.debug("活塞{}完成 - 位置: ({}, {}, {})", extending ? "伸展" : "收缩", pos->x, pos->y, pos->z);
    } else {
        origin(region);
    }
}

CATALYST_HOOKED_EVENT_PAIR(BlockPistonBeforeEvent, BlockPistonAfterEvent, PistonBlockEventHook)

} // namespace Catalyst
