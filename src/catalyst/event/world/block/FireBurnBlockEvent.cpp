#include "FireBurnBlockEvent.h"
#include "FireSpreadEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/IRandom.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/shared_types/legacy/Difficulty.h"
#include "mc/util/Random.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/TickingQueueType.h"
#include "mc/world/level/Weather.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/BedrockBlockNames.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/BlockProperty.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/CampfireBlock.h"
#include "mc/world/level/block/FireBlock.h"
#include "mc/world/level/block/VanillaBlockTypeGroups.h"
#include "mc/world/level/block/VanillaBlockTypeIds.h"
#include "mc/world/level/block/VanillaStates.h"
#include "mc/world/level/block/actor/BeehiveBlockActor.h"
#include "mc/world/level/block/block_events/BlockQueuedTickEvent.h"
#include "mc/world/level/chunk/LevelChunk.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/material/Material.h"
#include "mc/deps/shared_types/v1_26_20/block/MaterialType.h"
#include "mc/world/level/storage/GameRuleId.h"
#include "mc/world/level/storage/GameRules.h"

#include <algorithm>
#include <string_view>

#include "mc/deps/nbt/CompoundTag.h"

namespace Catalyst {

void FireBurnBlockEvent::serialize(CompoundTag& nbt) const {
    ll::event::world::WorldEvent::serialize(nbt);
    nbt["burnPos"] = ListTag{burnPos().x, burnPos().y, burnPos().z};
    nbt["firePos"] = ListTag{firePos().x, firePos().y, firePos().z};
    nbt["age"]     = age();
}

void FireSpreadEvent::serialize(CompoundTag& nbt) const {
    ll::event::world::WorldEvent::serialize(nbt);
    nbt["spreadPos"] = ListTag{spreadPos().x, spreadPos().y, spreadPos().z};
    nbt["firePos"]   = ListTag{firePos().x, firePos().y, firePos().z};
    nbt["newAge"]    = newAge();
    nbt["sourceAge"] = sourceAge();
}


// 辅助函数：检查方块是否为TNT
static bool isTntBlock(Block const& block) {
    auto const& tntIds   = VanillaBlockTypeGroups::TntIds();
    auto const  nameHash = block.mBlockType->mNameInfo.get().mFullName.get().mStrHash;
    for (auto const& ref : tntIds) {
        if (ref.get().mStrHash == nameHash) {
            return true;
        }
    }
    return false;
}

// 辅助函数：检查方块是否为营火
static bool isCampfireBlock(Block const& block) {
    auto const nameHash = block.mBlockType->mNameInfo.get().mFullName.get().mStrHash;
    return nameHash == VanillaBlockTypeIds::CampFire().mStrHash
        || nameHash == VanillaBlockTypeIds::SoulCampfire().mStrHash;
}

// 辅助函数：检查方块是否为蜂巢/蜂窝
static bool isBeehiveBlock(Block const& block) {
    auto const nameHash = block.mBlockType->mNameInfo.get().mFullName.get().mStrHash;
    return nameHash == VanillaBlockTypeIds::Beehive().mStrHash
        || nameHash == VanillaBlockTypeIds::BeeNest().mStrHash;
}

static GameRuleId ruleId(GameRules::GameRulesIndex index) {
    GameRuleId id;
    id.mValue = static_cast<int>(index);
    return id;
}

static void removeFire(BlockSource& region, BlockPos const& pos) {
    BlockChangeContext ctx{};
    region.removeBlock(pos, ctx);
}

static void tryAddFireToTickingQueue(
    FireBlock const& fireBlock,
    BlockSource&     region,
    BlockPos const&  pos,
    IRandom&         random
) {
    if (!region.isInstaticking(pos) && !region.hasTickInPendingTicks(pos, TickingQueueType::Internal)
        && !region.hasTickInPendingTicks(pos, TickingQueueType::Random)) {
        region.addToRandomTickingQueue(pos, *fireBlock.mDefaultState, random.nextInt(10) + 30, 0, false);
    }
}

// 伪代码：方块带 InfiniburnBit 状态时取状态值，否则回退到 BlockProperty::InfiniBurn
static bool isInfiniburnBlock(Block const& block) {
    return block.getState<bool>(VanillaStates::InfiniburnBit())
        .value_or(block.hasProperty(BlockProperty::InfiniBurn));
}

// 26.32 中 FireBlock::getFireOdds 已被内联进 tick：目标必须是空气，取六个相邻方块 mFlameOdds 的最大值
static int getFireOdds(BlockSource& region, BlockPos const& pos) {
    auto const& target = region.getBlock(pos);
    if (target.mBlockType->mNameInfo->mFullName->mStrHash != BedrockBlockNames::Air().mStrHash) {
        return 0;
    }
    auto flameOddsAt = [&](int x, int y, int z) {
        return static_cast<ushort>(region.getBlock(BlockPos(x, y, z)).mDirectData->mFlameOdds);
    };
    ushort odds = flameOddsAt(pos.x + 1, pos.y, pos.z);
    odds        = std::max(odds, flameOddsAt(pos.x - 1, pos.y, pos.z));
    odds        = std::max(odds, flameOddsAt(pos.x, pos.y - 1, pos.z));
    odds        = std::max(odds, flameOddsAt(pos.x, pos.y + 1, pos.z));
    odds        = std::max(odds, flameOddsAt(pos.x, pos.y, pos.z - 1));
    odds        = std::max(odds, flameOddsAt(pos.x, pos.y, pos.z + 1));
    return odds;
}

// 伪代码按 difficulty - 1 查一张 3 项浮点表，Peaceful 越界取 0；
// 表值已在带符号的二进制中确认为 {7, 14, 21}（40 在调用处单独相加）
static float difficultyFlameBonus(SharedTypes::Legacy::Difficulty difficulty) {
    switch (difficulty) {
    case SharedTypes::Legacy::Difficulty::Easy:
        return 7.0f;
    case SharedTypes::Legacy::Difficulty::Normal:
        return 14.0f;
    case SharedTypes::Legacy::Difficulty::Hard:
        return 21.0f;
    default:
        return 0.0f;
    }
}

// checkBurn hook: 按照原版逻辑重写
LL_TYPE_INSTANCE_HOOK(
    FireBurnBlockHook,
    ll::memory::HookPriority::Normal,
    FireBlock,
    &FireBlock::checkBurn,
    void,
    ::BlockSource&    region,
    ::BlockPos const& pos,
    int               chance,
    ::IRandom&        random,
    int               age,
    ::BlockPos const& firePos
) {
    // 获取目标方块
    auto const& block     = region.getBlock(pos);
    auto        flameOdds = static_cast<int>(block.mDirectData.get().mFlameOdds);

    // 处理蜂巢 - 在燃烧前驱逐蜜蜂
    if (isBeehiveBlock(block)) {
        auto* chunk = region.getChunkAt(pos);
        if (chunk) {
            auto* blockActor = region.getBlockEntity(pos);
            if (blockActor) {
                auto* beehive = static_cast<BeehiveBlockActor*>(blockActor);
                beehive->evictAll(region, false);
            }
        }
    }

    // 原版随机检查: nextInt(chance) < flameOdds
    int      randVal   = 0;
    IRandom* randomPtr = &random;
    if (chance > 0) {
        randVal = randomPtr->nextInt(chance);
    }
    if (randVal >= flameOdds) {
        return; // 不燃烧
    }

    // 检查是否为 TNT
    bool isTnt = isTntBlock(block);

    // 检查是否为营火
    bool isCampfire = isCampfireBlock(block);

    // 获取天气
    auto& dimension = region.getDimension();
    auto* weather   = dimension.mWeather.get();

    // 降雨检查 - 如果下雨且温度 > 0.15，阻止燃烧
    bool rainBlocking = false;
    if (weather && weather->isPrecipitatingAt(region, pos)) {
        auto const& biome = region.getBiome(pos);
        if (biome.getTemperature(region, pos) > 0.15000001f) {
            rainBlocking = true;
        }
    }

    auto& bus = ll::event::EventBus::getInstance();

    // 发布 before 事件
    FireBurnBlockBeforeEvent beforeEvent(region, pos, firePos, age);
    bus.publish(beforeEvent);

    if (beforeEvent.isCancelled()) {
        return;
    }

    // 获取游戏规则
    auto&      level     = region.getLevel();
    auto&      gameRules = level.getGameRules();
    GameRuleId doTileDropsId;
    doTileDropsId.mValue = static_cast<int>(GameRules::GameRulesIndex::DoTileDrops);

    if (rainBlocking) {
        // 下雨时的处理
        if (isCampfire) {
            CampfireBlock::tryLightFire(region, pos, nullptr);
        }
        if (isTnt) {
            // TNT 爆炸逻辑
            auto newBlock = block.setState<bool>(VanillaStates::ExplodeBit(), true);
            if (newBlock) {
                newBlock->mBlockType->destroy(region, pos, *newBlock, nullptr);
            }
            // 检查 doTileDrops 规则
            if (gameRules.getBool(doTileDropsId, true)) {
                BlockChangeContext ctx{};
                region.removeBlock(pos, ctx);
            }
        }
        return; // 下雨阻止燃烧
    }

    // 非下雨情况的处理

    // 计算新火焰的 age
    int newFireAge = age;
    if (randomPtr) {
        newFireAge = (std::min)(15, age + randomPtr->nextInt(5) / 4);
    }

    // 获取火焰方块
    auto fireRef = Block::tryGetFromRegistry(std::string_view("minecraft:fire"));

    if (isCampfire) {
        // 营火处理
        CampfireBlock::tryLightFire(region, pos, nullptr);

        // 如果也是 TNT（虽然不太可能同时是营火和TNT）
        if (isTnt) {
            auto newBlock = block.setState<bool>(VanillaStates::ExplodeBit(), true);
            if (newBlock) {
                newBlock->mBlockType->destroy(region, pos, *newBlock, nullptr);
            }
            if (gameRules.getBool(doTileDropsId, true)) {
                BlockChangeContext ctx{};
                region.removeBlock(pos, ctx);
            }
        }
    } else if (!isTnt) {
        // 普通可燃方块处理
        BlockChangeContext ctx{};
        region.removeBlock(pos, ctx);

        // 只有在有效火焰位置才放置火焰
        if (isValidFireLocation(region, pos)) {
            if (fireRef) {
                auto newFireBlock = fireRef->setState<int>(VanillaStates::Age(), newFireAge);
                if (newFireBlock) {
                    region.setBlock(pos, *newFireBlock, 3, nullptr, ctx);
                }
            }
        }

        // 发布 after 事件
        FireBurnBlockAfterEvent afterEvent(region, pos, firePos, age);
        bus.publish(afterEvent);
    } else {
        // TNT 处理
        auto newBlock = block.setState<bool>(VanillaStates::ExplodeBit(), true);
        if (newBlock) {
            newBlock->mBlockType->destroy(region, pos, *newBlock, nullptr);
        }

        // 检查 doTileDrops 规则
        if (gameRules.getBool(doTileDropsId, true)) {
            BlockChangeContext ctx{};
            region.removeBlock(pos, ctx);
        }

        // 发布 after 事件
        FireBurnBlockAfterEvent afterEvent(region, pos, firePos, age);
        bus.publish(afterEvent);
    }
}

// tick hook: 按照 26.32 版 FireBlock::tick 伪代码重写
LL_TYPE_INSTANCE_HOOK(
    FireTickHook,
    ll::memory::HookPriority::Normal,
    FireBlock,
    &FireBlock::tick,
    void,
    ::BlockEvents::BlockQueuedTickEvent& eventData
) {
    auto&          region  = eventData.mRegion;
    auto&          random  = eventData.mRandom;
    BlockPos const firePos = eventData.mPos.get();

    // 尝试生成灵魂火
    if (_trySpawnSoulFire(region, firePos)) {
        return;
    }

    // 检查火焰下方的方块是否为无限燃烧方块 (infiniburn)
    BlockPos const belowPos(firePos.x, firePos.y - 1, firePos.z);
    bool const     isInfiniburn = isInfiniburnBlock(region.getBlock(belowPos));

    // 检查火焰位置是否有效
    if (!mayPlace(region, firePos)) {
        removeFire(region, firePos);
        return;
    }

    auto& level     = region.getLevel();
    auto& gameRules = level.getGameRules();

    if (!gameRules.getBool(ruleId(GameRules::GameRulesIndex::DoFireTick), false)) {
        tryAddFireToTickingQueue(*this, region, firePos, random);
        return;
    }

    // 26.32 新增：allowdestructiveobjects 为 false 时直接移除火焰
    if (!gameRules.getBool(ruleId(GameRules::GameRulesIndex::AllowDestructiveObjects), true)) {
        removeFire(region, firePos);
        return;
    }

    auto& weather = *region.getDimension().mWeather;

    auto isHotRainAt = [&](BlockPos const& pos) -> bool {
        return weather.isPrecipitatingAt(region, pos) && region.getBiome(pos).getTemperature(region, pos) > 0.15f;
    };
    // 伪代码：mOldRainLevel + (mRainLevel - mOldRainLevel) > 0.2
    auto isWeatherRaining = [&]() -> bool {
        return weather.mDimension.mHasWeather
            && (weather.mRainLevel - weather.mOldRainLevel) + weather.mOldRainLevel > 0.2f;
    };

    // 雨水熄灭检查：火焰位置及 ±x 用降水 + 温度判断，±z 用 isRainingAt
    bool const rainOnFire = isHotRainAt(firePos) || isHotRainAt(BlockPos(firePos.x + 1, firePos.y, firePos.z))
                         || isHotRainAt(BlockPos(firePos.x - 1, firePos.y, firePos.z))
                         || weather.isRainingAt(region, BlockPos(firePos.x, firePos.y, firePos.z - 1))
                         || weather.isRainingAt(region, BlockPos(firePos.x, firePos.y, firePos.z + 1));
    if (!isInfiniburn && isWeatherRaining() && rainOnFire) {
        removeFire(region, firePos);
        return;
    }

    auto const& fireBlock     = region.getBlock(firePos);
    int         age           = fireBlock.getState<int>(VanillaStates::Age()).value_or(0);
    auto const  belowMaterial = region.getBlock(belowPos).mBlockType->mMaterial.mType;

    // 非无限燃烧且 age < 15 时增加 age 并写回；之后 currentFire 即为世界中当前的火焰方块
    Block const* currentFire = &fireBlock;
    if (!isInfiniburn && age < 15) {
        age += random.nextInt(3) / 2;
        if (auto updated = fireBlock.setState<int>(VanillaStates::Age(), age)) {
            currentFire = updated.as_ptr();
        }
        BlockChangeContext ctx{};
        region.setBlock(firePos, *currentFire, 1, nullptr, ctx);
    }

    tryAddFireToTickingQueue(*this, region, firePos, random);

    // 无限燃烧方块上的火焰跳过有效性检查，直接蔓延
    if (!isInfiniburn) {
        if (belowMaterial == SharedTypes::v1_26_20::MaterialType::Explosive
            && !gameRules.getBool(ruleId(GameRules::GameRulesIndex::DoTntExplode), false)) {
            if (age >= 4) {
                removeFire(region, firePos);
            }
            return;
        }

        if (isValidFireLocation(region, firePos)
            && region.getLiquidBlock(belowPos).mBlockType->mMaterial.mType
                   != SharedTypes::v1_26_20::MaterialType::Water) {
            auto const belowFlameOdds = static_cast<ushort>(region.getBlock(belowPos).mDirectData->mFlameOdds);
            if (age == 15 && belowFlameOdds == 0 && random.nextInt(4) == 0) {
                removeFire(region, firePos);
                return;
            }
        } else {
            bool const hasSupport = region.getBlock(belowPos).canProvideFullSupport(1);
            if (age <= 3 && hasSupport) {
                return;
            }
            removeFire(region, firePos);
            return;
        }
    }

    // ============ 火焰蔓延逻辑 ============

    // 26.32 中 Biome::isHumid 已移除。伪代码优先读取生物群系的 CustomHumidityAttributes 组件，
    // 但其 type_id 由 BDS 运行时分配、插件侧拿不到，这里只保留伪代码的兜底判断 mDownfall > 0.85
    bool const isHumid = region.getBiome(firePos).mDownfall > 0.85f;

    int const humidPenalty     = isHumid ? 50 : 0;
    int const horizontalChance = 300 - humidPenalty;
    int const verticalChance   = 250 - humidPenalty;

    // 检查6个相邻方块的烧毁 (checkBurn 会触发 FireBurnBlockEvent)，传入的是更新后的 age
    checkBurn(region, BlockPos(firePos.x + 1, firePos.y, firePos.z), horizontalChance, random, age, firePos);
    checkBurn(region, BlockPos(firePos.x - 1, firePos.y, firePos.z), horizontalChance, random, age, firePos);
    checkBurn(region, BlockPos(firePos.x, firePos.y - 1, firePos.z), verticalChance, random, age, firePos);
    checkBurn(region, BlockPos(firePos.x, firePos.y + 1, firePos.z), verticalChance, random, age, firePos);
    checkBurn(region, BlockPos(firePos.x, firePos.y, firePos.z - 1), horizontalChance, random, age, firePos);
    checkBurn(region, BlockPos(firePos.x, firePos.y, firePos.z + 1), horizontalChance, random, age, firePos);

    auto&       bus             = ll::event::EventBus::getInstance();
    float const ageFactor       = static_cast<float>(age + 30);
    float const difficultyBonus = difficultyFlameBonus(level.getDifficulty());

    // 火焰蔓延到周围方块（循环顺序 dx -> dz -> dy 与伪代码一致，影响随机数消耗顺序）
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            for (int dy = -1; dy <= 4; dy++) {
                if (dx == 0 && dy == 0 && dz == 0) continue;

                BlockPos const testPos(firePos.x + dx, firePos.y + dy, firePos.z + dz);
                int const      fireOdds = getFireOdds(region, testPos);
                if (fireOdds == 0) continue;

                float spreadChance = (static_cast<float>(fireOdds) + 40.0f + difficultyBonus) / ageFactor;
                if (isHumid) spreadChance *= 0.5f;
                if (spreadChance <= 0.0f) continue;

                // 伪代码里的 (double)(int)_genRandInt32() * 2^-32 是内联后的 nextFloat()/nextDouble()：
                // 汇编为零扩展后再转换，即无符号、范围 [0, 1)，反编译显示的 (int) 是 Hex-Rays 的误差
                float const heightMultiplier = dy >= 2 ? static_cast<float>(100 * dy) : 100.0f;
                if (spreadChance < static_cast<float>(heightMultiplier * random.nextDouble())) continue;

                // 目标位置及四周下雨时不蔓延
                bool const rainOnTarget = isHotRainAt(testPos)
                                       || isHotRainAt(BlockPos(testPos.x - 1, testPos.y, testPos.z))
                                       || isHotRainAt(BlockPos(testPos.x + 1, testPos.y, testPos.z))
                                       || isHotRainAt(BlockPos(testPos.x, testPos.y, testPos.z - 1))
                                       || isHotRainAt(BlockPos(testPos.x, testPos.y, testPos.z + 1));
                if (rainOnTarget && isWeatherRaining()) continue;

                // 发布火焰蔓延事件
                FireSpreadBeforeEvent spreadBeforeEvent(region, testPos, firePos, age, age);
                bus.publish(spreadBeforeEvent);
                if (spreadBeforeEvent.isCancelled()) continue;

                // 蔓延放置的是当前火焰方块本身（携带更新后的 age），updateFlags = 3
                BlockChangeContext ctx{};
                region.setBlock(testPos, *currentFire, 3, nullptr, ctx);

                FireSpreadAfterEvent spreadAfterEvent(region, testPos, firePos, age, age);
                bus.publish(spreadAfterEvent);
            }
        }
    }
}

CATALYST_HOOKED_EVENT_PAIR(
    FireBurnBlockBeforeEvent,
    FireBurnBlockAfterEvent,
    FireBurnBlockHook,
    FireTickHook
)
CATALYST_HOOKED_EVENT_PAIR(
    FireSpreadBeforeEvent,
    FireSpreadAfterEvent,
    FireBurnBlockHook,
    FireTickHook
)

} // namespace Catalyst
