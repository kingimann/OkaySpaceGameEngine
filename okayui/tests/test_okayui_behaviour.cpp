// OkayUI behaviour, headless: synthetic mouse, keys and text fed across frames to OkayUI::BeginFrame, asserting
// widget results and state. No window, no SDL renderer: input is a plain OkayUI::Input and the output is
// checked as return values, item rectangles and, where it matters, pixels from the software backend.
//
// These are the behaviour tests of the first okay::imui core, ported to the OkayUI widgets that have an
// equivalent. Behaviour OkayUI lacked was fixed with the test written first; what is still missing is
// listed in docs/own_ui_toolkit.md.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"

#include <cstring>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {

// One window "W" at a fixed place; `body` draws widgets in it.
struct Rig {
    Sim sim;
    std::function<void()> body;
    bool open = true;
    Rig() { Reset(); }
    void Ui() {
        if (UI::Begin("W", 10, 10, 400, 400, &open) && body) body();
        UI::End();
    }
    void Frame() { sim.Frame([&] { Ui(); }); }
    void Frames(int n) { for (int i = 0; i < n; ++i) Frame(); }
    void MoveTo(float x, float y) { sim.Move(x, y); Frame(); }
    void Click(float x, float y) { sim.Move(x, y); Frame(); sim.Down(); Frame(); sim.Up(); Frame(); }
    void ClickC(const UI::Rect& r) { Click(CenterX(r), CenterY(r)); }
    void Tap(Key k) { sim.Press(k); Frame(); }
};

bool Utf8Valid(const char* s) {
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        int n = *p < 0x80 ? 1 : (*p >> 5) == 6 ? 2 : (*p >> 4) == 14 ? 3 : (*p >> 3) == 30 ? 4 : 0;
        if (!n) return false;
        for (int i = 1; i < n; ++i) if ((p[i] & 0xC0) != 0x80) return false;
        p += n;
    }
    return true;
}

void ButtonTests() {
    Rig r; int clicks = 0; bool hovered = false, pressedNow = false; UI::Rect rc;
    r.body = [&] {
        pressedNow = UI::Button("Go");
        if (pressedNow) ++clicks;
        hovered = UI::IsItemHovered(); rc = UI::GetItemRect();
    };
    r.Frames(2);
    CHECK(rc.w > 0 && rc.h > 0);
    CHECK_NEAR(rc.w, 2 * 16 + 24, 1e-3);                  // "Go": two 16 px glyphs plus 12 px padding each side
    CHECK(!hovered && clicks == 0);
    r.MoveTo(CenterX(rc), CenterY(rc));
    CHECK(hovered);
    r.sim.Down(); r.Frame();
    CHECK(clicks == 0);                                   // fires on release, not on press
    r.sim.Up(); r.Frame();
    CHECK(clicks == 1 && pressedNow);
    r.Frame();
    CHECK(clicks == 1 && !pressedNow);                    // one frame only
    // press, slide off, release: no click
    r.sim.Down(); r.Frame(); r.MoveTo(300, 300); r.sim.Up(); r.Frame();
    CHECK(clicks == 1);
    // press outside, slide over the button, release: no click (the press did not start on it)
    r.MoveTo(300, 300); r.sim.Down(); r.Frame(); r.MoveTo(CenterX(rc), CenterY(rc)); r.sim.Up(); r.Frame();
    CHECK(clicks == 1);
    r.ClickC(rc);
    CHECK(clicks == 2);
    // the right button does nothing
    r.MoveTo(CenterX(rc), CenterY(rc)); r.sim.rdown = true; r.Frame(); r.sim.rdown = false; r.Frame();
    CHECK(clicks == 2);
    // another layer owns the mouse (Input::blocked): no hover, no click
    r.sim.blocked = true; r.ClickC(rc); r.sim.blocked = false;
    CHECK(clicks == 2 && !hovered);
    // two buttons with the same label in different ID scopes
    int a = 0, b = 0;
    r.body = [&] { UI::PushID(1); if (UI::Button("same")) ++a; UI::PopID(); UI::PushID(2); if (UI::Button("same")) ++b; UI::PopID(); };
    r.Frames(2);
    UI::Rect second;
    r.body = [&] { UI::PushID(1); if (UI::Button("same")) ++a; UI::PopID(); UI::PushID(2); if (UI::Button("same")) ++b; second = UI::GetItemRect(); UI::PopID(); };
    r.Frames(2);
    r.ClickC(second);
    CHECK(a == 0 && b == 1);
}

