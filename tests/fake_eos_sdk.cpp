// A stand-in for Epic's EOS SDK library, for tests of the Store Kit's run-time Epic backend
// (engine/src/Platform/Epic/DynamicEpicService.cpp). It exports the plain C function names of the documented EOS C
// interface that the backend calls, keeps a pretend "server" in memory and runs every completion callback from
// EOS_Platform_Tick, the way the real SDK does. Our own test code; it contains nothing of Epic's.
#include "../engine/src/Platform/Epic/EosFlat.hpp"

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#if defined(_WIN32)
#define FAKE_EXPORT extern "C" __declspec(dllexport)
#else
#define FAKE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

using namespace okay::eosflat;

namespace {
int g_platform, g_auth, g_connect, g_ach, g_stats, g_pds, g_presence, g_ui;   // their addresses are the handles
int g_epicUser, g_productUser, g_token, g_mod, g_transfer;
bool g_initialized = false, g_platformUp = false;
int g_shutdowns = 0, g_releases = 0, g_createUsers = 0, g_friendsShown = 0, g_ticks = 0;
bool g_failAuth = false, g_needCreateUser = true;
std::string g_productId, g_clientId, g_clientSecret, g_encryptionKey, g_deploymentId;
std::int32_t g_loginType = -1, g_connectType = -1;
std::string g_loginId, g_loginToken, g_presenceText;
std::int32_t g_presenceStatus = 0;
std::set<std::string> g_serverAch;
std::map<std::string, std::int32_t> g_serverStats;
std::map<std::string, std::string> g_serverFiles;
std::vector<std::function<void()>> g_pending;   // completions waiting for the next tick
std::string g_idTokenJwt = "fake.jwt.token";
AuthIdToken g_idToken;
std::vector<PlayerAchievement> g_achCopies;
std::vector<Stat> g_statCopies;
std::vector<PdsFileMetadata> g_metaCopies;
std::vector<std::string> g_strings;   // keeps copied names alive

const char* Keep(const std::string& s) { g_strings.push_back(s); return g_strings.back().c_str(); }
void Later(std::function<void()> f) { g_pending.push_back(std::move(f)); }
}  // namespace

FAKE_EXPORT Result EOS_Initialize(const InitializeOptions* o) {
    if (!o || o->ApiVersion < 1 || !o->ProductName) return 10;
    if (g_initialized) return kAlreadyConfigured;
    g_initialized = true;
    return kSuccess;
}
FAKE_EXPORT Result EOS_Shutdown() { ++g_shutdowns; g_initialized = false; return kSuccess; }
FAKE_EXPORT Handle EOS_Platform_Create(const PlatformOptions* o) {
    if (!g_initialized || !o || !o->ProductId || !o->ClientCredentials_.ClientId || !o->ClientCredentials_.ClientSecret) return nullptr;
    if (!o->ClientCredentials_.ClientSecret[0]) return nullptr;
    g_productId = o->ProductId; g_clientId = o->ClientCredentials_.ClientId; g_clientSecret = o->ClientCredentials_.ClientSecret;
    g_encryptionKey = o->EncryptionKey ? o->EncryptionKey : "";
    g_deploymentId = o->DeploymentId ? o->DeploymentId : "";
    g_platformUp = true;
    return &g_platform;
}
FAKE_EXPORT void EOS_Platform_Release(Handle h) { if (h == &g_platform) { g_platformUp = false; ++g_releases; } }
FAKE_EXPORT void EOS_Platform_Tick(Handle h) {
    if (h != &g_platform) return;
    ++g_ticks;
    std::vector<std::function<void()>> now;
    now.swap(g_pending);
    for (auto& f : now) f();
}
FAKE_EXPORT Handle EOS_Platform_GetAuthInterface(Handle) { return &g_auth; }
FAKE_EXPORT Handle EOS_Platform_GetConnectInterface(Handle) { return &g_connect; }
FAKE_EXPORT Handle EOS_Platform_GetAchievementsInterface(Handle) { return &g_ach; }
FAKE_EXPORT Handle EOS_Platform_GetStatsInterface(Handle) { return &g_stats; }
FAKE_EXPORT Handle EOS_Platform_GetPlayerDataStorageInterface(Handle) { return &g_pds; }
FAKE_EXPORT Handle EOS_Platform_GetPresenceInterface(Handle) { return &g_presence; }
FAKE_EXPORT Handle EOS_Platform_GetUIInterface(Handle) { return &g_ui; }

