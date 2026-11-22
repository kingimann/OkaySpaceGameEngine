// The Store Kit's run-time Steam backend (okay/Platform/Steam/SteamService.hpp, CreateDynamicSteamService).
//
// It loads the Steam client library the game developer copied from their own Steamworks SDK (we never ship it) and
// calls the plain C functions Valve documents as the "flat" interface: SteamAPI_InitFlat / SteamAPI_Init,
// SteamAPI_RunCallbacks, SteamAPI_Shutdown, SteamAPI_RestartAppIfNecessary, the interface getters
// SteamAPI_Steam<Interface>_v<NNN> and the methods SteamAPI_I<Interface>_<Method>(self, ...). The getters carry a
// version number that changes between SDK releases, so we look for the newest one the library has.
//
// Our own code from Valve's public documentation of those functions; no SDK header or sample is used. The Steam
// client's own protocol is private and is not touched: everything goes through the developer's library.
#include "okay/Platform/Steam/SteamService.hpp"
#include "okay/Core/Log.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace okay {

const char* DefaultSteamLibraryName() {
#if defined(_WIN32)
    return "steam_api64.dll";
#elif defined(__APPLE__)
    return "libsteam_api.dylib";
#else
    return "libsteam_api.so";
#endif
}

namespace {

// ---- a loaded library -----------------------------------------------------------------------------------------
class Library {
public:
    bool Open(const std::string& path) {
#if defined(_WIN32)
        m_h = (void*)LoadLibraryA(path.c_str());
#else
        m_h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
        return m_h != nullptr;
    }
    void* Find(const char* name) const {
        if (!m_h) return nullptr;
#if defined(_WIN32)
        return (void*)GetProcAddress((HMODULE)m_h, name);
#else
        return dlsym(m_h, name);
#endif
    }
    ~Library() {
        // Kept loaded for the life of the process: Steam's library must not be unloaded while its threads run.
    }
private:
    void* m_h = nullptr;
};

// The function shapes (all C calling convention on x64).
using FnInitFlat      = int (*)(char* errMsg1024);           // 0 = OK
using FnInit          = bool (*)();
using FnVoid          = void (*)();
using FnRestart       = bool (*)(std::uint32_t appId);
using FnGetter        = void* (*)();
using FnSelfStr       = bool (*)(void*, const char*);
using FnSelfStrBoolP  = bool (*)(void*, const char*, bool*);
using FnSelfStrU32U32 = bool (*)(void*, const char*, std::uint32_t, std::uint32_t);
using FnSelfStrI32    = bool (*)(void*, const char*, std::int32_t);
using FnSelfStrI32P   = bool (*)(void*, const char*, std::int32_t*);
using FnSelfStrF      = bool (*)(void*, const char*, float);
using FnSelfStrFP     = bool (*)(void*, const char*, float*);
using FnSelf          = bool (*)(void*);
using FnSelfCStr      = const char* (*)(void*);
using FnSelfU64       = std::uint64_t (*)(void*);
using FnSelfStrVoid   = void (*)(void*, const char*);
using FnSelfStrStr    = bool (*)(void*, const char*, const char*);
using FnSelfIntInt    = int (*)(void*, int);
using FnSelfU32       = bool (*)(void*, std::uint32_t);
using FnFileWrite     = bool (*)(void*, const char*, const void*, std::int32_t);
using FnFileRead      = std::int32_t (*)(void*, const char*, void*, std::int32_t);
using FnFileSize      = std::int32_t (*)(void*, const char*);

template <class F> F Get(const Library& lib, const char* name) { return reinterpret_cast<F>(lib.Find(name)); }

// The newest "SteamAPI_Steam<Interface>_v<NNN>" the library exports.
FnGetter FindGetter(const Library& lib, const char* iface) {
    char name[96];
    for (int v = 40; v >= 1; --v) {
        std::snprintf(name, sizeof name, "SteamAPI_Steam%s_v%03d", iface, v);
        if (void* p = lib.Find(name)) return reinterpret_cast<FnGetter>(p);
    }
    return nullptr;
}

class DynamicSteamService : public ISteamService {
public:
    bool Load(const std::string& path) {
        if (!m_lib.Open(path)) return false;
        initFlat = Get<FnInitFlat>(m_lib, "SteamAPI_InitFlat");
        init = Get<FnInit>(m_lib, "SteamAPI_Init");
        shutdown = Get<FnVoid>(m_lib, "SteamAPI_Shutdown");
        runCallbacks = Get<FnVoid>(m_lib, "SteamAPI_RunCallbacks");
        restart = Get<FnRestart>(m_lib, "SteamAPI_RestartAppIfNecessary");
        getUserStats = FindGetter(m_lib, "UserStats");
        getFriends = FindGetter(m_lib, "Friends");
        getUser = FindGetter(m_lib, "User");
        getApps = FindGetter(m_lib, "Apps");
        getRemote = FindGetter(m_lib, "RemoteStorage");
        setAch = Get<FnSelfStr>(m_lib, "SteamAPI_ISteamUserStats_SetAchievement");
        getAch = Get<FnSelfStrBoolP>(m_lib, "SteamAPI_ISteamUserStats_GetAchievement");
        clearAch = Get<FnSelfStr>(m_lib, "SteamAPI_ISteamUserStats_ClearAchievement");
        progress = Get<FnSelfStrU32U32>(m_lib, "SteamAPI_ISteamUserStats_IndicateAchievementProgress");
        setStatI = Get<FnSelfStrI32>(m_lib, "SteamAPI_ISteamUserStats_SetStatInt32");
        getStatI = Get<FnSelfStrI32P>(m_lib, "SteamAPI_ISteamUserStats_GetStatInt32");
        setStatF = Get<FnSelfStrF>(m_lib, "SteamAPI_ISteamUserStats_SetStatFloat");
        getStatF = Get<FnSelfStrFP>(m_lib, "SteamAPI_ISteamUserStats_GetStatFloat");
        storeStats = Get<FnSelf>(m_lib, "SteamAPI_ISteamUserStats_StoreStats");
        requestStats = Get<FnSelf>(m_lib, "SteamAPI_ISteamUserStats_RequestCurrentStats");   // gone in newer SDKs; optional
        personaName = Get<FnSelfCStr>(m_lib, "SteamAPI_ISteamFriends_GetPersonaName");
        overlay = Get<FnSelfStrVoid>(m_lib, "SteamAPI_ISteamFriends_ActivateGameOverlay");
        richPresence = Get<FnSelfStrStr>(m_lib, "SteamAPI_ISteamFriends_SetRichPresence");
        friendCount = Get<FnSelfIntInt>(m_lib, "SteamAPI_ISteamFriends_GetFriendCount");
        steamId = Get<FnSelfU64>(m_lib, "SteamAPI_ISteamUser_GetSteamID");
        subscribedApp = Get<FnSelfU32>(m_lib, "SteamAPI_ISteamApps_BIsSubscribedApp");
        dlcInstalled = Get<FnSelfU32>(m_lib, "SteamAPI_ISteamApps_BIsDlcInstalled");
        language = Get<FnSelfCStr>(m_lib, "SteamAPI_ISteamApps_GetCurrentGameLanguage");
        fileWrite = Get<FnFileWrite>(m_lib, "SteamAPI_ISteamRemoteStorage_FileWrite");
        fileRead = Get<FnFileRead>(m_lib, "SteamAPI_ISteamRemoteStorage_FileRead");
        fileExists = Get<FnSelfStr>(m_lib, "SteamAPI_ISteamRemoteStorage_FileExists");
        fileSize = Get<FnFileSize>(m_lib, "SteamAPI_ISteamRemoteStorage_GetFileSize");
        fileDelete = Get<FnSelfStr>(m_lib, "SteamAPI_ISteamRemoteStorage_FileDelete");
        // The minimum: start, pump, stop, and achievements.
        return (initFlat || init) && shutdown && runCallbacks && getUserStats && setAch && getAch && storeStats;
    }

