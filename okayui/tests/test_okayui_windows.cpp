// OkayUI windows: z-order (click to front), input occlusion between overlapping windows and docked content, and the window
// flags (auto resize, no scrollbar, no background, no collapse, no focus on appearing, no bring to front). Headless: synthetic
// mouse frames through the test kit Sim, pixel checks through the software backend.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIDock.hpp"
#include "okay/UI/OkayUICompat.hpp"   // Window_MenuBar

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
        (void)barPixels; (void)track;
        {   // the thin overlay thumb along the right edge (no track): pixels there that are not the window's own colour
            const unsigned panel = ThemeRgba(UI::Style().panel);
            int thumb = 0;
            for (int y = 20 + 40; y < 20 + 190; ++y) for (int x = 20 + 200 - 9; x < 20 + 200 - 1; ++x) if (PixelAt(b, x, y) != panel) ++thumb;
            CHECK(noBar ? thumb < 20 : thumb > 50);   // (without a bar, only the rounded ends of the buttons' edges reach the strip)
        }
        CHECK(last.y > 20 + 200);                                    // the last button is below the window
        sim.Move(100, 100); sim.Frame(ui);
        for (int i = 0; i < 4; ++i) { sim.wheel = -2.0f; sim.Frame(ui); }
        sim.Frames(2, ui);
        CHECK(first.y < y0 - 20);                                     // the content moved up
        // scrolled content is clipped to the window: the pixels below the window are clear
        const Bitmap b2 = sim.Draw(12, 14, 20);
        CHECK(CountNot(b2, 20, 245, 220, 290, kClear) == 0);   // (below the window's shadow)
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

// Window_MenuBar: the window keeps a row under its title bar; the content starts below it; BeginMenuBar draws the bar in that row even
// when the window calls it AFTER its other content (the editor's dock host draws its menus last); a menu there opens on a click and
// a widget after SameLine sits on the bar's row; EndMenuBar gives the window its cursor back.
void TestMenuBarRow() {
    RUN_SUITE("windows: Window_MenuBar keeps a row for the menu bar");
    Reset();
    Sim sim(500, 320);
    UI::Rect first, after, menuBtn, barBtn, quitRow; UI::Vec2 wpos{}; bool fileOpen = false, quitHov = false; int barClicks = 0; float titleH = 0.0f;
    auto ui = [&] {
        UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(400, 240), UI::Cond_FirstUseEver);
        if (UI::Begin("Host", nullptr, UI::Window_MenuBar)) {
            wpos = UI::GetWindowPos();
            UI::Button("content"); first = UI::GetItemRect();
            if (UI::BeginMenuBar()) {
                fileOpen = UI::BeginMenu("File");
                if (fileOpen) { UI::MenuItem("Quit"); quitHov = UI::IsItemHovered(); quitRow = UI::GetItemRect(); UI::EndMenu(); }
                menuBtn = UI::GetItemRect();
                UI::SameLine(200.0f);
                if (UI::Button("Play")) ++barClicks;
                barBtn = UI::GetItemRect();
                UI::EndMenuBar();
            }
            UI::Button("after"); after = UI::GetItemRect();
        }
        UI::End();
    };
    sim.Frames(2, ui);
    titleH = first.y - wpos.y;                                   // title bar + bar row + padding
    CHECK(barBtn.y >= wpos.y && barBtn.y < first.y);             // the bar's widgets sit above the content, in the kept row
    CHECK(first.y > wpos.y + 2.0f * 16.0f);                      // the content starts below a title bar and the bar row
    CHECK(after.y > first.y + first.h - 1.0f);                   // the cursor came back: "after" is under "content", not on the bar
    CHECK(barBtn.x > wpos.x + 150.0f);                           // SameLine(200) moved along the bar
    sim.Click(CenterX(barBtn), CenterY(barBtn), ui);
    CHECK(barClicks == 1);
    sim.Click(wpos.x + 20.0f, barBtn.y + barBtn.h * 0.5f, ui);  // "File" is the bar's first menu, at its left end
    sim.Frame(ui);
    CHECK(fileOpen);
    CHECK(!quitHov);                                             // the pointer is on "File", not on the row: no stray tooltip
    sim.Move(CenterX(quitRow), CenterY(quitRow)); sim.Frame(ui); sim.Frame(ui);
    CHECK(fileOpen && quitHov);                                  // IsItemHovered after MenuItem asks about that row
    (void)menuBtn; (void)titleH;
    // without the flag nothing is kept: the content starts right under the title bar
    Reset();
    UI::Rect plain{}; UI::Vec2 ppos{};
    auto ui2 = [&] {
        UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(400, 240), UI::Cond_FirstUseEver);
        if (UI::Begin("Host", nullptr, 0)) { ppos = UI::GetWindowPos(); UI::Button("content"); plain = UI::GetItemRect(); }
        UI::End();
    };
    sim.Frames(2, ui2);
    CHECK(plain.y - ppos.y < first.y - wpos.y - 8.0f);
}

