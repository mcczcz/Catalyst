#include "ReceivePacketEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"

#include "catalyst/mod/Gloabl.h"
#include "mc/deps/core/utility/ReadOnlyBinaryStream.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/GameRefsEntity.h"
#include "mc/deps/game_refs/WeakRef.h"
#include "mc/gameplayhandlers/CoordinatorResult.h"
#include "mc/gameplayhandlers/GameplayHandlerResult.h"
#include "mc/network/IPacketHandlerDispatcher.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/MinecraftPackets.h"
#include "mc/network/NetworkConnection.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/network/NetworkSystem.h"
#include "mc/network/Packet.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/packet/RequestNetworkSettingsPacket.h"
#include "mc/network/packet/TextPacket.h"
#include "mc/scripting/event_handlers/ScriptServerNetworkEventHandler.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/Actor.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/events/IncomingPacketEvent.h"


/*
namespace {
thread_local NetworkConnection* g_currentConnection = nullptr;
}


template <>
struct GameplayHandlerResult<CoordinatorResult> {
    CoordinatorResult value;
    GameplayHandlerResult() : value(CoordinatorResult::Continue) {}
    GameplayHandlerResult(CoordinatorResult v) : value(v) {}
};

#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void ReceivePacketBeforeEvent::serialize(CompoundTag& nbt) const {
    Cancellable::serialize(nbt);
    nbt["networkId"]   = ll::event::serializeRefObj(networkId());
    nbt["packet"]      = ll::event::serializeRefObj(packet());
    nbt["subClientId"] = (int)subClientId();
    nbt["player"]      = ll::event::serializePtrObj(player());
}

void ReceivePacketAfterEvent::serialize(CompoundTag& nbt) const {
    ll::event::Event::serialize(nbt);
    nbt["networkId"]   = ll::event::serializeRefObj(networkId());
    nbt["packet"]      = ll::event::serializeRefObj(packet());
    nbt["subClientId"] = (int)subClientId();
    nbt["player"]      = ll::event::serializePtrObj(player());
}



LL_TYPE_INSTANCE_HOOK(
    ScriptServerNetworkEventHandlerHook,
    ll::memory::HookPriority::Normal,
    ScriptServerNetworkEventHandler,
    &ScriptServerNetworkEventHandler::$handleEvent,
    GameplayHandlerResult<CoordinatorResult>,
    IncomingPacketEvent& packetEvent
) {
    auto re = origin(packetEvent);
    logger.info("HAND:{}", packetEvent.mPacketId);
    if (g_currentConnection && !g_currentConnection->mShouldCloseConnection) {
        auto                 network = ll::service::getNetworkSystem();
        ReadOnlyBinaryStream stream(network->mReceiveBuffer.get(), false);
        auto                 header = stream.getUnsignedVarInt();

        logger.info("ID:{}", g_currentConnection->mId->toString());
        if (!header.has_value()) {
            return GameplayHandlerResult<CoordinatorResult>{CoordinatorResult::Cancel};
        }
        const auto packet_id = static_cast<int>(header.value() & 0x3ff);
        logger.info("Received packet id: {}", packet_id);
        auto payload = stream.mView.substr(stream.mReadPointer);
        auto packet  = MinecraftPackets::createPacket(static_cast<MinecraftPacketIds>(packet_id));
        if (!packet) {
            return GameplayHandlerResult<CoordinatorResult>{CoordinatorResult::Cancel};
        }
        if (packetEvent.mPacketId == MinecraftPacketIds::RequestNetworkSettings) {
            auto p                   = static_cast<RequestNetworkSettingsPacket*>(packet.get());
            p->mClientNetworkVersion = 808;
        }
        auto now                             = std::chrono::steady_clock::now();
        g_currentConnection->mLastPacketTime = now;
        packet->mReceiveTimepoint            = now;
        ReadOnlyBinaryStream read_stream(payload, false);
        auto                 packet_size = read_stream.mView.size() - read_stream.mReadPointer;
        if (auto result = packet->checkSize(packet_size, true)) {
            return GameplayHandlerResult<CoordinatorResult>{CoordinatorResult::Cancel};
        }
        if (auto result = packet->read(read_stream)) {
            return GameplayHandlerResult<CoordinatorResult>{CoordinatorResult::Cancel};
        }
        packet->mHandler->handle(g_currentConnection->mId, ll::service::getServerNetworkHandler(), packet);
        return GameplayHandlerResult<CoordinatorResult>{CoordinatorResult::Cancel};
    }
    return origin(packetEvent);
}
LL_TYPE_INSTANCE_HOOK(
    ScriptServerNetworkEventHandlerHook2,
    ll::memory::HookPriority::Normal,
    NetworkSystem,
    &NetworkSystem::_sortAndPacketizeEvents,
    bool,
    ::NetworkConnection&                    connection,
    ::std::chrono::steady_clock::time_point endTime
) {
    g_currentConnection = &connection;
    auto result         = origin(connection, endTime);
    g_currentConnection = nullptr;
    return result;
}
*/
#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void ReceivePacketEvent::serialize(CompoundTag& nbt) const {
    ll::event::Event::serialize(nbt);
    nbt["networkId"]   = ll::event::serializeRefObj(networkId());
    nbt["packet"]      = ll::event::serializeRefObj(packet());
    nbt["subClientId"] = (int)subClientId();
    nbt["player"]      = ll::event::serializePtrObj(player());
}

