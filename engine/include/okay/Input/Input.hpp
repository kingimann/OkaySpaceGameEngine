#pragma once
#include "okay/Math/Vec2.hpp"
#include <unordered_map>
#include <vector>
#include <string>

namespace okay {

/// Polled keyboard state, mirroring static `Input` API. On a terminal
/// it reads non-blocking keystrokes; in non-interactive environments it simply
/// reports no input, so games still run headlessly.
class Input {
public:
    static constexpr int kMaxGamepads = 4;   ///< players / controllers read at once

    // Synthetic key codes for modifier keys that have no printable character.
    // The windowed runtimes feed these so controllers can bind sprint/crouch to
    // Shift / Ctrl. They sit in the ASCII control range so they never collide
    // with letters, digits or space.
    static constexpr char KeyShift = 16;
    static constexpr char KeyCtrl  = 17;
    /// Command on macOS, the Windows / Super key elsewhere. Either side. Text
    /// fields treat it like Ctrl for copy, cut and paste.
    static constexpr char KeyCmd   = static_cast<char>(153);
    // Arrow keys, distinct from WASD. AxisWASD counts either. The player and
    // the editor feed both, so a script that asks for "up" or for W agrees.
    static constexpr char KeyUp    = 1;
    static constexpr char KeyDown  = 2;
    static constexpr char KeyLeft  = 3;
    static constexpr char KeyRight = 4;

    // Extended keys. The char range 128..255 is free (letters, digits and punctuation
    // are ASCII, the controls are below 32), so these never collide with a saved
    // binding. They are PHYSICAL keys: the runtime feeds them from SDL scancodes, so
    // F1 is F1 on every keyboard layout. KeyShift / KeyCtrl / KeyAlt are derived: they
    // read true while either the left or the right key is held.
    static constexpr char KeyF1 = static_cast<char>(128);   // F1..F12 are 128..139
    static constexpr char KeyF12 = static_cast<char>(139);
    static constexpr char KeyInsert   = static_cast<char>(140);
    static constexpr char KeyDelete   = static_cast<char>(141);
    static constexpr char KeyHome     = static_cast<char>(142);
    static constexpr char KeyEnd      = static_cast<char>(143);
    static constexpr char KeyPageUp   = static_cast<char>(144);
    static constexpr char KeyPageDown = static_cast<char>(145);
    static constexpr char KeyLShift   = static_cast<char>(146);
    static constexpr char KeyRShift   = static_cast<char>(147);
    static constexpr char KeyLCtrl    = static_cast<char>(148);
    static constexpr char KeyRCtrl    = static_cast<char>(149);
    static constexpr char KeyAlt      = static_cast<char>(150);
    static constexpr char KeyLAlt     = static_cast<char>(151);
    static constexpr char KeyRAlt     = static_cast<char>(152);
    /// The code of function key `n` (1..12), or 0.
    static constexpr char KeyF(int n) { return n >= 1 && n <= 12 ? static_cast<char>(128 + n - 1) : static_cast<char>(0); }

    /// Map a friendly key NAME to the char code this layer uses ("space",
    /// "enter", "shift", "up", "f5", "lshift", "pageup", "a", "7"...). A single
    /// character is taken as-is (lowercased). Arrows are KeyUp / KeyDown / KeyLeft /
    /// KeyRight; the player and editor also report them as WASD, and AxisWASD counts
    /// both. Names are case-insensitive and ignore spaces, '_' and '-' ("Left Shift",
    /// "page_up"). An unknown multi-char name falls back to its first letter (kept so
    /// old scripts behave the same; check IsKnownKeyName first). Shared by
    /// OkayScript's key("...") builtins and the visual-script key nodes.
    static char KeyFromName(const std::string& s) {
        if (s.empty()) return 0;
        if (s.size() == 1) return LowerKey(s[0]);
        if (const KeyEntry* e = FindKeyName(s)) return e->code;
        return LowerKey(s[0]);
    }
    /// True for a key name KeyFromName understands exactly: one character, or one
    /// of space / up / down / left / right / enter / return / escape / esc /
    /// backspace / tab / shift / ctrl / control / cmd / command / alt / f1..f12 / insert / delete /
    /// home / end / pageup / pagedown / lshift / rshift / lctrl / rctrl / lalt /
    /// ralt (any case). Any other name would silently become its first letter.
    static bool IsKnownKeyName(const std::string& s) { return s.size() == 1 || FindKeyName(s) != nullptr; }