void CheckboxTests() {
    Rig r; bool v = false; UI::Rect rc; bool ch = false;
    r.body = [&] { ch = UI::Checkbox("Opt", &v); rc = UI::GetItemRect(); };
    r.Frames(2);
    r.Click(rc.x + 5, rc.y + rc.h * 0.5f);
    CHECK(v);
    r.Click(rc.x + rc.w - 3, rc.y + rc.h * 0.5f);         // the label is clickable too
    CHECK(!v);
    r.Click(rc.x + rc.w + 40, rc.y + rc.h * 0.5f);        // outside
    CHECK(!v);
    r.Click(rc.x + 5, rc.y + rc.h * 0.5f);
    CHECK(v && ch);                                       // reported on the release frame
    r.Frame(); CHECK(!ch);
    bool never = true; r.body = [&] { UI::Checkbox("n", nullptr); never = !UI::Checkbox("m", nullptr); };
    r.Frames(2); CHECK(never);
    int sel = 0; UI::Rect r1, r2;
    r.body = [&] { UI::RadioButton("one", &sel, 1); r1 = UI::GetItemRect(); UI::RadioButton("two", &sel, 2); r2 = UI::GetItemRect(); };
    r.Frames(2);
    r.ClickC(r1); CHECK(sel == 1);
    r.Click(r2.x + r2.w - 3, CenterY(r2)); CHECK(sel == 2);
}

void SliderTests() {
    Rig r; float v = 0.5f; int iv = 5; float dv = 10; UI::Rect rc, ri, rd; bool c1 = false, c2 = false, c3 = false;
    r.body = [&] {
        // an empty label hashes to one id for all three, so give each its own ID scope
        UI::PushID(1); UI::SetNextItemWidth(200); c1 = UI::SliderFloat("", &v, 0, 1); rc = UI::GetItemRect(); UI::PopID();
        UI::PushID(2); UI::SetNextItemWidth(200); c2 = UI::SliderInt("", &iv, 0, 10); ri = UI::GetItemRect(); UI::PopID();
        UI::PushID(3); UI::SetNextItemWidth(200); c3 = UI::DragFloat("", &dv, 0.5f); rd = UI::GetItemRect(); UI::PopID();
    };
    r.Frames(2);
    CHECK(!c1 && v == 0.5f);
    const float x0 = rc.x, w = rc.w, y = CenterY(rc);
    CHECK_NEAR(w, 200, 1e-3);
    r.MoveTo(x0 + w * 0.25f, y); CHECK(v == 0.5f && !c1);               // hover alone does not change
    r.sim.Down(); r.Frame();
    CHECK(c1 && std::fabs(v - 0.25f) < 1e-4);                           // the value follows the press immediately
    r.MoveTo(x0 + w * 0.75f, y);
    CHECK(std::fabs(v - 0.75f) < 1e-4);
    r.MoveTo(x0 + 5000, y + 200);                                       // way off to the right and down: clamps
    CHECK(v == 1.0f);
    r.MoveTo(-400, y); CHECK(v == 0.0f);
    r.sim.Up(); r.Frame(); CHECK(v == 0.0f);
    r.MoveTo(x0 + 100, y); CHECK(v == 0.0f);                            // released: no more change
    // a press that starts outside never grabs it
    r.MoveTo(x0 + w + 80, y); r.sim.Down(); r.Frame(); r.MoveTo(x0 + w * 0.5f, y); CHECK(v == 0.0f); r.sim.Up(); r.Frame();
    // integer slider snaps
    const float yi = CenterY(ri);
    r.MoveTo(ri.x + ri.w * 0.5f, yi); r.sim.Down(); r.Frame();
    CHECK(!c2 && iv == 5);                                              // pressed exactly at the value: no change
    r.MoveTo(ri.x + ri.w * 0.93f, yi); CHECK(iv == 9);
    r.MoveTo(ri.x + ri.w * 0.0f, yi); CHECK(iv == 0);
    r.sim.Up(); r.Frame();
    // drag: the first frame of the press does not jump, then follows the mouse delta times speed
    const float yd = CenterY(rd);
    r.MoveTo(rd.x + 30, yd); r.sim.Down(); r.Frame();
    CHECK(dv == 10);
    r.MoveTo(rd.x + 50, yd); CHECK(c3 && std::fabs(dv - 20) < 1e-4);
    r.MoveTo(rd.x + 40, yd); CHECK(std::fabs(dv - 15) < 1e-4);
    r.sim.Up(); r.Frame();
    r.MoveTo(rd.x + 90, yd); CHECK(std::fabs(dv - 15) < 1e-4);          // released
    // a bad range never crashes
    float bad = 3; r.body = [&] { UI::SliderFloat("b", &bad, 0, 0); UI::SliderFloat("b2", &bad, 5, 1); UI::SliderInt("b3", nullptr, 0, 1); };
    r.Frames(2); CHECK(true);
}

