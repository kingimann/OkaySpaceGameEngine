#pragma once
// The project's own state, as the files inside the project folder hold it (no UI, no globals, so the
// editor and the tests read and write exactly the same text):
//
//   <project>/project.okayproj           ProjectSettings + the tag list and layer names (ProjectLists)
//   <project>/.okay/build_profiles.cfg   the named build profiles (Build Settings > profiles)
//   <project>/.okay/build_live.cfg       the Build Settings dialog as it was left (profile or not)
//   <project>/.okay/scene_build.txt      Scenes In Build (the Scene Manager)
//
// Every value is written so it reads back to the same number (shortest text that round trips); older
// files, which wrote six digits, still load. Unknown lines are ignored, so a file written by a newer
// editor loads in an older one.
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>
#include "okay/Scene/ProjectLists.hpp"
#include "../shared/ProjectCreate.hpp"

namespace oked {

/// The shortest text that reads back to exactly this float.
inline std::string FloatText(float v) {
    char buf[40];
    auto r = std::to_chars(buf, buf + sizeof(buf), v);
    return r.ec == std::errc() ? std::string(buf, r.ptr) : std::to_string(v);
}
inline float ParseFloatText(const std::string& s, float def = 0.0f) {
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    return end == s.c_str() ? def : v;
}

// One entry of "Scenes In Build": a project scene, ticked to ship. The first
// ticked scene is where the game starts; the rest load by index / name.
struct BuildScene { std::string path; bool on = true; };

// Build Settings: window + player options written into the build's
// game.okayconfig, plus which scenes to include.
struct BuildSettings {
    char  company[128] = "OkaySpace";
    char  version[32]  = "1.0.0";
    int   width = 1280, height = 720;
    bool  fullscreen = false;
    bool  borderless = false;
    bool  resizable = true;
    bool  vsync = true;
    bool  smoothDt = true;                 // even frame steps for movement and cameras (the game's smooth_dt)
    bool  hideCursor = false;
    int   fpsCap = 0;                      // 0 = uncapped (vsync paces)
    bool  quitOnEscape = true;
    float masterVolume = 1.0f;
    bool  showFps = false;
    bool  includeAllProjectScenes = false; // else just the current scene
    bool  developmentBuild = false;
    bool  cleanOutput = true;              // wipe stale build files first
    bool  dataFolder = true;               // clean Data/ layout (exe at root)
    bool  encryptData = false;             // obfuscate scenes + config
    int   antiCheat = 0;                   // 0 off, 1 report, 2 enforce (docs/anticheat.md)
    // ---- Player presentation ----
    char  icon[260] = "";                  // window/taskbar icon PNG
    char  bundleId[160] = "com.okayspace.game";  // application identifier
    bool  runInBackground = true;          // keep updating when the window loses focus
    bool  muteOnFocusLoss = false;         // silence audio when unfocused
    bool  startMaximized = false;          // open maximized
    int   minWidth = 0, minHeight = 0;     // minimum window size (0 = none)
    bool  saveToUserDir = true;            // saves to per-user app folder (persistentDataPath)
    char  splash[260] = "";                // startup logo PNG
    float splashTime = 2.0f;
    float splashBg[3] = {0.05f, 0.05f, 0.06f};
    // ---- Graphics / quality (applied by the player at startup) ----
    bool  lockCursor = false;              // hide + lock the cursor on launch
    int   cursorLockMode = 0;              // 0 none, 1 locked, 2 confined (player launch)
    bool  perPixelLighting = false;        // smooth per-pixel shading (slower)
    bool  shadows = true;                  // directional cast shadows (on = modern look)
    float shadowDistance = 80.0f;          // cascaded shadow reach (0 = legacy whole-scene)
    float shadowSoftness = 2.5f;           // PCF penumbra width (texels)
    int   shadowCascades = 3;              // number of cascades
    int   shadowResolution = 2048;         // texels per cascade
    bool  bloom = false;                   // glow on bright/emissive areas
    bool  ssao = false;                    // ambient occlusion
    bool  fxaa = true;                     // cheap edge anti-aliasing
    int   antialias = 1;                   // 1 = off, 2 = 2x supersample
    bool  gpuRenderer = true;              // use the GPU (D3D11/OpenGL) 3D renderer; falls back to software
    bool  preferD3D12 = false;             // opt-in: prefer the Direct3D 12 backend (Windows)
    // ---- Post-build actions (handled by the editor, not the builder) ----
    bool  openFolderAfter = true;          // reveal the output folder when done
    bool  zipAfter = false;                // also produce <Name>.zip of the build
    // ---- Output layout, stripping, versioning (docs/build_pipeline.md) ----
    int   layout = 0;                      // 0 Folder (<Name>.exe + <Name>_Data/), 1 Compact (exe + Data/), 2 Zip archive
    bool  stripAssets = true;              // ship only the assets the built scenes use (off: every project asset)
    bool  compress = true;                 // zip: deflate files that shrink (off: store)
    int   buildNumber = 1;                 // written to build.json and the game; the editor adds 1 after each build
    bool  autoBuildNumber = true;          // count the build number up after every successful build
    char  archiveName[96] = "{name}-{version}-{platform}";   // zip file name; {name} {version} {build} {platform} {date}
    // ---- Added later ----
    bool  fullscreenToggle = true;         // Alt+Enter / F11 switches fullscreen in the game
    std::vector<BuildScene> scenes;        // Scenes In Build (empty = just the open scene)
    // ---- Dedicated server (platform "Server") -> the server folder's server.cfg ----
    int   srvTarget = 0;                   // 0 = Linux, 1 = Windows
    char  srvName[96] = "";                // "" = "<Product> Server"
    int   srvPort = 45000;
    int   srvMaxPlayers = 16;
    int   srvTick = 30;
    char  srvPassword[64] = "";
    bool  srvAuth = false;                 // only signed-in OkaySpace accounts
    bool  srvAdvertise = false;            // list in the server browsers
    char  srvPublicAddr[128] = "";         // the address players connect to
    char  srvRegion[32] = "";
    float srvMaxSpeed = 0.0f;              // anti-cheat: units/second (0 = off)
    int   srvRateLimit = 0;                // messages per second per player (0 = off)
    int   srvKickAfter = 0;                // rejected moves per minute before a kick (0 = never)
};
// ---- Build profiles -------------
// Persist the full Build Settings under named profiles in the project's
// .okay/build_profiles.cfg, so "Dev" / "Release" / "Demo" setups can be kept
// and switched. Also remembers settings across editor restarts.
struct BuildProfile {
    std::string   name;
    BuildSettings settings;
    std::string   productName;
    std::string   outputDir;
    int           platform = 0;
};
// Serialize one profile as an "@profile <name>" block of key=value lines.
// The name and the free-text values must survive the line format: a name is one
// sanitized word (control characters and '=' stripped, so it cannot start or
// forge a key, or inject a new "@profile" block); values keep their characters
// and are escaped (\n becomes \\n, '=' becomes \\=, '|' becomes \\|) so the
// first '=' still splits the key and '|' still separates scene entries.
inline std::string SanitizeProfileName(const std::string& name) {
    std::string out;
    for (char ch : name)
        if ((unsigned char)ch >= ' ' && ch != '=') out.push_back(ch);
    return out;
}
inline std::string EscapeProfileValue(const std::string& v) {
    std::string out;
    out.reserve(v.size());
    for (char ch : v) {
        if (ch == '\\' || ch == '=' || ch == '|') out.push_back('\\');
        out.push_back(ch);
    }
    return out;
}
inline std::string UnescapeProfileValue(const std::string& v) {
    if (v.find('\\') == std::string::npos) return v;
    std::string out;
    out.reserve(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) ++i;   // \\x -> x (so \\\\ -> \\ and \\= -> =)
        out.push_back(v[i]);
    }
    return out;
}
inline void WriteBuildProfileBlock(std::ostream& o, const BuildProfile& p) {
    const BuildSettings& b = p.settings;
    o << "@profile " << SanitizeProfileName(p.name) << "\n";
    o << "product="  << EscapeProfileValue(p.productName) << "\n";
    o << "dir="      << EscapeProfileValue(p.outputDir) << "\n";
    o << "platform=" << p.platform << "\n";
    o << "company="  << EscapeProfileValue(b.company) << "\n";
    o << "version="  << EscapeProfileValue(b.version) << "\n";
    o << "bundleId=" << EscapeProfileValue(b.bundleId) << "\n";
    o << "icon="     << EscapeProfileValue(b.icon) << "\n";
    o << "splash="   << EscapeProfileValue(b.splash) << "\n";
    o << "width="    << b.width << "\n" << "height=" << b.height << "\n";
    o << "minWidth=" << b.minWidth << "\n" << "minHeight=" << b.minHeight << "\n";
    o << "fullscreen=" << b.fullscreen << "\n" << "borderless=" << b.borderless << "\n";
    o << "resizable=" << b.resizable << "\n" << "vsync=" << b.vsync << "\n" << "smoothDt=" << b.smoothDt << "\n";
    o << "hideCursor=" << b.hideCursor << "\n" << "fpsCap=" << b.fpsCap << "\n";
    o << "quitOnEscape=" << b.quitOnEscape << "\n" << "masterVolume=" << FloatText(b.masterVolume) << "\n";
    o << "showFps=" << b.showFps << "\n" << "includeAll=" << b.includeAllProjectScenes << "\n";
    o << "dev=" << b.developmentBuild << "\n" << "clean=" << b.cleanOutput << "\n";
    o << "dataFolder=" << b.dataFolder << "\n" << "encrypt=" << b.encryptData << "\n";
    o << "antiCheat=" << b.antiCheat << "\n";
    o << "runBg=" << b.runInBackground << "\n" << "muteUnfocus=" << b.muteOnFocusLoss << "\n";
    o << "maximized=" << b.startMaximized << "\n" << "userDir=" << b.saveToUserDir << "\n";
    o << "splashTime=" << FloatText(b.splashTime) << "\n";
    o << "splashBg0=" << FloatText(b.splashBg[0]) << "\n" << "splashBg1=" << FloatText(b.splashBg[1]) << "\n" << "splashBg2=" << FloatText(b.splashBg[2]) << "\n";
    o << "lockCursor=" << b.lockCursor << "\n" << "cursorLockMode=" << b.cursorLockMode << "\n" << "perPixel=" << b.perPixelLighting << "\n";
    o << "shadows=" << b.shadows << "\n" << "shadowDist=" << FloatText(b.shadowDistance) << "\n";
    o << "shadowSoft=" << FloatText(b.shadowSoftness) << "\n" << "shadowCasc=" << b.shadowCascades << "\n";
    o << "shadowRes=" << b.shadowResolution << "\n" << "bloom=" << b.bloom << "\n";
    o << "ssao=" << b.ssao << "\n" << "fxaa=" << b.fxaa << "\n" << "antialias=" << b.antialias << "\n";
    o << "gpu=" << b.gpuRenderer << "\n" << "d3d12=" << b.preferD3D12 << "\n";
    o << "openFolder=" << b.openFolderAfter << "\n" << "zip=" << b.zipAfter << "\n";
    o << "layout=" << b.layout << "\n" << "strip=" << b.stripAssets << "\n" << "compress=" << b.compress << "\n";
    o << "buildNumber=" << b.buildNumber << "\n" << "autoBuild=" << b.autoBuildNumber << "\n" << "archiveName=" << b.archiveName << "\n";
    o << "fsToggle=" << b.fullscreenToggle << "\n";
    o << "srvTarget=" << b.srvTarget << "\n" << "srvName=" << EscapeProfileValue(b.srvName) << "\n" << "srvPort=" << b.srvPort << "\n";
    o << "srvMax=" << b.srvMaxPlayers << "\n" << "srvTick=" << b.srvTick << "\n" << "srvPassword=" << EscapeProfileValue(b.srvPassword) << "\n";
    o << "srvAuth=" << b.srvAuth << "\n" << "srvAdvertise=" << b.srvAdvertise << "\n" << "srvAddr=" << EscapeProfileValue(b.srvPublicAddr) << "\n";
    o << "srvRegion=" << EscapeProfileValue(b.srvRegion) << "\n" << "srvMaxSpeed=" << FloatText(b.srvMaxSpeed) << "\n" << "srvRate=" << b.srvRateLimit << "\n";
    o << "srvKick=" << b.srvKickAfter << "\n";
    for (const BuildScene& sc : b.scenes) o << "scene=" << (sc.on ? 1 : 0) << "|" << EscapeProfileValue(sc.path) << "\n";
}

