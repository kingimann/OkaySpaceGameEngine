#pragma once
// A typed, composable action layer on top of the raw readers and the existing binding text.
//
//  * Value types: a button, a 1D axis, a 2D vector or a 3D vector (InputValue).
//  * Bindings are a single input ("key:space", "stick:left", "pad:rt" ... the InputBinding text) or a
//    composite: four inputs as a 2D vector, six as a 3D vector, two as a 1D axis, or modifier(s) + input (a chord).
//  * Processors reshape a value: deadzone, invert, scale, normalize, swizzle, smooth, curve, clamp. They can sit on a
//    binding and on the action, and run in the order written.
//  * Control schemes (Keyboard+Mouse, Gamepad, Touch ...) group bindings by device; the active scheme can follow the
//    device the player used last, and an ActionSet can be paired with one controller (a local player).
// Pure logic over Input: no SDL, no window. Existing InputActions is untouched; this is the richer layer next to it
// (interactions and phases come in ActionSet's second slice, the asset file and PlayerInput in the third).
#include "okay/Input/InputActions.hpp"
#include <deque>
#include <set>
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

/// One step that reshapes a value. Text form: "deadzone(0.2)", "deadzone(0.2,0.9)", "invert(x)", "invert(xy)",
/// "scale(2)", "scale(2,0.5,1)", "normalize", "swizzle(yx)", "smooth(0.1)", "curve(2)", "clamp(-1,1)".
struct Processor {
    enum class Kind { Deadzone, Invert, Scale, Normalize, Swizzle, Smooth, Curve, Clamp } kind = Kind::Scale;
    float a = 0, b = 0, c = 0;      ///< parameters (see Apply)
    int mask = 7;                   ///< Invert: bit 0 x, 1 y, 2 z. Swizzle: the order, 3 bits per output (0 x, 1 y, 2 z). Scale: how many numbers were given (1..3)
    float state[3] = {0, 0, 0};     ///< Smooth keeps the previous output here
    bool primed = false;
    /// Apply to `v` (a vector's deadzone and curve work on its length; the rest per component). `dt` is the frame time (Smooth).
    InputValue Apply(const InputValue& v, float dt);
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

struct ActionDef {
    std::string name;
    std::string map;                           ///< the action map it lives in ("" = the always-on default map)
    ValueType type = ValueType::Button;
    std::vector<BindingSpec> bindings;
    std::vector<Processor> processors;         ///< applied after the winning binding
    float pressPoint = 0.5f;                   ///< magnitude at which a button action counts as pressed
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
};

/// A group of actions that can be switched on and off together (Gameplay, UI, Vehicle). Enabled maps run from the highest
/// priority down; a map with `consume` set swallows the inputs its bindings use from every map below it.
struct ActionMap {
    std::string name;
    int priority = 0;
    bool consume = false;
    bool enabled = true;
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
    // ---- schemes
    void AddScheme(const ControlScheme& s);
    const std::vector<ControlScheme>& Schemes() const { return m_schemes; }
    bool SetScheme(const std::string& name);   ///< false for an unknown scheme
    const std::string& Scheme() const { return m_scheme; }
    /// Follow the device the player used last (with schemes defined). Off by default.
    void SetAutoSwitch(bool on) { m_auto = on; }
    bool AutoSwitch() const { return m_auto; }
    /// Pair with controller `pad` (0..3): its pad bindings read that controller and only its activity switches the scheme. -1 = no pairing (as written).
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
    InputValue Eval(const BindingSpec& b) const;
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
    mutable std::set<std::string> m_blocked;   ///< inputs swallowed by a consuming map above the one being evaluated
    std::vector<PhaseEvent> m_events;
    std::vector<std::pair<int, std::function<void(const PhaseEvent&)>>> m_subs;
    int m_nextSub = 1;
    double m_time = 0;
};

} // namespace okay
