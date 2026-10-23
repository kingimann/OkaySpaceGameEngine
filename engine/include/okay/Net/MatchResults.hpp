#pragma once
// Match results -> Supabase (matches / match_players / player_stats; see
// docs/sql/matches.sql). When a match ends, the server (a dedicated server or the
// player hosting) reports who played, their scores and who won. The database
// keeps a per-game stats row for every signed-in player, which feeds:
//   - per-game leaderboards (wins, score, best score, matches, time played),
//   - "Recently played with" in the launcher,
//   - net_stats_* in scripts.
// Reporting and reading go through SECURITY DEFINER / read-only functions, so a
// client can't write stats rows directly. Drives an okay::account::AccountService
// (the launcher passes its own; games use Account::Get()).
#include "okay/Platform/Account/AccountService.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

namespace okay {

struct MatchPlayer {
    std::string userId;     ///< verified account id ("" = anonymous / not signed in)
    std::string name;
    int   score = 0;
    int   place = 0;        ///< 1 = first (equal scores share a place)
    bool  won = false;
    bool  self = false;     ///< the reporting host itself (the database fills in its account)
    float seconds = 0.0f;   ///< time played in this match
};

struct MatchReport {
    std::string game, map, server, mode;
    bool  dedicated = false;
    float duration = 0.0f;  ///< seconds
    std::vector<MatchPlayer> players;

    /// Build from a NetworkManager roster (anything with id, name, userId, score,
    /// seconds). `hostId` marks the host's own entry as `self` (0 for a player host;
    /// pass a value that can't match, e.g. ~0u, on a dedicated server). Winners:
    /// the ids in `winners`, or if empty everyone in first place (when there were
    /// at least two players).
    template <class Roster>
    static MatchReport FromRoster(const Roster& roster, std::uint32_t hostId, float matchSeconds,
                                  const std::vector<std::uint32_t>& winners = {}) {
        MatchReport r;
        r.duration = matchSeconds;
        std::vector<std::pair<std::uint32_t, MatchPlayer>> rows;
        for (const auto& e : roster) {
            MatchPlayer p;
            p.userId = e.userId; p.name = e.name; p.score = e.score; p.self = (e.id == hostId);
            p.seconds = matchSeconds > 0.0f ? std::min(e.seconds, matchSeconds) : e.seconds;
            rows.push_back({e.id, p});
        }
        std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.score > b.second.score; });
        for (std::size_t i = 0; i < rows.size(); ++i)
            rows[i].second.place = (i > 0 && rows[i].second.score == rows[i - 1].second.score) ? rows[i - 1].second.place : (int)i + 1;
        for (auto& [id, p] : rows) {
            if (!winners.empty()) p.won = std::find(winners.begin(), winners.end(), id) != winners.end();
            else p.won = p.place == 1 && rows.size() >= 2;
            r.players.push_back(p);
        }
        return r;
    }

    std::string ToJson() const {
        auto js = [](const std::string& s) {
            std::string o = "\"";
            for (char c : s) {
                if (c == '"' || c == '\\') { o += '\\'; o += c; }
                else if (c == '\n') o += "\\n";
                else if ((unsigned char)c >= 0x20) o += c;
            }
            return o + "\"";
        };
        std::string o = "{\"game\":" + js(game) + ",\"map\":" + js(map) + ",\"server\":" + js(server) +
                        ",\"mode\":" + js(mode) + ",\"dedicated\":" + (dedicated ? "true" : "false") +
                        ",\"duration\":" + std::to_string((long)duration) + ",\"players\":[";
        for (std::size_t i = 0; i < players.size(); ++i) {
            const MatchPlayer& p = players[i];
            if (i) o += ",";
            o += "{\"user_id\":" + (p.userId.empty() ? std::string("null") : js(p.userId)) +
                 ",\"name\":" + js(p.name) + ",\"score\":" + std::to_string(p.score) +
                 ",\"place\":" + std::to_string(p.place) + ",\"won\":" + (p.won ? "true" : "false") +
                 ",\"self\":" + (p.self ? "true" : "false") + ",\"seconds\":" + std::to_string((long)p.seconds) + "}";
        }
        return o + "]}";
    }
};

