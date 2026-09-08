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
#include "mc/world/level/Weather.h"
#include "mc/world/level/biome/Biome.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockChangeContext.h"
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

static void tryAddFireToTickingQueue(
    FireBlock const& /*fireBlock*/,
    BlockSource&     region,
    BlockPos const&  pos,
    IRandom&         random
) {
    // 26.32 适配：hasTickInPendingTicks 需显式指定队列类型；FireBlock::getDefaultState
    // 已被移除，改用 pos 处的当前方块（所有调用点都保证该位置是火焰方块）。
    if (!region.isInstaticking(pos) && !region.hasTickInPendingTicks(pos, ::TickingQueueType::Random)) {
        region.addToRandomTickingQueue(pos, region.getBlock(pos), random.nextInt(10) + 30, 0, false);
    }
}

// 26.32 适配：FireBlock::getFireOdds 已被内联进 tick，按原版语义重新实现：
// 返回目标位置方块的 flame_odds 原始值（0~60），与难度加成（47/54/61）同量纲。
static float getFireOdds(BlockSource& region, BlockPos const& pos) {
    return static_cast<float>(region.getBlock(pos).mDirectData.get().mFlameOdds);
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

// tick hook: 按照原版逻辑重写
LL_TYPE_INSTANCE_HOOK(
    FireTickHook,
    ll::memory::HookPriority::Normal,
    FireBlock,
    &FireBlock::tick,
    void,
    ::BlockEvents::BlockQueuedTickEvent& eventData
) {
    auto& region     = eventData.mRegion;
    auto& firePosRef = eventData.mPos;
    auto& random     = eventData.mRandom;

    BlockPos firePos(firePosRef.get().x, firePosRef.get().y, firePosRef.get().z);

    // 尝试生成灵魂火
    if (_trySpawnSoulFire(region, firePos)) {
        return;
    }

    // 检查火焰下方的方块
    BlockPos belowPos(firePos.x, firePos.y - 1, firePos.z);
    auto const& belowBlock = region.getBlock(belowPos);

    // 检查是否为无限燃烧方块 (infiniburn)
    auto infiniburnOpt = belowBlock.getState<bool>(VanillaStates::InfiniburnBit());
    bool isInfiniburn  = infiniburnOpt.has_value() && infiniburnOpt.value();

    // 检查火焰位置是否有效
    if (!mayPlace(region, firePos)) {
        BlockChangeContext ctx{};
        region.removeBlock(firePos, ctx);
        return;
    }

    // 获取游戏规则
    auto&      level     = region.getLevel();
    auto&      gameRules = level.getGameRules();
    GameRuleId doFireTickId;
    doFireTickId.mValue = static_cast<int>(GameRules::GameRulesIndex::DoFireTick);

    if (!gameRules.getBool(doFireTickId, true)) {
        tryAddFireToTickingQueue(*this, region, firePos, random);
        return;
    }

    // 获取天气
    auto& dimension = region.getDimension();
    auto* weather   = dimension.mWeather.get();

    // 雨水熄灭检查
    if (weather && !isInfiniburn && dimension.mHasWeather && weather->mRainLevel > 0.2f) {
        auto checkRainExtinguish = [&](BlockPos const& checkPos) -> bool {
            if (weather->isPrecipitatingAt(region, checkPos)) {
                auto const& biome = region.getBiome(checkPos);
                if (biome.getTemperature(region, checkPos) > 0.15000001f) {
                    return true;
                }
            }
            return false;
        };

        bool extinguishByRain = false;
        if (checkRainExtinguish(firePos)) {
            extinguishByRain = true;
        } else if (checkRainExtinguish(BlockPos(firePos.x + 1, firePos.y, firePos.z))) {
            extinguishByRain = true;
        } else if (weather->isRainingAt(region, BlockPos(firePos.x - 1, firePos.y, firePos.z))) {
            extinguishByRain = true;
        } else if (weather->isRainingAt(region, BlockPos(firePos.x, firePos.y, firePos.z - 1))) {
            extinguishByRain = true;
        } else if (weather->isRainingAt(region, BlockPos(firePos.x, firePos.y, firePos.z + 1))) {
            extinguishByRain = true;
        }

        if (extinguishByRain) {
            BlockChangeContext ctx{};
            region.removeBlock(firePos, ctx);
            return;
        }
    }

    // 获取当前火焰的 age
    auto const& fireBlock = region.getBlock(firePos);
    auto        ageOpt    = fireBlock.getState<int>(VanillaStates::Age());
    int         age       = ageOpt.has_value() ? ageOpt.value() : 0;

    // 获取下方方块的材质类型
    auto belowMaterialType = belowBlock.mBlockType->mMaterial.mType;

    // 获取 TNT 爆炸游戏规则
    GameRuleId doTntExplodeId;
    doTntExplodeId.mValue = static_cast<int>(GameRules::GameRulesIndex::DoTntExplode);

    int newAge = age;

    // 非无限燃烧方块，增加 age
    if (!isInfiniburn && age < 15) {
        newAge = age + random.nextInt(3) / 2;

        auto newBlock = fireBlock.setState<int>(VanillaStates::Age(), newAge);
        if (newBlock) {
            BlockChangeContext ctx{};
            region.setBlock(firePos, *newBlock, 1, nullptr, ctx);
        }

        tryAddFireToTickingQueue(*this, region, firePos, random);

        // 进入 LABEL_45 逻辑 (有效性检查)
        goto LABEL_45;
    }

    // 对于无限燃烧方块或 age >= 15
    tryAddFireToTickingQueue(*this, region, firePos, random);

    if (!isInfiniburn) {
        // age >= 15 的情况
        newAge = age;
        goto LABEL_45;
    }

    // isInfiniburn 为 true，进入火焰蔓延逻辑 (LABEL_55)
    goto LABEL_55;

LABEL_45:
    // 有效性检查逻辑
    {
        // TNT 材质检查
        if (belowMaterialType == SharedTypes::v1_26_20::MaterialType::Explosive && !gameRules.getBool(doTntExplodeId, true)) {
            // 材质是爆炸物且禁止TNT爆炸，跳过有效性检查
            goto CHECK_AGE_REMOVE;
        }

        bool validLocation = isValidFireLocation(region, firePos);

        if (validLocation) {
            // 检查下方液体
            BlockPos belowFirePos(firePos.x, firePos.y - 1, firePos.z);
            auto const& liquidBelow = region.getLiquidBlock(belowFirePos);

            if (liquidBelow.mBlockType->mMaterial.mType != SharedTypes::v1_26_20::MaterialType::Water) {
                // 没有水，检查 flameOdds/age/random 条件
                auto const& blockBelowFire = region.getBlock(belowFirePos);
                auto        flameOdds      = static_cast<ushort>(blockBelowFire.mDirectData.get().mFlameOdds);

                if (flameOdds == 0 && newAge == 15 && random.nextInt(4) == 0) {
                    // 满足熄灭条件
                    goto REMOVE_FIRE;
                }

                // 有效位置，没有水，通过检查 -> 进入火焰蔓延
                goto LABEL_55;
            }
            // 有水，继续检查下方方块
        }

        // 无效位置或有水，检查下方是否为实心顶面方块
        BlockPos belowFirePos(firePos.x, firePos.y - 1, firePos.z);
        if (!region.getBlock(belowFirePos).canProvideFullSupport(1u)) {
            goto REMOVE_FIRE;
        }
        // 有实心顶面，继续
    }

CHECK_AGE_REMOVE:
    if (newAge <= 3) {
        return; // 保留火焰，不蔓延
    }

REMOVE_FIRE:
    {
        BlockChangeContext ctx{};
        region.removeBlock(firePos, ctx);
        return;
    }

LABEL_55:
    // ============ 火焰蔓延逻辑 ============

    auto const& biome  = region.getBiome(firePos);
    // 26.32 适配：Biome::isHumid 已移除，按原版语义以降水量（downfall > 0.85）判定潮湿
    bool isHumid = biome.mDownfall > 0.85f;

    // 烧毁相邻方块的概率
    int horizontalChance = isHumid ? 250 : 300;
    int verticalChance   = isHumid ? 200 : 250;

    auto& bus = ll::event::EventBus::getInstance();

    // 检查6个相邻方块的烧毁 (checkBurn 会触发 FireBurnBlockEvent)
    checkBurn(region, BlockPos(firePos.x + 1, firePos.y, firePos.z), horizontalChance, random, age, firePos);
    checkBurn(region, BlockPos(firePos.x - 1, firePos.y, firePos.z), horizontalChance, random, age, firePos);
    checkBurn(region, BlockPos(firePos.x, firePos.y - 1, firePos.z), verticalChance, random, age, firePos);
    checkBurn(region, BlockPos(firePos.x, firePos.y + 1, firePos.z), verticalChance, random, age, firePos);
    checkBurn(region, BlockPos(firePos.x, firePos.y, firePos.z - 1), horizontalChance, random, age, firePos);
    checkBurn(region, BlockPos(firePos.x, firePos.y, firePos.z + 1), horizontalChance, random, age, firePos);

    // 火焰蔓延到周围方块
    for (int dx = -1; dx <= 1; dx++) {
        for (int dz = -1; dz <= 1; dz++) {
            for (int dy = -1; dy <= 4; dy++) {
                if (dx == 0 && dy == 0 && dz == 0) continue;

                int heightMultiplier = 100;
                if (dy > 1) heightMultiplier = 100 * dy;

                BlockPos testPos(firePos.x + dx, firePos.y + dy, firePos.z + dz);
                float    fireOdds = getFireOdds(region, testPos);

                if (fireOdds <= 0.0f) continue;

                // 难度修正
                auto  difficulty      = level.getDifficulty();
                float difficultyBonus = 0.0f;
                switch (difficulty) {
                case SharedTypes::Legacy::Difficulty::Easy:
                    difficultyBonus = 47.0f;
                    break;
                case SharedTypes::Legacy::Difficulty::Normal:
                    difficultyBonus = 54.0f;
                    break;
                case SharedTypes::Legacy::Difficulty::Hard:
                    difficultyBonus = 61.0f;
                    break;
                default:
                    break;
                }

                if (difficultyBonus == 0.0f) continue;

                float spreadChance = (fireOdds + difficultyBonus) / static_cast<float>(age + 30);
                if (isHumid) spreadChance *= 0.5f;

                if (spreadChance <= 0.0f) continue;

                // 随机检查
                float randVal =
                    static_cast<float>(static_cast<uint>(random.nextInt())) * 2.328306436538696e-10f;
                if (randVal * static_cast<float>(heightMultiplier) > spreadChance) continue;

                // 雨水检查
                bool rainBlocked = false;
                if (weather) {
                    auto checkRainSpread = [&](BlockPos const& checkPos) -> bool {
                        if (weather->isPrecipitatingAt(region, checkPos)) {
                            auto const& b = region.getBiome(checkPos);
                            if (b.getTemperature(region, checkPos) > 0.15000001f) {
                                return true;
                            }
                        }
                        return false;
                    };

                    if (checkRainSpread(testPos)
                        || checkRainSpread(BlockPos(testPos.x - 1, testPos.y, testPos.z))
                        || checkRainSpread(BlockPos(testPos.x + 1, testPos.y, testPos.z))
                        || checkRainSpread(BlockPos(testPos.x, testPos.y, testPos.z - 1))
                        || checkRainSpread(BlockPos(testPos.x, testPos.y, testPos.z + 1))) {
                        rainBlocked = true;
                    }
                }

                if (rainBlocked) continue;

                // 计算新火焰的 age
                int newFireAge = (std::min)(15, age + random.nextInt(5) / 4);

                // 发布火焰蔓延事件
                FireSpreadBeforeEvent spreadBeforeEvent(region, testPos, firePos, newFireAge, age);
                bus.publish(spreadBeforeEvent);

                if (spreadBeforeEvent.isCancelled()) continue;

                // 放置新火焰
                auto fireRef = Block::tryGetFromRegistry(std::string_view("minecraft:fire"));
                if (fireRef) {
                    auto newFireBlock = fireRef->setState<int>(VanillaStates::Age(), newFireAge);
                    if (newFireBlock) {
                        BlockChangeContext ctx{};
                        region.setBlock(testPos, *newFireBlock, 1, nullptr, ctx);
                        tryAddFireToTickingQueue(*this, region, testPos, random);

                        // 发布 after 事件
                        FireSpreadAfterEvent spreadAfterEvent(region, testPos, firePos, newFireAge, age);
                        bus.publish(spreadAfterEvent);
                    }
                }
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
