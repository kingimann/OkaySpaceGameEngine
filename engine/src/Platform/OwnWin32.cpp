// okay::platform backend "own" for Windows: Win32 windows and input, WGL for OpenGL,
// the Win32 clipboard, QueryPerformanceCounter timers and XInput controllers.
//
// STATUS: COMPILES, UNTESTED. This file is only syntax-checked with MinGW on a Linux
// machine; it has never been run on Windows. Audio devices are only syntax-checked too
// (audio: waveOut / waveIn in OwnWin32Audio.cpp, also never run). Treat every behaviour here as a first draft.
#ifdef _WIN32
#include "Internal.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <xinput.h>
#include <imm.h>      // COMPOSITIONFORM, CANDIDATEFORM (imm32.dll is loaded at run time, not linked)
#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <vector>

namespace okay::platform {
namespace {

// ---- GL pieces we need without GL headers' extension loader ----
constexpr int kWglContextMajor = 0x2091, kWglContextMinor = 0x2092, kWglProfileMask = 0x9126;
constexpr int kWglProfileCore = 0x1, kWglProfileCompat = 0x2;

Key KeyFromScan(int scan, bool ext) {
    static const uint16_t kTab[89] = {
        0, 41, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 45, 46, 42, 43,
        20, 26, 8, 21, 23, 28, 24, 12, 18, 19, 47, 48, 40, 224, 4, 22,
        7, 9, 10, 11, 13, 14, 15, 51, 52, 53, 225, 49, 29, 27, 6, 25,
        5, 17, 16, 54, 55, 56, 229, 85, 226, 44, 57, 58, 59, 60, 61, 62,
        63, 64, 65, 66, 67, 83, 71, 95, 96, 97, 86, 92, 93, 94, 87, 89,
        90, 91, 98, 99, 0, 0, 0, 68, 69};
    if (ext) {
        switch (scan) {
        case 0x1C: return Key::KpEnter;   case 0x1D: return Key::RCtrl;   case 0x35: return Key::KpDivide;
        case 0x37: return Key::PrintScreen; case 0x38: return Key::RAlt;  case 0x47: return Key::Home;
        case 0x48: return Key::Up;        case 0x49: return Key::PageUp;  case 0x4B: return Key::Left;
        case 0x4D: return Key::Right;     case 0x4F: return Key::End;     case 0x50: return Key::Down;
        case 0x51: return Key::PageDown;  case 0x52: return Key::Insert;  case 0x53: return Key::Delete;
        case 0x5B: return Key::LGui;      case 0x5C: return Key::RGui;    case 0x45: return Key::NumLock;
        default: return Key::Unknown;
        }
    }
    if (scan == 0x45) return Key::Pause;   // Pause sends a non-extended 0x45 with a different prefix
    return scan >= 0 && scan < 89 ? (Key)kTab[scan] : Key::Unknown;
}

std::string Utf8(const wchar_t* w, int n) {
    if (n <= 0) return std::string();
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, n, &s[0], len, nullptr, nullptr);
    return s;
}
std::wstring Wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], len);
    return w;
}

uint32_t CurrentMods() {
    uint32_t m = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) m |= ModShift;
    if (GetKeyState(VK_CONTROL) & 0x8000) m |= ModCtrl;
    if (GetKeyState(VK_MENU) & 0x8000) m |= ModAlt;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) m |= ModGui;
    if (GetKeyState(VK_CAPITAL) & 1) m |= ModCaps;
    if (GetKeyState(VK_NUMLOCK) & 1) m |= ModNum;
    return m;
}

// ---- touch: WM_POINTER (Windows 8+) with WM_TOUCH (Windows 7) as the fallback ---------------------------------------------
// Written from Microsoft's documentation ("Pointer Input Messages and Notifications": WM_POINTERDOWN / UPDATE / UP,
// GetPointerType, GetPointerTouchInfo, POINTER_INFO, POINTER_TOUCH_INFO, GetPointerDevices, POINTER_DEVICE_INFO; "WM_TOUCH",
// RegisterTouchWindow, GetTouchInputInfo, CloseTouchInputHandle, TOUCHINPUT; GetMessageExtraInfo's pen / touch signature;
// GetSystemMetrics SM_DIGITIZER). The structures are declared here from those pages (field order and types) under our own
// names, so the file does not depend on how new the compiler's headers are; the functions come from user32.dll at run time.
//
// Touch contacts become FingerDown / FingerMotion / FingerUp (TouchTracker, without its mouse: Windows itself still makes the
// mouse messages of the primary contact when the pointer message goes on to DefWindowProc, and those are marked fromTouch).
// Pen and mouse pointers are left alone: they arrive as the mouse, as with SDL2. EnableMouseInPointer is NOT called: it would
// move every mouse message onto WM_POINTER for the whole process. STATUS: MinGW syntax check only, never run on Windows.
constexpr UINT kWmTouch = 0x0240, kWmPointerUpdate = 0x0245, kWmPointerDown = 0x0246, kWmPointerUp = 0x0247;
constexpr DWORD kPtTouch = 2;                                      // POINTER_INPUT_TYPE PT_TOUCH
constexpr UINT32 kPointerFlagCanceled = 0x00008000;                // POINTER_FLAG_CANCELED
constexpr UINT32 kTouchMaskPressure = 0x00000004;                  // TOUCH_MASK_PRESSURE
constexpr DWORD kTouchEventDown = 0x2, kTouchEventUp = 0x4;   // TOUCHEVENTF_DOWN / _UP (anything else: a move)
constexpr int kSmDigitizer = 94;                                   // SM_DIGITIZER
constexpr int kNidIntegratedTouch = 0x01, kNidExternalTouch = 0x02, kNidReady = 0x80;
constexpr DWORD kPointerDeviceTypeTouch = 3;                       // POINTER_DEVICE_TYPE_TOUCH
constexpr uint32_t kMouseSignatureMask = 0xFFFFFF00u, kMouseSignature = 0xFF515700u, kMouseFromTouchBit = 0x80u;   // MI_WP_SIGNATURE

struct OkPointerInfo {            // POINTER_INFO
    DWORD pointerType;
    UINT32 pointerId;
    UINT32 frameId;
    UINT32 pointerFlags;
    HANDLE sourceDevice;
    HWND hwndTarget;
    POINT ptPixelLocation;
    POINT ptHimetricLocation;
    POINT ptPixelLocationRaw;
    POINT ptHimetricLocationRaw;
    DWORD dwTime;
    UINT32 historyCount;
    INT32 InputData;
    DWORD dwKeyStates;
    UINT64 PerformanceCount;
    int ButtonChangeType;         // POINTER_BUTTON_CHANGE_TYPE (an enum)
};
struct OkPointerTouchInfo {       // POINTER_TOUCH_INFO
    OkPointerInfo pointerInfo;
    UINT32 touchFlags;
    UINT32 touchMask;
    RECT rcContact;
    RECT rcContactRaw;
    UINT32 orientation;
    UINT32 pressure;              // 0..1024
};
struct OkPointerDeviceInfo {      // POINTER_DEVICE_INFO
    DWORD displayOrientation;
    HANDLE device;
    DWORD pointerDeviceType;      // POINTER_DEVICE_TYPE (an enum)
    HMONITOR monitor;
    ULONG startingCursorId;
    USHORT maxActiveContacts;
    WCHAR productString[520];     // POINTER_DEVICE_PRODUCT_STRING_MAX
};
struct OkTouchInput {             // TOUCHINPUT
    LONG x, y;                    // hundredths of a pixel, screen coordinates
    HANDLE hSource;
    DWORD dwID;
    DWORD dwFlags;
    DWORD dwMask;
    DWORD dwTime;
    ULONG_PTR dwExtraInfo;
    DWORD cxContact, cyContact;
};

