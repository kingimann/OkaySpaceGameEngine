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

/// The pointer shapes an app can ask for (Backend::SetSystemCursor): the OS's own cursors, no images of ours.
enum class SystemCursor { Arrow, TextInput, Hand, ResizeAll, ResizeNS, ResizeEW, ResizeNESW, ResizeNWSE, NotAllowed, Wait };

/// How Window::Flash asks for attention: stop asking, flash once, or keep asking until the window has the focus.
enum class FlashMode { Cancel, Briefly, UntilFocused };

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

/// The largest window edge the player will ask for (a bigger number is a typo, not a screen).
constexpr int kMaxWindowExtent = 16384;
/// A window width or height read from a config file: anything below 1 (zero, negative) or past
/// kMaxWindowExtent is not a size, so it becomes `fallback`. Backends refuse such sizes, which
/// leaves the caller with no window at all.
inline int ValidWindowExtent(int v, int fallback) { return v >= 1 && v <= kMaxWindowExtent ? v : fallback; }

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
    /// True while in desktop fullscreen (set by WindowDesc::fullscreen or SetFullscreen).
    virtual bool IsFullscreen() const { return false; }
    /// Window / task-bar icon from tightly packed RGBA8 pixels. False = not supported here.
    virtual bool SetIcon(const uint8_t* rgba, int w, int h) { (void)rgba; (void)w; (void)h; return false; }
    /// Refresh rate (Hz) of the display the window is on; 0 = unknown.
    virtual int RefreshRateHz() const { return 0; }
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

    // ---- State queries and focus: what the apps asked SDL_GetWindowFlags and SDL_SetWindowInputFocus ----
    // Whatever the window manager decides (minimised, which window is under the pointer) is current only after the app has
    // read its events (Backend::PollEvent or Backend::PumpEvents), as with SDL.
    /// True while the window is minimised (iconified).
    virtual bool IsMinimized() const { return false; }
    /// False after Hide() and for a window created hidden; true after Show(). What a window that a window manager has minimised
    /// reports is not promised: the own backends say visible, SDL on X11 sets its HIDDEN flag as well (ask IsMinimized()).
    virtual bool IsVisible() const { return true; }
    /// True while the mouse pointer is over this window.
    virtual bool HasMouseFocus() const { return false; }
    /// Give this window the keyboard focus. False = refused (a hidden window cannot take it) or not supported here.
    virtual bool SetInputFocus() { return false; }

    // ---- Attention and the input method (the launcher asked SDL_FlashWindow and SDL_SetTextInputRect) ----
    /// Ask for the user's attention without taking the focus: the task-bar button flashes (Win32 FlashWindowEx), the
    /// window manager marks the window urgent (X11 urgency hint; Briefly clears it again after about a second, UntilFocused
    /// when the window gets the focus). False = not supported here.
    virtual bool Flash(FlashMode m) { (void)m; return false; }
    /// Where the text being typed is, in window pixels (the caret's top-left corner and the line's size): the input method's
    /// composition and candidate windows open there (Win32 ImmSetCompositionWindow / ImmSetCandidateWindow, X11 the XIM
    /// spot location when the input method draws next to the caret). False = not supported here, or the input method of
    /// this window does not place its windows.
    virtual bool SetTextInputArea(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; return false; }
};

/// A non-owning Window over an SDL_Window* that the app made itself with SDL_CreateWindow, so the Window methods (and the state
/// queries above) work on it while the app still creates its windows. `sdlWindow` is an SDL_Window*. Destroying the wrapper does
/// not destroy the SDL window; a GL context made through the wrapper is the wrapper's own. Null when `sdlWindow` is null or the sdl2
/// backend is not compiled in. Transitional: it goes away when the apps create their windows with Backend::MakeWindow.
std::unique_ptr<Window> WrapSdl2Window(void* sdlWindow);

/// One SDL_Event the app read itself with SDL_PollEvent (`sdlEvent` is a const SDL_Event*), as the sdl2 backend's PollEvent would have
/// returned it: the same fields, the same event kinds (others give false), with two differences: a dropped file's path is copied
/// and SDL's string is NOT freed (the app still owns the event and frees it), and the wheel carries SDL's fractional amounts
/// (preciseX / preciseY, touchpads) rather than whole notches, as ImGui's SDL2 backend reads them. False when the event has no okay::platform form or the sdl2 backend is not
/// compiled in. Transitional, like WrapSdl2Window: the editor's ImGui glue reads SDL's queue this way while SDL_Renderer still draws
/// its window (OKAY_EDITOR_PLATFORM=own, docs/sdl_removal_plan.md stage E1).
bool TranslateSdl2Event(const void* sdlEvent, Event& out);