// Apply one key=value line to a profile being read.
inline void ApplyBuildProfileKV(BuildProfile& p, const std::string& k, const std::string& v) {
    BuildSettings& b = p.settings;
    auto I = [&]{ return std::atoi(v.c_str()); };
    auto F = [&]{ return (float)std::atof(v.c_str()); };
    auto B = [&]{ return std::atoi(v.c_str()) != 0; };
    auto S = [&](char* dst, size_t n){ std::snprintf(dst, n, "%s", UnescapeProfileValue(v).c_str()); };
    if      (k == "product")   p.productName = UnescapeProfileValue(v);
    else if (k == "dir")       p.outputDir = UnescapeProfileValue(v);
    else if (k == "platform")  p.platform = I();
    else if (k == "company")   S(b.company, sizeof(b.company));
    else if (k == "version")   S(b.version, sizeof(b.version));
    else if (k == "bundleId")  S(b.bundleId, sizeof(b.bundleId));
    else if (k == "icon")      S(b.icon, sizeof(b.icon));
    else if (k == "splash")    S(b.splash, sizeof(b.splash));
    else if (k == "width")     b.width = I();
    else if (k == "height")    b.height = I();
    else if (k == "minWidth")  b.minWidth = I();
    else if (k == "minHeight") b.minHeight = I();
    else if (k == "fullscreen") b.fullscreen = B();
    else if (k == "borderless") b.borderless = B();
    else if (k == "resizable") b.resizable = B();
    else if (k == "vsync")     b.vsync = B();
    else if (k == "smoothDt")  b.smoothDt = B();
    else if (k == "hideCursor") b.hideCursor = B();
    else if (k == "fpsCap")    b.fpsCap = I();
    else if (k == "quitOnEscape") b.quitOnEscape = B();
    else if (k == "masterVolume") b.masterVolume = F();
    else if (k == "showFps")   b.showFps = B();
    else if (k == "includeAll") b.includeAllProjectScenes = B();
    else if (k == "fsToggle")  b.fullscreenToggle = B();
    else if (k == "scene" && v.size() > 2 && v[1] == '|') b.scenes.push_back(BuildScene{UnescapeProfileValue(v.substr(2)), v[0] == '1'});
    else if (k == "dev")       b.developmentBuild = B();
    else if (k == "clean")     b.cleanOutput = B();
    else if (k == "dataFolder") b.dataFolder = B();
    else if (k == "encrypt")   b.encryptData = B();
    else if (k == "antiCheat") b.antiCheat = std::max(0, std::min(2, I()));
    else if (k == "runBg")     b.runInBackground = B();
    else if (k == "muteUnfocus") b.muteOnFocusLoss = B();
    else if (k == "maximized") b.startMaximized = B();
    else if (k == "userDir")   b.saveToUserDir = B();
    else if (k == "splashTime") b.splashTime = F();
    else if (k == "splashBg0") b.splashBg[0] = F();
    else if (k == "splashBg1") b.splashBg[1] = F();
    else if (k == "splashBg2") b.splashBg[2] = F();
    else if (k == "lockCursor") b.lockCursor = B();
    else if (k == "cursorLockMode") b.cursorLockMode = std::max(0, std::min(2, I()));
    else if (k == "perPixel")  b.perPixelLighting = B();
    else if (k == "shadows")   b.shadows = B();
    else if (k == "shadowDist") b.shadowDistance = F();
    else if (k == "shadowSoft") b.shadowSoftness = F();
    else if (k == "shadowCasc") b.shadowCascades = I();
    else if (k == "shadowRes") b.shadowResolution = I();
    else if (k == "bloom")     b.bloom = B();
    else if (k == "ssao")      b.ssao = B();
    else if (k == "fxaa")      b.fxaa = B();
    else if (k == "antialias") b.antialias = I();
    else if (k == "gpu")       b.gpuRenderer = B();
    else if (k == "d3d12")     b.preferD3D12 = B();
    else if (k == "openFolder") b.openFolderAfter = B();
    else if (k == "zip")       b.zipAfter = B();
    else if (k == "layout")    b.layout = std::max(0, std::min(2, I()));
    else if (k == "strip")     b.stripAssets = B();
    else if (k == "compress")  b.compress = B();
    else if (k == "buildNumber") b.buildNumber = std::max(1, I());
    else if (k == "autoBuild") b.autoBuildNumber = B();
    else if (k == "archiveName") S(b.archiveName, sizeof(b.archiveName));
    else if (k == "srvTarget") b.srvTarget = I();
    else if (k == "srvName")   S(b.srvName, sizeof(b.srvName));
    else if (k == "srvPort")   b.srvPort = I();
    else if (k == "srvMax")    b.srvMaxPlayers = I();
    else if (k == "srvTick")   b.srvTick = I();
    else if (k == "srvPassword") S(b.srvPassword, sizeof(b.srvPassword));
    else if (k == "srvAuth")   b.srvAuth = B();
    else if (k == "srvAdvertise") b.srvAdvertise = B();
    else if (k == "srvAddr")   S(b.srvPublicAddr, sizeof(b.srvPublicAddr));
    else if (k == "srvRegion") S(b.srvRegion, sizeof(b.srvRegion));
    else if (k == "srvMaxSpeed") b.srvMaxSpeed = F();
    else if (k == "srvRate")   b.srvRateLimit = I();
    else if (k == "srvKick")   b.srvKickAfter = I();
}


