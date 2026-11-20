#pragma once
// The part of the editor's per-frame work that steps the game and has nothing to do with Dear ImGui, moved out of editor/main.cpp so window mode
// (editor/OkayUIHostMode.cpp) can run the same step. Header-only and templated on the editor-state type, so the logic is tested headless with a small fake
// (okayui/tests/test_okayui_frametick.cpp) while the editor uses the real okay::editor::EditorState. The code is the block that was in main.cpp, moved as is:
// docs/ui/own_ui_toolkit.md 6.24 step 4. No <mutex>/<thread> (Windows build rule).
#include <functional>

namespace oked {

/// One frame's numbers: the real time since the last frame, the editor's Pause button, the frame-time smoothing setting and the display's refresh rate
/// (0 = unknown or vsync off).
struct SimStep { float dt = 0.0f; bool paused = false; bool smoothFrameTime = true; float refreshHz = 0.0f; };

/// What the step needs from its host. `tickGame` runs one scene tick (the editor's runs it on the visual-script debugger's fiber while a breakpoint is
/// active); `onRunFramesDone` is told when "Run N frames" ended; `resumeBreakPause` lets a behaviour-tree node breakpoint go once the Pause button is up.
template <class ES> struct SimHooks {
    std::function<void(ES&, float)> tickGame;
    std::function<void()> onRunFramesDone;
    std::function<void()> resumeBreakPause;
};

/// Step the simulation one frame. In Play: hand the Play loop its tick, follow the Pause button, and let the loop step Time and tick the scene by the
/// scaled delta (0 while paused). Editing never steps Time: the scene ticks once with the delta cut to 0.1 s.
template <class ES> void StepSimulation(ES& ed, const SimStep& step, const SimHooks<ES>& hooks) {
    if (ed.isPlaying()) {
        if (!ed.playTick) ed.playTick = [&ed, hooks](float gdt) { if (hooks.tickGame) hooks.tickGame(ed, gdt); };   // the Play loop's update
        if (!ed.onRunFramesDone) ed.onRunFramesDone = hooks.onRunFramesDone ? hooks.onRunFramesDone : std::function<void()>([] {});
        if (!step.paused && hooks.resumeBreakPause) hooks.resumeBreakPause();
        ed.SetPaused(step.paused);   // the Pause button = Game::PushPause("editor")
        ed.PlayFrame((double)step.dt, step.smoothFrameTime, step.refreshHz);
    } else if (hooks.tickGame) {
        hooks.tickGame(ed, step.dt > 0.1f ? 0.1f : step.dt);   // editing never steps Time
    }
}

/// What happens after the step in a frame: a game that asked to quit during Play stops Play (returns true so the caller can log and clear its Pause),
/// then `afterSim` (the Inspector preview, modifier stacks: host specific) and the services tick (Steam callbacks, networking) run.
template <class ES> bool FinishFrame(ES& ed, float dt, bool gameQuitRequested, const std::function<void()>& stopPreview, const std::function<void(float)>& afterSim) {
    bool quit = false;
    if (ed.isPlaying() && gameQuitRequested) { if (stopPreview) stopPreview(); ed.Stop(); quit = true; }
    if (afterSim) afterSim(dt);
    ed.TickServices(dt);
    return quit;
}

} // namespace oked
