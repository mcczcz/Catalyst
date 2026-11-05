#include "ClientLoginEvent.h"

#include "catalyst/event/EmitterRegistration.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/Bedrock.h"
#include "mc/certificates/identity/PlayerAuthenticationInfo.h"
#include "mc/common/SubClientId.h"
#include "mc/deps/identity/edu_common/MessToken.h"
#include "mc/deps/raknet/RakPeer.h"
#include "mc/network/ConnectionRequest.h"
#include "mc/network/NetEventCallback.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/network/ServerNetworkHandler.h"
#include "mc/network/connection/DisconnectFailReason.h"
#include "mc/network/packet/LoginPacket.h"
#include "mc/network/packet/SetLocalPlayerAsInitializedPacket.h"
#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/player/Player.h"

#include <unordered_map>

#ifdef LL_PLAT_S

#include <atomic>
#include <cstring>
#include <mutex>

#include "ll/api/memory/Memory.h"
#include "ll/api/memory/Signature.h"
#include "mc/deps/json/Value.h"
#include "mc/deps/nether_net/NetworkSession.h"
#include "mc/external/webrtc/Candidate.h"
#include "mc/external/webrtc/CandidatePairChangeEvent.h"
#include "mc/external/webrtc/CandidatePairInterface.h"
#include "mc/network/NetherNetConnector.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "ll/api/event/EventRefObjSerializer.h"