// ---- build profiles: the file text ------------------------------------------------------------------------

/// All profiles as the text of build_profiles.cfg.
inline std::string WriteBuildProfiles(const std::vector<BuildProfile>& profiles) {
    std::ostringstream o;
    o << "# OkaySpace build profiles\n";
    for (const auto& p : profiles) WriteBuildProfileBlock(o, p);
    return o.str();
}

/// Read the profiles in `text` (an "@profile <name>" line starts each block).
inline std::vector<BuildProfile> ParseBuildProfiles(const std::string& text) {
    std::vector<BuildProfile> out;
    std::istringstream f(text);
    std::string line;
    BuildProfile cur;
    bool have = false;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("@profile ", 0) == 0) {
            if (have) out.push_back(cur);
            // The name is one word (control characters and '=' never reach the
            // file), so an old or hostile file cannot forge keys or profiles.
            cur = BuildProfile{}; cur.name = SanitizeProfileName(line.substr(9)); have = true;
            continue;
        }
        auto eq = line.find('=');
        if (!have || eq == std::string::npos) continue;
        ApplyBuildProfileKV(cur, line.substr(0, eq), line.substr(eq + 1));
    }
    if (have) out.push_back(cur);
    return out;
}

/// The Build Settings dialog as it was left: its settings, product name, output folder and platform, and
/// the name of the profile it came from ("" when it is not one). A separate file, so editors that know
/// only profiles never see it.
inline std::string WriteBuildLive(const BuildProfile& live, const std::string& selectedProfile) {
    BuildProfile p = live;
    p.name = "live";
    std::ostringstream o;
    o << "# OkaySpace build settings as last left in the dialog\n";
    WriteBuildProfileBlock(o, p);
    o << "selected=" << selectedProfile << "\n";
    return o.str();
}
inline bool ParseBuildLive(const std::string& text, BuildProfile& live, std::string& selectedProfile) {
    std::vector<BuildProfile> v = ParseBuildProfiles(text);
    if (v.empty()) return false;
    live = v[0];
    selectedProfile.clear();
    std::istringstream f(text);
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.rfind("selected=", 0) == 0) selectedProfile = line.substr(9);
    }
    return true;
}