    /// True while the key is held down.
    static bool GetKey(char key);
    /// True only on the frame the key was first pressed.
    static bool GetKeyDown(char key);
    /// True only on the frame the key was released.
    static bool GetKeyUp(char key);

    /// Composite WASD / arrow axis in the range [-1, 1] per component.
    static Vec2 AxisWASD();

    /// True if ANY key is held this frame — "press any key".
    static bool AnyKey();
    /// True only on the frame a new key was first pressed (Input.anyKeyDown).
    static bool AnyKeyDown();
    /// The key first pressed this frame (the lowest code if several), or 0. For
    /// "press a key to bind" fields.
    static char FirstKeyDown();
    /// A key code's display name, the inverse of KeyFromName: "Space", "Enter",
    /// "Shift", "Up", "A", "7"...
    static std::string KeyName(char key);

    // ---- Mouse (driven by FeedMouse from the windowed runtime) ----------
    /// Cursor position in pixels (origin top-left of the window).
    static Vec2 MousePosition();
    /// How far the cursor moved since the last frame, in pixels — for mouse-look /
    /// drag without tracking the previous position yourself.
    static Vec2 MouseDelta();
    /// True while a mouse button is held (0 = left, 1 = right, 2 = middle).
    static bool GetMouseButton(int button);
    /// True only on the frame the button was first pressed.
    static bool GetMouseButtonDown(int button);
    /// True only on the frame the button was released.
    static bool GetMouseButtonUp(int button);

    // ---- Gamepad (driven by FeedGamepad from the windowed runtime) -------
    /// Button ids for GetGamepadButton. The triggers also count as buttons once
    /// pulled past half way.
    enum PadButton {
        PadA = 0, PadB, PadX, PadY, PadBack, PadStart, PadLB, PadRB,
        PadUp, PadDown, PadLeft, PadRight, PadLeftStick, PadRightStick, PadLT, PadRT,
        PadButtonCount
    };
    // Every pad reader takes a `player` index (0..kMaxGamepads-1, default 0 = the
    // first controller), so existing single-controller code keeps working.
    /// Left-stick axis, each component in [-1, 1] (y up).
    static Vec2 GamepadAxis(int player = 0);
    /// Right-stick axis, each component in [-1, 1] (y up).
    static Vec2 GamepadRightAxis(int player = 0);
    /// Trigger pull in [0, 1]: 0 = left trigger, 1 = right trigger.
    static float GamepadTrigger(int side, int player = 0);
    /// True while that controller is plugged in.
    static bool GamepadConnected(int player = 0);
    /// How many controllers are plugged in.
    static int GamepadCount();

    /// How raw stick / trigger values become the values games read.
    struct StickSettings {
        enum class Shape { Radial, Axial };
        /// Radial: the whole stick vector under `deadZone` reads 0 (round dead zone, no
        /// drift-bias on diagonals). Axial: each axis is dead on its own, which makes
        /// the stick snap to the cardinal directions.
        Shape shape = Shape::Radial;
        float deadZone = 0.18f;   ///< 0..0.95, the part of the throw that reads 0
        float outerZone = 1.0f;   ///< where the stick reads full; a worn stick that tops out at 0.9 sets 0.9
        float curve = 1.0f;       ///< response curve: out = in ^ curve (1 linear, 2 fine aim near the centre)
    };
    struct TriggerSettings {
        float deadZone = 0.0f;    ///< pull under this reads 0
        float threshold = 0.5f;   ///< processed pull at which the trigger counts as a pressed button (PadLT / PadRT)
        float curve = 1.0f;
    };
    struct PadSettings { StickSettings left, right; TriggerSettings triggers; };
    /// The settings FeedGamepadRaw applies for a controller. Defaults match the old
    /// fixed behaviour: radial 0.18 dead zone, linear, triggers pressed at half.
    static PadSettings& GamepadSettings(int player = 0);
    /// Pure helpers: a raw stick / trigger value through the settings.
    static Vec2 ProcessStick(const Vec2& raw, const StickSettings& s);
    static float ProcessTrigger(float raw, const TriggerSettings& s);

