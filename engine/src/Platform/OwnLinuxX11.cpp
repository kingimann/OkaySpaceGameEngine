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

#include "EventQueue.hpp"
#include "Exchange.hpp"
#include "ScaleMath.hpp"
#include "X11Logic.hpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#if defined(__has_include)
#if __has_include(<X11/extensions/Xrandr.h>)
#include <X11/extensions/Xrandr.h>
#define OKAY_HAVE_XRANDR_H 1
#endif
#endif
#include <GL/glx.h>
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>
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
#ifdef CursorShape   // X.h: CursorShape 0 (XQueryBestSize); our CursorShape is the enum in Platform.hpp
#undef CursorShape
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
    F(char*, XGetAtomName, (Display*, Atom)) \
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
    F(int, XRefreshKeyboardMapping, (XMappingEvent*)) \
    F(Cursor, XCreateFontCursor, (Display*, unsigned)) \
    F(XWMHints*, XGetWMHints, (Display*, ::Window)) \
    F(XWMHints*, XAllocWMHints, (void)) \
    F(int, XSetWMHints, (Display*, ::Window, XWMHints*)) \
    F(XFontStruct*, XLoadQueryFont, (Display*, const char*)) \
    F(int, XFreeFont, (Display*, XFontStruct*)) \
    F(int, XTextWidth, (XFontStruct*, const char*, int)) \
    F(int, XDrawString, (Display*, ::Drawable, GC, int, int, const char*, int)) \
    F(int, XSetForeground, (Display*, GC, unsigned long)) \
    F(int, XFillRectangle, (Display*, ::Drawable, GC, int, int, unsigned, unsigned)) \
    F(int, XDrawRectangle, (Display*, ::Drawable, GC, int, int, unsigned, unsigned)) \
    F(int, XSetFont, (Display*, GC, Font)) \
    F(int, XClearWindow, (Display*, ::Window)) \
    F(int, XSetTransientForHint, (Display*, ::Window, ::Window))

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
    char* (*XGetIMValues_)(XIM, ...) = nullptr;
    XVaNestedList (*XVaCreateNestedList_)(int, ...) = nullptr;
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

#ifdef OKAY_HAVE_XRANDR_H
#define OKAY_XRR_FUNCS(F) \
    F(Bool, XRRQueryExtension, (Display*, int*, int*)) \
    F(Status, XRRQueryVersion, (Display*, int*, int*)) \
    F(XRRMonitorInfo*, XRRGetMonitors, (Display*, ::Window, Bool, int*)) \
    F(void, XRRFreeMonitors, (XRRMonitorInfo*)) \
    F(XRRScreenResources*, XRRGetScreenResourcesCurrent, (Display*, ::Window)) \
    F(void, XRRFreeScreenResources, (XRRScreenResources*)) \
    F(XRROutputInfo*, XRRGetOutputInfo, (Display*, XRRScreenResources*, RROutput)) \
    F(void, XRRFreeOutputInfo, (XRROutputInfo*)) \
    F(XRRCrtcInfo*, XRRGetCrtcInfo, (Display*, XRRScreenResources*, RRCrtc)) \
    F(void, XRRFreeCrtcInfo, (XRRCrtcInfo*))
