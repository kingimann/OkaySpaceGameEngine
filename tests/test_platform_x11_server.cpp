// The own X11 backend gaps against a real X server (run under Xvfb by tests/run_under_xvfb.sh):
//  * Xdnd drag and drop receive, with OUR OWN Xdnd source written here with plain Xlib (a second connection).
//    NOT checked against a real file manager (Nautilus, Dolphin, ...): unverified there.
//  * RandR monitor list (virtual monitors defined through RandR 1.5), per-monitor dpi from Xft.dpi in RESOURCE_MANAGER
//  * cursor shapes from the core cursor font (accepted by the server; what is drawn is not read back)
//  * the urgency hint (WM_HINTS) on RequestAttention
//  * the plain message box, closed by a key and by a click injected with xdotool
//  * a failing run of XIM preedit is NOT tested here: Xvfb has no input method server (unverified).
//
//   test_platform_x11_server        (exit 77 = skipped: no display / no xdotool)
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"
#include "Platform/Internal.hpp"
#include "Platform/OwnLinux.hpp"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#if defined(__has_include)
#if __has_include(<X11/extensions/Xrandr.h>) && !defined(OKAY_TEST_NO_XRANDR)
#include <X11/extensions/Xrandr.h>
#define TEST_HAVE_XRANDR 1
#endif
#endif
#ifdef CursorShape
#undef CursorShape
#endif
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <string>
#include <thread>
#include <vector>

using namespace okay::platform;

namespace {

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

std::vector<Event> Pump(Backend& b, int ms) {
    std::vector<Event> evs;
    const auto t0 = std::chrono::steady_clock::now();
    do {
        Event e;
        while (b.PollEvent(e)) evs.push_back(std::move(e));
        SleepMs(5);
    } while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(ms));
    return evs;
}

// ---- an Xdnd source, plain Xlib -------------------------------------------------------------
struct Source {
    Display* d = nullptr;
    ::Window win = 0;
    Atom aEnter, aPos, aLeave, aDrop, aStatus, aFinished, aSel, aTypeList, aCopy, aUri, aText, aOther;
    std::string payload;
    bool gotStatus = false, statusAccept = false;
    unsigned long statusAction = 0;
    bool gotFinished = false, finishedOk = false;
    int requests = 0;

    bool Open() {
        d = XOpenDisplay(nullptr);
        if (!d) return false;
        win = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 10, 10, 0, 0, 0);
        auto A = [&](const char* n) { return XInternAtom(d, n, False); };
        aEnter = A("XdndEnter"); aPos = A("XdndPosition"); aLeave = A("XdndLeave"); aDrop = A("XdndDrop");
        aStatus = A("XdndStatus"); aFinished = A("XdndFinished"); aSel = A("XdndSelection"); aTypeList = A("XdndTypeList");
        aCopy = A("XdndActionCopy"); aUri = A("text/uri-list"); aText = A("text/plain"); aOther = A("application/x-test");
        return true;
    }
    ~Source() { if (d) XCloseDisplay(d); }
    void Send(::Window to, Atom type, long a, long b, long c, long dd, long e) {
        XEvent ev; std::memset(&ev, 0, sizeof(ev));
        ev.xclient.type = ClientMessage; ev.xclient.window = to; ev.xclient.message_type = type; ev.xclient.format = 32;
        ev.xclient.data.l[0] = a; ev.xclient.data.l[1] = b; ev.xclient.data.l[2] = c; ev.xclient.data.l[3] = dd; ev.xclient.data.l[4] = e;
        XSendEvent(d, to, False, NoEventMask, &ev);
        XFlush(d);
    }
    // Handle what the target sent back (status, finished) and serve the selection.
    void Process() {
        while (XPending(d)) {
            XEvent ev; XNextEvent(d, &ev);
            if (ev.type == ClientMessage && ev.xclient.message_type == aStatus) {
                gotStatus = true; statusAccept = (ev.xclient.data.l[1] & 1) != 0; statusAction = (unsigned long)ev.xclient.data.l[4];
            } else if (ev.type == ClientMessage && ev.xclient.message_type == aFinished) {
                gotFinished = true; finishedOk = (ev.xclient.data.l[1] & 1) != 0;
            } else if (ev.type == SelectionRequest) {
                requests++;
                XSelectionRequestEvent& rq = ev.xselectionrequest;
                XEvent r; std::memset(&r, 0, sizeof(r));
                r.xselection.type = SelectionNotify; r.xselection.requestor = rq.requestor; r.xselection.selection = rq.selection;
                r.xselection.target = rq.target; r.xselection.time = rq.time; r.xselection.property = None;
                if (rq.selection == aSel && rq.target == aUri) {
                    XChangeProperty(d, rq.requestor, rq.property, rq.target, 8, PropModeReplace, (const unsigned char*)payload.data(), (int)payload.size());
                    r.xselection.property = rq.property;
                }
                XSendEvent(d, rq.requestor, False, NoEventMask, &r);
                XFlush(d);
            }
        }
    }
    void Reset() { gotStatus = statusAccept = gotFinished = finishedOk = false; statusAction = 0; requests = 0; }
};

