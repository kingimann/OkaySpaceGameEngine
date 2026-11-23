// The engine's own entries in the shared script-function table (okay/Scripting/ScriptApiRegistry.hpp).
// Each function here is the ONLY binding of that feature: OkayScript reaches it by the snake_case name,
// the typed language (okayscript-next / C#) and the visual-script Engine API nodes by Type.Member.
// To add one, see docs/scripting/script_api_registry.md. Written for this engine from the behaviour of
// the two copies it replaces (OkayScriptVM's builtins and Script/EngineApi); no outside code.
#include "okay/Scripting/ScriptApiRegistry.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/SaveGame.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <cmath>
#include <utility>

namespace okay::detail {

namespace {

using V = vs::VsValue;
using Args = std::vector<V>;
using T = ScriptApiType;

ScriptApiParam P(const char* name, T type) { ScriptApiParam p; p.name = name; p.type = type; return p; }
ScriptApiParam Opt(const char* name, T type, V def) { ScriptApiParam p = P(name, type); p.optional = true; p.def = std::move(def); return p; }

ScriptApiEntry Fn(const char* okay, const char* type, const char* member, std::vector<ScriptApiParam> params, T ret,
                  ScriptApiFn fn, const char* doc) {
    ScriptApiEntry e;
    e.name = okay; e.typedType = type; e.typedName = member; e.kind = ScriptApiKind::Function;
    e.params = std::move(params); e.ret = ret; e.fn = std::move(fn); e.doc = doc;
    return e;
}
ScriptApiEntry Method(const char* okay, const char* type, const char* member, std::vector<ScriptApiParam> params, T ret,
                      ScriptApiFn fn, const char* doc) {
    ScriptApiEntry e = Fn(okay, type, member, std::move(params), ret, std::move(fn), doc);
    e.kind = ScriptApiKind::Method;
    return e;
}
ScriptApiEntry Prop(const char* okay, const char* type, const char* member, T ret, ScriptApiFn fn, const char* doc) {
    ScriptApiEntry e = Fn(okay, type, member, {}, ret, std::move(fn), doc);
    e.kind = ScriptApiKind::StaticProperty;
    e.pure = true;
    return e;
}

// ---- Input ----
// Key names are Input::KeyFromName's ("space", "shift", "up", "f5", one letter or digit, any case). An
// unknown longer name acts as its first letter at run time (old OkayScript scripts rely on that); the typed
// language refuses a literal one when the script is checked.
std::string CheckKeyLiteral(int, const std::string& lit) {
    if (Input::IsKnownKeyName(lit)) return "";
    return "\"" + lit + "\" isn't a key name. Use one of: a-z, 0-9, space, enter, escape, tab, backspace, shift, "
           "ctrl, alt, up, down, left, right, f1-f12, insert, delete, home, end, pageup, pagedown";
}
char KeyArg(const Args& a) { return Input::KeyFromName(a[0].AsString()); }

std::string ActionArg(const Args& a) {   // by value: AsString() returns a temporary (ASan stack-use-after-return in CI, #1159)
    std::string n = a[0].AsString();
    if (!InputActions::Has(n))
        throw ScriptApiError("There's no input action called \"" + n + "\" (Move, Look, Jump, Sprint, Crouch, Fire, Aim, "
                             "Interact, Reload, Inventory, Pause, or one the game defined)");
    return n;
}
int PlayerArg(const Args& a) { const float p = a[1].AsFloat(); return std::isfinite(p) ? (int)p : -1; }

// ---- Physics ----
std::string NameOf(GameObject* g) { return g ? g->name : std::string("this object"); }
// The body a force call pushes: a 2D body first (what OkayScript did), else the 3D one.
template <class F2, class F3> void Push(GameObject* g, F2 on2d, F3 on3d) {
    if (!g) throw ScriptApiError("This script has no object to push");
    if (auto* b2 = g->GetComponent<Rigidbody2D>()) { on2d(*b2); return; }
    if (auto* b3 = g->GetComponent<Rigidbody3D>()) { on3d(*b3); return; }
    throw ScriptApiError("'" + g->name + "' has no Rigidbody (add one in the Inspector)");
}

// ---- Saves ----
Scene& SceneOf(GameObject* g) {
    Scene* s = g ? g->scene() : nullptr;
    if (!s) throw ScriptApiError("Saving and loading need a script running in a scene");
    return *s;
}

// ---- Audio ----
int GroupArg(const Args& a) {
    AudioGroup g;
    if (!AudioMixer::GroupFromName(a[0].AsString().c_str(), g))
        throw ScriptApiError("There's no audio group called \"" + a[0].AsString() + "\" (use Music, SFX, UI or Voice)");
    return (int)g;
}

} // namespace

void RegisterCoreScriptApis(std::vector<ScriptApiEntry>& out) {
    auto add = [&out](ScriptApiEntry e) { out.push_back(std::move(e)); };

    // ---- Input: keys (the "game" reads: a typing key is off while a text field has the keyboard) ----
    {
        ScriptApiEntry k = Fn("key", "Input", "GetKey", {P("key", T::String)}, T::Bool,
            [](GameObject*, Args& a) { const char c = KeyArg(a); return V(c != 0 && Input::GetKeyGame(c)); },
            "True while the key is held.");
        k.checkLiteral = CheckKeyLiteral;
        k.pure = true;
        add(k);
        ScriptApiEntry d = Fn("key_down", "Input", "GetKeyDown", {P("key", T::String)}, T::Bool,
            [](GameObject*, Args& a) { const char c = KeyArg(a); return V(c != 0 && Input::GetKeyDownGame(c)); },
            "True on the frame the key is pressed.");
        d.checkLiteral = CheckKeyLiteral;
        d.pure = true;
        add(d);
        ScriptApiEntry u = Fn("key_up", "Input", "GetKeyUp", {P("key", T::String)}, T::Bool,
            [](GameObject*, Args& a) { const char c = KeyArg(a); return V(c != 0 && Input::GetKeyUp(c)); },
            "True on the frame the key is released.");
        u.checkLiteral = CheckKeyLiteral;
        u.pure = true;
        add(u);
    }
    // ---- Input: game actions (InputActions), after the player's own rebinding; player -1 = any ----
    {
        auto action = [&](const char* okay, const char* member, bool (*any)(const std::string&),
                          bool (*one)(const std::string&, int), const char* doc) {
            ScriptApiEntry e = Fn(okay, "Input", member, {P("action", T::String), Opt("player", T::Int, V(-1.0f))}, T::Bool,
                [any, one](GameObject*, Args& a) {
                    const std::string& n = ActionArg(a);
                    const int p = PlayerArg(a);
                    return V(p < 0 ? any(n) : one(n, p));
                }, doc);
            e.typedMinParams = 1;
            e.pure = true;
            add(e);
        };
        action("action", "GetButton", &InputActions::Get, &InputActions::Get,
               "True while the action is held (\"Jump\", \"Fire\"...), on keyboard, mouse or controller. "
               "A player number (0-3) reads one controller's action.");
        action("action_down", "GetButtonDown", &InputActions::Down, &InputActions::Down, "True on the frame the action is pressed.");
        action("action_up", "GetButtonUp", &InputActions::Up, &InputActions::Up, "True on the frame the action is released.");
        ScriptApiEntry v = Fn("action_value", "Input", "GetActionValue", {P("action", T::String), Opt("player", T::Int, V(-1.0f))},
            T::Float,
            [](GameObject*, Args& a) {
                const std::string& n = ActionArg(a);
                const int p = PlayerArg(a);
                return V(p < 0 ? InputActions::Value(n) : InputActions::Value(n, p));
            }, "How far the action is pressed, 0 to 1 (a trigger can be part way).");
        v.typedMinParams = 1;
        v.pure = true;
        add(v);
    }
    // ---- Physics: forces on this object's body (2D or 3D) ----
    add(Method("add_force", "Rigidbody", "AddForce", {P("force", T::Vector3)}, T::Void,
        [](GameObject* g, Args& a) {
            const Vec3 f = a[0].AsVec3();
            Push(g, [&](Rigidbody2D& b) { b.AddForce(Vec2{f.x, f.y}); }, [&](Rigidbody3D& b) { b.AddForce(f); });
            return V();
        }, "Push it, over the next physics step (heavier bodies move less)."));
    add(Method("add_impulse", "Rigidbody", "AddImpulse", {P("impulse", T::Vector3)}, T::Void,
        [](GameObject* g, Args& a) {
            const Vec3 f = a[0].AsVec3();
            Push(g, [&](Rigidbody2D& b) { b.AddImpulse(Vec2{f.x, f.y}); }, [&](Rigidbody3D& b) { b.AddImpulse(f); });
            return V();
        }, "Kick it at once (a jump, a hit)."));
    add(Method("add_torque", "Rigidbody", "AddTorque", {P("torque", T::Vector3)}, T::Void,
        [](GameObject* g, Args& a) {
            const Vec3 t = a[0].AsVec3();
            Push(g, [&](Rigidbody2D& b) { b.AddTorque(t.z); }, [&](Rigidbody3D& b) { b.AddTorque(t); });
            return V();
        }, "Twist it around X / Y / Z (a 2D body turns by the Z part)."));
    // ---- Whole-game saves (SaveGame). OkayScript's slot defaults to "save1" and its yes / no answers are 1 / 0. ----
    {
        auto save = [&](ScriptApiEntry e) { e.boolAsNumber = e.ret == T::Bool; add(std::move(e)); };
        save(Fn("save_game", "SaveGame", "Save", {Opt("slot", T::String, V("save1")), Opt("label", T::String, V(""))}, T::Bool,
            [](GameObject* g, Args& a) {
                std::string err;
                const bool ok = SaveGame::Save(SceneOf(g), a[0].AsString(), a[1].AsString(), &err);
                if (!ok) Log::Warning("[script] save to slot '", a[0].AsString(), "' failed: ", err);
                return V(ok);
            }, "Save the game to a slot (\"1\", \"quick\"...) with a label for the load menu. False if it couldn't."));
        save(Fn("load_game", "SaveGame", "Load", {Opt("slot", T::String, V("save1"))}, T::Bool,
            [](GameObject* g, Args& a) {
                std::string err;
                const bool ok = SaveGame::Load(SceneOf(g), a[0].AsString(), &err);
                if (!ok) Log::Warning("[script] load of slot '", a[0].AsString(), "' failed: ", err);
                return V(ok);
            }, "Load a slot at the end of this frame. False if there's no save there."));
        ScriptApiEntry has = Fn("has_save", "SaveGame", "Exists", {Opt("slot", T::String, V("save1"))}, T::Bool,
            [](GameObject*, Args& a) { return V(SaveGame::Exists(a[0].AsString())); }, "True if the slot has a save.");
        has.aliases = {"save_exists"};
        has.pure = true;
        save(has);
        save(Fn("delete_save", "SaveGame", "Delete", {Opt("slot", T::String, V("save1"))}, T::Bool,
            [](GameObject*, Args& a) { return V(SaveGame::Delete(a[0].AsString())); }, "Delete the save in a slot."));
        ScriptApiEntry label = Fn("save_label", "SaveGame", "Label", {Opt("slot", T::String, V("save1"))}, T::String,
            [](GameObject*, Args& a) { return V(SaveGame::Info(a[0].AsString()).label); },
            "The label a slot was saved with (\"\" if empty).");
        label.pure = true;
        save(label);
        save(Prop("play_time", "SaveGame", "playTime", T::Float, [](GameObject*, Args&) { return V(SaveGame::PlayTime()); },
            "Seconds played, counting the time in the save that was loaded."));
    }
    // ---- Audio mixer groups ----
    add(Fn("set_group_volume", "Audio", "SetGroupVolume", {P("group", T::String), P("volume", T::Float)}, T::Void,
        [](GameObject*, Args& a) { AudioMixer::groupVolume[GroupArg(a)] = Mathf::Clamp(a[1].AsFloat(), 0.0f, 1.0f); return V(); },
        "Set one group's volume, 0 to 1 (\"Music\", \"SFX\", \"UI\" or \"Voice\")."));
    {
        ScriptApiEntry gv = Fn("group_volume", "Audio", "GetGroupVolume", {P("group", T::String)}, T::Float,
            [](GameObject*, Args& a) { return V(AudioMixer::groupVolume[GroupArg(a)]); }, "One group's volume.");
        gv.pure = true;
        add(gv);
    }
    // ---- Time ----
    add(Prop("time", "Time", "time", T::Float, [](GameObject*, Args&) { return V(Time::ElapsedTime()); },
        "Seconds since the game started."));
    add(Prop("frame_count", "Time", "frameCount", T::Int, [](GameObject*, Args&) { return V((float)Time::FrameCount()); },
        "Frames since the game started."));
}

} // namespace okay::detail
