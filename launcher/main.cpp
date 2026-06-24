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

#include <algorithm>
#include <cctype>
#include <chrono>
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
        SetUpMsg("Downloading engine v" + latest + "...");
        ok = ReplaceFile(std::string(kRawBase) + "OkayEngine.exe",
                         dir / "OkayEngine.exe", false) && ok;
        SetUpMsg("Downloading player runtime v" + latest + "...");
        ok = ReplaceFile(std::string(kRawBase) + "OkaySpacePlayer.exe",
                         dir / "OkaySpacePlayer.exe", false) && ok;
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
    // Look next to the launcher and in a tidy "Engine" subfolder (organized layout).
    const char* dirs[] = {"", "Engine", "runtime", "bin"};
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
    return out;
}

// Shared accent so UI code can match the theme.
// Mutable so the Appearance setting can recolor the UI live (DarkTheme() reads
// these, so changing them and re-applying restyles everything).
ImVec4 kAccent(0.30f, 0.62f, 1.00f, 1.0f);     // friendly blue
ImVec4 kAccentDim(0.22f, 0.44f, 0.74f, 1.0f);

// Selectable accent colors (name, main, dim).
struct AccentPreset { const char* name; ImVec4 col; ImVec4 dim; };
const AccentPreset kAccentPresets[] = {
    {"Blue",   ImVec4(0.30f, 0.62f, 1.00f, 1), ImVec4(0.22f, 0.44f, 0.74f, 1)},
    {"Green",  ImVec4(0.28f, 0.80f, 0.47f, 1), ImVec4(0.20f, 0.56f, 0.33f, 1)},
    {"Purple", ImVec4(0.62f, 0.46f, 1.00f, 1), ImVec4(0.44f, 0.32f, 0.74f, 1)},
    {"Orange", ImVec4(1.00f, 0.56f, 0.26f, 1), ImVec4(0.74f, 0.40f, 0.18f, 1)},
    {"Pink",   ImVec4(1.00f, 0.40f, 0.66f, 1), ImVec4(0.74f, 0.28f, 0.48f, 1)},
};
int g_accentIndex = 0;            // current accent preset (persisted)
bool g_updateOnLaunch = false;    // check for updates at startup (persisted)
int g_themeIndex = 0;             // 0 Dark, 1 Midnight, 2 Light (persisted)
float g_uiScale = 1.0f;           // font/UI scale for high-DPI (persisted)
int g_winW = 1040, g_winH = 680;  // last window size (persisted)
const char* kThemeNames[] = {"Dark", "Midnight", "Light"};

