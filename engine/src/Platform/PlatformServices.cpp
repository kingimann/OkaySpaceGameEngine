// The platform-services facade (okay/Platform/PlatformServices.hpp): one set of calls routed to Steam, Epic or no
// store. Our own design.
#include "okay/Platform/PlatformServices.hpp"
#include "okay/Platform/Epic/Epic.hpp"
#include "okay/Platform/Steam/Steam.hpp"
#include "okay/Core/Log.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <set>

namespace okay {

const char* StoreKindName(StoreKind k) {
    switch (k) {
        case StoreKind::Steam: return "steam";
        case StoreKind::Epic:  return "epic";
        default:               return "none";
    }
}

StoreKind ParseStoreKind(const std::string& text) {
    std::string l;
    for (char c : text) if (!std::isspace((unsigned char)c)) l.push_back((char)std::tolower((unsigned char)c));
    if (l == "steam") return StoreKind::Steam;
    if (l == "epic" || l == "eos" || l == "epicgames") return StoreKind::Epic;
    return StoreKind::None;
}

namespace {
// Store ids are short tokens: letters, digits, '-', '_', '.' (Epic ids are hex, Steam ids are numbers).
std::string CleanId(const std::string& v) {
    std::string o;
    for (char c : v) if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.') o.push_back(c);
    return o.size() > 128 ? o.substr(0, 128) : o;
}
}  // namespace

bool ApplyStoreKey(StoreSettings& s, const std::string& k, const std::string& v) {
    if (k == "store") s.store = ParseStoreKind(v);
    else if (k == "steam_app_id") {
        char* end = nullptr;
        const long long n = std::strtoll(v.c_str(), &end, 10);
        s.steamAppId = (end != v.c_str() && n > 0 && n <= 0xFFFFFFFFll) ? (std::uint32_t)n : 0u;
    }
    else if (k == "steam_relaunch") s.steamRelaunch = std::atoi(v.c_str()) != 0;
    else if (k == "epic_product_id") s.epicProductId = CleanId(v);
    else if (k == "epic_sandbox_id") s.epicSandboxId = CleanId(v);
    else if (k == "epic_deployment_id") s.epicDeploymentId = CleanId(v);
    else if (k == "epic_client_id") s.epicClientId = CleanId(v);
    else return false;
    return true;
}

std::string WriteStoreConfig(const StoreSettings& s) {
    if (s.store == StoreKind::None) return {};
    std::string o = std::string("store=") + StoreKindName(s.store) + "\n";
    if (s.steamAppId) o += "steam_app_id=" + std::to_string(s.steamAppId) + "\n";
    if (s.steamRelaunch) o += "steam_relaunch=1\n";
    if (!s.epicProductId.empty()) o += "epic_product_id=" + CleanId(s.epicProductId) + "\n";
    if (!s.epicSandboxId.empty()) o += "epic_sandbox_id=" + CleanId(s.epicSandboxId) + "\n";
    if (!s.epicDeploymentId.empty()) o += "epic_deployment_id=" + CleanId(s.epicDeploymentId) + "\n";
    if (!s.epicClientId.empty()) o += "epic_client_id=" + CleanId(s.epicClientId) + "\n";
    return o;
}

// ---- adapters -------------------------------------------------------------------------------------------------
namespace {

class SteamStore : public IStoreServices {
public:
    explicit SteamStore(ISteamService& s) : m_s(s), m_appId(Steam::AppId()) {}
    StoreKind Kind() const override { return StoreKind::Steam; }
    bool IsAvailable() const override { return m_s.IsAvailable(); }
    std::string UserName() const override { return m_s.UserName(); }
    bool UnlockAchievement(const std::string& id) override { return !id.empty() && m_s.UnlockAchievement(id); }
    bool IsAchievementUnlocked(const std::string& id) const override { return m_s.IsAchievementUnlocked(id); }
    void AchievementProgress(const std::string& id, std::uint32_t cur, std::uint32_t max) override {
        if (id.empty()) return;
        if (max > 0 && cur >= max) m_s.UnlockAchievement(id);   // Steam's progress popup never unlocks by itself
        else m_s.IndicateAchievementProgress(id, cur, max);
    }
    void SetStat(const std::string& n, std::int32_t v) override { if (!n.empty()) m_s.SetStatInt(n, v); }
    std::int32_t AddStat(const std::string& n, std::int32_t by) override { return n.empty() ? 0 : m_s.IncrementStatInt(n, by); }
    std::int32_t GetStat(const std::string& n) const override { return m_s.GetStatInt(n); }
    bool StoreStats() override { return m_s.StoreStats(); }
    bool SubmitScore(const std::string& b, std::int32_t s) override {
        if (b.empty()) return false;
        auto it = m_best.find(b);
        if (it == m_best.end() || s > it->second) m_best[b] = s;
        m_s.UploadLeaderboardScore(b, s);
        return true;
    }
    std::vector<StoreScore> TopScores(const std::string& b, int count) const override {
        std::vector<StoreScore> out;
        for (const LeaderboardEntry& e : m_s.DownloadLeaderboardTop(b, count)) out.push_back({e.name, e.score, e.rank});
        if (out.empty()) {   // nothing downloaded (the run-time library has no leaderboards): the player's own best
            auto it = m_best.find(b);
            if (it != m_best.end() && count > 0) out.push_back({m_s.UserName(), it->second, 1});
        }
        return out;
    }
    bool OwnsGame() const override { return !m_s.IsAvailable() || m_appId == 0 || m_s.OwnsApp(m_appId); }
    bool CloudWrite(const std::string& f, const std::string& d) override { return !f.empty() && m_s.CloudWrite(f, d); }
    std::string CloudRead(const std::string& f) const override { return m_s.CloudRead(f); }
    bool CloudHas(const std::string& f) const override { return m_s.CloudHasFile(f); }
    bool CloudDelete(const std::string& f) override { return m_s.CloudDelete(f); }
    void SetPresence(const std::string& k, const std::string& v) override { if (!k.empty()) m_s.SetRichPresence(k, v); }
    void ShowOverlay(const std::string& page) override { m_s.ActivateOverlay(page.empty() ? "friends" : page); }
    void Tick() override { m_s.RunCallbacks(); }
    void Shutdown() override {}   // the Steam accessor owns the service
private:
    ISteamService& m_s;
    std::uint32_t m_appId = 0;
    std::map<std::string, std::int32_t> m_best;
};

// Epic presence is one line of rich text; the facade's key/value pairs are joined in the order they were set
// ("status" alone gives just its value).
class EpicStore : public IStoreServices {
public:
    explicit EpicStore(IEpicService& e) : m_e(e) {}
    StoreKind Kind() const override { return StoreKind::Epic; }
    bool IsAvailable() const override { return m_e.IsAvailable(); }
    std::string UserName() const override { return m_e.DisplayName(); }
    bool UnlockAchievement(const std::string& id) override { return !id.empty() && m_e.UnlockAchievement(id); }
    bool IsAchievementUnlocked(const std::string& id) const override { return m_e.IsAchievementUnlocked(id); }
    void AchievementProgress(const std::string& id, std::uint32_t cur, std::uint32_t max) override {
        // EOS achievements built on stats unlock on the server; a plain achievement unlocks when complete.
        if (!id.empty() && max > 0 && cur >= max) m_e.UnlockAchievement(id);
    }
    // Set sends the value (for stats added up as latest / largest / smallest); Add sends the change (sum stats).
    void SetStat(const std::string& n, std::int32_t v) override {
        if (n.empty()) return;
        m_stats[n] = v; m_e.IngestStat(n, v);
    }
    std::int32_t AddStat(const std::string& n, std::int32_t by) override {
        if (n.empty()) return 0;
        auto it = m_stats.find(n);
        const std::int32_t now = (it == m_stats.end() ? m_e.GetStat(n) : it->second) + by;
        m_stats[n] = now; m_e.IngestStat(n, by);
        return now;
    }
    std::int32_t GetStat(const std::string& n) const override {
        auto it = m_stats.find(n);
        return it == m_stats.end() ? m_e.GetStat(n) : it->second;
    }
    bool StoreStats() override { return true; }   // EOS sends each unlock and stat as it happens
    bool SubmitScore(const std::string& b, std::int32_t s) override {
        // EOS leaderboards are built on stats in the Developer Portal: the board's stat receives the score.
        if (b.empty()) return false;
        auto it = m_best.find(b);
        if (it == m_best.end() || s > it->second) m_best[b] = s;
        return m_e.IngestStat(b, s);
    }
    std::vector<StoreScore> TopScores(const std::string& b, int count) const override {
        std::vector<StoreScore> out;
        auto it = m_best.find(b);
        if (it != m_best.end() && count > 0) out.push_back({m_e.DisplayName(), it->second, 1});
        return out;
    }
    bool OwnsGame() const override { return true; }   // the Epic launcher only starts games the player owns
    bool CloudWrite(const std::string& f, const std::string& d) override { return !f.empty() && m_e.WriteFile(f, d); }
    std::string CloudRead(const std::string& f) const override { return m_e.ReadFile(f); }
    bool CloudHas(const std::string& f) const override { return m_e.HasFile(f); }
    bool CloudDelete(const std::string& f) override { return m_e.DeleteFile(f); }
    void SetPresence(const std::string& k, const std::string& v) override {
        if (k.empty()) return;
        bool found = false;
        for (auto& kv : m_presence) if (kv.first == k) { kv.second = v; found = true; }
        if (!found) m_presence.emplace_back(k, v);
        std::string text;
        for (const auto& kv : m_presence) {
            if (kv.second.empty()) continue;
            if (!text.empty()) text += " - ";
            text += kv.second;
        }
        m_e.SetPresence(text);
    }
    void ShowOverlay(const std::string&) override { m_e.ShowFriends(); }
    void Tick() override { m_e.Tick(); }
    void Shutdown() override {}   // the Epic accessor owns the service
private:
    IEpicService& m_e;
    std::map<std::string, std::int32_t> m_stats, m_best;
    std::vector<std::pair<std::string, std::string>> m_presence;
};

// No store: kept in memory, or saved as plain files in a folder (the player uses its save folder) so achievements,
// stats, best scores and cloud saves survive between runs and the developer can test without any store account.
class NoStore : public IStoreServices {
public:
    explicit NoStore(std::string folder) : m_dir(std::move(folder)) { Load(); }
    StoreKind Kind() const override { return StoreKind::None; }
    bool IsAvailable() const override { return false; }
    std::string UserName() const override { return "Player"; }
    bool UnlockAchievement(const std::string& id) override {
        if (id.empty()) return false;
        if (m_ach.insert(id).second) Save();
        return true;
    }
    bool IsAchievementUnlocked(const std::string& id) const override { return m_ach.count(id) != 0; }
    void AchievementProgress(const std::string& id, std::uint32_t cur, std::uint32_t max) override {
        if (!id.empty() && max > 0 && cur >= max) UnlockAchievement(id);
    }
    void SetStat(const std::string& n, std::int32_t v) override { if (!n.empty()) m_stats[n] = v; }
    std::int32_t AddStat(const std::string& n, std::int32_t by) override { return n.empty() ? 0 : (m_stats[n] += by); }
    std::int32_t GetStat(const std::string& n) const override { auto it = m_stats.find(n); return it == m_stats.end() ? 0 : it->second; }
    bool StoreStats() override { Save(); return true; }
    bool SubmitScore(const std::string& b, std::int32_t s) override {
        if (b.empty()) return false;
        auto it = m_best.find(b);
        if (it == m_best.end() || s > it->second) { m_best[b] = s; Save(); }
        return true;
    }
    std::vector<StoreScore> TopScores(const std::string& b, int count) const override {
        std::vector<StoreScore> out;
        auto it = m_best.find(b);
        if (it != m_best.end() && count > 0) out.push_back({UserName(), it->second, 1});
        return out;
    }
    bool OwnsGame() const override { return true; }
    bool CloudWrite(const std::string& f, const std::string& d) override {
        if (f.empty()) return false;
        m_files[f] = d;
        if (!m_dir.empty()) {
            std::error_code ec;
            const std::filesystem::path p = std::filesystem::path(m_dir) / "cloud" / FileKey(f);
            std::filesystem::create_directories(p.parent_path(), ec);
            std::ofstream o(p, std::ios::binary | std::ios::trunc);
            o << d;
        }
        return true;
    }
    std::string CloudRead(const std::string& f) const override {
        auto it = m_files.find(f);
        if (it != m_files.end()) return it->second;
        if (m_dir.empty()) return {};
        std::ifstream in(std::filesystem::path(m_dir) / "cloud" / FileKey(f), std::ios::binary);
        if (!in) return {};
        std::stringstream ss; ss << in.rdbuf();
        return ss.str();
    }
    bool CloudHas(const std::string& f) const override {
        if (m_files.count(f)) return true;
        std::error_code ec;
        return !m_dir.empty() && std::filesystem::is_regular_file(std::filesystem::path(m_dir) / "cloud" / FileKey(f), ec);
    }
    bool CloudDelete(const std::string& f) override {
        const bool had = CloudHas(f);
        m_files.erase(f);
        std::error_code ec;
        if (!m_dir.empty()) std::filesystem::remove(std::filesystem::path(m_dir) / "cloud" / FileKey(f), ec);
        return had;
    }
    void SetPresence(const std::string&, const std::string&) override {}
    void ShowOverlay(const std::string&) override {}
    void Tick() override {}
    void Shutdown() override { Save(); }

private:
    // A cloud file name as one safe file name: letters, digits, '-', '_', '.' kept, everything else as %XX.
    static std::string FileKey(const std::string& name) {
        static const char* hex = "0123456789ABCDEF";
        std::string o;
        for (unsigned char c : name) {
            if (std::isalnum(c) || c == '-' || c == '_' || (c == '.' && !o.empty() && o.back() != '.')) o.push_back((char)c);
            else { o.push_back('%'); o.push_back(hex[c >> 4]); o.push_back(hex[c & 15]); }
        }
        return o.size() > 200 ? o.substr(0, 200) : o;
    }
    static bool Plain(const std::string& s) {
        for (char c : s) if (c == '\n' || c == '\r' || c == '\t') return false;
        return !s.empty();
    }
    void Save() const {
        if (m_dir.empty()) return;
        std::error_code ec;
        std::filesystem::create_directories(m_dir, ec);
        std::ofstream o(std::filesystem::path(m_dir) / "store_local.txt", std::ios::binary | std::ios::trunc);
        o << "okaystore1\n";
        for (const auto& a : m_ach) if (Plain(a)) o << "A\t" << a << "\n";
        for (const auto& kv : m_stats) if (Plain(kv.first)) o << "S\t" << kv.first << "\t" << kv.second << "\n";
        for (const auto& kv : m_best) if (Plain(kv.first)) o << "B\t" << kv.first << "\t" << kv.second << "\n";
    }
    void Load() {
        if (m_dir.empty()) return;
        std::ifstream in(std::filesystem::path(m_dir) / "store_local.txt", std::ios::binary);
        std::string line;
        if (!in || !std::getline(in, line) || line != "okaystore1") return;
        while (std::getline(in, line)) {
            std::vector<std::string> f;
            std::size_t pos = 0;
            for (;;) { const std::size_t t = line.find('\t', pos); f.push_back(line.substr(pos, t - pos)); if (t == std::string::npos) break; pos = t + 1; }
            if (f.size() == 2 && f[0] == "A") m_ach.insert(f[1]);
            else if (f.size() == 3 && (f[0] == "S" || f[0] == "B")) (f[0] == "S" ? m_stats : m_best)[f[1]] = (std::int32_t)std::strtol(f[2].c_str(), nullptr, 10);
        }
    }

