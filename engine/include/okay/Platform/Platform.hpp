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
    NonUsBackslash = 100, Application = 101,
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
    WindowDpiChanged,  // x = the window's new DPI (96 = 100 percent); the window has already been resized to the OS's suggestion. own Windows only
    TextEditing,       // input method composition: text = UTF-8 being composed ("" = cleared), x = caret in code points. own Windows only
    RenderTargetsReset,   // the graphics device lost its render-target contents: draw them again. sdl2 only (own never fires it)
    RenderDeviceReset,    // the graphics device was lost and made again: every texture must be re-created. sdl2 only
    Other,             // an event the seam has no type for. Only the sdl2 backend, and only after ReportUnmappedEvents(true); `native` has it
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
    /// The backend's own record of this event (sdl2: a `const SDL_Event*`), for the one consumer that still needs it (Dear
    /// ImGui's imgui_impl_sdl2 while it is the default). Null on the own backends. Valid only until the next PollEvent
    /// or PumpEvents call on the same backend; copy nothing out of it later. A DropFile's string has already been taken
    /// (and freed) in `text`, so do not read it from `native`. Temporary: it goes away with SDL2.
    const void* native = nullptr;
};

// ---- Windows and the OpenGL context -----------------------------------------
constexpr int kCentered = 0x2FFF0000;   // x / y: let the window manager centre it

/// The largest window edge the player will ask for (a bigger number is a typo, not a screen).
constexpr int kMaxWindowExtent = 16384;
/// A window width or height read from a config file: anything below 1 (zero, negative) or past
/// kMaxWindowExtent is not a size, so it becomes `fallback`. Backends refuse such sizes, which
/// leaves the caller with no window at all.
inline int ValidWindowExtent(int v, int fallback) { return v >= 1 && v <= kMaxWindowExtent ? v : fallback; }

/// How a window asks for attention (task bar button flash). Stop ends a flash that is running.
enum class FlashMode { Stop, Briefly, UntilFocused };

/// The pointer shapes a UI toolkit asks for (the names follow the common set of system cursors).
enum class CursorShape { Arrow, TextInput, ResizeAll, ResizeNS, ResizeEW, ResizeNESW, ResizeNWSE, Hand, Wait, Progress, NotAllowed };

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

/// What is current for GL on this thread (see Backend::CurrentGL). Opaque to callers.
struct GLCurrent {
    void* window = nullptr;
    void* context = nullptr;
};

class Window {
public:
    virtual ~Window() = default;
    virtual uint32_t Id() const = 0;
    /// Which backend made this window.
    virtual BackendKind Kind() const { return BackendKind::Own; }
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
    /// Keyboard (input) focus: the same as IsFocused().
    bool HasInputFocus() const { return IsFocused(); }
    /// True while the mouse pointer is over the window (SDL_WINDOW_MOUSE_FOCUS). False when a backend cannot tell.
    virtual bool HasMouseFocus() const { return false; }
    /// True while the window is minimized (iconic). False when a backend cannot tell.
    virtual bool IsMinimized() const { return false; }
    /// False once Hide() was called (or the window system hides it); a minimized window still counts as visible.
    virtual bool IsVisible() const { return true; }
    /// Ask for the keyboard focus for this window without raising it (SDL_SetWindowInputFocus). False = not supported.
    virtual bool SetInputFocus() { return false; }
    /// The Win32 HWND of this window (for the system-tray icon and other Win32 calls), or null off Windows. The
    /// sdl2 backend asks SDL for it, the own Windows backend returns its own handle; Linux windows return null.
    virtual void* Win32Handle() const { return nullptr; }
    /// True while in desktop fullscreen (set by WindowDesc::fullscreen or SetFullscreen).
    virtual bool IsFullscreen() const { return false; }
    /// Window / task-bar icon from tightly packed RGBA8 pixels. False = not supported here.
    virtual bool SetIcon(const uint8_t* rgba, int w, int h) { (void)rgba; (void)w; (void)h; return false; }
    /// DPI of the display the window is on (96 = 100 percent); 96 when the backend cannot tell.
    virtual int Dpi() const { return 96; }
    /// Refresh rate (Hz) of the display the window is on; 0 = unknown.
    virtual int RefreshRateHz() const { return 0; }
    /// Ask the user's attention: flash the task bar button (FlashMode::Briefly a few times, UntilFocused until the
    /// window gets focus, Stop to end it). False = not supported here (nothing happens).
    virtual bool Flash(FlashMode mode) { (void)mode; return false; }
    /// Show a finished software frame: tightly packed RGBA8 (alpha ignored), `w` x `h`, top row first, drawn at the
    /// window's top-left corner (X11 XPutImage, Win32 StretchDIBits). A frame that is not the window's size is cut
    /// or leaves the rest as it was. False when the window cannot take pixels (sdl2 windows draw through SDL_Renderer
    /// and GL windows through GL), or the server refused.
    virtual bool PresentPixels(const uint8_t* rgba, int w, int h) { (void)rgba; (void)w; (void)h; return false; }

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

// ---- Displays -----------------------------------------------------------------
struct DisplayInfo {
    std::string name;
    int x = 0, y = 0, w = 0, h = 0;          // the whole display, desktop coordinates
    int ux = 0, uy = 0, uw = 0, uh = 0;      // the part not covered by the task bar
    float dpi = 96.0f;                       // effective DPI (96 = 100 percent)
    int refreshHz = 0;                       // 0 = unknown
    bool primary = false;
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

