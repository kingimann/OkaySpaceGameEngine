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
        CHECK_NEAR(perf::SleepSeconds(100.0, 0.004), 0.006, 1e-12);
        CHECK(perf::SleepSeconds(100.0, 0.02) == 0.0);                    // a late frame is not paid back
        CHECK(perf::SleepSeconds(0.0, 0.001) == 0.0);
        CHECK(perf::SleepSeconds(std::numeric_limits<double>::infinity(), 0.001) == 0.0);
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
    return 0;
}
