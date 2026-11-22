// A stand-in for the Steam client library, for tests of the Store Kit's run-time Steam backend
// (engine/src/Platform/Steam/DynamicSteamService.cpp). It exports the same plain C function names the documented
// flat interface has, and keeps everything in memory. Our own test code; it contains nothing of Valve's.
#include "../engine/src/Platform/Steam/SteamFlat.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#if defined(_WIN32)
#define FAKE_EXPORT extern "C" __declspec(dllexport)
#else
#define FAKE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
int g_stats, g_friends, g_user, g_apps, g_remote, g_utils;   // their addresses are the "interface" pointers
bool g_up = false;
int g_callbacks = 0;
std::set<std::string> g_ach;
std::map<std::string, std::int32_t> g_int;
std::map<std::string, std::string> g_files, g_presence;
std::string g_overlay;
}

FAKE_EXPORT int SteamAPI_InitFlat(char* err) { g_up = true; if (err) err[0] = 0; return 0; }
FAKE_EXPORT void SteamAPI_Shutdown() { g_up = false; }
FAKE_EXPORT void SteamAPI_RunCallbacks() { ++g_callbacks; }
FAKE_EXPORT bool SteamAPI_RestartAppIfNecessary(std::uint32_t appId) { return appId == 999; }
// Two versions of one getter: the backend must pick the newest.
FAKE_EXPORT void* SteamAPI_SteamUserStats_v011() { return nullptr; }
FAKE_EXPORT void* SteamAPI_SteamUserStats_v013() { return &g_stats; }
FAKE_EXPORT void* SteamAPI_SteamFriends_v017() { return &g_friends; }
FAKE_EXPORT void* SteamAPI_SteamUser_v023() { return &g_user; }
FAKE_EXPORT void* SteamAPI_SteamApps_v008() { return &g_apps; }
FAKE_EXPORT void* SteamAPI_SteamRemoteStorage_v016() { return &g_remote; }
FAKE_EXPORT void* SteamAPI_SteamUtils_v010() { return &g_utils; }
FAKE_EXPORT bool SteamAPI_ISteamUserStats_SetAchievement(void* s, const char* n) { if (s != &g_stats) return false; g_ach.insert(n); return true; }
FAKE_EXPORT bool SteamAPI_ISteamUserStats_GetAchievement(void* s, const char* n, bool* d) { if (s != &g_stats) return false; *d = g_ach.count(n) != 0; return true; }
FAKE_EXPORT bool SteamAPI_ISteamUserStats_ClearAchievement(void*, const char* n) { g_ach.erase(n); return true; }
FAKE_EXPORT bool SteamAPI_ISteamUserStats_SetStatInt32(void*, const char* n, std::int32_t v) { g_int[n] = v; return true; }
FAKE_EXPORT bool SteamAPI_ISteamUserStats_GetStatInt32(void*, const char* n, std::int32_t* v) { *v = g_int[n]; return true; }
FAKE_EXPORT bool SteamAPI_ISteamUserStats_StoreStats(void* s) { return s == &g_stats; }
FAKE_EXPORT const char* SteamAPI_ISteamFriends_GetPersonaName(void* f) { return f == &g_friends ? "FakeSteamUser" : ""; }
FAKE_EXPORT void SteamAPI_ISteamFriends_ActivateGameOverlay(void*, const char* p) { g_overlay = p; }
FAKE_EXPORT bool SteamAPI_ISteamFriends_SetRichPresence(void*, const char* k, const char* v) { g_presence[k] = v; return true; }
FAKE_EXPORT std::uint64_t SteamAPI_ISteamUser_GetSteamID(void*) { return 76561197960287930ull; }
FAKE_EXPORT bool SteamAPI_ISteamApps_BIsSubscribedApp(void* a, std::uint32_t id) { return a == &g_apps && id == 2468; }
FAKE_EXPORT const char* SteamAPI_ISteamApps_GetCurrentGameLanguage(void*) { return "german"; }
FAKE_EXPORT bool SteamAPI_ISteamRemoteStorage_FileWrite(void*, const char* n, const void* d, std::int32_t len) { g_files[n] = std::string((const char*)d, (std::size_t)len); return true; }
FAKE_EXPORT bool SteamAPI_ISteamRemoteStorage_FileExists(void*, const char* n) { return g_files.count(n) != 0; }
FAKE_EXPORT std::int32_t SteamAPI_ISteamRemoteStorage_GetFileSize(void*, const char* n) { return (std::int32_t)g_files[n].size(); }
FAKE_EXPORT std::int32_t SteamAPI_ISteamRemoteStorage_FileRead(void*, const char* n, void* out, std::int32_t cap) {
    const std::string& d = g_files[n]; const std::int32_t k = (std::int32_t)d.size() < cap ? (std::int32_t)d.size() : cap;
    std::memcpy(out, d.data(), (std::size_t)k); return k;
}
FAKE_EXPORT bool SteamAPI_ISteamRemoteStorage_FileDelete(void*, const char* n) { return g_files.erase(n) != 0; }
FAKE_EXPORT std::int32_t SteamAPI_ISteamRemoteStorage_GetFileCount(void*) { return (std::int32_t)g_files.size(); }
FAKE_EXPORT const char* SteamAPI_ISteamRemoteStorage_GetFileNameAndSize(void*, int i, std::int32_t* size) {
    if (i < 0 || i >= (int)g_files.size()) { if (size) *size = 0; return ""; }
    auto it = g_files.begin(); std::advance(it, i);
    if (size) *size = (std::int32_t)it->second.size();
    return it->first.c_str();
}

