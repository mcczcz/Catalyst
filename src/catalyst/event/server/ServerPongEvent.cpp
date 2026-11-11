#include "ServerPongEvent.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "catalyst/event/EmitterRegistration.h"
#include "fmt/format.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/cereal/ContextArea.h"
#include "mc/deps/cereal/schema/SchemaRWType.h"
#include "mc/deps/cereal/schema/SchemaWriter.h"
#include "mc/deps/cereal/schema/dynamic/NullType.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/raknet/DefaultMessageIDTypes.h"
#include "mc/deps/raknet/RNS2_SendParameters.h"
#include "mc/deps/raknet/RNS2_Windows.h"
#include "mc/deps/raknet/SystemAddress.h"
#include "mc/external/webrtc/Socket.h"
#include "mc/external/webrtc/SocketAddress.h"
#include "mc/network/NetherNetServerLocator.h"

namespace {

using Catalyst::ServerPongAfterEvent;
using Catalyst::ServerPongBeforeEvent;

constexpr std::size_t kUnconnectedPongHeaderSize = 35;

thread_local std::string const* currentNetherNetPongAddress = nullptr;

class NetherNetPongAddressScope {
    std::string        mAddress;
    std::string const* mPrevious;

public:
    NetherNetPongAddressScope(NetherNetPongAddressScope const&)            = delete;
    NetherNetPongAddressScope& operator=(NetherNetPongAddressScope const&) = delete;

    explicit NetherNetPongAddressScope(std::string address)
    : mAddress(std::move(address)),
      mPrevious(std::exchange(currentNetherNetPongAddress, &mAddress)) {}
    ~NetherNetPongAddressScope() { currentNetherNetPongAddress = mPrevious; }
};

std::string netherNetAddress(webrtc::SocketAddress const& address) {
    if (address.IsNil() || address.IsAnyIP()) return {};
    auto ip = address.ip_->ToString();
    // The dual-stack discovery/listen sockets report IPv4 peers as mapped IPv6.
    if (ip.starts_with("::ffff:") && ip.find('.') != std::string::npos) ip.erase(0, 7);
    return fmt::format("{}|{}", ip, address.port_);
}

struct PongPayloadData {
    std::string              motd;
    int                      protocolVersion;
    std::string              networkVersion;
    int                      playerCount;
    int                      maxPlayerCount;
    std::string              guid;
    std::string              levelName;
    std::string              gameMode;
    std::string              joinable;
    std::uint16_t            localPort;
    std::uint16_t            localPortV6;
    std::string              editorWorld;
    std::vector<std::string> other;
    std::string              ipAndPort;
};

template <typename T>
bool parseInteger(std::string_view value, T& out) {
    T result{};
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (ec != std::errc{} || ptr != value.data() + value.size()) {
        return false;
    }
    out = result;
    return true;
}

bool parseUint16(std::string_view value, std::uint16_t& out) {
    unsigned int result{};
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (ec != std::errc{} || ptr != value.data() + value.size() || result > 0xFFFF) {
        return false;
    }
    out = static_cast<std::uint16_t>(result);
    return true;
}

std::vector<std::string> splitPongPayload(std::string_view payload) {
    std::vector<std::string> parts;
    std::size_t              start = 0;

    while (start < payload.size()) {
        auto end = payload.find(';', start);
        if (end == std::string_view::npos) {
            parts.emplace_back(payload.substr(start));
            break;
        }
        parts.emplace_back(payload.substr(start, end - start));
        start = end + 1;
    }

    return parts;
}

std::string systemAddressToString(RakNet::SystemAddress const& address) {
    std::array<char, 128> buffer{};
    address.ToString(true, buffer.data(), '|');
    return std::string(buffer.data());
}

std::string extractIp(std::string const& ipAndPort) {
    auto pos = ipAndPort.rfind('|');
    if (pos == std::string::npos) {
        return ipAndPort;
    }
    return ipAndPort.substr(0, pos);
}

std::uint16_t extractPort(std::string const& ipAndPort) {
    auto pos = ipAndPort.rfind('|');
    if (pos == std::string::npos || pos + 1 >= ipAndPort.size()) {
        return 0;
    }

    std::uint16_t port{};
    return parseUint16(std::string_view(ipAndPort).substr(pos + 1), port) ? port : 0;
}

// The first pass only reads the cached ServerData through the schema. Publishing
// after it returns also keeps listeners outside the locator's internal mutex.
// The second pass retains the real writer's scalar types and container behaviour.
class NetherNetPongWriter final : public cereal::SchemaWriter {
    using Field = std::variant<bool, int64, uint64, double, std::string>;
    cereal::SchemaWriter&                     mWriter;
    ServerPongBeforeEvent const*              mEvent;
    std::vector<std::string>                  mMemberPath;
    std::map<std::string, Field, std::less<>> mFields;