struct Win32TouchApi {
    typedef BOOL (WINAPI *GetPointerTypeFn)(UINT32, DWORD*);
    typedef BOOL (WINAPI *GetPointerTouchInfoFn)(UINT32, OkPointerTouchInfo*);
    typedef BOOL (WINAPI *GetPointerDevicesFn)(UINT32*, OkPointerDeviceInfo*);
    typedef BOOL (WINAPI *RegisterTouchWindowFn)(HWND, ULONG);
    typedef BOOL (WINAPI *GetTouchInputInfoFn)(HANDLE, UINT, OkTouchInput*, int);
    typedef BOOL (WINAPI *CloseTouchInputHandleFn)(HANDLE);
    GetPointerTypeFn getPointerType = nullptr;
    GetPointerTouchInfoFn getPointerTouchInfo = nullptr;
    GetPointerDevicesFn getPointerDevices = nullptr;
    RegisterTouchWindowFn registerTouchWindow = nullptr;
    GetTouchInputInfoFn getTouchInputInfo = nullptr;
    CloseTouchInputHandleFn closeTouchInputHandle = nullptr;
    bool loaded = false;
    void Load() {
        if (loaded) return;
        loaded = true;
        HMODULE u = GetModuleHandleW(L"user32.dll");
        if (!u) return;
        getPointerType = (GetPointerTypeFn)(void*)GetProcAddress(u, "GetPointerType");
        getPointerTouchInfo = (GetPointerTouchInfoFn)(void*)GetProcAddress(u, "GetPointerTouchInfo");
        getPointerDevices = (GetPointerDevicesFn)(void*)GetProcAddress(u, "GetPointerDevices");
        registerTouchWindow = (RegisterTouchWindowFn)(void*)GetProcAddress(u, "RegisterTouchWindow");
        getTouchInputInfo = (GetTouchInputInfoFn)(void*)GetProcAddress(u, "GetTouchInputInfo");
        closeTouchInputHandle = (CloseTouchInputHandleFn)(void*)GetProcAddress(u, "CloseTouchInputHandle");
    }
    bool HasPointer() const { return getPointerType && getPointerTouchInfo; }
    bool HasTouch() const { return registerTouchWindow && getTouchInputInfo && closeTouchInputHandle; }
};
/// A mouse message Windows made from a touch contact (GetMessageExtraInfo carries the pen / touch signature with the touch bit).
bool MouseMessageFromTouch(LPARAM extra) {
    const uint32_t v = (uint32_t)(uintptr_t)extra;   // the signature is in the low 32 bits
    return (v & kMouseSignatureMask) == kMouseSignature && (v & kMouseFromTouchBit) != 0;
}
/// The client size and the client area's top-left corner on the screen.
void ClientGeometry(HWND h, int& w, int& hgt, POINT& origin) {
    RECT r; GetClientRect(h, &r);
    w = r.right - r.left; hgt = r.bottom - r.top;
    origin.x = 0; origin.y = 0;
    ClientToScreen(h, &origin);
}

class WinBackend;
WinBackend* g_backend = nullptr;   // the window procedure has no user pointer before WM_NCCREATE

// The input method's window placement (imm32.dll), loaded on first use so nothing extra is linked. Written from the Win32
// documentation of ImmGetContext, ImmSetCompositionWindow, ImmSetCandidateWindow and ImmReleaseContext.
struct ImmApi {
    typedef HIMC (WINAPI *GetContextFn)(HWND);
    typedef BOOL (WINAPI *ReleaseContextFn)(HWND, HIMC);
    typedef BOOL (WINAPI *SetCompositionWindowFn)(HIMC, LPCOMPOSITIONFORM);
    typedef BOOL (WINAPI *SetCandidateWindowFn)(HIMC, LPCANDIDATEFORM);
    GetContextFn getContext = nullptr;
    ReleaseContextFn releaseContext = nullptr;
    SetCompositionWindowFn setComposition = nullptr;
    SetCandidateWindowFn setCandidate = nullptr;
    bool tried = false;
    bool Load() {
        if (tried) return getContext != nullptr;
        tried = true;
        HMODULE m = LoadLibraryA("imm32.dll");
        if (!m) return false;
        getContext = (GetContextFn)(void*)GetProcAddress(m, "ImmGetContext");
        releaseContext = (ReleaseContextFn)(void*)GetProcAddress(m, "ImmReleaseContext");
        setComposition = (SetCompositionWindowFn)(void*)GetProcAddress(m, "ImmSetCompositionWindow");
        setCandidate = (SetCandidateWindowFn)(void*)GetProcAddress(m, "ImmSetCandidateWindow");
        if (!getContext || !releaseContext || !setComposition || !setCandidate) { getContext = nullptr; return false; }
        return true;
    }
};
ImmApi g_imm;

// ---- Display modes (Microsoft's documentation of EnumDisplaySettingsW, ChangeDisplaySettingsExW and DEVMODEW) ----
/// Every mode a display device (\\.\DISPLAY1, the szDevice of MONITORINFOEXW) lists: EnumDisplaySettingsW with mode numbers
/// 0, 1, 2 ... until it fails. Windows lists each size once per colour depth and per rate; the caller sorts and drops repeats.
/// A rate of 0 or 1 means "the hardware's default" (unknown here: 0). Modes below 16 bits per pixel are left out.
std::vector<VideoMode> ModesOfDevice(const wchar_t* device) {
    std::vector<VideoMode> out;
    DEVMODEW dm;
    for (DWORD k = 0;; ++k) {
        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsW(device, k, &dm)) break;
        if (dm.dmBitsPerPel != 0 && dm.dmBitsPerPel < 16) continue;
        out.push_back(VideoMode{(int)dm.dmPelsWidth, (int)dm.dmPelsHeight, dm.dmDisplayFrequency > 1 ? (int)dm.dmDisplayFrequency : 0});
    }
    return out;
}
/// The device name of the monitor that holds most of `hwnd`.
std::wstring DeviceOfWindow(HWND hwnd) {
    MONITORINFOEXW mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), (MONITORINFO*)&mi)) return std::wstring();
    return std::wstring(mi.szDevice);
}

