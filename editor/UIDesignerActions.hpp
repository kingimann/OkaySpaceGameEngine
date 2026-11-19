#pragma once
// Canvas actions of the UI designer that change the scene, without ImGui, so a test can check that each one is a
// single undo step. The window and main.cpp call these.
#include "EditorState.hpp"
#include "UIDesignerModel.hpp"
#include "okay/Components/UIElement.hpp"

namespace oked::uid {

/// Arrow keys on the canvas: move every selected widget (skipping those `isLocked` says no to) by 1 pixel, or by the
/// grid step with Shift. One undo step. Returns how many widgets moved.
template <class LockedFn>
inline int NudgeSelection(okay::editor::EditorState& ed, int ax, int ay, bool shift, float gridStep, LockedFn isLocked) {
    float dx = 0, dy = 0;
    if (!Nudge(ax, ay, shift, gridStep, dx, dy)) return 0;
    std::vector<okay::GameObject*> targets;
    for (okay::GameObject* g : ed.MultiSelection()) {
        if (!g || isLocked(g)) continue;
        okay::UIRect r = okay::GetUIRect(g);
        if (r.valid && r.position) targets.push_back(g);
    }
    if (targets.empty()) return 0;
    ed.PushUndo("Nudge UI");
    for (okay::GameObject* g : targets) { okay::UIRect r = okay::GetUIRect(g); r.position->x += dx; r.position->y += dy; }
    ed.dirty = true;
    return (int)targets.size();
}

} // namespace oked::uid
