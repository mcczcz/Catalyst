#include "PlayerItemTransferEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/actor/player/Inventory.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/actor/player/PlayerInventory.h"
#include "mc/world/containers/managers/IContainerManager.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/inventory/network/ItemStackNetManagerBase.h"
#include "mc/world/inventory/network/ItemStackNetManagerServer.h"
#include "mc/world/inventory/network/ItemStackNetResult.h"
#include "mc/world/inventory/network/ItemStackRequestAction.h"
#include "mc/world/inventory/network/ItemStackRequestActionHandler.h"
#include "mc/world/inventory/network/ItemStackRequestActionTransferBase.h"
#include "mc/world/inventory/network/ItemStackRequestActionType.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

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


// 26.32 适配：ItemStackRequestActionHandler::handleRequestAction（动作分发入口）已被
// 内联进请求处理循环。Take/Place/Swap 三类转移动作在原版中均经由 _handleTransfer
// 处理，故改为直接钩住 _handleTransfer，其参数即转移动作本体。
LL_TYPE_INSTANCE_HOOK(
    PlayerItemTransferEventHook,
    HookPriority::Normal,
    ItemStackRequestActionHandler,
    &ItemStackRequestActionHandler::_handleTransfer,
    ItemStackNetResult,
    ItemStackRequestActionTransferBase const& requestAction,
    bool const                                isSrcHintSlot,
    bool const                                isDstHintSlot,
    bool const                                isSwap
) {
    auto const  actionType     = requestAction.mActionType;
    auto const& transferAction = requestAction;
    auto&       player         = mPlayer;

    // 获取屏幕上下文
    ContainerScreenContext screenContext = mItemStackNetManager.getScreenContext();

    // 获取源物品槽位信息 - 使用 .get() 访问 TypedStorage 内的数据
    auto const& srcSlotInfo = transferAction.mSrc.get();
    auto const& dstSlotInfo = transferAction.mDst.get();

    // 26.32 适配：Player::getContainerManager() 已无符号，改用 mContainerManager 成员
    //（ContainerManagerModel 实现 IContainerManager）
    auto containerModel = player.mContainerManager.get();
    if (!containerModel) {
        return origin(requestAction, isSrcHintSlot, isDstHintSlot, isSwap);
    }

    // 玩家背包类请求使用绝对背包槽号；ContainerModel 会再次应用区域偏移，不能交给它解析。
    // 其他 UI 容器仍由当前容器管理器按 FullContainerName 解析。
    auto getItemSnapshot = [&](auto const& slotInfo) -> ItemStack {
        auto containerName = slotInfo.mFullContainerName.mName;
        if (containerName == ContainerEnumName::InventoryContainer
            || containerName == ContainerEnumName::HotbarContainer
            || containerName == ContainerEnumName::CombinedHotbarAndInventoryContainer) {
            return player.mInventory->mInventory->getItem(slotInfo.mSlot);
        }
        return containerModel->getFullContainerSlot(slotInfo.mSlot, slotInfo.mFullContainerName);
    };

    // 在执行请求前复制快照，确保 AfterEvent 仍能表示本次实际转移的物品。
    ItemStack srcItem = getItemSnapshot(srcSlotInfo);
    ItemStack dstItem = getItemSnapshot(dstSlotInfo);

    auto& bus = ll::event::EventBus::getInstance();

    PlayerItemTransferBeforeEvent beforeEvent(
        player,
        actionType,
        srcSlotInfo.mFullContainerName,
        srcSlotInfo.mSlot,
        dstSlotInfo.mFullContainerName,
        dstSlotInfo.mSlot,
        transferAction.mAmount,
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
            transferAction.mAmount,
            srcItem,
            dstItem,
            screenContext
        );
        bus.publish(afterEvent);
    }

    return result;
}

CATALYST_HOOKED_EVENT_PAIR(
    PlayerItemTransferBeforeEvent,
    PlayerItemTransferAfterEvent,
    PlayerItemTransferEventHook
)

} // namespace Catalyst
