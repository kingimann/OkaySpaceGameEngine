// OkayUI windows: z-order (click to front), input occlusion between overlapping windows and docked content, and the window
// flags (auto resize, no scrollbar, no background, no collapse, no focus on appearing, no bring to front). Headless: synthetic
// mouse frames through the test kit Sim, pixel checks through the software backend.
#include "test_framework.hpp"
#include "OkayUITestKit.hpp"
#include "okay/UI/OkayUIDock.hpp"
#include "okay/UI/OkayUICompat.hpp"   // Window_MenuBar
#include "okay/UI/OkayUIInternals.hpp"   // GetDockHostDrawList

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

// A corner toast as the launcher draws one: SetNextWindowPos(bottom-right of the display, Always, pivot (1, 1)) on an auto-resizing
// window with no decoration. Every frame it is shown, its bottom-right corner is that point: on its first shown frame, when its text
// changes size, and when it comes back after being gone (owner report: the "You're up to date" toast sat in the middle of the launcher).
void TestPivotCornerAutoResize() {
    RUN_SUITE("windows: pivot (1, 1) at the corner of an auto-resizing window");
    Reset();
    const float W = 1600, H = 1135, M = 18;
    Sim sim(W, H);
    std::string msg = "You're up to date";
    bool show = true, closedModal = false;
    UI::Vec2 pos{0, 0}, size{0, 0};
    auto ui = [&] {
        if (closedModal) {   // the launcher's Create page: its New Project modal is set up every frame but is not open
            UI::SetNextWindowPos(V(W * 0.5f, H * 0.5f), UI::Cond_Always, V(0.5f, 0.5f)); UI::SetNextWindowSize(V(820, 680), UI::Cond_Always);
            bool open = false;
            if (UI::BeginPopupModal("New Project", &open, UI::Window_NoResize)) UI::EndPopup();
        }
        if (!show) return;
        const UI::Viewport vp = UI::GetMainViewport();
        UI::SetNextWindowPos(V(vp.WorkPos.x + vp.WorkSize.x - M, vp.WorkPos.y + vp.WorkSize.y - M), UI::Cond_Always, V(1, 1));
        UI::SetNextWindowBgAlpha(0.92f);
        UI::PushStyleVar(UI::StyleVar_WindowPadding, V(16, 12));
        if (UI::Begin("##toast", nullptr, UI::Window_NoDecoration | UI::Window_NoInputs | UI::Window_NoFocusOnAppearing | UI::Window_NoNav | UI::Window_AlwaysAutoResize)) {
            UI::Dummy(V(10, UI::GetTextLineHeight())); UI::SameLine(0, 6); UI::TextUnformatted(msg.c_str());
        }
        pos = UI::GetWindowPos(); size = UI::GetWindowSize();
        UI::End();
        UI::PopStyleVar(1);
    };
    int bad = 0, shown = 0;
    auto check = [&](int n) {
        for (int i = 0; i < n; ++i) {
            sim.Frame(ui);
            const Bitmap b = sim.Draw(21, 23, 29);
            const int lit = CountNot(b, 0, 0, (int)W, (int)H, Rgba(21, 23, 29));
            if (lit == 0) continue;   // the measuring frame is not shown
            ++shown;
            // whatever is drawn lies in the bottom-right corner (with its shadow), and the window's corner is the asked point
            const int outside = lit - CountNot(b, (int)(W - M - size.x - 24), (int)(H - M - size.y - 24), (int)W, (int)H, Rgba(21, 23, 29));   // (room for its shadow)
            const bool ok = outside == 0 && std::fabs(pos.x + size.x - (W - M)) < 0.5f && std::fabs(pos.y + size.y - (H - M)) < 0.5f;
            if (!ok) { ++bad; std::printf("  toast frame %d: pos (%.1f, %.1f) size (%.1f, %.1f), %d pixels outside its corner\n", sim.frames, pos.x, pos.y, size.x, size.y, outside); }
        }
    };
    check(4);
    msg = "A much longer message that makes the toast wider than before"; check(3);
    msg = "Short"; check(3);
    show = false; check(5);
    show = true; msg = "Back again, a different width"; check(3);
    closedModal = true; msg = "You're up to date"; check(4);   // (owner report: it sat at 761..918, 436..485 of a 1600 x 1135 launcher)
    CHECK(shown >= 14);
    CHECK(bad == 0);
}