    /// "a", "b", "x", "y", "back", "start", "lb", "rb", "up", "down", "left",
    /// "right", "ls", "rs", "lt", "rt" (any case) -> PadButton, or -1.
    static int GamepadButtonFromName(const std::string& name);
    /// Short display name ("A", "LB", "RT", "D-pad Up", ...).
    static std::string GamepadButtonName(int button);
    /// The lowercase name GamepadButtonFromName reads ("a", "lb", "rt", ...).
    static std::string GamepadButtonKey(int button);
    /// The gamepad button first pressed this frame, or -1. For "press a button to bind".
    static int FirstGamepadButtonDown(int player = 0);
    /// True while a gamepad button is held (see PadButton).
    static bool GetGamepadButton(int button, int player = 0);
    /// True only on the frame the button was first pressed.
    static bool GetGamepadButtonDown(int button, int player = 0);
    /// True only on the frame the button was released.
    static bool GetGamepadButtonUp(int button, int player = 0);

    /// Drive input from a non-terminal source (SDL window): pass the set of keys
    /// currently held this frame. Advances the down/up edge state.
    static void FeedKeys(const std::vector<char>& downKeys);
    /// Drive mouse state from a windowed runtime. `buttonMask` bit 0/1/2 =
    /// left/right/middle held. Advances the mouse down/up edge state.
    static void FeedMouse(const Vec2& position, unsigned buttonMask);
    /// Drive gamepad state: left-stick axis and a held-button bitmask (bit n =
    /// button id n). Advances the gamepad down-edge state.
    static void FeedGamepad(const Vec2& axis, unsigned buttonMask);
    /// The rest of the controller, fed with FeedGamepad each frame: right stick,
    /// trigger pulls [0, 1], and whether a controller is connected.
    static void FeedGamepadExtra(const Vec2& rightAxis, float leftTrigger, float rightTrigger, bool connected);
    /// The same for controller `player`. Values are taken as given (already processed).
    static void FeedGamepadAt(int player, const Vec2& axis, unsigned buttonMask);
    static void FeedGamepadExtraAt(int player, const Vec2& rightAxis, float leftTrigger, float rightTrigger, bool connected);
    /// Feed a controller's RAW state: both sticks and triggers go through
    /// GamepadSettings(player) (dead zones, outer zone, curve), and the LT / RT button
    /// bits are set when the processed pull passes the trigger threshold. `buttonMask`
    /// carries the other buttons. Pass connected = false for an empty slot.
    static void FeedGamepadRaw(int player, const Vec2& rawLeft, const Vec2& rawRight, float rawLeftTrigger,
                               float rawRightTrigger, unsigned buttonMask, bool connected);

    /// Shake the controller: `low` / `high` are the two motors' strength [0, 1]
    /// (low = heavy rumble, high = light buzz) for `seconds`. The windowed runtime
    /// picks the request up with TakeRumble.
    static void Rumble(float low, float high, float seconds, int player = 0);
    /// The latest Rumble request since the last call, for the runtime to play.
    static bool TakeRumble(float& low, float& high, float& seconds, int player = 0);

    // ---- Text input (real typed characters: shift, caps, symbols, layout) ----
    /// The characters typed since the last ClearTypedText() — for text fields.
    /// Fed by the windowed runtime from the OS text-input event (SDL_TEXTINPUT),
    /// so it correctly produces uppercase, punctuation and symbols, unlike the
    /// polled key state. Excludes control keys (backspace/enter — use GetKeyDown).
    static const std::string& TypedText();
    /// Append OS-decoded typed text for this frame (windowed runtime / tests).
    static void FeedText(const std::string& utf8);
    /// Clear the per-frame typed text and mouse-wheel delta, and drop lifted touches —
    /// call once at the start of each frame, before polling events.
    static void ClearTypedText();

    // ---- Mouse wheel (driven by FeedMouseWheel from the windowed runtime) -----
    /// This frame's wheel delta (notches; +up / -down). 0 when idle.
    static float MouseWheel();
    /// Add to this frame's wheel delta (from the OS scroll event).
    static void FeedMouseWheel(float delta);

    // ---- Touch (driven by FeedTouch from the windowed runtime's finger events) ----
    /// One finger on the screen. `position` and `start` are in the same pixels as
    /// MousePosition(); `delta` is this frame's movement.
    struct Touch {
        enum class Phase { Began, Moved, Stationary, Ended };
        long long id = 0;
        Vec2  position;
        Vec2  start;
        Vec2  delta;
        Phase phase = Phase::Began;
    };
    /// Fingers touching the screen this frame (a lifted finger stays one frame, Ended).
    static int TouchCount();
    /// The i-th touch, 0 <= i < TouchCount().
    static const Touch& GetTouch(int index);
    /// True while the pointer is driven by a finger (a touch is down or just ended).
    /// The OS also turns the first finger into the left mouse button.
    static bool PointerIsTouch();
    /// True once a finger has moved more than `kTouchDragPixels` from where it went
    /// down: it is a drag (scrolling), not a tap, so buttons under it don't fire.
    static bool TouchDragging();
    static constexpr float kTouchDragPixels = 10.0f;
    /// Report a finger event: `state` 0 = down, 1 = moved, 2 = up.
    static void FeedTouch(long long id, int state, const Vec2& position);

