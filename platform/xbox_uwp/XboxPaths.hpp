#pragma once
// Where files live inside a UWP package, as a pure mapping (no Windows headers, no file access): the package install folder is READ-ONLY
// (game data, shaders), everything the game writes goes to ApplicationData.LocalFolder. The glue fills in the two real folders
// (Package.Current.InstalledLocation.Path, ApplicationData.Current.LocalFolder.Path); this file decides what maps where and refuses
// anything that would escape or write into the install folder. Written from Microsoft's documentation of UWP app data and package
// folders. UNVERIFIED on Windows. Tested on Linux with fake folders.
#include <string>
#include <vector>

namespace okay::xbox {

struct PathRoots {
    std::string install;   // "C:/Program Files/WindowsApps/<pkg>/"  (read-only)
    std::string local;     // ApplicationData.LocalFolder (read + write, backed up)
    std::string temp;      // ApplicationData.TemporaryFolder (read + write, may be cleaned)
};

enum class PathArea { GameData, Shaders, Saves, Config, Cache, Logs, Temp };

struct PathResult { bool ok = false; std::string path; bool writable = false; std::string error; };

inline std::string Slash(std::string s) { for (char& c : s) if (c == '\\') c = '/'; return s; }
inline std::string TrimEndSlash(std::string s) { while (!s.empty() && s.back() == '/') s.pop_back(); return s; }

/// A relative path that stays inside its folder: no "..", no absolute path, no drive letter or stream (':'), no empty parts, no control
/// characters, not ending in '.' or ' ' (Windows trims those), no reserved device names. Returns the cleaned path with '/' separators.
inline bool CleanRelative(const std::string& in, std::string& out, std::string* why = nullptr) {
    auto fail = [&](const char* m) { if (why) *why = m; return false; };
    const std::string s = Slash(in);
    if (s.empty()) { out.clear(); return true; }
    if (s[0] == '/') return fail("absolute path");
    std::string res; std::size_t i = 0;
    while (i <= s.size()) {
        std::size_t e = s.find('/', i); if (e == std::string::npos) e = s.size();
        const std::string part = s.substr(i, e - i);
        if (part.empty()) { if (e == s.size() && i == s.size()) break; return fail("empty path part"); }
        if (part == "." ) { i = e + 1; continue; }
        if (part == "..") return fail("'..' is not allowed");
        for (char c : part) { if ((unsigned char)c < 32 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return fail("character not allowed"); }
        if (part.back() == '.' || part.back() == ' ') return fail("name ends with a dot or space");
        std::string up; for (char c : part) up += (c >= 'a' && c <= 'z') ? char(c - 32) : c;
        const std::string base = up.substr(0, up.find('.'));
        static const char* dev[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "LPT1", "LPT2", "LPT3"};
        for (const char* d : dev) if (base == d) return fail("reserved device name");
        if (!res.empty()) res += '/';
        res += part;
        i = e + 1;
    }
    out = res; return true;
}

inline std::string AreaFolder(PathArea a) {
    switch (a) {
        case PathArea::GameData: return "Data";       // under install
        case PathArea::Shaders:  return "Shaders";    // under install
        case PathArea::Saves:    return "saves";      // under local
        case PathArea::Config:   return "config";
        case PathArea::Cache:    return "cache";
        case PathArea::Logs:     return "logs";
        case PathArea::Temp:     return "";           // the temp folder itself
    }
    return "";
}
inline bool AreaReadOnly(PathArea a) { return a == PathArea::GameData || a == PathArea::Shaders; }

/// The real path for `rel` inside `area`.
inline PathResult Resolve(const PathRoots& r, PathArea a, const std::string& rel) {
    PathResult o; std::string clean;
    if (!CleanRelative(rel, clean, &o.error)) return o;
    const std::string base = AreaReadOnly(a) ? r.install : (a == PathArea::Temp ? r.temp : r.local);
    if (base.empty()) { o.error = "folder not known"; return o; }
    std::string p = TrimEndSlash(Slash(base));
    const std::string f = AreaFolder(a);
    if (!f.empty()) p += "/" + f;
    if (!clean.empty()) p += "/" + clean;
    o.ok = true; o.path = p; o.writable = !AreaReadOnly(a);
    return o;
}

/// Is this full path somewhere the app may write? (inside local or temp, not the install folder, no '..').
inline bool CanWrite(const PathRoots& r, const std::string& full) {
    const std::string p = Slash(full);
    if (p.find("/../") != std::string::npos || (p.size() >= 3 && p.compare(p.size() - 3, 3, "/..") == 0)) return false;
    auto inside = [&](const std::string& root) { const std::string b = TrimEndSlash(Slash(root)); return !b.empty() && p.size() > b.size() && p.compare(0, b.size(), b) == 0 && p[b.size()] == '/'; };
    if (inside(r.install)) return false;
    return inside(r.local) || inside(r.temp);
}

/// Desktop-style locations the engine builds from the environment (getenv("APPDATA") / LOCALAPPDATA, "OkaySpace/<game>/..."): the
/// UWP app has no such variables worth trusting, so those map into LocalFolder. `appFolder` is the game's folder name under it.
enum class DesktopDir { RoamingAppData, LocalAppData, Documents };
inline std::string DesktopDirToLocal(const PathRoots& r, DesktopDir d, const std::string& relative = std::string()) {
    std::string clean; if (!CleanRelative(relative, clean)) clean.clear();
    std::string p = TrimEndSlash(Slash(r.local));
    p += d == DesktopDir::RoamingAppData ? "/roaming" : d == DesktopDir::LocalAppData ? "/localappdata" : "/documents";
    if (!clean.empty()) p += "/" + clean;
    return p;
}

} // namespace okay::xbox
