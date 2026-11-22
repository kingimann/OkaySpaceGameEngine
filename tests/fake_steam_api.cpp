// A stand-in for the Steam client library, for tests of the Store Kit's run-time Steam backend
// (engine/src/Platform/Steam/DynamicSteamService.cpp). It exports the same plain C function names the documented
// flat interface has, and keeps everything in memory. Our own test code; it contains nothing of Valve's.
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>

#if defined(_WIN32)
#define FAKE_EXPORT extern "C" __declspec(dllexport)
#else
#define FAKE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
int g_stats, g_friends, g_user, g_apps, g_remote;   // their addresses are the "interface" pointers
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
// What the test reads back.
FAKE_EXPORT int FakeSteam_Callbacks() { return g_callbacks; }
FAKE_EXPORT const char* FakeSteam_Overlay() { return g_overlay.c_str(); }
FAKE_EXPORT const char* FakeSteam_Presence(const char* k) { return g_presence[k].c_str(); }
FAKE_EXPORT bool FakeSteam_Up() { return g_up; }
