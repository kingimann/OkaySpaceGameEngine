// Own Linux backend, windowing part: Xlib + GLX through dlopen (libX11.so.6,
// libGL.so.1 / libGLX.so.0 are opened at run time; nothing is linked). The X11 and
// GL header files only supply types and constants; with them absent (no
// OKAY_PLATFORM_X11_HEADERS) this file is just "no display".
#include "OwnLinux.hpp"

#ifndef OKAY_PLATFORM_X11_HEADERS
namespace okay::platform::own {
std::unique_ptr<X11System> X11System::Open(std::string& err) {
    err = "built without X11 headers; the own backend has no windowing in this build";
    return nullptr;
}
} // namespace okay::platform::own
#else

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <GL/glx.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <unordered_map>

// X11 headers define these as macros; keep our own names unaffected.
#ifdef Always
#undef Always
#endif

namespace okay::platform::own {
namespace {

// ---- function tables ---------------------------------------------------------------
#define OKAY_XLIB_FUNCS(F) \
    F(Display*, XOpenDisplay, (const char*)) \
    F(int, XCloseDisplay, (Display*)) \
    F(int, XPending, (Display*)) \
    F(int, XNextEvent, (Display*, XEvent*)) \
    F(int, XPeekEvent, (Display*, XEvent*)) \
    F(int, XFlush, (Display*)) \
    F(int, XSync, (Display*, int)) \
    F(Atom, XInternAtom, (Display*, const char*, int)) \
    F(Colormap, XCreateColormap, (Display*, ::Window, Visual*, int)) \
    F(int, XFreeColormap, (Display*, Colormap)) \
    F(::Window, XCreateWindow, (Display*, ::Window, int, int, unsigned, unsigned, unsigned, int, unsigned, Visual*, unsigned long, XSetWindowAttributes*)) \
    F(int, XDestroyWindow, (Display*, ::Window)) \
    F(int, XMapWindow, (Display*, ::Window)) \
    F(int, XUnmapWindow, (Display*, ::Window)) \
    F(int, XRaiseWindow, (Display*, ::Window)) \
    F(int, XMoveWindow, (Display*, ::Window, int, int)) \
    F(int, XResizeWindow, (Display*, ::Window, unsigned, unsigned)) \
    F(int, XSetInputFocus, (Display*, ::Window, int, Time)) \
    F(int, XStoreName, (Display*, ::Window, const char*)) \
    F(int, XChangeProperty, (Display*, ::Window, Atom, Atom, int, int, const unsigned char*, int)) \
    F(int, XDeleteProperty, (Display*, ::Window, Atom)) \
    F(int, XGetWindowProperty, (Display*, ::Window, Atom, long, long, int, Atom, Atom*, int*, unsigned long*, unsigned long*, unsigned char**)) \
    F(int, XSetWMProtocols, (Display*, ::Window, Atom*, int)) \
    F(int, XSelectInput, (Display*, ::Window, long)) \
    F(Status, XSendEvent, (Display*, ::Window, int, long, XEvent*)) \
    F(Status, XGetWindowAttributes, (Display*, ::Window, XWindowAttributes*)) \
    F(int, XTranslateCoordinates, (Display*, ::Window, ::Window, int, int, int*, int*, ::Window*)) \
    F(int, XQueryPointer, (Display*, ::Window, ::Window*, ::Window*, int*, int*, int*, int*, unsigned*)) \
    F(int, XWarpPointer, (Display*, ::Window, ::Window, int, int, unsigned, unsigned, int, int)) \
    F(int, XQueryKeymap, (Display*, char[32])) \
    F(int, XFree, (void*)) \
    F(XSizeHints*, XAllocSizeHints, (void)) \
    F(void, XSetWMNormalHints, (Display*, ::Window, XSizeHints*)) \
    F(KeySym, XLookupKeysym, (XKeyEvent*, int)) \
    F(KeyCode, XKeysymToKeycode, (Display*, KeySym)) \
    F(int, XLookupString, (XKeyEvent*, char*, int, KeySym*, XComposeStatus*)) \
    F(Pixmap, XCreateBitmapFromData, (Display*, ::Drawable, const char*, unsigned, unsigned)) \
    F(Cursor, XCreatePixmapCursor, (Display*, Pixmap, Pixmap, XColor*, XColor*, unsigned, unsigned)) \
    F(int, XFreePixmap, (Display*, Pixmap)) \
    F(int, XFreeCursor, (Display*, Cursor)) \
    F(int, XDefineCursor, (Display*, ::Window, Cursor)) \
    F(int, XUndefineCursor, (Display*, ::Window)) \
    F(int, XGrabPointer, (Display*, ::Window, int, unsigned, int, int, ::Window, Cursor, Time)) \
    F(int, XUngrabPointer, (Display*, Time)) \
    F(int, XSetSelectionOwner, (Display*, Atom, ::Window, Time)) \
    F(::Window, XGetSelectionOwner, (Display*, Atom)) \
    F(int, XConvertSelection, (Display*, Atom, Atom, Atom, ::Window, Time)) \
    F(XErrorHandler, XSetErrorHandler, (XErrorHandler)) \
    F(XImage*, XGetImage, (Display*, ::Drawable, int, int, unsigned, unsigned, unsigned long, int)) \
    F(GC, XCreateGC, (Display*, ::Drawable, unsigned long, XGCValues*)) \
    F(int, XFreeGC, (Display*, GC)) \
    F(XImage*, XCreateImage, (Display*, Visual*, unsigned, int, int, char*, unsigned, unsigned, int, int)) \
    F(int, XPutImage, (Display*, ::Drawable, GC, XImage*, int, int, int, int, unsigned, unsigned)) \
    F(Bool, XFilterEvent, (XEvent*, ::Window)) \
    F(char*, XSetLocaleModifiers, (const char*)) \
    F(Bool, XSupportsLocale, (void)) \
    F(XIM, XOpenIM, (Display*, void*, char*, char*)) \
    F(Status, XCloseIM, (XIM)) \
    F(void, XDestroyIC, (XIC)) \
    F(void, XSetICFocus, (XIC)) \
    F(void, XUnsetICFocus, (XIC)) \
    F(int, Xutf8LookupString, (XIC, XKeyPressedEvent*, char*, int, KeySym*, Status*)) \
    F(Status, XIconifyWindow, (Display*, ::Window, int)) \
    F(int, XRefreshKeyboardMapping, (XMappingEvent*))

#define OKAY_GLX_FUNCS(F) \
    F(GLXFBConfig*, glXChooseFBConfig, (Display*, int, const int*, int*)) \
    F(XVisualInfo*, glXGetVisualFromFBConfig, (Display*, GLXFBConfig)) \
    F(GLXContext, glXCreateNewContext, (Display*, GLXFBConfig, int, GLXContext, Bool)) \
    F(void, glXDestroyContext, (Display*, GLXContext)) \
    F(Bool, glXMakeCurrent, (Display*, GLXDrawable, GLXContext)) \
    F(void, glXSwapBuffers, (Display*, GLXDrawable)) \
    F(GLXContext, glXGetCurrentContext, (void)) \
    F(__GLXextFuncPtr, glXGetProcAddressARB, (const GLubyte*)) \
    F(Bool, glXQueryExtension, (Display*, int*, int*))

struct XTable {
    void* lib = nullptr;
    XIC (*XCreateIC_)(XIM, ...) = nullptr;   // varargs: declared by hand
#define OKAY_DECL(ret, name, args) ret (*name) args = nullptr;
    OKAY_XLIB_FUNCS(OKAY_DECL)
#undef OKAY_DECL
};
struct GlxTable {
    void* lib = nullptr;
#define OKAY_DECL(ret, name, args) ret (*name) args = nullptr;
    OKAY_GLX_FUNCS(OKAY_DECL)
#undef OKAY_DECL
};

bool LoadX(XTable& t, std::string& err) {
    t.lib = dlopen("libX11.so.6", RTLD_NOW | RTLD_GLOBAL);
    if (!t.lib) t.lib = dlopen("libX11.so", RTLD_NOW | RTLD_GLOBAL);
    if (!t.lib) { err = "libX11.so.6 not found"; return false; }
#define OKAY_LOAD(ret, name, args) \
    t.name = reinterpret_cast<ret (*) args>(dlsym(t.lib, #name)); \
    if (!t.name) { err = std::string("libX11 lacks ") + #name; return false; }
    OKAY_XLIB_FUNCS(OKAY_LOAD)
#undef OKAY_LOAD
    t.XCreateIC_ = reinterpret_cast<XIC (*)(XIM, ...)>(dlsym(t.lib, "XCreateIC"));
    if (!t.XCreateIC_) { err = "libX11 lacks XCreateIC"; return false; }
    return true;
}

bool LoadGlx(GlxTable& g) {
    g.lib = dlopen("libGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!g.lib) g.lib = dlopen("libGLX.so.0", RTLD_NOW | RTLD_GLOBAL);
    if (!g.lib) return false;
#define OKAY_LOAD(ret, name, args) \
    g.name = reinterpret_cast<ret (*) args>(dlsym(g.lib, #name)); \
    if (!g.name) return false;
    OKAY_GLX_FUNCS(OKAY_LOAD)
#undef OKAY_LOAD
    return true;
}

uint64_t NowMs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

// Errors from a window that is already gone must not kill the process (Xlib's default).
int g_lastXError = 0;
int OkayXErrorHandler(Display*, XErrorEvent* e) { g_lastXError = e->error_code; return 0; }

// Linux evdev scancode (X keycode - 8) -> USB HID usage, the Key numbering.
Key KeyFromEvdev(int code) {
    static const uint16_t kTab[128] = {
        0, 41, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 45, 46, 42, 43,        // 0..15
        20, 26, 8, 21, 23, 28, 24, 12, 18, 19, 47, 48, 40, 224, 4, 22,        // 16..31
        7, 9, 10, 11, 13, 14, 15, 51, 52, 53, 225, 49, 29, 27, 6, 25,         // 32..47
        5, 17, 16, 54, 55, 56, 229, 85, 226, 44, 57, 58, 59, 60, 61, 62,      // 48..63
        63, 64, 65, 66, 67, 83, 71, 95, 96, 97, 86, 92, 93, 94, 87, 89,       // 64..79
        90, 91, 98, 99, 0, 0, 0, 68, 69, 0, 0, 0, 0, 0, 0, 0,                 // 80..95
        88, 228, 84, 70, 230, 0, 74, 82, 75, 80, 79, 77, 81, 78, 73, 76,      // 96..111
        0, 0, 0, 0, 0, 0, 0, 72, 0, 0, 0, 0, 0, 227, 231, 0};                 // 112..127
    if (code < 0 || code >= 128) return Key::Unknown;
    return (Key)kTab[code];
}

uint32_t ModsFromX(unsigned state) {
    uint32_t m = 0;
    if (state & ShiftMask) m |= ModShift;
    if (state & LockMask) m |= ModCaps;
    if (state & ControlMask) m |= ModCtrl;
    if (state & Mod1Mask) m |= ModAlt;
    if (state & Mod2Mask) m |= ModNum;
    if (state & Mod4Mask) m |= ModGui;
    return m;
}

void AppendUtf8(std::string& s, uint32_t cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

// Keysym -> code point for the ranges that matter without an input method:
// Latin-1 and the 0x01000000 + Unicode keysyms. Returns 0 for anything else.
uint32_t KeysymToCodepoint(KeySym ks) {
    if (ks >= 0x20 && ks <= 0x7e) return (uint32_t)ks;
    if (ks >= 0xa0 && ks <= 0xff) return (uint32_t)ks;
    if ((ks & 0xff000000) == 0x01000000) return (uint32_t)(ks & 0x00ffffff);
    if (ks >= XK_KP_0 && ks <= XK_KP_9) return (uint32_t)('0' + (ks - XK_KP_0));
    return 0;
}

class X11Impl;

class X11Win final : public Window {
public:
    X11Win(X11Impl* sys, ::Window w, Colormap cm) : m_sys(sys), m_win(w), m_cmap(cm) {}
    ~X11Win() override;
    uint32_t Id() const override { return (uint32_t)m_win; }
    void* NativeHandle() const override { return (void*)(uintptr_t)m_win; }
    void GetSize(int& w, int& h) const override;
    void GetPosition(int& x, int& y) const override;
    void SetTitle(const std::string& t) override;
    void SetSize(int w, int h) override;
    void SetPosition(int x, int y) override;
    void SetMinimumSize(int w, int h) override;
    void Show() override;
    void Hide() override;
    void Raise() override;
    void Minimize() override;
    void Restore() override;
    void SetFullscreen(bool on) override;
    bool IsFocused() const override;
    bool IsFullscreen() const override { return m_fullscreen; }
    bool SetIcon(const uint8_t* rgba, int w, int h) override;
    bool PresentPixels(const uint8_t* rgba, int w, int h) override;
    bool CreateGLContext(const GLAttributes& a) override;
    void DestroyGLContext() override;
    bool MakeCurrent() override;
    void SwapBuffers() override;
    bool SetSwapInterval(int i) override;
    void GetDrawableSize(int& w, int& h) const override { GetSize(w, h); }

    X11Impl* m_sys;
    ::Window m_win;
    Colormap m_cmap;
    GLXFBConfig m_fbc = nullptr;
    GLXContext m_ctx = nullptr;
    bool m_focused = false;
    bool m_fullscreen = false;
    bool m_resizable = true;
    int m_minW = 0, m_minH = 0;
    int m_w = 0, m_h = 0;      // last size seen in ConfigureNotify (or requested)
    XIC m_ic = nullptr;
    GC m_gc = nullptr;                // PresentPixels: made on first use
    std::vector<uint8_t> m_present;   // PresentPixels: the frame in the server's pixel layout
};

class X11Impl final : public X11System {
public:
    ~X11Impl() override {
        if (m_blankCursor) m_x.XFreeCursor(m_dpy, m_blankCursor);
        if (m_im) m_x.XCloseIM(m_im);
        if (m_dpy) m_x.XCloseDisplay(m_dpy);
        // libX11 / libGL stay loaded; they may be shared with other code.
    }

    bool Connect(std::string& err) {
        if (!LoadX(m_x, err)) return false;
        m_x.XSetErrorHandler(OkayXErrorHandler);
        m_dpy = m_x.XOpenDisplay(nullptr);
        if (!m_dpy) { err = "cannot open the X display ($DISPLAY unset or unreachable)"; return false; }
        m_screen = DefaultScreen(m_dpy);
        m_root = RootWindow(m_dpy, m_screen);
        m_hasGlx = LoadGlx(m_glx) && m_glx.glXQueryExtension(m_dpy, nullptr, nullptr);
        m_aDelete = m_x.XInternAtom(m_dpy, "WM_DELETE_WINDOW", False);
        m_aProtocols = m_x.XInternAtom(m_dpy, "WM_PROTOCOLS", False);
        m_aNetState = m_x.XInternAtom(m_dpy, "_NET_WM_STATE", False);
        m_aNetFull = m_x.XInternAtom(m_dpy, "_NET_WM_STATE_FULLSCREEN", False);
        m_aMaxH = m_x.XInternAtom(m_dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
        m_aMaxV = m_x.XInternAtom(m_dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
        m_aNetIcon = m_x.XInternAtom(m_dpy, "_NET_WM_ICON", False);
        m_aNetName = m_x.XInternAtom(m_dpy, "_NET_WM_NAME", False);
        m_aUtf8 = m_x.XInternAtom(m_dpy, "UTF8_STRING", False);
        m_aClipboard = m_x.XInternAtom(m_dpy, "CLIPBOARD", False);
        m_aTargets = m_x.XInternAtom(m_dpy, "TARGETS", False);
        m_aProp = m_x.XInternAtom(m_dpy, "OKAY_SELECTION", False);
        m_aTextPlain = m_x.XInternAtom(m_dpy, "text/plain;charset=utf-8", False);
        if (m_x.XSupportsLocale()) {
            m_x.XSetLocaleModifiers("");
            m_im = m_x.XOpenIM(m_dpy, nullptr, nullptr, nullptr);
        }
        return true;
    }

    std::unique_ptr<Window> MakeWindow(const WindowDesc& d, std::string& err) override {
        Visual* visual = DefaultVisual(m_dpy, m_screen);
        int depth = DefaultDepth(m_dpy, m_screen);
        GLXFBConfig fbc = nullptr;
        if (d.opengl) {
            if (!m_hasGlx) { err = "GLX is not available (libGL.so.1 missing or the server has no GLX)"; return nullptr; }
            static const int attrs[] = {GLX_X_RENDERABLE, True, GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
                GLX_RENDER_TYPE, GLX_RGBA_BIT, GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
                GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8,
                GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, True, 0};
            int n = 0;
            GLXFBConfig* cfgs = m_glx.glXChooseFBConfig(m_dpy, m_screen, attrs, &n);
            if (!cfgs || n < 1) { err = "no GLX framebuffer config (RGBA8, depth 24, double buffered)"; if (cfgs) m_x.XFree(cfgs); return nullptr; }
            fbc = cfgs[0];
            m_x.XFree(cfgs);
            XVisualInfo* vi = m_glx.glXGetVisualFromFBConfig(m_dpy, fbc);
            if (!vi) { err = "GLX config has no visual"; return nullptr; }
            visual = vi->visual; depth = vi->depth;
            m_x.XFree(vi);
        }
        const int sw = DisplayWidth(m_dpy, m_screen), sh = DisplayHeight(m_dpy, m_screen);
        int x = d.x == kCentered ? (sw - d.width) / 2 : d.x;
        int y = d.y == kCentered ? (sh - d.height) / 2 : d.y;
        XSetWindowAttributes swa;
        std::memset(&swa, 0, sizeof(swa));
        Colormap cmap = m_x.XCreateColormap(m_dpy, m_root, visual, AllocNone);
        swa.colormap = cmap;
        swa.border_pixel = 0;
        swa.background_pixel = 0;
        swa.override_redirect = False;
        swa.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                         PointerMotionMask | StructureNotifyMask | ExposureMask | FocusChangeMask |
                         PropertyChangeMask;
        ::Window w = m_x.XCreateWindow(m_dpy, m_root, x, y, (unsigned)d.width, (unsigned)d.height, 0, depth,
            InputOutput, visual, CWColormap | CWBorderPixel | CWBackPixel | CWEventMask, &swa);
        if (!w) { err = "XCreateWindow failed"; m_x.XFreeColormap(m_dpy, cmap); return nullptr; }
        auto* win = new X11Win(this, w, cmap);
        win->m_fbc = fbc;
        win->m_resizable = d.resizable;
        win->m_w = d.width; win->m_h = d.height;
        m_wins[w] = win;
        Atom protos[1] = {m_aDelete};
        m_x.XSetWMProtocols(m_dpy, w, protos, 1);
        win->SetTitle(d.title);
        XSizeHints* sh2 = m_x.XAllocSizeHints();
        if (sh2) {
            sh2->flags = PPosition | PSize;
            sh2->x = x; sh2->y = y; sh2->width = d.width; sh2->height = d.height;
            if (!d.resizable) { sh2->flags |= PMinSize | PMaxSize; sh2->min_width = sh2->max_width = d.width; sh2->min_height = sh2->max_height = d.height; }
            m_x.XSetWMNormalHints(m_dpy, w, sh2);
            m_x.XFree(sh2);
        }
        if (d.borderless) {   // Motif hints: flags = decorations, decorations = none
            struct { unsigned long flags, functions, decorations; long input_mode; unsigned long status; } mwm = {2, 0, 0, 0, 0};
            Atom a = m_x.XInternAtom(m_dpy, "_MOTIF_WM_HINTS", False);
            m_x.XChangeProperty(m_dpy, w, a, a, 32, PropModeReplace, (const unsigned char*)&mwm, 5);
        }
        if (m_im) {
            win->m_ic = m_x.XCreateIC_(m_im, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                                       XNClientWindow, w, XNFocusWindow, w, (void*)nullptr);
        }
        {   // start fullscreen / maximised: the initial _NET_WM_STATE is read when the window is mapped
            Atom st[2]; int n = 0;
            if (d.fullscreen) { st[n++] = m_aNetFull; win->m_fullscreen = true; }
            else if (d.maximized && d.resizable) { st[n++] = m_aMaxH; st[n++] = m_aMaxV; }
            if (n) m_x.XChangeProperty(m_dpy, w, m_aNetState, XA_ATOM, 32, PropModeReplace, (const unsigned char*)st, n);
        }
        if (!d.hidden) win->Show();
        m_x.XSync(m_dpy, False);
        return std::unique_ptr<Window>(win);
    }

    void Unregister(::Window w) { m_wins.erase(w); if (m_focus == w) m_focus = 0; }

    // ---- events ---------------------------------------------------------------------
    bool PollEvent(Event& out) override {
        for (;;) {
            if (!m_pending.empty()) { out = std::move(m_pending.front()); m_pending.pop_front(); return true; }
            if (!m_x.XPending(m_dpy)) return false;
            XEvent ev;
            m_x.XNextEvent(m_dpy, &ev);
            if (m_x.XFilterEvent(&ev, None)) continue;   // eaten by the input method
            Handle(ev);
        }
    }

    void Handle(XEvent& ev) {
        auto it = m_wins.find(ev.xany.window);
        X11Win* win = it == m_wins.end() ? nullptr : it->second;
        Event e;
        e.timestampMs = (uint32_t)NowMs();
        if (win) e.windowId = (uint32_t)win->m_win;
        switch (ev.type) {
        case ClientMessage:
            if (win && ev.xclient.message_type == m_aProtocols && (Atom)ev.xclient.data.l[0] == m_aDelete) {
                e.type = EventType::WindowClose; m_pending.push_back(e);
                if (m_wins.size() == 1) { Event q; q.type = EventType::Quit; q.timestampMs = e.timestampMs; m_pending.push_back(q); }
            }
            break;
        case ConfigureNotify:
            if (win) {
                if (ev.xconfigure.width != win->m_w || ev.xconfigure.height != win->m_h) {
                    win->m_w = ev.xconfigure.width; win->m_h = ev.xconfigure.height;
                    e.type = EventType::WindowResized; e.width = win->m_w; e.height = win->m_h;
                    m_pending.push_back(e);
                }
            }
            break;
        case FocusIn:
            if (win && ev.xfocus.mode != NotifyGrab && ev.xfocus.mode != NotifyUngrab) {
                win->m_focused = true; m_focus = win->m_win;
                if (win->m_ic) m_x.XSetICFocus(win->m_ic);
                e.type = EventType::WindowFocusGained; m_pending.push_back(e);
            }
            break;
        case FocusOut:
            if (win && ev.xfocus.mode != NotifyGrab && ev.xfocus.mode != NotifyUngrab) {
                win->m_focused = false; if (m_focus == win->m_win) m_focus = 0;
                if (win->m_ic) m_x.XUnsetICFocus(win->m_ic);
                e.type = EventType::WindowFocusLost; m_pending.push_back(e);
            }
            break;
        case Expose:
            if (win && ev.xexpose.count == 0) { e.type = EventType::WindowExposed; m_pending.push_back(e); }
            break;
        case KeyPress: case KeyRelease: if (win) HandleKey(ev, win, e); break;
        case ButtonPress: case ButtonRelease: if (win) HandleButton(ev, e); break;
        case MotionNotify:
            if (win) {
                e.type = EventType::MouseMove; e.x = ev.xmotion.x; e.y = ev.xmotion.y;
                if (m_relative) {
                    const int cx = win->m_w / 2, cy = win->m_h / 2;
                    if (e.x == cx && e.y == cy && m_warped) { m_warped = false; break; }   // our own warp
                    e.dx = e.x - cx; e.dy = e.y - cy;
                    m_x.XWarpPointer(m_dpy, None, win->m_win, 0, 0, 0, 0, cx, cy);
                    m_warped = true;
                } else {
                    e.dx = e.x - m_lastX; e.dy = e.y - m_lastY;
                }
                m_lastX = e.x; m_lastY = e.y;
                m_relAccX += e.dx; m_relAccY += e.dy;
                m_pending.push_back(e);
            }
            break;
        case MappingNotify:   // the keyboard layout changed: Xlib's cached keymap must follow
            if (ev.xmapping.request != MappingPointer) m_x.XRefreshKeyboardMapping(&ev.xmapping);
            break;
        case SelectionRequest: HandleSelectionRequest(ev.xselectionrequest); break;
        case SelectionClear: m_ownsClip = false; break;
        case SelectionNotify: m_selNotify = true; m_selProp = ev.xselection.property; break;
        default: break;
        }
    }

    void HandleKey(XEvent& ev, X11Win* win, Event& e) {
        const bool down = ev.type == KeyPress;
        e.mods = ModsFromX(ev.xkey.state);
        e.key = KeyFromEvdev((int)ev.xkey.keycode - 8);
        if (!down) {
            // X sends KeyRelease+KeyPress for an auto-repeat; a release followed at once by
            // a press of the same key and time is a repeat, not a release.
            if (m_x.XPending(m_dpy)) {
                XEvent nx;
                m_x.XPeekEvent(m_dpy, &nx);
                if (nx.type == KeyPress && nx.xkey.keycode == ev.xkey.keycode && nx.xkey.time == ev.xkey.time) {
                    XEvent drop; m_x.XNextEvent(m_dpy, &drop);
                    e.type = EventType::KeyDown; e.repeat = true; m_pending.push_back(e);
                    EmitText(drop, win, e);
                    return;
                }
            }
            e.type = EventType::KeyUp; m_pending.push_back(e);
            return;
        }
        e.type = EventType::KeyDown; m_pending.push_back(e);
        EmitText(ev, win, e);
    }

    void EmitText(XEvent& ev, X11Win* win, const Event& base) {
        if (!m_textInput || (ev.xkey.state & (ControlMask | Mod1Mask | Mod4Mask))) return;
        std::string text;
        char buf[64];
        KeySym ks = NoSymbol;
        if (win->m_ic) {
            Status st = 0;
            const int n = m_x.Xutf8LookupString(win->m_ic, &ev.xkey, buf, (int)sizeof(buf) - 1, &ks, &st);
            if (n > 0 && (st == XLookupChars || st == XLookupBoth)) text.assign(buf, (size_t)n);
        }
        if (text.empty()) {
            m_x.XLookupString(&ev.xkey, buf, (int)sizeof(buf) - 1, &ks, nullptr);
            // XLookupString gives Latin-1 bytes; the keysym is the better source for UTF-8.
            const uint32_t cp = KeysymToCodepoint(ks);
            if (cp) AppendUtf8(text, cp);
        }
        // Drop control characters (Return, Tab, Backspace make no text, as in SDL).
        if (text.empty() || (unsigned char)text[0] < 0x20 || (unsigned char)text[0] == 0x7f) return;
        Event t = base;
        t.type = EventType::TextInput; t.text = text; t.repeat = false; t.key = Key::Unknown;
        m_pending.push_back(t);
    }

    void HandleButton(XEvent& ev, Event& e) {
        const bool down = ev.type == ButtonPress;
        const unsigned b = ev.xbutton.button;
        e.x = ev.xbutton.x; e.y = ev.xbutton.y; e.mods = ModsFromX(ev.xbutton.state);
        if (b >= 4 && b <= 7) {   // wheel: 4 up, 5 down, 6 left, 7 right (press only)
            if (!down) return;
            e.type = EventType::MouseWheel;
            if (b == 4) e.wheelY = 1.0f; else if (b == 5) e.wheelY = -1.0f;
            else if (b == 6) e.wheelX = -1.0f; else e.wheelX = 1.0f;
            m_pending.push_back(e);
            return;
        }
        int btn = 0;
        if (b >= 1 && b <= 3) btn = (int)b;
        else if (b == 8) btn = MouseX1;
        else if (b == 9) btn = MouseX2;
        if (!btn) return;
        e.type = down ? EventType::MouseButtonDown : EventType::MouseButtonUp;
        e.button = btn;
        if (down) {
            const bool again = btn == m_lastBtn && ev.xbutton.time - m_lastBtnTime < 400;
            m_clicks = again ? m_clicks + 1 : 1; m_lastBtn = btn; m_lastBtnTime = ev.xbutton.time;
        }
        e.clicks = m_clicks;
        m_pending.push_back(e);
    }

    // ---- misc input ------------------------------------------------------------------
    void* GLProcAddress(const char* name) override {
        if (!m_hasGlx) return nullptr;
        return (void*)m_glx.glXGetProcAddressARB((const GLubyte*)name);
    }
    void ReleaseCurrent() override { if (m_hasGlx) m_glx.glXMakeCurrent(m_dpy, None, nullptr); }
    void StartTextInput() override { m_textInput = true; }
    void StopTextInput() override { m_textInput = false; }
    bool IsKeyDown(Key k) override {
        char keys[32];
        m_x.XQueryKeymap(m_dpy, keys);
        for (int code = 8; code < 256; ++code)
            if ((keys[code / 8] >> (code % 8)) & 1)
                if (KeyFromEvdev(code - 8) == k) return true;
        return false;
    }
    uint32_t MouseState(int* x, int* y) override {
        ::Window root, child; int rx, ry, wx = 0, wy = 0; unsigned mask = 0;
        ::Window target = m_focus;
        if (!target && !m_wins.empty()) target = m_wins.begin()->first;
        if (!target) { if (x) *x = 0; if (y) *y = 0; return 0; }
        m_x.XQueryPointer(m_dpy, target, &root, &child, &rx, &ry, &wx, &wy, &mask);
        if (x) *x = wx;
        if (y) *y = wy;
        uint32_t r = 0;
        if (mask & Button1Mask) r |= MouseMask(MouseLeft);
        if (mask & Button2Mask) r |= MouseMask(MouseMiddle);
        if (mask & Button3Mask) r |= MouseMask(MouseRight);
        return r;
    }
    uint32_t RelativeMouseState(int* dx, int* dy) override {
        if (dx) *dx = m_relAccX;
        if (dy) *dy = m_relAccY;
        m_relAccX = m_relAccY = 0;
        return MouseState(nullptr, nullptr);
    }
    Key KeyForChar(char c) override {
        // The key whose unshifted keysym is `c` on the current layout (X keycode - 8 = evdev code).
        KeySym ks = 0;
        if (c >= 'A' && c <= 'Z') ks = (KeySym)(c - 'A' + 'a');
        else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ') ks = (KeySym)c;
        else return Key::Unknown;
        const KeyCode kc = m_x.XKeysymToKeycode(m_dpy, ks);
        return kc ? KeyFromEvdev((int)kc - 8) : Key::Unknown;
    }
    bool IsCursorShown() override { return !m_cursorHidden; }
    bool SetRelativeMouse(bool on) override {
        if (on == m_relative) return true;
        m_relAccX = m_relAccY = 0;   // no stale motion across a mode change
        ::Window w = m_focus;
        if (!w && !m_wins.empty()) w = m_wins.begin()->first;
        if (!w) return false;
        if (on) {
            HideCursorOn(w, true);
            if (m_x.XGrabPointer(m_dpy, w, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                 GrabModeAsync, GrabModeAsync, w, None, CurrentTime) != GrabSuccess) {
                HideCursorOn(w, m_cursorHidden);
                return false;
            }
            X11Win* win = m_wins[w];
            m_x.XWarpPointer(m_dpy, None, w, 0, 0, 0, 0, win->m_w / 2, win->m_h / 2);
            m_warped = true;
            m_relative = true;
        } else {
            m_x.XUngrabPointer(m_dpy, CurrentTime);
            m_relative = false; m_warped = false;
            for (auto& kv : m_wins) HideCursorOn(kv.first, m_cursorHidden);
        }
        m_x.XFlush(m_dpy);
        return true;
    }
    void ShowCursor(bool show) override {
        m_cursorHidden = !show;
        for (auto& kv : m_wins) HideCursorOn(kv.first, m_cursorHidden || m_relative);
        m_x.XFlush(m_dpy);
    }
    void HideCursorOn(::Window w, bool hide) {
        if (!hide) { m_x.XUndefineCursor(m_dpy, w); return; }
        if (!m_blankCursor) {
            static const char data[1] = {0};
            XColor black; std::memset(&black, 0, sizeof(black));
            Pixmap pm = m_x.XCreateBitmapFromData(m_dpy, m_root, data, 1, 1);
            m_blankCursor = m_x.XCreatePixmapCursor(m_dpy, pm, pm, &black, &black, 0, 0);
            m_x.XFreePixmap(m_dpy, pm);
        }
        m_x.XDefineCursor(m_dpy, w, m_blankCursor);
    }
    void WarpMouse(Window* w, int x, int y) override {
        ::Window target = w ? (::Window)(uintptr_t)w->NativeHandle() : m_focus;
        if (!target) return;
        m_x.XWarpPointer(m_dpy, None, target, 0, 0, 0, 0, x, y);
        m_x.XFlush(m_dpy);
    }

    // ---- clipboard ---------------------------------------------------------------------
    // We own the CLIPBOARD selection after SetClipboardText and answer requests while
    // events are pumped (PollEvent / GetClipboardText). Reading someone else's selection
    // sends a conversion request and waits up to a second for the answer.
    bool SetClipboardText(const std::string& t) override {
        ::Window w = AnyWindow();
        if (!w) return false;
        m_clip = t;
        m_x.XSetSelectionOwner(m_dpy, m_aClipboard, w, CurrentTime);
        m_ownsClip = m_x.XGetSelectionOwner(m_dpy, m_aClipboard) == w;
        m_x.XFlush(m_dpy);
        return m_ownsClip;
    }
    std::string GetClipboardText() override {
        ::Window w = AnyWindow();
        if (!w) return "";
        // Another client may have taken the selection since: ask the server who owns it now.
        const ::Window owner = m_x.XGetSelectionOwner(m_dpy, m_aClipboard);
        while (m_x.XPending(m_dpy)) { XEvent ev; m_x.XNextEvent(m_dpy, &ev); Handle(ev); }   // e.g. SelectionClear
        if (m_ownsClip && m_wins.count(owner)) return m_clip;
        m_ownsClip = false;
        if (owner == None) return "";
        m_selNotify = false;
        m_x.XDeleteProperty(m_dpy, w, m_aProp);
        m_x.XConvertSelection(m_dpy, m_aClipboard, m_aUtf8, m_aProp, w, CurrentTime);
        m_x.XFlush(m_dpy);
        const uint64_t t0 = NowMs();
        while (!m_selNotify && NowMs() - t0 < 1000) {
            while (m_x.XPending(m_dpy)) { XEvent ev; m_x.XNextEvent(m_dpy, &ev); Handle(ev); }
            if (!m_selNotify) { struct timespec ts = {0, 2000000}; nanosleep(&ts, nullptr); }
        }
        if (!m_selNotify || m_selProp == None) return "";
        Atom type; int fmt; unsigned long n, rest; unsigned char* data = nullptr;
        std::string out;
        if (m_x.XGetWindowProperty(m_dpy, w, m_aProp, 0, 1 << 20, True, AnyPropertyType, &type, &fmt, &n, &rest, &data) == Success && data) {
            if (fmt == 8) out.assign((char*)data, n);
            m_x.XFree(data);
        }
        return out;
    }
    void HandleSelectionRequest(XSelectionRequestEvent& rq) {
        XEvent r; std::memset(&r, 0, sizeof(r));
        r.xselection.type = SelectionNotify;
        r.xselection.display = rq.display;
        r.xselection.requestor = rq.requestor;
        r.xselection.selection = rq.selection;
        r.xselection.target = rq.target;
        r.xselection.time = rq.time;
        r.xselection.property = None;
        const Atom prop = rq.property ? rq.property : rq.target;
        if (m_ownsClip && rq.selection == m_aClipboard) {
            if (rq.target == m_aTargets) {
                Atom t[4] = {m_aTargets, m_aUtf8, XA_STRING, m_aTextPlain};
                m_x.XChangeProperty(m_dpy, rq.requestor, prop, XA_ATOM, 32, PropModeReplace, (unsigned char*)t, 4);
                r.xselection.property = prop;
            } else if (rq.target == m_aUtf8 || rq.target == XA_STRING || rq.target == m_aTextPlain) {
                m_x.XChangeProperty(m_dpy, rq.requestor, prop, rq.target, 8, PropModeReplace,
                                    (const unsigned char*)m_clip.data(), (int)m_clip.size());
                r.xselection.property = prop;
            }
        }
        m_x.XSendEvent(m_dpy, rq.requestor, False, NoEventMask, &r);
        m_x.XFlush(m_dpy);
    }
    ::Window AnyWindow() {
        if (m_focus) return m_focus;
        return m_wins.empty() ? 0 : m_wins.begin()->first;
    }

    bool GrabWindow(Window& w, std::vector<uint8_t>& rgba, int& width, int& height) override {
        auto* xw = static_cast<X11Win*>(&w);
        XWindowAttributes at;
        m_x.XSync(m_dpy, False);
        if (!m_x.XGetWindowAttributes(m_dpy, xw->m_win, &at)) return false;
        XImage* img = m_x.XGetImage(m_dpy, xw->m_win, 0, 0, (unsigned)at.width, (unsigned)at.height, AllPlanes, ZPixmap);
        if (!img) return false;
        width = img->width; height = img->height;
        rgba.assign((size_t)width * height * 4, 255);
        for (int yy = 0; yy < height; ++yy)
            for (int xx = 0; xx < width; ++xx) {
                const unsigned long p = img->f.get_pixel(img, xx, yy);
                uint8_t* o = &rgba[((size_t)yy * width + xx) * 4];
                o[0] = (uint8_t)((p & img->red_mask) >> Shift(img->red_mask));
                o[1] = (uint8_t)((p & img->green_mask) >> Shift(img->green_mask));
                o[2] = (uint8_t)((p & img->blue_mask) >> Shift(img->blue_mask));
            }
        img->f.destroy_image(img);
        return true;
    }
    static int Shift(unsigned long mask) { int s = 0; while (mask && !(mask & 1)) { mask >>= 1; ++s; } return s; }

    XTable m_x;
    GlxTable m_glx;
    bool m_hasGlx = false;
    Display* m_dpy = nullptr;
    int m_screen = 0;
    ::Window m_root = 0;
    XIM m_im = nullptr;
    Atom m_aMaxH = 0, m_aMaxV = 0, m_aNetIcon = 0;
    Atom m_aDelete = 0, m_aProtocols = 0, m_aNetState = 0, m_aNetFull = 0, m_aNetName = 0, m_aUtf8 = 0,
         m_aClipboard = 0, m_aTargets = 0, m_aProp = 0, m_aTextPlain = 0;
    std::unordered_map<::Window, X11Win*> m_wins;
    std::deque<Event> m_pending;
    ::Window m_focus = 0;
    bool m_textInput = false, m_relative = false, m_warped = false, m_cursorHidden = false;
    Cursor m_blankCursor = 0;
    int m_lastX = 0, m_lastY = 0;
    int m_relAccX = 0, m_relAccY = 0;   // motion since RelativeMouseState was last read
    int m_lastBtn = 0, m_clicks = 0; unsigned long m_lastBtnTime = 0;
    std::string m_clip;
    bool m_ownsClip = false, m_selNotify = false;
    Atom m_selProp = 0;
};

// ---- X11Win methods ------------------------------------------------------------------
#define XD m_sys->m_dpy
#define XF m_sys->m_x

X11Win::~X11Win() {
    DestroyGLContext();
    if (m_ic) XF.XDestroyIC(m_ic);
    if (m_gc) XF.XFreeGC(XD, m_gc);
    m_sys->Unregister(m_win);
    XF.XDestroyWindow(XD, m_win);
    XF.XFreeColormap(XD, m_cmap);
    XF.XSync(XD, False);
}
void X11Win::GetSize(int& w, int& h) const {
    XWindowAttributes a;
    if (XF.XGetWindowAttributes(XD, m_win, &a)) { w = a.width; h = a.height; } else { w = m_w; h = m_h; }
}
void X11Win::GetPosition(int& x, int& y) const {
    // Root-relative: a reparenting window manager makes ConfigureNotify x / y parent-relative.
    ::Window child;
    if (!XF.XTranslateCoordinates(XD, m_win, m_sys->m_root, 0, 0, &x, &y, &child)) { x = y = 0; }
}
void X11Win::SetTitle(const std::string& t) {
    XF.XStoreName(XD, m_win, t.c_str());
    XF.XChangeProperty(XD, m_win, m_sys->m_aNetName, m_sys->m_aUtf8, 8, PropModeReplace,
                       (const unsigned char*)t.data(), (int)t.size());
}
void X11Win::SetSize(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (!m_resizable) {   // size hints pin a fixed window; move them first or the WM refuses
        XSizeHints* sh = XF.XAllocSizeHints();
        if (sh) { sh->flags = PMinSize | PMaxSize; sh->min_width = sh->max_width = w; sh->min_height = sh->max_height = h; XF.XSetWMNormalHints(XD, m_win, sh); XF.XFree(sh); }
    }
    XF.XResizeWindow(XD, m_win, (unsigned)w, (unsigned)h);
    XF.XFlush(XD);
}
void X11Win::SetPosition(int x, int y) {
    if (x == kCentered) { int w, h; GetSize(w, h); x = (DisplayWidth(XD, m_sys->m_screen) - w) / 2; }
    if (y == kCentered) { int w, h; GetSize(w, h); y = (DisplayHeight(XD, m_sys->m_screen) - h) / 2; }
    XF.XMoveWindow(XD, m_win, x, y);
    XF.XFlush(XD);
}
void X11Win::SetMinimumSize(int w, int h) {
    m_minW = w; m_minH = h;
    XSizeHints* sh = XF.XAllocSizeHints();
    if (!sh) return;
    sh->flags = PMinSize; sh->min_width = w; sh->min_height = h;
    XF.XSetWMNormalHints(XD, m_win, sh);
    XF.XFree(sh);
}
void X11Win::Show() { XF.XMapWindow(XD, m_win); XF.XFlush(XD); }
void X11Win::Hide() { XF.XUnmapWindow(XD, m_win); XF.XFlush(XD); }
void X11Win::Raise() {
    XF.XRaiseWindow(XD, m_win);
    XF.XSync(XD, False);
    g_lastXError = 0;
    XF.XSetInputFocus(XD, m_win, RevertToParent, CurrentTime);   // no window manager to do it for us
    XF.XSync(XD, False);
}
void X11Win::Minimize() { XF.XIconifyWindow(XD, m_win, m_sys->m_screen); XF.XFlush(XD); }
void X11Win::Restore() { XF.XMapWindow(XD, m_win); XF.XRaiseWindow(XD, m_win); XF.XFlush(XD); }
bool X11Win::SetIcon(const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return false;
    std::vector<unsigned long> data((size_t)w * h + 2);   // _NET_WM_ICON: CARD32 as long: w, h, ARGB...
    data[0] = (unsigned long)w; data[1] = (unsigned long)h;
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        const uint8_t* p = rgba + i * 4;
        data[2 + i] = ((unsigned long)p[3] << 24) | ((unsigned long)p[0] << 16) | ((unsigned long)p[1] << 8) | p[2];
    }
    XF.XChangeProperty(XD, m_win, m_sys->m_aNetIcon, XA_CARDINAL, 32, PropModeReplace,
                       (const unsigned char*)data.data(), (int)data.size());
    XF.XFlush(XD);
    return true;
}
bool X11Win::PresentPixels(const uint8_t* rgba, int w, int h) {
    if (!rgba || w <= 0 || h <= 0) return false;
    XWindowAttributes at;
    if (!XF.XGetWindowAttributes(XD, m_win, &at) || !at.visual) return false;
    if (at.visual->c_class != TrueColor && at.visual->c_class != DirectColor) return false;
    const int cw = w < at.width ? w : at.width, ch = h < at.height ? h : at.height;   // what fits
    if (cw <= 0 || ch <= 0) return false;
    // The server's pixel layout: 32 bits per pixel with the visual's channel masks (the usual 24/32-bit
    // TrueColor, B G R X in memory on a little-endian host). Anything else (16-bit depth, palettes) is not
    // worth a slow path here and reports false.
    const unsigned long rm = at.visual->red_mask, gm = at.visual->green_mask, bm = at.visual->blue_mask;
    if (!rm || !gm || !bm || at.depth < 24) return false;
    auto shiftOf = [](unsigned long m) { int s = 0; while (m && !(m & 1)) { m >>= 1; ++s; } return s; };
    const int rs = shiftOf(rm), gs = shiftOf(gm), bs = shiftOf(bm);
    m_present.resize((size_t)cw * (size_t)ch * 4);
    uint32_t* out = (uint32_t*)m_present.data();
    for (int y = 0; y < ch; ++y) {
        const uint8_t* src = rgba + (size_t)y * (size_t)w * 4;
        uint32_t* row = out + (size_t)y * (size_t)cw;
        for (int x = 0; x < cw; ++x, src += 4)
            row[x] = ((uint32_t)src[0] << rs) | ((uint32_t)src[1] << gs) | ((uint32_t)src[2] << bs);
    }
    if (!m_gc) m_gc = XF.XCreateGC(XD, m_win, 0, nullptr);
    if (!m_gc) return false;
    XImage* img = XF.XCreateImage(XD, at.visual, (unsigned)at.depth, ZPixmap, 0, (char*)m_present.data(),
                                  (unsigned)cw, (unsigned)ch, 32, cw * 4);
    if (!img) return false;
    if (img->byte_order != LSBFirst || img->bits_per_pixel != 32) {   // the loop above writes native little-endian 32-bit words
        img->data = nullptr; img->f.destroy_image(img);
        return false;
    }
    const int before = g_lastXError;
    g_lastXError = 0;
    XF.XPutImage(XD, m_win, m_gc, img, 0, 0, 0, 0, (unsigned)cw, (unsigned)ch);
    img->data = nullptr;                       // ours (m_present), not Xlib's to free
    img->f.destroy_image(img);
    XF.XSync(XD, False);                       // the server has it before the next frame starts: no queue of frames
    const bool ok = g_lastXError == 0;
    if (before) g_lastXError = before;
    return ok;
}
void X11Win::SetFullscreen(bool on) {
    m_fullscreen = on;
    XEvent e; std::memset(&e, 0, sizeof(e));
    e.xclient.type = ClientMessage;
    e.xclient.window = m_win;
    e.xclient.message_type = m_sys->m_aNetState;
    e.xclient.format = 32;
    e.xclient.data.l[0] = on ? 1 : 0;   // _NET_WM_STATE_ADD / REMOVE
    e.xclient.data.l[1] = (long)m_sys->m_aNetFull;
    e.xclient.data.l[3] = 1;
    XF.XSendEvent(XD, m_sys->m_root, False, SubstructureNotifyMask | SubstructureRedirectMask, &e);
    XF.XFlush(XD);
}
bool X11Win::IsFocused() const { return m_focused; }

bool X11Win::CreateGLContext(const GLAttributes& a) {
    DestroyGLContext();
    if (!m_fbc || !m_sys->m_hasGlx) return false;
    using CreateAttribs = GLXContext (*)(Display*, GLXFBConfig, GLXContext, Bool, const int*);
    auto create = reinterpret_cast<CreateAttribs>(m_sys->m_glx.glXGetProcAddressARB((const GLubyte*)"glXCreateContextAttribsARB"));
    g_lastXError = 0;
    if (create) {
        const int attrs[] = {0x2091 /*MAJOR*/, a.major, 0x2092 /*MINOR*/, a.minor,
                             0x9126 /*PROFILE_MASK*/, a.compatibility ? 0x2 : 0x1, 0};
        m_ctx = create(XD, m_fbc, nullptr, True, attrs);
        XF.XSync(XD, False);
        if (g_lastXError) { m_ctx = nullptr; g_lastXError = 0; }
    }
    if (!m_ctx) m_ctx = m_sys->m_glx.glXCreateNewContext(XD, m_fbc, GLX_RGBA_TYPE, nullptr, True);   // legacy
    if (!m_ctx) return false;
    return m_sys->m_glx.glXMakeCurrent(XD, m_win, m_ctx);
}
void X11Win::DestroyGLContext() {
    if (!m_ctx) return;
    if (m_sys->m_glx.glXGetCurrentContext() == m_ctx) m_sys->m_glx.glXMakeCurrent(XD, None, nullptr);
    m_sys->m_glx.glXDestroyContext(XD, m_ctx);
    m_ctx = nullptr;
}
bool X11Win::MakeCurrent() { return m_ctx && m_sys->m_glx.glXMakeCurrent(XD, m_win, m_ctx); }
void X11Win::SwapBuffers() { if (m_ctx) m_sys->m_glx.glXSwapBuffers(XD, m_win); }
bool X11Win::SetSwapInterval(int i) {
    if (!m_ctx) return false;
    auto proc = [&](const char* n) { return (void*)m_sys->m_glx.glXGetProcAddressARB((const GLubyte*)n); };
    if (void* f = proc("glXSwapIntervalEXT")) { ((void (*)(Display*, GLXDrawable, int))f)(XD, m_win, i); return true; }
    if (void* f = proc("glXSwapIntervalMESA")) return ((int (*)(unsigned))f)((unsigned)i) == 0;
    if (void* f = proc("glXSwapIntervalSGI")) return ((int (*)(int))f)(i) == 0;
    return false;
}
#undef XD
#undef XF

} // namespace

std::unique_ptr<X11System> X11System::Open(std::string& err) {
    std::unique_ptr<X11Impl> s(new X11Impl());
    if (!s->Connect(err)) return nullptr;
    return s;
}

} // namespace okay::platform::own
#endif // OKAY_PLATFORM_X11_HEADERS