class WinWindow final : public Window {
public:
    WinWindow(WinBackend* b, HWND h) : m_b(b), m_hwnd(h) {}
    ~WinWindow() override;
    uint32_t Id() const override { return (uint32_t)(uintptr_t)m_hwnd; }
    void* NativeHandle() const override { return m_hwnd; }
    void GetSize(int& w, int& h) const override { RECT r; GetClientRect(m_hwnd, &r); w = r.right; h = r.bottom; }
    // The client area's top-left corner on the desktop, as SDL gives it (and our X11 backend): the frame and title bar are not
    // counted, so a position read here and given back to SetPosition puts the window where it was, and mouse positions
    // (ClientToScreen) line up with it (Dear ImGui's multi-window panels rely on that).
    void GetPosition(int& x, int& y) const override { POINT p = {0, 0}; ClientToScreen(m_hwnd, &p); x = p.x; y = p.y; }
    void SetTitle(const std::string& t) override { SetWindowTextW(m_hwnd, Wide(t).c_str()); }
    void SetSize(int w, int h) override {
        RECT r = {0, 0, w, h};
        AdjustWindowRectEx(&r, (DWORD)GetWindowLongW(m_hwnd, GWL_STYLE), FALSE, (DWORD)GetWindowLongW(m_hwnd, GWL_EXSTYLE));
        SetWindowPos(m_hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void SetPosition(int x, int y) override {
        if (x == kCentered || y == kCentered) {
            RECT r; GetWindowRect(m_hwnd, &r);
            if (x == kCentered) x = (GetSystemMetrics(SM_CXSCREEN) - (r.right - r.left)) / 2;
            if (y == kCentered) y = (GetSystemMetrics(SM_CYSCREEN) - (r.bottom - r.top)) / 2;
            SetWindowPos(m_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            return;
        }
        // x / y are where the client area goes (GetPosition): move the frame by the difference between the two corners.
        RECT wr; GetWindowRect(m_hwnd, &wr);
        POINT c = {0, 0}; ClientToScreen(m_hwnd, &c);
        SetWindowPos(m_hwnd, nullptr, x - (c.x - wr.left), y - (c.y - wr.top), 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void SetMinimumSize(int w, int h) override { m_minW = w; m_minH = h; }
    void Show() override { ShowWindow(m_hwnd, m_noActivate ? SW_SHOWNA : SW_SHOW); }
    void Hide() override { ShowWindow(m_hwnd, SW_HIDE); }
    void Raise() override { SetForegroundWindow(m_hwnd); SetFocus(m_hwnd); }
    void Minimize() override { ShowWindow(m_hwnd, SW_MINIMIZE); }
    void Restore() override { ShowWindow(m_hwnd, SW_RESTORE); }
    bool IsFullscreen() const override { return m_full; }
    int DisplayIndex() const override;
    bool SetFullscreenMode(const VideoMode& mode) override;
    VideoMode FullscreenMode() const override { return m_mode; }
    void LeaveFullscreenMode();   // ChangeDisplaySettingsExW(device, null): the registry's (desktop) mode comes back
    void SetFullscreen(bool on) override {
        LeaveFullscreenMode();   // from an exclusive mode to desktop fullscreen or a window: the display's own mode first
        if (on == m_full) {
            if (on) CoverMonitor();   // the monitor is back at its desktop size: cover it again
            return;
        }
        if (on) {
            m_style = GetWindowLongW(m_hwnd, GWL_STYLE);
            GetWindowRect(m_hwnd, &m_rect);
            MONITORINFO mi; mi.cbSize = sizeof(mi);
            GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
            SetWindowLongW(m_hwnd, GWL_STYLE, (LONG)((m_style & ~(WS_OVERLAPPEDWINDOW)) | WS_POPUP));
            SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        } else {
            SetWindowLongW(m_hwnd, GWL_STYLE, m_style);
            SetWindowPos(m_hwnd, nullptr, m_rect.left, m_rect.top, m_rect.right - m_rect.left, m_rect.bottom - m_rect.top,
                         SWP_FRAMECHANGED | SWP_NOZORDER | SWP_SHOWWINDOW);
        }
        m_full = on;
    }
    bool IsFocused() const override { return GetFocus() == m_hwnd; }
    bool PresentPixels(const uint8_t* rgba, int w, int h) override {
        if (!rgba || w <= 0 || h <= 0) return false;
        RECT rc; GetClientRect(m_hwnd, &rc);
        const int cw = w < rc.right ? w : (int)rc.right, ch = h < rc.bottom ? h : (int)rc.bottom;   // what fits
        if (cw <= 0 || ch <= 0) return false;
        // 32-bit BGRX, top-down (negative height), the layout GDI takes without a palette.
        m_present.resize((size_t)cw * (size_t)ch * 4);
        for (int y = 0; y < ch; ++y) {
            const uint8_t* src = rgba + (size_t)y * (size_t)w * 4;
            uint8_t* dst = m_present.data() + (size_t)y * (size_t)cw * 4;
            for (int x = 0; x < cw; ++x, src += 4, dst += 4) { dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = 255; }
        }
        BITMAPINFO bi;
        ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = cw;
        bi.bmiHeader.biHeight = -ch;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        HDC dc = GetDC(m_hwnd);   // not m_dc: that one belongs to the GL context
        if (!dc) return false;
        const int lines = StretchDIBits(dc, 0, 0, cw, ch, 0, 0, cw, ch, m_present.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(m_hwnd, dc);
        return lines == ch;
    }

    bool CreateGLContext(const GLAttributes& a) override;
    void DestroyGLContext() override {
        if (m_rc) {
            if (wglGetCurrentContext() == m_rc) wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(m_rc);
            m_rc = nullptr;
        }
        if (m_dc) { ReleaseDC(m_hwnd, m_dc); m_dc = nullptr; }
    }
    bool MakeCurrent() override { return m_rc && wglMakeCurrent(m_dc, m_rc); }
    // Microsoft's documentation of wglMakeCurrent: the device context must have the same pixel format as the one the context was
    // made for. So this window takes the owner's pixel format (SetPixelFormat, once: a window's format cannot change) and the
    // owner's context is made current on this window's DC.
    bool MakeCurrentWithContextOf(Window& owner) override {
        WinWindow* o = dynamic_cast<WinWindow*>(&owner);
        if (!o || !o->m_rc || !o->m_dc) return false;
        if (!m_dc) m_dc = GetDC(m_hwnd);
        if (!m_dc) return false;
        if (!m_formatSet) {
            const int fmt = GetPixelFormat(o->m_dc);
            PIXELFORMATDESCRIPTOR pfd; std::memset(&pfd, 0, sizeof(pfd));
            if (!fmt || !DescribePixelFormat(o->m_dc, fmt, sizeof(pfd), &pfd)) return false;
            if (GetPixelFormat(m_dc) != fmt && !SetPixelFormat(m_dc, fmt, &pfd)) return false;
            m_formatSet = true;
        }
        return wglMakeCurrent(m_dc, o->m_rc) != FALSE;
    }
    // A layered window (WS_EX_LAYERED) with one alpha for the whole window (SetLayeredWindowAttributes, LWA_ALPHA); fully opaque
    // takes the layered style off again.
    bool SetOpacity(float alpha) override {
        const LONG ex = GetWindowLongW(m_hwnd, GWL_EXSTYLE);
        if (!(alpha < 1.0f)) {
            if (ex & WS_EX_LAYERED) SetWindowLongW(m_hwnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
            return true;
        }
        if (!(ex & WS_EX_LAYERED)) SetWindowLongW(m_hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        const BYTE a = (BYTE)(alpha <= 0.0f ? 0 : (int)(alpha * 255.0f + 0.5f));
        return SetLayeredWindowAttributes(m_hwnd, 0, a, LWA_ALPHA) != FALSE;
    }
    void SwapBuffers() override { if (m_dc) ::SwapBuffers(m_dc); }
    bool SetSwapInterval(int i) override {
        typedef BOOL (WINAPI *Fn)(int);
        Fn f = (Fn)(void*)wglGetProcAddress("wglSwapIntervalEXT");
        return f && f(i);
    }
    void GetDrawableSize(int& w, int& h) const override { GetSize(w, h); }

    WinBackend* m_b;
    HWND m_hwnd;
    HDC m_dc = nullptr;
    HGLRC m_rc = nullptr;
    bool m_formatSet = false;   // MakeCurrentWithContextOf set the owner's pixel format on m_dc
    bool m_noActivate = false;  // WindowDesc::noActivate: Show() does not activate the window
    int m_minW = 0, m_minH = 0;
    bool m_full = false;
    LONG m_style = 0;
    RECT m_rect = {0, 0, 0, 0};
    VideoMode m_mode;          // SetFullscreenMode: the mode switched to ({0,0,0} = none)
    std::wstring m_modeDevice; // ...and the display device it was switched on
    /// Put the (already borderless) window over the whole monitor it is on.
    void CoverMonitor() {
        MONITORINFO mi; mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi)) return;
        SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    wchar_t m_highSurrogate = 0;
    std::vector<uint8_t> m_present;   // PresentPixels: the frame as BGRX

    // Window state queries (Window::IsMinimized / IsVisible / HasMouseFocus / SetInputFocus; docs/platform_layer.md, stage 4).
    // Kept after the members so they sit clear of the other platform edits.
    bool IsMinimized() const override { return IsIconic(m_hwnd) != 0; }
    bool IsVisible() const override { return IsWindowVisible(m_hwnd) != 0; }   // a minimised window keeps WS_VISIBLE, as the seam wants
    bool HasMouseFocus() const override {
        POINT p;
        if (!GetCursorPos(&p)) return false;
        HWND under = WindowFromPoint(p);
        return under == m_hwnd || (under && IsChild(m_hwnd, under));
    }
    bool SetInputFocus() override {
        SetForegroundWindow(m_hwnd);
        SetFocus(m_hwnd);                 // its return value is the PREVIOUS focus window (NULL is not a failure): ask who has it now
        return GetFocus() == m_hwnd;
    }
    bool Flash(FlashMode m) override {
        // FLASHWINFO: FLASHW_ALL flashes the caption and the task-bar button; FLASHW_TIMERNOFG keeps flashing until the
        // window comes to the foreground; FLASHW_STOP puts it back. FlashWindowEx returns the window's state before the
        // call, not success, so there is nothing to report but "asked".
        FLASHWINFO fi;
        ZeroMemory(&fi, sizeof(fi));
        fi.cbSize = sizeof(fi);
        fi.hwnd = m_hwnd;
        if (m == FlashMode::Cancel) fi.dwFlags = FLASHW_STOP;
        else if (m == FlashMode::Briefly) { fi.dwFlags = FLASHW_ALL; fi.uCount = 1; }
        else fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
        FlashWindowEx(&fi);
        return true;
    }
    // The refresh rate of the monitor the window is on: its device name from GetMonitorInfoW, then the current mode of that
    // device (EnumDisplaySettingsW with ENUM_CURRENT_SETTINGS, dmDisplayFrequency). Windows reports 0 or 1 for "the hardware's
    // default rate": that is unknown here (0).
    int RefreshRateHz() const override {
        MONITORINFOEXW mi;
        ZeroMemory(&mi, sizeof(mi));
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), (MONITORINFO*)&mi)) return 0;
        DEVMODEW dm;
        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) return 0;
        return dm.dmDisplayFrequency > 1 ? (int)dm.dmDisplayFrequency : 0;
    }
    bool SetTextInputArea(int x, int y, int w, int h) override {
        if (!g_imm.Load()) return false;
        HIMC c = g_imm.getContext(m_hwnd);
        if (!c) return false;   // no input method on this window (e.g. an English keyboard without one)
        // The composition string starts at the caret (client coordinates); the candidate list must not cover the line
        // being typed, so it gets the caret as its position and the line as the rectangle to stay out of.
        COMPOSITIONFORM cf;
        ZeroMemory(&cf, sizeof(cf));
        cf.dwStyle = CFS_FORCE_POSITION;
        cf.ptCurrentPos.x = x; cf.ptCurrentPos.y = y;
        CANDIDATEFORM cand;
        ZeroMemory(&cand, sizeof(cand));
        cand.dwIndex = 0;
        cand.dwStyle = CFS_EXCLUDE;
        cand.ptCurrentPos.x = x; cand.ptCurrentPos.y = y;
        cand.rcArea.left = x; cand.rcArea.top = y; cand.rcArea.right = x + (w > 0 ? w : 1); cand.rcArea.bottom = y + (h > 0 ? h : 1);
        const BOOL ok1 = g_imm.setComposition(c, &cf);
        const BOOL ok2 = g_imm.setCandidate(c, &cand);
        g_imm.releaseContext(m_hwnd, c);
        return ok1 && ok2;
    }
};

class WinPad final : public Gamepad {
public:
    WinPad(WinBackend* b, int slot, int id) : m_b(b), m_slot(slot), m_id(id) {}
    int InstanceId() const override { return m_id; }
    const char* Name() const override { return "XInput controller"; }
    int16_t Axis(GamepadAxis a) const override;
    bool Button(GamepadButton b) const override;
    bool Rumble(uint16_t lo, uint16_t hi, uint32_t ms) override;
private:
    WinBackend* m_b; int m_slot; int m_id;
    mutable unsigned long long m_rumbleUntil = 0;
};

class WinBackend final : public Backend {
public:
    WinBackend() { g_backend = this; QueryPerformanceFrequency(&m_freq); QueryPerformanceCounter(&m_t0); }
    ~WinBackend() override { if (g_backend == this) g_backend = nullptr; if (m_xinput) FreeLibrary(m_xinput); if (m_shcore) FreeLibrary(m_shcore); }
    BackendKind Kind() const override { return BackendKind::Own; }

    bool Init(const InitFlags&) override {
        if (m_inited) return true;
        WNDCLASSEXW wc; std::memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &WinBackend::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.lpszClassName = L"OkaySpacePlatformWindow";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { m_err = "RegisterClassEx failed"; return false; }
        m_inited = true;
        return true;
    }
    void Shutdown() override { m_pending.clear(); }
    std::string LastError() const override { return m_err; }

    uint64_t TicksMs() override { LARGE_INTEGER n; QueryPerformanceCounter(&n); return (uint64_t)((n.QuadPart - m_t0.QuadPart) * 1000 / m_freq.QuadPart); }
    uint64_t PerfCounter() override { LARGE_INTEGER n; QueryPerformanceCounter(&n); return (uint64_t)n.QuadPart; }
    uint64_t PerfFrequency() override { return (uint64_t)m_freq.QuadPart; }
    void DelayMs(uint32_t ms) override { Sleep(ms); }

    std::unique_ptr<Window> MakeWindow(const WindowDesc& d) override {
        if (!Init(InitFlags())) return nullptr;
        DWORD style = d.borderless ? WS_POPUP : (d.resizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX));
        // A tool window has no task-bar button; a topmost one stays above the others (CreateWindowEx extended styles).
        const DWORD exStyle = (d.skipTaskbar ? WS_EX_TOOLWINDOW : 0) | (d.topmost ? WS_EX_TOPMOST : 0);
        RECT r = {0, 0, d.width, d.height};
        AdjustWindowRectEx(&r, style, FALSE, exStyle);
        int x = d.x, y = d.y;
        const int ww = r.right - r.left, wh = r.bottom - r.top;
        // A given x / y is where the client area goes (as GetPosition reads it): the frame starts r.left / r.top before it.
        if (x == kCentered) x = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2; else x += r.left;
        if (y == kCentered) y = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2; else y += r.top;
        HWND h = CreateWindowExW(exStyle, L"OkaySpacePlatformWindow", Wide(d.title).c_str(), style, x, y, ww, wh,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!h) { m_err = "CreateWindowEx failed"; return nullptr; }
        DragAcceptFiles(h, TRUE);
        m_touchApi.Load();
        // Windows 8 and later send touch as WM_POINTER* without asking; registering for WM_TOUCH would replace those, so
        // WM_TOUCH is asked for only where the pointer functions do not exist (Windows 7).
        if (!m_touchApi.HasPointer() && m_touchApi.HasTouch()) m_touchApi.registerTouchWindow(h, 0);
        auto* w = new WinWindow(this, h);
        w->m_noActivate = d.noActivate;
        m_wins[h] = w;
        if (!d.hidden) ShowWindow(h, d.maximized && d.resizable && !d.fullscreen ? SW_SHOWMAXIMIZED : d.noActivate ? SW_SHOWNA : SW_SHOW);
        if (d.fullscreen) w->SetFullscreen(true);
        return std::unique_ptr<Window>(w);
    }
    void Unregister(HWND h) {
        auto it = m_wins.find(h);
        if (it != m_wins.end()) m_touch.ForgetWindow(it->second->Id());
        m_wins.erase(h);
    }

    int TouchDeviceCount() override {
        const uint64_t now = TicksMs();
        if (m_touchCountRead && now - m_touchCountAt < 2000) return m_touchCount;
        m_touchApi.Load();
        int n = 0;
        UINT32 count = 0;
        if (m_touchApi.getPointerDevices && m_touchApi.getPointerDevices(&count, nullptr) && count > 0) {
            std::vector<OkPointerDeviceInfo> devs(count);
            if (m_touchApi.getPointerDevices(&count, devs.data()))
                for (UINT32 i = 0; i < count && i < devs.size(); ++i) n += devs[i].pointerDeviceType == kPointerDeviceTypeTouch;
        } else {
            const int dig = GetSystemMetrics(kSmDigitizer);   // Windows 7: one "touch is ready" answer, not a list
            n = (dig & kNidReady) && (dig & (kNidIntegratedTouch | kNidExternalTouch)) ? 1 : 0;
        }
        m_touchCount = n; m_touchCountAt = now; m_touchCountRead = true;
        return n;
    }

    // One touch report (window pixels from a screen position) into finger events.
    void FeedTouch(HWND h, WinWindow* win, long long id, TouchPhase phase, double screenX, double screenY, float pressure, uint32_t ms) {
        TouchSample s;
        POINT origin;
        ClientGeometry(h, s.winW, s.winH, origin);
        s.id = id; s.phase = phase; s.x = screenX - origin.x; s.y = screenY - origin.y;
        s.pressure = pressure; s.windowId = win->Id(); s.timestampMs = ms;
        std::vector<Event> out;
        m_touch.Feed(s, out);
        for (Event& e : out) m_pending.push_back(std::move(e));
    }

    bool PollEvent(Event& out) override {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { Event e; e.type = EventType::Quit; m_pending.push_back(e); continue; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (m_pending.empty()) return false;
        out = std::move(m_pending.front());
        m_pending.pop_front();
        return true;
    }
    void* GLProcAddress(const char* n) override {
        void* p = (void*)wglGetProcAddress(n);
        if (!p || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1) {
            static HMODULE gl = LoadLibraryA("opengl32.dll");
            p = gl ? (void*)(void*)GetProcAddress(gl, n) : nullptr;
        }
        return p;
    }
    void ReleaseCurrent() override { wglMakeCurrent(nullptr, nullptr); }

    void StartTextInput() override { m_text = true; }
    void StopTextInput() override { m_text = false; }
    bool IsKeyDown(Key k) override {
        static const struct { Key k; int vk; } kMap[] = {{Key::LShift, VK_LSHIFT}, {Key::RShift, VK_RSHIFT}, {Key::LCtrl, VK_LCONTROL},
            {Key::RCtrl, VK_RCONTROL}, {Key::LAlt, VK_LMENU}, {Key::RAlt, VK_RMENU}, {Key::Space, VK_SPACE}, {Key::Return, VK_RETURN},
            {Key::Escape, VK_ESCAPE}, {Key::Tab, VK_TAB}, {Key::Backspace, VK_BACK}, {Key::Left, VK_LEFT}, {Key::Right, VK_RIGHT},
            {Key::Up, VK_UP}, {Key::Down, VK_DOWN}};
        const int ik = (int)k;
        int vk = 0;
        if (ik >= (int)Key::A && ik <= (int)Key::Z) vk = 'A' + (ik - (int)Key::A);
        else if (ik >= (int)Key::N1 && ik <= (int)Key::N9) vk = '1' + (ik - (int)Key::N1);
        else if (k == Key::N0) vk = '0';
        else if (ik >= (int)Key::F1 && ik <= (int)Key::F12) vk = VK_F1 + (ik - (int)Key::F1);
        else for (auto& m : kMap) if (m.k == k) vk = m.vk;
        return vk && (GetAsyncKeyState(vk) & 0x8000);
    }
    uint32_t MouseState(int* x, int* y) override {
        POINT p; GetCursorPos(&p);
        HWND f = GetForegroundWindow();
        if (f) ScreenToClient(f, &p);
        if (x) *x = p.x;
        if (y) *y = p.y;
        uint32_t m = 0;
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) m |= MouseMask(MouseLeft);
        if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) m |= MouseMask(MouseMiddle);
        if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) m |= MouseMask(MouseRight);
        return m;
    }
    uint32_t KeyboardFocusWindowId() override {
        HWND f = GetFocus();
        return f && m_wins.count(f) ? (uint32_t)(uintptr_t)f : 0;
    }
    uint32_t MouseFocusWindowId() override {
        POINT p;
        if (!GetCursorPos(&p)) return 0;
        HWND under = WindowFromPoint(p);
        HWND top = under ? GetAncestor(under, GA_ROOT) : nullptr;
        return top && m_wins.count(top) ? (uint32_t)(uintptr_t)top : 0;
    }
    bool IsRelativeMouse() override { return m_relative; }
    uint32_t GlobalMouseState(int* x, int* y) override {
        POINT p = {0, 0};
        GetCursorPos(&p);                 // screen coordinates
        if (x) *x = p.x;
        if (y) *y = p.y;
        // GetAsyncKeyState reads the PHYSICAL buttons; with "swap mouse buttons" on, the logical left button is the physical right one.
        const bool swapped = GetSystemMetrics(SM_SWAPBUTTON) != 0;
        uint32_t m = 0;
        if (GetAsyncKeyState(swapped ? VK_RBUTTON : VK_LBUTTON) & 0x8000) m |= MouseMask(MouseLeft);
        if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) m |= MouseMask(MouseMiddle);
        if (GetAsyncKeyState(swapped ? VK_LBUTTON : VK_RBUTTON) & 0x8000) m |= MouseMask(MouseRight);
        return m;
    }
    void PumpEvents() override {          // the message loop of PollEvent, without taking an event
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            // WM_QUIT only comes when no other message is waiting. Put it back for PollEvent, which turns it into the Quit
            // event, so no event is lost and this function does not touch the pending-event queue itself.
            if (msg.message == WM_QUIT) { PostQuitMessage((int)msg.wParam); break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    bool SetRelativeMouse(bool on) override {
        m_relative = on;
        if (on) { ::ShowCursor(FALSE); CenterCursor(); } else { ::ShowCursor(TRUE); ClipCursor(nullptr); }
        return true;
    }
    bool IsCursorShown() override { return m_cursorShown; }
    uint32_t RelativeMouseState(int* dx, int* dy) override {
        if (dx) *dx = m_relAccX;
        if (dy) *dy = m_relAccY;
        m_relAccX = m_relAccY = 0;
        return MouseState(nullptr, nullptr);
    }
    bool SupportsRelativeMouseState() override { return true; }
    bool SetPerMonitorDpiAware() override {
        using SetCtx = BOOL (WINAPI*)(HANDLE);
        HMODULE u = GetModuleHandleW(L"user32.dll");
        SetCtx f = u ? (SetCtx)(void*)GetProcAddress(u, "SetProcessDpiAwarenessContext") : nullptr;
        return f && f((HANDLE)-4) != FALSE;   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    }
    bool ShowErrorBox(const std::string& title, const std::string& text, Window* parent) override {
        return MessageBoxW(parent ? (HWND)parent->NativeHandle() : nullptr, Wide(text).c_str(), Wide(title).c_str(),
                           MB_OK | MB_ICONERROR) != 0;
    }
    bool SetNativeMessageHook(NativeMessageHook hook, void* user) override { m_hook = hook; m_hookUser = user; return true; }
    void ShowCursor(bool show) override { if (show != m_cursorShown) { m_cursorShown = show; ::ShowCursor(show ? TRUE : FALSE); } }
    bool SetSystemCursor(SystemCursor c) override {
        static const LPCTSTR kIds[] = {IDC_ARROW, IDC_IBEAM, IDC_HAND, IDC_SIZEALL, IDC_SIZENS, IDC_SIZEWE, IDC_SIZENESW,
                                       IDC_SIZENWSE, IDC_NO, IDC_WAIT};
        const int i = (int)c;
        if (i < 0 || i >= (int)(sizeof(kIds) / sizeof(kIds[0]))) return false;
        HCURSOR h = LoadCursor(nullptr, kIds[i]);   // shared system cursor: never destroyed (TCHAR form: IDC_* follow UNICODE)
        if (!h) return false;
        m_shape = h;
        if (m_cursorShown && !m_relative) ::SetCursor(h);
        return true;
    }
    void WarpMouse(Window* w, int x, int y) override {
        HWND h = w ? (HWND)w->NativeHandle() : GetForegroundWindow();
        POINT p = {x, y};
        if (h) ClientToScreen(h, &p);
        SetCursorPos(p.x, p.y);
    }

    bool SetClipboardText(const std::string& t) override {
        if (!OpenClipboard(nullptr)) return false;
        EmptyClipboard();
        const std::wstring w = Wide(t);
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
        if (!g) { CloseClipboard(); return false; }
        void* p = GlobalLock(g);
        std::memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(g);
        const bool ok = SetClipboardData(CF_UNICODETEXT, g) != nullptr;
        if (!ok) GlobalFree(g);
        CloseClipboard();
        return ok;
    }
    std::string GetClipboardText() override {
        std::string out;
        if (!OpenClipboard(nullptr)) return out;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            if (const wchar_t* w = (const wchar_t*)GlobalLock(h)) { out = Utf8(w, (int)wcslen(w)); GlobalUnlock(h); }
        }
        CloseClipboard();
        return out;
    }

    // Audio: waveOut / waveIn (winmm.dll loaded at run time), OwnWin32Audio.cpp.
    std::unique_ptr<AudioDevice> OpenAudio(const AudioSpec& want, bool capture, const char* name, AudioSpec* got) override {
        std::string err;
        auto d = OpenWinmmAudio(want, capture, name, got, err);
        if (!d) m_err = err;
        return d;
    }
    int AudioDeviceCount(bool capture) override { return WinmmDeviceCount(capture); }
    std::string AudioDeviceName(int i, bool capture) override { return WinmmDeviceName(i, capture); }

    // Displays: EnumDisplayMonitors + GetMonitorInfoW (user32), the DPI from GetDpiForMonitor (shcore.dll, Windows 8.1+,
    // loaded at run time; the system DPI from GetDeviceCaps before that).
    struct MonCtx { std::vector<DisplayInfo> out; HRESULT (WINAPI* getDpi)(HMONITOR, int, UINT*, UINT*); int sysDpi; };
    std::vector<DisplayInfo> Displays() override {
        MonCtx c; c.getDpi = nullptr; c.sysDpi = 96;
        if (!m_shcoreTried) {
            m_shcoreTried = true;
            m_shcore = LoadLibraryW(L"shcore.dll");
        }
        if (m_shcore) c.getDpi = (HRESULT (WINAPI*)(HMONITOR, int, UINT*, UINT*))(void*)GetProcAddress(m_shcore, "GetDpiForMonitor");
        if (HDC dc = GetDC(nullptr)) { c.sysDpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc); }
        EnumDisplayMonitors(nullptr, nullptr, &WinBackend::MonitorProc, (LPARAM)&c);
        std::stable_sort(c.out.begin(), c.out.end(), [](const DisplayInfo& a, const DisplayInfo& b) { return a.primary && !b.primary; });
        return c.out;
    }
    std::vector<VideoMode> DisplayModes(int display) override {
        const std::vector<DisplayInfo> ds = Displays();
        if (display < 0 || display >= (int)ds.size()) return {};
        return SortedVideoModes(ModesOfDevice(Wide(ds[(size_t)display].name).c_str()));
    }
    bool CurrentDisplayMode(int display, VideoMode& out) override {
        const std::vector<DisplayInfo> ds = Displays();
        if (display < 0 || display >= (int)ds.size()) return false;
        DEVMODEW dm;
        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsW(Wide(ds[(size_t)display].name).c_str(), ENUM_CURRENT_SETTINGS, &dm)) return false;
        out = VideoMode{(int)dm.dmPelsWidth, (int)dm.dmPelsHeight, dm.dmDisplayFrequency > 1 ? (int)dm.dmDisplayFrequency : 0};
        return true;
    }
    static BOOL CALLBACK MonitorProc(HMONITOR mon, HDC, LPRECT, LPARAM lp) {
        MonCtx& c = *(MonCtx*)lp;
        MONITORINFOEXW mi; std::memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(mon, (MONITORINFO*)&mi)) return TRUE;
        DisplayInfo d;
        d.bounds = DisplayRect{mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top};
        d.usable = DisplayRect{mi.rcWork.left, mi.rcWork.top, mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top};
        d.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        d.name = Utf8(mi.szDevice, (int)wcslen(mi.szDevice));
        UINT dx = 0, dy = 0;
        if (c.getDpi && c.getDpi(mon, 0 /* MDT_EFFECTIVE_DPI */, &dx, &dy) == S_OK && dx > 0) d.scale = (float)dx / 96.0f;
        else if (c.sysDpi > 0) d.scale = (float)c.sysDpi / 96.0f;
        c.out.push_back(d);
        return TRUE;
    }

    // Controllers: XInput, four slots, loaded on first use.
    int GamepadDeviceCount() override {
        if (!LoadXInput()) return 0;
        int n = 0;
        for (int i = 0; i < 4; ++i) { XINPUT_STATE s; if (m_getState(i, &s) == ERROR_SUCCESS) ++n; }
        return n;
    }
    bool IsGamepad(int i) override { return SlotOfIndex(i) >= 0; }
    std::unique_ptr<Gamepad> OpenGamepad(int i) override {
        const int slot = SlotOfIndex(i);
        return slot < 0 ? nullptr : std::unique_ptr<Gamepad>(new WinPad(this, slot, 1 + slot));
    }
    bool XState(int slot, XINPUT_STATE& s) { return LoadXInput() && m_getState((DWORD)slot, &s) == ERROR_SUCCESS; }
    bool XVibrate(int slot, uint16_t lo, uint16_t hi) {
        if (!LoadXInput() || !m_setState) return false;
        XINPUT_VIBRATION v; v.wLeftMotorSpeed = lo; v.wRightMotorSpeed = hi;
        return m_setState((DWORD)slot, &v) == ERROR_SUCCESS;
    }

    // ---- window procedure ----------------------------------------------------------------
    static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
        WinBackend* b = g_backend;
        if (!b) return DefWindowProcW(h, m, wp, lp);
        if (b->m_hook) b->m_hook(b->m_hookUser, h, m, (uint64_t)wp, (int64_t)lp);   // SetNativeMessageHook: sent and posted messages
        auto it = b->m_wins.find(h);
        WinWindow* win = it == b->m_wins.end() ? nullptr : it->second;
        if (!win) return DefWindowProcW(h, m, wp, lp);   // messages during creation
        Event e;
        e.timestampMs = (uint32_t)b->TicksMs();
        e.windowId = win->Id();
        switch (m) {
        case WM_CLOSE:
            e.type = EventType::WindowClose; b->m_pending.push_back(e);
            if (b->m_wins.size() == 1) { Event q; q.type = EventType::Quit; q.timestampMs = e.timestampMs; b->m_pending.push_back(q); }
            return 0;   // the app destroys the window
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) { e.type = EventType::WindowResized; e.width = LOWORD(lp); e.height = HIWORD(lp); b->m_pending.push_back(e); }
            return 0;
        case WM_GETMINMAXINFO:
            if (win->m_minW || win->m_minH) {
                RECT r = {0, 0, win->m_minW, win->m_minH};
                AdjustWindowRectEx(&r, (DWORD)GetWindowLongW(h, GWL_STYLE), FALSE, (DWORD)GetWindowLongW(h, GWL_EXSTYLE));
                MINMAXINFO* mi = (MINMAXINFO*)lp;
                mi->ptMinTrackSize.x = r.right - r.left; mi->ptMinTrackSize.y = r.bottom - r.top;
                return 0;
            }
            break;
        case WM_SETFOCUS: e.type = EventType::WindowFocusGained; b->m_pending.push_back(e); return 0;
        case WM_KILLFOCUS: e.type = EventType::WindowFocusLost; b->m_pending.push_back(e); return 0;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); e.type = EventType::WindowExposed; b->m_pending.push_back(e); return 0; }
        case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP: {
            e.type = (m == WM_KEYDOWN || m == WM_SYSKEYDOWN) ? EventType::KeyDown : EventType::KeyUp;
            e.key = KeyFromScan((int)((lp >> 16) & 0xFF), (lp & (1 << 24)) != 0);
            e.repeat = e.type == EventType::KeyDown && (lp & (1 << 30)) != 0;
            e.mods = CurrentMods();
            b->m_pending.push_back(e);
            if (m == WM_SYSKEYDOWN || m == WM_SYSKEYUP) break;   // let Alt+F4 / Alt+Space through
            return 0;
        }
        case WM_CHAR: {
            if (!b->m_text) return 0;
            wchar_t c = (wchar_t)wp;
            if (c >= 0xD800 && c < 0xDC00) { win->m_highSurrogate = c; return 0; }
            std::string text;
            if (c >= 0xDC00 && c < 0xE000 && win->m_highSurrogate) { wchar_t pair[2] = {win->m_highSurrogate, c}; text = Utf8(pair, 2); win->m_highSurrogate = 0; }
            else if (c >= 0x20 && c != 0x7f) text = Utf8(&c, 1);
            if (!text.empty()) { e.type = EventType::TextInput; e.text = text; b->m_pending.push_back(e); }
            return 0;
        }
        case kWmPointerDown: case kWmPointerUpdate: case kWmPointerUp: {
            // Touch contacts only; pen and mouse pointers go on to DefWindowProc, which makes their mouse messages. So does a
            // touch contact afterwards (the primary one's mouse messages, marked fromTouch below), as SDL2 gives both.
            const UINT32 pid = (UINT32)LOWORD(wp);   // GET_POINTERID_WPARAM
            DWORD type = 0;
            if (b->m_touchApi.HasPointer() && b->m_touchApi.getPointerType(pid, &type) && type == kPtTouch) {
                OkPointerTouchInfo ti; std::memset(&ti, 0, sizeof(ti));
                if (b->m_touchApi.getPointerTouchInfo(pid, &ti)) {
                    const bool cancel = (ti.pointerInfo.pointerFlags & kPointerFlagCanceled) != 0;
                    const TouchPhase ph = m == kWmPointerDown ? TouchPhase::Begin
                                        : (m == kWmPointerUp || cancel) ? (cancel ? TouchPhase::Cancel : TouchPhase::End)
                                        : TouchPhase::Move;
                    const float pressure = (ti.touchMask & kTouchMaskPressure) ? (float)ti.pressure / 1024.0f : 1.0f;
                    b->FeedTouch(h, win, (long long)pid, ph, ti.pointerInfo.ptPixelLocation.x, ti.pointerInfo.ptPixelLocation.y,
                                 pressure, e.timestampMs);
                }
            }
            break;   // DefWindowProc: the mouse messages
        }
        case kWmTouch: {
            // Windows 7 (RegisterTouchWindow, see MakeWindow): a batch of contacts, positions in hundredths of a screen pixel.
            const UINT n = (UINT)LOWORD(wp);
            HANDLE hti = (HANDLE)lp;
            if (n > 0 && b->m_touchApi.HasTouch()) {
                std::vector<OkTouchInput> in(n);
                if (b->m_touchApi.getTouchInputInfo(hti, n, in.data(), (int)sizeof(OkTouchInput))) {
                    for (const OkTouchInput& t : in) {
                        const TouchPhase ph = (t.dwFlags & kTouchEventDown) ? TouchPhase::Begin
                                            : (t.dwFlags & kTouchEventUp) ? TouchPhase::End : TouchPhase::Move;
                        b->FeedTouch(h, win, (long long)t.dwID, ph, t.x / 100.0, t.y / 100.0, 1.0f, e.timestampMs);
                    }
                }
                b->m_touchApi.closeTouchInputHandle(hti);
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            e.type = EventType::MouseMove; e.x = (short)LOWORD(lp); e.y = (short)HIWORD(lp);
            e.fromTouch = MouseMessageFromTouch((LPARAM)GetMessageExtraInfo());
            { POINT p = {e.x, e.y}; ClientToScreen(h, &p); e.hasScreenPos = true; e.screenX = p.x; e.screenY = p.y; }
            if (b->m_relative) {
                RECT r; GetClientRect(h, &r);
                const int cx = r.right / 2, cy = r.bottom / 2;
                if (e.x == cx && e.y == cy) return 0;
                e.dx = e.x - cx; e.dy = e.y - cy;
                b->CenterCursor();
            } else { e.dx = e.x - b->m_lastX; e.dy = e.y - b->m_lastY; }
            b->m_lastX = e.x; b->m_lastY = e.y;
            b->m_relAccX += e.dx; b->m_relAccY += e.dy;
            b->m_pending.push_back(e);
            return 0;
        }
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_XBUTTONDOWN: case WM_XBUTTONUP: {
            const bool down = m == WM_LBUTTONDOWN || m == WM_MBUTTONDOWN || m == WM_RBUTTONDOWN || m == WM_XBUTTONDOWN;
            e.type = down ? EventType::MouseButtonDown : EventType::MouseButtonUp;
            e.button = (m == WM_LBUTTONDOWN || m == WM_LBUTTONUP) ? MouseLeft
                     : (m == WM_MBUTTONDOWN || m == WM_MBUTTONUP) ? MouseMiddle
                     : (m == WM_RBUTTONDOWN || m == WM_RBUTTONUP) ? MouseRight
                     : (HIWORD(wp) == XBUTTON1 ? MouseX1 : MouseX2);
            e.x = (short)LOWORD(lp); e.y = (short)HIWORD(lp); e.clicks = 1;
            { POINT p = {e.x, e.y}; ClientToScreen(h, &p); e.hasScreenPos = true; e.screenX = p.x; e.screenY = p.y; }
            e.fromTouch = MouseMessageFromTouch((LPARAM)GetMessageExtraInfo());
            if (down) SetCapture(h); else ReleaseCapture();
            b->m_pending.push_back(e);
            return (m == WM_XBUTTONDOWN || m == WM_XBUTTONUP) ? TRUE : 0;
        }
        case WM_LBUTTONDBLCLK: return 0;
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
            e.type = EventType::MouseWheel;
            const float notches = (float)(short)HIWORD(wp) / (float)WHEEL_DELTA;
            if (m == WM_MOUSEWHEEL) e.wheelY = notches; else e.wheelX = notches;
            b->m_pending.push_back(e);
            return 0;
        }
        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            const UINT n = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
            for (UINT i = 0; i < n; ++i) {
                // Ask for the length first: a fixed buffer cut the path of a file dropped from
                // a deep folder, and the editor then looked for a file that does not exist.
                const UINT need = DragQueryFileW(drop, i, nullptr, 0);
                if (need == 0) continue;
                std::wstring path((std::size_t)need + 1, L'\0');
                const UINT len = DragQueryFileW(drop, i, path.data(), need + 1);
                if (len == 0) continue;
                Event d; d.type = EventType::DropFile; d.windowId = e.windowId; d.timestampMs = e.timestampMs; d.text = Utf8(path.c_str(), (int)len);
                b->m_pending.push_back(d);
            }
            DragFinish(drop);
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && (!b->m_cursorShown || b->m_relative)) { SetCursor(nullptr); return TRUE; }
            if (LOWORD(lp) == HTCLIENT && b->m_shape) { SetCursor(b->m_shape); return TRUE; }   // SetSystemCursor's shape
            break;
        default: break;
        }
        return DefWindowProcW(h, m, wp, lp);
    }

    void CenterCursor() {
        HWND f = GetForegroundWindow();
        if (!f) return;
        RECT r; GetClientRect(f, &r);
        POINT p = {r.right / 2, r.bottom / 2};
        ClientToScreen(f, &p);
        SetCursorPos(p.x, p.y);
    }

    std::map<HWND, WinWindow*> m_wins;
    std::deque<Event> m_pending;
    bool m_text = false, m_relative = false, m_cursorShown = true, m_inited = false;
    HCURSOR m_shape = nullptr;   // SetSystemCursor (null = the class cursor, the arrow)
    NativeMessageHook m_hook = nullptr;   // SetNativeMessageHook
    void* m_hookUser = nullptr;
    int m_lastX = 0, m_lastY = 0;
    int m_relAccX = 0, m_relAccY = 0;
    std::string m_err;
    Win32TouchApi m_touchApi;      // user32's pointer / touch functions, loaded on first use
    TouchTracker m_touch{false};   // Windows makes the touch contact's mouse messages itself
    int m_touchCount = 0; uint64_t m_touchCountAt = 0; bool m_touchCountRead = false;

