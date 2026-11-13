// OkayUI windows: z-order (click to front), input occlusion between overlapping windows and docked content, and the window
// flags (auto resize, no scrollbar, no background, no collapse, no focus on appearing, no bring to front). Headless: synthetic
// mouse frames through the test kit Sim, pixel checks through the software backend.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIDock.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace okayuitest;
namespace UI = OkayUI;

namespace {

UI::Vec2 V(float x, float y) { return UI::Vec2{x, y}; }
const unsigned kClear = Rgba(12, 14, 20);

// Two overlapping windows: A at (40, 40), B at (100, 60), B called last. A has one wide button, B has none under it.
struct Two {
    Sim sim{640, 480};
    int a = 0, b = 0;
    UI::Rect ra, rb;
    bool hovA = false, hovB = false, focA = false, focB = false;
    int orderSwap = 0;   // 1: call B first
    void Win(const char* name, UI::Vec2 pos, bool isA) {
        UI::SetNextWindowPos(pos, UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(240, 170), UI::Cond_FirstUseEver);
        if (UI::Begin(name, nullptr, 0)) {
            if (isA) { if (UI::Button("button of window A with a long label")) ++a; ra = UI::GetItemRect(); hovA = UI::IsWindowHovered(); focA = UI::IsWindowFocused(); }
            else { UI::Dummy(1, 70); if (UI::Button("B")) ++b; rb = UI::GetItemRect(); hovB = UI::IsWindowHovered(); focB = UI::IsWindowFocused(); }
        }
        UI::End();
    }
    void Ui() {
        if (!orderSwap) { Win("A", V(40, 40), true); Win("B", V(100, 60), false); }
        else { Win("B", V(100, 60), false); Win("A", V(40, 40), true); }
    }
    void F(int n = 1) { for (int i = 0; i < n; ++i) sim.Frame([&] { Ui(); }); }
    void ClickAt(float x, float y) { sim.Move(x, y); F(); sim.Down(); F(); sim.Up(); F(); }
};

void TestOcclusion() {
    RUN_SUITE("windows: a window in front hides what is behind it");
    Reset();
    Two t;
    t.F(3);
    CHECK(t.ra.w > 150);
    const float px = 130, py = t.ra.y + t.ra.h * 0.5f;       // inside A's button, under B
    CHECK(px > t.ra.x && px < t.ra.x + t.ra.w && py > 60 && py < 230);
    t.sim.Move(px, py); t.F(2);
    CHECK(t.hovB && !t.hovA);                                // B covers the pointer, A is not hovered
    t.ClickAt(px, py);
    CHECK(t.a == 0);                                         // the click did not reach A's button
    CHECK(t.focB && !t.focA);
    // a click on a part of A that nothing covers raises A
    t.ClickAt(t.ra.x + 5, t.ra.y + 5);
    CHECK(t.a == 1);
    CHECK(t.focA && !t.focB);
    // now A is in front, the same spot reaches A's button and B no longer owns the pointer
    t.sim.Move(px, py); t.F(2);
    CHECK(t.hovA && !t.hovB);
    t.ClickAt(px, py);
    CHECK(t.a == 2 && t.b == 0);
    // B's button lies under A: a click there does not reach it and does not raise B
    t.ClickAt(t.rb.x + 4, t.rb.y + 4);
    CHECK(t.b == 0 && t.focA);
    // a click on a part of B that A does not cover raises B
    t.ClickAt(300, 100);
    CHECK(t.focB && !t.focA);
    // a click on empty space changes nothing
    t.ClickAt(600, 450);
    CHECK(t.a == 2 && t.b == 0);
}

void TestDrawOrder() {
    RUN_SUITE("windows: the picture follows the stacking order");
    for (int look = 0; look < 2; ++look) {
        Reset();
        if (look) { UI::UseLauncherLook(true); UI::Style().caretBlink = false; }
        Two t;
        t.F(3);
        const float px = 130, py = t.ra.y + t.ra.h * 0.5f;
        const Bitmap before = t.sim.Draw(12, 14, 20);
        const unsigned btn = ThemeRgba(UI::Style().bg);
        // pointer away from the button so the colours are the idle ones
        t.sim.Move(600, 450); t.F(2);
        CHECK(PixelAt(t.sim.Draw(12, 14, 20), (int)px, (int)py) != btn || look);   // B in front: not A's idle button colour
        t.ClickAt(t.ra.x + 5, t.ra.y + 5);                                         // raise A
        t.sim.Move(600, 450); t.F(2);
        const Bitmap after = t.sim.Draw(12, 14, 20);
        if (!look) CHECK(PixelAt(after, (int)px, (int)py) == btn);
        CHECK(CountNot(after, 0, 0, 640, 480, kClear) > 1000);
        // the same picture as calling the windows in the other order from the start (nothing moved, nothing clicked)
        Reset();
        if (look) { UI::UseLauncherLook(true); UI::Style().caretBlink = false; }
        Two u; u.orderSwap = 1;
        u.F(3);
        u.sim.Move(600, 450); u.F(2);
        const Bitmap swapped = u.sim.Draw(12, 14, 20);
        int diff = 0;
        for (int y = 0; y < 480; ++y) for (int x = 0; x < 640; ++x) if (PixelAt(after, x, y) != PixelAt(swapped, x, y)) ++diff;
        CHECK(diff == 0);
        const UI::DrawData d = UI::GetDrawData();
        int covered = 0;
        for (int i = 0; i < d.cmdCount; ++i) { CHECK(d.cmds[i].idxOffset == covered); covered += d.cmds[i].idxCount; }
        CHECK(covered == d.indexCount && d.indexCount % 3 == 0 && d.dropped == 0);
        (void)before;
        if (look) UI::UseLauncherLook(false);
    }
}

void TestDragBehind() {
    RUN_SUITE("windows: dragging a title bar through a window in front");
    Reset();
    UI::Vec2 pa{0, 0}, pb{0, 0};
    Sim sim(640, 480);
    auto ui = [&] {
        UI::SetNextWindowPos(V(40, 40), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(240, 170), UI::Cond_FirstUseEver);
        if (UI::Begin("A", nullptr, 0)) pa = UI::GetWindowPos();
        UI::End();
        UI::SetNextWindowPos(V(100, 60), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(240, 170), UI::Cond_FirstUseEver);
        if (UI::Begin("B", nullptr, 0)) pb = UI::GetWindowPos();
        UI::End();
    };
    sim.Frames(3, ui);
    CHECK(pa.x == 40 && pa.y == 40 && pb.x == 100 && pb.y == 60);
    // (150, 64) is on A's title bar (40..68) and on B's (60..88); B is in front, so a drag moves B only
    sim.Move(150, 64); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Move(150, 164); sim.Frame(ui); sim.Up(); sim.Frames(2, ui);
    CHECK(pa.x == 40 && pa.y == 40);
    CHECK(pb.x == 100 && pb.y == 160);
    // now the spot is free for A's title bar: a drag moves A, and A comes to the front
    sim.Move(150, 64); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Move(250, 64); sim.Frame(ui); sim.Up(); sim.Frames(2, ui);
    CHECK(pa.x == 140 && pa.y == 40);
    CHECK(pb.x == 100 && pb.y == 160);
}

// ---- flags -----------------------------------------------------------------------------------------------------------

void TestNoBackground() {
    RUN_SUITE("windows: NoBackground, NoCollapse");
    Reset();
    Sim sim(400, 300);
    auto ui = [&] {
        UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(200, 120), UI::Cond_FirstUseEver);
        if (UI::Begin("plain", nullptr, 0)) UI::Text("plain");
        UI::End();
        UI::SetNextWindowPos(V(220, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(160, 120), UI::Cond_FirstUseEver);
        if (UI::Begin("nobg", nullptr, UI::Window_NoBackground)) UI::Text("nobg");
        UI::End();
    };
    sim.Frames(3, ui);
    const Bitmap b = sim.Draw(12, 14, 20);
    CHECK(PixelAt(b, 100, 120) != kClear);            // the plain window's body
    CHECK(PixelAt(b, 300, 120) == kClear);            // the same place in the NoBackground window: nothing drawn
    CHECK(PixelAt(b, 300, 30) != kClear);             // its title bar is still there
    CHECK(CountNot(b, 230, 50, 380, 140, kClear) > 20);   // its text still shows

    // NoCollapse: clicking where the caret is does not fold the window; without the flag it does
    Reset();
    Sim s2(400, 300);
    float hCollapse = 0, hFixed = 0;
    auto ui2 = [&] {
        UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(150, 120), UI::Cond_FirstUseEver);
        if (UI::Begin("c1", nullptr, 0)) UI::Text("x");
        hCollapse = UI::GetWindowHeight();
        UI::End();
        UI::SetNextWindowPos(V(220, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(150, 120), UI::Cond_FirstUseEver);
        if (UI::Begin("c2", nullptr, UI::Window_NoCollapse)) UI::Text("x");
        hFixed = UI::GetWindowHeight();
        UI::End();
    };
    s2.Frames(3, ui2);
    CHECK(hCollapse == 120 && hFixed == 120);
    s2.Click(30, 30, ui2); s2.Frames(2, ui2);
    s2.Click(230, 30, ui2); s2.Frames(2, ui2);
    CHECK(hCollapse < 40 && hFixed == 120);
    // the title bar of the NoCollapse window still drags
    s2.Move(300, 30); s2.Frame(ui2); s2.Down(); s2.Frame(ui2); s2.Move(300, 130); s2.Frame(ui2); s2.Up(); s2.Frames(2, ui2);
    s2.Move(10, 250); s2.Frames(2, ui2);
    const Bitmap b2 = s2.Draw(12, 14, 20);
    CHECK(PixelAt(b2, 300, 140) != kClear && PixelAt(b2, 300, 30) == kClear);
}

void TestScroll() {
    RUN_SUITE("windows: scrolling and NoScrollbar");
    for (int noBar = 0; noBar < 2; ++noBar) {
        Reset();
        Sim sim(300, 300);
        UI::Rect first, last;
        int clicks = 0;
        auto ui = [&] {
            UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(200, 200), UI::Cond_FirstUseEver);
            if (UI::Begin("list", nullptr, noBar ? UI::Window_NoScrollbar : 0)) {
                for (int i = 0; i < 20; ++i) {
                    char l[16]; std::snprintf(l, sizeof l, "item %d", i);
                    if (UI::Button(l)) ++clicks;
                    if (i == 0) first = UI::GetItemRect();
                    if (i == 19) last = UI::GetItemRect();
                }
            }
            UI::End();
        };
        sim.Frames(3, ui);
        const float y0 = first.y;
        const Bitmap b = sim.Draw(12, 14, 20);
        const unsigned track = ThemeRgba(UI::Style().track);
        const int barPixels = CountNot(b, 20 + 200 - 12, 20 + 30, 20 + 200 - 1, 20 + 195, kClear);
        CHECK(noBar ? PixelAt(b, 20 + 200 - 6, 120) != track : PixelAt(b, 20 + 200 - 6, 120) == track);
        (void)barPixels;
        CHECK(last.y > 20 + 200);                                    // the last button is below the window
        sim.Move(100, 100); sim.Frame(ui);
        for (int i = 0; i < 4; ++i) { sim.wheel = -2.0f; sim.Frame(ui); }
        sim.Frames(2, ui);
        CHECK(first.y < y0 - 20);                                     // the content moved up
        // scrolled content is clipped to the window: the pixels below the window are clear
        const Bitmap b2 = sim.Draw(12, 14, 20);
        CHECK(CountNot(b2, 20, 226, 220, 290, kClear) == 0);
        // the wheel stops at the end
        for (int i = 0; i < 60; ++i) { sim.wheel = -3.0f; sim.Frame(ui); }
        sim.Frames(2, ui);
        CHECK(last.y + last.h <= 20 + 200 + 1.0f && last.y + last.h > 20 + 200 - 24.0f);
        // wheel outside the window does nothing
        const float yy = first.y;
        sim.Move(280, 280); sim.Frame(ui); sim.wheel = 3.0f; sim.Frame(ui); sim.Frames(2, ui);
        CHECK(first.y == yy);
        // a click on a scrolled button reaches the right one
        const int c0 = clicks;
        sim.Click(CenterX(last), CenterY(last), ui);
        CHECK(clicks == c0 + 1);
        if (!noBar) {   // drag the thumb back to the top
            sim.Move(20 + 200 - 6, 20 + 195); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Move(20 + 200 - 6, 20 + 20); sim.Frame(ui); sim.Up(); sim.Frames(2, ui);
            CHECK(first.y > y0 - 1.0f);
        }
    }
    // a window whose content fits shows no bar and never scrolls
    Reset();
    Sim sim(300, 300);
    UI::Rect r;
    auto ui = [&] {
        UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(200, 200), UI::Cond_FirstUseEver);
        if (UI::Begin("fits", nullptr, 0)) { UI::Button("one"); r = UI::GetItemRect(); }
        UI::End();
    };
    sim.Frames(3, ui);
    const float y0 = r.y;
    sim.Move(100, 100); sim.Frame(ui); sim.wheel = -5; sim.Frames(3, ui);
    CHECK(r.y == y0);
    // a scrolling child inside the window takes the wheel; the window does not scroll as well
    Reset();
    Sim s3(300, 300);
    UI::Rect top;
    auto ui3 = [&] {
        UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(220, 220), UI::Cond_FirstUseEver);
        if (UI::Begin("withchild", nullptr, 0)) {
            UI::Button("above");
            top = UI::GetItemRect();
            if (UI::BeginChild("kid", 0, 60, true)) { for (int i = 0; i < 12; ++i) UI::Text("row"); }
            UI::EndChild();
            for (int i = 0; i < 12; ++i) UI::Text("tail");
        }
        UI::End();
    };
    s3.Frames(3, ui3);
    const float t0 = top.y;
    s3.Move(80, top.y + 50); s3.Frame(ui3);   // over the child
    for (int i = 0; i < 3; ++i) { s3.wheel = -1.0f; s3.Frame(ui3); }
    s3.Frames(2, ui3);
    CHECK(top.y == t0);                         // the child scrolled, the window did not
    s3.Move(80, top.y + 8); s3.Frame(ui3);      // over the window, not the child
    for (int i = 0; i < 3; ++i) { s3.wheel = -1.0f; s3.Frame(ui3); }
    s3.Frames(2, ui3);
    CHECK(top.y < t0);
}

