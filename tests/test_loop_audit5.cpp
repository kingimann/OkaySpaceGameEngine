// 1.1 fifth pass audit: the fixed-step alpha for code with no loop handle, real FPS from the loop's frame
// times (the player's show_fps readout), a time-scale ramp that survives a save, Cooldown's held-fire credit
// across save and load, ClockSet ramps and groups across Capture / Apply, LoopConsole's bad and huge numbers,
// FrameRecording replay of pauses and of the shared random stream, and Application's session start.
// Every frame time is made up; nothing sleeps and no wall clock is read.
#include "test_framework.hpp"
#ifndef LOOP_AUDIT5_MINI
#include "okay/Core/Application.hpp"
#endif
#include "okay/Core/Clock.hpp"
#include "okay/Core/Console.hpp"
#include "okay/Core/Countdown.hpp"
#include "okay/Core/FrameOverlay.hpp"
#include "okay/Core/FramePacing.hpp"
#include "okay/Core/FrameRecord.hpp"
#include "okay/Core/FrameStats.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/LoopConsole.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Core/SaveFramework.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/TimeBudget.hpp"
#include "okay/Core/TimeSnapshot.hpp"
#ifndef LOOP_AUDIT5_MINI   // (a build of just the Core files, without the scene, leaves the Application block out)
#include "okay/Render/Renderer.hpp"
#include "okay/Scene/Scene.hpp"
#endif
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
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
    Random::Shared().Seed(0xC0FFEEull);
}

GameLoop::Options Plain() {
    GameLoop::Options o;
    o.smoothDelta = false;
    return o;
}

bool Near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

#ifndef LOOP_AUDIT5_MINI
class NullRenderer : public IRenderer {
public:
    void BeginFrame(const Color&) override {}
    void EndFrame() override {}
    void SetCamera(const Vec3&, float, float) override {}
    void DrawQuad(const Vec3&, const Vec2&, const Color&, char) override {}
    Viewport GetViewport() const override { return {10, 10}; }
};
#endif

double g_fakeNow = 0.0;
double FakeNow() { return g_fakeNow; }

// Text with a correct checksum around an arbitrary body (so the parser, not the checksum, is what rejects it).
std::string Seal(const std::string& headerAndBody) {
    return headerAndBody + "checksum " + savefmt::Hex16(savefmt::Fnv1a(headerAndBody.data(), headerAndBody.size())) + "\n";
}

}  // namespace

