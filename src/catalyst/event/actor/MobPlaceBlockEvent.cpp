#if defined(__clang__)
// The generated vector storage header requires this otherwise-unused type to be complete.
struct NamedMolangScript {};
#endif

#include "MobPlaceBlockEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include <algorithm>
#include <vector>
#include "mc/util/Random.h"
#include "mc/util/VariantParameterList.h"
#include "mc/util/VariantParameterListConst.h"
#include "mc/world/actor/ActorDefinitionDescriptor.h"
#include "mc/world/actor/ActorFilterGroup.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/goal/PlaceBlockGoal.h"
#include "mc/world/events/gameevents/GameEventRegistry.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/ActorChangeContext.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockDescriptor.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void MobPlaceBlockEvent::serialize(CompoundTag& nbt) const {
    ll::event::entity::MobEvent::serialize(nbt);
    nbt["pos"]   = ListTag{pos().x, pos().y, pos().z};
    nbt["block"] = ll::event::serializeRefObj(block());
}


namespace {

// 26.32 适配：BlockUtils::getRandomPos 已移除，按原语义内联实现（各轴在 IntRange 内随机偏移）
BlockPos getRandomPos(::IRandom& random, BlockPos pos, ::IntRange const& xzRange, ::IntRange const& yRange) {
    BlockPos targetPos(pos);
    int const xzMin = xzRange.rangeMin;
    int const xzMax = xzRange.rangeMax;
    int const yMin  = yRange.rangeMin;
    int const yMax  = yRange.rangeMax;

    if (xzMin < xzMax) {
        targetPos.x += random.nextInt(xzMax - xzMin + 1) + xzMin;
    }
    if (yMin < yMax) {
        targetPos.y += random.nextInt(yMax - yMin + 1) + yMin;
    }
    if (xzMin < xzMax) {
        targetPos.z += random.nextInt(xzMax - xzMin + 1) + xzMin;
    }
    return targetPos;
}

} // namespace

