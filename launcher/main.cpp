// OkaySpace Launcher — a small FiveM-style front end with three sections:
//   Create      launch the engine/editor to build a game
//   Play        run a game you've built (any game.okayscene found nearby)
//   Marketplace browse starter templates to open in the editor
//
// It looks for OkayEngine.exe (editor) and OkaySpacePlayer.exe (runtime)
// sitting next to it, and launches them. Built with Dear ImGui + SDL2.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "AppIcon.hpp"
#include "okay/Platform/Account/AccountService.hpp"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "RobotoFont.h"     // embedded Roboto Medium (Apache 2.0) — shared with the editor

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace fs = std::filesystem;

#ifndef OKAY_ENGINE_VERSION
#  define OKAY_ENGINE_VERSION "dev"
#endif

// Optional account server compiled into the build (set at configure time with
// -DOKAY_DEFAULT_ACCOUNT_URL / -DOKAY_DEFAULT_ACCOUNT_KEY, injected from a CI
// secret) so a shipped build is online by default with no key file on disk.
#ifndef OKAY_DEFAULT_ACCOUNT_URL
#  define OKAY_DEFAULT_ACCOUNT_URL ""
#endif
#ifndef OKAY_DEFAULT_ACCOUNT_KEY
#  define OKAY_DEFAULT_ACCOUNT_KEY ""
#endif

namespace {

std::string g_exeDir = ".";
std::string g_selfPath;   // absolute path of the running launcher exe

// Resolve an engine/player exe by name, searching next to the launcher and in the
// Tools/ subfolder (defined fully below; forward-declared for the updater).
std::string FindExe(std::initializer_list<const char*> names);

// Where published builds live: the assets of the latest GitHub Release (this is
// what the release workflow uploads). "releases/latest/download/<name>" always
// redirects to the newest release's asset, so this never serves a stale binary
// the way the old raw dist/ URL did.
const char* kRawBase =
    "https://github.com/kingimann/OkaySpaceGameEngine/releases/latest/download/";

// ---- Auto-updater ----------------------------------------------------------
// On startup the launcher checks the published version and, if newer (or a
// runtime is missing), downloads the engine + player in the background and
// stages a new launcher for the next start. State is written by a worker
// thread and read by the UI. SDL's threading primitives are used because the
// MinGW win32 threads model doesn't provide std::thread / std::mutex.
enum UpState { Up_Idle, Up_Checking, Up_Downloading, Up_UpToDate, Up_Updated, Up_Failed };
SDL_atomic_t g_upState;          // holds a UpState value
SDL_mutex* g_upMutex = nullptr;
std::string g_upMessage = "Checking for updates...";
std::string g_upLatest;
bool g_upRelaunchNeeded = false;
SDL_Thread* g_upThread = nullptr;

void SetState(UpState s) { SDL_AtomicSet(&g_upState, (int)s); }
UpState GetState() { return (UpState)SDL_AtomicGet(&g_upState); }

void SetUpMsg(const std::string& m) {
    if (g_upMutex) SDL_LockMutex(g_upMutex);
    g_upMessage = m;
    if (g_upMutex) SDL_UnlockMutex(g_upMutex);
}
std::string GetUpMsg() {
    std::string m;
    if (g_upMutex) SDL_LockMutex(g_upMutex);
    m = g_upMessage;
    if (g_upMutex) SDL_UnlockMutex(g_upMutex);
    return m;
}

// Defeat the GitHub raw CDN cache (which can serve a stale VERSION.txt or .exe
// for minutes after a release) with a unique query string per request.
std::string BustCache(const std::string& url) {
    static unsigned counter = 0;
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string sep = url.find('?') == std::string::npos ? "?" : "&";
    return url + sep + "okaycb=" + std::to_string(now) + "_" + std::to_string(counter++);
}

// Run a shell command. On Windows, run it WITHOUT flashing a console window
// (CREATE_NO_WINDOW), which is why we don't use std::system there — every
// std::system call would otherwise pop a cmd window. wait=true blocks and
// returns the exit code (curl/powershell); wait=false fires and forgets
// (launching apps, opening URLs).
int RunCmd(const std::string& cmd, bool wait) {
#if defined(_WIN32)
    std::string full = "cmd /c " + cmd;
    std::vector<char> buf(full.begin(), full.end()); buf.push_back('\0');
    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return -1;
    DWORD code = 0;
    if (wait) { WaitForSingleObject(pi.hProcess, INFINITE); GetExitCodeProcess(pi.hProcess, &code); }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return (int)code;
#else
    (void)wait;
    return std::system(cmd.c_str());
#endif
}

// Download a URL to a file using whatever HTTP client is on the system.
bool Download(const std::string& url, const std::string& out) {
    std::error_code ec; fs::remove(out, ec);
    std::string u = BustCache(url);
    const char* nc = "-H \"Cache-Control: no-cache\" -H \"Pragma: no-cache\" ";
#if defined(_WIN32)
    std::string c1 = "curl -L -s -f " + std::string(nc) + "-o \"" + out + "\" \"" + u + "\"";
    if (RunCmd(c1, true) == 0 && fs::exists(out)) return true;
    std::string c2 = "powershell -NoProfile -Command \"try { Invoke-WebRequest "
                     "-Headers @{'Cache-Control'='no-cache'} "
                     "-UseBasicParsing -Uri '" + u + "' -OutFile '" + out +
                     "' } catch { exit 1 }\"";
    return RunCmd(c2, true) == 0 && fs::exists(out);
#else
    std::string c1 = "curl -L -s -f " + std::string(nc) + "-o \"" + out + "\" \"" + u + "\" 2>/dev/null";
    if (RunCmd(c1, true) == 0 && fs::exists(out)) return true;
    std::string c2 = "wget -q --no-cache -O \"" + out + "\" \"" + u + "\" 2>/dev/null";
    return RunCmd(c2, true) == 0 && fs::exists(out);
#endif
}

// Compare dotted versions ("1.5.0"); returns -1 / 0 / 1 for a<b / a==b / a>b.
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

// The installed version: a VERSION.txt beside the launcher (written after each
// upgrade) or, if newer, the version this launcher was compiled with.
std::string LocalVersion() {
    std::error_code ec;
    fs::path vf = fs::path(g_exeDir) / "VERSION.txt";
    if (fs::exists(vf, ec)) {
        std::ifstream f(vf); std::string v; std::getline(f, v);
        while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' '))
            v.pop_back();
        if (!v.empty())
            return CompareVersions(v, OKAY_ENGINE_VERSION) >= 0 ? v : OKAY_ENGINE_VERSION;
    }
    return OKAY_ENGINE_VERSION;
}

// Download <url> to <dest>.new, validate it, then replace <dest>. If the file
// is in use (the running launcher) the old exe is renamed aside first, which
// Windows allows; the swap then takes effect on the next launch.
bool ReplaceFile(const std::string& url, const fs::path& dest, bool inUse) {
    fs::path tmp = dest; tmp += ".new";
    if (!Download(url, tmp.string())) return false;
    std::error_code ec;
    if (!fs::exists(tmp, ec) || fs::file_size(tmp, ec) < 100000) {
        fs::remove(tmp, ec);
        return false;
    }
    if (inUse) {
        fs::path old = dest; old += ".old";
        fs::remove(old, ec);
        fs::rename(dest, old, ec);          // running exe -> .old (allowed on Win)
    } else {
        fs::remove(dest, ec);
    }
    fs::rename(tmp, dest, ec);
    if (ec) return false;
#if !defined(_WIN32)
    fs::permissions(dest, fs::perms::owner_exec | fs::perms::group_exec |
                    fs::perms::others_exec, fs::perm_options::add, ec);
#endif
    return true;
}

// Worker: check the published version and, if newer, install it. Runs on a
// background thread so the launcher stays responsive while ~35 MB downloads.
int RunUpdateCheck(void*) {
    SetState(Up_Checking);
    SetUpMsg("Checking for updates...");
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec);
    fs::path vf = tmp / "okayspace_launcher_ver.txt";
    if (!Download(std::string(kRawBase) + "VERSION.txt", vf.string())) {
        SetUpMsg("Couldn't reach GitHub (offline?).");
        SetState(Up_Failed);
        return 0;
    }
    std::string latest;
    { std::ifstream f(vf); std::getline(f, latest); }
    while (!latest.empty() && (latest.back() == '\r' || latest.back() == '\n' || latest.back() == ' '))
        latest.pop_back();
    fs::remove(vf, ec);
    g_upLatest = latest;

    std::string local = LocalVersion();
    fs::path dir(g_exeDir);
    bool newer = !latest.empty() && CompareVersions(local, latest) < 0;   // installed < published

    // Only ever download a STRICTLY newer published version. Never "self-heal" by
    // pulling a not-newer (older or equal) build — that used to downgrade the engine
    // to whatever stale binary sits in the repo's dist/ folder (the "it downloads
    // 2.97" bug). If we're current or ahead, do nothing.
    if (!newer) {
        SetUpMsg("Up to date (v" + local + ").");
        SetState(Up_UpToDate);
        return 0;
    }

    SetState(Up_Downloading);
    bool ok = true;
    {
        // Write each exe back to wherever it currently lives (e.g. Tools/), so an
        // update preserves the organized layout instead of scattering exes to the
        // top level. Falls back to next-to-launcher if not found (fresh install).
        auto destOf = [&](const char* name) -> fs::path {
            std::string found = FindExe({name});
            return found.empty() ? (dir / name) : fs::path(found);
        };
        SetUpMsg("Downloading engine v" + latest + "...");
        ok = ReplaceFile(std::string(kRawBase) + "OkayEngine.exe",
                         destOf("OkayEngine.exe"), false) && ok;
        SetUpMsg("Downloading player runtime v" + latest + "...");
        ok = ReplaceFile(std::string(kRawBase) + "OkaySpacePlayer.exe",
                         destOf("OkaySpacePlayer.exe"), false) && ok;
    }
    // Only self-update the launcher on a genuine version bump.
    bool launcherOk = false;
    if (newer) {
        SetUpMsg("Updating launcher...");
        launcherOk = !g_selfPath.empty() &&
            ReplaceFile(std::string(kRawBase) + "OkaySpace.exe",
                        fs::path(g_selfPath), true);
    }

    if (ok) {
        std::ofstream(dir / "VERSION.txt") << latest << "\n";
        g_upRelaunchNeeded = launcherOk;
        SetUpMsg(launcherOk
            ? "Updated to v" + latest + " — restart the launcher to finish."
            : (newer ? "Updated engine & player to v" + latest + "."
                     : "Downloaded the missing runtime (v" + local + ")."));
        SetState(Up_Updated);
    } else {
        SetUpMsg("Download failed — check your internet connection.");
        SetState(Up_Failed);
    }
    return 0;
}

// Kick off an update check on a background thread (no-op if one is running).
void StartUpdateCheck() {
    UpState s = GetState();
    if (s == Up_Checking || s == Up_Downloading) return;
    if (g_upThread) { SDL_WaitThread(g_upThread, nullptr); g_upThread = nullptr; }
    g_upThread = SDL_CreateThread(RunUpdateCheck, "okay-update", nullptr);
}

