// okay::platform's display list (Backend::Displays, okay::platform::Displays) and the launcher's keep-on-screen rule
// (WindowReachable, CenterOnDisplay).
//
//   test_platform_display unit             the pure parts: the rule, Xft.dpi parsing, the work-area clip (no display)
//   test_platform_display own              our X11 backend under Xvfb (tests/run_under_xvfb.sh): one screen; then a work
//                                          area, an Xft.dpi resource and two RandR 1.5 monitors set by the test itself
//                                          (tests/platform_fakedisplay.cpp plays the desktop)
//   test_platform_display sdl2             the sdl2 backend on the same one-screen Xvfb, and the own backend next to it:
//                                          the two must report the same screen
//   test_platform_display own-xinerama N   our X11 backend on an Xvfb with N screens joined by Xinerama (+xinerama)
//   test_platform_display modes-own        display modes on our X11 backend: RandR modes the test adds itself
//   test_platform_display modes-sdl2       (tests/platform_fakedisplay.cpp) are listed, a window switches the display to
//                                          them (exclusive fullscreen) and back
//
// Exit code 77 = skipped (no display, backend not compiled in).
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"
#include "platform_fakedisplay.hpp"
#include "Platform/Internal.hpp"   // SortedVideoModes, DisplayIndexForRect
#if defined(__linux__)
#include "Platform/OwnLinux.hpp"
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace okay::platform;

