#include "okay/Platform/Steam/SteamService.hpp"
#include "okay/Core/Log.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
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

    NullSteamService() { static std::uint64_t s_next = 1; m_selfId = s_next++; }
    ~NullSteamService() override { LeaveLobby(); }

    std::string   UserName() const override { return "Player"; }
    std::uint64_t UserId() const override { return m_selfId; }

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
    std::vector<std::string> CloudFiles() const override {
        std::vector<std::string> names;
        names.reserve(m_cloud.size());
        for (const auto& [name, data] : m_cloud) { (void)data; names.push_back(name); }
        std::sort(names.begin(), names.end());
        return names;
    }
    bool CloudEnabled() const override { return true; }

    // Workshop (UGC): an in-memory catalogue. Publishing assigns a fresh id and
    // auto-subscribes the author; Query does a case-insensitive substring match
    // on title and tags.
    std::uint64_t WorkshopPublish(const WorkshopItem& item) override {
        WorkshopItem it = item;
        it.id = m_nextWorkshopId++;
        it.subscribed = true;
        it.installed = true;
        m_workshop[it.id] = it;
        OKAY_INFO("Steam(sim): workshop published '", it.title, "' (id ", it.id, ")");
        return it.id;
    }
    bool WorkshopUpdate(const WorkshopItem& item) override {
        auto found = m_workshop.find(item.id);
        if (found == m_workshop.end()) return false;
        bool sub = found->second.subscribed, inst = found->second.installed;
        found->second = item;
        found->second.subscribed = sub;
        found->second.installed  = inst;
        return true;
    }
    bool WorkshopSubscribe(std::uint64_t id) override {
        auto found = m_workshop.find(id);
        if (found == m_workshop.end()) return false;
        found->second.subscribed = true;
        found->second.installed  = true;
        return true;
    }
    bool WorkshopUnsubscribe(std::uint64_t id) override {
        auto found = m_workshop.find(id);
        if (found == m_workshop.end()) return false;
        found->second.subscribed = false;
        found->second.installed  = false;
        return true;
    }
    bool WorkshopIsSubscribed(std::uint64_t id) const override {
        auto found = m_workshop.find(id);
        return found != m_workshop.end() && found->second.subscribed;
    }
    std::vector<WorkshopItem> WorkshopSubscribedItems() const override {
        std::vector<WorkshopItem> rows;
        for (const auto& [id, it] : m_workshop) { (void)id; if (it.subscribed) rows.push_back(it); }
        return rows;
    }
    std::string WorkshopItemPath(std::uint64_t id) const override {
        auto found = m_workshop.find(id);
        return (found != m_workshop.end() && found->second.installed) ? found->second.contentPath
                                                                      : std::string{};
    }
    std::vector<WorkshopItem> WorkshopQuery(const std::string& search, int count) const override {
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        };
        std::string needle = lower(search);
        std::vector<WorkshopItem> rows;
        for (const auto& [id, it] : m_workshop) {
            (void)id;
            if (needle.empty()) { rows.push_back(it); continue; }
            if (lower(it.title).find(needle) != std::string::npos) { rows.push_back(it); continue; }
            for (const auto& tag : it.tags)
                if (lower(tag).find(needle) != std::string::npos) { rows.push_back(it); break; }
        }
        std::sort(rows.begin(), rows.end(),
                  [](const WorkshopItem& a, const WorkshopItem& b) { return a.id < b.id; });
        if (count >= 0 && (int)rows.size() > count) rows.resize(count);
        return rows;
    }

    // Integer stats (kills, wins, ...).
    void SetStatInt(const std::string& name, int value) override { m_statsInt[name] = value; }
    int  GetStatInt(const std::string& name) const override {
        auto it = m_statsInt.find(name);
        return it != m_statsInt.end() ? it->second : 0;
    }
    int  IncrementStatInt(const std::string& name, int by) override {
        m_statsInt[name] += by; return m_statsInt[name];
    }

    int  FriendCount() const override { return 0; }       // no friends in the bare simulation
    std::string   FriendName(int) const override { return {}; }
    std::uint64_t FriendId(int) const override { return 0; }
    bool InviteFriend(std::uint64_t friendId) override {
        OKAY_TRACE("Steam(sim): invited friend ", friendId);
        return true;
    }
    void ActivateOverlay(const std::string& page) override {
        OKAY_TRACE("Steam(sim): overlay '", page, "' (no-op in simulation)");
    }

    // ---- Lobbies: a process-wide registry so multiple simulated services (e.g.
    // two players in one test) can create, browse, join and share data. ---------
    std::uint64_t CreateLobby(int maxMembers, const std::string& name) override {
        LeaveLobby();
        std::uint64_t id = NextLobbyId()++;
        LobbyRec rec;
        rec.id = id; rec.name = name; rec.maxMembers = maxMembers < 1 ? 1 : maxMembers;
        rec.owner = m_selfId; rec.members.insert(m_selfId);
        Lobbies()[id] = rec;
        m_currentLobby = id;
        OKAY_INFO("Steam(sim): created lobby ", id, " '", name, "'");
        return id;
    }
    bool JoinLobby(std::uint64_t lobbyId) override {
        auto it = Lobbies().find(lobbyId);
        if (it == Lobbies().end()) return false;
        if ((int)it->second.members.size() >= it->second.maxMembers && !it->second.members.count(m_selfId))
            return false;
        LeaveLobby();
        it->second.members.insert(m_selfId);
        m_currentLobby = lobbyId;
        return true;
    }
    void LeaveLobby() override {
        if (!m_currentLobby) return;
        auto it = Lobbies().find(m_currentLobby);
        if (it != Lobbies().end()) {
            it->second.members.erase(m_selfId);
            if (it->second.members.empty()) Lobbies().erase(it);
        }
        m_currentLobby = 0;
    }
    std::uint64_t CurrentLobby() const override { return m_currentLobby; }
    std::vector<SteamLobby> LobbyList() const override {
        std::vector<SteamLobby> out;
        for (const auto& [id, rec] : Lobbies()) {
            bool full = (int)rec.members.size() >= rec.maxMembers;
            out.push_back({id, rec.name, (int)rec.members.size(), rec.maxMembers, !full});
        }
        return out;
    }
    void SetLobbyData(const std::string& key, const std::string& value) override {
        auto it = Lobbies().find(m_currentLobby);
        if (it != Lobbies().end() && it->second.owner == m_selfId) it->second.data[key] = value;
    }
    std::string GetLobbyData(std::uint64_t lobbyId, const std::string& key) const override {
        auto it = Lobbies().find(lobbyId);
        if (it == Lobbies().end()) return {};
        auto d = it->second.data.find(key);
        return d != it->second.data.end() ? d->second : std::string{};
    }
    std::vector<std::string> LobbyMembers(std::uint64_t lobbyId) const override {
        std::vector<std::string> out;
        auto it = Lobbies().find(lobbyId);
        if (it != Lobbies().end())
            for (std::uint64_t m : it->second.members) out.push_back("Player" + std::to_string(m));
        return out;
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
        m_presence[key] = value;
        OKAY_TRACE("Steam(sim): presence ", key, " = ", value);
    }
    std::string GetRichPresence(const std::string& key) const override {
        auto it = m_presence.find(key);
        return it != m_presence.end() ? it->second : std::string{};
    }

private:
    struct LobbyRec {
        std::uint64_t id = 0;
        std::string   name;
        int           maxMembers = 0;
        std::uint64_t owner = 0;
        std::set<std::uint64_t> members;
        std::unordered_map<std::string, std::string> data;
    };
    // Process-wide so independent simulated services can see each other's lobbies.
    static std::map<std::uint64_t, LobbyRec>& Lobbies() { static std::map<std::uint64_t, LobbyRec> g; return g; }
    static std::uint64_t& NextLobbyId() { static std::uint64_t n = 1; return n; }

    std::uint64_t m_selfId = 0;
    std::uint64_t m_currentLobby = 0;
    std::uint32_t m_appId = 0;
    std::unordered_set<std::string> m_achievements;
    std::unordered_map<std::string, float> m_stats;
    std::unordered_map<std::string, int> m_statsInt;
    std::unordered_map<std::string, std::string> m_presence;
    std::map<std::string, std::map<std::string, std::int32_t>> m_leaderboards;
    std::unordered_map<std::string, std::string> m_cloud;
    std::map<std::uint64_t, WorkshopItem> m_workshop;
    std::uint64_t m_nextWorkshopId = 1000;
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
