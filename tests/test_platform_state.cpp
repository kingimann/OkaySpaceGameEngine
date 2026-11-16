// okay::platform window state, focus and pointer queries on a real X server (run under Xvfb by
// tests/run_under_xvfb.sh), on the sdl2 backend and on our own X11 backend:
//
//   Window::IsMinimized / IsVisible / HasMouseFocus / SetInputFocus
//   Backend::KeyboardFocusWindowId / MouseFocusWindowId / IsRelativeMouse / GlobalMouseState / PumpEvents
//   WrapSdl2Window (a non-owning Window over an SDL_Window* the app made itself; sdl2 only)
//
// They are the seam's answer to what the launcher and editor ask SDL directly (SDL_GetWindowFlags with
// MINIMIZED / HIDDEN / MOUSE_FOCUS, SDL_GetKeyboardFocus, SDL_GetMouseFocus, SDL_SetWindowInputFocus,
// SDL_GetRelativeMouseMode, SDL_GetGlobalMouseState, SDL_PumpEvents). Input comes from xdotool (XTEST), so it
// travels the server's real input path. Xvfb has no window manager, so the "minimised" cases play the window
// manager themselves (tests/platform_fakewm.cpp sets the properties a WM sets).
//
//   test_platform_state sdl2|own
//
// Exit code 77 = skipped (no display, no xdotool, backend not compiled in).
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"
#include "platform_fakewm.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#if defined(__has_include)
#if defined(OKAY_PLATFORM_HAVE_SDL2) && __has_include(<SDL.h>)
#include <SDL.h>
#define OKAY_TEST_HAVE_SDL_HEADERS 1
#endif
#endif

using namespace okay::platform;

namespace {

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// Read and drop events for `ms` (what an app's frame loop does), returning what arrived.
std::vector<Event> Pump(Backend& b, int ms) {
    std::vector<Event> evs;
    const auto t0 = std::chrono::steady_clock::now();
    do {
        Event e;
        while (b.PollEvent(e)) evs.push_back(std::move(e));
        SleepMs(10);
    } while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(ms));
    return evs;
}

// Run xdotool and wait for the server to deliver, WITHOUT reading any event.
void Xdo(const std::string& args, int settleMs = 150) {
    const std::string cmd = "LC_ALL=C.UTF-8 xdotool " + args + " >/dev/null 2>&1";
    if (std::system(cmd.c_str()) != 0) std::cout << "  (xdotool " << args << " failed)\n";
    SleepMs(settleMs);
}

std::vector<Event> Inject(Backend& b, const std::string& args, int settleMs = 150) {
    const std::string cmd = "LC_ALL=C.UTF-8 xdotool " + args + " >/dev/null 2>&1";
    if (std::system(cmd.c_str()) != 0) std::cout << "  (xdotool " << args << " failed)\n";
    return Pump(b, settleMs);
}

bool HasEvent(const std::vector<Event>& v, EventType t, int x, int y) {
    for (auto& e : v) if (e.type == t && e.x == x && e.y == y) return true;
    return false;
}

// The X window id of a window, whichever backend made it (0 when unknown).
unsigned long XIdOf(Window& w, BackendKind kind) {
    if (kind == BackendKind::Own) return (unsigned long)(uintptr_t)w.NativeHandle();
    return fakewm::X11WindowOfSdl(w.NativeHandle());
}

} // namespace