namespace {

void Print(const char* what, const std::vector<DisplayInfo>& ds) {
    std::printf("  %s: %zu display(s)\n", what, ds.size());
    for (const DisplayInfo& d : ds)
        std::printf("    '%s' bounds %d,%d %dx%d usable %d,%d %dx%d scale %.2f%s\n", d.name.c_str(), d.bounds.x, d.bounds.y,
                    d.bounds.w, d.bounds.h, d.usable.x, d.usable.y, d.usable.w, d.usable.h, d.scale, d.primary ? " primary" : "");
}
bool Same(const DisplayRect& a, int x, int y, int w, int h) { return a.x == x && a.y == y && a.w == w && a.h == h; }
DisplayInfo Disp(int x, int y, int w, int h) { DisplayInfo d; d.bounds = DisplayRect{x, y, w, h}; d.usable = d.bounds; return d; }

int Unit() {
    RUN_SUITE("WindowReachable / CenterOnDisplay");
    std::vector<DisplayInfo> one = {Disp(0, 0, 1920, 1080)};
    one[0].usable = DisplayRect{0, 0, 1920, 1040};   // a 40 px task bar at the bottom
    CHECK(WindowReachable(one, 100, 100, 800, 600));
    CHECK(!WindowReachable(one, 2500, 100, 800, 600));       // all of it off to the right (a monitor was unplugged)
    CHECK(!WindowReachable(one, 1800, 100, 800, 600));       // only 120 px wide on screen: < 160
    CHECK(WindowReachable(one, 1760, 100, 800, 600));        // exactly 160 px on screen
    CHECK(!WindowReachable(one, 100, -30, 800, 600));        // title bar above the top (more than 8 px)
    CHECK(WindowReachable(one, 100, -8, 800, 600));
    CHECK(!WindowReachable(one, 100, 1000, 800, 600));       // top edge within 40 px of the usable bottom
    CHECK(!WindowReachable({}, 0, 0, 800, 600));             // no displays known: not reachable (the caller decides)
    std::vector<DisplayInfo> two = {Disp(0, 0, 1920, 1080), Disp(-1280, 0, 1280, 1024)};
    CHECK(WindowReachable(two, -1000, 100, 800, 600));       // on the second monitor, left of the primary
    DisplayRect r = CenterOnDisplay(one[0], 800, 600);
    CHECK(Same(r, 560, 220, 800, 600));
    r = CenterOnDisplay(one[0], 4000, 3000);                  // shrunk to fit with a 20 px margin
    CHECK(Same(r, 20, 20, 1880, 1000));
    DisplayInfo noUsable = Disp(100, 50, 1000, 800); noUsable.usable = DisplayRect{};   // falls back to the bounds
    r = CenterOnDisplay(noUsable, 400, 200);
    CHECK(Same(r, 400, 350, 400, 200));
    RUN_SUITE("display modes: SortedVideoModes, ClosestVideoMode, DisplayIndexForRect");
    {
        const std::vector<VideoMode> m = SortedVideoModes({{800, 600, 60}, {1920, 1080, 60}, {800, 600, 75}, {0, 0, 60},
                                                           {1920, 1080, 60}, {640, 480, -5}, {1920, 1080, 144}, {1280, 1024, 0}});
        CHECK(m.size() == 6);   // the empty size and the repeat are gone
        if (m.size() == 6) {
            CHECK(m[0] == (VideoMode{1920, 1080, 144}) && m[1] == (VideoMode{1920, 1080, 60}));   // largest first, then the rate
            CHECK(m[2] == (VideoMode{1280, 1024, 0}) && m[3] == (VideoMode{800, 600, 75}) && m[4] == (VideoMode{800, 600, 60}));
            CHECK(m[5] == (VideoMode{640, 480, 0}));   // a negative rate is "not said"
        }
        CHECK(ClosestVideoMode(m, {800, 600, 60}) == (VideoMode{800, 600, 60}));      // exact
        CHECK(ClosestVideoMode(m, {800, 600, 70}) == (VideoMode{800, 600, 75}));      // nearest rate
        CHECK(ClosestVideoMode(m, {800, 600, 67}) == (VideoMode{800, 600, 60}));      // nearest rate (7 against 8)
        CHECK(ClosestVideoMode({{800, 600, 60}, {800, 600, 70}}, {800, 600, 65}) == (VideoMode{800, 600, 60}));   // a tie: the lower
        CHECK(ClosestVideoMode(m, {800, 600, 0}) == (VideoMode{800, 600, 75}));       // any rate: the highest
        CHECK(ClosestVideoMode(m, {1024, 768, 60}) == (VideoMode{1280, 1024, 0}));    // not listed: the smallest that holds it
        CHECK(ClosestVideoMode(m, {1600, 900, 0}) == (VideoMode{1920, 1080, 144}));
        CHECK(ClosestVideoMode(m, {1600, 900, 60}) == (VideoMode{1920, 1080, 60}));
        CHECK(ClosestVideoMode(m, {3840, 2160, 60}) == (VideoMode{1920, 1080, 60}));  // larger than any: the largest
        CHECK(ClosestVideoMode({}, {800, 600, 60}) == VideoMode());
        const std::vector<DisplayInfo> two = {Disp(0, 0, 1920, 1080), Disp(1920, 0, 1280, 1024)};
        CHECK(DisplayIndexForRect(two, 100, 100, 800, 600) == 0);
        CHECK(DisplayIndexForRect(two, 1800, 100, 800, 600) == 1);    // its centre (2200) is on the second
        CHECK(DisplayIndexForRect(two, 1500, 100, 800, 600) == 0);    // centre at 1900: the first
        CHECK(DisplayIndexForRect(two, 1900, 1000, 100, 100) == 1);   // centre on neither: the larger overlap (80 x 24 on the second)
        CHECK(DisplayIndexForRect(two, 5000, 5000, 10, 10) == -1);
    }
#if defined(__linux__)
    RUN_SUITE("Xft.dpi and the work area (own X11 parts)");
    CHECK_NEAR(own::ScaleFromXResources(""), 1.0, 1e-6);
    CHECK_NEAR(own::ScaleFromXResources("Xft.dpi:\t144\n"), 1.5, 1e-6);
    CHECK_NEAR(own::ScaleFromXResources("Xcursor.size:\t24\nXft.antialias:\t1\nXft.dpi:\t192\nXft.hinting:\t1\n"), 2.0, 1e-6);
    CHECK_NEAR(own::ScaleFromXResources("*Xft.dpi: 120"), 1.25, 1e-6);
    CHECK_NEAR(own::ScaleFromXResources("Xft.dpi:\tlots\n"), 1.0, 1e-6);   // not a number
    CHECK_NEAR(own::ScaleFromXResources("Xft.dpi:\t5000\n"), 1.0, 1e-6);   // not sane
    CHECK_NEAR(own::ScaleFromXResources("Xft.dpix:\t144\n"), 1.0, 1e-6);   // another resource
    CHECK(Same(own::ClipToWorkArea(DisplayRect{0, 0, 1024, 768}, DisplayRect{}), 0, 0, 1024, 768));
    CHECK(Same(own::ClipToWorkArea(DisplayRect{0, 0, 1024, 768}, DisplayRect{0, 24, 1024, 720}), 0, 24, 1024, 720));
    CHECK(Same(own::ClipToWorkArea(DisplayRect{1024, 0, 800, 600}, DisplayRect{0, 24, 1824, 576}), 1024, 24, 800, 576));
    CHECK(Same(own::ClipToWorkArea(DisplayRect{2000, 0, 800, 600}, DisplayRect{0, 0, 1024, 768}), 2000, 0, 800, 600));   // no overlap
#endif
    TEST_MAIN_RESULT();
}

Backend* Start(BackendKind k) {
    Backend* b = Get(k);
    if (!b) { std::printf("SKIP: backend %s not compiled in\n", BackendName(k)); std::exit(77); }
    InitFlags f; f.audio = false; f.gamepad = false;
    if (!b->Init(f)) { std::printf("SKIP: %s: no display (%s)\n", BackendName(k), b->LastError().c_str()); std::exit(77); }
    return b;
}

int Own() {
    if (!fakedisplay::Available()) { std::printf("SKIP: no X display for the helper\n"); return 77; }
    fakedisplay::SetWorkArea(0, 0, 0, 0);
    fakedisplay::SetResources(nullptr);
    Backend* b = Start(BackendKind::Own);

    RUN_SUITE("own: one Xvfb screen, no desktop");
    std::vector<DisplayInfo> ds = b->Displays();
    Print("plain", ds);
    CHECK(ds.size() == 1);
    if (!ds.empty()) {
        CHECK(Same(ds[0].bounds, 0, 0, 1024, 768));
        CHECK(Same(ds[0].usable, 0, 0, 1024, 768));
        CHECK(ds[0].primary);
        CHECK_NEAR(ds[0].scale, 1.0, 1e-6);
        CHECK(!ds[0].name.empty());
    }

    RUN_SUITE("own: a panel (_NET_WORKAREA) and Xft.dpi 144, read fresh");
    CHECK(fakedisplay::SetWorkArea(0, 24, 1024, 720));
    CHECK(fakedisplay::SetResources("Xft.antialias:\t1\nXft.dpi:\t144\n"));
    ds = b->Displays();
    Print("desktop", ds);
    CHECK(ds.size() == 1);
    if (!ds.empty()) {
        CHECK(Same(ds[0].bounds, 0, 0, 1024, 768));
        CHECK(Same(ds[0].usable, 0, 24, 1024, 720));
        CHECK_NEAR(ds[0].scale, 1.5, 1e-6);
    }

    RUN_SUITE("own: two RandR 1.5 monitors, the right one primary");
    if (fakedisplay::SplitIntoTwoMonitors(600, true)) {
        ds = b->Displays();
        Print("two monitors", ds);
        CHECK(ds.size() == 2);
        if (ds.size() == 2) {
            CHECK(ds[0].name == "OKAY-RIGHT");
            CHECK(ds[0].primary);
            CHECK(Same(ds[0].bounds, 600, 0, 424, 768));
            CHECK(Same(ds[0].usable, 600, 24, 424, 720));
            CHECK(ds[1].name == "OKAY-LEFT");
            CHECK(!ds[1].primary);
            CHECK(Same(ds[1].bounds, 0, 0, 600, 768));
            CHECK(Same(ds[1].usable, 0, 24, 600, 720));
            // The launcher's rule on this layout: a window on the left monitor stays; one off to the right moves to the
            // middle of the primary (right) monitor, shrunk to fit.
            CHECK(WindowReachable(ds, 50, 100, 400, 300));
            CHECK(!WindowReachable(ds, 1200, 100, 400, 300));
            const DisplayRect r = CenterOnDisplay(ds[0], 800, 600);
            CHECK(Same(r, 620, 84, 384, 600));
        }
        // The free function goes through the default backend (OKAY_PLATFORM_BACKEND=own in this run).
        if (DefaultKind() == BackendKind::Own) CHECK(Displays().size() == ds.size());
        fakedisplay::RemoveMonitors();
        ds = b->Displays();
        CHECK(ds.size() == 1);
    } else {
        std::printf("  (no RandR 1.5 on this server: the two-monitor case is not run)\n");
    }
    fakedisplay::SetWorkArea(0, 0, 0, 0);
    fakedisplay::SetResources(nullptr);
    ds = b->Displays();
    CHECK(ds.size() == 1 && Same(ds[0].usable, 0, 0, 1024, 768) && ds[0].scale == 1.0f);
    TEST_MAIN_RESULT();
}

int Sdl2() {
    Backend* s = Start(BackendKind::Sdl2);
    RUN_SUITE("sdl2: one Xvfb screen, and the own backend agrees");
    std::vector<DisplayInfo> ds = s->Displays();
    Print("sdl2", ds);
    CHECK(ds.size() == 1);
    if (!ds.empty()) {
        CHECK(Same(ds[0].bounds, 0, 0, 1024, 768));
        CHECK(ds[0].primary);
        CHECK(ds[0].usable.w > 0 && ds[0].usable.h > 0);
    }
    if (Backend* o = Get(BackendKind::Own)) {
        InitFlags f; f.audio = false; f.gamepad = false;
        if (o->Init(f)) {
            const std::vector<DisplayInfo> od = o->Displays();
            Print("own", od);
            CHECK(od.size() == ds.size());
            for (size_t i = 0; i < od.size() && i < ds.size(); ++i) {
                CHECK(Same(od[i].bounds, ds[i].bounds.x, ds[i].bounds.y, ds[i].bounds.w, ds[i].bounds.h));
                CHECK(Same(od[i].usable, ds[i].usable.x, ds[i].usable.y, ds[i].usable.w, ds[i].usable.h));
                CHECK(od[i].primary == ds[i].primary);
            }
        }
    }
    TEST_MAIN_RESULT();
}

int OwnXinerama(int screens) {
    Backend* b = Start(BackendKind::Own);
    RUN_SUITE("own: Xvfb screens joined by Xinerama");
    const std::vector<DisplayInfo> ds = b->Displays();
    Print("xinerama", ds);
    CHECK((int)ds.size() == screens);
    if ((int)ds.size() == screens && screens == 2) {
        CHECK(ds[0].primary && !ds[1].primary);
        CHECK(ds[0].bounds.w == 800 && ds[0].bounds.h == 600);
        CHECK(ds[1].bounds.w == 640 && ds[1].bounds.h == 480);
    }
    TEST_MAIN_RESULT();
}

void PrintModes(const char* what, const std::vector<VideoMode>& ms) {
    std::printf("  %s:", what);
    for (const VideoMode& m : ms) std::printf(" %dx%d@%d", m.width, m.height, m.refreshHz);
    std::printf("\n");
}
bool Has(const std::vector<VideoMode>& ms, const VideoMode& m) { for (const VideoMode& x : ms) if (x == m) return true; return false; }

// Display modes on one backend. The modes are added before the backend starts (SDL reads the list once, at its start).
int Modes(BackendKind kind) {
    if (!fakedisplay::Available()) { std::printf("SKIP: no X display for the helper\n"); return 77; }
    if (!fakedisplay::AddOutputMode(800, 600, 60) || !fakedisplay::AddOutputMode(800, 600, 75) ||
        !fakedisplay::AddOutputMode(640, 480, 60) || !fakedisplay::AddOutputMode(1024, 768, 50)) {
        std::printf("SKIP: the X server took no RandR modes\n");
        return 77;
    }
    Backend* b = Start(kind);
    const bool own = kind == BackendKind::Own;
    RUN_SUITE(std::string(own ? "own" : "sdl2") + ": the RandR modes, exclusive fullscreen and back");
    const std::vector<DisplayInfo> ds = b->Displays();
    CHECK(ds.size() == 1);
    const std::vector<VideoMode> ms = b->DisplayModes(0);
    PrintModes("modes of display 0", ms);
    CHECK(Has(ms, {800, 600, 60}) && Has(ms, {800, 600, 75}) && Has(ms, {640, 480, 60}) && Has(ms, {1024, 768, 50}));
    bool hasDesktop = false;
    for (const VideoMode& m : ms) hasDesktop |= m.width == 1024 && m.height == 768;
    CHECK(hasDesktop);   // Xvfb's own mode (it names no rate: 0 on own; SDL may make one up)
    CHECK(SortedVideoModes(ms) == ms);   // largest first, no repeats
    CHECK(b->DisplayModes(1).empty() && b->DisplayModes(-1).empty());
    VideoMode cur;
    CHECK(b->CurrentDisplayMode(0, cur) && cur.width == 1024 && cur.height == 768);
    CHECK(!b->CurrentDisplayMode(3, cur));

    WindowDesc wd; wd.title = "modes"; wd.width = 320; wd.height = 240; wd.x = 100; wd.y = 100;
    std::unique_ptr<Window> w = b->MakeWindow(wd);
    CHECK(w != nullptr);
    if (!w) TEST_MAIN_RESULT();
    for (int i = 0; i < 20; ++i) { Event e; while (b->PollEvent(e)) {} b->DelayMs(10); }
    CHECK(w->DisplayIndex() == 0);
    CHECK(w->FullscreenMode() == VideoMode());
    int cw = 0, ch = 0, sw = 0, sh = 0;
    auto settle = [&] { for (int i = 0; i < 20; ++i) { Event e; while (b->PollEvent(e)) {} b->DelayMs(10); } };

    // 800 x 600 at 75 Hz: the CRTC shows that mode; the window covers it.
    CHECK(w->SetFullscreenMode({800, 600, 75}));
    settle();
    CHECK(w->FullscreenMode() == (VideoMode{800, 600, 75}));
    CHECK(fakedisplay::CrtcAndScreen(cw, ch, sw, sh) && cw == 800 && ch == 600);
    std::printf("  after 800x600@75: CRTC %dx%d, screen %dx%d\n", cw, ch, sw, sh);
    CHECK(b->CurrentDisplayMode(0, cur) && cur == (VideoMode{800, 600, 75}));
    CHECK(w->IsFullscreen());
    if (own) {   // no window manager on Xvfb: our backend places the window itself (SDL leaves that to a window manager)
        int x = -1, y = -1, ww = 0, wh = 0;
        w->GetPosition(x, y); w->GetSize(ww, wh);
        std::printf("  window at %d,%d %dx%d\n", x, y, ww, wh);
        CHECK(x == 0 && y == 0 && ww == 800 && wh == 600);
    }
    // Not listed: 700 x 500 at any rate is the smallest mode that holds it, at its highest rate.
    CHECK(w->SetFullscreenMode({700, 500, 0}));
    settle();
    CHECK(w->FullscreenMode() == (VideoMode{800, 600, 75}));
    // The desktop's own size at another rate (Xvfb's screen cannot grow past the size it started with, so a mode larger
    // than the screen, where the backend first makes the screen larger, cannot be tried here).
    CHECK(w->SetFullscreenMode({1024, 768, 50}));
    settle();
    CHECK(w->FullscreenMode() == (VideoMode{1024, 768, 50}));
    CHECK(fakedisplay::CrtcAndScreen(cw, ch, sw, sh) && cw == 1024 && ch == 768);
    CHECK(b->CurrentDisplayMode(0, cur) && cur == (VideoMode{1024, 768, 50}));
    // Leaving: the display's own mode and the screen size come back.
    w->SetFullscreen(false);
    settle();
    CHECK(fakedisplay::CrtcAndScreen(cw, ch, sw, sh) && cw == 1024 && ch == 768);
    std::printf("  after SetFullscreen(false): CRTC %dx%d, screen %dx%d\n", cw, ch, sw, sh);
    CHECK(sw == 1024 && sh == 768);
    CHECK(w->FullscreenMode() == VideoMode());
    CHECK(!w->IsFullscreen());
    if (own) {
        int x = -1, y = -1, ww = 0, wh = 0;
        w->GetPosition(x, y); w->GetSize(ww, wh);
        CHECK(ww == 320 && wh == 240);   // its old size back
    }
    // A mode switch that is still on when the window goes away is undone too.
    CHECK(w->SetFullscreenMode({640, 480, 60}));
    settle();
    CHECK(fakedisplay::CrtcAndScreen(cw, ch, sw, sh) && cw == 640 && ch == 480);
    w.reset();
    settle();
    CHECK(fakedisplay::CrtcAndScreen(cw, ch, sw, sh) && cw == 1024 && ch == 768);
    std::printf("  after the window was destroyed: CRTC %dx%d, screen %dx%d\n", cw, ch, sw, sh);
    TEST_MAIN_RESULT();
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "unit";
    if (mode == "unit") return Unit();
    if (mode == "own") return Own();
    if (mode == "sdl2") return Sdl2();
    if (mode == "modes-own") return Modes(BackendKind::Own);
    if (mode == "modes-sdl2") return Modes(BackendKind::Sdl2);
    if (mode == "own-xinerama") return OwnXinerama(argc > 2 ? std::atoi(argv[2]) : 2);
    std::fprintf(stderr, "usage: test_platform_display unit|own|sdl2|own-xinerama [screens]|modes-own|modes-sdl2\n");
    return 2;
}
