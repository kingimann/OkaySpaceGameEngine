// Real Steamworks backend. Compiled only when the engine is built with
// -DOKAY_WITH_STEAM=ON and pointed at a Steamworks SDK (STEAMWORKS_SDK_PATH),
// since Valve's SDK is proprietary and cannot be redistributed with the engine.
//
// It implements ISteamService against the documented Steamworks C++ API. At
// run time it requires the Steam client to be running and a steam_appid.txt (or
// a real app id) present.
#include "okay/Platform/Steam/SteamService.hpp"
#include "okay/Core/Log.hpp"

#include "steam/steam_api.h"

#include <deque>
#include <unordered_map>

namespace okay {

// Provided by NullSteamService.cpp for graceful fallback.
std::unique_ptr<ISteamService> CreateNullSteamService();

class SteamworksService : public ISteamService {
public:
    bool Initialize(const SteamConfig& config) override {
        if (!config.skipRelaunchCheck && SteamAPI_RestartAppIfNecessary(config.appId)) {
            OKAY_INFO("Steam: relaunching through the Steam client");
            m_relaunch = true;
            return false;
        }
        if (!SteamAPI_Init()) {
            OKAY_ERROR("Steam: SteamAPI_Init failed (is the Steam client running?)");
            return false;
        }
        m_ready = true;
        if (SteamUserStats()) SteamUserStats()->RequestCurrentStats();
        OKAY_INFO("Steam: connected as '", UserName(), "'");
        return true;
    }

    void Shutdown() override {
        if (m_ready) { SteamAPI_Shutdown(); m_ready = false; }
    }

    void RunCallbacks() override {
        if (m_ready) SteamAPI_RunCallbacks();
    }

    bool IsAvailable() const override { return m_ready; }
    bool WantsRelaunch() const override { return m_relaunch; }
    const char* BackendName() const override { return "steamworks"; }

    std::string UserName() const override {
        if (m_ready && SteamFriends()) return SteamFriends()->GetPersonaName();
        return "Player";
    }
    std::uint64_t UserId() const override {
        if (m_ready && SteamUser()) return SteamUser()->GetSteamID().ConvertToUint64();
        return 0;
    }

    bool UnlockAchievement(const std::string& id) override {
        if (!m_ready || !SteamUserStats()) return false;
        SteamUserStats()->SetAchievement(id.c_str());
        return SteamUserStats()->StoreStats();
    }
    bool IsAchievementUnlocked(const std::string& id) const override {
        bool achieved = false;
        if (m_ready && SteamUserStats())
            SteamUserStats()->GetAchievement(id.c_str(), &achieved);
        return achieved;
    }
    bool ClearAchievement(const std::string& id) override {
        if (!m_ready || !SteamUserStats()) return false;
        SteamUserStats()->ClearAchievement(id.c_str());
        return SteamUserStats()->StoreStats();
    }

    void SetStat(const std::string& name, float value) override {
        if (m_ready && SteamUserStats()) SteamUserStats()->SetStat(name.c_str(), value);
    }
    float GetStat(const std::string& name) const override {
        float v = 0.0f;
        if (m_ready && SteamUserStats()) SteamUserStats()->GetStat(name.c_str(), &v);
        return v;
    }
    bool StoreStats() override {
        return m_ready && SteamUserStats() && SteamUserStats()->StoreStats();
    }

    void IndicateAchievementProgress(const std::string& id, std::uint32_t current, std::uint32_t max) override {
        if (m_ready && SteamUserStats()) SteamUserStats()->IndicateAchievementProgress(id.c_str(), current, max);
    }
    void SetStatInt(const std::string& name, int value) override {
        if (m_ready && SteamUserStats()) SteamUserStats()->SetStat(name.c_str(), (int32)value);
    }
    int GetStatInt(const std::string& name) const override {
        int32 v = 0;
        if (m_ready && SteamUserStats()) SteamUserStats()->GetStat(name.c_str(), &v);
        return (int)v;
    }
    int AchievementCount() const override {
        return (m_ready && SteamUserStats()) ? (int)SteamUserStats()->GetNumAchievements() : 0;
    }
    std::string AchievementName(int index) const override {
        if (!m_ready || !SteamUserStats() || index < 0) return {};
        const char* n = SteamUserStats()->GetAchievementName((uint32)index);
        return n ? std::string(n) : std::string();
    }