// ---- Displays -----------------------------------------------------------------
/// A rectangle on the desktop, in the same coordinates as Window::GetPosition.
struct DisplayRect { int x = 0, y = 0, w = 0, h = 0; };
/// One monitor. `usable` is `bounds` minus task bars and panels (the OS's work area; equal to `bounds` when the OS
/// says nothing). `scale` is the DPI scale the OS asks for: 1.0 = 96 DPI, 1.5 = 144 DPI.
struct DisplayInfo {
    std::string name;
    DisplayRect bounds, usable;
    float scale = 1.0f;
    bool primary = false;
};
/// The displays of the default backend, the primary one first (empty when the backend has no video, or none is up).
std::vector<DisplayInfo> Displays();
/// Whether a window at (x, y) of size w x h can be reached: at least 160 x 80 of it on one display's usable area and its
/// top edge (the title bar) inside that area, give or take 8 pixels above and 40 below. The launcher's "keep on screen" rule.
bool WindowReachable(const std::vector<DisplayInfo>& displays, int x, int y, int w, int h);
/// A window of w x h moved to the middle of the usable area of `d`, shrunk to leave a 20 pixel margin on each side.
DisplayRect CenterOnDisplay(const DisplayInfo& d, int w, int h);

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
    /// The pointer shape over this app's windows (the OS's own cursor for `c`). False when the backend cannot change it.
    /// ShowCursor(false) still hides it; the shape comes back with ShowCursor(true).
    virtual bool SetSystemCursor(SystemCursor c) { (void)c; return false; }
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

    // ---- Focus, pointer and event pump (the launcher and editor asked SDL for these directly) ----
    /// Window::Id() of the window that has the keyboard focus; 0 when none of ours has it (SDL_GetKeyboardFocus).
    virtual uint32_t KeyboardFocusWindowId() { return 0; }
    /// Window::Id() of the window the mouse pointer is over; 0 when none of ours (SDL_GetMouseFocus).
    virtual uint32_t MouseFocusWindowId() { return 0; }
    /// Whether relative mouse mode is on: what SetRelativeMouse last set successfully (SDL_GetRelativeMouseMode).
    virtual bool IsRelativeMouse() { return false; }
    /// The pointer position on the whole desktop (not relative to a window; either pointer may be null) and the MouseMask
    /// bits of the buttons held (SDL_GetGlobalMouseState). The own backends ask the OS. The sdl2 backend answers with SDL's call,
    /// which under X11 follows the pointer only while SDL sees it (over SDL's windows) and only after events were read
    /// (PollEvent or PumpEvents): measured under Xvfb.
    virtual uint32_t GlobalMouseState(int* x, int* y) { if (x) *x = 0; if (y) *y = 0; return 0; }
    /// Read the OS events that are waiting without taking one: window state and focus are current afterwards, and PollEvent still
    /// returns every event (SDL_PumpEvents).
    virtual void PumpEvents() {}

    /// Windows only: a function called with every window message (HWND, message, wParam, lParam) of this backend's windows,
    /// before the backend handles it (SDL_SetWindowsMessageHook): the launcher reads its tray icon's clicks and the message a
    /// second launcher sends this way. The own backend calls it from its window procedure, so it sees sent and posted
    /// messages; the sdl2 backend hands it to SDL. Null removes it. False = not supported here (every other platform).
    using NativeMessageHook = void (*)(void* user, void* hwnd, unsigned int msg, uint64_t wParam, int64_t lParam);
    virtual bool SetNativeMessageHook(NativeMessageHook hook, void* user) { (void)hook; (void)user; return false; }

    /// The monitors, the primary one first (SDL_GetNumVideoDisplays / SDL_GetDisplayBounds / SDL_GetDisplayUsableBounds /
    /// SDL_GetDisplayDPI). The own backends ask the OS: X11 RandR 1.5 monitors (libXrandr loaded at run time), else Xinerama,
    /// else the root window, with _NET_WORKAREA as the usable area and Xft.dpi as the scale; Win32 EnumDisplayMonitors /
    /// GetMonitorInfoW and GetDpiForMonitor (shcore.dll loaded at run time). Empty when video is not up.
    virtual std::vector<DisplayInfo> Displays() { return {}; }
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
