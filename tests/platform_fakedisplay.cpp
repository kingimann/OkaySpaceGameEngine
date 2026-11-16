// Helper for test_platform_display: changes what the X server reports about its monitors, so the test can check what
// okay::platform::Displays() reads. Xvfb has one monitor, no desktop and no Xft settings; a real desktop has several
// monitors (RandR 1.5 monitors), a work area (_NET_WORKAREA, EWMH) and an Xft.dpi resource. This file sets those itself.
//
// Kept in its own translation unit (the X11 headers define global names that clash with okay::platform). libX11 and
// libXrandr are opened with dlopen, so the test links no X library. Written from the Xlib manual (XChangeProperty,
// XDeleteProperty), the RandR 1.5 protocol / library documentation (XRRGetMonitors, XRRSetMonitor, XRRDeleteMonitor,
// XRRAllocateMonitor) and the EWMH property names.
#include "platform_fakedisplay.hpp"

#if defined(__linux__) && defined(__has_include)
#if __has_include(<X11/Xlib.h>) && __has_include(<X11/extensions/Xrandr.h>)
#define OKAY_FAKEDISPLAY_XLIB 1
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <dlfcn.h>
#include <cstring>
#endif
#endif

namespace fakedisplay {

#ifdef OKAY_FAKEDISPLAY_XLIB
namespace {

struct Conn {
    Display* dpy = nullptr;
    Display* (*open)(const char*) = nullptr;
    int (*close)(Display*) = nullptr;
    Atom (*intern)(Display*, const char*, int) = nullptr;
    int (*change)(Display*, ::Window, Atom, Atom, int, int, const unsigned char*, int) = nullptr;
    int (*del)(Display*, ::Window, Atom) = nullptr;
    int (*sync)(Display*, int) = nullptr;
    Status (*rrVersion)(Display*, int*, int*) = nullptr;
    XRRMonitorInfo* (*rrGet)(Display*, ::Window, Bool, int*) = nullptr;
    void (*rrFree)(XRRMonitorInfo*) = nullptr;
    XRRMonitorInfo* (*rrAlloc)(Display*, int) = nullptr;
    void (*rrSet)(Display*, ::Window, XRRMonitorInfo*) = nullptr;
    void (*rrDelete)(Display*, ::Window, Atom) = nullptr;
    Conn() {
        void* x = dlopen("libX11.so.6", RTLD_NOW | RTLD_GLOBAL);
        if (!x) return;
        open = reinterpret_cast<decltype(open)>(dlsym(x, "XOpenDisplay"));
        close = reinterpret_cast<decltype(close)>(dlsym(x, "XCloseDisplay"));
        intern = reinterpret_cast<decltype(intern)>(dlsym(x, "XInternAtom"));
        change = reinterpret_cast<decltype(change)>(dlsym(x, "XChangeProperty"));
        del = reinterpret_cast<decltype(del)>(dlsym(x, "XDeleteProperty"));
        sync = reinterpret_cast<decltype(sync)>(dlsym(x, "XSync"));
        if (void* r = dlopen("libXrandr.so.2", RTLD_NOW)) {
            rrVersion = reinterpret_cast<decltype(rrVersion)>(dlsym(r, "XRRQueryVersion"));
            rrGet = reinterpret_cast<decltype(rrGet)>(dlsym(r, "XRRGetMonitors"));
            rrFree = reinterpret_cast<decltype(rrFree)>(dlsym(r, "XRRFreeMonitors"));
            rrAlloc = reinterpret_cast<decltype(rrAlloc)>(dlsym(r, "XRRAllocateMonitor"));
            rrSet = reinterpret_cast<decltype(rrSet)>(dlsym(r, "XRRSetMonitor"));
            rrDelete = reinterpret_cast<decltype(rrDelete)>(dlsym(r, "XRRDeleteMonitor"));
        }
        if (open && close && intern && change && del && sync) dpy = open(nullptr);
    }
    ~Conn() { if (dpy) close(dpy); }
    ::Window Root() const { return DefaultRootWindow(dpy); }
    bool HasRandr15() {
        int maj = 0, min = 0;
        return rrVersion && rrGet && rrFree && rrAlloc && rrSet && rrDelete && rrVersion(dpy, &maj, &min) &&
               (maj > 1 || (maj == 1 && min >= 5));
    }
};

Conn& C() { static Conn c; return c; }

} // namespace
#endif

bool Available() {
#ifdef OKAY_FAKEDISPLAY_XLIB
    return C().dpy != nullptr;
#else
    return false;
#endif
}

bool SetWorkArea(int x, int y, int w, int h) {
#ifdef OKAY_FAKEDISPLAY_XLIB
    Conn& c = C();
    if (!c.dpy) return false;
    const Atom a = c.intern(c.dpy, "_NET_WORKAREA", False);
    if (w <= 0) c.del(c.dpy, c.Root(), a);
    else {
        const long v[4] = {x, y, w, h};   // format 32 data is passed as longs (Xlib manual, XChangeProperty)
        c.change(c.dpy, c.Root(), a, XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<const unsigned char*>(v), 4);
    }
    c.sync(c.dpy, False);
    return true;
#else
    (void)x; (void)y; (void)w; (void)h; return false;
#endif
}

bool SetResources(const char* text) {
#ifdef OKAY_FAKEDISPLAY_XLIB
    Conn& c = C();
    if (!c.dpy) return false;
    const Atom a = c.intern(c.dpy, "RESOURCE_MANAGER", False);
    if (!text) c.del(c.dpy, c.Root(), a);
    else c.change(c.dpy, c.Root(), a, XA_STRING, 8, PropModeReplace, reinterpret_cast<const unsigned char*>(text), (int)std::strlen(text));
    c.sync(c.dpy, False);
    return true;
#else
    (void)text; return false;
#endif
}

bool SplitIntoTwoMonitors(int splitX, bool rightPrimary) {
#ifdef OKAY_FAKEDISPLAY_XLIB
    Conn& c = C();
    if (!c.dpy || !c.HasRandr15()) return false;
    int n = 0;
    XRRMonitorInfo* cur = c.rrGet(c.dpy, c.Root(), True, &n);
    if (!cur || n < 1 || cur[0].noutput < 1) { if (cur) c.rrFree(cur); return false; }
    RROutput out = cur[0].outputs[0];
    const int h = cur[0].height, w = cur[0].width;
    c.rrFree(cur);
    XRRMonitorInfo* m = c.rrAlloc(c.dpy, 1);
    if (!m) return false;
    m->name = c.intern(c.dpy, "OKAY-LEFT", False);
    m->primary = rightPrimary ? False : True; m->automatic = False;
    m->x = 0; m->y = 0; m->width = splitX; m->height = h; m->mwidth = splitX / 4; m->mheight = h / 4;
    m->noutput = 1; m->outputs[0] = out;
    c.rrSet(c.dpy, c.Root(), m);
    m->name = c.intern(c.dpy, "OKAY-RIGHT", False);
    m->primary = rightPrimary ? True : False;
    m->x = splitX; m->width = w - splitX; m->mwidth = (w - splitX) / 4;
    m->noutput = 0;
    c.rrSet(c.dpy, c.Root(), m);
    c.rrFree(m);
    c.sync(c.dpy, False);
    return true;
#else
    (void)splitX; (void)rightPrimary; return false;
#endif
}

bool RemoveMonitors() {
#ifdef OKAY_FAKEDISPLAY_XLIB
    Conn& c = C();
    if (!c.dpy || !c.HasRandr15()) return false;
    c.rrDelete(c.dpy, c.Root(), c.intern(c.dpy, "OKAY-LEFT", False));
    c.rrDelete(c.dpy, c.Root(), c.intern(c.dpy, "OKAY-RIGHT", False));
    c.sync(c.dpy, False);
    return true;
#else
    return false;
#endif
}

} // namespace fakedisplay
