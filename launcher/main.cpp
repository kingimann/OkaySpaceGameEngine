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
#include "OkayTheme.hpp"  // shared Slate palette (launcher + editor)
#include "Splash.hpp"   // branded loading frame so startup isn't a black screen
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Platform/Achievements.hpp"
#include "okay/Platform/Friends.hpp"
#include "okay/Platform/Workshop.hpp"
#include "okay/Platform/Messages.hpp"
#include "okay/Platform/Forums.hpp"
#include "okay/Platform/Servers.hpp"
#include "okay/Platform/Profiles.hpp"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "RobotoFont.h"     // embedded Roboto Medium (Apache 2.0) — shared with the editor
#include "JetBrainsMonoFont.h" // embedded JetBrains Mono (OFL) — `code` in chat
#include "tinyfiledialogs.h" // native file picker (chat attachments)

// stb_image for optional per-game cover art. STATIC keeps its symbols file-local
// (the launcher doesn't link the engine, so there's no clash, but this is tidy).
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF        // animated GIFs in chat (stbi_load_gif_from_memory)
#include "stb_image.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

// URLs reach curl / wget / powershell / the browser through a shell command
// line wrapped in quotes, and many come from other people (chat links, GIFs,
// stickers, marketplace items). Only let through an http(s) URL made of plain
// URL characters: nothing that can close the quotes or expand inside them
// (" ' ` $ \ spaces, control bytes), "%" only as a %XX escape, and no
// %name% pair that cmd.exe would expand to an environment value.
bool ShellSafeUrl(const std::string& url) {
    if (url.size() < 8 || url.size() > 4096) return false;
    if (url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0) return false;
    static const char* kOk = "-._~:/?#[]@!*+,;=&()";
    for (std::size_t i = 0; i < url.size(); ++i) {
        const unsigned char c = (unsigned char)url[i];
        if (std::isalnum(c) || std::strchr(kOk, c)) continue;
        if (c == '%' && i + 2 < url.size() &&
            std::isxdigit((unsigned char)url[i + 1]) && std::isxdigit((unsigned char)url[i + 2])) continue;
        return false;
    }
    for (std::size_t a = url.find('%'); a != std::string::npos; ) {
        const std::size_t b = url.find('%', a + 1);
        if (b == std::string::npos) break;
        std::string name = url.substr(a + 1, b - a - 1), up;
        for (char ch : name) up += (char)std::toupper((unsigned char)ch);
        static const char* kDynamic[] = {"CD", "DATE", "TIME", "RANDOM", "ERRORLEVEL", "CMDEXTVERSION",
                                         "CMDCMDLINE", "HIGHESTNUMANODENUMBER", "__CD__", "__APPDIR__"};
        for (const char* dv : kDynamic) if (up == dv) return false;
        if (!name.empty() && std::getenv(name.c_str())) return false;
        a = b;
    }
    return true;
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
    if (!ShellSafeUrl(url)) return false;
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
    if (!ShellSafeUrl(url)) return 0;
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
    if (!ShellSafeUrl(d->url)) { set([&]{ d->status = 3; }); return 0; }
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
    if (!ShellSafeUrl(url)) return 0;
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

bool g_upRepairMissing = false;   // "Get the editor": same-version re-download of MISSING exes only
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
    // Repair: on the SAME version, fetch only executables that are missing
    // (e.g. the editor was never installed). Never downloads anything older.
    const bool repair = g_upRepairMissing && !newer && !latest.empty() && CompareVersions(local, latest) == 0;
    g_upRepairMissing = false;
    if (!newer && !repair) {
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
        if (!repair || FindExe({"OkayEngine.exe"}).empty()) {
            SetUpMsg("Downloading engine v" + latest + "...");
            SetUpStep("Downloading editor (OkayEngine)", 0, kSteps);
            ok = ReplaceFile(std::string(kRawBase) + "OkayEngine.exe",
                             destOf("OkayEngine.exe"), false, /*progress=*/true) && ok;
        }
        if (!repair || FindExe({"OkaySpacePlayer.exe"}).empty()) {
            SetUpMsg("Downloading player runtime v" + latest + "...");
            SetUpStep("Downloading game runtime (OkaySpacePlayer)", 1, kSteps);
            ok = ReplaceFile(std::string(kRawBase) + "OkaySpacePlayer.exe",
                             destOf("OkaySpacePlayer.exe"), false, /*progress=*/true) && ok;
        }
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

// ---- Periodic "is a newer version out?" check -------------------------------
// The full updater (RunUpdateCheck) installs immediately, which would be rude
// mid-session. While the launcher is open or minimized it only *looks* every
// 20 minutes; a newer version raises a toast + notification and the sidebar
// offers "Install now".
std::string g_upAvailable;                 // newer published version ("" = none known)
static SDL_mutex* g_upPollMutex = nullptr;
static std::string g_upPollResult;         // (guarded)
static bool g_upPollDone = false;          // (guarded)
static volatile bool g_upPollBusy = false;
static int UpdatePollWorker(void*) {
    std::error_code ec;
    fs::path vf = fs::temp_directory_path(ec) / "okayspace_launcher_ver_poll.txt";
    std::string latest;
    if (Download(std::string(kRawBase) + "VERSION.txt", vf.string())) {
        std::ifstream f(vf); std::getline(f, latest);
        while (!latest.empty() && (latest.back() == '\r' || latest.back() == '\n' || latest.back() == ' ')) latest.pop_back();
    }
    fs::remove(vf, ec);
    SDL_LockMutex(g_upPollMutex);
    g_upPollResult = latest; g_upPollDone = true;
    SDL_UnlockMutex(g_upPollMutex);
    g_upPollBusy = false;
    return 0;
}
static void StartUpdatePoll() {
    if (g_upPollBusy) return;
    if (!g_upPollMutex) g_upPollMutex = SDL_CreateMutex();
    g_upPollBusy = true;
    if (SDL_Thread* t = SDL_CreateThread(UpdatePollWorker, "okay-uppoll", nullptr)) SDL_DetachThread(t);
    else g_upPollBusy = false;
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
    // Links (often from other people) must pass the URL check; local paths
    // just can't close the quotes. POSIX shells also expand $ and ` inside "".
    const bool isLink = target.find("://") != std::string::npos;
    if (isLink && !ShellSafeUrl(target)) { Toast("That link can't be opened safely", 2); return; }
    if (target.find('"') != std::string::npos) return;
#if !defined(_WIN32)
    if (target.find_first_of("$`\\") != std::string::npos) return;
#endif
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

// How many playable games the library holds: built games + web games. Editor
// projects / loose dev scenes don't count. Cached (re-checked every 2s or when
// the scene list changes) so the sidebar badge doesn't hit the disk every frame.
int g_webGameCount = -1;   // set by the Play tab's web-game scan (-1 = not scanned yet)
std::vector<fs::path> FindWebGames();
int LibraryGameCount(const std::vector<fs::path>& scenes) {
    static int cached = 0; static std::size_t lastN = (std::size_t)-1; static Uint32 lastAt = 0;
    if (g_webGameCount < 0) g_webGameCount = (int)FindWebGames().size();
    Uint32 now = SDL_GetTicks();
    if (scenes.size() != lastN || now - lastAt > 2000) {
        cached = (int)std::count_if(scenes.begin(), scenes.end(), IsBuiltGame);
        lastN = scenes.size(); lastAt = now;
    }
    return cached + g_webGameCount;
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
// Messenger/WhatsApp-style per-conversation extras (client-side, persisted).
std::set<std::string>              g_blockedUsers;   // uids I've blocked: their messages hidden, can't send
std::map<std::string,int>          g_chatColor;      // uid -> per-chat theme colour index (0 = default)
struct StarredMsg { std::string uid; std::string preview; long when = 0; };
std::map<long, StarredMsg>         g_starred;        // message id -> starred ("saved") message
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
bool g_chatSounds = true;         // soft chime for new chat messages (persisted)
bool g_linkPreviews = true;       // fetch title/image cards for links in chat (persisted)
int  g_voiceSpeed = 0;            // voice-note playback speed: 0 = 1x, 1 = 1.5x, 2 = 2x (persisted)
bool g_chatListFolded = false;    // chat conversation list folded to an avatar strip (persisted)
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
const char* kThemeNames[] = {"Slate", "Midnight", "Light"};

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
std::set<std::string> g_notifFresh; // UI: keys that were unread when the panel opened (shown as new)
ImVec2 g_notifBellMin, g_notifBellMax;   // UI: sidebar bell row (clicks there toggle, not "outside")

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
    } else {                                              // Slate (default): shared with the editor + chat
        okaytheme::ApplySlateMetrics(s);
        s.WindowRounding = 0.0f;           // the launcher's root window fills the OS window
        okaytheme::ApplySlateColors(s, accent);
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
// "Last online 3h ago" for someone offline (empty when we don't know: never
// seen, or hidden by the Invisible status).
std::string LastOnline(long when) {
    if (when <= 978307200) return std::string();
    return "Last online " + RelTime(when);
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
static constexpr int kEmojiUnknown = 99;   // a Unicode emoji we have no drawing for
static constexpr int kEmojiUnicode = 98;   // "<e:1F600>" — a real emoji, drawn from the Twemoji sheet
static constexpr int kEmojiCustom  = 97;   // "<:name:url>" — a user's custom emoji image
static int EmojiId(const std::string& t) {
    for (const auto& e : kEmojis) if (t == e.code || (e.alt && t == e.alt)) return e.id;
    if (t == ":emoji?:") return kEmojiUnknown;
    if (t.size() > 4 && t.back() == '>') {
        if (t.compare(0, 3, "<e:") == 0) return kEmojiUnicode;
        if (t.compare(0, 2, "<:") == 0 && t.find(':', 2) != std::string::npos) return kEmojiCustom;
    }
    return -1;
}
// Draws any emoji token (shortcode, Unicode cluster, custom image) centred at c;
// defined with the chat media code further down.
static void DrawEmojiToken(ImDrawList* dl, ImVec2 c, float s, const std::string& tok);

// Real Unicode emoji (typed on a phone, pasted, or from the picker) have no
// glyphs in the UI font, so each emoji *cluster* (incl. skin tones, ZWJ
// families, flags and keycaps) becomes a token "<e:1F44D-1F3FB>" that the chat
// renders from the cached Twemoji sheet (DrawEmojiToken).
static bool EmojiIsPictograph(unsigned cp) {
    return (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF) || cp == 0x2B50 || cp == 0x2B55 ||
           (cp >= 0x2B05 && cp <= 0x2B07) || cp == 0x2B1B || cp == 0x2B1C || cp == 0x231A || cp == 0x231B ||
           cp == 0x2328 || cp == 0x23CF || (cp >= 0x23E9 && cp <= 0x23F3) || (cp >= 0x23F8 && cp <= 0x23FA) ||
           cp == 0x24C2 || cp == 0x25B6 || cp == 0x25C0 || (cp >= 0x25FB && cp <= 0x25FE) || cp == 0x2934 || cp == 0x2935 ||
           cp == 0x3030 || cp == 0x303D || cp == 0x3297 || cp == 0x3299;
}
static unsigned Utf8Decode(const std::string& s, std::size_t i, int& len) {
    unsigned char c0 = (unsigned char)s[i];
    len = c0 < 0x80 ? 1 : (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xE ? 3 : (c0 >> 3) == 0x1E ? 4 : 1;
    if (i + len > s.size()) { len = 1; return c0; }
    if (len == 2) return ((c0 & 0x1Fu) << 6) | (s[i + 1] & 0x3F);
    if (len == 3) return ((c0 & 0x0Fu) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
    if (len == 4) return ((c0 & 0x07u) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
    return c0;
}
static std::string Utf8Encode(unsigned cp) {
    std::string o;
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    return o;
}
static std::string NormalizeEmoji(const std::string& in) {
    bool any = false;
    for (unsigned char ch : in) if (ch >= 0xC2) { any = true; break; }
    if (!any) return in;
    std::string out;
    out.reserve(in.size() + 16);
    std::size_t i = 0, n = in.size();
    auto hex = [](unsigned cp) { char b[12]; std::snprintf(b, sizeof(b), "%X", cp); return std::string(b); };
    while (i < n) {
        int len = 1;
        unsigned cp = Utf8Decode(in, i, len);
        int nlen = 0;
        unsigned next = (i + len < n) ? Utf8Decode(in, i + len, nlen) : 0;
        const bool ri = cp >= 0x1F1E6 && cp <= 0x1F1FF;
        const bool keycap = (cp == '#' || cp == '*' || (cp >= '0' && cp <= '9')) && (next == 0xFE0F || next == 0x20E3);
        const bool textPresent = (cp == 0xA9 || cp == 0xAE || cp == 0x203C || cp == 0x2049 || cp == 0x2122 || cp == 0x2139 ||
                                  (cp >= 0x2194 && cp <= 0x21AA)) && next == 0xFE0F;
        if (cp == 0xFE0F || cp == 0xFE0E || cp == 0x200D) { i += len; continue; }   // stray joiners/selectors
        if (!(EmojiIsPictograph(cp) || ri || keycap || textPresent)) { out.append(in, i, (std::size_t)len); i += len; continue; }
        std::vector<unsigned> cl{cp};
        std::size_t j = i + len;
        if (ri && next >= 0x1F1E6 && next <= 0x1F1FF) { cl.push_back(next); j += nlen; }   // flag = two letters
        while (j < n) {
            int l2 = 0; unsigned c2 = Utf8Decode(in, j, l2);
            if (c2 == 0xFE0F || c2 == 0x20E3 || (c2 >= 0x1F3FB && c2 <= 0x1F3FF) || (c2 >= 0xE0020 && c2 <= 0xE007F)) {
                cl.push_back(c2); j += l2; continue;
            }
            if (c2 == 0x200D && j + l2 < n) {   // ZWJ sequence: keep going if a pictograph follows
                int l3 = 0; unsigned c3 = Utf8Decode(in, j + l2, l3);
                if (EmojiIsPictograph(c3)) { cl.push_back(c2); cl.push_back(c3); j += l2 + l3; continue; }
            }
            break;
        }
        std::string tok = "<e:";
        for (std::size_t k = 0; k < cl.size(); ++k) { if (k) tok += '-'; tok += hex(cl[k]); }
        tok += '>';
        if (!out.empty() && out.back() != ' ' && out.back() != '\n') out += ' ';
        out += tok;
        out += ' ';
        i = j;
    }
    return out;
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
static bool IsEmojiOnly(const std::string& bodyIn, int& count) {
    const std::string body = NormalizeEmoji(bodyIn);
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
    if (id == kEmojiUnknown) {   // neutral tile: "an emoji was here"
        dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), IM_COL32(255, 255, 255, 30), r * 0.45f);
        dl->AddCircleFilled(ImVec2(c.x - r * 0.32f, c.y - r * 0.12f), r * 0.11f, IM_COL32(255, 255, 255, 120), 8);
        dl->AddCircleFilled(ImVec2(c.x + r * 0.32f, c.y - r * 0.12f), r * 0.11f, IM_COL32(255, 255, 255, 120), 8);
        dl->AddLine(ImVec2(c.x - r * 0.3f, c.y + r * 0.35f), ImVec2(c.x + r * 0.3f, c.y + r * 0.35f), IM_COL32(255, 255, 255, 120), 1.5f);
        return;
    }
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
static std::string ChatMediaPicker(const char* id, char* buf, std::size_t cap,
                                   const std::vector<std::string>* recentGifs, bool canSendMedia);   // chat media section
// Emoji button + picker for any text box (inserts real Unicode emoji).
void EmojiPicker(const char* id, char* buf, std::size_t cap) {
    ChatMediaPicker(id, buf, cap, nullptr, false);
}

// ---- Chat text formatting (Discord / Messenger style) ---------------------------
// Blocks: ``` fenced code and "> " quotes. Inline: **bold**, *italic* / _italic_,
// ~~strike~~, `code`, ||spoiler||, plus links, @mentions and emoji tokens. One
// tokenizer feeds both drawing and width measurement, so a chat bubble always
// hugs exactly what gets drawn.
ImFont* g_fontMono = nullptr;   // JetBrains Mono for `code` (loaded in main)
enum { kMdBold = 1, kMdItalic = 2, kMdStrike = 4, kMdCode = 8, kMdSpoiler = 16 };
struct ChatTok { std::string text; int style = 0; bool glue = false; bool newline = false; };
struct ChatBlock { int kind = 0; std::string text; std::vector<ChatTok> toks; };   // kind: 0 text, 1 quote, 2 code

static std::vector<ChatTok> ChatInlineTokens(const std::string& s) {
    const std::size_t n = s.size();
    std::vector<int> st(n, 0);
    std::vector<char> hide(n, 0);
    auto ws = [&](std::size_t k) { return k >= n || s[k] == ' ' || s[k] == '\n'; };
    // Pair an opening marker with a closing one (opener followed by non-space,
    // closer preceded by non-space); wordEdge = "_" style, only at word edges.
    auto pairUp = [&](const char* mk, int flag, bool wordEdge, bool sameLine) {
        const std::size_t L = std::strlen(mk);
        std::size_t i = 0;
        while (i + L <= n) {
            if (hide[i] || (st[i] & kMdCode) || s.compare(i, L, mk) != 0 || ws(i + L) ||
                (wordEdge && i > 0 && !ws(i - 1) && std::strchr("([{\"'", s[i - 1]) == nullptr)) { ++i; continue; }
            std::size_t found = std::string::npos;
            for (std::size_t j = i + L + 1; j + L <= n; ++j) {
                if (sameLine && s[j] == '\n') break;
                if (hide[j] || ((st[j] & kMdCode) && flag != kMdCode)) continue;
                if (s.compare(j, L, mk) == 0 && !ws(j - 1) &&
                    (!wordEdge || ws(j + L) || std::strchr(".,!?:;)]}\"'", s[j + L]) != nullptr)) { found = j; break; }
            }
            if (found == std::string::npos) { i += L; continue; }
            for (std::size_t k = 0; k < L; ++k) { hide[i + k] = 1; hide[found + k] = 1; }
            for (std::size_t k = i + L; k < found; ++k) st[k] |= flag;
            i = found + L;
        }
    };
    pairUp("`", kMdCode, false, true);
    pairUp("||", kMdSpoiler, false, false);
    pairUp("**", kMdBold, false, false);
    pairUp("~~", kMdStrike, false, false);
    pairUp("*", kMdItalic, false, false);
    pairUp("_", kMdItalic, true, false);
    // Words; a style change mid-word splits it into "glued" pieces (no space).
    std::vector<ChatTok> out;
    std::string cur; int curSt = 0; bool space = false, lineStart = true;
    auto flush = [&]() {
        if (cur.empty()) return;
        ChatTok t; t.text = cur; t.style = curSt; t.glue = !space && !lineStart;
        out.push_back(t);
        cur.clear(); space = false; lineStart = false;
    };
    for (std::size_t k = 0; k < n; ++k) {
        if (hide[k]) continue;
        const char c = s[k];
        const bool inCode = (st[k] & kMdCode) != 0;
        if (c == '\n') { flush(); ChatTok t; t.newline = true; out.push_back(t); space = false; lineStart = true; continue; }
        if (c == ' ' && !inCode) { flush(); space = true; continue; }
        if (!cur.empty() && st[k] != curSt) flush();
        if (cur.empty()) curSt = st[k];
        cur += c;
    }
    flush();
    return out;
}
static std::vector<ChatBlock> ChatBlocks(const std::string& body) {
    std::vector<ChatBlock> out;
    std::vector<std::string> lines;
    { std::size_t p = 0; while (true) { std::size_t q = body.find('\n', p); lines.push_back(body.substr(p, q == std::string::npos ? std::string::npos : q - p)); if (q == std::string::npos) break; p = q + 1; } }
    auto push = [&](int kind, const std::string& t) {
        ChatBlock b; b.kind = kind; b.text = t;
        if (kind != 2) b.toks = ChatInlineTokens(t);
        out.push_back(b);
    };
    std::string para; bool havePara = false;
    std::size_t i = 0;
    while (i < lines.size()) {
        const std::string& L = lines[i];
        if (L.rfind("```", 0) == 0) {
            if (havePara) { push(0, para); para.clear(); havePara = false; }
            std::string rest = L.substr(3);
            std::size_t endInline = rest.find("```");
            if (endInline != std::string::npos) { push(2, rest.substr(0, endInline)); ++i; continue; }   // ```one line```
            std::string code; bool any = false;
            for (++i; i < lines.size() && lines[i].rfind("```", 0) != 0; ++i) { if (any) code += '\n'; code += lines[i]; any = true; }
            if (i < lines.size()) ++i;   // closing fence
            push(2, code);
            continue;
        }
        if (!L.empty() && L[0] == '>' && (L.size() == 1 || L[1] == ' ')) {
            if (havePara) { push(0, para); para.clear(); havePara = false; }
            std::string q; bool any = false;
            for (; i < lines.size() && !lines[i].empty() && lines[i][0] == '>'; ++i) {
                std::string x = lines[i].substr(1);
                if (!x.empty() && x[0] == ' ') x.erase(0, 1);
                if (any) q += '\n';
                q += x; any = true;
            }
            push(1, q);
            continue;
        }
        if (havePara) para += '\n';
        para += L; havePara = true; ++i;
    }
    if (havePara || out.empty()) push(0, para);
    return out;
}
static bool ChatBodyIsPlain(const std::string& body) {
    return body.find_first_of("*_~`|>@:<") == std::string::npos && body.find("http") == std::string::npos &&
           body.find("xD") == std::string::npos;
}
// Lay out (and optionally draw) inline tokens wrapped at `avail`. Returns the
// widest line. `key` makes spoiler reveal state stable per message.
static float ChatLayoutTokens(const std::vector<ChatTok>& toks, float avail, bool draw, std::size_t key) {
    static std::set<std::size_t> revealed;   // clicked-open spoilers
    const float spaceW = ImGui::CalcTextSize(" ").x, em = ImGui::GetTextLineHeight();
    ImDrawList* dl = draw ? ImGui::GetWindowDrawList() : nullptr;
    const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
    auto monoW = [&](const std::string& t) {
        if (g_fontMono) ImGui::PushFont(g_fontMono, 0.0f);
        float w = ImGui::CalcTextSize(t.c_str()).x;
        if (g_fontMono) ImGui::PopFont();
        return w;
    };
    float lineX = 0.0f, maxLine = 0.0f;
    bool lineStarted = false;
    for (std::size_t ti = 0; ti < toks.size(); ++ti) {
        const ChatTok& t = toks[ti];
        if (t.newline) {
            maxLine = std::max(maxLine, lineX);
            if (!lineStarted && draw) ImGui::Dummy(ImVec2(0, em));   // empty line
            lineStarted = false; lineX = 0.0f;
            continue;
        }
        const bool code = (t.style & kMdCode) != 0;
        const int eid = code ? -1 : EmojiId(t.text);
        float tw = eid >= 0 ? em : code ? monoW(t.text) + 8.0f : ImGui::CalcTextSize(t.text.c_str()).x;
        if (!code && eid < 0) { if (t.style & kMdBold) tw += 1.0f; if (t.style & kMdItalic) tw += 2.0f; }
        float gap = (lineStarted && !t.glue) ? spaceW : 0.0f;
        if (lineStarted && lineX + gap + tw > avail) { maxLine = std::max(maxLine, lineX); lineStarted = false; lineX = 0.0f; gap = 0.0f; }
        if (draw) {
            if (lineStarted) ImGui::SameLine(0, gap);
            ImVec2 p = ImGui::GetCursorScreenPos();
            const bool link = !code && (t.text.rfind("http://", 0) == 0 || t.text.rfind("https://", 0) == 0);
            const bool mention = !code && t.text.size() > 1 && t.text[0] == '@';
            const bool spoiler = (t.style & kMdSpoiler) != 0;
            const std::size_t sk = key ^ (ti * 0x9E3779B97F4A7C15ull);
            if (spoiler && !revealed.count(sk)) {   // hidden: a solid pill until clicked
                ImGui::PushID((int)ti);
                if (ImGui::InvisibleButton("##spoiler", ImVec2(tw, em))) revealed.insert(sk);
                const bool h = ImGui::IsItemHovered();
                ImGui::PopID();
                dl->AddRectFilled(p, ImVec2(p.x + tw, p.y + em), h ? IM_COL32(92, 100, 122, 255) : IM_COL32(70, 76, 94, 255), 4.0f);
                if (h) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("Spoiler: click to reveal"); }
            } else if (eid >= 0) {
                DrawEmojiToken(dl, ImVec2(p.x + em * 0.5f, p.y + em * 0.5f), em * 1.12f, t.text);
                ImGui::Dummy(ImVec2(em, em));
                if (eid == kEmojiCustom && ImGui::IsItemHovered())
                    ImGui::SetTooltip(":%s:", t.text.substr(2, t.text.find(':', 2) - 2).c_str());
            } else if (link) {
                const ImU32 linkCol = ImGui::GetColorU32(kAccent);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(linkCol), "%s", t.text.c_str());
                ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                dl->AddLine(ImVec2(mn.x, mx.y - 1), ImVec2(mx.x, mx.y - 1), linkCol, 1.0f);
                if (ImGui::IsItemHovered()) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", t.text.c_str()); }
                if (ImGui::IsItemClicked()) OpenExternal(t.text);
            } else {
                ImU32 col = mention ? IM_COL32(140, 199, 255, 255) : textCol;
                if (code) {
                    dl->AddRectFilled(ImVec2(p.x, p.y + 1), ImVec2(p.x + tw, p.y + em - 1), IM_COL32(0, 0, 0, 70), 4.0f);
                    dl->AddRect(ImVec2(p.x, p.y + 1), ImVec2(p.x + tw, p.y + em - 1), IM_COL32(255, 255, 255, 22), 4.0f);
                    dl->AddText(g_fontMono ? g_fontMono : ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + 4, p.y), IM_COL32(242, 196, 150, 255), t.text.c_str());
                } else {
                    const int v0 = dl->VtxBuffer.Size;
                    dl->AddText(p, col, t.text.c_str());
                    if ((t.style & kMdBold) || mention) dl->AddText(ImVec2(p.x + 0.7f, p.y), col, t.text.c_str());   // faux bold
                    if (t.style & kMdItalic) {   // shear the glyph quads around the baseline = real italics
                        const float base = p.y + em * 0.8f;
                        for (int v = v0; v < dl->VtxBuffer.Size; ++v) dl->VtxBuffer[v].pos.x += (base - dl->VtxBuffer[v].pos.y) * 0.20f;
                    }
                    if (t.style & kMdStrike) dl->AddLine(ImVec2(p.x, p.y + em * 0.56f), ImVec2(p.x + tw, p.y + em * 0.56f), col, 1.3f);
                }
                ImGui::Dummy(ImVec2(tw, em));
            }
        }
        lineX += gap + tw; lineStarted = true;
    }
    return std::max(maxLine, lineX);
}
// A ``` block: monospace lines in a dark rounded box (long lines are clipped).
static float ChatCodeBlock(const std::string& code, float avail, bool draw) {
    if (g_fontMono) ImGui::PushFont(g_fontMono, 0.0f);
    float w = 0.0f; int lines = 1;
    { std::size_t p = 0; while (true) { std::size_t q = code.find('\n', p); std::string ln = code.substr(p, q == std::string::npos ? std::string::npos : q - p);
        w = std::max(w, ImGui::CalcTextSize(ln.c_str()).x); if (q == std::string::npos) break; ++lines; p = q + 1; } }
    const float lh = ImGui::GetTextLineHeight(), bw = std::min(avail, w + 20.0f), bh = lines * lh + 14.0f;
    if (draw) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + bh), IM_COL32(0, 0, 0, 80), 8.0f);
        dl->AddRect(p, ImVec2(p.x + bw, p.y + bh), IM_COL32(255, 255, 255, 20), 8.0f);
        dl->PushClipRect(ImVec2(p.x + 6, p.y), ImVec2(p.x + bw - 6, p.y + bh), true);
        std::size_t s0 = 0; float y = p.y + 7;
        while (true) {
            std::size_t q = code.find('\n', s0);
            std::string ln = code.substr(s0, q == std::string::npos ? std::string::npos : q - s0);
            dl->AddText(ImVec2(p.x + 10, y), IM_COL32(222, 228, 240, 255), ln.c_str());
            y += lh;
            if (q == std::string::npos) break;
            s0 = q + 1;
        }
        dl->PopClipRect();
        ImGui::Dummy(ImVec2(bw, bh));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Right-click to copy the code");
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { ImGui::SetClipboardText(code.c_str()); Toast("Code copied", 1); }
    }
    if (g_fontMono) ImGui::PopFont();
    return bw;
}

// ---- Link previews ----------------------------------------------------------
// The first web-page link in a message gets a card (site, title, description,
// thumbnail) like Messenger/Telegram. Pages are fetched once in the background
// (curl, 8 s cap, first 256 KB) and their Open Graph / <title> tags parsed.
struct LinkPreview { int state = 0; std::string site, title, desc, image; };   // 0 loading, 1 ready, 2 nothing to show
static SDL_mutex* g_lpMutex = nullptr;
static std::map<std::string, LinkPreview> g_linkPrev;   // (guarded) by URL
static std::vector<std::string> g_lpQueue;              // (guarded)
static bool g_lpWorkerRunning = false;                  // (guarded)
static SDL_Texture* ChatImageTexture(const std::string& url, int* w, int* h);

// First http(s) link in the text that isn't an image/GIF (those render inline).
static std::string FirstPageLink(const std::string& body) {
    std::size_t i = 0;
    while ((i = body.find("http", i)) != std::string::npos) {
        if (body.compare(i, 7, "http://") != 0 && body.compare(i, 8, "https://") != 0) { i += 4; continue; }
        if (i > 0 && body[i - 1] == ':') { i += 4; continue; }   // inside a custom emoji "<:name:url>"
        std::size_t j = i;
        while (j < body.size() && !std::isspace((unsigned char)body[j]) && body[j] != '<' && body[j] != '>') ++j;
        std::string u = body.substr(i, j - i);
        while (!u.empty() && std::strchr(".,!?:;)'\"*_~|`", u.back())) u.pop_back();
        std::string low = u; for (auto& c : low) c = (char)std::tolower((unsigned char)c);
        const std::size_t q = low.find_first_of("?#"); if (q != std::string::npos) low.erase(q);
        bool media = false;
        for (const char* ext : {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp", ".wav", ".mp3", ".ogg", ".zip", ".pdf", ".exe"})
            if (low.size() > std::strlen(ext) && low.compare(low.size() - std::strlen(ext), std::strlen(ext), ext) == 0) media = true;
        if (!media && low.find("/storage/v1/object/") == std::string::npos && u.size() > 10) return u;
        i = j;
    }
    return std::string();
}
static std::string HtmlDecode(const std::string& in) {
    std::string out; out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') { out += (in[i] == '\n' || in[i] == '\r' || in[i] == '\t') ? ' ' : in[i]; continue; }
        const std::size_t semi = in.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { out += '&'; continue; }
        const std::string e = in.substr(i + 1, semi - i - 1);
        unsigned long cp = 0;
        if (e == "amp") cp = '&'; else if (e == "lt") cp = '<'; else if (e == "gt") cp = '>';
        else if (e == "quot") cp = '"'; else if (e == "apos") cp = '\''; else if (e == "nbsp") cp = ' ';
        else if (e.size() > 1 && e[0] == '#') cp = (e[1] == 'x' || e[1] == 'X') ? std::strtoul(e.c_str() + 2, nullptr, 16) : std::strtoul(e.c_str() + 1, nullptr, 10);
        if (!cp || cp > 0x10FFFF) { out += '&'; continue; }
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        i = semi;
    }
    std::string t; bool sp = false;   // collapse runs of spaces, trim
    for (char c : out) { if (c == ' ') { if (!sp && !t.empty()) t += ' '; sp = true; } else { t += c; sp = false; } }
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t;
}
// Value of attribute `name` inside one tag's text (tag is lower-cased copy `low`).
static std::string HtmlAttr(const std::string& tag, const std::string& low, const char* name) {
    std::size_t at = 0;
    const std::string key = std::string(name) + "=";
    while ((at = low.find(key, at)) != std::string::npos) {
        if (at > 0 && !std::isspace((unsigned char)low[at - 1])) { at += key.size(); continue; }
        std::size_t v = at + key.size();
        if (v >= tag.size()) return std::string();
        const char qc = tag[v];
        if (qc == '"' || qc == '\'') { const std::size_t e = tag.find(qc, v + 1); return e == std::string::npos ? std::string() : tag.substr(v + 1, e - v - 1); }
        std::size_t e = v; while (e < tag.size() && !std::isspace((unsigned char)tag[e]) && tag[e] != '>') ++e;
        return tag.substr(v, e - v);
    }
    return std::string();
}
static std::string Utf8Clip(std::string s, std::size_t n) {
    if (s.size() <= n) return s;
    std::size_t cut = n; while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) --cut;
    s.erase(cut); while (!s.empty() && s.back() == ' ') s.pop_back();
    return s + "\xE2\x80\xA6";
}
static LinkPreview ParseLinkPreview(const std::string& url, const std::string& html) {
    LinkPreview lp; lp.state = 2;
    std::string low = html; for (auto& c : low) c = (char)std::tolower((unsigned char)c);
    std::map<std::string, std::string> meta;
    for (std::size_t at = 0; (at = low.find("<meta", at)) != std::string::npos; ) {
        const std::size_t end = low.find('>', at); if (end == std::string::npos) break;
        const std::string tag = html.substr(at, end - at), ltag = low.substr(at, end - at);
        std::string k = HtmlAttr(tag, ltag, "property"); if (k.empty()) k = HtmlAttr(tag, ltag, "name");
        for (auto& c : k) c = (char)std::tolower((unsigned char)c);
        if (!k.empty() && !meta.count(k)) meta[k] = HtmlAttr(tag, ltag, "content");
        at = end;
    }
    auto pick = [&](std::initializer_list<const char*> keys) { for (const char* k : keys) { auto it = meta.find(k); if (it != meta.end() && !it->second.empty()) return HtmlDecode(it->second); } return std::string(); };
    lp.title = pick({"og:title", "twitter:title"});
    if (lp.title.empty()) {
        const std::size_t a = low.find("<title"), b = a == std::string::npos ? a : low.find('>', a), c = b == std::string::npos ? b : low.find("</title", b);
        if (c != std::string::npos) lp.title = HtmlDecode(html.substr(b + 1, c - b - 1));
    }
    lp.desc = pick({"og:description", "twitter:description", "description"});
    lp.site = pick({"og:site_name"});
    std::string img = pick({"og:image", "og:image:url", "twitter:image", "twitter:image:src"});
    // Origin of the page for relative image paths, and the bare host as a fallback site name.
    const std::size_t hs = url.find("://") + 3, he = url.find_first_of("/?#", hs);
    const std::string origin = url.substr(0, he), host = url.substr(hs, he == std::string::npos ? std::string::npos : he - hs);
    if (img.rfind("//", 0) == 0) img = "https:" + img;
    else if (!img.empty() && img[0] == '/') img = origin + img;
    if (ShellSafeUrl(img)) lp.image = img;
    if (lp.site.empty()) lp.site = host.rfind("www.", 0) == 0 ? host.substr(4) : host;
    lp.title = Utf8Clip(lp.title, 110); lp.desc = Utf8Clip(lp.desc, 180); lp.site = Utf8Clip(lp.site, 40);
    if (!lp.title.empty()) lp.state = 1;
    return lp;
}
static int LinkPreviewWorker(void*) {
    while (true) {
        std::string url;
        SDL_LockMutex(g_lpMutex);
        if (g_lpQueue.empty()) { g_lpWorkerRunning = false; SDL_UnlockMutex(g_lpMutex); break; }
        url = g_lpQueue.front(); g_lpQueue.erase(g_lpQueue.begin());
        SDL_UnlockMutex(g_lpMutex);
        LinkPreview lp; lp.state = 2;
        if (ShellSafeUrl(url)) {
            std::error_code ec;
            const fs::path tmp = fs::temp_directory_path(ec) / ("okay_lp_" + std::to_string(std::hash<std::string>{}(url)) + ".html");
            fs::remove(tmp, ec);
            std::string cmd = "curl -L -s -f -m 8 --max-filesize 3000000 -r 0-262143 "
                              "-A \"Mozilla/5.0 (compatible; OkaySpace link preview)\" -o \"" + tmp.string() + "\" \"" + url + "\"";
#if !defined(_WIN32)
            cmd += " 2>/dev/null";
#endif
            RunCmd(cmd, true);
            std::ifstream f(tmp, std::ios::binary);
            if (f) {
                std::string html((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                if (html.size() > 262144) html.resize(262144);
                lp = ParseLinkPreview(url, html);
            }
            f.close(); fs::remove(tmp, ec);
        }
        SDL_LockMutex(g_lpMutex); g_linkPrev[url] = lp; SDL_UnlockMutex(g_lpMutex);
    }
    return 0;
}
// The preview for `url` once it's ready (nullptr while loading / when there's none).
static const LinkPreview* LinkPreviewFor(const std::string& url) {
    if (!g_linkPreviews || url.empty()) return nullptr;
    if (!g_lpMutex) g_lpMutex = SDL_CreateMutex();
    static std::map<std::string, LinkPreview> ready;   // UI-thread copy (stable pointers while drawing)
    if (auto it = ready.find(url); it != ready.end()) return it->second.state == 1 ? &it->second : nullptr;
    SDL_LockMutex(g_lpMutex);
    auto it = g_linkPrev.find(url);
    if (it == g_linkPrev.end()) {
        g_linkPrev[url] = LinkPreview{};
        g_lpQueue.push_back(url);
        const bool start = !g_lpWorkerRunning;
        if (start) g_lpWorkerRunning = true;
        SDL_UnlockMutex(g_lpMutex);
        if (start) {
            if (SDL_Thread* t = SDL_CreateThread(LinkPreviewWorker, "okay-linkprev", nullptr)) SDL_DetachThread(t);
            else { SDL_LockMutex(g_lpMutex); g_lpWorkerRunning = false; SDL_UnlockMutex(g_lpMutex); }
        }
        return nullptr;
    }
    const LinkPreview lp = it->second;
    SDL_UnlockMutex(g_lpMutex);
    if (lp.state == 0) return nullptr;
    auto& r = ready[url] = lp;
    return r.state == 1 ? &r : nullptr;
}
static constexpr float kLinkCardMaxW = 320.0f;
// The card: accent bar, site, bold title, two lines of description, thumbnail on top.
static void DrawLinkPreviewCard(const std::string& url, const LinkPreview& lp, float avail) {
    const float w = std::min(avail, kLinkCardMaxW), pad = 10.0f;
    ImGui::Dummy(ImVec2(0, 4));
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGui::PushID(url.c_str());
    float y = p.y + pad;
    int iw = 0, ih = 0;
    SDL_Texture* tex = lp.image.empty() ? nullptr : ChatImageTexture(lp.image, &iw, &ih);
    float imgH = 0.0f;
    if (tex && iw > 0 && ih > 0) imgH = std::min(140.0f, (w - pad * 2 - 6) * (float)ih / (float)iw);
    // Measure text first so the background can be drawn behind it.
    const float tw = w - pad * 2 - 6;
    ImFont* f = ImGui::GetFont(); const float fs = ImGui::GetFontSize();
    const ImVec2 siteSz = f->CalcTextSizeA(fs * 0.85f, FLT_MAX, tw, lp.site.c_str());
    const ImVec2 titleSz = f->CalcTextSizeA(fs, FLT_MAX, tw, lp.title.c_str());
    std::string desc = lp.desc;
    ImVec2 descSz = desc.empty() ? ImVec2(0, 0) : f->CalcTextSizeA(fs * 0.9f, FLT_MAX, tw, desc.c_str());
    const float maxDescH = fs * 0.9f * 2.6f;   // about two lines
    if (descSz.y > maxDescH) descSz.y = maxDescH;
    const float h = pad + (imgH > 0 ? imgH + 8 : 0) + siteSz.y + 2 + titleSz.y + (desc.empty() ? 0 : 3 + descSz.y) + pad;
    const bool hov = ImGui::IsMouseHoveringRect(p, ImVec2(p.x + w, p.y + h));
    d->AddRectFilled(p, ImVec2(p.x + w, p.y + h), hov ? IM_COL32(0, 0, 0, 70) : IM_COL32(0, 0, 0, 50), 8.0f);
    d->AddRectFilled(p, ImVec2(p.x + 3, p.y + h), ImGui::GetColorU32(kAccent), 8.0f, ImDrawFlags_RoundCornersLeft);
    const float x = p.x + pad + 4;
    if (imgH > 0) {
        d->AddImageRounded((ImTextureID)(intptr_t)tex, ImVec2(x, y), ImVec2(x + tw, y + imgH), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.0f);
        y += imgH + 8;
    }
    d->AddText(f, fs * 0.85f, ImVec2(x, y), IM_COL32(170, 182, 205, 255), lp.site.c_str(), nullptr, tw); y += siteSz.y + 2;
    d->AddText(f, fs, ImVec2(x, y), IM_COL32(150, 196, 255, 255), lp.title.c_str(), nullptr, tw);
    d->AddText(f, fs, ImVec2(x + 0.6f, y), IM_COL32(150, 196, 255, 255), lp.title.c_str(), nullptr, tw);   // faux bold
    y += titleSz.y;
    if (!desc.empty()) {
        y += 3;
        d->PushClipRect(ImVec2(x, y), ImVec2(x + tw, y + descSz.y), true);
        d->AddText(f, fs * 0.9f, ImVec2(x, y), IM_COL32(205, 212, 226, 220), desc.c_str(), nullptr, tw);
        d->PopClipRect();
    }
    ImGui::SetCursorScreenPos(p);
    if (ImGui::InvisibleButton("##lpcard", ImVec2(w, h))) OpenExternal(url);
    if (ImGui::IsItemHovered()) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", url.c_str()); }
    ImGui::PopID();
}

// Render a chat message body with word-wrapping and formatting (see above).
// maxW > 0 constrains wrapping to that pixel width (e.g. a chat bubble); 0 uses
// the remaining content width.
static void RenderChatBodyText(const std::string& bodyIn, float maxW);
void RenderChatBody(const std::string& bodyIn, float maxW = 0.0f) {
    RenderChatBodyText(bodyIn, maxW);
    const std::string link = g_linkPreviews ? FirstPageLink(bodyIn) : std::string();
    if (const LinkPreview* lp = LinkPreviewFor(link))
        DrawLinkPreviewCard(link, *lp, maxW > 0.0f ? maxW : ImGui::GetContentRegionAvail().x);
}
static void RenderChatBodyText(const std::string& bodyIn, float maxW) {
    const std::string body = NormalizeEmoji(bodyIn);
    if (ChatBodyIsPlain(body)) {
        if (maxW > 0.0f) ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + maxW);
        ImGui::TextWrapped("%s", body.c_str());
        if (maxW > 0.0f) ImGui::PopTextWrapPos();
        return;
    }
    const float avail = maxW > 0.0f ? maxW : ImGui::GetContentRegionAvail().x;
    const std::size_t key = std::hash<std::string>{}(body);
    const auto blocks = ChatBlocks(body);
    for (std::size_t bi = 0; bi < blocks.size(); ++bi) {
        const ChatBlock& b = blocks[bi];
        ImGui::PushID((int)bi);
        if (b.kind == 2) ChatCodeBlock(b.text, avail, true);
        else if (b.kind == 1) {   // quote: dim text with an accent bar
            ImVec2 q0 = ImGui::GetCursorScreenPos();
            ImGui::Indent(12.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ChatLayoutTokens(b.toks, avail - 12.0f, true, key + bi);
            ImGui::PopStyleColor();
            ImGui::Unindent(12.0f);
            float y1 = ImGui::GetItemRectMax().y;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(q0.x + 1, q0.y), ImVec2(q0.x + 4, y1), ImGui::GetColorU32(kAccent), 1.5f);
        } else ChatLayoutTokens(b.toks, avail, true, key + bi);
        ImGui::PopID();
    }
}

// The pixel WIDTH RenderChatBody will use for `body` wrapped at maxW — the width
// of its longest wrapped line (<= maxW). Same tokenizer, so bubbles hug exactly.
static float MeasureChatBodyText(const std::string& bodyIn, float maxW);
float MeasureChatBodyWidth(const std::string& bodyIn, float maxW) {
    float w = MeasureChatBodyText(bodyIn, maxW);
    if (g_linkPreviews && LinkPreviewFor(FirstPageLink(bodyIn))) w = std::max(w, std::min(maxW, kLinkCardMaxW));
    return w;
}
static float MeasureChatBodyText(const std::string& bodyIn, float maxW) {
    const std::string body = NormalizeEmoji(bodyIn);
    if (ChatBodyIsPlain(body)) {
        float w = ImGui::CalcTextSize(body.c_str(), nullptr, false, maxW).x;
        return w < maxW ? w : maxW;
    }
    float w = 0.0f;
    for (const ChatBlock& b : ChatBlocks(body)) {
        if (b.kind == 2) w = std::max(w, ChatCodeBlock(b.text, maxW, false));
        else if (b.kind == 1) w = std::max(w, 12.0f + ChatLayoutTokens(b.toks, maxW - 12.0f, false, 0));
        else w = std::max(w, ChatLayoutTokens(b.toks, maxW, false, 0));
    }
    return w < maxW ? w : maxW;
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

// ===========================================================================
// Chat media: colour emoji, animated GIFs, custom emoji + stickers.
//
// - Emoji: the full Unicode set rendered from the Twemoji sprite sheet + catalog
//   of the emoji-datasource project (MIT; Twemoji art CC-BY 4.0 by Twitter/X and
//   contributors). Both are downloaded once and cached; until they arrive (or if
//   offline) the drawn fallback glyphs are used.
// - GIFs: any .gif URL animates (frames decoded once, cached as textures). Search
//   goes through Tenor when a Tenor API key is set in the GIF tab.
// - Custom emoji / stickers: your own images in Storage (Messages::*Library);
//   messages carry the URL ("<:name:url>", "[Sticker] url").
// ===========================================================================
void SavePrefs();                                              // defined with the prefs below
static void DrawStar(ImDrawList* dl, ImVec2 c, float r, ImU32 col);   // defined with the chat helpers
static const char* kEmojiJsonUrl  = "https://cdn.jsdelivr.net/npm/emoji-datasource@15.1.2/emoji.json";
static const char* kEmojiSheetUrl = "https://cdn.jsdelivr.net/npm/emoji-datasource-twitter@15.1.2/img/twitter/sheets/32.png";
static constexpr int kEmojiCell = 32, kEmojiStride = 34;   // sheet cells have 1px padding

static fs::path MediaCacheDir() {
    std::error_code ec;
    fs::path d = fs::temp_directory_path(ec) / "okayspace_media";
    fs::create_directories(d, ec);
    return d;
}
static fs::path MediaCachePath(const std::string& url, const char* ext) {
    return MediaCacheDir() / (std::to_string(std::hash<std::string>{}(url)) + ext);
}
// Background download into `path` (shares the avatar downloader's bookkeeping).
// 1 = on disk, 0 = downloading, -1 = failed.
static int CachedFetch(const std::string& url, const fs::path& path) {
    if (!g_avatarMutex) g_avatarMutex = SDL_CreateMutex();
    SDL_LockMutex(g_avatarMutex);
    const bool pending = g_avatarPending.count(url) != 0, failed = g_avatarFailed.count(url) != 0;
    SDL_UnlockMutex(g_avatarMutex);
    if (pending) return 0;
    std::error_code ec;
    if (fs::exists(path, ec) && fs::file_size(path, ec) > 0) return 1;
    if (failed) return -1;
    SDL_LockMutex(g_avatarMutex); g_avatarPending.insert(url); SDL_UnlockMutex(g_avatarMutex);
    AvatarDl* d = new AvatarDl{url, path.string()};
    if (SDL_Thread* t = SDL_CreateThread(AvatarDlWorker, "okay-mediadl", d)) SDL_DetachThread(t);
    else { SDL_LockMutex(g_avatarMutex); g_avatarPending.erase(url); SDL_UnlockMutex(g_avatarMutex); delete d; return -1; }
    return 0;
}

// ---- Emoji catalog + sheet ----------------------------------------------------
struct EmojiInfo { std::string unified, utf8, name, shortName; int cat = 0, sx = 0, sy = 0, sort = 0; };
static const char* kEmojiCatNames[] = {"Smileys & Emotion", "People & Body", "Animals & Nature", "Food & Drink",
                                       "Activities", "Travel & Places", "Objects", "Symbols", "Flags"};
static constexpr int kEmojiCatCount = 9;
static std::vector<EmojiInfo> g_emojiAll;                        // picker order (category, then sort_order)
static std::map<std::string, std::pair<int, int>> g_emojiCells;  // "1F44D-1F3FB" -> sheet cell (incl. skin tones)
static SDL_Texture* g_emojiSheet = nullptr;
static int g_emojiSheetW = 0, g_emojiSheetH = 0;
static int g_emojiState = 0;   // 0 idle, 1 downloading/parsing, 2 ready, -1 unavailable
static SDL_mutex* g_emojiMutex = nullptr;
static bool g_emojiParsed = false;   // (guarded) worker finished
static unsigned char* g_emojiPixels = nullptr; static int g_emojiPxW = 0, g_emojiPxH = 0;   // (guarded)
static std::vector<EmojiInfo> g_emojiParsedList;                  // (guarded)
static std::map<std::string, std::pair<int, int>> g_emojiParsedCells;

static int EmojiParseWorker(void*) {
    std::vector<EmojiInfo> list; std::map<std::string, std::pair<int, int>> cells;
    unsigned char* px = nullptr; int w = 0, h = 0, ch = 0;
    try {
        std::ifstream f(MediaCachePath(kEmojiJsonUrl, ".json"), std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        const std::string json = ss.str();
        auto utf8Of = [](const std::string& unified) {
            std::string o; std::size_t p = 0;
            while (p < unified.size()) {
                std::size_t q = unified.find('-', p); if (q == std::string::npos) q = unified.size();
                o += Utf8Encode((unsigned)std::strtoul(unified.substr(p, q - p).c_str(), nullptr, 16));
                p = q + 1;
            }
            return o;
        };
        for (const std::string& o : okay::Messages::SplitObjects(json)) {
            const std::size_t sk = o.find("\"skin_variations\"");
            const std::string top = sk == std::string::npos ? o : o.substr(0, sk);
            if (okay::Messages::Field(top, "has_img_twitter") == "false") continue;
            EmojiInfo e;
            e.unified = okay::Messages::Field(top, "unified");
            if (e.unified.empty()) continue;
            e.sx = std::atoi(okay::Messages::Field(top, "sheet_x").c_str());
            e.sy = std::atoi(okay::Messages::Field(top, "sheet_y").c_str());
            cells[e.unified] = {e.sx, e.sy};
            std::string nq = okay::Messages::Field(top, "non_qualified");
            if (!nq.empty()) cells[nq] = {e.sx, e.sy};
            if (sk != std::string::npos) {   // skin-tone variants: "unified":"1F44D-1F3FB", "sheet_x":..,"sheet_y":..
                std::size_t p = sk;
                while ((p = o.find("\"unified\"", p + 1)) != std::string::npos) {
                    std::string sub = o.substr(p, 400);
                    std::string u = okay::Messages::Field(sub, "unified");
                    if (!u.empty()) cells[u] = {std::atoi(okay::Messages::Field(sub, "sheet_x").c_str()),
                                                std::atoi(okay::Messages::Field(sub, "sheet_y").c_str())};
                }
            }
            std::string cat = okay::Messages::Field(top, "category");
            e.cat = -1;
            for (int k = 0; k < kEmojiCatCount; ++k) if (cat == kEmojiCatNames[k]) e.cat = k;
            if (e.cat < 0 || !okay::Messages::Field(top, "obsoleted_by").empty()) continue;   // "Component" etc.
            e.utf8 = utf8Of(e.unified);
            e.name = okay::Messages::Field(top, "name");
            for (auto& c : e.name) c = (char)std::tolower((unsigned char)c);
            e.shortName = okay::Messages::Field(top, "short_name");
            e.sort = std::atoi(okay::Messages::Field(top, "sort_order").c_str());
            list.push_back(e);
        }
        std::stable_sort(list.begin(), list.end(), [](const EmojiInfo& a, const EmojiInfo& b) {
            return a.cat != b.cat ? a.cat < b.cat : a.sort < b.sort;
        });
        px = stbi_load(MediaCachePath(kEmojiSheetUrl, ".png").string().c_str(), &w, &h, &ch, 4);
    } catch (...) {}
    SDL_LockMutex(g_emojiMutex);
    g_emojiParsedList.swap(list); g_emojiParsedCells.swap(cells);
    g_emojiPixels = px; g_emojiPxW = w; g_emojiPxH = h; g_emojiParsed = true;
    SDL_UnlockMutex(g_emojiMutex);
    return 0;
}
// Advance loading (call wherever emoji are drawn; cheap once ready).
static void EmojiCatalogPump() {
    if (g_emojiState == 2 || g_emojiState == -1) return;
    if (!g_emojiMutex) g_emojiMutex = SDL_CreateMutex();
    if (g_emojiState == 0) {
        int a = CachedFetch(kEmojiJsonUrl, MediaCachePath(kEmojiJsonUrl, ".json"));
        int b = CachedFetch(kEmojiSheetUrl, MediaCachePath(kEmojiSheetUrl, ".png"));
        if (a < 0 || b < 0) { g_emojiState = -1; return; }
        if (a == 0 || b == 0) return;   // still downloading
        g_emojiState = 1;
        if (SDL_Thread* t = SDL_CreateThread(EmojiParseWorker, "okay-emoji", nullptr)) SDL_DetachThread(t);
        else g_emojiState = -1;
        return;
    }
    SDL_LockMutex(g_emojiMutex);
    const bool done = g_emojiParsed;
    SDL_UnlockMutex(g_emojiMutex);
    if (!done) return;
    if (g_emojiPixels && g_renderer && !g_emojiParsedCells.empty()) {
        SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormatFrom(g_emojiPixels, g_emojiPxW, g_emojiPxH, 32, g_emojiPxW * 4, SDL_PIXELFORMAT_ABGR8888);
        if (sf) { g_emojiSheet = SDL_CreateTextureFromSurface(g_renderer, sf); SDL_FreeSurface(sf); }
        g_emojiSheetW = g_emojiPxW; g_emojiSheetH = g_emojiPxH;
    }
    if (g_emojiPixels) { stbi_image_free(g_emojiPixels); g_emojiPixels = nullptr; }
    g_emojiAll.swap(g_emojiParsedList); g_emojiCells.swap(g_emojiParsedCells);
    g_emojiState = g_emojiSheet ? 2 : -1;
    if (g_emojiState < 0) {   // corrupt/partial cache: drop it so the next launch re-downloads
        std::error_code ec;
        fs::remove(MediaCachePath(kEmojiJsonUrl, ".json"), ec);
        fs::remove(MediaCachePath(kEmojiSheetUrl, ".png"), ec);
    }
}
// Draw sheet cell (sx,sy) into the square [c-s/2, c+s/2].
static void DrawEmojiCell(ImDrawList* dl, ImVec2 c, float s, int sx, int sy) {
    const float u0 = (float)(sx * kEmojiStride + 1) / g_emojiSheetW, v0 = (float)(sy * kEmojiStride + 1) / g_emojiSheetH;
    const float u1 = u0 + (float)kEmojiCell / g_emojiSheetW, v1 = v0 + (float)kEmojiCell / g_emojiSheetH;
    dl->AddImage((ImTextureID)(intptr_t)g_emojiSheet, ImVec2(c.x - s * 0.5f, c.y - s * 0.5f), ImVec2(c.x + s * 0.5f, c.y + s * 0.5f),
                 ImVec2(u0, v0), ImVec2(u1, v1));
}
// Look up a "1F44D-1F3FB"-style code (tries with and without FE0F).
static bool EmojiCellFor(const std::string& code, int& sx, int& sy) {
    auto it = g_emojiCells.find(code);
    if (it == g_emojiCells.end()) {
        std::string stripped;
        std::size_t p = 0;
        while (p < code.size()) {
            std::size_t q = code.find('-', p); if (q == std::string::npos) q = code.size();
            std::string part = code.substr(p, q - p);
            if (part != "FE0F") { if (!stripped.empty()) stripped += '-'; stripped += part; }
            p = q + 1;
        }
        it = g_emojiCells.find(stripped);
        if (it == g_emojiCells.end()) {   // drop a skin tone as a last resort
            std::size_t t = stripped.find("-1F3F");
            if (t != std::string::npos) it = g_emojiCells.find(stripped.substr(0, t));
        }
    }
    if (it == g_emojiCells.end()) return false;
    sx = it->second.first; sy = it->second.second;
    return true;
}

// ---- Animated / still chat images ---------------------------------------------
struct AnimImage { std::vector<SDL_Texture*> frames; std::vector<int> ends; int total = 0, w = 0, h = 0; bool ok = false; };
static std::map<std::string, AnimImage> g_animImages;
static bool IsGifUrl(const std::string& url) {
    std::string low = url; for (auto& c : low) c = (char)std::tolower((unsigned char)c);
    std::size_t q = low.find('?'); if (q != std::string::npos) low.erase(q);
    return low.size() > 4 && low.compare(low.size() - 4, 4, ".gif") == 0;
}
// The texture to draw for an image URL right now (GIFs advance with time);
// nullptr while it downloads. w/h receive the image size.
static SDL_Texture* ChatImageTexture(const std::string& url, int* w = nullptr, int* h = nullptr) {
    if (url.empty() || !g_renderer) return nullptr;
    if (!IsGifUrl(url)) {
        SDL_Texture* t = AvatarTexture(url);
        if (t && (w || h)) { int tw = 0, th = 0; SDL_QueryTexture(t, nullptr, nullptr, &tw, &th); if (w) *w = tw; if (h) *h = th; }
        return t;
    }
    auto it = g_animImages.find(url);
    if (it == g_animImages.end()) {
        fs::path p = MediaCachePath(url, ".gif");
        int st = CachedFetch(url, p);
        if (st == 0) return nullptr;
        AnimImage& a = g_animImages[url];
        if (st > 0) {
            std::ifstream f(p, std::ios::binary);
            std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            int* delays = nullptr; int fw = 0, fh = 0, frames = 0, comp = 0;
            unsigned char* px = data.empty() ? nullptr :
                stbi_load_gif_from_memory((const stbi_uc*)data.data(), (int)data.size(), &delays, &fw, &fh, &frames, &comp, 4);
            if (px && fw > 0 && fh > 0 && frames > 0) {
                // Keep memory sane: skip frames on huge GIFs (merging their delays).
                const long long budget = 24LL * 1000 * 1000;   // pixels across all frames
                int step = 1;
                while ((long long)fw * fh * ((frames + step - 1) / step) > budget && step < frames) ++step;
                int acc = 0;
                for (int i = 0; i < frames; ++i) {
                    int d = delays ? delays[i] : 100;
                    if (d < 20) d = 100;   // browsers treat 0-10ms frames as 100ms
                    acc += d;
                    if (i % step != 0 && i != frames - 1) continue;
                    SDL_Surface* sf = SDL_CreateRGBSurfaceWithFormatFrom(px + (std::size_t)i * fw * fh * 4, fw, fh, 32, fw * 4, SDL_PIXELFORMAT_ABGR8888);
                    if (!sf) continue;
                    SDL_Texture* tx = SDL_CreateTextureFromSurface(g_renderer, sf);
                    SDL_FreeSurface(sf);
                    if (!tx) continue;
                    a.frames.push_back(tx);
                    a.total += acc; a.ends.push_back(a.total); acc = 0;
                }
                a.w = fw; a.h = fh; a.ok = !a.frames.empty();
            }
            if (px) stbi_image_free(px);
            if (delays) stbi_image_free(delays);
        }
        it = g_animImages.find(url);
    }
    const AnimImage& a = it->second;
    if (!a.ok) return nullptr;
    if (w) *w = a.w;
    if (h) *h = a.h;
    if (a.frames.size() == 1 || a.total <= 0) return a.frames[0];
    int t = (int)(SDL_GetTicks() % (Uint32)a.total);
    for (std::size_t i = 0; i < a.ends.size(); ++i) if (t < a.ends[i]) return a.frames[i];
    return a.frames.back();
}

static void DrawEmojiToken(ImDrawList* dl, ImVec2 c, float s, const std::string& tok) {
    const int id = EmojiId(tok);
    if (id == kEmojiUnicode) {
        EmojiCatalogPump();
        const std::string code = tok.substr(3, tok.size() - 4);
        int sx = 0, sy = 0;
        if (g_emojiState == 2 && EmojiCellFor(code, sx, sy)) { DrawEmojiCell(dl, c, s, sx, sy); return; }
        // Not loaded (or unknown): a few common ones have drawn stand-ins.
        static const struct { const char* code; int id; } kFallback[] = {
            {"1F600", 1}, {"1F603", 1}, {"1F604", 1}, {"1F601", 1}, {"1F642", 0}, {"1F60A", 0}, {"263A-FE0F", 0},
            {"1F622", 2}, {"1F61E", 2}, {"1F641", 2}, {"1F62D", 2}, {"2764-FE0F", 3}, {"2764", 3}, {"1F525", 4},
            {"2B50", 5}, {"1F31F", 5}, {"1F602", 6}, {"1F923", 6}, {"1F606", 6}, {"1F62E", 7}, {"1F632", 7},
            {"1F60E", 8}, {"1F44D", 9},
        };
        for (const auto& f : kFallback) if (code == f.code) { DrawEmoji(dl, c, s, f.id); return; }
        DrawEmoji(dl, c, s, kEmojiUnknown);
        return;
    }
    if (id == kEmojiCustom) {
        std::size_t c2 = tok.find(':', 2);
        std::string url = tok.substr(c2 + 1, tok.size() - c2 - 2);
        int iw = 0, ih = 0;
        if (SDL_Texture* t = ChatImageTexture(url, &iw, &ih)) {
            float k = (iw > 0 && ih > 0) ? std::min(s / iw, s / ih) : 1.0f;
            float hw = iw * k * 0.5f, hh = ih * k * 0.5f;
            dl->AddImage((ImTextureID)(intptr_t)t, ImVec2(c.x - hw, c.y - hh), ImVec2(c.x + hw, c.y + hh));
        } else DrawEmoji(dl, c, s, kEmojiUnknown);
        return;
    }
    DrawEmoji(dl, c, s, id < 0 ? 0 : id);
}

// ---- Custom emoji / sticker library (filled by workers defined later) ----------
static SDL_mutex* g_libMutex = nullptr;
static std::vector<okay::Messages::LibraryItem> g_libEmoji, g_libStickers;   // (guarded)
static bool g_libLoaded = false, g_libBusy = false;                         // (guarded / UI)
static std::string g_libStatus;                                              // (guarded) last upload result
static void LibraryRefresh();                                                // defined below the chat poller
static void LibraryUpload(const std::string& path, const std::string& folder, const std::string& name);
static void LibraryDelete(const std::string& folder, const std::string& file);
// Replace ":name:" with my custom emoji token before sending ("<:name:url>").
static std::string ExpandCustomEmoji(const std::string& text) {
    if (text.find(':') == std::string::npos) return text;
    std::vector<okay::Messages::LibraryItem> lib;
    if (g_libMutex) { SDL_LockMutex(g_libMutex); lib = g_libEmoji; SDL_UnlockMutex(g_libMutex); }
    if (lib.empty()) return text;
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ':') {
            std::size_t j = text.find(':', i + 1);
            if (j != std::string::npos && j > i + 1 && j - i < 40) {
                std::string nm = text.substr(i + 1, j - i - 1);
                bool hit = false;
                for (const auto& e : lib) if (e.name == nm) { out += " <:" + nm + ":" + e.url + "> "; hit = true; break; }
                if (hit) { i = j + 1; continue; }
            }
        }
        out += text[i++];
    }
    return out;
}

// ---- GIF search (Tenor) -----------------------------------------------------------
std::string g_tenorKey;            // persisted (tenor_key=); empty = search off
struct GifResult { std::string preview, full, desc; int w = 0, h = 0; };
static SDL_mutex* g_gifMutex = nullptr;
static std::vector<GifResult> g_gifResults;   // (guarded)
static bool g_gifBusy = false, g_gifReady = false;
static std::string g_gifError;                // (guarded)
static void GifSearch(const std::string& query);   // "" = trending; defined below
static std::vector<std::string> g_recentEmoji;     // most recent first (session)

// ---- The picker -------------------------------------------------------------------
// Emoji tab (search, categories, custom), GIFs tab, Stickers tab. Emoji are
// inserted into `buf`; a GIF or sticker returns a message body to send now.
static std::string ChatMediaPicker(const char* id, char* buf, std::size_t cap,
                                   const std::vector<std::string>* recentGifs, bool canSendMedia) {
    std::string sendNow;
    ImGui::PushID(id);
    ImVec2 bp = ImGui::GetCursorScreenPos();
    const float B = ImGui::GetFrameHeight();
    if (ImGui::InvisibleButton("##mediabtn", ImVec2(B, B))) {
        ImGui::OpenPopup("##mediapop");
        if (canSendMedia) LibraryRefresh();
    }
    const bool bh = ImGui::IsItemHovered();
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (bh) dl->AddRectFilled(bp, ImVec2(bp.x + B, bp.y + B), IM_COL32(255, 255, 255, 22), 8.0f);
        EmojiCatalogPump();
        DrawEmojiToken(dl, ImVec2(bp.x + B * 0.5f, bp.y + B * 0.5f), B * 0.70f, "<e:1F642>");
        if (bh) ImGui::SetTooltip(canSendMedia ? "Emoji, GIFs and stickers" : "Emoji");
    }
    static int tab = 0, cat = 0;               // tab: 0 emoji, 1 GIFs, 2 stickers; cat: -2 recent, -1 custom, 0.. categories
    static char search[64] = "", gifQuery[64] = "", keyBuf[96] = "";
    static std::string hoverName;
    static std::string pendingPath, pendingFolder; static char pendingName[40] = "";
    if (static bool demoOpen = false; !demoOpen && canSendMedia && std::getenv("OKAY_CHAT_DEMO_PICKER")) {   // dev screenshot hook
        demoOpen = true; ImGui::OpenPopup("##mediapop");
        std::string v = std::getenv("OKAY_CHAT_DEMO_PICKER");
        tab = v == "stickers" ? 2 : v == "gifs" ? 1 : 0; cat = v == "custom" ? -1 : 0;
    }
    ImGui::SetNextWindowSize(ImVec2(392, 440));
    if (ImGui::BeginPopup("##mediapop")) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 cText = ImGui::GetColorU32(ImGuiCol_Text), cDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImVec4 accLt(kAccent.x * 0.55f + 0.45f, kAccent.y * 0.55f + 0.45f, kAccent.z * 0.55f + 0.45f, 1.0f);
        if (!canSendMedia) tab = 0;
        // Tabs.
        if (canSendMedia) {
            const char* tabs[] = {"Emoji", "GIFs", "Stickers"};
            ImVec2 tp = ImGui::GetCursorScreenPos();
            float x = tp.x, th = ImGui::GetTextLineHeight() + 10;
            for (int k = 0; k < 3; ++k) {
                ImVec2 ts = ImGui::CalcTextSize(tabs[k]);
                float tw = ts.x + 24;
                ImGui::SetCursorScreenPos(ImVec2(x, tp.y));
                ImGui::PushID(k);
                if (ImGui::InvisibleButton("##mt", ImVec2(tw, th))) {
                    tab = k;
                    if (k == 1 && !g_tenorKey.empty() && !g_gifReady && !g_gifBusy) GifSearch("");
                }
                bool h = ImGui::IsItemHovered();
                ImGui::PopID();
                bool on = tab == k;
                dl->AddRectFilled(ImVec2(x, tp.y), ImVec2(x + tw, tp.y + th),
                                  on ? ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.24f)) : (h ? IM_COL32(255, 255, 255, 20) : IM_COL32(255, 255, 255, 8)), th * 0.5f);
                dl->AddText(ImVec2(x + 12, tp.y + (th - ts.y) * 0.5f), on ? ImGui::GetColorU32(accLt) : cDim, tabs[k]);
                x += tw + 6;
            }
            ImGui::SetCursorScreenPos(ImVec2(tp.x, tp.y + th + 8));
            ImGui::Dummy(ImVec2(0, 0));
        }
        auto searchPill = [&](const char* sid, const char* hint, char* sbuf, std::size_t scap) {
            ImVec2 sp = ImGui::GetCursorScreenPos();
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(28, 6));
            ImGui::SetNextItemWidth(-1);
            bool enter = ImGui::InputTextWithHint(sid, hint, sbuf, scap, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::PopStyleVar(2);
            float fh = ImGui::GetItemRectSize().y;
            ImVec2 mc(sp.x + 14.0f, sp.y + fh * 0.5f - 1.0f);
            dl->AddCircle(mc, 4.4f, cDim, 16, 1.5f);
            dl->AddLine(ImVec2(mc.x + 3.2f, mc.y + 3.2f), ImVec2(mc.x + 6.4f, mc.y + 6.4f), cDim, 1.7f);
            return enter;
        };
        // Pending upload (custom emoji / sticker): name it, then upload.
        auto pendingRow = [&]() {
            if (pendingPath.empty()) return;
            ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kRaised);
            ImGui::BeginChild("##pending", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
            ImGui::TextDisabled("New %s from %s", pendingFolder == "emoji" ? "emoji" : "sticker", fs::path(pendingPath).filename().string().c_str());
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150);
            ImGui::InputTextWithHint("##pname", "name", pendingName, sizeof(pendingName));
            ImGui::SameLine();
            ImGui::BeginDisabled(!pendingName[0] || g_libBusy);
            if (PrimaryButton("Upload", ImVec2(70, 0))) { LibraryUpload(pendingPath, pendingFolder, pendingName); pendingPath.clear(); }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) pendingPath.clear();
            ImGui::EndChild();
            ImGui::PopStyleColor();
        };
        auto pickFile = [&](const char* folder, unsigned long long maxBytes) {
            const char* pats[] = {"*.png", "*.gif", "*.jpg", "*.jpeg"};
            const char* pick = tinyfd_openFileDialog(folder[0] == 'e' ? "Choose an image for your emoji" : "Choose a sticker image",
                                                     "", 4, pats, "Images (PNG, GIF, JPG)", 0);
            if (!pick || !*pick) return;
            std::error_code ec; auto sz = fs::file_size(fs::path(pick), ec);
            if (ec) { Toast("Couldn't read that file", 2); return; }
            if ((unsigned long long)sz > maxBytes) { Toast(folder[0] == 'e' ? "Emoji images are limited to 512 KB" : "Stickers are limited to 2 MB", 2); return; }
            pendingPath = pick; pendingFolder = folder;
            std::string stem = fs::path(pick).stem().string(), nm;
            for (char c : stem) if (std::isalnum((unsigned char)c) || c == '_' || c == '-') nm += (char)std::tolower((unsigned char)c);
            std::snprintf(pendingName, sizeof(pendingName), "%s", nm.substr(0, 32).c_str());
        };
        std::vector<okay::Messages::LibraryItem> libEmoji, libStickers; bool libLoaded = false; std::string libStatus;
        if (g_libMutex) {
            SDL_LockMutex(g_libMutex);
            libEmoji = g_libEmoji; libStickers = g_libStickers; libLoaded = g_libLoaded; libStatus = g_libStatus;
            SDL_UnlockMutex(g_libMutex);
        }

        if (tab == 0) {   // ---------------- Emoji ----------------
            searchPill("##esearch", "Search emoji", search, sizeof(search));
            EmojiCatalogPump();
            const bool ready = g_emojiState == 2;
            // Category row: recent, custom, then the Unicode categories (an emoji each as the icon).
            static const char* kCatIcon[] = {"1F600", "1F44B", "1F43B", "1F354", "26BD", "2708-FE0F", "1F4A1", "2764-FE0F", "1F3C1"};
            if (!search[0]) {
                ImVec2 cp = ImGui::GetCursorScreenPos();
                const float cs = 30.0f;
                float x = cp.x;
                auto catBtn = [&](int c, const char* tip) {
                    ImGui::SetCursorScreenPos(ImVec2(x, cp.y));
                    ImGui::PushID(c + 100);
                    if (ImGui::InvisibleButton("##cat", ImVec2(cs, cs))) cat = c;
                    bool h = ImGui::IsItemHovered();
                    ImGui::PopID();
                    if (cat == c || h) dl->AddRectFilled(ImVec2(x, cp.y), ImVec2(x + cs, cp.y + cs), cat == c ? IM_COL32(255, 255, 255, 30) : IM_COL32(255, 255, 255, 14), 7.0f);
                    ImVec2 cc(x + cs * 0.5f, cp.y + cs * 0.5f);
                    if (c == -2) {   // clock
                        dl->AddCircle(cc, 7.0f, cDim, 16, 1.5f);
                        dl->AddLine(cc, ImVec2(cc.x, cc.y - 4.5f), cDim, 1.5f); dl->AddLine(cc, ImVec2(cc.x + 3.5f, cc.y), cDim, 1.5f);
                    } else if (c == -1) DrawStar(dl, cc, 7.5f, cat == c ? IM_COL32(250, 200, 70, 255) : cDim);
                    else DrawEmojiToken(dl, cc, 20.0f, std::string("<e:") + kCatIcon[c] + ">");
                    if (h) ImGui::SetTooltip("%s", tip);
                    x += cs + 3;
                };
                if (canSendMedia) catBtn(-1, "Your custom emoji");
                catBtn(-2, "Recently used");
                for (int k = 0; k < kEmojiCatCount; ++k) catBtn(k, kEmojiCatNames[k]);
                ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y + cs + 6));
                ImGui::Dummy(ImVec2(0, 0));
            }
            pendingRow();
            ImGui::BeginChild("##egrid", ImVec2(0, -30), false);
            const float cell = 38.0f, es = 26.0f;
            const int perRow = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cell));
            hoverName.clear();
            auto insert = [&](const std::string& text) {
                std::size_t len = std::strlen(buf);
                std::snprintf(buf + len, cap - len, "%s", text.c_str());
            };
            int col = 0;
            auto emojiCell = [&](const std::string& tok, const std::string& name, const std::string& insertText) {
                if (col > 0) ImGui::SameLine(0, 0);
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::PushID(tok.c_str());
                bool click = ImGui::InvisibleButton("##ec", ImVec2(cell, cell));
                bool h = ImGui::IsItemHovered();
                ImGui::PopID();
                ImDrawList* gdl = ImGui::GetWindowDrawList();
                if (h) { gdl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), IM_COL32(255, 255, 255, 22), 8.0f); hoverName = name; }
                DrawEmojiToken(gdl, ImVec2(p.x + cell * 0.5f, p.y + cell * 0.5f), h ? es + 4 : es, tok);
                if (click) {
                    insert(insertText);
                    g_recentEmoji.erase(std::remove(g_recentEmoji.begin(), g_recentEmoji.end(), insertText), g_recentEmoji.end());
                    g_recentEmoji.insert(g_recentEmoji.begin(), insertText);
                    if (g_recentEmoji.size() > 27) g_recentEmoji.resize(27);
                }
                col = (col + 1) % perRow;
            };
            auto unicodeTok = [&](const EmojiInfo& e) { return "<e:" + e.unified + ">"; };
            if (!ready && g_emojiState != -1) {
                float a0 = (float)ImGui::GetTime() * 6.0f; ImVec2 c(ImGui::GetCursorScreenPos().x + 180, ImGui::GetCursorScreenPos().y + 60);
                ImGui::GetWindowDrawList()->PathArcTo(c, 12.0f, a0, a0 + 4.6f, 24);
                ImGui::GetWindowDrawList()->PathStroke(ImGui::GetColorU32(kAccent), 0, 2.5f);
                ImGui::Dummy(ImVec2(0, 84));
                ImGui::TextDisabled("     Loading the emoji set (first time only)\xE2\x80\xA6");
            }
            if (g_emojiState == -1) {   // offline: the drawn set still works
                ImGui::TextDisabled("Couldn't download the full emoji set. Basic emoji:");
                for (const auto& e : kEmojis) emojiCell(e.code, e.code, std::string(" ") + e.code + " ");
            }
            if (ready) {
                std::string q = search; for (auto& c : q) c = (char)std::tolower((unsigned char)c);
                if (!q.empty()) {
                    int shown = 0;
                    if (canSendMedia) for (const auto& ce : libEmoji)
                        if (ce.name.find(q) != std::string::npos) { emojiCell("<:" + ce.name + ":" + ce.url + ">", ":" + ce.name + ":", ":" + ce.name + ":"); ++shown; }
                    for (const auto& e : g_emojiAll)
                        if (e.name.find(q) != std::string::npos || e.shortName.find(q) != std::string::npos) {
                            emojiCell(unicodeTok(e), ":" + e.shortName + ":", e.utf8);
                            if (++shown >= 240) break;
                        }
                    if (shown == 0) ImGui::TextDisabled("No emoji match \"%s\".", search);
                } else if (cat == -2) {
                    if (g_recentEmoji.empty()) ImGui::TextDisabled("Emoji you use show up here.");
                    for (const auto& r : g_recentEmoji) {
                        std::string norm = NormalizeEmoji(r);
                        std::string tok = norm; while (!tok.empty() && tok.front() == ' ') tok.erase(0, 1); while (!tok.empty() && tok.back() == ' ') tok.pop_back();
                        if (r.size() > 2 && r.front() == ':' && r.back() == ':') {   // custom
                            std::string nm = r.substr(1, r.size() - 2);
                            for (const auto& ce : libEmoji) if (ce.name == nm) tok = "<:" + nm + ":" + ce.url + ">";
                        }
                        emojiCell(tok, r, r);
                    }
                } else if (cat == -1) {
                    if (!libLoaded) ImGui::TextDisabled("Loading your emoji\xE2\x80\xA6");
                    for (const auto& ce : libEmoji) {
                        emojiCell("<:" + ce.name + ":" + ce.url + ">", ":" + ce.name + ":", ":" + ce.name + ":");
                        if (ImGui::BeginPopupContextItem(("##ced" + ce.file).c_str())) {
                            if (ImGui::MenuItem("Delete emoji")) LibraryDelete("emoji", ce.file);
                            ImGui::EndPopup();
                        }
                    }
                    // "+" tile: add your own.
                    if (col > 0) ImGui::SameLine(0, 0);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    if (ImGui::InvisibleButton("##addemoji", ImVec2(cell, cell))) pickFile("emoji", 512ULL * 1024ULL);
                    bool h = ImGui::IsItemHovered();
                    ImDrawList* gdl = ImGui::GetWindowDrawList();
                    gdl->AddRect(ImVec2(p.x + 4, p.y + 4), ImVec2(p.x + cell - 4, p.y + cell - 4), h ? cText : cDim, 8.0f, 0, 1.4f);
                    ImVec2 c(p.x + cell * 0.5f, p.y + cell * 0.5f);
                    gdl->AddLine(ImVec2(c.x - 6, c.y), ImVec2(c.x + 6, c.y), h ? cText : cDim, 1.6f);
                    gdl->AddLine(ImVec2(c.x, c.y - 6), ImVec2(c.x, c.y + 6), h ? cText : cDim, 1.6f);
                    if (h) { hoverName = "Add a custom emoji (PNG/GIF, up to 512 KB)"; ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
                    if (libLoaded && libEmoji.empty()) { ImGui::NewLine(); ImGui::TextDisabled("Upload an image to make your own emoji."); ImGui::TextDisabled("Use it as :name: in any chat."); }
                } else {
                    int c0 = std::clamp(cat, 0, kEmojiCatCount - 1);
                    ImGui::SetWindowFontScale(0.8f); ImGui::TextDisabled("%s", kEmojiCatNames[c0]); ImGui::SetWindowFontScale(1.0f);
                    std::vector<const EmojiInfo*> items;
                    for (const auto& e : g_emojiAll) if (e.cat == c0) items.push_back(&e);
                    ImGuiListClipper clip;
                    const int rows = ((int)items.size() + perRow - 1) / perRow;
                    clip.Begin(rows, cell + ImGui::GetStyle().ItemSpacing.y);
                    while (clip.Step())
                        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                            col = 0;
                            for (int k = r * perRow; k < std::min((int)items.size(), (r + 1) * perRow); ++k)
                                emojiCell(unicodeTok(*items[k]), ":" + items[k]->shortName + ":", items[k]->utf8);
                        }
                }
            }
            ImGui::EndChild();
            // Footer: hovered emoji name.
            ImGui::TextDisabled("%s", hoverName.empty() ? (libStatus.empty() ? "Pick an emoji" : libStatus.c_str()) : hoverName.c_str());
        } else if (tab == 1) {   // ---------------- GIFs ----------------
            std::vector<GifResult> res; bool busy = g_gifBusy; std::string gerr;
            if (g_gifMutex) { SDL_LockMutex(g_gifMutex); res = g_gifResults; gerr = g_gifError; SDL_UnlockMutex(g_gifMutex); }
            if (!g_tenorKey.empty()) {
                if (searchPill("##gsearch", "Search GIFs (Enter)", gifQuery, sizeof(gifQuery))) GifSearch(gifQuery);
            }
            ImGui::BeginChild("##ggrid", ImVec2(0, -30), false);
            const float colW = (ImGui::GetContentRegionAvail().x - 8) * 0.5f;
            auto gifGrid = [&](const std::vector<GifResult>& items) {
                float y[2] = {0, 0};
                ImVec2 o = ImGui::GetCursorScreenPos();
                for (std::size_t i = 0; i < items.size(); ++i) {
                    const GifResult& g = items[i];
                    int c = y[0] <= y[1] ? 0 : 1;
                    int iw = g.w, ih = g.h;
                    SDL_Texture* t = ChatImageTexture(g.preview, iw ? nullptr : &iw, ih ? nullptr : &ih);
                    float h = (iw > 0 && ih > 0) ? colW * ih / iw : colW * 0.7f;
                    h = std::clamp(h, 50.0f, 220.0f);
                    ImVec2 p(o.x + c * (colW + 8), o.y + y[c]);
                    ImGui::SetCursorScreenPos(p);
                    ImGui::PushID((int)i);
                    bool click = ImGui::InvisibleButton("##gif", ImVec2(colW, h));
                    bool hv = ImGui::IsItemHovered();
                    ImGui::PopID();
                    ImDrawList* gdl = ImGui::GetWindowDrawList();
                    gdl->AddRectFilled(p, ImVec2(p.x + colW, p.y + h), IM_COL32(255, 255, 255, 12), 8.0f);
                    if (t) gdl->AddImageRounded((ImTextureID)(intptr_t)t, p, ImVec2(p.x + colW, p.y + h), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8.0f);
                    if (hv) { gdl->AddRect(p, ImVec2(p.x + colW, p.y + h), ImGui::GetColorU32(kAccent), 8.0f, 0, 2.0f); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); if (!g.desc.empty()) ImGui::SetTooltip("%s", g.desc.c_str()); }
                    if (click) { sendNow = g.full.empty() ? g.preview : g.full; ImGui::CloseCurrentPopup(); }
                    y[c] += h + 8;
                }
                ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + std::max(y[0], y[1])));
                ImGui::Dummy(ImVec2(0, 0));
            };
            if (g_tenorKey.empty()) {
                // Recent GIFs from this conversation (no search without a key).
                std::vector<GifResult> recent;
                if (recentGifs) for (const auto& u : *recentGifs) { GifResult g; g.preview = g.full = u; recent.push_back(g); }
                if (!recent.empty()) { ImGui::SetWindowFontScale(0.8f); ImGui::TextDisabled("IN THIS CHAT"); ImGui::SetWindowFontScale(1.0f); gifGrid(recent); ImGui::Dummy(ImVec2(0, 6)); }
                ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kRaised);
                ImGui::BeginChild("##gifkey", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
                ImGui::TextUnformatted("Turn on GIF search");
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("GIF search uses Tenor. Paste a free Tenor API key (Google Cloud console, \"Tenor API\") to search millions of GIFs. You can always send your own GIF with the + attach button.");
                ImGui::PopTextWrapPos();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 74);
                ImGui::InputTextWithHint("##tenorkey", "Tenor API key", keyBuf, sizeof(keyBuf), ImGuiInputTextFlags_Password);
                ImGui::SameLine();
                ImGui::BeginDisabled(!keyBuf[0]);
                if (PrimaryButton("Save", ImVec2(66, 0))) { g_tenorKey = keyBuf; keyBuf[0] = '\0'; SavePrefs(); GifSearch(""); }
                ImGui::EndDisabled();
                if (ImGui::SmallButton("How to get a key")) OpenExternal("https://developers.google.com/tenor/guides/quickstart");
                ImGui::EndChild();
                ImGui::PopStyleColor();
            } else if (busy && res.empty()) {
                ImGui::TextDisabled("Searching\xE2\x80\xA6");
            } else if (!gerr.empty()) {
                ImGui::TextColored(ImVec4(0.95f, 0.5f, 0.5f, 1), "%s", gerr.c_str());
                if (ImGui::SmallButton("Remove key")) { g_tenorKey.clear(); SavePrefs(); }
            } else {
                gifGrid(res);
                if (res.empty() && !busy) ImGui::TextDisabled("No GIFs found.");
            }
            ImGui::EndChild();
            ImGui::TextDisabled(g_tenorKey.empty() ? "GIF search is off" : "Powered by Tenor");
        } else {   // ---------------- Stickers ----------------
            pendingRow();
            ImGui::BeginChild("##sgrid", ImVec2(0, -30), false);
            const float cell = 84.0f;
            const int perRow = std::max(1, (int)(ImGui::GetContentRegionAvail().x / cell));
            int col = 0;
            if (!libLoaded) ImGui::TextDisabled("Loading your stickers\xE2\x80\xA6");
            for (const auto& st : libStickers) {
                if (col > 0) ImGui::SameLine(0, 0);
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::PushID(st.file.c_str());
                bool click = ImGui::InvisibleButton("##st", ImVec2(cell, cell));
                bool h = ImGui::IsItemHovered();
                if (ImGui::BeginPopupContextItem("##stctx")) {
                    if (ImGui::MenuItem("Delete sticker")) LibraryDelete("stickers", st.file);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
                ImDrawList* gdl = ImGui::GetWindowDrawList();
                if (h) gdl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), IM_COL32(255, 255, 255, 18), 10.0f);
                int iw = 0, ih = 0;
                if (SDL_Texture* t = ChatImageTexture(st.url, &iw, &ih)) {
                    float s = cell - 12, k = (iw > 0 && ih > 0) ? std::min(s / iw, s / ih) : 1.0f;
                    ImVec2 c(p.x + cell * 0.5f, p.y + cell * 0.5f);
                    gdl->AddImage((ImTextureID)(intptr_t)t, ImVec2(c.x - iw * k * 0.5f, c.y - ih * k * 0.5f), ImVec2(c.x + iw * k * 0.5f, c.y + ih * k * 0.5f));
                }
                if (h) { hoverName = st.name; ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s  (right-click to delete)", st.name.c_str()); }
                if (click) { sendNow = "[Sticker] " + st.url; ImGui::CloseCurrentPopup(); }
                col = (col + 1) % perRow;
            }
            if (col > 0) ImGui::SameLine(0, 0);
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##addsticker", ImVec2(cell, cell))) pickFile("stickers", 2ULL * 1024ULL * 1024ULL);
            bool h = ImGui::IsItemHovered();
            ImDrawList* gdl = ImGui::GetWindowDrawList();
            gdl->AddRect(ImVec2(p.x + 6, p.y + 6), ImVec2(p.x + cell - 6, p.y + cell - 6), h ? cText : cDim, 10.0f, 0, 1.5f);
            ImVec2 c(p.x + cell * 0.5f, p.y + cell * 0.5f - 6);
            gdl->AddLine(ImVec2(c.x - 8, c.y), ImVec2(c.x + 8, c.y), h ? cText : cDim, 1.8f);
            gdl->AddLine(ImVec2(c.x, c.y - 8), ImVec2(c.x, c.y + 8), h ? cText : cDim, 1.8f);
            const char* lbl = "Add";
            ImVec2 ls = ImGui::CalcTextSize(lbl);
            gdl->AddText(ImVec2(p.x + (cell - ls.x) * 0.5f, p.y + cell - 26), h ? cText : cDim, lbl);
            if (h) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (libLoaded && libStickers.empty()) { ImGui::NewLine(); ImGui::TextDisabled("Make stickers from your own images (PNG or animated GIF)."); }
            ImGui::EndChild();
            ImGui::TextDisabled("%s", libStatus.empty() ? "Click a sticker to send it" : libStatus.c_str());
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return sendNow;
}

// ---- Sidebar icons ---------------------------------------------------------
// One consistent set of 1.6px line icons (Lucide-like), drawn with the draw list
// so no icon font is needed. Ids 0..10 match the launcher tabs; the rest are
// sidebar extras.
enum { kIcoBell = 11, kIcoChat = 12, kIcoPanel = 13, kIcoRefresh = 14 };
static void DrawNavIcon(ImDrawList* d, int id, ImVec2 c, ImU32 col) {
    const float r = 7.5f, t = 1.6f, PI = 3.14159265f;
    switch (id) {
    case 0:   // Create: plus in a rounded square
        d->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 3.5f, 0, t);
        d->AddLine(ImVec2(c.x - 3.6f, c.y), ImVec2(c.x + 3.6f, c.y), col, t);
        d->AddLine(ImVec2(c.x, c.y - 3.6f), ImVec2(c.x, c.y + 3.6f), col, t);
        break;
    case 1:   // Play: play button in a circle
        d->AddCircle(c, r, col, 28, t);
        d->AddTriangleFilled(ImVec2(c.x - 2.3f, c.y - 3.8f), ImVec2(c.x - 2.3f, c.y + 3.8f), ImVec2(c.x + 4.0f, c.y), col);
        break;
    case 2:   // Community: 2x2 grid
        for (int k = 0; k < 4; ++k) {
            float x = (k % 2) ? c.x + 1.2f : c.x - r, y = (k / 2) ? c.y + 1.2f : c.y - r;
            d->AddRect(ImVec2(x, y), ImVec2(x + r - 1.2f, y + r - 1.2f), col, 1.5f, 0, t);
        }
        break;
    case 3:   // Account: user
        d->AddCircle(ImVec2(c.x, c.y - 2.6f), 3.3f, col, 16, t);
        d->PathArcTo(ImVec2(c.x, c.y + 8.2f), 6.4f, PI + 0.32f, 2 * PI - 0.32f, 14);
        d->PathStroke(col, 0, t);
        break;
    case 4:   // Settings: gear
        d->AddCircle(c, 2.4f, col, 12, t);
        d->AddCircle(c, 5.4f, col, 20, t);
        for (int k = 0; k < 8; ++k) {
            float a = k * PI / 4;
            d->AddLine(ImVec2(c.x + std::cos(a) * 5.4f, c.y + std::sin(a) * 5.4f),
                       ImVec2(c.x + std::cos(a) * 7.8f, c.y + std::sin(a) * 7.8f), col, 2.2f);
        }
        break;
    case 5:   // Achievements: trophy
        d->PathLineTo(ImVec2(c.x - 4.6f, c.y - r));
        d->PathLineTo(ImVec2(c.x + 4.6f, c.y - r));
        d->PathArcTo(ImVec2(c.x, c.y - 2.6f), 4.6f, 0.0f, PI, 12);
        d->PathStroke(col, ImDrawFlags_Closed, t);
        d->PathArcTo(ImVec2(c.x - 4.6f, c.y - 4.6f), 2.4f, PI * 0.5f, PI * 1.5f, 8); d->PathStroke(col, 0, t);
        d->PathArcTo(ImVec2(c.x + 4.6f, c.y - 4.6f), 2.4f, -PI * 0.5f, PI * 0.5f, 8); d->PathStroke(col, 0, t);
        d->AddLine(ImVec2(c.x, c.y + 2.0f), ImVec2(c.x, c.y + 5.6f), col, t);
        d->AddLine(ImVec2(c.x - 3.8f, c.y + r - 0.6f), ImVec2(c.x + 3.8f, c.y + r - 0.6f), col, t);
        break;
    case 6:   // Friends: two users
        d->AddCircle(ImVec2(c.x - 2.4f, c.y - 2.8f), 3.0f, col, 16, t);
        d->PathArcTo(ImVec2(c.x - 2.4f, c.y + 7.8f), 5.8f, PI + 0.30f, 2 * PI - 0.30f, 12); d->PathStroke(col, 0, t);
        d->PathArcTo(ImVec2(c.x + 4.2f, c.y - 2.8f), 2.6f, -PI * 0.5f, PI * 0.5f, 8); d->PathStroke(col, 0, t);
        d->PathArcTo(ImVec2(c.x + 4.2f, c.y + 7.8f), 4.6f, PI * 1.55f, 2 * PI - 0.30f, 8); d->PathStroke(col, 0, t);
        break;
    case 7:   // Market: shopping bag
        d->AddRect(ImVec2(c.x - 6.2f, c.y - 3.0f), ImVec2(c.x + 6.2f, c.y + r), col, 2.0f, 0, t);
        d->PathArcTo(ImVec2(c.x, c.y - 3.0f), 3.2f, PI, 2 * PI, 10); d->PathStroke(col, 0, t);
        break;
    case 8:   // Servers: stacked server units
        for (int k = 0; k < 2; ++k) {
            float y0 = k ? c.y + 1.0f : c.y - r + 0.5f, y1 = k ? c.y + r - 0.5f : c.y - 1.0f;
            d->AddRect(ImVec2(c.x - r, y0), ImVec2(c.x + r, y1), col, 2.0f, 0, t);
            d->AddCircleFilled(ImVec2(c.x - 3.8f, (y0 + y1) * 0.5f), 1.2f, col, 8);
        }
        break;
    case 9:   // Forums: document with lines
        d->AddRect(ImVec2(c.x - 6.0f, c.y - r), ImVec2(c.x + 6.0f, c.y + r), col, 2.0f, 0, t);
        for (int k = 0; k < 3; ++k) {
            float y = c.y - 3.4f + k * 3.4f;
            d->AddLine(ImVec2(c.x - 3.2f, y), ImVec2(c.x + (k == 2 ? 1.0f : 3.2f), y), col, t);
        }
        break;
    case 10:  // Downloads: arrow into a tray
        d->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + 2.6f), col, t);
        d->AddLine(ImVec2(c.x - 3.6f, c.y - 1.0f), ImVec2(c.x, c.y + 2.6f), col, t);
        d->AddLine(ImVec2(c.x + 3.6f, c.y - 1.0f), ImVec2(c.x, c.y + 2.6f), col, t);
        d->PathLineTo(ImVec2(c.x - r, c.y + 2.2f)); d->PathLineTo(ImVec2(c.x - r, c.y + r));
        d->PathLineTo(ImVec2(c.x + r, c.y + r));    d->PathLineTo(ImVec2(c.x + r, c.y + 2.2f));
        d->PathStroke(col, 0, t);
        break;
    case kIcoBell:
        d->PathArcTo(ImVec2(c.x, c.y - 1.2f), 5.0f, PI, 2 * PI, 12);
        d->PathLineTo(ImVec2(c.x + 5.0f, c.y + 3.2f)); d->PathLineTo(ImVec2(c.x + 6.6f, c.y + 5.0f));
        d->PathLineTo(ImVec2(c.x - 6.6f, c.y + 5.0f)); d->PathLineTo(ImVec2(c.x - 5.0f, c.y + 3.2f));
        d->PathStroke(col, ImDrawFlags_Closed, t);
        d->AddLine(ImVec2(c.x - 1.8f, c.y + 7.4f), ImVec2(c.x + 1.8f, c.y + 7.4f), col, t);
        break;
    case kIcoChat:
        d->PathArcTo(c, 6.8f, PI * 0.80f, PI * 2.62f, 26);
        d->PathLineTo(ImVec2(c.x - r, c.y + r));
        d->PathStroke(col, ImDrawFlags_Closed, t);
        break;
    case kIcoPanel:   // sidebar toggle: a panel with its left column marked
        d->AddRect(ImVec2(c.x - r, c.y - 6.5f), ImVec2(c.x + r, c.y + 6.5f), col, 2.5f, 0, t);
        d->AddLine(ImVec2(c.x - 2.2f, c.y - 6.5f), ImVec2(c.x - 2.2f, c.y + 6.5f), col, t);
        break;
    case kIcoRefresh: {
        d->PathArcTo(c, 5.6f, -PI * 0.35f, PI * 1.45f, 20); d->PathStroke(col, 0, t);
        ImVec2 e(c.x + std::cos(-PI * 0.35f) * 5.6f, c.y + std::sin(-PI * 0.35f) * 5.6f);
        d->AddTriangleFilled(ImVec2(e.x + 2.6f, e.y - 2.4f), ImVec2(e.x + 2.0f, e.y + 2.6f), ImVec2(e.x - 2.6f, e.y - 0.6f), col);
        break;
    }
    }
}

// Round icon button (Discord/Messenger-style row actions).
enum { kRibChat, kRibPlay, kRibCheck, kRibClose, kRibMore };
static bool RoundIconButton(const char* id, int icon, float d, ImU32 bg, ImU32 bgHover, ImU32 fg, const char* tip) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool click = ImGui::InvisibleButton(id, ImVec2(d, d));
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + d * 0.5f, p.y + d * 0.5f);
    dl->AddCircleFilled(c, d * 0.5f, hov ? bgHover : bg, 28);
    switch (icon) {
    case kRibChat:  DrawNavIcon(dl, kIcoChat, c, fg); break;
    case kRibPlay:  dl->AddTriangleFilled(ImVec2(c.x - 3.5f, c.y - 5.5f), ImVec2(c.x - 3.5f, c.y + 5.5f), ImVec2(c.x + 5.5f, c.y), fg); break;
    case kRibCheck:
        dl->PathLineTo(ImVec2(c.x - 5.0f, c.y + 0.2f)); dl->PathLineTo(ImVec2(c.x - 1.5f, c.y + 4.0f));
        dl->PathLineTo(ImVec2(c.x + 5.5f, c.y - 4.5f)); dl->PathStroke(fg, 0, 2.2f);
        break;
    case kRibClose:
        dl->AddLine(ImVec2(c.x - 4.5f, c.y - 4.5f), ImVec2(c.x + 4.5f, c.y + 4.5f), fg, 2.0f);
        dl->AddLine(ImVec2(c.x + 4.5f, c.y - 4.5f), ImVec2(c.x - 4.5f, c.y + 4.5f), fg, 2.0f);
        break;
    case kRibMore:
        for (int k = -1; k <= 1; ++k) dl->AddCircleFilled(ImVec2(c.x + k * 5.0f, c.y), 1.8f, fg, 8);
        break;
    }
    if (hov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); if (tip && *tip) ImGui::SetTooltip("%s", tip); }
    return click;
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
        else if (k == "chat_sounds") g_chatSounds = (v != "0");
        else if (k == "link_previews") g_linkPreviews = (v != "0");
        else if (k == "voice_speed") g_voiceSpeed = std::clamp(std::atoi(v.c_str()), 0, 2);
        else if (k == "tenor_key") g_tenorKey = v;
        else if (k == "chat_list_folded") g_chatListFolded = (v == "1");
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
        else if (k == "block" && !v.empty()) g_blockedUsers.insert(v);
        else if (k == "chatcolor" && !v.empty()) {
            auto bar = v.find('|');
            if (bar != std::string::npos) { try { g_chatColor[v.substr(0, bar)] = std::stoi(v.substr(bar + 1)); } catch (...) {} }
        }
        else if (k == "star" && !v.empty()) {
            // star=<id>|<uid>|<when>|<preview>  (preview may itself contain '|')
            auto a = v.find('|');
            auto b = (a == std::string::npos) ? a : v.find('|', a + 1);
            auto c = (b == std::string::npos) ? b : v.find('|', b + 1);
            if (c != std::string::npos) {
                try {
                    StarredMsg s; long id = std::stol(v.substr(0, a));
                    s.uid = v.substr(a + 1, b - a - 1);
                    s.when = std::stol(v.substr(b + 1, c - b - 1));
                    s.preview = v.substr(c + 1);
                    if (id > 0) g_starred[id] = s;
                } catch (...) {}
            }
        }
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
    f << "chat_sounds=" << (g_chatSounds ? 1 : 0) << "\n";
    f << "voice_speed=" << g_voiceSpeed << "\n";
    f << "link_previews=" << (g_linkPreviews ? 1 : 0) << "\n";
    if (!g_tenorKey.empty()) f << "tenor_key=" << g_tenorKey << "\n";
    f << "chat_list_folded=" << (g_chatListFolded ? 1 : 0) << "\n";
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
    for (const auto& u : g_blockedUsers)  f << "block=" << u << "\n";
    for (const auto& kv : g_chatColor) if (kv.second > 0) f << "chatcolor=" << kv.first << "|" << kv.second << "\n";
    for (const auto& kv : g_starred)
        f << "star=" << kv.first << "|" << kv.second.uid << "|" << kv.second.when << "|" << kv.second.preview << "\n";
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

// ---- File attachment messages -----------------------------------------------
// A file message body is "[File] <name> (<size>) <url>" (okay::Messages::
// FileMessageBody). Older clients show it as text + a link; here it renders as
// a tappable file card.
struct ChatFileInfo { std::string name, size, url; };
static bool ParseChatFile(const std::string& body, ChatFileInfo& out) {
    if (body.rfind("[File] ", 0) != 0) return false;
    std::size_t us = body.find_last_of(' ');
    if (us == std::string::npos || us < 7) return false;
    out.url = body.substr(us + 1);
    if (out.url.rfind("http", 0) != 0) return false;
    std::string mid = body.substr(7, us - 7);               // "<name> (<size>)"
    std::size_t lp = mid.rfind(" (");
    if (lp != std::string::npos && !mid.empty() && mid.back() == ')') {
        out.name = mid.substr(0, lp);
        out.size = mid.substr(lp + 2, mid.size() - lp - 3);
    } else { out.name = mid; out.size.clear(); }
    return !out.name.empty();
}
static float ChatFileCardWidth(const ChatFileInfo& f, float maxW) {
    float w = std::max(ImGui::CalcTextSize(f.name.c_str()).x, ImGui::CalcTextSize(f.size.c_str()).x) + 50.0f;
    return std::min(std::max(w, 190.0f), maxW);
}
// Document icon + name + size; the whole card opens/downloads the file.
static void RenderChatFileCard(const ChatFileInfo& f, float width) {
    const float h = 44.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(f.url.c_str());
    bool clicked = ImGui::InvisibleButton("##filecard", ImVec2(width, h));
    bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, ImVec2(p.x + width, p.y + h), hov ? IM_COL32(255, 255, 255, 34) : IM_COL32(0, 0, 0, 46), 9.0f);
    // Page with a folded corner.
    ImVec2 ip(p.x + 8, p.y + 6); const float iw = 24, ih = 32, fold = 7;
    ImU32 pageC = IM_COL32(232, 236, 244, 255), foldC = IM_COL32(170, 180, 198, 255);
    d->AddRectFilled(ip, ImVec2(ip.x + iw - fold, ip.y + ih), pageC, 3.0f, ImDrawFlags_RoundCornersLeft);
    d->AddRectFilled(ImVec2(ip.x + iw - fold, ip.y + fold), ImVec2(ip.x + iw, ip.y + ih), pageC, 3.0f, ImDrawFlags_RoundCornersBottomRight);
    d->AddTriangleFilled(ImVec2(ip.x + iw - fold, ip.y), ImVec2(ip.x + iw, ip.y + fold), ImVec2(ip.x + iw - fold, ip.y + fold), foldC);
    std::string ext = fs::path(f.name).extension().string();
    if (!ext.empty()) ext.erase(0, 1);
    for (auto& c : ext) c = (char)std::toupper((unsigned char)c);
    if (ext.size() > 4) ext.resize(4);
    ImVec2 es = ImGui::CalcTextSize(ext.c_str());
    float sc = 0.62f;   // tiny extension label on the page
    d->AddText(nullptr, ImGui::GetFontSize() * sc, ImVec2(ip.x + (iw - es.x * sc) * 0.5f, ip.y + ih - es.y * sc - 4),
               IM_COL32(60, 70, 90, 255), ext.c_str());
    float tx = p.x + 42, avail = width - 50;
    std::string nm = f.name;
    while (nm.size() > 4 && ImGui::CalcTextSize(nm.c_str()).x > avail) nm = nm.substr(0, nm.size() - 4) + "\xE2\x80\xA6";
    d->AddText(ImVec2(tx, p.y + 5), IM_COL32(235, 238, 245, 255), nm.c_str());
    d->AddText(ImVec2(tx, p.y + 24), IM_COL32(190, 198, 215, 200), (f.size.empty() ? std::string("File") : f.size + "  \xC2\xB7  Open").c_str());
    if (hov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("Open / download\n%s", f.name.c_str()); }
    if (clicked) OpenExternal(f.url);
}
// Chat window palette (dark slate, Messenger-dark): the bubbles, header strip
// and composer are designed around it.
static const ImVec4 kChatWinBg   = okaytheme::kBase;
static const ImVec4 kChatSideBg  = okaytheme::kSunken;
static const ImVec4 kChatSideSel (0.170f, 0.178f, 0.204f, 1.0f);   // side bg + the selected-row wash

// Truncate to fit maxW pixels with an ellipsis, never splitting a UTF-8 sequence.
static std::string FitText(const std::string& s, float maxW) {
    if (maxW <= 0.0f) return std::string();
    if (ImGui::CalcTextSize(s.c_str()).x <= maxW) return s;
    const char* ell = "\xE2\x80\xA6";
    const float ew = ImGui::CalcTextSize(ell).x;
    std::size_t n = s.size();
    while (n > 0) {
        --n;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) --n;
        if (ImGui::CalcTextSize(s.c_str(), s.c_str() + n).x + ew <= maxW) break;
    }
    return s.substr(0, n) + ell;
}

// One line of text with emoji drawn inline (Unicode, custom, shortcodes),
// clipped to maxW with an ellipsis — for previews (AddText alone would show
// "?" boxes for emoji).
static void DrawRichLine(ImDrawList* dl, ImVec2 pos, ImU32 col, const std::string& text, float maxW) {
    const std::string t = NormalizeEmoji(text);
    const float em = ImGui::GetTextLineHeight(), spaceW = ImGui::CalcTextSize(" ").x;
    const float ellW = ImGui::CalcTextSize("\xE2\x80\xA6").x;
    float x = 0.0f;
    std::size_t i = 0, n = t.size();
    bool first = true;
    while (i < n) {
        while (i < n && (t[i] == ' ' || t[i] == '\n')) ++i;
        if (i >= n) break;
        std::size_t j = i; while (j < n && t[j] != ' ' && t[j] != '\n') ++j;
        std::string tok = t.substr(i, j - i); i = j;
        const bool emo = EmojiId(tok) >= 0;
        const float w = emo ? em : ImGui::CalcTextSize(tok.c_str()).x;
        const float gap = first ? 0.0f : spaceW;
        if (x + gap + w > maxW) {   // doesn't fit: squeeze what we can, then "…"
            if (!emo) {
                std::string part = FitText(tok, maxW - x - gap);
                dl->AddText(ImVec2(pos.x + x + gap, pos.y), col, part.c_str());
            } else if (x + ellW <= maxW) dl->AddText(ImVec2(pos.x + x + gap, pos.y), col, "\xE2\x80\xA6");
            return;
        }
        x += gap;
        if (emo) DrawEmojiToken(dl, ImVec2(pos.x + x + em * 0.5f, pos.y + em * 0.5f), em, tok);
        else dl->AddText(ImVec2(pos.x + x, pos.y), col, tok.c_str());
        x += w; first = false;
    }
}

// Poll message bodies (see Polls below).
struct ChatPollInfo { std::string question; std::vector<std::string> options; };
static bool ParseChatPoll(const std::string& body, ChatPollInfo& p) {
    if (body.rfind("[Poll] ", 0) != 0) return false;
    p = ChatPollInfo{};
    std::size_t nl = body.find('\n');
    p.question = body.substr(7, nl == std::string::npos ? std::string::npos : nl - 7);
    while (nl != std::string::npos && p.options.size() < 10) {
        const std::size_t next = body.find('\n', nl + 1);
        std::string line = body.substr(nl + 1, next == std::string::npos ? std::string::npos : next - nl - 1);
        if (line.rfind("- ", 0) == 0 && line.size() > 2) p.options.push_back(line.substr(2));
        nl = next;
    }
    return !p.question.empty() && p.options.size() >= 2;
}
static std::string PollMessageBody(const std::string& q, const std::vector<std::string>& opts) {
    std::string b = "[Poll] " + q;
    for (const auto& o : opts) if (!o.empty()) b += "\n- " + o;
    return b;
}
// Conversation-list preview: "Photo" / "File: name" instead of raw URLs.
static std::string ChatPreviewSmart(const std::string& body, std::size_t n) {
    ChatFileInfo fi;
    if (ParseChatFile(body, fi)) return ChatPreview("File: " + fi.name, n);
    if (body.rfind("[Voice] ", 0) == 0) return "Voice message";
    if (body.rfind("[Sticker] ", 0) == 0) return "Sticker";
    { ChatPollInfo pi; if (ParseChatPoll(body, pi)) return ChatPreview("Poll: " + pi.question, n); }
    std::string t = body; while (!t.empty() && (t.back() == ' ' || t.back() == '\n')) t.pop_back();
    if (!t.empty() && FirstImageUrl(t) == t) {
        std::string low = t; for (auto& c : low) c = (char)std::tolower((unsigned char)c);
        return low.find(".gif") != std::string::npos ? "GIF" : "Photo";
    }
    if (t.find("<:") != std::string::npos) {   // custom emoji -> ":name:"
        std::string o; std::size_t i = 0;
        while (i < t.size()) {
            std::size_t a = t.find("<:", i);
            if (a == std::string::npos) { o += t.substr(i); break; }
            std::size_t c2 = t.find(':', a + 2), e = t.find('>', a);
            if (c2 == std::string::npos || e == std::string::npos || c2 > e) { o += t.substr(i); break; }
            o += t.substr(i, a - i) + ":" + t.substr(a + 2, c2 - a - 2) + ":";
            i = e + 1;
        }
        return ChatPreview(o, n);
    }
    return ChatPreview(body, n);
}

// ---- Chat attachment upload (background) ---------------------------------------
// Uploading can take seconds, so it runs on a worker (own account service) that
// uploads to Storage and then sends the message; the composer shows progress.
static volatile bool g_attBusy = false;   // an upload is in flight
static std::string   g_attName;           // file being uploaded (composer status)
static SDL_mutex*    g_attMutex = nullptr;
static bool          g_attDone = false;   // (guarded) finished -> toast result
static std::string   g_attError;          // (guarded) "" on success
static void StartChatUpload(const std::string& path, const std::string& toUid, long toGroup,
                            int voiceSecs = -1);                                            // defined below
static void PickAndSendAttachment(const std::string& toUid, long toGroup);                   // defined below
static constexpr unsigned long long kMaxChatUpload = 25ULL * 1024ULL * 1024ULL;  // matches the bucket limit

// ---- Voice messages ------------------------------------------------------------
// A voice message body is "[Voice] m:ss <url>" — a 16 kHz mono WAV in the "chat"
// bucket (older clients show it as text + a link). Recording drains SDL's
// capture device each frame (queue mode, no callback thread); playback fetches
// the clip once in the background (same worker as avatars) and queues it on an
// output device.
struct ChatVoiceInfo { int secs = 0; std::string url; };
static bool ParseChatVoice(const std::string& body, ChatVoiceInfo& out) {
    if (body.rfind("[Voice] ", 0) != 0) return false;
    std::size_t sp = body.find(' ', 8);
    int m = 0, s = 0;
    if (sp == std::string::npos || std::sscanf(body.c_str() + 8, "%d:%d", &m, &s) != 2) return false;
    out.secs = m * 60 + s;
    out.url = body.substr(sp + 1);
    return out.url.rfind("http", 0) == 0;
}
static std::string VoiceClock(int secs) {
    char b[16]; std::snprintf(b, sizeof(b), "%d:%02d", secs / 60, secs % 60); return b;
}
static std::string VoiceMessageBody(int secs, const std::string& url) { return "[Voice] " + VoiceClock(secs) + " " + url; }

static bool EnsureAudio() {
    static int state = 0;   // 0 untried, 1 ok, -1 unavailable
    if (state == 0) state = SDL_InitSubSystem(SDL_INIT_AUDIO) == 0 ? 1 : -1;
    return state == 1;
}

// Microphone (Messenger-style: tap the mic, talk, then send or bin it).
static constexpr int kVoiceRate = 16000, kVoiceMaxSecs = 120, kVoiceLevelHop = kVoiceRate / 20;
static SDL_AudioDeviceID   g_recDev = 0;
static bool                g_recFake = false;     // dev demo: a recording without a device
static std::vector<Sint16> g_recPcm;
static std::vector<float>  g_recLevels;           // loudness per 50 ms (live meter)
static Uint32              g_recStart = 0;
static std::string         g_recToUid;            // which chat the recording is for
static long                g_recToGroup = 0;

static bool VoiceRecording() { return g_recDev != 0 || g_recFake; }
static bool VoiceRecordingFor(const std::string& uid, long gid) {
    return VoiceRecording() && g_recToUid == uid && g_recToGroup == gid;
}
static float VoiceRecSeconds() { return VoiceRecording() ? (SDL_GetTicks() - g_recStart) / 1000.0f : 0.0f; }
static bool StartVoiceRecord(const std::string& toUid, long toGroup) {
    if (VoiceRecording()) return false;
    if (g_attBusy) { Toast("An upload is already in progress", 2); return false; }
    if (!EnsureAudio()) { Toast("Audio isn't available on this system", 2); return false; }
    SDL_AudioSpec want{}, have{};
    want.freq = kVoiceRate; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024;
    g_recDev = SDL_OpenAudioDevice(nullptr, 1, &want, &have, 0);   // SDL converts to our format
    if (!g_recDev) { Toast("No microphone found", 2); return false; }
    g_recPcm.clear(); g_recLevels.clear();
    g_recToUid = toUid; g_recToGroup = toGroup; g_recStart = SDL_GetTicks();
    SDL_PauseAudioDevice(g_recDev, 0);
    return true;
}
static void PumpVoiceRecord() {   // once per frame: move captured audio into g_recPcm
    if (!g_recDev) return;
    Sint16 buf[2048];
    while (Uint32 got = SDL_DequeueAudio(g_recDev, buf, sizeof(buf)))
        g_recPcm.insert(g_recPcm.end(), buf, buf + got / sizeof(Sint16));
    while ((g_recLevels.size() + 1) * kVoiceLevelHop <= g_recPcm.size()) {
        const Sint16* w = g_recPcm.data() + g_recLevels.size() * kVoiceLevelHop;
        int peak = 0;
        for (int i = 0; i < kVoiceLevelHop; ++i) peak = std::max(peak, std::abs((int)w[i]));
        g_recLevels.push_back(std::min(1.0f, std::sqrt(peak / 32768.0f) * 1.3f));
    }
}
static bool WriteWav16(const std::string& path, const std::vector<Sint16>& pcm, int rate) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto u32 = [&](Uint32 v) { char b[4] = {(char)v, (char)(v >> 8), (char)(v >> 16), (char)(v >> 24)}; f.write(b, 4); };
    auto u16 = [&](Uint16 v) { char b[2] = {(char)v, (char)(v >> 8)}; f.write(b, 2); };
    Uint32 data = (Uint32)pcm.size() * 2;
    f.write("RIFF", 4); u32(36 + data); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    f.write("data", 4); u32(data);
    for (Sint16 s : pcm) u16((Uint16)s);
    return (bool)f;
}
// Stop recording; send=true writes the WAV and hands it to the upload worker.
static void FinishVoiceRecord(bool send) {
    if (g_recDev) { PumpVoiceRecord(); SDL_CloseAudioDevice(g_recDev); g_recDev = 0; }
    const bool fake = g_recFake; g_recFake = false;
    std::vector<Sint16> pcm; pcm.swap(g_recPcm); g_recLevels.clear();
    if (!send || fake) return;
    if (pcm.size() < (std::size_t)kVoiceRate / 2) { Toast("Too short to send — hold on a little longer", 2); return; }
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec) / "okayspace_voice";
    fs::create_directories(dir, ec);
    fs::path file = dir / ("voice-" + std::to_string(SDL_GetTicks()) + ".wav");
    if (!WriteWav16(file.string(), pcm, kVoiceRate)) { Toast("Couldn't save the recording", 2); return; }
    int secs = (int)((pcm.size() + kVoiceRate / 2) / kVoiceRate);
    StartChatUpload(file.string(), g_recToUid, g_recToGroup, std::max(1, secs));
}

// Playback: decoded clips by URL + one output device for the clip playing now.
static constexpr int kVoiceBars = 30;
struct VoiceClip {
    bool ok = false;                 // decoded (false = unplayable; don't retry)
    Uint8* pcm = nullptr; Uint32 len = 0;
    SDL_AudioSpec spec{};
    float secs = 0, bytesPerSec = 1;
    float bars[kVoiceBars] = {};
    std::map<int, std::vector<Uint8>> fast;   // time-stretched copies by speed index (built on first use)
};
static std::map<std::string, VoiceClip> g_voiceClips;
static SDL_AudioDeviceID g_playDev = 0;
static std::string       g_playUrl;          // clip loaded on g_playDev
static bool              g_playPaused = false;
static Uint32            g_playFrom = 0;     // byte offset the current queue started at
static Uint32            g_playLen = 0;      // length of the buffer playing (the clip, or a sped-up copy)
static const float kVoiceSpeeds[3] = {1.0f, 1.5f, 2.0f};
static const char* const kVoiceSpeedLabels[3] = {"1x", "1.5x", "2x"};

// Speed a voice note up without the chipmunk effect: WSOLA time-stretch
// (overlap-add of 30 ms Hann windows; each window is nudged within +-10 ms to
// the spot that best continues the previous one, so pitch is unchanged).
static std::vector<Sint16> VoiceTimeStretch(const Sint16* in, std::size_t n, int rate, float speed) {
    const int N = std::max(64, rate * 30 / 1000), hopS = N / 2, tol = rate / 100;
    const float hopA = hopS * speed;
    std::vector<float> win((std::size_t)N);
    for (int i = 0; i < N; ++i) win[(std::size_t)i] = 0.5f - 0.5f * std::cos(6.2831853f * i / N);
    const std::size_t outN = (std::size_t)(n / speed) + (std::size_t)N;
    std::vector<float> acc(outN, 0.0f), norm(outN, 0.0f);
    long prev = 0;
    for (long k = 0;; ++k) {
        const long nominal = (long)(k * hopA);
        if (nominal + N + tol >= (long)n) break;
        long pos = nominal;
        if (k > 0) {   // best match for the natural continuation of the previous window
            const long ref = prev + hopS;
            float best = -1e30f;
            for (long dlt = -tol; dlt <= tol; dlt += 2) {
                const long c = nominal + dlt;
                if (c < 0) continue;
                float sum = 0.0f;
                for (int i = 0; i < hopS; i += 4) sum += (float)in[c + i] * (float)in[ref + i];
                if (sum > best) { best = sum; pos = c; }
            }
        }
        const std::size_t o = (std::size_t)k * hopS;
        if (o + (std::size_t)N > outN) break;
        for (int i = 0; i < N; ++i) { acc[o + i] += in[pos + i] * win[(std::size_t)i]; norm[o + i] += win[(std::size_t)i]; }
        prev = pos;
    }
    std::size_t used = outN;
    while (used > 0 && norm[used - 1] < 1e-3f) --used;
    std::vector<Sint16> out(used);
    for (std::size_t i = 0; i < used; ++i)
        out[i] = (Sint16)std::clamp(norm[i] > 1e-3f ? acc[i] / norm[i] : 0.0f, -32768.0f, 32767.0f);
    return out;
}
static std::string       g_voiceWant;        // tapped play before the clip arrived

// Waveform placeholder until the real clip is decoded (stable per URL).
static float VoiceBarStub(const std::string& url, int i) {
    std::size_t h = std::hash<std::string>{}(url) ^ (std::size_t)(i * 2654435761u);
    h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15;
    return 0.25f + 0.6f * (float)(h % 1000) / 1000.0f;
}
static fs::path VoiceCachePath(const std::string& url) {
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec) / "okayspace_voice";
    fs::create_directories(dir, ec);
    return dir / (std::to_string(std::hash<std::string>{}(url)) + ".wav");
}
// The decoded clip, or nullptr while it downloads (kicked off once, in the background).
static VoiceClip* VoiceClipFor(const std::string& url) {
    auto it = g_voiceClips.find(url);
    if (it != g_voiceClips.end()) return &it->second;
    if (!g_avatarMutex) g_avatarMutex = SDL_CreateMutex();
    SDL_LockMutex(g_avatarMutex);
    const bool pending = g_avatarPending.count(url) != 0, failed = g_avatarFailed.count(url) != 0;
    SDL_UnlockMutex(g_avatarMutex);
    if (pending) return nullptr;
    fs::path cache = VoiceCachePath(url);
    std::error_code ec;
    if (failed || fs::exists(cache, ec)) {
        VoiceClip& c = g_voiceClips[url];
        if (!failed && SDL_LoadWAV(cache.string().c_str(), &c.spec, &c.pcm, &c.len)) {
            const int bps = SDL_AUDIO_BITSIZE(c.spec.format) / 8 * std::max<int>(1, c.spec.channels);
            c.bytesPerSec = (float)c.spec.freq * bps;
            c.secs = c.len / c.bytesPerSec;
            c.ok = c.len > 0;
            // Real waveform: peak per slice (16-bit clips; anything else keeps the stub).
            const Uint32 frames = bps ? c.len / bps : 0;
            const bool s16 = c.spec.format == AUDIO_S16LSB || c.spec.format == AUDIO_S16MSB;
            float top = 0.0001f;
            for (int b = 0; b < kVoiceBars; ++b) {
                float peak = 0;
                if (s16 && frames) {
                    Uint32 a = frames * b / kVoiceBars, e = frames * (b + 1) / kVoiceBars;
                    for (Uint32 f = a; f < e; ++f) {
                        Sint16 v; std::memcpy(&v, c.pcm + (std::size_t)f * bps, 2);
                        if (c.spec.format == AUDIO_S16MSB) v = (Sint16)SDL_Swap16((Uint16)v);
                        peak = std::max(peak, std::abs((float)v) / 32768.0f);
                    }
                } else peak = VoiceBarStub(url, b);
                c.bars[b] = peak; top = std::max(top, peak);
            }
            for (float& v : c.bars) v = 0.12f + 0.88f * std::sqrt(v / top);
        }
        return &c;
    }
    SDL_LockMutex(g_avatarMutex); g_avatarPending.insert(url); SDL_UnlockMutex(g_avatarMutex);
    AvatarDl* d = new AvatarDl{url, cache.string()};
    if (SDL_Thread* t = SDL_CreateThread(AvatarDlWorker, "okay-voicedl", d)) SDL_DetachThread(t);
    else { SDL_LockMutex(g_avatarMutex); g_avatarPending.erase(url); SDL_UnlockMutex(g_avatarMutex); delete d; }
    return nullptr;
}
static void StopVoice() {
    if (g_playDev) { SDL_CloseAudioDevice(g_playDev); g_playDev = 0; }
    g_playUrl.clear(); g_playPaused = false; g_playFrom = 0; g_playLen = 0;
}
// The samples to play at the chosen speed: the clip itself at 1x, otherwise a
// time-stretched copy (16-bit mono clips, i.e. our own recordings).
static const Uint8* VoiceBuffer(VoiceClip& c, Uint32& len) {
    const int si = std::clamp(g_voiceSpeed, 0, 2);
    const bool s16mono = c.spec.format == AUDIO_S16SYS && c.spec.channels == 1;
    if (si == 0 || !s16mono || c.len < 4) { len = c.len; return c.pcm; }
    auto it = c.fast.find(si);
    if (it == c.fast.end()) {
        auto st = VoiceTimeStretch(reinterpret_cast<const Sint16*>(c.pcm), c.len / 2, c.spec.freq, kVoiceSpeeds[si]);
        std::vector<Uint8> bytes(st.size() * 2);
        if (!st.empty()) std::memcpy(bytes.data(), st.data(), bytes.size());
        it = c.fast.emplace(si, std::move(bytes)).first;
    }
    if (it->second.empty()) { len = c.len; return c.pcm; }
    len = (Uint32)it->second.size();
    return it->second.data();
}
// Start (or restart) a clip at a fraction 0..1 of its length.
static void PlayVoiceAt(const std::string& url, VoiceClip& c, float frac) {
    if (!c.ok) { Toast("Couldn't play this voice message", 2); return; }
    if (!EnsureAudio()) { Toast("Audio isn't available on this system", 2); return; }
    if (g_playUrl != url || !g_playDev) {
        StopVoice();
        SDL_AudioSpec have{};
        g_playDev = SDL_OpenAudioDevice(nullptr, 0, &c.spec, &have, 0);
        if (!g_playDev) { Toast("No audio output device", 2); return; }
        g_playUrl = url;
    }
    SDL_ClearQueuedAudio(g_playDev);
    Uint32 len = 0;
    const Uint8* buf = VoiceBuffer(c, len);
    const Uint32 frame = SDL_AUDIO_BITSIZE(c.spec.format) / 8 * std::max<int>(1, c.spec.channels);
    Uint32 off = (Uint32)(std::clamp(frac, 0.0f, 1.0f) * len);
    off -= off % std::max<Uint32>(1, frame);
    if (off >= len) off = 0;
    g_playFrom = off; g_playLen = len;
    SDL_QueueAudio(g_playDev, buf + off, len - off);
    SDL_PauseAudioDevice(g_playDev, 0);
    g_playPaused = false;
}
static float VoicePlayFrac(const std::string& url, const VoiceClip& c) {   // -1 = not this clip
    if (!g_playDev || g_playUrl != url || !c.len) return -1.0f;
    const Uint32 len = g_playLen ? g_playLen : c.len;
    Uint32 left = SDL_GetQueuedAudioSize(g_playDev);
    return std::min(1.0f, (float)(len - std::min(left, len)) / len);
}
static void PumpVoicePlayback() {   // once per frame: finish clips, start "wanted" ones
    if (g_playDev && !g_playPaused && SDL_GetQueuedAudioSize(g_playDev) == 0) StopVoice();
    if (!g_voiceWant.empty()) {
        if (VoiceClip* c = VoiceClipFor(g_voiceWant)) { std::string u = g_voiceWant; g_voiceWant.clear(); PlayVoiceAt(u, *c, 0.0f); }
    }
}

static void DrawMicIcon(ImDrawList* d, ImVec2 c, float s, ImU32 col) {
    float w = s * 0.30f, top = c.y - s * 0.44f, h = s * 0.48f;
    d->AddRectFilled(ImVec2(c.x - w * 0.5f, top), ImVec2(c.x + w * 0.5f, top + h), col, w * 0.5f);
    d->PathArcTo(ImVec2(c.x, c.y - s * 0.10f), s * 0.27f, 0.0f, 3.14159265f, 16);
    d->PathStroke(col, 0, 1.7f);
    d->AddLine(ImVec2(c.x, c.y + s * 0.17f), ImVec2(c.x, c.y + s * 0.34f), col, 1.7f);
    d->AddLine(ImVec2(c.x - s * 0.15f, c.y + s * 0.34f), ImVec2(c.x + s * 0.15f, c.y + s * 0.34f), col, 1.7f);
}
static void DrawBinIcon(ImDrawList* d, ImVec2 c, float s, ImU32 col) {
    float w = s * 0.42f, top = c.y - s * 0.22f;
    d->AddLine(ImVec2(c.x - w * 0.62f, top), ImVec2(c.x + w * 0.62f, top), col, 1.7f);
    d->AddLine(ImVec2(c.x - w * 0.18f, top - s * 0.09f), ImVec2(c.x + w * 0.18f, top - s * 0.09f), col, 1.7f);
    d->AddQuad(ImVec2(c.x - w * 0.5f, top + 2), ImVec2(c.x + w * 0.5f, top + 2),
               ImVec2(c.x + w * 0.38f, c.y + s * 0.30f), ImVec2(c.x - w * 0.38f, c.y + s * 0.30f), col, 1.6f);
}

// The voice bubble body: play/pause, waveform (tap to seek), time.
static float ChatVoiceWidth(float maxW) { return std::min(maxW, 250.0f); }
static void RenderChatVoice(const ChatVoiceInfo& v, float width, bool mine, ImVec4 accent) {
    const float h = 38.0f, r = 16.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    VoiceClip* clip = VoiceClipFor(v.url);
    const float frac = clip ? VoicePlayFrac(v.url, *clip) : -1.0f;
    const bool playing = frac >= 0.0f && !g_playPaused;
    const bool loading = !clip || g_voiceWant == v.url;

    ImGui::PushID(v.url.c_str());
    // Play / pause disc.
    ImVec2 cc(p.x + r + 1, p.y + h * 0.5f);
    bool tap = ImGui::InvisibleButton("##vplay", ImVec2(r * 2 + 2, h));
    bool hov = ImGui::IsItemHovered();
    const float k = hov ? 1.7f : 1.5f;   // received: a brighter take on the chat colour so it pops off the grey bubble
    ImU32 discC = mine ? IM_COL32(255, 255, 255, hov ? 255 : 235)
                       : ImGui::GetColorU32(ImVec4(std::min(1.0f, accent.x * k), std::min(1.0f, accent.y * k), std::min(1.0f, accent.z * k), 1.0f));
    ImU32 glyphC = mine ? ImGui::GetColorU32(accent) : IM_COL32(255, 255, 255, 255);
    d->AddCircleFilled(cc, r, discC, 28);
    if (loading && g_voiceWant == v.url) {   // spinner while the clip downloads
        float t = (float)ImGui::GetTime() * 6.0f;
        d->PathArcTo(cc, r * 0.5f, t, t + 3.14159265f * 1.3f, 16);
        d->PathStroke(glyphC, 0, 2.0f);
    } else if (playing) {
        d->AddRectFilled(ImVec2(cc.x - 5, cc.y - 6), ImVec2(cc.x - 1.5f, cc.y + 6), glyphC, 1.0f);
        d->AddRectFilled(ImVec2(cc.x + 1.5f, cc.y - 6), ImVec2(cc.x + 5, cc.y + 6), glyphC, 1.0f);
    } else {
        d->AddTriangleFilled(ImVec2(cc.x - 4, cc.y - 7), ImVec2(cc.x - 4, cc.y + 7), ImVec2(cc.x + 7, cc.y), glyphC);
    }
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (tap) {
        if (!clip) g_voiceWant = (g_voiceWant == v.url) ? std::string() : v.url;
        else if (frac >= 0.0f) { g_playPaused = !g_playPaused; SDL_PauseAudioDevice(g_playDev, g_playPaused ? 1 : 0); }
        else PlayVoiceAt(v.url, *clip, 0.0f);
    }

    // Waveform (tap/drag to seek) + time on the right.
    const int total = clip && clip->ok ? (int)(clip->secs + 0.5f) : v.secs;
    std::string tm = VoiceClock(frac >= 0.0f && clip ? (int)(frac * clip->secs) : total);
    float tw = ImGui::CalcTextSize("0:00").x + 6;
    const float pillW = 34.0f;   // playback-speed pill (1x / 1.5x / 2x), WhatsApp-style
    float wx = p.x + r * 2 + 12, ww = width - (r * 2 + 12) - tw - 6 - pillW - 6;
    ImGui::SameLine(0, 0);
    ImGui::SetCursorScreenPos(ImVec2(wx, p.y));
    if (ImGui::InvisibleButton("##vwave", ImVec2(std::max(10.0f, ww), h)) && clip && clip->ok) {
        PlayVoiceAt(v.url, *clip, (ImGui::GetIO().MousePos.x - wx) / ww);
    }
    if (ImGui::IsItemHovered() && clip && clip->ok) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const float step = ww / kVoiceBars, bw = std::max(2.0f, step * 0.55f);
    ImU32 onC  = mine ? IM_COL32(255, 255, 255, 255) : ImGui::GetColorU32(ImVec4(0.55f, 0.78f, 1.0f, 1.0f));
    ImU32 offC = mine ? IM_COL32(255, 255, 255, 105) : IM_COL32(200, 210, 230, 95);
    for (int b = 0; b < kVoiceBars; ++b) {
        float a = clip && clip->ok ? clip->bars[b] : VoiceBarStub(v.url, b);
        float bh = std::max(3.0f, a * (h - 14));
        float x = wx + b * step;
        bool on = frac >= 0.0f && (b + 0.5f) / kVoiceBars <= frac;
        d->AddRectFilled(ImVec2(x, cc.y - bh * 0.5f), ImVec2(x + bw, cc.y + bh * 0.5f), on ? onC : offC, bw * 0.5f);
    }
    if (frac >= 0.0f) {   // playhead dot
        float hx = wx + frac * (ww - bw) + bw * 0.5f;
        d->AddCircleFilled(ImVec2(hx, cc.y), 5.0f, onC, 14);
    }
    ImVec2 ts = ImGui::CalcTextSize(tm.c_str());
    d->AddText(ImVec2(p.x + width - ts.x, cc.y - ts.y * 0.5f),
               mine ? IM_COL32(235, 240, 255, 230) : IM_COL32(200, 208, 225, 220), tm.c_str());
    {   // speed pill: tap to cycle; a clip that's playing continues from the same spot
        const char* sl = kVoiceSpeedLabels[std::clamp(g_voiceSpeed, 0, 2)];
        ImVec2 pp(wx + ww + 6, cc.y - 10);
        ImGui::SetCursorScreenPos(pp);
        const bool ptap = ImGui::InvisibleButton("##vspeed", ImVec2(pillW, 20));
        const bool phov = ImGui::IsItemHovered();
        const bool fast = g_voiceSpeed != 0;
        ImU32 pbg = mine ? IM_COL32(255, 255, 255, fast ? 70 : (phov ? 45 : 28))
                         : (fast ? ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.85f)) : IM_COL32(255, 255, 255, phov ? 34 : 20));
        d->AddRectFilled(pp, ImVec2(pp.x + pillW, pp.y + 20), pbg, 10.0f);
        ImVec2 ss = ImGui::CalcTextSize(sl);
        d->AddText(ImVec2(pp.x + (pillW - ss.x) * 0.5f, pp.y + (20 - ss.y) * 0.5f), IM_COL32(240, 244, 255, fast ? 255 : 200), sl);
        if (phov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("Playback speed"); }
        if (ptap) {
            g_voiceSpeed = (g_voiceSpeed + 1) % 3;
            SavePrefs();
            if (clip && frac >= 0.0f) {
                const bool wasPaused = g_playPaused;
                PlayVoiceAt(v.url, *clip, frac);
                if (wasPaused) { SDL_PauseAudioDevice(g_playDev, 1); g_playPaused = true; }
            }
        }
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(width, 0));
}

// Composer while recording: [bin] ● 0:04 ▮▮▮▮ live level  [Send]
static void DrawVoiceRecorderBar(float h, ImVec4 sendCol) {
    if (VoiceRecSeconds() >= kVoiceMaxSecs) { FinishVoiceRecord(true); return; }
    const float sendW = 58.0f;
    float w = ImGui::GetContentRegionAvail().x - sendW - 8;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(41, 46, 59, 255), 12.0f);
    // Discard.
    ImGui::SetCursorScreenPos(p);
    bool bin = ImGui::InvisibleButton("##vbin", ImVec2(h, h));
    bool bhov = ImGui::IsItemHovered();
    if (bhov) ImGui::SetTooltip("Discard recording");
    DrawBinIcon(d, ImVec2(p.x + h * 0.5f, p.y + h * 0.5f), h * 0.9f, bhov ? IM_COL32(255, 110, 110, 255) : IM_COL32(205, 212, 228, 230));
    // Pulsing red dot + clock.
    float pulse = 0.55f + 0.45f * std::sin((float)ImGui::GetTime() * 5.0f);
    float cy = p.y + h * 0.5f, x = p.x + h + 6;
    d->AddCircleFilled(ImVec2(x + 5, cy), 5.0f, IM_COL32(240, 70, 70, (int)(255 * pulse)), 16);
    std::string clk = VoiceClock((int)VoiceRecSeconds());
    ImVec2 cs = ImGui::CalcTextSize(clk.c_str());
    d->AddText(ImVec2(x + 16, cy - cs.y * 0.5f), IM_COL32(240, 243, 250, 255), clk.c_str());
    // Live level meter: newest on the right, scrolling left.
    float mx0 = x + 16 + ImGui::CalcTextSize("00:00").x + 10, mx1 = p.x + w - 12;
    const float step = 5.0f;
    int n = (int)((mx1 - mx0) / step);
    for (int i = 0; i < n; ++i) {
        int li = (int)g_recLevels.size() - n + i;
        float lv = li >= 0 ? g_recLevels[li] : 0.0f;
        float bh = std::max(2.0f, lv * (h - 12));
        float bx = mx0 + i * step;
        d->AddRectFilled(ImVec2(bx, cy - bh * 0.5f), ImVec2(bx + 3, cy + bh * 0.5f),
                         li >= 0 ? IM_COL32(235, 240, 250, 220) : IM_COL32(235, 240, 250, 50), 1.5f);
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + w + 8, p.y));
    auto brighten = [](ImVec4 c, float k) { return ImVec4(std::min(1.0f, c.x * k), std::min(1.0f, c.y * k), std::min(1.0f, c.z * k), 1.0f); };
    ImGui::PushStyleColor(ImGuiCol_Button, brighten(sendCol, 1.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brighten(sendCol, 1.45f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
    bool send = ImGui::Button("Send##voice", ImVec2(sendW, h));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if (bin) FinishVoiceRecord(false);
    else if (send) FinishVoiceRecord(true);
}
// New-message chime: a soft two-note "ding" synthesized once (no sound files),
// played on its own output device so it never interrupts a voice note.
static void PlayChatChime() {
    if (!g_chatSounds || !EnsureAudio()) return;
    static SDL_AudioDeviceID dev = 0;
    static std::vector<Sint16> pcm;
    static Uint32 lastAt = 0;
    if (SDL_GetTicks() - lastAt < 1200 && lastAt) return;   // a burst of messages = one chime
    lastAt = SDL_GetTicks();
    const int rate = 22050;
    if (pcm.empty()) {
        const float notes[2] = {880.0f, 1318.5f};             // A5 -> E6
        for (int n = 0; n < 2; ++n) {
            const int len = (int)(rate * (n ? 0.22f : 0.09f));
            for (int i = 0; i < len; ++i) {
                float t = (float)i / rate;
                float env = std::min(1.0f, t * 400.0f) * std::exp(-t * (n ? 14.0f : 22.0f));
                float v = std::sin(6.2831853f * notes[n] * t) + 0.25f * std::sin(6.2831853f * notes[n] * 2.0f * t);
                pcm.push_back((Sint16)(v * env * 5200.0f));
            }
        }
    }
    if (!dev) {
        SDL_AudioSpec want{}, have{};
        want.freq = rate; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512;
        dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (!dev) return;
        SDL_PauseAudioDevice(dev, 0);
    }
    SDL_ClearQueuedAudio(dev);
    SDL_QueueAudio(dev, pcm.data(), (Uint32)(pcm.size() * sizeof(Sint16)));
}

// Drag & drop target: DrawChatWindow publishes its rectangle and the open
// conversation each frame; the main loop's SDL_DROPFILE handler sends a file
// dropped inside it to that chat.
static bool        g_chatDropOk = false;
static ImVec2      g_chatDropMin, g_chatDropMax;
static std::string g_chatDropUid;
static long        g_chatDropGroup = 0;

// Round mic button that stands in for Send while the composer is empty.
static void DrawVoiceMicButton(const char* id, ImVec2 size, ImVec4 col, const std::string& toUid, long toGroup) {
    auto brighten = [](ImVec4 c, float k) { return ImVec4(std::min(1.0f, c.x * k), std::min(1.0f, c.y * k), std::min(1.0f, c.z * k), 1.0f); };
    ImGui::PushStyleColor(ImGuiCol_Button, brighten(col, 1.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brighten(col, 1.45f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (size.x <= 0) size.x = ImGui::GetContentRegionAvail().x;
    if (size.y <= 0) size.y = ImGui::GetFrameHeight();
    bool click = ImGui::Button(id, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Record a voice message");
    DrawMicIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + size.x * 0.5f, p.y + size.y * 0.5f),
                std::min(size.y, 26.0f), IM_COL32(255, 255, 255, 255));
    if (click) StartVoiceRecord(toUid, toGroup);
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

// ---- Outbox: a sent message shows up instantly; the network send runs off-thread ----
// (Sending used to block the UI on the HTTP request, and the message only
// appeared after the next poll.) Each entry renders as a "Sending" bubble until
// the conversation log contains it; a failed send offers Retry / Discard.
struct OutMsg {
    long localId = 0; std::string toUid; long toGroup = 0; long toChannel = 0; std::string body; long replyTo = 0;
    int state = 0;            // 0 queued/sending, 1 sent (waiting for the log), -1 failed
    long when = 0; Uint32 sentAt = 0;
};
static SDL_mutex* g_outMutex = nullptr;
static std::vector<OutMsg> g_outbox;   // (guarded)
static bool g_outWorkerRunning = false; // (guarded)
static long g_outNextId = 1;
static volatile bool g_srvForce = false; // Servers poller: refresh now (set after a channel post lands)
static void OutboxKick();               // starts the sender thread (defined with the chat workers)
static void OutboxSend(const std::string& toUid, long toGroup, const std::string& body, long replyTo = 0, long toChannel = 0) {
    if (body.empty()) return;
    if (!g_outMutex) g_outMutex = SDL_CreateMutex();
    OutMsg o; o.localId = g_outNextId++; o.toUid = toUid; o.toGroup = toGroup; o.toChannel = toChannel;
    o.body = body; o.replyTo = replyTo;
    o.when = (long)std::time(nullptr);
    SDL_LockMutex(g_outMutex); g_outbox.push_back(o); SDL_UnlockMutex(g_outMutex);
    OutboxKick();
}
static void OutboxSendChannel(long channelId, const std::string& body) { OutboxSend(std::string(), 0, body, 0, channelId); }
// My pending posts for a server channel, after dropping the ones the channel log
// now contains (inLog(body, when)).
static std::vector<OutMsg> OutboxForChannel(long channelId, const std::function<bool(const std::string&, long)>& inLog) {
    std::vector<OutMsg> pend;
    if (!g_outMutex || channelId <= 0) return pend;
    SDL_LockMutex(g_outMutex);
    const Uint32 now = SDL_GetTicks();
    g_outbox.erase(std::remove_if(g_outbox.begin(), g_outbox.end(), [&](const OutMsg& o) {
        return o.toChannel == channelId && o.state == 1 && (inLog(o.body, o.when) || now - o.sentAt > 15000);
    }), g_outbox.end());
    for (const auto& o : g_outbox) if (o.toChannel == channelId) pend.push_back(o);
    SDL_UnlockMutex(g_outMutex);
    return pend;
}
static void OutboxRetry(long localId) {
    if (!g_outMutex) return;
    SDL_LockMutex(g_outMutex);
    for (auto& x : g_outbox) if (x.localId == localId) x.state = 0;
    SDL_UnlockMutex(g_outMutex);
    OutboxKick();
}
static void OutboxDiscard(long localId) {
    if (!g_outMutex) return;
    SDL_LockMutex(g_outMutex);
    g_outbox.erase(std::remove_if(g_outbox.begin(), g_outbox.end(), [&](const OutMsg& x) { return x.localId == localId; }), g_outbox.end());
    SDL_UnlockMutex(g_outMutex);
}
// Draw my pending messages for one conversation (right-aligned, after the log)
// and drop the ones the log now contains. inLog(body, when) says whether the
// real message has arrived.
static int DrawOutbox(const std::string& uid, long gid, float avW, float baseX, ImVec4 bubbleCol,
                      const std::function<bool(const std::string&, long)>& inLog) {
    if (!g_outMutex) return 0;
    std::vector<OutMsg> pend;
    SDL_LockMutex(g_outMutex);
    const Uint32 now = SDL_GetTicks();
    g_outbox.erase(std::remove_if(g_outbox.begin(), g_outbox.end(), [&](const OutMsg& o) {
        return o.toChannel == 0 && o.state == 1 && (inLog(o.body, o.when) || now - o.sentAt > 15000);
    }), g_outbox.end());
    for (const auto& o : g_outbox) if (o.toChannel == 0 && o.toGroup == gid && (gid > 0 || o.toUid == uid)) pend.push_back(o);
    SDL_UnlockMutex(g_outMutex);
    const float padX = 14.0f, padY = 8.0f;
    for (const auto& o : pend) {
        const float bubbleMax = std::min(avW * 0.72f, 460.0f);
        const float bodyW = MeasureChatBodyWidth(o.body, bubbleMax - padX * 2);
        const char* status = o.state == -1 ? "Not sent" : o.state == 1 ? "Sent" : "Sending";
        const float statW = ImGui::CalcTextSize(status).x + (o.state == -1 ? 120.0f : 20.0f);
        const float useW = std::max(std::max(bodyW + 2.0f, statW) + padX * 2, 46.0f);
        ImGui::Dummy(ImVec2(0, 2));
        ImGui::SetCursorPosX(baseX + std::max(0.0f, avW - useW - 8.0f));
        ImGui::PushID((int)o.localId);
        ImVec4 bg = o.state == -1 ? ImVec4(0.42f, 0.18f, 0.20f, 1.0f) : ImVec4(bubbleCol.x, bubbleCol.y, bubbleCol.z, 0.78f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, padY));
        ImGui::BeginChild("##outbub", ImVec2(useW, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
        RenderChatBody(o.body, ImGui::GetContentRegionAvail().x);
        // Status line (right): a little clock while sending, Retry/Discard on failure.
        const float av = ImGui::GetContentRegionAvail().x;
        if (o.state == -1) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, av - statW));
            ImGui::TextColored(ImVec4(1.0f, 0.70f, 0.70f, 1.0f), "%s", status);
            ImGui::SameLine(0, 8);
            if (ImGui::SmallButton("Retry")) {
                SDL_LockMutex(g_outMutex);
                for (auto& x : g_outbox) if (x.localId == o.localId) x.state = 0;
                SDL_UnlockMutex(g_outMutex);
                OutboxKick();
            }
            ImGui::SameLine(0, 4);
            if (ImGui::SmallButton("Discard")) {
                SDL_LockMutex(g_outMutex);
                g_outbox.erase(std::remove_if(g_outbox.begin(), g_outbox.end(), [&](const OutMsg& x) { return x.localId == o.localId; }), g_outbox.end());
                SDL_UnlockMutex(g_outMutex);
            }
        } else {
            const float tw = ImGui::CalcTextSize(status).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, av - tw - 16.0f));
            ImVec2 cp = ImGui::GetCursorScreenPos();
            const float r = 5.0f, cy = cp.y + ImGui::GetTextLineHeight() * 0.5f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32 cc = IM_COL32(220, 228, 245, 170);
            dl->AddCircle(ImVec2(cp.x + r, cy), r, cc, 12, 1.2f);   // clock face + hands
            dl->AddLine(ImVec2(cp.x + r, cy), ImVec2(cp.x + r, cy - 3.2f), cc, 1.2f);
            dl->AddLine(ImVec2(cp.x + r, cy), ImVec2(cp.x + r + 2.4f, cy), cc, 1.2f);
            ImGui::Dummy(ImVec2(14, 0)); ImGui::SameLine(0, 2);
            ImGui::TextColored(ImVec4(0.86f, 0.90f, 1.0f, 0.65f), "%s", status);
        }
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    return (int)pend.size();
}
static volatile bool g_chatTypingReq = false;  // the composer asks the worker to broadcast "typing"
static std::string g_chatSelUid;                                        // (guarded) open conversation
static bool g_chatLogReady = false, g_chatConvReady = false, g_chatFriReady = false;  // (guarded)
static std::vector<okay::DirectMessage> g_chatLog;                      // (guarded)
static std::map<long, std::map<std::string,long>> g_chatReacts;        // (guarded)
static std::map<long, std::set<std::string>> g_chatMyReacts;           // (guarded)
static long g_chatPeerRead = 0;                                         // (guarded)
static bool g_chatPeerHere = false;        // (guarded) the peer has our chat open right now
static long g_chatPeerLastSeen = 0;        // (guarded) peer's last online heartbeat (unix)
static std::string g_chatPeerStateUid;     // (guarded) which conversation the three above describe
static volatile Uint32 g_chatViewMs = 0;   // last frame an open DM was on screen with the app focused
static std::vector<okay::Messages::ConversationPreview> g_chatConvs;    // (guarded)
static std::vector<okay::Friend> g_chatFriends;                        // (guarded)
// ---- Group chats (multi-person DMs) ----------------------------------------
static volatile long g_chatSelGroup = 0;        // (guarded input) open group id (0 = DM mode)
static bool g_chatGroupsReady = false, g_chatGLogReady = false;        // (guarded)
static std::vector<okay::Messages::GroupConvo>  g_chatGroups;          // (guarded) my groups
static std::vector<okay::Messages::GroupMessage> g_chatGLog;          // (guarded) open group thread
static std::map<long, std::map<std::string,long>> g_chatGReacts;      // (guarded)
static std::map<long, std::set<std::string>>      g_chatGMyReacts;    // (guarded)

// Messenger-style per-chat theme colours: the fill of *your* bubbles in that
// conversation. Index 0 follows the app accent.
static const char* kChatThemeNames[] = { "Default", "Purple", "Pink", "Orange", "Green", "Teal", "Red", "Graphite" };
static constexpr int kChatThemeCount = 8;
static ImVec4 ChatThemeColor(int idx) {
    static const ImVec4 pal[kChatThemeCount] = {
        ImVec4(0.19f, 0.43f, 0.72f, 1.0f), ImVec4(0.50f, 0.30f, 0.80f, 1.0f),
        ImVec4(0.80f, 0.30f, 0.52f, 1.0f), ImVec4(0.85f, 0.45f, 0.18f, 1.0f),
        ImVec4(0.16f, 0.58f, 0.38f, 1.0f), ImVec4(0.11f, 0.53f, 0.60f, 1.0f),
        ImVec4(0.76f, 0.24f, 0.24f, 1.0f), ImVec4(0.34f, 0.37f, 0.44f, 1.0f),
    };
    if (idx <= 0 || idx >= kChatThemeCount) return kAccentDim;
    return pal[idx];
}
static ImVec4 ChatThemeFor(const std::string& uid) {
    auto it = g_chatColor.find(uid);
    return ChatThemeColor(it == g_chatColor.end() ? 0 : it->second);
}
// A small filled 5-point star (drawn as geometry, so it never depends on the
// font having a star glyph). Used to mark starred/saved messages.
static void DrawStar(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    ImVec2 pts[10];
    for (int i = 0; i < 10; ++i) {
        float a = -1.5707963f + i * 0.6283185f;            // start at the top, 36deg steps
        float rr = (i % 2 == 0) ? r : r * 0.45f;
        pts[i] = ImVec2(c.x + std::cos(a) * rr, c.y + std::sin(a) * rr);
    }
    dl->AddConcavePolyFilled(pts, 10, col);
}

// ---- Polls ------------------------------------------------------------------
// A poll is an ordinary message: "[Poll] question" then one "- option" per line
// (older clients just see the text). A vote is a reaction "poll:<n>" on that
// message, so no extra tables; one vote per person (voting again moves it,
// tapping your choice again takes it back).
static float ChatPollWidth(float maxW) { return std::min(maxW, 300.0f); }
static void ChatReactAsync(bool group, long id, const std::string& removeKey, const std::string& addKey);   // defined with the chat workers
// My vote as just tapped, shown until the poller's data agrees (or 15 s pass).
struct PollLocalVote { int choice = -1; Uint32 at = 0; };
static std::map<std::pair<int, long>, PollLocalVote> g_pollLocal;
static void PollTally(bool group, long id, std::size_t n, const std::map<std::string, long>* counts,
                      const std::set<std::string>* mine, std::vector<long>& out, int& myChoice) {
    out.assign(n, 0); myChoice = -1;
    for (std::size_t i = 0; i < n; ++i) {
        const std::string k = "poll:" + std::to_string(i);
        if (counts) { auto it = counts->find(k); if (it != counts->end()) out[i] = std::max(0L, it->second); }
        if (mine && myChoice < 0 && mine->count(k)) myChoice = (int)i;
    }
    auto it = g_pollLocal.find({group ? 1 : 0, id});
    if (it == g_pollLocal.end()) return;
    if (it->second.choice == myChoice || SDL_GetTicks() - it->second.at > 15000) { g_pollLocal.erase(it); return; }
    if (myChoice >= 0 && out[(std::size_t)myChoice] > 0) --out[(std::size_t)myChoice];
    myChoice = it->second.choice;
    if (myChoice >= 0 && myChoice < (int)n) ++out[(std::size_t)myChoice];
}
// The poll card: question, one row per option (tap to vote) with a result bar,
// and the vote total. Votes apply locally at once and sync in the background.
static void RenderChatPoll(const ChatPollInfo& p, bool group, long id, const std::map<std::string, long>* counts,
                           const std::set<std::string>* mine, float width, bool mineBubble, ImVec4 accent) {
    std::vector<long> tally; int myChoice = -1;
    PollTally(group, id, p.options.size(), counts, mine, tally, myChoice);
    long total = 0; for (long v : tally) total += v;
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGui::PushID((int)id);
    {   // header: a little bar-chart glyph + "Poll", then the question
        ImVec2 hp = ImGui::GetCursorScreenPos();
        const ImU32 hc = mineBubble ? IM_COL32(230, 236, 255, 200) : IM_COL32(170, 182, 205, 230);
        for (int b = 0; b < 3; ++b) {
            const float bh = 5.0f + b * 3.0f, bx = hp.x + b * 4.5f;
            d->AddRectFilled(ImVec2(bx, hp.y + 13 - bh), ImVec2(bx + 3, hp.y + 13), hc, 1.0f);
        }
        ImGui::SetCursorScreenPos(ImVec2(hp.x + 18, hp.y));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(hc), "%s", group ? "Group poll" : "Poll");
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextWrapped("%s", p.question.c_str());
    ImGui::TextWrapped(" ");   // spacer line (keeps wrap pos)
    ImGui::PopTextWrapPos();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetTextLineHeight() + 2);
    const float rowH = 34.0f;
    int clicked = -1;
    for (std::size_t i = 0; i < p.options.size(); ++i) {
        ImGui::PushID((int)i);
        ImVec2 rp = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##opt", ImVec2(width, rowH))) clicked = (int)i;
        const bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const bool chosen = myChoice == (int)i;
        const float frac = total > 0 ? (float)tally[i] / (float)total : 0.0f;
        const ImU32 track = mineBubble ? IM_COL32(255, 255, 255, hov ? 34 : 22) : IM_COL32(255, 255, 255, hov ? 22 : 12);
        const ImU32 fill  = mineBubble ? IM_COL32(255, 255, 255, chosen ? 70 : 40)
                                       : ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, chosen ? 0.75f : 0.40f));
        d->AddRectFilled(rp, ImVec2(rp.x + width, rp.y + rowH - 4), track, 8.0f);
        if (frac > 0.0f) d->AddRectFilled(rp, ImVec2(rp.x + std::max(10.0f, width * frac), rp.y + rowH - 4), fill, 8.0f);
        // radio / check
        const ImVec2 cc(rp.x + 14, rp.y + (rowH - 4) * 0.5f);
        const ImU32 ink = IM_COL32(240, 244, 255, 235);
        if (chosen) {
            d->AddCircleFilled(cc, 7.0f, mineBubble ? IM_COL32(255, 255, 255, 240) : ImGui::GetColorU32(accent), 20);
            const ImU32 tick = mineBubble ? ImGui::GetColorU32(accent) : IM_COL32(255, 255, 255, 255);
            d->AddLine(ImVec2(cc.x - 3.2f, cc.y), ImVec2(cc.x - 0.8f, cc.y + 2.6f), tick, 1.6f);
            d->AddLine(ImVec2(cc.x - 0.8f, cc.y + 2.6f), ImVec2(cc.x + 3.6f, cc.y - 2.8f), tick, 1.6f);
        } else d->AddCircle(cc, 7.0f, IM_COL32(220, 228, 245, 170), 20, 1.5f);
        const std::string cnt = std::to_string(tally[i]);
        const float cw = ImGui::CalcTextSize(cnt.c_str()).x;
        const float ty = rp.y + (rowH - 4 - ImGui::GetTextLineHeight()) * 0.5f;
        d->PushClipRect(ImVec2(rp.x + 28, rp.y), ImVec2(rp.x + width - cw - 14, rp.y + rowH), true);
        d->AddText(ImVec2(rp.x + 28, ty), ink, p.options[i].c_str());
        d->PopClipRect();
        if (total > 0) d->AddText(ImVec2(rp.x + width - cw - 8, ty), IM_COL32(230, 236, 250, 210), cnt.c_str());
        ImGui::PopID();
    }
    ImGui::TextColored(mineBubble ? ImVec4(0.86f, 0.90f, 1.0f, 0.70f) : ImVec4(0.62f, 0.67f, 0.76f, 0.95f),
                       "%ld vote%s%s", total, total == 1 ? "" : "s", myChoice >= 0 ? "  \xC2\xB7  tap your choice to undo" : "");
    ImGui::PopID();
    if (clicked >= 0) {
        const int next = clicked == myChoice ? -1 : clicked;
        ChatReactAsync(group, id, myChoice >= 0 ? "poll:" + std::to_string(myChoice) : std::string(),
                       next >= 0 ? "poll:" + std::to_string(next) : std::string());
        g_pollLocal[{group ? 1 : 0, id}] = PollLocalVote{next, SDL_GetTicks()};
    }
}
static bool IsPollReaction(const std::string& k) { return k.rfind("poll:", 0) == 0; }
static bool HasVisibleReactions(const std::map<std::string, long>& m) {
    for (const auto& kv : m) if (kv.second > 0 && !IsPollReaction(kv.first)) return true;
    return false;
}
// Poll creator (shared by DM + group composers): question + 2..10 options.
static void PollCreatorPopup(const char* id, const std::string& toUid, long toGroup) {
    static char q[200] = "";
    static std::vector<std::array<char, 100>> opts(2);
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextDisabled("QUESTION");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##pq", "Ask something\xE2\x80\xA6", q, sizeof(q));
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextDisabled("OPTIONS");
    int removeAt = -1;
    for (std::size_t i = 0; i < opts.size(); ++i) {
        ImGui::PushID((int)i);
        ImGui::SetNextItemWidth(opts.size() > 2 ? ImGui::GetContentRegionAvail().x - 30 : -1);
        char hint[24]; std::snprintf(hint, sizeof(hint), "Option %d", (int)i + 1);
        ImGui::InputTextWithHint("##po", hint, opts[i].data(), opts[i].size());
        if (opts.size() > 2) { ImGui::SameLine(0, 4); if (ImGui::Button("x", ImVec2(26, 0))) removeAt = (int)i; }
        ImGui::PopID();
    }
    if (removeAt >= 0) opts.erase(opts.begin() + removeAt);
    if (opts.size() < 10 && ImGui::SmallButton("+ Add option")) opts.push_back({});
    ImGui::Dummy(ImVec2(0, 6));
    std::vector<std::string> filled;
    for (const auto& o : opts) { std::string t = o.data(); while (!t.empty() && t.back() == ' ') t.pop_back(); if (!t.empty()) filled.push_back(t); }
    std::string qs = q; while (!qs.empty() && qs.back() == ' ') qs.pop_back();
    const bool ok = !qs.empty() && filled.size() >= 2;
    auto reset = [&]() { q[0] = '\0'; opts.assign(2, {}); };
    if (ImGui::Button("Cancel", ImVec2(90, 30))) { reset(); ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    ImGui::BeginDisabled(!ok);
    if (PrimaryButton("Send poll", ImVec2(110, 30))) {
        OutboxSend(toUid, toGroup, PollMessageBody(qs, filled));
        reset(); ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    if (!ok) { ImGui::SameLine(); ImGui::TextDisabled("Add a question and 2+ options"); }
    ImGui::EndPopup();
}
// The composer's "+" button: photo/file or a poll.
static void ComposerPlusButton(const char* id, float size, const std::string& toUid, long toGroup) {
    ImGui::PushID(id);
    ImVec2 ap = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##plus", ImVec2(size, size))) ImGui::OpenPopup("##plusmenu");
    const bool ahov = ImGui::IsItemHovered();
    ImDrawList* ad = ImGui::GetWindowDrawList();
    ImVec2 c(ap.x + size * 0.5f, ap.y + size * 0.5f);
    ImU32 acol = ahov ? IM_COL32(255, 255, 255, 255) : IM_COL32(200, 206, 220, 220);
    ad->AddCircle(c, size * 0.35f, acol, 20, 1.6f);
    ad->AddLine(ImVec2(c.x - 4, c.y), ImVec2(c.x + 4, c.y), acol, 1.8f);
    ad->AddLine(ImVec2(c.x, c.y - 4), ImVec2(c.x, c.y + 4), acol, 1.8f);
    if (ahov) ImGui::SetTooltip("Attach a file or create a poll");
    bool openPoll = false;
    if (ImGui::BeginPopup("##plusmenu")) {
        if (ImGui::MenuItem("Photo or file\xE2\x80\xA6", "25 MB")) PickAndSendAttachment(toUid, toGroup);
        if (ImGui::MenuItem("Poll\xE2\x80\xA6")) openPoll = true;
        ImGui::EndPopup();
    }
    if (openPoll || std::getenv("OKAY_CHAT_DEMO_POLLNEW")) { if (!ImGui::IsPopupOpen("Create a poll")) ImGui::OpenPopup("Create a poll"); }
    PollCreatorPopup("Create a poll", toUid, toGroup);
    ImGui::PopID();
}

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
    std::map<long,long>& gSeenId,
    const std::string& myName)
{
    long gid = g_chatSelGroup;
    const okay::Messages::GroupConvo* gp = nullptr;
    for (const auto& gg : groups) if (gg.id == gid) gp = &gg;
    std::string title = (gp && !gp->title.empty()) ? gp->title : "Group chat";
    int memberCount = gp ? gp->members : 0;

    // ---- Header: group glyph, title, member count, options menu ----------
    ImVec2 hp = ImGui::GetCursorScreenPos();
    ImDrawList* hd = ImGui::GetWindowDrawList();
    {   // Distinct header zone (strip + bottom border), matching the DM header.
        float hw = ImGui::GetContentRegionAvail().x;
        hd->AddRectFilled(ImVec2(hp.x - 6, hp.y - 6), ImVec2(hp.x + hw + 6, hp.y + 40), IM_COL32(26, 29, 36, 150));
        hd->AddLine(ImVec2(hp.x - 6, hp.y + 40), ImVec2(hp.x + hw + 6, hp.y + 40), IM_COL32(60, 66, 78, 200), 1.0f);
    }
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
    // @mention autocomplete: when the word being typed starts with '@', offer
    // matching member names (computed before layout so the composer can reserve
    // room for the suggestion row).
    std::size_t mentionAt = std::string::npos;
    std::vector<std::string> mentionHits;
    {
        std::string cur = gInput;
        std::size_t sp = cur.find_last_of(" \n");
        std::size_t start = (sp == std::string::npos) ? 0 : sp + 1;
        if (start < cur.size() && cur[start] == '@') {
            mentionAt = start;
            std::string pre = cur.substr(start + 1);
            for (auto& ch : pre) ch = (char)std::tolower((unsigned char)ch);
            std::set<std::string> names;
            for (const auto& gm : glog) if (!gm.senderName.empty() && gm.senderName != myName) names.insert(gm.senderName);
            for (const auto& kv : fById) if (kv.second && kv.second->status == "accepted" && !kv.second->username.empty()) names.insert(kv.second->username);
            for (const auto& nm : names) {
                std::string low = nm; for (auto& ch : low) ch = (char)std::tolower((unsigned char)ch);
                if (low.compare(0, pre.size(), pre) == 0) mentionHits.push_back(nm);
                if (mentionHits.size() >= 6) break;
            }
        }
    }
    float composerH = 48.0f + (gReplyTo || gEditId ? 24.0f : 0.0f) + (mentionHits.empty() ? 0.0f : 28.0f);
    ImGui::BeginChild("##glog", ImVec2(0, ImGui::GetContentRegionAvail().y - composerH), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    float avW = ImGui::GetContentRegionAvail().x;
    float baseX = ImGui::GetCursorPosX();
    const float padX = 14.0f, padY = 8.0f;
    std::string prevSender; bool firstMsg = true;
    for (const auto& m : glog) {
        bool newSender = firstMsg || m.sender != prevSender;
        firstMsg = false; prevSender = m.sender;
        ImGui::Dummy(ImVec2(0, newSender ? 9.0f : 2.0f));

        float bubbleMax = std::min(avW * 0.72f, 460.0f); if (bubbleMax < 120) bubbleMax = (avW < 120 ? avW : 120);
        ChatFileInfo gfile; const bool gIsFile = ParseChatFile(m.body, gfile);
        ChatVoiceInfo gvoice; const bool gIsVoice = !gIsFile && ParseChatVoice(m.body, gvoice);
        ChatPollInfo gpoll; const bool gIsPoll = !gIsFile && !gIsVoice && ParseChatPoll(m.body, gpoll);
        float bodyW = gIsFile ? ChatFileCardWidth(gfile, bubbleMax - padX*2)
                    : gIsVoice ? ChatVoiceWidth(bubbleMax - padX*2)
                    : gIsPoll ? ChatPollWidth(bubbleMax - padX*2) : MeasureChatBodyWidth(m.body, bubbleMax - padX*2);
        char tbuf[24] = ""; if (m.when>0){ std::time_t tt=(std::time_t)m.when; std::tm* tm=std::localtime(&tt); if(tm) std::strftime(tbuf,sizeof(tbuf),"%H:%M",tm); }
        std::string meta = tbuf; if (m.edited) meta += " \xC2\xB7 edited";
        float metaW = ImGui::CalcTextSize(meta.c_str()).x;
        float contentW = std::max(bodyW + 2.0f, metaW);
        float useW = std::max(contentW + padX*2, 46.0f);
        float x0 = m.mine ? (avW - useW - 8.0f) : 0.0f;   // 8px breathing room from the scrollbar
        if (x0 < 0) x0 = 0;

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
        ImGui::BeginChild("##gbub", ImVec2(useW, 0),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
        if (gIsFile) RenderChatFileCard(gfile, ImGui::GetContentRegionAvail().x);
        else if (gIsVoice) RenderChatVoice(gvoice, ImGui::GetContentRegionAvail().x, m.mine, kAccentDim);
        else if (gIsPoll) {
            auto rc = greacts.find(m.id); auto mr = gmyreacts.find(m.id);
            RenderChatPoll(gpoll, true, m.id, rc != greacts.end() ? &rc->second : nullptr,
                           mr != gmyreacts.end() ? &mr->second : nullptr, ImGui::GetContentRegionAvail().x, m.mine, kAccentDim);
        }
        else RenderChatBody(m.body, ImGui::GetContentRegionAvail().x);
        {   // Timestamp tucked bottom-right, small + dim.
            float av2 = ImGui::GetContentRegionAvail().x;
            float mw2 = ImGui::CalcTextSize(meta.c_str()).x;
            if (mw2 < av2) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (av2 - mw2));
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(m.mine ? ImVec4(0.82f,0.88f,1.0f,0.60f) : ImVec4(0.60f,0.66f,0.76f,0.70f)));
        ImGui::TextUnformatted(meta.c_str());
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        // A message that @mentions you gets an accent outline so it stands out.
        if (!m.mine && !myName.empty()) {
            std::string lb = m.body, tag = "@" + myName;
            for (auto& ch : lb)  ch = (char)std::tolower((unsigned char)ch);
            for (auto& ch : tag) ch = (char)std::tolower((unsigned char)ch);
            if (lb.find(tag) != std::string::npos) {
                ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRect(ImVec2(a.x - 1.5f, a.y - 1.5f), ImVec2(b.x + 1.5f, b.y + 1.5f),
                                                    ImGui::GetColorU32(kAccent), 15.0f, 0, 2.0f);
            }
        }

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
        if (cit != greacts.end() && HasVisibleReactions(cit->second)) {
            ImGui::SetCursorPosX(baseX + x0);
            bool firstPill = true;
            for (const auto& kv : cit->second) {
                if (kv.second <= 0 || IsPollReaction(kv.first)) continue;
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
    DrawOutbox(std::string(), gid, avW, baseX, kAccentDim, [&](const std::string& body, long when) {
        for (auto it = glog.rbegin(); it != glog.rend(); ++it)
            if (it->mine && it->body == body && it->when >= when - 120) return true;
        return false;
    });
    if (glog.empty()) ImGui::TextDisabled("No messages yet — say hello!");
    // Keep pinned to the newest message: on opening the group, or on a new
    // message while already near the bottom. Stick for a few frames because
    // bubble heights settle a frame late (a one-shot scroll lands short).
    static int gStick = 0; static long gLastGid = 0; static size_t gLastN = 0;
    if (gid != gLastGid || glog.size() != gLastN) {
        bool nearBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 80.0f;
        if (gid != gLastGid || nearBottom) gStick = 4;
        gLastGid = gid; gLastN = glog.size();
    }
    if (gStick > 0) { ImGui::SetScrollHereY(1.0f); --gStick; }
    ImGui::EndChild();

    // ---- Composer ---------------------------------------------------------
    if (gReplyTo || gEditId) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(kAccent));
        ImGui::Text(gEditId ? "Editing message" : "Replying to %s", gReplyName.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton("x##gcancel")) { gReplyTo = 0; gEditId = 0; gInput[0] = '\0'; }
    }
    static bool gRefocus = false;
    if (!mentionHits.empty()) {   // suggestion chips: click to complete the @name
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Mention");
        for (const auto& nm : mentionHits) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.78f, 1.0f, 1.0f));
            if (ImGui::SmallButton(("@" + nm + "##mh").c_str())) {
                std::string cur = gInput;
                cur = cur.substr(0, mentionAt) + "@" + nm + " ";
                std::snprintf(gInput, 1024, "%s", cur.c_str());
                gRefocus = true;
            }
            ImGui::PopStyleColor();
        }
    }
    {   // Emoji / GIFs / stickers for the group (GIFs + stickers send straight away)
        std::string media = ChatMediaPicker("gmedia", gInput, 1024, nullptr, true);
        if (!media.empty()) {
            OutboxSend(std::string(), gid, media);
        }
        ImGui::SameLine(0, 4);
    }
    {   // Attach a photo or file to the group
        ComposerPlusButton("gplus", ImGui::GetTextLineHeight() + 14.0f, std::string(), gid);   // matches the input pill height
        ImGui::SameLine(0, 6);
    }
    if (VoiceRecording() && !VoiceRecordingFor(std::string(), gid)) FinishVoiceRecord(false);   // switched chats
    if (VoiceRecordingFor(std::string(), gid)) { DrawVoiceRecorderBar(ImGui::GetTextLineHeight() + 14.0f, kAccentDim); return; }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(11, 7));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.18f, 0.23f, 1.0f));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 66);
    if (gRefocus) { ImGui::SetKeyboardFocusHere(); gRefocus = false; }
    std::string ghint = g_attBusy ? ("Uploading " + ChatPreview(g_attName, 28) + "\xE2\x80\xA6")
                                 : std::string("Message the group  (@ to mention)");
    bool enter = ImGui::InputTextWithHint("##ginput", ghint.c_str(), gInput, 1024,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::SameLine(0, 8);
    if (!gInput[0] && !gEditId && !g_attBusy) {   // empty composer -> mic (WhatsApp)
        DrawVoiceMicButton("##gmic", ImVec2(-1, 0), kAccentDim, std::string(), gid);
        return;
    }
    ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.36f, 0.66f, 1.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
    bool sendBtn = ImGui::Button("Send##g", ImVec2(-1, 0));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if ((enter || sendBtn) && gInput[0]) {
        if (gEditId) { if (dm.EditGroup(gEditId, gInput)) { gEditId = 0; gInput[0] = '\0'; g_chatForceLog = true; } else Toast("Couldn't save the edit", 2); }
        else { OutboxSend(std::string(), gid, ExpandCustomEmoji(gInput), gReplyTo); gInput[0] = '\0'; gReplyTo = 0; }
        gRefocus = true;   // Enter deactivates a single-line input: keep typing without clicking
    }
}

void DrawChatWindow(okay::account::AccountService& account) {
    // Dev-only visual-test hook: OKAY_CHAT_DEMO=1 opens the chat with sample
    // messages (no login/network) so the UI can be reviewed in isolation. Inert
    // in normal use — the env var is never set for players.
    static const bool kChatDemo = std::getenv("OKAY_CHAT_DEMO") != nullptr;
    // Report a finished background attachment upload (success or why it failed).
    if (g_attMutex) {
        std::string upErr; bool upFinished = false;
        SDL_LockMutex(g_attMutex);
        if (g_attDone) { upFinished = true; upErr = g_attError; g_attDone = false; }
        SDL_UnlockMutex(g_attMutex);
        if (upFinished) Toast(upErr.empty() ? "Sent " + g_attName : upErr, upErr.empty() ? 1 : 2);
    }
    PumpVoiceRecord();
    PumpVoicePlayback();
    if (!g_chatWinOpen && !kChatDemo) { if (VoiceRecording()) FinishVoiceRecord(false); return; }
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
    static bool peerHere = false;                    // they have this chat open right now (Snapchat-style)
    static long peerLastSeen = 0;                    // their last online heartbeat (for "Delivered")
    static long scrollToId = 0, flashId = 0;         // jump to a replied-to message
    static Uint32 flashUntil = 0;
    static std::map<std::string,std::string> drafts;  // unsent text per conversation (kept on switch)
    static std::string fwdBody;                       // message body queued to forward
    static bool fwdPick = false;                      // forward: pick a conversation popup
    static std::map<std::string, std::pair<long,std::string>> pinnedMsg;  // uid -> (msgId, preview)
    static std::string lightboxUrl;                   // image shown fullscreen (empty = none)
    static bool openStarred = false;                  // conversation menu -> "Starred messages" popup
    static bool openMedia = false;                    // conversation menu -> "Media & links" popup
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

    // Open conversations as tabs above the thread (DMs and groups), browser-style.
    struct ChatTab { std::string key, uid, name, avatar; long gid = 0; };
    static std::vector<ChatTab> chatTabs;
    auto addTab = [&](const ChatTab& t) {
        for (auto& x : chatTabs)
            if (x.key == t.key) { if (!t.name.empty()) x.name = t.name; if (!t.avatar.empty()) x.avatar = t.avatar; return; }
        chatTabs.push_back(t);
        if (chatTabs.size() > 12) chatTabs.erase(chatTabs.begin());   // keep the strip manageable
    };
    // Keep the open conversation's unsent text (DM or group) before switching away.
    auto stashDraft = [&]() {
        if (g_chatSelGroup > 0) {
            std::string k = "g:" + std::to_string(g_chatSelGroup);
            if (gInput[0]) drafts[k] = gInput; else drafts.erase(k);
        } else if (!selUid.empty()) { if (input[0]) drafts[selUid] = input; else drafts.erase(selUid); }
    };
    // Display names learned when a chat is opened (covers people who aren't in
    // the friends list, e.g. OkayBot), so tabs/rows don't fall back to the id.
    static std::map<std::string, std::string> knownNames;
    auto openConv = [&](const std::string& uid, const std::string& nm, const std::string& av, bool online) {
        if (!nm.empty() && uid.rfind(nm.substr(0, std::min<std::size_t>(nm.size(), 6)), 0) != 0) knownNames[uid] = nm;
        stashDraft();
        addTab(ChatTab{uid, uid, nm, av, 0});
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
        stashDraft();
        std::string gname;
        for (const auto& gc : groups) if (gc.id == gid) gname = gc.title.empty() ? ("Group (" + std::to_string(gc.members) + ")") : gc.title;
        addTab(ChatTab{"g:" + std::to_string(gid), std::string(), gname, std::string(), gid});
        selUid.clear(); g_chatSelGroup = gid;
        glog.clear(); g_chatForceLog = true; gEditId = 0; gReplyTo = 0;
        auto dit = drafts.find("g:" + std::to_string(gid));
        std::snprintf(gInput, 1024, "%s", dit != drafts.end() ? dit->second.c_str() : "");
        focusComposer = true;
    };
    // Close a tab; closing the open one moves to its neighbour (or to nothing).
    auto closeTab = [&](std::size_t i) {
        if (i >= chatTabs.size()) return;
        const std::string cur = g_chatSelGroup > 0 ? "g:" + std::to_string(g_chatSelGroup) : selUid;
        const bool wasOpen = chatTabs[i].key == cur;
        chatTabs.erase(chatTabs.begin() + (long)i);
        if (!wasOpen) return;
        if (chatTabs.empty()) { stashDraft(); selUid.clear(); g_chatSelGroup = 0; input[0] = '\0'; gInput[0] = '\0'; return; }
        const ChatTab& n = chatTabs[std::min(i, chatTabs.size() - 1)];
        if (n.gid) openGroup(n.gid); else openConv(n.uid, n.name, n.avatar, false);
    };

    if (g_chatReqPending) {                     // a "Message" button asked to open a convo
        openConv(g_chatReqUid, g_chatReqName, g_chatReqAvatar, false);
        g_chatReqPending = false;
        ImGui::SetNextWindowFocus();
    }
    if (kChatDemo) {
        ImGui::SetNextWindowPos(ImVec2(16, 40), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(1000, 600), ImGuiCond_Always);
        g_chatWinOpen = true;
        static bool di = false;
        if (!di) { di = true; selUid = "demo-uid"; selName = "Alex Rivera"; selOnline = true;
            long tt = (long)time(nullptr);
            auto add = [&](long id, const char* b, long dt, bool mine, bool ed=false) {
                okay::DirectMessage m; m.id=id; m.body=b; m.when=tt-dt; m.mine=mine; m.edited=ed; log.push_back(m); };
            add(1,"Hey! Did you get the new build working?",900,false);
            add(2,"yeah just pulled it",880,true);
            add(3,"The chat bubbles actually hug the text now instead of stretching across the whole window.",820,false);
            add(4,"ok that was really bugging me",780,true);
            add(5,"wanna test group chats later tonight?",500,false);
            add(6,"sure, ping me when you're free",460,true,true);
            add(7,"@you the release notes are up: https://github.com/kingimann/OkaySpaceGameEngine/releases",120,false);
            add(8, okay::Messages::FileMessageBody("Level_Design_v3.pdf", 2516582ULL,
                   "https://example.com/storage/v1/object/public/chat/demo/Level_Design_v3.pdf").c_str(), 60, true);
            if (std::getenv("OKAY_CHAT_DEMO_VOICE")) {   // voice notes with a synthetic, speech-like clip
                const std::string u1 = "https://example.com/storage/v1/object/public/chat/demo/voice-a.wav";
                const std::string u2 = "https://example.com/storage/v1/object/public/chat/demo/voice-b.wav";
                for (const auto& u : {u1, u2}) {
                    std::vector<Sint16> pcm((std::size_t)kVoiceRate * (u == u1 ? 7 : 4));
                    unsigned rng = (unsigned)std::hash<std::string>{}(u);
                    for (std::size_t i = 0; i < pcm.size(); ++i) {
                        float t = (float)i / kVoiceRate;
                        float env = std::max(0.0f, std::sin(t * 2.3f) * std::sin(t * 7.1f + 1.0f));   // syllables
                        rng = rng * 1664525u + 1013904223u;
                        pcm[i] = (Sint16)(env * 20000.0f * (((rng >> 9) & 0xFFFF) / 32768.0f - 1.0f));
                    }
                    WriteWav16(VoiceCachePath(u).string(), pcm, kVoiceRate);
                }
                add(9, VoiceMessageBody(7, u1).c_str(), 40, false);
                add(10, VoiceMessageBody(4, u2).c_str(), 20, true);
                if (std::getenv("OKAY_CHAT_DEMO_PLAY")) {   // mid-playback state (needs SDL_AUDIODRIVER=dummy headless)
                    if (VoiceClip* c = VoiceClipFor(u1)) { PlayVoiceAt(u1, *c, 0.42f); SDL_PauseAudioDevice(g_playDev, 1); g_playPaused = true; }
                }
                if (std::getenv("OKAY_CHAT_DEMO_REC")) {    // composer in recording mode
                    g_recFake = true; g_recToUid = "demo-uid"; g_recToGroup = 0; g_recStart = SDL_GetTicks() - 4200;
                    for (int i = 0; i < 160; ++i) g_recLevels.push_back(std::max(0.05f, std::fabs(std::sin(i * 0.37f) * std::sin(i * 0.11f + 0.4f))));
                }
            }
            {   // Sidebar sample: friends, DM previews, a group, a draft, pinned + muted chats.
                auto fr = [&](const char* id, const char* nm, bool on) { okay::Friend f; f.id = id; f.username = nm; f.status = "accepted"; f.online = on; friends.push_back(f); };
                fr("demo-uid", "Alex Rivera", !std::getenv("OKAY_CHAT_DEMO_TICKS")); fr("u-maya", "Maya Chen", true); fr("u-jon", "Jonas Berg", false);
                fr("u-priya", "Priya Natarajan", true); fr("u-leo", "Leo Martins", false); fr("u-sam", "Sam Okafor", false);
                for (auto& f : friends) {   // recently-active tags in the list
                    if (f.id == "u-jon") f.lastSeen = tt - 12 * 60;
                    if (f.id == "u-leo") f.lastSeen = tt - 3 * 3600;
                    if (f.id == "u-sam") f.lastSeen = tt - 3 * 86400;
                }
                auto cv = [&](const char* uid, const char* body, long ago, long id, bool mine) {
                    okay::Messages::ConversationPreview c; c.uid = uid; c.lastBody = body; c.when = tt - ago; c.lastId = id; c.lastMine = mine; convs.push_back(c); };
                cv("demo-uid", "sure, ping me when you're free", 460, 6, true);
                cv("u-maya", "Pushed the new tileset, can you check the collision layer?", 300, 40, false);
                cv("u-jon", "[Voice] 0:12 https://example.com/v.wav", 3600 * 5, 22, false);
                cv("u-priya", "haha that boss fight is brutal", 86400 * 2, 15, true);
                okay::Messages::GroupConvo g; g.id = 7; g.title = "Game Jam Team"; g.members = 5; g.lastSender = "Maya";
                g.lastBody = "Build is up on itch, 3 hours left!"; g.when = tt - 180; g.lastId = 90; groups.push_back(g);
                g_pinnedChats.insert("g:7");
                chatTabs.push_back(ChatTab{"demo-uid", "demo-uid", "Alex Rivera", "", 0});
                chatTabs.push_back(ChatTab{"u-maya", "u-maya", "Maya Chen", "", 0});
                chatTabs.push_back(ChatTab{"g:7", "", "Game Jam Team", "", 7});
                if (std::getenv("OKAY_CHAT_DEMO_FOLD")) g_chatListFolded = true;
                g_mutedChats.insert("u-jon");
                drafts["u-priya"] = "I'll send you the save file tonight";
            }
            if (std::getenv("OKAY_CHAT_DEMO_EMOJI")) {   // colour emoji, a GIF, a sticker, a custom emoji
                const std::string rocket = "https://cdn.jsdelivr.net/gh/jdecked/twemoji@15.1.0/assets/72x72/1f680.png";
                add(11, "That's awesome \xF0\x9F\x94\xA5\xF0\x9F\x94\xA5 can't wait to try it \xF0\x9F\x98\x84 \xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", 50, false);
                add(12, "\xF0\x9F\x8E\x89\xF0\x9F\x9A\x80", 45, true);
                add(13, "https://upload.wikimedia.org/wikipedia/commons/2/2c/Rotating_earth_%28large%29.gif", 30, false);
                add(14, "[Sticker] https://cdn.jsdelivr.net/gh/jdecked/twemoji@15.1.0/assets/72x72/1f996.png", 20, true);
                std::string ce = "launch day <:rocketship:" + rocket + "> let's go";
                add(15, ce.c_str(), 10, false);
                if (!g_libMutex) g_libMutex = SDL_CreateMutex();
                okay::Messages::LibraryItem li; li.name = "rocketship"; li.file = "rocketship.png"; li.url = rocket;
                g_libEmoji.push_back(li);
                okay::Messages::LibraryItem ls; ls.name = "dino"; ls.file = "dino.png";
                ls.url = "https://cdn.jsdelivr.net/gh/jdecked/twemoji@15.1.0/assets/72x72/1f996.png";
                g_libStickers.push_back(ls); g_libLoaded = true;
                g_recentEmoji = {"\xF0\x9F\x94\xA5", "\xF0\x9F\x98\x84", ":rocketship:", "\xF0\x9F\x8E\x89"};
            }
            if (std::getenv("OKAY_CHAT_DEMO_POLL")) {   // a poll with some votes in
                add(23, PollMessageBody("Which level do we polish first for the jam?",
                        {"Asteroid belt", "Ice caves", "Boss arena"}).c_str(), 25, false);
                reacts[23]["poll:0"] = 3; reacts[23]["poll:2"] = 1; reacts[23]["heart"] = 1;
                myReacts[23].insert("poll:0");
            }
            if (std::getenv("OKAY_CHAT_DEMO_MD")) {   // formatting showcase
                add(21, "Formatting works now: **bold**, *italic*, ~~strike~~, `inline code` and a ||secret spoiler||", 15, false);
                add(22, "> quoting your last message\nyes! and code blocks:\n```\nvoid Update(float dt) {\n    pos += vel * dt;\n}\n```", 8, true);
            }
            if (std::getenv("OKAY_CHAT_DEMO_MEDIA")) openMedia = true;
            peerRead = 4;   // 1..4 read (blue ticks), 6 delivered (grey ticks)
            if (std::getenv("OKAY_CHAT_DEMO_HERE")) peerHere = true;          // Snapchat-style "is here"
            if (std::getenv("OKAY_CHAT_DEMO_TICKS")) { peerLastSeen = tt - 200; selOnline = false; }   // newest = "Sent"
            // Showcase per-chat extras: theme colour, nickname, a starred message.
            if (const char* th = std::getenv("OKAY_CHAT_DEMO_THEME")) g_chatColor["demo-uid"] = std::atoi(th);
            if (std::getenv("OKAY_CHAT_DEMO_BLOCK")) g_blockedUsers.insert("demo-uid");
            if (std::getenv("OKAY_CHAT_DEMO_EXTRAS")) {
                g_friendNicks["demo-uid"] = "Alex (team lead)";
                StarredMsg s; s.uid = "demo-uid"; s.when = tt - 820; s.preview = "The chat bubbles actually hug the text now...";
                g_starred[3] = s;
            }
        }
    }
    if (!kChatDemo) ImGui::SetNextWindowSize(ImVec2(720, 500), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(420, 320), ImVec2(FLT_MAX, FLT_MAX));
    // The chat window wears its own slate palette (dark themes only); popped
    // when this function returns, after End().
    struct ChatStyleScope {
        int n = 0;
        ~ChatStyleScope() { if (n) ImGui::PopStyleColor(n); }
    } chatStyle;
    if (g_themeIndex != 2) {
        const ImVec4 title(0.082f, 0.090f, 0.114f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, kChatWinBg);
        ImGui::PushStyleColor(ImGuiCol_TitleBg, title);
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, title);
        ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, title);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.22f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.29f, 0.36f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.18f, 0.23f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.19f, 0.21f, 0.27f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.130f, 0.142f, 0.178f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1, 1, 1, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.92f, 0.96f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.55f, 0.59f, 0.67f, 1.0f));
        chatStyle.n = 13;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));   // the list/thread manage their own padding
    const bool chatOpen = ImGui::Begin("Chat", &g_chatWinOpen, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleVar();
    g_chatDropOk = chatOpen && (account.IsLoggedIn() || kChatDemo);
    g_chatDropMin = ImGui::GetWindowPos();
    g_chatDropMax = ImVec2(g_chatDropMin.x + ImGui::GetWindowWidth(), g_chatDropMin.y + ImGui::GetWindowHeight());
    g_chatDropUid = g_chatSelGroup > 0 ? std::string() : selUid;
    g_chatDropGroup = g_chatSelGroup;
    if (chatOpen) {
        if (!account.IsLoggedIn() && !kChatDemo) {
            ImGui::SetCursorPos(ImVec2(20, 40));
            ImGui::TextDisabled("Sign in to chat with friends.");
            ImGui::End(); return;
        }
        okay::Messages dm(account);
        Uint32 now = SDL_GetTicks();
        // Friends + conversations come from the background chat poller (no UI block).
        (void)friendsNext; (void)convsNext;
        bool chatNewsCheck = false;
        if (g_chatMutex && !kChatDemo) {
            SDL_LockMutex(g_chatMutex);
            if (g_chatFriReady)  { friends = g_chatFriends; g_chatFriReady = false; }
            if (g_chatConvReady) { convs   = g_chatConvs;   g_chatConvReady = false; chatNewsCheck = true; }
            if (g_chatGroupsReady) { groups = g_chatGroups; g_chatGroupsReady = false; chatNewsCheck = true; }
            if (g_chatGLogReady) { glog = g_chatGLog; greacts = g_chatGReacts; gmyreacts = g_chatGMyReacts; g_chatGLogReady = false; }
            SDL_UnlockMutex(g_chatMutex);
        }

        // Chime when a chat gets a new incoming message (not muted/blocked, and
        // not the conversation you're already looking at).
        if (chatNewsCheck) {
            static std::map<std::string, long> lastIds;
            static bool primed = false;
            const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            const std::string me = account.CurrentSession().username;
            bool ding = false;
            for (const auto& c : convs) {
                long& prev = lastIds[c.uid];
                if (primed && c.lastId > prev && !c.lastMine && !g_mutedChats.count(c.uid) && !g_blockedUsers.count(c.uid)
                    && !(focused && g_chatSelGroup == 0 && c.uid == selUid)) ding = true;
                prev = std::max(prev, c.lastId);
            }
            for (const auto& gc : groups) {
                std::string key = "g:" + std::to_string(gc.id);
                long& prev = lastIds[key];
                if (primed && gc.lastId > prev && gc.lastSender != me && !g_mutedChats.count(key)
                    && !(focused && g_chatSelGroup == gc.id)) ding = true;
                prev = std::max(prev, gc.lastId);
            }
            primed = true;
            if (ding) PlayChatChime();
        }
        // Index friends by uid for names/avatars/presence.
        std::map<std::string, const okay::Friend*> fById;
        for (const auto& f : friends) fById[f.id] = &f;

        // ---- Left: conversation list (Messenger-style) -------------------------
        // A "Chats" header with compose / new-group buttons, a search pill,
        // All / Unread / Groups chips, then ONE list of DMs and groups sorted by
        // recency (pinned first): avatar + presence, name, time, a preview (or
        // the unsent draft), and unread / muted / pinned markers.
        static int  convTab = 0;          // 0 All, 1 Unread, 2 Groups
        static bool showArchived = false;
        const bool folded = g_chatListFolded;   // avatar-only strip
        float listW = folded ? 76.0f : (ImGui::GetContentRegionAvail().x < 640 ? 214.0f : 280.0f);
        const ImU32 cText   = IM_COL32(232, 236, 244, 255);
        const ImU32 cDim    = IM_COL32(150, 158, 175, 255);
        const ImU32 cFaint  = IM_COL32(118, 126, 142, 255);
        const ImVec4 accLt(kAccent.x * 0.55f + 0.45f, kAccent.y * 0.55f + 0.45f, kAccent.z * 0.55f + 0.45f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kChatSideBg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
        ImGui::BeginChild("##chatlist", ImVec2(listW, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        {   // Header: title + round icon buttons.
            ImVec2 hp = ImGui::GetCursorScreenPos();
            float hw = ImGui::GetContentRegionAvail().x;
            float th = 30.0f;
            if (!folded) {
                ImGui::SetCursorScreenPos(ImVec2(hp.x + 4, hp.y));
                ImGui::SetWindowFontScale(1.30f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(cText));
                ImGui::TextUnformatted("Chats");
                ImGui::PopStyleColor();
                ImGui::SetWindowFontScale(1.0f);
                th = ImGui::GetItemRectSize().y;
            }
            const float B = 30.0f;
            auto iconBtn = [&](const char* id, ImVec2 p, int kind, const char* tip) -> bool {
                ImGui::SetCursorScreenPos(p);
                bool c = ImGui::InvisibleButton(id, ImVec2(B, B));
                bool h = ImGui::IsItemHovered();
                ImVec2 cc(p.x + B * 0.5f, p.y + B * 0.5f);
                dl->AddCircleFilled(cc, B * 0.5f, h ? IM_COL32(255, 255, 255, 36) : IM_COL32(255, 255, 255, 16), 28);
                ImU32 ic = IM_COL32(232, 236, 244, 255);
                if (kind == 2) {   // fold / unfold the list
                    DrawNavIcon(dl, kIcoPanel, cc, ic);
                } else if (kind == 0) {   // compose: open square + pencil
                    dl->PathLineTo(ImVec2(cc.x + 0.5f, cc.y - 6.0f)); dl->PathLineTo(ImVec2(cc.x - 6.5f, cc.y - 6.0f));
                    dl->PathLineTo(ImVec2(cc.x - 6.5f, cc.y + 6.5f)); dl->PathLineTo(ImVec2(cc.x + 6.0f, cc.y + 6.5f));
                    dl->PathLineTo(ImVec2(cc.x + 6.0f, cc.y - 0.5f));
                    dl->PathStroke(ic, 0, 1.5f);
                    dl->AddLine(ImVec2(cc.x - 2.0f, cc.y + 2.0f), ImVec2(cc.x + 6.5f, cc.y - 6.5f), ic, 2.4f);
                } else {           // new group: person + plus
                    DrawNavIcon(dl, 3, ImVec2(cc.x - 2.0f, cc.y + 0.5f), ic);
                    dl->AddLine(ImVec2(cc.x + 4.0f, cc.y - 4.0f), ImVec2(cc.x + 9.0f, cc.y - 4.0f), ic, 1.6f);
                    dl->AddLine(ImVec2(cc.x + 6.5f, cc.y - 6.5f), ImVec2(cc.x + 6.5f, cc.y - 1.5f), ic, 1.6f);
                }
                if (h) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", tip); }
                return c;
            };
            if (folded) {   // stacked: unfold, then compose
                float cx = hp.x + (hw - B) * 0.5f;
                if (iconBtn("##foldlist", ImVec2(cx, hp.y), 2, "Show chat list")) { g_chatListFolded = false; SavePrefs(); }
                if (iconBtn("##newmsg", ImVec2(cx, hp.y + B + 8), 0, "New message")) ImGui::OpenPopup("##newchat");
                ImGui::SetCursorScreenPos(ImVec2(hp.x, hp.y + B * 2 + 8 + 10));
                ImVec2 lp = ImGui::GetCursorScreenPos();
                dl->AddLine(ImVec2(lp.x + 10, lp.y), ImVec2(lp.x + hw - 10, lp.y), IM_COL32(255, 255, 255, 18), 1.0f);
                ImGui::Dummy(ImVec2(0, 8));
            } else {
                float by = hp.y + (th - B) * 0.5f;
                if (iconBtn("##newgrp", ImVec2(hp.x + hw - B, by), 1, "New group")) {
                    showNewGroup = true; newGroupPick.clear(); newGroupName[0] = '\0';
                }
                if (iconBtn("##newmsg", ImVec2(hp.x + hw - B * 2 - 6, by), 0, "New message"))
                    ImGui::OpenPopup("##newchat");
                if (iconBtn("##foldlist", ImVec2(hp.x + hw - B * 3 - 12, by), 2, "Collapse chat list"))
                    { g_chatListFolded = true; SavePrefs(); }
                ImGui::SetCursorScreenPos(ImVec2(hp.x, hp.y + std::max(th, B) + 10));
            }
        }
        if (ImGui::BeginPopup("##newchat")) {
            ImGui::TextDisabled("New message to\xE2\x80\xA6");
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
        if (!folded) {   // Search pill with a magnifier.
            ImVec2 sp = ImGui::GetCursorScreenPos();
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(30, 7));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1, 1, 1, 0.065f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.09f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1, 1, 1, 0.11f));
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##convfilter", "Search chats", convFilter, sizeof(convFilter));
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            float fh = ImGui::GetItemRectSize().y;
            ImVec2 mc(sp.x + 15.0f, sp.y + fh * 0.5f - 1.0f);
            dl->AddCircle(mc, 4.6f, cDim, 16, 1.5f);
            dl->AddLine(ImVec2(mc.x + 3.4f, mc.y + 3.4f), ImVec2(mc.x + 6.8f, mc.y + 6.8f), cDim, 1.8f);
        }
        if (!folded) {   // Filter chips.
            ImGui::Dummy(ImVec2(0, 2));
            const char* chips[] = {"All", "Unread", "Groups"};
            ImVec2 cp = ImGui::GetCursorScreenPos();
            float x = cp.x, ch = ImGui::GetTextLineHeight() + 9.0f;
            for (int k = 0; k < 3; ++k) {
                ImVec2 ts = ImGui::CalcTextSize(chips[k]);
                float cw = ts.x + 24.0f;
                ImGui::SetCursorScreenPos(ImVec2(x, cp.y));
                ImGui::PushID(k);
                if (ImGui::InvisibleButton("##chip", ImVec2(cw, ch))) convTab = k;
                bool h = ImGui::IsItemHovered();
                ImGui::PopID();
                bool on = convTab == k;
                dl->AddRectFilled(ImVec2(x, cp.y), ImVec2(x + cw, cp.y + ch),
                                  on ? ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.24f))
                                     : (h ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 10)), ch * 0.5f);
                dl->AddText(ImVec2(x + 12.0f, cp.y + (ch - ts.y) * 0.5f), on ? ImGui::GetColorU32(accLt) : cDim, chips[k]);
                if (h) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                x += cw + 6.0f;
            }
            ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y + ch + 8.0f));
        }

        // ---- Build one list: DM conversations + groups, then friends to start.
        struct Row {
            std::string key, uid, name, avatar, preview;
            long gid = 0, when = 0, lastId = 0, lastSeen = 0;
            int members = 0;
            bool mine = false, online = false, unread = false, starter = false;
        };
        std::vector<Row> rows;
        std::set<std::string> have;
        for (const auto& c : convs) {
            Row r; r.key = r.uid = c.uid; r.lastId = c.lastId; r.when = c.when; r.mine = c.lastMine;
            r.preview = (c.lastMine ? "You: " : "") + ChatPreviewSmart(c.lastBody, 90);
            if (auto it = fById.find(c.uid); it != fById.end()) {
                r.name = it->second->display.empty() ? it->second->username : it->second->display;
                r.avatar = it->second->avatar; r.online = it->second->online; r.lastSeen = it->second->lastSeen;
            } else if (auto kn = knownNames.find(c.uid); kn != knownNames.end()) r.name = kn->second;
            else r.name = (c.uid == selUid) ? selName : ChatPreview(c.uid, 10);
            bool open = g_chatSelGroup == 0 && c.uid == selUid;
            r.unread = r.lastId > 0 && !r.mine && r.lastId > seenId[r.uid] && !open && !g_blockedUsers.count(r.uid);
            rows.push_back(r); have.insert(c.uid);
        }
        for (const auto& gc : groups) {
            Row r; r.gid = gc.id; r.key = "g:" + std::to_string(gc.id); r.when = gc.when; r.lastId = gc.lastId;
            r.members = gc.members;
            r.name = gc.title.empty() ? ("Group (" + std::to_string(gc.members) + ")") : gc.title;
            r.preview = gc.lastBody.empty() ? std::string("No messages yet")
                      : (gc.lastSender.empty() ? "" : gc.lastSender + ": ") + ChatPreviewSmart(gc.lastBody, 90);
            r.unread = gc.lastId > 0 && gc.lastId > std::max(gSeenId[gc.id], gc.lastRead) && g_chatSelGroup != gc.id;
            rows.push_back(r);
        }
        for (const auto& f : friends) {
            if (f.status != "accepted" || have.count(f.id)) continue;
            Row r; r.key = r.uid = f.id; r.name = f.display.empty() ? f.username : f.display;
            r.avatar = f.avatar; r.online = f.online; r.lastSeen = f.lastSeen; r.preview = "Say hi \xF0\x9F\x91\x8B"; r.starter = true;
            rows.push_back(r);
        }
        // Private nicknames (set from the conversation menu) replace display names.
        for (auto& r : rows)
            if (!r.gid) if (auto nk = g_friendNicks.find(r.uid); nk != g_friendNicks.end() && !nk->second.empty()) r.name = nk->second;
        // Pinned first, then most recent; people you haven't talked to yet last.
        std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
            if (a.starter != b.starter) return !a.starter;
            bool pa = g_pinnedChats.count(a.key) != 0, pb = g_pinnedChats.count(b.key) != 0;
            if (pa != pb) return pa;
            return a.when > b.when;
        });
        std::set<std::string> unreadKeys;   // for the tab strip's unread dots
        for (const auto& r : rows) if (r.unread) unreadKeys.insert(r.key);
        std::string filt = convFilter;
        for (auto& ch : filt) ch = (char)std::tolower((unsigned char)ch);
        auto passes = [&](const Row& r) {
            if (folded) return true;
            if (convTab == 1 && !r.unread) return false;
            if (convTab == 2 && !r.gid) return false;
            if (!filt.empty()) {
                std::string low = r.name; for (auto& ch : low) ch = (char)std::tolower((unsigned char)ch);
                if (low.find(filt) == std::string::npos) return false;
            }
            return true;
        };

        ImGui::BeginChild("##convrows", ImVec2(0, 0), false, folded ? ImGuiWindowFlags_NoScrollbar : 0);
        ImDrawList* rdl = ImGui::GetWindowDrawList();
        auto drawRow = [&](const Row& r) {
            const bool pinned = g_pinnedChats.count(r.key) != 0;
            const bool muted  = g_mutedChats.count(r.key) != 0;
            const bool archived = !r.gid && g_archivedChats.count(r.uid) != 0;
            const bool sel = r.gid ? g_chatSelGroup == r.gid : (g_chatSelGroup == 0 && r.uid == selUid);
            ImGui::PushID(r.key.c_str());
            ImVec2 rp = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x, rowH = folded ? 56.0f : 62.0f, A = 44.0f;
            bool click = ImGui::InvisibleButton("##row", ImVec2(w, rowH));
            bool hov = ImGui::IsItemHovered();
            if (hov && folded) ImGui::SetTooltip("%s\n%s", r.name.c_str(), r.preview.c_str());
            if (ImGui::BeginPopupContextItem("##convctx")) {   // right-click actions
                if (ImGui::MenuItem(pinned ? "Unpin" : "Pin to top")) { if (pinned) g_pinnedChats.erase(r.key); else g_pinnedChats.insert(r.key); SavePrefs(); }
                if (ImGui::MenuItem(muted ? "Unmute" : "Mute notifications")) { if (muted) g_mutedChats.erase(r.key); else g_mutedChats.insert(r.key); SavePrefs(); }
                if (!r.gid && !r.starter && ImGui::MenuItem(archived ? "Unarchive" : "Archive")) {
                    if (archived) g_archivedChats.erase(r.uid); else g_archivedChats.insert(r.uid); SavePrefs();
                }
                if (r.unread && ImGui::MenuItem("Mark as read")) { if (r.gid) gSeenId[r.gid] = r.lastId; else seenId[r.uid] = r.lastId; }
                ImGui::EndPopup();
            }
            if (click) { if (r.gid) openGroup(r.gid); else openConv(r.uid, r.name, r.avatar, r.online); }
            if (sel || hov) rdl->AddRectFilled(rp, ImVec2(rp.x + w, rp.y + rowH), sel ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 11), 10.0f);
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            // Avatar (groups: two overlapping discs) + presence dot.
            ImVec2 ac(folded ? rp.x + w * 0.5f : rp.x + 8.0f + A * 0.5f, rp.y + rowH * 0.5f);
            if (r.gid) {
                ImVec4 nc = NameColor(r.name);
                rdl->AddCircleFilled(ImVec2(ac.x - 6, ac.y - 6), A * 0.34f, ImGui::GetColorU32(kAccentDim), 24);
                rdl->AddCircleFilled(ImVec2(ac.x + 6, ac.y + 6), A * 0.34f + 2.5f, ImGui::GetColorU32(kChatSideBg), 24);
                rdl->AddCircleFilled(ImVec2(ac.x + 6, ac.y + 6), A * 0.34f, ImGui::GetColorU32(ImVec4(nc.x * 0.6f, nc.y * 0.6f, nc.z * 0.6f, 1.0f)), 24);
                char init[2] = {r.name.empty() ? '#' : (char)std::toupper((unsigned char)r.name[0]), 0};
                ImVec2 ts = ImGui::CalcTextSize(init);
                rdl->AddText(ImVec2(ac.x + 6 - ts.x * 0.5f, ac.y + 6 - ts.y * 0.5f), IM_COL32_WHITE, init);
            } else {
                SDL_Texture* tex = AvatarTexture(r.avatar);
                if (tex) rdl->AddImageRounded((ImTextureID)(intptr_t)tex, ImVec2(ac.x - A * 0.5f, ac.y - A * 0.5f),
                                              ImVec2(ac.x + A * 0.5f, ac.y + A * 0.5f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, A * 0.5f);
                else {
                    ImVec4 nc = NameColor(r.name);
                    rdl->AddCircleFilled(ac, A * 0.5f, ImGui::GetColorU32(ImVec4(nc.x * 0.55f, nc.y * 0.55f, nc.z * 0.55f, 1)), 28);
                    char init[2] = {r.name.empty() ? '?' : (char)std::toupper((unsigned char)r.name[0]), 0};
                    ImGui::SetWindowFontScale(1.15f);
                    ImVec2 ts = ImGui::CalcTextSize(init);
                    rdl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(ac.x - ts.x * 0.5f, ac.y - ts.y * 0.5f), IM_COL32_WHITE, init);
                    ImGui::SetWindowFontScale(1.0f);
                }
                if (r.online && !g_blockedUsers.count(r.uid)) {
                    ImVec2 pd(ac.x + A * 0.35f, ac.y + A * 0.35f);
                    rdl->AddCircleFilled(pd, 7.0f, ImGui::GetColorU32(sel ? kChatSideSel : kChatSideBg), 16);
                    rdl->AddCircleFilled(pd, 5.0f, IM_COL32(76, 200, 110, 255), 16);
                } else if (r.lastSeen > 0 && !g_blockedUsers.count(r.uid)) {
                    // Recently active (Messenger-style): a tiny "12m" / "3h" tag on the avatar.
                    const long ago = std::max(0L, (long)std::time(nullptr) - r.lastSeen);
                    if (ago < 86400) {
                        char tag[8];
                        if (ago < 3600) std::snprintf(tag, sizeof(tag), "%ldm", std::max(1L, ago / 60));
                        else std::snprintf(tag, sizeof(tag), "%ldh", ago / 3600);
                        ImFont* tf = ImGui::GetFont(); const float fs = ImGui::GetFontSize() * 0.68f;
                        const ImVec2 ts = tf->CalcTextSizeA(fs, FLT_MAX, 0.0f, tag);
                        const ImVec2 tc(ac.x + A * 0.30f, ac.y + A * 0.36f);
                        const ImVec2 t0(tc.x - ts.x * 0.5f - 4, tc.y - ts.y * 0.5f - 1), t1(tc.x + ts.x * 0.5f + 4, tc.y + ts.y * 0.5f + 1);
                        rdl->AddRectFilled(ImVec2(t0.x - 2, t0.y - 2), ImVec2(t1.x + 2, t1.y + 2), ImGui::GetColorU32(sel ? kChatSideSel : kChatSideBg), 9.0f);
                        rdl->AddRectFilled(t0, t1, IM_COL32(38, 62, 44, 255), 8.0f);
                        rdl->AddText(tf, fs, ImVec2(tc.x - ts.x * 0.5f, tc.y - ts.y * 0.5f), IM_COL32(120, 214, 130, 255), tag);
                    }
                }
            }

            if (folded) {   // avatar only: unread badge on the top-right of the avatar
                if (r.unread) {
                    ImVec2 ud(ac.x + A * 0.36f, ac.y - A * 0.36f);
                    rdl->AddCircleFilled(ud, 7.0f, ImGui::GetColorU32(sel ? kChatSideSel : kChatSideBg), 16);
                    rdl->AddCircleFilled(ud, 5.0f, muted ? IM_COL32(120, 128, 145, 255) : ImGui::GetColorU32(kAccent), 16);
                }
                if (sel) rdl->AddRectFilled(ImVec2(rp.x, rp.y + 14), ImVec2(rp.x + 3, rp.y + rowH - 14), ImGui::GetColorU32(kAccent), 1.5f);
                ImGui::PopID();
                return;
            }
            // Text column: name + time, then preview / draft + markers.
            const float tx = rp.x + 8.0f + A + 12.0f, right = rp.x + w - 10.0f;
            const float y1 = rp.y + 11.0f, y2 = rp.y + 33.0f;
            std::string rt = r.starter ? std::string() : RelTime(r.when);
            if (rt.size() > 4 && rt.compare(rt.size() - 4, 4, " ago") == 0) rt.resize(rt.size() - 4);   // "5m", "2h", "3d"
            else if (rt == "just now") rt = "now";
            float rtw = rt.empty() ? 0.0f : ImGui::CalcTextSize(rt.c_str()).x;
            if (!rt.empty()) rdl->AddText(ImVec2(right - rtw, y1 + 1), r.unread && !muted ? ImGui::GetColorU32(accLt) : cFaint, rt.c_str());
            std::string nm = FitText(r.name, right - tx - rtw - 8.0f);
            rdl->AddText(ImVec2(tx, y1), cText, nm.c_str());
            if (r.unread) rdl->AddText(ImVec2(tx + 0.6f, y1), cText, nm.c_str());   // faux-bold when unread
            // Markers on the preview line (right to left): unread dot, muted, pinned.
            float mx = right;
            if (r.unread) {
                ImU32 uc = muted ? IM_COL32(120, 128, 145, 255) : ImGui::GetColorU32(kAccent);
                rdl->AddCircleFilled(ImVec2(mx - 5.0f, y2 + 8.0f), 5.0f, uc, 16);
                mx -= 16.0f;
            }
            if (muted) {   // bell with a slash
                ImVec2 bc(mx - 5.0f, y2 + 8.0f);
                rdl->PathArcTo(ImVec2(bc.x, bc.y - 0.5f), 3.4f, 3.14159f, 6.28318f, 8);
                rdl->PathLineTo(ImVec2(bc.x + 3.4f, bc.y + 2.6f)); rdl->PathLineTo(ImVec2(bc.x - 3.4f, bc.y + 2.6f));
                rdl->PathStroke(cFaint, ImDrawFlags_Closed, 1.3f);
                rdl->AddLine(ImVec2(bc.x - 5.0f, bc.y - 5.0f), ImVec2(bc.x + 5.0f, bc.y + 5.0f), cFaint, 1.4f);
                mx -= 16.0f;
            }
            if (pinned) {   // push-pin
                ImVec2 pc(mx - 5.0f, y2 + 6.0f);
                rdl->AddCircleFilled(pc, 2.8f, cFaint, 10);
                rdl->AddLine(ImVec2(pc.x - 1.0f, pc.y + 1.0f), ImVec2(pc.x - 5.0f, pc.y + 5.0f), cFaint, 1.5f);
                mx -= 14.0f;
            }
            const float pw = mx - tx - 6.0f;
            auto dr = drafts.find(r.key);
            if (dr != drafts.end() && !dr->second.empty() && !sel) {
                const char* lbl = "Draft: ";
                float lw = ImGui::CalcTextSize(lbl).x;
                rdl->AddText(ImVec2(tx, y2), IM_COL32(240, 120, 120, 255), lbl);
                rdl->AddText(ImVec2(tx + lw, y2), cDim, FitText(ChatPreview(dr->second, 90), pw - lw).c_str());
            } else if (sel && !r.gid && peerTyping) {
                rdl->AddText(ImVec2(tx, y2), ImGui::GetColorU32(accLt), "typing\xE2\x80\xA6");
            } else {
                DrawRichLine(rdl, ImVec2(tx, y2), r.unread ? IM_COL32(214, 220, 232, 255) : cDim, r.preview, pw);
            }
            ImGui::PopID();
        };

        int shown = 0, archivedCount = 0, starters = 0;
        for (const auto& r : rows) {
            if (!r.gid && !r.starter && g_archivedChats.count(r.uid)) { ++archivedCount; continue; }
            if (r.starter) { ++starters; continue; }
            if (!passes(r)) continue;
            drawRow(r); ++shown;
        }
        // Friends you haven't messaged yet (All tab only).
        if (convTab == 0 && starters > 0) {
            bool header = false;
            for (const auto& r : rows) {
                if (!r.starter || !passes(r)) continue;
                if (!header && folded) {
                    header = true;
                    ImVec2 lp = ImGui::GetCursorScreenPos();
                    rdl->AddLine(ImVec2(lp.x + 10, lp.y + 4), ImVec2(lp.x + ImGui::GetContentRegionAvail().x - 10, lp.y + 4), IM_COL32(255, 255, 255, 18), 1.0f);
                    ImGui::Dummy(ImVec2(0, 10));
                }
                if (!header) {
                    header = true;
                    ImGui::Dummy(ImVec2(0, 6));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);
                    ImGui::SetWindowFontScale(0.80f);
                    ImGui::TextDisabled("START A CHAT");
                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::Dummy(ImVec2(0, 2));
                }
                drawRow(r); ++shown;
            }
        }
        // Archived chats (collapsed behind one row).
        if (archivedCount > 0 && convTab == 0 && !folded) {
            ImGui::Dummy(ImVec2(0, 4));
            ImVec2 ap = ImGui::GetCursorScreenPos();
            float aw = ImGui::GetContentRegionAvail().x, ah = 34.0f;
            if (ImGui::InvisibleButton("##archrow", ImVec2(aw, ah))) showArchived = !showArchived;
            bool ahov = ImGui::IsItemHovered();
            if (ahov) rdl->AddRectFilled(ap, ImVec2(ap.x + aw, ap.y + ah), IM_COL32(255, 255, 255, 11), 8.0f);
            ImVec2 bx(ap.x + 8.0f + 22.0f - 7.0f, ap.y + ah * 0.5f);   // archive box icon
            rdl->AddRect(ImVec2(bx.x - 1, bx.y - 6), ImVec2(bx.x + 15, bx.y - 2), cDim, 1.5f, 0, 1.4f);
            rdl->AddRect(ImVec2(bx.x + 1, bx.y - 2), ImVec2(bx.x + 13, bx.y + 7), cDim, 1.5f, 0, 1.4f);
            rdl->AddLine(ImVec2(bx.x + 5, bx.y + 1.5f), ImVec2(bx.x + 9, bx.y + 1.5f), cDim, 1.4f);
            char al[40]; std::snprintf(al, sizeof(al), "Archived  (%d)", archivedCount);
            rdl->AddText(ImVec2(ap.x + 64.0f, ap.y + (ah - ImGui::GetTextLineHeight()) * 0.5f), cDim, al);
            rdl->AddText(ImVec2(ap.x + aw - 18, ap.y + (ah - ImGui::GetTextLineHeight()) * 0.5f), cFaint, showArchived ? "\xE2\x80\x93" : "+");
            if (showArchived)
                for (const auto& r : rows)
                    if (!r.gid && !r.starter && g_archivedChats.count(r.uid) && passes(r)) drawRow(r);
        }
        if (shown == 0 && !folded) {   // empty state
            float aw = ImGui::GetContentRegionAvail().x;
            ImGui::Dummy(ImVec2(0, 28));
            ImVec2 c(ImGui::GetCursorScreenPos().x + aw * 0.5f, ImGui::GetCursorScreenPos().y + 26);
            rdl->AddCircleFilled(c, 26.0f, IM_COL32(255, 255, 255, 14), 32);
            rdl->PathArcTo(c, 12.0f, 3.14159f * 0.80f, 3.14159f * 2.62f, 26);
            rdl->PathLineTo(ImVec2(c.x - 12.0f, c.y + 12.0f));
            rdl->PathStroke(cDim, ImDrawFlags_Closed, 2.0f);
            ImGui::Dummy(ImVec2(0, 62));
            auto centered = [&](const char* s, ImU32 col) {
                float tw = ImGui::CalcTextSize(s).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (aw - tw) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(col));
                ImGui::TextUnformatted(s);
                ImGui::PopStyleColor();
            };
            if (convFilter[0]) centered("No chats match your search", cText);
            else if (convTab == 1) { centered("You're all caught up", cText); centered("No unread chats.", cDim); }
            else if (convTab == 2) { centered("No groups yet", cText); centered("Tap + to start one.", cDim); }
            else { centered("No conversations yet", cText); centered("Add friends, then say hi.", cDim); }
        }
        ImGui::EndChild();   // ##convrows
        ImGui::EndChild();   // ##chatlist
        ImGui::PopStyleColor();
        {   // hairline between the list and the thread
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(b.x, a.y), ImVec2(b.x, b.y), IM_COL32(255, 255, 255, 14), 1.0f);
        }
        ImGui::SameLine(0, 0);

        // ---- Right: the selected conversation ---------------------------------
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 10));
        ImGui::BeginChild("##chatconv", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        // ---- Tabs: one per open conversation (DMs + groups) ----------------------
        if (!chatTabs.empty()) {
            const std::string curKey = g_chatSelGroup > 0 ? "g:" + std::to_string(g_chatSelGroup) : selUid;
            int curIdx = -1;
            for (std::size_t i = 0; i < chatTabs.size(); ++i) if (chatTabs[i].key == curKey) curIdx = (int)i;
            int openIdx = -1, closeIdx = -1;
            // Ctrl+PgDn / Ctrl+PgUp cycle tabs, Ctrl+W closes the open one.
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl) {
                const int n = (int)chatTabs.size();
                if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) openIdx = (curIdx + 1) % n;
                if (ImGui::IsKeyPressed(ImGuiKey_PageUp))   openIdx = curIdx <= 0 ? n - 1 : curIdx - 1;
                if (ImGui::IsKeyPressed(ImGuiKey_W, false) && curIdx >= 0) closeIdx = curIdx;
            }
            const float tabH = 32.0f;
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 4.0f);
            ImGui::BeginChild("##chattabs", ImVec2(0, tabH + 6), false,
                              ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar();
            if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)   // wheel scrolls the strip sideways
                ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseWheel * 48.0f);
            ImDrawList* td = ImGui::GetWindowDrawList();
            const ImVec2 start = ImGui::GetCursorScreenPos();
            const float th = ImGui::GetTextLineHeight();
            float x = start.x;
            for (std::size_t i = 0; i < chatTabs.size(); ++i) {
                const ChatTab& t = chatTabs[i];
                const bool active = (int)i == curIdx;
                const bool un = !active && unreadKeys.count(t.key) != 0;
                // Live name: nickname > friend's name > name learned when opened > stored.
                std::string nm = t.name;
                if (!t.gid) {
                    if (auto nk = g_friendNicks.find(t.uid); nk != g_friendNicks.end() && !nk->second.empty()) nm = nk->second;
                    else if (auto fi = fById.find(t.uid); fi != fById.end()) nm = fi->second->display.empty() ? fi->second->username : fi->second->display;
                    else if (auto kn = knownNames.find(t.uid); kn != knownNames.end()) nm = kn->second;
                    else if (t.uid == selUid && !selName.empty()) nm = selName;
                } else for (const auto& gc : groups) if (gc.id == t.gid && !gc.title.empty()) nm = gc.title;
                if (nm.empty()) nm = t.gid ? "Group" : "Chat";
                // avatar (36) + name + unread dot (12) + close (18) + margins (24)
                const float tw = std::clamp(ImGui::CalcTextSize(nm.c_str()).x + 80.0f + (un ? 12.0f : 0.0f), 118.0f, 220.0f);
                ImVec2 p(x, start.y);
                ImGui::PushID(t.key.c_str());
                ImGui::SetCursorScreenPos(p);
                ImGui::SetNextItemAllowOverlap();
                if (ImGui::InvisibleButton("##tab", ImVec2(tw, tabH))) openIdx = (int)i;
                const bool hov = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) closeIdx = (int)i;
                if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                td->AddRectFilled(p, ImVec2(p.x + tw, p.y + tabH),
                                  active ? IM_COL32(255, 255, 255, 26) : hov ? IM_COL32(255, 255, 255, 15) : IM_COL32(255, 255, 255, 7), 8.0f);
                if (active) td->AddRectFilled(ImVec2(p.x + 10, p.y + tabH - 2.5f), ImVec2(p.x + tw - 10, p.y + tabH),
                                              ImGui::GetColorU32(kAccent), 1.5f);
                // Mini avatar.
                const float A = 20.0f;
                ImVec2 ac(p.x + 8.0f + A * 0.5f, p.y + tabH * 0.5f);
                if (t.gid) {
                    td->AddCircleFilled(ImVec2(ac.x - 3, ac.y - 3), 6.5f, ImGui::GetColorU32(kAccentDim), 16);
                    ImVec4 nc = NameColor(nm);
                    td->AddCircleFilled(ImVec2(ac.x + 3, ac.y + 3), 8.0f, ImGui::GetColorU32(kChatWinBg), 16);
                    td->AddCircleFilled(ImVec2(ac.x + 3, ac.y + 3), 6.5f, ImGui::GetColorU32(ImVec4(nc.x * 0.6f, nc.y * 0.6f, nc.z * 0.6f, 1)), 16);
                } else if (SDL_Texture* at = AvatarTexture(t.avatar)) {
                    td->AddImageRounded((ImTextureID)(intptr_t)at, ImVec2(ac.x - A * 0.5f, ac.y - A * 0.5f), ImVec2(ac.x + A * 0.5f, ac.y + A * 0.5f),
                                        ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, A * 0.5f);
                } else {
                    ImVec4 nc = NameColor(nm);
                    td->AddCircleFilled(ac, A * 0.5f, ImGui::GetColorU32(ImVec4(nc.x * 0.55f, nc.y * 0.55f, nc.z * 0.55f, 1)), 20);
                    char init[2] = {(char)std::toupper((unsigned char)nm[0]), 0};
                    float fs = ImGui::GetFontSize() * 0.78f;
                    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, init);
                    td->AddText(ImGui::GetFont(), fs, ImVec2(ac.x - ts.x * 0.5f, ac.y - ts.y * 0.5f), IM_COL32_WHITE, init);
                }
                // Name, unread dot, close button.
                const float closeW = 18.0f;
                float nameMax = tw - (A + 16.0f) - closeW - 12.0f - (un ? 12.0f : 0.0f);
                td->AddText(ImVec2(p.x + A + 16.0f, p.y + (tabH - th) * 0.5f), (active || hov) ? cText : cDim,
                            FitText(nm, nameMax).c_str());
                if (un) td->AddCircleFilled(ImVec2(p.x + tw - closeW - 14.0f, p.y + tabH * 0.5f), 4.0f, ImGui::GetColorU32(kAccent), 12);
                ImVec2 xp(p.x + tw - closeW - 6.0f, p.y + (tabH - closeW) * 0.5f);
                ImGui::SetCursorScreenPos(xp);
                if (ImGui::InvisibleButton("##x", ImVec2(closeW, closeW))) closeIdx = (int)i;
                const bool xh = ImGui::IsItemHovered();
                if (xh) ImGui::SetTooltip("Close  (Ctrl+W)");
                if (active || hov || xh) {
                    ImVec2 xc(xp.x + closeW * 0.5f, xp.y + closeW * 0.5f);
                    if (xh) td->AddCircleFilled(xc, closeW * 0.5f, IM_COL32(255, 255, 255, 30), 16);
                    ImU32 xcCol = xh ? cText : cDim;
                    td->AddLine(ImVec2(xc.x - 3.5f, xc.y - 3.5f), ImVec2(xc.x + 3.5f, xc.y + 3.5f), xcCol, 1.5f);
                    td->AddLine(ImVec2(xc.x + 3.5f, xc.y - 3.5f), ImVec2(xc.x - 3.5f, xc.y + 3.5f), xcCol, 1.5f);
                }
                ImGui::PopID();
                x += tw + 4.0f;
            }
            ImGui::SetCursorScreenPos(start);
            ImGui::Dummy(ImVec2(x - start.x, tabH));   // content width -> horizontal scrolling
            ImGui::EndChild();
            ImGui::Dummy(ImVec2(0, 4));
            if (closeIdx >= 0) closeTab((std::size_t)closeIdx);
            else if (openIdx >= 0 && openIdx != curIdx) {
                const ChatTab t = chatTabs[(std::size_t)openIdx];
                if (t.gid) openGroup(t.gid); else openConv(t.uid, t.name, t.avatar, false);
            }
        }
        if (g_chatSelGroup > 0) {
            DrawGroupConversation(dm, groups, glog, greacts, gmyreacts, fById,
                                  gInput, gEditId, gReplyTo, gReplyName, gReplySnippet, gSeenId,
                                  account.CurrentSession().username);
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
            if (auto nk = g_friendNicks.find(selUid); nk != g_friendNicks.end() && !nk->second.empty())
                selName = nk->second;
            const bool selBlocked = g_blockedUsers.count(selUid) != 0;
            const ImVec4 myBubble = ChatThemeFor(selUid);   // this chat's theme colour for my bubbles
            // Header: avatar with a presence dot, name + status, and actions.
            ImVec2 avp = ImGui::GetCursorScreenPos();
            {   // Distinct header zone: a subtle strip + bottom border. Drawn first
                // so the avatar/name/buttons (added to the same draw list after)
                // render on top of it.
                float hw = ImGui::GetContentRegionAvail().x;
                ImDrawList* hbg = ImGui::GetWindowDrawList();
                hbg->AddRectFilled(ImVec2(avp.x - 6, avp.y - 6), ImVec2(avp.x + hw + 6, avp.y + 40),
                                   IM_COL32(26, 29, 36, 150));
                hbg->AddLine(ImVec2(avp.x - 6, avp.y + 40), ImVec2(avp.x + hw + 6, avp.y + 40),
                             IM_COL32(60, 66, 78, 200), 1.0f);
            }
            DrawAvatar(selAvatar, selName, 34.0f);
            {   // presence dot on the avatar's corner
                ImDrawList* hd = ImGui::GetWindowDrawList();
                ImU32 dot = (selOnline && !selBlocked) ? IM_COL32(120, 200, 110, 255) : IM_COL32(120, 128, 140, 255);
                hd->AddCircleFilled(ImVec2(avp.x + 28, avp.y + 28), 5.5f, dot, 16);
                hd->AddCircle(ImVec2(avp.x + 28, avp.y + 28), 5.5f,
                              ImGui::GetColorU32(ImGuiCol_WindowBg), 16, 2.0f);
            }
            ImGui::SameLine(); ImGui::BeginGroup();
            ImGui::TextUnformatted(selName.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(selBlocked ? ImVec4(0.92f,0.45f,0.45f,1)
                                        : peerTyping ? kAccent
                                        : ((selOnline || peerHere) ? ImVec4(0.47f,0.80f,0.43f,1) : ImVec4(0.6f,0.64f,0.7f,1))));
            const std::string sub = selBlocked ? std::string("Blocked") : peerTyping ? std::string("typing\xE2\x80\xA6")
                                  : peerHere ? std::string("In the chat") : selOnline ? std::string("Active now")
                                  : peerLastSeen > 978307200 ? "Last seen " + RelTime(peerLastSeen) : std::string("Offline");   // (Invisible backdates to 2000)
            ImGui::TextUnformatted(sub.c_str());
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
                // Messenger-style theme: colour of your bubbles in this chat.
                if (ImGui::BeginMenu("Theme colour")) {
                    int cur = g_chatColor.count(selUid) ? g_chatColor[selUid] : 0;
                    for (int ci = 0; ci < kChatThemeCount; ++ci) {
                        ImVec2 sp = ImGui::GetCursorScreenPos();
                        float lh = ImGui::GetTextLineHeight();
                        if (ImGui::MenuItem((std::string("      ") + kChatThemeNames[ci] + "##th" + std::to_string(ci)).c_str(),
                                            nullptr, cur == ci)) {
                            if (ci == 0) g_chatColor.erase(selUid); else g_chatColor[selUid] = ci;
                            SavePrefs();
                        }
                        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(sp.x + 9, sp.y + lh * 0.5f), 6.0f,
                                                                    ImGui::GetColorU32(ChatThemeColor(ci)), 16);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Nickname")) {
                    static char nickBuf[48] = ""; static std::string nickFor;
                    if (nickFor != selUid) {   // prefill with the current nickname
                        nickFor = selUid;
                        auto nk = g_friendNicks.find(selUid);
                        std::snprintf(nickBuf, sizeof(nickBuf), "%s", nk != g_friendNicks.end() ? nk->second.c_str() : "");
                    }
                    ImGui::SetNextItemWidth(180);
                    ImGui::InputTextWithHint("##nick", "Nickname (only you see it)", nickBuf, sizeof(nickBuf));
                    if (ImGui::Button("Save##nick")) {
                        if (nickBuf[0]) g_friendNicks[selUid] = nickBuf; else g_friendNicks.erase(selUid);
                        SavePrefs(); ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Clear##nick")) { g_friendNicks.erase(selUid); nickBuf[0] = '\0'; SavePrefs(); }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Starred messages")) openStarred = true;
                if (ImGui::MenuItem("Media & links")) openMedia = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Copy username")) ImGui::SetClipboardText(selName.c_str());
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1.0f));
                if (ImGui::MenuItem(selBlocked ? "Unblock" : "Block")) {
                    if (selBlocked) g_blockedUsers.erase(selUid); else g_blockedUsers.insert(selUid);
                    SavePrefs();
                }
                ImGui::PopStyleColor();
                ImGui::EndPopup();
            }
            // Starred messages in this conversation (WhatsApp "Starred"): click to jump.
            if (openStarred) { ImGui::OpenPopup("##starredpop"); openStarred = false; }
            ImGui::SetNextWindowSizeConstraints(ImVec2(280, 0), ImVec2(420, 360));
            if (ImGui::BeginPopup("##starredpop")) {
                ImGui::TextDisabled("Starred messages");
                ImGui::Separator();
                std::vector<std::pair<long, const StarredMsg*>> mine;
                for (const auto& kv : g_starred) if (kv.second.uid == selUid) mine.push_back({kv.first, &kv.second});
                std::sort(mine.begin(), mine.end(), [](const auto& a, const auto& b) { return a.second->when > b.second->when; });
                long unstar = 0;
                for (const auto& s : mine) {
                    ImGui::PushID((int)s.first);
                    ImVec2 sp = ImGui::GetCursorScreenPos();
                    if (ImGui::Selectable(("      " + s.second->preview).c_str(), false, 0, ImVec2(ImGui::GetContentRegionAvail().x - 26, 0))) {
                        scrollToId = s.first; flashId = s.first; flashUntil = now + 1400;
                        ImGui::CloseCurrentPopup();
                    }
                    DrawStar(ImGui::GetWindowDrawList(), ImVec2(sp.x + 8, sp.y + ImGui::GetTextLineHeight() * 0.5f),
                             6.0f, IM_COL32(250, 200, 70, 255));
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) unstar = s.first;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unstar");
                    ImGui::PopID();
                }
                if (unstar) { g_starred.erase(unstar); SavePrefs(); }
                if (mine.empty()) ImGui::TextDisabled("No starred messages yet.\nRight-click a message > Star.");
                ImGui::EndPopup();
            }
            // Shared media & links in this conversation (Messenger/WhatsApp
            // "Media, links and docs"): an image grid (click -> lightbox) and a
            // link list (click -> browser), newest first.
            if (openMedia) { ImGui::OpenPopup("##mediapop"); openMedia = false; }
            ImGui::SetNextWindowSizeConstraints(ImVec2(360, 0), ImVec2(460, 420));
            if (ImGui::BeginPopup("##mediapop")) {
                std::vector<std::string> imgs, links;
                for (auto it = log.rbegin(); it != log.rend(); ++it) {
                    if (selBlocked && !it->mine) continue;
                    std::string iu = FirstImageUrl(it->body);
                    if (!iu.empty()) imgs.push_back(iu);
                    std::size_t i = 0, n = it->body.size();
                    while (i < n) {   // every http(s) token that isn't the image
                        while (i < n && (it->body[i] == ' ' || it->body[i] == '\n')) ++i;
                        std::size_t j = i; while (j < n && it->body[j] != ' ' && it->body[j] != '\n') ++j;
                        std::string tok = it->body.substr(i, j - i); i = j;
                        if ((tok.rfind("http://", 0) == 0 || tok.rfind("https://", 0) == 0) && tok != iu) links.push_back(tok);
                    }
                }
                ImGui::TextDisabled("Media (%d)", (int)imgs.size());
                ImGui::Separator();
                if (imgs.empty()) ImGui::TextDisabled("No photos shared yet.");
                const float th = 76.0f;
                int col = 0, perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + 6) / (th + 6)));
                for (std::size_t k = 0; k < imgs.size(); ++k) {
                    ImGui::PushID((int)k);
                    if (col > 0) ImGui::SameLine(0, 6);
                    ImVec2 tp = ImGui::GetCursorScreenPos();
                    if (ImGui::InvisibleButton("##thumb", ImVec2(th, th))) { lightboxUrl = imgs[k]; ImGui::CloseCurrentPopup(); }
                    ImDrawList* md = ImGui::GetWindowDrawList();
                    if (SDL_Texture* tx = AvatarTexture(imgs[k]))
                        md->AddImageRounded((ImTextureID)(intptr_t)tx, tp, ImVec2(tp.x + th, tp.y + th),
                                            ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8.0f);
                    else
                        md->AddRectFilled(tp, ImVec2(tp.x + th, tp.y + th), IM_COL32(44, 48, 58, 255), 8.0f);
                    if (ImGui::IsItemHovered()) {
                        md->AddRect(tp, ImVec2(tp.x + th, tp.y + th), ImGui::GetColorU32(kAccent), 8.0f, 0, 2.0f);
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    }
                    ImGui::PopID();
                    if (++col >= perRow) col = 0;
                }
                ImGui::Spacing();
                ImGui::TextDisabled("Links (%d)", (int)links.size());
                ImGui::Separator();
                if (links.empty()) ImGui::TextDisabled("No links shared yet.");
                for (std::size_t k = 0; k < links.size(); ++k) {
                    ImGui::PushID((int)(1000 + k));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.78f, 1.0f, 1.0f));
                    if (ImGui::Selectable(ChatPreview(links[k], 52).c_str())) { OpenExternal(links[k]); ImGui::CloseCurrentPopup(); }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", links[k].c_str());
                    ImGui::PopID();
                }
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
            if (g_chatMutex && !kChatDemo) {
                SDL_LockMutex(g_chatMutex);
                g_chatSelUid = selUid;
                if (g_chatLogReady) {
                    log = g_chatLog; reacts = g_chatReacts; myReacts = g_chatMyReacts;
                    g_chatLogReady = false;
                }
                if (g_chatPeerStateUid == selUid) {
                    peerRead = g_chatPeerRead; peerHere = g_chatPeerHere; peerLastSeen = g_chatPeerLastSeen;
                } else { peerRead = 0; peerHere = false; peerLastSeen = 0; }
                SDL_UnlockMutex(g_chatMutex);
            }
            peerTyping = g_chatPeerTyping;
            // We're looking at this conversation (window focused, not minimised):
            // lets the poller mark it read and tell the peer we're in the chat.
            if (!selUid.empty() && SDL_GetKeyboardFocus() != nullptr) g_chatViewMs = SDL_GetTicks();
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
            // 1 sent, 2 delivered (they've been online / in the chat since), 3 read.
            const long nowT = (long)std::time(nullptr);
            auto deliveryState = [&](long id, long when) -> int {
                if (id <= peerRead) return 3;
                if (peerHere || (peerLastSeen > 0 && peerLastSeen >= when) || (selOnline && nowT - when >= 6)) return 2;
                return 1;
            };

            // Reserve space for the composer: a slim format toolbar row + the input
            // pill (grows 1..4 lines) + the optional reply/edit banner.
            int clines = 1; for (const char* c = input; *c; ++c) if (*c == '\n') ++clines;
            if (clines > 4) clines = 4;
            // Measured from the style (toolbar row + spacing + input pill + bottom
            // window padding) — the old fixed estimate was ~20px short and
            // clipped the bottom of the input box.
            const ImGuiStyle& cst = ImGui::GetStyle();
            float composerH = ImGui::GetFrameHeightWithSpacing()                      // toolbar row
                              + (ImGui::GetTextLineHeight() * clines + 16.0f)          // input pill
                              + cst.ItemSpacing.y * 2.0f + cst.WindowPadding.y + 4.0f
                              + (replyTo || editId ? ImGui::GetTextLineHeightWithSpacing() + 4.0f : 0.0f);
            bool scrolledUp = false; ImVec2 logRectMin, logRectMax;
            {   // Composer grew/shrank (multi-line): stay pinned to the newest message.
                static int prevClines = 1;
                if (clines != prevClines) { prevClines = clines; wantScrollBottom = true; }
            }
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
            const float avatarW = 28.0f, avatarGap = 6.0f, padX = 14.0f, padY = 8.0f;
            int matches = 0;
            if (selBlocked) {   // blocked: their messages are hidden (only yours remain)
                ImGui::Dummy(ImVec2(0, 6));
                const char* bl = "You blocked this person. Their messages are hidden.";
                ImGui::SetCursorPosX(baseX + (avW - ImGui::CalcTextSize(bl).x) * 0.5f);
                ImGui::TextDisabled("%s", bl);
            }
            for (const auto& m : log) {
                if (selBlocked && !m.mine) continue;
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
                ChatFileInfo fileInfo; const bool isFile = ParseChatFile(m.body, fileInfo);   // "[File] ..." -> card
                ChatVoiceInfo voice; const bool isVoice = !isFile && ParseChatVoice(m.body, voice);
                ChatPollInfo pollInfo; const bool isPoll = !isFile && !isVoice && ParseChatPoll(m.body, pollInfo);
                float bodyW = isFile ? ChatFileCardWidth(fileInfo, bubbleMax - padX*2)
                            : isVoice ? ChatVoiceWidth(bubbleMax - padX*2)
                            : isPoll ? ChatPollWidth(bubbleMax - padX*2) : MeasureChatBodyWidth(m.body, bubbleMax - padX*2);
                // reply quote + time contribute to width
                const okay::DirectMessage* rep = (m.replyTo && byId.count(m.replyTo)) ? byId[m.replyTo] : nullptr;
                std::string repText = rep ? ChatPreview(rep->body, 36) : "";
                float repW = rep ? std::min(bubbleMax - padX*2, ImGui::CalcTextSize(repText.c_str()).x + 8) : 0.0f;
                char tbuf[24] = ""; if (m.when>0){ std::time_t tt=(std::time_t)m.when; std::tm* tm=std::localtime(&tt); if(tm) std::strftime(tbuf,sizeof(tbuf),"%H:%M",tm); }
                std::string meta = tbuf; if (m.edited) meta += " \xC2\xB7 edited";
                float metaW = ImGui::CalcTextSize(meta.c_str()).x;
                if (m.mine) metaW += 18.0f;           // room for the delivery/read ticks
                const bool starred = g_starred.count(m.id) != 0;
                if (starred) metaW += 14.0f;          // room for the star marker
                bool bareEmoji = emojiOnly && !rep;   // Telegram-style big, background-less emoji
                // Stickers and image/GIF-only messages also go bubble-less (Messenger).
                std::string mediaUrl; bool isSticker = false;
                if (!rep && !isFile && !isVoice) {
                    if (m.body.rfind("[Sticker] ", 0) == 0) { mediaUrl = m.body.substr(10); isSticker = true; }
                    else {
                        std::string t = m.body;
                        while (!t.empty() && (t.back() == ' ' || t.back() == '\n')) t.pop_back();
                        while (!t.empty() && t.front() == ' ') t.erase(0, 1);
                        if (!t.empty() && FirstImageUrl(t) == t) mediaUrl = t;
                    }
                }
                const bool bareMedia = !mediaUrl.empty();
                const bool bare = bareEmoji || bareMedia;
                ImVec2 mediaSz(0, 0);
                if (bareMedia) {
                    int iw = 0, ih = 0; ChatImageTexture(mediaUrl, &iw, &ih);
                    const float mw = isSticker ? 150.0f : std::min(bubbleMax - padX * 2, 260.0f), mh = isSticker ? 150.0f : 220.0f;
                    if (iw > 0 && ih > 0) { float k = std::min(mw / iw, mh / ih); if (!isSticker) k = std::min(k, 2.0f); mediaSz = ImVec2(iw * k, ih * k); }
                    else mediaSz = ImVec2(mw, isSticker ? mw : mh * 0.7f);
                }
                std::string imgUrl = (bare || isFile || isVoice) ? std::string() : FirstImageUrl(m.body);
                // +2px slack on the body so the glyphs never butt against the
                // content edge (absorbs wrap/measure rounding -> no "text spilling
                // out of the bubble").
                float contentW = bareEmoji ? std::max((float)ecount * (bigEm + 3), metaW)
                               : bareMedia ? std::max(mediaSz.x, metaW)
                                           : std::max({bodyW + 2.0f, repW, metaW});
                if (!imgUrl.empty()) contentW = std::max(contentW, std::min(bubbleMax - padX*2, 240.0f));
                float boxW = contentW + padX*2;
                float useW = std::max(boxW, 46.0f);   // actual bubble width (min clamp)
                float leftPad = m.mine ? 0.0f : (avatarW + avatarGap);
                // Right-align "mine" bubbles by the REAL bubble width (useW), not the
                // raw content width — otherwise short bubbles ("hi"/"ok") overhang
                // the right edge and drift when the window is resized.
                // (-8: a little breathing room from the scrollbar, like Messenger.)
                float x0 = m.mine ? (avW - useW - 8.0f) : leftPad; if (x0 < leftPad) x0 = leftPad;

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
                ImGui::PushStyleColor(ImGuiCol_ChildBg, bare ? ImVec4(0,0,0,0)
                                        : (m.mine ? myBubble : ImVec4(0.204f,0.235f,0.306f,1.0f)));
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, padY));
                // AlwaysUseWindowPadding: a border-less child ignores WindowPadding
                // by default (ImGui 1.92), which left the text flush against the
                // bubble edge — the rounded corner then clipped the first letter.
                ImGui::BeginChild("##bubble", ImVec2(useW, 0),
                                  ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
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
                    const std::string eb = NormalizeEmoji(m.body);
                    std::size_t bi = 0, bn = eb.size();
                    bool firstTok = true;
                    while (bi < bn) {
                        while (bi < bn && (eb[bi]==' '||eb[bi]=='\n')) ++bi;
                        if (bi >= bn) break;
                        std::size_t bj = bi; while (bj < bn && eb[bj]!=' ' && eb[bj]!='\n') ++bj;
                        std::string etok = eb.substr(bi, bj-bi); bi = bj;
                        if (!firstTok) ImGui::SameLine(0, 3);
                        ImVec2 ep = ImGui::GetCursorScreenPos();
                        DrawEmojiToken(bdl, ImVec2(ep.x + bigEm*0.5f, ep.y + bigEm*0.5f), bigEm, etok);
                        ImGui::Dummy(ImVec2(bigEm, bigEm));
                        firstTok = false;
                    }
                } else if (bareMedia) {                     // sticker / GIF / photo without a bubble
                    ImVec2 ip = ImGui::GetCursorScreenPos();
                    bool mclick = ImGui::InvisibleButton("##media", mediaSz);
                    bool mhov = ImGui::IsItemHovered();
                    if (SDL_Texture* mt = ChatImageTexture(mediaUrl))
                        bdl->AddImageRounded((ImTextureID)(intptr_t)mt, ip, ImVec2(ip.x + mediaSz.x, ip.y + mediaSz.y),
                                             ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, isSticker ? 0.0f : 12.0f);
                    else {   // loading placeholder
                        bdl->AddRectFilled(ip, ImVec2(ip.x + mediaSz.x, ip.y + mediaSz.y), IM_COL32(255, 255, 255, 14), 12.0f);
                        float a0 = (float)ImGui::GetTime() * 6.0f;
                        bdl->PathArcTo(ImVec2(ip.x + mediaSz.x * 0.5f, ip.y + mediaSz.y * 0.5f), 10.0f, a0, a0 + 4.6f, 20);
                        bdl->PathStroke(IM_COL32(255, 255, 255, 120), 0, 2.0f);
                    }
                    if (!isSticker && mhov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); if (mclick) lightboxUrl = mediaUrl; }
                } else {
                    if (isFile) RenderChatFileCard(fileInfo, ImGui::GetContentRegionAvail().x);
                    else if (isVoice) RenderChatVoice(voice, ImGui::GetContentRegionAvail().x, m.mine, myBubble);
                    else if (isPoll) {
                        auto rc = reacts.find(m.id); auto mr = myReacts.find(m.id);
                        RenderChatPoll(pollInfo, false, m.id, rc != reacts.end() ? &rc->second : nullptr,
                                       mr != myReacts.end() ? &mr->second : nullptr, ImGui::GetContentRegionAvail().x, m.mine, myBubble);
                    }
                    else RenderChatBody(m.body, ImGui::GetContentRegionAvail().x);
                }
                // Inline image preview for an image link in the message.
                if (!imgUrl.empty()) {
                    if (SDL_Texture* it = ChatImageTexture(imgUrl)) {
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
                {   // Timestamp: small + tucked at the bubble's bottom-right edge,
                    // leaving room for the delivery/read ticks on my own messages.
                    float av2 = ImGui::GetContentRegionAvail().x;
                    float mw2 = ImGui::CalcTextSize(meta.c_str()).x;
                    float tickRoom = m.mine ? 18.0f : 0.0f;
                    if (!bare && mw2 + tickRoom < av2) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (av2 - mw2 - tickRoom));
                    if (starred) {   // gold star just left of the timestamp
                        ImVec2 sp = ImGui::GetCursorScreenPos();
                        DrawStar(bdl, ImVec2(sp.x - 8.0f, sp.y + ImGui::GetTextLineHeight() * 0.5f), 5.0f,
                                 IM_COL32(250, 200, 70, 255));
                    }
                }
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(bare ? ImVec4(0.6f,0.64f,0.72f,0.9f)
                                        : (m.mine ? ImVec4(0.82f,0.88f,1.0f,0.60f) : ImVec4(0.60f,0.66f,0.76f,0.70f))));
                ImGui::TextUnformatted(meta.c_str());
                ImGui::PopStyleColor();
                // WhatsApp-style ticks after my timestamp: one grey check = sent,
                // two grey = delivered (they've been online since), two blue = read.
                if (m.mine && !bare) {
                    ImVec2 tmin = ImGui::GetItemRectMin(), tmax = ImGui::GetItemRectMax();
                    const int ds = deliveryState(m.id, m.when);
                    ImU32 tc = ds == 3 ? ImGui::GetColorU32(ImVec4(0.42f, 0.70f, 1.0f, 1.0f)) : IM_COL32(205, 214, 230, 150);
                    float bx = tmax.x + 4.0f, by = (tmin.y + tmax.y) * 0.5f;
                    auto tick = [&](float ox) {
                        bdl->AddLine(ImVec2(bx + ox, by + 1), ImVec2(bx + ox + 2.5f, by + 3.5f), tc, 1.3f);
                        bdl->AddLine(ImVec2(bx + ox + 2.5f, by + 3.5f), ImVec2(bx + ox + 7, by - 2.5f), tc, 1.3f);
                    };
                    if (ds >= 2) { tick(0.0f); tick(3.5f); } else tick(1.5f);
                    if (ImGui::IsMouseHoveringRect(ImVec2(tmin.x, tmin.y), ImVec2(bx + 12.0f, tmax.y)))
                        ImGui::SetTooltip("%s", ds == 3 ? "Read" : ds == 2 ? "Delivered" : "Sent");
                }
                ImGui::PopStyleVar();                       // ItemSpacing
                ImGui::EndChild();
                ImGui::PopStyleVar(2);                      // ChildRounding, WindowPadding
                ImGui::PopStyleColor();                     // ChildBg
                ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
                // (No bubble "tails": with fully rounded bubbles they float detached
                // off the corner — Messenger-style plain rounded bubbles read cleaner.)
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
                    // Star = save for later (WhatsApp); listed under Conversation > Starred.
                    if (ImGui::MenuItem(starred ? "Unstar" : "Star message")) {
                        if (starred) g_starred.erase(m.id);
                        else {
                            StarredMsg s; s.uid = selUid; s.when = m.when;
                            s.preview = ChatPreview(m.body, 60);
                            for (auto& ch : s.preview) if (ch == '\n' || ch == '\r') ch = ' ';
                            g_starred[m.id] = s;
                        }
                        SavePrefs();
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
                if (cit != reacts.end() && HasVisibleReactions(cit->second)) {
                    ImGui::SetCursorPosX(baseX + x0);
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
                    bool firstPill = true;
                    for (const auto& kv : cit->second) {
                        if (kv.second <= 0 || IsPollReaction(kv.first)) continue;
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
            // My messages that are still on their way (instant feedback).
            const int pendingOut = DrawOutbox(selUid, 0, avW, baseX, myBubble, [&](const std::string& body, long when) {
                for (auto it = log.rbegin(); it != log.rend(); ++it)
                    if (it->mine && it->body == body && it->when >= when - 120) return true;
                return false;
            });
            // Status under my latest message (Instagram / Messenger style).
            if (!log.empty() && log.back().mine && pendingOut == 0 && msFilt.empty()) {
                const int ds = deliveryState(log.back().id, log.back().when);
                const char* st = ds == 3 ? "Read" : ds == 2 ? "Delivered" : "Sent";
                const float sw = ImGui::CalcTextSize(st).x;
                ImGui::SetCursorPosX(baseX + avW - sw - 10.0f);
                ImGui::TextColored(ds == 3 ? ImVec4(0.55f, 0.76f, 1.0f, 0.95f) : ImVec4(0.60f, 0.64f, 0.72f, 0.90f), "%s", st);
            }
            // Snapchat-style presence: their avatar sits at the bottom of the
            // thread while they have this chat open (with the typing dots when
            // they type), and slides away when they leave.
            static float hereAnim = 0.0f;
            const bool showHere = (peerHere || peerTyping) && !selBlocked && msFilt.empty();
            hereAnim += (showHere ? 1.0f : -1.0f) * ImGui::GetIO().DeltaTime * 6.0f;
            hereAnim = std::clamp(hereAnim, 0.0f, 1.0f);
            if (hereAnim > 0.01f && !peerTyping) {
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::SetCursorPosX(baseX);
                ImVec2 hp = ImGui::GetCursorScreenPos();
                const float sz = 26.0f, a = hereAnim;
                const float pulse = 0.5f + 0.5f * std::sin((float)SDL_GetTicks() * 0.004f);
                const ImVec2 hc(hp.x + sz * 0.5f, hp.y + sz * 0.5f);
                dl->AddCircleFilled(hc, sz * 0.5f + 3.0f + pulse * 2.0f, IM_COL32(120, 200, 110, (int)(40 * a)), 28);
                dl->AddCircle(hc, sz * 0.5f + 1.5f, IM_COL32(120, 200, 110, (int)(230 * a)), 28, 2.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * a);
                DrawAvatar(selAvatar, selName, sz);
                ImGui::SameLine(0, 10);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (sz - ImGui::GetTextLineHeight()) * 0.5f);
                std::string first = selName.substr(0, selName.find(' '));
                ImGui::TextColored(ImVec4(0.55f, 0.82f, 0.52f, 1.0f), "%s is here", first.c_str());
                ImGui::PopStyleVar();
            }
            // Typing bubble: a little received-style bubble with animated dots.
            if (peerTyping && msFilt.empty()) {
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::SetCursorPosX(baseX);
                {   // their avatar beside the dots, ringed green while they're in the chat
                    ImVec2 ap = ImGui::GetCursorScreenPos();
                    ImGui::SetCursorScreenPos(ImVec2(ap.x, ap.y + (26.0f - avatarW) * 0.5f));
                    DrawAvatar(selAvatar, selName, avatarW);
                    if (peerHere) dl->AddCircle(ImVec2(ap.x + avatarW * 0.5f, ap.y + 13.0f), avatarW * 0.5f + 1.5f,
                                                IM_COL32(120, 200, 110, 230), 28, 2.0f);
                    ImGui::SameLine(0, 0);
                    ImGui::SetCursorScreenPos(ap);
                }
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
            if (log.empty()) ImGui::TextDisabled("No messages yet. Say hi!");
            else if (!msFilt.empty() && matches == 0) ImGui::TextDisabled("No messages match \"%s\".", msgSearch);
            // Auto-scroll to the newest message on open / new message (unless the
            // user has scrolled up to read history). Jump-to-latest overrides it.
            scrolledUp = ImGui::GetScrollMaxY() > 1.0f && (ImGui::GetScrollMaxY() - ImGui::GetScrollY()) > 80.0f;
            // Bubbles are auto-height children whose final height settles a frame
            // later, so a one-shot scroll lands short of the newest message. Keep
            // pinned to the bottom for a few frames after opening a chat, a new
            // message, or Jump-to-latest.
            static int stickBottom = 0;
            if (wantScrollBottom) { wantScrollBottom = false; stickBottom = 4; }
            else if ((int)log.size() != lastShown || selUid != lastConv) {
                bool opened = selUid != lastConv;
                if (msFilt.empty() && (opened || !scrolledUp)) stickBottom = 4;
                lastShown = (int)log.size(); lastConv = selUid;
            }
            {   // Content grew while we sat at the bottom (an image/GIF/sticker finished
                // loading, a bubble re-measured): stay pinned to the newest message.
                static float prevMaxY = 0.0f, prevY = 0.0f;
                const float maxY = ImGui::GetScrollMaxY();
                if (maxY > prevMaxY + 0.5f && prevY >= prevMaxY - 4.0f && msFilt.empty()) stickBottom = std::max(stickBottom, 2);
                prevMaxY = maxY; prevY = ImGui::GetScrollY();
            }
            if (stickBottom > 0) { ImGui::SetScrollHereY(1.0f); --stickBottom; scrolledUp = false; }
            logRectMin = ImGui::GetWindowPos();
            logRectMax = ImVec2(logRectMin.x + ImGui::GetWindowWidth(), logRectMin.y + ImGui::GetWindowHeight());
            // Floating "jump to latest" button while scrolled up. It's the LAST
            // child of the log, so it renders above the message bubbles (each
            // bubble is itself a child window and would otherwise cover it).
            if (scrolledUp) {
                ImVec2 keep = ImGui::GetCursorPos();
                float sbw = ImGui::GetStyle().ScrollbarSize;
                ImGui::SetCursorScreenPos(ImVec2(logRectMax.x - sbw - 48, logRectMax.y - 46));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::BeginChild("##jumpbtn", ImVec2(36, 36), ImGuiChildFlags_None,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                  ImGuiWindowFlags_NoBackground);
                ImVec2 jp = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##jump", ImVec2(34, 34))) wantScrollBottom = true;
                bool jh = ImGui::IsItemHovered();
                {   // round button with a drawn chevron (the arrow glyph isn't in Roboto)
                    ImDrawList* jd = ImGui::GetWindowDrawList();
                    ImVec2 jc(jp.x + 17, jp.y + 17);
                    jd->AddCircleFilled(jc, 16.0f, ImGui::GetColorU32(jh ? ImVec4(0.30f, 0.33f, 0.40f, 1.0f)
                                                                         : ImVec4(0.22f, 0.24f, 0.30f, 0.97f)), 24);
                    jd->AddCircle(jc, 16.0f, IM_COL32(255, 255, 255, 34), 24, 1.0f);
                    ImU32 ac = IM_COL32(230, 234, 242, 255);
                    jd->AddLine(ImVec2(jc.x - 5.5f, jc.y - 2.5f), ImVec2(jc.x, jc.y + 3.0f), ac, 2.0f);
                    jd->AddLine(ImVec2(jc.x, jc.y + 3.0f), ImVec2(jc.x + 5.5f, jc.y - 2.5f), ac, 2.0f);
                }
                if (jh) ImGui::SetTooltip("Jump to latest");
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::SetCursorPos(keep);
                ImGui::Dummy(ImVec2(0, 0));   // an item must follow a cursor move (ImGui boundary check)
            }
            ImGui::EndChild();

            if (selBlocked) {   // no composer while blocked — just a way back
                ImGui::Dummy(ImVec2(0, 10));
                ImGui::TextDisabled("You blocked %s, so you can't message them.", selName.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Unblock")) { g_blockedUsers.erase(selUid); SavePrefs(); }
            } else {
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
            // ---- Row 1: a slim, muted format toolbar (emoji + markdown helpers) ----
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));   // flat, unobtrusive
            {   // Emoji / GIFs / stickers. GIFs + stickers send straight away.
                std::vector<std::string> gifs;   // GIFs already in this chat ("in this chat")
                for (auto it = log.rbegin(); it != log.rend() && gifs.size() < 12; ++it) {
                    std::string t = it->body;
                    while (!t.empty() && (t.back() == ' ' || t.back() == '\n')) t.pop_back();
                    if (IsGifUrl(t) && FirstImageUrl(t) == t && std::find(gifs.begin(), gifs.end(), t) == gifs.end()) gifs.push_back(t);
                }
                std::string media = ChatMediaPicker("cwmedia", input, sizeof(input), &gifs, true);
                if (!media.empty()) {
                    if (kChatDemo) {
                        okay::DirectMessage dmm; dmm.id = log.empty() ? 1 : log.back().id + 1; dmm.body = media;
                        dmm.when = (long)time(nullptr); dmm.mine = true; log.push_back(dmm); wantScrollBottom = true;
                    } else { OutboxSend(selUid, 0, media); wantScrollBottom = true; }
                }
                ImGui::SameLine(0, 6);
            }
            auto fmtBtn = [&](const char* lbl, const char* open, const char* close, const char* word, const char* tip) {
                if (ImGui::SmallButton(lbl)) {
                    std::size_t len = std::char_traits<char>::length(input);
                    std::string ins = std::string(open) + word + close;
                    std::snprintf(input + len, sizeof(input) - len, "%s", ins.c_str());
                    focusComposer = true;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
                ImGui::SameLine(0, 3);
            };
            fmtBtn("B", "**", "**", "bold", "Bold  **text**");
            fmtBtn("I", "*", "*", "italic", "Italic  *text*");
            fmtBtn("</>", "`", "`", "code", "Code  `text`");
            {   // Attach a photo or file (native picker; uploads in the background)
                ImGui::SameLine(0, 6);
                ComposerPlusButton("dmplus", ImGui::GetFrameHeight(), selUid, 0);
                ImGui::SameLine(0, 8);
                if (g_attBusy) {
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("Uploading %s\xE2\x80\xA6", ChatPreview(g_attName, 28).c_str());
                    ImGui::SameLine();
                }
            }
            ImGui::PopStyleColor();
            ImGui::NewLine();                                          // input starts on its own row

            // ---- Row 2: the input pill + accent Send button ------------------------
            int lines = 1; for (const char* c = input; *c; ++c) if (*c == '\n') ++lines;
            if (lines > 4) lines = 4;
            float inH = ImGui::GetTextLineHeight() * lines + 16;
            if (input[0] && !editId && now - typingSentAt > 2000) { g_chatTypingReq = true; typingSentAt = now; }
            if (VoiceRecording() && !VoiceRecordingFor(selUid, 0)) FinishVoiceRecord(false);   // switched chats
            if (VoiceRecordingFor(selUid, 0)) DrawVoiceRecorderBar(inH, myBubble);
            else {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(11, 7));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.18f, 0.23f, 1.0f));
            ImVec2 inSz(ImGui::GetContentRegionAvail().x - 66, inH);
            if (focusComposer) { ImGui::SetKeyboardFocusHere(); focusComposer = false; }
            bool send = ImGui::InputTextMultiline("##cwin", input, sizeof(input), inSz,
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            ImGui::SameLine(0, 8);
            // Send button follows this chat's theme colour (a touch brighter).
            auto brighten = [](ImVec4 c, float k) { return ImVec4(std::min(1.0f, c.x * k), std::min(1.0f, c.y * k), std::min(1.0f, c.z * k), 1.0f); };
            ImGui::PushStyleColor(ImGuiCol_Button, brighten(myBubble, 1.25f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brighten(myBubble, 1.45f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
            const bool micMode = !input[0] && !editId && !g_attBusy;   // empty composer -> mic (WhatsApp)
            if (!micMode && (ImGui::Button(editId ? "Save" : "Send", ImVec2(58, inH)) || send) && input[0]) {
                if (kChatDemo && !std::getenv("OKAY_CHAT_DEMO_OUTBOX")) {   // demo: echo locally (no network)
                    okay::DirectMessage dmm; dmm.id = log.empty() ? 1 : log.back().id + 1; dmm.body = ExpandCustomEmoji(input);
                    dmm.when = (long)time(nullptr); dmm.mine = true; dmm.replyTo = replyTo; log.push_back(dmm);
                    input[0] = '\0'; replyTo = 0; wantScrollBottom = true;
                }
                else if (editId) { if (dm.Edit(editId, input)) { editId = 0; input[0] = '\0'; g_chatForceLog = true; } else Toast("Couldn't save the edit", 2); }
                else {   // instant: shows as "Sending" right away, sent off-thread
                    OutboxSend(selUid, 0, ExpandCustomEmoji(input), replyTo);
                    input[0] = '\0'; replyTo = 0; drafts.erase(selUid); wantScrollBottom = true;
                }
                // Keep typing: refocus the message box (not the Send button, which
                // SetKeyboardFocusHere(-1) would pick — that broke Enter-to-send
                // after the first message).
                focusComposer = true;
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            if (micMode) DrawVoiceMicButton("##dmmic", ImVec2(58, inH), myBubble, selUid, 0);
            }   // not recording
            }   // !selBlocked composer
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
            SDL_Texture* it = ChatImageTexture(lightboxUrl);
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

// ============================================================================
// Public profiles (Steam-style). A full page that can show ANY player — me, a
// friend, a server member or a forum author. It overlays the current tab's
// content, so Back simply returns there. All network work runs on a worker
// (ProfileWorker); OKAY_UI_DEMO_USER seeds sample profiles for screenshots.
// Server side: docs/sql/social.sql + the optional docs/sql/profile_showcase.sql
// (without it, others' showcases are hidden and mine is kept on this device).
// ============================================================================
struct PfOpenReq { std::string uid, username, name, avatar; int accent = 0; };
static bool g_pfOpen = false;                  // the profile page is showing
static PfOpenReq g_pfReq;                      // who: uid preferred, else username
static unsigned g_pfSerial = 0;                // bumps per open (drops stale results)
static SDL_mutex* g_pfMutex = nullptr;
static volatile bool g_pfBusy = false;         // a profile job is in flight
static bool g_pfReady = false;                 // (guarded) a fresh bundle is waiting
static unsigned g_pfReadySerial = 0;           // (guarded) which open it belongs to
static okay::ProfileBundle g_pfResult;         // (guarded)
static int g_pfActType = 0, g_pfActResult = 0; // (guarded) last action: type, 1 ok / -1 failed

static void OpenPublicProfile(const std::string& uid, const std::string& username,
                              const std::string& name = std::string(), const std::string& avatar = std::string(),
                              int accent = 0) {
    if (uid.empty() && username.empty()) return;
    if (!g_pfMutex) g_pfMutex = SDL_CreateMutex();
    g_pfReq.uid = uid; g_pfReq.username = username; g_pfReq.name = name;
    g_pfReq.avatar = avatar; g_pfReq.accent = accent;
    g_pfOpen = true; ++g_pfSerial;
}
static void ClosePublicProfile() { g_pfOpen = false; }

// type: 0 load, 1 save my showcase, 2 add friend, 3 accept, 4 remove/decline.
// Every job ends with a fresh Load() so the page shows the server's truth.
struct PfJob {
    int type = 0; unsigned serial = 0;
    std::string target; bool byName = false;   // whose profile to (re)load
    bool syncLocal = false;                    // mirror my local achievements first
    std::vector<std::string> ach; std::vector<long> games; int banner = -1;
    long row = 0;
};
static int ProfileWorker(void* arg) {
    PfJob* j = (PfJob*)arg;
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        okay::Profiles pr(a);
        int act = 0;
        if (j->type == 1) {
            pr.SyncAchievements(okay::Achievements::All());
            act = pr.SaveShowcase(j->ach, j->games, j->banner) ? 1 : -1;
        } else if (j->type == 2) act = pr.AddFriend(j->target) ? 1 : -1;
        else if (j->type == 3)   act = pr.Accept(j->row) ? 1 : -1;
        else if (j->type == 4)   act = pr.Remove(j->row) ? 1 : -1;
        else if (j->syncLocal)   pr.SyncAchievements(okay::Achievements::All());
        okay::ProfileBundle b = pr.Load(j->target, j->byName);
        if (j->type == 0 && !j->syncLocal && b.isMe && pr.SyncAchievements(okay::Achievements::All()))
            b = pr.Load(j->target, j->byName);
        if (g_pfMutex) SDL_LockMutex(g_pfMutex);
        g_pfResult = std::move(b); g_pfReadySerial = j->serial; g_pfReady = true;
        if (j->type) { g_pfActType = j->type; g_pfActResult = act; }
        if (g_pfMutex) SDL_UnlockMutex(g_pfMutex);
    } catch (...) {}
    g_pfBusy = false;
    delete j;
    return 0;
}

// My showcase picks on this device: the fallback when the server doesn't have
// the showcase columns yet (and a cache of my last save otherwise).
struct PfLocalShowcase { std::vector<std::string> ach; std::vector<long> games; int banner = -1; };
static std::string PfLocalShowcasePath() { return okay::Achievements::Dir() + "/profile_showcase.txt"; }
static PfLocalShowcase LoadLocalShowcase() {
    PfLocalShowcase s;
    std::ifstream f(PfLocalShowcasePath());
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("ach=", 0) == 0 && line.size() > 4) s.ach.push_back(line.substr(4));
        else if (line.rfind("game=", 0) == 0) { long id = std::atol(line.c_str() + 5); if (id > 0) s.games.push_back(id); }
        else if (line.rfind("banner=", 0) == 0) s.banner = std::atoi(line.c_str() + 7);
    }
    return s;
}
static void SaveLocalShowcase(const PfLocalShowcase& s) {
    std::ofstream f(PfLocalShowcasePath(), std::ios::trunc);
    if (!f) return;
    for (const auto& a : s.ach) f << "ach=" << a << "\n";
    for (long g : s.games) f << "game=" << g << "\n";
    f << "banner=" << s.banner << "\n";
}

// Sample profiles for the OKAY_UI_DEMO_USER screenshot hook.
static okay::ProfileBundle DemoProfileBundle(const PfOpenReq& r, const std::string& me) {
    okay::ProfileBundle b;
    b.found = b.reached = b.showcaseSupported = b.achSupported = true;
    const long t0 = (long)std::time(nullptr), day = 86400;
    okay::PublicProfile& p = b.p;
    auto ach = [&](const char* id, const char* label, long ago) {
        okay::Achievement a; a.id = id; a.label = label; a.when = t0 - ago; b.achievements.push_back(a); };
    auto game = [&](long id, const char* t, const char* d, const char* k, long dls) {
        okay::WorkshopItem w; w.id = id; w.title = t; w.description = d; w.kind = k; w.downloads = dls; b.games.push_back(w); };
    auto ev = [&](const char* k, const char* d, long ago) {
        okay::FriendEvent e; e.kind = k; e.detail = d; e.when = t0 - ago; b.activity.push_back(e); };
    const std::string key = !r.username.empty() ? r.username : r.uid;
    if (key == me || r.uid == "me") {
        p.id = "me"; p.username = me; p.display = me; p.accent = 3; p.bannerAccent = -1;
        p.bio = "Indie dev making small, loud arcade games in OkaySpace. Neon Drift is my baby; "
                "Pixel Farm is the cozy one. Always up for a jam team.";
        p.location = "Pixel City"; p.favoriteGame = "Neon Drift"; p.customStatus = "shipping Neon Drift 2.0";
        p.online = true; p.playing = "In the launcher"; p.memberSince = t0 - 412 * day;
        ach("perfect_lap", "Perfect Lap", 2 * 3600); ach("speed_demon", "Speed Demon", 1 * day);
        ach("first_publish", "Published!", 3 * day); ach("coin_hoarder", "Coin Hoarder", 6 * day);
        ach("night_owl", "Night Owl", 9 * day); ach("untouchable", "Untouchable", 14 * day);
        ach("jam_survivor", "Jam Survivor", 21 * day); ach("explorer", "Explorer", 33 * day);
        ach("social_butterfly", "Social Butterfly", 40 * day); ach("master_builder", "Master Builder", 58 * day);
        ach("marathon", "Marathon Runner", 77 * day); ach("first_win", "First Victory", 120 * day);
        p.showcaseAch = {"untouchable", "perfect_lap", "jam_survivor", "speed_demon", "master_builder", "first_win"};
        game(41, "Neon Drift", "Synthwave arcade racer with drift combos and a ghost-lap leaderboard.", "game", 4210);
        game(57, "Pixel Farm", "A cozy farming loop: plant, water, sell, repeat. Seasons included.", "game", 2380);
        game(33, "Asteroid Belt", "Classic asteroids with a gravity-well twist.", "game", 1190);
        game(62, "Dungeon Dash", "One-room roguelike dungeon runs in under 3 minutes.", "game", 640);
        game(70, "Racer Starter Kit", "Template: car physics, laps and a HUD to start your own racer.", "template", 312);
        p.showcaseGames = {41, 57, 62, 33};
        b.friends = 23; b.isMe = true;
        ev("publish", "Neon Drift 2.0", 3 * 3600); ev("achievement", "Perfect Lap", 2 * 3600 + 600);
        ev("playing", "Dungeon Dash", day); ev("achievement", "Speed Demon", day + 1800);
    } else if (key == "maya" || r.uid == "u-maya") {
        p.id = "u-maya"; p.username = "maya"; p.display = "Maya Chen"; p.accent = 2;
        p.bio = "Level designer by day, speedrunner by night. Currently holding the Neon Drift any% record (for now).";
        p.location = "Vancouver, BC"; p.favoriteGame = "Neon Drift"; p.online = true; p.playing = "Playing Neon Drift";
        p.sessionCode = "QX7-42"; p.memberSince = t0 - 290 * day;
        ach("wr_holder", "World Record", 5 * 3600); ach("perfect_lap", "Perfect Lap", 2 * day);
        ach("no_brakes", "No Brakes", 4 * day); ach("speed_demon", "Speed Demon", 9 * day);
        ach("explorer", "Explorer", 30 * day); ach("first_win", "First Victory", 200 * day);
        p.showcaseAch = {"wr_holder", "no_brakes", "perfect_lap", "speed_demon"};
        game(88, "Skyline Sprint", "Rooftop parkour time trials across a neon city.", "game", 1820);
        game(91, "Glow Garden", "A tiny relaxing light-puzzle garden.", "scene", 260);
        b.friends = 41; b.friendRow = 1; b.friendStatus = "accepted";
        ev("playing", "Neon Drift", 240); ev("achievement", "World Record", 5 * 3600); ev("publish", "Skyline Sprint", 6 * day);
    } else {
        p.username = !r.username.empty() ? r.username : (!r.name.empty() ? r.name : r.uid);
        for (auto& c : p.username) c = (char)std::tolower((unsigned char)c);
        p.id = !r.uid.empty() ? r.uid : "u-" + p.username;
        p.display = !r.name.empty() ? r.name : p.username;
        p.avatar = r.avatar;
        unsigned h = 2166136261u; for (char c : p.username) h = (h ^ (unsigned char)c) * 16777619u;
        p.accent = (int)(h % 7u); p.online = (h & 1u) != 0; p.lastSeen = t0 - (long)(h % 20u + 1) * 3600;
        p.memberSince = t0 - (long)(h % 500u + 30) * day;
        p.bio = "Making games with friends on OkaySpace.";
        p.favoriteGame = (h & 2u) ? "Asteroid Belt" : "Pixel Farm";
        ach("first_win", "First Victory", 3 * day); ach("explorer", "Explorer", 12 * day);
        ach("night_owl", "Night Owl", 40 * day);
        if (h & 4u) game(120 + (long)(h % 50u), "Untitled Platformer", "A work-in-progress platformer.", "game", (long)(h % 300u));
        b.friends = (int)(h % 30u) + 2;
        if (!r.uid.empty()) { b.friendRow = 9; b.friendStatus = "accepted"; ev("online", "", 3 * 3600); }
    }
    return b;
}

// Recolor the vertices added since v0 with a linear gradient from c0 (at p0)
// to c1 (at p1); vertex alpha is multiplied by the gradient alpha. Lets a
// rounded AddRectFilled become a rounded gradient.
static void PfGradient(ImDrawList* dl, int v0, ImVec2 p0, ImVec2 p1, ImVec4 c0, ImVec4 c1) {
    ImVec2 d(p1.x - p0.x, p1.y - p0.y);
    float len2 = d.x * d.x + d.y * d.y; if (len2 <= 0.0f) len2 = 1.0f;
    for (int i = v0; i < dl->VtxBuffer.Size; ++i) {
        ImDrawVert& v = dl->VtxBuffer[i];
        float t = ((v.pos.x - p0.x) * d.x + (v.pos.y - p0.y) * d.y) / len2;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        float a = (float)((v.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
        ImVec4 c(c0.x + (c1.x - c0.x) * t, c0.y + (c1.y - c0.y) * t, c0.z + (c1.z - c0.z) * t,
                 (c0.w + (c1.w - c0.w) * t) * a);
        v.col = ImGui::GetColorU32(c);
    }
}

// Achievement "icon": a hexagon medal in the achievement's own hue with a
// trophy glyph (achievements carry no artwork, so the id picks the color).
static void DrawAchMedal(ImDrawList* dl, ImVec2 c, float r, const std::string& id, float alpha) {
    const float PI = 3.14159265f, s = r / 15.0f;
    ImVec4 nc = NameColor(id);
    dl->AddCircleFilled(c, r + 3.0f, ImGui::GetColorU32(ImVec4(nc.x, nc.y, nc.z, 0.14f * alpha)), 32);
    int v0 = dl->VtxBuffer.Size;
    dl->AddNgonFilled(c, r, IM_COL32_WHITE, 6);
    PfGradient(dl, v0, ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r),
               ImVec4(nc.x * 0.62f, nc.y * 0.62f, nc.z * 0.62f, alpha), ImVec4(nc.x * 0.30f, nc.y * 0.30f, nc.z * 0.30f, alpha));
    dl->AddNgon(c, r, ImGui::GetColorU32(ImVec4(nc.x, nc.y, nc.z, alpha)), 6, 1.8f);
    ImU32 g = ImGui::GetColorU32(ImVec4(nc.x * 0.30f + 0.70f, nc.y * 0.30f + 0.70f, nc.z * 0.30f + 0.70f, alpha));
    dl->PathLineTo(ImVec2(c.x - 5.6f * s, c.y - 7.5f * s));
    dl->PathLineTo(ImVec2(c.x + 5.6f * s, c.y - 7.5f * s));
    dl->PathArcTo(ImVec2(c.x, c.y - 2.6f * s), 5.6f * s, 0.0f, PI, 14);
    dl->PathFillConvex(g);
    dl->PathArcTo(ImVec2(c.x - 5.6f * s, c.y - 4.6f * s), 2.6f * s, PI * 0.5f, PI * 1.5f, 8); dl->PathStroke(g, 0, 1.5f * s);
    dl->PathArcTo(ImVec2(c.x + 5.6f * s, c.y - 4.6f * s), 2.6f * s, -PI * 0.5f, PI * 0.5f, 8); dl->PathStroke(g, 0, 1.5f * s);
    dl->AddRectFilled(ImVec2(c.x - 1.2f * s, c.y + 2.6f * s), ImVec2(c.x + 1.2f * s, c.y + 6.0f * s), g);
    dl->AddRectFilled(ImVec2(c.x - 4.8f * s, c.y + 5.8f * s), ImVec2(c.x + 4.8f * s, c.y + 8.4f * s), g, 1.2f * s);
}

// "first_win" -> "First Win" (label for showcase ids we have no row for).
static std::string PfPrettyId(const std::string& id) {
    std::string o; bool up = true;
    for (char c : id) {
        if (c == '_' || c == '-') { o += ' '; up = true; continue; }
        o += up ? (char)std::toupper((unsigned char)c) : c; up = false;
    }
    return o;
}
static std::string PfCount(long n) {   // 4210 -> "4.2k"
    char b[32];
    if (n >= 1000000) std::snprintf(b, sizeof(b), "%.1fM", n / 1e6);
    else if (n >= 10000) std::snprintf(b, sizeof(b), "%ldk", n / 1000);
    else if (n >= 1000) std::snprintf(b, sizeof(b), "%.1fk", n / 1e3);
    else std::snprintf(b, sizeof(b), "%ld", n);
    return b;
}

struct PfPageOut { int gotoTab = -1; std::string marketSearch; };

static PfPageOut DrawPublicProfilePage(okay::account::AccountService& account, int fromTab) {
    PfPageOut out;
    static const char* kDemoUser = std::getenv("OKAY_UI_DEMO_USER");   // dev screenshot hook
    const bool kDemo = kDemoUser != nullptr;
    const std::string meName = kDemo ? std::string(kDemoUser) : account.CurrentSession().username;
    static const char* kTabNames[] = {"Create", "Play", "Community", "Account", "Settings", "Achievements",
                                      "Friends", "Market", "Servers", "Forums", "Downloads"};
    const int nPresets = (int)(sizeof(kAccentPresets) / sizeof(kAccentPresets[0]));

    // ---- Page state --------------------------------------------------------
    static unsigned shown = 0;                 // g_pfSerial the state below belongs to
    static okay::ProfileBundle s_b;            // what's on screen
    static bool s_loaded = false;
    static std::vector<PfJob*> s_queue;        // jobs waiting for the worker
    static std::vector<okay::Achievement> s_local;   // my local unlocks (newest first)
    static PfLocalShowcase s_localShow;        // my device-side picks
    static bool s_edit = false;                // "Edit showcase" mode (own profile)
    static std::vector<std::string> e_ach; static std::vector<long> e_games; static int e_banner = -1;
    auto refreshLocal = [&]() {
        s_local = okay::Achievements::All();
        std::reverse(s_local.begin(), s_local.end());
        s_localShow = LoadLocalShowcase();
    };
    auto queueJob = [&](int type) -> PfJob* {
        PfJob* j = new PfJob; j->type = type; j->serial = shown;
        j->target = !s_b.p.id.empty() ? s_b.p.id : (!g_pfReq.uid.empty() ? g_pfReq.uid : g_pfReq.username);
        j->byName = s_b.p.id.empty() && g_pfReq.uid.empty();
        s_queue.push_back(j);
        return j;
    };
    if (shown != g_pfSerial) {                 // a (new) profile was opened
        shown = g_pfSerial;
        s_b = okay::ProfileBundle(); s_loaded = false; s_edit = false;
        s_b.p.id = g_pfReq.uid; s_b.p.username = g_pfReq.username; s_b.p.avatar = g_pfReq.avatar;
        s_b.p.display = !g_pfReq.name.empty() ? g_pfReq.name : g_pfReq.username;
        s_b.p.accent = g_pfReq.accent;
        s_b.isMe = !meName.empty() && g_pfReq.username == meName;
        if (kDemo) { s_b = DemoProfileBundle(g_pfReq, meName); s_loaded = true; }
        else if (account.IsLoggedIn()) {
            if (s_b.isMe) refreshLocal();
            PfJob* j = queueJob(0); j->syncLocal = s_b.isMe;
        }
    }
    // Feed the worker one job at a time and pick up results (never blocks).
    if (!kDemo) {
        if (!g_pfBusy && !s_queue.empty()) {
            PfJob* j = s_queue.front(); s_queue.erase(s_queue.begin());
            g_pfBusy = true;
            if (SDL_Thread* t = SDL_CreateThread(ProfileWorker, "okay-profile", j)) SDL_DetachThread(t);
            else { g_pfBusy = false; delete j; }
        }
        if (g_pfMutex) {
            SDL_LockMutex(g_pfMutex);
            if (g_pfReady) {
                if (g_pfReadySerial == shown && (g_pfResult.found || !s_loaded)) {
                    bool wasMe = s_b.isMe;
                    s_b = g_pfResult; s_loaded = true;
                    if (s_b.isMe && !wasMe) refreshLocal();
                }
                g_pfReady = false;
            }
            int at = g_pfActType, ar = g_pfActResult; g_pfActType = 0; g_pfActResult = 0;
            SDL_UnlockMutex(g_pfMutex);
            if (at == 1) Toast(ar > 0 ? "Showcase saved" : "Showcase saved on this device (server showcases aren't enabled yet)", ar > 0 ? 1 : 2);
            else if (at == 2) Toast(ar > 0 ? "Friend request sent" : "Couldn't send the friend request", ar > 0 ? 1 : 2);
            else if (at == 3) Toast(ar > 0 ? "Friend request accepted" : "Couldn't accept the request", ar > 0 ? 1 : 2);
            else if (at == 4 && ar < 0) Toast("Couldn't update the friendship", 2);
        }
    }

    const okay::PublicProfile& p = s_b.p;
    const bool me = s_b.isMe || (!meName.empty() && p.username == meName);
    const std::string dispName = !p.display.empty() ? p.display : (!p.username.empty() ? p.username : std::string("Player"));
    const ImU32 cText = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 cDim  = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 cRaised = ImGui::GetColorU32(okaytheme::kRaised);
    const ImU32 hair = IM_COL32(255, 255, 255, 16);
    const ImVec4 accLt(kAccent.x * 0.55f + 0.45f, kAccent.y * 0.55f + 0.45f, kAccent.z * 0.55f + 0.45f, 1.0f);
    const ImU32 cGreen = IM_COL32(76, 200, 110, 255), cGrey = IM_COL32(120, 128, 140, 255);
    const float fs = ImGui::GetFontSize();
    ImFont* font = ImGui::GetFont();

    // Back: Esc or the mouse's back button (Esc leaves edit mode first).
    bool goBack = false;
    if (!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsMouseClicked(3))) {
        if (s_edit) s_edit = false; else goBack = true;
    }

    // ---- Top bar: "< Back to Friends" pill (+ a loading hint) ----------------
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cp = ImGui::GetCursorScreenPos();
        const float ch = ImGui::GetTextLineHeight() + 14.0f;
        char lbl[64]; std::snprintf(lbl, sizeof(lbl), "Back to %s", kTabNames[std::clamp(fromTab, 0, 10)]);
        float bw = ImGui::CalcTextSize(lbl).x + 42.0f;
        if (ImGui::InvisibleButton("##pfback", ImVec2(bw, ch))) goBack = true;
        bool h = ImGui::IsItemHovered();
        if (h) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        dl->AddRectFilled(cp, ImVec2(cp.x + bw, cp.y + ch), h ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 9), ch * 0.5f);
        float cy = cp.y + ch * 0.5f;
        dl->AddLine(ImVec2(cp.x + 20.0f, cy - 4.5f), ImVec2(cp.x + 15.5f, cy), cText, 1.8f);
        dl->AddLine(ImVec2(cp.x + 15.5f, cy), ImVec2(cp.x + 20.0f, cy + 4.5f), cText, 1.8f);
        dl->AddText(ImVec2(cp.x + 29.0f, cp.y + (ch - ImGui::GetTextLineHeight()) * 0.5f), h ? cText : cDim, lbl);
        if (!kDemo && (g_pfBusy || !s_queue.empty())) {
            float a0 = (float)ImGui::GetTime() * 6.0f;
            ImVec2 sc(cp.x + bw + 20.0f, cy);
            dl->PathArcTo(sc, 6.0f, a0, a0 + 4.6f, 20); dl->PathStroke(ImGui::GetColorU32(accLt), 0, 2.0f);
            dl->AddText(ImVec2(sc.x + 14.0f, cp.y + (ch - ImGui::GetTextLineHeight()) * 0.5f), cDim,
                        s_loaded ? "Updating\xE2\x80\xA6" : "Loading profile\xE2\x80\xA6");
        }
        ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y + ch + 14.0f));
        ImGui::Dummy(ImVec2(0, 0));
    }
    if (goBack) { ClosePublicProfile(); s_edit = false; return out; }

    // Centered empty state (signed out / not found).
    auto emptyState = [&](int icon, const char* title, const char* sub) {
        float aw = ImGui::GetContentRegionAvail().x;
        ImGui::Dummy(ImVec2(0, 50));
        ImVec2 c(ImGui::GetCursorScreenPos().x + aw * 0.5f, ImGui::GetCursorScreenPos().y + 30);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(c, 30.0f, IM_COL32(255, 255, 255, 12), 40);
        DrawNavIcon(dl, icon, c, cDim);
        ImGui::Dummy(ImVec2(0, 70));
        for (int k = 0; k < 2; ++k) {
            const char* s = k ? sub : title;
            float tw = ImGui::CalcTextSize(s).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (aw - tw) * 0.5f));
            if (k) ImGui::TextDisabled("%s", s); else ImGui::TextUnformatted(s);
        }
    };
    if (!kDemo && !account.IsLoggedIn()) {
        emptyState(3, "Sign in to view profiles", "Profiles, showcases and friends need an OkaySpace account.");
        return out;
    }
    if (s_loaded && !s_b.found) {
        if (s_b.reached) emptyState(3, "Profile not found", "This player may have renamed or deleted their account.");
        else             emptyState(3, "Couldn't load this profile", "Check your connection and try again.");
        return out;
    }

    // ---- Derived data --------------------------------------------------------
    const std::vector<okay::Achievement>& achAll = (me && !kDemo) ? s_local : s_b.achievements;
    const bool achKnown = me || s_b.achSupported;
    const std::vector<std::string> pickA = s_b.showcaseSupported ? p.showcaseAch : (me ? s_localShow.ach : std::vector<std::string>());
    const std::vector<long> pickG = s_b.showcaseSupported ? p.showcaseGames : (me ? s_localShow.games : std::vector<long>());
    const int pickBanner = s_b.showcaseSupported ? p.bannerAccent : (me ? s_localShow.banner : -1);
    auto findAch = [&](const std::string& id) -> const okay::Achievement* {
        for (const auto& a : achAll) if (a.id == id) return &a;
        return nullptr;
    };
    auto findGame = [&](long id) -> const okay::WorkshopItem* {
        for (const auto& g : s_b.games) if (g.id == id) return &g;
        return nullptr;
    };
    std::vector<okay::Achievement> shownA;
    for (const auto& id : pickA) {
        if (shownA.size() >= 6) break;
        if (const okay::Achievement* a = findAch(id)) shownA.push_back(*a);
        else if (!achKnown) { okay::Achievement x; x.id = id; x.label = PfPrettyId(id); shownA.push_back(x); }
    }
    const bool achPinned = !shownA.empty();
    if (!achPinned) for (std::size_t i = 0; i < achAll.size() && i < 6; ++i) shownA.push_back(achAll[i]);
    std::vector<okay::WorkshopItem> shownG;
    for (long id : pickG) { if (shownG.size() >= 4) break; if (const okay::WorkshopItem* g = findGame(id)) shownG.push_back(*g); }
    const bool gamesPinned = !shownG.empty();
    if (!gamesPinned) for (std::size_t i = 0; i < s_b.games.size() && i < 4; ++i) shownG.push_back(s_b.games[i]);
    long totalDl = 0; for (const auto& g : s_b.games) totalDl += g.downloads;
    const int achN = (int)achAll.size(), gameN = (int)s_b.games.size();
    const int level = 1 + (int)std::sqrt((achN * 40.0 + gameN * 120.0 + std::max(0, s_b.friends) * 15.0 + totalDl / 40.0) / 20.0);
    int bannerIdx = (s_edit ? e_banner : pickBanner);
    if (bannerIdx < 0 || bannerIdx >= nPresets) bannerIdx = p.accent;
    if (bannerIdx < 0 || bannerIdx >= nPresets) bannerIdx = 0;
    const ImVec4 bc = kAccentPresets[bannerIdx].col;
    const bool playingSomething = p.online && !p.playing.empty() && p.playing != "In OkaySpace" &&
                                  p.playing != "In the launcher" && p.playing != "Away";
    const ImU32 presCol = !p.online ? cGrey : playingSomething ? ImGui::GetColorU32(accLt) : cGreen;

    auto enterEdit = [&]() {
        s_edit = true; e_ach.clear(); e_games.clear();
        for (const auto& a : shownA) if (achPinned) e_ach.push_back(a.id);
        for (const auto& g : shownG) if (gamesPinned) e_games.push_back(g.id);
        e_banner = pickBanner;
    };
    auto saveEdit = [&]() {
        if (kDemo) {
            s_b.p.showcaseAch = e_ach; s_b.p.showcaseGames = e_games; s_b.p.bannerAccent = e_banner;
            Toast("Showcase saved", 1);
        } else {
            PfLocalShowcase ls; ls.ach = e_ach; ls.games = e_games; ls.banner = e_banner;
            SaveLocalShowcase(ls); s_localShow = ls;
            if (s_b.showcaseSupported) { s_b.p.showcaseAch = e_ach; s_b.p.showcaseGames = e_games; s_b.p.bannerAccent = e_banner; }
            PfJob* j = queueJob(1); j->ach = e_ach; j->games = e_games; j->banner = e_banner;
        }
        s_edit = false;
    };

    // Small-caps card title with optional right-side text (clickable if link).
    auto cardTitle = [&](const char* title, const char* right, bool link) -> bool {
        bool hit = false;
        const float x0 = ImGui::GetCursorPosX(), avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetWindowFontScale(0.80f);
        ImGui::TextDisabled("%s", title);
        if (right && *right) {
            float rw = ImGui::CalcTextSize(right).x;
            ImGui::SameLine(std::max(0.0f, x0 + avail - rw));
            if (link) {
                ImGui::PushStyleColor(ImGuiCol_Text, accLt);
                ImGui::TextUnformatted(right);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                hit = ImGui::IsItemClicked();
            } else ImGui::TextDisabled("%s", right);
        }
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Dummy(ImVec2(0, 4));
        return hit;
    };
    auto beginCard = [&](const char* id, float w) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kRaised);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
        ImGui::BeginChild(id, ImVec2(w, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);
    };
    auto endCard = [&]() {
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 8));
    };
    // Pick badge (edit mode): numbered accent disc, or a faint "+" when unpicked.
    auto pickBadge = [&](ImDrawList* dl, ImVec2 c, int n, bool hov) {
        if (n > 0) {
            dl->AddCircleFilled(c, 10.0f, ImGui::GetColorU32(kAccent), 20);
            char t[4]; std::snprintf(t, sizeof(t), "%d", n);
            ImVec2 ts = ImGui::CalcTextSize(t);
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32_WHITE, t);
        } else {
            dl->AddCircle(c, 9.0f, hov ? cText : IM_COL32(255, 255, 255, 60), 20, 1.5f);
            ImU32 pc = hov ? cText : IM_COL32(255, 255, 255, 90);
            dl->AddLine(ImVec2(c.x - 4, c.y), ImVec2(c.x + 4, c.y), pc, 1.5f);
            dl->AddLine(ImVec2(c.x, c.y - 4), ImVec2(c.x, c.y + 4), pc, 1.5f);
        }
    };

    ImGui::BeginChild("##pfbody", ImVec2(0, ImGui::GetContentRegionAvail().y - 46.0f), false);
    const float W = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ======================= HERO: banner, avatar, name, actions, stats =======
    {
        const bool compact = W < 640.0f;
        const float bannerH = W < 520.0f ? 104.0f : 132.0f;
        const float A = W < 520.0f ? 84.0f : 104.0f;
        const float infoH = compact ? (A * 0.5f + 14.0f + 70.0f + 36.0f + 14.0f) : 98.0f;
        const int statCols = W < 460.0f ? 2 : 4;
        const float statRowH = 62.0f, statsH = statRowH * (4 / statCols) + 6.0f;
        const float heroH = bannerH + infoH + statsH;
        ImVec2 hp = ImGui::GetCursorScreenPos();
        ImVec2 he(hp.x + W, hp.y + heroH);
        dl->AddRectFilled(hp, he, cRaised, 12.0f);
        // Banner: profile-accent gradient + soft shapes, fading into the card.
        int v0 = dl->VtxBuffer.Size;
        dl->AddRectFilled(hp, ImVec2(he.x, hp.y + bannerH), IM_COL32_WHITE, 12.0f, ImDrawFlags_RoundCornersTop);
        PfGradient(dl, v0, hp, ImVec2(he.x, hp.y + bannerH),
                   okaytheme::Mix(okaytheme::kRaised, bc, 0.78f), okaytheme::Mix(okaytheme::kRaised, bc, 0.18f));
        dl->PushClipRect(hp, ImVec2(he.x, hp.y + bannerH), true);
        dl->AddCircleFilled(ImVec2(he.x - W * 0.16f, hp.y - bannerH * 0.15f), bannerH * 1.05f, IM_COL32(255, 255, 255, 10), 64);
        dl->AddCircleFilled(ImVec2(he.x - W * 0.05f, hp.y + bannerH * 1.05f), bannerH * 0.65f, IM_COL32(255, 255, 255, 8), 48);
        dl->AddCircleFilled(ImVec2(hp.x + W * 0.42f, hp.y + bannerH * 1.3f), bannerH * 0.8f, IM_COL32(0, 0, 0, 22), 48);
        for (int k = 0; k < 7; ++k) {
            float x = hp.x + W * 0.55f + k * 26.0f;
            dl->AddLine(ImVec2(x, hp.y + bannerH + 10), ImVec2(x + bannerH * 0.9f, hp.y - 10), IM_COL32(255, 255, 255, 6), 9.0f);
        }
        dl->PopClipRect();
        v0 = dl->VtxBuffer.Size;
        dl->AddRectFilled(ImVec2(hp.x, hp.y + bannerH - 46.0f), ImVec2(he.x, hp.y + bannerH), IM_COL32_WHITE);
        PfGradient(dl, v0, ImVec2(hp.x, hp.y + bannerH - 46.0f), ImVec2(hp.x, hp.y + bannerH),
                   okaytheme::WithAlpha(okaytheme::kRaised, 0.0f), okaytheme::WithAlpha(okaytheme::kRaised, 0.85f));
        // Level badge (top-right of the banner).
        {
            ImVec2 lc(he.x - 42.0f, hp.y + 38.0f);
            dl->AddCircleFilled(lc, 24.0f, IM_COL32(0, 0, 0, 80), 40);
            dl->AddCircle(lc, 21.0f, ImGui::GetColorU32(okaytheme::Mix(bc, ImVec4(1, 1, 1, 1), 0.35f)), 40, 2.5f);
            char lv[8]; std::snprintf(lv, sizeof(lv), "%d", level);
            ImVec2 ls = font->CalcTextSizeA(fs * 1.2f, FLT_MAX, 0.0f, lv);
            dl->AddText(font, fs * 1.2f, ImVec2(lc.x - ls.x * 0.5f, lc.y - ls.y * 0.5f), IM_COL32_WHITE, lv);
            ImVec2 ks = font->CalcTextSizeA(fs * 0.72f, FLT_MAX, 0.0f, "LEVEL");
            dl->AddText(font, fs * 0.72f, ImVec2(lc.x - ks.x * 0.5f, lc.y + 27.0f), IM_COL32(255, 255, 255, 190), "LEVEL");
            if (ImGui::IsMouseHoveringRect(ImVec2(lc.x - 26, lc.y - 26), ImVec2(lc.x + 26, lc.y + 40)) && ImGui::IsWindowHovered())
                ImGui::SetTooltip("Profile level %d\nGrows with achievements, published games and friends.", level);
        }
        // Avatar with a presence ring (cut out of the card).
        ImVec2 ac(hp.x + 26.0f + A * 0.5f, hp.y + bannerH - 6.0f);
        dl->AddCircleFilled(ac, A * 0.5f + 7.0f, cRaised, 64);
        dl->AddCircle(ac, A * 0.5f + 3.0f, presCol, 64, 3.0f);
        if (SDL_Texture* tex = AvatarTexture(p.avatar)) {
            dl->AddImageRounded((ImTextureID)(intptr_t)tex, ImVec2(ac.x - A * 0.5f, ac.y - A * 0.5f), ImVec2(ac.x + A * 0.5f, ac.y + A * 0.5f),
                                ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, A * 0.5f);
        } else {
            ImVec4 nc = NameColor(dispName);
            dl->AddCircleFilled(ac, A * 0.5f, ImGui::GetColorU32(ImVec4(nc.x * 0.55f, nc.y * 0.55f, nc.z * 0.55f, 1)), 64);
            char init[2] = {(char)std::toupper((unsigned char)dispName[0]), 0};
            ImVec2 is = font->CalcTextSizeA(A * 0.44f, FLT_MAX, 0.0f, init);
            dl->AddText(font, A * 0.44f, ImVec2(ac.x - is.x * 0.5f, ac.y - is.y * 0.5f), IM_COL32_WHITE, init);
        }
        ImVec2 pd(ac.x + A * 0.354f, ac.y + A * 0.354f);
        dl->AddCircleFilled(pd, 11.0f, cRaised, 20);
        dl->AddCircleFilled(pd, 7.5f, presCol, 20);

        // Actions: right of the name on wide cards, under it on compact ones.
        const float bh = ImGui::GetTextLineHeight() + 16.0f, rb = bh;   // button height, round "more" size
        struct Btn { std::string label; int kind; };   // kind: 0 secondary, 1 primary
        std::vector<Btn> btns;
        enum { kBMsg, kBFriend, kBEdit, kBProfile, kBCancel, kBSave };
        std::vector<int> ids;
        if (me) {
            if (s_edit) { btns.push_back({"Cancel", 0}); ids.push_back(kBCancel); btns.push_back({"Save showcase", 1}); ids.push_back(kBSave); }
            else { btns.push_back({"Edit profile", 0}); ids.push_back(kBProfile); btns.push_back({"Edit showcase", 1}); ids.push_back(kBEdit); }
        } else {
            const bool friends = s_b.friendStatus == "accepted";
            btns.push_back({"Message", friends ? 1 : 0}); ids.push_back(kBMsg);
            std::string fl = friends ? "Friends" : s_b.friendStatus == "pending" ? (s_b.incoming ? "Accept request" : "Request sent") : "Add friend";
            btns.push_back({fl, (!friends && !(s_b.friendStatus == "pending" && !s_b.incoming)) ? 1 : 0}); ids.push_back(kBFriend);
        }
        float actW = rb;
        for (const auto& b : btns) actW += ImGui::CalcTextSize(b.label.c_str()).x + 30.0f + 8.0f;
        const float nameX = compact ? hp.x + 26.0f : hp.x + 26.0f + A + 22.0f;
        const float nameY = compact ? hp.y + bannerH + A * 0.5f + 12.0f : hp.y + bannerH + 12.0f;
        const float textMaxW = compact ? W - 52.0f : std::max(80.0f, (he.x - 22.0f - actW) - nameX - 16.0f);
        float bx = compact ? nameX : he.x - 22.0f - actW;
        const float by = compact ? nameY + 74.0f : hp.y + bannerH + 16.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(15, 8));
        for (std::size_t i = 0; i < btns.size(); ++i) {
            const Btn& b = btns[i];
            float w = ImGui::CalcTextSize(b.label.c_str()).x + 30.0f;
            ImGui::SetCursorScreenPos(ImVec2(bx, by));
            ImGui::PushID((int)i);
            bool disabled = !kDemo && ((ids[i] == kBMsg || ids[i] == kBFriend) && p.id.empty());
            ImGui::BeginDisabled(disabled);
            bool hit;
            if (b.kind == 1) hit = PrimaryButton(b.label.c_str(), ImVec2(w, bh));
            else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.07f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.14f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.20f));
                hit = ImGui::Button(b.label.c_str(), ImVec2(w, bh));
                ImGui::PopStyleColor(3);
            }
            ImGui::EndDisabled();
            if (ids[i] == kBFriend && ImGui::IsItemHovered() && !s_b.friendStatus.empty() && !s_b.incoming)
                ImGui::SetTooltip(s_b.friendStatus == "accepted" ? "You're friends - click for options" : "Waiting for them to accept");
            ImGui::PopID();
            if (hit) {
                switch (ids[i]) {
                case kBMsg: OpenChatWith(p.id, dispName, p.avatar); break;
                case kBEdit: enterEdit(); break;
                case kBCancel: s_edit = false; break;
                case kBSave: saveEdit(); break;
                case kBProfile: out.gotoTab = 3; ClosePublicProfile(); break;
                case kBFriend:
                    if (s_b.friendStatus == "accepted" || (s_b.friendStatus == "pending" && !s_b.incoming))
                        ImGui::OpenPopup("##pffriend");
                    else if (s_b.friendStatus == "pending" && s_b.incoming) {
                        if (kDemo) Toast("You're now friends with " + dispName, 1);
                        else { PfJob* j = queueJob(3); j->row = s_b.friendRow; }
                        s_b.friendStatus = "accepted";
                    } else {
                        if (kDemo) Toast("Friend request sent", 1);
                        else queueJob(2);
                        s_b.friendStatus = "pending"; s_b.incoming = false;
                    }
                    break;
                }
            }
            bx += w + 8.0f;
        }
        ImGui::PopStyleVar(2);
        ImGui::SetCursorScreenPos(ImVec2(bx, by + (bh - rb) * 0.5f));
        if (RoundIconButton("##pfmore", kRibMore, rb, IM_COL32(255, 255, 255, 18), IM_COL32(255, 255, 255, 36), cText, "More"))
            ImGui::OpenPopup("##pfmorepop");
        if (ImGui::BeginPopup("##pffriend")) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1));
            if (ImGui::MenuItem(s_b.friendStatus == "accepted" ? "Remove friend" : "Cancel request")) {
                if (!kDemo) { PfJob* j = queueJob(4); j->row = s_b.friendRow; }
                else Toast(s_b.friendStatus == "accepted" ? "Removed from friends" : "Request cancelled", 1);
                s_b.friendStatus.clear(); s_b.friendRow = 0;
            }
            ImGui::PopStyleColor();
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopup("##pfmorepop")) {
            if (ImGui::MenuItem("Copy username")) { ImGui::SetClipboardText(("@" + p.username).c_str()); Toast("Username copied", 1); }
            if (!me && p.online && !p.sessionCode.empty() && ImGui::MenuItem("Join their game")) {
                ImGui::SetClipboardText(p.sessionCode.c_str());
                Toast("Session code " + p.sessionCode + " copied - open the game and Join with it", 2);
            }
            if (me && ImGui::MenuItem("View all achievements")) { out.gotoTab = 5; ClosePublicProfile(); }
            if (!me && s_b.friendStatus == "pending" && s_b.incoming) {
                ImGui::Separator();
                if (ImGui::MenuItem("Decline request")) {
                    if (!kDemo) { PfJob* j = queueJob(4); j->row = s_b.friendRow; }
                    s_b.friendStatus.clear(); s_b.friendRow = 0;
                }
            }
            ImGui::EndPopup();
        }

        // Name, @handle + presence, custom status.
        {
            float nsz = fs * 1.5f;
            std::string nm = dispName;
            while (nm.size() > 4 && font->CalcTextSizeA(nsz, FLT_MAX, 0.0f, nm.c_str()).x > textMaxW - (me ? 46.0f : 0.0f))
                nm = nm.substr(0, nm.size() - 4) + "\xE2\x80\xA6";
            dl->AddText(font, nsz, ImVec2(nameX, nameY), IM_COL32(242, 244, 250, 255), nm.c_str());
            if (me) {   // "YOU" pill after the name
                float nx = nameX + font->CalcTextSizeA(nsz, FLT_MAX, 0.0f, nm.c_str()).x + 10.0f;
                ImVec2 ts = font->CalcTextSizeA(fs * 0.72f, FLT_MAX, 0.0f, "YOU");
                float py = nameY + (nsz - ts.y - 6.0f) * 0.5f + 1.0f;
                dl->AddRectFilled(ImVec2(nx, py), ImVec2(nx + ts.x + 12.0f, py + ts.y + 6.0f), ImGui::GetColorU32(okaytheme::WithAlpha(kAccent, 0.28f)), 8.0f);
                dl->AddText(font, fs * 0.72f, ImVec2(nx + 6.0f, py + 3.0f), ImGui::GetColorU32(accLt), "YOU");
            }
            float y2 = nameY + nsz + 6.0f;
            std::string handle = p.username.empty() ? std::string() : "@" + p.username;
            dl->AddText(ImVec2(nameX, y2), cDim, handle.c_str());
            float hx = nameX + ImGui::CalcTextSize(handle.c_str()).x + (handle.empty() ? 0.0f : 12.0f);
            std::string pres = !s_loaded && !kDemo ? std::string()
                             : playingSomething ? p.playing
                             : p.online ? std::string("Online")
                             : p.lastSeen > 0 ? "Offline  \xC2\xB7  seen " + RelTime(p.lastSeen) : std::string("Offline");
            if (!pres.empty()) {
                float ly = y2 + ImGui::GetTextLineHeight() * 0.5f;
                dl->AddCircleFilled(ImVec2(hx + 4.0f, ly), 4.0f, presCol, 12);
                dl->AddText(ImVec2(hx + 13.0f, y2), playingSomething ? ImGui::GetColorU32(accLt) : p.online ? cGreen : cDim,
                            FitText(pres, nameX + textMaxW - hx - 13.0f).c_str());
            }
            if (!p.customStatus.empty()) {
                std::string cs = "\"" + p.customStatus + "\"";
                dl->AddText(ImVec2(nameX, y2 + ImGui::GetTextLineHeight() + 6.0f), IM_COL32(184, 191, 209, 255),
                            FitText(cs, textMaxW).c_str());
            }
        }

        // Stats strip.
        {
            const float sy = hp.y + bannerH + infoH;
            dl->AddLine(ImVec2(hp.x + 18.0f, sy), ImVec2(he.x - 18.0f, sy), hair, 1.0f);
            char v0s[24], v1s[24], v2s[24], v3s[24] = "\xE2\x80\x94";
            if (s_b.friends >= 0) std::snprintf(v0s, sizeof(v0s), "%d", s_b.friends); else std::snprintf(v0s, sizeof(v0s), "\xE2\x80\x94");
            std::snprintf(v1s, sizeof(v1s), "%d", gameN);
            if (achKnown) std::snprintf(v2s, sizeof(v2s), "%d", achN); else std::snprintf(v2s, sizeof(v2s), "\xE2\x80\x94");
            if (p.memberSince > 0) { std::time_t t = (std::time_t)p.memberSince; if (std::tm* tm = std::localtime(&t)) std::strftime(v3s, sizeof(v3s), "%b %Y", tm); }
            const char* vals[4] = {v0s, v1s, v2s, v3s};
            const char* labs[4] = {"FRIENDS", "GAMES PUBLISHED", "ACHIEVEMENTS", "MEMBER SINCE"};
            const float tw = W / statCols;
            for (int i = 0; i < 4; ++i) {
                float tx = hp.x + (i % statCols) * tw, ty = sy + 3.0f + (i / statCols) * statRowH;
                if (i % statCols) dl->AddLine(ImVec2(tx, ty + 14.0f), ImVec2(tx, ty + statRowH - 14.0f), hair, 1.0f);
                dl->AddText(font, fs * 1.3f, ImVec2(tx + 24.0f, ty + 11.0f), IM_COL32(242, 244, 250, 255), vals[i]);
                dl->AddText(font, fs * 0.74f, ImVec2(tx + 24.0f, ty + 13.0f + fs * 1.3f + 2.0f), cDim, labs[i]);
            }
        }
        ImGui::SetCursorScreenPos(hp);
        ImGui::Dummy(ImVec2(W, heroH));
        ImGui::Dummy(ImVec2(0, 8));
    }

    // ======================= Cards ============================================
    const bool twoCol = W >= 820.0f;
    const float gap = 16.0f;
    const float sideW = twoCol ? std::clamp(W * 0.31f, 280.0f, 360.0f) : 0.0f;
    const float mainW = twoCol ? W - sideW - gap : W;

    auto editCard = [&](float w) {
        beginCard("##pfeditcard", w);
        cardTitle("EDIT SHOWCASE", nullptr, false);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("Click achievements and games below to pin them (up to 6 and 4). They show in the order you pick them.");
        ImGui::PopTextWrapPos();
        if (!kDemo && !s_b.showcaseSupported && s_loaded)
            ImGui::TextColored(ImVec4(0.95f, 0.72f, 0.36f, 1), "This server hasn't enabled showcases yet, so picks stay on this device.");
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextUnformatted("Banner color");
        ImGui::Dummy(ImVec2(0, 2));
        ImVec2 sp = ImGui::GetCursorScreenPos();
        ImDrawList* d = ImGui::GetWindowDrawList();
        const float D = 26.0f;
        for (int k = -1; k < nPresets; ++k) {
            ImVec2 c(sp.x + D * 0.5f + (k + 1) * (D + 10.0f), sp.y + D * 0.5f);
            ImGui::SetCursorScreenPos(ImVec2(c.x - D * 0.5f, sp.y));
            ImGui::PushID(k + 10);
            if (ImGui::InvisibleButton("##sw", ImVec2(D, D))) e_banner = k;
            bool h = ImGui::IsItemHovered();
            ImGui::PopID();
            int pi = k < 0 ? std::clamp(p.accent, 0, nPresets - 1) : k;
            ImVec4 col = kAccentPresets[pi].col;
            if (k < 0) {   // "Auto": follow my profile accent (half-filled disc)
                d->AddCircle(c, D * 0.5f - 1.0f, ImGui::GetColorU32(col), 24, 2.0f);
                d->PathArcTo(c, D * 0.5f - 4.0f, 3.14159f * 0.5f, 3.14159f * 1.5f, 16); d->PathFillConvex(ImGui::GetColorU32(col));
            } else d->AddCircleFilled(c, D * 0.5f - 1.0f, ImGui::GetColorU32(col), 24);
            if (e_banner == k) d->AddCircle(c, D * 0.5f + 3.0f, IM_COL32_WHITE, 24, 2.0f);
            else if (h) d->AddCircle(c, D * 0.5f + 3.0f, IM_COL32(255, 255, 255, 90), 24, 1.5f);
            if (h) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", k < 0 ? "Auto (profile color)" : kAccentPresets[k].name); }
        }
        ImGui::SetCursorScreenPos(ImVec2(sp.x, sp.y + D + 6.0f));
        ImGui::Dummy(ImVec2(0, 0));
        endCard();
    };

    auto achCard = [&](float w) {
        if (!me && !s_edit && shownA.empty() && (s_loaded || kDemo)) return;   // nothing public to show
        beginCard("##pfach", w);
        char right[48];
        if (s_edit) std::snprintf(right, sizeof(right), "%d / 6 pinned", (int)e_ach.size());
        else if (me) std::snprintf(right, sizeof(right), "%d unlocked  \xC2\xBB", achN);
        else if (achKnown) std::snprintf(right, sizeof(right), "%d unlocked", achN);
        else right[0] = '\0';
        if (cardTitle(s_edit ? "PIN ACHIEVEMENTS" : achPinned ? "ACHIEVEMENT SHOWCASE" : "RECENT ACHIEVEMENTS", right, me && !s_edit)) {
            out.gotoTab = 5; ClosePublicProfile();
        }
        const std::vector<okay::Achievement>& list = s_edit ? achAll : shownA;
        if (list.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            if (!s_loaded && !kDemo && !me) ImGui::TextDisabled("Loading\xE2\x80\xA6");
            else if (me) ImGui::TextDisabled("No achievements yet. Play games that unlock achievements, then pin your favourites here.");
            ImGui::PopTextWrapPos();
        } else {
            const float iw = ImGui::GetContentRegionAvail().x, g = 10.0f, th = 66.0f;
            const int cols = iw >= 560.0f ? 3 : iw >= 300.0f ? 2 : 1;
            const float tw = (iw - g * (cols - 1)) / cols;
            ImVec2 o = ImGui::GetCursorScreenPos();
            ImDrawList* d = ImGui::GetWindowDrawList();
            for (std::size_t i = 0; i < list.size(); ++i) {
                const okay::Achievement& a = list[i];
                ImVec2 tp(o.x + (i % cols) * (tw + g), o.y + (i / cols) * (th + g));
                ImGui::SetCursorScreenPos(tp);
                ImGui::PushID((int)i);
                bool click = ImGui::InvisibleButton("##achtile", ImVec2(tw, th));
                bool h = ImGui::IsItemHovered();
                ImGui::PopID();
                int pick = 0;
                if (s_edit) for (std::size_t k = 0; k < e_ach.size(); ++k) if (e_ach[k] == a.id) pick = (int)k + 1;
                const bool faded = s_edit && pick == 0;
                d->AddRectFilled(tp, ImVec2(tp.x + tw, tp.y + th), h ? IM_COL32(255, 255, 255, 18) : IM_COL32(255, 255, 255, 8), 10.0f);
                if (pick) d->AddRect(tp, ImVec2(tp.x + tw, tp.y + th), ImGui::GetColorU32(kAccent), 10.0f, 0, 2.0f);
                DrawAchMedal(d, ImVec2(tp.x + 12.0f + 21.0f, tp.y + th * 0.5f), 21.0f, a.id, faded ? 0.45f : 1.0f);
                const float tx = tp.x + 66.0f, maxW = tw - 66.0f - (s_edit ? 28.0f : 10.0f);
                d->AddText(ImVec2(tx, tp.y + 14.0f), faded ? ImGui::GetColorU32(ImGuiCol_Text, 0.55f) : cText, FitText(a.label, maxW).c_str());
                char when[48] = "";
                if (a.when > 0) { std::time_t t = (std::time_t)a.when; if (std::tm* tm = std::localtime(&t)) std::strftime(when, sizeof(when), "Unlocked %b %d, %Y", tm); }
                d->AddText(ImVec2(tx, tp.y + 36.0f), cDim, FitText(when[0] ? when : "Unlocked", maxW).c_str());
                if (s_edit) pickBadge(d, ImVec2(tp.x + tw - 16.0f, tp.y + 16.0f), pick, h);
                if (h) {
                    if (s_edit) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    else ImGui::SetTooltip("%s%s%s", a.label.c_str(), when[0] ? "\n" : "", when);
                }
                if (click && s_edit) {
                    if (pick) e_ach.erase(e_ach.begin() + (pick - 1));
                    else if (e_ach.size() < 6) e_ach.push_back(a.id);
                    else Toast("You can pin up to 6 achievements", 2);
                }
            }
            int rows = ((int)list.size() + cols - 1) / cols;
            ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + rows * th + (rows - 1) * g));
            ImGui::Dummy(ImVec2(0, 0));
        }
        endCard();
    };

    auto gamesCard = [&](float w) {
        if (!me && !s_edit && s_b.games.empty() && (s_loaded || kDemo)) return;
        beginCard("##pfgames", w);
        char right[48];
        if (s_edit) std::snprintf(right, sizeof(right), "%d / 4 pinned", (int)e_games.size());
        else std::snprintf(right, sizeof(right), "%d published", gameN);
        cardTitle(s_edit ? "PIN GAMES" : gamesPinned ? "FEATURED GAMES" : "PUBLISHED GAMES", right, false);
        const std::vector<okay::WorkshopItem>& list = s_edit ? s_b.games : shownG;
        if (list.empty()) {
            if (!s_loaded && !kDemo) ImGui::TextDisabled("Loading\xE2\x80\xA6");
            else {
                ImGui::TextDisabled("You haven't published anything to the Marketplace yet.");
                ImGui::Dummy(ImVec2(0, 2));
                if (PrimaryButton("Publish a game", ImVec2(160, 0))) { out.gotoTab = 7; g_openPublishForm = true; ClosePublicProfile(); }
            }
        } else {
            const float iw = ImGui::GetContentRegionAvail().x, g = 12.0f, coverH = 92.0f, th = coverH + 56.0f;
            const int cols = iw >= 760.0f ? 4 : iw >= 330.0f ? 2 : 1;
            const float tw = (iw - g * (cols - 1)) / cols;
            ImVec2 o = ImGui::GetCursorScreenPos();
            ImDrawList* d = ImGui::GetWindowDrawList();
            for (std::size_t i = 0; i < list.size(); ++i) {
                const okay::WorkshopItem& gm = list[i];
                ImVec2 tp(o.x + (i % cols) * (tw + g), o.y + (i / cols) * (th + g));
                ImGui::SetCursorScreenPos(tp);
                ImGui::PushID((int)gm.id);
                bool click = ImGui::InvisibleButton("##gametile", ImVec2(tw, th));
                bool h = ImGui::IsItemHovered();
                ImGui::PopID();
                int pick = 0;
                if (s_edit) for (std::size_t k = 0; k < e_games.size(); ++k) if (e_games[k] == gm.id) pick = (int)k + 1;
                const bool faded = s_edit && pick == 0;
                const float fa = faded ? 0.45f : 1.0f;
                d->AddRectFilled(tp, ImVec2(tp.x + tw, tp.y + th), h ? IM_COL32(255, 255, 255, 18) : IM_COL32(255, 255, 255, 8), 10.0f);
                // Cover: a gradient in the game's own hue + a play disc + kind pill.
                ImVec4 gc = NameColor(gm.title);
                int v0 = d->VtxBuffer.Size;
                d->AddRectFilled(tp, ImVec2(tp.x + tw, tp.y + coverH), IM_COL32_WHITE, 10.0f, ImDrawFlags_RoundCornersTop);
                PfGradient(d, v0, tp, ImVec2(tp.x + tw, tp.y + coverH),
                           ImVec4(gc.x * 0.78f, gc.y * 0.78f, gc.z * 0.78f, fa), ImVec4(gc.x * 0.26f, gc.y * 0.26f, gc.z * 0.30f, fa));
                d->PushClipRect(tp, ImVec2(tp.x + tw, tp.y + coverH), true);
                d->AddCircleFilled(ImVec2(tp.x + tw * 0.85f, tp.y + coverH * 0.1f), coverH * 0.75f, IM_COL32(255, 255, 255, (int)(14 * fa)), 40);
                std::string ini = gm.title.substr(0, 1);
                float isz = coverH * 0.9f;
                d->AddText(font, isz, ImVec2(tp.x + 12.0f, tp.y + coverH - isz * 0.86f), IM_COL32(255, 255, 255, (int)(34 * fa)), ini.c_str());
                d->PopClipRect();
                ImVec2 pc(tp.x + tw * 0.5f, tp.y + coverH * 0.5f);
                d->AddCircleFilled(pc, h && !s_edit ? 20.0f : 18.0f, IM_COL32(0, 0, 0, (int)(90 * fa)), 32);
                d->AddTriangleFilled(ImVec2(pc.x - 4.5f, pc.y - 7.0f), ImVec2(pc.x - 4.5f, pc.y + 7.0f), ImVec2(pc.x + 7.5f, pc.y),
                                     IM_COL32(255, 255, 255, (int)(235 * fa)));
                std::string kind = gm.kind.empty() ? std::string("GAME") : gm.kind;
                for (auto& c : kind) c = (char)std::toupper((unsigned char)c);
                ImVec2 ks = font->CalcTextSizeA(fs * 0.70f, FLT_MAX, 0.0f, kind.c_str());
                d->AddRectFilled(ImVec2(tp.x + 8, tp.y + 8), ImVec2(tp.x + 8 + ks.x + 12, tp.y + 8 + ks.y + 6), IM_COL32(0, 0, 0, (int)(110 * fa)), 7.0f);
                d->AddText(font, fs * 0.70f, ImVec2(tp.x + 14, tp.y + 11), IM_COL32(255, 255, 255, (int)(220 * fa)), kind.c_str());
                if (pick) d->AddRect(tp, ImVec2(tp.x + tw, tp.y + th), ImGui::GetColorU32(kAccent), 10.0f, 0, 2.0f);
                d->AddText(ImVec2(tp.x + 12.0f, tp.y + coverH + 9.0f), faded ? ImGui::GetColorU32(ImGuiCol_Text, 0.55f) : cText,
                           FitText(gm.title, tw - 24.0f).c_str());
                std::string sub = PfCount(gm.downloads) + (gm.downloads == 1 ? " download" : " downloads");
                d->AddText(ImVec2(tp.x + 12.0f, tp.y + coverH + 29.0f), cDim, FitText(sub, tw - 24.0f).c_str());
                if (s_edit) pickBadge(d, ImVec2(tp.x + tw - 16.0f, tp.y + 16.0f), pick, h);
                if (h) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if (!s_edit) {
                        ImGui::BeginTooltip();
                        ImGui::TextUnformatted(gm.title.c_str());
                        if (!gm.description.empty()) {
                            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
                            ImGui::TextDisabled("%s", gm.description.c_str());
                            ImGui::PopTextWrapPos();
                        }
                        ImGui::TextColored(accLt, "Click to find it in the Marketplace");
                        ImGui::EndTooltip();
                    }
                }
                if (click) {
                    if (s_edit) {
                        if (pick) e_games.erase(e_games.begin() + (pick - 1));
                        else if (e_games.size() < 4) e_games.push_back(gm.id);
                        else Toast("You can pin up to 4 games", 2);
                    } else { out.gotoTab = 7; out.marketSearch = gm.title; ClosePublicProfile(); }
                }
            }
            int rows = ((int)list.size() + cols - 1) / cols;
            ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + rows * th + (rows - 1) * g));
            ImGui::Dummy(ImVec2(0, 0));
        }
        endCard();
    };

    auto aboutCard = [&](float w) {
        beginCard("##pfabout", w);
        cardTitle("ABOUT", nullptr, false);
        ImGui::PushTextWrapPos(0.0f);
        if (!p.bio.empty()) ImGui::TextUnformatted(p.bio.c_str());
        else if (!s_loaded && !kDemo) ImGui::TextDisabled("Loading\xE2\x80\xA6");
        else ImGui::TextDisabled(me ? "No bio yet. Add one under Account \xC2\xBB Customize profile." : "No bio yet.");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, 6));
        ImDrawList* d = ImGui::GetWindowDrawList();
        auto row = [&](int icon, const char* label, const std::string& value) {
            if (value.empty()) return;
            ImVec2 rp = ImGui::GetCursorScreenPos();
            const float lh = ImGui::GetTextLineHeight();
            ImVec2 c(rp.x + 8.0f, rp.y + lh * 0.5f + 3.0f);
            const ImU32 ic = cDim; const float t = 1.5f;
            if (icon == 0) {        // location pin
                d->AddCircle(ImVec2(c.x, c.y - 2.0f), 4.2f, ic, 16, t);
                d->AddLine(ImVec2(c.x - 3.4f, c.y + 0.4f), ImVec2(c.x, c.y + 6.0f), ic, t);
                d->AddLine(ImVec2(c.x + 3.4f, c.y + 0.4f), ImVec2(c.x, c.y + 6.0f), ic, t);
            } else if (icon == 1) { // favourite game: play in a circle
                d->AddCircle(c, 6.5f, ic, 20, t);
                d->AddTriangleFilled(ImVec2(c.x - 2.0f, c.y - 3.2f), ImVec2(c.x - 2.0f, c.y + 3.2f), ImVec2(c.x + 3.4f, c.y), ic);
            } else if (icon == 2) { // calendar
                d->AddRect(ImVec2(c.x - 6.0f, c.y - 5.0f), ImVec2(c.x + 6.0f, c.y + 6.0f), ic, 2.0f, 0, t);
                d->AddLine(ImVec2(c.x - 6.0f, c.y - 1.5f), ImVec2(c.x + 6.0f, c.y - 1.5f), ic, t);
                d->AddLine(ImVec2(c.x - 2.8f, c.y - 7.0f), ImVec2(c.x - 2.8f, c.y - 3.5f), ic, t);
                d->AddLine(ImVec2(c.x + 2.8f, c.y - 7.0f), ImVec2(c.x + 2.8f, c.y - 3.5f), ic, t);
            } else {                // clock
                d->AddCircle(c, 6.5f, ic, 20, t);
                d->AddLine(c, ImVec2(c.x, c.y - 4.0f), ic, t);
                d->AddLine(c, ImVec2(c.x + 3.0f, c.y + 1.5f), ic, t);
            }
            ImGui::SetCursorScreenPos(ImVec2(rp.x + 24.0f, rp.y + 3.0f));
            ImGui::BeginGroup();
            ImGui::SetWindowFontScale(0.80f);
            ImGui::TextDisabled("%s", label);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(value.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
            ImGui::Dummy(ImVec2(0, 4));
        };
        char since[48] = "";
        if (p.memberSince > 0) { std::time_t t = (std::time_t)p.memberSince; if (std::tm* tm = std::localtime(&t)) std::strftime(since, sizeof(since), "%B %d, %Y", tm); }
        row(1, "FAVOURITE GAME", p.favoriteGame);
        row(0, "LOCATION", p.location);
        row(2, "MEMBER SINCE", since);
        if (!p.online && p.lastSeen > 0) row(3, "LAST ONLINE", RelTime(p.lastSeen));
        endCard();
    };

    auto activityCard = [&](float w) {
        beginCard("##pfactivity", w);
        cardTitle("RECENT ACTIVITY", nullptr, false);
        if (s_b.activity.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            if (!s_loaded && !kDemo) ImGui::TextDisabled("Loading\xE2\x80\xA6");
            else if (!me && s_b.friendStatus != "accepted") ImGui::TextDisabled("Recent activity is visible to friends.");
            else ImGui::TextDisabled("Nothing new lately.");
            ImGui::PopTextWrapPos();
        }
        int n = 0;
        for (const auto& e : s_b.activity) {
            if (n++ >= 6) break;
            ImVec2 ip = ImGui::GetCursorScreenPos();
            ImDrawList* d = ImGui::GetWindowDrawList();
            ImVec2 c(ip.x + 15.0f, ip.y + 15.0f);
            int icon = e.kind == "playing" ? 1 : e.kind == "achievement" ? 5 : e.kind == "publish" ? 7 : 3;
            d->AddCircleFilled(c, 15.0f, e.kind == "achievement" ? IM_COL32(240, 196, 92, 40) : IM_COL32(255, 255, 255, 14), 28);
            DrawNavIcon(d, icon, c, e.kind == "achievement" ? IM_COL32(240, 196, 92, 255) : cText);
            const char* verb = e.kind == "playing" ? "Played " : e.kind == "achievement" ? "Unlocked "
                             : e.kind == "publish" ? "Published " : e.kind == "online" ? "Came online" : "";
            ImGui::SetCursorScreenPos(ImVec2(ip.x + 40.0f, ip.y));
            ImGui::BeginGroup();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextWrapped("%s%s", verb, e.detail.c_str());
            ImGui::PopTextWrapPos();
            std::string ago = RelTime(e.when);
            if (!ago.empty()) ImGui::TextDisabled("%s", ago.c_str());
            ImGui::EndGroup();
            ImGui::Dummy(ImVec2(0, 6));
        }
        endCard();
    };

    ImGui::BeginGroup();
    if (s_edit) editCard(mainW);
    if (!twoCol) aboutCard(mainW);
    achCard(mainW);
    gamesCard(mainW);
    if (!twoCol) activityCard(mainW);
    ImGui::EndGroup();
    if (twoCol) {
        ImGui::SameLine(0, gap);
        ImGui::BeginGroup();
        aboutCard(sideW);
        activityCard(sideW);
        ImGui::EndGroup();
    }
    ImGui::EndChild();   // ##pfbody
    return out;
}

// ---- Background chat poll (worker; state declared before DrawChatWindow) ----
// Upload worker: own account service (thread-safe, like the pollers), upload to
// the "chat" Storage bucket, then send — images as the bare URL (rendered
// inline), anything else as a "[File] name (size) url" card.
struct ChatUploadJob { std::string path, toUid; long toGroup = 0; int voiceSecs = -1; };
static ChatUploadJob g_attJob;
static int ChatUploadWorker(void*) {
    ChatUploadJob job = g_attJob;
    std::string err;
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        okay::Messages dm(a);
        std::string url = dm.UploadChatFile(job.path, &err);
        if (!url.empty()) {
            std::string name = fs::path(job.path).filename().string();
            std::error_code ec; auto sz = fs::file_size(fs::path(job.path), ec);
            bool isImg = okay::Messages::MimeFor(name).rfind("image/", 0) == 0;
            std::string body = job.voiceSecs >= 0 ? VoiceMessageBody(job.voiceSecs, url)
                             : isImg ? url : okay::Messages::FileMessageBody(name, ec ? 0 : (unsigned long long)sz, url);
            bool ok = job.toGroup > 0 ? dm.SendGroup(job.toGroup, body) : dm.Send(job.toUid, body);
            if (!ok) err = "Uploaded, but sending the message failed.";
        } else if (err.empty()) err = "Upload failed.";
    } catch (...) { err = "Upload failed."; }
    if (job.voiceSecs >= 0) { std::error_code ec; fs::remove(fs::path(job.path), ec); }   // temp recording
    if (g_attMutex) { SDL_LockMutex(g_attMutex); g_attError = err; g_attDone = true; SDL_UnlockMutex(g_attMutex); }
    g_chatForceLog = true;
    g_attBusy = false;
    return 0;
}
static void StartChatUpload(const std::string& path, const std::string& toUid, long toGroup, int voiceSecs) {
    if (g_attBusy || path.empty()) return;
    if (!g_attMutex) g_attMutex = SDL_CreateMutex();
    g_attJob = ChatUploadJob{path, toUid, toGroup, voiceSecs};
    g_attName = voiceSecs >= 0 ? std::string("voice message") : fs::path(path).filename().string();
    g_attBusy = true;
    if (SDL_Thread* t = SDL_CreateThread(ChatUploadWorker, "okay-upload", nullptr)) SDL_DetachThread(t);
    else g_attBusy = false;
}
// ---- Custom emoji / sticker library + GIF search workers -------------------------
// (state + picker UI live in the chat media section above; these need the
// poller's account config, defined just above.)
struct LibJob { int kind = 0; std::string path, folder, name, file; };   // 0 list, 1 upload, 2 delete
static int LibraryWorker(void* p) {
    LibJob* job = (LibJob*)p;
    std::string status;
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        if (!a.IsLoggedIn()) {   // signed out: nothing to list (keep whatever is shown)
            SDL_LockMutex(g_libMutex); g_libLoaded = true; SDL_UnlockMutex(g_libMutex);
            g_libBusy = false; delete job; return 0;
        }
        okay::Messages dm(a);
        if (job->kind == 1) {
            std::string err;
            if (dm.UploadToLibrary(job->path, job->folder, job->name, &err).empty()) status = err.empty() ? "Upload failed." : err;
            else status = job->folder == "emoji" ? "Added :" + job->name + ": to your emoji" : "Sticker added";
        } else if (job->kind == 2) {
            status = dm.DeleteFromLibrary(job->folder, job->file) ? "Deleted" : "Couldn't delete that";
        }
        auto em = dm.ListLibrary("emoji");
        auto st = dm.ListLibrary("stickers");
        SDL_LockMutex(g_libMutex);
        g_libEmoji = em; g_libStickers = st; g_libLoaded = true;
        if (!status.empty()) g_libStatus = status;
        SDL_UnlockMutex(g_libMutex);
    } catch (...) {}
    g_libBusy = false;
    delete job;
    return 0;
}
static void LibraryStart(LibJob* job) {
    if (!g_libMutex) g_libMutex = SDL_CreateMutex();
    if (g_libBusy) { delete job; return; }
    g_libBusy = true;
    if (SDL_Thread* t = SDL_CreateThread(LibraryWorker, "okay-library", job)) SDL_DetachThread(t);
    else { g_libBusy = false; delete job; }
}
static void LibraryRefresh() { LibraryStart(new LibJob{0, "", "", "", ""}); }
static void LibraryUpload(const std::string& path, const std::string& folder, const std::string& name) {
    if (g_libMutex) { SDL_LockMutex(g_libMutex); g_libStatus = "Uploading\xE2\x80\xA6"; SDL_UnlockMutex(g_libMutex); }
    LibraryStart(new LibJob{1, path, folder, name, ""});
}
static void LibraryDelete(const std::string& folder, const std::string& file) { LibraryStart(new LibJob{2, "", folder, "", file}); }

static std::string UrlEncode(const std::string& s) {
    std::string o; char b[4];
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { std::snprintf(b, sizeof(b), "%%%02X", c); o += b; }
    }
    return o;
}
static std::string g_gifJobQuery;
static int GifSearchWorker(void*) {
    std::vector<GifResult> out; std::string err;
    const std::string q = g_gifJobQuery;
    std::string url = q.empty()
        ? "https://tenor.googleapis.com/v2/featured?key=" + UrlEncode(g_tenorKey) + "&client_key=okayspace&limit=30&media_filter=gif,tinygif&contentfilter=medium"
        : "https://tenor.googleapis.com/v2/search?q=" + UrlEncode(q) + "&key=" + UrlEncode(g_tenorKey) + "&client_key=okayspace&limit=30&media_filter=gif,tinygif&contentfilter=medium";
    fs::path tmp = MediaCacheDir() / "tenor_results.json";
    if (!Download(url, tmp.string())) err = "GIF search failed. Check the Tenor key or your connection.";
    else {
        std::ifstream f(tmp, std::ios::binary);
        std::string json((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::size_t r = json.find("\"results\"");
        if (r == std::string::npos) err = json.find("API key") != std::string::npos ? "That Tenor API key was rejected." : "GIF search failed.";
        else {
            // Split the results array into its top-level objects.
            std::size_t i = json.find('[', r); int depth = 0; std::size_t start = 0;
            for (; i != std::string::npos && i < json.size(); ++i) {
                char c = json[i];
                if (c == '{') { if (depth == 0) start = i; ++depth; }
                else if (c == '}') {
                    if (--depth == 0) {
                        std::string o = json.substr(start, i - start + 1);
                        auto urlIn = [&](const char* fmt) {
                            std::size_t p = o.find(fmt); if (p == std::string::npos) return std::string();
                            return okay::Messages::Field(o.substr(p, 600), "url");
                        };
                        GifResult g;
                        g.preview = urlIn("\"tinygif\"");
                        g.full = urlIn("\"gif\":");
                        g.desc = okay::Messages::Field(o, "content_description");
                        std::size_t tp = o.find("\"tinygif\"");
                        if (tp != std::string::npos) {
                            std::size_t d = o.find("\"dims\"", tp);
                            if (d != std::string::npos) std::sscanf(o.c_str() + o.find('[', d), "[%d,%d]", &g.w, &g.h);
                        }
                        if (!g.preview.empty()) out.push_back(g);
                    }
                } else if (c == ']' && depth == 0) break;
            }
        }
    }
    SDL_LockMutex(g_gifMutex);
    g_gifResults = out; g_gifError = err;
    SDL_UnlockMutex(g_gifMutex);
    g_gifReady = true; g_gifBusy = false;
    return 0;
}
static void GifSearch(const std::string& query) {
    if (g_tenorKey.empty() || g_gifBusy) return;
    if (!g_gifMutex) g_gifMutex = SDL_CreateMutex();
    g_gifJobQuery = query; g_gifBusy = true;
    if (SDL_Thread* t = SDL_CreateThread(GifSearchWorker, "okay-gifs", nullptr)) SDL_DetachThread(t);
    else g_gifBusy = false;
}

// Outbox sender: one thread drains queued messages in order, then exits.
static int OutboxWorker(void*) {
    std::unique_ptr<okay::account::AccountService> acct;
    while (true) {
        OutMsg job; bool have = false;
        SDL_LockMutex(g_outMutex);
        for (auto& o : g_outbox) if (o.state == 0) { job = o; have = true; break; }
        if (!have) { g_outWorkerRunning = false; SDL_UnlockMutex(g_outMutex); break; }
        SDL_UnlockMutex(g_outMutex);
        bool ok = false;
        try {
            if (!acct) acct = std::make_unique<okay::account::AccountService>(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
            okay::Messages dm(*acct);
            if (job.toChannel > 0) ok = okay::Servers(*acct).Post(job.toChannel, job.body);
            else ok = job.toGroup > 0 ? dm.SendGroup(job.toGroup, job.body, job.replyTo) : dm.Send(job.toUid, job.body, job.replyTo);
        } catch (...) {}
        SDL_LockMutex(g_outMutex);
        for (auto& o : g_outbox) if (o.localId == job.localId) { o.state = ok ? 1 : -1; o.sentAt = SDL_GetTicks(); }
        SDL_UnlockMutex(g_outMutex);
        if (ok) { if (job.toChannel > 0) g_srvForce = true; else g_chatForceLog = true; }   // pull the real message in right away
    }
    return 0;
}
// Poll votes (and other reaction changes) without blocking the UI: remove one
// reaction and/or add another on a DM or group message, then refresh.
struct ChatReactJob { bool group = false; long id = 0; std::string removeKey, addKey; };
static int ChatReactWorker(void* p) {
    std::unique_ptr<ChatReactJob> j(static_cast<ChatReactJob*>(p));
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        okay::Messages dm(a);
        if (!j->removeKey.empty()) { if (j->group) dm.GroupReact(j->id, j->removeKey, false); else dm.React(j->id, j->removeKey, false); }
        if (!j->addKey.empty())    { if (j->group) dm.GroupReact(j->id, j->addKey, true);     else dm.React(j->id, j->addKey, true); }
    } catch (...) {}
    g_chatForceLog = true;
    return 0;
}
static void ChatReactAsync(bool group, long id, const std::string& removeKey, const std::string& addKey) {
    if (std::getenv("OKAY_CHAT_DEMO")) return;   // demo: the local vote is enough
    auto* j = new ChatReactJob{group, id, removeKey, addKey};
    if (SDL_Thread* t = SDL_CreateThread(ChatReactWorker, "okay-react", j)) SDL_DetachThread(t);
    else delete j;
}
static void OutboxKick() {
    if (!g_outMutex) g_outMutex = SDL_CreateMutex();
    SDL_LockMutex(g_outMutex);
    const bool start = !g_outWorkerRunning;
    if (start) g_outWorkerRunning = true;
    SDL_UnlockMutex(g_outMutex);
    if (!start) return;
    if (SDL_Thread* t = SDL_CreateThread(OutboxWorker, "okay-outbox", nullptr)) SDL_DetachThread(t);
    else { SDL_LockMutex(g_outMutex); g_outWorkerRunning = false; SDL_UnlockMutex(g_outMutex); }
}

// Pick a file with the native dialog and start uploading it to a chat.
static void PickAndSendAttachment(const std::string& toUid, long toGroup) {
    if (g_attBusy) { Toast("An upload is already in progress", 2); return; }
    const char* pick = tinyfd_openFileDialog("Send a photo or file", "", 0, nullptr, nullptr, 0);
    if (!pick || !*pick) return;
    std::error_code ec; auto sz = fs::file_size(fs::path(pick), ec);
    if (ec) { Toast("Couldn't read that file", 2); return; }
    if ((unsigned long long)sz > kMaxChatUpload) { Toast("Files are limited to 25 MB", 2); return; }
    StartChatUpload(pick, toUid, toGroup);
}

static int ChatPollWorker(void*) {
    okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
    okay::Messages dm(a); okay::Friends fr(a);
    Uint32 nextLog = 0, nextTyping = 0, nextConv = 0, nextFri = 0, nextGroups = 0, nextGLog = 0;
    Uint32 nextHere = 0, nextSeen = 0;
    std::string lastUid; long lastGid = 0, lastMark = 0, peerLastSeen = 0;
    while (g_chatPollRun) {
        if (!g_chatWinOpen) { SDL_Delay(200); continue; }
        Uint32 now = SDL_GetTicks();
        std::string uid; long gid = 0;
        if (g_chatMutex) { SDL_LockMutex(g_chatMutex); uid = g_chatSelUid; gid = g_chatSelGroup; SDL_UnlockMutex(g_chatMutex); }
        if (uid != lastUid) { lastUid = uid; nextLog = 0; nextTyping = 0; nextHere = 0; nextSeen = 0; lastMark = 0; peerLastSeen = 0; }   // conversation changed → poll now
        // Only an open, focused conversation counts as read / "in the chat".
        const bool viewing = SDL_GetTicks() - g_chatViewMs < 1500;
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
                    long markId = l.empty() ? 0 : l.back().id;
                    if (markId && viewing) { dm.MarkRead(uid, markId); nextHere = now + 3000; }
                    if (markId) lastMark = markId;
                    if (g_chatMutex) {
                        SDL_LockMutex(g_chatMutex);
                        g_chatLog = std::move(l); g_chatReacts = std::move(rc); g_chatMyReacts = std::move(mr);
                        g_chatLogReady = true;
                        SDL_UnlockMutex(g_chatMutex);
                    }
                    nextLog = now + 4000;
                }
                // "I'm here" heartbeat between log polls (refreshes the read row's time).
                if (viewing && lastMark > 0 && now >= nextHere) { dm.MarkRead(uid, lastMark); nextHere = now + 3000; }
                if (now >= nextTyping) {
                    g_chatPeerTyping = dm.PeerTyping(uid);
                    // Read position + whether they're in the chat (every 2.5s), and
                    // when they were last online for "Delivered" (every 15s).
                    const bool wantSeen = now >= nextSeen;
                    auto ps = dm.PeerStateOf(uid, wantSeen);
                    if (wantSeen && ps.ok) { peerLastSeen = ps.lastSeen; nextSeen = now + 15000; }
                    if (ps.ok && g_chatMutex) {
                        const long t = (long)std::time(nullptr);
                        SDL_LockMutex(g_chatMutex);
                        g_chatPeerRead = ps.readId;
                        g_chatPeerHere = ps.viewingAt > 0 && t - ps.viewingAt >= -3 && t - ps.viewingAt < 9;
                        g_chatPeerLastSeen = peerLastSeen;
                        g_chatPeerStateUid = uid;
                        SDL_UnlockMutex(g_chatMutex);
                    }
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

// Likes and edits on channel messages: applied to the local copy right away,
// sent off-thread. While one is in flight, poll snapshots (which may predate
// it) are ignored so the change doesn't flicker back; a fresh poll follows.
struct SrvActJob { int kind = 0; long id = 0; std::string body; };   // 0 like, 1 unlike, 2 edit
static SDL_atomic_t g_srvActPending;
static volatile bool g_srvActFailed = false;
static int SrvActWorker(void* p) {
    std::unique_ptr<SrvActJob> j(static_cast<SrvActJob*>(p));
    bool ok = false;
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        okay::Servers sv(a);
        ok = j->kind == 2 ? sv.EditMessage(j->id, j->body) : sv.Like(j->id, j->kind == 0);
    } catch (...) {}
    if (!ok && j->kind == 2) g_srvActFailed = true;
    SDL_AtomicDecRef(&g_srvActPending);
    g_srvForce = true;
    return 0;
}
static void ServerActionAsync(int kind, long id, const std::string& body = std::string()) {
    auto* j = new SrvActJob; j->kind = kind; j->id = id; j->body = body;
    SDL_AtomicIncRef(&g_srvActPending);
    if (SDL_Thread* t = SDL_CreateThread(SrvActWorker, "okay-srvact", j)) SDL_DetachThread(t);
    else { SDL_AtomicDecRef(&g_srvActPending); delete j; }
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
        // JetBrains Mono (same as the editor's script editor) for `code` in chat.
        g_fontMono = ImGui::GetIO().Fonts->AddFontFromMemoryCompressedBase85TTF(
            JetBrainsMono_compressed_data_base85, 16.0f, &fc);
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
                // Dropped onto the open chat window: send it to that conversation.
                bool toChat = false;
                if (g_chatWinOpen && g_chatDropOk && (!g_chatDropUid.empty() || g_chatDropGroup > 0)) {
                    int gx = 0, gy = 0, wx = 0, wy = 0;
                    SDL_GetGlobalMouseState(&gx, &gy);
                    SDL_GetWindowPosition(window, &wx, &wy);
                    float mx = (float)(gx - wx), my = (float)(gy - wy);
                    toChat = mx >= g_chatDropMin.x && mx <= g_chatDropMax.x && my >= g_chatDropMin.y && my <= g_chatDropMax.y;
                }
                if (toChat) {
                    std::error_code fec;
                    auto fsz = fs::file_size(fs::path(dropped), fec);
                    if (fec || fs::is_directory(dropped, fec)) Toast("Only files can be sent", 2);
                    else if ((unsigned long long)fsz > kMaxChatUpload) Toast("Files are limited to 25 MB", 2);
                    else if (g_attBusy) Toast("An upload is already in progress", 2);
                    else { StartChatUpload(dropped, g_chatDropUid, g_chatDropGroup); Toast("Sending " + fs::path(dropped).filename().string(), 1); }
                    continue;
                }
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
                    if (unread > g_dmUnread && !g_chatWinOpen) PlayChatChime();
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

        // Periodic update check — runs while open AND while minimized (this block
        // sits before the minimized early-out below).
        {
            static Uint32 nextPoll = SDL_GetTicks() + 5u * 60u * 1000u;   // first look 5 min after start
            if (static bool demoUp = false; !demoUp) {   // dev screenshot hook: pretend a newer version was found
                demoUp = true;
                if (const char* v = std::getenv("OKAY_UI_DEMO_UPDATE")) {
                    SDL_LockMutex(g_upPollMutex ? g_upPollMutex : (g_upPollMutex = SDL_CreateMutex()));
                    g_upPollResult = v; g_upPollDone = true;
                    SDL_UnlockMutex(g_upPollMutex);
                }
            }
            const UpState us = GetState();
            if (SDL_GetTicks() >= nextPoll) {
                nextPoll = SDL_GetTicks() + 20u * 60u * 1000u;
                if (us != Up_Checking && us != Up_Downloading && us != Up_Updated) StartUpdatePoll();
            }
            std::string latest; bool done = false;
            if (g_upPollMutex) {
                SDL_LockMutex(g_upPollMutex);
                if (g_upPollDone) { done = true; latest = g_upPollResult; g_upPollDone = false; }
                SDL_UnlockMutex(g_upPollMutex);
            }
            if (done && !latest.empty() && CompareVersions(LocalVersion(), latest) < 0 && latest != g_upAvailable) {
                g_upAvailable = latest;
                Toast("Update available: OkaySpace v" + latest, 1);
                UpsertNotif("update:" + latest, "Update available",
                            "OkaySpace v" + latest + " is ready. Install it from the sidebar.", 3, -1);
#if SDL_VERSION_ATLEAST(2, 0, 16)
                if ((SDL_GetWindowFlags(window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_INPUT_FOCUS)) != SDL_WINDOW_INPUT_FOCUS)
                    SDL_FlashWindow(window, SDL_FLASH_UNTIL_FOCUSED);   // taskbar attention while minimized/unfocused
#endif
            }
            if (us == Up_Updated || us == Up_UpToDate) {   // a real check/install supersedes the hint
                if (!g_upAvailable.empty() && (us == Up_Updated || CompareVersions(LocalVersion(), g_upAvailable) >= 0)) g_upAvailable.clear();
            }
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
                        ImGui::TextDisabled("v%s  \xC2\xBB  v%s", fromV.c_str(), toV.c_str());   // » (arrows aren't in Roboto)
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

        // ---- Notifications panel ------------------------------------------
        // A Slate flyout anchored beside the sidebar: header (new count, Mark all
        // read, Clear), filter chips, then the feed grouped by day. Opened by the
        // sidebar bell; closes on its [x], Escape, the bell, or clicking away.
        {
            static const bool kNtDemo = std::getenv("OKAY_UI_DEMO_USER") != nullptr;   // dev screenshot hook
            static bool ntSeeded = false;
            if (kNtDemo && !ntSeeded) {   // seed a realistic feed once (ages relative to now)
                ntSeeded = true;
                struct Seed { const char* key; const char* title; const char* body; int kind; Uint32 agoMin; bool unread; int go; };
                const Seed seed[] = {
                    {"demo:dm", "maya sent you a message", "Perfect. First playable by tonight? I can share the build link as soon as it's up.", 2, 2, true, 6},
                    {"demo:fr", "New friend request", "alexr wants to be your friend.", 1, 14, true, 6},
                    {"demo:upd", "Update installed", "Updated to v" OKAY_ENGINE_VERSION " \xE2\x80\x94 restart to finish.", 3, 65, true, -1},
                    {"demo:mention", "Game Jam Crew", "You were mentioned in #game-jam: \"@kingimann can you take the menus and the pause screen?\"", 0, 190, false, 8},
                    {"demo:acc", "priya accepted your friend request", "You can now chat and see when priya is playing.", 1, 26 * 60, false, 6},
                    {"demo:dms", "New messages", "3 unread direct messages from jonas", 2, 30 * 60, false, 6},
                    {"demo:news", "What's new in v2.776", "Servers get channel tabs, faster chat and a refreshed notifications panel.", 0, 3 * 1440, false, 0},
                    {"demo:dl", "Download finished", "Orbit Hopper (42 MB) is ready to play in your Library.", 3, 5 * 1440, false, 1},
                };
                const Uint32 t0 = SDL_GetTicks();
                for (const Seed& s : seed) {
                    Notif n; n.key = s.key; n.title = s.title; n.body = s.body; n.kind = s.kind;
                    n.time = t0 - s.agoMin * 60000u;   // may wrap: ages are always computed as (now - time) mod 2^32
                    n.read = !s.unread; n.gotoTab = s.go;
                    g_notifs.push_back(n);
                }
            }
        }
        static bool ntWasOpen = false;
        static int  ntFilter = 0;                  // 0 All, 1 Unread, 2 Friends, 3 Messages, 4 Updates, 5 System
        static std::set<std::string> ntHidden;     // dismissed items ("key\nbody"), so re-synced ones stay gone
        if (g_notifPanelOpen) {
            ImGuiViewport* vp = ImGui::GetMainViewport();
            const float navWNow = g_navCollapsed ? 64.0f : 232.0f;
            const float pw = std::max(300.0f, std::min(480.0f, vp->WorkSize.x - navWNow - 24.0f));
            const float ph = std::max(280.0f, std::min(640.0f, vp->WorkSize.y - 24.0f));
            const ImVec2 pp(vp->WorkPos.x + navWNow + 10.0f, vp->WorkPos.y + vp->WorkSize.y - ph - 12.0f);
            ImGui::SetNextWindowPos(pp, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(pw, ph), ImGuiCond_Always);
            if (!ntWasOpen) ImGui::SetNextWindowFocus();
            const ImVec4 panelBg = g_themeIndex == 0 ? okaytheme::kRaised : ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(panelBg.x, panelBg.y, panelBg.z, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.07f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
            ImGui::Begin("##notifpanel", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar(3);
            ImGui::PopStyleColor(2);
            const ImVec2 wpos = ImGui::GetWindowPos(), wsz = ImGui::GetWindowSize();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            {   // Soft drop shadow just outside the panel edge.
                const ImVec2 a = wpos, b(wpos.x + wsz.x, wpos.y + wsz.y);
                dl->PushClipRect(ImVec2(a.x - 24, a.y - 24), ImVec2(b.x + 24, b.y + 24), false);
                for (int i = 1; i <= 12; ++i)
                    dl->AddRect(ImVec2(a.x - i, a.y - i * 0.5f), ImVec2(b.x + i, b.y + i * 1.5f),
                                IM_COL32(0, 0, 0, std::max(0, 46 - i * 4)), 12.0f + i, 0, 1.0f);
                dl->PopClipRect();
            }
            const ImU32 cText = ImGui::GetColorU32(ImGuiCol_Text);
            const ImU32 cDim  = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const ImVec4 accLt(kAccent.x * 0.55f + 0.45f, kAccent.y * 0.55f + 0.45f, kAccent.z * 0.55f + 0.45f, 1.0f);
            auto sigOf    = [](const Notif& n) { return n.key + "\n" + n.body; };
            auto isHidden = [&](const Notif& n) { return ntHidden.count(sigOf(n)) != 0; };
            auto isUnread = [&](const Notif& n) { return !n.read || g_notifFresh.count(n.key) != 0; };
            auto kindOf   = [](const Notif& n) { return (n.kind >= 1 && n.kind <= 3) ? n.kind : 0; };
            int totalN = 0, unreadN = 0, kindN[4] = {0, 0, 0, 0};
            for (const auto& n : g_notifs)
                if (!isHidden(n)) { ++totalN; if (isUnread(n)) ++unreadN; ++kindN[kindOf(n)]; }
            int gotoTabReq = -1;
            bool closeReq = false;

            // ---- Header: title + "N new" pill, then Mark all read / Clear / close.
            const ImVec2 hp = ImGui::GetCursorScreenPos();
            const float cw = ImGui::GetContentRegionAvail().x;
            const float hh = 28.0f;
            ImGui::SetWindowFontScale(1.22f);
            const ImVec2 tsz = ImGui::CalcTextSize("Notifications");
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(hp.x, hp.y + (hh - tsz.y) * 0.5f), cText, "Notifications");
            ImGui::SetWindowFontScale(1.0f);
            if (unreadN > 0) {
                char nb[24]; std::snprintf(nb, sizeof(nb), "%d new", unreadN);
                const ImVec2 bs = ImGui::CalcTextSize(nb);
                const ImVec2 b0(hp.x + tsz.x + 10.0f, hp.y + hh * 0.5f - 10.0f);
                dl->AddRectFilled(b0, ImVec2(b0.x + bs.x + 14.0f, b0.y + 20.0f), ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.24f)), 10.0f);
                dl->AddText(ImVec2(b0.x + 7.0f, b0.y + (20.0f - bs.y) * 0.5f), ImGui::GetColorU32(accLt), nb);
            }
            auto textBtn = [&](const char* id, const char* label, float x, bool enabled, bool accent) -> bool {
                const ImVec2 ts = ImGui::CalcTextSize(label);
                const ImVec2 p(x, hp.y + (hh - 26.0f) * 0.5f), sz(ts.x + 16.0f, 26.0f);
                ImGui::SetCursorScreenPos(p);
                const bool c = ImGui::InvisibleButton(id, sz) && enabled;
                const bool h = enabled && ImGui::IsItemHovered();
                if (h) { dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(255, 255, 255, 16), 7.0f); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
                const ImU32 col = !enabled ? ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.55f)
                                : h ? cText : accent ? ImGui::GetColorU32(accLt) : cDim;
                dl->AddText(ImVec2(p.x + 8.0f, p.y + (sz.y - ts.y) * 0.5f), col, label);
                return c;
            };
            {   // close (round x) on the far right
                const ImVec2 xp(hp.x + cw - 26.0f, hp.y + (hh - 26.0f) * 0.5f);
                ImGui::SetCursorScreenPos(xp);
                if (ImGui::InvisibleButton("##ntclose", ImVec2(26, 26))) closeReq = true;
                const bool xh = ImGui::IsItemHovered();
                const ImVec2 xc(xp.x + 13.0f, xp.y + 13.0f);
                if (xh) { dl->AddCircleFilled(xc, 13.0f, IM_COL32(255, 255, 255, 20), 24); ImGui::SetTooltip("Close  (Esc)"); }
                const ImU32 xcol = xh ? cText : cDim;
                dl->AddLine(ImVec2(xc.x - 4.5f, xc.y - 4.5f), ImVec2(xc.x + 4.5f, xc.y + 4.5f), xcol, 1.6f);
                dl->AddLine(ImVec2(xc.x + 4.5f, xc.y - 4.5f), ImVec2(xc.x - 4.5f, xc.y + 4.5f), xcol, 1.6f);
                const float clearW = ImGui::CalcTextSize("Clear").x + 16.0f;
                const float markW  = ImGui::CalcTextSize("Mark all read").x + 16.0f;
                const float clearX = xp.x - 6.0f - clearW, markX = clearX - 2.0f - markW;
                if (textBtn("##ntmark", "Mark all read", markX, unreadN > 0, true)) {
                    for (auto& n : g_notifs) n.read = true;
                    g_notifFresh.clear();
                }
                if (textBtn("##ntclear", "Clear", clearX, totalN > 0, false)) {
                    for (auto& n : g_notifs) { ntHidden.insert(sigOf(n)); n.read = true; }
                    g_notifFresh.clear();
                }
            }
            ImGui::SetCursorScreenPos(ImVec2(hp.x, hp.y + hh + 10.0f));
            ImGui::Dummy(ImVec2(0, 0));

            // ---- Filter chips (wrap onto a second row on narrow panels).
            {
                const char* chipLbl[] = {"All", "Unread", "Friends", "Messages", "Updates", "System"};
                const ImVec2 cp = ImGui::GetCursorScreenPos();
                const float ch = ImGui::GetTextLineHeight() + 10.0f;
                float x = cp.x, y = cp.y;
                for (int k = 0; k < 6; ++k) {
                    const ImVec2 ts = ImGui::CalcTextSize(chipLbl[k]);
                    char cnt[8] = "";
                    if (k == 1 && unreadN > 0) std::snprintf(cnt, sizeof(cnt), "%d", unreadN);
                    const ImVec2 cs = ImGui::CalcTextSize(cnt);
                    const float w = ts.x + 18.0f + (cnt[0] ? cs.x + 16.0f : 0.0f);
                    if (x > cp.x && x + w > cp.x + cw) { x = cp.x; y += ch + 6.0f; }
                    ImGui::SetCursorScreenPos(ImVec2(x, y));
                    ImGui::PushID(k);
                    if (ImGui::InvisibleButton("##ntchip", ImVec2(w, ch))) ntFilter = k;
                    const bool h = ImGui::IsItemHovered();
                    ImGui::PopID();
                    const bool on = ntFilter == k;
                    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + ch),
                                      on ? ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.24f))
                                         : (h ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 9)), ch * 0.5f);
                    dl->AddText(ImVec2(x + 9.0f, y + (ch - ts.y) * 0.5f), on ? ImGui::GetColorU32(accLt) : (h ? cText : cDim), chipLbl[k]);
                    if (cnt[0]) {
                        const float bx = x + 9.0f + ts.x + 6.0f, bw = cs.x + 10.0f;
                        dl->AddRectFilled(ImVec2(bx, y + (ch - 16) * 0.5f), ImVec2(bx + bw, y + (ch + 16) * 0.5f), ImGui::GetColorU32(kAccent), 8.0f);
                        dl->AddText(ImVec2(bx + 5.0f, y + (ch - cs.y) * 0.5f), IM_COL32_WHITE, cnt);
                    }
                    if (h) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    x += w + 6.0f;
                }
                ImGui::SetCursorScreenPos(ImVec2(cp.x, y + ch + 12.0f));
                const ImVec2 lp = ImGui::GetCursorScreenPos();
                dl->AddLine(ImVec2(lp.x - 16.0f, lp.y), ImVec2(lp.x + cw + 16.0f, lp.y), IM_COL32(255, 255, 255, 16), 1.0f);
                ImGui::Dummy(ImVec2(0, 4));
            }

            // ---- The feed, newest first, grouped Today / Yesterday / Earlier.
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);
            ImGui::BeginChild("##ntlist", ImVec2(0, 0), ImGuiChildFlags_None);
            ImGui::PopStyleVar();
            {
                ImDrawList* ld = ImGui::GetWindowDrawList();
                const Uint32 nowT = SDL_GetTicks();
                const long nowUnix = (long)std::time(nullptr);
                const long today = DayKey(nowUnix), yday = DayKey(nowUnix - 86400);
                static const int kFilterKind[] = {-1, -1, 1, 2, 3, 0};
                std::vector<int> order;
                for (int i = 0; i < (int)g_notifs.size(); ++i) {
                    const Notif& n = g_notifs[(std::size_t)i];
                    if (isHidden(n)) continue;
                    if (ntFilter == 1 && !isUnread(n)) continue;
                    if (ntFilter >= 2 && kindOf(n) != kFilterKind[ntFilter]) continue;
                    order.push_back(i);
                }
                std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                    return (Uint32)(nowT - g_notifs[(std::size_t)a].time) < (Uint32)(nowT - g_notifs[(std::size_t)b].time);
                });
                // First line that fits, then the rest ellipsized onto a second line.
                auto wrap2 = [](const std::string& s, float maxW, std::string& l1, std::string& l2) {
                    l1.clear(); l2.clear();
                    if (ImGui::CalcTextSize(s.c_str()).x <= maxW) { l1 = s; return; }
                    std::size_t cut = 0;
                    for (std::size_t i = 0; i < s.size(); ++i)
                        if (s[i] == ' ') { if (ImGui::CalcTextSize(s.c_str(), s.c_str() + i).x <= maxW) cut = i; else break; }
                    if (cut == 0) { l1 = FitText(s, maxW); return; }
                    l1 = s.substr(0, cut); l2 = FitText(s.substr(cut + 1), maxW);
                };
                const float th = ImGui::GetTextLineHeight();
                int lastGroup = -1;
                int dismissIdx = -1;
                for (int idx : order) {
                    Notif& n = g_notifs[(std::size_t)idx];
                    const int kind = kindOf(n);
                    const bool un = isUnread(n);
                    const Uint32 ageS = (Uint32)(nowT - n.time) / 1000u;
                    const long dk = DayKey(nowUnix - (long)ageS);
                    const int grp = dk == today ? 0 : dk == yday ? 1 : 2;
                    if (grp != lastGroup) {
                        if (lastGroup != -1) ImGui::Dummy(ImVec2(0, 4));
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.0f);
                        ImGui::SetWindowFontScale(0.80f);
                        ImGui::TextDisabled("%s", grp == 0 ? "TODAY" : grp == 1 ? "YESTERDAY" : "EARLIER");
                        ImGui::SetWindowFontScale(1.0f);
                        ImGui::Dummy(ImVec2(0, 1));
                        lastGroup = grp;
                    }
                    ImGui::PushID(n.key.c_str());
                    const ImVec2 rp = ImGui::GetCursorScreenPos();
                    const float rw = ImGui::GetContentRegionAvail().x;
                    const float icD = 36.0f, tx = rp.x + 10.0f + icD + 12.0f;
                    const float textW = rw - (tx - rp.x) - 46.0f;
                    std::string b1, b2; wrap2(n.body, textW, b1, b2);
                    const int bodyLines = n.body.empty() ? 0 : (b2.empty() ? 1 : 2);
                    const float rh = std::max(58.0f, 11.0f + th + 3.0f + bodyLines * (th + 1.0f) + 10.0f);
                    ImGui::SetNextItemAllowOverlap();
                    const bool click = ImGui::InvisibleButton("##ntrow", ImVec2(rw, rh));
                    const bool hov = ImGui::IsMouseHoveringRect(rp, ImVec2(rp.x + rw, rp.y + rh)) &&
                                          ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ld->AddRectFilled(rp, ImVec2(rp.x + rw, rp.y + rh),
                                      hov ? IM_COL32(255, 255, 255, 15) : un ? ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.07f)) : IM_COL32(0, 0, 0, 0), 10.0f);
                    // Kind icon on a tinted disc.
                    const ImVec4 kc = kind == 1 ? ImVec4(0.38f, 0.68f, 0.98f, 1.0f)    // friend
                                    : kind == 2 ? ImVec4(0.38f, 0.80f, 0.55f, 1.0f)    // message
                                    : kind == 3 ? ImVec4(0.66f, 0.54f, 0.98f, 1.0f)    // update
                                                : ImVec4(0.95f, 0.72f, 0.30f, 1.0f);   // system
                    const ImVec2 ic(rp.x + 10.0f + icD * 0.5f, rp.y + 11.0f + icD * 0.5f - 2.0f);
                    ld->AddCircleFilled(ic, icD * 0.5f, ImGui::GetColorU32(ImVec4(kc.x, kc.y, kc.z, 0.17f)), 32);
                    const int icon = kind == 1 ? 6 : kind == 2 ? kIcoChat : kind == 3 ? kIcoRefresh : kIcoBell;
                    DrawNavIcon(ld, icon, ic, ImGui::GetColorU32(ImVec4(kc.x * 0.8f + 0.2f, kc.y * 0.8f + 0.2f, kc.z * 0.8f + 0.2f, 1.0f)));
                    // Title (faux-bold + brighter when unread), then up to two body lines.
                    const std::string title = FitText(n.title.empty() ? std::string("Notification") : n.title, textW);
                    const ImU32 tcol = un ? cText : ImGui::GetColorU32(ImGuiCol_Text, 0.80f);
                    ld->AddText(ImVec2(tx, rp.y + 11.0f), tcol, title.c_str());
                    if (un) ld->AddText(ImVec2(tx + 0.7f, rp.y + 11.0f), tcol, title.c_str());
                    float by = rp.y + 11.0f + th + 3.0f;
                    if (!b1.empty()) { ld->AddText(ImVec2(tx, by), cDim, b1.c_str()); by += th + 1.0f; }
                    if (!b2.empty()) ld->AddText(ImVec2(tx, by), cDim, b2.c_str());
                    // Right column: relative time (or a dismiss x on hover) + unread dot.
                    char rt[16];
                    if (ageS < 60) std::snprintf(rt, sizeof(rt), "now");
                    else if (ageS < 3600) std::snprintf(rt, sizeof(rt), "%um", (unsigned)(ageS / 60));
                    else if (ageS < 86400) std::snprintf(rt, sizeof(rt), "%uh", (unsigned)(ageS / 3600));
                    else std::snprintf(rt, sizeof(rt), "%ud", (unsigned)(ageS / 86400));
                    const float rx = rp.x + rw - 12.0f;
                    if (hov) {
                        const ImVec2 xp(rx - 22.0f, rp.y + 8.0f);
                        ImGui::SetCursorScreenPos(xp);
                        if (ImGui::InvisibleButton("##ntx", ImVec2(22, 22))) dismissIdx = idx;
                        const bool xh = ImGui::IsItemHovered();
                        const ImVec2 xc(xp.x + 11.0f, xp.y + 11.0f);
                        ld->AddCircleFilled(xc, 11.0f, xh ? IM_COL32(255, 255, 255, 34) : IM_COL32(255, 255, 255, 14), 20);
                        const ImU32 xcol = xh ? cText : cDim;
                        ld->AddLine(ImVec2(xc.x - 3.5f, xc.y - 3.5f), ImVec2(xc.x + 3.5f, xc.y + 3.5f), xcol, 1.5f);
                        ld->AddLine(ImVec2(xc.x + 3.5f, xc.y - 3.5f), ImVec2(xc.x - 3.5f, xc.y + 3.5f), xcol, 1.5f);
                        if (xh) ImGui::SetTooltip("Dismiss");
                    } else {
                        const ImVec2 ts = ImGui::CalcTextSize(rt);
                        ld->AddText(ImVec2(rx - ts.x, rp.y + 11.0f), un ? ImGui::GetColorU32(accLt) : cDim, rt);
                    }
                    if (un) ld->AddCircleFilled(ImVec2(rx - 5.0f, rp.y + 11.0f + th + 3.0f + th * 0.5f + 1.0f), 4.5f, ImGui::GetColorU32(kAccent), 14);
                    if (click && dismissIdx != idx) {
                        n.read = true; g_notifFresh.erase(n.key);
                        if (n.gotoTab >= 0) gotoTabReq = n.gotoTab;
                    }
                    ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + rh + 3.0f));
                    ImGui::Dummy(ImVec2(0, 0));
                    ImGui::PopID();
                }
                if (dismissIdx >= 0 && dismissIdx < (int)g_notifs.size()) {
                    Notif& n = g_notifs[(std::size_t)dismissIdx];
                    ntHidden.insert(sigOf(n)); n.read = true; g_notifFresh.erase(n.key);
                }
                // Static notifications that were dismissed can go for good; the
                // re-synced ones (dyn:/news:) stay hidden via ntHidden.
                for (auto it = g_notifs.begin(); it != g_notifs.end();) {
                    const bool synced = it->key.rfind("dyn:", 0) == 0 || it->key.rfind("news:", 0) == 0;
                    if (!synced && isHidden(*it)) { ntHidden.erase(sigOf(*it)); it = g_notifs.erase(it); }
                    else ++it;
                }
                if (order.empty()) {   // ---- Empty state ----
                    const float aw = ImGui::GetContentRegionAvail().x, ah = ImGui::GetContentRegionAvail().y;
                    ImGui::Dummy(ImVec2(0, std::max(16.0f, ah * 0.20f)));
                    const ImVec2 c(ImGui::GetCursorScreenPos().x + aw * 0.5f, ImGui::GetCursorScreenPos().y + 30.0f);
                    const bool caughtUp = totalN == 0 || ntFilter == 1;
                    ld->AddCircleFilled(c, 30.0f, IM_COL32(255, 255, 255, 12), 40);
                    DrawNavIcon(ld, kIcoBell, c, cDim);
                    if (caughtUp) {   // small green check badge
                        const ImVec2 bc(c.x + 20.0f, c.y + 20.0f);
                        ld->AddCircleFilled(bc, 10.0f, ImGui::GetColorU32(panelBg), 20);
                        ld->AddCircleFilled(bc, 8.0f, IM_COL32(64, 186, 104, 255), 20);
                        ld->PathLineTo(ImVec2(bc.x - 3.6f, bc.y + 0.2f)); ld->PathLineTo(ImVec2(bc.x - 1.0f, bc.y + 2.8f));
                        ld->PathLineTo(ImVec2(bc.x + 3.8f, bc.y - 2.8f)); ld->PathStroke(IM_COL32_WHITE, 0, 1.8f);
                    }
                    ImGui::Dummy(ImVec2(0, 74));
                    auto centered = [&](const char* s, bool dim) {
                        const float tw = ImGui::CalcTextSize(s).x;
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (aw - tw) * 0.5f));
                        if (dim) ImGui::TextDisabled("%s", s); else ImGui::TextUnformatted(s);
                    };
                    const char* hint = totalN == 0 ? "Friend requests, messages and updates show up here."
                                     : ntFilter == 1 ? "No unread notifications."
                                     : ntFilter == 2 ? "No friend notifications."
                                     : ntFilter == 3 ? "No message notifications."
                                     : ntFilter == 4 ? "No update notifications."
                                                     : "No system notifications.";
                    ImGui::SetWindowFontScale(1.08f);
                    centered(caughtUp ? "You're all caught up" : "Nothing here", false);
                    ImGui::SetWindowFontScale(1.0f);
                    centered(hint, true);
                }
            }
            ImGui::EndChild();
            ImGui::End();
            if (gotoTabReq >= 0) { tab = gotoTabReq; closeReq = true; }
            if (closeReq || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) g_notifPanelOpen = false;
            // Clicking anywhere outside closes it (the bell itself toggles).
            if (ntWasOpen && g_notifPanelOpen && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
                const ImVec2 m = ImGui::GetIO().MousePos;
                const bool inPanel = m.x >= wpos.x && m.y >= wpos.y && m.x < wpos.x + wsz.x && m.y < wpos.y + wsz.y;
                const bool inBell = m.x >= g_notifBellMin.x && m.y >= g_notifBellMin.y && m.x < g_notifBellMax.x && m.y < g_notifBellMax.y;
                if (!inPanel && !inBell) g_notifPanelOpen = false;
            }
        } else {
            g_notifFresh.clear();   // "new since you opened it" only lasts while it's open
        }
        ntWasOpen = g_notifPanelOpen;

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
        // Unity Hub-style rail: a darker, borderless column with the logo and the
        // collapse toggle on top, grouped rows (inset + rounded; the active one
        // gets a soft fill, an accent bar and an accent icon), and notifications,
        // settings and a profile card pinned to the bottom. The page sits flush
        // beside it, separated by a single hairline.
        static const char* kUiDemoUser = std::getenv("OKAY_UI_DEMO_USER");   // dev screenshot hook
        const bool navSignedIn = account.IsLoggedIn() || kUiDemoUser;
        const float kNavExpandedW = 232.0f, kNavCollapsedW = 64.0f;
        const float navW = g_navCollapsed ? kNavCollapsedW : kNavExpandedW;
        const float navRowH = g_navCollapsed ? 40.0f : 36.0f;
        const bool  navLight = (g_themeIndex == 2);
        const ImVec4 wbg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        const float  navK = navLight ? 0.955f : 0.78f;
        const ImVec4 navBg = g_themeIndex == 0 ? okaytheme::kSunken : ImVec4(wbg.x * navK, wbg.y * navK, wbg.z * navK, 1.0f);
        const ImU32 navHover  = navLight ? IM_COL32(0, 0, 0, 12) : IM_COL32(255, 255, 255, 11);
        const ImU32 navSel    = navLight ? IM_COL32(0, 0, 0, 22) : IM_COL32(255, 255, 255, 21);
        const ImU32 navTextHi = navLight ? IM_COL32(22, 24, 28, 255) : IM_COL32(244, 245, 247, 255);
        const ImU32 navText   = ImGui::GetColorU32(ImGuiCol_Text, 0.86f);
        const ImU32 navIcon   = ImGui::GetColorU32(ImGuiCol_Text, 0.62f);
        const ImU32 navDivide = navLight ? IM_COL32(0, 0, 0, 22) : IM_COL32(255, 255, 255, 14);
        const ImU32 navRed    = IM_COL32(222, 76, 76, 255);
        const ImU32 navAccent = ImGui::GetColorU32(kAccent);

        // One sidebar row: icon + label (+ count pill). Returns true on click.
        auto navRow = [&](const char* id, const char* label, int icon, bool sel, int badge, ImU32 badgeCol) -> bool {
            ImVec2 mn = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            ImVec2 mx(mn.x + w, mn.y + navRowH);
            bool click = ImGui::InvisibleButton(id, ImVec2(w, navRowH));
            bool hov = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (sel || hov) dl->AddRectFilled(mn, mx, sel ? navSel : navHover, 7.0f);
            if (sel) dl->AddRectFilled(ImVec2(mn.x, mn.y + 10), ImVec2(mn.x + 3, mx.y - 10), navAccent, 1.5f);
            const float cy = mn.y + navRowH * 0.5f;
            ImVec2 ic(g_navCollapsed ? mn.x + w * 0.5f : mn.x + 22.0f, cy);
            DrawNavIcon(dl, icon, ic, sel ? navAccent : (hov ? navTextHi : navIcon));
            if (!g_navCollapsed)
                dl->AddText(ImVec2(mn.x + 42.0f, cy - ImGui::GetTextLineHeight() * 0.5f),
                            (sel || hov) ? navTextHi : navText, label);
            if (badge > 0) {
                if (g_navCollapsed) {
                    ImVec2 bc(ic.x + 9.0f, cy - 8.0f);
                    dl->AddCircleFilled(bc, 5.5f, ImGui::GetColorU32(navBg), 12);
                    dl->AddCircleFilled(bc, 4.0f, badgeCol, 12);
                } else {
                    char nb[8]; std::snprintf(nb, sizeof(nb), badge > 99 ? "99+" : "%d", badge);
                    ImVec2 ts = ImGui::CalcTextSize(nb);
                    float bw = std::max(20.0f, ts.x + 12.0f), bx = mx.x - bw - 8.0f;
                    dl->AddRectFilled(ImVec2(bx, cy - 9.0f), ImVec2(bx + bw, cy + 9.0f), badgeCol, 9.0f);
                    dl->AddText(ImVec2(bx + (bw - ts.x) * 0.5f, cy - ts.y * 0.5f), IM_COL32_WHITE, nb);
                }
            }
            if (hov) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (g_navCollapsed) ImGui::SetTooltip("%s", label);
            }
            return click;
        };
        // Group label (expanded) or a short hairline (collapsed).
        auto navSection = [&](const char* title) {
            if (g_navCollapsed) {
                ImGui::Dummy(ImVec2(0, 6));
                ImVec2 p = ImGui::GetCursorScreenPos();
                float w = ImGui::GetContentRegionAvail().x;
                ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + 12, p.y), ImVec2(p.x + w - 12, p.y), navDivide, 1.0f);
                ImGui::Dummy(ImVec2(0, 7));
                return;
            }
            ImGui::Dummy(ImVec2(0, 12));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12);
            ImGui::SetWindowFontScale(0.78f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(title);
            ImGui::PopStyleColor();
            ImGui::SetWindowFontScale(1.0f);
            ImGui::Dummy(ImVec2(0, 4));
        };

        ImGui::PushStyleColor(ImGuiCol_ChildBg, navBg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 14));
        ImGui::BeginChild("nav", ImVec2(navW, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
        {   // ---- Header: logo mark + name/version, and the collapse toggle ----
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float S = 32.0f;
            ImVec2 start = ImGui::GetCursorScreenPos();
            float avail = ImGui::GetContentRegionAvail().x;
            ImVec2 lp(g_navCollapsed ? start.x + (avail - S) * 0.5f : start.x + 6.0f, start.y);
            ImVec2 br(lp.x + S, lp.y + S);
            // Brand tile: accent fill with a soft top sheen; ringed planet + star.
            dl->AddRectFilled(lp, br, ImGui::GetColorU32(kAccentDim), 8.0f);
            dl->AddRectFilled(lp, ImVec2(br.x, lp.y + S * 0.5f), IM_COL32(255, 255, 255, 20), 8.0f, ImDrawFlags_RoundCornersTop);
            ImVec2 c(lp.x + S * 0.47f, lp.y + S * 0.53f);
            dl->AddCircleFilled(c, 6.8f, IM_COL32(236, 240, 248, 255), 24);
            ImVec2 ring[28];
            for (int k = 0; k < 28; ++k) {
                float a = k * (6.2831853f / 28), rx = 12.0f, ry = 4.0f, rot = -0.5f;
                float ex = std::cos(a) * rx, ey = std::sin(a) * ry;
                ring[k] = ImVec2(c.x + ex * std::cos(rot) - ey * std::sin(rot), c.y + ex * std::sin(rot) + ey * std::cos(rot));
            }
            dl->PushClipRect(lp, br, true);
            dl->AddPolyline(ring, 28, IM_COL32(255, 255, 255, 235), ImDrawFlags_Closed, 1.5f);
            dl->PopClipRect();
            dl->AddCircleFilled(ImVec2(lp.x + S * 0.78f, lp.y + S * 0.24f), 1.4f, IM_COL32(255, 255, 255, 240));
            ImGui::Dummy(ImVec2(avail, S));
            if (!g_navCollapsed) {
                float tx = br.x + 10.0f;
                dl->AddText(ImVec2(tx, lp.y - 1.0f), navTextHi, "OkaySpace");
                // Version doubles as a "check for updates" link.
                const char* ver = "v" OKAY_ENGINE_VERSION;
                float fs = ImGui::GetFontSize() * 0.82f;
                ImVec2 vsz = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, ver);
                ImVec2 vp(tx, lp.y + S - vsz.y);
                ImGui::SetCursorScreenPos(vp);
                bool vclick = ImGui::InvisibleButton("##navver", vsz);
                bool vhov = ImGui::IsItemHovered();
                dl->AddText(ImGui::GetFont(), fs, vp, vhov ? navTextHi : ImGui::GetColorU32(ImGuiCol_TextDisabled), ver);
                if (vhov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("Check for updates"); }
                if (vclick && GetState() != Up_Checking && GetState() != Up_Downloading) StartUpdateCheck();
                // Collapse toggle on the right of the header.
                ImVec2 tp(start.x + avail - 30.0f, start.y + 2.0f);
                ImGui::SetCursorScreenPos(tp);
                if (ImGui::InvisibleButton("##navtoggle", ImVec2(28, 28))) { g_navCollapsed = true; SavePrefs(); }
                bool th = ImGui::IsItemHovered();
                if (th) dl->AddRectFilled(tp, ImVec2(tp.x + 28, tp.y + 28), navHover, 6.0f);
                DrawNavIcon(dl, kIcoPanel, ImVec2(tp.x + 14, tp.y + 14), th ? navTextHi : navIcon);
                if (th) ImGui::SetTooltip("Collapse sidebar");
                ImGui::SetCursorScreenPos(ImVec2(start.x, br.y));
                ImGui::Dummy(ImVec2(avail, 6));
            } else {
                ImGui::Dummy(ImVec2(0, 4));
                if (navRow("##navtoggle", "Expand sidebar", kIcoPanel, false, 0, 0)) { g_navCollapsed = false; SavePrefs(); }
            }
        }
        RefreshNotifs();

        // Footer (pinned): an update card while something is happening, then
        // Notifications, Settings and the profile card. The rows above scroll on
        // short windows instead of running into it.
        const UpState st = GetState();
        const bool upBusy = (st == Up_Checking || st == Up_Downloading);
        const bool upAvail = !g_upAvailable.empty() && !upBusy && st != Up_Updated;
        const bool upNote = upBusy || st == Up_Updated || st == Up_Failed || upAvail;
        const float profH = g_navCollapsed ? 44.0f : 52.0f;
        const float upH = upNote ? (g_navCollapsed ? navRowH + 2.0f : ((g_upRelaunchNeeded && st == Up_Updated) || upAvail ? 84.0f : 58.0f)) : 0.0f;
        const float footH = upH + (navRowH + 2.0f) * 2 + 15.0f + profH;
        ImGui::BeginChild("navitems", ImVec2(0, ImGui::GetContentRegionAvail().y - footH), false, ImGuiWindowFlags_NoScrollbar);
        const char* navs[] = {"Create", "Play", "Community", "Account", "Settings", "Achievements", "Friends", "Market", "Servers", "Forums", "Downloads"};
        auto tabRow = [&](int i, int badge = 0, ImU32 badgeCol = 0) {
            char id[16]; std::snprintf(id, sizeof(id), "##nav%d", i);
            if (navRow(id, navs[i], i, tab == i, badge, badgeCol)) { tab = i; ClosePublicProfile(); }
        };
        const ImU32 navSoft = ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.40f));
        navSection("MENU");
        tabRow(0);
        tabRow(1, LibraryGameCount(scenes), navSoft);     // built + web games (not editor projects)
        tabRow(2);
        tabRow(7);
        tabRow(10, ActiveDownloads(), navAccent);         // in-flight downloads
        navSection("SOCIAL");
        tabRow(6, g_friendRequests, navRed);              // pending friend requests
        if (navSignedIn && navRow("##navchat", "Chat", kIcoChat, g_chatWinOpen, g_dmUnread, navRed))
            g_chatWinOpen = !g_chatWinOpen;
        tabRow(8);
        tabRow(9);
        tabRow(5);
        ImGui::EndChild();   // end scrollable nav items

        {   // ---- Update card (only while checking/downloading, or with news) ----
            const char* head = (st == Up_Downloading) ? "Updating\xE2\x80\xA6"
                             : (st == Up_Checking)    ? "Checking for updates\xE2\x80\xA6"
                             : (st == Up_Updated)     ? "Update installed"
                             : upAvail                ? "Update available"
                                                      : "Update failed";
            ImU32 tone = upAvail ? IM_COL32(110, 205, 140, 255)
                       : (st == Up_Failed) ? IM_COL32(240, 120, 120, 255)
                       : (st == Up_Updated) ? IM_COL32(110, 205, 140, 255) : navAccent;
            std::string upMsg = upAvail ? "v" + g_upAvailable + " is ready" : GetUpMsg();
            if (upNote && g_navCollapsed) {
                ImVec2 mn = ImGui::GetCursorScreenPos();
                float w = ImGui::GetContentRegionAvail().x;
                bool click = ImGui::InvisibleButton("##navupd", ImVec2(w, navRowH));
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 uc(mn.x + w * 0.5f, mn.y + navRowH * 0.5f);
                if (ImGui::IsItemHovered()) {
                    dl->AddRectFilled(mn, ImVec2(mn.x + w, mn.y + navRowH), navHover, 7.0f);
                    ImGui::SetTooltip("%s\n%s%s", head, upMsg.c_str(),
                        upAvail ? "\nClick to install" : st == Up_Failed ? "\nClick to retry" : (g_upRelaunchNeeded && st == Up_Updated) ? "\nClick to restart" : "");
                }
                if (upBusy) {
                    float a0 = (float)ImGui::GetTime() * 6.0f;
                    dl->PathArcTo(uc, 7.0f, a0, a0 + 4.6f, 20);
                    dl->PathStroke(tone, 0, 2.2f);
                } else DrawNavIcon(dl, kIcoRefresh, uc, tone);
                if (click) {
                    if (upAvail) StartUpdateCheck(true);
                    else if (st == Up_Failed) StartUpdateCheck();
                    else if (g_upRelaunchNeeded && st == Up_Updated) { Launch(g_selfPath); running = false; }
                }
                ImGui::Dummy(ImVec2(0, 2));
            } else if (upNote) {
                ImVec2 mn = ImGui::GetCursorScreenPos();
                float w = ImGui::GetContentRegionAvail().x, h = upH - 8.0f;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(mn, ImVec2(mn.x + w, mn.y + h), navSel, 8.0f);
                dl->AddRectFilled(mn, ImVec2(mn.x + 3, mn.y + h), tone, 8.0f, ImDrawFlags_RoundCornersLeft);
                ImVec2 ic(mn.x + 20, mn.y + 17);
                if (upBusy) {
                    float a0 = (float)ImGui::GetTime() * 6.0f;
                    dl->PathArcTo(ic, 6.0f, a0, a0 + 4.6f, 20);
                    dl->PathStroke(tone, 0, 2.0f);
                } else DrawNavIcon(dl, kIcoRefresh, ic, tone);
                dl->AddText(ImVec2(mn.x + 36, mn.y + 8), navTextHi, head);
                std::string sub = upMsg.empty() ? std::string("v" OKAY_ENGINE_VERSION) : upMsg;
                while (sub.size() > 4 && ImGui::CalcTextSize(sub.c_str()).x > w - 44) sub = sub.substr(0, sub.size() - 4) + "\xE2\x80\xA6";
                ImGui::SetCursorScreenPos(ImVec2(mn.x + 36, mn.y + 27));
                ImGui::TextDisabled("%s", sub.c_str());
                if (ImGui::IsItemHovered() && !upMsg.empty()) ImGui::SetTooltip("%s", upMsg.c_str());
                if (upAvail) {
                    ImGui::SetCursorScreenPos(ImVec2(mn.x + 36, mn.y + 50));
                    if (PrimaryButton("Install now", ImVec2(w - 48, 24))) StartUpdateCheck(true);
                } else if (st == Up_Failed) {
                    ImGui::SameLine(0, 0);
                    ImGui::SetCursorScreenPos(ImVec2(mn.x + w - 50, mn.y + 7));
                    if (ImGui::SmallButton("Retry")) StartUpdateCheck();
                }
                if (g_upRelaunchNeeded && st == Up_Updated) {
                    ImGui::SetCursorScreenPos(ImVec2(mn.x + 36, mn.y + 50));
                    if (PrimaryButton("Restart now", ImVec2(w - 48, 24))) { Launch(g_selfPath); running = false; }
                }
                ImGui::SetCursorScreenPos(ImVec2(mn.x, mn.y + h + 8.0f));
            }
        }
        {   // ---- Notifications + Settings ----
            int unread = NotifUnread();
            if (navRow("##navbell", "Notifications", kIcoBell, g_notifPanelOpen, unread, navRed)) {
                g_notifPanelOpen = !g_notifPanelOpen;
                if (g_notifPanelOpen) {
                    g_notifFresh.clear();
                    for (auto& n : g_notifs) { if (!n.read) g_notifFresh.insert(n.key); n.read = true; }   // opening clears unread
                }
            }
            g_notifBellMin = ImGui::GetItemRectMin(); g_notifBellMax = ImGui::GetItemRectMax();
            if (navRow("##nav4", navs[4], 4, tab == 4, 0, 0)) { tab = 4; ClosePublicProfile(); }
        }
        {   // ---- Profile card (opens the Account tab) ----
            ImGui::Dummy(ImVec2(0, 6));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 dp = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            dl->AddLine(ImVec2(dp.x + 4, dp.y), ImVec2(dp.x + w - 4, dp.y), navDivide, 1.0f);
            ImGui::Dummy(ImVec2(0, 6));
            ImVec2 mn = ImGui::GetCursorScreenPos();
            const float h = profH - 4.0f;
            bool click = ImGui::InvisibleButton("##navprofile", ImVec2(w, h));
            bool hov = ImGui::IsItemHovered();
            bool sel = (tab == 3);
            if (sel || hov) dl->AddRectFilled(mn, ImVec2(mn.x + w, mn.y + h), sel ? navSel : navHover, 8.0f);
            std::string uname = kUiDemoUser ? std::string(kUiDemoUser)
                              : account.IsLoggedIn() ? account.CurrentSession().username : std::string();
            const float A = g_navCollapsed ? 30.0f : 32.0f;
            ImVec2 ac(g_navCollapsed ? mn.x + w * 0.5f : mn.x + 8.0f + A * 0.5f, mn.y + h * 0.5f);
            if (!navSignedIn) {   // generic person on a neutral disc
                dl->AddCircleFilled(ac, A * 0.5f, navLight ? IM_COL32(0, 0, 0, 22) : IM_COL32(255, 255, 255, 22), 28);
                DrawNavIcon(dl, 3, ImVec2(ac.x, ac.y + 1.0f), navIcon);
            } else {
                SDL_Texture* at = AvatarTexture(g_myAvatarSlot ? *g_myAvatarSlot : std::string());
                if (at) dl->AddImageRounded((ImTextureID)(intptr_t)at, ImVec2(ac.x - A * 0.5f, ac.y - A * 0.5f),
                                            ImVec2(ac.x + A * 0.5f, ac.y + A * 0.5f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, A * 0.5f);
                else {
                    ImVec4 nc = NameColor(uname);
                    dl->AddCircleFilled(ac, A * 0.5f, ImGui::GetColorU32(ImVec4(nc.x * 0.6f, nc.y * 0.6f, nc.z * 0.6f, 1.0f)), 28);
                    char init[2] = {uname.empty() ? '?' : (char)std::toupper((unsigned char)uname[0]), 0};
                    ImVec2 ts = ImGui::CalcTextSize(init);
                    dl->AddText(ImVec2(ac.x - ts.x * 0.5f, ac.y - ts.y * 0.5f), IM_COL32_WHITE, init);
                }
                // Presence dot.
                ImU32 pc = !account.IsOnline() && !kUiDemoUser ? IM_COL32(140, 146, 156, 255)
                         : g_onlineStatus == 1 ? IM_COL32(236, 180, 64, 255)
                         : g_onlineStatus == 2 ? IM_COL32(140, 146, 156, 255) : IM_COL32(76, 200, 110, 255);
                ImVec2 pd(ac.x + A * 0.36f, ac.y + A * 0.36f);
                dl->AddCircleFilled(pd, 5.5f, ImGui::GetColorU32(navBg), 12);
                dl->AddCircleFilled(pd, 4.0f, pc, 12);
            }
            if (!g_navCollapsed) {
                float tx = mn.x + 16.0f + A, th = ImGui::GetTextLineHeight();
                std::string line1 = navSignedIn ? uname : std::string("Sign in");
                std::string line2 = !navSignedIn ? std::string("Friends, chat & cloud sync")
                                  : (!account.IsOnline() && !kUiDemoUser) ? std::string("Offline")
                                  : g_onlineStatus == 1 ? std::string("Away")
                                  : g_onlineStatus == 2 ? std::string("Invisible") : std::string("Online");
                float maxW = w - (tx - mn.x) - 8.0f;
                while (line1.size() > 4 && ImGui::CalcTextSize(line1.c_str()).x > maxW) line1 = line1.substr(0, line1.size() - 4) + "\xE2\x80\xA6";
                dl->AddText(ImVec2(tx, mn.y + h * 0.5f - th + 1.0f), navTextHi, line1.c_str());
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.86f, ImVec2(tx, mn.y + h * 0.5f + 2.0f),
                            ImGui::GetColorU32(ImGuiCol_TextDisabled), line2.c_str());
            }
            if (hov) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (g_navCollapsed) ImGui::SetTooltip(navSignedIn ? "%s  \xC2\xB7  Account" : "Sign in", uname.c_str());
            }
            if (click) { tab = 3; g_lastTab = 3; SavePrefs(); ClosePublicProfile(); }
        }
        ImGui::PopStyleVar();   // ItemSpacing
        ImGui::EndChild();
        ImGui::PopStyleColor(); // nav ChildBg
        {   // A single hairline between the rail and the page.
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(b.x - 0.5f, a.y), ImVec2(b.x - 0.5f, b.y),
                                                navLight ? IM_COL32(0, 0, 0, 34) : IM_COL32(0, 0, 0, 120), 1.0f);
        }

        ImGui::SameLine(0, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 20));
        ImGui::BeginChild("content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();

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
        // Public profile page: overlays the current tab (Back returns to it);
        // switching tabs closes it.
        {
            static int pfTabWas = tab;
            if (tab != pfTabWas) ClosePublicProfile();
            pfTabWas = tab;
            static bool pfDemoOnce = false;   // dev screenshot hook: open a profile at startup
            if (!pfDemoOnce) {
                pfDemoOnce = true;
                const char* dp = std::getenv("OKAY_UI_DEMO_PROFILE");
                if (dp && *dp && std::getenv("OKAY_UI_DEMO_USER")) OpenPublicProfile("", dp);
            }
        }
        if (g_pfOpen) {
            PfPageOut po = DrawPublicProfilePage(account, tab);
            if (po.gotoTab >= 0) {
                tab = po.gotoTab;
                if (!po.marketSearch.empty()) std::snprintf(marketFilter, sizeof(marketFilter), "%s", po.marketSearch.c_str());
            }
        } else if (tab == 0) {                            // ---- Create ----
            sectionHeader("Create a game", nullptr);

            // Hero card: a flat, slightly-raised panel (no gradient wash) with the
            // one primary action — reads like a real app, not a designed banner.
            ImGui::PushStyleColor(ImGuiCol_ChildBg, g_themeIndex == 0 ? okaytheme::kRaised : ImVec4(0.180f, 0.180f, 0.190f, 1.0f));
            ImGui::BeginChild("hero", ImVec2(0, editor.empty() ? 122.0f : 110.0f), true, ImGuiWindowFlags_NoScrollbar);
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
                ImGui::SetCursorPosX(16);
                const UpState ust = GetState();
                const bool ubusy = ust == Up_Checking || ust == Up_Downloading;
                ImGui::BeginDisabled(ubusy);
                if (PrimaryButton(ubusy ? "Downloading\xE2\x80\xA6" : "Get the editor", ImVec2(150, 28))) { g_upRepairMissing = true; StartUpdateCheck(true); }
                ImGui::EndDisabled();
                ImGui::SameLine(0, 10);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
                ImGui::TextDisabled("Downloads OkayEngine next to the launcher (or place OkayEngine.exe there).");
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
            if (!s_webScanned || g_webNeedRescan) { s_webGames = FindWebGames(); s_webScanned = true; g_webNeedRescan = false; g_webGameCount = (int)s_webGames.size(); }
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
            sectionHeader("Community", nullptr);
            const ImU32 cText = ImGui::GetColorU32(ImGuiCol_Text);
            const ImU32 cDim  = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const ImVec4 accLt(kAccent.x * 0.55f + 0.45f, kAccent.y * 0.55f + 0.45f, kAccent.z * 0.55f + 0.45f, 1.0f);
            auto lowerStr = [](std::string s) {
                for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
                return s;
            };
            // Dashed outline along a rounded rect (ImGui has no dashed primitive):
            // walk the rect's path and emit alternating dash / gap pieces.
            auto dashedRoundRect = [](ImDrawList* dl, ImVec2 a, ImVec2 b, float r, ImU32 col, float th, float dash, float gap) {
                dl->PathRect(a, b, r);
                ImVector<ImVec2> pts = dl->_Path;
                dl->PathClear();
                if (pts.Size < 2) return;
                pts.push_back(pts[0]);
                bool on = true; float left = dash;
                for (int i = 0; i + 1 < pts.Size; ++i) {
                    ImVec2 p = pts[i], q = pts[i + 1];
                    float dx = q.x - p.x, dy = q.y - p.y, len = std::sqrt(dx * dx + dy * dy), t = 0.0f;
                    while (len > 0.0f && t < len) {
                        float step = std::min(left, len - t);
                        if (on) dl->AddLine(ImVec2(p.x + dx * t / len, p.y + dy * t / len),
                                            ImVec2(p.x + dx * (t + step) / len, p.y + dy * (t + step) / len), col, th);
                        t += step; left -= step;
                        if (left <= 0.001f) { on = !on; left = on ? dash : gap; }
                    }
                }
            };
            // Search pill (Friends-style): rounded field with a drawn magnifier.
            auto searchPill = [&](const char* id, const char* hint, char* buf, size_t sz, float w) {
                ImVec2 sp = ImGui::GetCursorScreenPos();
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(30, 7));
                ImGui::SetNextItemWidth(w);
                ImGui::InputTextWithHint(id, hint, buf, sz);
                ImGui::PopStyleVar(2);
                float fh = ImGui::GetItemRectSize().y;
                ImVec2 mc(sp.x + 15.0f, sp.y + fh * 0.5f - 1.0f);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddCircle(mc, 4.6f, cDim, 16, 1.5f);
                dl->AddLine(ImVec2(mc.x + 3.4f, mc.y + 3.4f), ImVec2(mc.x + 6.8f, mc.y + 6.8f), cDim, 1.8f);
            };
            // Section label row: "YOUR LIBRARY — 3" (small caps) + hairline.
            auto commLabel = [&](const char* title, int n) {
                char t[64];
                if (n >= 0) std::snprintf(t, sizeof(t), "%s  \xE2\x80\x94  %d", title, n); else std::snprintf(t, sizeof(t), "%s", title);
                ImGui::SetWindowFontScale(0.80f);
                ImGui::TextDisabled("%s", t);
                ImGui::SetWindowFontScale(1.0f);
            };
            auto folderGlyph = [](ImDrawList* d, ImVec2 c, ImU32 col) {
                d->PathLineTo(ImVec2(c.x - 7.5f, c.y - 5.5f)); d->PathLineTo(ImVec2(c.x - 2.5f, c.y - 5.5f));
                d->PathLineTo(ImVec2(c.x - 0.5f, c.y - 3.5f)); d->PathLineTo(ImVec2(c.x + 7.5f, c.y - 3.5f));
                d->PathLineTo(ImVec2(c.x + 7.5f, c.y + 5.5f)); d->PathLineTo(ImVec2(c.x - 7.5f, c.y + 5.5f));
                d->PathStroke(col, ImDrawFlags_Closed, 1.6f);
            };
            // Size + kind of each library item, computed once (folder walks are slow).
            static std::map<std::string, std::string> s_commMeta;
            auto itemMeta = [&](const fs::path& root, const fs::path& scene) -> const std::string& {
                auto it = s_commMeta.find(scene.string());
                if (it != s_commMeta.end()) return it->second;
                std::error_code ec;
                std::uintmax_t bytes = 0; int n = 0;
                if (fs::is_directory(root, ec)) {
                    for (auto f = fs::recursive_directory_iterator(root, ec); !ec && f != fs::recursive_directory_iterator() && n < 20000; f.increment(ec), ++n)
                        if (f->is_regular_file(ec)) bytes += f->file_size(ec);
                } else bytes = fs::file_size(root, ec);
                char sz[32];
                if (bytes >= 1024ull * 1024ull * 1024ull) std::snprintf(sz, sizeof(sz), "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
                else if (bytes >= 1024ull * 1024ull)      std::snprintf(sz, sizeof(sz), "%.1f MB", bytes / (1024.0 * 1024.0));
                else                                      std::snprintf(sz, sizeof(sz), "%.0f KB", std::max(1.0, bytes / 1024.0));
                std::string meta = std::string(IsBuiltGame(scene) ? "Game" : "Scene") + "  \xC2\xB7  " + sz;
                return s_commMeta.emplace(scene.string(), meta).first->second;
            };
            auto refreshLibrary = [&]() { scenes = FindScenes(); s_commMeta.clear(); };

            // Collect library items (scenes living under community/).
            struct CommItem { std::size_t idx; fs::path root; std::string title; };
            std::vector<CommItem> commAll, commShown;
            for (std::size_t i = 0; i < scenes.size(); ++i) {
                fs::path croot = CommunityItemRoot(scenes[i]);
                if (croot.empty()) continue;
                CommItem ci{i, croot, croot.filename().string()};
                commAll.push_back(ci);
                std::string cneedle = lowerStr(commFilter);
                if (cneedle.empty() || lowerStr(scenes[i].filename().string() + " " + ci.title).find(cneedle) != std::string::npos)
                    commShown.push_back(ci);
            }

            const float bodyH = ImGui::GetContentRegionAvail().y - 46.0f;
            ImGui::BeginChild("##commbody", ImVec2(0, bodyH), false);
            const float W = ImGui::GetContentRegionAvail().x;

            // ---- Hero: what Community is + primary actions -------------------------
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kRaised);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 20));
                ImGui::BeginChild("##commhero", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                                  ImGuiWindowFlags_NoScrollbar);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const float hw = ImGui::GetContentRegionAvail().x;
                const bool art = hw >= 560.0f;
                const float textW = art ? hw - 190.0f : hw;
                ImVec2 hp = ImGui::GetCursorScreenPos();
                // Decorative stack of tiles (play / grid / bag) on the right.
                if (art) {
                    const float S = 58.0f;
                    struct { float dx, dy; int icon; float a; } tiles[] = {{-150, 18, 1, 0.16f}, {-104, 2, 2, 0.26f}, {-58, 22, 7, 0.18f}};
                    for (auto& t : tiles) {
                        ImVec2 a(hp.x + hw + t.dx, hp.y + t.dy);
                        dl->AddRectFilled(a, ImVec2(a.x + S, a.y + S), ImGui::GetColorU32(okaytheme::kRaised), 14.0f);
                        dl->AddRectFilled(a, ImVec2(a.x + S, a.y + S), ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, t.a)), 14.0f);
                        dl->AddRect(a, ImVec2(a.x + S, a.y + S), IM_COL32(255, 255, 255, 26), 14.0f, 0, 1.0f);
                        DrawNavIcon(dl, t.icon, ImVec2(a.x + S * 0.5f, a.y + S * 0.5f), ImGui::GetColorU32(accLt));
                    }
                }
                ImGui::SetWindowFontScale(1.22f);
                ImGui::TextUnformatted("Play and share community creations");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::Dummy(ImVec2(0, 2));
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
                ImGui::TextDisabled("Games, levels, scripts and models made by other OkaySpace creators. Everything you add "
                                    "lives in your community folder and also shows up in Play.");
                ImGui::PopTextWrapPos();
                ImGui::Dummy(ImVec2(0, 8));
                // Actions: wrap onto a new line when the card is narrow.
                const float bh = 36.0f;
                float x = 0.0f;
                auto place = [&](float bw) {
                    if (x > 0 && x + 8 + bw <= hw) { ImGui::SameLine(0, 8); x += 8 + bw; }
                    else x = bw;
                };
                place(206);
                if (PrimaryButton("Publish to Marketplace", ImVec2(206, bh))) { tab = 7; g_openPublishForm = true; }
                place(206);
                if (ImGui::Button("Open community folder", ImVec2(206, bh))) OpenExternal(CommunityDir().string());
                place(112);
                {
                    ImVec2 rp = ImGui::GetCursorScreenPos();
                    if (ImGui::Button("      Refresh", ImVec2(112, bh))) { refreshLibrary(); Toast("Library refreshed", 1); }
                    DrawNavIcon(ImGui::GetWindowDrawList(), kIcoRefresh, ImVec2(rp.x + 22, rp.y + bh * 0.5f), cText);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rescan the community folder");
                }
                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
            }
            ImGui::Dummy(ImVec2(0, 10));

            // ---- Drop zone: dashed card, click opens the community folder ----------
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetCursorScreenPos();
                const float h = 88.0f;
                const bool clicked = ImGui::InvisibleButton("##commdrop", ImVec2(W, h));
                const bool hov = ImGui::IsItemHovered();
                dl->AddRectFilled(p, ImVec2(p.x + W, p.y + h), ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, hov ? 0.10f : 0.05f)), 12.0f);
                dashedRoundRect(dl, ImVec2(p.x + 0.5f, p.y + 0.5f), ImVec2(p.x + W - 0.5f, p.y + h - 0.5f), 12.0f,
                                ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, hov ? 0.85f : 0.50f)), 1.5f, 7.0f, 5.0f);
                ImVec2 ic(p.x + 46.0f, p.y + h * 0.5f);
                dl->AddCircleFilled(ic, 22.0f, ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.18f)), 32);
                DrawNavIcon(dl, 10, ic, ImGui::GetColorU32(accLt));
                const float tx = p.x + 84.0f, tw = W - 84.0f - 18.0f;
                const float lh = ImGui::GetTextLineHeight();
                const char* d1 = "Drag a game folder, scene, script or model onto this window to add it to your library. "
                                 "A dropped .okayscene plays right away.";
                ImVec2 ds = ImGui::CalcTextSize(d1, nullptr, false, tw);
                const float ty = p.y + (h - (lh + 4.0f + ds.y)) * 0.5f;
                dl->AddText(ImVec2(tx, ty), cText, "Drop files here to add them to your library");
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx, ty + lh + 4.0f), cDim, d1, nullptr, tw);
                if (hov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("Open the community folder"); }
                if (clicked) OpenExternal(CommunityDir().string());
            }
            ImGui::Dummy(ImVec2(0, 14));

            // ---- Your library: header (label + search) and a grid of cards ---------
            {
                const bool pillBelow = W < 560.0f;
                ImVec2 lp = ImGui::GetCursorScreenPos();
                const float pillW = pillBelow ? W : std::min(300.0f, W * 0.45f);
                ImGui::SetCursorScreenPos(ImVec2(lp.x, lp.y + (pillBelow ? 0.0f : 9.0f)));
                commLabel("YOUR LIBRARY", (int)commAll.size());
                if (!pillBelow) ImGui::SetCursorScreenPos(ImVec2(lp.x + W - pillW, lp.y));
                else ImGui::Dummy(ImVec2(0, 2));
                if (focusSearch) { ImGui::SetKeyboardFocusHere(); focusSearch = false; }
                searchPill("##commFilter", "Search your library  (Ctrl+F)", commFilter, sizeof(commFilter), pillW);
                ImGui::Dummy(ImVec2(0, 6));
            }
            if (commShown.empty()) {
                // Centered empty state.
                ImGui::Dummy(ImVec2(0, 14));
                ImVec2 c(ImGui::GetCursorScreenPos().x + W * 0.5f, ImGui::GetCursorScreenPos().y + 30);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddCircleFilled(c, 30.0f, IM_COL32(255, 255, 255, 12), 40);
                DrawNavIcon(dl, 2, c, cDim);
                ImGui::Dummy(ImVec2(0, 70));
                auto centered = [&](const char* s, bool dim) {
                    float tw = ImGui::CalcTextSize(s).x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (W - tw) * 0.5f));
                    if (dim) ImGui::TextDisabled("%s", s); else ImGui::TextUnformatted(s);
                };
                if (commFilter[0]) {
                    centered("No matches", false);
                    std::string m = std::string("Nothing in your library matches \"") + commFilter + "\".";
                    centered(m.c_str(), true);
                } else {
                    centered("Your library is empty", false);
                    centered("Drop a shared game onto this window, or put it in the community folder and Refresh.", true);
                }
                ImGui::Dummy(ImVec2(0, 18));
            } else {
                const float gap = 12.0f, cardH = 128.0f;
                int cols = std::max(1, (int)((W + gap) / (270.0f + gap)));
                const float cardW = std::floor((W - (cols - 1) * gap) / cols);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 g0 = ImGui::GetCursorScreenPos();
                int n = 0;
                for (const CommItem& it : commShown) {
                    const fs::path& scene = scenes[it.idx];
                    const std::string cpath = scene.string();
                    ImGui::PushID((int)(it.idx + 5000));
                    const ImVec2 cp(g0.x + (n % cols) * (cardW + gap), g0.y + (n / cols) * (cardH + gap));
                    ++n;
                    ImGui::SetCursorScreenPos(cp);
                    ImGui::SetNextItemAllowOverlap();
                    ImGui::InvisibleButton("##ccard", ImVec2(cardW, cardH));
                    const bool bgHov = ImGui::IsItemHovered();
                    const bool hov = ImGui::IsMouseHoveringRect(cp, ImVec2(cp.x + cardW, cp.y + cardH)) && ImGui::IsWindowHovered();
                    // Double-click plays, right-click opens the item menu.
                    if (bgHov && !player.empty() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        Launch(player, cpath); RecordPlayed(cpath); SavePrefs();
                    }
                    if (ImGui::BeginPopupContextItem("##commctx")) {
                        if (!player.empty() && ImGui::MenuItem("Play")) { Launch(player, cpath); RecordPlayed(cpath); SavePrefs(); }
                        if (ImGui::MenuItem("Show in Explorer")) OpenExternal(it.root.string());
                        if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(cpath.c_str());
                        ImGui::Separator();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1));
                        if (ImGui::MenuItem("Remove from library...")) { commRemovePath = it.root.string(); commRemoveName = it.title; }
                        ImGui::PopStyleColor();
                        ImGui::EndPopup();
                    }
                    dl->AddRectFilled(cp, ImVec2(cp.x + cardW, cp.y + cardH), ImGui::GetColorU32(okaytheme::kRaised), 10.0f);
                    if (hov) {
                        dl->AddRectFilled(cp, ImVec2(cp.x + cardW, cp.y + cardH), IM_COL32(255, 255, 255, 8), 10.0f);
                        dl->AddRect(cp, ImVec2(cp.x + cardW, cp.y + cardH), IM_COL32(255, 255, 255, 30), 10.0f, 0, 1.0f);
                    }
                    // Tile (cover art if the game ships one) + title + meta.
                    const float A = 52.0f;
                    ImGui::SetCursorScreenPos(ImVec2(cp.x + 14, cp.y + 14));
                    DrawGameIcon(cpath, it.title, A);
                    const float tx = cp.x + 14 + A + 12, tr = cp.x + cardW - 14;
                    dl->AddText(ImVec2(tx, cp.y + 16), cText, FitText(it.title, tr - tx).c_str());
                    dl->AddText(ImVec2(tx, cp.y + 37), ImGui::GetColorU32(accLt), FitText(itemMeta(it.root, scene), tr - tx).c_str());
                    std::string ago = ModifiedAgo(scene);
                    std::string sub = scene.filename().string() + (ago.empty() ? "" : "  \xC2\xB7  " + ago);
                    dl->AddText(ImVec2(tx, cp.y + 56), cDim, FitText(sub, tr - tx).c_str());
                    // Actions: Play (primary) + reveal folder + more.
                    const float d = 32.0f, by = cp.y + cardH - 14 - d;
                    ImGui::SetCursorScreenPos(ImVec2(cp.x + 14, by));
                    ImGui::BeginDisabled(player.empty());
                    if (PrimaryButton("Play", ImVec2(cardW - 28 - 2 * (d + 8), d))) { Launch(player, cpath); RecordPlayed(cpath); SavePrefs(); }
                    ImGui::EndDisabled();
                    if (player.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("Player runtime not found next to the launcher");
                    const ImU32 rbg = IM_COL32(255, 255, 255, 16), rbgh = IM_COL32(255, 255, 255, 34);
                    {
                        ImVec2 fp(cp.x + cardW - 14 - 2 * d - 8, by);
                        ImGui::SetCursorScreenPos(fp);
                        if (ImGui::InvisibleButton("##reveal", ImVec2(d, d))) OpenExternal(it.root.string());
                        const bool fh = ImGui::IsItemHovered();
                        ImVec2 fc(fp.x + d * 0.5f, fp.y + d * 0.5f);
                        dl->AddCircleFilled(fc, d * 0.5f, fh ? rbgh : rbg, 28);
                        folderGlyph(dl, fc, cText);
                        if (fh) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("Show in folder"); }
                    }
                    ImGui::SetCursorScreenPos(ImVec2(cp.x + cardW - 14 - d, by));
                    if (RoundIconButton("##more", kRibMore, d, rbg, rbgh, cText, "More")) ImGui::OpenPopup("##commctx");
                    ImGui::PopID();
                }
                const int rows = (n + cols - 1) / cols;
                ImGui::SetCursorScreenPos(ImVec2(g0.x, g0.y + rows * (cardH + gap) - gap));
                ImGui::Dummy(ImVec2(W, 0));
            }
            ImGui::Dummy(ImVec2(0, 12));

            // ---- Share your creation: numbered how-to ------------------------------
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kRaised);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));
                ImGui::BeginChild("##commshare", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                                  ImGuiWindowFlags_NoScrollbar);
                commLabel("SHARE YOUR CREATION", -1);
                ImGui::Dummy(ImVec2(0, 4));
                static const struct { const char* title; const char* desc; } kSteps[] = {
                    {"Build it",  "In the editor, File > Build Game makes a self-contained game folder."},
                    {"Share it",  "Zip the folder and send it. Friends drop it onto their launcher to play."},
                    {"Publish it","Or put it on the Marketplace so anyone can find and download it."},
                };
                const float sw = ImGui::GetContentRegionAvail().x;
                const bool rowLayout = sw >= 620.0f;
                const float colW = rowLayout ? (sw - 2 * 18.0f) / 3.0f : sw;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                for (int k = 0; k < 3; ++k) {
                    if (rowLayout && k) ImGui::SameLine(0, 18);
                    ImGui::BeginGroup();
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImVec2 nc(p.x + 14, p.y + 14);
                    dl->AddCircleFilled(nc, 14.0f, ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.22f)), 28);
                    char num[4]; std::snprintf(num, sizeof(num), "%d", k + 1);
                    ImVec2 ns = ImGui::CalcTextSize(num);
                    dl->AddText(ImVec2(nc.x - ns.x * 0.5f, nc.y - ns.y * 0.5f), ImGui::GetColorU32(accLt), num);
                    const float tx = 40.0f, tw = colW - tx;
                    ImGui::SetCursorScreenPos(ImVec2(p.x + tx, p.y + 2));
                    ImGui::BeginGroup();
                    ImGui::TextUnformatted(kSteps[k].title);
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tw);
                    ImGui::TextDisabled("%s", kSteps[k].desc);
                    ImGui::PopTextWrapPos();
                    ImGui::EndGroup();
                    ImGui::EndGroup();
                    if (!rowLayout && k < 2) ImGui::Dummy(ImVec2(0, 6));
                }
                ImGui::Dummy(ImVec2(0, 8));
                if (ImGui::Button("Reveal my games folder", ImVec2(196, 32))) OpenExternal(g_exeDir);
                if (sw >= 196 + 8 + 200) ImGui::SameLine(0, 8);
                if (ImGui::Button("Publish to Marketplace\xE2\x80\xA6", ImVec2(200, 32))) { tab = 7; g_openPublishForm = true; }
                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
            }
            ImGui::Dummy(ImVec2(0, 16));

            // ---- Starter templates (grid like the Create tab) ----------------------
            {
                const bool pillBelow = W < 560.0f;
                ImVec2 lp = ImGui::GetCursorScreenPos();
                const float pillW = pillBelow ? W : std::min(300.0f, W * 0.45f);
                ImGui::SetCursorScreenPos(ImVec2(lp.x, lp.y + (pillBelow ? 0.0f : 9.0f)));
                commLabel("STARTER TEMPLATES", -1);
                if (!pillBelow) ImGui::SetCursorScreenPos(ImVec2(lp.x + W - pillW, lp.y));
                else ImGui::Dummy(ImVec2(0, 2));
                searchPill("##marketFilter", "Search templates", marketFilter, sizeof(marketFilter), pillW);
                ImGui::Dummy(ImVec2(0, 6));
                std::string mneedle = lowerStr(marketFilter);
                const float gap = 10.0f, cardH = 72.0f;
                int cols = std::clamp((int)((W + gap) / (230.0f + gap)), 1, 4);
                const float cardW = std::floor((W - (cols - 1) * gap) / cols);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImVec2 g0 = ImGui::GetCursorScreenPos();
                int shown = 0;
                for (const auto& t : templates) {
                    if (!mneedle.empty() && lowerStr(std::string(t.name) + " " + t.desc).find(mneedle) == std::string::npos) continue;
                    const ImVec2 cp(g0.x + (shown % cols) * (cardW + gap), g0.y + (shown / cols) * (cardH + gap));
                    ++shown;
                    ImGui::PushID(t.name);
                    ImGui::SetCursorScreenPos(cp);
                    const bool clicked = ImGui::InvisibleButton("##tpl", ImVec2(cardW, cardH));
                    const bool hov = ImGui::IsItemHovered() && !editor.empty();
                    dl->AddRectFilled(cp, ImVec2(cp.x + cardW, cp.y + cardH), ImGui::GetColorU32(okaytheme::kRaised), 10.0f);
                    if (hov) {
                        dl->AddRectFilled(cp, ImVec2(cp.x + cardW, cp.y + cardH), IM_COL32(255, 255, 255, 10), 10.0f);
                        dl->AddRect(cp, ImVec2(cp.x + cardW, cp.y + cardH), ImGui::GetColorU32(kAccent), 10.0f, 0, 1.5f);
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        ImGui::SetTooltip("Open the editor with the %s template", t.name);
                    } else if (editor.empty() && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Editor not found next to the launcher");
                    }
                    ImGui::SetCursorScreenPos(ImVec2(cp.x + 14, cp.y + (cardH - 40) * 0.5f));
                    IconTile(t.name, 40.0f);
                    const float tx = cp.x + 14 + 40 + 12, tr = cp.x + cardW - 12;
                    const float lh = ImGui::GetTextLineHeight();
                    const ImU32 a = editor.empty() ? IM_COL32(255, 255, 255, 110) : cText;
                    dl->AddText(ImVec2(tx, cp.y + cardH * 0.5f - lh - 1), a, FitText(t.name, tr - tx).c_str());
                    dl->AddText(ImVec2(tx, cp.y + cardH * 0.5f + 2), cDim, FitText(t.desc, tr - tx).c_str());
                    if (clicked && !editor.empty()) {
                        LaunchEditor(editor, t.tmpl);
                        Toast(std::string("Opening the editor with ") + t.name, 1);
                    }
                    ImGui::PopID();
                }
                if (shown > 0) {
                    const int rows = (shown + cols - 1) / cols;
                    ImGui::SetCursorScreenPos(ImVec2(g0.x, g0.y + rows * (cardH + gap) - gap));
                    ImGui::Dummy(ImVec2(W, 0));
                }
                if (shown == 0) ImGui::TextDisabled("No templates match \"%s\".", marketFilter);
                ImGui::Dummy(ImVec2(0, 6));
                ImGui::TextDisabled("Templates open the editor's New Project on the chosen starting point.");
                ImGui::Dummy(ImVec2(0, 6));
            }
            ImGui::EndChild();   // ##commbody

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
                    refreshLibrary();
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
                    int gameN = LibraryGameCount(scenes);   // built games only, not projects
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
                // Open my full public profile page, exactly as other players see it.
                if (account.IsOnline() && PrimaryButton("View my profile", ImVec2(200, 36)))
                    OpenPublicProfile("", s.username, s.username, s_myAvatar, acctAccent);
                if (account.IsOnline() && ImGui::IsItemHovered())
                    ImGui::SetTooltip("See your public profile and pick the achievements and games it shows off");
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
            // Pill switch (Discord / iOS-style) used for every on/off setting. The knob
            // eases between states; returns true on the frame the value flips.
            auto ToggleSwitch = [](const char* id, bool* v) -> bool {
                const float h = 22.0f, w = 40.0f;
                ImVec2 p = ImGui::GetCursorScreenPos();
                bool changed = false;
                if (ImGui::InvisibleButton(id, ImVec2(w, h))) { *v = !*v; changed = true; }
                const bool hov = ImGui::IsItemHovered();
                float* t = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetItemID(), *v ? 1.0f : 0.0f);
                const float target = *v ? 1.0f : 0.0f;
                *t += (target - *t) * std::min(1.0f, ImGui::GetIO().DeltaTime * 16.0f);
                if (std::fabs(target - *t) < 0.01f) *t = target;
                ImVec4 off = ImGui::GetStyleColorVec4(hov ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
                off.x += 0.06f; off.y += 0.06f; off.z += 0.06f;
                ImVec4 on(kAccent.x, kAccent.y, kAccent.z, 1.0f);
                if (hov) { on.x += (1 - on.x) * 0.12f; on.y += (1 - on.y) * 0.12f; on.z += (1 - on.z) * 0.12f; }
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(okaytheme::Mix(off, on, *t)), h * 0.5f);
                const float r = h * 0.5f - 3.0f;
                ImVec2 kc(p.x + h * 0.5f + (w - h) * (*t), p.y + h * 0.5f);
                dl->AddCircleFilled(ImVec2(kc.x, kc.y + 1.0f), r, IM_COL32(0, 0, 0, 50), 20);
                dl->AddCircleFilled(kc, r, IM_COL32(250, 251, 253, 255), 20);
                if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                return changed;
            };

            const ImU32 cText = ImGui::GetColorU32(ImGuiCol_Text);
            const ImU32 cDim  = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const ImVec4 accLt(kAccent.x * 0.55f + 0.45f, kAccent.y * 0.55f + 0.45f, kAccent.z * 0.55f + 0.45f, 1.0f);
            const ImVec4 colBg = okaytheme::Mix(okaytheme::kSunken, okaytheme::kBase, 0.35f);

            // One setting: label + one-line dim description on the left, the control
            // right-aligned. When the card is too narrow the control drops below.
            // `control(w)` draws the control at the cursor, w = width it may use.
            auto settingRow = [&](const char* label, const char* desc, float ctrlW, float ctrlH, auto&& control) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetCursorScreenPos();
                const float w = ImGui::GetContentRegionAvail().x;
                const bool stack = ctrlW <= 0.0f || (w - ctrlW - 24.0f) < 200.0f;
                const float textW = stack ? w : w - ctrlW - 24.0f;
                const float lh = ImGui::GetTextLineHeight();
                ImVec2 ds = (desc && *desc) ? ImGui::CalcTextSize(desc, nullptr, false, textW) : ImVec2(0, 0);
                const float textH = lh + (ds.y > 0 ? 3.0f + ds.y : 0.0f);
                const float rowH = stack ? textH + 10.0f + ctrlH : std::max(textH, ctrlH);
                const float ty = stack ? p.y : p.y + (rowH - textH) * 0.5f;
                dl->AddText(ImVec2(p.x, ty), cText, label);
                if (ds.y > 0)
                    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x, ty + lh + 3.0f), cDim, desc, nullptr, textW);
                ImGui::SetCursorScreenPos(stack ? ImVec2(p.x, p.y + textH + 10.0f)
                                                : ImVec2(p.x + w - ctrlW, p.y + (rowH - ctrlH) * 0.5f));
                ImGui::PushID(label);
                control(stack ? w : ctrlW);
                ImGui::PopID();
                float endH = rowH;   // stacked controls may be taller than announced
                if (stack) endH = std::max(rowH, ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y - p.y);
                ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + endH));
                ImGui::Dummy(ImVec2(0, 0));
            };
            auto rowDivider = [&]() {
                ImGui::Dummy(ImVec2(0, 7));
                ImVec2 lp = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddLine(lp, ImVec2(lp.x + ImGui::GetContentRegionAvail().x, lp.y),
                                                    IM_COL32(255, 255, 255, 14), 1.0f);
                ImGui::Dummy(ImVec2(0, 7));
            };
            // Settings card: kRaised panel with a small-caps title.
            auto cardBegin = [&](const char* id, const char* title) {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kRaised);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 14));
                ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                                  ImGuiWindowFlags_NoScrollbar);
                if (title) {
                    ImGui::SetWindowFontScale(0.80f);
                    ImGui::TextDisabled("%s", title);
                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::Dummy(ImVec2(0, 4));
                }
            };
            auto cardEnd = [&]() {
                ImGui::Dummy(ImVec2(0, 2));
                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 10));
            };
            // Colour swatch that opens a picker; returns true while edited.
            auto colorSwatch = [&](const char* id, float* col) -> bool {
                bool changed = false;
                ImGui::PushID(id);
                ImVec2 p = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##sw", ImVec2(56, 26))) ImGui::OpenPopup("##swpick");
                const bool hov = ImGui::IsItemHovered();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p, ImVec2(p.x + 56, p.y + 26), ImGui::GetColorU32(ImVec4(col[0], col[1], col[2], 1)), 7.0f);
                dl->AddRect(p, ImVec2(p.x + 56, p.y + 26), hov ? IM_COL32(255, 255, 255, 120) : IM_COL32(255, 255, 255, 40), 7.0f, 0, 1.2f);
                if (hov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("#%02X%02X%02X", (int)(col[0] * 255 + 0.5f), (int)(col[1] * 255 + 0.5f), (int)(col[2] * 255 + 0.5f)); }
                if (ImGui::BeginPopup("##swpick")) {
                    ImGui::SetNextItemWidth(220);
                    changed = ImGui::ColorPicker3("##picker", col, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_DisplayHex);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
                return changed;
            };

            // ---- Category list (left) + the selected category's cards (right) ----
            static int setCat = 0;
            static const struct { const char* name; int icon; const char* sub; } kSetCats[] = {
                {"Appearance",           kIcoPanel,   "Theme, accent color and interface size."},
                {"Chat & Notifications", kIcoBell,    "Message sounds and what friends can see."},
                {"Loading screen",       kIcoRefresh, "Customize the splash shown while OkaySpace starts."},
                {"Behavior",             4,           "Startup, updates and maintenance."},
                {"Game libraries",       2,           "Extra folders the launcher scans for games."},
                {"Account",              3,           "Your OkaySpace account on this device."},
                {"About",                9,           "Version, updates and links."},
            };
            const int nCats = (int)(sizeof(kSetCats) / sizeof(kSetCats[0]));
            setCat = std::clamp(setCat, 0, nCats - 1);
            const float availW = ImGui::GetContentRegionAvail().x;
            const float bodyH = ImGui::GetContentRegionAvail().y - 46.0f;
            const bool iconOnly = availW < 600.0f;
            const float listW = iconOnly ? 52.0f : (availW >= 820.0f ? 212.0f : 184.0f);

            ImGui::PushStyleColor(ImGuiCol_ChildBg, colBg);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 10));
            ImGui::BeginChild("##setnav", ImVec2(listW, bodyH), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                if (!iconOnly) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
                    ImGui::SetWindowFontScale(0.80f);
                    ImGui::TextDisabled("SETTINGS");
                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::Dummy(ImVec2(0, 2));
                }
                for (int i = 0; i < nCats; ++i) {
                    ImGui::PushID(i);
                    ImVec2 rp = ImGui::GetCursorScreenPos();
                    const float rw = ImGui::GetContentRegionAvail().x, rh = 36.0f;
                    if (ImGui::InvisibleButton("##cat", ImVec2(rw, rh))) setCat = i;
                    const bool hov = ImGui::IsItemHovered(), sel = (setCat == i);
                    if (sel || hov) dl->AddRectFilled(rp, ImVec2(rp.x + rw, rp.y + rh), sel ? IM_COL32(255, 255, 255, 24) : IM_COL32(255, 255, 255, 11), 7.0f);
                    if (sel) dl->AddRectFilled(ImVec2(rp.x, rp.y + 10), ImVec2(rp.x + 3, rp.y + rh - 10), ImGui::GetColorU32(kAccent), 1.5f);
                    ImVec2 ic(iconOnly ? rp.x + rw * 0.5f : rp.x + 20.0f, rp.y + rh * 0.5f);
                    DrawNavIcon(dl, kSetCats[i].icon, ic, sel ? ImGui::GetColorU32(kAccent) : (hov ? cText : cDim));
                    if (!iconOnly)
                        dl->AddText(ImVec2(rp.x + 38.0f, rp.y + (rh - ImGui::GetTextLineHeight()) * 0.5f),
                                    (sel || hov) ? cText : ImGui::GetColorU32(ImGuiCol_Text, 0.78f),
                                    FitText(kSetCats[i].name, rw - 46.0f).c_str());
                    if (hov) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (iconOnly) ImGui::SetTooltip("%s", kSetCats[i].name);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 16);

            static bool setOpenReset = false;
            ImGui::BeginChild("##setbody", ImVec2(0, bodyH), false);
            {
                // Category title + subtitle.
                ImGui::SetWindowFontScale(1.2f);
                ImGui::TextUnformatted(kSetCats[setCat].name);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("%s", kSetCats[setCat].sub);
                ImGui::PopTextWrapPos();
                ImGui::Dummy(ImVec2(0, 8));

                if (setCat == 0) {   // ---- Appearance ----
                    cardBegin("##sc_theme", "THEME");
                    settingRow("Color theme", "Slate is the default look. Changes apply instantly.", 0.0f, 0.0f, [&](float w) {
                        // Preview cards: a tiny mock window per theme (top bar, side panel,
                        // content area, accent strip) — pick by look, not by name.
                        static const struct { ImVec4 bg, panel, text; } kThemePrev[] = {
                            {okaytheme::kSunken, okaytheme::kControl, okaytheme::kText},
                            {ImVec4(0.035f, 0.040f, 0.060f, 1), ImVec4(0.120f, 0.140f, 0.200f, 1), ImVec4(0.90f, 0.93f, 0.98f, 1)},
                            {ImVec4(0.930f, 0.940f, 0.960f, 1), ImVec4(0.845f, 0.865f, 0.905f, 1), ImVec4(0.10f, 0.12f, 0.16f, 1)},
                        };
                        const float cw = std::clamp((w - 20.0f) / 3.0f, 84.0f, 132.0f), chh = std::floor(cw * 0.66f);
                        for (int i = 0; i < 3; ++i) {
                            if (i) ImGui::SameLine(0, 10);
                            ImGui::PushID(i);
                            const ImVec2 sz(cw, chh);
                            ImVec2 p = ImGui::GetCursorScreenPos();
                            bool clicked = ImGui::InvisibleButton("##themecard", sz);
                            bool hov = ImGui::IsItemHovered();
                            bool cur = (g_themeIndex == i);
                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), ImGui::GetColorU32(kThemePrev[i].bg), 8.0f);
                            ImU32 pc = ImGui::GetColorU32(kThemePrev[i].panel);
                            const float sideW = sz.x * 0.26f, bot = sz.y - 22.0f;
                            dl->AddRectFilled(ImVec2(p.x + 8, p.y + 8),  ImVec2(p.x + sz.x - 8, p.y + 16), pc, 3.0f);
                            dl->AddRectFilled(ImVec2(p.x + 8, p.y + 21), ImVec2(p.x + 8 + sideW, p.y + bot), pc, 3.0f);
                            dl->AddRectFilled(ImVec2(p.x + 13 + sideW, p.y + 21), ImVec2(p.x + sz.x - 8, p.y + bot), pc, 3.0f);
                            dl->AddRectFilled(ImVec2(p.x + 8, p.y + 8),  ImVec2(p.x + 24, p.y + 11), ImGui::GetColorU32(kAccent), 2.0f);
                            ImVec2 ts = ImGui::CalcTextSize(kThemeNames[i]);
                            dl->AddText(ImVec2(p.x + (sz.x - ts.x) * 0.5f, p.y + sz.y - 19), ImGui::GetColorU32(kThemePrev[i].text), kThemeNames[i]);
                            dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y),
                                        cur ? ImGui::GetColorU32(kAccent)
                                            : (hov ? ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.6f)) : IM_COL32(255, 255, 255, 26)),
                                        8.0f, 0, cur ? 2.5f : 1.5f);
                            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                            if (clicked && !cur) { g_themeIndex = i; DarkTheme(); SavePrefs(); }
                            ImGui::PopID();
                        }
                    });
                    rowDivider();
                    const int accentCount = (int)(sizeof(kAccentPresets) / sizeof(kAccentPresets[0]));
                    char accDesc[96];
                    std::snprintf(accDesc, sizeof(accDesc), "Highlights, selection and primary buttons. Current: %s.", kAccentPresets[g_accentIndex].name);
                    settingRow("Accent color", accDesc, 0.0f, 30.0f, [&](float) {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        for (int i = 0; i < accentCount; ++i) {
                            if (i) ImGui::SameLine(0, 10);
                            ImGui::PushID(i);
                            ImVec2 p = ImGui::GetCursorScreenPos();
                            if (ImGui::InvisibleButton("##accent", ImVec2(30, 30))) { ApplyAccent(i); SavePrefs(); }
                            const bool hov = ImGui::IsItemHovered(), cur = (g_accentIndex == i);
                            ImVec2 c(p.x + 15, p.y + 15);
                            if (cur) dl->AddCircle(c, 14.5f, IM_COL32(255, 255, 255, 230), 32, 2.0f);
                            else if (hov) dl->AddCircle(c, 14.5f, IM_COL32(255, 255, 255, 90), 32, 1.5f);
                            dl->AddCircleFilled(c, cur ? 11.0f : 12.0f, ImGui::GetColorU32(kAccentPresets[i].col), 32);
                            if (cur) {   // drawn check on the active swatch
                                dl->AddLine(ImVec2(c.x - 4.5f, c.y + 0.2f), ImVec2(c.x - 1.5f, c.y + 3.2f), IM_COL32(255, 255, 255, 245), 2.2f);
                                dl->AddLine(ImVec2(c.x - 1.5f, c.y + 3.2f), ImVec2(c.x + 4.5f, c.y - 3.0f), IM_COL32(255, 255, 255, 245), 2.2f);
                            }
                            if (hov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", kAccentPresets[i].name); }
                            ImGui::PopID();
                        }
                    });
                    cardEnd();

                    cardBegin("##sc_ui", "INTERFACE");
                    settingRow("UI scale", "Make text and controls larger or smaller. Applies when you release the slider.", 220.0f, ImGui::GetFrameHeight(), [&](float w) {
                        ImGui::SetNextItemWidth(std::min(w, 320.0f));
                        if (ImGui::SliderFloat("##uiscale", &g_uiScale, 0.8f, 1.6f, "%.2fx"))
                            ImGui::GetIO().FontGlobalScale = g_uiScale;   // live preview
                        // Persist + restyle when the drag ends — no separate Apply click.
                        if (ImGui::IsItemDeactivatedAfterEdit()) { DarkTheme(); SavePrefs(); }
                    });
                    cardEnd();
                } else if (setCat == 1) {   // ---- Chat & Notifications ----
                    cardBegin("##sc_chat", "CHAT");
                    settingRow("Play a sound for new chat messages", "A soft chime when a message arrives while the launcher is open.", 40.0f, 22.0f, [&](float) {
                        if (ToggleSwitch("##chatsnd", &g_chatSounds)) {
                            SavePrefs();
                            if (g_chatSounds) PlayChatChime();   // preview
                        }
                    });
                    rowDivider();
                    settingRow("Show link previews", "Cards with the page title and image under links in chat. Loading one contacts that website.", 40.0f, 22.0f, [&](float) {
                        if (ToggleSwitch("##linkprev", &g_linkPreviews)) SavePrefs();
                    });
                    rowDivider();
                    settingRow("Test the chime", "Hear what a new message sounds like.", 96.0f, 30.0f, [&](float) {
                        if (ImGui::Button("Play sound", ImVec2(96, 30))) PlayChatChime();
                    });
                    cardEnd();

                    cardBegin("##sc_privacy", "PRIVACY");
                    settingRow("Share what I'm playing", "Friends see the game you're in on their Friends list and activity feed.", 40.0f, 22.0f, [&](float) {
                        if (ToggleSwitch("##sharepres", &g_sharePresence)) SavePrefs();
                    });
                    cardEnd();

                    cardBegin("##sc_notif", "NOTIFICATIONS");
                    settingRow("Notification center", "Friend requests, update notes and finished downloads collect here.", 96.0f, 30.0f, [&](float) {
                        if (ImGui::Button("Open", ImVec2(96, 30))) g_notifPanelOpen = true;
                    });
                    cardEnd();
                } else if (setCat == 2) {   // ---- Loading screen ----
                    cardBegin("##sc_splprev", "PREVIEW");
                    {
                        // Live mock of the splash: gradient backdrop, logo (or the cube),
                        // title and progress bar in the chosen colours.
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        const float pw = std::min(ImGui::GetContentRegionAvail().x, 420.0f), ph = std::floor(pw * 0.42f);
                        ImVec2 p = ImGui::GetCursorScreenPos();
                        auto c3 = [](const float* c, float a = 1.0f) { return ImGui::GetColorU32(ImVec4(c[0], c[1], c[2], a)); };
                        const float* acc = g_splashAccentOverride ? g_splashAccent : nullptr;
                        ImVec4 a4 = acc ? ImVec4(acc[0], acc[1], acc[2], 1) : kAccent;
                        dl->AddRectFilledMultiColor(p, ImVec2(p.x + pw, p.y + ph), c3(g_splashBgTop), c3(g_splashBgTop), c3(g_splashBgBot), c3(g_splashBgBot));
                        dl->AddRect(p, ImVec2(p.x + pw, p.y + ph), IM_COL32(255, 255, 255, 22), 0.0f, 0, 1.0f);
                        ImVec2 c(p.x + pw * 0.5f, p.y + ph * 0.40f);
                        const float S = ph * 0.30f;
                        if (!g_splashLogo.empty() && g_splashLogoTex) {
                            dl->AddImage((ImTextureID)(intptr_t)g_splashLogoTex, ImVec2(c.x - S * 0.6f, c.y - S * 0.6f), ImVec2(c.x + S * 0.6f, c.y + S * 0.6f));
                        } else if (!g_splashLogo.empty()) {   // new path: loads on next launch
                            dl->AddRect(ImVec2(c.x - S * 0.6f, c.y - S * 0.6f), ImVec2(c.x + S * 0.6f, c.y + S * 0.6f), IM_COL32(255, 255, 255, 90), 6.0f, 0, 1.5f);
                            const char* lt = "PNG";
                            ImVec2 ts = ImGui::CalcTextSize(lt);
                            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 150), lt);
                        } else {   // isometric cube in the accent
                            float hw = S * 0.62f, hh = S * 0.34f, vh = S * 0.62f;
                            ImVec2 top(c.x, c.y - hh - vh * 0.5f), l(c.x - hw, c.y - vh * 0.5f), r(c.x + hw, c.y - vh * 0.5f), m(c.x, c.y + hh - vh * 0.5f);
                            ImVec2 lb(l.x, l.y + vh), rb(r.x, r.y + vh), mb(m.x, m.y + vh);
                            dl->AddQuadFilled(top, r, m, l, ImGui::GetColorU32(ImVec4(a4.x * 0.55f + 0.45f, a4.y * 0.55f + 0.45f, a4.z * 0.55f + 0.45f, 1)));
                            dl->AddQuadFilled(l, m, mb, lb, ImGui::GetColorU32(ImVec4(a4.x * 0.70f, a4.y * 0.70f, a4.z * 0.70f, 1)));
                            dl->AddQuadFilled(m, r, rb, mb, ImGui::GetColorU32(a4));
                        }
                        const char* nm = "OkaySpace";
                        ImVec2 ts = ImGui::CalcTextSize(nm);
                        dl->AddText(ImVec2(c.x - ts.x * 0.5f, p.y + ph * 0.66f), IM_COL32(240, 242, 246, 235), nm);
                        if (g_splashShowBar) {
                            float bw = pw * 0.42f, by = p.y + ph * 0.86f;
                            dl->AddRectFilled(ImVec2(c.x - bw * 0.5f, by), ImVec2(c.x + bw * 0.5f, by + 3), IM_COL32(255, 255, 255, 30), 1.5f);
                            dl->AddRectFilled(ImVec2(c.x - bw * 0.5f, by), ImVec2(c.x + bw * 0.12f, by + 3), ImGui::GetColorU32(a4), 1.5f);
                        }
                        ImGui::Dummy(ImVec2(pw, ph));
                        ImGui::Dummy(ImVec2(0, 2));
                        ImGui::TextDisabled("Changes apply the next time OkaySpace starts.");
                    }
                    cardEnd();

                    cardBegin("##sc_splash", "LOGO & COLORS");
                    settingRow("Logo image", "A PNG shown instead of the cube. Leave blank for the built-in cube.", 0.0f, ImGui::GetFrameHeight(), [&](float w) {
                        char lb[260]; std::snprintf(lb, sizeof(lb), "%s", g_splashLogo.c_str());
                        ImGui::SetNextItemWidth(std::max(120.0f, w - 82.0f));
                        if (ImGui::InputTextWithHint("##splashlogo", "Path to a .png file", lb, sizeof(lb)))
                            { g_splashLogo = lb; SavePrefs(); }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Paste the location of a PNG.");
                        ImGui::SameLine(0, 8);
                        ImGui::BeginDisabled(g_splashLogo.empty());
                        if (ImGui::Button("Clear##splashlogo", ImVec2(74, 0))) { g_splashLogo.clear(); SavePrefs(); }
                        ImGui::EndDisabled();
                    });
                    rowDivider();
                    settingRow("Custom splash accent", "Tint the cube and progress bar with your own color.",
                               g_splashAccentOverride ? 40.0f + 12.0f + 56.0f : 40.0f, 26.0f, [&](float) {
                        if (g_splashAccentOverride) {
                            if (colorSwatch("##splashaccent", g_splashAccent)) SavePrefs();
                            ImGui::SameLine(0, 12);
                            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
                        }
                        if (ToggleSwitch("##splashaccon", &g_splashAccentOverride)) SavePrefs();
                    });
                    rowDivider();
                    settingRow("Background top", "Color at the top of the backdrop gradient.", 56.0f, 26.0f, [&](float) {
                        if (colorSwatch("##splashtop", g_splashBgTop)) SavePrefs();
                    });
                    rowDivider();
                    settingRow("Background bottom", "Color at the bottom of the backdrop gradient.", 56.0f, 26.0f, [&](float) {
                        if (colorSwatch("##splashbot", g_splashBgBot)) SavePrefs();
                    });
                    rowDivider();
                    settingRow("Show progress bar", "A thin loading bar under the logo.", 40.0f, 22.0f, [&](float) {
                        if (ToggleSwitch("##splashbar", &g_splashShowBar)) SavePrefs();
                    });
                    cardEnd();
                } else if (setCat == 3) {   // ---- Behavior ----
                    cardBegin("##sc_start", "STARTUP & UPDATES");
                    settingRow("Check for updates when the launcher starts", "Looks for a new version in the background each time you open OkaySpace.", 40.0f, 22.0f, [&](float) {
                        if (ToggleSwitch("##uponlaunch", &g_updateOnLaunch)) SavePrefs();
                    });
                    cardEnd();

                    cardBegin("##sc_maint", "MAINTENANCE");
                    settingRow("Configuration folder", "Preferences are stored in launcher.cfg next to the launcher.", 120.0f, 30.0f, [&](float) {
                        if (ImGui::Button("Open folder", ImVec2(120, 30))) OpenExternal(g_exeDir);
                    });
                    rowDivider();
                    settingRow("Reset preferences", "Restores theme, accent, UI scale, sorting and update settings. Favorites and recent games are kept.", 120.0f, 30.0f, [&](float) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.20f, 0.20f, 0.55f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.26f, 0.26f, 1.0f));
                        if (ImGui::Button("Reset\xE2\x80\xA6", ImVec2(120, 30))) setOpenReset = true;
                        ImGui::PopStyleColor(2);
                    });
                    cardEnd();
                } else if (setCat == 4) {   // ---- Game libraries ----
                    cardBegin("##sc_libs", "LIBRARY FOLDERS");
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextDisabled("Games in these folders show up in Play, alongside the built-in ones next to the launcher.");
                    ImGui::PopTextWrapPos();
                    ImGui::Dummy(ImVec2(0, 6));
                    static char libBuf[512] = "";
                    int removeIdx = -1;
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    for (std::size_t li = 0; li < g_libraryDirs.size(); ++li) {
                        ImGui::PushID((int)li);
                        ImVec2 rp = ImGui::GetCursorScreenPos();
                        const float rw = ImGui::GetContentRegionAvail().x, rh = 44.0f;
                        if (ImGui::IsMouseHoveringRect(rp, ImVec2(rp.x + rw, rp.y + rh)) && ImGui::IsWindowHovered())
                            dl->AddRectFilled(rp, ImVec2(rp.x + rw, rp.y + rh), IM_COL32(255, 255, 255, 10), 8.0f);
                        // Folder glyph.
                        ImVec2 fc(rp.x + 20, rp.y + rh * 0.5f);
                        dl->PathLineTo(ImVec2(fc.x - 8, fc.y - 6)); dl->PathLineTo(ImVec2(fc.x - 3, fc.y - 6));
                        dl->PathLineTo(ImVec2(fc.x - 1, fc.y - 4)); dl->PathLineTo(ImVec2(fc.x + 8, fc.y - 4));
                        dl->PathLineTo(ImVec2(fc.x + 8, fc.y + 6)); dl->PathLineTo(ImVec2(fc.x - 8, fc.y + 6));
                        dl->PathStroke(ImGui::GetColorU32(accLt), ImDrawFlags_Closed, 1.6f);
                        std::error_code lec;
                        const bool exists = fs::is_directory(fs::path(g_libraryDirs[li]), lec);
                        const float tx = rp.x + 40;
                        dl->AddText(ImVec2(tx, rp.y + (rh - ImGui::GetTextLineHeight()) * 0.5f - (exists ? 0.0f : 8.0f)), cText,
                                    FitText(g_libraryDirs[li], rw - 40 - 48).c_str());
                        if (!exists) dl->AddText(ImVec2(tx, rp.y + rh * 0.5f + 1), IM_COL32(236, 150, 90, 255), "Folder not found");
                        ImGui::SetCursorScreenPos(ImVec2(rp.x + rw - 36, rp.y + (rh - 30) * 0.5f));
                        if (RoundIconButton("##rmlib", kRibClose, 30.0f, IM_COL32(255, 255, 255, 14), IM_COL32(200, 70, 70, 255), cText, "Remove folder"))
                            removeIdx = (int)li;
                        ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + rh + 2));
                        ImGui::Dummy(ImVec2(0, 0));
                        ImGui::PopID();
                    }
                    if (g_libraryDirs.empty()) {
                        ImGui::TextDisabled("No extra folders yet.");
                        ImGui::Dummy(ImVec2(0, 2));
                    }
                    if (removeIdx >= 0) {
                        g_libraryDirs.erase(g_libraryDirs.begin() + removeIdx);
                        SavePrefs(); scenes = FindScenes();
                        Toast("Library folder removed", 1);
                    }
                    rowDivider();
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 8));
                    ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - 128.0f));
                    bool addSubmit = ImGui::InputTextWithHint("##libadd", "Paste a folder path to scan...",
                                                              libBuf, sizeof(libBuf), ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::SameLine(0, 8);
                    ImGui::BeginDisabled(!libBuf[0]);
                    bool addClick = PrimaryButton("Add folder", ImVec2(120, ImGui::GetFrameHeight()));
                    ImGui::EndDisabled();
                    ImGui::PopStyleVar(2);
                    if ((addClick || addSubmit) && libBuf[0]) {
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
                    cardEnd();
                } else if (setCat == 5) {   // ---- Account ----
                    const bool signedIn = account.IsLoggedIn() || kUiDemoUser;
                    const std::string uname = kUiDemoUser ? std::string(kUiDemoUser)
                                            : account.IsLoggedIn() ? account.CurrentSession().username : std::string();
                    cardBegin("##sc_acct", "ACCOUNT");
                    {
                        ImVec2 p = ImGui::GetCursorScreenPos();
                        const float w = ImGui::GetContentRegionAvail().x, A = 48.0f;
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        if (signedIn) {
                            DrawAvatar(g_myAvatarSlot ? *g_myAvatarSlot : std::string(), uname, A);
                        } else {
                            ImVec2 c(p.x + A * 0.5f, p.y + A * 0.5f);
                            dl->AddCircleFilled(c, A * 0.5f, IM_COL32(255, 255, 255, 18), 32);
                            DrawNavIcon(dl, 3, ImVec2(c.x, c.y + 1), cDim);
                            ImGui::Dummy(ImVec2(A, A));
                        }
                        const bool narrow = w < 380.0f;
                        const float tx = p.x + A + 14.0f, tr = narrow ? p.x + w : p.x + w - 156.0f;
                        ImGui::SetWindowFontScale(1.1f);
                        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx, p.y + 4), cText,
                                    FitText(signedIn ? uname : std::string("Not signed in"), tr - tx).c_str());
                        ImGui::SetWindowFontScale(1.0f);
                        const char* sub = signedIn ? "Signed in on this device" : "Sign in for friends, chat, servers and cloud sync.";
                        dl->AddText(ImVec2(tx, p.y + 28), cDim, FitText(sub, tr - tx).c_str());
                        ImVec2 bp = narrow ? ImVec2(p.x, p.y + A + 10) : ImVec2(p.x + w - 146.0f, p.y + (A - 34.0f) * 0.5f);
                        ImGui::SetCursorScreenPos(bp);
                        if (signedIn ? ImGui::Button("Manage account", ImVec2(146, 34)) : PrimaryButton("Sign in", ImVec2(146, 34))) {
                            tab = 3; g_lastTab = 3; SavePrefs();
                        }
                        ImGui::SetCursorScreenPos(ImVec2(p.x, narrow ? bp.y + 34 : p.y + A));
                        ImGui::Dummy(ImVec2(0, 0));
                    }
                    rowDivider();
                    // The account server is baked into this build, so there's nothing to
                    // configure here — just a plain connection status (no URL shown).
                    const bool online = accountPtr->IsOnline();
                    settingRow("Account service", online ? "Accounts are online. Sign in and manage your profile from the Account tab."
                                                         : "Using local accounts on this device.", 90.0f, 26.0f, [&](float) {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImVec2 p = ImGui::GetCursorScreenPos();
                        const char* lbl = online ? "Online" : "Local";
                        ImVec2 ts = ImGui::CalcTextSize(lbl);
                        const float pw = ts.x + 34.0f, px = p.x + 90.0f - pw;
                        dl->AddRectFilled(ImVec2(px, p.y), ImVec2(px + pw, p.y + 26), IM_COL32(255, 255, 255, 14), 13.0f);
                        dl->AddCircleFilled(ImVec2(px + 13, p.y + 13), 4.0f, online ? IM_COL32(76, 200, 110, 255) : IM_COL32(140, 146, 156, 255), 12);
                        dl->AddText(ImVec2(px + 23, p.y + (26 - ts.y) * 0.5f), cText, lbl);
                        ImGui::Dummy(ImVec2(90, 26));
                    });
                    cardEnd();
                } else {   // ---- About ----
                    cardBegin("##sc_about", nullptr);
                    {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImVec2 p = ImGui::GetCursorScreenPos();
                        const float w = ImGui::GetContentRegionAvail().x, S = 56.0f;
                        // Brand tile (same mark as the sidebar, larger).
                        ImVec2 br(p.x + S, p.y + S);
                        dl->AddRectFilled(p, br, ImGui::GetColorU32(kAccentDim), 13.0f);
                        dl->AddRectFilled(p, ImVec2(br.x, p.y + S * 0.5f), IM_COL32(255, 255, 255, 20), 13.0f, ImDrawFlags_RoundCornersTop);
                        ImVec2 c(p.x + S * 0.47f, p.y + S * 0.53f);
                        dl->AddCircleFilled(c, S * 0.21f, IM_COL32(236, 240, 248, 255), 32);
                        ImVec2 ring[36];
                        for (int k = 0; k < 36; ++k) {
                            float a = k * (6.2831853f / 36), rx = S * 0.375f, ry = S * 0.125f, rot = -0.5f;
                            float ex = std::cos(a) * rx, ey = std::sin(a) * ry;
                            ring[k] = ImVec2(c.x + ex * std::cos(rot) - ey * std::sin(rot), c.y + ex * std::sin(rot) + ey * std::cos(rot));
                        }
                        dl->PushClipRect(p, br, true);
                        dl->AddPolyline(ring, 36, IM_COL32(255, 255, 255, 235), ImDrawFlags_Closed, 2.2f);
                        dl->PopClipRect();
                        dl->AddCircleFilled(ImVec2(p.x + S * 0.78f, p.y + S * 0.24f), 2.2f, IM_COL32(255, 255, 255, 240));
                        const bool narrow = w < 430.0f;
                        const float tx = p.x + S + 16.0f, tr = narrow ? p.x + w : p.x + w - 186.0f;
                        ImGui::SetWindowFontScale(1.2f);
                        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx, p.y + 2), cText, "OkaySpace Engine");
                        ImGui::SetWindowFontScale(1.0f);
                        dl->AddText(ImVec2(tx, p.y + 26), ImGui::GetColorU32(accLt), "Version " OKAY_ENGINE_VERSION);
                        dl->AddText(ImVec2(tx, p.y + 44), cDim, FitText("A small Unity-inspired C++ game engine.", tr - tx).c_str());
                        // Check for updates (+ live status underneath).
                        const UpState us = GetState();
                        const bool busy = (us == Up_Checking || us == Up_Downloading);
                        ImVec2 bp = narrow ? ImVec2(p.x, p.y + S + 14) : ImVec2(p.x + w - 176.0f, p.y + 2);
                        ImGui::SetCursorScreenPos(bp);
                        ImGui::BeginDisabled(busy);
                        if (PrimaryButton(busy ? (us == Up_Downloading ? "Downloading\xE2\x80\xA6###chkupd" : "Checking\xE2\x80\xA6###chkupd") : "Check for updates###chkupd",
                                          ImVec2(176, 34)))
                            StartUpdateCheck();
                        ImGui::EndDisabled();
                        const char* stTxt = us == Up_UpToDate ? "You're up to date"
                                          : us == Up_Updated  ? "Update installed"
                                          : us == Up_Failed   ? "Last check failed"
                                          : us == Up_Checking ? "Looking for a new version\xE2\x80\xA6"
                                          : us == Up_Downloading ? "Downloading the update\xE2\x80\xA6" : "";
                        if (*stTxt) {
                            ImVec2 ts = ImGui::CalcTextSize(stTxt);
                            ImU32 sc = us == Up_Failed ? IM_COL32(236, 120, 120, 255) : us == Up_UpToDate || us == Up_Updated ? IM_COL32(110, 205, 140, 255) : cDim;
                            dl->AddText(ImVec2(narrow ? bp.x + 186.0f : bp.x + (176.0f - ts.x) * 0.5f, narrow ? bp.y + (34 - ts.y) * 0.5f : bp.y + 40), sc, stTxt);
                        }
                        ImGui::SetCursorScreenPos(ImVec2(p.x, narrow ? bp.y + 34 : p.y + S + 4));
                        ImGui::Dummy(ImVec2(0, 0));
                    }
                    cardEnd();

                    cardBegin("##sc_links", "RESOURCES");
                    settingRow("Documentation", "Guides for the editor, scripting and accounts.", 96.0f, 30.0f, [&](float) {
                        if (ImGui::Button("Open", ImVec2(96, 30)))
                            OpenExternal("https://github.com/kingimann/OkaySpaceGameEngine/blob/main/README.md");
                    });
                    rowDivider();
                    settingRow("GitHub repository", "Source code, issues and release notes.", 96.0f, 30.0f, [&](float) {
                        if (ImGui::Button("Open", ImVec2(96, 30)))
                            OpenExternal("https://github.com/kingimann/OkaySpaceGameEngine");
                    });
                    rowDivider();
                    settingRow("Version info", "Copy the version for bug reports.", 96.0f, 30.0f, [&](float) {
                        if (ImGui::Button("Copy", ImVec2(96, 30))) {
                            ImGui::SetClipboardText("OkaySpace v" OKAY_ENGINE_VERSION);
                            Toast("Version copied", 1);
                        }
                    });
                    cardEnd();
                }
                ImGui::Dummy(ImVec2(0, 4));
            }
            ImGui::EndChild();   // ##setbody

            // Reset confirmation (opened from Behavior > Maintenance).
            if (setOpenReset) { ImGui::OpenPopup("Reset preferences?"); setOpenReset = false; }
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
        } else if (tab == 5) {                            // ---- Achievements ----
            sectionHeader("Achievements", nullptr);
            static const bool kAchDemo = std::getenv("OKAY_UI_DEMO_USER") != nullptr;   // dev screenshot hook
            if (kAchDemo || loginGate("achievements")) {
            // Achievements live on this device (okay::Achievements). Re-read the
            // small file a few times a second at most, not every frame.
            static std::vector<okay::Achievement> achs;
            static Uint32 achNext = 0;
            static PfLocalShowcase achShow;
            if (SDL_GetTicks() >= achNext) {
                achNext = SDL_GetTicks() + 1500;
                achShow = LoadLocalShowcase();
                if (!kAchDemo) achs = okay::Achievements::All();
                else if (achs.empty()) {
                    const long t0 = (long)std::time(nullptr), day = 86400;
                    auto a = [&](const char* id, const char* lb, long ago) { okay::Achievement x; x.id = id; x.label = lb; x.when = t0 - ago; achs.push_back(x); };
                    a("first_win", "First Victory", 120 * day); a("coin_hoarder", "Coin Hoarder", 60 * day);
                    a("speed_demon", "Speed Demon", 20 * day); a("night_owl", "Night Owl", 9 * day);
                    a("first_publish", "Published!", 3 * day); a("perfect_lap", "Perfect Lap", 2 * 3600);
                    a("untouchable", "Untouchable", 40 * 60); a("jam_survivor", "Jam Survivor", 26 * 60);
                }
                if (kAchDemo && achShow.ach.empty()) achShow.ach = {"perfect_lap", "untouchable", "first_win"};
            }
            const ImU32 cText = ImGui::GetColorU32(ImGuiCol_Text), cDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const ImVec4 accLt(kAccent.x * 0.55f + 0.45f, kAccent.y * 0.55f + 0.45f, kAccent.z * 0.55f + 0.45f, 1.0f);
            const ImU32 gold = IM_COL32(250, 200, 70, 255);
            const long nowT = (long)std::time(nullptr);
            int monthN = 0; const okay::Achievement* latest = nullptr;
            for (const auto& a : achs) {
                if (a.when > nowT - 30L * 86400) ++monthN;
                if (!latest || a.when > latest->when) latest = &a;
            }
            auto showcased = [&](const std::string& id) { return std::find(achShow.ach.begin(), achShow.ach.end(), id) != achShow.ach.end(); };
            auto dateOf = [](long when) {
                char b[32] = "";
                if (when > 0) { std::time_t t = (std::time_t)when; if (std::tm* tm = std::localtime(&t)) std::strftime(b, sizeof(b), "%b %d, %Y", tm); }
                return std::string(b);
            };

            // ---- Header card: medal stack, count, latest unlock, quick stats ----
            {
                ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kRaised);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
                ImGui::BeginChild("##achhead", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetCursorScreenPos();
                // Three overlapping medals (latest unlocks) as the hero art.
                std::vector<const okay::Achievement*> top;
                for (const auto& a : achs) top.push_back(&a);
                std::sort(top.begin(), top.end(), [](const okay::Achievement* x, const okay::Achievement* y) { return x->when > y->when; });
                const int heroN = std::min<int>(3, (int)top.size());
                for (int k = heroN - 1; k >= 0; --k)
                    DrawAchMedal(dl, ImVec2(p.x + 30 + k * 26.0f, p.y + 32), k == 0 ? 26.0f : 22.0f, top[k]->id, k == 0 ? 1.0f : 0.75f);
                if (heroN == 0) { dl->AddNgon(ImVec2(p.x + 30, p.y + 32), 24.0f, cDim, 6, 1.8f); DrawNavIcon(dl, 5, ImVec2(p.x + 30, p.y + 32), cDim); }
                const float tx = p.x + 30 + std::max(1, heroN) * 26.0f + 32.0f;
                ImGui::SetCursorScreenPos(ImVec2(tx, p.y + 2));
                ImGui::BeginGroup();
                ImGui::SetWindowFontScale(1.45f);
                ImGui::Text("%d", (int)achs.size());
                ImGui::SetWindowFontScale(1.0f);
                ImGui::SameLine(0, 8);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
                ImGui::TextUnformatted(achs.size() == 1 ? "achievement unlocked" : "achievements unlocked");
                if (latest) ImGui::TextDisabled("Latest: %s  \xC2\xB7  %s", latest->label.c_str(), RelTime(latest->when).c_str());
                else ImGui::TextDisabled("Play games to start earning achievements.");
                ImGui::EndGroup();
                // Right: mini stats + profile button.
                const float bw = 150.0f;
                const float rx = p.x + ImGui::GetContentRegionAvail().x;
                auto stat = [&](float x, const char* big, const char* small) {
                    ImGui::SetWindowFontScale(1.2f);
                    ImVec2 bs = ImGui::CalcTextSize(big);
                    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(x, p.y + 6), cText, big);
                    ImGui::SetWindowFontScale(1.0f);
                    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.8f, ImVec2(x, p.y + 10 + bs.y), cDim, small);
                };
                char m[16]; std::snprintf(m, sizeof(m), "%d", monthN);
                char sc[16]; std::snprintf(sc, sizeof(sc), "%d / 6", (int)std::min<size_t>(6, achShow.ach.size()));
                if (ImGui::GetContentRegionAvail().x > 640) {
                    stat(rx - bw - 230, m, "THIS MONTH");
                    stat(rx - bw - 120, sc, "SHOWCASED");
                }
                ImGui::SetCursorScreenPos(ImVec2(rx - bw, p.y + 12));
                if (ImGui::Button("View my profile", ImVec2(bw, 34)))
                    OpenPublicProfile("", account.IsLoggedIn() ? account.CurrentSession().username : std::string(kAchDemo ? std::getenv("OKAY_UI_DEMO_USER") : ""));
                ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 66));
                ImGui::Dummy(ImVec2(0, 0));
                ImGui::EndChild();
                ImGui::PopStyleVar(2);
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 8));
            }

            // ---- Chips + sort + search ----
            static int achView = 0, achSort = 0;   // view: 0 all, 1 this month, 2 showcased; sort: newest, oldest, A-Z
            static char achFilter[64] = "";
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 cp = ImGui::GetCursorScreenPos();
                const float ch = ImGui::GetTextLineHeight() + 12.0f;
                float x = cp.x;
                const char* labels[] = {"All", "This month", "Showcased"};
                const int counts[] = {(int)achs.size(), monthN, (int)achShow.ach.size()};
                for (int k = 0; k < 3; ++k) {
                    char cnt[8]; std::snprintf(cnt, sizeof(cnt), "%d", counts[k]);
                    ImVec2 ts = ImGui::CalcTextSize(labels[k]), cs = ImGui::CalcTextSize(cnt);
                    float cw = ts.x + cs.x + 40.0f;
                    ImGui::SetCursorScreenPos(ImVec2(x, cp.y));
                    ImGui::PushID(k);
                    if (ImGui::InvisibleButton("##achchip", ImVec2(cw, ch))) achView = k;
                    bool h = ImGui::IsItemHovered();
                    ImGui::PopID();
                    bool on = achView == k;
                    dl->AddRectFilled(ImVec2(x, cp.y), ImVec2(x + cw, cp.y + ch),
                                      on ? ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.24f)) : (h ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 9)), ch * 0.5f);
                    dl->AddText(ImVec2(x + 13, cp.y + (ch - ts.y) * 0.5f), on ? ImGui::GetColorU32(accLt) : (h ? cText : cDim), labels[k]);
                    float bx = x + 13 + ts.x + 7;
                    dl->AddRectFilled(ImVec2(bx, cp.y + (ch - 16) * 0.5f), ImVec2(bx + cs.x + 10, cp.y + (ch + 16) * 0.5f), IM_COL32(255, 255, 255, 28), 8.0f);
                    dl->AddText(ImVec2(bx + 5, cp.y + (ch - cs.y) * 0.5f), IM_COL32_WHITE, cnt);
                    if (h) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    x += cw + 8;
                }
                // Sort (right) + search pill (between).
                const float sortW = 132.0f, right = cp.x + ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorScreenPos(ImVec2(right - sortW, cp.y));
                ImGui::SetNextItemWidth(sortW);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f);
                ImGui::Combo("##achsort", &achSort, "Newest first\0Oldest first\0A to Z\0");
                ImGui::PopStyleVar();
                const float sw = std::min(280.0f, right - sortW - 10 - (x + 10));
                if (sw > 120) {
                    ImVec2 sp(right - sortW - 10 - sw, cp.y);
                    ImGui::SetCursorScreenPos(sp);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(30, 7));
                    ImGui::SetNextItemWidth(sw);
                    ImGui::InputTextWithHint("##achsearch", "Search achievements", achFilter, sizeof(achFilter));
                    ImGui::PopStyleVar(2);
                    ImVec2 mc(sp.x + 15.0f, sp.y + ImGui::GetItemRectSize().y * 0.5f - 1.0f);
                    dl->AddCircle(mc, 4.6f, cDim, 16, 1.5f);
                    dl->AddLine(ImVec2(mc.x + 3.4f, mc.y + 3.4f), ImVec2(mc.x + 6.8f, mc.y + 6.8f), cDim, 1.8f);
                }
                ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y + ch + 12));
                ImGui::Dummy(ImVec2(0, 0));
            }

            // ---- Grid of medal tiles ----
            auto alo = [](std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
            const std::string an = alo(achFilter);
            std::vector<const okay::Achievement*> list;
            for (const auto& a : achs) {
                if (achView == 1 && a.when <= nowT - 30L * 86400) continue;
                if (achView == 2 && !showcased(a.id)) continue;
                if (!an.empty() && alo(a.label + " " + a.id).find(an) == std::string::npos) continue;
                list.push_back(&a);
            }
            std::sort(list.begin(), list.end(), [&](const okay::Achievement* x, const okay::Achievement* y) {
                if (achSort == 2) return alo(x->label) < alo(y->label);
                return achSort == 1 ? x->when < y->when : x->when > y->when;
            });
            ImGui::BeginChild("##achlist", ImVec2(0, ImGui::GetContentRegionAvail().y - 46.0f), false);
            if (list.empty()) {
                float aw = ImGui::GetContentRegionAvail().x;
                ImGui::Dummy(ImVec2(0, 30));
                ImVec2 c(ImGui::GetCursorScreenPos().x + aw * 0.5f, ImGui::GetCursorScreenPos().y + 30);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddCircleFilled(c, 30.0f, IM_COL32(255, 255, 255, 12), 40);
                DrawNavIcon(dl, 5, c, cDim);
                ImGui::Dummy(ImVec2(0, 70));
                auto centered = [&](const char* s, bool dim) {
                    float tw = ImGui::CalcTextSize(s).x;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (aw - tw) * 0.5f));
                    if (dim) ImGui::TextDisabled("%s", s); else ImGui::TextUnformatted(s);
                };
                if (achs.empty()) {
                    centered("No achievements yet", false);
                    centered("Games award them with the \"Unlock Achievement\" action. Go play something!", true);
                    ImGui::Dummy(ImVec2(0, 8));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (aw - 150) * 0.5f));
                    if (PrimaryButton("Browse games", ImVec2(150, 34))) tab = 1;
                } else if (achView == 2) {
                    centered("Nothing showcased yet", false);
                    centered("Pick up to 6 achievements to show off on your profile (right-click a tile).", true);
                } else if (achView == 1) {
                    centered("No new achievements this month", false);
                    centered("Your latest unlocks will show up here.", true);
                } else {
                    centered("No achievements match your search", false);
                }
            } else {
                const float gap = 12.0f, tileH = 78.0f;
                const float aw = ImGui::GetContentRegionAvail().x;
                const int cols = std::max(1, std::min(4, (int)((aw + gap) / (260.0f + gap))));
                const float tileW = (aw - gap * (cols - 1)) / cols;
                ImVec2 o = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                for (std::size_t i = 0; i < list.size(); ++i) {
                    const okay::Achievement& a = *list[i];
                    const int r = (int)i / cols, c = (int)i % cols;
                    ImVec2 p(o.x + c * (tileW + gap), o.y + r * (tileH + gap));
                    ImGui::SetCursorScreenPos(p);
                    ImGui::PushID(a.id.c_str());
                    ImGui::InvisibleButton("##achtile", ImVec2(tileW, tileH));
                    const bool hov = ImGui::IsItemHovered();
                    const bool pinned = showcased(a.id);
                    if (ImGui::BeginPopupContextItem("##achctx")) {
                        if (pinned) { if (ImGui::MenuItem("Remove from showcase")) {
                            achShow.ach.erase(std::remove(achShow.ach.begin(), achShow.ach.end(), a.id), achShow.ach.end());
                            SaveLocalShowcase(achShow); Toast("Removed from your showcase (Edit showcase on your profile to sync)", 1); } }
                        else if (ImGui::MenuItem("Add to showcase", nullptr, false, achShow.ach.size() < 6)) {
                            achShow.ach.push_back(a.id); SaveLocalShowcase(achShow); Toast("Added to your showcase (Edit showcase on your profile to sync)", 1);
                        }
                        if (ImGui::MenuItem("Copy id")) { ImGui::SetClipboardText(a.id.c_str()); Toast("Achievement id copied", 1); }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                    const bool fresh = a.when > nowT - 2L * 86400;
                    dl->AddRectFilled(p, ImVec2(p.x + tileW, p.y + tileH), ImGui::GetColorU32(hov ? okaytheme::Mix(okaytheme::kRaised, ImVec4(1, 1, 1, 1), 0.04f) : okaytheme::kRaised), 12.0f);
                    if (pinned) dl->AddRect(p, ImVec2(p.x + tileW, p.y + tileH), ImGui::GetColorU32(ImVec4(1.0f, 0.80f, 0.30f, 0.45f)), 12.0f, 0, 1.5f);
                    DrawAchMedal(dl, ImVec2(p.x + 38, p.y + tileH * 0.5f), 24.0f, a.id, 1.0f);
                    const float tx = p.x + 74, maxW = tileW - 74 - 14 - (pinned ? 16 : 0);
                    dl->AddText(ImVec2(tx, p.y + 18), cText, FitText(a.label.empty() ? PfPrettyId(a.id) : a.label, maxW).c_str());
                    std::string d = dateOf(a.when);
                    dl->AddText(ImVec2(tx, p.y + 40), cDim, FitText(d.empty() ? std::string("Unlocked") : "Unlocked " + d, maxW).c_str());
                    if (pinned) DrawStar(dl, ImVec2(p.x + tileW - 18, p.y + 18), 6.0f, gold);
                    if (fresh) {   // "NEW" pill on recent unlocks
                        const char* nw = "NEW";
                        float fs = ImGui::GetFontSize() * 0.72f;
                        ImVec2 ns = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, nw);
                        ImVec2 np(p.x + tileW - ns.x - 22 - (pinned ? 18 : 0), p.y + 11);
                        dl->AddRectFilled(ImVec2(np.x - 6, np.y - 2), ImVec2(np.x + ns.x + 6, np.y + ns.y + 2), ImGui::GetColorU32(kAccent), 8.0f);
                        dl->AddText(ImGui::GetFont(), fs, np, IM_COL32_WHITE, nw);
                    }
                    if (hov) ImGui::SetTooltip("%s\n%s  \xC2\xB7  %s\nRight-click to %s your showcase", a.label.c_str(), a.id.c_str(), RelTime(a.when).c_str(), pinned ? "remove it from" : "add it to");
                }
                const int rows = ((int)list.size() + cols - 1) / cols;
                ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + rows * (tileH + gap)));
                ImGui::Dummy(ImVec2(0, 0));
            }
            ImGui::EndChild();
            }   // end achievements login gate
        } else if (tab == 6) {                            // ---- Friends ----
            sectionHeader("Friends", nullptr);
            // Viewing Friends clears the unread-DM badge.
            if (account.IsLoggedIn() && (g_dmUnread > 0 || g_newestDmId > g_lastDmId)) {
                g_lastDmId = g_newestDmId; g_dmUnread = 0; SavePrefs();
            }
            static const bool kFrDemo = std::getenv("OKAY_UI_DEMO_USER") != nullptr;   // dev screenshot hook
            okay::Friends fr(account);
            if (!fr.Available() && !kFrDemo) {
                loginGate("Friends");
            } else {
                static char addBuf[48] = "";
                static std::string fErr, fOk;
                static std::vector<okay::Friend> fCache;
                static std::vector<okay::FriendEvent> fFeed;
                static std::string fCode;
                // Direct messages live in the floating Chat window (DrawChatWindow);
                // the "Message" buttons call OpenChatWith(). Clicking a row opens
                // the full public profile page (OpenPublicProfile).
                static Uint32 fNextRefresh = 0;
                static int fView = 0;             // 0 Online, 1 All, 2 Pending, 3 Add friend
                Uint32 now = SDL_GetTicks();
                if (kFrDemo) {
                    static bool seeded = false;
                    if (!seeded) {
                        seeded = true;
                        long t0 = (long)std::time(nullptr);
                        auto mk = [&](long row, const char* id, const char* user, const char* disp, const char* st, bool in, bool on, const char* playing, const char* custom) {
                            okay::Friend f; f.rowId = row; f.id = id; f.username = user; f.display = disp; f.status = st;
                            f.incoming = in; f.online = on; f.playing = playing; f.customStatus = custom; fCache.push_back(f); };
                        mk(1, "u-maya", "maya", "Maya Chen", "accepted", false, true, "Playing Neon Drift", "");
                        mk(2, "demo-uid", "alexr", "Alex Rivera", "accepted", false, true, "", "shipping the jam build");
                        mk(3, "u-priya", "priya", "Priya Natarajan", "accepted", false, true, "In the editor", "");
                        mk(4, "u-jon", "jonas", "Jonas Berg", "accepted", false, false, "", "");
                        mk(5, "u-leo", "leo", "Leo Martins", "accepted", false, false, "", "");
                        mk(6, "u-kai", "kai_dev", "Kai", "pending", true, true, "", "");
                        mk(7, "u-zoe", "zoe", "Zoe Park", "pending", false, false, "", "");
                        fCache[3].lastSeen = t0 - 12 * 60; fCache[4].lastSeen = t0 - 3 * 3600 - 600;   // "Last online …"
                        fCache[0].sessionCode = "QX7-42";
                        g_favFriends.insert("u-maya");
                        auto ev = [&](const char* a, const char* k, const char* d, long ago) { okay::FriendEvent e; e.actor = a; e.kind = k; e.detail = d; e.when = t0 - ago; fFeed.push_back(e); };
                        ev("maya", "playing", "Neon Drift", 240); ev("alexr", "publish", "Coin Collector Deluxe", 3600);
                        ev("priya", "achievement", "First Build", 7200); ev("jonas", "online", "", 86400);
                        fCode = "OKAY-7K2F";
                    }
                } else {
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
                }

                // Private display name: a nickname you set, else their display
                // name, else the username.
                auto nameOf = [&](const okay::Friend* f) -> std::string {
                    auto it = g_friendNicks.find(f->id);
                    if (it != g_friendNicks.end() && !it->second.empty()) return it->second;
                    return f->display.empty() ? f->username : f->display;
                };
                int acceptedN = 0, onlineN = 0, reqN = 0, sentN = 0;
                for (const auto& f : fCache) {
                    if (f.status == "accepted") { ++acceptedN; if (f.online) ++onlineN; }
                    else if (f.incoming) ++reqN; else ++sentN;
                }
                const ImU32 cText = ImGui::GetColorU32(ImGuiCol_Text);
                const ImU32 cDim  = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                const ImVec4 accLt(kAccent.x * 0.55f + 0.45f, kAccent.y * 0.55f + 0.45f, kAccent.z * 0.55f + 0.45f, 1.0f);
                const ImU32 cGreen = IM_COL32(76, 200, 110, 255);
                const ImU32 pageBg = ImGui::GetColorU32(ImGuiCol_WindowBg);

                // ---- Toolbar: view chips (left) + my presence (right) ------------
                {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 cp = ImGui::GetCursorScreenPos();
                    const float ch = ImGui::GetTextLineHeight() + 12.0f;
                    float x = cp.x;
                    auto chip = [&](int view, const char* label, int count, bool alert) {
                        char txt[48];
                        if (count >= 0) std::snprintf(txt, sizeof(txt), "%s", label); else std::snprintf(txt, sizeof(txt), "%s", label);
                        ImVec2 ts = ImGui::CalcTextSize(txt);
                        char cnt[8] = ""; if (count > 0) std::snprintf(cnt, sizeof(cnt), "%d", count);
                        float cw = ts.x + 26.0f + (cnt[0] ? ImGui::CalcTextSize(cnt).x + 14.0f : 0.0f);
                        ImGui::SetCursorScreenPos(ImVec2(x, cp.y));
                        ImGui::PushID(view);
                        if (ImGui::InvisibleButton("##frchip", ImVec2(cw, ch))) { fView = view; fErr.clear(); fOk.clear(); }
                        bool h = ImGui::IsItemHovered();
                        ImGui::PopID();
                        const bool on = fView == view;
                        const bool add = view == 3;
                        ImU32 bg = add ? (on ? ImGui::GetColorU32(ImVec4(0.18f, 0.55f, 0.32f, 1)) : (h ? IM_COL32(46, 140, 82, 255) : IM_COL32(38, 120, 70, 255)))
                                       : on ? ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.24f))
                                            : (h ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 9));
                        dl->AddRectFilled(ImVec2(x, cp.y), ImVec2(x + cw, cp.y + ch), bg, ch * 0.5f);
                        dl->AddText(ImVec2(x + 13.0f, cp.y + (ch - ts.y) * 0.5f),
                                    add ? IM_COL32_WHITE : on ? ImGui::GetColorU32(accLt) : (h ? cText : cDim), txt);
                        if (cnt[0]) {
                            ImVec2 cs = ImGui::CalcTextSize(cnt);
                            float bx = x + 13.0f + ts.x + 7.0f, bw = cs.x + 10.0f;
                            dl->AddRectFilled(ImVec2(bx, cp.y + (ch - 16) * 0.5f), ImVec2(bx + bw, cp.y + (ch + 16) * 0.5f),
                                              alert ? IM_COL32(222, 76, 76, 255) : IM_COL32(255, 255, 255, 28), 8.0f);
                            dl->AddText(ImVec2(bx + 5.0f, cp.y + (ch - cs.y) * 0.5f), IM_COL32_WHITE, cnt);
                        }
                        if (h) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        x += cw + 8.0f;
                    };
                    chip(0, "Online", onlineN, false);
                    chip(1, "All", acceptedN, false);
                    chip(2, "Pending", reqN + sentN, reqN > 0);
                    chip(3, "Add friend", 0, false);
                    // Presence picker (right): dot + label, opens a small menu.
                    static const char* kStat[] = {"Online", "Away", "Invisible"};
                    static const ImU32 kStatCol[] = {IM_COL32(76, 200, 110, 255), IM_COL32(236, 180, 64, 255), IM_COL32(140, 146, 156, 255)};
                    int si = std::clamp(g_onlineStatus, 0, 2);
                    float pw = ImGui::CalcTextSize(kStat[si]).x + 52.0f;
                    ImVec2 pp(cp.x + ImGui::GetContentRegionAvail().x - pw, cp.y);
                    ImGui::SetCursorScreenPos(pp);
                    if (ImGui::InvisibleButton("##presence", ImVec2(pw, ch))) ImGui::OpenPopup("##presencepop");
                    bool ph = ImGui::IsItemHovered();
                    dl->AddRectFilled(pp, ImVec2(pp.x + pw, pp.y + ch), ph ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 9), ch * 0.5f);
                    dl->AddCircleFilled(ImVec2(pp.x + 15, pp.y + ch * 0.5f), 4.5f, kStatCol[si], 12);
                    dl->AddText(ImVec2(pp.x + 26, pp.y + (ch - ImGui::GetTextLineHeight()) * 0.5f), cText, kStat[si]);
                    float vx = pp.x + pw - 15, vy = pp.y + ch * 0.5f;   // chevron
                    dl->AddLine(ImVec2(vx - 3.5f, vy - 1.5f), ImVec2(vx, vy + 2.0f), cDim, 1.6f);
                    dl->AddLine(ImVec2(vx + 3.5f, vy - 1.5f), ImVec2(vx, vy + 2.0f), cDim, 1.6f);
                    if (ph) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("Your status"); }
                    if (ImGui::BeginPopup("##presencepop")) {
                        static const char* kStatDesc[] = {"Friends see you're around", "Shown as away", "Appear offline to everyone"};
                        for (int k = 0; k < 3; ++k) {
                            ImGui::PushID(k);
                            ImVec2 rp = ImGui::GetCursorScreenPos();
                            if (ImGui::Selectable("##st", g_onlineStatus == k, 0, ImVec2(230, 38))) {
                                g_onlineStatus = k; fNextRefresh = 0; SavePrefs();   // the worker broadcasts it
                            }
                            ImDrawList* pd = ImGui::GetWindowDrawList();
                            pd->AddCircleFilled(ImVec2(rp.x + 10, rp.y + 19), 5.0f, kStatCol[k], 12);
                            pd->AddText(ImVec2(rp.x + 24, rp.y + 2), cText, kStat[k]);
                            pd->AddText(ImVec2(rp.x + 24, rp.y + 19), cDim, kStatDesc[k]);
                            ImGui::PopID();
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y + ch + 14.0f));
                    ImGui::Dummy(ImVec2(0, 0));
                }

                // Nickname editor popup state.
                static std::string nickUid; static char nickBuf[48] = ""; static bool nickOpen = false;

                // ---- One friend row (chat-list style) ------------------------------
                // kind: 0 friend, 1 incoming request, 2 sent request.
                auto drawFriend = [&](const okay::Friend* f, int kind) {
                    const bool fav = g_favFriends.count(f->id) != 0;
                    const std::string dispName = nameOf(f);
                    const bool accepted = kind == 0;
                    const bool joinable = accepted && f->online && !f->sessionCode.empty();
                    ImGui::PushID((int)f->rowId);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 rp = ImGui::GetCursorScreenPos();
                    const float w = ImGui::GetContentRegionAvail().x, rowH = 62.0f, A = 40.0f, d = 34.0f;
                    ImGui::SetNextItemAllowOverlap();
                    if (ImGui::InvisibleButton("##frrow", ImVec2(w, rowH)))
                        OpenPublicProfile(f->id, f->username, dispName, f->avatar, f->accent);
                    const bool rowHov = ImGui::IsMouseHoveringRect(rp, ImVec2(rp.x + w, rp.y + rowH)) && ImGui::IsWindowHovered();
                    // Right-click menu: favorite, nickname, copy, remove (Discord-style).
                    if (ImGui::BeginPopupContextItem("##frctx")) {
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
                        if (ImGui::MenuItem("View profile")) OpenPublicProfile(f->id, f->username, dispName, f->avatar, f->accent);
                        ImGui::Separator();
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1));
                        if (ImGui::MenuItem(kind == 1 ? "Decline request" : accepted ? "Remove friend" : "Cancel request")) {
                            fr.Remove(f->rowId); fNextRefresh = 0;
                        }
                        ImGui::PopStyleColor();
                        ImGui::EndPopup();
                    }
                    if (rowHov) dl->AddRectFilled(rp, ImVec2(rp.x + w, rp.y + rowH), IM_COL32(255, 255, 255, 11), 10.0f);
                    // Avatar + presence dot + favourite star.
                    ImGui::SetCursorScreenPos(ImVec2(rp.x + 10, rp.y + (rowH - A) * 0.5f));
                    ImVec2 ap = ImGui::GetCursorScreenPos();
                    DrawAvatar(f->avatar, dispName, A);
                    if (accepted || kind == 1) {
                        ImVec2 pd(ap.x + A * 0.85f, ap.y + A * 0.85f);
                        dl->AddCircleFilled(pd, 7.0f, pageBg, 16);
                        dl->AddCircleFilled(pd, 5.0f, f->online ? cGreen : IM_COL32(120, 128, 140, 255), 16);
                    }
                    if (fav) DrawStar(dl, ImVec2(ap.x + 3, ap.y + 3), 6.0f, IM_COL32(250, 200, 70, 255));
                    // Name, @username, status line.
                    const float tx = rp.x + 10 + A + 12, ty = rp.y + 12;
                    const int nBtns = kind == 1 ? 2 : kind == 2 ? 1 : (joinable ? 3 : 2);
                    const float right = rp.x + w - 12 - nBtns * (d + 8);
                    std::string nm = FitText(dispName, right - tx - 10);
                    dl->AddText(ImVec2(tx, ty), cText, nm.c_str());
                    float nx = tx + ImGui::CalcTextSize(nm.c_str()).x + 6;
                    std::string un = "@" + f->username;
                    if (nx + ImGui::CalcTextSize(un.c_str()).x < right - 6)
                        dl->AddText(ImVec2(nx, ty), cDim, un.c_str());
                    const bool playing = f->online && !f->playing.empty();
                    std::string sub = kind == 1 ? "Incoming friend request"
                                    : kind == 2 ? "Request sent"
                                    : playing ? f->playing
                                    : !f->customStatus.empty() ? f->customStatus
                                    : f->online ? "Online"
                                    : f->lastSeen > 0 ? LastOnline(f->lastSeen) : "Offline";
                    ImU32 sc = kind == 1 ? ImGui::GetColorU32(accLt) : playing ? ImGui::GetColorU32(accLt)
                             : (accepted && f->online && f->customStatus.empty()) ? cGreen : cDim;
                    dl->AddText(ImVec2(tx, ty + 20), sc, FitText(sub, right - tx - 10).c_str());
                    // Actions (right): round icon buttons.
                    float bx = rp.x + w - 12 - d;
                    const float by = rp.y + (rowH - d) * 0.5f;
                    const ImU32 bg = IM_COL32(255, 255, 255, 16), bgh = IM_COL32(255, 255, 255, 34);
                    auto at = [&](void) { ImGui::SetCursorScreenPos(ImVec2(bx, by)); bx -= d + 8; };
                    if (kind == 1) {
                        at(); if (RoundIconButton("##decline", kRibClose, d, bg, IM_COL32(200, 70, 70, 255), cText, "Decline")) { fr.Remove(f->rowId); fNextRefresh = 0; }
                        at(); if (RoundIconButton("##accept", kRibCheck, d, IM_COL32(38, 120, 70, 255), IM_COL32(46, 150, 86, 255), IM_COL32_WHITE, "Accept")) {
                            if (fr.Accept(f->rowId)) Toast("You're now friends with " + dispName, 1);
                            fNextRefresh = 0;
                        }
                    } else if (kind == 2) {
                        at(); if (RoundIconButton("##cancel", kRibClose, d, bg, bgh, cText, "Cancel request")) { fr.Remove(f->rowId); fNextRefresh = 0; }
                    } else {
                        at(); if (RoundIconButton("##more", kRibMore, d, bg, bgh, cText, "More")) ImGui::OpenPopup("##frctx");
                        if (joinable) {
                            at();
                            if (RoundIconButton("##join", kRibPlay, d, IM_COL32(38, 120, 70, 255), IM_COL32(46, 150, 86, 255), IM_COL32_WHITE, "Join their game")) {
                                ImGui::SetClipboardText(f->sessionCode.c_str());
                                Toast("Session code " + f->sessionCode + " copied - open the game and Join with it", 2);
                            }
                        }
                        at(); if (RoundIconButton("##msg", kRibChat, d, bg, bgh, cText, "Message")) OpenChatWith(f->id, dispName, f->avatar);
                    }
                    ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + rowH + 2));
                    ImGui::Dummy(ImVec2(0, 0));   // an item must follow a cursor move (ImGui boundary check)
                    ImGui::PopID();
                };
                // Section label: "ONLINE — 3" + hairline.
                auto sectionLabel = [&](const char* title, int n) {
                    ImGui::Dummy(ImVec2(0, 6));
                    char t[64]; std::snprintf(t, sizeof(t), "%s  \xE2\x80\x94  %d", title, n);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 10);
                    ImGui::SetWindowFontScale(0.80f);
                    ImGui::TextDisabled("%s", t);
                    ImGui::SetWindowFontScale(1.0f);
                    ImVec2 lp = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(lp.x + 10, lp.y + 2), ImVec2(lp.x + ImGui::GetContentRegionAvail().x - 10, lp.y + 2),
                                                        IM_COL32(255, 255, 255, 14), 1.0f);
                    ImGui::Dummy(ImVec2(0, 6));
                };
                // Centered empty state with a big icon.
                auto emptyState = [&](int icon, const char* title, const char* sub) {
                    float aw = ImGui::GetContentRegionAvail().x;
                    ImGui::Dummy(ImVec2(0, 40));
                    ImVec2 c(ImGui::GetCursorScreenPos().x + aw * 0.5f, ImGui::GetCursorScreenPos().y + 30);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddCircleFilled(c, 30.0f, IM_COL32(255, 255, 255, 12), 40);
                    DrawNavIcon(dl, icon, c, cDim);
                    ImGui::Dummy(ImVec2(0, 70));
                    auto centered = [&](const char* s, bool dim) {
                        float tw = ImGui::CalcTextSize(s).x;
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (aw - tw) * 0.5f));
                        if (dim) ImGui::TextDisabled("%s", s); else ImGui::TextUnformatted(s);
                    };
                    centered(title, false);
                    centered(sub, true);
                };
                // Friend code card (side panel / add view).
                auto codeCard = [&]() {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kRaised);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
                    ImGui::BeginChild("##frcode", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
                    ImGui::TextDisabled("YOUR FRIEND CODE");
                    ImGui::SetWindowFontScale(1.35f);
                    ImGui::PushStyleColor(ImGuiCol_Text, accLt);
                    ImGui::TextUnformatted(fCode.empty() ? "\xE2\x80\xA6" : fCode.c_str());
                    ImGui::PopStyleColor();
                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::BeginDisabled(fCode.empty());
                    if (ImGui::Button("Copy code", ImVec2(-1, 0))) { ImGui::SetClipboardText(fCode.c_str()); Toast("Friend code copied", 1); }
                    ImGui::EndDisabled();
                    ImGui::PushTextWrapPos(0.0f);
                    ImGui::TextDisabled("Share it so friends can add you.");
                    ImGui::PopTextWrapPos();
                    ImGui::EndChild();
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                };
                // Recent activity card.
                auto activityCard = [&]() {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kRaised);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
                    ImGui::BeginChild("##fract", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
                    ImGui::TextDisabled("ACTIVITY");
                    ImGui::Dummy(ImVec2(0, 2));
                    if (fFeed.empty()) ImGui::TextDisabled("It's quiet\xE2\x80\xA6 nothing new from friends yet.");
                    int n = 0;
                    for (const auto& e : fFeed) {
                        if (n++ >= 8) break;
                        std::string av, who = e.actor;
                        for (const auto& f : fCache) if (f.username == e.actor) { av = f.avatar; who = nameOf(&f); break; }
                        ImGui::PushID(n);
                        ImVec2 ip = ImGui::GetCursorScreenPos();
                        DrawAvatar(av, who, 28.0f);
                        ImGui::SetCursorScreenPos(ImVec2(ip.x + 38, ip.y - 1));
                        ImGui::BeginGroup();
                        const char* verb = e.kind == "playing" ? "is playing " : e.kind == "achievement" ? "unlocked "
                                         : e.kind == "publish" ? "published " : e.kind == "online" ? "came online" : "";
                        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
                        ImGui::TextWrapped("%s %s%s", who.c_str(), verb, e.detail.c_str());
                        ImGui::PopTextWrapPos();
                        std::string ago = RelTime(e.when);
                        if (!ago.empty()) ImGui::TextDisabled("%s", ago.c_str());
                        ImGui::EndGroup();
                        ImGui::Dummy(ImVec2(0, 4));
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                };

                // ---- Body: list (left) + side panel (right on wide windows) ---------
                const float availW = ImGui::GetContentRegionAvail().x;
                const bool side = availW >= 760.0f;
                const float sideW = side ? 280.0f : 0.0f;
                const float bodyH = ImGui::GetContentRegionAvail().y - 46.0f;
                ImGui::BeginChild("##friendlist", ImVec2(side ? availW - sideW - 18.0f : 0.0f, bodyH), false);
                if (fView == 3) {   // ---- Add friend ----
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kRaised);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 18));
                    ImGui::BeginChild("##addcard", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
                    ImGui::SetWindowFontScale(1.15f);
                    ImGui::TextUnformatted("Add a friend");
                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::TextDisabled("Add people by their OkaySpace username or friend code.");
                    ImGui::Dummy(ImVec2(0, 6));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 9));
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 156.0f);
                    bool submit = ImGui::InputTextWithHint("##addfriend", "Username or friend code", addBuf, sizeof(addBuf),
                                                           ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::SameLine(0, 10);
                    ImGui::BeginDisabled(!addBuf[0]);
                    bool send = PrimaryButton("Send request", ImVec2(146, ImGui::GetFrameHeight()));
                    ImGui::EndDisabled();
                    ImGui::PopStyleVar(2);
                    if ((send || submit) && addBuf[0]) {
                        // Try username first, then fall back to a friend code.
                        std::string e1, e2;
                        if (fr.Request(addBuf, &e1) || fr.RequestByCode(addBuf, &e2)) {
                            fOk = std::string("Friend request sent to ") + addBuf + ".";
                            Toast(fOk, 1);
                            addBuf[0] = '\0'; fErr.clear(); fNextRefresh = 0;
                        } else { fErr = "Hmm, no player with that username or code. Check the spelling?"; fOk.clear(); }
                    }
                    if (!fErr.empty()) ImGui::TextColored(ImVec4(0.95f, 0.50f, 0.50f, 1), "%s", fErr.c_str());
                    if (!fOk.empty())  ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1), "%s", fOk.c_str());
                    ImGui::EndChild();
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                    if (!side) { ImGui::Dummy(ImVec2(0, 10)); codeCard(); }
                } else {
                    // Search pill.
                    static char friendFilter[64] = "";
                    {
                        ImVec2 sp = ImGui::GetCursorScreenPos();
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(30, 7));
                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputTextWithHint("##friendsearch", "Search friends", friendFilter, sizeof(friendFilter));
                        ImGui::PopStyleVar(2);
                        float fh = ImGui::GetItemRectSize().y;
                        ImVec2 mc(sp.x + 15.0f, sp.y + fh * 0.5f - 1.0f);
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        dl->AddCircle(mc, 4.6f, cDim, 16, 1.5f);
                        dl->AddLine(ImVec2(mc.x + 3.4f, mc.y + 3.4f), ImVec2(mc.x + 6.8f, mc.y + 6.8f), cDim, 1.8f);
                    }
                    std::string ffilt = friendFilter;
                    for (auto& ch : ffilt) ch = (char)std::tolower((unsigned char)ch);
                    auto matches = [&](const okay::Friend* f) {
                        if (ffilt.empty()) return true;
                        std::string hay = nameOf(f) + " @" + f->username;
                        for (auto& ch : hay) ch = (char)std::tolower((unsigned char)ch);
                        return hay.find(ffilt) != std::string::npos;
                    };
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
                    // Offline: most recently online first (then by name).
                    std::sort(offline.begin(), offline.end(), [&](const okay::Friend* a, const okay::Friend* b) {
                        if (a->lastSeen != b->lastSeen) return a->lastSeen > b->lastSeen;
                        return byName(a, b);
                    });
                    int shownN = 0;
                    if (fView == 0) {          // Online: favourites that are online + online
                        std::vector<const okay::Friend*> on;
                        for (auto* f : favs) if (f->online) on.push_back(f);
                        for (auto* f : online) on.push_back(f);
                        if (!on.empty()) { sectionLabel("ONLINE", (int)on.size()); for (auto* f : on) drawFriend(f, 0); shownN += (int)on.size(); }
                    } else if (fView == 1) {   // All
                        if (!favs.empty())    { sectionLabel("FAVORITES", (int)favs.size()); for (auto* f : favs) drawFriend(f, 0); }
                        if (!online.empty())  { sectionLabel("ONLINE", (int)online.size()); for (auto* f : online) drawFriend(f, 0); }
                        if (!offline.empty()) { sectionLabel("OFFLINE", (int)offline.size()); for (auto* f : offline) drawFriend(f, 0); }
                        shownN = (int)(favs.size() + online.size() + offline.size());
                    } else {                   // Pending
                        if (!reqs.empty())    { sectionLabel("INCOMING", (int)reqs.size()); for (auto* f : reqs) drawFriend(f, 1); }
                        if (!pending.empty()) { sectionLabel("SENT", (int)pending.size()); for (auto* f : pending) drawFriend(f, 2); }
                        shownN = (int)(reqs.size() + pending.size());
                    }
                    if (shownN == 0) {
                        if (!ffilt.empty())      emptyState(6, "No matches", "No friends match your search.");
                        else if (fCache.empty()) emptyState(6, "No friends yet", "Use \"Add friend\" to send your first request.");
                        else if (fView == 0)     emptyState(6, "No one's online right now", "Friends who come online will show up here.");
                        else if (fView == 2)     emptyState(kIcoBell, "No pending requests", "Requests you send or receive appear here.");
                        else                     emptyState(6, "No friends yet", "Use \"Add friend\" to send your first request.");
                    }
                    // Incoming requests nudge on the Online/All views.
                    if (fView != 2 && reqN > 0 && ffilt.empty()) {
                        ImGui::Dummy(ImVec2(0, 8));
                        char rq[64]; std::snprintf(rq, sizeof(rq), "%d friend request%s waiting  \xC2\xBB", reqN, reqN == 1 ? "" : "s");
                        ImGui::PushStyleColor(ImGuiCol_Text, accLt);
                        if (ImGui::Selectable(rq, false, 0, ImVec2(0, 0))) fView = 2;
                        ImGui::PopStyleColor();
                    }
                }
                if (!side) { ImGui::Dummy(ImVec2(0, 12)); activityCard(); }
                ImGui::EndChild();   // ##friendlist
                if (side) {
                    ImGui::SameLine(0, 18);
                    ImGui::BeginChild("##frside", ImVec2(0, bodyH), false);
                    codeCard();
                    ImGui::Dummy(ImVec2(0, 10));
                    activityCard();
                    ImGui::EndChild();
                }

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
            static const bool kSvDemo = std::getenv("OKAY_UI_DEMO_USER") != nullptr;   // dev screenshot hook
            okay::Servers sv(account);
            if (!sv.Available() && !kSvDemo) {
                loginGate("Servers");
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
                static bool svShowMembers = true, svAddOpen = false, svAddChan = false, svFocus = false;
                static std::map<long,long> svLikes; static std::map<long,bool> svMyLikes; static long svEditId = 0;
                // Browser-style tabs: one per opened channel, across servers.
                struct SvTab { long sel = 0, chan = 0; std::string chanName, srvName; };
                static std::vector<SvTab> svTabs;
                static long svTabSkipSel = 0, svTabSkipChan = 0;   // last tab was closed: don't re-add this view
                Uint32 now = SDL_GetTicks();
                if (kSvDemo) {
                    static bool seeded = false;
                    if (!seeded) {
                        seeded = true;
                        long t0 = (long)std::time(nullptr);
                        svList = {{1, "Game Jam Crew", "JAM-2026", "me"}, {2, "Pixel Artists", "PIX-88", "x"}, {3, "OkaySpace Devs", "OKAY-1", "x"}};
                        svChannels = {{11, "general"}, {12, "game-jam"}, {13, "showcase"}, {14, "help"}};
                        auto mem = [&](const char* n, const char* role, bool on) { okay::ServerMember m; m.name = n; m.role = role; m.online = on; svMembers.push_back(m); };
                        mem("kingimann", "owner", true); mem("maya", "member", true); mem("alexr", "member", true);
                        mem("priya", "member", false); mem("jonas", "member", false);
                        svMembers[3].lastSeen = t0 - 47 * 60; svMembers[4].lastSeen = t0 - 2 * 86400;
                        auto msg = [&](long id, const char* a, const char* b, long ago, long likes) {
                            okay::ChannelMessage m; m.id = id; m.author = a; m.body = b; m.when = t0 - ago; svMsgs.push_back(m); if (likes) svLikes[id] = likes; };
                        msg(1, "maya", "Morning! Theme for the jam just dropped: orbit", 5400, 3);
                        msg(2, "maya", "I'm thinking a tiny planet hopper, anyone want to team up?", 5340, 0);
                        msg(3, "alexr", "I'm in. I can do the character controller + camera", 4800, 1);
                        msg(4, "kingimann", "Same, I'll take UI and the menus. Let's use the Platformer template as a base", 4200, 2);
                        msg(5, "maya", "Perfect. First playable by tonight?", 600, 0);
                        svMyLikes[4] = true;
                        svSel = 1; svSelName = "Game Jam Crew"; svChan = 11;
                        svTabs = {{1, 11, "general", "Game Jam Crew"}, {1, 12, "game-jam", "Game Jam Crew"}, {3, 14, "help", "OkaySpace Devs"}};
                    }
                } else {
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
                    // Open the first server automatically (no empty "pick a server" pane).
                    if (svSel == 0 && !svList.empty()) { svSel = svList[0].id; svSelName = svList[0].name; svChan = 0; svNextChan = 0; }
                    if (svSel != 0) {
                        // Feed the background Servers poller; read what it published.
                        // No channels/members/messages fetch on the UI thread.
                        g_srvActiveMs = now; g_srvSel = svSel; g_srvChan = svChan;
                        (void)svNextChan; (void)svNextMem; (void)svNextMsg;
                        if (g_srvMutex) {
                            SDL_LockMutex(g_srvMutex);
                            if (g_srvChanReady) { svChannels = g_srvChannels; g_srvChanReady = false; }
                            if (g_srvMemReady)  { svMembers  = g_srvMembers;  g_srvMemReady  = false; }
                            if (g_srvMsgReady)  {
                                if (SDL_AtomicGet(&g_srvActPending) == 0) { svMsgs = g_srvMsgs; svLikes = g_srvLikes; svMyLikes = g_srvMyLikes; }
                                g_srvMsgReady = false;
                            }
                            if (g_srvActFailed) { g_srvActFailed = false; Toast("Couldn't save the edit \xE2\x80\x94 check your connection", 2); }
                            SDL_UnlockMutex(g_srvMutex);
                        }
                        if (svChan == 0 && !svChannels.empty()) svChan = svChannels[0].id;
                    }
                }
                // Owner check: MyUid() is the session's id, so fetch it once.
                static std::string svMyUidCache; static bool svMyUidGot = false;
                if (!svMyUidGot && !kSvDemo) { svMyUidCache = sv.MyUid(); svMyUidGot = true; }
                const std::string svMyUid = kSvDemo ? std::string("me") : svMyUidCache;
                bool svIAmOwner = false;
                for (const auto& s : svList) if (s.id == svSel && !s.owner.empty() && s.owner == svMyUid) svIAmOwner = true;
                const std::string svMe = kSvDemo ? std::string("kingimann") : account.CurrentSession().username;

                const ImU32 cText = ImGui::GetColorU32(ImGuiCol_Text);
                const ImU32 cDim  = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                const ImVec4 accLt(kAccent.x * 0.55f + 0.45f, kAccent.y * 0.55f + 0.45f, kAccent.z * 0.55f + 0.45f, 1.0f);
                const ImVec4 colBg = okaytheme::Mix(okaytheme::kSunken, okaytheme::kBase, 0.35f);
                const float colH = ImGui::GetContentRegionAvail().y - 46.0f;   // leave the page footer clear
                const float availW = ImGui::GetContentRegionAvail().x;
                auto initials = [](const std::string& n) {
                    std::string o;
                    bool start = true;
                    for (char ch : n) {
                        if (ch == ' ' || ch == '-' || ch == '_') { start = true; continue; }
                        if (start && o.size() < 2) o += (char)std::toupper((unsigned char)ch);
                        start = false;
                    }
                    return o.empty() ? std::string("#") : o;
                };

                // ---- Pane 1: server rail (round icons, Discord-style) -------------------
                ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kSunken);
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 12));
                ImGui::BeginChild("##svrail", ImVec2(72, colH), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
                ImGui::PopStyleVar();
                {
                    ImDrawList* d = ImGui::GetWindowDrawList();
                    const float S = 44.0f;
                    for (const auto& s : svList) {
                        const bool sel = (s.id == svSel);
                        ImGui::PushID((int)s.id);
                        ImVec2 rp = ImGui::GetCursorScreenPos();
                        ImVec2 ip(rp.x + (72.0f - S) * 0.5f, rp.y);
                        ImGui::SetCursorScreenPos(ip);
                        if (ImGui::InvisibleButton("##srv", ImVec2(S, S)) && !sel) {
                            svSel = s.id; svSelName = s.name; svChan = 0; svNextChan = 0; svMsgs.clear(); svEditId = 0;
                            if (!kSvDemo) { svChannels.clear(); svMembers.clear(); }
                            else svChan = 11;
                        }
                        const bool hov = ImGui::IsItemHovered();
                        ImVec4 sc = NameColor(s.name);
                        ImU32 fill = ImGui::GetColorU32(sel ? kAccentDim : ImVec4(sc.x * 0.55f, sc.y * 0.55f, sc.z * 0.55f, 1));
                        // Selected/hovered icons morph from a circle to a rounded square.
                        d->AddRectFilled(ip, ImVec2(ip.x + S, ip.y + S), fill, (sel || hov) ? 14.0f : S * 0.5f);
                        std::string ini = initials(s.name);
                        ImVec2 ts = ImGui::CalcTextSize(ini.c_str());
                        d->AddText(ImVec2(ip.x + (S - ts.x) * 0.5f, ip.y + (S - ts.y) * 0.5f), IM_COL32(245, 246, 250, 255), ini.c_str());
                        if (sel || hov) {   // left pill
                            float ph = sel ? 36.0f : 18.0f;
                            d->AddRectFilled(ImVec2(rp.x, ip.y + (S - ph) * 0.5f), ImVec2(rp.x + 4, ip.y + (S + ph) * 0.5f), IM_COL32(240, 243, 250, 255), 2.0f);
                        }
                        if (hov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", s.name.c_str()); }
                        ImGui::SetCursorScreenPos(ImVec2(rp.x, ip.y + S + 8));
                        ImGui::Dummy(ImVec2(0, 0));
                        ImGui::PopID();
                    }
                    if (!svList.empty()) {
                        ImVec2 lp = ImGui::GetCursorScreenPos();
                        d->AddLine(ImVec2(lp.x + 22, lp.y), ImVec2(lp.x + 50, lp.y), IM_COL32(255, 255, 255, 22), 2.0f);
                        ImGui::Dummy(ImVec2(0, 8));
                    }
                    ImVec2 rp = ImGui::GetCursorScreenPos();
                    ImVec2 ip(rp.x + (72.0f - S) * 0.5f, rp.y);
                    ImGui::SetCursorScreenPos(ip);
                    if (ImGui::InvisibleButton("##svadd", ImVec2(S, S))) { svAddOpen = true; svErr.clear(); }
                    const bool ah = ImGui::IsItemHovered();
                    d->AddRectFilled(ip, ImVec2(ip.x + S, ip.y + S), ah ? IM_COL32(46, 150, 86, 255) : ImGui::GetColorU32(okaytheme::kControl), ah ? 14.0f : S * 0.5f);
                    ImU32 pc = ah ? IM_COL32_WHITE : IM_COL32(76, 200, 110, 255);
                    ImVec2 c(ip.x + S * 0.5f, ip.y + S * 0.5f);
                    d->AddLine(ImVec2(c.x - 7, c.y), ImVec2(c.x + 7, c.y), pc, 2.2f);
                    d->AddLine(ImVec2(c.x, c.y - 7), ImVec2(c.x, c.y + 7), pc, 2.2f);
                    if (ah) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("Create or join a server"); }
                }
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 10);

                if (svSel == 0) {
                    // ---- No servers yet: friendly empty state with both actions ----
                    ImGui::BeginChild("##svempty", ImVec2(0, colH), false);
                    float aw = ImGui::GetContentRegionAvail().x;
                    ImGui::Dummy(ImVec2(0, colH * 0.22f));
                    ImVec2 c(ImGui::GetCursorScreenPos().x + aw * 0.5f, ImGui::GetCursorScreenPos().y + 34);
                    ImGui::GetWindowDrawList()->AddCircleFilled(c, 34.0f, IM_COL32(255, 255, 255, 12), 40);
                    DrawNavIcon(ImGui::GetWindowDrawList(), 8, c, cDim);
                    ImGui::Dummy(ImVec2(0, 80));
                    auto centered = [&](const char* s, bool dim) {
                        float tw = ImGui::CalcTextSize(s).x;
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (aw - tw) * 0.5f));
                        if (dim) ImGui::TextDisabled("%s", s); else ImGui::TextUnformatted(s);
                    };
                    centered("You're not in any servers yet", false);
                    centered("Create one for your team, or join with an invite code.", true);
                    ImGui::Dummy(ImVec2(0, 10));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (aw - 300.0f) * 0.5f));
                    if (PrimaryButton("Create a server", ImVec2(146, 36))) { svAddOpen = true; svErr.clear(); }
                    ImGui::SameLine(0, 8);
                    if (ImGui::Button("Join with code", ImVec2(146, 36))) { svAddOpen = true; svErr.clear(); }
                    ImGui::EndChild();
                } else {
                    // ---- Pane 2: server header + channels ------------------------------
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, colBg);
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
                    ImGui::BeginChild("##svchan", ImVec2(224, colH), ImGuiChildFlags_AlwaysUseWindowPadding);
                    ImGui::PopStyleVar();
                    {
                        ImDrawList* d = ImGui::GetWindowDrawList();
                        // Server name header (menu: invite / settings / leave).
                        ImVec2 hp = ImGui::GetCursorScreenPos();
                        float hw = ImGui::GetContentRegionAvail().x, hh = 38.0f;
                        if (ImGui::InvisibleButton("##svhead", ImVec2(hw, hh))) ImGui::OpenPopup("##svmenu");
                        bool hh2 = ImGui::IsItemHovered();
                        if (hh2) d->AddRectFilled(hp, ImVec2(hp.x + hw, hp.y + hh), IM_COL32(255, 255, 255, 12), 8.0f);
                        d->AddText(ImVec2(hp.x + 10, hp.y + (hh - ImGui::GetTextLineHeight()) * 0.5f), cText,
                                   FitText(svSelName, hw - 40).c_str());
                        float vx = hp.x + hw - 16, vy = hp.y + hh * 0.5f;
                        d->AddLine(ImVec2(vx - 4, vy - 2), ImVec2(vx, vy + 2), cDim, 1.6f);
                        d->AddLine(ImVec2(vx + 4, vy - 2), ImVec2(vx, vy + 2), cDim, 1.6f);
                        if (hh2) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::BeginPopup("##svmenu")) {
                            std::string inv; for (const auto& s : svList) if (s.id == svSel) inv = s.invite;
                            if (ImGui::MenuItem("Copy invite code", inv.c_str(), false, !inv.empty())) {
                                ImGui::SetClipboardText(inv.c_str()); Toast("Invite code copied", 1);
                            }
                            if (svIAmOwner && ImGui::MenuItem("Server settings")) {
                                std::snprintf(svRenSrvBuf, sizeof(svRenSrvBuf), "%s", svSelName.c_str()); svSettingsOpen = true;
                            }
                            ImGui::MenuItem("Show members", nullptr, &svShowMembers);
                            if (!svIAmOwner) {
                                ImGui::Separator();
                                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1));
                                if (ImGui::MenuItem("Leave server")) {
                                    if (sv.Leave(svSel)) { svSel = 0; svChan = 0; svNextList = 0; Toast("Left server", 1); }
                                }
                                ImGui::PopStyleColor();
                            }
                            ImGui::EndPopup();
                        }
                        ImVec2 lp = ImGui::GetCursorScreenPos();
                        d->AddLine(ImVec2(lp.x, lp.y + 2), ImVec2(lp.x + hw, lp.y + 2), IM_COL32(255, 255, 255, 16), 1.0f);
                        ImGui::Dummy(ImVec2(0, 10));
                        // "TEXT CHANNELS" + add button.
                        ImVec2 sp = ImGui::GetCursorScreenPos();
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);
                        ImGui::SetWindowFontScale(0.80f);
                        ImGui::TextDisabled("TEXT CHANNELS");
                        ImGui::SetWindowFontScale(1.0f);
                        ImGui::SetCursorScreenPos(ImVec2(sp.x + hw - 22, sp.y - 3));
                        if (ImGui::InvisibleButton("##addchan", ImVec2(20, 20))) { svAddChan = !svAddChan; svNewChan[0] = '\0'; }
                        bool ach = ImGui::IsItemHovered();
                        ImVec2 ac(sp.x + hw - 12, sp.y + 7);
                        d->AddLine(ImVec2(ac.x - 5, ac.y), ImVec2(ac.x + 5, ac.y), ach ? cText : cDim, 1.6f);
                        d->AddLine(ImVec2(ac.x, ac.y - 5), ImVec2(ac.x, ac.y + 5), ach ? cText : cDim, 1.6f);
                        if (ach) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("Create channel"); }
                        ImGui::SetCursorScreenPos(ImVec2(sp.x, sp.y + 20));
                        ImGui::Dummy(ImVec2(0, 0));
                        if (svAddChan) {
                            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                            ImGui::SetNextItemWidth(-1);
                            ImGui::SetKeyboardFocusHere();
                            bool nc = ImGui::InputTextWithHint("##svnewchan", "new-channel  (Enter)", svNewChan, sizeof(svNewChan), ImGuiInputTextFlags_EnterReturnsTrue);
                            ImGui::PopStyleVar();
                            if (nc && svNewChan[0]) {
                                if (sv.CreateChannel(svSel, svNewChan)) { g_srvForce = true; Toast("Channel created", 1); }
                                svNewChan[0] = '\0'; svAddChan = false;
                            }
                            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) svAddChan = false;
                        }
                        for (const auto& c : svChannels) {
                            const bool sel = (c.id == svChan);
                            ImGui::PushID((int)c.id);
                            ImVec2 rp = ImGui::GetCursorScreenPos();
                            float rw = ImGui::GetContentRegionAvail().x, rh = 32.0f;
                            if (ImGui::InvisibleButton("##chan", ImVec2(rw, rh)) && !sel) {
                                svChan = c.id; svNextMsg = 0; svEditId = 0;
                                if (!kSvDemo) svMsgs.clear();
                            }
                            bool hov = ImGui::IsItemHovered();
                            if (sel || hov) d->AddRectFilled(rp, ImVec2(rp.x + rw, rp.y + rh), sel ? IM_COL32(255, 255, 255, 24) : IM_COL32(255, 255, 255, 11), 7.0f);
                            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                            const float ty = rp.y + (rh - ImGui::GetTextLineHeight()) * 0.5f;
                            d->AddText(ImVec2(rp.x + 10, ty), sel ? cText : cDim, "#");
                            d->AddText(ImVec2(rp.x + 26, ty), (sel || hov) ? cText : ImGui::GetColorU32(ImGuiCol_Text, 0.72f),
                                       FitText(c.name, rw - 34).c_str());
                            // Owner: right-click a channel to rename or delete it.
                            if (svIAmOwner && ImGui::BeginPopupContextItem("##chanctx")) {
                                if (ImGui::MenuItem("Rename channel")) {
                                    svRenChanId = c.id; std::snprintf(svRenChanBuf, sizeof(svRenChanBuf), "%s", c.name.c_str()); svRenChanOpen = true;
                                }
                                if (svChannels.size() > 1 && ImGui::MenuItem("Delete channel")) {
                                    if (sv.DeleteChannel(c.id)) { if (svChan == c.id) { svChan = 0; } g_srvForce = true; Toast("Channel deleted", 1); }
                                }
                                ImGui::EndPopup();
                            }
                            ImGui::PopID();
                        }
                        if (svChannels.empty()) ImGui::TextDisabled("  Loading channels\xE2\x80\xA6");
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0, 10);

                    // ---- Pane 3: channel chat (+ members on wide windows) ----------------
                    const float leftUsed = 72 + 10 + 224 + 10;
                    const bool showMem = svShowMembers && (availW - leftUsed) > 620.0f;
                    const float memW = showMem ? 210.0f : 0.0f;
                    ImGui::BeginChild("##svmsgs", ImVec2(showMem ? availW - leftUsed - memW - 10 : 0.0f, colH), false);
                    std::string chanName;
                    for (const auto& c : svChannels) if (c.id == svChan) { chanName = c.name; break; }
                    // ---- Tabs: one per opened channel, across servers (like the chat window) ----
                    {
                        // Track the open view as a tab (skip a view whose tab was just closed).
                        if (svChan != 0 && !chanName.empty()) {
                            if (svSel != svTabSkipSel || svChan != svTabSkipChan) {
                                svTabSkipSel = svTabSkipChan = 0;
                                bool have = false;
                                for (auto& t : svTabs) if (t.sel == svSel && t.chan == svChan) { t.chanName = chanName; t.srvName = svSelName; have = true; }
                                if (!have) {
                                    svTabs.push_back(SvTab{svSel, svChan, chanName, svSelName});
                                    if (svTabs.size() > 12) svTabs.erase(svTabs.begin());
                                }
                            }
                        }
                        int curIdx = -1;
                        for (std::size_t i = 0; i < svTabs.size(); ++i) if (svTabs[i].sel == svSel && svTabs[i].chan == svChan) curIdx = (int)i;
                        int openIdx = -1, closeIdx = -1;
                        // Ctrl+PgDn / Ctrl+PgUp cycle tabs, Ctrl+W closes the open one.
                        if (!svTabs.empty() && ImGui::GetIO().KeyCtrl && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                            const int n = (int)svTabs.size();
                            if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) openIdx = (curIdx + 1) % n;
                            if (ImGui::IsKeyPressed(ImGuiKey_PageUp))   openIdx = curIdx <= 0 ? n - 1 : curIdx - 1;
                            if (ImGui::IsKeyPressed(ImGuiKey_W, false) && curIdx >= 0) closeIdx = curIdx;
                        }
                        if (svTabs.size() > 1 || (svTabs.size() == 1 && curIdx < 0)) {
                            const float tabH = 30.0f;
                            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 4.0f);
                            ImGui::BeginChild("##svtabs", ImVec2(0, tabH + 6), false,
                                              ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                            ImGui::PopStyleVar();
                            if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f)
                                ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseWheel * 48.0f);
                            ImDrawList* td = ImGui::GetWindowDrawList();
                            const ImVec2 start = ImGui::GetCursorScreenPos();
                            const float th = ImGui::GetTextLineHeight();
                            float x = start.x;
                            for (std::size_t i = 0; i < svTabs.size(); ++i) {
                                const SvTab& t = svTabs[i];
                                const bool active = (int)i == curIdx;
                                const std::string lbl = "#" + t.chanName;
                                const float tw = std::clamp(ImGui::CalcTextSize(lbl.c_str()).x + 66.0f, 110.0f, 200.0f);
                                ImVec2 p(x, start.y);
                                ImGui::PushID((int)i);
                                ImGui::SetCursorScreenPos(p);
                                ImGui::SetNextItemAllowOverlap();
                                if (ImGui::InvisibleButton("##svtab", ImVec2(tw, tabH))) openIdx = (int)i;
                                const bool hov = ImGui::IsItemHovered();
                                if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) closeIdx = (int)i;
                                if (hov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("#%s  \xC2\xB7  %s", t.chanName.c_str(), t.srvName.c_str()); }
                                td->AddRectFilled(p, ImVec2(p.x + tw, p.y + tabH),
                                                  active ? IM_COL32(255, 255, 255, 26) : hov ? IM_COL32(255, 255, 255, 15) : IM_COL32(255, 255, 255, 7), 8.0f);
                                if (active) td->AddRectFilled(ImVec2(p.x + 10, p.y + tabH - 2.5f), ImVec2(p.x + tw - 10, p.y + tabH),
                                                              ImGui::GetColorU32(kAccent), 1.5f);
                                // Server badge: its colour + initial.
                                ImVec2 bc(p.x + 17.0f, p.y + tabH * 0.5f);
                                ImVec4 nc = NameColor(t.srvName);
                                td->AddRectFilled(ImVec2(bc.x - 9, bc.y - 9), ImVec2(bc.x + 9, bc.y + 9),
                                                  ImGui::GetColorU32(ImVec4(nc.x * 0.55f, nc.y * 0.55f, nc.z * 0.55f, 1)), 6.0f);
                                char ini[2] = {(char)(t.srvName.empty() ? '#' : std::toupper((unsigned char)t.srvName[0])), 0};
                                float fs = ImGui::GetFontSize() * 0.75f;
                                ImVec2 is = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, ini);
                                td->AddText(ImGui::GetFont(), fs, ImVec2(bc.x - is.x * 0.5f, bc.y - is.y * 0.5f), IM_COL32_WHITE, ini);
                                const float closeW = 16.0f;
                                td->AddText(ImVec2(p.x + 32.0f, p.y + (tabH - th) * 0.5f), (active || hov) ? cText : cDim,
                                            FitText(lbl, tw - 32.0f - closeW - 12.0f).c_str());
                                ImVec2 xp(p.x + tw - closeW - 6.0f, p.y + (tabH - closeW) * 0.5f);
                                ImGui::SetCursorScreenPos(xp);
                                if (ImGui::InvisibleButton("##x", ImVec2(closeW, closeW))) closeIdx = (int)i;
                                const bool xh = ImGui::IsItemHovered();
                                if (xh) ImGui::SetTooltip("Close  (Ctrl+W)");
                                if (active || hov || xh) {
                                    ImVec2 xc(xp.x + closeW * 0.5f, xp.y + closeW * 0.5f);
                                    if (xh) td->AddCircleFilled(xc, closeW * 0.5f, IM_COL32(255, 255, 255, 30), 16);
                                    td->AddLine(ImVec2(xc.x - 3.5f, xc.y - 3.5f), ImVec2(xc.x + 3.5f, xc.y + 3.5f), xh ? cText : cDim, 1.5f);
                                    td->AddLine(ImVec2(xc.x + 3.5f, xc.y - 3.5f), ImVec2(xc.x - 3.5f, xc.y + 3.5f), xh ? cText : cDim, 1.5f);
                                }
                                ImGui::PopID();
                                x += tw + 4.0f;
                            }
                            ImGui::SetCursorScreenPos(start);
                            ImGui::Dummy(ImVec2(x - start.x, tabH));
                            ImGui::EndChild();
                            ImGui::Dummy(ImVec2(0, 2));
                        }
                        // Switch to a tab: same server -> just the channel; another server -> select it too.
                        auto openTab = [&](const SvTab& t) {
                            if (t.sel != svSel) {
                                svSel = t.sel; svSelName = t.srvName; svNextChan = 0;
                                if (!kSvDemo) { svChannels.clear(); svMembers.clear(); }
                            }
                            svChan = t.chan; svNextMsg = 0; svEditId = 0;
                            if (!kSvDemo) svMsgs.clear();
                        };
                        if (closeIdx >= 0 && closeIdx < (int)svTabs.size()) {
                            const bool wasOpen = closeIdx == curIdx;
                            svTabs.erase(svTabs.begin() + closeIdx);
                            if (wasOpen) {
                                svTabSkipSel = svSel; svTabSkipChan = svChan;   // don't re-add the view we're leaving
                                if (!svTabs.empty()) openTab(svTabs[std::min((std::size_t)closeIdx, svTabs.size() - 1)]);
                            }
                        } else if (openIdx >= 0 && openIdx != curIdx && openIdx < (int)svTabs.size()) {
                            openTab(svTabs[(std::size_t)openIdx]);
                        }
                        chanName.clear();
                        for (const auto& c : svChannels) if (c.id == svChan) { chanName = c.name; break; }
                    }
                    {   // Channel header: "# name" + members toggle.
                        ImDrawList* d = ImGui::GetWindowDrawList();
                        ImVec2 hp = ImGui::GetCursorScreenPos();
                        float hw = ImGui::GetContentRegionAvail().x;
                        ImGui::SetWindowFontScale(1.12f);
                        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(cDim), "#");
                        ImGui::SameLine(0, 6);
                        ImGui::TextUnformatted(chanName.empty() ? "channel" : chanName.c_str());
                        ImGui::SetWindowFontScale(1.0f);
                        ImGui::SetCursorScreenPos(ImVec2(hp.x + hw - 32, hp.y - 4));
                        if (ImGui::InvisibleButton("##memtoggle", ImVec2(30, 30))) svShowMembers = !svShowMembers;
                        bool mh = ImGui::IsItemHovered();
                        if (mh || svShowMembers) d->AddRectFilled(ImVec2(hp.x + hw - 32, hp.y - 4), ImVec2(hp.x + hw - 2, hp.y + 26),
                                                                  mh ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 10), 8.0f);
                        DrawNavIcon(d, 6, ImVec2(hp.x + hw - 17, hp.y + 11), svShowMembers ? cText : cDim);
                        if (mh) ImGui::SetTooltip(svShowMembers ? "Hide members" : "Show members");
                        ImGui::SetCursorScreenPos(ImVec2(hp.x, hp.y + 30));
                        ImVec2 lp = ImGui::GetCursorScreenPos();
                        d->AddLine(lp, ImVec2(lp.x + hw, lp.y), IM_COL32(255, 255, 255, 16), 1.0f);
                        ImGui::Dummy(ImVec2(0, 6));
                    }
                    if (svChan == 0) {
                        ImGui::TextDisabled("Pick a channel on the left.");
                    } else {
                        // Inline per-message actions: like + count, "(edited)", and an
                        // Edit affordance on my own messages (loads it into the box).
                        auto msgActions = [&](const okay::ChannelMessage& m) {
                            long lc = svLikes.count(m.id) ? svLikes[m.id] : 0;
                            bool liked = svMyLikes.count(m.id) != 0;
                            ImGui::PushID((int)m.id);
                            ImVec2 hp2 = ImGui::GetCursorScreenPos();
                            if (ImGui::InvisibleButton("like", ImVec2(18, 16))) {
                                if (liked) { svMyLikes.erase(m.id); if (svLikes[m.id] > 0) --svLikes[m.id]; }
                                else { svMyLikes[m.id] = true; ++svLikes[m.id]; }
                                if (!kSvDemo) ServerActionAsync(liked ? 1 : 0, m.id);
                            }
                            if (ImGui::IsItemHovered()) ImGui::SetTooltip(liked ? "Unlike" : "Like");
                            DrawHeart(ImGui::GetWindowDrawList(), ImVec2(hp2.x+8, hp2.y+9), 6.0f,
                                      liked ? IM_COL32(232,72,92,255) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
                            if (lc > 0) { ImGui::SameLine(0, 2); ImGui::TextDisabled("%ld", lc); }
                            if (m.edited) { ImGui::SameLine(0, 8); ImGui::TextDisabled("(edited)"); }
                            if (!svMe.empty() && m.author == svMe) {
                                ImGui::SameLine(0, 8);
                                if (ImGui::SmallButton("edit")) { svEditId = m.id; std::snprintf(svMsg, sizeof(svMsg), "%s", m.body.c_str()); svFocus = true; }
                            }
                            ImGui::PopID();
                        };
                        const float composerH = (ImGui::GetTextLineHeight() + 16.0f) + 4.0f + ImGui::GetStyle().ItemSpacing.y * 3.0f + 8.0f
                                              + (svEditId ? ImGui::GetTextLineHeightWithSpacing() : 0.0f);
                        ImGui::BeginChild("##svscroll", ImVec2(0, ImGui::GetContentRegionAvail().y - composerH), false);
                        static int svLastShown = -1;
                        std::string prevAuthor; bool firstMsg = true; long prevDay = 0, prevWhen = 0;
                        {   // channel intro at the top of the history (Discord-style "start of #channel")
                            ImGui::Dummy(ImVec2(0, 6));
                            ImVec2 ip = ImGui::GetCursorScreenPos();
                            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(ip.x + 26, ip.y + 26), 26.0f, ImGui::GetColorU32(okaytheme::kControl), 32);
                            ImGui::SetWindowFontScale(1.6f);
                            ImVec2 hs = ImGui::CalcTextSize("#");
                            ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(ip.x + 26 - hs.x * 0.5f, ip.y + 26 - hs.y * 0.5f), cText, "#");
                            ImGui::SetWindowFontScale(1.0f);
                            ImGui::Dummy(ImVec2(0, 58));
                            ImGui::SetWindowFontScale(1.25f);
                            ImGui::Text("Welcome to #%s", chanName.empty() ? "channel" : chanName.c_str());
                            ImGui::SetWindowFontScale(1.0f);
                            ImGui::TextDisabled("This is the start of the #%s channel.", chanName.empty() ? "channel" : chanName.c_str());
                            ImGui::Dummy(ImVec2(0, 8));
                        }
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
                                if (!firstMsg) ImGui::Dummy(ImVec2(0, 8));
                                std::string av; for (const auto& mm : svMembers) if (mm.name == who) { av = mm.avatar; break; }
                                DrawAvatar(av, who, 36.0f);
                                ImGui::SameLine(); ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6);
                                ImGui::BeginGroup();
                                ImVec4 nc2 = NameColor(who);
                                ImGui::TextColored(ImVec4(nc2.x * 0.35f + 0.65f, nc2.y * 0.35f + 0.65f, nc2.z * 0.35f + 0.65f, 1), "%s", who.c_str());
                                ImGui::SameLine(0, 8); ImGui::TextDisabled("%s", w);
                                RenderChatBody(m.body);
                                msgActions(m);
                                ImGui::EndGroup();
                                prevAuthor = who;
                            } else {
                                // Continuation of the same author's group: indent under the
                                // avatar, with a faint timestamp revealed on hover (Discord-like).
                                ImGui::Indent(42.0f);
                                ImVec2 rowTop = ImGui::GetCursorScreenPos();
                                RenderChatBody(m.body);
                                if (w[0] && ImGui::IsItemHovered()) {
                                    ImVec2 tp(rowTop.x - 40.0f, rowTop.y + 1.0f);
                                    ImGui::GetWindowDrawList()->AddText(tp, ImGui::GetColorU32(ImGuiCol_TextDisabled), w);
                                }
                                msgActions(m);
                                ImGui::Unindent(42.0f);
                            }
                            firstMsg = false;
                        }
                        {   // My posts still on their way: same row layout, dimmed, with a clock
                            // while sending and Retry / Discard if the post failed.
                            const std::vector<OutMsg> pend = OutboxForChannel(svChan, [&](const std::string& body, long when) {
                                for (auto it = svMsgs.rbegin(); it != svMsgs.rend(); ++it)
                                    if (it->author == svMe && it->body == body && (it->when == 0 || it->when >= when - 120)) return true;
                                return false;
                            });
                            for (const auto& o : pend) {
                                ImGui::PushID((int)o.localId + 700000);
                                const bool failed = o.state == -1;
                                const bool cont = svMe == prevAuthor;
                                if (!cont) {
                                    if (!firstMsg) ImGui::Dummy(ImVec2(0, 8));
                                    std::string av; for (const auto& mm : svMembers) if (mm.name == svMe) { av = mm.avatar; break; }
                                    DrawAvatar(av, svMe, 36.0f);
                                    ImGui::SameLine(); ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6);
                                    ImGui::BeginGroup();
                                    ImVec4 nc2 = NameColor(svMe);
                                    ImGui::TextColored(ImVec4(nc2.x * 0.35f + 0.65f, nc2.y * 0.35f + 0.65f, nc2.z * 0.35f + 0.65f, 1), "%s", svMe.c_str());
                                    prevAuthor = svMe;
                                } else {
                                    ImGui::Indent(42.0f);
                                    ImGui::BeginGroup();
                                }
                                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (failed ? 0.85f : 0.55f));
                                RenderChatBody(o.body);
                                ImGui::PopStyleVar();
                                if (failed) {
                                    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.55f, 1.0f), "Not sent");
                                    ImGui::SameLine(0, 8); if (ImGui::SmallButton("Retry")) OutboxRetry(o.localId);
                                    ImGui::SameLine(0, 4); if (ImGui::SmallButton("Discard")) OutboxDiscard(o.localId);
                                } else {
                                    ImVec2 cp = ImGui::GetCursorScreenPos();
                                    const float r = 5.0f, cy = cp.y + ImGui::GetTextLineHeight() * 0.5f;
                                    ImDrawList* dl = ImGui::GetWindowDrawList();
                                    dl->AddCircle(ImVec2(cp.x + r, cy), r, cDim, 12, 1.2f);
                                    dl->AddLine(ImVec2(cp.x + r, cy), ImVec2(cp.x + r, cy - 3.2f), cDim, 1.2f);
                                    dl->AddLine(ImVec2(cp.x + r, cy), ImVec2(cp.x + r + 2.6f, cy), cDim, 1.2f);
                                    ImGui::Dummy(ImVec2(r * 2 + 4, ImGui::GetTextLineHeight())); ImGui::SameLine(0, 2);
                                    ImGui::TextDisabled("Sending\xE2\x80\xA6");
                                }
                                ImGui::EndGroup();
                                if (cont) ImGui::Unindent(42.0f);
                                firstMsg = false;
                                ImGui::PopID();
                            }
                            const int shown = (int)svMsgs.size() * 64 + (int)pend.size();
                            if (shown != svLastShown) { ImGui::SetScrollHereY(1.0f); svLastShown = shown; }
                        }
                        ImGui::EndChild();
                        if (svEditId) {
                            ImGui::TextDisabled("Editing a message"); ImGui::SameLine();
                            if (ImGui::SmallButton("cancel edit")) { svEditId = 0; svMsg[0] = '\0'; }
                        }
                        // Composer (chat-style pill): emoji + input, Enter sends,
                        // Shift/Ctrl+Enter adds a line.
                        ImGui::Dummy(ImVec2(0, 4));
                        EmojiPicker("svemoji", svMsg, sizeof(svMsg)); ImGui::SameLine(0, 6);
                        const float inH = ImGui::GetTextLineHeight() + 16.0f;
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(11, 8));
                        if (svFocus) { ImGui::SetKeyboardFocusHere(); svFocus = false; }
                        std::string hint = "Message #" + (chanName.empty() ? std::string("channel") : chanName);
                        bool send = ImGui::InputTextMultiline("##svsend", svMsg, sizeof(svMsg),
                            ImVec2(ImGui::GetContentRegionAvail().x - 70, inH),
                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
                        if (!svMsg[0] && !ImGui::IsItemActive()) {   // placeholder (multiline inputs have no hint)
                            ImVec2 ip = ImGui::GetItemRectMin();
                            ImGui::GetWindowDrawList()->AddText(ImVec2(ip.x + 11, ip.y + 8), cDim, hint.c_str());
                        }
                        ImGui::PopStyleVar(2);
                        ImGui::SameLine(0, 8);
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
                        bool clicked = PrimaryButton(svEditId ? "Save" : "Send", ImVec2(62, inH));
                        ImGui::PopStyleVar();
                        if ((clicked || send) && svMsg[0]) {
                            if (svEditId) {   // instant: patch the local copy, save off-thread
                                for (auto& em : svMsgs) if (em.id == svEditId) { em.body = svMsg; em.edited = true; }
                                if (!kSvDemo) ServerActionAsync(2, svEditId, svMsg);
                                svEditId = 0; svMsg[0] = '\0';
                            }
                            else if (kSvDemo && !std::getenv("OKAY_CHAT_DEMO_OUTBOX")) {   // demo: local echo
                                okay::ChannelMessage em; em.id = 900000 + (long)svMsgs.size(); em.author = svMe;
                                em.body = svMsg; em.when = (long)std::time(nullptr);
                                svMsgs.push_back(em); svMsg[0] = '\0';
                            } else {   // instant: shows as a pending row now, posts off-thread
                                OutboxSendChannel(svChan, ExpandCustomEmoji(svMsg)); svMsg[0] = '\0';
                            }
                            svFocus = true;   // keep typing (refocus the box, not the Send button)
                        }
                    }
                    ImGui::EndChild();

                    if (showMem) {   // ---- Members (Online / Offline) ----
                        ImGui::SameLine(0, 10);
                        ImGui::PushStyleColor(ImGuiCol_ChildBg, colBg);
                        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
                        ImGui::BeginChild("##svmembers", ImVec2(memW, colH), ImGuiChildFlags_AlwaysUseWindowPadding);
                        ImGui::PopStyleVar();
                        int onlineN = 0; for (const auto& m : svMembers) if (m.online) ++onlineN;
                        auto drawMember = [&](const okay::ServerMember& m) {
                            ImDrawList* d = ImGui::GetWindowDrawList();
                            ImVec2 mp = ImGui::GetCursorScreenPos();
                            float rw = ImGui::GetContentRegionAvail().x;
                            std::string nm = m.name.empty() ? std::string("member") : m.name;
                            ImGui::PushID(m.uid.empty() ? nm.c_str() : m.uid.c_str());
                            if (ImGui::InvisibleButton("##memrow", ImVec2(rw, 40)))   // open their public profile
                                OpenPublicProfile(m.uid, m.uid.empty() ? nm : std::string(), nm, m.avatar);
                            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                            ImGui::PopID();
                            if (ImGui::IsMouseHoveringRect(mp, ImVec2(mp.x + rw, mp.y + 40)))
                                d->AddRectFilled(mp, ImVec2(mp.x + rw, mp.y + 40), IM_COL32(255, 255, 255, 10), 8.0f);
                            ImGui::SetCursorScreenPos(ImVec2(mp.x + 4, mp.y + 5));
                            DrawAvatar(m.avatar, nm, 30.0f);
                            ImVec2 pd(mp.x + 4 + 26, mp.y + 5 + 26);
                            d->AddCircleFilled(pd, 6.0f, ImGui::GetColorU32(colBg), 12);
                            d->AddCircleFilled(pd, 4.0f, m.online ? IM_COL32(76, 200, 110, 255) : IM_COL32(110, 118, 130, 255), 12);
                            ImU32 tc = m.online ? cText : ImGui::GetColorU32(ImGuiCol_Text, 0.45f);
                            float tx = mp.x + 44;
                            const bool seenLine = !m.online && m.lastSeen > 0;   // "2h ago" under offline names
                            const float nameY = seenLine ? mp.y + 4 : mp.y + 20 - ImGui::GetTextLineHeight() * 0.5f;
                            d->AddText(ImVec2(tx, nameY), tc, FitText(nm, rw - 66).c_str());
                            if (seenLine) {
                                ImFont* sf = ImGui::GetFont(); const float fs = ImGui::GetFontSize() * 0.82f;
                                d->AddText(sf, fs, ImVec2(tx, mp.y + 22), ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.85f),
                                           FitText(RelTime(m.lastSeen), rw - 66).c_str());
                            }
                            if (m.role == "owner") {   // crown-ish gold star after the name
                                float nx = tx + std::min(ImGui::CalcTextSize(nm.c_str()).x, rw - 66) + 10;
                                const float sy = nameY + ImGui::GetTextLineHeight() * 0.5f;
                                DrawStar(d, ImVec2(nx, sy), 5.5f, IM_COL32(250, 200, 70, 255));
                                if (ImGui::IsMouseHoveringRect(ImVec2(nx - 6, mp.y + 14), ImVec2(nx + 6, mp.y + 26))) ImGui::SetTooltip("Server owner");
                            }
                            ImGui::SetCursorScreenPos(ImVec2(mp.x, mp.y + 42));
                            ImGui::Dummy(ImVec2(0, 0));
                        };
                        auto label = [&](const char* t, int n) {
                            ImGui::SetWindowFontScale(0.80f);
                            ImGui::TextDisabled("  %s  \xE2\x80\x94  %d", t, n);
                            ImGui::SetWindowFontScale(1.0f);
                            ImGui::Dummy(ImVec2(0, 2));
                        };
                        if (onlineN > 0) { label("ONLINE", onlineN); for (const auto& m : svMembers) if (m.online) drawMember(m); }
                        int offN = (int)svMembers.size() - onlineN;
                        if (offN > 0) { ImGui::Dummy(ImVec2(0, 6)); label("OFFLINE", offN); for (const auto& m : svMembers) if (!m.online) drawMember(m); }
                        if (svMembers.empty()) ImGui::TextDisabled("Loading members\xE2\x80\xA6");
                        ImGui::EndChild();
                        ImGui::PopStyleVar();
                        ImGui::PopStyleColor();
                    }
                }

                // --- Create / join a server ---
                if (svAddOpen) { ImGui::OpenPopup("Add a server"); svAddOpen = false; }
                ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
                if (ImGui::BeginPopupModal("Add a server", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::SetWindowFontScale(1.12f);
                    ImGui::TextUnformatted("Create a server");
                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::TextDisabled("A home for your team: channels, chat and members.");
                    ImGui::SetNextItemWidth(-1);
                    bool mk = ImGui::InputTextWithHint("##svnew", "Server name", svNew, sizeof(svNew), ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::BeginDisabled(!svNew[0]);
                    bool mkBtn = PrimaryButton("Create", ImVec2(-1, 34));
                    ImGui::EndDisabled();
                    if ((mkBtn || mk) && svNew[0]) {
                        okay::Server made;
                        if (sv.CreateServer(svNew, &made, &svErr)) {
                            svNew[0] = '\0'; svErr.clear(); svNextList = 0;
                            svSel = made.id; svSelName = made.name; svChan = 0; svNextChan = 0;
                            Toast("Server created", 1);
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::Dummy(ImVec2(0, 8));
                    ImGui::SeparatorText("or");
                    ImGui::SetWindowFontScale(1.12f);
                    ImGui::TextUnformatted("Join with an invite code");
                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::SetNextItemWidth(-1);
                    bool jn = ImGui::InputTextWithHint("##svjoin", "e.g. JAM-2026", svJoin, sizeof(svJoin), ImGuiInputTextFlags_EnterReturnsTrue);
                    ImGui::BeginDisabled(!svJoin[0]);
                    bool jnBtn = ImGui::Button("Join server", ImVec2(-1, 34));
                    ImGui::EndDisabled();
                    if ((jnBtn || jn) && svJoin[0]) {
                        if (sv.JoinServer(svJoin, &svErr)) { svJoin[0] = '\0'; svErr.clear(); svNextList = 0; Toast("Joined server", 1); ImGui::CloseCurrentPopup(); }
                    }
                    if (!svErr.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.5f, 0.5f, 1)); ImGui::TextWrapped("%s", svErr.c_str()); ImGui::PopStyleColor(); }
                    ImGui::Dummy(ImVec2(0, 4));
                    if (ImGui::Button("Cancel", ImVec2(-1, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
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
            static const bool kFoDemo = std::getenv("OKAY_UI_DEMO_USER") != nullptr;   // dev screenshot hook
            okay::Forums fo(account);
            static std::vector<okay::ForumThread> ftCache;
            static std::vector<okay::ForumPost> fpCache;
            static std::map<long,long> thScores, poScores;
            static std::map<long,int>  thMy, poMy, thComments;
            static long   openThread = 0;
            static std::string openTitle, openAuthor, openBody, openCat, foErr;
            static long openWhen = 0;
            static char newThread[160] = "", newBody[800] = "", newCat[48] = "", replyBuf[600] = "", foSearch[64] = "";
            static int  foSort = 0;      // 0 Hot, 1 New, 2 Top
            static bool foShowNew = false, foReplyFocus = false;
            static std::string foCat;    // selected category chip ("" = all)
            static Uint32 foNext = 0, fpNext = 0;
            Uint32 tnow = SDL_GetTicks();
            const ImU32 cText = ImGui::GetColorU32(ImGuiCol_Text), cDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const ImVec4 accLt(kAccent.x * 0.55f + 0.45f, kAccent.y * 0.55f + 0.45f, kAccent.z * 0.55f + 0.45f, 1.0f);
            const bool canAct = fo.Available() || kFoDemo;
            if (kFoDemo) {
                static bool seeded = false;
                if (!seeded) {
                    seeded = true;
                    const long t0 = (long)std::time(nullptr);
                    auto th = [&](long id, const char* title, const char* au, const char* cat, const char* body, long ago, long score, int comments) {
                        okay::ForumThread t; t.id = id; t.title = title; t.author = au; t.category = cat; t.body = body; t.when = t0 - ago;
                        ftCache.push_back(t); thScores[id] = score; thComments[id] = comments; };
                    th(1, "Show off your jam games!", "maya", "showcase", "Post a screenshot and a link. Planet hoppers especially welcome.", 3600, 42, 12);
                    th(2, "How do I make the camera follow smoothly in 2D?", "alexr", "help", "My camera snaps to the player every frame and it feels jittery. Is there a damping option?", 7200, 17, 5);
                    th(3, "OkayScript tip: use Update(dt) for movement", "priya", "tutorials", "Multiply speeds by dt so your game runs the same at 30 or 144 fps.", 86400, 31, 8);
                    th(4, "Feature idea: tilemap auto-tiling", "jonas", "ideas", "", 3 * 86400, 9, 3);
                    th(5, "Weekly jam #12: theme is ORBIT", "kingimann", "events", "48 hours, any engine feature goes. Submit by Sunday night.", 5 * 3600, 56, 21);
                    thMy[5] = 1; thMy[1] = 1;
                    auto po = [&](long id, const char* au, const char* body, long ago, long score) {
                        okay::ForumPost p; p.id = id; p.author = au; p.body = body; p.when = t0 - ago; fpCache.push_back(p); poScores[id] = score; };
                    po(101, "priya", "Use the Camera Follow component and set damping to ~0.15. Way smoother.", 6500, 9);
                    po(102, "alexr", "That fixed it, thank you!! \xF0\x9F\x99\x8F", 6200, 3);
                    po(103, "maya", "Also turn on pixel snapping if your sprites shimmer.", 5000, 5);
                    if (const char* ot = std::getenv("OKAY_UI_DEMO_THREAD")) {
                        long id = std::atol(ot);
                        for (const auto& t : ftCache) if (t.id == id) { openThread = t.id; openTitle = t.title; openAuthor = t.author; openBody = t.body; openCat = t.category; openWhen = t.when; }
                    }
                }
            } else {
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
            }

            // Vote column (Reddit-style, drawn chevrons): clicking your current
            // direction clears the vote. Updates the caches OPTIMISTICALLY and fires
            // the POST on a background thread, so a click never stalls the frame.
            // horizontal = compact inline widget for comments.
            auto voteWidget = [&](const char* kind, long id, std::map<long,long>& scores, std::map<long,int>& myVotes, bool horizontal) {
                const long baseScore = scores.count(id) ? scores[id] : 0L;
                const int  myVote    = myVotes.count(id) ? myVotes[id] : 0;
                auto apply = [&](int nv) {
                    scores[id] = baseScore + (nv - myVote);
                    myVotes[id] = nv;
                    if (!kFoDemo) FireForumAction(new FoAction{0, kind, id, nv, "", "", ""});
                };
                ImGui::PushID((int)id); ImGui::PushID(kind);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImU32 upC = IM_COL32(236, 128, 64, 255), dnC = IM_COL32(120, 140, 245, 255);
                ImVec2 p = ImGui::GetCursorScreenPos();
                const float b = 24.0f;
                char sc[16]; std::snprintf(sc, sizeof(sc), "%ld", scores.count(id) ? scores[id] : baseScore);
                const ImVec2 ss = ImGui::CalcTextSize(sc);
                const ImU32 scCol = myVote == 1 ? upC : myVote == -1 ? dnC : cText;
                auto arrow = [&](const char* bid, ImVec2 at, bool up, bool on, ImU32 col) {
                    ImGui::SetCursorScreenPos(at);
                    bool click = ImGui::InvisibleButton(bid, ImVec2(b, b)) && canAct;
                    bool h = ImGui::IsItemHovered();
                    if (h) { dl->AddRectFilled(at, ImVec2(at.x + b, at.y + b), IM_COL32(255, 255, 255, 18), 6.0f); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); if (!canAct) ImGui::SetTooltip("Sign in to vote"); }
                    ImVec2 c(at.x + b * 0.5f, at.y + b * 0.5f);
                    ImU32 ac = on ? col : (h ? cText : cDim);
                    if (up) dl->AddTriangleFilled(ImVec2(c.x, c.y - 5), ImVec2(c.x + 6, c.y + 3), ImVec2(c.x - 6, c.y + 3), ac);
                    else    dl->AddTriangleFilled(ImVec2(c.x - 6, c.y - 3), ImVec2(c.x + 6, c.y - 3), ImVec2(c.x, c.y + 5), ac);
                    return click;
                };
                if (!horizontal) {
                    if (arrow("u", p, true, myVote == 1, upC)) apply(myVote == 1 ? 0 : 1);
                    dl->AddText(ImVec2(p.x + (b - ss.x) * 0.5f, p.y + b + 1), scCol, sc);
                    if (arrow("d", ImVec2(p.x, p.y + b + ss.y + 2), false, myVote == -1, dnC)) apply(myVote == -1 ? 0 : -1);
                    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + b * 2 + ss.y + 2));
                } else {
                    if (arrow("u", p, true, myVote == 1, upC)) apply(myVote == 1 ? 0 : 1);
                    dl->AddText(ImVec2(p.x + b + 4, p.y + (b - ss.y) * 0.5f), scCol, sc);
                    if (arrow("d", ImVec2(p.x + b + 8 + ss.x, p.y), false, myVote == -1, dnC)) apply(myVote == -1 ? 0 : -1);
                    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + b));
                }
                ImGui::Dummy(ImVec2(0, 0));
                ImGui::PopID(); ImGui::PopID();
            };
            // Category pill (tinted by its name).
            auto catPill = [&](ImDrawList* dl, ImVec2 at, const std::string& cat) -> float {
                if (cat.empty()) return 0.0f;
                ImVec4 nc = NameColor(cat);
                float fs = ImGui::GetFontSize() * 0.82f;
                ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, cat.c_str());
                dl->AddRectFilled(at, ImVec2(at.x + ts.x + 14, at.y + ts.y + 6), ImGui::GetColorU32(ImVec4(nc.x, nc.y, nc.z, 0.22f)), (ts.y + 6) * 0.5f);
                dl->AddText(ImGui::GetFont(), fs, ImVec2(at.x + 7, at.y + 3), ImGui::GetColorU32(ImVec4(nc.x * 0.4f + 0.6f, nc.y * 0.4f + 0.6f, nc.z * 0.4f + 0.6f, 1)), cat.c_str());
                return ts.x + 14;
            };
            // Small comment-bubble glyph.
            auto commentIcon = [&](ImDrawList* dl, ImVec2 c, ImU32 col) {
                dl->AddRect(ImVec2(c.x - 6, c.y - 5), ImVec2(c.x + 6, c.y + 3), col, 3.0f, 0, 1.4f);
                dl->AddTriangleFilled(ImVec2(c.x - 3, c.y + 3), ImVec2(c.x + 1, c.y + 3), ImVec2(c.x - 3, c.y + 7), col);
            };
            auto searchPill = [&](const char* id, const char* hint, char* buf, std::size_t cap, float w) {
                ImVec2 sp = ImGui::GetCursorScreenPos();
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 16.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(30, 7));
                ImGui::SetNextItemWidth(w);
                ImGui::InputTextWithHint(id, hint, buf, cap);
                ImGui::PopStyleVar(2);
                ImVec2 mc(sp.x + 15.0f, sp.y + ImGui::GetItemRectSize().y * 0.5f - 1.0f);
                ImGui::GetWindowDrawList()->AddCircle(mc, 4.6f, cDim, 16, 1.5f);
                ImGui::GetWindowDrawList()->AddLine(ImVec2(mc.x + 3.4f, mc.y + 3.4f), ImVec2(mc.x + 6.8f, mc.y + 6.8f), cDim, 1.8f);
            };

            if (openThread == 0) {
                // ---- Toolbar: categories (left), sort + search + New post (right) ----
                std::vector<std::string> cats;
                for (const auto& t : ftCache) if (!t.category.empty() && std::find(cats.begin(), cats.end(), t.category) == cats.end()) cats.push_back(t.category);
                std::sort(cats.begin(), cats.end());
                {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 cp = ImGui::GetCursorScreenPos();
                    const float ch = ImGui::GetTextLineHeight() + 12.0f, right = cp.x + ImGui::GetContentRegionAvail().x;
                    // Right side first: New post, then sort segmented control.
                    ImGui::SetCursorScreenPos(ImVec2(right - 120, cp.y));
                    if (PrimaryButton(foShowNew ? "Cancel" : "New post", ImVec2(120, ch))) foShowNew = !foShowNew;
                    const char* sorts[] = {"Hot", "New", "Top"};
                    float segW = 0; for (auto* s2 : sorts) segW += ImGui::CalcTextSize(s2).x + 24;
                    float sx = right - 120 - 12 - segW;
                    dl->AddRectFilled(ImVec2(sx - 3, cp.y), ImVec2(sx + segW + 3, cp.y + ch), IM_COL32(255, 255, 255, 10), ch * 0.5f);
                    for (int k = 0; k < 3; ++k) {
                        float w = ImGui::CalcTextSize(sorts[k]).x + 24;
                        ImGui::SetCursorScreenPos(ImVec2(sx, cp.y));
                        ImGui::PushID(k + 50);
                        if (ImGui::InvisibleButton("##fs", ImVec2(w, ch))) foSort = k;
                        bool h = ImGui::IsItemHovered();
                        ImGui::PopID();
                        if (foSort == k) dl->AddRectFilled(ImVec2(sx + 2, cp.y + 3), ImVec2(sx + w - 2, cp.y + ch - 3), ImGui::GetColorU32(okaytheme::kControl), (ch - 6) * 0.5f);
                        dl->AddText(ImVec2(sx + 12, cp.y + (ch - ImGui::GetTextLineHeight()) * 0.5f), foSort == k ? cText : (h ? cText : cDim), sorts[k]);
                        sx += w;
                    }
                    // Left: category chips (wrap-less; extra ones scroll off).
                    float x = cp.x;
                    const float chipsRight = right - 120 - 12 - segW - 16;
                    auto chip = [&](const std::string& label, const std::string& value) {
                        ImVec2 ts = ImGui::CalcTextSize(label.c_str());
                        float cw = ts.x + 24;
                        if (x + cw > chipsRight) return;
                        ImGui::SetCursorScreenPos(ImVec2(x, cp.y));
                        ImGui::PushID(label.c_str());
                        if (ImGui::InvisibleButton("##fc", ImVec2(cw, ch))) foCat = value;
                        bool h = ImGui::IsItemHovered();
                        ImGui::PopID();
                        bool on = foCat == value;
                        dl->AddRectFilled(ImVec2(x, cp.y), ImVec2(x + cw, cp.y + ch),
                                          on ? ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.24f)) : (h ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 9)), ch * 0.5f);
                        dl->AddText(ImVec2(x + 12, cp.y + (ch - ts.y) * 0.5f), on ? ImGui::GetColorU32(accLt) : (h ? cText : cDim), label.c_str());
                        if (h) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        x += cw + 6;
                    };
                    chip("All", "");
                    for (const auto& c : cats) chip(c, c);
                    ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y + ch + 10));
                    ImGui::Dummy(ImVec2(0, 0));
                }
                // ---- New post composer ----
                if (foShowNew) {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kRaised);
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
                    ImGui::BeginChild("##newpost", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
                    if (!canAct) {
                        ImGui::TextDisabled("Sign in to post.");
                    } else {
                        ImGui::TextUnformatted("Start a discussion");
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputTextWithHint("##nt", "Title", newThread, sizeof(newThread));
                        ImGui::SetNextItemWidth(240);
                        ImGui::InputTextWithHint("##ncat", "Topic (e.g. help, showcase)", newCat, sizeof(newCat));
                        for (const auto& c : cats) {   // quick-pick existing topics
                            ImGui::SameLine(0, 6);
                            if (ImGui::SmallButton(c.c_str())) std::snprintf(newCat, sizeof(newCat), "%s", c.c_str());
                        }
                        ImGui::InputTextMultiline("##nb", newBody, sizeof(newBody), ImVec2(-1, 90));
                        ImGui::PopStyleVar(2);
                        ImGui::BeginDisabled(!newThread[0]);
                        if (PrimaryButton("Post", ImVec2(110, 32))) {
                            // Fire the post off-thread; optimistically close the form.
                            if (!kFoDemo) FireForumAction(new FoAction{2, "", 0, 0, newThread, newBody, newCat});
                            newThread[0] = newBody[0] = newCat[0] = '\0'; foErr.clear();
                            foShowNew = false; foNext = tnow + 1200;   // refresh shortly after
                            Toast("Posted", 1);
                        }
                        ImGui::EndDisabled();
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel", ImVec2(90, 32))) foShowNew = false;
                        if (!foErr.empty()) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.45f, 1), "%s", foErr.c_str()); }
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor();
                    ImGui::Dummy(ImVec2(0, 6));
                }
                // ---- Search ----
                searchPill("##fosearch", "Search discussions", foSearch, sizeof(foSearch), -1);
                ImGui::Dummy(ImVec2(0, 4));

                // Sorted + filtered view.
                std::vector<const okay::ForumThread*> order;
                auto lo = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
                const std::string q = lo(foSearch);
                for (auto& t : ftCache) {
                    if (!foCat.empty() && t.category != foCat) continue;
                    if (!q.empty() && lo(t.title + " " + t.body + " " + t.author).find(q) == std::string::npos) continue;
                    order.push_back(&t);
                }
                auto scoreOf = [&](long id) { auto it = thScores.find(id); return it != thScores.end() ? it->second : 0L; };
                if (foSort == 2)        // Top: highest score
                    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return scoreOf(a->id) > scoreOf(b->id); });
                else if (foSort == 0)   // Hot: score decays with age (hours)
                    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) {
                        const long n = (long)std::time(nullptr);
                        auto hot = [&](const okay::ForumThread* t) { return (scoreOf(t->id) + 1.0) / std::pow((n - t->when) / 3600.0 + 2.0, 1.5); };
                        return hot(a) > hot(b); });
                else                    // New
                    std::stable_sort(order.begin(), order.end(), [](auto a, auto b) { return a->when > b->when; });

                ImGui::BeginChild("##threads", ImVec2(0, ImGui::GetContentRegionAvail().y - 46.0f), false);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                for (const okay::ForumThread* tp : order) {
                    const okay::ForumThread& t = *tp;
                    const int nc = thComments.count(t.id) ? thComments[t.id] : 0;
                    ImGui::PushID((int)t.id);
                    ImVec2 rp = ImGui::GetCursorScreenPos();
                    const float w = ImGui::GetContentRegionAvail().x;
                    const bool hasBody = !t.body.empty();
                    const float h = hasBody ? 96.0f : 76.0f;
                    ImGui::SetNextItemAllowOverlap();
                    const bool open = ImGui::InvisibleButton("##trow", ImVec2(w, h));
                    const bool hov = ImGui::IsMouseHoveringRect(rp, ImVec2(rp.x + w, rp.y + h)) && ImGui::IsWindowHovered();
                    dl->AddRectFilled(rp, ImVec2(rp.x + w, rp.y + h), ImGui::GetColorU32(hov ? okaytheme::Mix(okaytheme::kRaised, ImVec4(1, 1, 1, 1), 0.04f) : okaytheme::kRaised), 12.0f);
                    // Vote column.
                    ImGui::SetCursorScreenPos(ImVec2(rp.x + 10, rp.y + (h - 62) * 0.5f));
                    voteWidget("thread", t.id, thScores, thMy, false);
                    // Content.
                    const float tx = rp.x + 52, maxW = w - 52 - 16;
                    float y = rp.y + 12;
                    float pw = catPill(dl, ImVec2(tx, y), t.category);
                    ImGui::SetWindowFontScale(1.06f);
                    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(tx + (pw > 0 ? pw + 8 : 0), y), cText, FitText(t.title, maxW - pw - 8).c_str());
                    ImGui::SetWindowFontScale(1.0f);
                    y += 26;
                    if (hasBody) {
                        std::string ex = t.body; for (auto& c : ex) if (c == '\n') c = ' ';
                        DrawRichLine(dl, ImVec2(tx, y), cDim, ex, maxW);
                        y += 22;
                    }
                    // Meta: avatar + author, time, comment count.
                    ImGui::SetCursorScreenPos(ImVec2(tx, y + 2));
                    DrawAvatar("", t.author.empty() ? "?" : t.author, 18.0f);
                    std::string meta = (t.author.empty() ? std::string("unknown") : t.author) + "  \xC2\xB7  " + RelTime(t.when);
                    dl->AddText(ImVec2(tx + 24, y + 3), cDim, meta.c_str());
                    float mx = tx + 24 + ImGui::CalcTextSize(meta.c_str()).x + 18;
                    commentIcon(dl, ImVec2(mx + 6, y + 10), cDim);
                    char cc[24]; std::snprintf(cc, sizeof(cc), "%d", nc);
                    dl->AddText(ImVec2(mx + 18, y + 3), cDim, cc);
                    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if (open) {
                        openThread = t.id; openTitle = t.title; openAuthor = t.author; openBody = t.body; openCat = t.category; openWhen = t.when; fpNext = 0;
                        if (!kFoDemo) { fpCache.clear(); poScores.clear(); poMy.clear(); }   // show the loading state, not stale comments
                    }
                    ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + h + 10));
                    ImGui::Dummy(ImVec2(0, 0));
                    ImGui::PopID();
                }
                if (order.empty()) {
                    float aw = ImGui::GetContentRegionAvail().x;
                    ImGui::Dummy(ImVec2(0, 30));
                    ImVec2 c(ImGui::GetCursorScreenPos().x + aw * 0.5f, ImGui::GetCursorScreenPos().y + 30);
                    dl->AddCircleFilled(c, 30.0f, IM_COL32(255, 255, 255, 12), 40);
                    DrawNavIcon(dl, 9, c, cDim);
                    ImGui::Dummy(ImVec2(0, 70));
                    auto centered = [&](const char* s, bool dim) {
                        float tw = ImGui::CalcTextSize(s).x;
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (aw - tw) * 0.5f));
                        if (dim) ImGui::TextDisabled("%s", s); else ImGui::TextUnformatted(s);
                    };
                    if (g_foFetching && ftCache.empty()) centered("Loading discussions\xE2\x80\xA6", true);
                    else if (!q.empty() || !foCat.empty()) { centered("Nothing matches", false); centered("Try another topic or search.", true); }
                    else if (!fo.Available()) { centered("Sign in to join the conversation", false); centered("Read, post and vote with your OkaySpace account.", true); }
                    else { centered("No discussions yet", false); centered("Start the first one with \"New post\".", true); }
                }
                ImGui::EndChild();
            } else {
                // ---- Thread view ----
                // Fetch the thread's comments + vote maps on a background thread,
                // so opening a post never blocks the frame.
                if (!kFoDemo) {
                    if (tnow >= fpNext && !g_foPostFetching) {
                        g_foPostFetching = true; fpNext = tnow + 6000;
                        if (SDL_Thread* pt = SDL_CreateThread(ForumPostWorker, "okay-fopost", (void*)(intptr_t)openThread))
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
                }
                const bool detailLoading = !kFoDemo && g_foPostHave != openThread;   // first open, not cached yet
                ImDrawList* dl = ImGui::GetWindowDrawList();
                {   // Back pill
                    ImVec2 bp = ImGui::GetCursorScreenPos();
                    const char* bl = "Back to Forums";
                    ImVec2 bs = ImGui::CalcTextSize(bl);
                    if (ImGui::InvisibleButton("##foback", ImVec2(bs.x + 38, bs.y + 12))) { openThread = 0; if (!kFoDemo) fpCache.clear(); }
                    bool h = ImGui::IsItemHovered();
                    dl->AddRectFilled(bp, ImVec2(bp.x + bs.x + 38, bp.y + bs.y + 12), h ? IM_COL32(255, 255, 255, 24) : IM_COL32(255, 255, 255, 10), (bs.y + 12) * 0.5f);
                    dl->AddLine(ImVec2(bp.x + 18, bp.y + 7), ImVec2(bp.x + 13, bp.y + 6 + bs.y * 0.5f), cText, 1.6f);
                    dl->AddLine(ImVec2(bp.x + 13, bp.y + 6 + bs.y * 0.5f), ImVec2(bp.x + 18, bp.y + 5 + bs.y), cText, 1.6f);
                    dl->AddText(ImVec2(bp.x + 26, bp.y + 6), cText, bl);
                    if (h) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::Dummy(ImVec2(0, 4));
                }
                const float composerH = ImGui::GetTextLineHeight() + 18.0f + 16.0f;
                ImGui::BeginChild("##fothread", ImVec2(0, ImGui::GetContentRegionAvail().y - 46.0f - composerH), false);
                {   // Original post card
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, okaytheme::kRaised);
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 14));
                    ImGui::BeginChild("##opcard", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
                    ImVec2 op = ImGui::GetCursorScreenPos();
                    voteWidget("thread", openThread, thScores, thMy, false);
                    ImGui::SetCursorScreenPos(ImVec2(op.x + 42, op.y));
                    ImGui::BeginGroup();
                    ImDrawList* cdl = ImGui::GetWindowDrawList();
                    float pw = catPill(cdl, ImGui::GetCursorScreenPos(), openCat);
                    if (pw > 0) ImGui::Dummy(ImVec2(pw, ImGui::GetTextLineHeight() + 6));
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 8);
                    ImGui::SetWindowFontScale(1.3f);
                    ImGui::TextWrapped("%s", openTitle.c_str());
                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::PopTextWrapPos();
                    DrawAvatar("", openAuthor.empty() ? "?" : openAuthor, 20.0f);
                    ImGui::SameLine(0, 6);
                    ImGui::TextDisabled("%s  \xC2\xB7  %s", openAuthor.empty() ? "unknown" : openAuthor.c_str(), RelTime(openWhen).c_str());
                    if (!openAuthor.empty() && ImGui::IsItemHovered()) {   // author -> public profile
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        ImGui::SetTooltip("View %s's profile", openAuthor.c_str());
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) OpenPublicProfile("", openAuthor, openAuthor);
                    }
                    if (!openBody.empty()) { ImGui::Dummy(ImVec2(0, 4)); RenderChatBody(openBody, ImGui::GetContentRegionAvail().x - 8); }
                    ImGui::EndGroup();
                    ImGui::EndChild();
                    ImGui::PopStyleVar(2);
                    ImGui::PopStyleColor();
                }
                ImGui::Dummy(ImVec2(0, 8));
                ImGui::SetWindowFontScale(0.80f);
                ImGui::TextDisabled("  %d COMMENT%s", (int)fpCache.size(), fpCache.size() == 1 ? "" : "S");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::Dummy(ImVec2(0, 2));
                // Comments: chat-style rows (avatar, name, time, body, inline votes).
                const std::string me = kFoDemo ? std::string(std::getenv("OKAY_UI_DEMO_USER")) : account.CurrentSession().username;
                for (const auto& p : fpCache) {
                    ImGui::PushID((int)p.id);
                    ImVec2 cp = ImGui::GetCursorScreenPos();
                    DrawAvatar("", p.author.empty() ? "?" : p.author, 34.0f);
                    ImGui::SetCursorScreenPos(ImVec2(cp.x + 46, cp.y));
                    ImGui::BeginGroup();
                    const bool mine = !me.empty() && p.author == me;
                    ImVec4 nc = NameColor(p.author);
                    ImGui::TextColored(mine ? accLt : ImVec4(nc.x * 0.35f + 0.65f, nc.y * 0.35f + 0.65f, nc.z * 0.35f + 0.65f, 1), "%s", p.author.empty() ? "unknown" : p.author.c_str());
                    if (!p.author.empty() && ImGui::IsItemHovered()) {   // author -> public profile
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) OpenPublicProfile("", p.author, p.author);
                    }
                    ImGui::SameLine(0, 8); ImGui::TextDisabled("%s", RelTime(p.when).c_str());
                    RenderChatBody(p.body, ImGui::GetContentRegionAvail().x - 8);
                    voteWidget("post", p.id, poScores, poMy, true);
                    ImGui::EndGroup();
                    ImGui::Dummy(ImVec2(0, 8));
                    ImGui::PopID();
                }
                if (fpCache.empty()) {
                    ImGui::Dummy(ImVec2(0, 10));
                    ImGui::TextDisabled(detailLoading ? "   Loading comments\xE2\x80\xA6" : "   No comments yet. Be the first to comment.");
                }
                ImGui::EndChild();
                // ---- Comment composer (chat-style pill; Enter posts) ----
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 9));
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 108);
                if (foReplyFocus) { ImGui::SetKeyboardFocusHere(); foReplyFocus = false; }
                ImGui::BeginDisabled(!canAct);
                bool rp = ImGui::InputTextWithHint("##reply", canAct ? "Add a comment\xE2\x80\xA6" : "Sign in to comment", replyBuf, sizeof(replyBuf), ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine(0, 8);
                bool post = PrimaryButton("Comment", ImVec2(100, ImGui::GetFrameHeight()));
                ImGui::EndDisabled();
                ImGui::PopStyleVar(2);
                if ((post || rp) && replyBuf[0]) {
                    // Post the comment off-thread; clear the box immediately and
                    // refresh the detail shortly after so it appears.
                    if (kFoDemo) {
                        okay::ForumPost np; np.id = 1000 + (long)fpCache.size(); np.author = me; np.body = replyBuf; np.when = (long)std::time(nullptr); fpCache.push_back(np);
                    } else FireForumAction(new FoAction{1, "", openThread, 0, replyBuf, "", ""});
                    replyBuf[0] = '\0'; foErr.clear(); fpNext = tnow + 1000;
                    foReplyFocus = true;   // keep typing (Enter deactivates a single-line input)
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
