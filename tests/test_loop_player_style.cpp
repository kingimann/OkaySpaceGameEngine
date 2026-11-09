// 1.1 The safety net for moving the windowed player (and the editor's Play loop) onto GameLoop.
//
// player/main.cpp ran this every frame until the third pass, when it moved onto GameLoop exactly as the
// "adopting" sketch in docs/loop_and_time.md says (the editor's Play loop is still on it). The lines below
// are the old per-frame code, kept here as the reference the new one is compared with, with the SDL timer
// replaced by made-up frame times:
//
//     float dt = (now - last) / freq;                       // measured
//     if (dt > 0.1f) dt = 0.1f;                             // clamp
//     const float stepDt = cfg.smoothDt ? Time::SmoothDelta(dt, cfg.vsync ? refreshHz : 0.0f) : dt;
//     const bool tick = windowFocused || cfg.runInBackground;
//     Time::Step(tick ? stepDt : 0.0f);
//     if (tick) scene.Update(Time::DeltaTime());
//     if (Game::QuitRequested()) running = false;
//
// The same synthetic frame sequences (steady, spikes, an alt-tab gap, paused ticks, slow motion,
// a hit stop, a quit, bad numbers) are driven through that code and through a GameLoop configured
// to match, and every Time value of every frame is compared for exact equality. Where the two
// differ on purpose, a test says so and pins the difference. Nothing sleeps, nothing reads a clock.
#include "test_framework.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
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

std::uint32_t g_s = 90210u;
float Rand01() { g_s ^= g_s << 13; g_s ^= g_s >> 17; g_s ^= g_s << 5; return (float)(g_s >> 8) / 16777216.0f; }

enum Action { kNone, kPauseOn, kPauseOff, kQuit, kHitStop, kSlowMo, kNormalSpeed, kPushMenu, kPopMenu };

struct FrameIn {
    float dt = 0.016f;          // what the timer measured (float, as the player computes it)
    bool focused = true;
    int action = kNone;         // a script / menu doing something at the start of this frame
};
using Seq = std::vector<FrameIn>;

struct Cfg {
    bool smoothDt = true;
    bool vsync = false;
    float refreshHz = 60.0f;
    bool runInBackground = false;
};

struct Trace {
    float delta, unscaled, raw;
    double elapsed, unscaledTime;
    std::uint64_t frame;
    int updates;               // scene.Update calls so far
    float sceneDt;             // the dt of the last scene.Update
    bool running;
    bool operator==(const Trace& o) const {
        return delta == o.delta && unscaled == o.unscaled && raw == o.raw && elapsed == o.elapsed && unscaledTime == o.unscaledTime &&
               frame == o.frame && updates == o.updates && sceneDt == o.sceneDt && running == o.running;
    }
};

void ApplyAction(int a) {
    switch (a) {
        case kPauseOn: Game::SetPaused(true); break;
        case kPauseOff: Game::SetPaused(false); break;
        case kQuit: Game::RequestQuit("test quit", 0); break;
        case kHitStop: Time::HitStop(0.12f, 0.0f); break;
        case kSlowMo: Time::SetTimeScale(0.4f); break;
        case kNormalSpeed: Time::SetTimeScale(1.0f); break;
        case kPushMenu: Game::PushPause("menu"); break;
        case kPopMenu: Game::PopPause("menu"); break;
        default: break;
    }
}

Trace Snap(int updates, float sceneDt, bool running) {
    return Trace{Time::DeltaTime(), Time::UnscaledDeltaTime(), Time::RawDeltaTime(), Time::ElapsedTimeD(), Time::UnscaledTime(),
                 Time::FrameCount(), updates, sceneDt, running};
}

// ---- the player's loop, as it is in player/main.cpp ------------------------------------------------------------------------------------------
// `pollute` = whether SmoothDelta sees the frames of an unfocused window (the player calls it on every frame).
std::vector<Trace> RunPlayer(const Seq& seq, const Cfg& cfg, bool pollute = true) {
    Fresh();
    std::vector<Trace> out;
    int updates = 0;
    float sceneDt = 0.0f;
    bool running = true;
    for (const FrameIn& f : seq) {
        ApplyAction(f.action);
        float dt = f.dt;
        if (dt > 0.1f) dt = 0.1f;
        const bool tick = f.focused || cfg.runInBackground;
        float stepDt = dt;
        if (cfg.smoothDt && (pollute || tick)) stepDt = Time::SmoothDelta(dt, cfg.vsync ? cfg.refreshHz : 0.0f);
        else if (cfg.smoothDt) stepDt = 0.0f;
        Time::Step(tick ? stepDt : 0.0f);
        if (tick) { ++updates; sceneDt = Time::DeltaTime(); }
        if (Game::QuitRequested()) running = false;
        out.push_back(Snap(updates, sceneDt, running));
        if (!running) break;
    }
    Fresh();
    return out;
}

