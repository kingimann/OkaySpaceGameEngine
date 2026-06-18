#include "okay/Platform/PlayFab/PlayFabService.hpp"
#include "okay/Core/Log.hpp"

#include <algorithm>
#include <unordered_map>

namespace okay {

/// In-memory PlayFab simulation. Implements the full API surface so games can
/// be built and tested offline; data lives only for the session.
class NullPlayFabService : public IPlayFabService {
public:
    bool Initialize(const PlayFabConfig& config) override {
        m_titleId = config.titleId;
        OKAY_INFO("PlayFab(sim): initialized for title '", m_titleId,
                  "' (simulation backend)");
        return true;
    }
    void Shutdown() override { OKAY_INFO("PlayFab(sim): shutdown"); }

    const char* BackendName() const override { return "null"; }
    bool IsRealBackend() const override { return false; }

    bool LoginWithCustomId(const std::string& customId, bool) override {
        m_playFabId = "SIM-" + customId;
        m_session   = "sim-session-ticket";
        m_loggedIn  = true;
        OKAY_INFO("PlayFab(sim): logged in as ", m_playFabId);
        return true;
    }
    bool IsLoggedIn() const override { return m_loggedIn; }
    std::string PlayFabId() const override { return m_playFabId; }
    std::string SessionTicket() const override { return m_session; }

    bool SetUserData(const std::string& key, const std::string& value) override {
        m_data[key] = value;
        return true;
    }
    std::string GetUserData(const std::string& key) const override {
        auto it = m_data.find(key);
        return it != m_data.end() ? it->second : std::string{};
    }

    bool UpdateStatistic(const std::string& name, int value) override {
        m_stats[name] = value;
        return true;
    }
    int GetStatistic(const std::string& name) const override {
        auto it = m_stats.find(name);
        return it != m_stats.end() ? it->second : 0;
    }
    std::vector<LeaderboardEntry> GetLeaderboard(const std::string& name, int maxCount) override {
        std::vector<LeaderboardEntry> out;
        auto it = m_stats.find(name);
        if (it != m_stats.end() && maxCount > 0) {
            out.push_back({m_playFabId, "Player", it->second, 0});
        }
        return out;
    }

private:
    std::string m_titleId, m_playFabId, m_session;
    bool m_loggedIn = false;
    std::unordered_map<std::string, std::string> m_data;
    std::unordered_map<std::string, int> m_stats;
};

#if !defined(OKAY_WITH_PLAYFAB)
std::unique_ptr<IPlayFabService> CreatePlayFabService() {
    return std::make_unique<NullPlayFabService>();
}
#endif

// Shared so the REST backend can fall back to simulation when offline.
std::unique_ptr<IPlayFabService> CreateNullPlayFabService() {
    return std::make_unique<NullPlayFabService>();
}

} // namespace okay