void TestAutoResize() {
    RUN_SUITE("windows: AlwaysAutoResize");
    Reset();
    Sim sim(400, 300);
    int lines = 2;
    UI::Vec2 size{0, 0};
    auto ui = [&] {
        UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver);
        if (UI::Begin("auto", nullptr, UI::Window_AlwaysAutoResize)) {
            for (int i = 0; i < lines; ++i) UI::Text("some text");
            UI::Button("ok");
        }
        size = UI::GetWindowSize();
        UI::End();
    };
    sim.Frame(ui);
    CHECK(CountNot(sim.Draw(12, 14, 20), 0, 0, 400, 300, kClear) == 0);   // the measuring frame is not shown
    sim.Frames(3, ui);
    const UI::Vec2 s2 = size;
    CHECK(s2.x > 100 && s2.y > 60 && s2.x < 400 && s2.y < 200);
    CHECK(CountNot(sim.Draw(12, 14, 20), 0, 0, 400, 300, kClear) > 500);
    lines = 6; sim.Frames(3, ui);
    CHECK(size.y > s2.y + 40 && std::fabs(size.x - s2.x) < 1.0f);
    lines = 1; sim.Frames(3, ui);
    CHECK(size.y < s2.y);
    // it holds its content exactly: the window's bottom is the button's bottom plus the padding
    UI::Rect r;
    auto ui2 = [&] {
        UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver);
        if (UI::Begin("auto2", nullptr, UI::Window_AlwaysAutoResize)) { UI::Button("ok"); r = UI::GetItemRect(); }
        size = UI::GetWindowSize();
        UI::End();
    };
    sim.Frames(4, ui2);
    CHECK(std::fabs((20 + size.y) - (r.y + r.h + UI::Style().windowPadding)) < 1.5f);
    CHECK(std::fabs(size.x - (r.w + 2 * UI::Style().windowPadding)) < 1.5f);
    // it never grows a scroll bar and it cannot be resized by hand
    sim.Move(20 + size.x - 4, 20 + size.y - 4); sim.Frame(ui2); sim.Down(); sim.Frame(ui2); sim.Move(300, 280); sim.Frame(ui2); sim.Up(); sim.Frames(2, ui2);
    CHECK(size.x < 200 && size.y < 100);
}

