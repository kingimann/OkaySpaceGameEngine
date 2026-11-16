// The pieces of the editor / launcher / player that now go through okay::platform instead of
// calling SDL: the OS clipboard (shared/PlatformClipboard.hpp), the monitor list
// (shared/PlatformDisplays.hpp), the millisecond timer, cursor visibility, text input and the
// file-drop event. Runs under Xvfb (tests/run_under_xvfb.sh) on a backend named on the command
// line:   test_platform_seam sdl2|own
// On sdl2 the monitor list is also compared with the SDL-only code it replaced. Exit 77 = skipped.
#include "test_framework.hpp"
#include "PlatformClipboard.hpp"
#include "PlatformDisplays.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#if defined(__has_include)
#if __has_include(<SDL.h>)
#include <SDL.h>
#define OKAY_SEAM_TEST_SDL 1
#endif
#endif

using namespace okay;
using namespace okay::platform;

#ifdef OKAY_SEAM_TEST_SDL
// The SDL-only ListMonitors this seam replaced (shared/SdlDisplays.cpp before the move), kept here as the reference.
static std::vector<DisplayMonitor> LegacySdlMonitors() {
    std::vector<DisplayMonitor> out;
    const int n = SDL_GetNumVideoDisplays();
    for (int i = 0; i < n; ++i) {
        DisplayMonitor m;
        m.index = i;
        const char* nm = SDL_GetDisplayName(i);
        m.name = (nm && nm[0]) ? nm : "Display " + std::to_string(i + 1);
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(i, &bounds) == 0) { m.x = bounds.x; m.y = bounds.y; m.width = bounds.w; m.height = bounds.h; }
        SDL_DisplayMode cur;
        if (SDL_GetCurrentDisplayMode(i, &cur) == 0) {
            if (m.width <= 0) m.width = cur.w;
            if (m.height <= 0) m.height = cur.h;
            if (cur.w > 0 && cur.h > 0) m.modes.push_back({cur.w, cur.h, cur.refresh_rate > 0 ? cur.refresh_rate : 0});
        }
        const int modes = SDL_GetNumDisplayModes(i);
        for (int k = 0; k < modes; ++k) {
            SDL_DisplayMode dm;
            if (SDL_GetDisplayMode(i, k, &dm) != 0 || dm.w <= 0 || dm.h <= 0) continue;
            m.modes.push_back({dm.w, dm.h, dm.refresh_rate > 0 ? dm.refresh_rate : 0});
        }
        m.modes = FilterDisplayModes(std::move(m.modes));
        out.push_back(std::move(m));
    }
    return out;
}
#endif

