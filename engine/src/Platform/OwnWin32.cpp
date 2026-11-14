// okay::platform backend "own" for Windows: Win32 windows and input (keyboard through scan codes, raw mouse
// for relative mode), WGL for OpenGL, the Win32 clipboard, QueryPerformanceCounter timers, XInput controllers
// with hot-plug events, DPI awareness and monitor queries. Audio is in OwnWin32Audio.cpp (waveOut / waveIn).
//
// STATUS: cross-compiled with MinGW (-Wall -Wextra clean) and run under Wine on Linux (tests/test_platform_win32.cpp).
// NEVER run on real Windows: see docs/platform_layer.md for exactly what Wine covered and what it cannot.
// The pure logic (key tables, DPI math, UTF-16, click counting, pad tracking) is in Win32Logic.hpp /
// PlatformText.hpp and is also unit-tested natively on Linux.
#ifdef _WIN32
#include "Internal.hpp"
#include "Win32Audio.hpp"
#include "Win32Logic.hpp"
#include "PlatformText.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <xinput.h>
#include <cstring>
#include <deque>
#include <map>

namespace okay::platform {
namespace {

// The constants Win32Logic.hpp repeats must match the real headers.
static_assert(win32::kXiA == XINPUT_GAMEPAD_A && win32::kXiY == XINPUT_GAMEPAD_Y && win32::kXiStart == XINPUT_GAMEPAD_START &&
              win32::kXiDpadUp == XINPUT_GAMEPAD_DPAD_UP && win32::kXiRightShoulder == XINPUT_GAMEPAD_RIGHT_SHOULDER &&
              win32::kXiLeftThumb == XINPUT_GAMEPAD_LEFT_THUMB && win32::kXiBack == XINPUT_GAMEPAD_BACK,
              "XInput button constants");
static_assert('A' == 0x41 && VK_F1 == 0x70 && VK_NUMPAD0 == 0x60 && VK_OEM_PLUS == 0xBB && VK_OEM_3 == 0xC0 &&
              VK_LSHIFT == 0xA0 && VK_RMENU == 0xA5 && VK_LWIN == 0x5B && VK_SCROLL == 0x91 && VK_SNAPSHOT == 0x2C,
              "virtual key constants");

// ---- GL pieces we need without GL headers' extension loader ----
constexpr int kWglContextMajor = 0x2091, kWglContextMinor = 0x2092, kWglProfileMask = 0x9126;
constexpr int kWglProfileCore = 0x1, kWglProfileCompat = 0x2;

const wchar_t kClassName[] = L"OkaySpacePlatformWindow";

std::string Utf8(const wchar_t* w, int n) { return n > 0 ? text::Utf16ToUtf8(reinterpret_cast<const char16_t*>(w), (size_t)n) : std::string(); }
std::wstring Wide(const std::string& s) {
    const std::u16string u = text::Utf8ToUtf16(s);
    return std::wstring(reinterpret_cast<const wchar_t*>(u.data()), u.size());
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

// ---- DPI: entry points that only exist on newer Windows are looked up at run time ----
template <class Fn> Fn User32Proc(const char* name) {
    HMODULE u = GetModuleHandleW(L"user32.dll");
    return u ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(u, name))) : nullptr;
}
unsigned DpiOfWindow(HWND h) {
    using Fn = UINT (WINAPI*)(HWND);
    static Fn f = User32Proc<Fn>("GetDpiForWindow");
    if (f && h) { const UINT d = f(h); if (d) return d; }
    HDC dc = GetDC(nullptr);
    const int d = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return d > 0 ? (unsigned)d : 96u;
}
unsigned DpiOfMonitor(HMONITOR mon) {
    using Fn = HRESULT (WINAPI*)(HMONITOR, int, UINT*, UINT*);   // shcore: GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI = 0, ...)
    static Fn f = [] {
        HMODULE m = LoadLibraryA("shcore.dll");
        return m ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(m, "GetDpiForMonitor"))) : nullptr;
    }();
    UINT x = 0, y = 0;
    if (f && f(mon, 0, &x, &y) == S_OK && x) return x;
    return DpiOfWindow(nullptr);
}
void AdjustRectForDpi(RECT& r, DWORD style, DWORD exStyle, unsigned dpi) {
    using Fn = BOOL (WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static Fn f = User32Proc<Fn>("AdjustWindowRectExForDpi");
    if (f) f(&r, style, FALSE, exStyle, dpi); else AdjustWindowRectEx(&r, style, FALSE, exStyle);
}
int RefreshOfDevice(const wchar_t* device) {
    DEVMODEW dm; std::memset(&dm, 0, sizeof(dm)); dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(device, ENUM_CURRENT_SETTINGS, &dm)) return 0;
    return dm.dmDisplayFrequency > 1 ? (int)dm.dmDisplayFrequency : 0;   // 0 and 1 mean "hardware default"
}

class WinBackend;
WinBackend* g_backend = nullptr;   // the window procedure has no user pointer before WM_NCCREATE