struct XrrTable {
    void* lib = nullptr;
#define OKAY_DECL(ret, name, args) ret (*name) args = nullptr;
    OKAY_XRR_FUNCS(OKAY_DECL)
#undef OKAY_DECL
};
// libXrandr is optional: without it the monitor list is the one X screen.
bool LoadXrr(XrrTable& r) {
    r.lib = dlopen("libXrandr.so.2", RTLD_NOW | RTLD_GLOBAL);
    if (!r.lib) r.lib = dlopen("libXrandr.so", RTLD_NOW | RTLD_GLOBAL);
    if (!r.lib) return false;
#define OKAY_LOAD(ret, name, args) \
    r.name = reinterpret_cast<ret (*) args>(dlsym(r.lib, #name)); \
    if (!r.name) return false;
    OKAY_XRR_FUNCS(OKAY_LOAD)
#undef OKAY_LOAD
    return true;
}
#else
struct XrrTable { void* lib = nullptr; };
#endif

static_assert(XC_left_ptr == 68 && XC_xterm == 152 && XC_watch == 150 && XC_crosshair == 34 && XC_hand2 == 60 &&
              XC_sb_v_double_arrow == 116 && XC_sb_h_double_arrow == 108 && XC_bottom_right_corner == 14 &&
              XC_bottom_left_corner == 12 && XC_fleur == 52 && XC_X_cursor == 0,
              "X11Logic.hpp CursorGlyph numbers must match cursorfont.h");

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
    t.XGetIMValues_ = reinterpret_cast<char* (*)(XIM, ...)>(dlsym(t.lib, "XGetIMValues"));
    t.XVaCreateNestedList_ = reinterpret_cast<XVaNestedList (*)(int, ...)>(dlsym(t.lib, "XVaCreateNestedList"));   // optional: preedit only
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
class X11Win;
int PreeditStartCb(XIC, XPointer, XPointer);
void PreeditDoneCb(XIC, XPointer, XPointer);
void PreeditDrawCb(XIC, XPointer, XPointer);
void PreeditCaretCb(XIC, XPointer, XPointer);

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
    bool RequestAttention(bool on) override;
    unsigned DisplayDpi() const override;
    int RefreshRateHz() const override;
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
    bool m_urgent = false;
    std::unique_ptr<x11::xdnd::Target> m_dnd;   // drag and drop receive state (Xdnd)
    // Input method preedit: the composition text, and the callbacks the IC calls (only with OKAY_X11_PREEDIT=1).
    x11::Preedit m_pre;
    bool m_preeditOn = false;
    XIMCallback m_cbStart{}, m_cbDone{}, m_cbDraw{}, m_cbCaret{};
    GC m_gc = nullptr;                // PresentPixels: made on first use
    std::vector<uint8_t> m_present;   // PresentPixels: the frame in the server's pixel layout
};

class X11Impl final : public X11System {
public:
    ~X11Impl() override {
        if (m_blankCursor) m_x.XFreeCursor(m_dpy, m_blankCursor);
        for (Cursor c : m_shapeCursor) if (c) m_x.XFreeCursor(m_dpy, c);
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
        m_aIncr = m_x.XInternAtom(m_dpy, "INCR", False);
        m_aTextPlain = m_x.XInternAtom(m_dpy, "text/plain;charset=utf-8", False);
        for (int i = 0; i < 4; i++) {
            static const char* names[4] = {"XdndEnter", "XdndPosition", "XdndLeave", "XdndDrop"};
            m_aDnd[i] = m_x.XInternAtom(m_dpy, names[i], False);
        }
        m_aXdndStatus = m_x.XInternAtom(m_dpy, "XdndStatus", False);
        m_aXdndFinished = m_x.XInternAtom(m_dpy, "XdndFinished", False);
        m_aXdndAware = m_x.XInternAtom(m_dpy, "XdndAware", False);
        m_aXdndSelection = m_x.XInternAtom(m_dpy, "XdndSelection", False);
        m_aXdndTypeList = m_x.XInternAtom(m_dpy, "XdndTypeList", False);
        m_aXdndCopy = m_x.XInternAtom(m_dpy, "XdndActionCopy", False);
        m_aXdndProp = m_x.XInternAtom(m_dpy, "OKAY_XDND", False);
        m_aUriList = m_x.XInternAtom(m_dpy, "text/uri-list", False);
        m_aDemands = m_x.XInternAtom(m_dpy, "_NET_WM_STATE_DEMANDS_ATTENTION", False);
        m_aWorkArea = m_x.XInternAtom(m_dpy, "_NET_WORKAREA", False);
        m_aCurDesk = m_x.XInternAtom(m_dpy, "_NET_CURRENT_DESKTOP", False);
        m_aDialogType = m_x.XInternAtom(m_dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
        m_aWinType = m_x.XInternAtom(m_dpy, "_NET_WM_WINDOW_TYPE", False);
#ifdef OKAY_HAVE_XRANDR_H
        if (LoadXrr(m_xrr)) {
            int ev = 0, er = 0, maj = 0, mnr = 0;
            m_hasRandr = m_xrr.XRRQueryExtension(m_dpy, &ev, &er) && m_xrr.XRRQueryVersion(m_dpy, &maj, &mnr) &&
                         (maj > 1 || (maj == 1 && mnr >= 5));   // XRRGetMonitors is RandR 1.5
        }
#endif
        if (m_x.XSupportsLocale()) {
            m_x.XSetLocaleModifiers("");
            m_im = m_x.XOpenIM(m_dpy, nullptr, nullptr, nullptr);
            // Preedit text as TextEditing events needs the preedit-callbacks input style and is opt-in
            // (OKAY_X11_PREEDIT=1): an app that does not draw the composition itself would show nothing.
            const char* pe = std::getenv("OKAY_X11_PREEDIT");
            if (m_im && pe && pe[0] == '1' && m_x.XGetIMValues_ && m_x.XVaCreateNestedList_) {
                XIMStyles* styles = nullptr;
                if (m_x.XGetIMValues_(m_im, XNQueryInputStyle, &styles, (void*)nullptr) == nullptr && styles) {
                    for (unsigned short i = 0; i < styles->count_styles; i++)
                        if (styles->supported_styles[i] == (XIMPreeditCallbacks | XIMStatusNothing)) m_imPreedit = true;
                    m_x.XFree(styles);
                }
            }
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
        {   // drag and drop: announce Xdnd version 5 and make the receive state machine
            Atom ver = (Atom)x11::xdnd::kVersion;
            m_x.XChangeProperty(m_dpy, w, m_aXdndAware, XA_ATOM, 32, PropModeReplace, (const unsigned char*)&ver, 1);
            x11::xdnd::Atoms at; at.uriList = m_aUriList; at.actionCopy = m_aXdndCopy;
            win->m_dnd.reset(new x11::xdnd::Target((uint32_t)w, at));
        }
        if (m_im) {
            if (m_imPreedit) {
                win->m_cbStart.client_data = win->m_cbDone.client_data = win->m_cbDraw.client_data = win->m_cbCaret.client_data = (XPointer)win;
                win->m_cbStart.callback = (XIMProc)(void*)PreeditStartCb; win->m_cbDone.callback = (XIMProc)(void*)PreeditDoneCb;
                win->m_cbDraw.callback = (XIMProc)(void*)PreeditDrawCb; win->m_cbCaret.callback = (XIMProc)(void*)PreeditCaretCb;
                XVaNestedList nl = m_x.XVaCreateNestedList_(0, XNPreeditStartCallback, &win->m_cbStart, XNPreeditDoneCallback, &win->m_cbDone,
                                                            XNPreeditDrawCallback, &win->m_cbDraw, XNPreeditCaretCallback, &win->m_cbCaret, (void*)nullptr);
                if (nl) {
                    win->m_ic = m_x.XCreateIC_(m_im, XNInputStyle, XIMPreeditCallbacks | XIMStatusNothing,
                                               XNClientWindow, w, XNFocusWindow, w, XNPreeditAttributes, nl, (void*)nullptr);
                    m_x.XFree(nl);
                    win->m_preeditOn = win->m_ic != nullptr;
                }
            }
            if (!win->m_ic)
                win->m_ic = m_x.XCreateIC_(m_im, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                                           XNClientWindow, w, XNFocusWindow, w, (void*)nullptr);
        }
        if (m_shapeSet) HideCursorOn(w, m_cursorHidden || m_relative);
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

    void PushEditing(X11Win* w) {   // a TextEditing event for the window's current composition (only while text input is on)
        if (!m_textInput) return;
        m_pending.Push(w->m_pre.MakeEvent((uint32_t)w->m_win, (uint32_t)NowMs()));
    }
    void Unregister(::Window w) { m_wins.erase(w); if (m_focus == w) m_focus = 0; }

    // ---- events ---------------------------------------------------------------------
    static constexpr int kDrainPerPoll = 2048;
    bool PollEvent(Event& out) override {
        // Everything the server has sent goes into the queue first (at most kDrainPerPoll events, so a flood
        // cannot starve the app), then one event comes out. Moves, wheel notches and resizes that arrive
        // together are merged by the queue.
        for (int n = 0; n < kDrainPerPoll && m_x.XPending(m_dpy); ++n) {
            XEvent ev;
            m_x.XNextEvent(m_dpy, &ev);
            if (m_x.XFilterEvent(&ev, None)) continue;   // eaten by the input method
            Handle(ev);
        }
        for (auto& kv : m_wins)
            if (kv.second->m_dnd && kv.second->m_dnd->Active()) ApplyDnd(kv.second, kv.second->m_dnd->OnTick(NowMs()));
        return m_pending.Poll(out);
    }

    void Handle(XEvent& ev) {
        auto it = m_wins.find(ev.xany.window);
        X11Win* win = it == m_wins.end() ? nullptr : it->second;
        Event e;
        e.timestampMs = (uint32_t)NowMs();
        if (win) e.windowId = (uint32_t)win->m_win;
        switch (ev.type) {
        case ClientMessage:
            if (win && HandleDnd(ev.xclient, win)) break;
            if (win && ev.xclient.message_type == m_aProtocols && (Atom)ev.xclient.data.l[0] == m_aDelete) {
                e.type = EventType::WindowClose; m_pending.Push(e);
                if (m_wins.size() == 1) { Event q; q.type = EventType::Quit; q.timestampMs = e.timestampMs; m_pending.Push(q); }
            }
            break;
        case ConfigureNotify:
            if (win) {
                if (ev.xconfigure.width != win->m_w || ev.xconfigure.height != win->m_h) {
                    win->m_w = ev.xconfigure.width; win->m_h = ev.xconfigure.height;
                    e.type = EventType::WindowResized; e.width = win->m_w; e.height = win->m_h;
                    m_pending.Push(e);
                }
            }
            break;
        case FocusIn:
            if (win && ev.xfocus.mode != NotifyGrab && ev.xfocus.mode != NotifyUngrab) {
                win->m_focused = true; m_focus = win->m_win;
                if (win->m_urgent) win->RequestAttention(false);   // "until focused"
                if (win->m_ic) m_x.XSetICFocus(win->m_ic);
                e.type = EventType::WindowFocusGained; m_pending.Push(e);
            }
            break;
        case FocusOut:
            if (win && ev.xfocus.mode != NotifyGrab && ev.xfocus.mode != NotifyUngrab) {
                win->m_focused = false; if (m_focus == win->m_win) m_focus = 0;
                if (win->m_ic) m_x.XUnsetICFocus(win->m_ic);
                e.type = EventType::WindowFocusLost; m_pending.Push(e);
            }
            break;
        case Expose:
            if (win && ev.xexpose.count == 0) { e.type = EventType::WindowExposed; m_pending.Push(e); }
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
                m_pending.Push(e);
            }
            break;
        case MappingNotify:   // the keyboard layout changed: Xlib's cached keymap must follow
            if (ev.xmapping.request != MappingPointer) m_x.XRefreshKeyboardMapping(&ev.xmapping);
            break;
        case SelectionRequest: HandleSelectionRequest(ev.xselectionrequest); break;
        case SelectionClear: m_ownsClip = false; break;
        case SelectionNotify:
            if (ev.xselection.selection == m_aXdndSelection) { HandleDndData(ev.xselection); break; }
            m_selNotify = true; m_selProp = ev.xselection.property;
            break;
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
                    e.type = EventType::KeyDown; e.repeat = true; m_pending.Push(e);
                    EmitText(drop, win, e);
                    return;
                }
            }
            e.type = EventType::KeyUp; m_pending.Push(e);
            return;
        }
        e.type = EventType::KeyDown; m_pending.Push(e);
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
        m_pending.Push(t);
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
            m_pending.Push(e);
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
        m_pending.Push(e);
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
        if (!hide) {
            if (m_shapeSet && m_shapeCursor[m_shape]) m_x.XDefineCursor(m_dpy, w, m_shapeCursor[m_shape]);
            else m_x.XUndefineCursor(m_dpy, w);
            return;
        }
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

    // ---- cursor shapes --------------------------------------------------------------------
    bool SetCursor(CursorShape shape) override {
        const int i = (int)shape;
        if (i < 0 || i >= kShapes) return false;
        if (!m_shapeCursor[i]) {
            g_lastXError = 0;
            Cursor c = m_x.XCreateFontCursor(m_dpy, x11::CursorGlyph(shape));
            m_x.XSync(m_dpy, False);
            if (g_lastXError) { g_lastXError = 0; if (c) m_x.XFreeCursor(m_dpy, c); c = 0; }   // no "cursor" font on this server
            if (!c) return false;
            m_shapeCursor[i] = c;
        }
        m_shape = i; m_shapeSet = true;
        for (auto& kv : m_wins) HideCursorOn(kv.first, m_cursorHidden || m_relative);
        m_x.XFlush(m_dpy);
        return true;
    }

    // ---- monitors and scale -----------------------------------------------------------------
    std::vector<x11::Monitor> QueryMonitors() {
        std::vector<x11::Monitor> mons;
#ifdef OKAY_HAVE_XRANDR_H
        if (m_hasRandr) {
            int n = 0;
            XRRMonitorInfo* mi = m_xrr.XRRGetMonitors(m_dpy, m_root, True, &n);
            if (mi) {
                XRRScreenResources* res = m_xrr.XRRGetScreenResourcesCurrent(m_dpy, m_root);
                for (int i = 0; i < n; i++) {
                    x11::Monitor m;
                    m.x = mi[i].x; m.y = mi[i].y; m.width = mi[i].width; m.height = mi[i].height;
                    m.mmWidth = mi[i].mwidth; m.mmHeight = mi[i].mheight; m.primary = mi[i].primary != 0;
                    if (mi[i].name) if (char* nm = m_x.XGetAtomName(m_dpy, mi[i].name)) { m.name = nm; m_x.XFree(nm); }
                    if (res && mi[i].noutput > 0) {   // refresh rate of the mode the first output's CRTC runs
                        if (XRROutputInfo* oi = m_xrr.XRRGetOutputInfo(m_dpy, res, mi[i].outputs[0])) {
                            if (oi->crtc) if (XRRCrtcInfo* ci = m_xrr.XRRGetCrtcInfo(m_dpy, res, oi->crtc)) {
                                for (int k = 0; k < res->nmode; k++)
                                    if (res->modes[k].id == ci->mode)
                                        m.refreshHz = x11::RefreshHzFromTiming(res->modes[k].dotClock, res->modes[k].hTotal, res->modes[k].vTotal,
                                                                               (res->modes[k].modeFlags & RR_Interlace) != 0, (res->modes[k].modeFlags & RR_DoubleScan) != 0);
                                m_xrr.XRRFreeCrtcInfo(ci);
                            }
                            m_xrr.XRRFreeOutputInfo(oi);
                        }
                    }
                    mons.push_back(m);
                }
                if (res) m_xrr.XRRFreeScreenResources(res);
                m_xrr.XRRFreeMonitors(mi);
            }
        }
#endif
        if (mons.empty()) {   // no RandR 1.5: the one X screen
            x11::Monitor m;
            m.width = DisplayWidth(m_dpy, m_screen); m.height = DisplayHeight(m_dpy, m_screen);
            m.mmWidth = DisplayWidthMM(m_dpy, m_screen); m.mmHeight = DisplayHeightMM(m_dpy, m_screen);
            m.primary = true; m.name = "screen";
            mons.push_back(m);
        }
        return mons;
    }
    // _NET_WORKAREA of the current desktop (x, y, w, h), or zeros when the window manager sets none.
    void QueryWorkArea(int out[4]) {
        out[0] = out[1] = out[2] = out[3] = 0;
        Atom type; int fmt; unsigned long n, rest; unsigned char* data = nullptr;
        int desk = 0;
        if (m_x.XGetWindowProperty(m_dpy, m_root, m_aCurDesk, 0, 1, False, XA_CARDINAL, &type, &fmt, &n, &rest, &data) == Success && data) {
            if (fmt == 32 && n >= 1) desk = (int)*(long*)data;
            m_x.XFree(data);
        }
        data = nullptr;
        if (desk < 0 || desk > 255) desk = 0;
        if (m_x.XGetWindowProperty(m_dpy, m_root, m_aWorkArea, 0, 4 * 256, False, XA_CARDINAL, &type, &fmt, &n, &rest, &data) == Success && data) {
            if (fmt == 32 && n >= (unsigned long)(desk * 4 + 4)) { const long* v = (const long*)data; for (int i = 0; i < 4; i++) out[i] = (int)v[desk * 4 + i]; }
            m_x.XFree(data);
        }
    }
    // Xft.dpi from the RESOURCE_MANAGER property of the root window, read afresh (Xlib's XResourceManagerString
    // keeps the value from the time the display was opened, so a later change by the desktop would be missed).
    unsigned QueryXftDpi() {
        Atom type; int fmt; unsigned long n, rest; unsigned char* data = nullptr;
        unsigned dpi = 0;
        if (m_x.XGetWindowProperty(m_dpy, m_root, XA_RESOURCE_MANAGER, 0, 1 << 16, False, XA_STRING, &type, &fmt, &n, &rest, &data) == Success && data) {
            if (fmt == 8) dpi = x11::ParseXftDpi(std::string((const char*)data, n));
            m_x.XFree(data);
        }
        return dpi;
    }
    const std::vector<DisplayInfo>& Displays() {
        const uint64_t now = NowMs();
        if (m_displays.empty() || now - m_displaysAt > 500) {
            int wa[4];
            QueryWorkArea(wa);
            m_displays = x11::BuildDisplays(QueryMonitors(), QueryXftDpi(), wa);
            m_displaysAt = now;
        }
        return m_displays;
    }
    int DisplayCount() override { return (int)Displays().size(); }
    bool GetDisplayInfo(int index, DisplayInfo& out) override {
        const auto& d = Displays();
        if (index < 0 || index >= (int)d.size()) return false;
        out = d[(size_t)index];
        return true;
    }
    int DisplayIndexOfWindow(X11Win* w);

    // ---- drag and drop (Xdnd, receive side) ---------------------------------------------------
    void SendClientMessage(::Window to, Atom type, const long data[5], ::Window win) {
        XEvent r; std::memset(&r, 0, sizeof(r));
        r.xclient.type = ClientMessage;
        r.xclient.display = m_dpy;
        r.xclient.window = win;
        r.xclient.message_type = type;
        r.xclient.format = 32;
        for (int i = 0; i < 5; i++) r.xclient.data.l[i] = data[i];
        m_x.XSendEvent(m_dpy, to, False, NoEventMask, &r);
        m_x.XFlush(m_dpy);
    }
    void ApplyDnd(X11Win* win, const x11::xdnd::Reply& r);
    bool HandleDnd(XClientMessageEvent& xc, X11Win* win) {
        const Atom t = xc.message_type;
        if (t != m_aDnd[0] && t != m_aDnd[1] && t != m_aDnd[2] && t != m_aDnd[3]) return false;
        if (!win->m_dnd || xc.format != 32) return true;
        if (t == m_aDnd[0]) {   // XdndEnter
            const x11::xdnd::Enter en = x11::xdnd::DecodeEnter(xc.data.l);
            std::vector<unsigned long> types;
            if (en.moreTypes) {   // the whole list is a property of the source window
                Atom type; int fmt; unsigned long n, rest; unsigned char* data = nullptr;
                g_lastXError = 0;
                if (m_x.XGetWindowProperty(m_dpy, (::Window)en.source, m_aXdndTypeList, 0, 1024, False, XA_ATOM, &type, &fmt, &n, &rest, &data) == Success && data) {
                    if (fmt == 32) for (unsigned long i = 0; i < n && i < 1024; i++) types.push_back((unsigned long)((long*)data)[i]);
                    m_x.XFree(data);
                }
                g_lastXError = 0;
            } else {
                for (int i = 0; i < 3; i++) if (en.types[i]) types.push_back(en.types[i]);
            }
            ApplyDnd(win, win->m_dnd->OnEnter(en, types));
        } else if (t == m_aDnd[1]) {   // XdndPosition
            const x11::xdnd::Position p = x11::xdnd::DecodePosition(xc.data.l);
            int wx = 0, wy = 0; ::Window child;
            m_x.XTranslateCoordinates(m_dpy, m_root, win->m_win, p.rootX, p.rootY, &wx, &wy, &child);
            ApplyDnd(win, win->m_dnd->OnPosition(p, wx, wy));
        } else if (t == m_aDnd[2]) {   // XdndLeave
            ApplyDnd(win, win->m_dnd->OnLeave((unsigned long)xc.data.l[0]));
        } else {                        // XdndDrop
            ApplyDnd(win, win->m_dnd->OnDrop((unsigned long)xc.data.l[0], (unsigned long)xc.data.l[2], NowMs()));
        }
        return true;
    }
    void HandleDndData(XSelectionEvent& se) {
        auto it = m_wins.find(se.requestor);
        if (it == m_wins.end() || !it->second->m_dnd) return;
        X11Win* win = it->second;
        std::string bytes;
        bool ok = false;
        if (se.property != None) {
            Atom type; int fmt; unsigned long n, rest; unsigned char* data = nullptr;
            if (m_x.XGetWindowProperty(m_dpy, win->m_win, se.property, 0, 1 << 20, True, AnyPropertyType, &type, &fmt, &n, &rest, &data) == Success && data) {
                if (type != m_aIncr && fmt == 8) { bytes.assign((const char*)data, n); ok = true; }
                m_x.XFree(data);
            }
        }
        ApplyDnd(win, win->m_dnd->OnData(ok, bytes, (uint32_t)NowMs()));
    }

    // ---- message box -----------------------------------------------------------------------------
    bool RunDialogProgram(const std::string& title, const std::string& text) {
        std::vector<std::vector<std::string>> cmds = {
            {"zenity", "--error", "--no-markup", "--title", title, "--text", text},
            {"kdialog", "--title", title, "--error", text},
            {"xmessage", "-center", "-buttons", "OK", title + "\n\n" + text},
        };
        for (auto& c : cmds) {
            const pid_t pid = fork();
            if (pid < 0) return false;
            if (pid == 0) {
                std::vector<char*> argv;
                for (auto& a : c) argv.push_back(const_cast<char*>(a.c_str()));
                argv.push_back(nullptr);
                if (FILE* f = std::freopen("/dev/null", "w", stdout)) (void)f;
                if (FILE* f = std::freopen("/dev/null", "w", stderr)) (void)f;
                execvp(argv[0], argv.data());
                _exit(127);   // not installed: the parent tries the next one
            }
            int st = 0;
            while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
            if (WIFEXITED(st) && WEXITSTATUS(st) != 127) return true;
        }
        return false;
    }
    bool MessageBox(const std::string& title, const std::string& text, Window* parent) override {
        XFontStruct* fs = nullptr;
        for (const char* name : {"fixed", "-misc-fixed-medium-r-normal--13-120-75-75-c-70-iso8859-1", "9x15", "6x13"}) {
            g_lastXError = 0;
            fs = m_x.XLoadQueryFont(m_dpy, name);
            m_x.XSync(m_dpy, False);
            g_lastXError = 0;
            if (fs) break;
        }
        if (!fs) return RunDialogProgram(title, text);
        const int charW = std::max(1, m_x.XTextWidth(fs, "M", 1));
        const int lineH = fs->ascent + fs->descent + 3;
        const int sw = DisplayWidth(m_dpy, m_screen), sh = DisplayHeight(m_dpy, m_screen);
        const int cols = std::max(20, std::min(100, (sw * 3 / 4) / charW - 4));
        std::vector<std::string> lines = x11::WrapText(text.empty() ? std::string(" ") : text, cols, std::max(3, (sh * 3 / 4) / lineH - 5));
        for (auto& l : lines) l = x11::ToLatin1(l);
        int maxChars = 0;
        for (auto& l : lines) maxChars = std::max(maxChars, (int)l.size());
        int ax = 0, ay = 0, aw = sw, ah = sh;
        ::Window parentWin = 0;
        if (parent) {
            parentWin = (::Window)(uintptr_t)parent->NativeHandle();
            int px, py, pw, ph;
            parent->GetPosition(px, py); parent->GetSize(pw, ph);
            if (pw > 0 && ph > 0) { ax = px; ay = py; aw = pw; ah = ph; }
        }
        const x11::DialogLayout L = x11::LayoutDialog((int)lines.size(), maxChars, charW, lineH, fs->ascent, ax, ay, aw, ah);
        XSetWindowAttributes swa; std::memset(&swa, 0, sizeof(swa));
        swa.background_pixel = WhitePixel(m_dpy, m_screen);
        swa.border_pixel = BlackPixel(m_dpy, m_screen);
        swa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask;
        ::Window dlg = m_x.XCreateWindow(m_dpy, m_root, std::max(0, L.winX), std::max(0, L.winY), (unsigned)L.width, (unsigned)L.height, 1,
                                         DefaultDepth(m_dpy, m_screen), InputOutput, DefaultVisual(m_dpy, m_screen),
                                         CWBackPixel | CWBorderPixel | CWEventMask, &swa);
        if (!dlg) { m_x.XFreeFont(m_dpy, fs); return RunDialogProgram(title, text); }
        m_x.XStoreName(m_dpy, dlg, title.c_str());
        m_x.XChangeProperty(m_dpy, dlg, m_aNetName, m_aUtf8, 8, PropModeReplace, (const unsigned char*)title.data(), (int)title.size());
        Atom dt = m_aDialogType;
        m_x.XChangeProperty(m_dpy, dlg, m_aWinType, XA_ATOM, 32, PropModeReplace, (const unsigned char*)&dt, 1);
        Atom protos[1] = {m_aDelete};
        m_x.XSetWMProtocols(m_dpy, dlg, protos, 1);
        if (parentWin) m_x.XSetTransientForHint(m_dpy, dlg, parentWin);
        if (XSizeHints* nh = m_x.XAllocSizeHints()) {   // fixed size and position
            nh->flags = PPosition | PSize | PMinSize | PMaxSize;
            nh->x = std::max(0, L.winX); nh->y = std::max(0, L.winY);
            nh->width = nh->min_width = nh->max_width = L.width; nh->height = nh->min_height = nh->max_height = L.height;
            m_x.XSetWMNormalHints(m_dpy, dlg, nh);
            m_x.XFree(nh);
        }
        GC gc = m_x.XCreateGC(m_dpy, dlg, 0, nullptr);
        m_x.XSetFont(m_dpy, gc, fs->fid);
        m_x.XMapWindow(m_dpy, dlg);
        m_x.XSync(m_dpy, False);
        g_lastXError = 0;
        m_x.XSetInputFocus(m_dpy, dlg, RevertToParent, CurrentTime);   // no window manager to do it for us
        m_x.XSync(m_dpy, False);
        g_lastXError = 0;
        bool done = false, pressed = false, hover = false;
        auto draw = [&]() {
            m_x.XClearWindow(m_dpy, dlg);
            m_x.XSetForeground(m_dpy, gc, BlackPixel(m_dpy, m_screen));
            for (size_t i = 0; i < lines.size(); i++)
                m_x.XDrawString(m_dpy, dlg, gc, L.textX, L.textY + (int)i * L.lineHeight, lines[i].data(), (int)lines[i].size());
            const bool inv = hover || pressed;
            if (inv) m_x.XFillRectangle(m_dpy, dlg, gc, L.btnX, L.btnY, (unsigned)L.btnW, (unsigned)L.btnH);
            else m_x.XDrawRectangle(m_dpy, dlg, gc, L.btnX, L.btnY, (unsigned)L.btnW - 1, (unsigned)L.btnH - 1);
            m_x.XSetForeground(m_dpy, gc, inv ? WhitePixel(m_dpy, m_screen) : BlackPixel(m_dpy, m_screen));
            const int tw = m_x.XTextWidth(fs, "OK", 2);
            m_x.XDrawString(m_dpy, dlg, gc, L.btnX + (L.btnW - tw) / 2, L.btnY + (L.btnH + fs->ascent - fs->descent) / 2, "OK", 2);
            m_x.XFlush(m_dpy);
        };
        while (!done) {
            while (m_x.XPending(m_dpy)) {
                XEvent ev;
                m_x.XNextEvent(m_dpy, &ev);
                if (ev.xany.window != dlg) { if (!m_x.XFilterEvent(&ev, None)) Handle(ev); continue; }   // the app's windows keep being served
                switch (ev.type) {
                case Expose: if (ev.xexpose.count == 0) draw(); break;
                case KeyPress: if (x11::DialogKeyCloses((unsigned long)m_x.XLookupKeysym(&ev.xkey, 0))) done = true; break;
                case ButtonPress: if (ev.xbutton.button == 1 && L.InButton(ev.xbutton.x, ev.xbutton.y)) { pressed = true; draw(); } break;
                case ButtonRelease:
                    if (ev.xbutton.button == 1 && pressed) { pressed = false; if (L.InButton(ev.xbutton.x, ev.xbutton.y)) done = true; else draw(); }
                    break;
                case MotionNotify: {
                    const bool h = L.InButton(ev.xmotion.x, ev.xmotion.y);
                    if (h != hover) { hover = h; draw(); }
                    break;
                }
                case ClientMessage:
                    if (ev.xclient.message_type == m_aProtocols && (Atom)ev.xclient.data.l[0] == m_aDelete) done = true;
                    break;
                default: break;
                }
            }
            if (!done) { struct timespec ts = {0, 5000000}; nanosleep(&ts, nullptr); }
        }
        m_x.XFreeGC(m_dpy, gc);
        m_x.XFreeFont(m_dpy, fs);
        m_x.XDestroyWindow(m_dpy, dlg);
        m_x.XSync(m_dpy, False);
        return true;
    }

    // ---- clipboard ---------------------------------------------------------------------
    // What we offer lives in a ClipboardProvider (Exchange.hpp), one format at a time. We own the CLIPBOARD
    // selection after a Set and answer requests while events are pumped (PollEvent / the Get calls). Reading
    // someone else's selection sends a conversion request and waits up to a second for the answer.
    // Text is also offered as UTF8_STRING, STRING and text/plain;charset=utf-8; any other type goes out under
    // its MIME name as the target atom. Not done: INCR (selections too large for one property), MULTIPLE.
    bool SetClipboardText(const std::string& t) override {
        if (!IsValidUtf8(t)) return false;
        return ClipboardPut(kMimeText, std::vector<uint8_t>(t.begin(), t.end()));
    }
    bool ClipboardPut(const std::string& mime, const std::vector<uint8_t>& data) override {
        ::Window w = AnyWindow();
        if (!w || mime.empty() || mime.size() > 200) return false;
        if (mime == kMimeText && !IsValidUtf8(std::string(data.begin(), data.end()))) return false;
        if (!m_clipData.Set(mime, data)) return false;
        m_x.XSetSelectionOwner(m_dpy, m_aClipboard, w, CurrentTime);
        m_ownsClip = m_x.XGetSelectionOwner(m_dpy, m_aClipboard) == w;
        m_x.XFlush(m_dpy);
        return m_ownsClip;
    }
    std::string GetClipboardText() override {
        std::vector<uint8_t> d;
        if (!ClipboardFetch(kMimeText, d)) return "";
        return std::string(d.begin(), d.end());
    }
    // True while a window of ours owns the clipboard (after processing whatever the server sent).
    bool OwnClipboardNow(::Window& owner) {
        owner = m_x.XGetSelectionOwner(m_dpy, m_aClipboard);
        while (m_x.XPending(m_dpy)) { XEvent ev; m_x.XNextEvent(m_dpy, &ev); Handle(ev); }   // e.g. SelectionClear
        if (m_ownsClip && m_wins.count(owner)) return true;
        m_ownsClip = false;
        return false;
    }
    // Ask the owner to convert the selection to `target`; the answer is the property bytes. False when refused,
    // empty, too late (1 s) or INCR.
    bool RequestSelection(Atom target, ::Window w, std::vector<uint8_t>& out, Atom* typeOut = nullptr) {
        m_selNotify = false;
        m_x.XDeleteProperty(m_dpy, w, m_aProp);
        m_x.XConvertSelection(m_dpy, m_aClipboard, target, m_aProp, w, CurrentTime);
        m_x.XFlush(m_dpy);
        const uint64_t t0 = NowMs();
        while (!m_selNotify && NowMs() - t0 < 1000) {
            while (m_x.XPending(m_dpy)) { XEvent ev; m_x.XNextEvent(m_dpy, &ev); Handle(ev); }
            if (!m_selNotify) { struct timespec ts = {0, 2000000}; nanosleep(&ts, nullptr); }
        }
        if (!m_selNotify || m_selProp == None) return false;
        Atom type; int fmt; unsigned long n, rest; unsigned char* data = nullptr;
        bool ok = false;
        if (m_x.XGetWindowProperty(m_dpy, w, m_aProp, 0, 1 << 22, True, AnyPropertyType, &type, &fmt, &n, &rest, &data) == Success && data) {
            if (typeOut) *typeOut = type;
            if (type != m_aIncr && (fmt == 8 || fmt == 32)) {
                const size_t bytes = fmt == 8 ? (size_t)n : (size_t)n * sizeof(long);   // format 32 arrives as C longs
                out.assign(data, data + bytes);
                ok = true;
            }
            m_x.XFree(data);
        }
        return ok;
    }
    bool ClipboardFetch(const std::string& mime, std::vector<uint8_t>& out) override {
        ::Window w = AnyWindow();
        if (!w || mime.empty()) return false;
        ::Window owner;
        if (OwnClipboardNow(owner)) return m_clipData.Get(mime, out);
        if (owner == None) return false;
        const Atom target = mime == kMimeText ? m_aUtf8 : m_x.XInternAtom(m_dpy, mime.c_str(), False);
        return RequestSelection(target, w, out);
    }
    std::vector<std::string> ClipboardFormats() override {
        ::Window w = AnyWindow();
        if (!w) return {};
        ::Window owner;
        if (OwnClipboardNow(owner)) return m_clipData.Formats();
        std::vector<std::string> out;
        if (owner == None) return out;
        std::vector<uint8_t> raw;
        if (!RequestSelection(m_aTargets, w, raw)) return out;
        const Atom* atoms = reinterpret_cast<const Atom*>(raw.data());
        for (size_t i = 0; i < raw.size() / sizeof(Atom) && i < 256; i++) {
            if (atoms[i] == m_aUtf8 || atoms[i] == XA_STRING || atoms[i] == m_aTextPlain) { out.push_back(kMimeText); continue; }
            char* nm = m_x.XGetAtomName(m_dpy, atoms[i]);
            if (!nm) continue;
            const std::string name(nm);
            m_x.XFree(nm);
            if (name.find('/') != std::string::npos && name.size() <= 200) out.push_back(name);
        }
        // de-duplicate, keep order
        std::vector<std::string> uniq;
        for (auto& f : out) { bool dup = false; for (auto& u : uniq) if (u == f) dup = true; if (!dup) uniq.push_back(f); }
        return uniq;
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
            const std::vector<std::string> fmts = m_clipData.Formats();
            const bool haveText = m_clipData.Has(kMimeText);
            if (rq.target == m_aTargets) {
                std::vector<Atom> t = {m_aTargets};
                if (haveText) { t.push_back(m_aUtf8); t.push_back(XA_STRING); t.push_back(m_aTextPlain); }
                for (auto& f : fmts) if (f != kMimeText) t.push_back(m_x.XInternAtom(m_dpy, f.c_str(), False));
                m_x.XChangeProperty(m_dpy, rq.requestor, prop, XA_ATOM, 32, PropModeReplace, (unsigned char*)t.data(), (int)t.size());
                r.xselection.property = prop;
            } else {
                std::string mime;
                if (haveText && (rq.target == m_aUtf8 || rq.target == XA_STRING || rq.target == m_aTextPlain)) mime = kMimeText;
                else for (auto& f : fmts) if (m_x.XInternAtom(m_dpy, f.c_str(), False) == rq.target) mime = f;
                std::vector<uint8_t> data;
                // Bigger than one request can carry is refused (no INCR). 256 KiB is under the usual server limit.
                if (!mime.empty() && m_clipData.Get(mime, data) && data.size() <= 262144) {
                    m_x.XChangeProperty(m_dpy, rq.requestor, prop, rq.target, 8, PropModeReplace, data.data(), (int)data.size());
                    r.xselection.property = prop;
                }
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
         m_aClipboard = 0, m_aTargets = 0, m_aIncr = 0, m_aProp = 0, m_aTextPlain = 0;
    std::unordered_map<::Window, X11Win*> m_wins;
    EventQueue m_pending;   // bounded, merges mouse moves, wheel and resize (EventQueue.hpp)
    ::Window m_focus = 0;
    bool m_textInput = false, m_relative = false, m_warped = false, m_cursorHidden = false;
    Cursor m_blankCursor = 0;
    int m_lastX = 0, m_lastY = 0;
    int m_relAccX = 0, m_relAccY = 0;   // motion since RelativeMouseState was last read
    int m_lastBtn = 0, m_clicks = 0; unsigned long m_lastBtnTime = 0;
    MemoryClipboardProvider m_clipData;
    Atom m_aDnd[4] = {0, 0, 0, 0};   // XdndEnter, XdndPosition, XdndLeave, XdndDrop
    Atom m_aXdndStatus = 0, m_aXdndFinished = 0, m_aXdndAware = 0, m_aXdndSelection = 0, m_aXdndTypeList = 0, m_aXdndCopy = 0,
         m_aXdndProp = 0, m_aUriList = 0, m_aDemands = 0, m_aWorkArea = 0, m_aCurDesk = 0, m_aDialogType = 0, m_aWinType = 0;
    XrrTable m_xrr;
    bool m_hasRandr = false, m_imPreedit = false;
    std::vector<DisplayInfo> m_displays;
    uint64_t m_displaysAt = 0;
    static constexpr int kShapes = 11;
    Cursor m_shapeCursor[kShapes] = {};
    int m_shape = 0;
    bool m_shapeSet = false;
    bool m_ownsClip = false, m_selNotify = false;
    Atom m_selProp = 0;
};

// ---- members that need both classes -------------------------------------------------
int X11Impl::DisplayIndexOfWindow(X11Win* w) {
    int x = 0, y = 0, ww = 0, wh = 0;
    w->GetPosition(x, y); w->GetSize(ww, wh);
    return scale::DisplayForRect(Displays(), x, y, ww, wh);
}
void X11Impl::ApplyDnd(X11Win* win, const x11::xdnd::Reply& r) {
    if (r.sendStatus) {
        long d[5];
        x11::xdnd::EncodeStatus((unsigned long)win->m_win, r.accept, false, 0, 0, 0, 0, r.action, d);
        SendClientMessage((::Window)r.to, m_aXdndStatus, d, (::Window)r.to);
    }
    if (r.requestData) {
        m_x.XConvertSelection(m_dpy, m_aXdndSelection, (Atom)r.dataType, m_aXdndProp, win->m_win, r.time ? (Time)r.time : CurrentTime);
        m_x.XFlush(m_dpy);
    }
    for (const Event& e : r.events) m_pending.Push(e);
    if (r.sendFinished) {
        long d[5];
        x11::xdnd::EncodeFinished((unsigned long)win->m_win, r.finishedOk, r.action, d);
        SendClientMessage((::Window)r.to, m_aXdndFinished, d, (::Window)r.to);
    }
}

// Input method preedit callbacks (called by Xlib from inside XFilterEvent).
int PreeditStartCb(XIC, XPointer, XPointer) { return -1; }   // no length limit
void PreeditDrawCb(XIC, XPointer client, XPointer call) {
    X11Win* w = (X11Win*)client;
    XIMPreeditDrawCallbackStruct* d = (XIMPreeditDrawCallbackStruct*)call;
    if (!w || !d) return;
    std::string utf8;
    if (d->text) {
        const XIMText* t = d->text;
        if (t->encoding_is_wchar) {
            if (t->string.wide_char) for (unsigned short i = 0; i < t->length; i++) AppendUtf8(utf8, (uint32_t)t->string.wide_char[i]);
        } else if (t->string.multi_byte) {
            utf8 = t->string.multi_byte;
        }
        if (!IsValidUtf8(utf8)) utf8.clear();   // a non-UTF-8 locale: nothing we can show
    }
    w->m_pre.Draw(d->chg_first, d->chg_length, utf8, d->caret);
    w->m_sys->PushEditing(w);
}
void PreeditCaretCb(XIC, XPointer client, XPointer call) {
    X11Win* w = (X11Win*)client;
    XIMPreeditCaretCallbackStruct* c = (XIMPreeditCaretCallbackStruct*)call;
    if (!w || !c || c->direction != XIMAbsolutePosition) return;
    w->m_pre.Draw(w->m_pre.Length(), 0, "", c->position);   // moves the caret, text unchanged
    w->m_sys->PushEditing(w);
    c->position = w->m_pre.Caret();
}
void PreeditDoneCb(XIC, XPointer client, XPointer) {
    X11Win* w = (X11Win*)client;
    if (!w) return;
    const bool was = w->m_pre.Active();
    w->m_pre.Clear();
    if (was) w->m_sys->PushEditing(w);
}

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
bool X11Win::RequestAttention(bool on) {
    XWMHints* h = XF.XGetWMHints(XD, m_win);
    if (!h) { h = XF.XAllocWMHints(); if (!h) return false; h->flags = 0; }
    h->flags = x11::WithUrgency(h->flags, on);
    XF.XSetWMHints(XD, m_win, h);
    XF.XFree(h);
    XEvent ev; std::memset(&ev, 0, sizeof(ev));   // EWMH: ask the window manager to set / clear "demands attention"
    ev.xclient.type = ClientMessage; ev.xclient.window = m_win; ev.xclient.message_type = m_sys->m_aNetState; ev.xclient.format = 32;
    x11::NetWmStateMessage(on, m_sys->m_aDemands, 0, ev.xclient.data.l);
    XF.XSendEvent(XD, m_sys->m_root, False, SubstructureNotifyMask | SubstructureRedirectMask, &ev);
    XF.XFlush(XD);
    m_urgent = on;
    return true;
}
unsigned X11Win::DisplayDpi() const {
    const int i = m_sys->DisplayIndexOfWindow(const_cast<X11Win*>(this));
    const auto& d = m_sys->Displays();
    return i >= 0 && i < (int)d.size() ? d[(size_t)i].dpi : 96u;
}
int X11Win::RefreshRateHz() const {
    const int i = m_sys->DisplayIndexOfWindow(const_cast<X11Win*>(this));
    const auto& d = m_sys->Displays();
    return i >= 0 && i < (int)d.size() ? d[(size_t)i].refreshHz : 0;
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
