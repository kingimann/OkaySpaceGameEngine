// The okay::platform calls added for the editor's stage E3 (docs/sdl_removal_plan.md: the editor's main window and events with no
// SDL), and the two small editor-side helpers built on okay::platform:
//
//   test_platform_editor_calls unit   no display: text/uri-list parsing (own::PathsFromUriList), the game display settings through
//                                     okay::platform (shared/PlatformDisplays.hpp: the monitor list, and each window mode on a
//                                     fake window), the Play clipboard (shared/PlatformClipboard.hpp) on a fake backend,
//                                     Backend::TouchDeviceCount's default
//   test_platform_editor_calls own    our X11 backend under Xvfb (tests/run_under_xvfb.sh):
//                                     * file drops (XDND, version 5): this test plays the drag source on its own X connection
//                                       (libX11 by dlopen) and drops two files on one of our windows, then a drag with no
//                                       file in it; checks the DropFile events, XdndStatus and XdndFinished;
//                                     * Window::RefreshRateHz against RandR's own current rate (XRRConfigCurrentRate, a
//                                       different request from the CRTC mode timings our code reads);
//                                     * Backend::ShowErrorBox: a child process shows the box, the test finds the window by
//                                       its title and closes it, once with Return and once with the window's close button
//                                       (WM_DELETE_WINDOW); the box must return true each time;
//                                     * TouchDeviceCount is 0 (our X11 code delivers no finger events).
//
// Exit code 77 = skipped (no display, backend not compiled in, no X11 headers). Written for this repository from the XDND
// specification and the Xlib manual; no code from elsewhere.
#include "test_framework.hpp"
#include "okay/Platform/Platform.hpp"
#include "PlatformClipboard.hpp"
#include "PlatformDisplays.hpp"
#if defined(__linux__)
#include "Platform/OwnLinux.hpp"
#include <unistd.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#if defined(__linux__) && __has_include(<X11/Xlib.h>)
#define OKAY_TEST_X11 1
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

using namespace okay::platform;

