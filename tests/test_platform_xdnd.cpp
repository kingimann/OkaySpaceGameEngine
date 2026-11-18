// File drops on the own X11 backend (XDND) and the URI list parser. The parser tests need nothing. The drop test starts as a
// second X client (this program, on its own Display connection) that plays the drag source from the XDND protocol text:
// it owns XdndSelection, sends XdndEnter / XdndPosition / XdndDrop to the backend's window, answers the selection request and
// reads XdndStatus / XdndFinished. Run under Xvfb by tests/run_under_xvfb.sh; exit 77 = skipped (no display / backend).
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"
#include "Platform/UriList.hpp"
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#if defined(OKAY_TEST_HAVE_X11)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#endif

using namespace okay::platform;

static void TestParser() {
    std::string o;
    CHECK(PercentDecode("a%20b%2Fc", o) && o == "a b/c");
    CHECK(PercentDecode("100%", o) && o == "100%");        // a lone % stays
    CHECK(PercentDecode("%zz%4", o) && o == "%zz%4");
    CHECK(PercentDecode("%C3%A9", o) && o == "\xC3\xA9");
    CHECK(!PercentDecode("a%00b", o) && o.empty());        // no NUL in a path

    auto L = [](const char* s) { return UriListToPaths(s); };
    CHECK(L("file:///tmp/a.png\r\n") == std::vector<std::string>({"/tmp/a.png"}));
    CHECK(L("file:///tmp/a%20b.png\r\nfile:///tmp/c.png") == std::vector<std::string>({"/tmp/a b.png", "/tmp/c.png"}));
    CHECK(L("# a comment\r\n\r\nfile://localhost/x/y\r\n") == std::vector<std::string>({"/x/y"}));
    CHECK(L("FILE:///upper\n") == std::vector<std::string>({"/upper"}));                    // scheme is case-insensitive, bare LF accepted
    CHECK(L("file://otherhost/x\r\n").empty());                                            // a file on another machine
    CHECK(L("http://example.com/a.png\r\n").empty());
    CHECK(L("file://\r\n").empty() && L("file:\r\n").empty() && L("").empty() && L("\r\n\r\n").empty());
    CHECK(L("file:///a%00b\r\n").empty());
    CHECK(L("file:///dir/with%0Anewline\r\n") == std::vector<std::string>({"/dir/with\nnewline"}));   // an encoded newline stays inside the name
    CHECK(L("file:///caf%C3%A9.txt   \r\n") == std::vector<std::string>({"/caf\xC3\xA9.txt"}));         // trailing blanks trimmed
    std::string big;
    for (int i = 0; i < 1000; ++i) big += "file:///f" + std::to_string(i) + "\r\n";
    CHECK(L(big.c_str()).size() == 1000);
}

#if defined(OKAY_TEST_HAVE_X11)
static void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

