// The time upgrade (docs/time_upgrade.md), checked headless with fake frame times:
//   whole-nanosecond clocks, clocks that follow clocks, the fixed-step overflow policy, the scheduler's
//   NextFrame / At / EndOfFrame / jitter / first delay / saved timers, OkayScript timers on the scheduler,
//   the day and season clock with events, the Time settings, the frame limiter and the physics alpha.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/Clock.hpp"
#include "okay/Core/FramePacing.hpp"
#include "okay/Core/FrameOverlay.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Scheduler.hpp"
#include "okay/Core/SaveFramework.hpp"
#include "okay/Core/TimeSettings.hpp"
#include "okay/Core/TimeSnapshot.hpp"
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;

namespace {
struct Lcg {
    std::uint64_t s = 99;
    double Next01() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return (double)(s >> 11) / 9007199254740992.0; }
};
void Fresh() { Time::ResetForTesting(); Game::Reset(); ClockSet::Global().Reset(); }
}

int main() {
    RUN_SUITE("time_upgrade");

    // ---- 1. Whole nanoseconds -------------------------------------------------------------------
    {
        Fresh();
        for (int i = 0; i < 216000; ++i) Time::StepD(1.0 / 60.0);   // one hour of 60 fps frames
        const std::int64_t exact = 3600LL * 1000000000LL;
        std::printf("[ticks] 1 h of StepD(1/60): %lld ns (exact %lld), ElapsedTimeD %.9f\n", (long long)Time::ElapsedTicks(), (long long)exact, Time::ElapsedTimeD());
        CHECK(std::llabs(Time::ElapsedTicks() - exact) <= 2);
        CHECK(std::llabs(Time::UnscaledTicks() - exact) <= 2);
        CHECK_NEAR(Time::ElapsedTimeD(), 3600.0, 1e-8);
        // The float path (Step) still works and still counts what it was given.
        Fresh();
        for (int i = 0; i < 1000; ++i) Time::Step(0.25f);
        CHECK(Time::ElapsedTicks() == 250000000000LL);
        CHECK(Time::ElapsedTimeD() == 250.0);
        // 100 hours at 144 fps: still within a nanosecond of exact, no float anywhere in the sum.
        Fresh();
        const long long frames = 144LL * 3600 * 100;
        for (long long i = 0; i < frames; ++i) Time::StepD(1.0 / 144.0);
        const std::int64_t want = 360000LL * 1000000000LL;
        std::printf("[ticks] 100 h of StepD(1/144): off by %lld ns\n", (long long)(Time::ElapsedTicks() - want));
        CHECK(std::llabs(Time::ElapsedTicks() - want) <= 5);
        // Bad and huge numbers never overflow the integer.
        Fresh();
        Time::StepD(1e300); Time::StepD(std::nan("")); Time::StepD(-5.0);
        for (int i = 0; i < 100; ++i) Time::StepD(1e30);
        CHECK(Time::ElapsedTicks() > 0 && Time::ElapsedTicks() <= 9000000000000000000LL);
        CHECK(std::isfinite(Time::ElapsedTimeD()));
        Fresh();
    }

    // ---- 2. GameLoop feeds exact doubles -----------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        for (int i = 0; i < 216000; ++i) loop.BeginFrame(1.0 / 60.0);
        std::printf("[loop] 1 h of 1/60 frames: game time %.6f s\n", Time::ElapsedTimeD());
        CHECK(std::fabs(Time::ElapsedTimeD() - 3600.0) < 1e-4);   // it was 3600.0002 with the float step (see time_sim)
        Fresh();
    }

    // ---- 3. Clocks that follow clocks --------------------------------------------------------------
    {
        ClockSet set;
        Clock& slow = set.Add("slowmo", Clock::Source::Unscaled);
        slow.SetParent("game");
        slow.SetScale(0.5f);
        Clock& grand = set.Add("grand", Clock::Source::Unscaled);
        grand.SetParent("slowmo");
        CHECK(slow.GetSource() == Clock::Source::Parent && slow.Parent() == "game");
        set.Get("game").SetScale(0.5f);
        for (int i = 0; i < 60; ++i) set.Tick(1.0 / 60.0, 1.0 / 60.0, false);
        std::printf("[clocks] game %.4f  slowmo %.4f  grand %.4f  ui %.4f after 1 s (game x0.5, slowmo x0.5 under it)\n",
                    set.Get("game").Elapsed(), slow.Elapsed(), grand.Elapsed(), set.Get("ui").Elapsed());
        // "game" follows the SCALED time (the global scale is 1 here), then its own x0.5.
        CHECK_NEAR(set.Get("game").Elapsed(), 0.5, 1e-9);
        CHECK_NEAR(slow.Elapsed(), 0.25, 1e-9);        // half of what game advanced
        CHECK_NEAR(grand.Elapsed(), 0.25, 1e-9);       // follows slowmo at x1
        CHECK_NEAR(set.Get("ui").Elapsed(), 1.0, 1e-9);   // not under game: runs on
        CHECK(std::llabs(set.Get("ui").ElapsedTicks() - 1000000000LL) <= 2);
        // A pause on the parent stops the whole chain; the ui clock still runs.
        set.Get("game").Pause();
        const double before = slow.Elapsed();
        for (int i = 0; i < 30; ++i) set.Tick(1.0 / 60.0, 1.0 / 60.0, false);
        CHECK(slow.Elapsed() == before && grand.Elapsed() == before);
        CHECK(set.Get("ui").Elapsed() > 1.4);
        set.Get("game").Resume();
        // The order the clocks were added does not matter: a child added BEFORE its parent still reads it this frame.
        ClockSet s2;
        Clock& child = s2.Add("child", Clock::Source::Unscaled);
        Clock& parent = s2.Add("parent", Clock::Source::Unscaled);
        child.SetParent("parent"); parent.SetScale(2.0f);
        s2.Tick(0.1, 0.1, false);
        CHECK_NEAR(child.DeltaD(), 0.2, 1e-12);
        // A loop in the chain is cut (no hang, no NaN); a missing parent reads real time; self-parent is ignored.
        ClockSet s3;
        Clock& a = s3.Add("a"); Clock& b = s3.Add("b");
        a.SetParent("b"); b.SetParent("a");
        s3.Tick(0.1, 0.1, false);
        CHECK(std::isfinite(a.Delta()) && std::isfinite(b.Delta()));
        Clock& orphan = s3.Add("orphan"); orphan.SetParent("nobody");
        s3.Tick(0.1, 0.1, false);
        CHECK_NEAR(orphan.DeltaD(), 0.1, 1e-12);
        orphan.SetParent("orphan");
        CHECK(orphan.Parent() == "nobody");
        orphan.SetParent("");
        CHECK(orphan.GetSource() == Clock::Source::Unscaled);
        // The save game keeps the parent.
        ClockSet s4;
        s4.Add("fx").SetParent("game");
        s4.Get("fx").SetScale(0.25f);
        TimeSnapshot snap = TimeSnapshot::Capture(s4);
        SaveBlock blk; snap.Write(SaveWriter(blk));
        TimeSnapshot back; CHECK(TimeSnapshot::Read(SaveReader(blk), back));
        ClockSet s5;
        back.Apply(s5, TimeSnapshot::ApplyOptions{});
        CHECK(s5.Find("fx") && s5.Find("fx")->Parent() == "game" && s5.Find("fx")->Scale() == 0.25f);
    }

    // ---- 4. The fixed step's overflow policy -------------------------------------------------------
    {
        // A 0.5 s hitch against 50 Hz and 5 steps: Drop loses 0.4 s for good, Spread pays it back.
        FixedTimestep drop(0.02f, 5), spread(0.02f, 5);
        spread.SetOverflow(FixedTimestep::Overflow::Spread);
        spread.SetMaxOwed(1.0);
        int dropSteps = drop.AdvanceD(0.5), spreadSteps = spread.AdvanceD(0.5);
        CHECK(dropSteps == 5 && spreadSteps == 5);
        CHECK_NEAR(drop.DroppedTime(), 0.4, 1e-6);
        CHECK(spread.DroppedTime() == 0.0 && spread.Owed() > 0.37);
        int dropAfter = 0, spreadAfter = 0, worst = 0;
        for (int i = 0; i < 120; ++i) {
            dropAfter += drop.AdvanceD(0.02); const int n = spread.AdvanceD(0.02); spreadAfter += n; if (n > worst) worst = n;
        }
        std::printf("[policy] after a 0.5 s hitch, 120 normal frames: Drop ran %d steps, Spread ran %d (at most %d in a frame)\n", dropAfter, spreadAfter, worst);
        CHECK(dropAfter == 120);
        CHECK(spreadAfter == 120 + 20);          // the 0.4 s owed = 20 steps, paid back
        CHECK(worst <= 5);
        // What is owed beyond MaxOwed is dropped.
        FixedTimestep big(0.02f, 5); big.SetOverflow(FixedTimestep::Overflow::Spread); big.SetMaxOwed(0.2);
        big.AdvanceD(2.0);
        CHECK_NEAR(big.DroppedTime(), 2.0 - 0.1 - 0.2, 1e-6);
        // The loop option reaches it.
        Fresh();
        GameLoop::Options o; o.spreadFixedOverflow = true; o.maxOwedSeconds = 1.0f; o.maxDelta = 0.0f;   // no clamp: the loop sees the hitch
        GameLoop loop(o);
        loop.BeginFrame(0.02);
        loop.BeginFrame(0.5);
        CHECK(loop.FixedStepsThisFrame() == 5 && loop.DroppedFixedTime() == 0.0);
        Fresh();
    }

    // ---- 5. Scheduler ---------------------------------------------------------------------------------
    {
        Scheduler s;
        // NextFrame from inside a callback runs in the NEXT update, not the one that scheduled it.
        int runs = 0;
        std::function<void()> again = [&] { ++runs; if (runs < 1000) s.NextFrame(again); };
        s.NextFrame(again);
        s.Update(0.016f);
        CHECK(runs == 1);
        s.Update(0.016f);
        CHECK(runs == 2);
        s.Clear();
        // A callback that re-arms itself with Invoke(0) used to keep one Update running for ever: it is cut.
        Scheduler sf; long long spins = 0;
        std::function<void()> forever = [&] { ++spins; sf.Invoke(0.0f, forever); };
        sf.Invoke(0.0f, forever);
        sf.Update(0.016f);
        std::printf("[scheduler] a self re-arming Invoke(0) ran %lld times in one update, then the update ended\n", spins);
        CHECK(spins > 1 && spins <= Scheduler::kMaxSpawnedPerUpdate + 1);
        CHECK(sf.ActiveCount() >= 1);
        sf.Clear();
        // A timer scheduled in a callback gets that frame's share (documented): Invoke(0.1) made in a 0.4 s frame fires at once.
        Scheduler s2;
        int innerFired = 0;
        s2.Invoke(0.1f, [&] { s2.Invoke(0.3f, [&] { ++innerFired; }); });
        s2.Update(0.4f);
        CHECK(innerFired == 1);

        // Same due time: scheduling order. Earlier due time: earlier or same update.
        Scheduler s3; std::string order;
        s3.Invoke(0.5f, [&] { order += "a"; }); s3.Invoke(0.5f, [&] { order += "b"; }); s3.Invoke(0.2f, [&] { order += "c"; });
        s3.Update(0.3f); CHECK(order == "c");
        s3.Update(0.3f); CHECK(order == "cab");

        // Cancel from a callback: a task cancelled by an earlier one in the same update never runs.
        Scheduler s4; int bRuns = 0; Scheduler::Handle hb = 0;
        s4.Invoke(0.1f, [&] { s4.Cancel(hb); });
        hb = s4.Invoke(0.1f, [&] { ++bRuns; });
        s4.Update(0.2f);
        CHECK(bRuns == 0);

        // At(): an absolute moment on the game clock.
        Fresh();
        Scheduler s5; int at = 0;
        Time::StepD(10.0);
        s5.At(10.5, [&] { ++at; });
        s5.Update(0.4f); CHECK(at == 0);
        s5.Update(0.2f); CHECK(at == 1);
        s5.At(3.0, [&] { ++at; });         // already past: the next update
        s5.Update(0.0f); CHECK(at == 2);

        // EndOfFrame(): after every timer of the update that is running.
        Scheduler s6; std::string e;
        s6.Invoke(0.0f, [&] { e += "1"; s6.EndOfFrame([&] { e += "E"; }); });
        s6.Invoke(0.0f, [&] { e += "2"; });
        s6.Update(0.016f);
        CHECK(e == "12E");

        // firstDelay: InvokeRepeating("Spawn", 2, 0.5).
        Scheduler s7; std::vector<double> at7; double now = 0.0;
        Scheduler::Options fo; fo.firstDelay = 0.25f;
        s7.InvokeRepeating(0.1f, [&] { at7.push_back(now); }, -1, fo);
        for (int i = 0; i < 50; ++i) { now += 0.02; s7.Update(0.02f); }
        CHECK(at7.size() == 8);            // 0.25, 0.35, ... 0.95 (1.0 s run)
        CHECK_NEAR(at7[0], 0.26, 0.021);

        // Frame-rate independence: a 0.1 s repeating timer fires 10 times a second at 30, 60 and 144 fps.
        for (double fps : {30.0, 60.0, 144.0, 240.0}) {
            Scheduler s8; int n = 0;
            s8.InvokeRepeating(0.1f, [&] { ++n; });
            const int frames = (int)std::llround(fps * 60.0);   // one minute
            for (int i = 0; i < frames; ++i) s8.Update((float)(1.0 / fps));
            std::printf("[timer] 0.1 s repeat for 60 s at %.0f fps: %d runs (600 expected)\n", fps, n);
            CHECK(n >= 599 && n <= 600);
        }
        // A 5 s hitch runs a 0.1 s timer 50 times in that frame, then keeps its rhythm.
        Scheduler s9; int n9 = 0; s9.InvokeRepeating(0.1f, [&] { ++n9; });
        s9.Update(5.0f); CHECK(n9 == 49 || n9 == 50);   // 0.1f is a hair over 0.1, so the 50th run is a hair past 5 s
        s9.Update(0.1f); CHECK(n9 >= 50 && n9 <= 51);

        // Jitter: each wait is its period times a factor in [1 - j, 1 + j], the same every run.
        for (int rep = 0; rep < 2; ++rep) {
            Scheduler sj; sj.SetJitterSeed(7);
            std::vector<double> gaps; double last = 0.0, t = 0.0;
            Scheduler::Options jo; jo.jitter = 0.5f;
            sj.InvokeRepeating(1.0f, [&] { gaps.push_back(t - last); last = t; }, -1, jo);
            for (int i = 0; i < 100000; ++i) { t += 0.001; sj.Update(0.001f); }
            bool inRange = gaps.size() > 50;
            double mn = 9, mx = 0;
            for (std::size_t i = 1; i < gaps.size(); ++i) { mn = std::min(mn, gaps[i]); mx = std::max(mx, gaps[i]); if (gaps[i] < 0.49 || gaps[i] > 1.52) inRange = false; }
            CHECK(inRange);
            CHECK(mx - mn > 0.3);
            static std::vector<double> first;
            if (rep == 0) first = gaps; else CHECK(first == gaps);
        }

        // Timers that can be saved.
        Scheduler sv; int spawned = 0, stunned = 0;
        sv.RegisterAction("spawn", [&] { ++spawned; });
        sv.RegisterAction("unstun", [&] { ++stunned; });
        Scheduler::Options to; to.tag = "wave";
        sv.InvokeActionRepeating(2.0f, "spawn", 3, to);
        sv.InvokeAction(5.0f, "unstun");
        sv.Invoke(1.0f, [] {});             // plain code is not saved
        sv.Update(1.5f);
        SaveBlock blk; CHECK(sv.SaveState(SaveWriter(blk)) == 2);
        Scheduler sl; int spawned2 = 0, stunned2 = 0;
        sl.RegisterAction("spawn", [&] { ++spawned2; }); sl.RegisterAction("unstun", [&] { ++stunned2; });
        CHECK(sl.LoadState(SaveReader(blk)) == 2);
        CHECK(sl.CountByTag("wave") == 1);
        sl.Update(0.4f); CHECK(spawned2 == 0);
        sl.Update(0.2f); CHECK(spawned2 == 1);        // 2.0 s period, 1.5 s already run
        sl.Update(3.0f); CHECK(stunned2 == 1);        // 5 s - 1.5 s - 0.6 s = 2.9 s left
        Scheduler sm; sm.RegisterAction("spawn", [] {});
        CHECK(sm.LoadState(SaveReader(blk)) == 1 && sm.LastLoadSkipped() == 1);   // "unstun" has no code here
    }

    // ---- 6. OkayScript timers run on the scheduler ---------------------------------------------------
    {
        auto Run = [](const char* src, int frames, float dt, GameObject*& out, Scene& scene) {
            GameObject* go = scene.CreateGameObject("T");
            auto* sc = go->AddComponent<ScriptComponent>("okayscript");
            CHECK(sc->LoadSource(src));
            scene.Start();
            for (int i = 0; i < frames; ++i) scene.Update(dt);
            out = go;
        };
        {   // a timer cancelled by another callback of the same frame must not run
            Scene sc; GameObject* go = nullptr;
            Run("function start() { Invoke(\"a\", 0.1); Invoke(\"b\", 0.1); }\n"
                "function a() { move(1, 0); CancelInvoke(\"b\"); }\n"
                "function b() { move(0, 1); }\n", 5, 0.05f, go, sc);
            CHECK_NEAR(go->transform->localPosition.x, 1.0f, 1e-4f);
            CHECK_NEAR(go->transform->localPosition.y, 0.0f, 1e-4f);
        }
        {   // InvokeRepeating(fn, first, rate): runs at 0.25, 0.35, 0.45 in half a second
            Scene sc; GameObject* go = nullptr;
            Run("function start() { InvokeRepeating(\"t\", 0.25, 0.1); }\nfunction t() { move(1, 0); }\n", 10, 0.05f, go, sc);
            CHECK_NEAR(go->transform->localPosition.x, 3.0f, 1e-4f);
        }
        {   // after() and every() at 30 and 144 fps: the same number of runs
            for (float fps : {30.0f, 144.0f}) {
                Scene sc; GameObject* go = nullptr;
                Run("function start() { every(0.5, \"t\"); after(1.0, \"u\"); }\nfunction t() { move(1, 0); }\nfunction u() { move(0, 1); }\n",
                    (int)(fps * 3.0f), 1.0f / fps, go, sc);
                CHECK_NEAR(go->transform->localPosition.x, 6.0f, 1.01f);   // 6 runs in 3 s (the last may land on the edge)
                CHECK_NEAR(go->transform->localPosition.y, 1.0f, 1e-4f);
            }
        }
        {   // a callback that throws is reported and the timer keeps going; cancel_timers from a callback
            Scene sc; GameObject* go = nullptr;
            Run("function start() { every(0.1, \"t\"); }\nfunction t() { move(1, 0); cancel_timers(); }\n", 20, 0.1f, go, sc);
            CHECK_NEAR(go->transform->localPosition.x, 1.0f, 1e-4f);
        }
    }

    // ---- 7. The day, the season and the events -------------------------------------------------------
    {
        Fresh();
        Scene s("cal");
        s.CreateGameObject("Cam")->AddComponent<Camera>();
        auto* dn = s.CreateGameObject("Time")->AddComponent<DayNightCycle>();
        dn->dayLengthSeconds = 240.0f;     // a day is 4 minutes: 1 hour = 10 s
        dn->SetTime(23.0f);
        dn->daysPerSeason = 2;
        std::vector<int> hours, days, seasons; int rise = 0, set = 0;
        dn->onHour = [&](int h) { hours.push_back(h); };
        dn->onDay = [&](int d) { days.push_back(d); };
        dn->onSeason = [&](int se) { seasons.push_back(se); };
        dn->onSunrise = [&] { ++rise; }; dn->onSunset = [&] { ++set; };
        s.Start();
        // 72.52 hours (from 23:00 on day 1: to 23:31 on day 4) at 144 fps: 10 s per hour.
        const int frames = (int)std::lround(144.0 * 10.0 * 72.52);
        for (int i = 0; i < frames; ++i) s.Update(1.0f / 144.0f);
        std::printf("[calendar] 72.5 hours at 144 fps: day %d, %02d:%02d, %d hour events, sunrise %d, sunset %d, season %s\n",
                    dn->day, dn->HourOfDay(), dn->Minute(), (int)hours.size(), rise, set, dn->SeasonName());
        CHECK((int)hours.size() == 72);
        CHECK(hours.front() == 0);          // 23:00 -> 00:00 is the first
        for (std::size_t i = 1; i < hours.size(); ++i) CHECK(hours[i] == (hours[i - 1] + 1) % 24);
        CHECK(dn->day == 4 && dn->HourOfDay() == 23 && dn->Minute() == 31);
        CHECK((int)days.size() == 3 && days.front() == 2 && days.back() == 4);
        CHECK(rise == 3 && set == 3);
        CHECK(seasons.size() == 1 && seasons[0] == 1);   // two days a season: day 3 is Summer

        // Events reach scripts: every object gets a named event.
        Scene s2("evt");
        auto* dn2 = s2.CreateGameObject("Time")->AddComponent<DayNightCycle>();
        GameObject* listener = s2.CreateGameObject("L");
        auto* sc = listener->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function OnEvent_OnHour(h) { move(1, 0); }\nfunction OnEvent_OnDay(d) { move(0, d); }\n"));
        dn2->dayLengthSeconds = 24.0f; dn2->SetTime(22.5f);
        s2.Start();
        for (int i = 0; i < 180; ++i) s2.Update(1.0f / 60.0f);   // 3 s = 3 h: 23, 0, 1
        CHECK_NEAR(listener->transform->localPosition.x, 3.0f, 1e-4f);
        CHECK_NEAR(listener->transform->localPosition.y, 2.0f, 1e-4f);    // OnDay(2) once

        // A jump fires nothing; SkipToHour and AddHours fire what they pass.
        Scene s3("skip");
        auto* d3 = s3.CreateGameObject("Time")->AddComponent<DayNightCycle>();
        int h3 = 0, dd = 0; d3->onHour = [&](int) { ++h3; }; d3->onDay = [&](int) { ++dd; };
        d3->SetTime(10.0f); d3->SetTime(2.0f);
        CHECK(h3 == 0 && dd == 0 && d3->day == 1);
        d3->SkipToHour(6.0f);
        CHECK(h3 == 4 && dd == 0 && std::fabs(d3->Hour() - 6.0f) < 1e-4f);
        d3->SkipToHour(6.0f);              // already 6: a full day
        CHECK(dd == 1 && d3->day == 2 && h3 == 4 + 24);
        d3->AddHours(-2.0f);               // backwards: a jump
        CHECK(d3->day == 2 && std::fabs(d3->Hour() - 4.0f) < 1e-4f && h3 == 28);
        CHECK(d3->ClockText() == "04:00");

        // Pause, speed, and a clock that ignores the game pause.
        Fresh();
        Scene s4("clock");
        auto* d4 = s4.CreateGameObject("Time")->AddComponent<DayNightCycle>();
        d4->dayLengthSeconds = 24.0f; d4->SetTime(8.0f); d4->clockName = "ui";
        s4.Start();
        // The scene is updated with dt 0 (the game is paused) but the ui clock keeps running.
        for (int i = 0; i < 60; ++i) { Time::StepD(1.0 / 60.0); ClockSet::Global().Tick(0.0, 1.0 / 60.0, true); s4.Update(0.0f); }
        CHECK_NEAR(d4->Hour(), 9.0f, 0.01f);
        d4->clockName = ""; d4->speed = 2.0f;
        for (int i = 0; i < 60; ++i) s4.Update(1.0f / 60.0f);
        CHECK_NEAR(d4->Hour(), 11.0f, 0.01f);
        d4->speed = 0.0f;
        for (int i = 0; i < 60; ++i) s4.Update(1.0f / 60.0f);
        CHECK_NEAR(d4->Hour(), 11.0f, 0.01f);
        Fresh();

        // Saved: the new fields round-trip; a cycle with the defaults is written exactly as before.
        Scene s5("ser");
        GameObject* g = s5.CreateGameObject("Time");
        auto* d5 = g->AddComponent<DayNightCycle>();
        const std::string plain = SceneSerializer::SerializeObject(*g);
        CHECK(plain.find("daynight ") != std::string::npos);
        d5->day = 7; d5->daysPerSeason = 10; d5->speed = 3.0f; d5->sunriseHour = 5.5f; d5->sunsetHour = 19.0f; d5->clockName = "ui";
        const std::string txt = SceneSerializer::SerializeObject(*g);
        Scene s6("ser2");
        GameObject* c6 = SceneSerializer::InstantiateFromText(s6, txt);
        auto* d6 = c6 ? c6->GetComponent<DayNightCycle>() : nullptr;
        CHECK(d6 != nullptr);
        if (d6) CHECK(d6->day == 7 && d6->daysPerSeason == 10 && d6->speed == 3.0f && d6->sunriseHour == 5.5f && d6->sunsetHour == 19.0f && d6->clockName == "ui");
        // An old line (no calendar fields) loads with the defaults.
        Scene s7("old");
        GameObject* c7 = SceneSerializer::InstantiateFromText(s7, plain);
        auto* d7 = c7 ? c7->GetComponent<DayNightCycle>() : nullptr;
        CHECK(d7 && d7->day == 1 && d7->daysPerSeason == 30 && d7->speed == 1.0f && d7->clockName.empty());
    }

    // ---- 8. Time settings --------------------------------------------------------------------------------
    {
        TimeSettings def;
        std::ostringstream none; def.WriteLines(none);
        CHECK(none.str().empty() && def.IsDefault());          // a project that never used the page is written as before
        TimeSettings t;
        t.timeScale = 0.5f; t.maxFrameSeconds = 0.2f; t.maxCatchUpSeconds = 0.5f; t.spreadCatchUp = true; t.unfocusedFps = 30.0f;
        t.clocks = {{"slowmo", "game", 0.25f}, {"audio", "game", 1.0f}};
        std::ostringstream out; t.WriteLines(out);
        TimeSettings back;
        std::istringstream in(out.str()); std::string line;
        while (std::getline(in, line)) { const auto eq = line.find('='); CHECK(back.ParseLine(line.substr(0, eq), line.substr(eq + 1))); }
        CHECK(back == t);
        CHECK(!back.ParseLine("physics_hz", "50"));       // not a time line
        // Hostile values are pulled in or ignored.
        TimeSettings h;
        h.ParseLine("time_scale", "nan"); CHECK(h.timeScale == 1.0f);
        h.ParseLine("time_scale", "1e30"); CHECK(h.timeScale == Time::kMaxTimeScale);
        h.ParseLine("time_max_frame", "-4"); CHECK(h.maxFrameSeconds == 0.01f);
        h.ParseLine("time_unfocused_fps", "2"); CHECK(h.unfocusedFps == 5.0f);
        h.ParseLine("time_clock", "|game|1"); CHECK(h.clocks.empty());
        h.ParseLine("time_clock", "x|x|1"); CHECK(h.clocks.empty());
        h.ParseLine("time_clock", "a|b|zzz"); CHECK(h.clocks.size() == 1 && h.clocks[0].scale == 1.0f);
        h.ParseLine("time_clock", "a|c|2"); CHECK(h.clocks.size() == 1);   // duplicate name
        // It reaches the loop and the clocks.
        GameLoop::Options o; t.ApplyToLoop(o, 0.02f);
        CHECK(o.maxDelta == 0.2f && o.maxFixedSteps == 25 && o.spreadFixedOverflow);
        ClockSet set; t.ApplyClocks(set);
        CHECK(set.Find("slowmo") && set.Find("slowmo")->Parent() == "game" && set.Find("slowmo")->Scale() == 0.25f);
        TimeSettings std1; std1.clocks = TimeSettings::StandardClocks(); ClockSet s2; std1.ApplyClocks(s2);
        CHECK(s2.Count() == 6 && s2.Find("network") && s2.Find("network")->Parent().empty());
        // The default options keep the 0.1 s clamp and the 1/3 s catch-up the player and editor always had.
        GameLoop::Options d; def.ApplyToLoop(d, 0.02f);
        CHECK(d.maxDelta == 0.1f && d.maxFixedSteps == 17 && !d.spreadFixedOverflow);
    }

    // ---- 9. The frame limiter: measured with a fake clock ---------------------------------------------
    {
        // The player's old cap: sleep whole milliseconds of what is left, no spin, no carry.
        auto OldCapFps = [](double cap, double work) {
            Lcg rng; double total = 0.0; const int frames = 20000;
            for (int i = 0; i < frames; ++i) {
                const double remain = 1.0 / cap - work;
                double frame = work;
                if (remain > 0.0015) {
                    const unsigned ms = (unsigned)((remain - 0.0005) * 1000.0);
                    frame += (double)ms / 1000.0 + 0.0002 + 0.0010 * rng.Next01();   // the OS sleeps a bit over
                }
                total += frame;
            }
            return frames / total;
        };
        // The limiter: sleep until 1.5 ms before the deadline (the OS overshoots by up to 1 ms), spin for the rest.
        auto NewCapFps = [](double cap, double work) {
            Lcg rng; FrameLimiter lim(cap); double now = 0.0; lim.Plan(now); const int frames = 20000;
            const double start = now;
            for (int i = 0; i < frames; ++i) {
                now += work;
                const double wait = lim.Plan(now);
                if (wait > 0.0) {
                    const double deadline = now + wait;
                    if (wait > 0.0015) now += (wait - 0.0015) + 0.0010 * rng.Next01();   // sleeps (and overshoots)
                    if (now < deadline) now = deadline;                                  // spins to the deadline
                }
            }
            return frames / (now - start);
        };
        for (double cap : {30.0, 60.0, 75.0, 120.0, 144.0}) {
            const double oldFps = OldCapFps(cap, 0.002), newFps = NewCapFps(cap, 0.002);
            std::printf("[limiter] cap %.0f: old whole-ms sleep %.2f fps (%+.1f%%), hybrid %.3f fps (%+.2f%%)\n",
                        cap, oldFps, (oldFps / cap - 1.0) * 100.0, newFps, (newFps / cap - 1.0) * 100.0);
            CHECK(std::fabs(newFps / cap - 1.0) < 0.002);
        }
        CHECK(std::fabs(OldCapFps(120.0, 0.002) / 120.0 - 1.0) > 0.05);   // the old formula really was off by more than 5% at some caps
        // What the cap means with vsync and focus.
        CHECK(FrameLimiter::EffectiveTargetFps(0, true, 60, true, 0) == 0.0);
        CHECK(FrameLimiter::EffectiveTargetFps(60, true, 60, true, 0) == 0.0);       // vsync paces it
        CHECK(FrameLimiter::EffectiveTargetFps(144, true, 60, true, 0) == 0.0);
        CHECK(FrameLimiter::EffectiveTargetFps(30, true, 60, true, 0) == 30.0);
        CHECK(FrameLimiter::EffectiveTargetFps(60, false, 60, true, 0) == 60.0);     // no vsync: the cap is the only pacing
        CHECK(FrameLimiter::EffectiveTargetFps(0, true, 60, false, 20) == 20.0);     // power saving while not active
        CHECK(FrameLimiter::EffectiveTargetFps(15, true, 60, false, 20) == 15.0);
        CHECK(FrameLimiter::EffectiveTargetFps(0, true, 60, true, 20) == 0.0);       // active: not limited
        CHECK(FrameLimiter::EffectiveTargetFps(std::nan(""), true, 60, true, -1) == 0.0);
    }

    // ---- 10. Physics: the fixed alpha, the step, and the catch-up policy ---------------------------------
    {
        Fresh();
        Scene::SetProjectPhysicsRate(100.0f, true);
        CHECK_NEAR(Time::FixedDeltaTime(), 0.01f, 1e-7);       // one fixed step for everyone
        Scene::SetProjectPhysicsRate(50.0f, true);
        Scene::SetProjectPhysicsCatchUp(0.1f, false);
        Scene a("a"); a.UseFixedStepPhysics(true); a.Start();
        a.Update(0.5f);
        CHECK(a.PhysicsStepsLastFrame() == 5);                  // 0.1 s of catch-up at 50 Hz
        Scene::SetProjectPhysicsCatchUp(0.1f, true);
        Scene b("b"); b.UseFixedStepPhysics(true); b.Start();
        b.Update(0.5f);
        CHECK(b.PhysicsStepsLastFrame() == 5);
        int later = 0;
        for (int i = 0; i < 25; ++i) { b.Update(0.02f); later += b.PhysicsStepsLastFrame(); if (b.PhysicsStepsLastFrame() > 5) later = 9999; }
        std::printf("[physics] 0.5 s hitch then 25 normal frames: Spread ran %d steps (25 + the owed 20 = 45 expected, max 5 a frame)\n", later);
        CHECK(later == 44 || later == 45);
        // Time::FixedAlpha() is the alpha of the steps the scene ran.
        Scene::SetProjectPhysicsCatchUp(1.0f / 3.0f, false);
        Scene c("c"); c.UseFixedStepPhysics(true); c.Start();
        bool same = true, inRange = true; Lcg rng;
        for (int i = 0; i < 2000; ++i) {
            c.Update((float)(1.0 / (25.0 + 200.0 * rng.Next01())));
            if (Time::FixedAlpha() != c.PhysicsAlpha()) same = false;
            if (!(c.PhysicsAlpha() >= 0.0f && c.PhysicsAlpha() < 1.0f)) inRange = false;
        }
        CHECK(same && inRange);
        // InFixedStep() is true inside a component's FixedUpdate.
        struct Probe : Component { bool saw = false; void FixedUpdate(float) override { if (Time::InFixedStep()) saw = true; } };
        Scene d("d"); auto* pr = d.CreateGameObject("P")->AddComponent<Probe>(); d.UseFixedStepPhysics(true); d.Start();
        d.Update(0.05f);
        CHECK(pr->saw && !Time::InFixedStep());
        Scene::SetProjectPhysicsCatchUp(1.0f / 3.0f, false);
        Fresh();
    }

    // ---- 11. The overlay lines -------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        for (int i = 0; i < 10; ++i) loop.BeginFrame(1.0 / 60.0);
        ClockSet::Global().Add("fx").SetParent("game");
        Scheduler sch; sch.Invoke(2.0f, [] {});
        const std::vector<std::string> lines = FrameOverlay::TimeLines(loop, &sch);
        CHECK(lines.size() == 3);
        CHECK(lines[0].find("fixed 50 Hz") == 0);
        CHECK(lines[1].find("fx 1.00x < game") != std::string::npos);
        CHECK(lines[2].find("timers: 1, next in 2") == 0);
        ClockSet::Global().Remove("fx");
        Fresh();
    }

    // ---- 12. Slow motion with an ease -----------------------------------------------------------------------
    {
        for (double fps : {30.0, 60.0, 144.0, 7.0}) {
            Fresh();
            Time::SlowMotion(0.2f, 2.0f, 0.5f);
            double game = 0.0, real = 0.0; float lowest = 1.0f, prev = 1.0f; bool smooth = true;
            while (real < 3.0 - 1e-9) {
                Time::StepD(1.0 / fps);
                real += 1.0 / fps;
                if (real <= 2.0 + 1e-9) game += Time::DeltaTimeD();
                const float m = Time::EffectiveTimeScale();
                lowest = std::min(lowest, m);
                if (fps >= 30.0 && std::fabs(m - prev) > 0.2f) smooth = false;   // no sudden jump to the low speed
                prev = m;
            }
            // 0.5 s easing down (average 0.6), 1 s at 0.2, 0.5 s easing up (average 0.6): 0.3 + 0.2 + 0.3 = 0.8 game seconds in 2 real seconds.
            std::printf("[slowmo] ease at %.0f fps: %.4f game seconds in 2 real seconds (0.8 expected), lowest speed %.2f\n", fps, game, (double)lowest);
            CHECK_NEAR(game, 0.8, 0.01);
            CHECK(lowest >= 0.19f && lowest <= 0.35f);
            CHECK(smooth);
            CHECK(!Time::HitStopActive() && Time::EffectiveTimeScale() == 1.0f);
        }
        Fresh();
        Time::SlowMotion(0.5f, 1.0f, 100.0f);          // an ease longer than half the time is cut to half
        for (int i = 0; i < 60; ++i) Time::StepD(1.0 / 60.0);
        CHECK(!Time::HitStopActive() && std::isfinite(Time::ElapsedTimeD()));
        Fresh();
        Time::SlowMotion(0.0f, 1.0f, 0.25f); Time::SlowMotion(std::nanf(""), -1.0f, std::nanf(""));   // frozen with an ease; bad numbers ignored
        for (int i = 0; i < 120; ++i) Time::StepD(1.0 / 60.0);
        CHECK(std::isfinite(Time::ElapsedTimeD()) && Time::ElapsedTimeD() > 0.9 && Time::ElapsedTimeD() < 1.9);
        Fresh();
    }

    // ---- 13. The shipped example scene (examples/time/time_playground.okayscene) -------------------------------
    {
        Fresh();
        Scene s("example");
        std::string err;
        const bool loaded = SceneSerializer::LoadFromFile(s, std::string(OKAY_SOURCE_DIR) + "/examples/time/time_playground.okayscene", &err);
        if (!loaded) std::printf("  load failed: %s\n", err.c_str());
        CHECK(loaded);
        if (loaded) {
            s.UseFixedStepPhysics(true);
            s.runScripts = true;
            s.Start();
            GameObject* blue = s.Find("Game Time Spinner");
            GameObject* orange = s.Find("Real Time Spinner");
            DayNightCycle* day = s.FindObjectOfType<DayNightCycle>();
            CHECK(blue && orange && day && s.Find("Clock Text") && s.Find("Director"));
            if (blue && orange && day) {
                GameLoop::Options o; o.maxDelta = 0.0f;
                GameLoop loop(o);
                loop.OnUpdate = [&](float dt) { s.Update(dt); };
                loop.BeginSession();
                auto rot = [](GameObject* g) { return g->transform->localRotation; };
                auto turned = [](const Quat& p, const Quat& q) {   // the angle between two orientations, degrees
                    float d = std::fabs(p.x * q.x + p.y * q.y + p.z * q.z + p.w * q.w);
                    if (d > 1.0f) d = 1.0f;
                    return 2.0f * std::acos(d) * 57.29578f;
                };
                auto Frames = [&](int n, const std::vector<char>& keys) {
                    for (int i = 0; i < n; ++i) { Input::FeedKeys(i == 0 ? keys : std::vector<char>{}); loop.BeginFrame(1.0 / 60.0); }
                };
                Frames(30, {});
                Quat b0 = rot(blue), o0 = rot(orange);
                Frames(60, {});
                const float dBlue = turned(b0, rot(blue)), dOrange = turned(o0, rot(orange));
                std::printf("[example] one normal second: blue turned %.1f, orange %.1f degrees\n", (double)dBlue, (double)dOrange);
                CHECK(dBlue > 80.0f && dOrange > 80.0f && std::fabs(dBlue - dOrange) < 5.0f);   // both turn 90 degrees a second
                // 1: slow motion. The blue cube slows to a fifth in the hold, the orange one does not.
                Frames(1, {'1'});
                Frames(60, {});                  // 1 s in: eased down and holding
                Quat b1 = rot(blue), o1 = rot(orange);
                Frames(60, {});
                const float sBlue = turned(b1, rot(blue)), sOrange = turned(o1, rot(orange));
                std::printf("[example] slow motion second: blue turned %.1f, orange %.1f degrees\n", (double)sBlue, (double)sOrange);
                CHECK(sBlue < 25.0f && sOrange > 80.0f);
                // The slow motion ends by itself (3 s): normal speed again.
                Frames(120, {});
                Quat b2 = rot(blue);
                Frames(60, {});
                CHECK(turned(b2, rot(blue)) > 80.0f);
                // 3: pause. The blue cube stops, the orange one keeps turning, 3 again resumes.
                Frames(1, {'3'});
                Quat b3 = rot(blue), o3 = rot(orange);
                Frames(60, {});
                CHECK(Game::Paused() && turned(b3, rot(blue)) < 0.1f && turned(o3, rot(orange)) > 80.0f);
                Frames(1, {'3'});
                CHECK(!Game::Paused());
                // 4: sleep until 6:00 sends the sunrise event; the clock reads about 6:00.
                Frames(1, {'4'});
                CHECK(std::fabs(day->Hour() - 6.0f) < 0.2f);
                // 2: a hit stop freezes the blue cube for 0.15 s.
                Frames(2, {});
                Quat b4 = rot(blue);
                Frames(1, {'2'});
                Frames(6, {});                    // 0.1 s: inside the freeze
                CHECK(turned(b4, rot(blue)) < 3.0f);
                Frames(30, {});
                CHECK(turned(b4, rot(blue)) > 20.0f);
                Input::FeedKeys({});
            }
        }
        Fresh();
    }

    TEST_MAIN_RESULT();
}
