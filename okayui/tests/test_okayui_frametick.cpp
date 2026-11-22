// The game-step part of the editor frame (editor/EditorFrameTick.hpp), headless with a fake editor state: what StepSimulation and FinishFrame do in Play,
// while paused, while editing, with hooks missing, and in what order. The block moved out of editor/main.cpp as is, so these are the rules main.cpp had.
// Step 4 of the window-mode plan (docs/ui/own_ui_toolkit.md 6.24).
#include "test_framework.hpp"
#include "EditorFrameTick.hpp"

#include <string>
#include <vector>

namespace {
struct FakeEditor {
    bool playing = false, pausedSet = false; int pauseCalls = 0, playFrames = 0, stops = 0, services = 0;
    double lastReal = -1; bool lastSmooth = false; float lastHz = -1;
    std::function<void(float)> playTick; std::function<void()> onRunFramesDone;
    std::vector<std::string> log;
    bool isPlaying() const { return playing; }
    void SetPaused(bool p) { pausedSet = p; ++pauseCalls; log.push_back(p ? "pause" : "unpause"); }
    bool PlayFrame(double real, bool smooth, float hz) { lastReal = real; lastSmooth = smooth; lastHz = hz; ++playFrames; log.push_back("play"); if (playTick) playTick((float)real); return true; }
    void Stop() { playing = false; ++stops; log.push_back("stop"); }
    void TickServices(float) { ++services; log.push_back("services"); }
};
}

int main() {
    RUN_SUITE("editor frame tick (game step)");
    using oked::SimStep; using oked::SimHooks; using oked::StepSimulation; using oked::FinishFrame;
    std::vector<float> ticks; int resumes = 0, doneCalls = 0;
    SimHooks<FakeEditor> hooks;
    hooks.tickGame = [&](FakeEditor&, float d) { ticks.push_back(d); };
    hooks.onRunFramesDone = [&] { ++doneCalls; };
    hooks.resumeBreakPause = [&] { ++resumes; };

    {   // editing: one tick, the delta cut to 0.1 s, no Play loop, no pause calls
        FakeEditor ed;
        StepSimulation(ed, SimStep{0.016f, false, true, 0.0f}, hooks);
        StepSimulation(ed, SimStep{0.5f, false, true, 0.0f}, hooks);
        StepSimulation(ed, SimStep{0.1f, true, true, 0.0f}, hooks);
        CHECK(ticks.size() == 3 && ticks[0] == 0.016f && ticks[1] == 0.1f && ticks[2] == 0.1f);
        CHECK(ed.playFrames == 0 && ed.pauseCalls == 0 && !ed.playTick && resumes == 0);
    }
    {   // Play: the loop gets its tick once, follows the Pause button, and is handed the real delta, smoothing and refresh rate
        ticks.clear(); resumes = 0;
        FakeEditor ed; ed.playing = true;
        StepSimulation(ed, SimStep{0.02f, false, false, 144.0f}, hooks);
        CHECK(ed.playFrames == 1 && ed.lastReal > 0.0199 && ed.lastReal < 0.0201 && !ed.lastSmooth && ed.lastHz == 144.0f);
        CHECK(ed.playTick && ed.onRunFramesDone && resumes == 1 && ed.pauseCalls == 1 && !ed.pausedSet);
        CHECK(ticks.size() == 1 && ticks[0] > 0.0199f);          // the loop's tick went to the host's tickGame with the scaled delta it chose
        const auto tick0 = ed.playTick.target_type().hash_code();
        StepSimulation(ed, SimStep{0.02f, true, true, 0.0f}, hooks);   // paused: the Pause button goes down, a node breakpoint is NOT released
        CHECK(ed.pausedSet && resumes == 1 && ed.pauseCalls == 2 && ed.playFrames == 2 && ed.playTick.target_type().hash_code() == tick0);
        ed.onRunFramesDone(); CHECK(doneCalls == 1);
        // the order inside one Play frame: pause state first, then the loop
        FakeEditor e2; e2.playing = true; StepSimulation(e2, SimStep{0.01f, false, true, 0.0f}, hooks);
        CHECK(e2.log.size() == 2 && e2.log[0] == "unpause" && e2.log[1] == "play");
    }
    {   // a host's own onRunFramesDone is not overwritten; hooks left empty do not crash
        FakeEditor ed; ed.playing = true; int mine = 0; ed.onRunFramesDone = [&] { ++mine; };
        SimHooks<FakeEditor> bare;   // nothing set
        StepSimulation(ed, SimStep{0.01f, false, true, 0.0f}, bare);
        ed.onRunFramesDone(); CHECK(mine == 1 && ed.playFrames == 1);
        FakeEditor e2; e2.playing = true; StepSimulation(e2, SimStep{0.01f, false, true, 0.0f}, bare); e2.onRunFramesDone();   // the no-op default
        FakeEditor e3; StepSimulation(e3, SimStep{0.01f, false, true, 0.0f}, bare);                                           // editing with no tickGame: nothing
        CHECK(e2.playFrames == 1 && e3.playFrames == 0);
    }
    {   // the end of the frame: quit stops Play and says so; the host's after-step and the services always run, in that order
        FakeEditor ed; ed.playing = true; int previews = 0, after = 0;
        const bool q = FinishFrame(ed, 0.016f, true, [&] { ++previews; ed.log.push_back("preview"); }, [&](float) { ++after; ed.log.push_back("after"); });
        CHECK(q && !ed.playing && ed.stops == 1 && previews == 1 && after == 1 && ed.services == 1);
        CHECK(ed.log.size() == 4 && ed.log[0] == "preview" && ed.log[1] == "stop" && ed.log[2] == "after" && ed.log[3] == "services");
        FakeEditor e2; e2.playing = true;
        CHECK(!FinishFrame(e2, 0.016f, false, nullptr, nullptr) && e2.playing && e2.services == 1);          // no quit: Play goes on
        FakeEditor e3;   // editing: a quit flag left over from the game does nothing
        CHECK(!FinishFrame(e3, 0.016f, true, nullptr, nullptr) && e3.stops == 0 && e3.services == 1);
    }
    TEST_MAIN_RESULT();
}
