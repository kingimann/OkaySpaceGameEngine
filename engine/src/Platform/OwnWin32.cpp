// okay::platform backend "own" for Windows: Win32 windows and input, WGL for OpenGL, the Win32
// clipboard, QueryPerformanceCounter timers, XInput controllers and waveOut / waveIn audio.
//
// STATUS: NOT YET RUN BY ANYONE ON REAL WINDOWS. Written from Microsoft's public documentation of
// each call and message (see docs/platform_layer.md, "Windows backend review"). The logic that does
// not need Windows (key code tables, UTF-8 / UTF-16, clipboard line ends, the event queue, tick, DPI
// and click maths, XInput mapping and hot-plug, rumble timing, the audio byte queue and buffer plan)
// lives in okay/Platform/Win32Logic.hpp and Win32AudioLogic.hpp and is unit-tested on Linux and, in
// CI, on a real Windows machine. What this file does with the real API is checked by
// OkayPlatformSelfTest.exe (platform_selftest/), which the Windows CI job runs and the owner can run.
//
// Threading rule (CLAUDE.md 21a): no standard-library threads or locks (MinGW's win32 thread model cannot
// compile them); the audio device uses CreateThread, CRITICAL_SECTION and events.
#ifdef _WIN32
#include "Internal.hpp"
#include "okay/Platform/Win32AudioLogic.hpp"
#include "okay/Platform/Win32Logic.hpp"
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
#include <map>

namespace okay::platform {
namespace {

static_assert(sizeof(wchar_t) == 2, "the Windows logic converts through UTF-16 code units");
static_assert(wl::xinput::kMaxPads == XUSER_MAX_COUNT, "XInput slot count");
static_assert(wl::xinput::kDpadUp == XINPUT_GAMEPAD_DPAD_UP && wl::xinput::kDpadDown == XINPUT_GAMEPAD_DPAD_DOWN &&
              wl::xinput::kDpadLeft == XINPUT_GAMEPAD_DPAD_LEFT && wl::xinput::kDpadRight == XINPUT_GAMEPAD_DPAD_RIGHT &&
              wl::xinput::kStart == XINPUT_GAMEPAD_START && wl::xinput::kBack == XINPUT_GAMEPAD_BACK &&
              wl::xinput::kLeftThumb == XINPUT_GAMEPAD_LEFT_THUMB && wl::xinput::kRightThumb == XINPUT_GAMEPAD_RIGHT_THUMB &&
              wl::xinput::kLeftShoulder == XINPUT_GAMEPAD_LEFT_SHOULDER && wl::xinput::kRightShoulder == XINPUT_GAMEPAD_RIGHT_SHOULDER &&
              wl::xinput::kA == XINPUT_GAMEPAD_A && wl::xinput::kB == XINPUT_GAMEPAD_B &&
              wl::xinput::kX == XINPUT_GAMEPAD_X && wl::xinput::kY == XINPUT_GAMEPAD_Y,
              "XINPUT_GAMEPAD button masks");
static_assert(wl::kWaveFormatPcm == WAVE_FORMAT_PCM, "WAVE_FORMAT_PCM");

// ---- GL pieces we need without GL headers' extension loader ----
constexpr int kWglContextMajor = 0x2091, kWglContextMinor = 0x2092, kWglProfileMask = 0x9126;
constexpr int kWglProfileCore = 0x1, kWglProfileCompat = 0x2;

std::string Utf8(const wchar_t* w, int n) {
    if (n <= 0) return std::string();
    return wl::Utf16ToUtf8(reinterpret_cast<const char16_t*>(w), (size_t)n);
}
std::wstring Wide(const std::string& s) {
    const std::u16string u = wl::Utf8ToUtf16(s);
    return std::wstring(reinterpret_cast<const wchar_t*>(u.data()), u.size());
}

class WinBackend;
WinBackend* g_backend = nullptr;   // the window procedure has no user pointer before WM_NCCREATE

// GetDpiForWindow exists from Windows 10 1607; look it up so older systems still start.
UINT DpiOfWindow(HWND h) {
    using Fn = UINT (WINAPI*)(HWND);
    static Fn f = [] { HMODULE u = GetModuleHandleW(L"user32.dll"); return u ? (Fn)(void*)GetProcAddress(u, "GetDpiForWindow") : (Fn) nullptr; }();
    if (f) { const UINT d = f(h); if (d) return d; }
    HDC dc = GetDC(nullptr);
    const int d = dc ? GetDeviceCaps(dc, LOGPIXELSX) : (int)wl::kBaseDpi;
    if (dc) ReleaseDC(nullptr, dc);
    return d > 0 ? (UINT)d : wl::kBaseDpi;
}

RECT WorkArea() {
    RECT r = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0);
    return r;
}

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
            const RECT wa = WorkArea();
            int cx = 0, cy = 0;
            wl::CenteredOrigin(wa.left, wa.top, wa.right - wa.left, wa.bottom - wa.top, r.right - r.left, r.bottom - r.top, cx, cy);
            if (x == kCentered) x = cx;
            if (y == kCentered) y = cy;
        }
        SetWindowPos(m_hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    void SetMinimumSize(int w, int h) override { m_minW = w; m_minH = h; }
    void Show() override { ShowWindow(m_hwnd, SW_SHOW); }
    void Hide() override { ShowWindow(m_hwnd, SW_HIDE); }
    void Raise() override {
        if (IsIconic(m_hwnd)) ShowWindow(m_hwnd, SW_RESTORE);
        SetForegroundWindow(m_hwnd);   // Windows may refuse this for a background process: documented
        SetFocus(m_hwnd);
    }
    void Minimize() override { ShowWindow(m_hwnd, SW_MINIMIZE); }
    void Restore() override { ShowWindow(m_hwnd, SW_RESTORE); }
    bool IsFullscreen() const override { return m_full; }
    void SetFullscreen(bool on) override {
        if (on == m_full) return;
        if (on) {
            // Remember the style and the restored (not maximised) placement, then cover the monitor with a popup.
            m_style = GetWindowLongW(m_hwnd, GWL_STYLE);
            m_placement.length = sizeof(m_placement);
            GetWindowPlacement(m_hwnd, &m_placement);
            MONITORINFO mi; std::memset(&mi, 0, sizeof(mi)); mi.cbSize = sizeof(mi);
            GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
            SetWindowLongW(m_hwnd, GWL_STYLE, (LONG)((m_style & ~(WS_OVERLAPPEDWINDOW)) | WS_POPUP));
            SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        } else {
            SetWindowLongW(m_hwnd, GWL_STYLE, m_style);
            SetWindowPlacement(m_hwnd, &m_placement);
            SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        m_full = on;
    }
    bool IsFocused() const override { return GetFocus() == m_hwnd; }
    float ContentScale() const override { return wl::ScaleFromDpi(DpiOfWindow(m_hwnd)); }
    int RefreshRateHz() const override {
        MONITORINFOEXW mi; std::memset(&mi, 0, sizeof(mi)); mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), reinterpret_cast<MONITORINFO*>(&mi))) return 0;
        DEVMODEW dm; std::memset(&dm, 0, sizeof(dm)); dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)) return 0;
        return dm.dmDisplayFrequency > 1 ? (int)dm.dmDisplayFrequency : 0;   // 0 and 1 mean "hardware default"
    }
    bool SetIcon(const uint8_t* rgba, int w, int h) override {
        if (!rgba || w <= 0 || h <= 0 || w > 256 || h > 256) return false;
        // A 32-bit top-down BGRA colour bitmap plus an all-zero AND mask: the alpha channel does the cutting.
        BITMAPINFO bi; ZeroMemory(&bi, sizeof(bi));
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HDC dc = GetDC(nullptr);
        HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, dc);
        if (!color || !bits) { if (color) DeleteObject(color); return false; }
        uint8_t* d = static_cast<uint8_t*>(bits);
        for (int i = 0; i < w * h; ++i) { d[i * 4 + 0] = rgba[i * 4 + 2]; d[i * 4 + 1] = rgba[i * 4 + 1]; d[i * 4 + 2] = rgba[i * 4 + 0]; d[i * 4 + 3] = rgba[i * 4 + 3]; }
        const int maskStride = ((w + 15) / 16) * 2;   // 1 bit per pixel, rows padded to 16 bits
        std::vector<uint8_t> maskBits((size_t)maskStride * (size_t)h, 0);
        HBITMAP mask = CreateBitmap(w, h, 1, 1, maskBits.data());
        ICONINFO ii; ZeroMemory(&ii, sizeof(ii));
        ii.fIcon = TRUE; ii.hbmColor = color; ii.hbmMask = mask;
        HICON icon = mask ? CreateIconIndirect(&ii) : nullptr;
        DeleteObject(color);
        if (mask) DeleteObject(mask);
        if (!icon) return false;
        SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
        SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
        if (m_icon) DestroyIcon(m_icon);
        m_icon = icon;
        return true;
    }
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
    HICON m_icon = nullptr;
    int m_minW = 0, m_minH = 0;
    bool m_full = false;
    LONG m_style = 0;
    WINDOWPLACEMENT m_placement = {};
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