// ---- project settings ---------------------------------------------------------------------------------------

// Project-wide settings (persisted to project.okayproj): defaults a build/scene
// inherits, edited via Edit > Project Settings.
struct ProjectSettings {
    char  company[128]  = "OkaySpace";
    char  version[32]   = "1.0.0";
    int   defaultWidth  = 1280, defaultHeight = 720;
    float gravity2D[2]  = {0.0f, -9.81f};
    float gravity3D[3]  = {0.0f, -9.81f, 0.0f};
    float ambient       = 0.30f;
    bool  skybox        = true;
    // ---- Editor loading screen (per-project splash customization) ----
    char  splashLogo[260] = "";            // optional PNG shown instead of the cube
    float splashBgTop[3]  = {0.094f, 0.094f, 0.106f};
    float splashBgBot[3]  = {0.051f, 0.051f, 0.059f};
    bool  splashAccentOverride = false;
    float splashAccent[3] = {0.235f, 0.475f, 0.800f};
    bool  splashShowBar   = true;
};

/// The text of project.okayproj for `s`, then `lists`: the tag list and layer names lines (ProjectLists::Write()).
/// A new project passes "" (no custom tags, default layer names) without touching the lists that are live.
inline std::string WriteProjectSettings(const ProjectSettings& s, const std::string& lists) {
    std::ostringstream f;
    auto v3 = [&](const float* a) { return FloatText(a[0]) + " " + FloatText(a[1]) + " " + FloatText(a[2]); };
    f << "company=" << s.company << "\n";
    f << "version=" << s.version << "\n";
    f << "width=" << s.defaultWidth << "\n";
    f << "height=" << s.defaultHeight << "\n";
    f << "gravity2d=" << FloatText(s.gravity2D[0]) << " " << FloatText(s.gravity2D[1]) << "\n";
    f << "gravity3d=" << v3(s.gravity3D) << "\n";
    f << "ambient=" << FloatText(s.ambient) << "\n";
    f << "skybox=" << (s.skybox ? 1 : 0) << "\n";
    f << "splash_logo=" << s.splashLogo << "\n";
    f << "splash_bar=" << (s.splashShowBar ? 1 : 0) << "\n";
    f << "splash_accent_on=" << (s.splashAccentOverride ? 1 : 0) << "\n";
    f << "splash_bg_top=" << v3(s.splashBgTop) << "\n";
    f << "splash_bg_bot=" << v3(s.splashBgBot) << "\n";
    f << "splash_accent=" << v3(s.splashAccent) << "\n";
    f << lists;
    return f.str();
}
/// The text of project.okayproj for `s` with the tag list and layer names that are live now.
inline std::string WriteProjectSettings(const ProjectSettings& s) { return WriteProjectSettings(s, okay::ProjectLists::Write()); }