    // ---- UI capture (modal panels: inventories, menus) ----
    /// True while a modal UI (an open inventory, a menu) owns the pointer — player
    /// controllers should pause mouse-look / movement so the game feels paused.
    static bool UICaptured();
    /// Set by the windowed runtime each frame before the scene updates.
    static void SetUICaptured(bool captured);

private:
    friend class Application;
    static void BeginSession();   // put terminal into raw, non-blocking mode
    static void EndSession();     // restore terminal
    static void Poll();           // sample input once per frame

    static std::unordered_map<char, bool> s_current;
    static std::unordered_map<char, bool> s_previous;
    static bool s_interactive;

    static Vec2     s_mousePos;
    static bool     s_mouseFed;       // a mouse sample has arrived (first one has no delta)
    static Vec2     s_mousePrevPos;   // cursor position last frame (for MouseDelta)
    static unsigned s_mouseCurrent;   // bitmask of held buttons this frame
    static unsigned s_mousePrevious;  // bitmask from last frame

    struct PadState {
        Vec2 axis, right;
        float trigger[2] = {0.0f, 0.0f};
        bool connected = false;
        unsigned current = 0, previous = 0;
    };
    static PadState s_pads[kMaxGamepads];
    static PadSettings s_padSettings[kMaxGamepads];

    struct KeyEntry { const char* name; char code; };
    static char LowerKey(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
    /// Look a multi-character key name up (case, spaces, '_' and '-' ignored).
    static const KeyEntry* FindKeyName(const std::string& s) {
        static const KeyEntry kTable[] = {
            {"space", ' '}, {"up", KeyUp}, {"down", KeyDown}, {"left", KeyLeft}, {"right", KeyRight},
            {"enter", '\r'}, {"return", '\r'}, {"escape", static_cast<char>(27)}, {"esc", static_cast<char>(27)},
            {"backspace", static_cast<char>(8)}, {"tab", '\t'},
            {"shift", KeyShift}, {"ctrl", KeyCtrl}, {"control", KeyCtrl}, {"alt", KeyAlt},
            {"cmd", KeyCmd}, {"command", KeyCmd}, {"meta", KeyCmd}, {"gui", KeyCmd},
            {"f1", KeyF(1)}, {"f2", KeyF(2)}, {"f3", KeyF(3)}, {"f4", KeyF(4)}, {"f5", KeyF(5)}, {"f6", KeyF(6)},
            {"f7", KeyF(7)}, {"f8", KeyF(8)}, {"f9", KeyF(9)}, {"f10", KeyF(10)}, {"f11", KeyF(11)}, {"f12", KeyF(12)},
            {"insert", KeyInsert}, {"ins", KeyInsert}, {"delete", KeyDelete}, {"del", KeyDelete},
            {"home", KeyHome}, {"end", KeyEnd},
            {"pageup", KeyPageUp}, {"pgup", KeyPageUp}, {"pagedown", KeyPageDown}, {"pgdn", KeyPageDown},
            {"lshift", KeyLShift}, {"leftshift", KeyLShift}, {"rshift", KeyRShift}, {"rightshift", KeyRShift},
            {"lctrl", KeyLCtrl}, {"leftctrl", KeyLCtrl}, {"leftcontrol", KeyLCtrl},
            {"rctrl", KeyRCtrl}, {"rightctrl", KeyRCtrl}, {"rightcontrol", KeyRCtrl},
            {"lalt", KeyLAlt}, {"leftalt", KeyLAlt}, {"ralt", KeyRAlt}, {"rightalt", KeyRAlt},
        };
        std::string k;
        for (char c : s) if (c != ' ' && c != '_' && c != '-') k += LowerKey(c);
        for (const KeyEntry& e : kTable) if (k == e.name) return &e;
        return nullptr;
    }

    static std::string s_typedText;
    static float       s_mouseWheel;
    static std::vector<Touch> s_touches;
    static bool        s_uiCaptured;
};

} // namespace okay