class WinWindow final : public Window {
public:
    WinWindow(WinBackend* b, HWND h) : m_b(b), m_hwnd(h) {}
    ~WinWindow() override;
    uint32_t Id() const override { return (uint32_t)(uintptr_t)m_hwnd; }   // HWNDs are 32-bit significant (documented)
    void* NativeHandle() const override { return m_hwnd; }
    void GetSize(int& w, int& h) const override { RECT r; GetClientRect(m_hwnd, &r); w = r.right; h = r.bottom; }
    void GetPosition(int& x, int& y) const override { RECT r; GetWindowRect(m_hwnd, &r); x = r.left; y = r.top; }
    void SetTitle(const std::string& t) override { SetWindowTextW(m_hwnd, Wide(t).c_str()); }
    void SetSize(int w, int h) override {
        RECT r = {0, 0, w, h};
        AdjustRectForDpi(r, (DWORD)GetWindowLongW(m_hwnd, GWL_STYLE), (DWORD)GetWindowLongW(m_hwnd, GWL_EXSTYLE), DpiOfWindow(m_hwnd));
        SetWindowPos(m_hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void SetPosition(int x, int y) override;
    void SetMinimumSize(int w, int h) override { m_minW = w; m_minH = h; }
    void Show() override { ShowWindow(m_hwnd, SW_SHOW); }
    void Hide() override { ShowWindow(m_hwnd, SW_HIDE); }
    void Raise() override { SetForegroundWindow(m_hwnd); SetFocus(m_hwnd); }
    void Minimize() override { ShowWindow(m_hwnd, SW_MINIMIZE); }
    void Restore() override { ShowWindow(m_hwnd, SW_RESTORE); }
    bool IsFullscreen() const override { return m_full; }
    void SetFullscreen(bool on) override {
        if (on == m_full) return;
        m_full = on;   // before the SetWindowPos below: WM_GETMINMAXINFO reads it to lift the size limit
        if (on) {
            m_style = GetWindowLongW(m_hwnd, GWL_STYLE);
            m_wasMaximized = IsZoomed(m_hwnd) != FALSE;
            if (m_wasMaximized) ShowWindow(m_hwnd, SW_RESTORE);   // so the rectangle saved next is the normal one
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
            if (m_wasMaximized) ShowWindow(m_hwnd, SW_SHOWMAXIMIZED);
        }
    }
    bool IsFocused() const override { return GetFocus() == m_hwnd; }
    int RefreshRateHz() const override {
        MONITORINFOEXW mi; std::memset(&mi, 0, sizeof(mi)); mi.cbSize = sizeof(mi);
        return GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi) ? RefreshOfDevice(mi.szDevice) : 0;
    }
    unsigned DisplayDpi() const override { return DpiOfWindow(m_hwnd); }
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
        // The class is CS_OWNDC, so GetDC returns the very DC a GL context uses; a window drawn with this
        // method is not also a GL window (GDI into a double-buffered pixel format is undefined).
        HDC dc = GetDC(m_hwnd);
        if (!dc) return false;
        const int lines = StretchDIBits(dc, 0, 0, cw, ch, 0, 0, cw, ch, m_present.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(m_hwnd, dc);
        return lines == ch;
    }

    bool CreateGLContext(const GLAttributes& a) override;
    void DestroyGLContext() override {
        if (m_rc) {
            if (wglGetCurrentContext() == m_rc) wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(m_rc);   // fails if the context is current on ANOTHER thread: the caller must release it there first
            m_rc = nullptr;
        }
        if (m_dc) { ReleaseDC(m_hwnd, m_dc); m_dc = nullptr; }
    }
    bool MakeCurrent() override { return m_rc && wglMakeCurrent(m_dc, m_rc); }   // binds to the CALLING thread
    void SwapBuffers() override { if (m_dc) ::SwapBuffers(m_dc); }
    bool SetSwapInterval(int i) override {
        typedef BOOL (WINAPI *Fn)(int);
        Fn f = (Fn)(void*)wglGetProcAddress("wglSwapIntervalEXT");   // needs a current context
        return f && f(i);
    }
    void GetDrawableSize(int& w, int& h) const override { GetSize(w, h); }   // per-monitor aware: client pixels are physical pixels

    WinBackend* m_b;
    HWND m_hwnd;
    HDC m_dc = nullptr;
    HGLRC m_rc = nullptr;
    int m_minW = 0, m_minH = 0;
    bool m_full = false, m_wasMaximized = false;
    LONG m_style = 0;
    RECT m_rect = {0, 0, 0, 0};
    text::Utf16Assembler m_chars;      // WM_CHAR surrogate pairing
    std::vector<uint8_t> m_present;   // PresentPixels: the frame as BGRX
};

class WinPad final : public Gamepad {
public:
    WinPad(WinBackend* b, int slot, int id) : m_b(b), m_slot(slot), m_id(id) {}
    ~WinPad() override;
    int InstanceId() const override { return m_id; }
    const char* Name() const override { return "XInput controller"; }
    int16_t Axis(GamepadAxis a) const override;
    bool Button(GamepadButton b) const override;
    bool Rumble(uint16_t lo, uint16_t hi, uint32_t ms) override;
private:
    WinBackend* m_b; int m_slot; int m_id;
};

class WinBackend final : public Backend {
public:
    WinBackend() { g_backend = this; QueryPerformanceFrequency(&m_freq); QueryPerformanceCounter(&m_t0); }
    ~WinBackend() override {
        if (m_clipHwnd) DestroyWindow(m_clipHwnd);
        if (m_classRegistered) UnregisterClassW(kClassName, GetModuleHandleW(nullptr));   // fails (harmlessly) while windows remain
        EndTimerPeriod();
        if (g_backend == this) g_backend = nullptr;
        if (m_xinput) FreeLibrary(m_xinput);
    }
    BackendKind Kind() const override { return BackendKind::Own; }