    std::string_view member() const { return mMemberPath.empty() ? std::string_view{} : mMemberPath.back(); }

    template <typename T>
    T field(std::string_view name, T fallback = {}) const {
        auto it = mFields.find(name);
        if (it == mFields.end()) {
            return fallback;
        }
        return std::visit(
            [&](auto const& value) -> T {
                using V = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, V>) {
                    return value;
                } else if constexpr (std::is_arithmetic_v<T> && std::is_arithmetic_v<V>) {
                    return static_cast<T>(value);
                }
                return fallback;
            },
            it->second
        );
    }

    template <typename T>
    std::optional<T> optionalField(std::string_view name) const {
        return mFields.contains(name) ? std::optional<T>(field<T>(name)) : std::nullopt;
    }

    static std::string gameModeName(int value) {
        switch (value) {
        case 0:
            return "Survival";
        case 1:
            return "Creative";
        case 2:
            return "Adventure";
        case 6:
            return "Spectator";
        default:
            return std::to_string(value);
        }
    }

    int gameModeValue(int fallback) const {
        auto const& mode = mEvent->gameMode();
        if (mode == "Survival") return 0;
        if (mode == "Creative") return 1;
        if (mode == "Adventure") return 2;
        if (mode == "Spectator") return 6;
        int value{};
        return parseInteger(mode, value) ? value : fallback;
    }

    template <typename T>
    bool writeNumber(T value) {
        auto key = member();
        if (!mEvent) {
            if constexpr (std::is_same_v<T, bool>) {
                mFields.insert_or_assign(std::string(key), value);
            } else if constexpr (std::is_floating_point_v<T>) {
                mFields.insert_or_assign(std::string(key), static_cast<double>(value));
            } else if constexpr (std::is_signed_v<T>) {
                mFields.insert_or_assign(std::string(key), static_cast<int64>(value));
            } else {
                mFields.insert_or_assign(std::string(key), static_cast<uint64>(value));
            }
            return true;
        }
        if constexpr (!std::is_same_v<T, bool> && !std::is_floating_point_v<T>) {
            if (key == "protocol") value = static_cast<T>(mEvent->protocolVersion());
            else if (key == "players") value = static_cast<T>(mEvent->playerCount());
            else if (key == "maxPlayers") value = static_cast<T>(mEvent->maxPlayerCount());
            else if (key == "gameType") value = static_cast<T>(gameModeValue(static_cast<int>(value)));
        }
        if (auto const& data = mEvent->netherNetData()) {
            if (key == "dataVersion") value = data->dataVersion ? static_cast<T>(*data->dataVersion) : value;
            else if (key == "editor") value = data->editorWorld ? static_cast<T>(*data->editorWorld) : value;
            else if (key == "hardcore") value = data->hardcore ? static_cast<T>(*data->hardcore) : value;
            else if (key == "onlineAuth") value = data->onlineAuth ? static_cast<T>(*data->onlineAuth) : value;
            else if (key == "selfSignedAuth")
                value = data->selfSignedAuth ? static_cast<T>(*data->selfSignedAuth) : value;
            else if (key == "connection") value = data->connection ? static_cast<T>(*data->connection) : value;
        }
        return mWriter.write(value);
    }

public:
    explicit NetherNetPongWriter(cereal::SchemaWriter& writer, ServerPongBeforeEvent const* event = nullptr)
    : mWriter(writer),
      mEvent(event) {
        mSerializationTraitsOverride = writer.mSerializationTraitsOverride;
        mSerializationTraits         = writer.mSerializationTraits;
    }

    bool hasServerName() const { return mFields.contains("name"); }