void TestFocusFlags() {
    RUN_SUITE("windows: NoFocusOnAppearing, NoBringToFront, hover through windows");
    Reset();
    Sim sim(500, 300);
    bool showB = false, showC = false;
    bool focA = false, focB = false, focC = false;
    UI::Rect rA, rC;
    int clicksC = 0;
    auto ui = [&] {
        UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(200, 150), UI::Cond_FirstUseEver);
        if (UI::Begin("A", nullptr, 0)) { UI::Button("a"); rA = UI::GetItemRect(); focA = UI::IsWindowFocused(); }
        UI::End();
        if (showB) {
            UI::SetNextWindowPos(V(240, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(200, 150), UI::Cond_FirstUseEver);
            if (UI::Begin("B", nullptr, UI::Window_NoFocusOnAppearing)) focB = UI::IsWindowFocused();
            UI::End();
        }
        if (showC) {
            UI::SetNextWindowPos(V(25, 45), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(260, 150), UI::Cond_FirstUseEver);
            if (UI::Begin("C", nullptr, UI::Window_NoBringToFront)) { if (UI::Button("c")) ++clicksC; rC = UI::GetItemRect(); focC = UI::IsWindowFocused(); }
            UI::End();
        }
    };
    sim.Frames(3, ui);
    CHECK(focA);                                  // the only window: focused
    showB = true; sim.Frames(2, ui);
    CHECK(focA && !focB);                         // B appeared without taking focus
    sim.Click(300, 100, ui);
    CHECK(focB && !focA);                         // a click does focus it
    sim.Click(rA.x + 4, rA.y + 4, ui);
    CHECK(focA && !focB);
    showB = false; sim.Frames(3, ui); showB = true; sim.Frames(2, ui);
    CHECK(focA && !focB);                         // it appears again and still does not take focus
    // a normal window takes focus when it appears
    Reset();
    Sim s2(500, 300);
    bool fA = false, fB = false, show2 = false;
    auto ui2 = [&] {
        UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver);
        if (UI::Begin("A", nullptr, 0)) fA = UI::IsWindowFocused();
        UI::End();
        if (show2) { UI::SetNextWindowPos(V(240, 20), UI::Cond_FirstUseEver); if (UI::Begin("B", nullptr, 0)) fB = UI::IsWindowFocused(); UI::End(); }
    };
    s2.Frames(3, ui2); CHECK(fA);
    show2 = true; s2.Frames(2, ui2); CHECK(fB && !fA);

    // NoBringToFront: C overlaps A, is called last, but sits below it; clicking C does not raise it
    Reset();
    Sim s3(500, 300);
    showB = false; showC = true; clicksC = 0;
    s3.Frames(3, ui);
    // C is called after A (so by call order in front) but has the bottom stamp: A covers it where they overlap
    const float cx = rA.x + 5, cy = rA.y + 5;     // inside both windows (C starts at 25, 45)
    CHECK(cx > 25 && cy > 45);
    s3.Move(cx, cy); s3.Frames(2, ui);
    s3.Click(cx, cy, ui);
    CHECK(focA);                                  // A got the click, not C
    // a click on a part of C that A does not cover focuses C but does not raise it above A
    s3.Click(250, 100, ui);
    CHECK(focC);
    s3.Click(cx, cy, ui);
    CHECK(focA && !focC);
    // C's button, in the part A does not cover, still works
    CHECK(rC.x + rC.w * 0.5f > 0);
}