    bool Init(const InitFlags& f) override {
        m_padsOn = f.gamepad;
        if (m_inited) return true;
        WNDCLASSEXW wc; std::memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &WinBackend::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        if (RegisterClassExW(&wc)) m_classRegistered = true;
        else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { m_err = "RegisterClassEx failed"; return false; }
        // Sleep() and waitable timers default to the ~15.6 ms system tick: ask for 1 ms for as long as we run.
        if (!m_periodActive && timeBeginPeriod(1) == TIMERR_NOERROR) m_periodActive = true;
        m_inited = true;
        return true;
    }
    void Shutdown() override {
        m_pending.clear();
        m_pads = win32::PadTracker();
        EndTimerPeriod();
        m_inited = false;
    }
    std::string LastError() const override { return m_err; }

    uint64_t TicksMs() override {
        LARGE_INTEGER n; QueryPerformanceCounter(&n);
        const uint64_t d = (uint64_t)(n.QuadPart - m_t0.QuadPart), f = (uint64_t)m_freq.QuadPart;
        return d / f * 1000 + d % f * 1000 / f;   // split: d * 1000 would overflow after months at 10 MHz
    }
    uint64_t PerfCounter() override { LARGE_INTEGER n; QueryPerformanceCounter(&n); return (uint64_t)n.QuadPart; }
    uint64_t PerfFrequency() override { return (uint64_t)m_freq.QuadPart; }
    void DelayMs(uint32_t ms) override { Sleep(ms); }

    std::unique_ptr<Window> MakeWindow(const WindowDesc& d) override {
        if (!Init(InitFlags())) return nullptr;
        // WS_CLIPCHILDREN | WS_CLIPSIBLINGS: the documented requirement for a window that carries an OpenGL context.
        const DWORD clip = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
        DWORD style = clip | (d.borderless ? WS_POPUP : (d.resizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX)));
        RECT r = {0, 0, ValidWindowExtent(d.width, 1280), ValidWindowExtent(d.height, 720)};
        AdjustRectForDpi(r, style, 0, DpiOfWindow(nullptr));
        int x = d.x, y = d.y;
        const int ww = r.right - r.left, wh = r.bottom - r.top;
        if (x == kCentered) x = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
        if (y == kCentered) y = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
        HWND h = CreateWindowExW(0, kClassName, Wide(d.title).c_str(), style, x, y, ww, wh,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!h) { m_err = "CreateWindowEx failed"; return nullptr; }
        DragAcceptFiles(h, TRUE);
        auto* w = new WinWindow(this, h);
        m_wins[h] = w;
        if (!d.hidden) ShowWindow(h, d.maximized && d.resizable && !d.fullscreen ? SW_SHOWMAXIMIZED : SW_SHOW);
        if (d.fullscreen) w->SetFullscreen(true);
        return std::unique_ptr<Window>(w);
    }
    void Unregister(HWND h) {
        m_wins.erase(h);
        if (m_focus == h) { m_focus = nullptr; ApplyRelative(); }
    }

