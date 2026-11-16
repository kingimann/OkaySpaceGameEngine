#pragma once
// The "Save your work before playing / stopping / opening another scene?" prompt (editor/main.cpp,
// DrawActionPrompt): what its Save button does, in order. ImGui-free, so
// tests/test_stop_save_timeline_lod.cpp checks it headless.

namespace okay::editor {

enum class PendingAction { None = 0, Play = 1, Stop = 2, LoadScene = 3 };

struct SaveBeforePlan {
    bool stopFirst = false;       // stop Play before saving, so the edit-time scene is what gets written
    bool runActionAfter = true;   // then run the action the prompt was opened for
};

inline SaveBeforePlan PlanSaveBefore(PendingAction action, bool playing) {
    (void)action; (void)playing;
    return {};
}

// The Save button: `stop()` stops Play, `save()` saves (false = something failed, and the action
// does not run), `run()` runs the pending action. True when the action ran (or was already done).
template <class StopFn, class SaveFn, class RunFn>
bool RunSaveBefore(PendingAction action, bool playing, StopFn&& stop, SaveFn&& save, RunFn&& run) {
    const SaveBeforePlan plan = PlanSaveBefore(action, playing);
    if (plan.stopFirst) stop();
    if (!save()) return false;
    if (plan.runActionAfter) run();
    return true;
}

}  // namespace okay::editor
