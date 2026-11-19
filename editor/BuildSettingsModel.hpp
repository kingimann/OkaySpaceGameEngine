#pragma once
// The Build Settings window's logic, with no ImGui and no editor globals, so the tests run it as it is
// (docs/build_settings_upgrade.md). Written from our own design.
//
//   pages        the plain-word pages in the left list, and which are shown for which platform
//   registry     every setting: its page, its plain label, search words, "changed from default?" and "reset"
//   search       the box above the page list: which settings (and so which pages) match some words
//   presets      Debug / Release (what a test build and a shipping build usually want) and Low..Ultra graphics
//   summary      "what will happen" in plain sentences
//   validation   problems with a Fix action where one exists
//   history      the list of finished builds (time, size, version), stored in <project>/.okay/build_history.txt
//   commands     pre-build / post-build commands: {name} style words filled in, run only when allowed
//   gitignore    the lines a project keeps out of version control, merged into an existing .gitignore
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <sys/wait.h>
#endif
#include "okay/Core/BuildConfigExtra.hpp"
#include "ProjectState.hpp"

namespace oked::bsm {

// ---- pages -------------------------------------------------------------------------------------------------
// The ids are the ones the window already used (kBuildSecs in main.cpp), plus two new pages at the end.
enum Page { kScenes = 0, kPlayer = 1, kDisplay = 2, kGraphics = 3, kAudioInput = 4, kOutput = 5, kServer = 6, kXbox = 7, kGame = 8, kAdvanced = 9, kWeb = 10, kAndroid = 11, kInstaller = 12, kPageCount = 13 };

inline const char* PageName(int p) {
    static const char* n[kPageCount] = {"Scenes", "Player", "Display", "Graphics", "Audio & Input", "Output", "Server", "Xbox", "Game", "Advanced", "Web", "Android", "Installer"};
    return p >= 0 && p < kPageCount ? n[p] : "";
}
inline const char* PageTip(int p) {
    static const char* t[kPageCount] = {
        "Which scenes ship, in what order (the first one starts the game).",
        "Name, company, version, identifier, icon and splash.",
        "Resolution, window mode, frame rate, focus behaviour.",
        "Renderer and quality: shadows, bloom, ambient occlusion, anti-aliasing.",
        "Volume, cursor and keyboard behaviour.",
        "Output folder layout, encryption and what happens after the build.",
        "Name, port, players, password, public listing and anti-cheat (written to server.cfg).",
        "Xbox One Developer Mode package (experimental).",
        "How the game runs: speed, starting language and volumes, logging, environment variables.",
        "Commands before and after the build, command-line arguments, custom defines, version control file, build history.",
        "Memory and compression for the web build (written into build-web.sh).",
        "Package, Android versions, screen direction and permissions (written into AndroidManifest.xml).",
        "A Windows installer script for your game: install folder, licence, Start menu, shortcut, uninstaller."};
    return p >= 0 && p < kPageCount ? t[p] : "";
}
/// platform: 0 Desktop, 1 Web, 2 Android, 3 Server, 4 Xbox. The order is the order of the left list.
inline std::vector<int> PagesFor(int platform) {
    switch (platform) {
        case 3: return {kScenes, kServer, kOutput};
        case 4: return {kScenes, kPlayer, kXbox};
        case 0: return {kScenes, kPlayer, kDisplay, kGraphics, kAudioInput, kGame, kOutput, kInstaller, kAdvanced};
        case 1: return {kScenes, kPlayer, kDisplay, kGraphics, kAudioInput, kOutput, kWeb};
        default: return {kScenes, kPlayer, kDisplay, kGraphics, kAudioInput, kOutput, kAndroid};
    }
}
inline bool PageShown(int page, int platform) {
    const std::vector<int> v = PagesFor(platform);
    return std::find(v.begin(), v.end(), page) != v.end();
}

// ---- the registry of settings ------------------------------------------------------------------------------
template <std::size_t N> bool SameField(const char (&a)[N], const char (&b)[N]) { return std::strcmp(a, b) == 0; }
template <std::size_t N> bool SameField(const float (&a)[N], const float (&b)[N]) { for (std::size_t i = 0; i < N; ++i) if (a[i] != b[i]) return false; return true; }
template <class T> bool SameField(const T& a, const T& b) { return a == b; }
template <std::size_t N> void CopyField(char (&a)[N], const char (&b)[N]) { std::memcpy(a, b, N); }
template <std::size_t N> void CopyField(float (&a)[N], const float (&b)[N]) { for (std::size_t i = 0; i < N; ++i) a[i] = b[i]; }
template <class T> void CopyField(T& a, const T& b) { a = b; }

struct SettingInfo {
    const char* label;     // plain words, as the window shows it
    int         page;
    const char* words;     // extra search words ("fps", "frame rate" ...)
    const char* member;    // the BuildSettings member it stands for (the test checks every member is covered)
    bool (*differs)(const BuildSettings& now, const BuildSettings& defaults);
    void (*reset)(BuildSettings& now, const BuildSettings& defaults);
};

#define OKAY_BS_FIELD(label, page, words, member)                                                         \
    SettingInfo{label, page, words, #member,                                                               \
                [](const BuildSettings& a, const BuildSettings& d) { return !SameField(a.member, d.member); }, \
                [](BuildSettings& a, const BuildSettings& d) { CopyField(a.member, d.member); }}

/// Every setting the window can show, once. (The scene list and the build counter are not "options", so a page
/// reset leaves them alone.) The test checks that every BuildSettings member is listed here or named in
/// `UnlistedMembers`.
inline const std::vector<SettingInfo>& Registry() {
    static const std::vector<SettingInfo> r = {
        OKAY_BS_FIELD("Include all project scenes", kScenes, "scene list levels every", includeAllProjectScenes),
        OKAY_BS_FIELD("Company", kPlayer, "publisher studio author", company),
        OKAY_BS_FIELD("Version", kPlayer, "number release", version),
        OKAY_BS_FIELD("Identifier", kPlayer, "bundle id package", bundleId),
        OKAY_BS_FIELD("Icon", kPlayer, "picture taskbar png", icon),
        OKAY_BS_FIELD("Splash screen", kPlayer, "logo start loading", splash),
        OKAY_BS_FIELD("Splash time", kPlayer, "seconds logo", splashTime),
        OKAY_BS_FIELD("Splash background colour", kPlayer, "color", splashBg),
        OKAY_BS_FIELD("Development build", kPlayer, "debug test panel", developmentBuild),
        OKAY_BS_FIELD("Show FPS counter", kPlayer, "frame rate speed overlay", showFps),
        OKAY_BS_FIELD("Save to user folder", kPlayer, "saves documents appdata", saveToUserDir),
        OKAY_BS_FIELD("Window width", kDisplay, "resolution size", width),
        OKAY_BS_FIELD("Window height", kDisplay, "resolution size", height),
        OKAY_BS_FIELD("Full screen", kDisplay, "window mode", fullscreen),
        OKAY_BS_FIELD("Borderless window", kDisplay, "window mode", borderless),
        OKAY_BS_FIELD("Resizable window", kDisplay, "drag size", resizable),
        OKAY_BS_FIELD("VSync", kDisplay, "tearing monitor refresh", vsync),
        OKAY_BS_FIELD("Smooth frame time", kDisplay, "even steps stutter", smoothDt),
        OKAY_BS_FIELD("Start maximized", kDisplay, "window", startMaximized),
        OKAY_BS_FIELD("Alt+Enter / F11 fullscreen", kDisplay, "toggle keyboard", fullscreenToggle),
        OKAY_BS_FIELD("Frame rate cap", kDisplay, "fps limit", fpsCap),
        OKAY_BS_FIELD("Minimum window width", kDisplay, "size", minWidth),
        OKAY_BS_FIELD("Minimum window height", kDisplay, "size", minHeight),
        OKAY_BS_FIELD("Keep running when not focused", kDisplay, "background pause focus", runInBackground),
        OKAY_BS_FIELD("Mute audio when not focused", kDisplay, "background focus sound", muteOnFocusLoss),
        OKAY_BS_FIELD("Per-pixel lighting", kGraphics, "smooth shading quality", perPixelLighting),
        OKAY_BS_FIELD("Shadows", kGraphics, "quality", shadows),
        OKAY_BS_FIELD("Shadow distance", kGraphics, "reach far", shadowDistance),
        OKAY_BS_FIELD("Shadow softness", kGraphics, "blur edge", shadowSoftness),
        OKAY_BS_FIELD("Shadow cascades", kGraphics, "layers", shadowCascades),
        OKAY_BS_FIELD("Shadow resolution", kGraphics, "texture size sharp", shadowResolution),
        OKAY_BS_FIELD("Bloom", kGraphics, "glow", bloom),
        OKAY_BS_FIELD("Ambient occlusion", kGraphics, "ssao dark corners", ssao),
        OKAY_BS_FIELD("FXAA", kGraphics, "anti-aliasing smooth edges", fxaa),
        OKAY_BS_FIELD("Supersampling", kGraphics, "anti-aliasing smooth edges antialias", antialias),
        OKAY_BS_FIELD("GPU renderer", kGraphics, "direct3d opengl software", gpuRenderer),
        OKAY_BS_FIELD("Prefer Direct3D 12", kGraphics, "dx12 renderer", preferD3D12),
        OKAY_BS_FIELD("3D renderer", kGraphics, "direct3d directx opengl software fallback gpu", renderer),
        OKAY_BS_FIELD("Particle limit", kGraphics, "particles cap effects fewer", particleCap),
        OKAY_BS_FIELD("Gamepad dead zone", kAudioInput, "controller stick drift", gamepadDeadzone),
        OKAY_BS_FIELD("Sound quality (sample rate)", kAudioInput, "audio hz khz output", sampleRate),
        OKAY_BS_FIELD("Files left out of the build", kOutput, "exclude skip ignore assets", excludeList),
        OKAY_BS_FIELD("3D picture size (render scale)", kGraphics, "resolution scale smaller faster pixels", renderScale),
        OKAY_BS_FIELD("Most lights at once", kGraphics, "light limit lamps", maxLights),
        OKAY_BS_FIELD("Switch fog off", kGraphics, "mist haze", fogOff),
        OKAY_BS_FIELD("Mouse sensitivity", kAudioInput, "look speed aim camera", mouseSensitivity),
        OKAY_BS_FIELD("Most sounds at once", kAudioInput, "voices limit audio channels", maxVoices),
        OKAY_BS_FIELD("Crash reports", kGame, "crash handler report dump", crashReports),
        OKAY_BS_FIELD("Crash report folder", kGame, "crash handler where", crashFolder),
        OKAY_BS_FIELD("Command-line arguments", kAdvanced, "args flags start options", launchArgs),
        OKAY_BS_FIELD("Custom defines", kAdvanced, "cvar variables settings values", defines),
        OKAY_BS_FIELD("Web: starting memory", kWeb, "emscripten ram heap", webMemoryMB),
        OKAY_BS_FIELD("Web: compress the files", kWeb, "gzip size download", webCompress),
        OKAY_BS_FIELD("Android: lowest Android version", kAndroid, "min sdk api level", androidMinSdk),
        OKAY_BS_FIELD("Android: target version", kAndroid, "target sdk api level", androidTargetSdk),
        OKAY_BS_FIELD("Android: screen direction", kAndroid, "orientation landscape portrait rotate", androidOrientation),
        OKAY_BS_FIELD("Android: permissions", kAndroid, "internet vibrate manifest", androidPermissions),
        OKAY_BS_FIELD("Create an installer script", kInstaller, "setup nsis exe install", createInstaller),
        OKAY_BS_FIELD("Install folder name", kInstaller, "directory appdata", installFolder),
        OKAY_BS_FIELD("License text file", kInstaller, "licence eula terms", licenseFile),
        OKAY_BS_FIELD("Start menu entry", kInstaller, "shortcut programs", startMenuEntry),
        OKAY_BS_FIELD("Desktop shortcut", kInstaller, "icon", desktopShortcut),
        OKAY_BS_FIELD("Differences for Web", kOutput, "platform override", overridesWeb),
        OKAY_BS_FIELD("Differences for Android", kOutput, "platform override", overridesAndroid),
        OKAY_BS_FIELD("Differences for Server", kOutput, "platform override", overridesServer),
        OKAY_BS_FIELD("Master volume", kAudioInput, "sound loudness", masterVolume),
        OKAY_BS_FIELD("Quit on Escape", kAudioInput, "key exit", quitOnEscape),
        OKAY_BS_FIELD("Hide cursor", kAudioInput, "mouse pointer", hideCursor),
        OKAY_BS_FIELD("Lock cursor on launch", kAudioInput, "mouse capture", lockCursor),
        OKAY_BS_FIELD("Cursor mode", kAudioInput, "mouse locked confined", cursorLockMode),
        OKAY_BS_FIELD("Game speed", kGame, "time scale slow motion", timeScale),
        OKAY_BS_FIELD("Most physics steps per frame", kGame, "fixed update catch up", maxFixedSteps),
        OKAY_BS_FIELD("Music volume", kGame, "sound loudness audio", musicVolume),
        OKAY_BS_FIELD("Sound effects volume", kGame, "sfx loudness audio", sfxVolume),
        OKAY_BS_FIELD("Starting language", kGame, "locale translation default", defaultLanguage),
        OKAY_BS_FIELD("Log detail", kGame, "logging level messages", logLevel),
        OKAY_BS_FIELD("Write a log file", kGame, "logging game.log", logToFile),
        OKAY_BS_FIELD("Description", kGame, "about text", description),
        OKAY_BS_FIELD("Environment variables", kGame, "env settings", envVars),
        OKAY_BS_FIELD("Build mode (folder, compact, zip)", kOutput, "layout archive", layout),
        OKAY_BS_FIELD("Clean the output folder first", kOutput, "wipe stale", cleanOutput),
        OKAY_BS_FIELD("Tidy layout (Data folder)", kOutput, "files root", dataFolder),
        OKAY_BS_FIELD("Encrypt game data", kOutput, "protect scrambled", encryptData),
        OKAY_BS_FIELD("Anti-cheat", kOutput, "cheating protection", antiCheat),
        OKAY_BS_FIELD("Strip unused assets", kOutput, "size smaller leave out", stripAssets),
        OKAY_BS_FIELD("Compress the archive", kOutput, "zip size smaller", compress),
        OKAY_BS_FIELD("Count the build number up", kOutput, "auto increment", autoBuildNumber),
        OKAY_BS_FIELD("Archive name", kOutput, "zip file pattern", archiveName),
        OKAY_BS_FIELD("Open the output folder after", kOutput, "reveal explorer", openFolderAfter),
        OKAY_BS_FIELD("Also create a .zip", kOutput, "archive share", zipAfter),
        OKAY_BS_FIELD("Server platform", kServer, "linux windows", srvTarget),
        OKAY_BS_FIELD("Server name", kServer, "title", srvName),
        OKAY_BS_FIELD("Server port", kServer, "udp network", srvPort),
        OKAY_BS_FIELD("Server max players", kServer, "slots", srvMaxPlayers),
        OKAY_BS_FIELD("Server tick rate", kServer, "hz updates", srvTick),
        OKAY_BS_FIELD("Server password", kServer, "secret", srvPassword),
        OKAY_BS_FIELD("Only signed-in players", kServer, "accounts auth", srvAuth),
        OKAY_BS_FIELD("List in server browsers", kServer, "advertise public", srvAdvertise),
        OKAY_BS_FIELD("Server public address", kServer, "ip host", srvPublicAddr),
        OKAY_BS_FIELD("Server region", kServer, "location", srvRegion),
        OKAY_BS_FIELD("Server max speed", kServer, "anti-cheat movement", srvMaxSpeed),
        OKAY_BS_FIELD("Server message limit", kServer, "rate flood", srvRateLimit),
        OKAY_BS_FIELD("Server kick after", kServer, "rejected moves", srvKickAfter),
        OKAY_BS_FIELD("Allow build commands", kAdvanced, "safety scripts run", allowBuildCommands),
        OKAY_BS_FIELD("Command before the build", kAdvanced, "pre-build script", preBuildCommand),
        OKAY_BS_FIELD("Command after the build", kAdvanced, "post-build script upload", postBuildCommand),
    };
    return r;
}
#undef OKAY_BS_FIELD

/// BuildSettings members that are not in the registry on purpose (they are not options a page reset should touch).
inline const std::vector<std::string>& UnlistedMembers() {
    static const std::vector<std::string> v = {"scenes", "buildNumber"};
    return v;
}

inline int ChangedCount(int page, const BuildSettings& now, const BuildSettings& defaults = BuildSettings{}) {
    int n = 0;
    for (const SettingInfo& s : Registry()) if (s.page == page && s.differs(now, defaults)) ++n;
    return n;
}
/// The dot beside a page in the left list.
inline bool PageChanged(int page, const BuildSettings& now) { return ChangedCount(page, now) > 0; }
inline std::vector<std::string> ChangedLabels(int page, const BuildSettings& now) {
    std::vector<std::string> out;
    const BuildSettings d;
    for (const SettingInfo& s : Registry()) if (s.page == page && s.differs(now, d)) out.push_back(s.label);
    return out;
}
/// "Reset page to defaults": every setting on the page back to the default. Returns how many changed.
inline int ResetPage(int page, BuildSettings& now) {
    const BuildSettings d;
    int n = 0;
    for (const SettingInfo& s : Registry()) if (s.page == page && s.differs(now, d)) { s.reset(now, d); ++n; }
    return n;
}

// ---- per-platform differences ------------------------------------------------------------------------------
/// The setting keys a profile file uses ("width", "vsync", "srvPort" ...), read from the text the profile writer produces,
/// so the list cannot fall behind the struct. Keys that are not settings (product, dir, platform, scene, the overrides) are left out.
inline const std::vector<std::string>& OverrideKeys() {
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> v;
        BuildProfile p; p.name = "x";
        std::ostringstream o; WriteBuildProfileBlock(o, p);
        std::istringstream in(o.str());
        for (std::string line; std::getline(in, line);) {
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos || line[0] == '@') continue;
            const std::string k = line.substr(0, eq);
            if (k == "product" || k == "dir" || k == "platform" || k == "scene" || k.rfind("overrides", 0) == 0) continue;
            v.push_back(k);
        }
        return v;
    }();
    return keys;
}
inline const char* OverrideText(const BuildSettings& s, int platform) {
    return platform == 1 ? s.overridesWeb : platform == 2 ? s.overridesAndroid : platform == 3 ? s.overridesServer : "";
}
/// The settings to build `platform` with: the base settings, then that platform's "key=value;key=value" differences on top.
/// Unknown keys are skipped and listed in `bad`. Desktop (0) and Xbox (4) have no differences: the base settings.
inline BuildSettings EffectiveSettings(const BuildSettings& base, int platform, std::vector<std::string>* bad = nullptr) {
    BuildProfile p; p.settings = base;
    const std::string text = OverrideText(base, platform);
    std::size_t pos = 0;
    const std::vector<std::string>& known = OverrideKeys();
    while (pos <= text.size()) {
        std::size_t e = text.find(';', pos); if (e == std::string::npos) e = text.size();
        std::string it = text.substr(pos, e - pos); pos = e + 1;
        const std::size_t a = it.find_first_not_of(' '); if (a == std::string::npos) continue;
        it = it.substr(a);
        const std::size_t eq = it.find('=');
        std::string k = it.substr(0, eq == std::string::npos ? it.size() : eq);
        while (!k.empty() && k.back() == ' ') k.pop_back();
        if (eq == std::string::npos || std::find(known.begin(), known.end(), k) == known.end()) { if (bad) bad->push_back(k); continue; }
        ApplyBuildProfileKV(p, k, it.substr(eq + 1));
    }
    return p.settings;
}
/// A text that changes whenever any setting changes (the build-size estimate is "out of date" when it differs).
inline std::string SettingsKey(const BuildSettings& s) {
    BuildProfile p; p.settings = s; p.name = "k";
    std::ostringstream o; WriteBuildProfileBlock(o, p);
    return o.str();
}