private:
    bool LoadXInput() {
        if (m_getState) return true;
        if (!m_xinput) m_xinput = LoadLibraryA("xinput1_4.dll");
        if (!m_xinput) m_xinput = LoadLibraryA("xinput1_3.dll");
        if (!m_xinput) return false;
        m_getState = (GetStateFn)(void*)GetProcAddress(m_xinput, "XInputGetState");
        m_setState = (SetStateFn)(void*)GetProcAddress(m_xinput, "XInputSetState");
        return m_getState != nullptr;
    }
    int SlotOfIndex(int index) {   // the index-th connected slot
        if (!LoadXInput()) return -1;
        int seen = 0;
        for (int i = 0; i < 4; ++i) { XINPUT_STATE s; if (m_getState(i, &s) == ERROR_SUCCESS) { if (seen == index) return i; ++seen; } }
        return -1;
    }
    typedef DWORD (WINAPI *GetStateFn)(DWORD, XINPUT_STATE*);
    typedef DWORD (WINAPI *SetStateFn)(DWORD, XINPUT_VIBRATION*);
    LARGE_INTEGER m_freq, m_t0;
    HMODULE m_xinput = nullptr;
    HMODULE m_shcore = nullptr;   // GetDpiForMonitor, loaded on the first Displays()
    bool m_shcoreTried = false;
    GetStateFn m_getState = nullptr;
    SetStateFn m_setState = nullptr;
};

