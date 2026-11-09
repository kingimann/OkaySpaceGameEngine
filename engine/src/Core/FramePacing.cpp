#include "okay/Core/FramePacing.hpp"
#include "CoreThreads.hpp"

namespace okay {

double FrameLimiter::EndFrame() {
    const double wait = Plan(FrameTimer::NowSeconds());
#if defined(__EMSCRIPTEN__)
    return 0.0;   // the browser paces frames; sleeping or spinning here would only stall the page
#else
    if (wait <= 0.0) return 0.0;
    // Sleep most of it (the OS may overshoot by a millisecond or more), then yield until the deadline.
    const double deadline = FrameTimer::NowSeconds() + wait;
    constexpr double kSpin = 0.0015;
    if (wait > kSpin) corethreads::SleepSeconds(wait - kSpin);
    while (FrameTimer::NowSeconds() < deadline) corethreads::YieldThread();
    return wait;
#endif
}

} // namespace okay
