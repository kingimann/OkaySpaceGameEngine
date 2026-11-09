// 1.1 GameLoop capture mode: a fixed frame rate whatever the real time is (screenshots, video,
// golden-image tests), the frame budget, and how it behaves with hit stop, pause reasons, clocks
// and the recorder. Made-up frame times only: nothing sleeps and nothing reads a real clock.
#include "test_framework.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/FrameRecord.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace okay;

namespace {

void Fresh() {
    Game::SetPauseOnFocusLoss(false);
    Game::NotifyFocus(true);
    Game::Reset();
    Time::ResetForTesting();
    Time::SetFixedDeltaTime(0.02f);
    ClockSet::Global().Reset();
}

}  // namespace

int main() {
    RUN_SUITE("capture_mode");
    Log::console = false;

    // ---- every frame takes exactly 1/fps, whatever the host measured --------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        CHECK(!loop.CaptureActive() && loop.CaptureFps() == 0.0f && loop.FramesRemaining() == -1);
        CHECK(loop.SetCapture(60.0f));
        CHECK(loop.CaptureActive() && loop.CaptureFps() == 60.0f);
        const double host[] = {0.5, std::numeric_limits<double>::quiet_NaN(), 0.0, 0.001, -3.0, 7.0, 0.0166};
        const float expect = 1.0f / 60.0f;
        bool allExact = true, framesExact = true;
        std::uint64_t f0 = Time::FrameCount();
        for (double h : host) {
            loop.BeginFrame(h);
            if (Time::DeltaTime() != expect || Time::UnscaledDeltaTime() != expect) allExact = false;
            if (loop.FrameDeltaReal() != expect) allExact = false;
            if (Time::FrameCount() != ++f0) framesExact = false;
        }
        CHECK(allExact);
        CHECK(framesExact);
        CHECK(loop.FrameCount() == 7);
        CHECK(loop.HitchCount() == 0 && loop.ClampedFrames() == 0 && loop.LostRealTime() == 0.0);   // never measured: never a hitch
        CHECK_NEAR(Time::UnscaledTime(), 7.0 / 60.0, 1e-6);
        // refused settings change nothing
        CHECK(!loop.SetCapture(0.0f));
        CHECK(!loop.SetCapture(-30.0f));
        CHECK(!loop.SetCapture(std::numeric_limits<float>::quiet_NaN()));
        CHECK(!loop.SetCapture(std::numeric_limits<float>::infinity()));
        CHECK(!loop.SetCapture(5000.0f));
        CHECK(loop.CaptureFps() == 60.0f);
        // switching it off gives the host's time back
        loop.ClearCapture();
        CHECK(!loop.CaptureActive());
        loop.BeginFrame(0.03);
        CHECK_NEAR(Time::UnscaledDeltaTime(), 0.03, 1e-7);
    }
    // an option instead of a call
    {
        Fresh();
        GameLoop::Options o; o.captureFps = 24.0f;
        GameLoop loop(o);
        loop.BeginFrame(1.0);
        CHECK_NEAR(Time::DeltaTime(), 1.0 / 24.0, 1e-7);
        o.captureFps = std::numeric_limits<float>::quiet_NaN();   // a bad option is simply off
        loop.SetOptions(o);
        CHECK(!loop.CaptureActive());
        loop.BeginFrame(0.04);
        CHECK_NEAR(Time::DeltaTime(), 0.04, 1e-7);
    }

    // ---- the frame budget ---------------------------------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        int updates = 0;
        loop.OnUpdate = [&](float) { ++updates; };
        CHECK(loop.SetCapture(30.0f, 10));
        CHECK(loop.FramesRemaining() == 10);
        std::vector<bool> results;
        std::vector<std::int64_t> left;
        for (int i = 0; i < 10; ++i) { results.push_back(loop.BeginFrame(0.0)); left.push_back(loop.FramesRemaining()); }
        bool onlyLastFalse = true;
        for (int i = 0; i < 9; ++i) if (!results[(std::size_t)i]) onlyLastFalse = false;
        CHECK(onlyLastFalse && !results[9]);                  // the tenth frame runs, and says stop
        CHECK(left[0] == 9 && left[8] == 1 && left[9] == 0);
        CHECK(updates == 10 && loop.FrameCount() == 10);
        CHECK(loop.QuitRequested());
        // it runs no more: exactly N frames
        const std::uint64_t tf = Time::FrameCount();
        CHECK(!loop.BeginFrame(0.0) && !loop.BeginFrame(0.0));
        CHECK(updates == 10 && Time::FrameCount() == tf && loop.FrameCount() == 10);
        // Reset and BeginSession give a fresh budget; clearing it (or <= 0) removes it
        loop.Reset();
        CHECK(loop.FramesRemaining() == 10 && loop.BeginFrame(0.0) && updates == 11);
        loop.BeginSession();
        CHECK(loop.FramesRemaining() == 10);
        loop.SetFrameBudget(0);
        CHECK(loop.FramesRemaining() == -1 && !loop.QuitRequested());
        for (int i = 0; i < 50; ++i) CHECK(loop.BeginFrame(0.0));
        loop.SetFrameBudget(5);                               // counted from now, not from the start of the run
        CHECK(loop.FramesRemaining() == 5);
        for (int i = 0; i < 4; ++i) CHECK(loop.BeginFrame(0.0));
        CHECK(!loop.BeginFrame(0.0) && loop.FramesRemaining() == 0);
        loop.SetFrameBudget(-7);
        CHECK(loop.FramesRemaining() == -1);
        // a budget with no capture: it counts the host's frames
        Fresh();
        GameLoop plain;
        plain.SetFrameBudget(3);
        CHECK(plain.BeginFrame(0.016) && plain.BeginFrame(0.016) && !plain.BeginFrame(0.016) && !plain.BeginFrame(0.016));
        CHECK(plain.FrameCount() == 3);
        // changing other options does not restart the budget
        GameLoop::Options o = plain.options();
        o.fixedStep = 0.01f;
        plain.SetOptions(o);
        CHECK(plain.FramesRemaining() == 0);
    }

    // ---- exact: not clamped, smoothed or counted -----------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop::Options o; o.smoothDelta = true; o.refreshHz = 60.0f;
        GameLoop loop(o);
        loop.SetCapture(2.0f);                                // half a second a frame: over the 0.25 s clamp
        for (int i = 0; i < 6; ++i) {
            loop.BeginFrame(0.016);
            CHECK(Time::UnscaledDeltaTime() == 0.5f);
        }
        CHECK(loop.ClampedFrames() == 0 && loop.HitchCount() == 0);
        Fresh();
        GameLoop v(o);
        v.SetCapture(60.0f);
        for (int i = 0; i < 10; ++i) { v.BeginFrame(0.2 * (i % 2)); CHECK(Time::UnscaledDeltaTime() == 1.0f / 60.0f); }
    }

    // ---- fixed steps and the clocks follow the captured time ------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        loop.SetCapture(60.0f);
        for (int i = 0; i < 300; ++i) loop.BeginFrame(0.0);   // five captured seconds
        CHECK(loop.TotalFixedSteps() >= 249 && loop.TotalFixedSteps() <= 250);
        CHECK_NEAR(Clock::Get("ui").Elapsed(), 5.0, 1e-4);
        CHECK_NEAR(Clock::Get("game").Elapsed(), 5.0, 1e-4);
        CHECK_NEAR(Time::ElapsedTimeD(), 5.0, 1e-4);
        CHECK(Clock::Get("ui").Ticks() == 300);
        // two runs that were handed different host times are identical frame for frame
        auto run = [](double noise) {
            Fresh();
            GameLoop l;
            l.SetCapture(60.0f);
            std::vector<double> v;
            for (int i = 0; i < 200; ++i) {
                l.BeginFrame(noise * (double)(i % 7));
                v.push_back(Time::ElapsedTimeD()); v.push_back(Time::FixedTime());
                v.push_back((double)l.FixedStepsThisFrame()); v.push_back((double)l.Alpha());
            }
            return v;
        };
        CHECK(run(0.0) == run(0.31));
        Fresh();
    }

    // ---- hit stop, pause reasons, time scale ------------------------------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        loop.SetCapture(30.0f);
        Time::HitStop(0.09f, 0.0f);                           // 0.09 s of captured time: two whole frames and part of a third
        std::vector<float> dts;
        for (int i = 0; i < 5; ++i) { loop.BeginFrame(0.0); dts.push_back(Time::DeltaTime()); }
        CHECK(dts[0] == 0.0f && dts[1] == 0.0f);
        CHECK_NEAR(dts[2], 1.0 / 30.0 - 0.09 + 2.0 / 30.0, 1e-5);
        CHECK_NEAR(dts[3], 1.0 / 30.0, 1e-6);
        CHECK(Time::FrameCount() == 5 && loop.FrameCount() == 5);   // the frames still advanced during the freeze
        // pause reasons hold time; the captured frames carry on, and the ui clock keeps its pace
        Game::PushPause("menu");
        Game::PushPause("cutscene");
        for (int i = 0; i < 3; ++i) loop.BeginFrame(0.0);
        CHECK(Time::DeltaTime() == 0.0f && loop.FrameCount() == 8);
        Game::PopPause("menu");
        loop.BeginFrame(0.0);
        CHECK(Time::DeltaTime() == 0.0f);
        Game::PopPause("cutscene");
        loop.BeginFrame(0.0);
        CHECK_NEAR(Time::DeltaTime(), 1.0 / 30.0, 1e-6);
        CHECK_NEAR(Clock::Get("ui").Elapsed(), 10.0 / 30.0, 1e-5);
        // game time scale
        Time::SetTimeScale(0.5f);
        loop.BeginFrame(0.0);
        CHECK_NEAR(Time::DeltaTime(), 1.0 / 60.0, 1e-6);
        Time::SetTimeScale(1.0f);
        // the debug scale composes: slow motion capture
        loop.SetDebugTimeScale(0.5f);
        loop.BeginFrame(0.0);
        CHECK_NEAR(Time::DeltaTime(), 1.0 / 60.0, 1e-6);
        Fresh();
    }

    // ---- quantizing: a recorded capture matches an unrecorded one only to the microsecond -----------------------------------------------------------
    {
        Fresh();
        GameLoop a;
        a.SetCapture(60.0f);
        a.BeginFrame(0.0);
        const float exact = Time::UnscaledDeltaTime();
        Fresh();
        GameLoop::Options o; o.quantizeMicros = true;
        GameLoop b(o);
        b.SetCapture(60.0f);
        b.BeginFrame(0.0);
        CHECK(exact == 1.0f / 60.0f);
        CHECK(Time::UnscaledDeltaTime() == (float)(16667.0 / 1e6));   // whole microseconds, as a recording stores them
        CHECK_NEAR(exact, Time::UnscaledDeltaTime(), 1e-6);
        Fresh();
    }

    // ---- the recorder sees the synthetic frames, flagged, in one run ---------------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        FrameRecorder rec;
        loop.AttachRecorder(&rec);
        loop.SetCapture(60.0f, 1000);
        int n = 0;
        while (loop.BeginFrame(0.123)) ++n;
        CHECK(n + 1 == 1000 && loop.FrameCount() == 1000);
        CHECK(rec.Recording().Frames() == 1000);
        CHECK(rec.Recording().Runs() == 1);                   // one token for the whole capture
        FrameSample s;
        CHECK(rec.Recording().At(0, s) && s.micros == 16667 && (s.flags & kFrameSynthetic) && !(s.flags & kFrameHitch));
        CHECK(rec.Recording().At(999, s) && s.micros == 16667);
        const FrameSummary sum = rec.Recording().Summary();
        CHECK(sum.frames == 1000 && sum.syntheticFrames == 1000 && sum.hitches == 0);
        CHECK_NEAR(sum.avgMs, 16.667, 1e-9);
        CHECK_NEAR(sum.seconds, 16.667, 1e-9);
        // replay: the same Time values come back without a capture setting and without the host's time
        std::vector<double> live;
        {
            Fresh();
            GameLoop l2;
            FrameRecorder r2;
            l2.AttachRecorder(&r2);
            l2.SetCapture(60.0f, 200);
            while (true) { const bool more = l2.BeginFrame(9.0); live.push_back(Time::ElapsedTimeD()); live.push_back(Time::FixedTime()); live.push_back((double)l2.Alpha()); if (!more) break; }
            Fresh();
            GameLoop l3;
            FramePlayer p(r2.Recording());
            l3.AttachPlayer(&p);
            std::vector<double> again;
            while (true) { const bool more = l3.BeginFrame(-1.0); again.push_back(Time::ElapsedTimeD()); again.push_back(Time::FixedTime()); again.push_back((double)l3.Alpha()); if (!more) break; }
            CHECK(live.size() == 600 && live == again);
        }
        Fresh();
    }

    Fresh();
    TEST_MAIN_RESULT();
}