LL_TYPE_INSTANCE_HOOK(
    ReceivePacketEventHook,
    ll::memory::HookPriority::Normal,
    NetworkConnection,
    &NetworkConnection::receivePacket,
    ::NetworkPeer::DataStatus,
    ::std::string&                                                    receiveBuffer,
    ::std::shared_ptr<::std::chrono::steady_clock::time_point> const& timepointPtr
) {
    auto network_handler = ll::service::getServerNetworkHandler();
    while (true) {
        auto       network = ll::service::getNetworkSystem();
        const auto status  = origin(receiveBuffer, timepointPtr);
        if (status != NetworkPeer::DataStatus::HasData) {
            return status;
        }
        ReadOnlyBinaryStream stream(receiveBuffer, false);
        const auto           header_result = stream.getUnsignedVarInt();
        if (!header_result.has_value()) {
            continue;
        }
        const auto header        = header_result.value();
        const auto packet_id     = static_cast<MinecraftPacketIds>(header & 0x3ff);
        const auto sub_client_id = static_cast<SubClientId>((header >> 12) & 0x3);
        auto*      p             = network_handler->_getServerPlayer(mId, sub_client_id);
        const auto payload       = stream.mView.substr(stream.mReadPointer);
        auto       packet        = MinecraftPackets::createPacket(packet_id);
        if (!packet) {
            continue;
        }

        auto now                  = std::chrono::steady_clock::now();
        this->mLastPacketTime     = now;
        packet->mReceiveTimepoint = now;
        ReadOnlyBinaryStream read_stream(payload, false);
        auto                 packet_size = read_stream.mView.size() - read_stream.mReadPointer;
        if (auto result = packet->checkSize(packet_size, true); !result.Base::has_value()) {
            Catalyst::logger.error("PacketReceiveEvent: Bad packet size: {}", packet_size);
            continue;
        }
        // 26.32: Packet::readNoHeader removed; replicate it (set sub client id, then virtual read with ctx)
        packet->mSenderSubId = sub_client_id;
        if (auto result = packet->read(read_stream, *network->mReflectionCtx); !result.Base::has_value()) {
            Catalyst::logger.error("PacketReceiveEvent: Bad packet!");
            continue;
        }

        auto&                    bus = ll::event::EventBus::getInstance();
        ReceivePacketBeforeEvent beforeEvent(mId, *packet, sub_client_id, p);
        bus.publish(beforeEvent);
        if (beforeEvent.isCancelled()) {
            continue;
        }

        packet->mHandler->handle(mId, *network_handler, packet);

        ReceivePacketAfterEvent afterEvent(mId, *packet, sub_client_id, p);
        bus.publish(afterEvent);
    }
}

CATALYST_HOOKED_EVENT_PAIR(
    ReceivePacketBeforeEvent,
    ReceivePacketAfterEvent,
    ReceivePacketEventHook
)

} // namespace Catalyst
