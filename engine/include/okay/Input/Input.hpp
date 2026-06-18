#pragma once
#include "okay/Math/Vec2.hpp"
#include <unordered_map>
#include <vector>

namespace okay {

/// Polled keyboard state, mirroring Unity's static `Input` API. On a terminal
/// it reads non-blocking keystrokes; in non-interactive environments it simply
/// reports no input, so games still run headlessly.
class Input {
public:
    /// True while the key is held down.
    static bool GetKey(char key);
    /// True only on the frame the key was first pressed.
    static bool GetKeyDown(char key);
    /// True only on the frame the key was released.
    static bool GetKeyUp(char key);

    /// Composite WASD / arrow axis in the range [-1, 1] per component.
    static Vec2 AxisWASD();

    // ---- Mouse (driven by FeedMouse from the windowed runtime) ----------
    /// Cursor position in pixels (origin top-left of the window).
    static Vec2 MousePosition();
    /// True while a mouse button is held (0 = left, 1 = right, 2 = middle).
    static bool GetMouseButton(int button);
    /// True only on the frame the button was first pressed.
    static bool GetMouseButtonDown(int button);
    /// True only on the frame the button was released.
    static bool GetMouseButtonUp(int button);

    /// Drive input from a non-terminal source (SDL window): pass the set of keys
    /// currently held this frame. Advances the down/up edge state.
    static void FeedKeys(const std::vector<char>& downKeys);
    /// Drive mouse state from a windowed runtime. `buttonMask` bit 0/1/2 =
    /// left/right/middle held. Advances the mouse down/up edge state.
    static void FeedMouse(const Vec2& position, unsigned buttonMask);

private:
    friend class Application;
    static void BeginSession();   // put terminal into raw, non-blocking mode
    static void EndSession();     // restore terminal
    static void Poll();           // sample input once per frame

    static std::unordered_map<char, bool> s_current;
    static std::unordered_map<char, bool> s_previous;
    static bool s_interactive;

    static Vec2     s_mousePos;
    static unsigned s_mouseCurrent;   // bitmask of held buttons this frame
    static unsigned s_mousePrevious;  // bitmask from last frame
};

} // namespace okay
