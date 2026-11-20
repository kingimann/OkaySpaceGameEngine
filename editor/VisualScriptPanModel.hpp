#pragma once
// One frame of Visual Script canvas panning as plain numbers (no ImGui), so tests can replay mouse
// sequences. The editor fills VsPanFrame from ImGui each frame and applies the result.
// Written from the behaviour the owner asked for ("I still can't pan around Visual Script"), no outside code.
#include "VisualScriptPan.hpp"

namespace okay::editor::vsed {

struct VsPanFrame {
    bool hovered = false;          // the pointer is over the canvas window
    bool leftDown = false, middleDown = false, rightDown = false;
    bool leftDragging = false, middleDragging = false, rightDragging = false;   // past ImGui's drag distance
    float rightMaxDrag = 0.0f;     // farthest the right press travelled
    float dx = 0.0f, dy = 0.0f;    // mouse movement this frame
    bool pressedOnBackground = false;   // the left press began on empty canvas (not a node, pin or card)
    bool nodeDragging = false;     // a node or wire is being dragged
    bool boxing = false;           // a selection box is running
    bool space = false, panTool = false, shift = false, ctrl = false;
};

struct VsPanOut { bool pans = false; float dx = 0.0f, dy = 0.0f; bool leftPan = false; };

/// Left-drag on EMPTY canvas pans unless Shift or Ctrl is held (then it draws the selection box).
/// Space or the Pan tool pan from anywhere, as before.
inline VsLeftDrag VsLeftDragModeEmpty(bool spaceHeld, bool panToggleOn, bool shift, bool ctrl) {
    if (spaceHeld || panToggleOn) return VsLeftDrag::Pan;
    return (shift || ctrl) ? VsLeftDrag::Box : VsLeftDrag::Pan;
}

inline VsPanOut VsPanStep(const VsPanFrame& f) {
    VsPanOut o;
    if (!f.hovered) return o;
    const bool forced = f.space || f.panTool;
    const bool leftPan = !f.boxing && f.leftDown && f.leftDragging &&
        ((forced) || (f.pressedOnBackground && !f.nodeDragging &&
                      VsLeftDragModeEmpty(false, false, f.shift, f.ctrl) == VsLeftDrag::Pan));
    const bool rightPan = f.rightDown && f.rightDragging && VsRightDragPans(f.rightMaxDrag, f.nodeDragging);
    if ((f.middleDown && f.middleDragging) || rightPan || leftPan) {
        o.pans = true; o.leftPan = leftPan;
        o.dx = VsPanDeltaFromMouse(f.dx, 1.0f); o.dy = VsPanDeltaFromMouse(f.dy, 1.0f);
    }
    return o;
}

}  // namespace okay::editor::vsed