    bool PollEvent(Event& out) override {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { Event e; e.type = EventType::Quit; m_pending.push_back(e); continue; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        PumpPads();
        if (m_pending.empty()) return false;
        out = std::move(m_pending.front());
        m_pending.pop_front();
        return true;
    }
    void* GLProcAddress(const char* n) override {
        void* p = (void*)wglGetProcAddress(n);
        if (!p || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1) {   // the documented failure values
            static HMODULE gl = LoadLibraryA("opengl32.dll");
            p = gl ? (void*)(void*)GetProcAddress(gl, n) : nullptr;
        }
        return p;
    }
    void ReleaseCurrent() override { wglMakeCurrent(nullptr, nullptr); }

    void StartTextInput() override { m_text = true; }
    void StopTextInput() override { m_text = false; for (auto& w : m_wins) w.second->m_chars.Reset(); }
    bool IsKeyDown(Key k) override {
        const int vk = win32::VkForKey(k);
        return vk && (GetAsyncKeyState(vk) & 0x8000);
    }
    Key KeyForChar(char c) override {
        const SHORT vks = VkKeyScanW((wchar_t)(unsigned char)c);   // the CURRENT keyboard layout
        if (vks == -1) return Backend::KeyForChar(c);
        const UINT scan = MapVirtualKeyW((UINT)(vks & 0xFF), MAPVK_VK_TO_VSC);
        const Key k = scan ? win32::KeyFromScan((int)scan, false) : Key::Unknown;
        return k != Key::Unknown ? k : Backend::KeyForChar(c);
    }
    uint32_t MouseState(int* x, int* y) override {
        POINT p; GetCursorPos(&p);
        if (HWND f = ActiveHwnd()) ScreenToClient(f, &p);
        if (x) *x = p.x;
        if (y) *y = p.y;
        const bool swap = GetSystemMetrics(SM_SWAPBUTTON) != 0;   // GetAsyncKeyState reports physical buttons
        uint32_t m = 0;
        if (GetAsyncKeyState(swap ? VK_RBUTTON : VK_LBUTTON) & 0x8000) m |= MouseMask(MouseLeft);
        if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) m |= MouseMask(MouseMiddle);
        if (GetAsyncKeyState(swap ? VK_LBUTTON : VK_RBUTTON) & 0x8000) m |= MouseMask(MouseRight);
        if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) m |= MouseMask(MouseX1);
        if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) m |= MouseMask(MouseX2);
        return m;
    }
    bool SetRelativeMouse(bool on) override {
        m_relative = on;
        m_raw.ResetAbsolute();
        return ApplyRelative();
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
        if (SetCtx f = User32Proc<SetCtx>("SetProcessDpiAwarenessContext")) {
            if (f((HANDLE)-4)) return true;                       // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 (1703+)
            if (GetLastError() == ERROR_ACCESS_DENIED) return true;   // already set (manifest or an earlier call)
            if (f((HANDLE)-3)) return true;                       // PER_MONITOR_AWARE (v1)
        }
        using SetAw = HRESULT (WINAPI*)(int);
        if (HMODULE sh = LoadLibraryA("shcore.dll")) {
            SetAw g = reinterpret_cast<SetAw>(reinterpret_cast<void*>(GetProcAddress(sh, "SetProcessDpiAwareness")));
            if (g) { const HRESULT hr = g(2); if (hr == S_OK || hr == E_ACCESSDENIED) return true; }   // 8.1
        }
        using SetOld = BOOL (WINAPI*)();
        SetOld old = User32Proc<SetOld>("SetProcessDPIAware");     // Vista: system-DPI aware only
        return old && old() != FALSE;
    }
    int DisplayCount() override { std::vector<HMONITOR> v; Monitors(v); return (int)v.size(); }
    bool GetDisplayInfo(int index, DisplayInfo& out) override {
        std::vector<HMONITOR> v; Monitors(v);
        if (index < 0 || index >= (int)v.size()) return false;
        MONITORINFOEXW mi; std::memset(&mi, 0, sizeof(mi)); mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(v[(size_t)index], &mi)) return false;
        out.x = mi.rcMonitor.left; out.y = mi.rcMonitor.top;
        out.width = mi.rcMonitor.right - mi.rcMonitor.left; out.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
        out.workX = mi.rcWork.left; out.workY = mi.rcWork.top;
        out.workWidth = mi.rcWork.right - mi.rcWork.left; out.workHeight = mi.rcWork.bottom - mi.rcWork.top;
        out.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        out.dpi = DpiOfMonitor(v[(size_t)index]);
        out.refreshHz = RefreshOfDevice(mi.szDevice);
        out.name = Utf8(mi.szDevice, (int)wcslen(mi.szDevice));
        return true;
    }
    bool ShowErrorBox(const std::string& title, const std::string& text, Window* parent) override {
        return MessageBoxW(parent ? (HWND)parent->NativeHandle() : nullptr, Wide(text).c_str(), Wide(title).c_str(),
                           MB_OK | MB_ICONERROR | MB_SETFOREGROUND) != 0;
    }
    void ShowCursor(bool show) override {
        // No ShowCursor() counter calls: that counter is process-global and unbalanced calls leave the cursor
        // stuck hidden. WM_SETCURSOR hides it over our client area; SetCursor applies it right now.
        m_cursorShown = show;
        RefreshCursor();
    }
    void WarpMouse(Window* w, int x, int y) override {
        HWND h = w ? (HWND)w->NativeHandle() : ActiveHwnd();
        POINT p = {x, y};
        if (h) ClientToScreen(h, &p);
        SetCursorPos(p.x, p.y);
        m_lastX = x; m_lastY = y;   // the WM_MOUSEMOVE this causes is not user motion
    }

    // Clipboard. OpenClipboard needs a real owner window: with a null one EmptyClipboard succeeds but then
    // SetClipboardData fails (documented), so a hidden message-only window owns it.
    bool SetClipboardText(const std::string& t) override {
        if (!OpenClip()) return false;
        EmptyClipboard();
        const std::wstring w = Wide(text::ToCrlf(t));
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
        if (!g) { CloseClipboard(); return false; }
        void* p = GlobalLock(g);
        if (!p) { GlobalFree(g); CloseClipboard(); return false; }
        std::memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(g);
        const bool ok = SetClipboardData(CF_UNICODETEXT, g) != nullptr;
        if (!ok) GlobalFree(g);   // on success the system owns the memory
        CloseClipboard();
        return ok;
    }
    std::string GetClipboardText() override {
        std::string out;
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClip()) return out;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            if (const wchar_t* w = (const wchar_t*)GlobalLock(h)) {
                const size_t cap = GlobalSize(h) / sizeof(wchar_t);   // never trust the terminator alone
                size_t n = 0;
                while (n < cap && w[n]) ++n;
                out = text::FromCrlf(Utf8(w, (int)n));
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
        return out;
    }

    std::unique_ptr<AudioDevice> OpenAudio(const AudioSpec& want, bool capture, const char* deviceName, AudioSpec* got) override {
        return win32::OpenWaveDevice(want, capture, deviceName, got, m_err);
    }
    int AudioDeviceCount(bool capture) override { return win32::WaveDeviceCount(capture); }
    std::string AudioDeviceName(int index, bool capture) override { return win32::WaveDeviceName(index, capture); }

    // Controllers: XInput, four slots, loaded on first use. Hot-plug is polled from PollEvent on the schedule in
    // win32::PadTracker (empty slots once a second: XInputGetState on an empty slot is the slow call).
    int GamepadDeviceCount() override { EnsurePadScan(); return m_pads.Count(); }
    bool IsGamepad(int i) override { EnsurePadScan(); return m_pads.SlotOfIndex(i) >= 0; }
    std::unique_ptr<Gamepad> OpenGamepad(int i) override {
        EnsurePadScan();
        const int slot = m_pads.SlotOfIndex(i);
        return slot < 0 ? nullptr : std::unique_ptr<Gamepad>(new WinPad(this, slot, m_pads.Instance(slot)));
    }
    int PadInstance(int slot) const { return m_pads.Instance(slot); }
    // A fresh state, read at most once per millisecond per slot however often the game asks for axes and buttons.
    bool XState(int slot, XINPUT_STATE& s) {
        if (!m_pads.Connected(slot) || !LoadXInput()) return false;
        const uint64_t now = TicksMs();
        PadCache& c = m_cache[slot];
        if (!c.valid || c.stamp != now) {
            if (m_getState((DWORD)slot, &c.state) != ERROR_SUCCESS) {
                std::vector<win32::PadTracker::Change> ch;
                m_pads.MarkLost(slot, ch);
                Emit(ch);
                c.valid = false;
                return false;
            }
            c.valid = true; c.stamp = now;
        }
        s = c.state;
        return true;
    }
    bool XVibrate(int slot, uint16_t lo, uint16_t hi, uint32_t ms) {
        if (!m_pads.Connected(slot) || !LoadXInput() || !m_setState) return false;
        XINPUT_VIBRATION v; v.wLeftMotorSpeed = lo; v.wRightMotorSpeed = hi;
        if (m_setState((DWORD)slot, &v) != ERROR_SUCCESS) return false;
        m_rumbleUntil[slot] = (lo || hi) ? TicksMs() + (ms ? ms : 1) : 0;
        return true;
    }
    void StopRumble(int slot) { if (m_rumbleUntil[slot]) XVibrate(slot, 0, 0, 0); }

    // ---- window procedure ----------------------------------------------------------------
    static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
        WinBackend* b = g_backend;
        if (!b) return DefWindowProcW(h, m, wp, lp);
        auto it = b->m_wins.find(h);
        WinWindow* win = it == b->m_wins.end() ? nullptr : it->second;
        if (!win) return DefWindowProcW(h, m, wp, lp);   // messages during creation, and the clipboard window
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
            if (b->m_relative && h == b->m_focus) b->ClipToWindow(h);
            return 0;
        case WM_MOVE:
            if (b->m_relative && h == b->m_focus) b->ClipToWindow(h);
            break;
        case WM_DPICHANGED: {
            // Per-monitor aware: the window moved to a monitor with another scale. Take the size Windows suggests
            // (keeps the same physical size); WM_SIZE then reports the new client size.
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            MINMAXINFO* mi = (MINMAXINFO*)lp;
            if (win->m_full) {   // a popup window may be bigger than the primary monitor's default maximum
                MONITORINFO mon; mon.cbSize = sizeof(mon);
                if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mon)) {
                    mi->ptMaxPosition.x = mon.rcMonitor.left; mi->ptMaxPosition.y = mon.rcMonitor.top;
                    mi->ptMaxSize.x = mi->ptMaxTrackSize.x = mon.rcMonitor.right - mon.rcMonitor.left;
                    mi->ptMaxSize.y = mi->ptMaxTrackSize.y = mon.rcMonitor.bottom - mon.rcMonitor.top;
                }
            }
            if (win->m_minW || win->m_minH) {
                RECT r = {0, 0, win->m_minW, win->m_minH};
                AdjustRectForDpi(r, (DWORD)GetWindowLongW(h, GWL_STYLE), (DWORD)GetWindowLongW(h, GWL_EXSTYLE), DpiOfWindow(h));
                mi->ptMinTrackSize.x = r.right - r.left; mi->ptMinTrackSize.y = r.bottom - r.top;
            }
            return 0;
        }
        case WM_SETFOCUS:
            b->m_focus = h;
            b->ApplyRelative();
            e.type = EventType::WindowFocusGained; b->m_pending.push_back(e);
            return 0;
        case WM_KILLFOCUS:
            if (b->m_focus == h) { b->m_focus = nullptr; b->ApplyRelative(); }
            win->m_chars.Reset();
            b->m_buttonsDown = 0;
            e.type = EventType::WindowFocusLost; b->m_pending.push_back(e);
            return 0;
        case WM_CAPTURECHANGED: b->m_buttonsDown = 0; break;
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); e.type = EventType::WindowExposed; b->m_pending.push_back(e); return 0; }
        case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP: {
            e.type = (m == WM_KEYDOWN || m == WM_SYSKEYDOWN) ? EventType::KeyDown : EventType::KeyUp;
            e.key = win32::KeyFromScan((int)((lp >> 16) & 0xFF), (lp & (1 << 24)) != 0);
            e.repeat = e.type == EventType::KeyDown && (lp & (1 << 30)) != 0;
            e.mods = CurrentMods();
            b->m_pending.push_back(e);
            if (m == WM_SYSKEYDOWN || m == WM_SYSKEYUP) break;   // let Alt+F4 / Alt+Space through
            return 0;
        }
        case WM_SYSCHAR:
            if (wp != VK_SPACE) return 0;   // no "ding" for Alt+letter; Alt+Space still opens the window menu
            break;
        case WM_CHAR: {
            if (!b->m_text) return 0;
            std::string t;
            if (win->m_chars.Feed((char16_t)wp, t) && !t.empty()) { e.type = EventType::TextInput; e.text = t; b->m_pending.push_back(e); }
            return 0;
        }
        case WM_UNICHAR: {   // a code point sent by a tool that does not know about surrogates
            if (wp == UNICODE_NOCHAR) return TRUE;   // "do you handle WM_UNICHAR?"
            if (b->m_text && wp >= 0x20 && wp != 0x7F) {
                std::string t; text::AppendUtf8(t, (uint32_t)wp);
                e.type = EventType::TextInput; e.text = t; b->m_pending.push_back(e);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            const int px = (short)LOWORD(lp), py = (short)HIWORD(lp);
            if (b->m_relative) { b->m_lastX = px; b->m_lastY = py; return 0; }   // relative motion comes from WM_INPUT
            e.type = EventType::MouseMove; e.x = px; e.y = py;
            e.dx = px - b->m_lastX; e.dy = py - b->m_lastY;
            b->m_lastX = px; b->m_lastY = py;
            b->m_relAccX += e.dx; b->m_relAccY += e.dy;
            b->m_pending.push_back(e);
            return 0;
        }
        case WM_INPUT: {   // raw mouse deltas, registered only while relative mode is on
            RAWINPUT ri;
            UINT size = sizeof(ri);
            if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 && ri.header.dwType == RIM_TYPEMOUSE && b->m_relative) {
                b->m_raw.Add(ri.data.mouse.usFlags, ri.data.mouse.lLastX, ri.data.mouse.lLastY);
                int dx, dy;
                b->m_raw.Take(dx, dy);
                if (dx || dy) {
                    e.type = EventType::MouseMove; e.x = b->m_lastX; e.y = b->m_lastY; e.dx = dx; e.dy = dy;
                    b->m_relAccX += dx; b->m_relAccY += dy;
                    b->m_pending.push_back(e);
                }
            }
            break;   // DefWindowProc must see WM_INPUT for the system's cleanup
        }
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_XBUTTONDOWN: case WM_XBUTTONUP: {
            const bool down = m == WM_LBUTTONDOWN || m == WM_MBUTTONDOWN || m == WM_RBUTTONDOWN || m == WM_XBUTTONDOWN;
            e.type = down ? EventType::MouseButtonDown : EventType::MouseButtonUp;
            e.button = (m == WM_LBUTTONDOWN || m == WM_LBUTTONUP) ? MouseLeft
                     : (m == WM_MBUTTONDOWN || m == WM_MBUTTONUP) ? MouseMiddle
                     : (m == WM_RBUTTONDOWN || m == WM_RBUTTONUP) ? MouseRight
                     : (HIWORD(wp) == XBUTTON1 ? MouseX1 : MouseX2);
            e.x = (short)LOWORD(lp); e.y = (short)HIWORD(lp);
            e.clicks = down ? b->m_clicks.Down(e.button, e.x, e.y, (uint32_t)GetMessageTime(), GetDoubleClickTime(),
                                              GetSystemMetrics(SM_CXDOUBLECLK) / 2) : 1;
            // Capture while ANY button is held (so a drag keeps reporting outside the window), released with the last one.
            const uint32_t bit = MouseMask(e.button);
            if (down) { if (!b->m_buttonsDown) SetCapture(h); b->m_buttonsDown |= bit; }
            else { b->m_buttonsDown &= ~bit; if (!b->m_buttonsDown) ReleaseCapture(); }
            b->m_pending.push_back(e);
            return (m == WM_XBUTTONDOWN || m == WM_XBUTTONUP) ? TRUE : 0;
        }
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
            e.type = EventType::MouseWheel;
            const float notches = (float)(short)HIWORD(wp) / (float)WHEEL_DELTA;
            if (m == WM_MOUSEWHEEL) e.wheelY = notches; else e.wheelX = notches;
            POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};   // wheel messages carry SCREEN coordinates
            ScreenToClient(h, &p);
            e.x = p.x; e.y = p.y;
            b->m_pending.push_back(e);
            return 0;
        }
        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            const UINT n = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
            for (UINT i = 0; i < n; ++i) {
                const UINT need = DragQueryFileW(drop, i, nullptr, 0);   // length without the terminator: no MAX_PATH truncation
                std::wstring path((size_t)need + 1, L'\0');
                const UINT len = DragQueryFileW(drop, i, &path[0], need + 1);
                Event d; d.type = EventType::DropFile; d.windowId = e.windowId; d.timestampMs = e.timestampMs; d.text = Utf8(path.c_str(), (int)len);
                b->m_pending.push_back(d);
            }
            DragFinish(drop);
            return 0;
        }
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && (!b->m_cursorShown || b->m_relative)) { SetCursor(nullptr); return TRUE; }
            break;
        default: break;
        }
        return DefWindowProcW(h, m, wp, lp);
    }

    // The window that keyboard and mouse queries refer to: the focused one of ours, else any of ours.
    HWND ActiveHwnd() const {
        if (m_focus && m_wins.count(m_focus)) return m_focus;
        return m_wins.empty() ? nullptr : m_wins.begin()->first;
    }
    void ClipToWindow(HWND h) {
        RECT r; GetClientRect(h, &r);
        POINT a = {r.left, r.top}, c = {r.right, r.bottom};
        ClientToScreen(h, &a); ClientToScreen(h, &c);
        RECT s = {a.x, a.y, c.x, c.y};
        ClipCursor(&s);
    }
    void RefreshCursor() {
        HWND f = ActiveHwnd();
        POINT p; RECT r;
        if (f && GetCursorPos(&p) && GetClientRect(f, &r) && ScreenToClient(f, &p) && PtInRect(&r, p))
            SetCursor((m_cursorShown && !m_relative) ? LoadCursorA(nullptr, IDC_ARROW) : nullptr);
    }
    // Relative mode = raw mouse input to the focused window + the cursor confined to it and hidden. Returns false
    // when raw input could not be registered (the mode then does nothing; motion events stay absolute).
    bool ApplyRelative() {
        bool ok = true;
        HWND f = m_focus && m_wins.count(m_focus) ? m_focus : nullptr;
        RAWINPUTDEVICE rid; std::memset(&rid, 0, sizeof(rid));
        rid.usUsagePage = 0x01; rid.usUsage = 0x02;   // generic desktop / mouse
        if (m_relative && f) {
            rid.dwFlags = 0; rid.hwndTarget = f;
            ok = RegisterRawInputDevices(&rid, 1, sizeof(rid)) != FALSE;
            ClipToWindow(f);
            m_rawRegistered = ok;
        } else {
            if (m_rawRegistered) { rid.dwFlags = RIDEV_REMOVE; rid.hwndTarget = nullptr; RegisterRawInputDevices(&rid, 1, sizeof(rid)); m_rawRegistered = false; }
            ClipCursor(nullptr);   // not confined without focus either, or the cursor would be stuck for other windows
        }
        RefreshCursor();
        return ok;
    }

    std::map<HWND, WinWindow*> m_wins;
    std::deque<Event> m_pending;
    bool m_text = false, m_relative = false, m_cursorShown = true, m_inited = false, m_classRegistered = false, m_periodActive = false;
    bool m_rawRegistered = false, m_padsOn = true;
    HWND m_focus = nullptr, m_clipHwnd = nullptr;
    int m_lastX = 0, m_lastY = 0;
    int m_relAccX = 0, m_relAccY = 0;
    uint32_t m_buttonsDown = 0;
    win32::ClickCounter m_clicks;
    win32::RawMouseAccumulator m_raw;
    std::string m_err;