// ---- Auth / Connect ---------------------------------------------------------------------------------------------
FAKE_EXPORT void EOS_Auth_Login(Handle h, const AuthLoginOptions* o, void* cd, AuthLoginCallback cb) {
    g_loginType = o->Credentials->Type;
    g_loginId = o->Credentials->Id ? o->Credentials->Id : "";
    g_loginToken = o->Credentials->Token ? o->Credentials->Token : "";
    const bool ok = h == &g_auth && !g_failAuth && !g_loginToken.empty();
    Later([=] { AuthLoginCallbackInfo i{ok ? kSuccess : 2, cd, ok ? &g_epicUser : nullptr}; cb(&i); });
}
FAKE_EXPORT Result EOS_Auth_CopyIdToken(Handle, const AuthCopyIdTokenOptions* o, AuthIdToken** out) {
    if (o->AccountId != &g_epicUser) return kNotFound;
    g_idToken = {1, &g_epicUser, g_idTokenJwt.c_str()};
    *out = &g_idToken;
    return kSuccess;
}
FAKE_EXPORT void EOS_Auth_IdToken_Release(AuthIdToken*) {}
FAKE_EXPORT Result EOS_EpicAccountId_ToString(EpicAccountId id, char* out, std::int32_t* len) {
    const char* s = id == &g_epicUser ? "0123456789abcdef0123456789abcdef" : "";
    const std::int32_t need = (std::int32_t)std::strlen(s) + 1;
    if (*len < need) { *len = need; return 10; }
    std::memcpy(out, s, (std::size_t)need); *len = need;
    return kSuccess;
}
FAKE_EXPORT void EOS_Connect_Login(Handle, const ConnectLoginOptions* o, void* cd, ConnectLoginCallback cb) {
    g_connectType = o->Credentials->Type;
    const bool tokenOk = o->Credentials->Token && g_idTokenJwt == o->Credentials->Token;
    const bool create = g_needCreateUser;
    Later([=] {
        ConnectLoginCallbackInfo i{!tokenOk ? 2 : (create ? kInvalidUser : kSuccess), cd,
                                   tokenOk && !create ? &g_productUser : nullptr, create ? &g_token : nullptr};
        cb(&i);
    });
}
FAKE_EXPORT void EOS_Connect_CreateUser(Handle, const ConnectCreateUserOptions* o, void* cd, ConnectCreateUserCallback cb) {
    ++g_createUsers;
    const bool ok = o->ContinuanceToken_ == &g_token;
    if (ok) g_needCreateUser = false;
    Later([=] { ConnectCreateUserCallbackInfo i{ok ? kSuccess : 10, cd, ok ? &g_productUser : nullptr}; cb(&i); });
}

// ---- Achievements -----------------------------------------------------------------------------------------------
FAKE_EXPORT void EOS_Achievements_UnlockAchievements(Handle, const AchUnlockOptions* o, void* cd, AchUnlockCallback cb) {
    const ProductUserId user = o->UserId;
    const bool ok = user == &g_productUser;
    std::vector<std::string> ids;
    for (std::uint32_t k = 0; k < o->AchievementsCount; ++k) ids.push_back(o->AchievementIds[k]);
    Later([=] {
        if (ok) for (const auto& id : ids) g_serverAch.insert(id);
        AchUnlockCallbackInfo i{ok ? kSuccess : 3, cd, user, (std::uint32_t)ids.size()};
        cb(&i);
    });
}
FAKE_EXPORT void EOS_Achievements_QueryPlayerAchievements(Handle, const AchQueryPlayerOptions* o, void* cd, AchQueryPlayerCallback cb) {
    const bool ok = o->TargetUserId == &g_productUser && o->LocalUserId == &g_productUser;
    Later([=] { AchQueryPlayerCallbackInfo i{ok ? kSuccess : 3, cd, &g_productUser, &g_productUser}; cb(&i); });
}
FAKE_EXPORT std::uint32_t EOS_Achievements_GetPlayerAchievementCount(Handle, const AchGetCountOptions*) {
    // Every defined achievement is listed, locked or not (like the real service); two locked ones exist.
    g_achCopies.clear();
    for (const auto& a : g_serverAch) g_achCopies.push_back({2, Keep(a), 1.0, 1700000000});
    g_achCopies.push_back({2, "ACH_LOCKED_A", 0.0, kTimeUndefined});
    g_achCopies.push_back({2, "ACH_LOCKED_B", 0.5, kTimeUndefined});
    return (std::uint32_t)g_achCopies.size();
}
FAKE_EXPORT Result EOS_Achievements_CopyPlayerAchievementByIndex(Handle, const AchCopyByIndexOptions* o, PlayerAchievement** out) {
    if (o->AchievementIndex >= g_achCopies.size()) return kNotFound;
    *out = &g_achCopies[o->AchievementIndex];
    return kSuccess;
}
FAKE_EXPORT void EOS_Achievements_PlayerAchievement_Release(PlayerAchievement*) {}

