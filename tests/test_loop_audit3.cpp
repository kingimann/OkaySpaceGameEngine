// 1.1 third pass audit: GameLoop edge cases (huge, NaN and negative times, hostile options, callbacks
// that call back into the loop), Application error paths and its pacing option, and what each caller of
// Game::Reset() should be doing. Everything runs on made-up frame times; the two Application cases that
// need real time check only bounds a loaded machine cannot break (a frame can run long, never short).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/Clock.hpp"
#include "okay/Core/FramePacing.hpp"
#include "okay/Core/FrameRecord.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr float  kNaNf = std::numeric_limits<float>::quiet_NaN();
constexpr float  kInff = std::numeric_limits<float>::infinity();

bool Near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

void Fresh() {
    Game::SetPauseOnFocusLoss(false);
    Game::NotifyFocus(true);
    Game::Reset();
    Time::ResetForTesting();
    Time::SetFixedDeltaTime(0.02f);
    ClockSet::Global().Reset();
}

bool TimeIsSane() {
    return std::isfinite(Time::ElapsedTimeD()) && std::isfinite(Time::UnscaledTime()) && std::isfinite(Time::DeltaTime()) &&
           std::isfinite(Time::UnscaledDeltaTime()) && Time::DeltaTime() >= 0.0f && Time::UnscaledDeltaTime() >= 0.0f;
}

class NullRenderer : public IRenderer {
public:
    int frames = 0;
    std::function<void()> onEnd;
    void BeginFrame(const Color&) override {}
    void EndFrame() override { ++frames; if (onEnd) onEnd(); }
    void SetCamera(const Vec3&, float, float) override {}
    void DrawQuad(const Vec3&, const Vec2&, const Color&, char) override {}
    Viewport GetViewport() const override { return {10, 10}; }
};

class Hook : public Component {
public:
    int frame = 0;
    std::function<void(int)> fn;
    std::function<void()> onStart;
    void Start() override { if (onStart) onStart(); }
    void Update(float) override { ++frame; if (fn) fn(frame); }
};

Application::Config Quiet() {
    Application::Config cfg;
    cfg.targetFps = 0.0f; cfg.maxFrames = 30; cfg.clearScreen = false; cfg.smoothDt = false;
    return cfg;
}

struct Rig {
    Scene scene{"t"};
    Hook* hook = nullptr;
    NullRenderer* renderer = nullptr;
    explicit Rig(Application& app) {
        auto r = std::make_unique<NullRenderer>();
        renderer = r.get();
        app.SetRenderer(std::move(r));
        hook = scene.CreateGameObject("hook")->AddComponent<Hook>();
    }
};

}  // namespace