// ---- search ------------------------------------------------------------------------------------------------
inline std::string Lower(const std::string& s) { std::string o = s; for (char& c : o) c = (char)std::tolower((unsigned char)c); return o; }

/// Registry indexes whose label, search words or page name contain EVERY word of `query` (any case).
/// An empty query matches nothing (the window then shows the page, not results). Pages not shown for
/// `platform` are skipped.
inline std::vector<int> Search(const std::string& query, int platform) {
    std::vector<std::string> words;
    std::istringstream in(Lower(query));
    for (std::string w; in >> w;) words.push_back(w);
    std::vector<int> out;
    if (words.empty()) return out;
    const auto& reg = Registry();
    for (int i = 0; i < (int)reg.size(); ++i) {
        if (!PageShown(reg[i].page, platform)) continue;
        const std::string hay = Lower(std::string(reg[i].label) + " " + reg[i].words + " " + PageName(reg[i].page));
        bool all = true;
        for (const std::string& w : words) if (hay.find(w) == std::string::npos) { all = false; break; }
        if (all) out.push_back(i);
    }
    return out;
}

// ---- presets -----------------------------------------------------------------------------------------------
enum class BuildPreset { Debug, Release };
inline const char* PresetName(BuildPreset p) { return p == BuildPreset::Debug ? "Debug" : "Release"; }
inline const char* PresetTip(BuildPreset p) {
    return p == BuildPreset::Debug
        ? "For testing: the developer panel and FPS counter on, every asset shipped, no encryption, a log file written."
        : "For shipping: no developer panel, only the assets the scenes use, compressed archive, no FPS counter.";
}
/// Only the settings a test build or a shipping build usually differ in change; the window, graphics and name stay.
inline void ApplyBuildPreset(BuildSettings& b, BuildPreset p) {
    if (p == BuildPreset::Debug) {
        b.developmentBuild = true; b.showFps = true; b.stripAssets = false; b.encryptData = false; b.compress = false;
        b.logToFile = true; std::snprintf(b.logLevel, sizeof(b.logLevel), "%s", "info"); b.autoBuildNumber = true;
    } else {
        b.developmentBuild = false; b.showFps = false; b.stripAssets = true; b.compress = true;
        b.logToFile = false; b.logLevel[0] = '\0'; b.autoBuildNumber = true;
    }
}
/// 0 Low, 1 Medium, 2 High, 3 Ultra: the graphics options only. (The window already has a "Quality preset" row with the
/// same four names; this is the same table, kept here so it is tested.)
inline void ApplyQuality(BuildSettings& b, int level) {
    level = std::max(0, std::min(3, level));
    b.shadows = level >= 1;
    b.shadowDistance = level == 0 ? 0.0f : level == 1 ? 40.0f : level == 2 ? 80.0f : 150.0f;
    b.shadowResolution = level <= 1 ? 1024 : level == 2 ? 2048 : 4096;
    b.shadowCascades = level <= 1 ? 2 : level == 2 ? 3 : 4;
    b.shadowSoftness = level <= 1 ? 1.5f : level == 2 ? 2.5f : 4.0f;
    b.perPixelLighting = level >= 2;
    b.fxaa = level >= 1;
    b.antialias = level == 3 ? 2 : 1;
    b.ssao = level >= 3;
    b.bloom = level >= 2;
    b.particleCap = level == 0 ? 100 : level == 1 ? 300 : 0;   // High and Ultra: no extra limit
}

