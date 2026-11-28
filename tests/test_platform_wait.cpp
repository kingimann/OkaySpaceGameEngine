// The idle wait of the own backend (Backend::WaitEvents / WakeWait, okay::platform::WakeMainLoop): the editor's idle throttle sleeps
// in it, so it must end at once on a mouse move, a key, a window event and a finished background job, and only then.
// The X parts start a second X client (this program, on its own Display connection) that moves the pointer into the backend's
// window (XWarpPointer: a real MotionNotify), sends it a key press (XSendEvent) and resizes it (XResizeWindow: ConfigureNotify),
// each from a helper thread 60 ms into a 3 s wait. Run under Xvfb by tests/run_under_xvfb.sh. Written from the Xlib manual and
// poll(2); nothing copied.
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"
#include "OkayThreads.hpp"
#include <chrono>
#include <cstdlib>
#include <functional>
#include <thread>
#if defined(OKAY_TEST_HAVE_X11)
#include <X11/Xlib.h>
#include <X11/keysym.h>
#undef CursorShape
#endif

using namespace okay::platform;
using Clock = std::chrono::steady_clock;

static double MsSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }
static void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// WaitEvents(3000) while `poke` runs on another thread after 60 ms. Returns how long the wait took; `woke` = its result.
static double WaitWhile(Backend& b, const std::function<void()>& poke, bool& woke) {
    std::thread t([&] { SleepMs(60); poke(); });
    const Clock::time_point t0 = Clock::now();
    woke = b.WaitEvents(3000);
    const double ms = MsSince(t0);
    t.join();
    return ms;
}

static void Drain(Backend& b) {   // let the server's events for a new window arrive and throw them away
    Event e;
    for (int i = 0; i < 20; ++i) { b.PumpEvents(); while (b.PollEvent(e)) {} SleepMs(10); }
}

static bool Queued(Backend& b, EventType type) {
    Event e; bool seen = false;
    b.PumpEvents();
    while (b.PollEvent(e)) if (e.type == type) seen = true;
    return seen;
}

static void TestWakes(Backend& b) {
    bool woke = false;
    // Nothing happens: the wait lasts its timeout and says so.
    Drain(b);
    Clock::time_point t0 = Clock::now();
    CHECK(!b.WaitEvents(40));
    CHECK(MsSince(t0) >= 35.0);
    // A WakeWait before the wait: it returns at once.
    b.WakeWait();
    t0 = Clock::now();
    CHECK(b.WaitEvents(2000));
    CHECK(MsSince(t0) < 50.0);
    // A WakeWait from another thread ends the wait.
    double ms = WaitWhile(b, [&] { b.WakeWait(); }, woke);
    CHECK(woke); CHECK(ms >= 50.0 && ms < 1000.0);
    // A finished background job: okay::thr threads call the hook the editor installs (WakeMainLoop) when they return.
    okay::thr::ThreadFinishedHook() = &WakeMainLoop;
    t0 = Clock::now();
    okay::thr::Thread* job = okay::thr::CreateThread([](void*) { SleepMs(60); return 0; }, "job", nullptr);
    CHECK(job != nullptr);
    woke = b.WaitEvents(3000);
    ms = MsSince(t0);
    CHECK(woke); CHECK(ms >= 50.0 && ms < 1000.0);
    okay::thr::WaitThread(job, nullptr);
    okay::thr::ThreadFinishedHook() = nullptr;
    // A queued event (PushEvent: how the editor's own input injection arrives) makes the wait return at once.
    Event k; k.type = EventType::KeyDown; k.key = Key::A;
    CHECK(b.PushEvent(k));
    t0 = Clock::now();
    CHECK(b.WaitEvents(2000));
    CHECK(MsSince(t0) < 50.0);
    CHECK(Queued(b, EventType::KeyDown));   // still there for PollEvent
}

#if defined(OKAY_TEST_HAVE_X11)
static void TestXInput(Backend& b) {
    WindowDesc wd; wd.title = "wait"; wd.x = 100; wd.y = 100; wd.width = 320; wd.height = 240;
    auto win = b.MakeWindow(wd);
    CHECK(win != nullptr);
    if (!win) return;
    const ::Window xw = (::Window)win->Id();
    Drain(b);
    bool woke = false;
    // A mouse move into the window from another client.
    double ms = WaitWhile(b, [&] {
        Display* d = XOpenDisplay(nullptr);
        XWarpPointer(d, None, xw, 0, 0, 0, 0, 40, 30); XSync(d, False); XCloseDisplay(d);
    }, woke);
    CHECK(woke); CHECK(ms >= 50.0 && ms < 1000.0);
    CHECK(Queued(b, EventType::MouseMove));
    Drain(b);
    // A key press sent to the window.
    ms = WaitWhile(b, [&] {
        Display* d = XOpenDisplay(nullptr);
        XEvent ev{}; ev.xkey.type = KeyPress; ev.xkey.display = d; ev.xkey.window = xw; ev.xkey.root = DefaultRootWindow(d);
        ev.xkey.subwindow = None; ev.xkey.time = CurrentTime; ev.xkey.x = ev.xkey.y = 10; ev.xkey.same_screen = True;
        ev.xkey.keycode = XKeysymToKeycode(d, XK_a); ev.xkey.state = 0;
        XSendEvent(d, xw, True, KeyPressMask, &ev); XSync(d, False); XCloseDisplay(d);
    }, woke);
    CHECK(woke); CHECK(ms >= 50.0 && ms < 1000.0);
    CHECK(Queued(b, EventType::KeyDown));
    Drain(b);
    // A window event: another client resizes the window.
    ms = WaitWhile(b, [&] {
        Display* d = XOpenDisplay(nullptr);
        XResizeWindow(d, xw, 400, 300); XSync(d, False); XCloseDisplay(d);
    }, woke);
    CHECK(woke); CHECK(ms >= 50.0 && ms < 1000.0);
    CHECK(Queued(b, EventType::WindowResized));
}
#endif

int main() {
    Backend* b = Get(BackendKind::Own);
    const bool display = std::getenv("DISPLAY") != nullptr;
    if (!b || !display || !b->Init(InitFlags())) {
        std::cout << "[skip] the idle wait test needs the own backend and an X display\n";
        return 77;
    }
    TestWakes(*b);
#if defined(OKAY_TEST_HAVE_X11)
    TestXInput(*b);
#endif
    TEST_MAIN_RESULT();
}
