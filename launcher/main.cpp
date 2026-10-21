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
#include "Splash.hpp"   // branded loading frame so startup isn't a black screen
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Platform/Achievements.hpp"
#include "okay/Platform/Friends.hpp"
#include "okay/Platform/Workshop.hpp"
#include "okay/Platform/Messages.hpp"
#include "okay/Platform/Forums.hpp"
#include "okay/Platform/Servers.hpp"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "RobotoFont.h"     // embedded Roboto Medium (Apache 2.0) — shared with the editor

// stb_image for optional per-game cover art. STATIC keeps its symbols file-local
// (the launcher doesn't link the engine, so there's no clash, but this is tidy).
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "stb_image.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#  include <shellapi.h>   // Shell_NotifyIcon (system-tray icon)
#  include <SDL_syswm.h>  // HWND of the SDL window, for the tray
#else
#  include <unistd.h>
#  include <sys/wait.h>
#  include <sys/file.h>   // flock (single-instance lock)
#  include <fcntl.h>
#  include <csignal>
#endif

#if defined(_WIN32)
// Prefer the dedicated GPU on hybrid-graphics laptops (NVIDIA Optimus / AMD
// switchable). Exporting these symbols tells the drivers to run this process on
// the discrete NVIDIA/AMD GPU instead of the slower integrated one. Must live at
// global scope with C linkage and be exported from the .exe.
extern "C" {
    __declspec(dllexport) unsigned long NvOptimusEnablement = 1;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
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
std::vector<std::string> g_libraryDirs;  // extra folders to scan for games (persisted)
SDL_Renderer* g_renderer = nullptr;      // set in main(); used to upload cover-art textures

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

// Rich update progress, so the updater can show a real dialog (from/to version,
// stepped progress, a live download bar in MB, elapsed time, and the changelog)
// instead of a one-line message. Written by the worker, read by the UI.
SDL_atomic_t g_upStepIdx;        // current step index (0-based)
SDL_atomic_t g_upStepTotal;      // total steps for this run
SDL_atomic_t g_upBytesNow;       // bytes downloaded for the current file
SDL_atomic_t g_upBytesTotal;     // total bytes for the current file (0 = unknown)
SDL_atomic_t g_upStartMs;        // SDL_GetTicks() when the run started
std::string  g_upFromVer;        // installed version at run start   (g_upMutex)
std::string  g_upToVer;          // target/published version         (g_upMutex)
std::string  g_upStepName;       // human label for the current step (g_upMutex)
std::string  g_upChangelog;      // release notes for g_upToVer       (g_upMutex)
bool         g_upModalOpen = false;   // UI: show the update dialog (main thread only)

void SetState(UpState s) { SDL_AtomicSet(&g_upState, (int)s); }
UpState GetState() { return (UpState)SDL_AtomicGet(&g_upState); }

// Publish the current update step (label + which step of how many).
void SetUpStep(const std::string& name, int idx, int total) {
    if (g_upMutex) SDL_LockMutex(g_upMutex);
    g_upStepName = name;
    if (g_upMutex) SDL_UnlockMutex(g_upMutex);
    SDL_AtomicSet(&g_upStepIdx, idx);
    SDL_AtomicSet(&g_upStepTotal, total);
    SDL_AtomicSet(&g_upBytesNow, 0);
    SDL_AtomicSet(&g_upBytesTotal, 0);
}
std::string GetUpStep() {
    std::string s; if (g_upMutex) SDL_LockMutex(g_upMutex);
    s = g_upStepName; if (g_upMutex) SDL_UnlockMutex(g_upMutex); return s;
}
void SetUpVers(const std::string& from, const std::string& to) {
    if (g_upMutex) SDL_LockMutex(g_upMutex);
    g_upFromVer = from; g_upToVer = to;
    if (g_upMutex) SDL_UnlockMutex(g_upMutex);
}
void GetUpVers(std::string& from, std::string& to) {
    if (g_upMutex) SDL_LockMutex(g_upMutex);
    from = g_upFromVer; to = g_upToVer;
    if (g_upMutex) SDL_UnlockMutex(g_upMutex);
}
void SetUpChangelog(const std::string& c) {
    if (g_upMutex) SDL_LockMutex(g_upMutex);
    g_upChangelog = c;
    if (g_upMutex) SDL_UnlockMutex(g_upMutex);
}
std::string GetUpChangelog() {
    std::string c; if (g_upMutex) SDL_LockMutex(g_upMutex);
    c = g_upChangelog; if (g_upMutex) SDL_UnlockMutex(g_upMutex); return c;
}

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

// Latest release notes, fetched by the update worker ("" until a check runs).
// Bullet lines separated by '\n'; shown on the Create tab's What's-new card.
std::string g_newsLines;
std::string g_newsVersion;
void SetNews(const std::string& ver, const std::string& lines) {
    if (g_upMutex) SDL_LockMutex(g_upMutex);
    g_newsVersion = ver; g_newsLines = lines;
    if (g_upMutex) SDL_UnlockMutex(g_upMutex);
}
void GetNews(std::string& ver, std::string& lines) {
    if (g_upMutex) SDL_LockMutex(g_upMutex);
    ver = g_newsVersion; lines = g_newsLines;
    if (g_upMutex) SDL_UnlockMutex(g_upMutex);
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

// ---- Download manager ------------------------------------------------------
// A background download queue the Marketplace "Get" flow feeds into: each item
// downloads to <launcher>/downloads/ via a hidden curl child process, and the
// Downloads tab shows live progress, cancel, open-folder and (for .zip) an
// extract-to-Community action. SDL threads again (no std::thread on MinGW).
//
// A download runs on its own SDL thread; the worker owns a child-process handle
// so a cancel can terminate the in-flight curl. All shared fields are guarded
// by g_dlMutex; progress is read by polling the growing .part file on disk.

// A spawned, hidden child process we can poll and kill.
struct Proc {
#if defined(_WIN32)
    void* h = nullptr;                 // HANDLE
    bool valid() const { return h != nullptr; }
#else
    int pid = -1;
    bool valid() const { return pid > 0; }
#endif
};

Proc SpawnHidden(const std::string& cmd) {
    Proc p;
#if defined(_WIN32)
    std::string full = "cmd /c " + cmd;
    std::vector<char> buf(full.begin(), full.end()); buf.push_back('\0');
    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    if (CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        p.h = pi.hProcess;
    }
#else
    pid_t pid = fork();
    if (pid == 0) {                    // child: exec the command via /bin/sh
        execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)nullptr);
        _exit(127);
    } else if (pid > 0) {
        p.pid = pid;
    }
#endif
    return p;
}

// Returns true while still running; when it has exited, returns false and sets
// *exitCode to its exit status.
bool ProcRunning(Proc* p, int* exitCode) {
    if (!p->valid()) { if (exitCode) *exitCode = -1; return false; }
#if defined(_WIN32)
    DWORD code = 0;
    if (GetExitCodeProcess((HANDLE)p->h, &code)) {
        if (code == STILL_ACTIVE) return true;
        if (exitCode) *exitCode = (int)code;
        CloseHandle((HANDLE)p->h); p->h = nullptr;
        return false;
    }
    return false;
#else
    int st = 0;
    pid_t r = waitpid(p->pid, &st, WNOHANG);
    if (r == 0) return true;           // still running
    if (exitCode) *exitCode = (r > 0 && WIFEXITED(st)) ? WEXITSTATUS(st) : -1;
    p->pid = -1;
    return false;
#endif
}

void ProcKill(Proc* p) {
    if (!p->valid()) return;
#if defined(_WIN32)
    TerminateProcess((HANDLE)p->h, 1);
    CloseHandle((HANDLE)p->h); p->h = nullptr;
#else
    kill(p->pid, SIGTERM);
    int st = 0; waitpid(p->pid, &st, 0);
    p->pid = -1;
#endif
}

// A HEAD request for the Content-Length (0 if unknown / chunked / failed). Runs
// hidden and parses the dumped response headers (last Content-Length wins, so a
// redirect chain reports the final resource's size).
long long HeadSize(const std::string& url) {
    std::error_code ec;
    fs::path hdr = fs::temp_directory_path(ec) / ("okay_dlhead_" + std::to_string(SDL_GetTicks()) + ".txt");
#if defined(_WIN32)
    std::string devnull = "NUL";
#else
    std::string devnull = "/dev/null";
#endif
    std::string cmd = "curl -sIL -A \"OkaySpace-Launcher\" -o \"" + devnull +
                      "\" -D \"" + hdr.string() + "\" \"" + url + "\"";
    RunCmd(cmd, true);
    long long total = 0;
    std::ifstream f(hdr);
    std::string line;
    while (std::getline(f, line)) {
        std::string low = line;
        for (char& c : low) c = (char)std::tolower((unsigned char)c);
        if (low.rfind("content-length:", 0) == 0) {
            try { total = std::stoll(line.substr(15)); } catch (...) {}
        }
    }
    f.close(); fs::remove(hdr, ec);
    return total;
}

struct DlItem {
    int         id = 0;
    std::string url, title, dest, part;
    long long   total = 0, received = 0;
    int         status = 0;            // 0 queued, 1 downloading, 2 done, 3 failed, 4 canceled
    bool        cancelReq = false;
    Proc        proc;
    SDL_Thread* th = nullptr;
};

std::vector<std::shared_ptr<DlItem>> g_dls;
SDL_mutex* g_dlMutex = nullptr;
int g_dlNextId = 1;

long long FileSizeSafe(const std::string& p) {
    std::error_code ec; auto s = fs::file_size(p, ec); return ec ? 0 : (long long)s;
}

int DlWorker(void* data) {
    DlItem* d = static_cast<DlItem*>(data);
    auto set = [&](std::function<void()> fn){ SDL_LockMutex(g_dlMutex); fn(); SDL_UnlockMutex(g_dlMutex); };
    set([&]{ d->status = 1; });
    long long total = HeadSize(d->url);
    set([&]{ d->total = total; });

    std::error_code ec; fs::remove(d->part, ec);
    std::string cmd = "curl -L -s -f -A \"OkaySpace-Launcher\" -o \"" + d->part + "\" \"" + d->url + "\"";
    Proc p = SpawnHidden(cmd);
    if (!p.valid()) { set([&]{ d->status = 3; }); return 0; }
    set([&]{ d->proc = p; });

    int code = 0;
    while (ProcRunning(&p, &code)) {
        bool cancel = false;
        SDL_LockMutex(g_dlMutex); cancel = d->cancelReq; SDL_UnlockMutex(g_dlMutex);
        if (cancel) {
            ProcKill(&p);
            fs::remove(d->part, ec);
            set([&]{ d->status = 4; d->proc = Proc{}; });
            return 0;
        }
        long long got = FileSizeSafe(d->part);
        set([&]{ d->received = got; });
        SDL_Delay(200);
    }
    long long got = FileSizeSafe(d->part);
    if (code == 0 && got > 0) {
        fs::remove(d->dest, ec);
        fs::rename(d->part, d->dest, ec);
        set([&]{ d->received = got; if (d->total <= 0) d->total = got; d->status = ec ? 3 : 2; d->proc = Proc{}; });
    } else {
        fs::remove(d->part, ec);
        set([&]{ d->status = 3; d->proc = Proc{}; });
    }
    return 0;
}

// Pick a filename from a URL (its last path segment, query stripped).
std::string FileNameFromUrl(const std::string& url, const std::string& fallback) {
    std::string u = url;
    auto q = u.find('?'); if (q != std::string::npos) u = u.substr(0, q);
    auto h = u.find('#'); if (h != std::string::npos) u = u.substr(0, h);
    while (!u.empty() && u.back() == '/') u.pop_back();
    auto s = u.find_last_of('/');
    std::string name = (s == std::string::npos) ? u : u.substr(s + 1);
    if (name.empty()) name = fallback;
    std::string out;                           // sanitize for the filesystem
    for (char c : name) out += (std::isalnum((unsigned char)c) || c=='.' || c=='-' || c=='_') ? c : '_';
    if (out.empty()) out = "download";
    return out;
}

// Queue a download; returns its id (0 if the manager isn't ready).
int StartDownload(const std::string& url, const std::string& title) {
    if (!g_dlMutex || url.empty()) return 0;
    std::error_code ec;
    fs::path dir = fs::path(g_exeDir) / "downloads";
    fs::create_directories(dir, ec);
    std::string base = FileNameFromUrl(url, title.empty() ? "download" : title);
    fs::path dest = dir / base;
    for (int n = 1; fs::exists(dest, ec); ++n)  // avoid clobbering an existing file
        dest = dir / (std::to_string(n) + "_" + base);

    auto d = std::make_shared<DlItem>();
    SDL_LockMutex(g_dlMutex);
    d->id = g_dlNextId++;
    d->url = url; d->title = title.empty() ? base : title;
    d->dest = dest.string(); d->part = dest.string() + ".part";
    g_dls.push_back(d);
    SDL_UnlockMutex(g_dlMutex);
    d->th = SDL_CreateThread(DlWorker, "okay-dl", d.get());
    return d->id;
}

// ---- Git for projects (clone / pull / commit+push / publish) ---------------
// Git can be slow (clone/push), so it runs on a background thread and reports a
// result the main loop turns into a toast. Requires `git` on the user's PATH.
SDL_mutex*  g_gitMutex = nullptr;
bool        g_gitBusy = false;
bool        g_gitResultReady = false;
std::string g_gitResultMsg;
int         g_gitResultKind = 0;      // Toast kind: 1 = ok, 2 = error
bool        g_gitWantRescan = false;  // re-list projects after a clone/pull

static bool GitAvailable() {
    static int cached = -1;
    if (cached < 0)
        cached = (RunCmd(
#if defined(_WIN32)
            "git --version >nul 2>&1"
#else
            "git --version >/dev/null 2>&1"
#endif
            , true) == 0) ? 1 : 0;
    return cached == 1;
}

struct GitJob { std::vector<std::string> cmds; std::string okMsg, failMsg; bool rescan = false; };

static int GitWorker(void* data) {
    GitJob* job = (GitJob*)data;
    int rc = 0;
    for (std::size_t i = 0; i < job->cmds.size(); ++i) {
        int c = RunCmd(job->cmds[i], true);
        if (i + 1 == job->cmds.size()) rc = c;   // the last command decides success
    }
    SDL_LockMutex(g_gitMutex);
    g_gitBusy = false;
    g_gitResultReady = true;
    g_gitResultMsg = (rc == 0) ? job->okMsg : job->failMsg;
    g_gitResultKind = (rc == 0) ? 1 : 2;
    if (rc == 0 && job->rescan) g_gitWantRescan = true;
    SDL_UnlockMutex(g_gitMutex);
    delete job;
    return rc;
}

// Start a git job in the background (ignored if one is already running).
static bool StartGit(GitJob* job) {
    if (!g_gitMutex) { delete job; return false; }
    SDL_LockMutex(g_gitMutex);
    if (g_gitBusy) { SDL_UnlockMutex(g_gitMutex); delete job; return false; }
    g_gitBusy = true;
    SDL_UnlockMutex(g_gitMutex);
    SDL_Thread* t = SDL_CreateThread(GitWorker, "okay-git", job);
    if (t) SDL_DetachThread(t); else { SDL_LockMutex(g_gitMutex); g_gitBusy = false; SDL_UnlockMutex(g_gitMutex); delete job; return false; }
    return true;
}
static std::string GitQ(const std::string& s) { return "\"" + s + "\""; }
static std::string GitC(const std::string& dir) { return "git -C " + GitQ(dir) + " "; }

// Extract a .zip into destDir, best-effort, using whatever's on the system.
bool ExtractZip(const std::string& zip, const std::string& destDir) {
    std::error_code ec; fs::create_directories(destDir, ec);
#if defined(_WIN32)
    // Windows 10+ ships bsdtar; fall back to PowerShell's Expand-Archive.
    std::string c1 = "tar -xf \"" + zip + "\" -C \"" + destDir + "\"";
    if (RunCmd(c1, true) == 0) return true;
    std::string c2 = "powershell -NoProfile -Command \"Expand-Archive -Force -LiteralPath '" +
                     zip + "' -DestinationPath '" + destDir + "'\"";
    return RunCmd(c2, true) == 0;
#else
    std::string c1 = "unzip -o \"" + zip + "\" -d \"" + destDir + "\" >/dev/null 2>&1";
    if (RunCmd(c1, true) == 0) return true;
    std::string c2 = "tar -xf \"" + zip + "\" -C \"" + destDir + "\" 2>/dev/null";
    return RunCmd(c2, true) == 0;
#endif
}

// Count of downloads still in flight (for the nav badge).
int ActiveDownloads() {
    if (!g_dlMutex) return 0;
    int n = 0;
    SDL_LockMutex(g_dlMutex);
    for (auto& d : g_dls) if (d->status == 0 || d->status == 1) ++n;
    SDL_UnlockMutex(g_dlMutex);
    return n;
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
// Best-effort remote size (Content-Length) via a HEAD request, so the updater's
// progress bar can show a percentage. Returns 0 when unknown (indeterminate).
long long RemoteSize(const std::string& url) {
    std::error_code ec;
    fs::path hf = fs::temp_directory_path(ec) / ("okay_head_" + std::to_string(SDL_GetTicks()) + ".txt");
    std::string u = BustCache(url);
#if defined(_WIN32)
    std::string c = "curl -sIL \"" + u + "\" -o \"" + hf.string() + "\"";
#else
    std::string c = "curl -sIL \"" + u + "\" -o \"" + hf.string() + "\" 2>/dev/null";
#endif
    if (RunCmd(c, true) != 0) { fs::remove(hf, ec); return 0; }
    std::ifstream f(hf); std::string line; long long sz = 0;
    while (std::getline(f, line)) {
        // Case-insensitive "content-length:"; keep the LAST one (after redirects).
        std::string low = line;
        for (auto& ch : low) ch = (char)std::tolower((unsigned char)ch);
        auto p = low.find("content-length:");
        if (p != std::string::npos) {
            long long v = 0; bool any = false;
            for (std::size_t i = p + 15; i < line.size(); ++i) {
                char d = line[i];
                if (d >= '0' && d <= '9') { v = v * 10 + (d - '0'); any = true; }
                else if (any) break;
            }
            if (any) sz = v;
        }
    }
    f.close(); fs::remove(hf, ec);
    return sz;
}

// Download helper that runs curl on a short-lived thread while the caller polls
// the growing output file, publishing byte progress into the update globals so
// the dialog can show a live MB / percentage bar.
struct DlMonitor { std::string url, out; SDL_atomic_t done; SDL_atomic_t ok; };
int DlMonitorThread(void* p) {
    DlMonitor* d = (DlMonitor*)p;
    bool r = Download(d->url, d->out);
    SDL_AtomicSet(&d->ok, r ? 1 : 0);
    SDL_AtomicSet(&d->done, 1);
    return 0;
}
bool DownloadProgress(const std::string& url, const std::string& out) {
    SDL_AtomicSet(&g_upBytesNow, 0);
    SDL_AtomicSet(&g_upBytesTotal, (int)RemoteSize(url));   // 0 if unknown
    DlMonitor d; d.url = url; d.out = out;
    SDL_AtomicSet(&d.done, 0); SDL_AtomicSet(&d.ok, 0);
    SDL_Thread* th = SDL_CreateThread(DlMonitorThread, "okay-dlp", &d);
    while (SDL_AtomicGet(&d.done) == 0) {
        std::error_code ec; auto sz = fs::file_size(out, ec);
        SDL_AtomicSet(&g_upBytesNow, ec ? 0 : (int)(long long)sz);
        SDL_Delay(90);
    }
    SDL_WaitThread(th, nullptr);
    std::error_code ec; auto sz = fs::file_size(out, ec);
    SDL_AtomicSet(&g_upBytesNow, ec ? 0 : (int)(long long)sz);
    return SDL_AtomicGet(&d.ok) == 1;
}

bool ReplaceFile(const std::string& url, const fs::path& dest, bool inUse,
                 bool progress = false) {
    fs::path tmp = dest; tmp += ".new";
    bool got = progress ? DownloadProgress(url, tmp.string())
                        : Download(url, tmp.string());
    if (!got) return false;
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

// Fetch the latest GitHub release's notes and stash the first few bullet
// lines for the Create tab's What's-new card. Best-effort: any failure just
// leaves the built-in list showing. Runs on the update worker thread.
void FetchReleaseNews() {
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec);
    if (ec) return;
    fs::path jf = tmp / "okayspace_release.json";
    if (!Download("https://api.github.com/repos/kingimann/OkaySpaceGameEngine/releases/latest",
                  jf.string())) return;
    std::ifstream f(jf, std::ios::binary);
    if (!f) return;
    std::string json((std::istreambuf_iterator<char>(f)), {});
    f.close(); fs::remove(jf, ec);
    // Pull a top-level string field out of the (flat enough) release JSON.
    auto field = [&](const char* name) -> std::string {
        std::string key = std::string("\"") + name + "\":\"";
        auto p = json.find(key);
        if (p == std::string::npos) return {};
        p += key.size();
        std::string out;
        while (p < json.size() && json[p] != '"') {
            char c2 = json[p++];
            if (c2 == '\\' && p < json.size()) {          // unescape \n \" \\ \r \t
                char e = json[p++];
                if (e == 'n') out += '\n';
                else if (e == 'r') { /* skip */ }
                else if (e == 't') out += ' ';
                else if (e == 'u') { p += 4; out += '?'; }
                else out += e;
            } else out += c2;
        }
        return out;
    };
    std::string tag  = field("tag_name");
    std::string body = field("body");
    if (body.empty()) return;
    // Keep the first handful of bullet lines ("- ..." / "* ..."), stripped.
    std::string lines; int kept = 0;
    std::size_t start = 0;
    while (start < body.size() && kept < 6) {
        std::size_t end = body.find('\n', start);
        if (end == std::string::npos) end = body.size();
        std::string ln = body.substr(start, end - start);
        start = end + 1;
        std::size_t a = ln.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        if (ln[a] != '-' && ln[a] != '*') continue;
        std::size_t b = ln.find_first_not_of(" \t", a + 1);
        if (b == std::string::npos) continue;
        lines += ln.substr(b) + "\n";
        ++kept;
    }
    if (kept > 0) SetNews(tag, lines);
}

// Worker: check the published version and, if newer, install it. Runs on a
// background thread so the launcher stays responsive while ~35 MB downloads.
int RunUpdateCheck(void*) {
    SDL_AtomicSet(&g_upStartMs, (int)SDL_GetTicks());
    std::string local = LocalVersion();
    SetUpVers(local, "");
    SetUpStep("Contacting GitHub", 0, 1);
    SetState(Up_Checking);
    SetUpMsg("Checking for updates...");
    FetchReleaseNews();   // refresh the What's-new card while we're online anyway
    // Mirror the fetched release notes into the update dialog's changelog.
    { std::string nv, nl;
      if (g_upMutex) SDL_LockMutex(g_upMutex);
      nv = g_newsVersion; nl = g_newsLines;
      if (g_upMutex) SDL_UnlockMutex(g_upMutex);
      if (!nl.empty()) SetUpChangelog(nl); }
    std::error_code ec;
    fs::path tmp = fs::temp_directory_path(ec);
    fs::path vf = tmp / "okayspace_launcher_ver.txt";
    if (!Download(std::string(kRawBase) + "VERSION.txt", vf.string())) {
        SetUpMsg("Couldn't reach GitHub (offline?).");
        SetUpStep("Couldn't reach the update server", 0, 1);
        SetState(Up_Failed);
        return 0;
    }
    std::string latest;
    { std::ifstream f(vf); std::getline(f, latest); }
    while (!latest.empty() && (latest.back() == '\r' || latest.back() == '\n' || latest.back() == ' '))
        latest.pop_back();
    fs::remove(vf, ec);
    g_upLatest = latest;

    fs::path dir(g_exeDir);
    bool newer = !latest.empty() && CompareVersions(local, latest) < 0;   // installed < published
    SetUpVers(local, latest);

    // Only ever download a STRICTLY newer published version. Never "self-heal" by
    // pulling a not-newer (older or equal) build — that used to downgrade the engine
    // to whatever stale binary sits in the repo's dist/ folder (the "it downloads
    // 2.97" bug). If we're current or ahead, do nothing.
    if (!newer) {
        SetUpMsg("Up to date (v" + local + ").");
        SetUpStep("You're on the latest version", 1, 1);
        SetState(Up_UpToDate);
        return 0;
    }

    SetState(Up_Downloading);
    bool ok = true;
    // Three download steps: engine, player, launcher.
    const int kSteps = 3;
    {
        // Write each exe back to wherever it currently lives (e.g. Tools/), so an
        // update preserves the organized layout instead of scattering exes to the
        // top level. Falls back to next-to-launcher if not found (fresh install).
        auto destOf = [&](const char* name) -> fs::path {
            std::string found = FindExe({name});
            return found.empty() ? (dir / name) : fs::path(found);
        };
        SetUpMsg("Downloading engine v" + latest + "...");
        SetUpStep("Downloading editor (OkayEngine)", 0, kSteps);
        ok = ReplaceFile(std::string(kRawBase) + "OkayEngine.exe",
                         destOf("OkayEngine.exe"), false, /*progress=*/true) && ok;
        SetUpMsg("Downloading player runtime v" + latest + "...");
        SetUpStep("Downloading game runtime (OkaySpacePlayer)", 1, kSteps);
        ok = ReplaceFile(std::string(kRawBase) + "OkaySpacePlayer.exe",
                         destOf("OkaySpacePlayer.exe"), false, /*progress=*/true) && ok;
    }
    // Only self-update the launcher on a genuine version bump.
    bool launcherOk = false;
    if (newer) {
        SetUpMsg("Updating launcher...");
        SetUpStep("Updating launcher (OkaySpace)", 2, kSteps);
        launcherOk = !g_selfPath.empty() &&
            ReplaceFile(std::string(kRawBase) + "OkaySpace.exe",
                        fs::path(g_selfPath), true, /*progress=*/true);
    }

    if (ok) {
        std::ofstream(dir / "VERSION.txt") << latest << "\n";
        g_upRelaunchNeeded = launcherOk;
        SetUpStep(launcherOk ? "Update complete — restart to finish"
                             : "Update complete", kSteps, kSteps);
        SetUpMsg(launcherOk
            ? "Updated to v" + latest + " — restart the launcher to finish."
            : (newer ? "Updated engine & player to v" + latest + "."
                     : "Downloaded the missing runtime (v" + local + ")."));
        SetState(Up_Updated);
    } else {
        SetUpMsg("Download failed — check your internet connection.");
        SetUpStep("Download failed — check your connection and retry", 0, kSteps);
        SetState(Up_Failed);
    }
    return 0;
}

// Kick off an update check on a background thread (no-op if one is running).
void StartUpdateCheck(bool showDialog = true) {
    UpState s = GetState();
    if (s == Up_Checking || s == Up_Downloading) return;
    if (showDialog) g_upModalOpen = true;   // UI opens the rich update dialog
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
void LaunchEditor(const std::string& exe, const std::string& tmpl = "",
                  const std::string& newProjectDir = "", const std::string& openScene = "") {
    if (exe.empty()) return;
    std::string args = "--launcher " + std::to_string(CurrentPid());
    if (!tmpl.empty()) args += " --template \"" + tmpl + "\"";
    // Project creation lives here in the launcher: the editor receives the
    // target folder and builds <folder>/Assets + the template scene headlessly.
    if (!newProjectDir.empty()) args += " --new-project \"" + newProjectDir + "\"";
    if (!openScene.empty()) args += " --open \"" + openScene + "\"";   // reopen a project
#if defined(_WIN32)
    std::string cmd = "start \"\" \"" + exe + "\" " + args;
#else
    std::string cmd = "\"" + exe + "\" " + args + " >/dev/null 2>&1 &";
#endif
    RunCmd(cmd, false);
}

void Toast(const std::string& m, int kind);   // defined below; used by CreateProject

// The Create tab asks for a rescan + highlight after making a project.
bool        g_projectsNeedRescan = false;   // force "Your projects" to re-list
std::string g_projectJustMade;              // name to highlight in the list

// Make a real project on disk, in the LAUNCHER, so clicking a template actually
// creates something (the folder + Assets exist the instant you click) instead of
// only handing a request to the editor. Returns the created folder, or "" on
// failure. When the editor is present it then fills in the template scene (it
// adopts an empty Assets folder as-is); when it isn't, we still write a minimal
// but valid starting scene so the project opens later.
std::string CreateProject(const std::string& location, const std::string& rawName,
                          const std::string& templateTitle, const std::string& editorExe) {
    std::error_code ec;
    // Sanitize the name into a filesystem-safe folder name.
    std::string name = rawName.empty() ? "MyGame" : rawName;
    for (char& ch : name)
        if (ch == '/' || ch == '\\' || ch == '"' || ch == ':' || ch == '*' ||
            ch == '?' || ch == '<' || ch == '>' || ch == '|') ch = '-';
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    if (name.empty()) name = "MyGame";

    fs::path base = location.empty() ? fs::path(".") : fs::path(location);
    fs::create_directories(base, ec);

    // Uniquify: a folder counts as "used" if it already holds a non-empty Assets
    // (a real project) or non-project clutter. Pick Name, then Name-2, Name-3, ...
    auto used = [&](const fs::path& r) {
        if (!fs::exists(r, ec)) return false;
        if (!fs::is_directory(r, ec)) return true;
        fs::path assets = r / "Assets";
        if (!fs::exists(assets, ec)) return !fs::is_empty(r, ec);
        return !fs::is_empty(assets, ec);
    };
    std::string finalName = name;
    fs::path root = base / finalName;
    for (int n = 2; used(root) && n < 1000; ++n) { finalName = name + "-" + std::to_string(n); root = base / finalName; }

    if (!fs::create_directories(root / "Assets", ec) && ec) {
        Toast("Couldn't create project folder: " + ec.message(), 2);
        return "";
    }

    if (!editorExe.empty()) {
        // Editor present: it adopts the empty Assets folder and writes the real
        // template scene there. (Single-instance: if an editor is already open,
        // the launch is handed off to it.)
        LaunchEditor(editorExe, templateTitle, root.string());
    } else {
        // No editor installed yet: still leave a real, openable project behind so
        // the folder isn't an empty shell — a minimal valid scene file.
        std::ofstream o((root / "Assets" / (finalName + ".okayscene")).string(), std::ios::binary);
        o << "name \"" << finalName << "\"\n";
        Toast("Created " + finalName + " (install the editor to open it)", 1);
    }
    g_projectsNeedRescan = true;
    g_projectJustMade = finalName;
    return root.string();
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
    for (const auto& d : g_libraryDirs) scan(fs::path(d));   // user-added libraries
    return out;
}

// Scan the same folders for HTML5 / JavaScript web games — a folder containing
// an index.html (e.g. an Emscripten/HTML export, or any browser game), or a
// loose *.html file. Returns the path to the .html to open; playing one just
// opens it in the system's default browser (OpenExternal).
std::vector<fs::path> FindWebGames() {
    std::vector<fs::path> out;
    std::error_code ec;
    auto scan = [&](const fs::path& dir) {
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;
        for (auto it = fs::recursive_directory_iterator(dir, ec);
             it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string ext;
            for (char c : it->path().extension().string()) ext += (char)std::tolower((unsigned char)c);
            if (ext != ".html" && ext != ".htm") continue;
            std::string stem;
            for (char c : it->path().stem().string()) stem += (char)std::tolower((unsigned char)c);
            // Prefer index.html per folder; a loose single .html also counts.
            if (stem == "index") out.push_back(it->path());
            else {
                // Only include a non-index .html if its folder has no index.html.
                std::error_code iec;
                if (!fs::exists(it->path().parent_path() / "index.html", iec) &&
                    !fs::exists(it->path().parent_path() / "index.htm", iec))
                    out.push_back(it->path());
            }
        }
    };
    fs::path base(g_exeDir);
    scan(base / "games");
    scan(base / "community");
    scan(base / "Projects");
    scan(base / "webgames");
    for (const auto& d : g_libraryDirs) scan(fs::path(d));
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// A scene is a *built game* (vs a raw dev scene) when File > Build Game exported
// it — that writes a game.okayconfig next to the scene. The Play tab shows only
// built games; loose .okayscene dev files are hidden there.
bool IsBuiltGame(const fs::path& scene) {
    std::error_code ec;
    return fs::exists(scene.parent_path() / "game.okayconfig", ec);
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

// Friends-tab personalization, persisted in launcher.cfg: pinned (favorite)
// friends float to the top, and a private per-friend nickname overrides the
// name you see (Discord/Steam-style; only you see it).
std::set<std::string>              g_favFriends;   // favorited friend uids
std::map<std::string,std::string>  g_friendNicks;  // uid -> private nickname
// Chat conversation state (client-side, persisted): pinned float to the top,
// muted never raise an unread badge, archived hide until you show archived.
std::set<std::string>              g_pinnedChats;
std::set<std::string>              g_mutedChats;
std::set<std::string>              g_archivedChats;
bool                               g_minimizeToTray = false;  // opt-in Steam-style tray (off by default: a normal minimize always restores from the taskbar)

// Shared accent so UI code can match the theme.
// Mutable so the Appearance setting can recolor the UI live (DarkTheme() reads
// these, so changing them and re-applying restyles everything).
ImVec4 kAccent(0.26f, 0.59f, 0.98f, 1.0f);     // matches the editor's default accent
ImVec4 kAccentDim(0.19f, 0.43f, 0.72f, 1.0f);

// Selectable accent colors (name, main, dim) — mirror the editor's accent presets.
struct AccentPreset { const char* name; ImVec4 col; ImVec4 dim; };
const AccentPreset kAccentPresets[] = {
    {"Unity", ImVec4(0.235f, 0.475f, 0.800f, 1), ImVec4(0.172f, 0.360f, 0.600f, 1)},  // Unity Editor selection blue (default)
    {"Blue",   ImVec4(0.26f, 0.59f, 0.98f, 1), ImVec4(0.19f, 0.43f, 0.72f, 1)},
    {"Teal",   ImVec4(0.18f, 0.78f, 0.74f, 1), ImVec4(0.13f, 0.56f, 0.53f, 1)},
    {"Violet", ImVec4(0.56f, 0.46f, 0.96f, 1), ImVec4(0.40f, 0.33f, 0.70f, 1)},
    {"Green",  ImVec4(0.36f, 0.80f, 0.46f, 1), ImVec4(0.26f, 0.58f, 0.33f, 1)},
    {"Amber",  ImVec4(0.95f, 0.66f, 0.26f, 1), ImVec4(0.70f, 0.48f, 0.19f, 1)},
    {"Rose",   ImVec4(0.96f, 0.42f, 0.56f, 1), ImVec4(0.70f, 0.30f, 0.41f, 1)},
};
int g_accentIndex = 0;            // current accent preset (persisted)
bool g_updateOnLaunch = false;    // check for updates at startup (persisted)
// ---- Customizable loading screen (persisted) ----
std::string g_splashLogo;                         // optional PNG shown instead of the cube
float g_splashBgTop[3] = {0.094f, 0.094f, 0.106f};// backdrop gradient (top)
float g_splashBgBot[3] = {0.051f, 0.051f, 0.059f};// backdrop gradient (bottom)
bool  g_splashAccentOverride = false;             // use a custom accent for the splash
float g_splashAccent[3] = {0.235f, 0.475f, 0.800f};
bool  g_splashShowBar = true;                     // show the progress bar
SDL_Texture* g_splashLogoTex = nullptr;           // loaded at startup from g_splashLogo
int g_themeIndex = 0;             // 0 Dark, 1 Midnight, 2 Light (persisted)
float g_uiScale = 1.0f;           // font/UI scale for high-DPI (persisted)
bool g_navCollapsed = false;      // left sidebar collapsed to an icon-only rail (persisted)
bool g_webNeedRescan = false;     // a dropped web game asks the Play tab to rescan
int g_winW = 1160, g_winH = 752;  // last window size (persisted) — roomier first-launch default
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
int g_playSort = 0;               // 0 Favorites first, 1 Name A–Z, 2 Recently played, 3 Most played
int g_playView = 0;               // 0 List rows, 1 Grid tiles
int g_lastTab = 0;                // section shown when the launcher was last closed

// Per-game play stats (persisted): how many times launched + when last played.
struct PlayStat { int count = 0; long last = 0; };
std::map<std::string, PlayStat> g_stats;
// Set by the Play tab's "Publish to Marketplace" action; the Market tab reads it
// once to prefill + open its publish form, then clears it.
std::string g_pendingPublishTitle;
bool g_openPublishForm = false;   // request the Market tab to open its publish form
int g_friendRequests = 0;   // incoming pending friend requests (nav badge), polled in the background
int  g_dmUnread = 0;        // unread direct messages (nav badge)
long g_lastDmId = 0;        // newest incoming DM id the user has seen (persisted)
long g_newestDmId = 0;      // newest incoming DM id currently on the server
int g_onlineStatus = 0;     // 0 Online, 1 Away, 2 Invisible (persisted)
bool g_sharePresence = true; // share "Playing X" with friends (persisted)

// ---- Notifications area -------------------------------------------------
// A unified notifications feed behind a bell button in the sidebar. Items are
// synthesized from live signals (friend requests, unread DMs, finished updates,
// release news) and keyed so the unread count stays stable across frames.
struct Notif {
    std::string key;      // stable identity (de-dup / mark-read)
    std::string title;
    std::string body;
    int    kind = 0;      // 0 system, 1 friend, 2 message, 3 update
    Uint32 time = 0;
    bool   read = false;
    int    gotoTab = -1;  // clicking jumps here (-1 = none)
};
std::vector<Notif> g_notifs;        // newest first
bool g_notifPanelOpen = false;      // UI: notifications popup shown

int NotifUnread() { int c = 0; for (auto& n : g_notifs) if (!n.read) ++c; return c; }

// Upsert a notification by key (new ones land at the top, unread). Returns true
// if a brand-new notification was created (so a toast can be shown once).
bool UpsertNotif(const std::string& key, const std::string& title,
                 const std::string& body, int kind, int gotoTab) {
    for (auto& n : g_notifs)
        if (n.key == key) { n.title = title; n.body = body; n.gotoTab = gotoTab; return false; }
    Notif n; n.key = key; n.title = title; n.body = body; n.kind = kind;
    n.time = SDL_GetTicks(); n.gotoTab = gotoTab; n.read = false;
    g_notifs.insert(g_notifs.begin(), n);
    if (g_notifs.size() > 50) g_notifs.pop_back();
    return true;
}
// Drop a dynamic notification whose condition no longer holds (e.g. the last
// friend request was accepted).
void RemoveNotif(const std::string& key) {
    for (auto it = g_notifs.begin(); it != g_notifs.end(); ++it)
        if (it->key == key) { g_notifs.erase(it); return; }
}

// Sync the dynamic notifications with the current app state; called once a frame.
void RefreshNotifs() {
    char b[96];
    if (g_friendRequests > 0) {
        std::snprintf(b, sizeof(b), "%d pending friend request%s",
                      g_friendRequests, g_friendRequests == 1 ? "" : "s");
        UpsertNotif("dyn:friends", "Friend requests", b, 1, 6);
    } else RemoveNotif("dyn:friends");
    if (g_dmUnread > 0) {
        std::snprintf(b, sizeof(b), "%d unread direct message%s",
                      g_dmUnread, g_dmUnread == 1 ? "" : "s");
        UpsertNotif("dyn:dms", "New messages", b, 2, 6);
    } else RemoveNotif("dyn:dms");
    if (GetState() == Up_Updated) {
        std::string f, to; GetUpVers(f, to);
        UpsertNotif("dyn:update", "Update installed",
                    to.empty() ? "OkaySpace was updated." : ("Updated to v" + to + " \xE2\x80\x94 restart to finish."),
                    3, -1);
    }
    // Release news ("what's new") once a check has fetched it.
    std::string nv, nl;
    if (g_upMutex) SDL_LockMutex(g_upMutex);
    nv = g_newsVersion; nl = g_newsLines;
    if (g_upMutex) SDL_UnlockMutex(g_upMutex);
    if (!nv.empty() && !nl.empty()) {
        std::string first = nl.substr(0, nl.find('\n'));
        UpsertNotif("news:" + nv, "What's new in v" + nv, first, 0, 0);
    }
}
// Floating Chat window (DMs as a tabbed window). The friend "Message" buttons
// request a conversation here; DrawChatWindow() consumes the request.
bool g_chatWinOpen = false;
std::string g_chatReqUid, g_chatReqName, g_chatReqAvatar;
bool g_chatReqPending = false;
void OpenChatWith(const std::string& uid, const std::string& name, const std::string& avatar) {
    g_chatReqUid = uid; g_chatReqName = name; g_chatReqAvatar = avatar;
    g_chatReqPending = true; g_chatWinOpen = true;
}
int  PlayCount(const std::string& p)  { auto it = g_stats.find(p); return it == g_stats.end() ? 0 : it->second.count; }
long LastPlayed(const std::string& p) { auto it = g_stats.find(p); return it == g_stats.end() ? 0 : it->second.last; }
// A short "2h ago" / "3d ago" from a unix time (empty if never).
std::string AgoText(long when) {
    if (when <= 0) return "";
    long s = (long)std::time(nullptr) - when; if (s < 0) s = 0;
    char b[32];
    if (s < 60)        std::snprintf(b, sizeof(b), "just now");
    else if (s < 3600) std::snprintf(b, sizeof(b), "%ldm ago", s / 60);
    else if (s < 86400)std::snprintf(b, sizeof(b), "%ldh ago", s / 3600);
    else               std::snprintf(b, sizeof(b), "%ldd ago", s / 86400);
    return b;
}

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

// ---- Collections (Steam-style library categories) ------------------------
// name -> game paths. Persisted; names can't contain '|' (the save delimiter).
std::map<std::string, std::vector<std::string>> g_collections;
std::string g_collFilter;   // active collection filter on the Play tab ("" = all games)

std::vector<long> g_wishlist;   // wishlisted marketplace item ids (persisted)
bool IsWishlisted(long id) { return std::find(g_wishlist.begin(), g_wishlist.end(), id) != g_wishlist.end(); }
void ToggleWishlist(long id) {
    auto it = std::find(g_wishlist.begin(), g_wishlist.end(), id);
    if (it != g_wishlist.end()) g_wishlist.erase(it); else g_wishlist.push_back(id);
}

bool InCollection(const std::string& name, const std::string& path) {
    auto it = g_collections.find(name);
    return it != g_collections.end() &&
           std::find(it->second.begin(), it->second.end(), path) != it->second.end();
}
void ToggleInCollection(const std::string& name, const std::string& path) {
    auto& v = g_collections[name];
    auto it = std::find(v.begin(), v.end(), path);
    if (it != v.end()) v.erase(it); else v.push_back(path);
}
// Set once in main() so RecordPlayed can broadcast "playing" presence/activity
// to friends. Null until then (and on builds with no account backend).
okay::account::AccountService* g_presenceAccount = nullptr;

// A friendly game name from a scene/project path (the file stem, Title Cased-ish).
static std::string GameNameFromPath(const std::string& p) {
    fs::path fp(p);
    std::string n = fp.stem().string();
    if (n.empty()) n = fp.filename().string();
    return n.empty() ? "a game" : n;
}

void RecordPlayed(const std::string& p) {
    auto it = std::find(g_recent.begin(), g_recent.end(), p);
    if (it != g_recent.end()) g_recent.erase(it);
    g_recent.insert(g_recent.begin(), p);
    if (g_recent.size() > 16) g_recent.resize(16);

    PlayStat& st = g_stats[p];   // bump the lifetime play count + last-played time
    st.count += 1;
    st.last = (long)std::time(nullptr);

    // Let friends see what I'm playing, and drop it in the activity feed —
    // unless I'm Invisible (status 2), in which case stay quiet.
    if (g_presenceAccount && g_presenceAccount->IsLoggedIn() && g_onlineStatus != 2 && g_sharePresence) {
        std::string name = GameNameFromPath(p);
        okay::Friends fr(*g_presenceAccount);
        fr.MarkPresence("Playing " + name);
        fr.Post("playing", name);
    }
}

// Apply the selected theme (g_themeIndex), accent (kAccent), and UI scale.
void DarkTheme() {
    const bool light = (g_themeIndex == 2);
    if (light) ImGui::StyleColorsLight(); else ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    // Utilitarian, Steam/Unity-style metrics: mostly square, dense, flat. Small
    // corner radii only (no big pill rounding), tight spacing, thin separators.
    s.WindowRounding    = 0.0f;  s.ChildRounding    = 2.0f;
    s.FrameRounding     = 2.0f;  s.GrabRounding     = 2.0f;
    s.PopupRounding     = 2.0f;  s.TabRounding      = 2.0f;
    s.ScrollbarRounding = 2.0f;
    s.WindowPadding   = ImVec2(14, 12); s.FramePadding = ImVec2(10, 7);
    s.ItemSpacing     = ImVec2(9, 8);   s.ItemInnerSpacing = ImVec2(7, 6);
    s.WindowBorderSize = 0.0f;          s.ChildBorderSize = 1.0f;
    s.FrameBorderSize  = 0.0f;          s.PopupBorderSize = 1.0f;
    s.ScrollbarSize   = 8.0f;           s.GrabMinSize = 11.0f;
    s.SeparatorTextBorderSize = 1.0f;
    s.SeparatorTextPadding = ImVec2(14, 5);
    s.TabBarBorderSize = 1.0f;
    s.DisabledAlpha = 0.55f;
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
    } else {                                              // Dark (default) = neutral charcoal
        // A true neutral dark theme (VS Code / Unity dark): charcoal-grey surfaces,
        // no blue tint. The accent colour is used ONLY for small interactive cues
        // (checkmarks, slider grabs, the active nav bar), not the panels.
        // Unity Editor "Dark" skin: neutral medium-grey panels (#383838) with
        // darker toolbars (#2D2D2D) and sunken fields (#2A2A2A), crisp light-grey
        // text, and Unity's signature blue selection/accent (the blue is what
        // keeps the grey from reading as "greyed out").
        auto lighten = [](float v) { return v + (1.0f - v) * 0.25f; };
        const ImVec4 accentHover = ImVec4(lighten(accent.x), lighten(accent.y), lighten(accent.z), 1.00f);
        c[ImGuiCol_WindowBg]         = ImVec4(0.220f, 0.220f, 0.220f, 1.00f);   // #383838 main panels
        c[ImGuiCol_ChildBg]          = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
        c[ImGuiCol_PopupBg]          = ImVec4(0.235f, 0.235f, 0.235f, 0.98f);   // #3C3C3C
        c[ImGuiCol_Border]           = ImVec4(0.000f, 0.000f, 0.000f, 0.50f);   // dark dividers (#1E1E1E look)
        c[ImGuiCol_FrameBg]          = ImVec4(0.165f, 0.165f, 0.165f, 1.00f);   // #2A2A2A input fields
        c[ImGuiCol_FrameBgHovered]   = ImVec4(0.205f, 0.205f, 0.205f, 1.00f);
        c[ImGuiCol_FrameBgActive]    = ImVec4(0.235f, 0.235f, 0.235f, 1.00f);
        c[ImGuiCol_TitleBg]          = ImVec4(0.176f, 0.176f, 0.176f, 1.00f);   // #2D2D2D
        c[ImGuiCol_TitleBgActive]    = ImVec4(0.200f, 0.200f, 0.200f, 1.00f);
        c[ImGuiCol_MenuBarBg]        = ImVec4(0.176f, 0.176f, 0.176f, 1.00f);   // #2D2D2D toolbar
        c[ImGuiCol_Header]           = accent;                                   // selected row = Unity blue
        c[ImGuiCol_HeaderHovered]    = ImVec4(1.00f, 1.00f, 1.00f, 0.055f);      // hover = subtle grey
        c[ImGuiCol_HeaderActive]     = accentHover;
        c[ImGuiCol_Button]           = ImVec4(0.345f, 0.345f, 0.345f, 1.00f);   // #585858 raised buttons
        c[ImGuiCol_ButtonHovered]    = ImVec4(0.400f, 0.400f, 0.400f, 1.00f);
        c[ImGuiCol_ButtonActive]     = accent;                                   // pressed = Unity blue
        c[ImGuiCol_CheckMark]        = ImVec4(0.900f, 0.910f, 0.930f, 1.00f);    // Unity toggle check is light
        c[ImGuiCol_SliderGrab]       = accent;
        c[ImGuiCol_SliderGrabActive] = accentHover;
        c[ImGuiCol_Separator]        = ImVec4(0.000f, 0.000f, 0.000f, 0.45f);   // dark Unity dividers
        c[ImGuiCol_SeparatorHovered] = accent;
        c[ImGuiCol_SeparatorActive]  = accent;
        c[ImGuiCol_Tab]              = ImVec4(0.176f, 0.176f, 0.176f, 1.00f);
        c[ImGuiCol_TabHovered]       = ImVec4(0.255f, 0.255f, 0.255f, 1.00f);
        c[ImGuiCol_TabActive]        = ImVec4(0.235f, 0.235f, 0.235f, 1.00f);
        c[ImGuiCol_TabUnfocused]     = ImVec4(0.165f, 0.165f, 0.165f, 1.00f);
        c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.210f, 0.210f, 0.210f, 1.00f);
        c[ImGuiCol_Text]             = ImVec4(0.824f, 0.824f, 0.827f, 1.00f);   // #D2D2D2 Unity label
        c[ImGuiCol_TextDisabled]     = ImVec4(0.500f, 0.500f, 0.505f, 1.00f);
        c[ImGuiCol_TextSelectedBg]   = ImVec4(accent.x, accent.y, accent.z, 0.45f);
        c[ImGuiCol_ScrollbarBg]      = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);   // no track column — just the thumb
        c[ImGuiCol_ScrollbarGrab]    = ImVec4(1.000f, 1.000f, 1.000f, 0.13f);   // faint until hovered (overlay style)
        c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1.000f, 1.000f, 1.000f, 0.26f);
        c[ImGuiCol_ScrollbarGrabActive]  = accent;
        c[ImGuiCol_TableHeaderBg]    = ImVec4(0.200f, 0.200f, 0.200f, 1.00f);
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

// Day bucket (YYYYMMDD in local time) for a unix timestamp — used to draw a
// date separator in chat when the day rolls over between messages.
long DayKey(long when) {
    if (when <= 0) return 0;
    std::time_t t = (std::time_t)when; std::tm* tm = std::localtime(&t);
    if (!tm) return 0;
    return (tm->tm_year + 1900) * 10000L + (tm->tm_mon + 1) * 100L + tm->tm_mday;
}

// "just now" / "5m ago" / "3h ago" / "2d ago" / "Mar 04" for a unix time.
std::string RelTime(long when) {
    if (when <= 0) return "";
    long now = (long)std::time(nullptr);
    long d = now - when;
    if (d < 0) d = 0;
    char out[32];
    if (d < 45)            std::snprintf(out, sizeof(out), "just now");
    else if (d < 3600)     std::snprintf(out, sizeof(out), "%ldm ago", d / 60);
    else if (d < 86400)    std::snprintf(out, sizeof(out), "%ldh ago", d / 3600);
    else if (d < 7*86400)  std::snprintf(out, sizeof(out), "%ldd ago", d / 86400);
    else {
        std::time_t t = (std::time_t)when; std::tm* tm = std::localtime(&t);
        if (tm) std::strftime(out, sizeof(out), "%b %d", tm); else out[0] = '\0';
    }
    return out;
}

// A centered "— Month DD, YYYY —" divider line across the chat width.
void ChatDateSeparator(long when) {
    char d[48] = "";
    std::time_t t = (std::time_t)when; std::tm* tm = std::localtime(&t);
    // "Today" / "Yesterday" for recent days, else the full date.
    long today = DayKey((long)std::time(nullptr));
    long yday  = DayKey((long)std::time(nullptr) - 86400);
    long key   = DayKey(when);
    if (key == today)      std::snprintf(d, sizeof(d), "Today");
    else if (key == yday)  std::snprintf(d, sizeof(d), "Yesterday");
    else if (tm)           std::strftime(d, sizeof(d), "%B %d, %Y", tm);
    ImGui::Dummy(ImVec2(0, 6));
    ImVec2 p = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x;
    ImU32 lc = ImGui::GetColorU32(ImGuiCol_Separator);
    float tw = ImGui::CalcTextSize(d).x, pad = 12.0f;
    float mid = p.x + avail * 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float ly = p.y + ImGui::GetTextLineHeight() * 0.5f;
    dl->AddLine(ImVec2(p.x + 4, ly), ImVec2(mid - tw * 0.5f - pad, ly), lc, 1.0f);
    dl->AddLine(ImVec2(mid + tw * 0.5f + pad, ly), ImVec2(p.x + avail - 4, ly), lc, 1.0f);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail * 0.5f - tw * 0.5f);
    ImGui::TextDisabled("%s", d);
    ImGui::Dummy(ImVec2(0, 2));
}

// ---- Emoji -----------------------------------------------------------------
// The shipped font has no emoji glyphs, so emoji are small vector drawings keyed
// by text shortcodes (Discord/Slack style). The composer's emoji picker inserts
// a shortcode; RenderChatBody draws it inline. This renders identically on every
// machine with no emoji font to ship.
struct EmojiDef { const char* code; const char* alt; int id; };
static const EmojiDef kEmojis[] = {
    {":)",     ":smile:",    0}, {":D",    ":grin:",  1}, {":(",     ":sad:",   2},
    {":heart:","<3",         3}, {":fire:", ":flame:", 4}, {":star:", ":+1:",   5},
    {":laugh:","xD",          6}, {":wow:", ":o",      7}, {":cool:", ":sunglasses:", 8},
    {":thumbsup:",":+1b:",   9},
};
static int EmojiId(const std::string& t) {
    for (const auto& e : kEmojis) if (t == e.code || (e.alt && t == e.alt)) return e.id;
    return -1;
}

// The quick-reaction palette (Messenger/Telegram-style): a short key stored in
// message_reactions.emoji, paired with the drawable emoji id it renders as.
struct ReactionDef { const char* key; int id; };
static const ReactionDef kReactions[] = {
    {"heart", 3}, {"thumbsup", 9}, {"laugh", 6}, {"wow", 7}, {"sad", 2}, {"fire", 4},
};
static int ReactionEmojiId(const std::string& key) {
    for (const auto& r : kReactions) if (key == r.key) return r.id;
    return 3;   // default to the heart glyph for unknown keys
}

// True when `body` is nothing but 1-3 emoji shortcodes (Telegram renders those
// big and background-less). Reports how many via `count`.
static bool IsEmojiOnly(const std::string& body, int& count) {
    count = 0;
    std::size_t i = 0, n = body.size();
    while (i < n) {
        while (i < n && (body[i] == ' ' || body[i] == '\n')) ++i;
        if (i >= n) break;
        std::size_t j = i; while (j < n && body[j] != ' ' && body[j] != '\n') ++j;
        if (EmojiId(body.substr(i, j - i)) < 0) return false;
        ++count; i = j;
    }
    return count > 0 && count <= 3;
}
// Draw emoji `id` centered at `c`, fitting a box of side `s`.
static void DrawEmoji(ImDrawList* dl, ImVec2 c, float s, int id) {
    float r = s * 0.46f;
    ImU32 face = IM_COL32(245, 200, 70, 255), dark = IM_COL32(60, 48, 20, 255), white = IM_COL32(250,250,250,255);
    auto eyes = [&](float ey, float er){ dl->AddCircleFilled(ImVec2(c.x-r*0.34f,c.y+ey), er, dark, 10);
                                         dl->AddCircleFilled(ImVec2(c.x+r*0.34f,c.y+ey), er, dark, 10); };
    auto mouthArc = [&](ImVec2 ctr, float rad, float a0, float a1){
        dl->PathClear(); dl->PathArcTo(ctr, rad, a0, a1, 16); dl->PathStroke(dark, 0, 2.2f); };
    const float PI = 3.14159265f;
    switch (id) {
        case 0: // smile
            dl->AddCircleFilled(c, r, face, 24); eyes(-r*0.18f, r*0.10f);
            mouthArc(ImVec2(c.x, c.y - r*0.05f), r*0.42f, 0.18f*PI, 0.82f*PI); break;
        case 1: // grin (open smile w/ teeth)
            dl->AddCircleFilled(c, r, face, 24); eyes(-r*0.2f, r*0.10f);
            dl->PathClear(); dl->PathArcTo(ImVec2(c.x,c.y+r*0.02f), r*0.5f, 0.1f*PI, 0.9f*PI, 20); dl->PathFillConvex(dark);
            dl->AddRectFilled(ImVec2(c.x-r*0.4f,c.y+r*0.08f), ImVec2(c.x+r*0.4f,c.y+r*0.2f), white); break;
        case 2: // sad
            dl->AddCircleFilled(c, r, face, 24); eyes(-r*0.18f, r*0.10f);
            mouthArc(ImVec2(c.x, c.y + r*0.55f), r*0.42f, 1.18f*PI, 1.82f*PI); break;
        case 3: { // heart
            ImU32 red = IM_COL32(232, 72, 92, 255);
            dl->AddCircleFilled(ImVec2(c.x-r*0.28f, c.y-r*0.12f), r*0.34f, red, 16);
            dl->AddCircleFilled(ImVec2(c.x+r*0.28f, c.y-r*0.12f), r*0.34f, red, 16);
            dl->AddTriangleFilled(ImVec2(c.x-r*0.58f,c.y+r*0.02f), ImVec2(c.x+r*0.58f,c.y+r*0.02f), ImVec2(c.x,c.y+r*0.62f), red); break; }
        case 4: { // fire
            dl->AddTriangleFilled(ImVec2(c.x,c.y-r*0.7f), ImVec2(c.x-r*0.5f,c.y+r*0.5f), ImVec2(c.x+r*0.5f,c.y+r*0.5f), IM_COL32(236,120,40,255));
            dl->AddTriangleFilled(ImVec2(c.x,c.y-r*0.1f), ImVec2(c.x-r*0.26f,c.y+r*0.5f), ImVec2(c.x+r*0.26f,c.y+r*0.5f), IM_COL32(250,210,70,255)); break; }
        case 5: { // star
            ImU32 gold = IM_COL32(245, 205, 80, 255); ImVec2 p[10];
            for (int k=0;k<10;++k){ float a=-PI/2+k*PI/5; float rr=(k%2==0)?r:r*0.42f; p[k]=ImVec2(c.x+std::cos(a)*rr,c.y+std::sin(a)*rr);}
            for (int k=0;k<10;++k) dl->AddTriangleFilled(c,p[k],p[(k+1)%10],gold); break; }
        case 6: // laugh (grin + closed happy eyes)
            dl->AddCircleFilled(c, r, face, 24);
            mouthArc(ImVec2(c.x-r*0.34f, c.y-r*0.05f), r*0.18f, 1.1f*PI, 1.9f*PI);
            mouthArc(ImVec2(c.x+r*0.34f, c.y-r*0.05f), r*0.18f, 1.1f*PI, 1.9f*PI);
            dl->PathClear(); dl->PathArcTo(ImVec2(c.x,c.y+r*0.05f), r*0.5f, 0.08f*PI, 0.92f*PI, 20); dl->PathFillConvex(dark); break;
        case 7: // wow (o mouth)
            dl->AddCircleFilled(c, r, face, 24); eyes(-r*0.2f, r*0.12f);
            dl->AddCircleFilled(ImVec2(c.x, c.y+r*0.32f), r*0.2f, dark, 14); break;
        case 8: // cool (sunglasses)
            dl->AddCircleFilled(c, r, face, 24);
            dl->AddRectFilled(ImVec2(c.x-r*0.6f,c.y-r*0.28f), ImVec2(c.x-r*0.08f,c.y+r*0.02f), dark, 2);
            dl->AddRectFilled(ImVec2(c.x+r*0.08f,c.y-r*0.28f), ImVec2(c.x+r*0.6f,c.y+r*0.02f), dark, 2);
            dl->AddLine(ImVec2(c.x-r*0.08f,c.y-r*0.16f), ImVec2(c.x+r*0.08f,c.y-r*0.16f), dark, 2.0f);
            mouthArc(ImVec2(c.x, c.y + r*0.02f), r*0.36f, 0.18f*PI, 0.82f*PI); break;
        case 9: { // thumbs up
            ImU32 sk = IM_COL32(240, 196, 120, 255);
            dl->AddRectFilled(ImVec2(c.x-r*0.1f,c.y-r*0.6f), ImVec2(c.x+r*0.32f,c.y+r*0.05f), sk, r*0.12f); // thumb
            dl->AddRectFilled(ImVec2(c.x-r*0.5f,c.y-r*0.05f), ImVec2(c.x+r*0.5f,c.y+r*0.6f), sk, r*0.14f);  // fist
            break; }
        default: break;
    }
}

// A heart centered at c with lobe-radius ~r, in an arbitrary color (used for
// the like toggle, where the color conveys liked/not-liked state).
void DrawHeart(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    dl->AddCircleFilled(ImVec2(c.x - r*0.45f, c.y - r*0.2f), r*0.55f, col, 14);
    dl->AddCircleFilled(ImVec2(c.x + r*0.45f, c.y - r*0.2f), r*0.55f, col, 14);
    dl->AddTriangleFilled(ImVec2(c.x - r*0.95f, c.y + r*0.05f), ImVec2(c.x + r*0.95f, c.y + r*0.05f), ImVec2(c.x, c.y + r), col);
}

// A small emoji button that opens a picker grid; clicking an emoji appends its
// shortcode to `buf` (space-separated so RenderChatBody tokenizes it).
void EmojiPicker(const char* id, char* buf, std::size_t cap) {
    ImGui::PushID(id);
    ImVec2 bp = ImGui::GetCursorScreenPos();
    if (ImGui::Button("##emojibtn", ImVec2(30, 30))) ImGui::OpenPopup("##emojipop");
    DrawEmoji(ImGui::GetWindowDrawList(), ImVec2(bp.x + 15, bp.y + 15), 22, 0);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Emoji");
    if (ImGui::BeginPopup("##emojipop")) {
        int k = 0;
        for (const auto& e : kEmojis) {
            if (k % 5 != 0) ImGui::SameLine();
            ImGui::PushID(e.id);
            ImVec2 sp = ImGui::GetCursorScreenPos();
            if (ImGui::Button("##e", ImVec2(32, 32))) {
                std::string s = buf;
                if (!s.empty() && s.back() != ' ') s += ' ';
                s += e.code; s += ' ';
                std::snprintf(buf, cap, "%s", s.c_str());
                ImGui::CloseCurrentPopup();
            }
            DrawEmoji(ImGui::GetWindowDrawList(), ImVec2(sp.x + 16, sp.y + 16), 24, e.id);
            ImGui::PopID(); ++k;
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

// Render a chat message body with word-wrapping, turning http(s) URLs into
// clickable links and emoji shortcodes into drawn glyphs. Plain text takes a
// fast TextWrapped path; richer bodies use the manual token layout.
// maxW > 0 constrains wrapping to that pixel width (e.g. a chat bubble); 0 uses
// the remaining content width.
void RenderChatBody(const std::string& body, float maxW = 0.0f) {
    bool hasLink  = body.find("http://") != std::string::npos || body.find("https://") != std::string::npos;
    bool hasEmoji = body.find(':') != std::string::npos || body.find("<3") != std::string::npos || body.find("xD") != std::string::npos;
    if (!hasLink && !hasEmoji) {
        if (maxW > 0.0f) ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + maxW);
        ImGui::TextWrapped("%s", body.c_str());
        if (maxW > 0.0f) ImGui::PopTextWrapPos();
        return;
    }
    const float avail = maxW > 0.0f ? maxW : ImGui::GetContentRegionAvail().x;
    const float spaceW = ImGui::CalcTextSize(" ").x;
    const float em = ImGui::GetTextLineHeight();
    ImU32 linkCol = ImGui::GetColorU32(kAccent);
    float lineX = 0.0f; bool lineStarted = false;
    std::size_t i = 0, n = body.size();
    while (i < n) {
        if (body[i] == '\n') { lineStarted = false; lineX = 0; ++i; ImGui::NewLine(); continue; }
        if (body[i] == ' ')  { ++i; continue; }
        std::size_t j = i;
        while (j < n && body[j] != ' ' && body[j] != '\n') ++j;
        std::string tok = body.substr(i, j - i);
        i = j;
        int eid = EmojiId(tok);
        float tw = (eid >= 0) ? em : ImGui::CalcTextSize(tok.c_str()).x;
        if (lineStarted && lineX + spaceW + tw > avail) { lineStarted = false; lineX = 0; }
        if (lineStarted) { ImGui::SameLine(0, spaceW); lineX += spaceW; }
        if (eid >= 0) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            DrawEmoji(ImGui::GetWindowDrawList(), ImVec2(p.x + em * 0.5f, p.y + em * 0.5f), em, eid);
            ImGui::Dummy(ImVec2(em, em));
        } else {
            bool link = tok.rfind("http://", 0) == 0 || tok.rfind("https://", 0) == 0;
            if (link) {
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(linkCol), "%s", tok.c_str());
                ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddLine(ImVec2(mn.x, mx.y - 1), ImVec2(mx.x, mx.y - 1), linkCol, 1.0f);
                if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsItemClicked()) OpenExternal(tok);
            } else {
                ImGui::TextUnformatted(tok.c_str());
            }
        }
        lineX += tw; lineStarted = true;
    }
}

// The pixel WIDTH RenderChatBody will use for `body` wrapped at maxW — the width
// of its longest wrapped line (<= maxW). Mirrors RenderChatBody's tokenizer so a
// chat bubble can be sized to hug its content exactly (no overflow on resize).
float MeasureChatBodyWidth(const std::string& body, float maxW) {
    bool hasLink  = body.find("http://") != std::string::npos || body.find("https://") != std::string::npos;
    bool hasEmoji = body.find(':') != std::string::npos || body.find("<3") != std::string::npos || body.find("xD") != std::string::npos;
    if (!hasLink && !hasEmoji) {
        float w = ImGui::CalcTextSize(body.c_str(), nullptr, false, maxW).x;
        return w < maxW ? w : maxW;
    }
    const float spaceW = ImGui::CalcTextSize(" ").x;
    const float em = ImGui::GetTextLineHeight();
    float lineX = 0.0f, maxLine = 0.0f; bool lineStarted = false;
    std::size_t i = 0, n = body.size();
    while (i < n) {
        if (body[i] == '\n') { if (lineX > maxLine) maxLine = lineX; lineStarted = false; lineX = 0; ++i; continue; }
        if (body[i] == ' ')  { ++i; continue; }
        std::size_t j = i; while (j < n && body[j] != ' ' && body[j] != '\n') ++j;
        std::string tok = body.substr(i, j - i); i = j;
        int eid = EmojiId(tok);
        float tw = (eid >= 0) ? em : ImGui::CalcTextSize(tok.c_str()).x;
        if (lineStarted && lineX + spaceW + tw > maxW) { if (lineX > maxLine) maxLine = lineX; lineStarted = false; lineX = 0; }
        if (lineStarted) lineX += spaceW;
        lineX += tw; lineStarted = true;
    }
    if (lineX > maxLine) maxLine = lineX;
    return maxLine < maxW ? maxLine : maxW;
}

// Rounded square tile showing the item's initial — a lightweight "thumbnail"
// that needs no image assets. Advances the layout like a normal item.
void IconTile(const std::string& name, float size) {
    ImVec4 raw = NameColor(name);
    // Mute the hue toward a neutral steel so a list reads as a clean, uniform set
    // (Steam/Unity) instead of a rainbow of saturated tiles.
    auto mute = [](float c) { return 0.40f + (c - 0.40f) * 0.38f; };
    ImVec4 col(mute(raw.x), mute(raw.y), mute(raw.z), 1.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rnd = size * 0.14f;
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size),
                      ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.30f)), rnd);
    dl->AddRect(p, ImVec2(p.x + size, p.y + size),
                ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.55f)), rnd, 0, 1.0f);
    char init[2] = { name.empty() ? '?' : (char)std::toupper((unsigned char)name[0]), 0 };
    ImVec2 ts = ImGui::CalcTextSize(init);
    dl->AddText(ImVec2(p.x + (size - ts.x) * 0.5f, p.y + (size - ts.y) * 0.5f),
                ImGui::GetColorU32(ImVec4(0.88f, 0.90f, 0.94f, 1.0f)), init);
    ImGui::Dummy(ImVec2(size, size));
}

// ---- Optional per-game cover art -----------------------------------------
// A game can ship a cover image next to its .okayscene (cover.png/.jpg, or
// <stem>.png/.jpg). We load it once via stb_image, upload an SDL texture, and
// cache it by game path. nullptr (cached) means "no cover — draw the initial".
std::map<std::string, SDL_Texture*> g_coverCache;

SDL_Texture* GameCover(const std::string& gamePath) {
    auto it = g_coverCache.find(gamePath);
    if (it != g_coverCache.end()) return it->second;
    SDL_Texture* tex = nullptr;
    if (g_renderer) {
        fs::path gp(gamePath), dir = gp.parent_path(), stem = gp.stem();
        std::error_code ec;
        const char* names[] = { "cover.png", "cover.jpg", "cover.jpeg" };
        std::vector<fs::path> cands = { dir / (stem.string() + ".png"),
                                        dir / (stem.string() + ".jpg") };
        for (const char* n : names) cands.push_back(dir / n);
        for (const auto& c : cands) {
            if (!fs::exists(c, ec)) continue;
            int w = 0, h = 0, ch = 0;
            unsigned char* px = stbi_load(c.string().c_str(), &w, &h, &ch, 4);
            if (!px) continue;
            SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
                px, w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
            if (surf) { tex = SDL_CreateTextureFromSurface(g_renderer, surf); SDL_FreeSurface(surf); }
            stbi_image_free(px);
            break;
        }
    }
    g_coverCache[gamePath] = tex;   // cache even nullptr so we don't re-stat every frame
    return tex;
}

// Draw a game's cover (rounded, cover-fit) if it has one, else the initial tile.
void DrawGameIcon(const std::string& gamePath, const std::string& name, float size) {
    SDL_Texture* tex = GameCover(gamePath);
    if (!tex) { IconTile(name, size); return; }
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddImageRounded(
        (ImTextureID)(intptr_t)tex, p, ImVec2(p.x + size, p.y + size),
        ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, size * 0.22f);
    ImGui::Dummy(ImVec2(size, size));
}

// ---- Profile-picture (avatar) textures, loaded from a public URL ----------
// Downloaded once to a per-URL cache file, decoded via stb_image, cached as an
// SDL texture. nullptr (cached) means "no avatar — draw the initial".
std::map<std::string, SDL_Texture*> g_avatarCache;
std::string* g_myAvatarSlot = nullptr;   // points at the Account card's cached avatar URL

// Avatar downloads used to block the render thread (a freeze the first time a
// friends/chat list showed uncached avatars). The network fetch now runs on a
// detached worker; decode + GPU upload stay on the main thread (local, fast).
static SDL_mutex*          g_avatarMutex = nullptr;   // guards the two sets below
static std::set<std::string> g_avatarPending;         // URLs currently downloading
static std::set<std::string> g_avatarFailed;          // URLs that failed (don't retry)
struct AvatarDl { std::string url, path; };
static int AvatarDlWorker(void* p) {
    AvatarDl* d = (AvatarDl*)p;
    bool ok = false;
    try { ok = Download(d->url, d->path); } catch (...) {}
    std::error_code ec;
    if (g_avatarMutex) {
        SDL_LockMutex(g_avatarMutex);
        g_avatarPending.erase(d->url);
        if (!ok && !fs::exists(d->path, ec)) g_avatarFailed.insert(d->url);
        SDL_UnlockMutex(g_avatarMutex);
    }
    delete d;
    return 0;
}

SDL_Texture* AvatarTexture(const std::string& url) {
    if (url.empty() || !g_renderer) return nullptr;
    auto it = g_avatarCache.find(url);
    if (it != g_avatarCache.end()) return it->second;
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec) / "okayspace_avatars";
    fs::create_directories(dir, ec);
    char name[32]; std::snprintf(name, sizeof(name), "%zu", std::hash<std::string>{}(url));
    fs::path cache = dir / (std::string(name) + ".img");
    if (fs::exists(cache, ec)) {
        // Already downloaded: decode + upload here (fast), then cache the texture.
        SDL_Texture* tex = nullptr;
        int w = 0, h = 0, ch = 0;
        unsigned char* px = stbi_load(cache.string().c_str(), &w, &h, &ch, 4);
        if (px) {
            SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(px, w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
            if (s) { tex = SDL_CreateTextureFromSurface(g_renderer, s); SDL_FreeSurface(s); }
            stbi_image_free(px);
        }
        if (tex) { g_avatarCache[url] = tex; return tex; }
    }
    // Not on disk yet (or a bad decode): fetch in the background once, and show
    // the coloured-initial fallback until it lands. Never block the UI.
    if (!g_avatarMutex) g_avatarMutex = SDL_CreateMutex();
    bool kick = false;
    SDL_LockMutex(g_avatarMutex);
    if (g_avatarFailed.count(url)) { SDL_UnlockMutex(g_avatarMutex); return nullptr; }
    if (!g_avatarPending.count(url)) { g_avatarPending.insert(url); kick = true; }
    SDL_UnlockMutex(g_avatarMutex);
    if (kick) {
        AvatarDl* d = new AvatarDl{url, cache.string()};
        if (SDL_Thread* t = SDL_CreateThread(AvatarDlWorker, "okay-avdl", d)) SDL_DetachThread(t);
        else { SDL_LockMutex(g_avatarMutex); g_avatarPending.erase(url); SDL_UnlockMutex(g_avatarMutex); delete d; }
    }
    return nullptr;
}

// Draw a circular avatar: the image if the URL loads, else a colored initial.
void DrawAvatar(const std::string& url, const std::string& name, float size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 ctr(p.x + size * 0.5f, p.y + size * 0.5f);
    SDL_Texture* tex = AvatarTexture(url);
    if (tex) {
        dl->AddImageRounded((ImTextureID)(intptr_t)tex, p, ImVec2(p.x + size, p.y + size),
                            ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, size * 0.5f);
    } else {
        ImVec4 col = NameColor(name);
        dl->AddCircleFilled(ctr, size * 0.5f, ImGui::GetColorU32(ImVec4(col.x*0.55f, col.y*0.55f, col.z*0.55f, 1)), 32);
        char init[2] = { name.empty() ? '?' : (char)std::toupper((unsigned char)name[0]), 0 };
        ImVec2 ts = ImGui::CalcTextSize(init);
        dl->AddText(ImVec2(ctr.x - ts.x*0.5f, ctr.y - ts.y*0.5f), IM_COL32(255,255,255,255), init);
    }
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

// Draw a 5-star rating row at a screen position. `avg` in [0,5] fills stars
// (a star lights once avg reaches its half-point). Each star is 5 triangles
// radiating from a centre, so it reads as a real star without concave-fill
// issues. Returns the row's pixel width so callers can advance the cursor.
float DrawStars(ImDrawList* dl, ImVec2 pos, float sz, float avg, ImU32 on, ImU32 off) {
    const float step = sz * 1.3f, outer = sz * 0.5f, inner = sz * 0.2f;
    for (int i = 0; i < 5; ++i) {
        ImVec2 c(pos.x + outer + i * step, pos.y + outer);
        ImU32 col = (avg >= i + 0.5f) ? on : off;
        ImVec2 pts[10];
        for (int k = 0; k < 10; ++k) {
            float ang = -1.5707963f + k * 0.6283185f;     // -90deg, +36deg steps
            float rad = (k % 2 == 0) ? outer : inner;
            pts[k] = ImVec2(c.x + std::cos(ang) * rad, c.y + std::sin(ang) * rad);
        }
        for (int k = 0; k < 10; ++k)                        // fan the 10 rim points around the centre
            dl->AddTriangleFilled(c, pts[k], pts[(k + 1) % 10], col);
    }
    return 5 * step;
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
        else if (k == "online_status") { try { g_onlineStatus = std::stoi(v); } catch (...) {} }
        else if (k == "last_dm_id") { try { g_lastDmId = std::stol(v); } catch (...) {} }
        else if (k == "share_presence") g_sharePresence = (v != "0");
        else if (k == "play_view") { try { g_playView = std::stoi(v) ? 1 : 0; } catch (...) {} }
        else if (k == "tab") { try { g_lastTab = std::stoi(v); } catch (...) {} }
        else if (k == "fav" && !v.empty()) g_favorites.push_back(v);
        else if (k == "favfriend" && !v.empty()) g_favFriends.insert(v);
        else if (k == "pinchat" && !v.empty()) g_pinnedChats.insert(v);
        else if (k == "mutechat" && !v.empty()) g_mutedChats.insert(v);
        else if (k == "archivechat" && !v.empty()) g_archivedChats.insert(v);
        else if (k == "min_to_tray") g_minimizeToTray = (v != "0");
        else if (k == "nav_collapsed") g_navCollapsed = (v != "0");
        else if (k == "nick" && !v.empty()) { auto bar = v.find('|'); if (bar != std::string::npos && bar + 1 < v.size()) g_friendNicks[v.substr(0, bar)] = v.substr(bar + 1); }
        else if (k == "recent" && !v.empty()) g_recent.push_back(v);
        else if (k == "libdir" && !v.empty()) g_libraryDirs.push_back(v);
        else if (k == "wish" && !v.empty()) { try { g_wishlist.push_back(std::stol(v)); } catch (...) {} }
        else if (k == "collname" && !v.empty()) g_collections[v];   // ensure (possibly empty) collection
        else if (k == "coll" && !v.empty()) {
            auto bar = v.find('|');
            if (bar != std::string::npos)
                g_collections[v.substr(0, bar)].push_back(v.substr(bar + 1));
        }
        else if (k == "stat" && !v.empty()) {
            // stat=<count>|<lastunix>|<path>  (path may itself contain '|')
            auto a = v.find('|'), b = (a == std::string::npos) ? a : v.find('|', a + 1);
            if (a != std::string::npos && b != std::string::npos) {
                PlayStat st; std::string path = v.substr(b + 1);
                try { st.count = std::stoi(v.substr(0, a)); } catch (...) {}
                try { st.last  = std::stol(v.substr(a + 1, b - a - 1)); } catch (...) {}
                if (!path.empty()) g_stats[path] = st;
            }
        }
        else if (k == "win_w") { try { g_winW = std::stoi(v); } catch (...) {} }
        else if (k == "win_h") { try { g_winH = std::stoi(v); } catch (...) {} }
        else if (k == "splash_logo") g_splashLogo = v;
        else if (k == "splash_bar") g_splashShowBar = (v != "0");
        else if (k == "splash_accent_on") g_splashAccentOverride = (v == "1");
        else if (k == "splash_bg_top")  { std::sscanf(v.c_str(), "%f %f %f", &g_splashBgTop[0], &g_splashBgTop[1], &g_splashBgTop[2]); }
        else if (k == "splash_bg_bot")  { std::sscanf(v.c_str(), "%f %f %f", &g_splashBgBot[0], &g_splashBgBot[1], &g_splashBgBot[2]); }
        else if (k == "splash_accent")  { std::sscanf(v.c_str(), "%f %f %f", &g_splashAccent[0], &g_splashAccent[1], &g_splashAccent[2]); }
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
    f << "online_status=" << g_onlineStatus << "\n";
    f << "last_dm_id=" << g_lastDmId << "\n";
    f << "share_presence=" << (g_sharePresence ? 1 : 0) << "\n";
    f << "play_view=" << g_playView << "\n";
    f << "tab=" << g_lastTab << "\n";
    f << "win_w=" << g_winW << "\n";
    f << "win_h=" << g_winH << "\n";
    f << "splash_logo=" << g_splashLogo << "\n";
    f << "splash_bar=" << (g_splashShowBar ? 1 : 0) << "\n";
    f << "splash_accent_on=" << (g_splashAccentOverride ? 1 : 0) << "\n";
    f << "splash_bg_top=" << g_splashBgTop[0] << " " << g_splashBgTop[1] << " " << g_splashBgTop[2] << "\n";
    f << "splash_bg_bot=" << g_splashBgBot[0] << " " << g_splashBgBot[1] << " " << g_splashBgBot[2] << "\n";
    f << "splash_accent=" << g_splashAccent[0] << " " << g_splashAccent[1] << " " << g_splashAccent[2] << "\n";
    for (const auto& p : g_favorites)    f << "fav=" << p << "\n";
    for (const auto& u : g_favFriends)   f << "favfriend=" << u << "\n";
    for (const auto& u : g_pinnedChats)  f << "pinchat=" << u << "\n";
    for (const auto& u : g_mutedChats)   f << "mutechat=" << u << "\n";
    for (const auto& u : g_archivedChats) f << "archivechat=" << u << "\n";
    f << "min_to_tray=" << (g_minimizeToTray ? 1 : 0) << "\n";
    f << "nav_collapsed=" << (g_navCollapsed ? 1 : 0) << "\n";
    for (const auto& kv : g_friendNicks) if (!kv.second.empty()) f << "nick=" << kv.first << "|" << kv.second << "\n";
    for (const auto& p : g_recent)       f << "recent=" << p << "\n";
    for (const auto& p : g_libraryDirs)  f << "libdir=" << p << "\n";
    for (long id : g_wishlist)           f << "wish=" << id << "\n";
    for (const auto& kv : g_collections) {
        f << "collname=" << kv.first << "\n";
        for (const auto& p : kv.second) f << "coll=" << kv.first << "|" << p << "\n";
    }
    for (const auto& kv : g_stats)
        f << "stat=" << kv.second.count << "|" << kv.second.last << "|" << kv.first << "\n";
}

// ---- Floating Chat window --------------------------------------------------
// A separate, always-available window for direct messages: a conversation list
// on the left (the "tabs") and the selected conversation on the right, with the
// full chat feature set (clickable links, emoji, ❤ likes, edit-your-own). Opened
// from the sidebar "Chat" button and from any friend's "Message" action.
// Short, single-line preview of a message body for the conversation list /
// reply quote: strips newlines and clips to `n` chars with an ellipsis.
static std::string ChatPreview(const std::string& body, std::size_t n) {
    std::string s; s.reserve(body.size());
    for (char c : body) s += (c == '\n' || c == '\r') ? ' ' : c;
    if (s.size() > n) { s.erase(n); while (!s.empty() && s.back()==' ') s.pop_back(); s += "\xE2\x80\xA6"; }
    return s;
}

// The first http(s) URL in `body` that points at an image (by extension), so a
// chat message containing an image link renders an inline preview.
static std::string FirstImageUrl(const std::string& body) {
    std::size_t i = 0, n = body.size();
    while (i < n) {
        while (i < n && (body[i]==' '||body[i]=='\n')) ++i;
        std::size_t j = i; while (j < n && body[j]!=' ' && body[j]!='\n') ++j;
        std::string tok = body.substr(i, j - i); i = j;
        if (tok.rfind("http://",0)==0 || tok.rfind("https://",0)==0) {
            std::string low = tok; for (auto& c : low) c = (char)std::tolower((unsigned char)c);
            std::size_t q = low.find('?'); if (q != std::string::npos) low.erase(q);
            for (std::string ext : {".png",".jpg",".jpeg",".bmp",".gif",".webp"})
                if (low.size() >= ext.size() && low.compare(low.size()-ext.size(), ext.size(), ext) == 0)
                    return tok;
        }
    }
    return "";
}

// ---- Background chat poll state ---------------------------------------------
// The floating Chat window polled messages (every 4s), typing (every 2.5s),
// conversations (every 6s) and the friend list (every 15s) on the UI thread —
// a steady stream of blocking HTTPS calls that made chat feel laggy. A worker
// thread (ChatPollWorker, own account service) now does all of it and publishes
// here under a mutex; the window renders what's cached and never blocks. User
// actions (send/edit/react/typing-out) stay on the main thread.
static SDL_mutex*  g_chatMutex = nullptr;
static SDL_Thread* g_chatThread = nullptr;
static volatile bool g_chatPollRun = true;
static volatile bool g_chatPeerTyping = false;
static volatile bool g_chatForceLog = false;   // a send/edit/react asks for an immediate re-poll
static volatile bool g_chatTypingReq = false;  // the composer asks the worker to broadcast "typing"
static std::string g_chatSelUid;                                        // (guarded) open conversation
static bool g_chatLogReady = false, g_chatConvReady = false, g_chatFriReady = false;  // (guarded)
static std::vector<okay::DirectMessage> g_chatLog;                      // (guarded)
static std::map<long, std::map<std::string,long>> g_chatReacts;        // (guarded)
static std::map<long, std::set<std::string>> g_chatMyReacts;           // (guarded)
static long g_chatPeerRead = 0;                                         // (guarded)
static std::vector<okay::Messages::ConversationPreview> g_chatConvs;    // (guarded)
static std::vector<okay::Friend> g_chatFriends;                        // (guarded)
// ---- Group chats (multi-person DMs) ----------------------------------------
static volatile long g_chatSelGroup = 0;        // (guarded input) open group id (0 = DM mode)
static bool g_chatGroupsReady = false, g_chatGLogReady = false;        // (guarded)
static std::vector<okay::Messages::GroupConvo>  g_chatGroups;          // (guarded) my groups
static std::vector<okay::Messages::GroupMessage> g_chatGLog;          // (guarded) open group thread
static std::map<long, std::map<std::string,long>> g_chatGReacts;      // (guarded)
static std::map<long, std::set<std::string>>      g_chatGMyReacts;    // (guarded)

// Render the open group conversation (header + members menu + multi-sender
// thread + composer). Actions run on the main thread (same as DM sends); the
// background poller publishes glog/greacts. Kept a free function to keep the
// huge DrawChatWindow readable.
static void DrawGroupConversation(
    okay::Messages& dm,
    const std::vector<okay::Messages::GroupConvo>& groups,
    const std::vector<okay::Messages::GroupMessage>& glog,
    const std::map<long, std::map<std::string,long>>& greacts,
    const std::map<long, std::set<std::string>>& gmyreacts,
    const std::map<std::string, const okay::Friend*>& fById,
    char* gInput, long& gEditId, long& gReplyTo,
    std::string& gReplyName, std::string& gReplySnippet,
    std::map<long,long>& gSeenId)
{
    long gid = g_chatSelGroup;
    const okay::Messages::GroupConvo* gp = nullptr;
    for (const auto& gg : groups) if (gg.id == gid) gp = &gg;
    std::string title = (gp && !gp->title.empty()) ? gp->title : "Group chat";
    int memberCount = gp ? gp->members : 0;

    // ---- Header: group glyph, title, member count, options menu ----------
    ImVec2 hp = ImGui::GetCursorScreenPos();
    ImDrawList* hd = ImGui::GetWindowDrawList();
    hd->AddCircleFilled(ImVec2(hp.x + 13, hp.y + 13), 11, ImGui::GetColorU32(kAccentDim), 20);
    hd->AddCircleFilled(ImVec2(hp.x + 23, hp.y + 21), 10, IM_COL32(92, 102, 122, 255), 20);
    ImGui::Dummy(ImVec2(38, 34));
    ImGui::SameLine(); ImGui::BeginGroup();
    ImGui::TextUnformatted(title.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
    ImGui::Text("%d member%s", memberCount, memberCount == 1 ? "" : "s");
    ImGui::PopStyleColor();
    ImGui::EndGroup();
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 30);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
    ImVec2 mbp = ImGui::GetCursorScreenPos();
    if (ImGui::Button("##gmenu", ImVec2(26, 26))) ImGui::OpenPopup("##gmenu_pop");
    { ImU32 ic = ImGui::GetColorU32(ImGuiCol_Text);
      for (int k = -1; k <= 1; ++k) hd->AddCircleFilled(ImVec2(mbp.x + 13 + k*5.0f, mbp.y + 13), 1.6f, ic, 8); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Group options");
    static char renameBuf[64] = "";
    if (ImGui::BeginPopup("##gmenu_pop")) {
        if (ImGui::BeginMenu("Add member")) {
            int n = 0;
            for (const auto& kv : fById) {
                const okay::Friend* f = kv.second;
                if (!f || f->status != "accepted") continue;
                std::string nm = f->display.empty() ? f->username : f->display;
                if (ImGui::MenuItem((nm + "##gaddm" + f->id).c_str())) {
                    if (dm.AddGroupMember(gid, f->id)) { Toast("Added " + nm + " to the group", 1); g_chatForceLog = true; }
                    else Toast("Only the group owner can add members", 2);
                }
                ++n;
            }
            if (n == 0) ImGui::TextDisabled("No friends to add.");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Rename group")) {
            ImGui::SetNextItemWidth(180);
            ImGui::InputTextWithHint("##grn", "New name", renameBuf, sizeof(renameBuf));
            if (ImGui::Button("Save##grn") && renameBuf[0]) {
                if (dm.RenameGroup(gid, renameBuf)) { g_chatForceLog = true; Toast("Group renamed", 1); renameBuf[0] = '\0'; }
                else Toast("Only the group owner can rename it", 2);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Leave group")) {
            if (dm.LeaveGroup(gid)) { g_chatSelGroup = 0; g_chatForceLog = true; Toast("You left the group", 1); }
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();
    if (g_chatSelGroup == 0) return;   // left the group this frame

    // Mark the newest message seen (clears the unread dot in the list).
    if (!glog.empty()) gSeenId[gid] = glog.back().id;

    // ---- Thread -----------------------------------------------------------
    float composerH = 48.0f + (gReplyTo || gEditId ? 24.0f : 0.0f);
    ImGui::BeginChild("##glog", ImVec2(0, ImGui::GetContentRegionAvail().y - composerH), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    float avW = ImGui::GetContentRegionAvail().x;
    float baseX = ImGui::GetCursorPosX();
    const float padX = 12.0f, padY = 7.0f;
    std::string prevSender; bool firstMsg = true;
    for (const auto& m : glog) {
        bool newSender = firstMsg || m.sender != prevSender;
        firstMsg = false; prevSender = m.sender;
        ImGui::Dummy(ImVec2(0, newSender ? 9.0f : 2.0f));

        float bubbleMax = std::min(avW * 0.72f, 460.0f); if (bubbleMax < 120) bubbleMax = (avW < 120 ? avW : 120);
        float bodyW = MeasureChatBodyWidth(m.body, bubbleMax - padX*2);
        char tbuf[24] = ""; if (m.when>0){ std::time_t tt=(std::time_t)m.when; std::tm* tm=std::localtime(&tt); if(tm) std::strftime(tbuf,sizeof(tbuf),"%H:%M",tm); }
        std::string meta = tbuf; if (m.edited) meta += " \xC2\xB7 edited";
        float metaW = ImGui::CalcTextSize(meta.c_str()).x;
        float contentW = std::max(bodyW, metaW);
        float useW = std::max(contentW + padX*2, 46.0f);
        float x0 = m.mine ? (avW - useW) : 0.0f; if (x0 < 0) x0 = 0;

        // Sender name above the first message of a received run.
        if (!m.mine && newSender && !m.senderName.empty()) {
            ImGui::SetCursorPosX(baseX + 2);
            ImGui::PushStyleColor(ImGuiCol_Text, NameColor(m.senderName));
            ImGui::TextUnformatted(m.senderName.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::PushID((int)m.id);
        ImGui::SetCursorPosX(baseX + x0);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, m.mine ? kAccentDim : ImVec4(0.204f,0.235f,0.306f,1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, padY));
        ImGui::BeginChild("##gbub", ImVec2(useW, 0), ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
        RenderChatBody(m.body, ImGui::GetContentRegionAvail().x);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(m.mine ? ImVec4(0.85f,0.90f,1.0f,0.75f) : ImVec4(0.65f,0.70f,0.80f,0.9f)));
        ImGui::TextUnformatted(meta.c_str());
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        // Right-click: react / reply / copy / (edit+delete if mine).
        auto myit = gmyreacts.find(m.id);
        if (ImGui::BeginPopupContextItem("##gmsgctx")) {
            if (ImGui::BeginMenu("React")) {
                for (const auto& rdef : kReactions) {
                    bool have = myit != gmyreacts.end() && myit->second.count(rdef.key);
                    ImVec2 mp = ImGui::GetCursorScreenPos();
                    if (ImGui::MenuItem((std::string("     ") + (have?"remove":"add") + "##grx" + rdef.key).c_str()))
                        { dm.GroupReact(m.id, rdef.key, !have); g_chatForceLog = true; }
                    DrawEmoji(ImGui::GetWindowDrawList(), ImVec2(mp.x+9, mp.y+ImGui::GetTextLineHeight()*0.5f), 18, rdef.id);
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Reply")) { gReplyTo = m.id; gReplyName = m.mine ? "yourself" : m.senderName; gReplySnippet = ChatPreview(m.body, 40); }
            if (ImGui::MenuItem("Copy text")) ImGui::SetClipboardText(m.body.c_str());
            if (m.mine) {
                ImGui::Separator();
                if (ImGui::MenuItem("Edit"))   { gEditId = m.id; gReplyTo = 0; std::snprintf(gInput, 1024, "%s", m.body.c_str()); }
                if (ImGui::MenuItem("Delete")) { if (dm.DeleteGroup(m.id)) g_chatForceLog = true; }
            }
            ImGui::EndPopup();
        }
        // Double-click to heart.
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            bool have = myit != gmyreacts.end() && myit->second.count("heart");
            dm.GroupReact(m.id, "heart", !have); g_chatForceLog = true;
        }

        // Reaction pills under the bubble.
        auto cit = greacts.find(m.id);
        if (cit != greacts.end() && !cit->second.empty()) {
            ImGui::SetCursorPosX(baseX + x0);
            bool firstPill = true;
            for (const auto& kv : cit->second) {
                if (kv.second <= 0) continue;
                if (!firstPill) ImGui::SameLine(); firstPill = false;
                bool mineR = myit != gmyreacts.end() && myit->second.count(kv.first);
                std::string cnt = std::to_string(kv.second);
                float pw = 22.0f + ImGui::CalcTextSize(cnt.c_str()).x;
                ImGui::PushID(ReactionEmojiId(kv.first) * 131 + (int)(m.id & 0xffff));
                ImVec2 pp = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##gpill", ImVec2(pw, 18))) { dm.GroupReact(m.id, kv.first, !mineR); g_chatForceLog = true; }
                ImDrawList* pdl = ImGui::GetWindowDrawList();
                pdl->AddRectFilled(pp, ImVec2(pp.x+pw, pp.y+18), mineR ? ImGui::GetColorU32(kAccentDim) : IM_COL32(40,46,58,255), 9.0f);
                DrawEmoji(pdl, ImVec2(pp.x+10, pp.y+9), 13, ReactionEmojiId(kv.first));
                pdl->AddText(ImVec2(pp.x+18, pp.y+2), ImGui::GetColorU32(ImGuiCol_Text), cnt.c_str());
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }
    if (glog.empty()) ImGui::TextDisabled("No messages yet — say hello!");
    // Keep pinned to the newest message.
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    // ---- Composer ---------------------------------------------------------
    if (gReplyTo || gEditId) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(kAccent));
        ImGui::Text(gEditId ? "Editing message" : "Replying to %s", gReplyName.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("x##gcancel")) { gReplyTo = 0; gEditId = 0; gInput[0] = '\0'; }
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 64);
    bool enter = ImGui::InputTextWithHint("##ginput", "Message the group", gInput, 1024,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    bool sendBtn = ImGui::Button("Send##g", ImVec2(-1, 0));
    if ((enter || sendBtn) && gInput[0]) {
        if (gEditId) { if (dm.EditGroup(gEditId, gInput)) { gEditId = 0; gInput[0] = '\0'; g_chatForceLog = true; } }
        else if (dm.SendGroup(gid, gInput, gReplyTo)) { gInput[0] = '\0'; gReplyTo = 0; g_chatForceLog = true; }
    }
}

void DrawChatWindow(okay::account::AccountService& account) {
    if (!g_chatWinOpen) return;
    static std::vector<okay::Friend> friends; static Uint32 friendsNext = 0;
    static std::vector<okay::Messages::ConversationPreview> convs; static Uint32 convsNext = 0;
    static std::string selUid, selName, selAvatar; static bool selOnline = false;
    static std::vector<okay::DirectMessage> log; static Uint32 logNext = 0;
    static std::map<long, std::map<std::string,long>> reacts;   // id -> emoji -> count
    static std::map<long, std::set<std::string>> myReacts;      // id -> emojis I used
    static long peerRead = 0;                        // peer's last-read id (Seen marker)
    static std::map<std::string,long> seenId;        // newest id I've viewed, per friend (unread)
    static long editId = 0, replyTo = 0;
    static std::string replyName, replySnippet;
    static char input[1024] = ""; static int lastShown = -1; static std::string lastConv;
    static char convFilter[96] = ""; static char msgSearch[96] = ""; static bool showSearch = false;
    static bool wantScrollBottom = false;            // Jump-to-latest request
    static bool focusComposer = false;               // focus the input on open
    static Uint32 typingSentAt = 0, typingPollNext = 0;  // typing indicator timers
    static bool peerTyping = false;                  // the other person is typing
    static long scrollToId = 0, flashId = 0;         // jump to a replied-to message
    static Uint32 flashUntil = 0;
    static std::map<std::string,std::string> drafts;  // unsent text per conversation (kept on switch)
    static std::string fwdBody;                       // message body queued to forward
    static bool fwdPick = false;                      // forward: pick a conversation popup
    static std::map<std::string, std::pair<long,std::string>> pinnedMsg;  // uid -> (msgId, preview)
    static std::string lightboxUrl;                   // image shown fullscreen (empty = none)
    // Group-chat composer / edit state (parallel to the DM ones above).
    static char   gInput[1024] = ""; static long gEditId = 0, gReplyTo = 0;
    static std::string gReplyName, gReplySnippet;
    static std::map<long,long> gSeenId;               // newest group msg id I've viewed (unread)
    static bool   showNewGroup = false;               // create-group dialog open
    static std::set<std::string> newGroupPick;        // friends picked for the new group
    static char   newGroupName[64] = "";
    // Group data published by the chat poller (read under g_chatMutex below).
    static std::vector<okay::Messages::GroupConvo>  groups;
    static std::vector<okay::Messages::GroupMessage> glog;
    static std::map<long, std::map<std::string,long>> greacts;
    static std::map<long, std::set<std::string>>      gmyreacts;

    auto openConv = [&](const std::string& uid, const std::string& nm, const std::string& av, bool online) {
        // Keep the current conversation's unsent draft before switching away.
        if (!selUid.empty()) { if (input[0]) drafts[selUid] = input; else drafts.erase(selUid); }
        g_chatSelGroup = 0;                 // leave group mode
        selUid = uid; selName = nm; selAvatar = av; selOnline = online;
        log.clear(); g_chatForceLog = true; editId = 0; replyTo = 0;
        // Restore this conversation's saved draft (if any).
        auto dit = drafts.find(uid);
        std::snprintf(input, sizeof(input), "%s", dit != drafts.end() ? dit->second.c_str() : "");
        lastShown = -1; msgSearch[0] = '\0'; showSearch = false; focusComposer = true;
        peerTyping = false; typingPollNext = 0; typingSentAt = 0;
    };
    // Open a group conversation (mutually exclusive with a 1:1 DM).
    auto openGroup = [&](long gid) {
        if (!selUid.empty()) { if (input[0]) drafts[selUid] = input; else drafts.erase(selUid); }
        selUid.clear(); g_chatSelGroup = gid;
        glog.clear(); g_chatForceLog = true; gEditId = 0; gReplyTo = 0; gInput[0] = '\0';
        focusComposer = true;
    };

    if (g_chatReqPending) {                     // a "Message" button asked to open a convo
        openConv(g_chatReqUid, g_chatReqName, g_chatReqAvatar, false);
        g_chatReqPending = false;
        ImGui::SetNextWindowFocus();
    }
    ImGui::SetNextWindowSize(ImVec2(720, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(420, 320), ImVec2(FLT_MAX, FLT_MAX));
    if (ImGui::Begin("Chat", &g_chatWinOpen)) {
        if (!account.IsLoggedIn()) {
            ImGui::TextDisabled("Sign in to chat with friends.");
            ImGui::End(); return;
        }
        okay::Messages dm(account);
        Uint32 now = SDL_GetTicks();
        // Friends + conversations come from the background chat poller (no UI block).
        (void)friendsNext; (void)convsNext;
        if (g_chatMutex) {
            SDL_LockMutex(g_chatMutex);
            if (g_chatFriReady)  { friends = g_chatFriends; g_chatFriReady = false; }
            if (g_chatConvReady) { convs   = g_chatConvs;   g_chatConvReady = false; }
            if (g_chatGroupsReady) { groups = g_chatGroups; g_chatGroupsReady = false; }
            if (g_chatGLogReady) { glog = g_chatGLog; greacts = g_chatGReacts; gmyreacts = g_chatGMyReacts; g_chatGLogReady = false; }
            SDL_UnlockMutex(g_chatMutex);
        }

        // Index friends by uid for names/avatars/presence.
        std::map<std::string, const okay::Friend*> fById;
        for (const auto& f : friends) fById[f.id] = &f;

        // ---- Left: conversation list (Messenger-style rows) -------------------
        // The window shrinks the sidebar on narrow widths so the thread stays usable.
        float listW = ImGui::GetContentRegionAvail().x < 560 ? 158.0f : 216.0f;
        ImGui::BeginChild("##chatlist", ImVec2(listW, 0), true);
        // New-chat button + search.
        ImGui::PushStyleColor(ImGuiCol_Button, kAccentDim);
        if (ImGui::Button("+ New message", ImVec2(-1, 0))) ImGui::OpenPopup("##newchat");
        ImGui::PopStyleColor();
        if (ImGui::BeginPopup("##newchat")) {
            ImGui::TextDisabled("Start a chat with...");
            ImGui::Separator();
            int shownF = 0;
            for (const auto& f : friends) {
                if (f.status != "accepted") continue;
                std::string nm = f.display.empty() ? f.username : f.display;
                if (ImGui::Selectable((nm + "##nc" + f.id).c_str())) {
                    openConv(f.id, nm, f.avatar, f.online);
                    g_archivedChats.erase(f.id);   // starting a chat un-archives it
                    ImGui::CloseCurrentPopup();
                }
                ++shownF;
            }
            if (shownF == 0) ImGui::TextDisabled("Add friends first.");
            ImGui::EndPopup();
        }
        // New group button opens the multi-select create dialog.
        if (ImGui::Button("+ New group", ImVec2(-1, 0))) {
            showNewGroup = true; newGroupPick.clear(); newGroupName[0] = '\0';
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##convfilter", "Search chats", convFilter, sizeof(convFilter));
        ImGui::Spacing();

        // Merge: conversations (most-recent first) then accepted friends with no
        // conversation yet, so you can always start one.
        struct Row { std::string uid, name, avatar, preview; long when=0, lastId=0; bool mine=false, online=false; };
        std::vector<Row> rows;
        std::set<std::string> have;
        for (const auto& c : convs) {
            Row r; r.uid = c.uid; r.lastId = c.lastId; r.when = c.when; r.mine = c.lastMine;
            r.preview = (c.lastMine ? "You: " : "") + ChatPreview(c.lastBody, 26);
            if (auto it = fById.find(c.uid); it != fById.end()) {
                r.name = it->second->display.empty() ? it->second->username : it->second->display;
                r.avatar = it->second->avatar; r.online = it->second->online;
            } else r.name = (c.uid == selUid) ? selName : ChatPreview(c.uid, 10);
            rows.push_back(r); have.insert(c.uid);
        }
        for (const auto& f : friends) {
            if (f.status != "accepted" || have.count(f.id)) continue;
            Row r; r.uid = f.id; r.name = f.display.empty() ? f.username : f.display;
            r.avatar = f.avatar; r.online = f.online; r.preview = "Tap to chat";
            rows.push_back(r);
        }
        // Pinned conversations float to the top (stable: keeps recency order).
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            return g_pinnedChats.count(a.uid) > g_pinnedChats.count(b.uid);
        });
        std::string filt = convFilter;
        for (auto& ch : filt) ch = (char)std::tolower((unsigned char)ch);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // One conversation row. Returns true if it was shown (passed the filter).
        auto drawConvRow = [&](const Row& r) -> bool {
            if (!filt.empty()) {
                std::string low = r.name; for (auto& ch : low) ch = (char)std::tolower((unsigned char)ch);
                if (low.find(filt) == std::string::npos) return false;
            }
            bool pinned = g_pinnedChats.count(r.uid) != 0;
            bool muted  = g_mutedChats.count(r.uid) != 0;
            bool archived = g_archivedChats.count(r.uid) != 0;
            ImGui::PushID(r.uid.c_str());
            bool sel = (r.uid == selUid);
            bool unread = r.lastId > 0 && !r.mine && r.lastId > seenId[r.uid] && !sel && !muted;
            ImVec2 rp = ImGui::GetCursorScreenPos();
            float rowH = 46.0f;
            if (ImGui::Selectable("##row", sel, 0, ImVec2(0, rowH))) openConv(r.uid, r.name, r.avatar, r.online);
            // Right-click: pin / mute / archive / mark read.
            if (ImGui::BeginPopupContextItem("##convctx")) {
                if (ImGui::MenuItem(pinned ? "Unpin" : "Pin to top")) { if (pinned) g_pinnedChats.erase(r.uid); else g_pinnedChats.insert(r.uid); SavePrefs(); }
                if (ImGui::MenuItem(muted ? "Unmute" : "Mute")) { if (muted) g_mutedChats.erase(r.uid); else g_mutedChats.insert(r.uid); SavePrefs(); }
                if (ImGui::MenuItem(archived ? "Unarchive" : "Archive")) { if (archived) g_archivedChats.erase(r.uid); else g_archivedChats.insert(r.uid); SavePrefs(); }
                if (unread && ImGui::MenuItem("Mark as read")) seenId[r.uid] = r.lastId;
                ImGui::EndPopup();
            }
            ImVec2 av(rp.x + 4, rp.y + 6); float as = 34.0f;
            SDL_Texture* tex = AvatarTexture(r.avatar);
            if (tex) dl->AddImageRounded((ImTextureID)(intptr_t)tex, av, ImVec2(av.x+as, av.y+as), ImVec2(0,0), ImVec2(1,1), IM_COL32_WHITE, as*0.5f);
            else {
                ImVec4 col = NameColor(r.name);
                dl->AddCircleFilled(ImVec2(av.x+as*0.5f, av.y+as*0.5f), as*0.5f, ImGui::GetColorU32(ImVec4(col.x*0.55f,col.y*0.55f,col.z*0.55f,1)), 24);
                char init[2] = { r.name.empty()?'?':(char)std::toupper((unsigned char)r.name[0]), 0 };
                ImVec2 tsz = ImGui::CalcTextSize(init);
                dl->AddText(ImVec2(av.x+as*0.5f-tsz.x*0.5f, av.y+as*0.5f-tsz.y*0.5f), IM_COL32(255,255,255,255), init);
            }
            if (r.online) dl->AddCircleFilled(ImVec2(av.x+as-3, av.y+as-3), 4.5f, IM_COL32(120,205,110,255), 16),
                          dl->AddCircle(ImVec2(av.x+as-3, av.y+as-3), 5.0f, IM_COL32(30,34,42,255), 16, 1.6f);
            float tx = rp.x + 4 + as + 8;
            // Unread conversations get a brighter name; a relative timestamp sits
            // right-aligned on the name line (Messenger / Discord style).
            ImU32 nameCol = ImGui::GetColorU32(unread ? ImVec4(0.97f, 0.98f, 1.00f, 1.0f)
                                                      : ImGui::GetStyleColorVec4(ImGuiCol_Text));
            dl->AddText(ImVec2(tx, rp.y + 6), nameCol, ChatPreview(r.name, 13).c_str());
            std::string rt = RelTime(r.when);
            if (!rt.empty()) {
                float rtw = ImGui::CalcTextSize(rt.c_str()).x;
                dl->AddText(ImVec2(rp.x + listW - 12 - rtw, rp.y + 7),
                            ImGui::GetColorU32(ImGuiCol_TextDisabled), rt.c_str());
            }
            dl->AddText(ImVec2(tx, rp.y + 24),
                        ImGui::GetColorU32(unread ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                        ChatPreview(r.preview, 20).c_str());
            // Right-side markers: unread dot (preview line) + mute.
            float mx = rp.x + listW - 14;
            if (unread) { dl->AddCircleFilled(ImVec2(mx, rp.y + 30), 4.5f, ImGui::GetColorU32(kAccent), 16); }
            if (muted)  { DrawEmoji(dl, ImVec2(mx, rp.y + 30), 12, 2); }   // small marker
            if (pinned) { dl->AddCircleFilled(ImVec2(rp.x + 10, rp.y + 10), 3.0f, ImGui::GetColorU32(kAccent), 10); }
            ImGui::PopID();
            return true;
        };

        int shown = 0, archivedCount = 0;
        for (const auto& r : rows) {
            if (g_archivedChats.count(r.uid)) { ++archivedCount; continue; }
            if (drawConvRow(r)) ++shown;
        }
        // Archived section (collapsed by default).
        if (archivedCount > 0) {
            ImGui::Spacing();
            if (ImGui::CollapsingHeader(("Archived (" + std::to_string(archivedCount) + ")###arch").c_str())) {
                for (const auto& r : rows) if (g_archivedChats.count(r.uid)) drawConvRow(r);
            }
        }
        // ---- Groups section ----------------------------------------------
        if (!groups.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted("GROUPS");
            ImGui::PopStyleColor();
            std::string gfilt = convFilter; for (auto& c : gfilt) c = (char)std::tolower((unsigned char)c);
            for (const auto& gc : groups) {
                std::string label = gc.title.empty() ? ("Group (" + std::to_string(gc.members) + ")") : gc.title;
                if (!gfilt.empty()) { std::string low = label; for (auto& c : low) c = (char)std::tolower((unsigned char)c);
                                      if (low.find(gfilt) == std::string::npos) continue; }
                bool gsel = (g_chatSelGroup == gc.id);
                bool gunread = gc.lastId > 0 && gc.lastId > gSeenId[gc.id] && !gsel;
                ImGui::PushID((int)(gc.id ^ 0x40000000));
                ImVec2 rp = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##grow", gsel, 0, ImVec2(0, 46))) openGroup(gc.id);
                ImDrawList* dl2 = ImGui::GetWindowDrawList();
                float cy = rp.y + 23;
                dl2->AddCircleFilled(ImVec2(rp.x + 15, cy - 4), 9, ImGui::GetColorU32(kAccentDim), 16);
                dl2->AddCircleFilled(ImVec2(rp.x + 24, cy + 4), 9, IM_COL32(92, 102, 122, 255), 16);
                dl2->AddText(ImVec2(rp.x + 42, rp.y + 6), ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
                std::string prev = (gc.lastSender.empty() ? "" : gc.lastSender + ": ") + ChatPreview(gc.lastBody, 18);
                dl2->AddText(ImVec2(rp.x + 42, rp.y + 24), ImGui::GetColorU32(ImGuiCol_TextDisabled), prev.c_str());
                if (gunread) dl2->AddCircleFilled(ImVec2(rp.x + ImGui::GetContentRegionAvail().x - 8, cy), 4,
                                                   ImGui::GetColorU32(kAccent), 12);
                ImGui::PopID();
            }
        }
        if (shown == 0 && groups.empty())
            ImGui::TextDisabled(convFilter[0] ? "No matches." : "Add friends to start chatting.");
        ImGui::EndChild();
        ImGui::SameLine();

        // ---- Right: the selected conversation ---------------------------------
        ImGui::BeginChild("##chatconv", ImVec2(0, 0), false);
        if (g_chatSelGroup > 0) {
            DrawGroupConversation(dm, groups, glog, greacts, gmyreacts, fById,
                                  gInput, gEditId, gReplyTo, gReplyName, gReplySnippet, gSeenId);
        } else
        if (selUid.empty()) {
            float cw = ImGui::GetContentRegionAvail().x, cy = ImGui::GetContentRegionAvail().y;
            ImGui::SetCursorPos(ImVec2(cw*0.5f - 90, cy*0.5f - 10));
            ImGui::TextDisabled("Pick a conversation to start chatting.");
        } else {
            // refresh presence/name for the selected friend
            if (auto it = fById.find(selUid); it != fById.end()) {
                selOnline = it->second->online;
                selName = it->second->display.empty() ? it->second->username : it->second->display;
                selAvatar = it->second->avatar;
            }
            // Header: avatar with a presence dot, name + status, and actions.
            ImVec2 avp = ImGui::GetCursorScreenPos();
            DrawAvatar(selAvatar, selName, 34.0f);
            {   // presence dot on the avatar's corner
                ImDrawList* hd = ImGui::GetWindowDrawList();
                ImU32 dot = selOnline ? IM_COL32(120, 200, 110, 255) : IM_COL32(120, 128, 140, 255);
                hd->AddCircleFilled(ImVec2(avp.x + 28, avp.y + 28), 5.5f, dot, 16);
                hd->AddCircle(ImVec2(avp.x + 28, avp.y + 28), 5.5f,
                              ImGui::GetColorU32(ImGuiCol_WindowBg), 16, 2.0f);
            }
            ImGui::SameLine(); ImGui::BeginGroup();
            ImGui::TextUnformatted(selName.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(peerTyping ? kAccent
                                        : (selOnline ? ImVec4(0.47f,0.80f,0.43f,1) : ImVec4(0.6f,0.64f,0.7f,1))));
            ImGui::TextUnformatted(peerTyping ? "typing\xE2\x80\xA6" : (selOnline ? "Active now" : "Offline"));
            ImGui::PopStyleColor();
            ImGui::EndGroup();
            // Right-aligned actions: conversation menu (⋯) + search toggle.
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 62);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
            ImVec2 mbp = ImGui::GetCursorScreenPos();
            if (ImGui::Button("##convmenu", ImVec2(26, 26))) ImGui::OpenPopup("##convmenu_pop");
            {   // three-dots glyph
                ImDrawList* d = ImGui::GetWindowDrawList();
                ImU32 ic = ImGui::GetColorU32(ImGuiCol_Text);
                for (int k = -1; k <= 1; ++k)
                    d->AddCircleFilled(ImVec2(mbp.x + 13 + k * 5.0f, mbp.y + 13), 1.6f, ic, 8);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Conversation options");
            if (ImGui::BeginPopup("##convmenu_pop")) {
                bool pin = g_pinnedChats.count(selUid) != 0;
                bool mut = g_mutedChats.count(selUid) != 0;
                bool arc = g_archivedChats.count(selUid) != 0;
                if (ImGui::MenuItem(pin ? "Unpin conversation" : "Pin conversation")) {
                    if (pin) g_pinnedChats.erase(selUid); else g_pinnedChats.insert(selUid); SavePrefs();
                }
                if (ImGui::MenuItem(mut ? "Unmute" : "Mute notifications")) {
                    if (mut) g_mutedChats.erase(selUid); else g_mutedChats.insert(selUid); SavePrefs();
                }
                if (ImGui::MenuItem(arc ? "Unarchive" : "Archive")) {
                    if (arc) g_archivedChats.erase(selUid); else g_archivedChats.insert(selUid); SavePrefs();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Copy username")) ImGui::SetClipboardText(selName.c_str());
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY());
            ImVec2 sbp = ImGui::GetCursorScreenPos();
            if (ImGui::Button("##srch", ImVec2(26, 26))) { showSearch = !showSearch; if (!showSearch) msgSearch[0]='\0'; }
            {   // magnifier / close glyph
                ImDrawList* d = ImGui::GetWindowDrawList();
                ImU32 ic = ImGui::GetColorU32(ImGuiCol_Text);
                if (showSearch) {
                    d->AddLine(ImVec2(sbp.x + 9, sbp.y + 9), ImVec2(sbp.x + 17, sbp.y + 17), ic, 1.8f);
                    d->AddLine(ImVec2(sbp.x + 17, sbp.y + 9), ImVec2(sbp.x + 9, sbp.y + 17), ic, 1.8f);
                } else {
                    d->AddCircle(ImVec2(sbp.x + 11, sbp.y + 11), 5.0f, ic, 16, 1.8f);
                    d->AddLine(ImVec2(sbp.x + 15, sbp.y + 15), ImVec2(sbp.x + 19, sbp.y + 19), ic, 1.9f);
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Search in conversation");
            if (showSearch) { ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##msgsearch", "Find a message", msgSearch, sizeof(msgSearch)); }
            ImGui::Separator();

            // Pinned-message bar: a slim strip under the header showing the pinned
            // message. Click the text to jump to it in the thread; the x unpins.
            if (auto pit = pinnedMsg.find(selUid); pit != pinnedMsg.end()) {
                long pinId = pit->second.first;
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.17f,0.19f,0.25f,1.0f));
                ImGui::BeginChild("##pinbar", ImVec2(0, 26), false, ImGuiWindowFlags_NoScrollbar);
                ImVec2 pbp = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pbp.x, pbp.y + 3),
                        ImVec2(pbp.x + 3, pbp.y + 19), ImGui::GetColorU32(kAccent), 1.0f);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 9);
                ImGui::AlignTextToFramePadding();
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(kAccent));
                ImGui::TextUnformatted("Pinned");
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 8);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::TextUnformatted(pit->second.second.c_str());
                ImGui::PopStyleColor();
                // Clicking anywhere on the label area jumps to the pinned message.
                if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    scrollToId = pinId; flashId = pinId; flashUntil = now + 1400;
                }
                if (ImGui::IsWindowHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                // Unpin button pinned to the right edge of the strip.
                ImGui::SameLine(ImGui::GetWindowWidth() - 24);
                ImVec2 xbp = ImGui::GetCursorScreenPos();
                if (ImGui::Button("##unpin", ImVec2(18, 18))) pinnedMsg.erase(selUid);
                {
                    ImDrawList* xd = ImGui::GetWindowDrawList();
                    ImU32 xc = ImGui::GetColorU32(ImGuiCol_Text);
                    xd->AddLine(ImVec2(xbp.x + 6, xbp.y + 6), ImVec2(xbp.x + 12, xbp.y + 12), xc, 1.6f);
                    xd->AddLine(ImVec2(xbp.x + 12, xbp.y + 6), ImVec2(xbp.x + 6, xbp.y + 12), xc, 1.6f);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unpin");
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }

            // Messages, reactions, read-receipt and typing come from the background
            // chat poller — the window never blocks on the network. We just tell the
            // worker which conversation is open and read what it has published.
            (void)logNext; (void)typingPollNext;
            if (g_chatMutex) {
                SDL_LockMutex(g_chatMutex);
                g_chatSelUid = selUid;
                if (g_chatLogReady) {
                    log = g_chatLog; reacts = g_chatReacts; myReacts = g_chatMyReacts;
                    peerRead = g_chatPeerRead; g_chatLogReady = false;
                }
                SDL_UnlockMutex(g_chatMutex);
            }
            peerTyping = g_chatPeerTyping;
            // Mark everything seen locally (clears the unread dot for this chat).
            long firstUnread = 0;
            for (const auto& m : log) {
                if (!m.mine && m.id > seenId[selUid] && firstUnread == 0) firstUnread = m.id;
            }
            for (const auto& m : log) if (!m.mine && m.id > seenId[selUid]) seenId[selUid] = m.id;
            // id -> message, for rendering reply quotes.
            std::map<long, const okay::DirectMessage*> byId;
            for (const auto& m : log) byId[m.id] = &m;
            long lastMineId = 0; for (const auto& m : log) if (m.mine) lastMineId = m.id;

            // Reserve space for the composer (grows with reply banner + edit tag).
            float composerH = 48.0f + (replyTo || editId ? 24.0f : 0.0f);
            bool scrolledUp = false; ImVec2 logRectMin, logRectMax;
            // AlwaysVerticalScrollbar reserves the scrollbar space permanently so the
            // content width is stable — otherwise the right-aligned ("mine") bubbles
            // jump sideways whenever the scrollbar appears/disappears on resize.
            ImGui::BeginChild("##clog", ImVec2(0, ImGui::GetContentRegionAvail().y - composerH), false,
                              ImGuiWindowFlags_AlwaysVerticalScrollbar);
            float avW = ImGui::GetContentRegionAvail().x;
            float baseX = ImGui::GetCursorPosX();
            std::string msFilt = msgSearch; for (auto& ch : msFilt) ch = (char)std::tolower((unsigned char)ch);
            long prevDay = 0; bool prevMine = false; long prevWhen = 0; bool first = true;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float avatarW = 28.0f, avatarGap = 6.0f, padX = 12.0f, padY = 7.0f;
            int matches = 0;
            for (const auto& m : log) {
                if (!msFilt.empty()) {
                    std::string low = m.body; for (auto& ch : low) ch = (char)std::tolower((unsigned char)ch);
                    if (low.find(msFilt) == std::string::npos) continue;
                    ++matches;
                }
                long day = DayKey(m.when);
                if (day != 0 && day != prevDay) { ImGui::SetCursorPosX(baseX); ChatDateSeparator(m.when); prevDay = day; prevMine = false; first = true; }
                // "Unread messages" divider before the first message I hadn't seen.
                if (firstUnread != 0 && m.id == firstUnread) {
                    ImGui::SetCursorPosX(baseX); ImGui::Dummy(ImVec2(0, 4));
                    ImVec2 up = ImGui::GetCursorScreenPos(); float uw = ImGui::GetContentRegionAvail().x;
                    ImU32 uc = ImGui::GetColorU32(kAccent);
                    const char* ul = "New messages"; float ulw = ImGui::CalcTextSize(ul).x;
                    float uy = up.y + ImGui::GetTextLineHeight()*0.5f;
                    dl->AddLine(ImVec2(up.x, uy), ImVec2(up.x + (uw-ulw)*0.5f - 8, uy), uc, 1.0f);
                    dl->AddLine(ImVec2(up.x + (uw+ulw)*0.5f + 8, uy), ImVec2(up.x + uw, uy), uc, 1.0f);
                    ImGui::SetCursorPosX(baseX + (uw-ulw)*0.5f);
                    ImGui::PushStyleColor(ImGuiCol_Text, uc); ImGui::TextUnformatted(ul); ImGui::PopStyleColor();
                    ImGui::SetCursorPosX(baseX); ImGui::Dummy(ImVec2(0, 2));
                }
                bool grouped = !first && prevMine == m.mine && (m.when - prevWhen) < 300 && (m.when > 0);
                ImGui::Dummy(ImVec2(0, grouped ? 2.0f : 9.0f));

                int ecount = 0; bool emojiOnly = IsEmojiOnly(m.body, ecount);
                const float bigEm = ImGui::GetTextLineHeight() * 2.3f;
                // Cap the bubble width so it hugs its text instead of stretching
                // across a wide window (Messenger/iMessage keep bubbles narrow).
                float bubbleMax = std::min(avW * 0.72f, 460.0f);
                if (bubbleMax < 120) bubbleMax = (avW < 120 ? avW : 120);
                float bodyW = MeasureChatBodyWidth(m.body, bubbleMax - padX*2);
                // reply quote + time contribute to width
                const okay::DirectMessage* rep = (m.replyTo && byId.count(m.replyTo)) ? byId[m.replyTo] : nullptr;
                std::string repText = rep ? ChatPreview(rep->body, 36) : "";
                float repW = rep ? std::min(bubbleMax - padX*2, ImGui::CalcTextSize(repText.c_str()).x + 8) : 0.0f;
                char tbuf[24] = ""; if (m.when>0){ std::time_t tt=(std::time_t)m.when; std::tm* tm=std::localtime(&tt); if(tm) std::strftime(tbuf,sizeof(tbuf),"%H:%M",tm); }
                std::string meta = tbuf; if (m.edited) meta += " \xC2\xB7 edited";
                float metaW = ImGui::CalcTextSize(meta.c_str()).x;
                bool bareEmoji = emojiOnly && !rep;   // Telegram-style big, background-less emoji
                std::string imgUrl = bareEmoji ? std::string() : FirstImageUrl(m.body);
                float contentW = bareEmoji ? std::max((float)ecount * (bigEm + 3), metaW)
                                           : std::max({bodyW, repW, metaW});
                if (!imgUrl.empty()) contentW = std::max(contentW, std::min(bubbleMax - padX*2, 240.0f));
                float boxW = contentW + padX*2;
                float useW = std::max(boxW, 46.0f);   // actual bubble width (min clamp)
                float leftPad = m.mine ? 0.0f : (avatarW + avatarGap);
                // Right-align "mine" bubbles by the REAL bubble width (useW), not the
                // raw content width — otherwise short bubbles ("hi"/"ok") overhang
                // the right edge and drift when the window is resized.
                float x0 = m.mine ? (avW - useW) : leftPad; if (x0 < leftPad) x0 = leftPad;

                // Avatar for received messages (only on the first of a run).
                if (!m.mine && !grouped) {
                    ImVec2 cp = ImGui::GetCursorScreenPos();
                    float ax = cp.x; float ay = cp.y;
                    SDL_Texture* at = AvatarTexture(selAvatar);
                    if (at) dl->AddImageRounded((ImTextureID)(intptr_t)at, ImVec2(ax,ay), ImVec2(ax+avatarW,ay+avatarW), ImVec2(0,0), ImVec2(1,1), IM_COL32_WHITE, avatarW*0.5f);
                    else {
                        ImVec4 col = NameColor(selName);
                        dl->AddCircleFilled(ImVec2(ax+avatarW*0.5f, ay+avatarW*0.5f), avatarW*0.5f, ImGui::GetColorU32(ImVec4(col.x*0.55f,col.y*0.55f,col.z*0.55f,1)), 20);
                        char init[2]={selName.empty()?'?':(char)std::toupper((unsigned char)selName[0]),0};
                        ImVec2 tsz=ImGui::CalcTextSize(init);
                        dl->AddText(ImVec2(ax+avatarW*0.5f-tsz.x*0.5f, ay+avatarW*0.5f-tsz.y*0.5f), IM_COL32(255,255,255,255), init);
                    }
                }

                ImGui::PushID((int)m.id);
                // The bubble is an auto-height child window: its ChildBg sits behind
                // the content natively (no draw-list channel tricks), so it always
                // hugs what it renders and stays correct on resize.
                ImGui::SetCursorPosX(baseX + x0);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, bareEmoji ? ImVec4(0,0,0,0)
                                        : (m.mine ? kAccentDim : ImVec4(0.204f,0.235f,0.306f,1.0f)));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, padY));
                ImGui::BeginChild("##bubble", ImVec2(useW, 0),
                                  ImGuiChildFlags_AutoResizeY,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
                ImDrawList* bdl = ImGui::GetWindowDrawList();
                if (rep) {                                  // quoted message (click to jump)
                    ImGui::BeginGroup();
                    ImVec2 qp = ImGui::GetCursorScreenPos();
                    bdl->AddRectFilled(ImVec2(qp.x-4, qp.y), ImVec2(qp.x-1, qp.y + ImGui::GetTextLineHeight()*1.7f), ImGui::GetColorU32(kAccent), 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImVec4(0.8f,0.85f,0.95f,0.9f)));
                    ImGui::TextUnformatted((rep->mine ? "You" : selName).c_str());
                    ImGui::PopStyleColor();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                    ImGui::TextUnformatted(repText.c_str());
                    ImGui::PopStyleColor();
                    ImGui::EndGroup();
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            scrollToId = rep->id; flashId = rep->id; flashUntil = now + 1400;
                        }
                    }
                }
                if (bareEmoji) {                            // big, bubble-less emoji
                    std::size_t bi = 0, bn = m.body.size();
                    bool firstTok = true;
                    while (bi < bn) {
                        while (bi < bn && (m.body[bi]==' '||m.body[bi]=='\n')) ++bi;
                        if (bi >= bn) break;
                        std::size_t bj = bi; while (bj < bn && m.body[bj]!=' ' && m.body[bj]!='\n') ++bj;
                        int eid = EmojiId(m.body.substr(bi, bj-bi)); bi = bj;
                        if (!firstTok) ImGui::SameLine(0, 3);
                        ImVec2 ep = ImGui::GetCursorScreenPos();
                        DrawEmoji(bdl, ImVec2(ep.x + bigEm*0.5f, ep.y + bigEm*0.5f), bigEm, eid < 0 ? 0 : eid);
                        ImGui::Dummy(ImVec2(bigEm, bigEm));
                        firstTok = false;
                    }
                } else {
                    RenderChatBody(m.body, ImGui::GetContentRegionAvail().x);
                }
                // Inline image preview for an image link in the message.
                if (!imgUrl.empty()) {
                    if (SDL_Texture* it = AvatarTexture(imgUrl)) {
                        int iw = 0, ih = 0; SDL_QueryTexture(it, nullptr, nullptr, &iw, &ih);
                        float availW = ImGui::GetContentRegionAvail().x;
                        float w = (iw > 0 && iw > availW) ? availW : (float)iw;
                        if (w < 1) w = availW;
                        float h = (iw > 0) ? w * (float)ih / (float)iw : w * 0.6f;
                        float maxH = 220.0f; if (h > maxH) { w *= maxH / h; h = maxH; }
                        ImGui::Dummy(ImVec2(0, 2));
                        if (ImGui::ImageButton("##img", (ImTextureID)(intptr_t)it, ImVec2(w, h)))
                            lightboxUrl = imgUrl;   // click to view full-size
                        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    }
                }
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(bareEmoji ? ImVec4(0.6f,0.64f,0.72f,0.9f)
                                        : (m.mine ? ImVec4(0.85f,0.90f,1.0f,0.75f) : ImVec4(0.65f,0.70f,0.80f,0.9f))));
                ImGui::TextUnformatted(meta.c_str());
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();                       // ItemSpacing
                ImGui::EndChild();
                ImGui::PopStyleVar(2);                      // ChildRounding, WindowPadding
                ImGui::PopStyleColor();                     // ChildBg
                ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
                // Bubble tail on the first message of a run, pointing toward the
                // sender's side (iMessage/Discord style) — only for text bubbles.
                if (!grouped && !bareEmoji) {
                    float ty = bmin.y + 7.0f;
                    ImU32 tc = ImGui::GetColorU32(m.mine ? kAccentDim : ImVec4(0.204f,0.235f,0.306f,1.0f));
                    if (m.mine)
                        dl->AddTriangleFilled(ImVec2(bmax.x - 1, ty - 4), ImVec2(bmax.x + 6, ty), ImVec2(bmax.x - 1, ty + 5), tc);
                    else
                        dl->AddTriangleFilled(ImVec2(bmin.x + 1, ty - 4), ImVec2(bmin.x - 6, ty), ImVec2(bmin.x + 1, ty + 5), tc);
                }
                // Quick-reaction bar on hover: thumbs-up / heart / laugh + Reply,
                // so reacting doesn't need the right-click menu. Save/restore the
                // cursor so this overlay never disturbs the message's vertical flow.
                if (ImGui::IsMouseHoveringRect(bmin, bmax) && !ImGui::IsPopupOpen("##msgctx")) {
                    ImVec2 afterBubble = ImGui::GetCursorScreenPos();
                    float barY = bmin.y - 13.0f;
                    float barX = m.mine ? bmax.x - 96.0f : bmin.x;
                    ImGui::SetCursorScreenPos(ImVec2(barX, barY));
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f,0.17f,0.21f,0.96f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f,0.26f,0.32f,1.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4,2));
                    const struct { const char* key; int id; } quick[] = {
                        {kReactions[0].key, kReactions[0].id},
                        {kReactions[1].key, kReactions[1].id},
                        {kReactions[2].key, kReactions[2].id} };
                    for (int qi = 0; qi < 3; ++qi) {
                        if (qi) ImGui::SameLine(0, 2);
                        ImVec2 qp = ImGui::GetCursorScreenPos();
                        if (ImGui::Button((std::string("  ##qr") + std::to_string(m.id) + quick[qi].key).c_str(), ImVec2(22, 20))) {
                            bool have = myReacts[m.id].count(quick[qi].key) != 0;
                            dm.React(m.id, quick[qi].key, !have); g_chatForceLog = true;
                        }
                        DrawEmoji(dl, ImVec2(qp.x + 11, qp.y + 10), 15, quick[qi].id);
                    }
                    ImGui::SameLine(0, 2);
                    if (ImGui::Button((std::string("Reply##qr") + std::to_string(m.id)).c_str(), ImVec2(0, 20))) {
                        replyTo = m.id; replyName = m.mine ? "yourself" : selName; replySnippet = ChatPreview(m.body, 40);
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                    ImGui::SetCursorScreenPos(afterBubble);   // restore vertical flow
                }
                // Jump-to-reply target: scroll this message into view, then flash it.
                if (scrollToId == m.id) { ImGui::SetScrollHereY(0.4f); scrollToId = 0; }
                if (flashId == m.id && now < flashUntil) {
                    float a = 0.55f * ((flashUntil - now) / 1400.0f);
                    dl->AddRect(ImVec2(bmin.x-3, bmin.y-3), ImVec2(bmax.x+3, bmax.y+3),
                                ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, a)), 12.0f, 0, 2.5f);
                }

                // Right-click actions (React submenu, Reply, Copy, Edit, Delete).
                auto& myset = myReacts[m.id];
                if (ImGui::BeginPopupContextItem("##msgctx")) {
                    if (ImGui::BeginMenu("React")) {
                        for (const auto& rdef : kReactions) {
                            bool have = myset.count(rdef.key) != 0;
                            ImVec2 mp = ImGui::GetCursorScreenPos();
                            if (ImGui::MenuItem((std::string("     ") + (have ? "remove" : "add") + "##rx" + rdef.key).c_str()))
                                { dm.React(m.id, rdef.key, !have); g_chatForceLog = true; }
                            DrawEmoji(ImGui::GetWindowDrawList(), ImVec2(mp.x + 9, mp.y + ImGui::GetTextLineHeight()*0.5f), 18, rdef.id);
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Reply")) { replyTo = m.id; replyName = m.mine ? "yourself" : selName; replySnippet = ChatPreview(m.body, 40); }
                    if (ImGui::MenuItem("Copy text")) ImGui::SetClipboardText(m.body.c_str());
                    if (ImGui::MenuItem("Forward...")) { fwdBody = m.body; fwdPick = true; }
                    {
                        bool isPinned = pinnedMsg.count(selUid) && pinnedMsg[selUid].first == m.id;
                        if (ImGui::MenuItem(isPinned ? "Unpin message" : "Pin message")) {
                            if (isPinned) pinnedMsg.erase(selUid);
                            else pinnedMsg[selUid] = {m.id, ChatPreview(m.body, 48)};
                        }
                    }
                    if (m.mine) {
                        ImGui::Separator();
                        if (ImGui::MenuItem("Edit")) { editId = m.id; replyTo = 0; std::snprintf(input, sizeof(input), "%s", m.body.c_str()); }
                        if (ImGui::MenuItem("Delete")) { if (dm.Delete(m.id)) g_chatForceLog = true; }
                    }
                    ImGui::EndPopup();
                }
                // Double-click a bubble to quick-react with the heart (Messenger).
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    bool have = myset.count("heart") != 0; dm.React(m.id, "heart", !have); g_chatForceLog = true;
                }

                // Reaction pills on the line under the bubble (normal layout).
                auto cit = reacts.find(m.id);
                if (cit != reacts.end() && !cit->second.empty()) {
                    ImGui::SetCursorPosX(baseX + x0);
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
                    bool firstPill = true;
                    for (const auto& kv : cit->second) {
                        if (kv.second <= 0) continue;
                        if (!firstPill) ImGui::SameLine();
                        firstPill = false;
                        bool mineR = myset.count(kv.first) != 0;
                        std::string cnt = std::to_string(kv.second);
                        float pw = 22.0f + ImGui::CalcTextSize(cnt.c_str()).x;
                        ImGui::PushID(ReactionEmojiId(kv.first));
                        ImVec2 pp = ImGui::GetCursorScreenPos();
                        if (ImGui::InvisibleButton("##pill", ImVec2(pw, 18))) { dm.React(m.id, kv.first, !mineR); g_chatForceLog = true; }
                        ImU32 pc = mineR ? ImGui::GetColorU32(kAccentDim) : IM_COL32(40, 46, 58, 255);
                        dl->AddRectFilled(pp, ImVec2(pp.x + pw, pp.y + 18), pc, 9.0f);
                        if (mineR) dl->AddRect(pp, ImVec2(pp.x + pw, pp.y + 18), ImGui::GetColorU32(kAccent), 9.0f, 0, 1.3f);
                        DrawEmoji(dl, ImVec2(pp.x + 10, pp.y + 9), 13, ReactionEmojiId(kv.first));
                        dl->AddText(ImVec2(pp.x + 17, pp.y + 2), ImGui::GetColorU32(ImGuiCol_Text), cnt.c_str());
                        ImGui::PopID();
                    }
                    ImGui::PopStyleVar();
                }

                ImGui::PopID();
                ImGui::SetCursorPosX(baseX);
                prevMine = m.mine; prevWhen = m.when; first = false;
            }
            // Submit a zero-size item so the trailing SetCursorPosX above is
            // "claimed" — otherwise ImGui warns about moving the cursor without
            // growing the window (the "MESSAGE FROM DEAR IMGUI" overlay).
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
            // Typing bubble: a little received-style bubble with animated dots.
            if (peerTyping && msFilt.empty()) {
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::SetCursorPosX(baseX + avatarW + avatarGap);
                ImVec2 tp = ImGui::GetCursorScreenPos();
                float bw = 54.0f, bh = 26.0f;
                dl->AddRectFilled(tp, ImVec2(tp.x + bw, tp.y + bh), IM_COL32(52, 60, 78, 255), 11.0f);
                float t = (float)(SDL_GetTicks() % 1200) / 1200.0f;
                for (int d = 0; d < 3; ++d) {
                    float ph = t - d * 0.18f; if (ph < 0) ph += 1.0f;
                    float a = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(ph * 6.2831853f));
                    dl->AddCircleFilled(ImVec2(tp.x + 15 + d * 12, tp.y + bh * 0.5f), 3.2f,
                                        ImGui::GetColorU32(ImVec4(0.8f, 0.84f, 0.92f, a)), 12);
                }
                ImGui::Dummy(ImVec2(bw, bh));
            }
            // "Seen" under my latest message once the peer has read it.
            if (!log.empty() && log.back().mine && lastMineId > 0 && peerRead >= lastMineId && msFilt.empty()) {
                std::string seen = "Seen";
                float sw = ImGui::CalcTextSize(seen.c_str()).x;
                ImGui::SetCursorPosX(baseX + ImGui::GetContentRegionAvail().x - sw - 4);
                ImGui::TextDisabled("%s", seen.c_str());
            }
            if (log.empty()) ImGui::TextDisabled("No messages yet. Say hi!");
            else if (!msFilt.empty() && matches == 0) ImGui::TextDisabled("No messages match \"%s\".", msgSearch);
            // Auto-scroll to the newest message on open / new message (unless the
            // user has scrolled up to read history). Jump-to-latest overrides it.
            scrolledUp = ImGui::GetScrollMaxY() > 1.0f && (ImGui::GetScrollMaxY() - ImGui::GetScrollY()) > 80.0f;
            if (wantScrollBottom) { ImGui::SetScrollHereY(1.0f); wantScrollBottom = false; scrolledUp = false; }
            else if ((int)log.size() != lastShown || selUid != lastConv) {
                if (msFilt.empty() && !scrolledUp) ImGui::SetScrollHereY(1.0f);
                lastShown = (int)log.size(); lastConv = selUid;
            }
            logRectMin = ImGui::GetWindowPos();
            logRectMax = ImVec2(logRectMin.x + ImGui::GetWindowWidth(), logRectMin.y + ImGui::GetWindowHeight());
            ImGui::EndChild();
            // Floating "jump to latest" button when scrolled up. Draw it as an
            // overlay without disturbing the composer's normal layout below.
            if (scrolledUp) {
                ImVec2 keep = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(ImVec2(logRectMax.x - 44, logRectMax.y - 44));
                if (ImGui::Button("\xE2\x86\x93##jump", ImVec2(32, 32))) wantScrollBottom = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Jump to latest");
                ImGui::SetCursorScreenPos(keep);
                ImGui::Dummy(ImVec2(0.0f, 0.0f));   // claim the restored cursor
            }

            // ---- Composer: reply/edit banner, emoji, growing input, send -------
            // Esc clears a pending reply/edit; Up-arrow on an empty box edits your
            // last message (Telegram/Messenger shortcuts).
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    if (replyTo) replyTo = 0;
                    else if (editId) { editId = 0; input[0] = '\0'; }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && input[0] == '\0' && !editId && lastMineId > 0) {
                    if (auto it = byId.find(lastMineId); it != byId.end()) {
                        editId = lastMineId; replyTo = 0;
                        std::snprintf(input, sizeof(input), "%s", it->second->body.c_str());
                    }
                }
            }
            if (replyTo) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(kAccent));
                ImGui::Text("Replying to %s", replyName.c_str());
                ImGui::PopStyleColor();
                ImGui::SameLine(); ImGui::TextDisabled("%s", replySnippet.c_str());
                ImGui::SameLine(); if (ImGui::SmallButton("x##cancelreply")) replyTo = 0;
            } else if (editId) {
                ImGui::TextDisabled("Editing message"); ImGui::SameLine();
                if (ImGui::SmallButton("cancel##edit")) { editId = 0; input[0] = '\0'; }
            }
            // Grow the input from 1 to 4 lines with the content.
            int lines = 1; for (const char* c = input; *c; ++c) if (*c == '\n') ++lines;
            if (lines > 4) lines = 4;
            float inH = ImGui::GetTextLineHeight() * lines + 14;
            EmojiPicker("cwemoji", input, sizeof(input)); ImGui::SameLine();
            // Markdown formatting helpers: insert a sample token to edit in place.
            auto fmtBtn = [&](const char* lbl, const char* open, const char* close, const char* word, const char* tip) {
                if (ImGui::SmallButton(lbl)) {
                    std::size_t len = std::char_traits<char>::length(input);
                    std::string ins = std::string(open) + word + close;
                    std::snprintf(input + len, sizeof(input) - len, "%s", ins.c_str());
                    focusComposer = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                ImGui::SameLine(0, 2);
            };
            fmtBtn("B", "**", "**", "bold", "Bold  **text**");
            fmtBtn("I", "*", "*", "italic", "Italic  *text*");
            fmtBtn("</>", "`", "`", "code", "Code  `text`");
            ImVec2 inSz(ImGui::GetContentRegionAvail().x - 60, inH);
            if (focusComposer) { ImGui::SetKeyboardFocusHere(); focusComposer = false; }
            bool send = ImGui::InputTextMultiline("##cwin", input, sizeof(input), inSz,
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
            // Broadcast a typing signal while there's text (throttled to ~2s).
            if (input[0] && !editId && now - typingSentAt > 2000) { g_chatTypingReq = true; typingSentAt = now; }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, kAccentDim);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccent);
            if ((ImGui::Button(editId ? "Save" : "Send", ImVec2(52, inH)) || send) && input[0]) {
                if (editId) { if (dm.Edit(editId, input)) { editId = 0; input[0] = '\0'; g_chatForceLog = true; } }
                else if (dm.Send(selUid, input, replyTo)) { input[0] = '\0'; replyTo = 0; drafts.erase(selUid); g_chatForceLog = true; }
                ImGui::SetKeyboardFocusHere(-1);
            }
            ImGui::PopStyleColor(2);
        }
        ImGui::EndChild();

        // Forward a message: pick a friend to send a copy to.
        if (fwdPick) { ImGui::OpenPopup("Forward to..."); fwdPick = false; }
        if (ImGui::BeginPopup("Forward to...")) {
            ImGui::TextDisabled("Forward to:");
            ImGui::Separator();
            int shown = 0;
            for (const auto& f : friends) {
                if (f.status != "accepted") continue;
                std::string nm = f.display.empty() ? f.username : f.display;
                if (ImGui::Selectable(nm.c_str())) {
                    if (dm.Send(f.id, fwdBody, 0)) Toast("Forwarded to " + nm, 1);
                    ImGui::CloseCurrentPopup();
                }
                ++shown;
            }
            if (shown == 0) ImGui::TextDisabled("No friends to forward to.");
            ImGui::EndPopup();
        }

        // New-group creation dialog: pick friends + optional name -> CreateGroup.
        if (showNewGroup) { ImGui::OpenPopup("New group"); showNewGroup = false; }
        ImGui::SetNextWindowSize(ImVec2(320, 400), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal("New group", nullptr, ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextDisabled("Pick friends to add:");
            ImGui::BeginChild("##ngfriends", ImVec2(0, 250), true);
            int nf = 0;
            for (const auto& f : friends) {
                if (f.status != "accepted") continue;
                std::string nm = f.display.empty() ? f.username : f.display;
                bool picked = newGroupPick.count(f.id) != 0;
                if (ImGui::Checkbox((nm + "##ng" + f.id).c_str(), &picked)) {
                    if (picked) newGroupPick.insert(f.id); else newGroupPick.erase(f.id);
                }
                ++nf;
            }
            if (nf == 0) ImGui::TextDisabled("Add friends first.");
            ImGui::EndChild();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##ngname", "Group name (optional)", newGroupName, sizeof(newGroupName));
            ImGui::Spacing();
            ImGui::BeginDisabled(newGroupPick.empty());
            if (ImGui::Button("Create group")) {
                std::vector<std::string> uids(newGroupPick.begin(), newGroupPick.end());
                long nid = dm.CreateGroup(newGroupName, uids);
                if (nid > 0) { g_chatForceLog = true; openGroup(nid); Toast("Group created", 1); }
                else Toast("Couldn't create group", 2);
                newGroupPick.clear(); newGroupName[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { newGroupPick.clear(); newGroupName[0] = '\0'; ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }
    ImGui::End();

    // ---- Image lightbox: full-screen viewer for a clicked chat image --------
    // Drawn as its own top-level overlay so it covers the whole window, dims the
    // background, centers the image, and dismisses on click / Esc.
    if (!lightboxUrl.empty()) {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.03f, 0.92f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::SetNextWindowFocus();
        if (ImGui::Begin("##lightbox", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav)) {
            ImVec2 sz = ImGui::GetContentRegionAvail();
            SDL_Texture* it = AvatarTexture(lightboxUrl);
            if (it) {
                int iw = 0, ih = 0; SDL_QueryTexture(it, nullptr, nullptr, &iw, &ih);
                float w = (float)iw, h = (float)ih;
                if (iw > 0 && ih > 0) {
                    float scale = std::min((sz.x - 48) / w, (sz.y - 72) / h);
                    if (scale > 1.0f) scale = 1.0f;   // never upscale past native
                    if (scale > 0.0f) { w *= scale; h *= scale; }
                }
                ImGui::SetCursorPos(ImVec2((sz.x - w) * 0.5f, (sz.y - h) * 0.5f - 12));
                ImGui::Image((ImTextureID)(intptr_t)it, ImVec2(w, h));
            } else {
                const char* ld = "Loading image\xE2\x80\xA6";
                ImVec2 tsz = ImGui::CalcTextSize(ld);
                ImGui::SetCursorPos(ImVec2((sz.x - tsz.x) * 0.5f, sz.y * 0.5f));
                ImGui::TextDisabled("%s", ld);
            }
            // Bottom hint bar: open-in-browser + close.
            ImGui::SetCursorPos(ImVec2(sz.x * 0.5f - 96, sz.y - 40));
            if (ImGui::Button("Open in browser")) OpenExternal(lightboxUrl);
            ImGui::SameLine();
            if (ImGui::Button("Close")) lightboxUrl.clear();
            // Click the dimmed backdrop (not the image/buttons) or Esc to dismiss.
            if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                lightboxUrl.clear();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) lightboxUrl.clear();
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
}

// ---- Steam-style minimize to the system tray (Windows) ---------------------
// When minimized (and the option is on), the launcher hides from the taskbar and
// shows a tray icon; clicking it brings the launcher back. Keeps it running in
// the background for presence/notifications, like Steam. No-op off Windows.
#if defined(_WIN32)
static HWND          g_trayHwnd = nullptr;
static bool          g_trayActive = false;
static volatile bool g_trayRestoreReq = false;   // bring the window back (tray click / 2nd launch)
static volatile bool g_trayMenuReq   = false;    // show the tray right-click menu
static volatile bool g_trayQuitReq   = false;    // quit chosen from the tray menu
static const UINT    WM_OKAYTRAY = WM_USER + 0x51;
static const UINT    OKAY_TRAY_UID = 1;
// A system-wide message a second launcher broadcasts so the first one surfaces
// itself (single-instance). Registered in main() before the hook can fire.
static UINT          g_showMsg = 0;

static HWND OkayWindowHWND(SDL_Window* w) {
    SDL_SysWMinfo wm; SDL_VERSION(&wm.version);
    if (w && SDL_GetWindowWMInfo(w, &wm) && wm.subsystem == SDL_SYSWM_WINDOWS)
        return wm.info.win.window;
    return nullptr;
}
// Returns true only if the tray icon was actually registered. Retained for
// possible future use; the launcher no longer hides itself to the tray.
[[maybe_unused]] static bool OkayTrayAdd(SDL_Window* w) {
    if (g_trayActive) return true;
    g_trayHwnd = OkayWindowHWND(w);
    if (!g_trayHwnd) return false;
    NOTIFYICONDATAA nid; ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_trayHwnd;
    nid.uID = OKAY_TRAY_UID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_OKAYTRAY;
    nid.hIcon = (HICON)LoadImageA(GetModuleHandleA(nullptr), MAKEINTRESOURCEA(1), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (!nid.hIcon) nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    lstrcpynA(nid.szTip, "OkaySpace \xE2\x80\x94 click to open (right-click for menu)", (int)sizeof(nid.szTip));
    if (!Shell_NotifyIconA(NIM_ADD, &nid)) return false;
    g_trayActive = true;
    return true;
}
static void OkayTrayRemove() {
    if (!g_trayActive || !g_trayHwnd) { g_trayActive = false; return; }
    NOTIFYICONDATAA nid; ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid); nid.hWnd = g_trayHwnd; nid.uID = OKAY_TRAY_UID;
    Shell_NotifyIconA(NIM_DELETE, &nid);
    g_trayActive = false;
}
// Right-click tray menu: a guaranteed way to reopen or quit even if a left-click
// is finicky. Called from the main loop (not the hook) since it blocks.
static void OkayTrayShowMenu() {
    if (!g_trayHwnd) return;
    HMENU m = CreatePopupMenu();
    if (!m) { g_trayRestoreReq = true; return; }
    AppendMenuA(m, MF_STRING, 1, "Open OkaySpace");
    AppendMenuA(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuA(m, MF_STRING, 2, "Quit");
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g_trayHwnd);   // required so the menu dismisses correctly
    int cmd = (int)TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                  pt.x, pt.y, 0, g_trayHwnd, nullptr);
    PostMessageA(g_trayHwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd == 1)      g_trayRestoreReq = true;
    else if (cmd == 2) g_trayQuitReq = true;
}
// SDL pumps Win32 messages on the main thread, so this hook runs there too; it
// just flags work the main loop performs (any left-click opens the app; a
// right-click raises the menu; a broadcast from a 2nd launch also surfaces us).
static void SDLCALL OkayMsgHook(void* userdata, void* hWnd, unsigned int msg, Uint64 wParam, Sint64 lParam) {
    (void)userdata; (void)hWnd; (void)wParam;
    if (msg == WM_OKAYTRAY) {
        UINT ev = (UINT)(lParam & 0xFFFF);
        if (ev == WM_LBUTTONUP || ev == WM_LBUTTONDBLCLK) g_trayRestoreReq = true;
        else if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) g_trayMenuReq = true;
    } else if (g_showMsg && msg == g_showMsg) {
        g_trayRestoreReq = true;   // a second launcher asked us to come forward
    }
}
#endif

// ---- Single instance -------------------------------------------------------
// Only one launcher may run at a time. Returns true if this process is the
// first/only instance (continue starting up); false if another is already
// running — in which case we've asked that one to surface itself and should exit.
#if defined(_WIN32)
static HANDLE g_singletonMutex = nullptr;
static bool AcquireSingleInstance() {
    g_showMsg = RegisterWindowMessageA("OkaySpaceLauncherShowWindow");
    g_singletonMutex = CreateMutexA(nullptr, FALSE, "Local\\OkaySpaceLauncherSingleton");
    if (g_singletonMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_showMsg) PostMessageA(HWND_BROADCAST, g_showMsg, 0, 0);
        return false;
    }
    return true;   // we hold the mutex (released when the process exits)
}
#else
static int g_singletonFd = -1;
static bool AcquireSingleInstance() {
    std::error_code ec;
    fs::path lock = fs::temp_directory_path(ec) / "okayspace_launcher.lock";
    g_singletonFd = ::open(lock.c_str(), O_CREAT | O_RDWR, 0600);
    if (g_singletonFd < 0) return true;   // can't lock → don't block startup
    if (::flock(g_singletonFd, LOCK_EX | LOCK_NB) != 0) {
        ::close(g_singletonFd); g_singletonFd = -1;
        return false;   // another instance holds the lock
    }
    return true;   // lock held for the process lifetime
}
#endif

// ---- Background friends/DM poll --------------------------------------------
// The nav badges (pending friend requests + unread DMs) used to be fetched on
// the MAIN thread every 30s, which froze the UI for the duration of two HTTPS
// round-trips — a periodic hitch. It now runs on a worker thread that uses its
// OWN AccountService (re-read from disk each cycle, so it tracks login/logout
// without racing the main thread's service). Results are published under a
// mutex; the main loop just reads them.
static SDL_mutex*  g_pollMutex = nullptr;
static SDL_Thread* g_pollThread = nullptr;
static volatile bool g_pollRun = true;
static bool g_pollReady = false;        // a fresh result is waiting (guarded)
static int  g_pollFriendReqs = 0;       // (guarded)
static int  g_pollUnread = 0;           // (guarded)
static long g_pollNewestDm = 0;         // (guarded)
static std::string g_pollCfgDir, g_pollUrl, g_pollKey;   // set once before the thread starts

static int PollWorker(void*) {
    while (g_pollRun) {
        long since = g_lastDmId;   // benign read of a long written by the main thread
        try {
            okay::account::AccountService poller(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
            if (poller.IsLoggedIn()) {
                int fr = okay::Friends(poller).PendingRequests();
                long newest = since;
                int unread = okay::Messages(poller).UnreadSince(since, newest);
                if (g_pollMutex) SDL_LockMutex(g_pollMutex);
                g_pollFriendReqs = fr; g_pollUnread = unread; g_pollNewestDm = newest;
                g_pollReady = true;
                if (g_pollMutex) SDL_UnlockMutex(g_pollMutex);
            }
        } catch (...) { /* transient network error — try again next cycle */ }
        // Wait ~30s, in short steps so shutdown is prompt.
        for (int i = 0; i < 150 && g_pollRun; ++i) SDL_Delay(200);
    }
    return 0;
}

// Verify the saved account session on a worker thread so the main thread can
// keep animating the startup splash instead of freezing on the network call.
struct VerifyJob { okay::account::AccountService* acct; volatile bool done; };
static int VerifySessionWorker(void* p) {
    VerifyJob* j = (VerifyJob*)p;
    try { if (j->acct) j->acct->VerifySession(); } catch (...) {}
    j->done = true;
    return 0;
}

// ---- Background Forums fetch -----------------------------------------------
// Opening the Forums tab used to fire four blocking HTTPS calls on the UI
// thread, freezing the tab switch. This worker does them on its own account
// service (same proven pattern as the friends poll) and publishes the results
// under a mutex; the tab renders whatever is cached and never blocks.
static SDL_mutex* g_foMutex = nullptr;
static volatile bool g_foFetching = false;
static bool g_foReady = false;                       // (guarded) fresh results waiting
static std::vector<okay::ForumThread> g_foThreads;   // (guarded)
static std::map<long,long> g_foScores;               // (guarded)
static std::map<long,int>  g_foMy, g_foComments;     // (guarded)
static int ForumFetchWorker(void*) {
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        okay::Forums fo(a);
        auto th = fo.Threads();
        auto sc = fo.Scores("thread");
        auto mv = fo.MyVotes("thread");
        auto cc = fo.CommentCounts();
        if (g_foMutex) SDL_LockMutex(g_foMutex);
        g_foThreads = std::move(th); g_foScores = std::move(sc);
        g_foMy = std::move(mv); g_foComments = std::move(cc);
        g_foReady = true;
        if (g_foMutex) SDL_UnlockMutex(g_foMutex);
    } catch (...) {}
    g_foFetching = false;
    return 0;
}

// --- Forum post detail (comments) fetched off-thread --------------------
// Opening a thread used to block the main thread on 5 network calls. Now a
// worker fetches the thread's posts + vote maps and publishes them; the UI
// shows "Loading comments..." until g_foPostHave matches the open thread.
static volatile bool g_foPostFetching = false;
static bool g_foPostReady = false;                      // (guarded) fresh detail waiting
static long g_foPostHave  = 0;                          // (guarded) thread id in the cache
static std::vector<okay::ForumPost> g_foPosts;          // (guarded)
static std::map<long,long> g_foPostScores, g_foPostThScores;  // (guarded)
static std::map<long,int>  g_foPostMy, g_foPostThMy;          // (guarded)
static int ForumPostWorker(void* arg) {
    long tid = (long)(intptr_t)arg;
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        okay::Forums fo(a);
        auto ps  = fo.Posts(tid);
        auto psc = fo.Scores("post");
        auto pmy = fo.MyVotes("post");
        auto tsc = fo.Scores("thread");
        auto tmy = fo.MyVotes("thread");
        if (g_foMutex) SDL_LockMutex(g_foMutex);
        g_foPosts = std::move(ps); g_foPostScores = std::move(psc); g_foPostMy = std::move(pmy);
        g_foPostThScores = std::move(tsc); g_foPostThMy = std::move(tmy);
        g_foPostHave = tid; g_foPostReady = true;
        if (g_foMutex) SDL_UnlockMutex(g_foMutex);
    } catch (...) {}
    g_foPostFetching = false;
    return 0;
}

// --- Forum actions (vote / reply / new thread) fired off-thread ---------
// The UI updates optimistically and fires the POST here, so clicking an arrow
// or posting a comment never stalls the frame. g_foActionDone asks the UI to
// kick a background refresh so the authoritative counts replace the optimistic
// ones shortly after.
struct FoAction { int type; std::string kind; long id; int value; std::string a, b, c; };
static SDL_atomic_t g_foActionBusy;     // number of in-flight actions (for a subtle hint)
static volatile bool g_foActionDone = false;
static int ForumActionWorker(void* arg) {
    FoAction* j = (FoAction*)arg;
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        okay::Forums fo(a);
        if      (j->type == 0) fo.Vote(j->kind, j->id, j->value);
        else if (j->type == 1) fo.Reply(j->id, j->a, nullptr);
        else if (j->type == 2) fo.StartThread(j->a, j->b, j->c, nullptr);
    } catch (...) {}
    g_foActionDone = true;
    SDL_AtomicAdd(&g_foActionBusy, -1);
    delete j;
    return 0;
}
static void FireForumAction(FoAction* j) {
    SDL_AtomicAdd(&g_foActionBusy, 1);
    if (SDL_Thread* t = SDL_CreateThread(ForumActionWorker, "okay-foact", j)) SDL_DetachThread(t);
    else { SDL_AtomicAdd(&g_foActionBusy, -1); delete j; }
}

// Background Servers list fetch (same idea — opening the Servers tab shouldn't
// block on the network). Only the server LIST is prefetched; channel/message
// loads happen after the user picks a server.
static SDL_mutex* g_svMutex = nullptr;
static volatile bool g_svFetching = false;
static bool g_svReady = false;                       // (guarded)
static std::vector<okay::Server> g_svList;           // (guarded)
static int ServerListWorker(void*) {
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        okay::Servers sv(a);
        auto l = sv.MyServers();
        if (g_svMutex) SDL_LockMutex(g_svMutex);
        g_svList = std::move(l); g_svReady = true;
        if (g_svMutex) SDL_UnlockMutex(g_svMutex);
    } catch (...) {}
    g_svFetching = false;
    return 0;
}

// Background Friends list/feed fetch (the periodic refresh used to hitch the
// Friends tab every 10s on the UI thread).
static SDL_mutex* g_frMutex = nullptr;
static volatile bool g_frFetching = false;
static bool g_frReady = false;                       // (guarded)
static std::vector<okay::Friend> g_frList;           // (guarded)
static std::vector<okay::FriendEvent> g_frFeed;      // (guarded)
static std::string g_frCode;                         // (guarded) my friend code
static int FriendsListWorker(void*) {
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        okay::Friends fr(a);
        // Everything that used to block the Friends tab on open now runs here:
        // make sure my profile exists, broadcast presence per my chosen status,
        // and fetch my code + the list/feed.
        fr.EnsureProfile();
        if (g_onlineStatus == 2) fr.GoOffline();
        else fr.MarkPresence(g_onlineStatus == 1 ? "Away" : "In OkaySpace");
        auto code = fr.MyCode();
        auto l = fr.List();
        auto fd = fr.Feed(14);
        if (g_frMutex) SDL_LockMutex(g_frMutex);
        g_frList = std::move(l); g_frFeed = std::move(fd); g_frCode = std::move(code);
        g_frReady = true;
        if (g_frMutex) SDL_UnlockMutex(g_frMutex);
    } catch (...) {}
    g_frFetching = false;
    return 0;
}

// Background Marketplace list/ratings fetch.
static SDL_mutex* g_wsMutex = nullptr;
static volatile bool g_wsFetching = false;
static bool g_wsReady = false;                       // (guarded)
static std::vector<okay::WorkshopItem> g_wsList;     // (guarded)
static std::map<long, std::pair<long,long>> g_wsRatings;  // (guarded)
static int MarketListWorker(void*) {
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        okay::Workshop ws(a);
        auto l = ws.List();
        auto r = ws.Ratings();
        if (g_wsMutex) SDL_LockMutex(g_wsMutex);
        g_wsList = std::move(l); g_wsRatings = std::move(r); g_wsReady = true;
        if (g_wsMutex) SDL_UnlockMutex(g_wsMutex);
    } catch (...) {}
    g_wsFetching = false;
    return 0;
}

// Background Account info fetch (profile + avatar + friend code). Opening the
// Account tab used to block on three HTTPS calls; this fetches them once on a
// worker and the tab applies the results when they arrive. g_acReady stays set
// so each of the three consumers can pick up its piece on a later frame.
static SDL_mutex* g_acMutex = nullptr;
static volatile bool g_acStarted = false;
static bool g_acReady = false;                 // (guarded)
static okay::Friend g_acProfile;               // (guarded)
static std::string g_acAvatar, g_acCode;       // (guarded)
static int AccountInfoWorker(void*) {
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        okay::Friends fr(a);
        auto prof = fr.MyProfile();
        auto av   = fr.MyAvatar();
        auto code = fr.MyCode();
        if (g_acMutex) SDL_LockMutex(g_acMutex);
        g_acProfile = std::move(prof); g_acAvatar = std::move(av); g_acCode = std::move(code);
        g_acReady = true;
        if (g_acMutex) SDL_UnlockMutex(g_acMutex);
    } catch (...) {}
    return 0;
}

// ---- Background chat poll (worker; state declared before DrawChatWindow) ----
static int ChatPollWorker(void*) {
    okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
    okay::Messages dm(a); okay::Friends fr(a);
    Uint32 nextLog = 0, nextTyping = 0, nextConv = 0, nextFri = 0, nextGroups = 0, nextGLog = 0;
    std::string lastUid; long lastGid = 0;
    while (g_chatPollRun) {
        if (!g_chatWinOpen) { SDL_Delay(200); continue; }
        Uint32 now = SDL_GetTicks();
        std::string uid; long gid = 0;
        if (g_chatMutex) { SDL_LockMutex(g_chatMutex); uid = g_chatSelUid; gid = g_chatSelGroup; SDL_UnlockMutex(g_chatMutex); }
        if (uid != lastUid) { lastUid = uid; nextLog = 0; nextTyping = 0; }   // conversation changed → poll now
        if (gid != lastGid) { lastGid = gid; nextGLog = 0; }                  // group switched → poll now
        if (g_chatForceLog) { g_chatForceLog = false; nextLog = 0; nextConv = 0; nextGLog = 0; nextGroups = 0; }  // a user action wants a fresh view
        try {
            if (now >= nextConv) {
                auto c = dm.Conversations();
                if (g_chatMutex) { SDL_LockMutex(g_chatMutex); g_chatConvs = std::move(c); g_chatConvReady = true; SDL_UnlockMutex(g_chatMutex); }
                nextConv = now + 6000;
            }
            if (now >= nextGroups) {
                auto gs = dm.MyGroups();
                if (g_chatMutex) { SDL_LockMutex(g_chatMutex); g_chatGroups = std::move(gs); g_chatGroupsReady = true; SDL_UnlockMutex(g_chatMutex); }
                nextGroups = now + 6000;
            }
            if (gid > 0 && now >= nextGLog) {
                auto gl = dm.GroupThread(gid);
                auto grc = dm.GroupReactions(); auto gmr = dm.GroupMyReactions();
                long markId = gl.empty() ? 0 : gl.back().id;
                if (markId) dm.MarkGroupRead(gid, markId);
                if (g_chatMutex) {
                    SDL_LockMutex(g_chatMutex);
                    g_chatGLog = std::move(gl); g_chatGReacts = std::move(grc); g_chatGMyReacts = std::move(gmr);
                    g_chatGLogReady = true;
                    SDL_UnlockMutex(g_chatMutex);
                }
                nextGLog = now + 4000;
            }
            if (now >= nextFri) {
                auto f = fr.List();
                if (g_chatMutex) { SDL_LockMutex(g_chatMutex); g_chatFriends = std::move(f); g_chatFriReady = true; SDL_UnlockMutex(g_chatMutex); }
                nextFri = now + 15000;
            }
            if (!uid.empty()) {
                if (g_chatTypingReq) { g_chatTypingReq = false; dm.SetTyping(uid); }
                if (now >= nextLog) {
                    auto l = dm.With(uid);
                    auto rc = dm.Reactions(); auto mr = dm.MyReactions();
                    long pr = dm.PeerReadId(uid);
                    long markId = l.empty() ? 0 : l.back().id;
                    if (markId) dm.MarkRead(uid, markId);
                    if (g_chatMutex) {
                        SDL_LockMutex(g_chatMutex);
                        g_chatLog = std::move(l); g_chatReacts = std::move(rc); g_chatMyReacts = std::move(mr);
                        g_chatPeerRead = pr; g_chatLogReady = true;
                        SDL_UnlockMutex(g_chatMutex);
                    }
                    nextLog = now + 4000;
                }
                if (now >= nextTyping) {
                    g_chatPeerTyping = dm.PeerTyping(uid);
                    nextTyping = now + 2500;
                }
            }
        } catch (...) { /* transient — retry next tick */ }
        SDL_Delay(250);
    }
    return 0;
}

// ---- Background Servers poll ------------------------------------------------
// With a server selected, the Servers tab polled channels (15s), members (20s)
// and messages+likes (every 4s = 3 calls) on the UI thread — periodic hitches
// ("the server tab is laggy"). A worker thread (own account service) now polls
// the selected server/channel and publishes under a mutex. It only runs while
// the tab is actually on screen (g_srvActiveMs is bumped each frame). User
// actions (send/create/like) stay on the main thread and set g_srvForce.
static SDL_mutex*  g_srvMutex = nullptr;
static SDL_Thread* g_srvThread = nullptr;
static volatile bool g_srvPollRun = true;
static volatile bool g_srvForce = false;
static volatile long g_srvSel = 0, g_srvChan = 0;    // inputs set by the main thread
static volatile Uint32 g_srvActiveMs = 0;            // last frame the Servers tab rendered
static bool g_srvChanReady = false, g_srvMemReady = false, g_srvMsgReady = false;  // (guarded)
static std::vector<okay::Channel> g_srvChannels;      // (guarded)
static std::vector<okay::ServerMember> g_srvMembers;  // (guarded)
static std::vector<okay::ChannelMessage> g_srvMsgs;   // (guarded)
static std::map<long,long> g_srvLikes;                // (guarded)
static std::map<long,bool> g_srvMyLikes;              // (guarded)
static int ServerPollWorker(void*) {
    okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
    okay::Servers sv(a);
    Uint32 nextChan = 0, nextMem = 0, nextMsg = 0;
    long lastSel = 0, lastChan = 0;
    while (g_srvPollRun) {
        Uint32 now = SDL_GetTicks();
        if (now - g_srvActiveMs > 2500) { SDL_Delay(200); continue; }   // only while viewing Servers
        long sel = g_srvSel, chan = g_srvChan;
        if (sel != lastSel)  { lastSel = sel;  nextChan = 0; nextMem = 0; }
        if (chan != lastChan) { lastChan = chan; nextMsg = 0; }
        if (g_srvForce) { g_srvForce = false; nextChan = 0; nextMem = 0; nextMsg = 0; }
        try {
            if (sel != 0) {
                if (now >= nextChan) {
                    auto c = sv.Channels(sel);
                    if (g_srvMutex) { SDL_LockMutex(g_srvMutex); g_srvChannels = std::move(c); g_srvChanReady = true; SDL_UnlockMutex(g_srvMutex); }
                    nextChan = now + 15000;
                }
                if (now >= nextMem) {
                    auto m = sv.Members(sel);
                    if (g_srvMutex) { SDL_LockMutex(g_srvMutex); g_srvMembers = std::move(m); g_srvMemReady = true; SDL_UnlockMutex(g_srvMutex); }
                    nextMem = now + 20000;
                }
            }
            if (chan != 0 && now >= nextMsg) {
                auto ms = sv.Messages(chan); auto lk = sv.Likes(); auto ml = sv.MyLikes();
                if (g_srvMutex) {
                    SDL_LockMutex(g_srvMutex);
                    g_srvMsgs = std::move(ms); g_srvLikes = std::move(lk); g_srvMyLikes = std::move(ml);
                    g_srvMsgReady = true;
                    SDL_UnlockMutex(g_srvMutex);
                }
                nextMsg = now + 4000;
            }
        } catch (...) { /* transient — retry next tick */ }
        SDL_Delay(250);
    }
    return 0;
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

    // Windows 11 / high-DPI: declare per-monitor-DPI-aware BEFORE SDL video init so
    // the launcher renders crisp at 125%/150% scaling instead of bitmap-stretched.
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;

    // One launcher at a time: a second launch asks the running one to come
    // forward (restoring it from the tray / taskbar) and then exits quietly.
    if (!AcquireSingleInstance()) { SDL_Quit(); return 0; }

    // Show the bundled version and DON'T phone home on startup: the published build
    // on GitHub can lag this one, so an auto-check used to download an OLDER engine
    // and mislead with "up to date". Updates are now opt-in via the button below.
    SDL_AtomicSet(&g_upState, (int)Up_UpToDate);
    g_upMutex = SDL_CreateMutex();
    g_dlMutex = SDL_CreateMutex();
    g_gitMutex = SDL_CreateMutex();
    SetUpMsg("OkaySpace v" + LocalVersion());

    LoadPrefs();                 // preferences (incl. last window size) from launcher.cfg
    // Create the window HIDDEN so the OS never composites a black, uninitialized
    // rectangle during the window + renderer setup gap. We draw the first splash
    // frame while hidden, then reveal the window already showing the splash, so
    // the very first thing the user sees is the branded loading screen.
    SDL_Window* window = SDL_CreateWindow("OkaySpace Launcher  v" OKAY_ENGINE_VERSION,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, g_winW, g_winH,
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (!window) return 1;
    SDL_SetWindowMinimumSize(window, 720, 480);   // the fixed layout needs this much
    okay::SetAppIcon(window);   // placeholder OkaySpace logo
#if defined(_WIN32)
    SDL_SetWindowsMessageHook(OkayMsgHook, nullptr);   // receive tray-icon clicks
#endif
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) return 1;
    g_renderer = renderer;   // lets GameCover() upload cover-art textures
    // Paint a loading frame into the back buffer, reveal the window, then present
    // again so the reveal lands on the splash (not a black frame), whatever the
    // platform's show-vs-present ordering.
    // Load the user's custom splash logo (if any) so the first frame can show it.
    if (!g_splashLogo.empty()) {
        int lw = 0, lh = 0, lch = 0;
        if (unsigned char* px = stbi_load(g_splashLogo.c_str(), &lw, &lh, &lch, 4)) {
            if (SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(px, lw, lh, 32, lw * 4, SDL_PIXELFORMAT_ABGR8888)) {
                g_splashLogoTex = SDL_CreateTextureFromSurface(renderer, s);
                SDL_FreeSurface(s);
            }
            stbi_image_free(px);
        }
    }
    // Build the splash style from the user's loading-screen preferences.
    auto splashStyle = [&]() {
        okay::SplashStyle st;
        int ai = g_accentIndex;
        int an = (int)(sizeof(kAccentPresets) / sizeof(kAccentPresets[0]));
        if (ai < 0 || ai >= an) ai = 0;
        if (g_splashAccentOverride) { st.ar = g_splashAccent[0]; st.ag = g_splashAccent[1]; st.ab = g_splashAccent[2]; }
        else { st.ar = kAccentPresets[ai].col.x; st.ag = kAccentPresets[ai].col.y; st.ab = kAccentPresets[ai].col.z; }
        auto C = [](const float* c) -> SDL_Color {
            auto u = [](float v){ return (Uint8)((v < 0 ? 0 : v > 1 ? 1 : v) * 255.0f + 0.5f); };
            return SDL_Color{u(c[0]), u(c[1]), u(c[2]), 255}; };
        st.bgTop = C(g_splashBgTop); st.bgBottom = C(g_splashBgBot);
        st.logo = g_splashLogoTex; st.showProgress = g_splashShowBar;
        return st;
    };
    auto firstSplash = [&]() {
        int sw = g_winW, sh = g_winH; SDL_GetRendererOutputSize(renderer, &sw, &sh);
        okay::DrawSplashStyled(renderer, sw, sh, splashStyle());
    };
    firstSplash();                 // draw + present while still hidden
    SDL_ShowWindow(window);        // reveal with the splash already composited
    SDL_RaiseWindow(window);
    firstSplash();                 // present once more so the shown frame is the splash

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
    // Fall back to values baked into the build. Ignore an empty or junk server
    // file (e.g. a stale "Use local (clear)" leftover from an old build) that
    // would otherwise knock the launcher offline — only an http(s) URL counts.
    if (serverUrl.rfind("http", 0) != 0) serverUrl = OKAY_DEFAULT_ACCOUNT_URL;
    if (apiKey.empty())                  apiKey    = OKAY_DEFAULT_ACCOUNT_KEY;
    // Held by pointer so the Settings tab can rebuild it live when the server
    // config changes.
    fs::path acctCfgDir = acct::DefaultConfigDir(fs::path(g_exeDir));
    auto accountPtr = std::make_unique<acct::AccountService>(acctCfgDir, serverUrl, apiKey);
    g_presenceAccount = accountPtr.get();   // RecordPlayed broadcasts playing-presence through this
    // If we resumed a saved session against an online server, make sure it's
    // still valid (the token may have been revoked/expired); this signs the
    // player out if so. Offline or local accounts are left as-is.
    // Run it on a worker thread and keep animating the splash so startup stays
    // smooth instead of freezing on the network round-trip.
    {
        VerifyJob vj{ accountPtr.get(), false };
        SDL_Thread* vt = SDL_CreateThread(VerifySessionWorker, "okay-verify", &vj);
        while (!vj.done) {
            SDL_PumpEvents();   // keep the window responsive (and mapped) while we wait
            int sw = g_winW, sh = g_winH; SDL_GetRendererOutputSize(renderer, &sw, &sh);
            okay::DrawSplashStyled(renderer, sw, sh, splashStyle());
            SDL_Delay(16);
        }
        if (vt) SDL_WaitThread(vt, nullptr);
    }

    // Start the background friends/DM poll (its own service, read from disk each
    // cycle) so the nav badges never block the UI on the network.
    g_pollCfgDir = acctCfgDir.string(); g_pollUrl = serverUrl; g_pollKey = apiKey;
    g_pollMutex = SDL_CreateMutex();
    g_pollThread = SDL_CreateThread(PollWorker, "okay-poll", nullptr);
    // Background chat poller (messages/typing/conversations) so the Chat window
    // never blocks the UI on the network.
    g_chatMutex = SDL_CreateMutex();
    g_chatThread = SDL_CreateThread(ChatPollWorker, "okay-chat", nullptr);
    // Background Servers poller (channels/members/messages for the open server).
    g_srvMutex = SDL_CreateMutex();
    g_srvThread = SDL_CreateThread(ServerPollWorker, "okay-srv", nullptr);

    // The account server is baked into this build (OKAY_DEFAULT_ACCOUNT_URL /
    // account_server.txt), so the launcher no longer exposes a server-URL field
    // or any way to change it in the UI — it just uses whatever was configured.
    char acctUser[64] = {0};
    char acctPass[64] = {0};
    char acctName[64] = {0};             // username (handle) on register
    char acctDisplay[64] = {0};          // display name on register (optional)
    bool acctShowPass = false;           // show/hide the password field
    char acctNewPass[64] = {0};          // change-password field (signed in)
    char acctChgName[64] = {0};          // change-username field (signed in)
    char acctChgDisplay[64] = {0};       // change-display-name field (signed in)
    char acctAvatar[256] = {0};          // avatar image URL field (signed in)
    char acctBio[280] = {0};             // profile bio field (signed in)
    char acctStatus[96] = {0};           // custom status / tagline (signed in)
    char acctFavGame[96] = {0};          // favorite-game showcase (signed in)
    char acctLocation[64] = {0};         // free-form location (signed in)
    int  acctAccent = 0;                 // chosen profile accent index
    bool acctProfileLoaded = false;      // customization fields fetched once
    char acctChgEmail[64] = {0};         // change-email field (signed in)
    bool acctRegisterMode = false;       // false = sign in, true = create account
    std::string acctMessage;             // last error/status, shown under the form
    bool acctMessageError = true;
    bool acctBusy = false;

    char playFilter[128] = {0};   // Play-tab search box
    char marketFilter[128] = {0}; // Marketplace search box
    char commFilter[128] = {0};   // Community library search box
    std::string commRemovePath;   // community item pending delete-confirmation
    std::string commRemoveName;
    // Reopen on the section the launcher was last closed on.
    int tab = (g_lastTab >= 0 && g_lastTab < 10) ? g_lastTab : 0;
    bool focusSearch = false;     // Ctrl+F jumps to the current tab's search box
    if (g_updateOnLaunch) StartUpdateCheck(false);   // opt-in auto-check at startup (no dialog)
    bool running = true;
    bool closeConfirm = false;    // the [X] raises an Exit / Minimize confirm dialog
    while (running) {
        Uint64 frameStart = SDL_GetPerformanceCounter();   // for the frame-rate cap below
        // Current account service for this frame. Held by reference so existing
        // code reads naturally; the Settings tab may rebuild accountPtr (which
        // is why the Settings tab uses accountPtr-> directly, not this alias).
        acct::AccountService& account = *accountPtr;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            // Clicking the window's [X] (or Alt+F4) asks whether to exit or just
            // minimize, rather than quitting outright.
            if (e.type == SDL_QUIT) closeConfirm = true;
            // NOTE: minimizing does a plain Windows minimize (to the taskbar) and
            // is always restored by clicking the taskbar button. The launcher does
            // NOT hide itself to the system tray — that could trap the window with
            // no way to reopen or close it, so the behavior was removed.
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
                if ((ext == ".html" || ext == ".htm") && !isDir) {
                    // A web game: play it straight in the default browser. (Loose
                    // .html files keep their sibling js/assets, so we open in place
                    // rather than copying.)
                    OpenExternal(dropped);
                    Toast("Opening web game in your browser", 1);
                    tab = 1;
                } else if (ext == ".okayscene" && !isDir && !player.empty()) {
                    Launch(player, dropped);
                    RecordPlayed(dropped); SavePrefs();
                    Toast("Playing dropped game");
                    tab = 1;
                } else if (isDir || ext == ".okay" || ext == ".okayvs" || ext == ".obj" ||
                           ext == ".png" || ext == ".jpg" || ext == ".okayscene") {
                    std::string out = InstallToCommunity(dropped);
                    if (!out.empty()) {
                        scenes = FindScenes(); g_webNeedRescan = true;   // a dropped folder may be a web game
                        // If the installed folder is a web game, jump to Play so it's visible.
                        std::error_code wec;
                        bool web = fs::exists(fs::path(out) / "index.html", wec) || fs::exists(fs::path(out) / "index.htm", wec);
                        Toast(web ? "Added a web game \xE2\x80\x94 see Play" : "Added to your community library", 1);
                        tab = web ? 1 : 2;
                    }
                    else Toast("Couldn't install that item", 2);
                } else {
                    Toast("Drop a game folder, .okayscene, or an .html web game");
                }
            }
        }
#if defined(_WIN32)
        // A right-click on the tray icon raises the Open/Quit menu (runs here, not
        // in the message hook, because TrackPopupMenu blocks).
        if (g_trayMenuReq) { g_trayMenuReq = false; OkayTrayShowMenu(); }
        if (g_trayQuitReq) { g_trayQuitReq = false; OkayTrayRemove(); running = false; }
        // A tray-icon click (or a second launch) restores the launcher.
        if (g_trayRestoreReq) {
            g_trayRestoreReq = false;
            OkayTrayRemove();
            SDL_ShowWindow(window);
            SDL_RestoreWindow(window);
            SDL_RaiseWindow(window);
        }
#endif
        // A finished git job (clone/pull/push) reports its result as a toast.
        if (g_gitMutex) {
            SDL_LockMutex(g_gitMutex);
            if (g_gitResultReady) {
                g_gitResultReady = false;
                Toast(g_gitResultMsg, g_gitResultKind);
                if (g_gitWantRescan) { g_gitWantRescan = false; g_projectsNeedRescan = true; }
            }
            SDL_UnlockMutex(g_gitMutex);
        }
        // Once a background download finishes, re-detect the runtimes so the
        // "not found" notices clear without needing a restart.
        if (GetState() == Up_Updated && !rescannedAfterUpdate) {
            editor = FindExe({"OkayEngine.exe", "OkaySpaceEngine.exe", "okay-editor.exe", "okay-editor"});
            player = FindExe({"OkaySpacePlayer.exe", "okay-player.exe", "okay-player"});
            scenes = FindScenes();
            rescannedAfterUpdate = true;
        }
        // Apply the latest friends/DM counts published by the background poll
        // thread (no network on the main thread → no hitch), and toast on new ones.
        {
            static int lastReqSeen = -1;
            if (account.IsLoggedIn()) {
                bool apply = false; int fr = 0, unread = 0; long newest = 0;
                if (g_pollMutex) {
                    SDL_LockMutex(g_pollMutex);
                    if (g_pollReady) { apply = true; fr = g_pollFriendReqs; unread = g_pollUnread; newest = g_pollNewestDm; g_pollReady = false; }
                    SDL_UnlockMutex(g_pollMutex);
                }
                if (apply) {
                    if (lastReqSeen >= 0 && fr > lastReqSeen && tab != 6)
                        Toast(fr == 1 ? "New friend request" : "New friend requests", 1);
                    lastReqSeen = fr;
                    g_friendRequests = fr;
                    g_newestDmId = newest;
                    if (unread > g_dmUnread && tab != 6) Toast("New message", 1);
                    g_dmUnread = unread;
                }
            } else {
                g_friendRequests = 0; lastReqSeen = -1; g_dmUnread = 0;
            }
        }
        // Toast once when an update check finishes.
        {
            static int lastUp = -1;
            int us = (int)GetState();
            if (us != lastUp) {
                if (us == (int)Up_Updated) Toast(g_upRelaunchNeeded ? "Updated — restarting..."
                                                                     : "Update installed", 1);
                else if (us == (int)Up_Failed) Toast("Update check failed", 2);
                else if (us == (int)Up_UpToDate && lastUp == (int)Up_Checking) Toast("You're up to date", 1);
                lastUp = us;
            }
        }
        // Auto-restart the launcher after it updates itself (the new exe is already
        // swapped in). Give the toast a moment, then relaunch and quit.
        if (g_upRelaunchNeeded && GetState() == Up_Updated) {
            static Uint32 relaunchAt = 0;
            if (relaunchAt == 0) relaunchAt = SDL_GetTicks() + 1600;
            else if (SDL_GetTicks() >= relaunchAt) { Launch(g_selfPath); running = false; }
        }

        // While minimized or hidden, idle cheaply instead of spinning the render
        // loop (there's nothing to draw until the window is restored).
        if ((SDL_GetWindowFlags(window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) != 0) {
            SDL_Delay(120);
            continue;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // ---- Exit / Minimize confirmation (raised by the window [X] or Alt+F4) ----
        // Closing the launcher asks first, so an accidental [X] doesn't quit mid-task;
        // "Minimize" is a normal taskbar minimize that always restores.
        if (closeConfirm && !ImGui::IsPopupOpen("Close OkaySpace?"))
            ImGui::OpenPopup("Close OkaySpace?");
        {
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Close OkaySpace?", nullptr,
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::TextUnformatted("Do you want to exit OkaySpace, or just minimize it?");
                ImGui::Dummy(ImVec2(0, 10));
                if (ImGui::Button("Exit", ImVec2(120, 0))) {
                    running = false; closeConfirm = false; ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Minimize", ImVec2(120, 0))) {
                    SDL_MinimizeWindow(window); closeConfirm = false; ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)) ||
                    ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                    closeConfirm = false; ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        // ---- Software Update dialog ----------------------------------------
        // A real update window: version from -> to, stepped progress, a live
        // download bar in MB, elapsed time and the changelog. Opened by the
        // "Check for updates" button (StartUpdateCheck sets g_upModalOpen).
        if (g_upModalOpen && !ImGui::IsPopupOpen("Software Update"))
            ImGui::OpenPopup("Software Update");
        {
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("Software Update", nullptr,
                    ImGuiWindowFlags_NoSavedSettings)) {
                UpState us = GetState();
                bool busy = (us == Up_Checking || us == Up_Downloading);
                std::string fromV, toV; GetUpVers(fromV, toV);
                ImDrawList* mdl = ImGui::GetWindowDrawList();

                // Header: a small accent cube glyph + title + sub-line.
                ImVec2 hp = ImGui::GetCursorScreenPos();
                float cs = 20.0f, cx0 = hp.x + cs * 0.5f, cy0 = hp.y + cs * 0.6f;
                ImU32 acol = ImGui::GetColorU32(kAccent);
                mdl->AddQuadFilled(ImVec2(cx0, cy0 - 9), ImVec2(cx0 + 10, cy0 - 3),
                                   ImVec2(cx0, cy0 + 3), ImVec2(cx0 - 10, cy0 - 3), acol);
                mdl->AddQuadFilled(ImVec2(cx0 - 10, cy0 - 3), ImVec2(cx0, cy0 + 3),
                                   ImVec2(cx0, cy0 + 13), ImVec2(cx0 - 10, cy0 + 7),
                                   ImGui::GetColorU32(ImVec4(kAccent.x*0.55f,kAccent.y*0.55f,kAccent.z*0.55f,1)));
                mdl->AddQuadFilled(ImVec2(cx0, cy0 + 3), ImVec2(cx0 + 10, cy0 - 3),
                                   ImVec2(cx0 + 10, cy0 + 7), ImVec2(cx0, cy0 + 13),
                                   ImGui::GetColorU32(ImVec4(kAccent.x*0.78f,kAccent.y*0.78f,kAccent.z*0.78f,1)));
                ImGui::SetCursorScreenPos(ImVec2(hp.x + cs + 12, hp.y));
                ImGui::BeginGroup();
                ImGui::TextUnformatted(
                    us == Up_Updated  ? "Update installed" :
                    us == Up_Failed   ? "Update failed" :
                    us == Up_UpToDate ? "You're up to date" :
                    us == Up_Downloading ? "Downloading update" : "Checking for updates");
                if (!fromV.empty()) {
                    if (!toV.empty() && toV != fromV)
                        ImGui::TextDisabled("v%s  \xE2\x86\x92  v%s", fromV.c_str(), toV.c_str());
                    else
                        ImGui::TextDisabled("Installed: v%s", fromV.c_str());
                }
                ImGui::EndGroup();
                ImGui::Dummy(ImVec2(0, 6));
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0, 4));

                // Current step label + animated spinner while busy.
                if (busy) {
                    ImVec2 sp = ImGui::GetCursorScreenPos();
                    float rad = 7.0f; ImVec2 ctr(sp.x + rad + 1, sp.y + rad + 2);
                    float a0 = (float)ImGui::GetTime() * 6.0f;
                    mdl->PathArcTo(ctr, rad, a0, a0 + 4.6f, 20);
                    mdl->PathStroke(acol, 0, 2.6f);
                    ImGui::Dummy(ImVec2(rad * 2 + 6, rad * 2 + 2)); ImGui::SameLine();
                }
                ImGui::TextUnformatted(GetUpStep().c_str());

                // Overall step progress (engine / player / launcher).
                int si = SDL_AtomicGet(&g_upStepIdx), stt = SDL_AtomicGet(&g_upStepTotal);
                if (stt > 0 && us == Up_Downloading) {
                    float sf = (float)si / (float)stt;
                    ImGui::ProgressBar(sf, ImVec2(-FLT_MIN, 6.0f), "");
                }

                // Live download bar for the current file (MB + %).
                long long bn = (long long)(unsigned)SDL_AtomicGet(&g_upBytesNow);
                long long bt = (long long)(unsigned)SDL_AtomicGet(&g_upBytesTotal);
                if (us == Up_Downloading) {
                    char ov[64];
                    double mbN = (double)bn / (1024.0 * 1024.0);
                    if (bt > 0) {
                        double mbT = (double)bt / (1024.0 * 1024.0);
                        float f = (float)bn / (float)bt; if (f > 1) f = 1;
                        std::snprintf(ov, sizeof(ov), "%.1f / %.1f MB  (%.0f%%)", mbN, mbT, f * 100.0f);
                        ImGui::ProgressBar(f, ImVec2(-FLT_MIN, 18.0f), ov);
                    } else {
                        // Unknown size: indeterminate sweep + MB-so-far readout.
                        float t = (float)ImGui::GetTime();
                        float f = 0.5f + 0.5f * std::sin(t * 2.2f);
                        std::snprintf(ov, sizeof(ov), "%.1f MB", mbN);
                        ImGui::ProgressBar(f, ImVec2(-FLT_MIN, 18.0f), ov);
                    }
                }

                // Elapsed time.
                int startMs = SDL_AtomicGet(&g_upStartMs);
                if (startMs && us != Up_Idle) {
                    int el = ((int)SDL_GetTicks() - startMs) / 1000; if (el < 0) el = 0;
                    ImGui::TextDisabled("Elapsed: %d:%02d", el / 60, el % 60);
                }

                // Changelog / what's-new.
                std::string cl = GetUpChangelog();
                if (!cl.empty()) {
                    ImGui::Dummy(ImVec2(0, 4));
                    ImGui::TextDisabled("What's new%s%s", toV.empty() ? "" : " in v",
                                        toV.empty() ? "" : toV.c_str());
                    ImGui::BeginChild("##upchangelog", ImVec2(0, 120), true);
                    // One bullet per line.
                    std::size_t s0 = 0;
                    while (s0 < cl.size()) {
                        std::size_t e0 = cl.find('\n', s0);
                        std::string ln = cl.substr(s0, e0 == std::string::npos ? std::string::npos : e0 - s0);
                        if (!ln.empty()) {
                            ImGui::TextColored(kAccent, "\xE2\x80\xA2"); ImGui::SameLine();
                            ImGui::PushTextWrapPos(0.0f);
                            ImGui::TextUnformatted(ln.c_str());
                            ImGui::PopTextWrapPos();
                        }
                        if (e0 == std::string::npos) break; s0 = e0 + 1;
                    }
                    ImGui::EndChild();
                }

                ImGui::Dummy(ImVec2(0, 8));
                // Action buttons depend on state.
                if (us == Up_Updated && g_upRelaunchNeeded) {
                    if (ImGui::Button("Restart now", ImVec2(140, 34))) {
                        Launch(g_selfPath); running = false;
                        g_upModalOpen = false; ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Later", ImVec2(110, 34))) {
                        g_upModalOpen = false; ImGui::CloseCurrentPopup();
                    }
                } else if (us == Up_Failed) {
                    if (ImGui::Button("Retry", ImVec2(130, 34))) StartUpdateCheck();
                    ImGui::SameLine();
                    if (ImGui::Button("Close", ImVec2(110, 34))) {
                        g_upModalOpen = false; ImGui::CloseCurrentPopup();
                    }
                } else if (busy) {
                    if (ImGui::Button("Hide", ImVec2(110, 34))) {
                        g_upModalOpen = false; ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Keep updating in the background");
                } else {   // UpToDate / Idle
                    if (ImGui::Button("Close", ImVec2(130, 34)) ||
                        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                        g_upModalOpen = false; ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndPopup();
            }
        }

        // ---- Notifications flyout ------------------------------------------
        // A left-anchored panel listing the notifications feed. Opened by the
        // sidebar bell; closes on its [x], Escape, or clicking away.
        if (g_notifPanelOpen) {
            ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 240, vp->WorkPos.y + 62), ImGuiCond_Appearing);
            ImGui::SetNextWindowSize(ImVec2(340, 420), ImGuiCond_Appearing);
            ImGui::SetNextWindowBgAlpha(0.98f);
            bool open = true;
            if (ImGui::Begin("Notifications", &open,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoDocking)) {
                if (g_notifs.empty()) {
                    ImGui::Dummy(ImVec2(0, 24));
                    float aw = ImGui::GetContentRegionAvail().x;
                    const char* msg = "You're all caught up";
                    ImGui::SetCursorPosX((aw - ImGui::CalcTextSize(msg).x) * 0.5f + ImGui::GetCursorPosX());
                    ImGui::TextDisabled("%s", msg);
                } else {
                    ImGui::TextDisabled("%d notification%s", (int)g_notifs.size(),
                                        g_notifs.size() == 1 ? "" : "s");
                    ImGui::SameLine();
                    float aw = ImGui::GetContentRegionAvail().x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + aw - 80);
                    if (ImGui::SmallButton("Clear all")) g_notifs.clear();
                    ImGui::Separator();
                    ImGui::BeginChild("##notiflist", ImVec2(0, 0), false);
                    int closeAfterTab = -1;
                    for (int ni = 0; ni < (int)g_notifs.size(); ++ni) {
                        Notif& n = g_notifs[ni];
                        ImGui::PushID(ni);
                        ImVec2 rp = ImGui::GetCursorScreenPos();
                        float rw = ImGui::GetContentRegionAvail().x;
                        bool click = ImGui::InvisibleButton("##n", ImVec2(rw, 52));
                        bool hov = ImGui::IsItemHovered();
                        ImDrawList* ndl = ImGui::GetWindowDrawList();
                        if (hov) ndl->AddRectFilled(rp, ImVec2(rp.x + rw, rp.y + 52), IM_COL32(255,255,255,12), 6.0f);
                        // Kind dot.
                        ImU32 kc = n.kind == 1 ? IM_COL32(90, 170, 250, 255) :   // friend
                                   n.kind == 2 ? IM_COL32(120, 210, 120, 255) :  // message
                                   n.kind == 3 ? ImGui::GetColorU32(kAccent) :    // update
                                                 IM_COL32(200, 180, 90, 255);     // system
                        ndl->AddCircleFilled(ImVec2(rp.x + 14, rp.y + 16), 4.0f, kc);
                        ndl->AddText(ImVec2(rp.x + 28, rp.y + 8),
                                     ImGui::GetColorU32(ImGuiCol_Text), n.title.c_str());
                        ndl->AddText(ImVec2(rp.x + 28, rp.y + 27),
                                     ImGui::GetColorU32(ImGuiCol_TextDisabled), n.body.c_str());
                        // Relative time from session ticks.
                        Uint32 ago = (SDL_GetTicks() - n.time) / 1000;
                        char rt[24];
                        if (ago < 10) std::snprintf(rt, sizeof(rt), "now");
                        else if (ago < 60) std::snprintf(rt, sizeof(rt), "%us", ago);
                        else if (ago < 3600) std::snprintf(rt, sizeof(rt), "%um", ago / 60);
                        else std::snprintf(rt, sizeof(rt), "%uh", ago / 3600);
                        ImVec2 ts = ImGui::CalcTextSize(rt);
                        ndl->AddText(ImVec2(rp.x + rw - ts.x - 4, rp.y + 8),
                                     ImGui::GetColorU32(ImGuiCol_TextDisabled), rt);
                        if (click && n.gotoTab >= 0) { closeAfterTab = n.gotoTab; }
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                    if (closeAfterTab >= 0) { tab = closeAfterTab; g_notifPanelOpen = false; }
                }
            }
            ImGui::End();
            if (!open || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) g_notifPanelOpen = false;
        }

        // Keyboard shortcuts: 1-5 switch tabs (when not typing in a field);
        // Ctrl+F focuses the current tab's search box; Esc clears every search.
        if (!closeConfirm && !ImGui::GetIO().WantTextInput) {
            for (int i = 0; i < 9; ++i)
                if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + i), false)) tab = i;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                playFilter[0] = commFilter[0] = marketFilter[0] = '\0';
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
            focusSearch = true;
        g_lastTab = tab;   // persisted on exit (and every SavePrefs)

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        // The root window fills the OS client area edge-to-edge (no rounding or
        // outer padding) so there's no odd dark border; panels inside it keep
        // their own rounding and padding.
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##launcher", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar(2);

        // (Removed the soft accent glow gradient across the top — a flat, Steam-style
        // dark surface reads more like a real app and less "designed".)

        // ---- Left nav (collapsible to an icon-only rail) ----
        const float kNavExpandedW = 224.0f, kNavCollapsedW = 58.0f;
        const float navW = g_navCollapsed ? kNavCollapsedW : kNavExpandedW;
        ImGui::BeginChild("nav", ImVec2(navW, 0), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::Dummy(ImVec2(0, 6));
        {   // App header: drawn OkaySpace logo mark (ringed planet + star on a
            // navy tile) + product name / version (hidden when collapsed).
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                                 (g_navCollapsed ? (navW - 34.0f) * 0.5f - 4.0f : 8.0f));
            ImVec2 lp = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float S = 34.0f;
            ImVec2 br(lp.x + S, lp.y + S);
            dl->AddRectFilledMultiColor(lp, br,
                IM_COL32(40, 40, 44, 255), IM_COL32(40, 40, 44, 255),
                IM_COL32(54, 54, 58, 255), IM_COL32(54, 54, 58, 255));
            dl->AddRect(lp, br, IM_COL32(255, 255, 255, 24), 9.0f, 0, 1.0f);
            // Clip the glyph to the rounded tile.
            dl->PushClipRect(lp, br, true);
            ImVec2 c(lp.x + S * 0.46f, lp.y + S * 0.52f);
            ImU32 planet = IM_COL32(176, 180, 190, 255);
            ImU32 ringCol = IM_COL32(220, 232, 255, 230);
            dl->AddCircleFilled(c, 7.5f, planet, 24);
            dl->AddCircleFilled(ImVec2(c.x - 2.2f, c.y - 2.2f), 7.5f,
                                IM_COL32(255, 255, 255, 40), 24);   // top-left sheen
            // Ring: a thin flattened ellipse across the planet.
            ImVec2 ring[28];
            for (int k = 0; k < 28; ++k) {
                float a = k * (6.2831853f / 28);
                float rx = 13.5f, ry = 4.6f, rot = -0.5f;
                float ex = std::cos(a) * rx, ey = std::sin(a) * ry;
                ring[k] = ImVec2(c.x + ex * std::cos(rot) - ey * std::sin(rot),
                                 c.y + ex * std::sin(rot) + ey * std::cos(rot));
            }
            dl->AddPolyline(ring, 28, ringCol, ImDrawFlags_Closed, 1.6f);
            dl->AddCircleFilled(ImVec2(lp.x + S * 0.76f, lp.y + S * 0.26f), 1.3f, IM_COL32(255, 255, 255, 230));
            dl->PopClipRect();
            ImGui::Dummy(ImVec2(S + (g_navCollapsed ? 0.0f : 8.0f), S));
            if (!g_navCollapsed) {
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::Text("OkaySpace");
                ImGui::TextDisabled("v%s", OKAY_ENGINE_VERSION);
                ImGui::EndGroup();
            }
        }
        // Collapse / expand toggle (hamburger) — a full-width row that flips the rail.
        {
            ImGui::Dummy(ImVec2(0, 6));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 rmin = ImGui::GetCursorScreenPos();
            float rw = ImGui::GetContentRegionAvail().x, rh = 26.0f;
            if (ImGui::InvisibleButton("##navtoggle", ImVec2(rw, rh))) { g_navCollapsed = !g_navCollapsed; SavePrefs(); }
            bool th = ImGui::IsItemHovered();
            if (th) dl->AddRectFilled(rmin, ImVec2(rmin.x + rw, rmin.y + rh), IM_COL32(255, 255, 255, 14));
            float hx = g_navCollapsed ? rmin.x + rw * 0.5f : rmin.x + 20.0f;
            float hy = rmin.y + rh * 0.5f;
            ImU32 hc = ImGui::GetColorU32(ImGuiCol_Text);
            for (int k = -1; k <= 1; ++k)
                dl->AddLine(ImVec2(hx - 7, hy + k * 5.0f), ImVec2(hx + 7, hy + k * 5.0f), hc, 2.0f);
            if (!g_navCollapsed)
                dl->AddText(ImVec2(rmin.x + 40, hy - ImGui::GetTextLineHeight() * 0.5f),
                            ImGui::GetColorU32(ImGuiCol_TextDisabled), "Collapse");
            if (th) ImGui::SetTooltip(g_navCollapsed ? "Expand sidebar" : "Collapse sidebar");
        }
        // Notifications bell — a full-width row that opens the notifications panel.
        // Shows an unread count badge drawn over the bell.
        RefreshNotifs();
        {
            ImGui::Dummy(ImVec2(0, 2));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 rmin = ImGui::GetCursorScreenPos();
            float rw = ImGui::GetContentRegionAvail().x, rh = 28.0f;
            int unread = NotifUnread();
            if (ImGui::InvisibleButton("##navbell", ImVec2(rw, rh))) {
                g_notifPanelOpen = true;
                for (auto& n : g_notifs) n.read = true;   // opening clears unread
            }
            bool bh = ImGui::IsItemHovered();
            if (bh || g_notifPanelOpen)
                dl->AddRectFilled(rmin, ImVec2(rmin.x + rw, rmin.y + rh), IM_COL32(255, 255, 255, 14), 4.0f);
            float bx = g_navCollapsed ? rmin.x + rw * 0.5f : rmin.x + 20.0f;
            float by = rmin.y + rh * 0.5f;
            ImU32 bc = unread > 0 ? ImGui::GetColorU32(kAccent) : ImGui::GetColorU32(ImGuiCol_Text);
            // Bell body: a rounded dome + flared base + clapper.
            dl->PathArcTo(ImVec2(bx, by + 2.0f), 6.0f, 3.14159f, 6.2831853f, 12);
            dl->PathLineTo(ImVec2(bx + 6.5f, by + 4.0f));
            dl->PathLineTo(ImVec2(bx - 6.5f, by + 4.0f));
            dl->PathStroke(bc, ImDrawFlags_Closed, 1.8f);
            dl->AddCircleFilled(ImVec2(bx, by + 6.5f), 1.7f, bc);
            dl->AddCircleFilled(ImVec2(bx, by - 5.5f), 1.4f, bc);   // top knob
            if (!g_navCollapsed)
                dl->AddText(ImVec2(rmin.x + 40, by - ImGui::GetTextLineHeight() * 0.5f),
                            ImGui::GetColorU32(ImGuiCol_Text), "Notifications");
            // Unread badge.
            if (unread > 0) {
                char cnt[8]; std::snprintf(cnt, sizeof(cnt), "%d", unread > 99 ? 99 : unread);
                ImVec2 ts = ImGui::CalcTextSize(cnt);
                ImVec2 bp = g_navCollapsed ? ImVec2(bx + 5, by - 9)
                                           : ImVec2(rmin.x + rw - 24, by - 9);
                float pad = 5.0f, bw = ts.x + pad * 2; if (bw < 18) bw = 18;
                dl->AddRectFilled(bp, ImVec2(bp.x + bw, bp.y + 18), IM_COL32(210, 72, 72, 255), 9.0f);
                dl->AddText(ImVec2(bp.x + (bw - ts.x) * 0.5f, bp.y + 2), IM_COL32_WHITE, cnt);
            }
            if (bh) ImGui::SetTooltip(unread > 0 ? "Notifications (%d new)" : "Notifications", unread);
        }
        // Scrollable nav items, sized to leave room for the pinned update footer
        // below — so on a short window the items scroll instead of overlapping it.
        const float kNavFootH = 92.0f;
        ImGui::BeginChild("navitems", ImVec2(0, ImGui::GetContentRegionAvail().y - kNavFootH), false,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::Dummy(ImVec2(0, 16));
        const char* navs[] = {"Create", "Play", "Community", "Account", "Settings", "Achievements", "Friends", "Market", "Servers", "Forums", "Downloads"};
        // Grouped, Steam-style sidebar. Display order + section headers; the tab
        // content blocks stay keyed by these indices, so only the order and the
        // grouping change here (keyboard shortcuts still map 1..8 to the tabs).
        struct NavRow { int tab; const char* section; };
        static const NavRow kNavRows[] = {
            {0, "MENU"},   {1, nullptr}, {2, nullptr}, {10, nullptr},
            {6, "SOCIAL"}, {8, nullptr}, {9, nullptr}, {5, nullptr}, {7, nullptr},
            {3, "SYSTEM"}, {4, nullptr},
        };
        for (const NavRow& nv : kNavRows) {
            int i = nv.tab;
            if (nv.section && !g_navCollapsed) {
                ImGui::Dummy(ImVec2(0, 8));
                ImGui::TextDisabled("  %s", nv.section);
                ImGui::Dummy(ImVec2(0, 2));
            } else if (nv.section) {
                ImGui::Dummy(ImVec2(0, 6));   // just a gap between groups when collapsed
            }
            char lbl[48];
            if (g_navCollapsed) std::snprintf(lbl, sizeof(lbl), "##nav%d", i);
            else                std::snprintf(lbl, sizeof(lbl), "          %s", navs[i]);
            bool sel = (tab == i);
            // Flat, full-width selection (Steam/Unity-style): a neutral fill across
            // the whole row + a thin accent bar on the left — no rounded pill.
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 rowMin = ImGui::GetCursorScreenPos();
            float  rowW   = ImGui::GetContentRegionAvail().x;
            const float rowH = 38.0f;
            ImVec2 rowMax = ImVec2(rowMin.x + rowW, rowMin.y + rowH);
            bool hov = ImGui::IsMouseHoveringRect(rowMin, rowMax);
            // Unity-style: the selected row is a solid blue fill (not a pill);
            // hover on other rows is a faint grey wash.
            if (sel)      dl->AddRectFilled(rowMin, rowMax, ImGui::GetColorU32(kAccent));
            else if (hov) dl->AddRectFilled(rowMin, rowMax, IM_COL32(255, 255, 255, 16));
            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_Text, sel ? ImVec4(1.00f,1.00f,1.00f,1.0f)
                                                     : ImVec4(0.760f,0.760f,0.765f,1.0f));
            if (ImGui::Selectable(lbl, sel, 0, ImVec2(0, rowH))) tab = i;
            ImGui::PopStyleColor(4);
            ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            // Little vector icon per section (no icon font needed).
            {
                float cy = (mn.y + mx.y) * 0.5f;
                float cx = g_navCollapsed ? (mn.x + mx.x) * 0.5f : mn.x + 24.0f;
                float r = 6.5f;
                ImU32 ic = ImGui::GetColorU32(sel ? ImVec4(1.00f, 1.00f, 1.00f, 1.0f)
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
                    case 5: {  // Achievements: trophy (cup + stem + base)
                        dl->AddRectFilled(ImVec2(cx - r * 0.55f, cy - r * 0.85f),
                                          ImVec2(cx + r * 0.55f, cy - r * 0.05f), ic, r * 0.2f);
                        dl->AddRectFilled(ImVec2(cx - r * 0.12f, cy - r * 0.1f),
                                          ImVec2(cx + r * 0.12f, cy + r * 0.55f), ic);
                        dl->AddRectFilled(ImVec2(cx - r * 0.6f, cy + r * 0.55f),
                                          ImVec2(cx + r * 0.6f, cy + r * 0.9f), ic, r * 0.2f);
                        break;
                    }
                    case 6:   // Friends: two heads
                        dl->AddCircleFilled(ImVec2(cx - r * 0.5f, cy - r * 0.35f), r * 0.42f, ic);
                        dl->AddCircleFilled(ImVec2(cx + r * 0.5f, cy - r * 0.35f), r * 0.42f, ic);
                        dl->AddRectFilled(ImVec2(cx - r, cy + r * 0.15f), ImVec2(cx, cy + r), ic, r * 0.3f);
                        dl->AddRectFilled(ImVec2(cx, cy + r * 0.15f), ImVec2(cx + r, cy + r), ic, r * 0.3f);
                        break;
                    case 7:   // Market: price tag
                        dl->AddRectFilled(ImVec2(cx - r * 0.8f, cy - r * 0.8f),
                                          ImVec2(cx + r * 0.3f, cy + r * 0.3f), ic, r * 0.2f);
                        dl->AddCircleFilled(ImVec2(cx - r * 0.4f, cy - r * 0.4f), r * 0.16f,
                                            ImGui::GetColorU32(ImGuiCol_WindowBg));
                        dl->AddLine(ImVec2(cx + r * 0.1f, cy + r * 0.1f), ImVec2(cx + r * 0.9f, cy + r * 0.9f), ic, 3.0f);
                        break;
                    case 8:   // Servers: chat bubble
                        dl->AddRectFilled(ImVec2(cx - r, cy - r * 0.8f), ImVec2(cx + r, cy + r * 0.3f), ic, r * 0.35f);
                        dl->AddTriangleFilled(ImVec2(cx - r * 0.5f, cy + r * 0.2f),
                                              ImVec2(cx - r * 0.1f, cy + r * 0.2f),
                                              ImVec2(cx - r * 0.5f, cy + r * 0.8f), ic);
                        dl->AddCircleFilled(ImVec2(cx - r * 0.45f, cy - r * 0.25f), r * 0.12f, ImGui::GetColorU32(ImGuiCol_WindowBg));
                        dl->AddCircleFilled(ImVec2(cx, cy - r * 0.25f), r * 0.12f, ImGui::GetColorU32(ImGuiCol_WindowBg));
                        dl->AddCircleFilled(ImVec2(cx + r * 0.45f, cy - r * 0.25f), r * 0.12f, ImGui::GetColorU32(ImGuiCol_WindowBg));
                        break;
                    case 9:   // Forums: stacked lines (document)
                        dl->AddRect(ImVec2(cx - r * 0.8f, cy - r), ImVec2(cx + r * 0.8f, cy + r), ic, r * 0.15f, 0, 1.6f);
                        for (int k = 0; k < 3; ++k) {
                            float yy = cy - r * 0.45f + k * r * 0.45f;
                            dl->AddLine(ImVec2(cx - r * 0.5f, yy), ImVec2(cx + r * 0.5f, yy), ic, 1.6f);
                        }
                        break;
                    case 10:  // Downloads: down-arrow into a tray
                        dl->AddLine(ImVec2(cx, cy - r * 0.9f), ImVec2(cx, cy + r * 0.25f), ic, 2.2f);
                        dl->AddLine(ImVec2(cx - r * 0.5f, cy - r * 0.25f), ImVec2(cx, cy + r * 0.25f), ic, 2.2f);
                        dl->AddLine(ImVec2(cx + r * 0.5f, cy - r * 0.25f), ImVec2(cx, cy + r * 0.25f), ic, 2.2f);
                        dl->AddLine(ImVec2(cx - r, cy + r * 0.7f), ImVec2(cx + r, cy + r * 0.7f), ic, 2.2f);
                        break;
                }
            }
            // Right-aligned count/badge chips. When collapsed, a small corner dot
            // stands in for the number so it doesn't overflow the narrow rail.
            auto cornerDot = [&](ImU32 dotCol) {
                dl->AddCircleFilled(ImVec2(mx.x - 8.0f, mn.y + 8.0f), 4.0f, dotCol, 12);
            };
            auto countChip = [&](int n, ImU32 bgc) {
                char nb[16]; std::snprintf(nb, sizeof(nb), "%d", n);
                ImVec2 ts = ImGui::CalcTextSize(nb);
                ImVec2 bp(mx.x - ts.x - 18.0f, (mn.y + mx.y) * 0.5f - ts.y * 0.5f);
                dl->AddRectFilled(ImVec2(bp.x - 6, bp.y - 2), ImVec2(bp.x + ts.x + 6, bp.y + ts.y + 2), bgc, 8.0f);
                dl->AddText(bp, IM_COL32(255, 255, 255, 255), nb);
            };
            // Play: games installed.
            if (i == 1 && !scenes.empty()) {
                if (g_navCollapsed) cornerDot(ImGui::GetColorU32(kAccent));
                else countChip((int)scenes.size(), ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.22f)));
            }
            // Friends: pending requests + unread DMs (red).
            if (i == 6 && (g_friendRequests + g_dmUnread) > 0) {
                if (g_navCollapsed) cornerDot(IM_COL32(210, 72, 72, 255));
                else countChip(g_friendRequests + g_dmUnread, IM_COL32(210, 72, 72, 255));
            }
            // Downloads: in-flight downloads.
            if (i == 10) {
                int act = ActiveDownloads();
                if (act > 0) {
                    if (g_navCollapsed) cornerDot(ImGui::GetColorU32(kAccent));
                    else countChip(act, ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.30f)));
                }
            }
            if (ImGui::IsItemHovered()) {
                if (g_navCollapsed) ImGui::SetTooltip("%s  (%d)", navs[i], i + 1);
                else                ImGui::SetTooltip("Shortcut: %d", i + 1);
            }
        }

        // Signed-in status chip, just under the nav items.
        ImGui::Dummy(ImVec2(0, 10));
        if (!g_navCollapsed) {
            ImGui::TextDisabled("  ACCOUNT");
            if (account.IsLoggedIn()) {
                ImGui::TextColored(ImVec4(0.55f, 0.9f, 0.6f, 1), "  %s  %s",
                                   account.IsOnline() ? "online" : "local",
                                   account.CurrentSession().username.c_str());
            } else {
                ImGui::TextDisabled("  not signed in");
            }
            // The whole account line jumps to the Account tab.
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip(account.IsLoggedIn() ? "Open the Account tab" : "Sign in");
            }
            if (ImGui::IsItemClicked()) { tab = 3; g_lastTab = 3; SavePrefs(); }
            // Open the floating Chat window (direct messages) from anywhere.
            if (account.IsLoggedIn()) {
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::SetCursorPosX(10);
                char cbtn[48]; std::snprintf(cbtn, sizeof(cbtn), g_dmUnread > 0 ? "  Chat  (%d)" : "  Open Chat", g_dmUnread);
                if (ImGui::Button(cbtn, ImVec2(-10, 0))) g_chatWinOpen = !g_chatWinOpen;
            }
        } else if (account.IsLoggedIn()) {
            // Collapsed: a compact, centered chat button with a bubble icon.
            float av = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (av - 34.0f) * 0.5f);
            if (ImGui::Button("##navchat", ImVec2(34, 30))) g_chatWinOpen = !g_chatWinOpen;
            ImVec2 bmn = ImGui::GetItemRectMin(), bmx = ImGui::GetItemRectMax();
            float bcx = (bmn.x + bmx.x) * 0.5f, bcy = (bmn.y + bmx.y) * 0.5f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 bc = ImGui::GetColorU32(ImGuiCol_Text);
            dl->AddRectFilled(ImVec2(bcx - 8, bcy - 6), ImVec2(bcx + 8, bcy + 3), bc, 3.0f);
            dl->AddTriangleFilled(ImVec2(bcx - 4, bcy + 2), ImVec2(bcx + 1, bcy + 2), ImVec2(bcx - 4, bcy + 7), bc);
            if (g_dmUnread > 0)
                dl->AddCircleFilled(ImVec2(bmx.x - 3, bmn.y + 3), 4.0f, IM_COL32(210, 72, 72, 255), 12);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(g_dmUnread > 0 ? "Chat  (%d unread)" : "Open Chat", g_dmUnread);
        }
        ImGui::EndChild();   // end scrollable nav items

        // ---- Update status (sits below the scrolling nav items) ----
        UpState st = GetState();
        bool busy = (st == Up_Checking || st == Up_Downloading);
        ImGui::Separator();
        if (!g_navCollapsed) {
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
            if (busy) {
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
            std::string upMsg = GetUpMsg();
            // Before any check runs, at least show the installed version here.
            ImGui::TextDisabled("%s", upMsg.empty() ? "v" OKAY_ENGINE_VERSION : upMsg.c_str());
            ImGui::PopTextWrapPos();
            ImGui::BeginDisabled(busy);
            if (ImGui::SmallButton("Check for updates")) StartUpdateCheck();
            ImGui::EndDisabled();
            if (g_upRelaunchNeeded && st == Up_Updated) {
                ImGui::SameLine();
                // The new launcher exe is already swapped in place; start it and quit.
                if (ImGui::SmallButton("Restart now")) { Launch(g_selfPath); running = false; }
            }
        } else {
            // Collapsed: a compact, centered update button (spinner while busy).
            float av = ImGui::GetContentRegionAvail().x;
            ImGui::Dummy(ImVec2(0, 2));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (av - 34.0f) * 0.5f);
            ImGui::BeginDisabled(busy);
            bool clicked = ImGui::Button("##navupd", ImVec2(34, 30));
            ImGui::EndDisabled();
            if (clicked) StartUpdateCheck();
            ImVec2 umn = ImGui::GetItemRectMin(), umx = ImGui::GetItemRectMax();
            ImVec2 uc((umn.x + umx.x) * 0.5f, (umn.y + umx.y) * 0.5f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (busy) {
                float a0 = (float)ImGui::GetTime() * 6.0f;
                dl->PathArcTo(uc, 7.0f, a0, a0 + 4.6f, 20);
                dl->PathStroke(ImGui::GetColorU32(kAccent), 0, 2.2f);
            } else {
                ImU32 uico = ImGui::GetColorU32(ImGuiCol_Text);
                dl->PathArcTo(uc, 7.0f, -1.3f, 3.4f, 22);
                dl->PathStroke(uico, 0, 2.0f);
                float ea = 3.4f;
                ImVec2 ep(uc.x + std::cos(ea) * 7.0f, uc.y + std::sin(ea) * 7.0f);
                dl->AddTriangleFilled(ImVec2(ep.x - 4, ep.y - 3), ImVec2(ep.x + 3, ep.y - 4),
                                      ImVec2(ep.x + 1, ep.y + 3), uico);
            }
            if (ImGui::IsItemHovered()) {
                std::string um = GetUpMsg();
                ImGui::SetTooltip("%s\nClick to check for updates",
                                  um.empty() ? "v" OKAY_ENGINE_VERSION : um.c_str());
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("content", ImVec2(0, 0), true);

        const ImVec4 kTitle(0.95f, 0.96f, 0.98f, 1.0f);
        // Page header: a large title with a short accent underline bar (tab-style),
        // an optional subtitle, and a faint full-width divider below it.
        auto sectionHeader = [&](const char* title, const char* subtitle) {
            ImGui::Dummy(ImVec2(0, 2));
            ImVec2 tp = ImGui::GetCursorScreenPos();
            ImGui::SetWindowFontScale(1.34f);
            ImGui::TextColored(kTitle, "%s", title);
            ImVec2 tsz = ImGui::GetItemRectSize();
            ImGui::SetWindowFontScale(1.0f);
            // Short accent underline directly under the title (like an active-tab bar).
            ImDrawList* hdl = ImGui::GetWindowDrawList();
            float uy = tp.y + tsz.y + 5.0f;
            hdl->AddRectFilled(ImVec2(tp.x, uy), ImVec2(tp.x + tsz.x, uy + 2.5f),
                               ImGui::GetColorU32(kAccent), 1.5f);
            ImGui::Dummy(ImVec2(0, subtitle && *subtitle ? 11 : 13));
            if (subtitle && *subtitle) {
                ImGui::TextDisabled("%s", subtitle);
                ImGui::Dummy(ImVec2(0, 8));
            }
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1, 1, 1, 0.05f));
            ImGui::Separator();
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 8));
        };
        // Login gate for online-only tabs: returns true when signed in, else
        // renders a centered "sign in" card and returns false.
        auto loginGate = [&](const char* what) -> bool {
            if (account.IsLoggedIn()) return true;
            ImGui::Dummy(ImVec2(0, 40));
            auto center = [&](const char* s, bool dim){ float tw=ImGui::CalcTextSize(s).x; float o=(ImGui::GetContentRegionAvail().x-tw)*0.5f; if(o>0) ImGui::SetCursorPosX(ImGui::GetCursorPosX()+o); if(dim) ImGui::TextDisabled("%s", s); else ImGui::TextUnformatted(s); };
            center("Sign in required", false);
            char line[96]; std::snprintf(line, sizeof(line), "Sign in to your OkaySpace account to use %s.", what);
            center(line, true);
            ImGui::Dummy(ImVec2(0, 10));
            float o=(ImGui::GetContentRegionAvail().x-180)*0.5f; if(o>0) ImGui::SetCursorPosX(ImGui::GetCursorPosX()+o);
            if (PrimaryButton("Sign in", ImVec2(180, 42))) { tab = 3; g_lastTab = 3; SavePrefs(); }
            return false;
        };
        if (tab == 0) {                                   // ---- Create ----
            sectionHeader("Create a game", nullptr);

            // Hero card: a flat, slightly-raised panel (no gradient wash) with the
            // one primary action — reads like a real app, not a designed banner.
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.180f, 0.180f, 0.190f, 1.0f));
            ImGui::BeginChild("hero", ImVec2(0, 110), true, ImGuiWindowFlags_NoScrollbar);
            {
                ImVec2 mn = ImGui::GetWindowPos(); ImVec2 sz = ImGui::GetWindowSize();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                // A small, desaturated isometric cube at the right — monochrome, subtle.
                float S = 66.0f;
                ImVec2 ctr(mn.x + sz.x - S * 0.85f, mn.y + sz.y * 0.5f);
                float hw = S * 0.42f, hh = S * 0.24f, vh = S * 0.34f;
                ImVec2 top(ctr.x, ctr.y - hh - vh * 0.5f), left(ctr.x - hw, ctr.y - vh * 0.5f),
                       right(ctr.x + hw, ctr.y - vh * 0.5f), mid(ctr.x, ctr.y + hh - vh * 0.5f);
                ImVec2 lb(left.x, left.y + vh), rb(right.x, right.y + vh), mb(mid.x, mid.y + vh);
                ImU32 cTop = IM_COL32(118, 136, 158, 42), cL = IM_COL32(78, 92, 110, 42), cR = IM_COL32(98, 114, 134, 42);
                dl->AddQuadFilled(top, right, mid, left, cTop);
                dl->AddQuadFilled(left, mid, mb, lb, cL);
                dl->AddQuadFilled(mid, right, rb, mb, cR);
                // Plain version label, top-right (no pill).
                const char* vlbl = "v" OKAY_ENGINE_VERSION;
                ImVec2 vts = ImGui::CalcTextSize(vlbl);
                dl->AddText(ImVec2(mn.x + sz.x - vts.x - 16.0f, mn.y + 12.0f),
                            ImGui::GetColorU32(ImGuiCol_TextDisabled), vlbl);
            }
            ImGui::SetCursorPos(ImVec2(16, 14));
            ImGui::Text("OkaySpace Editor");
            ImGui::SetCursorPosX(16);
            ImGui::TextDisabled("Build 2D and 3D games: scenes, scripting, UI, characters, multiplayer.");
            ImGui::SetCursorPos(ImVec2(16, 58));
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
            ImGui::PopStyleColor();   // hero ChildBg

            // ---- New project: name + location + a clickable template gallery.
            // The LAUNCHER owns project creation now: clicking a template sends
            // --new-project "<location>/<name>" to the editor, which creates the
            // folder + Assets + starting scene and opens ready to work.
            ImGui::Dummy(ImVec2(0, 10));
            ImGui::SeparatorText("New project");
            static char npName[96] = "MyGame";
            static char npLoc[400] = {0};
            if (!npLoc[0])   // default: a Projects folder beside the launcher
                std::snprintf(npLoc, sizeof(npLoc), "%s",
                              (fs::path(g_exeDir) / "Projects").string().c_str());
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Name"); ImGui::SameLine();
            ImGui::SetNextItemWidth(170);
            ImGui::InputText("##npname", npName, sizeof(npName));
            ImGui::SameLine(0, 14);
            ImGui::TextDisabled("Location"); ImGui::SameLine();
            ImGui::SetNextItemWidth(-16);
            ImGui::InputText("##nploc", npLoc, sizeof(npLoc));
            ImGui::TextDisabled("Pick a template to create <Location>/<Name> and open it in the editor.");
            static char tplFilter[64] = "";
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::SetNextItemWidth(260);
            ImGui::InputTextWithHint("##tplFilter", "Search templates...", tplFilter, sizeof(tplFilter));
            std::string tplNeedle; for (const char* c = tplFilter; *c; ++c) tplNeedle += (char)std::tolower((unsigned char)*c);
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
            int tplShown = 0;
            for (int i = 0; i < tplCount; ++i) {
                if (!tplNeedle.empty()) {
                    std::string hay;
                    for (const char* c = kTpl[i].name; *c; ++c) hay += (char)std::tolower((unsigned char)*c);
                    hay += ' ';
                    for (const char* c = kTpl[i].desc; *c; ++c) hay += (char)std::tolower((unsigned char)*c);
                    if (hay.find(tplNeedle) == std::string::npos) continue;
                }
                if (tplShown % cols) ImGui::SameLine();
                ++tplShown;
                ImGui::PushID(i);
                ImGui::BeginChild("tpl", ImVec2(cardW, 92), true, ImGuiWindowFlags_NoScrollbar);
                bool hov = !editor.empty() && ImGui::IsWindowHovered();
                if (hov) {
                    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        wp, ImVec2(wp.x + ws.x, wp.y + ws.y), IM_COL32(255, 255, 255, 12), 3.0f);
                    HoverRing();
                }
                ImGui::SetCursorPos(ImVec2(12, 12));
                IconTile(kTpl[i].name, 30.0f);
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::Text("%s", kTpl[i].name);
                ImGui::PushTextWrapPos(cardW - 14.0f);
                ImGui::TextDisabled("%s", kTpl[i].desc);
                ImGui::PopTextWrapPos();
                ImGui::EndGroup();
                if (hov) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("Create '%s' from the %s template and open it",
                                      npName[0] ? npName : "MyGame", kTpl[i].name);
                }
                if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    // The LAUNCHER creates the project folder + Assets right now
                    // (uniquifying if the name is taken); the editor then fills in
                    // the template scene. The project is real on disk immediately.
                    std::string root = CreateProject(npLoc[0] ? npLoc : ".",
                                                     npName[0] ? npName : "MyGame",
                                                     kTpl[i].name, editor);
                    if (!root.empty())
                        Toast("Created " + fs::path(root).filename().string() +
                              " (" + kTpl[i].name + ")" + (editor.empty() ? "" : " — opening editor"));
                }
                ImGui::EndChild();
                ImGui::PopID();
            }
            if (editor.empty()) ImGui::PopStyleVar();
            if (tplShown == 0 && !tplNeedle.empty())
                ImGui::TextDisabled("No templates match \"%s\".", tplFilter);

            // ---- Your projects: everything in the Location folder, one line each ----
            ImGui::Dummy(ImVec2(0, 12));
            ImGui::SeparatorText("Your projects");
            static std::vector<std::pair<std::string, std::string>> s_projects;  // name -> scene path
            static std::string s_projScanned = "\x01";   // never equals a real path -> first scan
            if (s_projScanned != std::string(npLoc) || g_projectsNeedRescan) {
                s_projScanned = npLoc;
                g_projectsNeedRescan = false;
                s_projects.clear();
                std::error_code pec;
                for (auto& e : fs::directory_iterator(fs::path(npLoc), pec)) {
                    if (!e.is_directory(pec)) continue;
                    fs::path assets = e.path() / "Assets";
                    if (!fs::exists(assets, pec)) continue;
                    // Prefer the scene named after the project; else the first scene found.
                    std::string scene;
                    fs::path preferred = assets / (e.path().filename().string() + ".okayscene");
                    if (fs::exists(preferred, pec)) scene = preferred.string();
                    else for (auto& f : fs::directory_iterator(assets, pec))
                        if (f.path().extension() == ".okayscene") { scene = f.path().string(); break; }
                    // A freshly made project whose scene the editor hasn't written
                    // yet still lists (as "setting up…"), pointing at the expected
                    // path so Open works the moment it exists.
                    if (scene.empty() && fs::is_empty(assets, pec)) scene = preferred.string();
                    if (!scene.empty())
                        s_projects.push_back({e.path().filename().string(), scene});
                }
                std::sort(s_projects.begin(), s_projects.end());
            }
            // Keep polling briefly after a create so the new project appears once
            // the editor saves its first scene (no manual Refresh needed), and
            // highlight the new card during that window.
            static Uint32 s_watchUntil = 0;
            static std::string s_highlight;
            if (!g_projectJustMade.empty()) { s_watchUntil = SDL_GetTicks() + 8000; s_highlight = g_projectJustMade; g_projectJustMade.clear(); }
            if (s_watchUntil && SDL_GetTicks() < s_watchUntil) {
                static Uint32 t = 0;
                if (SDL_GetTicks() - t > 1000) { t = SDL_GetTicks(); s_projScanned = "\x01"; }
            } else { s_watchUntil = 0; s_highlight.clear(); }
            if (ImGui::SmallButton("Refresh##projs")) s_projScanned = "\x01";
            ImGui::SameLine();
            // Clone a project straight from a GitHub (or any git) URL.
            ImGui::BeginDisabled(!GitAvailable() || g_gitBusy);
            if (ImGui::SmallButton("Clone from GitHub")) ImGui::OpenPopup("##clonegh");
            ImGui::EndDisabled();
            if (!GitAvailable())      { ImGui::SameLine(); ImGui::TextDisabled("install Git to clone/sync"); }
            else if (g_gitBusy)       { ImGui::SameLine(); ImGui::TextDisabled("git working\xE2\x80\xA6"); }
            {
                static char ghUrl[300] = "";
                if (ImGui::BeginPopup("##clonegh")) {
                    ImGui::TextDisabled("Clone a project repo into your projects folder.");
                    ImGui::SetNextItemWidth(380);
                    bool go = ImGui::InputTextWithHint("##ghurl", "https://github.com/user/repo.git",
                                                       ghUrl, sizeof(ghUrl), ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::SameLine();
                    if ((ImGui::Button("Clone") || go) && ghUrl[0]) {
                        std::string url = ghUrl;
                        std::string nm = url;
                        if (auto sl = nm.find_last_of('/'); sl != std::string::npos) nm = nm.substr(sl + 1);
                        if (nm.size() > 4 && nm.substr(nm.size() - 4) == ".git") nm.erase(nm.size() - 4);
                        for (char& ch : nm) if (ch=='"'||ch=='\\'||ch==':'||ch=='*'||ch=='?') ch='-';
                        if (nm.empty()) nm = "ClonedProject";
                        std::error_code cec; fs::create_directories(fs::path(npLoc), cec);
                        GitJob* j = new GitJob();
                        j->cmds   = { GitC(npLoc) + "clone " + GitQ(url) + " " + GitQ(nm) };
                        j->okMsg  = "Cloned " + nm + " from GitHub";
                        j->failMsg= "Clone failed — check the URL and your Git sign-in";
                        j->rescan = true;
                        StartGit(j);
                        Toast("Cloning " + nm + "\xE2\x80\xA6");
                        ghUrl[0] = '\0'; ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }
            if (s_projects.empty()) {
                ImGui::TextDisabled("No projects in this folder yet — create one above.");
            }
            // Per-project git popups (filled by the project menu below).
            static std::string gitCommitRoot, gitCommitName, gitPublishRoot, gitPublishName;
            static bool gitOpenCommit = false, gitOpenPublish = false;
            static char gitCommitMsg[200] = "Update", gitPublishUrl[300] = "";
            for (auto& pr : s_projects) {
                ImGui::PushID(pr.second.c_str());
                bool isNew = (pr.first == s_highlight);
                std::error_code sec;
                bool ready = fs::exists(fs::path(pr.second), sec);
                if (isNew) ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.16f));
                ImGui::BeginChild("proj", ImVec2(0, 56), true, ImGuiWindowFlags_NoScrollbar);
                if (ImGui::IsWindowHovered()) {
                    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        wp, ImVec2(wp.x + ws.x, wp.y + ws.y), IM_COL32(255, 255, 255, 10), 3.0f);
                }
                HoverRing();
                ImGui::SetCursorPos(ImVec2(10, 9));
                IconTile(pr.first, 38.0f);
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
                ImGui::TextUnformatted(pr.first.c_str());
                if (!ready) ImGui::TextDisabled("setting up\xE2\x80\xA6");
                else {
                    std::string pago = ModifiedAgo(fs::path(pr.second));
                    if (pago.empty()) ImGui::TextDisabled("%s", pr.second.c_str());
                    else ImGui::TextDisabled("edited %s", pago.c_str());
                }
                ImGui::EndGroup();
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 180);
                ImGui::SetCursorPosY(10.0f);
                // Play the project straight in the universal player — no export /
                // "Build Game" needed (Roblox / s&box style). Falls back to a
                // tooltip when the player runtime isn't found next to the launcher.
                bool canPlay = !player.empty() && ready;
                ImGui::BeginDisabled(!canPlay);
                if (ImGui::Button("Play", ImVec2(84, 36)) && canPlay) {
                    Launch(player, pr.second);
                    RecordPlayed(pr.second); SavePrefs();
                    Toast(std::string("Playing ") + pr.first);
                }
                ImGui::EndDisabled();
                if (player.empty() && ImGui::IsItemHovered())
                    ImGui::SetTooltip("Player runtime not found next to the launcher");
                ImGui::SameLine();
                ImGui::BeginDisabled(editor.empty() || !ready);
                if (PrimaryButton("Open", ImVec2(84, 36))) {
                    LaunchEditor(editor, "", "", pr.second);
                    Toast(std::string("Opening ") + pr.first);
                }
                ImGui::EndDisabled();
                // Double-click the card opens it too; right-click for file actions.
                if (ready && !editor.empty() && ImGui::IsWindowHovered() &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    LaunchEditor(editor, "", "", pr.second);
                    Toast(std::string("Opening ") + pr.first);
                }
                if (ImGui::BeginPopupContextWindow("projctx", ImGuiPopupFlags_MouseButtonRight)) {
                    if (!player.empty() && ready && ImGui::MenuItem("Play (no export)")) {
                        Launch(player, pr.second);
                        RecordPlayed(pr.second); SavePrefs();
                        Toast(std::string("Playing ") + pr.first);
                    }
                    if (!editor.empty() && ImGui::MenuItem("Open in Editor")) {
                        LaunchEditor(editor, "", "", pr.second);
                        Toast(std::string("Opening ") + pr.first);
                    }
                    if (ImGui::MenuItem("Show in Explorer"))
                        OpenExternal(fs::path(pr.second).parent_path().parent_path().string());
                    if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(pr.second.c_str());
                    // ---- Git: sync this project with GitHub ----
                    if (GitAvailable()) {
                        std::string root = fs::path(pr.second).parent_path().parent_path().string();
                        std::error_code gec;
                        bool hasGit = fs::exists(fs::path(root) / ".git", gec);
                        ImGui::Separator();
                        if (hasGit) {
                            if (ImGui::MenuItem("Git: Pull", nullptr, false, !g_gitBusy)) {
                                GitJob* j = new GitJob();
                                j->cmds = { GitC(root) + "pull" };
                                j->okMsg = "Pulled latest for " + pr.first;
                                j->failMsg = "Pull failed for " + pr.first;
                                j->rescan = true; StartGit(j);
                                Toast("Pulling " + pr.first + "\xE2\x80\xA6");
                            }
                            if (ImGui::MenuItem("Git: Commit & Push", nullptr, false, !g_gitBusy)) {
                                gitCommitRoot = root; gitCommitName = pr.first; gitOpenCommit = true;
                            }
                        } else {
                            if (ImGui::MenuItem("Publish to GitHub...", nullptr, false, !g_gitBusy)) {
                                gitPublishRoot = root; gitPublishName = pr.first; gitOpenPublish = true;
                            }
                        }
                    }
                    ImGui::EndPopup();
                }
                ImGui::EndChild();
                if (isNew) ImGui::PopStyleColor();
                ImGui::PopID();
            }
            // ---- Git commit-message + publish-URL popups ----
            if (gitOpenCommit) { ImGui::OpenPopup("Commit & Push"); gitOpenCommit = false; }
            if (ImGui::BeginPopupModal("Commit & Push", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextDisabled("Commit all changes in \"%s\" and push to GitHub.", gitCommitName.c_str());
                ImGui::SetNextItemWidth(360);
                ImGui::InputTextWithHint("##cmsg", "Commit message", gitCommitMsg, sizeof(gitCommitMsg));
                if (ImGui::Button("Commit & Push", ImVec2(150, 0)) && gitCommitMsg[0]) {
                    GitJob* j = new GitJob();
                    j->cmds = { GitC(gitCommitRoot) + "add -A",
                                GitC(gitCommitRoot) + "commit -m " + GitQ(gitCommitMsg),
                                GitC(gitCommitRoot) + "push" };
                    j->okMsg = "Pushed " + gitCommitName + " to GitHub";
                    j->failMsg = "Push failed — is a GitHub remote set up?";
                    StartGit(j);
                    Toast("Pushing " + gitCommitName + "\xE2\x80\xA6");
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            if (gitOpenPublish) { ImGui::OpenPopup("Publish to GitHub"); gitOpenPublish = false; }
            if (ImGui::BeginPopupModal("Publish to GitHub", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("Create an empty repository on GitHub first, then paste its URL here.");
                ImGui::TextDisabled("Publishes \"%s\" to that repository (initial commit on main).", gitPublishName.c_str());
                ImGui::SetNextItemWidth(380);
                ImGui::InputTextWithHint("##purl", "https://github.com/you/your-repo.git", gitPublishUrl, sizeof(gitPublishUrl));
                if (ImGui::Button("Publish", ImVec2(130, 0)) && gitPublishUrl[0]) {
                    GitJob* j = new GitJob();
                    j->cmds = { GitC(gitPublishRoot) + "init",
                                GitC(gitPublishRoot) + "add -A",
                                GitC(gitPublishRoot) + "commit -m " + GitQ("Initial commit"),
                                GitC(gitPublishRoot) + "branch -M main",
                                GitC(gitPublishRoot) + "remote add origin " + GitQ(gitPublishUrl),
                                GitC(gitPublishRoot) + "push -u origin main" };
                    j->okMsg = "Published " + gitPublishName + " to GitHub";
                    j->failMsg = "Publish failed — check the URL and your Git sign-in";
                    StartGit(j);
                    Toast("Publishing " + gitPublishName + "\xE2\x80\xA6");
                    gitPublishUrl[0] = '\0'; ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            ImGui::Dummy(ImVec2(0, 14));
            // Live notes from the latest GitHub release when a check has run;
            // the built-in highlights otherwise (offline / never checked).
            std::string newsVer, newsLines;
            GetNews(newsVer, newsLines);
            if (!newsLines.empty()) {
                std::string hdr = "What's new" + (newsVer.empty() ? "" : "  \xC2\xB7  " + newsVer);
                ImGui::SeparatorText(hdr.c_str());
                std::size_t s2 = 0;
                while (s2 < newsLines.size()) {
                    std::size_t e2 = newsLines.find('\n', s2);
                    if (e2 == std::string::npos) e2 = newsLines.size();
                    if (e2 > s2) ImGui::BulletText("%s", newsLines.substr(s2, e2 - s2).c_str());
                    s2 = e2 + 1;
                }
            } else {
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
            }
            if (ImGui::SmallButton("View all releases"))
                OpenExternal("https://github.com/kingimann/OkaySpaceGameEngine/releases");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open the full release notes on GitHub");

            ImGui::Dummy(ImVec2(0, 10));
            ImGui::SeparatorText("Tips");
            ImGui::BulletText("Press Play in the editor to test instantly.");
            ImGui::BulletText("Drag assets from the Project panel onto objects.");
            ImGui::BulletText("Add UI from GameObject > UI (buttons, sliders, radial loaders).");
        } else if (tab == 1) {                            // ---- Play ----
            sectionHeader("Play a game", nullptr);

            // ---- Web games (HTML / JavaScript) -------------------------------
            // Any folder with an index.html (e.g. a web export) or a loose .html
            // in the games/community/webgames folders plays in the default browser.
            static std::vector<fs::path> s_webGames; static bool s_webScanned = false;
            if (!s_webScanned || g_webNeedRescan) { s_webGames = FindWebGames(); s_webScanned = true; g_webNeedRescan = false; }
            if (!s_webGames.empty()) {
                ImGui::SeparatorText("Web games (HTML / JavaScript)");
                for (const auto& wg : s_webGames) {
                    std::string nm = wg.parent_path().filename().string();
                    if (nm.empty() || nm == "community" || nm == "games" || nm == "webgames")
                        nm = wg.stem().string();
                    ImGui::PushID(wg.string().c_str());
                    ImGui::BeginChild("##wg", ImVec2(0, 48), true, ImGuiWindowFlags_NoScrollbar);
                    if (ImGui::IsWindowHovered()) {
                        ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
                        ImGui::GetWindowDrawList()->AddRectFilled(
                            wp, ImVec2(wp.x + ws.x, wp.y + ws.y), IM_COL32(255, 255, 255, 10), 3.0f);
                    }
                    // A small "JS" tile badge.
                    ImGui::SetCursorPos(ImVec2(10, 8));
                    ImVec2 tp = ImGui::GetCursorScreenPos();
                    ImDrawList* wdl = ImGui::GetWindowDrawList();
                    wdl->AddRectFilled(tp, ImVec2(tp.x + 32, tp.y + 32),
                                       IM_COL32(222, 196, 60, 255), 5.0f);           // JS yellow
                    wdl->AddText(ImVec2(tp.x + 7, tp.y + 8), IM_COL32(28, 28, 20, 255), "JS");
                    ImGui::SetCursorPos(ImVec2(52, 7));
                    ImGui::BeginGroup();
                    ImGui::TextUnformatted(nm.c_str());
                    ImGui::TextDisabled("HTML game \xE2\x80\x94 opens in your browser");
                    ImGui::EndGroup();
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 88);
                    ImGui::SetCursorPosY(7);
                    if (PrimaryButton("Play", ImVec2(84, 34))) {
                        OpenExternal(wg.string());
                        Toast("Opening " + nm + " in your browser", 1);
                    }
                    if (ImGui::BeginPopupContextWindow("##wgctx", ImGuiPopupFlags_MouseButtonRight)) {
                        if (ImGui::MenuItem("Play in browser")) { OpenExternal(wg.string()); }
                        if (ImGui::MenuItem("Show in Explorer")) OpenExternal(wg.parent_path().string());
                        if (ImGui::MenuItem("Copy path")) ImGui::SetClipboardText(wg.string().c_str());
                        ImGui::EndPopup();
                    }
                    ImGui::EndChild();
                    ImGui::PopID();
                }
                ImGui::SameLine(); if (ImGui::SmallButton("Rescan web games")) s_webScanned = false;
                ImGui::Dummy(ImVec2(0, 10));
            } else if (s_webScanned) {
                // Nothing yet — a tiny hint so the feature is discoverable.
                ImGui::TextDisabled("Tip: drop an .html game (or a folder with index.html) here to play it in your browser.");
                if (ImGui::SmallButton("Rescan web games")) s_webScanned = false;
                ImGui::Dummy(ImVec2(0, 6));
            }
            if (player.empty())
                ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "Player runtime not found next to the launcher.");
            else if (std::none_of(scenes.begin(), scenes.end(), IsBuiltGame)) {
                // No *built* games (dev scenes don't count). A friendly centered card.
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
                bool haveEd = !editor.empty();
                float btnRow = haveEd ? (170.0f + 8.0f + 100.0f) : 100.0f;
                ImGui::SetCursorPosX((cw - btnRow) * 0.5f);
                if (haveEd) {
                    if (PrimaryButton("Open Editor", ImVec2(170, 36))) LaunchEditor(editor);
                    ImGui::SameLine();
                }
                if (ImGui::Button("Refresh", ImVec2(100, 36))) scenes = FindScenes();
                ImGui::EndChild();
            } else {
                auto lower = [](std::string s) {
                    for (char& c : s) c = (char)std::tolower((unsigned char)c);
                    return s;
                };
                // Toolbar: search + sort + view toggle + folder, all on one row
                // (the search field flexes to fill whatever is left).
                if (focusSearch) { ImGui::SetKeyboardFocusHere(); focusSearch = false; }
                {
                    const float sortW = 156.0f, viewW = 100.0f, foldW = 92.0f, refW = 84.0f;
                    float sp = ImGui::GetStyle().ItemSpacing.x;
                    float searchW = ImGui::GetContentRegionAvail().x - sortW - viewW - foldW - refW - sp * 4.0f;
                    if (searchW < 150.0f) searchW = 150.0f;
                    ImGui::SetNextItemWidth(searchW);
                }
                ImGui::InputTextWithHint("##playFilter", "Search games...  (Ctrl+F)", playFilter, sizeof(playFilter));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(156);
                if (ImGui::Combo("##playsort", &g_playSort,
                                 "Favorites first\0Name A\xE2\x80\x93Z\0Recently played\0Most played\0")) SavePrefs();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sort order");
                ImGui::SameLine();
                // List / Grid view toggle (persisted).
                auto viewBtn = [&](const char* lbl, int v) {
                    bool on = g_playView == v;
                    if (on) ImGui::PushStyleColor(ImGuiCol_Button, kAccentDim);
                    if (ImGui::Button(lbl, ImVec2(49, 0))) { g_playView = v; SavePrefs(); }
                    if (on) ImGui::PopStyleColor();
                };
                viewBtn("List", 0);
                ImGui::SameLine(0, 2);
                viewBtn("Grid", 1);
                ImGui::SameLine();
                if (ImGui::Button("Folder", ImVec2(92, 0))) OpenExternal(g_exeDir);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open the games folder");
                ImGui::SameLine();
                if (ImGui::Button("Refresh", ImVec2(84, 0))) scenes = FindScenes();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rescan for games");
                ImGui::Dummy(ImVec2(0, 6));

                // Collection chips (Steam-style library categories). "All" plus one
                // per collection; the active one filters the list below.
                {
                    auto chip = [&](const char* label, bool active) {
                        if (active) { ImGui::PushStyleColor(ImGuiCol_Button, kAccentDim);
                                      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,1,1)); }
                        bool c = ImGui::Button(label);
                        if (active) ImGui::PopStyleColor(2);
                        return c;
                    };
                    if (chip("All", g_collFilter.empty())) g_collFilter.clear();
                    for (auto& kv : g_collections) {
                        ImGui::SameLine();
                        char lbl[80]; std::snprintf(lbl, sizeof(lbl), "%s (%d)", kv.first.c_str(), (int)kv.second.size());
                        if (chip(lbl, g_collFilter == kv.first))
                            g_collFilter = (g_collFilter == kv.first) ? std::string() : kv.first;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("+ New collection")) ImGui::OpenPopup("##newcoll");
                    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
                    if (ImGui::BeginPopup("##newcoll")) {
                        static char cbuf[48] = "";
                        ImGui::TextDisabled("New collection");
                        ImGui::SetNextItemWidth(220);
                        bool ok = ImGui::InputTextWithHint("##cn", "Name...", cbuf, sizeof(cbuf),
                                                           ImGuiInputTextFlags_EnterReturnsTrue);
                        ImGui::SameLine();
                        if ((ImGui::Button("Create") || ok) && cbuf[0]) {
                            std::string nm = cbuf;
                            for (char& ch : nm) if (ch == '|') ch = '-';   // '|' is the save delimiter
                            g_collections[nm]; g_collFilter = nm; SavePrefs();
                            cbuf[0] = '\0'; ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                    // Manage the active collection: rename-free, but allow delete.
                    if (!g_collFilter.empty()) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Delete collection")) {
                            g_collections.erase(g_collFilter); g_collFilter.clear(); SavePrefs();
                            Toast("Collection deleted", 1);
                        }
                    }
                    ImGui::Dummy(ImVec2(0, 6));
                }

                // "Jump back in": a hero card for the most-recently-played game
                // that still exists on disk (hidden while searching).
                if (playFilter[0] == '\0' && !player.empty()) {
                    std::string resume;
                    for (const auto& rp : g_recent)
                        if (std::find_if(scenes.begin(), scenes.end(),
                                [&](const fs::path& s){ return s.string() == rp; }) != scenes.end()) { resume = rp; break; }
                    if (!resume.empty()) {
                        fs::path rpath(resume);
                        std::string rname = rpath.filename().string();
                        ImGui::BeginChild("resume", ImVec2(0, 66), true, ImGuiWindowFlags_NoScrollbar);
                        ImGui::SetCursorPos(ImVec2(10, 11));
                        DrawGameIcon(resume, rname, 44.0f);
                        ImGui::SameLine();
                        ImGui::BeginGroup();
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
                        ImGui::TextDisabled("JUMP BACK IN");
                        ImGui::Text("%s", rname.c_str());
                        ImGui::EndGroup();
                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 100);
                        ImGui::SetCursorPosY(13.0f);
                        if (PrimaryButton("Resume", ImVec2(96, 40))) {
                            Launch(player, resume); RecordPlayed(resume); SavePrefs();
                            Toast(std::string("Playing ") + rname);
                        }
                        ImGui::EndChild();
                        ImGui::Dummy(ImVec2(0, 6));
                    }
                }

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
                    } else if (g_playSort == 3) {
                        int ca = PlayCount(scenes[a].string()), cb = PlayCount(scenes[b].string());
                        if (ca != cb) return ca > cb;
                    }
                    return lower(scenes[a].filename().string()) < lower(scenes[b].filename().string());
                });
                // Play shows only *built* games (exported with File > Build Game),
                // never loose dev scenes.
                order.erase(std::remove_if(order.begin(), order.end(),
                    [&](std::size_t idx){ return !IsBuiltGame(scenes[idx]); }), order.end());
                // Collection filter: keep only games in the active collection.
                if (!g_collFilter.empty())
                    order.erase(std::remove_if(order.begin(), order.end(),
                        [&](std::size_t idx){ return !InCollection(g_collFilter, scenes[idx].string()); }),
                        order.end());

                std::string needle = lower(playFilter);
                int shown = 0;
                // ---- Grid view: big tiles, double-click to play ----
                if (g_playView == 1) {
                    const float tileW = 112.0f, tileH = 128.0f;
                    int cols = (int)(ImGui::GetContentRegionAvail().x / (tileW + 8.0f));
                    if (cols < 2) cols = 2;
                    int col = 0;
                    for (std::size_t oi = 0; oi < order.size(); ++oi) {
                        std::size_t i = order[oi];
                        std::string name = scenes[i].filename().string();
                        if (!needle.empty() && lower(name).find(needle) == std::string::npos)
                            continue;
                        ++shown;
                        std::string path = scenes[i].string();
                        bool fav = IsFavorite(path);
                        if (col) ImGui::SameLine();
                        ImGui::PushID((int)i);
                        ImGui::BeginChild("gtile", ImVec2(tileW, tileH), true, ImGuiWindowFlags_NoScrollbar);
                        HoverRing();
                        ImGui::SetCursorPos(ImVec2((tileW - 64.0f) * 0.5f, 12.0f));
                        DrawGameIcon(path, name, 64.0f);
                        std::string nm = name;
                        if (nm.size() > 13) nm = nm.substr(0, 12) + "..";
                        ImVec2 nts = ImGui::CalcTextSize(nm.c_str());
                        ImGui::SetCursorPos(ImVec2((tileW - nts.x) * 0.5f, 88.0f));
                        ImGui::TextUnformatted(nm.c_str());
                        ImGui::SetCursorPos(ImVec2((tileW - ImGui::CalcTextSize("2 d ago").x) * 0.5f, 104.0f));
                        std::string gago = ModifiedAgo(scenes[i]);
                        if (!gago.empty()) ImGui::TextDisabled("%s", gago.c_str());
                        bool tileHov = ImGui::IsWindowHovered();
                        bool overStar = false, overPlay = false;
                        ImVec2 wmn = ImGui::GetWindowPos();
                        // Play-count badge, top-left, for games that have been played.
                        int gpc = PlayCount(path);
                        if (gpc > 0) {
                            char cb[16]; std::snprintf(cb, sizeof(cb), "%d\xC3\x97", gpc);
                            ImVec2 cs = ImGui::CalcTextSize(cb);
                            ImVec2 bp(wmn.x + 8, wmn.y + 8);
                            ImDrawList* cdl = ImGui::GetWindowDrawList();
                            cdl->AddRectFilled(bp, ImVec2(bp.x + cs.x + 10, bp.y + cs.y + 4),
                                               ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.30f)), 7.0f);
                            cdl->AddText(ImVec2(bp.x + 5, bp.y + 2), ImGui::GetColorU32(ImGuiCol_Text), cb);
                        }
                        if (fav || tileHov) {
                            // Gold star badge, top-right: filled when favorited, a gray
                            // outline on hover — and clickable either way to toggle.
                            ImVec2 fc(wmn.x + tileW - 14.0f, wmn.y + 14.0f);
                            ImVec2 m = ImGui::GetIO().MousePos;
                            overStar = (m.x - fc.x) * (m.x - fc.x) + (m.y - fc.y) * (m.y - fc.y) < 10.0f * 10.0f;
                            float R = overStar ? 7.5f : 6.0f;
                            ImVec2 pts[10];
                            for (int k2 = 0; k2 < 10; ++k2) {
                                float ang = -1.5707963f + k2 * 0.62831853f;
                                float rr = (k2 % 2 == 0) ? R : R * 0.45f;
                                pts[k2] = ImVec2(fc.x + std::cos(ang) * rr, fc.y + std::sin(ang) * rr);
                            }
                            ImDrawList* sdl = ImGui::GetWindowDrawList();
                            if (fav)
                                sdl->AddConcavePolyFilled(pts, 10, IM_COL32(255, 204, 64, 255));
                            else
                                sdl->AddPolyline(pts, 10, IM_COL32(170, 175, 188, 220), ImDrawFlags_Closed, 1.6f);
                        }
                        if (tileHov) {
                            // Hover play overlay on the icon: dark disc + accent triangle.
                            // A single click on it plays right away (double-click elsewhere
                            // on the tile still works).
                            ImVec2 cc(wmn.x + tileW * 0.5f, wmn.y + 44.0f);
                            ImVec2 m = ImGui::GetIO().MousePos;
                            overPlay = (m.x - cc.x) * (m.x - cc.x) + (m.y - cc.y) * (m.y - cc.y) < 17.0f * 17.0f;
                            ImDrawList* odl = ImGui::GetWindowDrawList();
                            odl->AddCircleFilled(cc, 16.0f, IM_COL32(10, 12, 16, overPlay ? 215 : 165));
                            odl->AddCircle(cc, 16.0f, ImGui::GetColorU32(kAccent), 0, overPlay ? 2.2f : 1.4f);
                            odl->AddTriangleFilled(ImVec2(cc.x - 4.5f, cc.y - 7.0f),
                                                   ImVec2(cc.x - 4.5f, cc.y + 7.0f),
                                                   ImVec2(cc.x + 8.0f, cc.y), IM_COL32(255, 255, 255, 245));
                        }
                        if (tileHov) {
                            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                            if (overStar)
                                ImGui::SetTooltip(fav ? "Unfavorite" : "Favorite");
                            else
                                ImGui::SetTooltip("Double-click to play %s", name.c_str());
                            // MouseClickedCount == 1 so the second click of a fast
                            // double on the overlay doesn't launch the game twice.
                            bool click1 = ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                                          ImGui::GetIO().MouseClickedCount[ImGuiMouseButton_Left] == 1;
                            if (overStar && click1) {
                                ToggleFavorite(path); SavePrefs();
                            } else if ((overPlay && click1) ||
                                       (!overStar && !overPlay && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
                                Launch(player, path);
                                RecordPlayed(path); SavePrefs();
                                Toast(std::string("Playing ") + name);
                            }
                        }
                        if (ImGui::BeginPopupContextWindow("gtilectx", ImGuiPopupFlags_MouseButtonRight)) {
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
                        if (++col >= cols) col = 0;
                    }
                    if (shown == 0)
                        ImGui::TextDisabled("No games match \"%s\".", playFilter);
                    else
                        ImGui::TextDisabled("%d game%s%s", shown, shown == 1 ? "" : "s",
                                            needle.empty() ? "" : " matching");
                } else {
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
                    DrawGameIcon(path, name, 44.0f);
                    ImGui::SameLine();
                    ImGui::BeginGroup();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3);
                    ImGui::Text("%s", name.c_str());
                    if (RecentRank(path) < 3) {
                        // "recent" as a small accent pill chip next to the name.
                        ImGui::SameLine(0, 8);
                        const char* rl = "recent";
                        ImVec2 rs = ImGui::CalcTextSize(rl);
                        ImVec2 pp = ImGui::GetCursorScreenPos();
                        float ph = rs.y + 4.0f, pw = rs.x + 14.0f;
                        ImDrawList* rdl = ImGui::GetWindowDrawList();
                        rdl->AddRectFilled(ImVec2(pp.x, pp.y - 1), ImVec2(pp.x + pw, pp.y - 1 + ph),
                                           ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.20f)),
                                           ph * 0.5f);
                        rdl->AddText(ImVec2(pp.x + 7, pp.y + 1), ImGui::GetColorU32(kAccent), rl);
                        ImGui::Dummy(ImVec2(pw, rs.y));
                    }
                    std::string ago = ModifiedAgo(scenes[i]);
                    std::string sub = scenes[i].parent_path().string();
                    if (!ago.empty()) sub += "  \xC2\xB7  " + ago;
                    int pc = PlayCount(path);
                    if (pc > 0) {
                        char pb[64];
                        std::string la = AgoText(LastPlayed(path));
                        std::snprintf(pb, sizeof(pb), "  \xC2\xB7  played %d\xC3\x97%s%s",
                                      pc, la.empty() ? "" : "  \xC2\xB7  last ", la.c_str());
                        sub += pb;
                    }
                    ImGui::TextDisabled("%s", sub.c_str());
                    ImGui::EndGroup();
                    // Right-aligned: favorite star, Folder, Play.
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - (36 + 82 + 80 + 20));
                    ImGui::SetCursorPosY(12.0f);
                    if (ImGui::Button("##fav", ImVec2(36, 40))) { ToggleFavorite(path); SavePrefs(); }
                    {
                        // Drawn five-point star (the UI font has no ★ glyph):
                        // filled gold when favorited, gray outline otherwise.
                        ImVec2 fmn = ImGui::GetItemRectMin(), fmx = ImGui::GetItemRectMax();
                        ImVec2 fc((fmn.x + fmx.x) * 0.5f, (fmn.y + fmx.y) * 0.5f);
                        float R = 9.0f;
                        ImVec2 pts[10];
                        for (int k = 0; k < 10; ++k) {
                            float ang = -1.5707963f + k * 0.62831853f;
                            float rr = (k % 2 == 0) ? R : R * 0.45f;
                            pts[k] = ImVec2(fc.x + std::cos(ang) * rr, fc.y + std::sin(ang) * rr);
                        }
                        ImDrawList* fdl = ImGui::GetWindowDrawList();
                        if (fav)
                            fdl->AddConcavePolyFilled(pts, 10, IM_COL32(255, 204, 64, 255));
                        else
                            fdl->AddPolyline(pts, 10, IM_COL32(150, 155, 168, 255),
                                             ImDrawFlags_Closed, 1.6f);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip(fav ? "Unfavorite" : "Favorite");
                    ImGui::SameLine();
                    if (ImGui::Button("Folder", ImVec2(82, 40))) OpenExternal(scenes[i].parent_path().string());
                    ImGui::SameLine();
                    if (PrimaryButton("Play", ImVec2(80, 40))) {
                        Launch(player, path);
                        RecordPlayed(path); SavePrefs();
                        Toast(std::string("Playing ") + name);
                    }
                    // Double-click anywhere on the row also plays it.
                    if (ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
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
                        if (ImGui::BeginMenu("Collections")) {
                            for (auto& kv : g_collections) {
                                bool in = InCollection(kv.first, path);
                                if (ImGui::MenuItem(kv.first.c_str(), nullptr, in)) {
                                    ToggleInCollection(kv.first, path); SavePrefs();
                                }
                            }
                            if (!g_collections.empty()) ImGui::Separator();
                            static char nbuf[48] = "";
                            ImGui::TextDisabled("New collection");
                            ImGui::SetNextItemWidth(160);
                            bool mk = ImGui::InputTextWithHint("##mkcoll", "Name...", nbuf, sizeof(nbuf),
                                                               ImGuiInputTextFlags_EnterReturnsTrue);
                            ImGui::SameLine();
                            if ((ImGui::SmallButton("Add") || mk) && nbuf[0]) {
                                std::string nm = nbuf; for (char& ch : nm) if (ch == '|') ch = '-';
                                g_collections[nm].push_back(path); SavePrefs();
                                nbuf[0] = '\0';
                            }
                            ImGui::EndMenu();
                        }
                        if (ImGui::MenuItem("Show in Explorer")) OpenExternal(scenes[i].parent_path().string());
                        if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(path.c_str());
                        if (ImGui::MenuItem("Publish to Marketplace...")) {
                            g_pendingPublishTitle = scenes[i].stem().string();   // Market tab prefills + opens its form
                            tab = 7;
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::EndChild();
                    ImGui::PopID();
                }
                if (shown == 0)
                    ImGui::TextDisabled("No games match \"%s\".", playFilter);
                else {
                    long totalLaunches = 0;   // lifetime launches across this library
                    for (const auto& s : scenes) totalLaunches += PlayCount(s.string());
                    if (totalLaunches > 0)
                        ImGui::TextDisabled("%d game%s%s  \xC2\xB7  %ld total launch%s", shown,
                                            shown == 1 ? "" : "s", needle.empty() ? "" : " matching",
                                            totalLaunches, totalLaunches == 1 ? "" : "es");
                    else
                        ImGui::TextDisabled("%d game%s%s", shown, shown == 1 ? "" : "s",
                                            needle.empty() ? "" : " matching");
                }
                }   // end list view
            }
        } else if (tab == 2) {                            // ---- Community ----
            sectionHeader("Community",
                "Play and share games, levels, scripts and models with other creators.");

            // Play a shared game in place — drawn as a drop-zone banner (dashed
            // border + drop icon) so it reads as "drag files here" at a glance.
            {
                ImVec2 p = ImGui::GetCursorScreenPos();
                float w = ImGui::GetContentRegionAvail().x, h = 66.0f;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                                  ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.07f)), 9.0f);
                // Hand-dashed border (ImGui has no dashed-rect primitive).
                ImU32 bc = ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.55f));
                const float dash = 8.0f, gap = 6.0f;
                for (float x = p.x + 8; x + dash < p.x + w - 8; x += dash + gap) {
                    dl->AddLine(ImVec2(x, p.y), ImVec2(x + dash, p.y), bc, 1.6f);
                    dl->AddLine(ImVec2(x, p.y + h), ImVec2(x + dash, p.y + h), bc, 1.6f);
                }
                for (float y = p.y + 8; y + dash < p.y + h - 8; y += dash + gap) {
                    dl->AddLine(ImVec2(p.x, y), ImVec2(p.x, y + dash), bc, 1.6f);
                    dl->AddLine(ImVec2(p.x + w, y), ImVec2(p.x + w, y + dash), bc, 1.6f);
                }
                // Drop icon: circle + down arrow, left side of the banner.
                ImVec2 cc(p.x + 33.0f, p.y + h * 0.5f);
                dl->AddCircle(cc, 14.0f, bc, 0, 1.8f);
                dl->AddLine(ImVec2(cc.x, cc.y - 7.0f), ImVec2(cc.x, cc.y + 5.0f), bc, 2.2f);
                dl->AddLine(ImVec2(cc.x - 5.0f, cc.y), ImVec2(cc.x, cc.y + 5.0f), bc, 2.2f);
                dl->AddLine(ImVec2(cc.x + 5.0f, cc.y), ImVec2(cc.x, cc.y + 5.0f), bc, 2.2f);
                ImGui::SetCursorScreenPos(ImVec2(p.x + 58.0f, p.y + 13.0f));
                ImGui::Text("Drop a shared game here to play it");
                ImGui::SetCursorScreenPos(ImVec2(p.x + 58.0f, p.y + 13.0f + ImGui::GetTextLineHeightWithSpacing()));
                ImGui::TextDisabled("Drag a game folder or .okayscene onto this window. It plays in place, nothing is copied.");
                ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
                ImGui::Dummy(ImVec2(0, 10));
            }
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
                std::string cago = ModifiedAgo(scenes[i]);
                if (cago.empty())
                    ImGui::TextDisabled("%s", croot.filename().string().c_str());
                else
                    ImGui::TextDisabled("%s  \xC2\xB7  %s", croot.filename().string().c_str(), cago.c_str());
                ImGui::EndGroup();
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - (80 + 82 + 82 + 24));
                ImGui::SetCursorPosY(12.0f);
                ImGui::BeginDisabled(player.empty());
                if (PrimaryButton("Play", ImVec2(80, 40))) { Launch(player, cpath); RecordPlayed(cpath); SavePrefs(); }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Folder", ImVec2(82, 40))) OpenExternal(croot.string());
                ImGui::SameLine();
                // Removing deletes the game's folder from disk, so confirm first
                // (modal rendered after the loop).
                if (ImGui::Button("Remove", ImVec2(82, 40))) {
                    commRemovePath = croot.string();
                    commRemoveName = scenes[i].filename().string();
                }
                // Double-click plays, like the Play tab rows.
                if (!player.empty() && ImGui::IsWindowHovered() &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    Launch(player, cpath); RecordPlayed(cpath); SavePrefs();
                }
                // Right-click parity with the Play tab rows.
                if (ImGui::BeginPopupContextWindow("commctx", ImGuiPopupFlags_MouseButtonRight)) {
                    if (!player.empty() && ImGui::MenuItem("Play")) {
                        Launch(player, cpath); RecordPlayed(cpath); SavePrefs();
                    }
                    if (ImGui::MenuItem("Show in Explorer")) OpenExternal(croot.string());
                    if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(cpath.c_str());
                    ImGui::Separator();
                    if (ImGui::MenuItem("Remove from library...")) {
                        commRemovePath = croot.string();
                        commRemoveName = scenes[i].filename().string();
                    }
                    ImGui::EndPopup();
                }
                ImGui::EndChild();
                ImGui::PopID();
            }
            // Confirm before deleting a community game's folder from disk.
            if (!commRemovePath.empty() && !ImGui::IsPopupOpen("Remove from library?"))
                ImGui::OpenPopup("Remove from library?");
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                    ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Remove from library?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Remove \"%s\"?", commRemoveName.c_str());
                ImGui::TextDisabled("This deletes its folder from disk and can't be undone.");
                ImGui::Dummy(ImVec2(0, 6));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.22f, 0.22f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.28f, 0.28f, 1.0f));
                bool doRemove = ImGui::Button("Remove", ImVec2(110, 0));
                ImGui::PopStyleColor(2);
                if (doRemove) {
                    std::error_code rec; fs::remove_all(commRemovePath, rec);
                    Toast(rec ? "Remove failed" : "Removed from library", rec ? 2 : 1);
                    scenes = FindScenes();
                    commRemovePath.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(110, 0))) {
                    commRemovePath.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
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
            ImGui::TextWrapped("Build your game in the editor (File > Build Game), then either share the "
                "whole game folder (zip it) for friends to drop in their community folder, or "
                "publish it to the Marketplace so anyone can find and download it.");
            ImGui::Dummy(ImVec2(0, 4));
            if (ImGui::Button("Reveal my games folder", ImVec2(220, 0))) OpenExternal(g_exeDir);
            ImGui::SameLine();
            if (PrimaryButton("Publish to the Marketplace", ImVec2(230, 0))) { tab = 7; g_openPublishForm = true; }

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
                // Fetch profile + avatar + code once, on a worker thread, so opening
                // the Account tab doesn't freeze on three network calls.
                if (!g_acMutex) g_acMutex = SDL_CreateMutex();
                if (!g_acStarted && account.IsOnline()) {
                    g_acStarted = true;
                    if (SDL_Thread* t = SDL_CreateThread(AccountInfoWorker, "okay-acct", nullptr))
                        SDL_DetachThread(t);
                }
                // Apply the fetched customization once it arrives.
                if (!acctProfileLoaded && g_acMutex) {
                    SDL_LockMutex(g_acMutex);
                    if (g_acReady) {
                        const okay::Friend& me = g_acProfile;
                        acctAccent = me.accent;
                        std::snprintf(acctStatus,   sizeof(acctStatus),   "%s", me.customStatus.c_str());
                        std::snprintf(acctFavGame,  sizeof(acctFavGame),  "%s", me.favoriteGame.c_str());
                        std::snprintf(acctLocation, sizeof(acctLocation), "%s", me.location.c_str());
                        acctProfileLoaded = true;
                    }
                    SDL_UnlockMutex(g_acMutex);
                }
                int paIdx = acctAccent; if (paIdx < 0 || paIdx >= (int)(sizeof(kAccentPresets)/sizeof(kAccentPresets[0]))) paIdx = 0;
                ImVec4 myAccent = kAccentPresets[paIdx].col;
                ImGui::Dummy(ImVec2(0, 8));
                // Profile card: avatar initial + name + account details. It doubles
                // as a live preview of the player's chosen profile color/status.
                ImGui::BeginChild("profile", ImVec2(0, 124), true);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 cp = ImGui::GetCursorScreenPos();
                float r = 30.0f;
                ImVec2 ctr(cp.x + r + 4, cp.y + r + 6);
                // Avatar tinted by the username (same stable hash as game tiles),
                // so each account gets its own recognizable color.
                ImVec4 av = NameColor(s.username);
                // Wash the card with the chosen profile accent so it reads as a
                // preview of what friends see.
                ImVec2 wmn = ImGui::GetWindowPos();
                ImVec2 wmx(wmn.x + ImGui::GetWindowSize().x, wmn.y + ImGui::GetWindowSize().y);
                // Accent banner strip along the top + a faint full-card wash.
                dl->AddRectFilledMultiColor(wmn, ImVec2(wmx.x, wmn.y + 46),
                    ImGui::GetColorU32(ImVec4(myAccent.x,myAccent.y,myAccent.z,0.40f)),
                    ImGui::GetColorU32(ImVec4(myAccent.x,myAccent.y,myAccent.z,0.14f)),
                    ImGui::GetColorU32(ImVec4(myAccent.x,myAccent.y,myAccent.z,0.06f)),
                    ImGui::GetColorU32(ImVec4(myAccent.x,myAccent.y,myAccent.z,0.22f)));
                dl->AddRectFilled(ImVec2(wmn.x, wmn.y + 46), wmx,
                    ImGui::GetColorU32(ImVec4(myAccent.x, myAccent.y, myAccent.z, 0.05f)), 6.0f);
                // My avatar: fetched once; the "Update avatar" button refreshes it.
                static std::string s_myAvatar; static bool s_avFetched = false;
                if (!s_avFetched && g_acMutex) {
                    SDL_LockMutex(g_acMutex);
                    if (g_acReady) { s_myAvatar = g_acAvatar; s_avFetched = true; }
                    SDL_UnlockMutex(g_acMutex);
                }
                g_myAvatarSlot = &s_myAvatar;   // let the editor below update it live
                SDL_Texture* myTex = AvatarTexture(s_myAvatar);
                if (myTex) {
                    dl->AddImageRounded((ImTextureID)(intptr_t)myTex, ImVec2(ctr.x - r, ctr.y - r),
                                        ImVec2(ctr.x + r, ctr.y + r), ImVec2(0,0), ImVec2(1,1), IM_COL32_WHITE, r);
                    dl->AddCircle(ctr, r, ImGui::GetColorU32(av), 32, 2.0f);
                } else {
                    dl->AddCircleFilled(
                        ctr, r, ImGui::GetColorU32(ImVec4(av.x * 0.55f, av.y * 0.55f, av.z * 0.55f, 1.0f)), 32);
                    dl->AddCircle(ctr, r, ImGui::GetColorU32(av), 32, 2.0f);
                    char initial[2] = { (char)(s.username.empty() ? '?' : std::toupper((unsigned char)s.username[0])), 0 };
                    float isz = ImGui::GetFontSize() * 1.6f;
                    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(isz, 1e9f, 0.0f, initial);
                    dl->AddText(ImGui::GetFont(), isz, ImVec2(ctr.x - ts.x * 0.5f, ctr.y - ts.y * 0.5f),
                                ImGui::GetColorU32(ImVec4(1, 1, 1, 1)), initial);
                }
                // Username drawn oversized next to the avatar; status underneath
                // with a colored presence dot.
                float tx = cp.x + r * 2 + 22;
                float nameSz = ImGui::GetFontSize() * 1.4f;
                dl->AddText(ImGui::GetFont(), nameSz, ImVec2(tx, cp.y + 8),
                            ImGui::GetColorU32(ImVec4(0.94f, 0.95f, 0.99f, 1)), s.username.c_str());
                ImVec4 stCol = account.IsOnline() ? ImVec4(0.55f, 0.9f, 0.6f, 1)
                                                  : ImVec4(0.70f, 0.72f, 0.78f, 1);
                ImVec2 dp(tx, cp.y + 8 + nameSz + 7);
                float sdr = ImGui::GetFontSize() * 0.28f;
                dl->AddCircleFilled(ImVec2(dp.x + sdr, dp.y + ImGui::GetTextLineHeight() * 0.55f),
                                    sdr, ImGui::GetColorU32(stCol), 12);
                ImGui::SetCursorScreenPos(ImVec2(dp.x + sdr * 2 + 7, dp.y));
                ImGui::TextColored(stCol, "%s", account.IsOnline() ? "Online" : "Local (this device)");
                // Custom status / tagline preview.
                if (acctStatus[0]) {
                    ImGui::SetCursorScreenPos(ImVec2(tx, dp.y + ImGui::GetTextLineHeight() + 4));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f,0.75f,0.82f,1));
                    ImGui::Text("\"%s\"", acctStatus);
                    ImGui::PopStyleColor();
                }
                ImGui::EndChild();
                // Profile stats row: achievements, games in library, friend code.
                {
                    int achN = (int)okay::Achievements::All().size();
                    int gameN = (int)scenes.size();
                    static std::string myCode; static bool codeFetched = false;
                    if (!codeFetched && g_acMutex) {
                        SDL_LockMutex(g_acMutex);
                        if (g_acReady) { myCode = g_acCode; codeFetched = true; }
                        SDL_UnlockMutex(g_acMutex);
                    }
                    ImGui::Dummy(ImVec2(0, 4));
                    ImGui::TextDisabled("%d achievement%s   -   %d game%s in library",
                                        achN, achN == 1 ? "" : "s", gameN, gameN == 1 ? "" : "s");
                    if (!myCode.empty()) {
                        ImGui::TextDisabled("Friend code:"); ImGui::SameLine();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f,0.78f,1.0f,1)); ImGui::TextUnformatted(myCode.c_str()); ImGui::PopStyleColor();
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Copy##code")) { ImGui::SetClipboardText(myCode.c_str()); Toast("Friend code copied", 1); }
                    }
                }
                ImGui::Dummy(ImVec2(0, 8));
                // View my own public profile card, exactly as friends see it.
                static bool myProfOpen = false; static okay::Friend myProf; static int myProfAch = 0, myProfGames = 0;
                if (account.IsOnline() && PrimaryButton("View my profile", ImVec2(200, 36))) {
                    myProf = okay::Friends(account).MyProfile();
                    myProfAch = (int)okay::Achievements::All().size();
                    myProfGames = (int)scenes.size();
                    myProfOpen = true;
                }
                if (account.IsOnline() && ImGui::IsItemHovered())
                    ImGui::SetTooltip("See your Steam-style profile card as other players see it");
                if (myProfOpen) { ImGui::OpenPopup("My profile"); myProfOpen = false; }
                ImGui::SetNextWindowSize(ImVec2(360, 0));
                if (ImGui::BeginPopup("My profile")) {
                    const okay::Friend& pf = myProf;
                    std::string pname = pf.display.empty() ? pf.username : pf.display;
                    float aw = ImGui::GetContentRegionAvail().x;
                    int pai = pf.accent; if (pai < 0 || pai >= (int)(sizeof(kAccentPresets)/sizeof(kAccentPresets[0]))) pai = 0;
                    ImVec4 pac = kAccentPresets[pai].col;
                    ImVec2 bnp = ImGui::GetCursorScreenPos(); ImDrawList* pdl = ImGui::GetWindowDrawList();
                    pdl->AddRectFilledMultiColor(bnp, ImVec2(bnp.x + aw, bnp.y + 54),
                        ImGui::GetColorU32(ImVec4(pac.x,pac.y,pac.z,0.45f)), ImGui::GetColorU32(ImVec4(pac.x,pac.y,pac.z,0.12f)),
                        ImGui::GetColorU32(ImVec4(pac.x,pac.y,pac.z,0.06f)), ImGui::GetColorU32(ImVec4(pac.x,pac.y,pac.z,0.26f)));
                    ImGui::Dummy(ImVec2(0, 30));
                    ImGui::SetCursorPosX((aw - 76) * 0.5f + ImGui::GetCursorPosX());
                    ImVec2 avp = ImGui::GetCursorScreenPos();
                    DrawAvatar(pf.avatar, pname, 76.0f);
                    pdl->AddCircle(ImVec2(avp.x+38, avp.y+38), 39.0f, IM_COL32(120,200,110,255), 40, 3.0f);
                    auto ctr = [&](const std::string& t, ImVec4 col){
                        float w = ImGui::CalcTextSize(t.c_str()).x;
                        float o = (ImGui::GetContentRegionAvail().x - w) * 0.5f; if (o > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + o);
                        ImGui::TextColored(col, "%s", t.c_str()); };
                    ctr(pname, ImVec4(0.95f,0.96f,1,1));
                    ctr("@" + pf.username, ImVec4(0.6f,0.63f,0.7f,1));
                    if (!pf.customStatus.empty()) ctr("\"" + pf.customStatus + "\"", ImVec4(0.72f,0.75f,0.82f,1));
                    ImGui::Dummy(ImVec2(0, 6));
                    ImGui::Separator();
                    ImGui::Text("%d achievement%s   -   %d game%s", myProfAch, myProfAch==1?"":"s", myProfGames, myProfGames==1?"":"s");
                    if (!pf.bio.empty()) { ImGui::SeparatorText("About"); ImGui::TextWrapped("%s", pf.bio.c_str()); }
                    if (!pf.favoriteGame.empty() || !pf.location.empty()) {
                        ImGui::SeparatorText("Details");
                        if (!pf.favoriteGame.empty()) { ImGui::TextColored(ImVec4(pac.x,pac.y,pac.z,1), "Favorite game"); ImGui::SameLine(); ImGui::TextWrapped(": %s", pf.favoriteGame.c_str()); }
                        if (!pf.location.empty()) { ImGui::TextColored(ImVec4(pac.x,pac.y,pac.z,1), "Location"); ImGui::SameLine(); ImGui::TextWrapped(": %s", pf.location.c_str()); }
                    }
                    ImGui::Dummy(ImVec2(0, 6));
                    ImGui::TextDisabled("Edit any of this under Customize profile below.");
                    ImGui::Dummy(ImVec2(0, 2));
                    if (ImGui::Button("Close", ImVec2(-1, 0))) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
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

                // Display name (friendly name others see; online accounts only).
                if (account.IsOnline()) {
                    ImGui::PushItemWidth(kFieldW);
                    ImGui::InputTextWithHint("##acctChgDisplay", "New display name", acctChgDisplay, sizeof(acctChgDisplay));
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::Button("Update display name", ImVec2(kBtnW, 0)) && acctChgDisplay[0]) {
                        bool ok = okay::Friends(account).SetDisplayName(acctChgDisplay);
                        acctMessageError = !ok;
                        acctMessage = ok ? "Display name updated." : "Couldn't update display name.";
                        if (ok) { acctChgDisplay[0] = '\0'; Toast("Display name updated", 1); }
                    }
                    ImGui::TextDisabled("Shown to friends. Your username (below) is your unique handle for adding friends.");

                    // Profile picture (avatar) by public image URL.
                    ImGui::PushItemWidth(kFieldW);
                    ImGui::InputTextWithHint("##acctAvatar", "Avatar image URL (png/jpg)", acctAvatar, sizeof(acctAvatar));
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::Button("Update avatar", ImVec2(kBtnW, 0)) && acctAvatar[0]) {
                        bool ok = okay::Friends(account).SetAvatar(acctAvatar);
                        acctMessageError = !ok;
                        acctMessage = ok ? "Avatar updated." : "Couldn't update avatar.";
                        if (ok) {
                            g_avatarCache.erase(acctAvatar);
                            if (g_myAvatarSlot) *g_myAvatarSlot = acctAvatar;   // refresh the card now
                            acctAvatar[0] = '\0';
                            Toast("Avatar updated", 1);
                        }
                    }
                    ImGui::TextDisabled("Paste a public link to a square PNG/JPG (e.g. an imgur/GitHub image).");

                    // Bio ("about me") shown on the profile card.
                    ImGui::TextDisabled("Bio");
                    ImGui::InputTextMultiline("##acctBio", acctBio, sizeof(acctBio), ImVec2(kFieldW + kBtnW + 8, 60));
                    if (ImGui::Button("Update bio", ImVec2(kBtnW, 0))) {
                        bool ok = okay::Friends(account).SetBio(acctBio);
                        acctMessageError = !ok;
                        acctMessage = ok ? "Bio updated." : "Couldn't update bio.";
                        if (ok) Toast("Bio updated", 1);
                    }

                    // ---- Customize profile (Steam-style) ----
                    ImGui::Dummy(ImVec2(0, 10));
                    ImGui::SeparatorText("Customize profile");
                    // Customization is loaded once by the background worker above
                    // (applied the moment it arrives) — never block here.
                    if (!acctProfileLoaded && g_acMutex) {
                        SDL_LockMutex(g_acMutex);
                        if (g_acReady) {
                            const okay::Friend& me = g_acProfile;
                            acctAccent = me.accent;
                            std::snprintf(acctStatus,   sizeof(acctStatus),   "%s", me.customStatus.c_str());
                            std::snprintf(acctFavGame,  sizeof(acctFavGame),  "%s", me.favoriteGame.c_str());
                            std::snprintf(acctLocation, sizeof(acctLocation), "%s", me.location.c_str());
                            acctProfileLoaded = true;
                        }
                        SDL_UnlockMutex(g_acMutex);
                    }
                    // Accent color swatches — click one to set the profile theme.
                    ImGui::TextDisabled("Profile color");
                    const int kNAcc = (int)(sizeof(kAccentPresets)/sizeof(kAccentPresets[0]));
                    for (int ai = 0; ai < kNAcc; ++ai) {
                        if (ai) ImGui::SameLine();
                        ImGui::PushID(2000 + ai);
                        ImVec4 c = kAccentPresets[ai].col;
                        ImVec2 sp = ImGui::GetCursorScreenPos();
                        if (ImGui::InvisibleButton("##acc", ImVec2(30, 30))) {
                            acctAccent = ai;
                            if (okay::Friends(account).SetAccent(ai)) Toast("Profile color updated", 1);
                        }
                        ImDrawList* d2 = ImGui::GetWindowDrawList();
                        d2->AddRectFilled(sp, ImVec2(sp.x+26, sp.y+26), ImGui::GetColorU32(c), 6.0f);
                        if (ai == acctAccent)
                            d2->AddRect(ImVec2(sp.x-2, sp.y-2), ImVec2(sp.x+28, sp.y+28),
                                        ImGui::GetColorU32(ImVec4(1,1,1,0.9f)), 7.0f, 0, 2.0f);
                        ImGui::PopID();
                    }
                    // Custom status / tagline.
                    ImGui::PushItemWidth(kFieldW);
                    ImGui::InputTextWithHint("##acctStatus", "Status / tagline (e.g. \"Making my dream game\")", acctStatus, sizeof(acctStatus));
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::Button("Save status", ImVec2(kBtnW, 0))) {
                        if (okay::Friends(account).SetStatus(acctStatus)) Toast("Status updated", 1);
                    }
                    // Favorite game showcase.
                    ImGui::PushItemWidth(kFieldW);
                    ImGui::InputTextWithHint("##acctFav", "Favorite game to show off", acctFavGame, sizeof(acctFavGame));
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::Button("Save favorite", ImVec2(kBtnW, 0))) {
                        if (okay::Friends(account).SetFavoriteGame(acctFavGame)) Toast("Favorite game updated", 1);
                    }
                    // Location.
                    ImGui::PushItemWidth(kFieldW);
                    ImGui::InputTextWithHint("##acctLoc", "Location (optional)", acctLocation, sizeof(acctLocation));
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::Button("Save location", ImVec2(kBtnW, 0))) {
                        if (okay::Friends(account).SetLocation(acctLocation)) Toast("Location updated", 1);
                    }
                    ImGui::TextDisabled("These show on your profile card when friends click your name.");
                }

                // Change username / handle (all backends).
                ImGui::PushItemWidth(kFieldW);
                ImGui::InputTextWithHint("##acctChgName", "New username (handle)", acctChgName, sizeof(acctChgName));
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

                // ---- Privacy & presence ----
                if (account.IsOnline()) {
                    ImGui::Dummy(ImVec2(0, 14));
                    ImGui::SeparatorText("Privacy & presence");
                    ImGui::SetNextItemWidth(200);
                    if (ImGui::Combo("Status", &g_onlineStatus, "Online\0Away\0Invisible\0")) {
                        okay::Friends pf(account);
                        if (g_onlineStatus == 2) pf.GoOffline(); else pf.MarkPresence(g_onlineStatus == 1 ? "Away" : "In OkaySpace");
                        SavePrefs();
                    }
                    if (ImGui::Checkbox("Share what I'm playing with friends", &g_sharePresence)) SavePrefs();
                    ImGui::TextDisabled("When off, friends won't see the game you're playing.");

                    // ---- Testing ----
                    ImGui::Dummy(ImVec2(0, 14));
                    ImGui::SeparatorText("Testing");
                    ImGui::TextDisabled("Add OkayBot — a test friend that auto-accepts and replies to your DMs.");
                    if (ImGui::Button("Add OkayBot", ImVec2(160, 0))) {
                        std::string be;
                        if (okay::Friends(account).Request("OkayBot", &be)) Toast("OkayBot added — open Friends", 1);
                        else Toast(be.empty() ? "Couldn't add OkayBot" : be, 2);
                    }
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
                    ? "Sign in to your OkaySpace account to sync your work and use "
                      "Friends and the Marketplace."
                    : "Using a local account on this device.");
                ImGui::Dummy(ImVec2(0, 12));

                // --- Centered sign-in / create-account card ---
                const float cardW = 420.0f;
                float offx = (ImGui::GetContentRegionAvail().x - cardW) * 0.5f;
                if (offx > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offx);
                ImGui::BeginChild("##authcard", ImVec2(cardW, 0), true);
                float innerW = ImGui::GetContentRegionAvail().x;

                // Centered OkaySpace logo mark (ringed planet) atop the card.
                {
                    const float L = 46.0f;
                    ImGui::Dummy(ImVec2(0, 6));
                    float cxpos = (ImGui::GetContentRegionAvail().x - L) * 0.5f;
                    if (cxpos > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cxpos);
                    ImVec2 lp = ImGui::GetCursorScreenPos();
                    ImDrawList* ld = ImGui::GetWindowDrawList();
                    ImVec2 c(lp.x + L * 0.5f, lp.y + L * 0.5f);
                    ld->AddCircleFilled(c, L * 0.30f, ImGui::GetColorU32(kAccent), 28);
                    ld->AddCircleFilled(ImVec2(c.x - L*0.07f, c.y - L*0.07f), L*0.30f, IM_COL32(255,255,255,40), 28);
                    ImVec2 ring[28];
                    for (int k = 0; k < 28; ++k) {
                        float a = k * (6.2831853f/28), rx = L*0.46f, ry = L*0.16f, rot = -0.5f;
                        float ex = std::cos(a)*rx, ey = std::sin(a)*ry;
                        ring[k] = ImVec2(c.x + ex*std::cos(rot) - ey*std::sin(rot),
                                         c.y + ex*std::sin(rot) + ey*std::cos(rot));
                    }
                    ld->AddPolyline(ring, 28, IM_COL32(220,232,255,235), ImDrawFlags_Closed, 1.8f);
                    ImGui::Dummy(ImVec2(L, L));
                }

                // Heading (centered).
                auto centerText = [&](const char* t, bool disabled) {
                    float w = ImGui::CalcTextSize(t).x;
                    float cxpos = (ImGui::GetContentRegionAvail().x - w) * 0.5f;
                    if (cxpos > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cxpos);
                    if (disabled) ImGui::TextDisabled("%s", t); else ImGui::TextUnformatted(t);
                };
                centerText(acctRegisterMode ? "Create your account" : "Sign in to OkaySpace", false);
                centerText(acctRegisterMode
                    ? "Join to use Friends, the Marketplace, DMs and forums."
                    : "Welcome back.", true);
                ImGui::Dummy(ImVec2(0, 10));

                // Segmented Sign in / Create toggle (two half-width buttons). The
                // active mode is clearly highlighted in the accent with white text.
                auto seg = [&](const char* label, bool active, float w) {
                    if (active) {
                        ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccent);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
                    }
                    bool c = ImGui::Button(label, ImVec2(w, 0));
                    if (active) ImGui::PopStyleColor(3);
                    return c;
                };
                float halfW = (innerW - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
                if (seg("Sign in", !acctRegisterMode, halfW)) { acctRegisterMode = false; acctMessage.clear(); }
                ImGui::SameLine();
                if (seg("Create account", acctRegisterMode, halfW)) { acctRegisterMode = true; acctMessage.clear(); }
                ImGui::Dummy(ImVec2(0, 10));

                // Register takes an email; sign-in accepts email OR username.
                const char* idHint = !account.UsesEmail() ? "Username"
                                   : acctRegisterMode ? "Email" : "Email or username";
                ImGui::TextDisabled("%s", idHint);
                ImGui::SetNextItemWidth(innerW);
                ImGui::InputTextWithHint("##acctUser",
                    !account.UsesEmail() ? "username" : acctRegisterMode ? "you@example.com" : "email or username",
                    acctUser, sizeof(acctUser));

                if (acctRegisterMode && account.UsesEmail()) {
                    ImGui::TextDisabled("Username (unique handle)");
                    ImGui::SetNextItemWidth(innerW);
                    ImGui::InputTextWithHint("##acctName", "e.g. starpilot", acctName, sizeof(acctName));
                    ImGui::TextDisabled("Display name (optional)");
                    ImGui::SetNextItemWidth(innerW);
                    ImGui::InputTextWithHint("##acctDisplay", "e.g. Star Pilot", acctDisplay, sizeof(acctDisplay));
                }

                ImGui::TextDisabled("Password");
                ImGuiInputTextFlags pf = ImGuiInputTextFlags_EnterReturnsTrue |
                    (acctShowPass ? 0 : ImGuiInputTextFlags_Password);
                ImGui::SetNextItemWidth(innerW - 86);
                bool submit = ImGui::InputTextWithHint("##acctPass", "••••••••", acctPass, sizeof(acctPass), pf);
                ImGui::SameLine();
                if (ImGui::Button(acctShowPass ? "Hide" : "Show", ImVec2(80, 0))) acctShowPass = !acctShowPass;
                if (acctRegisterMode && account.UsesEmail())
                    ImGui::TextDisabled("Use a real email (for password resets). Password: 6+ characters.");
                ImGui::Dummy(ImVec2(0, 10));

                const char* btn = acctRegisterMode ? "Create account" : "Sign in";
                ImGui::BeginDisabled(acctBusy);
                if (PrimaryButton(btn, ImVec2(innerW, 46)) || submit) {
                    acctBusy = true;
                    // On sign-up, require a username and reject one already taken.
                    acct::Result r;
                    if (acctRegisterMode && account.UsesEmail() && !acctName[0]) {
                        r.ok = false; r.error = "Choose a username.";
                    } else if (acctRegisterMode && account.UsesEmail() &&
                               !account.UsernameAvailable(acctName)) {
                        r.ok = false; r.error = "That username is taken — pick another.";
                    } else {
                        r = acctRegisterMode
                            ? account.Register(acctUser, acctPass, acctName)
                            : account.Login(acctUser, acctPass);
                    }
                    acctBusy = false;
                    acctMessageError = !r.ok;
                    if (r.ok) {
                        acctMessage.clear();
                        // Set the optional display name right after signing up.
                        if (acctRegisterMode && account.IsLoggedIn() && acctDisplay[0])
                            okay::Friends(account).SetDisplayName(acctDisplay);
                        std::fill(acctPass, acctPass + sizeof(acctPass), '\0');
                        acctDisplay[0] = '\0';
                        if (account.IsLoggedIn()) Toast("Signed in", 1);
                    } else {
                        acctMessage = r.error;
                    }
                }
                ImGui::EndDisabled();

                // Forgot password (online/Supabase): emails a reset link.
                if (account.UsesEmail() && !acctRegisterMode) {
                    if (ImGui::SmallButton("Forgot password?")) {
                        acct::Result r = account.RequestPasswordReset(acctUser);
                        acctMessageError = !r.ok;
                        acctMessage = r.ok ? "Password reset email sent — check your inbox."
                                           : r.error;
                    }
                }

                if (!acctMessage.empty()) {
                    ImGui::Dummy(ImVec2(0, 4));
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextColored(acctMessageError ? ImVec4(1, 0.55f, 0.55f, 1)
                                                        : ImVec4(0.55f, 0.9f, 0.6f, 1),
                                       "%s", acctMessage.c_str());
                    ImGui::PopTextWrapPos();
                }
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::EndChild();
            }
        } else if (tab == 4) {                            // ---- Settings ----
            sectionHeader("Settings", nullptr);
            ImGui::SeparatorText("Account");
            // The account server is baked into this build, so there's nothing to
            // configure here — just a plain connection status (no URL shown).
            if (accountPtr->IsOnline())
                ImGui::TextDisabled("Accounts are online. Sign in from the Account tab.");
            else
                ImGui::TextDisabled("Using local accounts on this device.");

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
            // Persist + restyle when the drag ends, so releasing the slider is
            // enough — no separate Apply click needed.
            if (ImGui::IsItemDeactivatedAfterEdit()) { DarkTheme(); SavePrefs(); }
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::TextDisabled("applies on release");

            // ---- Loading screen ----
            ImGui::Dummy(ImVec2(0, 16));
            ImGui::SeparatorText("Loading screen");
            ImGui::TextDisabled("Customize the startup splash. Changes apply on the next launch.");
            {
                char lb[260]; std::snprintf(lb, sizeof(lb), "%s", g_splashLogo.c_str());
                ImGui::SetNextItemWidth(360);
                if (ImGui::InputTextWithHint("##splashlogo", "Logo image path (PNG) — blank = built-in cube", lb, sizeof(lb)))
                    { g_splashLogo = lb; SavePrefs(); }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear##splashlogo")) { g_splashLogo.clear(); SavePrefs(); }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("A PNG shown instead of the cube. Drag a .png onto a path field or paste its location.");
                if (ImGui::Checkbox("Custom splash accent", &g_splashAccentOverride)) SavePrefs();
                if (g_splashAccentOverride) {
                    ImGui::SameLine(); ImGui::SetNextItemWidth(220);
                    if (ImGui::ColorEdit3("##splashaccent", g_splashAccent)) SavePrefs();
                }
                ImGui::SetNextItemWidth(220);
                if (ImGui::ColorEdit3("Background top##splash", g_splashBgTop)) SavePrefs();
                ImGui::SetNextItemWidth(220);
                if (ImGui::ColorEdit3("Background bottom##splash", g_splashBgBot)) SavePrefs();
                if (ImGui::Checkbox("Show progress bar##splash", &g_splashShowBar)) SavePrefs();
            }

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
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy version")) {
                ImGui::SetClipboardText("OkaySpace v" OKAY_ENGINE_VERSION);
                Toast("Version copied", 1);
            }

            // ---- Game libraries ----
            ImGui::Dummy(ImVec2(0, 16));
            ImGui::SeparatorText("Game libraries");
            ImGui::TextDisabled("Folders scanned for games (alongside the built-in ones next to the launcher).");
            {
                static char libBuf[512] = "";
                int removeIdx = -1;
                for (std::size_t li = 0; li < g_libraryDirs.size(); ++li) {
                    ImGui::PushID((int)li);
                    if (ImGui::SmallButton("Remove")) removeIdx = (int)li;
                    ImGui::SameLine();
                    ImGui::TextUnformatted(g_libraryDirs[li].c_str());
                    ImGui::PopID();
                }
                if (removeIdx >= 0) {
                    g_libraryDirs.erase(g_libraryDirs.begin() + removeIdx);
                    SavePrefs(); scenes = FindScenes();
                    Toast("Library folder removed", 1);
                }
                ImGui::SetNextItemWidth(360);
                bool addSubmit = ImGui::InputTextWithHint("##libadd", "Paste a folder path to scan...",
                                                          libBuf, sizeof(libBuf),
                                                          ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                if ((ImGui::Button("Add folder") || addSubmit) && libBuf[0]) {
                    std::error_code lec;
                    std::string pathStr = libBuf;
                    if (!fs::exists(fs::path(pathStr), lec) || !fs::is_directory(fs::path(pathStr), lec)) {
                        Toast("That folder doesn't exist", 2);
                    } else if (std::find(g_libraryDirs.begin(), g_libraryDirs.end(), pathStr) != g_libraryDirs.end()) {
                        Toast("Already in your libraries", 2);
                    } else {
                        g_libraryDirs.push_back(pathStr);
                        SavePrefs(); scenes = FindScenes();
                        Toast("Library folder added", 1);
                        libBuf[0] = '\0';
                    }
                }
            }

            // ---- Maintenance ----
            ImGui::Dummy(ImVec2(0, 16));
            ImGui::SeparatorText("Maintenance");
            if (ImGui::Button("Open config folder")) OpenExternal(g_exeDir);
            ImGui::SameLine();
            if (ImGui::Button("Reset preferences")) ImGui::OpenPopup("Reset preferences?");
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
                                    ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Reset preferences?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Reset theme, accent, UI scale, sorting and update settings?");
                ImGui::TextDisabled("Favorites and recent games are kept.");
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.22f, 0.22f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.28f, 0.28f, 1.0f));
                bool doReset = ImGui::Button("Reset", ImVec2(110, 0));
                ImGui::PopStyleColor(2);
                if (doReset) {
                    g_accentIndex = 0; g_themeIndex = 0; g_uiScale = 1.0f;
                    g_updateOnLaunch = false; g_playSort = 0;
                    ImGui::GetIO().FontGlobalScale = g_uiScale;
                    ApplyAccent(g_accentIndex);   // re-applies theme + accent
                    SavePrefs();
                    Toast("Preferences reset", 1);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(110, 0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::TextDisabled("Preferences are stored in launcher.cfg next to the launcher.");
        } else if (tab == 5) {                            // ---- Achievements ----
            sectionHeader("Achievements", nullptr);
            if (loginGate("achievements")) {
            std::vector<okay::Achievement> achs = okay::Achievements::All();
            // Summary banner: big trophy + unlocked count.
            {
                ImGui::BeginChild("##achsum", ImVec2(0, 64), true);
                ImVec2 p = ImGui::GetCursorScreenPos(); ImDrawList* dl = ImGui::GetWindowDrawList();
                float cx = p.x + 26, cy = p.y + 30, r = 16.0f; ImU32 gold = IM_COL32(240, 196, 92, 255);
                dl->AddRectFilled(ImVec2(cx-r*0.6f,cy-r),ImVec2(cx+r*0.6f,cy-r*0.1f),gold,3.0f);
                dl->AddRectFilled(ImVec2(cx-r*0.12f,cy-r*0.1f),ImVec2(cx+r*0.12f,cy+r*0.55f),gold);
                dl->AddRectFilled(ImVec2(cx-r*0.7f,cy+r*0.55f),ImVec2(cx+r*0.7f,cy+r),gold,2.0f);
                ImGui::SetCursorPosX(58); ImGui::SetCursorPosY(ImGui::GetCursorPosY()+10);
                ImGui::BeginGroup();
                ImGui::Text("%d achievement%s unlocked", (int)achs.size(), achs.size()==1?"":"s");
                ImGui::TextDisabled("Across all your OkaySpace games");
                ImGui::EndGroup();
                ImGui::EndChild();
                ImGui::Dummy(ImVec2(0, 6));
            }
            static char achFilter[64] = "";
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##achsearch", "Search achievements...", achFilter, sizeof(achFilter));
            ImGui::Dummy(ImVec2(0, 4));
            auto alo = [](std::string s){ for (char& c : s) c=(char)std::tolower((unsigned char)c); return s; };
            std::string an = alo(achFilter);
            if (achs.empty()) {
                ImGui::Dummy(ImVec2(0, 24));
                ImGui::TextDisabled("No achievements yet.");
                ImGui::TextDisabled("Play a game whose actions call \"Unlock Achievement\" to earn your first.");
            } else {
                ImGui::BeginChild("##achlist", ImVec2(0, ImGui::GetContentRegionAvail().y - 46.0f), false);
                int shownA = 0;
                for (std::size_t i = achs.size(); i-- > 0; ) {   // newest first
                    const okay::Achievement& a = achs[i];
                    if (!an.empty() && alo(a.label + " " + a.id).find(an) == std::string::npos) continue;
                    ++shownA;
                    ImGui::PushID((int)i);
                    ImGui::BeginChild("##achrow", ImVec2(0, 56), true);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float cy = p.y + 18.0f, cx = p.x + 16.0f, r = 11.0f;
                    ImU32 gold = IM_COL32(240, 196, 92, 255);
                    dl->AddRectFilled(ImVec2(cx - r * 0.6f, cy - r), ImVec2(cx + r * 0.6f, cy - r * 0.1f), gold, 3.0f);
                    dl->AddRectFilled(ImVec2(cx - r * 0.12f, cy - r * 0.1f), ImVec2(cx + r * 0.12f, cy + r * 0.55f), gold);
                    dl->AddRectFilled(ImVec2(cx - r * 0.7f, cy + r * 0.55f), ImVec2(cx + r * 0.7f, cy + r), gold, 2.0f);
                    ImGui::Dummy(ImVec2(40, 0)); ImGui::SameLine();
                    ImGui::BeginGroup();
                    ImGui::TextUnformatted(a.label.c_str());
                    char when[48] = "";
                    if (a.when > 0) { std::time_t t = (std::time_t)a.when; std::tm* tm = std::localtime(&t);
                                      if (tm) std::strftime(when, sizeof(when), "Unlocked %b %d, %Y", tm); }
                    ImGui::TextDisabled("%s%s%s", a.id.c_str(), when[0] ? "  -  " : "", when);
                    ImGui::EndGroup();
                    ImGui::EndChild();
                    ImGui::PopID();
                }
                if (shownA == 0) {
                    ImGui::Dummy(ImVec2(0, 20));
                    ImGui::TextDisabled("No achievements match \"%s\".", achFilter);
                }
                ImGui::EndChild();
            }
            }   // end achievements login gate
        } else if (tab == 6) {                            // ---- Friends ----
            sectionHeader("Friends", nullptr);
            // Viewing Friends clears the unread-DM badge.
            if (account.IsLoggedIn() && (g_dmUnread > 0 || g_newestDmId > g_lastDmId)) {
                g_lastDmId = g_newestDmId; g_dmUnread = 0; SavePrefs();
            }
            okay::Friends fr(account);
            if (!fr.Available()) {
                ImGui::Dummy(ImVec2(0, 20));
                ImGui::TextDisabled("Sign in to add friends and see who's online.");
                ImGui::Dummy(ImVec2(0, 6));
                if (PrimaryButton("Sign in", ImVec2(160, 38))) { tab = 3; g_lastTab = 3; SavePrefs(); }
            } else {
                static char addBuf[48] = "";
                static std::string fErr;
                static std::vector<okay::Friend> fCache;
                static std::vector<okay::FriendEvent> fFeed;
                static std::string fCode;
                // Direct messages now live in the floating Chat window (DrawChatWindow);
                // the "Message" buttons call OpenChatWith().
                static okay::Friend profFriend;   // friend whose profile card is open
                static bool profOpen = false;
                static Uint32 fNextRefresh = 0;
                Uint32 now = SDL_GetTicks();
                // EVERYTHING network for this tab (ensure-profile, presence, my code,
                // list, feed) runs on the Friends worker thread — the tab never
                // blocks on the network, so switching to Friends can't freeze.
                if (!g_frMutex) g_frMutex = SDL_CreateMutex();
                if (now >= fNextRefresh && !g_frFetching) {
                    g_frFetching = true; fNextRefresh = now + 10000;
                    if (SDL_Thread* t = SDL_CreateThread(FriendsListWorker, "okay-frlist", nullptr))
                        SDL_DetachThread(t);
                    else g_frFetching = false;
                }
                if (g_frMutex) {
                    SDL_LockMutex(g_frMutex);
                    if (g_frReady) { fCache = g_frList; fFeed = g_frFeed; fCode = g_frCode; g_frReady = false; }
                    SDL_UnlockMutex(g_frMutex);
                }

                // --- Status selector (Online / Away / Invisible) ---
                ImGui::TextDisabled("Status:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(140);
                if (ImGui::Combo("##status", &g_onlineStatus, "Online\0Away\0Invisible\0")) {
                    fNextRefresh = 0;   // let the worker broadcast the new presence (no UI block)
                    SavePrefs();
                }
                ImGui::Spacing();

                // --- Add a friend (username or code) + my own shareable code ---
                ImGui::SetNextItemWidth(240);
                bool submit = ImGui::InputTextWithHint("##addfriend", "Add by username or friend code...",
                                                       addBuf, sizeof(addBuf),
                                                       ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                if ((ImGui::Button("Add") || submit) && addBuf[0]) {
                    // Try username first, then fall back to a friend code.
                    std::string e1, e2;
                    if (fr.Request(addBuf, &e1) || fr.RequestByCode(addBuf, &e2)) {
                        Toast(std::string("Friend request sent to ") + addBuf, 1);
                        addBuf[0] = '\0'; fErr.clear(); fNextRefresh = 0;
                    } else {
                        fErr = "No player with that username or code.";
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Refresh")) fNextRefresh = 0;
                if (!fCode.empty()) {
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 196);
                    ImGui::TextDisabled("Your code:");
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
                    ImGui::TextUnformatted(fCode.c_str());
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Copy")) { ImGui::SetClipboardText(fCode.c_str()); Toast("Friend code copied", 1); }
                }
                if (!fErr.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f,0.45f,0.45f,1));
                                     ImGui::TextWrapped("%s", fErr.c_str()); ImGui::PopStyleColor(); }

                // Private display name: a nickname you set, else their display
                // name, else the username.
                auto nameOf = [&](const okay::Friend* f) -> std::string {
                    auto it = g_friendNicks.find(f->id);
                    if (it != g_friendNicks.end() && !it->second.empty()) return it->second;
                    return f->display.empty() ? f->username : f->display;
                };

                // Search + summary row.
                static char friendFilter[64] = "";
                ImGui::SetNextItemWidth(240);
                ImGui::InputTextWithHint("##friendsearch", "Search friends...", friendFilter, sizeof(friendFilter));
                std::string ffilt = friendFilter;
                for (auto& ch : ffilt) ch = (char)std::tolower((unsigned char)ch);
                auto matches = [&](const okay::Friend* f) {
                    if (ffilt.empty()) return true;
                    std::string hay = nameOf(f) + " @" + f->username;
                    for (auto& ch : hay) ch = (char)std::tolower((unsigned char)ch);
                    return hay.find(ffilt) != std::string::npos;
                };
                int acceptedN = 0, onlineN = 0, reqN = 0;
                for (const auto& f : fCache) {
                    if (f.status == "accepted") { ++acceptedN; if (f.online) ++onlineN; }
                    else if (f.incoming) ++reqN;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%d of %d online", onlineN, acceptedN);
                if (reqN > 0) { ImGui::SameLine(); ImGui::TextColored(kAccent, "%d request%s", reqN, reqN==1?"":"s"); }

                // Split: requests / favorites / online / offline / outgoing-pending.
                std::vector<const okay::Friend*> reqs, favs, online, offline, pending;
                for (const auto& f : fCache) {
                    if (!matches(&f)) continue;
                    bool fav = g_favFriends.count(f.id) != 0;
                    if (f.status != "accepted" && f.incoming) reqs.push_back(&f);
                    else if (f.status != "accepted")          pending.push_back(&f);
                    else if (fav)                             favs.push_back(&f);
                    else if (f.online)                        online.push_back(&f);
                    else                                      offline.push_back(&f);
                }
                // Within a section: online first, then name.
                auto byName = [&](const okay::Friend* a, const okay::Friend* b) {
                    if (a->online != b->online) return a->online > b->online;
                    std::string na = nameOf(a), nb = nameOf(b);
                    for (auto& c : na) c = (char)std::tolower((unsigned char)c);
                    for (auto& c : nb) c = (char)std::tolower((unsigned char)c);
                    return na < nb;
                };
                std::sort(favs.begin(), favs.end(), byName);
                std::sort(online.begin(), online.end(), byName);
                std::sort(offline.begin(), offline.end(), byName);

                // Nickname editor popup state.
                static std::string nickUid; static char nickBuf[48] = ""; static bool nickOpen = false;

                ImGui::Spacing();
                ImGui::BeginChild("##friendlist", ImVec2(0, ImGui::GetContentRegionAvail().y - 46.0f), false);
                auto drawFriend = [&](const okay::Friend* f, bool request) {
                    bool fav = g_favFriends.count(f->id) != 0;
                    std::string dispName = nameOf(f);
                    ImGui::PushID((int)f->rowId);
                    ImGui::BeginChild("##frow", ImVec2(0, 48), true);
                    // Avatar (32px, clickable -> profile card) with a status dot.
                    ImGui::SetCursorPos(ImVec2(6, 6));
                    if (ImGui::InvisibleButton("##avbtn", ImVec2(32, 32))) {
                        profFriend = *f; profOpen = true;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetCursorPos(ImVec2(6, 6));
                    ImVec2 ap = ImGui::GetCursorScreenPos();
                    DrawAvatar(f->avatar, dispName, 32.0f);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 dot = f->online ? IM_COL32(120, 200, 110, 255) : IM_COL32(120, 128, 140, 255);
                    dl->AddCircleFilled(ImVec2(ap.x + 28, ap.y + 28), 5.5f, dot, 16);
                    dl->AddCircle(ImVec2(ap.x + 28, ap.y + 28), 5.5f, IM_COL32(45, 45, 47, 255), 16, 2.0f);
                    if (fav) DrawEmoji(dl, ImVec2(ap.x + 2, ap.y + 2), 12, 5);   // gold star badge
                    ImGui::SameLine(); ImGui::SetCursorPosX(46);
                    ImGui::BeginGroup();
                    ImGui::TextUnformatted(dispName.c_str());
                    ImGui::SameLine(0, 6);
                    ImGui::TextDisabled("@%s", f->username.c_str());
                    bool playing = f->online && !f->playing.empty();
                    std::string sub = request ? "wants to be your friend"
                                   : f->status != "accepted" ? "request sent"
                                   : playing ? f->playing
                                   : f->online ? "Online" : "Offline";
                    ImVec4 sc = playing ? kAccent
                              : (!request && f->online) ? ImVec4(0.55f,0.9f,0.6f,1)
                                                        : ImVec4(0.60f,0.63f,0.70f,1);
                    ImGui::TextColored(sc, "%s", sub.c_str());
                    ImGui::EndGroup();
                    bool accepted = !request && f->status == "accepted";
                    bool joinable = accepted && f->online && !f->sessionCode.empty();
                    float btnW = request ? 170.0f : accepted ? (joinable ? 170.0f : 92.0f) : 86.0f;
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - btnW);
                    if (request) {
                        if (ImGui::Button("Accept")) { fr.Accept(f->rowId); fNextRefresh = 0; }
                        ImGui::SameLine();
                        if (ImGui::Button("Decline")) { fr.Remove(f->rowId); fNextRefresh = 0; }
                    } else if (accepted) {
                        if (ImGui::Button("Message")) OpenChatWith(f->id, dispName, f->avatar);
                        if (joinable) {
                            ImGui::SameLine();
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.44f, 0.26f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.56f, 0.33f, 1.0f));
                            if (ImGui::Button("Join")) {
                                ImGui::SetClipboardText(f->sessionCode.c_str());
                                Toast("Session code " + f->sessionCode + " copied - open the game and Join with it", 2);
                            }
                            ImGui::PopStyleColor(2);
                        }
                    } else {
                        if (ImGui::SmallButton("Cancel")) { fr.Remove(f->rowId); fNextRefresh = 0; }
                    }
                    // Right-click menu: favorite, nickname, copy, remove (Discord-style).
                    if (ImGui::BeginPopupContextWindow("##frctx", ImGuiPopupFlags_MouseButtonRight)) {
                        if (accepted) {
                            if (ImGui::MenuItem("Message")) OpenChatWith(f->id, dispName, f->avatar);
                            if (joinable && ImGui::MenuItem("Join their game")) {
                                ImGui::SetClipboardText(f->sessionCode.c_str());
                                Toast("Session code " + f->sessionCode + " copied", 2);
                            }
                            if (ImGui::MenuItem(fav ? "Unfavorite" : "Favorite")) {
                                if (fav) g_favFriends.erase(f->id); else g_favFriends.insert(f->id);
                                SavePrefs();
                            }
                            if (ImGui::MenuItem("Set nickname...")) {
                                nickUid = f->id;
                                std::snprintf(nickBuf, sizeof(nickBuf), "%s",
                                              g_friendNicks.count(f->id) ? g_friendNicks[f->id].c_str() : "");
                                nickOpen = true;
                                ImGui::CloseCurrentPopup();
                            }
                            if (g_friendNicks.count(f->id) && ImGui::MenuItem("Clear nickname")) {
                                g_friendNicks.erase(f->id); SavePrefs();
                            }
                            ImGui::Separator();
                        }
                        if (ImGui::MenuItem("Copy username")) { ImGui::SetClipboardText(f->username.c_str()); Toast("Username copied", 1); }
                        if (ImGui::MenuItem("View profile")) { profFriend = *f; profOpen = true; }
                        ImGui::Separator();
                        if (ImGui::MenuItem(request ? "Decline request" : accepted ? "Remove friend" : "Cancel request")) {
                            fr.Remove(f->rowId); fNextRefresh = 0;
                        }
                        ImGui::EndPopup();
                    }
                    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
                        ImGui::SetTooltip("Right-click for more");
                    ImGui::EndChild();
                    ImGui::PopID();
                };
                if (!reqs.empty())    { ImGui::SeparatorText(("Friend requests (" + std::to_string(reqs.size()) + ")").c_str()); for (auto* f : reqs)    drawFriend(f, true);  }
                if (!favs.empty())    { ImGui::SeparatorText(("Favorites (" + std::to_string(favs.size()) + ")").c_str());       for (auto* f : favs)    drawFriend(f, false); }
                if (!online.empty())  { ImGui::SeparatorText(("Online (" + std::to_string(online.size()) + ")").c_str());        for (auto* f : online)  drawFriend(f, false); }
                if (!offline.empty()) { ImGui::SeparatorText(("Offline (" + std::to_string(offline.size()) + ")").c_str());      for (auto* f : offline) drawFriend(f, false); }
                if (!pending.empty()) { ImGui::SeparatorText(("Sent requests (" + std::to_string(pending.size()) + ")").c_str()); for (auto* f : pending) drawFriend(f, false); }
                if (!ffilt.empty() && reqs.empty() && favs.empty() && online.empty() && offline.empty() && pending.empty())
                    ImGui::TextDisabled("No friends match \"%s\".", friendFilter);

                // Nickname editor popup.
                if (nickOpen) { ImGui::OpenPopup("Set nickname"); nickOpen = false; }
                if (ImGui::BeginPopupModal("Set nickname", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextDisabled("Only you see this nickname.");
                    ImGui::SetNextItemWidth(260);
                    bool ok = ImGui::InputText("##nick", nickBuf, sizeof(nickBuf), ImGuiInputTextFlags_EnterReturnsTrue);
                    if (ImGui::Button("Save", ImVec2(110, 0)) || ok) {
                        if (!nickUid.empty()) {
                            if (nickBuf[0]) g_friendNicks[nickUid] = nickBuf; else g_friendNicks.erase(nickUid);
                            SavePrefs();
                        }
                        nickUid.clear(); ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(110, 0))) { nickUid.clear(); ImGui::CloseCurrentPopup(); }
                    ImGui::EndPopup();
                }

                // --- Steam-style profile card popup (opened from a friend avatar) ---
                if (profOpen) { ImGui::OpenPopup("##profilecard"); profOpen = false; }
                ImGui::SetNextWindowSize(ImVec2(340, 0));
                if (ImGui::BeginPopup("##profilecard")) {
                    const okay::Friend& pf = profFriend;
                    std::string pname = pf.display.empty() ? pf.username : pf.display;
                    float aw = ImGui::GetContentRegionAvail().x;
                    // The player's chosen profile accent colors the banner (Steam-style).
                    int pai = pf.accent; if (pai < 0 || pai >= (int)(sizeof(kAccentPresets)/sizeof(kAccentPresets[0]))) pai = 0;
                    ImVec4 pac = kAccentPresets[pai].col;
                    // Accent banner header with the avatar (ringed by presence color).
                    ImVec2 bnp = ImGui::GetCursorScreenPos();
                    ImDrawList* pdl = ImGui::GetWindowDrawList();
                    pdl->AddRectFilledMultiColor(bnp, ImVec2(bnp.x + aw, bnp.y + 54),
                        ImGui::GetColorU32(ImVec4(pac.x,pac.y,pac.z,0.45f)),
                        ImGui::GetColorU32(ImVec4(pac.x,pac.y,pac.z,0.12f)),
                        ImGui::GetColorU32(ImVec4(pac.x,pac.y,pac.z,0.06f)),
                        ImGui::GetColorU32(ImVec4(pac.x,pac.y,pac.z,0.26f)));
                    ImGui::Dummy(ImVec2(0, 30));
                    ImGui::SetCursorPosX((aw - 76) * 0.5f + ImGui::GetCursorPosX());
                    ImVec2 avp = ImGui::GetCursorScreenPos();
                    DrawAvatar(pf.avatar, pname, 76.0f);
                    ImU32 ring = pf.online ? IM_COL32(120,200,110,255) : IM_COL32(120,128,140,255);
                    pdl->AddCircle(ImVec2(avp.x+38, avp.y+38), 39.0f, ring, 40, 3.0f);
                    auto ctr = [&](const std::string& t, ImVec4 col){
                        float w = ImGui::CalcTextSize(t.c_str()).x;
                        float o = (ImGui::GetContentRegionAvail().x - w) * 0.5f; if (o > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + o);
                        ImGui::TextColored(col, "%s", t.c_str());
                    };
                    ctr(pname, ImVec4(0.95f,0.96f,1,1));
                    ctr("@" + pf.username, ImVec4(0.6f,0.63f,0.7f,1));
                    std::string stx = (pf.online && !pf.playing.empty()) ? pf.playing
                                     : pf.online ? "Online" : "Offline";
                    ImVec4 stc = (pf.online && !pf.playing.empty()) ? kAccent
                               : pf.online ? ImVec4(0.55f,0.9f,0.6f,1) : ImVec4(0.6f,0.63f,0.7f,1);
                    ctr(stx, stc);
                    // Custom status / tagline, centered under the presence line.
                    if (!pf.customStatus.empty()) ctr("\"" + pf.customStatus + "\"", ImVec4(0.72f,0.75f,0.82f,1));
                    ImGui::Dummy(ImVec2(0, 6));
                    if (!pf.bio.empty()) {
                        ImGui::SeparatorText("About");
                        ImGui::TextWrapped("%s", pf.bio.c_str());
                    }
                    if (!pf.favoriteGame.empty() || !pf.location.empty()) {
                        ImGui::SeparatorText("Details");
                        if (!pf.favoriteGame.empty()) {
                            ImGui::TextColored(ImVec4(pac.x,pac.y,pac.z,1), "Favorite game");
                            ImGui::SameLine(); ImGui::TextWrapped(": %s", pf.favoriteGame.c_str());
                        }
                        if (!pf.location.empty()) {
                            ImGui::TextColored(ImVec4(pac.x,pac.y,pac.z,1), "Location");
                            ImGui::SameLine(); ImGui::TextWrapped(": %s", pf.location.c_str());
                        }
                    }
                    // Their recent activity (filtered from the feed).
                    bool anyAct = false;
                    for (const auto& e : fFeed) if (e.actor == pf.username || e.actor == pname) { anyAct = true; break; }
                    if (anyAct) {
                        ImGui::SeparatorText("Recent activity");
                        int n = 0;
                        for (const auto& e : fFeed) {
                            if (e.actor != pf.username && e.actor != pname) continue;
                            if (n++ >= 4) break;
                            const char* verb = e.kind == "playing" ? "Played " : e.kind == "achievement" ? "Unlocked "
                                             : e.kind == "publish" ? "Published " : e.kind == "online" ? "Came online" : "";
                            ImGui::BulletText("%s%s", verb, e.detail.c_str());
                        }
                    }
                    ImGui::Dummy(ImVec2(0, 4));
                    ImGui::Separator();
                    // Working actions.
                    if (ImGui::Button("Message", ImVec2(-1, 0))) {
                        OpenChatWith(pf.id, pname, pf.avatar);
                        ImGui::CloseCurrentPopup();
                    }
                    if (pf.online && !pf.sessionCode.empty()) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f,0.44f,0.26f,1));
                        if (ImGui::Button("Join their game", ImVec2(-1, 0))) {
                            ImGui::SetClipboardText(pf.sessionCode.c_str());
                            Toast("Session code " + pf.sessionCode + " copied", 2);
                        }
                        ImGui::PopStyleColor();
                    }
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f,0.22f,0.22f,1));
                    if (ImGui::Button("Remove friend", ImVec2(-1, 0))) {
                        fr.Remove(pf.rowId); fNextRefresh = 0; ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopStyleColor();
                    ImGui::EndPopup();
                }

                if (fCache.empty()) {
                    ImGui::Dummy(ImVec2(0, 20));
                    ImGui::TextDisabled("No friends yet. Add someone by their username or friend code above,");
                    ImGui::TextDisabled("or share your own code so they can add you.");
                    ImGui::TextDisabled("(Run docs/sql/social.sql in your Supabase project if this stays empty.)");
                }
                // --- Recent activity feed ---
                if (!fFeed.empty()) {
                    ImGui::Dummy(ImVec2(0, 8));
                    ImGui::SeparatorText("Recent activity");
                    for (const auto& e : fFeed) {
                        const char* verb = e.kind == "playing"     ? " started playing "
                                         : e.kind == "achievement" ? " unlocked "
                                         : e.kind == "publish"     ? " published "
                                         : e.kind == "online"      ? " came online"
                                         : " ";
                        ImGui::Bullet();
                        ImGui::SameLine(0, 0);
                        ImGui::Text("%s%s%s", e.actor.c_str(), verb, e.detail.c_str());
                        std::string ago = RelTime(e.when);
                        if (!ago.empty()) { ImGui::SameLine(0, 6); ImGui::TextDisabled("- %s", ago.c_str()); }
                    }
                }
                ImGui::EndChild();
            }
        } else if (tab == 7) {                            // ---- Market / Workshop ----
            sectionHeader("Marketplace", nullptr);
            if (loginGate("the marketplace")) {
            okay::Workshop ws(account);
            static std::vector<okay::WorkshopItem> wCache;
            static std::map<long, std::pair<long, long>> wRatings;  // itemId -> (starSum, count)
            static Uint32 wNextRefresh = 0;
            static bool wShowPublish = false;
            // Reviews popup (one item at a time).
            static bool rvOpen = false;        // request to open the reviews popup
            static long rvItem = 0;
            static std::string rvTitle, rvErr;
            static std::vector<okay::WorkshopReview> rvList;
            static int  rvStars = 5;
            static char rvBody[400] = "";
            // Download-safety confirm (community downloads are untrusted).
            static bool getConfirmOpen = false;
            static long getId = 0, getDl = 0;
            static std::string getUrl, getTitle, getAuthor;
            // Edit-my-item popup.
            static bool edOpen = false;
            static long edId = 0;
            static int  edKind = 0;
            static char edTitle[128] = "", edUrl[256] = "", edDesc[512] = "";
            static std::string edErr;
            static char wTitle[128] = "", wUrl[256] = "";
            static char wDesc[512] = "";
            static int  wKind = 0;
            static std::string wErr;
            // A "Publish to Marketplace" action from the Play tab prefills + opens the form.
            if (!g_pendingPublishTitle.empty()) {
                std::snprintf(wTitle, sizeof(wTitle), "%s", g_pendingPublishTitle.c_str());
                wShowPublish = true;
                g_pendingPublishTitle.clear();
            }
            if (g_openPublishForm) { wShowPublish = true; g_openPublishForm = false; }
            Uint32 now = SDL_GetTicks();
            // Refresh the marketplace list/ratings in the background (no hitch).
            if (!g_wsMutex) g_wsMutex = SDL_CreateMutex();
            if (now >= wNextRefresh && !g_wsFetching) {
                g_wsFetching = true; wNextRefresh = now + 15000;
                if (SDL_Thread* t = SDL_CreateThread(MarketListWorker, "okay-wslist", nullptr))
                    SDL_DetachThread(t);
                else g_wsFetching = false;
            }
            if (g_wsMutex) {
                SDL_LockMutex(g_wsMutex);
                if (g_wsReady) { wCache = g_wsList; wRatings = g_wsRatings; g_wsReady = false; }
                SDL_UnlockMutex(g_wsMutex);
            }

            ImGui::TextDisabled("Browse and share games, scenes and templates from the community.");
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 196);
            if (ImGui::Button(wShowPublish ? "Browse" : "Publish an item", ImVec2(110, 0)))
                wShowPublish = !wShowPublish;
            ImGui::SameLine();
            if (ImGui::Button("Refresh", ImVec2(78, 0))) wNextRefresh = 0;
            ImGui::Spacing();

            if (wShowPublish) {
                if (!ws.Available()) {
                    ImGui::TextDisabled("Sign in to publish.");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Sign in")) { tab = 3; g_lastTab = 3; SavePrefs(); }
                } else {
                    ImGui::BeginChild("##publish", ImVec2(0, ImGui::GetContentRegionAvail().y - 46.0f), true);
                    ImGui::SeparatorText("Publish to the marketplace");
                    ImGui::SetNextItemWidth(360); ImGui::InputTextWithHint("Title", "My awesome game", wTitle, sizeof(wTitle));
                    ImGui::SetNextItemWidth(200); ImGui::Combo("Kind", &wKind, "Game\0Template\0");
                    ImGui::TextDisabled("Share a built game (exported with File > Build Game) or a template. Scenes can't be shared.");
                    ImGui::InputTextMultiline("Description", wDesc, sizeof(wDesc), ImVec2(-1, 90));
                    ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##url", "Download link (GitHub release, itch.io, or a public URL)...", wUrl, sizeof(wUrl));
                    ImGui::TextDisabled("Host your build anywhere public and paste the link. Players download it from here.");
                    ImGui::Dummy(ImVec2(0, 4));
                    if (ImGui::Button("Publish", ImVec2(120, 0))) {
                        const char* kinds[] = { "game", "template" };
                        if (ws.Publish(wTitle, wDesc, kinds[wKind], wUrl, &wErr)) {
                            okay::Friends(account).Post("publish", wTitle);   // show it in friends' feeds
                            Toast("Published to the marketplace", 1);
                            wTitle[0] = wDesc[0] = wUrl[0] = '\0'; wErr.clear();
                            wShowPublish = false; wNextRefresh = 0;
                        }
                    }
                    if (!wErr.empty()) { ImGui::SameLine(); ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f,0.45f,0.45f,1));
                                         ImGui::TextWrapped("%s", wErr.c_str()); ImGui::PopStyleColor(); }
                    ImGui::EndChild();
                }
            } else {
                // Search + sort + wishlist, with a category chip row underneath.
                static char wSearch[96] = "";
                static int  wFilterKind = 0;   // 0 All, 1 Games, 2 Scenes, 3 Templates
                static int  wSort = 0;         // 0 Newest, 1 Most downloaded, 2 Highest rated
                static bool wWishOnly = false;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 170 - 108);
                ImGui::InputTextWithHint("##mktsearch", "Search the marketplace...", wSearch, sizeof(wSearch));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(162);
                ImGui::Combo("##mktsort", &wSort, "Newest\0Most downloaded\0Highest rated\0");
                ImGui::SameLine();
                if (wWishOnly) ImGui::PushStyleColor(ImGuiCol_Button, kAccentDim);
                if (ImGui::Button(wWishOnly ? "Wishlist *" : "Wishlist", ImVec2(100, 0))) wWishOnly = !wWishOnly;
                if (wWishOnly) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show only wishlisted items");
                ImGui::Dummy(ImVec2(0, 4));

                auto mlo = [](std::string s){ for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
                std::string nd = mlo(wSearch);
                const char* kindKey[] = { "", "game", "scene", "template" };

                // Category chips: a modern, one-click filter row (counts per kind).
                {
                    const char* chipLbl[] = { "All", "Games", "Scenes", "Templates" };
                    int counts[4] = {0,0,0,0};
                    long totalDl = 0;
                    for (const auto& w : wCache) {
                        std::string wk = w.kind.empty() ? "game" : w.kind;
                        counts[0]++;
                        if (wk == "game") counts[1]++;
                        else if (wk == "scene") counts[2]++;
                        else if (wk == "template") counts[3]++;
                        totalDl += w.downloads;
                    }
                    for (int c = 0; c < 4; ++c) {
                        if (c) ImGui::SameLine(0, 6);
                        bool on = (wFilterKind == c);
                        if (on) { ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
                                  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1,1,1,1)); }
                        char cl[48]; std::snprintf(cl, sizeof(cl), "%s  %d", chipLbl[c], counts[c]);
                        if (ImGui::Button(cl)) wFilterKind = c;
                        if (on) ImGui::PopStyleColor(2);
                    }
                    ImGui::SameLine();
                    float aw = ImGui::GetContentRegionAvail().x;
                    char stat[64]; std::snprintf(stat, sizeof(stat), "%ld total download%s",
                                                 totalDl, totalDl == 1 ? "" : "s");
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + aw - ImGui::CalcTextSize(stat).x - 4);
                    ImGui::TextDisabled("%s", stat);
                    ImGui::Dummy(ImVec2(0, 4));
                }
                // Build the display order from the chosen sort (wCache is newest-first).
                auto avgOf = [&](long id)->float {
                    auto it = wRatings.find(id);
                    return (it != wRatings.end() && it->second.second > 0)
                         ? (float)it->second.first / (float)it->second.second : 0.0f;
                };
                std::vector<const okay::WorkshopItem*> order;
                order.reserve(wCache.size());
                for (const auto& w : wCache) order.push_back(&w);
                if (wSort == 1)
                    std::stable_sort(order.begin(), order.end(),
                        [](const okay::WorkshopItem* a, const okay::WorkshopItem* b){ return a->downloads > b->downloads; });
                else if (wSort == 2)
                    std::stable_sort(order.begin(), order.end(),
                        [&](const okay::WorkshopItem* a, const okay::WorkshopItem* b){ return avgOf(a->id) > avgOf(b->id); });

                // Featured strip: top-rated items, shown only on the unfiltered
                // default view so it doesn't fight an active search/sort.
                if (nd.empty() && wFilterKind == 0 && !wWishOnly && wSort == 0) {
                    std::vector<const okay::WorkshopItem*> top;
                    for (const auto& w : wCache) if (avgOf(w.id) > 0.0f) top.push_back(&w);
                    std::stable_sort(top.begin(), top.end(),
                        [&](const okay::WorkshopItem* a, const okay::WorkshopItem* b){ return avgOf(a->id) > avgOf(b->id); });
                    if (top.size() > 3) top.resize(3);
                    if (!top.empty()) {
                        ImGui::TextDisabled("FEATURED  -  TOP RATED");
                        ImGui::Dummy(ImVec2(0, 2));
                        float cw = (ImGui::GetContentRegionAvail().x - 2 * 10) / 3.0f;
                        for (std::size_t i = 0; i < top.size(); ++i) {
                            const okay::WorkshopItem& w = *top[i];
                            if (i) ImGui::SameLine(0, 10);
                            ImGui::PushID((int)(90000 + w.id));
                            ImGui::BeginChild("##feat", ImVec2(cw, 86), true);
                            HoverRing();
                            ImGui::SetCursorPos(ImVec2(10, 12)); IconTile(w.title, 40.0f);
                            ImGui::SameLine(); ImGui::SetCursorPosX(60);
                            ImGui::BeginGroup();
                            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4);
                            ImGui::TextUnformatted(w.title.c_str());
                            ImVec2 fp = ImGui::GetCursorScreenPos(); fp.y += 2;
                            DrawStars(ImGui::GetWindowDrawList(), fp, 10.0f, avgOf(w.id),
                                      IM_COL32(240,196,92,255), IM_COL32(255,255,255,46));
                            ImGui::Dummy(ImVec2(66, 13)); ImGui::SameLine();
                            ImGui::TextDisabled("%.1f", avgOf(w.id));
                            ImGui::TextDisabled("by %s", w.author.empty() ? "unknown" : w.author.c_str());
                            ImGui::EndGroup();
                            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                                rvItem = w.id; rvTitle = w.title; rvOpen = true;
                                rvStars = 5; rvBody[0] = '\0'; rvErr.clear();
                                rvList = ws.Reviews(w.id);
                                for (const auto& rev : rvList) if (rev.mine) {
                                    rvStars = rev.rating; std::snprintf(rvBody, sizeof(rvBody), "%s", rev.body.c_str()); }
                            }
                            ImGui::EndChild();
                            ImGui::PopID();
                        }
                        ImGui::Dummy(ImVec2(0, 6));
                    }
                }
                ImGui::BeginChild("##market", ImVec2(0, ImGui::GetContentRegionAvail().y - 46.0f), false);
                std::string me = account.CurrentSession().username;
                int mktShown = 0;
                for (const okay::WorkshopItem* wp : order) {
                    const okay::WorkshopItem& w = *wp;
                    std::string wk = w.kind.empty() ? "game" : w.kind;
                    if (wFilterKind != 0 && wk != kindKey[wFilterKind]) continue;
                    if (wWishOnly && !IsWishlisted(w.id)) continue;
                    if (!nd.empty() &&
                        mlo(w.title + " " + w.description + " " + w.author).find(nd) == std::string::npos) continue;
                    ++mktShown;
                    ImGui::PushID((int)w.id);
                    ImGui::BeginChild("##witem", ImVec2(0, 118), true);
                    HoverRing();
                    // Colored icon tile from the title.
                    ImGui::SetCursorPos(ImVec2(12, 14));
                    IconTile(w.title, 56.0f);
                    ImGui::SameLine(); ImGui::SetCursorPosX(84);
                    ImGui::BeginGroup();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
                    ImGui::TextUnformatted(w.title.c_str());
                    // Kind badge pill (colored per kind) + author + downloads.
                    ImVec4 kc = wk == "scene" ? ImVec4(0.30f,0.62f,0.40f,1)
                              : wk == "template" ? ImVec4(0.52f,0.42f,0.72f,1)
                                                 : ImVec4(0.24f,0.50f,0.72f,1);
                    ImVec2 bp = ImGui::GetCursorScreenPos();
                    ImVec2 ks = ImGui::CalcTextSize(wk.c_str());
                    ImGui::GetWindowDrawList()->AddRectFilled(bp, ImVec2(bp.x+ks.x+14, bp.y+ks.y+4),
                        ImGui::GetColorU32(ImVec4(kc.x,kc.y,kc.z,0.30f)), (ks.y+4)*0.5f);
                    ImGui::GetWindowDrawList()->AddText(ImVec2(bp.x+7, bp.y+2), ImGui::GetColorU32(kc), wk.c_str());
                    ImGui::Dummy(ImVec2(ks.x+14, ks.y+4));
                    ImGui::SameLine(0, 8);
                    ImGui::TextDisabled("by %s  -  %ld download%s",
                                        w.author.empty() ? "unknown" : w.author.c_str(),
                                        w.downloads, w.downloads == 1 ? "" : "s");
                    // Rating row: star meter + average + count (or "Not yet rated").
                    {
                        auto it = wRatings.find(w.id);
                        long sum = it != wRatings.end() ? it->second.first : 0;
                        long cnt = it != wRatings.end() ? it->second.second : 0;
                        float avg = cnt > 0 ? (float)sum / (float)cnt : 0.0f;
                        ImVec2 sp = ImGui::GetCursorScreenPos(); sp.y += 2;
                        float w5 = DrawStars(ImGui::GetWindowDrawList(), sp, 11.0f, avg,
                                             IM_COL32(240, 196, 92, 255), IM_COL32(255, 255, 255, 46));
                        ImGui::Dummy(ImVec2(w5, 15)); ImGui::SameLine(0, 8);
                        if (cnt > 0) ImGui::TextDisabled("%.1f  (%ld)", avg, cnt);
                        else         ImGui::TextDisabled("Not yet rated");
                    }
                    if (!w.description.empty()) {
                        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - 100);
                        ImGui::TextDisabled("%s", w.description.c_str());
                        ImGui::PopTextWrapPos();
                    }
                    ImGui::EndGroup();
                    // Right-aligned actions.
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 96);
                    ImGui::SetCursorPosY(14.0f);
                    ImGui::BeginGroup();
                    if (!w.url.empty()) {
                        ImGui::PushStyleColor(ImGuiCol_Button, kAccentDim);
                        if (ImGui::Button("Get", ImVec2(90, 30))) {
                            getId = w.id; getUrl = w.url; getTitle = w.title;
                            getAuthor = w.author; getDl = w.downloads; getConfirmOpen = true;
                        }
                        ImGui::PopStyleColor();
                    } else ImGui::TextDisabled("no link");
                    bool wished = IsWishlisted(w.id);
                    if (wished) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f,0.80f,0.35f,1));
                    if (ImGui::SmallButton(wished ? "* Wishlisted" : "+ Wishlist")) { ToggleWishlist(w.id); SavePrefs(); }
                    if (wished) ImGui::PopStyleColor();
                    if (ImGui::SmallButton("Reviews")) {
                        rvItem = w.id; rvTitle = w.title; rvOpen = true;
                        rvStars = 5; rvBody[0] = '\0'; rvErr.clear();
                        rvList = ws.Reviews(w.id);
                        for (const auto& rev : rvList) if (rev.mine) {   // prefill my existing review
                            rvStars = rev.rating;
                            std::snprintf(rvBody, sizeof(rvBody), "%s", rev.body.c_str());
                        }
                    }
                    if (!me.empty() && me == w.author) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Edit")) {
                            edId = w.id; edKind = (wk == "template") ? 1 : 0;
                            std::snprintf(edTitle, sizeof(edTitle), "%s", w.title.c_str());
                            std::snprintf(edDesc,  sizeof(edDesc),  "%s", w.description.c_str());
                            std::snprintf(edUrl,   sizeof(edUrl),   "%s", w.url.c_str());
                            edErr.clear(); edOpen = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Remove")) { ws.Remove(w.id); wNextRefresh = 0; }
                    }
                    ImGui::EndGroup();
                    ImGui::EndChild();
                    ImGui::PopID();
                }
                if (wCache.empty()) {
                    ImGui::Dummy(ImVec2(0, 20));
                    ImGui::TextDisabled("Nothing published yet. Be the first — hit \"Publish an item\".");
                    ImGui::TextDisabled("(Run docs/sql/social.sql in your Supabase project if this stays empty.)");
                } else if (mktShown == 0) {
                    ImGui::Dummy(ImVec2(0, 20));
                    ImGui::TextDisabled("No items match your search or filter.");
                }
                ImGui::EndChild();
            }

            // --- Edit my item ---
            if (edOpen) { ImGui::OpenPopup("Edit item"); edOpen = false; }
            ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("Edit item", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##edtitle", "Title", edTitle, sizeof(edTitle));
                ImGui::SetNextItemWidth(200);
                ImGui::Combo("Kind##ed", &edKind, "Game\0Template\0");
                ImGui::InputTextMultiline("##eddesc", edDesc, sizeof(edDesc), ImVec2(-1, 80));
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##edurl", "Download link", edUrl, sizeof(edUrl));
                ImGui::Dummy(ImVec2(0, 4));
                if (PrimaryButton("Save changes", ImVec2(150, 32))) {
                    const char* kinds[] = { "game", "template" };
                    if (okay::Workshop(account).Update(edId, edTitle, edDesc, kinds[edKind], edUrl, &edErr)) {
                        Toast("Item updated", 1); wNextRefresh = 0; ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(100, 32))) ImGui::CloseCurrentPopup();
                if (!edErr.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f,0.45f,0.45f,1));
                                      ImGui::TextWrapped("%s", edErr.c_str()); ImGui::PopStyleColor(); }
                ImGui::EndPopup();
            }

            // --- Download-safety confirm ---
            if (getConfirmOpen) { ImGui::OpenPopup("Download"); getConfirmOpen = false; }
            ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("Download", nullptr,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("You're about to open a download for \"%s\" by %s.",
                                   getTitle.c_str(), getAuthor.empty() ? "unknown" : getAuthor.c_str());
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.80f, 0.35f, 1));
                ImGui::TextWrapped("Community uploads aren't vetted by OkaySpace. Only download and run "
                                   "games from people you trust.");
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 2));
                ImGui::TextDisabled("%s", getUrl.c_str());
                ImGui::Dummy(ImVec2(0, 6));
                if (PrimaryButton("Download", ImVec2(150, 34))) {
                    StartDownload(getUrl, getTitle);
                    okay::Workshop(account).NoteDownload(getId, getDl);
                    wNextRefresh = 0;
                    Toast("Added to Downloads", 1);
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Download to the launcher's Downloads tab (best for direct file links)");
                ImGui::SameLine();
                if (ImGui::Button("Open in browser", ImVec2(150, 34))) {
                    OpenExternal(getUrl);
                    okay::Workshop(account).NoteDownload(getId, getDl);
                    wNextRefresh = 0;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(90, 34))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            // --- Reviews popup (opened from a card's "Reviews" button) ---
            if (rvOpen) { ImGui::OpenPopup("Reviews"); rvOpen = false; }
            ImGui::SetNextWindowSize(ImVec2(520, 520), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal("Reviews", nullptr, ImGuiWindowFlags_NoCollapse)) {
                ImGui::TextUnformatted(rvTitle.c_str());
                // Aggregate for this item.
                long sum = 0, cnt = 0;
                auto rit = wRatings.find(rvItem);
                if (rit != wRatings.end()) { sum = rit->second.first; cnt = rit->second.second; }
                float avg = cnt > 0 ? (float)sum / (float)cnt : 0.0f;
                ImVec2 ap = ImGui::GetCursorScreenPos(); ap.y += 2;
                float aw = DrawStars(ImGui::GetWindowDrawList(), ap, 13.0f, avg,
                                     IM_COL32(240,196,92,255), IM_COL32(255,255,255,46));
                ImGui::Dummy(ImVec2(aw, 17)); ImGui::SameLine(0, 8);
                if (cnt > 0) ImGui::TextDisabled("%.1f out of 5  -  %ld review%s", avg, cnt, cnt==1?"":"s");
                else         ImGui::TextDisabled("No reviews yet");
                ImGui::Separator();

                // My review editor.
                ImGui::TextUnformatted("Your rating:");
                ImVec2 sp = ImGui::GetCursorScreenPos();
                float sw = DrawStars(ImGui::GetWindowDrawList(), sp, 22.0f, (float)rvStars,
                                     IM_COL32(240,196,92,255), IM_COL32(255,255,255,55));
                float stepw = sw / 5.0f;
                for (int s = 0; s < 5; ++s) {
                    ImGui::SetCursorScreenPos(ImVec2(sp.x + s * stepw, sp.y));
                    if (ImGui::InvisibleButton(("##st" + std::to_string(s)).c_str(), ImVec2(stepw, 28)))
                        rvStars = s + 1;
                }
                ImGui::SetCursorScreenPos(ImVec2(sp.x + sw + 10, sp.y + 4));
                ImGui::Text("%d / 5", rvStars);
                ImGui::Dummy(ImVec2(0, 30));
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextMultiline("##rvbody", rvBody, sizeof(rvBody), ImVec2(-1, 70));
                if (ImGui::Button("Submit review", ImVec2(140, 0))) {
                    if (ws.SubmitReview(rvItem, rvStars, rvBody, &rvErr)) {
                        Toast("Review posted", 1);
                        rvList = ws.Reviews(rvItem);
                        wRatings = ws.Ratings(); wNextRefresh = SDL_GetTicks() + 15000;
                        rvErr.clear();
                    }
                }
                bool haveMine = false; for (const auto& rev : rvList) if (rev.mine) haveMine = true;
                if (haveMine) {
                    ImGui::SameLine();
                    if (ImGui::Button("Delete mine", ImVec2(120, 0))) {
                        ws.RemoveMyReview(rvItem);
                        rvStars = 5; rvBody[0] = '\0';
                        rvList = ws.Reviews(rvItem);
                        wRatings = ws.Ratings(); wNextRefresh = SDL_GetTicks() + 15000;
                    }
                }
                if (!rvErr.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f,0.45f,0.45f,1));
                                      ImGui::TextWrapped("%s", rvErr.c_str()); ImGui::PopStyleColor(); }
                ImGui::Separator();

                // All reviews for this item.
                ImGui::BeginChild("##rvlist", ImVec2(0, ImGui::GetContentRegionAvail().y - 44), false);
                if (rvList.empty()) ImGui::TextDisabled("Be the first to review this.");
                for (const auto& rev : rvList) {
                    ImGui::BeginChild(("##rv" + rev.reviewer + std::to_string(rev.when)).c_str(),
                                      ImVec2(0, rev.body.empty() ? 40 : 64), true);
                    ImVec2 rp = ImGui::GetCursorScreenPos(); rp.y += 2;
                    DrawStars(ImGui::GetWindowDrawList(), rp, 10.0f, (float)rev.rating,
                              IM_COL32(240,196,92,255), IM_COL32(255,255,255,46));
                    ImGui::Dummy(ImVec2(68, 13)); ImGui::SameLine();
                    ImGui::Text("%s%s", rev.reviewer.empty() ? "someone" : rev.reviewer.c_str(),
                                rev.mine ? "  (you)" : "");
                    if (!rev.body.empty()) { ImGui::PushTextWrapPos(0);
                        ImGui::TextDisabled("%s", rev.body.c_str()); ImGui::PopTextWrapPos(); }
                    ImGui::EndChild();
                }
                ImGui::EndChild();
                if (ImGui::Button("Close", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            }   // end marketplace login gate
        } else if (tab == 8) {                            // ---- Servers (Discord-style) ----
            sectionHeader("Servers", nullptr);
            okay::Servers sv(account);
            if (!sv.Available()) {
                ImGui::Dummy(ImVec2(0, 20));
                ImGui::TextDisabled("Sign in to create or join servers.");
                ImGui::Dummy(ImVec2(0, 6));
                if (PrimaryButton("Sign in", ImVec2(160, 38))) { tab = 3; g_lastTab = 3; SavePrefs(); }
            } else {
                static std::vector<okay::Server> svList;
                static std::vector<okay::Channel> svChannels;
                static std::vector<okay::ChannelMessage> svMsgs;
                static std::vector<okay::ServerMember> svMembers;
                static long svSel = 0, svChan = 0;
                static std::string svSelName, svErr;
                static char svNew[64] = "", svJoin[24] = "", svMsg[800] = "", svNewChan[40] = "";
                static Uint32 svNextList = 0, svNextChan = 0, svNextMsg = 0, svNextMem = 0;
                // Owner-management state.
                static long svRenChanId = 0;             // channel being renamed (0 = none)
                static char svRenChanBuf[64] = "", svRenSrvBuf[64] = "";
                static bool svSettingsOpen = false, svRenChanOpen = false;
                Uint32 now = SDL_GetTicks();
                // Prefetch the server list in the background so switching to this
                // tab never blocks on the network.
                if (!g_svMutex) g_svMutex = SDL_CreateMutex();
                if (now >= svNextList && !g_svFetching) {
                    g_svFetching = true; svNextList = now + 20000;
                    if (SDL_Thread* t = SDL_CreateThread(ServerListWorker, "okay-svlist", nullptr))
                        SDL_DetachThread(t);
                    else g_svFetching = false;
                }
                if (g_svMutex) {
                    SDL_LockMutex(g_svMutex);
                    if (g_svReady) { svList = g_svList; g_svReady = false; }
                    SDL_UnlockMutex(g_svMutex);
                }

                float colH = ImGui::GetContentRegionAvail().y - 6.0f;
                // Resizable 3-pane Discord layout: drag the column borders to resize.
                if (ImGui::BeginTable("##svtable", 3,
                        ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoHostExtendX,
                        ImVec2(0, colH))) {
                    ImGui::TableSetupColumn("servers",  ImGuiTableColumnFlags_WidthFixed, 180.0f);
                    ImGui::TableSetupColumn("channels", ImGuiTableColumnFlags_WidthFixed, 190.0f);
                    ImGui::TableSetupColumn("chat",     ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableNextRow();

                    // --- Pane 1: server list + create/join ---
                    ImGui::TableSetColumnIndex(0);
                    ImGui::BeginChild("##svlist", ImVec2(0, colH - 4), false);
                    ImGui::TextDisabled("SERVERS");
                    ImGui::Dummy(ImVec2(0, 2));
                    // Discord-style server rail: a circular, name-tinted icon with the
                    // server's initial, a left selection pill, and a hover wash.
                    for (const auto& s : svList) {
                        bool sel = (s.id == svSel);
                        ImGui::PushID((int)s.id);
                        ImVec2 rp = ImGui::GetCursorScreenPos();
                        float av = ImGui::GetContentRegionAvail().x, rh = 46.0f;
                        if (ImGui::InvisibleButton("##srv", ImVec2(av, rh))) {
                            svSel = s.id; svSelName = s.name; svChan = 0; svNextChan = 0; svMsgs.clear();
                        }
                        bool hov = ImGui::IsItemHovered();
                        ImDrawList* d = ImGui::GetWindowDrawList();
                        if (sel)      d->AddRectFilled(rp, ImVec2(rp.x + av, rp.y + rh), IM_COL32(255, 255, 255, 18), 5.0f);
                        else if (hov) d->AddRectFilled(rp, ImVec2(rp.x + av, rp.y + rh), IM_COL32(255, 255, 255, 9), 5.0f);
                        if (sel)      d->AddRectFilled(ImVec2(rp.x, rp.y + rh * 0.5f - 11), ImVec2(rp.x + 3, rp.y + rh * 0.5f + 11),
                                                       ImGui::GetColorU32(kAccent), 2.0f);
                        float cr = 16.0f; ImVec2 ctr(rp.x + 12 + cr, rp.y + rh * 0.5f);
                        ImVec4 sc = NameColor(s.name);
                        d->AddCircleFilled(ctr, cr, ImGui::GetColorU32(ImVec4(sc.x * 0.55f, sc.y * 0.55f, sc.z * 0.55f, 1)), 26);
                        if (sel || hov) d->AddCircle(ctr, cr, ImGui::GetColorU32(sc), 26, 2.0f);
                        char ini[2] = { (char)(s.name.empty() ? '#' : std::toupper((unsigned char)s.name[0])), 0 };
                        ImVec2 ts = ImGui::CalcTextSize(ini);
                        d->AddText(ImVec2(ctr.x - ts.x * 0.5f, ctr.y - ts.y * 0.5f), IM_COL32(245, 246, 250, 255), ini);
                        d->AddText(ImVec2(ctr.x + cr + 10, rp.y + rh * 0.5f - ImGui::GetTextLineHeight() * 0.5f),
                                   ImGui::GetColorU32(sel ? ImVec4(0.96f, 0.97f, 0.99f, 1) : ImVec4(0.74f, 0.76f, 0.82f, 1)),
                                   s.name.c_str());
                        ImGui::PopID();
                    }
                    ImGui::Dummy(ImVec2(0, 4));
                    ImGui::Separator();
                    ImGui::TextDisabled("Create");
                    ImGui::SetNextItemWidth(-1);
                    bool mk = ImGui::InputTextWithHint("##svnew", "New server name", svNew, sizeof(svNew), ImGuiInputTextFlags_EnterReturnsTrue);
                    if ((ImGui::Button("Create server", ImVec2(-1, 0)) || mk) && svNew[0]) {
                        okay::Server made;
                        if (sv.CreateServer(svNew, &made, &svErr)) {
                            svNew[0] = '\0'; svErr.clear(); svNextList = 0;
                            svSel = made.id; svSelName = made.name; svChan = 0; svNextChan = 0;
                            Toast("Server created", 1);
                        }
                    }
                    ImGui::Dummy(ImVec2(0, 4));
                    ImGui::TextDisabled("Join by code");
                    ImGui::SetNextItemWidth(-1);
                    bool jn = ImGui::InputTextWithHint("##svjoin", "invite code", svJoin, sizeof(svJoin), ImGuiInputTextFlags_EnterReturnsTrue);
                    if ((ImGui::Button("Join server", ImVec2(-1, 0)) || jn) && svJoin[0]) {
                        if (sv.JoinServer(svJoin, &svErr)) { svJoin[0] = '\0'; svErr.clear(); svNextList = 0; Toast("Joined server", 1); }
                    }
                    if (!svErr.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f,0.45f,0.45f,1)); ImGui::TextWrapped("%s", svErr.c_str()); ImGui::PopStyleColor(); }
                    ImGui::EndChild();

                    // --- Pane 2: channels + members ---
                    ImGui::TableSetColumnIndex(1);
                    ImGui::BeginChild("##svchan", ImVec2(0, colH - 4), false);
                    if (svSel == 0) {
                        ImGui::TextDisabled("Pick a server");
                    } else {
                        // Feed the background Servers poller; read what it published.
                        // No channels/members/messages fetch on the UI thread.
                        g_srvActiveMs = now; g_srvSel = svSel; g_srvChan = svChan;
                        (void)svNextChan; (void)svNextMem;
                        if (g_srvMutex) {
                            SDL_LockMutex(g_srvMutex);
                            if (g_srvChanReady) { svChannels = g_srvChannels; g_srvChanReady = false; }
                            if (g_srvMemReady)  { svMembers  = g_srvMembers;  g_srvMemReady  = false; }
                            SDL_UnlockMutex(g_srvMutex);
                        }
                        if (svChan == 0 && !svChannels.empty()) svChan = svChannels[0].id;
                        // Owner check: MyUid() is the session's id, so fetch it once.
                        static std::string svMyUidCache; static bool svMyUidGot = false;
                        if (!svMyUidGot) { svMyUidCache = sv.MyUid(); svMyUidGot = true; }
                        std::string svMyUid = svMyUidCache;
                        bool svIAmOwner = false;
                        for (const auto& s : svList) if (s.id == svSel && !s.owner.empty() && s.owner == svMyUid) svIAmOwner = true;
                        ImGui::TextDisabled("%s", svSelName.c_str());
                        if (svIAmOwner) { ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24);
                            if (ImGui::SmallButton("...")) { std::snprintf(svRenSrvBuf,sizeof(svRenSrvBuf),"%s",svSelName.c_str()); svSettingsOpen = true; } }
                        ImGui::Separator();
                        ImGui::Dummy(ImVec2(0, 2));
                        ImGui::TextDisabled("CHANNELS - %d", (int)svChannels.size());
                        ImGui::Dummy(ImVec2(0, 2));
                        for (const auto& c : svChannels) {
                            bool sel = (c.id == svChan);
                            if (ImGui::Selectable(("# " + c.name + "##" + std::to_string(c.id)).c_str(), sel)) { svChan = c.id; svNextMsg = 0; }
                            // Owner: right-click a channel to rename or delete it.
                            if (svIAmOwner && ImGui::BeginPopupContextItem(("chanctx" + std::to_string(c.id)).c_str())) {
                                if (ImGui::MenuItem("Rename channel")) {
                                    svRenChanId = c.id; std::snprintf(svRenChanBuf, sizeof(svRenChanBuf), "%s", c.name.c_str()); svRenChanOpen = true;
                                }
                                if (svChannels.size() > 1 && ImGui::MenuItem("Delete channel")) {
                                    if (sv.DeleteChannel(c.id)) { if (svChan == c.id) { svChan = 0; } g_srvForce = true; Toast("Channel deleted", 1); }
                                }
                                ImGui::EndPopup();
                            }
                        }
                        if (svIAmOwner) ImGui::TextDisabled("(right-click a channel to rename/delete)");
                        ImGui::Separator();
                        ImGui::SetNextItemWidth(-1);
                        bool nc = ImGui::InputTextWithHint("##svnewchan", "new channel", svNewChan, sizeof(svNewChan), ImGuiInputTextFlags_EnterReturnsTrue);
                        if ((ImGui::SmallButton("Add channel") || nc) && svNewChan[0]) {
                            if (sv.CreateChannel(svSel, svNewChan)) { svNewChan[0] = '\0'; g_srvForce = true; }
                        }
                        if (!svIAmOwner) {
                            if (ImGui::SmallButton("Leave server")) {
                                if (sv.Leave(svSel)) { svSel = 0; svChan = 0; svNextList = 0; Toast("Left server", 1); }
                            }
                        }
                        // (members come from the background poller, read above)
                        // Collapsible, Online/Offline-grouped member list (Discord-style).
                        ImGui::Dummy(ImVec2(0, 6));
                        static bool svMembersOpen = true;
                        int onlineN = 0; for (const auto& m : svMembers) if (m.online) ++onlineN;
                        if (ImGui::Selectable(svMembersOpen ? "- MEMBERS" : "+ MEMBERS", false,
                                              ImGuiSelectableFlags_None, ImVec2(ImGui::GetContentRegionAvail().x - 70, 0)))
                            svMembersOpen = !svMembersOpen;
                        ImGui::SameLine(); ImGui::TextDisabled("%d/%d", onlineN, (int)svMembers.size());
                        ImGui::Separator();
                        if (svMembersOpen) {
                            auto drawMember = [&](const okay::ServerMember& m) {
                                ImVec2 mp = ImGui::GetCursorScreenPos();
                                ImU32 dot = m.online ? IM_COL32(120, 200, 110, 255) : IM_COL32(110, 118, 130, 255);
                                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(mp.x + 5, mp.y + 8), 4.0f, dot, 12);
                                ImGui::Dummy(ImVec2(14, 0)); ImGui::SameLine();
                                if (m.role == "owner") {
                                    ImGui::TextColored(m.online ? ImVec4(0.9f,0.92f,0.96f,1) : ImVec4(0.6f,0.63f,0.7f,1),
                                                       "%s", m.name.empty() ? "member" : m.name.c_str());
                                    ImGui::SameLine(0, 4); ImGui::TextColored(ImVec4(0.95f,0.80f,0.35f,1), "(owner)");
                                } else {
                                    ImGui::TextColored(m.online ? ImVec4(0.9f,0.92f,0.96f,1) : ImVec4(0.6f,0.63f,0.7f,1),
                                                       "%s", m.name.empty() ? "member" : m.name.c_str());
                                }
                            };
                            if (onlineN > 0) {
                                ImGui::TextDisabled("ONLINE - %d", onlineN);
                                for (const auto& m : svMembers) if (m.online) drawMember(m);
                            }
                            int offN = (int)svMembers.size() - onlineN;
                            if (offN > 0) {
                                ImGui::Dummy(ImVec2(0, 3));
                                ImGui::TextDisabled("OFFLINE - %d", offN);
                                for (const auto& m : svMembers) if (!m.online) drawMember(m);
                            }
                            if (svMembers.empty()) ImGui::TextDisabled("No members loaded.");
                        }
                    }
                    ImGui::EndChild();

                    // --- Pane 3: Discord-style messages ---
                    ImGui::TableSetColumnIndex(2);
                    ImGui::BeginChild("##svmsgs", ImVec2(0, colH - 4), false);
                    if (svChan == 0) {
                        ImGui::TextDisabled("Pick a channel");
                    } else {
                        static std::map<long,long> svLikes; static std::map<long,bool> svMyLikes; static long svEditId = 0;
                        // Messages + likes come from the background Servers poller.
                        (void)svNextMsg;
                        if (g_srvMutex) {
                            SDL_LockMutex(g_srvMutex);
                            if (g_srvMsgReady) { svMsgs = g_srvMsgs; svLikes = g_srvLikes; svMyLikes = g_srvMyLikes; g_srvMsgReady = false; }
                            SDL_UnlockMutex(g_srvMutex);
                        }
                        std::string svMe = account.CurrentSession().username;
                        // Inline per-message actions: ❤ like + count, "(edited)", and
                        // an Edit affordance on my own messages (loads it into the box).
                        auto msgActions = [&](const okay::ChannelMessage& m) {
                            long lc = svLikes.count(m.id) ? svLikes[m.id] : 0;
                            bool liked = svMyLikes.count(m.id) != 0;
                            ImGui::PushID((int)m.id);
                            ImVec2 hp2 = ImGui::GetCursorScreenPos();
                            if (ImGui::InvisibleButton("like", ImVec2(18, 16))) { sv.Like(m.id, !liked); g_srvForce = true; }
                            DrawHeart(ImGui::GetWindowDrawList(), ImVec2(hp2.x+8, hp2.y+9), 6.0f,
                                      liked ? IM_COL32(232,72,92,255) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
                            if (lc > 0) { ImGui::SameLine(0, 2); ImGui::TextDisabled("%ld", lc); }
                            if (m.edited) { ImGui::SameLine(0, 8); ImGui::TextDisabled("(edited)"); }
                            if (!svMe.empty() && m.author == svMe) {
                                ImGui::SameLine(0, 8);
                                if (ImGui::SmallButton("edit")) { svEditId = m.id; std::snprintf(svMsg, sizeof(svMsg), "%s", m.body.c_str()); }
                            }
                            ImGui::PopID();
                        };
                        // Channel header bar (# name), Discord-style.
                        std::string chanName;
                        for (const auto& c : svChannels) if (c.id == svChan) { chanName = c.name; break; }
                        ImGui::Text("# %s", chanName.empty() ? "channel" : chanName.c_str());
                        ImGui::Separator();
                        ImGui::BeginChild("##svscroll", ImVec2(0, ImGui::GetContentRegionAvail().y - 40.0f), false);
                        static int svLastShown = -1;
                        std::string prevAuthor; bool firstMsg = true; long prevDay = 0, prevWhen = 0;
                        for (const auto& m : svMsgs) {
                            std::string who = m.author.empty() ? "unknown" : m.author;
                            // Date separator when the day changes.
                            long day = DayKey(m.when);
                            if (day != 0 && day != prevDay) { ChatDateSeparator(m.when); prevDay = day; prevAuthor.clear(); }
                            char w[24] = ""; if (m.when > 0) { std::time_t tt=(std::time_t)m.when; std::tm* tm=std::localtime(&tt); if(tm) std::strftime(w,sizeof(w),"%H:%M",tm); }
                            // Start a fresh group on a new author or a >7 min gap.
                            bool newGroup = (who != prevAuthor) || (m.when > 0 && prevWhen > 0 && m.when - prevWhen > 420);
                            prevWhen = m.when;
                            if (newGroup) {
                                if (!firstMsg) ImGui::Dummy(ImVec2(0, 6));
                                DrawAvatar("", who, 34.0f);            // colored initial avatar
                                ImGui::SameLine(); ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6);
                                ImGui::BeginGroup();
                                ImVec4 nc2 = NameColor(who);
                                ImGui::TextColored(ImVec4(nc2.x,nc2.y,nc2.z,1), "%s", who.c_str());
                                ImGui::SameLine(0, 8); ImGui::TextDisabled("%s", w);
                                RenderChatBody(m.body);
                                msgActions(m);
                                ImGui::EndGroup();
                                prevAuthor = who;
                            } else {
                                // Continuation of the same author's group: indent under the
                                // avatar, with a faint timestamp revealed on hover (Discord-like).
                                ImGui::Indent(40.0f);
                                ImVec2 rowTop = ImGui::GetCursorScreenPos();
                                RenderChatBody(m.body);
                                if (w[0] && ImGui::IsItemHovered()) {
                                    ImVec2 tp(rowTop.x - 38.0f, rowTop.y + 1.0f);
                                    ImGui::GetWindowDrawList()->AddText(tp,
                                        ImGui::GetColorU32(ImGuiCol_TextDisabled), w);
                                }
                                msgActions(m);
                                ImGui::Unindent(40.0f);
                            }
                            firstMsg = false;
                        }
                        if (svMsgs.empty()) {
                            ImGui::Dummy(ImVec2(0, 10));
                            ImGui::TextDisabled("This is the start of #%s.", chanName.empty()?"channel":chanName.c_str());
                            ImGui::TextDisabled("Say something to kick things off!");
                        }
                        if ((int)svMsgs.size() != svLastShown) { ImGui::SetScrollHereY(1.0f); svLastShown = (int)svMsgs.size(); }
                        ImGui::EndChild();
                        if (svEditId) { ImGui::TextDisabled("Editing a message"); ImGui::SameLine();
                            if (ImGui::SmallButton("cancel edit")) { svEditId = 0; svMsg[0] = '\0'; } }
                        // Emoji picker + multiline composer: Enter sends, Ctrl+Enter = newline.
                        ImVec2 inSz(ImGui::GetContentRegionAvail().x - 70 - 36, ImGui::GetTextLineHeight() + 14);
                        EmojiPicker("svemoji", svMsg, sizeof(svMsg)); ImGui::SameLine();
                        bool send = ImGui::InputTextMultiline("##svsend", svMsg, sizeof(svMsg), inSz,
                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
                        ImGui::SameLine();
                        if ((ImGui::Button(svEditId ? "Save" : "Send", ImVec2(62, inSz.y)) || send) && svMsg[0]) {
                            if (svEditId) { if (sv.EditMessage(svEditId, svMsg)) { svEditId = 0; svMsg[0] = '\0'; g_srvForce = true; } }
                            else if (sv.Post(svChan, svMsg)) { svMsg[0] = '\0'; g_srvForce = true; }
                            ImGui::SetKeyboardFocusHere(-1);   // keep focus in the box after sending
                        }
                    }
                    ImGui::EndChild();
                    ImGui::EndTable();
                }

                // --- Rename channel popup ---
                if (svRenChanOpen) { ImGui::OpenPopup("Rename channel"); svRenChanOpen = false; }
                if (ImGui::BeginPopupModal("Rename channel", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::SetNextItemWidth(260);
                    bool go = ImGui::InputTextWithHint("##renchan", "channel name", svRenChanBuf, sizeof(svRenChanBuf), ImGuiInputTextFlags_EnterReturnsTrue);
                    if ((PrimaryButton("Rename", ImVec2(120, 30)) || go) && svRenChanBuf[0]) {
                        if (sv.RenameChannel(svRenChanId, svRenChanBuf)) { g_srvForce = true; Toast("Channel renamed", 1); }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(100, 30))) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }

                // --- Server settings popup (owner) ---
                if (svSettingsOpen) { ImGui::OpenPopup("Server settings"); svSettingsOpen = false; }
                ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Appearing);
                if (ImGui::BeginPopupModal("Server settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextDisabled("Rename server");
                    ImGui::SetNextItemWidth(-1);
                    bool go = ImGui::InputTextWithHint("##rensrv", "server name", svRenSrvBuf, sizeof(svRenSrvBuf), ImGuiInputTextFlags_EnterReturnsTrue);
                    if ((ImGui::Button("Save name", ImVec2(130, 0)) || go) && svRenSrvBuf[0]) {
                        if (sv.RenameServer(svSel, svRenSrvBuf)) { svSelName = svRenSrvBuf; svNextList = 0; Toast("Server renamed", 1); }
                    }
                    ImGui::Separator();
                    ImGui::TextDisabled("Invite code"); ImGui::SameLine();
                    std::string inv; for (const auto& s : svList) if (s.id == svSel) inv = s.invite;
                    ImGui::TextUnformatted(inv.c_str()); ImGui::SameLine();
                    if (ImGui::SmallButton("Copy##inv")) { ImGui::SetClipboardText(inv.c_str()); Toast("Invite code copied", 1); }
                    ImGui::Separator();
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f,0.22f,0.22f,1));
                    if (ImGui::Button("Delete this server", ImVec2(-1, 0))) ImGui::OpenPopup("Delete server?");
                    ImGui::PopStyleColor();
                    if (ImGui::BeginPopupModal("Delete server?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::TextWrapped("Delete \"%s\" for everyone? Its channels and messages are removed. This can't be undone.", svSelName.c_str());
                        ImGui::Dummy(ImVec2(0, 4));
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f,0.22f,0.22f,1));
                        if (ImGui::Button("Delete", ImVec2(120, 0))) {
                            if (sv.DeleteServer(svSel)) { svSel = 0; svChan = 0; svNextList = 0; Toast("Server deleted", 1); }
                            ImGui::CloseCurrentPopup(); ImGui::CloseCurrentPopup();
                        }
                        ImGui::PopStyleColor();
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
                        ImGui::EndPopup();
                    }
                    ImGui::Dummy(ImVec2(0, 2));
                    if (ImGui::Button("Close", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
        } else if (tab == 9) {                            // ---- Forums ----
            sectionHeader("Forums", "Discuss games, ask for help, and share ideas with the community.");
            okay::Forums fo(account);
            static std::vector<okay::ForumThread> ftCache;
            static std::vector<okay::ForumPost> fpCache;
            static std::map<long,long> thScores, poScores;
            static std::map<long,int>  thMy, poMy, thComments;
            static long   openThread = 0;
            static std::string openTitle, openAuthor, openBody, openCat, foErr;
            static long openWhen = 0;
            static char newThread[160] = "", newBody[800] = "", newCat[48] = "", replyBuf[600] = "";
            static int  foSort = 0;      // 0 Hot, 1 New, 2 Top
            static bool foShowNew = false;
            static Uint32 foNext = 0, fpNext = 0;
            Uint32 tnow = SDL_GetTicks();
            // Kick a background refresh (never blocks the tab switch); apply the
            // results on a later frame once the worker publishes them.
            if (!g_foMutex) g_foMutex = SDL_CreateMutex();
            if (tnow >= foNext && !g_foFetching && fo.Available()) {
                g_foFetching = true; foNext = tnow + 20000;
                if (SDL_Thread* ft = SDL_CreateThread(ForumFetchWorker, "okay-forum", nullptr))
                    SDL_DetachThread(ft);
                else g_foFetching = false;
            }
            if (g_foMutex) {
                SDL_LockMutex(g_foMutex);
                if (g_foReady) {
                    ftCache = g_foThreads; thScores = g_foScores; thMy = g_foMy; thComments = g_foComments;
                    g_foReady = false;
                }
                SDL_UnlockMutex(g_foMutex);
            }
            if (ftCache.empty() && g_foFetching) { ImGui::Dummy(ImVec2(0, 8)); ImGui::TextDisabled("Loading threads\xE2\x80\xA6"); }

            // A compact up/score/down vote widget (Reddit-style). Toggles like
            // Reddit: clicking your current direction clears the vote. Returns true
            // if a vote changed (so the caller can refresh).
            // Vote widget: updates the local score/vote maps OPTIMISTICALLY and
            // fires the POST on a background thread, so a click never stalls the
            // frame. `scores`/`myVotes` are the live caches for this kind.
            auto voteWidget = [&](const char* kind, long id,
                                  std::map<long,long>& scores, std::map<long,int>& myVotes) -> bool {
                long baseScore = scores.count(id) ? scores[id] : 0L;
                int  myVote    = myVotes.count(id) ? myVotes[id] : 0;
                bool changed = false;
                bool canVote = fo.Available();
                auto apply = [&](int nv) {
                    scores[id] = baseScore + (nv - myVote);   // optimistic net-score delta
                    myVotes[id] = nv;
                    FireForumAction(new FoAction{0, kind, id, nv, "", "", ""});
                    changed = true;
                };
                ImGui::BeginGroup();
                ImGui::PushID((int)id); ImGui::PushID(kind);
                ImU32 up  = myVote == 1  ? IM_COL32(236,128,64,255)  : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                ImU32 dn  = myVote == -1 ? IM_COL32(110,130,240,255) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                ImGui::PushStyleColor(ImGuiCol_Text, up);
                if (ImGui::ArrowButton("u", ImGuiDir_Up) && canVote) apply(myVote == 1 ? 0 : 1);
                ImGui::PopStyleColor();
                // Score, centered under the arrow, tinted by my vote.
                char sc[16]; std::snprintf(sc, sizeof(sc), "%ld", scores.count(id) ? scores[id] : baseScore);
                float aw = ImGui::GetItemRectSize().x, tw = ImGui::CalcTextSize(sc).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (aw - tw) * 0.5f);
                int shownVote = myVotes.count(id) ? myVotes[id] : 0;
                ImVec4 scCol = shownVote==1?ImVec4(0.93f,0.5f,0.25f,1):shownVote==-1?ImVec4(0.43f,0.51f,0.94f,1):ImVec4(0.8f,0.82f,0.88f,1);
                ImGui::TextColored(scCol, "%s", sc);
                ImGui::PushStyleColor(ImGuiCol_Text, dn);
                if (ImGui::ArrowButton("d", ImGuiDir_Down) && canVote) apply(myVote == -1 ? 0 : -1);
                ImGui::PopStyleColor();
                ImGui::PopID(); ImGui::PopID();
                ImGui::EndGroup();
                return changed;
            };

            if (openThread == 0) {
                // Toolbar: sort selector + "New post" toggle.
                ImGui::SetNextItemWidth(150);
                ImGui::Combo("##fosort", &foSort, "Hot\0New\0Top\0");
                ImGui::SameLine();
                if (foShowNew) ImGui::PushStyleColor(ImGuiCol_Button, kAccentDim);
                if (PrimaryButton(foShowNew ? "Cancel" : "+ New post", ImVec2(130, 0))) foShowNew = !foShowNew;
                if (foShowNew) ImGui::PopStyleColor();
                ImGui::SameLine(); if (ImGui::Button("Refresh", ImVec2(84, 0))) foNext = 0;

                if (foShowNew) {
                    ImGui::BeginChild("##newpost", ImVec2(0, 182), true);
                    if (!fo.Available()) {
                        ImGui::TextDisabled("Sign in to post.");
                    } else {
                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputTextWithHint("##nt", "Title", newThread, sizeof(newThread));
                        ImGui::SetNextItemWidth(220);
                        ImGui::InputTextWithHint("##ncat", "Topic (e.g. help, showcase)", newCat, sizeof(newCat));
                        ImGui::InputTextMultiline("##nb", newBody, sizeof(newBody), ImVec2(-1, 80));
                        if (PrimaryButton("Post", ImVec2(120, 0)) && newThread[0]) {
                            // Fire the post off-thread; optimistically close the form.
                            FireForumAction(new FoAction{2, "", 0, 0, newThread, newBody, newCat});
                            newThread[0] = newBody[0] = newCat[0] = '\0'; foErr.clear();
                            foShowNew = false; foNext = tnow + 1200;   // refresh shortly after
                        }
                        if (!foErr.empty()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.9f,0.45f,0.45f,1), "%s", foErr.c_str()); }
                    }
                    ImGui::EndChild();
                }
                ImGui::Dummy(ImVec2(0, 4));

                // Sorted view of the threads.
                std::vector<const okay::ForumThread*> order;
                order.reserve(ftCache.size());
                for (auto& t : ftCache) order.push_back(&t);
                auto scoreOf = [&](long id){ auto it = thScores.find(id); return it!=thScores.end()?it->second:0L; };
                if (foSort == 2)        // Top: highest score
                    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b){ return scoreOf(a->id) > scoreOf(b->id); });
                else if (foSort == 0)   // Hot: score, then recency
                    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b){
                        if (scoreOf(a->id) != scoreOf(b->id)) return scoreOf(a->id) > scoreOf(b->id);
                        return a->when > b->when; });
                // New (1): ftCache is already newest-first.

                ImGui::BeginChild("##threads", ImVec2(0, ImGui::GetContentRegionAvail().y - 4), false);
                for (const okay::ForumThread* tp : order) {
                    const okay::ForumThread& t = *tp;
                    int nc = thComments.count(t.id) ? thComments[t.id] : 0;
                    ImGui::PushID((int)t.id);
                    ImGui::BeginChild("##trow", ImVec2(0, 66), true);
                    HoverRing();
                    ImGui::SetCursorPos(ImVec2(6, 8));
                    voteWidget("thread", t.id, thScores, thMy);
                    ImGui::SameLine(); ImGui::SetCursorPosX(52);
                    ImGui::BeginGroup();
                    ImGui::SetCursorPosY(9);
                    if (!t.category.empty()) {
                        ImVec2 bp = ImGui::GetCursorScreenPos(); ImVec2 ks = ImGui::CalcTextSize(t.category.c_str());
                        ImGui::GetWindowDrawList()->AddRectFilled(bp, ImVec2(bp.x+ks.x+12, bp.y+ks.y+4), ImGui::GetColorU32(kAccentDim), (ks.y+4)*0.5f);
                        ImGui::GetWindowDrawList()->AddText(ImVec2(bp.x+6, bp.y+2), IM_COL32(235,238,245,255), t.category.c_str());
                        ImGui::Dummy(ImVec2(ks.x+12, ks.y+4)); ImGui::SameLine(0, 8);
                    }
                    // Emphasized title.
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.96f, 0.98f, 1));
                    ImGui::TextUnformatted(t.title.c_str());
                    ImGui::PopStyleColor();
                    // Meta: a small author color dot, then author · time · replies, with
                    // a comment count chip tinted when there are replies.
                    ImVec2 mp = ImGui::GetCursorScreenPos();
                    ImVec4 ac = NameColor(t.author.empty() ? "?" : t.author);
                    ImGui::GetWindowDrawList()->AddCircleFilled(
                        ImVec2(mp.x + 5, mp.y + ImGui::GetTextLineHeight() * 0.5f + 2), 5.0f,
                        ImGui::GetColorU32(ac), 16);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 16);
                    std::string when = RelTime(t.when);
                    ImGui::TextDisabled("%s%s%s  \xC2\xB7  %d repl%s",
                        t.author.empty() ? "unknown" : t.author.c_str(),
                        when.empty() ? "" : "  \xC2\xB7  ", when.c_str(),
                        nc, nc == 1 ? "y" : "ies");
                    ImGui::EndGroup();
                    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                        !ImGui::IsAnyItemHovered()) {
                        openThread=t.id; openTitle=t.title; openAuthor=t.author; openBody=t.body; openCat=t.category; openWhen=t.when; fpNext=0;
                        fpCache.clear(); poScores.clear(); poMy.clear();   // show the loading state, not stale comments
                    }
                    ImGui::EndChild();
                    ImGui::PopID();
                }
                if (ftCache.empty()) {
                    ImGui::Dummy(ImVec2(0, 20));
                    if (!fo.Available()) ImGui::TextDisabled("Browse freely; sign in to post and vote.");
                    ImGui::TextDisabled("No discussions yet — start the first one with \"+ New post\".");
                }
                ImGui::EndChild();
            } else {
                if (ImGui::Button("< Back")) { openThread = 0; fpCache.clear(); }
                ImGui::SameLine();
                if (!openCat.empty()) { ImGui::TextDisabled("[%s]", openCat.c_str()); ImGui::SameLine(); }
                ImGui::TextUnformatted(openTitle.c_str());
                // Fetch the thread's comments + vote maps on a background thread,
                // so opening a post never blocks the frame.
                if (tnow >= fpNext && !g_foPostFetching) {
                    g_foPostFetching = true; fpNext = tnow + 6000;
                    if (SDL_Thread* pt = SDL_CreateThread(ForumPostWorker, "okay-fopost",
                                                          (void*)(intptr_t)openThread))
                        SDL_DetachThread(pt);
                    else g_foPostFetching = false;
                }
                if (g_foMutex) {
                    SDL_LockMutex(g_foMutex);
                    if (g_foPostReady && g_foPostHave == openThread) {
                        fpCache = g_foPosts; poScores = g_foPostScores; poMy = g_foPostMy;
                        thScores = g_foPostThScores; thMy = g_foPostThMy;
                        g_foPostReady = false;
                    }
                    SDL_UnlockMutex(g_foMutex);
                }
                bool detailLoading = g_foPostHave != openThread;   // first open, not cached yet
                // The original post card: vote column + author/time + self-text.
                ImGui::BeginChild("##opcard", ImVec2(0, openBody.empty() ? 70 : 128), true);
                ImGui::SetCursorPos(ImVec2(6, 8));
                voteWidget("thread", openThread, thScores, thMy);
                ImGui::SameLine(); ImGui::SetCursorPosX(52);
                ImGui::BeginGroup();
                ImGui::TextDisabled("Posted by %s  -  %s", openAuthor.empty()?"unknown":openAuthor.c_str(), RelTime(openWhen).c_str());
                if (!openBody.empty()) RenderChatBody(openBody);
                else ImGui::TextDisabled("(no body)");
                ImGui::EndGroup();
                ImGui::EndChild();

                ImGui::BeginChild("##fposts", ImVec2(0, ImGui::GetContentRegionAvail().y - 42), false);
                std::string me = account.CurrentSession().username;
                int shownC = 0;
                for (const auto& p : fpCache) {
                    ++shownC;
                    ImGui::PushID((int)p.id);
                    ImGui::BeginChild("##crow", ImVec2(0, p.body.size() > 80 ? 92 : 68), true);
                    ImGui::SetCursorPos(ImVec2(6, 8));
                    voteWidget("post", p.id, poScores, poMy);
                    ImGui::SameLine(); ImGui::SetCursorPosX(52);
                    ImGui::BeginGroup();
                    bool mine = !me.empty() && p.author == me;
                    ImGui::TextColored(mine ? ImVec4(0.62f,0.78f,1.0f,1) : ImVec4(0.72f,0.76f,0.84f,1),
                                       "%s", p.author.empty()?"unknown":p.author.c_str());
                    ImGui::SameLine(0,8); ImGui::TextDisabled("%s", RelTime(p.when).c_str());
                    RenderChatBody(p.body);
                    ImGui::EndGroup();
                    ImGui::EndChild();
                    ImGui::PopID();
                }
                if (shownC == 0) {
                    ImGui::Dummy(ImVec2(0, 10));
                    ImGui::TextDisabled(detailLoading ? "Loading comments\xE2\x80\xA6"
                                                      : "No comments yet. Be the first to comment.");
                }
                ImGui::EndChild();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90);
                bool rp = ImGui::InputTextWithHint("##reply", "Add a comment...", replyBuf, sizeof(replyBuf), ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                if ((PrimaryButton("Comment", ImVec2(82, 0)) || rp) && replyBuf[0]) {
                    // Post the comment off-thread; clear the box immediately and
                    // refresh the detail shortly after so it appears.
                    FireForumAction(new FoAction{1, "", openThread, 0, replyBuf, "", ""});
                    replyBuf[0] = '\0'; foErr.clear(); fpNext = tnow + 1000;
                }
            }
        } else if (tab == 10) {                           // ---- Downloads ----
            sectionHeader("Downloads", "Games you download from the marketplace land here.");
            fs::path ddir = fs::path(g_exeDir) / "downloads";
            if (ImGui::Button("Open downloads folder", ImVec2(200, 0))) {
                std::error_code ec; fs::create_directories(ddir, ec);
                OpenExternal(ddir.string());
            }
            ImGui::SameLine();
            bool anyDone = false;
            SDL_LockMutex(g_dlMutex);
            for (auto& d : g_dls) if (d->status >= 2) { anyDone = true; break; }
            SDL_UnlockMutex(g_dlMutex);
            ImGui::BeginDisabled(!anyDone);
            if (ImGui::Button("Clear finished", ImVec2(130, 0))) {
                SDL_LockMutex(g_dlMutex);
                for (auto it = g_dls.begin(); it != g_dls.end(); ) {
                    if ((*it)->status >= 2) { if ((*it)->th) SDL_WaitThread((*it)->th, nullptr); it = g_dls.erase(it); }
                    else ++it;
                }
                SDL_UnlockMutex(g_dlMutex);
            }
            ImGui::EndDisabled();
            ImGui::Dummy(ImVec2(0, 6));

            // Snapshot the list under the lock so the UI iterates safely.
            std::vector<std::shared_ptr<DlItem>> snap;
            SDL_LockMutex(g_dlMutex);
            snap = g_dls;
            SDL_UnlockMutex(g_dlMutex);

            ImGui::BeginChild("##dllist", ImVec2(0, ImGui::GetContentRegionAvail().y - 46.0f), false);
            if (snap.empty()) {
                float cw = ImGui::GetContentRegionAvail().x;
                auto center = [&](const char* s, bool dim) {
                    float tw = ImGui::CalcTextSize(s).x, o = (cw - tw) * 0.5f;
                    if (o > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + o);
                    if (dim) ImGui::TextDisabled("%s", s); else ImGui::TextUnformatted(s);
                };
                ImGui::Dummy(ImVec2(0, 34));
                // Download-into-tray icon.
                ImVec2 ip = ImGui::GetCursorScreenPos();
                float icx = ip.x + cw * 0.5f, icy = ip.y + 12;
                ImDrawList* dl2 = ImGui::GetWindowDrawList();
                ImU32 ic = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                dl2->AddLine(ImVec2(icx, icy - 16), ImVec2(icx, icy + 6), ic, 2.6f);
                dl2->AddLine(ImVec2(icx - 9, icy - 3), ImVec2(icx, icy + 6), ic, 2.6f);
                dl2->AddLine(ImVec2(icx + 9, icy - 3), ImVec2(icx, icy + 6), ic, 2.6f);
                dl2->AddLine(ImVec2(icx - 15, icy + 15), ImVec2(icx + 15, icy + 15), ic, 2.6f);
                ImGui::Dummy(ImVec2(0, 40));
                center("No downloads yet", false);
                ImGui::Dummy(ImVec2(0, 2));
                center("Games you get from the Marketplace land here.", true);
                ImGui::Dummy(ImVec2(0, 12));
                float bw = 210.0f; ImGui::SetCursorPosX((cw - bw) * 0.5f);
                if (PrimaryButton("Browse Marketplace", ImVec2(bw, 38))) { tab = 7; g_lastTab = 7; SavePrefs(); }
            }
            auto humanBytes = [](long long b) {
                char out[32];
                if (b <= 0) std::snprintf(out, sizeof(out), "--");
                else if (b < 1024) std::snprintf(out, sizeof(out), "%lld B", b);
                else if (b < 1024*1024) std::snprintf(out, sizeof(out), "%.1f KB", b/1024.0);
                else if (b < 1024LL*1024*1024) std::snprintf(out, sizeof(out), "%.1f MB", b/(1024.0*1024));
                else std::snprintf(out, sizeof(out), "%.2f GB", b/(1024.0*1024*1024));
                return std::string(out);
            };
            for (auto& d : snap) {
                long long rec, tot; int stt; std::string title, dest;
                SDL_LockMutex(g_dlMutex);
                rec = d->received; tot = d->total; stt = d->status; title = d->title; dest = d->dest;
                SDL_UnlockMutex(g_dlMutex);
                ImGui::PushID(d->id);
                ImGui::BeginChild("##dlrow", ImVec2(0, 80), true);
                HoverRing();
                ImGui::TextUnformatted(title.c_str());
                const char* stTxt = stt==0?"Queued":stt==1?"Downloading":stt==2?"Completed":stt==3?"Failed":"Canceled";
                ImVec4 stCol = stt==2?ImVec4(0.55f,0.9f,0.6f,1):stt==3?ImVec4(0.9f,0.45f,0.45f,1)
                             : stt==4?ImVec4(0.75f,0.7f,0.5f,1):ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
                // Progress bar.
                float frac = (tot > 0) ? (float)rec / (float)tot : (stt==2?1.0f:0.0f);
                if (frac > 1.0f) frac = 1.0f;
                char overlay[64];
                if (tot > 0) std::snprintf(overlay, sizeof(overlay), "%s / %s", humanBytes(rec).c_str(), humanBytes(tot).c_str());
                else         std::snprintf(overlay, sizeof(overlay), "%s", humanBytes(rec).c_str());
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 220);
                ImGui::ProgressBar(frac, ImVec2(ImGui::GetContentRegionAvail().x - 220, 16), overlay);
                ImGui::SameLine();
                ImGui::TextColored(stCol, "%s", stTxt);
                // Actions.
                if (stt == 0 || stt == 1) {
                    if (ImGui::SmallButton("Cancel")) { SDL_LockMutex(g_dlMutex); d->cancelReq = true; SDL_UnlockMutex(g_dlMutex); }
                } else {
                    if (stt == 2) {
                        if (ImGui::SmallButton("Open folder")) OpenExternal(ddir.string());
                        bool isZip = dest.size() > 4 &&
                            (dest.substr(dest.size()-4) == ".zip" || dest.substr(dest.size()-4) == ".ZIP");
                        if (isZip) {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Install to Community")) {
                                fs::path sub = CommunityDir() /
                                    fs::path(dest).stem();
                                if (ExtractZip(dest, sub.string())) { Toast("Installed to Community", 1); scenes = FindScenes(); }
                                else Toast("Couldn't extract the archive", 2);
                            }
                        }
                        ImGui::SameLine();
                    }
                    if (ImGui::SmallButton("Remove")) {
                        SDL_LockMutex(g_dlMutex);
                        for (auto it = g_dls.begin(); it != g_dls.end(); ++it)
                            if ((*it)->id == d->id) { if ((*it)->th) SDL_WaitThread((*it)->th, nullptr); g_dls.erase(it); break; }
                        SDL_UnlockMutex(g_dlMutex);
                    }
                }
                ImGui::EndChild();
                ImGui::PopID();
            }
            ImGui::EndChild();
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

        // ---- Floating Chat window (DMs), available over any tab ----
        DrawChatWindow(account);

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
        SDL_SetRenderDrawColor(renderer, 30, 30, 32, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        // Cap the frame rate so the launcher never spins the GPU/CPU at full tilt
        // (which makes the whole machine feel laggy) — important if the renderer
        // fell back to a non-VSync mode. ~60 fps while focused, ~20 fps when the
        // window is in the background. With real VSync the present already took a
        // frame, so the extra delay is ~0 when focused.
        {
            Uint32 flags = SDL_GetWindowFlags(window);
            bool focused = (flags & (SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS)) != 0;
            double targetMs = focused ? (1000.0 / 60.0) : (1000.0 / 20.0);
            double elapsed = (double)(SDL_GetPerformanceCounter() - frameStart) * 1000.0
                           / (double)SDL_GetPerformanceFrequency();
            if (elapsed < targetMs) SDL_Delay((Uint32)(targetMs - elapsed));
        }
    }

    // Remember the window size for next launch.
    { int w = 0, h = 0; SDL_GetWindowSize(window, &w, &h);
      if (w > 0 && h > 0) { g_winW = w; g_winH = h; SavePrefs(); } }

    // Stop the background friends/DM poll + chat poll threads.
    g_pollRun = false; g_chatPollRun = false; g_srvPollRun = false;
    if (g_pollThread) { SDL_WaitThread(g_pollThread, nullptr); g_pollThread = nullptr; }
    if (g_chatThread) { SDL_WaitThread(g_chatThread, nullptr); g_chatThread = nullptr; }
    if (g_srvThread) { SDL_WaitThread(g_srvThread, nullptr); g_srvThread = nullptr; }
    if (g_pollMutex) { SDL_DestroyMutex(g_pollMutex); g_pollMutex = nullptr; }
    if (g_chatMutex) { SDL_DestroyMutex(g_chatMutex); g_chatMutex = nullptr; }
    if (g_srvMutex) { SDL_DestroyMutex(g_srvMutex); g_srvMutex = nullptr; }
    if (g_upThread) { SDL_WaitThread(g_upThread, nullptr); g_upThread = nullptr; }
    if (g_upMutex) { SDL_DestroyMutex(g_upMutex); g_upMutex = nullptr; }
    // Stop any in-flight downloads and join their worker threads before teardown.
    if (g_dlMutex) {
        SDL_LockMutex(g_dlMutex);
        for (auto& d : g_dls) d->cancelReq = true;
        std::vector<SDL_Thread*> threads;
        for (auto& d : g_dls) if (d->th) threads.push_back(d->th);
        SDL_UnlockMutex(g_dlMutex);
        for (SDL_Thread* t : threads) SDL_WaitThread(t, nullptr);
        g_dls.clear();
        SDL_DestroyMutex(g_dlMutex); g_dlMutex = nullptr;
    }
#if defined(_WIN32)
    OkayTrayRemove();   // make sure no stray tray icon lingers after exit
#endif
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
