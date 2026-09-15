#pragma once

#include "ll/api/event/Cancellable.h"
#include "ll/api/event/entity/MobEvent.h"
#include "mc/world/level/BlockPos.h"

#include "catalyst/Macros.h"

class Block;

namespace Catalyst {

class CATALYST_API MobTakeBlockEvent : public ll::event::entity::MobEvent {
    BlockPos     mPos;
    Block const& mBlock;

public:
    MobTakeBlockEvent(Mob& mob, BlockPos const& pos, Block const& block) : MobEvent(mob), mPos(pos), mBlock(block) {}

    void serialize(CompoundTag&) const override;

    BlockPos const& pos() const { return mPos; }
    Block const&    block() const { return mBlock; }
};

// 在原生 ActorGriefingBlockEvent 和拾取操作之前触发；取消后不执行拾取。
class CATALYST_API MobTakeBlockBeforeEvent final : public ll::event::Cancellable<MobTakeBlockEvent> {
public:
    using Cancellable::Cancellable;
};

// 方块成功移除且 onTake 执行完成后触发；block() 是拾取前的方块。
// 若回调已移除生物，则不再触发 After。
class CATALYST_API MobTakeBlockAfterEvent final : public MobTakeBlockEvent {
public:
    using MobTakeBlockEvent::MobTakeBlockEvent;
};

} // namespace Catalyst
