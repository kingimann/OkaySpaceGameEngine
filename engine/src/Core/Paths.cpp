#include "okay/Core/Paths.hpp"
#include <cstdlib>
#include <filesystem>
#include <utility>
#include <vector>

namespace okay {

namespace {
std::string& AppNameRef() { static std::string n = "OkaySpace"; return n; }
std::string& AssetDirRef() { static std::string d; return d; }
struct Listener { int id; std::function<void(const std::string&)> fn; };
std::vector<Listener>& Listeners() { static std::vector<Listener> l; return l; }
int g_nextListener = 1;

std::string Env_(const char* name) { const char* v = std::getenv(name); return v ? v : ""; }

std::string Norm(std::string s) {
    for (char& c : s) if (c == '\\') c = '/';
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}
// `base` if set, else `fallbackRoot`/`fallbackSub`.
std::string Pick(const std::string& base, const std::string& home, const char* fallbackSub) {
    if (!base.empty()) return Norm(base);
    return Paths::Join(Norm(home), fallbackSub);
}
}

Paths::Env Paths::Env::Current() {
    Env e;
    e.home = Env_("HOME");
    if (e.home.empty()) e.home = Env_("USERPROFILE");
    e.appData = Env_("APPDATA");
    e.localAppData = Env_("LOCALAPPDATA");
    e.xdgData = Env_("XDG_DATA_HOME");
    e.xdgConfig = Env_("XDG_CONFIG_HOME");
    e.xdgCache = Env_("XDG_CACHE_HOME");
    e.xdgState = Env_("XDG_STATE_HOME");
    return e;
}

Paths::Platform Paths::CurrentPlatform() {
#if defined(_WIN32)
    return Platform::Windows;
#elif defined(__APPLE__)
    return Platform::MacOS;
#else
    return Platform::Linux;
#endif
}

void Paths::SetAppName(const std::string& name) { if (!name.empty()) AppNameRef() = name; }
const std::string& Paths::AppName() { return AppNameRef(); }

std::string Paths::Join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const bool aSep = a.back() == '/' || a.back() == '\\';
    const bool bSep = b.front() == '/' || b.front() == '\\';
    if (aSep && bSep) return a + b.substr(1);
    if (aSep || bSep) return a + b;
    return a + "/" + b;
}

std::string Paths::UserDataDirFor(Platform p, const Env& e, const std::string& app) {
    switch (p) {
        case Platform::Windows:
            return Join(Pick(e.appData, Join(Norm(e.home), "AppData/Roaming"), ""), app);
        case Platform::MacOS:
            return Join(Join(Norm(e.home), "Library/Application Support"), app);
        default:
            return Join(Pick(e.xdgData, e.home, ".local/share"), app);
    }
}
std::string Paths::ConfigDirFor(Platform p, const Env& e, const std::string& app) {
    switch (p) {
        case Platform::Windows: return Join(UserDataDirFor(p, e, app), "config");
        case Platform::MacOS:   return Join(UserDataDirFor(p, e, app), "config");
        default:                return Join(Pick(e.xdgConfig, e.home, ".config"), app);
    }
}
std::string Paths::CacheDirFor(Platform p, const Env& e, const std::string& app) {
    switch (p) {
        case Platform::Windows:
            return Join(Join(Pick(e.localAppData, Join(Norm(e.home), "AppData/Local"), ""), app), "cache");
        case Platform::MacOS:
            return Join(Join(Norm(e.home), "Library/Caches"), app);
        default:
            return Join(Pick(e.xdgCache, e.home, ".cache"), app);
    }
}
std::string Paths::LogDirFor(Platform p, const Env& e, const std::string& app) {
    switch (p) {
        case Platform::Windows:
            return Join(Join(Pick(e.localAppData, Join(Norm(e.home), "AppData/Local"), ""), app), "logs");
        case Platform::MacOS:
            return Join(Join(Norm(e.home), "Library/Logs"), app);
        default:
            return Join(Join(Pick(e.xdgState, e.home, ".local/state"), app), "logs");
    }
}

std::string Paths::UserDataDir() { return UserDataDirFor(CurrentPlatform(), Env::Current(), AppName()); }
std::string Paths::ConfigDir()   { return ConfigDirFor(CurrentPlatform(), Env::Current(), AppName()); }
std::string Paths::CacheDir()    { return CacheDirFor(CurrentPlatform(), Env::Current(), AppName()); }
std::string Paths::LogDir()      { return LogDirFor(CurrentPlatform(), Env::Current(), AppName()); }

bool Paths::EnsureDir(const std::string& dir) {
    if (dir.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::u8path(dir), ec);
    return std::filesystem::is_directory(std::filesystem::u8path(dir), ec);
}

void Paths::SetAssetBaseDir(const std::string& dir) {
    AssetDirRef() = dir;
    const auto snapshot = Listeners();   // a listener may add/remove listeners
    for (const auto& l : snapshot) l.fn(dir);
}
const std::string& Paths::AssetBaseDir() { return AssetDirRef(); }

bool Paths::IsAbsolute(const std::string& p) {
    if (p.empty()) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
    return p.size() >= 2 && p[1] == ':' &&
           ((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z'));
}
std::string Paths::Resolve(const std::string& relative) {
    if (IsAbsolute(relative)) return relative;
    return Join(AssetDirRef(), relative);
}

int Paths::AddAssetBaseDirListener(std::function<void(const std::string&)> fn) {
    if (!fn) return 0;
    const int id = g_nextListener++;
    Listeners().push_back({id, fn});
    fn(AssetDirRef());
    return id;
}
void Paths::RemoveAssetBaseDirListener(int id) {
    auto& l = Listeners();
    for (auto it = l.begin(); it != l.end(); ++it) if (it->id == id) { l.erase(it); return; }
}

} // namespace okay