/// Move the item at `from` so it sits at index `to` (the others shift). False if either index is outside the list.
template <class T> bool MoveItem(std::vector<T>& v, int from, int to) {
    if (from < 0 || to < 0 || from >= (int)v.size() || to >= (int)v.size()) return false;
    if (from == to) return true;
    T x = std::move(v[from]);
    v.erase(v.begin() + from);
    v.insert(v.begin() + to, std::move(x));
    return true;
}

// ---- files left out of the build ---------------------------------------------------------------------------------
inline std::vector<std::string> ExcludeEntries(const std::string& list) {
    std::vector<std::string> v; std::size_t pos = 0;
    while (pos <= list.size()) {
        std::size_t e = list.find(';', pos); if (e == std::string::npos) e = list.size();
        std::string it = list.substr(pos, e - pos); pos = e + 1;
        for (char& c : it) if (c == '\\') c = '/';
        const std::size_t a = it.find_first_not_of(' '), z = it.find_last_not_of(' ');
        if (a == std::string::npos) continue;
        v.push_back(it.substr(a, z - a + 1));
    }
    return v;
}
/// Is the project-relative path `rel` ("Assets/Textures/a.png") on the list? An entry ending in / covers a whole folder. Not case sensitive.
inline bool IsExcluded(const std::string& rel, const std::string& list) {
    std::string r = Lower(rel); for (char& c : r) if (c == '\\') c = '/';
    for (const std::string& e0 : ExcludeEntries(list)) {
        const std::string e = Lower(e0);
        if (e.back() == '/') { if (r.compare(0, e.size(), e) == 0) return true; }
        else if (r == e) return true;
    }
    return false;
}
/// Add `rel` to the list, or take it off when it is already there. Returns the new list text (cut at `cap` - 1 characters; "" - and `ok` false - if it would not fit).
inline std::string ToggleExcluded(const std::string& list, const std::string& rel, std::size_t cap, bool* ok = nullptr) {
    std::vector<std::string> es = ExcludeEntries(list);
    std::string r = rel; for (char& c : r) if (c == '\\') c = '/';
    const auto it = std::find_if(es.begin(), es.end(), [&](const std::string& e) { return Lower(e) == Lower(r); });
    if (it != es.end()) es.erase(it); else es.push_back(r);
    std::string out;
    for (const std::string& e : es) out += (out.empty() ? "" : ";") + e;
    const bool fits = out.size() < cap;
    if (ok) *ok = fits;
    return fits ? out : list;
}

