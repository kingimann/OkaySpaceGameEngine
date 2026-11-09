// 1.1 FrameStats: the rolling frame-time window GameLoop feeds (average, 1% low, 0.1% low, worst,
// hitches) and its adapter to Debug/FrameHistory. Made-up frame times only.
#include "test_framework.hpp"
#include "okay/Core/FrameRecord.hpp"
#include "okay/Core/FrameStats.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Debug/FrameHistory.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
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
}

std::uint32_t g_s = 31337u;
float Rand01() { g_s ^= g_s << 13; g_s ^= g_s >> 17; g_s ^= g_s << 5; return (float)(g_s >> 8) / 16777216.0f; }

}  // namespace

int main() {
    RUN_SUITE("frame_stats");
    Log::console = false;

    // ---- empty, one frame ---------------------------------------------------------------------------------------------------------------
    {
        FrameStats s;
        CHECK(s.Window() == 240 && s.Empty() && s.Size() == 0 && s.TotalFrames() == 0 && s.LastMs() == 0.0);
        FrameStatsSnapshot z = s.Snapshot();
        CHECK(z.frames == 0 && z.fps == 0.0 && z.avgMs == 0.0 && z.low1PercentFps == 0.0 && z.hitches == 0 && z.window == 240);
        CHECK(s.ToText() == "no frames" && s.ToJson().find("\"frames\":0") != std::string::npos);
        CHECK(s.Values().empty());
        s.Push(16.0);
        z = s.Snapshot();
        CHECK(z.frames == 1 && z.avgMs == 16.0 && z.minMs == 16.0 && z.worstMs == 16.0 && z.p99Ms == 16.0);
        CHECK(std::fabs(z.fps - 62.5) < 1e-9 && z.low1PercentMs == 16.0 && z.low01PercentMs == 16.0 && std::fabs(z.low1PercentFps - 62.5) < 1e-9);
        CHECK(s.LastMs() == 16.0 && s.TotalFrames() == 1);
    }

    // ---- the figures on known data ----------------------------------------------------------------------------------------------------------
    {
        FrameStats s(1000);
        for (int i = 0; i < 990; ++i) s.Push(10.0);
        for (int i = 0; i < 10; ++i) s.Push(40.0);            // 1% of the frames are 4x slower
        FrameStatsSnapshot z = s.Snapshot();
        CHECK(z.frames == 1000);
        CHECK_NEAR(z.avgMs, 10.3, 1e-9);
        CHECK_NEAR(z.fps, 1000.0 / 10.3, 1e-9);
        CHECK_NEAR(z.worstMs, 40.0, 1e-9);
        CHECK_NEAR(z.low1PercentMs, 40.0, 1e-9);              // the slowest 10 of 1000 frames
        CHECK_NEAR(z.low1PercentFps, 25.0, 1e-9);
        CHECK_NEAR(z.low01PercentMs, 40.0, 1e-9);             // the single slowest
        CHECK_NEAR(z.p50Ms, 10.0, 1e-9);
        CHECK_NEAR(z.p99Ms, 10.3, 1e-9);                      // rank 989.01: a hundredth of the way from a 10 ms frame to a 40 ms one
        CHECK(z.hitches == 0);
        // a different mix: the 0.1% low is the worst single frame, the 1% low the mean of the worst ten
        FrameStats t(1000);
        for (int i = 0; i < 989; ++i) t.Push(10.0);
        for (int i = 0; i < 10; ++i) t.Push(20.0);
        t.Push(120.0);
        z = t.Snapshot();
        CHECK_NEAR(z.low01PercentMs, 120.0, 1e-9);
        CHECK_NEAR(z.low1PercentMs, (120.0 + 9 * 20.0) / 10.0, 1e-9);
        CHECK_NEAR(z.low01PercentFps, 1000.0 / 120.0, 1e-9);
        CHECK(z.hitches == 1 && z.totalHitches == 1);
        // small windows: at least one frame backs each low
        FrameStats u(5);
        for (double v : {10.0, 11.0, 12.0, 13.0, 50.0}) u.Push(v);
        z = u.Snapshot();
        CHECK_NEAR(z.low1PercentMs, 50.0, 1e-9);
        CHECK_NEAR(z.low01PercentMs, 50.0, 1e-9);
        CHECK_NEAR(z.avgMs, 19.2, 1e-9);
        CHECK_NEAR(z.minMs, 10.0, 1e-9);
        CHECK(z.low1PercentMs >= z.avgMs && z.low01PercentMs >= z.low1PercentMs);   // the lows are never better than the average
    }

    // ---- the window rolls ---------------------------------------------------------------------------------------------------------------------
    {
        FrameStats s(4);
        for (int i = 1; i <= 10; ++i) s.Push((double)i);
        CHECK(s.Size() == 4 && s.TotalFrames() == 10);
        CHECK((s.Values() == std::vector<double>{7.0, 8.0, 9.0, 10.0}));   // oldest first
        CHECK(s.LastMs() == 10.0);
        FrameStatsSnapshot z = s.Snapshot();
        CHECK(z.frames == 4 && z.totalFrames == 10 && std::fabs(z.avgMs - 8.5) < 1e-9 && z.worstMs == 10.0 && z.minMs == 7.0);
        // growing keeps what there is, shrinking keeps the newest
        s.SetWindow(8);
        CHECK(s.Window() == 8 && s.Size() == 4 && (s.Values() == std::vector<double>{7.0, 8.0, 9.0, 10.0}));
        for (int i = 11; i <= 14; ++i) s.Push((double)i);
        CHECK((s.Values() == std::vector<double>{7.0, 8.0, 9.0, 10.0, 11.0, 12.0, 13.0, 14.0}));
        s.Push(15.0);
        CHECK(s.Values().front() == 8.0 && s.Values().back() == 15.0);
        s.SetWindow(3);
        CHECK((s.Values() == std::vector<double>{13.0, 14.0, 15.0}));
        s.Push(16.0);
        CHECK((s.Values() == std::vector<double>{14.0, 15.0, 16.0}));
        // window 0 records nothing but still counts
        s.SetWindow(0);
        CHECK(s.Size() == 0 && s.Window() == 0);
        s.Push(99.0);
        CHECK(s.Empty() && s.TotalFrames() == 17 && s.TotalHitches() == 0 && s.Snapshot().frames == 0);
        // a huge window is capped
        s.SetWindow(100000000);
        CHECK(s.Window() == FrameStats::kMaxWindow);
        // Clear
        FrameStats c(10);
        c.Push(5.0); c.Push(500.0);
        c.Clear();
        CHECK(c.Empty() && c.TotalFrames() == 0 && c.TotalHitches() == 0 && c.Window() == 10);
        c.Push(7.0);
        CHECK(c.Size() == 1 && c.LastMs() == 7.0);
    }

    // ---- bad numbers are 0 ms frames, hitches follow the threshold -----------------------------------------------------------------------------------
    {
        FrameStats s(10);
        s.Push(std::numeric_limits<double>::quiet_NaN());
        s.Push(std::numeric_limits<double>::infinity());
        s.Push(-5.0);
        CHECK(s.Size() == 3 && s.TotalFrames() == 3 && s.Snapshot().worstMs == 0.0 && !std::isnan(s.Snapshot().avgMs));
        FrameStats h(10, 50.0);
        CHECK(h.HitchMs() == 50.0);
        h.Push(49.0); h.Push(50.0); h.Push(50.1); h.Push(300.0);
        CHECK(h.Snapshot().hitches == 2 && h.TotalHitches() == 2);   // strictly longer than the threshold
        h.SetHitchMs(0.0);                                    // counting off: the window is recounted, the total keeps what was counted
        CHECK(h.Snapshot().hitches == 0 && h.TotalHitches() == 2);
        h.Push(900.0);
        CHECK(h.TotalHitches() == 2);
        h.SetHitchMs(std::numeric_limits<double>::quiet_NaN());
        CHECK(h.HitchMs() == 0.0);
        h.SetHitchMs(100.0);
        CHECK(h.Snapshot().hitches == 2 && h.Snapshot().hitchMs == 100.0);   // 300 and 900
    }

    // ---- text and JSON --------------------------------------------------------------------------------------------------------------------------------
    {
        FrameStats s(300);
        for (int i = 0; i < 99; ++i) s.Push(1000.0 / 60.0);
        s.Push(150.0);
        const std::string t = s.ToText();
        CHECK(t.find("100 frames:") == 0);
        CHECK(t.find("fps avg") != std::string::npos && t.find("1% low") != std::string::npos && t.find("0.1% low") != std::string::npos);
        CHECK(t.find("worst 150.00 ms") != std::string::npos && t.find("1 hitch") != std::string::npos && t.find("1 hitches") == std::string::npos);
        const std::string j = s.ToJson();
        CHECK(j.front() == '{' && j.back() == '}' && j.find(' ') == std::string::npos);
        for (const char* key : {"\"frames\":100", "\"window\":300", "\"totalFrames\":100", "\"worstMs\":150.0000", "\"hitches\":1", "\"low1PercentFps\":", "\"low01PercentMs\":150.0000", "\"p95Ms\":", "\"fps\":"})
            CHECK(j.find(key) != std::string::npos);
    }

    // ---- the Debug/FrameHistory adapter ---------------------------------------------------------------------------------------------------------------------
    {
        FrameStats s(50);
        FrameHistory history(1000);
        for (int i = 0; i < 10; ++i) s.Push(16.0);
        CHECK(s.SyncTo(history) == 10 && history.Size() == 10);
        CHECK(s.SyncTo(history) == 0);                        // nothing new
        s.Push(16.0); s.Push(90.0); s.Push(16.0);
        CHECK(s.SyncTo(history) == 3 && history.Size() == 13);
        const Distribution d = history.SeriesStats(FrameHistory::Series::Total);
        CHECK(d.count == 13 && std::fabs(d.max - 90.0) < 1e-9 && std::fabs(d.min - 16.0) < 1e-9);
        CHECK_NEAR(d.avg, s.Snapshot().avgMs, 1e-9);          // the same average from either side
        const std::vector<FrameSpike> spikes = history.FindSpikes(2.0);   // FrameHistory's own spike finder works on loop frames
        CHECK(spikes.size() == 1 && spikes[0].frame == 11 && std::fabs(spikes[0].totalMs - 90.0) < 1e-9);
        CHECK(history.At(0).startMs == 0.0 && std::fabs(history.At(1).startMs - 16.0) < 1e-9 && std::fabs(history.At(12).startMs - (11 * 16.0 + 90.0)) < 1e-9);
        CHECK(history.At(11).cpuMs == 90.0 && history.At(11).totalMs == 90.0);
        // more pushed than the window holds: only the window's worth is copied
        FrameStats w(5);
        FrameHistory h2(1000);
        for (int i = 0; i < 30; ++i) w.Push(10.0 + i);
        CHECK(w.SyncTo(h2) == 5 && h2.At(0).totalMs == 35.0 && h2.At(4).totalMs == 39.0);
        // a second history needs ResetSync: it then gets the window again
        FrameHistory h3(1000);
        CHECK(w.SyncTo(h3) == 0);
        w.ResetSync();
        CHECK(w.SyncTo(h3) == 5);
        // Clear starts a new timeline
        w.Clear();
        w.Push(1.0);
        CHECK(w.SyncTo(h3) == 1);
    }

    // ---- GameLoop feeds it -------------------------------------------------------------------------------------------------------------------------------------
    {
        Fresh();
        GameLoop::Options o; o.statsWindow = 50;
        GameLoop loop(o);
        CHECK(loop.Stats().Window() == 50 && loop.Stats().Empty());
        for (int i = 0; i < 100; ++i) loop.BeginFrame(i == 90 ? 5.0 : 0.016);
        CHECK(loop.Stats().Size() == 50 && loop.Stats().TotalFrames() == 100);
        FrameStatsSnapshot z = loop.Stats().Snapshot();
        CHECK_NEAR(z.worstMs, 5000.0, 1e-3);                  // the measured time, not the 0.25 s the game saw
        CHECK(z.hitches == 1 && loop.HitchCount() == 1 && loop.Stats().TotalHitches() == 1);
        CHECK_NEAR(z.minMs, 16.0, 1e-3);
        CHECK(z.low01PercentMs > 4999.0);
        CHECK_NEAR(loop.Stats().LastMs(), 16.0, 1e-3);
        // a hitch threshold from the loop's options
        Fresh();
        GameLoop::Options p; p.hitchThreshold = 0.03f;
        GameLoop lp(p);
        lp.BeginFrame(0.02); lp.BeginFrame(0.04); lp.BeginFrame(0.05);
        CHECK(lp.Stats().HitchMs() == 30.0 || std::fabs(lp.Stats().HitchMs() - 30.0) < 1e-3);
        CHECK(lp.Stats().Snapshot().hitches == 2 && lp.HitchCount() == 2);
        p.hitchThreshold = 0.0f;
        lp.SetOptions(p);
        CHECK(lp.Stats().HitchMs() == 0.0);
        // off
        Fresh();
        GameLoop::Options off; off.statsWindow = 0;
        GameLoop quiet(off);
        for (int i = 0; i < 10; ++i) quiet.BeginFrame(0.016);
        CHECK(quiet.Stats().Empty() && quiet.Stats().TotalFrames() == 10);
        off.statsWindow = -4;
        quiet.SetOptions(off);
        CHECK(quiet.Stats().Window() == 0);
        // Reset and BeginSession clear it
        Fresh();
        GameLoop r;
        for (int i = 0; i < 10; ++i) r.BeginFrame(0.016);
        CHECK(r.Stats().Size() == 10);
        r.Reset();
        CHECK(r.Stats().Empty() && r.Stats().TotalFrames() == 0);
        r.BeginFrame(0.016);
        r.BeginSession();
        CHECK(r.Stats().Empty());
        // paused frames and debug steps count as frames with the time they used
        Fresh();
        GameLoop s;
        Game::SetPaused(true);
        s.BeginFrame(0.016);
        s.RequestStep(1, 0.04f);
        s.BeginFrame(0.016);
        CHECK(s.Stats().Size() == 2 && std::fabs(s.Stats().LastMs() - 40.0) < 1e-3);
        // capture: every frame is the capture time
        Fresh();
        GameLoop c;
        c.SetCapture(50.0f);
        for (int i = 0; i < 20; ++i) c.BeginFrame(0.3);
        z = c.Stats().Snapshot();
        CHECK(z.frames == 20 && std::fabs(z.avgMs - 20.0) < 1e-3 && std::fabs(z.worstMs - 20.0) < 1e-3 && std::fabs(z.fps - 50.0) < 1e-2);
        // replay: the stats of a replay equal the stats of the live run
        Fresh();
        GameLoop live;
        FrameRecorder rec;
        live.AttachRecorder(&rec);
        g_s = 4242u;
        for (int i = 0; i < 200; ++i) live.BeginFrame(Rand01() < 0.05f ? 0.2 + Rand01() : 0.014 + Rand01() * 0.01);
        Fresh();
        GameLoop again;
        FramePlayer player(rec.Recording());
        again.AttachPlayer(&player);
        while (again.BeginFrame(0.0)) {}
        CHECK(live.Stats().ToJson() == again.Stats().ToJson());
        CHECK_NEAR(live.Stats().Snapshot().worstMs, rec.Recording().Summary().maxMs, 1e-9);
        CHECK_NEAR(live.Stats().Snapshot().avgMs, rec.Recording().Summary().avgMs, 1e-9);
        CHECK(live.Stats().Snapshot().hitches == (int)rec.Recording().Summary().hitches);
        Fresh();
    }

    // ---- property: the lows are ordered and bounded for any data ------------------------------------------------------------------------------------------
    {
        bool ordered = true, bounded = true, rolling = true;
        for (int trial = 0; trial < 200; ++trial) {
            const std::size_t window = 1 + (std::size_t)(Rand01() * 300.0f);
            const int n = 1 + (int)(Rand01() * 700.0f);
            FrameStats s(window);
            std::vector<double> all;
            for (int i = 0; i < n; ++i) { const double v = Rand01() < 0.1f ? 50.0 + Rand01() * 500.0 : 5.0 + Rand01() * 20.0; s.Push(v); all.push_back(v); }
            const FrameStatsSnapshot z = s.Snapshot();
            const std::size_t kept = std::min(window, all.size());
            if (z.frames != kept) rolling = false;
            const std::vector<double> vals = s.Values();
            for (std::size_t i = 0; i < kept; ++i) if (vals[i] != all[all.size() - kept + i]) rolling = false;
            if (!(z.minMs <= z.avgMs + 1e-9 && z.avgMs <= z.worstMs + 1e-9)) ordered = false;
            if (!(z.low1PercentMs >= z.avgMs - 1e-9 && z.low01PercentMs >= z.low1PercentMs - 1e-9 && z.worstMs >= z.low01PercentMs - 1e-9)) ordered = false;
            if (!(z.p50Ms >= z.minMs - 1e-9 && z.p99Ms <= z.worstMs + 1e-9 && z.p95Ms <= z.p99Ms + 1e-9)) bounded = false;
        }
        CHECK(ordered);
        CHECK(bounded);
        CHECK(rolling);
    }

    Fresh();
    TEST_MAIN_RESULT();
}