/// One row of a game's stats (a leaderboard line, or my own stats).
struct PlayerStats {
    std::string userId, username, display, avatar;
    int  matches = 0, wins = 0, bestScore = 0, rank = 0;
    long totalScore = 0, seconds = 0;
    long lastPlayed = 0;
    std::string Name() const { return display.empty() ? username : display; }
};

/// Someone I shared a match with.
struct RecentPlayer {
    std::string userId, username, display, avatar, game;
    long when = 0;          ///< unix seconds of the last match together
    int  matches = 0;       ///< matches together (in the window the server keeps)
    std::string Name() const { return display.empty() ? username : display; }
};

class MatchResults {
public:
    explicit MatchResults(okay::account::AccountService& acct) : acct_(acct) {}

    /// Leaderboard columns, in the order the UI offers them.
    static const char* const* Stats(int* count) {
        static const char* const k[] = {"wins", "score", "best", "matches", "time"};
        if (count) *count = 5;
        return k;
    }

    /// Record a finished match (the host / dedicated server calls this).
    bool Report(const MatchReport& m, std::string* matchId = nullptr, std::string* err = nullptr) {
        if (!acct_.IsLoggedIn()) { if (err) *err = "Sign in first."; return false; }
        auto r = acct_.Api("/rest/v1/rpc/report_match", "POST", "{\"p\":" + m.ToJson() + "}");
        if (r.ok) {
            if (matchId) { *matchId = r.body; Unquote(*matchId); }
            return true;
        }
        if (err) {
            std::string msg = Field(r.body, "message");
            if (r.reached && r.status == 404) msg = "Match results aren't set up on the server yet (docs/sql/matches.sql).";
            *err = msg.empty() ? (r.reached ? "The server refused the match report." : "Couldn't reach the server.") : msg;
        }
        return false;
    }

    /// Top players of a game by `stat` (see Stats()). `supported` is false when the
    /// database has no match tables yet.
    std::vector<PlayerStats> Leaderboard(const std::string& game, const std::string& stat, int limit = 50, bool* supported = nullptr) {
        std::vector<PlayerStats> out;
        if (supported) *supported = true;
        if (!acct_.IsLoggedIn()) return out;
        auto r = acct_.Api("/rest/v1/rpc/game_leaderboard", "POST",
                           "{\"p_game\":" + JStr(game) + ",\"p_stat\":" + JStr(stat) + ",\"p_limit\":" + std::to_string(limit) + "}");
        if (!r.ok) { if (supported && r.reached && r.status == 404) *supported = false; return out; }
        int rank = 0;
        for (const auto& o : SplitObjects(r.body)) {
            PlayerStats s = ParseStats(o);
            s.rank = ++rank;
            out.push_back(s);
        }
        return out;
    }

    /// My stats in a game (false if I have none yet or on failure).
    bool MyStats(const std::string& game, PlayerStats& out) {
        out = PlayerStats{};
        if (!acct_.IsLoggedIn()) return false;
        auto r = acct_.Api("/rest/v1/rpc/my_game_stats", "POST", "{\"p_game\":" + JStr(game) + "}");
        if (!r.ok) return false;
        auto rows = SplitObjects(r.body);
        if (rows.empty()) return false;
        out = ParseStats(rows[0]);
        out.rank = std::atoi(Field(rows[0], "rank").c_str());
        return true;
    }