// ---- audio: waveOut / waveIn ---------------------------------------------------------------------
std::string MmError(MMRESULT r, bool capture) {
    wchar_t buf[MAXERRORLENGTH] = {};
    const MMRESULT ok = capture ? waveInGetErrorTextW(r, buf, MAXERRORLENGTH) : waveOutGetErrorTextW(r, buf, MAXERRORLENGTH);
    return ok == MMSYSERR_NOERROR ? Utf8(buf, (int)wcslen(buf)) : "multimedia error " + std::to_string((unsigned)r);
}

std::vector<std::string> WaveDevices(bool capture) {
    std::vector<std::string> v;
    const UINT n = capture ? waveInGetNumDevs() : waveOutGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        wchar_t name[MAXPNAMELEN + 1] = {};
        if (capture) { WAVEINCAPSW c; if (waveInGetDevCapsW(i, &c, sizeof(c)) == MMSYSERR_NOERROR) { std::memcpy(name, c.szPname, sizeof(c.szPname)); } }
        else { WAVEOUTCAPSW c; if (waveOutGetDevCapsW(i, &c, sizeof(c)) == MMSYSERR_NOERROR) { std::memcpy(name, c.szPname, sizeof(c.szPname)); } }
        v.push_back(Utf8(name, (int)wcslen(name)));
    }
    return v;
}

// One device = one worker thread that keeps `count` buffers cycling through waveOutWrite (playback) or
// waveInAddBuffer (capture). The queue is shared with the caller under a CRITICAL_SECTION.
class WinAudio final : public AudioDevice {
public:
    WinAudio(const AudioSpec& spec, bool capture) : m_spec(spec), m_capture(capture), m_fifo(capture ? (size_t)1 << 20 : 0) {}
    ~WinAudio() override { Close(); }