// ---- summary: what will happen, in plain words -------------------------------------------------------------
struct SummaryInput {
    BuildSettings s;
    std::string productName, outputDir;
    int platform = 0;
    int enabledScenes = 0;        // scenes ticked in the list (0 = only the open scene)
    std::uintmax_t lastBytes = 0; // size of the last build (0 = never built), from the history
    std::uintmax_t estimateBytes = 0; // size a Dry Run of these settings reported (0 = no dry run yet)
    bool estimateFresh = true;        // the settings are the ones that Dry Run used
};
inline std::string HumanSize(std::uintmax_t b) {
    char t[40]; double v = (double)b;
    if (v >= 1024.0 * 1024 * 1024) std::snprintf(t, sizeof t, "%.1f GB", v / (1024.0 * 1024 * 1024));
    else if (v >= 1024.0 * 1024)   std::snprintf(t, sizeof t, "%.1f MB", v / (1024.0 * 1024));
    else if (v >= 1024.0)          std::snprintf(t, sizeof t, "%.0f KB", v / 1024.0);
    else std::snprintf(t, sizeof t, "%.0f bytes", v);
    return t;
}
inline std::vector<std::string> BuildSummary(const SummaryInput& in) {
    const BuildSettings& s = in.s;
    std::vector<std::string> L;
    const std::string name = in.productName.empty() ? "(no name yet)" : in.productName;
    if (in.platform == 3) {
        L.push_back("Makes a game server for " + std::string(s.srvTarget == 0 ? "Linux" : "Windows") + " called " + (s.srvName[0] ? std::string(s.srvName) : name + " Server") +
                    ", on port " + std::to_string(s.srvPort) + " for up to " + std::to_string(s.srvMaxPlayers) + " players.");
    } else if (in.platform != 0) {
        L.push_back(std::string("Exports the game data and a project for ") + (in.platform == 1 ? "the web" : in.platform == 2 ? "Android" : "Xbox") + ". You finish the build with that platform's own tools.");
    } else {
        L.push_back("Makes " + name + " version " + (s.version[0] ? s.version : "1.0.0") + " (build " + std::to_string(s.buildNumber) + ") for Windows.");
        const char* mode = s.fullscreen ? "full screen" : s.borderless ? "a borderless window" : "a window";
        L.push_back("The game opens in " + std::string(mode) + " at " + std::to_string(s.width) + " x " + std::to_string(s.height) + (s.resizable ? ", and the player can resize it." : "."));
    }
    if (in.enabledScenes > 0) L.push_back(std::to_string(in.enabledScenes) + (in.enabledScenes == 1 ? " scene is" : " scenes are") + " included; the first one starts the game.");
    else if (s.includeAllProjectScenes) L.push_back("Every scene in the project is included.");
    else L.push_back("Only the scene you have open is included.");
    if (in.platform == 0) {
        L.push_back(s.stripAssets ? "Only the files the scenes use are copied, which keeps the game small." : "Every project file is copied, even ones no scene uses.");
        const char* lay[] = {"a folder", "a compact folder", "one zip file"};
        L.push_back(std::string("The result is ") + lay[std::max(0, std::min(2, s.layout))] + (s.zipAfter && s.layout != 2 ? " and a .zip beside it" : "") +
                    (s.encryptData ? ", with the game data scrambled so it cannot be read or edited." : "."));
        if (s.developmentBuild) L.push_back("This is a development build: it shows a developer badge and has the F1 test panel.");
        if (s.timeScale != 1.0f) L.push_back("The game runs at " + FloatText(s.timeScale) + " times normal speed.");
        if (s.defaultLanguage[0]) L.push_back(std::string("The first time it runs, the game uses the language \"") + s.defaultLanguage + "\".");
        if (s.logToFile || s.logLevel[0]) L.push_back(std::string("The game writes ") + (s.logToFile ? "a log file (game.log) in its save folder" : "log messages") + (s.logLevel[0] ? std::string(" with detail level ") + s.logLevel : std::string()) + ".");
        if (s.envVars[0]) L.push_back("It sets environment variables before it starts.");
        if (s.allowBuildCommands && (s.preBuildCommand[0] || s.postBuildCommand[0])) {
            if (s.preBuildCommand[0]) L.push_back("Before building it runs your command: " + std::string(s.preBuildCommand));
            if (s.postBuildCommand[0]) L.push_back("After a good build it runs your command: " + std::string(s.postBuildCommand));
        }
    }
    L.push_back(in.outputDir.empty() ? "No output folder is chosen yet." : "It is written to " + in.outputDir + ".");
    if (in.estimateBytes) L.push_back(in.estimateFresh ? "Estimated size of this build (from a Dry Run of these exact settings): " + HumanSize(in.estimateBytes) + "."
                                                       : "The last Dry Run said " + HumanSize(in.estimateBytes) + ", but settings have changed since: press Dry Run for a new estimate.");
    if (in.platform == 0) {
        if (s.renderScale != 1.0f) L.push_back("The 3D picture is drawn at " + std::to_string((int)(s.renderScale * 100 + 0.5f)) + "% size and stretched to the window.");
        if (s.maxLights > 0) L.push_back("At most " + std::to_string(s.maxLights) + " lights are used at once.");
        if (s.mouseSensitivity != 1.0f) L.push_back("Mouse look runs at " + FloatText(s.mouseSensitivity) + " times the usual speed.");
        if (s.crashReports) L.push_back("If the game crashes it writes a crash report" + (s.crashFolder[0] ? std::string(" into the folder \"") + s.crashFolder + "\" in its save folder." : std::string(" beside its files.")));
        if (s.launchArgs[0]) L.push_back(std::string("It starts with these command-line arguments: ") + s.launchArgs);
        if (s.createInstaller && s.layout != 2) L.push_back("An installer script (.nsi) is written next to the build. Run NSIS on it to make the setup program.");
    }
    if (in.platform == 1 && (s.webMemoryMB > 0 || s.webCompress)) L.push_back(std::string("The web build script ") + (s.webMemoryMB > 0 ? "asks for " + std::to_string(s.webMemoryMB) + " MB of memory" : std::string("keeps the default memory")) + (s.webCompress ? " and writes .gz copies." : "."));
    if (in.platform == 2) L.push_back(std::string("An AndroidManifest.xml and a Gradle file are written for the package ") + s.bundleId + ".");
    if (!ExcludeEntries(s.excludeList).empty()) L.push_back(std::to_string(ExcludeEntries(s.excludeList).size()) + " file or folder entries are left out of the build (Output page).");
    if (in.lastBytes) L.push_back("The last build was " + HumanSize(in.lastBytes) + ". Use Dry Run to see the size of this one without writing it.");
    return L;
}

