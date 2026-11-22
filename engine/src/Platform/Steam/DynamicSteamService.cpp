// The Store Kit's run-time Steam backend (okay/Platform/Steam/SteamService.hpp, CreateDynamicSteamService).
//
// It loads the Steam client library the game developer copied from their own Steamworks SDK (we never ship it) and
// calls the plain C functions Valve documents as the "flat" interface: SteamAPI_InitFlat / SteamAPI_Init,
// SteamAPI_RunCallbacks, SteamAPI_Shutdown, SteamAPI_RestartAppIfNecessary, the interface getters
// SteamAPI_Steam<Interface>_v<NNN> and the methods SteamAPI_I<Interface>_<Method>(self, ...). The getters carry a
// version number that changes between SDK releases, so we look for the newest one the library has.
//
// Leaderboards are asynchronous: FindOrCreateLeaderboard, UploadLeaderboardScore and DownloadLeaderboardEntries each
// return a call handle, and the result is collected with ISteamUtils::IsAPICallCompleted / GetAPICallResult from
// RunCallbacks (the Store Kit's per-frame tick). The result records are in SteamFlat.hpp (with what is unconfirmed).
//
// Our own code from Valve's public documentation of those functions; no SDK header or sample is used. The Steam
// client's own protocol is private and is not touched: everything goes through the developer's library.
#include "okay/Platform/Steam/SteamService.hpp"
#include "okay/Core/Log.hpp"

#include "SteamFlat.hpp"
#include "../RuntimeLibrary.hpp"

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

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

using Library = detail::RuntimeLibrary;

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
using FnFileCount     = std::int32_t (*)(void*);
using FnFileNameSize  = const char* (*)(void*, int, std::int32_t*);
using FnFindBoard     = steamflat::APICall (*)(void*, const char*, int, int);
using FnUploadScore   = steamflat::APICall (*)(void*, steamflat::LeaderboardHandle, int, std::int32_t, const std::int32_t*, int);
using FnDownload      = steamflat::APICall (*)(void*, steamflat::LeaderboardHandle, int, int, int);
using FnGetEntry      = bool (*)(void*, steamflat::EntriesHandle, int, steamflat::LeaderboardEntry*, std::int32_t*, int);
using FnPersonaOf     = const char* (*)(void*, std::uint64_t);
using FnCallDone      = bool (*)(void*, steamflat::APICall, bool*);
using FnCallResult    = bool (*)(void*, steamflat::APICall, void*, int, int, bool*);

