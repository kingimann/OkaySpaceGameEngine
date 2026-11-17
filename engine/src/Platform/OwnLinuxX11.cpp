// Own Linux backend, windowing part: Xlib + GLX through dlopen (libX11.so.6,
// libGL.so.1 / libGLX.so.0 are opened at run time; nothing is linked). The X11 and
// GL header files only supply types and constants; with them absent (no
// OKAY_PLATFORM_X11_HEADERS) this file is just "no display".
#include "OwnLinux.hpp"
#include "Internal.hpp"   // SortedVideoModes, DisplayIndexForRect

#ifndef OKAY_PLATFORM_X11_HEADERS
namespace okay::platform::own {
std::unique_ptr<X11System> X11System::Open(std::string& err) {
    err = "built without X11 headers; the own backend has no windowing in this build";
    return nullptr;
}
bool X11MessageBox(const std::string&, const std::string&, std::string& err) {
    err = "built without X11 headers";
    return false;
}
} // namespace okay::platform::own
#else

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <GL/glx.h>
#include "OwnLinuxXI2.hpp"   // XInput 2.2 touch (OKAY_X11_HAVE_XI2)
#include <dlfcn.h>
#include <algorithm>
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
    F(int, XMoveResizeWindow, (Display*, ::Window, int, int, unsigned, unsigned)) \
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
    F(Cursor, XCreateFontCursor, (Display*, unsigned)) \
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
    F(XWMHints*, XAllocWMHints, (void)) \
    F(XWMHints*, XGetWMHints, (Display*, ::Window)) \
    F(int, XSetWMHints, (Display*, ::Window, XWMHints*)) \
    F(int, XRefreshKeyboardMapping, (XMappingEvent*)) \
    F(int, XDrawString, (Display*, ::Drawable, GC, int, int, const char*, int)) \
    F(int, XFillRectangle, (Display*, ::Drawable, GC, int, int, unsigned, unsigned)) \
    F(int, XDrawRectangle, (Display*, ::Drawable, GC, int, int, unsigned, unsigned)) \
    F(int, XSetForeground, (Display*, GC, unsigned long)) \
    F(int, XSetFont, (Display*, GC, Font)) \
    F(XFontStruct*, XLoadQueryFont, (Display*, const char*)) \
    F(int, XFreeFont, (Display*, XFontStruct*)) \
    F(int, XTextWidth, (XFontStruct*, const char*, int))

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
    char* (*XSetICValues_)(XIC, ...) = nullptr;
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
    // Optional (the IME spot location): without them the input method keeps its own window placement.
    t.XSetICValues_ = reinterpret_cast<char* (*)(XIC, ...)>(dlsym(t.lib, "XSetICValues"));
    t.XGetIMValues_ = reinterpret_cast<char* (*)(XIM, ...)>(dlsym(t.lib, "XGetIMValues"));
    t.XVaCreateNestedList_ = reinterpret_cast<XVaNestedList (*)(int, ...)>(dlsym(t.lib, "XVaCreateNestedList"));
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
    bool IsMinimized() const override;
    bool IsVisible() const override { return !m_hidden; }
    bool HasMouseFocus() const override { return m_mouseInside; }
    bool SetInputFocus() override;
    bool IsFullscreen() const override { return m_fullscreen; }
    bool SetIcon(const uint8_t* rgba, int w, int h) override;
    bool Flash(FlashMode m) override;
    bool SetTextInputArea(int x, int y, int w, int h) override;
    int RefreshRateHz() const override;
    int DisplayIndex() const override;
    bool SetFullscreenMode(const VideoMode& mode) override;
    VideoMode FullscreenMode() const override { return m_mode; }
    void LeaveFullscreenMode();   // puts the CRTC's own mode and the window's old place back (SetFullscreen, the destructor)
    void SetUrgent(bool on);
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
    bool m_hidden = false;        // Hide() was called (or the window was made hidden): what IsVisible() reports
    bool m_mouseInside = false;   // the pointer is over the window (EnterNotify / LeaveNotify)
    bool m_fullscreen = false;
    bool m_resizable = true;
    int m_minW = 0, m_minH = 0;
    int m_w = 0, m_h = 0;      // last size seen in ConfigureNotify (or requested)
    XIC m_ic = nullptr;
    bool m_icSpot = false;            // the input context draws its preedit at a spot we give (XIMPreeditPosition)
    bool m_urgent = false;            // the urgency hint is set (Flash)
    uint64_t m_urgentUntil = 0;       // Flash(Briefly): when to clear it (ms, NowMs), 0 = until focused
    GC m_gc = nullptr;                // PresentPixels: made on first use
    std::vector<uint8_t> m_present;   // PresentPixels: the frame in the server's pixel layout
    VideoMode m_mode;                 // SetFullscreenMode: the mode switched to ({0,0,0} = none)
    int m_savedX = 0, m_savedY = 0, m_savedW = 0, m_savedH = 0;   // ...and where the window was before it
};

class X11Impl final : public X11System {
public:
    ~X11Impl() override {
        if (m_blankCursor) m_x.XFreeCursor(m_dpy, m_blankCursor);
        for (Cursor& fc : m_fontCursors) if (fc) { m_x.XFreeCursor(m_dpy, fc); fc = 0; }
        m_shapeCursor = 0;
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
        m_aXdndAware = m_x.XInternAtom(m_dpy, "XdndAware", False);
        m_aXdndEnter = m_x.XInternAtom(m_dpy, "XdndEnter", False);
        m_aXdndPosition = m_x.XInternAtom(m_dpy, "XdndPosition", False);
        m_aXdndStatus = m_x.XInternAtom(m_dpy, "XdndStatus", False);
        m_aXdndLeave = m_x.XInternAtom(m_dpy, "XdndLeave", False);
        m_aXdndDrop = m_x.XInternAtom(m_dpy, "XdndDrop", False);
        m_aXdndFinished = m_x.XInternAtom(m_dpy, "XdndFinished", False);
        m_aXdndSelection = m_x.XInternAtom(m_dpy, "XdndSelection", False);
        m_aXdndActionCopy = m_x.XInternAtom(m_dpy, "XdndActionCopy", False);
        m_aXdndTypeList = m_x.XInternAtom(m_dpy, "XdndTypeList", False);
        m_aUriList = m_x.XInternAtom(m_dpy, "text/uri-list", False);
        m_aDropProp = m_x.XInternAtom(m_dpy, "OKAY_XDND", False);
        if (m_x.XSupportsLocale()) {
            m_x.XSetLocaleModifiers("");
            m_im = m_x.XOpenIM(m_dpy, nullptr, nullptr, nullptr);
        }
        // Over-the-spot input (XIMPreeditPosition): the input method draws the text being composed, and its candidate list,
        // at a spot the program gives (SetTextInputArea). Used when the input method offers it; otherwise, or with
        // OKAY_X11_IME_SPOT=0, the root-window style (XIMPreeditNothing), where the input method places its own window.
        // From the Xlib manual, chapter 13 (input methods: XGetIMValues XNQueryInputStyle, XNSpotLocation).
        if (m_im && m_x.XGetIMValues_ && m_x.XSetICValues_ && m_x.XVaCreateNestedList_) {
            const char* env = std::getenv("OKAY_X11_IME_SPOT");
            if (!(env && std::strcmp(env, "0") == 0)) {
                XIMStyles* styles = nullptr;
                if (!m_x.XGetIMValues_(m_im, XNQueryInputStyle, &styles, (void*)nullptr) && styles) {
                    for (unsigned short i = 0; i < styles->count_styles; ++i)
                        if (styles->supported_styles[i] == (XIMPreeditPosition | XIMStatusNothing)) m_imSpot = true;
                    m_x.XFree(styles);
                }
            }
        }
        InitTouch();
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
                         PropertyChangeMask | EnterWindowMask | LeaveWindowMask;
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
        {   // files can be dropped on every window (XDND): the protocol version we speak
            const long version = kXdndVersion;
            m_x.XChangeProperty(m_dpy, w, m_aXdndAware, XA_ATOM, 32, PropModeReplace, (const unsigned char*)&version, 1);
        }
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
        if (m_im && m_imSpot) {   // over the spot, starting at the top-left corner until the program says where the caret is
            XPoint spot; spot.x = 0; spot.y = 0;
            XVaNestedList pre = m_x.XVaCreateNestedList_(0, XNSpotLocation, &spot, (void*)nullptr);
            if (pre) {
                win->m_ic = m_x.XCreateIC_(m_im, XNInputStyle, XIMPreeditPosition | XIMStatusNothing,
                                           XNClientWindow, w, XNFocusWindow, w, XNPreeditAttributes, pre, (void*)nullptr);
                m_x.XFree(pre);
            }
            win->m_icSpot = win->m_ic != nullptr;
        }
        if (m_im && !win->m_ic) {
            win->m_ic = m_x.XCreateIC_(m_im, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                                       XNClientWindow, w, XNFocusWindow, w, (void*)nullptr);
        }
        {   // start fullscreen / maximised: the initial _NET_WM_STATE is read when the window is mapped
            Atom st[2]; int n = 0;
            if (d.fullscreen) { st[n++] = m_aNetFull; win->m_fullscreen = true; }
            else if (d.maximized && d.resizable) { st[n++] = m_aMaxH; st[n++] = m_aMaxV; }
            if (n) m_x.XChangeProperty(m_dpy, w, m_aNetState, XA_ATOM, 32, PropModeReplace, (const unsigned char*)st, n);
        }
        SelectTouch(w);
        win->m_hidden = d.hidden;
        if (!d.hidden) win->Show();
        m_x.XSync(m_dpy, False);
        return std::unique_ptr<Window>(win);
    }

