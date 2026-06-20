// OkaySpace Editor — a Unity-style desktop editor (Dear ImGui + SDL2 + OpenGL).
//
// Panels: Hierarchy, Inspector, Scene viewport (mouse select + drag, pan/zoom),
// and a Play/Stop/Step toolbar. File menu saves/loads scenes via the engine's
// SceneSerializer.
//
// Run with --selftest to exercise the (headless) editor logic without opening a
// window — used in CI where there's no display.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "EditorState.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// ----------------------------------------------------------------------------
// Headless self-test: verifies the editor's model logic with no GUI.
// ----------------------------------------------------------------------------
static int RunSelfTest() {
    using namespace okay;
    using namespace okay::editor;
    int failures = 0;
    auto check = [&](bool cond, const char* what) {
        if (!cond) { ++failures; std::cerr << "  FAIL: " << what << "\n"; }
    };

    EditorState ed;
    GameObject* sprite = ed.CreateSprite("Hero");
    check(sprite != nullptr, "create sprite");
    check(ed.selected() == sprite, "auto-select new object");
    sprite->transform->localPosition = {5, 6, 0};

    // Save / load round-trip.
    std::string path = "/tmp/okay_editor_selftest.okayscene";
    check(ed.Save(path), "save scene");

    EditorState ed2;
    std::string err;
    check(ed2.Load(path, &err), "load scene");
    GameObject* loaded = ed2.scene().Find("Hero");
    check(loaded != nullptr, "loaded object present");
    if (loaded) check(loaded->transform->localPosition == Vec3(5, 6, 0), "position preserved");

    // Play / stop restores edit state (move during play is reverted).
    ed.Play();
    check(ed.isPlaying(), "is playing");
    ed.Tick(0.1f);
    ed.Stop();
    check(!ed.isPlaying(), "stopped");
    GameObject* afterStop = ed.scene().Find("Hero");
    check(afterStop != nullptr, "object restored after stop");

    // Delete.
    ed.Select(ed.scene().Find("Hero"));
    ed.DeleteSelected();
    check(ed.scene().Find("Hero") == nullptr, "delete selected");

    // Undo / redo around a creation.
    EditorState ue;
    std::size_t base = ue.scene().Objects().size();
    ue.CreateSprite("Temp");
    check(ue.scene().Objects().size() == base + 1, "created object");
    check(ue.CanUndo(), "can undo");
    ue.Undo();
    check(ue.scene().Objects().size() == base, "undo removes object");
    check(ue.CanRedo(), "can redo");
    ue.Redo();
    check(ue.scene().Objects().size() == base + 1, "redo restores object");

    std::cout << (failures == 0 ? "editor selftest: OK\n" : "editor selftest: FAILED\n");
    return failures == 0 ? 0 : 1;
}

#if defined(OKAY_EDITOR_HEADLESS)
int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--selftest") return RunSelfTest();
    std::cerr << "This editor build is headless (no SDL). Use --selftest.\n";
    return 0;
}
#else

// Take control of the program entry point ourselves so SDL doesn't #define
// `main` to SDL_main (which would also rewrite identifiers like `cam->main`).
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "AppIcon.hpp"
#include "imgui.h"
#include "imgui_internal.h" // DockBuilder for the default layout
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#endif

#ifndef OKAY_ENGINE_VERSION
#  define OKAY_ENGINE_VERSION "dev"
#endif

using namespace okay;
using namespace okay::editor;

namespace {

ImU32 ToColor(const Color& c) {
    return IM_COL32((int)(c.r * 255), (int)(c.g * 255), (int)(c.b * 255), (int)(c.a * 255));
}

// ---- Z-buffered 3D: render meshes into an SDL texture we blit into the view,
//      so overlapping faces occlude correctly (the ImGui draw list has no depth
//      buffer). Set by main(); the texture is reused/resized across frames.
SDL_Renderer* g_sdlRenderer = nullptr;

// One render target per 3D view "slot" (0 = Scene viewport, 1 = Game view).
// ImGui defers rendering, so the SDL texture is only sampled at SDL_RenderCopy
// time — if the Scene and Game views shared one texture, whichever drew last
// would overwrite it and BOTH panels would show that content (the flicker seen
// when splitting Scene + Game). A texture per slot keeps them independent.
static const int kView3DSlots = 3;   // 0=Scene view, 1=Game view, 2=Camera preview
SDL_Texture* g_view3DTex[kView3DSlots] = {};
int g_view3DW[kView3DSlots] = {}, g_view3DH[kView3DSlots] = {};
Raster g_view3DRaster[kView3DSlots];

// Render the scene's solid meshes (z-buffered) at w*h into the slot's texture;
// transparent where nothing is drawn (so a grid/background shows through).
SDL_Texture* Render3DTexture(const Scene& scene, const Mat4& vp, const Vec3& eye,
                             int w, int h, int slot = 0) {
    if (!g_sdlRenderer) return nullptr;
    if (slot < 0 || slot >= kView3DSlots) slot = 0;
    w = w < 1 ? 1 : (w > 4096 ? 4096 : w);
    h = h < 1 ? 1 : (h > 4096 ? 4096 : h);
    Raster& ras = g_view3DRaster[slot];
    ras.Resize(w, h);
    ras.Clear(0u);                               // transparent
    ApplySceneLight(scene);                      // a Light object aims the shading
    RenderMeshes(ras, scene, vp, eye);
    if (!g_view3DTex[slot] || g_view3DW[slot] != w || g_view3DH[slot] != h) {
        if (g_view3DTex[slot]) SDL_DestroyTexture(g_view3DTex[slot]);
        g_view3DTex[slot] = SDL_CreateTexture(g_sdlRenderer, SDL_PIXELFORMAT_ABGR8888,
                                              SDL_TEXTUREACCESS_STREAMING, w, h);
        SDL_SetTextureBlendMode(g_view3DTex[slot], SDL_BLENDMODE_BLEND);
        g_view3DW[slot] = w; g_view3DH[slot] = h;
    }
    SDL_UpdateTexture(g_view3DTex[slot], nullptr, ras.color.data(), w * 4);
    return g_view3DTex[slot];
}

// Cached image thumbnail for the Project browser: loads the file once and
// uploads it as an SDL texture. okay::Image is tightly-packed RGBA8, which is
// exactly SDL_PIXELFORMAT_ABGR8888 byte order on little-endian. Returns nullptr
// if the image can't be loaded.
SDL_Texture* GetThumb(const std::string& path) {
    static std::unordered_map<std::string, SDL_Texture*> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    SDL_Texture* tex = nullptr;
    okay::Image img;
    if (g_sdlRenderer && img.Load(path) && img.Width() > 0) {
        tex = SDL_CreateTexture(g_sdlRenderer, SDL_PIXELFORMAT_ABGR8888,
                                SDL_TEXTUREACCESS_STATIC, img.Width(), img.Height());
        if (tex) {
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            SDL_UpdateTexture(tex, nullptr, img.Pixels().data(), img.Width() * 4);
        }
    }
    cache[path] = tex;   // cache failures (nullptr) too, so we don't retry
    return tex;
}

// ---- Self-updater + runtime fetcher -----------------------------------
// The editor can pull newer published builds from GitHub and, when exporting a
// game, fetch the player runtime if it isn't sitting beside the editor — so a
// fresh editor "generates everything it needs" without a manual file shuffle.
namespace updater {
namespace fs = std::filesystem;

// Raw GitHub URL of the published dist/ folder.
const char* kRawBase =
    "https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/main/dist/";

bool pendingQuit = false; // set true after a successful self-update relaunch

// Absolute path of the running executable.
std::string SelfPath() {
#if defined(_WIN32)
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::string(buf, n);
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = '\0'; return buf; }
    return {};
#endif
}

// Append a unique cache-busting query parameter. raw.githubusercontent.com is
// fronted by a CDN that caches files for minutes, so a plain request can hand
// back a stale VERSION.txt or an old .exe right after a release — the cause of
// "it didn't update". Varying the URL per request forces a fresh fetch.
std::string BustCache(const std::string& url) {
    static unsigned counter = 0;
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string sep = url.find('?') == std::string::npos ? "?" : "&";
    return url + sep + "okaycb=" + std::to_string(now) + "_" + std::to_string(counter++);
}

// Download a URL to a file using whatever HTTP client is on the system. Tries
// curl first (ships on Win10+/macOS/Linux), then a platform fallback. -f makes
// curl fail (non-zero) on a 404 so we don't cache an error page as a binary.
// No-cache headers + a unique query string defeat the GitHub raw CDN cache.
bool Download(const std::string& url, const std::string& outPath) {
    std::error_code ec; fs::remove(outPath, ec);
    std::string u = BustCache(url);
    const char* noCache = "-H \"Cache-Control: no-cache\" -H \"Pragma: no-cache\" ";
#if defined(_WIN32)
    std::string c1 = "curl -L -s -f " + std::string(noCache) + "-o \"" + outPath + "\" \"" + u + "\"";
    if (std::system(c1.c_str()) == 0 && fs::exists(outPath)) return true;
    std::string c2 = "powershell -NoProfile -Command \"try { Invoke-WebRequest "
                     "-Headers @{'Cache-Control'='no-cache'} "
                     "-UseBasicParsing -Uri '" + u + "' -OutFile '" + outPath +
                     "' } catch { exit 1 }\"";
    return std::system(c2.c_str()) == 0 && fs::exists(outPath);
#else
    std::string c1 = "curl -L -s -f " + std::string(noCache) + "-o \"" + outPath + "\" \"" + u + "\" 2>/dev/null";
    if (std::system(c1.c_str()) == 0 && fs::exists(outPath)) return true;
    std::string c2 = "wget -q --no-cache -O \"" + outPath + "\" \"" + u + "\" 2>/dev/null";
    return std::system(c2.c_str()) == 0 && fs::exists(outPath);
#endif
}

// Compare dotted versions ("1.4.0"); returns -1 / 0 / 1 for a<b / a==b / a>b.
int CompareVersions(const std::string& a, const std::string& b) {
    auto parse = [](const std::string& s) {
        std::vector<int> v; std::stringstream ss(s); std::string tok;
        while (std::getline(ss, tok, '.')) {
            int n = 0; try { n = std::stoi(tok); } catch (...) { n = 0; }
            v.push_back(n);
        }
        return v;
    };
    std::vector<int> va = parse(a), vb = parse(b);
    std::size_t n = std::max(va.size(), vb.size());
    for (std::size_t i = 0; i < n; ++i) {
        int x = i < va.size() ? va[i] : 0;
        int y = i < vb.size() ? vb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

// Result of an update check: the running version, the published version, the
// release notes (if any) and whether an upgrade is actually available.
struct UpdateInfo {
    bool checked = false;
    bool available = false;
    std::string current, latest, notes, error;
};

// Query GitHub for the published version + release notes. No files are swapped;
// this only reports what's available (the install step is explicit).
UpdateInfo CheckLatest() {
    UpdateInfo info;
    info.current = OKAY_ENGINE_VERSION;
    info.checked = true;
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec);
    fs::path vf = tmp / "okayspace_version.txt";
    if (!Download(std::string(kRawBase) + "VERSION.txt", vf.string())) {
        info.error = "Couldn't reach GitHub (no internet, or curl/PowerShell missing).";
        return info;
    }
    { std::ifstream f(vf); std::getline(f, info.latest); }
    while (!info.latest.empty() &&
           (info.latest.back() == '\r' || info.latest.back() == '\n' || info.latest.back() == ' '))
        info.latest.pop_back();
    fs::remove(vf, ec);

    // Optional human-readable release notes.
    fs::path cf = tmp / "okayspace_changelog.txt";
    if (Download(std::string(kRawBase) + "CHANGELOG.txt", cf.string())) {
        std::ifstream f(cf);
        std::stringstream ss; ss << f.rdbuf();
        info.notes = ss.str();
        fs::remove(cf, ec);
    }
    info.available = !info.latest.empty() && CompareVersions(info.current, info.latest) < 0;
    return info;
}

// Launch a (new) executable detached from this process.
void Relaunch(const std::string& path) {
#if defined(_WIN32)
    int rc = std::system(("start \"\" \"" + path + "\"").c_str());
#else
    int rc = std::system(("\"" + path + "\" >/dev/null 2>&1 &").c_str());
#endif
    (void)rc;
}

// Download the published editor and swap it in for the running .exe, then
// relaunch. Returns a human-readable status. Only call when an update is known
// to be available (so this never loops on an already-current build).
std::string InstallUpdate(const std::string& latest) {
    std::error_code ec;
    fs::path self = SelfPath();
    if (self.empty()) return "Couldn't locate the running executable.";
    fs::path newFile = self; newFile += ".new";
    if (!Download(std::string(kRawBase) + "OkaySpaceEngine.exe", newFile.string()))
        return "Download of v" + latest + " failed.";
    if (fs::file_size(newFile, ec) < 100000) {
        fs::remove(newFile, ec);
        return "Downloaded file looked invalid; update aborted.";
    }
    // Reject a cached HTML/error page: a real Windows .exe begins with "MZ".
    {
        std::ifstream chk(newFile, std::ios::binary);
        char magic[2] = {0, 0}; chk.read(magic, 2);
        if (magic[0] != 'M' || magic[1] != 'Z') {
            fs::remove(newFile, ec);
            return "Downloaded file wasn't a valid program (CDN cache?). Try again.";
        }
    }
    fs::path backup = self; backup += ".old";
    fs::remove(backup, ec);
    fs::rename(self, backup, ec);
    if (ec) { fs::remove(newFile, ec); return "Couldn't replace the app: " + ec.message(); }
    fs::rename(newFile, self, ec);
    if (ec) { fs::rename(backup, self, ec); return "Couldn't install the update: " + ec.message(); }
#if !defined(_WIN32)
    fs::permissions(self, fs::perms::owner_exec | fs::perms::group_exec |
                    fs::perms::others_exec, fs::perm_options::add, ec);
#endif
    Relaunch(self.string());
    pendingQuit = true;
    return "Updated to v" + latest + "! Reopening...";
}

} // namespace updater

std::string g_updateStatus;
bool g_openUpdatePopup = false;
updater::UpdateInfo g_update;     // last update check result
bool g_installingUpdate = false;  // set while InstallUpdate runs
bool g_openAbout = false;
bool g_showNewProject = true;   // show the project chooser on launch

// Panel visibility (View menu).
bool g_showHierarchy = true, g_showInspector = true, g_showConsole = true,
     g_showProject = true, g_showServices = true, g_showScriptEditor = true;
bool g_showGame = true;   // Unity-style Game view (main-camera render)
bool g_focusGameOnPlay = false;  // pressing Play brings the Game tab forward
bool g_showScriptDocs = false;   // OkayScript reference window
bool g_showColliders = true;     // draw collider wireframes in the Scene view
bool g_resetLayout = false;      // request a dock-layout rebuild next frame
bool g_paused = false;           // pause the simulation while staying in Play
bool g_clearConsoleOnPlay = true; // wipe the console each time Play starts
int  g_theme = 0;                // 0 = Dark, 1 = Light, 2 = Classic
float g_uiScale = 1.00f;         // global UI scale (1.0 keeps the font crisp)
bool g_autosave = false;         // periodically save the open scene
double g_lastAutosave = 0.0;     // seconds since last autosave
bool g_quitRequested = false;    // quit pending (may prompt to save first)
std::vector<std::string> g_recent; // recently opened/saved scene paths
std::string g_clipboard;         // serialized GameObject for copy/paste

// Recently-used scenes, persisted to a small file beside the working dir.
void LoadRecent() {
    g_recent.clear();
    std::ifstream f("okay_recent.txt");
    std::string line;
    while (std::getline(f, line) && g_recent.size() < 10)
        if (!line.empty()) g_recent.push_back(line);
}
void SaveRecent() {
    std::ofstream f("okay_recent.txt");
    for (const auto& p : g_recent) f << p << "\n";
}
void AddRecent(const std::string& path) {
    if (path.empty()) return;
    g_recent.erase(std::remove(g_recent.begin(), g_recent.end(), path), g_recent.end());
    g_recent.insert(g_recent.begin(), path);
    if (g_recent.size() > 10) g_recent.resize(10);
    SaveRecent();
}

// File dialogs.
bool g_showSaveAs = false, g_showOpen = false;
char g_pathBuf[256] = "scene.okayscene";

// Data Asset (Scriptable Object) editor window.
bool  g_dataAssetOpen = false;
std::string g_dataAssetPath;
// Material (.okaymat) editor window.
bool  g_matAssetOpen = false;
std::string g_matAssetPath;
// "New Script" naming dialog (Add Component > Script > New Script...).
GameObject* g_newScriptGO = nullptr;
bool  g_newScriptOpen = false;
char  g_newScriptName[96] = "";

// Extra panels / tools.
bool  g_showStats = true;
bool  g_showScenes = false;
bool  g_showInstPrefab = false;
char  g_prefabBuf[256] = "Prefab.okayprefab";
bool  g_showBuildGame = false;
char  g_buildDirBuf[256] = "build/MyGame";
char  g_buildNameBuf[128] = "MyGame";
std::string g_buildStatus;
bool  g_openBuildResult = false;

// Unity-style Build Settings: window + player options written into the build's
// game.okayconfig, plus which scenes to include.
struct BuildSettings {
    char  company[128] = "OkaySpace";
    char  version[32]  = "1.0.0";
    int   width = 1280, height = 720;
    bool  fullscreen = false;
    bool  borderless = false;
    bool  resizable = true;
    bool  vsync = true;
    bool  hideCursor = false;
    int   fpsCap = 0;                      // 0 = uncapped (vsync paces)
    bool  quitOnEscape = true;
    float masterVolume = 1.0f;
    bool  showFps = false;
    bool  includeAllProjectScenes = false; // else just the current scene
    bool  developmentBuild = false;
};
BuildSettings g_build;

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
};
ProjectSettings g_project;
bool g_showProjectSettings = false;

void SaveProjectSettings() {
    std::ofstream f("project.okayproj");
    if (!f) return;
    f << "company=" << g_project.company << "\n";
    f << "version=" << g_project.version << "\n";
    f << "width=" << g_project.defaultWidth << "\n";
    f << "height=" << g_project.defaultHeight << "\n";
    f << "gravity2d=" << g_project.gravity2D[0] << " " << g_project.gravity2D[1] << "\n";
    f << "gravity3d=" << g_project.gravity3D[0] << " " << g_project.gravity3D[1] << " " << g_project.gravity3D[2] << "\n";
    f << "ambient=" << g_project.ambient << "\n";
    f << "skybox=" << (g_project.skybox ? 1 : 0) << "\n";
}

void LoadProjectSettings() {
    std::ifstream f("project.okayproj");
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if      (k == "company") std::strncpy(g_project.company, v.c_str(), sizeof(g_project.company) - 1);
        else if (k == "version") std::strncpy(g_project.version, v.c_str(), sizeof(g_project.version) - 1);
        else if (k == "width")   g_project.defaultWidth = std::atoi(v.c_str());
        else if (k == "height")  g_project.defaultHeight = std::atoi(v.c_str());
        else if (k == "gravity2d") std::sscanf(v.c_str(), "%f %f", &g_project.gravity2D[0], &g_project.gravity2D[1]);
        else if (k == "gravity3d") std::sscanf(v.c_str(), "%f %f %f", &g_project.gravity3D[0], &g_project.gravity3D[1], &g_project.gravity3D[2]);
        else if (k == "ambient") g_project.ambient = (float)std::atof(v.c_str());
        else if (k == "skybox")  g_project.skybox = std::atoi(v.c_str()) != 0;
    }
}
bool  g_snap = false;
float g_snapSize = 1.0f;
int   g_uiGrid = 8;         // UI snap grid in pixels (Snap on)
// Alignment guides: screen-space lines the UI drag snapped to this frame, drawn
// by the overlay (Unity-style smart snapping). Cleared and refilled each drag.
float g_uiGuideX = -1.0f;   // canvas-x of a vertical guide, or <0 = none
float g_uiGuideY = -1.0f;   // canvas-y of a horizontal guide, or <0 = none
// Active transform tool for the Scene view (Unity's W/E/R).
enum class Tool { Move, Rotate, Scale };
Tool  g_tool = Tool::Move;
int   g_gizmoAxis = -1;     // axis being dragged: 0=X 1=Y 2=Z, -1 = none
bool  g_gizmoGrab = false;  // true while a gizmo handle is held
float g_rotAccum = 0.0f;    // raw degrees this rotate-drag (for 15-deg snap detents)
float g_rotApplied = 0.0f;  // snapped degrees already applied this drag
float g_rotSnapDeg = 15.0f; // rotation snap increment when Snap is on
bool  g_gizmoLocal = false; // gizmo axes in the object's local space (Unity's Local/Global)
bool  g_terrainSculpt = false; // terrain brush active in the 3D scene view
float g_terrainRadius = 6.0f;
float g_terrainStrength = 4.0f;
GameObject* g_uiDragTarget = nullptr; // UI widget being dragged in the viewport
bool  g_uiHandled = false;  // a UI widget consumed this frame's click/drag
int   g_uiResizeHandle = -1; // 0..7 resize handle being dragged, -1 = moving

// First connected game controller (opened in main); fed into Input during Play.
SDL_GameController* g_pad = nullptr;
// Top-left of the canvas the running game is previewed in (Game view if shown,
// otherwise the Scene viewport). Mouse input is fed relative to this so UI
// buttons hit-test correctly while playing.
ImVec2 g_playCanvasPos = ImVec2(0, 0);

// Shortest distance (pixels) from point p to the segment a-b, for handle picking.
static float SegDistPx(ImVec2 p, ImVec2 a, ImVec2 b) {
    float vx = b.x - a.x, vy = b.y - a.y;
    float wx = p.x - a.x, wy = p.y - a.y;
    float L2 = vx * vx + vy * vy;
    float t = (L2 > 0.0f) ? (wx * vx + wy * vy) / L2 : 0.0f;
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    float cx = a.x + t * vx, cy = a.y + t * vy;
    return Mathf::Sqrt((p.x - cx) * (p.x - cx) + (p.y - cy) * (p.y - cy));
}

// Per-object editor-only Euler-angle cache (degrees) for the inspector.
std::unordered_map<GameObject*, Vec3> g_euler;

// Editable text buffers for script/visual-script components, keyed by pointer.
std::unordered_map<void*, std::vector<char>> g_codeBuf;
std::vector<char>& CodeBuffer(void* key, const std::string& initial) {
    auto it = g_codeBuf.find(key);
    if (it == g_codeBuf.end()) {
        std::vector<char> b(65536, 0);
        std::strncpy(b.data(), initial.c_str(), b.size() - 1);
        it = g_codeBuf.emplace(key, std::move(b)).first;
    }
    return it->second;
}
void SetCodeBuffer(void* key, const std::string& text) {
    auto& b = g_codeBuf[key];
    if (b.size() < 65536) b.assign(65536, 0); else std::fill(b.begin(), b.end(), 0);
    std::strncpy(b.data(), text.c_str(), b.size() - 1);
}

// External-IDE helpers: write/read script files and open them in the system editor.
namespace extide {
namespace fs = std::filesystem;
bool WriteFile(const std::string& path, const std::string& text) {
    std::ofstream f(path);
    if (!f) return false;
    f << text;
    return static_cast<bool>(f);
}
std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
// Reveal a path in the OS file manager (NOT an IDE) — for "Show in Explorer".
void RevealInFiles(const std::string& path) {
#if defined(_WIN32)
    int rc = std::system(("explorer \"" + path + "\"").c_str());
#elif defined(__APPLE__)
    int rc = std::system(("open \"" + path + "\" 2>/dev/null").c_str());
#else
    int rc = std::system(("xdg-open \"" + path + "\" >/dev/null 2>&1").c_str());
#endif
    (void)rc;
}

void OpenExternal(const std::string& path) {
    // Prefer VS Code if present, otherwise the OS default handler.
#if defined(_WIN32)
    int rc = std::system(("code \"" + path + "\"").c_str());
    if (rc != 0) rc = std::system(("start \"\" \"" + path + "\"").c_str());
#elif defined(__APPLE__)
    int rc = std::system(("code \"" + path + "\" 2>/dev/null").c_str());
    if (rc != 0) rc = std::system(("open \"" + path + "\"").c_str());
#else
    int rc = std::system(("code \"" + path + "\" >/dev/null 2>&1").c_str());
    if (rc != 0) rc = std::system(("xdg-open \"" + path + "\" >/dev/null 2>&1").c_str());
#endif
    (void)rc;
}
std::string ExtFor(const std::string& lang) {
    return lang == "lua" ? "lua" : lang == "csharp" ? "cs" : "okay";
}
// A starter script written in the right syntax for each language, so the
// example actually compiles in the selected backend. OkayScript is C-style
// (braces + semicolons); Lua uses function...end; C# is class-based.
const char* StarterScript(const std::string& lang) {
    if (lang == "lua")
        return "function start()\n    set_pos(0, 0)\nend\n\n"
               "function update(dt)\n    move(2 * dt, 0)\nend\n";
    if (lang == "csharp")
        return "class Script {\n    void Start() { Okay.SetPos(0, 0); }\n"
               "    void Update(float dt) { Okay.Move(2 * dt, 0); }\n}\n";
    // OkayScript, written Unity-style but with OkaySpace's own base class,
    // OkaySource — this is the syntax most users expect. The classic
    // function start()/update(dt) style still works.
    return "public class NewScript : OkaySource {\n"
           "    float speed = 2f;\n\n"
           "    void Start() {\n"
           "        transform.position = new Vector3(0, 0, 0);\n"
           "    }\n\n"
           "    void Update() {\n"
           "        transform.position.x += Input.GetAxis(\"Horizontal\") * speed * Time.deltaTime;\n"
           "        transform.position.y += Input.GetAxis(\"Vertical\") * speed * Time.deltaTime;\n"
           "    }\n"
           "}\n";
}
} // namespace extide

// ---- Build Game: export the current scene as a standalone runnable game ----
// A shipped game is just <Game>.exe (a copy of the player runtime) + a
// game.okayscene sitting beside it. Double-clicking the exe runs the scene.
namespace builder {
namespace fs = std::filesystem;

#if defined(_WIN32)
const char* kPlayerExe = "OkaySpacePlayer.exe";
#else
const char* kPlayerExe = "okay-player";
#endif

// Locate the player runtime. Searches next to the editor, then a few sibling
// locations (a launcher layout, a build tree), so it works whether the editor
// was launched from the dist folder or via the launcher.
std::string FindPlayer() {
    fs::path self = updater::SelfPath();
    if (self.empty()) return {};
    fs::path dir = self.parent_path();
#if defined(_WIN32)
    const char* names[] = {"OkaySpacePlayer.exe", "okay-player.exe"};
#else
    const char* names[] = {"okay-player", "OkaySpacePlayer"};
#endif
    std::vector<fs::path> roots = {dir, dir / "bin", dir.parent_path(),
                                   dir / "runtime"};
    for (const fs::path& root : roots)
        for (const char* n : names) {
            std::error_code ec;
            if (fs::exists(root / n, ec)) return (root / n).string();
        }
    return {};
}

// Ensure the player runtime is available, downloading it next to the editor
// from GitHub if it isn't found locally. Returns its path, or "" on failure.
// This is what lets a freshly-downloaded editor build a runnable game with no
// manual setup ("when building, generate everything it needs").
std::string EnsurePlayer(std::string* note) {
    std::string p = FindPlayer();
    if (!p.empty()) return p;
    fs::path self = updater::SelfPath();
    if (self.empty()) return {};
    fs::path dest = self.parent_path() / kPlayerExe;
    if (note) *note = "Fetching the player runtime from GitHub...";
    if (!updater::Download(std::string(updater::kRawBase) + "OkaySpacePlayer.exe",
                           dest.string()))
        return {};
    std::error_code ec;
    if (!fs::exists(dest, ec) || fs::file_size(dest, ec) < 100000) {
        fs::remove(dest, ec);
        return {};
    }
#if !defined(_WIN32)
    fs::permissions(dest, fs::perms::owner_exec | fs::perms::group_exec |
                    fs::perms::others_exec, fs::perm_options::add, ec);
#endif
    if (note) *note = "Downloaded the player runtime to " + dest.string() + ".";
    return dest.string();
}

// Options forwarded from the Build Settings dialog into the build.
struct Options {
    std::string company = "OkaySpace";
    std::string version = "1.0.0";
    int  width = 1280, height = 720;
    bool fullscreen = false, borderless = false, resizable = true, vsync = true;
    bool hideCursor = false;
    int  fpsCap = 0;
    bool quitOnEscape = true;
    float masterVolume = 1.0f;
    bool showFps = false;
    bool includeAllProjectScenes = false, developmentBuild = false;
};

// Build the game into outDir. Returns a human-readable status string.
std::string Build(EditorState& ed, const std::string& outDir,
                  const std::string& gameName, const Options& opt = {}) {
    std::error_code ec;
    fs::path dir(outDir);
    fs::create_directories(dir, ec);
    if (ec) return "Couldn't create folder: " + ec.message();

    // 1) Write the active scene next to the player as game.okayscene (the
    // startup scene), and gather the scene list for the config.
    if (!SceneSerializer::SaveToFile(ed.scene(), (dir / "game.okayscene").string()))
        return "Failed to write game.okayscene.";

    std::vector<std::string> sceneFiles; // filenames placed in the build folder
    sceneFiles.push_back("game.okayscene");
    int extraScenes = 0;
    if (opt.includeAllProjectScenes && !ed.projectDir().empty()) {
        fs::path assets = fs::path(ed.projectDir()) / "Assets";
        if (fs::exists(assets, ec))
            for (auto& e : fs::recursive_directory_iterator(assets, ec)) {
                if (!e.is_regular_file() || e.path().extension() != ".okayscene") continue;
                std::string fn = e.path().filename().string();
                if (fn == "game.okayscene") continue;
                fs::copy_file(e.path(), dir / fn, fs::copy_options::overwrite_existing, ec);
                if (!ec) { sceneFiles.push_back(fn); ++extraScenes; }
            }
    }

    // 1b) Write game.okayconfig (window + scene list) read by the player.
    {
        std::ofstream cf((dir / "game.okayconfig").string());
        cf << "title=" << gameName << "\n";
        cf << "company=" << opt.company << "\n";
        cf << "version=" << opt.version << "\n";
        cf << "width=" << opt.width << "\n";
        cf << "height=" << opt.height << "\n";
        cf << "fullscreen=" << (opt.fullscreen ? 1 : 0) << "\n";
        cf << "borderless=" << (opt.borderless ? 1 : 0) << "\n";
        cf << "resizable=" << (opt.resizable ? 1 : 0) << "\n";
        cf << "vsync=" << (opt.vsync ? 1 : 0) << "\n";
        cf << "cursor=" << (opt.hideCursor ? 0 : 1) << "\n";
        cf << "fps_cap=" << opt.fpsCap << "\n";
        cf << "quit_on_escape=" << (opt.quitOnEscape ? 1 : 0) << "\n";
        cf << "volume=" << opt.masterVolume << "\n";
        cf << "show_fps=" << (opt.showFps ? 1 : 0) << "\n";
        cf << "development=" << (opt.developmentBuild ? 1 : 0) << "\n";
        cf << "startup=game.okayscene\n";
        for (const std::string& s : sceneFiles) cf << "scene=" << s << "\n";
    }

    // 2) Copy the player runtime, renamed to <Game>.exe. If it isn't beside the
    // editor, fetch it from GitHub so the build is self-contained.
    std::string fetchNote;
    std::string player = EnsurePlayer(&fetchNote);
#if defined(_WIN32)
    std::string exeName = gameName + ".exe";
#else
    std::string exeName = gameName;
#endif
    if (player.empty()) {
        return "Wrote game.okayscene to " + dir.string() +
               ", but couldn't find or download the player runtime. Check your "
               "internet connection, or place OkaySpacePlayer next to the editor.";
    }
    fs::copy_file(player, dir / exeName, fs::copy_options::overwrite_existing, ec);
    if (ec) return "Wrote scene but couldn't copy the player: " + ec.message();
#if !defined(_WIN32)
    fs::permissions(dir / exeName, fs::perms::owner_exec | fs::perms::group_exec |
                    fs::perms::others_exec, fs::perm_options::add, ec);
#endif

    // 3) Copy every asset the scene references (textures, WAVs, frames) so the
    // shipped game folder is self-contained, preserving relative subpaths.
    int copied = 0, missing = 0;
    for (const std::string& asset : SceneSerializer::CollectAssetPaths(ed.scene())) {
        fs::path src(asset);
        std::error_code aec;
        if (!fs::exists(src, aec)) { ++missing; continue; }
        fs::path dst = src.is_absolute() ? (dir / src.filename()) : (dir / src);
        if (dst.has_parent_path()) fs::create_directories(dst.parent_path(), aec);
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, aec);
        if (!aec) ++copied;
    }

    std::string msg = "Built '" + gameName + "' to " + dir.string() +
                      " — run " + exeName + " to play.";
    if (!fetchNote.empty() && fetchNote.rfind("Downloaded", 0) == 0)
        msg += "  (runtime fetched from GitHub)";
    if (extraScenes) msg += "  (" + std::to_string(extraScenes + 1) + " scenes)";
    if (copied)  msg += "  (" + std::to_string(copied) + " asset(s) copied)";
    if (missing) msg += "  WARNING: " + std::to_string(missing) +
                        " referenced asset(s) not found and not copied.";
    return msg;
}
} // namespace builder

// ---- Console log -------------------------------------------------------
// A Unity-style console entry: severity, text, a short timestamp, and a repeat
// count (incremented when the same line is logged twice in a row).
struct ConsoleEntry {
    int level = 0;            // 0 = info, 1 = warning, 2 = error
    std::string text;
    std::string time;
    int count = 1;
};
std::vector<ConsoleEntry> g_console;
int g_consoleCounts[3] = {0, 0, 0};   // running totals per level (for the toggles)

void ConsoleLog(const std::string& msg, int level = 0) {
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    // Collapse an immediate repeat of the same message into a count.
    if (!g_console.empty() && g_console.back().text == msg && g_console.back().level == level) {
        g_console.back().count++;
        g_consoleCounts[level]++;
        return;
    }
    char ts[16];
    std::time_t t = std::time(nullptr);
    std::tm* lt = std::localtime(&t);
    std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d", lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0, lt ? lt->tm_sec : 0);
    g_console.push_back({level, msg, ts, 1});
    g_consoleCounts[level]++;
    if (g_console.size() > 500) g_console.erase(g_console.begin());
}
void ConsoleClear() {
    g_console.clear();
    g_consoleCounts[0] = g_consoleCounts[1] = g_consoleCounts[2] = 0;
}

// ---- A dark, Unity-ish theme ------------------------------------------
void ApplyTheme() {
    if (g_theme == 1)      ImGui::StyleColorsLight();
    else if (g_theme == 2) ImGui::StyleColorsClassic();
    else                   ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    // Rounding for a soft, modern look.
    s.WindowRounding    = 7.0f;  s.ChildRounding    = 6.0f;
    s.FrameRounding     = 5.0f;  s.GrabRounding     = 5.0f;
    s.PopupRounding     = 7.0f;  s.TabRounding      = 6.0f;
    s.ScrollbarRounding = 10.0f;
    // Balanced spacing: comfortable but not bloated (slightly roomier than the
    // ImGui defaults). View > UI Scale scales everything for bigger displays.
    s.WindowPadding   = ImVec2(10, 9);  s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing     = ImVec2(8, 7);   s.ItemInnerSpacing = ImVec2(7, 5);
    s.CellPadding     = ImVec2(6, 4);   s.IndentSpacing = 19.0f;
    s.ScrollbarSize   = 13.0f;          s.GrabMinSize = 11.0f;
    s.WindowBorderSize = 0.0f;          s.FrameBorderSize = 0.0f;
    s.PopupBorderSize = 1.0f;           s.SeparatorTextBorderSize = 2.0f;
    s.WindowTitleAlign = ImVec2(0.02f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.DockingSeparatorSize = 2.0f;
    // Apply the global UI scale LAST so every metric above is scaled together.
    if (g_uiScale != 1.0f) s.ScaleAllSizes(g_uiScale);
    ImGui::GetIO().FontGlobalScale = g_uiScale;

    // Light / Classic keep ImGui's own palette (only the metrics above apply).
    if (g_theme != 0) return;

    // A cohesive dark palette with a warm blue accent.
    const ImVec4 accent      = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
    const ImVec4 accentDim   = ImVec4(0.26f, 0.59f, 0.98f, 0.55f);
    const ImVec4 accentHover = ImVec4(0.34f, 0.66f, 1.00f, 1.00f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]         = ImVec4(0.115f, 0.123f, 0.145f, 1.00f);
    c[ImGuiCol_ChildBg]          = ImVec4(0.135f, 0.143f, 0.168f, 0.40f);
    c[ImGuiCol_PopupBg]          = ImVec4(0.10f, 0.107f, 0.128f, 0.98f);
    c[ImGuiCol_Border]           = ImVec4(0.00f, 0.00f, 0.00f, 0.35f);
    c[ImGuiCol_FrameBg]          = ImVec4(0.20f, 0.21f, 0.245f, 1.00f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.26f, 0.28f, 0.32f, 1.00f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.30f, 0.33f, 0.38f, 1.00f);
    c[ImGuiCol_TitleBg]          = ImVec4(0.09f, 0.095f, 0.115f, 1.00f);
    c[ImGuiCol_TitleBgActive]    = ImVec4(0.13f, 0.20f, 0.31f, 1.00f);
    c[ImGuiCol_MenuBarBg]        = ImVec4(0.135f, 0.143f, 0.168f, 1.00f);
    c[ImGuiCol_Header]           = accentDim;
    c[ImGuiCol_HeaderHovered]    = accentHover;
    c[ImGuiCol_HeaderActive]     = accent;
    c[ImGuiCol_Button]           = ImVec4(0.22f, 0.24f, 0.29f, 1.00f);
    c[ImGuiCol_ButtonHovered]    = accentHover;
    c[ImGuiCol_ButtonActive]     = accent;
    c[ImGuiCol_CheckMark]        = accent;
    c[ImGuiCol_SliderGrab]       = accent;
    c[ImGuiCol_SliderGrabActive] = accentHover;
    c[ImGuiCol_Separator]        = ImVec4(0.00f, 0.00f, 0.00f, 0.45f);
    c[ImGuiCol_SeparatorHovered] = accentDim;
    c[ImGuiCol_ResizeGrip]       = accentDim;
    c[ImGuiCol_ResizeGripHovered]= accentHover;
    c[ImGuiCol_Tab]              = ImVec4(0.155f, 0.165f, 0.195f, 1.00f);
    c[ImGuiCol_TabHovered]       = accentHover;
    c[ImGuiCol_TabActive]        = ImVec4(0.20f, 0.32f, 0.50f, 1.00f);
    c[ImGuiCol_TabUnfocused]     = ImVec4(0.135f, 0.143f, 0.168f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.17f, 0.22f, 0.30f, 1.00f);
    c[ImGuiCol_DockingPreview]   = accentDim;
    c[ImGuiCol_TextSelectedBg]   = accentDim;
    c[ImGuiCol_NavHighlight]     = accent;
    c[ImGuiCol_Text]             = ImVec4(0.90f, 0.92f, 0.95f, 1.00f);
    c[ImGuiCol_TextDisabled]     = ImVec4(0.50f, 0.53f, 0.60f, 1.00f);
    c[ImGuiCol_ScrollbarBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.18f);
    c[ImGuiCol_ScrollbarGrab]    = ImVec4(0.32f, 0.35f, 0.42f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.44f, 0.52f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = accent;
    c[ImGuiCol_CheckMark]        = ImVec4(0.40f, 0.80f, 1.00f, 1.00f);
    c[ImGuiCol_DockingEmptyBg]   = ImVec4(0.085f, 0.09f, 0.11f, 1.00f);
}

// Build the default Unity-style dock layout once.
void BuildDefaultLayout(ImGuiID dockId, ImVec2 size) {
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, size);

    ImGuiID center = dockId, left, right, down;
    left  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,  0.18f, nullptr, &center);
    right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
    down  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down,  0.26f, nullptr, &center);

    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Console", down);
    ImGui::DockBuilderDockWindow("Project", down);
    ImGui::DockBuilderDockWindow("Services", down);
    ImGui::DockBuilderDockWindow("Script Editor", down);
    ImGui::DockBuilderDockWindow("Stats", right);
    ImGui::DockBuilderDockWindow("Scene", center);
    ImGui::DockBuilderDockWindow("Game", center);   // tab alongside Scene (Unity-style)
    ImGui::DockBuilderFinish(dockId);
}

// Unity's UI rule: every widget lives under a Canvas, and a scene needs one
// Event System to route pointer input. Ensure both exist and return the Canvas
// GameObject so a freshly created widget can be parented to it.
GameObject* EnsureUIRoot(EditorState& ed) {
    GameObject* canvas = nullptr;
    bool hasEvent = false;
    for (const auto& up : ed.scene().Objects()) {
        if (!canvas && up->GetComponent<Canvas>()) canvas = up.get();
        if (up->GetComponent<EventSystem>()) hasEvent = true;
    }
    if (!canvas) {
        canvas = ed.scene().CreateGameObject("Canvas");
        auto* cv = canvas->AddComponent<Canvas>();
        cv->scaleMode = Canvas::ScaleMode::ScaleWithScreenSize;
    }
    if (!hasEvent) {
        GameObject* es = ed.scene().CreateGameObject("EventSystem");
        es->AddComponent<EventSystem>();
    }
    return canvas;
}

void DrawMenuAndToolbar(EditorState& ed) {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Project...", "Ctrl+N")) g_showNewProject = true;
        if (ImGui::MenuItem("New 2D Scene")) { ed.NewScene2D(); ConsoleLog("New 2D project"); }
        if (ImGui::MenuItem("New 3D Scene")) { ed.NewScene3D(); ConsoleLog("New 3D project"); }
        ImGui::Separator();
        if (ImGui::MenuItem("Open...", "Ctrl+O")) g_showOpen = true;
        if (ImGui::BeginMenu("Open Recent", !g_recent.empty())) {
            for (const std::string& p : g_recent) {
                if (ImGui::MenuItem(p.c_str())) {
                    std::string err;
                    if (ed.Load(p, &err)) { ConsoleLog("Opened " + p); AddRecent(p); }
                    else ConsoleLog("Open failed: " + err);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear")) { g_recent.clear(); SaveRecent(); }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Save", "Ctrl+S")) {
            std::string p = ed.path().empty() ? "scene.okayscene" : ed.path();
            if (ed.Save(p)) { ConsoleLog("Saved " + p); AddRecent(p); ed.Achievement("FIRST_SAVE"); }
            else ConsoleLog("Save failed");
        }
        if (ImGui::MenuItem("Save As...")) {
            std::strncpy(g_pathBuf, ed.path().empty() ? "scene.okayscene" : ed.path().c_str(),
                         sizeof(g_pathBuf) - 1);
            g_showSaveAs = true;
        }
        ImGui::Separator();
        ImGui::MenuItem("Autosave", nullptr, &g_autosave);
        if (ImGui::MenuItem("Build Game...", "Ctrl+B")) g_showBuildGame = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) g_quitRequested = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, ed.CanUndo())) { ed.Undo(); ConsoleLog("Undo"); }
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, ed.CanRedo())) { ed.Redo(); ConsoleLog("Redo"); }
        ImGui::Separator();
        if (ImGui::MenuItem("Project Settings...")) g_showProjectSettings = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Hierarchy", nullptr, &g_showHierarchy);
        ImGui::MenuItem("Game", nullptr, &g_showGame);
        ImGui::MenuItem("Inspector", nullptr, &g_showInspector);
        ImGui::MenuItem("Console", nullptr, &g_showConsole);
        ImGui::MenuItem("Project", nullptr, &g_showProject);
        ImGui::MenuItem("Services", nullptr, &g_showServices);
        ImGui::MenuItem("Script Editor", nullptr, &g_showScriptEditor);
        ImGui::MenuItem("Stats", nullptr, &g_showStats);
        ImGui::MenuItem("Scenes", nullptr, &g_showScenes);
        ImGui::Separator();
        ImGui::MenuItem("Colliders (gizmos)", nullptr, &g_showColliders);
        if (ImGui::MenuItem("Skybox", nullptr, &ed.scene().renderSettings.skybox)) ed.dirty = true;
        ImGui::MenuItem("Clear Console on Play", nullptr, &g_clearConsoleOnPlay);
        ImGui::Separator();
        if (ImGui::BeginMenu("UI Scale")) {
            struct { const char* n; float s; } presets[] = {
                {"Compact (90%)", 0.9f}, {"Normal (100%)", 1.0f},
                {"Comfortable (110%)", 1.1f}, {"Large (125%)", 1.25f}, {"Huge (150%)", 1.5f}};
            for (auto& p : presets)
                if (ImGui::MenuItem(p.n, nullptr, std::fabs(g_uiScale - p.s) < 0.01f))
                    { g_uiScale = p.s; ApplyTheme(); }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Theme")) {
            if (ImGui::MenuItem("Dark", nullptr, g_theme == 0))    { g_theme = 0; ApplyTheme(); }
            if (ImGui::MenuItem("Light", nullptr, g_theme == 1))   { g_theme = 1; ApplyTheme(); }
            if (ImGui::MenuItem("Classic", nullptr, g_theme == 2)) { g_theme = 2; ApplyTheme(); }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Reset Layout")) g_resetLayout = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("GameObject")) {
        bool created = false;
        if (ImGui::MenuItem("Create Empty"))   { ed.CreateEmpty();   ConsoleLog("Created empty GameObject"); created = true; }
        if (ImGui::MenuItem("Create Sprite"))  { ed.CreateSprite();  ConsoleLog("Created Sprite"); created = true; }
        if (ImGui::MenuItem("Create Camera"))  { ed.CreateCamera();  ConsoleLog("Created Camera"); created = true; }
        ImGui::Separator();
        if (ImGui::BeginMenu("3D Object")) {       // every built-in primitive
            if (ImGui::MenuItem("Cube"))      { ed.CreateCube();    ConsoleLog("Created Cube"); created = true; }
            if (ImGui::MenuItem("Sphere"))    { ed.CreateMesh("Sphere");    ConsoleLog("Created Sphere"); created = true; }
            if (ImGui::MenuItem("Cylinder"))  { ed.CreateMesh("Cylinder");  ConsoleLog("Created Cylinder"); created = true; }
            if (ImGui::MenuItem("Capsule"))   { ed.CreateMesh("Capsule");   ConsoleLog("Created Capsule"); created = true; }
            if (ImGui::MenuItem("Cone"))      { ed.CreateMesh("Cone");      ConsoleLog("Created Cone"); created = true; }
            if (ImGui::MenuItem("Plane"))     { ed.CreateMesh("Plane");     ConsoleLog("Created Plane"); created = true; }
            if (ImGui::MenuItem("Pyramid"))   { ed.CreatePyramid(); ConsoleLog("Created Pyramid"); created = true; }
            if (ImGui::MenuItem("Wedge"))     { ed.CreateMesh("Wedge");     ConsoleLog("Created Wedge"); created = true; }
            if (ImGui::MenuItem("Torus"))     { ed.CreateMesh("Torus");     ConsoleLog("Created Torus"); created = true; }
            if (ImGui::MenuItem("Icosphere")) { ed.CreateMesh("Icosphere"); ConsoleLog("Created Icosphere"); created = true; }
            if (ImGui::MenuItem("Quad"))      { ed.CreateMesh("Quad");      ConsoleLog("Created Quad"); created = true; }
            ImGui::Separator();
            if (ImGui::MenuItem("Terrain")) {
                GameObject* go = ed.CreateEmpty("Terrain");
                auto* tr = go->AddComponent<Terrain>();
                tr->Resize(32); tr->size = 50.0f;
                tr->Hills(6, 6.0f, (unsigned)ImGui::GetTime());   // start with gentle hills
                tr->Apply();
                ed.Select(go); ed.view3D = true; ed.dirty = true; created = true;
                ConsoleLog("Created Terrain");
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Create Directional Light")) {
            GameObject* go = ed.CreateEmpty("Directional Light");
            go->AddComponent<Light>();
            go->transform->localRotation = Quat::Euler({50, -30, 0}); // angled key light
            ed.Select(go); ConsoleLog("Created Directional Light"); created = true;
        }
        if (ImGui::MenuItem("Create Point Light")) {
            GameObject* go = ed.CreateEmpty("Point Light");
            auto* l = go->AddComponent<Light>(); l->type = Light::Type::Point; l->range = 12.0f;
            go->transform->localPosition = {0, 3, 0};
            ed.Select(go); ConsoleLog("Created Point Light"); created = true;
        }
        if (ImGui::MenuItem("Create Spot Light")) {
            GameObject* go = ed.CreateEmpty("Spot Light");
            auto* l = go->AddComponent<Light>(); l->type = Light::Type::Spot; l->range = 16.0f; l->spotAngle = 50.0f;
            go->transform->localPosition = {0, 5, 0};
            go->transform->localRotation = Quat::Euler({90, 0, 0}); // aim down
            ed.Select(go); ConsoleLog("Created Spot Light"); created = true;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Create Particle System")) {
            GameObject* go = ed.CreateEmpty("Particles");
            go->AddComponent<ParticleSystem>();
            ConsoleLog("Created Particle System (press Play to see it)");
            created = true;
        }
        if (ImGui::MenuItem("Create Tilemap")) {
            GameObject* go = ed.CreateEmpty("Tilemap");
            auto* tm = go->AddComponent<Tilemap>();
            tm->Resize(12, 8);
            for (int x = 0; x < 12; ++x) tm->SetTile(x, 0, 1); // a ground row
            ConsoleLog("Created Tilemap");
            created = true;
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("UI")) {   // each UI element is its own GameObject
            // Create a widget under the scene's Canvas (making one — plus an
            // Event System — if missing), exactly like Unity.
            auto addUI = [&](const char* nm, auto build) {
                GameObject* root = EnsureUIRoot(ed);
                GameObject* g = ed.CreateEmpty(nm);
                build(g);
                if (root) g->transform->SetParent(root->transform, /*worldPositionStays=*/false);
                // Spawn centered on screen (not the top-left corner) so it's visible.
                UIRect r = GetUIRect(g);
                if (r.valid && r.anchorPtr && r.position) { *r.anchorPtr = UIAnchor::Center; *r.position = {0, 0}; }
                ed.Select(g); ed.dirty = true; created = true;
            };
            if (ImGui::MenuItem("Canvas"))       { ed.Select(ed.CreateEmpty("Canvas"));   ed.selected()->AddComponent<Canvas>();        ed.dirty = true; created = true; }
            if (ImGui::MenuItem("Event System")) { ed.Select(ed.CreateEmpty("EventSystem")); ed.selected()->AddComponent<EventSystem>();  ed.dirty = true; created = true; }
            if (ImGui::MenuItem("UI Document"))  {
                GameObject* root = EnsureUIRoot(ed);
                GameObject* g = ed.CreateEmpty("UIDocument");
                auto* doc = g->AddComponent<UIDocument>();
                doc->markup =
                    "panel pos=40,40 size=360,240 color=30,36,52,220\n"
                    "  text \"MY GAME\" pos=70,70 size=5\n"
                    "  button \"Play\" pos=70,170 size=300,60 anchor=topleft\n";
                if (root) g->transform->SetParent(root->transform, false);
                doc->Rebuild(); ed.scene().Update(0.0f);
                ed.Select(g); ed.dirty = true; created = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Button"))       addUI("Button",      [](GameObject* g){ g->AddComponent<UIButton>(); });
            if (ImGui::MenuItem("Panel"))        addUI("Panel",       [](GameObject* g){ g->AddComponent<UIPanel>(); });
            if (ImGui::MenuItem("Image"))        addUI("Image",       [](GameObject* g){ g->AddComponent<UIImage>(); });
            if (ImGui::MenuItem("Text"))         addUI("Text",        [](GameObject* g){ auto* t = g->AddComponent<TextRenderer>(); t->screenSpace = true; t->pixelSize = 3.0f; t->align = 1; });
            if (ImGui::MenuItem("Progress Bar")) addUI("ProgressBar", [](GameObject* g){ g->AddComponent<UIProgressBar>(); });
            if (ImGui::MenuItem("Slider"))       addUI("Slider",      [](GameObject* g){ g->AddComponent<UISlider>(); });
            if (ImGui::MenuItem("Toggle"))       addUI("Toggle",      [](GameObject* g){ g->AddComponent<UIToggle>(); });
            if (ImGui::MenuItem("Input Field"))  addUI("InputField",  [](GameObject* g){ g->AddComponent<UIInputField>(); });
            if (ImGui::MenuItem("Dropdown"))     addUI("Dropdown",    [](GameObject* g){ g->AddComponent<UIDropdown>(); });
            if (ImGui::MenuItem("Scroll View"))  addUI("ScrollView",  [](GameObject* g){ g->AddComponent<UIScrollView>(); });
            if (ImGui::MenuItem("Layout Group")) addUI("Layout",      [](GameObject* g){ g->AddComponent<UILayoutGroup>(); });
            if (ImGui::MenuItem("Tooltip"))      addUI("Tooltip",     [](GameObject* g){ g->AddComponent<UIPanel>(); g->AddComponent<UITooltip>(); });
            ImGui::EndMenu();
        }
        if (created) ed.Achievement("FIRST_OBJECT");
        ImGui::Separator();
        if (ImGui::MenuItem("Instantiate Prefab...")) g_showInstPrefab = true;
        if (ImGui::MenuItem("Duplicate Selected", "Ctrl+D", false, ed.selected() != nullptr)) {
            ed.DuplicateSelected(); ConsoleLog("Duplicated selection");
        }
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, ed.selected() != nullptr)) {
            g_clipboard = SceneSerializer::SerializeObject(*ed.selected());
            ConsoleLog("Copied " + ed.selected()->name);
        }
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !g_clipboard.empty())) {
            ed.PushUndo();
            if (GameObject* go = SceneSerializer::InstantiateFromText(ed.scene(), g_clipboard)) {
                go->transform->localPosition += Vec3{0.5f, 0.5f, 0.0f};
                ed.Select(go); ed.dirty = true; ConsoleLog("Pasted " + go->name);
            }
        }
        if (ImGui::MenuItem("Delete Selected", "Del", false, ed.selected() != nullptr)) {
            ed.DeleteSelected(); ConsoleLog("Deleted selection");
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Engine")) {
        if (ImGui::MenuItem("Build Game...", "Ctrl+B")) g_showBuildGame = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Check for Updates...")) {
            g_update = updater::CheckLatest();
            g_openUpdatePopup = true;
            ConsoleLog(g_update.error.empty()
                ? (g_update.available ? "Update available: v" + g_update.latest
                                      : "Up to date (v" + g_update.current + ").")
                : "Update check failed: " + g_update.error);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Scripting Reference")) g_showScriptDocs = true;
        if (ImGui::MenuItem("About OkaySpace")) g_openAbout = true;
        ImGui::EndMenu();
    }

    // Centered Play / Stop / Step controls (Unity-style toolbar), color-coded.
    float btnW = 64.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() * 0.5f - btnW);
    if (!ed.isPlaying()) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.55f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.70f, 0.32f, 1.0f));
        if (ImGui::Button(">  Play", ImVec2(btnW, 0))) {
            if (g_clearConsoleOnPlay) ConsoleClear();
            ed.Play(); g_paused = false; ConsoleLog("Play"); ed.Achievement("HIT_PLAY");
            g_showGame = true; g_focusGameOnPlay = true; // jump to the Game tab
        }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play (Space) — runs the scene in the Game view");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.22f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.28f, 0.28f, 1.0f));
        if (ImGui::Button("[]  Stop", ImVec2(btnW, 0))) { ed.Stop(); g_paused = false; ConsoleLog("Stop"); }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop (Space) — return to the edit state");
        ImGui::SameLine();
        if (g_paused) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.45f, 0.15f, 1.0f));
            if (ImGui::Button("Resume", ImVec2(64, 0))) g_paused = false;
            ImGui::PopStyleColor();
        } else {
            if (ImGui::Button("Pause", ImVec2(64, 0))) g_paused = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pause / Resume the simulation");
    }
    ImGui::SameLine();
    if (ImGui::Button("Step", ImVec2(50, 0))) { g_paused = true; ed.Tick(1.0f / 60.0f); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Advance one frame (pauses first)");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.34f, 0.62f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.52f, 0.42f, 0.75f, 1.0f));
    if (ImGui::Button("Build", ImVec2(54, 0))) g_showBuildGame = true;
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Build Game (Ctrl+B) — export a standalone exe");

    // Right-aligned status.
    char status[96];
    const char* mode = !ed.isPlaying() ? "EDIT" : (g_paused ? "PAUSED" : "PLAYING");
    std::snprintf(status, sizeof(status), "v%s   %s   %.0f FPS", OKAY_ENGINE_VERSION,
                  mode, ImGui::GetIO().Framerate);
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(status).x - 16);
    ImGui::TextColored(ed.isPlaying() ? ImVec4(0.4f, 0.9f, 0.4f, 1) : ImVec4(0.7f, 0.7f, 0.7f, 1),
                       "%s", status);
    ImGui::EndMenuBar();
}

// Full-window host that hosts the dockspace + menu/toolbar.
void DrawDockSpace(EditorState& ed) {
    static bool first = true;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_MenuBar;
    ImGui::Begin("##OkayDockHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockId = ImGui::GetID("OkayDockSpace");
    if (first || g_resetLayout) {
        first = false;
        BuildDefaultLayout(dockId, vp->WorkSize);
        if (g_resetLayout) {
            // Reopen every panel so a reset always restores the full workspace.
            g_showHierarchy = g_showInspector = g_showConsole = g_showProject =
                g_showServices = g_showScriptEditor = g_showStats = g_showGame = true;
            g_resetLayout = false;
        }
    }
    ImGui::DockSpace(dockId, ImVec2(0, 0), ImGuiDockNodeFlags_None);

    DrawMenuAndToolbar(ed);
    ImGui::End();
}

// A Unity-style console: severity icons + colors, Collapse, per-level filter
// toggles with counts, search, a selectable list, and a details pane.
void DrawConsole() {
    static char filter[96] = "";
    static bool autoScroll = true;
    static bool collapse = false;
    static bool showInfo = true, showWarn = true, showError = true;
    static int  selected = -1;

    if (ImGui::Begin("Console", &g_showConsole)) {
        if (ImGui::Button("Clear")) { ConsoleClear(); selected = -1; }
        ImGui::SameLine();
        ImGui::Checkbox("Collapse", &collapse);
        ImGui::SameLine();
        ImGui::Checkbox("Clear on Play", &g_clearConsoleOnPlay);
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &autoScroll);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160);
        ImGui::InputTextWithHint("##cfilter", "Search", filter, sizeof(filter));

        // Right-aligned per-level toggle buttons with counts (like Unity).
        const ImVec4 cInfo(0.82f, 0.84f, 0.88f, 1.0f);
        const ImVec4 cWarn(0.95f, 0.80f, 0.35f, 1.0f);
        const ImVec4 cErr (0.96f, 0.45f, 0.42f, 1.0f);
        auto toggle = [](const char* label, bool& on, const ImVec4& col, int count) {
            char buf[32]; std::snprintf(buf, sizeof(buf), "%s %d", label, count);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImVec4 bg = ImGui::GetStyleColorVec4(on ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg);
            ImGui::PushStyleColor(ImGuiCol_Button, bg);
            if (ImGui::Button(buf)) on = !on;
            ImGui::PopStyleColor(2);
        };
        float btnW = 168.0f;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - btnW);
        toggle("i", showInfo, cInfo, g_consoleCounts[0]); ImGui::SameLine();
        toggle("!", showWarn, cWarn, g_consoleCounts[1]); ImGui::SameLine();
        toggle("x", showError, cErr, g_consoleCounts[2]);
        ImGui::Separator();

        std::string needle = filter;
        for (auto& n : needle) n = (char)std::tolower((unsigned char)n);
        const ImVec4 levelCol[3] = {cInfo, cWarn, cErr};
        const char*  levelIcon[3] = {"[i]", "[!]", "[x]"};
        const bool   levelShow[3] = {showInfo, showWarn, showError};

        // Details pane reserves the bottom; the list fills the rest.
        float detailsH = 90.0f;
        ImGui::BeginChild("log", ImVec2(0, -detailsH));
        // Optional collapse: merge identical (level,text) lines, summing counts.
        std::vector<int> order;          // indices into g_console to display
        std::vector<int> mergedCount;    // parallel display counts
        if (collapse) {
            std::vector<int> firstAt;    // first display index for a (level|text)
            std::unordered_map<std::string, int> seen;
            for (int i = 0; i < (int)g_console.size(); ++i) {
                const auto& e = g_console[i];
                std::string key = std::to_string(e.level) + "|" + e.text;
                auto it = seen.find(key);
                if (it == seen.end()) { seen[key] = (int)order.size(); order.push_back(i); mergedCount.push_back(e.count); }
                else mergedCount[it->second] += e.count;
            }
        } else {
            for (int i = 0; i < (int)g_console.size(); ++i) { order.push_back(i); mergedCount.push_back(g_console[i].count); }
        }

        for (std::size_t row = 0; row < order.size(); ++row) {
            int i = order[row];
            const ConsoleEntry& e = g_console[i];
            if (!levelShow[e.level]) continue;
            if (!needle.empty()) {
                std::string low = e.text;
                for (auto& ch : low) ch = (char)std::tolower((unsigned char)ch);
                if (low.find(needle) == std::string::npos) continue;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, levelCol[e.level]);
            std::string label = std::string(levelIcon[e.level]) + " " + e.time + "  " + e.text;
            if (mergedCount[row] > 1) label += "  (" + std::to_string(mergedCount[row]) + ")";
            label += "##c" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), selected == i)) selected = i;
            ImGui::PopStyleColor();
        }
        if (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::BeginChild("details", ImVec2(0, 0));
        if (selected >= 0 && selected < (int)g_console.size()) {
            const ConsoleEntry& e = g_console[selected];
            ImGui::PushStyleColor(ImGuiCol_Text, levelCol[e.level]);
            ImGui::TextWrapped("%s", e.text.c_str());
            ImGui::PopStyleColor();
            ImGui::TextDisabled("%s at %s", e.level == 2 ? "Error" : e.level == 1 ? "Warning" : "Info", e.time.c_str());
        } else {
            ImGui::TextDisabled("Select a log entry to see details.");
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

// Lowercase a copy of a string (for case-insensitive compares).
static std::string Lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Visual kind (icon color + short letter) for an asset, by extension.
struct AssetKind { ImU32 col; const char* letter; };
static AssetKind KindOf(const std::string& extLower, bool isDir) {
    if (isDir) return {IM_COL32(225, 200, 95, 255), "DIR"};
    if (extLower == ".okayscene")  return {IM_COL32(90, 150, 240, 255), "SCN"};
    if (extLower == ".okayprefab") return {IM_COL32(80, 210, 225, 255), "PFB"};
    if (extLower == ".png" || extLower == ".jpg" || extLower == ".jpeg" ||
        extLower == ".bmp" || extLower == ".tga") return {IM_COL32(110, 200, 120, 255), "IMG"};
    if (extLower == ".wav")        return {IM_COL32(230, 160, 70, 255), "WAV"};
    if (extLower == ".okay" || extLower == ".lua" || extLower == ".cs")
        return {IM_COL32(185, 140, 235, 255), "CS"};
    if (extLower == ".obj")        return {IM_COL32(150, 175, 210, 255), "OBJ"};
    if (extLower == ".okayvs")     return {IM_COL32(150, 200, 170, 255), "VS"};
    return {IM_COL32(150, 152, 162, 255), "•"};
}

// Move an asset (file or folder) into a destination folder (drag-and-drop).
static void MoveAssetInto(const std::string& src, const std::filesystem::path& destDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path s(src), d = destDir / fs::path(src).filename();
    if (s.empty() || s == d || s.parent_path() == destDir || !fs::is_directory(destDir, ec)) return;
    fs::rename(s, d, ec);
    ConsoleLog(ec ? ("Move failed: " + ec.message())
                  : ("Moved " + s.filename().string() + " -> " + destDir.filename().string()));
}

// A drop target on the last-drawn item that accepts an asset path and moves it
// into `destDir`.
static void AssetDropTarget(const std::filesystem::path& destDir) {
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH"))
            MoveAssetInto(std::string((const char*)p->Data), destDir);
        ImGui::EndDragDropTarget();
    }
}

// Recursive folder tree (left pane). Clicking a node makes it the current dir.
static void DrawFolderTree(const std::filesystem::path& dir, char* dirBuf, std::size_t bufsz) {
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> subs;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.is_directory(ec)) subs.push_back(e.path());
    std::sort(subs.begin(), subs.end(), [](const fs::path& a, const fs::path& b) {
        return Lower(a.filename().string()) < Lower(b.filename().string());
    });
    for (const fs::path& s : subs) {
        bool hasKids = false;
        std::error_code e2;
        for (auto& c : fs::directory_iterator(s, e2)) { if (c.is_directory(e2)) { hasKids = true; break; } }
        ImGuiTreeNodeFlags f = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!hasKids) f |= ImGuiTreeNodeFlags_Leaf;
        if (fs::path(dirBuf) == s) f |= ImGuiTreeNodeFlags_Selected;
        bool open = ImGui::TreeNodeEx(s.filename().string().c_str(), f);
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            std::strncpy(dirBuf, s.string().c_str(), bufsz - 1);
        AssetDropTarget(s);   // drop assets onto a folder to move them in
        if (open) { DrawFolderTree(s, dirBuf, bufsz); ImGui::TreePop(); }
    }
}

void DrawProject(EditorState& ed) {
    namespace fs = std::filesystem;
    static char dirBuf[512] = ".";
    static char search[96] = "";
    static std::string selected;        // currently selected asset path
    static std::string lastProject;     // re-home the browser when a project opens
    if (!ImGui::Begin("Project", &g_showProject)) { ImGui::End(); return; }
    // Roomier spacing for the Project panel (it was too compact).
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  ImVec2(8, 8));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 5));

    std::error_code ec;
    fs::path root = ed.projectDir().empty() ? fs::absolute(".", ec)
                                            : fs::path(ed.projectDir()) / "Assets";
    if (!ed.projectDir().empty() && ed.projectDir() != lastProject) {
        lastProject = ed.projectDir();
        std::strncpy(dirBuf, root.string().c_str(), sizeof(dirBuf) - 1);
    }
    fs::path dir(dirBuf);

    // ---- Top bar: breadcrumb + search --------------------------------
    if (ImGui::SmallButton("Assets")) std::strncpy(dirBuf, root.string().c_str(), sizeof(dirBuf) - 1);
    {   // breadcrumb of folders between root and the current dir
        fs::path rel = fs::relative(dir, root, ec);
        if (!ec && !rel.empty() && rel.native()[0] != '.') {
            fs::path acc = root;
            for (const auto& part : rel) {
                acc /= part;
                ImGui::SameLine(0, 4); ImGui::TextDisabled("/"); ImGui::SameLine(0, 4);
                if (ImGui::SmallButton(part.string().c_str()))
                    std::strncpy(dirBuf, acc.string().c_str(), sizeof(dirBuf) - 1);
            }
        }
    }
    ImGui::SameLine();
    float sw = 180.0f;
    ImGui::SameLine(ImGui::GetContentRegionMax().x - sw);
    ImGui::SetNextItemWidth(sw);
    ImGui::InputTextWithHint("##search", "search...", search, sizeof(search));
    ImGui::Separator();

    // ---- Two panes: folder tree (left) + asset grid (right) ----------
    ImGui::BeginChild("tree", ImVec2(180, 0), true);
    ImGuiTreeNodeFlags rootF = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (fs::path(dirBuf) == root) rootF |= ImGuiTreeNodeFlags_Selected;
    bool ro = ImGui::TreeNodeEx("Assets##root", rootF);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        std::strncpy(dirBuf, root.string().c_str(), sizeof(dirBuf) - 1);
    AssetDropTarget(root);
    if (ro) { DrawFolderTree(root, dirBuf, sizeof(dirBuf)); ImGui::TreePop(); }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("grid", ImVec2(0, 0), true);

    // Asset operations toolbar + the deferred rename target.
    static std::string renameTarget;
    static char renameBuf[256] = "";
    auto uniquePath = [&](const std::string& base, const std::string& ext) {
        std::error_code ue;
        fs::path p = dir / (base + ext);
        for (int n = 1; fs::exists(p, ue); ++n) p = dir / (base + std::to_string(n) + ext);
        return p;
    };
    // Open the rename dialog on a freshly-created asset, so users name it on the
    // spot (Unity-style "create, then type the name").
    auto nameOnCreate = [&](const fs::path& p) {
        renameTarget = p.string();
        std::string fn = p.filename().string();
        std::strncpy(renameBuf, fn.c_str(), sizeof(renameBuf) - 1);
        renameBuf[sizeof(renameBuf) - 1] = '\0';
        selected = p.string();
    };
    bool canEdit = fs::is_directory(dir, ec);
    ImGui::BeginDisabled(!canEdit);
    if (ImGui::SmallButton("+ Folder")) {
        std::error_code ce; fs::path p = uniquePath("New Folder", "");
        if (!fs::create_directory(p, ce) ? false : true) nameOnCreate(p);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Script")) {
        fs::path p = uniquePath("NewScript", ".okay");
        std::ofstream(p) << extide::StarterScript("okayscript");
        ConsoleLog("Created " + p.string());
        nameOnCreate(p);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Material")) {
        fs::path p = uniquePath("New Material", ".okaymat");
        if (Material{}.SaveToFile(p.string())) { ConsoleLog("Created " + p.string()); nameOnCreate(p); }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Scene")) {
        fs::path p = uniquePath("New Scene", ".okayscene");
        okay::Scene empty("New Scene");
        if (SceneSerializer::SaveToFile(empty, p.string())) { ConsoleLog("Created " + p.string()); nameOnCreate(p); }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(F2 or right-click to Rename)");
    ImGui::Separator();

    std::vector<fs::directory_entry> entries;
    if (fs::is_directory(dir, ec))
        for (auto& e : fs::directory_iterator(dir, ec)) entries.push_back(e);
    std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
        bool da = a.is_directory(), db = b.is_directory();
        if (da != db) return da;     // folders first
        return Lower(a.path().filename().string()) < Lower(b.path().filename().string());
    });

    std::string needle = Lower(search);
    const float cell = 76.0f;
    float availW = ImGui::GetContentRegionAvail().x;
    int cols = (int)(availW / (cell + ImGui::GetStyle().ItemSpacing.x));
    if (cols < 1) cols = 1;

    int shown = 0, col = 0;
    for (auto& e : entries) {
        std::error_code de;
        bool isDir = e.is_directory(de);
        std::string name = e.path().filename().string();
        if (!needle.empty() && Lower(name).find(needle) == std::string::npos) continue;
        std::string ext = Lower(e.path().extension().string());
        AssetKind k = KindOf(ext, isDir);
        std::string full = e.path().string();

        ImGui::PushID(shown);
        ImGui::BeginGroup();
        ImVec4 cv = ImGui::ColorConvertU32ToFloat4(k.col);
        SDL_Texture* thumb = (std::string(k.letter) == "IMG") ? GetThumb(full) : nullptr;
        if (thumb) {
            // Real image preview; tint the frame when selected.
            if (selected == full) ImGui::PushStyleColor(ImGuiCol_Button, cv);
            if (ImGui::ImageButton("##thumb", (ImTextureID)thumb, ImVec2(cell, cell)))
                selected = full;
            if (selected == full) ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(cv.x * 0.5f, cv.y * 0.5f, cv.z * 0.5f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, cv);
            if (selected == full) ImGui::PushStyleColor(ImGuiCol_Button, cv);
            ImGui::Button(k.letter, ImVec2(cell, cell));
            if (selected == full) ImGui::PopStyleColor();
            ImGui::PopStyleColor(2);
        }
        bool hov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) selected = full;
        bool dbl = hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

        // Drag this asset; drop it onto a folder (here or in the tree) to move it.
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("ASSET_PATH", full.c_str(), full.size() + 1);
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }
        if (isDir) AssetDropTarget(e.path());

        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cell);
        ImGui::TextWrapped("%s", name.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name.c_str());

        // Per-item right-click: Open / Rename / Delete.
        if (ImGui::BeginPopupContextItem("itemctx")) {
            selected = full;
            if (ImGui::MenuItem("Open")) dbl = true;
            if (ImGui::MenuItem("Rename")) {
                renameTarget = full;
                std::strncpy(renameBuf, name.c_str(), sizeof(renameBuf) - 1);
                renameBuf[sizeof(renameBuf) - 1] = '\0';
            }
            if (ImGui::MenuItem("Show in Explorer"))
                extide::RevealInFiles(fs::path(full).parent_path().string());
            if (ImGui::MenuItem("Delete")) {
                std::error_code re; fs::remove_all(full, re);
                ConsoleLog((re ? "Delete failed: " : "Deleted ") + full);
                if (selected == full) selected.clear();
            }
            ImGui::EndPopup();
        }

        if (dbl) {
            if (isDir) std::strncpy(dirBuf, full.c_str(), sizeof(dirBuf) - 1);
            else if (ext == ".okayscene") {
                std::string err;
                if (ed.Load(full, &err)) ConsoleLog("Opened " + full);
                else ConsoleLog("Open failed: " + err);
            } else if (ext == ".okayprefab") {
                ed.PushUndo();
                std::string err;
                if (GameObject* r = SceneSerializer::InstantiateFromFile(ed.scene(), full, &err)) {
                    ed.Select(r); ed.dirty = true; ConsoleLog("Instantiated " + full);
                } else ConsoleLog("Prefab load failed: " + err);
            } else if (ext == ".okaydata") {
                g_dataAssetPath = full; g_dataAssetOpen = true;
            } else if (ext == ".okaymat") {
                g_matAssetPath = full; g_matAssetOpen = true;
            } else if (ext == ".okay" || ext == ".lua" || ext == ".cs" || ext == ".okayvs") {
                extide::OpenExternal(full);
            }
        }

        ImGui::PopID();
        if (++col < cols) ImGui::SameLine();
        else col = 0;
        ++shown;
    }
    if (shown == 0) {
        ImGui::TextDisabled(ed.projectDir().empty()
            ? "No project open — File > New Project to create one."
            : "Empty folder.");
    }

    // Right-click empty space: create assets here / reveal the folder.
    if (ImGui::BeginPopupContextWindow("bgctx",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (canEdit) {
            if (ImGui::MenuItem("New Folder")) { std::error_code ce; fs::path p = uniquePath("New Folder", ""); if (fs::create_directory(p, ce)) nameOnCreate(p); }
            if (ImGui::MenuItem("New Script")) {
                fs::path p = uniquePath("NewScript", ".okay");
                std::ofstream(p) << extide::StarterScript("okayscript");
                ConsoleLog("Created " + p.string());
                nameOnCreate(p);
            }
            if (ImGui::MenuItem("New Data Asset")) {   // Scriptable Object
                fs::path p = uniquePath("NewData", ".okaydata");
                std::ofstream(p) << "# Scriptable Object: key = value fields\n"
                                    "name = Item\n"
                                    "value = 10\n";
                ConsoleLog("Created " + p.string());
                nameOnCreate(p);
            }
            if (ImGui::MenuItem("New Material")) {
                fs::path p = uniquePath("New Material", ".okaymat");
                if (Material{}.SaveToFile(p.string())) { ConsoleLog("Created " + p.string()); nameOnCreate(p); }
            }
            if (ImGui::MenuItem("New Scene")) {
                fs::path p = uniquePath("New Scene", ".okayscene");
                okay::Scene empty("New Scene");
                if (SceneSerializer::SaveToFile(empty, p.string())) { ConsoleLog("Created " + p.string()); nameOnCreate(p); }
            }
            ImGui::Separator();
        }
        if (ImGui::MenuItem("Show in Explorer")) extide::RevealInFiles(dir.string());
        ImGui::EndPopup();
    }

    // F2 renames the selected asset (Explorer/Unity-style) when the Project
    // window is focused and nothing else is being typed.
    if (renameTarget.empty() && !selected.empty() &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        renameTarget = selected;
        std::string fn = fs::path(selected).filename().string();
        std::strncpy(renameBuf, fn.c_str(), sizeof(renameBuf) - 1);
        renameBuf[sizeof(renameBuf) - 1] = '\0';
    }

    // Rename modal (opened from an item's context menu). Only open once — calling
    // OpenPopup every frame resets the popup so its buttons never register.
    if (!renameTarget.empty() && !ImGui::IsPopupOpen("Rename Asset")) ImGui::OpenPopup("Rename Asset");
    if (ImGui::BeginPopupModal("Rename Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputText("New name", renameBuf, sizeof(renameBuf),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        bool ok = ImGui::Button("Rename", ImVec2(110, 0)) || enter;
        ImGui::SameLine();
        bool cancel = ImGui::Button("Cancel", ImVec2(110, 0));
        if (ok && renameBuf[0]) {
            fs::path src(renameTarget), dst = fs::path(renameTarget).parent_path() / renameBuf;
            std::error_code re; fs::rename(src, dst, re);
            ConsoleLog((re ? "Rename failed: " : "Renamed to ") + dst.string());
            if (selected == renameTarget) selected = dst.string();
            renameTarget.clear(); ImGui::CloseCurrentPopup();
        } else if (cancel) { renameTarget.clear(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::End();
}

// The online services that ship inside the engine: Steam and Multiplayer.
void DrawServices(EditorState& ed) {
    if (!ImGui::Begin("Services", &g_showServices)) { ImGui::End(); return; }

    // ---- Steam ----
    if (ImGui::CollapsingHeader("Steam", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (auto* s = ed.steam()) {
            ImGui::Text("Backend: %s%s", s->BackendName(),
                        s->IsAvailable() ? " (live)" : " (simulation)");
            ImGui::Text("User: %s    Friends: %d", s->UserName().c_str(), s->FriendCount());

            ImGui::SeparatorText("Achievements");
            static char ach[64] = "MY_ACHIEVEMENT";
            ImGui::SetNextItemWidth(180);
            ImGui::InputText("##ach", ach, sizeof(ach));
            ImGui::SameLine();
            if (ImGui::Button("Unlock")) { s->UnlockAchievement(ach); s->StoreStats(); }
            ImGui::SameLine();
            if (ImGui::Button("Clear")) s->ClearAchievement(ach);
            const char* known[] = {"FIRST_OBJECT", "FIRST_SAVE", "HIT_PLAY"};
            for (const char* a : known)
                ImGui::BulletText("%s: %s", a,
                    s->IsAchievementUnlocked(a) ? "unlocked" : "locked");

            ImGui::SeparatorText("Stats");
            static char stat[64] = "kills";
            ImGui::SetNextItemWidth(140);
            ImGui::InputText("##stat", stat, sizeof(stat));
            ImGui::SameLine();
            if (ImGui::Button("+1")) { s->IncrementStat(stat, 1.0f); s->StoreStats(); }
            ImGui::SameLine();
            ImGui::Text("= %.0f", s->GetStat(stat));

            ImGui::SeparatorText("Leaderboard");
            static char board[64] = "high_score";
            static int  lbScore = 100;
            ImGui::SetNextItemWidth(140);
            ImGui::InputText("##board", board, sizeof(board));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90);
            ImGui::InputInt("##lbscore", &lbScore);
            ImGui::SameLine();
            if (ImGui::Button("Submit##lb")) s->UploadLeaderboardScore(board, lbScore);
            for (const auto& e : s->DownloadLeaderboardTop(board, 5))
                ImGui::BulletText("#%d  %s  %d", e.rank, e.name.c_str(), e.score);

            ImGui::SeparatorText("Steam Cloud");
            static char cloudFile[64] = "save.dat";
            static char cloudData[128] = "level=3";
            ImGui::SetNextItemWidth(120);
            ImGui::InputText("File##cf", cloudFile, sizeof(cloudFile));
            ImGui::SetNextItemWidth(200);
            ImGui::InputText("Data##cd", cloudData, sizeof(cloudData));
            if (ImGui::Button("Write##cloud")) s->CloudWrite(cloudFile, cloudData);
            ImGui::SameLine();
            if (ImGui::Button("Read##cloud")) {
                std::string d = s->CloudRead(cloudFile);
                std::strncpy(cloudData, d.c_str(), sizeof(cloudData) - 1);
                cloudData[sizeof(cloudData) - 1] = '\0';
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete##cloud")) s->CloudDelete(cloudFile);
            ImGui::SameLine();
            ImGui::TextDisabled(s->CloudHasFile(cloudFile) ? "(exists)" : "(none)");

            if (ImGui::Button("Open Overlay")) s->ActivateOverlay("friends");
        } else ImGui::TextDisabled("unavailable");
    }

    // ---- Multiplayer ----
    if (ImGui::CollapsingHeader("Multiplayer", ImGuiTreeNodeFlags_DefaultOpen)) {
        static int port = 45000;
        static char host[64] = "127.0.0.1";
        static char pname[48] = "Player";
        static char srvName[64] = "My Server";
        static char srvPass[48] = "";
        static int  maxP = 8;
        static float tick = 20.0f;
        // Apply the host settings to the live NetworkManager (used on Host).
        auto applyHostSettings = [&]() {
            if (auto* n = ed.net()) {
                n->SetLocalName(pname);
                n->serverName = srvName; n->password = srvPass;
                n->maxPlayers = maxP; n->snapshotRate = tick;
            }
        };
        ImGui::SetNextItemWidth(110);
        ImGui::InputInt("Port", &port);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ImGui::InputText("Name", pname, sizeof(pname));

        ImGui::SeparatorText("Host a game");
        ImGui::SetNextItemWidth(160); ImGui::InputText("Server##srv", srvName, sizeof(srvName));
        ImGui::SameLine(); ImGui::SetNextItemWidth(120);
        ImGui::InputText("Password##srv", srvPass, sizeof(srvPass), ImGuiInputTextFlags_Password);
        ImGui::SetNextItemWidth(120); ImGui::SliderInt("Max##srv", &maxP, 1, 64);
        ImGui::SameLine(); ImGui::SetNextItemWidth(140); ImGui::SliderFloat("Tick##srv", &tick, 5.0f, 60.0f, "%.0f/s");
        if (ImGui::Button("Host Game", ImVec2(-1, 0))) { ed.StartHost((std::uint16_t)port); applyHostSettings(); }

        ImGui::SeparatorText("Join a game");
        ImGui::SetNextItemWidth(180);
        ImGui::InputText("Address##host", host, sizeof(host));
        ImGui::SameLine();
        if (ImGui::Button("Join")) { ed.StartJoin(host, (std::uint16_t)port); if (ed.net()) { ed.net()->SetLocalName(pname); ed.net()->password = srvPass; } }
        ImGui::SameLine();
        if (ImGui::Button("Disconnect")) ed.StopNetwork();
        if (auto* n = ed.net()) {
            const char* mode = n->IsServer() ? "Server" : n->IsClient() ? "Client" : "Offline";
            ImGui::Text("Mode: %s   Peers: %d   LocalId: %u",
                        mode, (int)n->PeerCount(), n->LocalId());
            if (n->IsClient()) ImGui::SameLine(), ImGui::Text("  Ping: %.0f ms", n->RttMs());
            // Chat: broadcast a line to every peer; show what arrives.
            static char chat[128] = "";
            static std::vector<std::string> log;
            n->SetMessageHandler([&](const NetworkManager::NetMessage& m) {
                if (m.channel == "chat")
                    log.push_back("#" + std::to_string(m.from) + ": " + m.data);
            });
            ImGui::SetNextItemWidth(220);
            ImGui::InputText("##chat", chat, sizeof(chat));
            ImGui::SameLine();
            if (ImGui::Button("Send") && chat[0]) { n->Send("chat", chat); chat[0] = '\0'; }
            if (n->IsServer())
                for (const auto& pi : n->Peers())
                    ImGui::BulletText("peer %u  '%s'", pi.id, pi.name.c_str());
            for (std::size_t i = log.size() > 6 ? log.size() - 6 : 0; i < log.size(); ++i)
                ImGui::TextWrapped("%s", log[i].c_str());
        } else ImGui::TextDisabled("not connected");
    }

    ImGui::End();
}

// Scriptable Object editor: edit a .okaydata file's key/value fields.
void DrawDataAssetEditor() {
    if (!g_dataAssetOpen) return;
    static std::string loaded;
    static DataAsset asset;
    if (loaded != g_dataAssetPath) { asset = DataAsset{}; asset.Load(g_dataAssetPath); loaded = g_dataAssetPath; }
    if (!ImGui::Begin("Data Asset", &g_dataAssetOpen)) { ImGui::End(); return; }
    ImGui::TextDisabled("%s", g_dataAssetPath.c_str());
    ImGui::TextDisabled("Read from script: data_num(\"file.okaydata\", \"key\") / data_str(...)");
    ImGui::Separator();
    auto& fields = asset.Fields();
    int remove = -1;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        ImGui::PushID((int)i);
        char k[64]; std::strncpy(k, fields[i].first.c_str(), sizeof(k) - 1); k[sizeof(k)-1] = '\0';
        ImGui::SetNextItemWidth(140);
        if (ImGui::InputText("##k", k, sizeof(k))) fields[i].first = k;
        ImGui::SameLine();
        char v[192]; std::strncpy(v, fields[i].second.c_str(), sizeof(v) - 1); v[sizeof(v)-1] = '\0';
        ImGui::SetNextItemWidth(200);
        if (ImGui::InputText("##v", v, sizeof(v))) fields[i].second = v;
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) remove = (int)i;
        ImGui::PopID();
    }
    if (remove >= 0) fields.erase(fields.begin() + remove);
    if (ImGui::Button("Add Field")) fields.emplace_back("key", "value");
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        if (asset.Save(g_dataAssetPath)) ConsoleLog("Saved " + g_dataAssetPath);
        else ConsoleLog("Save failed: " + g_dataAssetPath);
    }
    ImGui::End();
}

// Edit a .okaymat asset (Unity-style material inspector): tweak the surface
// look and Save, or stamp it onto the selected mesh with Apply to Selection.
void DrawMaterialEditor(EditorState& ed) {
    if (!g_matAssetOpen) return;
    static std::string loaded;
    static Material mat;
    if (loaded != g_matAssetPath) {
        mat = Material{}; Material::LoadFromFile(g_matAssetPath, mat); loaded = g_matAssetPath;
    }
    if (!ImGui::Begin("Material", &g_matAssetOpen)) { ImGui::End(); return; }
    ImGui::TextDisabled("%s", g_matAssetPath.c_str());
    ImGui::Separator();
    float col[4] = {mat.color.r, mat.color.g, mat.color.b, mat.color.a};
    if (ImGui::ColorEdit4("Albedo", col)) mat.color = {col[0], col[1], col[2], col[3]};
    float em[3] = {mat.emissive.r, mat.emissive.g, mat.emissive.b};
    if (ImGui::ColorEdit3("Emissive", em)) mat.emissive = {em[0], em[1], em[2], 1.0f};
    ImGui::SliderFloat("Specular", &mat.specular, 0.0f, 1.0f);
    if (mat.specular > 0.0f) ImGui::SliderFloat("Shininess", &mat.shininess, 1.0f, 128.0f);
    ImGui::Checkbox("Unlit", &mat.unlit);
    ImGui::SameLine();
    ImGui::Checkbox("Double-sided", &mat.doubleSided);
    char tex[256]; std::strncpy(tex, mat.texture.c_str(), sizeof(tex) - 1); tex[sizeof(tex) - 1] = '\0';
    if (ImGui::InputText("Texture", tex, sizeof(tex))) mat.texture = tex;
    float til[2] = {mat.tiling.x, mat.tiling.y};
    if (ImGui::DragFloat2("Tiling", til, 0.05f, 0.01f, 64.0f)) mat.tiling = {til[0], til[1]};
    ImGui::Separator();
    if (ImGui::Button("Save")) {
        if (mat.SaveToFile(g_matAssetPath)) ConsoleLog("Saved " + g_matAssetPath);
        else ConsoleLog("Save failed: " + g_matAssetPath);
    }
    ImGui::SameLine();
    bool canApply = ed.selected() && ed.selected()->GetComponent<MeshRenderer>();
    ImGui::BeginDisabled(!canApply);
    if (ImGui::Button("Apply to Selection")) {
        mat.ApplyTo(*ed.selected()->GetComponent<MeshRenderer>());
        ed.dirty = true; ConsoleLog("Applied material to " + ed.selected()->name);
    }
    ImGui::EndDisabled();
    if (!canApply) { ImGui::SameLine(); ImGui::TextDisabled("(select a mesh to apply)"); }
    ImGui::End();
}

void DrawStats(EditorState& ed) {
    if (!ImGui::Begin("Stats", &g_showStats)) { ImGui::End(); return; }
    ImGuiIO& io = ImGui::GetIO();
    static float hist[120] = {0};
    static int hi = 0;
    hist[hi] = io.Framerate; hi = (hi + 1) % 120;
    ImGui::Text("FPS: %.0f  (%.2f ms)", io.Framerate, io.Framerate > 0 ? 1000.0f / io.Framerate : 0.0f);
    ImGui::PlotLines("##fps", hist, 120, hi, nullptr, 0.0f, 240.0f, ImVec2(-1, 60));
    ImGui::Separator();
    ImGui::Text("Mode: %s", ed.isPlaying() ? "PLAYING" : "EDIT");
    ImGui::Text("GameObjects: %d", (int)ed.scene().Objects().size());
    ImGui::Text("Sprites: %d", (int)ed.scene().FindObjectsOfType<SpriteRenderer>().size());
    ImGui::Text("Meshes: %d", (int)ed.scene().FindObjectsOfType<MeshRenderer>().size());
    ImGui::Text("Colliders: %d", (int)ed.scene().FindObjectsOfType<Collider2D>().size());

    // Environment: the scene's skybox + ambient, saved with the scene so the
    // built game renders the same sky and base light.
    if (ImGui::CollapsingHeader("Environment (Sky & Light)")) {
        auto& rs = ed.scene().renderSettings;
        if (ImGui::Checkbox("Skybox", &rs.skybox)) ed.dirty = true;
        float t[3] = {rs.skyTop.r, rs.skyTop.g, rs.skyTop.b};
        if (ImGui::ColorEdit3("Sky Top", t)) { rs.skyTop = {t[0], t[1], t[2], 1}; ed.dirty = true; }
        float hz[3] = {rs.skyHorizon.r, rs.skyHorizon.g, rs.skyHorizon.b};
        if (ImGui::ColorEdit3("Horizon", hz)) { rs.skyHorizon = {hz[0], hz[1], hz[2], 1}; ed.dirty = true; }
        float bo[3] = {rs.skyBottom.r, rs.skyBottom.g, rs.skyBottom.b};
        if (ImGui::ColorEdit3("Sky Bottom", bo)) { rs.skyBottom = {bo[0], bo[1], bo[2], 1}; ed.dirty = true; }
        if (ImGui::SliderFloat("Ambient", &rs.ambient, 0.0f, 1.0f)) ed.dirty = true;
        ImGui::Spacing();
        if (ImGui::Checkbox("Distance Fog", &rs.fog)) ed.dirty = true;
        if (rs.fog) {
            float fc[3] = {rs.fogColor.r, rs.fogColor.g, rs.fogColor.b};
            if (ImGui::ColorEdit3("Fog Color", fc)) { rs.fogColor = {fc[0], fc[1], fc[2], 1}; ed.dirty = true; }
            if (ImGui::DragFloat("Fog Start", &rs.fogStart, 0.5f, 0.0f, 4000.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Fog End", &rs.fogEnd, 0.5f, 0.0f, 8000.0f)) ed.dirty = true;
        }
    }
    ImGui::End();
}

// The Scene Manager panel: the project's scenes (Unity's Build Settings list).
// Lists the .okayscene files in the project, opens them, and manages a build
// order with a startup scene the player loads first. The build list is fed into
// the engine SceneManager so scripts can load_scene_index / load_next_scene.
void DrawScenes(EditorState& ed) {
    if (!ImGui::Begin("Scenes", &g_showScenes)) { ImGui::End(); return; }
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path dir = ed.projectDir().empty() ? fs::absolute(".", ec)
                                           : fs::path(ed.projectDir()) / "Assets";
    ImGui::TextDisabled("%s", dir.string().c_str());

    // Rescan the folder for scene files (cheap; refresh button forces it).
    static std::vector<std::string> scenes;
    static std::string lastDir;
    auto rescan = [&]() {
        scenes.clear();
        if (fs::exists(dir, ec))
            for (auto& e : fs::recursive_directory_iterator(dir, ec)) {
                if (e.is_regular_file() && e.path().extension() == ".okayscene")
                    scenes.push_back(e.path().string());
            }
        std::sort(scenes.begin(), scenes.end());
        // Mirror into the engine SceneManager build list.
        SceneManager::ClearScenes();
        for (auto& s : scenes) SceneManager::AddScene(s);
        lastDir = dir.string();
    };
    if (lastDir != dir.string()) rescan();
    if (ImGui::Button("Refresh")) rescan();
    ImGui::SameLine();
    if (ImGui::Button("Save Current As...")) g_showSaveAs = true;
    ImGui::Separator();

    if (scenes.empty()) ImGui::TextDisabled("No .okayscene files in the project yet.");
    for (int i = 0; i < (int)scenes.size(); ++i) {
        const std::string& path = scenes[i];
        std::string name = fs::path(path).stem().string();
        bool isCurrent = (path == ed.path());
        ImGui::PushID(i);
        ImGui::Text("%d", i);                 // build index
        ImGui::SameLine();
        if (ImGui::Selectable(name.c_str(), isCurrent, ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                std::string err;
                if (ed.Load(path, &err)) ConsoleLog("Opened " + name);
                else ConsoleLog("Open failed: " + err);
            }
        }
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70);
        if (ImGui::SmallButton("Open")) {
            std::string err;
            if (ed.Load(path, &err)) ConsoleLog("Opened " + name);
            else ConsoleLog("Open failed: " + err);
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    ImGui::TextDisabled("double-click to open; the build order is the index shown");
    ImGui::End();
}

// A dedicated code/graph editor panel (separate from the Inspector), with
// external-IDE round-tripping.
// Prompt to save unsaved work before quitting. Routed through g_quitRequested
// (set by Exit, the window close button, or SDL_QUIT) so closing never silently
// discards changes.
void DrawQuitPrompt(EditorState& ed, bool& running) {
    static bool open = false;
    if (g_quitRequested && !open) {
        if (!ed.dirty) { running = false; g_quitRequested = false; return; }
        ImGui::OpenPopup("Unsaved Changes");
        open = true;
    }
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("You have unsaved changes. Save before quitting?");
        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(110, 0))) {
            std::string p = ed.path().empty() ? "scene.okayscene" : ed.path();
            if (ed.Save(p)) { ConsoleLog("Saved " + p); AddRecent(p); }
            running = false; g_quitRequested = false; open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(110, 0))) {
            running = false; g_quitRequested = false; open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110, 0))) {
            g_quitRequested = false; open = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// In-editor OkayScript reference, so scripting is documented without leaving
// the app. Opened from Help > Scripting Reference or the Script Editor's Docs.
void DrawScriptDocs() {
    if (!g_showScriptDocs) return;
    ImGui::SetNextWindowSize(ImVec2(560, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scripting Reference", &g_showScriptDocs)) { ImGui::End(); return; }

    auto header = [](const char* s) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "%s", s);
        ImGui::Separator();
    };
    auto api = [](const char* sig, const char* desc) {
        ImGui::BulletText("%s", sig);
        ImGui::SameLine(230); ImGui::TextDisabled("%s", desc);
    };
    auto code = [](const char* c) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.74f, 0.88f, 0.70f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.25f));
        ImVec2 sz(-1.0f, ImGui::GetTextLineHeightWithSpacing() *
                  (float)(std::count(c, c + std::strlen(c), '\n') + 1) + 8);
        ImGui::BeginChild(ImGui::GetID(c), sz, true);
        ImGui::TextUnformatted(c);
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
    };

    // ---- Tutorial: teach the language from scratch ----
    if (ImGui::CollapsingHeader("Learn the basics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("OkayScript is a small, friendly language for game logic. "
            "You write two functions: start() runs once, update(dt) runs every frame "
            "(dt = seconds since the last frame). Everything below is just those two "
            "plus values, math, and the built-in commands.");

        ImGui::SeparatorText("1. Variables");
        ImGui::TextWrapped("Make a value with 'var'. Values are numbers, text (in "
            "quotes), or true/false. End each statement with a semicolon ;");
        code("var speed = 5;\nvar name = \"Hero\";\nvar alive = true;");

        ImGui::SeparatorText("2. Math & comparisons");
        ImGui::TextWrapped("%s", "Use + - * / % for math, and == != < > <= >= to compare. "
            "Combine conditions with && (and), || (or), ! (not).");
        code("var hp = 100 - 25;        // 75\nvar low = hp < 50;       // false");

        ImGui::SeparatorText("3. Making decisions: if / else");
        code("if (key(\"space\")) {\n    move(0, 5 * dt);   // jump while held\n} else {\n    move(0, -1 * dt);  // gently fall\n}");

        ImGui::SeparatorText("4. Repeating: while / for");
        code("for (var i = 0; i < 3; i = i + 1) {\n    spawn(\"Coin\", i, 0);\n}");

        ImGui::SeparatorText("5. The two lifecycle functions");
        ImGui::TextWrapped("start() sets things up once; update(dt) drives behaviour "
            "every frame. Multiply movement by dt so it's the same speed on any PC.");
        code("function start() {\n    set_pos(0, 0);\n    set(\"score\", 0);\n}\n\n"
             "function update(dt) {\n    // move with the WASD / arrow keys\n"
             "    move(axis_x() * 5 * dt, axis_y() * 5 * dt);\n}");

        ImGui::SeparatorText("6. Your own functions");
        ImGui::TextWrapped("Group steps into a function and call it by name (or with "
            "call(\"name\") for state machines).");
        code("function hurt() {\n    set(\"score\", get(\"score\") - 1);\n}\n\n"
             "function update(dt) {\n    if (key_down(\"x\")) { hurt(); }\n}");

        ImGui::TextWrapped("That's the whole language. The rest is the built-in "
            "commands below (move, key, spawn, set/get, …). Hit Compile & Run in the "
            "Script Editor to try it live.");
        ImGui::Spacing();
    }

    ImGui::TextWrapped("OkaySpace scripts are written in OkayScript — a small "
        "C-style language. Attach a Script component, then edit it in the Script "
        "Editor. Two functions drive a script, the same way Unity uses a base "
        "class (here it's OkaySource):");
    ImGui::Indent();
    ImGui::TextDisabled("start()      runs once when the scene starts");
    ImGui::TextDisabled("update(dt)   runs every frame; dt is seconds elapsed");
    ImGui::Unindent();

    header("Syntax");
    ImGui::TextWrapped("%s",
        "Blocks use braces { }, statements end with ;  (NOT Lua-style 'end').\n\n"
        "function start() {\n"
        "    set_pos(0, 0);\n"
        "    set(\"score\", 0);\n"
        "}\n\n"
        "function update(dt) {\n"
        "    var speed = 3;\n"
        "    if (key(\"d\")) { move(speed * dt, 0); }\n"
        "    if (key(\"a\")) { move(-speed * dt, 0); }\n"
        "    move(axis_x() * speed * dt, axis_y() * speed * dt);\n"
        "}\n\n"
        "Has: var, if/else, while, for, return, break, continue, + - * / %, "
        "== != < > <= >=, && || !, and arrays a[i].");

    header("Movement & Transform (2D)");
    api("move(dx, dy)", "translate by (dx, dy)");
    api("set_pos(x, y)", "set local position");
    api("set_x(v) / set_y(v)", "set one position axis");
    api("pos_x() / pos_y()", "current local position");
    api("rotate(deg)", "rotate around Z by degrees");
    api("move_toward(x, y, step)", "step toward a point");
    api("look_at(\"name\")", "face another object (2D)");

    header("Transform & Physics (3D)");
    api("move3(dx, dy, dz)", "translate in 3D");
    api("set_pos3(x, y, z)", "set 3D position");
    api("set_z(v) / pos_z()", "Z position");
    api("rotate3(x, y, z)", "rotate by Euler degrees");
    api("set_rot3 / set_scale3", "set rotation / scale (3D)");
    api("scale_x/y/z()", "current scale");
    api("move_forward(d) / move_right(d)", "move along facing");
    api("forward_x/y/z()", "facing direction vector");
    api("look_at3(\"name\")", "face an object in 3D");

    header("Rigidbody (2D & 3D)");
    api("set_velocity(x, y)", "set 2D velocity");
    api("set_velocity3(x, y, z)", "set 3D velocity");
    api("velocity_x/y/z()", "read velocity");
    api("add_force / add_force3", "accumulate force");
    api("add_impulse / add_impulse3", "instant velocity change");
    api("jump(v)", "set upward velocity");
    api("set_gravity / set_gravity3", "change world gravity");

    header("Input");
    api("key / key_down / key_up", "keyboard, by \"x\"");
    api("axis_x() / axis_y()", "WASD / arrows, -1..1");
    api("mouse_x() / mouse_y()", "cursor position");
    api("mouse / mouse_down / mouse_up", "buttons 0=L 1=R 2=M");
    api("gamepad(btn) / gamepad_x()", "controller input");

    header("This object");
    api("name() / set_name(s)", "object name");
    api("tag() / set_tag(s) / has_tag(s)", "object tag");
    api("set_active(b) / self_active()", "enable/disable self");
    api("destroy() / destroy_obj(\"n\")", "remove an object");
    api("set_parent(\"n\") / detach()", "re-parent");
    api("set_text / set_color / set_texture", "sibling renderers");
    api("set_mesh(\"Sphere\")", "swap a 3D mesh");

    header("Other objects & scene");
    api("exists(\"n\") / is_active(\"n\")", "query by name");
    api("obj_x/y/z(\"n\")", "another object's position");
    api("dist_to(\"n\") / vel_toward(\"n\", s)", "chase / home");
    api("count_tag(\"t\") / nearest_tag(\"t\")", "tag queries");
    api("set_cam / move_cam / set_cam_zoom", "camera control");
    api("set_bg / set_light / set_ambient", "background & lighting");
    api("load_scene(\"file\")", "switch scenes");

    header("State, math & data");
    api("set(\"k\", v) / get(\"k\")", "shared variables");
    api("prefs_set / prefs_get / prefs_save", "save data across runs");
    api("rand(lo,hi) / randi / chance(p)", "randomness");
    api("dist(...) / dist3(...) / angle_to", "geometry");
    api("sin cos tan sqrt pow abs min max", "math");
    api("clamp clamp01 lerp smoothstep wrap", "interpolation");
    api("array push pop count sort_num", "lists");
    api("upper lower split join substr", "strings");
    api("time() / dt() / fps()", "timing");

    header("Timers, spawning & FX");
    api("after(s,\"fn\") / every(s,\"fn\")", "scheduled callbacks");
    api("spawn(\"p\", x, y) / spawn3(...)", "instantiate prefabs");
    api("emit(n) / particles_on(b)", "particle FX");
    api("play_anim() / play_sound()", "animation & audio");
    api("set_tile / get_tile / tile_resize", "tilemap editing");

    header("Physics queries & events");
    api("raycast_hit(ox,oy,dx,dy)", "2D ray test");
    api("raycast_hit3(o.., d..)", "3D ray test");
    api("overlap(x, y)", "point inside a 2D collider?");
    ImGui::TextWrapped("Define on_collision() / on_trigger() (2D) — they fire when "
        "this object's collider hits or overlaps another.");

    ImGui::Spacing();
    ImGui::TextDisabled("This is a summary — see docs/SCRIPTING.md for the full list.");
    if (ImGui::Button("Close")) g_showScriptDocs = false;
    ImGui::End();
}

// --- VS Code-style script editor helpers ---------------------------------

// Cursor line/column for the status bar, filled by an InputText callback.
struct ScriptCaret { int line = 1, col = 1; };
static int ScriptCaretCallback(ImGuiInputTextCallbackData* d) {
    auto* c = (ScriptCaret*)d->UserData;
    int ln = 1, col = 1;
    for (int k = 0; k < d->CursorPos && k < d->BufTextLen; ++k) {
        if (d->Buf[k] == '\n') { ++ln; col = 1; } else ++col;
    }
    c->line = ln; c->col = col;
    return 0;
}

// Draw the code text with VS Code "Dark+" syntax colors on top of the editor.
// ProggyClean is monospace, so glyphs advance by a fixed width and the colored
// overlay lines up exactly with the InputText beneath it.
static void DrawCodeHighlight(ImDrawList* dl, const char* text, ImVec2 origin,
                              float charW, float lineH) {
    const ImU32 cDefault = IM_COL32(212, 212, 212, 255);
    const ImU32 cKeyword = IM_COL32( 86, 156, 214, 255);  // blue
    const ImU32 cType    = IM_COL32( 78, 201, 176, 255);  // teal
    const ImU32 cString  = IM_COL32(206, 145, 120, 255);  // orange
    const ImU32 cComment = IM_COL32(106, 153,  85, 255);  // green
    const ImU32 cNumber  = IM_COL32(181, 206, 168, 255);  // light green
    static const char* kw[] = {
        "if","else","for","while","do","return","break","continue","switch","case",
        "var","let","const","function","func","def","class","struct","new","public",
        "private","protected","static","void","int","float","bool","string","true",
        "false","null","this","import","using","namespace","foreach","in","and","or","not"};
    static const char* types[] = {
        "Vector2","Vector3","Vec2","Vec3","Color","Quaternion","Transform","GameObject",
        "Mathf","Input","Time","Okay","OkaySource","Debug","Random","Physics"};
    auto isWord = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
    auto inList = [](const std::string& s, const char* const* arr, int n) {
        for (int i = 0; i < n; ++i) if (s == arr[i]) return true; return false; };

    float x = origin.x, y = origin.y;
    for (int i = 0; text[i];) {
        char c = text[i];
        if (c == '\n') { x = origin.x; y += lineH; ++i; continue; }
        if (c == '/' && text[i + 1] == '/') {                          // line comment
            int j = i; while (text[j] && text[j] != '\n') ++j;
            std::string s(text + i, text + j);
            dl->AddText(ImVec2(x, y), cComment, s.c_str()); x += (j - i) * charW; i = j; continue;
        }
        if (c == '"' || c == '\'') {                                   // string literal
            char q = c; int j = i + 1;
            while (text[j] && text[j] != q && text[j] != '\n') { if (text[j] == '\\' && text[j + 1]) ++j; ++j; }
            if (text[j] == q) ++j;
            std::string s(text + i, text + j);
            dl->AddText(ImVec2(x, y), cString, s.c_str()); x += (j - i) * charW; i = j; continue;
        }
        if (std::isdigit((unsigned char)c)) {                          // number
            int j = i; while (text[j] && (std::isalnum((unsigned char)text[j]) || text[j] == '.')) ++j;
            std::string s(text + i, text + j);
            dl->AddText(ImVec2(x, y), cNumber, s.c_str()); x += (j - i) * charW; i = j; continue;
        }
        if (isWord(c)) {                                               // identifier / keyword
            int j = i; while (text[j] && isWord(text[j])) ++j;
            std::string s(text + i, text + j);
            ImU32 col = inList(s, kw, (int)(sizeof(kw) / sizeof(*kw))) ? cKeyword
                      : inList(s, types, (int)(sizeof(types) / sizeof(*types))) ? cType : cDefault;
            dl->AddText(ImVec2(x, y), col, s.c_str()); x += (j - i) * charW; i = j; continue;
        }
        if (c != ' ' && c != '\t') { char b[2] = {c, 0}; dl->AddText(ImVec2(x, y), cDefault, b); }
        x += (c == '\t' ? 4 * charW : charW); ++i;
    }
}

void DrawScriptEditor(EditorState& ed) {
    if (!ImGui::Begin("Script Editor", &g_showScriptEditor)) { ImGui::End(); return; }
    GameObject* go = ed.selected();
    ScriptComponent* sc = go ? go->GetComponent<ScriptComponent>() : nullptr;

    if (!sc) {
        ImGui::TextDisabled("Select an object with a Script component.");
        ImGui::TextDisabled("Add one via Inspector > Add Component > Scripting.");
        ImGui::End();
        return;
    }

    {
        auto& buf = CodeBuffer(sc, sc->Source().empty()
            ? extide::StarterScript(sc->Language()) : sc->Source());
        auto filePath = [&]() {
            return sc->Path().empty() ? go->name + "." + extide::ExtFor(sc->Language()) : sc->Path();
        };

        // --- Toolbar (VS Code-style title row): file + Run / Save / Reload ----
        std::string fname = sc->Path().empty() ? (go->name + "." + extide::ExtFor(sc->Language()))
                                               : std::filesystem::path(sc->Path()).filename().string();
        ImGui::TextColored(ImVec4(0.82f, 0.82f, 0.88f, 1.0f), "%s", fname.c_str());
        ImGui::SameLine();
        static bool s_highlight = true;
        static std::string s_error;     // last compile error (shown red in status)
        static char s_find[128] = "";   // Find query (Ctrl+F highlights matches)
        if (ImGui::SmallButton("Run")) {           // compile + run
            bool ok = sc->LoadSource(buf.data(), &s_error);
            if (ok) s_error.clear();
            ConsoleLog(ok ? "Compiled OK" : "Error: " + s_error);
            ed.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Save")) {
            std::string p = filePath();
            if (extide::WriteFile(p, buf.data())) { sc->SetPath(p); ConsoleLog("Saved " + p); }
            else ConsoleLog("Save failed");
            ed.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reload")) {
            if (!sc->Path().empty()) {
                std::string src = extide::ReadFile(sc->Path());
                SetCodeBuffer(sc, src);
                std::string e; sc->LoadSource(src, &e);
                ConsoleLog("Reloaded " + sc->Path());
            } else ConsoleLog("No file to reload (Save first)");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Open in IDE")) {
            std::string p = filePath();
            extide::WriteFile(p, buf.data()); sc->SetPath(p); extide::OpenExternal(p);
            ConsoleLog("Opened " + p + " in external IDE");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Docs")) g_showScriptDocs = true;
        ImGui::SameLine();
        ImGui::Checkbox("Syntax", &s_highlight);
        // Language picker (only backends this build supports).
        static std::vector<std::string> avail = AvailableScriptLanguages();
        if (avail.size() > 1) {
            std::vector<const char*> items; items.reserve(avail.size());
            for (auto& s : avail) items.push_back(s.c_str());
            int li = 0;
            for (int i = 0; i < (int)avail.size(); ++i) if (avail[i] == sc->Language()) li = i;
            ImGui::SameLine(); ImGui::SetNextItemWidth(110);
            if (ImGui::Combo("##lang", &li, items.data(), (int)items.size())) {
                std::string oldLang = sc->Language(), newLang = avail[li];
                sc->SetLanguage(newLang);
                std::string cur = buf.data();
                if (cur.empty() || cur == extide::StarterScript(oldLang))
                    SetCodeBuffer(sc, extide::StarterScript(newLang));
            }
        }

        // Find bar: Ctrl+F focuses it; matches are highlighted in the editor.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(180);
        ImGui::InputTextWithHint("##find", "Find (Ctrl+F)", s_find, sizeof(s_find));
        // Count matches (case-insensitive) for the status bar.
        int findCount = 0;
        if (s_find[0]) {
            std::string hay = buf.data(), needle = s_find;
            for (auto& c : hay) c = (char)std::tolower((unsigned char)c);
            for (auto& c : needle) c = (char)std::tolower((unsigned char)c);
            for (std::size_t pos = hay.find(needle); pos != std::string::npos;
                 pos = hay.find(needle, pos + needle.size())) ++findCount;
            ImGui::SameLine(); ImGui::TextDisabled("%d match%s", findCount, findCount == 1 ? "" : "es");
        }

        // --- Editor surface: dark theme, line-number gutter, syntax overlay ---
        static ScriptCaret caret;
        int lines = 1, curLen = 0, maxLen = 0;
        for (const char* p = buf.data(); *p; ++p) {
            if (*p == '\n') { ++lines; if (curLen > maxLen) maxLen = curLen; curLen = 0; }
            else ++curLen;
        }
        if (curLen > maxLen) maxLen = curLen;

        float charW = ImGui::CalcTextSize("0").x;
        float lineH = ImGui::GetTextLineHeight();
        float padY  = ImGui::GetStyle().FramePadding.y;
        float padX  = ImGui::GetStyle().FramePadding.x;

        ImVec2 av = ImGui::GetContentRegionAvail(); av.y -= lineH + 8.0f; if (av.y < 80) av.y = 80;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 30, 255));   // VS Code editor bg
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(30, 30, 30, 255));
        ImGui::PushStyleColor(ImGuiCol_Text,    IM_COL32(212, 212, 212, 255));
        ImGui::BeginChild("editorscroll", av, true,
                          ImGuiWindowFlags_HorizontalScrollbar);

        // Line-number gutter (tight line spacing so rows match the editor).
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0, padY));
        for (int i = 1; i <= lines; ++i)
            ImGui::TextColored(ImVec4(0.42f, 0.44f, 0.5f, 1.0f), "%4d", i);
        ImGui::EndGroup();
        ImGui::PopStyleVar();
        ImGui::SameLine();

        // The editable text, sized to fit all content so the outer child does the
        // scrolling and the colored overlay (drawn after) stays aligned.
        float contentW = (maxLen + 2) * charW + padX * 2;
        float contentH = lines * lineH + padY * 2 + 2;
        ImGui::InputTextMultiline("##code", buf.data(), buf.size(),
            ImVec2(contentW, contentH),
            ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways,
            ScriptCaretCallback, &caret);
        ImVec2 mn = ImGui::GetItemRectMin();
        ImVec2 origin(mn.x + padX, mn.y + padY);
        ImDrawList* edl = ImGui::GetWindowDrawList();
        // Highlight every Find match (drawn under the text overlay).
        if (s_find[0]) {
            std::string needle = s_find;
            std::size_t nlen = needle.size();
            int ln = 0, col = 0;
            const char* t = buf.data();
            auto lower = [](char c) { return (char)std::tolower((unsigned char)c); };
            for (int i = 0; t[i]; ) {
                if (t[i] == '\n') { ++ln; col = 0; ++i; continue; }
                bool match = true;
                for (std::size_t k = 0; k < nlen; ++k)
                    if (!t[i + k] || lower(t[i + k]) != lower(needle[k])) { match = false; break; }
                if (match) {
                    ImVec2 a(origin.x + col * charW, origin.y + ln * lineH);
                    edl->AddRectFilled(a, ImVec2(a.x + nlen * charW, a.y + lineH),
                                       IM_COL32(120, 110, 40, 180));
                }
                ++i; ++col;
            }
        }
        if (s_highlight)
            DrawCodeHighlight(edl, buf.data(), origin, charW, lineH);
        ImGui::EndChild();
        ImGui::PopStyleColor(3);

        // --- Status bar (VS Code-style) --------------------------------------
        const char* langLbl = sc->Language() == "okayscript" ? "OkayScript"
                            : sc->Language() == "lua" ? "Lua" : sc->Language().c_str();
        ImGui::TextDisabled("%s   Ln %d, Col %d   %d lines   Spaces", langLbl,
                            caret.line, caret.col, lines);
        if (!s_error.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "  \xe2\x9c\x97 %s", s_error.c_str());
        }
    }
    ImGui::End();
}

// Global keyboard shortcuts (ignored while typing in a field).
void HandleShortcuts(EditorState& ed) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return;
    bool ctrl = io.KeyCtrl;
    // Ctrl+Z undoes; Ctrl+Shift+Z and Ctrl+Y redo. (Guard the undo with !Shift
    // so Ctrl+Shift+Z doesn't undo and redo in the same frame.)
    if (ctrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (ed.Undo()) ConsoleLog("Undo"); }
    if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
                 (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))) {
        if (ed.Redo()) ConsoleLog("Redo");
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) g_showNewProject = true;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) g_showOpen = true;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        std::string p = ed.path().empty() ? "scene.okayscene" : ed.path();
        if (ed.Save(p)) { ConsoleLog("Saved " + p); ed.Achievement("FIRST_SAVE"); }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && ed.selected()) {
        ed.DuplicateSelected(); ConsoleLog("Duplicated selection");
    }
    // Copy / paste a GameObject via a serialized clipboard.
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && ed.selected()) {
        g_clipboard = SceneSerializer::SerializeObject(*ed.selected());
        ConsoleLog("Copied " + ed.selected()->name);
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !g_clipboard.empty()) {
        ed.PushUndo();
        if (GameObject* go = SceneSerializer::InstantiateFromText(ed.scene(), g_clipboard)) {
            go->transform->localPosition += Vec3{0.5f, 0.5f, 0.0f}; // offset so it's visible
            ed.Select(go);
            ed.dirty = true;
            ConsoleLog("Pasted " + go->name);
        }
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_B, false)) g_showBuildGame = true;
    // Ctrl+Up / Ctrl+Down reorder the selected object among its siblings.
    if (ctrl && ed.selected() && ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
        ed.PushUndo(); ed.scene().MoveSibling(ed.selected(), -1); ed.dirty = true;
    }
    if (ctrl && ed.selected() && ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
        ed.PushUndo(); ed.scene().MoveSibling(ed.selected(), +1); ed.dirty = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && ed.selected()) {
        ed.DeleteSelected(); ConsoleLog("Deleted selection");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        if (ed.isPlaying()) { ed.Stop(); g_paused = false; ConsoleLog("Stop"); }
        else {
            if (g_clearConsoleOnPlay) ConsoleClear();
            ed.Play(); g_paused = false; ConsoleLog("Play"); ed.Achievement("HIT_PLAY");
            g_showGame = true; g_focusGameOnPlay = true; // jump to the Game tab
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F, false) && ed.selected()) {
        Vec3 pp = ed.selected()->transform->Position();
        ed.camTarget = pp; ed.cameraPos = {pp.x, pp.y};
    }
}

void DrawFileDialogs(EditorState& ed) {
    if (g_showOpen)   { ImGui::OpenPopup("Open Scene");    g_showOpen = false; }
    if (g_showSaveAs) { ImGui::OpenPopup("Save Scene As"); g_showSaveAs = false; }
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();

    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Path", g_pathBuf, sizeof(g_pathBuf));
        if (ImGui::Button("Open", ImVec2(120, 0))) {
            std::string err;
            if (ed.Load(g_pathBuf, &err)) { ConsoleLog("Opened " + std::string(g_pathBuf)); AddRecent(g_pathBuf); }
            else ConsoleLog("Open failed: " + err);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Path", g_pathBuf, sizeof(g_pathBuf));
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (ed.Save(g_pathBuf)) { ConsoleLog("Saved " + std::string(g_pathBuf)); AddRecent(g_pathBuf); ed.Achievement("FIRST_SAVE"); }
            else ConsoleLog("Save failed");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (g_showInstPrefab) { ImGui::OpenPopup("Instantiate Prefab"); g_showInstPrefab = false; }
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Instantiate Prefab", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Prefab", g_prefabBuf, sizeof(g_prefabBuf));
        if (ImGui::Button("Instantiate", ImVec2(120, 0))) {
            ed.PushUndo();
            std::string err;
            GameObject* r = SceneSerializer::InstantiateFromFile(ed.scene(), g_prefabBuf, &err);
            if (r) { ed.Select(r); ed.dirty = true; ConsoleLog("Instantiated " + std::string(g_prefabBuf)); }
            else ConsoleLog("Prefab load failed: " + err);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (g_showBuildGame) { ImGui::OpenPopup("Build Game"); g_showBuildGame = false; }
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Build Game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Build Settings — export a standalone game.");
        ImGui::TextDisabled("Creates <Name>.exe + game.okayscene + game.okayconfig.");
        ImGui::Separator();

        ImGui::SeparatorText("Player");
        ImGui::InputText("Product name", g_buildNameBuf, sizeof(g_buildNameBuf));
        ImGui::InputText("Company", g_build.company, sizeof(g_build.company));
        ImGui::SetNextItemWidth(140);
        ImGui::InputText("Version", g_build.version, sizeof(g_build.version));
        ImGui::InputText("Output folder", g_buildDirBuf, sizeof(g_buildDirBuf));

        ImGui::SeparatorText("Window / Display");
        ImGui::SetNextItemWidth(90); ImGui::InputInt("Width", &g_build.width); ImGui::SameLine();
        ImGui::SetNextItemWidth(90); ImGui::InputInt("Height", &g_build.height);
        const char* presets[] = {"1280 x 720", "1920 x 1080", "2560 x 1440", "960 x 600", "800 x 600"};
        static int presetSel = 0;
        ImGui::SetNextItemWidth(150);
        if (ImGui::Combo("Resolution", &presetSel, presets, IM_ARRAYSIZE(presets))) {
            const int dims[][2] = {{1280,720},{1920,1080},{2560,1440},{960,600},{800,600}};
            g_build.width = dims[presetSel][0]; g_build.height = dims[presetSel][1];
        }
        ImGui::Checkbox("Fullscreen", &g_build.fullscreen); ImGui::SameLine();
        ImGui::Checkbox("Borderless", &g_build.borderless); ImGui::SameLine();
        ImGui::Checkbox("Resizable", &g_build.resizable);
        ImGui::Checkbox("VSync", &g_build.vsync);           ImGui::SameLine();
        ImGui::Checkbox("Hide cursor", &g_build.hideCursor);
        const char* fpsOpts[] = {"Uncapped", "30", "60", "120", "144"};
        const int   fpsVals[] = {0, 30, 60, 120, 144};
        int fpsSel = 0; for (int i = 0; i < 5; ++i) if (fpsVals[i] == g_build.fpsCap) fpsSel = i;
        ImGui::SetNextItemWidth(150);
        if (ImGui::Combo("Frame rate cap", &fpsSel, fpsOpts, 5)) g_build.fpsCap = fpsVals[fpsSel];

        ImGui::SeparatorText("Gameplay");
        ImGui::Checkbox("Quit on Escape", &g_build.quitOnEscape); ImGui::SameLine();
        ImGui::Checkbox("Show FPS overlay", &g_build.showFps);
        ImGui::SetNextItemWidth(200);
        ImGui::SliderFloat("Master volume", &g_build.masterVolume, 0.0f, 1.0f);

        ImGui::SeparatorText("Scenes & Options");
        ImGui::Checkbox("Include all project scenes", &g_build.includeAllProjectScenes);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bundle every .okayscene so load_scene_index works");
        ImGui::Checkbox("Development build", &g_build.developmentBuild);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Extra logging; marks the build as non-final");
        if (g_build.width  < 320) g_build.width  = 320;
        if (g_build.height < 240) g_build.height = 240;
        if (g_build.fpsCap < 0)   g_build.fpsCap = 0;

        ImGui::Separator();
        if (ImGui::Button("Build", ImVec2(120, 0))) {
            builder::Options o;
            o.company = g_build.company; o.version = g_build.version;
            o.width = g_build.width; o.height = g_build.height;
            o.fullscreen = g_build.fullscreen; o.borderless = g_build.borderless;
            o.resizable = g_build.resizable; o.vsync = g_build.vsync;
            o.hideCursor = g_build.hideCursor; o.fpsCap = g_build.fpsCap;
            o.quitOnEscape = g_build.quitOnEscape; o.masterVolume = g_build.masterVolume; o.showFps = g_build.showFps;
            o.includeAllProjectScenes = g_build.includeAllProjectScenes;
            o.developmentBuild = g_build.developmentBuild;
            g_buildStatus = builder::Build(ed, g_buildDirBuf, g_buildNameBuf, o);
            g_openBuildResult = true;
            ConsoleLog("Build Game: " + g_buildStatus);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (g_openBuildResult) { ImGui::OpenPopup("Build Result"); g_openBuildResult = false; }
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Build Result", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(420.0f);
        ImGui::TextUnformatted(g_buildStatus.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void DrawNewProjectPopup(EditorState& ed) {
    if (g_showNewProject) { ImGui::OpenPopup("New Project"); g_showNewProject = false; }
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char nameBuf[128] = "MyGame";
        static char locBuf[400]  = ".";
        ImGui::Text("Create a new project");
        ImGui::TextDisabled("A folder <Location>/<Name> is created with an Assets/ subfolder;");
        ImGui::TextDisabled("the starting scene is saved into Assets/.");
        ImGui::SetNextItemWidth(330); ImGui::InputText("Name", nameBuf, sizeof(nameBuf));
        ImGui::SetNextItemWidth(330); ImGui::InputText("Location", locBuf, sizeof(locBuf));
        ImGui::Separator();

        // After a NewScene*/template call, lay down the project folder and save.
        auto finishProject = [&]() {
            namespace fs = std::filesystem;
            std::string name = nameBuf[0] ? nameBuf : "MyGame";
            fs::path root = fs::path(locBuf[0] ? locBuf : ".") / name;
            std::error_code ec;
            fs::create_directories(root / "Assets", ec);
            if (ec) { ConsoleLog("Could not create project folder: " + ec.message()); return; }
            ed.setProjectDir(root.string());
            std::string sp = (root / "Assets" / (name + ".okayscene")).string();
            if (ed.Save(sp)) ConsoleLog("Created project at " + root.string());
            else ConsoleLog("Project folder made, but saving the scene failed.");
            ImGui::CloseCurrentPopup();
        };

        ImGui::TextDisabled("Pick a starting scene type.");
        if (ImGui::Button("2D Scene", ImVec2(160, 60))) { ed.NewScene2D(); finishProject(); }
        ImGui::SameLine();
        if (ImGui::Button("3D Scene", ImVec2(160, 60))) { ed.NewScene3D(); finishProject(); }
        ImGui::Spacing();
        ImGui::TextDisabled("Or start from a playable template:");
        if (ImGui::Button("Platformer", ImVec2(160, 44))) { ed.NewPlatformer(); finishProject(); }
        ImGui::SameLine();
        if (ImGui::Button("Top-Down", ImVec2(160, 44)))   { ed.NewTopDown(); finishProject(); }
        if (ImGui::Button("3D Platformer (physics)", ImVec2(-1, 44))) { ed.NewPlatformer3D(); finishProject(); }
        if (ImGui::Button("Multiplayer (host/join)", ImVec2(-1, 44))) { ed.NewMultiplayer(); finishProject(); }
        if (ImGui::Button("Coin Collector (full game)", ImVec2(-1, 36))) { ed.NewCoinCollector(); finishProject(); }
        if (ImGui::Button("Main Menu (UI)", ImVec2(-1, 36)))            { ed.NewMainMenu(); finishProject(); }
        if (ImGui::Button("Inventory (drag & drop)", ImVec2(-1, 36)))   { ed.NewInventory(); finishProject(); }
        if (ImGui::Button("Snake (full game)", ImVec2(-1, 36)))         { ed.NewSnake(); finishProject(); }
        ImGui::Spacing();
        if (ImGui::Button("Empty Scene", ImVec2(-1, 0))) { ed.NewScene(); finishProject(); }
        ImGui::EndPopup();
    }
}

void DrawAboutPopup() {
    if (g_openAbout) { ImGui::OpenPopup("About OkaySpace"); g_openAbout = false; }
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("About OkaySpace", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.34f, 0.66f, 1.0f, 1.0f));
        ImGui::TextUnformatted("OkaySpace Game Engine");
        ImGui::PopStyleColor();
        ImGui::Text("Version %s", OKAY_ENGINE_VERSION);
        ImGui::Separator();
        ImGui::TextWrapped("A Unity-inspired C++ game engine. Build 2D/3D scenes, "
                           "script them, and export a standalone game with File > Build Game.");
        ImGui::Spacing();
        ImGui::TextDisabled("github.com/kingimann/OkaySpaceGameEngine");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// Edit > Project Settings: project-wide defaults that builds inherit, plus
// quick "apply to current scene" for physics/rendering.
void DrawProjectSettings(EditorState& ed) {
    if (!g_showProjectSettings) return;
    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Project Settings", &g_showProjectSettings)) {
        ImGui::TextDisabled("Defaults a new build/scene inherits. Saved to project.okayproj.");
        if (ImGui::CollapsingHeader("Player", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::InputText("Company##ps", g_project.company, sizeof(g_project.company));
            ImGui::SetNextItemWidth(160);
            ImGui::InputText("Version##ps", g_project.version, sizeof(g_project.version));
            ImGui::SetNextItemWidth(110); ImGui::InputInt("Width##ps", &g_project.defaultWidth); ImGui::SameLine();
            ImGui::SetNextItemWidth(110); ImGui::InputInt("Height##ps", &g_project.defaultHeight);
        }
        if (ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat2("Gravity 2D##ps", g_project.gravity2D, 0.05f);
            ImGui::DragFloat3("Gravity 3D##ps", g_project.gravity3D, 0.05f);
            if (ImGui::Button("Apply to current scene##phys")) {
                ed.scene().physics().gravity = {g_project.gravity2D[0], g_project.gravity2D[1]};
                ed.scene().physics3D().gravity = {g_project.gravity3D[0], g_project.gravity3D[1], g_project.gravity3D[2]};
                ed.dirty = true; ConsoleLog("Applied project physics to the scene");
            }
        }
        if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Ambient##ps", &g_project.ambient, 0.0f, 1.0f);
            ImGui::Checkbox("Skybox##ps", &g_project.skybox);
            if (ImGui::Button("Apply to current scene##rend")) {
                ed.scene().renderSettings.ambient = g_project.ambient;
                ed.scene().renderSettings.skybox = g_project.skybox;
                ed.dirty = true; ConsoleLog("Applied project rendering to the scene");
            }
        }
        ImGui::Separator();
        if (ImGui::Button("Save", ImVec2(110, 0))) { SaveProjectSettings(); ConsoleLog("Project settings saved"); }
        ImGui::SameLine();
        if (ImGui::Button("Use in Build", ImVec2(120, 0))) {
            std::strncpy(g_build.company, g_project.company, sizeof(g_build.company) - 1);
            std::strncpy(g_build.version, g_project.version, sizeof(g_build.version) - 1);
            g_build.width = g_project.defaultWidth; g_build.height = g_project.defaultHeight;
            ConsoleLog("Build settings set from project defaults");
        }
    }
    ImGui::End();
}

void DrawUpdatePopup() {
    if (g_openUpdatePopup) { ImGui::OpenPopup("Engine Update"); g_openUpdatePopup = false; }
    ImGui::SetNextWindowSizeConstraints(ImVec2(420, 0), ImVec2(560, 520));
    if (ImGui::BeginPopupModal("Engine Update", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const updater::UpdateInfo& u = g_update;
        if (!u.error.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.5f, 1), "Update check failed");
            ImGui::TextWrapped("%s", u.error.c_str());
        } else if (u.available) {
            ImGui::TextColored(ImVec4(0.55f, 0.9f, 0.6f, 1), "Update available!");
            ImGui::Text("Installed: v%s", u.current.c_str());
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.4f, 1), "  ->  v%s", u.latest.c_str());
            if (!u.notes.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("Release notes");
                ImGui::BeginChild("notes", ImVec2(0, 200), true);
                ImGui::TextWrapped("%s", u.notes.c_str());
                ImGui::EndChild();
            }
            ImGui::Separator();
            if (!g_updateStatus.empty())
                ImGui::TextWrapped("%s", g_updateStatus.c_str());
            ImGui::BeginDisabled(g_installingUpdate);
            if (ImGui::Button(g_installingUpdate ? "Installing..." : "Download & Install",
                              ImVec2(180, 0))) {
                g_installingUpdate = true;
                g_updateStatus = "Downloading v" + u.latest + "...";
                // Synchronous: the editor briefly blocks while the new build is
                // fetched and swapped in, then relaunches.
                g_updateStatus = updater::InstallUpdate(u.latest);
                ConsoleLog(g_updateStatus);
                g_installingUpdate = false;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Later", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        } else {
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1), "You're up to date.");
            ImGui::Text("Installed version: v%s", u.current.c_str());
            ImGui::Separator();
            if (ImGui::Button("OK", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// A short type tag for a GameObject, based on its most distinctive component.
static const char* ObjectKind(GameObject* go) {
    if (go->GetComponent<Camera>())         return "[Cam] ";
    if (go->GetComponent<MeshRenderer>())   return "[3D] ";
    if (go->GetComponent<TextRenderer>())   return "[Txt] ";
    if (go->GetComponent<ParticleSystem>()) return "[FX] ";
    if (go->GetComponent<Tilemap>())        return "[Tile] ";
    if (go->GetComponent<SpriteRenderer>()) return "[Spr] ";
    if (go->GetComponent<AudioSource>())    return "[Snd] ";
    return "";
}

static char g_hierFilter[96] = "";
static GameObject* g_hierRename = nullptr;   // object being renamed inline
static bool        g_hierRenameOpen = false; // request to open the rename popup (once)
static char g_hierRenameBuf[128] = "";
static GameObject* g_prefabSaveTarget = nullptr; // object being saved as a prefab
static char g_prefabNameBuf[128] = "";

void DrawHierarchy(EditorState& ed) {
    ImGui::Begin("Hierarchy", &g_showHierarchy);
    ImGui::TextDisabled("Scene: %s%s", ed.scene().Name().c_str(), ed.dirty ? " *" : "");
    ImGui::SameLine(ImGui::GetWindowWidth() - 70);
    ImGui::TextDisabled("%d obj", (int)ed.scene().Objects().size());
    if (ImGui::Button("+ Create")) ImGui::OpenPopup("HierCreate");
    if (ImGui::BeginPopup("HierCreate")) {
        if (ImGui::MenuItem("Empty"))  ed.Select(ed.CreateEmpty());
        if (ImGui::MenuItem("Sprite")) ed.Select(ed.CreateSprite());
        if (ImGui::MenuItem("Camera")) ed.Select(ed.CreateCamera());
        if (ImGui::MenuItem("Cube"))   ed.Select(ed.CreateCube());
        ImGui::Separator();
        if (ImGui::MenuItem("UI Button")) { GameObject* root = EnsureUIRoot(ed); GameObject* g = ed.CreateEmpty("Button"); g->AddComponent<UIButton>(); if (root) g->transform->SetParent(root->transform, false); ed.Select(g); }
        if (ImGui::MenuItem("UI Text"))   { GameObject* root = EnsureUIRoot(ed); GameObject* g = ed.CreateEmpty("Text"); auto* t = g->AddComponent<TextRenderer>(); t->screenSpace = true; t->pixelSize = 3.0f; t->align = 1; t->anchor = UIAnchor::Center; if (root) g->transform->SetParent(root->transform, false); ed.Select(g); }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##hfilter", "search objects...", g_hierFilter, sizeof(g_hierFilter));
    ImGui::Separator();

    // When searching, show a flat list of every matching object.
    if (g_hierFilter[0] != '\0') {
        std::string needle = g_hierFilter;
        for (auto& n : needle) n = (char)std::tolower((unsigned char)n);
        for (const auto& up : ed.scene().Objects()) {
            GameObject* go = up.get();
            std::string low = go->name;
            for (auto& ch : low) ch = (char)std::tolower((unsigned char)ch);
            if (low.find(needle) == std::string::npos) continue;
            bool sel = (go == ed.selected());
            if (ImGui::Selectable((std::string(ObjectKind(go)) + go->name).c_str(), sel))
                ed.Select(go);
        }
        ImGui::End();
        return;
    }

    const auto& objs = ed.scene().Objects();
    for (const auto& up : objs) {
        GameObject* go = up.get();
        // Show only roots; children are listed under their parent.
        if (go->transform->Parent() != nullptr) continue;
        std::function<void(GameObject*)> drawNode = [&](GameObject* node) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                       ImGuiTreeNodeFlags_DefaultOpen |
                                       ImGuiTreeNodeFlags_SpanAvailWidth;
            if (node == ed.selected()) flags |= ImGuiTreeNodeFlags_Selected;
            if (node->transform->ChildCount() == 0) flags |= ImGuiTreeNodeFlags_Leaf;
            // Unity dims inactive objects; grey the whole row (and its subtree label).
            bool dim = !node->active;
            if (dim) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
            bool open = ImGui::TreeNodeEx(node, flags, "%s%s%s", ObjectKind(node),
                                          node->name.c_str(), node->active ? "" : "  (off)");
            if (dim) ImGui::PopStyleColor();
            ImVec2 rowMin = ImGui::GetItemRectMin(), rowMax = ImGui::GetItemRectMax();
            if (ImGui::IsItemClicked()) ed.Select(node);
            // Drag a row to rearrange: drop near a row's top/bottom edge to REORDER
            // it as a sibling (above/below), or onto the middle to RE-PARENT it.
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("GO_PTR", &node, sizeof(GameObject*));
                ImGui::TextUnformatted(node->name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                // Preview line for an edge (reorder) drop.
                float ty = (ImGui::GetIO().MousePos.y - rowMin.y) / (rowMax.y - rowMin.y);
                if (ty < 0.30f || ty > 0.70f) {
                    float ly = ty < 0.30f ? rowMin.y : rowMax.y;
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(rowMin.x, ly), ImVec2(rowMax.x, ly),
                                                        IM_COL32(120, 190, 255, 255), 2.0f);
                }
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GO_PTR")) {
                    GameObject* dragged = *(GameObject**)p->Data;
                    bool cycle = false;
                    for (Transform* t = node->transform; t; t = t->Parent())
                        if (dragged && t == dragged->transform) { cycle = true; break; }
                    if (dragged && dragged != node && !cycle) {
                        ed.PushUndo();
                        if (ty < 0.30f)      ed.scene().ReorderSibling(dragged, node, /*after=*/false);
                        else if (ty > 0.70f) ed.scene().ReorderSibling(dragged, node, /*after=*/true);
                        else                 dragged->transform->SetParent(node->transform, true);
                        ed.dirty = true;
                    }
                }
                // Drop a Project asset onto an object: a .okaymat applies its look
                // to the object's Mesh Renderer; an image sets the texture on a
                // Mesh/Sprite Renderer (Unity-style).
                if (const ImGuiPayload* ap = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    std::string path((const char*)ap->Data);
                    std::string ext;
                    if (auto d = path.find_last_of('.'); d != std::string::npos) ext = path.substr(d);
                    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
                    if (ext == ".okaymat") {
                        if (auto* mr = node->GetComponent<MeshRenderer>()) {
                            Material m; if (Material::LoadFromFile(path, m)) {
                                ed.PushUndo(); m.ApplyTo(*mr); ed.dirty = true;
                                ConsoleLog("Applied material to " + node->name);
                            }
                        } else ConsoleLog(node->name + " has no Mesh Renderer for the material");
                    } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
                        ed.PushUndo();
                        if (auto* mr = node->GetComponent<MeshRenderer>())      mr->texture = path;
                        else if (auto* sr = node->GetComponent<SpriteRenderer>()) sr->texture = path;
                        ed.dirty = true; ConsoleLog("Set texture on " + node->name);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            // Double-click a row to rename it inline.
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                g_hierRename = node; g_hierRenameOpen = true;
                std::strncpy(g_hierRenameBuf, node->name.c_str(), sizeof(g_hierRenameBuf) - 1);
                g_hierRenameBuf[sizeof(g_hierRenameBuf) - 1] = '\0';
            }
            // Right-click context menu per item.
            if (ImGui::BeginPopupContextItem()) {
                ed.Select(node);
                if (ImGui::MenuItem("Rename", "F2")) {
                    g_hierRename = node; g_hierRenameOpen = true;
                    std::strncpy(g_hierRenameBuf, node->name.c_str(), sizeof(g_hierRenameBuf) - 1);
                    g_hierRenameBuf[sizeof(g_hierRenameBuf) - 1] = '\0';
                }
                if (ImGui::MenuItem(node->active ? "Deactivate" : "Activate"))
                    { node->active = !node->active; ed.dirty = true; }
                ImGui::Separator();
                if (ImGui::MenuItem("Copy", "Ctrl+C"))
                    g_clipboard = SceneSerializer::SerializeObject(*node);
                if (ImGui::MenuItem("Paste As Sibling", "Ctrl+V", false, !g_clipboard.empty())) {
                    ed.PushUndo();
                    if (GameObject* p = SceneSerializer::InstantiateFromText(ed.scene(), g_clipboard)) {
                        if (node->transform->Parent()) p->transform->SetParent(node->transform->Parent(), false);
                        ed.Select(p); ed.dirty = true;
                    }
                }
                if (ImGui::MenuItem("Paste As Child", nullptr, false, !g_clipboard.empty())) {
                    ed.PushUndo();
                    if (GameObject* p = SceneSerializer::InstantiateFromText(ed.scene(), g_clipboard)) {
                        p->transform->SetParent(node->transform, false);
                        ed.Select(p); ed.dirty = true;
                    }
                }
                if (ImGui::MenuItem("Duplicate", "Ctrl+D")) { ed.DuplicateSelected(); ConsoleLog("Duplicated"); }
                if (ImGui::MenuItem("Delete", "Del"))    { ed.DeleteSelected(); ConsoleLog("Deleted"); }
                ImGui::Separator();
                if (ImGui::MenuItem("Move Up", "Ctrl+Up"))     { ed.PushUndo(); ed.scene().MoveSibling(node, -1); ed.dirty = true; }
                if (ImGui::MenuItem("Move Down", "Ctrl+Down")) { ed.PushUndo(); ed.scene().MoveSibling(node, +1); ed.dirty = true; }
                if (ImGui::MenuItem("Move to Top"))    { ed.PushUndo(); ed.scene().MoveSiblingToEdge(node, true);  ed.dirty = true; }
                if (ImGui::MenuItem("Move to Bottom")) { ed.PushUndo(); ed.scene().MoveSiblingToEdge(node, false); ed.dirty = true; }
                ImGui::Separator();
                if (ImGui::MenuItem("Create Empty Child")) {
                    GameObject* child = ed.scene().CreateGameObject("Child");
                    child->transform->SetParent(node->transform, false);
                    ed.Select(child); ed.dirty = true;
                }
                if (ImGui::BeginMenu("Create Child")) {
                    auto childOf = [&](GameObject* g) { if (g) { g->transform->SetParent(node->transform, false); ed.Select(g); ed.dirty = true; } };
                    if (ImGui::MenuItem("Cube"))   childOf(ed.CreateCube("Cube"));
                    if (ImGui::MenuItem("Sprite")) childOf(ed.CreateSprite("Sprite"));
                    if (ImGui::MenuItem("Camera")) childOf(ed.CreateCamera("Camera"));
                    if (ImGui::MenuItem("Light"))  { GameObject* g = ed.CreateEmpty("Light"); g->AddComponent<Light>(); childOf(g); }
                    ImGui::EndMenu();
                }
                // Wrap this object in a new empty parent (Unity's "Create Parent"),
                // keeping its on-screen transform unchanged.
                if (ImGui::MenuItem("Create Parent")) {
                    ed.PushUndo();
                    GameObject* parent = ed.scene().CreateGameObject(node->name + " Group");
                    parent->transform->SetPosition(node->transform->Position());
                    if (Transform* op = node->transform->Parent())
                        parent->transform->SetParent(op, true);
                    node->transform->SetParent(parent->transform, true);  // keep world pose
                    ed.Select(parent); ed.dirty = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Focus (frame)")) {
                    ed.camTarget = node->transform->Position();
                    ed.cameraPos = {node->transform->Position().x, node->transform->Position().y};
                }
                if (ImGui::MenuItem("Save As Prefab...")) {
                    g_prefabSaveTarget = node;
                    std::strncpy(g_prefabNameBuf, node->name.c_str(), sizeof(g_prefabNameBuf) - 1);
                    g_prefabNameBuf[sizeof(g_prefabNameBuf) - 1] = '\0';
                }
                ImGui::EndPopup();
            }
            if (open) {
                for (Transform* child : node->transform->Children())
                    drawNode(child->gameObject);
                ImGui::TreePop();
            }
        };
        drawNode(go);
    }
    // Right-click empty space to create objects.
    if (ImGui::BeginPopupContextWindow("HierarchyCtx",
            ImGuiPopupFlags_NoOpenOverItems | ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItem("Create Empty"))  ed.CreateEmpty();
        if (ImGui::MenuItem("Create Sprite")) ed.CreateSprite();
        if (ImGui::MenuItem("Create Cube"))   ed.CreateCube();
        if (ImGui::MenuItem("Create Camera")) ed.CreateCamera();
        if (ImGui::MenuItem("Create Light"))  { GameObject* g = ed.CreateEmpty("Light"); g->AddComponent<Light>(); ed.Select(g); }
        ImGui::Separator();
        if (ImGui::BeginMenu("3D Object")) {
            if (ImGui::MenuItem("Cube"))     ed.CreateCube();
            if (ImGui::MenuItem("Sphere"))   ed.CreateMesh("Sphere");
            if (ImGui::MenuItem("Cylinder")) ed.CreateMesh("Cylinder");
            if (ImGui::MenuItem("Plane"))    ed.CreateMesh("Plane");
            if (ImGui::MenuItem("Pyramid"))  ed.CreatePyramid();
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Paste", "Ctrl+V", false, !g_clipboard.empty())) {
            ed.PushUndo();
            if (GameObject* p = SceneSerializer::InstantiateFromText(ed.scene(), g_clipboard)) { ed.Select(p); ed.dirty = true; }
        }
        ImGui::EndPopup();
    }

    // Empty area: drop a prefab/scene asset to instantiate/open it, or drop a
    // hierarchy row here to unparent it (move to the scene root).
    ImGui::Dummy(ImGui::GetContentRegionAvail());
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* gp = ImGui::AcceptDragDropPayload("GO_PTR")) {
            GameObject* dragged = *(GameObject**)gp->Data;
            if (dragged && dragged->transform->Parent()) {
                ed.PushUndo(); dragged->transform->SetParent(nullptr, true); ed.dirty = true;
            }
        }
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            std::string path((const char*)p->Data);
            std::size_t dot = path.find_last_of('.');
            std::string ext = dot == std::string::npos ? "" : path.substr(dot);
            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext == ".okayprefab") {
                ed.PushUndo();
                std::string err;
                if (GameObject* r = SceneSerializer::InstantiateFromFile(ed.scene(), path, &err)) {
                    ed.Select(r); ed.dirty = true; ConsoleLog("Instantiated " + path);
                } else ConsoleLog("Prefab load failed: " + err);
            } else if (ext == ".okayscene") {
                std::string err;
                if (ed.Load(path, &err)) ConsoleLog("Opened " + path);
                else ConsoleLog("Open failed: " + err);
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Inline rename modal (double-click a row or context-menu Rename).
    if (g_hierRename) {
        // Confirm the target still exists in the scene.
        bool alive = false;
        for (const auto& up : ed.scene().Objects()) if (up.get() == g_hierRename) { alive = true; break; }
        if (!alive) g_hierRename = nullptr;
    }
    if (g_hierRename) {
        // OpenPopup must be called once (on request) — calling it every frame
        // resets the popup's active-id each frame, so OK/Cancel never register.
        if (g_hierRenameOpen) { ImGui::OpenPopup("Rename Object"); g_hierRenameOpen = false; }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Rename Object", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();   // focus once, not every frame
            bool enter = ImGui::InputText("Name", g_hierRenameBuf, sizeof(g_hierRenameBuf),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
            if (enter || ImGui::Button("OK", ImVec2(110, 0))) {
                if (g_hierRenameBuf[0]) { g_hierRename->name = g_hierRenameBuf; ed.dirty = true; }
                g_hierRename = nullptr; ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(110, 0))) { g_hierRename = nullptr; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }

    // Save-as-Prefab modal: writes a .okayprefab into the project's Assets so it
    // can be dragged back in / instantiated anywhere.
    if (g_prefabSaveTarget) {
        bool alive = false;
        for (const auto& up : ed.scene().Objects()) if (up.get() == g_prefabSaveTarget) { alive = true; break; }
        if (!alive) g_prefabSaveTarget = nullptr;
    }
    if (g_prefabSaveTarget) {
        if (!ImGui::IsPopupOpen("Save As Prefab")) ImGui::OpenPopup("Save As Prefab");   // open once
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Save As Prefab", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path dir = ed.projectDir().empty() ? fs::absolute(".", ec)
                                                   : fs::path(ed.projectDir()) / "Assets";
            ImGui::Text("Folder: %s", dir.string().c_str());
            ImGui::SetKeyboardFocusHere();
            bool enter = ImGui::InputText("Name", g_prefabNameBuf, sizeof(g_prefabNameBuf),
                                          ImGuiInputTextFlags_EnterReturnsTrue);
            if (enter || ImGui::Button("Save", ImVec2(110, 0))) {
                if (g_prefabNameBuf[0]) {
                    fs::create_directories(dir, ec);
                    fs::path out = dir / (std::string(g_prefabNameBuf) + ".okayprefab");
                    if (SceneSerializer::SaveObjectToFile(*g_prefabSaveTarget, out.string()))
                        ConsoleLog("Saved prefab " + out.string());
                    else ConsoleLog("Prefab save failed");
                }
                g_prefabSaveTarget = nullptr; ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(110, 0))) { g_prefabSaveTarget = nullptr; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

// A 9-way anchor dropdown shared by every UI widget inspector.
static void AnchorCombo(const char* id, okay::UIAnchor& anchor, EditorState& ed) {
    static const char* kAnchors[] = {
        "Top-Left", "Top", "Top-Right", "Left", "Center", "Right",
        "Bottom-Left", "Bottom", "Bottom-Right"};
    int ai = (int)anchor;
    if (ImGui::Combo(id, &ai, kAnchors, 9)) { anchor = (okay::UIAnchor)ai; ed.dirty = true; }
}

// Size a 3D collider to wrap the object's MeshRenderer bounds (Unity's "fit").
// Collider size/offset are in local units (scaled by the Transform), matching
// how the mesh is drawn, so the wireframe lands right on the mesh.
static bool MeshBounds(GameObject* go, Vec3& lo, Vec3& hi) {
    auto* mr = go->GetComponent<MeshRenderer>();
    if (!mr) return false;
    mr->mesh.Bounds(lo, hi);
    return true;
}
static void FitBox(GameObject* go, BoxCollider3D* bc) {
    Vec3 lo, hi; if (!MeshBounds(go, lo, hi)) return;
    bc->size = {hi.x - lo.x, hi.y - lo.y, hi.z - lo.z};
    bc->offset = {(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
}
static void FitSphere(GameObject* go, SphereCollider3D* sc) {
    Vec3 lo, hi; if (!MeshBounds(go, lo, hi)) return;
    sc->radius = Mathf::Max(hi.x - lo.x, Mathf::Max(hi.y - lo.y, hi.z - lo.z)) * 0.5f;
    sc->offset = {(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
}
static void FitCapsule(GameObject* go, CapsuleCollider3D* cap) {
    Vec3 lo, hi; if (!MeshBounds(go, lo, hi)) return;
    cap->axis = 1; // Y-up
    cap->radius = Mathf::Max(hi.x - lo.x, hi.z - lo.z) * 0.5f;
    cap->height = hi.y - lo.y;
    cap->offset = {(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
}

// One row of the Game-Creator-style action editor: an op dropdown + a free-text
// args field + reorder/remove buttons. Returns 0 = none, 1 = remove, 2 = up,
// 3 = down (the caller applies the index change).
static int DrawActionItem(ActionList::Item& it, const char* const* ops, int nops,
                          int id, bool& dirty) {
    int action = 0;
    ImGui::PushID(id);
    if (it.op.empty()) it.op = ops[0];
    int cur = 0;
    for (int i = 0; i < nops; ++i) if (it.op == ops[i]) cur = i;
    ImGui::SetNextItemWidth(132);
    if (ImGui::Combo("##op", &cur, ops, nops)) { it.op = ops[cur]; dirty = true; }
    ImGui::SameLine();
    std::string joined;
    for (auto& a : it.args) { if (!joined.empty()) joined += ' '; joined += a; }
    char buf[256];
    std::strncpy(buf, joined.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
    ImGui::SetNextItemWidth(-78);
    if (ImGui::InputTextWithHint("##args", "args (space-separated)", buf, sizeof(buf))) {
        it.args.clear();
        std::stringstream ss(buf); std::string tok;
        while (ss >> tok) it.args.push_back(tok);
        dirty = true;
    }
    ImGui::SameLine(); if (ImGui::SmallButton("^")) action = 2;
    ImGui::SameLine(); if (ImGui::SmallButton("v")) action = 3;
    ImGui::SameLine(); if (ImGui::SmallButton("X")) { action = 1; dirty = true; }
    ImGui::PopID();
    return action;
}

// Apply a DrawActionItem reorder/remove result to a list at index i; returns
// the next index to visit.
static std::size_t ApplyItemAction(std::vector<ActionList::Item>& list, std::size_t i,
                                   int action, bool& dirty) {
    if (action == 1) { list.erase(list.begin() + i); return i; }
    if (action == 2 && i > 0) { std::swap(list[i], list[i - 1]); dirty = true; }
    if (action == 3 && i + 1 < list.size()) { std::swap(list[i], list[i + 1]); dirty = true; }
    return i + 1;
}

// If an asset is dropped on the previous widget, set `field` to its path.
static bool AcceptAssetPathField(std::string& field) {
    bool changed = false;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            field = std::string((const char*)p->Data);
            changed = true;
        }
        ImGui::EndDragDropTarget();
    }
    return changed;
}

// Unity-style component header: an enable/disable checkbox, the collapsing
// header title, and a right-click context menu offering "Remove Component"
// (which sets *toRemove, removed safely after the inspector finishes drawing).
// Returns whether the component body should be drawn.
static bool CompHeader(const char* label, okay::Component* comp, okay::Component** toRemove,
                       bool removable = true) {
    ImGui::PushID(comp);
    bool en = comp->enabled;
    if (ImGui::Checkbox("##enabled", &en)) comp->enabled = en;
    ImGui::SameLine();
    bool open = ImGui::CollapsingHeader(label,
                    ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
    if (ImGui::BeginPopupContextItem("##compctx")) {
        if (removable && ImGui::MenuItem("Remove Component")) *toRemove = comp;
        if (!removable) ImGui::TextDisabled("(required component)");
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return open;
}

void DrawInspector(EditorState& ed) {
    ImGui::Begin("Inspector", &g_showInspector);
    // Roomier spacing for the Inspector specifically (it was too compact).
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2(9, 9));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,     ImVec2(9, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(8, 6));

    // Lock (pin) the inspector to the current object so it stays put while you
    // click around the scene/hierarchy — Unity's inspector lock.
    static bool s_locked = false;
    static GameObject* s_pinned = nullptr;
    if (ImGui::Checkbox("Lock", &s_locked)) s_pinned = s_locked ? ed.selected() : nullptr;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep showing this object while selecting others");

    GameObject* go = ed.selected();
    if (s_locked && s_pinned) {
        bool alive = false;
        for (const auto& up : ed.scene().Objects()) if (up.get() == s_pinned) { alive = true; break; }
        if (alive) go = s_pinned; else { s_locked = false; s_pinned = nullptr; }
    }
    if (!go) {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::TextDisabled("  Select an object in the Hierarchy");
        ImGui::TextDisabled("  or Scene to edit it here.");
        ImGui::PopStyleVar(3);
        ImGui::End();
        return;
    }

    // Header (Unity-style): an active toggle + a type chip + the object name on
    // a tinted bar, then a Tag field + Save-as-Prefab, then a divider. The child
    // auto-sizes to its contents so there's no empty gap at the top.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.16f, 0.17f, 0.20f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
    ImGui::BeginChild("##objheader", ImVec2(0, 0),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);

    // Row 1: active toggle · type chip · editable name (fills the rest).
    ImGui::Checkbox("##active", &go->active);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Active in scene");
    ImGui::SameLine();
    const char* kind = ObjectKind(go);
    if (kind && *kind) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%s", kind);
        ImGui::SameLine();
    }
    char nameBuf[128];
    std::strncpy(nameBuf, go->name.c_str(), sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    // Leave room on the right for the Static toggle (Unity places it here).
    float staticW = ImGui::CalcTextSize("Static").x + ImGui::GetFrameHeight() +
                    ImGui::GetStyle().ItemInnerSpacing.x + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - staticW);
    if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) { go->name = nameBuf; ed.dirty = true; }
    ImGui::SameLine();
    if (ImGui::Checkbox("Static", &go->isStatic)) ed.dirty = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Marks the object as non-moving (saved with the scene)");

    // Row 2: Tag dropdown (Unity-style presets + custom) + Save-as-Prefab.
    static const char* kTagPresets[] = {"Untagged", "Player", "MainCamera", "Enemy",
                                        "Respawn", "Finish", "GameController", "UI"};
    std::string curTag = go->tag.empty() ? "Untagged" : go->tag;
    ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo("Tag", curTag.c_str())) {
        for (const char* t : kTagPresets) {
            bool sel = (curTag == t);
            if (ImGui::Selectable(t, sel)) {
                go->tag = (std::strcmp(t, "Untagged") == 0) ? "" : t; ed.dirty = true;
            }
        }
        ImGui::Separator();
        static char customTag[64] = "";
        ImGui::SetNextItemWidth(130);
        if (ImGui::InputTextWithHint("##customtag", "custom tag...", customTag, sizeof(customTag),
                                     ImGuiInputTextFlags_EnterReturnsTrue) && customTag[0]) {
            go->tag = customTag; ed.dirty = true; customTag[0] = '\0';
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    float btnW = ImGui::CalcTextSize("Save as Prefab").x + ImGui::GetStyle().FramePadding.x * 2;
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > btnW) ImGui::SameLine(ImGui::GetCursorPosX() + (avail - btnW));
    if (ImGui::Button("Save as Prefab")) {
        std::string p = go->name + ".okayprefab";
        if (SceneSerializer::SaveObjectToFile(*go, p)) ConsoleLog("Saved prefab " + p);
        else ConsoleLog("Prefab save failed");
    }
    ImGui::EndChild();
    // Drop a Project asset onto the object header to attach it: a .okay script
    // adds a Script component bound to that file; a .okaymat applies its look; an
    // image sets the texture. (Unity-style drag from Project into the Inspector.)
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            std::string path((const char*)p->Data), ext;
            if (auto d = path.find_last_of('.'); d != std::string::npos) ext = path.substr(d);
            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            if (ext == ".okay") {
                ed.PushUndo();
                auto* nsc = go->AddComponent<ScriptComponent>("okayscript");
                std::string err; nsc->LoadFile(path, &err); nsc->SetPath(path);
                ConsoleLog("Attached script " + path); ed.dirty = true;
            } else if (ext == ".okaymat") {
                if (auto* mr = go->GetComponent<MeshRenderer>()) {
                    Material m; if (Material::LoadFromFile(path, m)) {
                        ed.PushUndo(); m.ApplyTo(*mr); ed.dirty = true; ConsoleLog("Applied material");
                    }
                }
            } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
                ed.PushUndo();
                if (auto* mr = go->GetComponent<MeshRenderer>())        mr->texture = path;
                else if (auto* sr = go->GetComponent<SpriteRenderer>()) sr->texture = path;
                ed.dirty = true; ConsoleLog("Set texture");
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        ImGui::SetTooltip("Drop a script/material/image here to attach it");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    Component* toRemove = nullptr; // removed after drawing (avoids dangling use)

    // UI layering + quick-center for any screen-space widget (draw order is the
    // scene's object order; later = on top, which is also what picking selects).
    if (IsUIElement(go)) {
        if (ImGui::Button("Bring to Front")) { ed.scene().MoveToFront(go); ed.dirty = true; }
        ImGui::SameLine();
        if (ImGui::Button("Send to Back"))   { ed.scene().MoveToBack(go);  ed.dirty = true; }
        ImGui::SameLine();
        if (ImGui::Button("Center in Canvas")) {
            UIRect r = GetUIRect(go);
            if (r.position) {
                // Center the resolved rect on the canvas for the widget's anchor.
                float cw = UICanvas::Width(), ch = UICanvas::Height();
                Vec2 term = ResolveAnchor(r.anchor, Vec2{0, 0}, r.size, cw, ch);
                r.position->x = (cw - r.size.x) * 0.5f - term.x;
                r.position->y = (ch - r.size.y) * 0.5f - term.y;
                ed.dirty = true;
            }
        }
        // Anchor presets (Unity's 3x3): pick a corner/edge/center; the widget
        // keeps its on-screen position (its offset is recomputed for the new
        // anchor) so re-anchoring never makes it jump.
        UIRect ar = GetUIRect(go);
        if (ar.anchorPtr && ar.position) {
            ImGui::TextDisabled("Anchor preset:");
            float cw = UICanvas::Width(), ch = UICanvas::Height();
            Vec2 resolved = ResolveAnchor(*ar.anchorPtr, *ar.position, ar.size, cw, ch);
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    if (col) ImGui::SameLine();
                    UIAnchor cand = (UIAnchor)(row * 3 + col);
                    bool sel = (*ar.anchorPtr == cand);
                    if (sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.75f, 1.0f));
                    ImGui::PushID(row * 3 + col);
                    if (ImGui::Button("##anc", ImVec2(20, 18))) {
                        *ar.anchorPtr = cand;       // preserve on-screen position
                        Vec2 term = ResolveAnchor(cand, Vec2{0, 0}, ar.size, cw, ch);
                        ar.position->x = resolved.x - term.x;
                        ar.position->y = resolved.y - term.y;
                        ed.dirty = true;
                    }
                    ImGui::PopID();
                    if (sel) ImGui::PopStyleColor();
                }
            }
        }
        // Stretch helpers for resizable widgets: fill the canvas on an axis.
        if (ar.sizePtr && ar.position) {
            float cw = UICanvas::Width(), ch = UICanvas::Height();
            auto fill = [&](bool x, bool y) {
                if (x) ar.sizePtr->x = cw;
                if (y) ar.sizePtr->y = ch;
                Vec2 term = ResolveAnchor(*ar.anchorPtr, Vec2{0, 0}, *ar.sizePtr, cw, ch);
                if (x) ar.position->x = -term.x;   // resolved left -> 0
                if (y) ar.position->y = -term.y;   // resolved top  -> 0
                ed.dirty = true;
            };
            if (ImGui::Button("Fill Width"))  fill(true, false);
            ImGui::SameLine();
            if (ImGui::Button("Fill Height")) fill(false, true);
            ImGui::SameLine();
            if (ImGui::Button("Fill Canvas")) fill(true, true);
        }
        ImGui::Spacing();
    }

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        Transform* t = go->transform;
        float pos[3] = {t->localPosition.x, t->localPosition.y, t->localPosition.z};
        if (ImGui::DragFloat3("Position", pos, 0.05f)) {
            t->localPosition = {pos[0], pos[1], pos[2]}; ed.dirty = true;
        }
        if (ImGui::IsItemActivated()) ed.PushUndo();
        Vec3& e = g_euler[go];
        float rot[3] = {e.x, e.y, e.z};
        if (ImGui::DragFloat3("Rotation", rot, 1.0f)) {
            e = {rot[0], rot[1], rot[2]};
            t->localRotation = Quat::Euler(e); ed.dirty = true;
        }
        if (ImGui::IsItemActivated()) ed.PushUndo();
        // Cameras aren't scalable (scaling warps the view); show it read-only.
        bool isCam = go->GetComponent<Camera>() != nullptr;
        float scl[3] = {t->localScale.x, t->localScale.y, t->localScale.z};
        if (isCam) ImGui::BeginDisabled();
        if (ImGui::DragFloat3("Scale", scl, 0.05f)) {
            t->localScale = {scl[0], scl[1], scl[2]}; ed.dirty = true;
        }
        if (ImGui::IsItemActivated()) ed.PushUndo();
        if (isCam) { ImGui::EndDisabled(); ImGui::TextDisabled("(camera scale is locked)"); }
        // Quick resets (Unity's right-click-component conveniences).
        if (ImGui::SmallButton("Reset Pos")) { ed.PushUndo(); t->localPosition = {0, 0, 0}; ed.dirty = true; }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset Rot")) { ed.PushUndo(); g_euler[go] = {0, 0, 0}; t->localRotation = Quat::Euler({0, 0, 0}); ed.dirty = true; }
        ImGui::SameLine();
        if (!isCam && ImGui::SmallButton("Reset Scale")) { ed.PushUndo(); t->localScale = {1, 1, 1}; ed.dirty = true; }
    }

    if (auto* sr = go->GetComponent<SpriteRenderer>()) {
        if (CompHeader("Sprite Renderer", sr, &toRemove)) {
            float col[4] = {sr->color.r, sr->color.g, sr->color.b, sr->color.a};
            if (ImGui::ColorEdit4("Color##sprite", col)) {
                sr->color = {col[0], col[1], col[2], col[3]}; ed.dirty = true;
            }
            float size[2] = {sr->size.x, sr->size.y};
            if (ImGui::DragFloat2("Size", size, 0.05f, 0.0f, 1000.0f)) {
                sr->size = {size[0], size[1]}; ed.dirty = true;
            }
            char tex[256];
            std::strncpy(tex, sr->texture.c_str(), sizeof(tex) - 1);
            tex[sizeof(tex) - 1] = '\0';
            if (ImGui::InputText("Texture##sprite", tex, sizeof(tex))) { sr->texture = tex; ed.dirty = true; }
            if (AcceptAssetPathField(sr->texture)) ed.dirty = true;   // drop from Project
            if (ImGui::DragInt("Sort Order##sprite", &sr->sortOrder, 0.1f, -1000, 1000)) ed.dirty = true;
            if (ImGui::Checkbox("Flip X##sprite", &sr->flipX)) ed.dirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Flip Y##sprite", &sr->flipY)) ed.dirty = true;
            ImGui::TextDisabled("image file (PNG/JPG); higher Sort Order draws on top");
            if (ImGui::SmallButton("Remove##sprite")) toRemove = sr;
        }
    }
    if (auto* mr = go->GetComponent<MeshRenderer>()) {
        if (CompHeader("Mesh Renderer", mr, &toRemove)) {
            float col[4] = {mr->color.r, mr->color.g, mr->color.b, mr->color.a};
            if (ImGui::ColorEdit4("Color##mesh", col)) {
                mr->color = {col[0], col[1], col[2], col[3]}; ed.dirty = true;
            }
            ImGui::Checkbox("Wireframe", &mr->wireframe);
            ImGui::SameLine();
            if (ImGui::Checkbox("Unlit", &mr->unlit)) ed.dirty = true;
            // Material: emissive glow + specular highlight.
            float em[3] = {mr->emissive.r, mr->emissive.g, mr->emissive.b};
            if (ImGui::ColorEdit3("Emissive##mesh", em)) {
                mr->emissive = {em[0], em[1], em[2], 1.0f}; ed.dirty = true;
            }
            if (ImGui::SliderFloat("Specular##mesh", &mr->specular, 0.0f, 1.0f)) ed.dirty = true;
            if (mr->specular > 0.0f)
                if (ImGui::SliderFloat("Shininess##mesh", &mr->shininess, 1.0f, 128.0f)) ed.dirty = true;
            const char* shapes[] = {"Cube", "Pyramid", "Wedge", "Quad", "Plane", "Sphere",
                                    "Cylinder", "Cone", "Tube", "Torus", "Capsule", "Icosphere", "Grid"};
            const int kShapeCount = 13;
            int shapeIdx = -1;
            for (int i = 0; i < kShapeCount; ++i) if (mr->mesh.name == shapes[i]) shapeIdx = i;
            if (ImGui::Combo("Primitive", &shapeIdx, shapes, kShapeCount)) {
                mr->mesh = Mesh::FromName(shapes[shapeIdx]);
                mr->meshPath.clear();
                ed.dirty = true;
            }
            // Import a 3D model from an .OBJ file.
            char mp[256];
            std::strncpy(mp, mr->meshPath.c_str(), sizeof(mp) - 1);
            mp[sizeof(mp) - 1] = '\0';
            if (ImGui::InputText("OBJ File##mesh", mp, sizeof(mp))) { mr->meshPath = mp; ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Load##obj") && mr->meshPath[0]) {
                bool ok = false;
                Mesh m = Mesh::LoadOBJ(mr->meshPath, &ok);
                if (ok && !m.vertices.empty()) { mr->mesh = m; ConsoleLog("Loaded " + mr->meshPath); }
                else ConsoleLog("OBJ load failed: " + mr->meshPath);
                ed.dirty = true;
            }
            // Texture (planar/box-mapped, tinted by Color). Blank = untextured.
            char tex[256];
            std::strncpy(tex, mr->texture.c_str(), sizeof(tex) - 1);
            tex[sizeof(tex) - 1] = '\0';
            if (ImGui::InputText("Texture##mesh", tex, sizeof(tex))) { mr->texture = tex; ed.dirty = true; }
            if (AcceptAssetPathField(mr->texture)) ed.dirty = true;   // drop from Project
            if (!mr->texture.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear##tex")) { mr->texture.clear(); ed.dirty = true; }
                float til[2] = {mr->tiling.x, mr->tiling.y};
                if (ImGui::DragFloat2("Tiling##mesh", til, 0.05f, 0.01f, 64.0f)) {
                    mr->tiling = {til[0], til[1]}; ed.dirty = true;
                }
            }
            ImGui::TextDisabled("%d verts, %d triangles",
                                (int)mr->mesh.vertices.size(), mr->mesh.TriangleCount());
            if (ImGui::SmallButton("Subdivide##mesh")) { mr->mesh.Subdivide(); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Smooth##mesh")) {     // subdivide + reproject to sphere
                Vec3 sz = mr->mesh.Size();
                mr->mesh.Subdivide();
                mr->mesh.ProjectToSphere(0.5f * std::fmax(sz.x, std::fmax(sz.y, sz.z)));
                ed.dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Weld##mesh")) {       // merge coincident verts
                int n = mr->mesh.WeldVertices();
                ConsoleLog("Welded " + std::to_string(n) + " duplicate verts");
                ed.dirty = true;
            }
            if (ImGui::SmallButton("Recenter##mesh")) { mr->mesh.RecenterToOrigin(); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Ground##mesh")) { mr->mesh.GroundPivot(); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Fit 1u##mesh")) { mr->mesh.ScaleToFit(1.0f); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove##mesh")) toRemove = mr;
            // Material presets: save the current look as a reusable .okaymat, or
            // load one (also accepts a .okaymat dropped from the Project).
            ImGui::SeparatorText("Material preset");
            static char matPath[256] = "Assets/Material.okaymat";
            ImGui::InputText("##matpath", matPath, sizeof(matPath));
            std::string dropped;
            if (AcceptAssetPathField(dropped) && dropped.size() < sizeof(matPath)) {
                std::strncpy(matPath, dropped.c_str(), sizeof(matPath) - 1); matPath[sizeof(matPath)-1]='\0';
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Save##mat")) {
                if (Material::FromRenderer(*mr).SaveToFile(matPath)) ConsoleLog(std::string("Saved material ") + matPath);
                else ConsoleLog("Material save failed");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Load##mat")) {
                Material m;
                if (Material::LoadFromFile(matPath, m)) { m.ApplyTo(*mr); ed.dirty = true; ConsoleLog(std::string("Loaded material ") + matPath); }
                else ConsoleLog("Material load failed");
            }
            // One-click physics: add a Rigidbody3D + a box collider fitted to the
            // mesh, so a primitive drops and collides immediately on Play.
            if (!go->GetComponent<Rigidbody3D>() || !go->GetComponent<BoxCollider3D>()) {
                if (ImGui::SmallButton("Add Physics (Rigidbody + Box)##mesh")) {
                    if (!go->GetComponent<Rigidbody3D>()) go->AddComponent<Rigidbody3D>();
                    BoxCollider3D* bc = go->GetComponent<BoxCollider3D>();
                    if (!bc) bc = go->AddComponent<BoxCollider3D>();
                    FitBox(go, bc);
                    ed.dirty = true;
                    ConsoleLog("Added Rigidbody3D + fitted BoxCollider3D");
                }
            }
        }
    }
    if (auto* tr = go->GetComponent<Terrain>()) {
        if (CompHeader("Terrain", tr, &toRemove)) {
            float c[4] = {tr->color.r, tr->color.g, tr->color.b, tr->color.a};
            if (ImGui::ColorEdit4("Color##terr", c)) { tr->color = {c[0],c[1],c[2],c[3]}; tr->Apply(); ed.dirty = true; }
            int res = tr->resolution;
            if (ImGui::SliderInt("Resolution##terr", &res, 4, 128) && res != tr->resolution) {
                tr->Resize(res); tr->Apply(); ed.dirty = true;
            }
            if (ImGui::DragFloat("Size##terr", &tr->size, 0.5f, 2.0f, 1000.0f)) { tr->Apply(); ed.dirty = true; }

            ImGui::SeparatorText("Sculpt brush (drag in the 3D view)");
            ImGui::Checkbox("Sculpt##terr", &g_terrainSculpt);
            ImGui::SameLine(); ImGui::TextDisabled("(Shift = lower)");
            ImGui::SliderFloat("Radius##terr", &g_terrainRadius, 0.5f, 30.0f);
            ImGui::SliderFloat("Strength##terr", &g_terrainStrength, 0.1f, 20.0f);

            ImGui::SeparatorText("Generate");
            if (ImGui::SmallButton("Flatten##terr"))   { tr->Flatten(0.0f); tr->Apply(); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Smooth##terr"))    { tr->Smooth(); tr->Apply(); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Randomize##terr")) { tr->Randomize(2.0f, (unsigned)ImGui::GetTime()); tr->Apply(); ed.dirty = true; }
            if (ImGui::SmallButton("Hills##terr"))     { tr->Flatten(0.0f); tr->Hills(8, 8.0f, (unsigned)ImGui::GetTime()); tr->Apply(); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove##terr")) toRemove = tr;
        }
    }
    if (auto* li = go->GetComponent<Light>()) {
        if (CompHeader("Light", li, &toRemove)) {
            const char* types[] = {"Directional", "Point", "Spot"};
            int ty = (int)li->type;
            if (ImGui::Combo("Type##light", &ty, types, 3)) { li->type = (Light::Type)ty; ed.dirty = true; }

            // Color mode: an RGB swatch, or a Kelvin temperature like a real bulb.
            if (ImGui::Checkbox("Use Temperature##light", &li->useTemperature)) ed.dirty = true;
            if (li->useTemperature) {
                if (ImGui::SliderFloat("Kelvin##light", &li->temperature, 1500.0f, 15000.0f, "%.0f K"))
                    ed.dirty = true;
                Color k = Light::KelvinToColor(li->temperature);
                ImGui::SameLine(); ImGui::ColorButton("##kprev", ImVec4(k.r, k.g, k.b, 1.0f));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("2700K warm  -  6500K daylight  -  10000K cool");
            } else {
                float c[4] = {li->color.r, li->color.g, li->color.b, li->color.a};
                if (ImGui::ColorEdit4("Color##light", c)) { li->color = {c[0], c[1], c[2], c[3]}; ed.dirty = true; }
            }
            if (ImGui::DragFloat("Intensity##light", &li->intensity, 0.02f, 0.0f, 8.0f)) ed.dirty = true;

            ImGui::SeparatorText("Ambient (scene floor)");
            if (ImGui::SliderFloat("Ambient##light", &li->ambient, 0.0f, 1.0f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unlit floor brightness (taken from the first light)");
            float ac[3] = {li->ambientColor.r, li->ambientColor.g, li->ambientColor.b};
            if (ImGui::ColorEdit3("Ambient Tint##light", ac)) { li->ambientColor = {ac[0], ac[1], ac[2], 1.0f}; ed.dirty = true; }

            if (li->type != Light::Type::Directional) {
                ImGui::SeparatorText(li->type == Light::Type::Spot ? "Spot" : "Point");
                if (ImGui::DragFloat("Range##light", &li->range, 0.2f, 0.1f, 500.0f)) ed.dirty = true;
            }
            if (li->type == Light::Type::Spot) {
                if (ImGui::SliderFloat("Spot Angle##light", &li->spotAngle, 5.0f, 170.0f)) ed.dirty = true;
                if (ImGui::SliderFloat("Softness##light", &li->spotSoftness, 0.0f, 1.0f)) ed.dirty = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = crisp cone edge, 1 = very feathered");
            }
            ImGui::TextDisabled(li->type == Light::Type::Point
                ? "Radiates from this object's position out to Range."
                : "Rotate this object to aim the light (its +Z is the direction).");
            if (ImGui::SmallButton("Remove##light")) toRemove = li;
        }
    }
    if (auto* cam = go->GetComponent<Camera>()) {
        if (CompHeader("Camera", cam, &toRemove)) {
            int proj = (int)cam->projection;
            const char* projs[] = {"Orthographic", "Perspective"};
            if (ImGui::Combo("Projection", &proj, projs, 2)) { cam->projection = (Camera::Projection)proj; ed.dirty = true; }
            if (cam->projection == Camera::Projection::Orthographic) {
                if (ImGui::DragFloat("Size", &cam->orthographicSize, 0.1f, 0.1f, 1000.0f)) ed.dirty = true;
            } else {
                if (ImGui::SliderFloat("Field of View", &cam->fieldOfView, 10.0f, 170.0f, "%.0f deg")) ed.dirty = true;
            }

            ImGui::SeparatorText("Background");
            int cf = (int)cam->clearFlags;
            const char* cfs[] = {"Skybox", "Solid Color"};
            if (ImGui::Combo("Clear Flags", &cf, cfs, 2)) { cam->clearFlags = (Camera::ClearFlags)cf; ed.dirty = true; }
            if (cam->clearFlags == Camera::ClearFlags::SolidColor) {
                float bg[4] = {cam->backgroundColor.r, cam->backgroundColor.g, cam->backgroundColor.b, cam->backgroundColor.a};
                if (ImGui::ColorEdit4("Background", bg)) { cam->backgroundColor = {bg[0], bg[1], bg[2], bg[3]}; ed.dirty = true; }
            }

            ImGui::SeparatorText("Clipping Planes");
            if (ImGui::DragFloat("Near", &cam->nearClip, 0.01f, 0.001f, 100.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Far",  &cam->farClip, 1.0f, 1.0f, 100000.0f)) ed.dirty = true;

            ImGui::Spacing();
            if (ImGui::Checkbox("Main", &cam->main)) ed.dirty = true;
            ImGui::SameLine(); ImGui::SetNextItemWidth(90);
            if (ImGui::DragFloat("Depth", &cam->depth, 0.1f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Render priority; the highest-depth camera is used as main");
            if (ImGui::SmallButton("Remove##cam")) toRemove = cam;
        }
    }
    if (auto* rb2 = go->GetComponent<Rigidbody2D>())
        if (CompHeader("Rigidbody2D", rb2, &toRemove)) {
            auto* rb = go->GetComponent<Rigidbody2D>();
            int bt = (int)rb->bodyType;
            const char* types[] = {"Dynamic", "Kinematic", "Static"};
            if (ImGui::Combo("Body Type", &bt, types, 3)) rb->bodyType = (Rigidbody2D::BodyType)bt;
            ImGui::DragFloat("Gravity Scale", &rb->gravityScale, 0.05f);
            ImGui::DragFloat("Mass", &rb->mass, 0.05f, 0.01f, 1000.0f);
            ImGui::DragFloat("Bounciness", &rb->bounciness, 0.01f, 0.0f, 1.0f);
            if (ImGui::SmallButton("Remove##rb")) toRemove = rb;
        }
    if (auto* bc = go->GetComponent<BoxCollider2D>()) {
        if (CompHeader("Box Collider 2D", bc, &toRemove)) {
            float sz[2] = {bc->size.x, bc->size.y};
            if (ImGui::DragFloat2("Size##bc", sz, 0.05f, 0.0f, 1000.0f)) { bc->size = {sz[0], sz[1]}; ed.dirty = true; }
            float off[2] = {bc->offset.x, bc->offset.y};
            if (ImGui::DragFloat2("Offset##bc", off, 0.05f)) { bc->offset = {off[0], off[1]}; ed.dirty = true; }
            ImGui::Checkbox("Trigger##bc", &bc->isTrigger);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80); ImGui::DragInt("Layer##bc", &bc->layer, 0.1f, 0, 31);
            if (ImGui::SmallButton("Remove##bc")) toRemove = bc;
        }
    }
    if (auto* cc = go->GetComponent<CircleCollider2D>()) {
        if (CompHeader("Circle Collider 2D", cc, &toRemove)) {
            ImGui::DragFloat("Radius##cc", &cc->radius, 0.05f, 0.0f, 1000.0f);
            float off[2] = {cc->offset.x, cc->offset.y};
            if (ImGui::DragFloat2("Offset##cc", off, 0.05f)) { cc->offset = {off[0], off[1]}; ed.dirty = true; }
            ImGui::Checkbox("Trigger##cc", &cc->isTrigger);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80); ImGui::DragInt("Layer##cc", &cc->layer, 0.1f, 0, 31);
            if (ImGui::SmallButton("Remove##cc")) toRemove = cc;
        }
    }
    if (auto* cap = go->GetComponent<CapsuleCollider2D>()) {
        if (CompHeader("Capsule Collider 2D", cap, &toRemove)) {
            float sz[2] = {cap->size.x, cap->size.y};
            if (ImGui::DragFloat2("Size##cap2", sz, 0.05f, 0.0f, 1000.0f)) { cap->size = {sz[0], sz[1]}; ed.dirty = true; }
            int dir = (int)cap->direction;
            const char* dirs[] = {"Vertical", "Horizontal"};
            if (ImGui::Combo("Direction##cap2", &dir, dirs, 2)) cap->direction = (CapsuleCollider2D::Direction)dir;
            float off[2] = {cap->offset.x, cap->offset.y};
            if (ImGui::DragFloat2("Offset##cap2", off, 0.05f)) { cap->offset = {off[0], off[1]}; ed.dirty = true; }
            ImGui::Checkbox("Trigger##cap2", &cap->isTrigger);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80); ImGui::DragInt("Layer##cap2", &cap->layer, 0.1f, 0, 31);
            if (ImGui::SmallButton("Remove##cap2")) toRemove = cap;
        }
    }
    if (auto* rb3 = go->GetComponent<Rigidbody3D>())
        if (CompHeader("Rigidbody3D", rb3, &toRemove)) {
            auto* rb = go->GetComponent<Rigidbody3D>();
            int bt = (int)rb->bodyType;
            const char* types[] = {"Dynamic", "Kinematic", "Static"};
            if (ImGui::Combo("Body Type##rb3", &bt, types, 3)) rb->bodyType = (Rigidbody3D::BodyType)bt;
            ImGui::DragFloat("Gravity Scale##rb3", &rb->gravityScale, 0.05f);
            ImGui::DragFloat("Mass##rb3", &rb->mass, 0.05f, 0.01f, 1000.0f);
            ImGui::DragFloat("Drag##rb3", &rb->drag, 0.01f, 0.0f, 100.0f);
            ImGui::DragFloat("Bounciness##rb3", &rb->bounciness, 0.01f, 0.0f, 1.0f);
            ImGui::TextDisabled("Freeze position");
            ImGui::SameLine(); ImGui::Checkbox("X##fz", &rb->freezeX);
            ImGui::SameLine(); ImGui::Checkbox("Y##fz", &rb->freezeY);
            ImGui::SameLine(); ImGui::Checkbox("Z##fz", &rb->freezeZ);
            if (ImGui::SmallButton("Remove##rb3")) toRemove = rb;
        }
    if (auto* bc = go->GetComponent<BoxCollider3D>()) {
        if (CompHeader("Box Collider 3D", bc, &toRemove)) {
            float sz[3] = {bc->size.x, bc->size.y, bc->size.z};
            if (ImGui::DragFloat3("Size##bc3", sz, 0.05f, 0.0f, 1000.0f)) { bc->size = {sz[0], sz[1], sz[2]}; ed.dirty = true; }
            float off[3] = {bc->offset.x, bc->offset.y, bc->offset.z};
            if (ImGui::DragFloat3("Offset##bc3", off, 0.05f)) { bc->offset = {off[0], off[1], off[2]}; ed.dirty = true; }
            ImGui::Checkbox("Trigger##bc3", &bc->isTrigger);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80); ImGui::DragInt("Layer##bc3", &bc->layer, 0.1f, 0, 31);
            if (go->GetComponent<MeshRenderer>() && ImGui::SmallButton("Fit to mesh##bc3")) {
                FitBox(go, bc); ed.dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove##bc3")) toRemove = bc;
        }
    }
    if (auto* sc = go->GetComponent<SphereCollider3D>()) {
        if (CompHeader("Sphere Collider 3D", sc, &toRemove)) {
            ImGui::DragFloat("Radius##sc3", &sc->radius, 0.05f, 0.0f, 1000.0f);
            float off[3] = {sc->offset.x, sc->offset.y, sc->offset.z};
            if (ImGui::DragFloat3("Offset##sc3", off, 0.05f)) { sc->offset = {off[0], off[1], off[2]}; ed.dirty = true; }
            ImGui::Checkbox("Trigger##sc3", &sc->isTrigger);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80); ImGui::DragInt("Layer##sc3", &sc->layer, 0.1f, 0, 31);
            if (go->GetComponent<MeshRenderer>() && ImGui::SmallButton("Fit to mesh##sc3")) {
                FitSphere(go, sc); ed.dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove##sc3")) toRemove = sc;
        }
    }
    if (auto* cap = go->GetComponent<CapsuleCollider3D>()) {
        if (CompHeader("Capsule Collider 3D", cap, &toRemove)) {
            ImGui::DragFloat("Radius##cap3", &cap->radius, 0.05f, 0.0f, 1000.0f);
            ImGui::DragFloat("Height##cap3", &cap->height, 0.05f, 0.0f, 1000.0f);
            const char* axes[] = {"X", "Y", "Z"};
            ImGui::Combo("Axis##cap3", &cap->axis, axes, 3);
            float off[3] = {cap->offset.x, cap->offset.y, cap->offset.z};
            if (ImGui::DragFloat3("Offset##cap3", off, 0.05f)) { cap->offset = {off[0], off[1], off[2]}; ed.dirty = true; }
            ImGui::Checkbox("Trigger##cap3", &cap->isTrigger);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80); ImGui::DragInt("Layer##cap3", &cap->layer, 0.1f, 0, 31);
            if (go->GetComponent<MeshRenderer>() && ImGui::SmallButton("Fit to mesh##cap3")) {
                FitCapsule(go, cap); ed.dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove##cap3")) toRemove = cap;
        }
    }
    if (auto* sc = go->GetComponent<ScriptComponent>()) {
        if (CompHeader("Script", sc, &toRemove)) {
            static std::vector<std::string> avail = AvailableScriptLanguages();
            std::vector<const char*> items; items.reserve(avail.size());
            for (auto& s : avail) items.push_back(s.c_str());
            int li = 0;
            for (int i = 0; i < (int)avail.size(); ++i)
                if (avail[i] == sc->Language()) li = i;
            if (ImGui::Combo("Language", &li, items.data(), (int)items.size()))
                sc->SetLanguage(avail[li]);
            if (sc->Path().empty()) ImGui::TextDisabled("inline script");
            else ImGui::TextDisabled("file: %s", sc->Path().c_str());
            ImGui::TextWrapped("Edit code in the Script Editor panel (View > Script Editor).");
            if (ImGui::SmallButton("Remove##script")) toRemove = sc;
        }
    }
    if (auto* cc = go->GetComponent<CharacterController2D>()) {
        if (CompHeader("Character Controller 2D", cc, &toRemove)) {
            int m = (int)cc->mode;
            const char* modes[] = {"Top-Down", "Platformer"};
            if (ImGui::Combo("Mode##cc2", &m, modes, 2)) { cc->mode = (CharacterController2D::Mode)m; ed.dirty = true; }
            if (ImGui::DragFloat("Speed##cc2", &cc->speed, 0.1f, 0.0f, 200.0f)) ed.dirty = true;
            if (cc->mode == CharacterController2D::Mode::Platformer)
                if (ImGui::DragFloat("Jump Force##cc2", &cc->jumpForce, 0.1f, 0.0f, 200.0f)) ed.dirty = true;
            ImGui::TextDisabled("WASD / arrows. Uses a Rigidbody2D if present.");
            if (ImGui::SmallButton("Remove##cc2")) toRemove = cc;
        }
    }
    if (auto* cc = go->GetComponent<CharacterController3D>()) {
        if (CompHeader("Character Controller 3D", cc, &toRemove)) {
            if (ImGui::DragFloat("Speed##cc3", &cc->speed, 0.1f, 0.0f, 200.0f)) ed.dirty = true;
            if (ImGui::Checkbox("Can Jump##cc3", &cc->canJump)) ed.dirty = true;
            if (cc->canJump)
                if (ImGui::DragFloat("Jump Force##cc3", &cc->jumpForce, 0.1f, 0.0f, 200.0f)) ed.dirty = true;
            ImGui::TextDisabled("WASD / arrows on XZ. Uses a Rigidbody3D if present.");
            if (ImGui::SmallButton("Remove##cc3")) toRemove = cc;
        }
    }
    if (auto* ft = go->GetComponent<FollowTarget2D>()) {
        if (CompHeader("Follow Target 2D", ft, &toRemove)) {
            char tb[64];
            std::strncpy(tb, ft->target.c_str(), sizeof(tb) - 1); tb[sizeof(tb) - 1] = '\0';
            if (ImGui::InputText("Target##ft", tb, sizeof(tb))) { ft->target = tb; ed.dirty = true; }
            if (ImGui::DragFloat("Speed##ft", &ft->speed, 0.05f, 0.0f, 200.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Stop Distance##ft", &ft->stopDistance, 0.05f, 0.0f, 1000.0f)) ed.dirty = true;
            ImGui::TextDisabled("Chases the named object (enemy AI / homing).");
            if (ImGui::SmallButton("Remove##ft")) toRemove = ft;
        }
    }
    if (auto* al = go->GetComponent<ActionList>()) {
        if (CompHeader("Actions (Visual Script)", al, &toRemove)) {
            // Trigger -> Conditions -> Instructions, Game-Creator style.
            const char* trigs[] = {"On Start", "On Update", "On Key", "On Collision",
                                   "On Click", "On Key Up", "On Message"};
            int ti = (int)al->trigger;
            ImGui::SetNextItemWidth(150);
            if (ImGui::Combo("Trigger", &ti, trigs, IM_ARRAYSIZE(trigs))) { al->trigger = (ActionList::Trigger)ti; ed.dirty = true; }
            if (al->trigger == ActionList::Trigger::OnKey ||
                al->trigger == ActionList::Trigger::OnKeyUp) {
                ImGui::SameLine();
                char kb[16];
                std::strncpy(kb, al->triggerKey.c_str(), sizeof(kb) - 1); kb[sizeof(kb) - 1] = '\0';
                ImGui::SetNextItemWidth(50);
                if (ImGui::InputText("Key##al", kb, sizeof(kb))) { al->triggerKey = kb; ed.dirty = true; }
            } else if (al->trigger == ActionList::Trigger::OnMessage) {
                ImGui::SameLine();
                char mb[64];
                std::strncpy(mb, al->triggerKey.c_str(), sizeof(mb) - 1); mb[sizeof(mb) - 1] = '\0';
                ImGui::SetNextItemWidth(110);
                if (ImGui::InputText("Message##al", mb, sizeof(mb))) { al->triggerKey = mb; ed.dirty = true; }
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Once", &al->once)) ed.dirty = true;

            static const char* condOps[] = {"always", "key", "key_down", "key_up", "mouse",
                "mouse_down", "chance", "var_eq", "var_neq", "var_gt", "var_lt",
                "prefs_eq", "prefs_gt", "has_tag", "is_active",
                "dist_lt", "dist_gt", "exists"};
            static const char* instOps[] = {"move", "set_pos", "rotate", "set_scale",
                "set_scale3", "move_toward", "look_at", "wait", "goto", "stop",
                "set_var", "add_var", "mul_var", "div_var", "copy_var", "rand_var",
                "set_active", "set_text", "set_color", "velocity",
                "impulse", "emit", "play_anim", "play_sound", "set_cam", "set_bg",
                "set_light", "set_ambient", "set_timescale", "send", "spawn", "spawn3",
                "destroy", "destroy_obj", "activate", "deactivate", "set_tag",
                "set_prefs", "add_prefs", "save_prefs",
                "net_host", "net_join", "net_send", "net_send_reliable", "net_set", "net_spawn",
                "net_ready", "net_start_match", "net_kick", "net_disconnect",
                "steam_unlock", "steam_set_stat", "steam_inc_stat",
                "load_scene", "load_scene_index", "load_next_scene", "log"};

            ImGui::SeparatorText("Conditions (all must pass)");
            for (std::size_t i = 0; i < al->conditions.size();) {
                int act = DrawActionItem(al->conditions[i], condOps, IM_ARRAYSIZE(condOps), (int)i, ed.dirty);
                i = ApplyItemAction(al->conditions, i, act, ed.dirty);
            }
            if (ImGui::SmallButton("+ Condition")) { al->conditions.push_back({"always", {}}); ed.dirty = true; }

            ImGui::SeparatorText("Instructions (run top to bottom)");
            for (std::size_t i = 0; i < al->instructions.size();) {
                int act = DrawActionItem(al->instructions[i], instOps, IM_ARRAYSIZE(instOps), 1000 + (int)i, ed.dirty);
                i = ApplyItemAction(al->instructions, i, act, ed.dirty);
            }
            if (ImGui::SmallButton("+ Instruction")) { al->instructions.push_back({"move", {}}); ed.dirty = true; }

            ImGui::Spacing();
            if (ImGui::SmallButton("Remove##al")) toRemove = al;
        }
    }
    if (auto* a = go->GetComponent<AudioSource>()) {
        if (CompHeader("Audio Source", a, &toRemove)) {
            ImGui::DragFloat("Volume", &a->volume, 0.01f, 0.0f, 1.0f);
            ImGui::Checkbox("Loop", &a->loop);
            ImGui::SameLine();
            ImGui::Checkbox("Play On Awake", &a->playOnAwake);
            if (ImGui::Button("Beep"))  a->clip = AudioClip::Sine(440.0f, 0.3f);
            ImGui::SameLine();
            if (ImGui::Button("Noise")) a->clip = AudioClip::Noise(0.3f);
            ImGui::SameLine();
            if (ImGui::Button("Play"))  a->Play();
            char cb[256];
            std::strncpy(cb, a->clipPath.c_str(), sizeof(cb) - 1);
            cb[sizeof(cb) - 1] = '\0';
            if (ImGui::InputText("WAV File##audio", cb, sizeof(cb))) { a->clipPath = cb; ed.dirty = true; }
            if (AcceptAssetPathField(a->clipPath)) ed.dirty = true;   // drop from Project
            ImGui::TextDisabled("WAV path loads in the built game; %.2fs clip", a->clip.Duration());
            if (ImGui::Checkbox("3D (spatial)", &a->spatial)) ed.dirty = true;
            if (a->spatial) {
                if (ImGui::DragFloat("Min Distance##audio", &a->minDistance, 0.1f, 0.0f, 1000.0f)) ed.dirty = true;
                if (ImGui::DragFloat("Max Distance##audio", &a->maxDistance, 0.1f, 0.0f, 5000.0f)) ed.dirty = true;
            }
            if (ImGui::SmallButton("Remove##audio")) toRemove = a;
        }
    }

    if (auto* mv = go->GetComponent<Mover>()) {
        if (CompHeader("Mover", mv, &toRemove)) {
            float v[3] = {mv->velocity.x, mv->velocity.y, mv->velocity.z};
            if (ImGui::DragFloat3("Velocity##mv", v, 0.05f)) { mv->velocity = {v[0], v[1], v[2]}; ed.dirty = true; }
            ImGui::TextDisabled("units / second");
            if (ImGui::SmallButton("Remove##mv")) toRemove = mv;
        }
    }
    if (auto* sp = go->GetComponent<Spinner>()) {
        if (CompHeader("Spinner", sp, &toRemove)) {
            float v[3] = {sp->angularVelocity.x, sp->angularVelocity.y, sp->angularVelocity.z};
            if (ImGui::DragFloat3("Angular Vel##sp", v, 0.5f)) { sp->angularVelocity = {v[0], v[1], v[2]}; ed.dirty = true; }
            ImGui::TextDisabled("degrees / second (X, Y, Z)");
            if (ImGui::SmallButton("Remove##sp")) toRemove = sp;
        }
    }
    if (auto* lt = go->GetComponent<Lifetime>()) {
        if (CompHeader("Lifetime", lt, &toRemove)) {
            if (ImGui::DragFloat("Seconds##lt", &lt->seconds, 0.05f, 0.0f, 10000.0f)) ed.dirty = true;
            ImGui::TextDisabled("destroys this object after N seconds of play");
            if (ImGui::SmallButton("Remove##lt")) toRemove = lt;
        }
    }
    if (auto* cf = go->GetComponent<CameraFollow>()) {
        if (CompHeader("Camera Follow", cf, &toRemove)) {
            char buf[128];
            std::strncpy(buf, cf->targetName.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            if (ImGui::InputText("Target##cf", buf, sizeof(buf))) { cf->targetName = buf; ed.dirty = true; }
            float off[3] = {cf->offset.x, cf->offset.y, cf->offset.z};
            if (ImGui::DragFloat3("Offset##cf", off, 0.05f)) { cf->offset = {off[0], off[1], off[2]}; ed.dirty = true; }
            if (ImGui::DragFloat("Smoothing##cf", &cf->smoothing, 0.1f, 0.0f, 50.0f)) ed.dirty = true;
            ImGui::TextDisabled("follows the named GameObject (0 = instant snap)");
            if (ImGui::SmallButton("Remove##cf")) toRemove = cf;
        }
    }
    if (auto* tr = go->GetComponent<TextRenderer>()) {
        if (CompHeader("Text", tr, &toRemove)) {
            char buf[512];
            std::strncpy(buf, tr->text.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            // Multi-line box so '\n' / Enter make several lines.
            if (ImGui::InputTextMultiline("Text##txt", buf, sizeof(buf),
                    ImVec2(-1.0f, ImGui::GetTextLineHeight() * 3.0f))) { tr->text = buf; ed.dirty = true; }
            float col[4] = {tr->color.r, tr->color.g, tr->color.b, tr->color.a};
            if (ImGui::ColorEdit4("Color##txt", col)) { tr->color = {col[0], col[1], col[2], col[3]}; ed.dirty = true; }
            if (ImGui::DragFloat("Font Size##txt", &tr->pixelSize, tr->screenSpace ? 0.1f : 0.005f, 0.001f, 100.0f))
                ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(tr->screenSpace ? "Window pixels per font pixel (screen UI: try 2-6)"
                                                                          : "World units per font pixel");
            if (ImGui::Checkbox("Screen Space (UI)##txt", &tr->screenSpace)) ed.dirty = true;
            if (tr->screenSpace) {
                float bs[2] = {tr->size.x, tr->size.y};
                if (ImGui::DragFloat2("Box Size##txt", bs, 1.0f, 1.0f, 4000.0f)) { tr->size = {bs[0], bs[1]}; ed.dirty = true; }
                if (ImGui::SmallButton("Fit to Text##txt")) {
                    tr->size = {(float)tr->PixelWidth() * tr->pixelSize + 8.0f,
                                (float)tr->PixelHeight() * tr->pixelSize + 8.0f};
                    ed.dirty = true;
                }
                float sp[2] = {tr->screenPos.x, tr->screenPos.y};
                if (ImGui::DragFloat2("Offset##txt", sp, 1.0f)) { tr->screenPos = {sp[0], sp[1]}; ed.dirty = true; }
                AnchorCombo("Anchor##txt", tr->anchor, ed);
                const char* aligns[] = {"Left", "Center", "Right"};
                if (ImGui::Combo("Align##txt", &tr->align, aligns, 3)) ed.dirty = true;
                if (ImGui::Checkbox("Vertical Center##txt", &tr->vcenter)) ed.dirty = true;
                if (ImGui::Checkbox("Background##txt", &tr->background)) ed.dirty = true;
                if (tr->background) {
                    float bg[4] = {tr->backgroundColor.r, tr->backgroundColor.g, tr->backgroundColor.b, tr->backgroundColor.a};
                    if (ImGui::ColorEdit4("BG Color##txt", bg)) { tr->backgroundColor = {bg[0], bg[1], bg[2], bg[3]}; ed.dirty = true; }
                }
            }
            if (ImGui::Checkbox("Shadow##txt", &tr->shadow)) ed.dirty = true;
            if (tr->shadow) {
                float scol[4] = {tr->shadowColor.r, tr->shadowColor.g, tr->shadowColor.b, tr->shadowColor.a};
                if (ImGui::ColorEdit4("Shadow Color##txt", scol)) { tr->shadowColor = {scol[0], scol[1], scol[2], scol[3]}; ed.dirty = true; }
                float so[2] = {tr->shadowOffset.x, tr->shadowOffset.y};
                if (ImGui::DragFloat2("Shadow Offset##txt", so, 0.1f)) { tr->shadowOffset = {so[0], so[1]}; ed.dirty = true; }
            }
            if (ImGui::Checkbox("Outline##txt", &tr->outline)) ed.dirty = true;
            if (tr->outline) {
                float ocol[4] = {tr->outlineColor.r, tr->outlineColor.g, tr->outlineColor.b, tr->outlineColor.a};
                if (ImGui::ColorEdit4("Outline Color##txt", ocol)) { tr->outlineColor = {ocol[0], ocol[1], ocol[2], ocol[3]}; ed.dirty = true; }
            }
            if (ImGui::Checkbox("Bold##txt", &tr->bold)) ed.dirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("UPPERCASE##txt", &tr->uppercase)) ed.dirty = true;
            if (tr->screenSpace) { ImGui::SameLine(); if (ImGui::Checkbox("Wrap##txt", &tr->wrap)) ed.dirty = true; }
            if (ImGui::DragFloat("Letter Spacing##txt", &tr->letterSpacing, 0.1f, -4.0f, 32.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Line Spacing##txt", &tr->lineSpacing, 0.1f, -4.0f, 32.0f)) ed.dirty = true;
            ImGui::TextDisabled("8x8 bitmap font; renders in the built game");
            if (ImGui::SmallButton("Remove##txt")) toRemove = tr;
        }
    }
    if (auto* cv = go->GetComponent<Canvas>()) {
        if (CompHeader("Canvas", cv, &toRemove)) {
            const char* modes[] = {"Constant Pixel Size", "Scale With Screen Size"};
            int m = (int)cv->scaleMode;
            if (ImGui::Combo("UI Scale Mode##cv", &m, modes, 2)) { cv->scaleMode = (Canvas::ScaleMode)m; ed.dirty = true; }
            if (cv->scaleMode == Canvas::ScaleMode::ConstantPixelSize) {
                if (ImGui::DragFloat("Scale Factor##cv", &cv->scaleFactor, 0.01f, 0.1f, 10.0f)) ed.dirty = true;
            } else {
                float ref[2] = {cv->referenceResolution.x, cv->referenceResolution.y};
                if (ImGui::DragFloat2("Reference Res##cv", ref, 1.0f, 1.0f, 8000.0f)) { cv->referenceResolution = {ref[0], ref[1]}; ed.dirty = true; }
                if (ImGui::SliderFloat("Match W/H##cv", &cv->matchWidthOrHeight, 0.0f, 1.0f)) ed.dirty = true;
            }
            if (ImGui::DragInt("Sort Order##cv", &cv->sortOrder)) ed.dirty = true;
            ImGui::TextDisabled("scales screen-space UI to the window (Unity CanvasScaler)");
            if (ImGui::SmallButton("Remove##cv")) toRemove = cv;
        }
    }
    if (auto* es = go->GetComponent<EventSystem>()) {
        if (CompHeader("Event System", es, &toRemove)) {
            ImGui::TextDisabled("Routes pointer input to UI widgets.");
            GameObject* h = es->Hovered();
            ImGui::Text("Hovered: %s", h ? h->name.c_str() : "(none)");
            GameObject* s = es->Selected();
            ImGui::Text("Selected: %s", s ? s->name.c_str() : "(none)");
            if (ImGui::SmallButton("Remove##es")) toRemove = es;
        }
    }
    if (auto* nm = go->GetComponent<NetworkManager>()) {
        if (CompHeader("Network Manager", nm, &toRemove)) {
            const char* modes[] = {"None (start via script/Services)", "Host on Play", "Join on Play"};
            int m = (int)nm->autoStart;
            if (ImGui::Combo("Auto Start##nm", &m, modes, 3)) { nm->autoStart = (NetworkManager::AutoStart)m; ed.dirty = true; }
            int port = nm->autoPort;
            ImGui::SetNextItemWidth(120);
            if (ImGui::InputInt("Port##nm", &port)) { nm->autoPort = (std::uint16_t)(port < 0 ? 0 : port); ed.dirty = true; }
            if (nm->autoStart == NetworkManager::AutoStart::Join) {
                static char hostBuf[64]; static NetworkManager* bound = nullptr;
                if (bound != nm) { std::strncpy(hostBuf, nm->autoHost.c_str(), sizeof(hostBuf) - 1); hostBuf[sizeof(hostBuf)-1]='\0'; bound = nm; }
                if (ImGui::InputText("Host IP##nm", hostBuf, sizeof(hostBuf))) { nm->autoHost = hostBuf; ed.dirty = true; }
            }
            static char nameBuf[48]; static NetworkManager* nbound = nullptr;
            if (nbound != nm) { std::strncpy(nameBuf, nm->startName.c_str(), sizeof(nameBuf) - 1); nameBuf[sizeof(nameBuf)-1]='\0'; nbound = nm; }
            if (ImGui::InputText("Player Name##nm", nameBuf, sizeof(nameBuf))) { nm->startName = nameBuf; ed.dirty = true; }
            static char roomBuf[48]; static NetworkManager* rbound = nullptr;
            if (rbound != nm) { std::strncpy(roomBuf, nm->startRoom.c_str(), sizeof(roomBuf) - 1); roomBuf[sizeof(roomBuf)-1]='\0'; rbound = nm; }
            if (ImGui::InputText("Room (lobby)##nm", roomBuf, sizeof(roomBuf))) { nm->startRoom = roomBuf; ed.dirty = true; }
            ImGui::SliderFloat("Smoothing##nm", &nm->interpolationRate, 0.0f, 30.0f, "%.0f /s");

            ImGui::SeparatorText("Host settings");
            static char srvBuf[64]; static NetworkManager* sbound = nullptr;
            if (sbound != nm) { std::strncpy(srvBuf, nm->serverName.c_str(), sizeof(srvBuf)-1); srvBuf[sizeof(srvBuf)-1]='\0'; sbound = nm; }
            if (ImGui::InputText("Server Name##nm", srvBuf, sizeof(srvBuf))) { nm->serverName = srvBuf; ed.dirty = true; }
            static char passBuf[48]; static NetworkManager* pbound = nullptr;
            if (pbound != nm) { std::strncpy(passBuf, nm->password.c_str(), sizeof(passBuf)-1); passBuf[sizeof(passBuf)-1]='\0'; pbound = nm; }
            if (ImGui::InputText("Password##nm", passBuf, sizeof(passBuf), ImGuiInputTextFlags_Password)) { nm->password = passBuf; ed.dirty = true; }
            if (ImGui::SliderInt("Max Players##nm", &nm->maxPlayers, 1, 64)) ed.dirty = true;
            if (ImGui::SliderFloat("Tick Rate##nm", &nm->snapshotRate, 5.0f, 60.0f, "%.0f /s")) ed.dirty = true;

            const char* mode = nm->IsServer() ? "Server" : nm->IsClient() ? "Client" : "Offline";
            ImGui::Text("Live: %s   Peers: %d / %d   Id: %u", mode, (int)nm->PeerCount(), nm->maxPlayers, nm->LocalId());
            if (nm->IsClient()) {
                ImGui::Text("Server: %s   Ping: %.0f ms", nm->ServerName().c_str(), nm->RttMs());
                if (nm->JoinRejected()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.96f,0.45f,0.42f,1)); ImGui::TextUnformatted("Join refused (full / wrong password)"); ImGui::PopStyleColor(); }
            }
            ImGui::TextDisabled("Add this, pick Host/Join, press Play — no code needed.");
            if (ImGui::SmallButton("Remove##nm")) toRemove = nm;
        }
    }
    if (auto* doc = go->GetComponent<UIDocument>()) {
        if (CompHeader("UI Document", doc, &toRemove)) {
            ImGui::TextDisabled("OkayUI markup -> widgets. One widget per line; indent to nest.");
            // Edit the markup in a resizable multiline box. A static buffer big
            // enough for sizeable documents keeps this dependency-free.
            static char buf[1 << 14];
            static UIDocument* s_bound = nullptr;
            if (s_bound != doc) {
                std::strncpy(buf, doc->markup.c_str(), sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                s_bound = doc;
            }
            if (ImGui::InputTextMultiline("##uidoc", buf, sizeof(buf),
                    ImVec2(-1, 180), ImGuiInputTextFlags_AllowTabInput)) {
                doc->markup = buf; ed.dirty = true;
            }
            if (ImGui::Button("Rebuild UI##uidoc")) {
                doc->markup = buf;
                doc->Rebuild();
                ed.scene().Update(0.0f);   // flush the created/destroyed widgets
                ed.dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Insert Example##uidoc")) {
                const char* tmpl =
                    "style card color=30,36,52,235 corner=10 border=1\n"
                    "panel pos=40,40 size=380,280 class=card gradient=12,14,22,235\n"
                    "  text \"SETTINGS\" pos=70,64 size=4 align=left outline=0,0,0,255\n"
                    "  button \"Play\" pos=70,110 size=300,52 corner=8 font=3 tooltip=\"Start the game\" onclick=load_scene(\"game\")\n"
                    "  slider pos=70,180 size=300,16 value=0.7 showvalue=1\n"
                    "  toggle \"Fullscreen\" pos=70,220 size=26,26 on=0\n"
                    "  dropdown pos=70,260 size=200,30 options=Low|Medium|High value=1\n";
                std::strncpy(buf, tmpl, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0';
                doc->markup = buf; doc->Rebuild(); ed.scene().Update(0.0f); ed.dirty = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(%d widgets)", (int)doc->Generated().size());
            ImGui::TextDisabled("types: panel text button image slider toggle progress input dropdown scroll layout");
            ImGui::TextDisabled("keys: name corner border gradient font hover align outline tooltip options bind on*=...");
            // Live validation: list any warnings from the last rebuild in red.
            const auto& diag = doc->Diagnostics();
            if (diag.empty()) {
                ImGui::TextColored(ImVec4(0.5f, 0.85f, 0.5f, 1.0f), "No problems.");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%d problem(s):", (int)diag.size());
                for (const auto& d : diag)
                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "  %s", d.c_str());
            }
            if (ImGui::SmallButton("Remove##uidoc")) toRemove = doc;
        }
    }
    if (auto* btn = go->GetComponent<UIButton>()) {
        if (CompHeader("UI Button", btn, &toRemove)) {
            char lb[128];
            std::strncpy(lb, btn->label.c_str(), sizeof(lb) - 1);
            lb[sizeof(lb) - 1] = '\0';
            if (ImGui::InputText("Label##uib", lb, sizeof(lb))) { btn->label = lb; ed.dirty = true; }
            float pos[2] = {btn->position.x, btn->position.y};
            if (ImGui::DragFloat2("Pos (px)##uib", pos, 1.0f)) { btn->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {btn->size.x, btn->size.y};
            if (ImGui::DragFloat2("Size (px)##uib", sz, 1.0f, 1.0f, 4000.0f)) { btn->size = {sz[0], sz[1]}; ed.dirty = true; }
            float c[4] = {btn->color.r, btn->color.g, btn->color.b, btn->color.a};
            if (ImGui::ColorEdit4("Color##uib", c)) { btn->color = {c[0], c[1], c[2], c[3]}; ed.dirty = true; }
            float hc[4] = {btn->hoverColor.r, btn->hoverColor.g, btn->hoverColor.b, btn->hoverColor.a};
            if (ImGui::ColorEdit4("Hover##uib", hc)) { btn->hoverColor = {hc[0], hc[1], hc[2], hc[3]}; ed.dirty = true; }
            float prc[4] = {btn->pressedColor.r, btn->pressedColor.g, btn->pressedColor.b, btn->pressedColor.a};
            if (ImGui::ColorEdit4("Pressed##uib", prc)) { btn->pressedColor = {prc[0], prc[1], prc[2], prc[3]}; ed.dirty = true; }
            float dc[4] = {btn->disabledColor.r, btn->disabledColor.g, btn->disabledColor.b, btn->disabledColor.a};
            if (ImGui::ColorEdit4("Disabled##uib", dc)) { btn->disabledColor = {dc[0], dc[1], dc[2], dc[3]}; ed.dirty = true; }
            if (ImGui::Checkbox("Interactable##uib", &btn->interactable)) ed.dirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Focusable##uib", &btn->focusable)) ed.dirty = true;
            ImGui::TextDisabled("calls the script's on_click(); disabled buttons are greyed out");
            AnchorCombo("Anchor##uib", btn->anchor, ed);
            ImGui::SeparatorText("Style");
            if (ImGui::DragFloat("Corner Radius##uib", &btn->cornerRadius, 0.2f, 0.0f, 64.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Font Scale##uib", &btn->fontScale, 0.05f, 0.5f, 16.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Border Width##uib", &btn->borderWidth, 0.1f, 0.0f, 16.0f)) ed.dirty = true;
            if (btn->borderWidth > 0.0f) {
                float bc[4] = {btn->borderColor.r, btn->borderColor.g, btn->borderColor.b, btn->borderColor.a};
                if (ImGui::ColorEdit4("Border Color##uib", bc)) { btn->borderColor = {bc[0], bc[1], bc[2], bc[3]}; ed.dirty = true; }
            }
            if (ImGui::DragFloat("Hover Grow##uib", &btn->hoverScale, 0.01f, 1.0f, 2.0f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale when hovered/focused (1 = none)");
            float htc[4] = {btn->hoverTextColor.r, btn->hoverTextColor.g, btn->hoverTextColor.b, btn->hoverTextColor.a};
            if (ImGui::ColorEdit4("Hover Text##uib", htc)) { btn->hoverTextColor = {htc[0], htc[1], htc[2], htc[3]}; ed.dirty = true; }
            if (ImGui::DragFloat("Transition Speed##uib", &btn->transitionSpeed, 0.2f, 0.0f, 30.0f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Smooth color fade on hover/press (0 = instant)");
            if (ImGui::DragFloat("Press Offset##uib", &btn->pressOffset, 0.1f, 0.0f, 16.0f)) ed.dirty = true;
            char ic[256]; std::strncpy(ic, btn->icon.c_str(), sizeof(ic) - 1); ic[sizeof(ic)-1] = '\0';
            if (ImGui::InputText("Icon##uib", ic, sizeof(ic))) { btn->icon = ic; ed.dirty = true; }
            if (ImGui::DragFloat("Icon Size##uib", &btn->iconSize, 0.5f, 0.0f, 256.0f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("PNG/JPG drawn beside the label; 0 = none");
            if (ImGui::Checkbox("Icon on Right##uib", &btn->iconRight)) ed.dirty = true;
            ImGui::SeparatorText("Behavior");
            if (ImGui::Checkbox("Toggle Button##uib", &btn->toggleMode)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stays pressed; each click flips its on/off state");
            if (btn->toggleMode) { ImGui::SameLine(); if (ImGui::Checkbox("On##uib", &btn->isOn)) ed.dirty = true; }
            if (ImGui::Checkbox("Hold to Repeat##uib", &btn->holdRepeat)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fires on_click() repeatedly while held (steppers)");
            if (btn->holdRepeat) {
                if (ImGui::DragFloat("Repeat Delay##uib", &btn->repeatDelay, 0.01f, 0.0f, 2.0f)) ed.dirty = true;
                if (ImGui::DragFloat("Repeat Rate##uib", &btn->repeatInterval, 0.01f, 0.01f, 1.0f)) ed.dirty = true;
            }
            if (ImGui::SmallButton("Remove##uib")) toRemove = btn;
        }
    }
    if (auto* pn = go->GetComponent<UIPanel>()) {
        if (CompHeader("UI Panel", pn, &toRemove)) {
            float pos[2] = {pn->position.x, pn->position.y};
            if (ImGui::DragFloat2("Pos (px)##uip", pos, 1.0f)) { pn->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {pn->size.x, pn->size.y};
            if (ImGui::DragFloat2("Size (px)##uip", sz, 1.0f, 0.0f, 8000.0f)) { pn->size = {sz[0], sz[1]}; ed.dirty = true; }
            float c[4] = {pn->color.r, pn->color.g, pn->color.b, pn->color.a};
            if (ImGui::ColorEdit4("Color##uip", c)) { pn->color = {c[0], c[1], c[2], c[3]}; ed.dirty = true; }
            AnchorCombo("Anchor##uip", pn->anchor, ed);
            ImGui::SeparatorText("Style");
            if (ImGui::DragFloat("Corner Radius##uip", &pn->cornerRadius, 0.2f, 0.0f, 64.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Border Width##uip", &pn->borderWidth, 0.1f, 0.0f, 16.0f)) ed.dirty = true;
            if (pn->borderWidth > 0.0f) {
                float bc[4] = {pn->borderColor.r, pn->borderColor.g, pn->borderColor.b, pn->borderColor.a};
                if (ImGui::ColorEdit4("Border Color##uip", bc)) { pn->borderColor = {bc[0], bc[1], bc[2], bc[3]}; ed.dirty = true; }
            }
            if (ImGui::Checkbox("Gradient##uip", &pn->useGradient)) ed.dirty = true;
            if (pn->useGradient) {
                float gb[4] = {pn->colorBottom.r, pn->colorBottom.g, pn->colorBottom.b, pn->colorBottom.a};
                if (ImGui::ColorEdit4("Bottom Color##uip", gb)) { pn->colorBottom = {gb[0], gb[1], gb[2], gb[3]}; ed.dirty = true; }
                ImGui::TextDisabled("Color is the top; gradient ignores corner rounding.");
            }
            if (ImGui::Checkbox("Drop Shadow##uip", &pn->shadow)) ed.dirty = true;
            if (pn->shadow) {
                float sc[4] = {pn->shadowColor.r, pn->shadowColor.g, pn->shadowColor.b, pn->shadowColor.a};
                if (ImGui::ColorEdit4("Shadow Color##uip", sc)) { pn->shadowColor = {sc[0], sc[1], sc[2], sc[3]}; ed.dirty = true; }
                float so[2] = {pn->shadowOffset.x, pn->shadowOffset.y};
                if (ImGui::DragFloat2("Shadow Offset##uip", so, 0.5f)) { pn->shadowOffset = {so[0], so[1]}; ed.dirty = true; }
            }
            if (ImGui::SmallButton("Remove##uip")) toRemove = pn;
        }
    }
    if (auto* in = go->GetComponent<UIInputField>()) {
        if (CompHeader("UI Input Field", in, &toRemove)) {
            char tb[128]; std::strncpy(tb, in->text.c_str(), sizeof(tb) - 1); tb[sizeof(tb)-1] = '\0';
            if (ImGui::InputText("Text##uif", tb, sizeof(tb))) { in->text = tb; ed.dirty = true; }
            char ph[96]; std::strncpy(ph, in->placeholder.c_str(), sizeof(ph) - 1); ph[sizeof(ph)-1] = '\0';
            if (ImGui::InputText("Placeholder##uif", ph, sizeof(ph))) { in->placeholder = ph; ed.dirty = true; }
            float pos[2] = {in->position.x, in->position.y};
            if (ImGui::DragFloat2("Pos (px)##uif", pos, 1.0f)) { in->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {in->size.x, in->size.y};
            if (ImGui::DragFloat2("Size (px)##uif", sz, 1.0f, 8.0f, 4000.0f)) { in->size = {sz[0], sz[1]}; ed.dirty = true; }
            float c[4] = {in->color.r, in->color.g, in->color.b, in->color.a};
            if (ImGui::ColorEdit4("Color##uif", c)) { in->color = {c[0],c[1],c[2],c[3]}; ed.dirty = true; }
            if (ImGui::DragInt("Max Length##uif", &in->maxLength, 1, 1, 1024)) ed.dirty = true;
            const char* cts[] = {"Standard", "Integer", "Decimal", "Password"};
            int ct = (int)in->contentType;
            if (ImGui::Combo("Content Type##uif", &ct, cts, 4)) { in->contentType = (UIInputField::ContentType)ct; ed.dirty = true; }
            AnchorCombo("Anchor##uif", in->anchor, ed);
            ImGui::TextDisabled("Click to focus + type at runtime; Enter calls on_submit().");
            if (ImGui::SmallButton("Remove##uif")) toRemove = in;
        }
    }
    if (auto* dd = go->GetComponent<UIDropdown>()) {
        if (CompHeader("UI Dropdown", dd, &toRemove)) {
            float pos[2] = {dd->position.x, dd->position.y};
            if (ImGui::DragFloat2("Pos (px)##udd", pos, 1.0f)) { dd->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {dd->size.x, dd->size.y};
            if (ImGui::DragFloat2("Size (px)##udd", sz, 1.0f, 8.0f, 4000.0f)) { dd->size = {sz[0], sz[1]}; ed.dirty = true; }
            AnchorCombo("Anchor##udd", dd->anchor, ed);
            char ph[96]; std::strncpy(ph, dd->placeholder.c_str(), sizeof(ph) - 1); ph[sizeof(ph)-1] = '\0';
            if (ImGui::InputText("Placeholder##udd", ph, sizeof(ph))) { dd->placeholder = ph; ed.dirty = true; }
            if (ImGui::Button("Clear selection##udd")) { dd->value = -1; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("value = -1 shows the placeholder");
            ImGui::SeparatorText("Options");
            int toErase = -1;
            for (int i = 0; i < (int)dd->options.size(); ++i) {
                ImGui::PushID(i);
                char ob[96]; std::strncpy(ob, dd->options[i].c_str(), sizeof(ob) - 1); ob[sizeof(ob)-1] = '\0';
                ImGui::SetNextItemWidth(160);
                if (ImGui::InputText("##opt", ob, sizeof(ob))) { dd->options[i] = ob; ed.dirty = true; }
                ImGui::SameLine();
                bool sel = (dd->value == i);
                if (ImGui::RadioButton("##selopt", sel)) { dd->value = i; ed.dirty = true; }
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) toErase = i;
                ImGui::PopID();
            }
            if (toErase >= 0) {
                dd->options.erase(dd->options.begin() + toErase);
                if (dd->value >= (int)dd->options.size()) dd->value = (int)dd->options.size() - 1;
                if (dd->value < 0) dd->value = 0;
                ed.dirty = true;
            }
            if (ImGui::SmallButton("Add Option##udd")) { dd->options.push_back("Option"); ed.dirty = true; }
            ImGui::SeparatorText("Style");
            float c[4]  = {dd->color.r, dd->color.g, dd->color.b, dd->color.a};
            if (ImGui::ColorEdit4("Header##udd", c)) { dd->color = {c[0],c[1],c[2],c[3]}; ed.dirty = true; }
            float h[4]  = {dd->hoverColor.r, dd->hoverColor.g, dd->hoverColor.b, dd->hoverColor.a};
            if (ImGui::ColorEdit4("Highlight##udd", h)) { dd->hoverColor = {h[0],h[1],h[2],h[3]}; ed.dirty = true; }
            float l[4]  = {dd->listColor.r, dd->listColor.g, dd->listColor.b, dd->listColor.a};
            if (ImGui::ColorEdit4("List BG##udd", l)) { dd->listColor = {l[0],l[1],l[2],l[3]}; ed.dirty = true; }
            float t[4]  = {dd->textColor.r, dd->textColor.g, dd->textColor.b, dd->textColor.a};
            if (ImGui::ColorEdit4("Text##udd", t)) { dd->textColor = {t[0],t[1],t[2],t[3]}; ed.dirty = true; }
            float b[4]  = {dd->borderColor.r, dd->borderColor.g, dd->borderColor.b, dd->borderColor.a};
            if (ImGui::ColorEdit4("Border##udd", b)) { dd->borderColor = {b[0],b[1],b[2],b[3]}; ed.dirty = true; }
            ImGui::TextDisabled("Click to open; picking an option calls on_change().");
            if (ImGui::Checkbox("Interactable##udd", &dd->interactable)) ed.dirty = true;
            if (ImGui::SmallButton("Remove##udd")) toRemove = dd;
        }
    }
    if (auto* tt = go->GetComponent<UITooltip>()) {
        if (CompHeader("UI Tooltip", tt, &toRemove)) {
            char tb[256]; std::strncpy(tb, tt->text.c_str(), sizeof(tb) - 1); tb[sizeof(tb)-1] = '\0';
            if (ImGui::InputText("Text##utt", tb, sizeof(tb))) { tt->text = tb; ed.dirty = true; }
            if (ImGui::DragFloat("Delay (s)##utt", &tt->delay, 0.05f, 0.0f, 5.0f)) ed.dirty = true;
            float bg[4] = {tt->background.r, tt->background.g, tt->background.b, tt->background.a};
            if (ImGui::ColorEdit4("Background##utt", bg)) { tt->background = {bg[0],bg[1],bg[2],bg[3]}; ed.dirty = true; }
            float tc[4] = {tt->textColor.r, tt->textColor.g, tt->textColor.b, tt->textColor.a};
            if (ImGui::ColorEdit4("Text Color##utt", tc)) { tt->textColor = {tc[0],tc[1],tc[2],tc[3]}; ed.dirty = true; }
            ImGui::TextDisabled("Hover the sibling widget to show it (in Game view / built game).");
            if (ImGui::SmallButton("Remove##utt")) toRemove = tt;
        }
    }
    if (auto* dg = go->GetComponent<UIDraggable>()) {
        if (CompHeader("UI Draggable", dg, &toRemove)) {
            if (ImGui::Checkbox("Return to Start##udg", &dg->returnToStart)) ed.dirty = true;
            if (ImGui::Checkbox("Any widget is a target##udg", &dg->anyTarget)) ed.dirty = true;
            if (ImGui::Checkbox("Snap into slot##udg", &dg->snapToSlot)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Centers the item in the drop target — instant inventory");
            const char* axes[] = {"Both", "Horizontal", "Vertical"};
            int ax = (int)dg->axis;
            if (ImGui::Combo("Lock Axis##udg", &ax, axes, 3)) { dg->axis = (UIDraggable::Axis)ax; ed.dirty = true; }
            if (ImGui::DragFloat("Drag Threshold##udg", &dg->dragThreshold, 0.5f, 0.0f, 100.0f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pixels before a drag starts (0 = instant)");
            if (ImGui::Checkbox("Bring to Front while dragging##udg", &dg->bringToFront)) ed.dirty = true;
            ImGui::TextDisabled("Drop onto a UI Drop Target fires on_drop() here +");
            ImGui::TextDisabled("on_receive() on the target.");
            if (ImGui::SmallButton("Remove##udg")) toRemove = dg;
        }
    }
    if (auto* dt = go->GetComponent<UIDropTarget>()) {
        if (CompHeader("UI Drop Target", dt, &toRemove)) {
            char tb[64]; std::strncpy(tb, dt->acceptTag.c_str(), sizeof(tb) - 1); tb[sizeof(tb)-1] = '\0';
            if (ImGui::InputText("Accept Tag##udt", tb, sizeof(tb))) { dt->acceptTag = tb; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only accept draggables whose object Tag matches (empty = any)");
            if (ImGui::Checkbox("Highlight on hover##udt", &dt->showHighlight)) ed.dirty = true;
            float hl[4] = {dt->highlight.r, dt->highlight.g, dt->highlight.b, dt->highlight.a};
            if (ImGui::ColorEdit4("Highlight##udt", hl)) { dt->highlight = {hl[0],hl[1],hl[2],hl[3]}; ed.dirty = true; }
            ImGui::TextDisabled("Draggables dropped here call this object's on_receive().");
            if (ImGui::SmallButton("Remove##udt")) toRemove = dt;
        }
    }
    if (auto* dg = go->GetComponent<Draggable>()) {
        if (CompHeader("Draggable (item)", dg, &toRemove)) {
            if (ImGui::Checkbox("Return to Start##dg", &dg->returnToStart)) ed.dirty = true;
            if (ImGui::Checkbox("Any sprite is a target##dg", &dg->anyTarget)) ed.dirty = true;
            if (ImGui::Checkbox("Snap onto zone##dg", &dg->snapToZone)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Centers the item on the drop zone — instant world-space slots");
            const char* axes[] = {"Both", "Horizontal", "Vertical"};
            int ax = (int)dg->axis;
            if (ImGui::Combo("Lock Axis##dg", &ax, axes, 3)) { dg->axis = (Draggable::Axis)ax; ed.dirty = true; }
            if (ImGui::DragFloat("Drag Threshold##dg", &dg->dragThreshold, 0.01f, 0.0f, 10.0f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("World units before a drag starts (0 = instant)");
            if (ImGui::Checkbox("Bring to Front while dragging##dg", &dg->bringToFront)) ed.dirty = true;
            float grid[2] = {dg->gridX, dg->gridY};
            if (ImGui::DragFloat2("Grid Snap##dg", grid, 0.05f, 0.0f, 100.0f)) { dg->gridX = grid[0]; dg->gridY = grid[1]; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap drag to a world grid (0 = off) — board/tile games");
            if (ImGui::DragFloat("Drag Scale##dg", &dg->dragScale, 0.01f, 0.1f, 4.0f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale the item while dragging for a lifted look (1 = off)");
            ImGui::TextDisabled("Drag the sprite at runtime; dropping on a Drop Zone");
            ImGui::TextDisabled("fires on_drop() here + on_receive() on the zone.");
            ImGui::TextDisabled("Zones also get on_hover_enter/exit() during a drag.");
            if (ImGui::SmallButton("Remove##dg")) toRemove = dg;
        }
    }
    if (auto* dz = go->GetComponent<DropZone>()) {
        if (CompHeader("Drop Zone (item)", dz, &toRemove)) {
            char tb[64]; std::strncpy(tb, dz->acceptTag.c_str(), sizeof(tb) - 1); tb[sizeof(tb)-1] = '\0';
            if (ImGui::InputText("Accept Tag##dz", tb, sizeof(tb))) { dz->acceptTag = tb; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Only accept draggables whose object Tag matches (empty = any)");
            ImGui::TextDisabled("Sprites dropped here call this object's on_receive().");
            if (ImGui::SmallButton("Remove##dz")) toRemove = dz;
        }
    }
    if (auto* lg = go->GetComponent<UILayoutGroup>()) {
        if (CompHeader("UI Layout Group", lg, &toRemove)) {
            const char* dirs[] = {"Vertical", "Horizontal"};
            int d = (int)lg->direction;
            if (ImGui::Combo("Direction##lg", &d, dirs, 2)) { lg->direction = (UILayoutGroup::Direction)d; lg->Arrange(); ed.dirty = true; }
            float orig[2] = {lg->origin.x, lg->origin.y};
            if (ImGui::DragFloat2("Origin##lg", orig, 1.0f)) { lg->origin = {orig[0], orig[1]}; lg->Arrange(); ed.dirty = true; }
            if (ImGui::DragFloat("Spacing##lg", &lg->spacing, 0.5f, 0.0f, 400.0f)) { lg->Arrange(); ed.dirty = true; }
            if (ImGui::DragFloat("Padding##lg", &lg->padding, 0.5f, 0.0f, 400.0f)) { lg->Arrange(); ed.dirty = true; }
            AnchorCombo("Anchor##lg", lg->anchor, ed);
            if (ImGui::SmallButton("Arrange Now##lg")) { lg->Arrange(); ed.dirty = true; }
            ImGui::SameLine();
            ImGui::TextDisabled("content: %.0f px", lg->ContentSize());
            if (ImGui::SmallButton("Remove##lg")) toRemove = lg;
        }
    }
    if (auto* sv = go->GetComponent<UIScrollView>()) {
        if (CompHeader("UI Scroll View", sv, &toRemove)) {
            float pos[2] = {sv->position.x, sv->position.y};
            if (ImGui::DragFloat2("Pos (px)##usv", pos, 1.0f)) { sv->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {sv->size.x, sv->size.y};
            if (ImGui::DragFloat2("Viewport (px)##usv", sz, 1.0f, 16.0f, 8000.0f)) { sv->size = {sz[0], sz[1]}; ed.dirty = true; }
            if (ImGui::DragFloat("Content Height##usv", &sv->contentHeight, 1.0f, 0.0f, 100000.0f)) ed.dirty = true;
            if (ImGui::SliderFloat("Scroll##usv", &sv->scroll, 0.0f, sv->ScrollMax())) ed.dirty = true;
            float c[4] = {sv->background.r, sv->background.g, sv->background.b, sv->background.a};
            if (ImGui::ColorEdit4("Background##usv", c)) { sv->background = {c[0], c[1], c[2], c[3]}; ed.dirty = true; }
            AnchorCombo("Anchor##usv", sv->anchor, ed);
            ImGui::TextDisabled("Parent UI widgets to this; the wheel scrolls them.");
            if (ImGui::SmallButton("Remove##usv")) toRemove = sv;
        }
    }
    if (auto* pb = go->GetComponent<UIProgressBar>()) {
        if (CompHeader("UI Progress Bar", pb, &toRemove)) {
            float pos[2] = {pb->position.x, pb->position.y};
            if (ImGui::DragFloat2("Pos (px)##upb", pos, 1.0f)) { pb->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {pb->size.x, pb->size.y};
            if (ImGui::DragFloat2("Size (px)##upb", sz, 1.0f, 1.0f, 8000.0f)) { pb->size = {sz[0], sz[1]}; ed.dirty = true; }
            if (ImGui::SliderFloat("Value##upb", &pb->value, 0.0f, 1.0f)) ed.dirty = true;
            float fc[4] = {pb->fill.r, pb->fill.g, pb->fill.b, pb->fill.a};
            if (ImGui::ColorEdit4("Fill##upb", fc)) { pb->fill = {fc[0], fc[1], fc[2], fc[3]}; ed.dirty = true; }
            float bc[4] = {pb->background.r, pb->background.g, pb->background.b, pb->background.a};
            if (ImGui::ColorEdit4("Background##upb", bc)) { pb->background = {bc[0], bc[1], bc[2], bc[3]}; ed.dirty = true; }
            ImGui::TextDisabled("script: set_progress(0..1)");
            AnchorCombo("Anchor##upb", pb->anchor, ed);
            if (ImGui::DragFloat("Corner Radius##upb", &pb->cornerRadius, 0.2f, 0.0f, 64.0f)) ed.dirty = true;
            if (ImGui::Checkbox("Show Percent##upb", &pb->showPercent)) ed.dirty = true;
            if (pb->showPercent) {
                float tc[4] = {pb->textColor.r, pb->textColor.g, pb->textColor.b, pb->textColor.a};
                if (ImGui::ColorEdit4("Text Color##upb", tc)) { pb->textColor = {tc[0], tc[1], tc[2], tc[3]}; ed.dirty = true; }
            }
            const char* fds[] = {"Left -> Right", "Right -> Left", "Bottom -> Top", "Top -> Bottom"};
            int fd = (int)pb->fillDir;
            if (ImGui::Combo("Fill Dir##upb", &fd, fds, 4)) { pb->fillDir = (UIProgressBar::FillDir)fd; ed.dirty = true; }
            if (ImGui::SmallButton("Remove##upb")) toRemove = pb;
        }
    }
    if (auto* im = go->GetComponent<UIImage>()) {
        if (CompHeader("UI Image", im, &toRemove)) {
            float pos[2] = {im->position.x, im->position.y};
            if (ImGui::DragFloat2("Pos (px)##uim", pos, 1.0f)) { im->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {im->size.x, im->size.y};
            if (ImGui::DragFloat2("Size (px)##uim", sz, 1.0f, 1.0f, 8000.0f)) { im->size = {sz[0], sz[1]}; ed.dirty = true; }
            char tx[256];
            std::strncpy(tx, im->texture.c_str(), sizeof(tx) - 1);
            tx[sizeof(tx) - 1] = '\0';
            if (ImGui::InputText("Texture##uim", tx, sizeof(tx))) { im->texture = tx; ed.dirty = true; }
            float c[4] = {im->color.r, im->color.g, im->color.b, im->color.a};
            if (ImGui::ColorEdit4("Tint##uim", c)) { im->color = {c[0], c[1], c[2], c[3]}; ed.dirty = true; }
            ImGui::TextDisabled("image path (PNG/JPG); empty = colored rect");
            if (ImGui::Checkbox("Nine-slice##uim", &im->nineSlice)) ed.dirty = true;
            if (im->nineSlice) {
                if (ImGui::DragFloat("Border (px)##uim", &im->border, 0.5f, 0.0f, 512.0f)) ed.dirty = true;
                ImGui::TextDisabled("corners stay fixed; edges/center stretch to size");
            }
            AnchorCombo("Anchor##uim", im->anchor, ed);
            ImGui::SeparatorText("Fill & shape");
            const char* fills[] = {"None", "Left", "Right", "Up", "Down"};
            int fm = (int)im->fillMode;
            if (ImGui::Combo("Fill Mode##uim", &fm, fills, 5)) { im->fillMode = (UIImage::FillMode)fm; ed.dirty = true; }
            if (im->fillMode != UIImage::FillMode::None) {
                if (ImGui::SliderFloat("Fill Amount##uim", &im->fillAmount, 0.0f, 1.0f)) ed.dirty = true;
                ImGui::TextDisabled("for cooldowns / health bars; drive with ui_set_progress-style scripts");
            }
            if (ImGui::DragFloat("Corner Radius##uim", &im->cornerRadius, 0.2f, 0.0f, 64.0f)) ed.dirty = true;
            if (ImGui::SmallButton("Remove##uim")) toRemove = im;
        }
    }
    if (auto* sl = go->GetComponent<UISlider>()) {
        if (CompHeader("UI Slider", sl, &toRemove)) {
            float pos[2] = {sl->position.x, sl->position.y};
            if (ImGui::DragFloat2("Pos (px)##usl", pos, 1.0f)) { sl->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {sl->size.x, sl->size.y};
            if (ImGui::DragFloat2("Size (px)##usl", sz, 1.0f, 1.0f, 8000.0f)) { sl->size = {sz[0], sz[1]}; ed.dirty = true; }
            if (ImGui::DragFloat("Min##usl", &sl->minValue, 0.1f)) ed.dirty = true;
            if (ImGui::DragFloat("Max##usl", &sl->maxValue, 0.1f)) ed.dirty = true;
            if (ImGui::SliderFloat("Value##usl", &sl->value, sl->minValue, sl->maxValue)) ed.dirty = true;
            float fc[4] = {sl->fill.r, sl->fill.g, sl->fill.b, sl->fill.a};
            if (ImGui::ColorEdit4("Fill##usl", fc)) { sl->fill = {fc[0], fc[1], fc[2], fc[3]}; ed.dirty = true; }
            float kc[4] = {sl->knob.r, sl->knob.g, sl->knob.b, sl->knob.a};
            if (ImGui::ColorEdit4("Knob##usl", kc)) { sl->knob = {kc[0], kc[1], kc[2], kc[3]}; ed.dirty = true; }
            ImGui::TextDisabled("drag in the built game; calls script on_change()");
            AnchorCombo("Anchor##usl", sl->anchor, ed);
            if (ImGui::DragFloat("Corner Radius##usl", &sl->cornerRadius, 0.2f, 0.0f, 64.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Knob Size##usl", &sl->knobSize, 0.02f, 0.1f, 3.0f)) ed.dirty = true;
            if (ImGui::Checkbox("Show Value##usl", &sl->showValue)) ed.dirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Whole Numbers##usl", &sl->wholeNumbers)) {
                if (sl->wholeNumbers) sl->value = Mathf::Round(sl->value);
                ed.dirty = true;
            }
            if (sl->showValue) {
                float tc[4] = {sl->textColor.r, sl->textColor.g, sl->textColor.b, sl->textColor.a};
                if (ImGui::ColorEdit4("Text Color##usl", tc)) { sl->textColor = {tc[0], tc[1], tc[2], tc[3]}; ed.dirty = true; }
            }
            if (ImGui::Checkbox("Interactable##usl", &sl->interactable)) ed.dirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Vertical##usl", &sl->vertical)) ed.dirty = true;
            if (ImGui::SmallButton("Remove##usl")) toRemove = sl;
        }
    }
    if (auto* tg = go->GetComponent<UIToggle>()) {
        if (CompHeader("UI Toggle", tg, &toRemove)) {
            char lb[128];
            std::strncpy(lb, tg->label.c_str(), sizeof(lb) - 1);
            lb[sizeof(lb) - 1] = '\0';
            if (ImGui::InputText("Label##utg", lb, sizeof(lb))) { tg->label = lb; ed.dirty = true; }
            if (ImGui::Checkbox("On##utg", &tg->on)) ed.dirty = true;
            float pos[2] = {tg->position.x, tg->position.y};
            if (ImGui::DragFloat2("Pos (px)##utg", pos, 1.0f)) { tg->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {tg->size.x, tg->size.y};
            if (ImGui::DragFloat2("Size (px)##utg", sz, 1.0f, 1.0f, 4000.0f)) { tg->size = {sz[0], sz[1]}; ed.dirty = true; }
            float cc[4] = {tg->checkColor.r, tg->checkColor.g, tg->checkColor.b, tg->checkColor.a};
            if (ImGui::ColorEdit4("Check##utg", cc)) { tg->checkColor = {cc[0], cc[1], cc[2], cc[3]}; ed.dirty = true; }
            ImGui::TextDisabled("click in the built game; calls script on_toggle()");
            AnchorCombo("Anchor##utg", tg->anchor, ed);
            if (ImGui::DragFloat("Corner Radius##utg", &tg->cornerRadius, 0.2f, 0.0f, 64.0f)) ed.dirty = true;
            const char* styles[] = {"Checkbox", "Switch"};
            int st = (int)tg->style;
            if (ImGui::Combo("Style##utg", &st, styles, 2)) { tg->style = (UIToggle::Style)st; ed.dirty = true; }
            if (tg->style == UIToggle::Style::Switch) {
                float kc[4] = {tg->knobColor.r, tg->knobColor.g, tg->knobColor.b, tg->knobColor.a};
                if (ImGui::ColorEdit4("Knob##utg", kc)) { tg->knobColor = {kc[0], kc[1], kc[2], kc[3]}; ed.dirty = true; }
            }
            if (ImGui::Checkbox("Interactable##utg", &tg->interactable)) ed.dirty = true;
            if (ImGui::SmallButton("Remove##utg")) toRemove = tg;
        }
    }
    if (auto* an = go->GetComponent<SpriteAnimator>()) {
        if (CompHeader("Sprite Animator", an, &toRemove)) {
            if (ImGui::DragFloat("FPS##anim", &an->fps, 0.25f, 0.0f, 120.0f)) ed.dirty = true;
            if (ImGui::Checkbox("Loop##anim", &an->loop)) ed.dirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Playing##anim", &an->playing)) ed.dirty = true;
            ImGui::Spacing();
            ImGui::TextDisabled("Atlas mode (sprite sheet on the SpriteRenderer texture):");
            if (ImGui::DragInt("Columns##anim", &an->atlasColumns, 0.1f, 0, 64)) ed.dirty = true;
            if (ImGui::DragInt("Rows##anim", &an->atlasRows, 0.1f, 1, 64)) ed.dirty = true;
            if (ImGui::DragInt("Count 0=all##anim", &an->atlasCount, 0.1f, 0, 4096)) ed.dirty = true;
            if (an->atlasColumns > 0) ImGui::TextDisabled("Using atlas cells; frame list ignored.");
            ImGui::Spacing();
            ImGui::TextDisabled("Frames (image paths, used when Columns = 0):");
            int removeAt = -1;
            for (std::size_t i = 0; i < an->frames.size(); ++i) {
                char fb[256];
                std::strncpy(fb, an->frames[i].c_str(), sizeof(fb) - 1);
                fb[sizeof(fb) - 1] = '\0';
                ImGui::PushID((int)i);
                if (ImGui::InputText("##frame", fb, sizeof(fb))) { an->frames[i] = fb; ed.dirty = true; }
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) removeAt = (int)i;
                ImGui::PopID();
            }
            if (removeAt >= 0) { an->frames.erase(an->frames.begin() + removeAt); ed.dirty = true; }
            if (ImGui::SmallButton("Add Frame")) { an->frames.push_back("frame.png"); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove##anim")) toRemove = an;
        }
    }

    // Apply a queued component removal (undoable).
    if (toRemove) { ed.PushUndo(); go->RemoveComponent(toRemove); ed.dirty = true; }

    ImGui::Spacing();
    ImGui::Separator();
    static char acFilter[64] = "";
    if (ImGui::Button("Add Component", ImVec2(-1, 0))) { acFilter[0] = '\0'; ImGui::OpenPopup("AddComponent"); }
    if (ImGui::BeginPopup("AddComponent")) {
        // Centered "Component" title + search box, like Unity's Add Component.
        const char* title = "Component";
        float availW = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - ImGui::CalcTextSize(title).x) * 0.5f);
        ImGui::TextDisabled("%s", title);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##acfilter", "Search", acFilter, sizeof(acFilter));
        ImGui::Separator();

        bool searching = acFilter[0] != '\0';
        // Case-insensitive substring match against the search box.
        auto F = [&](const char* name) {
            if (!searching) return true;
            std::string a = name, b = acFilter;
            for (auto& ch : a) ch = (char)std::tolower((unsigned char)ch);
            for (auto& ch : b) ch = (char)std::tolower((unsigned char)ch);
            return a.find(b) != std::string::npos;
        };
        // A category drills into a submenu when browsing (Unity-style), but its
        // items render inline in a flat list while searching.
        auto BeginCat = [&](const char* name) -> bool {
            return searching ? true : ImGui::BeginMenu(name);
        };
        auto EndCat = [&](bool opened) { if (!searching && opened) ImGui::EndMenu(); };
        // One component row: shown only if absent + matches the search.
        auto item = [&](bool absent, const char* label) {
            return absent && F(label) && ImGui::MenuItem(label);
        };

        { bool o = BeginCat("Rendering");
          if (o) {
            if (item(!go->GetComponent<SpriteRenderer>(), "Sprite Renderer")) { go->AddComponent<SpriteRenderer>(); ed.dirty = true; }
            if (item(!go->GetComponent<MeshRenderer>(), "Mesh Renderer (3D)")) { go->AddComponent<MeshRenderer>(); ed.view3D = true; ed.dirty = true; }
            if (item(!go->GetComponent<TextRenderer>(), "Text")) { go->AddComponent<TextRenderer>(); ed.dirty = true; }
            if (item(!go->GetComponent<SpriteAnimator>(), "Sprite Animator")) { go->AddComponent<SpriteAnimator>(); ed.dirty = true; }
            if (item(!go->GetComponent<ParticleSystem>(), "Particle System")) { go->AddComponent<ParticleSystem>(); ed.dirty = true; }
            if (item(!go->GetComponent<Draggable>(), "Draggable (item)")) { go->AddComponent<Draggable>(); ed.dirty = true; }
            if (item(!go->GetComponent<DropZone>(), "Drop Zone (item)")) { go->AddComponent<DropZone>(); ed.dirty = true; }
          } EndCat(o); }

        { bool o = BeginCat("Physics 2D");
          if (o) {
            if (item(!go->GetComponent<Rigidbody2D>(), "Rigidbody2D")) { go->AddComponent<Rigidbody2D>(); ed.dirty = true; }
            if (item(!go->GetComponent<BoxCollider2D>(), "Box Collider 2D")) { go->AddComponent<BoxCollider2D>(); ed.dirty = true; }
            if (item(!go->GetComponent<CircleCollider2D>(), "Circle Collider 2D")) { go->AddComponent<CircleCollider2D>(); ed.dirty = true; }
            if (item(!go->GetComponent<CapsuleCollider2D>(), "Capsule Collider 2D")) { go->AddComponent<CapsuleCollider2D>(); ed.dirty = true; }
            if (item(go->GetComponent<Tilemap>() && !go->GetComponent<TilemapCollider2D>(), "Tilemap Collider 2D")) { go->AddComponent<TilemapCollider2D>(); ed.dirty = true; }
          } EndCat(o); }

        { bool o = BeginCat("Physics 3D");
          if (o) {
            if (item(!go->GetComponent<Rigidbody3D>(), "Rigidbody3D")) { go->AddComponent<Rigidbody3D>(); ed.dirty = true; }
            if (item(!go->GetComponent<BoxCollider3D>(), "Box Collider 3D")) { go->AddComponent<BoxCollider3D>(); ed.dirty = true; }
            if (item(!go->GetComponent<SphereCollider3D>(), "Sphere Collider 3D")) { go->AddComponent<SphereCollider3D>(); ed.dirty = true; }
            if (item(!go->GetComponent<CapsuleCollider3D>(), "Capsule Collider 3D")) { go->AddComponent<CapsuleCollider3D>(); ed.dirty = true; }
          } EndCat(o); }

        { bool o = BeginCat("Lighting");
          if (o) {
            if (item(!go->GetComponent<Light>(), "Directional Light")) { go->AddComponent<Light>(); ed.dirty = true; }
          } EndCat(o); }

        { bool o = BeginCat("Camera");
          if (o) {
            if (item(!go->GetComponent<Camera>(), "Camera")) { go->AddComponent<Camera>(); ed.dirty = true; }
            if (item(!go->GetComponent<CameraFollow>(), "Camera Follow")) { go->AddComponent<CameraFollow>(); ed.dirty = true; }
          } EndCat(o); }

        { bool o = BeginCat("Scripts");
          if (o) {
            // Existing .okay scripts in the project show up directly here, so one
            // click attaches them (Unity-style). "New Script..." makes a new file.
            if (!go->GetComponent<ScriptComponent>()) {
                namespace fs = std::filesystem;
                fs::path assets = ed.projectDir().empty() ? fs::path("Assets")
                                                          : fs::path(ed.projectDir()) / "Assets";
                std::error_code ec;
                if (fs::exists(assets, ec)) {
                    for (auto& e : fs::recursive_directory_iterator(assets, ec)) {
                        if (!e.is_regular_file()) continue;
                        std::string ext = e.path().extension().string();
                        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
                        if (ext != ".okay") continue;
                        std::string rel = fs::relative(e.path(), assets, ec).string();
                        if (F(rel.c_str()) && ImGui::MenuItem(rel.c_str())) {
                            auto* sc = go->AddComponent<ScriptComponent>("okayscript");
                            std::string err; sc->LoadFile(e.path().string(), &err);
                            sc->SetPath(e.path().string());
                            ConsoleLog("Attached script " + e.path().string());
                            ed.dirty = true;
                        }
                    }
                }
                if (F("New Script...") && ImGui::MenuItem("New Script...")) {
                    g_newScriptGO = go; g_newScriptOpen = true;
                    std::snprintf(g_newScriptName, sizeof(g_newScriptName), "%sScript", go->name.c_str());
                }
            }
            if (item(!go->GetComponent<ActionList>(), "Actions (Visual Script)")) { go->AddComponent<ActionList>(); ed.dirty = true; }
          } EndCat(o); }

        { bool o = BeginCat("Audio");
          if (o) {
            if (item(!go->GetComponent<AudioSource>(), "Audio Source")) { go->AddComponent<AudioSource>()->clip = AudioClip::Sine(440.0f, 0.3f); ed.dirty = true; }
          } EndCat(o); }

        { bool o = BeginCat("Gameplay");
          if (o) {
            if (item(!go->GetComponent<CharacterController2D>(), "Character Controller 2D")) { go->AddComponent<CharacterController2D>(); ed.dirty = true; }
            if (item(!go->GetComponent<CharacterController3D>(), "Character Controller 3D")) { go->AddComponent<CharacterController3D>(); ed.dirty = true; }
            if (item(!go->GetComponent<FollowTarget2D>(), "Follow Target 2D")) { go->AddComponent<FollowTarget2D>(); ed.dirty = true; }
            if (item(!go->GetComponent<Mover>(), "Mover")) { go->AddComponent<Mover>(); ed.dirty = true; }
            if (item(!go->GetComponent<Spinner>(), "Spinner")) { go->AddComponent<Spinner>(); ed.dirty = true; }
            if (item(!go->GetComponent<Lifetime>(), "Lifetime")) { go->AddComponent<Lifetime>(); ed.dirty = true; }
          } EndCat(o); }

        { bool o = BeginCat("UI");
          if (o) {
            if (item(!go->GetComponent<Canvas>(), "Canvas")) { go->AddComponent<Canvas>(); ed.dirty = true; }
            if (item(!go->GetComponent<EventSystem>(), "Event System")) { go->AddComponent<EventSystem>(); ed.dirty = true; }
            if (item(!go->GetComponent<UIDocument>(), "UI Document")) { go->AddComponent<UIDocument>(); ed.dirty = true; }
            if (item(!go->GetComponent<NetworkManager>(), "Network Manager")) { go->AddComponent<NetworkManager>(); ed.dirty = true; }
            if (item(!go->GetComponent<UIButton>(), "UI Button")) { go->AddComponent<UIButton>(); ed.dirty = true; }
            if (item(!go->GetComponent<UIPanel>(), "UI Panel")) { go->AddComponent<UIPanel>(); ed.dirty = true; }
            if (item(!go->GetComponent<UIImage>(), "UI Image")) { go->AddComponent<UIImage>(); ed.dirty = true; }
            if (item(!go->GetComponent<UIProgressBar>(), "UI Progress Bar")) { go->AddComponent<UIProgressBar>(); ed.dirty = true; }
            if (item(!go->GetComponent<UISlider>(), "UI Slider")) { go->AddComponent<UISlider>(); ed.dirty = true; }
            if (item(!go->GetComponent<UIToggle>(), "UI Toggle")) { go->AddComponent<UIToggle>(); ed.dirty = true; }
            if (item(!go->GetComponent<UIDraggable>(), "UI Draggable")) { go->AddComponent<UIDraggable>(); ed.dirty = true; }
            if (item(!go->GetComponent<UIDropTarget>(), "UI Drop Target")) { go->AddComponent<UIDropTarget>(); ed.dirty = true; }
          } EndCat(o); }

        ImGui::EndPopup();
    }

    // "New Script" naming dialog: create a fresh .okay under Assets and attach it
    // to the target object (Unity's Add Component > New Script > Create and Add).
    if (g_newScriptOpen && !ImGui::IsPopupOpen("New Script")) ImGui::OpenPopup("New Script");
    if (ImGui::BeginPopupModal("New Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("Creates Assets/<name>.okay and attaches it.");
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool enter = ImGui::InputText("Name", g_newScriptName, sizeof(g_newScriptName),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        bool create = ImGui::Button("Create and Add", ImVec2(140, 0)) || enter;
        ImGui::SameLine();
        bool cancel = ImGui::Button("Cancel", ImVec2(100, 0));
        if (create && g_newScriptName[0] && g_newScriptGO) {
            namespace fs = std::filesystem;
            fs::path assets = ed.projectDir().empty() ? fs::path("Assets")
                                                      : fs::path(ed.projectDir()) / "Assets";
            std::error_code se; fs::create_directories(assets, se);
            std::string base = g_newScriptName;
            if (base.size() < 5 || base.substr(base.size() - 5) != ".okay") base += ".okay";
            fs::path p = assets / base;
            if (fs::exists(p, se)) {
                ConsoleLog("Script already exists: " + p.string());
            } else {
                std::ofstream(p) << extide::StarterScript("okayscript");
            }
            auto* nsc = g_newScriptGO->AddComponent<ScriptComponent>("okayscript");
            std::string err; nsc->LoadFile(p.string(), &err); nsc->SetPath(p.string());
            ConsoleLog("Created + attached " + p.string());
            ed.dirty = true;
            g_newScriptOpen = false; g_newScriptGO = nullptr; ImGui::CloseCurrentPopup();
        } else if (cancel) {
            g_newScriptOpen = false; g_newScriptGO = nullptr; ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.18f, 1.0f));
    if (ImGui::Button("Delete GameObject", ImVec2(-1, 0))) ed.DeleteSelected();
    ImGui::PopStyleColor();

    // Whole-Inspector drop target for scripts and materials (images keep going to
    // their own texture fields). Drop a .okay anywhere here to attach a Script.
    if (ImGuiWindow* w = ImGui::GetCurrentWindow()) {
        if (ImGui::BeginDragDropTargetCustom(w->InnerClipRect, w->ID)) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                std::string path((const char*)p->Data), ext;
                if (auto d = path.find_last_of('.'); d != std::string::npos) ext = path.substr(d);
                for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
                if (ext == ".okay") {
                    ed.PushUndo();
                    auto* nsc = go->AddComponent<ScriptComponent>("okayscript");
                    std::string err; nsc->LoadFile(path, &err); nsc->SetPath(path);
                    ConsoleLog("Attached script " + path); ed.dirty = true;
                } else if (ext == ".okaymat") {
                    if (auto* mr = go->GetComponent<MeshRenderer>()) {
                        Material m; if (Material::LoadFromFile(path, m)) {
                            ed.PushUndo(); m.ApplyTo(*mr); ed.dirty = true; ConsoleLog("Applied material");
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::PopStyleVar(3);
    ImGui::End();
}

// Draw a string with the engine's 8x8 bitmap font into an ImGui draw list, so
// the editor viewport shows the same text the built game will (HUDs, labels).
void DrawBitmapText(ImDrawList* dl, const std::string& text, float ox, float oy,
                    float px, ImU32 col, float letterSp = 0.0f, float lineSp = 0.0f) {
    if (px < 1.0f) px = 1.0f;
    float cx = ox;
    for (char ch : text) {
        if (ch == '\n') { oy += (Font8x8::Height + 1 + lineSp) * px; cx = ox; continue; }
        for (int y = 0; y < Font8x8::Height; ++y)
            for (int x = 0; x < Font8x8::Width; ++x)
                if (Font8x8::Pixel(ch, x, y))
                    dl->AddRectFilled(ImVec2(cx + x * px, oy + y * px),
                                      ImVec2(cx + (x + 1) * px, oy + (y + 1) * px), col);
        cx += (Font8x8::Width + 1 + letterSp) * px;
    }
}

// The 8 resize handles of a screen rect, in order:
// 0=TL 1=T(op) 2=TR 3=R(ight) 4=BR 5=B(ottom) 6=BL 7=L(eft).
static void UIHandlePositions(ImVec2 a, ImVec2 b, ImVec2 out[8]) {
    float mx = (a.x + b.x) * 0.5f, my = (a.y + b.y) * 0.5f;
    out[0] = ImVec2(a.x, a.y); out[1] = ImVec2(mx, a.y); out[2] = ImVec2(b.x, a.y);
    out[3] = ImVec2(b.x, my);  out[4] = ImVec2(b.x, b.y); out[5] = ImVec2(mx, b.y);
    out[6] = ImVec2(a.x, b.y); out[7] = ImVec2(a.x, my);
}

// The camera to frame the Game view through: the scene's main camera if one is
// active (set on Play), else the first Camera component found (edit mode).
static Camera* SceneCamera(Scene& s) {
    if (s.mainCamera) return s.mainCamera;
    for (const auto& go : s.Objects())
        if (go->active)
            if (auto* c = go->GetComponent<Camera>()) return c;
    return nullptr;
}

// Screen-space UI overlay (text, images, panels, bars, sliders, toggles,
// buttons). UI objects anchor to the canvas via UICanvas, so this is identical
// in the 2D Scene view, the 3D Scene view and the Game view — calling it from
// all three is what makes created UI actually appear (it used to draw only in
// the 2D scene, so UI added to a 3D project was invisible).
void DrawUIOverlay(EditorState& ed, ImDrawList* dl, ImVec2 canvasPos,
                   ImVec2 canvasSize, bool gameView) {
    const auto& objs = ed.scene().Objects();

    // Publish the canvas size so anchored widgets resolve and (in Play) hit-test
    // against the same dimensions the preview uses.
    UICanvas::Set(canvasSize.x, canvasSize.y);

    // Each widget's pixels are scaled by its owning Canvas (Unity's CanvasScaler):
    // ScaleWithScreenSize grows/shrinks the whole HUD with the window. Widgets not
    // parented to a Canvas fall back to scale 1.
    auto uiScale = [&](GameObject* go) { return UIScaleFor(go, canvasSize.x, canvasSize.y); };

    // Hide a scroll-view child when it falls outside the viewport (cheap clip).
    auto svCull = [&](GameObject* go, const Vec2& o, const Vec2& sz) -> bool {
        UIScrollView* sv = OwningScrollView(go);
        if (!sv) return false;
        float s = uiScale(go);
        Vec2 vo = ResolveAnchor(sv->anchor, sv->position * s, sv->size * s, canvasSize.x, canvasSize.y);
        return (o.y + sz.y < vo.y) || (o.y > vo.y + sv->size.y * s);
    };

    // Scroll View backgrounds (drawn behind their content) + a scrollbar.
    for (const auto& up : objs) {
        auto* sv = up->GetComponent<UIScrollView>();
        if (!sv || !up->active) continue;
        float s = uiScale(up.get());
        Vec2 o = ResolveAnchor(sv->anchor, sv->position * s, sv->size * s, canvasSize.x, canvasSize.y);
        ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
        ImVec2 b(a.x + sv->size.x * s, a.y + sv->size.y * s);
        dl->AddRectFilled(a, b, ToColor(sv->background), 4.0f);
        // Scrollbar track + thumb on the right edge.
        if (sv->ScrollMax() > 0.0f) {
            float barW = 6.0f * s;
            ImVec2 ta(b.x - barW - 2, a.y + 2), tb(b.x - 2, b.y - 2);
            dl->AddRectFilled(ta, tb, IM_COL32(255, 255, 255, 30), 3.0f);
            float viewFrac = (sv->size.y) / sv->contentHeight; if (viewFrac > 1) viewFrac = 1;
            float thumbH = (tb.y - ta.y) * viewFrac;
            float thumbY = ta.y + (tb.y - ta.y - thumbH) * sv->Fraction();
            dl->AddRectFilled(ImVec2(ta.x, thumbY), ImVec2(tb.x, thumbY + thumbH), ToColor(sv->barColor), 3.0f);
        }
    }

    // Screen-space text (world-anchored text stays with the 2D scene draw).
    for (const auto& up : objs) {
        auto* tr = up->GetComponent<TextRenderer>();
        if (!tr || !up->active || !tr->screenSpace) continue;
        float s = uiScale(up.get());
        ImU32 col = ToColor(tr->color);
        ImU32 sh = ToColor(tr->shadowColor);
        Vec2 box = tr->BoxTopLeft(canvasSize.x, canvasSize.y, s);   // box top-left
        Vec2 boxSz{tr->size.x * s, tr->size.y * s};
        if (UIScrollView* sv = OwningScrollView(up.get())) box.y -= sv->scroll * s;
        if (svCull(up.get(), box, boxSz)) continue;
        if (tr->background)
            dl->AddRectFilled(ImVec2(canvasPos.x + box.x, canvasPos.y + box.y),
                              ImVec2(canvasPos.x + box.x + boxSz.x, canvasPos.y + box.y + boxSz.y),
                              ToColor(tr->backgroundColor), 4.0f);
        Vec2 o = tr->ResolvedScreenPos(canvasSize.x, canvasSize.y, s);   // text inside box
        if (UIScrollView* sv = OwningScrollView(up.get())) o.y -= sv->scroll * s;
        float px = tr->pixelSize * s, ls = tr->letterSpacing, lp = tr->lineSpacing;
        std::string disp = tr->DisplayText();
        float bx = canvasPos.x + o.x, by = canvasPos.y + o.y;
        if (tr->shadow)
            DrawBitmapText(dl, disp, bx + tr->shadowOffset.x * px,
                           by + tr->shadowOffset.y * px, px, sh, ls, lp);
        if (tr->outline) {                            // 4-direction outline
            ImU32 oc = ToColor(tr->outlineColor);
            DrawBitmapText(dl, disp, bx - px, by, px, oc, ls, lp);
            DrawBitmapText(dl, disp, bx + px, by, px, oc, ls, lp);
            DrawBitmapText(dl, disp, bx, by - px, px, oc, ls, lp);
            DrawBitmapText(dl, disp, bx, by + px, px, oc, ls, lp);
        }
        DrawBitmapText(dl, disp, bx, by, px, col, ls, lp);
        if (tr->bold) DrawBitmapText(dl, disp, bx + px, by, px, col, ls, lp);  // faux-bold
    }

    // UI images (logos/icons): preview as a tinted rect with the path centered.
    for (const auto& up : objs) {
        auto* im = up->GetComponent<UIImage>();
        if (!im || !up->active) continue;
        Vec2 o, sz; GetUIScreenRect(up.get(), canvasSize.x, canvasSize.y, o, sz);
        if (svCull(up.get(), o, sz)) continue;
        ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
        ImVec2 b(a.x + sz.x, a.y + sz.y);
        float fox, foy, fw, fh;
        im->FilledRect(sz.x, sz.y, fox, foy, fw, fh);
        ImVec2 fa(a.x + fox, a.y + foy), fb(fa.x + fw, fa.y + fh);
        dl->AddRectFilled(fa, fb, ToColor(im->color), im->cornerRadius);
        dl->AddRect(a, b, IM_COL32(255, 255, 255, 90), im->cornerRadius);
        if (!im->texture.empty())
            DrawBitmapText(dl, im->texture, a.x + 4, a.y + 4, 1.0f, IM_COL32(255, 255, 255, 160));
    }

    // UI panels (backgrounds) and progress bars: screen-space, canvas-relative.
    for (const auto& up : objs) {
        auto* pn = up->GetComponent<UIPanel>();
        if (!pn || !up->active) continue;
        Vec2 o, sz; GetUIScreenRect(up.get(), canvasSize.x, canvasSize.y, o, sz);
        if (svCull(up.get(), o, sz)) continue;
        ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
        ImVec2 pb2(a.x + sz.x, a.y + sz.y);
        if (pn->shadow)          // drop shadow behind the panel
            dl->AddRectFilled(ImVec2(a.x + pn->shadowOffset.x, a.y + pn->shadowOffset.y),
                              ImVec2(pb2.x + pn->shadowOffset.x, pb2.y + pn->shadowOffset.y),
                              ToColor(pn->shadowColor), pn->cornerRadius);
        if (pn->useGradient) {   // vertical top->bottom fade (no rounding)
            dl->AddRectFilledMultiColor(a, pb2, ToColor(pn->color), ToColor(pn->color),
                                        ToColor(pn->colorBottom), ToColor(pn->colorBottom));
        } else {
            dl->AddRectFilled(a, pb2, ToColor(pn->color), pn->cornerRadius);
        }
        if (pn->borderWidth > 0.0f)
            dl->AddRect(a, pb2, ToColor(pn->borderColor), pn->cornerRadius, 0, pn->borderWidth);
    }
    for (const auto& up : objs) {
        auto* pb = up->GetComponent<UIProgressBar>();
        if (!pb || !up->active) continue;
        float s = uiScale(up.get());
        Vec2 o, sz; GetUIScreenRect(up.get(), canvasSize.x, canvasSize.y, o, sz);
        if (svCull(up.get(), o, sz)) continue;
        ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
        dl->AddRectFilled(a, ImVec2(a.x + sz.x, a.y + sz.y), ToColor(pb->background), pb->cornerRadius);
        { float fox, foy, fw, fh; pb->FillRect(sz.x, sz.y, fox, foy, fw, fh);
          dl->AddRectFilled(ImVec2(a.x + fox, a.y + foy), ImVec2(a.x + fox + fw, a.y + foy + fh),
                            ToColor(pb->fill), pb->cornerRadius); }
        if (pb->showPercent) {
            char pct[8]; std::snprintf(pct, sizeof(pct), "%d%%", (int)(pb->Fraction() * 100.0f + 0.5f));
            float px = 2.0f * s;
            float tw = std::strlen(pct) * (Font8x8::Width + 1) * px;
            DrawBitmapText(dl, pct, a.x + (sz.x - tw) * 0.5f,
                           a.y + (sz.y - Font8x8::Height * px) * 0.5f, px, ToColor(pb->textColor));
        }
    }

    // UI sliders: track + fill + knob.
    for (const auto& up : objs) {
        auto* sl = up->GetComponent<UISlider>();
        if (!sl || !up->active) continue;
        float s = uiScale(up.get());
        Vec2 o, sz; GetUIScreenRect(up.get(), canvasSize.x, canvasSize.y, o, sz);
        if (svCull(up.get(), o, sz)) continue;
        ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
        dl->AddRectFilled(a, ImVec2(a.x + sz.x, a.y + sz.y), ToColor(sl->background), sl->cornerRadius);
        float f = sl->Fraction();
        if (sl->vertical) {
            dl->AddRectFilled(ImVec2(a.x, a.y + sz.y * (1.0f - f)), ImVec2(a.x + sz.x, a.y + sz.y),
                              ToColor(sl->fill), sl->cornerRadius);
            float ky = a.y + sz.y * (1.0f - f), kh = sz.x * sl->knobSize;
            dl->AddRectFilled(ImVec2(a.x - 2, ky - kh * 0.5f), ImVec2(a.x + sz.x + 2, ky + kh * 0.5f),
                              ToColor(sl->knob), 2.0f);
        } else {
            dl->AddRectFilled(a, ImVec2(a.x + sz.x * f, a.y + sz.y), ToColor(sl->fill), sl->cornerRadius);
            float kx = a.x + sz.x * f, kw = sz.y * sl->knobSize;
            dl->AddRectFilled(ImVec2(kx - kw * 0.5f, a.y - 2), ImVec2(kx + kw * 0.5f, a.y + sz.y + 2),
                              ToColor(sl->knob), 2.0f);
        }
        if (sl->showValue) {
            char vbuf[16]; std::snprintf(vbuf, sizeof(vbuf), "%.2f", sl->value);
            float px = 2.0f * s;
            DrawBitmapText(dl, vbuf, a.x + sz.x + 8 * s,
                           a.y + (sz.y - Font8x8::Height * px) * 0.5f, px, ToColor(sl->textColor));
        }
        if (!sl->interactable) dl->AddRectFilled(a, ImVec2(a.x + sz.x, a.y + sz.y), IM_COL32(30, 30, 35, 150), sl->cornerRadius);
    }
    // UI toggles: box (+ inset check when on) and a label.
    for (const auto& up : objs) {
        auto* tg = up->GetComponent<UIToggle>();
        if (!tg || !up->active) continue;
        float s = uiScale(up.get());
        Vec2 o, sz; GetUIScreenRect(up.get(), canvasSize.x, canvasSize.y, o, sz);
        if (svCull(up.get(), o, sz)) continue;
        ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
        ImVec2 b(a.x + sz.x, a.y + sz.y);
        float labelX = b.x + 8.0f * s;
        if (tg->style == UIToggle::Style::Switch) {
            // Pill track (full when on) + a sliding knob.
            float r = sz.y * 0.5f;
            dl->AddRectFilled(a, b, ToColor(tg->on ? tg->checkColor : tg->boxColor), r);
            float kr = r - 2.0f;
            float kx = tg->on ? (b.x - r) : (a.x + r);
            dl->AddCircleFilled(ImVec2(kx, a.y + r), kr, ToColor(tg->knobColor));
        } else {
            dl->AddRectFilled(a, b, ToColor(tg->boxColor), tg->cornerRadius);
            if (tg->on) {
                float pad = sz.x * 0.22f;
                dl->AddRectFilled(ImVec2(a.x + pad, a.y + pad), ImVec2(b.x - pad, b.y - pad),
                                  ToColor(tg->checkColor), 2.0f);
            }
        }
        float px = 2.0f * s;
        DrawBitmapText(dl, tg->label, labelX,
                       a.y + (sz.y - Font8x8::Height * px) * 0.5f, px, ToColor(tg->textColor));
        if (!tg->interactable) dl->AddRectFilled(a, b, IM_COL32(30, 30, 35, 150), tg->cornerRadius);
    }

    // UI buttons: screen-space, pinned to the canvas (pixels from its top-left).
    for (const auto& up : objs) {
        auto* btn = up->GetComponent<UIButton>();
        if (!btn || !up->active) continue;
        float s = uiScale(up.get());
        Vec2 o, sz; GetUIScreenRect(up.get(), canvasSize.x, canvasSize.y, o, sz);
        if (svCull(up.get(), o, sz)) continue;
        ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
        ImVec2 b(a.x + sz.x, a.y + sz.y);
        // Hover/focus grow: scale the rect about its center.
        if (btn->hoverScale != 1.0f && (btn->IsHovered() || btn->IsFocused())) {
            float gx = sz.x * (btn->hoverScale - 1.0f) * 0.5f;
            float gy = sz.y * (btn->hoverScale - 1.0f) * 0.5f;
            a.x -= gx; a.y -= gy; b.x += gx; b.y += gy;
        }
        dl->AddRectFilled(a, b, ToColor(btn->DisplayColor()), btn->cornerRadius);
        if (btn->borderWidth > 0.0f)
            dl->AddRect(a, b, ToColor(btn->borderColor), btn->cornerRadius, 0, btn->borderWidth);
        float shift = btn->PressShift() * s;
        // Icon placeholder (the editor overlay doesn't load textures), left or
        // right; label takes the remaining space. Press shifts content down.
        float isz = (!btn->icon.empty() && btn->iconSize > 0.0f) ? btn->iconSize * s : 0.0f;
        if (isz > 0.0f) {
            float ix = btn->iconRight ? (b.x - isz - 8 * s) : (a.x + 8 * s);
            ImVec2 ia(ix, a.y + ((b.y - a.y) - isz) * 0.5f + shift);
            dl->AddRectFilled(ia, ImVec2(ia.x + isz, ia.y + isz), IM_COL32(255, 255, 255, 40), 3.0f);
            dl->AddRect(ia, ImVec2(ia.x + isz, ia.y + isz), IM_COL32(255, 255, 255, 110), 3.0f);
        }
        float px = btn->fontScale * s;
        float tw = btn->label.size() * (Font8x8::Width + 1) * px;
        float left  = a.x + (isz > 0.0f && !btn->iconRight ? isz + 12 * s : 0.0f);
        float right = b.x - (isz > 0.0f &&  btn->iconRight ? isz + 12 * s : 0.0f);
        DrawBitmapText(dl, btn->label, left + ((right - left) - tw) * 0.5f,
                       a.y + ((b.y - a.y) - Font8x8::Height * px) * 0.5f + shift, px,
                       ToColor(btn->CurrentTextColor()));
    }

    // UI input fields: box + the text (or placeholder) + a caret when focused.
    for (const auto& up : objs) {
        auto* in = up->GetComponent<UIInputField>();
        if (!in || !up->active) continue;
        float s = uiScale(up.get());
        Vec2 o, sz; GetUIScreenRect(up.get(), canvasSize.x, canvasSize.y, o, sz);
        if (svCull(up.get(), o, sz)) continue;
        ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
        ImVec2 b(a.x + sz.x, a.y + sz.y);
        dl->AddRectFilled(a, b, ToColor(in->CurrentColor()), 4.0f);
        if (in->focused) dl->AddRect(a, b, IM_COL32(120, 170, 255, 255), 4.0f, 0, 1.5f);
        float px = 2.0f * s;
        float pad = 6 * s;
        bool empty = in->text.empty();
        // Horizontal scroll: show the tail that fits so the caret stays visible.
        std::string full = empty ? in->placeholder : in->DisplayText();
        float adv = (Font8x8::Width + 1) * px;
        int fit = adv > 0 ? (int)((sz.x - pad * 2) / adv) : (int)full.size();
        if (fit < 1) fit = 1;
        std::string shown = ((int)full.size() > fit) ? full.substr(full.size() - fit) : full;
        float ty = a.y + (sz.y - Font8x8::Height * px) * 0.5f;
        DrawBitmapText(dl, shown, a.x + pad, ty, px,
                       ToColor(empty ? in->placeholderColor : in->textColor));
        if (in->focused && in->CaretVisible()) {  // blinking caret after the visible text
            float cw = shown.size() * adv;
            float cx = a.x + pad + cw;
            dl->AddLine(ImVec2(cx, ty), ImVec2(cx, ty + Font8x8::Height * px), IM_COL32(255, 255, 255, 220), 1.5f);
        }
    }

    // UI dropdowns: header (shows the selection + a caret); when open, the option
    // list below with the hovered/selected option highlighted. Drawn last among
    // widgets so an open list sits above its neighbours.
    for (const auto& up : objs) {
        auto* dd = up->GetComponent<UIDropdown>();
        if (!dd || !up->active) continue;
        float s = uiScale(up.get());
        Vec2 o, sz; GetUIScreenRect(up.get(), canvasSize.x, canvasSize.y, o, sz);
        if (svCull(up.get(), o, sz)) continue;
        ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
        ImVec2 b(a.x + sz.x, a.y + sz.y);
        float px = 2.0f * s;
        float ty = a.y + (sz.y - Font8x8::Height * px) * 0.5f;
        dl->AddRectFilled(a, b, ToColor(dd->color), 4.0f);
        dl->AddRect(a, b, ToColor(dd->borderColor), 4.0f, 0, 1.0f);
        ImU32 hcol = dd->HasSelection() ? ToColor(dd->textColor) : IM_COL32(150, 152, 158, 255);
        DrawBitmapText(dl, dd->HeaderText(), a.x + 8 * s, ty, px, hcol);
        // Down-caret on the right.
        float cx = b.x - 14 * s, cy = a.y + sz.y * 0.5f;
        dl->AddTriangleFilled(ImVec2(cx - 5 * s, cy - 3 * s), ImVec2(cx + 5 * s, cy - 3 * s),
                              ImVec2(cx, cy + 4 * s), ToColor(dd->textColor));
        if (dd->open) {
            float top = b.y;
            dl->AddRectFilled(ImVec2(a.x, top), ImVec2(b.x, top + sz.y * dd->options.size()),
                              ToColor(dd->listColor), 0.0f);
            for (int i = 0; i < (int)dd->options.size(); ++i) {
                float oy = top + i * sz.y;
                if (i == dd->HoveredOption())
                    dl->AddRectFilled(ImVec2(a.x, oy), ImVec2(b.x, oy + sz.y), ToColor(dd->hoverColor), 0.0f);
                DrawBitmapText(dl, dd->options[i], a.x + 8 * s,
                               oy + (sz.y - Font8x8::Height * px) * 0.5f, px, ToColor(dd->textColor));
            }
            dl->AddRect(ImVec2(a.x, top), ImVec2(b.x, top + sz.y * dd->options.size()),
                        ToColor(dd->borderColor), 0.0f, 0, 1.0f);
        }
        if (!dd->interactable) dl->AddRectFilled(a, b, IM_COL32(30, 30, 35, 150), 4.0f);
    }

    // UI tooltips: when a sibling widget has been hovered long enough (Ready),
    // draw the hint box next to the cursor. Tooltips tick only while the scene
    // updates (Play / Game view), matching the built game.
    for (const auto& up : objs) {
        auto* tt = up->GetComponent<UITooltip>();
        if (!tt || !up->active || !tt->Ready()) continue;
        Vec2 m = Input::MousePosition();
        float px = 2.0f;
        float tw = tt->text.size() * (Font8x8::Width + 1) * px;
        float th = Font8x8::Height * px;
        ImVec2 a(canvasPos.x + m.x + 14, canvasPos.y + m.y + 14);
        ImVec2 b(a.x + tw + 12, a.y + th + 10);
        dl->AddRectFilled(a, b, ToColor(tt->background), 4.0f);
        dl->AddRect(a, b, ToColor(tt->borderColor), 4.0f);
        DrawBitmapText(dl, tt->text, a.x + 6, a.y + 5, px, ToColor(tt->textColor));
    }

    // Selection highlight for the selected widget — works for every UI type and
    // in both the 2D and 3D Scene views (the Game view stays clean). Drag the
    // body to move; drag a handle to resize (handles only on sizable widgets).
    if (!gameView && ed.selected()) {
        UIRect r = GetUIRect(ed.selected());
        Vec2 o, sz;
        if (GetUIScreenRect(ed.selected(), canvasSize.x, canvasSize.y, o, sz)) {
            ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
            ImVec2 b(a.x + sz.x, a.y + sz.y);
            // Anchor marker: a diamond at the canvas point this widget anchors to,
            // with a thin line to the widget — so anchoring is visible at a glance.
            int ai = (int)r.anchor;
            float ax = (ai % 3 == 0) ? 0.0f : (ai % 3 == 1) ? canvasSize.x * 0.5f : canvasSize.x;
            float ay = (ai / 3 == 0) ? 0.0f : (ai / 3 == 1) ? canvasSize.y * 0.5f : canvasSize.y;
            ImVec2 ap(canvasPos.x + ax, canvasPos.y + ay);
            ImVec2 wc((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
            dl->AddLine(ap, wc, IM_COL32(90, 170, 240, 150), 1.0f);
            dl->AddNgonFilled(ap, 5.0f, IM_COL32(90, 170, 240, 230), 4);  // diamond
            // Selection box.
            dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1),
                        IM_COL32(255, 200, 0, 255), 2.0f, 0, 2.0f);
            if (r.sizePtr) {   // resizable: draw the 8 grab handles
                ImVec2 h[8]; UIHandlePositions(a, b, h);
                for (int i = 0; i < 8; ++i)
                    dl->AddRectFilled(ImVec2(h[i].x - 4, h[i].y - 4),
                                      ImVec2(h[i].x + 4, h[i].y + 4), IM_COL32(255, 200, 0, 255));
            }
            // Live size readout above the box (the unscaled pixel size you author).
            char dims[48];
            std::snprintf(dims, sizeof(dims), "%g x %g", r.size.x, r.size.y);
            dl->AddText(ImVec2(a.x, a.y - 16), IM_COL32(255, 220, 120, 255), dims);
        }
        // Smart-snap alignment guides (magenta), full canvas extent.
        if (g_uiGuideX >= 0.0f)
            dl->AddLine(ImVec2(canvasPos.x + g_uiGuideX, canvasPos.y),
                        ImVec2(canvasPos.x + g_uiGuideX, canvasPos.y + canvasSize.y),
                        IM_COL32(255, 80, 220, 200), 1.0f);
        if (g_uiGuideY >= 0.0f)
            dl->AddLine(ImVec2(canvasPos.x, canvasPos.y + g_uiGuideY),
                        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + g_uiGuideY),
                        IM_COL32(255, 80, 220, 200), 1.0f);
    }
}

// Click-to-select and drag-to-reposition for screen-space UI in the Scene view.
// Runs in both 2D and 3D modes (UI is an overlay, identical in either), and
// takes priority over the world picking below it — clicking a HUD button selects
// the button, dragging it edits its pixel offset. Sets g_uiHandled so the 2D/3D
// world pickers skip a click the UI already consumed.
void EditUIWidgets(EditorState& ed, ImVec2 canvasPos, ImVec2 canvasSize,
                   bool hovered, ImGuiIO& io) {
    g_uiHandled = false;
    UICanvas::Set(canvasSize.x, canvasSize.y);
    Vec2 mouseCanvas{io.MousePos.x - canvasPos.x, io.MousePos.y - canvasPos.y};

    // Mouse wheel over a Scroll View's viewport scrolls it (preview authoring).
    if (hovered && io.MouseWheel != 0.0f && !ed.isPlaying()) {  // play mode scrolls via Input wheel
        for (const auto& up : ed.scene().Objects()) {
            auto* sv = up->GetComponent<UIScrollView>();
            if (!sv || !up->active) continue;
            float s = UIScaleFor(up.get(), canvasSize.x, canvasSize.y);
            Vec2 o = ResolveAnchor(sv->anchor, sv->position * s, sv->size * s, canvasSize.x, canvasSize.y);
            if (mouseCanvas.x >= o.x && mouseCanvas.x <= o.x + sv->size.x * s &&
                mouseCanvas.y >= o.y && mouseCanvas.y <= o.y + sv->size.y * s) {
                sv->ScrollBy(-io.MouseWheel * 30.0f);
                g_uiHandled = true;
                break;
            }
        }
    }

    // Continue an in-progress move/resize of the grabbed widget. Mouse pixels are
    // divided by the owning Canvas's scale so the widget tracks the cursor 1:1
    // even when the canvas is scaling the UI.
    if (g_uiDragTarget) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            UIRect r = GetUIRect(g_uiDragTarget);
            if (r.valid && r.position) {
                float s = UIScaleFor(g_uiDragTarget, canvasSize.x, canvasSize.y);
                if (s < 1e-3f) s = 1.0f;
                float dx = io.MouseDelta.x / s, dy = io.MouseDelta.y / s;
                float grid = g_uiGrid > 0 ? (float)g_uiGrid : 1.0f;
                auto snap = [&](float v) { return g_snap ? Mathf::Round(v / grid) * grid : v; };
                if (g_uiResizeHandle >= 0 && r.sizePtr) {
                    int hdl = g_uiResizeHandle;
                    bool left  = (hdl == 0 || hdl == 6 || hdl == 7);
                    bool right = (hdl == 2 || hdl == 3 || hdl == 4);
                    bool top   = (hdl == 0 || hdl == 1 || hdl == 2);
                    bool bottom= (hdl == 4 || hdl == 5 || hdl == 6);
                    // Resize in SCREEN space so the grabbed edge follows the cursor
                    // for ANY anchor, then invert the anchor to recover position.
                    Vec2 o, sz;
                    GetUIScreenRect(g_uiDragTarget, canvasSize.x, canvasSize.y, o, sz);
                    if (UIScrollView* sv = OwningScrollView(g_uiDragTarget)) o.y += sv->scroll * s; // undo scroll
                    float l = o.x, t = o.y, rr = o.x + sz.x, bb = o.y + sz.y;
                    float minPx = 8.0f * s;
                    if (left)   l  = Mathf::Min(l + io.MouseDelta.x, rr - minPx);
                    if (right)  rr = Mathf::Max(rr + io.MouseDelta.x, l + minPx);
                    if (top)    t  = Mathf::Min(t + io.MouseDelta.y, bb - minPx);
                    if (bottom) bb = Mathf::Max(bb + io.MouseDelta.y, t + minPx);
                    // Snap the dragged edge to canvas/sibling guide lines.
                    g_uiGuideX = g_uiGuideY = -1.0f;
                    bool gxHit = false, gyHit = false;
                    if (g_snap) {
                        std::vector<float> cx{0.0f, canvasSize.x * 0.5f, canvasSize.x};
                        std::vector<float> cy{0.0f, canvasSize.y * 0.5f, canvasSize.y};
                        for (const auto& up2 : ed.scene().Objects()) {
                            if (up2.get() == g_uiDragTarget) continue;
                            Vec2 oo, ss;
                            if (GetUIScreenRect(up2.get(), canvasSize.x, canvasSize.y, oo, ss)) {
                                cx.push_back(oo.x); cx.push_back(oo.x + ss.x);
                                cy.push_back(oo.y); cy.push_back(oo.y + ss.y);
                            }
                        }
                        const float thr = 6.0f;
                        auto edge = [&](float v, const std::vector<float>& cs, float& guide, bool& hit) {
                            float best = thr, res = v;
                            for (float c : cs) { float d = v > c ? v - c : c - v; if (d < best) { best = d; res = c; guide = c; hit = true; } }
                            return res;
                        };
                        if (left)   l  = edge(l,  cx, g_uiGuideX, gxHit);
                        if (right)  rr = edge(rr, cx, g_uiGuideX, gxHit);
                        if (top)    t  = edge(t,  cy, g_uiGuideY, gyHit);
                        if (bottom) bb = edge(bb, cy, g_uiGuideY, gyHit);
                    }
                    Vec2 newScreen{rr - l, bb - t};
                    Vec2 term = ResolveAnchor(r.anchor, Vec2{0.0f, 0.0f}, newScreen, canvasSize.x, canvasSize.y);
                    // Grid-snap only on axes that didn't lock onto a guide.
                    auto gsnap = [&](float v, bool guided) { return (g_snap && !guided) ? Mathf::Round(v / grid) * grid : v; };
                    r.sizePtr->x  = gsnap(newScreen.x / s, gxHit);
                    r.sizePtr->y  = gsnap(newScreen.y / s, gyHit);
                    r.position->x = gsnap((l - term.x) / s, gxHit);
                    r.position->y = gsnap((t - term.y) / s, gyHit);
                } else {
                    r.position->x = snap(r.position->x + dx);
                    r.position->y = snap(r.position->y + dy);
                    // Unity-style smart guides: snap our edges/center to the canvas
                    // edges/center and to sibling widgets, drawing a guide line.
                    g_uiGuideX = g_uiGuideY = -1.0f;
                    if (g_snap) {
                        Vec2 o, sz; GetUIScreenRect(g_uiDragTarget, canvasSize.x, canvasSize.y, o, sz);
                        std::vector<float> cx{0.0f, canvasSize.x * 0.5f, canvasSize.x};
                        std::vector<float> cy{0.0f, canvasSize.y * 0.5f, canvasSize.y};
                        for (const auto& up2 : ed.scene().Objects()) {
                            if (up2.get() == g_uiDragTarget) continue;
                            Vec2 oo, ss;
                            if (GetUIScreenRect(up2.get(), canvasSize.x, canvasSize.y, oo, ss)) {
                                cx.push_back(oo.x); cx.push_back(oo.x + ss.x * 0.5f); cx.push_back(oo.x + ss.x);
                                cy.push_back(oo.y); cy.push_back(oo.y + ss.y * 0.5f); cy.push_back(oo.y + ss.y);
                            }
                        }
                        const float thr = 6.0f;
                        auto bestSnap = [&](float lo, float mid, float hi, const std::vector<float>& cands,
                                            float& guide) -> float {
                            float best = thr, adj = 0.0f; bool found = false;
                            for (float m : {lo, mid, hi})
                                for (float c : cands) {
                                    float d = m > c ? m - c : c - m;
                                    if (d < best) { best = d; adj = c - m; guide = c; found = true; }
                                }
                            return found ? adj : 0.0f;
                        };
                        float gx = -1.0f, gy = -1.0f;
                        float adjX = bestSnap(o.x, o.x + sz.x * 0.5f, o.x + sz.x, cx, gx);
                        float adjY = bestSnap(o.y, o.y + sz.y * 0.5f, o.y + sz.y, cy, gy);
                        if (adjX != 0.0f) { r.position->x += adjX / s; g_uiGuideX = gx; }
                        if (adjY != 0.0f) { r.position->y += adjY / s; g_uiGuideY = gy; }
                    }
                }
                ed.dirty = true;
            }
            g_uiHandled = true;
            return;
        }
        g_uiDragTarget = nullptr;
        g_uiResizeHandle = -1;
        g_uiGuideX = g_uiGuideY = -1.0f;   // clear guides when the drag ends
    }

    // Press: first try a resize handle on the current selection, then fall back
    // to picking a widget to select + move.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // Resize handles of the selected, sizable widget take priority.
        if (ed.selected()) {
            UIRect sr = GetUIRect(ed.selected());
            Vec2 o, sz;
            if (sr.sizePtr && GetUIScreenRect(ed.selected(), canvasSize.x, canvasSize.y, o, sz)) {
                ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y);
                ImVec2 b(a.x + sz.x, a.y + sz.y);
                ImVec2 h[8]; UIHandlePositions(a, b, h);
                for (int i = 0; i < 8; ++i) {
                    float dx = io.MousePos.x - h[i].x, dy = io.MousePos.y - h[i].y;
                    if (dx * dx + dy * dy <= 9.0f * 9.0f) {
                        ed.PushUndo();                     // checkpoint before a resize
                        g_uiDragTarget = ed.selected();
                        g_uiResizeHandle = i;
                        g_uiHandled = true;
                        break;
                    }
                }
            }
        }
        // Otherwise pick a widget under the cursor and start moving it.
        if (!g_uiHandled) {
            GameObject* hit = UIRaycast(ed.scene(), mouseCanvas, canvasSize.x, canvasSize.y);
            if (hit) {
                ed.Select(hit);
                ed.PushUndo();                             // checkpoint before a move
                g_uiDragTarget = hit;
                g_uiResizeHandle = -1;
                g_uiHandled = true;
            }
        }
    }
}

void DrawScene2D(EditorState& ed, ImDrawList* dl, ImVec2 canvasPos, ImVec2 canvasSize,
                 ImVec2 canvasEnd, bool hovered, ImGuiIO& io, bool gameView = false) {
    ImVec2 center(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);
    // The Game view frames through the scene's main camera (position + ortho
    // size); the Scene view uses the editor's free camera.
    Vec2  camPos = ed.cameraPos;
    float zoom   = ed.cameraZoom;
    if (gameView) {
        if (Camera* mc = SceneCamera(ed.scene())) {
            Vec3 cp = mc->gameObject->transform->Position();
            camPos = {cp.x, cp.y};
            zoom = mc->orthographicSize * 2.0f;        // viewport world-height
        }
    }
    float scale = canvasSize.y / zoom;
    auto worldToScreen = [&](const Vec3& w) {
        return ImVec2(center.x + (w.x - camPos.x) * scale,
                      center.y - (w.y - camPos.y) * scale);
    };
    auto screenToWorld = [&](const ImVec2& s) {
        return Vec2((s.x - center.x) / scale + camPos.x,
                    -(s.y - center.y) / scale + camPos.y);
    };

    if (!gameView && hovered && io.MouseWheel != 0.0f)
        ed.cameraZoom = Mathf::Clamp(ed.cameraZoom * (1.0f - io.MouseWheel * 0.1f), 2.0f, 200.0f);
    if (!gameView && hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ed.cameraPos.x -= io.MouseDelta.x / scale;
        ed.cameraPos.y += io.MouseDelta.y / scale;
    }

    dl->PushClipRect(canvasPos, canvasEnd, true);

    // Faint unit grid + world axes — Scene view only (the Game view is clean).
    if (!gameView) {
        float halfH = zoom * 0.5f;
        float halfW = halfH * (canvasSize.x / canvasSize.y);
        int x0 = (int)Mathf::Floor(camPos.x - halfW);
        int x1 = (int)Mathf::Ceil(camPos.x + halfW);
        int y0 = (int)Mathf::Floor(camPos.y - halfH);
        int y1 = (int)Mathf::Ceil(camPos.y + halfH);
        ImU32 grid = IM_COL32(40, 40, 52, 255);
        if (x1 - x0 < 200) // avoid drawing thousands of lines when zoomed way out
            for (int x = x0; x <= x1; ++x)
                dl->AddLine(worldToScreen({(float)x, (float)y0, 0}),
                            worldToScreen({(float)x, (float)y1, 0}), grid);
        if (y1 - y0 < 200)
            for (int y = y0; y <= y1; ++y)
                dl->AddLine(worldToScreen({(float)x0, (float)y, 0}),
                            worldToScreen({(float)x1, (float)y, 0}), grid);
        dl->AddLine(worldToScreen({-1000, 0, 0}), worldToScreen({1000, 0, 0}), IM_COL32(120, 60, 60, 255));
        dl->AddLine(worldToScreen({0, -1000, 0}), worldToScreen({0, 1000, 0}), IM_COL32(60, 120, 60, 255));
    }

    const auto& objs = ed.scene().Objects();
    for (const auto& up : objs) {
        GameObject* go = up.get();
        auto* sr = go->GetComponent<SpriteRenderer>();
        if (!sr || !go->active) continue;
        Vec3 wp = go->transform->Position();
        Vec3 ls = go->transform->LossyScale();
        float hx = sr->size.x * ls.x * 0.5f * scale;
        float hy = sr->size.y * ls.y * 0.5f * scale;
        ImVec2 c = worldToScreen(wp);
        ImVec2 a(c.x - hx, c.y - hy), b(c.x + hx, c.y + hy);
        dl->AddRectFilled(a, b, ToColor(sr->color));
        if (!gameView && go == ed.selected())
            dl->AddRect(ImVec2(a.x - 2, a.y - 2), ImVec2(b.x + 2, b.y + 2),
                        IM_COL32(255, 200, 0, 255), 0, 0, 2.0f);
    }

    // Tilemaps: filled cells, color keyed by tile id.
    for (const auto& up : objs) {
        auto* tm = up->GetComponent<Tilemap>();
        if (!tm || !up->active) continue;
        float half = tm->tileSize * 0.5f * scale;
        for (int y = 0; y < tm->Height(); ++y)
            for (int x = 0; x < tm->Width(); ++x) {
                int id = tm->GetTile(x, y);
                if (id == 0) continue;
                ImVec2 cc = worldToScreen(tm->CellToWorld(x, y));
                ImU32 col = IM_COL32(60 + (id * 70) % 196, 60 + (id * 130) % 196,
                                     80 + (id * 50) % 176, 255);
                dl->AddRectFilled(ImVec2(cc.x - half, cc.y - half),
                                  ImVec2(cc.x + half, cc.y + half), col);
            }
    }

    // Particles: filled circles (move during Play).
    for (const auto& up : objs) {
        auto* ps = up->GetComponent<ParticleSystem>();
        if (!ps || !up->active) continue;
        for (const auto& p : ps->Particles()) {
            if (!p.alive) continue;
            ImVec2 sp = worldToScreen(p.position);
            dl->AddCircleFilled(sp, Mathf::Max(1.0f, p.size * scale * 0.5f), ToColor(p.color));
        }
    }

    // World-anchored text (screen-space text is drawn by the shared UI overlay).
    for (const auto& up : objs) {
        auto* tr = up->GetComponent<TextRenderer>();
        if (!tr || !up->active || tr->screenSpace) continue;
        ImU32 col = ToColor(tr->color);
        ImU32 sh = ToColor(tr->shadowColor);
        ImVec2 o = worldToScreen(up->transform->Position());
        float px = tr->pixelSize * scale;
        if (tr->shadow)
            DrawBitmapText(dl, tr->text, o.x + tr->shadowOffset.x * px,
                           o.y + tr->shadowOffset.y * px, px, sh);
        DrawBitmapText(dl, tr->text, o.x, o.y, px, col);
        if (tr->bold) DrawBitmapText(dl, tr->text, o.x + px, o.y, px, col);  // faux-bold
    }

    // 2D collider wireframes (Unity-style green outlines), Scene view only.
    if (!gameView && g_showColliders) {
        const ImU32 cg = IM_COL32(90, 230, 120, 255);
        for (const auto& up : objs) {
            if (!up->active) continue;
            if (auto* bc = up->GetComponent<BoxCollider2D>()) {
                Vec2 c = bc->WorldCenter(), h = bc->HalfExtents();
                ImVec2 a = worldToScreen(Vec3{c.x - h.x, c.y - h.y, 0});
                ImVec2 b = worldToScreen(Vec3{c.x + h.x, c.y + h.y, 0});
                dl->AddRect(a, b, cg, 0, 0, 1.5f);
            }
            if (auto* cc = up->GetComponent<CircleCollider2D>())
                dl->AddCircle(worldToScreen(Vec3{cc->WorldCenter(), 0}),
                              cc->WorldRadius() * scale, cg, 28, 1.5f);
            if (auto* cap = up->GetComponent<CapsuleCollider2D>()) {
                Vec2 s0, s1; cap->Segment(s0, s1); float r = cap->WorldRadius();
                dl->AddCircle(worldToScreen(Vec3{s0, 0}), r * scale, cg, 20, 1.5f);
                dl->AddCircle(worldToScreen(Vec3{s1, 0}), r * scale, cg, 20, 1.5f);
                Vec2 n = (cap->direction == CapsuleCollider2D::Direction::Vertical)
                       ? Vec2{r, 0} : Vec2{0, r};
                dl->AddLine(worldToScreen(Vec3{s0 + n, 0}), worldToScreen(Vec3{s1 + n, 0}), cg, 1.5f);
                dl->AddLine(worldToScreen(Vec3{s0 - n, 0}), worldToScreen(Vec3{s1 - n, 0}), cg, 1.5f);
            }
        }
    }

    // Screen-space UI (text, images, panels, bars, sliders, toggles, buttons).
    DrawUIOverlay(ed, dl, canvasPos, canvasSize, gameView);

    // Transform gizmo at the selection, reflecting the active tool (Move/Rotate/Scale).
    if (!gameView && ed.selected() && !IsUIElement(ed.selected())) {
        ImVec2 g = worldToScreen(ed.selected()->transform->Position());
        if (g_tool == Tool::Move) {
            dl->AddLine(g, ImVec2(g.x + 36, g.y), IM_COL32(230, 70, 70, 255), 2.0f);   // +X red
            dl->AddTriangleFilled(ImVec2(g.x + 36, g.y - 4), ImVec2(g.x + 36, g.y + 4),
                                  ImVec2(g.x + 44, g.y), IM_COL32(230, 70, 70, 255));
            dl->AddLine(g, ImVec2(g.x, g.y - 36), IM_COL32(80, 210, 90, 255), 2.0f);   // +Y green
            dl->AddTriangleFilled(ImVec2(g.x - 4, g.y - 36), ImVec2(g.x + 4, g.y - 36),
                                  ImVec2(g.x, g.y - 44), IM_COL32(80, 210, 90, 255));
        } else if (g_tool == Tool::Rotate) {
            dl->AddCircle(g, 34.0f, IM_COL32(90, 170, 240, 255), 32, 2.0f);
        } else { // Scale — axis lines capped with square handles
            dl->AddLine(g, ImVec2(g.x + 36, g.y), IM_COL32(230, 70, 70, 255), 2.0f);
            dl->AddRectFilled(ImVec2(g.x + 32, g.y - 4), ImVec2(g.x + 40, g.y + 4), IM_COL32(230, 70, 70, 255));
            dl->AddLine(g, ImVec2(g.x, g.y - 36), IM_COL32(80, 210, 90, 255), 2.0f);
            dl->AddRectFilled(ImVec2(g.x - 4, g.y - 40), ImVec2(g.x + 4, g.y - 32), IM_COL32(80, 210, 90, 255));
        }
    }

    dl->PopClipRect();

    if (gameView) return;   // the Game view is non-interactive

    if (hovered && !g_uiHandled && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        Vec2 world = screenToWorld(io.MousePos);
        GameObject* hit = nullptr;
        for (const auto& up : objs) {
            GameObject* go = up.get();
            auto* sr = go->GetComponent<SpriteRenderer>();
            if (!sr || !go->active) continue;
            Vec3 wp = go->transform->Position();
            Vec3 ls = go->transform->LossyScale();
            float hx = sr->size.x * ls.x * 0.5f, hy = sr->size.y * ls.y * 0.5f;
            if (world.x >= wp.x - hx && world.x <= wp.x + hx &&
                world.y >= wp.y - hy && world.y <= wp.y + hy)
                hit = go;
        }
        ed.Select(hit);
    }
    // Selecting and dragging work in Play too (edits revert on Stop, like Unity).
    // Skipped while a UI widget is being dragged (it owns the drag this frame).
    if (ed.selected() && hovered && !g_uiHandled && !IsUIElement(ed.selected()) &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        Transform* t = ed.selected()->transform;
        if (g_tool == Tool::Move) {
            Vec3& lp = t->localPosition;
            lp.x += io.MouseDelta.x / scale;
            lp.y -= io.MouseDelta.y / scale;
            if (g_snap && g_snapSize > 0.0f) {
                lp.x = Mathf::Round(lp.x / g_snapSize) * g_snapSize;
                lp.y = Mathf::Round(lp.y / g_snapSize) * g_snapSize;
            }
        } else if (g_tool == Tool::Rotate) {
            t->Rotate({0, 0, -io.MouseDelta.x * 0.5f});      // drag X to spin about Z
        } else { // Scale (uniform; horizontal drag grows/shrinks)
            float k = 1.0f + io.MouseDelta.x * 0.01f;
            t->localScale = t->localScale * k;
        }
        ed.dirty = true;
    }
}

void DrawScene3D(EditorState& ed, ImDrawList* dl, ImVec2 canvasPos, ImVec2 canvasSize,
                 ImVec2 canvasEnd, bool hovered, ImGuiIO& io, bool gameView = false) {
    ImVec2 center(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);

    // Orbit controls (Scene view only).
    if (!gameView && hovered && io.MouseWheel != 0.0f)
        ed.camDist = Mathf::Clamp(ed.camDist * (1.0f - io.MouseWheel * 0.1f), 2.0f, 400.0f);
    if (!gameView && hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ed.selected()) {
        ed.camYaw   -= io.MouseDelta.x * 0.4f;
        ed.camPitch = Mathf::Clamp(ed.camPitch + io.MouseDelta.y * 0.4f, -85.0f, 85.0f);
    }
    if (!gameView && hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ed.camYaw   -= io.MouseDelta.x * 0.4f;
        ed.camPitch = Mathf::Clamp(ed.camPitch + io.MouseDelta.y * 0.4f, -85.0f, 85.0f);
    }
    // Fly the Scene camera with WASD while holding the right mouse button
    // (Unity-style); Q/E go down/up, Shift moves faster. Right-mouse gating keeps
    // the W/E/R tool shortcuts working normally when not flying.
    if (!gameView && ImGui::IsMouseDown(ImGuiMouseButton_Right) && !io.WantTextInput) {
        float yr = ed.camYaw * Mathf::Deg2Rad;
        Vec3 fwd{-Mathf::Sin(yr), 0.0f, -Mathf::Cos(yr)};   // ground-plane forward
        Vec3 right{Mathf::Cos(yr), 0.0f, -Mathf::Sin(yr)};
        float spd = ed.camDist * (io.KeyShift ? 3.5f : 1.6f) * io.DeltaTime;
        Vec3 mv{0, 0, 0};
        if (ImGui::IsKeyDown(ImGuiKey_W)) mv = mv + fwd;
        if (ImGui::IsKeyDown(ImGuiKey_S)) mv = mv - fwd;
        if (ImGui::IsKeyDown(ImGuiKey_D)) mv = mv + right;
        if (ImGui::IsKeyDown(ImGuiKey_A)) mv = mv - right;
        if (ImGui::IsKeyDown(ImGuiKey_E)) mv.y += 1.0f;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) mv.y -= 1.0f;
        ed.camTarget = ed.camTarget + mv * spd;
    }

    float yawR = ed.camYaw * Mathf::Deg2Rad, pitchR = ed.camPitch * Mathf::Deg2Rad;
    Vec3 dir{Mathf::Cos(pitchR) * Mathf::Sin(yawR), Mathf::Sin(pitchR),
             Mathf::Cos(pitchR) * Mathf::Cos(yawR)};
    Vec3 eye = ed.camTarget + dir * ed.camDist;
    Mat4 view = Mat4::LookAt(eye, ed.camTarget, Vec3::Up);
    Mat4 proj = Mat4::Perspective(50.0f, canvasSize.x / canvasSize.y, 0.1f, 2000.0f);
    // The Game view renders through the scene's main camera instead.
    if (gameView) {
        if (Camera* mc = SceneCamera(ed.scene())) {
            eye = mc->gameObject->transform->Position();
            view = mc->ViewMatrix();
            proj = mc->ProjectionMatrix(canvasSize.x / canvasSize.y);
        }
    }
    Mat4 vp = proj * view;

    auto toScreen = [&](const Vec4& c, ImVec2& out) -> bool {
        if (c.w <= 0.05f) return false;
        out = ImVec2(center.x + (c.x / c.w) * canvasSize.x * 0.5f,
                     center.y - (c.y / c.w) * canvasSize.y * 0.5f);
        return true;
    };

    dl->PushClipRect(canvasPos, canvasEnd, true);

    // Skybox: a vertical sky gradient behind everything, using the scene's
    // render settings (these save with the scene, so the built game matches).
    // Drawn first so the grid and the transparent mesh layer sit on top of it.
    // A camera set to "Solid Color" clear flags suppresses the skybox in the
    // Game view (the camera's background color shows through instead).
    const auto& rs = ed.scene().renderSettings;
    bool solidClear = false;
    if (gameView) {
        if (Camera* gmc = SceneCamera(ed.scene()))
            solidClear = gmc->clearFlags == Camera::ClearFlags::SolidColor;
    }
    if (rs.skybox && !solidClear) {
        ImU32 top = ToColor(rs.skyTop);
        ImU32 mid = ToColor(rs.skyHorizon);
        ImU32 bot = ToColor(rs.skyBottom);
        ImVec2 cmid(canvasEnd.x, (canvasPos.y + canvasEnd.y) * 0.5f);
        dl->AddRectFilledMultiColor(canvasPos, cmid, top, top, mid, mid);
        dl->AddRectFilledMultiColor(ImVec2(canvasPos.x, cmid.y), canvasEnd, mid, mid, bot, bot);
    }

    // Ground grid on the XZ plane (Scene view only; the Game view is clean).
    if (!gameView)
    for (int i = -10; i <= 10; ++i) {
        ImVec2 a, b;
        if (toScreen(vp * Vec4{Vec3{(float)i, 0, -10}, 1}, a) &&
            toScreen(vp * Vec4{Vec3{(float)i, 0, 10}, 1}, b))
            dl->AddLine(a, b, IM_COL32(50, 50, 64, 255));
        if (toScreen(vp * Vec4{Vec3{-10, 0, (float)i}, 1}, a) &&
            toScreen(vp * Vec4{Vec3{10, 0, (float)i}, 1}, b))
            dl->AddLine(a, b, IM_COL32(50, 50, 64, 255));
    }

    const auto& objs = ed.scene().Objects();

    // Editor gizmos for non-visual objects (Scene view only): a wireframe
    // frustum for cameras, a sun + direction arrow for lights, so you can see
    // and place them even though they don't render a mesh.
    if (!gameView) {
        auto line = [&](const Vec3& a, const Vec3& b, ImU32 col, float th = 1.5f) {
            ImVec2 pa, pb;
            if (toScreen(vp * Vec4{a, 1}, pa) && toScreen(vp * Vec4{b, 1}, pb))
                dl->AddLine(pa, pb, col, th);
        };
        // A ring of `seg` segments centered at c, in the plane spanned by u,v.
        auto ring = [&](const Vec3& c, const Vec3& u, const Vec3& v, float r,
                        ImU32 col, int seg = 24) {
            Vec3 prev = c + u * r;
            for (int k = 1; k <= seg; ++k) {
                float a = (float)k / seg * 6.2831853f;
                Vec3 cur = c + u * (r * Mathf::Cos(a)) + v * (r * Mathf::Sin(a));
                line(prev, cur, col, 1.0f);
                prev = cur;
            }
        };
        // Box wireframe (axis-aligned) from center + half extents.
        auto box = [&](const Vec3& c, const Vec3& h, ImU32 col) {
            Vec3 cr[8];
            for (int i = 0; i < 8; ++i)
                cr[i] = {c.x + ((i & 1) ? h.x : -h.x), c.y + ((i & 2) ? h.y : -h.y),
                         c.z + ((i & 4) ? h.z : -h.z)};
            static const int e[12][2] = {{0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},
                                         {6,4},{0,4},{1,5},{2,6},{3,7}};
            for (auto& ed2 : e) line(cr[ed2[0]], cr[ed2[1]], col, 1.0f);
        };
        const ImU32 kColliderCol = IM_COL32(90, 230, 120, 255); // Unity-ish green

        for (const auto& up : objs) {
            if (!up->active) continue;
            Transform* t = up->transform;
            Vec3 p = t->Position();

            if (up->GetComponent<Camera>()) {
                ImU32 col = (up.get() == ed.selected()) ? IM_COL32(255, 220, 90, 255)
                                                        : IM_COL32(120, 200, 255, 255);
                Vec3 f = t->Forward(), r = t->Right(), u = t->Up();
                float d = 1.4f, w = 0.5f, h = 0.35f;
                Vec3 c[4] = { p + f * d + r * w + u * h, p + f * d - r * w + u * h,
                              p + f * d - r * w - u * h, p + f * d + r * w - u * h };
                for (int i = 0; i < 4; ++i) {
                    line(p, c[i], col);
                    line(c[i], c[(i + 1) % 4], col);
                }
            }

            if (auto* lt = up->GetComponent<Light>()) {
                // Tint the gizmo by the light's own color (so lights are
                // distinguishable), brightening to yellow when selected.
                auto chan = [](float v) { return (int)(Mathf::Clamp(v, 0.0f, 1.0f) * 185.0f) + 70; };
                ImU32 col = (up.get() == ed.selected())
                    ? IM_COL32(255, 220, 90, 255)
                    : IM_COL32(chan(lt->color.r), chan(lt->color.g), chan(lt->color.b), 255);
                // Sun: short rays around the light's position.
                Vec3 ax[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
                for (auto& a : ax) { line(p - a * 0.25f, p + a * 0.25f, col); }
                Vec3 f = t->Forward(), r = t->Right(), u = t->Up();
                if (lt->type == Light::Type::Point) {
                    // Range sphere: three rings so the reach is readable in 3D.
                    ring(p, Vec3{1,0,0}, Vec3{0,1,0}, lt->range, col);
                    ring(p, Vec3{1,0,0}, Vec3{0,0,1}, lt->range, col);
                    ring(p, Vec3{0,1,0}, Vec3{0,0,1}, lt->range, col);
                } else if (lt->type == Light::Type::Spot) {
                    // Cone: apex at the light, base ring at `range` with a radius
                    // from the half-angle; four edge lines to the rim.
                    float half = Mathf::Clamp(lt->spotAngle * 0.5f, 1.0f, 89.0f) * Mathf::Deg2Rad;
                    float rad = lt->range * Mathf::Tan(half);
                    Vec3 baseC = p + f * lt->range;
                    ring(baseC, r, u, rad, col);
                    line(p, baseC + r * rad, col);
                    line(p, baseC - r * rad, col);
                    line(p, baseC + u * rad, col);
                    line(p, baseC - u * rad, col);
                } else {
                    // Directional: an arrow along forward (the sun's direction).
                    Vec3 tip = p + f * 1.4f;
                    line(p, tip, col, 2.0f);
                    line(tip, tip - f * 0.3f + u * 0.15f, col);
                    line(tip, tip - f * 0.3f - u * 0.15f, col);
                }
            }

            // 3D collider wireframes (Unity-style green outlines).
            if (g_showColliders) {
                const Vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};
                if (auto* bc = up->GetComponent<BoxCollider3D>())
                    box(bc->WorldCenter(), bc->HalfExtents(), kColliderCol);
                if (auto* sp = up->GetComponent<SphereCollider3D>()) {
                    Vec3 c = sp->WorldCenter(); float r = sp->WorldRadius();
                    ring(c, X, Y, r, kColliderCol);
                    ring(c, X, Z, r, kColliderCol);
                    ring(c, Y, Z, r, kColliderCol);
                }
                if (auto* cap = up->GetComponent<CapsuleCollider3D>()) {
                    Vec3 a, b; cap->Segment(a, b); float r = cap->WorldRadius();
                    Vec3 u = cap->axis == 0 ? Y : X;
                    Vec3 v = cap->axis == 2 ? Y : Z;
                    ring(a, u, v, r, kColliderCol);
                    ring(b, u, v, r, kColliderCol);
                    line(a + u * r, b + u * r, kColliderCol, 1.0f);
                    line(a - u * r, b - u * r, kColliderCol, 1.0f);
                    line(a + v * r, b + v * r, kColliderCol, 1.0f);
                    line(a - v * r, b - v * r, kColliderCol, 1.0f);
                }
            }
        }
    }

    // Solid meshes: render z-buffered into a texture (correct per-pixel
    // occlusion) and blit it; transparent where empty so the grid shows through.
    if (SDL_Texture* tex = Render3DTexture(ed.scene(), vp, eye,
                                           (int)canvasSize.x, (int)canvasSize.y,
                                           gameView ? 1 : 0))
        dl->AddImage((ImTextureID)tex, canvasPos, canvasEnd);

    // Highlight the selection with a clean yellow bounding box (12 edges).
    if (!gameView && ed.selected()) {
        if (auto* mr = ed.selected()->GetComponent<MeshRenderer>()) {
            Mat4 m = vp * ed.selected()->transform->LocalToWorldMatrix();
            Vec3 lo, hi; mr->mesh.Bounds(lo, hi);
            ImVec2 corner[8]; bool cOk[8];
            for (int ci = 0; ci < 8; ++ci)
                cOk[ci] = toScreen(m * Vec4{Vec3{(ci & 1) ? hi.x : lo.x,
                                                 (ci & 2) ? hi.y : lo.y,
                                                 (ci & 4) ? hi.z : lo.z}, 1}, corner[ci]);
            static const int edges[12][2] = {
                {0,1},{1,3},{3,2},{2,0}, {4,5},{5,7},{7,6},{6,4}, {0,4},{1,5},{2,6},{3,7}};
            ImU32 y = IM_COL32(255, 200, 0, 220);
            for (auto& e : edges)
                if (cOk[e[0]] && cOk[e[1]]) dl->AddLine(corner[e[0]], corner[e[1]], y, 1.5f);
        }
    }

    // Wireframe meshes (wireframe == true): edges only, drawn over the solids.
    for (const auto& up : objs) {
        GameObject* go = up.get();
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || !go->active || !mr->wireframe) continue;
        Mat4 m = vp * go->transform->LocalToWorldMatrix();
        ImU32 col = (go == ed.selected()) ? IM_COL32(255, 200, 0, 255) : ToColor(mr->color);
        const auto& v = mr->mesh.vertices;
        const auto& t = mr->mesh.triangles;
        for (size_t i = 0; i + 2 < t.size(); i += 3) {
            ImVec2 p0, p1, p2;
            if (toScreen(m * Vec4{v[t[i]], 1}, p0) &&
                toScreen(m * Vec4{v[t[i + 1]], 1}, p1) &&
                toScreen(m * Vec4{v[t[i + 2]], 1}, p2)) {
                dl->AddLine(p0, p1, col);
                dl->AddLine(p1, p2, col);
                dl->AddLine(p2, p0, col);
            }
        }
    }

    // Screen-space UI draws on top of the 3D view too, so UI added to a 3D
    // project (HUD, menus, buttons) is visible here and in the Game view.
    DrawUIOverlay(ed, dl, canvasPos, canvasSize, gameView);

    // Camera Preview (Unity): when a perspective Camera is selected, show what
    // it sees in a small inset in the corner of the Scene view (slot 2 so it
    // doesn't clobber the Scene/Game textures).
    if (!gameView && ed.selected()) {
        if (auto* pc = ed.selected()->GetComponent<Camera>();
            pc && pc->projection == Camera::Projection::Perspective) {
            float pw = Mathf::Clamp(canvasSize.x * 0.28f, 140.0f, 320.0f);
            float ph = pw * 9.0f / 16.0f;
            ImVec2 pmin(canvasEnd.x - pw - 10, canvasEnd.y - ph - 10);
            ImVec2 pmax(canvasEnd.x - 10, canvasEnd.y - 10);
            Color bg = pc->backgroundColor;
            dl->AddRectFilled(pmin, pmax, IM_COL32((int)(bg.r * 255), (int)(bg.g * 255),
                                                   (int)(bg.b * 255), 255));
            Mat4 cvp = pc->ProjectionMatrix(pw / ph) * pc->ViewMatrix();
            Vec3 ceye = ed.selected()->transform->Position();
            if (SDL_Texture* ptex = Render3DTexture(ed.scene(), cvp, ceye,
                                                    (int)pw, (int)ph, 2))
                dl->AddImage((ImTextureID)ptex, pmin, pmax);
            dl->AddRect(pmin, pmax, IM_COL32(255, 255, 255, 180));
            dl->AddText(ImVec2(pmin.x + 4, pmin.y - 16), IM_COL32(220, 220, 230, 255),
                        "Camera Preview");
        }
    }

    dl->PopClipRect();

    if (gameView) return;   // the Game view is non-interactive

    // ---- Terrain sculpt brush: drag on the terrain to raise it (Shift lowers).
    //      Unprojects the cursor to a world ray and intersects the terrain's
    //      ground plane, then raises heights within the brush radius. ----
    if (ed.selected() && g_terrainSculpt && hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (auto* terr = ed.selected()->GetComponent<Terrain>()) {
            float ndcx = 2.0f * (io.MousePos.x - canvasPos.x) / canvasSize.x - 1.0f;
            float ndcy = 1.0f - 2.0f * (io.MousePos.y - canvasPos.y) / canvasSize.y;
            Mat4 inv = vp.Inverse();
            Vec4 pn = inv * Vec4{ndcx, ndcy, -1.0f, 1.0f};
            Vec4 pf = inv * Vec4{ndcx, ndcy,  1.0f, 1.0f};
            Vec3 a{pn.x / pn.w, pn.y / pn.w, pn.z / pn.w};
            Vec3 b{pf.x / pf.w, pf.y / pf.w, pf.z / pf.w};
            Vec3 rdir = (b - a).Normalized();
            Vec3 op = ed.selected()->transform->Position();
            if (Mathf::Abs(rdir.y) > 1e-5f) {
                float t = (op.y - a.y) / rdir.y;
                if (t > 0.0f) {
                    Vec3 hit = a + rdir * t;
                    float delta = g_terrainStrength * io.DeltaTime * (io.KeyShift ? -1.0f : 1.0f);
                    terr->RaiseAt(hit.x - op.x, hit.z - op.z, g_terrainRadius, delta);
                    terr->Apply();
                    ed.dirty = true;
                }
            }
            g_uiHandled = true;   // consume the drag: no gizmo / pick this frame
            return;
        }
    }

    // ---- Transform gizmo: 3 colored axis handles at the selected object
    //      (Unity W/E/R). Grab a handle and drag to move/rotate/scale on that
    //      axis. X=red, Y=green, Z=blue. ----
    GameObject* sel = ed.selected();
    bool grabbedThisClick = false;
    // The transform gizmo works in Play too, so you can move/rotate/scale a
    // selection live (edits revert on Stop, just like Unity).
    if (sel) {
        Transform* t = sel->transform;
        // Cameras may be moved/rotated but not scaled (scaling a camera warps the
        // view and flickers the scene), so the Scale tool falls back to Move.
        bool noScale = sel->GetComponent<Camera>() != nullptr;
        Tool tool = (noScale && g_tool == Tool::Scale) ? Tool::Move : g_tool;
        Vec3 o = t->Position();
        float L = ed.camDist * 0.18f;                 // arm length, screen-stable
        // World axes by default; Local mode (and Scale, which is inherently local)
        // uses the object's own right/up/forward so the handles follow rotation.
        Vec3 axis[3];
        if (g_gizmoLocal || tool == Tool::Scale) {
            axis[0] = t->Right(); axis[1] = t->Up(); axis[2] = t->Forward();
        } else {
            axis[0] = {1, 0, 0}; axis[1] = {0, 1, 0}; axis[2] = {0, 0, 1};
        }
        ImU32 col[3] = {IM_COL32(230, 80, 80, 255), IM_COL32(90, 210, 100, 255),
                        IM_COL32(90, 150, 240, 255)};
        ImVec2 so; bool oOk = toScreen(vp * Vec4{o, 1}, so);
        ImVec2 tip[3]; bool tipOk[3];
        for (int i = 0; i < 3; ++i) tipOk[i] = toScreen(vp * Vec4{o + axis[i] * L, 1}, tip[i]);

        if (oOk) {
            for (int i = 0; i < 3; ++i) {
                if (!tipOk[i]) continue;
                ImU32 c = (g_gizmoGrab && g_gizmoAxis == i) ? IM_COL32(255, 230, 90, 255) : col[i];
                dl->AddLine(so, tip[i], c, 3.0f);
                if (tool == Tool::Scale)
                    dl->AddRectFilled(ImVec2(tip[i].x - 5, tip[i].y - 5),
                                      ImVec2(tip[i].x + 5, tip[i].y + 5), c);   // scale cube
                else
                    dl->AddCircleFilled(tip[i], 5.0f, c);                       // move/rotate knob
            }
            dl->AddCircleFilled(so, 3.0f, IM_COL32(230, 230, 230, 255));
        }

        // Grab the closest handle on mouse-press.
        if (hovered && oOk && !g_uiHandled && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            float best = 11.0f; int pick = -1;
            for (int i = 0; i < 3; ++i)
                if (tipOk[i]) { float d = SegDistPx(io.MousePos, so, tip[i]); if (d < best) { best = d; pick = i; } }
            if (pick >= 0) { ed.PushUndo(); g_gizmoAxis = pick; g_gizmoGrab = true; grabbedThisClick = true;
                             g_rotAccum = 0.0f; g_rotApplied = 0.0f; }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { g_gizmoGrab = false; g_gizmoAxis = -1; }

        // Drag the grabbed axis.
        if (g_gizmoGrab && g_gizmoAxis >= 0 && tipOk[g_gizmoAxis] && oOk &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            int i = g_gizmoAxis;
            ImVec2 sdir{tip[i].x - so.x, tip[i].y - so.y};
            float slen = Mathf::Sqrt(sdir.x * sdir.x + sdir.y * sdir.y);
            if (slen > 1e-3f) {
                float along = (io.MouseDelta.x * sdir.x + io.MouseDelta.y * sdir.y) / slen;
                float amt = along * (L / slen);       // screen px -> world units along axis
                if (tool == Tool::Move) {
                    t->localPosition += axis[i] * amt;
                    if (g_snap && g_snapSize > 0.0f) {
                        t->localPosition.x = Mathf::Round(t->localPosition.x / g_snapSize) * g_snapSize;
                        t->localPosition.y = Mathf::Round(t->localPosition.y / g_snapSize) * g_snapSize;
                        t->localPosition.z = Mathf::Round(t->localPosition.z / g_snapSize) * g_snapSize;
                    }
                } else if (tool == Tool::Rotate) {
                    float deg = along * 0.6f;                   // degrees about the axis
                    if (g_snap && g_rotSnapDeg > 0.0f) {
                        // 15-deg detents: apply only whole snapped steps as we cross them.
                        g_rotAccum += deg;
                        float snapped = Mathf::Round(g_rotAccum / g_rotSnapDeg) * g_rotSnapDeg;
                        float step = snapped - g_rotApplied;
                        if (step != 0.0f) { t->Rotate(axis[i] * step); g_rotApplied = snapped; }
                    } else {
                        t->Rotate(axis[i] * deg);
                    }
                } else { // Scale
                    Vec3 sc = t->localScale;
                    if (i == 0) sc.x += amt; else if (i == 1) sc.y += amt; else sc.z += amt;
                    if (g_snap && g_snapSize > 0.0f) {          // snap scale to the increment
                        sc.x = Mathf::Round(sc.x / g_snapSize) * g_snapSize;
                        sc.y = Mathf::Round(sc.y / g_snapSize) * g_snapSize;
                        sc.z = Mathf::Round(sc.z / g_snapSize) * g_snapSize;
                    }
                    sc.x = Mathf::Max(0.01f, sc.x); sc.y = Mathf::Max(0.01f, sc.y); sc.z = Mathf::Max(0.01f, sc.z);
                    t->localScale = sc;
                }
                ed.dirty = true;
            }
        }
    }

    // Click-select (skipped when a gizmo handle was just grabbed): pick the
    // nearest mesh whose projected bounding box contains the cursor.
    if (hovered && !g_uiHandled && !g_gizmoGrab && !grabbedThisClick && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        GameObject* hit = nullptr;
        float bestDepth = 1e30f;
        for (const auto& up : objs) {
            GameObject* go = up.get();
            auto* mr = go->GetComponent<MeshRenderer>();
            if (!mr || !go->active) continue;
            Mat4 model = go->transform->LocalToWorldMatrix();
            Vec3 lo, hi; mr->mesh.Bounds(lo, hi);
            float minx = 1e30f, miny = 1e30f, maxx = -1e30f, maxy = -1e30f;
            bool any = false;
            for (int ci = 0; ci < 8; ++ci) {
                Vec3 corner{(ci & 1) ? hi.x : lo.x, (ci & 2) ? hi.y : lo.y, (ci & 4) ? hi.z : lo.z};
                ImVec2 sp;
                if (!toScreen(vp * Vec4{model.MultiplyPoint(corner), 1}, sp)) continue;
                any = true;
                minx = Mathf::Min(minx, sp.x); maxx = Mathf::Max(maxx, sp.x);
                miny = Mathf::Min(miny, sp.y); maxy = Mathf::Max(maxy, sp.y);
            }
            if (!any) continue;
            if (io.MousePos.x >= minx && io.MousePos.x <= maxx &&
                io.MousePos.y >= miny && io.MousePos.y <= maxy) {
                float depth = (go->transform->Position() - eye).SqrMagnitude();
                if (depth < bestDepth) { bestDepth = depth; hit = go; }
            }
        }
        // Non-visual objects (Camera, Light, empties) have no mesh to box-pick, so
        // when nothing solid is under the cursor, select the nearest one whose
        // gizmo/origin is within a small pixel radius of the click.
        if (!hit) {
            float best = 16.0f * 16.0f;
            for (const auto& up : objs) {
                GameObject* go = up.get();
                if (!go->active || go->GetComponent<MeshRenderer>()) continue;
                ImVec2 sp;
                if (!toScreen(vp * Vec4{go->transform->Position(), 1}, sp)) continue;
                float dx = io.MousePos.x - sp.x, dy = io.MousePos.y - sp.y, d2 = dx * dx + dy * dy;
                if (d2 < best) { best = d2; hit = go; }
            }
        }
        ed.Select(hit);   // hit may be null -> clicking empty space deselects (matches 2D)
    }
}

void DrawViewport(EditorState& ed) {
    ImGui::Begin("Scene");

    // 2D / 3D toggle + frame the selection.
    if (ImGui::Button(ed.view3D ? "3D" : "2D")) ed.view3D = !ed.view3D;
    ImGui::SameLine();
    if (ImGui::Button("Frame") && ed.selected()) {
        Vec3 p = ed.selected()->transform->Position();
        ed.camTarget = p;
        ed.cameraPos = {p.x, p.y};
    }
    // Align a selected Camera to the current scene view (Unity's Align With View):
    // snap its position/orientation to the editor eye so the Game view matches.
    if (ed.view3D && ed.selected() && ed.selected()->GetComponent<Camera>()) {
        ImGui::SameLine();
        if (ImGui::Button("Align Cam to View")) {
            float yawR = ed.camYaw * Mathf::Deg2Rad, pitchR = ed.camPitch * Mathf::Deg2Rad;
            Vec3 dir{Mathf::Cos(pitchR) * Mathf::Sin(yawR), Mathf::Sin(pitchR),
                     Mathf::Cos(pitchR) * Mathf::Cos(yawR)};
            Vec3 eye = ed.camTarget + dir * ed.camDist;
            Transform* t = ed.selected()->transform;
            t->SetPosition(eye);
            t->localRotation = Quat::LookRotation((ed.camTarget - eye).Normalized(), Vec3::Up);
            ed.dirty = true;
        }
    }
    // Transform tools (W/E/R), highlighting the active one.
    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    auto toolBtn = [&](const char* lbl, Tool t) {
        bool active = g_tool == t;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.75f, 1.0f));
        if (ImGui::Button(lbl)) g_tool = t;
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    toolBtn("Move", Tool::Move);
    toolBtn("Rotate", Tool::Rotate);
    toolBtn("Scale", Tool::Scale);
    // Local/Global handle orientation (Unity's toggle); X toggles it.
    if (ImGui::Button(g_gizmoLocal ? "Local" : "Global")) g_gizmoLocal = !g_gizmoLocal;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gizmo orientation: Local (object) vs Global (world). Shortcut: X");
    // Keyboard shortcuts W/E/R when the Scene window is focused (and not typing).
    if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) g_tool = Tool::Move;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) g_tool = Tool::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) g_tool = Tool::Scale;
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) g_gizmoLocal = !g_gizmoLocal;
        // Arrow keys nudge a selected UI widget: 1px, or a grid step with Shift.
        if (ed.selected()) {
            UIRect nr = GetUIRect(ed.selected());
            if (nr.valid && nr.position) {
                float step = ImGui::GetIO().KeyShift ? (float)(g_uiGrid > 0 ? g_uiGrid : 1) : 1.0f;
                float nx = 0.0f, ny = 0.0f;
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow,  true)) nx -= step;
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) nx += step;
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow,    true)) ny -= step;
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow,  true)) ny += step;
                if (nx != 0.0f || ny != 0.0f) {
                    nr.position->x += nx; nr.position->y += ny; ed.dirty = true;
                }
            }
        }
    }
    ImGui::Checkbox("Snap", &g_snap);
    if (g_snap) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        ImGui::DragFloat("##snap", &g_snapSize, 0.05f, 0.05f, 10.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move/Scale snap increment");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        ImGui::DragFloat("Rot deg##rotsnap", &g_rotSnapDeg, 1.0f, 1.0f, 90.0f, "%.0f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotation snap increment (degrees)");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        ImGui::DragInt("UI px##uigrid", &g_uiGrid, 1, 1, 256);   // UI pixel grid
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("UI snap grid (pixels) + edge/center guides");
    }
    ImGui::SameLine();
    ImGui::TextDisabled(ed.view3D ? "drag: orbit  wheel: zoom"
                                  : "drag: move  right-drag: pan  wheel: zoom");

    ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 50) canvasSize.x = 50;
    if (canvasSize.y < 50) canvasSize.y = 50;
    ImVec2 canvasEnd(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(canvasPos, canvasEnd, IM_COL32(15, 15, 30, 255));

    ImGui::InvisibleButton("canvas", canvasSize,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    // Drag an asset from the Project panel straight onto the Scene to place it
    // (Unity-style): prefabs instantiate, scenes open, images become sprites,
    // OBJ models become 3D objects. New objects land at the camera's focus.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            std::string path((const char*)p->Data);
            std::string ext;
            if (auto d = path.find_last_of('.'); d != std::string::npos) ext = path.substr(d);
            for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
            Vec3 drop = ed.view3D ? ed.camTarget
                                  : Vec3{ed.cameraPos.x, ed.cameraPos.y, 0.0f};
            GameObject* placed = nullptr;
            if (ext == ".okayprefab") {
                ed.PushUndo();
                std::string err;
                placed = SceneSerializer::InstantiateFromFile(ed.scene(), path, &err);
                if (!placed) ConsoleLog("Prefab load failed: " + err);
            } else if (ext == ".okayscene") {
                std::string err;
                if (ed.Load(path, &err)) ConsoleLog("Opened " + path);
                else ConsoleLog("Open failed: " + err);
            } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
                ed.PushUndo();
                std::filesystem::path fp(path);
                placed = ed.CreateSprite(fp.stem().string());
                if (auto* sr = placed->GetComponent<SpriteRenderer>()) sr->texture = path;
            } else if (ext == ".obj") {
                ed.PushUndo();
                std::filesystem::path fp(path);
                placed = ed.CreateMesh("Cube");
                if (auto* mr = placed->GetComponent<MeshRenderer>()) {
                    bool ok = false;
                    Mesh m = Mesh::LoadOBJ(path, &ok);
                    if (ok && !m.vertices.empty()) { mr->mesh = m; mr->meshPath = path; }
                    placed->name = fp.stem().string();
                }
            }
            if (placed) {
                placed->transform->SetPosition(drop);
                ed.Select(placed); ed.dirty = true;
                ConsoleLog("Placed " + placed->name);
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGuiIO& io = ImGui::GetIO();
    // Treat the canvas as hovered when the pointer is over it. IsItemHovered()
    // alone can read false in some docking/overlap states (and while the canvas
    // is the active item mid-drag), which would make clicks do nothing — so also
    // accept "window hovered + mouse inside the canvas rect" as a fallback.
    bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (!hovered && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        ImVec2 mp = io.MousePos;
        if (mp.x >= canvasPos.x && mp.y >= canvasPos.y &&
            mp.x <= canvasEnd.x && mp.y <= canvasEnd.y)
            hovered = true;
    }

    // UI editing (pick/drag screen-space widgets) runs first and may consume the
    // click so the world pickers below leave the selection alone.
    EditUIWidgets(ed, canvasPos, canvasSize, hovered, io);

    if (ed.view3D) DrawScene3D(ed, dl, canvasPos, canvasSize, canvasEnd, hovered, io);
    else           DrawScene2D(ed, dl, canvasPos, canvasSize, canvasEnd, hovered, io);

    // Corner overlay (view mode, object count, live FPS, selection).
    char overlay[160];
    std::snprintf(overlay, sizeof(overlay), "%s  |  %d objects  |  %.0f FPS%s%s",
                  ed.view3D ? "3D" : "2D", (int)ed.scene().Objects().size(),
                  ImGui::GetIO().Framerate,
                  ed.selected() ? "  |  " : "",
                  ed.selected() ? ed.selected()->name.c_str() : "");
    dl->AddText(ImVec2(canvasPos.x + 8, canvasPos.y + 6), IM_COL32(200, 200, 210, 255), overlay);

    ImGui::End();
}

// Unity-style "Game" view: renders the scene through its main camera with no
// editor chrome (grid, gizmos, selection) — what the built game shows. 2D or 3D
// follows the camera's projection. Read-only; press Play to make it live.
void DrawGameView(EditorState& ed) {
    // Pressing Play focuses this window so it comes forward if docked as a tab
    // behind the Scene view. SetWindowFocus() must be called before Begin().
    if (g_focusGameOnPlay) { ImGui::SetNextWindowFocus(); g_focusGameOnPlay = false; }
    if (!ImGui::Begin("Game", &g_showGame)) { ImGui::End(); return; }

    Camera* mc = SceneCamera(ed.scene());
    bool persp = mc && mc->projection == Camera::Projection::Perspective;

    // Aspect-ratio selector (Unity's Game-view "Free / 16:9 / ..." dropdown).
    static int s_aspect = 0;
    static const char* kAspectNames[] = {"Free", "16:9", "9:16", "4:3", "1:1"};
    static const float kAspectVals[]  = {0.0f, 16.0f / 9, 9.0f / 16, 4.0f / 3, 1.0f};
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("Aspect", &s_aspect, kAspectNames, IM_ARRAYSIZE(kAspectNames));
    ImGui::SameLine();
    ImGui::TextDisabled(ed.isPlaying() ? "live" : "press Play to run");

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 50) avail.x = 50;
    if (avail.y < 50) avail.y = 50;
    ImVec2 origin = ImGui::GetCursorScreenPos();

    // Letterbox a fixed-aspect rect centered in the available region.
    ImVec2 canvasSize = avail, canvasPos = origin;
    if (kAspectVals[s_aspect] > 0.0f) {
        float target = kAspectVals[s_aspect];
        float boxW = avail.x, boxH = avail.x / target;
        if (boxH > avail.y) { boxH = avail.y; boxW = avail.y * target; }
        canvasSize = {boxW, boxH};
        canvasPos = {origin.x + (avail.x - boxW) * 0.5f, origin.y + (avail.y - boxH) * 0.5f};
    }
    ImVec2 canvasEnd(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);

    // The Game view is the surface the running game is shown on, so feed mouse
    // input relative to it (see the main loop).
    g_playCanvasPos = canvasPos;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Dark letterbox bars over the whole region, then the camera background.
    dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(8, 8, 10, 255));
    Color bg = mc ? mc->backgroundColor : Color::Black;
    dl->AddRectFilled(canvasPos, canvasEnd,
                      IM_COL32((int)(bg.r * 255), (int)(bg.g * 255), (int)(bg.b * 255), 255));

    ImGui::InvisibleButton("gamecanvas", avail);
    ImGuiIO& io = ImGui::GetIO();
    // Publish the canvas size so anchored UI resolves to this view.
    UICanvas::Set(canvasSize.x, canvasSize.y);

    if (!mc) {
        dl->AddText(ImVec2(canvasPos.x + 10, canvasPos.y + 10),
                    IM_COL32(200, 120, 120, 255), "No main Camera in the scene.");
    } else if (persp) {
        DrawScene3D(ed, dl, canvasPos, canvasSize, canvasEnd, false, io, /*gameView*/ true);
    } else {
        DrawScene2D(ed, dl, canvasPos, canvasSize, canvasEnd, false, io, /*gameView*/ true);
    }

    const char* tag = ed.isPlaying() ? "PLAYING" : "Game (press Play)";
    dl->AddText(ImVec2(canvasPos.x + 8, canvasPos.y + 6),
                ed.isPlaying() ? IM_COL32(120, 230, 140, 255) : IM_COL32(180, 180, 190, 255), tag);
    ImGui::End();
}

} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--selftest") return RunSelfTest();

    SDL_SetMainReady(); // we manage the entry point (SDL_MAIN_HANDLED)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO |
                 SDL_INIT_GAMECONTROLLER) != 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    // Open the first connected game controller (optional) so the playable
    // templates can be driven with a gamepad inside the editor's Play mode.
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) { g_pad = SDL_GameControllerOpen(i); if (g_pad) break; }

    // Open a mono float audio device (queue mode); audio is optional.
    SDL_AudioSpec want{}, have{};
    want.freq = 44100; want.format = AUDIO_F32SYS; want.channels = 1; want.samples = 1024;
    SDL_AudioDeviceID audioDev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audioDev) SDL_PauseAudioDevice(audioDev, 0);

    SDL_Window* window = SDL_CreateWindow(
        "OkaySpace Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 800, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) { std::cerr << "CreateWindow failed: " << SDL_GetError() << "\n"; return 1; }
    okay::SetAppIcon(window);   // placeholder OkaySpace logo

    // SDL's 2D renderer (Direct3D/Metal/OpenGL, chosen by SDL); fall back to software.
    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) { std::cerr << "CreateRenderer failed: " << SDL_GetError() << "\n"; return 1; }
    g_sdlRenderer = renderer; // used by the z-buffered 3D view to make textures

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    LoadProjectSettings();   // project.okayproj defaults (company/version/gravity/...)
    ApplyTheme();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    // Start empty; the New Project chooser pops up on launch (2D / 3D / Empty).
    EditorState ed;
    LoadRecent();

    // Route engine logs (and script print/log/debug output) into the Console
    // with the matching severity (Unity-style info/warning/error).
    Log::sink = [](Log::Level lvl, const std::string& msg) {
        int level = lvl == Log::Level::Error ? 2 : lvl == Log::Level::Warning ? 1 : 0;
        ConsoleLog(msg, level);
    };

    ConsoleLog("Welcome to OkaySpace v" OKAY_ENGINE_VERSION
               ". Choose a 2D or 3D project to begin.");

    bool running = true;
    std::string lastTitle;
    Uint64 last = SDL_GetPerformanceCounter();
    while (running) {
        Input::ClearTypedText();   // collect this frame's typed characters
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) g_quitRequested = true;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE &&
                e.window.windowID == SDL_GetWindowID(window))
                g_quitRequested = true;
            // Route typed characters to the playing game's UI input fields (but not
            // while an ImGui field — inspector/markup box — is capturing text).
            if (e.type == SDL_TEXTINPUT && ed.isPlaying() && !ImGui::GetIO().WantTextInput)
                Input::FeedText(e.text.text);
            if (e.type == SDL_MOUSEWHEEL && ed.isPlaying())
                Input::FeedMouseWheel((float)e.wheel.y);
        }

        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((now - last) / (double)SDL_GetPerformanceFrequency());
        last = now;

        // While Play is active, feed real keyboard/mouse/gamepad into the engine
        // Input so scripts and the playable templates respond exactly like they do
        // in the standalone player ("can't move" was because the editor never fed
        // input during Play). Gated on !WantTextInput so typing in a field (script
        // editor, name box) doesn't also drive the game.
        if (ed.isPlaying() && !io.WantTextInput) {
            const Uint8* ks = SDL_GetKeyboardState(nullptr);
            std::vector<char> down;
            for (char c = 'a'; c <= 'z'; ++c)
                if (ks[SDL_GetScancodeFromKey(c)]) down.push_back(c);
            for (char c = '0'; c <= '9'; ++c)
                if (ks[SDL_GetScancodeFromKey(c)]) down.push_back(c);
            if (ks[SDL_SCANCODE_SPACE]) down.push_back(' ');
            if (ks[SDL_SCANCODE_BACKSPACE]) down.push_back((char)8); // text editing
            if (ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER]) down.push_back('\r');
            if (ks[SDL_SCANCODE_ESCAPE]) down.push_back((char)27);
            if (ks[SDL_SCANCODE_UP])    down.push_back('w'); // arrows -> WASD
            if (ks[SDL_SCANCODE_LEFT])  down.push_back('a');
            if (ks[SDL_SCANCODE_DOWN])  down.push_back('s');
            if (ks[SDL_SCANCODE_RIGHT]) down.push_back('d');

            Vec2 padAxis; unsigned padMask = 0;
            if (g_pad) {
                float lx = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f;
                float ly = -SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f;
                if (lx > -0.18f && lx < 0.18f) lx = 0.0f;
                if (ly > -0.18f && ly < 0.18f) ly = 0.0f;
                padAxis = {lx, ly};
                auto bit = [&](SDL_GameControllerButton b, int id) {
                    if (SDL_GameControllerGetButton(g_pad, b)) padMask |= 1u << id;
                };
                bit(SDL_CONTROLLER_BUTTON_A, 0); bit(SDL_CONTROLLER_BUTTON_B, 1);
                bit(SDL_CONTROLLER_BUTTON_X, 2); bit(SDL_CONTROLLER_BUTTON_Y, 3);
                if (lx > 0.4f) down.push_back('d');
                if (lx < -0.4f) down.push_back('a');
                if (ly > 0.4f) down.push_back('w');
                if (ly < -0.4f) down.push_back('s');
                if (padMask & 1u) down.push_back(' ');
            }
            Input::FeedKeys(down);
            Input::FeedGamepad(padAxis, padMask);

            // Mouse, relative to the Game/Scene canvas so UI buttons hit-test
            // against the same coordinates the preview draws with.
            ImVec2 mp = ImGui::GetMousePos();
            float rx = mp.x - g_playCanvasPos.x, ry = mp.y - g_playCanvasPos.y;
            unsigned mask = 0;
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))   mask |= 1u << 0;
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right))  mask |= 1u << 1;
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) mask |= 1u << 2;
            Input::FeedMouse(Vec2{rx, ry}, mask);
        } else if (!ed.isPlaying()) {
            Input::FeedKeys({}); // release everything in edit mode
        }

        if (!g_paused) ed.Tick(dt);   // Pause freezes the sim (Step advances it)
        ed.TickServices(dt); // Steam callbacks + networking every frame

        // Autosave the open scene every 60s while editing (never mid-Play).
        if (g_autosave && !ed.isPlaying() && ed.dirty && !ed.path().empty()) {
            double t = SDL_GetTicks64() / 1000.0;
            if (t - g_lastAutosave > 60.0) {
                g_lastAutosave = t;
                if (ed.Save(ed.path())) ConsoleLog("Autosaved " + ed.path());
            }
        }

        // Pump audio while playing (mono float, queued).
        if (audioDev) {
            if (ed.isPlaying()) {
                int n = (int)(dt * 44100.0f);
                if (n > 8192) n = 8192;
                if (n > 0) {
                    // Listener = the scene's main camera (for 3D/spatial sources).
                    if (Camera* mc = SceneCamera(ed.scene()))
                        AudioMixer::SetListener(mc->gameObject->transform->Position());
                    std::vector<float> ab(n);
                    AudioMixer::Render(ed.scene(), ab.data(), n);
                    SDL_QueueAudio(audioDev, ab.data(), (Uint32)(n * sizeof(float)));
                }
            } else {
                SDL_ClearQueuedAudio(audioDev);
            }
        }

        // Keep the window title in sync with the scene + dirty state.
        std::string title = "OkaySpace Editor  -  " + ed.scene().Name() +
                            (ed.dirty ? " *" : "") + "   [v" OKAY_ENGINE_VERSION "]";
        if (title != lastTitle) { SDL_SetWindowTitle(window, title.c_str()); lastTitle = title; }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        HandleShortcuts(ed);
        DrawDockSpace(ed);
        DrawNewProjectPopup(ed);
        DrawFileDialogs(ed);
        DrawQuitPrompt(ed, running);
        DrawUpdatePopup();
        DrawAboutPopup();
        DrawProjectSettings(ed);
        if (g_showHierarchy) DrawHierarchy(ed);
        DrawViewport(ed);   // the "Scene" panel (always shown)
        DrawDataAssetEditor();
        DrawMaterialEditor(ed);
        if (g_showGame)      DrawGameView(ed);
        if (g_showInspector) DrawInspector(ed);
        if (g_showConsole)   DrawConsole();
        if (g_showProject)   DrawProject(ed);
        if (g_showServices)  DrawServices(ed);
        if (g_showScriptEditor) DrawScriptEditor(ed);
        DrawScriptDocs();
        if (g_showStats)     DrawStats(ed);
        if (g_showScenes)    DrawScenes(ed);

        // Play-mode tint: a colored border around the whole window so it's
        // obvious the game is running (green) or paused (amber) — like Unity.
        if (ed.isPlaying()) {
            ImGuiViewport* mv = ImGui::GetMainViewport();
            ImU32 c = g_paused ? IM_COL32(220, 180, 60, 200) : IM_COL32(70, 200, 110, 200);
            ImGui::GetForegroundDrawList()->AddRect(mv->WorkPos,
                ImVec2(mv->WorkPos.x + mv->WorkSize.x, mv->WorkPos.y + mv->WorkSize.y),
                c, 0.0f, 0, 3.0f);
        }

        ImGui::Render();
        SDL_RenderSetScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 30, 30, 34, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        if (updater::pendingQuit) running = false; // auto-update relaunched
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (g_pad) SDL_GameControllerClose(g_pad);
    if (audioDev) SDL_CloseAudioDevice(audioDev);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
#endif // OKAY_EDITOR_HEADLESS