private:
    void EndTimerPeriod() { if (m_periodActive) { timeEndPeriod(1); m_periodActive = false; } }

    static BOOL CALLBACK MonitorCb(HMONITOR m, HDC, LPRECT, LPARAM lp) { reinterpret_cast<std::vector<HMONITOR>*>(lp)->push_back(m); return TRUE; }
    static void Monitors(std::vector<HMONITOR>& v) {
        EnumDisplayMonitors(nullptr, nullptr, &WinBackend::MonitorCb, reinterpret_cast<LPARAM>(&v));
        // Primary first, as SDL lists it.
        for (size_t i = 1; i < v.size(); ++i) {
            MONITORINFO mi; mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(v[i], &mi) && (mi.dwFlags & MONITORINFOF_PRIMARY)) { std::swap(v[0], v[i]); break; }
        }
    }

    bool OpenClip() {
        if (!m_clipHwnd) {
            if (!Init(InitFlags())) return false;
            m_clipHwnd = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (!m_clipHwnd) return false;
        }
        for (int tries = 0; tries < 10; ++tries) {   // another program may hold the clipboard for a moment
            if (OpenClipboard(m_clipHwnd)) return true;
            Sleep(5);
        }
        return false;
    }

    bool LoadXInput() {
        if (m_getState) return true;
        if (m_xinputTried) return false;   // no DLL: do not call LoadLibrary every frame
        m_xinputTried = true;
        for (const char* dll : {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"}) {
            m_xinput = LoadLibraryA(dll);
            if (m_xinput) break;
        }
        if (!m_xinput) return false;
        m_getState = (GetStateFn)(void*)GetProcAddress(m_xinput, "XInputGetState");
        m_setState = (SetStateFn)(void*)GetProcAddress(m_xinput, "XInputSetState");
        return m_getState != nullptr;
    }
    void Emit(const std::vector<win32::PadTracker::Change>& ch) {
        for (const auto& c : ch) {
            Event e;
            e.type = c.added ? EventType::GamepadAdded : EventType::GamepadRemoved;
            e.deviceId = c.added ? c.deviceIndex : c.instance;   // Added: index for OpenGamepad; Removed: Gamepad::InstanceId()
            e.timestampMs = (uint32_t)TicksMs();
            m_pending.push_back(e);
            if (!c.added) { m_cache[c.slot].valid = false; m_rumbleUntil[c.slot] = 0; }
        }
    }
    void ScanPads() {
        if (!m_padsOn || !LoadXInput()) return;
        std::vector<win32::PadTracker::Change> ch;
        m_pads.Poll(TicksMs(), [this](int slot) { XINPUT_STATE s; return m_getState((DWORD)slot, &s) == ERROR_SUCCESS; }, ch);
        Emit(ch);
    }
    void EnsurePadScan() { if (!m_pads.Scanned()) ScanPads(); }
    void PumpPads() {
        ScanPads();
        const uint64_t now = TicksMs();
        for (int s = 0; s < 4; ++s) if (m_rumbleUntil[s] && now >= m_rumbleUntil[s]) XVibrate(s, 0, 0, 0);   // timed rumble ends
    }

    typedef DWORD (WINAPI *GetStateFn)(DWORD, XINPUT_STATE*);
    typedef DWORD (WINAPI *SetStateFn)(DWORD, XINPUT_VIBRATION*);
    struct PadCache { XINPUT_STATE state; uint64_t stamp = 0; bool valid = false; };
    LARGE_INTEGER m_freq, m_t0;
    HMODULE m_xinput = nullptr;
    bool m_xinputTried = false;
    GetStateFn m_getState = nullptr;
    SetStateFn m_setState = nullptr;
    win32::PadTracker m_pads;
    PadCache m_cache[4];
    uint64_t m_rumbleUntil[4] = {0, 0, 0, 0};
};

