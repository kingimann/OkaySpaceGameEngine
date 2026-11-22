#pragma once
#include "okay/Math/Vec2.hpp"
#include "okay/Input/Input.hpp"
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
///   chord:ctrl+z  chord:shift+w        two to four keys that must all be held together
///   wheel:up  wheel:down               one notch of the mouse wheel this frame
/// A controller binding can name which controller: "pad:a@1", "stick:left@2",
/// "dpad@1" (player 1 = second controller). No suffix means the first one.
struct InputBinding {
    enum class Kind { None, Key, Mouse, PadButton, Keys4, StickLeft, StickRight, DPad, MouseDelta, Chord, Wheel };
    Kind kind = Kind::None;
    int  code = 0;          ///< key char, mouse button or PadButton
    int  player = 0;        ///< controller index for pad / stick / dpad bindings
    char keys[4] = {0, 0, 0, 0};   ///< Keys4: up, down, left, right. Chord: the keys, 0 ends the list. Wheel: code is +1 (up) or -1 (down)

    static InputBinding Parse(const std::string& text);
    std::string ToString() const;
    /// What to show a player: "Space", "Left Mouse", "RT", "W A S D", "Left Stick".
    std::string Label() const;
    /// The same with the controller pictures of `family` ("Cross" and "R2" instead of "A" and "RT").
    std::string Label(Input::PadFamily family) const;
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

        // ---- Response: how the raw reading becomes the value games read. Neutral by default
        // (no dead zone, linear, 1x, not inverted), so old actions read exactly as before.
        float deadzone = 0.0f;             ///< readings under this are 0, the rest is stretched back to 0..1
        float curve = 1.0f;                ///< out = in ^ curve (2 = fine aim near the centre)
        float sensitivity = 1.0f;          ///< multiplier after the curve (a direction may pass 1: look speed)
        bool invertX = false, invertY = false;   ///< flip a direction's axes (invert look)
        bool perPlayer = false;            ///< also evaluated for each controller (see Get(name, player))

