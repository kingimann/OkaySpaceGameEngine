#pragma once
#include "okay/Platform/Account/Account.hpp"
#include "okay/Platform/Account/AccountService.hpp"   // detail JSON helpers
#include <string>
#include <vector>

namespace okay {

/// One advertised multiplayer session in the Supabase `game_sessions` table — the
/// data a server browser shows and a client needs to connect.
struct GameSession {
    std::string id;          // row id (the host keeps this to heartbeat / remove)
    std::string name;        // friendly server name
    std::string room;        // lobby/room tag ("" = default)
    std::string hostAddr;    // address clients connect to (IP or hostname)
    std::string region;      // optional region label
    int port       = 0;      // UDP port
    int players    = 0;      // current player count
    int maxPlayers = 0;      // capacity
};

/// Supabase-backed matchmaking / server browser. A host advertises its session in
/// the `game_sessions` table; clients List() the open sessions, pick one, and then
/// connect with NetworkManager::StartClient(session.hostAddr, session.port) over the
/// normal UDP transport. Supabase provides *discovery*, not relay — the host must be
/// reachable at hostAddr:port (LAN, port-forward, or a public address).
///
/// All calls go through the signed-in player's authenticated REST session, so a
/// player must be logged in (okay::Account) and the project must have the
/// game_sessions table + RLS (see docs/supabase-backend.md). Everything no-ops
/// gracefully (returns "" / false / empty) offline or signed out.
class Matchmaking {
public:
    /// Advertise a hosted session. Returns its row id ("" on failure). Keep the id
    /// to Heartbeat() while hosting and Unregister() when you stop.
    static std::string Host(const std::string& name, const std::string& hostAddr, int port,
                            int maxPlayers = 8, const std::string& room = {},
                            const std::string& region = {}) {
        namespace d = okay::account::detail;
        std::string body = "[{\"name\":\"" + d::JsonEscape(name) +
            "\",\"host_addr\":\"" + d::JsonEscape(hostAddr) +
            "\",\"port\":" + std::to_string(port) +
            ",\"max_players\":" + std::to_string(maxPlayers) +
            ",\"players\":0,\"room\":\"" + d::JsonEscape(room) +
            "\",\"region\":\"" + d::JsonEscape(region) + "\"}]";
        account::ApiResponse r = Account::Api("/rest/v1/game_sessions", "POST", body,
                                              "Prefer: return=representation");
        return r.ok ? d::JsonField(r.body, "id") : std::string{};
    }

    /// Refresh a session's player count (and, via a DB trigger, its updated_at), so
    /// it stays "live" in listings. Call periodically while hosting.
    static bool Heartbeat(const std::string& id, int players) {
        if (id.empty()) return false;
        return Account::Api("/rest/v1/game_sessions?id=eq." + id, "PATCH",
                            "{\"players\":" + std::to_string(players) + "}").ok;
    }

    /// Remove the advertised session (call when the host stops).
    static bool Unregister(const std::string& id) {
        if (id.empty()) return false;
        return Account::Api("/rest/v1/game_sessions?id=eq." + id, "DELETE").ok;
    }

    /// List open sessions, newest first. Optionally filter by room. (Stale sessions
    /// from crashed hosts are best purged by a scheduled job on the project; see
    /// docs/supabase-backend.md.)
    static std::vector<GameSession> List(const std::string& room = {}) {
        namespace d = okay::account::detail;
        std::string path = "/rest/v1/game_sessions"
            "?select=id,name,room,host_addr,port,players,max_players,region"
            "&order=updated_at.desc";
        if (!room.empty()) path += "&room=eq." + room;
        account::ApiResponse r = Account::Api(path, "GET");
        std::vector<GameSession> out;
        if (!r.ok) return out;
        // PostgREST returns an array of objects; every column is NOT NULL (see the
        // schema), so the per-field value lists line up row-for-row.
        auto ids   = d::JsonFieldValues(r.body, "id");
        auto names = d::JsonFieldValues(r.body, "name");
        auto rooms = d::JsonFieldValues(r.body, "room");
        auto addrs = d::JsonFieldValues(r.body, "host_addr");
        auto regs  = d::JsonFieldValues(r.body, "region");
        auto ports = d::JsonNumberValues(r.body, "port");
        auto plrs  = d::JsonNumberValues(r.body, "players");
        auto maxs  = d::JsonNumberValues(r.body, "max_players");
        std::size_t n = ids.size();
        for (std::size_t i = 0; i < n; ++i) {
            GameSession g;
            g.id       = ids[i];
            g.name     = i < names.size() ? names[i] : "";
            g.room     = i < rooms.size() ? rooms[i] : "";
            g.hostAddr = i < addrs.size() ? addrs[i] : "";
            g.region   = i < regs.size()  ? regs[i]  : "";
            g.port       = i < ports.size() ? (int)ports[i] : 0;
            g.players    = i < plrs.size()  ? (int)plrs[i]  : 0;
            g.maxPlayers = i < maxs.size()  ? (int)maxs[i]  : 0;
            out.push_back(std::move(g));
        }
        return out;
    }
};

} // namespace okay
