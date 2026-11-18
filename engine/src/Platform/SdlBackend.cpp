// okay::platform backend over SDL2: a thin translation layer, behaviour as the
// engine always had it. Compiled only when SDL2 was found (or Emscripten's port).
#ifdef OKAY_PLATFORM_HAVE_SDL2
#include <algorithm>
#include <cstdlib>
#include <vector>
#include "Internal.hpp"
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#if defined(_WIN32)
#  include <SDL_syswm.h>   // the HWND of an SDL window (Window::Win32Handle)
#endif
#include <cstring>

namespace okay::platform {
namespace {

uint32_t ModsFromSdl(Uint16 m) {
    uint32_t r = 0;
    if (m & KMOD_SHIFT) r |= ModShift;
    if (m & KMOD_CTRL)  r |= ModCtrl;
    if (m & KMOD_ALT)   r |= ModAlt;
    if (m & KMOD_GUI)   r |= ModGui;
    if (m & KMOD_CAPS)  r |= ModCaps;
    if (m & KMOD_NUM)   r |= ModNum;
    return r;
}

SDL_AudioFormat ToSdlFormat(SampleFormat f) { return f == SampleFormat::F32 ? AUDIO_F32SYS : AUDIO_S16SYS; }
SampleFormat FromSdlFormat(SDL_AudioFormat f) { return SDL_AUDIO_ISFLOAT(f) ? SampleFormat::F32 : SampleFormat::S16; }

class SdlWindow final : public Window {
public:
    explicit SdlWindow(SDL_Window* w, bool owns = true) : m_win(w), m_owns(owns) {}
    ~SdlWindow() override {
        DestroyGLContext();
        if (m_win && m_owns) SDL_DestroyWindow(m_win);
    }
    uint32_t Id() const override { return SDL_GetWindowID(m_win); }
    BackendKind Kind() const override { return BackendKind::Sdl2; }
    void* NativeHandle() const override { return m_win; }
    void GetSize(int& w, int& h) const override { SDL_GetWindowSize(m_win, &w, &h); }
    void GetPosition(int& x, int& y) const override { SDL_GetWindowPosition(m_win, &x, &y); }
    void SetTitle(const std::string& t) override { SDL_SetWindowTitle(m_win, t.c_str()); }
    void SetSize(int w, int h) override { SDL_SetWindowSize(m_win, w, h); }
    void SetPosition(int x, int y) override {
        SDL_SetWindowPosition(m_win, x == kCentered ? (int)SDL_WINDOWPOS_CENTERED : x,
                              y == kCentered ? (int)SDL_WINDOWPOS_CENTERED : y);
    }
    void SetMinimumSize(int w, int h) override { SDL_SetWindowMinimumSize(m_win, w, h); }
    void Show() override { SDL_ShowWindow(m_win); }
    void Hide() override { SDL_HideWindow(m_win); }
    void Raise() override { SDL_RaiseWindow(m_win); }
    void Minimize() override { SDL_MinimizeWindow(m_win); }
    void Restore() override { SDL_RestoreWindow(m_win); }
    void SetFullscreen(bool on) override { SDL_SetWindowFullscreen(m_win, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); }
    bool IsFocused() const override { return (SDL_GetWindowFlags(m_win) & SDL_WINDOW_INPUT_FOCUS) != 0; }
    bool IsFullscreen() const override { return (SDL_GetWindowFlags(m_win) & SDL_WINDOW_FULLSCREEN) != 0; }
    bool HasMouseFocus() const override { return (SDL_GetWindowFlags(m_win) & SDL_WINDOW_MOUSE_FOCUS) != 0; }
    bool IsMinimized() const override { return (SDL_GetWindowFlags(m_win) & SDL_WINDOW_MINIMIZED) != 0; }
    bool IsVisible() const override { return (SDL_GetWindowFlags(m_win) & SDL_WINDOW_HIDDEN) == 0; }
    bool SetInputFocus() override { return SDL_SetWindowInputFocus(m_win) == 0; }
    void* Win32Handle() const override {
#if defined(_WIN32)
        SDL_SysWMinfo wm; SDL_VERSION(&wm.version);
        if (m_win && SDL_GetWindowWMInfo(m_win, &wm) && wm.subsystem == SDL_SYSWM_WINDOWS) return (void*)wm.info.win.window;
#endif
        return nullptr;
    }
    bool Flash(FlashMode mode) override {
#if SDL_VERSION_ATLEAST(2, 0, 16)
        SDL_FlashOperation op = mode == FlashMode::Stop ? SDL_FLASH_CANCEL
                              : mode == FlashMode::Briefly ? SDL_FLASH_BRIEFLY : SDL_FLASH_UNTIL_FOCUSED;
        return SDL_FlashWindow(m_win, op) == 0;
#else
        (void)mode; return false;
#endif
    }
    bool SetIcon(const uint8_t* rgba, int w, int h) override {
        if (!rgba || w <= 0 || h <= 0) return false;
        SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom((void*)rgba, w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888);
        if (!surf) return false;
        SDL_SetWindowIcon(m_win, surf);
        SDL_FreeSurface(surf);
        return true;
    }
    int Dpi() const override {
        float ddpi = 0;
        const int idx = SDL_GetWindowDisplayIndex(m_win);
        return (idx >= 0 && SDL_GetDisplayDPI(idx, &ddpi, nullptr, nullptr) == 0 && ddpi >= 48.0f && ddpi <= 1000.0f) ? (int)(ddpi + 0.5f) : 96;
    }
    int RefreshRateHz() const override {
        SDL_DisplayMode dm;
        return (SDL_GetWindowDisplayMode(m_win, &dm) == 0 && dm.refresh_rate > 0) ? dm.refresh_rate : 0;
    }

