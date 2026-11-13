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
    void Tap(Key k) { sim.Press(k); Frame(); }
    void Click(float x, float y) { sim.Move(x, y); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); }
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

void BeginComboTests() {
    Rig r; int sel = 0; bool open = false; UI::Rect rbox, r1, r2;
    r.body = [&] {
        UI::PushID(1); UI::SetNextItemWidth(220);
        open = UI::BeginCombo("", sel == 0 ? "none" : sel == 1 ? "one" : "two");
        rbox = UI::GetItemRect(); UI::PopID();
        if (open) {
            if (UI::Selectable("one", sel == 1)) { sel = 1; UI::CloseCurrentPopup(); }
            r1 = UI::GetItemRect();
            if (UI::Selectable("two", sel == 2)) { sel = 2; UI::CloseCurrentPopup(); }
            r2 = UI::GetItemRect();
            UI::EndCombo();
        }
    };
    r.Frames(2);
    CHECK(!open && sel == 0);                                       // closed until clicked
    r.MoveTo(CenterX(rbox), CenterY(rbox)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.Frame();
    CHECK(open);                                                    // a click opens the list
    CHECK(rbox.w > 100 && rbox.h > 10);                             // GetItemRect right after BeginCombo is the box, open or not
    CHECK(r1.y >= rbox.y + rbox.h - 1 && r2.y > r1.y);              // the rows sit under the box, in order
    r.MoveTo(CenterX(r2), CenterY(r2)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.Frame();
   
    CHECK(sel == 2 && !open);                                       // picking a row sets it and closes the list
    // clicking the box twice opens and then closes it
    r.MoveTo(CenterX(rbox), CenterY(rbox)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.Frame();
    CHECK(open);
    r.MoveTo(CenterX(rbox), CenterY(rbox)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.Frame();
    CHECK(!open);
    // a click outside the list closes it, without changing the value
    r.MoveTo(CenterX(rbox), CenterY(rbox)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.Frame();
    CHECK(open);
    r.MoveTo(380, 380); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.Frame();
    CHECK(!open && sel == 2);
    // a null preview is drawn as nothing
    r.body = [&] { if (UI::BeginCombo("x", nullptr)) UI::EndCombo(); };
    r.Frames(3);
    CHECK(true);
}

void TypedNumberTests() {
    Rig r; float v[3] = {1, 2, 3}; int iv[2] = {5, 6}; float sl[2] = {0.25f, 0.75f}; UI::Rect rv, ri, rs;
    r.body = [&] {
        UI::PushID(1); UI::SetNextItemWidth(300); UI::DragFloat3("", v, 0.5f, -100.0f, 100.0f); rv = UI::GetItemRect(); UI::PopID();
        UI::PushID(2); UI::SetNextItemWidth(300); UI::DragInt2("", iv, 1.0f, 0, 50); ri = UI::GetItemRect(); UI::PopID();
        UI::PushID(3); UI::SetNextItemWidth(300); UI::SliderFloat2("", sl, 0.0f, 1.0f); rs = UI::GetItemRect(); UI::PopID();
    };
    r.Frames(2);
    auto ctrlClick = [&](const UI::Rect& rc, int n, int i) {
        r.sim.ctrl = true; r.MoveTo(CellX(rc.x, rc.w, n, i), CenterY(rc)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.sim.ctrl = false;
    };
    // Ctrl+click a drag cell, type, Enter: the value is what was typed (the old text is replaced)
    ctrlClick(rv, 3, 1); r.sim.Type("42.5"); r.Frame(); r.Tap(Key_Enter); r.Frame();
    CHECK_NEAR(v[1], 42.5f, 1e-4); CHECK(v[0] == 1 && v[2] == 3);
    // clamped to the cell's range
    ctrlClick(rv, 3, 2); r.sim.Type("1000"); r.Frame(); r.Tap(Key_Enter); r.Frame();
    CHECK_NEAR(v[2], 100.0f, 1e-4);
    // Escape cancels
    ctrlClick(rv, 3, 0); r.sim.Type("77"); r.Frame(); r.Tap(Key_Escape); r.Frame();
    CHECK(v[0] == 1);
    // text that is not a number keeps the old value
    ctrlClick(rv, 3, 0); r.sim.Type("abc"); r.Frame(); r.Tap(Key_Enter); r.Frame();
    CHECK(v[0] == 1);
    // integers round and clamp
    ctrlClick(ri, 2, 0); r.sim.Type("7.6"); r.Frame(); r.Tap(Key_Enter); r.Frame();
    CHECK(iv[0] == 8 && iv[1] == 6);
    ctrlClick(ri, 2, 1); r.sim.Type("99"); r.Frame(); r.Tap(Key_Enter); r.Frame();
    CHECK(iv[1] == 50);
    // slider cells take typed values too, clamped to their range
    ctrlClick(rs, 2, 1); r.sim.Type("0.1"); r.Frame(); r.Tap(Key_Enter); r.Frame();
    CHECK_NEAR(sl[1], 0.1f, 1e-5); CHECK(sl[0] == 0.25f);
    // a plain click (no Ctrl) still scrubs and never opens the text box
    const float before = v[1];
    r.MoveTo(CellX(rv.x, rv.w, 3, 1), CenterY(rv)); r.sim.Down(); r.Frame(); r.MoveTo(CellX(rv.x, rv.w, 3, 1) + 4, CenterY(rv)); r.sim.Up(); r.Frame();
    CHECK_NEAR(v[1], before + 2.0f, 1e-3);
    // clicking away while typing commits
    ctrlClick(rv, 3, 0); r.sim.Type("9"); r.Frame();
    r.Click(450, 380);
    CHECK_NEAR(v[0], 9.0f, 1e-4);
}

void TypedSingleTests() {
    Rig r; float f = 1.0f, sf = 0.5f; int i = 3, si = 4; UI::Rect rf, rsf, ri, rsi; bool cf = false, ci = false, csf = false, csi = false;
    r.body = [&] {
        UI::PushID(1); UI::SetNextItemWidth(240); cf = UI::DragFloat("", &f, 0.5f, 0.0f, 10.0f); rf = UI::GetItemRect(); UI::PopID();
        UI::PushID(2); UI::SetNextItemWidth(240); ci = UI::DragInt("", &i, 1.0f, 0, 20); ri = UI::GetItemRect(); UI::PopID();
        UI::PushID(3); UI::SetNextItemWidth(240); csf = UI::SliderFloat("", &sf, 0.0f, 1.0f); rsf = UI::GetItemRect(); UI::PopID();
        UI::PushID(4); UI::SetNextItemWidth(240); csi = UI::SliderInt("", &si, 0, 10); rsi = UI::GetItemRect(); UI::PopID();
    };
    r.Frames(2);
    auto typeInto = [&](const UI::Rect& rc, const char* text) {
        r.sim.ctrl = true; r.MoveTo(CenterX(rc), CenterY(rc)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame(); r.sim.ctrl = false;
        r.sim.Type(text); r.Frame(); r.Tap(Key_Enter); r.Frame();
    };
    typeInto(rf, "6.25");  CHECK_NEAR(f, 6.25f, 1e-4);              // DragFloat
    typeInto(rf, "99");    CHECK_NEAR(f, 10.0f, 1e-4);              // clamped to its range
    typeInto(ri, "12.4");  CHECK(i == 12);                          // DragInt rounds
    typeInto(ri, "-5");    CHECK(i == 0);                           // and clamps
    typeInto(rsf, "0.8");  CHECK_NEAR(sf, 0.8f, 1e-4);              // SliderFloat
    typeInto(rsi, "7");    CHECK(si == 7);                          // SliderInt
    typeInto(rsi, "x");    CHECK(si == 7);                          // not a number: unchanged
    CHECK(!cf && !ci && !csf && !csi);                              // no change is reported while nothing is happening
}

void TypedInputTests() {
    Rig r; int i = 5; float f = 1.5f; UI::Rect ri, rf; bool ci = false, cf = false;
    r.body = [&] {
        UI::PushID(1); UI::SetNextItemWidth(240); ci = UI::InputInt("", &i, 1); ri = UI::GetItemRect(); UI::PopID();
        UI::PushID(2); UI::SetNextItemWidth(240); cf = UI::InputFloat("", &f, 0.5f); rf = UI::GetItemRect(); UI::PopID();
    };
    r.Frames(2);
    auto typeInto = [&](const UI::Rect& rc, const char* text) {   // a plain click on the value box (no Ctrl), then the text, then Enter
        r.MoveTo(rc.x + 20.0f, CenterY(rc)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
        r.sim.Type(text); r.Frame(); r.Tap(Key_Enter); r.Frame();
    };
    typeInto(ri, "42");     CHECK(i == 42 && ci == false);          // (the change flag is for the frame it happens in)
    typeInto(ri, "-7.6");   CHECK(i == -8);                         // rounds
    typeInto(ri, "x");      CHECK(i == -8);                         // not a number: unchanged
    typeInto(ri, "99999999999"); CHECK(i == 2147483647);            // clamped to the int range, no overflow
    typeInto(rf, "3.25");   CHECK_NEAR(f, 3.25f, 1e-4);
    typeInto(rf, "-0.5");   CHECK_NEAR(f, -0.5f, 1e-4);
    typeInto(rf, "nan");    CHECK_NEAR(f, -0.5f, 1e-4);             // not finite: unchanged
    typeInto(rf, "1e50");   CHECK_NEAR(f, -0.5f, 1e-4);             // does not fit a float: unchanged
    // the steppers still work after typing
    i = 10; r.Frames(2);
    r.Click(ri.x + ri.w - 2.0f * ri.h - 20.0f + ri.h * 0.5f, CenterY(ri));   // the "-" button is left of the "+" button at the right end
    CHECK(i == 10 || i == 9 || i == 11);                            // a stepper moved it by one at most (the exact button is layout-dependent)
    // while typing is open the value is not drawn twice: the box shows the edit text, and Escape cancels
    f = 2.0f; r.Frames(2);
    r.MoveTo(rf.x + 20.0f, CenterY(rf)); r.sim.Down(); r.Frame(); r.sim.Up(); r.Frame();
    r.sim.Type("9"); r.Frame(); r.Tap(Key_Escape); r.Frame(); r.Frame();
    CHECK_NEAR(f, 2.0f, 1e-4);
}

void MenuTests() {
    Rig r; UI::Rect bar; bool a = false, b = false, c = false, d = false, open = false;
    r.body = [&] {
        UI::BeginMenuBar(); bar = UI::GetItemRect();
        open = UI::BeginMenu("File");
        if (open) {
            if (UI::MenuItem("New", "Ctrl+N")) a = true;
            if (UI::MenuItem("Save", "Ctrl+S", false, false)) b = true;      // disabled
            if (UI::MenuItem("Auto", nullptr, true)) c = true;               // checked
            UI::MenuSeparator();
            if (UI::MenuItem("Quit")) d = true;
            UI::EndMenu();
        }
        UI::EndMenuBar();
    };
    r.Frames(2);
    CHECK(!open && bar.h > 0);
    const float h = bar.h, rowY0 = bar.y + h;                         // the list starts under the bar, rows are one bar height
    auto openMenu = [&] { r.Click(bar.x + 10, bar.y + h * 0.5f); r.Frame(); };
    auto clickRow = [&](int row, float extra) { r.Click(bar.x + 30, rowY0 + (float)row * h + extra + h * 0.5f); r.Frame(); r.Frame(); };
    openMenu(); CHECK(open);
    clickRow(0, 0);  CHECK(a && !open);                               // a normal item with a shortcut works and closes the menu
    openMenu(); CHECK(open);
    clickRow(1, 0);  CHECK(!b && open);                               // a disabled item does nothing and keeps the menu open
    clickRow(2, 0);  CHECK(c && !open);                               // a checked item still clicks
    openMenu(); CHECK(open);
    clickRow(3, 8.0f - h + h);                                        // below the 8 px separator: "Quit" is row 3 plus the separator's height
    CHECK(d);
    // the shortcut text is drawn (pixels in the row beyond the label) for an enabled item, dimmer for a disabled one
    openMenu();
    const auto& bmp = r.sim.Draw();
    const unsigned panel = ThemeRgba(UI::Style().panel);
    CHECK(CountNot(bmp, (int)(bar.x + 120), (int)rowY0 + 2, (int)(bar.x + 176), (int)(rowY0 + h) - 2, panel) > 0);
    // null label and a missing BeginMenu are harmless
    r.body = [&] { UI::MenuItem(nullptr); UI::MenuItem("x", nullptr); UI::MenuSeparator(); };
    r.Frames(2);
    CHECK(true);
}

void MenuKeyTests() {
    Rig r; UI::Rect bar; bool fileOpen = false, subOpen = false; int picked = 0;
    r.body = [&] {
        UI::BeginMenuBar(); bar = UI::GetItemRect();
        fileOpen = UI::BeginMenu("File");
        if (fileOpen) {
            if (UI::MenuItem("New")) picked = 1;
            if (UI::MenuItem("Off", nullptr, false, false)) picked = 99;        // disabled: the highlight skips it
            subOpen = UI::BeginMenu("Recent");
            if (subOpen) { if (UI::MenuItem("One")) picked = 3; if (UI::MenuItem("Two")) picked = 4; UI::EndMenu(); }
            if (UI::MenuItem("Quit")) picked = 5;
            UI::EndMenu();
        } else subOpen = false;
        UI::EndMenuBar();
    };
    r.Frames(2);
    const float h = bar.h;
    auto openFile = [&] { r.Click(bar.x + 10, bar.y + h * 0.5f); r.sim.Move(650, 470); r.Frames(2); };   // open it, then park the pointer far away
    auto key = [&](Key k) { r.Tap(k); r.Frame(); };
    openFile(); CHECK(fileOpen);
    key(Key_Down);  key(Key_Enter);                                           // first Down highlights "New", Enter runs it
    CHECK(picked == 1 && !fileOpen);
    picked = 0; openFile();
    key(Key_Down); key(Key_Down); key(Key_Down);                              // New, (Off skipped), Recent, Quit
    key(Key_Enter);
    CHECK(picked == 5 && !fileOpen);                                          // landed on "Quit": the disabled row was skipped
    picked = 0; openFile();
    key(Key_Up);                                                              // Up from nothing wraps to the last enabled item
    key(Key_Enter);
    CHECK(picked == 5);
    // Right opens the highlighted sub-menu, Down moves inside it, Enter runs and closes everything, Left closes just the sub-menu
    picked = 0; openFile();
    key(Key_Down); key(Key_Down);                                             // on "Recent"
    key(Key_Right);
    CHECK(fileOpen && subOpen);
    key(Key_Down); key(Key_Down);                                             // "One", then "Two" (the highlight lives in the sub-menu now)
    key(Key_Left);
    CHECK(fileOpen && !subOpen);
    key(Key_Right); CHECK(subOpen);
    key(Key_Down); key(Key_Enter);
    CHECK(picked == 3 && !fileOpen && !subOpen);                              // "One" ran and the whole chain closed
    // Escape closes the innermost menu first
    openFile(); key(Key_Down); key(Key_Down); key(Key_Right); CHECK(subOpen);
    key(Key_Escape); CHECK(fileOpen && !subOpen);
    key(Key_Escape); CHECK(!fileOpen);
    // keys do nothing while no menu is open, and Enter with no highlight runs nothing
    picked = 0; key(Key_Down); key(Key_Enter); CHECK(picked == 0 && !fileOpen);
    openFile(); key(Key_Enter); CHECK(picked == 0 && fileOpen);
}

void SubMenuTests() {
    Rig r; UI::Rect bar; bool fileOpen = false, subOpen = false, subSubOpen = false, a = false, b = false, deep = false;
    r.body = [&] {
        UI::BeginMenuBar(); bar = UI::GetItemRect();
        fileOpen = UI::BeginMenu("File");
        if (fileOpen) {
            if (UI::MenuItem("New")) a = true;
            subOpen = UI::BeginMenu("Recent");
            if (subOpen) {
                if (UI::MenuItem("One")) b = true;
                subSubOpen = UI::BeginMenu("More");
                if (subSubOpen) { if (UI::MenuItem("Deep")) deep = true; UI::EndMenu(); }
                UI::EndMenu();
            } else subSubOpen = false;
            UI::MenuItem("Quit");
            UI::EndMenu();
        } else subOpen = subSubOpen = false;
        UI::EndMenuBar();
    };
    r.Frames(2);
    const float h = bar.h, listX = bar.x, listW = 180.0f, rowY0 = bar.y + h;
    auto rowMid = [&](int row) { return rowY0 + (float)row * h + h * 0.5f; };
    r.Click(bar.x + 10, bar.y + h * 0.5f); r.Frame();
    CHECK(fileOpen && !subOpen);                                         // the sub-menu is closed until the row is pointed at
    r.MoveTo(listX + 30, rowMid(1)); r.Frame();
    CHECK(fileOpen && subOpen);                                          // hovering the "Recent" row opens it
    r.MoveTo(listX + listW + 30, rowMid(1)); r.Frame();
    CHECK(subOpen && !subSubOpen);                                       // moving into its list keeps it open
    r.MoveTo(listX + listW + 30, rowMid(2)); r.Frame();                  // onto the "More" row (second row of the sub-list, starts at the Recent row)
    CHECK(subOpen && subSubOpen);                                        // a second level opens
    r.MoveTo(listX + 2 * listW + 30, rowMid(2)); r.Frame();
    CHECK(subSubOpen);
    r.Click(listX + 2 * listW + 30, rowMid(2)); r.Frame(); r.Frame();
    CHECK(deep && !fileOpen && !subOpen && !subSubOpen);                 // picking a deep item runs it and closes every level
    // leaving the sub-menu closes just it
    deep = false; r.Click(bar.x + 10, bar.y + h * 0.5f); r.Frame();
    r.MoveTo(listX + 30, rowMid(1)); r.Frame(); CHECK(subOpen);
    r.MoveTo(listX + 30, rowMid(0)); r.Frame(); r.Frame();               // back onto "New"
    CHECK(fileOpen && !subOpen);
    // a press elsewhere closes everything
    r.MoveTo(listX + 30, rowMid(1)); r.Frame();
    r.Click(400, 300); r.Frame();
    CHECK(!fileOpen && !subOpen);
    // a plain item next to the sub-menu still works, and the sub-menu row itself does not click through
    r.Click(bar.x + 10, bar.y + h * 0.5f); r.Frame();
    r.Click(listX + 30, rowMid(0)); r.Frame(); r.Frame();
    CHECK(a && !fileOpen);
    // BeginMenu outside any menu or bar, with a null label, is harmless
    r.body = [&] { CHECK(!UI::BeginMenu(nullptr)); };
    r.Frames(2);
}

void TableTests() {
    Rig r; UI::Rect c0, c1, c2, c1b, hdrProbe; int rowSeen = -9, colSeen = -9; bool bad = false; bool noResize = false;
    r.body = [&] {
        const UI::TableFlags fl = noResize ? (UI::Table_RowBg | UI::Table_BordersInnerH | UI::Table_NoResize) : (UI::Table_RowBg | UI::Table_BordersInnerH);
        if (UI::BeginTable("t", 3, fl)) {
            UI::TableSetupColumn("Name", UI::Col_WidthFixed, 100.0f);
            UI::TableSetupColumn("Value", 0, 1.0f);
            UI::TableSetupColumn("Note", 0, 3.0f);
            UI::TableSetupColumn("ignored: more than the columns");
            UI::TableHeadersRow();
            UI::TableNextRow();
            UI::TableSetColumnIndex(2); UI::Text("n"); c2 = UI::GetItemRect();      // out of order: the last column first
            UI::TableSetColumnIndex(0); UI::Text("a"); c0 = UI::GetItemRect();
            colSeen = UI::TableGetColumnIndex(); rowSeen = UI::TableGetRowIndex();
            UI::TableSetColumnIndex(1); UI::Text("b"); c1 = UI::GetItemRect();
            if (UI::TableSetColumnIndex(3) || UI::TableSetColumnIndex(-1)) bad = true;   // no such column
            UI::TableNextRow(); UI::TableNextColumn(); UI::Text("second"); UI::TableNextColumn(); UI::Text("b2"); c1b = UI::GetItemRect();
            UI::EndTable();
        }
        if (UI::TableGetRowIndex() != -1 || UI::TableGetColumnIndex() != -1) bad = true;   // outside a table
        UI::TableSetupColumn("x"); UI::TableHeadersRow(); UI::TableNextRow(); UI::EndTable();   // calls outside a table are harmless
    };
    r.Frames(3);
    CHECK(!bad && colSeen == 0 && rowSeen == 0);
    CHECK(c0.x < c1.x && c1.x < c2.x);                                       // cells land in their own columns, even set out of order
    CHECK(c1.x - c0.x > 95.0f && c1.x - c0.x < 105.0f);                      // the fixed column keeps about 100 px
    const float w1 = c2.x - c1.x;                                           // "Value" has weight 1 and "Note" weight 3: Note is about three times as wide
    CHECK(std::fabs(w1 - 95.0f) < 2.0f);                                     // (480 - 100) shared 1:3 gives 95 px for Value (and 285 for Note)
    CHECK(c1b.y > c1.y);                                                     // the second row is below the first
    // the border between Value and Note can be dragged unless Table_NoResize
    const float borderX = c2.x - 4.0f, my = c1.y + 2.0f;
    r.sim.Move(borderX, my); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Move(borderX + 30.0f, my); r.Frame(); r.Frame(); r.sim.Up(); r.Frame(); r.Frames(2);
    CHECK(c2.x > c1.x + w1 + 10.0f);                                         // Value grew
    // NoResize: the same drag changes nothing
    noResize = true; r.Frames(2);
    const float before = c2.x;
    r.sim.Move(c2.x - 4.0f, my); r.Frame(); r.sim.Down(); r.Frame(); r.sim.Move(c2.x + 26.0f, my); r.Frame(); r.Frame(); r.sim.Up(); r.Frame(); r.Frames(2);
    CHECK(std::fabs(c2.x - before) < 0.5f);
    // a banded row draws a faint band: a pixel in the second row's empty right side differs from one in the first row
    r.Frames(2);
    const auto& bmp = r.sim.Draw();
    const int px = (int)(c2.x + 150), y0 = (int)(c1.y + 3.0f), y1 = (int)(c1b.y + 3.0f);
    if (px < (int)bmp.w) CHECK(PixelAt(bmp, px, y0) != PixelAt(bmp, px, y1));
}

void ModalTests() {
    Rig r; int behind = 0; bool ok = false, shown = false, flag = true; UI::Rect rOpen, rBehind, rOk;
    r.body = [&] {
        if (UI::Button("open")) UI::OpenPopup("dlg"); rOpen = UI::GetItemRect();
        if (UI::Button("behind")) ++behind; rBehind = UI::GetItemRect();
        shown = UI::BeginPopupModal("dlg", &flag);
        if (shown) {
            UI::Text("are you sure?");
            if (UI::Button("OK")) { ok = true; UI::CloseCurrentPopup(); }
            rOk = UI::GetItemRect();
            UI::EndPopup();
        }
    };
    r.Frames(2);
    CHECK(!shown && !UI::IsPopupOpen("dlg"));
    r.Frame(); const unsigned plain = PixelAt(r.sim.Draw(), 15, 400);   // a window pixel away from the dialog, before it opens
    r.Click(CenterX(rOpen), CenterY(rOpen)); r.Frames(3);
    CHECK(shown && UI::IsPopupOpen("dlg"));
    {   // the area behind is dimmed: every colour channel of that pixel got darker
        const unsigned dimmed = PixelAt(r.sim.Draw(), 15, 400);
        CHECK(dimmed != plain);
        CHECK(((dimmed >> 24) & 255) < ((plain >> 24) & 255) && ((dimmed >> 16) & 255) < ((plain >> 16) & 255) && ((dimmed >> 8) & 255) < ((plain >> 8) & 255));
    }
    // centred in the 640x480 display
    CHECK(rOk.x > 640 * 0.5f - 200 && rOk.x < 640 * 0.5f + 200 && rOk.y > 100 && rOk.y < 380);
    // widgets behind are inert while it is open
    r.Click(CenterX(rBehind), CenterY(rBehind)); r.Frames(2);
    CHECK(behind == 0 && shown);
    // a click outside the dialog does not close it
    r.Click(600, 440); r.Frames(2);
    CHECK(shown);
    // OK runs, closes it, and the widgets behind work again
    r.Click(CenterX(rOk), CenterY(rOk)); r.Frames(3);
    CHECK(ok && !shown && !UI::IsPopupOpen("dlg"));
    r.Click(CenterX(rBehind), CenterY(rBehind)); r.Frames(2);
    CHECK(behind == 1);
    // Escape closes it too
    r.Click(CenterX(rOpen), CenterY(rOpen)); r.Frames(3); CHECK(shown);
    r.Tap(Key_Escape); r.Frames(2);
    CHECK(!shown && !flag);
    // a null name is refused
    r.body = [&] { if (UI::BeginPopupModal(nullptr)) UI::EndPopup(); UI::IsPopupOpen(nullptr); };
    r.Frames(2);
    CHECK(true);
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
    BeginComboTests();
    TypedNumberTests();
    TypedSingleTests();
    TypedInputTests();
    MenuTests();
    SubMenuTests();
    MenuKeyTests();
    TableTests();
    ModalTests();
    TEST_MAIN_RESULT();
}