    bool Open(int deviceId, std::string& err) {
        InitializeCriticalSection(&m_cs);
        m_csReady = true;
        m_plan = wl::PlanBuffers(m_spec);
        m_spec.frames = m_plan.frames;
        const wl::WaveFormatFields f = wl::WaveFormatFor(m_spec);
        WAVEFORMATEX fx; ZeroMemory(&fx, sizeof(fx));
        fx.wFormatTag = f.tag; fx.nChannels = f.channels; fx.nSamplesPerSec = f.samplesPerSec;
        fx.nAvgBytesPerSec = f.avgBytesPerSec; fx.nBlockAlign = f.blockAlign; fx.wBitsPerSample = f.bitsPerSample; fx.cbSize = 0;
        m_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!m_event) { err = "CreateEvent failed"; return false; }
        const UINT id = deviceId < 0 ? (UINT)WAVE_MAPPER : (UINT)deviceId;
        MMRESULT r = m_capture ? waveInOpen(&m_in, id, &fx, (DWORD_PTR)m_event, 0, CALLBACK_EVENT)
                               : waveOutOpen(&m_out, id, &fx, (DWORD_PTR)m_event, 0, CALLBACK_EVENT);
        if (r != MMSYSERR_NOERROR) { err = std::string(m_capture ? "waveInOpen: " : "waveOutOpen: ") + MmError(r, m_capture); return false; }
        m_data.assign((size_t)m_plan.count, std::vector<uint8_t>((size_t)m_plan.bytes, 0));
        m_hdr.assign((size_t)m_plan.count, WAVEHDR());
        m_submitted.assign((size_t)m_plan.count, 0);
        for (int i = 0; i < m_plan.count; ++i) {
            WAVEHDR& h = m_hdr[(size_t)i];
            ZeroMemory(&h, sizeof(h));
            h.lpData = reinterpret_cast<LPSTR>(m_data[(size_t)i].data());
            h.dwBufferLength = (DWORD)m_plan.bytes;
            r = m_capture ? waveInPrepareHeader(m_in, &h, sizeof(h)) : waveOutPrepareHeader(m_out, &h, sizeof(h));
            if (r != MMSYSERR_NOERROR) { err = "prepare header: " + MmError(r, m_capture); return false; }
            m_prepared = i + 1;
            if (m_capture) {
                if (waveInAddBuffer(m_in, &h, sizeof(h)) != MMSYSERR_NOERROR) { err = "waveInAddBuffer failed"; return false; }
                m_submitted[(size_t)i] = 1;
            }
        }
        if (!m_capture) waveOutPause(m_out);   // devices start paused
        m_thread = CreateThread(nullptr, 0, &WinAudio::ThreadMain, this, 0, nullptr);
        if (!m_thread) { err = "CreateThread failed"; return false; }
        return true;
    }

    const AudioSpec& Spec() const override { return m_spec; }
    void Pause(bool p) override {
        if (p == m_paused) return;
        m_paused = p;
        if (m_capture) { if (p) waveInStop(m_in); else waveInStart(m_in); }
        else { if (p) waveOutPause(m_out); else waveOutRestart(m_out); }
        SetEvent(m_event);
    }
    bool Queue(const void* d, uint32_t n) override {
        if (m_capture || m_spec.callback) return false;
        EnterCriticalSection(&m_cs);
        m_fifo.Push(d, n);
        LeaveCriticalSection(&m_cs);
        return true;
    }
    uint32_t QueuedBytes() const override {
        EnterCriticalSection(&m_cs);
        const uint32_t n = (uint32_t)m_fifo.Size();
        LeaveCriticalSection(&m_cs);
        return n;
    }
    void ClearQueued() override { EnterCriticalSection(&m_cs); m_fifo.Clear(); LeaveCriticalSection(&m_cs); }
    uint32_t Dequeue(void* out, uint32_t max) override {
        EnterCriticalSection(&m_cs);
        const uint32_t n = (uint32_t)m_fifo.Pop(out, max);
        LeaveCriticalSection(&m_cs);
        return n;
    }

private:
    static DWORD WINAPI ThreadMain(LPVOID p) { static_cast<WinAudio*>(p)->Loop(); return 0; }
    void Loop() {
        while (!m_stop) {
            WaitForSingleObject(m_event, 20);   // wake on a finished buffer; the timeout covers a missed signal
            if (m_stop) break;
            if (m_capture) PumpCapture(); else PumpPlayback();
        }
    }
    void PumpPlayback() {
        if (m_paused) return;
        for (int i = 0; i < m_plan.count; ++i) {
            WAVEHDR& h = m_hdr[(size_t)i];
            if (m_submitted[(size_t)i] && !(h.dwFlags & WHDR_DONE)) continue;   // still playing
            uint8_t* buf = m_data[(size_t)i].data();
            if (m_spec.callback) {
                std::memset(buf, 0, (size_t)m_plan.bytes);
                m_spec.callback(m_spec.user, buf, m_plan.bytes);
            } else {
                EnterCriticalSection(&m_cs);
                wl::FillFromFifo(m_fifo, buf, (size_t)m_plan.bytes);   // an underrun plays silence
                LeaveCriticalSection(&m_cs);
            }
            h.dwBufferLength = (DWORD)m_plan.bytes;
            m_submitted[(size_t)i] = waveOutWrite(m_out, &h, sizeof(h)) == MMSYSERR_NOERROR ? 1 : 0;
        }
    }
    void PumpCapture() {
        for (int i = 0; i < m_plan.count; ++i) {
            WAVEHDR& h = m_hdr[(size_t)i];
            if (!m_submitted[(size_t)i] || !(h.dwFlags & WHDR_DONE)) continue;
            const int got = (int)h.dwBytesRecorded;
            if (got > 0 && !m_paused) {
                if (m_spec.callback) m_spec.callback(m_spec.user, m_data[(size_t)i].data(), got);
                else { EnterCriticalSection(&m_cs); m_fifo.Push(m_data[(size_t)i].data(), (size_t)got); LeaveCriticalSection(&m_cs); }
            }
            h.dwBytesRecorded = 0;
            m_submitted[(size_t)i] = waveInAddBuffer(m_in, &h, sizeof(h)) == MMSYSERR_NOERROR ? 1 : 0;
        }
    }
    void Close() {
        m_stop = true;
        if (m_event) SetEvent(m_event);
        if (m_thread) {
            if (WaitForSingleObject(m_thread, 5000) != WAIT_OBJECT_0) return;   // a callback that never returns: leak rather than free what it uses
            CloseHandle(m_thread);
            m_thread = nullptr;
        }
        if (m_out) waveOutReset(m_out);
        if (m_in) waveInReset(m_in);
        for (int i = 0; i < m_prepared; ++i) {
            if (m_out) waveOutUnprepareHeader(m_out, &m_hdr[(size_t)i], sizeof(WAVEHDR));
            if (m_in) waveInUnprepareHeader(m_in, &m_hdr[(size_t)i], sizeof(WAVEHDR));
        }
        m_prepared = 0;
        if (m_out) { waveOutClose(m_out); m_out = nullptr; }
        if (m_in) { waveInClose(m_in); m_in = nullptr; }
        if (m_event) { CloseHandle(m_event); m_event = nullptr; }
        if (m_csReady) { DeleteCriticalSection(&m_cs); m_csReady = false; }
    }

    AudioSpec m_spec;
    bool m_capture;
    wl::BufferPlan m_plan;
    HWAVEOUT m_out = nullptr;
    HWAVEIN m_in = nullptr;
    HANDLE m_event = nullptr, m_thread = nullptr;
    mutable CRITICAL_SECTION m_cs;
    bool m_csReady = false;
    volatile bool m_stop = false, m_paused = true;
    int m_prepared = 0;
    wl::ByteFifo m_fifo;
    std::vector<std::vector<uint8_t>> m_data;
    std::vector<WAVEHDR> m_hdr;
    std::vector<char> m_submitted;
};

