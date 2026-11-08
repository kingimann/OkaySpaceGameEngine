#include "test_framework.hpp"
#include "okay/Debug/GpuTimer.hpp"
#include "okay/Debug/RenderStats.hpp"
#include "okay/Core/Profiler.hpp"

#include <cstdint>
#include <string>
#include <vector>
#ifndef _WIN32
#include <thread>
#endif

using namespace okay;

static bool Balanced(const std::string& s) {
    int curly = 0, square = 0; bool inStr = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (inStr) { if (c == '\\') ++i; else if (c == '"') inStr = false; continue; }
        if (c == '"') inStr = true;
        else if (c == '{') ++curly; else if (c == '}') { if (--curly < 0) return false; }
        else if (c == '[') ++square; else if (c == ']') { if (--square < 0) return false; }
    }
    return !inStr && curly == 0 && square == 0;
}

int main() {
    RUN_SUITE("debug_gpu_stats");

    // ---- CPU-fence GpuTimer with a fake clock and fence ---------------------------
    {
        double now = 100.0;
        int fences = 0;
        CpuFenceGpuTimer t([&] { ++fences; }, [&] { return now; });
        CHECK(std::string(t.Name()) == "cpu-fence");
        CHECK(!t.IsHardware());
        CHECK(!t.Enabled());                         // starts disabled: fencing stalls the pipeline

        // disabled: every call is a no-op and nothing is reported
        t.BeginFrame(); t.BeginScope("x"); t.EndScope(); t.EndFrame();
        CHECK(fences == 0 && t.FrameCount() == 0);
        GpuFrameTiming out;
        CHECK(!t.Poll(out));

        t.SetEnabled(true);
        t.BeginFrame();                              // fence #1
        CHECK(fences == 1);
        now += 1.0;
        t.BeginScope("Shadows");
        now += 2.0;
        t.BeginScope("Casters");                     // nested
        now += 3.0;
        t.EndScope();                                // Casters = 3
        now += 0.5;
        t.EndScope();                                // Shadows = 2 + 3 + 0.5 = 5.5
        t.BeginScope("Opaque");
        now += 4.0;
        t.EndScope();                                // Opaque = 4
        now += 1.0;
        t.EndFrame();
        CHECK(t.FrameCount() == 1 && t.OpenScopes() == 0);
        CHECK(fences == 1 + 3 * 2 + 1 + 0);          // begin + 6 scope edges (3 scopes) + end
        CHECK(t.Poll(out));
        CHECK(out.frame == 1 && !out.fromHardware);
        CHECK_NEAR(out.totalMs, 1.0 + 2.0 + 3.0 + 0.5 + 4.0 + 1.0, 1e-9);
        CHECK(out.scopes.size() == 3);
        if (out.scopes.size() == 3) {
            CHECK(out.scopes[0].name == "Shadows" && out.scopes[0].depth == 0);
            CHECK_NEAR(out.scopes[0].ms, 5.5, 1e-9);
            CHECK(out.scopes[1].name == "Casters" && out.scopes[1].depth == 1);
            CHECK_NEAR(out.scopes[1].ms, 3.0, 1e-9);
            CHECK(out.scopes[2].name == "Opaque" && out.scopes[2].depth == 0);
            CHECK_NEAR(out.scopes[2].ms, 4.0, 1e-9);
        }
        CHECK(!t.Poll(out));                         // consumed

        // frames queue up in order, at most 8 are kept
        for (int i = 0; i < 12; ++i) { t.BeginFrame(); now += 1.0 + i; t.EndFrame(); }
        int polled = 0; std::uint64_t first = 0, last = 0;
        while (t.Poll(out)) { if (!polled) first = out.frame; last = out.frame; ++polled; }
        CHECK(polled == 8 && first == 6 && last == 13);

        // a scope left open still reports when the frame ends; stray EndScope is harmless
        t.BeginFrame(); t.BeginScope("Open"); now += 2.0; t.EndFrame();
        CHECK(t.Poll(out) && out.scopes.size() == 1 && std::abs(out.scopes[0].ms - 2.0) < 1e-9);
        t.EndScope(); t.EndScope();
        CHECK(t.OpenScopes() == 0);

        // disabling mid frame discards the frame
        t.BeginFrame(); t.BeginScope("a"); t.SetEnabled(false); t.EndScope(); t.EndFrame();
        CHECK(!t.Poll(out));
    }
    {
        // no fence: only the submit time is measured; the default clock gives non-negative numbers
        CpuFenceGpuTimer t;
        CHECK(std::string(t.Name()) == "cpu-submit");
        t.SetEnabled(true);
        t.BeginFrame(); t.BeginScope("a"); t.EndScope(); t.EndFrame();
        GpuFrameTiming out;
        CHECK(t.Poll(out) && out.totalMs >= 0.0 && out.scopes.size() == 1 && out.scopes[0].ms >= 0.0);
        NullGpuTimer n;
        n.SetEnabled(true);
        n.BeginFrame(); n.BeginScope("a"); n.EndScope(); n.EndFrame();
        CHECK(!n.Enabled() && !n.Poll(out) && !n.IsHardware());
    }

    // ---- the active timer and the RAII scope -------------------------------------
    {
        CHECK(ActiveGpuTimer() == nullptr);
        { GpuScope s("nothing"); }                   // fine with no timer
        double now = 0.0;
        CpuFenceGpuTimer t(nullptr, [&] { return now; });
        t.SetEnabled(true);
        SetActiveGpuTimer(&t);
        CHECK(ActiveGpuTimer() == &t);
        t.BeginFrame();
        { GpuScope a("Pass"); now += 2.5; { GpuScope b("Inner"); now += 1.0; } }
        t.EndFrame();
        SetActiveGpuTimer(nullptr);
        GpuFrameTiming out;
        CHECK(t.Poll(out) && out.scopes.size() == 2);
        if (out.scopes.size() == 2) {
            CHECK_NEAR(out.scopes[0].ms, 3.5, 1e-9);
            CHECK_NEAR(out.scopes[1].ms, 1.0, 1e-9);
        }
        CHECK(ActiveGpuTimer() == nullptr);
    }

    // ---- RenderStatsCollector ---------------------------------------------------------
    {
        RenderStatsCollector rs(4);
        CHECK(rs.Last().frame == 0 && rs.Last().drawCalls == 0);
        rs.BeginFrame();
        rs.BindMaterial(1); rs.DrawCall(100, 300);
        rs.BindMaterial(1); rs.DrawCall(50, 150);               // same material: no switch
        rs.BindMaterial(2); rs.DrawCall(10, 30, 4);              // instanced: counts 4x
        rs.BindMaterial(1); rs.DrawCall(5);
        rs.BindShader(7); rs.BindShader(7); rs.BindShader(8);
        rs.BindTexture(RenderStatsCollector::IdOf(&rs)); rs.BindTexture(RenderStatsCollector::IdOf(&rs));
        rs.BindRenderTarget(0); rs.BindRenderTarget(3);
        rs.Dispatch(); rs.Dispatch();
        GpuFrameStats cur = rs.Current();
        CHECK(cur.drawCalls == 4 && cur.dispatches == 2);
        CHECK(cur.triangles == 100 + 50 + 40 + 5);
        CHECK(cur.vertices == 300 + 150 + 120);
        CHECK(cur.instances == 1 + 1 + 4 + 1);
        CHECK(cur.materialSwitches == 3);                         // 1 (first bind), 2, back to 1
        CHECK(cur.shaderSwitches == 2);
        CHECK(cur.textureSwitches == 1);
        CHECK(cur.renderTargetSwitches == 2);
        CHECK(rs.Last().drawCalls == 0);                          // not finished yet
        rs.EndFrame();
        GpuFrameStats last = rs.Last();
        CHECK(last.frame == 1 && last.drawCalls == 4 && last.triangles == 195);

        // the next frame starts from zero and from "nothing bound"
        rs.BeginFrame();
        CHECK(rs.Current().drawCalls == 0 && rs.Current().frame == 2);
        rs.BindMaterial(1);
        CHECK(rs.Current().materialSwitches == 1);               // first bind of a frame counts
        rs.EndFrame();
        CHECK(rs.Last().materialSwitches == 1);

        // GPU memory estimates are persistent and clamp at zero
        rs.TextureCreated(GpuFrameStats::EstimateTextureBytes(1024, 1024));
        CHECK(rs.Current().textureBytes == (1024u * 1024u * 4u) + (1024u * 1024u * 4u) / 3u);
        rs.TextureDestroyed(1ull << 40);
        CHECK(rs.Current().textureBytes == 0);
        rs.MeshCreated(4096); rs.MeshCreated(4096); rs.MeshDestroyed(1000);
        CHECK(rs.Current().meshBytes == 7192);
        rs.TextureCreated(100);
        rs.BeginFrame(); rs.EndFrame();
        CHECK(rs.Last().textureBytes == 100 && rs.Last().meshBytes == 7192);   // survive BeginFrame
        CHECK(GpuFrameStats::EstimateTextureBytes(0, 10) == 0);
        CHECK(GpuFrameStats::EstimateTextureBytes(16, 16, 4, false) == 1024);
        CHECK(GpuFrameStats::EstimateTextureBytes(16, 16, 4, false, 6) == 6144);

        // history is a ring of the last 4 finished frames
        for (int i = 0; i < 6; ++i) { rs.BeginFrame(); rs.DrawCall((std::uint64_t)(i + 1)); rs.EndFrame(); }
        std::vector<GpuFrameStats> h = rs.History();
        CHECK(h.size() == 4);
        if (h.size() == 4) {
            CHECK(h[0].triangles == 3 && h[3].triangles == 6);
            CHECK(h[0].frame + 3 == h[3].frame);
        }
        CHECK(rs.History(2).size() == 2 && rs.History(2)[1].triangles == 6);

        rs.SetGpuMs(2.5);
        CHECK_NEAR(rs.Current().gpuMs, 2.5, 1e-9);
        std::string js = rs.Last().ToJson();
        CHECK(Balanced(js) && js.find("\"drawCalls\":1") != std::string::npos);
        CHECK(rs.Last().ToText().find("1 draw calls") != std::string::npos);

        // the Profiler's live stats can be fed from the last frame
        rs.BeginFrame(); rs.DrawCall(10, 0, 3); rs.EndFrame();
        Profiler p;
        rs.ApplyToProfiler(p, 7);
        CHECK(p.drawObjects == 3 && p.drawTriangles == 30 && p.particles == 7);

        rs.Reset();
        CHECK(rs.Frame() == 0 && rs.Last().drawCalls == 0 && rs.History().empty() && rs.Current().textureBytes == 0);
        CHECK(&RenderStatsCollector::Global() == &RenderStatsCollector::Global());
    }

#ifndef _WIN32
    // ---- thread safety smoke test: counters from several threads add up exactly -----
    {
        RenderStatsCollector rs;
        rs.BeginFrame();
        const int kThreads = 4, kCalls = 5000;
        std::vector<std::thread> th;
        for (int t = 0; t < kThreads; ++t)
            th.emplace_back([&rs, kCalls, t] {
                for (int i = 0; i < kCalls; ++i) {
                    rs.DrawCall(2, 6);
                    rs.BindMaterial((std::uint64_t)(t * 2 + (i & 1)));
                    rs.TextureCreated(8);
                }
            });
        for (auto& x : th) x.join();
        rs.EndFrame();
        GpuFrameStats s = rs.Last();
        CHECK(s.drawCalls == (std::uint32_t)(kThreads * kCalls));
        CHECK(s.triangles == (std::uint64_t)kThreads * kCalls * 2);
        CHECK(s.textureBytes == (std::uint64_t)kThreads * kCalls * 8);
        CHECK(s.materialSwitches > 0);
    }
#endif

    TEST_MAIN_RESULT();
}
