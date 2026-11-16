// Helper for test_platform_state: talks to the X server directly so the test can play the window manager.
// Xvfb has no window manager, so nothing ever minimises a window there; a real WM sets the properties
// _NET_WM_STATE (= _NET_WM_STATE_HIDDEN) and WM_STATE (= IconicState) on the window. The test sets them itself
// with these functions, then checks that okay::platform reports "minimised".
//
// Kept in its own translation unit: the X11 headers define global names (Window, None, Status, Bool, ...) that
// clash with okay::platform. libX11 is opened with dlopen, so the test links no X library. Written from the
// Xlib manual (XOpenDisplay, XInternAtom, XChangeProperty) and the ICCCM / EWMH property names.
#include "platform_fakewm.hpp"

#if defined(__linux__) && defined(__has_include)
#if __has_include(<X11/Xlib.h>)
#define OKAY_FAKEWM_XLIB 1
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <dlfcn.h>
#if defined(OKAY_PLATFORM_HAVE_SDL2) && __has_include(<SDL.h>) && __has_include(<SDL_syswm.h>)
#include <SDL.h>
#include <SDL_syswm.h>
#define OKAY_FAKEWM_SDL 1
#endif
#endif
#endif

namespace fakewm {

#ifdef OKAY_FAKEWM_XLIB
namespace {

struct Conn {
    void* lib = nullptr;
    Display* dpy = nullptr;
    Display* (*open)(const char*) = nullptr;
    int (*close)(Display*) = nullptr;
    Atom (*intern)(Display*, const char*, int) = nullptr;
    int (*change)(Display*, ::Window, Atom, Atom, int, int, const unsigned char*, int) = nullptr;
    int (*sync)(Display*, int) = nullptr;
    XWMHints* (*getHints)(Display*, ::Window) = nullptr;
    int (*xfree)(void*) = nullptr;
    Conn() {
        lib = dlopen("libX11.so.6", RTLD_NOW);
        if (!lib) return;
        open = reinterpret_cast<decltype(open)>(dlsym(lib, "XOpenDisplay"));
        close = reinterpret_cast<decltype(close)>(dlsym(lib, "XCloseDisplay"));
        intern = reinterpret_cast<decltype(intern)>(dlsym(lib, "XInternAtom"));
        change = reinterpret_cast<decltype(change)>(dlsym(lib, "XChangeProperty"));
        sync = reinterpret_cast<decltype(sync)>(dlsym(lib, "XSync"));
        getHints = reinterpret_cast<decltype(getHints)>(dlsym(lib, "XGetWMHints"));
        xfree = reinterpret_cast<decltype(xfree)>(dlsym(lib, "XFree"));
        if (open && close && intern && change && sync) dpy = open(nullptr);
    }
    ~Conn() { if (dpy) close(dpy); }
};

Conn& C() { static Conn c; return c; }

} // namespace
#endif

bool Available() {
#ifdef OKAY_FAKEWM_XLIB
    return C().dpy != nullptr;
#else
    return false;
#endif
}

bool SetNetWmState(unsigned long win, const char* const* states, int n) {
#ifdef OKAY_FAKEWM_XLIB
    Conn& c = C();
    if (!c.dpy || !win) return false;
    Atom atoms[8];
    if (n < 0 || n > 8) return false;
    for (int i = 0; i < n; ++i) atoms[i] = c.intern(c.dpy, states[i], False);
    c.change(c.dpy, (::Window)win, c.intern(c.dpy, "_NET_WM_STATE", False), XA_ATOM, 32, PropModeReplace,
             reinterpret_cast<const unsigned char*>(atoms), n);
    c.sync(c.dpy, False);
    return true;
#else
    (void)win; (void)states; (void)n;
    return false;
#endif
}

bool SetWmState(unsigned long win, long state) {
#ifdef OKAY_FAKEWM_XLIB
    Conn& c = C();
    if (!c.dpy || !win) return false;
    const Atom prop = c.intern(c.dpy, "WM_STATE", False);   // the property and its type have the same name (ICCCM 4.1.3.1)
    long data[2] = {state, 0};                             // state, icon window
    c.change(c.dpy, (::Window)win, prop, prop, 32, PropModeReplace, reinterpret_cast<const unsigned char*>(data), 2);
    c.sync(c.dpy, False);
    return true;
#else
    (void)win; (void)state;
    return false;
#endif
}

int UrgencyHint(unsigned long win) {
#ifdef OKAY_FAKEWM_XLIB
    Conn& c = C();
    if (!c.dpy || !win || !c.getHints || !c.xfree) return -1;
    c.sync(c.dpy, False);
    XWMHints* h = c.getHints(c.dpy, (::Window)win);
    if (!h) return 0;   // no WM_HINTS at all: not urgent
    const int urgent = (h->flags & XUrgencyHint) ? 1 : 0;
    c.xfree(h);
    return urgent;
#else
    (void)win;
    return -1;
#endif
}

unsigned long X11WindowOfSdl(void* sdlWindow) {
#ifdef OKAY_FAKEWM_SDL
    if (!sdlWindow) return 0;
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(static_cast<SDL_Window*>(sdlWindow), &info)) return 0;
    if (info.subsystem != SDL_SYSWM_X11) return 0;
    return (unsigned long)info.info.x11.window;
#else
    (void)sdlWindow;
    return 0;
#endif
}

} // namespace fakewm
