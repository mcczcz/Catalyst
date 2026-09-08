#include "PlayerCloseContainerEvent.h"

#include "catalyst/event/server/SendPacketEvent.h"
#include "catalyst/mod/Gloabl.h"
#include "ll/api/event/Emitter.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/Listener.h"
#include "mc/network/packet/ContainerClosePacket.h"
#include "mc/world/actor/player/Player.h"


#include "mc/deps/nbt/CompoundTag.h"

namespace Catalyst {

void PlayerCloseContainerEvent::serialize(CompoundTag& nbt) const {
    ll::event::ServerPlayerEvent::serialize(nbt);
    nbt["containerId"]          = (int)containerId();
    nbt["containerType"]        = magic_enum::enum_name(containerType());
    nbt["serverInitiatedClose"] = serverInitiatedClose();
}


static std::unique_ptr<ll::event::EmitterBase> beforeEmitterFactory();
class PlayerCloseContainerBeforeEventEmitter
: public ll::event::Emitter<beforeEmitterFactory, PlayerCloseContainerBeforeEvent> {
    ll::event::ListenerPtr listener;

public:
    PlayerCloseContainerBeforeEventEmitter() {
        listener = ll::event::EventBus::getInstance().emplaceListener<SendPacketBeforeEvent>(
            [](SendPacketBeforeEvent& ev) {
                // 检查是否是ContainerClosePacket
                if (ev.packet().getId() != MinecraftPacketIds::ContainerClose) {
                    return;
                }
                logger.debug("捕获到ContainerClosePacket发送事件");
                // 获取玩家
                auto player = ev.player();
                if (!player) {
                    return;
                }

                // 转换为ContainerClosePacket
                auto& containerPacket = static_cast<ContainerClosePacket const&>(ev.packet());

                // 发布PlayerCloseContainerBeforeEvent
                auto&                           bus = ll::event::EventBus::getInstance();
                PlayerCloseContainerBeforeEvent beforeEvent(
                    *player,
                    containerPacket.mContainerId,
                    containerPacket.mContainerType,
                    containerPacket.mServerInitiatedClose
                );
                bus.publish(beforeEvent);

                // 如果事件被取消，则取消发包
                if (beforeEvent.isCancelled()) {
                    ev.cancel();
                }
            },
            ll::event::EventPriority::Normal
        );
    }

    ~PlayerCloseContainerBeforeEventEmitter() override {
        if (listener) {
            ll::event::EventBus::getInstance().removeListener(listener);
        }
    }
};
static std::unique_ptr<ll::event::EmitterBase> beforeEmitterFactory() {
    return std::make_unique<PlayerCloseContainerBeforeEventEmitter>();
}

static std::unique_ptr<ll::event::EmitterBase> afterEmitterFactory();
class PlayerCloseContainerAfterEventEmitter
: public ll::event::Emitter<afterEmitterFactory, PlayerCloseContainerAfterEvent> {
    ll::event::ListenerPtr listener;

public:
    PlayerCloseContainerAfterEventEmitter() {
        listener = ll::event::EventBus::getInstance().emplaceListener<SendPacketAfterEvent>(
            [](SendPacketAfterEvent& ev) {
                // 检查是否是ContainerClosePacket
                if (ev.packet().getId() != MinecraftPacketIds::ContainerClose) {
                    return;
                }

                // 获取玩家
                auto player = ev.player();
                if (!player) {
                    return;
                }

                // 转换为ContainerClosePacket
                auto& containerPacket = static_cast<ContainerClosePacket const&>(ev.packet());

                // 发布PlayerCloseContainerAfterEvent
                auto&                          bus = ll::event::EventBus::getInstance();
                PlayerCloseContainerAfterEvent afterEvent(
                    *player,
                    containerPacket.mContainerId,
                    containerPacket.mContainerType,
                    containerPacket.mServerInitiatedClose
                );
                bus.publish(afterEvent);
            },
            ll::event::EventPriority::Normal
        );
    }

    ~PlayerCloseContainerAfterEventEmitter() override {
        if (listener) {
            ll::event::EventBus::getInstance().removeListener(listener);
        }
    }
};
static std::unique_ptr<ll::event::EmitterBase> afterEmitterFactory() {
    return std::make_unique<PlayerCloseContainerAfterEventEmitter>();
}


// 修复：LL inline 变量模板 emitterRegistration 被 /OPT:REF 丢弃，显式注册（见 EmitterRegistration.h 注释）
namespace {
struct ExplicitEmitterRegistration {
    ExplicitEmitterRegistration() {
        ll::event::EventBus::getInstance().setEventEmitter<PlayerCloseContainerBeforeEvent>(beforeEmitterFactory);
        ll::event::EventBus::getInstance().setEventEmitter<PlayerCloseContainerAfterEvent>(afterEmitterFactory);
    }
};
ExplicitEmitterRegistration const explicitEmitterRegistrationInstance;
} // namespace

} // namespace Catalyst
