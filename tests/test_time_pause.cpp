#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <limits>

using namespace okay;

int main() {
    RUN_SUITE("time_pause");
    Game::Reset();
    Time::SetTimeScale(1.0f);

    // --- double elapsed time stays exact over a long run ---
    {
        const double t0 = Time::ElapsedTimeD(), u0 = Time::UnscaledTime();
        const int ticks = (int)(100.0 * 3600.0 / 0.016);   // 100 hours of 16 ms frames
        for (int i = 0; i < ticks; ++i) Time::Step(0.016f);
        const double expect = (double)ticks * (double)0.016f;
        CHECK_NEAR(Time::ElapsedTimeD() - t0, expect, 1e-6);
        CHECK_NEAR(Time::UnscaledTime() - u0, expect, 1e-6);
        CHECK_NEAR(Time::ElapsedTime(), (float)Time::ElapsedTimeD(), 1.0);
        // a float accumulator would have stalled: one more tick must still register
        const double before = Time::ElapsedTimeD();
        Time::Step(0.016f);
        CHECK(Time::ElapsedTimeD() > before);
    }

    // --- unscaled time ignores the scale ---
    {
        const double e0 = Time::ElapsedTimeD(), u0 = Time::UnscaledTime();
        Time::SetTimeScale(0.5f);
        Time::Step(1.0f);
        CHECK_NEAR(Time::ElapsedTimeD() - e0, 0.5, 1e-9);
        CHECK_NEAR(Time::UnscaledTime() - u0, 1.0, 1e-9);
        Time::SetTimeScale(1.0f);
    }

    // --- negative scales clamp to 0 ---
    {
        Time::SetTimeScale(-2.0f);
        CHECK(Time::TimeScale() == 0.0f);
        Time::Step(0.1f);
        CHECK(Time::DeltaTime() == 0.0f);
        Time::SetTimeScale(1.0f);
    }

    // --- SmoothDelta ---
    {
        CHECK(Time::SmoothDelta(0.0f) == 0.0f);                  // paused stays 0
        // with a known refresh rate, a near-whole number of periods snaps to it
        float s = Time::SmoothDelta(0.0171f, 60.0f);
        CHECK_NEAR(s, 1.0f / 60.0f, 1e-5);
        float s2 = Time::SmoothDelta(0.0329f, 60.0f);
        CHECK_NEAR(s2, 2.0f / 60.0f, 1e-3);
        // without it, jitter is averaged and kept within [raw/2, raw*2]
        Time::SmoothDelta(0.010f); Time::SmoothDelta(0.030f); Time::SmoothDelta(0.010f);
        float a = Time::SmoothDelta(0.030f);
        CHECK_NEAR(a, 0.02f, 1e-4);
        float big = Time::SmoothDelta(0.001f);                   // history says ~0.02: capped at raw*2
        CHECK_NEAR(big, 0.002f, 1e-6);
        Time::Step(0.0f);                                        // clear the pending raw value
    }

    // --- a scale set while paused applies on resume; DeltaTime stays 0 meanwhile ---
    {
        Time::SetTimeScale(1.0f);
        Game::SetPaused(true);
        CHECK(Time::TimeScale() == 0.0f);
        Time::SetTimeScale(0.5f);
        Time::Step(0.1f);
        CHECK(Time::DeltaTime() == 0.0f);
        CHECK(Game::Paused());
        Game::SetPaused(false);
        CHECK_NEAR(Time::TimeScale(), 0.5f, 1e-6);
        Time::Step(0.1f);
        CHECK_NEAR(Time::DeltaTime(), 0.05f, 1e-6);

        // Reset while paused unfreezes and keeps the scale
        Game::SetPaused(true);
        Game::Reset();
        CHECK(!Game::Paused());
        CHECK_NEAR(Time::TimeScale(), 0.5f, 1e-6);
        Time::SetTimeScale(1.0f);
    }

    // --- a bad frame time never poisons the clocks (audit: Step(NaN) made ElapsedTime NaN for good) ---
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        Time::ResetForTesting();
        Time::Step(0.016f);
        const double e0 = Time::ElapsedTimeD(), u0 = Time::UnscaledTime();
        const std::uint64_t f0 = Time::FrameCount();
        Time::Step(nan);
        CHECK(std::isfinite(Time::ElapsedTimeD()) && std::isfinite(Time::UnscaledTime()));
        CHECK(Time::ElapsedTimeD() == e0 && Time::UnscaledTime() == u0);
        CHECK(Time::DeltaTime() == 0.0f && Time::UnscaledDeltaTime() == 0.0f);
        Time::Step(-0.5f);                                    // a negative frame used to run time backwards
        CHECK(Time::ElapsedTimeD() == e0 && Time::UnscaledTime() == u0);
        Time::Step(inf);
        CHECK(Time::ElapsedTimeD() == e0 && Time::UnscaledTime() == u0);
        CHECK(Time::FrameCount() == f0 + 3);                  // still counted as frames
        Time::Step(0.016f);
        CHECK(Time::ElapsedTimeD() > e0);                     // and time still moves afterwards
    }

    // --- an infinite scale is capped (0 * inf was NaN) ---
    {
        Time::ResetForTesting();
        Time::SetTimeScale(std::numeric_limits<float>::infinity());
        CHECK(Time::TimeScale() <= 100.0f && Time::TimeScale() > 0.0f);
        Time::Step(0.0f);
        CHECK(Time::DeltaTime() == 0.0f);                     // not NaN
        Time::Step(0.01f);
        CHECK(std::isfinite(Time::DeltaTime()) && std::isfinite(Time::ElapsedTimeD()));
        Time::SetTimeScale(1e30f);
        CHECK(Time::TimeScale() <= 100.0f);
        Time::SetTimeScale(std::numeric_limits<float>::quiet_NaN());
        CHECK(Time::TimeScale() == 0.0f);
        Time::SetTimeScale(1.0f);
    }

    // --- SmoothDelta ignores an infinite or NaN time ---
    {
        Time::ResetForTesting();
        CHECK(Time::SmoothDelta(std::numeric_limits<float>::infinity()) == 0.0f);
        CHECK(Time::SmoothDelta(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
        CHECK_NEAR(Time::SmoothDelta(0.016f), 0.016f, 1e-6);  // the history was not poisoned
    }

    // --- SmoothedFPS: the first frames are not garbage (a 10 microsecond first frame read as 100000 FPS for ~100 frames) ---
    {
        Time::ResetForTesting();
        Time::Step(1e-5f);
        CHECK(Time::SmoothedFPS() < 1000.0f);
        for (int i = 0; i < 60; ++i) Time::Step(0.016f);
        CHECK_NEAR(Time::SmoothedFPS(), 62.5f, 62.5f * 0.05f);
        Time::ResetForTesting();
        for (int i = 0; i < 30; ++i) Time::Step(1.0f / 30.0f);
        CHECK_NEAR(Time::SmoothedFPS(), 30.0f, 0.5f);         // steady state is unchanged
    }

    // --- Application quit logic (Run needs a terminal; the decision is factored out) ---
    {
        Application::Config cfg;
        CHECK(cfg.quitKey == 'q');                                      // default keeps the old behaviour
        CHECK(Application::ShouldQuit(cfg, true, false));
        CHECK(!Application::ShouldQuit(cfg, false, false));
        CHECK(Application::ShouldQuit(cfg, false, true));        // Game::RequestQuit ends the loop
        cfg.quitKey = 0;
        CHECK(!Application::ShouldQuit(cfg, true, false));
        CHECK(Application::ShouldQuit(cfg, true, true));
    }

    TEST_MAIN_RESULT();
}
