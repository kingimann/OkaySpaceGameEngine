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
#include <atomic>
#include <chrono>
#include <future>
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
#  include <csignal>
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
std::vector<std::uint32_t> g_view3DDown[kView3DSlots];   // AA downsample buffers
int g_ssaa = 2;   // 3D anti-aliasing: 1 = off (FXAA still on), 2 = 2x supersample.
                  // ON by default: removes the faint edge shimmer that remains during
                  // camera motion after the depth (W-buffer) fix. Auto-performance drops
                  // it to 1x on heavy scenes / large viewports to protect FPS.
float g_renderScale = 1.0f;  // 3D view render resolution (1.0 = native; lower = faster, softer)
bool g_autoPerf = true;  // auto-drop supersampling when the scene gets heavy
bool g_autoUpdate = false; // auto-install a newer build on startup (persisted)

// Sum the triangles of all active, visible solid meshes — a cheap proxy for how
// expensive this frame is to rasterize (used to auto-scale anti-aliasing).
static long SceneTriangleLoad(const Scene& scene) {
    long n = 0;
    for (const auto& go : scene.Objects()) {
        if (!go->active) continue;
        auto* mr = go->GetComponent<MeshRenderer>();
        if (mr && !mr->wireframe) n += mr->mesh.TriangleCount();
    }
    return n;
}