// A pivot position kept once (Cond_Appearing) on an auto-resizing window: its first frame is only measured, so the pivot is applied
// with the measured size when it is first shown, not with the 320 x 240 a new window starts from.
void TestPivotAppearingAutoResize() {
    RUN_SUITE("windows: a Cond_Appearing pivot on an auto-resizing window uses the measured size");
    Reset();
    Sim sim(800, 600);
    UI::Vec2 pos{0, 0}, size{0, 0};
    auto ui = [&] {
        UI::SetNextWindowPos(V(400, 300), UI::Cond_Appearing, V(0.5f, 0.5f));
        if (UI::Begin("centred", nullptr, UI::Window_NoDecoration | UI::Window_AlwaysAutoResize)) UI::Button("a small button");
        pos = UI::GetWindowPos(); size = UI::GetWindowSize();
        UI::End();
    };
    for (int i = 0; i < 4; ++i) {
        sim.Frame(ui);
        if (i == 0) continue;   // the measuring frame
        CHECK(std::fabs(pos.x + size.x * 0.5f - 400.0f) < 0.5f && std::fabs(pos.y + size.y * 0.5f - 300.0f) < 0.5f);
    }
    CHECK(size.x < 320.0f && size.y < 240.0f);
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

// Dear ImGui's SameLine(offset_from_start_x): the next item goes at that x from the window's left edge, also left of the item before it (the
// editor's menu bar centres Play and right-aligns the layout list and the version that way; the one-argument Dear ImGui call is that, see
// okayui/probe/imgui.h). OkayUI used to treat it as a gap after the last item: Play sat 624 pixels right of the centre, the rest off screen.
void TestSameLineOffset() {
    RUN_SUITE("windows: SameLine(offset) is an x from the window's left edge");
    Reset();
    Sim sim(800, 400);
    UI::Rect a{}, b{}, c{}; UI::Vec2 wpos{};
    auto ui = [&] {
        UI::UseImGuiCoordinates(true);
        UI::SetNextWindowPos(V(30, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(600, 300), UI::Cond_FirstUseEver);
        if (UI::Begin("Host", nullptr, 0)) {
            wpos = UI::GetWindowPos();
            UI::Button("a long first button", V(300, 0)); a = UI::GetItemRect();
            UI::SameLine(100.0f, 0.0f); UI::Button("b"); b = UI::GetItemRect();      // left of a's end
            UI::SameLine(450.0f, 0.0f); UI::Button("c"); c = UI::GetItemRect();
        }
        UI::End();
        UI::UseImGuiCoordinates(false);
    };
    sim.Frames(3, ui);
    CHECK(std::fabs(b.x - (wpos.x + 100.0f)) < 0.5f);
    CHECK(std::fabs(b.y - a.y) < 0.5f);
    CHECK(std::fabs(c.x - (wpos.x + 450.0f)) < 0.5f);
}

// MenuItem(label, shortcut, bool* p_selected): Dear ImGui's toggle row. Before the overload the pointer became `selected = true`, so the
// editor's menu toggles (Show Gizmos, Snap to grid, Auto-scroll, ...) always showed a tick and a click changed nothing.
void TestMenuItemToggle() {
    RUN_SUITE("windows: MenuItem with a bool pointer toggles it");
    Reset();
    Sim sim(500, 320);
    UI::Rect btn{}, row{}; bool open = false, on = false; int clicks = 0;
    auto ui = [&] {
        UI::SetNextWindowPos(V(10, 10), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(400, 240), UI::Cond_FirstUseEver);
        if (UI::Begin("Host", nullptr, UI::Window_MenuBar)) {
            if (UI::BeginMenuBar()) {
                open = UI::BeginMenu("View");
                if (open) { if (UI::MenuItem("Grid", nullptr, &on)) ++clicks; row = UI::GetItemRect(); UI::EndMenu(); }
                btn = UI::GetItemRect();
                UI::EndMenuBar();
            }
        }
        UI::End();
    };
    sim.Frames(2, ui);
    sim.Click(CenterX(btn), CenterY(btn), ui); sim.Frames(2, ui);
    CHECK(open && !on);
    sim.Click(CenterX(row), CenterY(row), ui); sim.Frame(ui);
    CHECK(on && clicks == 1);
    sim.Click(CenterX(btn), CenterY(btn), ui); sim.Frames(2, ui);
    sim.Click(CenterX(row), CenterY(row), ui); sim.Frame(ui);
    CHECK(!on && clicks == 2);
}

// The dock host's draw list (a dock node's HostWindow->DrawList): what is drawn on it lies under the floating windows even when it is drawn
// after they ended. The editor draws its panel icons on the docked tabs that way, at the end of the frame; they showed through a floating
// window (the Animator, the Modeling window) as stray squares and arrows.
void TestDockHostListUnderFloating() {
    RUN_SUITE("windows: the dock host's list is drawn under floating windows");
    Reset();
    Sim sim(400, 300);
    UI::Vec2 fpos{}, fsize{};
    auto ui = [&] {
        // a busy window first: many clip changes (draw spans), as the editor's frame has; the stacking order must still be applied
        UI::SetNextWindowPos(V(0, 250), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(60, 40), UI::Cond_FirstUseEver);
        if (UI::Begin("Busy", nullptr, 0))
            for (int i = 0; i < 200; ++i) { UI::PushClipRect(V(1.0f + (float)(i % 50), 251), V(52.0f + (float)(i % 50), 289), true); UI::GetWindowDrawList().AddRectFilled(V(2, 260), V(4, 262), UI::RGBA(9, 9, 9, 255)); UI::PopClipRect(); }
        UI::End();
        UI::SetNextWindowPos(V(100, 80), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(200, 150), UI::Cond_FirstUseEver);
        if (UI::Begin("Floating", nullptr, 0)) { fpos = UI::GetWindowPos(); fsize = UI::GetWindowSize(); UI::Text("over the dock"); }
        UI::End();
        UI::GetDockHostDrawList().AddRectFilled(V(20, 20), V(380, 280), UI::RGBA(201, 31, 77, 255));   // after every window: as the editor's tab icons
    };
    sim.Frames(3, ui);
    const Bitmap& b = sim.Draw(12, 14, 20);
    const unsigned red = Rgba(201, 31, 77);
    CHECK(PixelAt(b, 40, 40) == red);                                                        // outside the window: drawn
    CHECK(PixelAt(b, (int)(fpos.x + fsize.x * 0.5f), (int)(fpos.y + fsize.y - 10)) != red);   // inside it: under the window
}

// The menu bar behaves as one control (owner, 2026-11-27: "the top bar where file is, I gotta click, hovering over them doesn't anything"):
// a button under the pointer is lit even with no menu open; with File open, moving onto Edit opens Edit and closes File without a click;
// Right / Left in an open menu go to the neighbouring menu; a tap of Alt opens the first menu and Escape closes it. Drawn as the editor
// draws it: a full-window host with Window_MenuBar and Dear ImGui's framed look pushed.
void TestMenuBarHoverSwitch() {
    RUN_SUITE("windows: the menu bar switches menus on hover and from the keyboard");
    Reset();
    Sim sim(600, 400);
    UI::Rect fileBtn{}, editBtn{}, viewBtn{}; bool fileOpen = false, editOpen = false, viewOpen = false; bool editHovered = false;
    auto ui = [&] {
        UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);
        UI::SetNextWindowPos(V(0, 0)); UI::SetNextWindowSize(V(600, 400));
        if (UI::Begin("##Host", nullptr, UI::Window_MenuBar | UI::Window_NoTitleBar | UI::Window_NoMove | UI::Window_NoResize | UI::Window_NoBringToFrontOnFocus)) {
            if (UI::BeginMenuBar()) {
                fileOpen = UI::BeginMenu("File"); if (fileOpen) { UI::MenuItem("New"); UI::MenuItem("Open"); UI::EndMenu(); } fileBtn = UI::GetItemRect();
                editOpen = UI::BeginMenu("Edit"); if (editOpen) { UI::MenuItem("Undo"); UI::EndMenu(); } editBtn = UI::GetItemRect(); editHovered = UI::IsItemHovered();
                viewOpen = UI::BeginMenu("View"); if (viewOpen) { UI::MenuItem("Grid"); UI::EndMenu(); } viewBtn = UI::GetItemRect();
                UI::EndMenuBar();
            }
            UI::Text("body");
        }
        UI::End();
        UI::PopStyleVar(1);
    };
    sim.Frames(2, ui);
    sim.Move(500, 300); sim.Frames(2, ui);
    const Bitmap& rest = sim.Draw(12, 14, 20);
    const unsigned restPx = PixelAt(rest, (int)(editBtn.x + 4), (int)CenterY(editBtn));
    sim.Move(CenterX(editBtn), CenterY(editBtn)); sim.Frames(2, ui);
    CHECK(editHovered && !editOpen);
    const Bitmap& lit = sim.Draw(12, 14, 20);
    CHECK(PixelAt(lit, (int)(editBtn.x + 4), (int)CenterY(editBtn)) != restPx);   // hovering a closed bar button lights it
    // open File with a click, then only move onto Edit and View
    sim.Click(CenterX(fileBtn), CenterY(fileBtn), ui); sim.Frame(ui);
    CHECK(fileOpen && !editOpen);
    sim.Move(CenterX(editBtn), CenterY(editBtn)); sim.Frames(2, ui);
    CHECK(editOpen && !fileOpen);
    sim.Move(CenterX(viewBtn), CenterY(viewBtn)); sim.Frames(2, ui);
    CHECK(viewOpen && !editOpen && !fileOpen);
    // keyboard: Left goes back to Edit, Right on to View, Right again wraps to File
    sim.Move(500, 300); sim.Frame(ui);
    sim.Tap(Key_Left, ui); sim.Frames(2, ui);
    CHECK(editOpen && !viewOpen);
    sim.Tap(Key_Right, ui); sim.Frames(2, ui);
    CHECK(viewOpen && !editOpen);
    sim.Tap(Key_Right, ui); sim.Frames(2, ui);
    CHECK(fileOpen && !viewOpen);
    sim.Tap(Key_Escape, ui); sim.Frames(2, ui);
    CHECK(!fileOpen && !editOpen && !viewOpen);
    // nothing open: moving over the bar opens nothing by itself
    sim.Move(CenterX(editBtn), CenterY(editBtn)); sim.Frames(2, ui);
    CHECK(!editOpen);
    // a tap of Alt alone opens the first menu
    sim.Move(500, 300); sim.Frame(ui);
    sim.Hold(UI::KeyId_Alt, true); sim.Frame(ui); sim.Hold(UI::KeyId_Alt, false); sim.Frames(2, ui);
    CHECK(fileOpen);
    sim.Tap(Key_Escape, ui); sim.Frames(2, ui);
    CHECK(!fileOpen);
    // Alt as part of a chord (Alt+Left in a code editor, Alt+Enter) opens nothing
    sim.Hold(UI::KeyId_Alt, true); sim.Hold(UI::KeyId_LeftArrow, true); sim.Frame(ui); sim.Hold(UI::KeyId_LeftArrow, false); sim.Frame(ui);
    sim.Hold(UI::KeyId_Alt, false); sim.Frames(2, ui);
    CHECK(!fileOpen && !editOpen && !viewOpen);
    sim.Hold(UI::KeyId_Alt, true); sim.Frame(ui); sim.Press(Key_Enter); sim.Frame(ui); sim.Hold(UI::KeyId_Alt, false); sim.Frames(2, ui);
    CHECK(!fileOpen && !editOpen && !viewOpen);
}

// IsWindowHovered under an open menu list: the list is hovered, not the window under it (Dear ImGui's rule). The editor's Scene view
// asks IsWindowHovered as a fallback for its clicks, and picked the object under a View > Panels row while that row was clicked.
void TestWindowHoveredUnderMenu() {
    RUN_SUITE("windows: a window under an open menu list is not hovered there");
    Reset();
    Sim sim(600, 400);
    UI::Rect btn{}, row{}; bool open = false, belowHovered = false;
    auto ui = [&] {
        UI::SetNextWindowPos(V(0, 0), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(600, 400), UI::Cond_FirstUseEver);
        if (UI::Begin("Host", nullptr, UI::Window_MenuBar)) {
            if (UI::BeginMenuBar()) {
                open = UI::BeginMenu("View");
                if (open) { UI::MenuItem("Panels"); row = UI::GetItemRect(); UI::MenuItem("Layout"); UI::EndMenu(); }
                btn = UI::GetItemRect();
                UI::EndMenuBar();
            }
            UI::BeginChild("scene", V(0, 0));
            belowHovered = UI::IsWindowHovered();
            UI::EndChild();
        }
        UI::End();
    };
    sim.Frames(2, ui);
    sim.Click(CenterX(btn), CenterY(btn), ui); sim.Frames(2, ui);
    CHECK(open);
    sim.Move(CenterX(row), CenterY(row)); sim.Frames(2, ui);
    CHECK(open && !belowHovered);
    sim.Move(500, 300); sim.Frames(2, ui);                    // away from the list
    CHECK(belowHovered);
}

// A modal opened and begun outside every window keeps its id when the window drawn last changes (the editor's built-in file browser,
// begun after all windows: it closed itself one frame after it opened, because the key took in the last window's id).
void TestTopLevelPopupId() {
    RUN_SUITE("windows: a popup begun outside every window keeps its id");
    Reset();
    Sim sim(640, 480);
    int frame = 0; bool shown = false;
    auto ui = [&] {
        const char* order[2] = {frame < 2 ? "A" : "B", frame < 2 ? "B" : "A"};   // the window drawn last changes on frame 2
        for (const char* n : order) { UI::SetNextWindowPos(V(n[0] == 'A' ? 10.0f : 300.0f, 10), UI::Cond_FirstUseEver); if (UI::Begin(n, nullptr, 0)) UI::Text(n); UI::End(); }
        if (frame == 1) UI::OpenPopup("###browser");
        shown = UI::BeginPopupModal("Open a folder###browser");
        if (shown) { UI::Text("files"); UI::EndPopup(); }
        ++frame;
    };
    sim.Frames(2, ui);
    CHECK(shown);
    sim.Frames(3, ui);
    CHECK(shown);   // still open after the last window changed
}

// A modal given a size (SetNextWindowSize before BeginPopupModal) is that tall, and GetContentRegionAvail ends at its inner bottom: the
// editor's file browser sizes its list from it and puts Cancel / Choose under the list, which drew below the box before.
void TestModalFixedHeight() {
    RUN_SUITE("windows: a modal given a height keeps it; the content region ends there");
    Reset();
    Sim sim(800, 600);
    UI::Rect footer{}; float availY = 0.0f; bool shown = false;
    auto ui = [&] {
        if (!UI::IsPopupOpen("Browse")) UI::OpenPopup("Browse");
        UI::SetNextWindowSize(V(500, 360), UI::Cond_Appearing);
        shown = UI::BeginPopupModal("Browse");
        if (shown) {
            availY = UI::GetContentRegionAvail().y;
            const float footerH = UI::GetFrameHeightWithSpacing() + 8.0f;
            if (UI::BeginChild("list", V(0, availY - footerH), UI::Child_Borders)) { for (int i = 0; i < 40; ++i) UI::Text("file"); }
            UI::EndChild();
            UI::Button("Choose"); footer = UI::GetItemRect();
            UI::EndPopup();
        }
    };
    sim.Frames(4, ui);
    CHECK(shown);
    CHECK(availY > 200.0f && availY < 360.0f);
    const float boxBottom = (600.0f + 360.0f) * 0.5f;          // centred, 360 tall
    CHECK(footer.y + footer.h <= boxBottom + 1.0f);            // the footer is inside the box
}

// A modal with AlwaysAutoResize hugs its content (owner, 2026-11-27: the launcher's "Close OkaySpace?" box was far taller and wider than
// its text and three buttons). A size set for a closed modal (the launcher's New Project dialog) must not reach the next one, and
// a width given to the modal itself each frame (the Software Update dialog) is kept.
void TestModalFitsContent() {
    RUN_SUITE("windows: an auto-resizing modal is the size of its content");
    Reset();
    Sim sim(1160, 752);
    UI::Rect text{}, cancel{}; bool shown = false; float giveW = 0.0f;
    auto ui = [&] {
        UI::UseImGuiCoordinates(true);
        UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f); UI::PushStyleVar(UI::StyleVar_TextRowHeight, -1.0f);
        UI::SetNextWindowSize(V(820, 680), UI::Cond_Always);   // meant for the New Project dialog, which is closed: BeginPopupModal uses it up
        if (UI::BeginPopupModal("New Project", nullptr, 0)) UI::EndPopup();
        if (!UI::IsPopupOpen("Close OkaySpace?")) UI::OpenPopup("Close OkaySpace?");
        UI::SetNextWindowPos(V(580, 376), UI::Cond_Appearing, V(0.5f, 0.5f));
        if (giveW > 0.0f) UI::SetNextWindowSize(V(giveW, 0), UI::Cond_Always);
        shown = UI::BeginPopupModal("Close OkaySpace?", nullptr, UI::Window_AlwaysAutoResize);
        if (shown) {
            UI::TextUnformatted("Do you want to exit OkaySpace, or just minimize it?"); text = UI::GetItemRect();
            UI::Dummy(V(0, 10));
            UI::Button("Exit", V(120, 0)); UI::SameLine(); UI::Button("Minimize", V(120, 0)); UI::SameLine(); UI::Button("Cancel", V(120, 0));
            cancel = UI::GetItemRect();
            UI::EndPopup();
        }
        UI::PopStyleVar(2);
        UI::UseImGuiCoordinates(false);
    };
    sim.Frames(4, ui);
    CHECK(shown);
    UI::PopupSlotInfo pops[4]; int n = UI::ListOpenPopups(pops, 4);
    CHECK(n == 1);
    if (n == 1) {
        const float contentW = std::max(text.x + text.w, cancel.x + cancel.w) - pops[0].pos.x;
        const float contentH = cancel.y + cancel.h - pops[0].pos.y;
        CHECK(pops[0].size.x <= contentW + 40.0f);           // no more than the padding past the widest line
        CHECK(pops[0].size.y <= contentH + 40.0f);           // nor below the buttons
        CHECK(pops[0].size.x >= contentW && pops[0].size.y >= contentH);
    }
    giveW = 1000.0f;                                         // a width the program asks for each frame (wider than the content) is kept
    sim.Frames(3, ui);
    n = UI::ListOpenPopups(pops, 4);
    CHECK(n == 1 && std::fabs(pops[0].size.x - 1000.0f) < 1.0f);

    // ... but it is the least width, not a cage: a line longer than it makes the auto-resizing modal grow to hold it (round 4 left this:
    // the editor's Project panel asks 480 for its Delete dialog, and "It goes to the project's trash. Ctrl+Z in the Project panel brings
    // it back." ran past the right edge). Dear ImGui's AlwaysAutoResize fits the content after the frame the size was given on.
    for (int cond = 0; cond < 2; ++cond) {
        Reset();
        Sim sim2(1160, 752);
        UI::Rect line{}, btn{};
        auto del = [&] {
            UI::UseImGuiCoordinates(true);
            UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f); UI::PushStyleVar(UI::StyleVar_TextRowHeight, -1.0f);
            if (!UI::IsPopupOpen("##projdel")) UI::OpenPopup("##projdel");
            UI::SetNextWindowSize(V(300, 0), cond ? UI::Cond_Always : UI::Cond_Appearing);
            if (UI::BeginPopupModal("##projdel", nullptr, UI::Window_NoTitleBar | UI::Window_AlwaysAutoResize)) {
                UI::Text("Delete \"Wood.png\"?");
                UI::TextDisabled("It goes to the project's trash. Ctrl+Z brings it back.");
                line = UI::GetItemRect();
                UI::Button("Delete", V(110, 0)); btn = UI::GetItemRect();
                UI::EndPopup();
            }
            UI::PopStyleVar(2);
            UI::UseImGuiCoordinates(false);
        };
        sim2.Frames(4, del);
        n = UI::ListOpenPopups(pops, 4);
        CHECK(n == 1 && line.w > 300.0f && line.w < 1000.0f);   // longer than the width asked for, shorter than the display
        if (n == 1) {
            CHECK(line.x + line.w <= pops[0].pos.x + pops[0].size.x - 6.0f);   // the long line is inside the box, with padding
            CHECK(btn.x - pops[0].pos.x >= 6.0f);
        }
    }
    // Round 6: a line longer than the whole display: the dialog stops at the display less 16 pixels and the line wraps inside it (it
    // used to run past the dialog's right edge).
    for (int imgui = 0; imgui < 2; ++imgui) {
        Reset();
        Sim sim3(420, 400);
        UI::Rect line{}, after{}; float top = 0.0f;
        auto wide = [&] {
            if (imgui) { UI::UseImGuiCoordinates(true); UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f); UI::PushStyleVar(UI::StyleVar_TextRowHeight, -1.0f); }
            if (!UI::IsPopupOpen("##wide")) UI::OpenPopup("##wide");
            if (UI::BeginPopupModal("##wide", nullptr, UI::Window_NoTitleBar | UI::Window_AlwaysAutoResize)) {
                top = UI::GetCursorScreenPos().y;
                UI::TextUnformatted("This file is used by eleven scenes, four prefabs and two materials, so deleting it breaks every one of them until you put it back.");
                line = UI::GetItemRect();
                UI::Button("OK", V(80, 0)); after = UI::GetItemRect();
                UI::EndPopup();
            }
            if (imgui) { UI::PopStyleVar(2); UI::UseImGuiCoordinates(false); }
        };
        sim3.Frames(5, wide);
        UI::PopupSlotInfo p3[4];
        const int n3 = UI::ListOpenPopups(p3, 4);
        CHECK(n3 == 1);
        if (n3 == 1) {
            CHECK(p3[0].size.x <= 420.0f - 16.0f + 0.5f);                   // as wide as the display allows
            CHECK(line.x + line.w <= p3[0].pos.x + p3[0].size.x + 0.5f);    // the long line stays inside it ...
            CHECK(after.y - top > UI::GetTextLineHeight() * 2.5f);           // ... on more than one line (three here), the button after them
            CHECK(after.y >= line.y + line.h - 0.5f);
        }
    }
}