// Find the first of `names` that exists next to the launcher.
std::string FindExe(std::initializer_list<const char*> names) {
    std::error_code ec;
    // Look next to the launcher and in the "Tools" subfolder (the canonical
    // organized layout — see docs/packaging.md); "Engine"/"runtime"/"bin" are
    // also accepted for older / alternative layouts.
    const char* dirs[] = {"", "Tools", "Engine", "runtime", "bin"};
    for (const char* d : dirs)
        for (const char* n : names) {
            fs::path p = d[0] ? (fs::path(g_exeDir) / d / n) : (fs::path(g_exeDir) / n);
            if (fs::exists(p, ec)) return p.string();
        }
    return {};
}

// Launch an executable (detached), optionally with one argument.
void Launch(const std::string& exe, const std::string& arg = "") {
    if (exe.empty()) return;
#if defined(_WIN32)
    std::string cmd = "start \"\" \"" + exe + "\"";
    if (!arg.empty()) cmd += " \"" + arg + "\"";
#else
    std::string cmd = "\"" + exe + "\"";
    if (!arg.empty()) cmd += " \"" + arg + "\"";
    cmd += " >/dev/null 2>&1 &";
#endif
    RunCmd(cmd, false);
}

// This launcher's process id (handed to the editor so it can verify the launcher
// is still running).
unsigned long CurrentPid() {
#if defined(_WIN32)
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)getpid();
#endif
}

// Launch the editor with a handshake token: "--launcher <pid>". The editor refuses
// to start unless this token is present AND that launcher process is still alive,
// so the engine can only be opened through (and alongside) the launcher.
void LaunchEditor(const std::string& exe, const std::string& tmpl = "") {
    if (exe.empty()) return;
    std::string args = "--launcher " + std::to_string(CurrentPid());
    if (!tmpl.empty()) args += " --template \"" + tmpl + "\"";
#if defined(_WIN32)
    std::string cmd = "start \"\" \"" + exe + "\" " + args;
#else
    std::string cmd = "\"" + exe + "\" " + args + " >/dev/null 2>&1 &";
#endif
    RunCmd(cmd, false);
}

// Open a URL in the default browser, or a folder in the file manager.
void OpenExternal(const std::string& target) {
    if (target.empty()) return;
#if defined(_WIN32)
    std::string cmd = "start \"\" \"" + target + "\"";
#elif defined(__APPLE__)
    std::string cmd = "open \"" + target + "\" >/dev/null 2>&1 &";
#else
    std::string cmd = "xdg-open \"" + target + "\" >/dev/null 2>&1 &";
#endif
    RunCmd(cmd, false);
}

// Scan a few likely folders for built games (*.okayscene).
std::vector<fs::path> FindScenes() {
    std::vector<fs::path> out;
    std::error_code ec;
    auto scan = [&](const fs::path& dir) {
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;
        for (auto it = fs::recursive_directory_iterator(dir, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (it->is_regular_file(ec) && it->path().extension() == ".okayscene")
                out.push_back(it->path());
        }
    };
    fs::path base(g_exeDir);
    scan(base);
    scan(base / "games");
    scan(base / "Projects");
    scan(base / "community");     // installed/shared community content
    return out;
}

// The local library where downloaded/shared community content lands. Games dropped
// here (a folder or a loose .okayscene) show up in Play and the Community tab.
fs::path CommunityDir() {
    fs::path d = fs::path(g_exeDir) / "community";
    std::error_code ec; fs::create_directories(d, ec);
    return d;
}

// Copy a shared item (a game folder, or a loose .okayscene / script / model file)
// into the community library. Returns "" on failure, else the installed path.
std::string InstallToCommunity(const std::string& src) {
    namespace fsy = std::filesystem;
    std::error_code ec;
    fsy::path s(src);
    if (src.empty() || !fsy::exists(s, ec)) return {};
    fsy::path dest = CommunityDir() / s.filename();
    if (fsy::is_directory(s, ec)) {
        for (int n = 2; fsy::exists(dest, ec); ++n) dest = CommunityDir() / (s.filename().string() + " " + std::to_string(n));
        fsy::copy(s, dest, fsy::copy_options::recursive | fsy::copy_options::overwrite_existing, ec);
    } else {
        // Loose file: give it its own subfolder so it reads as a self-contained item.
        fsy::path sub = CommunityDir() / s.stem();
        for (int n = 2; fsy::exists(sub, ec); ++n) sub = CommunityDir() / (s.stem().string() + " " + std::to_string(n));
        fsy::create_directories(sub, ec);
        dest = sub / s.filename();
        fsy::copy_file(s, dest, fsy::copy_options::overwrite_existing, ec);
    }
    return ec ? std::string{} : dest.string();
}

// The community sub-folder a given scene path belongs to (the item to reveal/remove).
// Empty if the path isn't under the community library.
fs::path CommunityItemRoot(const fs::path& scenePath) {
    std::error_code ec;
    fs::path comm = fs::weakly_canonical(CommunityDir(), ec);
    fs::path p = fs::weakly_canonical(scenePath, ec);
    fs::path acc;
    for (auto it = p.begin(); it != p.end(); ++it) {
        acc /= *it;
        if (fs::weakly_canonical(acc, ec) == comm) {
            ++it;
            if (it != p.end()) return acc / *it;   // community/<item>
            break;
        }
    }
    return {};
}

// Shared accent so UI code can match the theme.
// Mutable so the Appearance setting can recolor the UI live (DarkTheme() reads
// these, so changing them and re-applying restyles everything).
ImVec4 kAccent(0.26f, 0.59f, 0.98f, 1.0f);     // matches the editor's default accent
ImVec4 kAccentDim(0.19f, 0.43f, 0.72f, 1.0f);

// Selectable accent colors (name, main, dim) — mirror the editor's accent presets.
struct AccentPreset { const char* name; ImVec4 col; ImVec4 dim; };
const AccentPreset kAccentPresets[] = {
    {"Blue",   ImVec4(0.26f, 0.59f, 0.98f, 1), ImVec4(0.19f, 0.43f, 0.72f, 1)},
    {"Teal",   ImVec4(0.18f, 0.78f, 0.74f, 1), ImVec4(0.13f, 0.56f, 0.53f, 1)},
    {"Violet", ImVec4(0.56f, 0.46f, 0.96f, 1), ImVec4(0.40f, 0.33f, 0.70f, 1)},
    {"Green",  ImVec4(0.36f, 0.80f, 0.46f, 1), ImVec4(0.26f, 0.58f, 0.33f, 1)},
    {"Amber",  ImVec4(0.95f, 0.66f, 0.26f, 1), ImVec4(0.70f, 0.48f, 0.19f, 1)},
    {"Rose",   ImVec4(0.96f, 0.42f, 0.56f, 1), ImVec4(0.70f, 0.30f, 0.41f, 1)},
};
int g_accentIndex = 0;            // current accent preset (persisted)
bool g_updateOnLaunch = false;    // check for updates at startup (persisted)
int g_themeIndex = 0;             // 0 Dark, 1 Midnight, 2 Light (persisted)
float g_uiScale = 1.0f;           // font/UI scale for high-DPI (persisted)
int g_winW = 1040, g_winH = 680;  // last window size (persisted)
const char* kThemeNames[] = {"Dark", "Midnight", "Light"};

// Transient toast notification (bottom-right of the window).
// kind: 0 = info (accent), 1 = success (green), 2 = error (red).
std::string g_toastMsg;
Uint32 g_toastUntil = 0;
int g_toastKind = 0;
void Toast(const std::string& m, int kind = 0) {
    g_toastMsg = m; g_toastKind = kind;
    g_toastUntil = SDL_GetTicks() + 2600;
}
std::vector<std::string> g_favorites;   // favorited game paths (persisted)
std::vector<std::string> g_recent;       // recently played, most-recent first (persisted)
int g_playSort = 0;               // 0 Favorites first, 1 Name A–Z, 2 Recently played
int g_lastTab = 0;                // section shown when the launcher was last closed

bool IsFavorite(const std::string& p) {
    return std::find(g_favorites.begin(), g_favorites.end(), p) != g_favorites.end();
}
void ToggleFavorite(const std::string& p) {
    auto it = std::find(g_favorites.begin(), g_favorites.end(), p);
    if (it != g_favorites.end()) g_favorites.erase(it);
    else g_favorites.push_back(p);
}
// Index in the recent list (0 = most recent), or a large number if not present.
std::size_t RecentRank(const std::string& p) {
    auto it = std::find(g_recent.begin(), g_recent.end(), p);
    return it == g_recent.end() ? (std::size_t)1e9 : (std::size_t)(it - g_recent.begin());
}
void RecordPlayed(const std::string& p) {
    auto it = std::find(g_recent.begin(), g_recent.end(), p);
    if (it != g_recent.end()) g_recent.erase(it);
    g_recent.insert(g_recent.begin(), p);
    if (g_recent.size() > 16) g_recent.resize(16);
}

// Apply the selected theme (g_themeIndex), accent (kAccent), and UI scale.
void DarkTheme() {
    const bool light = (g_themeIndex == 2);
    if (light) ImGui::StyleColorsLight(); else ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    // Metrics match the game engine editor's theme (ApplyTheme) so the launcher and
    // editor read as one product: soft unified rounding, thin frame borders.
    s.WindowRounding    = 6.0f;  s.ChildRounding    = 6.0f;
    s.FrameRounding     = 5.0f;  s.GrabRounding     = 4.0f;
    s.PopupRounding     = 6.0f;  s.TabRounding      = 6.0f;
    s.ScrollbarRounding = 6.0f;
    s.WindowPadding   = ImVec2(14, 12); s.FramePadding = ImVec2(10, 7);
    s.ItemSpacing     = ImVec2(9, 8);   s.ItemInnerSpacing = ImVec2(7, 5);
    s.WindowBorderSize = 0.0f;          s.ChildBorderSize = 1.0f;
    s.FrameBorderSize  = 1.0f;          s.PopupBorderSize = 1.0f;
    s.ScrollbarSize   = 11.0f;          s.GrabMinSize = 11.0f;
    s.SeparatorTextBorderSize = 1.0f;
    ImVec4* c = s.Colors;
    const ImVec4 accent = kAccent;
    const ImVec4 accentDim = kAccentDim;
    const bool darkGrabs = !light;
    if (g_themeIndex == 1) {                              // Midnight (deep, bluer)
        c[ImGuiCol_WindowBg]  = ImVec4(0.035f, 0.040f, 0.060f, 1.0f);
        c[ImGuiCol_ChildBg]   = ImVec4(0.070f, 0.080f, 0.110f, 1.0f);
        c[ImGuiCol_PopupBg]   = ImVec4(0.070f, 0.080f, 0.110f, 0.98f);
        c[ImGuiCol_Border]    = ImVec4(1, 1, 1, 0.05f);
        c[ImGuiCol_Text]      = ImVec4(0.90f, 0.93f, 0.98f, 1.0f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.46f, 0.50f, 0.60f, 1.0f);
        c[ImGuiCol_Button]    = ImVec4(0.12f, 0.14f, 0.20f, 1.0f);
        c[ImGuiCol_FrameBg]   = ImVec4(0.10f, 0.12f, 0.17f, 1.0f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.16f, 0.23f, 1.0f);
        c[ImGuiCol_FrameBgActive]  = ImVec4(0.16f, 0.19f, 0.27f, 1.0f);
        c[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.06f);
    } else if (light) {                                   // Light
        c[ImGuiCol_WindowBg]  = ImVec4(0.93f, 0.94f, 0.96f, 1.0f);
        c[ImGuiCol_ChildBg]   = ImVec4(0.99f, 0.99f, 1.00f, 1.0f);
        c[ImGuiCol_PopupBg]   = ImVec4(0.99f, 0.99f, 1.00f, 0.98f);
        c[ImGuiCol_Border]    = ImVec4(0, 0, 0, 0.10f);
        c[ImGuiCol_Text]      = ImVec4(0.10f, 0.12f, 0.16f, 1.0f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.48f, 0.55f, 1.0f);
        c[ImGuiCol_Button]    = ImVec4(0.87f, 0.89f, 0.93f, 1.0f);
        c[ImGuiCol_FrameBg]   = ImVec4(0.90f, 0.92f, 0.96f, 1.0f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.85f, 0.88f, 0.93f, 1.0f);
        c[ImGuiCol_FrameBgActive]  = ImVec4(0.82f, 0.86f, 0.92f, 1.0f);
        c[ImGuiCol_Separator] = ImVec4(0, 0, 0, 0.10f);
    } else {                                              // Dark (default) = engine editor theme
        // Neutral medium-gray base (Unity Pro dark), identical to the editor's
        // ApplyTheme, so the launcher and editor look like one product.
        auto lighten = [](float v) { return v + (1.0f - v) * 0.30f; };
        const ImVec4 accentHover = ImVec4(lighten(accent.x), lighten(accent.y), lighten(accent.z), 1.00f);
        auto tint = [&](float gray, float amt, float a) {
            return ImVec4(gray + (accent.x - gray) * amt, gray + (accent.y - gray) * amt,
                          gray + (accent.z - gray) * amt, a);
        };
        c[ImGuiCol_WindowBg]         = ImVec4(0.180f, 0.180f, 0.192f, 1.00f);
        c[ImGuiCol_ChildBg]          = ImVec4(0.160f, 0.160f, 0.170f, 0.00f);
        c[ImGuiCol_PopupBg]          = ImVec4(0.145f, 0.145f, 0.155f, 0.99f);
        c[ImGuiCol_Border]           = ImVec4(0.00f, 0.00f, 0.00f, 0.45f);
        c[ImGuiCol_FrameBg]          = ImVec4(0.130f, 0.130f, 0.140f, 1.00f);
        c[ImGuiCol_FrameBgHovered]   = tint(0.185f, 0.18f, 1.00f);
        c[ImGuiCol_FrameBgActive]    = tint(0.225f, 0.25f, 1.00f);
        c[ImGuiCol_TitleBg]          = ImVec4(0.130f, 0.130f, 0.140f, 1.00f);
        c[ImGuiCol_TitleBgActive]    = tint(0.165f, 0.16f, 1.00f);
        c[ImGuiCol_MenuBarBg]        = ImVec4(0.155f, 0.155f, 0.165f, 1.00f);
        c[ImGuiCol_Header]           = ImVec4(accent.x, accent.y, accent.z, 0.42f);
        c[ImGuiCol_HeaderHovered]    = ImVec4(accent.x, accent.y, accent.z, 0.55f);
        c[ImGuiCol_HeaderActive]     = ImVec4(accent.x, accent.y, accent.z, 0.70f);
        c[ImGuiCol_Button]           = ImVec4(0.255f, 0.255f, 0.275f, 1.00f);
        c[ImGuiCol_ButtonHovered]    = tint(0.320f, 0.22f, 1.00f);
        c[ImGuiCol_ButtonActive]     = ImVec4(accent.x, accent.y, accent.z, 0.85f);
        c[ImGuiCol_CheckMark]        = accentHover;
        c[ImGuiCol_SliderGrab]       = accent;
        c[ImGuiCol_SliderGrabActive] = accentHover;
        c[ImGuiCol_Separator]        = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
        c[ImGuiCol_SeparatorHovered] = accentDim;
        c[ImGuiCol_SeparatorActive]  = accent;
        c[ImGuiCol_Tab]              = ImVec4(0.150f, 0.150f, 0.160f, 1.00f);
        c[ImGuiCol_TabHovered]       = ImVec4(accent.x, accent.y, accent.z, 0.55f);
        c[ImGuiCol_TabActive]        = tint(0.245f, 0.14f, 1.00f);
        c[ImGuiCol_TabUnfocused]     = ImVec4(0.135f, 0.135f, 0.145f, 1.00f);
        c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.190f, 0.190f, 0.205f, 1.00f);
        c[ImGuiCol_Text]             = ImVec4(0.860f, 0.865f, 0.880f, 1.00f);
        c[ImGuiCol_TextDisabled]     = ImVec4(0.480f, 0.485f, 0.510f, 1.00f);
        c[ImGuiCol_TextSelectedBg]   = ImVec4(accent.x, accent.y, accent.z, 0.45f);
        c[ImGuiCol_ScrollbarBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.20f);
        c[ImGuiCol_ScrollbarGrab]    = ImVec4(0.330f, 0.330f, 0.355f, 1.00f);
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.400f, 0.400f, 0.430f, 1.00f);
        c[ImGuiCol_ScrollbarGrabActive]  = accent;
        c[ImGuiCol_TableHeaderBg]    = ImVec4(0.205f, 0.205f, 0.220f, 1.00f);
        c[ImGuiCol_TableRowBgAlt]    = ImVec4(1.00f, 1.00f, 1.00f, 0.022f);
        ImGui::GetIO().FontGlobalScale = g_uiScale;
        return;   // Dark theme is fully specified above (no shared overrides needed)
    }
    // Accent-tinted slots (shared by the Midnight / Light themes).
    c[ImGuiCol_ButtonHovered]    = accent;
    c[ImGuiCol_ButtonActive]     = accentDim;
    c[ImGuiCol_Header]           = ImVec4(accent.x, accent.y, accent.z, 0.28f);
    c[ImGuiCol_HeaderHovered]    = ImVec4(accent.x, accent.y, accent.z, 0.45f);
    c[ImGuiCol_HeaderActive]     = ImVec4(accent.x, accent.y, accent.z, 0.65f);
    c[ImGuiCol_SeparatorHovered] = accent;
    c[ImGuiCol_SeparatorActive]  = accent;
    c[ImGuiCol_CheckMark]        = accent;
    c[ImGuiCol_SliderGrab]       = accent;
    c[ImGuiCol_SliderGrabActive] = accentDim;
    c[ImGuiCol_ScrollbarBg]      = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]    = ImVec4(darkGrabs ? 1.f : 0.f, darkGrabs ? 1.f : 0.f, darkGrabs ? 1.f : 0.f, 0.16f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(darkGrabs ? 1.f : 0.f, darkGrabs ? 1.f : 0.f, darkGrabs ? 1.f : 0.f, 0.28f);
    c[ImGuiCol_ScrollbarGrabActive]  = accent;
    ImGui::GetIO().FontGlobalScale = g_uiScale;
}