// Render the scene's solid meshes (z-buffered) at w*h into the slot's texture;
// transparent where nothing is drawn (so a grid/background shows through).
SDL_Texture* Render3DTexture(const Scene& scene, const Mat4& vp, const Vec3& eye,
                             int w, int h, int slot = 0, const GameObject* ignore = nullptr) {
    if (!g_sdlRenderer) return nullptr;
    if (slot < 0 || slot >= kView3DSlots) slot = 0;
    w = w < 1 ? 1 : (w > 4096 ? 4096 : w);
    h = h < 1 ? 1 : (h > 4096 ? 4096 : h);
    ApplySceneLight(scene);                      // a Light object aims the shading
    // Render at a fraction of the panel resolution when render scale < 1: the
    // texture is drawn STRETCHED to the panel, so a smaller buffer upscales for
    // free (linear filtered) — a near-linear FPS win for the software renderer.
    float scale = g_renderScale < 0.25f ? 0.25f : (g_renderScale > 1.0f ? 1.0f : g_renderScale);
    int ss = g_ssaa;
    int rw = (int)(w * scale); if (rw < 1) rw = 1;
    int rh = (int)(h * scale); if (rh < 1) rh = 1;
    // Auto-performance: drop supersampling on heavy scenes OR very large viewports
    // (the supersampled buffer is rw*rh*ss*ss pixels — keep that within a budget so
    // 2x AA never tanks FPS on a big window).
    if (g_autoPerf && ss > 1 &&
        ((long)rw * rh * ss * ss > 5'000'000L || SceneTriangleLoad(scene) > 11000))
        ss = 1;
    const std::uint32_t* px = RenderMeshesSS(g_view3DRaster[slot], g_view3DDown[slot],
                                             scene, vp, eye, rw, rh, ss, ignore);
    if (!g_view3DTex[slot] || g_view3DW[slot] != rw || g_view3DH[slot] != rh) {
        if (g_view3DTex[slot]) SDL_DestroyTexture(g_view3DTex[slot]);
        g_view3DTex[slot] = SDL_CreateTexture(g_sdlRenderer, SDL_PIXELFORMAT_ABGR8888,
                                              SDL_TEXTUREACCESS_STREAMING, rw, rh);
        SDL_SetTextureBlendMode(g_view3DTex[slot], SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(g_view3DTex[slot], SDL_ScaleModeLinear);  // smooth upscale
        g_view3DW[slot] = rw; g_view3DH[slot] = rh;
    }
    SDL_UpdateTexture(g_view3DTex[slot], nullptr, px, rw * 4);
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
// Repo raw root (a commit SHA or "main" is inserted between this and /dist/...).
const char* kRawRepo =
    "https://raw.githubusercontent.com/kingimann/OkaySpaceGameEngine/";
// GitHub API endpoint for the latest commit on main (to pin downloads by SHA).
const char* kApiCommit =
    "https://api.github.com/repos/kingimann/OkaySpaceGameEngine/commits/main";

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
    // No-cache headers + a User-Agent (the GitHub API rejects requests without one).
    const char* noCache = "-H \"Cache-Control: no-cache\" -H \"Pragma: no-cache\" -A \"OkaySpace-Updater\" ";
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

// The SHA of the latest commit on main, or "main" if the API can't be reached.
// Downloading from a commit-pinned raw URL is IMMUTABLE — it can never be served
// stale by the CDN, which is the real reason "update" sometimes kept the old
// version (the branch URL handed back a cached old binary).
std::string LatestRef() {
    std::error_code ec;
    fs::path jf = fs::temp_directory_path(ec) / "okayspace_commit.json";
    if (!Download(kApiCommit, jf.string())) return "main";
    std::ifstream f(jf); std::stringstream ss; ss << f.rdbuf();
    std::string s = ss.str(); fs::remove(jf, ec);
    auto k = s.find("\"sha\"");                       // first sha = the commit's
    if (k == std::string::npos) return "main";
    auto colon = s.find(':', k);
    auto q1 = colon == std::string::npos ? std::string::npos : s.find('"', colon);
    auto q2 = q1 == std::string::npos ? std::string::npos : s.find('"', q1 + 1);
    if (q2 == std::string::npos) return "main";
    std::string sha = s.substr(q1 + 1, q2 - q1 - 1);
    return sha.size() >= 7 ? sha : "main";
}

// A commit-pinned raw URL for a file in dist/ (ref is a SHA or "main").
std::string RawUrl(const std::string& ref, const std::string& name) {
    return std::string(kRawRepo) + ref + "/dist/" + name;
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
    std::string ref = "main";   // commit the check resolved to (install uses the same)
};

// Query GitHub for the published version + release notes. No files are swapped;
// this only reports what's available (the install step is explicit).
UpdateInfo CheckLatest() {
    UpdateInfo info;
    info.current = OKAY_ENGINE_VERSION;
    info.checked = true;
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec);
    info.ref = LatestRef();                       // pin to the latest commit
    fs::path vf = tmp / "okayspace_version.txt";
    if (!Download(RawUrl(info.ref, "VERSION.txt"), vf.string())) {
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
    if (Download(RawUrl(info.ref, "CHANGELOG.txt"), cf.string())) {
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
std::string InstallUpdate(const std::string& latest, const std::string& ref = "main") {
    std::error_code ec;
    fs::path self = SelfPath();
    if (self.empty()) return "Couldn't locate the running executable.";
    fs::path newFile = self; newFile += ".new";
    // Download the new build from the commit-pinned URL so the CDN can never hand
    // back the old binary (the cause of "it updated but stayed the same version").
    if (!Download(RawUrl(ref, "OkaySpaceEngine.exe"), newFile.string()))
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
#if defined(_WIN32)
    // A running .exe can't always be renamed in place (AV / file locks), which is
    // why a swap could silently fail. Instead hand the swap to a tiny batch helper
    // that waits for THIS process to exit (the move retries until the lock frees),
    // overwrites the exe, and relaunches it.
    fs::path bat = self.parent_path() / "okay_update.bat";
    std::string sp = self.string(), np = newFile.string();
    {
        std::ofstream b(bat, std::ios::binary);
        b << "@echo off\r\n"
          << ":retry\r\n"
          << "move /y \"" << np << "\" \"" << sp << "\" >nul 2>&1\r\n"
          << "if errorlevel 1 ( ping -n 2 127.0.0.1 >nul & goto retry )\r\n"
          << "start \"\" \"" << sp << "\"\r\n"
          << "del \"%~f0\"\r\n";
    }
    std::system(("start \"\" /min cmd /c \"" + bat.string() + "\"").c_str());
    pendingQuit = true;   // exit so the helper can replace the exe
    return "Updating to v" + latest + "... reopening.";
#else
    fs::path backup = self; backup += ".old";
    fs::remove(backup, ec);
    fs::rename(self, backup, ec);
    if (ec) { fs::remove(newFile, ec); return "Couldn't replace the app: " + ec.message(); }
    fs::rename(newFile, self, ec);
    if (ec) { fs::rename(backup, self, ec); return "Couldn't install the update: " + ec.message(); }
    fs::permissions(self, fs::perms::owner_exec | fs::perms::group_exec |
                    fs::perms::others_exec, fs::perm_options::add, ec);
    Relaunch(self.string());
    pendingQuit = true;
    return "Updated to v" + latest + "! Reopening...";
#endif
}

} // namespace updater

std::string g_updateStatus;
bool g_openUpdatePopup = false;
updater::UpdateInfo g_update;     // last update check result
bool g_installingUpdate = false;  // set while InstallUpdate runs
std::future<updater::UpdateInfo> g_updateCheck;  // async startup check (non-blocking)
bool g_autoCheckDone = false;     // consumed the async result yet?
bool g_openAbout = false;
bool g_showNewProject = true;   // show the project chooser on launch

// Panel visibility (View menu).
bool g_showHierarchy = true, g_showInspector = true, g_showConsole = true,
     g_showProject = true, g_showServices = true, g_showScriptEditor = true;
bool g_showGame = true;   // Unity-style Game view (main-camera render)
bool g_focusGameOnPlay = false;  // pressing Play brings the Game tab forward
bool g_showScriptDocs = false;   // OkayScript reference window
bool g_showColliders = true;     // draw collider wireframes in the Scene view
bool g_showGizmos = true;        // draw selection outlines + camera/light gizmos in the Scene view
bool g_showGrid = true;          // draw the XZ ground grid in the Scene view
bool g_sceneSkybox = true;       // draw the sky gradient in the Scene view (Game view always uses the camera)
bool g_wireframeAll = false;     // debug: draw every mesh as wireframe (Scene view)
bool g_showWorldAxes = false;    // debug: draw the world X/Y/Z axes at the origin
bool g_showCamHud = true;        // debug: on-screen readout of Scene camera zoom/angle/position
float g_editorFov = 50.0f;       // Scene-view camera vertical FOV (degrees)
float g_editorNear = 0.3f;       // Scene-view camera near clip (Unity-like). Adjustable
                                 // in Stats > Rendering > Debug view.
// Debug capture: stash the last Scene-view camera so a button can re-render the
// exact frame straight to a PNG (the true engine output, bypassing the display).
Mat4 g_lastSceneVP; Vec3 g_lastSceneEye{0,0,0};
int  g_lastSceneW = 0, g_lastSceneH = 0;
bool g_captureScene = false;
bool g_resetLayout = false;      // request a dock-layout rebuild next frame
bool g_paused = false;           // pause the simulation while staying in Play
bool g_clearConsoleOnPlay = true; // wipe the console each time Play starts
int  g_theme = 0;                // 0 = Dark, 1 = Light, 2 = Classic
float g_uiScale = 1.00f;         // global UI scale (1.0 keeps the font crisp)
bool g_autosave = true;          // periodically write a crash-recovery sidecar
double g_lastAutosave = 0.0;     // seconds since last autosave
float  g_autosaveInterval = 120.0f;  // seconds between autosaves
double g_autosaveStamp = 0.0;    // time of the last successful autosave (for UI)
std::string g_recoverPath;       // a newer ".autosave" found on load (recovery prompt)
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

// Persisted editor preferences (a tiny key/value file beside the working dir).
void LoadSettings() {
    std::ifstream f("okay_settings.txt");
    std::string k; int v;
    while (f >> k >> v) {
        if (k == "autoupdate") g_autoUpdate = (v != 0);
        else if (k == "autoperf") g_autoPerf = (v != 0);
        else if (k == "ssaa") g_ssaa = v < 1 ? 1 : (v > 2 ? 2 : v);
        else if (k == "renderscalepct") g_renderScale = (v < 25 ? 25 : (v > 100 ? 100 : v)) / 100.0f;
        else if (k == "showgizmos") g_showGizmos = (v != 0);
        else if (k == "showgrid") g_showGrid = (v != 0);
        else if (k == "sceneskybox") g_sceneSkybox = (v != 0);
        else if (k == "wireframeall") g_wireframeAll = (v != 0);
        else if (k == "showworldaxes") g_showWorldAxes = (v != 0);
        else if (k == "showcamhud") g_showCamHud = (v != 0);
        else if (k == "editorfovx10") g_editorFov = (v < 200 ? 200 : (v > 1100 ? 1100 : v)) / 10.0f;
        else if (k == "editornearx100") g_editorNear = (v < 1 ? 1 : (v > 5000 ? 5000 : v)) / 100.0f;
    }
}
void SaveSettings() {
    std::ofstream f("okay_settings.txt");
    f << "autoupdate " << (g_autoUpdate ? 1 : 0) << "\n"
      << "autoperf "   << (g_autoPerf ? 1 : 0) << "\n"
      << "ssaa "       << g_ssaa << "\n"
      << "renderscalepct " << (int)(g_renderScale * 100 + 0.5f) << "\n"
      << "showgizmos " << (g_showGizmos ? 1 : 0) << "\n"
      << "showgrid " << (g_showGrid ? 1 : 0) << "\n"
      << "sceneskybox " << (g_sceneSkybox ? 1 : 0) << "\n"
      << "wireframeall " << (g_wireframeAll ? 1 : 0) << "\n"
      << "showworldaxes " << (g_showWorldAxes ? 1 : 0) << "\n"
      << "showcamhud " << (g_showCamHud ? 1 : 0) << "\n"
      << "editorfovx10 " << (int)(g_editorFov * 10 + 0.5f) << "\n"
      << "editornearx100 " << (int)(g_editorNear * 100 + 0.5f) << "\n";
}

// Save the open scene so an auto-update relaunch never loses work. Uses the
// current path when known, otherwise a recovery file (added to Recent).
void ConsoleLog(const std::string& msg, int level);   // (defined below)
void SaveAllBeforeExit(EditorState& ed) {
    std::string p = ed.path().empty() ? "autosaved_scene.okayscene" : ed.path();
    if (ed.Save(p)) { AddRecent(p); ConsoleLog("Saved before updating: " + p, 0); }
    ed.dirty = false;
}

// File dialogs.
bool g_showSaveAs = false, g_showOpen = false, g_showImportObj = false;
char g_objPathBuf[256] = "model.obj";
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
bool  g_showSaveManager = false;   // browse/edit .okaysave files
// Script Editor tabs: one per open ScriptComponent (an object can have several).
std::vector<okay::ScriptComponent*> g_scriptTabs;
okay::ScriptComponent* g_activeScriptTab = nullptr;
okay::ScriptComponent* g_focusScriptTab = nullptr;   // request to focus a tab this frame
// Project scripts opened for editing on their own (not attached to a GameObject).
// Owned here so their Script Editor tabs persist; freed when the tab is closed.
std::vector<std::unique_ptr<okay::ScriptComponent>> g_looseScripts;
static bool IsLooseScript(okay::ScriptComponent* s) {
    for (auto& up : g_looseScripts) if (up.get() == s) return true;
    return false;
}
// Open a Project script file in the in-app Script Editor as a standalone,
// file-backed tab (focuses an existing tab if it's already open).
static void OpenScriptFileInEditor(const std::string& path) {
    for (auto& up : g_looseScripts)
        if (up->Path() == path) {                       // already open -> focus it
            if (std::find(g_scriptTabs.begin(), g_scriptTabs.end(), up.get()) == g_scriptTabs.end())
                g_scriptTabs.push_back(up.get());
            g_activeScriptTab = up.get(); g_focusScriptTab = up.get();
            g_showScriptEditor = true; return;
        }
    std::string ext;
    if (auto d = path.find_last_of('.'); d != std::string::npos) ext = path.substr(d);
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    std::string lang = (ext == ".lua") ? "lua" : (ext == ".cs") ? "cs" : "okayscript";
    auto sc = std::make_unique<okay::ScriptComponent>(lang);
    std::string err; sc->LoadFile(path, &err); sc->SetPath(path);
    okay::ScriptComponent* raw = sc.get();
    g_looseScripts.push_back(std::move(sc));
    g_scriptTabs.push_back(raw);
    g_activeScriptTab = raw; g_focusScriptTab = raw;
    g_showScriptEditor = true;
}
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
int   g_gizmoAxis = -1;     // axis being dragged: 0=X 1=Y 2=Z, 3=view, -1 = none
bool  g_gizmoGrab = false;  // true while a gizmo handle is held
int   g_gizmoHover = -1;    // axis the mouse is hovering this frame (for highlight)
float g_rotAccum = 0.0f;    // raw degrees this rotate-drag (for 15-deg snap detents)
float g_rotApplied = 0.0f;  // snapped degrees already applied this drag
float g_rotTotal = 0.0f;    // signed degrees turned this drag (HUD readout)
float g_rotStartAng = 0.0f; // ring angle (rad) where this rotate-drag began
float g_rotSnapDeg = 15.0f; // rotation snap increment when Snap is on
bool  g_gizmoLocal = false; // gizmo axes in the object's local space (Unity's Local/Global)
bool  g_terrainSculpt = false; // terrain brush active in the 3D scene view
float g_terrainRadius = 6.0f;
float g_terrainStrength = 4.0f;
int   g_terrainBrush = 0;      // 0 Raise/Lower, 1 Smooth, 2 Flatten, 3 Set Height
float g_terrainFlattenH = 0.0f;// target height for the Flatten/Set-Height brush
int   g_terrainGenType = 0;    // 0 Mountains,1 Hills,2 Plains,3 Plateau,4 Islands
float g_terrainGenAmp = 12.0f; // generation amplitude (peak height)
float g_terrainGenFreq = 3.0f; // generation frequency (feature density)
int   g_terrainGenOct = 5;     // generation octaves (detail)
int   g_scatterProp = 0;       // 0 Tree,1 Pine,2 Rock,3 Bush
int   g_scatterCount = 80;     // how many props to scatter per pass
float g_scatterMinScale = 0.7f, g_scatterMaxScale = 1.5f;
float g_scatterMaxSlope = 0.6f;// skip faces steeper than this (0 flat..1 vertical)
float g_scatterMinH = -100.0f, g_scatterMaxH = 100.0f; // local height band to fill
GameObject* g_uiDragTarget = nullptr; // UI widget being dragged in the viewport
bool  g_uiHandled = false;  // a UI widget consumed this frame's click/drag
int   g_uiResizeHandle = -1; // 0..7 resize handle being dragged, -1 = moving
int   g_spriteHandle = -1;   // 0..7 sprite resize handle being dragged in the 2D view

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
// Peek the live edit buffer for a key (e.g. a ScriptComponent) without creating
// one — lets the inspector read the code being typed, not just the last compile.
const char* PeekCodeBuffer(void* key) {
    auto it = g_codeBuf.find(key);
    return it == g_codeBuf.end() ? nullptr : it->second.data();
}
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
        if (ImGui::MenuItem("Import Model (.obj)...")) g_showImportObj = true;
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
            if (ed.Save(p)) { ConsoleLog("Saved " + p); AddRecent(p); ed.Achievement("FIRST_SAVE");
                std::error_code rc; std::filesystem::remove(p + ".autosave", rc); }
            else ConsoleLog("Save failed");
        }
        if (ImGui::MenuItem("Save As...")) {
            std::strncpy(g_pathBuf, ed.path().empty() ? "scene.okayscene" : ed.path().c_str(),
                         sizeof(g_pathBuf) - 1);
            g_showSaveAs = true;
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Autosave")) {
            ImGui::MenuItem("Enabled", nullptr, &g_autosave);
            ImGui::SetNextItemWidth(150);
            ImGui::SliderFloat("Interval (s)", &g_autosaveInterval, 30.0f, 600.0f, "%.0f");
            if (g_autosaveStamp > 0.0)
                ImGui::TextDisabled("Last autosave: %.0fs ago",
                                    SDL_GetTicks64() / 1000.0 - g_autosaveStamp);
            ImGui::TextDisabled("Writes a <scene>.autosave recovery copy;");
            ImGui::TextDisabled("offered on next open if it's newer than the scene.");
            ImGui::EndMenu();
        }
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
        ImGui::MenuItem("Save Manager", nullptr, &g_showSaveManager);
        ImGui::MenuItem("Scenes", nullptr, &g_showScenes);
        ImGui::Separator();
        bool aa = g_ssaa > 1;
        if (ImGui::MenuItem("3D Anti-aliasing (2x)", nullptr, &aa)) { g_ssaa = aa ? 2 : 1; SaveSettings(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Smoother edges; turn OFF to boost FPS.");
        if (ImGui::MenuItem("Auto performance", nullptr, &g_autoPerf)) SaveSettings();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Automatically drop anti-aliasing when the scene\nhas many models, to keep the editor smooth.");
        ImGui::Separator();
        ImGui::MenuItem("Colliders (gizmos)", nullptr, &g_showColliders);
        if (ImGui::MenuItem("Gizmos (selection / camera / lights)", nullptr, &g_showGizmos)) SaveSettings();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Editor-only overlays in the Scene view\n(yellow selection box, camera frustum, light icons).\nNever appear in the Game view or a build.");
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
        if (ImGui::MenuItem("Create Virtual Camera")) {
            GameObject* g = ed.CreateEmpty("Virtual Camera");
            g->AddComponent<VirtualCamera>();
            ed.Select(g); ConsoleLog("Created Virtual Camera"); created = true;
        }
        ImGui::Separator();
        if (ImGui::BeginMenu("Player")) {           // ready-to-play premade players
            if (ImGui::MenuItem("Third Person")) {
                ed.PushUndo();
                ed.Select(okay::Templates::AddThirdPersonPlayer(ed.scene()));
                ConsoleLog("Added Third-Person player"); ed.dirty = true; created = true;
            }
            if (ImGui::MenuItem("First Person")) {
                ed.PushUndo();
                ed.Select(okay::Templates::AddFirstPersonPlayer(ed.scene()));
                ConsoleLog("Added First-Person player"); ed.dirty = true; created = true;
            }
            if (ImGui::MenuItem("Top Down")) {
                ed.PushUndo();
                ed.Select(okay::Templates::AddTopDownPlayer(ed.scene()));
                ConsoleLog("Added Top-Down player"); ed.dirty = true; created = true;
            }
            if (ImGui::MenuItem("Click To Move")) {
                ed.PushUndo();
                ed.Select(okay::Templates::AddClickToMovePlayer(ed.scene()));
                ConsoleLog("Added Click-To-Move player"); ed.dirty = true; created = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drops a fully set-up player (Character + controller + physics) into this scene.");
            ImGui::EndMenu();
        }
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
            if (ImGui::MenuItem("Tree"))      { ed.CreateMesh("Tree");      ConsoleLog("Created Tree"); created = true; }
            if (ImGui::MenuItem("Pine"))      { ed.CreateMesh("Pine");      ConsoleLog("Created Pine"); created = true; }
            if (ImGui::MenuItem("Rock"))      { ed.CreateMesh("Rock");      ConsoleLog("Created Rock"); created = true; }
            if (ImGui::MenuItem("Bush"))      { ed.CreateMesh("Bush");      ConsoleLog("Created Bush"); created = true; }
            ImGui::Separator();
            if (ImGui::MenuItem("Terrain")) {
                GameObject* go = ed.CreateEmpty("Terrain");
                auto* tr = go->AddComponent<Terrain>();
                tr->Resize(64); tr->size = 80.0f;
                tr->Generate(1, 10.0f, 3.0f, 5, (unsigned)(ImGui::GetTime() * 1000.0)); // rolling hills
                tr->Apply();
                ed.Select(go); ed.view3D = true; ed.dirty = true; created = true;
                ConsoleLog("Created Terrain");
            }
            if (ImGui::MenuItem("Character")) {
                GameObject* g = ed.CreateEmpty("Character");
                g->AddComponent<Character>()->Apply();
                ed.Select(g); ed.view3D = true; ed.dirty = true; created = true;
                ConsoleLog("Created Character");
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
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play (Ctrl+P) — runs the scene in the Game view");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.22f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.28f, 0.28f, 1.0f));
        if (ImGui::Button("[]  Stop", ImVec2(btnW, 0))) { ed.Stop(); g_paused = false; ConsoleLog("Stop"); }
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop (Ctrl+P) — return to the edit state");
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
// An asset's icon: a category (for the drawn glyph), a tint, and a short label.
enum class AssetIcon { Folder, Script, Material, Scene, Prefab, Image, Audio, Mesh, Data, Visual, Generic };
struct AssetKind { ImU32 col; const char* letter; AssetIcon icon; };
static AssetKind KindOf(const std::string& extLower, bool isDir) {
    if (isDir) return {IM_COL32(235, 200, 90, 255), "", AssetIcon::Folder};
    if (extLower == ".okayscene")  return {IM_COL32(90, 150, 240, 255), "SCN", AssetIcon::Scene};
    if (extLower == ".okayprefab") return {IM_COL32(80, 210, 225, 255), "PFB", AssetIcon::Prefab};
    if (extLower == ".png" || extLower == ".jpg" || extLower == ".jpeg" ||
        extLower == ".bmp" || extLower == ".tga") return {IM_COL32(110, 200, 120, 255), "IMG", AssetIcon::Image};
    if (extLower == ".wav")        return {IM_COL32(230, 160, 70, 255), "WAV", AssetIcon::Audio};
    if (extLower == ".okay")       return {IM_COL32(120, 180, 255, 255), "OKS", AssetIcon::Script};
    if (extLower == ".lua" || extLower == ".cs") return {IM_COL32(150, 140, 235, 255), "SCR", AssetIcon::Script};
    if (extLower == ".okaymat")    return {IM_COL32(235, 110, 170, 255), "MAT", AssetIcon::Material};
    if (extLower == ".okaydata")   return {IM_COL32(110, 205, 190, 255), "DAT", AssetIcon::Data};
    if (extLower == ".obj")        return {IM_COL32(150, 175, 210, 255), "OBJ", AssetIcon::Mesh};
    if (extLower == ".okayvs")     return {IM_COL32(150, 200, 170, 255), "VS",  AssetIcon::Visual};
    return {IM_COL32(150, 152, 162, 255), "FILE", AssetIcon::Generic};
}

// Draw a simple type icon inside the cell rect (a folder shape, a script page, a
// material sphere, ...), so scripts/folders/materials are recognizable at a glance.
static void DrawAssetIcon(ImDrawList* dl, ImVec2 mn, ImVec2 mx, const AssetKind& k) {
    ImU32 c = k.col, dim = (k.col & 0x00FFFFFF) | 0x66000000;
    float w = mx.x - mn.x, h = mx.y - mn.y;
    ImVec2 ctr(mn.x + w * 0.5f, mn.y + h * 0.5f);
    float r = (w < h ? w : h) * 0.30f;
    switch (k.icon) {
        case AssetIcon::Folder: {
            ImVec2 a(mn.x + w*0.18f, mn.y + h*0.34f), b(mx.x - w*0.18f, mx.y - h*0.24f);
            dl->AddRectFilled(ImVec2(a.x, a.y - h*0.10f), ImVec2(a.x + w*0.30f, a.y + h*0.04f), c, 2.0f); // tab
            dl->AddRectFilled(a, b, c, 3.0f);
            dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(b.x, a.y + h*0.06f), dim);
            break;
        }
        case AssetIcon::Script: {  // a page with text lines
            ImVec2 a(mn.x + w*0.28f, mn.y + h*0.18f), b(mx.x - w*0.28f, mx.y - h*0.18f);
            dl->AddRectFilled(a, b, IM_COL32(235,238,245,255), 2.0f);
            for (int i = 0; i < 4; ++i) {
                float y = a.y + (b.y - a.y) * (0.22f + i * 0.2f);
                dl->AddLine(ImVec2(a.x + w*0.06f, y), ImVec2(b.x - w*0.06f, y), c, 1.5f);
            }
            break;
        }
        case AssetIcon::Material: {  // a shaded sphere
            dl->AddCircleFilled(ctr, r, c, 24);
            dl->AddCircleFilled(ImVec2(ctr.x - r*0.32f, ctr.y - r*0.32f), r*0.42f, IM_COL32(255,255,255,120), 16);
            break;
        }
        case AssetIcon::Scene:  dl->AddNgonFilled(ctr, r, c, 6); break;       // hex
        case AssetIcon::Prefab: dl->AddNgonFilled(ctr, r, c, 6);
                                dl->AddCircleFilled(ctr, r*0.4f, IM_COL32(20,30,40,255), 12); break;
        case AssetIcon::Audio: {
            dl->AddRectFilled(ImVec2(ctr.x - r*0.7f, ctr.y - r*0.3f), ImVec2(ctr.x, ctr.y + r*0.3f), c);
            dl->AddTriangleFilled(ImVec2(ctr.x, ctr.y - r*0.7f), ImVec2(ctr.x, ctr.y + r*0.7f), ImVec2(ctr.x + r*0.6f, ctr.y), c);
            break;
        }
        case AssetIcon::Mesh:   dl->AddNgon(ctr, r, c, 4, 2.5f); break;
        default:                dl->AddRectFilled(ImVec2(ctr.x - r, ctr.y - r*1.2f), ImVec2(ctr.x + r, ctr.y + r*1.2f), c, 2.0f); break;
    }
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

    // ---- View options: type filter, sort, thumbnail size ---------------------
    static int   s_filter = 0;   // 0 All,1 Scripts,2 Images,3 Scenes,4 Materials,5 Prefabs,6 Data,7 Audio
    static int   s_sort   = 0;   // 0 Name, 1 Type
    static float s_cell   = 76.0f;
    ImGui::SetNextItemWidth(110);
    ImGui::Combo("##filter", &s_filter, "All\0Scripts\0Images\0Scenes\0Materials\0Prefabs\0Data\0Audio\0");
    ImGui::SameLine(); ImGui::SetNextItemWidth(90);
    ImGui::Combo("##sort", &s_sort, "Name\0Type\0");
    ImGui::SameLine(); ImGui::SetNextItemWidth(100);
    ImGui::SliderFloat("##size", &s_cell, 48.0f, 128.0f, "%.0f px");
    ImGui::Separator();

    // Map the filter to a set of extensions ("" = always show, dirs always show).
    auto passFilter = [&](const std::string& ext, bool isDir) -> bool {
        if (isDir || s_filter == 0) return true;
        switch (s_filter) {
            case 1: return ext==".okay"||ext==".lua"||ext==".cs"||ext==".okayvs";
            case 2: return ext==".png"||ext==".jpg"||ext==".jpeg"||ext==".bmp";
            case 3: return ext==".okayscene";
            case 4: return ext==".okaymat";
            case 5: return ext==".okayprefab";
            case 6: return ext==".okaydata";
            case 7: return ext==".wav";
        }
        return true;
    };

    std::vector<fs::directory_entry> entries;
    if (fs::is_directory(dir, ec))
        for (auto& e : fs::directory_iterator(dir, ec)) entries.push_back(e);
    std::sort(entries.begin(), entries.end(), [&](const fs::directory_entry& a, const fs::directory_entry& b) {
        bool da = a.is_directory(), db = b.is_directory();
        if (da != db) return da;     // folders first
        if (s_sort == 1 && !da) {    // by type (extension), then name
            std::string ea = Lower(a.path().extension().string()), eb = Lower(b.path().extension().string());
            if (ea != eb) return ea < eb;
        }
        return Lower(a.path().filename().string()) < Lower(b.path().filename().string());
    });

    std::string needle = Lower(search);
    const float cell = s_cell;
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
        if (!passFilter(ext, isDir)) continue;
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
            // Neutral cell background; the drawn icon carries the category color.
            ImVec4 bg(0.16f, 0.17f, 0.19f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, selected == full ? cv : bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(cv.x * 0.45f, cv.y * 0.45f, cv.z * 0.45f, 1.0f));
            ImGui::Button("##cell", ImVec2(cell, cell));
            DrawAssetIcon(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(),
                          ImGui::GetItemRectMax(), k);
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
            if (!isDir && ImGui::MenuItem("Duplicate")) {
                std::error_code ce;
                fs::path src(full), stem = src.stem(), x = src.extension();
                fs::path dst = src.parent_path() / (stem.string() + " copy" + x.string());
                for (int n = 2; fs::exists(dst, ce); ++n)
                    dst = src.parent_path() / (stem.string() + " copy" + std::to_string(n) + x.string());
                fs::copy_file(src, dst, ce);
                ConsoleLog((ce ? "Duplicate failed: " : "Duplicated ") + dst.string());
            }
            if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(full.c_str());
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
            } else if (ext == ".okay" || ext == ".lua" || ext == ".cs") {
                OpenScriptFileInEditor(full);    // edit in the in-app Script Editor
            } else if (ext == ".okayvs") {
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
            : (s_filter == 0 && needle.empty() ? "Empty folder." : "No matching assets."));
    }
    // Footer: item count + the selected asset's name and size.
    ImGui::Separator();
    if (!selected.empty()) {
        std::error_code se; std::uintmax_t sz = fs::is_regular_file(selected, se) ? fs::file_size(selected, se) : 0;
        ImGui::TextDisabled("%d item%s   |   %s%s", shown, shown == 1 ? "" : "s",
                            fs::path(selected).filename().string().c_str(),
                            sz ? ("  (" + (sz < 1024 ? std::to_string(sz) + " B"
                                  : sz < 1024 * 1024 ? std::to_string(sz / 1024) + " KB"
                                  : std::to_string(sz / (1024 * 1024)) + " MB") + ")").c_str() : "");
    } else {
        ImGui::TextDisabled("%d item%s", shown, shown == 1 ? "" : "s");
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

// Save Manager: browse the .okaysave files in a project and view/edit their
// keys (the runtime side of the Easy-Save-style save system). Lets you inspect
// what a playtest wrote, tweak values, add/remove keys, or wipe a save.
void DrawSaveManager(EditorState& ed) {
    namespace fs = std::filesystem;
    if (!ImGui::Begin("Save Manager", &g_showSaveManager)) { ImGui::End(); return; }

    static std::string sel;            // selected .okaysave path
    static okay::SaveFile file;        // working copy of the selected file
    static std::string loaded;         // path currently loaded into `file`

    // Gather candidate save files: the default, plus any *.okaysave in the project.
    std::vector<std::string> files;
    files.push_back(okay::Save::DefaultFile());
    fs::path root = ed.projectDir().empty() ? fs::path(".") : fs::path(ed.projectDir());
    std::error_code ec;
    if (fs::exists(root, ec))
        for (auto& e : fs::recursive_directory_iterator(root, ec))
            if (e.is_regular_file() && e.path().extension() == ".okaysave")
                files.push_back(e.path().string());

    ImGui::TextDisabled("Runtime save files (Easy-Save-style key/value data)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##savefile", sel.empty() ? "(choose a save file)" : sel.c_str())) {
        for (auto& f : files)
            if (ImGui::Selectable(f.c_str(), f == sel)) { sel = f; loaded.clear(); }
        ImGui::EndCombo();
    }
    if (sel.empty()) { ImGui::TextDisabled("No file selected."); ImGui::End(); return; }
    if (loaded != sel) { file = okay::SaveFile{}; file.Load(sel); loaded = sel; }

    ImGui::SameLine();
    if (ImGui::SmallButton("Reload")) { file = okay::SaveFile{}; file.Load(sel); }
    ImGui::Separator();

    // Editable key table. Values are stored as type:payload; show the type and an
    // editable payload, re-encoding on change.
    if (ImGui::BeginTable("savekeys", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 0.4f);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableHeadersRow();

        std::string deleteKey;
        // Snapshot keys (sorted) so editing during iteration is safe.
        std::vector<std::pair<std::string, std::string>> rows(file.Raw().begin(), file.Raw().end());
        std::sort(rows.begin(), rows.end(), [](auto& a, auto& b){ return a.first < b.first; });
        for (auto& kv : rows) {
            ImGui::PushID(kv.first.c_str());
            char type = kv.second.empty() ? 's' : kv.second[0];
            std::string payload = kv.second.size() > 2 ? kv.second.substr(2) : "";
            const char* tname = type == 'f' ? "float" : type == 'i' ? "int"
                              : type == 'b' ? "bool" : type == 'v' ? "Vector3" : "string";
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(kv.first.c_str());
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", tname);
            ImGui::TableNextColumn();
            char vbuf[256]; std::strncpy(vbuf, payload.c_str(), sizeof(vbuf) - 1); vbuf[sizeof(vbuf)-1] = '\0';
            ImGui::SetNextItemWidth(-44);
            if (ImGui::InputText("##v", vbuf, sizeof(vbuf)))
                file.SetRaw(kv.first, std::string(1, type) + ":" + vbuf);
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) deleteKey = kv.first;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (!deleteKey.empty()) file.Delete(deleteKey);
    }

    // Add a new key (typed).
    ImGui::Spacing();
    static char newKey[64] = "";
    static int newType = 0;     // 0 float,1 int,2 bool,3 string,4 vec3
    static char newVal[128] = "0";
    ImGui::SetNextItemWidth(140); ImGui::InputTextWithHint("##nk", "new key", newKey, sizeof(newKey));
    ImGui::SameLine(); ImGui::SetNextItemWidth(90);
    ImGui::Combo("##nt", &newType, "float\0int\0bool\0string\0Vector3\0");
    ImGui::SameLine(); ImGui::SetNextItemWidth(140); ImGui::InputText("##nv", newVal, sizeof(newVal));
    ImGui::SameLine();
    if (ImGui::SmallButton("Add") && newKey[0]) {
        const char tc[] = {'f', 'i', 'b', 's', 'v'};
        file.SetRaw(newKey, std::string(1, tc[newType]) + ":" + newVal);
        newKey[0] = '\0';
    }

    ImGui::Separator();
    if (ImGui::Button("Save to disk")) {
        if (file.Save(sel)) ConsoleLog("Saved " + sel); else ConsoleLog("Save failed: " + sel);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear all keys")) file.Clear();
    ImGui::SameLine();
    ImGui::TextDisabled("%d keys", (int)file.Count());
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

    // Global renderer pipeline switches (process-wide, not per-scene). These let
    // you toggle/tune the 3D post-processing & lighting features live.
    if (ImGui::CollapsingHeader("Rendering (Lighting & Post FX)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Performance");
        if (ImGui::SliderFloat("Render scale", &g_renderScale, 0.25f, 1.0f, "%.2fx")) SaveSettings();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Renders the 3D view at this fraction of resolution and\nupscales. Lower = much faster (softer image). The biggest\nFPS lever for the software renderer.");
        ImGui::SeparatorText("Effects");
        ImGui::TextDisabled("Toggle any effect on/off. On by default: Hemisphere\nambient, Tone mapping, Anti-aliasing. The rest are opt-in.");
        ImGui::Spacing();
        ImGui::Checkbox("Per-pixel lighting (Phong)", &PerPixelLighting());

        ImGui::Checkbox("Hemisphere ambient", &HemisphereAmbient());
        if (HemisphereAmbient())
            ImGui::SliderFloat("Hemisphere strength", &HemisphereStrength(), 0.0f, 1.5f, "%.2f");

        ImGui::Checkbox("Shadows", &ShadowsEnabled());
        if (ShadowsEnabled())
            ImGui::SliderFloat("Shadow softness", &ShadowSoftness(), 0.0f, 6.0f, "%.1f tx");

        ImGui::Checkbox("Ambient occlusion (SSAO)", &SSAOEnabled());
        if (SSAOEnabled()) {
            ImGui::SliderFloat("AO radius", &SSAORadius(), 0.05f, 2.0f, "%.2f");
            ImGui::SliderFloat("AO strength", &SSAOStrength(), 0.0f, 2.0f, "%.2f");
        }

        ImGui::Checkbox("Rim light (Fresnel)", &RimLightEnabled());
        if (RimLightEnabled()) {
            ImGui::SliderFloat("Rim strength", &RimStrength(), 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Rim power", &RimPower(), 0.5f, 8.0f, "%.1f");
        }

        ImGui::Checkbox("Bloom", &BloomEnabled());
        if (BloomEnabled()) {
            ImGui::SliderFloat("Bloom threshold", &BloomThreshold(), 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Bloom intensity", &BloomIntensity(), 0.0f, 2.0f, "%.2f");
        }

        ImGui::Checkbox("Tone mapping (ACES)", &ToneMapEnabled());
        if (ToneMapEnabled())
            ImGui::SliderFloat("Exposure", &Exposure(), 0.1f, 4.0f, "%.2f");

        ImGui::Checkbox("Anti-aliasing (FXAA)", &FXAAEnabled());

        ImGui::Checkbox("Color grading", &ColorGradeEnabled());
        if (ColorGradeEnabled()) {
            ImGui::SliderFloat("Brightness", &Brightness(), 0.2f, 2.0f, "%.2f");
            ImGui::SliderFloat("Contrast",   &Contrast(),   0.5f, 2.0f, "%.2f");
            ImGui::SliderFloat("Saturation", &Saturation(), 0.0f, 2.0f, "%.2f");
            ImGui::SliderFloat("Vignette",   &Vignette(),   0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Gamma",      &Gamma(),      0.5f, 2.4f, "%.2f");
        }

        // Debug View: Scene-view overlays and camera knobs to inspect 3D geometry
        // yourself. These are editor-only (Scene view) and never affect the Game
        // view or a build. All persist across sessions.
        ImGui::SeparatorText("Debug view (Scene only)");
        if (ImGui::Checkbox("Ground grid", &g_showGrid)) SaveSettings();
        if (ImGui::Checkbox("Sky gradient", &g_sceneSkybox)) SaveSettings();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Turn off to see meshes against a flat background\n(isolates what's geometry vs. the sky behind it).");
        if (ImGui::Checkbox("Colliders", &g_showColliders)) SaveSettings();
        if (ImGui::Checkbox("Gizmos (selection / camera / lights)", &g_showGizmos)) SaveSettings();
        if (ImGui::Checkbox("Wireframe (all meshes)", &g_wireframeAll)) SaveSettings();
        if (ImGui::Checkbox("World axes (origin)", &g_showWorldAxes)) SaveSettings();
        if (ImGui::Checkbox("Camera HUD (zoom / angle / pos)", &g_showCamHud)) SaveSettings();
        if (ImGui::SliderFloat("Scene camera FOV", &g_editorFov, 20.0f, 110.0f, "%.0f deg")) SaveSettings();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Vertical field of view for the editor Scene camera.\nLower = flatter / more zoomed (less perspective\ndistortion); higher = wider / more stretched at edges.");
        if (ImGui::SliderFloat("Scene near clip", &g_editorNear, 0.05f, 20.0f, "%.2f")) SaveSettings();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Near clip plane for the Scene camera. Higher = better depth\nprecision (stops surfaces z-fighting/flickering as you move),\nbut you can't view things closer than this. Default 5.");
        ImGui::Spacing();
        if (ImGui::Button("Save Scene view -> PNG (debug)") && g_lastSceneW > 0)
            g_captureScene = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Writes the EXACT pixels the engine renders for the current\nScene view to scene_capture.png — bypasses the screen/monitor.\nUse it when a glitch is visible, then share the PNG.");
    }

    // Handle a requested capture: re-render the stored Scene-view frame at full
    // resolution and write the raw engine pixels to a PNG (true engine output).
    if (g_captureScene && g_lastSceneW > 0) {
        g_captureScene = false;
        ApplySceneLight(ed.scene());
        Raster work; std::vector<std::uint32_t> out;
        const std::uint32_t* px = RenderMeshesSS(work, out, ed.scene(),
                                                 g_lastSceneVP, g_lastSceneEye,
                                                 g_lastSceneW, g_lastSceneH, 2);
        okay::Image img(g_lastSceneW, g_lastSceneH);
        for (int y = 0; y < g_lastSceneH; ++y)
            for (int x = 0; x < g_lastSceneW; ++x) {
                std::uint32_t p = px[(std::size_t)y * g_lastSceneW + x];
                img.SetPixel(x, y, Color{(p & 0xFF) / 255.0f, ((p >> 8) & 0xFF) / 255.0f,
                                         ((p >> 16) & 0xFF) / 255.0f, ((p >> 24) & 0xFF) / 255.0f});
            }
        std::string path = "scene_capture.png";
        if (img.SavePNG(path)) ConsoleLog("Saved Scene view to " + path + " (raw engine pixels)", 0);
        else                   ConsoleLog("Failed to save scene_capture.png", 2);
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
    api("prefs_set / prefs_get / prefs_save", "PlayerPrefs-style key/value");
    api("save(\"k\", v[, file]) / load(\"k\", def[, file])", "Easy-Save: typed values (num/str/Vector3), many files");
    api("save_has / save_delete / save_clear", "manage keys in a save file");
    api("save_exists([file]) / save_delete_file([file])", "check / remove a save file");
    api("data_num / data_str / data_set", "Scriptable Object (.okaydata) fields");
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

// Carries cursor info OUT (line/col/pos) and pending edit commands IN (go-to-line,
// comment toggle) so the InputText callback can act on the live, active buffer —
// the only safe way to mutate/move the cursor while editing.
struct ScriptCaret {
    int  line = 1, col = 1, pos = 0;   // reported out each frame
    int  selLen = 0;                   // out: selected character count
    int  gotoLine = 0;                 // in: jump to this 1-based line (0 = none)
    int  gotoPos = -1;                 // in: move caret to this byte offset (find-next)
    int  gotoSelLen = 0;               // in: select this many chars from gotoPos
    bool toggleComment = false;        // in: toggle "// " on the caret's line
    std::string insert;                // in: insert this text at the caret (snippets)
    bool autoPairs = true;             // auto-close brackets + auto-indent on Enter
    int  prevLen = -1;                 // buffer length last frame (to detect typing)
};
static int ScriptCaretCallback(ImGuiInputTextCallbackData* d) {
    auto* c = (ScriptCaret*)d->UserData;
    ImGuiIO& io = ImGui::GetIO();
    auto lineBounds = [&](int p, int& ls, int& le) {
        if (p > d->BufTextLen) p = d->BufTextLen;
        ls = p; while (ls > 0 && d->Buf[ls - 1] != '\n') --ls;
        le = p; while (le < d->BufTextLen && d->Buf[le] != '\n') ++le;
    };
    // Go to line: move the caret to the start of the requested line.
    if (c->gotoLine > 0) {
        int target = c->gotoLine; c->gotoLine = 0;
        int line = 1, pos = 0;
        for (; pos < d->BufTextLen && line < target; ++pos)
            if (d->Buf[pos] == '\n') ++line;
        d->CursorPos = d->SelectionStart = d->SelectionEnd = pos;
    }
    // Find Next/Prev: move the caret to a match and select it.
    if (c->gotoPos >= 0) {
        int p = c->gotoPos > d->BufTextLen ? d->BufTextLen : c->gotoPos;
        int e = (c->gotoSelLen > 0) ? p + c->gotoSelLen : p;
        if (e > d->BufTextLen) e = d->BufTextLen;
        d->CursorPos = e; d->SelectionStart = p; d->SelectionEnd = e;
        c->gotoPos = -1; c->gotoSelLen = 0;
    }
    // Insert a snippet/template at the caret.
    if (!c->insert.empty()) {
        d->InsertChars(d->CursorPos, c->insert.c_str());
        c->insert.clear();
    }
    // Toggle a line comment ("// ") at the first non-space of the caret's line.
    if (c->toggleComment) {
        c->toggleComment = false;
        int ls, le; lineBounds(d->CursorPos, ls, le);
        int fnw = ls; while (fnw < le && (d->Buf[fnw] == ' ' || d->Buf[fnw] == '\t')) ++fnw;
        if (fnw + 1 < d->BufTextLen && d->Buf[fnw] == '/' && d->Buf[fnw + 1] == '/') {
            int rem = (fnw + 2 < d->BufTextLen && d->Buf[fnw + 2] == ' ') ? 3 : 2;
            d->DeleteChars(fnw, rem);
        } else {
            d->InsertChars(fnw, "// ");
        }
    }
    // Ctrl+D: duplicate the caret's line below it.
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
        int ls, le; lineBounds(d->CursorPos, ls, le);
        std::string dup = "\n" + std::string(d->Buf + ls, d->Buf + le);
        d->InsertChars(le, dup.c_str());
        d->CursorPos = d->SelectionStart = d->SelectionEnd = d->CursorPos + (int)dup.size();
    }
    // Alt+Up / Alt+Down: move the caret's line up or down (swap with neighbor).
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        int ls, le; lineBounds(d->CursorPos, ls, le);
        if (ls > 0) {
            int pls = ls - 1; while (pls > 0 && d->Buf[pls - 1] != '\n') --pls;
            std::string prev(d->Buf + pls, d->Buf + (ls - 1));
            std::string cur(d->Buf + ls, d->Buf + le);
            int off = d->CursorPos - ls;
            d->DeleteChars(pls, le - pls);
            std::string repl = cur + "\n" + prev;
            d->InsertChars(pls, repl.c_str());
            d->CursorPos = d->SelectionStart = d->SelectionEnd = pls + off;
        }
    }
    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        int ls, le; lineBounds(d->CursorPos, ls, le);
        if (le < d->BufTextLen) {
            int nls = le + 1, nle = nls; while (nle < d->BufTextLen && d->Buf[nle] != '\n') ++nle;
            std::string cur(d->Buf + ls, d->Buf + le);
            std::string next(d->Buf + nls, d->Buf + nle);
            int off = d->CursorPos - ls;
            d->DeleteChars(ls, nle - ls);
            std::string repl = next + "\n" + cur;
            d->InsertChars(ls, repl.c_str());
            d->CursorPos = d->SelectionStart = d->SelectionEnd = ls + (int)next.size() + 1 + off;
        }
    }
    // Auto-pairing + auto-indent: only when exactly one char was just typed.
    if (c->autoPairs && c->prevLen >= 0 && d->BufTextLen == c->prevLen + 1 &&
        d->CursorPos > 0 && d->CursorPos <= d->BufTextLen) {
        char ch = d->Buf[d->CursorPos - 1];
        char next = d->CursorPos < d->BufTextLen ? d->Buf[d->CursorPos] : '\0';
        auto isWord = [](char x){ return std::isalnum((unsigned char)x) || x == '_'; };
        if (ch == '\n') {
            // Copy the broken line's leading whitespace; add a level after '{'.
            int nl = d->CursorPos - 1;
            int pls = nl; while (pls > 0 && d->Buf[pls - 1] != '\n') --pls;
            std::string indent;
            for (int i = pls; i < nl && (d->Buf[i] == ' ' || d->Buf[i] == '\t'); ++i) indent += d->Buf[i];
            int last = nl - 1; while (last >= pls && (d->Buf[last] == ' ' || d->Buf[last] == '\t')) --last;
            if (last >= pls && d->Buf[last] == '{') indent += "    ";
            if (!indent.empty()) {
                int c0 = d->CursorPos; d->InsertChars(c0, indent.c_str());
                d->CursorPos = d->SelectionStart = d->SelectionEnd = c0 + (int)indent.size();
            }
        } else if ((ch == '(' || ch == '[' || ch == '{' || ch == '"') &&
                   (next == '\0' || next == ' ' || next == ')' || next == ']' ||
                    next == '}' || next == '\n' || !isWord(next))) {
            const char* close = ch == '(' ? ")" : ch == '[' ? "]" : ch == '{' ? "}" : "\"";
            int c0 = d->CursorPos; d->InsertChars(c0, close);
            d->CursorPos = d->SelectionStart = d->SelectionEnd = c0;   // sit between the pair
        } else if ((ch == ')' || ch == ']' || ch == '}' || ch == '"') && next == ch) {
            // Typed a closer right before the auto-inserted one: skip over it.
            d->DeleteChars(d->CursorPos - 1, 1);
            d->CursorPos = d->SelectionStart = d->SelectionEnd = d->CursorPos + 1;
        }
    }
    c->prevLen = d->BufTextLen;
    int ln = 1, col = 1;
    for (int k = 0; k < d->CursorPos && k < d->BufTextLen; ++k) {
        if (d->Buf[k] == '\n') { ++ln; col = 1; } else ++col;
    }
    c->line = ln; c->col = col; c->pos = d->CursorPos;
    c->selLen = d->SelectionEnd - d->SelectionStart;
    if (c->selLen < 0) c->selLen = -c->selLen;
    return 0;
}

// Draw the code text with VS Code "Dark+" syntax colors on top of the editor.
// ProggyClean is monospace, so glyphs advance by a fixed width and the colored
// overlay lines up exactly with the InputText beneath it.
// Words the editor offers for autocomplete: keywords, types, and common builtins
// (a curated subset of the OkayScript API — see docs/scripting.md).
static const std::vector<std::string>& ScriptCompletions() {
    static const std::vector<std::string> w = {
        // keywords
        "if","else","for","while","do","return","break","continue","switch","case",
        "var","function","class","new","public","private","true","false","foreach","in",
        // Unity-style API
        "Vector3","Vector2","Color","Mathf","Input","Time","Debug","Random","Physics2D",
        "SceneManager","Quaternion","transform","gameObject","OkaySource",
        // transform / movement
        "move","set_pos","set_x","set_y","pos_x","pos_y","rotate","move_toward","look_at",
        "move3","set_pos3","set_z","pos_z","rotate3","set_scale","set_scale3","move_forward",
        // physics
        "set_velocity","set_velocity3","add_force","add_impulse","jump","set_gravity",
        // input
        "key","key_down","key_up","axis_x","axis_y","mouse_x","mouse_y","mouse","mouse_down",
        // object / scene
        "name","set_name","tag","set_tag","has_tag","set_active","self_active","destroy",
        "set_parent","exists","is_active","obj_x","obj_y","dist_to","destroy_obj","count_tag",
        "nearest_tag","set_cam","move_cam","set_cam_zoom","set_bg","set_light","set_ambient",
        "load_scene","load_scene_index","load_next_scene","screen_w","screen_h",
        // components / fx
        "set_text","set_color","set_texture","set_mesh","emit","play_anim","play_sound",
        "set_progress","set_fill",
        // save / prefs / data
        "save","load","save_has","save_delete","save_exists","save_clear","prefs_set",
        "prefs_get","prefs_get_str","prefs_save","prefs_load","data_num","data_str","data_set",
        // math / util
        "abs","sin","cos","tan","sqrt","pow","floor","ceil","round","min","max","sign",
        "clamp","clamp01","lerp","distance","random","random_range","print","log","format",
        // tweens
        "tween_move","tween_move3","tween_scale","tween_rotate","tween_color","tween_fade",
        "tween_loop_move","tween_punch_scale","tween_shake",
        // networking
        "net_host","net_join","net_send","net_poll","net_is_server",
        // ui
        "ui_set_text","ui_get_text","ui_clicked","ui_slider_value","ui_set_slider",
        "ui_toggle_value","ui_set_progress",
        // raycast
        "raycast","raycast3","overlap_circle",
    };
    return w;
}

// Members offered after "<receiver>." in the editor (Unity-style API surface, so
// e.g. typing `transform.` lists position/rotation/Translate/…). Returns an empty
// list for unknown receivers.
static const std::vector<std::string>& ScriptMembers(const std::string& receiver) {
    static const std::unordered_map<std::string, std::vector<std::string>> M = {
        {"transform", {"position","rotation","localPosition","localScale","eulerAngles",
                       "right","up","forward","Translate","Rotate","LookAt"}},
        {"gameObject", {"name","tag","activeSelf","SetActive","GetComponent"}},
        {"Input", {"GetAxis","GetButton","GetButtonDown","GetButtonUp","GetKey","GetKeyDown",
                   "GetKeyUp","GetMouseButton","GetMouseButtonDown","GetMouseButtonUp","mousePosition"}},
        {"Time", {"deltaTime","time","timeScale","fixedDeltaTime"}},
        {"Mathf", {"Abs","Acos","Asin","Atan","Ceil","Clamp","Clamp01","Cos","Deg2Rad","DeltaAngle",
                   "Epsilon","Exp","Floor","Infinity","InverseLerp","Lerp","LerpAngle","Log","Max",
                   "Min","MoveTowards","PI","PerlinNoise","PingPong","Pow","Rad2Deg","Repeat","Round",
                   "Sign","Sin","SmoothStep","Sqrt","Tan"}},
        {"Physics2D", {"Raycast","OverlapCircle"}},
        {"Debug", {"Log","LogWarning","LogError"}},
        {"Random", {"Range","value"}},
        {"Quaternion", {"Euler","identity","AngleAxis"}},
        {"Vector3", {"zero","one","up","down","left","right","forward","back","Angle","Distance",
                     "Dot","Cross","Lerp","LerpUnclamped","Normalize"}},
        {"Vector2", {"zero","one","up","down","left","right","Lerp","Distance"}},
        {"Color", {"red","green","blue","white","black","gray","grey","yellow","cyan","magenta","Lerp"}},
        {"SceneManager", {"LoadScene","GetActiveScene"}},
        {"Screen", {"width","height"}},
        {"Application", {"Quit"}},
    };
    static const std::vector<std::string> empty;
    auto it = M.find(receiver);
    return it == M.end() ? empty : it->second;
}

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
    // Bracket-pair colorization: rotate through these by nesting depth (VS Code).
    static const ImU32 cBracket[] = {
        IM_COL32(255, 214, 110, 255),  // gold
        IM_COL32(214, 130, 230, 255),  // magenta
        IM_COL32(90, 178, 240, 255),   // blue
    };
    int bracketDepth = 0;
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
        if (c == '(' || c == '[' || c == '{') {                        // opening bracket
            ImU32 bc = cBracket[bracketDepth % 3]; ++bracketDepth;
            char b[2] = {c, 0}; dl->AddText(ImVec2(x, y), bc, b); x += charW; ++i; continue;
        }
        if (c == ')' || c == ']' || c == '}') {                        // closing bracket
            if (bracketDepth > 0) --bracketDepth;
            ImU32 bc = cBracket[bracketDepth % 3];
            char b[2] = {c, 0}; dl->AddText(ImVec2(x, y), bc, b); x += charW; ++i; continue;
        }
        if (c != ' ' && c != '\t') { char b[2] = {c, 0}; dl->AddText(ImVec2(x, y), cDefault, b); }
        x += (c == '\t' ? 4 * charW : charW); ++i;
    }
}

// Per-script "last saved" text so tabs can show a modified (●) marker like VS
// Code. A tab is dirty when its live edit buffer differs from this baseline.
static std::unordered_map<okay::ScriptComponent*, std::string> g_scriptSaved;
static bool ScriptTabDirty(okay::ScriptComponent* t) {
    const char* live = PeekCodeBuffer(t);
    if (!live) return false;
    auto it = g_scriptSaved.find(t);
    return it != g_scriptSaved.end() && it->second != live;
}

// Re-indent OkayScript by brace depth (4 spaces / level), a basic "Format
// Document". Braces inside strings and // line comments are ignored.
static std::string FormatOkayScript(const std::string& src) {
    std::vector<std::string> lines; std::string cur;
    for (char c : src) { if (c == '\n') { lines.push_back(cur); cur.clear(); } else if (c != '\r') cur.push_back(c); }
    lines.push_back(cur);
    std::string out; int depth = 0;
    for (const std::string& raw : lines) {
        auto a = raw.find_first_not_of(" \t");
        auto b = raw.find_last_not_of(" \t");
        std::string t = (a == std::string::npos) ? "" : raw.substr(a, b - a + 1);
        int net = 0, leadClose = 0; bool sawOther = false, inStr = false, comment = false; char q = 0;
        for (std::size_t i = 0; i < t.size(); ++i) {
            char c = t[i];
            if (comment) break;
            if (inStr) { if (c == '\\') { ++i; continue; } if (c == q) inStr = false; continue; }
            if (c == '"' || c == '\'') { inStr = true; q = c; continue; }
            if (c == '/' && i + 1 < t.size() && t[i + 1] == '/') break;
            if (c == '{') { ++net; sawOther = true; }
            else if (c == '}') { --net; if (!sawOther) ++leadClose; }
            else if (c != ' ' && c != '\t') sawOther = true;
        }
        int indent = depth - leadClose; if (indent < 0) indent = 0;
        if (!t.empty()) out.append((std::size_t)indent * 4, ' ');
        out += t; out += '\n';
        depth += net; if (depth < 0) depth = 0;
    }
    if (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

// Functions/classes in a script, for the Outline jump menu: {1-based line, label}.
static std::vector<std::pair<int, std::string>> ScriptOutline(const std::string& src) {
    std::vector<std::pair<int, std::string>> out;
    auto isW = [](char c){ return std::isalnum((unsigned char)c) || c == '_'; };
    auto consider = [&](const std::string& raw, int lineNo) {
        auto a = raw.find_first_not_of(" \t"); if (a == std::string::npos) return;
        std::string t = raw.substr(a);
        auto after = [&](const char* kw) -> std::string {
            std::string k = kw; if (t.rfind(k, 0) != 0) return "";
            std::size_t p = k.size(); while (p < t.size() && (t[p]==' '||t[p]=='\t')) ++p;
            std::string n; while (p < t.size() && isW(t[p])) n += t[p++]; return n;
        };
        if (std::string n = after("function"); !n.empty()) { out.push_back({lineNo, "f  " + n}); return; }
        if (t.rfind("public class", 0) == 0) {
            std::size_t p = 12; while (p < t.size() && (t[p]==' '||t[p]=='\t')) ++p;
            std::string n; while (p < t.size() && isW(t[p])) n += t[p++];
            if (!n.empty()) out.push_back({lineNo, "C  " + n}); return;
        }
        if (std::string n = after("class"); !n.empty()) { out.push_back({lineNo, "C  " + n}); return; }
        // method form:  <type> Name(   (skip control keywords / calls)
        std::size_t p = 0; auto skip = [&]{ while (p < t.size() && (t[p]==' '||t[p]=='\t')) ++p; };
        skip(); std::string w1; while (p < t.size() && isW(t[p])) w1 += t[p++];
        skip(); std::string w2; while (p < t.size() && isW(t[p])) w2 += t[p++];
        skip();
        static const char* skipW[] = {"return","new","if","for","while","switch","else","var","do"};
        for (const char* s : skipW) if (w1 == s) return;
        if (!w1.empty() && !w2.empty() && p < t.size() && t[p] == '(')
            out.push_back({lineNo, "f  " + w2});
    };
    std::string ln; int n = 1;
    for (char c : src) { if (c == '\n') { consider(ln, n); ln.clear(); ++n; } else if (c != '\r') ln.push_back(c); }
    consider(ln, n);
    return out;
}

void DrawScriptEditor(EditorState& ed) {
    if (!ImGui::Begin("Script Editor", &g_showScriptEditor)) { ImGui::End(); return; }

    // Gather every live ScriptComponent (an object can have several), and prune
    // tabs whose component was deleted. `owner` maps a component to its object.
    std::unordered_map<ScriptComponent*, GameObject*> owner;
    for (const auto& up : ed.scene().Objects())
        for (ScriptComponent* s : up->GetComponents<ScriptComponent>()) owner[s] = up.get();
    {
        std::vector<ScriptComponent*> alive;
        for (ScriptComponent* t : g_scriptTabs) if (owner.count(t) || IsLooseScript(t)) alive.push_back(t);
        g_scriptTabs = std::move(alive);
        if (std::find(g_scriptTabs.begin(), g_scriptTabs.end(), g_activeScriptTab) == g_scriptTabs.end())
            g_activeScriptTab = g_scriptTabs.empty() ? nullptr : g_scriptTabs.front();
    }
    // Selecting a scriptable object (or one that just gained a script) opens and
    // focuses tabs for its scripts.
    static GameObject* s_lastSel = nullptr;
    static int s_lastSelScripts = 0;
    GameObject* sel = ed.selected();
    int selScripts = sel ? (int)sel->GetComponents<ScriptComponent>().size() : 0;
    if (sel && selScripts > 0 && (sel != s_lastSel || selScripts != s_lastSelScripts)) {
        ScriptComponent* focusComp = nullptr;
        for (ScriptComponent* s : sel->GetComponents<ScriptComponent>()) {
            if (std::find(g_scriptTabs.begin(), g_scriptTabs.end(), s) == g_scriptTabs.end())
                g_scriptTabs.push_back(s);
            if (!focusComp) focusComp = s;
        }
        if (focusComp) { g_activeScriptTab = focusComp; g_focusScriptTab = focusComp; }
    }
    s_lastSel = sel; s_lastSelScripts = selScripts;

    if (g_scriptTabs.empty()) {
        ImGui::TextDisabled("Select an object with a Script component.");
        ImGui::TextDisabled("Add one via Inspector > Add Component > Scripts.");
        ImGui::End();
        return;
    }

    // One tab per open script; the selected tab drives the editor below.
    if (ImGui::BeginTabBar("##scripttabs",
            ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll)) {
        ScriptComponent* closeTab = nullptr;
        for (ScriptComponent* t : g_scriptTabs) {
            GameObject* tgo = owner[t];
            std::string disp = !t->Path().empty()
                ? std::filesystem::path(t->Path()).filename().string()
                : (tgo ? tgo->name : std::string("script")) + "." + extide::ExtFor(t->Language());
            if (ScriptTabDirty(t)) disp = "\xe2\x97\x8f " + disp;   // modified marker
            char id[32]; std::snprintf(id, sizeof(id), "###sct%p", (void*)t);
            ImGuiTabItemFlags fl = (g_focusScriptTab == t) ? ImGuiTabItemFlags_SetSelected : 0;
            bool open = true;
            if (ImGui::BeginTabItem((disp + id).c_str(), &open, fl)) {
                g_activeScriptTab = t;
                ImGui::EndTabItem();
            }
            if (!open) closeTab = t;
        }
        ImGui::EndTabBar();
        g_focusScriptTab = nullptr;
        if (closeTab) {
            g_scriptTabs.erase(std::remove(g_scriptTabs.begin(), g_scriptTabs.end(), closeTab),
                               g_scriptTabs.end());
            if (g_activeScriptTab == closeTab)
                g_activeScriptTab = g_scriptTabs.empty() ? nullptr : g_scriptTabs.front();
            g_scriptSaved.erase(closeTab);   // drop the modified-marker baseline
            // Free a loose (Project-opened) script when its tab is closed.
            g_looseScripts.erase(std::remove_if(g_looseScripts.begin(), g_looseScripts.end(),
                [&](const std::unique_ptr<ScriptComponent>& u){ return u.get() == closeTab; }),
                g_looseScripts.end());
        }
    }

    ScriptComponent* sc = g_activeScriptTab ? g_activeScriptTab
                        : (g_scriptTabs.empty() ? nullptr : g_scriptTabs.front());
    if (!sc) { ImGui::End(); return; }
    GameObject* go = owner.count(sc) ? owner[sc] : nullptr;
    // A loose (Project-opened) script has no GameObject but is file-backed.
    if (!go && !IsLooseScript(sc)) { ImGui::End(); return; }

    {
        auto& buf = CodeBuffer(sc, sc->Source().empty()
            ? extide::StarterScript(sc->Language()) : sc->Source());
        g_scriptSaved.emplace(sc, std::string(buf.data()));   // clean baseline on first open
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
        static bool s_palReq = false;   // request to open the command palette
        static char s_find[128] = "";   // Find query (Ctrl+F highlights matches)
        if (ImGui::SmallButton("Run")) {           // compile + run
            bool ok = sc->LoadSource(buf.data(), &s_error);
            if (ok) s_error.clear();
            ConsoleLog(ok ? "Compiled OK" : "Error: " + s_error);
            ed.dirty = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Compile and run now (executes the script immediately).\nUse Save to just write the file without running.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Save")) {
            std::string p = filePath();
            if (extide::WriteFile(p, buf.data())) {
                sc->SetPath(p); ConsoleLog("Saved " + p);
                g_scriptSaved[sc] = buf.data();    // clears the modified marker
            } else ConsoleLog("Save failed");
            ed.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reload")) {
            if (!sc->Path().empty()) {
                std::string src = extide::ReadFile(sc->Path());
                SetCodeBuffer(sc, src);
                std::string e; sc->LoadSource(src, &e);
                g_scriptSaved[sc] = src;
                ConsoleLog("Reloaded " + sc->Path());
            } else ConsoleLog("No file to reload (Save first)");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Format")) {          // re-indent by brace depth
            std::string f = FormatOkayScript(buf.data());
            SetCodeBuffer(sc, f); ed.dirty = true; ConsoleLog("Formatted script");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Re-indent the whole document (4 spaces / level)");
        ImGui::SameLine();
        if (ImGui::SmallButton("Open in IDE")) {
            std::string p = filePath();
            extide::WriteFile(p, buf.data()); sc->SetPath(p); extide::OpenExternal(p);
            ConsoleLog("Opened " + p + " in external IDE");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Docs")) g_showScriptDocs = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("Cmds")) s_palReq = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Command palette (Ctrl+Shift+P)");
        ImGui::SameLine();
        ImGui::Checkbox("Syntax", &s_highlight);
        ImGui::SameLine();
        static bool s_minimap = true;
        ImGui::Checkbox("Map", &s_minimap);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show the code minimap (click it to scroll)");
        ImGui::SameLine();
        static float s_zoom = 1.0f;
        if (ImGui::SmallButton("A-")) s_zoom = Mathf::Clamp(s_zoom - 0.1f, 0.7f, 3.0f);
        ImGui::SameLine();
        if (ImGui::SmallButton("A+")) s_zoom = Mathf::Clamp(s_zoom + 0.1f, 0.7f, 3.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom code (Ctrl+scroll in the editor)");

        // Caret state + pending editor commands (applied in the InputText callback
        // so they act on the live buffer). Declared here so the toolbar can drive
        // them; reported back (line/col) by the callback below.
        static ScriptCaret caret;
        ImGui::SameLine();
        if (ImGui::SmallButton("//")) caret.toggleComment = true;   // comment/uncomment line
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle // comment on the current line (Ctrl+/)");
        ImGui::SameLine(); ImGui::TextDisabled("Go");
        ImGui::SameLine(); ImGui::SetNextItemWidth(54);
        static int s_gotoLine = 1;
        static int s_scrollToLine = 0;
        bool goEnter = ImGui::InputInt("##goto", &s_gotoLine, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::SmallButton("->") || goEnter) {
            caret.gotoLine = s_gotoLine < 1 ? 1 : s_gotoLine;
            s_scrollToLine = caret.gotoLine;
        }
        // Outline: jump to any function/class in the file (VS Code's symbol list).
        ImGui::SameLine();
        if (ImGui::SmallButton("Outline")) ImGui::OpenPopup("##outline");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jump to a function or class");
        if (ImGui::BeginPopup("##outline")) {
            auto syms = ScriptOutline(buf.data());
            if (syms.empty()) ImGui::TextDisabled("No functions or classes found.");
            for (const auto& s : syms) {
                char lbl[160]; std::snprintf(lbl, sizeof(lbl), "%-28s :%d", s.second.c_str(), s.first);
                if (ImGui::MenuItem(lbl)) { caret.gotoLine = s.first; s_scrollToLine = s.first; }
            }
            ImGui::EndPopup();
        }
        // Ctrl+/ toggles the comment, Ctrl+G focuses the line box.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_Slash, false)) caret.toggleComment = true;
        }
        // Snippets: insert a common template at the caret.
        ImGui::SameLine();
        if (ImGui::SmallButton("Snippet")) ImGui::OpenPopup("##snippets");
        if (ImGui::BeginPopup("##snippets")) {
            struct Snip { const char* name; const char* text; };
            static const Snip snips[] = {
                {"start()",   "function start() {\n    \n}\n"},
                {"update(dt)","function update(dt) {\n    \n}\n"},
                {"class",     "public class NewScript : OkaySource {\n    void Start() {\n        \n    }\n    void Update() {\n        \n    }\n}\n"},
                {"if",        "if () {\n    \n}\n"},
                {"if/else",   "if () {\n    \n} else {\n    \n}\n"},
                {"for",       "for (int i = 0; i < 10; i++) {\n    \n}\n"},
                {"foreach",   "foreach (var item in list) {\n    \n}\n"},
                {"while",     "while () {\n    \n}\n"},
                {"on_collision", "function on_collision(other) {\n    \n}\n"},
                {"save/load", "save(\"key\", value);\nvar v = load(\"key\", 0);\n"},
            };
            for (const auto& s : snips)
                if (ImGui::MenuItem(s.name)) caret.insert = s.text;
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto", &caret.autoPairs);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Auto-close brackets/quotes and auto-indent on Enter");

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
        ImGui::SetNextItemWidth(150);
        ImGui::InputTextWithHint("##find", "Find (Ctrl+F)", s_find, sizeof(s_find));
        // Find Next / Prev: scroll to (and select) the next match around the caret.
        // F3 / Shift+F3 do the same while typing in the editor.
        auto jumpMatch = [&](bool fwd) {
            if (!s_find[0]) return;
            std::string hay = buf.data(), needle = s_find;
            for (auto& c : hay) c = (char)std::tolower((unsigned char)c);
            for (auto& c : needle) c = (char)std::tolower((unsigned char)c);
            std::vector<int> at;
            for (std::size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + needle.size()))
                at.push_back((int)p);
            if (at.empty()) return;
            int cur = caret.pos, target = -1;
            if (fwd) { for (int p : at) if (p > cur) { target = p; break; } if (target < 0) target = at.front(); }
            else     { for (int p : at) if (p < cur) target = p; if (target < 0) target = at.back(); }
            caret.gotoPos = target; caret.gotoSelLen = (int)needle.size();
            int ln = 1; for (int k = 0; k < target && hay[k]; ++k) if (hay[k] == '\n') ++ln;
            s_scrollToLine = ln;
        };
        ImGui::SameLine();
        if (ImGui::SmallButton("<")) jumpMatch(false);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Find previous (Shift+F3)");
        ImGui::SameLine();
        if (ImGui::SmallButton(">")) jumpMatch(true);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Find next (F3)");
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            ImGui::IsKeyPressed(ImGuiKey_F3, false))
            jumpMatch(!ImGui::GetIO().KeyShift);
        // Replace field + Replace All (case-sensitive). Clicking here deactivates
        // the editor, so editing the buffer directly takes effect.
        static char s_replace[128] = "";
        ImGui::SameLine(); ImGui::SetNextItemWidth(150);
        ImGui::InputTextWithHint("##replace", "Replace", s_replace, sizeof(s_replace));
        ImGui::SameLine();
        if (ImGui::SmallButton("Replace All") && s_find[0]) {
            std::string text = buf.data(), from = s_find, to = s_replace;
            int n = 0;
            for (std::size_t p = text.find(from); p != std::string::npos && !from.empty();
                 p = text.find(from, p + to.size())) { text.replace(p, from.size(), to); ++n; }
            if (n > 0) { SetCodeBuffer(sc, text); ed.dirty = true; }
            ConsoleLog("Replaced " + std::to_string(n) + " occurrence" + (n == 1 ? "" : "s"));
        }
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

        // --- Command Palette (Ctrl+Shift+P): searchable editor actions + go-to ---
        static char s_palQuery[64] = "";
        static bool s_palFocus = false;
        if (s_palReq ||
            (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
             ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_P, false))) {
            s_palReq = false; s_palQuery[0] = '\0'; s_palFocus = true; ImGui::OpenPopup("##cmdpalette");
        }
        if (ImGui::BeginPopup("##cmdpalette")) {
            struct Cmd { const char* name; std::function<void()> run; };
            std::vector<Cmd> cmds = {
                {"Run (compile & execute)", [&]{ bool ok = sc->LoadSource(buf.data(), &s_error); if (ok) s_error.clear(); ed.dirty = true; }},
                {"Save", [&]{ std::string p = filePath(); if (extide::WriteFile(p, buf.data())) { sc->SetPath(p); g_scriptSaved[sc] = buf.data(); } ed.dirty = true; }},
                {"Format Document", [&]{ SetCodeBuffer(sc, FormatOkayScript(buf.data())); ed.dirty = true; }},
                {"Reload from disk", [&]{ if (!sc->Path().empty()) { std::string s = extide::ReadFile(sc->Path()); SetCodeBuffer(sc, s); std::string e; sc->LoadSource(s, &e); g_scriptSaved[sc] = s; } }},
                {"Toggle Syntax Highlight", [&]{ s_highlight = !s_highlight; }},
                {"Toggle Minimap", [&]{ s_minimap = !s_minimap; }},
                {"Toggle Comment Line", [&]{ caret.toggleComment = true; }},
                {"Open Scripting Docs", [&]{ g_showScriptDocs = true; }},
                {"Open in External IDE", [&]{ std::string p = filePath(); extide::WriteFile(p, buf.data()); sc->SetPath(p); extide::OpenExternal(p); }},
                {"Zoom In", [&]{ s_zoom = Mathf::Clamp(s_zoom + 0.1f, 0.7f, 3.0f); }},
                {"Zoom Out", [&]{ s_zoom = Mathf::Clamp(s_zoom - 0.1f, 0.7f, 3.0f); }},
            };
            if (s_palFocus) { ImGui::SetKeyboardFocusHere(); s_palFocus = false; }
            ImGui::SetNextItemWidth(340);
            ImGui::InputTextWithHint("##palq", "Type a command or symbol...", s_palQuery, sizeof(s_palQuery));
            std::string q = s_palQuery; for (auto& c : q) c = (char)std::tolower((unsigned char)c);
            auto matches = [&](const std::string& label) {
                if (q.empty()) return true;
                std::string l = label; for (auto& c : l) c = (char)std::tolower((unsigned char)c);
                return l.find(q) != std::string::npos;
            };
            ImGui::BeginChild("##palscroll", ImVec2(340, 220));
            for (auto& cm : cmds)
                if (matches(cm.name) && ImGui::Selectable(cm.name)) { cm.run(); ImGui::CloseCurrentPopup(); }
            // Go to any function/class in the file (symbol search).
            for (const auto& s : ScriptOutline(buf.data())) {
                std::string label = "Go to  " + s.second;
                if (matches(label) && ImGui::Selectable(label.c_str())) {
                    caret.gotoLine = s.first; s_scrollToLine = s.first; ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }

        // --- Editor surface: dark theme, line-number gutter, syntax overlay ---
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
        const float mmW = 92.0f;                       // minimap column width
        float editW = av.x - (s_minimap ? mmW + 4.0f : 0.0f);
        if (editW < 120.0f) editW = av.x;              // too narrow: skip the minimap
        bool showMap = s_minimap && editW < av.x;
        float mmScrollY = 0.0f, mmVisibleH = av.y;     // captured from inside the child
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 30, 30, 255));   // VS Code editor bg
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(30, 30, 30, 255));
        ImGui::PushStyleColor(ImGuiCol_Text,    IM_COL32(212, 212, 212, 255));
        ImGui::BeginChild("editorscroll", ImVec2(editW, av.y), true,
                          ImGuiWindowFlags_HorizontalScrollbar);

        // Zoom: scale the editor font, then recompute glyph metrics so the gutter
        // and the syntax overlay stay aligned at any zoom (Ctrl+wheel or A-/A+).
        ImGui::SetWindowFontScale(s_zoom);
        if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f)
            s_zoom = Mathf::Clamp(s_zoom + ImGui::GetIO().MouseWheel * 0.1f, 0.7f, 3.0f);
        charW = ImGui::CalcTextSize("0").x;
        lineH = ImGui::GetTextLineHeight();

        // Scroll to a Go-to-line request (center the target line in the view).
        if (s_scrollToLine > 0) {
            ImGui::SetScrollY((s_scrollToLine - 1) * lineH - av.y * 0.4f);
            s_scrollToLine = 0;
        }

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
        // A snippet chosen from the toolbar menu steals focus from the editor, so
        // the callback won't run — splice it into the buffer at the caret here.
        if (!caret.insert.empty() && !ImGui::IsItemActive()) {
            std::string s(buf.data());
            int p = caret.pos < 0 ? 0 : (caret.pos > (int)s.size() ? (int)s.size() : caret.pos);
            s.insert((std::size_t)p, caret.insert);
            SetCodeBuffer(sc, s); caret.insert.clear(); ed.dirty = true;
        }
        ImVec2 mn = ImGui::GetItemRectMin();
        ImVec2 origin(mn.x + padX, mn.y + padY);
        ImDrawList* edl = ImGui::GetWindowDrawList();
        // Current-line highlight (the row the caret is on), VS Code-style.
        {
            float ly = origin.y + (caret.line - 1) * lineH;
            edl->AddRectFilled(ImVec2(mn.x, ly), ImVec2(mn.x + contentW, ly + lineH),
                               IM_COL32(255, 255, 255, 16));
        }
        // Indent guides: faint vertical lines at each 4-space level (VS Code).
        {
            const char* t = buf.data();
            int ln = 0;
            for (int i = 0;;) {
                int col = 0, j = i;
                while (t[j] == ' ' || t[j] == '\t') { col += (t[j] == '\t' ? 4 : 1); ++j; }
                if (t[j] != '\n' && t[j] != '\0') {        // skip blank lines
                    float y0 = origin.y + ln * lineH, y1 = y0 + lineH;
                    for (int g = 4; g < col; g += 4) {
                        float gx = origin.x + g * charW;
                        edl->AddLine(ImVec2(gx, y0), ImVec2(gx, y1), IM_COL32(255, 255, 255, 18));
                    }
                }
                while (t[i] && t[i] != '\n') ++i;
                if (!t[i]) break;
                ++i; ++ln;
            }
        }
        // Highlight all occurrences of the identifier under the caret (VS Code).
        {
            const char* t = buf.data();
            int tlen = (int)std::strlen(t);
            auto isW = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
            int p = caret.pos < 0 ? 0 : (caret.pos > tlen ? tlen : caret.pos);
            int ws = p, we = p;
            while (ws > 0 && isW(t[ws - 1])) --ws;
            while (we < tlen && isW(t[we])) ++we;
            std::string word(t + ws, t + we);
            if (word.size() >= 2 && (std::isalpha((unsigned char)word[0]) || word[0] == '_')) {
                int ln = 0, col = 0;
                for (int i = 0; i < tlen;) {
                    if (t[i] == '\n') { ++ln; col = 0; ++i; continue; }
                    bool wb = (i == 0 || !isW(t[i - 1]));
                    if (wb && std::strncmp(t + i, word.c_str(), word.size()) == 0 &&
                        !isW(t[i + word.size()])) {
                        ImVec2 a(origin.x + col * charW, origin.y + ln * lineH);
                        edl->AddRectFilled(a, ImVec2(a.x + word.size() * charW, a.y + lineH),
                                           IM_COL32(120, 145, 175, 55));
                        i += (int)word.size(); col += (int)word.size(); continue;
                    }
                    ++i; ++col;
                }
            }
        }
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

        // Inline diagnostic: a red wavy underline under the error line (the
        // compiler now prefixes "line N:" to parse/compile errors).
        int errLine = (!s_error.empty() && s_error.rfind("line ", 0) == 0)
                      ? std::atoi(s_error.c_str() + 5) : 0;
        if (errLine > 0 && errLine <= lines) {
            const char* t = buf.data();
            int cur = 1, len = 0;
            for (int i = 0; t[i]; ++i) {
                if (t[i] == '\n') { if (cur == errLine) break; ++cur; len = 0; }
                else if (cur == errLine) ++len;
            }
            if (len < 1) len = 1;
            float y = origin.y + errLine * lineH - 1.5f;
            float x1 = origin.x + len * charW;
            ImU32 red = IM_COL32(240, 80, 80, 230);
            bool up = true;
            for (float x = origin.x; x < x1; x += 3.0f, up = !up) {
                float xe = x + 3.0f > x1 ? x1 : x + 3.0f;
                edl->AddLine(ImVec2(x, y + (up ? 2.0f : 0.0f)),
                             ImVec2(xe, y + (up ? 0.0f : 2.0f)), red, 1.3f);
            }
        }

        // Bracket matching: when the caret sits next to a ()[]{} bracket, box it
        // and its partner so nesting is easy to read.
        {
            const char* tx = buf.data();
            int tlen = (int)std::strlen(tx);
            auto opener = [](char x){ return x=='('||x=='['||x=='{'; };
            auto closer = [](char x){ return x==')'||x==']'||x=='}'; };
            auto match = [](char x)->char{ return x=='('?')':x=='['?']':x=='{'?'}':x==')'?'(':x==']'?'[':'{'; };
            int bpos = -1;
            if (caret.pos > 0 && caret.pos - 1 < tlen && (opener(tx[caret.pos-1]) || closer(tx[caret.pos-1]))) bpos = caret.pos - 1;
            else if (caret.pos < tlen && (opener(tx[caret.pos]) || closer(tx[caret.pos]))) bpos = caret.pos;
            if (bpos >= 0) {
                char b = tx[bpos], m = match(b);
                int dir = opener(b) ? 1 : -1, depth = 0, other = -1;
                for (int i = bpos; i >= 0 && i < tlen; i += dir) {
                    if (tx[i] == b) ++depth; else if (tx[i] == m) { if (--depth == 0) { other = i; break; } }
                }
                auto box = [&](int p) {
                    int ln = 0, col = 0;
                    for (int i = 0; i < p && i < tlen; ++i) { if (tx[i]=='\n'){++ln;col=0;} else ++col; }
                    ImVec2 a(origin.x + col*charW, origin.y + ln*lineH);
                    edl->AddRect(a, ImVec2(a.x + charW, a.y + lineH), IM_COL32(120,180,120,220));
                };
                box(bpos);
                if (other >= 0) box(other);
            }
        }
        // Caret screen position (for the autocomplete popup, drawn after the child).
        ImVec2 caretScreen(origin.x + (caret.col - 1) * charW, origin.y + caret.line * lineH);
        mmScrollY = ImGui::GetScrollY();
        ImGui::EndChild();
        ImGui::PopStyleColor(3);

        // --- Minimap: a scaled overview of the file pinned beside the editor.
        // Each line is a bar (length ~ its content); a box marks the visible
        // region; click/drag to scroll there. Built from the live buffer.
        if (showMap) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(24, 24, 24, 255));
            ImGui::BeginChild("##minimap", ImVec2(mmW, av.y), true,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImDrawList* mdl = ImGui::GetWindowDrawList();
            ImVec2 mp = ImGui::GetWindowPos();
            float mh = av.y, innerW = mmW - 8.0f;
            float step = lines > 0 ? mh / lines : mh;     // vertical px per source line
            if (step > 3.0f) step = 3.0f;                 // cap so big files still fit
            float usedH = step * lines; if (usedH > mh) usedH = mh;
            // Per-line bars, colored by a cheap content sniff (comment/string/code).
            const char* t = buf.data();
            int ln = 0; const char* ls = t;
            auto drawLine = [&](const char* s, const char* e, int idx) {
                int lead = 0; const char* p = s;
                while (p < e && (*p == ' ' || *p == '\t')) { lead += (*p == '\t' ? 4 : 1); ++p; }
                int len = (int)(e - p); if (len <= 0) return;
                ImU32 col = IM_COL32(150, 152, 160, 200);
                if (len >= 2 && p[0] == '/' && p[1] == '/') col = IM_COL32(106, 153, 85, 200);
                float y = mp.y + 4.0f + idx * step;
                if (y > mp.y + mh - 2.0f) return;
                float x0 = mp.x + 4.0f + (lead * 0.5f);
                float w = len * 0.5f; if (x0 - mp.x + w > innerW) w = innerW - (x0 - mp.x);
                if (w < 1.0f) w = 1.0f;
                mdl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + w, y + (step > 1.5f ? 1.5f : step * 0.8f)), col);
            };
            for (const char* p = t;; ++p) {
                if (*p == '\n' || *p == '\0') { drawLine(ls, p, ln); ++ln; ls = p + 1; if (*p == '\0') break; }
            }
            // Visible-region box.
            float total = (float)lines;
            if (total > 0) {
                float top = (mmScrollY / lineH) / total;
                float vis = (mmVisibleH / lineH) / total;
                float y0 = mp.y + 4.0f + top * usedH;
                float y1 = y0 + vis * usedH;
                if (y1 > mp.y + mh - 2.0f) y1 = mp.y + mh - 2.0f;
                mdl->AddRectFilled(ImVec2(mp.x + 1, y0), ImVec2(mp.x + mmW - 1, y1), IM_COL32(255, 255, 255, 24));
                mdl->AddRect(ImVec2(mp.x + 1, y0), ImVec2(mp.x + mmW - 1, y1), IM_COL32(255, 255, 255, 70));
            }
            // Click or drag to scroll: map the cursor's Y to a line and center it.
            if (ImGui::IsWindowHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                float fy = (ImGui::GetIO().MousePos.y - (mp.y + 4.0f)) / (usedH > 1 ? usedH : 1);
                int target = (int)(fy * lines) + 1;
                if (target < 1) target = 1; if (target > lines) target = lines;
                s_scrollToLine = target;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        // --- Autocomplete: a click-to-insert suggestion list at the caret -------
        {
            // The word being typed: trailing [A-Za-z0-9_] before the caret.
            const char* tx = buf.data();
            int p = caret.pos, ws = p;
            auto isWord = [](char x){ return std::isalnum((unsigned char)x) || x == '_'; };
            while (ws > 0 && tx[ws - 1] && isWord(tx[ws - 1])) --ws;
            std::string prefix(tx + ws, tx + p);
            // Member mode: if the word is preceded by "<receiver>.", complete the
            // members of that receiver (e.g. transform.|  ->  position, Rotate, …).
            std::string receiver;
            if (ws > 0 && tx[ws - 1] == '.') {
                int rs = ws - 1;
                while (rs > 0 && isWord(tx[rs - 1])) --rs;
                receiver.assign(tx + rs, tx + (ws - 1));
            }
            const std::vector<std::string>& members = ScriptMembers(receiver);
            bool memberMode = !receiver.empty() && !members.empty();
            // Members list from the first keystroke (or right after the dot); plain
            // words still need 2+ chars so the popup doesn't fire constantly.
            if (memberMode || prefix.size() >= 2) {
                std::string lp = prefix; for (auto& ch : lp) ch = (char)std::tolower((unsigned char)ch);
                std::vector<const std::string*> hits;
                const std::vector<std::string>& pool = memberMode ? members : ScriptCompletions();
                for (const auto& w : pool) {
                    if (w.size() < prefix.size()) continue;
                    if (!memberMode && w.size() == prefix.size()) continue;  // exact word: nothing to add
                    std::string lw = w; for (auto& ch : lw) ch = (char)std::tolower((unsigned char)ch);
                    if (lw.compare(0, lp.size(), lp) == 0) hits.push_back(&w);
                    if (hits.size() >= 12) break;
                }
                if (!hits.empty()) {
                    ImGui::SetNextWindowPos(ImVec2(caretScreen.x, caretScreen.y + 2));
                    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(40, 40, 46, 245));
                    if (ImGui::Begin("##autocomplete", nullptr,
                            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                            ImGuiWindowFlags_NoSavedSettings)) {
                        for (const std::string* w : hits) {
                            if (ImGui::Selectable(w->c_str()))
                                caret.insert = w->substr(prefix.size());   // spliced when editor inactive
                        }
                    }
                    ImGui::End();
                    ImGui::PopStyleColor();
                }
            }
        }

        // --- Status bar (VS Code-style) --------------------------------------
        const char* langLbl = sc->Language() == "okayscript" ? "OkayScript"
                            : sc->Language() == "lua" ? "Lua" : sc->Language().c_str();
        char selInfo[32] = "";
        if (caret.selLen > 0) std::snprintf(selInfo, sizeof(selInfo), " (%d sel)", caret.selLen);
        ImGui::TextDisabled("%s   Ln %d, Col %d%s   %d lines   %d chars   Spaces", langLbl,
                            caret.line, caret.col, selInfo, lines, (int)std::strlen(buf.data()));
        if (ScriptTabDirty(sc)) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.45f, 1.0f), "  \xe2\x97\x8f modified");
        }
        if (!s_error.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "  \xe2\x9c\x97 %s", s_error.c_str());
            int eln = (s_error.rfind("line ", 0) == 0) ? std::atoi(s_error.c_str() + 5) : 0;
            if (eln > 0 && ImGui::IsItemClicked()) { caret.gotoLine = eln; s_scrollToLine = eln; }
            if (eln > 0 && ImGui::IsItemHovered()) ImGui::SetTooltip("Click to go to line %d", eln);
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
        if (ed.Save(p)) { ConsoleLog("Saved " + p); ed.Achievement("FIRST_SAVE");
            std::error_code rc; std::filesystem::remove(p + ".autosave", rc); }
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
    // Play/Stop toggle is Ctrl+P (Unity-style). It used to be plain Space, which
    // clashed with the in-game jump — pressing Space in play mode stopped the game.
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) {
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
    if (g_showImportObj) { ImGui::OpenPopup("Import Model"); g_showImportObj = false; }
    ImVec2 c = ImGui::GetMainViewport()->GetCenter();

    ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Import Model", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Import a Wavefront .OBJ (with its .mtl + texture next to it).");
        ImGui::TextDisabled("Tip: export from MakeHuman / Mixamo / Blender as OBJ.");
        ImGui::InputText("Path##obj", g_objPathBuf, sizeof(g_objPathBuf));
        if (ImGui::Button("Import", ImVec2(120, 0))) {
            bool okl = false; std::string tex;
            Mesh im = Mesh::LoadOBJ(g_objPathBuf, &okl, &tex);
            if (okl && im.TriangleCount() > 0) {
                std::string nm = g_objPathBuf;
                std::size_t sl = nm.find_last_of("/\\"); if (sl != std::string::npos) nm = nm.substr(sl + 1);
                std::size_t dot = nm.find_last_of('.'); if (dot != std::string::npos) nm = nm.substr(0, dot);
                GameObject* go = ed.CreateEmpty(nm.empty() ? "Model" : nm);
                auto* mr = go->AddComponent<MeshRenderer>();
                mr->mesh = im; mr->meshPath = g_objPathBuf; mr->doubleSided = true;
                if (!tex.empty()) mr->texture = tex;
                ed.Select(go); ed.view3D = true; ed.dirty = true;
                ConsoleLog("Imported " + std::string(g_objPathBuf) + " (" +
                           std::to_string(im.TriangleCount()) + " tris" +
                           (tex.empty() ? ")" : ", textured)"));
            } else ConsoleLog("Import failed: couldn't read " + std::string(g_objPathBuf), 2);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

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
    ImGui::SetNextWindowSize(ImVec2(760, 580), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(560, 420), ImVec2(2000, 1600));
    if (ImGui::BeginPopupModal("New Project")) {   // resizable
        static char nameBuf[128] = "MyGame";
        static char locBuf[400]  = ".";

        // Template registry: category, title, one-line blurb, longer description,
        // and the factory. The grid + footer are driven from this.
        struct Tpl { int cat; const char* title; const char* blurb; const char* desc;
                     void (EditorState::*fn)(); };
        enum { C_BLANK, C_3D, C_2D, C_GAME, C_UI };
        static const Tpl tpls[] = {
            {C_BLANK, "2D Scene",   "Empty 2D + camera",  "An empty 2D scene with a camera. Good starting point for sprites and UI.", &EditorState::NewScene2D},
            {C_BLANK, "3D Scene",   "Empty 3D + cube",    "An empty 3D scene with a camera, a directional light and a cube.", &EditorState::NewScene3D},
            {C_BLANK, "Empty",      "Nothing at all",     "A totally empty scene (no camera or light). Build it up yourself.", &EditorState::NewScene},
            {C_3D,    "First Person","Character + FPS",    "A blocky Character you control in first person: mouse-look, WASD, jump. Camera at eye height, with crates to walk around.", &EditorState::NewFPS},
            {C_3D,    "Third Person","Orbit camera",       "Your blocky Character with an orbit camera behind it: WASD relative to the camera, Space to jump, with walk/run animation. You see and control the character.", &EditorState::NewThirdPerson},
            {C_3D,    "Point & Click","Click to move",     "RuneScape / Diablo style: click the ground and your Character walks there, under a high angled camera. Powered by the Click To Move controller.", &EditorState::NewPointAndClick},
            {C_2D,    "Platformer",  "Side-scroller",      "A side-scroller: follow camera, a physics player on a wide ground, and a coin.", &EditorState::NewPlatformer},
            {C_2D,    "Top-Down",    "WASD movement",      "A WASD-driven player with a follow camera and a couple of walls.", &EditorState::NewTopDown},
            {C_GAME,  "Coin Collector","Mini game",        "A small complete game: drive the player to collect spinning coins; a HUD counts the score.", &EditorState::NewCoinCollector},
            {C_GAME,  "Snake",       "Classic arcade",     "The classic Snake, fully playable.", &EditorState::NewSnake},
            {C_UI,    "Main Menu",   "Title screen",       "A title screen with buttons wired to actions.", &EditorState::NewMainMenu},
            {C_UI,    "Inventory",   "Drag & drop grid",   "A drag & drop item grid.", &EditorState::NewInventory},
            {C_UI,    "Multiplayer", "Host / join",        "A host/join networked starter scene.", &EditorState::NewMultiplayer},
        };
        const int N = (int)(sizeof(tpls) / sizeof(tpls[0]));
        static int sel = 1;   // default: 3D Scene
        if (sel < 0 || sel >= N) sel = 1;
        auto catColor = [](int c) -> ImVec4 {
            switch (c) { case C_3D:   return ImVec4(0.30f, 0.55f, 0.85f, 1);
                         case C_2D:   return ImVec4(0.30f, 0.70f, 0.45f, 1);
                         case C_GAME: return ImVec4(0.90f, 0.65f, 0.25f, 1);
                         case C_UI:   return ImVec4(0.62f, 0.45f, 0.80f, 1);
                         default:     return ImVec4(0.45f, 0.47f, 0.55f, 1); }
        };
        const char* catName[] = {"Blank canvas", "3D templates", "2D templates",
                                 "Mini games", "UI & systems"};

        ImGui::TextColored(ImVec4(0.95f, 0.95f, 1.0f, 1), "Create a New Project");
        ImGui::TextDisabled("Pick a starting template, name it, and hit Create.");
        ImGui::Spacing();

        // ---- Card grid (scrollable; grows with the window) ----
        float gridH = ImGui::GetContentRegionAvail().y - 196.0f;   // leave room for desc + fields + footer
        if (gridH < 120.0f) gridH = 120.0f;
        ImGui::BeginChild("tpl_grid", ImVec2(0, gridH), true);
        const float CARD_W = 150.0f, CARD_H = 60.0f;
        const int COLS = 4;
        for (int cat = 0; cat <= C_UI; ++cat) {
            ImGui::PushStyleColor(ImGuiCol_Text, catColor(cat));
            ImGui::SeparatorText(catName[cat]);
            ImGui::PopStyleColor();
            int col = 0;
            for (int i = 0; i < N; ++i) {
                if (tpls[i].cat != cat) continue;
                if (col > 0) ImGui::SameLine();
                ImGui::PushID(i);
                bool selected = (sel == i);
                ImVec4 ac = catColor(cat);
                ImVec4 base = selected ? ImVec4(ac.x, ac.y, ac.z, 0.55f) : ImVec4(ac.x, ac.y, ac.z, 0.18f);
                ImGui::PushStyleColor(ImGuiCol_Button, base);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(ac.x, ac.y, ac.z, 0.40f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(ac.x, ac.y, ac.z, 0.65f));
                if (selected) { ImGui::PushStyleColor(ImGuiCol_Border, ac); ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f); }
                std::string label = std::string(tpls[i].title) + "\n";
                if (ImGui::Button((label + tpls[i].blurb + "##c").c_str(), ImVec2(CARD_W, CARD_H))) sel = i;
                if (selected) { ImGui::PopStyleColor(); ImGui::PopStyleVar(); }
                ImGui::PopStyleColor(3);
                ImGui::PopID();
                if (++col >= COLS) col = 0;
            }
        }
        ImGui::EndChild();

        // ---- Selected template description ----
        ImGui::TextColored(catColor(tpls[sel].cat), "%s", tpls[sel].title);
        ImGui::SameLine(); ImGui::TextDisabled("— %s", tpls[sel].blurb);
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::TextWrapped("%s", tpls[sel].desc);
        ImGui::PopTextWrapPos();
        ImGui::Spacing(); ImGui::Separator();

        // ---- Name / Location ----
        ImGui::SetNextItemWidth(300); ImGui::InputText("Name", nameBuf, sizeof(nameBuf));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1); ImGui::InputText("Location", locBuf, sizeof(locBuf));
        ImGui::TextDisabled("Creates <Location>/<Name>/ with an Assets/ folder and the starting scene.");
        ImGui::Spacing();

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

        // ---- Footer buttons ----
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.30f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.68f, 0.38f, 1));
        if (ImGui::Button("Create Project", ImVec2(170, 32))) { (ed.*tpls[sel].fn)(); finishProject(); }
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 32))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// Offer to restore a newer "<scene>.autosave" recovery copy after a crash or
// unclean exit. Recover loads it (and overwrites the scene); Discard removes it.
void DrawRecoveryPopup(EditorState& ed) {
    if (!g_recoverPath.empty() && !ImGui::IsPopupOpen("Recover Autosave"))
        ImGui::OpenPopup("Recover Autosave");
    if (ImGui::BeginPopupModal("Recover Autosave", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("A newer autosave was found for this scene:");
        ImGui::TextDisabled("%s", g_recoverPath.c_str());
        ImGui::TextWrapped("It looks like the editor didn't close cleanly. Recover it?");
        ImGui::Separator();
        if (ImGui::Button("Recover", ImVec2(130, 0))) {
            std::error_code rc;
            std::string scene = ed.path();
            std::filesystem::copy_file(g_recoverPath, scene,
                std::filesystem::copy_options::overwrite_existing, rc);
            std::string err;
            if (!rc && ed.Load(scene, &err)) { ed.dirty = true; ConsoleLog("Recovered autosave"); }
            else ConsoleLog("Recover failed: " + (rc ? rc.message() : err));
            std::filesystem::remove(g_recoverPath, rc);
            g_recoverPath.clear(); ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(130, 0))) {
            std::error_code rc; std::filesystem::remove(g_recoverPath, rc);
            g_recoverPath.clear(); ImGui::CloseCurrentPopup();
        }
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

void DrawUpdatePopup(EditorState& ed) {
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
            if (ImGui::Checkbox("Automatically install future updates", &g_autoUpdate)) SaveSettings();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("On launch, save your work and update on its own —\nno need to confirm next time.");
            if (!g_updateStatus.empty())
                ImGui::TextWrapped("%s", g_updateStatus.c_str());
            ImGui::BeginDisabled(g_installingUpdate);
            if (ImGui::Button(g_installingUpdate ? "Installing..." : "Download & Install",
                              ImVec2(180, 0))) {
                g_installingUpdate = true;
                g_updateStatus = "Downloading v" + u.latest + "...";
                // Save first so nothing is lost when the new build relaunches.
                SaveAllBeforeExit(ed);
                g_updateStatus = updater::InstallUpdate(u.latest, u.ref);
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
static bool g_hierSort   = false;            // sort siblings A->Z (Unity's alpha sort)
static int  g_hierExpand = 0;                // 1=expand-all, 2=collapse-all (one frame)

// Component "icon" badges at a Hierarchy row's right edge (like Unity showing
// component icons): a quick read of what's attached. Drawn via the draw list so
// they don't interfere with the row's own click/drag handling.
static void HierComponentBadges(GameObject* go, ImVec2 rowMin, ImVec2 rowMax) {
    struct Badge { bool on; ImU32 col; const char* s; };
    bool hasRb = go->GetComponent<Rigidbody2D>() != nullptr ||
                 go->GetComponent<Rigidbody3D>() != nullptr;
    bool hasCol = go->GetComponent<BoxCollider2D>() != nullptr ||
                  go->GetComponent<CircleCollider2D>() != nullptr;
    Badge badges[] = {
        { !go->GetComponents<ScriptComponent>().empty(), IM_COL32(150, 140, 235, 255), "S" },
        { go->GetComponent<Light>()    != nullptr,       IM_COL32(235, 205, 90, 255),  "L" },
        { hasRb,                                         IM_COL32(235, 140, 70, 255),  "R" },
        { hasCol,                                        IM_COL32(120, 200, 130, 255), "C" },
        { go->GetComponent<Animator>() != nullptr,       IM_COL32(110, 200, 160, 255), "A" },
        { go->GetComponent<ParticleSystem>() != nullptr, IM_COL32(110, 200, 235, 255), "P" },
    };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = rowMax.x - 4.0f, cy = (rowMin.y + rowMax.y) * 0.5f;
    for (const Badge& b : badges) {
        if (!b.on) continue;
        const float w = 13.0f;
        ImVec2 mn(x - w, cy - 7.0f), mx(x, cy + 7.0f);
        dl->AddRectFilled(mn, mx, (b.col & 0x00FFFFFF) | 0x55000000, 3.0f);
        dl->AddText(ImVec2(mn.x + 3.5f, mn.y - 1.0f), b.col, b.s);
        x -= (w + 3.0f);
    }
}

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
    if (ImGui::SmallButton("Expand"))   g_hierExpand = 1;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Expand all");
    ImGui::SameLine();
    if (ImGui::SmallButton("Collapse")) g_hierExpand = 2;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Collapse all");
    ImGui::SameLine();
    if (ImGui::SmallButton(g_hierSort ? "Sort: A-Z" : "Sort: None")) g_hierSort = !g_hierSort;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle alphabetical sorting of siblings");
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
            ImGui::PushID(go);
            if (ImGui::Selectable((std::string(ObjectKind(go)) + go->name).c_str(), sel))
                ed.Select(go);
            HierComponentBadges(go, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::PopID();
        }
        ImGui::End();
        return;
    }

    const auto& objs = ed.scene().Objects();
    // Roots only (children render under their parent); optionally A->Z sorted.
    std::vector<GameObject*> roots;
    for (const auto& up : objs)
        if (up->transform->Parent() == nullptr) roots.push_back(up.get());
    if (g_hierSort)
        std::sort(roots.begin(), roots.end(), [](GameObject* a, GameObject* b) {
            return a->name < b->name;
        });
    for (GameObject* go : roots) {
        std::function<void(GameObject*)> drawNode = [&](GameObject* node) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                       ImGuiTreeNodeFlags_DefaultOpen |
                                       ImGuiTreeNodeFlags_SpanAvailWidth;
            if (node == ed.selected()) flags |= ImGuiTreeNodeFlags_Selected;
            int childCount = node->transform->ChildCount();
            if (childCount == 0) flags |= ImGuiTreeNodeFlags_Leaf;
            // Expand All / Collapse All applies to nodes that actually have children.
            if (g_hierExpand && childCount > 0) ImGui::SetNextItemOpen(g_hierExpand == 1);
            // Unity dims inactive objects; grey the whole row (and its subtree label).
            bool dim = !node->active;
            if (dim) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
            // Show a child count on parents (Unity-style), plus an (off) marker.
            char cnt[16] = ""; if (childCount > 0) std::snprintf(cnt, sizeof(cnt), "  (%d)", childCount);
            bool open = ImGui::TreeNodeEx(node, flags, "%s%s%s%s", ObjectKind(node),
                                          node->name.c_str(), cnt, node->active ? "" : "  (off)");
            if (dim) ImGui::PopStyleColor();
            ImVec2 rowMin = ImGui::GetItemRectMin(), rowMax = ImGui::GetItemRectMax();
            HierComponentBadges(node, rowMin, rowMax);
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
                std::vector<Transform*> kids(node->transform->Children().begin(),
                                             node->transform->Children().end());
                if (g_hierSort)
                    std::sort(kids.begin(), kids.end(), [](Transform* a, Transform* b) {
                        return a->gameObject->name < b->gameObject->name;
                    });
                for (Transform* child : kids) drawNode(child->gameObject);
                ImGui::TreePop();
            }
        };
        drawNode(go);
    }
    g_hierExpand = 0;   // expand/collapse-all is a one-frame request
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

// A public field discovered in a script: its name, declared type, default value,
// and any Unity attributes (Header/Tooltip/Range). Used to show Unity-style
// serialized fields in the Script inspector.
struct ScriptField {
    std::string name, type, def;
    std::string header, tooltip;          // [Header("..")] / [Tooltip("..")]
    bool  hasRange = false; float rmin = 0, rmax = 1;   // [Range(min,max)]
};

// Pull the text inside an attribute, e.g. attrArg("[Header(\"Stats\")]", "Header").
static bool ScriptAttrArg(const std::string& line, const char* attr, std::string& out) {
    std::string key = std::string("[") + attr;
    auto p = line.find(key);
    if (p == std::string::npos) return false;
    auto lp = line.find('(', p); auto rp = line.find(')', lp);
    if (lp == std::string::npos || rp == std::string::npos) { out.clear(); return true; }
    out = line.substr(lp + 1, rp - lp - 1);
    // strip surrounding quotes/space
    auto a = out.find_first_not_of(" \t\"'"); auto b = out.find_last_not_of(" \t\"'");
    out = (a == std::string::npos) ? "" : out.substr(a, b - a + 1);
    return true;
}

// Scan script source for public/top-level field declarations: lines with `=` at
// brace depth 0 (classic top-level) or 1 (inside a `class` body, Unity style),
// where the left side is an identifier (optionally typed / public / attributed)
// and not a function call. Locals inside function bodies (deeper) are ignored.
static std::vector<ScriptField> ParseScriptFields(const std::string& src) {
    std::vector<ScriptField> out;
    std::unordered_set<std::string> seen;
    static const std::unordered_set<std::string> kw = {
        "return","if","else","while","for","switch","case","do","break","continue"};
    int depth = 0;
    // Attributes attach to the NEXT field (Unity puts them on the line above).
    std::string pendH, pendT; bool pendR = false; float pendRmin = 0, pendRmax = 1;
    std::istringstream in(src);
    std::string line;
    while (std::getline(in, line)) {
        int startDepth = depth;
        std::string code = line;
        if (auto cm = code.find("//"); cm != std::string::npos) code = code.substr(0, cm);
        if (startDepth <= 1) {
            // Capture [Header("..")] / [Tooltip("..")] / [Range(a,b)] (standalone or inline).
            std::string av;
            if (ScriptAttrArg(code, "Header", av))  pendH = av;
            if (ScriptAttrArg(code, "Tooltip", av)) pendT = av;
            if (ScriptAttrArg(code, "Range", av)) {
                for (char& c : av) if (c == ',') c = ' ';
                std::istringstream is(av); pendR = true; is >> pendRmin >> pendRmax;
            }
            std::size_t eq = code.find('=');
            if (eq != std::string::npos) {
                char before = eq > 0 ? code[eq - 1] : ' ';
                char after  = eq + 1 < code.size() ? code[eq + 1] : ' ';
                bool assign = after != '=' && before != '=' && before != '!' &&
                              before != '<' && before != '>' && before != '+' &&
                              before != '-' && before != '*' && before != '/';
                std::string lhs = code.substr(0, eq);
                if (assign && lhs.find('(') == std::string::npos) {
                    std::string rhs = code.substr(eq + 1);
                    if (auto sc2 = rhs.find(';'); sc2 != std::string::npos) rhs = rhs.substr(0, sc2);
                    // Only PUBLIC vars (or [SerializeField]) show in the inspector;
                    // an undecorated or `private` var is private, so it's hidden.
                    bool hasSerialize = lhs.find("[SerializeField]") != std::string::npos ||
                                        lhs.find("[Serialize") != std::string::npos;
                    // Strip [Attribute] markers from the left side.
                    std::string l2; bool inb = false;
                    for (char c : lhs) { if (c == '[') inb = true; else if (c == ']') inb = false; else if (!inb) l2 += c; }
                    std::vector<std::string> toks; { std::istringstream ls(l2); std::string t; while (ls >> t) toks.push_back(t); }
                    bool hasPublic = false, hasPrivate = false;
                    for (auto& t : toks) { if (t == "public") hasPublic = true; if (t == "private") hasPrivate = true; }
                    if (!toks.empty() && (hasPublic || hasSerialize) && !hasPrivate) {
                        std::string name = toks.back();
                        bool valid = !name.empty() && (std::isalpha((unsigned char)name[0]) || name[0] == '_');
                        for (char c : name) if (!(std::isalnum((unsigned char)c) || c == '_')) valid = false;
                        if (valid && !kw.count(name) && !seen.count(name)) {
                            std::string type;
                            for (auto& t : toks) {
                                if (t == name) break;
                                if (t == "public" || t == "private" || t == "static" ||
                                    t == "const" || t == "readonly" || t == "var") continue;
                                type = t;
                            }
                            auto a = rhs.find_first_not_of(" \t");
                            auto b = rhs.find_last_not_of(" \t");
                            ScriptField f;
                            f.name = name; f.type = type;
                            f.def = a == std::string::npos ? "" : rhs.substr(a, b - a + 1);
                            f.header = pendH; f.tooltip = pendT;
                            f.hasRange = pendR; f.rmin = pendRmin; f.rmax = pendRmax;
                            out.push_back(f);
                            seen.insert(name);
                            pendH.clear(); pendT.clear(); pendR = false;   // consumed
                        }
                    }
                }
            }
        }
        for (char c : code) { if (c == '{') ++depth; else if (c == '}') --depth; }
        if (depth < 0) depth = 0;
    }
    return out;
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
    // Layer dropdown (Unity-style): which of the 32 named layers the object sits on.
    ImGui::SetNextItemWidth(130);
    if (ImGui::BeginCombo("Layer", okay::Layers::Name(go->layer).c_str())) {
        for (int i = 0; i < 32; ++i) {
            std::string nm = okay::Layers::Name(i);
            bool sel = (go->layer == i);
            if (ImGui::Selectable((nm + "##layer" + std::to_string(i)).c_str(), sel)) {
                go->layer = i; ed.dirty = true;
            }
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
                // Always add a new script (objects can hold several, including
                // multiple instances of the same one — like Unity).
                auto* nsc = go->AddComponent<ScriptComponent>("okayscript");
                std::string err; nsc->LoadFile(path, &err); nsc->SetPath(path);
                SetCodeBuffer(nsc, nsc->Source());   // fresh editor buffer for it
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
            // Shader (surface model): Standard lit / Unlit / Toon (cel). Folds the old
            // Unlit flag into the selector; the renderer treats Unlit-shader == unlit.
            int sh = (int)mr->shader;
            if (mr->unlit && mr->shader == MeshRenderer::Shader::Standard) sh = 1;  // legacy unlit flag
            const char* shaders[] = {"Standard", "Unlit", "Toon"};
            if (ImGui::Combo("Shader##mesh", &sh, shaders, 3)) {
                mr->shader = (MeshRenderer::Shader)sh;
                mr->unlit = (mr->shader == MeshRenderer::Shader::Unlit);  // keep legacy flag in sync
                ed.dirty = true;
            }
            if (mr->shader == MeshRenderer::Shader::Toon)
                if (ImGui::SliderInt("Cel Bands##mesh", &mr->toonBands, 2, 6)) ed.dirty = true;
            // Rim (Fresnel) backlight — a per-material glow, great with Toon.
            if (ImGui::SliderFloat("Rim##mesh", &mr->rimStrength, 0.0f, 2.0f)) ed.dirty = true;
            if (mr->rimStrength > 0.0f) {
                if (ImGui::SliderFloat("Rim Power##mesh", &mr->rimPower, 1.0f, 8.0f)) ed.dirty = true;
                float rc[3] = {mr->rimColor.r, mr->rimColor.g, mr->rimColor.b};
                if (ImGui::ColorEdit3("Rim Color##mesh", rc)) { mr->rimColor = {rc[0], rc[1], rc[2], 1.0f}; ed.dirty = true; }
            }
            // Silhouette outline (inverted hull) — pairs with Toon for a cartoon edge.
            if (ImGui::Checkbox("Outline##mesh", &mr->outline)) ed.dirty = true;
            if (mr->outline) {
                if (ImGui::DragFloat("Outline Width##mesh", &mr->outlineWidth, 0.002f, 0.0f, 1.0f)) ed.dirty = true;
                float oc[3] = {mr->outlineColor.r, mr->outlineColor.g, mr->outlineColor.b};
                if (ImGui::ColorEdit3("Outline Color##mesh", oc)) { mr->outlineColor = {oc[0], oc[1], oc[2], 1.0f}; ed.dirty = true; }
            }
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
                float sc[2] = {mr->uvScroll.x, mr->uvScroll.y};
                if (ImGui::DragFloat2("UV Scroll##mesh", sc, 0.01f)) { mr->uvScroll = {sc[0], sc[1]}; ed.dirty = true; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Animated texture: UV units/second (flowing water, lava, belts).");
                if (ImGui::Checkbox("Triplanar##mesh", &mr->triplanar)) ed.dirty = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Project the texture on the 3 world axes (no UV seams; great for terrain/cliffs).\nTiling is the world-space scale.");
            }
            char nmap[260];
            std::strncpy(nmap, mr->normalMap.c_str(), sizeof(nmap) - 1);
            nmap[sizeof(nmap) - 1] = '\0';
            if (ImGui::InputText("Normal Map##mesh", nmap, sizeof(nmap))) { mr->normalMap = nmap; ed.dirty = true; }
            if (AcceptAssetPathField(mr->normalMap)) ed.dirty = true;   // drop from Project
            if (!mr->normalMap.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear##nmap")) { mr->normalMap.clear(); ed.dirty = true; }
                if (ImGui::SliderFloat("Bump Strength##mesh", &mr->normalStrength, 0.0f, 2.0f)) ed.dirty = true;
            }
            if (ImGui::SliderFloat("Reflectivity##mesh", &mr->reflectivity, 0.0f, 1.0f)) ed.dirty = true;
            if (ImGui::SliderFloat("Metallic##mesh", &mr->metallic, 0.0f, 1.0f)) ed.dirty = true;
            char smap[260];
            std::strncpy(smap, mr->specularMap.c_str(), sizeof(smap) - 1);
            smap[sizeof(smap) - 1] = '\0';
            if (ImGui::InputText("Specular Map##mesh", smap, sizeof(smap))) { mr->specularMap = smap; ed.dirty = true; }
            if (AcceptAssetPathField(mr->specularMap)) ed.dirty = true;   // drop from Project
            if (!mr->specularMap.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear##smap")) { mr->specularMap.clear(); ed.dirty = true; }
            }
            ImGui::TextDisabled("%d verts, %d triangles",
                                (int)mr->mesh.vertices.size(), mr->mesh.TriangleCount());
            if (ImGui::SmallButton("Subdivide##mesh")) { mr->mesh.Subdivide(); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::SmallButton("Subdiv+Smooth##mesh")) {  // low-poly -> high-poly (rounds it)
                mr->mesh.SubdivideSmooth(1, 0.5f); ed.dirty = true;
                ConsoleLog("Subdivided + smoothed: " + std::to_string(mr->mesh.TriangleCount()) + " triangles");
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Quadruple the triangles and relax them — turns a\nlow-poly blockout (e.g. Human) into smooth high-poly.");
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
            ImGui::SameLine(); ImGui::TextDisabled("(Shift = lower/invert)");
            const char* brushes[] = {"Raise / Lower", "Smooth", "Flatten", "Set Height"};
            ImGui::SetNextItemWidth(160);
            ImGui::Combo("Mode##terrbr", &g_terrainBrush, brushes, 4);
            ImGui::SliderFloat("Radius##terr", &g_terrainRadius, 0.5f, 30.0f);
            ImGui::SliderFloat("Strength##terr", &g_terrainStrength, 0.1f, 20.0f);
            if (g_terrainBrush == 3)
                ImGui::DragFloat("Target Height##terr", &g_terrainFlattenH, 0.1f, -100.0f, 100.0f);

            ImGui::SeparatorText("Generate (Perlin noise)");
            const char* gens[] = {"Mountains", "Hills", "Plains", "Plateau", "Islands"};
            ImGui::SetNextItemWidth(160);
            ImGui::Combo("Type##terrgen", &g_terrainGenType, gens, 5);
            ImGui::SliderFloat("Amplitude##terrgen", &g_terrainGenAmp, 1.0f, 60.0f);
            ImGui::SliderFloat("Frequency##terrgen", &g_terrainGenFreq, 0.5f, 12.0f);
            ImGui::SliderInt("Detail##terrgen", &g_terrainGenOct, 1, 8);
            if (ImGui::Button("Generate##terrgen")) {
                tr->Generate(g_terrainGenType, g_terrainGenAmp, g_terrainGenFreq,
                             g_terrainGenOct, (unsigned)(ImGui::GetTime() * 1000.0));
                tr->Apply(); ed.dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Flatten##terr")) { tr->Flatten(0.0f); tr->Apply(); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::Button("Smooth All##terr")) { tr->Smooth(); tr->Apply(); ed.dirty = true; }

            ImGui::SeparatorText("Layers (auto-color by height & slope)");
            if (ImGui::Checkbox("Auto Color##terr", &tr->autoColor)) { tr->Apply(); ed.dirty = true; }
            if (tr->autoColor) {
                auto layerCol = [&](const char* lbl, Color& col) {
                    float v[3] = {col.r, col.g, col.b};
                    if (ImGui::ColorEdit3(lbl, v, ImGuiColorEditFlags_NoInputs)) {
                        col = {v[0], v[1], v[2], 1.0f}; tr->Apply(); ed.dirty = true;
                    }
                };
                layerCol("Water##terr", tr->waterColor); ImGui::SameLine();
                layerCol("Sand##terr",  tr->sandColor);  ImGui::SameLine();
                layerCol("Grass##terr", tr->grassColor);
                layerCol("Rock##terr",  tr->rockColor);  ImGui::SameLine();
                layerCol("Snow##terr",  tr->snowColor);
                if (ImGui::SliderFloat("Water Level##terr", &tr->waterLevel, -20.0f, 40.0f)) { tr->Apply(); ed.dirty = true; }
                if (ImGui::SliderFloat("Snow Level##terr",  &tr->snowLevel,  0.0f, 80.0f))   { tr->Apply(); ed.dirty = true; }
                if (ImGui::SliderFloat("Rock Slope##terr",  &tr->rockSlope,  0.1f, 1.0f))    { tr->Apply(); ed.dirty = true; }
            }

            ImGui::SeparatorText("Scatter props (trees, rocks, bushes)");
            const char* props[] = {"Tree", "Pine", "Rock", "Bush"};
            ImGui::SetNextItemWidth(160);
            ImGui::Combo("Prop##scat", &g_scatterProp, props, 4);
            ImGui::SliderInt("Count##scat", &g_scatterCount, 1, 500);
            ImGui::DragFloatRange2("Scale##scat", &g_scatterMinScale, &g_scatterMaxScale, 0.02f, 0.1f, 6.0f, "%.2f");
            ImGui::SliderFloat("Max Slope##scat", &g_scatterMaxSlope, 0.0f, 1.0f);
            ImGui::DragFloatRange2("Height Band##scat", &g_scatterMinH, &g_scatterMaxH, 0.2f, -100.0f, 100.0f, "%.1f");
            if (ImGui::Button("Scatter##scat")) {
                Random rng((unsigned)(ImGui::GetTime() * 1000.0) + 7u);
                float half = tr->size * 0.5f, e = tr->CellSize();
                int placed = 0;
                for (int i = 0; i < g_scatterCount; ++i) {
                    float lx = rng.Range(-half, half), lz = rng.Range(-half, half);
                    float h = tr->SampleHeight(lx, lz);
                    if (h < g_scatterMinH || h > g_scatterMaxH) continue;
                    float hx = tr->SampleHeight(lx + e, lz) - tr->SampleHeight(lx - e, lz);
                    float hz = tr->SampleHeight(lx, lz + e) - tr->SampleHeight(lx, lz - e);
                    Vec3 n = Vec3{-hx, 2.0f * e, -hz}.Normalized();
                    if (1.0f - n.y > g_scatterMaxSlope) continue;       // too steep
                    GameObject* o = ed.scene().CreateGameObject(std::string("Scatter_") + props[g_scatterProp]);
                    o->AddComponent<MeshRenderer>()->mesh = Mesh::FromName(props[g_scatterProp]);
                    o->transform->SetParent(tr->gameObject->transform, false);
                    o->transform->localPosition = {lx, h, lz};
                    float sc = rng.Range(g_scatterMinScale, g_scatterMaxScale);
                    o->transform->localScale = {sc, sc, sc};
                    o->transform->localRotation = Quat::Euler({0.0f, rng.Range(0.0f, 360.0f), 0.0f});
                    ++placed;
                }
                ConsoleLog("Scattered " + std::to_string(placed) + " " + props[g_scatterProp] + "(s)");
                ed.dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Scattered##scat")) {
                std::vector<GameObject*> kill;
                for (const auto& o : ed.scene().Objects())
                    if (o->name.rfind("Scatter_", 0) == 0) kill.push_back(o.get());
                for (GameObject* k : kill) ed.scene().Destroy(k);
                ConsoleLog("Cleared " + std::to_string(kill.size()) + " scattered prop(s)");
                ed.dirty = true;
            }

            if (ImGui::SmallButton("Remove##terr")) toRemove = tr;
        }
    }
    if (auto* ch = go->GetComponent<Character>()) {
        if (CompHeader("Character", ch, &toRemove)) {
            bool c = false;
            auto coledit = [&](const char* lbl, Color& col){ float v[3]={col.r,col.g,col.b}; if (ImGui::ColorEdit3(lbl, v)) { col={v[0],v[1],v[2],1.0f}; c=true; } };

            c |= ImGui::SliderFloat("Height##char", &ch->height, 0.6f, 1.8f);

            ImGui::SeparatorText("Clothing");
            const char* shirts[] = {"Tank (bare arms)","Short sleeve","Long sleeve"};
            ImGui::SetNextItemWidth(150); c |= ImGui::Combo("Shirt##char", &ch->shirtStyle, shirts, 3);
            const char* legs[] = {"Trousers","Shorts"};
            ImGui::SetNextItemWidth(150); c |= ImGui::Combo("Legs##char", &ch->legStyle, legs, 2);
            c |= ImGui::Checkbox("Belt##char", &ch->hasBelt);
            ImGui::SameLine(); c |= ImGui::Checkbox("Jacket##char", &ch->hasJacket);
            ImGui::SameLine(); c |= ImGui::Checkbox("Gloves##char", &ch->hasGloves);

            ImGui::SeparatorText("Hair");
            c |= ImGui::Checkbox("Hair##char", &ch->hasHair);
            if (ch->hasHair) {
                const char* hs[]={"Short","Long","Mohawk","Bun","Spiky","Afro","Ponytail","Buzz"};
                ImGui::SameLine(); ImGui::SetNextItemWidth(120); c |= ImGui::Combo("##hairstyle", &ch->hairStyle, hs, 8);
            }
            const char* beards[] = {"None","Full","Goatee","Mustache"};
            ImGui::SetNextItemWidth(150); c |= ImGui::Combo("Beard##char", &ch->beardStyle, beards, 4);

            ImGui::SeparatorText("Accessories");
            c |= ImGui::Checkbox("Hat##char", &ch->hasHat);
            if (ch->hasHat) {
                const char* ht[]={"Cap","Helmet","Top Hat","Wizard","Beanie","Cowboy","Crown","Bandana"};
                ImGui::SameLine(); ImGui::SetNextItemWidth(120); c |= ImGui::Combo("##hatstyle", &ch->hatStyle, ht, 8);
            }
            const char* gl[] = {"None","Glasses","Sunglasses"};
            ImGui::SetNextItemWidth(150); c |= ImGui::Combo("Eyewear##char", &ch->glassesStyle, gl, 3);
            c |= ImGui::Checkbox("Mask##char", &ch->hasMask);
            ImGui::SameLine(); c |= ImGui::Checkbox("Scarf##char", &ch->hasScarf);
            c |= ImGui::Checkbox("Shoulder Pads##char", &ch->hasShoulderPads);
            c |= ImGui::Checkbox("Backpack##char", &ch->hasBackpack);
            ImGui::SameLine(); c |= ImGui::Checkbox("Cape##char", &ch->hasCape);

            ImGui::SeparatorText("Colors");
            coledit("Skin##char", ch->skin);     coledit("Shirt##char", ch->shirt);
            coledit("Pants##char", ch->pants);   coledit("Shoes##char", ch->shoes);
            coledit("Hair##char", ch->hair);     coledit("Eyes##char", ch->eyes);
            coledit("Hat##char", ch->hat);       coledit("Jacket##char", ch->jacket);
            coledit("Gloves##char", ch->gloves); coledit("Belt##char", ch->belt);
            coledit("Backpack##char", ch->pack);

            ImGui::SeparatorText("Animation (plays in Play mode)");
            const char* anims[] = {"None","Idle","Walk","Run","Wave","Jump"};
            ImGui::SetNextItemWidth(150); ImGui::Combo("Animation##char", &ch->anim, anims, 6);
            if (ch->anim != 0) ImGui::SliderFloat("Anim Speed##char", &ch->animSpeed, 0.1f, 3.0f);

            if (c) { ch->Apply(); ed.dirty = true; }
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
                if (ImGui::Checkbox("Physical Camera", &cam->physicalCamera)) ed.dirty = true;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Drive FOV from real lens optics (focal length + sensor size),\nlike Unity's physical camera.");
                if (cam->physicalCamera) {
                    if (ImGui::DragFloat("Focal Length", &cam->focalLength, 0.5f, 1.0f, 300.0f, "%.1f mm")) ed.dirty = true;
                    if (ImGui::DragFloat("Sensor Height", &cam->sensorHeight, 0.1f, 1.0f, 100.0f, "%.1f mm")) ed.dirty = true;
                    ImGui::TextDisabled("Effective FOV: %.1f deg", cam->VerticalFovDegrees(16.0f / 9.0f));
                } else {
                    if (ImGui::SliderFloat("Field of View", &cam->fieldOfView, 10.0f, 170.0f, "%.0f deg")) ed.dirty = true;
                    int ax = cam->fovAxisHorizontal ? 1 : 0;
                    const char* axes[] = {"Vertical", "Horizontal"};
                    if (ImGui::Combo("FOV Axis", &ax, axes, 2)) { cam->fovAxisHorizontal = (ax == 1); ed.dirty = true; }
                }
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

            ImGui::SeparatorText("Viewport Rect");
            float r[4] = {cam->rectX, cam->rectY, cam->rectW, cam->rectH};
            if (ImGui::DragFloat4("X / Y / W / H", r, 0.01f, 0.0f, 1.0f)) {
                cam->rectX = r[0]; cam->rectY = r[1]; cam->rectW = r[2]; cam->rectH = r[3]; ed.dirty = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Where on screen this camera draws (0..1, origin bottom-left)\n— like Unity's Camera.rect. Use for split-screen / mini-maps.");

            ImGui::SeparatorText("Culling Mask");
            // Layer set this camera renders (Unity's Camera.cullingMask). Summarize the
            // selection on the combo preview, with per-layer toggles + Everything/Nothing.
            std::string maskPreview;
            if (cam->cullingMask == ~0) maskPreview = "Everything";
            else if (cam->cullingMask == 0) maskPreview = "Nothing";
            else {
                int cnt = 0;
                for (int i = 0; i < 32; ++i) if (cam->cullingMask & (1 << i)) {
                    if (cnt < 2) { if (cnt) maskPreview += ", "; maskPreview += okay::Layers::Name(i); }
                    ++cnt;
                }
                if (cnt > 2) maskPreview += " (+" + std::to_string(cnt - 2) + ")";
            }
            if (ImGui::BeginCombo("Layers", maskPreview.c_str())) {
                if (ImGui::Selectable("Everything", cam->cullingMask == ~0)) { cam->cullingMask = ~0; ed.dirty = true; }
                if (ImGui::Selectable("Nothing", cam->cullingMask == 0)) { cam->cullingMask = 0; ed.dirty = true; }
                ImGui::Separator();
                for (int i = 0; i < 32; ++i) {
                    std::string nm = okay::Layers::Name(i);
                    bool on = (cam->cullingMask & (1 << i)) != 0;
                    if (ImGui::Checkbox((nm + "##cull" + std::to_string(i)).c_str(), &on)) {
                        if (on) cam->cullingMask |= (1 << i);
                        else    cam->cullingMask &= ~(1 << i);
                        ed.dirty = true;
                    }
                }
                ImGui::EndCombo();
            }

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
            if (go->GetComponent<SpriteRenderer>() && ImGui::SmallButton("Fit to Object##bc")) { FitColliders(go); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::Checkbox("Auto Fit##bc", &bc->autoFit)) { if (bc->autoFit) FitColliders(go); ed.dirty = true; }
            if (bc->autoFit) FitColliders(go, true);   // keep matched while editing
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
            if (go->GetComponent<SpriteRenderer>() && ImGui::SmallButton("Fit to Object##cc")) { FitColliders(go); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::Checkbox("Auto Fit##cc", &cc->autoFit)) { if (cc->autoFit) FitColliders(go); ed.dirty = true; }
            if (cc->autoFit) FitColliders(go, true);
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
            if (go->GetComponent<SpriteRenderer>() && ImGui::SmallButton("Fit to Object##cap2")) { FitColliders(go); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::Checkbox("Auto Fit##cap2", &cap->autoFit)) { if (cap->autoFit) FitColliders(go); ed.dirty = true; }
            if (cap->autoFit) FitColliders(go, true);
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
            if (ImGui::Checkbox("Auto Fit##bc3", &bc->autoFit)) { if (bc->autoFit) FitColliders(go); ed.dirty = true; }
            if (bc->autoFit) FitColliders(go, true);
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
            if (ImGui::Checkbox("Auto Fit##sc3", &sc->autoFit)) { if (sc->autoFit) FitColliders(go); ed.dirty = true; }
            if (sc->autoFit) FitColliders(go, true);
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
            if (ImGui::Checkbox("Auto Fit##cap3", &cap->autoFit)) { if (cap->autoFit) FitColliders(go); ed.dirty = true; }
            if (cap->autoFit) FitColliders(go, true);
            if (ImGui::SmallButton("Remove##cap3")) toRemove = cap;
        }
    }
    // A GameObject may carry several scripts (like Unity's multiple MonoBehaviours).
    for (auto* sc : go->GetComponents<ScriptComponent>()) {
        // Scope every widget by the component pointer so multiple scripts on one
        // object don't collide IDs (which makes ImGui flag a conflict on hover).
        ImGui::PushID(sc);
        std::string sname = sc->Path().empty() ? go->name
                          : std::filesystem::path(sc->Path()).stem().string();
        std::string slabel = "Script (" + sname + ")";
        if (CompHeader(slabel.c_str(), sc, &toRemove)) {
            if (ImGui::SmallButton("Open in Script Editor")) {
                g_showScriptEditor = true;
                if (std::find(g_scriptTabs.begin(), g_scriptTabs.end(), sc) == g_scriptTabs.end())
                    g_scriptTabs.push_back(sc);
                g_activeScriptTab = sc; g_focusScriptTab = sc;
            }

            // Public variables (Unity's serialized fields): editable values that
            // override the script's defaults per object.
            // Parse the live editor buffer when this script is open (so new public
            // vars appear as you type), else the last-compiled source.
            const char* liveBuf = PeekCodeBuffer(sc);
            std::vector<ScriptField> fields =
                ParseScriptFields(liveBuf ? std::string(liveBuf) : sc->Source());
            if (fields.empty()) {
                ImGui::TextDisabled("No public variables. Declare top-level vars,");
                ImGui::TextDisabled("e.g.  public float speed = 5;");
            } else {
                ImGui::SeparatorText("Variables");
                float nameCol = ImGui::GetContentRegionAvail().x * 0.42f;   // label column width
                if (nameCol < 80) nameCol = 80;
                for (const auto& f : fields) {
                    bool overridden = sc->fields.count(f.name) != 0;
                    std::string cur = overridden ? sc->fields[f.name] : f.def;
                    ImGui::PushID(f.name.c_str());
                    bool changed = false;
                    if (!f.header.empty()) { ImGui::Spacing(); ImGui::SeparatorText(f.header.c_str()); }
                    // Variable NAME on the left (Unity-style), value widget on the right.
                    ImGui::AlignTextToFramePadding();
                    if (overridden) ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.55f, 1.0f), "%s", f.name.c_str());
                    else            ImGui::TextUnformatted(f.name.c_str());
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", f.tooltip.empty() ? (f.type.empty() ? f.name.c_str() : f.type.c_str())
                                                                   : f.tooltip.c_str());
                    ImGui::SameLine(nameCol);
                    // Is this a Vector3/Vector2 field?
                    bool isVec = f.type.find("Vector3") != std::string::npos ||
                                 f.type.find("Vector2") != std::string::npos ||
                                 f.type.find("Vec3") != std::string::npos ||
                                 cur.find("Vector3") != std::string::npos ||
                                 cur.find("Vector2") != std::string::npos;
                    if (cur == "true" || cur == "false") {
                        bool b = (cur == "true");
                        if (ImGui::Checkbox("##v", &b)) { cur = b ? "true" : "false"; changed = true; }
                    } else if (isVec) {
                        // Pull 3 numbers from "...(x,y,z)" and edit them.
                        float v[3] = {0, 0, 0};
                        if (auto lp = cur.find('('); lp != std::string::npos) {
                            std::string inside = cur.substr(lp + 1);
                            if (auto rp = inside.find(')'); rp != std::string::npos) inside = inside.substr(0, rp);
                            for (char& c : inside) if (c == ',') c = ' ';
                            std::istringstream is(inside); is >> v[0] >> v[1] >> v[2];
                        }
                        ImGui::SetNextItemWidth(-28);
                        if (ImGui::DragFloat3("##v", v, 0.05f)) {
                            char nb[96]; std::snprintf(nb, sizeof(nb), "new Vector3(%g, %g, %g)", v[0], v[1], v[2]);
                            cur = nb; changed = true;
                        }
                    } else {
                        // Numeric? (allow an f/d suffix). Else edit as text.
                        char* endp = nullptr; std::string num = cur;
                        if (!num.empty() && (num.back()=='f'||num.back()=='F'||num.back()=='d'||num.back()=='D')) num.pop_back();
                        double dv = std::strtod(num.c_str(), &endp);
                        bool isNum = endp && *endp == '\0' && !num.empty();
                        ImGui::SetNextItemWidth(-28);
                        if (isNum) {
                            float v = (float)dv;
                            bool e = f.hasRange ? ImGui::SliderFloat("##v", &v, f.rmin, f.rmax)
                                                : ImGui::DragFloat("##v", &v, 0.05f);
                            if (e) { char nb[32]; std::snprintf(nb, sizeof(nb), "%g", v); cur = nb; changed = true; }
                        } else {
                            std::string sv = cur;
                            if (sv.size() >= 2 && (sv.front()=='"'||sv.front()=='\'') && sv.back()==sv.front())
                                sv = sv.substr(1, sv.size()-2);
                            char tb[256]; std::strncpy(tb, sv.c_str(), sizeof(tb)-1); tb[sizeof(tb)-1]='\0';
                            if (ImGui::InputText("##v", tb, sizeof(tb))) {
                                cur = std::string("\"") + tb + "\""; changed = true;
                            }
                        }
                    }
                    // Revert-to-default button when overridden.
                    if (overridden) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("o")) { sc->fields.erase(f.name); sc->ApplyFieldOverrides(); ed.dirty = true; }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Revert to the script default (%s)", f.def.c_str());
                    }
                    if (changed) { sc->fields[f.name] = cur; sc->ApplyFieldOverrides(); ed.dirty = true; }
                    ImGui::PopID();
                }
            }
        }
        ImGui::PopID();
    }
    if (auto* cc = go->GetComponent<CharacterController2D>()) {
        if (CompHeader("Character Controller 2D", cc, &toRemove)) {
            int m = (int)cc->mode;
            const char* modes[] = {"Top-Down", "Platformer"};
            if (ImGui::Combo("Mode##cc2", &m, modes, 2)) { cc->mode = (CharacterController2D::Mode)m; ed.dirty = true; }

            ImGui::SeparatorText("Movement");
            if (ImGui::DragFloat("Speed##cc2", &cc->speed, 0.1f, 0.0f, 200.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Run Speed##cc2", &cc->runSpeed, 0.1f, 0.0f, 200.0f)) ed.dirty = true;
            int sk = cc->sprintKey ? cc->sprintKey : 0; char skb[2] = { (char)(sk ? sk : ' '), 0 };
            if (ImGui::InputText("Sprint Key##cc2", skb, sizeof(skb))) { cc->sprintKey = skb[0] == ' ' ? 0 : skb[0]; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold to run (blank = disabled)");
            if (ImGui::DragFloat("Acceleration##cc2", &cc->acceleration, 0.5f, 0.0f, 500.0f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = instant; higher = snappier ramp-up");
            if (ImGui::DragFloat("Deceleration##cc2", &cc->deceleration, 0.5f, 0.0f, 500.0f)) ed.dirty = true;
            if (ImGui::Checkbox("Use Gamepad##cc2", &cc->useGamepad)) ed.dirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Flip Sprite##cc2", &cc->flipSprite)) ed.dirty = true;
            if (cc->mode == CharacterController2D::Mode::TopDown) {
                if (ImGui::Checkbox("Normalize Diagonal##cc2", &cc->normalizeDiagonal)) ed.dirty = true;
            }

            if (cc->mode == CharacterController2D::Mode::Platformer) {
                ImGui::SeparatorText("Jump");
                if (ImGui::DragFloat("Jump Force##cc2", &cc->jumpForce, 0.1f, 0.0f, 200.0f)) ed.dirty = true;
                if (ImGui::DragInt("Max Jumps##cc2", &cc->maxJumps, 0.05f, 1, 5)) ed.dirty = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("2 = double jump (ground counts as one)");
                if (ImGui::Checkbox("Variable Jump##cc2", &cc->variableJump)) ed.dirty = true;
                if (cc->variableJump)
                    if (ImGui::DragFloat("Jump Cut##cc2", &cc->jumpCutMultiplier, 0.01f, 0.0f, 1.0f)) ed.dirty = true;
                if (ImGui::DragFloat("Coyote Time##cc2", &cc->coyoteTime, 0.005f, 0.0f, 0.5f, "%.3f s")) ed.dirty = true;
                if (ImGui::DragFloat("Jump Buffer##cc2", &cc->jumpBuffer, 0.005f, 0.0f, 0.5f, "%.3f s")) ed.dirty = true;
                if (ImGui::DragFloat("Air Control##cc2", &cc->airControl, 0.02f, 0.0f, 1.0f)) ed.dirty = true;
                if (ImGui::DragFloat("Max Fall Speed##cc2", &cc->maxFallSpeed, 0.5f, 0.0f, 200.0f)) ed.dirty = true;
                if (ImGui::DragFloat("Extra Fall Gravity##cc2", &cc->extraFallGravity, 0.5f, 0.0f, 200.0f)) ed.dirty = true;
            }
            ImGui::TextDisabled("WASD / arrows / stick. Uses a Rigidbody2D if present.");
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
    if (auto* fp = go->GetComponent<FirstPersonController>()) {
        if (CompHeader("First Person Controller", fp, &toRemove)) {
            if (ImGui::DragFloat("Walk Speed##fp", &fp->walkSpeed, 0.1f, 0.0f, 50.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Run Speed##fp", &fp->runSpeed, 0.1f, 0.0f, 50.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Jump Force##fp", &fp->jumpForce, 0.1f, 0.0f, 50.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Acceleration##fp", &fp->acceleration, 1.0f, 1.0f, 300.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Deceleration##fp", &fp->deceleration, 1.0f, 1.0f, 300.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Air Control##fp", &fp->airControl, 0.01f, 0.0f, 1.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Coyote Time##fp", &fp->coyoteTime, 0.005f, 0.0f, 0.5f, "%.3f s")) ed.dirty = true;
            if (ImGui::DragFloat("Jump Buffer##fp", &fp->jumpBufferTime, 0.005f, 0.0f, 0.5f, "%.3f s")) ed.dirty = true;
            if (ImGui::DragFloat("Mouse Sensitivity##fp", &fp->mouseSensitivity, 0.01f, 0.0f, 2.0f)) ed.dirty = true;
            if (ImGui::Checkbox("Invert Y##fp", &fp->invertY)) ed.dirty = true;
            if (ImGui::Checkbox("Can Jump##fp", &fp->canJump)) ed.dirty = true;
            if (ImGui::Checkbox("Drive Animation##fp", &fp->driveAnimation)) ed.dirty = true;
            ImGui::TextDisabled("Mouse look + WASD. Put a Camera as a child (eye height).");
            if (ImGui::SmallButton("Remove##fp")) toRemove = fp;
        }
    }
    if (auto* tp = go->GetComponent<ThirdPersonController>()) {
        if (CompHeader("Third Person Controller", tp, &toRemove)) {
            ImGui::SeparatorText("Movement");
            if (ImGui::DragFloat("Walk Speed##tp", &tp->walkSpeed, 0.1f, 0.0f, 50.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Run Speed##tp", &tp->runSpeed, 0.1f, 0.0f, 50.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Jump Force##tp", &tp->jumpForce, 0.1f, 0.0f, 50.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Turn Speed##tp", &tp->turnSpeed, 0.1f, 0.0f, 40.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Acceleration##tp", &tp->acceleration, 1.0f, 1.0f, 300.0f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Higher = snappier starts; very high ~= instant (no momentum)");
            if (ImGui::DragFloat("Deceleration##tp", &tp->deceleration, 1.0f, 1.0f, 300.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Air Control##tp", &tp->airControl, 0.01f, 0.0f, 1.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Coyote Time##tp", &tp->coyoteTime, 0.005f, 0.0f, 0.5f, "%.3f s")) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Grace window to still jump just after leaving a ledge");
            if (ImGui::DragFloat("Jump Buffer##tp", &tp->jumpBufferTime, 0.005f, 0.0f, 0.5f, "%.3f s")) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remembers a jump pressed just before landing");
            if (ImGui::Checkbox("Can Jump##tp", &tp->canJump)) ed.dirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Drive Animation##tp", &tp->driveAnimation)) ed.dirty = true;
            const char* faceModes[] = {"Face Movement", "Face Camera"};
            int fm = (int)tp->faceMode;
            ImGui::SetNextItemWidth(160);
            if (ImGui::Combo("Body Facing##tp", &fm, faceModes, 2)) { tp->faceMode = (ThirdPersonController::FaceMode)fm; ed.dirty = true; }

            ImGui::SeparatorText("Camera");
            if (ImGui::DragFloat("Mouse Sensitivity##tp", &tp->mouseSensitivity, 0.01f, 0.0f, 2.0f)) ed.dirty = true;
            if (ImGui::Checkbox("Invert X##tp", &tp->invertX)) ed.dirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Invert Y##tp", &tp->invertY)) ed.dirty = true;
            if (ImGui::DragFloat("Distance##tp", &tp->distance, 0.1f, 1.0f, 20.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Min Distance##tp", &tp->minDistance, 0.1f, 0.5f, 20.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Max Distance##tp", &tp->maxDistance, 0.1f, 1.0f, 40.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Zoom Speed##tp", &tp->zoomSpeed, 0.05f, 0.0f, 10.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Camera Height##tp", &tp->cameraHeight, 0.05f, 0.0f, 5.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Shoulder Offset##tp", &tp->shoulderOffset, 0.02f, -3.0f, 3.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Damping##tp", &tp->cameraDamping, 0.1f, 0.0f, 30.0f, "%.1f")) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = instant follow; higher = smoother lag");
            if (ImGui::Checkbox("Camera Collision##tp", &tp->cameraCollision)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pull the camera in so it never clips through walls/floors");
            if (tp->cameraCollision)
                if (ImGui::DragFloat("Collision Skin##tp", &tp->cameraCollisionSkin, 0.01f, 0.0f, 2.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Min Pitch##tp", &tp->minPitch, 0.5f, -89.0f, 0.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Max Pitch##tp", &tp->maxPitch, 0.5f, 0.0f, 89.0f)) ed.dirty = true;
            ImGui::TextDisabled("Orbit camera (mouse/wheel) + WASD/stick. Uses the main Camera.");
            if (ImGui::SmallButton("Remove##tp")) toRemove = tp;
        }
    }
    if (auto* td = go->GetComponent<TopDownController>()) {
        if (CompHeader("Top Down Controller", td, &toRemove)) {
            ImGui::SeparatorText("Movement");
            if (ImGui::DragFloat("Walk Speed##td", &td->walkSpeed, 0.1f, 0.0f, 50.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Run Speed##td", &td->runSpeed, 0.1f, 0.0f, 50.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Acceleration##td", &td->acceleration, 1.0f, 1.0f, 300.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Deceleration##td", &td->deceleration, 1.0f, 1.0f, 300.0f)) ed.dirty = true;
            if (ImGui::Checkbox("Rotate To Face##td", &td->rotateToFace)) ed.dirty = true;
            if (td->rotateToFace)
                if (ImGui::DragFloat("Turn Speed##td", &td->turnSpeed, 0.1f, 0.0f, 40.0f)) ed.dirty = true;
            if (ImGui::Checkbox("Camera-Relative Move##td", &td->cameraRelative)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("WASD relative to the camera yaw instead of world axes");
            if (ImGui::Checkbox("Drive Animation##td", &td->driveAnimation)) ed.dirty = true;
            ImGui::SeparatorText("Camera");
            if (ImGui::DragFloat("Distance##td", &td->cameraDistance, 0.1f, 1.0f, 60.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Pitch##td", &td->cameraPitch, 0.5f, 10.0f, 89.0f, "%.0f deg")) ed.dirty = true;
            if (ImGui::DragFloat("Yaw##td", &td->cameraYaw, 0.5f, -180.0f, 180.0f, "%.0f deg")) ed.dirty = true;
            if (ImGui::DragFloat("Look Height##td", &td->lookHeight, 0.05f, 0.0f, 5.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Damping##td", &td->cameraDamping, 0.1f, 0.0f, 30.0f)) ed.dirty = true;
            ImGui::TextDisabled("Twin-stick / ARPG: WASD + a fixed high follow camera.");
            if (ImGui::SmallButton("Remove##td")) toRemove = td;
        }
    }
    if (auto* cm = go->GetComponent<ClickToMoveController>()) {
        if (CompHeader("Click To Move Controller", cm, &toRemove)) {
            if (ImGui::DragFloat("Walk Speed##ctm", &cm->walkSpeed, 0.1f, 0.0f, 50.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Run Speed##ctm", &cm->runSpeed, 0.1f, 0.0f, 50.0f)) ed.dirty = true;
            int rk = cm->runKey ? cm->runKey : 0; char rkb[2] = { (char)(rk ? rk : ' '), 0 };
            if (ImGui::InputText("Run Key##ctm", rkb, sizeof(rkb))) { cm->runKey = rkb[0] == ' ' ? 0 : rkb[0]; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold to run (blank = disabled)");
            if (ImGui::DragFloat("Stop Distance##ctm", &cm->stopDistance, 0.01f, 0.0f, 5.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Arrive Radius##ctm", &cm->arriveRadius, 0.05f, 0.0f, 10.0f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ease to a stop over the last this-many metres (0 = abrupt stop)");
            if (ImGui::DragFloat("Turn Speed##ctm", &cm->turnSpeed, 0.1f, 0.0f, 40.0f)) ed.dirty = true;
            const char* btns[] = {"Left", "Right", "Middle"};
            ImGui::SetNextItemWidth(110);
            if (ImGui::Combo("Click Button##ctm", &cm->mouseButton, btns, 3)) ed.dirty = true;
            if (ImGui::Checkbox("Hold To Move##ctm", &cm->holdToMove)) ed.dirty = true;
            ImGui::SameLine();
            if (ImGui::Checkbox("Drive Animation##ctm", &cm->driveAnimation)) ed.dirty = true;
            if (ImGui::Checkbox("Use Player Height##ctm", &cm->usePlayerHeight)) ed.dirty = true;
            if (!cm->usePlayerHeight)
                if (ImGui::DragFloat("Ground Y##ctm", &cm->groundY, 0.05f)) ed.dirty = true;

            ImGui::SeparatorText("Follow Camera");
            if (ImGui::Checkbox("Follow Camera##ctm", &cm->followCamera)) ed.dirty = true;
            if (cm->followCamera) {
                if (ImGui::DragFloat("Cam Height##ctm", &cm->cameraHeight, 0.05f, 0.0f, 10.0f)) ed.dirty = true;
                if (ImGui::DragFloat("Cam Distance##ctm", &cm->cameraDistance, 0.1f, 1.0f, 40.0f)) ed.dirty = true;
                if (ImGui::DragFloat("Min Distance##ctm", &cm->minDistance, 0.1f, 1.0f, 40.0f)) ed.dirty = true;
                if (ImGui::DragFloat("Max Distance##ctm", &cm->maxDistance, 0.1f, 1.0f, 60.0f)) ed.dirty = true;
                if (ImGui::DragFloat("Cam Yaw##ctm", &cm->cameraYaw, 0.5f)) ed.dirty = true;
                if (ImGui::DragFloat("Cam Pitch##ctm", &cm->cameraPitch, 0.5f, 5.0f, 89.0f)) ed.dirty = true;
                if (ImGui::DragFloat("Rotate Speed##ctm", &cm->rotateSpeed, 0.01f, 0.0f, 2.0f)) ed.dirty = true;
                if (ImGui::DragFloat("Damping##ctm", &cm->cameraDamping, 0.1f, 0.0f, 30.0f)) ed.dirty = true;
                ImGui::TextDisabled("Middle-drag rotates, wheel zooms.");
            }
            ImGui::TextDisabled("Click the ground to walk there (RuneScape-style). Needs the main Camera.");
            if (ImGui::SmallButton("Remove##ctm")) toRemove = cm;
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
                                   "On Click", "On Key Up", "On Message",
                                   "On Trigger Enter", "On Trigger Exit", "On Mouse Enter",
                                   "On Mouse Exit", "On Mouse Down", "On Mouse Up", "On Mouse Over"};
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
            static const char* instOps[] = {"move", "set_pos", "rotate", "set_rotation",
                "set_rotation3", "set_scale", "set_scale3", "move_toward", "look_at",
                "wait", "goto", "if_goto", "stop", "call", "send", "send_to",
                "set_var", "add_var", "mul_var", "div_var", "copy_var", "rand_var",
                "set_active", "set_visible", "set_text", "set_sprite", "set_color", "velocity",
                "impulse", "velocity3", "impulse3", "force3", "emit", "play_anim", "play_sound",
                "set_cam", "set_bg", "set_light", "set_ambient", "set_timescale", "spawn", "spawn3",
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
    if (auto* vc = go->GetComponent<VirtualCamera>()) {
        if (CompHeader("Virtual Camera", vc, &toRemove)) {
            if (ImGui::DragInt("Priority##vc", &vc->priority, 1.0f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Highest enabled vcam becomes live (the brain blends to it).");
            ImGui::SeparatorText("Body");
            char fb[128];
            std::strncpy(fb, vc->follow.c_str(), sizeof(fb) - 1); fb[sizeof(fb) - 1] = '\0';
            if (ImGui::InputText("Follow##vc", fb, sizeof(fb))) { vc->follow = fb; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("GameObject to position relative to (the body).");
            float off[3] = {vc->followOffset.x, vc->followOffset.y, vc->followOffset.z};
            if (ImGui::DragFloat3("Follow Offset##vc", off, 0.05f)) { vc->followOffset = {off[0], off[1], off[2]}; ed.dirty = true; }
            int bm = (int)vc->bindingMode;
            const char* bms[] = {"World Space", "Lock To Target (orbital)"};
            if (ImGui::Combo("Binding Mode##vc", &bm, bms, 2)) { vc->bindingMode = (VirtualCamera::BindingMode)bm; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Lock To Target orbits the offset with the target's facing\n(3rd-person chase cam).");
            ImGui::SeparatorText("FreeLook (orbit rig)");
            if (ImGui::Checkbox("FreeLook##vc", &vc->freeLook)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Orbit the Follow target on a sphere (overrides offset + aim).\nGreat for 3rd-person player cameras.");
            if (vc->freeLook) {
                if (ImGui::DragFloat("Radius##vcfl", &vc->orbitRadius, 0.1f, 0.1f, 200.0f)) ed.dirty = true;
                if (ImGui::DragFloat("Pivot Height##vcfl", &vc->orbitHeight, 0.05f)) ed.dirty = true;
                if (ImGui::DragFloat("Yaw##vcfl", &vc->orbitYaw, 0.5f)) ed.dirty = true;
                if (ImGui::DragFloat("Pitch##vcfl", &vc->orbitPitch, 0.5f)) ed.dirty = true;
                float pc[2] = {vc->orbitMinPitch, vc->orbitMaxPitch};
                if (ImGui::DragFloat2("Pitch Min/Max##vcfl", pc, 0.5f, -89.0f, 89.0f)) { vc->orbitMinPitch = pc[0]; vc->orbitMaxPitch = pc[1]; ed.dirty = true; }
                if (ImGui::Checkbox("Mouse Look##vcfl", &vc->orbitInput)) ed.dirty = true;
                if (vc->orbitInput) {
                    if (ImGui::DragInt("Mouse Button##vcfl", &vc->orbitButton, 0.1f, -1, 4)) ed.dirty = true;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hold this mouse button to orbit (-1 = always; 1 = right button).");
                    if (ImGui::DragFloat("Sensitivity##vcfl", &vc->mouseSensitivity, 0.01f, 0.0f, 5.0f)) ed.dirty = true;
                }
            }
            ImGui::SeparatorText("Tracked Dolly");
            if (ImGui::Checkbox("Dolly##vc", &vc->dolly)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Constrain the body to a Dolly Path rail (aim still tracks LookAt/Follow).");
            if (vc->dolly) {
                char db[128];
                std::strncpy(db, vc->dollyPath.c_str(), sizeof(db) - 1); db[sizeof(db) - 1] = '\0';
                if (ImGui::InputText("Path##vcd", db, sizeof(db))) { vc->dollyPath = db; ed.dirty = true; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("GameObject holding a Dolly Path component.");
                if (ImGui::Checkbox("Auto Dolly##vcd", &vc->autoDolly)) ed.dirty = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slide to the path point nearest the Follow target.");
                if (!vc->autoDolly)
                    if (ImGui::SliderFloat("Position##vcd", &vc->dollyPosition, 0.0f, 1.0f)) ed.dirty = true;
            }
            ImGui::SeparatorText("Aim");
            char lb[128];
            std::strncpy(lb, vc->lookAt.c_str(), sizeof(lb) - 1); lb[sizeof(lb) - 1] = '\0';
            if (ImGui::InputText("Look At##vc", lb, sizeof(lb))) { vc->lookAt = lb; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("GameObject to aim at (the aim).");
            float lo[3] = {vc->lookAtOffset.x, vc->lookAtOffset.y, vc->lookAtOffset.z};
            if (ImGui::DragFloat3("Look Offset##vc", lo, 0.05f)) { vc->lookAtOffset = {lo[0], lo[1], lo[2]}; ed.dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Added to the LookAt target before aiming (e.g. aim at the head).");
            if (ImGui::DragFloat("Dead Zone##vc", &vc->aimDeadZone, 0.1f, 0.0f, 45.0f, "%.1f deg")) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Don't re-aim until the target drifts beyond this angle (reduces jitter).");
            ImGui::SeparatorText("Damping");
            if (ImGui::DragFloat("Position##vcd", &vc->positionDamping, 0.1f, 0.0f, 50.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Rotation##vcd", &vc->rotationDamping, 0.1f, 0.0f, 50.0f)) ed.dirty = true;
            ImGui::SeparatorText("Lens");
            if (ImGui::SliderFloat("Field of View##vc", &vc->fieldOfView, 10.0f, 170.0f, "%.0f deg")) ed.dirty = true;
            ImGui::SeparatorText("Noise / Impulse");
            if (ImGui::DragFloat("Amplitude##vcn", &vc->shakeAmplitude, 0.01f, 0.0f, 10.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Frequency##vcn", &vc->shakeFrequency, 0.05f, 0.0f, 30.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Impulse Decay##vcn", &vc->impulseDecay, 0.05f, 0.1f, 20.0f)) ed.dirty = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("Test Impulse##vc")) vc->AddImpulse(1.0f);
            ImGui::TextDisabled("Needs a Cinemachine Brain on the main camera.");
            if (ImGui::SmallButton("Remove##vc")) toRemove = vc;
        }
    }
    if (auto* cb = go->GetComponent<CinemachineBrain>()) {
        if (CompHeader("Cinemachine Brain", cb, &toRemove)) {
            if (ImGui::DragFloat("Blend Time##cb", &cb->blendTime, 0.05f, 0.0f, 10.0f, "%.2f s")) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Seconds to ease between virtual cameras (0 = cut).");
            if (ImGui::Checkbox("Ease In/Out##cb", &cb->easeInOut)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Smooth the blend curve instead of a linear ramp.");
            if (VirtualCamera* live = cb->LiveCamera())
                ImGui::TextDisabled("Live: %s", live->gameObject ? live->gameObject->name.c_str() : "?");
            else
                ImGui::TextDisabled("Drives this Camera from virtual cameras.");
            if (ImGui::SmallButton("Remove##cb")) toRemove = cb;
        }
    }
    if (auto* dp = go->GetComponent<DollyPath>()) {
        if (CompHeader("Dolly Path", dp, &toRemove)) {
            if (ImGui::Checkbox("Looped##dp", &dp->looped)) ed.dirty = true;
            ImGui::Text("Waypoints: %d (local space)", (int)dp->waypoints.size());
            for (int i = 0; i < (int)dp->waypoints.size(); ++i) {
                float w[3] = {dp->waypoints[i].x, dp->waypoints[i].y, dp->waypoints[i].z};
                ImGui::PushID(i);
                if (ImGui::DragFloat3("##wp", w, 0.1f)) { dp->waypoints[i] = {w[0], w[1], w[2]}; ed.dirty = true; }
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) { dp->waypoints.erase(dp->waypoints.begin() + i); ed.dirty = true; ImGui::PopID(); break; }
                ImGui::PopID();
            }
            if (ImGui::SmallButton("Add Waypoint##dp")) {
                Vec3 last = dp->waypoints.empty() ? Vec3{0, 0, 0} : dp->waypoints.back();
                dp->waypoints.push_back(last + Vec3{0, 0, 5}); ed.dirty = true;
            }
            if (ImGui::SmallButton("Remove##dp")) toRemove = dp;
        }
    }
    if (auto* dc = go->GetComponent<DollyCart>()) {
        if (CompHeader("Dolly Cart", dc, &toRemove)) {
            char db[128];
            std::strncpy(db, dc->path.c_str(), sizeof(db) - 1); db[sizeof(db) - 1] = '\0';
            if (ImGui::InputText("Path##dc", db, sizeof(db))) { dc->path = db; ed.dirty = true; }
            if (ImGui::SliderFloat("Position##dc", &dc->position, 0.0f, 1.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Speed##dc", &dc->speed, 0.01f)) ed.dirty = true;
            if (ImGui::Checkbox("Auto Move##dc", &dc->autoMove)) ed.dirty = true;
            if (ImGui::SmallButton("Remove##dc")) toRemove = dc;
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
            if (ImGui::Checkbox("Visible##cv", &cv->visible)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide/show every widget under this canvas at once.");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150);
            if (ImGui::SliderFloat("Opacity##cv", &cv->opacity, 0.0f, 1.0f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Master fade for this canvas's UI (drive at runtime for fade in/out).");
            ImGui::Separator();
            const char* modes[] = {"Constant Pixel Size", "Scale With Screen Size"};
            int m = (int)cv->scaleMode;
            if (ImGui::Combo("UI Scale Mode##cv", &m, modes, 2)) { cv->scaleMode = (Canvas::ScaleMode)m; ed.dirty = true; }

            if (cv->scaleMode == Canvas::ScaleMode::ScaleWithScreenSize) {
                float ref[2] = {cv->referenceResolution.x, cv->referenceResolution.y};
                if (ImGui::DragFloat2("Reference Res##cv", ref, 1.0f, 1.0f, 8000.0f)) { cv->referenceResolution = {ref[0], ref[1]}; ed.dirty = true; }
                // Quick presets for the most common authoring resolutions.
                ImGui::TextDisabled("Presets:");
                auto preset = [&](const char* label, float w, float h) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(label)) { cv->referenceResolution = {w, h}; ed.dirty = true; }
                };
                preset("1080p", 1920, 1080); preset("720p", 1280, 720); preset("1440p", 2560, 1440);
                ImGui::TextDisabled("Portrait:");
                auto presetP = [&](const char* label, float w, float h) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(label)) { cv->referenceResolution = {w, h}; ed.dirty = true; }
                };
                presetP("9:16", 1080, 1920); presetP("phone", 720, 1280);

                if (ImGui::SliderFloat("Match W/H##cv", &cv->matchWidthOrHeight, 0.0f, 1.0f,
                                       cv->matchWidthOrHeight < 0.25f ? "Width" :
                                       cv->matchWidthOrHeight > 0.75f ? "Height" : "%.2f")) ed.dirty = true;
                if (ImGui::SmallButton("Width##cvm"))  { cv->matchWidthOrHeight = 0.0f; ed.dirty = true; }
                ImGui::SameLine(); if (ImGui::SmallButton("Match##cvm")) { cv->matchWidthOrHeight = 0.5f; ed.dirty = true; }
                ImGui::SameLine(); if (ImGui::SmallButton("Height##cvm")) { cv->matchWidthOrHeight = 1.0f; ed.dirty = true; }
            }

            // Universal extra UI zoom (now applies in both scale modes).
            if (ImGui::DragFloat("Scale Factor##cv", &cv->scaleFactor, 0.01f, 0.05f, 10.0f, "%.2fx")) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Extra UI zoom on top of the scale mode.");
            if (ImGui::DragInt("Sort Order##cv", &cv->sortOrder)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Higher canvases draw on top of lower ones.");

            // Live readout of the resulting scale for the current view.
            float liveScale = cv->ScaleFactor(UICanvas::Width(), UICanvas::Height());
            ImGui::TextDisabled("Current scale: %.2fx  (canvas %.0f x %.0f)",
                                liveScale, UICanvas::Width(), UICanvas::Height());
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
            int bshp = (int)btn->shape;
            const char* bshapes[] = {"Rectangle", "Rounded", "Circle", "Pill"};
            if (ImGui::Combo("Shape##uib", &bshp, bshapes, 4)) { btn->shape = (UIShape)bshp; ed.dirty = true; }
            if (btn->shape == UIShape::Rounded)
                if (ImGui::DragFloat("Corner Radius##uib", &btn->cornerRadius, 0.2f, 0.0f, 64.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Font Scale##uib", &btn->fontScale, 0.05f, 0.5f, 16.0f)) ed.dirty = true;
            if (ImGui::Checkbox("Drop Shadow##uib", &btn->shadow)) ed.dirty = true;
            if (btn->shadow) {
                float sc[4] = {btn->shadowColor.r, btn->shadowColor.g, btn->shadowColor.b, btn->shadowColor.a};
                if (ImGui::ColorEdit4("Shadow Color##uib", sc)) { btn->shadowColor = {sc[0], sc[1], sc[2], sc[3]}; ed.dirty = true; }
                float so[2] = {btn->shadowOffset.x, btn->shadowOffset.y};
                if (ImGui::DragFloat2("Shadow Offset##uib", so, 0.5f)) { btn->shadowOffset = {so[0], so[1]}; ed.dirty = true; }
            }
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
            int shp = (int)pn->shape;
            const char* shapes[] = {"Rectangle", "Rounded", "Circle", "Pill"};
            if (ImGui::Combo("Shape##uip", &shp, shapes, 4)) { pn->shape = (UIShape)shp; ed.dirty = true; }
            if (pn->shape == UIShape::Rounded)
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
                if (ImGui::Checkbox("Horizontal Gradient##uip", &pn->gradientHorizontal)) ed.dirty = true;
                ImGui::TextDisabled("Color is the start; %s fade.", pn->gradientHorizontal ? "left->right" : "top->bottom");
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
            int ishp = (int)in->shape;
            const char* fshapes[] = {"Rectangle", "Rounded", "Circle", "Pill"};
            if (ImGui::Combo("Shape##uif", &ishp, fshapes, 4)) { in->shape = (UIShape)ishp; ed.dirty = true; }
            if (in->shape == UIShape::Rounded)
                if (ImGui::DragFloat("Corner Radius##uif", &in->cornerRadius, 0.2f, 0.0f, 64.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Focus Ring##uif", &in->borderWidth, 0.1f, 0.0f, 12.0f)) ed.dirty = true;
            if (in->borderWidth > 0.0f) {
                float bc[4] = {in->borderColor.r, in->borderColor.g, in->borderColor.b, in->borderColor.a};
                if (ImGui::ColorEdit4("Ring Color##uif", bc)) { in->borderColor = {bc[0], bc[1], bc[2], bc[3]}; ed.dirty = true; }
            }
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
            int dshp = (int)dd->shape;
            const char* dshapes[] = {"Rectangle", "Rounded", "Circle", "Pill"};
            if (ImGui::Combo("Shape##udd", &dshp, dshapes, 4)) { dd->shape = (UIShape)dshp; ed.dirty = true; }
            if (dd->shape == UIShape::Rounded)
                if (ImGui::DragFloat("Corner Radius##udd", &dd->cornerRadius, 0.2f, 0.0f, 64.0f)) ed.dirty = true;
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

            ImGui::SeparatorText("Slot background");
            if (ImGui::Checkbox("Draw background##udt", &dt->drawBackground)) ed.dirty = true;
            if (dt->drawBackground) {
                float bgc[4] = {dt->background.r, dt->background.g, dt->background.b, dt->background.a};
                if (ImGui::ColorEdit4("Background##udt", bgc)) { dt->background = {bgc[0],bgc[1],bgc[2],bgc[3]}; ed.dirty = true; }
                if (ImGui::DragFloat("Corner Radius##udt", &dt->cornerRadius, 0.2f, 0.0f, 32.0f)) ed.dirty = true;
                if (ImGui::DragFloat("Border Width##udt", &dt->borderWidth, 0.1f, 0.0f, 12.0f)) ed.dirty = true;
                if (dt->borderWidth > 0.0f) {
                    float bc[4] = {dt->borderColor.r, dt->borderColor.g, dt->borderColor.b, dt->borderColor.a};
                    if (ImGui::ColorEdit4("Border Color##udt", bc)) { dt->borderColor = {bc[0],bc[1],bc[2],bc[3]}; ed.dirty = true; }
                }
            }

            ImGui::SeparatorText("Hover feedback");
            if (ImGui::Checkbox("Highlight on hover##udt", &dt->showHighlight)) ed.dirty = true;
            if (dt->showHighlight) {
                float hl[4] = {dt->highlight.r, dt->highlight.g, dt->highlight.b, dt->highlight.a};
                if (ImGui::ColorEdit4("Accept tint##udt", hl)) { dt->highlight = {hl[0],hl[1],hl[2],hl[3]}; ed.dirty = true; }
                float rj[4] = {dt->rejectHighlight.r, dt->rejectHighlight.g, dt->rejectHighlight.b, dt->rejectHighlight.a};
                if (ImGui::ColorEdit4("Reject tint##udt", rj)) { dt->rejectHighlight = {rj[0],rj[1],rj[2],rj[3]}; ed.dirty = true; }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shown when a wrong-tag item hovers over the slot.");
            }

            ImGui::SeparatorText("Drop");
            if (ImGui::Checkbox("Snap dropped item to center##udt", &dt->snapToCenter)) ed.dirty = true;
            ImGui::TextDisabled("Script events (on a ScriptComponent here):");
            ImGui::BulletText("on_receive  — an item is dropped on this slot");
            ImGui::BulletText("on_remove   — an item is dragged out of this slot");
            ImGui::BulletText("on_drag_enter / on_drag_exit — item hovers/leaves");
            ImGui::TextDisabled("Items expose prefs: ui_drop_source, ui_drag_item, ui_remove_item.");
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
            int pshp = (int)pb->shape;
            const char* pshapes[] = {"Rectangle", "Rounded", "Circle", "Pill"};
            if (ImGui::Combo("Shape##upb", &pshp, pshapes, 4)) { pb->shape = (UIShape)pshp; ed.dirty = true; }
            if (pb->shape == UIShape::Rounded)
                if (ImGui::DragFloat("Corner Radius##upb", &pb->cornerRadius, 0.2f, 0.0f, 64.0f)) ed.dirty = true;
            if (ImGui::Checkbox("Gradient Fill##upb", &pb->gradientFill)) ed.dirty = true;
            if (pb->gradientFill) {
                float fe[4] = {pb->fillEnd.r, pb->fillEnd.g, pb->fillEnd.b, pb->fillEnd.a};
                if (ImGui::ColorEdit4("Fill End##upb", fe)) { pb->fillEnd = {fe[0], fe[1], fe[2], fe[3]}; ed.dirty = true; }
            }
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
    if (auto* rp = go->GetComponent<UIRadialProgress>()) {
        if (CompHeader("UI Radial Progress", rp, &toRemove)) {
            float pos[2] = {rp->position.x, rp->position.y};
            if (ImGui::DragFloat2("Pos (px)##urp", pos, 1.0f)) { rp->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {rp->size.x, rp->size.y};
            if (ImGui::DragFloat2("Size (px)##urp", sz, 1.0f, 8.0f, 4000.0f)) { rp->size = {sz[0], sz[1]}; ed.dirty = true; }
            if (ImGui::SliderFloat("Value##urp", &rp->value, 0.0f, 1.0f)) ed.dirty = true;
            if (ImGui::DragFloat("Thickness##urp", &rp->thickness, 0.5f, 0.0f, 200.0f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Ring width in px (0 = filled pie)");
            if (ImGui::DragFloat("Start Angle##urp", &rp->startAngle, 1.0f, -360.0f, 360.0f, "%.0f deg")) ed.dirty = true;
            if (ImGui::Checkbox("Clockwise##urp", &rp->clockwise)) ed.dirty = true;
            if (ImGui::Checkbox("Spin (loader)##urp", &rp->spin)) ed.dirty = true;
            if (rp->spin)
                if (ImGui::DragFloat("Spin Speed##urp", &rp->spinSpeed, 2.0f, -1080.0f, 1080.0f, "%.0f deg/s")) ed.dirty = true;
            float bgc[4] = {rp->background.r, rp->background.g, rp->background.b, rp->background.a};
            if (ImGui::ColorEdit4("Track##urp", bgc)) { rp->background = {bgc[0], bgc[1], bgc[2], bgc[3]}; ed.dirty = true; }
            float flc[4] = {rp->fill.r, rp->fill.g, rp->fill.b, rp->fill.a};
            if (ImGui::ColorEdit4("Fill##urp", flc)) { rp->fill = {flc[0], flc[1], flc[2], flc[3]}; ed.dirty = true; }
            AnchorCombo("Anchor##urp", rp->anchor, ed);
            if (ImGui::Checkbox("Show Percent##urp", &rp->showPercent)) ed.dirty = true;
            ImGui::TextDisabled("script: set_progress(0..1) on cooldowns / loaders");
            if (ImGui::SmallButton("Remove##urp")) toRemove = rp;
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
            int ishp = (int)im->shape;
            const char* ishapes[] = {"Rectangle", "Rounded", "Circle", "Pill"};
            if (ImGui::Combo("Shape (no texture)##uim", &ishp, ishapes, 4)) { im->shape = (UIShape)ishp; ed.dirty = true; }
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
            int sshp = (int)sl->trackShape;
            const char* sshapes[] = {"Rectangle", "Rounded", "Circle", "Pill"};
            if (ImGui::Combo("Track Shape##usl", &sshp, sshapes, 4)) { sl->trackShape = (UIShape)sshp; ed.dirty = true; }
            if (sl->trackShape == UIShape::Rounded)
                if (ImGui::DragFloat("Corner Radius##usl", &sl->cornerRadius, 0.2f, 0.0f, 64.0f)) ed.dirty = true;
            if (ImGui::Checkbox("Round Knob##usl", &sl->roundKnob)) ed.dirty = true;
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
            if (ImGui::DragFloat("Anim Speed##utg", &tg->animSpeed, 0.2f, 0.0f, 40.0f)) ed.dirty = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Knob glide / check fade speed (0 = snap).");
            if (ImGui::Checkbox("Interactable##utg", &tg->interactable)) ed.dirty = true;
            if (ImGui::SmallButton("Remove##utg")) toRemove = tg;
        }
    }
    if (auto* tb = go->GetComponent<UITabs>()) {
        if (CompHeader("UI Tabs", tb, &toRemove)) {
            float pos[2] = {tb->position.x, tb->position.y};
            if (ImGui::DragFloat2("Pos (px)##utb", pos, 1.0f)) { tb->position = {pos[0], pos[1]}; ed.dirty = true; }
            float sz[2] = {tb->size.x, tb->size.y};
            if (ImGui::DragFloat2("Size (px)##utb", sz, 1.0f, 8.0f, 4000.0f)) { tb->size = {sz[0], sz[1]}; ed.dirty = true; }
            AnchorCombo("Anchor##utb", tb->anchor, ed);
            int tshp = (int)tb->shape;
            const char* tshapes[] = {"Rectangle", "Rounded", "Circle", "Pill"};
            if (ImGui::Combo("Shape##utb", &tshp, tshapes, 4)) { tb->shape = (UIShape)tshp; ed.dirty = true; }
            if (tb->shape == UIShape::Rounded)
                if (ImGui::DragFloat("Corner Radius##utb", &tb->cornerRadius, 0.2f, 0.0f, 64.0f)) ed.dirty = true;
            if (tb->Count() > 0) {
                int v = tb->value;
                if (ImGui::SliderInt("Selected##utb", &v, 0, tb->Count() - 1)) { tb->value = v; ed.dirty = true; }
            }
            float se[4] = {tb->selected.r, tb->selected.g, tb->selected.b, tb->selected.a};
            if (ImGui::ColorEdit4("Selected Color##utb", se)) { tb->selected = {se[0], se[1], se[2], se[3]}; ed.dirty = true; }
            float bgc[4] = {tb->background.r, tb->background.g, tb->background.b, tb->background.a};
            if (ImGui::ColorEdit4("Track Color##utb", bgc)) { tb->background = {bgc[0], bgc[1], bgc[2], bgc[3]}; ed.dirty = true; }
            ImGui::SeparatorText("Tabs");
            int eraseT = -1;
            for (int i = 0; i < (int)tb->tabs.size(); ++i) {
                ImGui::PushID(i);
                char ob[64]; std::strncpy(ob, tb->tabs[i].c_str(), sizeof(ob) - 1); ob[sizeof(ob) - 1] = '\0';
                ImGui::SetNextItemWidth(160);
                if (ImGui::InputText("##tab", ob, sizeof(ob))) { tb->tabs[i] = ob; ed.dirty = true; }
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) eraseT = i;
                ImGui::PopID();
            }
            if (eraseT >= 0) { tb->tabs.erase(tb->tabs.begin() + eraseT); if (tb->value >= tb->Count()) tb->value = tb->Count() - 1; ed.dirty = true; }
            if (ImGui::SmallButton("Add Tab##utb")) { tb->tabs.push_back("Tab"); ed.dirty = true; }
            ImGui::TextDisabled("Click a segment in the game; calls script on_change().");
            if (ImGui::SmallButton("Remove##utb")) toRemove = tb;
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
    if (auto* anm = go->GetComponent<Animator>()) {
        if (CompHeader("Animator", anm, &toRemove)) {
            char cn[64]; std::strncpy(cn, anm->clip.name.c_str(), sizeof(cn) - 1); cn[sizeof(cn)-1] = '\0';
            if (ImGui::InputText("Clip##anm", cn, sizeof(cn))) { anm->clip.name = cn; ed.dirty = true; }
            if (ImGui::Checkbox("Loop##anm", &anm->clip.loop)) { anm->clip.Recompute(); ed.dirty = true; }
            ImGui::SameLine();
            if (ImGui::Checkbox("Playing##anm", &anm->playing)) ed.dirty = true;
            if (ImGui::DragFloat("Speed##anm", &anm->speed, 0.02f, -4.0f, 4.0f)) ed.dirty = true;

            // Playback scrubber: drag to preview the clip at any time.
            float len = anm->clip.Length();
            float t = anm->Time();
            ImGui::SetNextItemWidth(-90);
            if (ImGui::SliderFloat("Time##anm", &t, 0.0f, len > 0 ? len : 1.0f, "%.2fs")) {
                anm->SetTime(t); ed.dirty = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(anm->playing ? "Pause" : "Play")) anm->playing = !anm->playing;
            ImGui::SameLine();
            if (ImGui::SmallButton("|<")) anm->Restart();

            ImGui::SeparatorText("Keyframes");
            ImGui::TextDisabled("Scrub to a time, pose the object, then Record.");
            // Record the object's current transform into keys at the current time.
            if (ImGui::Button("Record Key (transform @ time)")) {
                float kt = anm->Time();
                Vec3 p = go->transform->localPosition, s = go->transform->localScale;
                Quat q = go->transform->localRotation;       // Z angle for the rotation.z track
                float zdeg = 2.0f * std::atan2(q.z, q.w) * Mathf::Rad2Deg;
                anm->clip.AddKey("position.x", kt, p.x); anm->clip.AddKey("position.y", kt, p.y);
                anm->clip.AddKey("position.z", kt, p.z);
                anm->clip.AddKey("scale.x", kt, s.x);    anm->clip.AddKey("scale.y", kt, s.y);
                anm->clip.AddKey("scale.z", kt, s.z);
                anm->clip.AddKey("rotation.z", kt, zdeg);
                ed.dirty = true; ConsoleLog("Recorded keyframe at " + std::to_string(kt) + "s");
            }
            // Per-track key counts, with a way to drop a track.
            std::vector<std::string> tracks;
            for (auto& tr : anm->clip.Tracks()) tracks.push_back(tr.first);
            std::sort(tracks.begin(), tracks.end());
            std::string dropTrack;
            for (auto& tn : tracks) {
                AnimationCurve* c = anm->clip.Track(tn);
                ImGui::BulletText("%s  (%d keys)", tn.c_str(), c ? (int)c->Count() : 0);
                ImGui::SameLine();
                ImGui::PushID(tn.c_str());
                if (ImGui::SmallButton("Clear")) dropTrack = tn;
                ImGui::PopID();
            }
            if (!dropTrack.empty()) { anm->clip.RemoveTrack(dropTrack); ed.dirty = true; }
            if (tracks.empty()) ImGui::TextDisabled("(no tracks yet — Record a key)");
            ImGui::Spacing();
            if (ImGui::SmallButton("Remove##anm-comp")) toRemove = anm;
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
            if (item(!go->GetComponent<Character>(), "Character")) { go->AddComponent<Character>()->Apply(); ed.view3D = true; ed.dirty = true; }
            if (item(!go->GetComponent<TextRenderer>(), "Text")) { go->AddComponent<TextRenderer>(); ed.dirty = true; }
            if (item(!go->GetComponent<SpriteAnimator>(), "Sprite Animator")) { go->AddComponent<SpriteAnimator>(); ed.dirty = true; }
            if (item(!go->GetComponent<ParticleSystem>(), "Particle System")) { go->AddComponent<ParticleSystem>(); ed.dirty = true; }
            if (item(!go->GetComponent<Draggable>(), "Draggable (item)")) { go->AddComponent<Draggable>(); ed.dirty = true; }
            if (item(!go->GetComponent<DropZone>(), "Drop Zone (item)")) { go->AddComponent<DropZone>(); ed.dirty = true; }
          } EndCat(o); }

        { bool o = BeginCat("Animation");
          if (o) {
            if (item(!go->GetComponent<Animator>(), "Animator (keyframes)")) { go->AddComponent<Animator>(); ed.dirty = true; }
            if (item(!go->GetComponent<SpriteAnimator>(), "Sprite Animator")) { go->AddComponent<SpriteAnimator>(); ed.dirty = true; }
          } EndCat(o); }

        { bool o = BeginCat("Physics 2D");
          if (o) {
            if (item(!go->GetComponent<Rigidbody2D>(), "Rigidbody2D")) { go->AddComponent<Rigidbody2D>(); ed.dirty = true; }
            if (item(!go->GetComponent<BoxCollider2D>(), "Box Collider 2D")) { go->AddComponent<BoxCollider2D>(); FitColliders(go); ed.dirty = true; }
            if (item(!go->GetComponent<CircleCollider2D>(), "Circle Collider 2D")) { go->AddComponent<CircleCollider2D>(); FitColliders(go); ed.dirty = true; }
            if (item(!go->GetComponent<CapsuleCollider2D>(), "Capsule Collider 2D")) { go->AddComponent<CapsuleCollider2D>(); FitColliders(go); ed.dirty = true; }
            if (item(go->GetComponent<Tilemap>() && !go->GetComponent<TilemapCollider2D>(), "Tilemap Collider 2D")) { go->AddComponent<TilemapCollider2D>(); ed.dirty = true; }
          } EndCat(o); }

        { bool o = BeginCat("Physics 3D");
          if (o) {
            if (item(!go->GetComponent<Rigidbody3D>(), "Rigidbody3D")) { go->AddComponent<Rigidbody3D>(); ed.dirty = true; }
            if (item(!go->GetComponent<BoxCollider3D>(), "Box Collider 3D")) { go->AddComponent<BoxCollider3D>(); FitColliders(go); ed.dirty = true; }
            if (item(!go->GetComponent<SphereCollider3D>(), "Sphere Collider 3D")) { go->AddComponent<SphereCollider3D>(); FitColliders(go); ed.dirty = true; }
            if (item(!go->GetComponent<CapsuleCollider3D>(), "Capsule Collider 3D")) { go->AddComponent<CapsuleCollider3D>(); FitColliders(go); ed.dirty = true; }
          } EndCat(o); }

        { bool o = BeginCat("Lighting");
          if (o) {
            if (item(!go->GetComponent<Light>(), "Directional Light")) { go->AddComponent<Light>(); ed.dirty = true; }
          } EndCat(o); }

        { bool o = BeginCat("Camera");
          if (o) {
            if (item(!go->GetComponent<Camera>(), "Camera")) { go->AddComponent<Camera>(); ed.dirty = true; }
            if (item(!go->GetComponent<CameraFollow>(), "Camera Follow")) { go->AddComponent<CameraFollow>(); ed.dirty = true; }
            if (item(!go->GetComponent<VirtualCamera>(), "Virtual Camera")) { go->AddComponent<VirtualCamera>(); ed.dirty = true; }
            if (item(!go->GetComponent<CinemachineBrain>(), "Cinemachine Brain")) { go->AddComponent<CinemachineBrain>(); ed.dirty = true; }
            if (item(!go->GetComponent<DollyPath>(), "Dolly Path")) { go->AddComponent<DollyPath>(); ed.dirty = true; }
            if (item(!go->GetComponent<DollyCart>(), "Dolly Cart")) { go->AddComponent<DollyCart>(); ed.dirty = true; }
          } EndCat(o); }

        { bool o = BeginCat("Scripts");
          if (o) {
            // A GameObject can carry MANY scripts (like Unity). Existing .okay
            // scripts show up directly (one click attaches one). You can attach
            // as many as you like, including the same script more than once
            // (Unity allows multiple instances). "New Script..." makes a file.
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
                    std::string full = e.path().string();
                    std::string rel = fs::relative(e.path(), assets, ec).string();
                    if (F(rel.c_str()) && ImGui::MenuItem(rel.c_str())) {
                        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
                        std::string err; sc->LoadFile(full, &err); sc->SetPath(full);
                        ConsoleLog("Attached script " + full);
                        ed.dirty = true;
                    }
                }
            }
            if (F("New Script...") && ImGui::MenuItem("New Script...")) {
                g_newScriptGO = go; g_newScriptOpen = true;
                std::snprintf(g_newScriptName, sizeof(g_newScriptName), "%sScript", go->name.c_str());
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
            if (item(!go->GetComponent<FirstPersonController>(), "First Person Controller")) { go->AddComponent<FirstPersonController>(); ed.dirty = true; }
            if (item(!go->GetComponent<ThirdPersonController>(), "Third Person Controller")) { go->AddComponent<ThirdPersonController>(); ed.dirty = true; }
            if (item(!go->GetComponent<TopDownController>(), "Top Down Controller")) { go->AddComponent<TopDownController>(); ed.dirty = true; }
            if (item(!go->GetComponent<ClickToMoveController>(), "Click To Move Controller")) { go->AddComponent<ClickToMoveController>(); ed.dirty = true; }
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
            if (item(!go->GetComponent<UIRadialProgress>(), "UI Radial Progress")) { go->AddComponent<UIRadialProgress>(); ed.dirty = true; }
            if (item(!go->GetComponent<UISlider>(), "UI Slider")) { go->AddComponent<UISlider>(); ed.dirty = true; }
            if (item(!go->GetComponent<UIToggle>(), "UI Toggle")) { go->AddComponent<UIToggle>(); ed.dirty = true; }
            if (item(!go->GetComponent<UITabs>(), "UI Tabs")) { go->AddComponent<UITabs>(); ed.dirty = true; }
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
                    // Always attach a new Script (objects can hold several, even
                    // multiple instances of the same script) — like Unity.
                    auto* nsc = go->AddComponent<ScriptComponent>("okayscript");
                    std::string err; nsc->LoadFile(path, &err); nsc->SetPath(path);
                    SetCodeBuffer(nsc, nsc->Source());   // fresh editor buffer for it
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
    // Only trust mainCamera if it still points to a LIVE object — deleting the
    // main camera leaves a dangling pointer, which made the Game view go black
    // even after adding a replacement. Comparing pointers never dereferences the
    // stale one; if it's gone we clear it and pick another camera.
    if (s.mainCamera) {
        for (const auto& go : s.Objects())
            if (go->GetComponent<Camera>() == s.mainCamera) return s.mainCamera;
        s.mainCamera = nullptr;   // it was deleted
    }
    for (const auto& go : s.Objects())
        if (go->active)
            if (auto* c = go->GetComponent<Camera>()) { s.mainCamera = c; return c; }
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
        if (!sv || !up->active || UIHidden(up.get())) continue;
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

    // Drop-target slot backgrounds (behind items), so slots are visible while
    // designing — mirrors what the running game draws.
    for (const auto& up : objs) {
        auto* dt = up->GetComponent<UIDropTarget>();
        if (!dt || !up->active || !dt->drawBackground || UIHidden(up.get())) continue;
        Vec2 o, sz; GetUIScreenRect(up.get(), canvasSize.x, canvasSize.y, o, sz);
        ImVec2 a(canvasPos.x + o.x, canvasPos.y + o.y), b(a.x + sz.x, a.y + sz.y);
        dl->AddRectFilled(a, b, ToColor(dt->background), dt->cornerRadius);
        if (dt->borderWidth > 0.0f)
            dl->AddRect(a, b, ToColor(dt->borderColor), dt->cornerRadius, 0, dt->borderWidth);
    }

    // UI images (logos/icons): preview as a tinted rect with the path centered.
    for (const auto& up : objs) {
        auto* im = up->GetComponent<UIImage>();
        if (!im || !up->active || UIHidden(up.get())) continue;
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
        if (!pn || !up->active || UIHidden(up.get())) continue;
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
        if (!pb || !up->active || UIHidden(up.get())) continue;
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
        if (!sl || !up->active || UIHidden(up.get())) continue;
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
        if (!tg || !up->active || UIHidden(up.get())) continue;
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
        if (!btn || !up->active || UIHidden(up.get())) continue;
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
        if (!in || !up->active || UIHidden(up.get())) continue;
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

    // Screen-space text (world-anchored text stays with the 2D scene draw). Drawn
    // AFTER panels/images/controls so a label always sits ON TOP of a panel rather
    // than being hidden behind it (only dropdown popups + tooltips go above it).
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

    // UI dropdowns: header (shows the selection + a caret); when open, the option
    // list below with the hovered/selected option highlighted. Drawn last among
    // widgets so an open list sits above its neighbours.
    for (const auto& up : objs) {
        auto* dd = up->GetComponent<UIDropdown>();
        if (!dd || !up->active || UIHidden(up.get())) continue;
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
            if (!sv || !up->active || UIHidden(up.get())) continue;
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
    // Draw sprites back-to-front by Sort Order (scene order breaks ties), so the
    // editor preview layers them the same way the built game does.
    {
        std::vector<GameObject*> sprites;
        for (const auto& up : objs)
            if (up->active && up->GetComponent<SpriteRenderer>()) sprites.push_back(up.get());
        std::stable_sort(sprites.begin(), sprites.end(), [](GameObject* x, GameObject* y) {
            return x->GetComponent<SpriteRenderer>()->sortOrder <
                   y->GetComponent<SpriteRenderer>()->sortOrder;
        });
        for (GameObject* go : sprites) {
            auto* sr = go->GetComponent<SpriteRenderer>();
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
    }

    // Tilemaps: filled cells, color keyed by tile id.
    for (const auto& up : objs) {
        auto* tm = up->GetComponent<Tilemap>();
        if (!tm || !up->active || UIHidden(up.get())) continue;
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
        if (!ps || !up->active || UIHidden(up.get())) continue;
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

    // Resize handles for a selected 2D sprite: drag a corner/edge to change its
    // Size (the same 8-handle affordance UI widgets have, which sprites lacked).
    if (!gameView && ed.selected() && !IsUIElement(ed.selected())) {
        if (auto* sr = ed.selected()->GetComponent<SpriteRenderer>()) {
            Vec3 c = ed.selected()->transform->Position();
            Vec3 ls = ed.selected()->transform->LossyScale();
            float hx = sr->size.x * ls.x * 0.5f, hy = sr->size.y * ls.y * 0.5f;
            ImVec2 a = worldToScreen(Vec3{c.x - hx, c.y + hy, 0.0f});   // top-left (screen)
            ImVec2 b = worldToScreen(Vec3{c.x + hx, c.y - hy, 0.0f});   // bottom-right
            dl->AddRect(a, b, IM_COL32(255, 200, 0, 200), 0.0f, 0, 1.5f);
            ImVec2 h[8]; UIHandlePositions(a, b, h);
            for (int i = 0; i < 8; ++i)
                dl->AddRectFilled(ImVec2(h[i].x - 4, h[i].y - 4),
                                  ImVec2(h[i].x + 4, h[i].y + 4), IM_COL32(255, 200, 0, 255));
        }
    }

    dl->PopClipRect();

    if (gameView) return;   // the Game view is non-interactive

    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) g_spriteHandle = -1;

    if (hovered && !g_uiHandled && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // A resize handle on the selected sprite takes priority over (re)selection.
        g_spriteHandle = -1;
        if (ed.selected() && !IsUIElement(ed.selected())) {
            if (auto* sr = ed.selected()->GetComponent<SpriteRenderer>()) {
                Vec3 c = ed.selected()->transform->Position();
                Vec3 ls = ed.selected()->transform->LossyScale();
                float hx = sr->size.x * ls.x * 0.5f, hy = sr->size.y * ls.y * 0.5f;
                ImVec2 a = worldToScreen(Vec3{c.x - hx, c.y + hy, 0.0f});
                ImVec2 b = worldToScreen(Vec3{c.x + hx, c.y - hy, 0.0f});
                ImVec2 h[8]; UIHandlePositions(a, b, h);
                for (int i = 0; i < 8; ++i) {
                    float dx = io.MousePos.x - h[i].x, dy = io.MousePos.y - h[i].y;
                    if (dx * dx + dy * dy <= 9.0f * 9.0f) { ed.PushUndo(); g_spriteHandle = i; break; }
                }
            }
        }
        if (g_spriteHandle < 0) {                       // no handle grabbed -> pick a sprite
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
    }
    // Dragging a sprite resize handle changes its Size (opposite edge stays put).
    if (g_spriteHandle >= 0 && ed.selected() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        Transform* t = ed.selected()->transform;
        if (auto* sr = ed.selected()->GetComponent<SpriteRenderer>()) {
            Vec3 ls = t->LossyScale();
            if (Mathf::Abs(ls.x) < 1e-4f) ls.x = 1.0f;
            if (Mathf::Abs(ls.y) < 1e-4f) ls.y = 1.0f;
            Vec3 c = t->Position();
            float hx = sr->size.x * ls.x * 0.5f, hy = sr->size.y * ls.y * 0.5f;
            float left = c.x - hx, right = c.x + hx, bottom = c.y - hy, top = c.y + hy;
            Vec2 mw = screenToWorld(io.MousePos);
            bool L = (g_spriteHandle == 0 || g_spriteHandle == 6 || g_spriteHandle == 7);
            bool R = (g_spriteHandle == 2 || g_spriteHandle == 3 || g_spriteHandle == 4);
            bool T = (g_spriteHandle == 0 || g_spriteHandle == 1 || g_spriteHandle == 2);
            bool B = (g_spriteHandle == 4 || g_spriteHandle == 5 || g_spriteHandle == 6);
            if (L) left   = Mathf::Min(mw.x, right - 0.01f);
            if (R) right  = Mathf::Max(mw.x, left + 0.01f);
            if (T) top    = Mathf::Max(mw.y, bottom + 0.01f);
            if (B) bottom = Mathf::Min(mw.y, top - 0.01f);
            sr->size.x = (right - left) / ls.x;
            sr->size.y = (top - bottom) / ls.y;
            t->localPosition.x += (left + right) * 0.5f - c.x;   // keep the opposite edge fixed
            t->localPosition.y += (bottom + top) * 0.5f - c.y;
            ed.dirty = true;
        }
    }
    // Selecting and dragging work in Play too (edits revert on Stop, like Unity).
    // Skipped while a UI widget or a sprite handle is being dragged.
    else if (ed.selected() && hovered && !g_uiHandled && g_spriteHandle < 0 &&
        !IsUIElement(ed.selected()) &&
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
    Mat4 proj = Mat4::Perspective(g_editorFov, canvasSize.x / canvasSize.y, g_editorNear, 2000.0f);
    // Where the 3D image is drawn within the panel. The Game view honors the main
    // camera's normalized viewport rect (Unity's Camera.rect) for split-screen /
    // mini-map / picture-in-picture; the Scene view always fills the panel.
    ImVec2 view3dMin = canvasPos, view3dMax = canvasEnd;
    // The Game view renders through the scene's main camera instead.
    if (gameView) {
        if (Camera* mc = SceneCamera(ed.scene())) {
            eye = mc->gameObject->transform->Position();
            view = mc->ViewMatrix();
            // Clamp the rect to 0..1 and compute the on-panel pixel rectangle. Unity's
            // rect y is measured from the BOTTOM, so flip it for top-left screen space.
            float rw = Mathf::Clamp(mc->rectW, 0.01f, 1.0f), rh = Mathf::Clamp(mc->rectH, 0.01f, 1.0f);
            float rx = Mathf::Clamp(mc->rectX, 0.0f, 1.0f - rw), ry = Mathf::Clamp(mc->rectY, 0.0f, 1.0f - rh);
            view3dMin = ImVec2(canvasPos.x + rx * canvasSize.x,
                               canvasPos.y + (1.0f - ry - rh) * canvasSize.y);
            view3dMax = ImVec2(view3dMin.x + rw * canvasSize.x, view3dMin.y + rh * canvasSize.y);
            proj = mc->ProjectionMatrix((rw * canvasSize.x) / (rh * canvasSize.y));
        }
    }
    Mat4 vp = proj * view;
    if (!gameView) {   // remember this exact frame for the debug PNG capture
        g_lastSceneVP = vp; g_lastSceneEye = eye;
        g_lastSceneW = (int)canvasSize.x; g_lastSceneH = (int)canvasSize.y;
    }

    auto toScreen = [&](const Vec4& c, ImVec2& out) -> bool {
        if (c.w <= 0.05f) return false;
        out = ImVec2(center.x + (c.x / c.w) * canvasSize.x * 0.5f,
                     center.y - (c.y / c.w) * canvasSize.y * 0.5f);
        return true;
    };
    // Project an already-in-front clip-space point (w > 0) to the screen.
    auto projClip = [&](const Vec4& c) -> ImVec2 {
        return ImVec2(center.x + (c.x / c.w) * canvasSize.x * 0.5f,
                      center.y - (c.y / c.w) * canvasSize.y * 0.5f);
    };
    // Draw a line from two CLIP-SPACE endpoints, clipped to the near plane. The
    // mesh renderer near-clips its triangles, but gizmo lines used to just DROP any
    // endpoint at/behind the camera (w <= eps) — so the selection box / grid lost
    // their near edges and no longer matched the geometry. Clipping the segment to
    // the near plane keeps the visible part and the outline aligned with the mesh.
    auto clipLine = [&](Vec4 a, Vec4 b, ImU32 col, float th = 1.5f) {
        // Clip to the TRUE near plane (z + w >= 0), exactly like the mesh renderer —
        // NOT w > small. Clipping on w alone left endpoints between the camera and
        // the near plane in play, where 1/w explodes; gizmo/grid lines then projected
        // to enormous coordinates and SNAPPED/SWUNG as the camera zoomed (glitchy
        // lines while moving). Clipping at the near plane lands endpoints at w≈near,
        // so coordinates stay sane and the lines move smoothly.
        float da = a.z + a.w, db = b.z + b.w;          // >0 = in front of near plane
        if (da <= 0.0f && db <= 0.0f) return;          // wholly clipped
        auto lerp = [](const Vec4& p, const Vec4& q, float t) {
            return Vec4{p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t,
                        p.z + (q.z - p.z) * t, p.w + (q.w - p.w) * t};
        };
        float t = da / (da - db);                      // param a->b where z+w crosses 0
        if (da <= 0.0f)      a = lerp(a, b, t);
        else if (db <= 0.0f) b = lerp(a, b, t);
        dl->AddLine(projClip(a), projClip(b), col, th);
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
    if (rs.skybox && !solidClear && (gameView || g_sceneSkybox)) {
        ImU32 top = ToColor(rs.skyTop);
        ImU32 mid = ToColor(rs.skyHorizon);
        ImU32 bot = ToColor(rs.skyBottom);
        ImVec2 cmid(canvasEnd.x, (canvasPos.y + canvasEnd.y) * 0.5f);
        dl->AddRectFilledMultiColor(canvasPos, cmid, top, top, mid, mid);
        dl->AddRectFilledMultiColor(ImVec2(canvasPos.x, cmid.y), canvasEnd, mid, mid, bot, bot);
    }

    // Ground grid on the XZ plane (Scene view only; the Game view is clean).
    if (!gameView && g_showGrid)
    for (int i = -10; i <= 10; ++i) {
        clipLine(vp * Vec4{Vec3{(float)i, 0, -10}, 1},
                 vp * Vec4{Vec3{(float)i, 0, 10}, 1}, IM_COL32(50, 50, 64, 255), 1.0f);
        clipLine(vp * Vec4{Vec3{-10, 0, (float)i}, 1},
                 vp * Vec4{Vec3{10, 0, (float)i}, 1}, IM_COL32(50, 50, 64, 255), 1.0f);
    }
    // World axes at the origin (debug): X red, Y green, Z blue.
    if (!gameView && g_showWorldAxes) {
        clipLine(vp * Vec4{Vec3{0,0,0},1}, vp * Vec4{Vec3{5,0,0},1}, IM_COL32(230,70,70,255), 2.0f);
        clipLine(vp * Vec4{Vec3{0,0,0},1}, vp * Vec4{Vec3{0,5,0},1}, IM_COL32(90,220,90,255), 2.0f);
        clipLine(vp * Vec4{Vec3{0,0,0},1}, vp * Vec4{Vec3{0,0,5},1}, IM_COL32(90,150,255,255), 2.0f);
    }

    const auto& objs = ed.scene().Objects();

    // Editor gizmos for non-visual objects (Scene view only): a wireframe
    // frustum for cameras, a sun + direction arrow for lights, so you can see
    // and place them even though they don't render a mesh.
    if (!gameView && g_showGizmos) {
        auto line = [&](const Vec3& a, const Vec3& b, ImU32 col, float th = 1.5f) {
            clipLine(vp * Vec4{a, 1}, vp * Vec4{b, 1}, col, th);   // near-clipped
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
                // A camera looks down its local -Z, so the frustum must extend along
                // -Forward (it used to point the opposite way to the actual view).
                Vec3 f = t->Forward() * -1.0f, r = t->Right(), u = t->Up();
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
    // In the Game view, honor the active camera's ignoreObject (first-person body);
    // the Scene view always shows everything (ignore = null).
    const GameObject* viewIgnore = nullptr;
    if (gameView) {
        if (Camera* gc = SceneCamera(ed.scene())) {
            viewIgnore = gc->ignoreObject;   // set at runtime by the controller (Play)
            // In edit mode the controller hasn't run yet, so the Game preview would
            // still show the body. Derive it: a first-person camera's owner (parent)
            // with "Show Body" off should be ignored, matching how Play looks.
            if (!viewIgnore && gc->transform && gc->transform->Parent() &&
                gc->transform->Parent()->gameObject) {
                GameObject* owner = gc->transform->Parent()->gameObject;
                if (auto* fpc = owner->GetComponent<FirstPersonController>())
                    if (!fpc->showBody) viewIgnore = owner;
            }
        }
    }
    // Render the 3D view at the display's TRUE physical pixel resolution. On a
    // high-DPI / Windows-scaled display, ImGui coords are logical pixels but the
    // renderer scales to physical pixels (SDL_RenderSetScale by the framebuffer
    // scale). Rendering the 3D texture at logical size left it to be LINEARLY
    // upscaled to physical — blurry, and it shimmers/crawls while the camera moves
    // (the resample shifts each frame). Rendering at physical size maps it 1:1, so
    // it's sharp and stable in motion. The texture is still drawn to the logical
    // canvas rect (AddImage), and SDL's scale maps that to the same physical area.
    float dpi = ImGui::GetIO().DisplayFramebufferScale.x;
    if (dpi < 1.0f || dpi > 4.0f) dpi = 1.0f;
    // The Game view honors the main camera's layer culling mask; the Scene view
    // always shows every layer so you can edit hidden objects.
    RenderCullingMask() = (gameView && SceneCamera(ed.scene())) ? SceneCamera(ed.scene())->cullingMask : ~0;
    float v3w = view3dMax.x - view3dMin.x, v3h = view3dMax.y - view3dMin.y;
    if (SDL_Texture* tex = Render3DTexture(ed.scene(), vp, eye,
                                           (int)(v3w * dpi), (int)(v3h * dpi),
                                           gameView ? 1 : 0, viewIgnore))
        dl->AddImage((ImTextureID)tex, view3dMin, view3dMax);

    // Highlight the selection with a clean yellow bounding box (12 edges).
    if (!gameView && g_showGizmos && ed.selected()) {
        if (auto* mr = ed.selected()->GetComponent<MeshRenderer>()) {
            Mat4 m = vp * ed.selected()->transform->LocalToWorldMatrix();
            Vec3 lo, hi; mr->mesh.Bounds(lo, hi);
            Vec4 corner[8];   // keep clip-space so the edges can be near-clipped
            for (int ci = 0; ci < 8; ++ci)
                corner[ci] = m * Vec4{Vec3{(ci & 1) ? hi.x : lo.x,
                                           (ci & 2) ? hi.y : lo.y,
                                           (ci & 4) ? hi.z : lo.z}, 1};
            static const int edges[12][2] = {
                {0,1},{1,3},{3,2},{2,0}, {4,5},{5,7},{7,6},{6,4}, {0,4},{1,5},{2,6},{3,7}};
            ImU32 y = IM_COL32(255, 200, 0, 220);
            for (auto& e : edges) clipLine(corner[e[0]], corner[e[1]], y, 1.5f);
        }
    }

    // Wireframe meshes (wireframe == true), plus the debug "Wireframe (all meshes)"
    // toggle which draws EVERY mesh as edges in the Scene view. Edges only, over the
    // solids, near-plane clipped so close-up geometry stays correct.
    for (const auto& up : objs) {
        GameObject* go = up.get();
        auto* mr = go->GetComponent<MeshRenderer>();
        if (!mr || !go->active) continue;
        if (!mr->wireframe && !(g_wireframeAll && !gameView)) continue;
        Mat4 m = vp * go->transform->LocalToWorldMatrix();
        ImU32 col = (go == ed.selected()) ? IM_COL32(255, 200, 0, 255) : ToColor(mr->color);
        const auto& v = mr->mesh.vertices;
        const auto& t = mr->mesh.triangles;
        for (size_t i = 0; i + 2 < t.size(); i += 3) {
            Vec4 c0 = m * Vec4{v[t[i]], 1}, c1 = m * Vec4{v[t[i + 1]], 1}, c2 = m * Vec4{v[t[i + 2]], 1};
            clipLine(c0, c1, col, 1.0f);
            clipLine(c1, c2, col, 1.0f);
            clipLine(c2, c0, col, 1.0f);
        }
    }

    // Camera HUD (Scene view debug): on-screen readout of the orbit camera so a
    // screenshot tells you the exact zoom / angle / position you're looking from.
    // Editor-only; never drawn in the Game view or a build.
    if (!gameView && g_showCamHud) {
        char l1[64], l2[64], l3[80], l4[80], l5[48];
        std::snprintf(l1, sizeof(l1), "Zoom (dist): %.1f", ed.camDist);
        std::snprintf(l2, sizeof(l2), "Yaw: %.0f%s  Pitch: %.0f%s",
                      ed.camYaw, "\xc2\xb0", ed.camPitch, "\xc2\xb0");
        std::snprintf(l3, sizeof(l3), "Eye: (%.1f, %.1f, %.1f)", eye.x, eye.y, eye.z);
        std::snprintf(l4, sizeof(l4), "Target: (%.1f, %.1f, %.1f)",
                      ed.camTarget.x, ed.camTarget.y, ed.camTarget.z);
        std::snprintf(l5, sizeof(l5), "FOV: %.0f%s", g_editorFov, "\xc2\xb0");
        const char* lines[5] = {l1, l2, l3, l4, l5};
        float lh = ImGui::GetTextLineHeight();
        ImVec2 box0(canvasPos.x + 8, canvasPos.y + 8);
        ImVec2 box1(box0.x + 188, box0.y + lh * 5 + 12);
        dl->AddRectFilled(box0, box1, IM_COL32(0, 0, 0, 140), 4.0f);
        for (int i = 0; i < 5; ++i)
            dl->AddText(ImVec2(box0.x + 8, box0.y + 6 + lh * i),
                        IM_COL32(120, 230, 150, 255), lines[i]);
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
                                                    (int)pw, (int)ph, 2, pc->ignoreObject))
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
                    float lx = hit.x - op.x, lz = hit.z - op.z;
                    float dt = io.DeltaTime, sign = io.KeyShift ? -1.0f : 1.0f;
                    switch (g_terrainBrush) {
                        case 1:  // Smooth
                            terr->SmoothAt(lx, lz, g_terrainRadius, std::min(1.0f, g_terrainStrength * dt));
                            break;
                        case 2:  // Flatten: pull toward the height under the cursor
                            terr->FlattenAt(lx, lz, g_terrainRadius, terr->SampleHeight(lx, lz),
                                            std::min(1.0f, g_terrainStrength * dt));
                            break;
                        case 3:  // Set Height: pull toward the explicit target
                            terr->FlattenAt(lx, lz, g_terrainRadius, g_terrainFlattenH,
                                            std::min(1.0f, g_terrainStrength * dt));
                            break;
                        default: // Raise / Lower
                            terr->RaiseAt(lx, lz, g_terrainRadius, g_terrainStrength * dt * sign);
                            break;
                    }
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

        // View basis at the object (camera-facing) for the screen-aligned ring
        // and for fading the back half of each ring, Unity-style.
        Vec3 vToCam = eye - o;
        Vec3 vdir   = vToCam.Normalized();
        Vec3 vright = Vec3::Cross(Vec3::Up, vdir).Normalized();
        Vec3 vup    = Vec3::Cross(vdir, vright);

        // Rotate uses Unity-style rings: a circle per axis (X/Y/Z) plus an outer
        // screen-aligned ring (i==3) for view-axis "trackball" spin. Move/Scale
        // use straight arms with knobs.
        const int RING_SEG = 64;
        const float VIEW_RAD = L * 1.2f;
        auto rotAxisOf = [&](int i) -> Vec3 { return i < 3 ? axis[i] : vdir; };
        auto ringPt = [&](int i, float ang, ImVec2& out) -> bool {
            Vec3 u, v; float rad;
            if (i < 3) { u = axis[(i + 1) % 3]; v = axis[(i + 2) % 3]; rad = L; }
            else       { u = vright;            v = vup;              rad = VIEW_RAD; }
            Vec3 p = o + (u * Mathf::Cos(ang) + v * Mathf::Sin(ang)) * rad;
            return toScreen(vp * Vec4{p, 1}, out);
        };
        // Is the ring point at this angle on the camera-facing half?
        auto ringFront = [&](int i, float ang) -> bool {
            if (i >= 3) return true;
            Vec3 u = axis[(i + 1) % 3], v = axis[(i + 2) % 3];
            Vec3 rel = u * Mathf::Cos(ang) + v * Mathf::Sin(ang);
            return (rel.x * vToCam.x + rel.y * vToCam.y + rel.z * vToCam.z) >= 0.0f;
        };
        // Closest screen distance (px) from the mouse to ring i, plus the angle there.
        auto ringPick = [&](int i, float& bestAng) -> float {
            float best = 1e30f; bestAng = 0.0f;
            ImVec2 prev; bool prevOk = ringPt(i, 0.0f, prev);
            for (int s = 1; s <= RING_SEG; ++s) {
                float a = (float)s / RING_SEG * 2.0f * Mathf::PI;
                ImVec2 cur; bool curOk = ringPt(i, a, cur);
                if (prevOk && curOk) {
                    float d = SegDistPx(io.MousePos, prev, cur);
                    if (d < best) { best = d; bestAng = a - (Mathf::PI / RING_SEG); }
                }
                prev = cur; prevOk = curOk;
            }
            return best;
        };

        // Hover detection for rotate (highlight the ring under the cursor before
        // you grab it). Skipped while already dragging a handle.
        g_gizmoHover = -1;
        if (oOk && tool == Tool::Rotate && hovered && !g_uiHandled && !g_gizmoGrab) {
            float best = 8.0f, a;
            for (int i = 0; i < 4; ++i) { float d = ringPick(i, a); if (d < best) { best = d; g_gizmoHover = i; } }
        }

        if (oOk && tool == Tool::Rotate) {
            ImU32 colA[4] = {col[0], col[1], col[2], IM_COL32(210, 210, 215, 255)};
            // Faint silhouette sphere behind the rings (radius = projected view ring).
            ImVec2 vr; if (ringPt(3, 0.0f, vr)) {
                float sr = Mathf::Sqrt((vr.x - so.x) * (vr.x - so.x) + (vr.y - so.y) * (vr.y - so.y));
                dl->AddCircleFilled(so, sr, IM_COL32(120, 130, 145, 22), 48);
            }
            for (int i = 3; i >= 0; --i) {   // view ring first (behind), axes on top
                bool active = (g_gizmoGrab && g_gizmoAxis == i);
                bool hot    = active || g_gizmoHover == i;
                ImU32 base  = hot ? IM_COL32(255, 230, 90, 255) : colA[i];
                float th    = hot ? 3.5f : 2.5f;
                ImVec2 prev; bool prevOk = ringPt(i, 0.0f, prev);
                bool prevFront = ringFront(i, 0.0f);
                for (int s = 1; s <= RING_SEG; ++s) {
                    float a = (float)s / RING_SEG * 2.0f * Mathf::PI;
                    ImVec2 cur; bool curOk = ringPt(i, a, cur);
                    bool front = ringFront(i, a);
                    if (prevOk && curOk) {
                        // Dim the back-facing half so the ring reads as 3D.
                        ImU32 c = (front && prevFront)
                                  ? base
                                  : (base & 0x00FFFFFF) | (hot ? 0x70000000u : 0x40000000u);
                        dl->AddLine(prev, cur, c, th);
                    }
                    prev = cur; prevOk = curOk; prevFront = front;
                }
            }
            // While dragging: a translucent wedge from the grab angle to the
            // current angle, plus a live degree readout near the pivot.
            if (g_gizmoGrab && g_gizmoAxis >= 0) {
                int i = g_gizmoAxis;
                float sweep = g_rotTotal * Mathf::Deg2Rad;
                int steps = (int)(Mathf::Abs(sweep) / (2.0f * Mathf::PI) * RING_SEG) + 1;
                if (steps > RING_SEG * 3) steps = RING_SEG * 3;
                ImU32 fill = IM_COL32(255, 230, 90, 70);
                ImVec2 a0; bool a0ok = ringPt(i, g_rotStartAng, a0);
                for (int s = 0; s < steps; ++s) {
                    float f0 = (float)s / steps, f1 = (float)(s + 1) / steps;
                    ImVec2 p0, p1;
                    if (ringPt(i, g_rotStartAng + sweep * f0, p0) &&
                        ringPt(i, g_rotStartAng + sweep * f1, p1))
                        dl->AddTriangleFilled(so, p0, p1, fill);
                }
                if (a0ok) dl->AddLine(so, a0, IM_COL32(255, 230, 90, 200), 1.5f);
                ImVec2 aN; if (ringPt(i, g_rotStartAng + sweep, aN))
                    dl->AddLine(so, aN, IM_COL32(255, 230, 90, 200), 1.5f);
                char hud[32]; std::snprintf(hud, sizeof(hud), "%.1f\xc2\xb0", g_rotTotal);
                dl->AddText(ImVec2(so.x + 12, so.y - 24), IM_COL32(255, 255, 255, 255), hud);
            }
            dl->AddCircleFilled(so, 3.0f, IM_COL32(230, 230, 230, 255));
        } else if (oOk) {
            for (int i = 0; i < 3; ++i) {
                if (!tipOk[i]) continue;
                ImU32 c = (g_gizmoGrab && g_gizmoAxis == i) ? IM_COL32(255, 230, 90, 255) : col[i];
                dl->AddLine(so, tip[i], c, 3.0f);
                if (tool == Tool::Scale)
                    dl->AddRectFilled(ImVec2(tip[i].x - 5, tip[i].y - 5),
                                      ImVec2(tip[i].x + 5, tip[i].y + 5), c);   // scale cube
                else
                    dl->AddCircleFilled(tip[i], 5.0f, c);                       // move knob
            }
            dl->AddCircleFilled(so, 3.0f, IM_COL32(230, 230, 230, 255));
        }

        // Grab the closest handle (ring for Rotate, arm for Move/Scale) on press.
        if (hovered && oOk && !g_uiHandled && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            int pick = -1; float pickAng = 0.0f;
            if (tool == Tool::Rotate) {
                float best = 8.0f, a;
                for (int i = 0; i < 4; ++i) { float d = ringPick(i, a); if (d < best) { best = d; pick = i; pickAng = a; } }
            } else {
                float best = 11.0f;
                for (int i = 0; i < 3; ++i)
                    if (tipOk[i]) { float d = SegDistPx(io.MousePos, so, tip[i]); if (d < best) { best = d; pick = i; } }
            }
            if (pick >= 0) { ed.PushUndo(); g_gizmoAxis = pick; g_gizmoGrab = true; grabbedThisClick = true;
                             g_rotAccum = 0.0f; g_rotApplied = 0.0f;
                             g_rotTotal = 0.0f; g_rotStartAng = pickAng; }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { g_gizmoGrab = false; g_gizmoAxis = -1; }

        // Drag the grabbed handle.
        if (g_gizmoGrab && g_gizmoAxis >= 0 && oOk && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            int i = g_gizmoAxis;
            if (tool == Tool::Rotate) {
                // Spin about the grabbed axis (X/Y/Z or the view axis) by dragging
                // tangent to its ring: find the ring point nearest the cursor, take
                // its screen-space tangent, and project the mouse motion onto it.
                Vec3 rax = rotAxisOf(i);
                float a; ringPick(i, a);
                ImVec2 p0, p1;
                if (ringPt(i, a - 0.06f, p0) && ringPt(i, a + 0.06f, p1)) {
                    ImVec2 tg{p1.x - p0.x, p1.y - p0.y};
                    float tl = Mathf::Sqrt(tg.x * tg.x + tg.y * tg.y);
                    if (tl > 1e-3f) {
                        tg.x /= tl; tg.y /= tl;
                        float deg = (io.MouseDelta.x * tg.x + io.MouseDelta.y * tg.y) * 0.5f;
                        if (g_snap && g_rotSnapDeg > 0.0f) {
                            // Detents: apply only whole snapped steps as we cross them.
                            g_rotAccum += deg;
                            float snapped = Mathf::Round(g_rotAccum / g_rotSnapDeg) * g_rotSnapDeg;
                            float step = snapped - g_rotApplied;
                            if (step != 0.0f) { t->Rotate(rax * step); g_rotApplied = snapped; g_rotTotal = snapped; }
                        } else {
                            t->Rotate(rax * deg);
                            g_rotTotal += deg;
                        }
                        ed.dirty = true;
                    }
                }
            } else if (tipOk[i]) {
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
                    } else { // Scale
                        Vec3 sc = t->localScale;
                        if (i == 0) sc.x += amt; else if (i == 1) sc.y += amt; else sc.z += amt;
                        if (g_snap && g_snapSize > 0.0f) {      // snap scale to the increment
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

// ---- Launcher gate ---------------------------------------------------------
// The editor may only be opened through the OkaySpace Launcher, and only while
// that launcher is still running. The launcher passes "--launcher <pid>"; we
// verify the token is present and that process is alive. Double-clicking the
// editor directly (no token) bounces the user to the launcher instead.
static bool ProcessAlive(unsigned long pid) {
    if (pid == 0) return false;
#if defined(_WIN32)
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) return false;
    bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
#else
    return ::kill((pid_t)pid, 0) == 0;
#endif
}

// Returns true if the editor is allowed to run. When not, it tries to open the
// launcher (found next to this exe) and returns false so main() can exit.
static bool PassesLauncherGate(int argc, char** argv) {
    // Development / CI escape hatch: OKAY_DEV=1 or a --dev flag bypasses the gate.
    if (const char* dev = std::getenv("OKAY_DEV"); dev && dev[0] && dev[0] != '0') return true;
    unsigned long launcherPid = 0;
    bool haveToken = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--dev") return true;
        if (a == "--launcher" && i + 1 < argc) { launcherPid = std::strtoul(argv[i + 1], nullptr, 10); haveToken = true; }
        else if (a.rfind("--launcher=", 0) == 0) { launcherPid = std::strtoul(a.c_str() + 11, nullptr, 10); haveToken = true; }
    }
    if (haveToken && ProcessAlive(launcherPid)) return true;

    // Not launched by a live launcher: bounce to the launcher and refuse.
    namespace sfs = std::filesystem;
    std::error_code ec;
    sfs::path dir = sfs::absolute(argv[0], ec).parent_path();
    const char* names[] = {"okayspace-launcher.exe", "OkaySpaceLauncher.exe", "okayspace-launcher"};
    sfs::path launcher;
    for (const char* n : names) { sfs::path p = dir / n; if (sfs::exists(p, ec)) { launcher = p; break; } }
    const char* msg = "OkaySpace Editor must be opened from the OkaySpace Launcher.\n\n"
                      "Starting the launcher now...";
    if (launcher.empty())
        msg = "OkaySpace Editor must be opened from the OkaySpace Launcher,\n"
              "but the launcher was not found next to the editor.";
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "OkaySpace", msg, nullptr);
    if (!launcher.empty()) {
#if defined(_WIN32)
        std::string cmd = "start \"\" \"" + launcher.string() + "\"";
#else
        std::string cmd = "\"" + launcher.string() + "\" >/dev/null 2>&1 &";
#endif
        std::system(cmd.c_str());
    }
    return false;
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--selftest") return RunSelfTest();

    if (!PassesLauncherGate(argc, argv)) return 0;

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
    LoadSettings();

    // Route engine logs (and script print/log/debug output) into the Console
    // with the matching severity (Unity-style info/warning/error).
    Log::sink = [](Log::Level lvl, const std::string& msg) {
        int level = lvl == Log::Level::Error ? 2 : lvl == Log::Level::Warning ? 1 : 0;
        ConsoleLog(msg, level);
    };

    ConsoleLog("Welcome to OkaySpace v" OKAY_ENGINE_VERSION
               ". Choose a 2D or 3D project to begin.");

    // Automatically check for a newer build at startup, in the background so it
    // never blocks the editor. The result is picked up in the main loop and, if
    // an update is available, the Update window pops up on its own.
    g_updateCheck = std::async(std::launch::async, updater::CheckLatest);

    bool running = true;
    std::string lastTitle;
    Uint64 last = SDL_GetPerformanceCounter();
    while (running) {
        // Pick up the background update check when it finishes (once).
        if (!g_autoCheckDone && g_updateCheck.valid() &&
            g_updateCheck.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            g_update = g_updateCheck.get();
            g_autoCheckDone = true;
            if (g_update.available) {
                ConsoleLog("Update available: v" + g_update.latest +
                           " (you have v" + g_update.current + ").");
                if (g_autoUpdate) {
                    // User opted into auto-updates: save their work, then install
                    // and relaunch automatically (no prompt).
                    SaveAllBeforeExit(ed);
                    ConsoleLog("Auto-updating to v" + g_update.latest + "...");
                    g_updateStatus = updater::InstallUpdate(g_update.latest, g_update.ref);
                    ConsoleLog(g_updateStatus);
                } else {
                    g_openUpdatePopup = true;   // surface it for a manual choice
                }
            }
        }
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

        // When the open scene changes, reset the autosave timer and look for a
        // newer "<scene>.autosave" (a recovery copy from a crash/unclean exit).
        {
            static std::string s_watchedScene = "\x01";  // force first check
            if (ed.path() != s_watchedScene) {
                s_watchedScene = ed.path();
                g_lastAutosave = SDL_GetTicks64() / 1000.0;
                g_recoverPath.clear();
                if (!ed.path().empty()) {
                    std::error_code rc;
                    std::string side = ed.path() + ".autosave";
                    if (std::filesystem::exists(side, rc)) {
                        auto ts = std::filesystem::last_write_time(side, rc);
                        auto os = std::filesystem::exists(ed.path(), rc)
                                  ? std::filesystem::last_write_time(ed.path(), rc)
                                  : std::filesystem::file_time_type::min();
                        if (!rc && ts > os) g_recoverPath = side;
                    }
                }
            }
        }

        // Autosave a crash-recovery sidecar ("<scene>.autosave") while editing —
        // never mid-Play, and without touching the real file or the dirty flag.
        if (g_autosave && !ed.isPlaying() && ed.dirty && !ed.path().empty()) {
            double t = SDL_GetTicks64() / 1000.0;
            if (t - g_lastAutosave > (double)g_autosaveInterval) {
                g_lastAutosave = t;
                if (SceneSerializer::SaveToFile(ed.scene(), ed.path() + ".autosave")) {
                    g_autosaveStamp = t;
                    ConsoleLog("Autosaved recovery copy: " + ed.path() + ".autosave");
                }
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

        // While the game is actually running, turn OFF ImGui keyboard navigation:
        // otherwise a toolbar button keeps nav-focus and pressing Space (jump)
        // activates it — which is why Space was pausing the game. Restore it when
        // stopped/paused so editor keyboard nav still works.
        if (ed.isPlaying() && !g_paused)
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        else
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        HandleShortcuts(ed);
        DrawDockSpace(ed);
        DrawNewProjectPopup(ed);
        DrawFileDialogs(ed);
        DrawQuitPrompt(ed, running);
        DrawUpdatePopup(ed);
        DrawAboutPopup();
        DrawRecoveryPopup(ed);
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
        if (g_showSaveManager) DrawSaveManager(ed);
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
