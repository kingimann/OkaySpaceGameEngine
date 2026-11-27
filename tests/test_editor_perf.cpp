// Editor frame-time maths (editor/EditorPerf.hpp): the breakdown of a profiler frame, history numbers, the idle throttle and
// frame limiter decisions, the viewport scale and the performance-mode preset. Made-up frames only; no window, no GPU.
#include "test_framework.hpp"
#include "EditorPerf.hpp"
#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace okay;
namespace perf = okay::editor::perf;

static Profiler::Frame MakeFrame(double total) {
    Profiler::Frame f; f.total = total; return f;
}
static void Add(Profiler::Frame& f, const char* name, double self, int depth = 0) {
    Profiler::Bucket b; b.name = name; b.self = self; b.inclusive = self; b.calls = 1; b.depth = depth; f.buckets.push_back(b);
}

int main() {
    RUN_SUITE("editor_perf");

    // ---- classify scope names --------------------------------------------------------------------------------------------------------
    CHECK(perf::Classify("Render3D") == perf::Section::Viewport3D);
    CHECK(perf::Classify("Render3D.Upload") == perf::Section::ViewportUpload);
    CHECK(perf::Classify("UI.Viewport") == perf::Section::ViewportUI);
    CHECK(perf::Classify("UI.GameView") == perf::Section::ViewportUI);
    CHECK(perf::Classify("UI.Hierarchy") == perf::Section::Panels);
    CHECK(perf::Classify("UI.Inspector") == perf::Section::Panels);
    CHECK(perf::Classify("Editor.Update") == perf::Section::EditorUpdate);
    CHECK(perf::Classify("ImGui.NewFrame") == perf::Section::EditorUpdate);
    CHECK(perf::Classify("ImGui.Draw") == perf::Section::ImGuiDraw);
    CHECK(perf::Classify("ImGui.Render") == perf::Section::ImGuiDraw);
    CHECK(perf::Classify("Present") == perf::Section::Present);
    CHECK(perf::Classify("Present.Windows") == perf::Section::Present);   // the multi-window panels' presents (were "Not timed")
    CHECK(perf::Classify("Pacing.Sleep") == perf::Section::Sleep);
    CHECK(perf::Classify("Physics3D.Step") == perf::Section::Play);
    CHECK(perf::Classify("Scripts") == perf::Section::Play);

    // ---- the breakdown adds up to the frame, and what no scope covers is "not timed" ---------------------------------------------------
    {
        Profiler::Frame f = MakeFrame(10.0);
        Add(f, "Render3D", 4.0); Add(f, "UI.Hierarchy", 1.0); Add(f, "UI.Inspector", 0.5); Add(f, "ImGui.Draw", 1.5);
        Add(f, "Present", 2.0);
        const perf::Breakdown b = perf::Break(f);
        CHECK_NEAR(b.ms[(int)perf::Section::Viewport3D], 4.0, 1e-9);
        CHECK_NEAR(b.ms[(int)perf::Section::Panels], 1.5, 1e-9);
        CHECK_NEAR(b.ms[(int)perf::Section::ImGuiDraw], 1.5, 1e-9);
        CHECK_NEAR(b.ms[(int)perf::Section::Present], 2.0, 1e-9);
        CHECK_NEAR(b.ms[(int)perf::Section::Untimed], 1.0, 1e-9);
        double sum = 0; for (int i = 0; i < (int)perf::Section::Count; ++i) sum += b.ms[i];
        CHECK_NEAR(sum, 10.0, 1e-9);
        CHECK_NEAR(b.Share(perf::Section::Viewport3D), 0.4, 1e-9);
    }
    {   // scopes that add up to MORE than the frame (clock noise) never make a negative "not timed"; an empty frame is all zero
        Profiler::Frame f = MakeFrame(5.0); Add(f, "Render3D", 6.0);
        CHECK(perf::Break(f).ms[(int)perf::Section::Untimed] == 0.0);
        const perf::Breakdown z = perf::Break(MakeFrame(0.0));
        CHECK(z.total == 0.0 && z.Share(perf::Section::Present) == 0.0);
    }

    // ---- numbers over the last frames --------------------------------------------------------------------------------------------------
    {
        std::vector<double> v(99, 8.0); v.push_back(40.0);
        const perf::HistoryStats s = perf::Stats(v);
        CHECK(s.frames == 100);
        CHECK_NEAR(s.avgMs, 8.32, 1e-9);
        CHECK_NEAR(s.fps, 1000.0 / 8.32, 1e-6);
        CHECK_NEAR(s.worstMs, 40.0, 1e-9);
        CHECK_NEAR(s.bestMs, 8.0, 1e-9);
        CHECK_NEAR(s.lowFps, 25.0, 1e-6);   // the 1% low is the single 40 ms frame
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const perf::HistoryStats e = perf::Stats({nan, -1.0, 0.0});
        CHECK(e.frames == 0 && e.fps == 0.0);
        CHECK(perf::Stats({}).frames == 0);
    }

    // ---- idle throttle and frame limiter ---------------------------------------------------------------------------------------------
    {
        perf::PacingSettings p;   // everything off: never sleeps
        CHECK(perf::TargetFps(p, false, false, 99.0) == 0.0);
        p.fpsCap = 120;
        CHECK_NEAR(perf::TargetFps(p, false, false, 0.0), 120.0, 1e-9);
        CHECK_NEAR(perf::TargetFps(p, true, false, 99.0), 120.0, 1e-9);   // the cap holds while playing
        p.fpsCap = 0; p.idleThrottle = true; p.idleFps = 30;
        CHECK(perf::TargetFps(p, false, false, 0.2) == 0.0);              // just touched: full speed
        CHECK_NEAR(perf::TargetFps(p, false, false, 0.75), 30.0, 1e-9);   // idle long enough
        CHECK(perf::TargetFps(p, true, false, 5.0) == 0.0);               // never throttle a running game
        CHECK(perf::TargetFps(p, false, true, 5.0) == 0.0);               // a button is held (a drag): full speed
        CHECK(perf::TargetFps(p, false, false, std::numeric_limits<double>::quiet_NaN()) == 0.0);
        p.fpsCap = 20;                                                    // the lower of cap and idle rate wins
        CHECK_NEAR(perf::TargetFps(p, false, false, 5.0), 20.0, 1e-9);
        p.fpsCap = 144;
        CHECK_NEAR(perf::TargetFps(p, false, false, 5.0), 30.0, 1e-9);
        p.idleFps = 1;                                                    // out-of-range idle rate is held to 5
        CHECK_NEAR(perf::TargetFps(p, false, false, 5.0), 5.0, 1e-9);
        // A minimized editor (nothing playing) holds 10 FPS whatever the settings: its present no longer waits for the display.
        perf::PacingSettings off;
        CHECK_NEAR(perf::TargetFps(off, false, false, 0.0, true), perf::kMinimizedFps, 1e-9);
        CHECK(perf::TargetFps(off, true, false, 0.0, true) == 0.0);       // a running game is left alone
        off.fpsCap = 5;
        CHECK_NEAR(perf::TargetFps(off, false, false, 0.0, true), 5.0, 1e-9);   // a lower cap still wins
        // 100 loop turns of 1 ms each while minimized: the pacing sleeps the rest of every 100 ms frame (was: no sleep at all).
        double slept = 0.0;
        for (int i = 0; i < 100; ++i) slept += perf::SleepSeconds(perf::TargetFps(perf::PacingSettings{}, false, false, 0.0, true), 0.001);
        CHECK(slept > 9.8);
        CHECK_NEAR(perf::SleepSeconds(100.0, 0.004), 0.006, 1e-12);
        CHECK(perf::SleepSeconds(100.0, 0.02) == 0.0);                    // a late frame is not paid back
        CHECK(perf::SleepSeconds(0.0, 0.001) == 0.0);
        CHECK(perf::SleepSeconds(std::numeric_limits<double>::infinity(), 0.001) == 0.0);
    }

    // ---- idle throttle: on by default, and every wake reason keeps full speed ---------------------------------------------------------
    {
        CHECK(perf::kIdleThrottleDefault);   // the editor starts with it on (was off: an untouched editor redrew as fast as it could)
        perf::PacingSettings p; p.idleThrottle = perf::kIdleThrottleDefault; p.idleFps = 30;
        const perf::IdleWake none;
        CHECK_NEAR(perf::TargetFps(p, none, 5.0), 30.0, 1e-9);     // nothing happening for 5 s: the idle rate
        CHECK(perf::TargetFps(p, none, 0.1) == 0.0);               // input 0.1 s ago: full speed at once
        perf::IdleWake w;
        w = none; w.playing = true;     CHECK(perf::TargetFps(p, w, 5.0) == 0.0);   // Play mode
        w = none; w.pointerHeld = true; CHECK(perf::TargetFps(p, w, 5.0) == 0.0);   // a drag, a right-drag fly camera
        w = none; w.keyHeld = true;     CHECK(perf::TargetFps(p, w, 5.0) == 0.0);   // a held key with no repeat events
        w = none; w.preview = true;     CHECK(perf::TargetFps(p, w, 5.0) == 0.0);   // an animation / timeline / model preview
        w = none; w.debugger = true;    CHECK(perf::TargetFps(p, w, 5.0) == 0.0);   // the visual-script debugger
        w = none; w.splash = true;      CHECK(perf::TargetFps(p, w, 5.0) == 0.0);   // the loading screen and its fade
        CHECK(!perf::Awake(none));
        // Activity without input (a Console line from a build, a download, an import, a file reload or a job finishing) is "just now".
        CHECK_NEAR(perf::QuietSeconds(8.0, 0.2), 0.2, 1e-12);
        CHECK(perf::TargetFps(p, none, perf::QuietSeconds(8.0, 0.2)) == 0.0);
        CHECK_NEAR(perf::TargetFps(p, none, perf::QuietSeconds(8.0, 3.0)), 30.0, 1e-9);
        CHECK(perf::QuietSeconds(std::numeric_limits<double>::quiet_NaN(), 3.0) == 0.0);
        CHECK(perf::QuietSeconds(-1.0, 3.0) == 0.0);
        // A minimized window still holds 10 FPS; Play still overrides it.
        CHECK_NEAR(perf::TargetFps(p, none, 0.0, true), perf::kMinimizedFps, 1e-9);
        w = none; w.playing = true; CHECK(perf::TargetFps(p, w, 0.0, true) == 0.0);
    }

    // ---- viewport scale and size -----------------------------------------------------------------------------------------------------
    {
        CHECK_NEAR(perf::EffectiveScale(1.0f, false), 1.0, 1e-6);
        CHECK_NEAR(perf::EffectiveScale(1.0f, true), 0.75, 1e-6);
        CHECK_NEAR(perf::EffectiveScale(0.5f, true), 0.5, 1e-6);          // never raised
        CHECK_NEAR(perf::EffectiveScale(0.1f, false), 0.25, 1e-6);
        CHECK_NEAR(perf::EffectiveScale(7.0f, false), 1.0, 1e-6);
        CHECK_NEAR(perf::EffectiveScale(std::numeric_limits<float>::quiet_NaN(), false), 1.0, 1e-6);
        int w = 0, h = 0;
        perf::RenderSize(1000, 500, 0.5f, 1920, w, h); CHECK(w == 500 && h == 250);
        perf::RenderSize(4000, 2000, 1.0f, 1920, w, h); CHECK(w == 1920 && h == 960);   // the long edge is held
        perf::RenderSize(0, 0, 0.25f, 1920, w, h); CHECK(w == 1 && h == 1);             // never 0
    }

    // ---- performance mode ---------------------------------------------------------------------------------------------------------------
    {
        perf::ViewKnobs k; k.shadows = true; k.shadowResolution = 4096; k.shadowCascades = 4; k.ssao = true; k.bloom = true; k.fxaa = true; k.supersample = 2;
        const perf::ViewKnobs c = perf::Cheaper(k);
        CHECK(c.shadows && c.shadowResolution == 1024 && c.shadowCascades == 1 && !c.ssao && !c.bloom && !c.fxaa && c.supersample == 1);
        const perf::ViewKnobs keep = perf::Cheaper(k, /*changeShadowSize=*/false);   // Game view open too: do not resize the shadow map
        CHECK(keep.shadowResolution == 4096 && keep.shadowCascades == 4 && !keep.ssao && !keep.bloom && !keep.fxaa && keep.supersample == 1);
        perf::ViewKnobs low; low.shadowResolution = 512; low.shadowCascades = 1; low.shadows = false;   // already cheap: untouched, never raised
        const perf::ViewKnobs c2 = perf::Cheaper(low);
        CHECK(c2.shadowResolution == 512 && c2.shadowCascades == 1 && !c2.shadows);
    }

    // ---- the copied report names what the owner needs to send back --------------------------------------------------------------------
    {
        Profiler::Frame f = MakeFrame(10.0); Add(f, "Render3D", 6.0); Add(f, "Present", 3.0);
        const std::string r = perf::Report(perf::Stats({10.0, 10.0}), perf::Break(f), "Direct3D 11 (GPU, direct to the view)", false, 1280, 720, 0.75f, true, 0, true);
        CHECK(r.find("Direct3D 11") != std::string::npos && r.find("1280x720") != std::string::npos);
        CHECK(r.find("100 FPS") != std::string::npos && r.find("performance mode on") != std::string::npos);
        CHECK(r.find("3D view render") != std::string::npos && r.find("60.0%") != std::string::npos);
    }

    // ---- the slow-frame log (okay_editor_perf.log): a slow frame names where its time went; a quick one writes nothing --------------
    {
        CHECK_NEAR(perf::SlowFrameLog::ThresholdFrom(nullptr), 250.0, 1e-9);
        CHECK_NEAR(perf::SlowFrameLog::ThresholdFrom("400"), 400.0, 1e-9);
        CHECK_NEAR(perf::SlowFrameLog::ThresholdFrom("0"), 0.0, 1e-9);       // off
        CHECK_NEAR(perf::SlowFrameLog::ThresholdFrom("5"), 20.0, 1e-9);      // never so low that every frame is logged
        CHECK_NEAR(perf::SlowFrameLog::ThresholdFrom("abc"), 250.0, 1e-9);
        perf::SlowFrameLog log;
        Profiler::Frame quick = MakeFrame(16.0); Add(quick, "UI.Inspector", 10.0);
        CHECK(log.Line(quick, 16.0, 1, "").empty() && log.written == 0);
        Profiler::Frame slow = MakeFrame(900.0); Add(slow, "UI.Inspector", 700.0); Add(slow, "Present", 120.0); Add(slow, "UI.Hierarchy", 30.0);
        const std::string l = log.Line(slow, 900.0, 42, "okayui inspector ownrender, edit");
        CHECK(log.written == 1);
        CHECK(l.find("slow frame 42: 900 ms") != std::string::npos);
        CHECK(l.find("mostly Panels") != std::string::npos);                                 // the section that took the time
        CHECK(l.find("UI.Inspector 700 ms, Present 120 ms, UI.Hierarchy 30 ms") != std::string::npos);   // biggest scopes first
        CHECK(l.find("not timed 50 ms") != std::string::npos && l.find("| okayui inspector ownrender, edit") != std::string::npos);
        const std::string none = log.Line(MakeFrame(300.0), 300.0, 43, "");                // the profiler saw nothing
        CHECK(none.find("mostly Not timed") != std::string::npos && none.find("no timed scopes") != std::string::npos);
        perf::SlowFrameLog off; off.thresholdMs = 0.0;
        CHECK(off.Line(slow, 5000.0, 1, "").empty());
        perf::SlowFrameLog full; full.maxLines = 2;
        CHECK(!full.Line(slow, 900.0, 1, "").empty());
        CHECK(full.Line(slow, 900.0, 2, "").find("the log is full") != std::string::npos);
        CHECK(full.Line(slow, 900.0, 3, "").empty() && full.written == 2);
    }
    RUN_SUITE("the once-a-minute FPS summary (a steady low frame rate shows up, not only frames over 250 ms)");
    {
        CHECK_NEAR(perf::FpsSummary::PeriodFrom(nullptr), 60.0, 1e-9);
        CHECK_NEAR(perf::FpsSummary::PeriodFrom("0"), 0.0, 1e-9);
        CHECK_NEAR(perf::FpsSummary::PeriodFrom("1"), 5.0, 1e-9);
        CHECK_NEAR(perf::FpsSummary::PeriodFrom("x"), 60.0, 1e-9);
        perf::FpsSummary sum;
        Profiler::Frame f = MakeFrame(100.0);
        Add(f, "UI.Diagnostics", 60.0); Add(f, "Render3D", 20.0); Add(f, "Present", 5.0); Add(f, "UI.Hierarchy", 1.0);
        std::string line;
        int lines = 0;
        for (int i = 0; i < 1200; ++i) {   // 100 ms frames (10 FPS) for 120 s: two lines
            const std::string l = sum.Add(f, 100.0);
            if (!l.empty()) { ++lines; line = l; }
        }
        CHECK(lines == 2);
        CHECK(line.find("600 frames in 60 s") != std::string::npos);
        CHECK(line.find("average 10.0 FPS") != std::string::npos);
        CHECK(line.find("top scopes per frame: UI.Diagnostics 60.0 ms, Render3D 20.0 ms, Present 5.0 ms") != std::string::npos);
        CHECK(line.find("UI.Hierarchy") == std::string::npos);
        perf::FpsSummary off; off.periodSeconds = 0.0;
        bool any = false;
        for (int i = 0; i < 2000; ++i) any = any || !off.Add(f, 100.0).empty();
        CHECK(!any);
    }
    TEST_MAIN_RESULT();
}