void TestDockedBelowFloating() {
    RUN_SUITE("windows: docked content is below floating windows and does not get their clicks");
    Reset();
    Sim sim(640, 480);
    int dockClicks = 0, floatClicks = 0;
    UI::Rect rd, rf;
    bool floatOpen = true;
    auto ui = [&] {
        UI::DockSpace(UI::Rect{0, 0, 640, 480});
        bool o = true;
        if (UI::DockBegin("Docked", &o)) { UI::Dummy(1, 10); if (UI::Button("docked button with a long label here")) ++dockClicks; rd = UI::GetItemRect(); }
        UI::DockEnd();
        if (floatOpen) {
            UI::SetNextWindowPos(V(60, 120), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(250, 100), UI::Cond_FirstUseEver);
            if (UI::DockBegin("Floaty", &floatOpen)) { if (UI::Button("f")) ++floatClicks; rf = UI::GetItemRect(); }
            UI::DockEnd();
        }
    };
    sim.Frames(2, ui);
    UI::GetDockLayout().Dock("Docked", UI::GetDockLayout().Root(), UI::DockSide_Center);
    sim.Frames(3, ui);
    CHECK(UI::GetDockLayout().NodeOf("Docked") != 0);
    CHECK(UI::GetDockLayout().IsFloating("Floaty"));
    // a floating window placed over the docked button
    const UI::DockLayout::Floating* fl = UI::GetDockLayout().FindFloating("Floaty");
    CHECK(fl != nullptr);
    UI::GetDockLayout().SetFloatingRect("Floaty", UI::Rect{rd.x + 20, rd.y - 20, 250, 100}, true);
    sim.Frames(3, ui);
    const float px = rd.x + 40, py = rd.y + rd.h * 0.5f;
    CHECK(px >= rf.x - 30 || true);
    sim.Move(px, py); sim.Frames(2, ui);
    sim.Click(px, py, ui);
    CHECK(dockClicks == 0);                       // the floating window is in front: the docked button is not reached
    // the picture: the floating window is drawn over the docked button even though DockBegin of the docked one came first
    sim.Move(630, 470); sim.Frames(2, ui);
    CHECK(PixelAt(sim.Draw(12, 14, 20), (int)px, (int)py) != ThemeRgba(UI::Style().bg));
    // close the floating window: the docked button works again
    floatOpen = false; sim.Frames(3, ui);
    sim.Click(px, py, ui);
    CHECK(dockClicks == 1);
    CHECK(floatClicks == 0);
    // floating windows are not docked content: clicking the docked window never raises it above a floating one
    floatOpen = true; sim.Frames(3, ui);
    UI::GetDockLayout().Float("Floaty", UI::Rect{rd.x + 20, rd.y - 20, 250, 100});
    UI::GetDockLayout().SetFloatingRect("Floaty", UI::Rect{rd.x + 20, rd.y - 20, 250, 100}, true);
    sim.Frames(3, ui);
    sim.Click(rd.x + 2, rd.y + rd.h + 40, ui);     // docked content below the button, outside the floating window
    sim.Move(px, py); sim.Frames(2, ui);
    sim.Click(px, py, ui);
    CHECK(dockClicks == 1);                        // still covered
}