    bool CreateGLContext(const GLAttributes& a) override {
        DestroyGLContext();
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, a.major);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, a.minor);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            a.compatibility ? SDL_GL_CONTEXT_PROFILE_COMPATIBILITY : SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, a.depthBits);
        m_ctx = SDL_GL_CreateContext(m_win);
        if (!m_ctx) return false;
        SDL_GL_MakeCurrent(m_win, m_ctx);
        return true;
    }
    void DestroyGLContext() override {
        if (!m_ctx) return;
        if (SDL_GL_GetCurrentContext() == m_ctx) SDL_GL_MakeCurrent(nullptr, nullptr);
        SDL_GL_DeleteContext(m_ctx);
        m_ctx = nullptr;
    }
    bool MakeCurrent() override { return m_ctx && SDL_GL_MakeCurrent(m_win, m_ctx) == 0; }
    void SwapBuffers() override { SDL_GL_SwapWindow(m_win); }
    bool SetSwapInterval(int i) override { return SDL_GL_SetSwapInterval(i) == 0; }
    void GetDrawableSize(int& w, int& h) const override { SDL_GL_GetDrawableSize(m_win, &w, &h); }

private:
    SDL_Window* m_win = nullptr;
    bool m_owns = true;
    SDL_GLContext m_ctx = nullptr;
};

class SdlAudioDevice final : public AudioDevice {
public:
    SdlAudioDevice(SDL_AudioDeviceID id, const AudioSpec& spec) : m_id(id), m_spec(spec) {}
    ~SdlAudioDevice() override { if (m_id) SDL_CloseAudioDevice(m_id); }
    const AudioSpec& Spec() const override { return m_spec; }
    void Pause(bool p) override { SDL_PauseAudioDevice(m_id, p ? 1 : 0); }
    bool Queue(const void* d, uint32_t n) override { return SDL_QueueAudio(m_id, d, n) == 0; }
    uint32_t QueuedBytes() const override { return SDL_GetQueuedAudioSize(m_id); }
    void ClearQueued() override { SDL_ClearQueuedAudio(m_id); }
    uint32_t Dequeue(void* out, uint32_t max) override { return SDL_DequeueAudio(m_id, out, max); }
private:
    SDL_AudioDeviceID m_id;
    AudioSpec m_spec;
};

