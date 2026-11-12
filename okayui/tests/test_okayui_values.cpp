// OkayUI value widgets with several cells in a row (DragFloat2/3/4, DragInt2/3/4, DragFloatRange2), headless:
// synthetic mouse across frames, asserting what each cell does and what the others keep.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"

#include <cmath>
#include <limits>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {

struct Rig {
    Sim sim;
    std::function<void()> body;
    bool open = true;
    Rig() { Reset(); }
    void Frame() { sim.Frame([&] { if (UI::Begin("W", 10, 10, 500, 400, &open) && body) body(); UI::End(); }); }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void MoveTo(float x, float y) { sim.Move(x, y); Frame(); }
    // Press in a cell, scrub `dx` pixels, release.
    void Scrub(float x, float y, float dx) { MoveTo(x, y); sim.Down(); Frame(); MoveTo(x + dx, y); sim.Up(); Frame(); }
};

// The centre of cell i of n in a row of width `w` that starts at `x` (4 px gaps, see OkayUI::dragCells).
float CellX(float x, float w, int n, int i) {
    const float cw = (w - 4.0f * (float)(n - 1)) / (float)n;
    return x + (float)i * (cw + 4.0f) + cw * 0.5f;
}

void VectorTests() {
    Rig r; float v[3] = {1, 2, 3}; UI::Rect rc; bool ch = false;
    r.body = [&] { UI::PushID(1); UI::SetNextItemWidth(300); ch = UI::DragFloat3("", v, 0.5f); rc = UI::GetItemRect(); UI::PopID(); };
    r.Frames(2);
    CHECK_NEAR(rc.w, 300, 1e-3);
    CHECK(!ch && v[0] == 1 && v[1] == 2 && v[2] == 3);
    const float y = CenterY(rc);
    r.Scrub(CellX(rc.x, rc.w, 3, 1), y, 10);                       // scrub the middle cell: only it changes
    CHECK(v[0] == 1 && v[2] == 3);
    CHECK_NEAR(v[1], 7.0f, 1e-3);                                   // 10 px * 0.5
    r.Scrub(CellX(rc.x, rc.w, 3, 0), y, -4);
    CHECK_NEAR(v[0], -1.0f, 1e-3); CHECK_NEAR(v[1], 7.0f, 1e-3); CHECK(v[2] == 3);
    r.Scrub(CellX(rc.x, rc.w, 3, 2), y, 6);
    CHECK_NEAR(v[2], 6.0f, 1e-3);
    // hover alone changes nothing; a press that starts outside grabs nothing
    r.MoveTo(CellX(rc.x, rc.w, 3, 1), y); CHECK(!ch);
    const float before = v[1];
    r.Scrub(rc.x + rc.w + 40, y, -100);
    CHECK(v[1] == before);
}

void ClampAndCountTests() {
    Rig r; float v2[2] = {0, 0}, v4[4] = {0, 0, 0, 0}; UI::Rect r2, r4;
    r.body = [&] {
        UI::PushID(2); UI::SetNextItemWidth(200); UI::DragFloat2("", v2, 1.0f, -5.0f, 5.0f); r2 = UI::GetItemRect(); UI::PopID();
        UI::PushID(4); UI::SetNextItemWidth(400); UI::DragFloat4("", v4, 1.0f); r4 = UI::GetItemRect(); UI::PopID();
    };
    r.Frames(2);
    r.Scrub(CellX(r2.x, r2.w, 2, 0), CenterY(r2), 50);              // clamps at the top
    CHECK(v2[0] == 5.0f && v2[1] == 0.0f);
    r.Scrub(CellX(r2.x, r2.w, 2, 1), CenterY(r2), -50);             // and at the bottom
    CHECK(v2[1] == -5.0f);
    for (int i = 0; i < 4; ++i) r.Scrub(CellX(r4.x, r4.w, 4, i), CenterY(r4), (float)(i + 1));
    CHECK_NEAR(v4[0], 1, 1e-3); CHECK_NEAR(v4[1], 2, 1e-3); CHECK_NEAR(v4[2], 3, 1e-3); CHECK_NEAR(v4[3], 4, 1e-3);
}

void IntAndRangeTests() {
    Rig r; int iv[3] = {10, 20, 30}; float lo = 2, hi = 4; UI::Rect ri, rr;
    r.body = [&] {
        UI::PushID(3); UI::SetNextItemWidth(300); UI::DragInt3("", iv, 1.0f, 0, 100); ri = UI::GetItemRect(); UI::PopID();
        UI::PushID(5); UI::SetNextItemWidth(200); UI::DragFloatRange2("", &lo, &hi, 1.0f, 0.0f, 10.0f); rr = UI::GetItemRect(); UI::PopID();
    };
    r.Frames(2);
    r.Scrub(CellX(ri.x, ri.w, 3, 2), CenterY(ri), 7);
    CHECK(iv[0] == 10 && iv[1] == 20 && iv[2] == 37);
    r.Scrub(CellX(ri.x, ri.w, 3, 0), CenterY(ri), 500);             // clamps at 100
    CHECK(iv[0] == 100);
    // the low cell cannot pass the high one, and the high cannot go below the low
    r.Scrub(CellX(rr.x, rr.w, 2, 0), CenterY(rr), 8);
    CHECK(lo == 4.0f && hi == 4.0f);
    r.Scrub(CellX(rr.x, rr.w, 2, 1), CenterY(rr), -9);
    CHECK(lo == 4.0f && hi == 4.0f);
    r.Scrub(CellX(rr.x, rr.w, 2, 1), CenterY(rr), 5);
    CHECK(lo == 4.0f && hi == 9.0f);
}

