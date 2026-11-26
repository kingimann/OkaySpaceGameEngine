#pragma once
// Build Game (BuildGame.cpp): export the current scene as a standalone runnable game (the player runtime plus the
// game's data), the data-only exports for Web, Android and Xbox, and the dedicated server folder. Called from main.cpp:
// the Build Settings window's Build button (DrawFileDialogs), `--build-game` (RunBuildGameCli), the OKAY_UI_BUILDSERVER hook.
// Moved out of main.cpp unchanged (docs/editor/main_split_plan.md, move 1).
#include "EditorState.hpp"
#include "ProjectState.hpp"              // BuildSettings, ProjectSettings
#include "okay/Core/BuildConfigExtra.hpp"   // buildpipe::ExtraConfig
#include "okay/Core/BuildPipeline.hpp"      // buildpipe::Report
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace oked {
namespace builder {

// The player runtime's file name, and where it is (next to the editor, a launcher layout or a build tree); "" when missing.
extern const char* kPlayerExe;
std::string FindPlayer();
// The player runtime, downloaded next to the editor (checked against the published checksum) when it is missing.
std::string EnsurePlayer(std::string* note);

// Options forwarded from the Build Settings dialog into the build.
struct Options {
    std::string company = "OkaySpace";
    std::string version = "1.0.0";
    int  width = 1280, height = 720;
    bool fullscreen = false, borderless = false, resizable = true, vsync = true;
    bool smoothDt = true;
    bool hideCursor = false;
    int  fpsCap = 0;
    bool quitOnEscape = true;
    float masterVolume = 1.0f;
    bool showFps = false;
    bool includeAllProjectScenes = false, developmentBuild = false;
    bool lockCursor = false, perPixelLighting = false, shadows = true,
         bloom = false, ssao = false, fxaa = true;
    int cursorLockMode = 0;
    float shadowDistance = 80.0f, shadowSoftness = 2.5f;
    int  shadowCascades = 3, shadowResolution = 2048;
    int  antialias = 1;
    bool gpuRenderer = true;
    bool preferD3D12 = false;
    bool cleanOutput = true;   // wipe stale build files first (no leftovers from old builds)
    bool dataFolder  = true;   // tuck scenes/config/assets into a "Data/" subfolder (clean root)
    bool encryptData = false;  // obfuscate scenes + config so players can't read/edit them
    bool packageAssets = true; // pack the data folder into game.okaypkg (docs/engine/asset_packages.md); OKAY_BUILD_LOOSE_FILES=1 turns it off
    int  antiCheat = 0;        // 0 off, 1 report, 2 enforce: the player checks Data/manifest.json + runtime checks
    bool fullscreenToggle = true;           // Alt+Enter / F11 in the shipped game
    int  layout = 0;                        // buildpipe::Layout: 0 Folder, 1 Compact, 2 Zip
    bool stripAssets = true;                // ship only what the built scenes use
    bool compress = true;                   // zip: deflate
    bool zipAlso = false;                   // also write <archive>.zip next to the output folder
    bool dryRun = false;                    // build in a scratch folder, keep only the report
    int  buildNumber = 1;
    std::string archiveName = "{name}-{version}-{platform}";
    std::vector<std::string> sceneList;     // Scenes In Build, in order (empty = the open scene)
    okay::buildpipe::ExtraConfig extra;     // Build Settings > Game: speed, start volumes, language, logging, environment (game.okayconfig)
    std::string excludeList;                // Build Settings > Output > Files left out of the build ("Assets/a.png;Assets/Old/")
    bool allowCommands = false;             // Build Settings > Advanced: run the two commands below (off = never)
    std::string preCommand, postCommand;    // run before the build (a failure stops it) / after a good build
    // ---- Player presentation ----
    std::string iconPath;      // window icon PNG (copied into the build)
    std::string bundleId = "com.okayspace.game";
    bool runInBackground = true;
    bool muteOnFocusLoss = false;
    bool startMaximized  = false;
    int  minWidth = 0, minHeight = 0;
    bool saveToUserDir = true;   // saves -> per-user app folder (persistentDataPath)
    std::string splashImage;     // startup logo PNG
    float splashTime = 2.0f;
    float splashBg[3] = {0.05f, 0.05f, 0.06f};
    // ---- Boot splash (branded loading frame; the project's custom loading
    // screen carried into the shipped game so it matches the editor/launcher) ----
    std::string bootLogo;        // optional PNG (copied into the build)
    float bootBgTop[3] = {0.094f, 0.094f, 0.106f};
    float bootBgBot[3] = {0.051f, 0.051f, 0.059f};
    bool  bootAccentOverride = false;
    float bootAccent[3] = {0.235f, 0.475f, 0.800f};
    bool  bootBar = true;
};

// A per-file size breakdown of the finished build, for the result popup.
enum BuildCat { CAT_EXE = 0, CAT_LIB = 1, CAT_DATA = 2, CAT_ASSET = 3 };
struct ReportEntry { std::string name; std::uintmax_t bytes = 0; int cat = CAT_ASSET; };
struct BuildReport {
    std::vector<ReportEntry> files;
    std::uintmax_t total = 0;
    std::uintmax_t catBytes[4] = {0, 0, 0, 0};
    int missing = 0;
    bool valid = false;
    bool ok = false;                       // the build (or dry run) finished
    okay::buildpipe::Report r;             // the full report: by kind and type, what was left out, warnings, time
};

inline std::string HumanBytes(std::uintmax_t b) {
    // dup-allow: moved verbatim from editor/main.cpp by the split; the two HumanBytes differ in output ("512.0 B" vs "512 B") and are merged in a follow-up dedup
    char s[32]; double v = (double)b; const char* u = "B";
    if (v >= 1024.0 * 1024 * 1024) { v /= 1024.0 * 1024 * 1024; u = "GB"; }
    else if (v >= 1024.0 * 1024)   { v /= 1024.0 * 1024;        u = "MB"; }
    else if (v >= 1024.0)          { v /= 1024.0;               u = "KB"; }
    std::snprintf(s, sizeof(s), "%.1f %s", v, u); return std::string(s);
}

// Build the game into outDir; the full description is at the definition (BuildGame.cpp).
std::string Build(okay::editor::EditorState& ed, const std::string& outDir,
                  const std::string& gameName, const Options& opt = {}, BuildReport* report = nullptr);
// The Build Settings as the builder's options.
Options OptionsFromSettings(const BuildSettings& g, const ProjectSettings& pr);
// The game's data plus a ready-to-build project for Web (1), Android (2) or Xbox (4).
std::string ExportForPlatform(okay::editor::EditorState& ed, const std::string& outDir,
                              const std::string& gameName, const Options& opt, int target);

// A dedicated server folder: the settings.
struct ServerOptions {
    int  target = 0;              // 0 = Linux, 1 = Windows
    std::string name, password, publicAddr, region;
    int  port = 45000, maxPlayers = 16, tick = 30, rateLimit = 0, kickAfter = 0;
    bool auth = false, advertise = false;
    float maxSpeed = 0.0f;
};
// Build the dedicated server folder (data, player runtime, server.cfg, start and deploy scripts).
std::string BuildServer(okay::editor::EditorState& ed, const std::string& outDir, const std::string& gameName,
                        Options opt, const ServerOptions& so, BuildReport* report = nullptr);

} // namespace builder
} // namespace oked
