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
    // Synthetic key codes for modifier keys that have no printable character.
    // The windowed runtimes feed these so controllers can bind sprint/crouch to
    // Shift / Ctrl. They sit in the ASCII control range so they never collide
    // with letters, digits or space.
    static constexpr char KeyShift = 16;
    static constexpr char KeyCtrl  = 17;
    // Arrow keys, distinct from WASD. AxisWASD counts either. The player and
    // the editor feed both, so a script that asks for "up" or for W agrees.
    static constexpr char KeyUp    = 1;
    static constexpr char KeyDown  = 2;
    static constexpr char KeyLeft  = 3;
    static constexpr char KeyRight = 4;

    /// Map a friendly key NAME to the char code this layer uses ("space",
    /// "enter", "shift", "up", "a", "7"...). A single character is taken as-is
    /// (lowercased). Arrows are KeyUp / KeyDown / KeyLeft / KeyRight; the
    /// player and editor also report them as WASD, and AxisWASD counts both.
    /// Unknown multi-char names fall back to their first letter. Shared by
    /// OkayScript's key("...") builtins and the visual-script key nodes.
    static char KeyFromName(const std::string& s) {
        if (s.empty()) return 0;
        auto lower1 = [](char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; };
        if (s.size() == 1) return lower1(s[0]);
        std::string k; k.reserve(s.size());
        for (char c : s) k += lower1(c);
        if (k == "space")                    return ' ';
        if (k == "up")                       return KeyUp;
        if (k == "down")                     return KeyDown;
        if (k == "left")                     return KeyLeft;
        if (k == "right")                    return KeyRight;
        if (k == "enter" || k == "return")   return '\r';
        if (k == "escape" || k == "esc")     return (char)27;
        if (k == "backspace")                return (char)8;
        if (k == "tab")                      return '\t';
        if (k == "shift")                    return KeyShift;
        if (k == "ctrl" || k == "control")   return KeyCtrl;
        return lower1(s[0]);
    }
    /// True for a key name KeyFromName understands exactly: one character, or
    /// space / up / down / left / right / enter / return / escape / esc /
    /// backspace / tab / shift / ctrl / control (any case). Any other name would
    /// silently become its first letter ("F1" -> 'f').
    static bool IsKnownKeyName(const std::string& s) {
        if (s.size() == 1) return true;
        std::string k; for (char c : s) k += (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        for (const char* n : {"space", "up", "down", "left", "right", "enter", "return", "escape", "esc",
                              "backspace", "tab", "shift", "ctrl", "control"})
            if (k == n) return true;
        return false;
    }

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
    /// Left-stick axis, each component in [-1, 1] (y up).
    static Vec2 GamepadAxis();
    /// True while a gamepad button is held. Button ids: 0=A 1=B 2=X 3=Y
    /// 4=Back 5=Start 6=L 7=R 8=Up 9=Down 10=Left 11=Right.
    static bool GetGamepadButton(int button);
    /// True only on the frame the button was first pressed.
    static bool GetGamepadButtonDown(int button);
    /// True only on the frame the button was released.
    static bool GetGamepadButtonUp(int button);

    /// Drive input from a non-terminal source (SDL window): pass the set of keys
    /// currently held this frame. Advances the down/up edge state.
    static void FeedKeys(const std::vector<char>& downKeys);
    /// Drive mouse state from a windowed runtime. `buttonMask` bit 0/1/2 =
    /// left/right/middle held. Advances the mouse down/up edge state.
    static void FeedMouse(const Vec2& position, unsigned buttonMask);
    /// Drive gamepad state: left-stick axis and a held-button bitmask (bit n =
    /// button id n). Advances the gamepad down-edge state.
    static void FeedGamepad(const Vec2& axis, unsigned buttonMask);

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
    static Vec2     s_mousePrevPos;   // cursor position last frame (for MouseDelta)
    static unsigned s_mouseCurrent;   // bitmask of held buttons this frame
    static unsigned s_mousePrevious;  // bitmask from last frame

    static Vec2     s_padAxis;
    static unsigned s_padCurrent;
    static unsigned s_padPrevious;

    static std::string s_typedText;
    static float       s_mouseWheel;
    static std::vector<Touch> s_touches;
    static bool        s_uiCaptured;
};

} // namespace okay
