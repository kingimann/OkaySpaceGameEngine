// Frame delta clamp, FrameTimer and FrameLimiter (decisions tested with a fake clock;
// one real-time check with generous bounds).
#include "test_framework.hpp"
#include "okay/Core/FramePacing.hpp"

#include <chrono>
#include <cmath>
#include <limits>

using namespace okay;

namespace {

void TestClamp() {
    RUN_SUITE("pacing: delta clamp");
    CHECK(ClampFrameDelta(0.016f, 0.1f) == 0.016f);
    CHECK(ClampFrameDelta(0.5f, 0.1f) == 0.1f);
    CHECK(ClampFrameDelta(0.1f, 0.1f) == 0.1f);
    CHECK(ClampFrameDelta(7.0f, 0.0f) == 7.0f && ClampFrameDelta(7.0f, -1.0f) == 7.0f);   // no limit
    CHECK(ClampFrameDelta(0.0f, 0.1f) == 0.0f);
    CHECK(ClampFrameDelta(-0.5f, 0.1f) == 0.0f);                                          // never runs backwards
    CHECK(ClampFrameDelta(std::numeric_limits<float>::quiet_NaN(), 0.1f) == 0.0f);
    CHECK(ClampFrameDelta(std::numeric_limits<float>::infinity(), 0.1f) == 0.0f);
    CHECK(ClampFrameDelta(-std::numeric_limits<float>::infinity(), 0.1f) == 0.0f);
}

void TestTimer() {
    RUN_SUITE("pacing: frame timer");
    FrameTimer t(0.1f);
    CHECK(t.Tick(100.0) == 0.0f);                       // the first call has nothing to compare with
    CHECK_NEAR(t.Tick(100.016), 0.016, 1e-6);
    CHECK(!t.WasClamped() && t.ClampedCount() == 0);
    CHECK(t.Tick(105.0) == 0.1f);                       // a 4.984 s stall becomes 0.1 s
    CHECK(t.WasClamped() && t.ClampedCount() == 1);
    CHECK_NEAR(t.LastRawDelta(), 4.984, 1e-9);
    CHECK_NEAR(t.Tick(105.02), 0.02, 1e-6);             // and the stall does not leak into the next frame
    CHECK(!t.WasClamped());
    CHECK(t.Tick(105.02) == 0.0f);                      // same time twice: 0
    CHECK(t.Tick(105.0) == 0.0f);                       // a clock that went back: 0, not negative
    t.SetMaxDelta(0.0f);                                // no clamp
    CHECK(t.Tick(200.0) > 90.0f && !t.WasClamped());
    t.SetMaxDelta(0.05f);
    CHECK(t.MaxDelta() == 0.05f && t.Tick(201.0) == 0.05f && t.ClampedCount() == 2);
    t.Reset();
    CHECK(t.Tick(9999.0) == 0.0f);                      // after a Reset the gap is not a delta
    // the real clock moves forward
    FrameTimer real;
    real.Tick();
    const float d = real.Tick();
    CHECK(d >= 0.0f && d <= 0.1f);
}

void TestLimiterPlan() {
    RUN_SUITE("pacing: frame limiter decisions");
    FrameLimiter off;
    CHECK(!off.Enabled() && off.TargetFps() == 0.0 && off.WaitSeconds(0.0) == 0.0 && off.Plan(1.0) == 0.0 && off.Plan(1.001) == 0.0);
    for (double bad : {0.0, -5.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        FrameLimiter l(bad);
        CHECK(!l.Enabled());
    }
    FrameLimiter l(100.0);                              // 10 ms frames
    CHECK(l.Enabled() && std::fabs(l.TargetFps() - 100.0) < 1e-9 && std::fabs(l.TargetFrameSeconds() - 0.01) < 1e-12);
    CHECK_NEAR(l.WaitSeconds(0.004), 0.006, 1e-12);
    CHECK(l.WaitSeconds(0.01) == 0.0 && l.WaitSeconds(0.5) == 0.0 && l.WaitSeconds(-1.0) > 0.0);

    CHECK(l.Plan(10.000) == 0.0);                       // the first call only starts the clock
    CHECK_NEAR(l.Plan(10.004), 0.006, 1e-9);            // 4 ms into a 10 ms frame: wait 6 ms
    CHECK_NEAR(l.Plan(10.013), 0.007, 1e-9);            // the next frame is due at 10.020
    CHECK(l.Plan(10.034) == 0.0);                       // work ended 4 ms after the 10.030 deadline: no wait...
    CHECK_NEAR(l.Plan(10.037), 0.003, 1e-9);            // ...and the grid is kept: the next deadline is 10.040
    // far behind: no catching up with a burst of short frames
    CHECK(l.Plan(11.000) == 0.0);
    CHECK_NEAR(l.Plan(11.002), 0.008, 1e-9);            // the schedule restarted at 11.000
    // changing the rate and resetting
    l.SetTargetFps(50.0);
    l.Reset();
    CHECK(l.Plan(20.0) == 0.0 && std::fabs(l.Plan(20.005) - 0.015) < 1e-9);
    l.SetTargetFps(0.0);
    CHECK(l.Plan(20.006) == 0.0 && l.EndFrame() == 0.0);
    // steady state: frames that do 3 ms of work each end up on a 10 ms grid
    FrameLimiter steady(100.0);
    double now = 0.0, waited = 0.0;
    steady.Plan(now);
    for (int i = 0; i < 200; ++i) { now += 0.003; const double w = steady.Plan(now); waited += w; now += w; }
    CHECK_NEAR(now, 2.0, 1e-6);
    CHECK_NEAR(waited, 200 * 0.007, 1e-6);
}

void TestLimiterReal() {
    RUN_SUITE("pacing: frame limiter, real clock");
    FrameLimiter l(200.0);                              // 5 ms frames
    l.EndFrame();                                       // start
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) l.EndFrame();
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(secs >= 0.095);                               // 20 frames of 5 ms: never faster than the target
    CHECK(secs < 1.0);                                  // and not wildly slower
}

} // namespace

int main() {
    RUN_SUITE("frame_pacing");
    TestClamp();
    TestTimer();
    TestLimiterPlan();
    TestLimiterReal();
    TEST_MAIN_RESULT();
}
