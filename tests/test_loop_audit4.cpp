// 1.1 fourth pass audit: the pause button while another system holds a pause, what a script sees across
// Stop and Play (session clocks), run-N-frames-then-pause, the debug time scale presets, and edge cases of
// Time, GameLoop, Clock ramps, TimeSnapshot, FrameRecord, TickPacer, Application pacing and Utf8. Every
// frame time is made up; nothing sleeps and no wall clock is read.
#include "test_framework.hpp"
#include "okay/Core/Application.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/FrameOverlay.hpp"
#include "okay/Core/FrameRecord.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Core/SaveFramework.hpp"
#include "okay/Core/TickPacer.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/TimeSnapshot.hpp"
#include "okay/Core/Utf8.hpp"
#include <climits>
#include <cmath>
#include <iostream>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

using namespace okay;

namespace {

constexpr float kFltMax = std::numeric_limits<float>::max();

void Fresh() {
    Game::SetPauseOnFocusLoss(false);
    Game::NotifyFocus(true);
    Game::Reset();
    Time::ResetForTesting();
    Time::SetFixedDeltaTime(0.02f);
    ClockSet::Global().Reset();
}

GameLoop::Options Plain() {
    GameLoop::Options o;
    o.smoothDelta = false;
    return o;
}

bool Near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

std::vector<std::uint32_t> Decode(const std::string& s) {
    std::vector<std::uint32_t> out;
    const char* p = s.data();
    const char* end = p + s.size();
    while (p < end) out.push_back(utf8::Next(p, end));
    return out;
}

std::string Bytes(std::initializer_list<int> b) {
    std::string s;
    for (int v : b) s += (char)v;
    return s;
}

}  // namespace