int main(int argc, char** argv) {
    RUN_SUITE("platform_state");
    BackendKind kind = BackendKind::Own;
    if (argc < 2 || !ParseBackendName(argv[1], kind)) { std::cerr << "usage: test_platform_state sdl2|own\n"; return 2; }
    if (!IsCompiledIn(kind)) { std::cout << "SKIP: backend " << argv[1] << " not compiled in\n"; return 77; }
    if (!std::getenv("DISPLAY")) { std::cout << "SKIP: no $DISPLAY (run through tests/run_under_xvfb.sh)\n"; return 77; }
    if (std::system("command -v xdotool >/dev/null 2>&1") != 0) { std::cout << "SKIP: xdotool not installed\n"; return 77; }
    setenv("SDL_VIDEODRIVER", "x11", 1);   // the sdl2 backend must talk to the same server

    Backend* b = Get(kind);
    CHECK(b != nullptr);
    if (!b) return 1;
    std::cout << "[backend] " << b->Name() << "\n";
    if (!b->Init(InitFlags{true, false, false})) {
        std::cout << "SKIP: cannot open the display: " << b->LastError() << "\n";
        return 77;
    }

    // ---- a window to ask about -------------------------------------------------------------
    WindowDesc d;
    d.title = "Okay State A";
    d.x = 100; d.y = 80; d.width = 320; d.height = 240;
    auto a = b->MakeWindow(d);
    CHECK(a != nullptr);
    if (!a) { std::cout << "MakeWindow failed: " << b->LastError() << "\n"; return 1; }
    a->Raise();
    Pump(*b, 250);
    int px = 0, py = 0;
    a->GetPosition(px, py);

    // ---- IsVisible: hidden by Hide() or by being created hidden; minimised still counts as visible -------
    CHECK(a->IsVisible());
    CHECK(!a->IsMinimized());
    a->Hide();
    Pump(*b, 100);
    CHECK(!a->IsVisible());
    CHECK(!a->IsMinimized());          // hidden is not minimised
    a->Show();
    Pump(*b, 150);
    CHECK(a->IsVisible());
    {
        WindowDesc dh = d;
        dh.title = "Okay State Hidden"; dh.x = 500; dh.hidden = true;
        auto hid = b->MakeWindow(dh);
        CHECK(hid != nullptr);
        if (hid) {
            CHECK(!hid->IsVisible());  // created hidden
            hid->Show();
            Pump(*b, 150);
            CHECK(hid->IsVisible());
            hid->Hide();
            CHECK(!hid->IsVisible());
        }
    }

    // ---- HasMouseFocus / MouseFocusWindowId: the pointer is over the window ------------------------
    a->Raise();
    Pump(*b, 150);
    Inject(*b, "mousemove 900 700");   // far outside (the screen is 1024x768, the window is at 100,80)
    CHECK(!a->HasMouseFocus());
    CHECK(b->MouseFocusWindowId() == 0);
    {
        char cmd[96];
        std::snprintf(cmd, sizeof cmd, "mousemove %d %d", px + 60, py + 50);
        auto ev = Inject(*b, cmd);
        CHECK(a->HasMouseFocus());
        CHECK(b->MouseFocusWindowId() == a->Id());
        CHECK(HasEvent(ev, EventType::MouseMove, 60, 50));
    }
    Inject(*b, "mousemove 900 700");
    CHECK(!a->HasMouseFocus());
    CHECK(b->MouseFocusWindowId() == 0);

    // ---- PumpEvents: reads the OS events into the queue without taking one -----------------------------
    // Move the pointer into the window and call ONLY PumpEvents (no PollEvent): the window must now know the pointer
    // is over it, and the motion event must still be waiting for the next PollEvent.
    {
        char cmd[96];
        std::snprintf(cmd, sizeof cmd, "mousemove %d %d", px + 70, py + 40);
        Xdo(cmd);
        b->PumpEvents();
        CHECK(a->HasMouseFocus());
        std::vector<Event> evs;
        Event e;
        while (b->PollEvent(e)) evs.push_back(std::move(e));
        CHECK(HasEvent(evs, EventType::MouseMove, 70, 40));   // not lost by the pump
        Xdo("mousemove 900 700");
        b->PumpEvents();
        CHECK(!a->HasMouseFocus());
        Pump(*b, 50);
    }

    // ---- GlobalMouseState: the pointer on the whole desktop, not relative to a window --------------------
    // The own backends ask the OS, so any desktop position is exact. The sdl2 backend answers with SDL's call, which under X11
    // follows the pointer only while SDL sees it (over SDL's windows) and only after events were read: measured under Xvfb. So
    // every query here follows PumpEvents, and the sdl2 run checks positions over the window and only prints the outside one.
    {
        int gx = -1, gy = -1;
        const bool exact = kind == BackendKind::Own;
        auto at = [&](int x, int y) {
            char c[64];
            std::snprintf(c, sizeof c, "mousemove %d %d", x, y);
            Xdo(c);
            b->PumpEvents();
        };
        const int tx = px + 221, ty = py + 43;   // over the window
        at(tx, ty);
        uint32_t m = b->GlobalMouseState(&gx, &gy);
        std::cout << "  global pointer: " << gx << "," << gy << " buttons " << m << "\n";
        CHECK(gx == tx && gy == ty);             // the desktop's coordinates, not the window's (px, py are the window's origin)
        CHECK(m == 0);
        Xdo("mousedown 1", 100);
        b->PumpEvents();
        m = b->GlobalMouseState(&gx, &gy);
        CHECK((m & MouseMask(MouseLeft)) != 0);
        CHECK((m & MouseMask(MouseRight)) == 0);
        Xdo("mouseup 1", 100);
        b->PumpEvents();
        m = b->GlobalMouseState(nullptr, nullptr);   // both pointers are optional
        CHECK(m == 0);
        at(5, 9);                                // outside every window of ours
        b->GlobalMouseState(&gx, &gy);
        std::cout << "  global pointer outside the windows: " << gx << "," << gy << (exact ? "" : "  (sdl2: SDL's own answer, not checked)") << "\n";
        if (exact) CHECK(gx == 5 && gy == 9);
        Pump(*b, 50);
    }

    // ---- KeyboardFocusWindowId, IsFocused and SetInputFocus ------------------------------------------------
    a->Raise();
    CHECK(a->SetInputFocus());   // sdl2: after another window took and lost the focus, Raise alone does not give it back
    Pump(*b, 200);
    CHECK(b->KeyboardFocusWindowId() == a->Id());
    CHECK(a->IsFocused());
    {
        WindowDesc d2 = d;
        d2.title = "Okay State B"; d2.x = 500; d2.y = 80;
        auto c = b->MakeWindow(d2);
        CHECK(c != nullptr);
        if (c) {
            Pump(*b, 250);
            CHECK(c->SetInputFocus());
            Pump(*b, 250);
            CHECK(b->KeyboardFocusWindowId() == c->Id());
            CHECK(c->IsFocused());
            CHECK(!a->IsFocused());
            CHECK(a->SetInputFocus());
            Pump(*b, 250);
            CHECK(b->KeyboardFocusWindowId() == a->Id());
            CHECK(a->IsFocused() && !c->IsFocused());
            // A hidden window cannot take the focus: asking must not move it there (the own backend also says so: false).
            c->Hide();
            Pump(*b, 100);
            const bool took = c->SetInputFocus();
            if (kind == BackendKind::Own) CHECK(!took);
            Pump(*b, 150);
            CHECK(b->KeyboardFocusWindowId() != c->Id());
        }
    }
    // No window of ours has the focus: the id is 0.
    a->Hide();
    Pump(*b, 250);
    CHECK(b->KeyboardFocusWindowId() == 0);
    a->Show();
    Pump(*b, 200);

    // ---- IsRelativeMouse follows SetRelativeMouse ---------------------------------------------------------
    a->Raise();
    Pump(*b, 200);
    CHECK(!b->IsRelativeMouse());
    if (b->SetRelativeMouse(true)) {
        CHECK(b->IsRelativeMouse());
        b->SetRelativeMouse(false);
    } else {
        std::cout << "  relative mouse: not available on this server\n";
    }
    CHECK(!b->IsRelativeMouse());
    Pump(*b, 50);

    // ---- IsMinimized: the window manager says so ---------------------------------------------------------
    // Xvfb has no window manager: play it. A real WM sets _NET_WM_STATE_HIDDEN (EWMH) and WM_STATE = Iconic (ICCCM)
    // on a minimised window. The window must report it (the own backends also keep it "visible", not hidden).
    const unsigned long xid = XIdOf(*a, kind);
    if (fakewm::Available() && xid) {
        const char* hidden[] = {"_NET_WM_STATE_HIDDEN"};
        CHECK(!a->IsMinimized());
        CHECK(fakewm::SetNetWmState(xid, hidden, 1));
        Pump(*b, 250);
        CHECK(a->IsMinimized());
        if (kind == BackendKind::Own) CHECK(a->IsVisible());   // not promised for a minimised window: SDL on X11 sets its HIDDEN flag too
        CHECK(fakewm::SetNetWmState(xid, nullptr, 0));
        Pump(*b, 250);
        CHECK(!a->IsMinimized());
        if (kind == BackendKind::Own) {   // the ICCCM property is read as well (a WM that only sets WM_STATE)
            CHECK(fakewm::SetWmState(xid, 3));
            Pump(*b, 150);
            CHECK(a->IsMinimized());
            CHECK(fakewm::SetWmState(xid, 1));
            Pump(*b, 150);
            CHECK(!a->IsMinimized());
        }
    } else {
        std::cout << "  (no way to reach the X server directly: the minimised cases were skipped)\n";
    }

    // ---- WrapSdl2Window: a window the app made itself, used through the seam -----------------------------------
    CHECK(WrapSdl2Window(nullptr) == nullptr);
#ifdef OKAY_TEST_HAVE_SDL_HEADERS
    if (kind == BackendKind::Sdl2) {
        SDL_Window* raw = SDL_CreateWindow("Okay Raw SDL window", 600, 400, 260, 180, SDL_WINDOW_RESIZABLE);
        CHECK(raw != nullptr);
        if (raw) {
            const Uint32 sdlId = SDL_GetWindowID(raw);
            {
                std::unique_ptr<Window> pw = WrapSdl2Window(raw);
                CHECK(pw != nullptr);
                if (pw) {
                    CHECK(pw->Kind() == BackendKind::Sdl2);
                    CHECK(pw->NativeHandle() == raw);
                    CHECK(pw->Id() == sdlId);
                    int w = 0, h = 0, sw = 0, sh = 0;
                    pw->GetSize(w, h);
                    SDL_GetWindowSize(raw, &sw, &sh);
                    CHECK(w == 260 && h == 180 && w == sw && h == sh);
                    pw->SetTitle("Renamed through the seam");
                    CHECK(std::string(SDL_GetWindowTitle(raw)) == "Renamed through the seam");
                    pw->SetSize(300, 200);
                    Pump(*b, 200);   // the size is current once the resize event has been read
                    SDL_GetWindowSize(raw, &sw, &sh);
                    CHECK(sw == 300 && sh == 200);

                    // The wrapper must answer exactly what SDL_GetWindowFlags says, in every state the launcher asks about.
                    auto same = [&](const char* when) {
                        const Uint32 f = SDL_GetWindowFlags(raw);
                        const bool okMin = pw->IsMinimized() == ((f & SDL_WINDOW_MINIMIZED) != 0);
                        const bool okVis = pw->IsVisible() == ((f & SDL_WINDOW_HIDDEN) == 0);
                        const bool okMouse = pw->HasMouseFocus() == ((f & SDL_WINDOW_MOUSE_FOCUS) != 0);
                        const bool okFocus = pw->IsFocused() == ((f & SDL_WINDOW_INPUT_FOCUS) != 0);
                        if (!(okMin && okVis && okMouse && okFocus)) std::cout << "  differs from SDL_GetWindowFlags " << when << "\n";
                        CHECK(okMin && okVis && okMouse && okFocus);
                    };
                    pw->Show();
                    Pump(*b, 200);
                    same("after Show");
                    CHECK(pw->IsVisible());
                    pw->Hide();
                    Pump(*b, 100);
                    same("after Hide");
                    CHECK(!pw->IsVisible());
                    pw->Show();
                    Pump(*b, 150);
                    Xdo("mousemove 700 460");   // inside the raw window (600,400 + 260x180 or 300x200)
                    Pump(*b, 150);
                    same("with the pointer over it");
                    CHECK(pw->HasMouseFocus());
                    Xdo("mousemove 900 700");
                    Pump(*b, 150);
                    same("with the pointer away");
                    CHECK(!pw->HasMouseFocus());
                    pw->Raise();
                    Pump(*b, 200);
                    same("after Raise");
                    // Keyboard focus: the seam call and the raw SDL call agree.
                    SDL_Window* kf = SDL_GetKeyboardFocus();
                    CHECK((b->KeyboardFocusWindowId() != 0) == (kf != nullptr));
                    if (kf) CHECK(b->KeyboardFocusWindowId() == SDL_GetWindowID(kf));
                    if (fakewm::Available()) {
                        const unsigned long rx = fakewm::X11WindowOfSdl(raw);
                        const char* hidden[] = {"_NET_WM_STATE_HIDDEN"};
                        if (rx && fakewm::SetNetWmState(rx, hidden, 1)) {
                            Pump(*b, 250);
                            same("with the window manager's minimised property");
                            fakewm::SetNetWmState(rx, nullptr, 0);
                            Pump(*b, 250);
                            same("after the property is cleared");
                        }
                    }
                    // GetSize / SetSize round trip through the wrapper for the apps' geometry code.
                    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                    pw->GetPosition(x0, y0);
                    SDL_GetWindowPosition(raw, &x1, &y1);
                    CHECK(x0 == x1 && y0 == y1);
                }
            }   // the wrapper is gone here: it must NOT have destroyed the window the app owns
            CHECK(SDL_GetWindowFromID(sdlId) == raw);
            int w = 0, h = 0;
            SDL_GetWindowSize(raw, &w, &h);
            CHECK(w == 300 && h == 200);
            SDL_DestroyWindow(raw);
            CHECK(SDL_GetWindowFromID(sdlId) == nullptr);
        }
    }
#endif

    // ---- Flash: the X11 urgency hint (the task bar's "wants attention"), set while another window has the focus -------
    // Window::Flash is what the launcher asked SDL_FlashWindow before: UntilFocused stays until the window gets the focus or
    // Cancel, Briefly clears itself after about a second. Read back from the X server (WM_HINTS) by tests/platform_fakewm.cpp.
    {
        RUN_SUITE("platform_state: Flash (urgency hint)");
        WindowDesc df = d;
        df.title = "Okay State Focus"; df.x = 520; df.y = 80;
        auto other = b->MakeWindow(df);
        CHECK(other != nullptr);
        const unsigned long ax = XIdOf(*a, kind);
        if (other && ax && fakewm::Available()) {
            other->Raise();
            other->SetInputFocus();
            Pump(*b, 250);
            CHECK(!a->IsFocused());
            CHECK(fakewm::UrgencyHint(ax) == 0);
            CHECK(a->Flash(FlashMode::UntilFocused));
            Pump(*b, 100);
            CHECK(fakewm::UrgencyHint(ax) == 1);
            CHECK(a->Flash(FlashMode::Cancel));
            Pump(*b, 100);
            CHECK(fakewm::UrgencyHint(ax) == 0);
            CHECK(a->Flash(FlashMode::Briefly));
            Pump(*b, 100);
            CHECK(fakewm::UrgencyHint(ax) == 1);
            Pump(*b, 1400);   // "briefly": gone again after about a second (the backend clears it while reading events)
            CHECK(fakewm::UrgencyHint(ax) == 0);
            CHECK(a->Flash(FlashMode::UntilFocused));
            Pump(*b, 100);
            CHECK(fakewm::UrgencyHint(ax) == 1);
            a->SetInputFocus();   // the user comes to the window: the hint goes
            Pump(*b, 300);
            if (a->IsFocused()) CHECK(fakewm::UrgencyHint(ax) == 0);
            else std::cout << "  (the window did not get the focus; the clear-on-focus case is not checked)\n";
            std::cout << "  urgency hint set, cleared by Cancel, by time (Briefly) and by focus\n";
        } else std::cout << "  (no X window id or no Xlib for the check; skipped)\n";
        other.reset();
        Pump(*b, 100);
        // The input method's caret rectangle: accepted on sdl2 (SDL_SetTextInputRect keeps it); on the own backend only
        // when the input method draws over the spot (Xvfb has no input method server, so this is reported, not required).
        const bool imeTaken = a->SetTextInputArea(40, 50, 1, 18);
        std::cout << "  SetTextInputArea: " << (imeTaken ? "taken" : "not taken (no over-the-spot input method)") << "\n";
        if (kind == BackendKind::Sdl2) CHECK(imeTaken);
        // The window-message hook is Windows only.
        CHECK(!b->SetNativeMessageHook(nullptr, nullptr));
    }

    // ---- the first window is still usable after all of that ---------------------------------------------------
    CHECK(a->IsVisible());
    a.reset();
    Pump(*b, 100);
    CHECK(b->KeyboardFocusWindowId() == 0);
    CHECK(b->MouseFocusWindowId() == 0);

    TEST_MAIN_RESULT();
}