namespace {

// ---- a fake window and backend for the display and clipboard helpers ---------------------------------------------------
struct FakeWindow final : okay::platform::Window {
    std::vector<std::string> calls;
    uint32_t Id() const override { return 7; }
    void* NativeHandle() const override { return nullptr; }
    void GetSize(int& w, int& h) const override { w = 800; h = 600; }
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
    void SetFullscreen(bool on) override { calls.push_back(on ? "full on" : "full off"); }
    bool IsFocused() const override { return true; }
    bool CreateGLContext(const GLAttributes&) override { return false; }
    void DestroyGLContext() override {}
    bool MakeCurrent() override { return false; }
    void SwapBuffers() override {}
    bool SetSwapInterval(int) override { return false; }
    void GetDrawableSize(int& w, int& h) const override { GetSize(w, h); }
};

struct FakeBackend final : Backend {
    std::string clip;
    BackendKind Kind() const override { return BackendKind::Own; }
    bool Init(const InitFlags&) override { return true; }
    void Shutdown() override {}
    std::string LastError() const override { return ""; }
    uint64_t TicksMs() override { return 0; }
    uint64_t PerfCounter() override { return 0; }
    uint64_t PerfFrequency() override { return 1; }
    void DelayMs(uint32_t) override {}
    std::unique_ptr<okay::platform::Window> MakeWindow(const WindowDesc&) override { return nullptr; }
    bool PollEvent(Event&) override { return false; }
    void* GLProcAddress(const char*) override { return nullptr; }
    void ReleaseCurrent() override {}
    void StartTextInput() override {}
    void StopTextInput() override {}
    bool IsKeyDown(Key) override { return false; }
    uint32_t MouseState(int*, int*) override { return 0; }
    bool SetRelativeMouse(bool) override { return false; }
    void ShowCursor(bool) override {}
    void WarpMouse(okay::platform::Window*, int, int) override {}
    bool SetClipboardText(const std::string& t) override { clip = t; return true; }
    std::string GetClipboardText() override { return clip; }
    std::unique_ptr<AudioDevice> OpenAudio(const AudioSpec&, bool, const char*, AudioSpec*) override { return nullptr; }
    int AudioDeviceCount(bool) override { return 0; }
    std::string AudioDeviceName(int, bool) override { return ""; }
    int GamepadDeviceCount() override { return 0; }
    bool IsGamepad(int) override { return false; }
    std::unique_ptr<Gamepad> OpenGamepad(int) override { return nullptr; }
};

int Unit() {
#if defined(__linux__)
    RUN_SUITE("text/uri-list -> local paths (own::PathsFromUriList)");
    {
        std::vector<std::string> p = own::PathsFromUriList(
            "# a comment line\r\nfile:///home/me/a%20b.png\r\nfile://localhost/tmp/x.okayscene\r\n"
            "http://example.com/not-a-file\r\nfile://elsewhere.invalid/tmp/remote.txt\r\nfile:/tmp/short-form.wav\n"
            "file:///tmp/%C3%A9t%C3%A9.txt\r\nfile:///tmp/bad%2xescape\r\n\r\n");
        for (const std::string& s : p) std::printf("    '%s'\n", s.c_str());
        CHECK(p.size() == 5);
        if (p.size() == 5) {
            CHECK(p[0] == "/home/me/a b.png");
            CHECK(p[1] == "/tmp/x.okayscene");
            CHECK(p[2] == "/tmp/short-form.wav");
            CHECK(p[3] == "/tmp/\xC3\xA9t\xC3\xA9.txt");   // UTF-8 bytes, percent-encoded on the wire
            CHECK(p[4] == "/tmp/bad%2xescape");               // not a valid escape: kept as it is
        }
        CHECK(own::PathsFromUriList("").empty());
        CHECK(own::PathsFromUriList("file:relative/path\r\n").empty());
        char host[256] = {0};
        if (gethostname(host, sizeof(host) - 1) == 0 && host[0]) {
            const std::vector<std::string> mine = own::PathsFromUriList(std::string("file://") + host + "/tmp/mine.txt");
            CHECK(mine.size() == 1 && mine[0] == "/tmp/mine.txt");   // this machine's own name is local
        }
    }
#endif
    RUN_SUITE("game display settings through okay::platform (shared/PlatformDisplays.hpp)");
    {
        std::vector<DisplayInfo> ds(2);
        ds[0].name = "DP-1"; ds[0].bounds = DisplayRect{0, 0, 1920, 1080}; ds[0].primary = true;
        ds[1].name = ""; ds[1].bounds = DisplayRect{1920, 0, 1280, 1024};
        const std::vector<okay::DisplayMonitor> ms = okay::platdisplay::MonitorsFrom(ds);
        CHECK(ms.size() == 2);
        if (ms.size() == 2) {
            CHECK(ms[0].index == 0 && ms[0].name == "DP-1" && ms[0].width == 1920 && ms[0].height == 1080);
            CHECK(ms[1].index == 1 && ms[1].name == "Display 2" && ms[1].x == 1920 && ms[1].width == 1280);
            CHECK(ms[0].modes.size() == 1 && ms[0].modes[0].width == 1920 && ms[0].modes[0].refreshHz == 0);   // one mode: the desktop
        }
        FakeWindow w;
        okay::DisplayApply a;
        a.mode = okay::WindowMode::Windowed; a.hasMonitor = true; a.originX = 1920; a.originY = 0;
        a.desktopWidth = 1280; a.desktopHeight = 1024; a.hasSize = true; a.width = 800; a.height = 600;
        CHECK(okay::platdisplay::Apply(w, a));
        CHECK(w.calls.size() == 3 && w.calls[0] == "full off" && w.calls[1] == "pos 2160,212" && w.calls[2] == "size 800x600");
        w.calls.clear();
        a.mode = okay::WindowMode::Borderless;
        okay::platdisplay::Apply(w, a);
        CHECK(w.calls.size() == 3 && w.calls[1] == "pos 1920,0" && w.calls[2] == "full on");
        w.calls.clear();
        a.mode = okay::WindowMode::Fullscreen; a.hasMonitor = false;
        okay::platdisplay::Apply(w, a);
        CHECK(w.calls.size() == 2 && w.calls[0] == "full off" && w.calls[1] == "full on");   // no mode switch: desktop fullscreen
    }
    RUN_SUITE("Play clipboard through okay::platform (shared/PlatformClipboard.hpp), TouchDeviceCount default");
    {
        FakeBackend b;
        okay::PlatformClipboard none;
        CHECK(none.GetText().empty() && !none.SetText("x"));   // no backend: nothing, no crash
        okay::PlatformClipboard c(&b);
        CHECK(c.SetText("copied \xE2\x82\xAC"));
        CHECK(b.clip == "copied \xE2\x82\xAC");
        b.clip = "from the system";
        CHECK(c.GetText() == "from the system");
        CHECK(b.TouchDeviceCount() == 0);
    }
    TEST_MAIN_RESULT();
}

#if defined(OKAY_TEST_X11)
// ---- the other X client: libX11 by dlopen (no X library is linked into the test) -------------------------------------
struct X {
    void* lib = nullptr;
    Display* (*OpenDisplay)(const char*) = nullptr;
    int (*CloseDisplay)(Display*) = nullptr;
    Atom (*InternAtom)(Display*, const char*, int) = nullptr;
    ::Window (*CreateSimpleWindow)(Display*, ::Window, int, int, unsigned, unsigned, unsigned, unsigned long, unsigned long) = nullptr;
    int (*DestroyWindow)(Display*, ::Window) = nullptr;
    int (*SetSelectionOwner)(Display*, Atom, ::Window, Time) = nullptr;
    Status (*SendEvent)(Display*, ::Window, int, long, XEvent*) = nullptr;
    int (*Flush)(Display*) = nullptr;
    int (*Sync)(Display*, int) = nullptr;
    int (*Pending)(Display*) = nullptr;
    int (*NextEvent)(Display*, XEvent*) = nullptr;
    int (*ChangeProperty)(Display*, ::Window, Atom, Atom, int, int, const unsigned char*, int) = nullptr;
    Status (*QueryTree)(Display*, ::Window, ::Window*, ::Window*, ::Window**, unsigned*) = nullptr;
    Status (*FetchName)(Display*, ::Window, char**) = nullptr;
    int (*Free)(void*) = nullptr;
    KeyCode (*KeysymToKeycode)(Display*, KeySym) = nullptr;
    Status (*GetWindowAttributes)(Display*, ::Window, XWindowAttributes*) = nullptr;
    bool Load() {
        lib = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return false;
        bool ok = true;
        auto get = [&](auto& fn, const char* name) { fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(lib, name)); ok = ok && fn; };
        get(OpenDisplay, "XOpenDisplay"); get(CloseDisplay, "XCloseDisplay"); get(InternAtom, "XInternAtom");
        get(CreateSimpleWindow, "XCreateSimpleWindow"); get(DestroyWindow, "XDestroyWindow");
        get(SetSelectionOwner, "XSetSelectionOwner"); get(SendEvent, "XSendEvent"); get(Flush, "XFlush"); get(Sync, "XSync");
        get(Pending, "XPending"); get(NextEvent, "XNextEvent"); get(ChangeProperty, "XChangeProperty");
        get(QueryTree, "XQueryTree"); get(FetchName, "XFetchName"); get(Free, "XFree"); get(KeysymToKeycode, "XKeysymToKeycode");
        get(GetWindowAttributes, "XGetWindowAttributes");
        return ok;
    }
};

