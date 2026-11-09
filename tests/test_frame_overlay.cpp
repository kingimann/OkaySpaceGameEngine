// 1.1 fourth pass: FrameOverlay, the text the player's F3 panel and the editor's Game view share. The formatting is
// pinned on made-up numbers (the pure Format form) and on a real GameLoop fed made-up frame times. No clocks, no sleeping.
#include "test_framework.hpp"
#include "okay/Core/FrameOverlay.hpp"
#include "okay/Core/FrameStats.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Time.hpp"
#include <clocale>
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

bool Has(const std::vector<std::string>& lines, const std::string& text) {
    for (const std::string& l : lines) if (l == text) return true;
    return false;
}

bool HasPrefix(const std::vector<std::string>& lines, const std::string& prefix) {
    for (const std::string& l : lines) if (l.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
}

}  // namespace

int main() {
    RUN_SUITE("frame_overlay");

    // ---- the pure form: every line, exactly ---------------------------------------------------------------------------------
    {
        FrameStatsSnapshot st;
        st.frames = 240; st.window = 240; st.totalFrames = 240;
        st.fps = 59.8; st.avgMs = 16.72; st.minMs = 15.9; st.worstMs = 22.1; st.p99Ms = 20.3;
        st.low1PercentFps = 48.2; st.low01PercentFps = 40.1;
        FrameOverlayState s;
        const std::vector<std::string> lines = FrameOverlay::Format(st, s);
        CHECK(lines.size() == 4);
        CHECK(lines.size() == 4 && lines[0] == "59.8 fps  16.72 ms avg");
        CHECK(lines.size() == 4 && lines[1] == "min 15.90  max 22.10  p99 20.30 ms");
        CHECK(lines.size() == 4 && lines[2] == "1% low 48.2 fps  0.1% low 40.1 fps");
        CHECK(lines.size() == 4 && lines[3] == "time x1.00");
        CHECK(FrameOverlay::Text(lines) == "59.8 fps  16.72 ms avg\nmin 15.90  max 22.10  p99 20.30 ms\n1% low 48.2 fps  0.1% low 40.1 fps\ntime x1.00");
        CHECK(FrameOverlay::Text(std::vector<std::string>()) == "");
    }

    // ---- optional lines appear only when they have something to say ----------------------------------------------------------
    {
        FrameStatsSnapshot st;
        st.frames = 10; st.fps = 50; st.avgMs = 20; st.minMs = 20; st.worstMs = 500; st.p99Ms = 500;
        st.low1PercentFps = 2; st.low01PercentFps = 2;
        st.hitches = 1; st.totalHitches = 3;
        FrameOverlayState s;
        s.clampedFrames = 1; s.lostRealSeconds = 0.25; s.droppedFixedSeconds = 0.16;
        s.timeScale = 0.5f; s.hitStop = true; s.hitStopSeconds = 0.08f; s.ramping = true; s.rampTarget = 1.0f;
        s.debugTimeScale = 0.25f; s.captureFps = 60.0f; s.runFramesLeft = 7; s.stepsLeft = 3;
        s.pausedBy = {"menu", "cutscene"};
        const std::vector<std::string> lines = FrameOverlay::Format(st, s);
        CHECK(Has(lines, "hitches 1 (3 total)  worst 500 ms"));
        CHECK(Has(lines, "dropped 1 frame (0.25 s lost)"));
        CHECK(Has(lines, "fixed steps dropped 0.16 s"));
        CHECK(Has(lines, "time x0.50  hit stop 0.08 s  ramp to x1.00  debug x0.25  capture 60 fps  run 7 more  step 3"));
        CHECK(Has(lines, "paused by: menu, cutscene"));
        s.clampedFrames = 2;
        CHECK(Has(FrameOverlay::Format(st, s), "dropped 2 frames (0.25 s lost)"));
        // nothing to report: none of them
        FrameStatsSnapshot calm;
        calm.frames = 10; calm.fps = 60; calm.avgMs = 16.67;
        const std::vector<std::string> quiet = FrameOverlay::Format(calm, FrameOverlayState());
        CHECK(!HasPrefix(quiet, "hitches") && !HasPrefix(quiet, "dropped") && !HasPrefix(quiet, "fixed") && !HasPrefix(quiet, "paused"));
        // hitches seen earlier but none in the window now still show (the total)
        calm.totalHitches = 2;
        CHECK(Has(FrameOverlay::Format(calm, FrameOverlayState()), "hitches 0 (2 total)  worst 0 ms"));
        // no frame in the window: a line that says so, then the state lines
        const std::vector<std::string> empty = FrameOverlay::Format(FrameStatsSnapshot(), FrameOverlayState());
        CHECK(empty.size() == 2 && empty[0] == "no frames yet" && empty[1] == "time x1.00");
    }

    // ---- a real loop: the same numbers FrameStats reports, and the state of Time and Game -------------------------------------
    {
        Fresh();
        GameLoop::Options o;
        o.smoothDelta = false;
        GameLoop loop(o);
        CHECK(FrameOverlay::Lines(loop).size() == 2 && FrameOverlay::Lines(loop)[0] == "no frames yet");
        for (int i = 0; i < 10; ++i) loop.BeginFrame(0.020);
        std::vector<std::string> lines = FrameOverlay::Lines(loop);
        CHECK(lines.size() == 4);
        CHECK(lines.size() == 4 && lines[0] == "50.0 fps  20.00 ms avg");
        CHECK(lines.size() == 4 && lines[1] == "min 20.00  max 20.00  p99 20.00 ms");
        CHECK(lines.size() == 4 && lines[2] == "1% low 50.0 fps  0.1% low 50.0 fps");
        CHECK(lines.size() == 4 && lines[3] == "time x1.00");
        // the text matches what FrameStats itself says (one source of numbers)
        const FrameStatsSnapshot snap = loop.Stats().Snapshot();
        CHECK(lines[0] == FrameOverlay::Format(snap, FrameOverlay::StateOf(loop))[0]);

        // a stall: a hitch, a dropped (clamped) frame and dropped fixed-step time, all from the loop's own counters
        loop.BeginFrame(0.5);
        lines = FrameOverlay::Lines(loop);
        CHECK(Has(lines, "hitches 1 (1 total)  worst 500 ms"));
        CHECK(Has(lines, "dropped 1 frame (0.25 s lost)"));
        CHECK(HasPrefix(lines, "fixed steps dropped "));

        // slow motion, a hit stop, a debug scale and two pause reasons
        Fresh();
        GameLoop loop2(o);
        Time::SetTimeScale(0.5f);
        Time::HitStop(0.08f, 0.0f);
        CHECK(loop2.SetDebugTimeScale(0.25f));
        Game::PushPause("menu");
        Game::PushPause("cutscene");
        lines = FrameOverlay::Lines(loop2);
        CHECK(Has(lines, "time x0.50  hit stop 0.08 s  debug x0.25"));
        CHECK(Has(lines, "paused by: menu, cutscene"));
        Game::PopPause("menu");
        CHECK(Has(FrameOverlay::Lines(loop2), "paused by: cutscene"));
        Game::PopPause("cutscene");
        CHECK(!HasPrefix(FrameOverlay::Lines(loop2), "paused"));

        // capture mode, a pending run and pending steps
        Fresh();
        GameLoop loop3(o);
        loop3.SetCapture(60.0f);
        loop3.RunFrames(5, 0.01f);
        CHECK(Has(FrameOverlay::Lines(loop3), "time x1.00  capture 60 fps  run 5 more"));
        Fresh();
        GameLoop loop4(o);
        Game::PushPause("menu");
        loop4.RequestStep(2);
        CHECK(Has(FrameOverlay::Lines(loop4), "time x1.00  step 2"));
        Game::PopPause("menu");
        Time::CancelHitStop();
    }

    // ---- the numbers use a dot whatever the process locale ---------------------------------------------------------------------
    {
        const char* old = std::setlocale(LC_NUMERIC, nullptr);
        const std::string saved = old ? old : "C";
        const char* tried[] = {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "fr_FR.utf8", "de_DE", "fr_FR"};
        bool changed = false;
        for (const char* l : tried) if (std::setlocale(LC_NUMERIC, l)) { changed = true; break; }
        FrameStatsSnapshot st;
        st.frames = 1; st.fps = 59.8; st.avgMs = 16.72;
        const std::vector<std::string> lines = FrameOverlay::Format(st, FrameOverlayState());
        CHECK(lines[0] == "59.8 fps  16.72 ms avg");   // with a comma locale installed this is the check that matters
        (void)changed;
        std::setlocale(LC_NUMERIC, saved.c_str());
    }

    // ---- graph series ------------------------------------------------------------------------------------------------------------
    {
        FrameStats stats(8);
        stats.Push(10.0); stats.Push(20.0); stats.Push(0.0);
        const std::vector<float> fps = FrameOverlay::RecentFps(stats, 5);
        CHECK(fps.size() == 5 && fps[0] == 0.0f && fps[1] == 0.0f && fps[2] == 100.0f && fps[3] == 50.0f && fps[4] == 0.0f);
        const std::vector<float> ms = FrameOverlay::RecentMs(stats, 5);
        CHECK(ms.size() == 5 && ms[2] == 10.0f && ms[3] == 20.0f && ms[4] == 0.0f && ms[0] == 0.0f);
        const std::vector<float> two = FrameOverlay::RecentMs(stats, 2);      // fewer than there are: the newest
        CHECK(two.size() == 2 && two[0] == 20.0f && two[1] == 0.0f);
        CHECK(FrameOverlay::RecentFps(stats, 0).empty());
        CHECK(FrameOverlay::RecentFps(stats, 1000000).size() == 4096);          // capped
        CHECK(FrameOverlay::RecentFps(FrameStats(0), 3).size() == 3);            // statistics off: all zeros
        // wraps around the window
        FrameStats ring(3);
        for (int i = 1; i <= 7; ++i) ring.Push(i * 10.0);
        const std::vector<float> last = FrameOverlay::RecentMs(ring, 3);
        CHECK(last[0] == 50.0f && last[1] == 60.0f && last[2] == 70.0f);
    }

    TEST_MAIN_RESULT();
}