    ServerPongBeforeEvent event() const {
        Catalyst::ServerPongNetherNetData data;
        data.dataVersion    = optionalField<std::uint8_t>("dataVersion");
        data.editorWorld    = optionalField<bool>("editor");
        data.hardcore       = optionalField<bool>("hardcore");
        data.onlineAuth     = optionalField<bool>("onlineAuth");
        data.selfSignedAuth = optionalField<bool>("selfSignedAuth");
        data.nonce          = optionalField<std::string>("nonce");
        data.connection     = optionalField<std::uint32_t>("connection");
        return ServerPongBeforeEvent(
            field<std::string>("name"),
            field<int>("protocol"),
            field<std::string>("version"),
            field<int>("players"),
            field<int>("maxPlayers"),
            std::string{},
            field<std::string>("level"),
            gameModeName(field<int>("gameType")),
            0,
            0,
            std::vector<std::string>{},
            currentNetherNetPongAddress ? *currentNetherNetPongAddress : std::string{},
            std::move(data)
        );
    }

    bool write(cereal::NullType value) override { return !mEvent || mWriter.write(value); }
    bool write(bool value) override { return writeNumber(value); }
    bool write(schar value) override { return writeNumber(value); }
    bool write(uchar value) override { return writeNumber(value); }
    bool write(short value) override { return writeNumber(value); }
    bool write(ushort value) override { return writeNumber(value); }
    bool write(int value) override { return writeNumber(value); }
    bool write(uint value) override { return writeNumber(value); }
    bool write(int64 value) override { return writeNumber(value); }
    bool write(uint64 value) override { return writeNumber(value); }
    bool write(float value) override { return writeNumber(value); }
    bool write(double value) override { return writeNumber(value); }

    bool write(std::string_view const value) override {
        auto key = member();
        if (!mEvent) {
            mFields.insert_or_assign(std::string(key), std::string(value));
            return true;
        }
        if (key == "name") return mWriter.write(mEvent->motd());
        if (key == "version") return mWriter.write(mEvent->networkVersion());
        if (key == "level") return mWriter.write(mEvent->levelName());
        if (key == "nonce" && mEvent->netherNetData() && mEvent->netherNetData()->nonce) {
            return mWriter.write(*mEvent->netherNetData()->nonce);
        }
        return mWriter.write(value);
    }

    bool write(gsl::span<uchar const> view) override { return !mEvent || mWriter.write(view); }
    void writeAdditionalData(bool value) override {
        if (mEvent) mWriter.writeAdditionalData(value);
    }
    void writeAdditionalData(uint value) override {
        if (mEvent) mWriter.writeAdditionalData(value);
    }
    void writeAdditionalData(std::string_view const value) override {
        if (mEvent) mWriter.writeAdditionalData(value);
    }

    bool pushMember(std::string_view const value) override {
        if (mEvent && !mWriter.pushMember(value)) return false;
        mMemberPath.emplace_back(value);
        return true;
    }
    void popMember() override {
        if (mEvent) mWriter.popMember();
        if (!mMemberPath.empty()) mMemberPath.pop_back();
    }
    cereal::SchemaRWType openObject() override { return mEvent ? mWriter.openObject() : cereal::SchemaRWType::Object; }
    cereal::SchemaRWType openArray(bool isDynamicExtent, uint64 length) override {
        return mEvent ? mWriter.openArray(isDynamicExtent, length) : cereal::SchemaRWType::Array;
    }
    void close() override {
        if (mEvent) mWriter.close();
    }
};

std::optional<PongPayloadData> parsePongPayload(std::string_view payload, std::string ipAndPort) {
    auto parts = splitPongPayload(payload);
    if (parts.size() < 13) {
        return std::nullopt;
    }

    PongPayloadData data{};
    data.motd            = std::move(parts[1]);
    data.protocolVersion = 0;
    data.networkVersion  = std::move(parts[3]);
    data.playerCount     = 0;
    data.maxPlayerCount  = 0;
    data.guid            = std::move(parts[6]);
    data.levelName       = std::move(parts[7]);
    data.gameMode        = std::move(parts[8]);
    data.joinable        = std::move(parts[9]);
    data.localPort       = 0;
    data.localPortV6     = 0;
    data.editorWorld     = std::move(parts[12]);
    data.ipAndPort       = std::move(ipAndPort);

    if (parts[0] != "MCPE" || !parseInteger(parts[2], data.protocolVersion) || !parseInteger(parts[4], data.playerCount)
        || !parseInteger(parts[5], data.maxPlayerCount) || !parseUint16(parts[10], data.localPort)
        || !parseUint16(parts[11], data.localPortV6)) {
        return std::nullopt;
    }

    for (std::size_t i = 13; i < parts.size(); ++i) {
        data.other.emplace_back(std::move(parts[i]));
    }

    return data;
}

