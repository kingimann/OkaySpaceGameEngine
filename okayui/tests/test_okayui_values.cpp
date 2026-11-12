// OkayUI value widgets with several cells in a row (DragFloat2/3/4, DragInt2/3/4, DragFloatRange2), headless:
// synthetic mouse across frames, asserting what each cell does and what the others keep.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"

#include <cmath>
#include <cstring>
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

void HintAndFocusTests() {
    Rig r; char text[32] = ""; UI::Rect rc; const char* hint = "Search..."; bool focusHere = false; bool ch = false;
    r.body = [&] {
        UI::PushID(1); UI::SetNextItemWidth(300);
        if (focusHere) UI::SetKeyboardFocusHere();
        ch = UI::InputTextWithHint("", hint, text, (int)sizeof text);
        rc = UI::GetItemRect(); UI::PopID();
    };
    r.Frames(2);
    const unsigned field = ThemeRgba(UI::Style().field);
    auto inner = [&](const char* h) {   // text pixels in the field, away from its border
        hint = h; r.Frame();
        const auto& bmp = r.sim.Draw();
        return CountNot(bmp, (int)rc.x + 8, (int)rc.y + 4, (int)(rc.x + rc.w) - 8, (int)(rc.y + rc.h) - 4, field);
    };
    CHECK(inner("Search...") > 0);                                  // the empty field shows the placeholder
    CHECK(inner(nullptr) == 0);                                     // without one it is plain
    CHECK(text[0] == '\0' && !ch);                                  // a hint never writes into the buffer
    // SetKeyboardFocusHere: typing reaches the field without a click
    focusHere = true; r.Frame(); focusHere = false;
    r.sim.Type("hi"); r.Frame();
    CHECK(!std::strcmp(text, "hi") && ch);
    // once there is text the placeholder is gone: the pixels are the same with and without a hint
    const int withHint = inner("Search..."), without = inner(nullptr);
    CHECK(withHint == without && withHint > 0);
    // a null hint and a null buffer are refused
    r.body = [&] { UI::InputTextWithHint("a", nullptr, text, (int)sizeof text); UI::InputTextWithHint("b", "x", nullptr, 8); UI::SetKeyboardFocusHere(); };
    r.Frames(3);
    CHECK(true);
}

void WrapTests() {
    Rig r; UI::Rect before, after, afterPop;
    const char* longText = "the quick brown fox jumps over the lazy dog again and again until it is tired";
    bool wrap = false; bool pop = true;
    r.body = [&] {
        if (wrap) UI::PushTextWrapPos(120.0f);
        UI::Text(longText);
        UI::TextColored(200, 100, 50, longText);
        if (wrap && pop) UI::PopTextWrapPos();
        UI::Button("marker"); after = UI::GetItemRect();
        UI::Text(longText);                                            // after Pop: one line again
        UI::Button("marker2"); afterPop = UI::GetItemRect();
    };
    wrap = false; r.Frames(2); before = after; const float singleGap = afterPop.y - after.y;
    wrap = true; r.Frames(2);
    CHECK(after.y > before.y + 40);                                    // both texts wrapped onto several lines
    CHECK_NEAR(afterPop.y - after.y, singleGap, 1e-3);                 // the text after PopTextWrapPos is a single line again
    // a missing Pop does not leak into the next frame
    pop = false; r.Frames(2); wrap = false; r.Frames(2);
    CHECK_NEAR(after.y, before.y, 1e-3);
    // hostile widths
    r.body = [&] { UI::PushTextWrapPos(std::numeric_limits<float>::quiet_NaN()); UI::Text("a b c"); UI::PopTextWrapPos();
                   UI::PushTextWrapPos(-5.0f); UI::Text(""); UI::Text(nullptr); UI::PopTextWrapPos(); UI::PopTextWrapPos(); };
    r.Frames(3);
    CHECK(true);
}