// ---- Stats (every stat adds up as a sum here) -------------------------------------------------------------------
FAKE_EXPORT void EOS_Stats_IngestStat(Handle, const StatIngestOptions* o, void* cd, StatIngestCallback cb) {
    const bool ok = o->LocalUserId == &g_productUser && o->TargetUserId == &g_productUser;
    std::vector<std::pair<std::string, std::int32_t>> in;
    for (std::uint32_t k = 0; k < o->StatsCount; ++k) in.emplace_back(o->Stats[k].StatName, o->Stats[k].IngestAmount);
    Later([=] {
        if (ok) for (const auto& p : in) g_serverStats[p.first] += p.second;
        StatIngestCallbackInfo i{ok ? kSuccess : 3, cd, &g_productUser, &g_productUser};
        cb(&i);
    });
}
FAKE_EXPORT void EOS_Stats_QueryStats(Handle, const StatQueryOptions* o, void* cd, StatQueryCallback cb) {
    const bool ok = o->LocalUserId == &g_productUser && o->TargetUserId == &g_productUser;
    Later([=] { StatQueryCallbackInfo i{ok ? kSuccess : 3, cd, &g_productUser, &g_productUser}; cb(&i); });
}
FAKE_EXPORT std::uint32_t EOS_Stats_GetStatsCount(Handle, const StatGetCountOptions*) {
    g_statCopies.clear();
    for (const auto& s : g_serverStats) g_statCopies.push_back({1, Keep(s.first), kTimeUndefined, kTimeUndefined, s.second});
    return (std::uint32_t)g_statCopies.size();
}
FAKE_EXPORT Result EOS_Stats_CopyStatByIndex(Handle, const StatCopyByIndexOptions* o, Stat** out) {
    if (o->StatIndex >= g_statCopies.size()) return kNotFound;
    *out = &g_statCopies[o->StatIndex];
    return kSuccess;
}
FAKE_EXPORT void EOS_Stats_Stat_Release(Stat*) {}

