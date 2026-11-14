// Gap step 32: the calls the launcher and editor use that OkayUI lacked. Password and filtered text fields, edit tracking
// (IsItemEdited / IsItemDeactivatedAfterEdit), ImGui child flags (borders, padding, auto height), hover and focus flags with delays,
// the middle mouse button, GetStyle, tooltips made of widgets, context popups on a window, SetNextWindowFocus / BgAlpha / SetWindowPos,
// GetMouseDragDelta, ListClipper, rounded rectangles, polygons and textured quads in the draw list, column NoResize.
// Headless, through the same frame driver as the other OkayUI tests; nothing here needs a window.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUICodeEdit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {

// One bare window at (10, 10), 400 x 400, around `body`.
void Win(const std::function<void()>& body) {
    UI::BareNextWindow();
    if (UI::Begin("w", 10, 10, 400, 400)) body();
    UI::End();
}

bool SameBitmap(const Bitmap& a, const Bitmap& b) {
    if (a.w != b.w || a.h != b.h) return false;
    return a.rgba == b.rgba;
}

int CountColour(const Bitmap& b, unsigned colour) {
    int n = 0;
    for (int y = 0; y < b.h; ++y) for (int x = 0; x < b.w; ++x) if (PixelAt(b, x, y) == colour) ++n;
    return n;
}

const unsigned kRed = Rgba(255, 0, 0);
const UI::Color32 kRedPacked = UI::RGBA(255, 0, 0);

} // namespace

