#pragma once
#include <cstdint>

namespace okay {

/// Global frame timing, mirroring Unity's static `Time` class.
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

    static float SmoothedFPS() { return s_fps; }

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
        if (unscaledDt > 0.0f) {
            float instant = 1.0f / unscaledDt;
            s_fps = s_fps <= 0.0f ? instant : s_fps * 0.9f + instant * 0.1f;
        }
    }

    static inline float        s_deltaTime         = 0.0f;
    static inline float        s_unscaledDeltaTime = 0.0f;
    static inline float        s_time              = 0.0f;
    static inline float        s_timeScale         = 1.0f;
    static inline float        s_fps               = 0.0f;
    static inline std::uint64_t s_frameCount       = 0;
};

} // namespace okay
