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
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/wait.h>
#  include <csignal>
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
    SetState(Up_Checking);
    SetUpMsg("Checking for updates...");
    FetchReleaseNews();   // refresh the What's-new card while we're online anyway
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
    // Metrics match the game engine editor's theme (ApplyTheme) so the launcher and
    // editor read as one product: soft unified rounding, thin frame borders.
    s.WindowRounding    = 3.0f;  s.ChildRounding    = 3.0f;
    s.FrameRounding     = 2.0f;  s.GrabRounding     = 2.0f;
    s.PopupRounding     = 3.0f;  s.TabRounding      = 2.0f;
    s.ScrollbarRounding = 2.0f;
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
        c[ImGuiCol_WindowBg]         = ImVec4(0.106f, 0.153f, 0.212f, 1.00f);   // Steam #1b2736
        c[ImGuiCol_ChildBg]          = ImVec4(0.086f, 0.125f, 0.176f, 0.00f);
        c[ImGuiCol_PopupBg]          = ImVec4(0.078f, 0.114f, 0.161f, 0.99f);
        c[ImGuiCol_Border]           = ImVec4(0.00f, 0.00f, 0.00f, 0.45f);
        c[ImGuiCol_FrameBg]          = ImVec4(0.071f, 0.106f, 0.149f, 1.00f);
        c[ImGuiCol_FrameBgHovered]   = tint(0.110f, 0.18f, 1.00f);
        c[ImGuiCol_FrameBgActive]    = tint(0.140f, 0.25f, 1.00f);
        c[ImGuiCol_TitleBg]          = ImVec4(0.071f, 0.106f, 0.149f, 1.00f);
        c[ImGuiCol_TitleBgActive]    = tint(0.100f, 0.16f, 1.00f);
        c[ImGuiCol_MenuBarBg]        = ImVec4(0.090f, 0.133f, 0.184f, 1.00f);
        c[ImGuiCol_Header]           = ImVec4(accent.x, accent.y, accent.z, 0.42f);
        c[ImGuiCol_HeaderHovered]    = ImVec4(accent.x, accent.y, accent.z, 0.55f);
        c[ImGuiCol_HeaderActive]     = ImVec4(accent.x, accent.y, accent.z, 0.70f);
        c[ImGuiCol_Button]           = ImVec4(0.165f, 0.278f, 0.368f, 1.00f);   // Steam #2a475e
        c[ImGuiCol_ButtonHovered]    = ImVec4(0.216f, 0.361f, 0.467f, 1.00f);
        c[ImGuiCol_ButtonActive]     = ImVec4(accent.x, accent.y, accent.z, 0.85f);
        c[ImGuiCol_CheckMark]        = accentHover;
        c[ImGuiCol_SliderGrab]       = accent;
        c[ImGuiCol_SliderGrabActive] = accentHover;
        c[ImGuiCol_Separator]        = ImVec4(0.00f, 0.00f, 0.00f, 0.50f);
        c[ImGuiCol_SeparatorHovered] = accentDim;
        c[ImGuiCol_SeparatorActive]  = accent;
        c[ImGuiCol_Tab]              = ImVec4(0.078f, 0.114f, 0.161f, 1.00f);
        c[ImGuiCol_TabHovered]       = ImVec4(accent.x, accent.y, accent.z, 0.55f);
        c[ImGuiCol_TabActive]        = tint(0.140f, 0.14f, 1.00f);
        c[ImGuiCol_TabUnfocused]     = ImVec4(0.063f, 0.094f, 0.133f, 1.00f);
        c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.110f, 0.157f, 0.212f, 1.00f);
        c[ImGuiCol_Text]             = ImVec4(0.878f, 0.902f, 0.925f, 1.00f);
        c[ImGuiCol_TextDisabled]     = ImVec4(0.451f, 0.510f, 0.580f, 1.00f);
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
    if (tm) std::strftime(d, sizeof(d), "%B %d, %Y", tm);
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
void RenderChatBody(const std::string& body) {
    bool hasLink  = body.find("http://") != std::string::npos || body.find("https://") != std::string::npos;
    bool hasEmoji = body.find(':') != std::string::npos || body.find("<3") != std::string::npos || body.find("xD") != std::string::npos;
    if (!hasLink && !hasEmoji) { ImGui::TextWrapped("%s", body.c_str()); return; }
    const float avail = ImGui::GetContentRegionAvail().x;
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

SDL_Texture* AvatarTexture(const std::string& url) {
    if (url.empty() || !g_renderer) return nullptr;
    auto it = g_avatarCache.find(url);
    if (it != g_avatarCache.end()) return it->second;
    SDL_Texture* tex = nullptr;
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec) / "okayspace_avatars";
    fs::create_directories(dir, ec);
    char name[32]; std::snprintf(name, sizeof(name), "%zu", std::hash<std::string>{}(url));
    fs::path cache = dir / (std::string(name) + ".img");
    if (!fs::exists(cache, ec)) Download(url, cache.string());   // best-effort fetch
    if (fs::exists(cache, ec)) {
        int w = 0, h = 0, ch = 0;
        unsigned char* px = stbi_load(cache.string().c_str(), &w, &h, &ch, 4);
        if (px) {
            SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(px, w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
            if (s) { tex = SDL_CreateTextureFromSurface(g_renderer, s); SDL_FreeSurface(s); }
            stbi_image_free(px);
        }
    }
    g_avatarCache[url] = tex;
    return tex;
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
    for (const auto& p : g_favorites)    f << "fav=" << p << "\n";
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
void DrawChatWindow(okay::account::AccountService& account) {
    if (!g_chatWinOpen) return;
    static std::vector<okay::Friend> friends; static Uint32 friendsNext = 0;
    static std::string selUid, selName, selAvatar;
    static std::vector<okay::DirectMessage> log; static Uint32 logNext = 0;
    static std::map<long,long> likes; static std::map<long,bool> myLikes;
    static long editId = 0; static char input[512] = ""; static int lastShown = -1;

    if (g_chatReqPending) {                     // a "Message" button asked to open a convo
        selUid = g_chatReqUid; selName = g_chatReqName; selAvatar = g_chatReqAvatar;
        log.clear(); logNext = 0; editId = 0; input[0] = '\0'; lastShown = -1;
        g_chatReqPending = false;
        ImGui::SetNextWindowFocus();
    }
    ImGui::SetNextWindowSize(ImVec2(660, 460), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Chat", &g_chatWinOpen)) {
        if (!account.IsLoggedIn()) {
            ImGui::TextDisabled("Sign in to chat with friends.");
            ImGui::End(); return;
        }
        okay::Messages dm(account);
        Uint32 now = SDL_GetTicks();
        if (now >= friendsNext) { friends = okay::Friends(account).List(); friendsNext = now + 15000; }

        // Left: conversation list (the tabs).
        ImGui::BeginChild("##chatlist", ImVec2(176, 0), true);
        ImGui::TextDisabled("CONVERSATIONS");
        ImGui::Separator();
        int accepted = 0;
        for (auto& f : friends) {
            if (f.status != "accepted") continue;
            ++accepted;
            std::string nm = f.display.empty() ? f.username : f.display;
            ImVec2 rp = ImGui::GetCursorScreenPos();
            ImU32 dot = f.online ? IM_COL32(120,200,110,255) : IM_COL32(110,118,130,255);
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(rp.x+5, rp.y+9), 4.0f, dot, 12);
            ImGui::Dummy(ImVec2(12,0)); ImGui::SameLine();
            if (ImGui::Selectable((nm + "##" + f.id).c_str(), f.id == selUid)) {
                selUid = f.id; selName = nm; selAvatar = f.avatar;
                log.clear(); logNext = 0; editId = 0; input[0] = '\0'; lastShown = -1;
            }
        }
        if (accepted == 0) ImGui::TextDisabled("Add friends to start chatting.");
        ImGui::EndChild();
        ImGui::SameLine();

        // Right: the selected conversation.
        ImGui::BeginChild("##chatconv", ImVec2(0, 0), false);
        if (selUid.empty()) {
            ImGui::Dummy(ImVec2(0, 30));
            ImGui::TextDisabled("  Pick a conversation on the left.");
        } else {
            DrawAvatar(selAvatar, selName, 30.0f);
            ImGui::SameLine(); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
            ImGui::TextUnformatted(selName.c_str());
            ImGui::Separator();
            if (now >= logNext) { log = dm.With(selUid); likes = dm.Likes(); myLikes = dm.MyLikes(); logNext = now + 4000; }

            ImGui::BeginChild("##clog", ImVec2(0, ImGui::GetContentRegionAvail().y - 40), false);
            float avW = ImGui::GetContentRegionAvail().x;
            long prevDay = 0;
            for (const auto& m : log) {
                long day = DayKey(m.when);
                if (day != 0 && day != prevDay) { ImGui::SetCursorPosX(0); ChatDateSeparator(m.when); prevDay = day; }
                float bubbleMax = avW * 0.66f;
                ImVec2 ts = ImGui::CalcTextSize(m.body.c_str(), nullptr, false, bubbleMax);
                float bw = ts.x + 20, bh = ts.y + 14;
                float x0 = m.mine ? (avW - bw - 8) : 8; if (x0 < 0) x0 = 0;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x0);
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImU32 bg = m.mine ? ImGui::GetColorU32(kAccentDim) : IM_COL32(48, 58, 80, 255);
                ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + bw, p.y + bh), bg, 9.0f);
                ImGui::SetCursorScreenPos(ImVec2(p.x + 10, p.y + 6));
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + bubbleMax);
                RenderChatBody(m.body);
                ImGui::PopTextWrapPos();
                // meta row: time, (edited), ❤ like, edit
                char w[24] = ""; if (m.when > 0) { std::time_t tt=(std::time_t)m.when; std::tm* tm=std::localtime(&tt); if(tm) std::strftime(w,sizeof(w),"%H:%M",tm); }
                long lc = likes.count(m.id) ? likes[m.id] : 0; bool liked = myLikes.count(m.id) != 0;
                ImGui::PushID((int)m.id);
                ImGui::SetCursorPosX(m.mine ? (avW - 150) : 8);
                ImGui::BeginGroup();
                if (w[0]) { ImGui::TextDisabled("%s%s", w, m.edited ? " (edited)" : ""); ImGui::SameLine(0, 10); }
                ImVec2 hp2 = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("like", ImVec2(18, 16))) { dm.Like(m.id, !liked); logNext = 0; }
                DrawHeart(ImGui::GetWindowDrawList(), ImVec2(hp2.x+8, hp2.y+8), 6.0f,
                          liked ? IM_COL32(232,72,92,255) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
                if (lc > 0) { ImGui::SameLine(0, 2); ImGui::TextDisabled("%ld", lc); }
                if (m.mine) { ImGui::SameLine(0, 10); if (ImGui::SmallButton("edit")) { editId = m.id; std::snprintf(input, sizeof(input), "%s", m.body.c_str()); } }
                ImGui::EndGroup();
                ImGui::PopID();
                ImGui::SetCursorPosX(0); ImGui::Dummy(ImVec2(0, 2));
            }
            if (log.empty()) ImGui::TextDisabled("No messages yet. Say hi!");
            if ((int)log.size() != lastShown) { ImGui::SetScrollHereY(1.0f); lastShown = (int)log.size(); }
            ImGui::EndChild();

            if (editId) { ImGui::TextDisabled("Editing a message"); ImGui::SameLine();
                if (ImGui::SmallButton("cancel edit")) { editId = 0; input[0] = '\0'; } }
            ImVec2 inSz(ImGui::GetContentRegionAvail().x - 60 - 36, ImGui::GetTextLineHeight() + 14);
            EmojiPicker("cwemoji", input, sizeof(input)); ImGui::SameLine();
            bool send = ImGui::InputTextMultiline("##cwin", input, sizeof(input), inSz,
                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, kAccentDim);
            if ((ImGui::Button(editId ? "Save" : "Send", ImVec2(52, inSz.y)) || send) && input[0]) {
                if (editId) { if (dm.Edit(editId, input)) { editId = 0; input[0] = '\0'; logNext = 0; } }
                else if (dm.Send(selUid, input)) { input[0] = '\0'; logNext = 0; }
                ImGui::SetKeyboardFocusHere(-1);
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
    }
    ImGui::End();
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

    // Show the bundled version and DON'T phone home on startup: the published build
    // on GitHub can lag this one, so an auto-check used to download an OLDER engine
    // and mislead with "up to date". Updates are now opt-in via the button below.
    SDL_AtomicSet(&g_upState, (int)Up_UpToDate);
    g_upMutex = SDL_CreateMutex();
    g_dlMutex = SDL_CreateMutex();
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
    g_renderer = renderer;   // lets GameCover() upload cover-art textures

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
    accountPtr->VerifySession();

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
        // Poll incoming friend requests in the background for the nav badge,
        // and toast when a new one arrives.
        {
            static Uint32 nextReqPoll = 0;
            static int lastReqSeen = -1;
            Uint32 nowReq = SDL_GetTicks();
            if (account.IsLoggedIn() && nowReq >= nextReqPoll) {
                g_friendRequests = okay::Friends(account).PendingRequests();
                if (lastReqSeen >= 0 && g_friendRequests > lastReqSeen && tab != 6)
                    Toast(g_friendRequests == 1 ? "New friend request" : "New friend requests", 1);
                lastReqSeen = g_friendRequests;
                // Unread DMs for the nav badge (+ toast on new ones).
                long newest = g_lastDmId;
                int unread = okay::Messages(account).UnreadSince(g_lastDmId, newest);
                g_newestDmId = newest;
                if (unread > g_dmUnread && tab != 6) Toast("New message", 1);
                g_dmUnread = unread;
                nextReqPoll = nowReq + 30000;
            } else if (!account.IsLoggedIn()) {
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

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Keyboard shortcuts: 1-5 switch tabs (when not typing in a field);
        // Ctrl+F focuses the current tab's search box; Esc clears every search.
        if (!ImGui::GetIO().WantTextInput) {
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
        ImGui::Begin("##launcher", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        // (Removed the soft accent glow gradient across the top — a flat, Steam-style
        // dark surface reads more like a real app and less "designed".)

        // ---- Left nav ----
        ImGui::BeginChild("nav", ImVec2(224, 0), true);
        ImGui::Dummy(ImVec2(0, 6));
        {   // App header: drawn OkaySpace logo mark (ringed planet + star on a
            // navy tile) + product name / version.
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 8);
            ImVec2 lp = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float S = 34.0f;
            ImVec2 br(lp.x + S, lp.y + S);
            dl->AddRectFilledMultiColor(lp, br,
                IM_COL32(20, 28, 48, 255), IM_COL32(20, 28, 48, 255),
                IM_COL32(30, 40, 66, 255), IM_COL32(30, 40, 66, 255));
            dl->AddRect(lp, br, IM_COL32(255, 255, 255, 24), 9.0f, 0, 1.0f);
            // Clip the glyph to the rounded tile.
            dl->PushClipRect(lp, br, true);
            ImVec2 c(lp.x + S * 0.46f, lp.y + S * 0.52f);
            ImU32 planet = ImGui::GetColorU32(kAccent);
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
            ImGui::Dummy(ImVec2(S + 8, S));
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Text("OkaySpace");
            ImGui::TextDisabled("v%s", OKAY_ENGINE_VERSION);
            ImGui::EndGroup();
        }
        // Scrollable nav items, sized to leave room for the pinned update footer
        // below — so on a short window the items scroll instead of overlapping it.
        const float kNavFootH = 92.0f;
        ImGui::BeginChild("navitems", ImVec2(0, ImGui::GetContentRegionAvail().y - kNavFootH), false);
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
            if (nv.section) {
                ImGui::Dummy(ImVec2(0, 8));
                ImGui::TextDisabled("  %s", nv.section);
                ImGui::Dummy(ImVec2(0, 2));
            }
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
            // Play shows how many games are installed, right-aligned in the row.
            if (i == 1 && !scenes.empty()) {
                char nb[16]; std::snprintf(nb, sizeof(nb), "%d", (int)scenes.size());
                ImVec2 ts = ImGui::CalcTextSize(nb);
                ImVec2 bp(mx.x - ts.x - 18.0f, (mn.y + mx.y) * 0.5f - ts.y * 0.5f);
                dl->AddRectFilled(ImVec2(bp.x - 6, bp.y - 2), ImVec2(bp.x + ts.x + 6, bp.y + ts.y + 2),
                                  ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.22f)), 8.0f);
                dl->AddText(bp, ImGui::GetColorU32(ImGuiCol_Text), nb);
            }
            // Friends shows a red badge: pending requests + unread DMs.
            if (i == 6 && (g_friendRequests + g_dmUnread) > 0) {
                char nb[16]; std::snprintf(nb, sizeof(nb), "%d", g_friendRequests + g_dmUnread);
                ImVec2 ts = ImGui::CalcTextSize(nb);
                ImVec2 bp(mx.x - ts.x - 18.0f, (mn.y + mx.y) * 0.5f - ts.y * 0.5f);
                dl->AddRectFilled(ImVec2(bp.x - 6, bp.y - 2), ImVec2(bp.x + ts.x + 6, bp.y + ts.y + 2),
                                  IM_COL32(210, 72, 72, 255), 8.0f);
                dl->AddText(bp, IM_COL32(255, 255, 255, 255), nb);
            }
            // Downloads shows the number of active (in-flight) downloads.
            if (i == 10) {
                int act = ActiveDownloads();
                if (act > 0) {
                    char nb[16]; std::snprintf(nb, sizeof(nb), "%d", act);
                    ImVec2 ts = ImGui::CalcTextSize(nb);
                    ImVec2 bp(mx.x - ts.x - 18.0f, (mn.y + mx.y) * 0.5f - ts.y * 0.5f);
                    dl->AddRectFilled(ImVec2(bp.x - 6, bp.y - 2), ImVec2(bp.x + ts.x + 6, bp.y + ts.y + 2),
                                      ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.30f)), 8.0f);
                    dl->AddText(bp, ImGui::GetColorU32(ImGuiCol_Text), nb);
                }
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
        ImGui::EndChild();   // end scrollable nav items

        // ---- Update status (sits below the scrolling nav items) ----
        UpState st = GetState();
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
        std::string upMsg = GetUpMsg();
        // Before any check runs, at least show the installed version here.
        ImGui::TextDisabled("%s", upMsg.empty() ? "v" OKAY_ENGINE_VERSION : upMsg.c_str());
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

            // Hero card: gradient panel with the one primary action.
            ImGui::BeginChild("hero", ImVec2(0, 118), true, ImGuiWindowFlags_NoScrollbar);
            {
                ImVec2 mn = ImGui::GetWindowPos(); ImVec2 sz = ImGui::GetWindowSize();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImU32 g0 = ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.22f));
                ImU32 g1 = ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.03f));
                dl->AddRectFilledMultiColor(mn, ImVec2(mn.x + sz.x, mn.y + sz.y), g0, g1, g1, g0);
                // Drawn isometric-cube logo mark, faded into the card's right side.
                float S = 84.0f;
                ImVec2 ctr(mn.x + sz.x - S * 0.75f, mn.y + sz.y * 0.5f);
                float hw = S * 0.42f, hh = S * 0.24f, vh = S * 0.34f;
                ImVec2 top(ctr.x, ctr.y - hh - vh * 0.5f), left(ctr.x - hw, ctr.y - vh * 0.5f),
                       right(ctr.x + hw, ctr.y - vh * 0.5f), mid(ctr.x, ctr.y + hh - vh * 0.5f);
                ImVec2 lb(left.x, left.y + vh), rb(right.x, right.y + vh), mb(mid.x, mid.y + vh);
                dl->AddQuadFilled(top, right, mid, left,
                    ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.30f)));
                dl->AddQuadFilled(left, mid, mb, lb,
                    ImGui::GetColorU32(ImVec4(kAccent.x * 0.55f, kAccent.y * 0.55f, kAccent.z * 0.55f, 0.30f)));
                dl->AddQuadFilled(mid, right, rb, mb,
                    ImGui::GetColorU32(ImVec4(kAccent.x * 0.75f, kAccent.y * 0.75f, kAccent.z * 0.75f, 0.30f)));
                // Version chip, top-right corner.
                const char* vlbl = "v" OKAY_ENGINE_VERSION;
                ImVec2 vts = ImGui::CalcTextSize(vlbl);
                ImVec2 vp2(mn.x + sz.x - vts.x - 22.0f, mn.y + 10.0f);
                dl->AddRectFilled(ImVec2(vp2.x - 7, vp2.y - 3), ImVec2(vp2.x + vts.x + 7, vp2.y + vts.y + 3),
                                  ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.25f)),
                                  (vts.y + 6.0f) * 0.5f);
                dl->AddText(vp2, ImGui::GetColorU32(ImVec4(0.92f, 0.95f, 1.0f, 0.95f)), vlbl);
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
                if (hov) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("Create '%s' from the %s template and open it",
                                      npName[0] ? npName : "MyGame", kTpl[i].name);
                }
                if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    // Compose <Location>/<Name>; the editor creates the folder,
                    // Assets/ and the starting scene (uniquifying if it's taken).
                    std::string nm = npName[0] ? npName : "MyGame";
                    for (char& ch : nm)   // keep the folder name filesystem-safe
                        if (ch == '/' || ch == '\\' || ch == '"' || ch == ':') ch = '-';
                    std::string root = (fs::path(npLoc[0] ? npLoc : ".") / nm).string();
                    LaunchEditor(editor, kTpl[i].name, root);
                    Toast(std::string("Creating ") + nm + " (" + kTpl[i].name + ")");
                }
                ImGui::EndChild();
                ImGui::PopID();
            }
            if (editor.empty()) ImGui::PopStyleVar();

            // ---- Your projects: everything in the Location folder, one line each ----
            ImGui::Dummy(ImVec2(0, 12));
            ImGui::SeparatorText("Your projects");
            static std::vector<std::pair<std::string, std::string>> s_projects;  // name -> scene path
            static std::string s_projScanned = "\x01";   // never equals a real path -> first scan
            if (s_projScanned != std::string(npLoc)) {
                s_projScanned = npLoc;
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
                    if (!scene.empty())
                        s_projects.push_back({e.path().filename().string(), scene});
                }
                std::sort(s_projects.begin(), s_projects.end());
            }
            if (ImGui::SmallButton("Refresh##projs")) s_projScanned = "\x01";
            if (s_projects.empty()) {
                ImGui::TextDisabled("No projects in this folder yet — create one above.");
            }
            for (auto& pr : s_projects) {
                ImGui::PushID(pr.second.c_str());
                ImGui::BeginChild("proj", ImVec2(0, 56), true, ImGuiWindowFlags_NoScrollbar);
                HoverRing();
                ImGui::SetCursorPos(ImVec2(10, 9));
                IconTile(pr.first, 38.0f);
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
                ImGui::TextUnformatted(pr.first.c_str());
                std::string pago = ModifiedAgo(fs::path(pr.second));
                if (pago.empty()) ImGui::TextDisabled("%s", pr.second.c_str());
                else ImGui::TextDisabled("edited %s", pago.c_str());
                ImGui::EndGroup();
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 92);
                ImGui::SetCursorPosY(10.0f);
                ImGui::BeginDisabled(editor.empty());
                if (PrimaryButton("Open", ImVec2(84, 36))) {
                    LaunchEditor(editor, "", "", pr.second);
                    Toast(std::string("Opening ") + pr.first);
                }
                ImGui::EndDisabled();
                // Double-click the card opens it too; right-click for file actions.
                if (!editor.empty() && ImGui::IsWindowHovered() &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    LaunchEditor(editor, "", "", pr.second);
                    Toast(std::string("Opening ") + pr.first);
                }
                if (ImGui::BeginPopupContextWindow("projctx", ImGuiPopupFlags_MouseButtonRight)) {
                    if (!editor.empty() && ImGui::MenuItem("Open in Editor")) {
                        LaunchEditor(editor, "", "", pr.second);
                        Toast(std::string("Opening ") + pr.first);
                    }
                    if (ImGui::MenuItem("Show in Explorer"))
                        OpenExternal(fs::path(pr.second).parent_path().parent_path().string());
                    if (ImGui::MenuItem("Copy Path")) ImGui::SetClipboardText(pr.second.c_str());
                    ImGui::EndPopup();
                }
                ImGui::EndChild();
                ImGui::PopID();
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
                // Toolbar: search + sort + view toggle + folder, all on one row
                // (the search field flexes to fill whatever is left).
                if (focusSearch) { ImGui::SetKeyboardFocusHere(); focusSearch = false; }
                {
                    const float sortW = 156.0f, viewW = 100.0f, foldW = 108.0f;
                    float sp = ImGui::GetStyle().ItemSpacing.x;
                    float searchW = ImGui::GetContentRegionAvail().x - sortW - viewW - foldW - sp * 3.0f;
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
                if (ImGui::Button("Folder", ImVec2(108, 0))) OpenExternal(g_exeDir);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open the games folder");
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
                // Load my customization once so the card below previews it.
                if (!acctProfileLoaded && account.IsOnline()) {
                    okay::Friend me = okay::Friends(account).MyProfile();
                    acctAccent = me.accent;
                    std::snprintf(acctStatus,   sizeof(acctStatus),   "%s", me.customStatus.c_str());
                    std::snprintf(acctFavGame,  sizeof(acctFavGame),  "%s", me.favoriteGame.c_str());
                    std::snprintf(acctLocation, sizeof(acctLocation), "%s", me.location.c_str());
                    acctProfileLoaded = true;
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
                if (!s_avFetched && account.IsOnline()) { s_myAvatar = okay::Friends(account).MyAvatar(); s_avFetched = true; }
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
                    if (!codeFetched && account.IsOnline()) { myCode = okay::Friends(account).MyCode(); codeFetched = true; }
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
                    // Load current customization once.
                    if (!acctProfileLoaded && account.IsOnline()) {
                        okay::Friend me = okay::Friends(account).MyProfile();
                        acctAccent = me.accent;
                        std::snprintf(acctStatus,   sizeof(acctStatus),   "%s", me.customStatus.c_str());
                        std::snprintf(acctFavGame,  sizeof(acctFavGame),  "%s", me.favoriteGame.c_str());
                        std::snprintf(acctLocation, sizeof(acctLocation), "%s", me.location.c_str());
                        acctProfileLoaded = true;
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

                // Segmented Sign in / Create toggle (two half-width buttons).
                auto seg = [&](const char* label, bool active, float w) {
                    if (active) ImGui::PushStyleColor(ImGuiCol_Button, kAccentDim);
                    bool c = ImGui::Button(label, ImVec2(w, 0));
                    if (active) ImGui::PopStyleColor();
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
                static Uint32 fNextRefresh = 0, fNextBeat = 0;
                static bool fProfiled = false;
                // Broadcast presence according to the chosen status (Online/Away/Invisible).
                auto applyStatus = [&]() {
                    if (g_onlineStatus == 2) fr.GoOffline();
                    else fr.MarkPresence(g_onlineStatus == 1 ? "Away" : "In OkaySpace");
                };
                if (!fProfiled) {
                    fr.EnsureProfile();
                    fCode = fr.MyCode();
                    applyStatus();
                    fProfiled = true; fNextRefresh = 0;
                }
                Uint32 now = SDL_GetTicks();
                if (now >= fNextBeat)    { applyStatus(); fNextBeat = now + 60000; }
                if (now >= fNextRefresh) { fCache = fr.List(); fFeed = fr.Feed(14); fNextRefresh = now + 10000; }

                // --- Status selector (Online / Away / Invisible) ---
                ImGui::TextDisabled("Status:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(140);
                if (ImGui::Combo("##status", &g_onlineStatus, "Online\0Away\0Invisible\0")) {
                    applyStatus(); SavePrefs();
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
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.78f, 1.0f, 1.0f));
                    ImGui::TextUnformatted(fCode.c_str());
                    ImGui::PopStyleColor();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Copy")) { ImGui::SetClipboardText(fCode.c_str()); Toast("Friend code copied", 1); }
                }
                if (!fErr.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f,0.45f,0.45f,1));
                                     ImGui::TextWrapped("%s", fErr.c_str()); ImGui::PopStyleColor(); }

                // Split into requests / online / offline
                std::vector<const okay::Friend*> reqs, online, offline;
                for (const auto& f : fCache) {
                    if (f.status != "accepted" && f.incoming) reqs.push_back(&f);
                    else if (f.status != "accepted")          offline.push_back(&f); // outgoing pending
                    else if (f.online)                        online.push_back(&f);
                    else                                      offline.push_back(&f);
                }
                ImGui::Spacing();
                ImGui::BeginChild("##friendlist", ImVec2(0, ImGui::GetContentRegionAvail().y - 46.0f), false);
                auto drawFriend = [&](const okay::Friend* f, bool request) {
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
                    DrawAvatar(f->avatar, f->display.empty() ? f->username : f->display, 32.0f);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 dot = f->online ? IM_COL32(120, 200, 110, 255) : IM_COL32(120, 128, 140, 255);
                    dl->AddCircleFilled(ImVec2(ap.x + 28, ap.y + 28), 5.5f, dot, 16);
                    dl->AddCircle(ImVec2(ap.x + 28, ap.y + 28), 5.5f, IM_COL32(20, 28, 48, 255), 16, 2.0f);
                    ImGui::SameLine(); ImGui::SetCursorPosX(46);
                    ImGui::BeginGroup();
                    ImGui::TextUnformatted(f->display.empty() ? f->username.c_str() : f->display.c_str());
                    if (!f->display.empty() && f->display != f->username) {
                        ImGui::SameLine(0, 6);
                        ImGui::TextDisabled("@%s", f->username.c_str());
                    }
                    bool playing = f->online && !f->playing.empty();
                    std::string sub = request ? "wants to be your friend"
                                   : f->status != "accepted" ? "request sent"
                                   : playing ? f->playing
                                   : f->online ? "Online" : "Offline";
                    // Color the presence line: accent when playing, green online,
                    // muted grey offline/pending.
                    ImVec4 sc = playing ? kAccent
                              : (!request && f->online) ? ImVec4(0.55f,0.9f,0.6f,1)
                                                        : ImVec4(0.60f,0.63f,0.70f,1);
                    ImGui::TextColored(sc, "%s", sub.c_str());
                    ImGui::EndGroup();
                    bool accepted = !request && f->status == "accepted";
                    bool joinable = accepted && f->online && !f->sessionCode.empty();
                    float btnW = request ? 170.0f : accepted ? (joinable ? 240.0f : 170.0f) : 86.0f;
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - btnW);
                    if (request) {
                        if (ImGui::Button("Accept")) { fr.Accept(f->rowId); fNextRefresh = 0; }
                        ImGui::SameLine();
                        if (ImGui::Button("Decline")) { fr.Remove(f->rowId); fNextRefresh = 0; }
                    } else {
                        if (accepted) {
                            if (ImGui::Button("Message")) {
                                OpenChatWith(f->id, f->display.empty() ? f->username : f->display, f->avatar);
                            }
                            ImGui::SameLine();
                        }
                        if (joinable) {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.44f, 0.26f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.56f, 0.33f, 1.0f));
                            if (ImGui::Button("Join")) {
                                ImGui::SetClipboardText(f->sessionCode.c_str());
                                Toast("Session code " + f->sessionCode + " copied - open the game and Join with it", 2);
                            }
                            ImGui::PopStyleColor(2);
                            ImGui::SameLine();
                        }
                        if (ImGui::SmallButton("Remove")) { fr.Remove(f->rowId); fNextRefresh = 0; }
                    }
                    ImGui::EndChild();
                    ImGui::PopID();
                };
                if (!reqs.empty())    { ImGui::SeparatorText("Friend requests"); for (auto* f : reqs)    drawFriend(f, true);  }
                if (!online.empty())  { ImGui::SeparatorText("Online");          for (auto* f : online)  drawFriend(f, false); }
                if (!offline.empty()) { ImGui::SeparatorText("Offline");         for (auto* f : offline) drawFriend(f, false); }

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
            if (now >= wNextRefresh) { wCache = ws.List(); wRatings = ws.Ratings(); wNextRefresh = now + 15000; }

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
                // Search + kind filter + sort.
                static char wSearch[96] = "";
                static int  wFilterKind = 0;   // 0 All, 1 Games, 2 Templates
                static int  wSort = 0;         // 0 Newest, 1 Most downloaded, 2 Highest rated
                static bool wWishOnly = false;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150 - 170 - 108);
                ImGui::InputTextWithHint("##mktsearch", "Search the marketplace...", wSearch, sizeof(wSearch));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(142);
                ImGui::Combo("##mktkind", &wFilterKind, "All kinds\0Games\0Templates\0");
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
                const char* kindKey[] = { "", "game", "template" };
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
                if (now >= svNextList) { svList = sv.MyServers(); svNextList = now + 20000; }

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
                    for (const auto& s : svList) {
                        bool sel = (s.id == svSel);
                        if (ImGui::Selectable((s.name + "##" + std::to_string(s.id)).c_str(), sel)) {
                            svSel = s.id; svSelName = s.name; svChan = 0; svNextChan = 0; svMsgs.clear();
                        }
                    }
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
                        if (now >= svNextChan) { svChannels = sv.Channels(svSel); svNextChan = now + 15000;
                            if (svChan == 0 && !svChannels.empty()) { svChan = svChannels[0].id; svNextMsg = 0; } }
                        // Am I this server's owner? (drives the management controls)
                        std::string svMyUid = sv.MyUid();
                        bool svIAmOwner = false;
                        for (const auto& s : svList) if (s.id == svSel && !s.owner.empty() && s.owner == svMyUid) svIAmOwner = true;
                        ImGui::TextDisabled("%s", svSelName.c_str());
                        if (svIAmOwner) { ImGui::SameLine(ImGui::GetContentRegionAvail().x - 24);
                            if (ImGui::SmallButton("...")) { std::snprintf(svRenSrvBuf,sizeof(svRenSrvBuf),"%s",svSelName.c_str()); svSettingsOpen = true; } }
                        ImGui::Separator();
                        for (const auto& c : svChannels) {
                            bool sel = (c.id == svChan);
                            if (ImGui::Selectable(("# " + c.name + "##" + std::to_string(c.id)).c_str(), sel)) { svChan = c.id; svNextMsg = 0; }
                            // Owner: right-click a channel to rename or delete it.
                            if (svIAmOwner && ImGui::BeginPopupContextItem(("chanctx" + std::to_string(c.id)).c_str())) {
                                if (ImGui::MenuItem("Rename channel")) {
                                    svRenChanId = c.id; std::snprintf(svRenChanBuf, sizeof(svRenChanBuf), "%s", c.name.c_str()); svRenChanOpen = true;
                                }
                                if (svChannels.size() > 1 && ImGui::MenuItem("Delete channel")) {
                                    if (sv.DeleteChannel(c.id)) { if (svChan == c.id) { svChan = 0; } svNextChan = 0; Toast("Channel deleted", 1); }
                                }
                                ImGui::EndPopup();
                            }
                        }
                        if (svIAmOwner) ImGui::TextDisabled("(right-click a channel to rename/delete)");
                        ImGui::Separator();
                        ImGui::SetNextItemWidth(-1);
                        bool nc = ImGui::InputTextWithHint("##svnewchan", "new channel", svNewChan, sizeof(svNewChan), ImGuiInputTextFlags_EnterReturnsTrue);
                        if ((ImGui::SmallButton("Add channel") || nc) && svNewChan[0]) {
                            if (sv.CreateChannel(svSel, svNewChan)) { svNewChan[0] = '\0'; svNextChan = 0; }
                        }
                        if (!svIAmOwner) {
                            if (ImGui::SmallButton("Leave server")) {
                                if (sv.Leave(svSel)) { svSel = 0; svChan = 0; svNextList = 0; Toast("Left server", 1); }
                            }
                        }
                        if (now >= svNextMem) { svMembers = sv.Members(svSel); svNextMem = now + 20000; }
                        ImGui::Dummy(ImVec2(0, 6));
                        ImGui::TextDisabled("MEMBERS - %d", (int)svMembers.size());
                        ImGui::Separator();
                        for (const auto& m : svMembers) {
                            ImVec2 mp = ImGui::GetCursorScreenPos();
                            ImU32 dot = m.online ? IM_COL32(120, 200, 110, 255) : IM_COL32(110, 118, 130, 255);
                            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(mp.x + 5, mp.y + 8), 4.0f, dot, 12);
                            ImGui::Dummy(ImVec2(14, 0)); ImGui::SameLine();
                            if (m.role == "owner") {
                                ImGui::TextUnformatted(m.name.empty() ? "member" : m.name.c_str());
                                ImGui::SameLine(0, 4); ImGui::TextColored(ImVec4(0.95f,0.80f,0.35f,1), "(owner)");
                            } else {
                                ImGui::TextColored(m.online ? ImVec4(0.9f,0.92f,0.96f,1) : ImVec4(0.6f,0.63f,0.7f,1),
                                                   "%s", m.name.empty() ? "member" : m.name.c_str());
                            }
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
                        if (now >= svNextMsg) { svMsgs = sv.Messages(svChan); svLikes = sv.Likes(); svMyLikes = sv.MyLikes(); svNextMsg = now + 4000; }
                        std::string svMe = account.CurrentSession().username;
                        // Inline per-message actions: ❤ like + count, "(edited)", and
                        // an Edit affordance on my own messages (loads it into the box).
                        auto msgActions = [&](const okay::ChannelMessage& m) {
                            long lc = svLikes.count(m.id) ? svLikes[m.id] : 0;
                            bool liked = svMyLikes.count(m.id) != 0;
                            ImGui::PushID((int)m.id);
                            ImVec2 hp2 = ImGui::GetCursorScreenPos();
                            if (ImGui::InvisibleButton("like", ImVec2(18, 16))) { sv.Like(m.id, !liked); svNextMsg = 0; }
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
                            if (svEditId) { if (sv.EditMessage(svEditId, svMsg)) { svEditId = 0; svMsg[0] = '\0'; svNextMsg = 0; } }
                            else if (sv.Post(svChan, svMsg)) { svMsg[0] = '\0'; svNextMsg = 0; }
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
                        if (sv.RenameChannel(svRenChanId, svRenChanBuf)) { svNextChan = 0; Toast("Channel renamed", 1); }
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
            if (tnow >= foNext) {
                ftCache = fo.Threads();
                thScores = fo.Scores("thread"); thMy = fo.MyVotes("thread"); thComments = fo.CommentCounts();
                foNext = tnow + 20000;
            }

            // A compact up/score/down vote widget (Reddit-style). Toggles like
            // Reddit: clicking your current direction clears the vote. Returns true
            // if a vote changed (so the caller can refresh).
            auto voteWidget = [&](const char* kind, long id, long baseScore, int myVote) -> bool {
                bool changed = false;
                ImGui::BeginGroup();
                ImGui::PushID((int)id); ImGui::PushID(kind);
                ImU32 up  = myVote == 1  ? IM_COL32(236,128,64,255)  : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                ImU32 dn  = myVote == -1 ? IM_COL32(110,130,240,255) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                ImGui::PushStyleColor(ImGuiCol_Text, up);
                bool canVote = fo.Available();
                if (ImGui::ArrowButton("u", ImGuiDir_Up) && canVote) { fo.Vote(kind, id, myVote == 1 ? 0 : 1); changed = true; }
                ImGui::PopStyleColor();
                // Score, centered under the arrow, tinted by my vote.
                char sc[16]; std::snprintf(sc, sizeof(sc), "%ld", baseScore);
                float aw = ImGui::GetItemRectSize().x, tw = ImGui::CalcTextSize(sc).x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (aw - tw) * 0.5f);
                ImVec4 scCol = myVote==1?ImVec4(0.93f,0.5f,0.25f,1):myVote==-1?ImVec4(0.43f,0.51f,0.94f,1):ImVec4(0.8f,0.82f,0.88f,1);
                ImGui::TextColored(scCol, "%s", sc);
                ImGui::PushStyleColor(ImGuiCol_Text, dn);
                if (ImGui::ArrowButton("d", ImGuiDir_Down) && canVote) { fo.Vote(kind, id, myVote == -1 ? 0 : -1); changed = true; }
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
                            if (fo.StartThread(newThread, newBody, newCat, &foErr)) {
                                newThread[0] = newBody[0] = newCat[0] = '\0'; foErr.clear();
                                foShowNew = false; foNext = 0;
                            }
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
                    long sc = scoreOf(t.id); int mv = thMy.count(t.id) ? thMy[t.id] : 0;
                    int nc = thComments.count(t.id) ? thComments[t.id] : 0;
                    ImGui::PushID((int)t.id);
                    ImGui::BeginChild("##trow", ImVec2(0, 66), true);
                    HoverRing();
                    ImGui::SetCursorPos(ImVec2(6, 8));
                    if (voteWidget("thread", t.id, sc, mv)) foNext = 0;
                    ImGui::SameLine(); ImGui::SetCursorPosX(52);
                    ImGui::BeginGroup();
                    ImGui::SetCursorPosY(10);
                    if (!t.category.empty()) {
                        ImVec2 bp = ImGui::GetCursorScreenPos(); ImVec2 ks = ImGui::CalcTextSize(t.category.c_str());
                        ImGui::GetWindowDrawList()->AddRectFilled(bp, ImVec2(bp.x+ks.x+12, bp.y+ks.y+4), ImGui::GetColorU32(kAccentDim), (ks.y+4)*0.5f);
                        ImGui::GetWindowDrawList()->AddText(ImVec2(bp.x+6, bp.y+2), IM_COL32(235,238,245,255), t.category.c_str());
                        ImGui::Dummy(ImVec2(ks.x+12, ks.y+4)); ImGui::SameLine(0, 6);
                    }
                    ImGui::TextUnformatted(t.title.c_str());
                    std::string when = RelTime(t.when);
                    ImGui::TextDisabled("by %s%s%s  -  %d comment%s",
                        t.author.empty()?"unknown":t.author.c_str(), when.empty()?"":"  -  ", when.c_str(),
                        nc, nc==1?"":"s");
                    ImGui::EndGroup();
                    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                        !ImGui::IsAnyItemHovered()) {
                        openThread=t.id; openTitle=t.title; openAuthor=t.author; openBody=t.body; openCat=t.category; openWhen=t.when; fpNext=0;
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
                if (tnow >= fpNext) {
                    fpCache = fo.Posts(openThread);
                    poScores = fo.Scores("post"); poMy = fo.MyVotes("post");
                    thScores = fo.Scores("thread"); thMy = fo.MyVotes("thread");
                    fpNext = tnow + 6000;
                }
                // The original post card: vote column + author/time + self-text.
                ImGui::BeginChild("##opcard", ImVec2(0, openBody.empty() ? 70 : 128), true);
                long osc = thScores.count(openThread)?thScores[openThread]:0; int omv = thMy.count(openThread)?thMy[openThread]:0;
                ImGui::SetCursorPos(ImVec2(6, 8));
                if (voteWidget("thread", openThread, osc, omv)) fpNext = 0;
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
                    long psc = poScores.count(p.id)?poScores[p.id]:0; int pmv = poMy.count(p.id)?poMy[p.id]:0;
                    ImGui::PushID((int)p.id);
                    ImGui::BeginChild("##crow", ImVec2(0, p.body.size() > 80 ? 92 : 68), true);
                    ImGui::SetCursorPos(ImVec2(6, 8));
                    if (voteWidget("post", p.id, psc, pmv)) fpNext = 0;
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
                if (shownC == 0) { ImGui::Dummy(ImVec2(0, 10)); ImGui::TextDisabled("No comments yet. Be the first to comment."); }
                ImGui::EndChild();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90);
                bool rp = ImGui::InputTextWithHint("##reply", "Add a comment...", replyBuf, sizeof(replyBuf), ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::SameLine();
                if ((PrimaryButton("Comment", ImVec2(82, 0)) || rp) && replyBuf[0]) {
                    if (fo.Reply(openThread, replyBuf, &foErr)) { replyBuf[0] = '\0'; fpNext = 0; }
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
                ImGui::Dummy(ImVec2(0, 24));
                ImGui::TextDisabled("No downloads yet.");
                ImGui::TextDisabled("Open the Marketplace, pick a game and hit Get > Download.");
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
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
