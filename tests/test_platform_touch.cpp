// Touch input on our own backends (okay::platform FingerDown / FingerMotion / FingerUp, Backend::TouchDeviceCount).
//
//   test_platform_touch unit   the shared translation (TouchTracker: finger events, normalised positions, pressure, the
//                              first finger as the mouse) and the XInput 2.2 part of the X11 backend fed with XI2 event and
//                              device structs the test builds itself (OwnLinuxXI2.hpp). No display.
//   test_platform_touch x11    our X11 backend under Xvfb (tests/run_under_xvfb.sh): the server speaks XInput 2.2, the
//                              backend's TouchDeviceCount agrees with the server's own device list (Xvfb has no touch
//                              device: 0), and with touch selected on its window the mouse still arrives (XTEST motion and a
//                              click), not marked as touch. Xvfb cannot make touch events, so a real XI_TouchBegin from the
//                              server is NOT exercised here.
//
// Exit code 77 = skipped (no display, backend or header not compiled in).
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"
#include "Platform/Internal.hpp"   // TouchTracker, TouchSample
#if defined(__linux__)
#include "Platform/OwnLinuxXI2.hpp"
#endif
#if defined(__linux__) && OKAY_X11_HAVE_XI2
#include <dlfcn.h>
#endif
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace okay::platform;

