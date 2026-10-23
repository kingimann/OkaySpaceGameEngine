#pragma once
// ---------------------------------------------------------------------------
// Server browser queries: ask a game server "who are you?" over UDP without
// joining it, Steam A2S_INFO-style. Every NetworkManager server (listen or
// dedicated) answers with its name, game, map, player count, flags and — from
// the round trip — a ping. A LAN scan broadcasts the same query on the usual
// game ports, so servers on the local network show up with no setup.
//
// QueryServers() blocks for up to `timeoutMs`; call it off the UI thread.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <vector>

namespace okay {
namespace net {
inline constexpr const char*   kQueryMagic    = "OKQ1";
inline constexpr std::uint8_t  kQueryProtocol = 1;
/// Queries are padded to this size so an answer is never larger than the
/// question (no traffic amplification at a spoofed address).
inline constexpr std::size_t   kQueryMinSize  = 160;
enum : std::uint8_t {
    kQueryPassword  = 1,   ///< a password is required to join
    kQueryDedicated = 2,   ///< a headless dedicated server (not a player's game)
    kQueryAuth      = 4,   ///< only signed-in OkaySpace accounts may join
    kQueryEncrypted = 8,   ///< traffic is encrypted
};
/// Ports a LAN scan broadcasts to (the default 45000 and the next few).
inline constexpr std::uint16_t kLanPortFirst = 45000;
inline constexpr std::uint16_t kLanPortLast  = 45015;
} // namespace net

/// One server's answer to a query.
struct ServerInfo {
    std::string host;          ///< as asked (name or IP), or the LAN address that answered
    std::uint16_t port = 0;
    std::string name, game, map;
    int  players = 0, maxPlayers = 0;
    bool password = false, dedicated = false, auth = false, encrypted = false;
    bool responded = false;    ///< false: no answer before the timeout
    bool lan = false;          ///< found by the LAN scan
    int  pingMs = -1;          ///< round trip (-1 = no answer)
    std::string Address() const { return host + ":" + std::to_string(port); }
};

struct QueryTarget { std::string host; std::uint16_t port = 0; };

/// Query `targets` (and, with `lan`, broadcast on the LAN ports), waiting up to
/// `timeoutMs` for answers. Returns one entry per target, in order (responded =
/// false for those that didn't answer), followed by any LAN servers found.
std::vector<ServerInfo> QueryServers(const std::vector<QueryTarget>& targets,
                                     int timeoutMs = 1500, bool lan = false);

/// One player on a server (server browser's player list).
struct ServerPlayer { std::string name, userId; int score = 0; float seconds = 0.0f; };
/// Ask a server who's playing (a two-step challenge, like Steam's A2S_PLAYER).
/// Blocks up to `timeoutMs`. False if it didn't answer.
bool QueryPlayers(const std::string& host, std::uint16_t port, std::vector<ServerPlayer>& out, int timeoutMs = 1500);

/// Split "host:port" ("host" alone uses `defaultPort`). False if empty / bad port.
bool SplitHostPort(const std::string& text, std::string& host, std::uint16_t& port,
                   std::uint16_t defaultPort = 45000);

/// This machine's address on the local network ("" if unknown) — what a LAN
/// friend would connect to. Sends nothing.
std::string LocalNetworkAddress();

} // namespace okay