/// Apply one project.okayproj line. False for a key that is not a project setting (the tag and layer lines
/// are ProjectLists' and are handled by the caller).
inline bool ApplyProjectSettingLine(ProjectSettings& s, const std::string& k, const std::string& v) {
    auto f3 = [&](float* dst) { float a = dst[0], b = dst[1], c = dst[2]; std::sscanf(v.c_str(), "%f %f %f", &a, &b, &c); dst[0] = a; dst[1] = b; dst[2] = c; };
    if      (k == "company") { std::snprintf(s.company, sizeof(s.company), "%s", v.c_str()); }
    else if (k == "version") { std::snprintf(s.version, sizeof(s.version), "%s", v.c_str()); }
    else if (k == "width")   s.defaultWidth = std::atoi(v.c_str());
    else if (k == "height")  s.defaultHeight = std::atoi(v.c_str());
    else if (k == "gravity2d") { float a = s.gravity2D[0], b = s.gravity2D[1]; std::sscanf(v.c_str(), "%f %f", &a, &b); s.gravity2D[0] = a; s.gravity2D[1] = b; }
    else if (k == "gravity3d") f3(s.gravity3D);
    else if (k == "ambient") s.ambient = (float)std::atof(v.c_str());
    else if (k == "skybox")  s.skybox = std::atoi(v.c_str()) != 0;
    else if (k == "splash_logo") std::snprintf(s.splashLogo, sizeof(s.splashLogo), "%s", v.c_str());
    else if (k == "splash_bar") s.splashShowBar = std::atoi(v.c_str()) != 0;
    else if (k == "splash_accent_on") s.splashAccentOverride = std::atoi(v.c_str()) != 0;
    else if (k == "splash_bg_top") f3(s.splashBgTop);
    else if (k == "splash_bg_bot") f3(s.splashBgBot);
    else if (k == "splash_accent") f3(s.splashAccent);
    else return false;
    return true;
}

/// Read project.okayproj text into `s` and the tag / layer lists. The lists start from their defaults.
inline void ParseProjectSettings(ProjectSettings& s, const std::string& text) {
    okay::ProjectLists::Reset();
    std::istringstream f(text);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (okay::ProjectLists::ApplyLine(k, v)) continue;   // tag=, strict_tags=, layer_name_N=
        ApplyProjectSettingLine(s, k, v);
    }
}

// ---- where the files are ------------------------------------------------------------------------------------

/// <project>/project.okayproj. With no project open it is the working folder's, as before projects had their own.
inline std::string ProjectSettingsFile(const std::string& projectDir) { return okayproj::SettingsPath(projectDir); }
inline std::string BuildProfilesFile(const std::string& projectDir) {
    return projectDir.empty() ? std::string() : (std::filesystem::path(projectDir) / ".okay" / "build_profiles.cfg").string();
}
inline std::string BuildLiveFile(const std::string& projectDir) {
    return projectDir.empty() ? std::string() : (std::filesystem::path(projectDir) / ".okay" / "build_live.cfg").string();
}

} // namespace oked