        // ---- Bookkeeping filled by Tick (do not set)
        std::string key, ctxKey;           ///< lower-case name and context, for quick lookups
        double pressedAt = -1.0e9, prevPressedAt = -1.0e9, heldSince = -1.0;
        bool pressConsumed = false, pressedNow = false;
        struct PlayerState { float value = 0, previous = 0; Vec2 vector; };
        PlayerState players[Input::kMaxGamepads];
        bool latchActive = false, latchHeld = false;   ///< hold-to-toggle state
    };

    /// How an action's raw reading is shaped. Saved with the player's settings ("inputresp.<action>").
    struct Response { float deadzone = 0.0f, curve = 1.0f, sensitivity = 1.0f; bool invertX = false, invertY = false; };
    static bool SetResponse(const std::string& action, const Response& r);   ///< clamps; false for an unknown action
    static Response GetResponse(const std::string& action);
    /// The curve itself, for drawing it: `in` 0..1 through the response (invert does not change a magnitude).
    static float ShapeValue(float in, const Response& r);

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
    /// A contribution posted by an on-screen control is folded in here, then cleared.
    static void Tick();
    /// Tick with a known frame time (tests, replays, hosts that already measure it).
    static void Tick(float dt);
    /// Use the original, slower evaluation (OKAY_OLD_INPUT=1 turns it on at start): same
    /// results, kept to prove that and to measure the speed-up.
    static void SetLegacy(bool on);
    static bool Legacy();

    /// Add to the action this frame, on top of its keys and pads (a touch button,
    /// a virtual stick). Call before Tick. A direction action's vector is summed
    /// and then kept to length 1; a button action keeps the stronger press.
    /// Cleared at the end of Tick, and by ClearContributions.
    static void Contribute(const std::string& action, float value);
    static void ContributeVector(const std::string& action, const Vec2& vector);
    static void ClearContributions();

    /// Held now (pressed past half way).
    static bool Get(const std::string& name);
    /// Pressed this frame / released this frame.
    static bool Down(const std::string& name);
    static bool Up(const std::string& name);

    // ---- Several local players. An action marked per player is also read for each
    // controller 0..3: its controller bindings follow the player, its keyboard and mouse
    // bindings belong to player 0. So "Jump" works for the keyboard player and for every
    // pad at once, and a game reads Get("Jump", 2) for the third controller.
    static void SetPerPlayer(const std::string& action, bool on = true);
    static bool Get(const std::string& name, int player);
    static bool Down(const std::string& name, int player);
    static bool Up(const std::string& name, int player);
    static float Value(const std::string& name, int player);
    static Vec2 Vector(const std::string& name, int player);

    // ---- Timing (buffering, double tap, hold to confirm). Time is the seconds passed to
    // Tick(dt); Tick() measures it from the clock.
    /// A press happened within the last `window` seconds and nobody consumed it yet: jump
    /// buffering ("pressed Jump a moment before landing"). Consume it when you act on it.
    static bool Buffered(const std::string& name, float window = 0.15f);
    /// Use up the buffered press so it does not fire twice. True when there was one.
    static bool Consume(const std::string& name);
    /// Pressed this frame, and also pressed less than `window` seconds before that.
    static bool DoubleTap(const std::string& name, float window = 0.3f);
    /// How long the action has been held, in seconds (0 when it is not held).
    static float HeldFor(const std::string& name);
    /// 0..1: how far through a "hold to confirm" of `seconds`.
    static float HoldProgress(const std::string& name, float seconds);
    /// True on the frame a hold of `seconds` is first reached.
    static bool HoldReached(const std::string& name, float seconds);
    /// The time of the last Tick, in seconds.
    static double Now();
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
    // Conflict checks and swapping live in RemapModel (RemapConflicts, RemapBind with RemapResolve::Swap), which a
    // Controls screen already uses; they understand the chord and wheel bindings too.
    /// All bindings as one text block ("okayinput 1" then one line per action) and back, for
    /// per-player profiles and sharing. Unknown actions are skipped on import.
    static std::string ExportBindings();
    static bool ImportBindings(const std::string& text);
    /// Named profiles kept in Prefs ("inputprofile.<name>"): one per local player or preset.
    static bool SaveProfile(const std::string& profile);
    static bool LoadProfile(const std::string& profile);
    static bool HasProfile(const std::string& profile);
    static void DeleteProfile(const std::string& profile);
    /// One-handed presets: "left" puts movement on the arrow keys and actions near them,
    /// "right" keeps everything on the right half (I J K L and the numpad side).
    static bool ApplyPreset(const std::string& preset);

    // ---- Built-in controllers (first / third person, character, top-down, weapons, pickups).
    // Each keeps its own key field (sprintKey, crouchKey, fireButton ...). While the player has
    // not rebound the matching action, these read exactly that key, as before. Once the action
    // is rebound (Controls screen, Rebind, a profile or preset), a field still set to the
    // action's default key follows the action instead, so the new binding reaches the
    // controller and the old key stops working. A field a designer set to some other key stays
    // that key. Reading the action needs Tick to have run this frame (the player and editor do).
    /// The player's bindings for the action differ from its defaults.
    static bool Rebound(const std::string& name);
    /// `key` held / pressed this frame, or the action's, as described above. 0 = never.
    /// `stockKey` is the controller's own shipped key for an action that starts with no default binding of its own
    /// (Prone, LeanLeft, LeanRight, Dash: their keys are shared with Interact and Jump or often taken by a game's own actions). Once that action is bound, a field still
    /// on `stockKey` follows it too.
    /// An action in a context that is not pushed (the "Vehicle" driving actions) is read straight from its bindings.
    static bool ControlKey(const std::string& action, char key, char stockKey = 0);
    static bool ControlKeyDown(const std::string& action, char key, char stockKey = 0);
    /// One key of a four-key direction (part 0 up, 1 down, 2 left, 3 right), pressed this frame: `key` until the
    /// action is rebound; then, if `key` is that part of one of its default four-key bindings, the same part of its
    /// current four-key bindings (the 2.5D platformer's layer keys follow "Move" up and down).
    static bool ControlDirKeyDown(const std::string& action, int part, char key);
    /// ControlKeyDown for the key that opens and closes a menu: read straight from the devices, so it still works
    /// while that menu's own Block context swallows every action (the Pause Menu and "Pause").
    static bool MenuKeyDown(const std::string& action, char key);
    /// Input::AxisWASD until one of the four button actions is rebound; then y = up - down, x = right - left from
    /// those actions (the Vehicle Controller's Throttle, Brake, SteerLeft, SteerRight).
    static Vec2 ControlAxis(const std::string& up, const std::string& down, const std::string& left, const std::string& right);
    /// The action's bindings read straight from the devices this frame, ignoring contexts and Tick.
    static bool DirectHeld(const std::string& name);
    static bool DirectDown(const std::string& name);
    /// The same for a mouse button (0 left, 1 right, 2 middle). Negative = never.
    static bool ControlMouse(const std::string& action, int button);
    static bool ControlMouseDown(const std::string& action, int button);
    /// Input::AxisWASD until "Move" is rebound, then the "Move" action's vector.
    static Vec2 ControlMove();

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
