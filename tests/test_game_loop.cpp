// 1.1 GameLoop: the shared per-frame driver. Everything runs on made-up frame times (a fake
// clock): no sleeping, no real time, so every number below is exact and repeatable.
#include "test_framework.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
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

std::uint32_t g_s = 2463534242u;
float Rand01() { g_s ^= g_s << 13; g_s ^= g_s >> 17; g_s ^= g_s << 5; return (float)(g_s >> 8) / 16777216.0f; }

std::string Join(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) { if (!out.empty()) out += ' '; out += s; }
    return out;
}

}  // namespace

int main() {
    RUN_SUITE("game_loop");
    Log::console = false;   // hitch warnings are expected below

    // ---- a steady 60 Hz-ish run: fixed steps add up to real time ------------------------------------------
    {
        Fresh();
        GameLoop loop;
        int fixedCalls = 0, updates = 0;
        loop.OnFixedUpdate = [&](float dt) { ++fixedCalls; CHECK_NEAR(dt, 0.02, 1e-9); };
        loop.OnUpdate = [&](float dt) { ++updates; CHECK_NEAR(dt, 0.016, 1e-6); };
        for (int i = 0; i < 100; ++i) CHECK(loop.BeginFrame(0.016));
        CHECK(loop.FrameCount() == 100 && Time::FrameCount() == 100 && updates == 100);
        CHECK(fixedCalls >= 79 && fixedCalls <= 80);          // 1.6 s of 0.02 s steps
        CHECK(loop.TotalFixedSteps() == (std::uint64_t)fixedCalls);
        CHECK(loop.HitchCount() == 0 && loop.ClampedFrames() == 0 && loop.LostRealTime() == 0.0);
        CHECK_NEAR(Time::UnscaledTime(), 1.6, 1e-4);
        CHECK(loop.Alpha() >= 0.0f && loop.Alpha() < 1.0f);
        CHECK_NEAR(loop.FixedStep(), 0.02, 1e-9);
    }

    // ---- a huge frame (breakpoint, window drag, alt-tab) is clamped, counted and does not spiral ------------------
    {
        Fresh();
        GameLoop loop;
        int fixedCalls = 0;
        loop.OnFixedUpdate = [&](float) { ++fixedCalls; };
        loop.BeginFrame(0.016);
        loop.BeginFrame(5.0);                                 // five seconds stuck on a breakpoint
        CHECK_NEAR(loop.FrameDeltaReal(), 5.0, 1e-6);
        CHECK_NEAR(loop.FrameDeltaClamped(), 0.25, 1e-6);
        CHECK_NEAR(Time::UnscaledDeltaTime(), 0.25, 1e-6);    // the game only ever sees 0.25 s
        CHECK(loop.FixedStepsThisFrame() == 5);               // capped: no spiral of death
        CHECK_NEAR(loop.DroppedFixedTime(), 0.166, 1e-4);     // 0.016 carried + 0.25 owed, 0.1 run, the rest dropped
        CHECK(loop.HitchCount() == 1 && loop.ClampedFrames() == 1);
        CHECK_NEAR(loop.LastHitch(), 5.0, 1e-6);
        CHECK_NEAR(loop.WorstHitch(), 5.0, 1e-6);
        CHECK_NEAR(loop.LostRealTime(), 4.75, 1e-6);
        loop.BeginFrame(0.016);                               // and the very next frame is back to normal
        CHECK_NEAR(Time::DeltaTime(), 0.016, 1e-6);
        CHECK(loop.FixedStepsThisFrame() <= 1);
        CHECK(loop.HitchCount() == 1 && loop.LastHitch() > 4.9f);   // the record stays
    }
    // a hitch under the clamp is counted but not shortened
    {
        Fresh();
        GameLoop loop;
        loop.BeginFrame(0.2);
        CHECK(loop.HitchCount() == 1 && loop.ClampedFrames() == 0);
        CHECK_NEAR(loop.FrameDeltaClamped(), 0.2, 1e-6);
        CHECK(loop.FixedStepsThisFrame() == 5);               // 10 owed, 5 run
        loop.BeginFrame(0.1);                                 // exactly at the threshold: not a hitch
        CHECK(loop.HitchCount() == 1);
        loop.BeginFrame(0.3);
        CHECK(loop.HitchCount() == 2 && loop.ClampedFrames() == 1);
        CHECK_NEAR(loop.WorstHitch(), 0.3, 1e-6);
        CHECK_NEAR(loop.LastHitch(), 0.3, 1e-6);
    }
    // configurable clamp, replacement time, threshold, and the clamp switched off
    {
        Fresh();
        GameLoop::Options o;
        o.maxDelta = 0.1f; o.hitchThreshold = 0.05f;
        GameLoop loop(o);
        loop.BeginFrame(0.07);
        CHECK(loop.HitchCount() == 1 && loop.ClampedFrames() == 0);
        loop.BeginFrame(0.5);
        CHECK_NEAR(loop.FrameDeltaClamped(), 0.1, 1e-6);
        o.hitchReplacement = 1.0f / 30.0f;                    // Application's rule: a stall steps one target frame
        loop.SetOptions(o);
        loop.BeginFrame(0.5);
        CHECK_NEAR(loop.FrameDeltaClamped(), 1.0 / 30.0, 1e-6);
        o.maxDelta = 0.0f;                                    // <= 0: no clamp
        loop.SetOptions(o);
        loop.BeginFrame(5.0);
        CHECK_NEAR(Time::UnscaledDeltaTime(), 5.0, 1e-6);
        o.hitchThreshold = 0.0f;                              // <= 0: no hitch counting
        loop.SetOptions(o);
        const int before = loop.HitchCount();
        loop.BeginFrame(5.0);
        CHECK(loop.HitchCount() == before);
    }
    // the OnHitch callback
    {
        Fresh();
        GameLoop loop;
        std::vector<std::pair<float, float>> seen;
        loop.OnHitch = [&](float real, float stepped) { seen.push_back({real, stepped}); };
        loop.BeginFrame(0.016);
        loop.BeginFrame(2.0);
        loop.BeginFrame(0.15);
        CHECK(seen.size() == 2);
        CHECK(seen.size() == 2 && std::fabs(seen[0].first - 2.0f) < 1e-6f && std::fabs(seen[0].second - 0.25f) < 1e-6f);
        CHECK(seen.size() == 2 && std::fabs(seen[1].first - 0.15f) < 1e-6f && std::fabs(seen[1].second - 0.15f) < 1e-6f);
    }
    // hitch logging never crashes and is rate limited (it is silent here: Log::console is off)
    {
        Fresh();
        GameLoop::Options o; o.logHitches = true;
        GameLoop loop(o);
        for (int i = 0; i < 120; ++i) loop.BeginFrame(0.5);
        CHECK(loop.HitchCount() == 120);
    }

    // ---- a measurement that is not a number is a 0 s frame -------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        loop.BeginFrame(0.016);
        const double t = Time::ElapsedTimeD();
        const std::uint64_t f = Time::FrameCount();
        loop.BeginFrame(std::numeric_limits<double>::quiet_NaN());
        loop.BeginFrame(-1.0);                                // the clock went backwards
        loop.BeginFrame(std::numeric_limits<double>::infinity());
        CHECK(Time::ElapsedTimeD() == t && Time::FrameCount() == f + 3);
        CHECK(loop.FrameDeltaReal() == 0.0f && loop.FixedStepsThisFrame() == 0);
        CHECK(loop.HitchCount() == 0);
        loop.BeginFrame(0.016);
        CHECK(Time::ElapsedTimeD() > t);
    }

    // ---- the first frame -------------------------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop::Options o; o.firstFrameDelta = 0.016f;
        GameLoop loop(o);
        loop.BeginFrame(3.0);                                 // the first measurement held loading time
        CHECK_NEAR(loop.FrameDeltaReal(), 0.016, 1e-6);
        CHECK(loop.HitchCount() == 0);
        loop.BeginFrame(0.5);                                 // later frames are taken as measured
        CHECK(loop.HitchCount() == 1);
        loop.Reset();                                         // a new session has a first frame again
        loop.BeginFrame(3.0);
        CHECK_NEAR(loop.FrameDeltaReal(), 0.016, 1e-6);
    }

    // ---- callback order, pause and resume ----------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        std::vector<std::string> log;
        loop.OnPause = [&] { log.push_back("pause"); };
        loop.OnResume = [&] { log.push_back("resume"); };
        loop.OnFixedUpdate = [&](float) { log.push_back("fixed"); };
        loop.OnUpdate = [&](float dt) { log.push_back(dt == 0.0f ? "update0" : "update"); };
        loop.OnLateUpdate = [&](float dt) { log.push_back(dt == 0.0f ? "late0" : "late"); };

        loop.BeginFrame(0.05);
        CHECK(Join(log) == "fixed fixed update late");        // fixed steps, then update, then late
        log.clear();
        Game::SetPaused(true);
        loop.BeginFrame(0.05);
        CHECK(Join(log) == "pause update0 late0");            // frozen: no fixed steps, dt 0, still ticked (UI)
        CHECK(loop.FixedStepsThisFrame() == 0 && Time::DeltaTime() == 0.0f && Time::UnscaledDeltaTime() > 0.0f);
        log.clear();
        loop.BeginFrame(0.05);
        CHECK(Join(log) == "update0 late0");                  // no second "pause"
        log.clear();
        Game::SetPaused(false);
        loop.BeginFrame(0.05);
        CHECK(log.size() >= 4 && log[0] == "resume" && log[1] == "fixed" && log[log.size() - 2] == "update" && log.back() == "late");
        log.clear();
        // two reasons: the loop sees one pause and one resume
        Game::PushPause("menu"); Game::PushPause("cutscene");
        loop.BeginFrame(0.016);
        Game::PopPause("menu");
        loop.BeginFrame(0.016);
        Game::PopPause("cutscene");
        loop.BeginFrame(0.016);
        int pauses = 0, resumes = 0;
        for (auto& s : log) { if (s == "pause") ++pauses; if (s == "resume") ++resumes; }
        CHECK(pauses == 1 && resumes == 1);
        // a game that starts paused (focus lost before the first frame) still announces it
        Fresh();
        loop.Reset();
        log.clear();
        Game::SetPaused(true);
        loop.BeginFrame(0.016);
        CHECK(!log.empty() && log[0] == "pause");
    }
    // callbacks are all optional
    {
        Fresh();
        GameLoop loop;
        for (int i = 0; i < 20; ++i) loop.BeginFrame(0.016);
        CHECK(loop.TotalFixedSteps() >= 15);                  // still accounted, just nobody listening
        Game::SetPaused(true);
        loop.BeginFrame(0.016);
        Game::SetPaused(false);
        loop.BeginFrame(0.016);
        CHECK(loop.FrameCount() == 22);
    }

    // ---- quit: the frame finishes, BeginFrame says stop --------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        bool lateRan = false;
        loop.OnUpdate = [&](float) { Game::RequestQuit("script quit", 7); };
        loop.OnLateUpdate = [&](float) { lateRan = true; };
        CHECK(!loop.BeginFrame(0.016));                       // false: stop
        CHECK(lateRan);                                       // the quit frame was run to the end
        CHECK(loop.QuitRequested() && loop.ExitCode() == 7);
        CHECK(Game::QuitReason() == "script quit");
        Fresh();
        GameLoop b;
        CHECK(b.BeginFrame(0.016));
        b.Stop();
        CHECK(b.QuitRequested() && !b.BeginFrame(0.016));
        b.Reset();
        CHECK(!b.QuitRequested() && b.BeginFrame(0.016));
        // a guard that vetoes keeps the loop going
        Fresh();
        Game::SetQuitGuard([] { return false; });
        GameLoop c;
        c.OnUpdate = [&](float) { Game::RequestQuit("window closed"); };
        CHECK(c.BeginFrame(0.016) && c.BeginFrame(0.016));
        Fresh();
    }

    // ---- the fixed clock: Time::InFixedStep / FixedTime ----------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        std::vector<double> starts;
        bool allIn = true;
        loop.OnFixedUpdate = [&](float) { starts.push_back(Time::FixedTime()); if (!Time::InFixedStep()) allIn = false; };
        bool updateSawFixed = false;
        loop.OnUpdate = [&](float) { if (Time::InFixedStep()) updateSawFixed = true; };
        for (int i = 0; i < 50; ++i) loop.BeginFrame(0.03);
        CHECK(allIn && !updateSawFixed && !Time::InFixedStep());
        CHECK(starts.size() == loop.TotalFixedSteps());
        bool ok = true;
        for (std::size_t k = 0; k < starts.size(); ++k) if (std::fabs(starts[k] - (double)k * 0.02) > 1e-6) ok = false;
        CHECK(ok);                                            // the k-th step starts at k * step
        CHECK_NEAR(Time::FixedTime(), (double)starts.size() * 0.02, 1e-6);
        // an exception in a fixed update does not leave Time inside a step
        loop.OnFixedUpdate = [](float) { throw std::runtime_error("boom"); };
        bool threw = false;
        try { loop.BeginFrame(0.05); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw && !Time::InFixedStep());
        // Time::FixedTime advances even with no OnFixedUpdate set
        Fresh();
        GameLoop quiet;
        for (int i = 0; i < 10; ++i) quiet.BeginFrame(0.02);
        CHECK_NEAR(Time::FixedTime(), (double)quiet.TotalFixedSteps() * 0.02, 1e-6);
    }

    // ---- fixed step options ------------------------------------------------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop::Options o; o.fixedStep = 0.01f; o.maxFixedSteps = 3;
        GameLoop loop(o);
        loop.BeginFrame(0.016);
        CHECK(loop.FixedStepsThisFrame() == 1 && std::fabs(loop.FixedStep() - 0.01f) < 1e-9f);
        loop.BeginFrame(0.1);
        CHECK(loop.FixedStepsThisFrame() == 3);               // the cap
        o.maxFixedSteps = 0;                                  // nonsense: at least one
        loop.SetOptions(o);
        loop.BeginFrame(0.1);
        CHECK(loop.FixedStepsThisFrame() == 1);
        // the default follows Time::FixedDeltaTime, also when it changes
        Fresh();
        GameLoop d;
        Time::SetFixedDeltaTime(0.05f);
        d.BeginFrame(0.05);
        CHECK(d.FixedStepsThisFrame() == 1 && std::fabs(d.FixedStep() - 0.05f) < 1e-9f);
        Time::SetFixedDeltaTime(0.02f);
    }

    // ---- slow motion slows the fixed steps; a hit stop stops them; pause stops them -------------------------------------------------------------
    {
        Fresh();
        Time::SetTimeScale(0.5f);
        GameLoop loop;
        for (int i = 0; i < 100; ++i) loop.BeginFrame(0.016);
        CHECK(loop.TotalFixedSteps() >= 39 && loop.TotalFixedSteps() <= 40);
        Fresh();
        GameLoop h;
        Time::HitStop(0.1f, 0.0f);
        int stoppedSteps = 0, stoppedFrames = 0;
        for (int i = 0; i < 5; ++i) { h.BeginFrame(0.02); stoppedSteps += h.FixedStepsThisFrame(); ++stoppedFrames; }
        CHECK(stoppedSteps == 0 && stoppedFrames == 5);       // 5 frames x 0.02 = the whole 0.1 s freeze
        CHECK(Clock::Get("ui").Delta() > 0.0f);               // while the ui clock kept running
        for (int i = 0; i < 5; ++i) h.BeginFrame(0.02);
        CHECK(h.TotalFixedSteps() == 5);                      // then 0.1 s of normal time: 5 steps
        Fresh();
    }

    // ---- the clocks are ticked once a frame ------------------------------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        Game::SetPaused(true);
        for (int i = 0; i < 5; ++i) loop.BeginFrame(0.02);
        CHECK(Clock::Get("game").Elapsed() == 0.0);
        CHECK_NEAR(Clock::Get("ui").Elapsed(), 0.1, 1e-6);
        CHECK(Clock::Get("ui").Ticks() == 5);
        Fresh();
        GameLoop::Options o; o.tickClocks = false;
        GameLoop off(o);
        off.BeginFrame(0.02);
        CHECK(Clock::Get("ui").Ticks() == 0);
        Fresh();
    }

    // ---- smoothing option --------------------------------------------------------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop::Options o; o.smoothDelta = true;
        GameLoop loop(o);
        loop.BeginFrame(0.010); loop.BeginFrame(0.030); loop.BeginFrame(0.010); loop.BeginFrame(0.030);
        CHECK_NEAR(loop.FrameDeltaStepped(), 0.02, 1e-4);     // averaged over the last four
        CHECK_NEAR(Time::UnscaledDeltaTime(), 0.02, 1e-4);
        CHECK_NEAR(loop.FrameDeltaReal(), 0.03, 1e-6);        // the measured time is still reported
        Game::SetPaused(true);
        loop.BeginFrame(0.016);
        CHECK(Time::DeltaTime() == 0.0f);
        Fresh();
    }

    // ---- BeginSession: a clean slate for a new Play ----------------------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        int fixed = 0;
        loop.OnFixedUpdate = [&](float) { ++fixed; };
        Time::SetTimeScale(0.1f);
        Time::HitStop(9.0f, 0.0f);
        Game::PushPause("menu");
        Game::RequestQuit("x", 4);
        loop.BeginFrame(0.5);
        loop.BeginFrame(0.016);
        loop.BeginSession();
        CHECK(loop.FrameCount() == 0 && loop.HitchCount() == 0 && loop.TotalFixedSteps() == 0 && loop.ClampedFrames() == 0);
        CHECK(loop.WorstHitch() == 0.0f && loop.DroppedFixedTime() == 0.0 && !loop.QuitRequested());
        CHECK(!Game::Paused() && !Game::QuitRequested() && Game::ExitCode() == 0);
        CHECK(Time::TimeScale() == 1.0f && !Time::HitStopActive() && Time::FixedTime() == 0.0);
        CHECK(Clock::Get("ui").Elapsed() == 0.0);
        const int before = fixed;
        loop.BeginFrame(0.02);
        CHECK(fixed == before + 1);                           // callbacks survive a session reset
    }

    // ---- property: the accumulator never loses or invents time -----------------------------------------------------------------------------------------
    {
        const float scales[] = {1.0f, 0.5f, 2.0f, 0.25f};
        const float steps[] = {0.02f, 0.01f, 1.0f / 60.0f};
        const int caps[] = {1, 3, 5};
        int framesChecked = 0;
        bool capOk = true, alphaOk = true, conserveOk = true, withinOneStep = true;
        for (float scale : scales) for (float step : steps) for (int cap : caps) {
            Fresh();
            Time::SetTimeScale(scale);
            GameLoop::Options o; o.fixedStep = step; o.maxFixedSteps = cap;
            GameLoop loop(o);
            double scaledSum = 0.0, realSum = 0.0;
            bool anyDropped = false;
            for (int i = 0; i < 400; ++i) {
                // mostly short frames, some long ones, some zero
                const float r = Rand01();
                const double real = r < 0.1 ? 0.0 : (r < 0.8 ? Rand01() * 0.03 : Rand01() * 0.5);
                loop.BeginFrame(real);
                ++framesChecked;
                scaledSum += (double)Time::DeltaTime();
                realSum += (double)loop.FrameDeltaClamped();
                if (loop.FixedStepsThisFrame() > cap) capOk = false;
                if (!(loop.Alpha() >= 0.0f && loop.Alpha() < 1.0f)) alphaOk = false;
                const double simulated = (double)loop.TotalFixedSteps() * (double)step + (double)loop.Alpha() * (double)step + loop.DroppedFixedTime();
                if (std::fabs(simulated - scaledSum) > 1e-3) conserveOk = false;   // nothing lost, nothing invented
                if (loop.DroppedFixedTime() > 0.0) anyDropped = true;
                if (!anyDropped) {                            // with nothing dropped: within one step of the real (scaled) time
                    if (std::fabs((double)loop.TotalFixedSteps() * (double)step - scaledSum) > (double)step + 1e-3) withinOneStep = false;
                }
            }
            (void)realSum;
        }
        CHECK(framesChecked == 4 * 3 * 3 * 400);
        CHECK(capOk);
        CHECK(alphaOk);
        CHECK(conserveOk);
        CHECK(withinOneStep);
        Fresh();
    }

    // ---- determinism: the same frame times give the same callbacks ----------------------------------------------------------------------------------------
    {
        auto run = [] {
            Fresh();
            g_s = 777u;
            GameLoop loop;
            std::vector<std::string> trace;
            loop.OnFixedUpdate = [&](float) { trace.push_back("f"); };
            loop.OnUpdate = [&](float dt) { trace.push_back("u" + std::to_string((int)(dt * 1e6))); };
            loop.OnLateUpdate = [&](float) { trace.push_back("l"); };
            for (int i = 0; i < 300; ++i) {
                if (i == 100) Game::SetPaused(true);
                if (i == 150) Game::SetPaused(false);
                loop.BeginFrame(Rand01() * 0.05);
            }
            return Join(trace);
        };
        const std::string a = run(), b = run();
        CHECK(!a.empty() && a == b);
        Fresh();
    }

    Fresh();
    TEST_MAIN_RESULT();
}