int main() {
    RUN_SUITE("loop_audit4");
    Log::console = false;

    // ---- BUG: Game::TogglePause did nothing while another system held a pause ---------------------------------------------
    // It flipped "is the game paused", and SetPaused(false) cannot release somebody else's hold. The pause button must always do
    // what the player asked: it flips the "manual" hold.
    {
        Fresh();
        Game::PushPause("cutscene");
        CHECK(Game::Paused());
        CHECK(Game::TogglePause());                                      // adds the player's own hold (it used to do nothing)
        CHECK(Game::IsPausedBy(Game::kManualPause));
        CHECK(Game::PauseReasons().size() == 2);
        Game::PopPause("cutscene");                                      // the cutscene ends...
        CHECK(Game::Paused());                                           // ...but the player's pause stands
        CHECK(!Game::TogglePause());                                     // pressed again: released
        CHECK(!Game::Paused() && !Time::Frozen());

        Game::PushPause("menu");
        Game::SetPaused(true);
        CHECK(!Game::TogglePause());                                     // releases only the manual hold
        CHECK(Game::Paused() && Game::IsPausedBy("menu") && !Game::IsPausedBy(Game::kManualPause));
        Game::PopPause("menu");
        CHECK(!Game::Paused());

        CHECK(Game::TogglePause() && Game::Paused());                    // from running: unchanged behaviour
        CHECK(!Game::TogglePause() && !Game::Paused());

        // a focus pause holds the game; the player's press still counts after the window comes back
        Game::SetPauseOnFocusLoss(true);
        Game::NotifyFocus(false);
        CHECK(Game::Paused() && Game::IsPausedBy(Game::kFocusPause));
        CHECK(Game::TogglePause());
        Game::NotifyFocus(true);
        CHECK(Game::Paused() && Game::IsPausedBy(Game::kManualPause));
        Game::SetPauseOnFocusLoss(false);
        Game::SetPaused(false);
        CHECK(!Game::Paused());
    }

    // ---- what a script sees across Stop and Play ---------------------------------------------------------------------------
    // Time::ElapsedTime() and FrameCount() are the process's clocks: BeginSession does not rewind them (stamps taken from them stay
    // valid). The Session readers start again from 0.
    {
        Fresh();
        GameLoop loop(Plain());
        for (int i = 0; i < 4; ++i) loop.BeginFrame(0.25);              // a first session: 1 s, 4 frames
        CHECK(Near(Time::SessionTime(), 1.0) && Time::SessionFrameCount() == 4 && Near(Time::SessionUnscaledTime(), 1.0));
        const float stampedAt = Time::ElapsedTime();                     // a cooldown's "ready at" kept across Stop and Play
        loop.BeginSession();                                             // Stop, Play
        CHECK(Time::SessionTime() == 0.0 && Time::SessionFrameCount() == 0 && Time::SessionUnscaledTime() == 0.0);
        CHECK(Near(Time::ElapsedTimeD(), 1.0) && Time::FrameCount() == 4);   // the process clocks stand where they were
        CHECK(Time::ElapsedTime() >= stampedAt);                         // monotonic: no stamp reads as the future
        Time::SetTimeScale(0.5f);
        for (int i = 0; i < 3; ++i) loop.BeginFrame(0.25);
        CHECK(Near(Time::SessionTime(), 0.375) && Time::SessionFrameCount() == 3 && Near(Time::SessionUnscaledTime(), 0.75));
        CHECK(Near(Time::ElapsedTimeD(), 1.375) && Time::FrameCount() == 7);
        Game::Reset();                                                   // Reset alone is not a new session
        CHECK(Time::SessionFrameCount() == 3);
        Game::BeginSession();                                            // Game::BeginSession is (the dedicated server calls it)
        CHECK(Time::SessionFrameCount() == 0 && Time::SessionTime() == 0.0);
        Time::ResetForTesting();                                         // a test reset zeroes both
        CHECK(Time::SessionTime() == 0.0 && Time::FrameCount() == 0 && Time::SessionFrameCount() == 0);
        Time::Step(0.1f);
        CHECK(Near(Time::SessionTime(), Time::ElapsedTimeD()) && Time::SessionFrameCount() == 1);   // before any mark: the process clocks
    }

    // ---- BUG: a huge frame at a time scale above 1 overflowed to infinity and poisoned ElapsedTime for good ---------------------
    {
        Fresh();
        Time::SetTimeScale(2.0f);
        Time::Step(3.0e38f);                                             // beyond FLT_MAX / 2 (a broken timer, the clamp turned off)
        CHECK(std::isfinite(Time::DeltaTime()) && std::isfinite(Time::ElapsedTimeD()));
        Time::Step(0.016f);
        CHECK(std::isfinite(Time::ElapsedTimeD()) && Time::DeltaTime() > 0.0f);
        // the same through a hit stop (the effects path integrates in double)
        Fresh();
        Time::SetTimeScale(Time::kMaxTimeScale);
        Time::HitStop(1.0f, 1.0f);
        Time::Step(3.0e38f);
        CHECK(std::isfinite(Time::DeltaTime()) && std::isfinite(Time::ElapsedTimeD()));
        Fresh();
        // and the loop: a debug fast forward times a huge frame (the clamp off) was cast out of float range
        GameLoop::Options o = Plain();
        o.maxDelta = 0.0f;
        GameLoop loop(o);
        CHECK(loop.SetDebugTimeScale(32.0f));
        loop.BeginFrame(1.0e38);
        CHECK(std::isfinite(loop.FrameDeltaStepped()) && Time::UnscaledDeltaTime() == kFltMax);
        CHECK(std::isfinite(Time::ElapsedTimeD()));
    }

    // ---- BUG: SmoothDelta near the top of the float range returned infinity (the sum, or twice the frame, overflowed) --------------------------------
    {
        Fresh();
        CHECK(std::isfinite(Time::SmoothDelta(kFltMax)) && std::isfinite(Time::SmoothDelta(kFltMax)));
        Time::ResetSmoothing();
        GameLoop::Options o = Plain();
        o.maxDelta = 0.0f;
        o.smoothDelta = true;
        GameLoop loop(o);
        for (int i = 0; i < 6; ++i) loop.BeginFrame(1.0e300);
        CHECK(std::isfinite(loop.FrameDeltaStepped()) && loop.FrameDeltaStepped() > 1.0e38f && std::isfinite(Time::ElapsedTimeD()));
        loop.BeginFrame(0.016);                                           // and the average comes back down
        CHECK(std::isfinite(loop.FrameDeltaStepped()));
    }

    // ---- BUG: two denormal frame times turned SmoothedFPS into NaN for the rest of the run (1 / dt = infinity, then inf - inf) -------------
    {
        Fresh();
        Time::Step(0.016f);
        Time::Step(1.0e-40f);
        Time::Step(1.0e-40f);
        for (int i = 0; i < 5; ++i) Time::Step(0.016f);
        CHECK(std::isfinite(Time::SmoothedFPS()) && Time::SmoothedFPS() > 0.0f);
        for (int i = 0; i < 200; ++i) Time::Step(0.016f);
        CHECK(Near(Time::SmoothedFPS(), 62.5, 3.0));                      // it recovers to the real rate
        Fresh();
        GameLoop loop(Plain());
        for (int i = 0; i < 4; ++i) loop.BeginFrame(1.0e-42);              // denormal real times through the loop
        for (int i = 0; i < 300; ++i) loop.BeginFrame(0.016);              // (the one 1e6 fps reading fades with 0.9 per frame)
        CHECK(std::isfinite(Time::SmoothedFPS()) && Near(Time::SmoothedFPS(), 62.5, 3.0));
        CHECK(std::isfinite(loop.Stats().Snapshot().fps));                // the statistics too
    }

    // ---- BUG: a clock's step times its scale overflowed to infinity (the clamp off, a scale of 100) ------------------------------------------
    {
        Clock c("huge", Clock::Source::Unscaled);
        c.SetScale(100.0f);
        c.Tick(0.0f, 1.0e37f, false);
        CHECK(std::isfinite(c.Delta()) && std::isfinite(c.Elapsed()));
        c.Tick(0.0f, 0.016f, false);
        CHECK(std::isfinite(c.Elapsed()) && c.Delta() > 0.0f);
        Clock r("ramp", Clock::Source::Unscaled);
        r.SetScale(100.0f);
        r.RampScale(100.0f, 1.0f);
        r.Tick(0.0f, 1.0e37f, false);                                       // through the ramp's own arithmetic
        CHECK(std::isfinite(r.Delta()) && std::isfinite(r.Elapsed()));
    }

    // ---- a fixed-step accumulator with an absurd limit stays quick ----------------------------------------------------------------------------
    {
        FixedTimestep f(0.02f, INT_MAX);
        CHECK(f.MaxSteps() == FixedTimestep::kMaxStepsCap);              // used to be INT_MAX: billions of steps in one Advance
        const int n = f.Advance(1.0e9f);
        CHECK(n == FixedTimestep::kMaxStepsCap && f.DroppedTime() > 0.0);
        FixedTimestep g(0.02f, 0);
        CHECK(g.MaxSteps() == 1);
        FixedTimestep h(0.02f, 5);
        h.SetMaxSteps(-4);
        CHECK(h.MaxSteps() == 1);
    }

    // ---- scale 0 with fixed steps, dt 0 and negative frames -----------------------------------------------------------------
    {
        Fresh();
        GameLoop loop(Plain());
        int fixed = 0;
        loop.OnFixedUpdate = [&](float) { ++fixed; };
        Time::SetTimeScale(0.0f);
        for (int i = 0; i < 10; ++i) {
            loop.BeginFrame(0.1);
            CHECK(loop.FixedStepsThisFrame() == 0 && loop.Alpha() == 0.0f && Time::DeltaTime() == 0.0f);
        }
        CHECK(fixed == 0 && loop.TotalFixedSteps() == 0 && loop.DroppedFixedTime() == 0.0);
        Time::SetTimeScale(1.0f);
        loop.BeginFrame(0.1);
        CHECK(fixed == 5 && Near(loop.DroppedFixedTime(), 0.0));      // 0.1 s = exactly five 0.02 s steps
        const std::uint64_t frames = Time::FrameCount();
        for (int i = 0; i < 1000; ++i) loop.BeginFrame(i % 2 ? 0.0 : -1.0);
        CHECK(Time::FrameCount() == frames + 1000 && fixed == 5);    // dt 0 and negative frames are frames that add no time
        CHECK(std::isfinite(Time::ElapsedTimeD()) && loop.Alpha() >= 0.0f && loop.Alpha() < 1.0f);
    }

    // ---- clock ramps that end at zero or start above one ---------------------------------------------------------------------
    {
        Clock c("test", Clock::Source::Unscaled);
        c.SetScale(2.0f);
        c.RampScale(0.0f, 1.0f);                                         // 2 -> 0 over one second of source time: area 1
        double sum = 0.0;
        for (int i = 0; i < 7; ++i) { c.Tick(0.3f, 0.3f, false); sum += c.Delta(); }   // 2.1 s of source time
        CHECK(Near(sum, 1.0, 1e-5) && c.Scale() == 0.0f && !c.Ramping());
        c.Tick(0.5f, 0.5f, false);
        CHECK(c.Delta() == 0.0f && Near(c.Elapsed(), 1.0, 1e-5));
        Clock d("test2", Clock::Source::Unscaled);
        d.SetScale(0.0f);
        d.RampScale(1.0f, 1.0f);                                         // 0 -> 1: a 5 s frame gets 0.5 + 4
        d.Tick(5.0f, 5.0f, false);
        CHECK(Near(d.Delta(), 4.5, 1e-5) && d.Scale() == 1.0f);
        // Time's own ramp: the same, in real seconds
        Fresh();
        Time::RampTimeScale(0.0f, 1.0f);
        Time::Step(3.0f);
        CHECK(Near(Time::DeltaTime(), 0.5, 1e-5) && Time::BaseTimeScale() == 0.0f);
        Time::Step(1.0f);
        CHECK(Time::DeltaTime() == 0.0f);
    }

    // ---- BUG: a snapshot block with no time scale set the scale to 1 ----------------------------------------------------------
    {
        Fresh();
        SaveDocument doc;
        SaveWriter w(doc.Block("t"));
        w.Int("v", 1);                                                   // an older or partial block: no `timeScale`, no clocks
        TimeSnapshot snap;
        CHECK(TimeSnapshot::Read(SaveReader(doc.Block("t")), snap));
        CHECK(!snap.hasTimeScale);
        Time::SetTimeScale(0.5f);
        snap.Apply();
        CHECK(Time::BaseTimeScale() == 0.5f);                            // left alone (it was set to 1)
        // a block that has it still sets it
        SaveWriter w2(doc.Block("u"));
        w2.Int("v", 1); w2.Float("timeScale", 0.25f);
        TimeSnapshot snap2;
        CHECK(TimeSnapshot::Read(SaveReader(doc.Block("u")), snap2) && snap2.hasTimeScale);
        snap2.Apply();
        CHECK(Time::BaseTimeScale() == 0.25f);
        // a block from a build that does not exist yet (a higher version, a key this one does not know) still reads
        SaveWriter w3(doc.Block("f"));
        w3.Int("v", 99); w3.Float("timeScale", 2.0f); w3.Int("future.key", 7);
        TimeSnapshot snap3;
        CHECK(TimeSnapshot::Read(SaveReader(doc.Block("f")), snap3) && snap3.timeScale == 2.0f);
        // no version at all is not a snapshot
        SaveWriter w4(doc.Block("n"));
        w4.Float("timeScale", 3.0f);
        TimeSnapshot snap4;
        CHECK(!TimeSnapshot::Read(SaveReader(doc.Block("n")), snap4));
    }

    // ---- BUG: a marker of INT_MIN was written but could not be read back ------------------------------------------------------
    {
        FrameRecording rec;
        rec.Add(0.016, 0, INT_MIN);
        rec.Add(0.016, 0, INT_MAX);
        rec.Add(0.016, 0, -1);
        FrameRecording back;
        std::string why;
        CHECK(FrameRecording::FromText(rec.ToText(), back, &why));
        FrameSample s;
        CHECK(back.At(0, s) && s.marker == INT_MIN);
        CHECK(back.At(1, s) && s.marker == INT_MAX);
        CHECK(back == rec);
        // one past each end is still refused (the loader checks every token before the checksum)
        FrameRecording tmp;
        CHECK(!FrameRecording::FromText("okayframes 1\nframes 1\nmicros 16000\n16000@-2147483649\nchecksum 0000000000000000\n", tmp, &why));
        CHECK(why.find("marker") != std::string::npos);
        CHECK(!FrameRecording::FromText("okayframes 1\nframes 1\nmicros 16000\n16000@2147483648\nchecksum 0000000000000000\n", tmp, &why));
        CHECK(why.find("marker") != std::string::npos);
    }

    // ---- replay desync: a recording played into a game that is not in the state it was recorded in ---------------------------
    {
        Fresh();
        FrameRecording rec;
        rec.Add(1.0 / 60.0, kFrameStepped);                              // a debug step...
        rec.Add(0.016, kFramePaused);                                    // ...a paused frame...
        rec.Add(0.4, kFrameHitch);                                       // ...and a hitch
        rec.AddRun(FrameSample{16667, 0, kFrameSynthetic}, 3);           // ...and capture frames
        FramePlayer player(rec);
        GameLoop::Options o = Plain();
        o.maxDelta = 0.25f;
        GameLoop loop(o);
        loop.AttachPlayer(&player);
        int frames = 0;
        while (loop.BeginFrame(99.0) && frames < 100) ++frames;          // the game is NOT paused: the step frame is an ordinary frame
        CHECK(frames + 1 == 6 && player.Finished() && loop.PlaybackFinished());
        CHECK(std::isfinite(Time::ElapsedTimeD()) && Time::FrameCount() == 6);
        CHECK(Near(loop.LostRealTime(), 0.4 - 0.25, 1e-6) && loop.HitchCount() == 1);   // the hitch is clamped and counted as it was
        // a finished replay runs nothing more and the host's clock is not read
        const std::uint64_t f = Time::FrameCount();
        CHECK(!loop.BeginFrame(0.016) && Time::FrameCount() == f);
        // with stopAtPlaybackEnd off the host's own time takes over
        Fresh();
        GameLoop::Options o2 = Plain();
        o2.stopAtPlaybackEnd = false;
        GameLoop loop2(o2);
        FramePlayer p2(rec);
        loop2.AttachPlayer(&p2);
        for (int i = 0; i < 6; ++i) loop2.BeginFrame(99.0);
        CHECK(loop2.Replaying());
        loop2.BeginFrame(0.125);
        CHECK(!loop2.Replaying() && Near(loop2.FrameDeltaReal(), 0.125));
        // an empty recording is a finished one
        Fresh();
        GameLoop loop3(Plain());
        FramePlayer p3;
        loop3.AttachPlayer(&p3);
        CHECK(!loop3.BeginFrame(0.016) && Time::FrameCount() == 0);
    }

    // ---- Application pacing: a tiny rate used to ask the sleep for 1e30 seconds ----------------------------------------------
    {
        CHECK(Near(Application::TargetFrameSeconds(30.0f), 1.0 / 30.0, 1e-7));
        CHECK(Application::TargetFrameSeconds(1.0f) == 1.0f);
        CHECK(Application::TargetFrameSeconds(0.5f) == 1.0f);            // never longer than a second
        CHECK(Application::TargetFrameSeconds(1e-30f) == 1.0f);
        CHECK(Application::TargetFrameSeconds(0.0f) == 0.0f && Application::TargetFrameSeconds(-5.0f) == 0.0f);
        CHECK(Application::TargetFrameSeconds(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
        CHECK(Application::TargetFrameSeconds(std::numeric_limits<float>::infinity()) == 0.0f);   // uncapped
    }

    // ---- TickPacer on a clock at the ends of the 64-bit range (overflow was undefined behaviour) -------------------------------
    {
        const std::int64_t kMax = std::numeric_limits<std::int64_t>::max(), kMin = std::numeric_limits<std::int64_t>::min();
        TickPacer p(30.0);
        p.Start(0);
        CHECK(p.FinishTick(kMax) == 0);                                  // far behind: resync, run at once
        CHECK(p.FinishTick(kMax) == 0);                                  // m_next + tick would wrap
        CHECK(p.LagTicks(kMax) >= 0.0);
        TickPacer q(30.0);
        q.Start(kMax);
        const std::int64_t wait = q.FinishTick(kMin);                    // a clock that went back by the whole range
        CHECK(wait > 0 && wait <= q.TickNs());                           // one tick from now, not a sleep of centuries
        CHECK(q.BackJumps() == 1);
        TickPacer r(1000.0, INT_MAX);
        r.Start(kMin);
        CHECK(r.FinishTick(kMin) >= 0);
        CHECK(r.FinishTick(kMax) == 0);
    }

    // ---- run N frames, then pause ---------------------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop(Plain());
        int updates = 0, done = 0;
        loop.OnUpdate = [&](float) { ++updates; };
        loop.OnRunFramesDone = [&] { ++done; };
        CHECK(!loop.RunFrames(0) && !loop.RunFrames(-3));
        CHECK(!loop.IsRunningFrames());
        CHECK(loop.RunFrames(3, 1.0f / 60.0f));
        CHECK(loop.IsRunningFrames() && loop.RunFramesRemaining() == 3);
        loop.BeginFrame(0.5);                                            // the host measured half a second: the run frame is exactly 1/60 s
        CHECK(Near(loop.FrameDeltaStepped(), 1.0 / 60.0, 1e-7) && loop.HitchCount() == 0 && loop.ClampedFrames() == 0);
        CHECK(loop.RunFramesRemaining() == 2 && !Game::Paused());
        loop.BeginFrame(0.001);
        CHECK(!Game::Paused() && done == 0);
        loop.BeginFrame(0.3);
        CHECK(Game::Paused() && Game::IsPausedBy(Game::kManualPause) && done == 1);   // paused at the end of the third frame
        CHECK(!loop.IsRunningFrames() && loop.RunFramesRemaining() == 0 && updates == 3);
        CHECK(Near(Time::ElapsedTimeD(), 3.0 / 60.0, 1e-6) && Time::FrameCount() == 3);
        loop.BeginFrame(0.016);                                          // the next frame is a paused one
        CHECK(Time::DeltaTime() == 0.0f && done == 1 && Time::FrameCount() == 4);
        Game::SetPaused(false);                                          // the manual hold: SetPaused(false) resumes
        CHECK(!Game::Paused());
        loop.BeginFrame(0.02);
        CHECK(Near(Time::DeltaTime(), 0.02, 1e-7));

        // the host's own time when no length is given, and its own pause reason
        Fresh();
        GameLoop loop2(Plain());
        int done2 = 0;
        loop2.OnRunFramesDone = [&] { ++done2; };
        CHECK(loop2.RunFrames(2, 0.0f, "editor"));
        loop2.BeginFrame(0.03);
        CHECK(Near(loop2.FrameDeltaStepped(), 0.03, 1e-7));
        loop2.BeginFrame(0.04);
        CHECK(Near(loop2.FrameDeltaStepped(), 0.04, 1e-7) && Game::IsPausedBy("editor") && !Game::IsPausedBy(Game::kManualPause) && done2 == 1);
        Game::ClearPause("editor");

        // a pause somebody else holds meanwhile: those frames do not count
        Fresh();
        GameLoop loop3(Plain());
        CHECK(loop3.RunFrames(2, 0.01f));
        Game::PushPause("menu");
        // (the game is paused now, so a frame runs as a normal paused frame)
        loop3.BeginFrame(0.016);
        CHECK(loop3.RunFramesRemaining() == 2);
        Game::PopPause("menu");
        loop3.BeginFrame(0.016);
        CHECK(loop3.RunFramesRemaining() == 1);
        loop3.BeginFrame(0.016);
        CHECK(Game::Paused() && !loop3.IsRunningFrames());

        // on a game that is already paused it is RequestStep: the steps run, the pause stays
        Fresh();
        GameLoop loop4(Plain());
        int finished = 0, runDone = 0;
        loop4.OnStepsFinished = [&] { ++finished; };
        loop4.OnRunFramesDone = [&] { ++runDone; };
        Game::PushPause("menu");
        CHECK(loop4.RunFrames(2, 0.01f));
        CHECK(!loop4.IsRunningFrames() && loop4.StepsRemaining() == 2);
        loop4.BeginFrame(0.5); loop4.BeginFrame(0.5);
        CHECK(Near(Time::ElapsedTimeD(), 0.02, 1e-7) && Game::Paused() && finished == 1 && runDone == 0);
        Game::PopPause("menu");

        // a second call replaces the first; Cancel and BeginSession clear it
        Fresh();
        GameLoop loop5(Plain());
        loop5.RunFrames(5);
        loop5.RunFrames(2);
        CHECK(loop5.RunFramesRemaining() == 2);
        loop5.CancelRunFrames();
        CHECK(!loop5.IsRunningFrames());
        loop5.BeginFrame(0.016);
        CHECK(!Game::Paused());
        loop5.RunFrames(4);
        loop5.BeginSession();
        CHECK(!loop5.IsRunningFrames());
        CHECK(loop5.RunFrames(INT_MAX) && loop5.RunFramesRemaining() == GameLoop::kMaxStepRequest);   // capped

        // a callback that cancels the run from inside the frame: the pause does not follow
        Fresh();
        GameLoop loop6(Plain());
        loop6.OnUpdate = [&](float) { loop6.CancelRunFrames(); };
        loop6.RunFrames(1, 0.01f);
        loop6.BeginFrame(0.016);
        CHECK(!Game::Paused());

        // capture mode: a run length replaces the capture rate for the run
        Fresh();
        GameLoop loop7(Plain());
        loop7.SetCapture(30.0f);
        loop7.RunFrames(1, 1.0f / 120.0f);
        loop7.BeginFrame(0.0);
        CHECK(Near(loop7.FrameDeltaReal(), 1.0 / 120.0, 1e-7));
        Game::SetPaused(false);
        loop7.BeginFrame(0.0);
        CHECK(Near(loop7.FrameDeltaReal(), 1.0 / 30.0, 1e-7));
        loop7.RunFrames(1);                                              // no length: the capture rate
        loop7.BeginFrame(0.0);
        CHECK(Near(loop7.FrameDeltaReal(), 1.0 / 30.0, 1e-7) && Game::Paused());
        Game::SetPaused(false);

        // a recorder sees the run frames as exact (synthetic) ones
        Fresh();
        GameLoop loop8(Plain());
        FrameRecorder rec;
        loop8.AttachRecorder(&rec);
        loop8.BeginFrame(0.016);
        loop8.RunFrames(2, 1.0f / 60.0f);
        loop8.BeginFrame(0.5); loop8.BeginFrame(0.5);
        loop8.BeginFrame(0.016);                                         // paused
        FrameSample s;
        CHECK(rec.Recording().Frames() == 4);
        CHECK(rec.Recording().At(0, s) && s.flags == 0 && s.micros == 16000);
        CHECK(rec.Recording().At(1, s) && (s.flags & kFrameSynthetic) && s.micros == 16667);
        CHECK(rec.Recording().At(2, s) && (s.flags & kFrameSynthetic) && s.micros == 16667);
        CHECK(rec.Recording().At(3, s) && (s.flags & kFramePaused) && !(s.flags & kFrameSynthetic));
    }

    // ---- debug time scale presets for a slower / faster button pair ------------------------------------------------------------
    {
        CHECK(GameLoop::NextDebugTimeScale(1.0f, 1) == 2.0f && GameLoop::NextDebugTimeScale(1.0f, -1) == 0.5f);
        CHECK(GameLoop::NextDebugTimeScale(0.5f, -1) == 0.25f && GameLoop::NextDebugTimeScale(0.25f, -1) == 0.1f);
        CHECK(GameLoop::NextDebugTimeScale(0.05f, -1) == 0.05f && GameLoop::NextDebugTimeScale(32.0f, 1) == 32.0f);   // the ends stay
        CHECK(GameLoop::NextDebugTimeScale(0.3f, 1) == 0.5f && GameLoop::NextDebugTimeScale(0.3f, -1) == 0.25f);       // between presets
        CHECK(GameLoop::NextDebugTimeScale(0.0f, 1) == 0.05f && GameLoop::NextDebugTimeScale(100.0f, -1) == 32.0f);
        CHECK(GameLoop::NextDebugTimeScale(std::numeric_limits<float>::quiet_NaN(), 1) == 2.0f);       // counts as 1
        CHECK(GameLoop::NextDebugTimeScale(-3.0f, -1) == 0.5f);
        CHECK(GameLoop::NextDebugTimeScale(0.7f, 0) == 0.7f);
        float v = 1.0f;                                                  // walk up and down through every preset
        for (int i = 0; i < 20; ++i) v = GameLoop::NextDebugTimeScale(v, 1);
        CHECK(v == 32.0f);
        for (int i = 0; i < 20; ++i) v = GameLoop::NextDebugTimeScale(v, -1);
        CHECK(v == 0.05f);
        GameLoop loop(Plain());
        for (int i = 0; i < 20; ++i) CHECK(loop.SetDebugTimeScale(GameLoop::NextDebugTimeScale(loop.DebugTimeScale(), i < 10 ? 1 : -1)));
    }

    // ---- UTF-8: the Unicode standard's examples of ill-formed text (maximal subpart replacement) ----------------------------------
    {
        const std::uint32_t F = utf8::kReplacement;
        auto same = [](const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b) { return a == b; };
        CHECK(same(Decode(Bytes({0x61, 0xF1, 0x80, 0x80, 0xE1, 0x80, 0xC2, 0x62, 0x80, 0x63, 0x80, 0xBF, 0x64})),
                   {0x61, F, F, F, 0x62, F, 0x63, F, F, 0x64}));
        CHECK(same(Decode(Bytes({0xC0, 0xAF, 0xE0, 0x80, 0xBF, 0xF0, 0x81, 0x82, 0x41})), {F, F, F, F, F, F, F, F, 0x41}));          // overlong
        CHECK(same(Decode(Bytes({0xED, 0xA0, 0x80, 0xED, 0xBF, 0xBF, 0xED, 0xAF, 0x41})), {F, F, F, F, F, F, F, F, 0x41}));         // surrogates
        CHECK(same(Decode(Bytes({0xF4, 0x91, 0x92, 0x93, 0xFF, 0x41, 0x80, 0xBF, 0x42})), {F, F, F, F, F, 0x41, F, F, 0x42}));     // above U+10FFFF
        CHECK(same(Decode(Bytes({0xE1, 0x80, 0xE2, 0xF0, 0x91, 0x92, 0xF1, 0xBF, 0x41})), {F, F, F, F, 0x41}));                     // truncated
        // every prefix of a 2, 3 and 4 byte character, cut at the end of the string, is ONE replacement for all its bytes
        const std::string chars[] = {Bytes({0xC3, 0xA9}), Bytes({0xE2, 0x82, 0xAC}), Bytes({0xF0, 0x9F, 0x98, 0x80})};
        for (const std::string& ch : chars) {
            for (std::size_t cut = 1; cut < ch.size(); ++cut) {
                const std::string prefix = ch.substr(0, cut);
                CHECK(same(Decode(prefix), {F}));
                CHECK(utf8::Length(prefix) == 1 && !utf8::IsValid(prefix));
                std::size_t i = prefix.size();
                CHECK(utf8::Prev(prefix, i) == F && i == 0);             // a backwards walk agrees
                CHECK(utf8::Sanitize(prefix) == Bytes({0xEF, 0xBF, 0xBD}));
            }
            CHECK(utf8::IsValid(ch) && utf8::Length(ch) == 1);
        }
        // the bound characters themselves
        CHECK(utf8::IsValid(Bytes({0xED, 0x9F, 0xBF})) && utf8::IsValid(Bytes({0xEE, 0x80, 0x80})));          // U+D7FF, U+E000
        CHECK(!utf8::IsValid(Bytes({0xED, 0xA0, 0x80})));                                                      // U+D800
        CHECK(utf8::IsValid(Bytes({0xF4, 0x8F, 0xBF, 0xBF})) && !utf8::IsValid(Bytes({0xF4, 0x90, 0x80, 0x80})));   // U+10FFFF, U+110000
        CHECK(utf8::IsValid(Bytes({0xC2, 0x80})) && !utf8::IsValid(Bytes({0xC1, 0xBF})) && !utf8::IsValid(Bytes({0xE0, 0x9F, 0xBF})));   // U+0080; overlongs
        // every byte string of up to 3 bytes made of interesting bytes: a forward walk ends, advances at least one byte, and the
        // backwards walk visits the same pieces in reverse
        const int pool[] = {0x00, 0x41, 0x7F, 0x80, 0xBF, 0xC0, 0xC2, 0xDF, 0xE0, 0xE1, 0xED, 0xEE, 0xF0, 0xF1, 0xF4, 0xF5, 0xFF, 0xA0, 0x9F, 0x90, 0x8F};
        const int n = (int)(sizeof pool / sizeof pool[0]);
        bool walk = true;
        for (int a = 0; a < n && walk; ++a)
            for (int b = -1; b < n && walk; ++b)
                for (int c = -1; c < n && walk; ++c) {
                    if (b < 0 && c >= 0) continue;
                    std::string s;
                    s += (char)pool[a];
                    if (b >= 0) s += (char)pool[b];
                    if (c >= 0) s += (char)pool[c];
                    std::vector<std::uint32_t> fwd;
                    std::size_t i = 0;
                    while (i < s.size()) { const std::size_t before = i; fwd.push_back(utf8::Next(s, i)); if (i <= before) { walk = false; break; } }
                    std::vector<std::uint32_t> back;
                    std::size_t j = s.size();
                    while (j > 0) { const std::size_t before = j; back.push_back(utf8::Prev(s, j)); if (j >= before) { walk = false; break; } }
                    std::vector<std::uint32_t> rev(back.rbegin(), back.rend());
                    if (rev != fwd) walk = false;
                    const std::string clean = utf8::Sanitize(s);
                    if (!utf8::IsValid(clean) || utf8::Length(clean) != fwd.size()) walk = false;
                }
        CHECK(walk);
        // appending and truncating: a limit that falls inside a character never leaves half of one
        std::string text = "a";
        utf8::Append(text, 0xE9); utf8::Append(text, 0x20AC); utf8::Append(text, 0x1F600); utf8::Append(text, 0xD800); utf8::Append(text, 0x110000);
        CHECK(utf8::IsValid(text) && utf8::Length(text) == 6);
        for (std::size_t cap = 0; cap <= text.size() + 2; ++cap) {
            const std::string cut = utf8::TruncateBytes(text, cap);
            CHECK(cut.size() <= cap && utf8::IsValid(cut) && text.compare(0, cut.size(), cut) == 0);
        }
    }

    // ---- a random walk over every control of the loop: the invariants hold after every frame -------------------------------------------------------
    // Pause, toggle, step, run N frames, scales (good and bad), hit stops, ramps, capture, new sessions, options, and frame times from
    // 1e-45 to infinity. Nothing may go NaN or infinite, time may not run backwards, the fixed-step count and Alpha stay in range,
    // OnPause and OnResume stay balanced, and the clocks stay sane. (24 seeds x 3000 frames.)
    {
        struct Mix {
            std::uint64_t s;
            std::uint64_t Next() { s += 0x9E3779B97F4A7C15ull; std::uint64_t z = s; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
            int Below(int n) { return (int)(Next() % (std::uint64_t)n); }
            bool Chance(int percent) { return Below(100) < percent; }
        };
        const double kTimes[] = {0.0, 1e-45, 1e-9, 0.001, 0.0083, 0.016, 0.0167, 0.033, 0.05, 0.1, 0.25, 0.5, 1.0, 30.0, 1e6, 1e30, 1e300,
                                 -1.0, -1e-9, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
        const float kScales[] = {0.0f, 0.001f, 0.25f, 1.0f, 2.0f, 100.0f, 1e30f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
        bool broken = false;
        std::string why;
        std::set<std::string> kinds;   // one line per kind of failure, with the first seed and frame that showed it
        auto bad = [&](const char* what, int seed, int frame) {
            broken = true;
            if (kinds.insert(what).second) why += std::string("\n    ") + what + " (seed " + std::to_string(seed) + ", frame " + std::to_string(frame) + ")";
        };
        for (int seed = 1; seed <= 24; ++seed) {
            Mix r{(std::uint64_t)seed * 7919};
            Fresh();
            GameLoop::Options o = Plain();
            o.maxDelta = r.Chance(30) ? 0.0f : (r.Chance(50) ? 0.1f : 0.25f);
            o.hitchReplacement = r.Chance(20) ? 0.016f : 0.0f;
            o.smoothDelta = r.Chance(40);
            o.refreshHz = r.Chance(50) ? 60.0f : 0.0f;
            o.maxFixedSteps = 1 + r.Below(8);
            o.fixedStep = r.Chance(30) ? 0.0f : 0.01f + 0.01f * (float)r.Below(4);
            o.firstFrameDelta = r.Chance(20) ? 0.016f : -1.0f;
            GameLoop loop(o);
            bool announced = false, doubled = false, updateMismatch = false;
            loop.OnPause = [&] { if (announced) doubled = true; announced = true; };
            loop.OnResume = [&] { if (!announced) doubled = true; announced = false; };
            loop.OnUpdate = [&](float dt) { if (dt != Time::DeltaTime()) updateMismatch = true; };
            double lastElapsed = 0.0;
            std::uint64_t lastFrames = Time::FrameCount();
            for (int frame = 0; frame < 3000; ++frame) {
                const int op = r.Below(100);
                if (op < 4) Game::TogglePause();
                else if (op < 7) Game::PushPause(r.Chance(50) ? "menu" : "cutscene");
                else if (op < 10) Game::PopPause(r.Chance(50) ? "menu" : "cutscene");
                else if (op < 12) Game::ClearPause("menu");
                else if (op < 16) loop.RequestStep(1 + r.Below(3), r.Chance(50) ? 0.0f : 0.01f);
                else if (op < 19) loop.RunFrames(1 + r.Below(4), r.Chance(50) ? 0.0f : 1.0f / 60.0f, r.Chance(50) ? "manual" : "editor");
                else if (op < 21) loop.CancelRunFrames();
                else if (op < 24) loop.SetDebugTimeScale(r.Chance(70) ? GameLoop::NextDebugTimeScale(loop.DebugTimeScale(), r.Chance(50) ? 1 : -1) : kScales[r.Below(10)]);
                else if (op < 27) Time::SetTimeScale(kScales[r.Below(10)]);
                else if (op < 29) Time::HitStop(r.Chance(50) ? 0.05f : kScales[r.Below(10)], r.Chance(50) ? 0.0f : 0.5f);
                else if (op < 31) Time::RampTimeScale(kScales[r.Below(10)], r.Chance(50) ? 0.5f : 5.0f);
                else if (op < 32) Time::CancelRamp();
                else if (op < 34) { if (r.Chance(50)) loop.SetCapture(r.Chance(50) ? 60.0f : 2.0f); else loop.ClearCapture(); }
                else if (op < 35) { loop.BeginSession(); lastFrames = Time::FrameCount(); }
                else if (op < 36) { Game::NotifyFocus(r.Chance(50)); Game::SetPauseOnFocusLoss(r.Chance(50)); }
                else if (op < 38) { GameLoop::Options n = loop.options(); n.maxFixedSteps = 1 + r.Below(20); n.smoothDelta = r.Chance(50); n.fixedStep = r.Chance(50) ? 0.0f : 0.02f; loop.SetOptions(n); }
                else if (op < 39) Time::SetFixedDeltaTime(r.Chance(50) ? 0.02f : 0.005f);
                else if (op < 40) loop.SetFrameMarker(r.Below(1000) - 500);
                const bool pausedBefore = Game::Paused();
                const bool frozenBefore = Time::Frozen();
                (void)frozenBefore;
                loop.BeginFrame(kTimes[r.Below((int)(sizeof kTimes / sizeof kTimes[0]))]);
                if (announced != pausedBefore) bad("OnPause / OnResume out of step with the pause at the start of the frame", seed, frame);
                if (doubled) bad("OnPause or OnResume announced twice", seed, frame);
                if (updateMismatch) bad("OnUpdate got a time other than Time::DeltaTime()", seed, frame);
                if (!(std::isfinite(Time::DeltaTime()) && Time::DeltaTime() >= 0.0f)) bad("DeltaTime not a finite number >= 0", seed, frame);
                if (!(std::isfinite(Time::UnscaledDeltaTime()) && Time::UnscaledDeltaTime() >= 0.0f)) bad("UnscaledDeltaTime", seed, frame);
                if (!std::isfinite(Time::ElapsedTimeD()) || Time::ElapsedTimeD() < lastElapsed) bad("ElapsedTimeD went backwards or non-finite", seed, frame);
                lastElapsed = Time::ElapsedTimeD();
                if (!std::isfinite(Time::UnscaledTime())) bad("UnscaledTime", seed, frame);
                if (Time::FrameCount() != lastFrames + 1) bad("FrameCount did not advance by exactly one", seed, frame);
                lastFrames = Time::FrameCount();
                if (!(loop.Alpha() >= 0.0f && loop.Alpha() < 1.0f)) bad("Alpha outside [0, 1)", seed, frame);
                if (loop.FixedStepsThisFrame() < 0 || loop.FixedStepsThisFrame() > GameLoop::kMaxFixedStepsPerFrame) bad("fixed steps out of range", seed, frame);
                if (!std::isfinite(loop.FrameDeltaStepped()) || loop.FrameDeltaStepped() < 0.0f) bad("FrameDeltaStepped not finite and >= 0", seed, frame);
                if (!std::isfinite(loop.FrameDeltaReal()) || !std::isfinite(loop.FrameDeltaClamped())) bad("FrameDeltaReal / Clamped not finite", seed, frame);
                if (!(loop.LostRealTime() >= 0.0) || !(loop.DroppedFixedTime() >= 0.0) || !std::isfinite(loop.LostRealTime())) bad("loop counters", seed, frame);
                if (!(Time::SmoothedFPS() >= 0.0f) || !std::isfinite(Time::SmoothedFPS())) bad("SmoothedFPS", seed, frame);
                if (!(Time::SessionTime() >= 0.0) || Time::SessionTime() > Time::ElapsedTimeD() + 1e-9) bad("SessionTime", seed, frame);
                if (Time::BaseTimeScale() < 0.0f || !(Time::BaseTimeScale() <= Time::kMaxTimeScale)) bad("time scale out of range", seed, frame);
                for (const std::string& name : ClockSet::Global().Names()) {
                    const Clock* c = ClockSet::Global().Find(name);
                    if (!c || !std::isfinite(c->Elapsed()) || c->Elapsed() < 0.0 || !(c->Delta() >= 0.0f) || !std::isfinite(c->Delta())) bad("a clock", seed, frame);
                }
                if (frame % 250 == 0) {
                    const FrameStatsSnapshot snap = loop.Stats().Snapshot();
                    if (!std::isfinite(snap.avgMs) || !std::isfinite(snap.fps) || !std::isfinite(snap.p99Ms)) bad("frame statistics", seed, frame);
                    const std::vector<std::string> lines = FrameOverlay::Lines(loop);
                    if (lines.empty() || lines.back().find("nan") != std::string::npos || lines.back().find("inf") != std::string::npos) bad("overlay text", seed, frame);
                }
                if (loop.QuitRequested()) { Game::CancelQuit(); loop.Reset(); announced = false; }   // a frame budget cannot be set here; a quit is cleared and the walk goes on
            }
        }
        if (broken) std::cerr << "  fuzz:" << why << "\n";
        CHECK(!broken);
    }

    // ---- hostile recordings and snapshot blocks never crash and never leave a bad number behind ------------------------------------------------------------
    {
        struct Mix {
            std::uint64_t s;
            std::uint64_t Next() { s += 0x9E3779B97F4A7C15ull; std::uint64_t z = s; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
            int Below(int n) { return (int)(Next() % (std::uint64_t)n); }
        };
        FrameRecording base;
        base.Add(0.016, 0, 3); base.Add(0.016, 0, 3); base.Add(0.25, kFrameHitch);
        base.AddRun(FrameSample{16667, 0, kFrameSynthetic}, 40);
        base.Add(0.02, kFramePaused | kFrameStepped, -9); base.Add(0.0); base.Add(3600.0, 0, INT_MIN);
        const std::string text = base.ToText();
        Mix r{12345};
        int accepted = 0;
        bool consistent = true;
        for (int i = 0; i < 6000; ++i) {
            std::string t = text;
            const int edits = 1 + r.Below(3);
            for (int e = 0; e < edits && !t.empty(); ++e) {
                const std::size_t at = (std::size_t)r.Below((int)t.size());
                switch (r.Below(5)) {
                    case 0: t[at] = (char)r.Below(256); break;
                    case 1: t.erase(at, (std::size_t)(1 + r.Below(6))); break;
                    case 2: t.insert(at, 1, "0123456789@/*-pschx \n\r"[r.Below(21)]); break;
                    case 3: t.resize(at); break;
                    default: t.insert(at, t.substr((std::size_t)r.Below((int)t.size()), 8)); break;
                }
            }
            FrameRecording out;
            std::string why;
            if (FrameRecording::FromText(t, out, &why)) {
                ++accepted;
                FrameRecording again;
                if (!FrameRecording::FromText(out.ToText(), again) || !(again == out)) consistent = false;
                const FrameSummary sm = out.Summary();
                if (!std::isfinite(sm.avgMs) || !std::isfinite(sm.p99Ms) || sm.frames != out.Frames()) consistent = false;
            } else if (why.empty() || !out.Empty()) {
                consistent = false;                                  // a refusal says why, and `out` was not touched (it was empty)
            }
        }
        CHECK(consistent);
        CHECK(accepted < 6000);                                      // most edits are caught (by the checksum, or the format)
        // a file with a valid checksum and hostile content
        auto sealed = [](const std::string& body) { return body + "checksum " + savefmt::Hex16(savefmt::Fnv1a(body.data(), body.size())) + "\n"; };
        FrameRecording hostile;
        std::string why;
        CHECK(!FrameRecording::FromText(sealed("okayframes 1\nframes 1000001\nmicros 16000016000\n16000*1000001\n"), hostile, &why));   // over the frame limit
        CHECK(!FrameRecording::FromText(sealed("okayframes 1\nframes 2\nmicros 32000\n16000*3\n"), hostile, &why) && why.find("frame count") != std::string::npos);
        CHECK(!FrameRecording::FromText(sealed("okayframes 1\nframes 1\nmicros 4000000000\n4000000000\n"), hostile, &why));            // over an hour
        CHECK(!FrameRecording::FromText(sealed("okayframes 1\nframes 1\nmicros 16000\n16000/pp\n"), hostile, &why));                    // a repeated flag
        CHECK(!FrameRecording::FromText(sealed("okayframes 1\nframes 1\nmicros 16000\n16000*0\n"), hostile, &why));                     // an empty run
        CHECK(!FrameRecording::FromText(sealed("okayframes 2\nframes 1\nmicros 16000\n16000\n"), hostile, &why) && why.find("version") != std::string::npos);
        CHECK(FrameRecording::FromText(sealed("okayframes 1\nframes 1000000\nmicros 3600000000000000\n3600000000*1000000\n"), hostile, &why));   // the largest valid one
        CHECK(hostile.Frames() == 1000000 && hostile.Runs() == 1);
        const FrameSummary big = hostile.Summary();
        CHECK(std::isfinite(big.avgMs) && std::fabs(big.avgMs - 3.6e6) < 1.0);
        FramePlayer pl(hostile);
        FrameSample fs;
        CHECK(pl.Seek(999999) && pl.Next(fs) && fs.micros == 3600000000u && !pl.HasNext());
        // snapshot blocks with garbage in every field
        const char* nasty[] = {"nan", "inf", "-inf", "-5", "1e999", "abc", "", " 7", "5abc", "0x10", "-0", "1e-999", "9999999999999999999999"};
        for (const char* v : nasty) {
            SaveDocument doc;
            SaveWriter w(doc.Block("t"));
            w.Int("v", 1); w.Float("timeScale", std::numeric_limits<float>::quiet_NaN());
            w.Int("clocks.n", 2000000000);
            for (int i = 0; i < 3; ++i) {
                SaveWriter c = w.Scope("clocks").Scope(std::to_string(i));
                c.String("name", i == 2 ? "" : (i == 1 ? "game" : "extra"));
                c.Int("source", 77 - i); c.Bool("holds", true); c.Bool("paused", i == 1);
                c.Float("scale", i == 0 ? std::numeric_limits<float>::infinity() : -3.0f);
                c.String("elapsed", v);
                c.Int64("ticks", i == 0 ? -4 : (long long)INT64_MAX);
                c.Float("rampTarget", std::numeric_limits<float>::quiet_NaN());
                c.Float("rampLeft", i == 0 ? std::numeric_limits<float>::quiet_NaN() : 1e30f);
            }
            w.Int("randoms.n", -5);
            TimeSnapshot snap;
            if (!TimeSnapshot::Read(SaveReader(doc.Block("t")), snap)) { consistent = false; continue; }
            ClockSet set;
            snap.Apply(set, TimeSnapshot::ApplyOptions{true, false, false});
            for (const std::string& name : set.Names()) {
                const Clock* c = set.Find(name);
                if (!c || !std::isfinite(c->Elapsed()) || c->Elapsed() < 0.0 || !(c->Scale() >= 0.0f) || !(c->Scale() <= Time::kMaxTimeScale)) consistent = false;
            }
            set.Get("extra").Tick(0.016f, 0.016f, false);
            set.Get("game").Tick(0.016f, 0.016f, false);
            if (!std::isfinite(set.Get("extra").Delta()) || !std::isfinite(set.Get("game").Delta()) || !std::isfinite(set.Get("extra").Elapsed())) consistent = false;
        }
        CHECK(consistent);
        // generator states of every shape: zero, all ones, one word zero
        Random keep(1);
        TimeSnapshot rs;
        rs.randoms["a"] = Random::State{0, 0};
        rs.randoms["b"] = Random::State{~0ull, ~0ull};
        rs.randoms["c"] = Random::State{0, 5};
        for (const char* n : {"a", "b", "c"}) {
            CHECK(rs.ApplyRandom(n, keep));
            std::uint64_t v = 0;
            for (int i = 0; i < 100; ++i) v |= keep.NextUInt64();
            CHECK(v != 0);                                           // none of them sticks at zero
        }
    }

    TEST_MAIN_RESULT();
}
