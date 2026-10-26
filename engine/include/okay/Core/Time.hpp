#pragma once
#include <cmath>
#include <cstdint>

namespace okay {

/// Global frame timing, mirroring static `Time` class.
/// Values are updated once per frame by the Application loop.
class Time {
public:
    /// Seconds elapsed since the last frame (scaled by timeScale).
    static float DeltaTime()   { return s_deltaTime; }
    /// Unscaled seconds elapsed since the last frame.
    static float UnscaledDeltaTime() { return s_unscaledDeltaTime; }
    /// Seconds since the application started (scaled).
    static float ElapsedTime() { return s_time; }
    /// Number of frames rendered since startup.
    static std::uint64_t FrameCount() { return s_frameCount; }
    /// Multiplier applied to DeltaTime (1 = normal, 0 = paused).
    static float TimeScale()   { return s_timeScale; }
    static void  SetTimeScale(float scale) { s_timeScale = scale; }

    /// Frames per second, smoothed, from the measured frame times.
    static float SmoothedFPS() { return s_fps; }
    /// The last frame's time as measured (before SmoothDelta), for FPS readouts
    /// and profiling. The game itself steps by DeltaTime.
    static float RawDeltaTime() { return s_rawDeltaTime; }

    /// Even frame steps for movement and cameras. A measured frame time jitters
    /// around the display's refresh period even at a steady frame rate; moving by
    /// it while the screen shows frames at a fixed rate judders. With vsync on
    /// and the refresh rate known (`refreshHz` > 0), a frame within 15% of a
    /// whole number of refresh periods steps exactly that much (the difference
    /// is carried, so time never drifts by more than a period); otherwise the
    /// step is the average of the last 4 frames, kept within [raw/2, raw*2].
    /// 0 (paused) stays 0. Call it once a frame on the measured time, then pass
    /// the result to Step.
    static float SmoothDelta(float raw, float refreshHz = 0.0f) {
        s_rawPending = raw;
        if (!(raw > 0.0f)) return 0.0f;
        s_hist[s_histAt] = raw;
        s_histAt = (s_histAt + 1) % 4;
        if (s_histN < 4) ++s_histN;
        if (refreshHz > 0.0f) {
            const float period = 1.0f / refreshHz;
            const float n = std::round(raw / period);
            if (n >= 1.0f && std::fabs(raw - n * period) <= 0.15f * period) {
                float step = n * period;
                s_carry += raw - step;
                if (s_carry > period) { step += period; s_carry -= period; }
                else if (s_carry < -period && step > period) { step -= period; s_carry += period; }
                return step;
            }
        }
        s_carry = 0.0f;
        float sum = 0.0f;
        for (int i = 0; i < s_histN; ++i) sum += s_hist[i];
        const float avg = sum / (float)s_histN;
        return avg < raw * 0.5f ? raw * 0.5f : (avg > raw * 2.0f ? raw * 2.0f : avg);
    }

    /// Advance global time by one frame of `unscaledDt` seconds. The Application
    /// loop calls this automatically; standalone runtimes (the player) call it so
    /// `ElapsedTime()`, `DeltaTime()`, and `timeScale` work there too.
    static void Step(float unscaledDt) { Advance(unscaledDt); }

private:
    friend class Application;
    static void Advance(float unscaledDt) {
        s_unscaledDeltaTime = unscaledDt;
        s_deltaTime = unscaledDt * s_timeScale;
        s_time += s_deltaTime;
        ++s_frameCount;
        // The FPS readout follows the measured time (SmoothDelta's input), when there is one.
        s_rawDeltaTime = s_rawPending >= 0.0f ? s_rawPending : unscaledDt;
        s_rawPending = -1.0f;
        if (s_rawDeltaTime > 0.0f) {
            float instant = 1.0f / s_rawDeltaTime;
            s_fps = s_fps <= 0.0f ? instant : s_fps * 0.9f + instant * 0.1f;
        }
    }

    static inline float        s_deltaTime         = 0.0f;
    static inline float        s_unscaledDeltaTime = 0.0f;
    static inline float        s_time              = 0.0f;
    static inline float        s_timeScale         = 1.0f;
    static inline float        s_fps               = 0.0f;
    static inline std::uint64_t s_frameCount       = 0;
    static inline float        s_rawDeltaTime      = 0.0f;
    static inline float        s_rawPending        = -1.0f;   // SmoothDelta's input this frame (-1 = none)
    static inline float        s_hist[4]           = {0.0f, 0.0f, 0.0f, 0.0f};
    static inline int          s_histAt = 0, s_histN = 0;
    static inline float        s_carry             = 0.0f;    // vsync snapping: measured minus stepped
};

} // namespace okay