// Apply an accent preset and restyle the whole UI live.
void ApplyAccent(int idx) {
    int n = (int)(sizeof(kAccentPresets) / sizeof(kAccentPresets[0]));
    if (idx < 0 || idx >= n) idx = 0;
    g_accentIndex = idx;
    kAccent    = kAccentPresets[idx].col;
    kAccentDim = kAccentPresets[idx].dim;
    DarkTheme();          // re-apply the palette with the new accent
}

// ---- Shared look-and-feel helpers ------------------------------------------

// Stable pastel color hashed from a name — every game/template gets its own
// hue so lists read as a gallery instead of a wall of identical rows.
ImVec4 NameColor(const std::string& s) {
    unsigned h = 2166136261u;
    for (char ch : s) h = (h ^ (unsigned char)ch) * 16777619u;
    float hue = (float)(h % 360u) / 360.0f;
    const float v = 0.80f, sat = 0.52f;
    float i = std::floor(hue * 6.0f), f = hue * 6.0f - i;
    float p = v * (1.0f - sat), q = v * (1.0f - sat * f), t = v * (1.0f - sat * (1.0f - f));
    float r, g, b;
    switch ((int)i % 6) {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    return ImVec4(r, g, b, 1.0f);
}

// Rounded square tile showing the item's initial — a lightweight "thumbnail"
// that needs no image assets. Advances the layout like a normal item.
void IconTile(const std::string& name, float size) {
    ImVec4 col = NameColor(name);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size),
                      ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.22f)), size * 0.22f);
    dl->AddRect(p, ImVec2(p.x + size, p.y + size),
                ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.80f)), size * 0.22f, 0, 1.5f);
    char init[2] = { name.empty() ? '?' : (char)std::toupper((unsigned char)name[0]), 0 };
    ImVec2 ts = ImGui::CalcTextSize(init);
    dl->AddText(ImVec2(p.x + (size - ts.x) * 0.5f, p.y + (size - ts.y) * 0.5f),
                ImGui::GetColorU32(ImVec4(0.96f, 0.97f, 1.0f, 1.0f)), init);
    ImGui::Dummy(ImVec2(size, size));
}

// Accent-filled call-to-action button — the launcher's primary action style
// (Open Editor, Play). Secondary actions keep the default gray Button.
bool PrimaryButton(const char* label, const ImVec2& size) {
    auto lift = [](float x) { return x + (1.0f - x) * 0.20f; };
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImVec4(lift(kAccent.x), lift(kAccent.y), lift(kAccent.z), 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentDim);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    bool hit = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return hit;
}

// Human "modified N ago" text for a file ("" if the time can't be read).
std::string ModifiedAgo(const fs::path& p) {
    std::error_code ec;
    auto wt = fs::last_write_time(p, ec);
    if (ec) return {};
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(
        fs::file_time_type::clock::now() - wt).count();
    if (secs < 0) secs = 0;
    if (secs < 90) return "just now";
    if (secs < 90 * 60) return std::to_string(secs / 60) + " min ago";
    if (secs < 36 * 3600) return std::to_string(secs / 3600) + " h ago";
    return std::to_string(secs / 86400) + " d ago";
}

// Accent ring around the current child window when the mouse is over it
// (hover feedback for card-style rows and tiles).
void HoverRing() {
    if (!ImGui::IsWindowHovered()) return;
    ImVec2 mn = ImGui::GetWindowPos();
    ImVec2 mx(mn.x + ImGui::GetWindowSize().x, mn.y + ImGui::GetWindowSize().y);
    ImGui::GetWindowDrawList()->AddRect(mn, mx, ImGui::GetColorU32(kAccent), 7.0f, 0, 2.0f);
}

