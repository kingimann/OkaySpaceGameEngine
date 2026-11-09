#include "test_framework.hpp"
#include "okay/Debug/Debug.hpp"

#include <string>
#include <vector>

using namespace okay;

static bool HasLine(const std::vector<std::string>& lines, const std::string& part) {
    for (const std::string& l : lines) if (l.find(part) != std::string::npos) return true;
    return false;
}
static FrameRecord Frame(double total, double cpu = 0.0, double gpu = 0.0) {
    FrameRecord f; f.totalMs = total; f.cpuMs = cpu; f.gpuMs = gpu; return f;
}

int main() {
    RUN_SUITE("debug_overlay");

    // ---- no sources: nothing to show ---------------------------------------------------------
    {
        DebugStatsOverlay o;
        CHECK(o.BuildLines().empty());
        DebugStatsOverlay::Graph g = o.BuildFrameGraph();
        CHECK(g.points.empty() && g.unit == "ms");
        CHECK(o.BuildMemoryGraph().points.empty());
        FrameHistory empty;
        o.SetSources(&empty, nullptr, nullptr);
        CHECK(o.BuildLines().empty());
    }

    // ---- FPS, frame time, CPU and GPU ------------------------------------------------------------
    {
        FrameHistory h(500);
        for (int i = 0; i < 100; ++i) h.Push(Frame(100.0));                 // old, slow
        for (int i = 0; i < 30; ++i) h.Push(Frame(16.0, 9.0, 0.0));
        DebugStatsOverlay o;
        o.SetSources(&h, nullptr, nullptr);
        std::vector<std::string> lines = o.BuildLines();
        CHECK(lines.size() == 1 && lines[0] == "FPS: 62.5 (16.0 ms)");      // the last 30 frames only
        o.GetOptions().smoothFrames = 130;
        CHECK(o.BuildLines()[0] == "FPS: 12.4 (80.6 ms)");                   // 100 frames at 100 ms and 30 at 16 ms
        o.GetOptions().smoothFrames = 30;

        o.GetOptions().showFps = false;
        CHECK(o.BuildLines()[0] == "Frame: 16.0 ms");
        o.GetOptions().showFps = true; o.GetOptions().showFrameTime = false;
        CHECK(o.BuildLines()[0] == "FPS: 62.5");
        o.GetOptions().showFps = false;
        CHECK(o.BuildLines().empty());
        o.GetOptions().showFps = true; o.GetOptions().showFrameTime = true;

        CHECK(!HasLine(o.BuildLines(), "GPU"));                              // no GPU time known
        h.Push(Frame(16.0, 9.5, 4.25));
        CHECK(HasLine(o.BuildLines(), "CPU: 9.5 ms  GPU: 4.2 ms") || HasLine(o.BuildLines(), "CPU: 9.5 ms  GPU: 4.3 ms"));
        o.GetOptions().showCpuGpu = false;
        CHECK(!HasLine(o.BuildLines(), "GPU"));
        o.GetOptions().showCpuGpu = true;

        std::vector<std::string> detail = o.BuildLines(true);
        CHECK(HasLine(detail, "Frame ms: avg") && HasLine(detail, "p95") && HasLine(detail, "max"));
        CHECK(!HasLine(o.BuildLines(false), "Frame ms:"));

    }

    // ---- spikes ------------------------------------------------------------------------------------------
    {
        FrameHistory h;
        for (int i = 0; i < 60; ++i) h.Push(Frame(10.0));
        h.Push(Frame(80.0));
        DebugStatsOverlay o;
        o.SetSources(&h, nullptr, nullptr);
        CHECK(!HasLine(o.BuildLines(), "Spikes"));                            // off by default
        o.GetOptions().showSpikes = true;
        o.GetOptions().graphPoints = 40;
        CHECK(HasLine(o.BuildLines(), "Spikes: 1 in last 40 frames"));
    }

    // ---- GPU time taken from the render stats when the frame has none ------------------------------------
    {
        FrameHistory h;
        h.Push(Frame(16.0, 8.0, 0.0));
        RenderStatsCollector rs;
        rs.BeginFrame(); rs.SetGpuMs(5.0); rs.EndFrame();
        DebugStatsOverlay o;
        o.SetSources(&h, nullptr, &rs);
        CHECK(HasLine(o.BuildLines(), "GPU: 5.0 ms"));
    }

    // ---- memory ---------------------------------------------------------------------------------------------
    {
        MemoryTracker m;
        DebugStatsOverlay o;
        o.SetSources(nullptr, &m, nullptr);
        CHECK(o.BuildLines().empty());                                        // nothing tracked yet
        m.Add(MemoryTracker::Textures, 8 * 1024 * 1024);
        m.Add(MemoryTracker::Meshes, 2 * 1024 * 1024);
        m.Add(MemoryTracker::Audio, 512 * 1024);
        m.Add(MemoryTracker::Scripts, 1024);
        m.Remove(MemoryTracker::Scripts, 1024);
        std::vector<std::string> lines = o.BuildLines();
        CHECK(lines.size() == 2);
        CHECK(HasLine(lines, "Memory: 10.50 MB (peak 10.50 MB)"));
        CHECK(lines.size() == 2 && lines[1] == "  Textures 8.00 MB, Meshes 2.00 MB, Audio 512.00 KB");   // biggest first, top three
        o.GetOptions().topCategories = 1;
        CHECK(o.BuildLines()[1] == "  Textures 8.00 MB");
        o.GetOptions().topCategories = 0;
        CHECK(o.BuildLines().size() == 1);
        std::vector<std::string> detail = o.BuildLines(true);
        CHECK(HasLine(detail, "Textures: 8.00 MB x1") && HasLine(detail, "Audio: 512.00 KB"));
        o.GetOptions().showMemory = false;
        CHECK(o.BuildLines().empty());
        o.GetOptions().showMemory = true; o.GetOptions().topCategories = 3;

        // a leak shows up as a warning line
        MemoryTracker leaky;
        DebugStatsOverlay lo;
        lo.SetSources(nullptr, &leaky, nullptr);
        for (int i = 0; i < 130; ++i) { leaky.Add(MemoryTracker::Audio, 20000); leaky.EndFrame(); }
        CHECK(HasLine(lo.BuildLines(), "Possible leak: Audio"));
        lo.GetOptions().showLeakWarning = false;
        CHECK(!HasLine(lo.BuildLines(), "Possible leak"));

        // memory graph in MB
        DebugStatsOverlay::Graph g = lo.BuildMemoryGraph();
        CHECK(g.unit == "MB" && !g.points.empty() && g.points.size() <= lo.GetOptions().graphPoints);
        CHECK(g.points.back().value > g.points.front().value);
        CHECK_NEAR(g.points.back().value, 130 * 20000.0 / (1024.0 * 1024.0), 1e-9);
    }

    // ---- render stats ----------------------------------------------------------------------------------------
    {
        RenderStatsCollector rs;
        DebugStatsOverlay o;
        o.SetSources(nullptr, nullptr, &rs);
        CHECK(o.BuildLines().empty());                                       // no finished frame yet
        rs.BeginFrame();
        rs.BindMaterial(1); rs.DrawCall(30000); rs.BindMaterial(2); rs.DrawCall(15600);
        rs.TextureCreated(64ull * 1024 * 1024);
        rs.EndFrame();
        std::vector<std::string> lines = o.BuildLines();
        CHECK(lines.size() == 2);
        CHECK(HasLine(lines, "Draw calls: 2  Tris: 45.6k  Mat switches: 2"));
        CHECK(HasLine(lines, "Textures: ~64.00 MB"));
        std::vector<std::string> detail = o.BuildLines(true);
        CHECK(HasLine(detail, "Verts:") && HasLine(detail, "Switches: shader 0, texture 0, target 0"));
        o.GetOptions().showRender = false;
        CHECK(o.BuildLines().empty());
    }

    // ---- custom lines -----------------------------------------------------------------------------------------------
    {
        DebugStatsOverlay o;
        int n = 3;
        int a = o.AddLine([&n] { return "Bullets: " + std::to_string(n); });
        int b = o.AddLine([] { return std::string(); });                      // empty: skipped
        CHECK(a != b && a > 0);
        CHECK(o.AddLine(nullptr) == 0);
        CHECK(o.BuildLines().size() == 1 && o.BuildLines()[0] == "Bullets: 3");
        n = 9;
        CHECK(o.BuildLines()[0] == "Bullets: 9");                              // evaluated on every build
        o.RemoveLine(a);
        CHECK(o.BuildLines().empty());
        o.RemoveLine(12345);                                                    // harmless
        o.AddLine([] { return std::string("x"); });
        o.ClearLines();
        CHECK(o.BuildLines().empty());
    }

    // ---- graph points ------------------------------------------------------------------------------------------------------
    {
        FrameHistory h;
        for (int i = 0; i < 10; ++i) h.Push(Frame(10.0));
        h.Push(Frame(20.0));
        DebugStatsOverlay o;
        o.SetSources(&h, nullptr, nullptr);
        o.GetOptions().graphPoints = 5;
        DebugStatsOverlay::Graph g = o.BuildFrameGraph();
        CHECK(g.points.size() == 5);                                            // the newest five
        CHECK(g.unit == "ms");
        CHECK_NEAR(g.points.front().x, 0.0, 1e-9); CHECK_NEAR(g.points.back().x, 1.0, 1e-9);
        CHECK_NEAR(g.points[2].x, 0.5, 1e-9);
        CHECK_NEAR(g.points.back().value, 20.0, 1e-9);
        // auto scale: worst 20 ms, target 16.667 ms -> rounded up to a multiple of half the target = 25 ms
        CHECK_NEAR(g.maxValue, 25.0, 1e-6);
        CHECK_NEAR(g.points.back().y, 20.0 / 25.0, 1e-5);
        CHECK_NEAR(g.points.front().y, 10.0 / 25.0, 1e-5);
        CHECK(g.points.back().overTarget && !g.points.front().overTarget);
        CHECK_NEAR(g.target, 1000.0 / 60.0, 1e-9);
        CHECK_NEAR(g.targetY, (1000.0 / 60.0) / 25.0, 1e-5);
        CHECK_NEAR(g.minValue, 10.0, 1e-9); CHECK_NEAR(g.avgValue, 12.0, 1e-9);

        o.GetOptions().graphMaxMs = 40.0;                                       // fixed scale
        g = o.BuildFrameGraph();
        CHECK_NEAR(g.maxValue, 40.0, 1e-9); CHECK_NEAR(g.points.back().y, 0.5, 1e-6);
        o.GetOptions().graphMaxMs = 15.0;                                       // values above the top are clipped to 1
        g = o.BuildFrameGraph();
        CHECK_NEAR(g.points.back().y, 1.0, 1e-9);

        // direct use with any series, no target
        DebugStatsOverlay::Graph s = DebugStatsOverlay::BuildGraph({1.0, 2.0, 4.0}, 0.0, 0.0, "x");
        CHECK(s.points.size() == 3 && s.unit == "x" && s.target == 0.0 && s.targetY == 0.0f);
        CHECK_NEAR(s.maxValue, 4.0, 1e-9);
        CHECK(!s.points[0].overTarget && !s.points[2].overTarget);
        DebugStatsOverlay::Graph one = DebugStatsOverlay::BuildGraph({3.0}, 0.0, 0.0, "x");
        CHECK(one.points.size() == 1 && one.points[0].x == 1.0f);
        DebugStatsOverlay::Graph zeros = DebugStatsOverlay::BuildGraph({0.0, 0.0}, 0.0, 0.0, "x");
        CHECK(zeros.maxValue == 1.0 && zeros.points[0].y == 0.0f);               // flat zero data does not divide by zero
        DebugStatsOverlay::Graph none = DebugStatsOverlay::BuildGraph({}, 16.0, 0.0, "ms");
        CHECK(none.points.empty() && none.maxValue > 0.0);
    }

    // ---- counts ------------------------------------------------------------------------------------------------------------------
    {
        CHECK(DebugStatsOverlay::FormatCount(0) == "0");
        CHECK(DebugStatsOverlay::FormatCount(9999) == "9999");
        CHECK(DebugStatsOverlay::FormatCount(45600) == "45.6k");
        CHECK(DebugStatsOverlay::FormatCount(1200000) == "1.2M");
    }

    TEST_MAIN_RESULT();
}