int WinWindow::DisplayIndex() const {
    const std::wstring dev = DeviceOfWindow(m_hwnd);
    if (dev.empty()) return -1;
    const std::string name = Utf8(dev.c_str(), (int)dev.size());
    const std::vector<DisplayInfo> ds = m_b->Displays();
    for (size_t i = 0; i < ds.size(); ++i) if (ds[i].name == name) return (int)i;
    return -1;
}
bool WinWindow::SetFullscreenMode(const VideoMode& want) {
    const std::wstring dev = m_modeDevice.empty() ? DeviceOfWindow(m_hwnd) : m_modeDevice;
    if (dev.empty()) return false;
    const VideoMode pick = ClosestVideoMode(SortedVideoModes(ModesOfDevice(dev.c_str())), want);
    if (pick.width <= 0) return false;
    DEVMODEW dm;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);
    dm.dmPelsWidth = (DWORD)pick.width;
    dm.dmPelsHeight = (DWORD)pick.height;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    if (pick.refreshHz > 0) { dm.dmDisplayFrequency = (DWORD)pick.refreshHz; dm.dmFields |= DM_DISPLAYFREQUENCY; }
    // CDS_FULLSCREEN: a temporary change, not written to the registry; Windows puts the registry's mode back when this
    // process ends, and ChangeDisplaySettingsExW(device, null) does it now.
    if (ChangeDisplaySettingsExW(dev.c_str(), &dm, nullptr, CDS_FULLSCREEN, nullptr) != DISP_CHANGE_SUCCESSFUL) return false;
    m_modeDevice = dev;
    m_mode = pick;
    if (!m_full) {   // borderless over the monitor, as desktop fullscreen (SetFullscreen's own steps, without leaving the mode)
        m_style = GetWindowLongW(m_hwnd, GWL_STYLE);
        GetWindowRect(m_hwnd, &m_rect);
        SetWindowLongW(m_hwnd, GWL_STYLE, (LONG)((m_style & ~(WS_OVERLAPPEDWINDOW)) | WS_POPUP));
        m_full = true;
    }
    CoverMonitor();   // the monitor's rectangle is the new mode's size now
    return true;
}
void WinWindow::LeaveFullscreenMode() {
    if (m_modeDevice.empty()) return;
    ChangeDisplaySettingsExW(m_modeDevice.c_str(), nullptr, nullptr, 0, nullptr);
    m_modeDevice.clear();
    m_mode = VideoMode();
}