// Transient toast notification (bottom-right of the window).
std::string g_toastMsg;
Uint32 g_toastUntil = 0;
void Toast(const std::string& m) { g_toastMsg = m; g_toastUntil = SDL_GetTicks() + 2600; }
std::vector<std::string> g_favorites;   // favorited game paths (persisted)
std::vector<std::string> g_recent;       // recently played, most-recent first (persisted)
int g_playSort = 0;               // 0 Favorites first, 1 Name A–Z, 2 Recently played

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
    // Soft, modern rounding + generous spacing for a clean, flat look.
    s.WindowRounding = 0.0f;
    s.ChildRounding = 12.0f;
    s.FrameRounding = 8.0f; s.GrabRounding = 8.0f; s.PopupRounding = 10.0f;
    s.ScrollbarRounding = 10.0f; s.TabRounding = 8.0f;
    s.FramePadding = ImVec2(12, 10); s.ItemSpacing = ImVec2(10, 10);
    s.ItemInnerSpacing = ImVec2(8, 6);
    s.WindowPadding = ImVec2(18, 18); s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 1.0f; s.FrameBorderSize = 0.0f;
    s.ScrollbarSize = 12.0f; s.SeparatorTextBorderSize = 2.0f;
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
    } else {                                              // Dark (default)
        c[ImGuiCol_WindowBg]  = ImVec4(0.055f, 0.065f, 0.085f, 1.0f);
        c[ImGuiCol_ChildBg]   = ImVec4(0.100f, 0.112f, 0.142f, 1.0f);
        c[ImGuiCol_PopupBg]   = ImVec4(0.100f, 0.112f, 0.142f, 0.98f);
        c[ImGuiCol_Border]    = ImVec4(1, 1, 1, 0.055f);
        c[ImGuiCol_Text]      = ImVec4(0.93f, 0.95f, 0.98f, 1.0f);
        c[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.54f, 0.63f, 1.0f);
        c[ImGuiCol_Button]    = ImVec4(0.16f, 0.18f, 0.24f, 1.0f);
        c[ImGuiCol_FrameBg]   = ImVec4(0.14f, 0.16f, 0.21f, 1.0f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.21f, 0.27f, 1.0f);
        c[ImGuiCol_FrameBgActive]  = ImVec4(0.20f, 0.24f, 0.31f, 1.0f);
        c[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.07f);
    }
    // Accent-tinted slots (shared by all themes).
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
    okay::SetAppIcon(window);   // placeholder OkaySpace logo
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) return 1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // the launcher has a fixed layout
    ImGui::GetIO().ConfigDebugHighlightIdConflicts = false; // hide dev-only ID warnings
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
    bool acctRegisterMode = false;       // false = sign in, true = create account
    std::string acctMessage;             // last error/status, shown under the form
    bool acctMessageError = true;
    bool acctBusy = false;

    char playFilter[128] = {0};   // Play-tab search box
    int tab = 0; // 0 Create, 1 Play, 2 Marketplace, 3 Account, 4 Settings
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
        }
        // Once a background download finishes, re-detect the runtimes so the
        // "not found" notices clear without needing a restart.
        if (GetState() == Up_Updated && !rescannedAfterUpdate) {
            editor = FindExe({"OkayEngine.exe", "OkaySpaceEngine.exe", "okay-editor.exe", "okay-editor"});
            player = FindExe({"OkaySpacePlayer.exe", "okay-player.exe", "okay-player"});
            scenes = FindScenes();
            rescannedAfterUpdate = true;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Keyboard shortcuts: 1-5 switch tabs (when not typing in a field).
        if (!ImGui::GetIO().WantTextInput) {
            for (int i = 0; i < 5; ++i)
                if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i), false)) tab = i;
        }

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("##launcher", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        // ---- Left nav ----
        ImGui::BeginChild("nav", ImVec2(224, 0), true);
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextColored(kAccent, "  OkaySpace");
        ImGui::SameLine();
        ImGui::TextDisabled("v%s", OKAY_ENGINE_VERSION);
        ImGui::TextDisabled("  game engine");
        ImGui::Dummy(ImVec2(0, 18));
        ImGui::TextDisabled("  MENU");
        ImGui::Dummy(ImVec2(0, 2));
        const char* navs[]  = {"Create", "Play", "Marketplace", "Account", "Settings"};
        const char* navIco[] = {"+", ">", "*", "@", "="};
        for (int i = 0; i < 5; ++i) {
            char lbl[48];
            std::snprintf(lbl, sizeof(lbl), "   %s   %s", navIco[i], navs[i]);
            bool sel = (tab == i);
            // Accent the active item's label so the selection reads clearly.
            if (sel) ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
            if (ImGui::Selectable(lbl, sel, 0, ImVec2(0, 42))) tab = i;
            if (sel) ImGui::PopStyleColor();
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
        ImGui::TextColored(col, "%s", head);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", GetUpMsg().c_str());
        ImGui::PopTextWrapPos();
        bool busy = (st == Up_Checking || st == Up_Downloading);
        ImGui::BeginDisabled(busy);
        if (ImGui::SmallButton("Check for updates")) StartUpdateCheck();
        ImGui::EndDisabled();
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
            ImGui::TextWrapped("Open the OkaySpace editor to build 2D or 3D scenes, script "
                               "them, and design UI. Use File > Build Game to export a "
                               "standalone game you can share.");
            ImGui::Dummy(ImVec2(0, 18));
            if (editor.empty()) {
                ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "Editor not found next to the launcher.");
                ImGui::TextDisabled("Place OkayEngine.exe beside this launcher.");
            } else {
                if (ImGui::Button("Open Editor", ImVec2(240, 60))) LaunchEditor(editor);
                ImGui::Dummy(ImVec2(0, 6));
                ImGui::TextDisabled("%s", editor.c_str());

                // ---- New Project: pick a template, open the editor on it ----
                ImGui::Dummy(ImVec2(0, 16));
                ImGui::SeparatorText("New project");
                // Titles must match the editor's New Project templates exactly.
                static const char* kTemplates[] = {
                    "2D Scene", "3D Scene", "First Person", "Third Person",
                    "Third Person Shooter", "Point & Click", "Platformer", "Top-Down",
                    "Coin Collector", "Snake", "Main Menu", "Inventory", "Multiplayer",
                };
                static int newTpl = 1;   // default 3D Scene
                ImGui::TextDisabled("Template");
                ImGui::PushItemWidth(280);
                ImGui::Combo("##newtpl", &newTpl,
                             kTemplates, (int)(sizeof(kTemplates) / sizeof(kTemplates[0])));
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("New Project", ImVec2(150, 0))) {
                    LaunchEditor(editor, kTemplates[newTpl]);
                    Toast(std::string("Opening editor: ") + kTemplates[newTpl]);
                }
                ImGui::TextDisabled("Opens the editor with the chosen template selected.");
            }
            ImGui::Dummy(ImVec2(0, 22));
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
                ImGui::Dummy(ImVec2(0, 12));
                ImGui::TextDisabled("No games found yet.");
                ImGui::TextWrapped("Build one from the editor (File > Build Game), then put it "
                                   "next to the launcher or in a 'games' folder and hit Refresh.");
            } else {
                auto lower = [](std::string s) {
                    for (char& c : s) c = (char)std::tolower((unsigned char)c);
                    return s;
                };
                // Toolbar: search, sort, open-folder.
                ImGui::PushItemWidth(-1);
                ImGui::InputTextWithHint("##playFilter", "Search games...", playFilter, sizeof(playFilter));
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
                    ImGui::BeginChild("game", ImVec2(0, 62), true);
                    if (ImGui::IsWindowHovered()) {
                        ImVec2 mn = ImGui::GetWindowPos();
                        ImGui::GetWindowDrawList()->AddRect(mn,
                            ImVec2(mn.x + ImGui::GetWindowSize().x, mn.y + ImGui::GetWindowSize().y),
                            ImGui::GetColorU32(kAccent), 12.0f, 0, 2.0f);
                    }
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
                    ImGui::TextColored(ImVec4(0.92f, 0.94f, 0.98f, 1), "%s", name.c_str());
                    ImGui::TextDisabled("%s", scenes[i].parent_path().string().c_str());
                    // Right-aligned: favorite star, Folder, Play.
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - (36 + 82 + 80 + 20));
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 8);
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        fav ? ImVec4(1.0f, 0.80f, 0.25f, 1) : ImVec4(0.55f, 0.58f, 0.65f, 1));
                    if (ImGui::Button("*", ImVec2(36, 40))) { ToggleFavorite(path); SavePrefs(); }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(fav ? "Unfavorite" : "Favorite");
                    ImGui::SameLine();
                    if (ImGui::Button("Folder", ImVec2(82, 40))) OpenExternal(scenes[i].parent_path().string());
                    ImGui::SameLine();
                    if (ImGui::Button("Play", ImVec2(80, 40))) {
                        Launch(player, path);
                        RecordPlayed(path); SavePrefs();
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
        } else if (tab == 2) {                            // ---- Marketplace ----
            sectionHeader("Marketplace", nullptr);
            ImGui::TextDisabled("Starter templates — open one in the editor (New Project) to begin.");
            ImGui::Spacing();
            for (const auto& t : templates) {
                ImGui::PushID(t.name);
                ImGui::BeginChild(t.name, ImVec2(0, 70), true);
                if (ImGui::IsWindowHovered()) {
                    ImVec2 mn = ImGui::GetWindowPos();
                    ImGui::GetWindowDrawList()->AddRect(mn,
                        ImVec2(mn.x + ImGui::GetWindowSize().x, mn.y + ImGui::GetWindowSize().y),
                        ImGui::GetColorU32(kAccent), 12.0f, 0, 2.0f);
                }
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
                ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.95f, 1.0f), "%s", t.name);
                ImGui::TextDisabled("%s", t.desc);
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 90);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 6);
                ImGui::BeginDisabled(editor.empty());
                if (ImGui::Button("Open", ImVec2(90, 44))) LaunchEditor(editor, t.tmpl);
                ImGui::EndDisabled();
                ImGui::EndChild();
                ImGui::PopID();
            }
            ImGui::Dummy(ImVec2(0, 8));
            ImGui::TextDisabled("Opens the editor's New Project on the chosen template. "
                                "Community content marketplace coming soon.");
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
                ImGui::GetWindowDrawList()->AddCircleFilled(ctr, r, ImGui::GetColorU32(kAccentDim), 32);
                ImGui::GetWindowDrawList()->AddCircle(ctr, r, ImGui::GetColorU32(kAccent), 32, 2.0f);
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
                    Toast("Signed out");
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
                        ? account.Register(acctUser, acctPass)
                        : account.Login(acctUser, acctPass);
                    acctBusy = false;
                    acctMessageError = !r.ok;
                    if (r.ok) {
                        acctMessage.clear();
                        // Don't leave the password sitting in memory longer than needed.
                        std::fill(acctPass, acctPass + sizeof(acctPass), '\0');
                    } else {
                        acctMessage = r.error;
                    }
                }
                ImGui::EndDisabled();

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

            if (ImGui::Button("Save & apply", ImVec2(180, 46))) { applyAccountSettings(false); Toast("Settings saved"); }
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
            for (int i = 0; i < 3; ++i) {
                if (i) ImGui::SameLine();
                if (ImGui::RadioButton(kThemeNames[i], g_themeIndex == i)) {
                    g_themeIndex = i; DarkTheme(); SavePrefs();
                }
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
                if (ImGui::Button(current ? "*" : " ", ImVec2(34, 34))) {
                    ApplyAccent(i);
                    SavePrefs();
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
                Toast("Preferences reset");
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

        // ---- Toast notification (bottom-right, auto-fading) ----
        if (SDL_GetTicks() < g_toastUntil && !g_toastMsg.empty()) {
            float remain = (float)(g_toastUntil - SDL_GetTicks());
            float alpha = remain > 400.0f ? 1.0f : remain / 400.0f;   // fade out
            ImVec2 wp = vp->WorkPos, ws = vp->WorkSize;
            ImGui::SetNextWindowPos(ImVec2(wp.x + ws.x - 18, wp.y + ws.y - 18), ImGuiCond_Always, ImVec2(1, 1));
            ImGui::SetNextWindowBgAlpha(0.92f * alpha);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.97f, 1.0f, alpha));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(kAccent.x, kAccent.y, kAccent.z, alpha));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
            if (ImGui::Begin("##toast", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted(g_toastMsg.c_str());
            }
            ImGui::End();
            ImGui::PopStyleVar();
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