int main(int argc, char** argv) {
    RUN_SUITE("platform_seam");
    BackendKind kind = BackendKind::Own;
    if (argc < 2 || !ParseBackendName(argv[1], kind)) { std::cerr << "usage: test_platform_seam sdl2|own\n"; return 2; }
    if (!IsCompiledIn(kind)) { std::cout << "SKIP: backend " << argv[1] << " not compiled in\n"; return 77; }
    if (!std::getenv("DISPLAY")) { std::cout << "SKIP: no $DISPLAY (run through tests/run_under_xvfb.sh)\n"; return 77; }
    setenv("SDL_VIDEODRIVER", "x11", 1);
    Backend* b = Get(kind);
    CHECK(b != nullptr);
    if (!b) return 1;
    if (!b->Init(InitFlags{true, false, false})) { std::cout << "SKIP: cannot open the display: " << b->LastError() << "\n"; return 77; }
    WindowDesc d; d.width = 200; d.height = 120; d.x = 50; d.y = 50;
    auto win = b->MakeWindow(d);   // the own X11 clipboard needs a window to own the selection
    CHECK(win != nullptr);

    // ---- timer: monotonic, advances with real time, and the 32-bit form is its low bits ----
    {
        const uint64_t t0 = b->TicksMs();
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        const uint64_t t1 = b->TicksMs();
        CHECK(t1 >= t0 + 30 && t1 < t0 + 2000);
        CHECK((uint32_t)b->TicksMs() >= (uint32_t)t1);
#ifdef OKAY_SEAM_TEST_SDL
        if (kind == BackendKind::Sdl2) {   // the sdl2 backend is the SDL clock: what editor and player used to read directly
            const uint64_t a = b->TicksMs(), s = SDL_GetTicks64();
            CHECK(s >= a && s - a < 50);
        }
#endif
    }

    // ---- clipboard through the shared wrapper (what the editor's Play mode and the player install) ----
    {
        PlatformClipboard clip(b);
        const std::string text = "seam clip \xC3\xA9 \xE2\x82\xAC 1";
        CHECK(clip.SetText(text));
        // The own X11 backend serves the selection from its event pump; reading back our own selection does not need it.
        CHECK(clip.GetText() == text);
        CHECK(clip.SetText("second"));
        CHECK(clip.GetText() == "second");
        CHECK(b->GetClipboardText() == "second");   // the wrapper and the backend see one clipboard
    }

    // ---- cursor and text input ----
    {
        CHECK(b->IsCursorShown());
        b->ShowCursor(false);
        CHECK(!b->IsCursorShown());
        b->ShowCursor(true);
        CHECK(b->IsCursorShown());
        b->StartTextInput();
        b->StopTextInput();
        b->StartTextInput();   // twice in a row is harmless (the editor calls it every frame)
        b->StartTextInput();
        b->StopTextInput();
    }

    // ---- monitor list ----
    {
        const std::vector<DisplayMonitor> mons = platdisplay::ListMonitors(*b);
        std::cout << "  monitors via " << b->Name() << ": " << mons.size() << "\n";
        for (const DisplayMonitor& m : mons) {
            std::cout << "    #" << m.index << " '" << m.name << "' " << m.x << "," << m.y << " " << m.width << "x" << m.height
                      << " modes=" << m.modes.size() << "\n";
            CHECK(m.Valid());
            CHECK(!m.name.empty());
        }
        if (b->DisplayCount() == 0) {
            CHECK(mons.empty());   // a backend that cannot say gives an empty list, never a made-up one
            std::cout << "  (this backend does not enumerate monitors yet)\n";
        } else {
            CHECK((int)mons.size() == b->DisplayCount());
            CHECK(!mons.empty() && !mons[0].modes.empty());
        }
#ifdef OKAY_SEAM_TEST_SDL
        if (kind == BackendKind::Sdl2) {
            const std::vector<DisplayMonitor> ref = LegacySdlMonitors();
            CHECK(!ref.empty());
            CHECK(ref.size() == mons.size());
            for (size_t i = 0; i < ref.size() && i < mons.size(); ++i) {
                CHECK(ref[i].index == mons[i].index);
                CHECK(ref[i].name == mons[i].name);
                CHECK(ref[i].x == mons[i].x && ref[i].y == mons[i].y);
                CHECK(ref[i].width == mons[i].width && ref[i].height == mons[i].height);
                CHECK(ref[i].modes == mons[i].modes);
            }
            // The extra fields the seam now carries.
            DisplayInfo info;
            CHECK(b->GetDisplayInfo(0, info));
            CHECK(info.width == ref[0].width && info.height == ref[0].height && info.primary);
            CHECK(info.workWidth > 0 && info.workHeight > 0 && info.dpi >= 24);
            CHECK(!b->GetDisplayInfo(99, info));
            CHECK(b->DisplayModes(99).empty());
        }
#endif
    }

    // ---- file drop: the event the editor and launcher read from SDL_DROPFILE ----
#ifdef OKAY_SEAM_TEST_SDL
    if (kind == BackendKind::Sdl2) {
        SDL_Event e;
        SDL_zero(e);
        e.type = SDL_DROPFILE;
        e.drop.file = SDL_strdup("/tmp/okay seam drop \xC3\xA9.png");
        CHECK(SDL_PushEvent(&e) == 1);
        bool got = false;
        for (int i = 0; i < 50 && !got; ++i) {
            Event ev;
            while (b->PollEvent(ev))
                if (ev.type == EventType::DropFile) { got = ev.text == "/tmp/okay seam drop \xC3\xA9.png"; break; }
            if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(got);
    } else
#endif
    {
        std::cout << "  file drop: not testable on this backend under Xvfb (XDND needs a second client; the own X11 backend has no drop target yet)\n";
    }

    win.reset();
    b->Shutdown();
    TEST_MAIN_RESULT();
}