    // ---- Leaderboards (ISteamUserStats; asynchronous) --------------------
    // A submitted score waits in a queue: the board is looked up (or created) once, then the score is uploaded
    // keeping the player's best. Results arrive through RunCallbacks.
    bool UploadLeaderboardScore(const std::string& board, std::int32_t score) override {
        if (!m_ready || !SteamUserStats() || board.empty()) return false;
        auto it = m_boards.find(board);
        if (it != m_boards.end()) { UploadTo(it->second, score); return true; }
        m_pendingScores.push_back({board, score});
        StartNextFind();
        return true;
    }

    // ---- Friends / overlay / apps (ISteamFriends, ISteamApps) ------------
    int FriendCount() const override {
        return (m_ready && SteamFriends()) ? SteamFriends()->GetFriendCount(k_EFriendFlagImmediate) : 0;
    }
    std::string FriendName(int index) const override {
        if (!m_ready || !SteamFriends() || index < 0 || index >= FriendCount()) return {};
        const char* n = SteamFriends()->GetFriendPersonaName(SteamFriends()->GetFriendByIndex(index, k_EFriendFlagImmediate));
        return n ? std::string(n) : std::string();
    }
    std::uint64_t FriendId(int index) const override {
        if (!m_ready || !SteamFriends() || index < 0 || index >= FriendCount()) return 0;
        return SteamFriends()->GetFriendByIndex(index, k_EFriendFlagImmediate).ConvertToUint64();
    }
    bool InviteFriend(std::uint64_t friendId) override {
        return m_ready && SteamFriends() && SteamFriends()->InviteUserToGame(CSteamID((uint64)friendId), "");
    }
    void ActivateOverlay(const std::string& page) override {
        if (m_ready && SteamFriends()) SteamFriends()->ActivateGameOverlay(page.empty() ? "Friends" : page.c_str());
    }
    bool IsDlcInstalled(std::uint32_t dlcAppId) const override {
        return m_ready && SteamApps() && SteamApps()->BIsDlcInstalled((AppId_t)dlcAppId);
    }
    bool OwnsApp(std::uint32_t appId) const override {
        return m_ready && SteamApps() && SteamApps()->BIsSubscribedApp((AppId_t)appId);
    }
    std::string Language() const override {
        if (!m_ready || !SteamApps()) return "english";
        const char* l = SteamApps()->GetCurrentGameLanguage();
        return l && l[0] ? std::string(l) : std::string("english");
    }

    void SetRichPresence(const std::string& key, const std::string& value) override {
        if (m_ready && SteamFriends())
            SteamFriends()->SetRichPresence(key.c_str(), value.c_str());
    }

    // ---- Steam Cloud (ISteamRemoteStorage) -----------------------------
    bool CloudWrite(const std::string& file, const std::string& data) override {
        return m_ready && SteamRemoteStorage() &&
               SteamRemoteStorage()->FileWrite(file.c_str(), data.data(), (int32)data.size());
    }
    std::string CloudRead(const std::string& file) const override {
        if (!m_ready || !SteamRemoteStorage() || !SteamRemoteStorage()->FileExists(file.c_str()))
            return {};
        int32 size = SteamRemoteStorage()->GetFileSize(file.c_str());
        std::string buf(size, '\0');
        int32 read = SteamRemoteStorage()->FileRead(file.c_str(), buf.data(), size);
        buf.resize(read > 0 ? read : 0);
        return buf;
    }
    bool CloudHasFile(const std::string& file) const override {
        return m_ready && SteamRemoteStorage() && SteamRemoteStorage()->FileExists(file.c_str());
    }
    bool CloudDelete(const std::string& file) override {
        return m_ready && SteamRemoteStorage() && SteamRemoteStorage()->FileDelete(file.c_str());
    }
    std::vector<std::string> CloudFiles() const override {
        std::vector<std::string> names;
        if (!m_ready || !SteamRemoteStorage()) return names;
        int32 count = SteamRemoteStorage()->GetFileCount();
        for (int32 i = 0; i < count; ++i) {
            int32 size = 0;
            const char* name = SteamRemoteStorage()->GetFileNameAndSize(i, &size);
            if (name) names.emplace_back(name);
        }
        return names;
    }
    bool CloudEnabled() const override {
        return m_ready && SteamRemoteStorage() &&
               SteamRemoteStorage()->IsCloudEnabledForAccount() &&
               SteamRemoteStorage()->IsCloudEnabledForApp();
    }