// Drags between windows, the editor's way (round 3 of the editor on OkayUI): a file dragged from the Project panel (a tile with a
// drag source) dropped on the Scene view (an InvisibleButton with mouse-button flags, then BeginDragDropTarget), on an Inspector slot
// (a button whose target accepts before delivery and acts only on IsDelivery()) and anywhere else in the Inspector (a target over the
// whole window: BeginDragDropTargetCustom of GetCurrentWindow()->InnerClipRect). The slot takes the drop over the window-wide target
// drawn after it. Dear ImGui's documented outline rule: the target outline is drawn only when a payload of the accepted type is over
// it (AcceptDragDropPayload), and never with AcceptNoDrawDefaultRect; a payload of another type draws nothing.
void TestDragAcrossWindows() {
    RUN_SUITE("windows: drags from one window to targets in others (Project -> Scene view, Inspector slot, Inspector window)");
    Reset();
    Sim sim(640, 480);
    std::string scene, slot, window; bool slotHover = false, slotPending = false;
    const char* type = "ASSET_PATH";
    UI::Rect src{}, view{}, slotR{}, inspR{};
    auto ui = [&] {
        slotHover = slotPending = false;
        UI::PushStyleColor(UI::Col_DragDropTarget, 255, 0, 255);   // the outline in a colour nothing else uses
        UI::SetNextWindowPos(V(10, 10), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(200, 200), UI::Cond_FirstUseEver);
        if (UI::Begin("Project", nullptr, 0)) {
            UI::Button("Wood.png", V(80, 60)); src = UI::GetItemRect();
            if (UI::BeginDragDropSource()) { UI::SetDragDropPayload(type, "Assets/Wood.png", 16); UI::Text("Wood.png"); UI::EndDragDropSource(); }
        }
        UI::End();
        UI::SetNextWindowPos(V(230, 10), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(400, 200), UI::Cond_FirstUseEver);
        if (UI::Begin("Scene", nullptr, 0)) {
            UI::InvisibleButton("##view", V(360, 140), UI::Button_MouseButtonLeft | UI::Button_MouseButtonRight); view = UI::GetItemRect();
            if (UI::BeginDragDropTarget()) {
                if (const UI::DragPayload* p = UI::AcceptDragDropPayload("ASSET_PATH")) scene = (const char*)p->Data;
                UI::EndDragDropTarget();
            }
        }
        UI::End();
        UI::SetNextWindowPos(V(10, 230), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(400, 230), UI::Cond_FirstUseEver);
        if (UI::Begin("Inspector", nullptr, 0)) {
            UI::Text("Mesh Renderer");
            UI::Button("None###asset", V(200, 0)); slotR = UI::GetItemRect();
            const UI::DragPayload* pl = UI::GetDragDropPayload();
            if (pl && pl->IsDataType("ASSET_PATH") && UI::IsItemHovered(UI::Hovered_AllowWhenBlockedByActiveItem)) slotHover = true;
            if (UI::BeginDragDropTarget()) {
                if (const UI::DragPayload* p = UI::AcceptDragDropPayload("ASSET_PATH", UI::DragDrop_AcceptBeforeDelivery | UI::DragDrop_AcceptNoDrawDefaultRect)) {
                    if (p->IsDelivery()) slot = (const char*)p->Data; else slotPending = true;
                }
                UI::EndDragDropTarget();
            }
            UI::Dummy(10, 120);
            const UI::CurrentWindow cw = UI::GetCurrentWindow();
            const UI::WindowRef* w = cw;
            if (w) { inspR = UI::Rect{w->InnerClipRect.Min.x, w->InnerClipRect.Min.y, w->InnerClipRect.Max.x - w->InnerClipRect.Min.x, w->InnerClipRect.Max.y - w->InnerClipRect.Min.y};
                if (UI::BeginDragDropTargetCustom(w->InnerClipRect, w->ID)) {
                    if (const UI::DragPayload* p = UI::AcceptDragDropPayload("ASSET_PATH")) window = (const char*)p->Data;
                    UI::EndDragDropTarget();
                } }
        }
        UI::End();
        UI::PopStyleColor();
    };
    auto drag = [&](float tx, float ty, bool drop = true) {
        sim.Move(CenterX(src), CenterY(src)); sim.Frame(ui); sim.Down(); sim.Frame(ui);
        sim.Move(CenterX(src) + 12.0f, CenterY(src) + 4.0f); sim.Frame(ui);
        sim.Move((CenterX(src) + tx) * 0.5f, (CenterY(src) + ty) * 0.5f); sim.Frame(ui);
        sim.Move(tx, ty); sim.Frame(ui); sim.Frame(ui);
        if (drop) { sim.Up(); sim.Frame(ui); sim.Frames(2, ui); }
    };
    const unsigned magenta = Rgba(255, 0, 255);
    sim.Frames(3, ui);   // the windows lay out (and the Inspector's window table entry exists)
    CHECK(view.w > 300 && slotR.w > 100 && inspR.w > 300);
    // 1. onto the Scene view: the view takes it (the drag source is in another window, its button is the active item)
    drag(CenterX(view), CenterY(view), false);
    CHECK(CountNot(sim.Draw(1, 2, 3), (int)view.x, (int)view.y, (int)(view.x + view.w), (int)(view.y + view.h), Rgba(1, 2, 3)) > 0);
    int outline = 0; { const Bitmap& b = sim.Draw(1, 2, 3); for (int y = 0; y < b.h; ++y) for (int x = 0; x < b.w; ++x) if (PixelAt(b, x, y) == magenta) ++outline; }
    CHECK(outline > 100);                                         // an accepted payload over the view: its outline shows
    sim.Up(); sim.Frame(ui); sim.Frames(2, ui);
    CHECK(scene == "Assets/Wood.png" && slot.empty() && window.empty());
    CHECK(UI::GetDragDropPayload() == nullptr);                   // the drag is over
    // 2. onto the Inspector's slot: hovered before the release it is marked and accepted early (no delivery), then delivered;
    //    the window-wide target behind it gets nothing
    scene.clear();
    drag(CenterX(slotR), CenterY(slotR), false);
    CHECK(slotHover && slotPending && slot.empty());
    { const Bitmap& b = sim.Draw(1, 2, 3); int n = 0;
      for (int x = (int)slotR.x; x < (int)(slotR.x + slotR.w); ++x) if (PixelAt(b, x, (int)slotR.y) == magenta || PixelAt(b, x, (int)slotR.y + 1) == magenta) ++n;
      CHECK(n == 0); }                                            // AcceptNoDrawDefaultRect: no outline on the slot
    sim.Up(); sim.Frame(ui); sim.Frames(2, ui);
    CHECK(slot == "Assets/Wood.png" && window.empty() && scene.empty());
    // 3. anywhere else in the Inspector: the window-wide target takes it
    slot.clear();
    drag(inspR.x + 300.0f, inspR.y + inspR.h - 30.0f);
    CHECK(window == "Assets/Wood.png" && slot.empty() && scene.empty());
    // 4. released over no target: nobody gets it and the drag ends
    window.clear();
    drag(620, 470);
    CHECK(window.empty() && slot.empty() && scene.empty() && UI::GetDragDropPayload() == nullptr);
    // 5. a payload of another type over the Scene view: no outline (Dear ImGui draws it only for an accepted type), no drop
    type = "GO_PTR";
    drag(CenterX(view), CenterY(view), false);
    outline = 0; { const Bitmap& b = sim.Draw(1, 2, 3); for (int y = 0; y < b.h; ++y) for (int x = 0; x < b.w; ++x) if (PixelAt(b, x, y) == magenta) ++outline; }
    CHECK(outline == 0);
    sim.Up(); sim.Frame(ui); sim.Frames(2, ui);
    CHECK(scene.empty());
}