void Nap(int ms) { timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L}; nanosleep(&ts, nullptr); }

void SendClient(X& x, Display* d, ::Window to, Atom type, long l0, long l1, long l2, long l3, long l4) {
    XEvent e; std::memset(&e, 0, sizeof(e));
    e.xclient.type = ClientMessage;
    e.xclient.window = to;
    e.xclient.message_type = type;
    e.xclient.format = 32;
    e.xclient.data.l[0] = l0; e.xclient.data.l[1] = l1; e.xclient.data.l[2] = l2; e.xclient.data.l[3] = l3; e.xclient.data.l[4] = l4;
    x.SendEvent(d, to, False, NoEventMask, &e);
    x.Flush(d);
}

struct DragResult { bool statusSeen = false, accepted = false, finishedSeen = false, finishedOk = false; std::vector<std::string> drops; };

// One drag from `src` (the test's window) onto `target`, offering `types`; `uriList` is what the source hands over when asked.
DragResult Drag(X& x, Display* d, ::Window src, ::Window target, Backend& b, const std::vector<Atom>& types, const std::string& uriList) {
    DragResult r;
    const Atom aEnter = x.InternAtom(d, "XdndEnter", False), aPos = x.InternAtom(d, "XdndPosition", False),
               aStatus = x.InternAtom(d, "XdndStatus", False), aDrop = x.InternAtom(d, "XdndDrop", False),
               aFinished = x.InternAtom(d, "XdndFinished", False), aSel = x.InternAtom(d, "XdndSelection", False),
               aCopy = x.InternAtom(d, "XdndActionCopy", False);
    x.SetSelectionOwner(d, aSel, src, CurrentTime);
    long t[3] = {0, 0, 0};
    for (size_t i = 0; i < types.size() && i < 3; ++i) t[i] = (long)types[i];
    SendClient(x, d, target, aEnter, (long)src, 5L << 24, t[0], t[1], t[2]);
    SendClient(x, d, target, aPos, (long)src, 0, (40L << 16) | 30L, CurrentTime, (long)aCopy);
    auto pump = [&](int ms, bool untilFinished) {
        for (int i = 0; i < ms / 10; ++i) {
            Event e;
            while (b.PollEvent(e)) if (e.type == EventType::DropFile) r.drops.push_back(e.text);
            while (x.Pending(d)) {
                XEvent ev; x.NextEvent(d, &ev);
                if (ev.type == ClientMessage && ev.xclient.message_type == aStatus) {
                    r.statusSeen = true; r.accepted = (ev.xclient.data.l[1] & 1) != 0 && (Atom)ev.xclient.data.l[4] == aCopy;
                } else if (ev.type == ClientMessage && ev.xclient.message_type == aFinished) {
                    r.finishedSeen = true; r.finishedOk = (ev.xclient.data.l[1] & 1) != 0;
                } else if (ev.type == SelectionRequest && ev.xselectionrequest.selection == aSel) {
                    XSelectionRequestEvent& rq = ev.xselectionrequest;
                    x.ChangeProperty(d, rq.requestor, rq.property, rq.target, 8, PropModeReplace,
                                     (const unsigned char*)uriList.data(), (int)uriList.size());
                    XEvent n; std::memset(&n, 0, sizeof(n));
                    n.xselection.type = SelectionNotify; n.xselection.requestor = rq.requestor; n.xselection.selection = rq.selection;
                    n.xselection.target = rq.target; n.xselection.property = rq.property; n.xselection.time = rq.time;
                    x.SendEvent(d, rq.requestor, False, NoEventMask, &n);
                    x.Flush(d);
                }
            }
            if (untilFinished ? r.finishedSeen : r.statusSeen) { Event e; while (b.PollEvent(e)) if (e.type == EventType::DropFile) r.drops.push_back(e.text); return; }
            Nap(10);
        }
    };
    pump(2000, false);
    SendClient(x, d, target, aDrop, (long)src, 0, CurrentTime, 0, 0);
    pump(3000, true);
    return r;
}