// ---- validation --------------------------------------------------------------------------------------------
enum class Severity { Info, Warning, Error };
enum class Fix { ClearInstaller, ClearLicense, LowerMinSdk, None, CleanName, DefaultVersion, DropBorderless, RaiseMinSize, ClearIcon, ClearSplash, ResetTimeScale, ClearZipAlso, ClearLogLevel, DropBadEnv, RaiseSize };
struct Issue { Severity sev = Severity::Warning; std::string text; Fix fix = Fix::None; const char* fixLabel = ""; int page = -1; };
struct ValidateInput {
    BuildSettings s;
    std::string productName, outputDir;
    int platform = 0;
    int enabledScenes = 0;
    bool openSceneSaved = true;     // the open scene has a file (else "only the open scene" has nothing to ship)
    bool iconMissing = false;       // an icon is set but the file is not there
    bool splashMissing = false;
    bool licenseMissing = false;     // a licence file is set but is not there
};
inline bool NameHasBadChars(const std::string& n) {
    for (char c : n) if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' || (unsigned char)c < 32) return true;
    return false;
}
inline std::string CleanName(const std::string& n) {
    std::string o;
    for (char c : n) o.push_back(NameHasBadChars(std::string(1, c)) ? '_' : c);
    while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
    return o;
}
inline bool LooksLikeVersion(const char* v) {
    int dots = 0; bool digit = false;
    for (const char* p = v; *p; ++p) {
        if (*p == '.') { if (!digit) return false; ++dots; digit = false; }
        else if (*p >= '0' && *p <= '9') digit = true;
        else return false;
    }
    return digit && dots >= 1 && dots <= 3;
}
inline std::vector<Issue> Validate(const ValidateInput& in) {
    const BuildSettings& s = in.s;
    std::vector<Issue> v;
    auto add = [&](Severity sev, int page, const std::string& t, Fix f = Fix::None, const char* fl = "") { v.push_back(Issue{sev, t, f, fl, page}); };
    if (in.productName.empty()) add(Severity::Error, kPlayer, "The game has no name. Type one on the Player page.");
    else if (NameHasBadChars(in.productName)) add(Severity::Error, kPlayer, "The name has characters Windows files cannot have ( < > : \" / \\ | ? * ). They are replaced by _ in file names.", Fix::CleanName, "Clean the name");
    if (in.outputDir.empty()) add(Severity::Error, kOutput, "No output folder is chosen. Pick one on the Output page.");
    if (in.platform == 3 || in.platform == 4) return v;
    if (in.enabledScenes == 0 && !s.includeAllProjectScenes && !in.openSceneSaved)
        add(Severity::Error, kScenes, "There are no scenes to ship. Save the open scene, or tick scenes on the Scenes page.");
    if (!s.version[0] || !LooksLikeVersion(s.version)) add(Severity::Warning, kPlayer, "The version is not in the form 1.2.3.", Fix::DefaultVersion, "Use 1.0.0");
    if (in.iconMissing) add(Severity::Warning, kPlayer, "The icon file cannot be found, so the game gets the default icon.", Fix::ClearIcon, "Forget the icon");
    else if (!s.icon[0] && in.platform == 0) add(Severity::Info, kPlayer, "No icon is set, so the game gets the default icon.");
    if (in.splashMissing) add(Severity::Warning, kPlayer, "The splash image cannot be found.", Fix::ClearSplash, "Forget the splash");
    if (s.width < 640 || s.height < 360) add(Severity::Warning, kDisplay, "The window is very small (" + std::to_string(s.width) + " x " + std::to_string(s.height) + "). Most games need at least 640 x 360.", Fix::RaiseSize, "Use 1280 x 720");
    if (s.fullscreen && s.borderless) add(Severity::Warning, kDisplay, "Full screen and borderless window are both on. Full screen wins.", Fix::DropBorderless, "Turn borderless off");
    if ((s.minWidth > s.width || s.minHeight > s.height) && s.minWidth > 0) add(Severity::Warning, kDisplay, "The smallest window size is bigger than the window itself.", Fix::RaiseMinSize, "Lower the minimum");
    if (s.timeScale <= 0.0f) add(Severity::Warning, kGame, "Game speed is 0, so the game starts frozen.", Fix::ResetTimeScale, "Use normal speed");
    if (s.masterVolume <= 0.0f) add(Severity::Info, kAudioInput, "Master volume is 0, so the game starts silent.");
    if (s.logLevel[0] && okay::buildpipe::NormalizeLogLevel(s.logLevel).empty()) add(Severity::Warning, kGame, "The log detail is not one of trace, info, warn, error, so it is ignored.", Fix::ClearLogLevel, "Clear it");
    if (s.envVars[0]) {
        std::vector<std::string> bad; okay::buildpipe::ParseEnvList(s.envVars, &bad);
        if (!bad.empty()) add(Severity::Warning, kGame, "These environment variables are not allowed or not written NAME=VALUE and are skipped: " + bad.front() + (bad.size() > 1 ? " and others." : "."), Fix::DropBadEnv, "Remove them");
    }
    if (s.createInstaller && s.layout == 2) add(Severity::Warning, kInstaller, "An installer needs the folder layout, and the Zip layout keeps only the zip. No installer script is written.", Fix::ClearInstaller, "Turn the installer off");
    if (s.licenseFile[0] && in.licenseMissing) add(Severity::Warning, kInstaller, "The licence file cannot be found, so the installer will have no licence page.", Fix::ClearLicense, "Forget the licence");
    if (s.androidMinSdk > s.androidTargetSdk) add(Severity::Warning, kAndroid, "The lowest Android version is higher than the target version.", Fix::LowerMinSdk, "Make them equal");
    {
        std::vector<std::string> bad;
        for (int pf = 1; pf <= 3; ++pf) { EffectiveSettings(s, pf, &bad); }
        if (!bad.empty()) add(Severity::Warning, kOutput, "A \"Differences for\" box has a setting name that does not exist (" + bad.front() + "). It is ignored.");
    }
    if (s.defines[0]) { std::vector<std::string> bad; okay::buildpipe::ParseDefines(s.defines, &bad); if (!bad.empty()) add(Severity::Warning, kAdvanced, "A custom define is not written NAME=VALUE (" + bad.front() + "). It is skipped."); }
    if (s.layout == 2 && s.zipAfter) add(Severity::Info, kOutput, "The Zip layout already makes a .zip, so \"Also create a .zip\" does nothing.", Fix::ClearZipAlso, "Turn it off");
    if ((s.preBuildCommand[0] || s.postBuildCommand[0]) && !s.allowBuildCommands)
        add(Severity::Info, kAdvanced, "You wrote build commands but \"Allow build commands\" is off, so they will not run.");
    if (s.developmentBuild && s.encryptData) add(Severity::Info, kOutput, "A development build with encrypted data is hard to debug. Consider turning one off.");
    return v;
}
/// Apply one Fix. Returns false if there was nothing to fix.
inline bool ApplyFix(Fix f, BuildSettings& s, std::string& productName) {
    switch (f) {
        case Fix::None: return false;
        case Fix::CleanName: productName = CleanName(productName); return true;
        case Fix::DefaultVersion: std::snprintf(s.version, sizeof s.version, "%s", "1.0.0"); return true;
        case Fix::DropBorderless: s.borderless = false; return true;
        case Fix::RaiseMinSize: s.minWidth = std::min(s.minWidth, s.width); s.minHeight = std::min(s.minHeight, s.height); return true;
        case Fix::ClearIcon: s.icon[0] = '\0'; return true;
        case Fix::ClearSplash: s.splash[0] = '\0'; return true;
        case Fix::ResetTimeScale: s.timeScale = 1.0f; return true;
        case Fix::ClearZipAlso: s.zipAfter = false; return true;
        case Fix::ClearLogLevel: s.logLevel[0] = '\0'; return true;
        case Fix::ClearInstaller: s.createInstaller = false; return true;
        case Fix::ClearLicense: s.licenseFile[0] = '\0'; return true;
        case Fix::LowerMinSdk: s.androidMinSdk = s.androidTargetSdk; return true;
        case Fix::RaiseSize: s.width = 1280; s.height = 720; return true;
        case Fix::DropBadEnv: {
            std::string keep;
            for (const auto& kv : okay::buildpipe::ParseEnvList(s.envVars)) keep += (keep.empty() ? "" : ";") + kv.first + "=" + kv.second;
            std::snprintf(s.envVars, sizeof s.envVars, "%s", keep.c_str());
            return true;
        }
    }
    return false;
}
inline bool HasErrors(const std::vector<Issue>& v) { for (const Issue& i : v) if (i.sev == Severity::Error) return true; return false; }

