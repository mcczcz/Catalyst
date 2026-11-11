#include "LiquidFlowEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/LiquidBlock.h"
#include "mc/world/level/material/Material.h"
#include "mc/deps/shared_types/v1_26_20/block/MaterialType.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void LiquidFlowEvent::serialize(CompoundTag& nbt) const {
    ll::event::world::WorldEvent::serialize(nbt);
    nbt["liquidBlock"]       = ll::event::serializeRefObj(liquidBlock());
    nbt["toPos"]             = ListTag{toPos().x, toPos().y, toPos().z};
    nbt["neighbor"]          = neighbor();
    nbt["fromPos"]           = ListTag{fromPos().x, fromPos().y, fromPos().z};
    nbt["flowFromDirection"] = (int)flowFromDirection();
}


namespace {

bool materialsAreEqual(Material const& a, Material const& b) {
    return a.mType == b.mType && a.mNeverBuildable == b.mNeverBuildable && a.mLiquid == b.mLiquid
        && a.mBlocksMotion == b.mBlocksMotion && a.mBlocksPrecipitation == b.mBlocksPrecipitation
        && a.mSolid == b.mSolid && a.mSuperHot == b.mSuperHot;
}

bool liquidBlockCanSpreadTo(
    LiquidBlock&    liquidBlock,
    BlockSource&    region,
    BlockPos const& pos,
    BlockPos const& flowFromPos,
    uchar           flowFromDirection
) {
    if (pos.y < region.getMinHeight()) {
        return false;
    }

    if (const auto& block = region.getLiquidBlock(pos);
        materialsAreEqual(block.getBlockType().mMaterial, liquidBlock.mMaterial)
        || block.getBlockType().mMaterial->mType == SharedTypes::v1_26_20::MaterialType::Lava
        || liquidBlock._isLiquidBlocking(region, pos, flowFromPos, flowFromDirection)) {
        return false;
    }

    return true;
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    LiquidFlowHook,
    ll::memory::HookPriority::Normal,
    LiquidBlock,
    &LiquidBlock::_trySpreadTo,
    void,
    ::BlockSource&    region,
    ::BlockPos const& pos,
    int               neighbor,
    ::BlockPos const& flowFromPos,
    uchar             flowFromDirection
) {
    if (liquidBlockCanSpreadTo(*this, region, pos, flowFromPos, flowFromDirection)) {
        auto& bus = ll::event::EventBus::getInstance();

        LiquidFlowBeforeEvent beforeEvent(region, *this, pos, neighbor, flowFromPos, flowFromDirection);
        bus.publish(beforeEvent);
        if (beforeEvent.isCancelled()) {
            return;
        }

        origin(region, pos, neighbor, flowFromPos, flowFromDirection);

        LiquidFlowAfterEvent afterEvent(region, *this, pos, neighbor, flowFromPos, flowFromDirection);
        bus.publish(afterEvent);
        return;
    }

    origin(region, pos, neighbor, flowFromPos, flowFromDirection);
}

CATALYST_HOOKED_EVENT_PAIR(
    LiquidFlowBeforeEvent,
    LiquidFlowAfterEvent,
    LiquidFlowHook
)

} // namespace Catalyst
