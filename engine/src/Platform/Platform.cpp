// Backend registry and the timer fallbacks of okay::platform.
#include "okay/Platform/Platform.hpp"
#include <atomic>
#include "Internal.hpp"
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <thread>
#if defined(OKAY_XBOX_UWP)
#include "okay/Core/Paths.hpp"   // UWP: per-user folders come from ApplicationData (Paths::SetEnvOverride)
#include <filesystem>
#endif
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace okay::platform {

const char* BackendName(BackendKind) { return "own"; }

bool ParseBackendName(const char* s, BackendKind& out) {
    if (!s || std::strcmp(s, "own") != 0) return false;   // "sdl2" named the backend that was deleted on 2026-11-27
    out = BackendKind::Own;
    return true;
}

bool IsCompiledIn(BackendKind k) {
#ifdef OKAY_PLATFORM_HAVE_OWN
    return k == BackendKind::Own;
#else
    (void)k;
    return false;
#endif
}

// One backend (our own) on every platform, so the build's choice and the OKAY_PLATFORM_BACKEND / OKAY_OWN_PLATFORM
// variables that chose between two have nothing left to pick.
BackendKind DefaultKind() { return BackendKind::Own; }

namespace {
// Set when a backend instance is destroyed. That happens only during static destruction, at the very end of the process (after
// main() returns, or inside std::exit). A plain atomic with a trivial destructor, so it can still be read by objects destroyed later.
std::atomic<bool> g_backendsGone{false};

// Holds one backend instance for the process. Its destructor raises the flag first, so a window destroyed later (a global that
// still owns one, for example) can see that its backend is gone and must not call it.
struct BackendHolder {
    std::unique_ptr<Backend> p;
    explicit BackendHolder(std::unique_ptr<Backend> b) : p(std::move(b)) {}
    ~BackendHolder() { g_backendsGone.store(true); p.reset(); }
};
} // namespace

bool BackendsShutDown() { return g_backendsGone.load(); }

Backend* Get(BackendKind k) {
#ifdef OKAY_PLATFORM_HAVE_OWN
    if (k != BackendKind::Own) return nullptr;
    static BackendHolder inst(CreateOwnBackend());
    return inst.p.get();
#else
    (void)k;
    return nullptr;
#endif
}

Backend* Default() { return Get(BackendKind::Own); }

// ---- Backend defaults (shared by both backends) ----------------------------------
Key Backend::KeyForChar(char c) {
    // US layout: the HID numbering puts a..z at 4.., 1..9 at 30.., 0 at 39.
    if (c >= 'a' && c <= 'z') return (Key)(4 + (c - 'a'));
    if (c >= 'A' && c <= 'Z') return (Key)(4 + (c - 'A'));
    if (c >= '1' && c <= '9') return (Key)(30 + (c - '1'));
    if (c == '0') return Key::N0;
    if (c == ' ') return Key::Space;
    return Key::Unknown;
}

char Backend::CharForKey(Key k) {
    if (k == Key::Unknown) return 0;
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789 ";
    for (const char* c = kChars; *c; ++c)
        if (KeyForChar(*c) == k) return *c;
    return 0;
}

// Written from the Win32 documentation (GetSystemInfo, GlobalMemoryStatusEx) and POSIX sysconf (_SC_NPROCESSORS_ONLN,
// _SC_PHYS_PAGES, _SC_PAGE_SIZE; the last two are common extensions on Linux and macOS).
int CpuCount() {
#if defined(_WIN32)
    SYSTEM_INFO si; GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#elif defined(__unix__) || defined(__APPLE__)
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 0;
#else
    return 0;
#endif
}

int SystemRamMB() {
#if defined(_WIN32)
    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
    return GlobalMemoryStatusEx(&ms) ? (int)(ms.ullTotalPhys / (1024ull * 1024ull)) : 0;
#elif defined(__unix__) || defined(__APPLE__)
    const long pages = sysconf(_SC_PHYS_PAGES), size = sysconf(_SC_PAGE_SIZE);
    return pages > 0 && size > 0 ? (int)((unsigned long long)pages * (unsigned long long)size / (1024ull * 1024ull)) : 0;
#else
    return 0;
#endif
}

std::vector<DisplayMode> Backend::DisplayModes(int index) {
    std::vector<DisplayMode> out;
    DisplayInfo d;
    if (DisplayAt(index, d) && d.w > 0 && d.h > 0) out.push_back({d.w, d.h, d.refreshHz});
    return out;
}

bool Backend::ShowErrorBox(const std::string& title, const std::string& text, Window*) {
    std::fprintf(stderr, "ERROR: %s: %s\n", title.c_str(), text.c_str());
    return false;
}

std::string Backend::BasePath() {
#if defined(_WIN32)
    // Grow the buffer until the whole path fits (a fixed one gave no path at all for a
    // program in a very deep folder).
    std::wstring w(512, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, w.data(), (DWORD)w.size());
        if (n == 0) return std::string();
        if (n < w.size()) { w.resize(n); break; }
        if (w.size() >= 32768) return std::string();   // the longest path Windows has
        w.assign(w.size() * 2, L'\0');
    }
    const size_t cut = w.find_last_of(L"\\/");
    if (cut == std::wstring::npos) return std::string();
    w.resize(cut + 1);
    const int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)(len > 0 ? len : 0), '\0');
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &out[0], len, nullptr, nullptr);
    return out;