// ---- the settings as the game.okayconfig extra lines --------------------------------------------------------
inline okay::buildpipe::ExtraConfig ExtraFrom(const BuildSettings& s) {
    okay::buildpipe::ExtraConfig c;
    c.timeScale = s.timeScale; c.maxFixedSteps = s.maxFixedSteps; c.musicVolume = s.musicVolume; c.sfxVolume = s.sfxVolume;
    c.defaultLanguage = s.defaultLanguage;
    c.logLevel = okay::buildpipe::NormalizeLogLevel(s.logLevel);
    c.logFile = s.logToFile ? "game.log" : "";
    for (const char* p = s.description; *p; ++p) c.description.push_back(*p == '\n' || *p == '\r' ? ' ' : *p);
    c.env = okay::buildpipe::ParseEnvList(s.envVars);
    c.renderScale = s.renderScale; c.maxLights = s.maxLights; c.fogOff = s.fogOff; c.mouseSensitivity = s.mouseSensitivity; c.maxVoices = s.maxVoices;
    c.crashReports = s.crashReports; c.crashFolder = okay::buildpipe::CleanLogFileName(s.crashFolder);
    { std::string a = s.launchArgs; for (char& ch : a) if (ch == '\n' || ch == '\r') ch = ' '; c.args = okay::buildpipe::SplitArgs(a); }
    c.defines = okay::buildpipe::ParseDefines(s.defines);
    c.renderer = s.renderer; c.particleCap = s.particleCap; c.gamepadDeadzone = s.gamepadDeadzone; c.sampleRate = s.sampleRate;
    return c;
}