WinWindow::~WinWindow() {
    LeaveFullscreenMode();
    DestroyGLContext();
    m_b->Unregister(m_hwnd);
    DestroyWindow(m_hwnd);
}

bool WinWindow::CreateGLContext(const GLAttributes& a) {
    DestroyGLContext();
    m_dc = GetDC(m_hwnd);
    PIXELFORMATDESCRIPTOR pfd; std::memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cAlphaBits = 8; pfd.cDepthBits = (BYTE)a.depthBits;
    const int fmt = ChoosePixelFormat(m_dc, &pfd);
    if (!fmt || !SetPixelFormat(m_dc, fmt, &pfd)) return false;
    HGLRC legacy = wglCreateContext(m_dc);
    if (!legacy) return false;
    wglMakeCurrent(m_dc, legacy);
    typedef HGLRC (WINAPI *CreateAttribsFn)(HDC, HGLRC, const int*);
    CreateAttribsFn create = (CreateAttribsFn)(void*)wglGetProcAddress("wglCreateContextAttribsARB");
    if (create) {
        const int attrs[] = {kWglContextMajor, a.major, kWglContextMinor, a.minor,
                             kWglProfileMask, a.compatibility ? kWglProfileCompat : kWglProfileCore, 0};
        m_rc = create(m_dc, nullptr, attrs);
    }
    if (m_rc) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(legacy); }
    else m_rc = legacy;   // no ARB_create_context: the legacy context is what the driver offers
    return wglMakeCurrent(m_dc, m_rc) != FALSE;
}