class WinBackend final : public Backend {
public:
    WinBackend() : m_events(8192), m_pads(), m_padGate(1000) {
        g_backend = this; QueryPerformanceFrequency(&m_freq); QueryPerformanceCounter(&m_t0);
    }
    ~WinBackend() override { Shutdown(); if (g_backend == this) g_backend = nullptr; if (m_xinput) FreeLibrary(m_xinput); }
    BackendKind Kind() const override { return BackendKind::Own; }

    bool Init(const InitFlags& flags) override {
        if (!m_inited) {
            m_inited = true;
            m_wantPads = flags.gamepad;
            // Sleep() rounds up to the system timer tick (about 15.6 ms) unless asked for 1 ms.
            if (timeBeginPeriod(1) == TIMERR_NOERROR) m_timerPeriod = true;
            const UINT dc = GetDoubleClickTime();
            m_clicks.Configure(dc ? dc : 500, GetSystemMetrics(SM_CXDOUBLECLK), GetSystemMetrics(SM_CYDOUBLECLK));
        }
        return !flags.video || EnsureVideo();
    }
    void Shutdown() override {
        if (m_inited && m_wantPads && m_xinput) for (int s = 0; s < wl::xinput::kMaxPads; ++s) XVibrate(s, 0, 0);
        m_events.Clear();
        if (m_clipOwner) { DestroyWindow(m_clipOwner); m_clipOwner = nullptr; }
        if (m_timerPeriod) { timeEndPeriod(1); m_timerPeriod = false; }
        m_inited = false;
        m_video = false;
    }
    std::string LastError() const override { return m_err; }

    uint64_t TicksMs() override { LARGE_INTEGER n; QueryPerformanceCounter(&n); return wl::TicksToMs((uint64_t)(n.QuadPart - m_t0.QuadPart), (uint64_t)m_freq.QuadPart); }
    uint64_t PerfCounter() override { LARGE_INTEGER n; QueryPerformanceCounter(&n); return (uint64_t)n.QuadPart; }
    uint64_t PerfFrequency() override { return (uint64_t)m_freq.QuadPart; }
    void DelayMs(uint32_t ms) override { Sleep(ms); }