    /// People I played with recently, newest first.
    std::vector<RecentPlayer> RecentPlayers(int limit = 40, bool* supported = nullptr) {
        std::vector<RecentPlayer> out;
        if (supported) *supported = true;
        if (!acct_.IsLoggedIn()) return out;
        auto r = acct_.Api("/rest/v1/rpc/recent_players", "POST", "{\"p_limit\":" + std::to_string(limit) + "}");
        if (!r.ok) { if (supported && r.reached && r.status == 404) *supported = false; return out; }
        for (const auto& o : SplitObjects(r.body)) {
            RecentPlayer p;
            p.userId = Field(o, "user_id"); p.username = Field(o, "username"); p.display = Field(o, "display_name");
            p.avatar = Field(o, "avatar_url"); p.game = Field(o, "game");
            p.when = ParseIso(Field(o, "last_played")); p.matches = std::atoi(Field(o, "matches").c_str());
            if (!p.userId.empty()) out.push_back(p);
        }
        return out;
    }

    // (JSON helpers, public for tests.)
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
    static std::string Field(const std::string& obj, const std::string& key) {
        const std::string k = "\"" + key + "\"";
        std::size_t p = obj.find(k);
        if (p == std::string::npos) return {};
        p = obj.find(':', p + k.size());
        if (p == std::string::npos) return {};
        ++p; while (p < obj.size() && obj[p] == ' ') ++p;
        std::string v;
        if (p < obj.size() && obj[p] == '"') {
            for (++p; p < obj.size() && obj[p] != '"'; ++p) {
                if (obj[p] == '\\' && p + 1 < obj.size()) { ++p; v += obj[p] == 'n' ? '\n' : obj[p]; continue; }
                v += obj[p];
            }
            return v;
        }
        while (p < obj.size() && obj[p] != ',' && obj[p] != '}') v += obj[p++];
        while (!v.empty() && v.back() == ' ') v.pop_back();
        return v == "null" ? std::string() : v;
    }
    static long ParseIso(const std::string& ls) {
        if (ls.size() < 19) return 0;
        std::tm tm{};
        if (std::sscanf(ls.c_str(), "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) return 0;
        tm.tm_year -= 1900; tm.tm_mon -= 1;
#if defined(_WIN32)
        return (long)_mkgmtime(&tm);
#else
        return (long)timegm(&tm);
#endif
    }

private:
    okay::account::AccountService& acct_;

    static PlayerStats ParseStats(const std::string& o) {
        PlayerStats s;
        s.userId = Field(o, "user_id"); s.username = Field(o, "username"); s.display = Field(o, "display_name");
        s.avatar = Field(o, "avatar_url");
        s.matches = std::atoi(Field(o, "matches").c_str()); s.wins = std::atoi(Field(o, "wins").c_str());
        s.totalScore = std::atol(Field(o, "total_score").c_str()); s.bestScore = std::atoi(Field(o, "best_score").c_str());
        s.seconds = std::atol(Field(o, "seconds").c_str()); s.lastPlayed = ParseIso(Field(o, "last_played"));
        return s;
    }
    static void Unquote(std::string& s) {
        while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    }
    static std::string JStr(const std::string& s) {
        std::string o = "\"";
        for (char c : s) {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if (c == '\n') o += "\\n"; else if ((unsigned char)c >= 0x20) o += c;
        }
        return o + "\"";
    }
};

class NetworkManager;
/// Server: end the match on `net` and report it (roster, scores, places; winners
/// = these peer ids, or first place). Game/map default to the NetworkManager's
/// gameId/mapName, then the given fallbacks. True if a report was queued.
bool EndMatchAndReport(NetworkManager& net, const std::vector<std::uint32_t>& winners = {},
                       const std::string& fallbackGame = std::string(), const std::string& fallbackMap = std::string());

/// Games: send a match report on a background thread with the signed-in account
/// (Account::Get()). False when signed out or threads aren't available (web).
bool SubmitMatchAsync(const MatchReport& report);
/// Reports still being sent / finished so far (either way).
int PendingMatchReports();
int FinishedMatchReports();
/// Wait (up to `timeoutMs`) for queued reports to finish — call before exiting.
void WaitForMatchReports(int timeoutMs = 5000);
/// The last report's outcome ("" until one finished): "ok" or an error message.
std::string LastMatchReportStatus();

} // namespace okay