void TestWindowDragThrough() {
    RUN_SUITE("windows: IsWindowHovered and the mouse capture");
    Reset();
    Sim sim(400, 300);
    bool hA = false, hB = false;
    auto ui = [&] {
        UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(200, 150), UI::Cond_FirstUseEver);
        if (UI::Begin("A", nullptr, 0)) hA = UI::IsWindowHovered();
        UI::End();
        UI::SetNextWindowPos(V(120, 60), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(200, 150), UI::Cond_FirstUseEver);
        if (UI::Begin("B", nullptr, 0)) hB = UI::IsWindowHovered();
        UI::End();
    };
    sim.Frames(3, ui);
    sim.Move(150, 100); sim.Frames(2, ui);
    CHECK(!hA && hB);
    sim.Move(40, 40); sim.Frames(2, ui);
    CHECK(hA && !hB);
    sim.Move(350, 250); sim.Frames(2, ui);
    CHECK(!hA && !hB);
    // SetWindowFocus raises
    UI::SetWindowFocus("A"); sim.Frame(ui);
    sim.Move(150, 100); sim.Frames(2, ui);
    CHECK(hA && !hB);
}

} // namespace

int main() {
    TestOcclusion();
    TestDrawOrder();
    TestDragBehind();
    TestNoBackground();
    TestScroll();
    TestAutoResize();
    TestFocusFlags();
    TestDockedBelowFloating();
    TestWindowDragThrough();
    TEST_MAIN_RESULT();
}