    void Unregister(::Window w) {
        m_wins.erase(w); if (m_focus == w) m_focus = 0;
#if OKAY_X11_HAVE_XI2
        m_touch.ForgetWindow((uint32_t)w);
#endif
    }

    // ---- touch (XInput 2.2) -------------------------------------------------------------
    // Written from the X Input Extension protocol specification 2.2 ("Touch device support"). A window that selects
    // XI_TouchBegin / XI_TouchUpdate / XI_TouchEnd for the master devices gets one event stream per finger (the touch id in
    // `detail`), and the server then stops emulating the pointer for that window; TouchTracker makes the mouse events from
    // the first finger again, as SDL does. libXi.so.6 is loaded at run time; without it, an X server older than XI 2.2, or
    // with OKAY_X11_TOUCH=0, nothing is selected and touch screens keep acting as the mouse (the server's own emulation).
#if OKAY_X11_HAVE_XI2
    void InitTouch() {
        const char* env = std::getenv("OKAY_X11_TOUCH");
        if (env && std::strcmp(env, "0") == 0) return;
        m_queryExtension = reinterpret_cast<Bool (*)(Display*, const char*, int*, int*, int*)>(dlsym(m_x.lib, "XQueryExtension"));
        m_getEventData = reinterpret_cast<Bool (*)(Display*, XGenericEventCookie*)>(dlsym(m_x.lib, "XGetEventData"));
        m_freeEventData = reinterpret_cast<void (*)(Display*, XGenericEventCookie*)>(dlsym(m_x.lib, "XFreeEventData"));
        if (!m_queryExtension || !m_getEventData || !m_freeEventData) return;
        void* xi = dlopen("libXi.so.6", RTLD_NOW | RTLD_GLOBAL);
        if (!xi) xi = dlopen("libXi.so", RTLD_NOW | RTLD_GLOBAL);
        if (!xi) return;
        m_xiQueryVersion = reinterpret_cast<Status (*)(Display*, int*, int*)>(dlsym(xi, "XIQueryVersion"));
        m_xiSelectEvents = reinterpret_cast<int (*)(Display*, ::Window, XIEventMask*, int)>(dlsym(xi, "XISelectEvents"));
        m_xiQueryDevice = reinterpret_cast<XIDeviceInfo* (*)(Display*, int, int*)>(dlsym(xi, "XIQueryDevice"));
        m_xiFreeDeviceInfo = reinterpret_cast<void (*)(XIDeviceInfo*)>(dlsym(xi, "XIFreeDeviceInfo"));
        if (!m_xiQueryVersion || !m_xiSelectEvents || !m_xiQueryDevice || !m_xiFreeDeviceInfo) return;
        int op = 0, evBase = 0, errBase = 0;
        if (!m_queryExtension(m_dpy, "XInputExtension", &op, &evBase, &errBase)) return;
        int major = 2, minor = 2;   // we speak 2.2; the server answers with what it has (at most that)
        if (m_xiQueryVersion(m_dpy, &major, &minor) != Success || major < 2 || (major == 2 && minor < 2)) return;
        m_xiOpcode = op;
        m_aMtPressure = m_x.XInternAtom(m_dpy, "Abs MT Pressure", False);
        m_aAbsPressure = m_x.XInternAtom(m_dpy, "Abs Pressure", False);
        m_xiTouch = true;
    }
    void SelectTouch(::Window w) {
        if (!m_xiTouch) return;
        unsigned char bits[XIMaskLen(XI_LASTEVENT)] = {};
        XISetMask(bits, XI_TouchBegin); XISetMask(bits, XI_TouchUpdate); XISetMask(bits, XI_TouchEnd);
        XIEventMask mask;
        mask.deviceid = XIAllMasterDevices;
        mask.mask_len = (int)sizeof(bits);
        mask.mask = bits;
        m_xiSelectEvents(m_dpy, w, &mask, 1);
    }
    XIPressureAxis PressureAxisFor(int sourceId) {
        auto it = m_pressureAxes.find(sourceId);
        if (it != m_pressureAxes.end()) return it->second;
        XIPressureAxis axis;
        int n = 0;
        if (XIDeviceInfo* info = m_xiQueryDevice(m_dpy, sourceId, &n)) {
            if (n > 0) axis = PressureAxisOf(info[0], m_aMtPressure, m_aAbsPressure);
            m_xiFreeDeviceInfo(info);
        }
        m_pressureAxes[sourceId] = axis;
        return axis;
    }
    void HandleGeneric(XEvent& ev) {
        if (!m_xiTouch || ev.xcookie.extension != m_xiOpcode) return;
        if (!m_getEventData(m_dpy, &ev.xcookie)) return;
        const int t = ev.xcookie.evtype;
        if (t == XI_TouchBegin || t == XI_TouchUpdate || t == XI_TouchEnd) {
            const XIDeviceEvent* de = static_cast<const XIDeviceEvent*>(ev.xcookie.data);
            auto it = m_wins.find(de->event);
            TouchSample s;
            if (it != m_wins.end() &&
                TouchSampleFromXI(*de, PressureAxisFor(de->sourceid), it->second->m_w, it->second->m_h,
                                  (uint32_t)it->second->m_win, (uint32_t)NowMs(), s)) {
                std::vector<Event> out;
                m_touch.Feed(s, out);
                for (Event& e : out) {
                    if (e.type == EventType::MouseMove) { m_lastX = e.x; m_lastY = e.y; }
                    m_pending.push_back(std::move(e));
                }
            }
        }
        m_freeEventData(m_dpy, &ev.xcookie);
    }
    int TouchDeviceCount() override {
        if (!m_xiTouch) return 0;
        const uint64_t now = NowMs();
        if (m_touchCountRead && now - m_touchCountAt < 2000) return m_touchCount;
        int n = 0;
        XIDeviceInfo* info = m_xiQueryDevice(m_dpy, XIAllDevices, &n);
        m_touchCount = info ? CountTouchDevices(info, n) : 0;
        if (info) m_xiFreeDeviceInfo(info);
        m_touchCountRead = true; m_touchCountAt = now;
        m_pressureAxes.clear();   // devices may have come or gone: read their axes again
        return m_touchCount;
    }
#else
    void InitTouch() {}
    void SelectTouch(::Window) {}
    void HandleGeneric(XEvent&) {}
    int TouchDeviceCount() override { return 0; }
#endif

    // ---- events ---------------------------------------------------------------------
    // Flash(Briefly): the urgency hints whose second is over.
    void ExpireUrgency() {
        const uint64_t now = NowMs();
        for (auto& kv : m_wins)
            if (kv.second->m_urgent && kv.second->m_urgentUntil && now >= kv.second->m_urgentUntil) kv.second->SetUrgent(false);
    }

    bool PollEvent(Event& out) override {
        ExpireUrgency();
        for (;;) {
            if (!m_pending.empty()) { out = std::move(m_pending.front()); m_pending.pop_front(); return true; }
            if (!m_x.XPending(m_dpy)) return false;
            XEvent ev;
            m_x.XNextEvent(m_dpy, &ev);
            if (m_x.XFilterEvent(&ev, None)) continue;   // eaten by the input method
            Handle(ev);
        }
    }

    void PumpEvents() override {
        ExpireUrgency();
        while (m_x.XPending(m_dpy)) {
            XEvent ev;
            m_x.XNextEvent(m_dpy, &ev);
            if (m_x.XFilterEvent(&ev, None)) continue;   // eaten by the input method
            Handle(ev);
        }
    }