    std::unique_ptr<Window> MakeWindow(const WindowDesc& d) override {
        if (!m_inited && !Init(InitFlags())) return nullptr;
        if (!EnsureVideo()) return nullptr;
        if (d.width < 1 || d.height < 1 || d.width > kMaxWindowExtent || d.height > kMaxWindowExtent) { m_err = "window size out of range"; return nullptr; }
        DWORD style = d.borderless ? WS_POPUP : (d.resizable ? WS_OVERLAPPEDWINDOW : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX));
        RECT r = {0, 0, d.width, d.height};
        AdjustWindowRectEx(&r, style, FALSE, 0);
        const int ww = r.right - r.left, wh = r.bottom - r.top;
        int x = d.x, y = d.y;
        if (x == kCentered || y == kCentered) {
            const RECT wa = WorkArea();
            int cx = 0, cy = 0;
            wl::CenteredOrigin(wa.left, wa.top, wa.right - wa.left, wa.bottom - wa.top, ww, wh, cx, cy);
            if (x == kCentered) x = cx;
            if (y == kCentered) y = cy;
        }
        HWND h = CreateWindowExW(0, L"OkaySpacePlatformWindow", Wide(d.title).c_str(), style, x, y, ww, wh,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!h) { m_err = "CreateWindowEx failed (error " + std::to_string((unsigned)GetLastError()) + ")"; return nullptr; }
        DragAcceptFiles(h, TRUE);
        auto* w = new WinWindow(this, h);
        m_wins[h] = w;
        if (!d.hidden) ShowWindow(h, d.maximized && d.resizable && !d.fullscreen ? SW_SHOWMAXIMIZED : SW_SHOW);
        if (d.fullscreen) w->SetFullscreen(true);
        return std::unique_ptr<Window>(w);
    }
    void Unregister(HWND h) {
        m_wins.erase(h);
        if (m_focus == h) m_focus = nullptr;
    }

    bool PollEvent(Event& out) override {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { Event e; e.type = EventType::Quit; m_events.Push(e); continue; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (m_wantPads) {
            const uint64_t now = TicksMs();
            if (m_rescanPads || m_padGate.Due(now)) { m_rescanPads = false; ScanPads(); }
            if (const unsigned stop = m_rumble.Expired(now)) for (int s = 0; s < wl::xinput::kMaxPads; ++s) if ((stop >> s) & 1u) XVibrate(s, 0, 0);
        }
        return m_events.Pop(out);
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

    void StartTextInput() override { m_text = true; m_chars.Reset(); }
    void StopTextInput() override { m_text = false; m_chars.Reset(); }
    // Keys are tracked from the window's own key messages, so a key reads as down only while one of our windows
    // has the keyboard focus, and focus loss releases everything (no key stays stuck when the user alt-tabs).
    bool IsKeyDown(Key k) override { return m_keys.IsDown(k); }
    uint32_t ModState() override {
        uint32_t m = Backend::ModState();
        if (GetKeyState(VK_CAPITAL) & 1) m |= ModCaps;
        if (GetKeyState(VK_NUMLOCK) & 1) m |= ModNum;
        return m;
    }
    Key KeyForChar(char c) override {
        // The key that types `c` on the keyboard layout of this thread: VkKeyScanEx gives the virtual key,
        // MapVirtualKeyEx the make code that key has on that layout.
        HKL hkl = GetKeyboardLayout(0);
        const SHORT vks = VkKeyScanExW((wchar_t)(unsigned char)c, hkl);
        if (vks == -1) return Key::Unknown;
        const UINT vk = (UINT)(vks & 0xFF);
        if (vk == 0) return Key::Unknown;
        const UINT scanEx = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC_EX, hkl);
        return scanEx ? wl::KeyFromScanEx(scanEx) : Key::Unknown;
    }
    uint32_t MouseState(int* x, int* y) override {
        POINT p; GetCursorPos(&p);
        HWND f = m_focus ? m_focus : GetForegroundWindow();
        if (f) ScreenToClient(f, &p);
        if (x) *x = p.x;
        if (y) *y = p.y;
        // GetAsyncKeyState reports the PHYSICAL buttons; SM_SWAPBUTTON says whether left and right are swapped.
        const bool swap = GetSystemMetrics(SM_SWAPBUTTON) != 0;
        uint32_t m = 0;
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) m |= MouseMask(swap ? MouseRight : MouseLeft);
        if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) m |= MouseMask(MouseMiddle);
        if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) m |= MouseMask(swap ? MouseLeft : MouseRight);
        if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) m |= MouseMask(MouseX1);
        if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) m |= MouseMask(MouseX2);
        return m;
    }
    // Relative mode: the cursor is hidden (WM_SETCURSOR), confined to the window (ClipCursor) and put back in the
    // middle after every move; the distance it moved is the motion. (The cursor display counter of ShowCursor is
    // never touched: it is a counter, and two callers hiding it would need two calls to show it again.)
    bool SetRelativeMouse(bool on) override {
        m_relative = on;
        if (on) { ApplyClip(); CenterCursor(FocusWindow()); }
        else ClipCursor(nullptr);
        ApplyCursor();
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
        HMODULE u = GetModuleHandleW(L"user32.dll");
        using SetCtx = BOOL (WINAPI*)(HANDLE);
        SetCtx f = u ? (SetCtx)(void*)GetProcAddress(u, "SetProcessDpiAwarenessContext") : nullptr;
        if (f && f((HANDLE)-4) != FALSE) return true;   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
        using SetOld = BOOL (WINAPI*)();
        SetOld g = u ? (SetOld)(void*)GetProcAddress(u, "SetProcessDPIAware") : nullptr;   // Vista: system DPI only
        return g && g() != FALSE;
    }
    bool ShowErrorBox(const std::string& title, const std::string& text, Window* parent) override {
        return MessageBoxW(parent ? (HWND)parent->NativeHandle() : nullptr, Wide(text).c_str(), Wide(title).c_str(),
                           MB_OK | MB_ICONERROR) != 0;
    }
    void ShowCursor(bool show) override { m_cursorShown = show; ApplyCursor(); }
    void WarpMouse(Window* w, int x, int y) override {
        HWND h = w ? (HWND)w->NativeHandle() : GetForegroundWindow();
        POINT p = {x, y};
        if (h) ClientToScreen(h, &p);
        SetCursorPos(p.x, p.y);
    }

    // The clipboard needs an owner window: with a null owner OpenClipboard succeeds but EmptyClipboard then makes
    // the owner null and SetClipboardData fails (documented). A hidden message-only window is the owner.
    bool SetClipboardText(const std::string& t) override {
        if (!EnsureVideo() || !OpenClipboardRetry()) return false;
        EmptyClipboard();
        const std::u16string w = wl::Utf8ToUtf16(wl::ToClipboardNewlines(t));
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(char16_t));
        if (!g) { CloseClipboard(); return false; }
        void* p = GlobalLock(g);
        if (!p) { GlobalFree(g); CloseClipboard(); return false; }
        std::memcpy(p, w.c_str(), (w.size() + 1) * sizeof(char16_t));
        GlobalUnlock(g);
        const bool ok = SetClipboardData(CF_UNICODETEXT, g) != nullptr;
        if (!ok) GlobalFree(g);   // on success the system owns the memory
        CloseClipboard();
        return ok;
    }
    std::string GetClipboardText() override {
        std::string out;
        if (!EnsureVideo() || !IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboardRetry()) return out;
        if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
            const SIZE_T bytes = GlobalSize(h);
            if (const char16_t* w = (const char16_t*)GlobalLock(h)) {
                out = wl::FromClipboardNewlines(wl::Utf16ToUtf8(w, wl::BoundedLength16(w, bytes / sizeof(char16_t))));
                GlobalUnlock(h);
            }
        }
        CloseClipboard();
        return out;
    }

    // Audio: waveOut / waveIn (the multimedia API every Windows version has). Device 0 of the list is the system
    // default ("default", the wave mapper); the others are the devices by name.
    std::unique_ptr<AudioDevice> OpenAudio(const AudioSpec& want, bool capture, const char* deviceName, AudioSpec* got) override {
        AudioSpec spec;
        std::string why;
        if (!wl::NegotiateSpec(want, spec, why)) { m_err = "audio: " + why; return nullptr; }
        const int dev = wl::ResolveWaveDevice(deviceName, WaveDevices(capture));
        if (dev == -2) { m_err = std::string("audio device not found: ") + deviceName; return nullptr; }
        std::unique_ptr<WinAudio> a(new WinAudio(spec, capture));
        if (!a->Open(dev, m_err)) return nullptr;
        if (got) *got = a->Spec();
        return std::unique_ptr<AudioDevice>(a.release());
    }
    int AudioDeviceCount(bool capture) override { return 1 + (int)WaveDevices(capture).size(); }
    std::string AudioDeviceName(int index, bool capture) override {
        if (index == 0) return "default";
        const auto v = WaveDevices(capture);
        return index > 0 && (size_t)index <= v.size() ? v[(size_t)index - 1] : std::string();
    }

    // Controllers: XInput, four slots, loaded when the backend was initialised with `gamepad` or on first use.
    // Slots are looked at about once a second (XInputGetState on an empty slot is slow, as documented) and the
    // changes become GamepadAdded / GamepadRemoved events.
    int GamepadDeviceCount() override { if (!LoadXInput()) return 0; ScanPads(); return m_pads.Count(); }
    bool IsGamepad(int i) override { return LoadXInput() && (ScanPads(), m_pads.SlotOfIndex(i) >= 0); }
    std::unique_ptr<Gamepad> OpenGamepad(int i) override {
        if (!LoadXInput()) return nullptr;
        ScanPads();
        const int slot = m_pads.SlotOfIndex(i);
        return slot < 0 ? nullptr : std::unique_ptr<Gamepad>(new WinPad(this, slot, m_pads.InstanceOfSlot(slot)));
    }
    bool XState(int slot, XINPUT_STATE& s) {
        if (!LoadXInput()) return false;
        const DWORD r = m_getState((DWORD)slot, &s);
        if (r == ERROR_DEVICE_NOT_CONNECTED) m_rescanPads = true;   // noticed a pull: tell the application at the next poll
        return r == ERROR_SUCCESS;
    }
    bool XVibrate(int slot, uint16_t lo, uint16_t hi) {
        if (!LoadXInput() || !m_setState) return false;
        XINPUT_VIBRATION v; v.wLeftMotorSpeed = wl::PadMotor(lo); v.wRightMotorSpeed = wl::PadMotor(hi);
        return m_setState((DWORD)slot, &v) == ERROR_SUCCESS;
    }
    void NoteRumble(int slot, uint16_t lo, uint16_t hi, uint32_t ms) { m_rumble.Set(slot, lo, hi, ms, TicksMs()); }

    // ---- window procedure ----------------------------------------------------------------
    static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
        WinBackend* b = g_backend;
        if (!b) return DefWindowProcW(h, m, wp, lp);
        auto it = b->m_wins.find(h);
        WinWindow* win = it == b->m_wins.end() ? nullptr : it->second;
        if (!win) return DefWindowProcW(h, m, wp, lp);   // messages during creation, and the clipboard owner
        Event e;
        e.timestampMs = (uint32_t)b->TicksMs();
        e.windowId = win->Id();
        switch (m) {
        case WM_CLOSE:
            e.type = EventType::WindowClose; b->m_events.Push(e);
            if (b->m_wins.size() == 1) { Event q; q.type = EventType::Quit; q.timestampMs = e.timestampMs; b->m_events.Push(q); }
            return 0;   // the app destroys the window
        case WM_ENDSESSION:
            if (wp) { Event q; q.type = EventType::Quit; q.timestampMs = e.timestampMs; b->m_events.Push(q); }
            return 0;
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) { e.type = EventType::WindowResized; e.width = LOWORD(lp); e.height = HIWORD(lp); b->m_events.Push(e); }
            if (b->m_relative && b->FocusWindow() == h) b->ApplyClip();
            return 0;
        case WM_DPICHANGED: {
            // lParam is the rectangle Windows suggests so the window keeps its physical size on the new monitor.
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            if (r) SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_GETMINMAXINFO:
            if (win->m_minW || win->m_minH) {
                RECT r = {0, 0, win->m_minW, win->m_minH};
                AdjustWindowRectEx(&r, (DWORD)GetWindowLongW(h, GWL_STYLE), FALSE, (DWORD)GetWindowLongW(h, GWL_EXSTYLE));
                MINMAXINFO* mi = (MINMAXINFO*)lp;
                mi->ptMinTrackSize.x = r.right - r.left; mi->ptMinTrackSize.y = r.bottom - r.top;
                return 0;
            }
            break;
        case WM_SETFOCUS:
            b->m_focus = h;
            if (b->m_relative) { b->ApplyClip(); b->CenterCursor(h); }
            e.type = EventType::WindowFocusGained; b->m_events.Push(e);
            return 0;
        case WM_KILLFOCUS:
            if (b->m_focus == h) b->m_focus = nullptr;
            b->ReleaseHeldInput(e);
            if (b->m_relative) ClipCursor(nullptr);
            e.type = EventType::WindowFocusLost; b->m_events.Push(e);
            return 0;
        case WM_ERASEBKGND: return 1;   // every frame is drawn whole (GL or PresentPixels): no flicker from a background fill
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); e.type = EventType::WindowExposed; b->m_events.Push(e); return 0; }
        case WM_KEYDOWN: case WM_SYSKEYDOWN: case WM_KEYUP: case WM_SYSKEYUP: {
            const bool down = m == WM_KEYDOWN || m == WM_SYSKEYDOWN;
            const wl::KeyLParam kl = wl::DecodeKeyLParam((std::uintptr_t)lp);
            const Key k = wl::KeyFromScan(kl.scan, kl.extended);
            if (k != Key::Unknown) {
                if (!down && k == Key::PrintScreen && !b->m_keys.IsDown(k)) {   // Print Screen only reports its release
                    Event d = e; d.type = EventType::KeyDown; d.key = k; d.mods = b->Mods(); b->m_events.Push(d);
                }
                e.type = down ? EventType::KeyDown : EventType::KeyUp;
                e.key = k;
                e.repeat = down && kl.wasDown;
                if (down) b->m_keys.Down(k); else b->m_keys.Up(k);
                e.mods = b->Mods();
                b->m_events.Push(e);
            }
            if ((m == WM_SYSKEYDOWN || m == WM_SYSKEYUP) && wl::PassSysKeyToDefault((unsigned)wp)) break;   // Alt+F4, Alt+Space
            return 0;   // never let a bare Alt, F10 or Alt+Enter start Windows' menu loop or beep
        }
        case WM_SYSCHAR: return 0;
        case WM_MENUCHAR: return MAKELRESULT(0, MNC_CLOSE);
        case WM_CHAR: {
            if (!b->m_text) return 0;
            const std::string text = b->m_chars.Push((uint16_t)wp);
            if (!text.empty()) { e.type = EventType::TextInput; e.text = text; b->m_events.Push(e); }
            return 0;
        }
        case WM_MOUSEMOVE: {
            e.type = EventType::MouseMove; e.x = (short)LOWORD(lp); e.y = (short)HIWORD(lp);
            if (b->m_relative) {
                RECT r; GetClientRect(h, &r);
                const int cx = r.right / 2, cy = r.bottom / 2;
                if (e.x == cx && e.y == cy) return 0;   // our own re-centring
                e.dx = e.x - cx; e.dy = e.y - cy;
                b->CenterCursor(h);
            } else { e.dx = e.x - b->m_lastX; e.dy = e.y - b->m_lastY; }
            b->m_lastX = e.x; b->m_lastY = e.y;
            b->m_relAccX += e.dx; b->m_relAccY += e.dy;
            b->m_events.Push(e);
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
            e.x = (short)LOWORD(lp); e.y = (short)HIWORD(lp);
            const uint32_t bit = MouseMask(e.button);
            if (down) {
                e.clicks = b->m_clicks.Press(e.button, e.x, e.y, e.timestampMs);
                b->m_pressClicks[e.button] = e.clicks;
                if (b->m_buttonsDown == 0) SetCapture(h);   // keep the drag even outside the window
                b->m_buttonsDown |= bit;
            } else {
                e.clicks = b->m_pressClicks[e.button] ? b->m_pressClicks[e.button] : 1;
                b->m_buttonsDown &= ~bit;
                if (b->m_buttonsDown == 0) ReleaseCapture();   // only when the last button is up
            }
            b->m_events.Push(e);
            return (m == WM_XBUTTONDOWN || m == WM_XBUTTONUP) ? TRUE : 0;   // documented: X-button handlers return TRUE
        }
        case WM_LBUTTONDBLCLK: return 0;
        case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
            e.type = EventType::MouseWheel;
            const float notches = (float)(short)HIWORD(wp) / (float)WHEEL_DELTA;
            if (m == WM_MOUSEWHEEL) e.wheelY = notches; else e.wheelX = notches;
            b->m_events.Push(e);
            return 0;
        }
        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            const UINT n = DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
            for (UINT i = 0; i < n; ++i) {
                const UINT len = DragQueryFileW(drop, i, nullptr, 0);   // characters, without the terminator: no fixed-size buffer
                std::vector<wchar_t> path((size_t)len + 1, L'\0');
                const UINT got = DragQueryFileW(drop, i, path.data(), len + 1);
                Event d; d.type = EventType::DropFile; d.windowId = e.windowId; d.timestampMs = e.timestampMs; d.text = Utf8(path.data(), (int)got);
                b->m_events.Push(d);
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

    HWND FocusWindow() const { return m_focus ? m_focus : GetForegroundWindow(); }
    void CenterCursor(HWND f) {
        if (!f) return;
        RECT r; GetClientRect(f, &r);
        POINT p = {r.right / 2, r.bottom / 2};
        ClientToScreen(f, &p);
        SetCursorPos(p.x, p.y);
    }
    void ApplyClip() {
        HWND f = FocusWindow();
        if (!f) return;
        RECT r; GetClientRect(f, &r);
        POINT a = {r.left, r.top}, c = {r.right, r.bottom};
        ClientToScreen(f, &a); ClientToScreen(f, &c);
        RECT s = {a.x, a.y, c.x, c.y};
        ClipCursor(&s);
    }
    // Takes effect at once for a cursor that is already over our window; later moves go through WM_SETCURSOR.
    void ApplyCursor() {
        if (m_focus) SetCursor((m_cursorShown && !m_relative) ? LoadCursorA(nullptr, IDC_ARROW) : nullptr);
    }
    uint32_t Mods() {
        uint32_t m = 0;
        if (m_keys.IsDown(Key::LShift) || m_keys.IsDown(Key::RShift)) m |= ModShift;
        if (m_keys.IsDown(Key::LCtrl) || m_keys.IsDown(Key::RCtrl)) m |= ModCtrl;
        if (m_keys.IsDown(Key::LAlt) || m_keys.IsDown(Key::RAlt)) m |= ModAlt;
        if (m_keys.IsDown(Key::LGui) || m_keys.IsDown(Key::RGui)) m |= ModGui;
        if (GetKeyState(VK_CAPITAL) & 1) m |= ModCaps;
        if (GetKeyState(VK_NUMLOCK) & 1) m |= ModNum;
        return m;
    }
    // Focus lost: tell the application every key and button that is still held went up, as the user can no
    // longer release them to us.
    void ReleaseHeldInput(const Event& base) {
        for (Key k : m_keys.ReleaseAll()) { Event u = base; u.type = EventType::KeyUp; u.key = k; u.mods = Mods(); m_events.Push(u); }
        for (int btn = MouseLeft; btn <= MouseX2; ++btn)
            if (m_buttonsDown & MouseMask(btn)) { Event u = base; u.type = EventType::MouseButtonUp; u.button = btn; u.clicks = 1; m_events.Push(u); }
        if (m_buttonsDown) ReleaseCapture();
        m_buttonsDown = 0;
        m_chars.Reset();
    }

    std::map<HWND, WinWindow*> m_wins;
    wl::EventQueue m_events;
    wl::KeyState m_keys;
    wl::ClickTracker m_clicks;
    wl::CharAssembler m_chars;
    HWND m_focus = nullptr;
    uint32_t m_buttonsDown = 0;
    int m_pressClicks[8] = {};
    bool m_text = false, m_relative = false, m_cursorShown = true, m_inited = false;
    int m_lastX = 0, m_lastY = 0;
    int m_relAccX = 0, m_relAccY = 0;
    std::string m_err;

private:
    bool EnsureVideo() {
        if (m_video) return true;
        WNDCLASSEXW wc; std::memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &WinBackend::WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.lpszClassName = L"OkaySpacePlatformWindow";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { m_err = "RegisterClassEx failed"; return false; }
        m_clipOwner = CreateWindowExW(0, L"OkaySpacePlatformWindow", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!m_clipOwner) { m_err = "could not create the clipboard owner window"; return false; }
        m_video = true;
        return true;
    }
    bool OpenClipboardRetry() {
        // Another program can hold the clipboard for a moment; the documentation advises trying again.
        for (int i = 0; i < 5; ++i) {
            if (OpenClipboard(m_clipOwner)) return true;
            Sleep(5);
        }
        return false;
    }
    bool LoadXInput() {
        if (m_getState) return true;
        if (!m_xinput) m_xinput = LoadLibraryA("xinput1_4.dll");
        if (!m_xinput) m_xinput = LoadLibraryA("xinput1_3.dll");
        if (!m_xinput) m_xinput = LoadLibraryA("xinput9_1_0.dll");
        if (!m_xinput) return false;
        m_getState = (GetStateFn)(void*)GetProcAddress(m_xinput, "XInputGetState");
        m_setState = (SetStateFn)(void*)GetProcAddress(m_xinput, "XInputSetState");
        return m_getState != nullptr;
    }
    void ScanPads() {
        if (!LoadXInput()) return;
        unsigned mask = 0;
        for (int i = 0; i < wl::xinput::kMaxPads; ++i) { XINPUT_STATE s; if (m_getState((DWORD)i, &s) == ERROR_SUCCESS) mask |= 1u << i; }
        for (const auto& c : m_pads.Update(mask)) {
            Event e;
            e.type = c.added ? EventType::GamepadAdded : EventType::GamepadRemoved;
            e.deviceId = c.added ? c.index : c.instanceId;
            e.timestampMs = (uint32_t)TicksMs();
            m_events.Push(e);
        }
    }
    typedef DWORD (WINAPI *GetStateFn)(DWORD, XINPUT_STATE*);
    typedef DWORD (WINAPI *SetStateFn)(DWORD, XINPUT_VIBRATION*);
    LARGE_INTEGER m_freq, m_t0;
    HMODULE m_xinput = nullptr;
    GetStateFn m_getState = nullptr;
    SetStateFn m_setState = nullptr;
    wl::PadSlotTracker m_pads;
    wl::IntervalGate m_padGate;
    wl::RumbleSchedule m_rumble;
    HWND m_clipOwner = nullptr;
    bool m_video = false, m_wantPads = false, m_rescanPads = false, m_timerPeriod = false;
};

