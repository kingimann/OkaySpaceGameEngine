#pragma once
#include "okay/Platform/Account/Account.hpp"
#include "okay/Platform/Account/AccountService.hpp"   // detail JSON helpers
#include <cctype>
#include <cstdlib>
#include <ctime>
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
    // Server-browser details (columns from docs/sql/game_servers.sql; empty /
    // false on a project that hasn't added them yet).
    std::string game;        // game title (the launcher filters by it)
    std::string map;         // current map / scene
    bool hasPassword = false;
    bool dedicated   = false;
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

    /// Advertise with the full server-browser details (game, map, password flag,
    /// dedicated). Falls back to the basic columns on a project without them.
    static std::string Advertise(const GameSession& s) {
        namespace d = okay::account::detail;
        const std::string basic = "\"name\":\"" + d::JsonEscape(s.name) +
            "\",\"host_addr\":\"" + d::JsonEscape(s.hostAddr) +
            "\",\"port\":" + std::to_string(s.port) +
            ",\"max_players\":" + std::to_string(s.maxPlayers) +
            ",\"players\":" + std::to_string(s.players) +
            ",\"room\":\"" + d::JsonEscape(s.room) +
            "\",\"region\":\"" + d::JsonEscape(s.region) + "\"";
        const std::string full = basic + ",\"game\":\"" + d::JsonEscape(s.game) +
            "\",\"map\":\"" + d::JsonEscape(s.map) +
            "\",\"has_password\":" + (s.hasPassword ? "true" : "false") +
            ",\"dedicated\":" + (s.dedicated ? "true" : "false");
        account::ApiResponse r = Account::Api("/rest/v1/game_sessions", "POST", "[{" + full + "}]",
                                              "Prefer: return=representation");
        if (!r.ok && r.reached && r.status == 400)   // columns not there yet: basic listing
            r = Account::Api("/rest/v1/game_sessions", "POST", "[{" + basic + "}]", "Prefer: return=representation");
        return r.ok ? d::JsonField(r.body, "id") : std::string{};
    }

    /// Open sessions for the server browser: only ones heartbeated in the last
    /// `freshSeconds` (a crashed host drops off), optionally for one `game`.
    static std::vector<GameSession> Browse(const std::string& game = {}, int freshSeconds = 150) {
        if (!Account::Exists()) return {};
        return BrowseWith(Account::Get(), game, freshSeconds);
    }
    /// Browse() through a specific account service (the launcher's worker threads).
    static std::vector<GameSession> BrowseWith(account::AccountService& svc, const std::string& game = {},
                                               int freshSeconds = 150) {
        char iso[32] = {0};
        std::time_t t = std::time(nullptr) - freshSeconds;
        if (std::tm* g = std::gmtime(&t)) std::strftime(iso, sizeof(iso), "%Y-%m-%dT%H:%M:%SZ", g);
        const std::string fresh = std::string("&updated_at=gt.") + iso + "&order=players.desc,updated_at.desc&limit=200";
        std::string path = "/rest/v1/game_sessions?select=id,name,room,host_addr,port,players,max_players,region,game,map,has_password,dedicated" + fresh;
        if (!game.empty()) path += "&game=eq." + UrlEnc(game);
        account::ApiResponse r = svc.Api(path, "GET");
        if (!r.ok && r.reached && r.status == 400)   // older table: basic columns
            r = svc.Api("/rest/v1/game_sessions?select=id,name,room,host_addr,port,players,max_players,region" + fresh, "GET");
        std::vector<GameSession> out;
        if (!r.ok) return out;
        for (const std::string& o : SplitObjects(r.body)) {
            GameSession g;
            g.id = Raw(o, "id"); g.name = Raw(o, "name"); g.room = Raw(o, "room");
            g.hostAddr = Raw(o, "host_addr"); g.region = Raw(o, "region");
            g.port = std::atoi(Raw(o, "port").c_str());
            g.players = std::atoi(Raw(o, "players").c_str());
            g.maxPlayers = std::atoi(Raw(o, "max_players").c_str());
            g.game = Raw(o, "game"); g.map = Raw(o, "map");
            g.hasPassword = Raw(o, "has_password") == "true";
            g.dedicated = Raw(o, "dedicated") == "true";
            if (!g.hostAddr.empty() && g.port > 0) out.push_back(std::move(g));
        }
        return out;
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

private:
    static std::vector<std::string> SplitObjects(const std::string& json) {
        std::vector<std::string> out; int depth = 0; std::size_t start = 0; bool inStr = false;
        for (std::size_t i = 0; i < json.size(); ++i) {
            const char c = json[i];
            if (inStr) { if (c == '\\') ++i; else if (c == '"') inStr = false; continue; }
            if (c == '"') inStr = true;
            else if (c == '{') { if (depth++ == 0) start = i; }
            else if (c == '}' && depth > 0 && --depth == 0) out.push_back(json.substr(start, i - start + 1));
        }
        return out;
    }
    // A field's value from one flat object: strings unescaped, others as written ("" for null).
    static std::string Raw(const std::string& obj, const std::string& key) {
        const std::string k = "\"" + key + "\"";
        std::size_t p = obj.find(k);
        if (p == std::string::npos) return {};
        p = obj.find(':', p + k.size());
        if (p == std::string::npos) return {};
        ++p; while (p < obj.size() && obj[p] == ' ') ++p;
        std::string v;
        if (p < obj.size() && obj[p] == '"') {
            for (++p; p < obj.size() && obj[p] != '"'; ++p) {
                if (obj[p] == '\\' && p + 1 < obj.size()) ++p;
                v += obj[p];
            }
            return v;
        }
        while (p < obj.size() && obj[p] != ',' && obj[p] != '}') v += obj[p++];
        while (!v.empty() && v.back() == ' ') v.pop_back();
        return v == "null" ? std::string() : v;
    }
    static std::string UrlEnc(const std::string& s) {
        static const char* hex = "0123456789ABCDEF";
        std::string o;
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
            else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
        }
        return o;
    }
};

} // namespace okay
