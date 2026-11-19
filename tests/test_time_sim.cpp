// Time simulations: deterministic, no sleeping, no real clock. Every number the "Existing / Gap" table of
// docs/time_upgrade.md quotes is printed here (run the test to see them) and the properties we promise are
// CHECKed. Frame times come from small generators, so a 100 hour run takes a moment.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/Scheduler.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace okay;

namespace {

// A tiny deterministic generator for "variable fps" (no <random>, same on every platform).
struct Lcg {
    std::uint64_t s = 12345;
    double Next01() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return (double)(s >> 11) / 9007199254740992.0; }
};

struct RunResult {
    double gameSeconds = 0.0;      // Time::ElapsedTimeD()
    double realSeconds = 0.0;      // what the generator handed out
    std::uint64_t steps = 0;       // fixed steps
    int worstStepsInFrame = 0;
    double dropped = 0.0;
};

// Run `realSeconds` of simulated real time at a frame time chosen by `next` (seconds per frame).
template <class Next>
RunResult RunLoop(double realSeconds, Next next, GameLoop::Options opt = GameLoop::Options{}) {
    Time::ResetForTesting();
    Game::Reset();
    GameLoop loop(opt);
    RunResult r;
    while (r.realSeconds < realSeconds) {
        const double dt = next();
        r.realSeconds += dt;
        loop.BeginFrame(dt);
        if (loop.FixedStepsThisFrame() > r.worstStepsInFrame) r.worstStepsInFrame = loop.FixedStepsThisFrame();
    }
    r.gameSeconds = Time::ElapsedTimeD();
    r.steps = loop.TotalFixedSteps();
    r.dropped = loop.DroppedFixedTime();
    return r;
}

} // namespace