// ---- the same frames through GameLoop, configured as the adoption sketch in docs/loop_and_time.md says ---------------------------------------------------
struct LoopRun {
    std::vector<Trace> trace;
    int rawUpdates = 0;       // OnUpdate calls, gated or not
};
LoopRun RunLoop(const Seq& seq, const Cfg& cfg, bool gateOnTick = true) {
    Fresh();
    LoopRun r;
    GameLoop::Options o;
    o.maxDelta = 0.1f;                           // what the player clamps to
    o.smoothDelta = cfg.smoothDt;
    o.refreshHz = cfg.vsync ? cfg.refreshHz : 0.0f;
    GameLoop loop(o);
    int updates = 0;
    float sceneDt = 0.0f;
    bool tick = true;
    loop.OnUpdate = [&](float dt) { ++r.rawUpdates; if (!gateOnTick || tick) { ++updates; sceneDt = dt; } };
    for (const FrameIn& f : seq) {
        ApplyAction(f.action);
        tick = f.focused || cfg.runInBackground;
        const bool running = loop.BeginFrame(tick ? (double)f.dt : 0.0);
        r.trace.push_back(Snap(updates, sceneDt, running));
        if (!running) break;
    }
    Fresh();
    return r;
}

int FirstDiff(const std::vector<Trace>& a, const std::vector<Trace>& b) {
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) if (!(a[i] == b[i])) return (int)i;
    return a.size() == b.size() ? -1 : (int)n;
}

// ---- synthetic sequences ---------------------------------------------------------------------------------------------------------------------------------
Seq Steady(int n, float base = 1.0f / 60.0f, float jitter = 0.002f) {
    Seq s;
    for (int i = 0; i < n; ++i) { FrameIn f; f.dt = base + (Rand01() - 0.5f) * jitter; s.push_back(f); }
    return s;
}
Seq Spikes(int n) {
    Seq s = Steady(n);
    for (int i = 20; i < n; i += 37) s[(std::size_t)i].dt = 0.3f + Rand01() * 2.0f;            // 0.3 .. 2.3 s stalls (clamped to 0.1)
    for (int i = 33; i < n; i += 71) s[(std::size_t)i].dt = 0.1f;                              // exactly the limit
    for (int i = 47; i < n; i += 90) s[(std::size_t)i].dt = 0.1000001f;                        // just over it
    return s;
}
Seq WithAltTab(int n) {
    Seq s = Steady(n);
    for (int i = 100; i < 220; ++i) { s[(std::size_t)i].focused = false; s[(std::size_t)i].dt = 0.05f + Rand01() * 0.2f; }
    s[220].dt = 3.0f;                                                                           // the frame that finds the window again
    for (int i = 400; i < 430; ++i) { s[(std::size_t)i].focused = false; }
    return s;
}
Seq WithPauses(int n) {
    Seq s = Steady(n);
    s[50].action = kPauseOn; s[90].action = kPauseOff;
    s[120].action = kPushMenu; s[125].action = kPauseOn; s[140].action = kPopMenu; s[160].action = kPauseOff;
    s[200].action = kSlowMo; s[260].action = kHitStop; s[300].action = kNormalSpeed;
    s[330].action = kHitStop; s[331].action = kHitStop;
    return s;
}
Seq WithBadNumbers(int n) {
    Seq s = Steady(n);
    s[10].dt = std::numeric_limits<float>::quiet_NaN();
    s[11].dt = -1.0f;
    s[12].dt = 0.0f;
    s[31].dt = 1e-6f;
    return s;
}
Seq WithQuit(int n, int at) {
    Seq s = Steady(n);
    s[(std::size_t)at].action = kQuit;
    return s;
}

}  // namespace

