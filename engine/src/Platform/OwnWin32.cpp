// okay::platform backend "own" for Windows: Win32 windows and input, WGL for OpenGL,
// the Win32 clipboard, QueryPerformanceCounter timers and XInput controllers.
//
// STATUS: COMPILES, UNTESTED. This file is only syntax-checked with MinGW on a Linux
// machine; it has never been run on Windows. Audio devices are not written yet
// (OpenAudio returns null). Treat every behaviour here as a first draft.
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
#include <cstring>
#include <deque>
#include <map>

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

class WinBackend;
WinBackend* g_backend = nullptr;   // the window procedure has no user pointer before WM_NCCREATE

class WinWindow final : public Window {
public:
    WinWindow(WinBackend* b, HWND h) : m_b(b), m_hwnd(h) {}
    ~WinWindow() override;
    uint32_t Id() const override { return (uint32_t)(uintptr_t)m_hwnd; }
    void* NativeHandle() const override { return m_hwnd; }
    void GetSize(int& w, int& h) const override { RECT r; GetClientRect(m_hwnd, &r); w = r.right; h = r.bottom; }
    void GetPosition(int& x, int& y) const override { RECT r; GetWindowRect(m_hwnd, &r); x = r.left; y = r.top; }
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
        }
        SetWindowPos(m_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void SetMinimumSize(int w, int h) override { m_minW = w; m_minH = h; }
    void Show() override { ShowWindow(m_hwnd, SW_SHOW); }
    void Hide() override { ShowWindow(m_hwnd, SW_HIDE); }
    void Raise() override { SetForegroundWindow(m_hwnd); SetFocus(m_hwnd); }
    void Minimize() override { ShowWindow(m_hwnd, SW_MINIMIZE); }
    void Restore() override { ShowWindow(m_hwnd, SW_RESTORE); }
    bool IsFullscreen() const override { return m_full; }
    void SetFullscreen(bool on) override {
        if (on == m_full) return;
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
    int m_minW = 0, m_minH = 0;
    bool m_full = false;
    LONG m_style = 0;
    RECT m_rect = {0, 0, 0, 0};
    wchar_t m_highSurrogate = 0;
    std::vector<uint8_t> m_present;   // PresentPixels: the frame as BGRX
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
    ~WinBackend() override { if (g_backend == this) g_backend = nullptr; if (m_xinput) FreeLibrary(m_xinput); }
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
        RECT r = {0, 0, d.width, d.height};
        AdjustWindowRectEx(&r, style, FALSE, 0);
        int x = d.x, y = d.y;
        const int ww = r.right - r.left, wh = r.bottom - r.top;
        if (x == kCentered) x = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
        if (y == kCentered) y = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
        HWND h = CreateWindowExW(0, L"OkaySpacePlatformWindow", Wide(d.title).c_str(), style, x, y, ww, wh,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!h) { m_err = "CreateWindowEx failed"; return nullptr; }
        DragAcceptFiles(h, TRUE);
        auto* w = new WinWindow(this, h);
        m_wins[h] = w;
        if (!d.hidden) ShowWindow(h, d.maximized && d.resizable && !d.fullscreen ? SW_SHOWMAXIMIZED : SW_SHOW);
        if (d.fullscreen) w->SetFullscreen(true);
        return std::unique_ptr<Window>(w);
    }
    void Unregister(HWND h) { m_wins.erase(h); }

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
    void ShowCursor(bool show) override { if (show != m_cursorShown) { m_cursorShown = show; ::ShowCursor(show ? TRUE : FALSE); } }
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

    // Audio: not written for Windows yet.
    std::unique_ptr<AudioDevice> OpenAudio(const AudioSpec&, bool, const char*, AudioSpec*) override {
        m_err = "the own Win32 backend has no audio device yet"; return nullptr;
    }
    int AudioDeviceCount(bool) override { return 0; }
    std::string AudioDeviceName(int, bool) override { return std::string(); }

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
        case WM_MOUSEMOVE: {
            e.type = EventType::MouseMove; e.x = (short)LOWORD(lp); e.y = (short)HIWORD(lp);
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
                wchar_t path[MAX_PATH * 2];
                const UINT len = DragQueryFileW(drop, i, path, (UINT)(sizeof(path) / sizeof(path[0])));
                Event d; d.type = EventType::DropFile; d.windowId = e.windowId; d.timestampMs = e.timestampMs; d.text = Utf8(path, (int)len);
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
    int m_lastX = 0, m_lastY = 0;
    int m_relAccX = 0, m_relAccY = 0;
    std::string m_err;

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
    GetStateFn m_getState = nullptr;
    SetStateFn m_setState = nullptr;
};

WinWindow::~WinWindow() {
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