namespace {

TouchSample Sample(long long id, TouchPhase ph, double x, double y, uint32_t ms, float pressure = 1.0f, uint32_t win = 5) {
    TouchSample s;
    s.id = id; s.phase = ph; s.x = x; s.y = y; s.winW = 400; s.winH = 200; s.pressure = pressure; s.windowId = win; s.timestampMs = ms;
    return s;
}
int Count(const std::vector<Event>& ev, EventType t) {
    int n = 0;
    for (const Event& e : ev) n += e.type == t;
    return n;
}
const Event* First(const std::vector<Event>& ev, EventType t) {
    for (const Event& e : ev) if (e.type == t) return &e;
    return nullptr;
}

void TrackerSuite() {
    RUN_SUITE("NormalizedTouch");
    CHECK(NormalizedTouch(100.0, 400) == 0.25f);
    CHECK(NormalizedTouch(-3.0, 400) == 0.0f);
    CHECK(NormalizedTouch(401.0, 400) == 1.0f);
    CHECK(NormalizedTouch(5.0, 0) == 0.0f);

    RUN_SUITE("TouchTracker without the mouse (Win32: Windows makes the mouse messages)");
    {
        TouchTracker t(false);
        std::vector<Event> ev;
        t.Feed(Sample(9, TouchPhase::Begin, 100, 50, 10, 0.5f), ev);
        CHECK(ev.size() == 1);
        CHECK(ev[0].type == EventType::FingerDown && ev[0].fingerId == 9 && ev[0].fx == 0.25f && ev[0].fy == 0.25f);
        CHECK(ev[0].pressure == 0.5f && ev[0].windowId == 5 && ev[0].timestampMs == 10);
        CHECK(t.ActiveContacts() == 1);
        ev.clear();
        t.Feed(Sample(9, TouchPhase::Move, 200, 100, 20, 2.0f), ev);   // pressure clamped to 1
        CHECK(ev.size() == 1 && ev[0].type == EventType::FingerMotion && ev[0].fx == 0.5f && ev[0].fy == 0.5f && ev[0].pressure == 1.0f);
        ev.clear();
        t.Feed(Sample(9, TouchPhase::Begin, 210, 100, 25), ev);        // a repeated Begin is a Move
        CHECK(ev.size() == 1 && ev[0].type == EventType::FingerMotion);
        ev.clear();
        t.Feed(Sample(4, TouchPhase::Move, 10, 10, 26), ev);           // a contact we never saw begin: dropped
        t.Feed(Sample(4, TouchPhase::End, 10, 10, 27), ev);
        CHECK(ev.empty());
        t.Feed(Sample(9, TouchPhase::End, 400, 200, 30), ev);
        CHECK(ev.size() == 1 && ev[0].type == EventType::FingerUp && ev[0].fx == 1.0f && ev[0].fy == 1.0f);
        CHECK(t.ActiveContacts() == 0);
        CHECK(Count(ev, EventType::MouseButtonDown) == 0);
        ev.clear();
        t.Feed(Sample(1, TouchPhase::Begin, 0, 0, 40), ev);
        t.Feed(Sample(1, TouchPhase::Cancel, 0, 0, 41), ev);           // a cancel lifts the finger too
        CHECK(Count(ev, EventType::FingerUp) == 1 && t.ActiveContacts() == 0);
        ev.clear();
        t.Feed(Sample(2, TouchPhase::Begin, 0, 0, 50, 1.0f, 7), ev);
        t.ForgetWindow(7);                                             // the window went away
        CHECK(t.ActiveContacts() == 0);
        t.Feed(Sample(2, TouchPhase::Move, 5, 5, 51, 1.0f, 7), ev);
        CHECK(ev.size() == 1);
    }

    RUN_SUITE("TouchTracker with the first finger as the mouse (X11, as SDL does)");
    {
        TouchTracker t(true);
        std::vector<Event> ev;
        t.Feed(Sample(3, TouchPhase::Begin, 100, 50, 100), ev);
        // FingerDown, MouseMove to the finger, left button down (one click)
        CHECK(ev.size() == 3);
        CHECK(ev.size() == 3 && ev[0].type == EventType::FingerDown && ev[1].type == EventType::MouseMove && ev[2].type == EventType::MouseButtonDown);
        const Event* d = First(ev, EventType::MouseButtonDown);
        CHECK(d && d->button == MouseLeft && d->clicks == 1 && d->x == 100 && d->y == 50 && d->fromTouch && d->windowId == 5);
        ev.clear();
        t.Feed(Sample(8, TouchPhase::Begin, 300, 150, 110), ev);       // a second finger: touch only
        CHECK(ev.size() == 1 && ev[0].type == EventType::FingerDown);
        ev.clear();
        t.Feed(Sample(8, TouchPhase::Move, 310, 150, 115), ev);
        CHECK(ev.size() == 1 && ev[0].type == EventType::FingerMotion);
        ev.clear();
        t.Feed(Sample(3, TouchPhase::Move, 120.4, 60.6, 120), ev);     // the first finger moves the mouse (rounded)
        const Event* m = First(ev, EventType::MouseMove);
        CHECK(Count(ev, EventType::FingerMotion) == 1);
        CHECK(m && m->x == 120 && m->y == 61 && m->dx == 20 && m->dy == 11 && m->fromTouch);
        ev.clear();
        t.Feed(Sample(3, TouchPhase::Move, 120.2, 60.9, 121), ev);     // the same pixel: no mouse move
        CHECK(ev.size() == 1 && ev[0].type == EventType::FingerMotion);
        ev.clear();
        t.Feed(Sample(3, TouchPhase::End, 120, 61, 130), ev);
        CHECK(ev.size() == 2 && ev[0].type == EventType::FingerUp && ev[1].type == EventType::MouseButtonUp);
        CHECK(ev.size() == 2 && ev[1].button == MouseLeft && ev[1].fromTouch);
        ev.clear();
        t.Feed(Sample(8, TouchPhase::End, 310, 150, 131), ev);         // the second finger lifts: no mouse
        CHECK(ev.size() == 1 && ev[0].type == EventType::FingerUp);
        ev.clear();
        // Soon after, but 25 x 15 px from the first press: too far for a double click.
        t.Feed(Sample(11, TouchPhase::Begin, 125, 65, 300), ev);
        d = First(ev, EventType::MouseButtonDown);
        CHECK(d && d->clicks == 1);
        // A tap near that one, 200 ms later: a double click.
        ev.clear();
        t.Feed(Sample(11, TouchPhase::End, 125, 65, 320), ev);
        ev.clear();
        t.Feed(Sample(12, TouchPhase::Begin, 130, 60, 500), ev);
        d = First(ev, EventType::MouseButtonDown);
        CHECK(d && d->clicks == 2);
        ev.clear();
        t.Feed(Sample(12, TouchPhase::End, 130, 60, 510), ev);
        ev.clear();
        t.Feed(Sample(13, TouchPhase::Begin, 300, 60, 600), ev);       // far away: one click again
        d = First(ev, EventType::MouseButtonDown);
        CHECK(d && d->clicks == 1);
        ev.clear();
        t.Feed(Sample(13, TouchPhase::End, 300, 60, 610), ev);
        ev.clear();
        t.Feed(Sample(14, TouchPhase::Begin, 300, 60, 1200), ev);      // too late: one click
        d = First(ev, EventType::MouseButtonDown);
        CHECK(d && d->clicks == 1);
    }
}

#if defined(__linux__) && OKAY_X11_HAVE_XI2
// Atoms are only numbers to the translation; these two stand for "Abs MT Pressure" and "Abs Pressure".
constexpr Atom kMt = 301, kAbs = 302, kOther = 303;

void Xi2Suite() {
    RUN_SUITE("XI2: valuator values (packed by mask bit)");
    unsigned char mask[2] = {0, 0};
    XISetMask(mask, 0); XISetMask(mask, 1); XISetMask(mask, 5); XISetMask(mask, 9);
    double values[4] = {10.0, 20.0, 55.0, 99.0};
    XIValuatorState vs; vs.mask_len = 2; vs.mask = mask; vs.values = values;
    double v = 0;
    CHECK(own::XIValuatorValue(vs, 0, v) && v == 10.0);
    CHECK(own::XIValuatorValue(vs, 5, v) && v == 55.0);
    CHECK(own::XIValuatorValue(vs, 9, v) && v == 99.0);
    CHECK(!own::XIValuatorValue(vs, 2, v));
    CHECK(!own::XIValuatorValue(vs, 16, v));   // past the mask
    CHECK(!own::XIValuatorValue(vs, -1, v));

    RUN_SUITE("XI2: the pressure axis of a device");
    XIValuatorClassInfo vx{}; vx.type = XIValuatorClass; vx.number = 0; vx.label = kOther; vx.min = 0; vx.max = 4095;
    XIValuatorClassInfo vabs{}; vabs.type = XIValuatorClass; vabs.number = 2; vabs.label = kAbs; vabs.min = 0; vabs.max = 255;
    XIValuatorClassInfo vmt{}; vmt.type = XIValuatorClass; vmt.number = 5; vmt.label = kMt; vmt.min = 5; vmt.max = 105;
    XITouchClassInfo tc{}; tc.type = XITouchClass; tc.mode = XIDirectTouch; tc.num_touches = 10;
    XIAnyClassInfo* classes[4] = {reinterpret_cast<XIAnyClassInfo*>(&vx), reinterpret_cast<XIAnyClassInfo*>(&vabs),
                                  reinterpret_cast<XIAnyClassInfo*>(&vmt), reinterpret_cast<XIAnyClassInfo*>(&tc)};
    XIDeviceInfo dev{}; dev.deviceid = 11; dev.use = XISlavePointer; dev.num_classes = 4; dev.classes = classes;
    own::XIPressureAxis ax = own::PressureAxisOf(dev, kMt, kAbs);
    CHECK(ax.number == 5 && ax.min == 5.0 && ax.max == 105.0);   // the per-contact (MT) axis wins
    ax = own::PressureAxisOf(dev, 0, kAbs);
    CHECK(ax.number == 2 && ax.max == 255.0);
    ax = own::PressureAxisOf(dev, 0, 0);
    CHECK(ax.number == -1);

    RUN_SUITE("XI2: touch device events as TouchSamples");
    XIDeviceEvent e{};
    e.type = GenericEvent; e.evtype = XI_TouchBegin; e.detail = 42; e.event = 0x400001; e.event_x = 64.0; e.event_y = 48.5;
    e.sourceid = 11; e.deviceid = 2;
    e.valuators = vs;   // pressure valuator 5 = 55 -> (55 - 5) / 100 = 0.5
    own::XIPressureAxis mt = own::PressureAxisOf(dev, kMt, kAbs);
    TouchSample s;
    CHECK(own::TouchSampleFromXI(e, mt, 640, 480, 77, 1234, s));
    CHECK(s.id == 42 && s.phase == TouchPhase::Begin && s.x == 64.0 && s.y == 48.5 && s.winW == 640 && s.winH == 480);
    CHECK(s.windowId == 77 && s.timestampMs == 1234);
    CHECK_NEAR(s.pressure, 0.5, 1e-6);
    e.evtype = XI_TouchUpdate;
    CHECK(own::TouchSampleFromXI(e, mt, 640, 480, 77, 1235, s) && s.phase == TouchPhase::Move);
    e.evtype = XI_TouchEnd;
    CHECK(own::TouchSampleFromXI(e, mt, 640, 480, 77, 1236, s) && s.phase == TouchPhase::End);
    own::XIPressureAxis none;
    CHECK(own::TouchSampleFromXI(e, none, 640, 480, 77, 1236, s) && s.pressure == 1.0f);   // no axis: 1
    unsigned char noMask[1] = {0};
    XIValuatorState empty; empty.mask_len = 1; empty.mask = noMask; empty.values = values;
    e.valuators = empty;
    CHECK(own::TouchSampleFromXI(e, mt, 640, 480, 77, 1236, s) && s.pressure == 1.0f);       // axis not in this event: 1
    values[2] = 500.0; e.valuators = vs;
    CHECK(own::TouchSampleFromXI(e, mt, 640, 480, 77, 1236, s) && s.pressure == 1.0f);       // above the range: clamped
    e.evtype = XI_Motion;
    CHECK(!own::TouchSampleFromXI(e, mt, 640, 480, 77, 1236, s));
    e.evtype = XI_ButtonPress;
    CHECK(!own::TouchSampleFromXI(e, mt, 640, 480, 77, 1236, s));
    e.evtype = XI_TouchBegin; e.detail = (int)0x80000001u;   // touch ids are unsigned 32-bit numbers
    CHECK(own::TouchSampleFromXI(e, mt, 640, 480, 77, 1236, s) && s.id == 0x80000001LL);

    RUN_SUITE("XI2: an XI2 event through the tracker (as the backend does it)");
    {
        TouchTracker t(true);
        std::vector<Event> ev;
        e.evtype = XI_TouchBegin; e.detail = 7; e.event_x = 320; e.event_y = 120; values[2] = 30.0; e.valuators = vs;
        CHECK(own::TouchSampleFromXI(e, mt, 640, 480, 77, 50, s));
        t.Feed(s, ev);
        const Event* f = First(ev, EventType::FingerDown);
        CHECK(f && f->fingerId == 7 && f->fx == 0.5f && f->fy == 0.25f && f->windowId == 77);
        CHECK(f && std::fabs(f->pressure - 0.25f) < 1e-6f);
        CHECK(Count(ev, EventType::MouseButtonDown) == 1);
    }

    RUN_SUITE("XI2: counting touch devices");
    XIValuatorClassInfo mx{}; mx.type = XIValuatorClass;
    XIAnyClassInfo* mouseClasses[1] = {reinterpret_cast<XIAnyClassInfo*>(&mx)};
    XITouchClassInfo pad{}; pad.type = XITouchClass; pad.mode = XIDependentTouch; pad.num_touches = 5;
    XIAnyClassInfo* padClasses[2] = {reinterpret_cast<XIAnyClassInfo*>(&mx), reinterpret_cast<XIAnyClassInfo*>(&pad)};
    XIDeviceInfo list[5] = {};
    list[0].deviceid = 2; list[0].use = XIMasterPointer; list[0].num_classes = 4; list[0].classes = classes;   // master: mirrors, not counted
    list[1].deviceid = 11; list[1].use = XISlavePointer; list[1].num_classes = 4; list[1].classes = classes;   // a touch screen
    list[2].deviceid = 12; list[2].use = XISlavePointer; list[2].num_classes = 1; list[2].classes = mouseClasses;   // a mouse
    list[3].deviceid = 13; list[3].use = XIFloatingSlave; list[3].num_classes = 2; list[3].classes = padClasses;   // a touch pad
    list[4].deviceid = 14; list[4].use = XISlaveKeyboard; list[4].num_classes = 0; list[4].classes = nullptr;
    CHECK(own::CountTouchDevices(list, 5) == 2);
    CHECK(own::CountTouchDevices(list, 1) == 0);
    CHECK(own::CountTouchDevices(nullptr, 3) == 0);
}
#endif

int Unit() {
    TrackerSuite();
#if defined(__linux__) && OKAY_X11_HAVE_XI2
    Xi2Suite();
#else
    std::printf("(XI2 translation not compiled in: no XInput2.h)\n");
#endif
    TEST_MAIN_RESULT();
}

#if defined(__linux__) && OKAY_X11_HAVE_XI2
int X11() {
    Backend* b = Get(BackendKind::Own);
    if (!b) { std::printf("SKIP: own backend not compiled in\n"); return 77; }
    InitFlags fl; fl.audio = false; fl.gamepad = false;
    if (!b->Init(fl)) { std::printf("SKIP: own: no display (%s)\n", b->LastError().c_str()); return 77; }
    // The test's own connection: what the server says about XInput and its devices, and XTEST input.
    void* x11 = dlopen("libX11.so.6", RTLD_NOW);
    void* xi = dlopen("libXi.so.6", RTLD_NOW);
    void* xtst = dlopen("libXtst.so.6", RTLD_NOW);
    if (!x11 || !xi || !xtst) { std::printf("SKIP: libX11 / libXi / libXtst not installed\n"); return 77; }
    auto openDisplay = reinterpret_cast<Display* (*)(const char*)>(dlsym(x11, "XOpenDisplay"));
    auto closeDisplay = reinterpret_cast<int (*)(Display*)>(dlsym(x11, "XCloseDisplay"));
    auto queryExt = reinterpret_cast<Bool (*)(Display*, const char*, int*, int*, int*)>(dlsym(x11, "XQueryExtension"));
    auto flush = reinterpret_cast<int (*)(Display*)>(dlsym(x11, "XFlush"));
    auto sync = reinterpret_cast<int (*)(Display*, Bool)>(dlsym(x11, "XSync"));
    auto translate = reinterpret_cast<Bool (*)(Display*, ::Window, ::Window, int, int, int*, int*, ::Window*)>(dlsym(x11, "XTranslateCoordinates"));
    auto queryVersion = reinterpret_cast<Status (*)(Display*, int*, int*)>(dlsym(xi, "XIQueryVersion"));
    auto queryDevice = reinterpret_cast<XIDeviceInfo* (*)(Display*, int, int*)>(dlsym(xi, "XIQueryDevice"));
    auto freeDevice = reinterpret_cast<void (*)(XIDeviceInfo*)>(dlsym(xi, "XIFreeDeviceInfo"));
    auto fakeMotion = reinterpret_cast<int (*)(Display*, int, int, int, unsigned long)>(dlsym(xtst, "XTestFakeMotionEvent"));
    auto fakeButton = reinterpret_cast<int (*)(Display*, unsigned, Bool, unsigned long)>(dlsym(xtst, "XTestFakeButtonEvent"));
    Display* dpy = openDisplay(nullptr);
    if (!dpy) { std::printf("SKIP: cannot open the display\n"); return 77; }

    RUN_SUITE("x11: the server speaks XInput 2.2 (what the backend needs to select touch)");
    int op = 0, evb = 0, erb = 0;
    CHECK(queryExt(dpy, "XInputExtension", &op, &evb, &erb));
    int major = 2, minor = 2;
    CHECK(queryVersion(dpy, &major, &minor) == Success);
    std::printf("  server XInput %d.%d\n", major, minor);
    CHECK(major > 2 || (major == 2 && minor >= 2));

    RUN_SUITE("x11: TouchDeviceCount matches the server's device list");
    int n = 0;
    XIDeviceInfo* devs = queryDevice(dpy, XIAllDevices, &n);
    const int serverCount = devs ? own::CountTouchDevices(devs, n) : -1;
    for (int i = 0; devs && i < n; ++i) std::printf("  device %d '%s' use %d classes %d\n", devs[i].deviceid, devs[i].name, devs[i].use, devs[i].num_classes);
    if (devs) freeDevice(devs);
    std::printf("  touch devices: server list %d, backend %d\n", serverCount, b->TouchDeviceCount());
    CHECK(serverCount == b->TouchDeviceCount());
    CHECK(b->TouchDeviceCount() == 0);   // Xvfb has none

    RUN_SUITE("x11: with touch selected on the window, the mouse still works and is not marked as touch");
    WindowDesc d; d.title = "touch test"; d.width = 320; d.height = 240; d.x = 40; d.y = 40;
    std::unique_ptr<okay::platform::Window> w = b->MakeWindow(d);
    CHECK(w != nullptr);
    if (w) {
        Event e;
        for (int i = 0; i < 50; ++i) { while (b->PollEvent(e)) {} b->DelayMs(10); }

        // Proof that the backend selected touch on its window, seen from outside: the XI 2.2 specification lets only one
        // client select touch events on a window for a device, so our second connection's selection must fail with BadAccess
        // (and succeed when the backend was told not to select touch with OKAY_X11_TOUCH=0).
        {
            auto setHandler = reinterpret_cast<XErrorHandler (*)(XErrorHandler)>(dlsym(x11, "XSetErrorHandler"));
            auto selectEvents = reinterpret_cast<int (*)(Display*, ::Window, XIEventMask*, int)>(dlsym(xi, "XISelectEvents"));
            static int s_err = 0;
            s_err = 0;
            XErrorHandler old = setHandler([](Display*, XErrorEvent* ev) { s_err = ev->error_code; return 0; });
            unsigned char bits[XIMaskLen(XI_LASTEVENT)] = {};
            XISetMask(bits, XI_TouchBegin); XISetMask(bits, XI_TouchUpdate); XISetMask(bits, XI_TouchEnd);
            XIEventMask mask; mask.deviceid = XIAllMasterDevices; mask.mask_len = (int)sizeof(bits); mask.mask = bits;
            selectEvents(dpy, (::Window)(uintptr_t)w->NativeHandle(), &mask, 1);
            sync(dpy, False);
            setHandler(old);
            const char* env = std::getenv("OKAY_X11_TOUCH");
            const bool off = env && std::strcmp(env, "0") == 0;
            std::printf("  a second client selecting touch on the window: error %d (%s expected)\n", s_err, off ? "none" : "BadAccess");
            CHECK(off ? s_err == 0 : s_err == BadAccess);
        }
        int rx = 0, ry = 0; ::Window child = 0;
        const ::Window root = DefaultRootWindow(dpy);
        translate(dpy, (::Window)(uintptr_t)w->NativeHandle(), root, 100, 80, &rx, &ry, &child);
        fakeMotion(dpy, -1, rx, ry, 0); sync(dpy, False);
        fakeButton(dpy, 1, True, 0); fakeButton(dpy, 1, False, 0); flush(dpy); sync(dpy, False);
        bool moved = false, down = false, up = false, touched = false, finger = false;
        for (int i = 0; i < 100 && !(moved && down && up); ++i) {
            while (b->PollEvent(e)) {
                if (e.type == EventType::MouseMove && e.x == 100 && e.y == 80) moved = true;
                if (e.type == EventType::MouseButtonDown && e.button == MouseLeft) down = true;
                if (e.type == EventType::MouseButtonUp && e.button == MouseLeft) up = true;
                if ((e.type == EventType::MouseMove || e.type == EventType::MouseButtonDown || e.type == EventType::MouseButtonUp) && e.fromTouch) touched = true;
                if (e.type == EventType::FingerDown || e.type == EventType::FingerMotion || e.type == EventType::FingerUp) finger = true;
            }
            b->DelayMs(10);
        }
        CHECK(moved); CHECK(down); CHECK(up);
        CHECK(!touched); CHECK(!finger);
        w.reset();
    }
    closeDisplay(dpy);
    b->Shutdown();
    TEST_MAIN_RESULT();
}
#endif

} // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "unit";
    if (mode == "unit") return Unit();
#if defined(__linux__) && OKAY_X11_HAVE_XI2
    if (mode == "x11") return X11();
#else
    if (mode == "x11") { std::printf("SKIP: no XInput2 header in this build\n"); return 77; }
#endif
    std::fprintf(stderr, "usage: test_platform_touch unit|x11\n");
    return 2;
}