    bool Initialize(const SteamConfig& config) override {
        if (!config.skipRelaunchCheck && restart && restart(config.appId)) {
            OKAY_INFO("Steam: relaunching through the Steam client");
            m_relaunch = true;
            return false;
        }
        bool ok = false;
        if (initFlat) { char err[1024] = {0}; ok = initFlat(err) == 0; if (!ok && err[0]) OKAY_WARN("Steam: ", err); }
        else ok = init();
        if (!ok) { OKAY_WARN("Steam: the Steam library did not start (is the Steam client running?)"); return false; }
        m_ready = true;
        m_stats = getUserStats();
        m_friends = getFriends ? getFriends() : nullptr;
        m_user = getUser ? getUser() : nullptr;
        m_apps = getApps ? getApps() : nullptr;
        m_remote = getRemote ? getRemote() : nullptr;
        if (requestStats && m_stats) requestStats(m_stats);
        OKAY_INFO("Steam: connected through the game's ", DefaultSteamLibraryName(), " (Store Kit)");
        return true;
    }
    void Shutdown() override { if (m_ready) { shutdown(); m_ready = false; } }
    void RunCallbacks() override { if (m_ready) runCallbacks(); }
    bool IsAvailable() const override { return m_ready; }
    bool WantsRelaunch() const override { return m_relaunch; }
    const char* BackendName() const override { return "steam-runtime"; }

    std::string UserName() const override {
        const char* n = (m_ready && m_friends && personaName) ? personaName(m_friends) : nullptr;
        return n && n[0] ? std::string(n) : std::string("Player");
    }
    std::uint64_t UserId() const override { return (m_ready && m_user && steamId) ? steamId(m_user) : 0; }

