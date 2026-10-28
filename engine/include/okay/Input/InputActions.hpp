#pragma once
#include "okay/Math/Vec2.hpp"
#include <string>
#include <vector>

namespace okay {

/// One input a game action listens to, written as text so it saves and shows
/// easily:
///   key:space   key:w   key:shift      a keyboard key (Input::KeyFromName names)
///   mouse:left  mouse:right  mouse:middle
///   pad:a  pad:rt  pad:ls ...          a controller button (Input::GamepadButtonFromName);
///                                      pad:lt / pad:rt read the analog trigger
///   keys:w,s,a,d                       four keys as a direction (up, down, left, right)
///   stick:left  stick:right  dpad      a controller stick or the d-pad as a direction
///   mouse:delta                        mouse movement as a direction
struct InputBinding {
    enum class Kind { None, Key, Mouse, PadButton, Keys4, StickLeft, StickRight, DPad, MouseDelta };
    Kind kind = Kind::None;
    int  code = 0;          ///< key char, mouse button or PadButton
    char keys[4] = {0, 0, 0, 0};   ///< Keys4: up, down, left, right

    static InputBinding Parse(const std::string& text);
    std::string ToString() const;
    /// What to show a player: "Space", "Left Mouse", "RT", "W A S D", "Left Stick".
    std::string Label() const;
    bool IsGamepad() const { return kind == Kind::PadButton || kind == Kind::StickLeft || kind == Kind::StickRight || kind == Kind::DPad; }
    bool IsDirection() const { return kind == Kind::Keys4 || kind == Kind::StickLeft || kind == Kind::StickRight || kind == Kind::DPad || kind == Kind::MouseDelta; }
    /// How pressed it is, 0..1 (a trigger can be part way). Directions give their length.
    float Value() const;
    /// The direction it points, each component in [-1, 1] (y up). Buttons give (0, 0).
    Vec2 Vector() const;
};

/// Named game actions ("Jump", "Move", "Fire") that games read instead of raw
/// keys, so players can rebind them and play on a keyboard or a controller.
/// Each action has a list of bindings; it is down while any of them is. The
/// defaults cover a typical action game; games add their own with Define.
/// A player's rebinding is kept in Prefs ("input.<action>") and loaded at start.
class InputActions {
public:
    struct Action {
        std::string name;
        bool direction = false;            ///< a 2D action (Move, Look) rather than a button
        std::vector<InputBinding> bindings;
        std::vector<InputBinding> defaults;
        float value = 0.0f, previous = 0.0f;
        Vec2 vector;
    };

    /// Add an action, or replace an existing one's default bindings (text form).
    static void Define(const std::string& name, const std::vector<std::string>& bindings, bool direction = false);
    static bool Has(const std::string& name);
    static const std::vector<Action>& All();
    static Action* Find(const std::string& name);

    /// Sample every action once per frame, after the runtime has fed Input.
    static void Tick();

    /// Held now (pressed past half way).
    static bool Get(const std::string& name);
    /// Pressed this frame / released this frame.
    static bool Down(const std::string& name);
    static bool Up(const std::string& name);
    /// How pressed, 0..1 (an analog trigger can be part way).
    static float Value(const std::string& name);
    /// A direction action's vector, each component in [-1, 1] (y up). Sticks keep
    /// their analog tilt; keys give full steps; the length never passes 1.
    static Vec2 Vector(const std::string& name);

    /// Change the binding the player uses on one device: replaces the action's
    /// first gamepad binding when `binding` is a gamepad one, else its first
    /// keyboard / mouse binding (added when there is none). For a Keys4 binding,
    /// `part` 0..3 (up, down, left, right) changes just that key. Saved to Prefs.
    static bool Rebind(const std::string& name, const std::string& binding, int part = -1);
    /// The binding a rebind for that device would change, or nullptr.
    static const InputBinding* Current(const std::string& name, bool gamepad);
    /// Put one action (or every action, with "") back to its defaults.
    static void Reset(const std::string& name = "");

    /// Read / write the player's rebinding from / to Prefs.
    static void Load();
    static void Save();
    /// Back to the built-in default set (tests, new game).
    static void Restore();

    /// Mouse pixels that count as a full stick push for mouse:delta.
    static inline float mouseDeltaScale = 20.0f;
};

} // namespace okay
