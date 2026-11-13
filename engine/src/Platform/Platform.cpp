// Backend registry and the timer fallbacks of okay::platform.
#include "okay/Platform/Platform.hpp"
#include "Internal.hpp"
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "PlatformText.hpp"
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
    // Wide APIs throughout: %APPDATA% and the folder names may hold non-ASCII characters, which the ANSI
    // calls (getenv, CreateDirectoryA) would mangle.
    wchar_t base[MAX_PATH * 2];
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", base, (DWORD)(sizeof(base) / sizeof(base[0])));
    if (n == 0 || n >= sizeof(base) / sizeof(base[0])) return std::string();
    std::wstring root(base, n);
    for (const std::string& part : {org, app}) {
        std::string clean = part;
        for (char& c : clean) if (std::strchr("<>:\"/\\|?*", c) || (unsigned char)c < 0x20) c = '_';   // characters Windows forbids in a file name
        const std::u16string u = text::Utf8ToUtf16(clean);
        root += L"\\";
        root.append(reinterpret_cast<const wchar_t*>(u.data()), u.size());
        CreateDirectoryW(root.c_str(), nullptr);
    }
    root += L"\\";
    return text::Utf16ToUtf8(reinterpret_cast<const char16_t*>(root.data()), root.size());
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
    // The user's UI language list (Settings > Language order), "en-US" style; the default locale as a fallback.
    {
        ULONG count = 0, size = 0;
        if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size > 1) {
            std::wstring buf((size_t)size, L'\0');
            if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, &buf[0], &size)) {
                for (const wchar_t* c = buf.c_str(); *c; c += wcslen(c) + 1)
                    out.push_back(text::Utf16ToUtf8(reinterpret_cast<const char16_t*>(c), wcslen(c)));
            }
        }
    }
    if (out.empty()) {
        wchar_t name[LOCALE_NAME_MAX_LENGTH];
        if (GetUserDefaultLocaleName(name, LOCALE_NAME_MAX_LENGTH) > 0)
            out.push_back(text::Utf16ToUtf8(reinterpret_cast<const char16_t*>(name), wcslen(name)));
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