void ItemQueryTests() {
    Rig r; char text[16] = ""; UI::Rect rt, rb, rb2; bool focT = false, focB = false, anyActive = false, visT = false;
    unsigned idT = 0, idB = 0, idB2 = 0;
    r.body = [&] {
        UI::InputText("t", text, (int)sizeof text); rt = UI::GetItemRect(); focT = UI::IsItemFocused(); idT = UI::GetItemID(); visT = UI::IsItemVisible();
        UI::Button("b1"); rb = UI::GetItemRect(); focB = UI::IsItemFocused(); idB = UI::GetItemID();
        UI::Button("b2"); rb2 = UI::GetItemRect(); idB2 = UI::GetItemID();
        anyActive = UI::IsAnyItemActive();
    };
    r.Frames(2);
    CHECK(idT != 0 && idB != 0 && idB2 != 0);
    CHECK(idT != idB && idB != idB2 && idT != idB2);                // every widget has its own id
    CHECK(!focT && !focB && !anyActive);
    CHECK(visT);
    r.MoveTo(CenterX(rt), CenterY(rt)); r.sim.Down(); r.Frame();
    CHECK(anyActive);                                               // the press is held
    r.sim.Up(); r.Frame();
    CHECK(focT && !focB);                                           // the clicked text field has the keyboard, the button after it does not
    r.MoveTo(CenterX(rb), CenterY(rb)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(!focT);                                                   // clicking elsewhere drops it
    // an item outside a small clip rectangle is not visible
    bool inside = true, outside = true;
    r.body = [&] {
        UI::PushClipRect(0, 0, 50, 50);
        UI::Button("tiny"); inside = UI::IsItemVisible();
        UI::Dummy(10, 600); UI::Button("far"); outside = UI::IsItemVisible();
        UI::PopClipRect();
    };
    r.Frames(2);
    CHECK(inside && !outside);
}

void GroupTests() {
    Rig r; UI::Rect ra, rb, rg, rc, rg2; bool hov = false;
    r.body = [&] {
        UI::BeginGroup();
        UI::Button("first"); ra = UI::GetItemRect();
        UI::Button("a much longer second button"); rb = UI::GetItemRect();
        UI::EndGroup(); rg = UI::GetItemRect(); hov = UI::IsItemHovered();
        UI::SameLine();
        UI::Button("beside"); rc = UI::GetItemRect();
        UI::BeginGroup(); UI::BeginGroup(); UI::Button("deep"); UI::EndGroup(); UI::EndGroup(); rg2 = UI::GetItemRect();
        UI::BeginGroup(); UI::EndGroup();                              // an empty group is harmless
        UI::EndGroup(); UI::EndGroup();                                // so are unmatched Ends
        UI::BeginGroup();                                              // and a missing End (cleared next frame)
    };
    r.Frames(3);
    CHECK_NEAR(rg.x, ra.x, 1e-3); CHECK_NEAR(rg.y, ra.y, 1e-3);        // the group starts where its first item starts
    CHECK(rg.w >= rb.w - 1e-3 && rg.h >= (rb.y + rb.h - ra.y) - 1e-3); // and covers both stacked buttons
    CHECK_NEAR(rc.y, rg.y, 1e-3);                                      // SameLine after a group goes next to its top
    CHECK(rc.x > rg.x + rg.w);
    CHECK(rg2.w > 0 && rg2.h > 0);                                     // nested groups report the inner item's box
    r.MoveTo(CenterX(ra), CenterY(ra));
    r.Frame(); CHECK(hov);                                             // hovering any part of the group hovers the group item
    r.MoveTo(rg.x + rg.w + 1, ra.y - 20); r.Frame();
    CHECK(!hov);
}

void TreeTests() {
    Rig r; bool a = false, b = false, leafOpen = false, everToggled = false, leafToggled = false;
    UI::Rect ra, rl; bool forceOpen = false, forceClose = false;
    r.body = [&] {
        if (forceOpen) { UI::SetNextItemOpen(true); forceOpen = false; }
        if (forceClose) { UI::SetNextItemOpen(false); forceClose = false; }
        a = UI::TreeNode("plain"); ra = UI::GetItemRect(); if (UI::IsItemToggledOpen()) everToggled = true;
        if (a) { UI::Text("child"); UI::TreePop(); }
        b = UI::TreeNodeEx("starts open", UI::TreeNode_DefaultOpen);
        if (b) {
            leafOpen = UI::TreeNodeEx("leaf", UI::TreeNode_Leaf | UI::TreeNode_Selected); rl = UI::GetItemRect();
            if (UI::IsItemToggledOpen()) leafToggled = true;
            if (leafOpen) UI::TreePop();
            UI::TreePop();
        }
    };
    r.Frames(2);
    CHECK(!a && b && leafOpen);                                      // default closed, DefaultOpen starts open, a leaf returns true
    CHECK(!everToggled);
    r.MoveTo(CenterX(ra), CenterY(ra)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(a && everToggled);                                         // a click opens it and is reported as a toggle
    r.MoveTo(CenterX(ra), CenterY(ra)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(!a);                                                       // the next click closes it
    forceOpen = true; r.Frame(); CHECK(a);                           // SetNextItemOpen forces the next node open
    forceClose = true; r.Frame(); CHECK(!a);
    r.MoveTo(CenterX(rl), CenterY(rl)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(leafOpen && !leafToggled);                                 // clicking a leaf never toggles it
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
    HintAndFocusTests();
    WrapTests();
    ItemQueryTests();
    GroupTests();
    TreeTests();
    TEST_MAIN_RESULT();
}