// Run both sides for `ms`, collecting the backend's events.
std::vector<Event> Both(Backend& b, Source& s, int ms) {
    std::vector<Event> evs;
    const auto t0 = std::chrono::steady_clock::now();
    do {
        Event e;
        while (b.PollEvent(e)) evs.push_back(std::move(e));
        s.Process();
        SleepMs(3);
    } while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(ms));
    return evs;
}

int Count(const std::vector<Event>& v, EventType t) { int n = 0; for (auto& e : v) if (e.type == t) ++n; return n; }

std::string Sh(const std::string& cmd) {
    std::string out;
    if (FILE* p = popen(cmd.c_str(), "r")) { char buf[256]; while (size_t n = fread(buf, 1, sizeof buf, p)) out.append(buf, n); pclose(p); }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

void TestDnd(Backend& b, okay::platform::Window& win, Source& s) {
    RUN_SUITE("xdnd receive against our own source");
    ::Window target = (::Window)(uintptr_t)win.NativeHandle();
    int wx = 0, wy = 0; win.GetPosition(wx, wy);
    // The window announces Xdnd version 5.
    {
        Atom aware = XInternAtom(s.d, "XdndAware", False), type; int fmt; unsigned long n, rest; unsigned char* data = nullptr;
        CHECK(XGetWindowProperty(s.d, target, aware, 0, 1, False, XA_ATOM, &type, &fmt, &n, &rest, &data) == Success && data && n == 1 && *(long*)data == 5);
        if (data) XFree(data);
    }
    XSetSelectionOwner(s.d, s.aSel, s.win, CurrentTime);
    s.payload = "# dropped\r\nfile:///tmp/okay%20drop/a%20b.txt\r\nfile:///tmp/okay drop/c.png\r\nhttp://example.com/skip\r\nfile:///home/x/\xC3\xA9.txt\r\n";
    // -- a good drop: Enter (3 types), Position, Drop
    s.Reset();
    s.Send(target, s.aEnter, (long)s.win, 5L << 24, (long)s.aOther, (long)s.aUri, 0);
    s.Send(target, s.aPos, (long)s.win, 0, ((long)(wx + 30) << 16) | (wy + 40), 0, (long)s.aCopy);
    auto ev = Both(b, s, 200);
    CHECK(s.gotStatus && s.statusAccept && s.statusAction == s.aCopy);
    CHECK(Count(ev, EventType::DropFile) == 0);        // nothing until the drop
    s.Send(target, s.aDrop, (long)s.win, 0, 0, 0, 0);
    ev = Both(b, s, 400);
    CHECK(s.requests == 1);
    CHECK(s.gotFinished && s.finishedOk);
    std::vector<std::string> files;
    int begin = -1, complete = -1, idx = 0, fx = -1, fy = -1;
    for (auto& e : ev) {
        if (e.type == EventType::DropBegin) begin = idx;
        if (e.type == EventType::DropComplete) complete = idx;
        if (e.type == EventType::DropFile) { files.push_back(e.text); fx = e.x; fy = e.y; CHECK(e.windowId == (uint32_t)target); }
        idx++;
    }
    CHECK(files.size() == 3);
    if (files.size() == 3) CHECK(files[0] == "/tmp/okay drop/a b.txt" && files[1] == "/tmp/okay drop/c.png" && files[2] == "/home/x/\xC3\xA9.txt");
    CHECK(begin >= 0 && complete > begin && complete == begin + 4);
    CHECK(fx == 30 && fy == 40);                        // window-local drop position
    // -- more than three types: the list is read from the XdndTypeList property of the source
    {
        Atom list[5] = {s.aText, s.aOther, s.aText, s.aOther, s.aUri};
        XChangeProperty(s.d, s.win, s.aTypeList, XA_ATOM, 32, PropModeReplace, (const unsigned char*)list, 5);
        s.Reset();
        s.Send(target, s.aEnter, (long)s.win, (5L << 24) | 1, (long)s.aText, (long)s.aOther, (long)s.aText);
        s.Send(target, s.aPos, (long)s.win, 0, ((long)(wx + 5) << 16) | (wy + 6), 0, (long)s.aCopy);
        Both(b, s, 150);
        CHECK(s.gotStatus && s.statusAccept);
        s.Send(target, s.aDrop, (long)s.win, 0, 0, 0, 0);
        ev = Both(b, s, 300);
        CHECK(Count(ev, EventType::DropFile) == 3 && s.finishedOk);
    }
    // -- no uri list among the types: refused in the status and in Finished, no events
    {
        s.Reset();
        s.Send(target, s.aEnter, (long)s.win, 5L << 24, (long)s.aText, (long)s.aOther, 0);
        s.Send(target, s.aPos, (long)s.win, 0, ((long)(wx + 5) << 16) | (wy + 6), 0, (long)s.aCopy);
        Both(b, s, 150);
        CHECK(s.gotStatus && !s.statusAccept && s.statusAction == 0);
        s.Send(target, s.aDrop, (long)s.win, 0, 0, 0, 0);
        ev = Both(b, s, 200);
        CHECK(s.gotFinished && !s.finishedOk && s.requests == 0 && Count(ev, EventType::DropFile) == 0);
    }
    // -- leave, then a stray drop: nothing happens
    {
        s.Reset();
        s.Send(target, s.aEnter, (long)s.win, 5L << 24, (long)s.aUri, 0, 0);
        s.Send(target, s.aLeave, (long)s.win, 0, 0, 0, 0);
        s.Send(target, s.aDrop, (long)s.win, 0, 0, 0, 0);
        ev = Both(b, s, 200);
        CHECK(!s.gotFinished && s.requests == 0 && Count(ev, EventType::DropFile) == 0);
    }
    // -- the source refuses the data (answers with property None): Finished says not accepted
    {
        s.payload.clear();
        s.Reset();
        XSetSelectionOwner(s.d, s.aSel, None, CurrentTime);   // nobody owns XdndSelection: the conversion fails
        s.Send(target, s.aEnter, (long)s.win, 5L << 24, (long)s.aUri, 0, 0);
        s.Send(target, s.aPos, (long)s.win, 0, ((long)(wx + 5) << 16) | (wy + 6), 0, (long)s.aCopy);
        Both(b, s, 100);
        s.Send(target, s.aDrop, (long)s.win, 0, 0, 0, 0);
        ev = Both(b, s, 300);
        CHECK(s.gotFinished && !s.finishedOk && Count(ev, EventType::DropFile) == 0);
    }
    // The clipboard still works after all that (the two selections do not mix).
    CHECK(b.SetClipboardText("after dnd"));
    CHECK(b.GetClipboardText() == "after dnd");
}

void TestMonitors(Backend& b, okay::platform::Window& win) {
    RUN_SUITE("monitors and scale");
    CHECK(b.DisplayCount() >= 1);
    DisplayInfo d;
    CHECK(b.GetDisplayInfo(0, d));
    CHECK(d.width == 1024 && d.height == 768 && d.primary && d.dpi == 96);
    CHECK(!b.GetDisplayInfo(b.DisplayCount(), d) && !b.GetDisplayInfo(-1, d));
    CHECK(win.DisplayDpi() == 96);
    std::cout << "  display 0: " << d.name << " " << d.width << "x" << d.height << " refresh " << d.refreshHz << " Hz\n";
    Display* sd = XOpenDisplay(nullptr);
    CHECK(sd != nullptr);
    if (!sd) return;
    // Xft.dpi in RESOURCE_MANAGER: every monitor follows it (after the half second cache).
    const char* res = "Xft.antialias:\t1\nXft.dpi:\t144\n";
    XChangeProperty(sd, DefaultRootWindow(sd), XA_RESOURCE_MANAGER, XA_STRING, 8, PropModeReplace, (const unsigned char*)res, (int)std::strlen(res));
    XSync(sd, False);
    Pump(b, 700);
    CHECK(b.GetDisplayInfo(0, d) && d.dpi == 144);
    CHECK(win.DisplayDpi() == 144);
#ifdef TEST_HAVE_XRANDR
    int ev = 0, er = 0, maj = 0, mnr = 0;
    if (XRRQueryExtension(sd, &ev, &er) && XRRQueryVersion(sd, &maj, &mnr) && (maj > 1 || mnr >= 5)) {
        // Two virtual monitors side by side (RandR 1.5 XRRSetMonitor); the right one is primary.
        XRRScreenResources* sres = XRRGetScreenResources(sd, DefaultRootWindow(sd));
        // An output belongs to one monitor only: LEFT takes the server's output, RIGHT is a monitor with no output.
        auto mk = [&](const char* name, int x, int w, bool primary, bool claim) {
            XRRMonitorInfo* m = XRRAllocateMonitor(sd, claim && sres && sres->noutput > 0 ? 1 : 0);
            m->name = XInternAtom(sd, name, False); m->primary = primary; m->automatic = False;
            if (m->noutput == 1) m->outputs[0] = sres->outputs[0];
            m->x = x; m->y = 0; m->width = w; m->height = 768; m->mwidth = w / 4; m->mheight = 190;
            XRRSetMonitor(sd, DefaultRootWindow(sd), m);
            XRRFreeMonitors(m);
        };
        mk("LEFT", 0, 400, false, true);
        mk("RIGHT", 400, 624, true, false);
        if (sres) XRRFreeScreenResources(sres);
        XSync(sd, False);
        Pump(b, 700);
        const int n = b.DisplayCount();
        std::cout << "  RandR " << maj << "." << mnr << ": " << n << " monitors after XRRSetMonitor\n";
        if (n == 2) {
            DisplayInfo a, c;
            CHECK(b.GetDisplayInfo(0, a) && b.GetDisplayInfo(1, c));
            CHECK(a.name == "RIGHT" && a.primary && a.x == 400 && a.width == 624 && a.height == 768 && a.dpi == 144);
            CHECK(c.name == "LEFT" && !c.primary && c.x == 0 && c.width == 400);
            // The test window (100,80) lies on the left monitor, which is index 1.
            win.SetPosition(10, 10);
            Pump(b, 100);
            CHECK(win.DisplayDpi() == 144);
        } else {
            std::cout << "  (the server did not accept the virtual monitors: multi-monitor list NOT tested here)\n";
        }
    } else {
        std::cout << "  (no RandR 1.5 on this server: multi-monitor list NOT tested here)\n";
    }
#endif
    XCloseDisplay(sd);
}

void TestCursorAndAttention(Backend& b, okay::platform::Window& win) {
    RUN_SUITE("cursor shapes and attention");
    int ok = 0;
    for (CursorShape s : {CursorShape::Arrow, CursorShape::IBeam, CursorShape::Wait, CursorShape::Crosshair, CursorShape::Hand, CursorShape::SizeNS,
                          CursorShape::SizeWE, CursorShape::SizeNWSE, CursorShape::SizeNESW, CursorShape::SizeAll, CursorShape::NotAllowed})
        if (b.SetCursor(s)) ok++;
    std::cout << "  cursor shapes accepted by the server: " << ok << " of 11\n";
    CHECK(ok == 11);
    b.ShowCursor(false);
    CHECK(!b.IsCursorShown());
    CHECK(b.SetCursor(CursorShape::Hand));   // changing the shape while hidden is fine
    b.ShowCursor(true);
    CHECK(b.IsCursorShown());
    WindowDesc d; d.width = 40; d.height = 40; d.hidden = true;
    auto w2 = b.MakeWindow(d);                // a window made after SetCursor gets the shape too (no crash, no error)
    CHECK(w2 != nullptr);

    Display* sd = XOpenDisplay(nullptr);
    ::Window target = (::Window)(uintptr_t)win.NativeHandle();
    auto urgent = [&]() {
        XWMHints* h = XGetWMHints(sd, target);
        const bool u = h && (h->flags & XUrgencyHint);
        if (h) XFree(h);
        return u;
    };
    CHECK(!urgent());
    CHECK(win.RequestAttention(true));
    XSync(sd, False);
    CHECK(urgent());
    CHECK(win.RequestAttention(false));
    XSync(sd, False);
    CHECK(!urgent());
    win.RequestAttention(true);
    XSync(sd, False);
    CHECK(urgent());
    // "until focused": gaining the focus clears it (no window manager here: another window takes the focus first).
    WindowDesc d3; d3.width = 40; d3.height = 40;
    auto w3 = b.MakeWindow(d3);
    w3->Raise();
    Pump(b, 150);
    win.Raise();
    Pump(b, 200);
    XSync(sd, False);
    CHECK(!urgent());
    XCloseDisplay(sd);
}

void TestMessageBox(Backend& b, okay::platform::Window& win) {
    RUN_SUITE("message box");
    // A thread shows the box (it blocks); xdotool closes it with a key, then with a click.
    for (int round = 0; round < 2; round++) {
        const std::string title = round == 0 ? "Okay Box Key" : "Okay Box Click";
        auto fut = std::async(std::launch::async, [&] {
            return b.ShowErrorBox(title, "Something went wrong.\nSecond line with a longer text so that it wraps maybe, caf\xC3\xA9 \xE4\xBD\xA0.", &win);
        });
        std::string id;
        for (int i = 0; i < 100 && id.empty(); i++) { SleepMs(50); id = Sh("xdotool search --name '" + title + "' 2>/dev/null | head -1"); }
        CHECK(!id.empty());
        CHECK(fut.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout);   // modal: still open
        if (!id.empty()) {
            SleepMs(300);   // let it draw and take the focus
            if (round == 0) {
                std::system("xdotool key Return >/dev/null 2>&1");
            } else {
                int w = 0, h = 0;
                std::sscanf(Sh("xdotool getwindowgeometry " + id + " | grep Geometry | sed 's/.*: //'").c_str(), "%dx%d", &w, &h);
                CHECK(w > 50 && h > 50);
                const std::string cmd = "xdotool mousemove --window " + id + " " + std::to_string(w / 2) + " " + std::to_string(h - 26) + " click 1 >/dev/null 2>&1";
                std::system(cmd.c_str());
            }
        }
        CHECK(fut.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
        if (fut.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) CHECK(fut.get());
        else { std::system("xdotool key Escape >/dev/null 2>&1"); fut.wait(); }
        CHECK(Sh("xdotool search --name '" + title + "' 2>/dev/null | head -1").empty());   // gone
    }
}

} // namespace

int main() {
    RUN_SUITE("platform_x11_server");
    if (!std::getenv("DISPLAY")) { std::cout << "SKIP: no $DISPLAY (run through tests/run_under_xvfb.sh)\n"; return 77; }
    if (std::system("command -v xdotool >/dev/null 2>&1") != 0) { std::cout << "SKIP: xdotool not installed\n"; return 77; }
    if (!IsCompiledIn(BackendKind::Own)) { std::cout << "SKIP: own backend not compiled in\n"; return 77; }
    std::unique_ptr<Backend> own = okay::platform::CreateOwnBackend();
    if (!own->Init(InitFlags{true, false, false})) { std::cout << "SKIP: cannot open the display: " << own->LastError() << "\n"; return 77; }
    WindowDesc d; d.title = "Okay X11 Server Test"; d.x = 100; d.y = 80; d.width = 320; d.height = 240;
    auto win = own->MakeWindow(d);
    CHECK(win != nullptr);
    if (!win) return 1;
    win->Raise();
    Pump(*own, 200);
    Source src;
    CHECK(src.Open());
    if (src.d) TestDnd(*own, *win, src);
    TestCursorAndAttention(*own, *win);
    TestMessageBox(*own, *win);
    TestMonitors(*own, *win);   // last: it redefines the server's monitors
    win.reset();
    own->Shutdown();
    TEST_MAIN_RESULT();
}