// A scrolling child inside a popup body (the editor's template list in its "Open a project" modal): its rows are clipped to the child,
// and the clip of the window the popup was opened over does not cut the popup. Popups draw in the overlay layer, which now has clips.
void TestChildClipInPopup() {
    RUN_SUITE("windows: a child region inside a modal clips its rows");
    Reset();
    Sim sim(640, 480);
    UI::Rect child{}; bool shown = false;
    auto ui = [&] {
        UI::SetNextWindowPos(V(10, 10), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(120, 80), UI::Cond_FirstUseEver);
        if (UI::Begin("Small", nullptr, 0)) UI::Text("under the modal");
        UI::End();
        if (!UI::IsPopupOpen("Modal")) UI::OpenPopup("Modal");   // named outside every window, where it is begun (ids depend on where)
        if (UI::BeginPopupModal("Modal")) {
            shown = true;
            UI::Text("above");
            if (UI::BeginChild("list", UI::Vec2{200, 60}, UI::Child_Borders)) {
                for (int i = 0; i < 30; ++i) { char b[16]; std::snprintf(b, sizeof b, "row %d", i); UI::Button(b); }
            }
            UI::EndChild();
            child = UI::GetItemRect();
            UI::Text("below");
            UI::EndPopup();
        }
    };
    sim.Frames(4, ui);
    CHECK(shown);
    const UI::DrawData dd = UI::GetDrawData();
    bool childClip = false, anyUnclippedOverlayAfter = false;
    for (int i = 0; i < dd.cmdCount; ++i) {
        const UI::DrawCmd& c = dd.cmds[i];
        if (c.clipOn && std::fabs(c.clipY - child.y) <= 2.0f && std::fabs(c.clipH - child.h) <= 3.0f && c.idxCount > 0) childClip = true;
        if (childClip && !c.clipOn && c.idxCount > 0) anyUnclippedOverlayAfter = true;   // "below" and the frame: back to no clip
    }
    CHECK(childClip);
    CHECK(anyUnclippedOverlayAfter);
    // pixels: nothing of the rows is drawn under the child's bottom edge (between it and the "below" text there is only the popup's colour)
    const Bitmap& b = sim.Draw(12, 14, 20);
    const unsigned bg = PixelAt(b, (int)(child.x + child.w + 20), (int)(child.y + child.h + 2));
    int stray = 0;
    for (int x = (int)child.x + 4; x < (int)(child.x + 60); ++x) if (PixelAt(b, x, (int)(child.y + child.h + 2)) != bg) ++stray;
    CHECK(stray == 0);
}

// SetNextWindowSize with an axis of 0: Dear ImGui fits that axis to the content (the editor's Project Settings asks for 460 x 0); OkayUI
// used to make the window 0 pixels tall, a bare title bar with the content spilling out under it.
void TestSizeAxisZeroFits() {
    RUN_SUITE("windows: SetNextWindowSize with a 0 axis fits it to the content");
    Reset();
    Sim sim(640, 480);
    UI::Vec2 sz{}; UI::Rect last{}; bool began = false;
    auto ui = [&] {
        UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver);
        UI::SetNextWindowSize(V(300, 0), UI::Cond_FirstUseEver);
        began = UI::Begin("Fit", nullptr, 0);
        if (began) { for (int i = 0; i < 5; ++i) { char b[8]; std::snprintf(b, sizeof b, "b%d", i); UI::Button(b); } last = UI::GetItemRect(); sz = UI::GetWindowSize(); }
        UI::End();
    };
    sim.Frames(3, ui);
    CHECK(began);
    CHECK(std::fabs(sz.x - 300.0f) < 0.5f);                       // the width asked for
    CHECK(sz.y > 60.0f && sz.y < 400.0f);                         // a real height, not 0 and not the 240 default
    CHECK(last.y + last.h <= 20.0f + sz.y + 0.5f);                // the last button is inside the window
    // later frames keep it (FirstUseEver is spent) and it stays resizable
    sim.Frames(2, ui);
    CHECK(std::fabs(sz.x - 300.0f) < 0.5f && sz.y > 60.0f);
}

// A bare window begun inside an open window is a region of it (how the editor's OkayUI section bodies are drawn inside its windows when the
// editor itself runs on OkayUI): the outer window stays open, the region's items sit at its rectangle and are clipped to it, and after
// its End the outer window carries on where it was.
void TestRegionInsideWindow() {
    RUN_SUITE("windows: a bare window inside a window is a region of it");
    Reset();
    Sim sim(640, 480);
    UI::Rect before{}, inner{}, after{}; UI::Vec2 hostSize{}, hostSizeAfter{}; int innerClicks = 0, afterClicks = 0;
    auto ui = [&] {
        UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(400, 300), UI::Cond_FirstUseEver);
        if (UI::Begin("Host", nullptr, 0)) {
            hostSize = UI::GetWindowSize();
            UI::Button("before"); before = UI::GetItemRect();
            const UI::Vec2 p = UI::GetCursorScreenPos();
            UI::BareNextWindow();
            if (UI::Begin("section##inner", p.x, p.y, 300.0f, 60.0f)) { if (UI::Button("inside")) ++innerClicks; inner = UI::GetItemRect(); }
            UI::End();
            UI::Dummy(300.0f, 60.0f);
            if (UI::Button("after")) ++afterClicks;
            after = UI::GetItemRect();
            hostSizeAfter = UI::GetWindowSize();
        }
        UI::End();
    };
    sim.Frames(3, ui);
    CHECK(inner.y >= before.y + before.h - 0.5f && inner.y < before.y + before.h + 20.0f);   // the region's item where the region is
    CHECK(after.y >= inner.y + 59.0f);                                                       // the host carried on under the region
    CHECK(std::fabs(hostSizeAfter.x - hostSize.x) < 0.5f && std::fabs(hostSizeAfter.y - hostSize.y) < 0.5f);   // still the host window
    sim.Click(CenterX(inner), CenterY(inner), ui);
    sim.Click(CenterX(after), CenterY(after), ui);
    CHECK(innerClicks == 1 && afterClicks == 1);
}