std::string buildPayload(ServerPongBeforeEvent const& event, PongPayloadData const& original) {
    // These announcement flags are not exposed by the event; retain their original values.
    std::string payload = fmt::format(
        "MCPE;{};{};{};{};{};{};{};{};{};{};{};{};",
        event.motd(),
        event.protocolVersion(),
        event.networkVersion(),
        event.playerCount(),
        event.maxPlayerCount(),
        event.guid(),
        event.levelName(),
        event.gameMode(),
        original.joinable,
        event.localPort(),
        event.localPortV6(),
        original.editorWorld
    );

    for (auto const& extra : event.other()) {
        payload += extra;
        payload.push_back(';');
    }

    return payload;
}

std::string buildPacket(std::string_view prefix, std::string_view payload) {
    std::string packet;
    packet.reserve(kUnconnectedPongHeaderSize + payload.size());
    packet.append(prefix);

    auto payloadSize = static_cast<std::uint16_t>(payload.size());
    packet.push_back(static_cast<char>((payloadSize >> 8) & 0xFF));
    packet.push_back(static_cast<char>(payloadSize & 0xFF));
    packet.append(payload);
    return packet;
}

void serializePongFields(
    CompoundTag&                    nbt,
    std::string const&              motd,
    int                             protocolVersion,
    std::string const&              networkVersion,
    int                             playerCount,
    int                             maxPlayerCount,
    std::string const&              guid,
    std::string const&              levelName,
    std::string const&              gameMode,
    std::uint16_t                   localPort,
    std::uint16_t                   localPortV6,
    std::vector<std::string> const& other,
    std::string const&              ipAndPort
) {
    nbt["motd"]            = motd;
    nbt["protocolVersion"] = protocolVersion;
    nbt["networkVersion"]  = networkVersion;
    nbt["playerCount"]     = playerCount;
    nbt["maxPlayerCount"]  = maxPlayerCount;
    nbt["guid"]            = guid;
    nbt["levelName"]       = levelName;
    nbt["gameMode"]        = gameMode;
    nbt["localPort"]       = static_cast<int>(localPort);
    nbt["localPortV6"]     = static_cast<int>(localPortV6);
    nbt["ip"]              = extractIp(ipAndPort);
    nbt["port"]            = static_cast<int>(extractPort(ipAndPort));
    nbt["ipAndPort"]       = ipAndPort;

    std::vector<CompoundTagVariant> others;
    others.reserve(other.size());
    for (auto const& entry : other) {
        others.emplace_back(entry);
    }
    nbt["others"] = ListTag(std::move(others));
}

void serializeNetherNetData(CompoundTag& nbt, std::optional<Catalyst::ServerPongNetherNetData> const& data) {
    if (!data) return;
    CompoundTag fields;
    if (data->dataVersion) fields["dataVersion"] = static_cast<int>(*data->dataVersion);
    if (data->editorWorld) fields["editor"] = *data->editorWorld;
    if (data->hardcore) fields["hardcore"] = *data->hardcore;
    if (data->onlineAuth) fields["onlineAuth"] = *data->onlineAuth;
    if (data->selfSignedAuth) fields["selfSignedAuth"] = *data->selfSignedAuth;
    if (data->nonce) fields["nonce"] = *data->nonce;
    if (data->connection) fields["connection"] = static_cast<int64>(*data->connection);
    nbt["netherNet"] = std::move(fields);
}

} // namespace