int main() {
    RUN_SUITE("okayui gaps");

    {   // key aliases and flag values that must match Dear ImGui's spelling
        CHECK(UI::KeyId_UpArrow == UI::KeyId_Up && UI::KeyId_DownArrow == UI::KeyId_Down);
        CHECK(UI::KeyId_LeftArrow == UI::KeyId_Left && UI::KeyId_RightArrow == UI::KeyId_Right);
        CHECK(UI::KeyId_KeypadEnter == UI::KeyId_Enter);
        CHECK(UI::MouseButton_Left == 0 && UI::MouseButton_Right == 1 && UI::MouseButton_Middle == 2);
        CHECK(UI::DrawFlags_Closed == 1 && UI::DrawFlags_RoundCornersTop == 48 && UI::DrawFlags_RoundCornersAll == 240);
        CHECK(UI::Col_WidthStretch == 0 && UI::Col_NoResize != 0);
        CHECK(UI::SortDirection_Ascending == 1 && UI::SortDirection_Descending == 2);
    }

    {   // character filters and uppercase
        Sim sim(500, 200);
        char buf[32] = "";
        const unsigned flagsList[] = { UI::InputText_CharsDecimal, UI::InputText_CharsHexadecimal, UI::InputText_CharsNoBlank, UI::InputText_CharsUppercase };
        const char* typed[] = { "a1b2.c-3 ", "xyz9fG", "a b\tc d", "abc d" };
        const char* want[]  = { "12.-3",     "9f",     "abcd",    "ABC D" };
        for (int k = 0; k < 4; ++k) {
            Reset(); buf[0] = 0;
            UI::Rect r;
            auto ui = [&] { Win([&] { UI::InputText("##f", buf, sizeof buf, flagsList[k]); r = UI::GetItemRect(); }); };
            sim.Frame(ui);
            sim.Click(CenterX(r), CenterY(r), ui);
            sim.Type(typed[k]); sim.Frame(ui);
            CHECK(std::strcmp(buf, want[k]) == 0);
        }
        // a pasted string is filtered the same way
        Reset(); buf[0] = 0;
        UI::Rect r;
        auto ui = [&] { Win([&] { UI::InputText("##f", buf, sizeof buf, UI::InputText_CharsDecimal); r = UI::GetItemRect(); }); };
        sim.Frame(ui);
        sim.Click(CenterX(r), CenterY(r), ui);
        UI::SetClipboardText("12ab34");
        sim.Tap(Key_Paste, ui);
        CHECK(std::strcmp(buf, "1234") == 0);
    }

    {   // password: the text is kept, the picture shows no letters, copy and cut give nothing
        Reset();
        Sim sim(500, 200);
        char pw[32] = "", plain[32] = "";
        UI::Rect rp, rt;
        auto ui = [&] { Win([&] {
            UI::InputText("##pw", pw, sizeof pw, UI::InputText_Password); rp = UI::GetItemRect();
            UI::InputText("##plain", plain, sizeof plain); rt = UI::GetItemRect();
        }); };
        sim.Frame(ui);
        sim.Click(CenterX(rp), CenterY(rp), ui);
        sim.Type("secret"); sim.Frame(ui);
        CHECK(std::strcmp(pw, "secret") == 0);
        const Bitmap pwShot = sim.Draw();
        sim.Click(CenterX(rt), CenterY(rt), ui);
        sim.Type("secret"); sim.Frame(ui);
        CHECK(std::strcmp(plain, "secret") == 0);
        const Bitmap plainShot = sim.Draw();
        // compare the two field rectangles: same text, different pixels (dots against letters)
        int diff = 0;
        for (int dy = 0; dy < (int)rp.h; ++dy) for (int dx = 0; dx < (int)rp.w; ++dx)
            if (PixelAt(pwShot, (int)rp.x + dx, (int)rp.y + dy) != PixelAt(plainShot, (int)rt.x + dx, (int)rt.y + dy)) ++diff;
        CHECK(diff > 20);
        // the dots take the place of the letters: a longer password lights more of the field
        const unsigned fieldBg = PixelAt(pwShot, (int)rp.x + 6, (int)rp.y + 4);
        const int litBefore = CountNot(pwShot, (int)rp.x + 4, (int)rp.y + 4, (int)(rp.x + rp.w) - 4, (int)(rp.y + rp.h) - 4, fieldBg);
        sim.Click(CenterX(rp), CenterY(rp), ui);
        sim.Type("secret2"); sim.Frame(ui);
        const int litAfter = CountNot(sim.Draw(), (int)rp.x + 4, (int)rp.y + 4, (int)(rp.x + rp.w) - 4, (int)(rp.y + rp.h) - 4, fieldBg);
        CHECK(litAfter > litBefore);
        // clipboard: select all and copy in a password field leaves the clipboard alone; in the plain field it is filled
        std::strcpy(pw, "secret2");                                  // (the caller may rewrite the buffer between frames)
        UI::SetClipboardText("keep");
        sim.Click(CenterX(rp), CenterY(rp), ui);
        sim.Tap(Key_SelectAll, ui); sim.Tap(Key_Copy, ui);
        CHECK(std::strcmp(UI::GetClipboardText(), "keep") == 0);
        sim.Tap(Key_SelectAll, ui); sim.Tap(Key_Cut, ui);
        CHECK(std::strcmp(pw, "secret2") == 0);                   // and cut does not remove it either
        sim.Click(CenterX(rt), CenterY(rt), ui);
        std::strcpy(plain, "secret");
        sim.Tap(Key_SelectAll, ui); sim.Tap(Key_Copy, ui);
        CHECK(std::strcmp(UI::GetClipboardText(), "secret") == 0);
        // editing keys still work on the real text
        sim.Click(CenterX(rp), CenterY(rp), ui);
        sim.Tap(Key_Home, ui);
        sim.Type("X"); sim.Frame(ui);
        CHECK(std::strcmp(pw, "Xsecret2") == 0);
        sim.Tap(Key_End, ui); sim.Tap(Key_Backspace, ui);
        CHECK(std::strcmp(pw, "Xsecret") == 0);
    }

    {   // IsItemEdited / IsItemDeactivatedAfterEdit on a text field
        Reset();
        Sim sim(500, 200);
        char buf[32] = "";
        UI::Rect r; bool edited = false, dae = false;
        auto ui = [&] { Win([&] { UI::InputText("##f", buf, sizeof buf); r = UI::GetItemRect(); edited = UI::IsItemEdited(); dae = UI::IsItemDeactivatedAfterEdit(); }); };
        sim.Frame(ui);
        CHECK(!edited && !dae);
        sim.Click(CenterX(r), CenterY(r), ui);
        CHECK(!edited && !dae);                                   // focusing is not editing
        sim.Type("ab"); sim.Frame(ui);
        CHECK(edited && !dae);
        sim.Frame(ui);
        CHECK(!edited && !dae);                                   // edited on that call only
        sim.Tap(Key_Enter, ui);
        CHECK(!edited && dae);                                    // Enter ends the edit
        sim.Frame(ui);
        CHECK(!dae);
        // typing, then clicking elsewhere
        sim.Click(CenterX(r), CenterY(r), ui);
        sim.Type("c"); sim.Frame(ui);
        CHECK(edited);
        sim.Move(300, 300); sim.Frame(ui); sim.Down(); sim.Frame(ui);
        CHECK(dae);
        sim.Up(); sim.Frames(2, ui);
        // focus and leave without typing: not an edit
        sim.Click(CenterX(r), CenterY(r), ui);
        sim.Tap(Key_Enter, ui);
        CHECK(!dae);
        sim.Click(CenterX(r), CenterY(r), ui);
        sim.Move(300, 300); sim.Frame(ui); sim.Down(); sim.Frame(ui);
        CHECK(!dae);
        sim.Up(); sim.Frames(2, ui);
        // typing, Escape
        sim.Click(CenterX(r), CenterY(r), ui);
        sim.Type("d"); sim.Frame(ui);
        sim.Tap(Key_Escape, ui);
        CHECK(dae);
    }

    {   // IsItemEdited / IsItemDeactivatedAfterEdit on a check box, a slider and a typed number
        Reset();
        Sim sim(500, 300);
        bool on = false; float v = 0.5f;
        UI::Rect rc, rs; bool cEdited = false, cDae = false, sEdited = false, sDae = false;
        auto ui = [&] { Win([&] {
            UI::Checkbox("chk", &on); rc = UI::GetItemRect(); cEdited = UI::IsItemEdited(); cDae = UI::IsItemDeactivatedAfterEdit();
            UI::SliderFloat("sl", &v, 0.0f, 1.0f); rs = UI::GetItemRect(); sEdited = UI::IsItemEdited(); sDae = UI::IsItemDeactivatedAfterEdit();
        }); };
        sim.Frame(ui);
        sim.Move(rc.x + 8, CenterY(rc)); sim.Frame(ui);
        sim.Down(); sim.Frame(ui);
        CHECK(!cEdited && !cDae);                                 // pressed, not yet clicked
        sim.Up(); sim.Frame(ui);
        CHECK(on && cEdited && cDae);                             // toggled and let go on the same frame
        sim.Frame(ui);
        CHECK(!cEdited && !cDae);
        // slider: edits while dragging, deactivated-after-edit only on the release frame
        sim.Move(rs.x + 20, CenterY(rs)); sim.Frame(ui);
        sim.Down(); sim.Frame(ui);
        int edits = 0, daeFrames = 0;
        for (int i = 0; i < 6; ++i) { sim.Move(rs.x + 40 + i * 12, CenterY(rs)); sim.Frame(ui); if (sEdited) ++edits; if (sDae) ++daeFrames; }
        CHECK(edits >= 4 && daeFrames == 0);
        sim.Up(); sim.Frame(ui);
        CHECK(sDae);
        sim.Frame(ui);
        CHECK(!sDae && !sEdited);
        // a drag field with a typed number: Ctrl+click, type, Enter
        float d = 1.0f; UI::Rect rd; bool dEdited = false, dDae = false, anyEdited = false, anyDae = false;
        auto ui2 = [&] { Win([&] {
            UI::DragFloat("drag", &d, 0.1f, -100.0f, 100.0f); rd = UI::GetItemRect(); dEdited = UI::IsItemEdited(); dDae = UI::IsItemDeactivatedAfterEdit();
            anyEdited = anyEdited || dEdited; anyDae = anyDae || dDae;
        }); };
        sim.Frame(ui2);
        sim.ctrl = true; sim.Click(rd.x + 20, CenterY(rd), ui2); sim.ctrl = false;
        sim.Type("42"); sim.Frame(ui2);
        anyEdited = anyDae = false;
        sim.Tap(Key_Enter, ui2);
        CHECK(std::fabs(d - 42.0f) < 1e-3f);
        CHECK(anyEdited && anyDae);
    }

    {   // child flags: border, padding, auto height, negative size
        Reset();
        Sim sim(600, 500);
        UI::Rect cr, after; float innerX = 0, y0 = 0, y1 = 0, sp = 0; int lines = 2; int flags = 0; UI::Vec2 size{0, 0};
        auto ui = [&] { Win([&] {
            sp = UI::GetStyle().ItemSpacing.y;
            UI::BeginChild("c", size, flags);
            innerX = UI::GetCursorScreenPos().x; y0 = UI::GetCursorScreenPos().y;
            for (int i = 0; i < lines; ++i) { char t[32]; std::snprintf(t, sizeof t, "line %d", i); UI::Text(t); }
            y1 = UI::GetCursorScreenPos().y;
            UI::EndChild();
            cr = UI::GetItemRect();
            UI::Button("after"); after = UI::GetItemRect();
        }); };
        size = UI::Vec2{0, 120};
        flags = UI::Child_None;
        sim.Frames(3, ui);
        CHECK(innerX == cr.x);                                    // no border, no padding: content at the edge
        CHECK(cr.h == 120);
        flags = UI::Child_Borders;
        sim.Frames(3, ui);
        CHECK(innerX == cr.x + 6.0f);                             // the frame brings the padding
        flags = UI::Child_AlwaysUseWindowPadding;
        sim.Frames(3, ui);
        CHECK(innerX == cr.x + 6.0f);
        // auto height: as tall as two lines plus the padding
        flags = UI::Child_Borders | UI::Child_AutoResizeY; size = UI::Vec2{0, 0};
        sim.Frames(4, ui);
        const float contentH = y1 - y0 - sp;
        CHECK(std::fabs(cr.h - (contentH + 12.0f)) < 0.51f);
        CHECK(after.y >= cr.y + cr.h);                            // the next item sits below it
        CHECK(after.y - (cr.y + cr.h) < 12.0f);
        const float h2 = cr.h;
        lines = 5; sim.Frames(4, ui);
        CHECK(cr.h > h2 + 20.0f);                                 // more lines, taller child
        lines = 1; sim.Frames(4, ui);
        CHECK(cr.h < h2);                                         // fewer, shorter
        // a given height is the least an auto-sized child gets
        lines = 1; size = UI::Vec2{0, 200}; sim.Frames(4, ui);
        CHECK(cr.h == 200);
        lines = 30; sim.Frames(4, ui);
        CHECK(cr.h > 200);
        // a negative width is what is left, less that much
        flags = UI::Child_Borders; lines = 2; size = UI::Vec2{0, 60}; sim.Frames(3, ui);
        const float fullW = cr.w;
        size = UI::Vec2{-50, 60}; sim.Frames(3, ui);
        CHECK(std::fabs(cr.w - (fullW - 50.0f)) < 1.01f);
        // the old four-argument call still works the way it did
        sim.Frame([&] { Win([&] { UI::BeginChild("old", 0, 80, true); UI::Text("x"); UI::EndChild(); cr = UI::GetItemRect(); }); });
        CHECK(cr.h == 80);
        // a child with the no-scroll-bar flag draws no bar even when its content overflows
        {
            Reset(); Sim s2(600, 500);
            auto bar = [&](int wf) {
                auto body = [&] { Win([&] { UI::BeginChild("n", UI::Vec2{200, 60}, UI::Child_Borders, wf); for (int i = 0; i < 20; ++i) { char t[32]; std::snprintf(t, sizeof t, "row %d", i); UI::Text(t); } UI::EndChild(); }); };
                s2.Frames(3, body);
                return s2.Draw(0, 0, 0);
            };
            const Bitmap withBar = bar(0);
            const Bitmap noBar = bar(UI::Window_NoScrollbar);
            CHECK(!SameBitmap(withBar, noBar));
        }
    }

    {   // hover flags
        Reset();
        Sim sim(500, 300);
        UI::Rect ra, rb; bool plain = false, allow = false, dflt = false, blocked = false, allowActive = false;
        bool disabledOn = false;
        auto ui = [&] { Win([&] {
            UI::BeginDisabled(disabledOn);
            UI::Button("A"); ra = UI::GetItemRect();
            plain = UI::IsItemHovered(); allow = UI::IsItemHovered(UI::Hovered_AllowWhenDisabled); dflt = UI::IsItemHovered(0);
            UI::EndDisabled();
            UI::Button("B"); rb = UI::GetItemRect();
            blocked = UI::IsItemHovered(0); allowActive = UI::IsItemHovered(UI::Hovered_AllowWhenBlockedByActiveItem);
        }); };
        sim.Frame(ui);
        sim.Move(CenterX(ra), CenterY(ra)); sim.Frame(ui);
        CHECK(plain && allow && dflt);
        disabledOn = true; sim.Frame(ui);
        CHECK(!plain && allow && !dflt);                          // greyed out: only the AllowWhenDisabled question says yes
        disabledOn = false;
        // held item A: B is not hovered by default, but is with AllowWhenBlockedByActiveItem
        sim.Move(CenterX(ra), CenterY(ra)); sim.Frame(ui); sim.Down(); sim.Frame(ui);
        sim.Move(CenterX(rb), CenterY(rb)); sim.Frame(ui);
        CHECK(!blocked && allowActive);
        sim.Up(); sim.Frame(ui);
        // delays: the pointer has to stay
        UI::Rect rd; bool shortOk = false, normalOk = false;
        auto ui2 = [&] { Win([&] { UI::Button("D"); rd = UI::GetItemRect(); shortOk = UI::IsItemHovered(UI::Hovered_DelayShort); normalOk = UI::IsItemHovered(UI::Hovered_DelayNormal); }); };
        sim.Move(300, 250); sim.Frames(2, ui2);
        sim.Move(CenterX(rd), CenterY(rd));
        sim.Frame(ui2);
        CHECK(!shortOk && !normalOk);
        sim.Frames(5, ui2);
        CHECK(!shortOk);                                          // 6 frames: 0.1 s
        sim.Frames(6, ui2);
        CHECK(shortOk && !normalOk);                              // 12 frames: 0.2 s
        sim.Frames(20, ui2);
        CHECK(shortOk && normalOk);                               // 32 frames: 0.53 s
        sim.Move(300, 250); sim.Frame(ui2);
        CHECK(!shortOk && !normalOk);
        sim.Move(CenterX(rd), CenterY(rd)); sim.Frame(ui2);
        CHECK(!shortOk);                                          // the timer started again
    }

    {   // window hover and focus flags
        Reset();
        Sim sim(500, 300);
        bool anyWin = false, anyFocus = false, hov = false, foc = false;
        auto ui = [&] { Win([&] {
            anyWin = UI::IsWindowHovered(UI::Hovered_AnyWindow); hov = UI::IsWindowHovered(UI::Hovered_RootAndChildWindows);
            anyFocus = UI::IsWindowFocused(UI::Focused_AnyWindow); foc = UI::IsWindowFocused(UI::Focused_RootAndChildWindows);
        }); };
        sim.Move(480, 280); sim.Frames(2, ui);
        CHECK(!anyWin && !hov);
        sim.Move(100, 100); sim.Frames(2, ui);
        CHECK(anyWin && hov);
        sim.Click(100, 100, ui);
        CHECK(anyFocus && foc);
    }

    {   // the middle mouse button
        Reset();
        Sim sim(300, 200);
        bool down = false, clicked = false, released = false;
        auto ui = [&] { Win([&] { down = UI::IsMouseDown(UI::MouseButton_Middle); clicked = UI::IsMouseClicked(UI::MouseButton_Middle); released = UI::IsMouseReleased(UI::MouseButton_Middle); }); };
        sim.Frame(ui);
        CHECK(!down && !clicked && !released);
        sim.mdown = true; sim.Frame(ui);
        CHECK(down && clicked && !released);
        sim.Frame(ui);
        CHECK(down && !clicked);
        CHECK(UI::GetIO().mouseDown[2]);
        sim.mdown = false; sim.Frame(ui);
        CHECK(!down && released);
        CHECK(!UI::IsMouseDown(UI::MouseButton_Left) && !UI::IsMouseDown(UI::MouseButton_Right));
    }

    {   // GetMouseDragDelta and ResetMouseDragDelta
        Reset();
        Sim sim(300, 300);
        UI::Vec2 dl{}, dr{}, dm{};
        auto ui = [&] { Win([&] { dl = UI::GetMouseDragDelta(0); dr = UI::GetMouseDragDelta(1, 10.0f); dm = UI::GetMouseDragDelta(2); }); };
        sim.Move(100, 100); sim.Frame(ui);
        sim.Down(); sim.Frame(ui);
        CHECK(dl.x == 0 && dl.y == 0);
        sim.Move(102, 101); sim.Frame(ui);
        CHECK(dl.x == 0 && dl.y == 0);                            // under the threshold: no drag yet
        sim.Move(130, 140); sim.Frame(ui);
        CHECK(dl.x == 30 && dl.y == 40);
        UI::ResetMouseDragDelta(0);
        sim.Move(135, 150); sim.Frame(ui);
        CHECK(dl.x == 5 && dl.y == 10);                           // from the new origin; the lock stays
        sim.Up(); sim.Frame(ui);
        CHECK(dl.x == 0 && dl.y == 0);
        sim.rdown = true; sim.Frame(ui); sim.Move(150, 150); sim.Frame(ui);
        CHECK(dr.x == 15 && dr.y == 0);                           // right button, its own 10 px threshold, measured from where it went down at (135, 150)
        sim.rdown = false; sim.mdown = true; sim.Frame(ui); sim.Move(150, 250); sim.Frame(ui);
        CHECK(dm.y == 100 && dm.x == 0);
    }

    {   // GetStyle follows PushStyleVar
        Reset();
        Sim sim(300, 300);
        UI::StyleView a, b, c;
        sim.Frame([&] { Win([&] {
            a = UI::GetStyle();
            UI::PushStyleVar(UI::StyleVar_ItemSpacing, UI::Vec2{3.0f, 9.0f});
            b = UI::GetStyle();
            UI::PopStyleVar();
            c = UI::GetStyle();
        }); });
        CHECK(a.ItemSpacing.x == c.ItemSpacing.x && a.ItemSpacing.y == c.ItemSpacing.y);
        CHECK(b.ItemSpacing.x == 3.0f && b.ItemSpacing.y == 9.0f);
        CHECK(a.FramePadding.x > 0 && a.FramePadding.y >= 0 && a.ScrollbarSize > 0 && a.WindowPadding.x == UI::Style().windowPadding);
        CHECK(a.Alpha == 1.0f);
    }

    {   // CalcItemWidth, GetCursorStartPos, GetWindowContentRegionMin, IsAnyItemHovered
        Reset();
        Sim sim(500, 300);
        float full = 0, pushed = 0, nextOne = 0; UI::Vec2 start{}, regionMin{}; bool anyHov = false, anyBefore = false; UI::Rect rb;
        auto ui = [&] { Win([&] {
            anyBefore = UI::IsAnyItemHovered();
            full = UI::CalcItemWidth();
            UI::PushItemWidth(120.0f); pushed = UI::CalcItemWidth(); UI::PopItemWidth();
            UI::SetNextItemWidth(77.0f); nextOne = UI::CalcItemWidth();
            start = UI::GetCursorStartPos(); regionMin = UI::GetWindowContentRegionMin();
            UI::Button("hover me"); rb = UI::GetItemRect(); anyHov = UI::IsAnyItemHovered();
        }); };
        sim.Move(450, 280); sim.Frame(ui);
        CHECK(!anyBefore && !anyHov);
        CHECK(full > 300 && pushed == 120.0f && nextOne == 77.0f);
        CHECK(regionMin.x == start.x);
        sim.Move(CenterX(rb), CenterY(rb)); sim.Frame(ui);
        CHECK(anyHov);
    }

    {   // a tooltip made of widgets: hidden on its first frame (it is measured off screen), then drawn; the layout does not change
        Reset();
        Sim sim(640, 480);
        float yAfter = 0; bool shown = false;
        bool withTip = true;
        auto ui = [&] { Win([&] {
            UI::Text("first");
            if (withTip && UI::BeginTooltip()) { shown = true; UI::Text("hello tooltip"); UI::Separator(); UI::Text("second line"); UI::EndTooltip(); }
            UI::Button("after"); yAfter = UI::GetItemRect().y;
        }); };
        sim.Move(300, 300);
        withTip = false; sim.Frames(2, ui);
        const float yPlain = yAfter;
        const Bitmap plain = sim.Draw(0, 0, 0);
        withTip = true; shown = false;
        sim.Frame(ui);
        CHECK(shown);
        CHECK(yAfter == yPlain);                                  // the layout does not see the tooltip
        CHECK(SameBitmap(sim.Draw(0, 0, 0), plain));              // first frame: nothing on screen
        sim.Frame(ui);
        const Bitmap second = sim.Draw(0, 0, 0);
        CHECK(!SameBitmap(second, plain));                        // now it is
        CHECK(yAfter == yPlain);
        // it sits at the pointer, down and to the right, and not elsewhere
        int differing = 0, outside = 0;
        for (int y = 0; y < 480; ++y) for (int x = 0; x < 640; ++x) if (PixelAt(second, x, y) != PixelAt(plain, x, y)) { ++differing; if (x < 300 || y < 300) ++outside; }
        CHECK(differing > 200 && outside == 0);
        // the pointer being blocked by another layer: no tooltip
        sim.blocked = true; shown = false; sim.Frame(ui);
        CHECK(!shown);
        sim.blocked = false;
        // near the corner it flips to stay on screen
        sim.Move(620, 470); sim.Frames(3, ui);
        const Bitmap corner = sim.Draw(0, 0, 0);
        int farOut = 0;
        for (int y = 0; y < 480; ++y) for (int x = 0; x < 640; ++x) if (PixelAt(corner, x, y) != PixelAt(plain, x, y) && (x > 622 || y > 472)) ++farOut;
        CHECK(farOut == 0);
        // a tooltip left unfinished (no EndTooltip) must not leak into the next frame
        sim.Move(300, 300);
        sim.Frame([&] { Win([&] { UI::Text("x"); UI::BeginTooltip(); }); });
        float yNext = 0;
        sim.Frame([&] { Win([&] { UI::Text("x"); UI::Button("b"); yNext = UI::GetItemRect().y; }); });
        CHECK(yNext > 10);
        // SetItemTooltip: only on a hovered item
        UI::Rect rb;
        auto ui2 = [&] { Win([&] { UI::Button("hover"); rb = UI::GetItemRect(); UI::SetItemTooltip("tip %d", 5); }); };
        sim.Move(400, 400); sim.Frame(ui2);
        const Bitmap noTip = sim.Draw(0, 0, 0);
        sim.Move(CenterX(rb), CenterY(rb)); sim.Frame(ui2);
        CHECK(!SameBitmap(sim.Draw(0, 0, 0), noTip));
        // BeginItemTooltip waits for the delay
        bool itemTip = false;
        auto ui3 = [&] { Win([&] { UI::Button("hover"); rb = UI::GetItemRect(); if (UI::BeginItemTooltip()) { itemTip = true; UI::Text("late"); UI::EndTooltip(); } }); };
        sim.Move(400, 400); sim.Frames(2, ui3);
        sim.Move(CenterX(rb), CenterY(rb));
        itemTip = false; sim.Frames(10, ui3);
        CHECK(!itemTip);
        sim.Frames(20, ui3);
        CHECK(itemTip);
    }

    {   // BeginPopupContextWindow: opens on the right button going up over the window, not on the left, closes on an outside click
        Reset();
        Sim sim(640, 480);
        bool open = false;
        auto ui = [&] { Win([&] {
            UI::Text("body");
            open = UI::BeginPopupContextWindow("ctx");
            if (open) { UI::Selectable("Item A", false); UI::Selectable("Item B", false); UI::EndPopup(); }
        }); };
        sim.Move(200, 200); sim.Frames(2, ui);
        CHECK(!open);
        sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
        CHECK(!open);                                             // the left button does nothing
        sim.rdown = true; sim.Frame(ui);
        CHECK(!open);                                             // pressed: not yet
        sim.rdown = false; sim.Frame(ui);
        CHECK(open);                                              // released over the window
        sim.Frame(ui);
        CHECK(open);
        const Bitmap withPopup = sim.Draw(0, 0, 0);
        sim.Move(380, 380); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
        CHECK(!open);                                             // an outside click closes it
        CHECK(!SameBitmap(sim.Draw(0, 0, 0), withPopup));
        // a release outside the window does not open it
        sim.Move(600, 450); sim.rdown = true; sim.Frame(ui); sim.rdown = false; sim.Frame(ui);
        CHECK(!open);
        // NoOpenOverItems: over a button it stays shut
        UI::Rect rb; bool open2 = false;
        auto ui2 = [&] { Win([&] {
            UI::Button("btn"); rb = UI::GetItemRect();
            open2 = UI::BeginPopupContextWindow("ctx2", UI::Popup_MouseButtonRight | UI::Popup_NoOpenOverItems);
            if (open2) UI::EndPopup();
        }); };
        sim.Move(CenterX(rb), CenterY(rb)); sim.Frames(2, ui2);
        sim.Move(CenterX(rb), CenterY(rb)); sim.Frame(ui2);
        sim.rdown = true; sim.Frame(ui2); sim.rdown = false; sim.Frame(ui2);
        CHECK(!open2);
        sim.Move(300, 300); sim.Frames(2, ui2);
        sim.rdown = true; sim.Frame(ui2); sim.rdown = false; sim.Frame(ui2);
        CHECK(open2);
        // inside a child it uses the child's rectangle
        Reset();
        bool inChild = false;
        auto ui3 = [&] { Win([&] {
            UI::BeginChild("c", UI::Vec2{150, 100}, UI::Child_Borders);
            inChild = UI::BeginPopupContextWindow("cc");
            if (inChild) UI::EndPopup();
            UI::EndChild();
        }); };
        sim.rdown = false; sim.Move(300, 300); sim.Frames(2, ui3);
        sim.rdown = true; sim.Frame(ui3); sim.rdown = false; sim.Frame(ui3);
        CHECK(!inChild);                                          // outside the child
        sim.Move(60, 60); sim.Frames(2, ui3);
        sim.rdown = true; sim.Frame(ui3); sim.rdown = false; sim.Frame(ui3);
        CHECK(inChild);
    }

    {   // SetNextWindowFocus, SetNextWindowBgAlpha, SetWindowPos / SetWindowSize
        Reset();
        Sim sim(640, 480);
        bool focA = false, focB = false; bool focusA = false;
        auto ui = [&] {
            UI::SetNextWindowPos(UI::Vec2{20, 20}); UI::SetNextWindowSize(UI::Vec2{200, 150});
            if (focusA) UI::SetNextWindowFocus();
            if (UI::Begin("A", nullptr, 0)) focA = UI::IsWindowFocused();
            UI::End();
            UI::SetNextWindowPos(UI::Vec2{300, 20}); UI::SetNextWindowSize(UI::Vec2{200, 150});
            if (UI::Begin("B", nullptr, 0)) focB = UI::IsWindowFocused();
            UI::End();
        };
        sim.Frames(2, ui);
        CHECK(focB && !focA);                                     // the last window that appeared has the focus
        focusA = true; sim.Frame(ui); focusA = false;
        CHECK(focA && !focB);
        sim.Frame(ui);
        CHECK(focA && !focB);                                     // and keeps it
        // body alpha: 0 shows the page behind, 1 the panel
        float alpha = 1.0f; bool use = false;
        auto ui2 = [&] {
            UI::SetNextWindowPos(UI::Vec2{100, 100}); UI::SetNextWindowSize(UI::Vec2{200, 200});
            if (use) UI::SetNextWindowBgAlpha(alpha);
            UI::Begin("Bg", nullptr, 0); UI::End();
        };
        Reset(); sim.Frames(2, ui2);
        const unsigned solid = PixelAt(sim.Draw(10, 200, 10), 200, 250);
        use = true; alpha = 0.0f; sim.Frames(2, ui2);
        const unsigned clear = PixelAt(sim.Draw(10, 200, 10), 200, 250);
        alpha = 0.5f; sim.Frames(2, ui2);
        const unsigned half = PixelAt(sim.Draw(10, 200, 10), 200, 250);
        CHECK(clear == Rgba(10, 200, 10));
        CHECK(solid != clear && half != clear);
        CHECK(half != solid);
        // the setting is for one window only
        use = false; sim.Frames(2, ui2);
        CHECK(PixelAt(sim.Draw(10, 200, 10), 200, 250) == solid);
        // SetWindowPos moves it from the next frame
        Reset();
        UI::Vec2 pos{};
        bool move = false;
        auto ui3 = [&] {
            UI::SetNextWindowPos(UI::Vec2{50, 50}, UI::Cond_Once); UI::SetNextWindowSize(UI::Vec2{200, 100}, UI::Cond_Once);
            if (UI::Begin("M", nullptr, 0)) { if (move) { UI::SetWindowPos(UI::Vec2{150, 120}); UI::SetWindowSize(UI::Vec2{260, 90}); move = false; } pos = UI::GetWindowPos(); }
            UI::End();
        };
        sim.Frames(2, ui3);
        CHECK(pos.x == 50 && pos.y == 50);
        move = true; sim.Frames(3, ui3);
        CHECK(pos.x == 150 && pos.y == 120);
        UI::Vec2 sz{};
        sim.Frame([&] { UI::SetNextWindowPos(UI::Vec2{50, 50}, UI::Cond_Once); if (UI::Begin("M", nullptr, 0)) sz = UI::GetWindowSize(); UI::End(); });
        CHECK(sz.x == 260 && sz.y == 90);
        UI::SetWindowPos("M", UI::Vec2{10, 15}); UI::SetWindowSize("M", UI::Vec2{120, 80});
        sim.Frames(2, ui3);
        CHECK(pos.x == 10 && pos.y == 15);
        // hostile numbers change nothing
        const float nan = std::numeric_limits<float>::quiet_NaN();
        UI::SetWindowPos("M", UI::Vec2{nan, 1.0f}); UI::SetWindowSize("M", UI::Vec2{-5.0f, nan}); UI::SetNextWindowBgAlpha(nan); UI::SetWindowPos(UI::Vec2{nan, nan});
        sim.Frames(2, ui3);
        CHECK(pos.x == 10 && pos.y == 15);
    }

    {   // ListClipper
        Reset();
        Sim sim(500, 400);
        int rowsRun = 0, first = -1, last = -1, count = 5000; float yAfter = 0, yStart = 0, itemH = 0;
        auto ui = [&] { Win([&] {
            UI::BeginChild("list", UI::Vec2{300, 200}, UI::Child_Borders);
            UI::ListClipper clip; clip.Begin(count, itemH);
            yStart = UI::GetCursorScreenPos().y;
            rowsRun = 0; first = 1 << 30; last = -1;
            while (clip.Step()) for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) { char t[32]; std::snprintf(t, sizeof t, "row %d", i); UI::Text(t); ++rowsRun; first = std::min(first, i); last = std::max(last, i); }
            clip.End();
            yAfter = UI::GetCursorScreenPos().y;
            UI::EndChild();
        }); };
        // measure the real row advance of a Text first
        float adv = 0;
        sim.Frame([&] { Win([&] { UI::Text("a"); const float a = UI::GetCursorScreenPos().y; UI::Text("b"); adv = UI::GetCursorScreenPos().y - a; }); });
        itemH = adv;
        count = 200; sim.Frames(3, ui);
        const float span200 = yAfter - yStart;
        CHECK(std::fabs(span200 - 200 * adv) < 0.5f);             // a clipped list is as tall as the full one
        count = 5000; sim.Frames(3, ui);
        CHECK(rowsRun < 40 && rowsRun > 5);
        CHECK(first == 0);
        CHECK(std::fabs((yAfter - yStart) - 5000 * adv) < 0.5f);
        // scroll to the end: the last row is drawn and the range stays inside the list
        sim.Move(150, 100);
        for (int i = 0; i < 600; ++i) { sim.wheel = -40.0f; sim.Frame(ui); if (last == count - 1) break; }
        CHECK(last == count - 1);
        CHECK(first > 4000 && first < count);
        // a bit above the end, rows are in order and contiguous
        sim.wheel = 3.0f; sim.Frame(ui);
        CHECK(first >= 0 && last < count && rowsRun == last - first + 1);
        // degenerate counts and heights
        count = 0; sim.Frames(2, ui); CHECK(rowsRun == 0);
        count = -7; sim.Frames(2, ui); CHECK(rowsRun == 0);
        count = 3; itemH = -1.0f; sim.Frames(2, ui); CHECK(rowsRun == 3);   // the default height
        UI::ListClipper stray; stray.Begin(10, 20.0f); stray.End(); CHECK(!stray.Step());
    }

    {   // draw list: rounded corners by flag
        Reset();
        Sim sim(300, 300);
        auto shot = [&](const std::function<void(const UI::DrawList&)>& draw) {
            sim.Frame([&] { Win([&] { draw(UI::GetForegroundDrawList()); }); });
            return sim.Draw(0, 0, 0);
        };
        const UI::Vec2 a{50, 50}, b{150, 150};
        const Bitmap top = shot([&](const UI::DrawList& dl) { dl.AddRectFilledFlags(a, b, kRedPacked, 20.0f, UI::DrawFlags_RoundCornersTop); });
        CHECK(PixelAt(top, 50, 50) != kRed && PixelAt(top, 149, 50) != kRed);      // the two top corners are cut
        CHECK(PixelAt(top, 50, 149) == kRed && PixelAt(top, 149, 149) == kRed);    // the bottom ones are square
        CHECK(PixelAt(top, 100, 50) == kRed && PixelAt(top, 60, 60) == kRed && PixelAt(top, 100, 100) == kRed);
        const Bitmap all = shot([&](const UI::DrawList& dl) { dl.AddRectFilledFlags(a, b, kRedPacked, 20.0f, 0); });
        CHECK(PixelAt(all, 50, 149) != kRed && PixelAt(all, 50, 50) != kRed);      // no flags: all four corners
        const Bitmap none = shot([&](const UI::DrawList& dl) { dl.AddRectFilledFlags(a, b, kRedPacked, 20.0f, UI::DrawFlags_RoundCornersNone); });
        CHECK(PixelAt(none, 50, 50) == kRed && PixelAt(none, 149, 149) == kRed);
        const int nTop = CountColour(top, kRed), nAll = CountColour(all, kRed), nNone = CountColour(none, kRed);
        CHECK(nNone == 100 * 100 && nTop < nNone && nAll < nTop);
        const Bitmap left = shot([&](const UI::DrawList& dl) { dl.AddRectFilledFlags(a, b, kRedPacked, 20.0f, UI::DrawFlags_RoundCornersLeft); });
        CHECK(PixelAt(left, 50, 50) != kRed && PixelAt(left, 50, 149) != kRed && PixelAt(left, 149, 50) == kRed && PixelAt(left, 149, 149) == kRed);
        // the outline sits inside the bounds
        const Bitmap line = shot([&](const UI::DrawList& dl) { dl.AddRectFlags(a, b, kRedPacked, 12.0f, UI::DrawFlags_RoundCornersLeft, 2.0f); });
        CHECK(PixelAt(line, 50, 100) == kRed && PixelAt(line, 51, 100) == kRed && PixelAt(line, 49, 100) != kRed && PixelAt(line, 52, 100) != kRed);
        CHECK(PixelAt(line, 100, 100) != kRed);
        CHECK(PixelAt(line, 140, 50) == kRed && PixelAt(line, 140, 48) != kRed);
        CHECK(PixelAt(line, 149, 50) == kRed);                                    // the right corners are square
        CHECK(PixelAt(line, 50, 50) != kRed);                                     // the left one is cut
        // rounding 0 is the plain rectangle; hostile numbers draw nothing
        const Bitmap plain0 = shot([&](const UI::DrawList& dl) { dl.AddRectFilledFlags(a, b, kRedPacked, 0.0f, UI::DrawFlags_RoundCornersTop); });
        CHECK(CountColour(plain0, kRed) == 100 * 100);
        const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
        const Bitmap bad = shot([&](const UI::DrawList& dl) {
            dl.AddRectFilledFlags(UI::Vec2{nan, 0}, b, kRedPacked, 5.0f); dl.AddRectFlags(a, UI::Vec2{inf, inf}, kRedPacked, 5.0f);
            dl.AddRectFilledFlags(b, a, kRedPacked, 5.0f); dl.AddRectFlags(a, b, kRedPacked, nan, 0, 2.0f); dl.AddRectFlags(a, b, kRedPacked, 5.0f, 0, -1.0f);
        });
        CHECK(CountColour(bad, kRed) == 0);
        // huge rounding is clamped to half the shorter side
        const Bitmap pill = shot([&](const UI::DrawList& dl) { dl.AddRectFilledFlags(UI::Vec2{50, 50}, UI::Vec2{150, 90}, kRedPacked, 1000.0f); });
        CHECK(PixelAt(pill, 100, 70) == kRed && PixelAt(pill, 50, 50) != kRed && PixelAt(pill, 52, 70) == kRed);
    }

    {   // draw list: polygons
        Reset();
        Sim sim(300, 300);
        auto shot = [&](const std::function<void(const UI::DrawList&)>& draw) {
            sim.Frame([&] { Win([&] { draw(UI::GetForegroundDrawList()); }); });
            return sim.Draw(0, 0, 0);
        };
        const Bitmap hex = shot([&](const UI::DrawList& dl) { dl.AddNgonFilled(UI::Vec2{150, 150}, 40.0f, kRedPacked, 6); });
        const float area = 2.598076f * 40.0f * 40.0f;
        const int n = CountColour(hex, kRed);
        CHECK(n > area * 0.95f && n < area * 1.05f);
        CHECK(PixelAt(hex, 150, 150) == kRed && PixelAt(hex, 150, 100) != kRed);           // flat top: the first corner is to the right
        CHECK(PixelAt(hex, 188, 150) == kRed && PixelAt(hex, 192, 150) != kRed);
        const Bitmap ring = shot([&](const UI::DrawList& dl) { dl.AddNgon(UI::Vec2{150, 150}, 40.0f, kRedPacked, 8, 2.0f); });
        CHECK(PixelAt(ring, 150, 150) != kRed && CountColour(ring, kRed) > 100);
        const Bitmap few = shot([&](const UI::DrawList& dl) { dl.AddNgon(UI::Vec2{150, 150}, 40.0f, kRedPacked, 2); dl.AddNgonFilled(UI::Vec2{150, 150}, -4.0f, kRedPacked, 6); dl.AddNgonFilled(UI::Vec2{150, 150}, 40.0f, kRedPacked, 0); });
        CHECK(CountColour(few, kRed) == 0);
        const Bitmap many = shot([&](const UI::DrawList& dl) { dl.AddNgonFilled(UI::Vec2{150, 150}, 40.0f, kRedPacked, 100000); });
        CHECK(CountColour(many, kRed) > 4500);                                           // clamped to 128 corners: nearly a disc (area 5026)
    }

    {   // draw list: textured quads (the software backend does not draw them, so the draw data is what is checked)
        Reset();
        Sim sim(300, 300);
        SDL_Texture* fake = reinterpret_cast<SDL_Texture*>(static_cast<std::uintptr_t>(0x1000));
        auto texturedIndices = [&](const std::function<void(const UI::DrawList&)>& draw) {
            sim.Frame([&] { Win([&] { draw(UI::GetForegroundDrawList()); }); });
            const UI::DrawData dd = UI::GetDrawData();
            int idx = 0, verts = dd.vertexCount;
            for (int i = 0; i < dd.cmdCount; ++i) if (dd.cmds[i].texture == fake) idx += dd.cmds[i].idxCount;
            (void)verts;
            return idx;
        };
        const UI::Vec2 a{50, 50}, b{150, 150};
        CHECK(texturedIndices([&](const UI::DrawList& dl) { dl.AddImage(fake, a, b); }) == 6);
        CHECK(texturedIndices([&](const UI::DrawList& dl) { dl.AddImageRounded(fake, a, b, UI::Vec2{0, 0}, UI::Vec2{1, 1}, 0xffffffffu, 0.0f); }) == 6);
        const int rounded = texturedIndices([&](const UI::DrawList& dl) { dl.AddImageRounded(fake, a, b, UI::Vec2{0, 0}, UI::Vec2{1, 1}, 0xffffffffu, 20.0f); });
        CHECK(rounded > 6 && rounded % 3 == 0);
        const int topOnly = texturedIndices([&](const UI::DrawList& dl) { dl.AddImageRounded(fake, a, b, UI::Vec2{0, 0}, UI::Vec2{1, 1}, 0xffffffffu, 20.0f, UI::DrawFlags_RoundCornersTop); });
        CHECK(topOnly > 6 && topOnly < rounded);                                           // fewer corners, fewer triangles
        CHECK(texturedIndices([&](const UI::DrawList& dl) { dl.AddImageQuad(fake, a, UI::Vec2{160, 60}, b, UI::Vec2{40, 140}); }) == 6);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(texturedIndices([&](const UI::DrawList& dl) { dl.AddImageQuad(fake, a, UI::Vec2{nan, 60}, b, UI::Vec2{40, 140}); dl.AddImageQuad(nullptr, a, a, b, b); dl.AddImageRounded(nullptr, a, b, UI::Vec2{}, UI::Vec2{1, 1}, 0xffffffffu, 5.0f); dl.AddImageRounded(fake, b, a, UI::Vec2{}, UI::Vec2{1, 1}, 0xffffffffu, 5.0f); }) == 0);
        // the uv follow the shape: the first corner of a rounded image samples inside the picture, never outside [0, 1]
        sim.Frame([&] { Win([&] { UI::GetForegroundDrawList().AddImageRounded(fake, a, b, UI::Vec2{0, 0}, UI::Vec2{1, 1}, 0xffffffffu, 30.0f); }); });
        const UI::DrawData dd = UI::GetDrawData();
        bool inside = true; int seen = 0;
        const UI::DrawVertex* vs = static_cast<const UI::DrawVertex*>(dd.vertices);
        for (int i = 0; i < dd.cmdCount; ++i) if (dd.cmds[i].texture == fake)
            for (int k = 0; k < dd.cmds[i].idxCount; ++k) { const UI::DrawVertex& v = vs[dd.indices[dd.cmds[i].idxOffset + k]]; ++seen; if (v.u < -1e-4f || v.u > 1.0001f || v.v < -1e-4f || v.v > 1.0001f || v.x < 49.9f || v.x > 150.1f) inside = false; }
        CHECK(seen > 0 && inside);
    }

    {   // a column flagged NoResize keeps its width when its border is dragged; an ordinary one does not
        auto widthAfterDrag = [&](int colFlags, float grabX) {
            Reset();
            Sim sim(500, 300);
            float x2 = 0, y = 0;
            auto ui = [&] { Win([&] {
                if (UI::BeginTable("t", 2, UI::Table_None)) {
                    UI::TableSetupColumn("a", colFlags, 0.0f); UI::TableSetupColumn("b");
                    UI::TableHeadersRow();
                    UI::TableNextRow(); UI::TableSetColumnIndex(0); UI::Text("x");
                    UI::TableSetColumnIndex(1); x2 = UI::GetCursorScreenPos().x; y = UI::GetCursorScreenPos().y; UI::Text("y");
                    UI::EndTable();
                }
            }); };
            sim.Frames(3, ui);
            const float before = x2, gy = y + 6.0f;
            sim.Move(before + grabX, gy); sim.Frame(ui); sim.Down(); sim.Frame(ui);
            sim.Move(before + grabX + 40.0f, gy); sim.Frames(2, ui); sim.Up(); sim.Frames(2, ui);
            return x2 - before;
        };
        float best = 0.0f, bestX = 0.0f;
        for (float gx = -14.0f; gx <= 2.0f; gx += 1.0f) { const float d = widthAfterDrag(0, gx); if (std::fabs(d) > std::fabs(best)) { best = d; bestX = gx; } }
        CHECK(best > 15.0f);                                       // some grab point on the border drags it
        const float noResize = widthAfterDrag(UI::Col_NoResize, bestX);
        CHECK(std::fabs(noResize) < 0.01f);                        // the same grab does nothing to a NoResize column
    }

    {   // InvisibleButton with right and middle buttons, Shortcut, drag-and-drop peeking, popup origin, item widths for a row, tab bar flags
        Reset();
        Sim sim(640, 480);
        UI::Rect r; int leftN = 0, rightN = 0, midN = 0, bothN = 0;
        auto ui = [&] { Win([&] {
            UI::InvisibleButton("l", 100, 30); r = UI::GetItemRect(); (void)r;
            if (UI::InvisibleButton("L", 100, 30, 0)) ++leftN;
            if (UI::InvisibleButton("R", 100, 30, UI::Button_MouseButtonRight)) ++rightN;
            if (UI::InvisibleButton("M", 100, 30, UI::Button_MouseButtonMiddle)) ++midN;
            if (UI::InvisibleButton("B", 100, 30, UI::Button_MouseButtonLeft | UI::Button_MouseButtonRight)) ++bothN;
        }); };
        sim.Move(300, 300); sim.Frames(2, ui);
        // find the rows
        float ys[4]; {
            int k = 0;
            sim.Frame([&] { Win([&] {
                UI::InvisibleButton("l", 100, 30);
                const char* ids[4] = {"L", "R", "M", "B"};
                for (int i = 0; i < 4; ++i) { UI::InvisibleButton(ids[i], 100, 30, i == 0 ? 0 : i == 1 ? UI::Button_MouseButtonRight : i == 2 ? UI::Button_MouseButtonMiddle : 3); ys[k++] = UI::GetItemRect().y + 15; }
            }); });
        }
        const float x = 40;
        // left
        sim.Move(x, ys[0]); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
        CHECK(leftN == 1 && rightN == 0 && midN == 0);
        // right button on the right-only button: fires on release, not on press
        sim.Move(x, ys[1]); sim.Frame(ui);
        sim.rdown = true; sim.Frame(ui); CHECK(rightN == 0);
        sim.rdown = false; sim.Frame(ui); CHECK(rightN == 1);
        // a left click on it does nothing
        sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
        CHECK(rightN == 1);
        // pressed on it, released elsewhere: no click
        sim.rdown = true; sim.Frame(ui); sim.Move(300, 300); sim.Frame(ui); sim.rdown = false; sim.Frame(ui);
        CHECK(rightN == 1);
        // pressed elsewhere, released on it: no click
        sim.rdown = true; sim.Frame(ui); sim.Move(x, ys[1]); sim.Frame(ui); sim.rdown = false; sim.Frame(ui);
        CHECK(rightN == 1);
        // middle
        sim.Move(x, ys[2]); sim.Frame(ui);
        sim.mdown = true; sim.Frame(ui); sim.mdown = false; sim.Frame(ui);
        CHECK(midN == 1);
        // left and right both
        sim.Move(x, ys[3]); sim.Frame(ui);
        sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
        sim.rdown = true; sim.Frame(ui); sim.rdown = false; sim.Frame(ui);
        CHECK(bothN == 2);
    }

    {   // Shortcut
        Reset();
        Sim sim(300, 200);
        int saves = 0, plain = 0, shiftS = 0;
        char buf[16] = "";
        bool field = false;
        auto ui = [&] { Win([&] {
            if (UI::Shortcut(UI::Mod_Ctrl, UI::KeyId_S)) ++saves;
            if (UI::Shortcut(UI::Mod_None, UI::KeyId_F)) ++plain;
            if (UI::Shortcut(UI::Mod_Ctrl | UI::Mod_Shift, UI::KeyId_S)) ++shiftS;
            if (field) UI::InputText("##f", buf, sizeof buf);
        }); };
        sim.Frames(2, ui);
        sim.ctrl = true; sim.Hold(UI::KeyId_S, true); sim.Frame(ui);
        CHECK(saves == 1 && shiftS == 0 && plain == 0);
        sim.Frame(ui);
        CHECK(saves == 1);                                        // held: not again
        sim.Hold(UI::KeyId_S, false); sim.Frame(ui); sim.ctrl = false;
        sim.shift = true; sim.ctrl = true; sim.Hold(UI::KeyId_S, true); sim.Frame(ui);
        CHECK(saves == 1 && shiftS == 1);                         // exactly those modifiers
        sim.Hold(UI::KeyId_S, false); sim.Frame(ui); sim.shift = false; sim.ctrl = false;
        sim.Hold(UI::KeyId_S, true); sim.Frame(ui); sim.Hold(UI::KeyId_S, false); sim.Frame(ui);
        CHECK(saves == 1);                                        // no modifier: not Ctrl+S
        sim.Hold(UI::KeyId_F, true); sim.Frame(ui); sim.Hold(UI::KeyId_F, false); sim.Frame(ui);
        CHECK(plain == 1);
        // a text field with the keyboard swallows plain-letter shortcuts, not Ctrl ones
        field = true; sim.Frame(ui);
        UI::Rect r; sim.Frame([&] { Win([&] { UI::InputText("##f", buf, sizeof buf); r = UI::GetItemRect(); }); });
        sim.Click(CenterX(r), CenterY(r), ui);
        sim.Hold(UI::KeyId_F, true); sim.Frame(ui); sim.Hold(UI::KeyId_F, false); sim.Frame(ui);
        CHECK(plain == 1);
        sim.ctrl = true; sim.Hold(UI::KeyId_S, true); sim.Frame(ui); sim.Hold(UI::KeyId_S, false); sim.Frame(ui); sim.ctrl = false;
        CHECK(saves == 2);
        CHECK(!UI::Shortcut(UI::Mod_Ctrl, (UI::KeyId)9999) && !UI::Shortcut(UI::Mod_Ctrl, UI::KeyId_None));
    }

    {   // GetDragDropPayload and BeginDragDropTargetCustom
        Reset();
        Sim sim(640, 480);
        int item = 7; UI::Rect rs; int seen = -1; bool peeked = false, customOver = false; int got = -1;
        UI::Rect box{300, 40, 150, 60};
        auto ui = [&] { Win([&] {
            UI::Button("drag me"); rs = UI::GetItemRect();
            if (UI::BeginDragDropSource()) { UI::SetDragDropPayload("ITEM", &item, sizeof item); UI::SetDragDropPreview("item 7"); UI::EndDragDropSource(); }
            const UI::DragPayload* p = UI::GetDragDropPayload();
            peeked = p != nullptr; if (p) std::memcpy(&seen, p->data, sizeof seen);
            customOver = UI::BeginDragDropTargetCustom(box, 99);
            if (customOver) { if (const UI::DragPayload* a = UI::AcceptDragDropPayload("ITEM")) if (a->delivery) std::memcpy(&got, a->data, sizeof got); UI::EndDragDropTarget(); }
        }); };
        sim.Frames(2, ui);
        sim.Move(CenterX(rs), CenterY(rs)); sim.Frames(2, ui);
        CHECK(!peeked && !customOver);
        sim.Down(); sim.Frame(ui);
        sim.Move(CenterX(rs) + 30, CenterY(rs) + 30); sim.Frames(2, ui);
        CHECK(peeked && seen == 7 && !customOver);                // dragging: the payload can be seen, nobody is over the target
        sim.Move(box.x + 20, box.y + 20); sim.Frame(ui);
        CHECK(customOver);
        sim.Up(); sim.Frame(ui);
        CHECK(got == 7);                                          // delivered on the bare rectangle
        sim.Frame(ui);
        CHECK(!peeked);
        // hostile rectangle
        const float nan = std::numeric_limits<float>::quiet_NaN();
        box = UI::Rect{nan, nan, nan, nan}; sim.Frame(ui);
        CHECK(!customOver);
    }

    {   // GetMousePosOnOpeningCurrentPopup, PushMultiItemsWidths, GetTreeNodeToLabelSpacing, BeginTabBar flags
        Reset();
        Sim sim(640, 480);
        UI::Vec2 opened{}; bool open = false;
        auto ui = [&] { Win([&] {
            UI::Button("b");
            if (UI::BeginPopup("p")) { open = true; opened = UI::GetMousePosOnOpeningCurrentPopup(); UI::Text("in"); UI::EndPopup(); } else open = false;
        }); };
        sim.Move(55, 45); sim.Frame(ui);
        UI::OpenPopup("p");
        sim.Frame(ui);
        sim.Move(200, 200); sim.Frame(ui);
        CHECK(open && opened.x == 55 && opened.y == 45);          // where it opened, not where the pointer is now
        float wA = 0, wB = 0, wC = 0;
        sim.Frame([&] { Win([&] {
            UI::PushMultiItemsWidths(3, 300.0f);
            wA = UI::CalcItemWidth(); UI::PopItemWidth(); UI::Dummy(0, 1); wB = UI::CalcItemWidth(); UI::PopItemWidth(); wC = UI::CalcItemWidth(); UI::PopItemWidth();
        }); });
        CHECK(std::fabs(wA - (300.0f - 2 * UI::GetStyle().ItemSpacing.x) / 3.0f) < 0.01f && wA == wB && wB == wC);
        CHECK(UI::GetTreeNodeToLabelSpacing() > 0);
        bool tabs = false;
        sim.Frame([&] { Win([&] { if (UI::BeginTabBar("t", UI::TabBar_Reorderable | UI::TabBar_FittingPolicyScroll)) { tabs = true; if (UI::BeginTabItem("a", nullptr, UI::Tab_SetSelected)) UI::EndTabItem(); UI::EndTabBar(); } }); });
        CHECK(tabs);
    }

    {   // CodeEdit: what ImGuiColorTextEdit does that it did not: overwrite mode, Ctrl+Insert / Shift+Insert / Shift+Delete, Alt+Backspace, whitespace marks
        Reset();
        Sim sim(640, 400);
        UI::CodeEdit ce;
        auto ui = [&] { UI::Begin("W", 10, 10, 600, 360); ce.Draw("ed", 560, 300); UI::End(); };
        auto key = [&](UI::KeyId k) { sim.Hold(k, true); sim.Frame(ui); sim.Hold(k, false); sim.Frame(ui); };
        UI::SetClipboardText("");
        ce.SetText("hello\nab");
        sim.Frames(2, ui);
        sim.Click(ce.PosToScreen(UI::CodePos{0, 0}).x + 1, ce.PosToScreen(UI::CodePos{0, 0}).y + 4, ui);
        ce.SetCaret(UI::CodePos{0, 0});
        CHECK(ce.Focused() && !ce.Overwrite());
        sim.Type("X"); sim.Frame(ui);
        CHECK(ce.GetText() == "Xhello\nab");                                  // insert mode pushes the text along
        key(UI::KeyId_Insert);
        CHECK(ce.Overwrite());
        sim.Type("YZ"); sim.Frame(ui);
        CHECK(ce.GetText() == "XYZllo\nab");                                  // overwrite: two characters replaced
        ce.SetCaret(UI::CodePos{0, 6});
        sim.Type("!"); sim.Frame(ui);
        CHECK(ce.GetText() == "XYZllo!\nab");                                 // at the end of a line it appends, never eats the line break
        ce.SetCaret(UI::CodePos{1, 0}); ce.SetSelection(UI::CodePos{1, 0}, UI::CodePos{1, 2});
        sim.Type("q"); sim.Frame(ui);
        CHECK(ce.GetText() == "XYZllo!\nq");                                  // a selection is replaced as always
        sim.Hold(UI::KeyId_Ctrl, false);
        key(UI::KeyId_Insert);
        CHECK(!ce.Overwrite());                                               // and Insert turns it off again
        // Ctrl+Insert copies, Shift+Insert pastes, Shift+Delete cuts
        ce.SelectAll();
        sim.ctrl = true; key(UI::KeyId_Insert); sim.ctrl = false;
        CHECK(std::strcmp(UI::GetClipboardText(), "XYZllo!\nq") == 0);
        CHECK(ce.GetText() == "XYZllo!\nq");                                  // copy leaves the text
        ce.SetText("abc"); ce.SetCaret(UI::CodePos{0, 3});
        UI::SetClipboardText("-pasted");
        sim.shift = true; key(UI::KeyId_Insert); sim.shift = false;
        CHECK(ce.GetText() == "abc-pasted");
        ce.SetSelection(UI::CodePos{0, 0}, UI::CodePos{0, 3});
        sim.shift = true; sim.Press(Key_Delete); sim.Frame(ui); sim.shift = false;
        CHECK(ce.GetText() == "-pasted");
        CHECK(std::strcmp(UI::GetClipboardText(), "abc") == 0);               // cut, not deleted
        ce.SetCaret(UI::CodePos{0, 0});
        sim.Press(Key_Delete); sim.Frame(ui);
        CHECK(ce.GetText() == "pasted");                                      // the plain Delete still deletes
        // Alt+Backspace undoes
        ce.SetText("one"); ce.SetCaret(UI::CodePos{0, 3});
        sim.Type(" two"); sim.Frame(ui);
        CHECK(ce.GetText() == "one two");
        sim.Hold(UI::KeyId_Alt, true); sim.Press(Key_Backspace); sim.Frame(ui); sim.Hold(UI::KeyId_Alt, false); sim.Frame(ui);
        CHECK(ce.GetText() == "one");
        // read-only: none of the editing ones act
        ce.SetReadOnly(true); ce.SelectAll();
        sim.shift = true; sim.Press(Key_Delete); sim.Frame(ui); sim.shift = false;
        CHECK(ce.GetText() == "one");
        ce.SetReadOnly(false);
        // whitespace marks: more lit pixels with them on, none when off, and tabs get an arrow
        ce.SetText("a b  c\n\tx");
        ce.SetCaret(UI::CodePos{0, 0});
        sim.Move(5, 5); sim.Frames(2, ui);
        const Bitmap off = sim.Draw(0, 0, 0);
        ce.SetShowWhitespace(true);
        sim.Frames(2, ui);
        const Bitmap on = sim.Draw(0, 0, 0);
        CHECK(ce.ShowWhitespace());
        CHECK(!SameBitmap(off, on));
        const UI::Vec2 p1 = ce.PosToScreen(UI::CodePos{0, 1});                 // the space between a and b
        const unsigned bg = PixelAt(off, (int)(p1.x + ce.CharWidth() * 0.5f), (int)(p1.y + ce.LineHeight() * 0.5f));
        CHECK(PixelAt(on, (int)(p1.x + ce.CharWidth() * 0.5f), (int)(p1.y + ce.LineHeight() * 0.5f)) != bg);
        const UI::Vec2 t0 = ce.PosToScreen(UI::CodePos{1, 0});                 // the tab on line two
        int tabDiff = 0;
        for (int dx = 0; dx < (int)(ce.CharWidth() * 4); ++dx) for (int dy = 0; dy < (int)ce.LineHeight(); ++dy)
            if (PixelAt(on, (int)t0.x + dx, (int)t0.y + dy) != PixelAt(off, (int)t0.x + dx, (int)t0.y + dy)) ++tabDiff;
        CHECK(tabDiff > 4);
        ce.SetShowWhitespace(false);
        sim.Frames(2, ui);
        CHECK(SameBitmap(sim.Draw(0, 0, 0), off));
        // the overwrite caret is a block: wider than the 2 px bar
        ce.SetOverwrite(true);
        sim.Frames(2, ui);
        const Bitmap blockShot = sim.Draw(0, 0, 0);
        ce.SetOverwrite(false);
        sim.Frames(2, ui);
        CHECK(!SameBitmap(blockShot, sim.Draw(0, 0, 0)));
    }

    {   // fuzz: the new calls with hostile numbers, flags and unbalanced use, frame after frame; the draw data must stay valid and nothing may crash
        const char* e = std::getenv("OKAYUI_FUZZ_ITERS");
        const int iters = e ? std::max(10, std::atoi(e) / 4) : 400;
        std::uint64_t st = 88172645463325252ull;
        auto next = [&] { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return (unsigned)(st >> 16); };
        auto I = [&](int n) { return n <= 0 ? 0 : (int)(next() % (unsigned)n); };
        auto F = [&]() -> float {
            switch (I(12)) {
                case 0: return 0.0f; case 1: return std::numeric_limits<float>::quiet_NaN(); case 2: return std::numeric_limits<float>::infinity();
                case 3: return -std::numeric_limits<float>::infinity(); case 4: return 1e30f; case 5: return -1e30f; case 6: return -1.0f;
                default: return (float)I(1600) - 400.0f;
            }
        };
        Reset();
        Sim sim(640, 480);
        char buf[24] = "abc";
        SDL_Texture* fake = reinterpret_cast<SDL_Texture*>(static_cast<std::uintptr_t>(0x1000));
        bool valid = true;
        for (int it = 0; it < iters && valid; ++it) {
            sim.Move((float)I(640), (float)I(480));
            sim.down = I(4) == 0; sim.rdown = I(6) == 0; sim.mdown = I(8) == 0; sim.ctrl = I(5) == 0; sim.shift = I(5) == 0;
            if (I(3) == 0) sim.Type(std::string(1, (char)(32 + I(95))));
            if (I(5) == 0) sim.Press((Key)I(Key_COUNT));
            sim.Hold((UI::KeyId)I(UI::KeyId_COUNT), I(2) == 0);
            sim.Frame([&] {
                UI::SetNextWindowPos(UI::Vec2{(float)I(200), (float)I(100)}, UI::Cond_Once);
                UI::SetNextWindowSize(UI::Vec2{300.0f + (float)I(300), 200.0f + (float)I(200)}, UI::Cond_Once);
                if (I(6) == 0) UI::SetNextWindowFocus();
                if (I(6) == 0) UI::SetNextWindowBgAlpha(F());
                if (!UI::Begin("fz", nullptr, I(4) == 0 ? UI::Window_NoScrollbar : 0)) { UI::End(); return; }
                const int n = 3 + I(8);
                for (int k = 0; k < n; ++k) {
                    switch (I(22)) {
                        case 0: if (UI::BeginChild("c", UI::Vec2{F(), F()}, I(64), I(2) ? UI::Window_NoScrollbar : 0)) { UI::Text("child"); if (I(2)) UI::Button("b"); } UI::EndChild(); break;
                        case 1: UI::InputText("##i", buf, (int)sizeof buf, (unsigned)I(4096)); (void)UI::IsItemEdited(); (void)UI::IsItemDeactivatedAfterEdit(); break;
                        case 2: if (UI::BeginTooltip()) { UI::Text("tip"); if (I(2)) UI::Separator(); if (I(5)) UI::EndTooltip(); } break;   // sometimes left open
                        case 3: { UI::ListClipper c; c.Begin(I(3) == 0 ? (int)F() : I(20000), F()); int guard = 0; while (c.Step() && guard++ < 3) for (int i = c.DisplayStart; i < c.DisplayEnd && i < c.DisplayStart + 200; ++i) UI::Text("r"); if (I(2)) c.End(); break; }
                        case 4: (void)UI::IsItemHovered(I(1 << 16)); (void)UI::IsWindowHovered(I(8)); (void)UI::IsWindowFocused(I(8)); (void)UI::IsAnyItemHovered(); break;
                        case 5: if (UI::BeginPopupContextWindow(I(2) ? "ctx" : nullptr, I(300))) { UI::Selectable("x", false); UI::EndPopup(); } break;
                        case 6: { const UI::DrawList& dl = UI::GetForegroundDrawList(); dl.AddRectFlags(UI::Vec2{F(), F()}, UI::Vec2{F(), F()}, 0xff0000ffu, F(), I(512), F()); dl.AddRectFilledFlags(UI::Vec2{F(), F()}, UI::Vec2{F(), F()}, 0xff0000ffu, F(), I(512)); break; }
                        case 7: { const UI::DrawList& dl = UI::GetWindowDrawList(); dl.AddNgon(UI::Vec2{F(), F()}, F(), 0xff00ff00u, I(200) - 20, F()); dl.AddNgonFilled(UI::Vec2{F(), F()}, F(), 0xff00ff00u, I(200) - 20); break; }
                        case 8: { const UI::DrawList& dl = UI::GetForegroundDrawList(); dl.AddImageRounded(I(4) ? fake : nullptr, UI::Vec2{F(), F()}, UI::Vec2{F(), F()}, UI::Vec2{F(), F()}, UI::Vec2{F(), F()}, 0xffffffffu, F(), I(512)); dl.AddImageQuad(fake, UI::Vec2{F(), F()}, UI::Vec2{F(), F()}, UI::Vec2{F(), F()}, UI::Vec2{F(), F()}); break; }
                        case 9: (void)UI::GetMouseDragDelta(I(5) - 1, F()); if (I(3) == 0) UI::ResetMouseDragDelta(I(5) - 1); break;
                        case 10: (void)UI::Shortcut(I(16), (UI::KeyId)(I(UI::KeyId_COUNT + 6) - 3), I(2) == 0); break;
                        case 11: UI::PushMultiItemsWidths(I(30) - 3, F()); for (int i = 0; i < 4; ++i) UI::PopItemWidth(); break;
                        case 12: UI::SetWindowPos(UI::Vec2{F(), F()}); UI::SetWindowSize(UI::Vec2{F(), F()}); UI::SetWindowPos("fz", UI::Vec2{F(), F()}); UI::SetWindowSize("fz", UI::Vec2{F(), F()}); break;
                        case 13: (void)UI::GetStyle(); (void)UI::CalcItemWidth(); (void)UI::GetCursorStartPos(); (void)UI::GetWindowContentRegionMin(); (void)UI::GetMousePosOnOpeningCurrentPopup(); break;
                        case 14: UI::InvisibleButton("ib", F(), F(), I(10)); (void)UI::GetDragDropPayload(); break;
                        case 15: if (UI::BeginDragDropSource()) { int v = 1; UI::SetDragDropPayload("T", &v, sizeof v); UI::EndDragDropSource(); } (void)UI::BeginDragDropTargetCustom(UI::Rect{F(), F(), F(), F()}, 1); break;
                        case 16: UI::Button("x"); UI::SetItemTooltip("t %d", 1); (void)UI::BeginItemTooltip(); break;
                        case 17: { bool o = UI::BeginCombo("co", "pv", I(128)); if (o) UI::EndCombo(); break; }
                        case 18: if (UI::BeginTable("t", 1 + I(4), UI::Table_None)) { UI::TableSetupColumn("a", I(32), F()); UI::TableHeadersRow(); UI::TableNextRow(); UI::TableSetColumnIndex(0); UI::Text("c"); UI::EndTable(); } break;
                        case 19: { bool bb = I(2) == 0; float f = (float)I(100); UI::Checkbox("cb", &bb); UI::DragFloat("df", &f, F(), F(), F()); (void)UI::IsItemEdited(); (void)UI::IsItemDeactivatedAfterEdit(); break; }
                        default: UI::Text("t"); break;
                    }
                }
                UI::End();
            });
            const UI::DrawData dd = UI::GetDrawData();
            const UI::DrawVertex* vs = static_cast<const UI::DrawVertex*>(dd.vertices);
            for (int i = 0; i < dd.indexCount && valid; ++i) if (dd.indices[i] < 0 || dd.indices[i] >= dd.vertexCount) { valid = false; std::printf("  fuzz frame %d: index %d out of range (%d vertices)\n", it, dd.indices[i], dd.vertexCount); }
            for (int i = 0; i < dd.vertexCount && valid; ++i) if (!std::isfinite(vs[i].x) || !std::isfinite(vs[i].y) || !std::isfinite(vs[i].u) || !std::isfinite(vs[i].v)) { valid = false; std::printf("  fuzz frame %d: vertex %d not finite (%g %g %g %g)\n", it, i, vs[i].x, vs[i].y, vs[i].u, vs[i].v); }
            if (I(8) == 0) (void)sim.Draw(0, 0, 0);
        }
        CHECK(valid);
        // an overwrite-mode and a hostile CodeEdit
        UI::CodeEdit ce; ce.SetOverwrite(true); ce.SetShowWhitespace(true);
        ce.SetText("a\tb  c\n\n\xe2\x82\xac x");
        for (int it = 0; it < 120; ++it) {
            sim.Move((float)I(640), (float)I(480)); sim.down = I(3) == 0;
            if (I(2)) sim.Type(std::string(1, (char)(32 + I(95))));
            if (I(4) == 0) sim.Type("\xe2\x82\xac");
            sim.Hold(UI::KeyId_Insert, I(3) == 0); sim.Hold(UI::KeyId_Alt, I(6) == 0); sim.shift = I(3) == 0; sim.ctrl = I(4) == 0;
            if (I(4) == 0) sim.Press((Key)I(Key_COUNT));
            sim.Frame([&] { UI::Begin("W", 10, 10, 600, 360); ce.Draw("ed", 560, 300); UI::End(); });
        }
        sim.shift = sim.ctrl = sim.down = false;
        CHECK(ce.Caret().line >= 0 && ce.Caret().line < ce.LineCount() && ce.Caret().col <= ce.LineLength(ce.Caret().line));
    }

    TEST_MAIN_RESULT();
}
