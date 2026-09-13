#include "IceBlockMeltEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/LevelEvent.h"
#include "mc/deps/shared_types/legacy/LevelSoundEvent.h"
#include "mc/world/actor/ActorSoundIdentifier.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/BedrockBlockNames.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/IceBlock.h"
#include "mc/world/level/block/VanillaBlockTypeIds.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"
#include "mc/world/level/dimension/DimensionType.h"
#include "mc/world/level/dimension/VanillaDimensions.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"
#include <optional>

namespace Catalyst {

void IceBlockMeltEvent::serialize(CompoundTag& nbt) const {
    ll::event::world::WorldEvent::serialize(nbt);
    nbt["pos"]         = ListTag{pos().x, pos().y, pos().z};
    nbt["sourceBlock"] = ll::event::serializeRefObj(sourceBlock());
    nbt["meltedBlock"] = ll::event::serializeRefObj(meltedBlock());
    nbt["inNether"]    = isInNether();
}


LL_STATIC_HOOK(
    IceBlockMeltHook,
    ll::memory::HookPriority::Normal,
    &IceBlock::_getMeltedBlockAndSendEvents,
    ::Block const&,
    ::BlockSource&    region,
    ::BlockPos const& pos
) {
    auto& bus = ll::event::EventBus::getInstance();

    auto const& sourceBlock = region.getBlock(pos);
    bool const  inNether    = region.getDimensionId() == VanillaDimensions::Nether();

    auto&       registry     = BlockTypeRegistry::mBlockTypeRegistry().mValue;
    auto const& meltedBlock  = inNether ? registry.getDefaultBlockState(BedrockBlockNames::Air())
                                        : registry.getDefaultBlockState(VanillaBlockTypeIds::FlowingWater());

    IceBlockMeltBeforeEvent beforeEvent(region, pos, sourceBlock, meltedBlock, inNether);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        // Force a block update to keep client and server in sync when melting is intercepted.
        auto const sourceBlockName = HashedString{std::string_view{sourceBlock.getTypeName()}};
        if (auto cancelledBlock = Block::tryGetFromRegistry(sourceBlockName, sourceBlock.getData())) {
            region._blockChanged(pos, 0, *cancelledBlock, *cancelledBlock, 3, true, nullptr, nullptr);
            return *cancelledBlock;
        }

        region._blockChanged(pos, 0, sourceBlock, sourceBlock, 3, true, nullptr, nullptr);
        return sourceBlock;
    }

    if (inNether) {
        auto& level = region.getLevel();

        level.broadcastSoundEvent(
            region,
            SharedTypes::Legacy::LevelSoundEvent::Fizz,
            pos.center(),
            -1,
            ActorSoundIdentifier{},
            false,
            ::std::nullopt
        );

        Vec3 particlePos{
            static_cast<float>(pos.x),
            static_cast<float>(pos.y),
            static_cast<float>(pos.z)
        };
        level.broadcastLocalEvent(
            region,
            SharedTypes::Legacy::LevelEvent::ParticlesEvaporateWater,
            particlePos,
            sourceBlock
        );
    }

    IceBlockMeltAfterEvent afterEvent(region, pos, sourceBlock, meltedBlock, inNether);
    bus.publish(afterEvent);

    return meltedBlock;
}

CATALYST_HOOKED_EVENT_PAIR(
    IceBlockMeltBeforeEvent,
    IceBlockMeltAfterEvent,
    IceBlockMeltHook
)

} // namespace Catalyst
