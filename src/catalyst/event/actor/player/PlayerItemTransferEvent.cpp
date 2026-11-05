#include "PlayerItemTransferEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/EventRefObjSerializer.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/SimpleSparseContainer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/inventory/network/ItemStackNetResult.h"
#include "mc/world/inventory/network/ItemStackRequestActionHandler.h"
#include "mc/world/inventory/network/ItemStackRequestActionTransferBase.h"
#include "mc/world/inventory/network/ItemStackRequestActionType.h"

namespace Catalyst {

void PlayerItemTransferEvent::serialize(CompoundTag& nbt) const {
    ll::event::PlayerEvent::serialize(nbt);
    nbt["actionType"]    = magic_enum::enum_name(actionType());
    nbt["srcContainer"]  = ll::event::serializeRefObj(srcContainer());
    nbt["srcSlot"]       = (int)srcSlot();
    nbt["dstContainer"]  = ll::event::serializeRefObj(dstContainer());
    nbt["dstSlot"]       = (int)dstSlot();
    nbt["amount"]        = (int)amount();
    nbt["srcItem"]       = ll::event::serializeRefObj(srcItem());
    nbt["dstItem"]       = ll::event::serializeRefObj(dstItem());
    nbt["screenContext"] = ll::event::serializeRefObj(screenContext());
}


LL_TYPE_INSTANCE_HOOK(
    PlayerItemTransferEventHook,
    HookPriority::Normal,
    ItemStackRequestActionHandler,
    &ItemStackRequestActionHandler::_handleTransfer,
    ItemStackNetResult,
    ItemStackRequestActionTransferBase const& requestAction,
    bool                                      isSrcHintSlot,
    bool                                      isDstHintSlot,
    bool                                      isSwap
) {
    auto actionType = requestAction.mActionType;

    // 只处理物品转移相关的操作
    if (actionType != ItemStackRequestActionType::Take && actionType != ItemStackRequestActionType::Place
        && actionType != ItemStackRequestActionType::Swap) {
        return origin(requestAction, isSrcHintSlot, isDstHintSlot, isSwap);
    }

    auto&       player      = mPlayer;
    auto const& srcSlotInfo = requestAction.mSrc.get();
    auto const& dstSlotInfo = requestAction.mDst.get();

    // 临时容器包含同一请求中先前动作的结果；不要重复调用有缓存副作用的 _validateRequestSlot。
    auto srcContainer = _getOrInitSparseContainer(srcSlotInfo.mFullContainerName);
    if (!srcContainer) {
        return origin(requestAction, isSrcHintSlot, isDstHintSlot, isSwap);
    }
    auto dstContainer = _getOrInitSparseContainer(dstSlotInfo.mFullContainerName);
    if (!dstContainer) {
        return origin(requestAction, isSrcHintSlot, isDstHintSlot, isSwap);
    }

    // Before/After 共用动作执行前的快照，并使用当前请求对应的屏幕上下文。
    ItemStack              srcItem       = srcContainer->getItem(srcSlotInfo.mSlot);
    ItemStack              dstItem       = dstContainer->getItem(dstSlotInfo.mSlot);
    ContainerScreenContext screenContext = getScreenContext();
    // Swap 不序列化 mAmount；Take/Place 保留请求数量语义，由原版处理数量限制。
    uchar const amount = isSwap ? srcItem.mCount : requestAction.mAmount;

    auto& bus = ll::event::EventBus::getInstance();

    PlayerItemTransferBeforeEvent beforeEvent(
        player,
        actionType,
        srcSlotInfo.mFullContainerName,
        srcSlotInfo.mSlot,
        dstSlotInfo.mFullContainerName,
        dstSlotInfo.mSlot,
        amount,
        srcItem,
        dstItem,
        screenContext
    );
    bus.publish(beforeEvent);

    if (beforeEvent.isCancelled()) {
        return ItemStackNetResult::Error;
    }

    auto result = origin(requestAction, isSrcHintSlot, isDstHintSlot, isSwap);

    if (result == ItemStackNetResult::Success) {
        PlayerItemTransferAfterEvent afterEvent(
            player,
            actionType,
            srcSlotInfo.mFullContainerName,
            srcSlotInfo.mSlot,
            dstSlotInfo.mFullContainerName,
            dstSlotInfo.mSlot,
            amount,
            srcItem,
            dstItem,
            screenContext
        );
        bus.publish(afterEvent);
    }

    return result;
}

CATALYST_HOOKED_EVENT_PAIR(PlayerItemTransferBeforeEvent, PlayerItemTransferAfterEvent, PlayerItemTransferEventHook)

} // namespace Catalyst
