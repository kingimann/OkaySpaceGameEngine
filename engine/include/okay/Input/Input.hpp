#pragma once
#include "okay/Math/Vec2.hpp"
#include <unordered_map>
#include <vector>
#include <string>

namespace okay {

/// Polled keyboard state, mirroring Unity's static `Input` API. On a terminal
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

    /// True while the key is held down.
    static bool GetKey(char key);
    /// True only on the frame the key was first pressed.
    static bool GetKeyDown(char key);
    /// True only on the frame the key was released.
    static bool GetKeyUp(char key);

    /// Composite WASD / arrow axis in the range [-1, 1] per component.
    static Vec2 AxisWASD();

    /// True if ANY key is held this frame (Unity's Input.anyKey) — "press any key".
    static bool AnyKey();
    /// True only on the frame a new key was first pressed (Input.anyKeyDown).
    static bool AnyKeyDown();

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
    /// Clear the per-frame typed text AND mouse-wheel delta — call once at the
    /// start of each frame, before polling events.
    static void ClearTypedText();

    // ---- Mouse wheel (driven by FeedMouseWheel from the windowed runtime) -----
    /// This frame's wheel delta (notches; +up / -down). 0 when idle.
    static float MouseWheel();
    /// Add to this frame's wheel delta (from the OS scroll event).
    static void FeedMouseWheel(float delta);

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
    static bool        s_uiCaptured;
};

} // namespace okay