// The first top-level window called `title` (Xvfb has no window manager: our windows are children of the root).
::Window FindByTitle(X& x, Display* d, const char* title) {
    ::Window root = DefaultRootWindow(d), parent = 0, *kids = nullptr;
    unsigned n = 0;
    if (!x.QueryTree(d, root, &root, &parent, &kids, &n)) return 0;
    ::Window found = 0;
    for (unsigned i = 0; i < n && !found; ++i) {
        char* name = nullptr;
        if (x.FetchName(d, kids[i], &name) && name) { if (std::strcmp(name, title) == 0) found = kids[i]; x.Free(name); }
    }
    if (kids) x.Free(kids);
    return found;
}

// A child process shows the box; this one closes it by `how` ("return" or "close"). True when the child said the box worked.
bool MessageBoxRound(X& x, Display* d, const char* how, int& w, int& h) {
    const char* title = "OkaySpace box test";
    const pid_t pid = fork();
    if (pid == 0) {
        Backend* own = Get(BackendKind::Own);
        const bool ok = own && own->ShowErrorBox(title, "The first line of the message.\nAnd a second, longer line of the message text.");
        _exit(ok ? 0 : 3);
    }
    ::Window box = 0;
    for (int i = 0; i < 300 && !box; ++i) { Nap(10); box = FindByTitle(x, d, title); }
    if (!box) { kill(pid, SIGKILL); waitpid(pid, nullptr, 0); std::printf("    no box window appeared\n"); return false; }
    Nap(200);   // mapped and drawn
    XWindowAttributes at;
    if (x.GetWindowAttributes(d, box, &at)) { w = at.width; h = at.height; }
    if (std::strcmp(how, "return") == 0) {
        XEvent k; std::memset(&k, 0, sizeof(k));
        k.xkey.type = KeyPress; k.xkey.window = box; k.xkey.root = DefaultRootWindow(d); k.xkey.same_screen = True;
        k.xkey.keycode = x.KeysymToKeycode(d, XK_Return);
        x.SendEvent(d, box, False, KeyPressMask, &k);
    } else {
        SendClient(x, d, box, x.InternAtom(d, "WM_PROTOCOLS", False), (long)x.InternAtom(d, "WM_DELETE_WINDOW", False), CurrentTime, 0, 0, 0);
    }
    x.Flush(d);
    int status = -1;
    for (int i = 0; i < 300; ++i) {
        const pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) break;
        Nap(10);
        if (i == 299) { kill(pid, SIGKILL); waitpid(pid, &status, 0); std::printf("    the box did not close (%s)\n", how); return false; }
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 && FindByTitle(x, d, title) == 0;
}