WinWindow::~WinWindow() {
    DestroyGLContext();
    m_b->Unregister(m_hwnd);
    DestroyWindow(m_hwnd);
    if (m_icon) DestroyIcon(m_icon);
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

WinPad::~WinPad() { m_b->XVibrate(m_slot, 0, 0); }
int16_t WinPad::Axis(GamepadAxis a) const {
    XINPUT_STATE s; if (!m_b->XState(m_slot, s)) return 0;
    const XINPUT_GAMEPAD& g = s.Gamepad;
    switch (a) {
    case GamepadAxis::LeftX: return g.sThumbLX;
    case GamepadAxis::LeftY: return wl::PadStickY(g.sThumbLY);   // XInput is +y up, the seam is +y down
    case GamepadAxis::RightX: return g.sThumbRX;
    case GamepadAxis::RightY: return wl::PadStickY(g.sThumbRY);
    case GamepadAxis::TriggerLeft: return wl::PadTrigger(g.bLeftTrigger);
    case GamepadAxis::TriggerRight: return wl::PadTrigger(g.bRightTrigger);
    }
    return 0;
}
bool WinPad::Button(GamepadButton b) const {
    XINPUT_STATE s; if (!m_b->XState(m_slot, s)) return false;
    return wl::PadButtonDown(s.Gamepad.wButtons, b);
}
bool WinPad::Rumble(uint16_t lo, uint16_t hi, uint32_t ms) {
    const bool ok = m_b->XVibrate(m_slot, lo, hi);
    if (ok) m_b->NoteRumble(m_slot, lo, hi, ms);   // XInput never stops by itself: PollEvent turns it off when `ms` is up
    return ok;
}

} // namespace

std::unique_ptr<Backend> CreateOwnBackend() { return std::unique_ptr<Backend>(new WinBackend()); }

} // namespace okay::platform
#else
#include "Internal.hpp"
namespace okay::platform { std::unique_ptr<Backend> CreateOwnBackend() { return nullptr; } }
#endif
