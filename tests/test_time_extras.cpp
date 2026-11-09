// 1.1 Time extras: hit stop, time-scale ramps, effective vs base scale, the fixed clock,
// the real and wall clocks (with a test clock hook), duration / clock formatting, and the
// Stopwatch precision fix. All deterministic: no sleeping, no real clock.
#include "test_framework.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/Scheduler.hpp"
#include "okay/Core/Stopwatch.hpp"
#include "okay/Core/Time.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

using namespace okay;

namespace {

void Fresh() { Game::Reset(); Time::ResetForTesting(); }

double g_mono = 0.0, g_unix = 0.0;
double FakeMono() { return g_mono; }
double FakeUnix() { return g_unix; }

// xorshift for the property test (no dependence on okay::Random)
std::uint32_t g_s = 12345u;
float Rand01() { g_s ^= g_s << 13; g_s ^= g_s >> 17; g_s ^= g_s << 5; return (float)(g_s >> 8) / 16777216.0f; }

}  // namespace

int main() {
    RUN_SUITE("time_extras");

    // ---- HitStop: a freeze frame takes exactly its length out of game time -------------
    {
        Fresh();
        Time::HitStop(0.1f, 0.0f);
        CHECK(Time::HitStopActive());
        CHECK_NEAR(Time::HitStopRemaining(), 0.1, 1e-6);
        CHECK(Time::TimeScale() == 1.0f);                     // the scale itself is untouched
        CHECK(Time::EffectiveTimeScale() == 0.0f);            // but what DeltaTime uses is 0
        for (int i = 0; i < 10; ++i) Time::Step(0.016f);      // 0.16 s of real time
        CHECK_NEAR(Time::UnscaledTime(), 0.16, 1e-6);
        CHECK_NEAR(Time::ElapsedTimeD(), 0.06, 1e-6);         // 0.16 - 0.1 stopped
        CHECK(!Time::HitStopActive());                        // it restored itself
        CHECK(Time::EffectiveTimeScale() == 1.0f);
        Time::Step(0.016f);
        CHECK_NEAR(Time::DeltaTime(), 0.016, 1e-6);
    }
    // ---- ...at any frame length, even one longer than the stop --------------------------
    {
        Fresh();
        Time::HitStop(0.05f, 0.0f);
        Time::Step(0.2f);                                     // one big frame
        CHECK_NEAR(Time::DeltaTime(), 0.15, 1e-6);
        CHECK(!Time::HitStopActive());
        Fresh();
        Time::HitStop(0.1f, 0.0f);
        for (int i = 0; i < 100; ++i) Time::Step(0.001f);     // many tiny ones
        CHECK_NEAR(Time::ElapsedTimeD(), 0.0, 1e-5);          // 0.1 s real, all of it stopped
        Time::Step(0.01f);
        CHECK_NEAR(Time::ElapsedTimeD(), 0.01, 1e-5);
    }
    // ---- composes with the scale: SetTimeScale during a stop is not lost ------------------
    {
        Fresh();
        Time::SetTimeScale(0.5f);
        Time::HitStop(0.1f, 0.25f);
        CHECK_NEAR(Time::EffectiveTimeScale(), 0.125, 1e-6);
        CHECK_NEAR(Time::TimeScale(), 0.5, 1e-6);             // scripts reading the scale see 0.5
        Time::Step(0.05f);
        CHECK_NEAR(Time::DeltaTime(), 0.05 * 0.125, 1e-6);
        Time::SetTimeScale(2.0f);                             // a script changes it mid-stop
        CHECK_NEAR(Time::EffectiveTimeScale(), 0.5, 1e-6);    // 2.0 * 0.25
        Time::Step(0.05f);
        CHECK_NEAR(Time::DeltaTime(), 0.05 * 0.5, 1e-6);
        Time::Step(0.05f);                                    // the stop is over: back to the 2.0 the script set
        CHECK(!Time::HitStopActive());
        CHECK_NEAR(Time::DeltaTime(), 0.1, 1e-6);
        CHECK_NEAR(Time::TimeScale(), 2.0, 1e-6);
    }
    // ---- several at once: the strongest applies, each ends on its own clock ----------------
    {
        Fresh();
        Time::HitStop(0.05f, 0.5f);
        Time::HitStop(0.10f, 0.0f);
        CHECK(Time::EffectiveTimeScale() == 0.0f);
        Time::Step(0.2f);
        CHECK_NEAR(Time::DeltaTime(), 0.1, 1e-6);             // 0.1 s stopped, 0.1 s normal
        Fresh();
        Time::HitStop(0.2f, 0.5f);
        Time::HitStop(0.1f, 0.0f);
        Time::Step(0.3f);                                     // 0.1 freeze + 0.1 half + 0.1 normal
        CHECK_NEAR(Time::DeltaTime(), 0.0 + 0.05 + 0.1, 1e-6);
        CHECK_NEAR(Time::HitStopRemaining(), 0.0, 1e-6);
        // more than the table holds: the shortest makes room, nothing breaks
        Fresh();
        for (int i = 0; i < 20; ++i) Time::HitStop(0.01f * (float)(i + 1), 0.5f);
        Time::Step(0.01f);
        CHECK(Time::HitStopActive());
        Time::Step(1.0f);
        CHECK(!Time::HitStopActive());
    }
    // ---- bad arguments are ignored; Cancel and Game::Reset drop a running stop -----------------
    {
        Fresh();
        Time::HitStop(0.0f); Time::HitStop(-1.0f);
        Time::HitStop(std::numeric_limits<float>::quiet_NaN());
        Time::HitStop(std::numeric_limits<float>::infinity());
        CHECK(!Time::HitStopActive());
        Time::HitStop(0.1f, std::numeric_limits<float>::quiet_NaN());   // NaN scale = 0
        CHECK(Time::EffectiveTimeScale() == 0.0f);
        Time::CancelHitStop();
        CHECK(!Time::HitStopActive() && Time::EffectiveTimeScale() == 1.0f);
        Time::HitStop(10.0f, 0.0f);
        Game::Reset();                                        // a new Play session starts clean
        CHECK(!Time::HitStopActive() && Time::EffectiveTimeScale() == 1.0f);
        Time::HitStop(0.1f, 100.0f);                          // a burst of speed is allowed, capped
        Time::HitStop(0.1f, 1e9f);
        Time::Step(0.05f);
        CHECK(Time::EffectiveTimeScale() <= 100.0f);
        Fresh();
    }
    // ---- a hit stop runs on real time, also while the game is paused ---------------------------------
    {
        Fresh();
        Time::HitStop(0.1f, 0.0f);
        Game::SetPaused(true);
        Time::Step(0.2f);
        CHECK(Time::DeltaTime() == 0.0f);
        CHECK(!Time::HitStopActive());                        // it expired while paused
        Game::SetPaused(false);
        Time::Step(0.016f);
        CHECK_NEAR(Time::DeltaTime(), 0.016, 1e-6);
        Fresh();
    }

    // ---- RampTimeScale -----------------------------------------------------------------------------
    {
        Fresh();
        Time::RampTimeScale(0.0f, 1.0f);                      // slow to a stop over a second
        CHECK(Time::IsRamping());
        CHECK_NEAR(Time::RampTarget(), 0.0, 1e-9);
        Time::Step(0.1f);
        CHECK_NEAR(Time::DeltaTime(), 0.1 * 0.95, 1e-6);      // average of 1.0 and 0.9 over the frame
        CHECK_NEAR(Time::TimeScale(), 0.9, 1e-5);
        for (int i = 0; i < 9; ++i) Time::Step(0.1f);
        CHECK_NEAR(Time::ElapsedTimeD(), 0.5, 1e-5);          // the area under the line
        CHECK(!Time::IsRamping());
        CHECK(Time::TimeScale() == 0.0f);
        Time::Step(0.1f);
        CHECK(Time::DeltaTime() == 0.0f);
    }
    {   // ramping up, and a frame that overshoots the end
        Fresh();
        Time::SetTimeScale(0.0f);
        Time::RampTimeScale(1.0f, 0.05f);
        Time::Step(0.1f);                                     // 0.05 s of ramp (area 0.025) + 0.05 s at 1
        CHECK_NEAR(Time::DeltaTime(), 0.025 + 0.05, 1e-6);
        CHECK(Time::TimeScale() == 1.0f && !Time::IsRamping());
    }
    {   // SetTimeScale takes over; instant ramps; bad input; the target is capped
        Fresh();
        Time::RampTimeScale(0.2f, 10.0f);
        Time::Step(1.0f);
        Time::SetTimeScale(0.7f);
        CHECK(!Time::IsRamping());
        Time::Step(1.0f);
        CHECK_NEAR(Time::DeltaTime(), 0.7, 1e-6);
        Time::RampTimeScale(3.0f, 0.0f);                      // zero time: at once
        CHECK(!Time::IsRamping() && Time::TimeScale() == 3.0f);
        Time::RampTimeScale(1.0f, std::numeric_limits<float>::quiet_NaN());
        CHECK(Time::TimeScale() == 1.0f && !Time::IsRamping());
        Time::RampTimeScale(std::numeric_limits<float>::infinity(), 1.0f);
        CHECK_NEAR(Time::RampTarget(), 100.0, 1e-6);
        Time::RampTimeScale(-4.0f, 1.0f);
        CHECK(Time::RampTarget() == 0.0f);
        Time::CancelRamp();
        CHECK(!Time::IsRamping());
        Time::RampTimeScale(0.5f, 2.0f);                      // a new ramp starts from where the scale is
        Time::Step(1.0f);
        CHECK_NEAR(Time::TimeScale(), 0.75, 1e-5);
        Time::RampTimeScale(1.0f, 1.0f);
        Time::Step(0.5f);
        CHECK_NEAR(Time::TimeScale(), 0.875, 1e-5);
        Game::Reset();                                        // cancels a ramp but keeps the scale
        CHECK(!Time::IsRamping());
        CHECK_NEAR(Time::TimeScale(), 0.875, 1e-5);
        Fresh();
    }
    {   // a ramp runs on while the game is frozen (real time), moving the remembered scale
        Fresh();
        Time::RampTimeScale(0.5f, 1.0f);
        Game::SetPaused(true);
        Time::Step(2.0f);
        CHECK(Time::DeltaTime() == 0.0f);
        Game::SetPaused(false);
        CHECK_NEAR(Time::TimeScale(), 0.5, 1e-6);
        Fresh();
    }

    // ---- property: the scale curve is integrated exactly, however the frames fall -----------------------
    {
        // one reference run in 0.5 ms steps and many runs with random frame lengths summing to the same second
        auto scenario = [](bool fine) {
            Fresh();
            Time::SetTimeScale(0.8f);
            Time::HitStop(0.2f, 0.25f);
            Time::HitStop(0.35f, 0.5f);
            Time::HitStop(0.05f, 0.0f);
            Time::RampTimeScale(0.2f, 0.6f);
            double real = 0.0;
            if (fine) {
                for (int i = 0; i < 2000; ++i) { Time::Step(0.0005f); real += 0.0005; }
            } else {
                while (real < 1.0 - 1e-9) {
                    float dt = 0.0005f + Rand01() * 0.06f;
                    if ((double)dt > 1.0 - real) dt = (float)(1.0 - real);
                    Time::Step(dt); real += (double)dt;
                }
            }
            return Time::ElapsedTimeD();
        };
        const double reference = scenario(true);
        CHECK(reference > 0.1 && reference < 0.9);
        for (int run = 0; run < 25; ++run) CHECK_NEAR(scenario(false), reference, 1e-4);
        Fresh();
    }

    // ---- unscaled timers keep running through a hit stop (Scheduler uses EffectiveTimeScale) -------------
    {
        Fresh();
        Scheduler sch;
        int scaled = 0, unscaled = 0;
        Scheduler::Options o; o.unscaled = true;
        sch.Invoke(0.05f, [&] { ++scaled; });
        sch.Invoke(0.05f, [&] { ++unscaled; }, o);
        Time::HitStop(0.2f, 0.0f);
        for (int i = 0; i < 4; ++i) { Time::Step(0.016f); sch.Update(Time::DeltaTime()); }   // 0.064 s of real time
        CHECK(unscaled == 1);                                 // a UI timer fires on schedule
        CHECK(scaled == 0);                                   // the game timer is frozen with the game
        Fresh();
    }

    // ---- the fixed clock -------------------------------------------------------------------------------
    {
        Fresh();
        CHECK(Time::FixedTime() == 0.0 && !Time::InFixedStep());
        Time::BeginFixedStep();
        CHECK(Time::InFixedStep() && Time::FixedTime() == 0.0);   // the start of the step being run
        Time::EndFixedStep(0.02f);
        CHECK(!Time::InFixedStep());
        CHECK_NEAR(Time::FixedTime(), 0.02, 1e-9);
        Time::EndFixedStep(std::numeric_limits<float>::quiet_NaN());   // bad steps add nothing
        Time::EndFixedStep(-1.0f);
        CHECK_NEAR(Time::FixedTime(), 0.02, 1e-9);
        Time::ResetFixedTime();
        CHECK(Time::FixedTime() == 0.0);
        CHECK_NEAR(Time::FixedDeltaTime(), 0.02, 1e-9);       // the step itself already existed
    }

    // ---- real and wall clocks --------------------------------------------------------------------------------
    {
        Fresh();
        const double a = Time::RealtimeSinceStartup();
        const double b = Time::RealtimeSinceStartup();
        CHECK(a >= 0.0 && b >= a);                            // monotonic
        const double wall = Time::UnixSeconds();
        CHECK(wall > 1.6e9);                                  // after 2020
        Time::Step(3.0f);                                     // game time moves, real time does not care
        Game::SetPaused(true);
        Time::Step(3.0f);
        CHECK(Time::RealtimeSinceStartup() < a + 1.0);
        Game::SetPaused(false);
        // the test hook makes both deterministic
        g_mono = 100.0; g_unix = 1700000000.5;
        Time::SetClockSources(FakeMono, FakeUnix);
        CHECK(Time::RealtimeSinceStartup() == 100.0);
        CHECK(Time::UnixSeconds() == 1700000000.5);
        g_mono = 250.25;
        CHECK(Time::RealtimeSinceStartup() == 250.25);
        Time::Step(1.0f);                                     // frame stepping does not move them
        CHECK(Time::RealtimeSinceStartup() == 250.25);
        Time::SetClockSources(nullptr, nullptr);              // real clocks back
        CHECK(Time::UnixSeconds() > 1.6e9 && Time::UnixSeconds() != 1700000000.5);
        Time::SetClockSources(FakeMono, nullptr);             // one at a time
        CHECK(Time::RealtimeSinceStartup() == 250.25 && Time::UnixSeconds() > 1.6e9);
        Time::SetClockSources(nullptr, nullptr);
        Fresh();
    }

    // ---- formatting ---------------------------------------------------------------------------------------------
    {
        CHECK(Time::FormatDuration(0.0) == "0s");
        CHECK(Time::FormatDuration(7.0) == "7s");
        CHECK(Time::FormatDuration(59.999) == "59s");         // rounded down: never a moment that has not come
        CHECK(Time::FormatDuration(60.0) == "1m 00s");
        CHECK(Time::FormatDuration(125.0) == "2m 05s");
        CHECK(Time::FormatDuration(3600.0) == "1h 00m 00s");
        CHECK(Time::FormatDuration(3723.0) == "1h 02m 03s");
        CHECK(Time::FormatDuration(86400.0 + 3.0 * 3600 + 4.0 * 60 + 5.0) == "1d 03h 04m 05s");
        CHECK(Time::FormatDuration(36 * 3600.0) == "1d 12h 00m 00s");
        CHECK(Time::FormatDuration(-90.0) == "-1m 30s");
        CHECK(Time::FormatDuration(-0.2) == "0s");            // no "-0s"
        CHECK(Time::FormatDuration(std::numeric_limits<double>::quiet_NaN()) == "--");
        CHECK(Time::FormatDuration(std::numeric_limits<double>::infinity()) == "--");
        CHECK(!Time::FormatDuration(1e300).empty());          // huge values are capped, not undefined

        CHECK(Time::FormatClock(0.0) == "00:00");
        CHECK(Time::FormatClock(62.5) == "01:02");
        CHECK(Time::FormatClock(62.5, true) == "01:02.500");
        CHECK(Time::FormatClock(3599.999) == "59:59");
        CHECK(Time::FormatClock(3600.0) == "1:00:00");
        CHECK(Time::FormatClock(3723.0) == "1:02:03");
        CHECK(Time::FormatClock(3723.25, true) == "1:02:03.250");
        CHECK(Time::FormatClock(36000.0) == "10:00:00");
        CHECK(Time::FormatClock(-5.5, true) == "-00:05.500");
        CHECK(Time::FormatClock(0.0004, true) == "00:00.000");
        CHECK(Time::FormatClock(0.57, true) == "00:00.570");  // 0.57 * 1000 is 569.99999...
        CHECK(Time::FormatClock(std::numeric_limits<double>::quiet_NaN()) == "--:--");
    }

    // ---- Stopwatch::GameTime keeps full precision on long sessions (it read the float ElapsedTime) ---------
    {
        Fresh();
        Time::Step(1.0e7f);                                   // 115 days in: a float cannot show half a second here
        Stopwatch sw = Stopwatch::GameTime();
        sw.Start();
        Time::Step(0.5f);
        CHECK_NEAR(sw.ElapsedSeconds(), 0.5, 1e-6);
        Time::Step(0.25f);
        CHECK_NEAR(sw.ElapsedSeconds(), 0.75, 1e-6);
        Fresh();
    }

    // ---- ResetForTesting clears the effects too -----------------------------------------------------------------
    {
        Time::HitStop(5.0f, 0.0f);
        Time::RampTimeScale(0.0f, 5.0f);
        Time::BeginFixedStep();
        Time::ResetForTesting();
        CHECK(!Time::HitStopActive() && !Time::IsRamping() && !Time::InFixedStep());
        CHECK(Time::EffectiveTimeScale() == 1.0f && Time::FixedTime() == 0.0);
    }

    TEST_MAIN_RESULT();
}
