#pragma once
// On-screen controls for a touch screen: a virtual joystick (fixed or floating),
// a directional pad and a touch button. Each one names an input action. While a
// finger is on it, that action reads the control the same way it reads a key or
// a controller, so a game written for a keyboard or a pad plays unchanged.
//
// The player calls TouchControls::Sample once a frame after the fingers are fed
// and before InputActions::Tick. Set onlyOnTouch to draw and listen only when a
// touch screen is present (Input::TouchScreen).
#include "okay/Scene/Component.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Input/Input.hpp"
#include <string>

namespace okay {

class Scene;

/// True while this control should be drawn and should listen. A control with
/// onlyOnTouch waits until a touch screen has been seen.
inline bool TouchControlVisible(bool enabled, bool onlyOnTouch) {
    return enabled && (!onlyOnTouch || Input::TouchScreen());
}

/// Where the control sits on the current canvas, in pixels (origin top-left).
inline Vec2 TouchControlOrigin(UIAnchor anchor, const Vec2& position, const Vec2& size) {
    return ResolveAnchor(anchor, position, size);
}
inline bool TouchControlContains(UIAnchor anchor, const Vec2& position, const Vec2& size, const Vec2& p) {
    const Vec2 o = TouchControlOrigin(anchor, position, size);
    return p.x >= o.x && p.y >= o.y && p.x < o.x + size.x && p.y < o.y + size.y;
}

/// A thumb stick. `floating` leaves it at its anchored rect until a finger lands
/// inside that rect, then the stick appears under the finger. `value` is the
/// stick this frame, each component in -1..1 with y up (the same as a Move action).
class VirtualJoystick final : public Behaviour {
public:
    Vec2 position{40.0f, -40.0f};
    Vec2 size{160.0f, 160.0f};
    UIAnchor anchor = UIAnchor::BottomLeft;
    std::string action = "Move";
    bool floating = false;
    float deadZone = 0.15f;
    bool onlyOnTouch = true;
    /// Drawn fainter until a finger is on it, so a floating stick does not cover the game.
    bool fadeWhenIdle = true;
    float knobScale = 0.42f;   ///< knob diameter as a fraction of the stick
    Color baseColor = Color::FromBytes(48, 64, 102, 210);
    Color knobColor = Color::FromBytes(66, 150, 250, 230);

    Vec2 value;                 ///< runtime, y up
    Vec2 knob;                  ///< runtime, pixels from the centre, y down (for drawing)
    Vec2 floatCenter;           ///< runtime, screen centre while a floating stick is held
    bool tracking = false;
    bool hasFinger = false;
    long long fingerId = 0;
    bool mouseHeld = false;

    bool Shows() const { return TouchControlVisible(enabled, onlyOnTouch); }
    void Release() { tracking = false; hasFinger = false; mouseHeld = false; value = {}; knob = {}; }
};

/// Four (or eight) directions. `value` is -1, 0 or 1 on each axis, y up, like the
/// keyboard side of a Move action. Diagonals are on unless `diagonals` is false.
class TouchDPad final : public Behaviour {
public:
    Vec2 position{220.0f, -40.0f};
    Vec2 size{150.0f, 150.0f};
    UIAnchor anchor = UIAnchor::BottomLeft;
    std::string action = "Move";
    float deadZone = 0.28f;
    bool diagonals = true;
    bool onlyOnTouch = true;
    Color color = Color::FromBytes(20, 24, 36, 160);
    Color pressedColor = Color::FromBytes(66, 150, 250, 230);

    Vec2 value;
    bool tracking = false;
    bool hasFinger = false;
    long long fingerId = 0;
    bool mouseHeld = false;

    bool Shows() const { return TouchControlVisible(enabled, onlyOnTouch); }
    void Release() { tracking = false; hasFinger = false; mouseHeld = false; value = {}; }
};

/// A button that holds an action while the finger is down (Jump, Fire, ...).
class TouchButton final : public Behaviour {
public:
    Vec2 position{-40.0f, -40.0f};
    Vec2 size{120.0f, 120.0f};
    UIAnchor anchor = UIAnchor::BottomRight;
    std::string action = "Jump";
    std::string label = "Jump";
    bool onlyOnTouch = true;
    Color color = Color::FromBytes(66, 150, 250, 210);
    Color pressedColor = Color::FromBytes(40, 110, 210, 240);
    Color textColor = Color::White;

    bool pressed = false;
    bool tracking = false;
    bool hasFinger = false;
    long long fingerId = 0;
    bool mouseHeld = false;

    bool Shows() const { return TouchControlVisible(enabled, onlyOnTouch); }
    void Release() { tracking = false; pressed = false; hasFinger = false; mouseHeld = false; }
};

/// Read every touch control in the scene and add what they are doing to the
/// action map. Call after fingers (and the mouse) are fed, and before
/// InputActions::Tick. Fingers that land on a control are claimed so they are
/// not also read as a swipe or a pinch.
namespace TouchControls {
void Sample(Scene& scene);
/// Let go of every control (leaving Play, or the touch list was cleared).
void ReleaseAll(Scene& scene);
}

} // namespace okay