// Raw (unmapped) joysticks are opened too, so any controller can be used through our own
// generic mapping and Controller Setup. OKAY_GENERIC_PADS=0 turns that off.
bool GenericPadsOn() {
    static const bool on = [] { const char* e = std::getenv("OKAY_GENERIC_PADS"); return !(e && e[0] == '0'); }();
    return on;
}
std::vector<int>& RawInstances() { static std::vector<int> v; return v; }   // instance ids opened as plain joysticks

class SdlGamepad final : public Gamepad {
public:
    explicit SdlGamepad(SDL_GameController* c) : m_c(c), m_j(SDL_GameControllerGetJoystick(c)) {}
    explicit SdlGamepad(SDL_Joystick* j) : m_c(nullptr), m_j(j) {}
    ~SdlGamepad() override { if (m_c) SDL_GameControllerClose(m_c); else if (m_j) SDL_JoystickClose(m_j); }
    int InstanceId() const override { return (int)SDL_JoystickInstanceID(m_j); }
    const char* Name() const override {
        const char* n = m_c ? SDL_GameControllerName(m_c) : SDL_JoystickName(m_j);
        return n ? n : "";
    }
    int16_t Axis(GamepadAxis a) const override {
        return m_c ? SDL_GameControllerGetAxis(m_c, (SDL_GameControllerAxis)(int)a) : (int16_t)0;
    }
    bool Button(GamepadButton b) const override {
        return m_c && SDL_GameControllerGetButton(m_c, (SDL_GameControllerButton)(int)b) != 0;
    }
    bool Rumble(uint16_t lo, uint16_t hi, uint32_t ms) override {
        return m_c ? SDL_GameControllerRumble(m_c, lo, hi, ms) == 0 : SDL_JoystickRumble(m_j, lo, hi, ms) == 0;
    }
    uint16_t VendorId() const override { return SDL_JoystickGetVendor(m_j); }
    uint16_t ProductId() const override { return SDL_JoystickGetProduct(m_j); }
    int ControllerType() const override {
#if SDL_VERSION_ATLEAST(2, 0, 12)
        return m_c ? (int)SDL_GameControllerGetType(m_c) : -1;
#else
        return -1;
#endif
    }
    bool HasBuiltinMapping() const override { return m_c != nullptr; }
    int RawAxisCount() const override { const int n = SDL_JoystickNumAxes(m_j); return n < 0 ? 0 : n; }
    int RawButtonCount() const override { const int n = SDL_JoystickNumButtons(m_j); return n < 0 ? 0 : n; }
    int RawHatCount() const override { const int n = SDL_JoystickNumHats(m_j); return n < 0 ? 0 : n; }
    int16_t RawAxis(int i) const override { return SDL_JoystickGetAxis(m_j, i); }
    bool RawButton(int i) const override { return SDL_JoystickGetButton(m_j, i) != 0; }
    uint8_t RawHat(int i) const override { return SDL_JoystickGetHat(m_j, i); }   // SDL_HAT_UP/RIGHT/DOWN/LEFT = 1/2/4/8
    int PowerLevel() const override { return (int)SDL_JoystickCurrentPowerLevel(m_j); }
#if SDL_VERSION_ATLEAST(2, 0, 14)
    bool SetLightbar(uint8_t r, uint8_t g, uint8_t b) override { return m_c && SDL_GameControllerSetLED(m_c, r, g, b) == 0; }
    bool RumbleTriggers(uint16_t l, uint16_t r, uint32_t ms) override { return m_c && SDL_GameControllerRumbleTriggers(m_c, l, r, ms) == 0; }
    bool HasTriggerRumble() const override { return m_c && SDL_GameControllerHasRumbleTriggers(m_c) == SDL_TRUE; }
    int TouchpadCount() const override { return m_c ? SDL_GameControllerGetNumTouchpads(m_c) : 0; }
    bool TouchpadFinger(int pad, int finger, bool& down, float& x, float& y, float& pressure) const override {
        Uint8 st = 0; x = y = pressure = 0; down = false;
        if (!m_c || SDL_GameControllerGetTouchpadFinger(m_c, pad, finger, &st, &x, &y, &pressure) != 0) return false;
        down = st != 0; return true;
    }
    bool HasSensor(int kind) const override { return m_c && SDL_GameControllerHasSensor(m_c, kind == 0 ? SDL_SENSOR_GYRO : SDL_SENSOR_ACCEL) == SDL_TRUE; }
    bool SensorData(int kind, float out[3]) override {
        out[0] = out[1] = out[2] = 0;
        if (!HasSensor(kind)) return false;
        bool& on = m_sensorOn[kind == 0 ? 0 : 1];
        const SDL_SensorType t = kind == 0 ? SDL_SENSOR_GYRO : SDL_SENSOR_ACCEL;
        if (!on) { if (SDL_GameControllerSetSensorEnabled(m_c, t, SDL_TRUE) != 0) return false; on = true; }
        return SDL_GameControllerGetSensorData(m_c, t, out, 3) == 0;
    }
#endif
#if SDL_VERSION_ATLEAST(2, 0, 12)
    void SetPlayerIndex(int i) override { if (m_c) SDL_GameControllerSetPlayerIndex(m_c, i); }
#endif
private:
    SDL_GameController* m_c;
    SDL_Joystick* m_j;
    bool m_sensorOn[2] = {false, false};
};