// ---- Player Data Storage ----------------------------------------------------------------------------------------
FAKE_EXPORT Handle EOS_PlayerDataStorage_WriteFile(Handle, const PdsWriteOptions* o, void* cd, PdsTransferDoneCallback cb) {
    const std::string name = o->Filename;
    const std::uint32_t chunk = o->ChunkLengthBytes ? o->ChunkLengthBytes : 4096;
    const ProductUserId user = o->LocalUserId;
    PdsWriteDataCallback data = o->WriteFileDataCallback;
    // The SDK asks for the data chunk by chunk (here: all chunks on the next tick).
    Later([=] {
        std::string got;
        std::int32_t r = kWriteContinue;
        std::vector<char> buf(chunk);
        for (int guard = 0; r == kWriteContinue && guard < 100000; ++guard) {
            PdsWriteDataInfo info{cd, user, name.c_str(), chunk};
            std::uint32_t written = 0;
            r = data(&info, buf.data(), &written);
            if (written > chunk) { r = kWriteFail; break; }
            got.append(buf.data(), written);
        }
        const bool ok = r == kWriteComplete && user == &g_productUser;
        if (ok) g_serverFiles[name] = got;
        PdsTransferDoneInfo i{ok ? kSuccess : 10, cd, user, name.c_str()};
        cb(&i);
    });
    return &g_transfer;
}
FAKE_EXPORT Handle EOS_PlayerDataStorage_ReadFile(Handle, const PdsReadOptions* o, void* cd, PdsTransferDoneCallback cb) {
    const std::string name = o->Filename;
    const ProductUserId user = o->LocalUserId;
    PdsReadDataCallback data = o->ReadFileDataCallback;
    Later([=] {
        auto it = g_serverFiles.find(name);
        Result res = kSuccess;
        if (it == g_serverFiles.end() || user != &g_productUser) res = kNotFound;
        else {
            const std::string& d = it->second;
            const std::size_t step = 1000;   // several chunks for bigger files
            std::size_t at = 0;
            do {
                const std::size_t n = d.size() - at < step ? d.size() - at : step;
                PdsReadDataInfo info{cd, user, name.c_str(), (std::uint32_t)d.size(), at + n >= d.size() ? 1 : 0, (std::uint32_t)n, d.data() + at};
                if (data(&info) != kReadContinue) { res = 17; break; }
                at += n;
            } while (at < d.size());
        }
        PdsTransferDoneInfo i{res, cd, user, name.c_str()};
        cb(&i);
    });
    return &g_transfer;
}
FAKE_EXPORT void EOS_PlayerDataStorageFileTransferRequest_Release(Handle) {}
FAKE_EXPORT void EOS_PlayerDataStorage_DeleteFile(Handle, const PdsDeleteOptions* o, void* cd, PdsDeleteCallback cb) {
    const std::string name = o->Filename;
    Later([=] { const bool had = g_serverFiles.erase(name) != 0; PdsDeleteInfo i{had ? kSuccess : kNotFound, cd, &g_productUser}; cb(&i); });
}
FAKE_EXPORT void EOS_PlayerDataStorage_QueryFileList(Handle, const PdsQueryListOptions* o, void* cd, PdsQueryListCallback cb) {
    const bool ok = o->LocalUserId == &g_productUser;
    Later([=] {
        g_metaCopies.clear();
        for (const auto& f : g_serverFiles) g_metaCopies.push_back({1, (std::uint32_t)f.second.size(), "", Keep(f.first)});
        PdsQueryListInfo i{ok ? kSuccess : 3, cd, &g_productUser, (std::uint32_t)g_metaCopies.size()};
        cb(&i);
    });
}
FAKE_EXPORT Result EOS_PlayerDataStorage_CopyFileMetadataAtIndex(Handle, const PdsCopyMetaAtIndexOptions* o, PdsFileMetadata** out) {
    if (o->Index >= g_metaCopies.size()) return kNotFound;
    *out = &g_metaCopies[o->Index];
    return kSuccess;
}
FAKE_EXPORT void EOS_PlayerDataStorage_FileMetadata_Release(PdsFileMetadata*) {}

// ---- Presence / UI ----------------------------------------------------------------------------------------------
namespace { std::string g_modText; std::int32_t g_modStatus = 0; }
FAKE_EXPORT Result EOS_Presence_CreatePresenceModification(Handle, const PresenceCreateModOptions* o, Handle* out) {
    if (o->LocalUserId != &g_epicUser) return 3;
    g_modText.clear(); g_modStatus = 0;
    *out = &g_mod;
    return kSuccess;
}
FAKE_EXPORT Result EOS_PresenceModification_SetStatus(Handle, const PresenceSetStatusOptions* o) { g_modStatus = o->Status; return kSuccess; }
FAKE_EXPORT Result EOS_PresenceModification_SetRawRichText(Handle, const PresenceSetRichTextOptions* o) {
    if (!o->RichText || std::strlen(o->RichText) > 255) return 10;
    g_modText = o->RichText;
    return kSuccess;
}
FAKE_EXPORT void EOS_PresenceModification_Release(Handle) {}
FAKE_EXPORT void EOS_Presence_SetPresence(Handle, const PresenceSetOptions* o, void* cd, PresenceSetCallback cb) {
    const bool ok = o->LocalUserId == &g_epicUser && o->PresenceModificationHandle == &g_mod;
    const std::string text = g_modText; const std::int32_t st = g_modStatus;
    Later([=] { if (ok) { g_presenceText = text; g_presenceStatus = st; } PresenceSetInfo i{ok ? kSuccess : 3, cd, &g_epicUser}; cb(&i); });
}
FAKE_EXPORT void EOS_UI_ShowFriends(Handle, const UiShowFriendsOptions* o, void* cd, UiShowFriendsCallback cb) {
    const bool ok = o->LocalUserId == &g_epicUser;
    if (ok) ++g_friendsShown;
    Later([=] { UiShowFriendsInfo i{ok ? kSuccess : 3, cd, &g_epicUser}; cb(&i); });
}

