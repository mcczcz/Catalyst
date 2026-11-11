#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "catalyst/Macros.h"
#include "ll/api/event/Cancellable.h"
#include "ll/api/event/Event.h"

namespace Catalyst {

// Only present for NetherNet responses. Common fields use the existing event accessors.
struct ServerPongNetherNetData {
    // Empty fields are not serialized by the current transport/context.
    std::optional<std::uint8_t>  dataVersion;
    std::optional<bool>          editorWorld;
    std::optional<bool>          hardcore;
    std::optional<bool>          onlineAuth;
    std::optional<bool>          selfSignedAuth;
    std::optional<std::string>   nonce;
    std::optional<std::uint32_t> connection;
};

class CATALYST_API ServerPongBeforeEvent final : public ll::event::Cancellable<ll::event::Event> {
    std::string              mMotd;
    int                      mProtocolVersion;
    std::string              mNetworkVersion;
    int                      mPlayerCount;
    int                      mMaxPlayerCount;
    std::string              mGuid;
    std::string              mLevelName;
    std::string              mGameMode;
    std::uint16_t            mLocalPort;
    std::uint16_t            mLocalPortV6;
    std::vector<std::string> mOther;
    std::string              mIpAndPort;

    std::optional<ServerPongNetherNetData> mNetherNetData;

public:
    ServerPongBeforeEvent(
        std::string              motd,
        int                      protocolVersion,
        std::string              networkVersion,
        int                      playerCount,
        int                      maxPlayerCount,
        std::string              guid,
        std::string              levelName,
        std::string              gameMode,
        std::uint16_t            localPort,
        std::uint16_t            localPortV6,
        std::vector<std::string> other,
        std::string              ipAndPort,
        std::optional<ServerPongNetherNetData> netherNetData = std::nullopt
    )
    : Cancellable(),
      mMotd(std::move(motd)),
      mProtocolVersion(protocolVersion),
      mNetworkVersion(std::move(networkVersion)),
      mPlayerCount(playerCount),
      mMaxPlayerCount(maxPlayerCount),
      mGuid(std::move(guid)),
      mLevelName(std::move(levelName)),
      mGameMode(std::move(gameMode)),
      mLocalPort(localPort),
      mLocalPortV6(localPortV6),
      mOther(std::move(other)),
      mIpAndPort(std::move(ipAndPort)),
      mNetherNetData(std::move(netherNetData)) {}

    void serialize(CompoundTag&) const override;

    std::string&              motd() { return mMotd; }
    std::string const&        motd() const { return mMotd; }
    int&                      protocolVersion() { return mProtocolVersion; }
    int                       protocolVersion() const { return mProtocolVersion; }
    std::string&              networkVersion() { return mNetworkVersion; }
    std::string const&        networkVersion() const { return mNetworkVersion; }
    int&                      playerCount() { return mPlayerCount; }
    int                       playerCount() const { return mPlayerCount; }
    int&                      maxPlayerCount() { return mMaxPlayerCount; }
    int                       maxPlayerCount() const { return mMaxPlayerCount; }
    std::string&              guid() { return mGuid; }
    std::string const&        guid() const { return mGuid; }
    std::string&              levelName() { return mLevelName; }
    std::string const&        levelName() const { return mLevelName; }
    std::string&              gameMode() { return mGameMode; }
    std::string const&        gameMode() const { return mGameMode; }
    std::uint16_t&            localPort() { return mLocalPort; }
    std::uint16_t             localPort() const { return mLocalPort; }
    std::uint16_t&            localPortV6() { return mLocalPortV6; }
    std::uint16_t             localPortV6() const { return mLocalPortV6; }
    std::vector<std::string>& other() { return mOther; }
    std::vector<std::string> const& other() const { return mOther; }
    std::string const&        ipAndPort() const { return mIpAndPort; }
    std::string               ip() const;
    std::uint16_t             port() const;

    std::optional<ServerPongNetherNetData>& netherNetData() { return mNetherNetData; }
    std::optional<ServerPongNetherNetData> const& netherNetData() const { return mNetherNetData; }
};

class CATALYST_API ServerPongAfterEvent final : public ll::event::Event {
    std::string              mMotd;
    int                      mProtocolVersion;
    std::string              mNetworkVersion;
    int                      mPlayerCount;
    int                      mMaxPlayerCount;
    std::string              mGuid;
    std::string              mLevelName;
    std::string              mGameMode;
    std::uint16_t            mLocalPort;
    std::uint16_t            mLocalPortV6;
    std::vector<std::string> mOther;
    std::string              mIpAndPort;

    std::optional<ServerPongNetherNetData> mNetherNetData;

public:
    ServerPongAfterEvent(
        std::string              motd,
        int                      protocolVersion,
        std::string              networkVersion,
        int                      playerCount,
        int                      maxPlayerCount,
        std::string              guid,
        std::string              levelName,
        std::string              gameMode,
        std::uint16_t            localPort,
        std::uint16_t            localPortV6,
        std::vector<std::string> other,
        std::string              ipAndPort,
        std::optional<ServerPongNetherNetData> netherNetData = std::nullopt
    )
    : Event(),
      mMotd(std::move(motd)),
      mProtocolVersion(protocolVersion),
      mNetworkVersion(std::move(networkVersion)),
      mPlayerCount(playerCount),
      mMaxPlayerCount(maxPlayerCount),
      mGuid(std::move(guid)),
      mLevelName(std::move(levelName)),
      mGameMode(std::move(gameMode)),
      mLocalPort(localPort),
      mLocalPortV6(localPortV6),
      mOther(std::move(other)),
      mIpAndPort(std::move(ipAndPort)),
      mNetherNetData(std::move(netherNetData)) {}

    void serialize(CompoundTag&) const override;

    std::string const&        motd() const { return mMotd; }
    int                       protocolVersion() const { return mProtocolVersion; }
    std::string const&        networkVersion() const { return mNetworkVersion; }
    int                       playerCount() const { return mPlayerCount; }
    int                       maxPlayerCount() const { return mMaxPlayerCount; }
    std::string const&        guid() const { return mGuid; }
    std::string const&        levelName() const { return mLevelName; }
    std::string const&        gameMode() const { return mGameMode; }
    std::uint16_t             localPort() const { return mLocalPort; }
    std::uint16_t             localPortV6() const { return mLocalPortV6; }
    std::vector<std::string> const& other() const { return mOther; }
    std::string const&        ipAndPort() const { return mIpAndPort; }
    std::string               ip() const;
    std::uint16_t             port() const;

    std::optional<ServerPongNetherNetData> const& netherNetData() const { return mNetherNetData; }
};

} // namespace Catalyst