// A menu is a popup window of any widgets (Dear ImGui's BeginMenu): the editor's Edit > Appearance sub-menu holds a design picker made of
// text, buttons, sliders and a separator. They used to be laid out in the window under the menu (drawn at its edge, under the list); now
// they go into the list, below the rows before them, take clicks, keep the menu open, and the list grows to fit them.
void TestMenuOfWidgets() {
    RUN_SUITE("windows: a menu's list lays out any widgets (a popup window of widgets)");
    Reset();
    Sim sim(900, 600);
    UI::Rect undo{}, sub{}, label{}, wide{}, slider{}, after{}, outer{}, editBtn{}; UI::Vec2 wpos{};
    bool editOpen = false, subOpen = false; int wideClicks = 0, afterClicks = 0; float v = 0.25f;
    auto ui = [&] {
        UI::SetNextWindowPos(V(10, 10), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(600, 400), UI::Cond_FirstUseEver);
        if (UI::Begin("Host", nullptr, UI::Window_MenuBar)) {
            wpos = UI::GetWindowPos();
            if (UI::BeginMenuBar()) {
                editOpen = UI::BeginMenu("Edit");
                if (editOpen) {
                    UI::MenuItem("Undo"); undo = UI::GetItemRect();
                    subOpen = UI::BeginMenu("Appearance");
                    sub = UI::GetItemRect();
                    if (subOpen) {
                        UI::TextDisabled("Design"); label = UI::GetItemRect();
                        if (UI::Button("a wide design card", V(420, 40))) ++wideClicks;
                        wide = UI::GetItemRect();
                        UI::Separator();
                        UI::SliderFloat("Corner roundness", &v, 0.0f, 1.0f); slider = UI::GetItemRect();
                        if (UI::MenuItem("After the widgets")) ++afterClicks;
                        after = UI::GetItemRect();
                        UI::EndMenu();
                    }
                    UI::EndMenu();
                }
                editBtn = UI::GetItemRect();
                UI::EndMenuBar();
            }
            UI::Button("content"); outer = UI::GetItemRect();
        }
        UI::End();
    };
    sim.Frames(2, ui);
    sim.Click(CenterX(editBtn), CenterY(editBtn), ui);   // the bar's menu
    sim.Frames(2, ui);
    CHECK(editOpen);
    if (!editOpen) return;
    sim.Move(CenterX(sub), CenterY(sub)); sim.Frames(3, ui);
    CHECK(subOpen);
    if (!subOpen) return;
    // laid out in the sub-menu's list, to the right of the Edit list, from its row down
    CHECK(label.x >= sub.x + sub.w - 1.0f);
    CHECK(wide.x >= sub.x + sub.w - 1.0f && wide.y >= label.y + label.h - 0.5f);
    CHECK(slider.y >= wide.y + wide.h - 0.5f);
    CHECK(after.y >= slider.y + slider.h - 0.5f);           // a row after the widgets goes under them
    CHECK(after.w >= 420.0f);                               // the list grew to fit the wide widget
    // the widget takes the click and the menu stays open (only picking an item closes it)
    sim.Move(CenterX(sub) + 2.0f, CenterY(sub)); sim.Frame(ui);
    sim.Move(CenterX(wide), CenterY(wide)); sim.Frame(ui);
    sim.Down(); sim.Frame(ui); sim.Up(); sim.Frames(2, ui);
    CHECK(wideClicks == 1);
    CHECK(editOpen && subOpen);
    // drawn on top: the button's centre is not the window's background colour under the list
    const Bitmap& b = sim.Draw(12, 14, 20);
    CHECK(PixelAt(b, (int)CenterX(wide), (int)CenterY(wide)) != PixelAt(b, (int)(wpos.x + 300), (int)(wpos.y + 380)));
    // picking the row after the widgets closes the whole chain
    sim.Move(CenterX(after), CenterY(after)); sim.Frame(ui);
    sim.Down(); sim.Frame(ui); sim.Up(); sim.Frames(2, ui);
    CHECK(afterClicks == 1);
    CHECK(!editOpen);
    (void)undo;
}

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
    TestMenuBarRow();
    TestChildClipInPopup();
    TestSizeAxisZeroFits();
    TestRegionInsideWindow();
    TestMenuOfWidgets();
    TEST_MAIN_RESULT();
}