// ---- What the test sets and reads back --------------------------------------------------------------------------
FAKE_EXPORT void FakeEos_Reset() {
    g_pending.clear(); g_serverAch.clear(); g_serverStats.clear(); g_serverFiles.clear();
    g_failAuth = false; g_needCreateUser = true; g_createUsers = 0; g_friendsShown = 0;
    g_loginType = g_connectType = -1; g_loginId.clear(); g_loginToken.clear(); g_presenceText.clear(); g_presenceStatus = 0;
}
FAKE_EXPORT void FakeEos_FailAuth(bool on) { g_failAuth = on; }
FAKE_EXPORT void FakeEos_SeedAchievement(const char* id) { g_serverAch.insert(id); }
FAKE_EXPORT void FakeEos_SeedStat(const char* n, std::int32_t v) { g_serverStats[n] = v; }
FAKE_EXPORT void FakeEos_SeedFile(const char* n, const char* d, std::int32_t len) { g_serverFiles[n] = std::string(d, (std::size_t)len); }
FAKE_EXPORT bool FakeEos_HasAchievement(const char* id) { return g_serverAch.count(id) != 0; }
FAKE_EXPORT std::int32_t FakeEos_Stat(const char* n) { auto it = g_serverStats.find(n); return it == g_serverStats.end() ? 0 : it->second; }
FAKE_EXPORT bool FakeEos_HasFile(const char* n) { return g_serverFiles.count(n) != 0; }
FAKE_EXPORT std::int32_t FakeEos_FileSize(const char* n) { auto it = g_serverFiles.find(n); return it == g_serverFiles.end() ? -1 : (std::int32_t)it->second.size(); }
FAKE_EXPORT const char* FakeEos_File(const char* n) { auto it = g_serverFiles.find(n); return it == g_serverFiles.end() ? "" : it->second.c_str(); }
FAKE_EXPORT const char* FakeEos_Presence() { return g_presenceText.c_str(); }
FAKE_EXPORT std::int32_t FakeEos_PresenceStatus() { return g_presenceStatus; }
FAKE_EXPORT std::int32_t FakeEos_LoginType() { return g_loginType; }
FAKE_EXPORT std::int32_t FakeEos_ConnectType() { return g_connectType; }
FAKE_EXPORT const char* FakeEos_LoginId() { return g_loginId.c_str(); }
FAKE_EXPORT const char* FakeEos_LoginToken() { return g_loginToken.c_str(); }
FAKE_EXPORT const char* FakeEos_ProductId() { return g_productId.c_str(); }
FAKE_EXPORT const char* FakeEos_ClientSecret() { return g_clientSecret.c_str(); }
FAKE_EXPORT const char* FakeEos_EncryptionKey() { return g_encryptionKey.c_str(); }
FAKE_EXPORT const char* FakeEos_DeploymentId() { return g_deploymentId.c_str(); }
FAKE_EXPORT int FakeEos_CreateUsers() { return g_createUsers; }
FAKE_EXPORT int FakeEos_FriendsShown() { return g_friendsShown; }
FAKE_EXPORT int FakeEos_Releases() { return g_releases; }
FAKE_EXPORT int FakeEos_Shutdowns() { return g_shutdowns; }
FAKE_EXPORT bool FakeEos_PlatformUp() { return g_platformUp; }