int Own() {
    X x;
    if (!x.Load()) { std::printf("SKIP: libX11 not loadable\n"); return 77; }
    Display* d = x.OpenDisplay(nullptr);
    if (!d) { std::printf("SKIP: no X display\n"); return 77; }
    Backend* b = Get(BackendKind::Own);
    if (!b) { std::printf("SKIP: own backend not compiled in\n"); return 77; }
    InitFlags f; f.audio = false; f.gamepad = false;
    if (!b->Init(f)) { std::printf("SKIP: own: %s\n", b->LastError().c_str()); return 77; }
    WindowDesc wd; wd.title = "drop target"; wd.width = 320; wd.height = 200; wd.x = 50; wd.y = 60;
    std::unique_ptr<okay::platform::Window> win = b->MakeWindow(wd);
    CHECK(win != nullptr);
    if (!win) TEST_MAIN_RESULT();
    { Event e; for (int i = 0; i < 20; ++i) { while (b->PollEvent(e)) {} Nap(10); } }
    const ::Window target = (::Window)(uintptr_t)win->NativeHandle();

    RUN_SUITE("own X11: file drops (XDND)");
    {
        const ::Window src = x.CreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 10, 10, 0, 0, 0);
        x.Sync(d, False);
        const Atom uri = x.InternAtom(d, "text/uri-list", False), plain = x.InternAtom(d, "text/plain", False);
        DragResult r = Drag(x, d, src, target, *b, {plain, uri},
                            "file:///tmp/okay%20drop/one.png\r\n# comment\r\nfile://localhost/tmp/two.okayscene\r\n");
        std::printf("    status %d accepted %d finished %d ok %d, %zu drop(s)\n", r.statusSeen, r.accepted, r.finishedSeen, r.finishedOk, r.drops.size());
        for (const std::string& p : r.drops) std::printf("      '%s'\n", p.c_str());
        CHECK(r.statusSeen && r.accepted);
        CHECK(r.finishedSeen && r.finishedOk);
        CHECK(r.drops.size() == 2);
        if (r.drops.size() == 2) { CHECK(r.drops[0] == "/tmp/okay drop/one.png"); CHECK(r.drops[1] == "/tmp/two.okayscene"); }
        // A drag that offers no file list: refused at XdndPosition, and the drop is answered with an empty XdndFinished.
        DragResult n = Drag(x, d, src, target, *b, {plain}, "not used");
        std::printf("    no files: status %d accepted %d finished %d ok %d, %zu drop(s)\n", n.statusSeen, n.accepted, n.finishedSeen, n.finishedOk, n.drops.size());
        CHECK(n.statusSeen && !n.accepted);
        CHECK(n.finishedSeen && !n.finishedOk);
        CHECK(n.drops.empty());
        x.DestroyWindow(d, src);
    }

    RUN_SUITE("own X11: Window::RefreshRateHz against RandR's current rate");
    {
        const int hz = win->RefreshRateHz();
        int rrRate = -1;
        if (void* rr = dlopen("libXrandr.so.2", RTLD_NOW | RTLD_LOCAL)) {
            auto getInfo = reinterpret_cast<void* (*)(Display*, ::Window)>(dlsym(rr, "XRRGetScreenInfo"));
            auto rate = reinterpret_cast<short (*)(void*)>(dlsym(rr, "XRRConfigCurrentRate"));
            auto freeInfo = reinterpret_cast<void (*)(void*)>(dlsym(rr, "XRRFreeScreenConfigInfo"));
            if (getInfo && rate && freeInfo) {
                if (void* info = getInfo(d, DefaultRootWindow(d))) { rrRate = rate(info); freeInfo(info); }
            }
        }
        std::printf("    RefreshRateHz %d, XRRConfigCurrentRate %d\n", hz, rrRate);
        CHECK(hz >= 0 && hz < 1000);
        if (rrRate > 0) CHECK(hz == rrRate);   // RandR 1.0's rate is rounded the same way (whole hertz)
    }

    RUN_SUITE("own X11: Backend::ShowErrorBox (a box on its own X connection)");
    {
        int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
        CHECK(MessageBoxRound(x, d, "return", w1, h1));
        CHECK(MessageBoxRound(x, d, "close", w2, h2));
        std::printf("    box %dx%d\n", w1, h1);
        CHECK(w1 >= 320 && h1 > 60 && w1 == w2 && h1 == h2);
    }

    RUN_SUITE("own X11: TouchDeviceCount");
    CHECK(b->TouchDeviceCount() == 0);

    win.reset();
    x.CloseDisplay(d);
    TEST_MAIN_RESULT();
}
#endif

} // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "unit";
    if (mode == "unit") return Unit();
#if defined(OKAY_TEST_X11)
    if (mode == "own") return Own();
#else
    if (mode == "own") { std::printf("SKIP: no X11 headers\n"); return 77; }
#endif
    std::fprintf(stderr, "usage: test_platform_editor_calls unit|own\n");
    return 2;
}