class SdlBackend final : public Backend {
public:
    BackendKind Kind() const override { return BackendKind::Sdl2; }

    bool Init(const InitFlags& f) override {
        Uint32 want = SDL_INIT_TIMER | SDL_INIT_EVENTS;
        if (f.video) want |= SDL_INIT_VIDEO;
        if (f.audio) want |= SDL_INIT_AUDIO;
        if (f.gamepad) want |= SDL_INIT_GAMECONTROLLER;
        SDL_SetMainReady();   // the apps have their own main(); SDL must not insist on SDL_main
        const Uint32 missing = want & ~SDL_WasInit(0);
        if (missing && SDL_InitSubSystem(missing) != 0) return false;
        m_started |= missing;
        if (f.video) SDL_StopTextInput();   // SDL2 starts with it on; the seam's contract is off until StartTextInput
        return true;
    }
    void Shutdown() override {
        if (m_started) SDL_QuitSubSystem(m_started);
        m_started = 0;
    }
    std::string LastError() const override { return SDL_GetError(); }

    uint64_t TicksMs() override {
#if SDL_VERSION_ATLEAST(2, 0, 18)
        return SDL_GetTicks64();
#else
        return SDL_GetTicks();
#endif
    }
    uint64_t PerfCounter() override { return SDL_GetPerformanceCounter(); }
    uint64_t PerfFrequency() override { return SDL_GetPerformanceFrequency(); }
    void DelayMs(uint32_t ms) override { SDL_Delay(ms); }

    std::unique_ptr<Window> MakeWindow(const WindowDesc& d) override {
        Uint32 fl = 0;
        if (d.resizable) fl |= SDL_WINDOW_RESIZABLE;
        if (d.opengl) fl |= SDL_WINDOW_OPENGL;
        fl |= d.hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
        if (d.borderless) fl |= SDL_WINDOW_BORDERLESS;
        if (d.highDpi) fl |= SDL_WINDOW_ALLOW_HIGHDPI;
        if (d.fullscreen) fl |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        else if (d.maximized && d.resizable) fl |= SDL_WINDOW_MAXIMIZED;
        SDL_Window* w = SDL_CreateWindow(d.title.c_str(),
            d.x == kCentered ? (int)SDL_WINDOWPOS_CENTERED : d.x,
            d.y == kCentered ? (int)SDL_WINDOWPOS_CENTERED : d.y, d.width, d.height, fl);
        if (!w) return nullptr;
        return std::unique_ptr<Window>(new SdlWindow(w));
    }

