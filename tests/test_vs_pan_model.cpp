// Replays mouse sequences through VsPanStep (editor/VisualScriptPanModel.hpp) and checks that the view moves.
#include "test_framework.hpp"
#include "VisualScriptPanModel.hpp"

using namespace okay::editor::vsed;

static float run(VsPanFrame f, int frames, float& panX) {
    panX = 0;
    for (int i = 0; i < frames; ++i) { f.dx = 4.0f; f.dy = 0; VsPanOut o = VsPanStep(f); if (o.pans) panX += o.dx; }
    return panX;
}
static void vs_pan_model_gestures() {
    float p;
    VsPanFrame f; f.hovered = true;
    VsPanFrame m = f; m.middleDown = m.middleDragging = true;
    CHECK(run(m, 10, p) == 40.0f);
    VsPanFrame r = f; r.rightDown = r.rightDragging = true; r.rightMaxDrag = 30.0f;
    CHECK(run(r, 10, p) == 40.0f);
    r.rightMaxDrag = 2.0f; CHECK(run(r, 10, p) == 0.0f);          // still a click
    VsPanFrame l = f; l.leftDown = l.leftDragging = true; l.pressedOnBackground = true;
    CHECK(run(l, 10, p) == 40.0f);                                  // plain left-drag on empty canvas, no Pan tool
    VsPanFrame ln = l; ln.pressedOnBackground = false; CHECK(run(ln, 10, p) == 0.0f);   // press began on a node: it moves, no pan
    VsPanFrame ls = l; ls.shift = true; CHECK(run(ls, 10, p) == 0.0f);                  // Shift: marquee
    VsPanFrame lc = l; lc.ctrl = true; CHECK(run(lc, 10, p) == 0.0f);
    VsPanFrame sp = ln; sp.space = true; CHECK(run(sp, 10, p) == 40.0f);                // Space + drag, even over a node
    VsPanFrame pt = ln; pt.panTool = true; CHECK(run(pt, 10, p) == 40.0f);
    VsPanFrame off = m; off.hovered = false; CHECK(run(off, 10, p) == 0.0f);
}
int main() { vs_pan_model_gestures(); TEST_MAIN_RESULT(); }