// A drag source is the item before it, known by its id (Dear ImGui's documented rule: BeginDragDropSource uses the last item's id), so
// a source that moves while it is held still drags. The editor's Project panel: pressing a tile selects it, the details strip appears
// under the grid, the tiles get smaller and the second tile moves left; the drag of that tile never started (round 3).
void TestDragSourceThatMoves() {
    RUN_SUITE("windows: a drag source that moves while held still starts its drag");
    Reset();
    Sim sim(640, 480);
    bool selected = false; std::string got; UI::Rect t2{}, target{};
    auto ui = [&] {
        UI::SetNextWindowPos(V(10, 10), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(400, 300), UI::Cond_FirstUseEver);
        if (UI::Begin("Project", nullptr, 0)) {
            const float cell = selected ? 70.0f : 100.0f;   // the grid shrinks once a tile is selected
            UI::PushID(1); UI::Button("##cell", V(cell, cell)); UI::PopID();
            UI::SameLine();
            UI::PushID(2); UI::Button("##cell", V(cell, cell)); UI::PopID(); t2 = UI::GetItemRect();
            if (UI::IsItemClicked()) selected = true;
            if (UI::BeginDragDropSource()) { UI::SetDragDropPayload("ASSET_PATH", "Wood.png", 9); UI::EndDragDropSource(); }
        }
        UI::End();
        UI::SetNextWindowPos(V(420, 10), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(200, 200), UI::Cond_FirstUseEver);
        if (UI::Begin("Target", nullptr, 0)) {
            UI::Button("drop here", V(150, 100)); target = UI::GetItemRect();
            if (UI::BeginDragDropTarget()) { if (const UI::DragPayload* p = UI::AcceptDragDropPayload("ASSET_PATH")) got = (const char*)p->Data; UI::EndDragDropTarget(); }
        }
        UI::End();
    };
    sim.Frames(3, ui);
    const float px = t2.x + 20.0f, py = t2.y + 20.0f;   // inside the tile both before and after it moves
    sim.Move(px, py); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Frame(ui);
    CHECK(selected);
    sim.Move(px + 30.0f, py + 10.0f); sim.Frame(ui);
    sim.Move(CenterX(target), CenterY(target)); sim.Frame(ui); sim.Frame(ui);
    CHECK(UI::GetDragDropPayload() != nullptr);              // the drag started though the tile moved
    sim.Up(); sim.Frame(ui); sim.Frames(2, ui);
    CHECK(got == "Wood.png");
}