    bool PollEvent(Event& out) override {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            Event r;
            if (Translate(e, r)) { out = std::move(r); return true; }
        }
        return false;
    }
    void* GLProcAddress(const char* n) override { return SDL_GL_GetProcAddress(n); }
    void ReleaseCurrent() override { SDL_GL_MakeCurrent(nullptr, nullptr); }
    GLCurrent CurrentGL() override {
        GLCurrent c;
        c.window = SDL_GL_GetCurrentWindow();
        c.context = SDL_GL_GetCurrentContext();
        return c;
    }
    bool RestoreGL(const GLCurrent& s) override {
        return SDL_GL_MakeCurrent((SDL_Window*)s.window, (SDL_GLContext)s.context) == 0;
    }
    int TouchDeviceCount() override { return SDL_GetNumTouchDevices(); }

    void StartTextInput() override { SDL_StartTextInput(); }
    void StopTextInput() override { SDL_StopTextInput(); }
    bool IsKeyDown(Key k) override {
        const Uint8* st = SDL_GetKeyboardState(nullptr);
        return st && (int)k > 0 && (int)k < SDL_NUM_SCANCODES && st[(int)k] != 0;
    }
    uint32_t KeyboardFocusWindowId() override { SDL_Window* w = SDL_GetKeyboardFocus(); return w ? SDL_GetWindowID(w) : 0; }
    uint32_t MouseFocusWindowId() override { SDL_Window* w = SDL_GetMouseFocus(); return w ? SDL_GetWindowID(w) : 0; }
    void* NativeHandleFromId(uint32_t id) override { return id ? (void*)SDL_GetWindowFromID(id) : nullptr; }
    uint32_t MouseState(int* x, int* y) override { return SDL_GetMouseState(x, y); }
    bool IsRelativeMouse() override { return SDL_GetRelativeMouseMode() == SDL_TRUE; }
    bool SetRelativeMouse(bool on) override { return SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE) == 0; }
    void ShowCursor(bool s) override { SDL_ShowCursor(s ? SDL_ENABLE : SDL_DISABLE); }
    bool IsCursorShown() override { return SDL_ShowCursor(SDL_QUERY) != SDL_DISABLE; }
    uint32_t RelativeMouseState(int* dx, int* dy) override { return SDL_GetRelativeMouseState(dx, dy); }
    bool SupportsRelativeMouseState() override { return true; }
    uint32_t ModState() override { return ModsFromSdl((Uint16)SDL_GetModState()); }
    Key KeyForChar(char c) override { return (Key)SDL_GetScancodeFromKey((SDL_Keycode)(unsigned char)c); }
    int DisplayCount() override { return SDL_GetNumVideoDisplays() > 0 ? SDL_GetNumVideoDisplays() : 0; }
    bool DisplayAt(int i, DisplayInfo& o) override {
        if (i < 0 || i >= SDL_GetNumVideoDisplays()) return false;
        SDL_Rect b, u;
        if (SDL_GetDisplayBounds(i, &b) != 0) return false;
        if (SDL_GetDisplayUsableBounds(i, &u) != 0) u = b;
        const char* nm = SDL_GetDisplayName(i);
        float ddpi = 96.0f;
        if (SDL_GetDisplayDPI(i, &ddpi, nullptr, nullptr) != 0 || ddpi < 48.0f || ddpi > 1000.0f) ddpi = 96.0f;
        SDL_DisplayMode dm;
        o.name = nm ? nm : "";
        o.x = b.x; o.y = b.y; o.w = b.w; o.h = b.h;
        o.ux = u.x; o.uy = u.y; o.uw = u.w; o.uh = u.h;
        o.dpi = ddpi;
        o.refreshHz = (SDL_GetCurrentDisplayMode(i, &dm) == 0 && dm.refresh_rate > 0) ? dm.refresh_rate : 0;
        o.primary = (i == 0);   // SDL lists the primary display first
        return true;
    }
    bool SetPerMonitorDpiAware() override {
#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS   // SDL >= 2.24
        return SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2") == SDL_TRUE;
#else
        return false;
#endif
    }
    bool ShowErrorBox(const std::string& title, const std::string& text, Window* parent) override {
        return SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(),
                                        parent ? (SDL_Window*)parent->NativeHandle() : nullptr) == 0;
    }
    std::string BasePath() override {
        char* b = SDL_GetBasePath();
        std::string r = b ? b : "";
        if (b) SDL_free(b);
        return r;
    }
    std::string PrefPath(const std::string& org, const std::string& app) override {
        char* p = SDL_GetPrefPath(org.c_str(), app.c_str());
        std::string r = p ? p : "";
        if (p) SDL_free(p);
        return r;
    }
    std::vector<std::string> PreferredLocales() override {
        std::vector<std::string> out;
#if SDL_VERSION_ATLEAST(2, 0, 14)
        if (SDL_Locale* locs = SDL_GetPreferredLocales()) {
            for (const SDL_Locale* l = locs; l->language; ++l)
                out.push_back(l->country ? std::string(l->language) + "-" + l->country : std::string(l->language));
            SDL_free(locs);
        }
#endif
        return out;
    }
    void Log(const std::string& line) override { SDL_Log("%s", line.c_str()); }
    void WarpMouse(Window* w, int x, int y) override {
        SDL_WarpMouseInWindow(w ? (SDL_Window*)w->NativeHandle() : nullptr, x, y);
    }

    bool SetClipboardText(const std::string& t) override { return SDL_SetClipboardText(t.c_str()) == 0; }
    std::string GetClipboardText() override {
        char* s = SDL_GetClipboardText();
        std::string r = s ? s : "";
        if (s) SDL_free(s);
        return r;
    }

    std::unique_ptr<AudioDevice> OpenAudio(const AudioSpec& w, bool capture, const char* name, AudioSpec* got) override {
        if (!(SDL_WasInit(0) & SDL_INIT_AUDIO)) { if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return nullptr; m_started |= SDL_INIT_AUDIO; }
        SDL_AudioSpec want{}, have{};
        want.freq = w.freq; want.format = ToSdlFormat(w.format); want.channels = (Uint8)w.channels;
        want.samples = (Uint16)w.frames; want.callback = (SDL_AudioCallback)w.callback; want.userdata = w.user;
        const int allow = w.allowChange ? (SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE) : 0;
        const SDL_AudioDeviceID id = SDL_OpenAudioDevice(name, capture ? 1 : 0, &want, &have, allow);
        if (!id) return nullptr;
        AudioSpec s = w;
        s.freq = have.freq; s.channels = have.channels; s.frames = have.samples; s.format = FromSdlFormat(have.format);
        if (got) *got = s;
        return std::unique_ptr<AudioDevice>(new SdlAudioDevice(id, s));
    }
    int AudioDeviceCount(bool capture) override {
        if (!(SDL_WasInit(0) & SDL_INIT_AUDIO)) { if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return 0; m_started |= SDL_INIT_AUDIO; }
        const int n = SDL_GetNumAudioDevices(capture ? 1 : 0);
        return n < 0 ? 0 : n;
    }
    std::string AudioDeviceName(int i, bool capture) override {
        const char* n = SDL_GetAudioDeviceName(i, capture ? 1 : 0);
        return n ? n : "";
    }

    int GamepadDeviceCount() override {
        if (!(SDL_WasInit(0) & SDL_INIT_GAMECONTROLLER)) { if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) return 0; m_started |= SDL_INIT_GAMECONTROLLER; }
        const int n = SDL_NumJoysticks();
        return n < 0 ? 0 : n;
    }
    bool IsGamepad(int i) override { return SDL_IsGameController(i) == SDL_TRUE || (GenericPadsOn() && SDL_NumJoysticks() > i && i >= 0); }
    std::unique_ptr<Gamepad> OpenGamepad(int i) override {
        if (SDL_IsGameController(i) == SDL_TRUE) {
            SDL_GameController* c = SDL_GameControllerOpen(i);
            return c ? std::unique_ptr<Gamepad>(new SdlGamepad(c)) : nullptr;
        }
        if (!GenericPadsOn()) return nullptr;
        SDL_Joystick* j = SDL_JoystickOpen(i);
        return j ? std::unique_ptr<Gamepad>(new SdlGamepad(j)) : nullptr;
    }