// ---- Leaderboards: asynchronous calls that finish on the next SteamAPI_RunCallbacks ------------------------------
namespace {
using namespace okay::steamflat;
struct Call { int issuedAt; int kind; std::vector<unsigned char> result; };
std::map<APICall, Call> g_calls;
APICall g_nextCall = 100;
std::map<std::string, LeaderboardHandle> g_boardIds;
std::map<LeaderboardHandle, std::map<std::uint64_t, std::int32_t>> g_boards;   // board -> steam id -> score
std::map<EntriesHandle, std::vector<LeaderboardEntry>> g_entries;
EntriesHandle g_nextEntries = 7000;
const std::uint64_t kMe = 76561197960287930ull;
template <class T> APICall Issue(int kind, const T& r) {
    Call c{g_callbacks, kind, std::vector<unsigned char>(sizeof(T))};
    std::memcpy(c.result.data(), &r, sizeof(T));
    g_calls[++g_nextCall] = c;
    return g_nextCall;
}
}
FAKE_EXPORT APICall SteamAPI_ISteamUserStats_FindOrCreateLeaderboard(void* s, const char* name, int sort, int display) {
    if (s != &g_stats || !name || sort != kSortDescending || display != kDisplayNumeric) return 0;
    auto it = g_boardIds.find(name);
    if (it == g_boardIds.end()) it = g_boardIds.emplace(name, 5000 + g_boardIds.size()).first;
    g_boards[it->second];
    return Issue(kLeaderboardFindResult, LeaderboardFindResult{it->second, 1});
}
FAKE_EXPORT APICall SteamAPI_ISteamUserStats_UploadLeaderboardScore(void* s, LeaderboardHandle b, int method, std::int32_t score, const std::int32_t*, int) {
    if (s != &g_stats || !g_boards.count(b)) return 0;
    auto& row = g_boards[b];
    const bool had = row.count(kMe) != 0;
    const bool changed = !had || method != kUploadKeepBest || score > row[kMe];
    if (changed) row[kMe] = score;
    return Issue(kLeaderboardScoreUploaded, LeaderboardScoreUploaded{1, b, score, (std::uint8_t)(changed ? 1 : 0), 1, 0});
}
FAKE_EXPORT APICall SteamAPI_ISteamUserStats_DownloadLeaderboardEntries(void* s, LeaderboardHandle b, int request, int start, int end) {
    if (s != &g_stats || !g_boards.count(b) || request != kRequestGlobal || start < 1 || end < start) return 0;
    std::vector<std::pair<std::int32_t, std::uint64_t>> sorted;
    for (const auto& kv : g_boards[b]) sorted.emplace_back(kv.second, kv.first);
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& c) { return a.first > c.first; });
    std::vector<LeaderboardEntry> out;
    for (int r = start; r <= end && r <= (int)sorted.size(); ++r) out.push_back({sorted[r - 1].second, r, sorted[r - 1].first, 0, 0});
    const EntriesHandle h = ++g_nextEntries;
    g_entries[h] = out;
    return Issue(kLeaderboardScoresDownloaded, LeaderboardScoresDownloaded{b, h, (std::int32_t)out.size()});
}
FAKE_EXPORT bool SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry(void* s, EntriesHandle h, int i, LeaderboardEntry* out, std::int32_t*, int) {
    auto it = g_entries.find(h);
    if (s != &g_stats || it == g_entries.end() || i < 0 || i >= (int)it->second.size() || !out) return false;
    *out = it->second[(std::size_t)i];
    return true;
}
FAKE_EXPORT const char* SteamAPI_ISteamFriends_GetFriendPersonaName(void*, std::uint64_t id) {
    if (id == kMe) return "FakeSteamUser";
    static std::string n; n = "Player" + std::to_string(id % 1000); return n.c_str();
}
FAKE_EXPORT bool SteamAPI_ISteamUtils_IsAPICallCompleted(void* u, APICall c, bool* failed) {
    auto it = g_calls.find(c);
    if (u != &g_utils || it == g_calls.end()) { if (failed) *failed = true; return false; }
    if (failed) *failed = false;
    return g_callbacks > it->second.issuedAt;
}
FAKE_EXPORT bool SteamAPI_ISteamUtils_GetAPICallResult(void* u, APICall c, void* out, int size, int expected, bool* failed) {
    auto it = g_calls.find(c);
    if (u != &g_utils || it == g_calls.end() || g_callbacks <= it->second.issuedAt) { if (failed) *failed = true; return false; }
    if (it->second.kind != expected || size != (int)it->second.result.size()) { if (failed) *failed = true; return false; }
    std::memcpy(out, it->second.result.data(), it->second.result.size());
    if (failed) *failed = false;
    g_calls.erase(it);
    return true;
}

// What the test reads back.
FAKE_EXPORT void FakeSteam_SeedScore(const char* board, std::uint64_t id, std::int32_t score) {
    auto it = g_boardIds.find(board);
    if (it == g_boardIds.end()) it = g_boardIds.emplace(board, 5000 + g_boardIds.size()).first;
    g_boards[it->second][id] = score;
}
FAKE_EXPORT std::int32_t FakeSteam_Score(const char* board, std::uint64_t id) {
    auto it = g_boardIds.find(board);
    if (it == g_boardIds.end()) return -1;
    auto& row = g_boards[it->second];
    return row.count(id) ? row[id] : -1;
}
FAKE_EXPORT int FakeSteam_OpenCalls() { return (int)g_calls.size(); }
FAKE_EXPORT int FakeSteam_Callbacks() { return g_callbacks; }
FAKE_EXPORT const char* FakeSteam_Overlay() { return g_overlay.c_str(); }
FAKE_EXPORT const char* FakeSteam_Presence(const char* k) { return g_presence[k].c_str(); }
FAKE_EXPORT bool FakeSteam_Up() { return g_up; }
