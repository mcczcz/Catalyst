#pragma once

#include "ll/api/event/Cancellable.h"
#include "ll/api/event/entity/ActorEvent.h"

#include "catalyst/Macros.h"

class Vec3;
class Block;

namespace Catalyst {

class CATALYST_API ActorDestroyBlockEvent : public ll::event::entity::ActorEvent {
    Vec3 const&  mPos;
    Block const& mBlock;

public:
    constexpr ActorDestroyBlockEvent(Actor& actor, Vec3 const& pos, Block const& block)
    : ActorEvent(actor),
      mPos(pos),
      mBlock(block) {}

    void serialize(CompoundTag&) const override;

    Vec3 const&  pos() const { return mPos; }
    Block const& block() const { return mBlock; }
};

// 在带有实体来源的主方块层移除操作之前触发；取消后不执行本次破坏。
class CATALYST_API ActorDestroyBlockBeforeEvent final : public ll::event::Cancellable<ActorDestroyBlockEvent> {
public:
    using Cancellable::Cancellable;
};

// 仅在目标实际移除成功后触发，block() 是被破坏的旧方块。
// 破门开始、失败、取消、放置及非空气方块之间的转换均不会触发。
class CATALYST_API ActorDestroyBlockAfterEvent final : public ActorDestroyBlockEvent {
public:
    using ActorDestroyBlockEvent::ActorDestroyBlockEvent;
};

} // namespace Catalyst
