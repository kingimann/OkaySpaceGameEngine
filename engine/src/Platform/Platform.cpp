// Backend registry and the timer fallbacks of okay::platform.
#include "okay/Platform/Platform.hpp"
#include "Internal.hpp"
#include <chrono>
#include <cstring>
#include <thread>

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
#if defined(OKAY_PLATFORM_DEFAULT_OWN)
    return BackendKind::Own;
#else
    return BackendKind::Sdl2;
#endif
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

} // namespace okay::platform
