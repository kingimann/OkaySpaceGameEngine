#pragma once
// Small helpers around the frame loop: a max-delta clamp, a frame timer, and a frame
// limiter. They sit beside Application and Time and change nothing there; call them
// from your own loop (see docs/core_runtime.md). Header-light: only <chrono>.
#include <chrono>
#include <cmath>
#include <cstdint>

namespace okay {

/// Keep one long frame (a breakpoint, a window drag, a slow load) from making physics and
/// tweens leap: a delta over `maxDelta` seconds becomes `maxDelta`. NaN, infinity and
/// negative deltas become 0. `maxDelta` <= 0 means no upper limit.
inline float ClampFrameDelta(float dt, float maxDelta) {
    if (!std::isfinite(dt) || dt <= 0.0f) return 0.0f;
    if (maxDelta > 0.0f && dt > maxDelta) return maxDelta;
    return dt;
}

/// Measures the time between frames and clamps it:
///
///     FrameTimer timer;                  // max delta 0.1 s
///     while (running) { Time::Step(timer.Tick()); ... }
class FrameTimer {
public:
    explicit FrameTimer(float maxDeltaSeconds = 0.1f) : m_max(maxDeltaSeconds) {}

    void SetMaxDelta(float seconds) { m_max = seconds; }
    float MaxDelta() const { return m_max; }

    /// Seconds since the previous Tick, clamped. The first call returns 0.
    float Tick() { return Tick(NowSeconds()); }
    /// Same, with the clock given (seconds from any fixed origin): for tests and replays.
    float Tick(double nowSeconds) {
        if (!m_started) { m_started = true; m_last = nowSeconds; m_raw = 0.0; m_clamped = false; return 0.0f; }
        m_raw = nowSeconds - m_last;
        m_last = nowSeconds;
        const float dt = ClampFrameDelta(static_cast<float>(m_raw), m_max);
        m_clamped = m_raw > 0.0 && std::isfinite(m_raw) && m_max > 0.0f && m_raw > static_cast<double>(m_max);
        if (m_clamped) ++m_clampedCount;
        return dt;
    }
    /// Forget the previous frame (after a pause or a scene load, so the gap is not a delta).
    void Reset() { m_started = false; }

    /// The last delta before clamping, and whether it was clamped.
    double LastRawDelta() const { return m_raw; }
    bool WasClamped() const { return m_clamped; }
    std::uint64_t ClampedCount() const { return m_clampedCount; }

    static double NowSeconds() {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }

private:
    float m_max;
    bool m_started = false;
    bool m_clamped = false;
    double m_last = 0.0, m_raw = 0.0;
    std::uint64_t m_clampedCount = 0;
};

/// Holds the frame rate to a target by sleeping out the rest of each frame. A frame that
/// runs long is not "paid back" with several short ones (past one frame late it starts
/// over). In a WebGL build nothing sleeps: the browser paces the frames.
///
///     FrameLimiter limiter(60.0);
///     while (running) { ...frame...; limiter.EndFrame(); }
class FrameLimiter {
public:
    explicit FrameLimiter(double targetFps = 0.0) { SetTargetFps(targetFps); }

    /// Frames per second to hold; 0, a negative or a non-finite number turns the limiter off.
    void SetTargetFps(double fps) { m_target = (std::isfinite(fps) && fps > 0.0) ? 1.0 / fps : 0.0; }
    double TargetFps() const { return m_target > 0.0 ? 1.0 / m_target : 0.0; }
    double TargetFrameSeconds() const { return m_target; }
    bool Enabled() const { return m_target > 0.0; }

    /// How long a frame that has taken `elapsedSeconds` so far still has to wait.
    double WaitSeconds(double elapsedSeconds) const {
        if (m_target <= 0.0 || !(elapsedSeconds < m_target)) return 0.0;
        return m_target - elapsedSeconds;
    }

    /// The decision behind EndFrame, with the clock given: how long to sleep now, and the bookkeeping
    /// for it (it assumes the sleep happens). The first call only starts the clock.
    double Plan(double nowSeconds) {
        if (m_target <= 0.0 || !m_started) { m_started = true; m_last = nowSeconds; return 0.0; }
        const double deadline = m_last + m_target;
        if (nowSeconds >= deadline) {
            m_last = (nowSeconds > deadline + m_target) ? nowSeconds : deadline;
            return 0.0;
        }
        m_last = deadline;
        return deadline - nowSeconds;
    }

    /// End of a frame: sleep until the frame has lasted 1 / target seconds. Returns the seconds
    /// asked to sleep (0 when off, late, or in a WebGL build).
    double EndFrame();

    /// Start over (after a pause).
    void Reset() { m_started = false; }

private:
    double m_target = 0.0;   // seconds per frame; 0 = off
    bool m_started = false;
    double m_last = 0.0;     // when the current frame was due to start
};

} // namespace okay
