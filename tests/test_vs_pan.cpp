// Visual Script canvas panning decisions (editor/VisualScriptPan.hpp): click vs drag, which gesture pans,
// wheel/trackpad rules, arrow keys and clamping.
#include "test_framework.hpp"
#include "VisualScriptPan.hpp"

#include <cmath>

using namespace okay::editor::vsed;

static void vs_pan_click_vs_drag() {
    CHECK(VsIsClick(0.1f, 1.0f));
    CHECK(!VsIsClick(0.1f, 20.0f));
    CHECK(!VsIsClick(1.0f, 0.0f));
    CHECK(VsSpaceOpensPopup(0.12f, 0.0f));      // a tap still opens the add-node popup
    CHECK(!VsSpaceOpensPopup(0.12f, 40.0f));    // a quick drag with Space is a pan
    CHECK(!VsSpaceOpensPopup(2.0f, 0.0f));      // holding Space is not a tap
    CHECK(VsRightClickOpensMenu(0.1f, 2.0f));
    CHECK(!VsRightClickOpensMenu(0.1f, 30.0f));
}

static void vs_pan_which_gesture_pans() {
    CHECK(VsLeftDragMode(false, false) == VsLeftDrag::Box);   // old two-argument rule; the canvas uses VsLeftDragModeEmpty
    CHECK(VsLeftDragMode(true, false) == VsLeftDrag::Pan);
    CHECK(VsLeftDragMode(false, true) == VsLeftDrag::Pan);
    CHECK(VsMiddleDragPans());
    CHECK(VsRightDragPans(30.0f, false));
    CHECK(!VsRightDragPans(2.0f, false));
    CHECK(!VsRightDragPans(30.0f, true));
}

static void vs_pan_delta_same_at_every_zoom() {
    for (float z : {0.28f, 1.0f, 2.2f}) CHECK(VsPanDeltaFromMouse(17.0f, z) == 17.0f);
}

static void vs_pan_wheel() {
    VsWheelResult r = VsWheel(0, 1, false, false, false);
    CHECK(r.zoomSteps > 0 && r.panX == 0 && r.panY == 0);          // plain wheel still zooms
    r = VsWheel(0, 1, true, false, true);
    CHECK(r.zoomSteps > 0 && r.panY == 0);                          // Ctrl always zooms
    r = VsWheel(0, 1, false, false, true);
    CHECK(r.zoomSteps == 0 && r.panY > 0);                          // trackpad mode: scroll pans
    r = VsWheel(2, 0, false, false, false);
    CHECK(r.zoomSteps == 0 && r.panX > 0);                          // sideways scroll always pans
    r = VsWheel(0, 1, false, true, false);
    CHECK(r.zoomSteps == 0 && r.panX > 0);                          // Shift + wheel pans sideways
}

static void vs_pan_keys() {
    float dx, dy;
    VsKeyPan(false, true, false, false, false, 0.1f, dx, dy);
    CHECK(dx < 0 && dy == 0);                                       // view moves right, content left
    float fx, fy;
    VsKeyPan(false, true, false, false, true, 0.1f, fx, fy);
    CHECK(std::fabs(fx) > std::fabs(dx) * 2.5f);                    // Shift is faster
    VsKeyPan(true, false, true, false, false, 0.1f, dx, dy);
    CHECK(dx > 0 && dy > 0);
    VsKeyPan(false, false, false, false, false, 0.1f, dx, dy);
    CHECK(dx == 0 && dy == 0);
}

static void vs_pan_clamp() {
    float px = 100000, py = -100000;
    VsClampPan(px, py, 800, 600, 0, 0, 500, 300, 1.0f);
    CHECK(px <= 800 - 80 && py >= 80 - 300);                        // part of the graph stays visible
    px = 50; py = 40;
    VsClampPan(px, py, 800, 600, 0, 0, 500, 300, 1.0f);
    CHECK(px == 50.0f); CHECK(py == 40.0f);                       // inside the range: untouched
    for (float z : {0.28f, 1.0f, 2.2f}) {                           // every zoom: still reachable
        px = -1e6f; py = 1e6f;
        VsClampPan(px, py, 800, 600, 0, 0, 500, 300, z);
        CHECK(px + 500 * z >= std::min(80.0f, 500 * z) - 0.01f);
        CHECK(py <= 600 - std::min(80.0f, 300 * z) + 0.01f);
    }
    px = 1e6f; py = 1e6f;
    VsClampPan(px, py, 800, 600, 10, 10, 10, 10, 1.0f);             // empty box: no clamp
    CHECK(px == 1e6f);
}

int main() {
    vs_pan_click_vs_drag();
    vs_pan_which_gesture_pans();
    vs_pan_delta_same_at_every_zoom();
    vs_pan_wheel();
    vs_pan_keys();
    vs_pan_clamp();
    TEST_MAIN_RESULT();
}