template <class F> F Get(const Library& lib, const char* name) { return lib.Get<F>(name); }

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
        fileCount = Get<FnFileCount>(m_lib, "SteamAPI_ISteamRemoteStorage_GetFileCount");
        fileNameSize = Get<FnFileNameSize>(m_lib, "SteamAPI_ISteamRemoteStorage_GetFileNameAndSize");
        getUtils = FindGetter(m_lib, "Utils");
        findBoard = Get<FnFindBoard>(m_lib, "SteamAPI_ISteamUserStats_FindOrCreateLeaderboard");
        uploadScore = Get<FnUploadScore>(m_lib, "SteamAPI_ISteamUserStats_UploadLeaderboardScore");
        downloadEntries = Get<FnDownload>(m_lib, "SteamAPI_ISteamUserStats_DownloadLeaderboardEntries");
        getEntry = Get<FnGetEntry>(m_lib, "SteamAPI_ISteamUserStats_GetDownloadedLeaderboardEntry");
        personaOf = Get<FnPersonaOf>(m_lib, "SteamAPI_ISteamFriends_GetFriendPersonaName");
        callDone = Get<FnCallDone>(m_lib, "SteamAPI_ISteamUtils_IsAPICallCompleted");
        callResult = Get<FnCallResult>(m_lib, "SteamAPI_ISteamUtils_GetAPICallResult");
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
        m_utils = getUtils ? getUtils() : nullptr;
        if (requestStats && m_stats) requestStats(m_stats);
        OKAY_INFO("Steam: connected through the game's ", DefaultSteamLibraryName(), " (Store Kit)");
        return true;
    }
    void Shutdown() override { if (m_ready) { shutdown(); m_ready = false; } m_boards.clear(); }
    void RunCallbacks() override {
        if (!m_ready) return;
        runCallbacks();
        PumpLeaderboards();
    }
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
    std::vector<std::string> CloudFiles() const override {
        std::vector<std::string> out;
        if (!m_ready || !m_remote || !fileCount || !fileNameSize) return out;
        const std::int32_t n = fileCount(m_remote);
        for (std::int32_t i = 0; i < n; ++i) {
            std::int32_t size = 0;
            const char* name = fileNameSize(m_remote, i, &size);
            if (name && name[0]) out.emplace_back(name);
        }
        return out;
    }

    // ---- Leaderboards (asynchronous; see PumpLeaderboards) ------------------------------------------------------
    bool UploadLeaderboardScore(const std::string& board, std::int32_t score) override {
        if (!LeaderboardsReady() || board.empty() || board.size() > 128 || !uploadScore) return false;
        Board& b = m_boards[board];
        if (b.missing) return false;
        if (!b.hasPending || score > b.pending) b.pending = score;   // only the best of a burst is sent
        b.hasPending = true;
        Find(board, b);
        return true;
    }
    std::vector<LeaderboardEntry> DownloadLeaderboardTop(const std::string& board, int count) const override {
        std::vector<LeaderboardEntry> out;
        if (!LeaderboardsReady() || board.empty() || board.size() > 128 || count <= 0 || !downloadEntries || !getEntry) return out;
        if (count > 100) count = 100;
        Board& b = m_boards[board];
        if (count > b.wantCount) { b.wantCount = count; b.stale = true; }   // more rows than we have: download again
        if (!b.haveRows) b.stale = true;
        Find(board, b);
        for (const LeaderboardEntry& e : b.rows) { if ((int)out.size() >= count) break; out.push_back(e); }
        return out;
    }

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
    // One leaderboard as this session knows it.
    struct Board {
        steamflat::LeaderboardHandle handle = 0;
        steamflat::APICall finding = 0, uploading = 0, downloading = 0;
        bool missing = false, hasPending = false, stale = false, haveRows = false;
        std::int32_t pending = 0;
        int wantCount = 0;
        std::vector<LeaderboardEntry> rows;
    };
    bool LeaderboardsReady() const { return m_ready && m_stats && m_utils && findBoard && callDone && callResult; }
    void Find(const std::string& name, Board& b) const {
        if (b.handle || b.finding || b.missing) return;
        // A board the game names is made on first use (higher scores first, shown as numbers); one already set up in
        // the Steamworks partner site keeps its own settings.
        b.finding = findBoard(m_stats, name.c_str(), steamflat::kSortDescending, steamflat::kDisplayNumeric);
        if (!b.finding) { b.missing = true; OKAY_WARN("Steam: leaderboard '", name, "' could not be looked up"); }
    }
    // Collect a finished call's result. 1 = got it, 0 = still running, -1 = failed.
    template <class T> int Collect(steamflat::APICall call, int kind, T& out) const {
        bool failed = false;
        if (!callDone(m_utils, call, &failed)) return failed ? -1 : 0;
        if (failed) return -1;
        failed = false;
        if (!callResult(m_utils, call, &out, (int)sizeof(T), kind, &failed) || failed) return -1;
        return 1;
    }
    void PumpLeaderboards() {
        if (!LeaderboardsReady()) return;
        for (auto& kv : m_boards) {
            Board& b = kv.second;
            if (b.finding) {
                steamflat::LeaderboardFindResult r{};
                const int got = Collect(b.finding, steamflat::kLeaderboardFindResult, r);
                if (got != 0) {
                    b.finding = 0;
                    if (got > 0 && r.found && r.board) b.handle = r.board;
                    else { b.missing = true; OKAY_WARN("Steam: leaderboard '", kv.first, "' was not found"); }
                }
            }
            if (!b.handle) continue;
            if (b.uploading) {
                steamflat::LeaderboardScoreUploaded r{};
                const int got = Collect(b.uploading, steamflat::kLeaderboardScoreUploaded, r);
                if (got != 0) {
                    b.uploading = 0;
                    if (got > 0 && r.success) { if (r.changed) b.stale = true; }
                    else OKAY_WARN("Steam: sending a score to '", kv.first, "' failed");
                }
            }
            if (b.hasPending && !b.uploading && uploadScore) {
                b.uploading = uploadScore(m_stats, b.handle, steamflat::kUploadKeepBest, b.pending, nullptr, 0);
                b.hasPending = false;
            }
            if (b.downloading) {
                steamflat::LeaderboardScoresDownloaded r{};
                const int got = Collect(b.downloading, steamflat::kLeaderboardScoresDownloaded, r);
                if (got != 0) {
                    b.downloading = 0;
                    if (got > 0) {
                        b.rows.clear();
                        for (std::int32_t i = 0; i < r.count && i < 100; ++i) {
                            steamflat::LeaderboardEntry e{};
                            if (!getEntry(m_stats, r.entries, i, &e, nullptr, 0)) continue;
                            const char* n = (m_friends && personaOf) ? personaOf(m_friends, e.steamId) : nullptr;
                            b.rows.push_back({n && n[0] ? std::string(n) : std::string("Player"), e.score, e.globalRank});
                        }
                        b.haveRows = true;
                    }
                }
            }
            if (b.stale && b.wantCount > 0 && !b.downloading && !b.uploading && !b.hasPending) {
                b.downloading = downloadEntries(m_stats, b.handle, steamflat::kRequestGlobal, 1, b.wantCount);
                b.stale = false;
            }
        }
    }

    Library m_lib;
    mutable std::map<std::string, Board> m_boards;
    void* m_utils = nullptr;
    FnGetter getUtils = nullptr; FnFileCount fileCount = nullptr; FnFileNameSize fileNameSize = nullptr;
    FnFindBoard findBoard = nullptr; FnUploadScore uploadScore = nullptr; FnDownload downloadEntries = nullptr;
    FnGetEntry getEntry = nullptr; FnPersonaOf personaOf = nullptr; FnCallDone callDone = nullptr; FnCallResult callResult = nullptr;
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
