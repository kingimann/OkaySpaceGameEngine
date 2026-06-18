#pragma once
#include "okay/Math/Vec2.hpp"
#include <unordered_map>

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

private:
    friend class Application;
    static void BeginSession();   // put terminal into raw, non-blocking mode
    static void EndSession();     // restore terminal
    static void Poll();           // sample input once per frame

    static std::unordered_map<char, bool> s_current;
    static std::unordered_map<char, bool> s_previous;
    static bool s_interactive;
};

} // namespace okay