int main() {
    RUN_SUITE("loop_audit5");
    Log::console = false;

    // ---- Time::FixedAlpha: the loop's alpha for code that has no handle on the loop ------------------------------------------
    {
        Fresh();
        GameLoop loop(Plain());
        CHECK(Time::FixedAlpha() == 0.0f);
        loop.BeginFrame(0.03);                                  // one 0.02 s step and 0.01 s left over: halfway to the next
        CHECK(loop.FixedStepsThisFrame() == 1);
        CHECK(Near(loop.Alpha(), 0.5, 1e-4));
        CHECK(Time::FixedAlpha() == loop.Alpha());
        loop.BeginFrame(0.01);                                  // 0.02 s now owed: a step, nothing left
        CHECK(Time::FixedAlpha() == loop.Alpha() && Time::FixedAlpha() < 1e-3f);
        loop.BeginFrame(0.01);
        CHECK(Near(Time::FixedAlpha(), 0.5, 1e-4));
        Game::SetPaused(true);
        loop.BeginFrame(0.5);                                   // paused: the leftover stands still, and so does the alpha
        CHECK(Near(Time::FixedAlpha(), 0.5, 1e-4) && Time::FixedAlpha() == loop.Alpha());
        Game::SetPaused(false);
        Time::SetFixedAlpha(std::numeric_limits<float>::quiet_NaN());   // a bad number counts as 0, and the range is [0, 1)
        CHECK(Time::FixedAlpha() == 0.0f);
        Time::SetFixedAlpha(7.0f);
        CHECK(Time::FixedAlpha() < 1.0f && Time::FixedAlpha() > 0.99f);
        Time::SetFixedAlpha(-3.0f);
        CHECK(Time::FixedAlpha() == 0.0f);
        loop.BeginFrame(0.03);
        loop.BeginSession();                                    // a new session starts the interpolation over
        CHECK(Time::FixedAlpha() == 0.0f);
    }

    // ---- GameLoop::Current: the loop that ran the latest frame, and no dangling pointer -----------------------------------------
    {
        Fresh();
        CHECK(GameLoop::Current() == nullptr);
        {
            GameLoop a(Plain());
            CHECK(GameLoop::Current() == nullptr);              // constructing one does not make it current
            a.BeginFrame(0.01);
            CHECK(GameLoop::Current() == &a);
            {
                GameLoop b(Plain());
                b.BeginFrame(0.01);
                CHECK(GameLoop::Current() == &b);               // the one that ran last
            }
            CHECK(GameLoop::Current() == nullptr);              // destroyed: gone, not dangling
            a.BeginFrame(0.01);
            CHECK(GameLoop::Current() == &a);
            GameLoop copy = a;                                  // a copy is a loop of its own
            CHECK(GameLoop::Current() == &a);
            GameLoop idle(Plain());
            idle.Stop();
            idle.BeginFrame(0.01);                              // even one that is stopped is the driver now
            CHECK(GameLoop::Current() == &idle);
        }
        CHECK(GameLoop::Current() == nullptr);
    }

    // ---- real FPS: FrameStats::AverageFps and the show_fps label -------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop(Plain());
        CHECK(FrameOverlay::FpsLabel(loop) == "FPS --");        // no frame yet
        for (int i = 0; i < 10; ++i) loop.BeginFrame(0.01);
        CHECK(Near(loop.Stats().AverageFps(30), 100.0, 1e-6));
        CHECK(FrameOverlay::FpsLabel(loop) == "FPS 100");
        CHECK(Near(loop.Stats().AverageFps(4), 100.0, 1e-6));
        CHECK(loop.Stats().AverageFps(0) == 0.0);
        loop.BeginFrame(5.0);                                   // a 5 s stall: the game steps the 0.25 s clamp, the readout sees 5 s
        CHECK(loop.FrameDeltaClamped() < 0.26f);
        // The old readout cut the frame to the step limit (never below 1 / 0.1 = 10 FPS); the real numbers show the stall.
        CHECK(loop.Stats().AverageFps(30) < 3.0);
        CHECK(FrameOverlay::FpsLabel(loop) == "FPS 2");
        CHECK(Near(loop.Stats().AverageFps(1), 0.2, 1e-9));     // that one frame: 1 frame in 5 s
        CHECK(Near(loop.Stats().AverageFps(100000), loop.Stats().AverageFps(11), 1e-12));   // asking for more than there is uses all
        FrameStats direct(8);
        for (int i = 0; i < 3; ++i) direct.Push(16.0);
        CHECK(Near(direct.AverageFps(2), 62.5, 1e-9));
        CHECK(FrameOverlay::FpsLabel(direct) == "FPS 63");
        FrameStats off(0);                                      // statistics off: nothing to show
        off.Push(16.0);
        CHECK(off.AverageFps(30) == 0.0 && FrameOverlay::FpsLabel(off) == "FPS --");
        FrameStats zeros(8);
        zeros.Push(0.0); zeros.Push(0.0);                       // frames that took no time: no infinite rate
        CHECK(zeros.AverageFps(2) == 0.0 && FrameOverlay::FpsLabel(zeros) == "FPS --");
    }

    // ---- a Time::RampTimeScale in progress survives a save ----------------------------------------------------------------------------
    {
        Fresh();
        Time::SetTimeScale(0.2f);
        Time::RampTimeScale(1.0f, 1.0f);                        // easing back up out of slow motion over a second
        Time::Step(0.4f);
        CHECK(Time::IsRamping() && Near(Time::BaseTimeScale(), 0.52, 1e-5) && Near(Time::RampSecondsLeft(), 0.6, 1e-5));
        const TimeSnapshot snap = TimeSnapshot::Capture();
        CHECK(snap.timeRamping && snap.timeRampTarget == 1.0f && Near(snap.timeRampSecondsLeft, 0.6, 1e-5));
        SaveDocument doc;
        snap.Write(doc.Writer("time"));
        SaveDocument stored;
        CHECK(SaveDocument::Parse(doc.Serialize(), stored));
        TimeSnapshot loaded;
        CHECK(TimeSnapshot::Read(stored.Reader("time"), loaded));
        CHECK(loaded == snap);
        // the run that carries on
        const double before = Time::ElapsedTimeD();
        Time::Step(0.6f);
        const double refGain = Time::ElapsedTimeD() - before;
        CHECK(!Time::IsRamping() && Time::BaseTimeScale() == 1.0f);
        // the run that was saved, and loaded in a fresh game: the same ramp, from the same place
        Time::ResetForTesting();
        loaded.Apply();
        CHECK(Time::IsRamping() && Time::RampTarget() == 1.0f);
        CHECK(Near(Time::BaseTimeScale(), 0.52, 1e-5) && Near(Time::RampSecondsLeft(), 0.6, 1e-5));
        const double before2 = Time::ElapsedTimeD();
        Time::Step(0.6f);
        CHECK(Near(Time::ElapsedTimeD() - before2, refGain, 1e-5));   // the game time it gets is the one the saved run got
        CHECK(!Time::IsRamping() && Time::BaseTimeScale() == 1.0f);   // and it arrives (it used to stay at 0.52 for good)

        // a block from before this existed has no ramp keys: it loads and leaves no ramp
        SaveDocument oldDoc;
        { SaveWriter w = oldDoc.Writer("t"); w.Int("v", 1); w.Float("timeScale", 0.7f); w.Int("clocks.n", 0); w.Int("randoms.n", 0); }
        TimeSnapshot older;
        CHECK(TimeSnapshot::Read(oldDoc.Reader("t"), older) && !older.timeRamping && older.timeScale == 0.7f);
        Time::ResetForTesting();
        Time::RampTimeScale(0.1f, 5.0f);
        older.Apply();                                          // sets the scale, which also ends a ramp running here
        CHECK(!Time::IsRamping() && Time::BaseTimeScale() == 0.7f);
        // hostile ramp keys: nothing to ramp, or cleaned numbers
        for (float left : {0.0f, -2.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            SaveDocument bad;
            { SaveWriter w = bad.Writer("t"); w.Int("v", 1); w.Float("timeScale", 0.5f); w.Float("timeRampTarget", 3.0f); w.Float("timeRampLeft", left); }
            TimeSnapshot b;
            CHECK(TimeSnapshot::Read(bad.Reader("t"), b) && !b.timeRamping);
        }
        SaveDocument wild;
        { SaveWriter w = wild.Writer("t"); w.Int("v", 1); w.Float("timeScale", 0.5f); w.Float("timeRampTarget", 1.0e30f); w.Float("timeRampLeft", 2.0f); }
        TimeSnapshot capped;
        CHECK(TimeSnapshot::Read(wild.Reader("t"), capped) && capped.timeRamping && capped.timeRampTarget == Time::kMaxTimeScale);
        // ApplyOptions: a host that does not want the scale back leaves a running ramp alone
        Time::ResetForTesting();
        Time::RampTimeScale(0.5f, 2.0f);
        snap.Apply(ClockSet::Global(), TimeSnapshot::ApplyOptions{true, false, false});
        CHECK(Time::IsRamping() && Time::RampTarget() == 0.5f && Near(Time::RampSecondsLeft(), 2.0, 1e-6));
        // equality sees the ramp
        TimeSnapshot x = snap, y = snap;
        CHECK(x == y);
        y.timeRampSecondsLeft += 0.25f;
        CHECK(x != y);
        y = snap; y.timeRamping = false;
        CHECK(x != y);
        Time::ResetForTesting();
    }

    // ---- ClockSet groups and ramps across Capture / Write / Read / Apply -------------------------------------------------------------
    {
        Fresh();
        auto build = [](ClockSet& set) {
            Clock& a = set.Add("a", Clock::Source::Unscaled, false);
            Clock& b = set.Add("b", Clock::Source::Scaled, true);       // the game pause holds it
            Clock& c = set.Add("c", Clock::Source::Unscaled, false);
            a.RampScale(0.2f, 2.0f);
            b.RampScale(3.0f, 1.5f);
            c.SetScale(0.7f); c.Pause(); c.RampScale(1.0f, 1.0f);       // paused: its ramp holds too
            set.Get("game").SetScale(0.5f);
        };
        ClockSet ref;
        build(ref);
        std::uint32_t seed = 12345u;
        auto next = [&seed] { seed = seed * 1664525u + 1013904223u; return 0.005f + (float)(seed >> 8) / 16777216.0f * 0.045f; };
        std::vector<float> dts;
        for (int i = 0; i < 40; ++i) dts.push_back(next());
        auto tick = [&](ClockSet& set, int i) {
            const bool paused = i >= 25 && i < 31;                       // the game pause covers a stretch of frames
            const float unscaled = dts[(std::size_t)i];
            set.Tick(paused ? 0.0f : unscaled * 0.5f, unscaled, paused);
        };
        for (int i = 0; i < 17; ++i) tick(ref, i);
        const TimeSnapshot snap = TimeSnapshot::Capture(ref, false);
        SaveDocument doc;
        snap.Write(doc.Writer("t"));
        SaveDocument stored;
        CHECK(SaveDocument::Parse(doc.Serialize(), stored));
        TimeSnapshot loaded;
        CHECK(TimeSnapshot::Read(stored.Reader("t"), loaded));
        CHECK(loaded == snap);
        ClockSet restored;                                               // a fresh game: the custom clocks do not exist yet
        loaded.Apply(restored, TimeSnapshot::ApplyOptions{true, false, false});
        CHECK(restored.Count() == ref.Count());
        for (int i = 17; i < 40; ++i) { tick(ref, i); tick(restored, i); }
        bool same = true;
        for (const std::string& n : ref.Names()) {
            const Clock* r = ref.Find(n);
            const Clock* q = restored.Find(n);
            if (!q || !Near(r->Elapsed(), q->Elapsed(), 1e-5) || !Near(r->Scale(), q->Scale(), 1e-5) || r->Ramping() != q->Ramping() ||
                r->Paused() != q->Paused() || r->GetSource() != q->GetSource() || r->HoldsWhileGamePaused() != q->HoldsWhileGamePaused()) {
                same = false;
            }
        }
        CHECK(same);
        CHECK(restored.Find("c") && restored.Find("c")->Paused() && restored.Find("c")->Ramping());   // the held clock still holds its ramp
        CHECK(restored.Find("a") && restored.Find("a")->Elapsed() > 0.0);
        // a clock that exists only here keeps its own state when the snapshot does not name it
        ClockSet other;
        other.Get("mine").Tick(0.5f, 0.5f, false);
        loaded.Apply(other, TimeSnapshot::ApplyOptions{true, false, false});
        CHECK(Near(other.Get("mine").Elapsed(), 0.5, 1e-9));
    }

    // ---- Cooldown: a held-fire credit that is pending when the game is saved --------------------------------------------------------
    {
        Cooldown c(0.25f);
        c.TryUse();
        c.Tick(0.3f);                                            // ready, with 0.05 s of the tick left over
        SaveDocument doc;
        c.Save(doc.Writer("c"));
        SaveDocument stored;
        CHECK(SaveDocument::Parse(doc.Serialize(), stored));
        Cooldown d;
        d.Load(stored.Reader("c"));
        CHECK(c.TryUse() && d.TryUse());
        CHECK(Near(c.Remaining(), 0.2, 1e-6));
        CHECK(Near(d.Remaining(), c.Remaining(), 1e-6));         // a save taken here and a save taken a moment earlier are the same save
        // no credit pending: nothing extra is written, and an older save (no key) loads as before
        Cooldown e(1.0f);
        e.TryUse();
        e.Tick(0.4f);
        SaveDocument d2; e.Save(d2.Writer("e"));
        CHECK(!d2.Reader("e").Has("credit"));
        Cooldown f;
        f.Load(d2.Reader("e"));
        CHECK(Near(f.Remaining(), 0.6, 1e-6));
        // hostile credits are cleaned, and a credit on a cooldown that is still counting down is ignored
        for (float bad : {-1.0f, std::numeric_limits<float>::quiet_NaN(), 1.0e30f, std::numeric_limits<float>::infinity()}) {
            SaveDocument h;
            { SaveWriter w = h.Writer("c"); w.Float("duration", 0.5f); w.Float("remaining", 0.0f); w.Bool("paused", false); w.Float("credit", bad); }
            Cooldown g;
            g.Load(h.Reader("c"));
            CHECK(g.TryUse() && g.Remaining() >= 0.0f && g.Remaining() <= 0.5f);
        }
        SaveDocument h2;
        { SaveWriter w = h2.Writer("c"); w.Float("duration", 0.5f); w.Float("remaining", 0.3f); w.Bool("paused", false); w.Float("credit", 0.2f); }
        Cooldown g2;
        g2.Load(h2.Reader("c"));
        g2.Tick(0.3f);                                           // ready with exactly nothing left over (the saved credit is not his)
        CHECK(g2.TryUse() && Near(g2.Remaining(), 0.5, 1e-6));
    }

    // ---- Countdown and Cooldown: a long timer keeps its time (they counted in float) -------------------------------------------------------
    {
        const float dt = 1.0f / 60.0f;                              // ten minutes of 60 fps ticks
        for (float seconds : {600.0f, 3600.0f, 86400.0f, 604800.0f}) {
            Countdown c(seconds);
            for (int i = 0; i < 60 * 600; ++i) c.Tick(dt);
            CHECK(Near(c.Elapsed(), 600.0, 0.05));                  // a day long timer read 562 here, a week long one 0 (a tick is under half a float step)
            Cooldown k(seconds);
            k.TryUse();
            for (int i = 0; i < 60 * 600; ++i) k.Tick(dt);
            CHECK(Near(k.Remaining(), seconds - 600.0, 0.05 + seconds * 1e-7));
        }
        Countdown week(604800.0f);
        int fired = 0;
        for (int i = 0; i < 60 * 600; ++i) fired += week.Tick(dt) ? 1 : 0;
        CHECK(fired == 0 && !week.Done() && week.Active());
        week.Extend(-604190.0f);                                    // 10 s left (604800 - 600 - 604190)
        CHECK(Near(week.Remaining(), 10.0, 0.05));
        bool done = false;
        for (int i = 0; i < 60 * 10 + 2 && !done; ++i) done = week.Tick(dt);
        CHECK(done && week.Done());
        // short timers read as they did: the same floats come out
        Countdown c(1.0f);
        c.Tick(0.3f);
        CHECK(c.Remaining() == 0.7f && Near(c.Progress(), 0.3, 1e-6));
        Countdown two(0.5f);
        CHECK(!two.Tick(0.25f) && two.Remaining() == 0.25f && two.Tick(0.3f) && Near(two.Overshoot(), 0.05, 1e-6));
    }

    // ---- LoopConsole: huge, fractional and tiny numbers ---------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop(Plain());
        LoopConsole commands(loop);
        Console::Result r = Console::Execute("time.scale 1e300");
        CHECK(r.ok && r.output == "time.scale = 100");           // the loop's own cap decides what a huge number means
        r = Console::Execute("time.scale 1e-300");
        CHECK(r.ok && r.output == "time.scale = 0");
        Console::Execute("time.scale 1");
        r = Console::Execute("time.debug_scale 1e300");
        CHECK(r.ok && r.output == "debug scale = 32" && loop.DebugTimeScale() == 32.0f);
        CHECK(!Console::Execute("time.debug_scale -1e300").ok && loop.DebugTimeScale() == 32.0f);
        Console::Execute("time.debug_scale 1");
        CHECK(!Console::Execute("time.scale -1e300").ok && Time::BaseTimeScale() == 1.0f);

        Game::SetPaused(true);
        for (const char* bad : {"time.step 2.5", "time.step 0.5", "time.step 1e18", "time.step -1", "time.step 0x10", "time.step 1e400"}) {
            CHECK(!Console::Execute(bad).ok);
            CHECK(loop.StepsRemaining() == 0);                      // a refused command steps nothing (2.5 used to run 2 frames)
        }
        r = Console::Execute("time.step 2");
        CHECK(r.ok && loop.StepsRemaining() == 2);
        loop.CancelStep();
        r = Console::Execute("time.step 1e3");                     // whole, so fine in any spelling
        CHECK(r.ok && loop.StepsRemaining() == 1000);
        loop.CancelStep();
        r = Console::Execute("time.run 2.5");
        CHECK(!r.ok && loop.StepsRemaining() == 0);                // used to step 2 frames and say nothing about the .5
        Game::SetPaused(false);

        for (const char* bad : {"time.run 2.5", "time.run 3 1e-300", "time.run 3 0.5", "time.run 3 0", "time.run 3 -60", "time.run 3 1e300", "time.run 3 inf"}) {
            CHECK(!Console::Execute(bad).ok);
            CHECK(!loop.IsRunningFrames());                        // a refused command starts nothing
        }
        r = Console::Execute("time.run 3 1e-300");
        CHECK(!r.ok && !loop.IsRunningFrames());                   // used to start a run whose frames were not 1 / fps long at all
        r = Console::Execute("time.run 3 1");                      // one second frames: the longest a frame may be
        CHECK(r.ok && loop.IsRunningFrames() && loop.RunFramesRemaining() == 3);
        loop.CancelRunFrames();
        r = Console::Execute("time.run 1000000 1000");
        CHECK(r.ok && loop.RunFramesRemaining() == 1000000);
        loop.CancelRunFrames();
    }

    // ---- FrameRecording carries the shared random stream, and a replay puts it back ----------------------------------------------------
    {
        Fresh();
        // text: the optional line sits between the totals and the frames; a file without it is byte for byte what it was
        FrameRecording rec;
        rec.Add(0.016, 0, 0); rec.Add(0.016, 0, 0);
        const std::string plain = rec.ToText();
        CHECK(plain.find("random") == std::string::npos);
        rec.SetRandom(Random::State{0x0123456789ABCDEFull, 0xFEDCBA9876543210ull});
        const std::string withSeed = rec.ToText();
        CHECK(withSeed.find("\nmicros 32000\nrandom 0123456789abcdef fedcba9876543210\n16000*2\n") != std::string::npos);
        FrameRecording back;
        std::string why;
        CHECK(FrameRecording::FromText(withSeed, back, &why));
        CHECK(back == rec && back.HasRandom() && back.RandomState().s0 == 0x0123456789ABCDEFull && back.RandomState().s1 == 0xFEDCBA9876543210ull);
        CHECK(back.ToText() == withSeed);
        FrameRecording older;
        CHECK(FrameRecording::FromText(plain, older, &why) && !older.HasRandom() && older.ToText() == plain);   // files from before load as they did
        CHECK(older != back);                                      // the seed is part of what a recording is
        // the documented example from docs/loop_and_time.md is still a recording
        FrameRecording sample;
        CHECK(FrameRecording::FromText("okayframes 1\nframes 126\nmicros 2353373\n16667*120 16666 250000/h 16667/p@7*2 20000/ps 33333/c@-42\nchecksum 34c4491ed3039bc2\n", sample, &why) && !sample.HasRandom());
        // strictness
        auto rejects = [&](const std::string& body) {
            FrameRecording x; std::string e;
            return !FrameRecording::FromText(Seal(body), x, &e) && !e.empty();
        };
        const std::string head = "okayframes 1\nframes 1\nmicros 16667\n";
        CHECK(rejects(head + "random 00000000000000010000000000000002\n16667\n"));            // no blank between the words
        CHECK(rejects(head + "random 0000000000000001 0000000000000002 3\n16667\n"));
        CHECK(rejects(head + "random 0000000000000001 000000000000000\n16667\n"));              // 15 digits
        CHECK(rejects(head + "random 000000000000000g 0000000000000002\n16667\n"));
        CHECK(rejects(head + "random 0000000000000001\n16667\n"));
        CHECK(rejects(head + "random 0000000000000001 0000000000000002\nrandom 0000000000000001 0000000000000002\n16667\n"));   // twice
        CHECK(rejects(head + "16667\nrandom 0000000000000001 0000000000000002\n"));              // after the frames
        FrameRecording ok;
        CHECK(FrameRecording::FromText(Seal(head + "random 0000000000000001 0000000000000002\n16667\n"), ok, &why) && ok.HasRandom());
        CHECK(FrameRecording::FromText(Seal(head + "random 000000000000ABCD 0000000000000002\n16667\n"), ok, &why) && ok.RandomState().s0 == 0xABCDull);   // upper case hex
        FrameRecording cleared = rec;
        cleared.Clear();
        CHECK(!cleared.HasRandom());

        // a recorded session and its replay draw the same numbers from the shared stream
        Random::Shared().Seed(77);
        const Random::State start = Random::Shared().GetState();
        FrameRecorder recorder;
        GameLoop live(Plain());
        live.AttachRecorder(&recorder);
        std::vector<std::uint64_t> liveDraws;
        live.OnUpdate = [&](float) { liveDraws.push_back(Random::Shared().NextUInt64()); };
        for (int i = 0; i < 10; ++i) live.BeginFrame(0.016);
        CHECK(recorder.Recording().HasRandom());
        CHECK(recorder.Recording().RandomState().s0 == start.s0 && recorder.Recording().RandomState().s1 == start.s1);   // as the first frame began
        FrameRecording loaded;
        CHECK(FrameRecording::FromText(recorder.Recording().ToText(), loaded, &why) && loaded.HasRandom());

        auto replay = [&](bool restore, bool withSeed) {
            Fresh();
            Random::Shared().Seed(999);                            // another run, another stream
            FrameRecording r = loaded;
            if (!withSeed) r.ClearRandom();
            FramePlayer player(r);
            GameLoop::Options o = Plain();
            o.replayRandom = restore;
            GameLoop loop(o);
            loop.AttachPlayer(&player);
            std::vector<std::uint64_t> draws;
            loop.OnUpdate = [&](float) { draws.push_back(Random::Shared().NextUInt64()); };
            while (loop.BeginFrame(0.0)) {}
            return draws;
        };
        CHECK(replay(true, true) == liveDraws);                     // the replayed game rolls what the recorded one rolled
        const std::vector<std::uint64_t> off = replay(false, true);
        CHECK(off.size() == liveDraws.size() && off != liveDraws);  // replayRandom = false: the stream carries on from the host's
        Random check(999);
        bool carried = true;
        for (std::uint64_t d : replay(true, false)) if (d != check.NextUInt64()) carried = false;
        CHECK(carried);                                             // a recording with no seed (an older one) leaves the stream alone

        // a recorder told not to keep the seed writes the old format
        FrameRecorder quiet;
        quiet.SetRecordRandom(false);
        CHECK(!quiet.RecordRandom());
        Fresh();
        GameLoop l2(Plain());
        l2.AttachRecorder(&quiet);
        l2.BeginFrame(0.016);
        CHECK(!quiet.Recording().HasRandom() && quiet.Recording().ToText().find("random") == std::string::npos);
        // Clear() starts a fresh seed
        recorder.Clear();
        CHECK(!recorder.Recording().HasRandom());
        recorder.Recording().Clear();

        // a looping replay starts the stream over at each lap
        Fresh();
        Random::Shared().Seed(5);
        FrameRecorder lap;
        GameLoop recLoop(Plain());
        recLoop.AttachRecorder(&lap);
        for (int i = 0; i < 3; ++i) recLoop.BeginFrame(0.01);
        Fresh();
        Random::Shared().Seed(6);
        FramePlayer looping(lap.Recording());
        looping.SetLooping(true);
        GameLoop lapLoop(Plain());
        lapLoop.AttachPlayer(&looping);
        std::vector<std::uint64_t> laps;
        lapLoop.OnUpdate = [&](float) { laps.push_back(Random::Shared().NextUInt64()); };
        for (int i = 0; i < 9; ++i) lapLoop.BeginFrame(0.0);
        bool repeats = laps.size() == 9;
        for (std::size_t i = 3; i < laps.size() && repeats; ++i) repeats = laps[i] == laps[i - 3];
        CHECK(repeats);
        Fresh();
    }

    // ---- replaying a run that had a pause the replay's host does not reproduce ------------------------------------------------------------
    {
        Fresh();
        // The live run: 5 normal frames, a RunFrames(3) that ends in a pause, 4 frames while paused, a resume, 3 frames, a menu pause, 2 frames.
        FrameRecorder recorder;
        GameLoop live(Plain());
        live.AttachRecorder(&recorder);
        std::vector<double> liveTime;
        auto lf = [&](double dt) { live.BeginFrame(dt); liveTime.push_back(Time::ElapsedTimeD()); };
        for (int i = 0; i < 5; ++i) lf(0.016);
        live.RunFrames(3, 1.0f / 60.0f);
        for (int i = 0; i < 3; ++i) lf(0.5);                       // the host's 0.5 s is ignored: these are exact frames
        CHECK(Game::Paused() && Game::IsPausedBy("manual"));
        for (int i = 0; i < 4; ++i) lf(0.02);
        Game::SetPaused(false);
        for (int i = 0; i < 3; ++i) lf(0.016);
        Game::PushPause("menu");
        for (int i = 0; i < 2; ++i) lf(0.02);
        CHECK(Near(liveTime[8], liveTime[7], 0.0) && liveTime[7] > liveTime[4]);   // the pause really froze the live game
        const FrameRecording rec = recorder.Recording();
        CHECK(rec.Frames() == 17 && rec.Summary().pausedFrames == 6);

        auto replay = [&](bool pauses, bool* releasedAtEnd = nullptr) {
            Fresh();
            FramePlayer player(rec);
            GameLoop::Options o = Plain();
            o.replayPauses = pauses;
            GameLoop loop(o);
            loop.AttachPlayer(&player);
            std::vector<double> t;
            while (loop.BeginFrame(0.0)) t.push_back(Time::ElapsedTimeD());
            t.push_back(Time::ElapsedTimeD());                     // the last frame runs and returns false
            if (releasedAtEnd) *releasedAtEnd = !Game::Paused() && Game::PauseReasons().empty();
            return t;
        };
        const std::vector<double> without = replay(false);
        CHECK(without.size() == liveTime.size() && without != liveTime);   // the replay's game keeps moving through the pause: the Time values part ways
        CHECK(without[9] > liveTime[9]);
        bool released = false;
        const std::vector<double> with = replay(true, &released);
        CHECK(with == liveTime);                                    // frame for frame, bit for bit
        CHECK(released);                                            // the replay's own hold is let go when it ends, though the last frames were paused

        // a host that does reproduce the pause: the loop adds nothing of its own
        {
            Fresh();
            FrameRecording r;
            r.Add(0.016, 0, 0); r.Add(0.016, kFramePaused, 0); r.Add(0.016, kFramePaused, 0); r.Add(0.016, 0, 0);
            FramePlayer p(r);
            GameLoop::Options o = Plain(); o.replayPauses = true;
            GameLoop l(o);
            l.AttachPlayer(&p);
            l.BeginFrame(0.0);
            Game::SetPaused(true);                                  // the host's own input replay pauses
            l.BeginFrame(0.0);
            CHECK(Game::PauseReasons() == std::vector<std::string>{"manual"});
            l.BeginFrame(0.0);
            CHECK(Game::PauseReasons() == std::vector<std::string>{"manual"});
            const double t = Time::ElapsedTimeD();
            Game::SetPaused(false);
            l.BeginFrame(0.0);
            CHECK(Near(Time::ElapsedTimeD() - t, 0.016, 1e-7) && Game::PauseReasons().empty());
        }

        // recorded debug steps replay as steps on a game that is not paused
        {
            Fresh();
            FrameRecording r;
            r.Add(0.016, 0, 0);
            r.Add(0.02, (std::uint8_t)(kFramePaused | kFrameStepped), 0);
            r.Add(0.02, (std::uint8_t)(kFramePaused | kFrameStepped), 0);
            r.Add(0.016, kFramePaused, 0);
            r.Add(0.016, 0, 0);
            FramePlayer p(r);
            GameLoop::Options o = Plain(); o.replayPauses = true;
            GameLoop l(o);
            l.AttachPlayer(&p);
            std::vector<double> t;
            std::vector<bool> stepFlags;
            while (l.BeginFrame(0.0)) { t.push_back(Time::ElapsedTimeD()); stepFlags.push_back(l.WasStepFrame()); }
            t.push_back(Time::ElapsedTimeD()); stepFlags.push_back(l.WasStepFrame());
            CHECK(t.size() == 5);
            CHECK(Near(t[0], 0.016, 1e-7) && Near(t[1] - t[0], 0.02, 1e-7) && Near(t[2] - t[1], 0.02, 1e-7));
            CHECK(t[3] == t[2]);                                    // a recorded paused frame is a paused frame
            CHECK(Near(t[4] - t[3], 0.016, 1e-7));
            CHECK(!stepFlags[0] && stepFlags[1] && stepFlags[2] && !stepFlags[3] && !stepFlags[4]);
        }

        // the hold never outlives what asked for it
        {
            Fresh();
            FrameRecording r;
            r.Add(0.016, kFramePaused, 0); r.Add(0.016, kFramePaused, 0); r.Add(0.016, kFramePaused, 0);
            GameLoop::Options o = Plain(); o.replayPauses = true;
            {
                FramePlayer p(r);
                GameLoop l(o);
                l.AttachPlayer(&p);
                l.BeginFrame(0.0);
                CHECK(Game::IsPausedBy(GameLoop::kReplayPause));
                l.AttachPlayer(nullptr);                            // detached mid-replay
                CHECK(!Game::Paused());
            }
            {
                FramePlayer p(r);
                GameLoop l(o);
                l.AttachPlayer(&p);
                l.BeginFrame(0.0);
                CHECK(Game::IsPausedBy(GameLoop::kReplayPause));
                l.Reset();
                CHECK(!Game::Paused());
            }
            {
                FramePlayer p(r);
                GameLoop l(o);
                l.AttachPlayer(&p);
                l.BeginFrame(0.0);
                CHECK(Game::IsPausedBy(GameLoop::kReplayPause));
                l.BeginSession();
                CHECK(!Game::Paused());
                l.BeginFrame(0.016);                                // the replay carries on after a new session
                CHECK(Game::IsPausedBy(GameLoop::kReplayPause));
                Game::Reset();                                      // something else clears the holds: the loop does not trip over it
                l.BeginFrame(0.0);
                CHECK(Game::IsPausedBy(GameLoop::kReplayPause) || p.Finished());
            }
            Fresh();
        }
        // the default is the old behaviour
        CHECK(!GameLoop::Options().replayPauses && GameLoop::Options().replayRandom);
    }

    // ---- FramePacing: a clock reading that is not a number, and a tiny target rate ---------------------------------------------------------
    {
        FrameTimer timer(0.5f);
        CHECK(timer.Tick(10.0) == 0.0f);                                  // the first call only starts the clock
        CHECK(Near(timer.Tick(10.02), 0.02, 1e-6));
        // A NaN or infinite reading is no time passing. Taken as the new "last" it made every later delta NaN: 0 for the rest of the run.
        CHECK(timer.Tick(std::numeric_limits<double>::quiet_NaN()) == 0.0f && !timer.WasClamped());
        CHECK(timer.Tick(std::numeric_limits<double>::infinity()) == 0.0f);
        CHECK(Near(timer.Tick(10.04), 0.02, 1e-6));                       // it carries on from the last good reading
        FrameLimiter tiny(1e-30);                                         // used to plan a sleep of 1e30 seconds
        CHECK(tiny.Enabled() && tiny.TargetFrameSeconds() == FrameLimiter::kMaxFrameSeconds);
        tiny.Plan(0.0);
        CHECK(tiny.Plan(1.0) <= FrameLimiter::kMaxFrameSeconds && tiny.Plan(1.0) > 0.0);
        FrameLimiter denormal(1e-320);                                    // 1 / 1e-320 is infinity
        CHECK(denormal.TargetFrameSeconds() == FrameLimiter::kMaxFrameSeconds);
        FrameLimiter slow(0.5);                                           // two second frames are still two second frames
        CHECK(slow.TargetFrameSeconds() == 2.0);
        CHECK(!FrameLimiter(0.0).Enabled() && !FrameLimiter(std::numeric_limits<double>::quiet_NaN()).Enabled());
    }

    // ---- TimeBudget: a slice of time for work that must not blow a frame ------------------------------------------------------------------------
    {
        double clock = 100.0;
        TimeBudget b(4.0, [&] { return clock; });
        b.Begin();
        int done = 0;
        while (done < 100 && b.Allow()) { ++done; clock += 1.0; }          // each item costs a second: four fit in four
        CHECK(done == 4 && b.Used() == 4 && b.Exceeded() && b.Remaining() == 0.0 && b.Elapsed() == 4.0);
        // an item that alone costs more than the whole budget still gets done, one per slice (it must make progress)
        b.Begin();
        CHECK(b.Allow());
        clock += 10.0;
        CHECK(!b.Allow() && b.Used() == 1 && b.Exceeded());
        b.Begin();
        int three = 0;
        while (three < 10 && b.Allow(3)) { ++three; clock += 10.0; }       // minItems 3: three get through whatever they cost
        CHECK(three == 3);
        b.Begin();
        clock += 1.0;
        CHECK(Near(b.Remaining(), 3.0) && !b.Exceeded() && b.Allow(0));
        // minItems 0 and no budget: nothing runs
        TimeBudget none(0.0, [&] { return clock; });
        none.Begin();
        CHECK(!none.Allow(0) && none.Used() == 0 && none.Allow(1));
        // Run: steps until the work says it is done, or the slice is spent
        int left = 7;
        TimeBudget slice(4.5, [&] { return clock; });
        auto step = [&] { clock += 1.0; return --left > 0; };
        slice.Begin();
        CHECK(slice.Run(step) == 5 && left == 2);                          // elapsed 0, 1, 2, 3, 4 are under 4.5
        slice.Begin();
        CHECK(slice.Run(step) == 2 && left == 0);                          // the step that returned false counts: it ran
        slice.Begin();
        CHECK(slice.Run([] { return false; }, 0) == 1);                    // (a fresh slice with time left)
        // bad numbers: a budget that is not a finite number >= 0 is 0; a clock that goes back or is NaN is no time passing
        for (double bad : {-1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
            TimeBudget t(bad, [&] { return clock; });
            CHECK(t.Seconds() == 0.0);
        }
        TimeBudget back(1.0, [&] { return clock; });
        back.Begin();
        clock -= 50.0;
        CHECK(back.Elapsed() == 0.0 && back.Allow() && !back.Exceeded());
        clock = std::numeric_limits<double>::quiet_NaN();
        CHECK(back.Elapsed() == 0.0);
        // the default clock is Time::RealtimeSinceStartup, which a test can replace
        Time::SetClockSources(&FakeNow, nullptr);
        g_fakeNow = 5.0;
        TimeBudget dflt(2.0);
        dflt.Begin();
        g_fakeNow = 6.0;
        CHECK(dflt.Allow(0) && Near(dflt.Remaining(), 1.0));
        g_fakeNow = 7.5;
        CHECK(!dflt.Allow(0) && dflt.Exceeded());
        Time::SetClockSources(nullptr, nullptr);
    }

#ifndef LOOP_AUDIT5_MINI
    // ---- Application::Run starts a session ----------------------------------------------------------------------------------------------------
    {
        Game::Reset();
        Time::ResetForTesting();
        Application::Config cfg;
        cfg.targetFps = 0.0f; cfg.maxFrames = 3; cfg.clearScreen = false; cfg.quitKey = 0;
        Application app(cfg);
        app.SetRenderer(std::make_unique<NullRenderer>());
        Time::SetTimeScale(0.5f);                                   // a host may set one before Run
        {
            Scene scene("one");
            scene.CreateGameObject("x");
            app.Run(scene);
        }
        CHECK(Time::FrameCount() == 3 && Time::SessionFrameCount() == 3);
        CHECK(Time::BaseTimeScale() == 0.5f);                       // the scale is left alone, as before
        {
            Scene scene("two");
            scene.CreateGameObject("x");
            app.Run(scene);
        }
        CHECK(Time::FrameCount() == 6);                             // the process clock keeps counting
        CHECK(Time::SessionFrameCount() == 3);                      // the session started again (it read 6)
        CHECK(Time::SessionTime() > 0.0 && Time::SessionTime() < Time::ElapsedTimeD() + 1e-9);
        Time::ResetForTesting();
        Game::Reset();
    }
#endif

    TEST_MAIN_RESULT();
}