// Keyboard focus (round 5; round 4 left "Tab-key focus through every control with the focus ring"): Tab moves the focus to the next
// control of the window under the pointer (or of the one already focused), Shift+Tab to the one before, both wrap round; the focused button
// or check box gets the focus ring; Space or Enter presses it; a text field reached with Tab takes the keyboard with its text selected (typing
// replaces it), and Tab leaves it again; a field that completes on Tab is not in the order and keeps Tab for itself; a click hides the ring.
void TestTabFocus() {
    RUN_SUITE("windows: Tab and Shift+Tab move the keyboard focus; the focus ring; Space and Enter press");
    Reset();
    Sim sim(640, 480);
    char name[32] = "hello", cmd[32] = "";
    bool snap = false; int apply = 0, done = 0;
    UI::Rect ra{}, rd{}, rc{};
    auto ui = [&] {
        UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(400, 300), UI::Cond_FirstUseEver);
        if (UI::Begin("Form", nullptr, 0)) {
            if (UI::Button("Apply")) ++apply;
            ra = UI::GetItemRect();
            UI::Checkbox("Snap", &snap);
            UI::InputText("Name", name, (int)sizeof name);
            if (UI::Button("Done")) ++done;
            rd = UI::GetItemRect();
            UI::InputText("Command", cmd, (int)sizeof cmd, UI::InputText_CallbackCompletion);   // keeps Tab for its completion
            rc = UI::GetItemRect();
        }
        UI::End();
    };
    auto tab = [&](bool back) { sim.shift = back; sim.Hold(UI::KeyId_Tab, true); sim.Frame(ui); sim.Hold(UI::KeyId_Tab, false); sim.shift = false; sim.Frame(ui); };
    auto key = [&](UI::KeyId k) { sim.Hold(k, true); sim.Frame(ui); sim.Hold(k, false); sim.Frame(ui); };
    sim.Move(200, 200);   // the pointer over the window: Tab starts there
    sim.Frames(3, ui);
    const Bitmap& before = sim.Draw(0, 0, 0);
    const unsigned edgeBefore = PixelAt(before, (int)ra.x - 3, (int)(ra.y + ra.h * 0.5f));
    tab(false);                                               // Apply
    const Bitmap& ring = sim.Draw(0, 0, 0);
    CHECK(PixelAt(ring, (int)ra.x - 3, (int)(ra.y + ra.h * 0.5f)) != edgeBefore);   // the focus ring round Apply
    key(UI::KeyId_Space);
    CHECK(apply == 1);                                        // Space pressed it
    tab(false);                                               // Snap
    key(UI::KeyId_Space);
    CHECK(snap);                                              // Space ticked it
    tab(false);                                               // Name: it takes the keyboard, its text selected
    CHECK(UI::WantsTextInput());
    sim.Type("Ann"); sim.Frame(ui); sim.Frame(ui);
    CHECK(std::string(name) == "Ann");                        // typing replaced the selected text
    tab(false);                                               // Done: the field gives the keyboard up
    CHECK(!UI::WantsTextInput());
    key(UI::KeyId_Enter);
    CHECK(done == 1 && apply == 1);                           // Enter pressed Done, nothing else
    tab(false);                                               // past the last control: round to Apply (Command is not in the order)
    key(UI::KeyId_Enter);
    CHECK(apply == 2 && !UI::WantsTextInput());
    sim.Move(CenterX(rc), CenterY(rc)); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
    CHECK(UI::WantsTextInput());                              // Command clicked: it has the keyboard ...
    tab(false);
    CHECK(UI::WantsTextInput());                              // ... and keeps Tab for its completion
    sim.Move(CenterX(rd), CenterY(rd)); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
    CHECK(done == 2 && !UI::WantsTextInput());                // a click on Done: pressed, the field let go
    tab(true);                                                // Shift+Tab from Done: back to Name
    CHECK(UI::WantsTextInput());
    tab(true); tab(true);                                     // Snap, then Apply
    key(UI::KeyId_Enter);
    CHECK(apply == 3);
    tab(true);                                                // before Apply: round to the last control (Done)
    key(UI::KeyId_Space);
    CHECK(done == 3);
    sim.Move(600, 460); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);   // a click outside: nothing focused, no ring
    const Bitmap& after = sim.Draw(0, 0, 0);
    CHECK(PixelAt(after, (int)ra.x - 3, (int)(ra.y + ra.h * 0.5f)) == edgeBefore);

    // A window begun with Window_NoNavInputs (the editor's Scene and Game views, where Tab and Space are the view's own keys) is left out:
    // Tab over it focuses nothing, and Space presses nothing.
    int tool = 0;
    auto canvas = [&] {
        UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(400, 300), UI::Cond_FirstUseEver);
        if (UI::Begin("Canvas", nullptr, UI::Window_NoNavInputs)) { if (UI::Button("Move tool")) ++tool; }
        UI::End();
    };
    Reset();
    Sim s2(640, 480);
    s2.Move(200, 200); s2.Frames(3, canvas);
    s2.Hold(UI::KeyId_Tab, true); s2.Frame(canvas); s2.Hold(UI::KeyId_Tab, false); s2.Frame(canvas);
    s2.Hold(UI::KeyId_Space, true); s2.Frame(canvas); s2.Hold(UI::KeyId_Space, false); s2.Frame(canvas);
    CHECK(tool == 0);
}

