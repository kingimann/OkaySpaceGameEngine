// The platform registry for the Xbox UWP build: replaces engine/src/Platform/Platform.cpp in the okay_engine_uwp target (which cannot
// have the SDL / Win32 / X11 backends). The app shell creates a UwpBackend (XboxUwpBackend.hpp) and calls okay::xbox::InstallBackend;
// okay::platform::Default() and Get(Own) then return it, so engine code that asks the platform layer works unchanged.
// Written from engine/include/okay/Platform/Platform.hpp (the interface it must satisfy) and the C++ standard. Compiles on Linux
// (the tests link it) and, UNVERIFIED, with the UWP toolchain.
#include "UwpPlatform.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace okay::xbox {
namespace { platform::Backend* g_installed = nullptr; }
void InstallBackend(platform::Backend* b) { g_installed = b; }
platform::Backend* InstalledBackend() { return g_installed; }
}

namespace okay::platform {

const char* BackendName(BackendKind k) { return k == BackendKind::Own ? "own" : "sdl2"; }
bool ParseBackendName(const char* s, BackendKind& out) {
    if (!s) return false;
    if (std::strcmp(s, "sdl2") == 0) { out = BackendKind::Sdl2; return true; }
    if (std::strcmp(s, "own") == 0)  { out = BackendKind::Own;  return true; }
    return false;
}
// Only the UWP backend (reported as Own) exists in this build; SDL2 is not compiled in.
bool IsCompiledIn(BackendKind k) { return k == BackendKind::Own; }
BackendKind DefaultKind() { return BackendKind::Own; }
Backend* Get(BackendKind k) { return k == BackendKind::Own ? xbox::InstalledBackend() : nullptr; }
Backend* Default() { return xbox::InstalledBackend(); }

Key Backend::KeyForChar(char c) {   // the same US-layout rule as the desktop default
    if (c >= 'a' && c <= 'z') return (Key)(4 + (c - 'a'));
    if (c >= 'A' && c <= 'Z') return (Key)(4 + (c - 'A'));
    if (c >= '1' && c <= '9') return (Key)(30 + (c - '1'));
    if (c == '0') return Key::N0;
    if (c == ' ') return Key::Space;
    return Key::Unknown;
}
char Backend::CharForKey(Key k) {   // the reverse of KeyForChar for the letters, digits and space (added with the own platform layer, #1009)
    if (k == Key::Unknown) return 0;
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789 ";
    for (const char* c = kChars; *c; ++c) if (KeyForChar(*c) == k) return *c;
    return 0;
}
std::vector<DisplayMode> Backend::DisplayModes(int index) {   // the desktop default: the one current mode (added with #1341)
    std::vector<DisplayMode> out;
    DisplayInfo d;
    if (DisplayAt(index, d) && d.w > 0 && d.h > 0) out.push_back({d.w, d.h, d.refreshHz});
    return out;
}
bool Backend::ShowErrorBox(const std::string& title, const std::string& text, Window*) { std::fprintf(stderr, "ERROR: %s: %s\n", title.c_str(), text.c_str()); return false; }
std::string Backend::BasePath() { return std::string(); }
std::string Backend::PrefPath(const std::string&, const std::string&) { return std::string(); }
std::vector<std::string> Backend::PreferredLocales() { return {"en-US"}; }
void Backend::Log(const std::string& line) { std::fprintf(stderr, "INFO: %s\n", line.c_str()); std::fflush(stderr); }

namespace {
using Clock = std::chrono::steady_clock;
Clock::time_point Epoch() { static const Clock::time_point t = Clock::now(); return t; }
const Clock::time_point kForceEpoch = Epoch();
}
uint64_t TicksMs() { if (Backend* b = Default()) return b->TicksMs(); (void)kForceEpoch; return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - Epoch()).count(); }
uint64_t PerfCounter() { if (Backend* b = Default()) return b->PerfCounter(); return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - Epoch()).count(); }
uint64_t PerfFrequency() { if (Backend* b = Default()) return b->PerfFrequency(); return 1000000000ull; }
void DelayMs(uint32_t ms) {
    if (Backend* b = Default()) { b->DelayMs(ms); return; }
#if defined(_WIN32)
    Sleep(ms);
#else
    usleep((useconds_t)ms * 1000u);
#endif
}
void Log(const std::string& line) { if (Backend* b = Default()) { b->Log(line); return; } std::fprintf(stderr, "INFO: %s\n", line.c_str()); }

} // namespace okay::platform