    std::string m_dir;
    std::set<std::string> m_ach;
    std::map<std::string, std::int32_t> m_stats, m_best;
    std::map<std::string, std::string> m_files;
};

std::unique_ptr<IStoreServices>& Current() { static std::unique_ptr<IStoreServices> s; return s; }
StoreSettings& Settings() { static StoreSettings s; return s; }
std::string& LocalFolder() { static std::string f; return f; }

}  // namespace

std::unique_ptr<IStoreServices> MakeSteamStoreServices(ISteamService& steam) { return std::make_unique<SteamStore>(steam); }
std::unique_ptr<IStoreServices> MakeEpicStoreServices(IEpicService& epic) { return std::make_unique<EpicStore>(epic); }
std::unique_ptr<IStoreServices> MakeNoStoreServices(const std::string& folder) { return std::make_unique<NoStore>(folder); }

void PlatformServices::SetLocalFolder(const std::string& folder) { LocalFolder() = folder; }

StoreKind PlatformServices::Choose(StoreKind configured, const char* envValue) {
    if (envValue && envValue[0]) {
        const std::string v = envValue;
        const StoreKind k = ParseStoreKind(v);
        std::string l; for (char c : v) l.push_back((char)std::tolower((unsigned char)c));
        if (k != StoreKind::None || l == "none") return k;   // an unknown word changes nothing
    }
    return configured;
}

void PlatformServices::Configure(const StoreSettings& settings) {
    Settings() = settings;
    if (settings.steamAppId) Steam::SetAppId(settings.steamAppId);
    Steam::SetRelaunchCheck(settings.steamRelaunch);
    // A Steam build talks to Steam through the developer's own steam_api64.dll beside the game (the Store Kit).
    Steam::EnableRuntimeLibrary(settings.store == StoreKind::Steam);
    EpicConfig ec = Epic::Config();
    if (!settings.epicProductId.empty()) ec.productId = settings.epicProductId;
    if (!settings.epicSandboxId.empty()) ec.sandboxId = settings.epicSandboxId;
    if (!settings.epicDeploymentId.empty()) ec.deploymentId = settings.epicDeploymentId;
    if (!settings.epicClientId.empty()) ec.clientId = settings.epicClientId;
    Epic::Configure(ec);
}

IStoreServices& PlatformServices::Get() {
    auto& s = Current();
    if (!s) {
        const StoreKind k = Choose(Settings().store, std::getenv("OKAY_STORE"));
        if (k == StoreKind::Steam) s = MakeSteamStoreServices(Steam::Get());
        else if (k == StoreKind::Epic) {
            IEpicService& e = Epic::Get();
            if (e.LoginState() == EpicLoginState::LoggedOut) e.Login();   // the launcher's exchange code, if any
            s = MakeEpicStoreServices(e);
        }
        else s = MakeNoStoreServices(LocalFolder());
        OKAY_INFO("Platform services: store '", StoreKindName(k), "'");
    }
    return *s;
}

StoreKind PlatformServices::Active() { return Get().Kind(); }

void PlatformServices::Set(std::unique_ptr<IStoreServices> services) {
    if (Current()) Current()->Shutdown();
    Current() = std::move(services);
}

void PlatformServices::Tick() { if (Current()) Current()->Tick(); }

void PlatformServices::Shutdown() {
    if (Current()) { Current()->Shutdown(); Current().reset(); }
}

}  // namespace okay