void HostileTests() {
    Rig r; float v[3] = {0, 1, 2}; const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    UI::Rect rc;
    r.body = [&] { UI::PushID(1); UI::SetNextItemWidth(300); UI::DragFloat3("", v, nan, nan, inf); rc = UI::GetItemRect(); UI::PopID(); };
    r.Frames(2);
    r.Scrub(CellX(rc.x, rc.w, 3, 1), CenterY(rc), 30);              // a NaN speed changes nothing and does not poison the value
    CHECK(v[0] == 0 && v[1] == 1 && v[2] == 2);
    float* none = nullptr; int* noneI = nullptr; float* a = nullptr;
    r.body = [&] { UI::DragFloat3("x", none); UI::DragInt2("y", noneI); UI::DragFloatRange2("z", a, a); };
    r.Frames(3);                                                    // null pointers are refused, not dereferenced
    CHECK(true);
    r.body = [&] { UI::DragFloat4("a long label that does not fit in the window at all", v); };
    r.Frames(3);
    CHECK(v[0] == 0);
}

void SliderAndInputTests() {
    Rig r; float v[3] = {0, 0, 0}; float f = 1.0f; float rgba[4] = {0.2f, 0.4f, 0.6f, 1.0f}; UI::Rect rs, rf, rc;
    r.body = [&] {
        UI::PushID(1); UI::SetNextItemWidth(300); UI::SliderFloat3("", v, 0.0f, 10.0f); rs = UI::GetItemRect(); UI::PopID();
        UI::PushID(2); UI::SetNextItemWidth(260); UI::InputFloat("", &f, 0.25f); rf = UI::GetItemRect(); UI::PopID();
        UI::PushID(3); UI::SetNextItemWidth(400); UI::ColorEdit4("", rgba); rc = UI::GetItemRect(); UI::PopID();
    };
    r.Frames(2);
    // each slider cell takes the press at its own position and leaves the others alone
    r.MoveTo(CellX(rs.x, rs.w, 3, 1), CenterY(rs)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(v[0] == 0 && v[2] == 0);
    CHECK_NEAR(v[1], 5.0f, 0.05f);                                  // centre of the middle cell = half of 0..10
    // InputFloat: [-] and [+] change by the step
    const float btn = rf.h, field = rf.w - 2 * btn - 8.0f;
    const float minusX = rf.x + field + 4.0f + btn * 0.5f, plusX = minusX + btn + 4.0f, by = CenterY(rf);
    r.MoveTo(plusX, by); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK_NEAR(f, 1.25f, 1e-4);
    r.MoveTo(minusX, by); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    r.MoveTo(minusX, by); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK_NEAR(f, 0.75f, 1e-4);
    // ColorEdit4: the alpha slider is the fourth cell
    const float cw = rc.h, sw = rc.w - cw - 6.0f, each = (sw - 12.0f) / 4.0f;
    const float ax = rc.x + cw + 6.0f + 3.0f * (each + 4.0f) + each * 0.5f;
    r.MoveTo(ax, CenterY(rc)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK_NEAR(rgba[3], 0.5f, 0.05f);
    CHECK(rgba[0] == 0.2f && rgba[1] == 0.4f && rgba[2] == 0.6f);
}

void InputFloatHostileTests() {
    Rig r; float f = 1.0f; UI::Rect rf; const float inf = std::numeric_limits<float>::infinity();
    float step = std::numeric_limits<float>::quiet_NaN();
    r.body = [&] { UI::PushID(1); UI::SetNextItemWidth(260); UI::InputFloat("", &f, step); rf = UI::GetItemRect(); UI::PopID(); };
    r.Frames(2);
    const float btn = rf.h, field = rf.w - 2 * btn - 8.0f;
    const float plusX = rf.x + field + 4.0f + btn * 1.5f + 4.0f, by = CenterY(rf);
    r.MoveTo(plusX, by); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(f == 1.0f);                                               // a NaN step changes nothing
    step = inf; r.MoveTo(plusX, by); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(f == 1.0f);                                               // nor does an infinite one
    f = std::numeric_limits<float>::max(); step = std::numeric_limits<float>::max();
    r.MoveTo(plusX, by); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(std::isfinite(f));                                        // overflow to infinity is refused
}

void SmallWidgetTests() {
    Rig r; int arrows = 0, colors = 0; float c[4] = {1, 0, 0, 1}; UI::Rect ra, rcb, ra2, rb;
    r.body = [&] {
        if (UI::ArrowButton("left", 0)) ++arrows; ra = UI::GetItemRect();
        UI::SameLine();
        if (UI::ArrowButton("bad", 99)) ++arrows;                   // an unknown direction is drawn as left, not read out of range
        if (UI::ColorButton("col", c)) ++colors; rcb = UI::GetItemRect();
        UI::Button("a"); ra2 = UI::GetItemRect();
        UI::SameLine(); UI::NewLine();
        UI::Button("b"); rb = UI::GetItemRect();
    };
    r.Frames(2);
    CHECK(ra.w > 0 && ra.w == ra.h);                                // square
    r.MoveTo(CenterX(ra), CenterY(ra)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(arrows >= 1);
    r.MoveTo(CenterX(rcb), CenterY(rcb)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(colors >= 1);
    CHECK(rb.y > ra2.y);                                            // NewLine after SameLine starts a new row
}

}  // namespace

int main() {
    RUN_SUITE("okayui values");
    VectorTests();
    ClampAndCountTests();
    IntAndRangeTests();
    HostileTests();
    SliderAndInputTests();
    InputFloatHostileTests();
    SmallWidgetTests();
    TEST_MAIN_RESULT();
}