namespace Catalyst {

void ClientLoginBeforeEvent::serialize(CompoundTag& nbt) const {
    Cancellable::serialize(nbt);
    nbt["source"] = ll::event::serializeRefObj(source());
    nbt["packet"] = ll::event::serializePtrObj(packet().get());
}

void ClientLoginAfterEvent::serialize(CompoundTag& nbt) const {
    ll::event::Event::serialize(nbt);
    nbt["packet"]         = ll::event::serializePtrObj(packet().get());
    nbt["name"]           = name();
    nbt["ip"]             = ip();
    nbt["serverAuthXuid"] = serverAuthXuid();
    nbt["clientAuthXuid"] = clientAuthXuid();
    nbt["uuid"]           = ll::event::serializeRefObj(mUuid);
    nbt["deviceId"]       = deviceId();
    nbt["deviceModel"]    = deviceModel();
    nbt["networkIdHash"]  = (int64_t)mNetworkIdHash;
}


// Thread-local pointer to current NetworkIdentifier during event dispatch
static thread_local NetworkIdentifier const* sCurrentNetworkId = nullptr;

// Directly read IP from RakPeer's internal data, bypassing GetSystemAddressFromGuid's lock
static std::string getIPFromRakPeerUnsafe(RakNet::RakNetGUID const& guid) {
    auto peer = ll::service::getRakPeer();
    if (!peer) return {};

    // Iterate activeSystemList to find the matching GUID
    auto** activeList = peer->activeSystemList;
    uint   listSize   = peer->activeSystemListSize;

    for (uint i = 0; i < listSize; ++i) {
        auto* remote = activeList[i];
        if (!remote || !remote->isActive) continue;

        RakNet::RakNetGUID const& remoteGuid = remote->guid;
        if (remoteGuid.g == guid.g) {
            RakNet::SystemAddress const& addr = remote->systemAddress;
            return addr.GetIPString();
        }
    }
    return {};
}

namespace {

// Native NetherNet layouts and the signature below were checked in the stripped
// 26.51.9 server. See docs/client-login-nethernet.md before updating these offsets.
constexpr size_t observerOffset       = 88;
constexpr size_t peerObserverOffset   = 16;
constexpr size_t dataChannelOffset    = 96;
constexpr size_t sessionIdOffset      = 128;
constexpr size_t selectedPairSlot     = 16;
using SelectedPairCallback = void (*)(void*, webrtc::CandidatePairChangeEvent const&);

template <class T>
T readNativeField(void const* object, size_t offset) {
    T value;
    std::memcpy(&value, static_cast<std::byte const*>(object) + offset, sizeof(T));
    return value;
}

struct NetherNetPeerAddress {
    NetherNet::NetworkID remoteId;
    uint64_t            sessionId;
    uint64_t            generation;
    std::string         ip;
};

std::mutex                                           peerAddressMutex;
std::unordered_map<void*, NetherNetPeerAddress>        peerAddresses;
std::unordered_map<void**, SelectedPairCallback>      observerCallbacks;
uint64_t                                             nextPeerGeneration{};

void onSelectedCandidatePairChanged(void* observer, webrtc::CandidatePairChangeEvent const& event) {
    SelectedPairCallback original;
    {
        std::lock_guard lock(peerAddressMutex);
        original = observerCallbacks.at(*static_cast<void***>(observer));
    }

    // The event owns a CandidatePairInterface at +32. Use its virtual accessor,
    // rather than the SDK's unavailable CandidatePair method thunks.
    auto const& pair = *reinterpret_cast<webrtc::CandidatePairInterface const*>(&event.mUnkc4ddb0);
    auto const& address = pair.remote_candidate().address_;
    if (!address->IsNil() && !address->IsAnyIP()) {
        auto ip = address->ip_->ToString();
        std::lock_guard lock(peerAddressMutex);
        if (auto it = peerAddresses.find(observer); it != peerAddresses.end()) {
            it->second.ip = std::move(ip);
        }
    }
    original(observer, event);
}

void trackNetherNetSession(void* session, NetherNet::NetworkID const& remoteId) {
    auto* nativeObserver = readNativeField<std::byte*>(session, observerOffset);
    auto* dataChannel    = readNativeField<void*>(session, dataChannelOffset);
    if (!nativeObserver || !dataChannel) return;

    // The generated RefCountedObject base is incomplete: static_cast from the
    // generated NetherNet observer would produce the wrong secondary base.
    void* observer = nativeObserver + peerObserverOffset;
    auto** table   = *static_cast<void***>(observer);
    auto sessionId = readNativeField<uint64_t>(dataChannel, sessionIdOffset);
    std::lock_guard lock(peerAddressMutex);
    peerAddresses.insert_or_assign(
        observer,
        NetherNetPeerAddress{remoteId, sessionId, ++nextPeerGeneration, {}}
    );
    if (table[selectedPairSlot] != reinterpret_cast<void*>(&onSelectedCandidatePairChanged)) {
        observerCallbacks.insert_or_assign(table, reinterpret_cast<SelectedPairCallback>(table[selectedPairSlot]));
        // This slot normally points at a folded no-op shared by many callbacks.
        // Patching only this table entry avoids detouring those other methods.
        ll::memory::modify(table[selectedPairSlot], [](void*& slot) {
            std::atomic_ref(slot).store(reinterpret_cast<void*>(&onSelectedCandidatePairChanged));
        });
    }
}

std::string getPeerIP(NetworkIdentifier const& source) {
    switch (source.mType) {
    case NetworkIdentifier::Type::RakNet:
        return getIPFromRakPeerUnsafe(source.mGuid);
    case NetworkIdentifier::Type::Address:
    case NetworkIdentifier::Type::Address6:
        return source.getAddress();
    case NetworkIdentifier::Type::NetherNet: {
        std::lock_guard lock(peerAddressMutex);
        NetherNetPeerAddress const* latest = nullptr;
        for (auto const& [observer, peer] : peerAddresses) {
            if (peer.remoteId == source.mNetherNetId && (!latest || peer.generation > latest->generation)) {
                latest = &peer;
            }
        }
        return latest ? latest->ip : std::string{};
    }
    default:
        return {};
    }
}

// Destroy this AFTER HookRegistrar has removed the producers and resumed the
// threads: taking our mutex while a thread holding it is suspended can deadlock.
struct NetherNetAddressCleanup {
    ~NetherNetAddressCleanup() {
        std::lock_guard lock(peerAddressMutex);
        for (auto const& [table, original] : observerCallbacks) {
            if (table[selectedPairSlot] == reinterpret_cast<void*>(&onSelectedCandidatePairChanged)) {
                ll::memory::modify(table[selectedPairSlot], [&](void*& slot) {
                    std::atomic_ref(slot).store(reinterpret_cast<void*>(original));
                });
            }
        }
        // A callback already in flight may still need its original function.
        // The tables belong to the executable, so retain these few pointers.
        peerAddresses.clear();
    }
};

using namespace ll::memory_literals;

// NetworkSession::InitializeIncoming: the two native by-value aggregates are
// passed indirectly on Win64. Forward their pointers without copying ownership.
LL_STATIC_HOOK(
    NetherNetIncomingAddressHook,
    ll::memory::HookPriority::Normal,
    "55 41 57 41 56 56 57 53 48 81 EC 48 02 00 00 48 8D AC 24 80 00 00 00 48 C7 85 C0 01 00 00 FE FF FF FF 4C 89 C6 48 89 CF 4C 8B 85 20 02 00 00 48 B8 00 00 00 00 01 00 00 00 48 89 41 30 0F 10 02 0F 11 41 40"_sig,
    void,
    void*                       session,
    NetherNet::NetworkID const& remoteId,
    void const*                 offer,
    uint32_t                    preference,
    void*                       identityGenerator
) {
    // Install before SetRemoteDescription starts ICE; the selected pair is
    // delivered on the signaling thread before the login packet is received.
    trackNetherNetSession(session, remoteId);
    origin(session, remoteId, offer, preference, identityGenerator);
}

LL_TYPE_INSTANCE_HOOK(
    NetherNetCloseAddressHook,
    ll::memory::HookPriority::Normal,
    NetherNetConnector,
    &NetherNetConnector::$OnSessionClose,
    void,
    NetherNet::NetworkID     remoteId,
    uint64                  sessionId,
    NetherNet::ESessionError error,
    Json::Value             summary
) {
    {
        std::lock_guard lock(peerAddressMutex);
        std::erase_if(peerAddresses, [&](auto const& entry) {
            return entry.second.remoteId == remoteId && entry.second.sessionId == sessionId;
        });
    }
    origin(std::move(remoteId), sessionId, error, std::move(summary));
}

} // namespace

// Before event hook - on $handle(LoginPacket)
LL_TYPE_INSTANCE_HOOK(
    ClientLoginBeforeEventHook,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$handle,
    void,
    NetworkIdentifier const&     source,
    std::shared_ptr<LoginPacket> packet
) {
    auto& bus = ll::event::EventBus::getInstance();

    ClientLoginBeforeEvent beforeEvent(source, packet);
    bus.publish(beforeEvent);
    if (beforeEvent.isCancelled()) {
        return;
    }

    origin(source, packet);
}

// Capture player info and fire AfterEvent during async authorization
LL_TYPE_INSTANCE_HOOK(
    ClientLoginAfterEventHook,
    ll::memory::HookPriority::Normal,
    ServerNetworkHandler,
    &ServerNetworkHandler::$_onClientAsyncAuthorized,
    void,
    NetworkIdentifier const&        source,
    ConnectionRequest const&        request,
    PlayerAuthenticationInfo const& playerInfo,
    std::optional<MessToken> const& hostMessToken
) {
    // Store current NetworkIdentifier for disconnect() to use
    sCurrentNetworkId = &source;

    auto&                 bus = ll::event::EventBus::getInstance();
    ClientLoginAfterEvent afterEvent(
        nullptr,
        std::string(playerInfo.XboxLiveName),
        getPeerIP(source),
        std::string(playerInfo.Xuid),
        std::string(playerInfo.Xuid),
        playerInfo.AuthenticatedUuid,
        request.getDeviceId(),
        request.getDeviceModel(),
        source.getHash()
    );
    bus.publish(afterEvent);

    sCurrentNetworkId = nullptr;

    origin(source, request, playerInfo, hostMessToken);
}

CATALYST_HOOKED_EMITTER(ClientLoginBeforeEvent, ClientLoginBeforeEventHook)
static std::unique_ptr<ll::event::EmitterBase> ClientLoginAfterEventEmitterFactory();
class ClientLoginAfterEventEmitter final
: public ll::event::Emitter<ClientLoginAfterEventEmitterFactory, ClientLoginAfterEvent> {
    NetherNetAddressCleanup cleanup;
    ll::memory::HookRegistrar<ClientLoginAfterEventHook, NetherNetIncomingAddressHook, NetherNetCloseAddressHook> hooks;
};
static std::unique_ptr<ll::event::EmitterBase> ClientLoginAfterEventEmitterFactory() {
    return std::make_unique<ClientLoginAfterEventEmitter>();
}

// ClientLoginAfterEvent::disconnect implementation
void ClientLoginAfterEvent::disconnect(std::string const& message) const {
    auto handler = ll::service::getServerNetworkHandler();
    if (!handler) return;

    // If called during event dispatch, use the thread-local NetworkIdentifier directly
    if (sCurrentNetworkId && sCurrentNetworkId->getHash() == mNetworkIdHash) {
        handler->disconnectClientWithMessage(
            *sCurrentNetworkId,
            SubClientId::PrimaryClient,
            Connection::DisconnectFailReason::Kicked,
            message,
            std::nullopt
        );
        return;
    }

    // Fallback: search in mClients
    using ClientMap = std::unordered_map<NetworkIdentifier, std::unique_ptr<ServerNetworkHandler::Client>>;
    auto& clients   = const_cast<ClientMap&>(static_cast<ClientMap const&>(handler->mClients));

    for (auto& [netId, client] : clients) {
        if (netId.getHash() == mNetworkIdHash) {
            handler->disconnectClientWithMessage(
                netId,
                SubClientId::PrimaryClient,
                Connection::DisconnectFailReason::Kicked,
                message,
                std::nullopt
            );
            break;
        }
    }
}

} // namespace Catalyst

#else

namespace Catalyst {

void ClientLoginAfterEvent::disconnect(std::string const&) const {}

} // namespace Catalyst

#endif
