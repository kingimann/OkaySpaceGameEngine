// 1.1 loop and time, second audit: one regression case per bug found, plus edge cases that
// were checked and found fine (so they stay fine). Fake frame times, no sleeping.
#include "test_framework.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/RandomExtras.hpp"
#include "okay/Core/Scheduler.hpp"
#include "okay/Core/Stopwatch.hpp"
#include "okay/Core/Time.hpp"
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
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

// A string is well-formed UTF-8 with no raw control characters (what a log line or a UI label can show).
bool ValidText(const std::string& s) {
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7F) return false;
        int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + (std::size_t)n > s.size()) return false;
        for (int k = 1; k < n; ++k) if (((unsigned char)s[i + (std::size_t)k] >> 6) != 2) return false;
        i += (std::size_t)n;
    }
    return true;
}

std::weak_ptr<int> g_guardToken;
bool g_guardAliveAfterClear = false;

}  // namespace

int main() {
    RUN_SUITE("loop_audit2");
    Log::console = false;

    // ---- BUG: ClockSet::TickOncePerFrame ticked a second time in a frame GameLoop had already ticked ----------------
    {
        Fresh();
        GameLoop loop;
        loop.BeginFrame(0.016);
        ClockSet::Global().TickOncePerFrame();                // a component that cannot tell the loop already ticked
        CHECK(Clock::Get("ui").Ticks() == 1);
        CHECK_NEAR(Clock::Get("ui").Elapsed(), 0.016, 1e-6);  // it used to read 0.032
        loop.BeginFrame(0.016);
        ClockSet::Global().TickOncePerFrame();
        ClockSet::Global().TickOncePerFrame();
        CHECK(Clock::Get("ui").Ticks() == 2);
        // and a host with no loop still gets exactly one tick per stepped frame
        Fresh();
        Time::Step(0.01f); ClockSet::Global().TickOncePerFrame(); ClockSet::Global().TickOncePerFrame();
        Time::Step(0.01f); ClockSet::Global().TickOncePerFrame();
        CHECK(Clock::Get("ui").Ticks() == 2);
    }

    // ---- BUG: a callback that resets the loop or changes its options cut the frame's fixed steps short -----------------
    {
        Fresh();
        GameLoop loop;
        int calls = 0;
        std::vector<float> steps;
        loop.OnFixedUpdate = [&](float dt) { ++calls; steps.push_back(dt); if (calls == 1) loop.Reset(); };
        loop.BeginFrame(0.06);                                // three steps are due
        CHECK(calls == 3);                                    // Reset() zeroed the loop's own count: it used to stop after one
        Fresh();
        GameLoop b;
        int n = 0; std::vector<float> dts;
        b.OnFixedUpdate = [&](float dt) {
            dts.push_back(dt);
            if (++n == 1) { GameLoop::Options o = b.options(); o.fixedStep = 0.05f; b.SetOptions(o); }
        };
        b.BeginFrame(0.06);
        CHECK(dts.size() == 3);
        CHECK(dts.size() == 3 && dts[0] == dts[1] && dts[1] == dts[2]);   // every step of the frame has the length it was counted with
    }

    // ---- BUG: a throwing fixed update counted steps that never ran ------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        int calls = 0;
        loop.OnFixedUpdate = [&](float) { if (++calls == 2) throw std::runtime_error("boom"); };
        bool threw = false;
        try { loop.BeginFrame(0.06); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw && calls == 2);
        CHECK(loop.TotalFixedSteps() == 2);                   // it said 3
        CHECK_NEAR(Time::FixedTime(), 0.04, 1e-6);            // matches the steps that ran
        CHECK(!Time::InFixedStep());
        loop.OnFixedUpdate = nullptr;
        CHECK(loop.BeginFrame(0.02));                         // the loop is usable afterwards
    }

    // ---- BUG: a quit guard that replaces itself ran from a destroyed std::function --------------------------------------------------
    {
        Fresh();
        auto keep = std::make_shared<int>(7);
        g_guardToken = keep;
        g_guardAliveAfterClear = false;
        Game::SetQuitGuard([keep]() { Game::SetQuitGuard(nullptr); g_guardAliveAfterClear = !g_guardToken.expired(); return true; });
        keep.reset();                                         // the guard's own closure holds the only reference
        CHECK(Game::RequestQuit("x"));
        CHECK(g_guardAliveAfterClear);                        // the closure must outlive its own call
        CHECK(Game::QuitRequested());
        Fresh();
    }

    // ---- BUG: GameLoop::BeginSession dropped the pause without telling OnResume (audio stayed ducked) -------------------------------
    {
        Fresh();
        GameLoop loop;
        int pauses = 0, resumes = 0;
        loop.OnPause = [&] { ++pauses; };
        loop.OnResume = [&] { ++resumes; };
        Game::PushPause("menu");
        loop.BeginFrame(0.016);
        CHECK(pauses == 1 && resumes == 0);
        loop.BeginSession();                                  // the editor's Stop then Play: the pause is gone
        CHECK(resumes == 1);                                  // balanced with the OnPause above
        loop.BeginFrame(0.016);
        CHECK(pauses == 1 && resumes == 1);                   // and no stray pause or resume afterwards
        // a session that never paused announces nothing
        loop.BeginSession();
        CHECK(pauses == 1 && resumes == 1);
        // still paused after the reset (window unfocused, pause-on-focus-loss): no resume, and no second pause
        Fresh();
        GameLoop focus;
        int p2 = 0, r2 = 0;
        focus.OnPause = [&] { ++p2; };
        focus.OnResume = [&] { ++r2; };
        Game::SetPauseOnFocusLoss(true);
        Game::NotifyFocus(false);
        focus.BeginFrame(0.016);
        focus.BeginSession();
        focus.BeginFrame(0.016);
        CHECK(p2 == 1 && r2 == 0);
        Game::NotifyFocus(true);
        Game::SetPauseOnFocusLoss(false);
        Fresh();
    }

    // ---- BUG: BeginSession kept the previous scene's smoothing history ---------------------------------------------------------------
    {
        Fresh();
        GameLoop::Options o; o.smoothDelta = true;
        GameLoop loop(o);
        for (int i = 0; i < 4; ++i) loop.BeginFrame(0.1);
        loop.BeginSession();
        loop.BeginFrame(0.01);
        CHECK_NEAR(Time::UnscaledDeltaTime(), 0.01, 1e-6);    // it read 0.02: the old 0.1 s frames were averaged in
        Fresh();
    }

    // ---- BUG: a callback that pumps BeginFrame (a modal dialog) re-entered the frame -------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        int updates = 0;
        bool nestedResult = false;
        loop.OnUpdate = [&](float) { ++updates; if (updates == 1) nestedResult = loop.BeginFrame(0.5); };
        CHECK(loop.BeginFrame(0.016));
        CHECK(updates == 1 && loop.FrameCount() == 1 && Time::FrameCount() == 1);   // the nested call did not run a second frame
        CHECK(nestedResult);                                  // it still answers "keep going"
        Game::RequestQuit("bye");
        bool nested2 = true;
        loop.OnUpdate = [&](float) { nested2 = loop.BeginFrame(0.1); };
        CHECK(!loop.BeginFrame(0.016) && !nested2);           // and says stop once a quit is pending
        Fresh();
    }

    // ---- BUG: a full hit stop table threw away the strongest short effect for a weak short one ------------------------------------------
    {
        Fresh();
        Time::HitStop(0.05f, 0.0f);                           // the one that matters: a freeze, shortest-lived of all
        for (int i = 0; i < 7; ++i) Time::HitStop(5.0f, 0.5f);
        Time::HitStop(0.01f, 0.9f);                           // table full: this weaker, shorter one must not evict the freeze
        Time::Step(0.02f);
        CHECK(Time::DeltaTime() == 0.0f);                     // it was 0.01: the freeze had been evicted
        CHECK(Time::HitStopActive());
        // a stronger newcomer does take a slot from a dominated entry
        Fresh();
        for (int i = 0; i < 8; ++i) Time::HitStop(1.0f, 0.5f);
        Time::HitStop(0.5f, 0.0f);
        Time::Step(0.1f);
        CHECK(Time::DeltaTime() == 0.0f);
        Time::ClearEffects();
        Fresh();
    }

    // ---- BUG: dice error messages carried half a UTF-8 character or a raw control character ---------------------------------------------------
    {
        const char* bad[] = {"2d6\xC3\xA9", "\xE2\x82\xAC", "d\xF0\x9F\x8E\xB2", "2d6\x01", "1d20 \xFF", "3x4", "2d6+\x7F"};
        for (const char* t : bad) {
            DiceParse p = ParseDice(t);
            CHECK(!p.ok);
            CHECK(ValidText(p.message));
            CHECK(!p.message.empty());
        }
        DiceParse e = ParseDice("2d6\xC3\xA9");
        CHECK(e.error == DiceError::UnexpectedCharacter && e.position == 3);    // the byte offset is unchanged
        CHECK(e.message.find("\xC3\xA9") != std::string::npos);                 // the whole character is shown, not half
        // fuzz: whatever bytes come in, the message is printable and the parse never throws
        std::uint32_t s = 99u;
        bool allValid = true;
        for (int k = 0; k < 3000; ++k) {
            std::string t;
            const int len = (int)(s % 12u); s = s * 1664525u + 1013904223u;
            for (int i = 0; i < len; ++i) { t += (char)(s >> 24); s = s * 1664525u + 1013904223u; }
            DiceParse p = ParseDice(t);
            if (!p.ok && !ValidText(p.message)) allValid = false;
        }
        CHECK(allValid);
    }

    // ---- BUG: ShuffleBag.Add had no limit on total copies (100 entries x 100000 copies = 80 MB of slots) --------------------------------------------
    {
        ShuffleBag<int> bag;
        for (int i = 0; i < 100; ++i) bag.Add(i, 100000);
        CHECK(bag.Size() <= (std::size_t)(1u << 20));
        CHECK(!bag.Empty());
        Random rng(5);
        int v = -1;
        CHECK(bag.TryDraw(rng, v) && v >= 0 && v < 100);
        CHECK(bag.Entries() <= 100);
        // a normal bag is unchanged
        ShuffleBag<int> small;
        small.Add(1, 3); small.Add(2);
        CHECK(small.Size() == 4 && small.Entries() == 2);
    }

    // ---- BUG: a Stopwatch on a clock that stepped backwards read a negative time ------------------------------------------------------------------------------
    {
        double t = 10.0;
        Stopwatch sw([&] { return t; });
        sw.Start();
        t = 4.0;                                              // the source went backwards (a wall clock was set back)
        CHECK(sw.ElapsedSeconds() >= 0.0 && sw.Elapsed() >= 0.0f);
        sw.Stop();
        CHECK(sw.ElapsedSeconds() >= 0.0);
        t = 20.0;
        sw.Start();
        t = 21.5;
        CHECK_NEAR(sw.ElapsedSeconds(), 1.5, 1e-9);           // and it carries on counting from there
        double nan = std::numeric_limits<double>::quiet_NaN();
        Stopwatch bad([&] { return nan; });
        bad.Start();
        CHECK(!std::isnan(bad.ElapsedSeconds()));
    }

    // ---- BUG: FixedTimestep::Alpha() read above 1 after the step was shortened (the loop clamps; the class did not) ----------------------------------------
    {
        FixedTimestep f(0.1f, 5);
        f.Advance(0.09f);
        f.SetStep(0.01f);                                     // nine steps' worth of time is now carried
        CHECK(f.Alpha() >= 0.0f && f.Alpha() < 1.0f);         // it read 5.0
        CHECK(f.Advance(0.0f) <= 5);
        CHECK(f.Alpha() >= 0.0f && f.Alpha() < 1.0f);
    }

    // ---- BUG: "unscaled" timers ran slow in the frame a hit stop ended or a ramp moved the scale ----------------------------------------------------------------
    {
        Fresh();
        Time::HitStop(0.05f, 0.0f);
        for (int i = 0; i < 3; ++i) Time::Step(0.016f);       // 0.048 s of freeze
        Time::Step(0.016f);                                   // 0.002 s freeze, then 0.014 s running: the freeze ends mid-frame
        const float dt = Time::DeltaTime();
        CHECK_NEAR(dt, 0.014, 1e-5);
        const float legacy = dt / Time::EffectiveTimeScale(); // what Scene and Scheduler used to compute: the scale after the frame is 1
        CHECK_NEAR(legacy, 0.014, 1e-5);                      // ...which is not the 0.016 s the frame really lasted
        CHECK_NEAR(Time::UnscaledFor(dt), 0.016, 1e-6);       // now exact
        CHECK_NEAR(Time::UnscaledFor(dt * 2.0f), dt * 2.0f / Time::EffectiveTimeScale(), 1e-6);   // any other dt: the old rule
        // a ramp
        Fresh();
        Time::RampTimeScale(0.0f, 0.1f);
        Time::Step(0.05f);                                    // scale 1 -> 0.5 over the frame: average 0.75
        CHECK_NEAR(Time::DeltaTime(), 0.0375, 1e-5);
        CHECK_NEAR(Time::UnscaledFor(Time::DeltaTime()), 0.05, 1e-6);
        CHECK(std::fabs(Time::DeltaTime() / Time::EffectiveTimeScale() - 0.05f) > 0.01f);   // the old rule said 0.075
        // the Scheduler really uses it: an unscaled timer of 0.016 s fires in the frame the hit stop ends
        Fresh();
        Scheduler sch;
        int fired = 0;
        Scheduler::Options o; o.unscaled = true;
        sch.Invoke(0.0635f, [&] { ++fired; }, o);
        Time::HitStop(0.05f, 0.0f);
        for (int i = 0; i < 4; ++i) { Time::Step(0.016f); sch.Update(Time::DeltaTime()); }
        CHECK(fired == 1);                                    // four frames of 0.016 s: the old rule counted 0.014 s of the last one
        Fresh();
    }

    // ================= checked and found fine: these must stay fine =================

    // GameLoop accumulator: dt exactly equal to the step, tiny steps, huge step counts, a step changed mid-run
    {
        Fresh();
        GameLoop loop;
        int steps = 0;
        loop.OnFixedUpdate = [&](float) { ++steps; };
        for (int i = 0; i < 50; ++i) loop.BeginFrame(0.02);   // exactly one step per frame, every frame
        CHECK(steps == 50);
        Fresh();
        GameLoop tiny;
        for (int i = 0; i < 100000; ++i) tiny.BeginFrame(1e-5);   // 1 s in 10 microsecond frames
        CHECK(tiny.TotalFixedSteps() >= 49 && tiny.TotalFixedSteps() <= 50);
        CHECK(tiny.Alpha() >= 0.0f && tiny.Alpha() < 1.0f);
        Fresh();
        GameLoop::Options big; big.maxFixedSteps = INT_MAX;
        GameLoop huge(big);
        huge.BeginFrame(0.25);
        CHECK(huge.FixedStepsThisFrame() == 12 || huge.FixedStepsThisFrame() == 13);
        Fresh();
        GameLoop::Options o; o.fixedStep = 0.02f;
        GameLoop mid(o);
        double scaled = 0.0;
        bool alphaOk = true;
        for (int i = 0; i < 200; ++i) {
            if (i == 50) { o.fixedStep = 0.005f; mid.SetOptions(o); }
            if (i == 100) { o.fixedStep = 0.1f; mid.SetOptions(o); }
            mid.BeginFrame(0.016);
            scaled += Time::DeltaTime();
            if (!(mid.Alpha() >= 0.0f && mid.Alpha() < 1.0f)) alphaOk = false;
        }
        CHECK(alphaOk);
        CHECK(scaled > 3.1);
    }
    // paused during a hitch: the accumulator stands still and Alpha does not drift
    {
        Fresh();
        GameLoop loop;
        loop.BeginFrame(0.03);                                // one step, 0.01 carried
        const float a = loop.Alpha();
        Game::SetPaused(true);
        loop.BeginFrame(5.0);                                 // a five second stall while paused
        CHECK(loop.FixedStepsThisFrame() == 0 && loop.Alpha() == a);
        CHECK(loop.HitchCount() == 1);
        loop.BeginFrame(0.0);
        CHECK(loop.Alpha() == a);
        Game::SetPaused(false);
        loop.BeginFrame(0.01);                                // the carried 0.01 plus 0.01 is one more step
        CHECK(loop.FixedStepsThisFrame() == 1);
        Fresh();
    }
    // a callback that quits or pauses mid-frame: the frame completes, the next one sees the pause
    {
        Fresh();
        GameLoop loop;
        std::vector<std::string> log;
        loop.OnPause = [&] { log.push_back("pause"); };
        loop.OnFixedUpdate = [&](float) { log.push_back("fixed"); if (log.size() == 1) Game::PushPause("script"); };
        loop.OnUpdate = [&](float dt) { log.push_back(dt > 0.0f ? "update" : "update0"); };
        CHECK(loop.BeginFrame(0.04));
        CHECK(log.size() == 3 && log[0] == "fixed" && log[1] == "fixed" && log[2] == "update");   // both steps and the update still ran
        log.clear();
        CHECK(loop.BeginFrame(0.04));
        CHECK(!log.empty() && log[0] == "pause");
        Game::PopPause("script");
        Fresh();
    }
    // a quit guard that re-requests the quit, and one that throws
    {
        Fresh();
        int guardRuns = 0;
        Game::SetQuitGuard([&] { ++guardRuns; Game::RequestQuit("again", 9); return false; });
        CHECK(!Game::RequestQuit("first"));
        CHECK(guardRuns == 1 && !Game::QuitRequested() && Game::QuitVetoCount() == 1);
        Game::SetQuitGuard([] { throw std::runtime_error("bug in the guard"); return false; });
        CHECK(Game::RequestQuit("second", 2));
        CHECK(Game::QuitRequested() && Game::ExitCode() == 2);
        Fresh();
    }
    // pause reasons: duplicates, empty, absent, a thousand of them
    {
        Fresh();
        Game::PushPause("");
        CHECK(Game::IsPausedBy("unnamed") && Game::IsPausedBy(""));
        CHECK(Game::PopPause(""));
        CHECK(!Game::PopPause("never pushed") && !Game::Paused());
        for (int i = 0; i < 1000; ++i) Game::PushPause("r" + std::to_string(i));
        CHECK(Game::PauseReasons().size() == 1000 && Game::Paused());
        for (int i = 0; i < 999; ++i) Game::PopPause("r" + std::to_string(i));
        CHECK(Game::Paused() && Game::PauseReasons().size() == 1);
        Game::PopPause("r999");
        CHECK(!Game::Paused() && !Time::Frozen());
        Fresh();
    }
    // clocks: a NaN or negative scale, a paused channel, channels created while a set is read
    {
        Fresh();
        Clock& c = Clock::Get("probe");
        c.SetScale(std::numeric_limits<float>::quiet_NaN());
        CHECK(c.Scale() == 0.0f);
        c.SetScale(-3.0f);
        CHECK(c.Scale() == 0.0f);
        c.SetScale(1.0f);
        c.Pause();
        GameLoop loop;
        loop.BeginFrame(0.02);
        CHECK(c.Elapsed() == 0.0 && c.Delta() == 0.0f && c.SourceDelta() > 0.0f);
        ClockSet local;
        std::size_t seen = 0;
        for (const std::string& name : local.Names()) { local.Get(name + "-copy"); ++seen; }
        CHECK(seen == 3 && local.Count() == 6);
        local.Tick(0.01f, 0.02f, false);
        CHECK(local.Find("game-copy")->Ticks() == 1);
        ClockSet::Global().Remove("probe");
        Fresh();
    }
    // time effects: durations <= 0 and NaN, ramp to 0, ramp cut short by SetTimeScale
    {
        Fresh();
        Time::HitStop(0.0f); Time::HitStop(-1.0f); Time::HitStop(std::numeric_limits<float>::quiet_NaN());
        CHECK(!Time::HitStopActive());
        Time::RampTimeScale(0.0f, 0.1f);
        Time::Step(0.05f);
        Time::SetTimeScale(2.0f);
        CHECK(!Time::IsRamping() && Time::BaseTimeScale() == 2.0f);
        Time::RampTimeScale(0.0f, 0.1f);
        for (int i = 0; i < 4; ++i) Time::Step(0.05f);
        CHECK(Time::BaseTimeScale() == 0.0f && !Time::IsRamping() && Time::DeltaTime() == 0.0f);
        Time::RampTimeScale(std::numeric_limits<float>::quiet_NaN(), 0.0f);
        CHECK(Time::BaseTimeScale() == 0.0f);
        Fresh();
    }
    // dice: huge notation is rejected fast, modifiers cannot overflow, long input is bounded
    {
        const auto t0 = std::chrono::steady_clock::now();
        DiceParse p = ParseDice("1000000d1000000");
        CHECK(!p.ok && p.error == DiceError::TooManyDice);
        p = ParseDice("999999999d999999999");
        CHECK(!p.ok);
        p = ParseDice("99999999999999999999d6");
        CHECK(!p.ok && p.error == DiceError::NumberTooLarge);
        p = ParseDice(std::string(5000, '9'));
        CHECK(!p.ok && p.error == DiceError::TooLong);
        p = ParseDice("1000d100000+1000d100000+1000d100000+1000d100000+1000d100000+1000d100000+1000d100000+1000d100000+"
                      "1000d100000+1000d100000+1000d100000+1000d100000+1000d100000+1000d100000+1000d100000+1000d100000");
        CHECK(p.ok && p.expr.Max() == 1600000000 && p.expr.Min() == 16000);
        Random rng(3);
        const int roll = p.ok ? p.expr.Roll(rng) : 0;
        CHECK(roll >= 16000 && roll <= 1600000000);
        p = ParseDice("-1000000-1000000-1000000-1000000-1000000-1000000-1000000-1000000-1000000-1000000-1000000-1000000-1000000-1000000-1000000-1000000");
        CHECK(p.ok && p.expr.Min() == -16000000 && p.expr.Max() == -16000000);
        p = ParseDice("1d6+1d6+1d6+1d6+1d6+1d6+1d6+1d6+1d6+1d6+1d6+1d6+1d6+1d6+1d6+1d6+1d6");
        CHECK(!p.ok && p.error == DiceError::TooManyTerms);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        CHECK(ms < 500.0);
    }
    // weighted table and shuffle bag edge cases
    {
        Random rng(11);
        WeightedTable<int> t;
        CHECK(t.Draw(rng) == nullptr && t.DrawIndex(rng) == -1 && t.DrawDistinct(rng, 5).empty());
        t.Add(1, std::numeric_limits<float>::quiet_NaN());
        t.Add(2, std::numeric_limits<float>::infinity());
        t.Add(3, -4.0f);
        CHECK(t.Draw(rng) == nullptr);                        // nothing drawable
        t.Add(4, 3.0e38f); t.Add(5, 3.0e38f);                 // two near-maximal weights: the sum overflows a float, not the table
        int c4 = 0, c5 = 0;
        for (int i = 0; i < 400; ++i) { const int* v = t.Draw(rng); if (v && *v == 4) ++c4; else if (v && *v == 5) ++c5; }
        CHECK(c4 + c5 == 400 && c4 > 120 && c5 > 120);
        auto three = t.DrawDistinct(rng, 3);                  // only two can be drawn
        CHECK(three.size() == 2 && three[0] != three[1]);
        CHECK(t.DrawIndexWithout(rng) == -1 && t.Remaining() == 0);
        t.Refill();
        CHECK(t.Remaining() == 2);
        CHECK(t.DrawDistinct(rng, INT_MAX).size() == 2);
        ShuffleBag<int> bag;
        int v = 77;
        CHECK(!bag.TryDraw(rng, v) && v == 77 && bag.Draw(rng) == 0);
        bag.Add(5);
        for (int i = 0; i < 10; ++i) CHECK(bag.Draw(rng) == 5);   // one item: it repeats, there is nothing else
        bag.Add(6, 0); bag.Add(7, -3);
        CHECK(bag.Entries() == 1);
    }

    Fresh();
    TEST_MAIN_RESULT();
}