namespace Catalyst {

void ServerPongBeforeEvent::serialize(CompoundTag& nbt) const {
    Cancellable::serialize(nbt);
    serializeNetherNetData(nbt, netherNetData());
    serializePongFields(
        nbt,
        motd(),
        protocolVersion(),
        networkVersion(),
        playerCount(),
        maxPlayerCount(),
        guid(),
        levelName(),
        gameMode(),
        localPort(),
        localPortV6(),
        other(),
        ipAndPort()
    );
}

void ServerPongAfterEvent::serialize(CompoundTag& nbt) const {
    ll::event::Event::serialize(nbt);
    serializeNetherNetData(nbt, netherNetData());
    serializePongFields(
        nbt,
        motd(),
        protocolVersion(),
        networkVersion(),
        playerCount(),
        maxPlayerCount(),
        guid(),
        levelName(),
        gameMode(),
        localPort(),
        localPortV6(),
        other(),
        ipAndPort()
    );
}

std::string ServerPongBeforeEvent::ip() const { return extractIp(mIpAndPort); }

std::uint16_t ServerPongBeforeEvent::port() const { return extractPort(mIpAndPort); }

std::string ServerPongAfterEvent::ip() const { return extractIp(mIpAndPort); }

std::uint16_t ServerPongAfterEvent::port() const { return extractPort(mIpAndPort); }

#ifdef LL_PLAT_S

using namespace ll::memory_literals;

// Decrypted LAN receive callback (IDA 0x141119050). The native packet view
// contains a SocketAddress pointer at +32; the payload starts with length/type.
LL_STATIC_HOOK(
    NetherNetLanPongAddressHook,
    ll::memory::HookPriority::Normal,
    "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ?? ?? ?? ?? 48 8D AC 24 ?? ?? ?? ?? 48 C7 85 ?? ?? ?? ?? FE FF FF FF 49 8B 58 08 48 83 FB 04 72 ?? 4D 8B 20"_sig,
    void,
    void*       self,
    void*       unused,
    void const* packet
) {
    auto const*                  bytes = static_cast<std::byte const*>(packet);
    unsigned char const*         payload{};
    std::size_t                  length{};
    webrtc::SocketAddress const* address{};
    std::memcpy(&payload, bytes, sizeof(payload));
    std::memcpy(&length, bytes + 8, sizeof(length));
    std::memcpy(&address, bytes + 32, sizeof(address));
    if (payload && length >= 20 && payload[2] == 0 && payload[3] == 0 && address) {
        NetherNetPongAddressScope scope(netherNetAddress(*address));
        origin(self, unused, packet);
    } else {
        origin(self, unused, packet);
    }
}

// HttpConnection::_onReadEvent (IDA 0x141120770). Native mSocket is at +168.
// /v1/join invokes mGetServerInfo synchronously before scheduling the response.
LL_STATIC_HOOK(
    NetherNetHttpPongAddressHook,
    ll::memory::HookPriority::Normal,
    "55 41 57 41 56 41 55 41 54 56 57 53 B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 29 C4 48 8D AC 24 ?? ?? ?? ?? 0F 29 BD ?? ?? ?? ?? 0F 29 B5 ?? ?? ?? ?? 48 C7 85 ?? ?? ?? ?? FE FF FF FF 48 89 CE 48 8D 7D"_sig,
    void,
    void* self
) {
    webrtc::Socket* socket{};
    std::memcpy(&socket, static_cast<std::byte const*>(self) + 168, sizeof(socket));
    std::string address;
    if (socket) {
        auto remote = socket->GetRemoteAddress();
        address     = netherNetAddress(remote);
    }
    NetherNetPongAddressScope scope(std::move(address));
    origin(self);
}

// NetherNetServerLocator::_onDiscoveryRequest, verified on E:\ll2660 (1.26.60.6).
// The prologue,
// disabled-state check, and SchemaWriter/ContextArea argument use were verified
// in IDA; the wildcarded signature is unique in that server binary.
LL_STATIC_HOOK(
    NetherNetServerPongEventHook,
    ll::memory::HookPriority::Normal,
    "55 41 57 41 56 41 55 41 54 56 57 53 48 81 EC ?? ?? ?? ?? 48 8D AC 24 ?? ?? ?? ?? 48 C7 85 ?? ?? ?? ?? FE FF FF FF 83 79 08 01"_sig,
    bool,
    NetherNetServerLocator* self,
    cereal::SchemaWriter&   writer,
    cereal::ContextArea     area
) {
    std::optional<ServerPongBeforeEvent> beforeEvent;
    try {
        NetherNetPongWriter collector(writer);
        if (!origin(self, collector, area)) {
            return false;
        }
        if (collector.hasServerName()) {
            beforeEvent.emplace(collector.event());
            ll::event::EventBus::getInstance().publish(*beforeEvent);
        }
    } catch (...) {
        // The collector never touches the real writer, so the original response is safe.
        return origin(self, writer, area);
    }
    if (!beforeEvent) {
        return origin(self, writer, area);
    }
    if (beforeEvent->isCancelled()) {
        return false;
    }

    NetherNetPongWriter eventWriter(writer, &*beforeEvent);
    bool                sent = origin(self, eventWriter, area);
    if (sent) {
        try {
            ServerPongAfterEvent afterEvent(
                beforeEvent->motd(),
                beforeEvent->protocolVersion(),
                beforeEvent->networkVersion(),
                beforeEvent->playerCount(),
                beforeEvent->maxPlayerCount(),
                beforeEvent->guid(),
                beforeEvent->levelName(),
                beforeEvent->gameMode(),
                beforeEvent->localPort(),
                beforeEvent->localPortV6(),
                beforeEvent->other(),
                beforeEvent->ipAndPort(),
                beforeEvent->netherNetData()
            );
            ll::event::EventBus::getInstance().publish(afterEvent);
        } catch (...) {
            // The discovery response has already been written.
        }
    }
    return sent;
}

// macOS RNS2_Linux::Send uses the same RakNetSocket2::Send interface.
// On Windows the concrete implementation is RNS2_Windows::$Send.
LL_TYPE_INSTANCE_HOOK(
    ServerPongEventHook,
    ll::memory::HookPriority::Normal,
    RakNet::RNS2_Windows,
    &RakNet::RNS2_Windows::$Send,
    int,
    ::RakNet::RNS2_SendParameters* sendParameters,
    char const*                    file,
    uint                           line
) {
    std::optional<ServerPongBeforeEvent> beforeEvent;
    std::string                          rebuiltPacket;

    auto preparePacket = [&]() {
        if (!sendParameters || !sendParameters->data
            || sendParameters->length < static_cast<int>(kUnconnectedPongHeaderSize)
            || static_cast<unsigned char>(sendParameters->data[0])
                   != static_cast<unsigned char>(DefaultMessageIDTypes::UnconnectedPong)) {
            return false;
        }

        auto const* data        = reinterpret_cast<unsigned char const*>(sendParameters->data);
        auto        payloadSize = static_cast<std::size_t>((static_cast<unsigned int>(data[33]) << 8) | data[34]);
        if (payloadSize != static_cast<std::size_t>(sendParameters->length) - kUnconnectedPongHeaderSize) {
            return false;
        }

        std::string_view payloadView{sendParameters->data + kUnconnectedPongHeaderSize, payloadSize};
        auto             parsed = parsePongPayload(payloadView, systemAddressToString(sendParameters->systemAddress));
        if (!parsed.has_value()) {
            return false;
        }

        auto& bus = ll::event::EventBus::getInstance();

        beforeEvent.emplace(
            std::move(parsed->motd),
            parsed->protocolVersion,
            std::move(parsed->networkVersion),
            parsed->playerCount,
            parsed->maxPlayerCount,
            std::move(parsed->guid),
            std::move(parsed->levelName),
            std::move(parsed->gameMode),
            parsed->localPort,
            parsed->localPortV6,
            std::move(parsed->other),
            std::move(parsed->ipAndPort)
        );
        bus.publish(*beforeEvent);
        if (beforeEvent->isCancelled()) {
            return true;
        }

        auto rebuiltPayload = buildPayload(*beforeEvent, *parsed);
        if (rebuiltPayload.size() > 0xFFFF) {
            return false;
        }

        rebuiltPacket =
            buildPacket(std::string_view(sendParameters->data, kUnconnectedPongHeaderSize - 2), rebuiltPayload);
        return true;
    };

    bool prepared = false;
    try {
        prepared = preparePacket();
    } catch (...) {
        // Fall back only before attempting a send.
    }
    if (!prepared) {
        return origin(sendParameters, file, line);
    }
    if (beforeEvent->isCancelled()) {
        // Send returns a byte count on success; consume the original datagram without sending it.
        return sendParameters->length;
    }

    auto modifiedParams   = *sendParameters;
    modifiedParams.data   = rebuiltPacket.data();
    modifiedParams.length = static_cast<int>(rebuiltPacket.size());

    int result = origin(&modifiedParams, file, line);
    if (result != modifiedParams.length) {
        return result;
    }

    try {
        ServerPongAfterEvent afterEvent(
            beforeEvent->motd(),
            beforeEvent->protocolVersion(),
            beforeEvent->networkVersion(),
            beforeEvent->playerCount(),
            beforeEvent->maxPlayerCount(),
            beforeEvent->guid(),
            beforeEvent->levelName(),
            beforeEvent->gameMode(),
            beforeEvent->localPort(),
            beforeEvent->localPortV6(),
            beforeEvent->other(),
            beforeEvent->ipAndPort()
        );
        ll::event::EventBus::getInstance().publish(afterEvent);
    } catch (...) {
        // The datagram has already been sent; an After failure must not send it again.
    }
    return result;
}

CATALYST_HOOKED_EVENT_PAIR(
    ServerPongBeforeEvent,
    ServerPongAfterEvent,
    ServerPongEventHook,
    NetherNetServerPongEventHook,
    NetherNetLanPongAddressHook,
    NetherNetHttpPongAddressHook
)

#endif

} // namespace Catalyst
