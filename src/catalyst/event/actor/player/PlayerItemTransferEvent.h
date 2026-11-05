#pragma once

#include "ll/api/event/Cancellable.h"
#include "ll/api/event/player/PlayerEvent.h"
#include "mc/world/containers/ContainerEnumName.h"
#include "mc/world/containers/FullContainerName.h"
#include "mc/world/inventory/network/ContainerScreenContext.h"
#include "mc/world/inventory/network/ItemStackRequestActionType.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"

#include "catalyst/Macros.h"

class Player;

namespace Catalyst {

class CATALYST_API PlayerItemTransferEvent : public ll::event::PlayerEvent {
    ItemStackRequestActionType mActionType;
    FullContainerName          mSrcContainer;
    uchar                      mSrcSlot;
    FullContainerName          mDstContainer;
    uchar                      mDstSlot;
    uchar                      mAmount;
    ItemStack                  mSrcItem;
    ItemStack                  mDstItem;
    ContainerScreenContext     mScreenContext;

public:
    PlayerItemTransferEvent(
        Player&                       player,
        ItemStackRequestActionType    actionType,
        FullContainerName const&      srcContainer,
        uchar                         srcSlot,
        FullContainerName const&      dstContainer,
        uchar                         dstSlot,
        uchar                         amount,
        ItemStack const&              srcItem,
        ItemStack const&              dstItem,
        ContainerScreenContext const& screenContext
    )
    : PlayerEvent(player),
      mActionType(actionType),
      mSrcContainer(srcContainer),
      mSrcSlot(srcSlot),
      mDstContainer(dstContainer),
      mDstSlot(dstSlot),
      mAmount(amount),
      mSrcItem(srcItem),
      mDstItem(dstItem),
      mScreenContext(screenContext) {}

    void serialize(CompoundTag&) const override;

    ItemStackRequestActionType actionType() const { return mActionType; }
    FullContainerName const&   srcContainer() const { return mSrcContainer; }
    uchar                      srcSlot() const { return mSrcSlot; }
    FullContainerName const&   dstContainer() const { return mDstContainer; }
    uchar                      dstSlot() const { return mDstSlot; }

    // Take/Place 为请求数量，Swap 为交换前源堆叠数量。
    uchar amount() const { return mAmount; }

    // 源/目标物品均为当前动作执行前的快照。
    ItemStack const& srcItem() const { return mSrcItem; }
    ItemStack const& dstItem() const { return mDstItem; }

    ContainerScreenContext const& screenContext() const { return mScreenContext; }

    std::optional<BlockPos> getContainerBlockPos() const {
        auto const& owner = mScreenContext.mOwner.get();
        if (std::holds_alternative<BlockPos>(owner)) {
            return std::get<BlockPos>(owner);
        }
        return std::nullopt;
    }

    std::optional<ActorUniqueID> getContainerActorId() const {
        auto const& owner = mScreenContext.mOwner.get();
        if (std::holds_alternative<ActorUniqueID>(owner)) {
            return std::get<ActorUniqueID>(owner);
        }
        return std::nullopt;
    }
};

class CATALYST_API PlayerItemTransferBeforeEvent final : public ll::event::Cancellable<PlayerItemTransferEvent> {
public:
    using Cancellable::Cancellable;
};

// 单个动作处理成功后触发；整个请求仍可能在后续动作或最终提交时失败。
class CATALYST_API PlayerItemTransferAfterEvent final : public PlayerItemTransferEvent {
public:
    using PlayerItemTransferEvent::PlayerItemTransferEvent;
};

} // namespace Catalyst