// ---- build history -----------------------------------------------------------------------------------------
struct HistoryEntry {
    std::string time;        // "2026-11-19 14:03" (the caller's clock)
    std::string version;
    int         buildNumber = 0;
    std::string platform;    // "Desktop" ...
    std::uintmax_t bytes = 0;
    std::string output;
    bool        ok = true;
};
inline std::string HistoryClean(std::string s) { for (char& c : s) if (c == '|' || c == '\n' || c == '\r') c = ' '; return s; }
inline std::string WriteHistory(const std::vector<HistoryEntry>& h) {
    std::ostringstream o;
    o << "# OkaySpace build history (newest last)\n";
    for (const HistoryEntry& e : h)
        o << HistoryClean(e.time) << "|" << HistoryClean(e.version) << "|" << e.buildNumber << "|" << HistoryClean(e.platform) << "|" << e.bytes << "|" << (e.ok ? 1 : 0) << "|" << HistoryClean(e.output) << "\n";
    return o.str();
}
inline std::vector<HistoryEntry> ParseHistory(const std::string& text) {
    std::vector<HistoryEntry> out;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f; std::size_t pos = 0;
        while (f.size() < 6) { const std::size_t e = line.find('|', pos); if (e == std::string::npos) break; f.push_back(line.substr(pos, e - pos)); pos = e + 1; }
        if (f.size() < 6) continue;
        HistoryEntry e;
        e.time = f[0]; e.version = f[1]; e.buildNumber = std::atoi(f[2].c_str()); e.platform = f[3];
        e.bytes = (std::uintmax_t)std::strtoull(f[4].c_str(), nullptr, 10); e.ok = f[5] == "1"; e.output = line.substr(pos);
        out.push_back(e);
    }
    return out;
}
/// Add one finished build and keep only the newest `keep`.
inline std::vector<HistoryEntry> AddToHistory(std::vector<HistoryEntry> h, const HistoryEntry& e, std::size_t keep = 30) {
    h.push_back(e);
    if (h.size() > keep) h.erase(h.begin(), h.begin() + (std::ptrdiff_t)(h.size() - keep));
    return h;
}
inline std::uintmax_t LastGoodSize(const std::vector<HistoryEntry>& h) {
    for (auto it = h.rbegin(); it != h.rend(); ++it) if (it->ok && it->bytes) return it->bytes;
    return 0;
}

// ---- the build log: which lines are errors --------------------------------------------------------------------
enum class LogKind { Normal, Good, Warning, Error };
inline LogKind ClassifyLogLine(const std::string& line) {
    const std::string l = Lower(line);
    auto has = [&](const char* w) { return l.find(w) != std::string::npos; };
    if (has("failed") || has("error") || has("refusing") || has("couldn't") || has("could not") || has("cannot") || has("nothing was built") || has("stopped")) return LogKind::Error;
    if (has("warning") || has("not found") || has("left out of the build but") || has("missing")) return LogKind::Warning;
    if (has("built '") || has("ran your command") || has("dry run for")) return LogKind::Good;
    return LogKind::Normal;
}
/// The lines of the last build's log: the one-paragraph result split at its two-space separators, then each warning.
inline std::vector<std::string> BuildLogLines(const std::string& status, const std::vector<std::string>& warnings) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos < status.size()) {
        std::size_t e = status.find("  ", pos);
        if (e == std::string::npos) e = status.size();
        std::string part = status.substr(pos, e - pos);
        pos = e + 2;
        const std::size_t a = part.find_first_not_of(' ');
        if (a != std::string::npos) out.push_back(part.substr(a));
    }
    for (const std::string& w : warnings) out.push_back("Warning: " + w);
    return out;
}

// ---- build commands ----------------------------------------------------------------------------------------
struct CommandVars { std::string output, name, version, build, project; };
/// Fill {output} {name} {version} {build} {project} into the command text. Unknown {words} stay as they are.
inline std::string ExpandCommand(const std::string& cmd, const CommandVars& v) {
    std::string out;
    for (std::size_t i = 0; i < cmd.size();) {
        if (cmd[i] == '{') {
            const std::size_t e = cmd.find('}', i);
            if (e != std::string::npos) {
                const std::string k = cmd.substr(i + 1, e - i - 1);
                const std::string* val = k == "output" ? &v.output : k == "name" ? &v.name : k == "version" ? &v.version : k == "build" ? &v.build : k == "project" ? &v.project : nullptr;
                if (val) { out += *val; i = e + 1; continue; }
            }
        }
        out.push_back(cmd[i++]);
    }
    return out;
}
/// Run a command line through the shell, in `workDir` when given. Returns the program's exit code (-1 if it could not start).
inline int RunCommandLine(const std::string& cmd, const std::string& workDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path old = fs::current_path(ec);
    bool moved = false;
    if (!workDir.empty()) { fs::current_path(fs::path(workDir), ec); moved = !ec; }
    const int raw = std::system(cmd.c_str());
    if (moved) { std::error_code e2; fs::current_path(old, e2); }
    if (raw == -1) return -1;
#if defined(_WIN32)
    return raw;
#else
    return WIFEXITED(raw) ? WEXITSTATUS(raw) : -1;
#endif
}
enum class CommandStage { Before, After };
/// What the build step does with a command: "" = nothing to do (off, empty, or a dry run); else the expanded text.
inline std::string CommandToRun(const std::string& cmd, bool allowed, bool dryRun, const CommandVars& v) {
    if (!allowed || dryRun || cmd.empty()) return "";
    std::size_t a = cmd.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    return ExpandCommand(cmd, v);
}

