// 1.1 third pass: Countdown and Cooldown (poll-style timers), Stopwatch::OnClock and per-clock
// time-scale ramps (Clock::RampScale). Made-up frame times only: no sleeping, no real clock.
#include "test_framework.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/Countdown.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/SaveFramework.hpp"
#include "okay/Core/Stopwatch.hpp"
#include "okay/Core/Time.hpp"
#include <cmath>
#include <cstdint>
#include <limits>

using namespace okay;

namespace {
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
bool Near(double a, double b, double tol = 1e-5) { return std::fabs(a - b) <= tol; }

std::uint32_t g_s = 4711u;
float Rand01() { g_s ^= g_s << 13; g_s ^= g_s >> 17; g_s ^= g_s << 5; return (float)(g_s >> 8) / 16777216.0f; }

void Fresh() {
    Game::SetPauseOnFocusLoss(false);
    Game::NotifyFocus(true);
    Game::Reset();
    Time::ResetForTesting();
    ClockSet::Global().Reset();
}
}  // namespace

int main() {
    RUN_SUITE("countdown");

    // ---- Countdown: fires once, on the tick that takes it to zero -------------------------------------------------------------
    {
        Countdown c;
        CHECK(c.GetState() == Countdown::State::Idle && !c.Done() && !c.Active() && !c.Running());
        CHECK(!c.Tick(1.0f) && !c.Done());                                   // idle: ticks do nothing
        c.Start(1.0f);
        CHECK(c.Active() && c.Running() && !c.Done());
        CHECK(Near(c.Remaining(), 1.0) && Near(c.Duration(), 1.0) && Near(c.Progress(), 0.0) && c.Elapsed() == 0.0f);
        CHECK(!c.Tick(0.25f) && Near(c.Remaining(), 0.75) && Near(c.Progress(), 0.25) && Near(c.Elapsed(), 0.25));
        CHECK(!c.Tick(0.5f));
        CHECK(c.Tick(0.5f));                                                 // 0.25 left, 0.5 given: it fires
        CHECK(c.Done() && !c.Active() && c.Remaining() == 0.0f && c.Progress() == 1.0f);
        CHECK(Near(c.Overshoot(), 0.25));                                    // the part of that dt it did not need
        CHECK(!c.Tick(1.0f) && c.Done());                                    // and only once
        c.Restart();
        CHECK(c.Active() && Near(c.Remaining(), 1.0) && c.Overshoot() == 0.0f);
        CHECK(c.Tick(1.0f) && c.Overshoot() == 0.0f);                        // exactly on the dot counts as arrived
        c.Stop();
        CHECK(c.GetState() == Countdown::State::Idle && c.Progress() == 0.0f && c.Remaining() == 0.0f);
        CHECK(Near(c.Duration(), 1.0));                                      // Stop remembers the duration
        c.Restart();
        CHECK(c.Active());
    }

    // ---- bad numbers --------------------------------------------------------------------------------------------------------
    {
        Countdown c(2.0f);
        CHECK(!c.Tick(kNaN) && !c.Tick(-1.0f) && !c.Tick(kInf) && !c.Tick(0.0f));   // none of them adds time
        CHECK(Near(c.Remaining(), 2.0));
        Countdown zero(0.0f);                                                // a zero-length timer fires on the next tick, whatever dt
        CHECK(zero.Active() && zero.Progress() == 1.0f);
        CHECK(zero.Tick(0.0f) && zero.Done());
        Countdown neg(-5.0f), nan(kNaN), inf(kInf), ninf(-kInf);
        CHECK(neg.Duration() == 0.0f && nan.Duration() == 0.0f && ninf.Duration() == 0.0f);
        CHECK(inf.Duration() == Countdown::kMaxSeconds);                     // a duration that is "forever" is the cap, not a hang
        Countdown big(1e30f);
        CHECK(big.Duration() == Countdown::kMaxSeconds && !big.Tick(1000.0f) && big.Active());
        CHECK(Countdown::kMaxSeconds > 3.0e7f);
        Countdown p(1.0f);
        p.Extend(kNaN); p.Extend(kInf); p.Extend(-kInf);
        CHECK(Near(p.Remaining(), 1.0));                                      // a bad number changes nothing
        CHECK(!std::isnan(p.Remaining()) && !std::isnan(p.Duration()));
    }

    // ---- pause, resume, extend ------------------------------------------------------------------------------------------------------
    {
        Countdown c(1.0f);
        c.Tick(0.4f);
        c.Pause();
        CHECK(c.Paused() && c.Active() && !c.Running());
        CHECK(!c.Tick(10.0f) && Near(c.Remaining(), 0.6));                   // paused: no time, no firing
        c.Resume();
        CHECK(c.Running() && !c.Tick(0.5f) && c.Tick(0.2f));
        // Extend lengthens a running timer and can shorten it, never below zero
        Countdown e(1.0f);
        e.Tick(0.5f);
        e.Extend(2.0f);
        CHECK(Near(e.Remaining(), 2.5) && Near(e.Duration(), 2.5));          // the progress bar does not jump past 1
        e.Extend(-10.0f);
        CHECK(e.Remaining() == 0.0f && e.Active());                          // 0 left but not fired yet: the next tick reports it
        CHECK(e.Tick(0.0f) && e.Done());
        e.Extend(5.0f);
        CHECK(e.Remaining() == 0.0f);                                        // not while idle or done
        Countdown started(3.0f);
        started.Pause();
        started.Start(1.0f);
        CHECK(!started.Paused());                                            // Start un-pauses
    }

    // ---- Consume: a jump buffer and coyote time -----------------------------------------------------------------------------------------
    {
        Countdown buffer;
        CHECK(!buffer.Consume());                                            // nothing buffered
        buffer.Start(0.12f);
        buffer.Tick(0.05f);
        CHECK(buffer.Consume() && !buffer.Active() && !buffer.Done());       // landed 0.05 s after the press: the jump happens, once
        CHECK(!buffer.Consume());
        buffer.Start(0.12f);
        CHECK(buffer.Tick(0.2f));                                            // pressed too early: it expired
        CHECK(!buffer.Consume());
    }

    // ---- a chain: the part of a tick that is left over goes to the next timer ------------------------------------------------------------------
    {
        Countdown first(0.1f), second(0.1f);
        bool a = false, b = false;
        // 0.25 s frames: the first ends 0.1 into it and the second runs for the 0.15 s that is left
        if (first.Tick(0.25f)) { a = true; if (second.Tick(first.Overshoot())) b = true; }
        CHECK(a && b && Near(second.Overshoot(), 0.05));
    }

    // ---- ticking by Clock: the clock's own scale and pause apply ---------------------------------------------------------------------------
    {
        ClockSet set;
        Clock& ui = set.Get("t_ui");
        ui.SetScale(0.5f);
        Countdown c(1.0f);
        Cooldown cd(1.0f); cd.Trigger();
        for (int i = 0; i < 4; ++i) { set.Tick(0.25f, 0.25f, false); c.Tick(ui); cd.Tick(ui); }   // 4 x 0.125 s of the clock's time
        CHECK(Near(c.Remaining(), 0.5) && Near(cd.Remaining(), 0.5));
        ui.Pause();
        set.Tick(0.25f, 0.25f, false); c.Tick(ui); cd.Tick(ui);
        CHECK(Near(c.Remaining(), 0.5) && Near(cd.Remaining(), 0.5));        // a paused clock gives Delta() 0
    }

    // ---- Cooldown ---------------------------------------------------------------------------------------------------------------------
    {
        Cooldown fire(0.25f);
        CHECK(fire.Ready() && fire.Remaining() == 0.0f && fire.Progress() == 1.0f);
        CHECK(fire.TryUse() && !fire.Ready() && Near(fire.Remaining(), 0.25) && fire.Progress() == 0.0f);
        CHECK(!fire.TryUse());                                               // still waiting
        fire.Tick(0.1f);
        CHECK(!fire.Ready() && Near(fire.Progress(), 0.4));
        fire.Tick(0.1f);
        CHECK(!fire.TryUse());
        fire.Tick(0.1f);
        CHECK(fire.Ready() && fire.Remaining() == 0.0f && fire.TryUse());
        // four shots a second really is four shots a second, whatever the frame length
        for (float frame : {1.0f / 144.0f, 1.0f / 60.0f, 1.0f / 30.0f, 0.1f}) {
            Cooldown gun(0.25f);
            int shots = 0;
            for (int i = 0; i < (int)(10.0f / frame); ++i) { gun.Tick(frame); if (gun.TryUse()) ++shots; }
            CHECK(shots >= 39 && shots <= 41);                               // 10 s at 4 a second: 40 (the cooldown credits the part of a frame it did not need)
        }
        // the credit is for the frame that made it ready; a weapon that sat ready gets the full wait
        Cooldown credit(0.25f); credit.Trigger();
        credit.Tick(0.3f);                                                   // ready 0.05 s into this tick
        CHECK(credit.TryUse() && Near(credit.Remaining(), 0.2));
        Cooldown sat(0.25f); sat.Trigger();
        sat.Tick(0.3f); sat.Tick(0.016f);                                    // ...and then it sat ready for another frame
        CHECK(sat.TryUse() && Near(sat.Remaining(), 0.25));
        Cooldown quick(0.01f); quick.Trigger();
        quick.Tick(0.05f);
        CHECK(quick.TryUse() && quick.Ready());                              // a wait shorter than the leftover is paid off at once, but only one use a call
        Cooldown h(2.0f);
        h.Trigger();
        h.Reduce(0.5f); h.Reduce(kNaN); h.Reduce(-3.0f);
        CHECK(Near(h.Remaining(), 1.5));
        h.Reduce(100.0f);
        CHECK(h.Ready());
        h.Trigger(); h.Reset();
        CHECK(h.Ready());
        h.Trigger();
        h.SetPaused(true);
        h.Tick(5.0f);
        CHECK(!h.Ready() && Near(h.Remaining(), 2.0));
        h.SetPaused(false);
        h.Tick(kNaN); h.Tick(-1.0f); h.Tick(kInf);
        CHECK(Near(h.Remaining(), 2.0));
        h.SetDuration(0.5f);
        CHECK(Near(h.Remaining(), 2.0) && Near(h.Duration(), 0.5));          // a wait that is running keeps its length
        Cooldown none;
        CHECK(none.Ready() && none.TryUse() && none.Ready() && none.Progress() == 1.0f);   // no duration: always ready
        Cooldown bad(kNaN), inf(kInf);
        CHECK(bad.Duration() == 0.0f && inf.Duration() == Cooldown::kMaxSeconds);
    }

    // ---- saving and loading --------------------------------------------------------------------------------------------------------------
    {
        Countdown c(5.0f);
        c.Tick(1.5f);
        Cooldown cd(3.0f);
        cd.Trigger(); cd.Tick(1.0f); cd.SetPaused(true);
        Countdown done(1.0f); done.Tick(2.0f);
        Countdown paused(4.0f); paused.Tick(1.0f); paused.Pause();

        SaveDocument doc;
        {
            SaveWriter w = doc.Writer("timers");
            c.Save(w.Scope("fuse")); cd.Save(w.Scope("gun")); done.Save(w.Scope("done")); paused.Save(w.Scope("paused"));
        }
        // through the real text form, as a slot would be
        SaveDocument back;
        CHECK(SaveDocument::Parse(doc.Serialize(), back));
        const SaveReader r = back.Reader("timers");
        Countdown c2; Cooldown cd2; Countdown done2, paused2;
        c2.Load(r.Scope("fuse")); cd2.Load(r.Scope("gun")); done2.Load(r.Scope("done")); paused2.Load(r.Scope("paused"));
        CHECK(c2.Active() && c2.Remaining() == c.Remaining() && c2.Duration() == c.Duration());   // bit for bit
        CHECK(cd2.Remaining() == cd.Remaining() && cd2.Duration() == cd.Duration() && cd2.Paused());
        CHECK(done2.Done() && done2.Overshoot() == done.Overshoot() && done2.Remaining() == 0.0f);
        CHECK(paused2.Active() && paused2.Paused() && paused2.Remaining() == paused.Remaining());
        // and they carry on from there
        CHECK(!c2.Tick(3.0f) && c2.Tick(1.0f));
        // an older save with nothing written leaves a timer as built; hostile values are cleaned
        Countdown keep(7.0f);
        keep.Load(SaveReader(SaveBlock()));
        CHECK(keep.Active() && Near(keep.Remaining(), 7.0));
        SaveBlock hostile;
        hostile.SetFloat("duration", -3.0f); hostile.SetFloat("remaining", 1e30f); hostile.SetInt("state", 99);
        Countdown h2(1.0f);
        h2.Load(SaveReader(hostile));
        CHECK(h2.Duration() == 0.0f && h2.Remaining() == 0.0f && !h2.Active() && !h2.Done());
        SaveBlock overRemaining;
        overRemaining.SetFloat("duration", 2.0f); overRemaining.SetFloat("remaining", 50.0f); overRemaining.SetInt("state", 1);
        Countdown h3;
        h3.Load(SaveReader(overRemaining));
        CHECK(h3.Active() && h3.Remaining() == 2.0f);                         // remaining cannot be more than the duration
    }

    // ---- Stopwatch::OnClock follows a named clock ------------------------------------------------------------------------------------------------
    {
        Fresh();
        Clock& world = Clock::Get("sw_world");
        Stopwatch sw = Stopwatch::OnClock("sw_world");
        Stopwatch ui = Stopwatch::OnClock("ui");
        sw.Start(); ui.Start();
        ClockSet::Global().Tick(0.5f, 0.5f, false);
        CHECK(Near(sw.ElapsedSeconds(), 0.5) && Near(ui.ElapsedSeconds(), 0.5));
        world.SetScale(2.0f);
        ClockSet::Global().Tick(0.5f, 0.5f, false);
        CHECK(Near(sw.ElapsedSeconds(), 1.5) && Near(ui.ElapsedSeconds(), 1.0));   // the scale of that clock only
        world.Pause();
        ClockSet::Global().Tick(0.5f, 0.5f, false);
        CHECK(Near(sw.ElapsedSeconds(), 1.5) && Near(ui.ElapsedSeconds(), 1.5));
        Stopwatch gone = Stopwatch::OnClock("no_such_clock_yet");
        gone.Start();
        CHECK(gone.ElapsedSeconds() == 0.0);                                  // an unknown name reads no time (and creates nothing)
        CHECK(!Clock::Exists("no_such_clock_yet"));
        ClockSet::Global().Remove("sw_world");
        CHECK(sw.ElapsedSeconds() >= 0.0);                                    // a removed clock does not crash the stopwatch
        ClockSet::Global().Reset();
    }

    // ---- Clock::RampScale: exact integral over any frame length --------------------------------------------------------------------------------
    {
        // a ramp from 1 to 0 over 1 s: the time it lets through is the area under the line, 0.5 s
        for (float frame : {0.001f, 0.01f, 0.016f, 0.1f, 0.25f, 0.4f, 1.0f, 3.0f}) {
            ClockSet set;
            Clock& c = set.Get("ramp");
            c.RampScale(0.0f, 1.0f);
            CHECK(c.Ramping() && Near(c.RampTarget(), 0.0) && Near(c.RampSecondsLeft(), 1.0));
            double total = 0.0;
            const int frames = (int)(3.0f / frame + 0.5f);
            for (int i = 0; i < frames; ++i) { set.Tick(frame, frame, false); total += (double)c.Delta(); }
            CHECK(Near(total, 0.5, 2e-4));
            CHECK(!c.Ramping() && c.Scale() == 0.0f);
        }
        // up: 0 -> 2 over 2 s, then 1 s at 2: area 2 + 2
        {
            ClockSet set;
            Clock& c = set.Get("up");
            c.SetScale(0.0f);
            c.RampScale(2.0f, 2.0f);
            double total = 0.0;
            for (int i = 0; i < 300; ++i) { set.Tick(0.01f, 0.01f, false); total += (double)c.Delta(); }
            CHECK(Near(total, 2.0 + 2.0, 1e-3));
            CHECK(Near(c.Elapsed(), total, 1e-6));                            // Elapsed counts what Delta gave
        }
        // SetScale takes over; a zero-length ramp is at once; bad numbers
        {
            ClockSet set;
            Clock& c = set.Get("r2");
            c.RampScale(0.0f, 10.0f);
            set.Tick(1.0f, 1.0f, false);
            CHECK(c.Scale() < 1.0f && c.Scale() > 0.8f);
            c.SetScale(1.0f);
            CHECK(!c.Ramping() && c.Scale() == 1.0f);
            set.Tick(1.0f, 1.0f, false);
            CHECK(Near(c.Delta(), 1.0));
            c.RampScale(0.25f, 0.0f);
            CHECK(!c.Ramping() && c.Scale() == 0.25f);
            c.RampScale(0.5f, kNaN);
            CHECK(!c.Ramping() && c.Scale() == 0.5f);
            c.RampScale(0.5f, -1.0f);
            CHECK(!c.Ramping());
            c.RampScale(kNaN, 1.0f);                                          // a NaN target is 0
            CHECK(c.Ramping() && c.RampTarget() == 0.0f);
            c.RampScale(1e9f, 1.0f);
            CHECK(c.RampTarget() == Time::kMaxTimeScale);                     // capped like every scale
            c.CancelRamp();
            CHECK(!c.Ramping() && c.RampSecondsLeft() == 0.0f);
            // a second ramp starts from where the first one is
            c.SetScale(1.0f);
            c.RampScale(0.0f, 2.0f);
            set.Tick(1.0f, 1.0f, false);                                      // now 0.5
            c.RampScale(1.0f, 1.0f);
            set.Tick(0.5f, 0.5f, false);                                      // 0.5 -> 0.75 over half a second: 0.3125 s
            CHECK(Near(c.Delta(), 0.5 * 0.5 * (0.5 + 0.75), 1e-5));
        }
        // a paused clock (its own pause, or the game's pause on a held clock) stands still, ramp included
        {
            ClockSet set;
            Clock& c = set.Get("r3");
            c.RampScale(0.0f, 1.0f);
            c.Pause();
            for (int i = 0; i < 10; ++i) set.Tick(0.5f, 0.5f, false);
            CHECK(c.Ramping() && Near(c.RampSecondsLeft(), 1.0) && c.Elapsed() == 0.0);
            c.Resume();
            set.Tick(0.5f, 0.5f, false);
            CHECK(Near(c.RampSecondsLeft(), 0.5));
            Clock& cin = set.Get("cinematic");                                 // holds while the game is paused
            cin.RampScale(0.0f, 1.0f);
            set.Tick(0.5f, 0.5f, true);
            CHECK(Near(cin.RampSecondsLeft(), 1.0));
            // Reset clears it
            c.Reset();
            CHECK(!c.Ramping() && c.Scale() == 1.0f);
        }
        // random frame lengths give the same total as one fine-grained run (property)
        {
            double worst = 0.0;
            for (int round = 0; round < 200; ++round) {
                ClockSet a, b;
                Clock& ca = a.Get("p"); Clock& cb = b.Get("p");
                const float target = Rand01() * 3.0f, secs = 0.2f + Rand01() * 2.0f, start = Rand01() * 3.0f;
                ca.SetScale(start); cb.SetScale(start);
                ca.RampScale(target, secs); cb.RampScale(target, secs);
                double ta = 0.0, tb = 0.0, sim = 0.0;
                while (sim < 5.0) {
                    const float f = 0.001f + Rand01() * 0.2f;
                    a.Tick(f, f, false); ta += (double)ca.Delta(); sim += (double)f;
                }
                // one fine run over the same simulated length
                for (double t = 0.0; t < sim - 1e-9; t += 0.001) { const float f = (float)std::fmin(0.001, sim - t); b.Tick(f, f, false); tb += (double)cb.Delta(); }
                worst = std::fmax(worst, std::fabs(ta - tb));
            }
            CHECK(worst < 5e-3);
        }
    }

    TEST_MAIN_RESULT();
}