int16_t WinPad::Axis(GamepadAxis a) const {
    XINPUT_STATE s; if (!m_b->XState(m_slot, s)) return 0;
    const XINPUT_GAMEPAD& g = s.Gamepad;
    switch (a) {
    case GamepadAxis::LeftX: return g.sThumbLX;
    case GamepadAxis::LeftY: return (int16_t)(g.sThumbLY == -32768 ? 32767 : -g.sThumbLY);   // XInput is +y up, SDL is +y down
    case GamepadAxis::RightX: return g.sThumbRX;
    case GamepadAxis::RightY: return (int16_t)(g.sThumbRY == -32768 ? 32767 : -g.sThumbRY);
    case GamepadAxis::TriggerLeft: return (int16_t)(g.bLeftTrigger * 32767 / 255);
    case GamepadAxis::TriggerRight: return (int16_t)(g.bRightTrigger * 32767 / 255);
    }
    return 0;
}
bool WinPad::Button(GamepadButton b) const {
    XINPUT_STATE s; if (!m_b->XState(m_slot, s)) return false;
    const WORD w = s.Gamepad.wButtons;
    switch (b) {
    case GamepadButton::A: return w & XINPUT_GAMEPAD_A;
    case GamepadButton::B: return w & XINPUT_GAMEPAD_B;
    case GamepadButton::X: return w & XINPUT_GAMEPAD_X;
    case GamepadButton::Y: return w & XINPUT_GAMEPAD_Y;
    case GamepadButton::Back: return w & XINPUT_GAMEPAD_BACK;
    case GamepadButton::Guide: return false;
    case GamepadButton::Start: return w & XINPUT_GAMEPAD_START;
    case GamepadButton::LeftStick: return w & XINPUT_GAMEPAD_LEFT_THUMB;
    case GamepadButton::RightStick: return w & XINPUT_GAMEPAD_RIGHT_THUMB;
    case GamepadButton::LeftShoulder: return w & XINPUT_GAMEPAD_LEFT_SHOULDER;
    case GamepadButton::RightShoulder: return w & XINPUT_GAMEPAD_RIGHT_SHOULDER;
    case GamepadButton::DpadUp: return w & XINPUT_GAMEPAD_DPAD_UP;
    case GamepadButton::DpadDown: return w & XINPUT_GAMEPAD_DPAD_DOWN;
    case GamepadButton::DpadLeft: return w & XINPUT_GAMEPAD_DPAD_LEFT;
    case GamepadButton::DpadRight: return w & XINPUT_GAMEPAD_DPAD_RIGHT;
    }
    return false;
}
bool WinPad::Rumble(uint16_t lo, uint16_t hi, uint32_t) { return m_b->XVibrate(m_slot, lo, hi); }   // duration: not timed yet

} // namespace

std::unique_ptr<Backend> CreateOwnBackend() { return std::unique_ptr<Backend>(new WinBackend()); }

} // namespace okay::platform
#else
#include "Internal.hpp"
namespace okay::platform { std::unique_ptr<Backend> CreateOwnBackend() { return nullptr; } }
#endif