// Launcher preferences persisted next to the exe (launcher.cfg).
void LoadPrefs() {
    std::ifstream f(fs::path(g_exeDir) / "launcher.cfg");
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        while (!v.empty() && (v.back() == '\r' || v.back() == '\n' || v.back() == ' ')) v.pop_back();
        if (k == "accent") { try { g_accentIndex = std::stoi(v); } catch (...) {} }
        else if (k == "theme") { try { g_themeIndex = std::stoi(v); } catch (...) {} }
        else if (k == "ui_scale") { try { g_uiScale = std::stof(v); } catch (...) {} }
        else if (k == "update_on_launch") g_updateOnLaunch = (v == "1");
        else if (k == "play_sort") { try { g_playSort = std::stoi(v); } catch (...) {} }
        else if (k == "tab") { try { g_lastTab = std::stoi(v); } catch (...) {} }
        else if (k == "fav" && !v.empty()) g_favorites.push_back(v);
        else if (k == "recent" && !v.empty()) g_recent.push_back(v);
        else if (k == "win_w") { try { g_winW = std::stoi(v); } catch (...) {} }
        else if (k == "win_h") { try { g_winH = std::stoi(v); } catch (...) {} }
    }
    if (g_winW < 700)  g_winW = 700;
    if (g_winH < 500)  g_winH = 500;
    if (g_themeIndex < 0 || g_themeIndex > 2) g_themeIndex = 0;
    if (g_uiScale < 0.8f) g_uiScale = 0.8f;
    if (g_uiScale > 1.6f) g_uiScale = 1.6f;
}
void SavePrefs() {
    std::ofstream f(fs::path(g_exeDir) / "launcher.cfg", std::ios::trunc);
    f << "accent=" << g_accentIndex << "\n";
    f << "theme=" << g_themeIndex << "\n";
    f << "ui_scale=" << g_uiScale << "\n";
    f << "update_on_launch=" << (g_updateOnLaunch ? 1 : 0) << "\n";
    f << "play_sort=" << g_playSort << "\n";
    f << "tab=" << g_lastTab << "\n";
    f << "win_w=" << g_winW << "\n";
    f << "win_h=" << g_winH << "\n";
    for (const auto& p : g_favorites) f << "fav=" << p << "\n";
    for (const auto& p : g_recent)    f << "recent=" << p << "\n";
}

} // namespace

