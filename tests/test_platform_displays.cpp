// Displays through okay::platform (shared/PlatformDisplays.cpp, Backend::DisplayAt / DisplayModes, the own X11 RandR list):
//   test_platform_displays            pure checks: refresh from a mode line, current-first lists, Apply's calls on a fake window
//   test_platform_displays xvfb       on an X server (tests/run_under_xvfb.sh, screen 1024x768): the own backend lists the
//                                     screen and its modes, and agrees with the sdl2 backend when that is compiled in
// The refresh figures are worked out by hand from the published mode timings (CEA-861 1080p60 and 1080i60, VESA DMT 1024x768@60).
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"
#include "Platform/DisplayModeMath.hpp"
#include "PlatformDisplays.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace okay::platform;

namespace {

// A window that only records what Apply asked of it.
class FakeWindow final : public Window {
public:
    bool canChangeMode = false;
    std::vector<std::string> calls;
    uint32_t Id() const override { return 1; }
    void* NativeHandle() const override { return nullptr; }
    void GetSize(int& w, int& h) const override { w = 640; h = 480; }
    void GetPosition(int& x, int& y) const override { x = y = 0; }
    void SetTitle(const std::string&) override {}
    void SetSize(int w, int h) override { calls.push_back("size " + std::to_string(w) + "x" + std::to_string(h)); }
    void SetPosition(int x, int y) override { calls.push_back("pos " + std::to_string(x) + "," + std::to_string(y)); }
    void SetMinimumSize(int, int) override {}
    void Show() override {}
    void Hide() override {}
    void Raise() override {}
    void Minimize() override {}
    void Restore() override {}
    void SetFullscreen(bool on) override { calls.push_back(on ? "fullscreen on" : "fullscreen off"); }
    bool SetBordered(bool on) override { calls.push_back(on ? "bordered" : "borderless"); return true; }
    bool SetFullscreenMode(const DisplayMode& m, int display) override {
        calls.push_back("mode " + std::to_string(m.w) + "x" + std::to_string(m.h) + "@" + std::to_string(m.refreshHz) + " on " + std::to_string(display));
        return canChangeMode;
    }
    bool IsFocused() const override { return false; }
    bool CreateGLContext(const GLAttributes&) override { return false; }
    void DestroyGLContext() override {}
    bool MakeCurrent() override { return false; }
    void SwapBuffers() override {}
    bool SetSwapInterval(int) override { return false; }
    void GetDrawableSize(int& w, int& h) const override { GetSize(w, h); }
};

std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (const std::string& c : v) s += (s.empty() ? "" : "; ") + c;
    return s;
}

void PureChecks() {
    using modemath::RefreshFromModeLine;
    CHECK(RefreshFromModeLine(148500000u, 2200, 1125, false, false) == 60);   // 1920x1080p60: 148.5 MHz / (2200 * 1125)
    CHECK(RefreshFromModeLine(65000000u, 1344, 806, false, false) == 60);     // 1024x768@60: 60.004 Hz
    CHECK(RefreshFromModeLine(74250000u, 2200, 1125, false, true) == 60);     // 1920x1080i: 30 frames, 60 fields
    CHECK(RefreshFromModeLine(25175000u, 800, 449, true, false) == 35);       // 640x400 double scan: each line twice
    CHECK(RefreshFromModeLine(0u, 2200, 1125, false, false) == 0);
    CHECK(RefreshFromModeLine(148500000u, 0, 1125, false, false) == 0);

    const std::vector<DisplayMode> listed = {{800, 600, 60}, {1024, 768, 60}, {1024, 768, 60}, {0, 0, 0}, {640, 480, 75}};
    const std::vector<DisplayMode> cf = modemath::CurrentFirst({1024, 768, 60}, listed);
    CHECK(cf.size() == 3);
    CHECK(cf.size() == 3 && cf[0].w == 1024 && cf[0].h == 768 && cf[1].w == 800 && cf[2].refreshHz == 75);
    CHECK(modemath::CurrentFirst({0, 0, 0}, {}).empty());

    // Apply: the same order of steps the SDL version took.
    {
        FakeWindow w;
        okay::DisplayApply a;
        a.mode = okay::WindowMode::Windowed; a.hasSize = true; a.width = 800; a.height = 600;
        a.hasMonitor = true; a.originX = 1920; a.originY = 0; a.desktopWidth = 1280; a.desktopHeight = 1024;
        CHECK(okay::platformdisplay::Apply(&w, a));
        std::cout << "  windowed: " << Join(w.calls) << "\n";
        CHECK(Join(w.calls) == "fullscreen off; pos 2160,212; bordered; size 800x600");
    }
    {
        FakeWindow w;
        okay::DisplayApply a;
        a.mode = okay::WindowMode::Borderless;
        CHECK(okay::platformdisplay::Apply(&w, a));
        CHECK(Join(w.calls) == "fullscreen off; fullscreen on");
    }
    {   // exclusive fullscreen on a backend that changes the mode
        FakeWindow w; w.canChangeMode = true;
        okay::DisplayApply a;
        a.mode = okay::WindowMode::Fullscreen; a.hasSize = true; a.width = 1280; a.height = 720; a.refreshHz = 144;
        a.hasMonitor = true; a.monitorIndex = 1;
        CHECK(okay::platformdisplay::Apply(&w, a));
        std::cout << "  exclusive: " << Join(w.calls) << "\n";
        CHECK(Join(w.calls) == "fullscreen off; mode 1280x720@144 on 1");
    }
    {   // ...and on one that cannot (the own backends today): desktop fullscreen instead
        FakeWindow w;
        okay::DisplayApply a;
        a.mode = okay::WindowMode::Fullscreen; a.hasSize = true; a.width = 1280; a.height = 720;
        CHECK(okay::platformdisplay::Apply(&w, a));
        CHECK(Join(w.calls) == "fullscreen off; mode 1280x720@0 on 0; fullscreen on");
    }
    CHECK(!okay::platformdisplay::Apply(nullptr, okay::DisplayApply()));
}