struct Source {
    Display* d = nullptr; ::Window w = 0;
    Atom aSel, aUri, aEnter, aPos, aStatus, aDrop, aFinished, aLeave, aCopy, aTypeList, aProp;
    std::string payload;
    bool gotStatus = false, statusAccept = false, gotFinished = false, finishedOk = false;
    long statusAction = 0;
    void Init() {
        d = XOpenDisplay(nullptr);
        w = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 10, 10, 0, 0, 0);
        aSel = XInternAtom(d, "XdndSelection", False); aUri = XInternAtom(d, "text/uri-list", False);
        aEnter = XInternAtom(d, "XdndEnter", False); aPos = XInternAtom(d, "XdndPosition", False);
        aStatus = XInternAtom(d, "XdndStatus", False); aDrop = XInternAtom(d, "XdndDrop", False);
        aFinished = XInternAtom(d, "XdndFinished", False); aLeave = XInternAtom(d, "XdndLeave", False);
        aCopy = XInternAtom(d, "XdndActionCopy", False); aTypeList = XInternAtom(d, "XdndTypeList", False);
        aProp = XInternAtom(d, "OKAY_SRC", False);
        XSetSelectionOwner(d, aSel, w, CurrentTime);
        XFlush(d);
    }
    void Msg(::Window to, Atom type, long a, long b, long c, long dd, long e) {
        XEvent ev; std::memset(&ev, 0, sizeof(ev));
        ev.xclient.type = ClientMessage; ev.xclient.display = d; ev.xclient.window = to; ev.xclient.message_type = type; ev.xclient.format = 32;
        ev.xclient.data.l[0] = a; ev.xclient.data.l[1] = b; ev.xclient.data.l[2] = c; ev.xclient.data.l[3] = dd; ev.xclient.data.l[4] = e;
        XSendEvent(d, to, False, NoEventMask, &ev);
        XFlush(d);
    }
    // Serve whatever the target sent us.
    void Service() {
        while (XPending(d)) {
            XEvent ev; XNextEvent(d, &ev);
            if (ev.type == SelectionRequest) {
                XSelectionRequestEvent& rq = ev.xselectionrequest;
                XEvent r; std::memset(&r, 0, sizeof(r));
                r.xselection.type = SelectionNotify; r.xselection.display = d; r.xselection.requestor = rq.requestor;
                r.xselection.selection = rq.selection; r.xselection.target = rq.target; r.xselection.time = rq.time;
                if (rq.selection == aSel && rq.target == aUri) {
                    XChangeProperty(d, rq.requestor, rq.property, aUri, 8, PropModeReplace, (const unsigned char*)payload.data(), (int)payload.size());
                    r.xselection.property = rq.property;
                } else r.xselection.property = None;
                XSendEvent(d, rq.requestor, False, NoEventMask, &r); XFlush(d);
            } else if (ev.type == ClientMessage) {
                if (ev.xclient.message_type == aStatus) { gotStatus = true; statusAccept = (ev.xclient.data.l[1] & 1) != 0; statusAction = ev.xclient.data.l[4]; }
                if (ev.xclient.message_type == aFinished) { gotFinished = true; finishedOk = (ev.xclient.data.l[1] & 1) != 0; }
            }
        }
    }
};

// Run `step` until it is true, pumping both sides, for at most `ms`.
template <class F> static bool Until(Backend& b, Source& s, std::vector<Event>& evs, int ms, F cond) {
    for (int t = 0; t < ms; t += 5) {
        s.Service();
        Event e; while (b.PollEvent(e)) evs.push_back(e);
        if (cond()) return true;
        SleepMs(5);
    }
    return cond();
}

