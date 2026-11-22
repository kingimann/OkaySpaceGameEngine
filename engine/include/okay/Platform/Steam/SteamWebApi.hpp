#pragma once
// The Store Kit's Steam Web API checks, with our own HTTP code (no SDK): which achievements and stats the game's
// Steam app defines (ISteamUserStats/GetSchemaForGame/v2), compared with the ids the project's scripts use, so a
// typo ("ACH_WIN" vs "ACH_WINS") shows in Build Settings before the game ships.
//
// The publisher Web API key is read from the environment variable STEAM_WEB_API_KEY by the caller, sent only in the
// "x-webapi-key" request header (never in the URL, so it cannot reach a log line), and never stored. Written from
// Valve's public Steam Web API documentation; our own parser.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace okay::steamweb {

inline const char* KeyEnv() { return "STEAM_WEB_API_KEY"; }

struct Schema {
    bool ok = false;
    std::string error;                     // plain words when !ok
    std::string gameName;
    std::vector<std::string> achievements; // API names ("ACH_WIN_ONE_GAME")
    std::vector<std::string> stats;
};

/// The request URL (no key in it).
std::string SchemaUrl(std::uint32_t appId);
/// The request headers: the key travels here only.
std::vector<std::string> SchemaHeaders(const std::string& key);
/// Read a GetSchemaForGame response body.
Schema ParseSchema(const std::string& json);

/// Ids a project's script text uses: Achievements.Unlock / IsUnlocked / Progress("id"), steam_unlock("id"),
/// steam_is_unlocked("id"), steam_progress("id"), and visual-script lines "node <n> SteamUnlock <id>".
std::vector<std::string> FindAchievementIds(const std::string& text);
/// Stat names: Stats.Set / Add / Get("name") and steam_*_stat*("name").
std::vector<std::string> FindStatNames(const std::string& text);
/// Every id the project's scripts (.okayscript, .os, .okayvs, .okayscene under `projectDir`) use, sorted, once each.
void ScanProject(const std::string& projectDir, std::vector<std::string>& achievements, std::vector<std::string>& stats);

/// What is used but not defined on Steam, as plain sentences ("" list = all good).
std::vector<std::string> Compare(const Schema& schema, const std::vector<std::string>& usedAchievements,
                                 const std::vector<std::string>& usedStats);

/// Fetch the schema. `get(url, headers, &status, &body)` does the HTTP GET (the editor passes the engine's client)
/// and returns false when no response came. The key is never put into the returned error text.
Schema FetchSchema(std::uint32_t appId, const std::string& key,
                   const std::function<bool(const std::string&, const std::vector<std::string>&, int*, std::string*)>& get);

/// FetchSchema over the engine's own HTTP client (WinHTTP on Windows; elsewhere the own client when enabled).
Schema FetchSchemaOnline(std::uint32_t appId, const std::string& key);

}  // namespace okay::steamweb