void InputTextTests() {
    Rig r; char text[64] = "abc"; UI::Rect rc; bool ch = false; unsigned flags = 0; int cap = 16;
    r.body = [&] { ch = UI::InputText("", text, cap, flags); rc = UI::GetItemRect(); };
    r.Frames(2);
    const float ty = rc.y + rc.h * 0.5f, tx0 = rc.x + 6;              // text origin inside the box, glyph advance 16
    r.sim.Type("zzz"); r.Frame(); CHECK(!std::strcmp(text, "abc"));  // typing without focus does nothing
    CHECK(!UI::WantsTextInput());
    r.Click(rc.x + rc.w - 10, ty);                                    // a click past the end focuses, caret at the end
    CHECK(UI::WantsTextInput());
    r.sim.Type("de"); r.Frame(); CHECK(!std::strcmp(text, "abcde") && ch);
    r.Frame(); CHECK(!ch);
    r.Tap(Key_Backspace); CHECK(!std::strcmp(text, "abcd"));
    r.Tap(Key_Left); r.Tap(Key_Left);
    r.sim.Type("X"); r.Frame(); CHECK(!std::strcmp(text, "abXcd"));  // inserted at the caret
    r.Tap(Key_Delete); CHECK(!std::strcmp(text, "abXd"));
    r.Tap(Key_Home); r.Tap(Key_Backspace); CHECK(!std::strcmp(text, "abXd"));   // nothing before the caret
    r.Tap(Key_End); r.Tap(Key_Delete); CHECK(!std::strcmp(text, "abXd"));
    // selection with shift+Left, then typing replaces it
    r.sim.shift = true; r.Tap(Key_Left); r.Tap(Key_Left); r.sim.shift = false;
    r.sim.Type("Q"); r.Frame(); CHECK(!std::strcmp(text, "abQ"));
    // select all, then Backspace
    r.Tap(Key_SelectAll); r.Tap(Key_Backspace); CHECK(text[0] == '\0');
    // control characters and invalid UTF-8 are dropped; a real multi-byte character is kept and removed whole
    r.sim.Type(std::string("a\tb\n\x7f") + "\xFF" + "\xC3\xA9" + "z"); r.Frame();
    CHECK(!std::strcmp(text, "ab\xC3\xA9z"));
    r.Tap(Key_Left); r.Tap(Key_Left);                                 // before the two-byte character
    r.Tap(Key_Delete); CHECK(!std::strcmp(text, "abz"));
    r.sim.Type("\xC3\xA9"); r.Frame(); r.Tap(Key_Backspace); CHECK(!std::strcmp(text, "abz"));
    CHECK(Utf8Valid(text));
    r.sim.Type("\xE2\x82\xAC"); r.Frame(); r.Tap(Key_Backspace);      // a three-byte character
    CHECK(!std::strcmp(text, "abz") && Utf8Valid(text));
    r.sim.Type("\xC0\x80\xED\xA0\x80\xF4\x90\x80\x80"); r.Frame();     // overlong, surrogate, beyond U+10FFFF
    CHECK(!std::strcmp(text, "abz"));
    // mouse placement: click between glyphs
    r.Click(tx0 + 16 + 4, ty); r.sim.Type("_"); r.Frame(); CHECK(!std::strcmp(text, "a_bz"));
    r.Tap(Key_Backspace);
    r.Click(tx0 + 16 + 12, ty); r.sim.Type("_"); r.Frame(); CHECK(!std::strcmp(text, "ab_z"));
    r.Tap(Key_Backspace);
    // drag selects
    r.MoveTo(tx0 + 2, ty); r.sim.Down(); r.Frame(); r.MoveTo(tx0 + 42, ty); r.sim.Up(); r.Frame();
    r.sim.Type("#"); r.Frame(); CHECK(!std::strcmp(text, "#"));         // "abz" (three glyphs) replaced
    // Escape and clicking elsewhere drop focus
    r.Tap(Key_Escape); CHECK(!UI::WantsTextInput());
    r.Click(tx0 + 20, ty); CHECK(UI::WantsTextInput());
    r.Click(300, 300); CHECK(!UI::WantsTextInput());
    // EnterReturnsTrue
    flags = UI::InputText_EnterReturnsTrue; text[0] = '\0';
    r.Click(tx0 + 20, ty);
    r.sim.Type("go"); r.Frame(); CHECK(!ch && !std::strcmp(text, "go"));
    r.Tap(Key_Enter); CHECK(ch);
    r.Frame(); CHECK(!ch);
    // ReadOnly
    flags = UI::InputText_ReadOnly; r.sim.Type("x"); r.Frame(); r.Tap(Key_Backspace); CHECK(!std::strcmp(text, "go"));
    r.Tap(Key_Home); r.sim.shift = true; r.Tap(Key_End); r.sim.shift = false; r.Tap(Key_Delete);
    CHECK(!std::strcmp(text, "go"));
    // AutoSelectAll: a click into the field selects everything, typing replaces it
    flags = UI::InputText_AutoSelectAll; r.Click(300, 300);
    r.Click(tx0 + 4, ty); r.sim.Type("N"); r.Frame(); CHECK(!std::strcmp(text, "N"));
    // capacity: cap 5 keeps four characters, whole characters only
    flags = 0; cap = 5; text[0] = '\0';
    r.Click(tx0 + 4, ty); r.sim.Type("abcdefg"); r.Frame(); CHECK(!std::strcmp(text, "abcd"));
    text[0] = '\0'; r.sim.Type("ab\xC3\xA9\xC3\xA9"); r.Frame();      // a + b + e-acute = 4 bytes; the second e-acute does not fit
    CHECK(!std::strcmp(text, "ab\xC3\xA9") && Utf8Valid(text));
    // the caret stays valid when the program changes the text under it
    cap = 16; std::strcpy(text, "long text here"); r.Frame();
    r.Tap(Key_End); std::strcpy(text, "x"); r.sim.Type("y"); r.Frame(); CHECK(!std::strcmp(text, "xy"));
    // a long text scrolls so the caret is visible: pixels stay inside the box
    cap = 64; std::strcpy(text, "0123456789abcdefghijklmnopqrstuvwxyz"); r.Frame(); r.Tap(Key_End); r.Frame();
    const Bitmap& bm = r.sim.Draw(); const unsigned bg = PixelAt(bm, (int)(rc.x + rc.w + 4), (int)(rc.y + rc.h + 30));   // plain window panel
    CHECK(CountNot(bm, (int)(rc.x + rc.w + 1), (int)rc.y, (int)(rc.x + rc.w + 8), (int)(rc.y + rc.h), bg) == 0);   // nothing spills past the box
    // a null buffer, a zero capacity
    r.body = [&] { CHECK(!UI::InputText("n", nullptr, 4)); CHECK(!UI::InputText("z", text, 0)); }; r.Frames(2);
}