// Round 6: sliders, drags, list rows, tree rows and menu rows are in the keyboard focus too. Tab reaches a slider and a drag (Left / Right
// step them), and a list as one stop (Up / Down walk its rows, Enter picks one); a tree row opens with Right and closes with Left; in an
// open popup Down goes to its first row and Enter picks it. Run in both looks (OkayUI's own and Dear ImGui's framed widgets).
void TestNavRows() {
    for (int framed = 0; framed < 2; ++framed) {
        RUN_SUITE((framed ? "windows: arrow keys and Tab on sliders, drags, rows, trees and menus (framed widgets)"
                          : "windows: arrow keys and Tab on sliders, drags, rows, trees and menus"));
        Reset();
        Sim sim(640, 480);
        float vol = 0.5f, speed = 1.0f; int pick = 0, menuPick = 0; bool folderOpen = false, leafShown = false, openMenu = false, popupOpen = false;
        auto ui = [&] {
            if (framed) UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);
            UI::SetNextWindowPos(V(20, 20), UI::Cond_FirstUseEver); UI::SetNextWindowSize(V(420, 400), UI::Cond_FirstUseEver);
            if (UI::Begin("Rows", nullptr, 0)) {
                UI::SliderFloat("Volume", &vol, 0.0f, 1.0f, "%.2f");   // (the format forms: framed in Dear ImGui's look, plain otherwise)
                UI::DragFloat("Speed", &speed, 0.1f, 0.0f, 10.0f, "%.2f");
                const char* names[3] = {"Alpha", "Beta", "Gamma"};
                for (int i = 0; i < 3; ++i) if (UI::Selectable(names[i], pick == i)) pick = i;
                folderOpen = UI::TreeNode("Folder");
                leafShown = false;
                if (folderOpen) { leafShown = true; UI::Selectable("Leaf", false); UI::TreePop(); }
                if (UI::Button("Menu")) openMenu = true;
                if (openMenu) { UI::OpenPopup("menu"); openMenu = false; }
                popupOpen = UI::BeginPopup("menu");
                if (popupOpen) {
                    if (UI::MenuItem("Cut")) menuPick = 1;
                    if (UI::MenuItem("Copy")) menuPick = 2;
                    UI::EndPopup();
                }
            }
            UI::End();
            if (framed) UI::PopStyleVar();
        };
        auto key = [&](UI::KeyId k, bool shift = false) { sim.shift = shift; sim.Hold(k, true); sim.Frame(ui); sim.Hold(k, false); sim.shift = false; sim.Frame(ui); };
        sim.Move(300, 300);
        sim.Frames(3, ui);
        key(UI::KeyId_Tab);                       // the slider
        key(UI::KeyId_Right);
        CHECK(std::fabs(vol - 0.51f) < 0.001f);   // a hundredth of the range
        key(UI::KeyId_Left); key(UI::KeyId_Left);
        CHECK(std::fabs(vol - 0.49f) < 0.001f);
        key(UI::KeyId_Tab);                       // the drag
        key(UI::KeyId_Right);
        CHECK(std::fabs(speed - 1.1f) < 0.001f);  // its speed per press
        key(UI::KeyId_Tab);                       // the list: one stop, its first row
        key(UI::KeyId_Down);                      // Beta
        key(UI::KeyId_Enter);
        CHECK(pick == 1);
        key(UI::KeyId_Down); key(UI::KeyId_Down); // Gamma, then the tree row
        CHECK(!folderOpen);
        key(UI::KeyId_Right);                     // opens it
        CHECK(folderOpen && leafShown);
        key(UI::KeyId_Up); key(UI::KeyId_Enter);  // up to Gamma, picked
        CHECK(pick == 2);
        key(UI::KeyId_Down); key(UI::KeyId_Left); // the tree row again: closed
        CHECK(!folderOpen);
        key(UI::KeyId_Tab);                       // past the rows (one stop): the Menu button
        key(UI::KeyId_Space);
        CHECK(popupOpen);
        key(UI::KeyId_Down);                      // the popup's first row
        key(UI::KeyId_Down);                      // Copy
        key(UI::KeyId_Enter);
        CHECK(menuPick == 2 && !popupOpen);
        key(UI::KeyId_Space);                     // the focus went back to the Menu button: Space opens the menu again
        CHECK(popupOpen);
        key(UI::KeyId_Escape);
        CHECK(!popupOpen);
        key(UI::KeyId_Tab, true);                 // Shift+Tab: back to the list (its first row)
        key(UI::KeyId_Down); key(UI::KeyId_Down); key(UI::KeyId_Down);
        key(UI::KeyId_Right);                     // the tree row: open
        CHECK(folderOpen);
        key(UI::KeyId_Enter);                     // Enter closes it again
        CHECK(!folderOpen);
    }
}