private:
    static bool Translate(const SDL_Event& e, Event& r) {
        r.timestampMs = e.common.timestamp;
        switch (e.type) {
        case SDL_QUIT: r.type = EventType::Quit; return true;
        case SDL_WINDOWEVENT:
            r.windowId = e.window.windowID;
            switch (e.window.event) {
            case SDL_WINDOWEVENT_CLOSE: r.type = EventType::WindowClose; return true;
            case SDL_WINDOWEVENT_SIZE_CHANGED:
                r.type = EventType::WindowResized; r.width = e.window.data1; r.height = e.window.data2; return true;
            case SDL_WINDOWEVENT_FOCUS_GAINED: r.type = EventType::WindowFocusGained; return true;
            case SDL_WINDOWEVENT_FOCUS_LOST:   r.type = EventType::WindowFocusLost; return true;
            case SDL_WINDOWEVENT_EXPOSED:      r.type = EventType::WindowExposed; return true;
            default: return false;
            }
        case SDL_KEYDOWN: case SDL_KEYUP:
            r.type = e.type == SDL_KEYDOWN ? EventType::KeyDown : EventType::KeyUp;
            r.windowId = e.key.windowID; r.key = (Key)e.key.keysym.scancode;
            r.mods = ModsFromSdl(e.key.keysym.mod); r.repeat = e.key.repeat != 0; return true;
        case SDL_TEXTINPUT:
            r.type = EventType::TextInput; r.windowId = e.text.windowID; r.text = e.text.text; return true;
        case SDL_MOUSEMOTION:
            r.type = EventType::MouseMove; r.windowId = e.motion.windowID;
            r.x = e.motion.x; r.y = e.motion.y; r.dx = e.motion.xrel; r.dy = e.motion.yrel; return true;
        case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
            r.type = e.type == SDL_MOUSEBUTTONDOWN ? EventType::MouseButtonDown : EventType::MouseButtonUp;
            r.windowId = e.button.windowID; r.button = e.button.button; r.clicks = e.button.clicks;
            r.x = e.button.x; r.y = e.button.y; return true;
        case SDL_MOUSEWHEEL:
            r.type = EventType::MouseWheel; r.windowId = e.wheel.windowID;
            r.wheelX = (float)e.wheel.x; r.wheelY = (float)e.wheel.y;
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) { r.wheelX = -r.wheelX; r.wheelY = -r.wheelY; r.wheelFlipped = true; }
            return true;
        case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP:
            r.type = e.type == SDL_FINGERDOWN ? EventType::FingerDown
                   : e.type == SDL_FINGERMOTION ? EventType::FingerMotion : EventType::FingerUp;
            r.fingerId = (long long)e.tfinger.fingerId; r.fx = e.tfinger.x; r.fy = e.tfinger.y; return true;
        case SDL_DROPFILE:
            r.type = EventType::DropFile; r.text = e.drop.file ? e.drop.file : "";
            if (e.drop.file) SDL_free(e.drop.file);
            return true;
        case SDL_CONTROLLERDEVICEADDED:   r.type = EventType::GamepadAdded;   r.deviceId = e.cdevice.which; return true;
        case SDL_CONTROLLERDEVICEREMOVED: r.type = EventType::GamepadRemoved; r.deviceId = e.cdevice.which; return true;
        case SDL_JOYDEVICEADDED:   // a device SDL has no controller layout for: offered as a generic pad
            if (!GenericPadsOn() || SDL_IsGameController(e.jdevice.which) == SDL_TRUE) return false;
            RawInstances().push_back((int)SDL_JoystickGetDeviceInstanceID(e.jdevice.which));
            r.type = EventType::GamepadAdded; r.deviceId = e.jdevice.which; return true;
        case SDL_JOYDEVICEREMOVED: {
            auto& v = RawInstances();
            auto it = std::find(v.begin(), v.end(), (int)e.jdevice.which);
            if (it == v.end()) return false;   // a mapped controller: its CONTROLLERDEVICEREMOVED covers it
            v.erase(it);
            r.type = EventType::GamepadRemoved; r.deviceId = e.jdevice.which; return true;
        }
        default: return false;
        }
    }
    Uint32 m_started = 0;
};

} // namespace

std::unique_ptr<Backend> CreateSdl2Backend() { return std::unique_ptr<Backend>(new SdlBackend()); }

std::unique_ptr<Window> WrapSdl2Window(void* sdlWindow) {
    if (!sdlWindow) return nullptr;
    return std::unique_ptr<Window>(new SdlWindow((SDL_Window*)sdlWindow, false));
}

} // namespace okay::platform
#endif // OKAY_PLATFORM_HAVE_SDL2
