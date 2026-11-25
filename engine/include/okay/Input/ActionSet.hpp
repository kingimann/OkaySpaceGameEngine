#pragma once
// THE action layer of the engine (one input system: Input reads the devices, this turns them into actions, one store keeps
// the player's rebinding). InputActions is the old name-based front of the same table (InputRuntime::Actions()), and the
// .okayinput asset, local players and the typed script calls read it too.
//
//  * Value types: a button, a 1D axis, a 2D vector or a 3D vector (InputValue).
//  * Bindings are a single input ("key:space", "stick:left", "pad:rt" ... the InputBinding text) or a
//    composite: four inputs as a 2D vector, six as a 3D vector, two as a 1D axis, or modifier(s) + input (a chord).
//  * Processors reshape a value: deadzone, invert, scale, normalize, swizzle, smooth, curve, clamp. They can sit on a
//    binding and on the action, and run in the order written.
//  * Control schemes (Keyboard+Mouse, Gamepad, Touch ...) group bindings by device; the active scheme can follow the
//    device the player used last, and an ActionSet can be paired with one controller (a local player).
//  * Action maps are also the input contexts of InputActions: a map can consume the inputs its bindings use or block every
//    map below it, a held chord ("chord:ctrl+z") keeps its keys from single-key bindings, an action can add up its bindings
//    (`sum`), shape its value with a response (dead zone, curve, sensitivity, invert), be read per controller (`perPlayer`),
//    take on-screen contributions (Contribute) and keeps press timing for buffering, double tap and hold to confirm.
// Pure logic over Input: no SDL, no window. Update does not allocate once warmed up (test input_bench).
#include "okay/Input/InputActions.hpp"
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace okay {

enum class ValueType { Button, Axis1D, Vector2, Vector3 };
const char* ValueTypeName(ValueType t);               // "button", "axis", "vector2", "vector3"
bool ParseValueType(const std::string& s, ValueType& out);

struct InputValue {
    float x = 0, y = 0, z = 0;
    float Length() const;                              // sqrt(x*x + y*y + z*z)
    bool operator==(const InputValue& o) const { return x == o.x && y == o.y && z == o.z; }
};

/// One step that reshapes a value. Text form: "deadzone(0.2)", "deadzone(0.2,0.9)", "axial(0.2)", "axial(0.2,0.9)", "invert(x)", "invert(xy)",
/// "scale(2)", "scale(2,0.5,1)", "normalize", "swizzle(yx)", "smooth(0.1)", "curve(2)", "clamp(-1,1)".
struct Processor {
    enum class Kind { Deadzone, Invert, Scale, Normalize, Swizzle, Smooth, Curve, Clamp, Axial } kind = Kind::Scale;
    float a = 0, b = 0, c = 0;      ///< parameters (see Apply)
    int mask = 7;                   ///< Invert: bit 0 x, 1 y, 2 z. Swizzle: the order, 3 bits per output (0 x, 1 y, 2 z). Scale: how many numbers were given (1..3)
    float state[3] = {0, 0, 0};     ///< Smooth keeps the previous output here (slot 0: the combined read)
    bool primed = false;
    float slotState[Input::kMaxGamepads][3] = {};   ///< Smooth for each controller, so one player's stick does not drag another's
    bool slotPrimed[Input::kMaxGamepads] = {};
    /// Apply to `v` (a vector's deadzone and curve work on its length; axial works per component). `dt` is the frame
    /// time (Smooth). `slot` 0 is the combined read; 1..Input::kMaxGamepads are the controllers, each with its own Smooth memory.
    InputValue Apply(const InputValue& v, float dt, int slot = 0);
    std::string ToString() const;
    static bool Parse(const std::string& text, Processor& out);
};
/// "deadzone(0.2) invert(y)" (spaces or ';' between) <-> a list. Parse returns false on any unknown step and leaves `out` unchanged.
bool ParseProcessors(const std::string& text, std::vector<Processor>& out);
std::string ProcessorsToString(const std::vector<Processor>& list);

/// Where an action is in an interaction: the player started something (Started), it completed (Performed), or it did not
/// complete (Canceled). Waiting = nothing going on.
enum class Phase { Waiting, Started, Performed, Canceled };
const char* PhaseName(Phase p);

/// How an action turns presses into events. Text form: "press", "release", "press_release", "hold(0.5)", "tap(0.2)",
/// "multitap(2,0.3,0.2)" (taps, longest gap between them, longest single tap), "repeat(0.1,0.4)" (every, first wait),
/// "chorded(Modifier)" (only counts while the action named Modifier is held).
struct Interaction {
    enum class Kind { Press, Release, PressRelease, Hold, Tap, MultiTap, Repeat, Chorded } kind = Kind::Press;
    float a = 0, b = 0, c = 0;      ///< Hold: seconds. Tap: longest press. MultiTap: count, gap, tap time. Repeat: interval, first wait
    std::string arg;                ///< Chorded: the action that must be held
    // runtime
    int stage = 0, taps = 0;
    float t = 0, gap = 0, next = 0;
    float Progress() const;         ///< Hold: 0..1 toward performed; other kinds 0
    std::string ToString() const;
    static bool Parse(const std::string& text, Interaction& out);
};

/// One phase change, reported by ActionSet::Update (also to Subscribe callbacks).
struct PhaseEvent {
    std::string action;
    Phase phase = Phase::Waiting;
    int interaction = -1;           ///< index in the action's list, -1 for the default behaviour
    InputValue value;
    float held = 0;                 ///< seconds the control has been held (hold, tap, repeat)
    bool down = true;               ///< PressRelease: false for the release half
};

struct ControlScheme {
    std::string name;
    bool keyboard = false, mouse = false, gamepad = false, touch = false;
    bool Uses(Input::Device d) const;
};

struct BindingSpec {
    enum class Kind { Single, Vector2, Vector3, Axis1D, Chord } kind = Kind::Single;
    /// Single: one path. Vector2: up, down, left, right. Vector3: up, down, left, right, forward, back.
    /// Axis1D: negative, positive. Chord: modifier(s)... then the input they unlock (the last one).
    std::vector<std::string> parts;
    std::string group;                         ///< control scheme this binding belongs to; "" = every scheme
    std::vector<std::string> original;         ///< the parts before a runtime rebind (empty = never rebound)
    std::vector<Processor> processors;
    mutable std::vector<InputBinding> leaves;  ///< parsed `parts` (filled on first use)
    mutable bool built = false;
};

/// "single", "vector2", "vector3", "axis", "chord" (the asset file and the "ovlist" override line).
const char* BindingKindName(BindingSpec::Kind k);
bool ParseBindingKind(const std::string& s, BindingSpec::Kind& k);
/// "" and "Gameplay" (any case) are the base map: always on, what InputActions calls the base context.
bool IsBaseMapName(const std::string& map);
/// A 0..1 reading through a response (InputActions::ShapeValue: for drawing the curve).
float ShapeResponse(float in, const InputActions::Response& r);

struct ActionDef {
    std::string name;
    std::string map;                           ///< the action map it lives in ("" = the always-on default map)
    ValueType type = ValueType::Button;
    std::vector<BindingSpec> bindings;
    std::vector<Processor> processors;         ///< applied after the winning binding
    float pressPoint = 0.5f;                   ///< magnitude at which a button action counts as pressed
    bool sum = false;                          ///< add every binding's value (length kept to 1) instead of taking the strongest one
    InputActions::Response response;           ///< the player's shaping (dead zone, curve, sensitivity, invert), after `processors`
    bool perPlayer = false;                    ///< also read once per controller (see `pads`)
    /// The authored bindings when the player's rebinding changed the list's shape (a binding added, or a four-key
    /// direction turned into a stick): `reshaped` is set. Otherwise the list is as authored (per-binding overrides live in
    /// BindingSpec::original).
    std::vector<BindingSpec> authored;
    bool reshaped = false;
    BindingSpec& AddBinding(const std::string& path, const std::string& group = "", const std::string& processors = "");
    BindingSpec& AddVector2(const std::string& up, const std::string& down, const std::string& left, const std::string& right,
                            const std::string& group = "", const std::string& processors = "");
    BindingSpec& AddVector3(const std::string& up, const std::string& down, const std::string& left, const std::string& right,
                            const std::string& forward, const std::string& back, const std::string& group = "", const std::string& processors = "");
    BindingSpec& AddAxis(const std::string& negative, const std::string& positive, const std::string& group = "", const std::string& processors = "");
    BindingSpec& AddChord(const std::vector<std::string>& modifiersThenInput, const std::string& group = "", const std::string& processors = "");
    std::vector<Interaction> interactions;   ///< none = the default: a button is Started + Performed when pressed and Canceled when released; a value is Started when it leaves zero, Performed each time it changes, Canceled when it returns to zero
    bool AddInteraction(const std::string& text);
    bool started = false, performed = false, canceled = false;   ///< a phase change happened this frame
    Phase phase = Phase::Waiting;
    InputValue prevValue;
    float Progress() const;         ///< the first Hold interaction's progress (a "hold to confirm" bar), else 0
    // ---- read after ActionSet::Update
    InputValue value;                          ///< processed, typed
    bool pressed = false, down = false, released = false;   ///< this frame / held / this frame
    int activeBinding = -1;                    ///< index of the binding that drove the value now, -1 none
    bool live = false;                         ///< its map ran enabled and unblocked this frame
    struct PadState { float value = 0, previous = 0; Vec2 vector; };
    PadState pads[Input::kMaxGamepads];        ///< perPlayer: the same bindings read for each controller (keyboard and mouse: pad 0)
    // ---- press timing (seconds of ActionSet::Time)
    double pressedAt = -1.0e9, prevPressedAt = -1.0e9, heldSince = -1.0;
    bool pressConsumed = false;
    bool latchActive = false, latchHeld = false;   ///< accessibility hold-to-toggle
    float contribValue = 0; Vec2 contribVector; bool contributed = false;   ///< Contribute, folded in by the next Update
    /// Magnitude now / last frame: a button's value, a vector's length (what pressPoint is compared with).
    float Magnitude() const;
    float PrevMagnitude() const;
};

/// A group of actions that can be switched on and off together (Gameplay, UI, Vehicle). Enabled maps run from the highest
/// priority down; a map with `consume` set swallows the inputs its bindings use from every map below it.
struct ActionMap {
    std::string name;
    int priority = 0;
    bool consume = false;
    bool enabled = true;
    bool block = false;    ///< while enabled, every map below it gets nothing (a modal menu: InputActions ContextMode::Block)
};

class ActionSet {
public:
    // ---- maps
    ActionMap& AddMap(const std::string& name, int priority = 0, bool consume = false, bool enabled = true);
    const std::vector<ActionMap>& Maps() const { return m_maps; }
    bool SetMapEnabled(const std::string& name, bool on);   ///< false for an unknown map
    bool MapEnabled(const std::string& name) const;
    bool SetMapPriority(const std::string& name, int priority);

    // ---- runtime rebinding (the originals are kept, so overrides can be exported, imported and reset)
    /// Replace one part of one binding (part 0 for a single binding). False for an unknown action / binding / part or an unparsable path.
    bool Rebind(const std::string& action, int binding, int part, const std::string& path);
    /// Other places that already use `path` (as "Action" or "Action.binding.part"): the same map, and only bindings that can be active together (same group or no group).
    std::vector<std::string> Conflicts(const std::string& action, int binding, int part, const std::string& path) const;
    /// Text lines "ov <action> <binding> <parts joined by ','>" for every rebound binding; Import applies them (unknown ones skipped, returns how many applied).
    std::string ExportOverrides() const;
    int ImportOverrides(const std::string& text);
    void ResetOverrides();
    /// Put one binding back to what the asset says (false when it was never rebound).
    bool ResetOverride(const std::string& action, int binding);
    /// Save / load the overrides in Prefs under "inputov.<key>" (one key per local player).
    void SaveOverrides(const std::string& key) const;
    int LoadOverrides(const std::string& key);
    /// Replace an action's whole binding list (a Controls screen that adds a binding or changes its shape). When every
    /// binding keeps the authored kind and count this is per-binding rebinds; otherwise the authored list is kept in
    /// `authored` and the override is exported as one "ovlist" line. False for an unknown action.
    bool SetBindings(const std::string& action, const std::vector<BindingSpec>& list);
    /// The authored list of an action (what Reset goes back to).
    std::vector<BindingSpec> AuthoredBindings(const ActionDef& a) const;
    /// Changes whenever an action, a binding or an override changes (readers that cache the bindings compare it).
    unsigned Revision() const { return m_revision; }
    void Touch() { ++m_revision; }
    /// Case-insensitive lookup ("jump" finds "Jump"), as InputActions names have always been.
    ActionDef* FindNoCase(const std::string& name);
    const ActionDef* FindNoCase(const std::string& name) const;
    // ---- input contexts (maps pushed on top of the others)
    /// Enable `map` above every other map (adding it when missing): consume and/or block the maps below. Re-pushing moves it up.
    void PushMap(const std::string& map, bool consume, bool block);
    /// Undo PushMap: the map goes back to its authored priority, consume and enabled state.
    void PopMap(const std::string& map);
    /// True when an enabled map above the base maps ("" and Gameplay) swallows this input (InputActions::KeyConsumed).
    bool Consumed(const InputBinding& leaf) const;
    /// Every scheme's bindings count at once (the InputActions table: the keyboard and every pad together).
    void SetAllSchemes(bool on) { m_allSchemes = on; }
    // ---- on-screen controls: added to the action by the next Update, then cleared
    void Contribute(const std::string& action, float value);
    void ContributeVector(const std::string& action, const Vec2& v);
    void ClearContributions();
    float LastDt() const { return m_lastDt; }
    // ---- schemes
    void AddScheme(const ControlScheme& s);
    const std::vector<ControlScheme>& Schemes() const { return m_schemes; }
    bool SetScheme(const std::string& name);   ///< false for an unknown scheme
    const std::string& Scheme() const { return m_scheme; }
    /// Follow the device the player used last (with schemes defined). Off by default.
    void SetAutoSwitch(bool on) { m_auto = on; }
    bool AutoSwitch() const { return m_auto; }
    /// Pair with controller `pad` (0..Input::kMaxGamepads-1): its pad bindings read that controller and only its activity switches the scheme. -1 = no pairing (as written).
    void SetPad(int pad) { m_pad = pad; }
    int Pad() const { return m_pad; }

    // ---- editing (the authoring window): remove and rename keep every reference consistent
    bool RemoveAction(const std::string& name);
    bool RenameAction(const std::string& from, const std::string& to);   ///< false when `from` is missing or `to` is taken / empty
    bool RemoveMap(const std::string& name);                             ///< its actions move to the default map
    bool RenameMap(const std::string& from, const std::string& to);      ///< actions follow
    bool RemoveScheme(const std::string& name);                          ///< bindings of that group become group-less
    bool RenameScheme(const std::string& from, const std::string& to);   ///< bindings follow

    // ---- actions
    ActionDef& Add(const std::string& name, ValueType type, const std::string& map = "");
    ActionDef* Find(const std::string& name);
    const ActionDef* Find(const std::string& name) const;
    const std::deque<ActionDef>& Actions() const { return m_actions; }

    // ---- per frame (after Input was fed)
    void Update(float dt);

    // ---- readers (0 / false for an unknown action)
    InputValue Value(const std::string& name) const;
    float Axis(const std::string& name) const { return Value(name).x; }
    Vec2 Vector(const std::string& name) const { const InputValue v = Value(name); return {v.x, v.y}; }
    bool Pressed(const std::string& name) const;
    bool Held(const std::string& name) const;
    bool Released(const std::string& name) const;

    // ---- press timing on THIS set (a local player's own actions; InputActions reads the shared table)
    /// A press of `name` happened within `window` seconds and has not been used up.
    bool Buffered(const std::string& name, float window = 0.15f) const;
    /// Use up that buffered press. False when there is none inside `window`, so a stale press cannot fire late.
    bool Consume(const std::string& name, float window = 0.15f);
    /// Use up the latest press however old it is. InputActions::Consume calls this; there is one buffer.
    bool ConsumeAny(const std::string& name);
    /// The press this frame is the second one inside `window` seconds of the previous press.
    bool DoubleTap(const std::string& name, float window = 0.3f) const;
    /// Seconds the action has been held, or 0 when it is up.
    float HeldFor(const std::string& name) const;
    /// True on the frame the hold crosses `seconds` (a "hold to confirm" that fires once).
    bool HoldReached(const std::string& name, float seconds) const;

    // ---- phases (what happened this Update)
    const std::vector<PhaseEvent>& Events() const { return m_events; }
    bool Started(const std::string& name) const;
    bool Performed(const std::string& name) const;
    bool Canceled(const std::string& name) const;
    Phase PhaseOf(const std::string& name) const;
    float Progress(const std::string& name) const;
    /// Called for every phase change during Update. Returns an id for Unsubscribe.
    int Subscribe(std::function<void(const PhaseEvent&)> fn);
    void Unsubscribe(int id);
    double Time() const { return m_time; }

    /// Does binding `b` of `a` apply under the active scheme?
    bool BindingActive(const BindingSpec& b) const;
private:
    InputValue Eval(const BindingSpec& b, int pad = -1) const;
    void EvalAction(ActionDef& a, bool enabled, float dt);
    void Emit(ActionDef& a, int idx, Phase ph, bool down = true, float held = 0);
    void RunInteractions(ActionDef& a, bool wasDown, float dt);
    std::deque<ActionDef> m_actions;
    std::vector<ControlScheme> m_schemes;
    std::string m_scheme;
    bool m_auto = false;
    int m_pad = -1;
    bool m_seenDevice = false;
    Input::Device m_lastDevice = Input::Device::Keyboard;
    int m_lastPadSeen = -1;
    std::vector<ActionMap> m_maps;
    std::vector<std::string> m_keep;           ///< override lines Import could not apply (an action of another asset): exported again as they were
    struct Pushed { std::string name; int priority; bool consume, block, enabled, added; };
    std::vector<Pushed> m_pushed;              ///< PushMap's stack (the authored state to restore)
    std::vector<int> m_claims;                 ///< inputs swallowed by consuming maps above the one being evaluated
    std::vector<int> m_chordClaims;            ///< keys of held chords
    std::vector<const ActionMap*> m_order;     ///< maps by priority (reused every Update)
    bool m_allSchemes = false;
    unsigned m_revision = 1;
    mutable unsigned m_indexRev = 0;
    mutable std::size_t m_indexSize = 0;
    mutable std::vector<std::pair<std::string, std::size_t>> m_index;   ///< lower-case name -> action, sorted
    float m_lastDt = 0;
    std::vector<PhaseEvent> m_events;
    std::vector<std::pair<int, std::function<void(const PhaseEvent&)>>> m_subs;
    int m_nextSub = 1;
    double m_time = 0;
};

} // namespace okay