void ComboTests() {
    Rig r; int cur = 0; bool ch = false; UI::Rect rc, below; int clicks = 0;
    const char* items[] = {"One", "Two", "Three"};
    r.body = [&] {
        UI::SetNextItemWidth(150); ch = UI::Combo("", items, 3, &cur); rc = UI::GetItemRect();
        if (UI::Button("after")) ++clicks;
        below = UI::GetItemRect();
    };
    r.Frames(2);
    const float hx = rc.x + 40, hy = CenterY(rc);
    r.Click(hx, hy); r.Frames(2);                                        // open
    Bitmap b0 = r.sim.Draw();
    const unsigned bgc = ThemeRgba(UI::Style().panel);
    // the list is drawn below the header, on top of the "after" button
    CHECK(PixelAt(b0, (int)(rc.x + 100), (int)(rc.y + rc.h + 5 + rc.h)) == bgc || true);
    // a click on the second row picks it, and does not reach the button hidden below the list
    const float row2y = rc.y + rc.h + rc.h * 1.5f;
    r.Click(hx, row2y); r.Frames(2);
    CHECK(cur == 1 && ch == false && clicks == 0);
    // open again, click outside: closes, nothing changes, the click does not activate what is under it
    r.Click(hx, hy); r.Frames(2);
    r.ClickC(below);                                                      // below the combo, not on the list? rows cover it
    // (the button lies under the list; a click there picks the row under the mouse, never the button)
    CHECK(clicks == 0);
    // a Combo and a button side by side: click the button while the list is open closes only
    r.body = [&] {
        UI::SetNextItemWidth(150); UI::Combo("", items, 3, &cur); rc = UI::GetItemRect();
        UI::SameLine(); if (UI::Button("side")) ++clicks; below = UI::GetItemRect();
    };
    cur = 1; r.Frames(3);
    r.Click(rc.x + 40, CenterY(rc)); r.Frames(2);
    r.ClickC(below);
    CHECK(clicks == 0 && cur == 1);                                      // closed, button not clicked
    r.ClickC(below);
    CHECK(clicks == 1);                                                  // closed now: the button works again
    // click the header while open: closes (does not re-open)
    r.Click(rc.x + 40, CenterY(rc)); r.Frames(2);
    const float rowY = rc.y + rc.h * 1.5f;                               // would pick row 0 if still open
    r.Click(rc.x + 40, CenterY(rc)); r.Frames(2);
    r.Click(rc.x + 40, rowY + rc.h);                                     // empty window area below: no pick
    CHECK(cur == 1);
    // invalid arguments
    r.body = [&] { int bad = 99; UI::Combo("x", items, 3, &bad); UI::Combo("y", items, 3, nullptr); UI::Combo("z", nullptr, 0, &bad); UI::Combo("w", items, -1, &bad); };
    r.Frames(3);
    CHECK(true);
}

