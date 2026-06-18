#include "okay/Platform/Steam/SteamService.hpp"
#include "okay/Core/Log.hpp"

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

    void SetStat(const std::string& name, float value) override { m_stats[name] = value; }
    float GetStat(const std::string& name) const override {
        auto it = m_stats.find(name);
        return it != m_stats.end() ? it->second : 0.0f;
    }
    bool StoreStats() override {
        OKAY_TRACE("Steam(sim): stored ", m_stats.size(), " stat(s), ",
                   m_achievements.size(), " achievement(s)");
        return true;
    }

    void SetRichPresence(const std::string& key, const std::string& value) override {
        OKAY_TRACE("Steam(sim): presence ", key, " = ", value);
    }

private:
    std::uint32_t m_appId = 0;
    std::unordered_set<std::string> m_achievements;
    std::unordered_map<std::string, float> m_stats;
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