LL_TYPE_INSTANCE_HOOK(
    MobPlaceBlockEventHook,
    ll::memory::HookPriority::Normal,
    PlaceBlockGoal,
    &PlaceBlockGoal::$tick,
    void
) {
    auto& mob    = this->mMob;
    auto& random = mob.getRandom();

    // 26.32 适配：Definition 嵌套结构已拍平到 PlaceBlockGoal 自身成员
    BlockPos targetPos = getRandomPos(random, BlockPos(mob.getPosition()), this->mXZRange.get(), this->mYRange.get());

    auto& blockSource = mob.getDimensionBlockSource();
    auto& targetBlock = blockSource.getBlock(targetPos);

    // 检查目标位置是否为空气
    if (!targetBlock.isAir()) {
        return;
    }

    // 检查下方方块
    BlockPos belowPos = targetPos;
    belowPos.y        -= 1;
    auto& belowBlock  = blockSource.getBlock(belowPos);

    // 26.32 适配：Block::isSolidBlockingBlock 已移除，改用 _isSolid
    if (belowBlock.isAir() || !belowBlock._isSolid()) {
        return;
    }

    auto& bus = ll::event::EventBus::getInstance();

    // 区分两种放置模式
    // 26.32 适配：Actor::initParams(VariantParameterList&) 已移除，改为拷贝 Actor 自带的 mInitParams
    VariantParameterList triggerParams = mob.mInitParams;
    triggerParams.mBlock               = &targetPos;

    auto const& randomBlocks = this->mRandomlyPlaceableBlocks.get();

    if (randomBlocks.empty()) {
        // 模式A: 使用携带的方块（原 _tryPlaceCarriedBlock 已被内联，按语义重实现）
        auto const& carried = mob.getCarriedItem();
        auto const* toPlace = carried.mBlock;
        if (!toPlace || toPlace->isAir()) {
            return;
        }

        // 26.32：携带方块需在 mPlaceableCarriedBlocks 白名单内（非空时）
        auto const& placeableCarried = this->mPlaceableCarriedBlocks.get();
        if (!placeableCarried.empty()) {
            bool const allowed = std::any_of(
                placeableCarried.begin(),
                placeableCarried.end(),
                [toPlace](BlockDescriptor const& desc) { return desc.matches(*toPlace); }
            );
            if (!allowed) {
                return;
            }
        }

        // 发布 BeforeEvent
        MobPlaceBlockBeforeEvent beforeEvent(mob, targetPos, *toPlace);
        bus.publish(beforeEvent);
        if (beforeEvent.isCancelled()) {
            return;
        }

        BlockChangeContext changeContext{};
        changeContext.mContextSource = ActorChangeContext{&mob};
        if (!blockSource.setBlock(targetPos, *toPlace, 3, nullptr, changeContext)) {
            return;
        }

        blockSource.postGameEvent(&mob, GameEventRegistry::blockPlace(), targetPos, toPlace);

        ActorDefinitionDescriptor::executeTrigger(mob, this->mOnPlace.get(), triggerParams);

        // 发布 AfterEvent
        auto& afterBlock = blockSource.getBlock(targetPos);
        if (!afterBlock.isAir()) {
            MobPlaceBlockAfterEvent afterEvent(mob, targetPos, afterBlock);
            bus.publish(afterEvent);
        }
    } else {
        // 模式B: 加权随机放置（原 _tryGetRandomPlaceBlock 已被内联，按语义重实现）
        VariantParameterListConst blockPickParams{};
        blockPickParams.mSelf   = triggerParams.mSelf;
        blockPickParams.mOther  = triggerParams.mOther;
        blockPickParams.mPlayer = triggerParams.mPlayer;
        blockPickParams.mTarget = triggerParams.mTarget;
        blockPickParams.mParent = triggerParams.mParent;
        blockPickParams.mBaby   = triggerParams.mBaby;
        blockPickParams.mBlock  = triggerParams.mBlock;
        blockPickParams.mDamager  = triggerParams.mDamager;
        blockPickParams.mHolder   = triggerParams.mHolder;

        // 先按过滤器收集候选与总权重
        std::vector<PlaceBlockGoal::WeightedBlockDescriptor const*> candidates;
        int totalWeight = 0;
        for (auto const& entry : randomBlocks) {
            if (entry.mFilter.get().evaluateActor(mob, blockPickParams)) {
                candidates.push_back(&entry);
                totalWeight += entry.mWeight;
            }
        }

        Block const* randomBlock = nullptr;
        if (totalWeight > 0) {
            int roll = random.nextInt(totalWeight);
            for (auto const* entry : candidates) {
                roll -= entry->mWeight;
                if (roll < 0) {
                    randomBlock = &entry->mBlock.get().getBlockOrUnknownBlock();
                    break;
                }
            }
            if (!randomBlock && !candidates.empty()) {
                // 兜底（权重全为 0 时 nextInt(0) 不该被调用；此处 totalWeight>0 必有候选）
                randomBlock = &candidates.back()->mBlock.get().getBlockOrUnknownBlock();
            }
        }

        if (randomBlock) {
            // 发布 BeforeEvent
            MobPlaceBlockBeforeEvent beforeEvent(mob, targetPos, *randomBlock);
            bus.publish(beforeEvent);
            if (beforeEvent.isCancelled()) {
                return;
            }

            // 调用放置
            BlockChangeContext changeContext{};
            changeContext.mContextSource = ActorChangeContext{&mob};
            if (!blockSource.setBlock(targetPos, *randomBlock, 3, nullptr, changeContext)) {
                return;
            }

            blockSource.postGameEvent(&mob, GameEventRegistry::blockPlace(), targetPos, randomBlock);

            ActorDefinitionDescriptor::executeTrigger(mob, this->mOnPlace.get(), triggerParams);

            // 发布 AfterEvent
            auto& afterBlock = blockSource.getBlock(targetPos);
            if (!afterBlock.isAir()) {
                MobPlaceBlockAfterEvent afterEvent(mob, targetPos, afterBlock);
                bus.publish(afterEvent);
            }
        }
    }
}

CATALYST_HOOKED_EVENT_PAIR(
    MobPlaceBlockBeforeEvent,
    MobPlaceBlockAfterEvent,
    MobPlaceBlockEventHook
)

} // namespace Catalyst