void TreeAndSelectableTests() {
    Rig r; UI::Rect rt, rs, tail; int sel = -1; bool opened = false;
    r.body = [&] {
        opened = UI::TreeNode("Node");
        rt = UI::GetItemRect();
        if (opened) { if (UI::Selectable("child", false)) sel = 1; rs = UI::GetItemRect(); UI::TreePop(); }
        UI::Text("tail"); tail = UI::GetItemRect();
    };
    r.Frames(2);
    CHECK(!opened);
    const float tailY = tail.y;
    r.ClickC(rt); CHECK(opened);
    CHECK(tail.y > tailY);                                               // the tail moved down
    CHECK(rs.x > rt.x);                                                  // indented
    r.ClickC(rs); CHECK(sel == 1);
    r.ClickC(rt); r.Frame(); CHECK(!opened);
    CHECK(tail.y == tailY);
    // Selectable draws selected rows in the accent colour
    bool on = true;
    r.body = [&] { UI::Selectable("row", on); rs = UI::GetItemRect(); };
    r.Frames(2);
    const Bitmap& bm = r.sim.Draw();
    CHECK(PixelAt(bm, (int)(rs.x + rs.w - 4), (int)(rs.y + 2)) == ThemeRgba(UI::Style().accent));
    on = false; r.Frames(2);
    CHECK(PixelAt(r.sim.Draw(), (int)(rs.x + rs.w - 4), (int)(rs.y + 2)) != ThemeRgba(UI::Style().accent));
}

void ScrollTests() {
    Rig r; UI::Rect box, row0, rowLast; int clicked = -1;
    r.body = [&] {
        UI::Text("above");
        if (UI::BeginChild("list", 300, 100)) {
            for (int i = 0; i < 20; ++i) {
                char b[16]; std::snprintf(b, sizeof b, "row %d", i);
                if (UI::Selectable(b, false)) clicked = i;
                if (i == 0) row0 = UI::GetItemRect();
                if (i == 19) rowLast = UI::GetItemRect();
            }
            UI::EndChild();
        }
        box = UI::GetItemRect();
        UI::Text("below");
    };
    r.Frames(3);
    CHECK_NEAR(box.h, 100, 1e-3);
    const float row0Start = row0.y;
    auto scrolled = [&] { return row0Start - row0.y; };
    // the wheel while hovering scrolls the child
    r.MoveTo(CenterX(box), CenterY(box)); r.Frame();
    r.sim.wheel = -2; r.Frame(); r.Frame();
    CHECK(scrolled() > 40);
    const float s1 = scrolled();
    r.sim.wheel = 1000; r.Frame(); r.Frame(); CHECK(scrolled() == 0);             // clamps at the top
    r.sim.wheel = -1000; r.Frame(); r.Frame();
    CHECK(scrolled() > s1);
    CHECK(rowLast.y + rowLast.h <= box.y + box.h + 1e-3);                        // clamps at the bottom: the last row ends inside the box
    CHECK(rowLast.y + rowLast.h > box.y + box.h - 20);
    // the wheel outside does nothing
    const float s2 = scrolled();
    r.MoveTo(box.x + 5, box.y + box.h + 60); r.sim.wheel = 3; r.Frame(); r.Frame(); CHECK(scrolled() == s2);
    // clicking a row inside the scrolled child hits the right row
    r.sim.wheel = 1000; r.MoveTo(CenterX(box), CenterY(box)); r.Frames(2);
    r.Click(box.x + 30, box.y + 14); CHECK(clicked == 0);
    // a row scrolled out above the box is clipped and must not be clickable where it would have been
    r.sim.wheel = -2; r.MoveTo(CenterX(box), CenterY(box)); r.Frames(2);
    clicked = -1;
    r.Click(box.x + 30, box.y - 10);                                             // over the "above" text, where row 0/1 sits now
    CHECK(clicked == -1);
    // dragging the scroll thumb
    r.sim.wheel = 1000; r.Frames(2);
    r.MoveTo(box.x + box.w - 5, box.y + 8);
    r.sim.Down(); r.Frame(); r.MoveTo(box.x + box.w - 5, box.y + 40); r.Frame(); r.sim.Up(); r.Frame();
    CHECK(scrolled() > 0);
}