    // ---- Optional: who it is, its raw inputs and its extras. Every one has a default
    // (unknown / none), so a backend only overrides what its host can really tell.
    /// USB vendor / product id, 0 when the host does not say.
    virtual uint16_t VendorId() const { return 0; }
    virtual uint16_t ProductId() const { return 0; }
    /// The host's own controller type (SDL_GameControllerType numbers), -1 when it has none.
    virtual int ControllerType() const { return -1; }
    /// False for a device the host has no layout for (opened as a plain joystick): Axis() and
    /// Button() read 0 and the raw readers below are what to use.
    virtual bool HasBuiltinMapping() const { return true; }
    virtual int RawAxisCount() const { return 0; }
    virtual int RawButtonCount() const { return 0; }
    virtual int RawHatCount() const { return 0; }
    virtual int16_t RawAxis(int) const { return 0; }
    virtual bool RawButton(int) const { return false; }
    virtual uint8_t RawHat(int) const { return 0; }      ///< bits 1 up, 2 right, 4 down, 8 left
    /// -1 unknown, 0 empty, 1 low, 2 medium, 3 full, 4 wired (no battery).
    virtual int PowerLevel() const { return -1; }
    /// Lightbar colour (DualShock 4 / DualSense); false when the pad has none or the host cannot set it.
    virtual bool SetLightbar(uint8_t, uint8_t, uint8_t) { return false; }
    /// Player number shown on the pad (its LEDs), 0-based; -1 clears it.
    virtual void SetPlayerIndex(int) {}
    /// Separate trigger motors (Xbox One / Series); false when the pad has none.
    virtual bool RumbleTriggers(uint16_t, uint16_t, uint32_t) { return false; }
    virtual bool HasTriggerRumble() const { return false; }
    /// Touchpads (PlayStation, Steam): how many, and one finger's state (x, y 0..1).
    virtual int TouchpadCount() const { return 0; }
    virtual bool TouchpadFinger(int /*pad*/, int /*finger*/, bool& down, float& x, float& y, float& pressure) const { down = false; x = y = pressure = 0; return false; }
    /// Motion sensors: kind 0 = gyroscope (radians per second), 1 = accelerometer (m/s^2).
    virtual bool HasSensor(int /*kind*/) const { return false; }
    virtual bool SensorData(int /*kind*/, float out[3]) { out[0] = out[1] = out[2] = 0; return false; }
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
    /// Let the system deliver pending input without taking any event out (keeps a window alive and mapped during a
    /// wait loop). sdl2: SDL_PumpEvents. own: reads what is waiting into the backend's queue, so the next PollEvent
    /// still returns it, in order. Never loses an event.
    virtual void PumpEvents() {}
    /// Put an event into the queue; PollEvent returns it after the events already waiting. Used to inject input (tests,
    /// the editor's OKAY_UI_DROP hook). False when this backend cannot make that kind of event (sdl2 makes Quit,
    /// WindowClose, WindowFocusGained/Lost, KeyDown/Up, TextInput, MouseMove, MouseButtonDown/Up, MouseWheel and DropFile).
    virtual bool PushEvent(const Event& e) { (void)e; return false; }
    /// With true, PollEvent also returns the events the seam has no type for, as EventType::Other with `native` set
    /// (sdl2 only; default off). Lets an app that still feeds Dear ImGui's SDL backend see every event it needs.
    virtual void ReportUnmappedEvents(bool on) { (void)on; }
    virtual void* GLProcAddress(const char* name) = 0;   // needs a current context
    virtual void ReleaseCurrent() = 0;       // no GL context current on this thread
    /// The GL window and context current on this thread, to put back after other GL work. Both null = none current.
    /// The two pointers are the backend's own handles (SDL_Window* / SDL_GLContext on sdl2); give them back only to RestoreGL.
    virtual GLCurrent CurrentGL() { return GLCurrent(); }
    /// Make `state` current again (a null state releases the current context). False when the backend cannot.
    virtual bool RestoreGL(const GLCurrent& state) { (void)state; return false; }
    /// How many touch screens or touch pads the system has (0 = none, or this backend cannot tell).
    virtual int TouchDeviceCount() { return 0; }