    void Handle(XEvent& ev) {
        if (ev.type == GenericEvent) { HandleGeneric(ev); return; }   // extension events (XInput 2 touch): no xany.window
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
            } else if (win) HandleXdnd(ev.xclient, win);
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
                if (win->m_urgent) win->SetUrgent(false);   // Flash: the window has the user's attention now
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
        case EnterNotify:   // the pointer came over the window: HasMouseFocus / MouseFocusWindowId
            if (win) { win->m_mouseInside = true; m_mouseWin = win->m_win; }
            break;
        case LeaveNotify:
            if (win) { win->m_mouseInside = false; if (m_mouseWin == win->m_win) m_mouseWin = 0; }
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
        case SelectionNotify:
            if (ev.xselection.selection == m_aXdndSelection) { FinishDrop(ev.xselection, e.timestampMs); break; }
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

    // ---- file drops (XDND) ------------------------------------------------------------
    // Written from the XDND protocol description (freedesktop.org, "Drag-and-Drop Protocol for the X Window System", version 5):
    // a window that sets XdndAware gets XdndEnter / XdndPosition / XdndLeave / XdndDrop client messages from the drag source,
    // answers each XdndPosition with XdndStatus (accept or not, and the action), and on XdndDrop asks for the XdndSelection
    // selection as text/uri-list; when the data has come it sends XdndFinished. Each file:// URI becomes one DropFile event
    // (the path, percent-decoded), the same event the sdl2 backend makes from SDL_DROPFILE.
    static constexpr long kXdndVersion = 5;
    void SendXdnd(::Window to, Atom type, long l1, long l2, long l3, long l4, ::Window from) {
        XEvent r; std::memset(&r, 0, sizeof(r));
        r.xclient.type = ClientMessage;
        r.xclient.display = m_dpy;
        r.xclient.window = to;
        r.xclient.message_type = type;
        r.xclient.format = 32;
        r.xclient.data.l[0] = (long)from;
        r.xclient.data.l[1] = l1; r.xclient.data.l[2] = l2; r.xclient.data.l[3] = l3; r.xclient.data.l[4] = l4;
        m_x.XSendEvent(m_dpy, to, False, NoEventMask, &r);
        m_x.XFlush(m_dpy);
    }
    void HandleXdnd(const XClientMessageEvent& cm, X11Win* win) {
        const ::Window source = (::Window)cm.data.l[0];
        if (cm.message_type == m_aXdndEnter) {
            m_dnd = DndState();
            m_dnd.source = source;
            m_dnd.target = win->m_win;
            m_dnd.version = (int)((unsigned long)cm.data.l[1] >> 24);
            // Up to three types in the message; more are in the source's XdndTypeList property (bit 0 of l[1]).
            for (int i = 2; i <= 4; ++i) if ((Atom)cm.data.l[i] == m_aUriList) m_dnd.uriOffered = true;
            if (!m_dnd.uriOffered && (cm.data.l[1] & 1)) {
                Atom type = 0; int fmt = 0; unsigned long n = 0, after = 0; unsigned char* data = nullptr;
                if (m_x.XGetWindowProperty(m_dpy, source, m_aXdndTypeList, 0, 1024, False, XA_ATOM, &type, &fmt, &n, &after, &data) == Success && data) {
                    if (fmt == 32) for (unsigned long i = 0; i < n; ++i) if ((Atom)((long*)data)[i] == m_aUriList) m_dnd.uriOffered = true;
                    m_x.XFree(data);
                }
            }
        } else if (cm.message_type == m_aXdndPosition) {
            if (source != m_dnd.source) return;   // a position from a drag we never saw enter
            const bool accept = m_dnd.uriOffered && m_dnd.version <= kXdndVersion;
            // l[1] bit 0: we accept; bit 1: keep sending positions (an empty "no more messages" rectangle, l[2] / l[3] = 0).
            SendXdnd(source, m_aXdndStatus, accept ? 3 : 2, 0, 0, accept ? (long)m_aXdndActionCopy : 0L, win->m_win);
        } else if (cm.message_type == m_aXdndLeave) {
            if (source == m_dnd.source) m_dnd = DndState();
        } else if (cm.message_type == m_aXdndDrop) {
            if (source != m_dnd.source) return;
            if (!m_dnd.uriOffered) {   // nothing we can take: the source must still hear that the drop is over
                SendXdnd(source, m_aXdndFinished, 0, 0, 0, 0, win->m_win);
                m_dnd = DndState();
                return;
            }
            const Time t = m_dnd.version >= 1 ? (Time)cm.data.l[2] : CurrentTime;
            m_dnd.dropped = true;
            m_x.XDeleteProperty(m_dpy, win->m_win, m_aDropProp);
            m_x.XConvertSelection(m_dpy, m_aXdndSelection, m_aUriList, m_aDropProp, win->m_win, t);
            m_x.XFlush(m_dpy);
        }
    }
    void FinishDrop(const XSelectionEvent& se, uint32_t stamp) {
        if (!m_dnd.dropped || se.requestor != m_dnd.target) return;
        bool ok = false;
        if (se.property != None) {
            Atom type = 0; int fmt = 0; unsigned long n = 0, after = 0; unsigned char* data = nullptr;
            if (m_x.XGetWindowProperty(m_dpy, m_dnd.target, m_aDropProp, 0, 1 << 22, True, AnyPropertyType, &type, &fmt, &n, &after, &data) == Success && data) {
                if (fmt == 8) {
                    for (const std::string& path : PathsFromUriList(std::string((const char*)data, n))) {
                        Event d; d.type = EventType::DropFile; d.windowId = (uint32_t)m_dnd.target; d.timestampMs = stamp; d.text = path;
                        m_pending.push_back(d);
                        ok = true;
                    }
                }
                m_x.XFree(data);
            }
        }
        // XdndFinished: l[1] bit 0 = the drop was taken (version 5), l[2] = the action done.
        SendXdnd(m_dnd.source, m_aXdndFinished, ok ? 1 : 0, ok ? (long)m_aXdndActionCopy : 0L, 0, 0, m_dnd.target);
        m_dnd = DndState();
    }

    // ---- refresh rate -------------------------------------------------------------------
    // RandR's screen resources: the CRTC (a scan-out engine) that shows the window's centre and its mode's timings. Refresh =
    // pixel clock / (horizontal total x vertical total), doubled for an interlaced mode and halved for a double-scan one (the
    // X RandR protocol's mode description). The structs are laid out from libXrandr's public interface, as RrMonitor below.
    struct RrModeInfo {
        unsigned long id; unsigned width, height; unsigned long dotClock;
        unsigned hSyncStart, hSyncEnd, hTotal, hSkew, vSyncStart, vSyncEnd, vTotal;
        char* name; unsigned nameLength; unsigned long modeFlags;
    };
    struct RrScreenResources {
        Time timestamp, configTimestamp; int ncrtc; unsigned long* crtcs; int noutput; unsigned long* outputs;
        int nmode; RrModeInfo* modes;
    };
    struct RrCrtcInfo {
        Time timestamp; int x, y; unsigned width, height; unsigned long mode; unsigned short rotation; int noutput;
        unsigned long* outputs; unsigned short rotations; int npossible; unsigned long* possible;
    };
    static int RefreshFromMode(const RrModeInfo& m) {
        if (!m.dotClock || !m.hTotal || !m.vTotal) return 0;
        double hz = (double)m.dotClock / ((double)m.hTotal * (double)m.vTotal);
        if (m.modeFlags & 0x10) hz *= 2.0;   // RR_Interlace
        if (m.modeFlags & 0x20) hz /= 2.0;   // RR_DoubleScan
        return (int)(hz + 0.5);
    }
    int RefreshRateAt(int cx, int cy) {
        LoadDisplayLibs();
        if (!m_rrGetRes || !m_rrFreeRes || !m_rrGetCrtc || !m_rrFreeCrtc) return 0;
        int ev = 0, er = 0;
        if (!m_rrQueryExt || !m_rrQueryExt(m_dpy, &ev, &er)) return 0;
        RrScreenResources* res = m_rrGetRes(m_dpy, m_root);
        if (!res) return 0;
        int best = 0, first = 0;
        for (int i = 0; i < res->ncrtc && !best; ++i) {
            RrCrtcInfo* c = m_rrGetCrtc(m_dpy, res, res->crtcs[i]);
            if (!c) continue;
            if (c->mode) {
                for (int k = 0; k < res->nmode; ++k) {
                    if (res->modes[k].id != c->mode) continue;
                    const int hz = RefreshFromMode(res->modes[k]);
                    if (!first) first = hz;
                    if (cx >= c->x && cy >= c->y && cx < c->x + (int)c->width && cy < c->y + (int)c->height) best = hz;
                    break;
                }
            }
            m_rrFreeCrtc(c);
        }
        m_rrFreeRes(res);
        return best ? best : first;   // a window off every CRTC: the first lit one
    }