void WinWindow::SetPosition(int x, int y) {
    if (x == kCentered || y == kCentered) {
        RECT r; GetWindowRect(m_hwnd, &r);
        MONITORINFO mi; mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);   // the monitor it is on, not always the primary
        if (x == kCentered) x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - (r.right - r.left)) / 2;
        if (y == kCentered) y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - (r.bottom - r.top)) / 2;
    }
    SetWindowPos(m_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

WinWindow::~WinWindow() {
    DestroyGLContext();
    m_b->Unregister(m_hwnd);
    DestroyWindow(m_hwnd);
}

bool WinWindow::CreateGLContext(const GLAttributes& a) {
    DestroyGLContext();
    m_dc = GetDC(m_hwnd);
    if (!m_dc) return false;
    // A window's pixel format can be set ONCE: a second context on the same window (after DestroyGLContext) reuses it.
    if (GetPixelFormat(m_dc) == 0) {
        PIXELFORMATDESCRIPTOR pfd; std::memset(&pfd, 0, sizeof(pfd));
        pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cAlphaBits = 8; pfd.cDepthBits = (BYTE)a.depthBits;
        const int fmt = ChoosePixelFormat(m_dc, &pfd);
        if (!fmt || !SetPixelFormat(m_dc, fmt, &pfd)) { ReleaseDC(m_hwnd, m_dc); m_dc = nullptr; return false; }
    }
    HGLRC legacy = wglCreateContext(m_dc);
    if (!legacy) { ReleaseDC(m_hwnd, m_dc); m_dc = nullptr; return false; }
    wglMakeCurrent(m_dc, legacy);   // wglGetProcAddress needs a current context to return the ARB entry point
    typedef HGLRC (WINAPI *CreateAttribsFn)(HDC, HGLRC, const int*);
    CreateAttribsFn create = (CreateAttribsFn)(void*)wglGetProcAddress("wglCreateContextAttribsARB");
    if (create) {
        const int attrs[] = {kWglContextMajor, a.major, kWglContextMinor, a.minor,
                             kWglProfileMask, a.compatibility ? kWglProfileCompat : kWglProfileCore, 0};
        m_rc = create(m_dc, nullptr, attrs);
    }
    if (m_rc) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(legacy); }
    else m_rc = legacy;   // no ARB_create_context: the legacy context is what the driver offers
    if (!wglMakeCurrent(m_dc, m_rc)) {
        wglDeleteContext(m_rc); m_rc = nullptr;
        ReleaseDC(m_hwnd, m_dc); m_dc = nullptr;
        return false;
    }
    return true;
}