int main() {
    RUN_SUITE("time_sim");

    // ---- 1. A 500 ms hitch: clamp and no spiral of death -----------------------------------------
    {
        Time::ResetForTesting(); Game::Reset();
        GameLoop loop;
        for (int i = 0; i < 60; ++i) loop.BeginFrame(1.0 / 60.0);
        const std::uint64_t before = loop.TotalFixedSteps();
        loop.BeginFrame(0.5);
        const int hitchSteps = loop.FixedStepsThisFrame();
        std::printf("[hitch] 500 ms frame: stepped %.3f s, %d fixed steps, dropped %.3f s\n",
                    (double)loop.FrameDeltaStepped(), hitchSteps, loop.DroppedFixedTime());
        CHECK(loop.FrameDeltaStepped() <= 0.25f + 1e-6f);
        CHECK(hitchSteps <= 5);
        int worstAfter = 0;
        for (int i = 0; i < 60; ++i) { loop.BeginFrame(1.0 / 60.0); worstAfter = std::max(worstAfter, loop.FixedStepsThisFrame()); }
        std::printf("[hitch] 60 frames after it: at most %d steps in a frame\n", worstAfter);
        CHECK(worstAfter <= 2);   // back to 1 per frame (3 at 50 Hz / 60 fps: sometimes 0, sometimes 1)
        CHECK(loop.TotalFixedSteps() - before <= 5u + 60u);
    }

    // ---- 2. The same game seconds at every frame rate: steps and time ---------------------------
    {
        const double kHour = 3600.0;
        struct Case { const char* name; double fps; };
        const Case cases[] = {{"30", 30.0}, {"60", 60.0}, {"144", 144.0}};
        std::uint64_t first = 0;
        for (const Case& c : cases) {
            const double dt = 1.0 / c.fps;
            RunResult r = RunLoop(kHour, [&] { return dt; });
            std::printf("[1h @%s fps] game %.6f s (real %.6f s, drift %+.6f s), %llu fixed steps (50 Hz would be 180000)\n",
                        c.name, r.gameSeconds, r.realSeconds, r.gameSeconds - r.realSeconds, (unsigned long long)r.steps);
            CHECK(std::fabs(r.gameSeconds - r.realSeconds) < 0.01);
            CHECK(std::llabs((long long)r.steps - 180000LL) <= 2);
            if (!first) first = r.steps;
        }
        Lcg rng;   // 20 to 120 fps, jittering
        RunResult v = RunLoop(kHour, [&] { return 1.0 / (20.0 + 100.0 * rng.Next01()); });
        std::printf("[1h variable fps] game %.6f s (real %.6f s), %llu steps\n", v.gameSeconds, v.realSeconds, (unsigned long long)v.steps);
        CHECK(std::fabs(v.gameSeconds - v.realSeconds) < 0.01);
        CHECK(std::llabs((long long)v.steps - 180000LL) <= 2);
    }
    {   // 100 hours at 60 fps, no per-frame work beyond the loop
        RunResult r = RunLoop(360000.0, [] { return 1.0 / 60.0; });
        std::printf("[100h @60 fps] game %.6f s (real %.6f s, drift %+.6f s), %llu steps (18000000 expected)\n",
                    r.gameSeconds, r.realSeconds, r.gameSeconds - r.realSeconds, (unsigned long long)r.steps);
        CHECK(std::fabs(r.gameSeconds - r.realSeconds) < 0.05);
        CHECK(std::llabs((long long)r.steps - 18000000LL) <= 3);
        CHECK(std::isfinite(Time::ElapsedTimeD()));
    }

    // ---- 3. The float views lose precision on a long run ------------------------------------------
    {
        const double at24h = 86400.0 + 1.0 / 60.0;
        const float f = (float)at24h;
        const double step = (double)(std::nextafter(f, 1e9f) - f);
        std::printf("[float view] at 24 h a float second counter steps by %.4f s (%.1f ms)\n", step, step * 1000.0);
        CHECK(step > 0.001);   // coarser than a 144 fps frame: why ElapsedTimeD() exists
    }

    // ---- 4. Bad numbers and the time scale -------------------------------------------------------
    {
        Time::ResetForTesting(); Game::Reset();
        GameLoop loop;
        Time::SetTimeScale(0.0f);
        for (int i = 0; i < 100; ++i) loop.BeginFrame(1.0 / 60.0);
        CHECK(Time::ElapsedTimeD() == 0.0 && loop.TotalFixedSteps() == 0);
        Time::SetTimeScale(-3.0f);
        loop.BeginFrame(0.016);
        CHECK(Time::DeltaTime() == 0.0f);
        Time::SetTimeScale(1e30f);
        loop.BeginFrame(0.016);
        CHECK(std::isfinite(Time::ElapsedTimeD()) && Time::TimeScale() <= Time::kMaxTimeScale);
        Time::SetTimeScale(1.0f);
        const double nan = std::nan("");
        loop.BeginFrame(nan); loop.BeginFrame(-1.0); loop.BeginFrame(1e300);
        CHECK(std::isfinite(Time::ElapsedTimeD()) && std::isfinite(Time::DeltaTime()));
        std::printf("[bad dt] NaN, negative and 1e300 frames: elapsed %.3f s, finite\n", Time::ElapsedTimeD());
    }

    // ---- 5. Alpha stays in [0,1) at every rate ---------------------------------------------------
    {
        Time::ResetForTesting(); Game::Reset();
        GameLoop loop;
        Lcg rng; bool ok = true;
        for (int i = 0; i < 20000; ++i) {
            loop.BeginFrame(1.0 / (15.0 + 300.0 * rng.Next01()));
            const float a = loop.Alpha();
            if (!(a >= 0.0f && a < 1.0f)) ok = false;
        }
        CHECK(ok);
    }

    // ---- 6. Scene physics: the 500 ms hitch and the step count ----------------------------------
    {
        Scene s("sim");
        s.UseFixedStepPhysics(true);
        s.Start();
        s.Update(0.5f);
        std::printf("[scene] a 500 ms frame ran %d physics steps (maxPhysicsCatchUp %.3f s at %.0f Hz)\n",
                    s.PhysicsStepsLastFrame(), (double)s.maxPhysicsCatchUp, (double)s.PhysicsStepsPerSecond());
        CHECK(s.PhysicsStepsLastFrame() <= 17);
    }

    TEST_MAIN_RESULT();
}