    // ---- display modes ------------------------------------------------------------------
    // A display (an entry of Displays()) is shown by a CRTC; its modes are the modes of that CRTC's outputs (the RandR
    // protocol: each output lists the modes it can show, the screen resources describe them). XRROutputInfo is laid out from
    // libXrandr's public interface (field order and types), like the structs above.
    struct RrOutputInfo {
        Time timestamp; unsigned long crtc; char* name; int nameLen; unsigned long mmWidth, mmHeight;
        unsigned short connection, subpixelOrder; int ncrtc; unsigned long* crtcs; int nclone; unsigned long* clones;
        int nmode, npreferred; unsigned long* modes;
    };
    struct ListedMode { VideoMode mode; unsigned long id; };
    /// What SetFullscreenMode changed, to put back: the CRTC, its old mode and place, and the screen size if it grew.
    struct ModeSwitch {
        unsigned long crtc = 0, oldMode = 0; int x = 0, y = 0; unsigned short rotation = 1;
        std::vector<unsigned long> outputs;
        int oldScreenW = 0, oldScreenH = 0, oldScreenMmW = 0, oldScreenMmH = 0; bool screenGrew = false;
    };
    bool LoadModeFunctions() {
        LoadDisplayLibs();
        if (!m_rrModeTried) {
            m_rrModeTried = true;
            if (void* rr = dlopen("libXrandr.so.2", RTLD_NOW | RTLD_GLOBAL)) {
                m_rrGetOutput = reinterpret_cast<RrOutputInfo* (*)(Display*, RrScreenResources*, unsigned long)>(dlsym(rr, "XRRGetOutputInfo"));
                m_rrFreeOutput = reinterpret_cast<void (*)(RrOutputInfo*)>(dlsym(rr, "XRRFreeOutputInfo"));
                m_rrSetCrtc = reinterpret_cast<Status (*)(Display*, RrScreenResources*, unsigned long, Time, int, int, unsigned long,
                                                          unsigned short, unsigned long*, int)>(dlsym(rr, "XRRSetCrtcConfig"));
                m_rrSetScreenSize = reinterpret_cast<void (*)(Display*, ::Window, int, int, int, int)>(dlsym(rr, "XRRSetScreenSize"));
            }
        }
        int ev = 0, er = 0;
        return m_rrGetRes && m_rrFreeRes && m_rrGetCrtc && m_rrFreeCrtc && m_rrGetOutput && m_rrFreeOutput &&
               m_rrQueryExt && m_rrQueryExt(m_dpy, &ev, &er);
    }
    static VideoMode ModeOf(const RrModeInfo& m, unsigned short rotation) {
        const bool sideways = (rotation & (2 | 8)) != 0;   // RR_Rotate_90 / RR_Rotate_270: the picture is turned on its side
        return VideoMode{(int)(sideways ? m.height : m.width), (int)(sideways ? m.width : m.height), RefreshFromMode(m)};
    }
    /// The CRTC that shows display `display`: the lit one at the display's top-left corner with its size, else the lit one
    /// holding the display's centre. 0 = none (no RandR, or the display is not a CRTC's picture).
    unsigned long CrtcForDisplay(RrScreenResources* res, int display) {
        const std::vector<DisplayInfo> ds = Displays();
        if (display < 0 || display >= (int)ds.size()) return 0;
        const DisplayRect b = ds[(size_t)display].bounds;
        unsigned long exact = 0, holds = 0;
        for (int i = 0; i < res->ncrtc && !exact; ++i) {
            RrCrtcInfo* c = m_rrGetCrtc(m_dpy, res, res->crtcs[i]);
            if (!c) continue;
            if (c->mode) {
                if (c->x == b.x && c->y == b.y && (int)c->width == b.w && (int)c->height == b.h) exact = res->crtcs[i];
                const int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
                if (!holds && cx >= c->x && cy >= c->y && cx < c->x + (int)c->width && cy < c->y + (int)c->height) holds = res->crtcs[i];
            }
            m_rrFreeCrtc(c);
        }
        return exact ? exact : holds;
    }
    /// The modes the outputs of `crtc` can show (each once, with its RandR mode id).
    std::vector<ListedMode> CrtcModes(RrScreenResources* res, RrCrtcInfo* c) {
        std::vector<ListedMode> out;
        for (int o = 0; o < c->noutput; ++o) {
            RrOutputInfo* oi = m_rrGetOutput(m_dpy, res, c->outputs[o]);
            if (!oi) continue;
            for (int k = 0; k < oi->nmode; ++k)
                for (int j = 0; j < res->nmode; ++j)
                    if (res->modes[j].id == oi->modes[k]) {
                        bool seen = false;
                        for (const ListedMode& l : out) seen |= l.id == res->modes[j].id;
                        if (!seen) out.push_back(ListedMode{ModeOf(res->modes[j], c->rotation), res->modes[j].id});
                        break;
                    }
            m_rrFreeOutput(oi);
        }
        return out;
    }
    std::vector<VideoMode> DisplayModes(int display) override {
        std::vector<VideoMode> out;
        if (!LoadModeFunctions()) return out;
        RrScreenResources* res = m_rrGetRes(m_dpy, m_root);
        if (!res) return out;
        if (const unsigned long crtc = CrtcForDisplay(res, display)) {
            if (RrCrtcInfo* c = m_rrGetCrtc(m_dpy, res, crtc)) {
                for (const ListedMode& l : CrtcModes(res, c)) out.push_back(l.mode);
                m_rrFreeCrtc(c);
            }
        }
        m_rrFreeRes(res);
        return SortedVideoModes(std::move(out));
    }
    bool CurrentDisplayMode(int display, VideoMode& outMode) override {
        if (!LoadModeFunctions()) return false;
        RrScreenResources* res = m_rrGetRes(m_dpy, m_root);
        if (!res) return false;
        bool ok = false;
        if (const unsigned long crtc = CrtcForDisplay(res, display)) {
            if (RrCrtcInfo* c = m_rrGetCrtc(m_dpy, res, crtc)) {
                for (int j = 0; j < res->nmode && !ok; ++j)
                    if (res->modes[j].id == c->mode) { outMode = ModeOf(res->modes[j], c->rotation); ok = true; }
                m_rrFreeCrtc(c);
            }
        }
        m_rrFreeRes(res);
        return ok;
    }
    /// Switch the CRTC of `display` to the listed mode closest to `want`. On success `sw` says how to put it back (filled only
    /// the first time: a second switch keeps the first one's "old" values) and `rect` is the CRTC's picture now.
    bool SwitchMode(int display, const VideoMode& want, ModeSwitch& sw, bool haveOld, VideoMode& picked, DisplayRect& rect) {
        if (!LoadModeFunctions() || !m_rrSetCrtc) return false;
        RrScreenResources* res = m_rrGetRes(m_dpy, m_root);
        if (!res) return false;
        bool ok = false;
        const unsigned long crtc = CrtcForDisplay(res, display);
        RrCrtcInfo* c = crtc ? m_rrGetCrtc(m_dpy, res, crtc) : nullptr;
        if (c) {
            const std::vector<ListedMode> listed = CrtcModes(res, c);
            std::vector<VideoMode> modes;
            for (const ListedMode& l : listed) modes.push_back(l.mode);
            const VideoMode pick = ClosestVideoMode(modes, want);
            unsigned long id = 0;
            for (const ListedMode& l : listed) if (l.mode == pick) { id = l.id; break; }
            if (id) {
                if (!haveOld) {
                    sw = ModeSwitch();
                    sw.crtc = crtc; sw.oldMode = c->mode; sw.x = c->x; sw.y = c->y; sw.rotation = c->rotation;
                    sw.outputs.assign(c->outputs, c->outputs + c->noutput);
                    RootSize(sw.oldScreenW, sw.oldScreenH);
                    sw.oldScreenMmW = DisplayWidthMM(m_dpy, m_screen); sw.oldScreenMmH = DisplayHeightMM(m_dpy, m_screen);
                }
                // A mode larger than the screen needs a larger screen first (the protocol refuses a CRTC that leaves it).
                const int needW = c->x + pick.width, needH = c->y + pick.height;
                int curW = 0, curH = 0;
                RootSize(curW, curH);
                g_lastXError = 0;
                if ((needW > curW || needH > curH) && m_rrSetScreenSize) {
                    const int nw = std::max(needW, curW), nh = std::max(needH, curH);
                    const int mmW = curW > 0 ? (int)((long long)DisplayWidthMM(m_dpy, m_screen) * nw / curW) : 0;
                    const int mmH = curH > 0 ? (int)((long long)DisplayHeightMM(m_dpy, m_screen) * nh / curH) : 0;
                    m_rrSetScreenSize(m_dpy, m_root, nw, nh, mmW, mmH);
                    m_x.XSync(m_dpy, False);
                    sw.screenGrew = g_lastXError == 0;
                }
                const Status st = m_rrSetCrtc(m_dpy, res, crtc, CurrentTime, c->x, c->y, id, c->rotation, c->outputs, c->noutput);
                m_x.XSync(m_dpy, False);
                ok = st == 0 && g_lastXError == 0;   // RRSetConfigSuccess
                g_lastXError = 0;
                if (ok) { picked = pick; rect = DisplayRect{c->x, c->y, pick.width, pick.height}; }
                else if (sw.screenGrew && !haveOld) { RestoreScreenSize(sw); sw.screenGrew = false; }
            }
            m_rrFreeCrtc(c);
        }
        m_rrFreeRes(res);
        return ok;
    }
    /// The screen's size now (the root window's; Xlib's DisplayWidth keeps the size from when the connection opened).
    void RootSize(int& w, int& h) {
        XWindowAttributes a;
        if (m_x.XGetWindowAttributes(m_dpy, m_root, &a)) { w = a.width; h = a.height; }
        else { w = DisplayWidth(m_dpy, m_screen); h = DisplayHeight(m_dpy, m_screen); }
    }
    void RestoreScreenSize(const ModeSwitch& sw) {
        if (!m_rrSetScreenSize || sw.oldScreenW <= 0 || sw.oldScreenH <= 0) return;
        m_rrSetScreenSize(m_dpy, m_root, sw.oldScreenW, sw.oldScreenH, sw.oldScreenMmW, sw.oldScreenMmH);
        m_x.XSync(m_dpy, False);
    }
    /// Put back what SwitchMode changed.
    void RestoreMode(const ModeSwitch& sw) {
        if (!LoadModeFunctions() || !m_rrSetCrtc || !sw.crtc) return;
        RrScreenResources* res = m_rrGetRes(m_dpy, m_root);
        if (!res) return;
        std::vector<unsigned long> outs = sw.outputs;
        g_lastXError = 0;
        m_rrSetCrtc(m_dpy, res, sw.crtc, CurrentTime, sw.x, sw.y, sw.oldMode, sw.rotation, outs.empty() ? nullptr : outs.data(), (int)outs.size());
        m_x.XSync(m_dpy, False);
        g_lastXError = 0;
        m_rrFreeRes(res);
        if (sw.screenGrew) RestoreScreenSize(sw);
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
    uint32_t KeyboardFocusWindowId() override { return (uint32_t)m_focus; }
    uint32_t MouseFocusWindowId() override { return m_wins.count(m_mouseWin) ? (uint32_t)m_mouseWin : 0; }   // 0 once that window is gone
    bool IsRelativeMouse() override { return m_relative; }
    uint32_t GlobalMouseState(int* x, int* y) override {
        ::Window root, child; int rx = 0, ry = 0, wx = 0, wy = 0; unsigned mask = 0;
        m_x.XQueryPointer(m_dpy, m_root, &root, &child, &rx, &ry, &wx, &wy, &mask);   // the root's coordinates are the desktop's
        if (x) *x = rx;
        if (y) *y = ry;
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
    bool SetSystemCursor(SystemCursor c) override {
        // Shapes of the X core cursor font (the numbers of X11/cursorfont.h, which is part of the X protocol).
        static const unsigned kGlyph[] = {68 /*left_ptr*/, 152 /*xterm*/, 60 /*hand2*/, 52 /*fleur*/, 116 /*sb_v_double_arrow*/,
            108 /*sb_h_double_arrow*/, 12 /*bottom_left_corner*/, 14 /*bottom_right_corner*/, 0 /*X_cursor*/, 150 /*watch*/};
        const int i = (int)c;
        if (!m_dpy || i < 0 || i >= (int)(sizeof(kGlyph) / sizeof(kGlyph[0]))) return false;
        if (!m_fontCursors[i]) m_fontCursors[i] = m_x.XCreateFontCursor(m_dpy, kGlyph[i]);
        if (!m_fontCursors[i]) return false;
        m_shapeCursor = c == SystemCursor::Arrow ? 0 : m_fontCursors[i];   // the arrow is the window's default: undefine
        for (auto& kv : m_wins) HideCursorOn(kv.first, m_cursorHidden || m_relative);
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
            if (m_shapeCursor) m_x.XDefineCursor(m_dpy, w, m_shapeCursor);
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

    // ---- Displays ------------------------------------------------------------------------
    // RandR 1.5's monitor list (XRRGetMonitors) is what the X server itself calls a monitor: one per active output, or
    // what a user defined with `xrandr --setmonitor`. libXrandr is loaded at run time; its XRRMonitorInfo is laid out
    // here from the library's public interface (field order and types), since the header is not needed otherwise.
    struct RrMonitor {
        Atom name; Bool primary; Bool automatic; int noutput;
        int x, y, width, height, mwidth, mheight;
        unsigned long* outputs;
    };
    struct XineramaScreen { int screen_number; short x_org, y_org, width, height; };   // XineramaScreenInfo
    bool LoadDisplayLibs() {
        if (m_dispLibsTried) return true;
        m_dispLibsTried = true;
        if (void* rr = dlopen("libXrandr.so.2", RTLD_NOW | RTLD_GLOBAL)) {
            m_rrQueryExt = reinterpret_cast<Bool (*)(Display*, int*, int*)>(dlsym(rr, "XRRQueryExtension"));
            m_rrQueryVer = reinterpret_cast<Status (*)(Display*, int*, int*)>(dlsym(rr, "XRRQueryVersion"));
            m_rrGetMonitors = reinterpret_cast<RrMonitor* (*)(Display*, ::Window, Bool, int*)>(dlsym(rr, "XRRGetMonitors"));
            m_rrFreeMonitors = reinterpret_cast<void (*)(RrMonitor*)>(dlsym(rr, "XRRFreeMonitors"));
            m_rrGetRes = reinterpret_cast<RrScreenResources* (*)(Display*, ::Window)>(dlsym(rr, "XRRGetScreenResourcesCurrent"));
            m_rrFreeRes = reinterpret_cast<void (*)(RrScreenResources*)>(dlsym(rr, "XRRFreeScreenResources"));
            m_rrGetCrtc = reinterpret_cast<RrCrtcInfo* (*)(Display*, RrScreenResources*, unsigned long)>(dlsym(rr, "XRRGetCrtcInfo"));
            m_rrFreeCrtc = reinterpret_cast<void (*)(RrCrtcInfo*)>(dlsym(rr, "XRRFreeCrtcInfo"));
        }
        if (void* xi = dlopen("libXinerama.so.1", RTLD_NOW | RTLD_GLOBAL)) {
            m_xiIsActive = reinterpret_cast<Bool (*)(Display*)>(dlsym(xi, "XineramaIsActive"));
            m_xiQuery = reinterpret_cast<XineramaScreen* (*)(Display*, int*)>(dlsym(xi, "XineramaQueryScreens"));
        }
        m_getAtomName = reinterpret_cast<char* (*)(Display*, Atom)>(dlsym(m_x.lib, "XGetAtomName"));
        return true;
    }
    std::vector<DisplayInfo> MonitorsFromRandr() {
        std::vector<DisplayInfo> out;
        if (!m_rrQueryExt || !m_rrQueryVer || !m_rrGetMonitors || !m_rrFreeMonitors) return out;
        int ev = 0, er = 0, maj = 0, min = 0;
        if (!m_rrQueryExt(m_dpy, &ev, &er) || !m_rrQueryVer(m_dpy, &maj, &min)) return out;
        if (maj < 1 || (maj == 1 && min < 5)) return out;   // XRRGetMonitors is RandR 1.5
        int n = 0;
        RrMonitor* m = m_rrGetMonitors(m_dpy, m_root, True, &n);
        if (!m) return out;
        for (int i = 0; i < n; ++i) {
            if (m[i].width <= 0 || m[i].height <= 0) continue;
            DisplayInfo d;
            d.bounds = DisplayRect{m[i].x, m[i].y, m[i].width, m[i].height};
            d.primary = m[i].primary != 0;
            if (m_getAtomName && m[i].name) {
                if (char* nm = m_getAtomName(m_dpy, m[i].name)) { d.name = nm; m_x.XFree(nm); }
            }
            out.push_back(d);
        }
        m_rrFreeMonitors(m);
        return out;
    }
    std::vector<DisplayInfo> MonitorsFromXinerama() {
        std::vector<DisplayInfo> out;
        if (!m_xiIsActive || !m_xiQuery || !m_xiIsActive(m_dpy)) return out;
        int n = 0;
        XineramaScreen* s = m_xiQuery(m_dpy, &n);
        if (!s) return out;
        for (int i = 0; i < n; ++i) {
            if (s[i].width <= 0 || s[i].height <= 0) continue;
            DisplayInfo d;
            d.bounds = DisplayRect{s[i].x_org, s[i].y_org, s[i].width, s[i].height};
            d.primary = out.empty();   // Xinerama has no primary; its first screen is the usual stand-in
            d.name = "Xinerama-" + std::to_string(s[i].screen_number);
            out.push_back(d);
        }
        m_x.XFree(s);
        return out;
    }
    // A property of the root window as 32-bit items (format 32 comes back as longs) or as bytes.
    bool RootLongs(const char* atomName, std::vector<long>& out) {
        out.clear();
        const Atom a = m_x.XInternAtom(m_dpy, atomName, True);
        if (!a) return false;
        Atom type = 0; int fmt = 0; unsigned long n = 0, after = 0; unsigned char* data = nullptr;
        if (m_x.XGetWindowProperty(m_dpy, m_root, a, 0, 1024, False, AnyPropertyType, &type, &fmt, &n, &after, &data) != Success) return false;
        if (data && fmt == 32) out.assign((long*)data, (long*)data + n);
        if (data) m_x.XFree(data);
        return !out.empty();
    }
    std::string RootString(const char* atomName) {
        const Atom a = m_x.XInternAtom(m_dpy, atomName, True);
        if (!a) return std::string();
        Atom type = 0; int fmt = 0; unsigned long n = 0, after = 0; unsigned char* data = nullptr;
        if (m_x.XGetWindowProperty(m_dpy, m_root, a, 0, 1 << 20, False, AnyPropertyType, &type, &fmt, &n, &after, &data) != Success) return std::string();
        std::string r = data && fmt == 8 ? std::string((const char*)data, n) : std::string();
        if (data) m_x.XFree(data);
        return r;
    }
    std::vector<DisplayInfo> Displays() override {
        LoadDisplayLibs();
        std::vector<DisplayInfo> out = MonitorsFromRandr();
        // Real Xinerama (several X screens joined) can leave RandR describing only the first screen: take whichever
        // list has more monitors, RandR on a tie (it has names and a primary).
        std::vector<DisplayInfo> xi = MonitorsFromXinerama();
        if (xi.size() > out.size()) out = xi;
        if (out.empty()) {
            DisplayInfo d;
            d.name = "X11 screen " + std::to_string(m_screen);
            d.bounds = DisplayRect{0, 0, DisplayWidth(m_dpy, m_screen), DisplayHeight(m_dpy, m_screen)};
            d.primary = true;
            out.push_back(d);
        }
        // EWMH work area: four numbers per desktop; the current desktop's (else the first).
        DisplayRect work;
        std::vector<long> wa, cur;
        if (RootLongs("_NET_WORKAREA", wa) && wa.size() >= 4) {
            size_t k = 0;
            if (RootLongs("_NET_CURRENT_DESKTOP", cur) && cur[0] >= 0 && (size_t)cur[0] * 4 + 3 < wa.size()) k = (size_t)cur[0] * 4;
            work = DisplayRect{(int)wa[k], (int)wa[k + 1], (int)wa[k + 2], (int)wa[k + 3]};
        }
        const float scale = ScaleFromXResources(RootString("RESOURCE_MANAGER"));
        bool anyPrimary = false;
        for (DisplayInfo& d : out) { d.usable = ClipToWorkArea(d.bounds, work); d.scale = scale; anyPrimary |= d.primary; }
        if (!anyPrimary) out[0].primary = true;   // RandR without a primary output set: the first one
        std::stable_sort(out.begin(), out.end(), [](const DisplayInfo& a, const DisplayInfo& b) { return a.primary && !b.primary; });
        return out;
    }

    XTable m_x;
    GlxTable m_glx;
    bool m_hasGlx = false;
    Display* m_dpy = nullptr;
    int m_screen = 0;
    ::Window m_root = 0;
    XIM m_im = nullptr;
    bool m_imSpot = false;   // the input method offers over-the-spot input (XIMPreeditPosition | XIMStatusNothing)
    Atom m_aMaxH = 0, m_aMaxV = 0, m_aNetIcon = 0;
    Atom m_aDelete = 0, m_aProtocols = 0, m_aNetState = 0, m_aNetFull = 0, m_aNetName = 0, m_aUtf8 = 0,
         m_aClipboard = 0, m_aTargets = 0, m_aProp = 0, m_aTextPlain = 0;
    std::unordered_map<::Window, X11Win*> m_wins;
    std::deque<Event> m_pending;
    ::Window m_focus = 0;
    ::Window m_mouseWin = 0;      // the window the pointer is over (EnterNotify), 0 when none of ours
    Atom m_aWmState = 0, m_aNetHidden = 0;   // interned on first use by X11Win::IsMinimized
    bool m_textInput = false, m_relative = false, m_warped = false, m_cursorHidden = false;
    Cursor m_blankCursor = 0;
    Cursor m_fontCursors[10] = {};   // SetSystemCursor shapes, made on first use
    Cursor m_shapeCursor = 0;        // the shape shown while the cursor is visible (0 = the default arrow)
    int m_lastX = 0, m_lastY = 0;
    int m_relAccX = 0, m_relAccY = 0;   // motion since RelativeMouseState was last read
    int m_lastBtn = 0, m_clicks = 0; unsigned long m_lastBtnTime = 0;
    std::string m_clip;
    bool m_ownsClip = false, m_selNotify = false;
    Atom m_selProp = 0;
    bool m_dispLibsTried = false;
    Bool (*m_rrQueryExt)(Display*, int*, int*) = nullptr;
    Status (*m_rrQueryVer)(Display*, int*, int*) = nullptr;
    RrMonitor* (*m_rrGetMonitors)(Display*, ::Window, Bool, int*) = nullptr;
    void (*m_rrFreeMonitors)(RrMonitor*) = nullptr;
    RrScreenResources* (*m_rrGetRes)(Display*, ::Window) = nullptr;
    void (*m_rrFreeRes)(RrScreenResources*) = nullptr;
    RrCrtcInfo* (*m_rrGetCrtc)(Display*, RrScreenResources*, unsigned long) = nullptr;
    void (*m_rrFreeCrtc)(RrCrtcInfo*) = nullptr;
    bool m_rrModeTried = false;
    RrOutputInfo* (*m_rrGetOutput)(Display*, RrScreenResources*, unsigned long) = nullptr;
    void (*m_rrFreeOutput)(RrOutputInfo*) = nullptr;
    Status (*m_rrSetCrtc)(Display*, RrScreenResources*, unsigned long, Time, int, int, unsigned long, unsigned short, unsigned long*, int) = nullptr;
    void (*m_rrSetScreenSize)(Display*, ::Window, int, int, int, int) = nullptr;
    std::map<::Window, ModeSwitch> m_modeSwitches;   // SetFullscreenMode: one per window that switched a display's mode
    // XDND (file drops)
    Atom m_aXdndAware = 0, m_aXdndEnter = 0, m_aXdndPosition = 0, m_aXdndStatus = 0, m_aXdndLeave = 0, m_aXdndDrop = 0,
         m_aXdndFinished = 0, m_aXdndSelection = 0, m_aXdndActionCopy = 0, m_aXdndTypeList = 0, m_aUriList = 0, m_aDropProp = 0;
    struct DndState { ::Window source = 0, target = 0; int version = 0; bool uriOffered = false, dropped = false; };
    DndState m_dnd;
    Bool (*m_xiIsActive)(Display*) = nullptr;
    XineramaScreen* (*m_xiQuery)(Display*, int*) = nullptr;
    char* (*m_getAtomName)(Display*, Atom) = nullptr;
#if OKAY_X11_HAVE_XI2
    // XInput 2.2 touch
    bool m_xiTouch = false;   // touch events are selected on our windows
    int m_xiOpcode = -1;
    Bool (*m_queryExtension)(Display*, const char*, int*, int*, int*) = nullptr;
    Bool (*m_getEventData)(Display*, XGenericEventCookie*) = nullptr;
    void (*m_freeEventData)(Display*, XGenericEventCookie*) = nullptr;
    Status (*m_xiQueryVersion)(Display*, int*, int*) = nullptr;
    int (*m_xiSelectEvents)(Display*, ::Window, XIEventMask*, int) = nullptr;
    XIDeviceInfo* (*m_xiQueryDevice)(Display*, int, int*) = nullptr;
    void (*m_xiFreeDeviceInfo)(XIDeviceInfo*) = nullptr;
    Atom m_aMtPressure = 0, m_aAbsPressure = 0;
    std::map<int, XIPressureAxis> m_pressureAxes;   // by source device id
    TouchTracker m_touch{true};                     // the first finger drives the mouse (the server stops doing it)
    int m_touchCount = 0; uint64_t m_touchCountAt = 0; bool m_touchCountRead = false;
#endif
};

// ---- X11Win methods ------------------------------------------------------------------
#define XD m_sys->m_dpy
#define XF m_sys->m_x

X11Win::~X11Win() {
    LeaveFullscreenMode();
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
void X11Win::Show() { m_hidden = false; XF.XMapWindow(XD, m_win); XF.XFlush(XD); }
void X11Win::Hide() { m_hidden = true; XF.XUnmapWindow(XD, m_win); XF.XFlush(XD); }
void X11Win::Raise() {
    XF.XRaiseWindow(XD, m_win);
    XF.XSync(XD, False);
    g_lastXError = 0;
    XF.XSetInputFocus(XD, m_win, RevertToParent, CurrentTime);   // no window manager to do it for us
    XF.XSync(XD, False);
}
void X11Win::Minimize() { XF.XIconifyWindow(XD, m_win, m_sys->m_screen); XF.XFlush(XD); }
int X11Win::RefreshRateHz() const {
    int x = 0, y = 0, w = 0, h = 0;
    GetPosition(x, y);
    GetSize(w, h);
    return m_sys->RefreshRateAt(x + w / 2, y + h / 2);
}
int X11Win::DisplayIndex() const {
    int x = 0, y = 0, w = 0, h = 0;
    GetPosition(x, y);
    GetSize(w, h);
    return DisplayIndexForRect(m_sys->Displays(), x, y, w, h);
}
bool X11Win::SetFullscreenMode(const VideoMode& want) {
    const int display = DisplayIndex() >= 0 ? DisplayIndex() : 0;
    const bool haveOld = m_mode.width > 0;
    X11Impl::ModeSwitch& sw = m_sys->m_modeSwitches[m_win];
    if (!haveOld) { GetPosition(m_savedX, m_savedY); GetSize(m_savedW, m_savedH); }
    VideoMode picked; DisplayRect r;
    if (!m_sys->SwitchMode(display, want, sw, haveOld, picked, r)) {
        if (!haveOld) m_sys->m_modeSwitches.erase(m_win);
        return false;
    }
    m_mode = picked;
    // Cover the CRTC's picture: the window manager does it for _NET_WM_STATE_FULLSCREEN; with none (Xvfb) move and size it.
    m_fullscreen = true;
    XEvent e; std::memset(&e, 0, sizeof(e));
    e.xclient.type = ClientMessage; e.xclient.window = m_win; e.xclient.message_type = m_sys->m_aNetState; e.xclient.format = 32;
    e.xclient.data.l[0] = 1; e.xclient.data.l[1] = (long)m_sys->m_aNetFull; e.xclient.data.l[3] = 1;
    XF.XSendEvent(XD, m_sys->m_root, False, SubstructureNotifyMask | SubstructureRedirectMask, &e);
    XF.XMoveResizeWindow(XD, m_win, r.x, r.y, (unsigned)r.w, (unsigned)r.h);
    XF.XRaiseWindow(XD, m_win);
    XF.XSync(XD, False);
    return true;
}
void X11Win::LeaveFullscreenMode() {
    if (m_mode.width <= 0) return;
    auto it = m_sys->m_modeSwitches.find(m_win);
    if (it != m_sys->m_modeSwitches.end()) { m_sys->RestoreMode(it->second); m_sys->m_modeSwitches.erase(it); }
    m_mode = VideoMode();
    if (m_savedW > 0 && m_savedH > 0) XF.XMoveResizeWindow(XD, m_win, m_savedX, m_savedY, (unsigned)m_savedW, (unsigned)m_savedH);
    XF.XSync(XD, False);
}
void X11Win::Restore() { m_hidden = false; XF.XMapWindow(XD, m_win); XF.XRaiseWindow(XD, m_win); XF.XFlush(XD); }
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
// Flash: the ICCCM urgency hint (WM_HINTS flags bit XUrgencyHint, section 4.1.2.4): the window manager or task bar marks
// the window until the hint is cleared. Kept in the window's existing WM_HINTS so the input and icon hints stay as they are.
void X11Win::SetUrgent(bool on) {
    XWMHints* h = XF.XGetWMHints(XD, m_win);
    if (!h) h = XF.XAllocWMHints();
    if (!h) return;
    if (on) h->flags |= XUrgencyHint; else h->flags &= ~XUrgencyHint;
    XF.XSetWMHints(XD, m_win, h);
    XF.XFree(h);
    XF.XFlush(XD);
    m_urgent = on;
    if (!on) m_urgentUntil = 0;
}
bool X11Win::Flash(FlashMode m) {
    if (m == FlashMode::Cancel) { SetUrgent(false); return true; }
    if (m_focused) return true;   // it has the focus: nothing to ask for (what Windows does too)
    SetUrgent(true);
    m_urgentUntil = m == FlashMode::Briefly ? NowMs() + 1000 : 0;
    return true;
}
bool X11Win::SetTextInputArea(int x, int y, int w, int h) {
    (void)w;
    if (!m_ic || !m_icSpot) return false;   // root-window style: the input method places its own window
    // XNSpotLocation is the baseline start of the text being composed: the bottom of the caret's line.
    XPoint spot; spot.x = (short)x; spot.y = (short)(y + (h > 0 ? h : 0));
    XVaNestedList pre = XF.XVaCreateNestedList_(0, XNSpotLocation, &spot, (void*)nullptr);
    if (!pre) return false;
    const char* bad = XF.XSetICValues_(m_ic, XNPreeditAttributes, pre, (void*)nullptr);
    XF.XFree(pre);
    return bad == nullptr;
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
    LeaveFullscreenMode();   // from an exclusive mode to desktop fullscreen or a window: the display's own mode comes back
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
bool X11Win::SetInputFocus() {
    g_lastXError = 0;
    XF.XSetInputFocus(XD, m_win, RevertToParent, CurrentTime);   // BadMatch when the window is not viewable
    XF.XSync(XD, False);
    const bool ok = g_lastXError == 0;
    g_lastXError = 0;
    return ok;
}
bool X11Win::IsMinimized() const {
    // A window manager that iconifies a window says so in two ways: ICCCM sets WM_STATE = IconicState (3), and EWMH adds
    // _NET_WM_STATE_HIDDEN to _NET_WM_STATE. Either one counts. With no window manager (Xvfb) nothing minimises a window.
    X11Impl* sys = m_sys;
    if (!sys->m_aWmState) {
        sys->m_aWmState = XF.XInternAtom(XD, "WM_STATE", False);
        sys->m_aNetHidden = XF.XInternAtom(XD, "_NET_WM_STATE_HIDDEN", False);
    }
    Atom type = None; int fmt = 0; unsigned long n = 0, rest = 0; unsigned char* data = nullptr;
    bool minimized = false;
    if (XF.XGetWindowProperty(XD, m_win, sys->m_aWmState, 0, 2, False, AnyPropertyType, &type, &fmt, &n, &rest, &data) == Success && data) {
        if (type == sys->m_aWmState && fmt == 32 && n >= 1 && ((const long*)data)[0] == 3) minimized = true;   // IconicState
        XF.XFree(data);
    }
    data = nullptr;
    if (!minimized && XF.XGetWindowProperty(XD, m_win, sys->m_aNetState, 0, 32, False, XA_ATOM, &type, &fmt, &n, &rest, &data) == Success && data) {
        if (type == XA_ATOM && fmt == 32)
            for (unsigned long i = 0; i < n; ++i)
                if ((Atom)((const long*)data)[i] == sys->m_aNetHidden) { minimized = true; break; }
        XF.XFree(data);
    }
    return minimized;
}

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

#if OKAY_X11_HAVE_XI2
// ---- XInput 2.2 touch translation (OwnLinuxXI2.hpp) ----------------------------------------------------------------------
XIPressureAxis PressureAxisOf(const XIDeviceInfo& dev, Atom mtPressure, Atom absPressure) {
    XIPressureAxis mt, abs;
    for (int i = 0; i < dev.num_classes; ++i) {
        const XIAnyClassInfo* c = dev.classes ? dev.classes[i] : nullptr;
        if (!c || c->type != XIValuatorClass) continue;
        const XIValuatorClassInfo* v = reinterpret_cast<const XIValuatorClassInfo*>(c);
        if (v->label == None) continue;
        XIPressureAxis a; a.number = v->number; a.min = v->min; a.max = v->max;
        if (mtPressure && v->label == mtPressure && mt.number < 0) mt = a;
        else if (absPressure && v->label == absPressure && abs.number < 0) abs = a;
    }
    return mt.number >= 0 ? mt : abs;
}

bool XIValuatorValue(const XIValuatorState& v, int number, double& out) {
    if (number < 0 || !v.mask || number >= v.mask_len * 8 || !XIMaskIsSet(v.mask, number)) return false;
    int index = 0;   // the values hold one entry per set bit, lowest bit first
    for (int b = 0; b < number; ++b) if (XIMaskIsSet(v.mask, b)) ++index;
    if (!v.values) return false;
    out = v.values[index];
    return true;
}

bool TouchSampleFromXI(const XIDeviceEvent& ev, const XIPressureAxis& axis, int winW, int winH, uint32_t windowId,
                       uint32_t nowMs, TouchSample& out) {
    TouchPhase phase;
    if (ev.evtype == XI_TouchBegin) phase = TouchPhase::Begin;
    else if (ev.evtype == XI_TouchUpdate) phase = TouchPhase::Move;
    else if (ev.evtype == XI_TouchEnd) phase = TouchPhase::End;
    else return false;
    out = TouchSample{};
    out.id = (long long)(unsigned)ev.detail;
    out.phase = phase;
    out.x = ev.event_x; out.y = ev.event_y;
    out.winW = winW; out.winH = winH;
    out.windowId = windowId;
    out.timestampMs = nowMs;
    out.pressure = 1.0f;
    double raw = 0.0;
    if (axis.number >= 0 && axis.max > axis.min && XIValuatorValue(ev.valuators, axis.number, raw)) {
        const double f = (raw - axis.min) / (axis.max - axis.min);
        out.pressure = (float)(f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f);
    }
    return true;
}

int CountTouchDevices(const XIDeviceInfo* devs, int n) {
    int count = 0;
    for (int i = 0; devs && i < n; ++i) {
        const XIDeviceInfo& d = devs[i];
        if (d.use != XISlavePointer && d.use != XIFloatingSlave && d.use != XISlaveKeyboard) continue;
        for (int c = 0; c < d.num_classes; ++c)
            if (d.classes && d.classes[c] && d.classes[c]->type == XITouchClass) { ++count; break; }
    }
    return count;
}
#endif

std::unique_ptr<X11System> X11System::Open(std::string& err) {
    std::unique_ptr<X11Impl> s(new X11Impl());
    if (!s->Connect(err)) return nullptr;
    return s;
}

// A message box drawn with core X requests on its own connection (so it works before, after or without the backend's own):
// the text in the server's "fixed" font, one line per '\n', and an OK button. Written from the Xlib manual (windows, graphics
// contexts, XDrawString, events); the look is ours. The window is titled `title`, asks the window manager for a dialog
// (_NET_WM_WINDOW_TYPE_DIALOG) and is centred on the screen.
bool X11MessageBox(const std::string& title, const std::string& text, std::string& err) {
    XTable x;
    if (!LoadX(x, err)) return false;
    XErrorHandler prevHandler = x.XSetErrorHandler(OkayXErrorHandler);
    Display* dpy = x.XOpenDisplay(nullptr);
    if (!dpy) { err = "cannot open the X display"; x.XSetErrorHandler(prevHandler); return false; }
    const int screen = DefaultScreen(dpy);
    const ::Window root = RootWindow(dpy, screen);
    XFontStruct* font = x.XLoadQueryFont(dpy, "fixed");
    const int lineH = font ? font->ascent + font->descent + 4 : 17;
    const int ascent = font ? font->ascent : 11;
    auto textW = [&](const std::string& s) { return font ? x.XTextWidth(font, s.data(), (int)s.size()) : (int)s.size() * 7; };
    std::vector<std::string> lines;
    for (size_t pos = 0; pos <= text.size();) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        lines.push_back(text.substr(pos, nl - pos));
        pos = nl + 1;
    }
    const int pad = 20, btnW = 90, btnH = 28;
    int w = 320;
    for (const std::string& l : lines) w = std::max(w, textW(l) + 2 * pad);
    w = std::min(w, DisplayWidth(dpy, screen) - 40);
    const int h = pad + (int)lines.size() * lineH + pad + btnH + pad;
    XSetWindowAttributes swa; std::memset(&swa, 0, sizeof(swa));
    swa.background_pixel = WhitePixel(dpy, screen);
    swa.border_pixel = BlackPixel(dpy, screen);
    swa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | StructureNotifyMask;
    const int wx = (DisplayWidth(dpy, screen) - w) / 2, wy = (DisplayHeight(dpy, screen) - h) / 2;
    ::Window win = x.XCreateWindow(dpy, root, wx, wy, (unsigned)w, (unsigned)h, 1, CopyFromParent, InputOutput,
                                   CopyFromParent, CWBackPixel | CWBorderPixel | CWEventMask, &swa);
    if (!win) { err = "XCreateWindow failed"; if (font) x.XFreeFont(dpy, font); x.XCloseDisplay(dpy); x.XSetErrorHandler(prevHandler); return false; }
    x.XStoreName(dpy, win, title.c_str());
    const Atom utf8 = x.XInternAtom(dpy, "UTF8_STRING", False);
    x.XChangeProperty(dpy, win, x.XInternAtom(dpy, "_NET_WM_NAME", False), utf8, 8, PropModeReplace,
                      (const unsigned char*)title.data(), (int)title.size());
    const Atom dialog = x.XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    x.XChangeProperty(dpy, win, x.XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False), XA_ATOM, 32, PropModeReplace,
                      (const unsigned char*)&dialog, 1);
    Atom del = x.XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    const Atom protocols = x.XInternAtom(dpy, "WM_PROTOCOLS", False);
    x.XSetWMProtocols(dpy, win, &del, 1);
    if (XSizeHints* sh = x.XAllocSizeHints()) {   // a fixed size, where we put it
        sh->flags = PPosition | PMinSize | PMaxSize;
        sh->x = wx; sh->y = wy; sh->min_width = sh->max_width = w; sh->min_height = sh->max_height = h;
        x.XSetWMNormalHints(dpy, win, sh);
        x.XFree(sh);
    }
    GC gc = x.XCreateGC(dpy, win, 0, nullptr);
    if (font) x.XSetFont(dpy, gc, font->fid);
    x.XMapWindow(dpy, win);
    x.XRaiseWindow(dpy, win);
    x.XFlush(dpy);
    const int bx = w - pad - btnW, by = h - pad - btnH;
    auto draw = [&](bool pressed) {
        x.XSetForeground(dpy, gc, WhitePixel(dpy, screen));
        x.XFillRectangle(dpy, win, gc, 0, 0, (unsigned)w, (unsigned)h);
        x.XSetForeground(dpy, gc, BlackPixel(dpy, screen));
        for (size_t i = 0; i < lines.size(); ++i)
            x.XDrawString(dpy, win, gc, pad, pad + ascent + (int)i * lineH, lines[i].data(), (int)lines[i].size());
        if (pressed) x.XFillRectangle(dpy, win, gc, bx, by, (unsigned)btnW, (unsigned)btnH);
        else x.XDrawRectangle(dpy, win, gc, bx, by, (unsigned)btnW, (unsigned)btnH);
        x.XSetForeground(dpy, gc, pressed ? WhitePixel(dpy, screen) : BlackPixel(dpy, screen));
        x.XDrawString(dpy, win, gc, bx + (btnW - textW("OK")) / 2, by + (btnH + ascent) / 2 - 1, "OK", 2);
        x.XFlush(dpy);
    };
    auto onButton = [&](int px, int py) { return px >= bx && py >= by && px < bx + btnW && py < by + btnH; };
    bool done = false, pressed = false;
    while (!done) {
        XEvent ev;
        x.XNextEvent(dpy, &ev);
        if (ev.xany.window != win) continue;
        switch (ev.type) {
        case Expose: if (ev.xexpose.count == 0) draw(pressed); break;
        case ButtonPress: if (ev.xbutton.button == Button1 && onButton(ev.xbutton.x, ev.xbutton.y)) { pressed = true; draw(true); } break;
        case ButtonRelease:
            if (ev.xbutton.button == Button1) {
                if (pressed && onButton(ev.xbutton.x, ev.xbutton.y)) done = true;
                pressed = false;
                if (!done) draw(false);
            }
            break;
        case KeyPress: {
            const KeySym ks = x.XLookupKeysym(&ev.xkey, 0);
            if (ks == XK_Return || ks == XK_KP_Enter || ks == XK_Escape || ks == XK_space) done = true;
            break;
        }
        case ClientMessage:
            if (ev.xclient.message_type == protocols && (Atom)ev.xclient.data.l[0] == del) done = true;
            break;
        case DestroyNotify: done = true; win = 0; break;
        default: break;
        }
    }
    x.XFreeGC(dpy, gc);
    if (win) x.XDestroyWindow(dpy, win);
    if (font) x.XFreeFont(dpy, font);
    x.XCloseDisplay(dpy);
    x.XSetErrorHandler(prevHandler);
    return true;
}

} // namespace okay::platform::own
#endif // OKAY_PLATFORM_X11_HEADERS
