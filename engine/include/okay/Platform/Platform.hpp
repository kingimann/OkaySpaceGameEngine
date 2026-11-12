#pragma once
// okay::platform - the seam between the engine and the OS / windowing library.
//
// Everything the engine takes from SDL2 (windows, events, keyboard and mouse,
// the OpenGL context, timers, clipboard, audio devices, game controllers) goes
// through the interfaces here, so SDL2 is one backend among several:
//
//   BackendKind::Sdl2  thin wrapper over SDL2 (the default).
//   BackendKind::Own   our own code: Linux = X11/GLX + ALSA + evdev, all loaded
//                      with dlopen at run time (no link dependency); Windows =
//                      Win32/WGL (compiles, untested); macOS = not written.
//
// The CMake option OKAY_PLATFORM_BACKEND=sdl2|own picks DefaultKind(). Both
// backends can be created side by side with Get(kind), which is how the tests
// run the same checks on each.
//
// Header rules (MinGW): no <mutex>/<thread> here, and no names that clash with
// Windows or X11 macros (CreateWindow, None, Status, Bool, ...).
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay::platform {

enum class BackendKind { Sdl2, Own };

const char* BackendName(BackendKind k);                 // "sdl2" / "own"
bool ParseBackendName(const char* s, BackendKind& out); // case-sensitive, "sdl2" or "own"

class Backend;

/// True when `k` is compiled in on this build (not that a display exists).
bool IsCompiledIn(BackendKind k);
/// The backend the build selected (OKAY_PLATFORM_BACKEND), falling back to
/// whichever one is compiled in. Null only when none is (e.g. no SDL2, no own).
Backend* Default();
/// A backend by kind, or null when it is not compiled in. The instance lives for
/// the whole process. Creating it does not touch the OS; call Backend::Init.
Backend* Get(BackendKind k);
BackendKind DefaultKind();

// ---- Keys ------------------------------------------------------------------
// Physical keys, numbered with the USB HID usage ids (the same numbers as SDL
// scancodes). Only the keys the engine uses have names; others come through as
// their raw number (< kKeyCount) so nothing is lost.
enum class Key : uint16_t {
    Unknown = 0,
    A = 4, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    N1 = 30, N2, N3, N4, N5, N6, N7, N8, N9, N0,
    Return = 40, Escape = 41, Backspace = 42, Tab = 43, Space = 44,
    Minus = 45, Equals = 46, LeftBracket = 47, RightBracket = 48, Backslash = 49,
    Semicolon = 51, Apostrophe = 52, Grave = 53, Comma = 54, Period = 55, Slash = 56,
    CapsLock = 57,
    F1 = 58, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    PrintScreen = 70, ScrollLock = 71, Pause = 72,
    Insert = 73, Home = 74, PageUp = 75, Delete = 76, End = 77, PageDown = 78,
    Right = 79, Left = 80, Down = 81, Up = 82,
    NumLock = 83, KpDivide = 84, KpMultiply = 85, KpMinus = 86, KpPlus = 87, KpEnter = 88,
    Kp1 = 89, Kp2, Kp3, Kp4, Kp5, Kp6, Kp7, Kp8, Kp9, Kp0 = 98, KpPeriod = 99,
    LCtrl = 224, LShift = 225, LAlt = 226, LGui = 227,
    RCtrl = 228, RShift = 229, RAlt = 230, RGui = 231,
};
constexpr int kKeyCount = 512;

enum Mod : uint32_t { ModShift = 1, ModCtrl = 2, ModAlt = 4, ModGui = 8, ModCaps = 16, ModNum = 32 };

enum MouseButton : int { MouseLeft = 1, MouseMiddle = 2, MouseRight = 3, MouseX1 = 4, MouseX2 = 5 };
/// Bit for a MouseButton in the mask returned by Backend::MouseState.
constexpr uint32_t MouseMask(int button) { return 1u << (button - 1); }

// ---- Events ----------------------------------------------------------------
enum class EventType {
    NoEvent,
    Quit,              // the last window was closed / the app was asked to quit
    WindowClose,       // the user asked to close one window
    WindowResized,     // width / height = the new client size
    WindowFocusGained,
    WindowFocusLost,
    WindowExposed,
    KeyDown, KeyUp,    // key, mods, repeat
    TextInput,         // text = UTF-8, only between StartTextInput / StopTextInput
    MouseMove,         // x, y = window position, dx, dy = motion (relative mode: raw)
    MouseButtonDown, MouseButtonUp,   // button, x, y, clicks
    MouseWheel,        // wheelX, wheelY (one notch = 1.0, up = +y)
    DropFile,          // text = the path
    GamepadAdded,      // deviceId = device index to pass to Backend::OpenGamepad
    GamepadRemoved,    // deviceId = Gamepad::InstanceId()
    FingerDown, FingerMotion, FingerUp,   // touch: fingerId, fx, fy (0..1 of the window). sdl2 only; own never fires them
};

struct Event {
    EventType type = EventType::NoEvent;
    uint32_t timestampMs = 0;
    uint32_t windowId = 0;
    Key key = Key::Unknown;
    uint32_t mods = 0;
    bool repeat = false;
    int x = 0, y = 0, dx = 0, dy = 0;
    int button = 0, clicks = 0;
    float wheelX = 0.0f, wheelY = 0.0f;   // up = +y whatever the OS "natural scrolling" setting is
    bool wheelFlipped = false;            // the OS reports natural scrolling: the raw (OS-preference) wheel is -wheelX/-wheelY
    long long fingerId = 0;
    float fx = 0.0f, fy = 0.0f;
    int width = 0, height = 0;
    int deviceId = 0;
    std::string text;
};

// ---- Windows and the OpenGL context -----------------------------------------
constexpr int kCentered = 0x2FFF0000;   // x / y: let the window manager centre it

struct WindowDesc {
    std::string title = "OkaySpace";
    int x = kCentered, y = kCentered;
    int width = 1280, height = 720;
    bool resizable = true;
    bool opengl = false;      // the window must be able to carry a GL context
    bool hidden = false;
    bool borderless = false;
    bool highDpi = false;
    bool fullscreen = false;  // desktop fullscreen from the start (no mode switch)
    bool maximized = false;   // start maximised (needs resizable; ignored with fullscreen)
};

struct GLAttributes {
    int major = 3, minor = 2;
    bool compatibility = true;   // compatibility profile (the engine's GL 1.x paths need it)
    int depthBits = 24;
};

class Window {
public:
    virtual ~Window() = default;
    virtual uint32_t Id() const = 0;
    /// SDL_Window* (sdl2), X11 Window id (own, Linux) or HWND (own, Windows).
    virtual void* NativeHandle() const = 0;
    virtual void GetSize(int& w, int& h) const = 0;
    virtual void GetPosition(int& x, int& y) const = 0;
    virtual void SetTitle(const std::string& title) = 0;
    virtual void SetSize(int w, int h) = 0;
    virtual void SetPosition(int x, int y) = 0;
    virtual void SetMinimumSize(int w, int h) = 0;
    virtual void Show() = 0;
    virtual void Hide() = 0;
    virtual void Raise() = 0;
    virtual void Minimize() = 0;
    virtual void Restore() = 0;
    virtual void SetFullscreen(bool on) = 0;
    virtual bool IsFocused() const = 0;
    /// True while in desktop fullscreen (set by WindowDesc::fullscreen or SetFullscreen).
    virtual bool IsFullscreen() const { return false; }
    /// Window / task-bar icon from tightly packed RGBA8 pixels. False = not supported here.
    virtual bool SetIcon(const uint8_t* rgba, int w, int h) { (void)rgba; (void)w; (void)h; return false; }
    /// Refresh rate (Hz) of the display the window is on; 0 = unknown.
    virtual int RefreshRateHz() const { return 0; }

    /// Create the GL context for this window (WindowDesc::opengl) and make it current.
    virtual bool CreateGLContext(const GLAttributes& attrs) = 0;
    virtual void DestroyGLContext() = 0;
    virtual bool MakeCurrent() = 0;          // this window's context on this thread
    virtual void SwapBuffers() = 0;
    virtual bool SetSwapInterval(int interval) = 0;   // 0 = off, 1 = vsync
    virtual void GetDrawableSize(int& w, int& h) const = 0;
};

// ---- Audio -------------------------------------------------------------------
enum class SampleFormat { S16, F32 };   // native-endian, interleaved

/// Fills `stream` (playback) or consumes it (capture), like SDL's callback.
using AudioCallback = void (*)(void* user, uint8_t* stream, int len);

struct AudioSpec {
    int freq = 48000;
    SampleFormat format = SampleFormat::S16;
    int channels = 1;
    int frames = 1024;               // buffer size in frames (a hint)
    AudioCallback callback = nullptr; // null = queue mode (Queue / Dequeue)
    void* user = nullptr;
    /// Playback or capture may change freq / channels to what the device can do
    /// when true (the actual values come back in the `got` spec).
    bool allowChange = false;
};

inline int BytesPerSample(SampleFormat f) { return f == SampleFormat::F32 ? 4 : 2; }

class AudioDevice {
public:
    virtual ~AudioDevice() = default;
    virtual const AudioSpec& Spec() const = 0;        // what the device really runs at
    virtual void Pause(bool paused) = 0;              // devices start paused
    virtual bool Queue(const void* data, uint32_t bytes) = 0;   // playback, queue mode
    virtual uint32_t QueuedBytes() const = 0;         // playback: not yet played; capture: ready to Dequeue
    virtual void ClearQueued() = 0;
    virtual uint32_t Dequeue(void* out, uint32_t maxBytes) = 0; // capture, queue mode
};

// ---- Game controllers ----------------------------------------------------------
// Numbered like SDL's SDL_GameControllerAxis / Button.
enum class GamepadAxis { LeftX = 0, LeftY, RightX, RightY, TriggerLeft, TriggerRight };
enum class GamepadButton {
    A = 0, B, X, Y, Back, Guide, Start, LeftStick, RightStick, LeftShoulder, RightShoulder,
    DpadUp, DpadDown, DpadLeft, DpadRight
};

class Gamepad {
public:
    virtual ~Gamepad() = default;
    virtual int InstanceId() const = 0;
    virtual const char* Name() const = 0;
    /// -32768..32767; sticks are +y down like SDL, triggers 0..32767.
    virtual int16_t Axis(GamepadAxis a) const = 0;
    virtual bool Button(GamepadButton b) const = 0;
    /// lo / hi motor strength 0..65535 for `ms`; false when the pad can't rumble.
    virtual bool Rumble(uint16_t lo, uint16_t hi, uint32_t ms) = 0;
};

// ---- The backend ---------------------------------------------------------------
struct InitFlags {
    bool video = true, audio = true, gamepad = true;
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual BackendKind Kind() const = 0;
    const char* Name() const { return BackendName(Kind()); }

    /// Bring the subsystems up. Returns false (and LastError() says why) when
    /// `video` was asked for and there is no display. Safe to call twice.
    virtual bool Init(const InitFlags& flags = InitFlags()) = 0;
    virtual void Shutdown() = 0;
    virtual std::string LastError() const = 0;

    // Time (usable without Init).
    virtual uint64_t TicksMs() = 0;          // monotonic, ms since first use
    virtual uint64_t PerfCounter() = 0;
    virtual uint64_t PerfFrequency() = 0;
    virtual void DelayMs(uint32_t ms) = 0;

    // Windows and events.
    virtual std::unique_ptr<Window> MakeWindow(const WindowDesc& desc) = 0;
    virtual bool PollEvent(Event& out) = 0;  // false when the queue is empty
    virtual void* GLProcAddress(const char* name) = 0;   // needs a current context
    virtual void ReleaseCurrent() = 0;       // no GL context current on this thread

    // Keyboard, mouse.
    virtual void StartTextInput() = 0;
    virtual void StopTextInput() = 0;
    virtual bool IsKeyDown(Key k) = 0;
    virtual uint32_t MouseState(int* x, int* y) = 0;   // MouseMask bits; position in the focused window
    virtual bool SetRelativeMouse(bool on) = 0;
    virtual void ShowCursor(bool show) = 0;
    virtual void WarpMouse(Window* w, int x, int y) = 0;
    /// Whether the cursor is visible (what ShowCursor last asked for; starts true).
    virtual bool IsCursorShown() { return true; }
    /// Mouse motion since the last call, and the button mask (MouseMask bits). Not
    /// supported by a backend that returns false from SupportsRelativeMouseState().
    virtual uint32_t RelativeMouseState(int* dx, int* dy) { if (dx) *dx = 0; if (dy) *dy = 0; return 0; }
    virtual bool SupportsRelativeMouseState() { return false; }
    /// Modifier keys held right now (Mod bits).
    virtual uint32_t ModState() {
        uint32_t m = 0;
        if (IsKeyDown(Key::LShift) || IsKeyDown(Key::RShift)) m |= ModShift;
        if (IsKeyDown(Key::LCtrl) || IsKeyDown(Key::RCtrl)) m |= ModCtrl;
        if (IsKeyDown(Key::LAlt) || IsKeyDown(Key::RAlt)) m |= ModAlt;
        if (IsKeyDown(Key::LGui) || IsKeyDown(Key::RGui)) m |= ModGui;
        return m;
    }
    /// The physical key that types `c` ('a'..'z', '0'..'9', ' ') on the CURRENT keyboard
    /// layout, so a game's WASD follows an AZERTY layout. Key::Unknown when none.
    /// The base version assumes a US layout.
    virtual Key KeyForChar(char c);

    // Process-level helpers the apps used to take from SDL directly.
    /// Per-monitor DPI awareness on Windows (a hint, call before the first window). False = nothing to do here.
    virtual bool SetPerMonitorDpiAware() { return false; }
    /// A modal error box (title, text), parented to `parent` when given. False = no
    /// message box available (the text is written to the log instead).
    virtual bool ShowErrorBox(const std::string& title, const std::string& text, Window* parent = nullptr);
    /// Directory of the running executable with a trailing separator ("" if unknown).
    virtual std::string BasePath();
    /// A per-user writable folder for `org` / `app` (created if missing), with a trailing
    /// separator; "" when none can be made. (Linux: $XDG_DATA_HOME or ~/.local/share.)
    virtual std::string PrefPath(const std::string& org, const std::string& app);
    /// The user's preferred languages, most preferred first, as "en", "en-US", "de-DE".
    virtual std::vector<std::string> PreferredLocales();
    /// One line to the application log (stderr / debug console), "INFO: " prefixed.
    virtual void Log(const std::string& line);

    // Clipboard (UTF-8 text).
    virtual bool SetClipboardText(const std::string& text) = 0;
    virtual std::string GetClipboardText() = 0;

    // Audio. deviceName null = the system default. Null return: see LastError().
    virtual std::unique_ptr<AudioDevice> OpenAudio(const AudioSpec& want, bool capture,
                                                   const char* deviceName, AudioSpec* got) = 0;
    virtual int AudioDeviceCount(bool capture) = 0;
    virtual std::string AudioDeviceName(int index, bool capture) = 0;

    // Game controllers.
    virtual int GamepadDeviceCount() = 0;                  // device indices 0..n-1
    virtual bool IsGamepad(int deviceIndex) = 0;
    virtual std::unique_ptr<Gamepad> OpenGamepad(int deviceIndex) = 0;
};

// ---- Free functions on the default backend ------------------------------------
// Timers work with no backend at all (they fall back to std::chrono), so call
// sites need no checks.
uint64_t TicksMs();
/// Drop-in for SDL_GetTicks(): the same 32-bit millisecond count (wraps after 49 days).
inline uint32_t TicksMs32() { return (uint32_t)TicksMs(); }
uint64_t PerfCounter();
uint64_t PerfFrequency();
void DelayMs(uint32_t ms);
/// Log one line through the default backend (stderr when there is none).
void Log(const std::string& line);

} // namespace okay::platform
