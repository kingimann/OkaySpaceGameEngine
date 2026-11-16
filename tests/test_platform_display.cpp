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
//
// Exit code 77 = skipped (no display, backend not compiled in).
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"
#include "platform_fakedisplay.hpp"
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

} // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "unit";
    if (mode == "unit") return Unit();
    if (mode == "own") return Own();
    if (mode == "sdl2") return Sdl2();
    if (mode == "own-xinerama") return OwnXinerama(argc > 2 ? std::atoi(argv[2]) : 2);
    std::fprintf(stderr, "usage: test_platform_display unit|own|sdl2|own-xinerama [screens]\n");
    return 2;
}