    bool UnlockAchievement(const std::string& id) override {
        if (!m_ready || !m_stats) return false;
        return setAch(m_stats, id.c_str()) && storeStats(m_stats);
    }
    bool IsAchievementUnlocked(const std::string& id) const override {
        bool done = false;
        if (m_ready && m_stats) getAch(m_stats, id.c_str(), &done);
        return done;
    }
    bool ClearAchievement(const std::string& id) override {
        return m_ready && m_stats && clearAch && clearAch(m_stats, id.c_str()) && storeStats(m_stats);
    }
    void IndicateAchievementProgress(const std::string& id, std::uint32_t cur, std::uint32_t max) override {
        if (m_ready && m_stats && progress) progress(m_stats, id.c_str(), cur, max);
    }
    void SetStat(const std::string& n, float v) override { if (m_ready && m_stats && setStatF) setStatF(m_stats, n.c_str(), v); }
    float GetStat(const std::string& n) const override {
        float v = 0; if (m_ready && m_stats && getStatF) getStatF(m_stats, n.c_str(), &v); return v;
    }
    void SetStatInt(const std::string& n, int v) override { if (m_ready && m_stats && setStatI) setStatI(m_stats, n.c_str(), (std::int32_t)v); }
    int GetStatInt(const std::string& n) const override {
        std::int32_t v = 0; if (m_ready && m_stats && getStatI) getStatI(m_stats, n.c_str(), &v); return (int)v;
    }
    bool StoreStats() override { return m_ready && m_stats && storeStats(m_stats); }

    bool CloudWrite(const std::string& f, const std::string& d) override {
        return m_ready && m_remote && fileWrite && fileWrite(m_remote, f.c_str(), d.data(), (std::int32_t)d.size());
    }
    std::string CloudRead(const std::string& f) const override {
        if (!m_ready || !m_remote || !fileRead || !fileSize || !CloudHasFile(f)) return {};
        const std::int32_t size = fileSize(m_remote, f.c_str());
        if (size <= 0) return {};
        std::string buf((std::size_t)size, '\0');
        const std::int32_t got = fileRead(m_remote, f.c_str(), &buf[0], size);
        buf.resize(got > 0 ? (std::size_t)got : 0);
        return buf;
    }
    bool CloudHasFile(const std::string& f) const override { return m_ready && m_remote && fileExists && fileExists(m_remote, f.c_str()); }
    bool CloudDelete(const std::string& f) override { return m_ready && m_remote && fileDelete && fileDelete(m_remote, f.c_str()); }

    int FriendCount() const override { return (m_ready && m_friends && friendCount) ? friendCount(m_friends, 0x04 /* immediate friends */) : 0; }
    void ActivateOverlay(const std::string& page) override {
        if (m_ready && m_friends && overlay) overlay(m_friends, page.empty() ? "Friends" : page.c_str());
    }
    bool OwnsApp(std::uint32_t appId) const override { return m_ready && m_apps && subscribedApp && subscribedApp(m_apps, appId); }
    bool IsDlcInstalled(std::uint32_t appId) const override { return m_ready && m_apps && dlcInstalled && dlcInstalled(m_apps, appId); }
    std::string Language() const override {
        const char* l = (m_ready && m_apps && language) ? language(m_apps) : nullptr;
        return l && l[0] ? std::string(l) : std::string("english");
    }
    void SetRichPresence(const std::string& k, const std::string& v) override {
        if (m_ready && m_friends && richPresence) richPresence(m_friends, k.c_str(), v.c_str());
    }

private:
    Library m_lib;
    bool m_ready = false, m_relaunch = false;
    void *m_stats = nullptr, *m_friends = nullptr, *m_user = nullptr, *m_apps = nullptr, *m_remote = nullptr;
    FnInitFlat initFlat = nullptr; FnInit init = nullptr; FnVoid shutdown = nullptr, runCallbacks = nullptr; FnRestart restart = nullptr;
    FnGetter getUserStats = nullptr, getFriends = nullptr, getUser = nullptr, getApps = nullptr, getRemote = nullptr;
    FnSelfStr setAch = nullptr, clearAch = nullptr; FnSelfStrBoolP getAch = nullptr; FnSelfStrU32U32 progress = nullptr;
    FnSelfStrI32 setStatI = nullptr; FnSelfStrI32P getStatI = nullptr; FnSelfStrF setStatF = nullptr; FnSelfStrFP getStatF = nullptr;
    FnSelf storeStats = nullptr, requestStats = nullptr;
    FnSelfCStr personaName = nullptr, language = nullptr; FnSelfStrVoid overlay = nullptr; FnSelfStrStr richPresence = nullptr;
    FnSelfIntInt friendCount = nullptr; FnSelfU64 steamId = nullptr; FnSelfU32 subscribedApp = nullptr, dlcInstalled = nullptr;
    FnFileWrite fileWrite = nullptr; FnFileRead fileRead = nullptr; FnSelfStr fileExists = nullptr, fileDelete = nullptr; FnFileSize fileSize = nullptr;
};

}  // namespace

std::unique_ptr<ISteamService> CreateDynamicSteamService(const std::string& libraryPath) {
    auto s = std::make_unique<DynamicSteamService>();
    if (!s->Load(libraryPath.empty() ? std::string(DefaultSteamLibraryName()) : libraryPath)) return nullptr;
    return s;
}

}  // namespace okay
