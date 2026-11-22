// The Epic simulation: every call works in memory, so a game written against IEpicService runs and can be tested
// with no EOS SDK and no Epic account. Our own code.
#include "okay/Platform/Epic/EpicService.hpp"
#include "okay/Core/Log.hpp"

#include <map>
#include <set>

namespace okay {

namespace {
class NullEpicService : public IEpicService {
public:
    bool Initialize(const EpicConfig& config) override {
        m_config = config;
        OKAY_INFO("Epic(sim): initialized for product '", config.productId.empty() ? std::string("(none)") : config.productId,
                  "' (simulation backend)");
        return true;
    }
    void Shutdown() override { m_state = EpicLoginState::LoggedOut; }
    void Tick() override {}
    bool IsAvailable() const override { return false; }
    const char* BackendName() const override { return "null"; }

    bool Login() override {
        // Like the real backend: only with something to log in with (the launcher's code or the developer tool).
        if (m_config.launch.exchangeCode.empty() && m_config.devAuth.empty()) return false;
        m_state = EpicLoginState::LoggedIn;
        OKAY_INFO("Epic(sim): logged in as '", DisplayName(), "'");
        return true;
    }
    EpicLoginState LoginState() const override { return m_state; }
    std::string DisplayName() const override {
        return m_config.launch.userName.empty() ? std::string("Player") : m_config.launch.userName;
    }
    std::string AccountId() const override {
        if (m_state != EpicLoginState::LoggedIn) return {};
        return m_config.launch.userId.empty() ? std::string("sim-account") : m_config.launch.userId;
    }

    bool UnlockAchievement(const std::string& id) override {
        if (id.empty()) return false;
        if (m_achievements.insert(id).second) OKAY_INFO("Epic(sim): achievement unlocked '", id, "'");
        return true;
    }
    bool IsAchievementUnlocked(const std::string& id) const override { return m_achievements.count(id) != 0; }

    bool IngestStat(const std::string& name, std::int32_t amount) override {
        if (name.empty()) return false;
        m_stats[name] += amount;
        return true;
    }
    std::int32_t GetStat(const std::string& name) const override {
        auto it = m_stats.find(name);
        return it == m_stats.end() ? 0 : it->second;
    }

    bool WriteFile(const std::string& name, const std::string& data) override {
        if (name.empty()) return false;
        m_files[name] = data;
        return true;
    }
    std::string ReadFile(const std::string& name) const override {
        auto it = m_files.find(name);
        return it == m_files.end() ? std::string() : it->second;
    }
    bool HasFile(const std::string& name) const override { return m_files.count(name) != 0; }
    bool DeleteFile(const std::string& name) override { return m_files.erase(name) != 0; }

    void SetPresence(const std::string& text) override { m_presence = text; }
    std::string Presence() const override { return m_presence; }

private:
    EpicConfig m_config;
    EpicLoginState m_state = EpicLoginState::LoggedOut;
    std::set<std::string> m_achievements;
    std::map<std::string, std::int32_t> m_stats;
    std::map<std::string, std::string> m_files;
    std::string m_presence;
};
}  // namespace

std::unique_ptr<IEpicService> CreateNullEpicService() { return std::make_unique<NullEpicService>(); }

#if !defined(OKAY_WITH_EOS)
std::unique_ptr<IEpicService> CreateEpicService() { return CreateNullEpicService(); }
#endif

}  // namespace okay