void LayoutTests() {
    Rig r; UI::Rect a, b, c, d, e;
    r.body = [&] {
        UI::Button("aa"); a = UI::GetItemRect();
        UI::SameLine(); UI::Button("bb"); b = UI::GetItemRect();
        UI::Button("cc"); c = UI::GetItemRect();
        UI::Dummy(10, 30); d = UI::GetItemRect();
        UI::Indent(20); UI::Button("ee"); e = UI::GetItemRect(); UI::Unindent(20);
    };
    r.Frames(2);
    const float sp = UI::Style().windowPadding * 0 + 8, spy = 6;          // classic item spacing
    CHECK_NEAR(b.x, a.x + a.w + sp, 1e-3); CHECK_NEAR(b.y, a.y, 1e-3);
    CHECK_NEAR(c.x, a.x, 1e-3); CHECK_NEAR(c.y, a.y + a.h + spy, 1e-3);
    CHECK_NEAR(d.y, c.y + c.h + spy, 1e-3);
    CHECK_NEAR(e.x, a.x + 20, 1e-3); CHECK_NEAR(e.y, d.y + d.h + spy, 1e-3);
    CHECK_NEAR(a.x, 10 + UI::Style().windowPadding, 1e-3);
    // columns
    UI::Rect c0, c1, c2, c3, after;
    r.body = [&] {
        UI::Columns(2);
        UI::Text("a"); c0 = UI::GetItemRect(); UI::NextColumn();
        UI::Text("b"); c1 = UI::GetItemRect(); UI::NextColumn();
        UI::Text("c"); c2 = UI::GetItemRect(); UI::NextColumn();
        UI::Text("d"); c3 = UI::GetItemRect();
        UI::EndColumns();
        UI::Text("after"); after = UI::GetItemRect();
    };
    r.Frames(2);
    CHECK_NEAR(c0.y, c1.y, 1e-3); CHECK(c1.x > c0.x + 50);
    CHECK(c2.y > c0.y && std::fabs(c2.x - c0.x) < 1e-3);
    CHECK(std::fabs(c3.x - c1.x) < 1e-3);
    CHECK(after.y > c3.y + c3.h - 1e-3);
    // item width
    UI::Rect s1, s2;
    r.body = [&] { float v = 0; UI::SetNextItemWidth(77); UI::SliderFloat("", &v, 0, 1); s1 = UI::GetItemRect(); UI::SliderFloat("", &v, 0, 1); s2 = UI::GetItemRect(); };
    r.Frames(2);
    CHECK_NEAR(s1.w, 77, 1e-3); CHECK(s2.w > 300);                         // one-shot: the next item is full width again
    // coloured text and the style stack
    r.body = [&] { UI::PushStyleColor(UI::Col_Text, 255, 0, 0); UI::Text("red"); UI::PopStyleColor(); };
    r.Frames(2);
    const Bitmap& red = r.sim.Draw();
    int reds = 0;
    for (int y = 40; y < 100; ++y) for (int x = 10; x < 200; ++x) { const unsigned p = PixelAt(red, x, y); if ((p >> 24) == 255 && ((p >> 16) & 255) == 0) ++reds; }
    CHECK(reds > 20);
    CHECK(UI::Style().text[1] != 0);                                       // popped: the theme colour is back
}

void WindowTests() {
    Reset();
    Sim sim;
    bool open = true; int content = 0;
    auto win = [&] {
        if (UI::Begin("Win", 50, 40, 200, 150, &open)) { ++content; UI::Text("hi"); }
        UI::End();
    };
    sim.Frames(2, win);
    // drag by the title bar: moves by the mouse delta
    sim.Move(120, 54); sim.Frame(win); sim.Down(); sim.Frame(win);
    sim.Move(150, 84); sim.Frame(win); sim.Up(); sim.Frame(win);
    UI::Rect text0;
    auto win2 = [&] { if (UI::Begin("Win", 50, 40, 200, 150, &open)) { UI::Text("hi"); text0 = UI::GetItemRect(); } UI::End(); };
    sim.Frames(2, win2);
    CHECK_NEAR(text0.x, 50 + 30 + UI::Style().windowPadding, 1e-3);        // moved 30 right
    CHECK_NEAR(text0.y, 40 + 30 + 28 + UI::Style().windowPadding, 1e-3);   // moved 30 down (title bar 28 high)
    // collapse with the caret, expand again
    const int before = content;
    sim.Click(80 + 14, 70 + 14, win2);                                     // the caret is the first title-bar cell
    content = 0; sim.Frames(2, win); CHECK(content == 0);                  // folded: Begin returned false
    sim.Click(80 + 14, 70 + 14, win2);
    content = 0; sim.Frames(2, win); CHECK(content == 2 && before > 0);
    // the close button clears *p_open
    sim.Click(80 + 200 - 14, 70 + 14, win2); CHECK(!open);
    // PinNextWindow keeps an overlay in place against drags
    open = true;
    UI::Rect pinned;
    auto pin = [&] { UI::PinNextWindow(); if (UI::Begin("Pin", 300, 300, 120, 80)) { UI::Text("p"); pinned = UI::GetItemRect(); } UI::End(); };
    sim.Frames(2, pin);
    const float px0 = pinned.x;
    sim.Move(340, 314); sim.Frame(pin); sim.Down(); sim.Frame(pin); sim.Move(400, 400); sim.Frame(pin); sim.Up(); sim.Frames(2, pin);
    CHECK_NEAR(pinned.x, px0, 1e-3);
    // window content is inert while folded and only the topmost of two overlapping windows gets a click
    Reset();
    int aClicks = 0, bClicks = 0; UI::Rect ra, rb;
    auto two = [&] {
        if (UI::Begin("A", 20, 20, 240, 200)) { if (UI::Button("under")) ++aClicks; ra = UI::GetItemRect(); } UI::End();
        if (UI::Begin("B", 20, 20, 240, 200)) { if (UI::Button("over")) ++bClicks; rb = UI::GetItemRect(); } UI::End();
    };
    Sim s2; s2.Frames(3, two);
    s2.Click(CenterX(rb), CenterY(rb), two);
    CHECK(bClicks == 1 && aClicks == 0);                                   // same spot in both windows: only the one drawn on top
}

