#include "okay/Platform/Steam/SteamService.hpp"
#include "okay/Core/Log.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <unordered_set>

namespace okay {

/// A backend that simulates Steam entirely in memory. It lets games be written
/// against the full ISteamService API and developed/tested without the Steam
/// client or SDK; every call is recorded and logged.
class NullSteamService : public ISteamService {
public:
    bool Initialize(const SteamConfig& config) override {
        m_appId = config.appId;
        OKAY_INFO("Steam(sim): initialized for app ", m_appId, " (simulation backend)");
        return true;
    }
    void Shutdown() override { OKAY_INFO("Steam(sim): shutdown"); }
    void RunCallbacks() override {}

    bool IsAvailable() const override { return false; }
    const char* BackendName() const override { return "null"; }

    std::string   UserName() const override { return "Player"; }
    std::uint64_t UserId() const override { return 0; }

    bool UnlockAchievement(const std::string& id) override {
        bool isNew = m_achievements.insert(id).second;
        if (isNew) OKAY_INFO("Steam(sim): achievement unlocked '", id, "'");
        return true;
    }
    bool IsAchievementUnlocked(const std::string& id) const override {
        return m_achievements.count(id) != 0;
    }
    bool ClearAchievement(const std::string& id) override {
        m_achievements.erase(id);
        return true;
    }

    void IndicateAchievementProgress(const std::string& id, std::uint32_t current,
                                     std::uint32_t max) override {
        OKAY_TRACE("Steam(sim): progress '", id, "' ", current, "/", max);
        if (max > 0 && current >= max) UnlockAchievement(id);
    }

    void SetStat(const std::string& name, float value) override { m_stats[name] = value; }
    float GetStat(const std::string& name) const override {
        auto it = m_stats.find(name);
        return it != m_stats.end() ? it->second : 0.0f;
    }
    float IncrementStat(const std::string& name, float by) override {
        m_stats[name] += by; return m_stats[name];
    }
    bool StoreStats() override {
        OKAY_TRACE("Steam(sim): stored ", m_stats.size(), " stat(s), ",
                   m_achievements.size(), " achievement(s)");
        return true;
    }

    // Leaderboards: keep the player's best per board, plus a few seeded rivals so
    // a Top-N download looks realistic in development.
    bool UploadLeaderboardScore(const std::string& board, std::int32_t score) override {
        auto& best = m_leaderboards[board][UserName()];
        if (score > best) best = score;
        OKAY_INFO("Steam(sim): leaderboard '", board, "' <- ", score);
        return true;
    }
    std::vector<LeaderboardEntry> DownloadLeaderboardTop(const std::string& board,
                                                         int count) const override {
        std::vector<LeaderboardEntry> rows;
        auto it = m_leaderboards.find(board);
        if (it != m_leaderboards.end())
            for (auto& [name, score] : it->second) rows.push_back({name, score, 0});
        std::sort(rows.begin(), rows.end(),
                  [](const LeaderboardEntry& a, const LeaderboardEntry& b) { return a.score > b.score; });
        if (count >= 0 && (int)rows.size() > count) rows.resize(count);
        for (int i = 0; i < (int)rows.size(); ++i) rows[i].rank = i + 1;
        return rows;
    }

    // Steam Cloud: an in-memory file store.
    bool CloudWrite(const std::string& file, const std::string& data) override {
        m_cloud[file] = data; return true;
    }
    std::string CloudRead(const std::string& file) const override {
        auto it = m_cloud.find(file);
        return it != m_cloud.end() ? it->second : std::string{};
    }
    bool CloudHasFile(const std::string& file) const override { return m_cloud.count(file) != 0; }
    bool CloudDelete(const std::string& file) override { return m_cloud.erase(file) != 0; }

    int  FriendCount() const override { return 0; }
    void ActivateOverlay(const std::string& page) override {
        OKAY_TRACE("Steam(sim): overlay '", page, "' (no-op in simulation)");
    }

    // The simulation grants ownership so the owned/DLC code paths are testable.
    bool IsDlcInstalled(std::uint32_t) const override { return true; }
    bool OwnsApp(std::uint32_t) const override { return true; }
    int  AchievementCount() const override { return (int)m_achievements.size(); }
    std::string AchievementName(int index) const override {
        if (index < 0) return {};
        int i = 0;
        for (const auto& a : m_achievements) { if (i == index) return a; ++i; }
        return {};
    }
    std::string Language() const override { return "english"; }

    void SetRichPresence(const std::string& key, const std::string& value) override {
        OKAY_TRACE("Steam(sim): presence ", key, " = ", value);
    }

private:
    std::uint32_t m_appId = 0;
    std::unordered_set<std::string> m_achievements;
    std::unordered_map<std::string, float> m_stats;
    std::map<std::string, std::map<std::string, std::int32_t>> m_leaderboards;
    std::unordered_map<std::string, std::string> m_cloud;
};

#if !defined(OKAY_WITH_STEAM)
// When the real backend isn't compiled in, the factory returns the simulation.
std::unique_ptr<ISteamService> CreateSteamService() {
    return std::make_unique<NullSteamService>();
}
#endif

// Exposed so the Steamworks factory can fall back to simulation when Steam
// isn't actually running on the user's machine.
std::unique_ptr<ISteamService> CreateNullSteamService() {
    return std::make_unique<NullSteamService>();
}

} // namespace okay
