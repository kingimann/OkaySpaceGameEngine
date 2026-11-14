#include "test_framework.hpp"
#include "okay/Debug/FrameHistory.hpp"

#include <chrono>
#include <string>
#include <vector>

using namespace okay;

// Minimal JSON sanity: balanced braces/brackets outside strings, strings terminated.
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
static int CountOf(const std::string& s, const std::string& pat) {
    int n = 0; for (size_t p = s.find(pat); p != std::string::npos; p = s.find(pat, p + pat.size())) ++n; return n;
}

// Update (Physics inside) and Render at the top level.
static FrameRecord MakeFrame(double total, double update, double physics, double render) {
    FrameRecord f;
    f.totalMs = total; f.cpuMs = update + physics + render;
    f.AddScope("Update", 0, update + physics, update, 1, 0.0);
    f.AddScope("Physics", 1, physics, physics, 2, 0.1);
    f.AddScope("Render", 0, render, render, 1, update + physics);
    return f;
}

static void BusyUs(int us) {
    auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count() < us) {}
}

int main() {
    RUN_SUITE("debug_frame_history");

    // ---- ring buffer wrap ----------------------------------------------------
    {
        FrameHistory h(4);
        CHECK(h.Empty() && h.Size() == 0 && h.Latest() == nullptr);
        for (int i = 0; i < 10; ++i) { FrameRecord f; f.totalMs = i; h.Push(f); }
        CHECK(h.Size() == 4);
        CHECK(h.Capacity() == 4);
        CHECK(h.TotalPushed() == 10);
        CHECK(h.At(0).sequence == 6 && h.At(3).sequence == 9);
        CHECK_NEAR(h.At(0).totalMs, 6, 1e-9);
        CHECK_NEAR(h.Latest()->totalMs, 9, 1e-9);
        CHECK(h.At(4).scopes.empty() && h.At(4).sequence == 0);          // out of range: an empty record
        CHECK(h.IndexOfSequence(6) == 0 && h.IndexOfSequence(9) == 3);
        CHECK(h.IndexOfSequence(5) == -1 && h.IndexOfSequence(10) == -1);

        // shrink keeps the newest frames in order, then pushing continues
        h.SetCapacity(2);
        CHECK(h.Size() == 2 && h.At(0).sequence == 8 && h.At(1).sequence == 9);
        { FrameRecord f; f.totalMs = 10; h.Push(f); }
        CHECK(h.Size() == 2 && h.At(0).sequence == 9 && h.At(1).sequence == 10);
        // grow again: nothing is lost, new frames append
        h.SetCapacity(5);
        for (int i = 0; i < 3; ++i) { FrameRecord f; h.Push(f); }
        CHECK(h.Size() == 5 && h.At(0).sequence == 9 && h.At(4).sequence == 13);

        h.Clear();
        CHECK(h.Empty());
        { FrameRecord f; CHECK(h.Push(f) == 14); }                       // sequence numbers keep counting
        FrameHistory zero(0);
        { FrameRecord f; zero.Push(f); }
        CHECK(zero.Size() == 0 && zero.TotalPushed() == 1);
    }

    // ---- percentiles and series stats -----------------------------------------
    {
        Distribution e = Distribution::Of({});
        CHECK(e.count == 0 && e.avg == 0.0 && e.p95 == 0.0);
        Distribution one = Distribution::Of({7.0});
        CHECK(one.count == 1 && one.p50 == 7.0 && one.p99 == 7.0 && one.min == 7.0 && one.max == 7.0);

        FrameHistory h(200);
        for (int i = 100; i >= 1; --i) { FrameRecord f; f.totalMs = i; f.cpuMs = i * 0.5; h.Push(f); }   // pushed unsorted
        Distribution d = h.SeriesStats(FrameHistory::Series::Total);
        CHECK(d.count == 100);
        CHECK_NEAR(d.min, 1, 1e-9); CHECK_NEAR(d.max, 100, 1e-9);
        CHECK_NEAR(d.avg, 50.5, 1e-9);
        CHECK_NEAR(d.p50, 50.5, 1e-9);                                   // linear interpolation between ranks
        CHECK_NEAR(d.p90, 90.1, 1e-9);
        CHECK_NEAR(d.p95, 95.05, 1e-9);
        CHECK_NEAR(d.p99, 99.01, 1e-9);
        CHECK_NEAR(h.SeriesStats(FrameHistory::Series::Cpu).max, 50.0, 1e-9);
        CHECK_NEAR(h.Fps(), 1000.0 / 50.5, 1e-9);
        // a sub range: timeline indices 0..9 are the frames with totals 100..91
        Distribution r = h.SeriesStats(FrameHistory::Series::Total, 0, 9);
        CHECK(r.count == 10 && r.min == 91 && r.max == 100);
        // ranges clamp; an inverted or empty range gives nothing
        CHECK(h.SeriesStats(FrameHistory::Series::Total, 95, 5000).count == 5);
        CHECK(h.SeriesStats(FrameHistory::Series::Total, 50, 40).count == 0);
        CHECK(FrameHistory(10).SeriesStats().count == 0);
        std::vector<double> vals = h.SeriesValues(FrameHistory::Series::Total, 0, 2);
        CHECK(vals.size() == 3 && vals[0] == 100 && vals[2] == 98);
    }

    // ---- per scope statistics --------------------------------------------------
    {
        FrameHistory h(50);
        for (int i = 0; i < 10; ++i) h.Push(MakeFrame(16, 2.0 + i, 1.0, 3.0));          // Update self 2..11
        for (int i = 0; i < 10; ++i) {                                                  // Audio only runs in half of them
            FrameRecord f = MakeFrame(16, 2.0, 1.0, 3.0);
            if (i % 2 == 0) f.AddScope("Audio", 0, 4.0, 4.0, 1, 6.0);
            h.Push(f);
        }
        ScopeStats u = h.ScopeStat("Update", FrameHistory::Metric::Self, 0, 9);
        CHECK(u.framesPresent == 10 && u.framesTotal == 10);
        CHECK_NEAR(u.dist.min, 2.0, 1e-9); CHECK_NEAR(u.dist.max, 11.0, 1e-9); CHECK_NEAR(u.dist.avg, 6.5, 1e-9);
        ScopeStats ui = h.ScopeStat("Update", FrameHistory::Metric::Inclusive, 0, 9);
        CHECK_NEAR(ui.dist.min, 3.0, 1e-9); CHECK_NEAR(ui.dist.max, 12.0, 1e-9);        // includes Physics
        ScopeStats a = h.ScopeStat("Audio", FrameHistory::Metric::Self, 10, 19);
        CHECK(a.framesPresent == 5 && a.framesTotal == 10);
        CHECK_NEAR(a.dist.avg, 4.0, 1e-9);                                              // only frames where it ran
        ScopeStats az = h.ScopeStat("Audio", FrameHistory::Metric::Self, 10, 19, true);
        CHECK(az.dist.count == 10);
        CHECK_NEAR(az.dist.avg, 2.0, 1e-9); CHECK_NEAR(az.dist.min, 0.0, 1e-9);        // absent frames count as zero
        CHECK(h.ScopeStat("Nope").framesPresent == 0);

        std::vector<ScopeStats> all = h.AllScopeStats(FrameHistory::Metric::Self, 0, 19);
        CHECK(all.size() == 4);
        CHECK(all[0].name == "Update" && all[1].name == "Audio" && all[2].name == "Render" && all[3].name == "Physics");   // largest average first
        for (size_t i = 1; i < all.size(); ++i) CHECK(all[i - 1].dist.avg >= all[i].dist.avg);
    }

    // ---- recursion is not double counted ---------------------------------------
    {
        FrameRecord f;
        f.AddScope("Walk", 0, 10.0, 4.0);
        f.AddScope("Walk", 1, 6.0, 4.0);
        f.AddScope("Walk", 2, 2.0, 2.0);
        CHECK_NEAR(f.SelfMs("Walk"), 10.0, 1e-9);
        CHECK_NEAR(f.InclusiveMs("Walk"), 10.0, 1e-9);                                  // the outermost call only
        CHECK(f.scopes[2].parent == 1 && f.scopes[1].parent == 0 && f.scopes[0].parent == -1);
        FrameHistory h(4); h.Push(f);
        CHECK_NEAR(h.ScopeStat("Walk", FrameHistory::Metric::Inclusive).dist.avg, 10.0, 1e-9);
    }

    // ---- spike detection --------------------------------------------------------
    {
        FrameHistory h(100);
        for (int i = 0; i < 30; ++i) {
            if (i == 12) h.Push(MakeFrame(50.0, 2.0, 41.0, 3.0));                        // Physics blew up
            else if (i == 25) h.Push(MakeFrame(36.0, 2.0, 1.0, 30.0));                   // Render blew up
            else h.Push(MakeFrame(10.0, 2.0, 1.0, 3.0));
        }
        std::vector<FrameSpike> sp = h.FindSpikes(2.0);
        CHECK(sp.size() == 2);
        if (sp.size() == 2) {
            CHECK(sp[0].frame == 12 && sp[1].frame == 25);
            CHECK(sp[0].sequence == 12);
            CHECK_NEAR(sp[0].medianMs, 10.0, 1e-9);
            CHECK_NEAR(sp[0].ratio, 5.0, 1e-9);
            CHECK(sp[0].culprit == "Physics");
            CHECK_NEAR(sp[0].culpritExtraMs, 40.0, 1e-9);
            CHECK(sp[1].culprit == "Render");
        }
        CHECK(h.FindSpikes(4.0).size() == 1);                                           // only the 5x frame
        CHECK(h.FindSpikes(10.0).empty());
        CHECK(h.FindSpikes(2.0, 0, 20).size() == 1);                                     // range limits what is scanned
        CHECK(h.FindSpikes(2.0, 0, FrameHistory::kEnd, 40.0).size() == 1);              // minMs
        CHECK(FrameHistory(5).FindSpikes().empty());
        // uniform frames: no spikes, even right at the threshold
        FrameHistory u(10);
        for (int i = 0; i < 6; ++i) u.Push(MakeFrame(10, 1, 1, 1));
        CHECK(u.FindSpikes(1.0).empty());
        // all zero frame times: the median is 0, nothing to compare with
        FrameHistory z(10);
        for (int i = 0; i < 6; ++i) { FrameRecord f; z.Push(f); }
        CHECK(z.FindSpikes(2.0).empty());
    }

    // ---- tree aggregation (flame graph) ----------------------------------------
    {
        FrameHistory h(10);
        for (int i = 0; i < 4; ++i) {
            FrameRecord f;
            f.AddScope("Update", 0, 10.0, 4.0, 1, 0.0);        // 4 self + 6 children
            f.AddScope("Physics", 1, 5.0, 3.0, 1, 0.5);        // 3 self + 2 child
            f.AddScope("Broadphase", 2, 2.0, 2.0, 1, 0.6);
            f.AddScope("Scripts", 1, 1.0, 1.0, 1, 5.5);
            f.AddScope("Render", 0, 6.0, 4.0, 1, 10.0);        // 4 self + 2 child
            f.AddScope("Physics", 1, 2.0, 2.0, 1, 10.1);       // a different Physics: under Render
            h.Push(f);
        }
        FlameGraph g = h.BuildFlameGraph();
        CHECK(g.frames == 4);
        CHECK(!g.Empty());
        CHECK(g.nodes.size() == 1 + 6);                       // root + Update, Physics, Broadphase, Scripts, Render, Render/Physics
        CHECK(g.nodes[0].name == "(frames)" && g.nodes[0].depth == -1);
        CHECK_NEAR(g.TotalMs(), 4 * 16.0, 1e-9);              // sum of the top level inclusive times
        CHECK(g.nodes[0].children.size() == 2);
        // children come largest first
        const FlameNode& top0 = g.nodes[(size_t)g.nodes[0].children[0]];
        const FlameNode& top1 = g.nodes[(size_t)g.nodes[0].children[1]];
        CHECK(top0.name == "Update" && top1.name == "Render");
        CHECK_NEAR(top0.inclusiveMs, 40.0, 1e-9);
        CHECK_NEAR(top0.selfMs, 16.0, 1e-9);
        CHECK(top0.calls == 4);
        CHECK_NEAR(top1.inclusiveMs, 24.0, 1e-9);
        // inclusive = self + children's inclusive, all the way down
        for (const FlameNode& n : g.nodes) {
            if (n.depth < 0) continue;
            double kids = 0.0;
            for (int c : n.children) kids += g.nodes[(size_t)c].inclusiveMs;
            CHECK_NEAR(n.inclusiveMs, n.selfMs + kids, 1e-9);
        }
        // the two Physics scopes stay separate by path but SelfMs adds them
        CHECK_NEAR(g.SelfMs("Physics"), 4 * 3.0 + 4 * 2.0, 1e-9);
        CHECK_NEAR(g.SelfMs("Broadphase"), 8.0, 1e-9);
        CHECK_NEAR(g.SelfMs("Nope"), 0.0, 1e-9);
        int physicsNodes = 0;
        for (const FlameNode& n : g.nodes) if (n.name == "Physics") ++physicsNodes;
        CHECK(physicsNodes == 2);

        // a frame range restricts the aggregate
        FlameGraph g2 = h.BuildFlameGraph(1, 2);
        CHECK(g2.frames == 2 && std::abs(g2.TotalMs() - 32.0) < 1e-9);
        CHECK(FrameHistory(3).BuildFlameGraph().Empty());

        // text, folded stacks, JSON, layout
        std::string text = g.ToText();
        CHECK(text.find("Update") != std::string::npos && text.find("Broadphase") != std::string::npos);
        CHECK(text.find("62.5%") != std::string::npos);        // Update: 40 of 64
        CHECK(g.ToText(1).find("Physics") == std::string::npos);                          // top level only
        CHECK(g.ToText(1).find("Update") != std::string::npos);
        CHECK(g.ToText(0).find("Update") == std::string::npos);
        CHECK(g.ToText(-1, 50.0).find("Render") == std::string::npos);                    // below 50% of the total
        std::string folded = g.ToFolded();
        CHECK(folded.find("Update;Physics;Broadphase 8000\n") != std::string::npos);       // self microseconds
        CHECK(folded.find("Render;Physics 8000\n") != std::string::npos);
        CHECK(folded.find("Update 16000\n") != std::string::npos);
        std::string js = g.ToJson();
        CHECK(Balanced(js) && js.find("\"name\":\"Broadphase\"") != std::string::npos);
        std::vector<FlameGraph::Rect> rects = g.Layout();
        CHECK(rects.size() == 6);
        bool sane = true;
        for (const FlameGraph::Rect& r : rects) if (r.x0 < 0 || r.x1 > 1.0 + 1e-9 || r.x1 < r.x0) sane = false;
        CHECK(sane);
        CHECK(rects[0].node == g.nodes[0].children[0] && std::abs(rects[0].x0) < 1e-9 && std::abs(rects[0].x1 - 40.0 / 64.0) < 1e-9);
        CHECK(g.Layout(0.2).size() < rects.size());                                        // tiny nodes dropped
    }

    // ---- exports of a chosen range ---------------------------------------------
    {
        FrameHistory h(20);
        for (int i = 0; i < 6; ++i) {
            FrameRecord f = MakeFrame(10.0 + i, 2.0, 1.0, 3.0);
            f.startMs = i * 16.0;
            f.AddScope("Quote\"Scope\\", 0, 0.5, 0.5, 1, 7.0);
            h.Push(f);
        }
        std::string js = h.ExportJson(1, 3);
        CHECK(Balanced(js));
        CHECK(CountOf(js, "\"sequence\":") == 3);
        CHECK(js.find("\"frameCount\":3") != std::string::npos);
        CHECK(js.find("Quote\\\"Scope\\\\") != std::string::npos);                         // escaped
        CHECK(js.find("\"sequence\":0") == std::string::npos && js.find("\"sequence\":1") != std::string::npos);
        CHECK(Balanced(h.ExportJson()));
        CHECK(Balanced(FrameHistory(2).ExportJson()));

        std::string tr; int events = h.ExportChromeTrace(tr, 2, 4);
        CHECK(Balanced(tr));
        CHECK(events == 3 * (1 + 4));                                                      // frame event + 4 scopes per frame
        CHECK(CountOf(tr, "\"ph\":\"X\"") == events);
        CHECK(tr.find("\"traceEvents\"") != std::string::npos);
        CHECK(tr.find("\"name\":\"Frame 2\"") != std::string::npos && tr.find("\"name\":\"Frame 5\"") == std::string::npos);
        CHECK(tr.find("\"ts\":32000.000") != std::string::npos);                           // frame 2 starts at 32 ms
        std::string empty; CHECK(FrameHistory(2).ExportChromeTrace(empty) == 0 && Balanced(empty));
        std::string clamped; CHECK(h.ExportChromeTrace(clamped, 4, 999) == 2 * 5);

        std::string text = h.ExportText(0, 1);
        CHECK(text.find("Frame #0") != std::string::npos && text.find("Frame #1") != std::string::npos && text.find("Frame #2") == std::string::npos);
        CHECK(text.find("Summary over 2 frame(s)") != std::string::npos);
        CHECK(FrameHistory(2).ExportText() == "(no frames)\n");
    }

    // ---- Profiler adapter --------------------------------------------------------
    {
        Profiler P;                                   // a private instance
        FrameHistory h(600);
        P.enabled = true;
        auto runFrame = [&P](int ms) {
            P.BeginFrame();
            P.Begin("Update"); BusyUs(20);
              P.Begin("Physics"); BusyUs(20); P.End();
            P.End();
            P.Begin("Render"); BusyUs(20); P.End();
            BusyUs(30);                                // keeps frame start times strictly increasing
            P.EndFrame((double)ms);
        };
        CHECK(h.Capture(P) == 0);                      // nothing yet
        for (int i = 0; i < 5; ++i) runFrame(16);
        CHECK(h.Capture(P) == 5);
        CHECK(h.Size() == 5);
        CHECK(h.Capture(P) == 0);                      // already seen
        for (int i = 0; i < 3; ++i) runFrame(20);
        CHECK(h.Capture(P) == 3);
        CHECK(h.Size() == 8 && h.TotalPushed() == 8);
        CHECK_NEAR(h.At(0).totalMs, 16.0, 1e-9);
        CHECK_NEAR(h.Latest()->totalMs, 20.0, 1e-9);

        // the scope tree is rebuilt: parents before children, nesting recorded
        const FrameRecord& f = h.At(0);
        CHECK(f.scopes.size() == 3);
        if (f.scopes.size() == 3) {
            CHECK(f.scopes[0].name == "Update" && f.scopes[0].parent == -1 && f.scopes[0].depth == 0);
            CHECK(f.scopes[1].name == "Physics" && f.scopes[1].parent == 0 && f.scopes[1].depth == 1);
            CHECK(f.scopes[2].name == "Render" && f.scopes[2].parent == -1);
            CHECK(f.scopes[0].inclusiveMs > f.scopes[0].selfMs);
            CHECK(f.scopes[0].inclusiveMs >= f.scopes[1].inclusiveMs);
        }
        CHECK(h.ScopeStat("Physics").framesPresent == 8);
        FlameGraph g = h.BuildFlameGraph();
        CHECK(g.nodes.size() == 1 + 3);

        // paused / disabled profilers add nothing
        P.paused = true;
        runFrame(16);
        CHECK(h.Capture(P) == 0);
        P.paused = false;
        P.enabled = false;
        runFrame(16);
        CHECK(h.Capture(P) == 0);
        P.enabled = true;

        // far more frames than the Profiler keeps: what it still holds is taken, the rest is gone
        for (int i = 0; i < Profiler::kHistory + 100; ++i) runFrame(16);
        CHECK(h.Capture(P) == (size_t)Profiler::kHistory);
        CHECK(h.Size() == 8 + (size_t)Profiler::kHistory);
        // sequence numbers stay unique and increasing across the whole history
        bool increasing = true;
        for (size_t i = 1; i < h.Size(); ++i) if (h.At(i).sequence != h.At(i - 1).sequence + 1) increasing = false;
        CHECK(increasing);

        // another Profiler whose clock is behind restarts the capture
        Profiler Q;
        Q.enabled = true;
        BusyUs(100);
        Q.BeginFrame(); Q.Begin("Other"); BusyUs(20); Q.End(); Q.EndFrame(5.0);
        size_t before = h.Size();
        CHECK(h.Capture(Q) == 1);
        CHECK(h.Size() == before + 1 && h.Latest()->scopes.size() == 1 && h.Latest()->scopes[0].name == "Other");
        h.ResetCapture();
        CHECK(h.Capture(Q) == 1);                       // everything it holds again
    }

    TEST_MAIN_RESULT();
}