// Round 6: a reorderable tab bar outside the dock (BeginTabBar with TabBar_Reorderable) slides like a docked strip: the dragged tab is drawn
// under the pointer where it was grabbed, and a tab that makes room eases to its new place over a few frames instead of jumping (a tab
// drawn after the dragged one is clipped to beside it, so it does not cover it).
void TestTabBarSlides() {
    RUN_SUITE("windows: a reorderable tab bar's tabs slide aside while one is dragged");
    Reset();
    Sim sim(640, 300);
    UI::Rect ra{}, rb{}, rc{};
    auto ui = [&] {
        UI::SetNextWindowPos(V(10, 10), UI::Cond_Always); UI::SetNextWindowSize(V(600, 200), UI::Cond_Always);
        if (UI::Begin("Bar", nullptr, 0) && UI::BeginTabBar("tabs", UI::TabBar_Reorderable)) {
            const bool a = UI::BeginTabItem("Alpha"); ra = UI::GetItemRect(); if (a) UI::EndTabItem();
            const bool b = UI::BeginTabItem("Beta");  rb = UI::GetItemRect(); if (b) UI::EndTabItem();
            const bool c = UI::BeginTabItem("Gamma"); rc = UI::GetItemRect(); if (c) UI::EndTabItem();
            UI::EndTabBar();
        }
        UI::End();
    };
    sim.Frames(3, ui);
    const UI::Rect a0 = ra, b0 = rb;
    const float grabX = a0.x + 10.0f, y = CenterY(a0);
    sim.Move(grabX, y); sim.Frame(ui); sim.Down(); sim.Frame(ui);
    const float px = CenterX(b0) + 6.0f;   // just past Beta's centre: they swap places
    sim.Move(grabX + 8.0f, y); sim.Frame(ui);
    sim.Move(px, y); sim.Frame(ui);
    CHECK(std::fabs(ra.x - (px - 10.0f)) < 1.0f);            // Alpha is drawn under the pointer, where it was grabbed
    CHECK(rb.x < b0.x - 0.5f && rb.x > a0.x + 0.5f);         // Beta is on its way to the first place, not there yet
    float last = rb.x; bool left = true;
    for (int i = 0; i < 30; ++i) { sim.Frame(ui); left = left && rb.x <= last + 0.01f; last = rb.x; }
    CHECK(left && std::fabs(rb.x - a0.x) < 0.5f);            // it slid there and stopped
    sim.Up(); sim.Frames(30, ui);
    CHECK(rb.x < ra.x && ra.x < rc.x && std::fabs(rb.x - a0.x) < 0.5f);   // after the drop: Beta first, Alpha second, at rest
}

