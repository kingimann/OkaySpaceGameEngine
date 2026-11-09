// 1.1 GameLoop frame stepping while paused (RequestStep) and the debug time scale. Made-up frame
// times only: nothing sleeps and nothing reads a real clock.
#include "test_framework.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/FrameRecord.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
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

std::string Join(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) { if (!out.empty()) out += ' '; out += s; }
    return out;
}

}  // namespace

int main() {
    RUN_SUITE("frame_step");
    Log::console = false;

    // ---- one step: exactly one fixed step, the pause stays, the host's time is ignored ------------------------------------
    {
        Fresh();
        GameLoop loop;
        int fixed = 0, updates = 0, pauses = 0, resumes = 0;
        float lastUpdateDt = -1.0f;
        loop.OnFixedUpdate = [&](float) { ++fixed; };
        loop.OnUpdate = [&](float dt) { ++updates; lastUpdateDt = dt; };
        loop.OnPause = [&] { ++pauses; };
        loop.OnResume = [&] { ++resumes; };
        loop.BeginFrame(0.016);
        Game::SetPaused(true);
        loop.BeginFrame(0.016);
        CHECK(pauses == 1 && Time::DeltaTime() == 0.0f && !loop.IsStepping());
        fixed = 0;
        CHECK(loop.RequestStep());
        CHECK(loop.IsStepping() && loop.StepsRemaining() == 1 && !loop.WasStepFrame());
        CHECK_NEAR(loop.StepDelta(), 0.02, 1e-9);            // one fixed step by default
        const double before = Time::ElapsedTimeD();
        CHECK(loop.BeginFrame(0.5));                          // what the host measured does not matter
        CHECK(loop.WasStepFrame() && !loop.IsStepping() && loop.StepsRemaining() == 0);
        CHECK_NEAR(Time::DeltaTime(), 0.02, 1e-7);
        CHECK_NEAR(Time::ElapsedTimeD() - before, 0.02, 1e-7);
        CHECK_NEAR(loop.FrameDeltaReal(), 0.02, 1e-7);
        CHECK(fixed == 1);                                    // exactly one fixed update
        CHECK_NEAR(lastUpdateDt, 0.02, 1e-7);
        CHECK(Game::Paused() && Time::Frozen());              // paused again straight afterwards
        CHECK(loop.HitchCount() == 0 && loop.ClampedFrames() == 0);   // a 0.5 s "hitch" was never measured
        loop.BeginFrame(0.016);
        CHECK(!loop.WasStepFrame() && Time::DeltaTime() == 0.0f && fixed == 1);
        CHECK(pauses == 1 && resumes == 0);                   // no OnPause / OnResume noise from stepping
    }

    // ---- refusals --------------------------------------------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        CHECK(!loop.RequestStep());                           // not paused: nothing to step
        CHECK(!loop.IsStepping() && loop.StepsRemaining() == 0);
        Game::SetPaused(true);
        CHECK(!loop.RequestStep(0));
        CHECK(!loop.RequestStep(-5));
        CHECK(loop.StepsRemaining() == 0);
        CHECK(loop.RequestStep(1, std::numeric_limits<float>::quiet_NaN()));   // a bad delta falls back to the default
        CHECK_NEAR(loop.StepDelta(), 0.02, 1e-9);
        loop.CancelStep();
        CHECK(loop.RequestStep(1, -1.0f) && std::fabs(loop.StepDelta() - 0.02f) < 1e-9f);
        loop.CancelStep();
        CHECK(loop.RequestStep(1, std::numeric_limits<float>::infinity()) && std::fabs(loop.StepDelta() - 0.02f) < 1e-9f);
        loop.CancelStep();
        CHECK(loop.RequestStep(1, 100.0f) && loop.StepDelta() == GameLoop::kMaxStepDelta);   // capped
        loop.CancelStep();
        CHECK(loop.RequestStep(2147483647));                  // a huge request is capped, not overflowed
        CHECK(loop.StepsRemaining() == GameLoop::kMaxStepRequest);
        CHECK(loop.RequestStep(2147483647) && loop.StepsRemaining() == GameLoop::kMaxStepRequest);
        loop.CancelStep();
        CHECK(!loop.IsStepping());
    }

    // ---- property: N steps advance exactly N steps (fixed updates, Time, the frame count) -------------------------------------------------------
    {
        const int counts[] = {1, 2, 3, 5, 17, 100, 333};
        bool exactFixed = true, exactTime = true, exactFrames = true, stoppedAfter = true, finishedOnce = true;
        for (int n : counts) {
            Fresh();
            GameLoop loop;
            int fixed = 0, finished = 0, steps = 0;
            loop.OnFixedUpdate = [&](float) { ++fixed; };
            loop.OnStep = [&](float, int) { ++steps; };
            loop.OnStepsFinished = [&] { ++finished; };
            Game::SetPaused(true);
            loop.BeginFrame(0.016);
            fixed = 0;
            const std::uint64_t frames0 = Time::FrameCount();
            const double t0 = Time::ElapsedTimeD(), fixed0 = Time::FixedTime();
            loop.RequestStep(n);
            for (int i = 0; i < n; ++i) loop.BeginFrame(0.016);
            if (fixed != n) exactFixed = false;
            if (std::fabs(Time::ElapsedTimeD() - t0 - n * 0.02) > 1e-5 * n) exactTime = false;
            if (std::fabs(Time::FixedTime() - fixed0 - n * 0.02) > 1e-5 * n) exactTime = false;
            if (Time::FrameCount() - frames0 != (std::uint64_t)n) exactFrames = false;
            if (steps != n || finished != 1) finishedOnce = false;
            loop.BeginFrame(0.016);                           // and then it stands still again
            if (fixed != n || loop.WasStepFrame() || Time::DeltaTime() != 0.0f) stoppedAfter = false;
        }
        CHECK(exactFixed);
        CHECK(exactTime);
        CHECK(exactFrames);
        CHECK(finishedOnce);
        CHECK(stoppedAfter);
        // an explicit step length: N frames of 1/60 s advance N/60 s, and fixed steps + carried time add up to it
        for (int n : {1, 6, 60, 301}) {
            Fresh();
            GameLoop loop;
            Game::SetPaused(true);
            loop.BeginFrame(0.016);
            const double t0 = Time::ElapsedTimeD(), u0 = Time::UnscaledTime();
            loop.RequestStep(n, 1.0f / 60.0f);
            for (int i = 0; i < n; ++i) loop.BeginFrame(9.9);
            CHECK_NEAR(Time::ElapsedTimeD() - t0, n / 60.0, 1e-5 * n);
            CHECK_NEAR(Time::UnscaledTime() - u0, n / 60.0, 1e-5 * n);
            CHECK_NEAR((double)loop.TotalFixedSteps() * 0.02 + (double)loop.Alpha() * 0.02 + loop.DroppedFixedTime(), n / 60.0, 1e-4);
        }
    }

    // ---- the order of events is fixed ------------------------------------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        std::vector<std::string> log;
        loop.OnPause = [&] { log.push_back("pause"); };
        loop.OnResume = [&] { log.push_back("resume"); };
        loop.OnStep = [&](float, int left) { log.push_back("step" + std::to_string(left)); };
        loop.OnFixedUpdate = [&](float) { log.push_back("fixed"); };
        loop.OnUpdate = [&](float dt) { log.push_back(dt > 0.0f ? "update" : "update0"); };
        loop.OnLateUpdate = [&](float dt) { log.push_back(dt > 0.0f ? "late" : "late0"); };
        loop.OnStepsFinished = [&] { log.push_back("finished"); };
        Game::SetPaused(true);
        loop.BeginFrame(0.016);
        log.clear();
        loop.RequestStep(3);
        for (int i = 0; i < 4; ++i) loop.BeginFrame(0.016);
        CHECK(Join(log) == "step2 fixed update late step1 fixed update late step0 fixed update late finished update0 late0");
        // resuming afterwards
        log.clear();
        Game::SetPaused(false);
        loop.BeginFrame(0.02);
        CHECK(!log.empty() && log[0] == "resume");
        // a stepped frame during a hitch (the loop's own order: hitch, then pause events, then step)
        Fresh();
        GameLoop h;
        std::vector<std::string> log2;
        h.OnHitch = [&](float, float) { log2.push_back("hitch"); };
        h.OnPause = [&] { log2.push_back("pause"); };
        h.OnStep = [&](float, int) { log2.push_back("step"); };
        h.OnUpdate = [&](float) { log2.push_back("update"); };
        Game::SetPaused(true);
        h.BeginFrame(1.0);                                    // a one second stall while paused: hitch, then pause
        h.RequestStep();
        h.BeginFrame(1.0);                                    // the step frame is exact: no hitch at all
        CHECK(Join(log2) == "hitch pause update step update");
        Fresh();
    }

    // ---- stepping composes with the gameplay time scale and hit stops ----------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        Game::SetPaused(true);
        loop.BeginFrame(0.016);
        Time::SetTimeScale(0.5f);
        loop.RequestStep(1, 0.02f);
        loop.BeginFrame(0.016);
        CHECK_NEAR(Time::DeltaTime(), 0.01, 1e-7);            // half speed
        CHECK_NEAR(Time::UnscaledDeltaTime(), 0.02, 1e-7);
        Time::SetTimeScale(1.0f);
        Time::HitStop(0.05f, 0.0f);                           // a freeze frame, measured in real (stepped) seconds
        loop.RequestStep(4, 0.02f);
        std::vector<float> dts;
        for (int i = 0; i < 4; ++i) { loop.BeginFrame(0.016); dts.push_back(Time::DeltaTime()); }
        CHECK(dts[0] == 0.0f && dts[1] == 0.0f);              // 0.04 s of the freeze
        CHECK_NEAR(dts[2], 0.01, 1e-6);                       // 0.01 s frozen, 0.01 s running
        CHECK_NEAR(dts[3], 0.02, 1e-6);
        CHECK(!Time::HitStopActive());
        // a ramp advances with the stepped time
        Time::RampTimeScale(0.0f, 0.04f);
        loop.RequestStep(2, 0.02f);
        loop.BeginFrame(0.016); loop.BeginFrame(0.016);
        CHECK(Time::BaseTimeScale() == 0.0f && !Time::IsRamping());
        // a gameplay script setting the scale to 0 stops steps from doing anything (and says so: DeltaTime stays 0)
        Time::SetTimeScale(0.0f);
        loop.RequestStep(1, 0.02f);
        loop.BeginFrame(0.016);
        CHECK(Time::DeltaTime() == 0.0f && loop.WasStepFrame() && Game::Paused());
        Fresh();
    }

    // ---- clocks: the game clock follows the step, the cinematic clock steps too, ui follows the step time ---------------------------------------------
    {
        Fresh();
        GameLoop loop;
        Game::SetPaused(true);
        loop.BeginFrame(0.016);                               // a plain paused frame: only "ui" runs
        CHECK(Clock::Get("game").Elapsed() == 0.0 && Clock::Get("cinematic").Elapsed() == 0.0);
        CHECK_NEAR(Clock::Get("ui").Elapsed(), 0.016, 1e-6);
        loop.RequestStep(2, 0.02f);
        loop.BeginFrame(0.016); loop.BeginFrame(0.016);
        CHECK_NEAR(Clock::Get("game").Elapsed(), 0.04, 1e-6);
        CHECK_NEAR(Clock::Get("cinematic").Elapsed(), 0.04, 1e-6);
        CHECK_NEAR(Clock::Get("ui").Elapsed(), 0.056, 1e-6);
        loop.BeginFrame(0.016);                               // paused again: the game and the cinematic hold
        CHECK_NEAR(Clock::Get("game").Elapsed(), 0.04, 1e-6);
        CHECK_NEAR(Clock::Get("cinematic").Elapsed(), 0.04, 1e-6);
        CHECK(Clock::Get("ui").Ticks() == 4);                 // one tick a frame, no matter what the frame was
        Fresh();
    }

    // ---- resuming drops pending steps; Cancel; reasons; quit; exceptions ------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        Game::PushPause("menu");
        loop.BeginFrame(0.016);
        loop.RequestStep(5);
        loop.BeginFrame(0.016);
        CHECK(loop.StepsRemaining() == 4);
        Game::PopPause("menu");                               // the game resumes before the steps are used up
        loop.BeginFrame(0.016);
        CHECK(!loop.WasStepFrame() && loop.StepsRemaining() == 0 && !loop.IsStepping());
        CHECK_NEAR(Time::DeltaTime(), 0.016, 1e-6);           // an ordinary frame
        // Game paused again later: no stale steps left over
        Game::PushPause("menu");
        loop.BeginFrame(0.016);
        CHECK(!loop.WasStepFrame() && Time::DeltaTime() == 0.0f);
        // two reasons: stepping works while any of them holds, and neither is released by it
        Game::PushPause("cutscene");
        loop.RequestStep(2);
        loop.BeginFrame(0.016); loop.BeginFrame(0.016);
        CHECK(Game::IsPausedBy("menu") && Game::IsPausedBy("cutscene") && Game::Paused());
        loop.RequestStep(3);
        loop.CancelStep();
        CHECK(!loop.IsStepping());
        loop.BeginFrame(0.016);
        CHECK(!loop.WasStepFrame());
        Fresh();
    }
    {
        Fresh();
        GameLoop loop;
        Game::SetPaused(true);
        loop.BeginFrame(0.016);
        loop.OnUpdate = [&](float) { if (loop.WasStepFrame()) Game::RequestQuit("quit while stepping", 3); };
        loop.RequestStep(2);
        CHECK(!loop.BeginFrame(0.016));                       // the quit comes out of the step frame
        CHECK(loop.ExitCode() == 3 && Game::Paused());
        Fresh();
        GameLoop t;
        Game::SetPaused(true);
        t.BeginFrame(0.016);
        t.OnUpdate = [&](float) { throw std::runtime_error("boom"); };
        t.RequestStep(2);
        bool threw = false;
        try { t.BeginFrame(0.016); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
        CHECK(Time::Frozen());                                // the freeze is back even though the callback threw
        CHECK(t.StepsRemaining() == 1 && t.IsStepping());     // one of two used; the loop is still usable
        t.OnUpdate = nullptr;
        CHECK(t.BeginFrame(0.016));
        CHECK(t.StepsRemaining() == 0 && !t.IsStepping() && Time::Frozen());
        Fresh();
    }
    // inside the callbacks of a step frame: IsStepping is true, the freeze is lifted, Game still says paused
    {
        Fresh();
        GameLoop loop;
        Game::SetPaused(true);
        loop.BeginFrame(0.016);
        bool seenStepping = false, seenUnfrozen = false, seenPaused = false, plainStepping = false;
        loop.OnUpdate = [&](float) {
            if (loop.WasStepFrame()) { seenStepping = loop.IsStepping(); seenUnfrozen = !Time::Frozen(); seenPaused = Game::Paused(); }
            else plainStepping = plainStepping || loop.IsStepping();
        };
        loop.RequestStep(1);
        loop.BeginFrame(0.016);
        CHECK(seenStepping && seenUnfrozen && seenPaused);
        loop.BeginFrame(0.016);
        CHECK(!plainStepping);
        // a script that requests more steps from OnStepsFinished keeps the sequence going
        int finishes = 0;
        loop.OnStepsFinished = [&] { if (++finishes < 3) loop.RequestStep(1); };
        loop.RequestStep(1);
        for (int i = 0; i < 6; ++i) loop.BeginFrame(0.016);
        CHECK(finishes == 3);
        Fresh();
    }

    // ---- the step length: option, capture rate, per request ------------------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop::Options o; o.stepDelta = 0.05f;
        GameLoop loop(o);
        Game::SetPaused(true);
        loop.BeginFrame(0.016);
        loop.RequestStep();
        loop.BeginFrame(0.016);
        CHECK_NEAR(Time::DeltaTime(), 0.05, 1e-7);
        loop.RequestStep(1, 0.1f);                            // a request beats the option
        loop.BeginFrame(0.016);
        CHECK_NEAR(Time::DeltaTime(), 0.1, 1e-7);
        loop.RequestStep();                                   // and the request's length does not stick
        loop.BeginFrame(0.016);
        CHECK_NEAR(Time::DeltaTime(), 0.05, 1e-7);
        // capture mode: one step is one captured frame
        Fresh();
        GameLoop c;
        c.SetCapture(30.0f);
        Game::SetPaused(true);
        c.BeginFrame(0.0);
        c.RequestStep();
        c.BeginFrame(0.0);
        CHECK_NEAR(Time::DeltaTime(), 1.0 / 30.0, 1e-6);
        // exact: not clamped to the 0.25 s limit
        Fresh();
        GameLoop e;
        Game::SetPaused(true);
        e.BeginFrame(0.016);
        e.RequestStep(1, 0.9f);
        e.BeginFrame(0.016);
        CHECK_NEAR(Time::DeltaTime(), 0.9, 1e-6);
        CHECK(e.HitchCount() == 0 && e.ClampedFrames() == 0);
        CHECK(e.FixedStepsThisFrame() == 5);                  // 45 steps are owed, the cap still applies
        Fresh();
    }

    // ---- debug time scale: slow motion and fast forward ------------------------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        CHECK(loop.DebugTimeScale() == 1.0f);
        CHECK(loop.SetDebugTimeScale(0.5f) && loop.DebugTimeScale() == 0.5f);
        loop.BeginFrame(0.02);
        CHECK_NEAR(Time::DeltaTime(), 0.01, 1e-7);
        CHECK_NEAR(Time::UnscaledDeltaTime(), 0.01, 1e-7);    // the whole game runs as if the machine were slower
        CHECK_NEAR(loop.FrameDeltaReal(), 0.02, 1e-7);        // the real time is still reported as it was
        CHECK_NEAR(loop.FrameDeltaStepped(), 0.01, 1e-7);
        // composes with the gameplay scale
        Time::SetTimeScale(0.5f);
        loop.BeginFrame(0.02);
        CHECK_NEAR(Time::DeltaTime(), 0.005, 1e-7);
        Time::SetTimeScale(1.0f);
        // and with a hit stop: 0.05 s of (debug scaled) time frozen = five frames of 0.01 s, then it runs again
        Time::HitStop(0.05f, 0.0f);
        int frozen = 0;
        for (int i = 0; i < 8; ++i) { loop.BeginFrame(0.02); if (Time::DeltaTime() == 0.0f) ++frozen; }
        CHECK(frozen == 5);
        // garbage is refused and changes nothing
        CHECK(!loop.SetDebugTimeScale(std::numeric_limits<float>::quiet_NaN()));
        CHECK(!loop.SetDebugTimeScale(-1.0f));
        CHECK(!loop.SetDebugTimeScale(std::numeric_limits<float>::infinity()));
        CHECK(loop.DebugTimeScale() == 0.5f);
        CHECK(loop.SetDebugTimeScale(1000.0f) && loop.DebugTimeScale() == GameLoop::kMaxDebugTimeScale);   // capped
        loop.SetDebugTimeScale(1.0f);
        // zero stops the clock without pausing the game
        loop.SetDebugTimeScale(0.0f);
        const double e0 = Time::ElapsedTimeD();
        loop.BeginFrame(0.02);
        CHECK(Time::DeltaTime() == 0.0f && Time::ElapsedTimeD() == e0 && !Game::Paused());
        Fresh();
    }
    // the debug scale belongs to the loop: gameplay and sessions never reset or save it
    {
        Fresh();
        GameLoop loop;
        loop.SetDebugTimeScale(0.25f);
        Game::Reset();
        Game::BeginSession();                                 // a script or the editor starting Play
        Time::SetTimeScale(1.0f);
        Time::ResetForTesting();
        loop.Reset();
        loop.BeginSession();
        GameLoop::Options o; o.maxDelta = 0.1f; loop.SetOptions(o);
        CHECK(loop.DebugTimeScale() == 0.25f);
        loop.BeginFrame(0.02);
        CHECK_NEAR(Time::UnscaledDeltaTime(), 0.005, 1e-7);
        loop.SetDebugTimeScale(1.0f);
        loop.BeginFrame(0.02);
        CHECK_NEAR(Time::UnscaledDeltaTime(), 0.02, 1e-7);
        Fresh();
    }
    // fast forward: the step cap grows with it, so it really runs that much faster
    {
        Fresh();
        GameLoop loop;                                        // maxFixedSteps 5
        loop.SetDebugTimeScale(8.0f);
        loop.BeginFrame(0.02);                                // 0.16 s of game time: 8 steps
        CHECK(loop.FixedStepsThisFrame() == 8 && loop.DroppedFixedTime() == 0.0);
        for (int i = 0; i < 100; ++i) loop.BeginFrame(0.02);
        CHECK_NEAR((double)loop.TotalFixedSteps() * 0.02 + (double)loop.Alpha() * 0.02, 101 * 0.16, 1e-3);   // nothing lost
        loop.SetDebugTimeScale(1.0f);
        loop.BeginFrame(0.25);
        CHECK(loop.FixedStepsThisFrame() == 5);               // and back to the normal cap
        // a debug step ignores the debug scale
        Fresh();
        GameLoop s;
        s.SetDebugTimeScale(0.1f);
        Game::SetPaused(true);
        s.BeginFrame(0.02);
        s.RequestStep(1, 0.02f);
        s.BeginFrame(0.02);
        CHECK_NEAR(Time::DeltaTime(), 0.02, 1e-7);
        Fresh();
    }

    // ---- a step is deterministic: the same run twice gives the same Time values ----------------------------------------------------------------------
    {
        auto run = [] {
            Fresh();
            GameLoop loop;
            std::vector<double> values;
            std::uint32_t seed = 4242u;
            auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) / 16777216.0f; };
            for (int i = 0; i < 400; ++i) {
                if (i == 100) Game::SetPaused(true);
                if (i == 120 || i == 140) loop.RequestStep(1 + (int)(rnd() * 4.0f), 0.01f + rnd() * 0.03f);
                if (i == 170) Game::SetPaused(false);
                if (i == 250) loop.SetDebugTimeScale(0.5f);
                loop.BeginFrame(rnd() * 0.05);
                values.push_back(Time::ElapsedTimeD()); values.push_back(Time::FixedTime());
                values.push_back((double)loop.FixedStepsThisFrame()); values.push_back((double)loop.Alpha());
            }
            return values;
        };
        const std::vector<double> a = run(), b = run();
        CHECK(a.size() == b.size() && a == b);
    }

    // ---- fuzz: every control at once, in any order, keeps the loop's invariants ---------------------------------------------------------------------------------
    {
        std::uint32_t seed = 1u;
        auto rnd = [&] { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; };
        auto unit = [&] { return (float)(rnd() >> 8) / 16777216.0f; };
        long long frames = 0;
        bool finite = true, frozenMatches = true, alphaOk = true, capOk = true, budgetOk = true, monotonic = true, recordOk = true, stepsOk = true;
        for (int session = 0; session < 40; ++session) {
            seed = 1000u + (std::uint32_t)session * 7919u;
            Fresh();
            GameLoop::Options o;
            o.maxFixedSteps = 1 + (int)(rnd() % 6u);
            o.smoothDelta = rnd() % 2u == 0u;
            o.refreshHz = rnd() % 2u ? 60.0f : 0.0f;
            GameLoop loop(o);
            FrameRecorder rec;
            FramePlayer player;
            bool recording = false;
            std::uint64_t recordedBefore = 0;
            double lastElapsed = 0.0;
            for (int op = 0; op < 500; ++op) {
                switch (rnd() % 24u) {
                    case 0: Game::PushPause("a"); break;
                    case 1: Game::PopPause("a"); break;
                    case 2: Game::SetPaused(rnd() % 2u == 0u); break;
                    case 3: loop.RequestStep(1 + (int)(rnd() % 4u), rnd() % 3u ? unit() * 0.05f : 0.0f); break;
                    case 4: loop.CancelStep(); break;
                    case 5: loop.SetDebugTimeScale(rnd() % 5u == 0u ? std::numeric_limits<float>::quiet_NaN() : unit() * 12.0f); break;
                    case 6: loop.SetCapture(rnd() % 4u == 0u ? -1.0f : 1.0f + unit() * 120.0f, rnd() % 3u ? 0 : (long long)(rnd() % 30u)); break;
                    case 7: loop.ClearCapture(); break;
                    case 8: loop.SetFrameBudget(rnd() % 3u ? 0 : (long long)(rnd() % 40u)); break;
                    case 9: if (rnd() % 6u == 0u) { loop.Reset(); } break;
                    case 10: if (rnd() % 8u == 0u) { loop.BeginSession(); } break;
                    case 11: Time::HitStop(unit() * 0.2f, unit()); break;
                    case 12: Time::SetTimeScale(unit() * 2.0f); break;
                    case 13: Time::RampTimeScale(unit() * 2.0f, unit() * 0.5f); break;
                    case 14: loop.SetFrameMarker((int)(rnd() % 100u)); break;
                    case 15:
                        if (!recording) { rec.Clear(); loop.AttachRecorder(&rec); recording = true; recordedBefore = loop.FrameCount(); }
                        else { loop.AttachRecorder(nullptr); recording = false; }
                        break;
                    case 16: {
                        GameLoop::Options n = loop.options();
                        n.fixedStep = rnd() % 2u ? 0.0f : 0.005f + unit() * 0.05f;
                        n.maxFixedSteps = 1 + (int)(rnd() % 8u);
                        n.maxDelta = rnd() % 4u ? 0.25f : 0.0f;
                        loop.SetOptions(n);
                        break;
                    }
                    case 17: if (rnd() % 20u == 0u) { Game::CancelQuit(); } break;
                    default: {
                        const double host = rnd() % 10u == 0u ? std::numeric_limits<double>::quiet_NaN() : (rnd() % 8u == 0u ? 3.0 : unit() * 0.04);
                        const std::int64_t remainingBefore = loop.FramesRemaining();
                        const std::uint64_t tf = Time::FrameCount();
                        const std::uint64_t lf = loop.FrameCount();
                        const std::uint64_t rf = rec.Recording().Frames();
                        const bool more = loop.BeginFrame(host);
                        ++frames;
                        const bool ran = Time::FrameCount() != tf;
                        if (remainingBefore == 0 && (ran || more)) budgetOk = false;       // a spent budget runs nothing
                        if (ran && loop.FrameCount() != lf + 1 && loop.FrameCount() != 1) budgetOk = false;   // (a callback-free Reset can not happen mid-frame)
                        if (!std::isfinite(Time::DeltaTime()) || Time::DeltaTime() < 0.0f || !std::isfinite(Time::UnscaledDeltaTime()) || Time::UnscaledDeltaTime() < 0.0f) finite = false;
                        if (Time::Frozen() != Game::Paused()) frozenMatches = false;
                        if (!(loop.Alpha() >= 0.0f && loop.Alpha() < 1.0f)) alphaOk = false;
                        const double dbg = (double)loop.DebugTimeScale();
                        const long long cap = (long long)loop.options().maxFixedSteps * (long long)std::ceil(dbg > 1.0 ? dbg : 1.0);
                        if (ran && loop.FixedStepsThisFrame() > cap) capOk = false;   // (a refused frame leaves the last frame's count)
                        if (Time::ElapsedTimeD() + 1e-12 < lastElapsed) monotonic = false;
                        lastElapsed = Time::ElapsedTimeD();
                        if (loop.StepsRemaining() < 0 || loop.StepsRemaining() > GameLoop::kMaxStepRequest) stepsOk = false;
                        if (loop.FramesRemaining() < -1) budgetOk = false;
                        if (recording && ran && rec.Recording().Frames() != rf + 1 && !rec.Full()) recordOk = false;
                        (void)recordedBefore;
                        break;
                    }
                }
            }
            loop.AttachRecorder(nullptr);
            // whatever was recorded saves and loads back whole
            FrameRecording again;
            if (!FrameRecording::FromText(rec.Recording().ToText(), again) || !(again == rec.Recording())) recordOk = false;
        }
        CHECK(frames > 3000);
        CHECK(finite);
        CHECK(frozenMatches);
        CHECK(alphaOk);
        CHECK(capOk);
        CHECK(budgetOk);
        CHECK(monotonic);
        CHECK(recordOk);
        CHECK(stepsOk);
        Fresh();
    }

    Fresh();
    TEST_MAIN_RESULT();
}