// ---- version control file ----------------------------------------------------------------------------------
/// The paths a project keeps out of version control (relative to the project folder): editor scratch files and, when the
/// output folder is inside the project, the build output.
inline std::vector<std::string> IgnoreLines(const std::string& outputDirRelativeToProject) {
    std::vector<std::string> v = {".okay/build_live.cfg", ".okay/build_history.txt", "*.autosave", "*.log", "Thumbs.db", ".DS_Store"};
    std::string o = outputDirRelativeToProject;
    for (char& c : o) if (c == '\\') c = '/';
    while (!o.empty() && o.back() == '/') o.pop_back();
    if (!o.empty() && o.rfind("..", 0) != 0 && o.find(':') == std::string::npos && o[0] != '/') v.push_back(o + "/");
    return v;
}
/// `existing` plus the wanted lines that it does not have yet (nothing is removed or reordered). `added` = how many were added.
inline std::string MergeIgnore(const std::string& existing, const std::vector<std::string>& wanted, int* added = nullptr) {
    std::vector<std::string> have;
    std::istringstream in(existing);
    for (std::string l; std::getline(in, l);) { while (!l.empty() && l.back() == '\r') l.pop_back(); have.push_back(l); }
    std::string out = existing;
    int n = 0;
    for (const std::string& w : wanted) {
        if (std::find(have.begin(), have.end(), w) != have.end()) continue;
        if (n == 0) { if (!out.empty() && out.back() != '\n') out += "\n"; out += "# added by OkaySpace Build Settings\n"; }
        out += w + "\n"; ++n;
    }
    if (added) *added = n;
    return out;
}

// ---- profiles as .okaybuild files ----------------------------------------------------------------------------
/// A file name for a profile: letters, digits, space, dash, underscore, dot; "profile" if nothing is left.
inline std::string ProfileFileStem(const std::string& name) {
    std::string o;
    for (char c : name) if (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_' || c == '.') o.push_back(c);
    while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
    while (!o.empty() && (o.front() == ' ' || o.front() == '.')) o.erase(o.begin());
    return o.empty() ? "profile" : o;
}
/// "Name copy", "Name copy 2", ... the first one no profile has.
inline std::string UniqueCopyName(const std::vector<BuildProfile>& list, const std::string& base) {
    auto taken = [&](const std::string& n) { for (const auto& p : list) if (p.name == n) return true; return false; };
    std::string n = SanitizeProfileName(base) + " copy";
    for (int i = 2; taken(n); ++i) n = SanitizeProfileName(base) + " copy " + std::to_string(i);
    return n;
}
/// Duplicate profile `idx` at the end of the list. Returns the new index, or -1.
inline int DuplicateProfile(std::vector<BuildProfile>& list, int idx) {
    if (idx < 0 || idx >= (int)list.size()) return -1;
    BuildProfile p = list[idx];
    p.name = UniqueCopyName(list, list[idx].name);
    list.push_back(p);
    return (int)list.size() - 1;
}
/// The text of one .okaybuild file (the same text the project's profile list uses, with one profile in it).
inline std::string WriteProfileFile(const BuildProfile& p) { return WriteBuildProfiles({p}); }
/// The first profile in a .okaybuild file; false if there is none.
inline bool ReadProfileFile(const std::string& text, BuildProfile& out) {
    const std::vector<BuildProfile> v = ParseBuildProfiles(text);
    if (v.empty()) return false;
    out = v.front();
    return true;
}

// ---- the history file and the profile folder in a project --------------------------------------------------------
inline std::string HistoryFile(const std::string& projectDir) { return (std::filesystem::path(projectDir) / ".okay" / "build_history.txt").string(); }
inline std::string ProfileFolder(const std::string& projectDir) { return (std::filesystem::path(projectDir) / "BuildProfiles").string(); }
inline std::vector<HistoryEntry> LoadHistory(const std::string& projectDir) {
    std::string text;
    if (projectDir.empty() || !okayproj::ReadText(HistoryFile(projectDir), text)) return {};
    return ParseHistory(text);
}
/// Add one build to the project's history file. Returns false (and says why) if the file could not be written.
inline bool RecordBuild(const std::string& projectDir, const HistoryEntry& e, std::string* error = nullptr) {
    if (projectDir.empty()) { if (error) *error = "no project is open"; return false; }
    return okayproj::WriteTextAtomic(HistoryFile(projectDir), WriteHistory(AddToHistory(LoadHistory(projectDir), e)), error);
}
/// "2026-11-19 14:03" in the computer's local time.
inline std::string NowText() {
    const std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tmv);
    return buf;
}
/// The *.okaybuild files in the project's BuildProfiles folder (file names only, sorted).
inline std::vector<std::string> ListProfileFiles(const std::string& projectDir) {
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    if (projectDir.empty()) return out;
    for (const auto& e : fs::directory_iterator(fs::path(ProfileFolder(projectDir)), ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".okaybuild") out.push_back(e.path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}
inline bool SaveProfileFile(const std::string& projectDir, const BuildProfile& p, std::string* error = nullptr) {
    namespace fs = std::filesystem;
    if (projectDir.empty()) { if (error) *error = "no project is open"; return false; }
    std::error_code ec;
    fs::create_directories(fs::path(ProfileFolder(projectDir)), ec);
    return okayproj::WriteTextAtomic((fs::path(ProfileFolder(projectDir)) / (ProfileFileStem(p.name) + ".okaybuild")).string(), WriteProfileFile(p), error);
}
inline bool LoadProfileFile(const std::string& projectDir, const std::string& fileName, BuildProfile& out) {
    namespace fs = std::filesystem;
    if (projectDir.empty() || fileName.find('/') != std::string::npos || fileName.find('\\') != std::string::npos || fileName.find("..") != std::string::npos) return false;
    std::string text;
    return okayproj::ReadText((fs::path(ProfileFolder(projectDir)) / fs::path(fileName)).string(), text) && ReadProfileFile(text, out);
}

}  // namespace oked::bsm
