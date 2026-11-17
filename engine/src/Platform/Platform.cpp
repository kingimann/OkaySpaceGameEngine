// Backend registry and the timer fallbacks of okay::platform.
#include "okay/Platform/Platform.hpp"
#include "Internal.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <thread>
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

const char* BackendName(BackendKind k) { return k == BackendKind::Own ? "own" : "sdl2"; }

bool ParseBackendName(const char* s, BackendKind& out) {
    if (!s) return false;
    if (std::strcmp(s, "sdl2") == 0) { out = BackendKind::Sdl2; return true; }
    if (std::strcmp(s, "own") == 0)  { out = BackendKind::Own;  return true; }
    return false;
}

bool IsCompiledIn(BackendKind k) {
#ifdef OKAY_PLATFORM_HAVE_SDL2
    if (k == BackendKind::Sdl2) return true;
#endif
#ifdef OKAY_PLATFORM_HAVE_OWN
    if (k == BackendKind::Own) return true;
#endif
    return false;
}

BackendKind DefaultKind() {
    // The build picks the default (OKAY_PLATFORM_BACKEND at configure time). The same
    // variable at run time overrides it for this process, so one binary can be tried
    // on either backend; a value that is not "sdl2" or "own" is ignored.
    static const BackendKind kind = [] {
#if defined(OKAY_PLATFORM_DEFAULT_OWN)
        BackendKind k = BackendKind::Own;
#else
        BackendKind k = BackendKind::Sdl2;
#endif
        BackendKind fromEnv;
        if (ParseBackendName(std::getenv("OKAY_PLATFORM_BACKEND"), fromEnv)) k = fromEnv;
        return k;
    }();
    return kind;
}

Backend* Get(BackendKind k) {
    if (k == BackendKind::Sdl2) {
#ifdef OKAY_PLATFORM_HAVE_SDL2
        static std::unique_ptr<Backend> inst = CreateSdl2Backend();
        return inst.get();
#else
        return nullptr;
#endif
    }
#ifdef OKAY_PLATFORM_HAVE_OWN
    static std::unique_ptr<Backend> inst = CreateOwnBackend();
    return inst.get();
#else
    return nullptr;
#endif
}

Backend* Default() {
    if (Backend* b = Get(DefaultKind())) return b;
    return Get(DefaultKind() == BackendKind::Own ? BackendKind::Sdl2 : BackendKind::Own);
}

std::unique_ptr<Window> WrapSdl2Window(void* sdlWindow) {
#ifdef OKAY_PLATFORM_HAVE_SDL2
    return sdlWindow ? WrapSdl2WindowImpl(sdlWindow) : nullptr;
#else
    (void)sdlWindow;
    return nullptr;
#endif
}

bool TranslateSdl2Event(const void* sdlEvent, Event& out) {
#ifdef OKAY_PLATFORM_HAVE_SDL2
    return sdlEvent ? TranslateSdl2EventImpl(sdlEvent, out) : false;
#else
    (void)sdlEvent; (void)out;
    return false;
#endif
}

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

bool Backend::ShowErrorBox(const std::string& title, const std::string& text, Window*) {
    std::fprintf(stderr, "ERROR: %s: %s\n", title.c_str(), text.c_str());
    return false;
}

std::string Backend::BasePath() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return std::string();
    std::wstring w(buf, n);
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
#if defined(_WIN32)
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

std::vector<DisplayInfo> Displays() {
    Backend* b = Default();
    return b ? b->Displays() : std::vector<DisplayInfo>();
}

bool WindowReachable(const std::vector<DisplayInfo>& displays, int x, int y, int w, int h) {
    for (const DisplayInfo& d : displays) {
        const DisplayRect& b = d.usable.w > 0 && d.usable.h > 0 ? d.usable : d.bounds;
        if (b.w <= 0 || b.h <= 0) continue;
        const int ix = std::min(x + w, b.x + b.w) - std::max(x, b.x);
        const int iy = std::min(y + h, b.y + b.h) - std::max(y, b.y);
        if (ix >= 160 && iy >= 80 && y >= b.y - 8 && y < b.y + b.h - 40) return true;
    }
    return false;
}

