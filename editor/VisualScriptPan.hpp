#pragma once
// Panning the Visual Script canvas: which gesture pans, click versus drag, what the wheel does,
// how far the arrow keys move the view, and how far the view may be pushed away from the graph.
// Everything here is plain numbers (no ImGui), so tests/test_vs_pan.cpp can check it.
// Written from the behaviour the owner asked for (docs/status/UNFINISHED.md, "Visual Script pan"), no outside code.
// Included by VisualScriptEditor.hpp.
#include <algorithm>
#include <cmath>

namespace okay::editor::vsed {

/// A press that lasts at most this long and moves at most this far is a click, anything else is a drag.
constexpr float kVsClickSeconds = 0.30f;
constexpr float kVsClickPixels = 5.0f;

/// True when a press-and-release was a click (not a drag).
inline bool VsIsClick(float heldSeconds, float movedPixels) {
    return heldSeconds <= kVsClickSeconds && movedPixels <= kVsClickPixels;
}

/// Space released: open the add-node popup only if it was a tap. Held longer, or the mouse moved
/// while it was down (a pan), it must not open the popup.
inline bool VsSpaceOpensPopup(float heldSeconds, float mouseMovedWhileHeld) {
    return VsIsClick(heldSeconds, mouseMovedWhileHeld);
}

/// Right mouse released: open the add-node menu only for a click; a drag was a pan.
inline bool VsRightClickOpensMenu(float heldSeconds, float movedPixels) {
    return VsIsClick(heldSeconds, movedPixels);
}

enum class VsLeftDrag { Box, Pan };

/// What a left-drag on the canvas does: pans while Space is held or the Pan (hand) button is on,
/// otherwise it draws the selection box.
inline VsLeftDrag VsLeftDragMode(bool spaceHeld, bool panToggleOn) {
    return (spaceHeld || panToggleOn) ? VsLeftDrag::Pan : VsLeftDrag::Box;
}

/// Middle drag always pans, right drag pans on empty canvas once it is past the click distance.
inline bool VsMiddleDragPans() { return true; }
inline bool VsRightDragPans(float movedPixels, bool overNode) { return !overNode && movedPixels > kVsClickPixels; }

/// The view moves with the mouse: pan is in screen pixels, so the delta does not depend on zoom
/// (the content under the cursor stays under the cursor at every zoom level and density).
inline float VsPanDeltaFromMouse(float mouseDelta, float /*zoom*/) { return mouseDelta; }

struct VsWheelResult {
    float zoomSteps = 0.0f;   // + zoom in, - zoom out (each step is 12 percent)
    float panX = 0.0f;        // screen pixels to add to the view offset
    float panY = 0.0f;
};

/// Wheel and trackpad rules. `scrollPans` is the "Scroll pans (trackpad)" setting.
///   Ctrl + wheel, or a pinch the platform reports as Ctrl + wheel: always zooms.
///   Sideways scroll (wheelX): always pans sideways.
///   Plain wheel: zooms (the long-standing behaviour) unless `scrollPans`, then it pans up and down.
///   Shift + wheel: pans sideways when the wheel would zoom, so a mouse can pan without a button.
inline VsWheelResult VsWheel(float wheelX, float wheelY, bool ctrl, bool shift, bool scrollPans, float pixelsPerNotch = 48.0f) {
    VsWheelResult r;
    if (ctrl) { r.zoomSteps = wheelY; r.panX = wheelX * pixelsPerNotch; return r; }
    r.panX = wheelX * pixelsPerNotch;
    if (scrollPans) {
        if (shift) r.panX += wheelY * pixelsPerNotch; else r.panY = wheelY * pixelsPerNotch;
    } else if (shift) r.panX += wheelY * pixelsPerNotch;
    else r.zoomSteps = wheelY;
    return r;
}

/// Arrow keys: the view moves in the arrow's direction (the content moves the other way). 600 px per
/// second, 3 times that with Shift. Returns the pan offset change in screen pixels.
inline void VsKeyPan(bool left, bool right, bool up, bool down, bool shift, float dt, float& dx, float& dy) {
    const float v = 600.0f * (shift ? 3.0f : 1.0f) * dt;
    dx = ((left ? v : 0.0f) - (right ? v : 0.0f));
    dy = ((up ? v : 0.0f) - (down ? v : 0.0f));
}

/// Keep part of the graph on screen. `bx0..by1` is the graph's bounding box in screen units at zoom 1
/// (node positions already spread by the layout factor), `viewW/viewH` the canvas size. The pan offset
/// is moved only as far as needed so at least `keep` pixels of the box (or all of it if it is smaller)
/// stay inside the view. Fit always works from anywhere, since it replaces the offset outright.
inline void VsClampPan(float& panX, float& panY, float viewW, float viewH, float bx0, float by0, float bx1, float by1,
                       float zoom, float keep = 80.0f) {
    if (!(bx1 > bx0) || !(by1 > by0) || viewW <= 0.0f || viewH <= 0.0f) return;
    const float kx = std::min(keep, (bx1 - bx0) * zoom), ky = std::min(keep, (by1 - by0) * zoom);
    // box right edge on screen = panX + bx1*zoom must be >= kx; left edge panX + bx0*zoom <= viewW - kx
    panX = std::clamp(panX, kx - bx1 * zoom, viewW - kx - bx0 * zoom);
    panY = std::clamp(panY, ky - by1 * zoom, viewH - ky - by0 * zoom);
}

}  // namespace okay::editor::vsed