int main() {
    RUN_SUITE("loop_audit3");
    Log::console = false;

    // ---- BUG: a "replacement" step longer than the frame made a clamped frame step MORE than it measured --------------------------------------------------
    // hitchReplacement is what a clamped frame steps instead of maxDelta (Application: its target frame time). With a replacement above the real time
    // the clamp lengthened the frame (game runs fast), LostRealTime went negative, and an infinite replacement made it -infinity.
    {
        Fresh();
        GameLoop::Options o;
        o.maxDelta = 0.1f;
        o.hitchReplacement = 1.0f;
        o.smoothDelta = false;
        GameLoop loop(o);
        loop.BeginFrame(0.2);                                     // longer than maxDelta, shorter than the replacement
        CHECK(loop.FrameDeltaStepped() <= 0.2f + 1e-6f);          // used to step 1.0 s for a 0.2 s frame
        CHECK(loop.LostRealTime() >= 0.0);
        CHECK(Time::UnscaledDeltaTime() <= 0.2f + 1e-6f);

        Fresh();
        o.hitchReplacement = kInff;
        GameLoop loop2(o);
        loop2.BeginFrame(0.5);
        CHECK(std::isfinite(loop2.LostRealTime()) && loop2.LostRealTime() >= 0.0);   // used to be -infinity
        CHECK(Time::UnscaledDeltaTime() > 0.0f && Time::UnscaledDeltaTime() <= 0.5f); // used to be a 0 s frame (Step(inf) counts as 0)
        CHECK_NEAR(loop2.FrameDeltaStepped(), 0.1, 1e-6);          // an invalid replacement means maxDelta

        Fresh();
        o.hitchReplacement = kNaNf;
        GameLoop loop3(o);
        loop3.BeginFrame(0.5);
        CHECK_NEAR(loop3.FrameDeltaStepped(), 0.1, 1e-6);
        // the intended use is unchanged: a replacement below maxDelta
        Fresh();
        o.hitchReplacement = 0.016f;
        GameLoop loop4(o);
        loop4.BeginFrame(0.5);
        CHECK_NEAR(loop4.FrameDeltaStepped(), 0.016, 1e-6);
        CHECK(loop4.ClampedFrames() == 1 && Near(loop4.LostRealTime(), 0.5 - 0.016));
    }

    // ---- BUG: Application::Run threw away every option set on loop() before it ---------------------------------------------------------------------------------
    // Run replaced the loop's Options with a fresh struct built from Config, so capture mode, a frame budget, the stats window or hitch logging that the
    // host had set on app.loop() were silently dropped.
    {
        Fresh();
        Application app(Quiet());
        Rig rig(app);
        std::vector<float> seen;
        rig.hook->fn = [&](int) { seen.push_back(Time::UnscaledDeltaTime()); };
        CHECK(app.loop().SetCapture(50.0f, 6));
        GameLoop::Options o = app.loop().options();
        o.statsWindow = 7; o.logHitches = true; o.hitchThreshold = 0.05f;
        app.loop().SetOptions(o);
        app.Run(rig.scene);
        CHECK(rig.hook->frame == 6);                               // the frame budget stood: exactly six frames (the Config said 30)
        CHECK(seen.size() == 6);
        bool exact = true; for (float d : seen) if (std::fabs(d - 0.02f) > 1e-6f) exact = false;
        CHECK(exact);                                              // capture mode stood: every frame took exactly 1/50 s whatever the machine did
        CHECK(app.loop().CaptureActive() && app.loop().options().statsWindow == 7 && app.loop().options().logHitches);
        CHECK(app.loop().Stats().Window() == 7 && app.loop().options().hitchThreshold == 0.05f);
        // the options the Config has a field for still come from the Config
        CHECK(app.loop().options().maxDelta == 0.25f && app.loop().options().maxFixedSteps == 5 && !app.loop().options().smoothDelta);
        app.loop().ClearCapture();
        // and a second Run on the same Application starts a fresh budget count
        Rig rig2(app);
        app.loop().SetFrameBudget(3);
        app.Run(rig2.scene);
        CHECK(rig2.hook->frame == 3);
    }

    // ---- hostile numbers into BeginFrame ----------------------------------------------------------------------------------------------------------------------------
    {
        const double inputs[] = {kNaN, -kNaN, kInf, -kInf, -1.0, -1e300, 0.0, 1e-320, 5e-324, 1e-9, 0.016, 0.25, 0.2500001, 1e6, 1e30, 1e300, std::numeric_limits<double>::max()};
        for (int variant = 0; variant < 4; ++variant) {
            Fresh();
            GameLoop::Options o;
            o.smoothDelta = (variant & 1) != 0;
            o.refreshHz = (variant & 1) ? 60.0f : 0.0f;
            if (variant & 2) o.maxDelta = 0.0f;                    // the clamp off: the host's number reaches Time as it is
            GameLoop loop(o);
            FrameRecorder rec;
            loop.AttachRecorder(&rec);
            int fixed = 0;
            loop.OnFixedUpdate = [&](float) { ++fixed; };
            bool sane = true;
            for (double in : inputs) {
                const bool ok = loop.BeginFrame(in);
                if (!ok) sane = false;
                if (!TimeIsSane() && !(variant & 2)) sane = false;                  // with the clamp on, time never goes bad
                if (!(loop.Alpha() >= 0.0f && loop.Alpha() < 1.0f)) sane = false;
                if (loop.FixedStepsThisFrame() < 0 || loop.FixedStepsThisFrame() > o.maxFixedSteps) sane = false;
                if (!std::isfinite(loop.LostRealTime()) || loop.LostRealTime() < 0.0) sane = false;
            }
            CHECK(sane);
            CHECK(loop.FrameCount() == sizeof inputs / sizeof inputs[0] && rec.Recording().Frames() == loop.FrameCount());
            const FrameStatsSnapshot s = loop.Stats().Snapshot();
            CHECK(std::isfinite(s.avgMs) || (variant & 2));                         // the statistics hold the measured time as it was
            CHECK(!std::isnan(s.avgMs) && !std::isnan(s.worstMs));
        }
        // with the clamp on, a monstrous frame is one clamped step, and the one after it is a normal frame
        Fresh();
        GameLoop loop;
        loop.BeginFrame(std::numeric_limits<double>::max());
        CHECK(loop.FrameDeltaClamped() == 0.25f && loop.ClampedFrames() == 1);
        CHECK(std::isfinite(loop.FrameDeltaReal()) && loop.FrameDeltaReal() > 1e30f);   // used to read infinity (a double beyond float range, cast)
        loop.BeginFrame(0.016);
        CHECK_NEAR(Time::UnscaledDeltaTime(), 0.016, 1e-6);
        CHECK(loop.FrameDeltaReal() == 0.016f);
    }

    // ---- BUG: a tiny fixed step with a huge step cap kept one BeginFrame busy for billions of callbacks ----------------------------------------------------------------------
    {
        Fresh();
        GameLoop::Options o;
        o.fixedStep = 1e-9f;
        o.maxFixedSteps = INT_MAX;
        GameLoop loop(o);
        std::uint64_t calls = 0;
        loop.OnFixedUpdate = [&](float) { ++calls; };
        loop.BeginFrame(0.1);                                       // 0.1 s / 1e-9 s = 1e8 steps due (the cap used to be INT_MAX)
        CHECK(calls == (std::uint64_t)GameLoop::kMaxFixedStepsPerFrame && loop.FixedStepsThisFrame() == GameLoop::kMaxFixedStepsPerFrame);
        CHECK(loop.DroppedFixedTime() > 0.09);                      // the rest is dropped, as with any cap
        // a fast forward still raises the cap in proportion, up to the same limit
        Fresh();
        GameLoop fast;
        fast.SetDebugTimeScale(32.0f);
        fast.BeginFrame(0.25);                                      // 8 s of game time at 50 steps a second: 400 steps due, the cap is 5 x 32 = 160
        CHECK(fast.FixedStepsThisFrame() == 160);
    }

    // ---- hostile options --------------------------------------------------------------------------------------------------------------------------------------------------
    {
        const float bad[] = {kNaNf, kInff, -kInff, -1.0f, 0.0f, 1e-30f, 1e30f};
        for (float v : bad) {
            Fresh();
            GameLoop::Options o;
            o.maxDelta = v; o.hitchReplacement = v; o.hitchThreshold = v; o.fixedStep = v; o.refreshHz = v; o.firstFrameDelta = v;
            o.captureFps = v; o.stepDelta = v;
            o.maxFixedSteps = v > 0.0f ? INT_MAX : -5;
            o.statsWindow = v > 0.0f ? INT_MAX : -3;
            o.frameBudget = v > 0.0f ? INT64_MAX : -9;
            o.smoothDelta = true;
            GameLoop loop(o);
            int steps = 0;
            loop.OnFixedUpdate = [&](float dt) { if (std::isfinite(dt) && dt > 0.0f) ++steps; };
            bool sane = true;
            for (int i = 0; i < 20; ++i) {
                loop.BeginFrame(i == 5 ? 0.4 : 0.016);
                if (!TimeIsSane() || !(loop.Alpha() >= 0.0f && loop.Alpha() < 1.0f)) sane = false;
                if (!std::isfinite(loop.FixedStep()) || loop.FixedStep() <= 0.0f) sane = false;
            }
            CHECK(sane);
            CHECK(loop.FrameCount() >= 1);
            CHECK(loop.Stats().Window() <= FrameStats::kMaxWindow);
            loop.SetDebugTimeScale(2.0f);
            loop.RequestStep();                                      // not paused: refused, not a crash
            CHECK(steps >= 0);
        }
    }

    // ---- callbacks that call back into the loop: every kind, from every callback ----------------------------------------------------------------------------------------
    {
        enum { kFixed, kUpdate, kLate, kPause, kResume, kHitch, kStep, kStepsDone, kKinds };
        const char* kindName[kKinds] = {"fixed", "update", "late", "pause", "resume", "hitch", "step", "stepsdone"};
        for (int kind = 0; kind < kKinds; ++kind) {
            Fresh();
            GameLoop loop;
            int depth = 0, maxDepth = 0, calls = 0;
            auto reenter = [&] {
                ++calls;
                ++depth; maxDepth = std::max(maxDepth, depth);
                loop.BeginFrame(0.016);                              // a modal dialog that pumps the loop
                loop.BeginFrame(kNaN);
                --depth;
            };
            loop.OnFixedUpdate = [&](float) { if (kind == kFixed && calls == 0) reenter(); };
            loop.OnUpdate = [&](float) { if (kind == kUpdate && calls == 0) reenter(); };
            loop.OnLateUpdate = [&](float) { if (kind == kLate && calls == 0) reenter(); };
            loop.OnPause = [&] { if (kind == kPause && calls == 0) reenter(); };
            loop.OnResume = [&] { if (kind == kResume && calls == 0) reenter(); };
            loop.OnHitch = [&](float, float) { if (kind == kHitch && calls == 0) reenter(); };
            loop.OnStep = [&](float, int) { if (kind == kStep && calls == 0) reenter(); };
            loop.OnStepsFinished = [&] { if (kind == kStepsDone && calls == 0) reenter(); };
            loop.BeginFrame(0.016);
            if (kind == kPause || kind == kResume || kind == kStep || kind == kStepsDone) {
                Game::SetPaused(true);
                loop.BeginFrame(0.016);                              // OnPause
                if (kind != kPause) {
                    if (kind == kResume) { Game::SetPaused(false); loop.BeginFrame(0.016); }
                    else { loop.RequestStep(); loop.BeginFrame(0.016); }
                }
                Game::SetPaused(false);
            }
            if (kind == kHitch) loop.BeginFrame(0.5);
            loop.BeginFrame(0.016);
            if (calls != 1 || maxDepth != 1) { std::printf("  re-entrancy via %s: calls=%d depth=%d\n", kindName[kind], calls, maxDepth); }
            CHECK(calls == 1 && maxDepth == 1);                       // the callback ran once; the pumped frames did not nest
            CHECK(TimeIsSane());
            // the pumped calls ran no frame of their own: Time's frame count equals the loop's
            CHECK(Time::FrameCount() == loop.FrameCount());
        }
    }

    // ---- callbacks that change the loop under it: every way of calling in, from every callback of a frame --------------------------------------------------------------------
    {
        for (int where = 0; where < 4; ++where) {                    // 0 fixed, 1 update, 2 late, 3 pause
            for (int what = 0; what < 10; ++what) {
                Fresh();
                GameLoop l;
                GameLoop::Options o; o.maxFixedSteps = 8;
                l.SetOptions(o);
                FrameRecorder rec;
                bool done = false;
                int fixedRan = 0;
                auto doIt = [&] {
                    if (done) return;
                    done = true;
                    switch (what) {
                        case 0: l.Reset(); break;
                        case 1: l.BeginSession(); break;
                        case 2: { GameLoop::Options q = l.options(); q.maxFixedSteps = 1; q.fixedStep = 0.005f; q.smoothDelta = true; l.SetOptions(q); } break;
                        case 3: l.Stop(); break;
                        case 4: l.SetCapture(30.0f, 2); break;
                        case 5: l.AttachRecorder(&rec); l.AttachRecorder(nullptr); break;
                        case 6: l.SetFrameBudget(1); break;
                        case 7: l.RequestStep(2); l.CancelStep(); break;
                        case 8: l.SetDebugTimeScale(8.0f); l.SetDebugTimeScale(1.0f); break;
                        default: { GameLoop::Options q = l.options(); q.statsWindow = 3; l.SetOptions(q); l.Stats().Clear(); } break;
                    }
                };
                l.OnFixedUpdate = [&](float) { ++fixedRan; if (where == 0) doIt(); };
                l.OnUpdate = [&](float) { if (where == 1) doIt(); };
                l.OnLateUpdate = [&](float) { if (where == 2) doIt(); };
                l.OnPause = [&] { if (where == 3) doIt(); };
                if (where == 3) Game::SetPaused(true);
                l.BeginFrame(0.1);                                   // 5 fixed steps due (none while paused)
                CHECK(done);
                CHECK(TimeIsSane() && l.Alpha() >= 0.0f && l.Alpha() < 1.0f);
                if (where == 0) CHECK(fixedRan == 5);                // the frame's steps keep the count they were due with, whatever the callback did to the loop
                Game::SetPaused(false);
                l.Reset();                                           // (a Stop or a budget the callback set is cleared)
                l.SetCapture(0.0f);                                  // refused: capture off stays off
                l.ClearCapture();
                l.SetFrameBudget(0);
                for (int i = 0; i < 5; ++i) l.BeginFrame(0.016);
                CHECK(TimeIsSane() && l.FrameCount() == 5);
            }
        }
    }

    // ---- options changed mid-frame apply from the next frame, never half-way through this one ---------------------------------------------------------------------
    {
        Fresh();
        GameLoop::Options o;
        o.smoothDelta = false;
        GameLoop loop(o);
        std::vector<int> stepsPerFrame;
        loop.OnUpdate = [&](float) {
            if (loop.FrameCount() == 2) { GameLoop::Options q = loop.options(); q.fixedStep = 0.04f; q.maxDelta = 0.05f; loop.SetOptions(q); }
        };
        for (int i = 0; i < 4; ++i) { loop.BeginFrame(0.1); stepsPerFrame.push_back(loop.FixedStepsThisFrame()); }
        CHECK(stepsPerFrame[0] == 5 && stepsPerFrame[1] == 5);        // 0.1 s at 0.02 s: five steps (the cap)
        CHECK(stepsPerFrame[2] == 1 && stepsPerFrame[3] <= 2);        // from frame 3 on: 0.05 s clamp, 0.04 s steps
        CHECK_NEAR(loop.FixedStep(), 0.04, 1e-9);
    }

    // ---- Stop() inside a frame: the frame finishes, the loop says false, and Reset() lets it run again --------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        int updates = 0;
        loop.OnUpdate = [&](float) { ++updates; if (updates == 2) loop.Stop(); };
        CHECK(loop.BeginFrame(0.016));
        CHECK(!loop.BeginFrame(0.016) && updates == 2);              // the frame that stopped it still ran to the end
        CHECK(loop.QuitRequested());
        loop.Reset();
        CHECK(!loop.QuitRequested() && loop.BeginFrame(0.016));
    }

    // ---- Application error paths ----------------------------------------------------------------------------------------------------------------------------------------------
    {
        // scene.Start() throws: the terminal session is closed, the exception reaches the caller (or is contained)
        Fresh();
        Application app(Quiet());
        Rig rig(app);
        rig.hook->onStart = [] { throw std::runtime_error("start failed"); };
        bool threw = false;
        try { app.Run(rig.scene); } catch (const std::runtime_error& e) { threw = std::string(e.what()) == "start failed"; }
        CHECK(threw && app.LastError() == "start failed" && rig.hook->frame == 0);
        CHECK(!app.loop().OnUpdate);                                  // no callback left pointing at the dead scene

        Application::Config cfg = Quiet(); cfg.catchExceptions = true;
        Application app2(cfg);
        Rig rig2(app2);
        rig2.hook->onStart = [] { throw 7; };
        app2.Run(rig2.scene);
        CHECK(app2.LastError() == "unknown exception" && app2.ExitCode() == 1 && Game::QuitReason() == "exception");
        // the next run on the same Application is clean (a contained failure leaves no quit behind)
        Application app3(Quiet());
        Rig rig3(app3);
        app3.Run(rig3.scene);
        CHECK(rig3.hook->frame == 30 && app3.ExitCode() == 0 && app3.LastError().empty() && !Game::QuitRequested());

        // the renderer throws
        Application app4(Quiet());
        Rig rig4(app4);
        rig4.renderer->onEnd = [&] { if (rig4.renderer->frames == 3) throw std::runtime_error("device lost"); };
        threw = false;
        try { app4.Run(rig4.scene); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw && rig4.renderer->frames == 3 && app4.LastError() == "device lost");

        // a throwing fixed update
        Application::Config cfg5 = Quiet(); cfg5.fixedStep = 1e-8f;      // tiny steps: every frame owes some, so the fixed update really runs
        Application app5(cfg5);
        Rig rig5(app5);
        int fixedCalls = 0;
        app5.loop().OnFixedUpdate = [&](float) { if (++fixedCalls == 4) throw std::logic_error("physics"); };
        threw = false;
        try { app5.Run(rig5.scene); } catch (const std::logic_error&) { threw = true; }
        CHECK(threw && app5.LastError() == "physics");
        CHECK(!Time::InFixedStep());                                  // the step marker is cleared however the callback ended
        // the loop of an Application that ended by an exception can be used for the next one
        app5.loop().OnFixedUpdate = nullptr;
        Rig rig5b(app5);
        app5.Run(rig5b.scene);
        CHECK(rig5b.hook->frame == 30 && app5.LastError().empty());

        // Quit() from another thread while it runs
        Application app6(Quiet());
        app6.SetRenderer(std::make_unique<NullRenderer>());
        Scene s6("t6");
        auto* hook6 = s6.CreateGameObject("h")->AddComponent<Hook>();
        Application::Config c6 = Quiet(); c6.maxFrames = 0;           // run until told
        Application app7(c6);
        app7.SetRenderer(std::make_unique<NullRenderer>());
        Scene s7("t7");
        auto* hook7 = s7.CreateGameObject("h")->AddComponent<Hook>();
        std::atomic<bool> started{false};
        hook7->fn = [&](int f) { if (f == 3) started = true; };
        std::thread stopper([&] { while (!started) std::this_thread::yield(); app7.Quit(); });
        app7.Run(s7);
        stopper.join();
        CHECK(hook7->frame >= 3 && app7.ExitCode() == 0);
        (void)hook6;

        // a quit requested from inside Update ends it that frame; the quit key is ignored under a text field
        Application app8(Quiet());
        Rig rig8(app8);
        rig8.hook->fn = [](int f) { if (f == 5) Game::RequestQuit("done", 3); };
        app8.Run(rig8.scene);
        CHECK(rig8.hook->frame == 5 && app8.ExitCode() == 3);
        Game::Reset();
    }

    // ---- Application pacing: Sleep stays the default; Limiter holds the rate ------------------------------------------------------------------------------------------------
    {
        CHECK(Application::Config().pacing == Application::Config::Pacing::Sleep);
        // The decision, on a fake clock that sleeps late: each sleep returns a little after it was asked to (the coarse timers
        // of a busy machine or an older Windows). "Sleep" is the arithmetic Application used, "Limiter" is FrameLimiter::Plan + EndFrame.
        const double target = 1.0 / 30.0;
        const int frames = 600;
        auto overshoot = [](int i) { return 0.0005 + 0.001 * (double)((i * 7) % 5) / 4.0; };   // 0.5 .. 1.5 ms, repeatable
        // old: sleep(target - elapsed)
        double now = 0.0;
        for (int i = 0; i < frames; ++i) {
            const double start = now;
            now += 0.002;                                            // the frame's work
            const double remaining = target - (now - start);
            if (remaining > 0.0) now += remaining + overshoot(i);
        }
        const double sleepRate = (double)frames / now;
        // new: deadline based, sleep(wait - 1.5 ms) then yield until the deadline
        FrameLimiter limiter(30.0);
        double now2 = 0.0;
        for (int i = 0; i < frames; ++i) {
            now2 += 0.002;
            const double wait = limiter.Plan(now2);
            if (wait > 0.0) {
                const double deadline = now2 + wait;
                if (wait > 0.0015) now2 += (wait - 0.0015) + overshoot(i);   // the sleep is late
                if (now2 < deadline) now2 = deadline;                        // yielding runs it up to the deadline (a sleep that was late is already past it)
            }
        }
        const double limiterRate = (double)frames / now2;
        CHECK(sleepRate < 29.5);                                      // the old arithmetic lands under the target (about 29.1 fps)
        CHECK(Near(limiterRate, 30.0, 0.05));                         // the limiter pays an overshoot back with the next frame
        CHECK(limiterRate > sleepRate + 0.5);

        // The wiring, on the real clock with bounds that load cannot break: a frame can be late, never early.
        for (int mode = 0; mode < 2; ++mode) {
            Fresh();
            Application::Config cfg = Quiet();
            cfg.targetFps = 200.0f;                                   // 5 ms frames
            cfg.maxFrames = 20;
            cfg.pacing = mode == 0 ? Application::Config::Pacing::Sleep : Application::Config::Pacing::Limiter;
            Application app(cfg);
            Rig rig(app);
            const auto t0 = std::chrono::steady_clock::now();
            app.Run(rig.scene);
            const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            CHECK(rig.hook->frame == 20);
            CHECK(took >= 19 * 0.005 * 0.97);                         // 19 full waits at least (the first frame is not paced by the limiter)
            CHECK(took < 5.0);
            CHECK(app.loop().HitchCount() == 0);
        }
        // targetFps <= 0 is uncapped in both modes
        for (int mode = 0; mode < 2; ++mode) {
            Fresh();
            Application::Config cfg = Quiet();
            cfg.targetFps = 0.0f; cfg.maxFrames = 200;
            cfg.pacing = mode == 0 ? Application::Config::Pacing::Sleep : Application::Config::Pacing::Limiter;
            Application app(cfg);
            Rig rig(app);
            const auto t0 = std::chrono::steady_clock::now();
            app.Run(rig.scene);
            CHECK(rig.hook->frame == 200);
            CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < 3.0);
        }
    }

    // ---- Game::Reset keeps the time scale; BeginSession is what a new session wants (the caller audit in docs/loop_and_time.md) ---------------------------------------------
    {
        Fresh();
        Time::SetTimeScale(0.0f);                                     // a script froze the game
        Game::Reset();
        CHECK(Time::BaseTimeScale() == 0.0f);                         // Reset leaves it: the next Play would start frozen
        Game::BeginSession();
        CHECK(Time::BaseTimeScale() == 1.0f);
        Time::SetTimeScale(0.3f);
        GameLoop loop;
        loop.BeginSession();
        CHECK(Time::BaseTimeScale() == 1.0f);
        Fresh();
    }

    TEST_MAIN_RESULT();
}