    /// Which window has the keyboard focus / is under the mouse (Window::Id()); 0 = none of ours, or unknown.
    virtual uint32_t KeyboardFocusWindowId() { return 0; }
    virtual uint32_t MouseFocusWindowId() { return 0; }
    /// The backend's own handle (same kind as Window::NativeHandle()) of the window with this Id(); null when there is
    /// none. Lets code that still holds a bare id (an ImGui viewport handle) reach the window.
    virtual void* NativeHandleFromId(uint32_t id) { (void)id; return nullptr; }

    // Keyboard, mouse.
    virtual void StartTextInput() = 0;
    virtual void StopTextInput() = 0;
    virtual bool IsKeyDown(Key k) = 0;
    virtual uint32_t MouseState(int* x, int* y) = 0;   // MouseMask bits; position in the focused window
    virtual bool SetRelativeMouse(bool on) = 0;
    /// Whether relative (locked, endless) mouse mode is on right now.
    virtual bool IsRelativeMouse() { return false; }
    virtual void ShowCursor(bool show) = 0;
    virtual void WarpMouse(Window* w, int x, int y) = 0;
    /// Whether the cursor is visible (what ShowCursor last asked for; starts true).
    virtual bool IsCursorShown() { return true; }
    /// Change the pointer's shape (the system cursor of that kind). False = this backend cannot (the shape stays as it was).
    virtual bool SetCursorShape(CursorShape shape) { (void)shape; return false; }
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
    /// The character `k` types on the CURRENT layout, as KeyForChar's inverse: 'a'..'z', '0'..'9' or ' '; 0 when `k` types
    /// none of those. (Found by asking KeyForChar for each of them, so it follows whatever layout that follows.)
    virtual char CharForKey(Key k);

    /// Displays, primary first and then left to right. DisplayAt fills `out` and returns false for an index out of range.
    /// The base versions know none (0 displays); the own Windows backend and the sdl2 backend list them.
    virtual int DisplayCount() { return 0; }
    virtual bool DisplayAt(int index, DisplayInfo& out) { (void)index; (void)out; return false; }
    /// Where the input method should put its candidate list: the caret rectangle in window client pixels
    /// (own Windows only; elsewhere nothing happens).
    virtual void SetTextInputRect(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }

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

/// A non-owning Window over an SDL_Window* the app made itself (the bridge that lets window calls move behind the
/// seam before windows are created by Backend::MakeWindow). Null when sdl2 is not compiled in or `sdlWindow` is
/// null. The caller still owns and destroys the SDL window, and must destroy it only after this object. A GL
/// context made through the wrapper (CreateGLContext) belongs to the wrapper and goes with it. Temporary: it goes
/// away with SDL2.
std::unique_ptr<Window> WrapSdl2Window(void* sdlWindow);

/// Turn one SDL_Event (passed as `const SDL_Event*`) into a platform Event, for code that still polls SDL itself but
/// feeds a seam consumer (the ImGui platform feed). Handles quit, window, key, text, mouse and wheel events only and
/// touches nothing in the SDL event (a drop's string is not read). False for any other kind, or when sdl2 is not
/// compiled in. Temporary: it goes away with SDL2.
bool TranslateSdl2Event(const void* sdlEvent, Event& out);

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