int main() {
    RUN_SUITE("loop_player_style");
    Log::console = false;

    // ---- identical on every sequence where the window keeps focus -------------------------------------------------------------------------------------------
    {
        struct Named { const char* name; Seq seq; };
        g_s = 111u;
        std::vector<Named> all;
        all.push_back({"steady", Steady(600)});
        all.push_back({"steady 144 Hz", Steady(600, 1.0f / 144.0f, 0.0005f)});
        all.push_back({"steady 30 Hz", Steady(300, 1.0f / 30.0f, 0.004f)});
        all.push_back({"spikes", Spikes(600)});
        all.push_back({"paused ticks, slow motion, hit stop", WithPauses(400)});
        all.push_back({"bad numbers", WithBadNumbers(120)});
        all.push_back({"quit", WithQuit(300, 200)});
        {   // a long noisy run
            Seq s = Steady(2000, 1.0f / 60.0f, 0.02f);
            for (std::size_t i = 0; i < s.size(); ++i) if (Rand01() < 0.03f) s[i].dt = Rand01() * 0.4f;
            all.push_back({"noisy", s});
        }
        int sequences = 0, framesCompared = 0;
        for (const Named& n : all) {
            for (int smooth = 0; smooth < 2; ++smooth) for (int vsync = 0; vsync < 2; ++vsync) {
                Cfg cfg; cfg.smoothDt = smooth != 0; cfg.vsync = vsync != 0;
                const std::vector<Trace> p = RunPlayer(n.seq, cfg);
                const LoopRun l = RunLoop(n.seq, cfg);
                const int d = FirstDiff(p, l.trace);
                if (d != -1) std::fprintf(stderr, "  %s (smooth %d, vsync %d): first difference at frame %d\n", n.name, smooth, vsync, d);
                CHECK(d == -1);
                ++sequences;
                framesCompared += (int)p.size();
                CHECK(l.rawUpdates == (int)l.trace.size());   // GameLoop updated every frame, the player's tick flag was always true here
            }
        }
        CHECK(sequences == 32);
        CHECK(framesCompared > 10000);
    }

    // ---- a quit ends both on the same frame, after the frame ran ----------------------------------------------------------------------------------------------
    {
        Cfg cfg;
        const Seq s = WithQuit(300, 200);
        const std::vector<Trace> p = RunPlayer(s, cfg);
        const LoopRun l = RunLoop(s, cfg);
        CHECK(p.size() == 201 && l.trace.size() == 201);
        CHECK(!p.back().running && !l.trace.back().running && p[199].running && l.trace[199].running);
        CHECK(p.back().updates == 201 && l.trace.back().updates == 201);   // the quit frame still updated the scene, in both
    }

    // ---- losing the window: the player skips the frame; GameLoop is fed 0 s and told to gate the update -----------------------------------------------------------------
    {
        Cfg off; off.smoothDt = false;
        const Seq s = WithAltTab(500);
        const std::vector<Trace> p = RunPlayer(s, off);
        const LoopRun l = RunLoop(s, off);
        CHECK(FirstDiff(p, l.trace) == -1);                    // without smoothing: identical, including the 120 unfocused frames
        CHECK(p[150].delta == 0.0f && p[150].unscaled == 0.0f && p[150].updates == p[100].updates);   // time stood still, no scene update
        CHECK(p[150].frame == p[100].frame + 50);              // but frames are still counted
        CHECK(l.rawUpdates == 500 && p.back().updates == 500 - 150);   // the difference: GameLoop calls OnUpdate every frame; the host gates it
        // not gating shows it: the scene would see a 0 s update on every unfocused frame
        const LoopRun ungated = RunLoop(s, off, false);
        CHECK(ungated.trace.back().updates == 500 && p.back().updates == 350);
        CHECK(ungated.trace[150].elapsed == p[150].elapsed);   // time itself is the same either way
    }
    {
        // with smoothing: the player feeds the unfocused (and clamped) frames to SmoothDelta, GameLoop feeds it only frames that tick
        Cfg on; on.smoothDt = true;
        const Seq s = WithAltTab(500);
        const std::vector<Trace> polluted = RunPlayer(s, on, true);
        const std::vector<Trace> clean = RunPlayer(s, on, false);   // the player's code with SmoothDelta skipped while unfocused
        const LoopRun l = RunLoop(s, on);
        CHECK(FirstDiff(clean, l.trace) == -1);                // GameLoop is exactly that, to the last bit
        // compared on what the game steps by (RawDeltaTime reads the measured time of an unfocused frame in the player: 0 in GameLoop)
        int firstStep = -1, bigDiffs = 0;
        for (std::size_t i = 0; i < polluted.size(); ++i) {
            if (polluted[i].delta != l.trace[i].delta || polluted[i].unscaled != l.trace[i].unscaled) {
                if (firstStep < 0) firstStep = (int)i;
                if (std::fabs(polluted[i].unscaled - l.trace[i].unscaled) > 1e-4f) ++bigDiffs;
            }
        }
        CHECK(firstStep == 220);                               // identical until the frame that finds the window again ...
        CHECK(bigDiffs >= 1 && bigDiffs <= 8);                 // ... then visibly different only while SmoothDelta's 4 frame history still holds the gap (twice: two gaps)
        bool tailClose = true;                                 // afterwards the values agree to float rounding (the history ring is aligned differently, so the sum of 4 adds in another order)
        for (std::size_t i = 223; i < 400; ++i) if (std::fabs(polluted[i].unscaled - l.trace[i].unscaled) > 1e-6f) tailClose = false;
        for (std::size_t i = 433; i < 500; ++i) if (std::fabs(polluted[i].unscaled - l.trace[i].unscaled) > 1e-6f) tailClose = false;
        CHECK(tailClose);
        // the resumed frame: the player averages the clamped gap frames in; GameLoop smooths against the frames that ticked
        CHECK(polluted[220].unscaled != l.trace[220].unscaled);
        CHECK(l.trace[220].unscaled > 0.0f && l.trace[220].unscaled <= 0.1f);
    }

    // ---- the focus-loss pause (the adoption sketch's alternative to the tick flag) --------------------------------------------------------------------------------------
    {
        Cfg off; off.smoothDt = false;
        const Seq s = WithAltTab(500);
        const std::vector<Trace> p = RunPlayer(s, off);
        Fresh();
        Game::SetPauseOnFocusLoss(true);
        GameLoop::Options o; o.maxDelta = 0.1f;
        GameLoop loop(o);
        int updates = 0;
        loop.OnUpdate = [&](float) { ++updates; };
        std::vector<Trace> q;
        float sceneDt = 0.0f;
        for (const FrameIn& f : s) {
            Game::NotifyFocus(f.focused);
            loop.BeginFrame((double)f.dt);
            sceneDt = Time::DeltaTime();
            q.push_back(Snap(updates, sceneDt, true));
        }
        // game time stands still in the same frames and ends in the same place ...
        bool deltasEqual = true;
        for (std::size_t i = 0; i < p.size(); ++i) if (p[i].delta != q[i].delta) deltasEqual = false;
        CHECK(deltasEqual);
        CHECK(p.back().elapsed == q.back().elapsed);
        // ... but unlike the tick flag it keeps real time running, so UI and clocks still tick (the intended difference)
        CHECK(p[150].unscaled == 0.0f && q[150].unscaled > 0.0f);
        CHECK(Clock::Get("ui").Ticks() == 500);
        CHECK(updates == 500);                                 // the frame still runs with a 0 s step
        Game::SetPauseOnFocusLoss(false);
        Game::NotifyFocus(true);
        Fresh();
    }

    // ---- run in background: the player ticks while unfocused, so does GameLoop fed the real time -------------------------------------------------------------------------------
    {
        Cfg bg; bg.runInBackground = true;
        for (int smooth = 0; smooth < 2; ++smooth) {
            bg.smoothDt = smooth != 0;
            const Seq s = WithAltTab(500);
            const std::vector<Trace> p = RunPlayer(s, bg);
            const LoopRun l = RunLoop(s, bg);
            CHECK(FirstDiff(p, l.trace) == -1);
            CHECK(p.back().updates == 500);
        }
    }

    // ---- vsync switched on and off while playing (the player re-reads it each frame and calls SetOptions when it changes) ---------------------------------------------------------
    {
        g_s = 4242u;
        const Seq seq = Steady(800, 1.0f / 60.0f, 0.004f);
        auto vsyncAt = [](std::size_t i) { return (i / 97) % 2 == 1; };
        // the old per-frame code, with the toggle
        std::vector<Trace> p;
        {
            Fresh();
            int updates = 0; float sceneDt = 0.0f;
            for (std::size_t i = 0; i < seq.size(); ++i) {
                float dt = seq[i].dt; if (dt > 0.1f) dt = 0.1f;
                const float stepDt = Time::SmoothDelta(dt, vsyncAt(i) ? 60.0f : 0.0f);
                Time::Step(stepDt);
                ++updates; sceneDt = Time::DeltaTime();
                p.push_back(Snap(updates, sceneDt, true));
            }
        }
        // the new one, as player/main.cpp does it
        std::vector<Trace> l;
        {
            Fresh();
            GameLoop::Options o; o.maxDelta = 0.1f; o.smoothDelta = true;
            GameLoop loop(o);
            int updates = 0; float sceneDt = 0.0f;
            bool loopTick = true;
            loop.OnUpdate = [&](float dt) { if (loopTick) { ++updates; sceneDt = dt; } };
            float loopRefreshHz = 0.0f;
            int setOptionsCalls = 0;
            for (std::size_t i = 0; i < seq.size(); ++i) {
                const float hz = vsyncAt(i) ? 60.0f : 0.0f;
                if (hz != loopRefreshHz) { loopRefreshHz = hz; GameLoop::Options q = loop.options(); q.refreshHz = hz; loop.SetOptions(q); ++setOptionsCalls; }
                const bool running = loop.BeginFrame((double)seq[i].dt);
                l.push_back(Snap(updates, sceneDt, running));
            }
            CHECK(setOptionsCalls == 8);                         // 800 frames, a toggle every 97: SetOptions is not a per-frame cost
        }
        Fresh();
        CHECK(FirstDiff(p, l) == -1);
    }

    // ---- the one numeric difference: a measured infinity ---------------------------------------------------------------------------------------------------------------------------------
    {
        // no timer produces one, but the code differs: the player's `dt > 0.1f` clamp turns infinity into 0.1 s, GameLoop calls it a 0 s frame
        Seq s = Steady(5);
        s[2].dt = std::numeric_limits<float>::infinity();
        Cfg off; off.smoothDt = false;
        const std::vector<Trace> p = RunPlayer(s, off);
        const LoopRun l = RunLoop(s, off);
        CHECK(p[2].unscaled == 0.1f && l.trace[2].unscaled == 0.0f);
        CHECK(FirstDiff(p, l.trace) == 2);
    }

    // ---- what GameLoop adds on top, without changing the values -------------------------------------------------------------------------------------------------------------------
    {
        // fixed steps, clocks and hitch counters come for free; the player's Time values are untouched by them
        Fresh();
        GameLoop::Options o; o.maxDelta = 0.1f; o.smoothDelta = true;
        GameLoop loop(o);
        int fixed = 0;
        loop.OnFixedUpdate = [&](float) { ++fixed; };
        Seq s = Spikes(300);
        for (const FrameIn& f : s) loop.BeginFrame((double)f.dt);
        CHECK(fixed > 200 && loop.HitchCount() >= 5 && loop.ClampedFrames() >= 5);
        CHECK(Clock::Get("ui").Ticks() == 300);
        CHECK_NEAR(Time::UnscaledTime(), Clock::Get("ui").Elapsed(), 1e-3);
        const double withExtras = Time::ElapsedTimeD();
        const std::vector<Trace> plain = RunPlayer(s, Cfg{});
        CHECK(plain.back().elapsed == withExtras);
        Fresh();
    }

    // ---- GameLoop::BeginSession where the player calls Game::Reset ------------------------------------------------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop loop;
        Time::SetTimeScale(0.3f);
        for (int i = 0; i < 10; ++i) loop.BeginFrame(0.016);
        loop.BeginSession();
        CHECK(Time::TimeScale() == 1.0f && loop.FrameCount() == 0 && !Game::Paused());
        // Game::Reset (what the player calls today) leaves a script's time scale behind; BeginSession does not
        Fresh();
        Time::SetTimeScale(0.0f);
        Game::Reset();
        CHECK(Time::BaseTimeScale() == 0.0f);
        GameLoop b;
        b.BeginSession();
        CHECK(Time::BaseTimeScale() == 1.0f);
        Fresh();
    }

    Fresh();
    TEST_MAIN_RESULT();
}