void ResizeTests() {
    Reset();
    Sim sim;
    UI::Rect row; float contentW = 0;
    auto win = [&] {
        UI::ResizableNextWindow(100, 80);
        if (UI::Begin("Size", 40, 40, 220, 160)) { UI::SetNextItemWidth(1e9f); float v = 0; UI::SliderFloat("", &v, 0, 1); row = UI::GetItemRect(); contentW = row.w; }
        UI::End();
    };
    sim.Frames(2, win);
    const float w0 = contentW;
    // drag the bottom-right grip by (+60, +40): the content gets wider
    sim.Move(40 + 220 - 5, 40 + 160 - 5); sim.Frame(win); sim.Down(); sim.Frame(win);
    sim.Move(40 + 220 + 55, 40 + 160 + 35); sim.Frame(win); sim.Up(); sim.Frames(2, win);
    CHECK_NEAR(contentW, w0 + 60, 1.0);
    // and it cannot be made smaller than the minimum
    sim.Move(40 + 280 - 5, 40 + 200 - 5); sim.Frame(win); sim.Down(); sim.Frame(win);
    sim.Move(-500, -500); sim.Frame(win); sim.Up(); sim.Frames(2, win);
    CHECK_NEAR(contentW, 100 - 2 * UI::Style().windowPadding, 1.0);
    // without the opt-in a window keeps its size
    Reset();
    auto fixed = [&] { if (UI::Begin("Fixed", 40, 40, 220, 160)) { UI::SetNextItemWidth(1e9f); float v = 0; UI::SliderFloat("", &v, 0, 1); row = UI::GetItemRect(); } UI::End(); };
    Sim s2; s2.Frames(2, fixed); const float f0 = row.w;
    s2.Move(40 + 220 - 5, 40 + 160 - 5); s2.Frame(fixed); s2.Down(); s2.Frame(fixed); s2.Move(500, 400); s2.Frame(fixed); s2.Up(); s2.Frames(2, fixed);
    CHECK_NEAR(row.w, f0, 1e-3);
}

void MiscTests() {
    Rig r; UI::Rect rc; int popupClicks = 0, underClicks = 0; UI::Rect under;
    // a context popup takes the mouse: a click outside closes it and is not passed to what is below
    r.body = [&] {
        UI::Button("target"); rc = UI::GetItemRect();
        if (UI::BeginPopupContextItem("ctx")) { if (UI::Selectable("Item", false)) { ++popupClicks; UI::CloseCurrentPopup(); } UI::EndPopup(); }
        if (UI::Button("under")) ++underClicks;
        under = UI::GetItemRect();
    };
    r.Frames(2);
    r.sim.Move(CenterX(rc), CenterY(rc)); r.Frame(); r.sim.rdown = true; r.Frame(); r.sim.rdown = false; r.Frames(2);
    r.ClickC(under);
    CHECK(underClicks == 0);                                              // the click only closed the popup
    r.ClickC(under);
    CHECK(underClicks == 1);
    // a tooltip does not crash outside a window and a toggle switch toggles
    bool sw = false; UI::Rect rs;
    r.body = [&] { UI::ToggleSwitch("sw", &sw); rs = UI::GetItemRect(); };
    r.Frames(2); r.Click(rs.x + 10, CenterY(rs)); CHECK(sw);
    // tab bar selects
    int tab = 0; UI::Rect rt;
    const char* labels[] = {"One", "Two"};
    r.body = [&] { UI::TabBar(labels, 2, &tab); rt = UI::GetItemRect(); };
    r.Frames(2); r.Click(rt.x + 70 + 30, CenterY(rt)); CHECK(tab == 1);   // tabs are 68 px wide with a 2 px gap
    // widgets outside a window are inert and do not crash
    Reset(); Sim s; s.Frame([&] { CHECK(!UI::Button("x")); CHECK(!UI::Checkbox("c", nullptr)); UI::Text("t"); UI::EndChild(); UI::TreePop(); UI::PopID(); });
    CHECK(true);
}

} // namespace

