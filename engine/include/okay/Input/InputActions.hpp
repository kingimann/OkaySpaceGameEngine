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
/// A controller binding can name which controller: "pad:a@1", "stick:left@2",
/// "dpad@1" (player 1 = second controller). No suffix means the first one.
struct InputBinding {
    enum class Kind { None, Key, Mouse, PadButton, Keys4, StickLeft, StickRight, DPad, MouseDelta };
    Kind kind = Kind::None;
    int  code = 0;          ///< key char, mouse button or PadButton
    int  player = 0;        ///< controller index for pad / stick / dpad bindings
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
        std::string context;               ///< input context it belongs to; "" = the base (gameplay) context
        float threshold = 0.5f;            ///< value at which a button action counts as held
    };

    /// What a context does to the contexts beneath it on the stack while it is active.
    enum class ContextMode {
        Passthrough,   ///< lower contexts keep every input
        Consume,       ///< inputs bound by this context's actions are swallowed from lower ones
        Block,         ///< lower contexts get nothing at all (a modal menu)
    };

    // ---- Input contexts ----------------------------------------------------------
    // Actions can live in a named context ("UI", "Dialogue", "Vehicle"). The base
    // context ("" or "Gameplay") is always active at the bottom. Pushing a context
    // makes its actions live and, per its mode, stops the contexts below from seeing
    // the same keys or buttons: with a "UI" context pushed in Consume mode, Space bound
    // to a UI action no longer also fires Jump. Raw Input.GetKey is NOT affected.
    static void DefineIn(const std::string& context, const std::string& name,
                         const std::vector<std::string>& bindings, bool direction = false);
    /// Move an existing action into a context ("" for the base one).
    static bool SetContext(const std::string& action, const std::string& context);
    /// Make `context` the topmost active context (re-pushing moves it to the top).
    static void PushContext(const std::string& context, ContextMode mode = ContextMode::Consume);
    /// Deactivate a context wherever it is in the stack.
    static void PopContext(const std::string& context);
    static void ClearContexts();
    static bool ContextActive(const std::string& context);
    /// The topmost pushed context, or "" when only the base one is active.
    static std::string TopContext();
    /// True when a context above the base one swallows this key / mouse button /
    /// controller button, so a script reading raw input can honour it too.
    static bool KeyConsumed(char key);
    static bool PadButtonConsumed(int button, int player = 0);
    /// Value at which a button action counts as held / pressed (default 0.5).
    static void SetThreshold(const std::string& action, float threshold);

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