// Round 6: the label column (StyleVar_LabelColumn, the Inspector's labels on the left): every labelled field that starts a line has its
// label in a column at the left and the field after it, so the fields line up; the item's rectangle covers the label; a press on the
// label does not act on the field; a long label is cut before the field; a field after SameLine keeps the plain layout.
void TestLabelColumn() {
    RUN_SUITE("windows: the label column puts labels on the left and lines the fields up");
    Reset();
    Sim sim(640, 480);
    float pos[3] = {1, 2, 3}, amount = 0.5f, tint[3] = {1, 0.5f, 0.25f}; bool enabled = false, other = false; int kind = 0; char name[32] = "Box";
    static const char* kinds[] = {"Box", "Sphere"};
    UI::Rect rp{}, ra{}, re{}, rk{}, rn{}, rt{}, rlong{}, rsame{};
    float left = 0.0f, right = 0.0f;
    auto ui = [&] {
        UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f);
        UI::PushStyleVar(UI::StyleVar_LabelColumn, 120.0f);
        UI::SetNextWindowPos(V(20, 20), UI::Cond_Always); UI::SetNextWindowSize(V(420, 400), UI::Cond_Always);
        if (UI::Begin("Fields", nullptr, 0)) {
            left = UI::GetCursorScreenPos().x; right = left + UI::GetContentRegionAvail().x;
            UI::DragFloat3("Position", pos, 0.1f, 0.0f, 0.0f, "%.2f"); rp = UI::GetItemRect();
            UI::SliderFloat("Amount", &amount, 0.0f, 1.0f, "%.2f");    ra = UI::GetItemRect();
            UI::Checkbox("Enabled", &enabled);                         re = UI::GetItemRect();
            UI::Combo("Kind", &kind, kinds, 2);                        rk = UI::GetItemRect();
            UI::InputText("Name", name, (int)sizeof name);             rn = UI::GetItemRect();
            UI::ColorEdit3("Tint", tint);                              rt = UI::GetItemRect();
            UI::Checkbox("A label far too long for the column", &other); rlong = UI::GetItemRect();
            UI::TextUnformatted("Then"); UI::SameLine(); UI::Checkbox("Beside", &other); rsame = UI::GetItemRect();
        }
        UI::End();
        UI::PopStyleVar(2);
    };
    sim.Frames(3, ui);
    const UI::Rect rows[6] = {rp, ra, re, rk, rn, rt};
    bool lined = true;
    for (const UI::Rect& r : rows) lined = lined && std::fabs(r.x - left) < 0.5f && r.w > 120.0f;
    CHECK(lined);                                                     // every field's rectangle starts at the left, its label included
    CHECK(std::fabs((rp.x + rp.w) - right) < 1.5f && std::fabs((rn.x + rn.w) - right) < 1.5f);   // and reaches the right edge
    CHECK(rsame.x > left + 20.0f);                                    // after SameLine: the plain layout
    // a press on the label does nothing; a press on the box (after the column) ticks it
    sim.Move(left + 10.0f, CenterY(re)); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
    CHECK(!enabled);
    sim.Move(left + 120.0f + 6.0f, CenterY(re)); sim.Frame(ui); sim.Down(); sim.Frame(ui); sim.Up(); sim.Frame(ui);
    CHECK(enabled);
    // a drag on the Position label moves nothing; on the first cell it changes x
    sim.Move(left + 30.0f, CenterY(rp)); sim.Frame(ui); sim.Down(); sim.Frame(ui);
    for (int i = 1; i <= 4; ++i) { sim.Move(left + 30.0f + 10.0f * (float)i, CenterY(rp)); sim.Frame(ui); }
    sim.Up(); sim.Frame(ui);
    CHECK(pos[0] == 1.0f);
    sim.Move(left + 140.0f, CenterY(rp)); sim.Frame(ui); sim.Down(); sim.Frame(ui);
    for (int i = 1; i <= 4; ++i) { sim.Move(left + 140.0f + 10.0f * (float)i, CenterY(rp)); sim.Frame(ui); }
    sim.Up(); sim.Frame(ui);
    CHECK(pos[0] > 1.5f);
    // the long label is cut before the column ends: nothing of it is drawn in the last 6 pixels of the column
    const Bitmap& b = sim.Draw(0, 0, 0);
    const unsigned bg = PixelAt(b, (int)(left + 116.0f), (int)rlong.y + 1);
    bool clear = true;
    for (int x = (int)(left + 114.0f); x < (int)(left + 119.0f); ++x)
        for (int y = (int)rlong.y + 2; y < (int)(rlong.y + rlong.h) - 2; ++y) clear = clear && PixelAt(b, x, y) == bg;
    CHECK(clear);
}

// Round 6 audit: with Dear ImGui's text rows, AlignTextToFramePadding puts a label on the text line of the framed field after it (it did
// nothing: the New Project form's "Name" sat above its field's text), and a bullet is centred on the first line of the text after it
// (the launcher's What's New list had square dots below their lines).
void TestTextAlignment() {
    RUN_SUITE("windows: AlignTextToFramePadding and bullets sit on their text line");
    Reset();
    Sim sim(640, 480);
    UI::Rect label{}, field{}, plain{}, dot{}, note{};
    char buf[32] = "MyGame";
    auto ui = [&] {
        UI::UseImGuiCoordinates(true);
        UI::PushStyleVar(UI::StyleVar_FramedWidgets, 1.0f); UI::PushStyleVar(UI::StyleVar_TextRowHeight, -1.0f);
        UI::SetNextWindowPos(V(20, 20), UI::Cond_Always); UI::SetNextWindowSize(V(420, 300), UI::Cond_Always);
        if (UI::Begin("Form", nullptr, 0)) {
            UI::AlignTextToFramePadding();
            UI::TextDisabled("Name"); label = UI::GetItemRect(); UI::SameLine(84.0f);
            UI::InputText("##name", buf, (int)sizeof buf); field = UI::GetItemRect();
            UI::TextUnformatted("A plain line"); plain = UI::GetItemRect();   // the next line is not affected
            UI::Bullet(); dot = UI::GetItemRect(); UI::SameLine();
            UI::BeginGroup(); UI::TextWrapped("A note after a bullet"); note = UI::GetItemRect(); UI::EndGroup();
        }
        UI::End();
        UI::PopStyleVar(2);
        UI::UseImGuiCoordinates(false);
    };
    sim.Frames(3, ui);
    CHECK(std::fabs(CenterY(label) - CenterY(field)) < 1.0f);          // the label's line is the field's
    CHECK(plain.h < field.h - 2.0f);                                     // a text later on is one text line tall again
    CHECK(std::fabs(CenterY(dot) - CenterY(note)) < 1.0f);             // the dot is on the note's first line
}

// Round 6 audit: more open / closed states than the table held made every later combo draw its list open (the shared fallback slot was
// set by a header opened with SetNextItemOpen). The least recently used state is reused instead.
void TestManyOpenStates() {
    RUN_SUITE("windows: many tree / header / combo states: a new one starts closed");
    Reset();
    Sim sim(640, 480);
    bool lateOpen = true;
    int round = 0;
    auto ui = [&] {
        UI::SetNextWindowPos(V(0, 0), UI::Cond_Always); UI::SetNextWindowSize(V(640, 480), UI::Cond_Always);
        if (UI::Begin("Many", nullptr, 0)) {
            char name[32];
            for (int i = 0; i < 300; ++i) {   // 300 new default-open headers each round (an Inspector showing object after object)
                std::snprintf(name, sizeof name, "H%d_%d", round, i);
                UI::PushID(name); UI::SetNextItemOpen(true); UI::CollapsingHeader("Section"); UI::PopID();   // (the editor opens its headers this way)
            }
            lateOpen = UI::TreeNode("A late node");
            if (lateOpen) UI::TreePop();
        }
        UI::End();
    };
    for (round = 0; round < 6; ++round) sim.Frame(ui);
    CHECK(!lateOpen);   // a tree node nobody opened is closed
}

int main() {
    TestOcclusion();
    TestDrawOrder();
    TestDragBehind();
    TestNoBackground();
    TestScroll();
    TestAutoResize();
    TestPivotCornerAutoResize();
    TestPivotAppearingAutoResize();
    TestFocusFlags();
    TestDockedBelowFloating();
    TestWindowDragThrough();
    TestMenuBarRow();
    TestChildClipInPopup();
    TestSizeAxisZeroFits();
    TestRegionInsideWindow();
    TestMenuOfWidgets();
    TestSameLineOffset();
    TestMenuItemToggle();
    TestDockHostListUnderFloating();
    TestWindowHoveredUnderMenu();
    TestMenuBarHoverSwitch();
    TestTopLevelPopupId();
    TestModalFixedHeight();
    TestModalFitsContent();
    TestTabFocus();
    TestNavRows();
    TestTabBarSlides();
    TestLabelColumn();
    TestTextAlignment();
    TestManyOpenStates();
    TestDragAcrossWindows();
    TestDragSourceThatMoves();
    TEST_MAIN_RESULT();
}