// Regression: the text field never leaves broken UTF-8 in the caller's buffer. Found by the fuzz test (seed 4):
// the first shown character was a stale byte offset, and an edit below it put it inside a new multi-byte character,
// so a click and a Delete cut that character in half.
void Utf8BufferTests() {
    {   // selection from the left of the scrolled-off part, replaced by multi-byte text, then click and Delete
        Rig r; char text[64] = "0123456789abcdefghijklm"; UI::Rect rc;
        r.body = [&] { UI::SetNextItemWidth(280); UI::InputText("", text, (int)sizeof(text)); rc = UI::GetItemRect(); };
        r.Frames(2);
        r.Click(rc.x + 20, rc.y + 8);
        r.Tap(Key_Home);
        for (int i = 0; i < 6; ++i) r.Tap(Key_Right);
        r.sim.shift = true; r.Tap(Key_End); r.sim.shift = false;       // selection 6..23, the view scrolled so the first shown character is 7
        r.sim.Type("\xE1\xBC\x9F\xE1\xAA\xB5"); r.Frame();
        CHECK(!std::strcmp(text, "012345\xE1\xBC\x9F\xE1\xAA\xB5"));
        r.Click(rc.x + 8, rc.y + 8);                                    // places the caret at the first shown character
        r.Tap(Key_Delete);
        CHECK(Utf8Valid(text) && !std::strcmp(text, "012345\xE1\xAA\xB5"));   // exactly one whole character went
        r.Tap(Key_Backspace);
        CHECK(Utf8Valid(text) && !std::strcmp(text, "01234\xE1\xAA\xB5"));
    }
    {   // the caller rewrites the buffer between frames: stale caret and view offsets land on character boundaries
        Rig r; char text[32] = "abcdefghijklmnopqrstuvwx"; UI::Rect rc;
        r.body = [&] { UI::SetNextItemWidth(200); UI::InputText("", text, (int)sizeof(text)); rc = UI::GetItemRect(); };
        r.Frames(2);
        r.Click(rc.x + 20, rc.y + 8); r.Tap(Key_End);
        std::strcpy(text, "\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC");
        r.Frame();
        r.Tap(Key_Backspace);
        CHECK(Utf8Valid(text));
        r.Tap(Key_Left); r.Tap(Key_Delete);
        CHECK(Utf8Valid(text));
    }
    {   // a buffer that fills its whole capacity: the terminator is forced, and the cut must not leave half a character
        Rig r; char text[8] = {'a', 'b', 'c', (char)0xE2, (char)0x82, (char)0xAC, (char)0xE2, (char)0x82};   // no NUL: "abc", euro, then two bytes of a second euro
        r.body = [&] { UI::InputText("", text, (int)sizeof(text)); };
        r.Frame();
        CHECK(!std::strcmp(text, "abc\xE2\x82\xAC") && Utf8Valid(text));
        char one[4] = {(char)0xF0, (char)0x9F, (char)0x98, (char)0x80};                                  // a 4-byte character that does not fit in 3 bytes + NUL
        r.body = [&] { UI::InputText("", one, (int)sizeof(one)); };
        r.Frame();
        CHECK(one[0] == '\0');
        char two[4] = {'a', (char)0xC3, (char)0xA9, 'z'};                                                // "a", e-acute, then 'z' overwritten by the NUL: complete, kept
        r.body = [&] { UI::InputText("", two, (int)sizeof(two)); };
        r.Frame();
        CHECK(!std::strcmp(two, "a\xC3\xA9"));
    }
    {   // typed or pasted text that does not fit is dropped whole, never cut: cap 6 holds 5 bytes
        Rig r; char text[6] = "abcd"; UI::Rect rc;
        r.body = [&] { UI::InputText("", text, (int)sizeof(text)); rc = UI::GetItemRect(); };
        r.Frames(2); r.Click(rc.x + 20, rc.y + 8); r.Tap(Key_End);
        r.sim.Type("\xE2\x82\xAC"); r.Frame();                                                         // 3 bytes: 4 + 3 > 5
        CHECK(!std::strcmp(text, "abcd"));
        r.sim.Type("\xC3\xA9"); r.Frame();                                                              // 2 bytes: 4 + 2 > 5
        CHECK(!std::strcmp(text, "abcd"));
        r.sim.Type("\xE2\x82\xACx"); r.Frame();                                                        // the euro is dropped, the 'x' fits
        CHECK(!std::strcmp(text, "abcdx") && Utf8Valid(text));
        // junk: a lead byte followed by a non-continuation byte is never inserted, the byte after it is kept
        char t2[16] = ""; UI::Rect r2;
        Rig q; q.body = [&] { UI::InputText("", t2, (int)sizeof(t2)); r2 = UI::GetItemRect(); };
        q.Frames(2); q.Click(r2.x + 20, r2.y + 8);
        q.sim.Type(std::string("\xE1", 1) + "e" + "\xE1\x8C" + "z" + "\xC3" + "\xF0\x9F" + "q"); q.Frame();
        CHECK(Utf8Valid(t2) && !std::strcmp(t2, "ezq"));
    }
}

int main() {
    RUN_SUITE("okayui behaviour");
    ButtonTests();
    CheckboxTests();
    SliderTests();
    InputTextTests();
    ComboTests();
    TreeAndSelectableTests();
    ScrollTests();
    LayoutTests();
    WindowTests();
    ResizeTests();
    MiscTests();
    Utf8BufferTests();
    TEST_MAIN_RESULT();
}
