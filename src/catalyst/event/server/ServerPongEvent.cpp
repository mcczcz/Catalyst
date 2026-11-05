#include "ServerPongEvent.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "catalyst/event/EmitterRegistration.h"
#include "fmt/format.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/memory/Hook.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/CompoundTagVariant.h"
#include "mc/deps/raknet/DefaultMessageIDTypes.h"
#include "mc/deps/raknet/RNS2_SendParameters.h"
#include "mc/deps/raknet/RNS2_Windows.h"
#include "mc/deps/raknet/SystemAddress.h"

namespace {

using Catalyst::ServerPongAfterEvent;
using Catalyst::ServerPongBeforeEvent;

constexpr std::size_t kUnconnectedPongHeaderSize = 35;

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

} // namespace

namespace Catalyst {

void ServerPongBeforeEvent::serialize(CompoundTag& nbt) const {
    Cancellable::serialize(nbt);
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

CATALYST_HOOKED_EVENT_PAIR(ServerPongBeforeEvent, ServerPongAfterEvent, ServerPongEventHook)

#endif

} // namespace Catalyst