DisplayRect CenterOnDisplay(const DisplayInfo& d, int w, int h) {
    const DisplayRect& b = d.usable.w > 0 && d.usable.h > 0 ? d.usable : d.bounds;
    DisplayRect r;
    r.w = std::max(1, std::min(w, b.w - 40));
    r.h = std::max(1, std::min(h, b.h - 40));
    r.x = b.x + (b.w - r.w) / 2;
    r.y = b.y + (b.h - r.h) / 2;
    return r;
}

// ---- Display modes (written from the interface's own rules in Platform.hpp) ----
std::vector<VideoMode> SortedVideoModes(std::vector<VideoMode> modes) {
    modes.erase(std::remove_if(modes.begin(), modes.end(), [](const VideoMode& m) { return m.width <= 0 || m.height <= 0; }), modes.end());
    for (VideoMode& m : modes) if (m.refreshHz < 0) m.refreshHz = 0;
    std::sort(modes.begin(), modes.end(), [](const VideoMode& a, const VideoMode& b) {
        if (a.width != b.width) return a.width > b.width;
        if (a.height != b.height) return a.height > b.height;
        return a.refreshHz > b.refreshHz;
    });
    modes.erase(std::unique(modes.begin(), modes.end()), modes.end());
    return modes;
}

VideoMode ClosestVideoMode(const std::vector<VideoMode>& modes, const VideoMode& want) {
    const VideoMode* best = nullptr;
    // 1. The same size: the nearest refresh rate (the lower one on a tie), or the highest when any rate will do.
    for (const VideoMode& m : modes) {
        if (m.width != want.width || m.height != want.height) continue;
        if (!best) { best = &m; continue; }
        if (want.refreshHz <= 0) { if (m.refreshHz > best->refreshHz) best = &m; continue; }
        const int dm = std::abs(m.refreshHz - want.refreshHz), db = std::abs(best->refreshHz - want.refreshHz);
        if (dm < db || (dm == db && m.refreshHz < best->refreshHz)) best = &m;
    }
    if (best) return *best;
    // 2. The smallest mode that holds the wanted size (by area, then the nearest refresh rate).
    auto area = [](const VideoMode& m) { return (long long)m.width * m.height; };
    auto rateCloser = [&](const VideoMode& m, const VideoMode& b) {
        if (want.refreshHz <= 0) return m.refreshHz > b.refreshHz;
        return std::abs(m.refreshHz - want.refreshHz) < std::abs(b.refreshHz - want.refreshHz);
    };
    for (const VideoMode& m : modes) {
        if (m.width < want.width || m.height < want.height) continue;
        if (!best || area(m) < area(*best) || (area(m) == area(*best) && rateCloser(m, *best))) best = &m;
    }
    if (best) return *best;
    // 3. Nothing is that large: the largest mode there is.
    for (const VideoMode& m : modes)
        if (!best || area(m) > area(*best) || (area(m) == area(*best) && rateCloser(m, *best))) best = &m;
    return best ? *best : VideoMode();
}

int DisplayIndexForRect(const std::vector<DisplayInfo>& displays, int x, int y, int w, int h) {
    const int cx = x + w / 2, cy = y + h / 2;
    for (size_t i = 0; i < displays.size(); ++i) {
        const DisplayRect& b = displays[i].bounds;
        if (cx >= b.x && cy >= b.y && cx < b.x + b.w && cy < b.y + b.h) return (int)i;
    }
    long long bestArea = 0; int best = -1;
    for (size_t i = 0; i < displays.size(); ++i) {
        const DisplayRect& b = displays[i].bounds;
        const long long ix = std::min(x + w, b.x + b.w) - std::max(x, b.x);
        const long long iy = std::min(y + h, b.y + b.h) - std::max(y, b.y);
        if (ix > 0 && iy > 0 && ix * iy > bestArea) { bestArea = ix * iy; best = (int)i; }
    }
    return best;
}

void Log(const std::string& line) {
    if (Backend* b = Default()) { b->Log(line); return; }
    std::fprintf(stderr, "INFO: %s\n", line.c_str());
}

} // namespace okay::platform