static int TestDrop(Backend& b) {
    WindowDesc wd; wd.width = 200; wd.height = 150; wd.title = "xdnd";
    auto win = b.MakeWindow(wd);
    CHECK(win != nullptr);
    if (!win) return 0;
    const ::Window target = (::Window)win->Id();
    Display* chk = XOpenDisplay(nullptr);
    // The window says it takes drops (XdndAware, version 5).
    Atom aware = XInternAtom(chk, "XdndAware", False), type; int fmt; unsigned long n, rest; unsigned char* data = nullptr;
    CHECK(XGetWindowProperty(chk, target, aware, 0, 1, False, XA_ATOM, &type, &fmt, &n, &rest, &data) == Success && data && n == 1 && ((long*)data)[0] == 5);
    if (data) XFree(data);
    XCloseDisplay(chk);

    Source s; s.Init();
    std::vector<Event> evs;
    // 1. three types in XdndEnter, two files.
    s.payload = "file:///tmp/one.png\r\nfile:///tmp/two%20words.okayscene\r\n";
    s.Msg(target, s.aEnter, (long)s.w, 5L << 24, (long)s.aUri, 0, 0);
    s.Msg(target, s.aPos, (long)s.w, 0, (50L << 16) | 40, 0, (long)s.aCopy);
    CHECK(Until(b, s, evs, 2000, [&] { return s.gotStatus; }));
    CHECK(s.statusAccept && s.statusAction == (long)s.aCopy);
    s.Msg(target, s.aDrop, (long)s.w, 0, 0, 0, 0);
    CHECK(Until(b, s, evs, 3000, [&] { return s.gotFinished; }));
    CHECK(s.finishedOk);
    int drops = 0;
    std::vector<std::string> paths;
    for (const Event& e : evs) if (e.type == EventType::DropFile) { ++drops; paths.push_back(e.text); CHECK(e.windowId == win->Id()); }
    CHECK(drops == 2);
    if (drops == 2) CHECK(paths[0] == "/tmp/one.png" && paths[1] == "/tmp/two words.okayscene");

    // 2. more than three types: the list is in the source's XdndTypeList property.
    evs.clear(); s.gotStatus = s.gotFinished = false;
    Atom list[4] = {XInternAtom(s.d, "text/plain", False), XInternAtom(s.d, "text/html", False), XInternAtom(s.d, "image/png", False), s.aUri};
    XChangeProperty(s.d, s.w, s.aTypeList, XA_ATOM, 32, PropModeReplace, (const unsigned char*)list, 4);
    XFlush(s.d);
    s.payload = "file:///tmp/three.png\r\n";
    s.Msg(target, s.aEnter, (long)s.w, (5L << 24) | 1, 0, 0, 0);
    s.Msg(target, s.aPos, (long)s.w, 0, 0, 0, (long)s.aCopy);
    s.Msg(target, s.aDrop, (long)s.w, 0, 0, 0, 0);
    CHECK(Until(b, s, evs, 3000, [&] { return s.gotFinished; }));
    CHECK(s.statusAccept && s.finishedOk);
    drops = 0; for (const Event& e : evs) if (e.type == EventType::DropFile) { ++drops; CHECK(e.text == "/tmp/three.png"); }
    CHECK(drops == 1);

    // 3. a source that offers no file list: refused, no event, and a Finished that says it was not accepted.
    evs.clear(); s.gotStatus = s.gotFinished = false;
    s.Msg(target, s.aEnter, (long)s.w, 5L << 24, (long)XInternAtom(s.d, "text/plain", False), 0, 0);
    s.Msg(target, s.aPos, (long)s.w, 0, 0, 0, (long)s.aCopy);
    s.Msg(target, s.aDrop, (long)s.w, 0, 0, 0, 0);
    CHECK(Until(b, s, evs, 2000, [&] { return s.gotFinished; }));
    CHECK(!s.statusAccept && !s.finishedOk);
    for (const Event& e : evs) CHECK(e.type != EventType::DropFile);

    // 4. leave cancels; a drop after it does nothing.
    evs.clear(); s.gotStatus = s.gotFinished = false;
    s.Msg(target, s.aEnter, (long)s.w, 5L << 24, (long)s.aUri, 0, 0);
    s.Msg(target, s.aLeave, (long)s.w, 0, 0, 0, 0);
    s.Msg(target, s.aDrop, (long)s.w, 0, 0, 0, 0);
    Until(b, s, evs, 300, [&] { return s.gotFinished; });
    CHECK(!s.gotFinished);
    for (const Event& e : evs) CHECK(e.type != EventType::DropFile);

    // 5. a payload with no usable file (remote host, other scheme): Finished says not accepted.
    evs.clear(); s.gotStatus = s.gotFinished = false;
    s.payload = "http://example.com/x.png\r\nfile://elsewhere/y\r\n";
    s.Msg(target, s.aEnter, (long)s.w, 5L << 24, (long)s.aUri, 0, 0);
    s.Msg(target, s.aPos, (long)s.w, 0, 0, 0, (long)s.aCopy);
    s.Msg(target, s.aDrop, (long)s.w, 0, 0, 0, 0);
    CHECK(Until(b, s, evs, 3000, [&] { return s.gotFinished; }));
    CHECK(!s.finishedOk);
    for (const Event& e : evs) CHECK(e.type != EventType::DropFile);
    XCloseDisplay(s.d);
    return 1;
}
#endif

int main() {
    TestParser();
#if defined(OKAY_TEST_HAVE_X11)
    if (IsCompiledIn(BackendKind::Own) && std::getenv("DISPLAY")) {
        Backend* b = Get(BackendKind::Own);
        if (b && b->Init(InitFlags())) TestDrop(*b);
        else std::cout << "[skip] no X display for the drop test\n";
    } else std::cout << "[skip] drop test needs the own backend and a display\n";
#endif
    TEST_MAIN_RESULT();
}