void XvfbChecks() {
    if (!std::getenv("DISPLAY")) { std::cout << "SKIP: no $DISPLAY (run through tests/run_under_xvfb.sh)\n"; std::exit(77); }
    setenv("SDL_VIDEODRIVER", "x11", 1);
    Backend* own = Get(BackendKind::Own);
    if (!own || !own->Init(InitFlags{true, false, false})) { std::cout << "SKIP: own backend cannot open the display\n"; std::exit(77); }
    const int n = own->DisplayCount();
    std::cout << "  own: " << n << " display(s)\n";
    CHECK(n >= 1);
    DisplayInfo d;
    CHECK(own->DisplayAt(0, d));
    std::cout << "  own display 0: '" << d.name << "' " << d.x << "," << d.y << " " << d.w << "x" << d.h << " @" << d.refreshHz
              << (d.primary ? " primary" : "") << "\n";
    CHECK(d.w == 1024 && d.h == 768);   // run_under_xvfb.sh: -screen 0 1024x768x24
    CHECK(d.primary);
    CHECK(!own->DisplayAt(n, d));
    const std::vector<DisplayMode> modes = own->DisplayModes(0);
    std::cout << "  own modes of display 0: " << modes.size() << " (first " << (modes.empty() ? 0 : modes[0].w) << "x"
              << (modes.empty() ? 0 : modes[0].h) << ")\n";
    CHECK(!modes.empty() && modes[0].w == 1024 && modes[0].h == 768);
    CHECK(own->DisplayModes(n).empty());
    const std::vector<okay::DisplayMonitor> ownMons = okay::platformdisplay::ListMonitors(own);
    CHECK(ownMons.size() == (size_t)n);
    CHECK(!ownMons.empty() && !ownMons[0].modes.empty());

    // The same server through the sdl2 backend: the same displays, sizes and current mode.
    Backend* sdl = IsCompiledIn(BackendKind::Sdl2) ? Get(BackendKind::Sdl2) : nullptr;
    if (sdl && sdl->Init(InitFlags{true, false, false})) {
        CHECK(sdl->DisplayCount() == n);
        for (int i = 0; i < n && i < sdl->DisplayCount(); ++i) {
            DisplayInfo a, b;
            CHECK(own->DisplayAt(i, a) && sdl->DisplayAt(i, b));
            std::cout << "  display " << i << ": own " << a.w << "x" << a.h << "@" << a.refreshHz << ", sdl2 " << b.w << "x" << b.h << "@"
                      << b.refreshHz << "\n";
            CHECK(a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h);
            CHECK(a.refreshHz == b.refreshHz || a.refreshHz == 0 || b.refreshHz == 0);
            const std::vector<DisplayMode> ma = own->DisplayModes(i), mb = sdl->DisplayModes(i);
            CHECK(!ma.empty() && !mb.empty() && ma[0].w == mb[0].w && ma[0].h == mb[0].h);
        }
        const std::vector<okay::DisplayMonitor> sdlMons = okay::platformdisplay::ListMonitors(sdl);
        CHECK(sdlMons.size() == ownMons.size());
        for (size_t i = 0; i < sdlMons.size() && i < ownMons.size(); ++i)
            CHECK(sdlMons[i].width == ownMons[i].width && sdlMons[i].height == ownMons[i].height);
        sdl->Shutdown();
    } else {
        std::cout << "  (sdl2 backend not compiled in or no display for it: own-only checks)\n";
    }

    // A real own window: windowed with a frame, borderless, back (the calls must not fail or crash the connection).
    WindowDesc wd; wd.title = "displays"; wd.width = 200; wd.height = 150;
    std::unique_ptr<Window> w = own->MakeWindow(wd);
    CHECK(w != nullptr);
    if (w) {
        CHECK(w->SetBordered(false));
        CHECK(w->SetBordered(true));
        CHECK(!w->SetFullscreenMode(DisplayMode{800, 600, 60}, 0));   // the own X11 backend does not change video modes
        okay::DisplayApply a; a.mode = okay::WindowMode::Windowed; a.hasSize = true; a.width = 300; a.height = 200;
        CHECK(okay::platformdisplay::Apply(w.get(), a));
    }
    w.reset();
    own->Shutdown();
}

} // namespace

int main(int argc, char** argv) {
    RUN_SUITE("platform_displays");
    PureChecks();
    if (argc > 1 && std::string(argv[1]) == "xvfb") XvfbChecks();
    TEST_MAIN_RESULT();
}