    // ---- Workshop (ISteamUGC) ------------------------------------------
    // Subscribe / unsubscribe / install queries are synchronous; publishing is
    // an async multi-step flow on the real SDK and is left to higher-level tools.
    bool WorkshopSubscribe(std::uint64_t id) override {
        if (!m_ready || !SteamUGC()) return false;
        SteamUGC()->SubscribeItem((PublishedFileId_t)id);
        return true;
    }
    bool WorkshopUnsubscribe(std::uint64_t id) override {
        if (!m_ready || !SteamUGC()) return false;
        SteamUGC()->UnsubscribeItem((PublishedFileId_t)id);
        return true;
    }
    bool WorkshopIsSubscribed(std::uint64_t id) const override {
        if (!m_ready || !SteamUGC()) return false;
        return (SteamUGC()->GetItemState((PublishedFileId_t)id) & k_EItemStateSubscribed) != 0;
    }
    std::string WorkshopItemPath(std::uint64_t id) const override {
        if (!m_ready || !SteamUGC()) return {};
        uint64 sizeOnDisk = 0; char folder[1024] = {0}; uint32 timestamp = 0;
        if (SteamUGC()->GetItemInstallInfo((PublishedFileId_t)id, &sizeOnDisk,
                                           folder, sizeof(folder), &timestamp))
            return std::string(folder);
        return {};
    }
    std::vector<WorkshopItem> WorkshopSubscribedItems() const override {
        std::vector<WorkshopItem> rows;
        if (!m_ready || !SteamUGC()) return rows;
        uint32 n = SteamUGC()->GetNumSubscribedItems();
        std::vector<PublishedFileId_t> ids(n);
        if (n > 0) SteamUGC()->GetSubscribedItems(ids.data(), n);
        for (auto id : ids) {
            WorkshopItem it;
            it.id = (std::uint64_t)id;
            it.subscribed = true;
            it.contentPath = WorkshopItemPath(it.id);
            it.installed = !it.contentPath.empty();
            rows.push_back(it);
        }
        return rows;
    }

private:
    void UploadTo(SteamLeaderboard_t handle, std::int32_t score) {
        SteamUserStats()->UploadLeaderboardScore(handle, k_ELeaderboardUploadScoreMethodKeepBest, (int32)score, nullptr, 0);
    }
    void StartNextFind() {
        if (m_finding || m_pendingScores.empty() || !SteamUserStats()) return;
        m_finding = true;
        const SteamAPICall_t call = SteamUserStats()->FindOrCreateLeaderboard(m_pendingScores.front().board.c_str(),
                                                                             k_ELeaderboardSortMethodDescending,
                                                                             k_ELeaderboardDisplayTypeNumeric);
        m_findResult.Set(call, this, &SteamworksService::OnFindLeaderboard);
    }
    void OnFindLeaderboard(LeaderboardFindResult_t* result, bool ioFailure) {
        m_finding = false;
        if (m_pendingScores.empty()) return;
        const PendingScore first = m_pendingScores.front();
        if (ioFailure || !result || !result->m_bLeaderboardFound) {
            OKAY_WARN("Steam: leaderboard '", first.board, "' could not be found or created");
            m_pendingScores.pop_front();
        } else {
            m_boards[first.board] = result->m_hSteamLeaderboard;
            // Every queued score for this board goes now.
            for (auto it = m_pendingScores.begin(); it != m_pendingScores.end();) {
                if (it->board == first.board) { UploadTo(result->m_hSteamLeaderboard, it->score); it = m_pendingScores.erase(it); }
                else ++it;
            }
        }
        StartNextFind();
    }

    struct PendingScore { std::string board; std::int32_t score = 0; };
    bool m_ready = false;
    bool m_relaunch = false;
    bool m_finding = false;
    std::deque<PendingScore> m_pendingScores;
    std::unordered_map<std::string, SteamLeaderboard_t> m_boards;
    CCallResult<SteamworksService, LeaderboardFindResult_t> m_findResult;
};

std::unique_ptr<ISteamService> CreateSteamService() {
    // The caller still must Initialize(). If that fails (no Steam client), the Steam accessor
    // (Steam.cpp) swaps in the simulation so the game keeps working.
    return std::make_unique<SteamworksService>();
}

} // namespace okay