#else
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return std::string();
    std::string p(buf, (size_t)n);
    const size_t cut = p.find_last_of('/');
    if (cut == std::string::npos) return std::string();
    p.resize(cut + 1);
    return p;
#endif
}

std::string Backend::PrefPath(const std::string& org, const std::string& app) {
    if (org.empty() || app.empty()) return std::string();
#if defined(OKAY_XBOX_UWP)
    const std::string uwpBase = okay::Paths::Env::Current().appData;   // UWP: ApplicationData folder set by the app shell
    if (uwpBase.empty()) return std::string();
    std::string root = uwpBase;
    for (const std::string& part : {org, app}) {
        root += "/" + part;
        std::error_code ec; std::filesystem::create_directories(root, ec);
    }
    return root + "/";
#elif defined(_WIN32)
    const char* base = std::getenv("APPDATA");
    if (!base || !*base) return std::string();
    std::string root = base;
    for (const std::string& part : {org, app}) {
        root += "\\" + part;
        CreateDirectoryA(root.c_str(), nullptr);
    }
    return root + "\\";
#else
    std::string root;
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    if (xdg && *xdg) root = xdg;
    else if (home && *home) root = std::string(home) + "/.local/share";
    else return std::string();
    // mkdir -p root/org/app
    std::string path;
    size_t pos = 0;
    const std::string full = root + "/" + org + "/" + app;
    while (pos < full.size()) {
        const size_t nx = full.find('/', pos + 1);
        path = full.substr(0, nx);
        if (!path.empty()) mkdir(path.c_str(), 0700);
        if (nx == std::string::npos) break;
        pos = nx;
    }
    return full + "/";
#endif
}

std::vector<std::string> Backend::PreferredLocales() {
    std::vector<std::string> out;
#if defined(_WIN32)
    wchar_t name[LOCALE_NAME_MAX_LENGTH];
    if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0) {
        std::string s;
        for (const wchar_t* c = name; *c; ++c) s += (*c < 128) ? (char)*c : '?';
        if (!s.empty()) out.push_back(s);
    }
#else
    // LANGUAGE is a colon list; else LC_ALL / LC_MESSAGES / LANG. "de_DE.UTF-8@euro" -> "de-DE".
    auto add = [&](std::string v) {
        const size_t cut = v.find_first_of(".@");
        if (cut != std::string::npos) v.resize(cut);
        if (v.empty() || v == "C" || v == "POSIX") return;
        for (char& c : v) if (c == '_') c = '-';
        out.push_back(v);
    };
    if (const char* l = std::getenv("LANGUAGE")) {
        std::string all = l;
        size_t pos = 0;
        while (pos <= all.size()) {
            const size_t nx = all.find(':', pos);
            add(all.substr(pos, nx == std::string::npos ? std::string::npos : nx - pos));
            if (nx == std::string::npos) break;
            pos = nx + 1;
        }
    }
    if (out.empty()) {
        for (const char* var : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
            const char* v = std::getenv(var);
            if (v && *v) { add(v); break; }
        }
    }
#endif
    return out;
}

void Backend::Log(const std::string& line) {
    std::fprintf(stderr, "INFO: %s\n", line.c_str());
    std::fflush(stderr);
}

namespace {
using Clock = std::chrono::steady_clock;
Clock::time_point Epoch() { static const Clock::time_point t = Clock::now(); return t; }
const Clock::time_point kForceEpoch = Epoch();   // starts the fallback clock at load time
} // namespace

uint64_t TicksMs() {
    if (Backend* b = Default()) return b->TicksMs();
    (void)kForceEpoch;
    return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - Epoch()).count();
}
uint64_t PerfCounter() {
    if (Backend* b = Default()) return b->PerfCounter();
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - Epoch()).count();
}
uint64_t PerfFrequency() {
    if (Backend* b = Default()) return b->PerfFrequency();
    return 1000000000ull;
}
void DelayMs(uint32_t ms) {
    if (Backend* b = Default()) { b->DelayMs(ms); return; }
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void Log(const std::string& line) {
    if (Backend* b = Default()) { b->Log(line); return; }
    std::fprintf(stderr, "INFO: %s\n", line.c_str());
}

} // namespace okay::platform