int main(int argc, char** argv) {
    SDL_SetMainReady();
    if (argc > 0) {
        std::error_code ec;
        fs::path self = fs::absolute(argv[0], ec);
        if (!ec) { g_exeDir = self.parent_path().string(); g_selfPath = self.string(); }
    }
    // Clean up a launcher we replaced on a previous run.
    if (!g_selfPath.empty()) {
        std::error_code ec; fs::remove(fs::path(g_selfPath + ".old"), ec);
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;

    // Show the bundled version and DON'T phone home on startup: the published build
    // on GitHub can lag this one, so an auto-check used to download an OLDER engine
    // and mislead with "up to date". Updates are now opt-in via the button below.
    SDL_AtomicSet(&g_upState, (int)Up_UpToDate);
    g_upMutex = SDL_CreateMutex();
    SetUpMsg("OkaySpace v" + LocalVersion());

    LoadPrefs();                 // preferences (incl. last window size) from launcher.cfg
    SDL_Window* window = SDL_CreateWindow("OkaySpace Launcher  v" OKAY_ENGINE_VERSION,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, g_winW, g_winH,
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    if (!window) return 1;
    SDL_SetWindowMinimumSize(window, 720, 480);   // the fixed layout needs this much
    okay::SetAppIcon(window);   // placeholder OkaySpace logo
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // the launcher has a fixed layout
    ImGui::GetIO().ConfigDebugHighlightIdConflicts = false; // hide dev-only ID warnings
    // Same embedded Roboto Medium font as the editor, so the launcher and editor
    // read as one product (crisp, modern text instead of the tiny bitmap default).
    {
        ImFontConfig fc;
        fc.OversampleH = 2; fc.OversampleV = 2; fc.PixelSnapH = true;
        if (!ImGui::GetIO().Fonts->AddFontFromMemoryCompressedBase85TTF(
                RobotoMedium_compressed_data_base85, 17.0f, &fc))
            ImGui::GetIO().Fonts->AddFontDefault();
    }
    ApplyAccent(g_accentIndex);  // applies the saved accent/theme (calls DarkTheme)
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    std::string editor = FindExe({"OkayEngine.exe", "OkaySpaceEngine.exe", "okay-editor.exe", "okay-editor"});
    std::string player = FindExe({"OkaySpacePlayer.exe", "okay-player.exe", "okay-player"});
    std::vector<fs::path> scenes = FindScenes();
    bool rescannedAfterUpdate = false;

    // `tmpl` is the editor's New Project template title (passed via --template).
    struct Template { const char* name; const char* desc; const char* tmpl; };
    const Template templates[] = {
        {"Platformer",     "Side-scrolling jump-and-run starter.",  "Platformer"},
        {"Top-Down",       "Top-down movement and rooms.",          "Top-Down"},
        {"Coin Collector", "A complete pickup-the-coins game.",     "Coin Collector"},
        {"Main Menu (UI)", "A title screen with buttons.",          "Main Menu"},
        {"Snake",          "The classic, fully playable.",          "Snake"},
        {"First Person",   "FPS character: mouse-look, WASD, jump.", "First Person"},
        {"Third Person",   "Orbit-camera character controller.",    "Third Person"},
        {"Inventory",      "Drag & drop item grid.",                "Inventory"},
        {"Multiplayer",    "Host / join networked starter.",        "Multiplayer"},
    };

    // ---- Account ----
    // Online when an auth server URL is configured; with an API key too it's a
    // managed backend (Supabase). Config comes from env vars or files next to
    // the launcher (account_server.txt / account_apikey.txt). With nothing
    // configured, a local on-device account works for development.
    namespace acct = okay::account;
    auto trimEol = [](std::string& s) {
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' '))
            s.pop_back();
    };
    auto fromEnvOrFile = [&](const char* env, const char* file) {
        std::string v;
        if (const char* e = std::getenv(env)) v = e;
        if (v.empty()) {
            std::ifstream f(fs::path(g_exeDir) / file);
            if (f) std::getline(f, v);
        }
        trimEol(v);
        return v;
    };
    std::string serverUrl = fromEnvOrFile("OKAY_ACCOUNT_SERVER", "account_server.txt");
    std::string apiKey    = fromEnvOrFile("OKAY_ACCOUNT_API_KEY", "account_apikey.txt");
    // Fall back to values baked into the build (no config file needed).
    if (serverUrl.empty()) serverUrl = OKAY_DEFAULT_ACCOUNT_URL;
    if (apiKey.empty())    apiKey    = OKAY_DEFAULT_ACCOUNT_KEY;
    // Held by pointer so the Settings tab can rebuild it live when the server
    // config changes.
    fs::path acctCfgDir = acct::DefaultConfigDir(fs::path(g_exeDir));
    auto accountPtr = std::make_unique<acct::AccountService>(acctCfgDir, serverUrl, apiKey);
    // If we resumed a saved session against an online server, make sure it's
    // still valid (the token may have been revoked/expired); this signs the
    // player out if so. Offline or local accounts are left as-is.
    accountPtr->VerifySession();

    // Editable copies of the account-server settings, shown in the Settings tab.
    // The URL is prefilled (it isn't sensitive); the key field is left blank and
    // masked so an existing or compiled-in key is never displayed. activeKey
    // holds the key currently in use (from env/file/built-in) without showing it.
    char setUrl[256]; std::snprintf(setUrl, sizeof(setUrl), "%s", serverUrl.c_str());
    char setKey[256] = {0};
    std::string activeKey = apiKey;
    std::string setStatus;
    // Apply the Settings form and rebuild the service live. clear=true switches
    // to local accounts. The API key is written to disk ONLY when the user types
    // a new one — an empty key field keeps the current key, so a built-in key is
    // never persisted to a plaintext file. The URL isn't secret, so it's saved.
    auto applyAccountSettings = [&](bool clear) {
        std::error_code ec;
        std::string url, typed = setKey; trimEol(typed);
        if (!clear) { url = setUrl; trimEol(url); }
        std::string key = clear ? std::string{} : (typed.empty() ? activeKey : typed);

        std::ofstream(fs::path(g_exeDir) / "account_server.txt", std::ios::trunc) << url << "\n";
        if (clear)
            fs::remove(fs::path(g_exeDir) / "account_apikey.txt", ec);   // back to local/built-in
        else if (!typed.empty())
            std::ofstream(fs::path(g_exeDir) / "account_apikey.txt", std::ios::trunc) << typed << "\n";
        // else: empty key field -> leave any existing key file untouched and
        // never write a built-in key to disk.

        activeKey = key;
        setKey[0] = '\0';                       // never retain the typed key in the box
        if (clear) setUrl[0] = '\0';
        accountPtr = std::make_unique<acct::AccountService>(acctCfgDir, url, key);
        accountPtr->VerifySession();
        setStatus = accountPtr->IsOnline()
            ? std::string("Saved. Server: ") + accountPtr->ServerUrl() + " (" +
              accountPtr->ProviderName() + ")."
            : "Saved. Using local dev accounts (no server set).";
    };

    char acctUser[64] = {0};
    char acctPass[64] = {0};
    char acctName[64] = {0};             // username (display name) on register
    char acctNewPass[64] = {0};          // change-password field (signed in)
    char acctChgName[64] = {0};          // change-username field (signed in)
    char acctChgEmail[64] = {0};         // change-email field (signed in)
    bool acctRegisterMode = false;       // false = sign in, true = create account
    std::string acctMessage;             // last error/status, shown under the form
    bool acctMessageError = true;
    bool acctBusy = false;

    char playFilter[128] = {0};   // Play-tab search box
    char marketFilter[128] = {0}; // Marketplace search box
    char commFilter[128] = {0};   // Community library search box
    // Reopen on the section the launcher was last closed on.
    int tab = (g_lastTab >= 0 && g_lastTab < 5) ? g_lastTab : 0;
    bool focusSearch = false;     // Ctrl+F jumps to the current tab's search box
    if (g_updateOnLaunch) StartUpdateCheck();   // opt-in auto-check at startup
    bool running = true;
    while (running) {
        // Current account service for this frame. Held by reference so existing
        // code reads naturally; the Settings tab may rebuild accountPtr (which
        // is why the Settings tab uses accountPtr-> directly, not this alias).
        acct::AccountService& account = *accountPtr;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) running = false;
            // Drag & drop onto the launcher: a .okayscene plays instantly (no
            // download); a shared game FOLDER (or loose script/model) installs into
            // your community library.
            else if (e.type == SDL_DROPFILE && e.drop.file) {
                std::string dropped = e.drop.file;
                SDL_free(e.drop.file);
                std::string ext;
                auto dot = dropped.rfind('.');
                if (dot != std::string::npos) for (char ch : dropped.substr(dot)) ext += (char)std::tolower((unsigned char)ch);
                std::error_code dec;
                bool isDir = fs::is_directory(dropped, dec);
                if (ext == ".okayscene" && !isDir && !player.empty()) {
                    Launch(player, dropped);
                    RecordPlayed(dropped); SavePrefs();
                    Toast("Playing dropped game");
                    tab = 1;
                } else if (isDir || ext == ".okay" || ext == ".okayvs" || ext == ".obj" ||
                           ext == ".png" || ext == ".jpg" || ext == ".okayscene") {
                    std::string out = InstallToCommunity(dropped);
                    if (!out.empty()) { scenes = FindScenes(); Toast("Added to your community library", 1); tab = 2; }
                    else Toast("Couldn't install that item", 2);
                } else {
                    Toast("Drop a game folder or .okayscene to add or play it");
                }
            }
        }
        // Once a background download finishes, re-detect the runtimes so the
        // "not found" notices clear without needing a restart.
        if (GetState() == Up_Updated && !rescannedAfterUpdate) {
            editor = FindExe({"OkayEngine.exe", "OkaySpaceEngine.exe", "okay-editor.exe", "okay-editor"});
            player = FindExe({"OkaySpacePlayer.exe", "okay-player.exe", "okay-player"});
            scenes = FindScenes();
            rescannedAfterUpdate = true;
        }
        // Toast once when an update check finishes.
        {
            static int lastUp = -1;
            int us = (int)GetState();
            if (us != lastUp) {
                if (us == (int)Up_Updated) Toast("Update installed — restart to finish", 1);
                else if (us == (int)Up_Failed) Toast("Update check failed", 2);
                else if (us == (int)Up_UpToDate && lastUp == (int)Up_Checking) Toast("You're up to date", 1);
                lastUp = us;
            }
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Keyboard shortcuts: 1-5 switch tabs (when not typing in a field);
        // Ctrl+F focuses the current tab's search box.
        if (!ImGui::GetIO().WantTextInput) {
            for (int i = 0; i < 5; ++i)
                if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i), false)) tab = i;
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
            focusSearch = true;
        g_lastTab = tab;   // persisted on exit (and every SavePrefs)

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("##launcher", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        // Soft accent glow across the top of the window: gives the launcher a
        // branded, finished feel without shipping any image assets.
        {
            ImDrawList* bg = ImGui::GetWindowDrawList();
            ImVec2 wp = vp->WorkPos;
            ImU32 c0 = ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.10f));
            ImU32 c1 = ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.00f));
            bg->AddRectFilledMultiColor(wp, ImVec2(wp.x + vp->WorkSize.x, wp.y + 150.0f),
                                        c0, c0, c1, c1);
        }

        // ---- Left nav ----
        ImGui::BeginChild("nav", ImVec2(224, 0), true);
        ImGui::Dummy(ImVec2(0, 6));
        {   // App header: rounded accent logo tile + product name / version.
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);
            ImVec2 lp = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(lp, ImVec2(lp.x + 34, lp.y + 34),
                              ImGui::GetColorU32(kAccent), 9.0f);
            ImVec2 ts = ImGui::CalcTextSize("OS");
            dl->AddText(ImVec2(lp.x + (34 - ts.x) * 0.5f, lp.y + (34 - ts.y) * 0.5f),
                        IM_COL32(255, 255, 255, 255), "OS");
            ImGui::Dummy(ImVec2(34 + 8, 34));
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Text("OkaySpace");
            ImGui::TextDisabled("v%s", OKAY_ENGINE_VERSION);
            ImGui::EndGroup();
        }
        ImGui::Dummy(ImVec2(0, 16));
        ImGui::TextDisabled("  MENU");
        ImGui::Dummy(ImVec2(0, 2));
        const char* navs[] = {"Create", "Play", "Community", "Account", "Settings"};
        for (int i = 0; i < 5; ++i) {
            char lbl[48];
            std::snprintf(lbl, sizeof(lbl), "          %s", navs[i]);   // room for the icon
            bool sel = (tab == i);
            // Accent the active item's label so the selection reads clearly.
            if (sel) ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
            if (ImGui::Selectable(lbl, sel, 0, ImVec2(0, 42))) tab = i;
            if (sel) ImGui::PopStyleColor();
            ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (sel)   // accent bar marking the active section
                dl->AddRectFilled(ImVec2(mn.x, mn.y + 8), ImVec2(mn.x + 3.5f, mx.y - 8),
                                  ImGui::GetColorU32(kAccent), 2.0f);
            // Little vector icon per section (no icon font needed).
            {
                float cy = (mn.y + mx.y) * 0.5f, cx = mn.x + 24.0f, r = 6.5f;
                ImU32 ic = ImGui::GetColorU32(sel ? kAccent
                                                  : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                switch (i) {
                    case 0:   // Create: plus
                        dl->AddLine(ImVec2(cx - r, cy), ImVec2(cx + r, cy), ic, 2.2f);
                        dl->AddLine(ImVec2(cx, cy - r), ImVec2(cx, cy + r), ic, 2.2f);
                        break;
                    case 1:   // Play: triangle
                        dl->AddTriangleFilled(ImVec2(cx - r * 0.7f, cy - r),
                                              ImVec2(cx - r * 0.7f, cy + r),
                                              ImVec2(cx + r, cy), ic);
                        break;
                    case 2:   // Community: three heads
                        dl->AddCircleFilled(ImVec2(cx - r * 0.9f, cy + r * 0.45f), r * 0.42f, ic);
                        dl->AddCircleFilled(ImVec2(cx + r * 0.9f, cy + r * 0.45f), r * 0.42f, ic);
                        dl->AddCircleFilled(ImVec2(cx, cy - r * 0.45f), r * 0.52f, ic);
                        break;
                    case 3:   // Account: head + shoulders
                        dl->AddCircleFilled(ImVec2(cx, cy - r * 0.45f), r * 0.45f, ic);
                        dl->AddRectFilled(ImVec2(cx - r * 0.85f, cy + r * 0.1f),
                                          ImVec2(cx + r * 0.85f, cy + r * 0.95f), ic, r * 0.45f);
                        break;
                    case 4: { // Settings: gear (ring + spokes)
                        dl->AddCircle(ImVec2(cx, cy), r * 0.62f, ic, 12, 2.0f);
                        for (int k = 0; k < 8; ++k) {
                            float a = (float)k * 0.785398f;
                            dl->AddLine(ImVec2(cx + std::cos(a) * r * 0.72f, cy + std::sin(a) * r * 0.72f),
                                        ImVec2(cx + std::cos(a) * r * 1.05f, cy + std::sin(a) * r * 1.05f),
                                        ic, 2.0f);
                        }
                        break;
                    }
                }
            }
            // Play shows how many games are installed, right-aligned in the row.
            if (i == 1 && !scenes.empty()) {
                char nb[16]; std::snprintf(nb, sizeof(nb), "%d", (int)scenes.size());
                ImVec2 ts = ImGui::CalcTextSize(nb);
                ImVec2 bp(mx.x - ts.x - 18.0f, (mn.y + mx.y) * 0.5f - ts.y * 0.5f);
                dl->AddRectFilled(ImVec2(bp.x - 6, bp.y - 2), ImVec2(bp.x + ts.x + 6, bp.y + ts.y + 2),
                                  ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.22f)), 8.0f);
                dl->AddText(bp, ImGui::GetColorU32(ImGuiCol_Text), nb);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Shortcut: %d", i + 1);
        }

        // Signed-in status chip, just under the nav items.
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::TextDisabled("  ACCOUNT");
        if (account.IsLoggedIn()) {
            ImGui::TextColored(ImVec4(0.55f, 0.9f, 0.6f, 1), "  %s  %s",
                               account.IsOnline() ? "online" : "local",
                               account.CurrentSession().username.c_str());
        } else {
            ImGui::TextDisabled("  not signed in");
        }

        // ---- Update status (pinned to the bottom of the nav) ----
        UpState st = GetState();
        float footH = 78.0f;
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - footH);
        ImGui::Separator();
        ImVec4 col = (st == Up_Failed)  ? ImVec4(1.0f, 0.55f, 0.55f, 1)
                   : (st == Up_Updated) ? ImVec4(0.55f, 0.9f, 0.6f, 1)
                   : (st == Up_Downloading || st == Up_Checking)
                                        ? ImVec4(0.85f, 0.8f, 0.4f, 1)
                                        : ImVec4(0.6f, 0.65f, 0.75f, 1);
        const char* head = (st == Up_Downloading) ? "Updating..."
                         : (st == Up_Checking)    ? "Checking..."
                         : (st == Up_Updated)     ? "Updated"
                         : (st == Up_Failed)      ? "Update failed"
                                                  : "Engine";
        if (st == Up_Checking || st == Up_Downloading) {
            // Spinner: an arc sweeping around while the worker runs.
            ImVec2 sp = ImGui::GetCursorScreenPos();
            float rad = 6.0f;
            ImVec2 ctr(sp.x + rad + 2.0f, sp.y + rad + 3.0f);
            float a0 = (float)ImGui::GetTime() * 6.0f;
            ImDrawList* sdl = ImGui::GetWindowDrawList();
            sdl->PathArcTo(ctr, rad, a0, a0 + 4.6f, 20);
            sdl->PathStroke(ImGui::GetColorU32(kAccent), 0, 2.4f);
            ImGui::Dummy(ImVec2(rad * 2.0f + 6.0f, rad * 2.0f + 4.0f));
            ImGui::SameLine();
        }
        ImGui::TextColored(col, "%s", head);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", GetUpMsg().c_str());
        ImGui::PopTextWrapPos();
        bool busy = (st == Up_Checking || st == Up_Downloading);
        ImGui::BeginDisabled(busy);
        if (ImGui::SmallButton("Check for updates")) StartUpdateCheck();
        ImGui::EndDisabled();
        if (g_upRelaunchNeeded && st == Up_Updated) {
            ImGui::SameLine();
            // The new launcher exe is already swapped in place; start it and quit.
            if (ImGui::SmallButton("Restart now")) {
                Launch(g_selfPath);
                running = false;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("content", ImVec2(0, 0), true);

        const ImVec4 kTitle(0.85f, 0.9f, 1.0f, 1.0f);
        // Section title with a short accent underline; optional subtitle.
        auto sectionHeader = [&](const char* title, const char* subtitle) {
            ImGui::TextColored(kTitle, "%s", title);
            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(p.x, p.y + 1), ImVec2(p.x + w * 0.16f, p.y + 4),
                ImGui::GetColorU32(kAccent), 2.0f);
            ImGui::Dummy(ImVec2(0, subtitle && *subtitle ? 8 : 14));
            if (subtitle && *subtitle) {
                ImGui::TextDisabled("%s", subtitle);
                ImGui::Dummy(ImVec2(0, 10));
            }
        };
        if (tab == 0) {                                   // ---- Create ----
            sectionHeader("Create a game", nullptr);

            // Hero card: gradient panel with the one primary action.
            ImGui::BeginChild("hero", ImVec2(0, 118), true, ImGuiWindowFlags_NoScrollbar);
            {
                ImVec2 mn = ImGui::GetWindowPos(); ImVec2 sz = ImGui::GetWindowSize();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 g0 = ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.22f));
                ImU32 g1 = ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.03f));
                dl->AddRectFilledMultiColor(mn, ImVec2(mn.x + sz.x, mn.y + sz.y), g0, g1, g1, g0);
            }
            ImGui::SetCursorPos(ImVec2(18, 14));
            ImGui::Text("OkaySpace Editor");
            ImGui::SetCursorPosX(18);
            ImGui::TextDisabled("Build 2D and 3D games: scenes, scripting, UI, characters, multiplayer.");
            ImGui::SetCursorPos(ImVec2(18, 62));
            if (editor.empty()) {
                ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "Editor not found next to the launcher.");
                ImGui::SetCursorPosX(18);
                ImGui::TextDisabled("Place OkayEngine.exe beside this launcher, or run a Check for updates.");
            } else {
                if (PrimaryButton("Open Editor", ImVec2(190, 44))) LaunchEditor(editor);
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 13);
                ImGui::TextDisabled("%s", editor.c_str());
            }
            ImGui::EndChild();

            // ---- New project: a clickable template gallery ----
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::SeparatorText("New project");
            ImGui::TextDisabled("Pick a starting point — the editor opens with the template selected.");
            ImGui::Dummy(ImVec2(0, 6));
            // Titles must match the editor's New Project templates exactly.
            static const struct { const char* name; const char* desc; } kTpl[] = {
                {"2D Scene",             "A blank 2D canvas."},
                {"3D Scene",             "Ground, light and sky — start here."},
                {"First Person",         "FPS character: mouse-look, WASD, jump."},
                {"Third Person",         "Orbit-camera character controller."},
                {"Third Person Shooter", "TPS starter with aiming."},
                {"Point & Click",        "Click-to-move adventure starter."},
                {"Platformer",           "Side-scrolling jump-and-run."},
                {"Top-Down",             "Top-down movement and rooms."},
                {"Coin Collector",       "A complete pickup-the-coins game."},
                {"Snake",                "The classic, fully playable."},
                {"Main Menu",            "A title screen with buttons."},
                {"Inventory",            "Drag & drop item grid."},
                {"Multiplayer",          "Host / join networked starter."},
            };
            const int tplCount = (int)(sizeof(kTpl) / sizeof(kTpl[0]));
            float availW = ImGui::GetContentRegionAvail().x;
            int cols = (int)(availW / 235.0f);
            if (cols < 2) cols = 2; if (cols > 4) cols = 4;
            float cardW = (availW - (float)(cols - 1) * 8.0f) / (float)cols;
            if (editor.empty()) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
            for (int i = 0; i < tplCount; ++i) {
                if (i % cols) ImGui::SameLine();
                ImGui::PushID(i);
                ImGui::BeginChild("tpl", ImVec2(cardW, 92), true, ImGuiWindowFlags_NoScrollbar);
                bool hov = !editor.empty() && ImGui::IsWindowHovered();
                if (hov) HoverRing();
                ImGui::SetCursorPos(ImVec2(12, 12));
                IconTile(kTpl[i].name, 30.0f);
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::Text("%s", kTpl[i].name);
                ImGui::PushTextWrapPos(cardW - 14.0f);
                ImGui::TextDisabled("%s", kTpl[i].desc);
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    LaunchEditor(editor, kTpl[i].name);
                    Toast(std::string("Opening editor: ") + kTpl[i].name);
                }
                ImGui::EndChild();
                ImGui::PopID();
            }
            if (editor.empty()) ImGui::PopStyleVar();

            ImGui::Dummy(ImVec2(0, 14));
            ImGui::SeparatorText("What's new");
            // Curated highlights of the last few releases (updated each ship).
            static const char* kNews[] = {
                "NPC pathfinding (A*) with scriptable npc_goto commands",
                "Spawner waves: count / wave delay / max alive, script control",
                "Click-placed patrol waypoints + NPC vision-cone gizmos",
                "Script Editor: signature help and smarter autocomplete",
                "Hierarchy & Project: Shift+click range select, multi copy/paste",
            };
            for (const char* n : kNews) ImGui::BulletText("%s", n);
            ImGui::TextDisabled("Full notes: GitHub repository (link below).");

            ImGui::Dummy(ImVec2(0, 10));
            ImGui::SeparatorText("Tips");
            ImGui::BulletText("Press Play in the editor to test instantly.");
            ImGui::BulletText("Drag assets from the Project panel onto objects.");
            ImGui::BulletText("Add UI from GameObject > UI (buttons, sliders, radial loaders).");
        } else if (tab == 1) {                            // ---- Play ----
            ImGui::TextColored(kTitle, "Play a game");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 92);
            if (ImGui::Button("Refresh", ImVec2(92, 0))) scenes = FindScenes();
            { ImVec2 p = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x;
              ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + 1),
                  ImVec2(p.x + w * 0.16f, p.y + 4), ImGui::GetColorU32(kAccent), 2.0f); }
            ImGui::Dummy(ImVec2(0, 14));
            if (player.empty())
                ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "Player runtime not found next to the launcher.");
            else if (scenes.empty()) {
                // Empty state: a friendly centered card instead of a bare line.
                ImGui::Dummy(ImVec2(0, 26));
                ImGui::BeginChild("noGames", ImVec2(0, 150), true, ImGuiWindowFlags_NoScrollbar);
                float cw = ImGui::GetContentRegionAvail().x;
                auto center = [&](const char* s) {
                    float tw = ImGui::CalcTextSize(s).x;
                    ImGui::SetCursorPosX((cw - tw) * 0.5f);
                };
                ImGui::Dummy(ImVec2(0, 22));
                center("No games yet");
                ImGui::Text("No games yet");
                ImGui::Dummy(ImVec2(0, 2));
                center("Build one from the editor (File > Build Game), then put it next to");
                ImGui::TextDisabled("Build one from the editor (File > Build Game), then put it next to");
                center("the launcher or in a 'games' folder and hit Refresh.");
                ImGui::TextDisabled("the launcher or in a 'games' folder and hit Refresh.");
                ImGui::Dummy(ImVec2(0, 8));
                ImGui::SetCursorPosX((cw - 170.0f) * 0.5f);
                if (!editor.empty() && PrimaryButton("Open Editor", ImVec2(170, 36)))
                    LaunchEditor(editor);
                ImGui::EndChild();
            } else {
                auto lower = [](std::string s) {
                    for (char& c : s) c = (char)std::tolower((unsigned char)c);
                    return s;
                };
                // Toolbar: search, sort, open-folder.
                if (focusSearch) { ImGui::SetKeyboardFocusHere(); focusSearch = false; }
                ImGui::PushItemWidth(-1);
                ImGui::InputTextWithHint("##playFilter", "Search games...  (Ctrl+F)", playFilter, sizeof(playFilter));
                ImGui::PopItemWidth();
                ImGui::PushItemWidth(180);
                if (ImGui::Combo("##playsort", &g_playSort,
                                 "Favorites first\0Name A\xE2\x80\x93Z\0Recently played\0")) SavePrefs();
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Open games folder")) OpenExternal(g_exeDir);
                ImGui::Dummy(ImVec2(0, 6));

                // Display order: optional favorites-first, then name A–Z.
                std::vector<std::size_t> order(scenes.size());
                for (std::size_t i = 0; i < scenes.size(); ++i) order[i] = i;
                std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                    if (g_playSort == 0) {
                        bool fa = IsFavorite(scenes[a].string()), fb = IsFavorite(scenes[b].string());
                        if (fa != fb) return fa;
                    } else if (g_playSort == 2) {
                        std::size_t ra = RecentRank(scenes[a].string()), rb = RecentRank(scenes[b].string());
                        if (ra != rb) return ra < rb;
                    }
                    return lower(scenes[a].filename().string()) < lower(scenes[b].filename().string());
                });

                std::string needle = lower(playFilter);
                int shown = 0;
                for (std::size_t oi = 0; oi < order.size(); ++oi) {
                    std::size_t i = order[oi];
                    std::string name = scenes[i].filename().string();
                    if (!needle.empty() && lower(name).find(needle) == std::string::npos)
                        continue;
                    ++shown;
                    std::string path = scenes[i].string();
                    bool fav = IsFavorite(path);
                    ImGui::PushID((int)i);
                    ImGui::BeginChild("game", ImVec2(0, 64), true, ImGuiWindowFlags_NoScrollbar);
                    HoverRing();
                    ImGui::SetCursorPos(ImVec2(10, 10));
                    IconTile(name, 44.0f);
                    ImGui::SameLine();
                    ImGui::BeginGroup();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3);
                    ImGui::Text("%s", name.c_str());
                    if (RecentRank(path) < 3) {
                        ImGui::SameLine();
                        ImGui::TextColored(kAccent, "recent");
                    }
                    std::string ago = ModifiedAgo(scenes[i]);
                    if (ago.empty())
                        ImGui::TextDisabled("%s", scenes[i].parent_path().string().c_str());
                    else
                        ImGui::TextDisabled("%s  \xC2\xB7  %s",
                                            scenes[i].parent_path().string().c_str(), ago.c_str());
                    ImGui::EndGroup();
                    // Right-aligned: favorite star, Folder, Play.
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - (36 + 82 + 80 + 20));
                    ImGui::SetCursorPosY(12.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        fav ? ImVec4(1.0f, 0.80f, 0.25f, 1) : ImVec4(0.55f, 0.58f, 0.65f, 1));
                    if (ImGui::Button("*", ImVec2(36, 40))) { ToggleFavorite(path); SavePrefs(); }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(fav ? "Unfavorite" : "Favorite");
                    ImGui::SameLine();
                    if (ImGui::Button("Folder", ImVec2(82, 40))) OpenExternal(scenes[i].parent_path().string());
                    ImGui::SameLine();
                    if (PrimaryButton("Play", ImVec2(80, 40))) {
                        Launch(player, path);
                        RecordPlayed(path); SavePrefs();
                        Toast(std::string("Playing ") + name);
                    }
                    // Right-click anywhere on the row for the usual file actions.
                    if (ImGui::BeginPopupContextWindow("gamectx", ImGuiPopupFlags_MouseButtonRight)) {
                        if (ImGui::MenuItem("Play")) {
                            Launch(player, path); RecordPlayed(path); SavePrefs();
                            Toast(std::string("Playing ") + name);
                        }
                        if (ImGui::MenuItem(fav ? "Unfavorite" : "Favorite")) { ToggleFavorite(path); SavePrefs(); }
                        if (ImGui::MenuItem("Show in Explorer")) OpenExternal(scenes[i].parent_path().string());
                        if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(path.c_str());
                        ImGui::EndPopup();
                    }
                    ImGui::EndChild();
                    ImGui::PopID();
                }
                if (shown == 0)
                    ImGui::TextDisabled("No games match \"%s\".", playFilter);
                else
                    ImGui::TextDisabled("%d game%s%s", shown, shown == 1 ? "" : "s",
                                        needle.empty() ? "" : " matching");
            }
        } else if (tab == 2) {                            // ---- Community ----
            sectionHeader("Community",
                "Play and share games, levels, scripts and models with other creators.");

            // Play a shared game in place — no install, no copy into your projects.
            ImGui::TextWrapped("Got a game from a friend? Drag its game folder or .okayscene "
                "onto this window to play it instantly — nothing is downloaded or copied. "
                "Or keep it in your library below.");
            ImGui::Dummy(ImVec2(0, 8));
            if (ImGui::Button("Open community folder", ImVec2(200, 0))) OpenExternal(CommunityDir().string());
            ImGui::SameLine();
            if (ImGui::Button("Refresh", ImVec2(110, 0))) scenes = FindScenes();
            ImGui::SameLine();
            ImGui::TextDisabled("Drop shared game folders here, then Refresh.");
            ImGui::Dummy(ImVec2(0, 12));

            // Installed community content (scenes living under community/).
            ImGui::SeparatorText("Your community library");
            if (focusSearch) { ImGui::SetKeyboardFocusHere(); focusSearch = false; }
            ImGui::PushItemWidth(-1);
            ImGui::InputTextWithHint("##commFilter", "Search your library...  (Ctrl+F)",
                                     commFilter, sizeof(commFilter));
            ImGui::PopItemWidth();
            ImGui::Dummy(ImVec2(0, 4));
            auto clower = [](std::string s) {
                for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
                return s;
            };
            std::string cneedle = clower(commFilter);
            int cShown = 0;
            for (std::size_t i = 0; i < scenes.size(); ++i) {
                fs::path croot = CommunityItemRoot(scenes[i]);
                if (croot.empty()) continue;
                if (!cneedle.empty() &&
                    clower(scenes[i].filename().string() + " " + croot.filename().string())
                        .find(cneedle) == std::string::npos) continue;
                ++cShown;
                std::string cpath = scenes[i].string();
                ImGui::PushID((int)(i + 5000));
                ImGui::BeginChild("citem", ImVec2(0, 64), true, ImGuiWindowFlags_NoScrollbar);
                HoverRing();
                ImGui::SetCursorPos(ImVec2(10, 10));
                IconTile(scenes[i].filename().string(), 44.0f);
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3);
                ImGui::Text("%s", scenes[i].filename().string().c_str());
                ImGui::TextDisabled("%s", croot.filename().string().c_str());
                ImGui::EndGroup();
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - (80 + 82 + 82 + 24));
                ImGui::SetCursorPosY(12.0f);
                ImGui::BeginDisabled(player.empty());
                if (PrimaryButton("Play", ImVec2(80, 40))) { Launch(player, cpath); RecordPlayed(cpath); SavePrefs(); }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Folder", ImVec2(82, 40))) OpenExternal(croot.string());
                ImGui::SameLine();
                if (ImGui::Button("Remove", ImVec2(82, 40))) {
                    std::error_code rec; fs::remove_all(croot, rec);
                    Toast(rec ? "Remove failed" : "Removed from library", rec ? 2 : 1);
                    scenes = FindScenes();
                }
                ImGui::EndChild();
                ImGui::PopID();
            }
            if (cShown == 0) {
                ImGui::Dummy(ImVec2(0, 4));
                if (!cneedle.empty())
                    ImGui::TextDisabled("No library items match \"%s\".", commFilter);
                else
                    ImGui::TextDisabled("Nothing here yet. Drop a shared game into the community folder "
                                        "(button above) and hit Refresh, or drag a .okayscene onto the window to play it.");
            }

            // Share your own creations.
            ImGui::Dummy(ImVec2(0, 14));
            ImGui::SeparatorText("Share your creation");
            ImGui::TextWrapped("Build your game in the editor (File > Build Game), then share the whole "
                "game folder (zip it) so friends can drop it in their community folder and play. "
                "One-click online publishing is coming soon.");
            ImGui::Dummy(ImVec2(0, 4));
            if (ImGui::Button("Reveal my games folder", ImVec2(220, 0))) OpenExternal(g_exeDir);

            // Starter templates — quick starting points, opened in the editor.
            ImGui::Dummy(ImVec2(0, 16));
            ImGui::SeparatorText("Starter templates");
            ImGui::PushItemWidth(-1);
            ImGui::InputTextWithHint("##marketFilter", "Search templates...", marketFilter, sizeof(marketFilter));
            ImGui::PopItemWidth();
            ImGui::Dummy(ImVec2(0, 4));
            auto mlower = [](std::string s) {
                for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
                return s;
            };
            std::string mneedle = mlower(marketFilter);
            int mShown = 0;
            for (const auto& t : templates) {
                if (!mneedle.empty() &&
                    mlower(std::string(t.name) + " " + t.desc).find(mneedle) == std::string::npos)
                    continue;
                ++mShown;
                ImGui::PushID(t.name);
                ImGui::BeginChild(t.name, ImVec2(0, 66), true, ImGuiWindowFlags_NoScrollbar);
                HoverRing();
                ImGui::SetCursorPos(ImVec2(10, 11));
                IconTile(t.name, 44.0f);
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
                ImGui::Text("%s", t.name);
                ImGui::TextDisabled("%s", t.desc);
                ImGui::EndGroup();
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90);
                ImGui::SetCursorPosY(11.0f);
                ImGui::BeginDisabled(editor.empty());
                if (ImGui::Button("Open", ImVec2(90, 44))) LaunchEditor(editor, t.tmpl);
                ImGui::EndDisabled();
                ImGui::EndChild();
                ImGui::PopID();
            }
            if (mShown == 0) ImGui::TextDisabled("No templates match \"%s\".", marketFilter);
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::TextDisabled("Opens the editor's New Project on the chosen template.");
        } else if (tab == 3) {                            // ---- Account ----
            sectionHeader("Account", nullptr);

            if (account.IsLoggedIn()) {
                const auto& s = account.CurrentSession();
                ImGui::Dummy(ImVec2(0, 8));
                // Profile card: avatar initial + name + account details.
                ImGui::BeginChild("profile", ImVec2(0, 110), true);
                ImVec2 cp = ImGui::GetCursorScreenPos();
                float r = 30.0f;
                ImVec2 ctr(cp.x + r + 4, cp.y + r + 6);
                // Avatar tinted by the username (same stable hash as game tiles),
                // so each account gets its own recognizable color.
                ImVec4 av = NameColor(s.username);
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ctr, r, ImGui::GetColorU32(ImVec4(av.x * 0.55f, av.y * 0.55f, av.z * 0.55f, 1.0f)), 32);
                ImGui::GetWindowDrawList()->AddCircle(ctr, r, ImGui::GetColorU32(av), 32, 2.0f);
                char initial[2] = { (char)(s.username.empty() ? '?' : std::toupper((unsigned char)s.username[0])), 0 };
                ImVec2 ts = ImGui::CalcTextSize(initial);
                ImGui::GetWindowDrawList()->AddText(ImVec2(ctr.x - ts.x * 0.5f, ctr.y - ts.y * 0.5f),
                                                    ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), initial);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + r * 2 + 22);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10);
                ImGui::TextColored(ImVec4(0.92f, 0.94f, 0.98f, 1), "%s", s.username.c_str());
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + r * 2 + 22);
                ImGui::TextColored(account.IsOnline() ? ImVec4(0.55f, 0.9f, 0.6f, 1)
                                                      : ImVec4(0.70f, 0.72f, 0.78f, 1),
                                   account.IsOnline() ? "Online" : "Local (this device)");
                if (account.IsOnline()) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + r * 2 + 22);
                    ImGui::TextDisabled("%s", account.ServerUrl().c_str());
                }
                ImGui::EndChild();
                ImGui::Dummy(ImVec2(0, 12));
                if (account.IsOnline() && ImGui::Button("Re-check session", ImVec2(180, 44)))
                    account.VerifySession();
                if (account.IsOnline()) ImGui::SameLine();
                if (ImGui::Button("Sign out", ImVec2(160, 44))) {
                    account.Logout();
                    acctMessage.clear();
                    acctUser[0] = acctPass[0] = '\0';
                    Toast("Signed out", 1);
                }

                // ---- Account details ----
                ImGui::Dummy(ImVec2(0, 14));
                ImGui::SeparatorText("Account details");
                const float kFieldW = 300.0f, kBtnW = 170.0f;

                // Change username (all backends).
                ImGui::PushItemWidth(kFieldW);
                ImGui::InputTextWithHint("##acctChgName", "New username", acctChgName, sizeof(acctChgName));
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Update username", ImVec2(kBtnW, 0))) {
                    acct::Result r = account.ChangeUsername(acctChgName);
                    acctMessageError = !r.ok;
                    acctMessage = r.ok ? "Username updated." : r.error;
                    if (r.ok) { acctChgName[0] = '\0'; Toast("Username updated", 1); }
                }

                // Change email (online / Supabase only).
                if (account.UsesEmail()) {
                    ImGui::PushItemWidth(kFieldW);
                    ImGui::InputTextWithHint("##acctChgEmail", "New email", acctChgEmail, sizeof(acctChgEmail));
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::Button("Update email", ImVec2(kBtnW, 0))) {
                        acct::Result r = account.ChangeEmail(acctChgEmail);
                        acctMessageError = !r.ok;
                        acctMessage = r.ok ? "Email change requested — confirm via the link sent to it."
                                           : r.error;
                        if (r.ok) { acctChgEmail[0] = '\0'; Toast("Email change requested", 1); }
                    }
                }

                // Change password.
                ImGui::PushItemWidth(kFieldW);
                bool go = ImGui::InputTextWithHint("##acctNewPass", "New password", acctNewPass,
                              sizeof(acctNewPass), ImGuiInputTextFlags_Password |
                              ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Update password", ImVec2(kBtnW, 0)) || go) {
                    acct::Result r = account.ChangePassword(acctNewPass);
                    acctMessageError = !r.ok;
                    acctMessage = r.ok ? "Password updated." : r.error;
                    if (r.ok) { std::fill(acctNewPass, acctNewPass + sizeof(acctNewPass), '\0'); Toast("Password updated", 1); }
                }
                if (!acctMessage.empty()) {
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(acctMessageError ? ImVec4(1, 0.55f, 0.55f, 1)
                                                        : ImVec4(0.55f, 0.9f, 0.6f, 1),
                                       "%s", acctMessage.c_str());
                    ImGui::PopTextWrapPos();
                }
            } else {
                ImGui::TextWrapped(account.IsOnline()
                    ? "Sign in to your OkaySpace account to sync your work."
                    : "No account server configured — using a local dev account on "
                      "this device. Open the Settings tab to connect to a server "
                      "(Supabase) and sign in online.");
                if (account.IsOnline()) {
                    ImGui::TextDisabled("Server: %s (%s)", account.ServerUrl().c_str(),
                                        account.ProviderName());
                }
                ImGui::Dummy(ImVec2(0, 12));

                // Sign in / Create account toggle.
                if (ImGui::RadioButton("Sign in", !acctRegisterMode)) {
                    acctRegisterMode = false; acctMessage.clear();
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Create account", acctRegisterMode)) {
                    acctRegisterMode = true; acctMessage.clear();
                }
                ImGui::Dummy(ImVec2(0, 8));

                const char* idHint = account.UsesEmail() ? "Email" : "Username";
                ImGui::PushItemWidth(320);
                ImGui::InputTextWithHint("##acctUser", idHint, acctUser, sizeof(acctUser));
                // For Supabase, sign-up also takes a display username.
                if (acctRegisterMode && account.UsesEmail())
                    ImGui::InputTextWithHint("##acctName", "Username (display name)", acctName, sizeof(acctName));
                bool submit = ImGui::InputTextWithHint("##acctPass", "Password", acctPass,
                                  sizeof(acctPass), ImGuiInputTextFlags_Password |
                                  ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::PopItemWidth();
                ImGui::Dummy(ImVec2(0, 8));

                const char* btn = acctRegisterMode ? "Create account" : "Sign in";
                ImGui::BeginDisabled(acctBusy);
                if (ImGui::Button(btn, ImVec2(200, 48)) || submit) {
                    acctBusy = true;
                    acct::Result r = acctRegisterMode
                        ? account.Register(acctUser, acctPass, acctName)
                        : account.Login(acctUser, acctPass);
                    acctBusy = false;
                    acctMessageError = !r.ok;
                    if (r.ok) {
                        acctMessage.clear();
                        std::fill(acctPass, acctPass + sizeof(acctPass), '\0');
                        if (account.IsLoggedIn()) Toast("Signed in", 1);
                    } else {
                        acctMessage = r.error;
                    }
                }
                ImGui::EndDisabled();

                // Forgot password (online/Supabase): emails a reset link.
                if (account.UsesEmail()) {
                    ImGui::SameLine();
                    if (ImGui::Button("Forgot password?", ImVec2(180, 48))) {
                        acct::Result r = account.RequestPasswordReset(acctUser);
                        acctMessageError = !r.ok;
                        acctMessage = r.ok ? "Password reset email sent — check your inbox."
                                           : r.error;
                    }
                }

                if (!acctMessage.empty()) {
                    ImGui::Dummy(ImVec2(0, 6));
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(acctMessageError ? ImVec4(1, 0.55f, 0.55f, 1)
                                                        : ImVec4(0.55f, 0.9f, 0.6f, 1),
                                       "%s", acctMessage.c_str());
                    ImGui::PopTextWrapPos();
                }
            }
        } else {                                          // ---- Settings ----
            sectionHeader("Settings", nullptr);
            ImGui::TextWrapped("Account server. This build is preconfigured, so you "
                               "normally don't need to change anything here. Advanced: "
                               "point the launcher at a different server URL below, or "
                               "switch to local accounts on this device.");
            ImGui::Dummy(ImVec2(0, 12));
            ImGui::SeparatorText("Account server");

            ImGui::TextDisabled("Server URL");
            ImGui::PushItemWidth(460);
            ImGui::InputTextWithHint("##setUrl", "https://YOUR-PROJECT.supabase.co",
                                     setUrl, sizeof(setUrl));
            ImGui::PopItemWidth();
            ImGui::Dummy(ImVec2(0, 12));

            if (ImGui::Button("Save & apply", ImVec2(180, 46))) { applyAccountSettings(false); Toast("Settings saved", 1); }
            ImGui::SameLine();
            if (ImGui::Button("Use local (clear)", ImVec2(180, 46))) applyAccountSettings(true);

            ImGui::Dummy(ImVec2(0, 10));
            // accountPtr (not the per-frame alias) — applyAccountSettings above
            // may have just rebuilt it this frame.
            if (accountPtr->IsOnline())
                ImGui::TextDisabled("Status: connected to %s", accountPtr->ServerUrl().c_str());
            else
                ImGui::TextDisabled("Status: local accounts (no server)");
            if (!setStatus.empty()) {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(ImVec4(0.55f, 0.9f, 0.6f, 1), "%s", setStatus.c_str());
                ImGui::PopTextWrapPos();
            }

            // ---- Appearance ----
            ImGui::Dummy(ImVec2(0, 16));
            ImGui::SeparatorText("Appearance");
            ImGui::TextDisabled("Theme");
            // Preview cards: a tiny mock window per theme (top bar, side panel,
            // content area, accent strip) — pick by look, not by name.
            static const struct { ImVec4 bg, panel, text; } kThemePrev[] = {
                {ImVec4(0.180f, 0.180f, 0.192f, 1), ImVec4(0.265f, 0.265f, 0.285f, 1), ImVec4(0.86f, 0.87f, 0.88f, 1)},
                {ImVec4(0.035f, 0.040f, 0.060f, 1), ImVec4(0.120f, 0.140f, 0.200f, 1), ImVec4(0.90f, 0.93f, 0.98f, 1)},
                {ImVec4(0.930f, 0.940f, 0.960f, 1), ImVec4(0.845f, 0.865f, 0.905f, 1), ImVec4(0.10f, 0.12f, 0.16f, 1)},
            };
            for (int i = 0; i < 3; ++i) {
                if (i) ImGui::SameLine();
                ImGui::PushID(i);
                const ImVec2 sz(108, 76);
                ImVec2 p = ImGui::GetCursorScreenPos();
                bool clicked = ImGui::InvisibleButton("##themecard", sz);
                bool hov = ImGui::IsItemHovered();
                bool cur = (g_themeIndex == i);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y),
                                  ImGui::GetColorU32(kThemePrev[i].bg), 7.0f);
                ImU32 pc = ImGui::GetColorU32(kThemePrev[i].panel);
                dl->AddRectFilled(ImVec2(p.x + 8, p.y + 8),  ImVec2(p.x + sz.x - 8, p.y + 17), pc, 3.0f);
                dl->AddRectFilled(ImVec2(p.x + 8, p.y + 22), ImVec2(p.x + 38, p.y + sz.y - 22), pc, 3.0f);
                dl->AddRectFilled(ImVec2(p.x + 43, p.y + 22), ImVec2(p.x + sz.x - 8, p.y + sz.y - 22), pc, 3.0f);
                dl->AddRectFilled(ImVec2(p.x + 8, p.y + 8),  ImVec2(p.x + 24, p.y + 11),
                                  ImGui::GetColorU32(kAccent), 2.0f);
                ImVec2 ts = ImGui::CalcTextSize(kThemeNames[i]);
                dl->AddText(ImVec2(p.x + (sz.x - ts.x) * 0.5f, p.y + sz.y - 18),
                            ImGui::GetColorU32(kThemePrev[i].text), kThemeNames[i]);
                dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y),
                            ImGui::GetColorU32(cur ? kAccent
                                                   : (hov ? ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.6f)
                                                          : ImGui::GetStyleColorVec4(ImGuiCol_Border))),
                            7.0f, 0, cur ? 2.5f : 1.5f);
                if (clicked && !cur) { g_themeIndex = i; DarkTheme(); SavePrefs(); }
                ImGui::PopID();
            }
            ImGui::Dummy(ImVec2(0, 6));
            ImGui::TextDisabled("Accent color");
            const int accentCount = (int)(sizeof(kAccentPresets) / sizeof(kAccentPresets[0]));
            for (int i = 0; i < accentCount; ++i) {
                ImGui::PushID(i);
                ImGui::PushStyleColor(ImGuiCol_Button, kAccentPresets[i].col);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentPresets[i].col);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentPresets[i].dim);
                bool current = (g_accentIndex == i);
                if (ImGui::Button("##accent", ImVec2(34, 34))) {
                    ApplyAccent(i);
                    SavePrefs();
                }
                if (current) {   // white ring + drawn check on the active swatch
                    ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRect(mn, mx, IM_COL32(255, 255, 255, 230),
                                ImGui::GetStyle().FrameRounding, 0, 2.5f);
                    ImVec2 c((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f);
                    dl->AddLine(ImVec2(c.x - 6, c.y), ImVec2(c.x - 2, c.y + 4),
                                IM_COL32(255, 255, 255, 240), 2.5f);
                    dl->AddLine(ImVec2(c.x - 2, c.y + 4), ImVec2(c.x + 6, c.y - 4),
                                IM_COL32(255, 255, 255, 240), 2.5f);
                }
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kAccentPresets[i].name);
                ImGui::PopID();
                if (i < accentCount - 1) ImGui::SameLine();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("  %s", kAccentPresets[g_accentIndex].name);

            ImGui::Dummy(ImVec2(0, 6));
            ImGui::TextDisabled("UI scale");
            ImGui::PushItemWidth(280);
            if (ImGui::SliderFloat("##uiscale", &g_uiScale, 0.8f, 1.6f, "%.2fx"))
                ImGui::GetIO().FontGlobalScale = g_uiScale;   // live preview
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Apply##scale")) { DarkTheme(); SavePrefs(); }

            // ---- Behavior ----
            ImGui::Dummy(ImVec2(0, 16));
            ImGui::SeparatorText("Behavior");
            if (ImGui::Checkbox("Check for updates when the launcher starts", &g_updateOnLaunch))
                SavePrefs();

            // ---- About ----
            ImGui::Dummy(ImVec2(0, 16));
            ImGui::SeparatorText("About");
            ImGui::Text("OkaySpace Engine");
            ImGui::SameLine();
            ImGui::TextDisabled("v%s", OKAY_ENGINE_VERSION);
            ImGui::TextDisabled("A small Unity-inspired C++ game engine.");
            if (ImGui::SmallButton("Documentation"))
                OpenExternal("https://github.com/kingimann/OkaySpaceGameEngine/blob/main/README.md");
            ImGui::SameLine();
            if (ImGui::SmallButton("GitHub repository"))
                OpenExternal("https://github.com/kingimann/OkaySpaceGameEngine");

            // ---- Maintenance ----
            ImGui::Dummy(ImVec2(0, 16));
            ImGui::SeparatorText("Maintenance");
            if (ImGui::Button("Open config folder")) OpenExternal(g_exeDir);
            ImGui::SameLine();
            if (ImGui::Button("Reset preferences")) {
                g_accentIndex = 0; g_themeIndex = 0; g_uiScale = 1.0f;
                g_updateOnLaunch = false; g_playSort = 0;
                ImGui::GetIO().FontGlobalScale = g_uiScale;
                ApplyAccent(g_accentIndex);   // re-applies theme + accent
                SavePrefs();
                Toast("Preferences reset", 1);
            }
            ImGui::TextDisabled("Preferences are stored in launcher.cfg next to the launcher.");
        }

        // ---- Footer (pinned to the bottom of the content panel) ----
        {
            const char* kRepo = "https://github.com/kingimann/OkaySpaceGameEngine";
            float fy = ImGui::GetWindowHeight() - 38.0f;
            if (fy > ImGui::GetCursorPosY()) ImGui::SetCursorPosY(fy);
            ImGui::Separator();
            ImGui::TextDisabled("OkaySpace v%s", OKAY_ENGINE_VERSION);
            ImGui::SameLine();
            ImGui::TextDisabled("  -  ");
            ImGui::SameLine();
            if (ImGui::SmallButton("Docs")) OpenExternal(std::string(kRepo) + "/blob/main/docs/accounts.md");
            ImGui::SameLine();
            if (ImGui::SmallButton("GitHub")) OpenExternal(kRepo);
        }

        ImGui::EndChild();
        ImGui::End();
        focusSearch = false;   // Ctrl+F is consumed the frame it's pressed

        // ---- Toast notification (bottom-right, auto-fading) ----
        if (SDL_GetTicks() < g_toastUntil && !g_toastMsg.empty()) {
            float remain = (float)(g_toastUntil - SDL_GetTicks());
            float alpha = remain > 400.0f ? 1.0f : remain / 400.0f;   // fade out
            ImVec2 wp = vp->WorkPos, ws = vp->WorkSize;
            ImVec4 tcol = g_toastKind == 1 ? ImVec4(0.45f, 0.85f, 0.52f, 1)
                        : g_toastKind == 2 ? ImVec4(0.96f, 0.48f, 0.45f, 1)
                                           : kAccent;
            ImGui::SetNextWindowPos(ImVec2(wp.x + ws.x - 18, wp.y + ws.y - 18), ImGuiCond_Always, ImVec2(1, 1));
            ImGui::SetNextWindowBgAlpha(0.92f * alpha);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.97f, 1.0f, alpha));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(tcol.x, tcol.y, tcol.z, alpha));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 9.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 12));
            if (ImGui::Begin("##toast", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_AlwaysAutoResize)) {
                // Status dot in the toast's kind color (info/success/error).
                float r = ImGui::GetFontSize() * 0.24f;
                ImVec2 dp = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2(dp.x + r, dp.y + ImGui::GetTextLineHeight() * 0.55f), r,
                    ImGui::GetColorU32(ImVec4(tcol.x, tcol.y, tcol.z, alpha)));
                ImGui::Dummy(ImVec2(r * 2.0f + 2.0f, ImGui::GetTextLineHeight()));
                ImGui::SameLine(0, 6);
                ImGui::TextUnformatted(g_toastMsg.c_str());
            }
            ImGui::End();
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(2);
        }

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 18, 20, 26, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    // Remember the window size for next launch.
    { int w = 0, h = 0; SDL_GetWindowSize(window, &w, &h);
      if (w > 0 && h > 0) { g_winW = w; g_winH = h; SavePrefs(); } }

    if (g_upThread) { SDL_WaitThread(g_upThread, nullptr); g_upThread = nullptr; }
    if (g_upMutex) { SDL_DestroyMutex(g_upMutex); g_upMutex = nullptr; }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