WinPad::~WinPad() { m_b->StopRumble(m_slot); }

int16_t WinPad::Axis(GamepadAxis a) const {
    XINPUT_STATE s; if (!m_b->XState(m_slot, s)) return 0;
    if (m_b->PadInstance(m_slot) != m_id) return 0;   // the slot now holds a different (re-plugged) pad
    const XINPUT_GAMEPAD& g = s.Gamepad;
    switch (a) {
    case GamepadAxis::LeftX: return g.sThumbLX;
    case GamepadAxis::LeftY: return win32::XiFlipY(g.sThumbLY);
    case GamepadAxis::RightX: return g.sThumbRX;
    case GamepadAxis::RightY: return win32::XiFlipY(g.sThumbRY);
    case GamepadAxis::TriggerLeft: return win32::XiTrigger(g.bLeftTrigger);
    case GamepadAxis::TriggerRight: return win32::XiTrigger(g.bRightTrigger);
    }
    return 0;
}
bool WinPad::Button(GamepadButton b) const {
    XINPUT_STATE s; if (!m_b->XState(m_slot, s)) return false;
    if (m_b->PadInstance(m_slot) != m_id) return false;
    return (s.Gamepad.wButtons & win32::XiButtonMask(b)) != 0;
}
bool WinPad::Rumble(uint16_t lo, uint16_t hi, uint32_t ms) {
    if (m_b->PadInstance(m_slot) != m_id) return false;
    return m_b->XVibrate(m_slot, lo, hi, ms);
}

} // namespace

std::unique_ptr<Backend> CreateOwnBackend() { return std::unique_ptr<Backend>(new WinBackend()); }

} // namespace okay::platform
#else
#include "Internal.hpp"
namespace okay::platform { std::unique_ptr<Backend> CreateOwnBackend() { return nullptr; } }
#endif
