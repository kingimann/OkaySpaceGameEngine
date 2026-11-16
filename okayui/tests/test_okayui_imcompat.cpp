// The ImGui-shaped names and calls in OkayUIImCompat.hpp (docs/own_ui_toolkit.md section 4.36): mouse buttons and drag deltas,
// hover and focus flags, child flags, edit marks, tooltip scopes, context popups, rounded-corner draw flags and reorderable tabs.
// Headless, through the same frame driver as the other OkayUI tests. Written from how these calls behave, not from any other project.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIImCompat.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {
struct Rects { UI::Rect r[8]; };
void BeginWin(const char* name = "w", float x = 10, float y = 10, float w = 300, float h = 260) {
    UI::BareNextWindow();
    UI::Begin(name, x, y, w, h);
}
}

int main() {
    RUN_SUITE("okayui imcompat");
    {   // the names exist and have the values the callers rely on (only their relations matter)
        CHECK(UI::MouseButton_Left == 0 && UI::MouseButton_Right == 1 && UI::MouseButton_Middle == 2);
        CHECK(UI::Key_UpArrow == UI::KeyId_Up && UI::Key_DownArrow == UI::KeyId_Down && UI::Key_KeypadEnter == UI::KeyId_Enter);
        CHECK((UI::HoveredFlags_RectOnly & UI::HoveredFlags_AllowWhenDisabled) == 0);   // disabled hover is its own question
        CHECK(UI::HoveredFlags_RootAndChildWindows == (UI::HoveredFlags_RootWindow | UI::HoveredFlags_ChildWindows));
        CHECK(UI::DrawFlags_RoundCornersTop == (UI::DrawFlags_RoundCornersTopLeft | UI::DrawFlags_RoundCornersTopRight));
        CHECK(UI::DrawFlags_RoundCornersAll == (UI::DrawFlags_RoundCornersTop | UI::DrawFlags_RoundCornersBottom));
        CHECK(UI::Dir_Down == 3 && UI::Dir_None == -1);
    }
    {   // the middle button, drag deltas
        Reset();
        Sim sim(400, 300);
        auto ui = [&] { BeginWin(); UI::End(); };
        bool d1 = false, c2 = false, r2 = false;
        sim.Move(100, 100); sim.Frame(ui);
        sim.mdown = true; sim.Frame([&] { ui(); d1 = UI::IsMouseDown(UI::MouseButton_Middle); c2 = UI::IsMouseClicked(UI::MouseButton_Middle); });
        CHECK(d1 && c2);
        CHECK(!UI::IsMouseDown(UI::MouseButton_Left));
        sim.Move(130, 120);
        UI::Vec2 d;
        sim.Frame([&] { ui(); d = UI::GetMouseDragDelta(UI::MouseButton_Middle); });
        CHECK(d.x == 30 && d.y == 20);
        sim.Move(102, 101);   // inside the 4 px threshold
        sim.Frame([&] { ui(); d = UI::GetMouseDragDelta(UI::MouseButton_Middle); });
        CHECK(d.x == 0 && d.y == 0);
        sim.Move(150, 140);
        sim.Frame([&] { ui(); UI::ResetMouseDragDelta(UI::MouseButton_Middle); d = UI::GetMouseDragDelta(UI::MouseButton_Middle); });
        CHECK(d.x == 0 && d.y == 0);                                   // restarted from here
        sim.Move(170, 140);
        sim.Frame([&] { ui(); d = UI::GetMouseDragDelta(UI::MouseButton_Middle); });
        CHECK(d.x == 20 && d.y == 0);
        sim.mdown = false;
        sim.Frame([&] { ui(); r2 = UI::IsMouseReleased(UI::MouseButton_Middle); d = UI::GetMouseDragDelta(UI::MouseButton_Middle); });
        CHECK(r2 && d.x == 0 && d.y == 0);                             // released: no drag
        // the left and right buttons measure from their own press
        sim.Move(10, 10); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Move(60, 40); sim.Frame([&] { ui(); d = UI::GetMouseDragDelta(0); });
        CHECK(d.x == 50 && d.y == 30);
        CHECK(UI::GetMouseDragDelta(7).x == 0 && UI::GetMouseDragDelta(-1).y == 0);   // an unknown button is quiet
    }
    {   // IsItemHovered flags
        Reset();
        Sim sim(400, 300);
        UI::Rect ra, rb;
        bool a0 = false, b0 = false, bAbi = false, dis0 = false, disAllow = false;
        auto ui = [&] {
            BeginWin();
            UI::Button("A"); ra = UI::GetItemRect(); a0 = UI::IsItemHovered(0);
            UI::Button("B"); rb = UI::GetItemRect(); b0 = UI::IsItemHovered(0); bAbi = UI::IsItemHovered(UI::HoveredFlags_AllowWhenBlockedByActiveItem);
            UI::BeginDisabled(true);
            UI::Button("C"); dis0 = UI::IsItemHovered(0); disAllow = UI::IsItemHovered(UI::HoveredFlags_AllowWhenDisabled);
            UI::EndDisabled();
            UI::End();
        };
        sim.Frame(ui);
        sim.Move(CenterX(ra), CenterY(ra)); sim.Frame(ui);
        CHECK(a0 && !b0);
        sim.Down(); sim.Frame(ui);                                   // A is held
        sim.Move(CenterX(rb), CenterY(rb)); sim.Frame(ui);
        CHECK(!b0);                                                  // another item is held: not hovered
        CHECK(bAbi);                                                 // unless the flag says so
        sim.Up(); sim.Frame(ui); sim.Frame(ui);
        CHECK(b0);
        // the disabled button: find it below B
        sim.Move(CenterX(rb), rb.y + rb.h + 24); sim.Frame(ui);
        CHECK(!dis0 && disAllow);
    }
    {   // hover delays and stationary
        Reset();
        Sim sim(400, 300);
        UI::Rect ra;
        bool shortOk = false, normalOk = false, still = false;
        auto ui = [&] {
            BeginWin();
            UI::Button("A"); ra = UI::GetItemRect();
            shortOk = UI::IsItemHovered(UI::HoveredFlags_DelayShort);
            normalOk = UI::IsItemHovered(UI::HoveredFlags_DelayNormal);
            still = UI::IsItemHovered(UI::HoveredFlags_Stationary);
            UI::End();
        };
        sim.Frame(ui);
        sim.Move(CenterX(ra), CenterY(ra));
        int shortAt = -1, normalAt = -1, stillAt = -1;
        for (int i = 0; i < 60; ++i) {
            sim.Frame(ui);
            if (shortOk && shortAt < 0) shortAt = i;
            if (normalOk && normalAt < 0) normalAt = i;
            if (still && stillAt < 0) stillAt = i;
        }
        CHECK(shortAt >= 7 && shortAt <= 11);     // about 0.15 s at 60 frames a second
        CHECK(normalAt >= 22 && normalAt <= 26);  // about 0.4 s
        CHECK(stillAt >= 7 && stillAt <= 11);     // 0.15 s without moving
        sim.Move(CenterX(ra) + 3, CenterY(ra));   // moving restarts both
        sim.Frame(ui);
        CHECK(!still);
        sim.Move(2000, 2000); sim.Frame(ui);      // leaving resets the delay
        sim.Move(CenterX(ra), CenterY(ra)); sim.Frame(ui);
        CHECK(!normalOk);
    }
    {   // window hover and focus flags
        Reset();
        Sim sim(400, 300);
        bool h0 = false, hAny = false, hHeld = false, hHeldOk = false, f0 = false, fAny = false;
        float v = 0.5f; UI::Rect rs;
        auto ui = [&] {
            BeginWin("w", 10, 10, 200, 200);
            h0 = UI::IsWindowHovered(0); hAny = UI::IsWindowHovered(UI::HoveredFlags_AnyWindow);
            f0 = UI::IsWindowFocused(UI::FocusedFlags_RootAndChildWindows); fAny = UI::IsWindowFocused(UI::FocusedFlags_AnyWindow);
            UI::SliderFloat("s", &v, 0, 1); rs = UI::GetItemRect();
            hHeld = UI::IsWindowHovered(0); hHeldOk = UI::IsWindowHovered(UI::HoveredFlags_AllowWhenBlockedByActiveItem);
            UI::End();
        };
        sim.Frame(ui);
        sim.Move(100, 100); sim.Frames(2, ui);
        CHECK(h0 && hAny);
        sim.Move(380, 280); sim.Frames(2, ui);
        CHECK(!h0 && !hAny);
        sim.Move(rs.x + 20, CenterY(rs)); sim.Frames(2, ui);
        sim.Down(); sim.Frames(2, ui);
        CHECK(!hHeld && hHeldOk);                          // a held slider: only the flag sees the window hovered
        sim.Up(); sim.Frames(2, ui);
        CHECK(fAny);
    }
    {   // child flags: border, auto resize, negative size
        Reset();
        Sim sim(400, 400);
        UI::Rect rb, rn, ra;
        const unsigned bg = Rgba(255, 0, 0);
        auto ui = [&] {
            BeginWin("w", 0, 0, 400, 400);
            UI::PushStyleColor(UI::Col_ChildBg, UI::RGBA(255, 0, 0));
            if (UI::BeginChild("bordered", UI::Vec2{100, 40}, UI::ChildFlags_Borders)) { UI::Text("x"); UI::EndChild(); }
            rb = UI::GetItemRect();
            if (UI::BeginChild("plain", UI::Vec2{100, 40}, UI::ChildFlags_None)) { UI::Text("x"); UI::EndChild(); }
            rn = UI::GetItemRect();
            if (UI::BeginChild("auto", UI::Vec2{100, 0}, UI::ChildFlags_Borders | UI::ChildFlags_AutoResizeY)) { UI::Text("a"); UI::Text("b"); UI::Text("c"); UI::EndChild(); }
            ra = UI::GetItemRect();
            UI::PopStyleColor();
            UI::End();
        };
        sim.Frames(4, ui);
        CHECK(rb.w == 100 && rb.h == 40);
        CHECK(rn.w == 100 && rn.h == 40);
        const Bitmap& b = sim.Draw();
        // inside the bordered child there is a panel; the plain child has none (its pixels are the window's, not the panel colour)
        const unsigned inBordered = PixelAt(b, (int)(rb.x + 60), (int)(rb.y + rb.h - 6));
        const unsigned inPlain = PixelAt(b, (int)(rn.x + 60), (int)(rn.y + rn.h - 6));
        CHECK(inBordered == bg);
        CHECK(inPlain != bg);                                       // the plain child draws no panel
        {   // the auto child is as tall as its three rows plus its padding, and does not scroll
            const float row = UI::GetFrameHeight();
            CHECK(ra.h > 3 * 8 && ra.h < 3 * (row + 12) + 20);
            const float h1 = ra.h;
            sim.Frames(3, ui);
            CHECK(ra.h == h1);                              // settled
        }
        UI::Rect rneg;
        sim.Frame([&] {
            BeginWin("w", 0, 0, 400, 400);
            if (UI::BeginChild("neg", UI::Vec2{-50, 60}, UI::ChildFlags_Borders)) UI::EndChild();
            rneg = UI::GetItemRect();
            UI::End();
        });
        CHECK(rneg.w > 200 && rneg.w < 400 && rneg.h == 60);
        // hostile numbers
        const float nan = std::numeric_limits<float>::quiet_NaN();
        sim.Frame([&] { BeginWin(); if (UI::BeginChild("n", UI::Vec2{nan, nan}, 0xffff, 0xffff)) UI::EndChild(); UI::End(); });
    }
    {   // edit marks: slider drag, drag with no movement, checkbox, text field
        Reset();
        Sim sim(400, 300);
        float v = 0.5f; bool chk = false; float dv = 1.0f; char buf[32] = "abc";
        UI::Rect rs, rc, rd, rt;
        bool sEdited = false, sDone = false, cEdited = false, cDone = false, dDone = false, tDone = false, tEdited = false;
        auto ui = [&] {
            BeginWin("w", 0, 0, 400, 300);
            UI::SliderFloat("s", &v, 0, 1); rs = UI::GetItemRect(); sEdited = UI::IsItemEdited(); sDone = UI::IsItemDeactivatedAfterEdit();
            UI::Checkbox("c", &chk); rc = UI::GetItemRect(); cEdited = UI::IsItemEdited(); cDone = UI::IsItemDeactivatedAfterEdit();
            UI::DragFloat("d", &dv, 0.1f, 0, 10); rd = UI::GetItemRect(); dDone = UI::IsItemDeactivatedAfterEdit();
            UI::InputText("t", buf, sizeof buf); rt = UI::GetItemRect(); tDone = UI::IsItemDeactivatedAfterEdit(); tEdited = UI::IsItemEdited();
            UI::End();
        };
        sim.Frames(2, ui);
        sim.Move(rs.x + rs.w * 0.2f, CenterY(rs)); sim.Frame(ui); sim.Down(); sim.Frame(ui);
        sim.Move(rs.x + rs.w * 0.4f, CenterY(rs)); sim.Frame(ui);
        CHECK(sEdited);                                  // the value moved this frame
        CHECK(!sDone);                                   // still held
        sim.Up(); sim.Frame(ui);
        CHECK(sDone);                                    // let go after an edit
        sim.Frame(ui);
        CHECK(!sDone && !sEdited);                       // only that one frame
        // a click on the check box
        sim.Move(CenterX(rc) - rc.w * 0.4f, CenterY(rc)); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
        CHECK(chk && cDone);
        // a press and release on the drag field without moving: not an edit
        sim.Move(CenterX(rd), CenterY(rd)); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
        CHECK(!dDone && dv == 1.0f);
        // a text field: typing marks edited; clicking away ends it
        sim.Move(CenterX(rt), CenterY(rt)); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
        sim.Type("z"); sim.Frame(ui);
        CHECK(tEdited && std::string(buf).find('z') != std::string::npos);
        sim.Frame(ui);
        CHECK(!tDone);
        sim.Move(380, 280); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
        // (watch every frame of the same sequence: the text field loses the keyboard on the press frame)
        Reset();
        Sim s2(400, 300); strcpy(buf, "abc"); bool any = false;
        s2.Frames(2, ui);
        s2.Move(CenterX(rt), CenterY(rt)); s2.Frame(ui); s2.Down(); s2.Frame(ui); s2.Up(); s2.Frame(ui);
        s2.Type("q"); s2.Frame(ui);
        s2.Move(380, 280); s2.Frame(ui); s2.Down(); s2.Frame([&] { ui(); any = any || tDone; }); s2.Up(); s2.Frame([&] { ui(); any = any || tDone; });
        CHECK(any);
    }
    {   // CalcItemWidth
        Reset();
        Sim sim(400, 300);
        float w0 = 0, w1 = 0, w2 = 0;
        sim.Frame([&] {
            BeginWin("w", 0, 0, 400, 300);
            w0 = UI::CalcItemWidth();
            UI::SetNextItemWidth(120); w1 = UI::CalcItemWidth();
            UI::Dummy(1, 1);                                   // the one-shot width goes to the item that follows
            UI::PushItemWidth(80); w2 = UI::CalcItemWidth(); UI::PopItemWidth();
            UI::End();
        });
        CHECK(w0 > 300 && w0 <= 400);
        CHECK(w1 == 120 && w2 == 80);
        CHECK(UI::CalcItemWidth() == 0);                 // outside a window
    }
    {   // a tooltip with widgets in it
        Reset();
        Sim sim(400, 300);
        UI::Rect ra;
        bool began = false, nested = true;
        auto ui = [&] {
            BeginWin("w", 0, 0, 400, 300);
            UI::Button("hover me"); ra = UI::GetItemRect();
            if (UI::IsItemHovered() && UI::BeginTooltip()) {
                began = true;
                UI::Text("first line"); UI::Text("second line");
                nested = UI::BeginTooltip();            // not inside another tooltip
                UI::EndTooltip();
            }
            UI::End();
        };
        sim.Frame(ui);
        sim.Move(CenterX(ra), CenterY(ra)); sim.Frames(4, ui);
        CHECK(began && !nested);
        const unsigned bg = ThemeRgba(UI::Style().bg);
        const Bitmap& b = sim.Draw();
        // the box sits down-right of the pointer and has text pixels in it
        const int x0 = (int)sim.mx + 16, y0 = (int)sim.my + 16;
        CHECK(CountNot(b, x0, y0, x0 + 70, y0 + 28, bg) > 40);
        // the button under the pointer is still hovered while the tooltip shows (the tooltip does not take the pointer)
        bool still = false;
        sim.Frame([&] { BeginWin("w", 0, 0, 400, 300); UI::Button("hover me"); still = UI::IsItemHovered(UI::HoveredFlags_RectOnly); UI::End(); });
        CHECK(still);
        sim.Move(380, 280); began = false; sim.Frames(2, ui);
        CHECK(!began);
        UI::EndTooltip();                                // unbalanced end: harmless
        sim.Frame([&] { BeginWin(); UI::BeginItemTooltip(); UI::End(); });
    }
    {   // context popups: window and item, buttons, NoOpenOverItems
        Reset();
        Sim sim(400, 300);
        UI::Rect rb; bool open = false, anyOpen = false;
        auto ui = [&] {
            BeginWin("w", 0, 0, 400, 300);
            UI::Button("item"); rb = UI::GetItemRect();
            open = UI::BeginPopupContextWindow("ctx");
            if (open) { UI::Text("menu"); UI::EndPopup(); }
            anyOpen = UI::IsPopupOpen(nullptr, UI::PopupFlags_AnyPopupId);
            UI::End();
        };
        sim.Frame(ui);
        CHECK(!anyOpen);
        sim.Move(300, 200); sim.Frame(ui); sim.rdown = true; sim.Frame(ui); sim.rdown = false; sim.Frame(ui);
        CHECK(open && anyOpen);
        sim.Move(380, 290); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);   // a click away closes it
        CHECK(!open);
        // over an item it opens too, unless NoOpenOverItems
        bool open2 = false;
        auto ui2 = [&] {
            BeginWin("w", 0, 0, 400, 300);
            UI::Button("item"); rb = UI::GetItemRect();
            open2 = UI::BeginPopupContextWindow("ctx2", UI::PopupFlags_MouseButtonRight | UI::PopupFlags_NoOpenOverItems);
            if (open2) { UI::Text("menu"); UI::EndPopup(); }
            UI::End();
        };
        sim.Frame(ui2);
        sim.Move(CenterX(rb), CenterY(rb)); sim.Frame(ui2); sim.rdown = true; sim.Frame(ui2); sim.rdown = false; sim.Frame(ui2);
        CHECK(!open2);
        // item context popup on the left button
        bool item = false;
        auto ui3 = [&] {
            BeginWin("w", 0, 0, 400, 300);
            UI::Button("item");
            item = UI::BeginPopupContextItem("ictx", UI::PopupFlags_MouseButtonLeft);
            if (item) { UI::Text("item menu"); UI::EndPopup(); }
            UI::End();
        };
        sim.Move(CenterX(rb), CenterY(rb)); sim.Frame(ui3); sim.Down(); sim.Frame(ui3); sim.Up(); sim.Frame(ui3);
        CHECK(item);
    }
    {   // rounded corners on chosen corners, and n-gons
        Reset();
        Sim sim(200, 200);
        const UI::Color32 red = UI::RGBA(255, 0, 0, 255);
        const UI::DrawList& dl = UI::GetForegroundDrawList();
        auto ui = [&] {
            dl.AddRectFilled({20, 20}, {80, 80}, red, 20.0f, UI::DrawFlags_RoundCornersTop);
            dl.AddRectFilled({110, 20}, {170, 80}, red, 20.0f, UI::DrawFlags_RoundCornersBottom);
            dl.AddRect({20, 110}, {80, 170}, red, 20.0f, UI::DrawFlags_RoundCornersLeft, 2.0f);
            dl.AddRectFilled({110, 110}, {170, 170}, red, 20.0f, UI::DrawFlags_RoundCornersNone);
        };
        sim.Frame(ui);
        const Bitmap& b = sim.Draw();
        const unsigned R = Rgba(255, 0, 0), K = Rgba(0, 0, 0);
        CHECK(PixelAt(b, 21, 21) == K && PixelAt(b, 78, 21) == K);    // top corners cut away
        CHECK(PixelAt(b, 21, 78) == R && PixelAt(b, 78, 78) == R);    // bottom corners square
        CHECK(PixelAt(b, 111, 21) == R && PixelAt(b, 168, 21) == R);
        CHECK(PixelAt(b, 111, 78) == K && PixelAt(b, 168, 78) == K);
        CHECK(PixelAt(b, 21, 111) == K && PixelAt(b, 21, 168) == K);  // left corners rounded: the outline leaves them empty
        CHECK(PixelAt(b, 78, 111) == R && PixelAt(b, 78, 168) == R || PixelAt(b, 79, 111) == R);   // right corners square
        CHECK(PixelAt(b, 111, 111) == R && PixelAt(b, 168, 168) == R);   // none rounded: a square
        CHECK(PixelAt(b, 50, 50) == R && PixelAt(b, 140, 50) == R);
        // all four corners given: the same picture as the plain call
        Reset(); Sim s2(200, 200);
        s2.Frame([&] { UI::GetForegroundDrawList().AddRectFilled({20, 20}, {80, 80}, red, 12.0f); });
        const Bitmap plain = s2.Draw();
        Reset(); Sim s3(200, 200);
        s3.Frame([&] { UI::GetForegroundDrawList().AddRectFilled({20, 20}, {80, 80}, red, 12.0f, UI::DrawFlags_RoundCornersAll); });
        const Bitmap& flagged = s3.Draw();
        CHECK(plain.rgba == flagged.rgba);
        // n-gon: a square (4 sides) drawn as a diamond: the bounding box corners stay empty, the middle is filled
        Reset(); Sim s4(200, 200);
        s4.Frame([&] { UI::GetForegroundDrawList().AddNgonFilled({100, 100}, 40, red, 4); UI::GetForegroundDrawList().AddNgon({100, 100}, 60, red, 3, 2.0f); });
        const Bitmap& g = s4.Draw();
        CHECK(PixelAt(g, 100, 100) == R);
        CHECK(PixelAt(g, 62, 62) == K);      // the corner of the box around the diamond
        CHECK(PixelAt(g, 138, 100) == R);    // a corner of the diamond: first corner points right
        // hostile
        const float nan = std::numeric_limits<float>::quiet_NaN();
        s4.Frame([&] { const UI::DrawList& d = UI::GetForegroundDrawList(); d.AddRect({nan, 0}, {10, 10}, red, nan, 0xffff, nan); d.AddRectFilled({0, 0}, {10, 10}, red, -5.0f, -1); d.AddNgon({0, 0}, nan, red, -3); d.AddNgonFilled({0, 0}, 5, red, 100000); });
    }
    {   // tabs: the pure drag target
        const float w[4] = {50, 60, 70, 80};
        CHECK(UI::TabDragTarget(w, 4, 0, 0, 2, 10) == 0);
        CHECK(UI::TabDragTarget(w, 4, 0, 0, 2, 60) == 1);       // over the second tab
        CHECK(UI::TabDragTarget(w, 4, 0, 0, 2, 5000) == 3);     // past the end: last
        CHECK(UI::TabDragTarget(w, 4, 3, 0, 2, -50) == 0);      // before the start: first
        CHECK(UI::TabDragTarget(w, 4, 9, 0, 2, 10) == 9);       // bad index: unchanged
        CHECK(UI::TabDragTarget(nullptr, 4, 1, 0, 2, 10) == 1);
        CHECK(UI::TabDragTarget(w, 0, 0, 0, 2, 10) == 0);
        CHECK(UI::TabDragTarget(w, 4, 1, 0, 2, std::numeric_limits<float>::quiet_NaN()) == 1);
    }
    {   // a reorderable tab bar: drag A to the right of B; the order is kept; the selection follows the click
        Reset();
        Sim sim(500, 300);
        UI::Rect ra, rb, rc; bool aOn = false, bOn = false, cOn = false;
        auto ui = [&] {
            BeginWin("w", 0, 0, 500, 300);
            if (UI::BeginTabBar("bar", UI::TabBarFlags_Reorderable)) {
                aOn = UI::BeginTabItem("Alpha"); ra = UI::GetItemRect(); if (aOn) UI::EndTabItem();
                bOn = UI::BeginTabItem("Beta");  rb = UI::GetItemRect(); if (bOn) UI::EndTabItem();
                cOn = UI::BeginTabItem("Gamma"); rc = UI::GetItemRect(); if (cOn) UI::EndTabItem();
                UI::EndTabBar();
            }
            UI::End();
        };
        sim.Frames(3, ui);
        CHECK(ra.x < rb.x && rb.x < rc.x);
        CHECK(aOn && !bOn && !cOn);
        sim.Move(CenterX(ra), CenterY(ra)); sim.Frame(ui); sim.Down(); sim.Frame(ui);
        sim.Move(CenterX(rb) + 4, CenterY(rb)); sim.Frames(2, ui);   // over Beta: they swap
        CHECK(rb.x < ra.x && ra.x < rc.x);
        sim.Move(rc.x + rc.w - 2, CenterY(rc)); sim.Frames(2, ui);   // over Gamma's right end: Alpha goes last
        CHECK(rb.x < rc.x && rc.x < ra.x);
        sim.Up(); sim.Frames(3, ui);
        CHECK(rb.x < rc.x && rc.x < ra.x);                           // stays after the release
        CHECK(aOn);                                                  // A was clicked: it is the chosen one
        // without the flag nothing moves
        Reset();
        Sim s2(500, 300);
        UI::Rect pa, pb;
        auto ui2 = [&] {
            BeginWin("w", 0, 0, 500, 300);
            if (UI::BeginTabBar("plain")) {
                if (UI::BeginTabItem("Alpha")) UI::EndTabItem(); pa = UI::GetItemRect();
                if (UI::BeginTabItem("Beta")) UI::EndTabItem(); pb = UI::GetItemRect();
                UI::EndTabBar();
            }
            UI::End();
        };
        s2.Frames(3, ui2);
        s2.Move(CenterX(pa), CenterY(pa)); s2.Frame(ui2); s2.Down(); s2.Frame(ui2); s2.Move(CenterX(pb), CenterY(pb)); s2.Frames(3, ui2);
        CHECK(pa.x < pb.x);
    }
    {   // closable tabs: middle click closes; a vanished tab leaves no gap; new tabs can take the selection
        Reset();
        Sim sim(500, 300);
        bool aOpen = true, bOpen = true, withC = false;
        UI::Rect ra, rb, rc; bool cOn = false;
        auto ui = [&] {
            BeginWin("w", 0, 0, 500, 300);
            if (UI::BeginTabBar("bar", UI::TabBarFlags_Reorderable | UI::TabBarFlags_AutoSelectNewTabs)) {
                if (aOpen && UI::BeginTabItem("Alpha", &aOpen)) UI::EndTabItem(); ra = UI::GetItemRect();
                if (bOpen && UI::BeginTabItem("Beta", &bOpen)) UI::EndTabItem(); rb = UI::GetItemRect();
                if (withC) { cOn = UI::BeginTabItem("Gamma"); rc = UI::GetItemRect(); if (cOn) UI::EndTabItem(); }
                UI::EndTabBar();
            }
            UI::End();
        };
        sim.Frames(3, ui);
        const float betaX = rb.x;
        sim.Move(CenterX(ra) - 6, CenterY(ra)); sim.Frame(ui); sim.mdown = true; sim.Frame(ui); sim.mdown = false; sim.Frames(3, ui);
        CHECK(!aOpen && bOpen);                                      // the middle click closed Alpha
        CHECK(rb.x < betaX);                                         // and Beta moved left into the gap
        withC = true; sim.Frames(3, ui);
        CHECK(cOn);                                                  // a new tab after the first frame became the chosen one
    }
    {   // table columns: hide, default hide, locked columns, header menu, per-column NoResize, hovered column
        Reset();
        Sim sim(600, 300);
        float cellX[3] = {-1, -1, -1}; bool cellRet[3] = {true, true, true};
        UI::Rect hdr;
        int hovered = -9; int cnt = 0; std::string nm; int fl = -1; bool en1 = true;
        bool hideB = false, hideC = false;
        auto ui = [&] {
            BeginWin("w", 0, 0, 600, 300);
            { const UI::Vec2 tp = UI::GetCursorScreenPos(); hdr = UI::Rect{tp.x, tp.y, 100, UI::GetFrameHeight()}; }
            if (UI::BeginTable("t", 3, UI::Table_Hideable)) {
                UI::TableSetupColumn("Name", UI::Col_NoHide);
                UI::TableSetupColumn("Kind", hideB ? UI::Col_DefaultHide : 0);
                UI::TableSetupColumn("Size", hideC ? UI::Col_DefaultHide : 0);
                UI::TableHeadersRow();
                UI::TableNextRow();
                for (int c = 0; c < 3; ++c) { cellRet[c] = UI::TableNextColumn(); cellX[c] = UI::GetCursorScreenPos().x; UI::Text("x"); }
                hovered = UI::TableGetHoveredColumn(); cnt = UI::TableGetColumnCount(); nm = UI::TableGetColumnName(1); fl = UI::TableGetColumnFlags(0);
                en1 = UI::TableGetColumnIsEnabled(1);
                UI::EndTable();
            }
            UI::End();
        };
        sim.Frames(3, ui);
        CHECK(cellRet[0] && cellRet[1] && cellRet[2]);
        CHECK(cellX[0] < cellX[1] && cellX[1] < cellX[2] && cnt == 3 && nm == "Kind" && (fl & UI::Col_NoHide) && en1);
        sim.Move(cellX[1] + 4, 40); sim.Frames(2, ui);
        CHECK(hovered == 1);
        const float x1 = cellX[1], x2 = cellX[2];
        // hide Kind from code (next frame): the third column slides left into its place and the cell says it is hidden
        sim.Frame([&] { BeginWin("w", 0, 0, 600, 300); if (UI::BeginTable("t", 3, UI::Table_Hideable)) {
            UI::TableSetupColumn("Name", UI::Col_NoHide); UI::TableSetupColumn("Kind"); UI::TableSetupColumn("Size"); UI::TableHeadersRow();
            CHECK(UI::TableSetColumnEnabled(1, false));
            CHECK(!UI::TableSetColumnEnabled(0, false));              // NoHide
            UI::EndTable(); } UI::End(); });
        sim.Frames(3, ui);
        CHECK(!cellRet[1] && cellRet[0] && cellRet[2]);
        CHECK(cellX[2] < x2 && cellX[2] > x1 - 4 && cellX[1] < 0);    // Size took Kind's place; Kind's cell is off screen
        CHECK(!en1);
        // show it again: the old layout is back
        sim.Frame([&] { BeginWin("w", 0, 0, 600, 300); if (UI::BeginTable("t", 3, UI::Table_Hideable)) {
            UI::TableSetupColumn("Name", UI::Col_NoHide); UI::TableSetupColumn("Kind"); UI::TableSetupColumn("Size"); UI::TableHeadersRow();
            CHECK(UI::TableSetColumnEnabled(1, true)); UI::EndTable(); } UI::End(); });
        sim.Frames(3, ui);
        CHECK(cellRet[1] && std::fabs(cellX[1] - x1) < 1 && std::fabs(cellX[2] - x2) < 1);
        // the header menu: a right click on the header opens it, unchecking Size hides it
        sim.Move(hdr.x + 20, hdr.y + 6); sim.Frame(ui); sim.rdown = true; sim.Frame(ui); sim.rdown = false; sim.Frames(2, ui);
        CHECK(UI::IsPopupOpen(nullptr, UI::PopupFlags_AnyPopupId));
        // a column that starts hidden
        Reset(); Sim s2(600, 300); hideB = true;
        s2.Frames(3, ui);
        CHECK(!cellRet[1] && cellRet[0] && cellRet[2] && !en1);
        // hostile: out-of-range columns, outside a table
        CHECK(!UI::TableSetColumnEnabled(0, false) && UI::TableGetHoveredColumn() == -1 && UI::TableGetColumnCount() == 0);
        s2.Frame([&] { BeginWin(); if (UI::BeginTable("h", 2, UI::Table_Hideable)) { UI::TableSetupColumn("a"); UI::TableSetupColumn("b");
            UI::TableHeadersRow(); CHECK(!UI::TableSetColumnEnabled(5, false)); CHECK(!UI::TableSetColumnEnabled(-2, true));
            CHECK(std::string(UI::TableGetColumnName(9)) == ""); CHECK(UI::TableGetColumnFlags(9) == 0);
            // hiding every column is refused: one stays
            CHECK(UI::TableSetColumnEnabled(0, false)); CHECK(!UI::TableSetColumnEnabled(1, false)); UI::EndTable(); } UI::End(); });
    }
    {   // a column with NoResize keeps its width when its border is dragged; a normal one changes
        Reset();
        Sim sim(600, 300);
        float xs[3] = {0, 0, 0}; bool noResize = true;
        auto ui = [&] {
            BeginWin("w", 0, 0, 600, 300);
            if (UI::BeginTable("t", 3, UI::Table_None)) {
                UI::TableSetupColumn("A", noResize ? UI::Col_NoResize : 0); UI::TableSetupColumn("B"); UI::TableSetupColumn("C");
                UI::TableHeadersRow(); UI::TableNextRow();
                for (int c = 0; c < 3; ++c) { UI::TableNextColumn(); xs[c] = UI::GetCursorScreenPos().x; UI::Text("x"); }
                UI::EndTable();
            }
            UI::End();
        };
        sim.Frames(3, ui);
        const float border1 = xs[1] - 4;
        sim.Move(border1, 40); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Move(border1 + 40, 40); sim.Frames(2, ui); sim.Up(); sim.Frames(2, ui);
        CHECK(std::fabs(xs[1] - 4 - border1) < 1);                 // NoResize on A: the border between A and B did not move
        Reset(); Sim s2(600, 300); noResize = false;
        s2.Frames(3, ui);
        const float b2 = xs[1] - 4;
        s2.Move(b2, 40); s2.Frame(ui); s2.Down(); s2.Frame(ui); s2.Move(b2 + 40, 40); s2.Frames(2, ui); s2.Up(); s2.Frames(2, ui);
        CHECK(xs[1] - 4 > b2 + 20);                                // and a normal one moves
    }
    {   // pictures (golden images): a reordered tab bar, a table with a hidden column and its header menu, a tooltip with widgets next to child
        // panels with and without borders, and the rounded-corner and n-gon rectangles
        auto Check = [](const char* name, const Bitmap& b) {
            std::string why;
            const bool ok = MatchesGolden(name, b, &why);
            if (!ok) std::cerr << "  golden " << name << ": " << why << " (actual written as " << name << ".actual.png)\n";
            CHECK(ok);
        };
        {
            Reset(); UI::UseLauncherLook(true);
            Sim sim(520, 150);
            UI::Rect ra, rb, rc;
            auto ui = [&] {
                BeginWin("tabs", 6, 6, 508, 138);
                if (UI::BeginTabBar("bar", UI::TabBarFlags_Reorderable)) {
                    if (UI::BeginTabItem("Scene")) { ra = UI::GetItemRect(); UI::Text("the Scene tab, which was dragged to the end of the bar"); UI::EndTabItem(); } else ra = UI::GetItemRect();
                    if (UI::BeginTabItem("Game")) UI::EndTabItem(); rb = UI::GetItemRect();
                    bool unsaved = true; (void)unsaved;
                    if (UI::BeginTabItem("Script", nullptr, UI::TabItem_UnsavedDocument)) UI::EndTabItem(); rc = UI::GetItemRect();
                    UI::EndTabBar();
                }
                UI::End();
            };
            sim.Frames(3, ui);
            sim.Move(CenterX(ra), CenterY(ra)); sim.Frame(ui); sim.Down(); sim.Frame(ui);
            sim.Move(rc.x + rc.w - 3, CenterY(rc)); sim.Frames(3, ui); sim.Up(); sim.Frames(3, ui);
            CHECK(ra.x > rb.x && ra.x > rc.x);
            Check("imcompat_tabs_reordered", sim.Draw(28, 30, 36));
            UI::UseLauncherLook(false);
        }
        {
            Reset(); UI::UseLauncherLook(true);
            Sim sim(420, 190);
            UI::Rect hdr;
            auto ui = [&] {
                BeginWin("tbl", 6, 6, 408, 178);
                { const UI::Vec2 tp = UI::GetCursorScreenPos(); hdr = UI::Rect{tp.x, tp.y, 100, UI::GetFrameHeight()}; }
                if (UI::BeginTable("files", 4, UI::Table_Hideable | UI::Table_RowBg)) {
                    UI::TableSetupColumn("Name", UI::Col_NoHide, 2.0f); UI::TableSetupColumn("Kind", UI::Col_DefaultHide, 1.0f);
                    UI::TableSetupColumn("Size", 0, 1.0f); UI::TableSetupColumn("Modified", 0, 1.5f);
                    UI::TableHeadersRow();
                    const char* names[] = {"Player.okayscene", "hero.png", "theme.wav"}; const char* kinds[] = {"Scene", "Texture", "Audio"};
                    const char* sizes[] = {"12 KB", "480 KB", "2.1 MB"}; const char* when[] = {"today", "yesterday", "3 days ago"};
                    for (int r = 0; r < 3; ++r) { UI::TableNextRow(); UI::TableNextColumn(); UI::Text(names[r]); UI::TableNextColumn(); UI::Text(kinds[r]); UI::TableNextColumn(); UI::Text(sizes[r]); UI::TableNextColumn(); UI::Text(when[r]); }
                    UI::EndTable();
                }
                UI::End();
            };
            sim.Frames(3, ui);
            sim.Move(hdr.x + 40, hdr.y + 8); sim.Frame(ui); sim.rdown = true; sim.Frame(ui); sim.rdown = false; sim.Frames(2, ui);
            CHECK(UI::IsPopupOpen(nullptr, UI::PopupFlags_AnyPopupId));
            Check("imcompat_table_hide_menu", sim.Draw(28, 30, 36));
            UI::UseLauncherLook(false);
        }
        {
            Reset(); UI::UseLauncherLook(true);
            Sim sim(520, 340);
            UI::Rect rb;
            auto ui = [&] {
                BeginWin("kids", 6, 6, 508, 328);
                UI::PushStyleColor(UI::Col_ChildBg, UI::RGBA(44, 50, 64));
                if (UI::BeginChild("with borders", UI::Vec2{160, 60}, UI::ChildFlags_Borders)) { UI::Text("Borders"); UI::EndChild(); }
                UI::SameLine();
                if (UI::BeginChild("without", UI::Vec2{160, 60}, 0)) { UI::Text("No borders"); UI::EndChild(); }
                UI::SameLine();
                if (UI::BeginChild("auto", UI::Vec2{150, 0}, UI::ChildFlags_Borders | UI::ChildFlags_AutoResizeY)) { UI::Text("Auto height"); UI::Text("two rows"); UI::EndChild(); }
                UI::PopStyleColor();
                const UI::DrawList& dl = UI::GetWindowDrawList();
                const UI::Vec2 p = UI::GetCursorScreenPos();
                dl.AddRectFilled({p.x, p.y + 6}, {p.x + 50, p.y + 46}, UI::RGBA(110, 160, 255), 12.0f, UI::DrawFlags_RoundCornersTop);
                dl.AddRectFilled({p.x + 60, p.y + 6}, {p.x + 110, p.y + 46}, UI::RGBA(110, 160, 255), 12.0f, UI::DrawFlags_RoundCornersBottomLeft | UI::DrawFlags_RoundCornersTopRight);
                dl.AddRect({p.x + 120, p.y + 6}, {p.x + 170, p.y + 46}, UI::RGBA(255, 200, 90), 12.0f, UI::DrawFlags_RoundCornersLeft, 2.0f);
                dl.AddNgonFilled({p.x + 215, p.y + 26}, 20, UI::RGBA(120, 220, 150), 6);
                dl.AddNgon({p.x + 265, p.y + 26}, 20, UI::RGBA(240, 120, 120), 5, 2.0f);
                UI::Dummy(300, 52);
                UI::Button("Hover me for the tooltip"); rb = UI::GetItemRect();
                if (UI::IsItemHovered(UI::HoveredFlags_DelayShort) && UI::BeginTooltip()) { UI::Text("A tooltip with widgets"); UI::ProgressBar(0.6f, UI::Vec2{160, 0}, "60%"); UI::Button("Inside"); UI::EndTooltip(); }
                UI::End();
            };
            sim.Frames(3, ui);
            sim.Move(CenterX(rb), CenterY(rb));
            sim.Frames(30, ui);
            Check("imcompat_children_tooltip_shapes", sim.Draw(28, 30, 36));
            UI::UseLauncherLook(false);
        }
    }
    TEST_MAIN_RESULT();
}
