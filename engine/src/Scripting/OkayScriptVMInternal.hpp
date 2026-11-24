#pragma once
// Private to the OkayScriptVM*.cpp files: the script VM's value helpers, Runtime, the builtin table context and
// OkayScriptVM::Impl, shared by the topic files (OkayScriptVM_<Topic>.cpp) that register the builtin commands.
#include "okay/Core/SafeCast.hpp"
#include "okay/Scene/UIGallery.hpp"
#include "okay/Scene/UIStyleLib.hpp"
#include "okay/Components/UIWidgetsMore.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scripting/ScriptApiRegistry.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Components/UIAnimation.hpp"
#include "okay/Components/Ragdoll.hpp"
#include "okay/Components/ActiveRagdoll.hpp"
#include "okay/Components/TweenAnimation.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/Clipboard.hpp"
#include "okay/Core/DisplaySettings.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Input/Cursor.hpp"
#include "okay/Input/Gestures.hpp"
#include "okay/Scripting/ScriptUI.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Layers.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/SceneManager.hpp"
#include "okay/Physics/Physics2D.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Components/VehicleSeat.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/CinemachineBrain.hpp"
#include "okay/Components/VirtualCamera.hpp"
#include "okay/Components/Stats.hpp"
#include "okay/Components/SurvivalStats.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Render/Lighting.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/PlayerView.hpp"
#include "okay/Components/PlayerController.hpp"
#include "okay/Components/UITheme.hpp"          // grok/ui-theme
#include "okay/Components/UINavigation.hpp"     // SetUIFocus
#include "okay/Components/UIDamageNumbers.hpp"
#include "okay/Components/UIDialogueBox.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Script/AnimIkTools.hpp"
#include "okay/Components/AnimControl.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Components/BlendShapes.hpp"
#include "okay/Components/LimbIK.hpp"
#include "okay/Components/ChainIK.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Components/AimIK.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/AimOffset.hpp"
#include "okay/Components/PoseCapture.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/SpriteAnimator.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/SessionTicket.hpp"
#include "okay/Components/NetValidator.hpp"
#include "okay/Platform/GameIpc.hpp"
#include "okay/Net/ServerBrowser.hpp"
#include "okay/Net/MatchResults.hpp"
#include "okay/Net/Matchmaker.hpp"
#include "okay/Components/NetworkSync.hpp"
#include "okay/Components/NetworkObject.hpp"
#include "okay/Net/Matchmaking.hpp"
#include "okay/Platform/Steam/Steam.hpp"
#include "okay/Platform/PlayFab/PlayFab.hpp"
#include "okay/Platform/Account/Account.hpp"
#include "okay/Components/UIImage.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/MusicPlayer.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIColorPicker.hpp"   // grok/ui-color
#include "okay/Components/UIRadialProgress.hpp"   // grok/ui-hud
#include "okay/Components/Crosshair.hpp"          // grok/ui-hud
#include "okay/Components/UISlotGrid.hpp"         // grok/ui-hud
#include "okay/Components/UIToast.hpp"    // grok/ui-toast
#include "okay/Components/UIModal.hpp"    // grok/ui-toast
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/UITabs.hpp"
#include "okay/Components/UIScrollView.hpp"   // grok/ui-controls
#include "okay/Components/UITooltip.hpp"
#include "okay/Components/Tilemap.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/GridPathAgent2D.hpp"
#include "okay/Components/GridCostLayer.hpp"
#include "okay/Components/GridObstacle2D.hpp"
#include "okay/AI/Navigation.hpp"
#include "okay/Components/BehaviorTreeRunner.hpp"
#include "okay/Components/Spawner.hpp"
#include "okay/Components/BuildMode.hpp"
#include "okay/Components/LogicLinks.hpp"
#include "okay/Components/Crafting.hpp"
#include "okay/Components/MeleeAttacker.hpp"
#include "okay/Components/RangedWeapon.hpp"
#include "okay/Components/TopDownShooterController.hpp"
#include "okay/Combat/WeaponDefinition.hpp"
#include "okay/Components/Equipment.hpp"
#include "okay/Components/GridInventory.hpp"
#include "okay/Components/InventoryUI.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Inventory/LootTable.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Localization.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputRecorder.hpp"
#include "okay/Input/Haptics.hpp"
#include "okay/Input/RemapModel.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Input/PadProfiles.hpp"
#include "okay/Input/InputRuntime.hpp"
#include "okay/Components/LocalPlayerInput.hpp"
#include "okay/Input/InputAsset.hpp"
#include "okay/Core/SaveGame.hpp"
#include "okay/Core/GameSaves.hpp"
#include "okay/Core/SaveFramework.hpp"
#include "okay/Components/SequencePlayer.hpp"
#include "okay/Components/DialogueRunner.hpp"   // dialogue / story / quest builtins
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/StoryState.hpp"
#include "okay/Render/Atmosphere.hpp"          // post_set / fog_set / weather_set
#include "okay/Components/WindZone.hpp"        // wind_speed / wind_set / wind_gust (docs/gameplay/wind.md)
#include "okay/Components/DayNightCycle.hpp"   // time_of_day
#include "okay/Components/Spline.hpp"
#include "okay/Components/LineRenderer.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Core/Random.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Core/DataAsset.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Math/Easing.hpp"
#include "okayscript/Lexer.hpp"      // Tok, Token, ScriptError, Lexer (the engine-independent OkayScript core)
#include "okayscript/Parser.hpp"     // the OkayScript parser and syntax tree
#include "okayscript/Runtime.hpp"    // the interpreter core (a template over the value type)
#include "okayscript/detail/Support.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>


#include "okay/Core/ScriptPaths.hpp"   // ScriptPathOk: the one gate for script-supplied file names

namespace okay {
// A file that used to live beside the game (before saves moved to the save
// folder): read it from there if the new place has nothing yet.
inline std::string ScriptReadPath(const std::string& name) {
    std::string p = Save::Resolve(name);
    if (p != name && !std::ifstream(p).good() && std::ifstream(name).good()) return name;
    return p;
}
} // namespace okay

namespace okay {
namespace script_vm {

using Value = vs::VsValue;

inline FirstPersonHands::HandSide* FpSide(FirstPersonHands* h, const std::string& p, std::string& rest) {
    if (!h) return nullptr;
    if (p.rfind("right", 0) == 0) { rest = p.substr(5); return &h->right; }
    if (p.rfind("left", 0) == 0) { rest = p.substr(4); return &h->left; }
    return nullptr;
}
inline Value FpHandsGet(FirstPersonHands* h, const std::string& p) {
    if (!h) return Value{};
    if (p == "showWhenEmpty") return Value{h->showWhenEmpty};
    if (p == "showWhileRunning") return Value{h->showWhileRunning};
    if (p == "bobAmount") return Value{h->bobAmount};
    if (p == "swayAmount") return Value{h->swayAmount};
    if (p == "punchAmount") return Value{h->punchAmount};
    if (p == "handFov") return Value{h->handFov};
    if (p == "itemScale") return Value{h->itemScale};
    if (p == "holdMode") return Value{(float)h->holdMode};
    if (p == "itemOffset") return Value{h->itemOffset};
    if (p == "itemRotation") return Value{h->itemRotation};
    if (p == "othersItemOffset") return Value{h->othersItemOffset};
    if (p == "othersItemRotation") return Value{h->othersItemRotation};
    if (p == "othersArmLift") return Value{h->othersArmLift};
    if (p == "othersArmForward") return Value{h->othersArmForward};
    std::string rest;
    if (FirstPersonHands::HandSide* s = FpSide(h, p, rest)) {
        if (rest == "Enabled") return Value{s->enabled};
        if (rest == "Position") return Value{s->position};
        if (rest == "Rotation") return Value{s->rotation};
        if (rest == "UniformScale") return Value{s->uniformScale};
        if (rest == "Scale") return Value{s->scale};
        if (rest == "Size") return Value{s->size};
        if (rest == "UseColour" || rest == "UseColor") return Value{s->useColour};
        if (rest == "Colour" || rest == "Color") return Value{Vec3{s->colour.r, s->colour.g, s->colour.b}};
        if (rest == "Sleeve") return Value{s->sleeve};
        if (rest == "SleeveLength") return Value{s->sleeveLength};
        if (rest == "SleeveColour" || rest == "SleeveColor")
            return Value{Vec3{s->sleeveColour.r, s->sleeveColour.g, s->sleeveColour.b}};
    }
    return Value{};
}
inline bool FpHandsSet(FirstPersonHands* h, const std::string& p, const Value& v) {
    if (!h) return false;
    auto col = [&](Color& c) {
        if (!v.IsVec3()) return;
        Vec3 q = v.AsVec3();
        c = Color{q.x, q.y, q.z, 1.0f};
    };
    if (p == "showWhenEmpty") { h->showWhenEmpty = v.AsBool(); return true; }
    if (p == "showWhileRunning") { h->showWhileRunning = v.AsBool(); return true; }
    if (p == "bobAmount") { h->bobAmount = v.AsFloat(); return true; }
    if (p == "swayAmount") { h->swayAmount = v.AsFloat(); return true; }
    if (p == "punchAmount") { h->punchAmount = v.AsFloat(); return true; }
    if (p == "handFov") { h->handFov = v.AsFloat(); return true; }
    if (p == "itemScale") { h->itemScale = v.AsFloat(); return true; }
    if (p == "holdMode") {
        int m = SafeInt(v.AsFloat());
        h->holdMode = m == 1 ? FirstPersonHands::HoldMode::Left
                    : m == 2 ? FirstPersonHands::HoldMode::Two
                             : FirstPersonHands::HoldMode::Right;
        return true;
    }
    if (p == "itemOffset" && v.IsVec3()) { h->itemOffset = v.AsVec3(); return true; }
    if (p == "itemRotation" && v.IsVec3()) { h->itemRotation = v.AsVec3(); return true; }
    if (p == "othersItemOffset" && v.IsVec3()) { h->othersItemOffset = v.AsVec3(); return true; }
    if (p == "othersItemRotation" && v.IsVec3()) { h->othersItemRotation = v.AsVec3(); return true; }
    if (p == "othersArmLift") { h->othersArmLift = v.AsFloat(); return true; }
    if (p == "othersArmForward") { h->othersArmForward = v.AsFloat(); return true; }
    std::string rest;
    if (FirstPersonHands::HandSide* s = FpSide(h, p, rest)) {
        if (rest == "Enabled") { s->enabled = v.AsBool(); return true; }
        if (rest == "Position" && v.IsVec3()) { s->position = v.AsVec3(); return true; }
        if (rest == "Rotation" && v.IsVec3()) { s->rotation = v.AsVec3(); return true; }
        if (rest == "UniformScale") { s->uniformScale = v.AsFloat(); return true; }
        if (rest == "Scale" && v.IsVec3()) { s->scale = v.AsVec3(); return true; }
        if (rest == "Size" && v.IsVec3()) { s->size = v.AsVec3(); return true; }
        if (rest == "UseColour" || rest == "UseColor") { s->useColour = v.AsBool(); return true; }
        if (rest == "Colour" || rest == "Color") { col(s->colour); return true; }
        if (rest == "Sleeve") { s->sleeve = v.AsBool(); return true; }
        if (rest == "SleeveLength") { s->sleeveLength = v.AsFloat(); return true; }
        if (rest == "SleeveColour" || rest == "SleeveColor") { col(s->sleeveColour); return true; }
    }
    return false;
}
inline bool FpHandsCall(FirstPersonHands* h, const std::string& m) {
    if (!h) return false;
    if (m == "ResetPlayerView") { h->ResetPlayerView(); return true; }
    if (m == "ResetOthersView") { h->ResetOthersView(); return true; }
    return false;
}

// Friendly key name -> Input char code (shared with the visual-script nodes).
inline char KeyCharFromName(const std::string& s) { return Input::KeyFromName(s); }

// ===================== JSON (for to_json / from_json builtins) =====================
// A compact, dependency-free JSON writer/reader over VsValue. Numbers, bools,
// strings, arrays and maps round-trip; vec3 is written as a [x,y,z] array.
// dup-allow: moved here unchanged from OkayScriptVM.cpp (S2); not a new copy
inline void JsonEscape(const std::string& s, std::string& out) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': out += "\\r";  break;
            default:   out += c;      break;
        }
    }
    out += '"';
}

inline void ToJson(const Value& v, std::string& out) {
    if (auto arr = v.AsArray()) {
        out += '[';
        for (std::size_t i = 0; i < arr->size(); ++i) { if (i) out += ','; ToJson((*arr)[i], out); }
        out += ']';
    } else if (auto m = v.AsMap()) {
        out += '{';
        bool first = true;
        for (auto& kv : *m) { if (!first) out += ','; first = false; JsonEscape(kv.first, out); out += ':'; ToJson(kv.second, out); }
        out += '}';
    } else if (v.IsString()) {
        JsonEscape(v.AsString(), out);
    } else if (v.IsBool()) {
        out += v.AsBool() ? "true" : "false";
    } else if (v.IsVec3()) {
        Vec3 p = v.AsVec3();
        out += '['; out += vs::VsValue(p.x).AsString(); out += ',';
        out += vs::VsValue(p.y).AsString(); out += ','; out += vs::VsValue(p.z).AsString(); out += ']';
    } else {
        out += v.AsString();   // number -> its formatted form
    }
}

// A tiny recursive-descent JSON parser. On malformed input it returns whatever it
// managed to parse (best-effort) — scripts should validate important data anyway.
struct JsonReader {
    const std::string& s; std::size_t i = 0;
    int depth = 0;   // array/object nesting: too deep is an error, not a stack overflow
    explicit JsonReader(const std::string& src) : s(src) {}
    void Ws() { while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) ++i; }
    Value Parse() { Ws(); if (i >= s.size()) return Value{}; char c = s[i];
        if (c == '{' || c == '[') {
            if (++depth > 256) throw std::runtime_error("from_json: nested too deeply (more than 256 levels)");
            Value v = c == '{' ? Obj() : Arr();
            --depth;
            return v;
        }
        if (c == '"') return Value{Str()};
        if (c == 't') { i += 4; return Value{true}; }
        if (c == 'f') { i += 5; return Value{false}; }
        if (c == 'n') { i += 4; return Value{}; }
        return Num();
    }
    std::string Str() {
        std::string out; ++i;  // opening quote
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                char e = s[++i];
                out += (e=='n')?'\n':(e=='t')?'\t':(e=='r')?'\r':e;
            } else out += s[i];
            ++i;
        }
        if (i < s.size()) ++i;  // closing quote
        return out;
    }
    Value Num() {
        std::size_t start = i;
        while (i < s.size() && (std::isdigit((unsigned char)s[i])||s[i]=='-'||s[i]=='+'||s[i]=='.'||s[i]=='e'||s[i]=='E')) ++i;
        return Value{(float)std::atof(s.substr(start, i - start).c_str())};
    }
    Value Arr() {
        Value v = Value::MakeArray(); auto a = v.AsArray(); ++i;  // '['
        Ws(); if (i < s.size() && s[i] == ']') { ++i; return v; }
        while (i < s.size()) { a->push_back(Parse()); Ws();
            if (i < s.size() && s[i] == ',') { ++i; Ws(); continue; }
            if (i < s.size() && s[i] == ']') { ++i; break; }
            break;
        }
        return v;
    }
    Value Obj() {
        Value v = Value::MakeMap(); auto m = v.AsMap(); ++i;  // '{'
        Ws(); if (i < s.size() && s[i] == '}') { ++i; return v; }
        while (i < s.size()) { Ws();
            if (s[i] != '"') break;
            std::string key = Str(); Ws();
            if (i < s.size() && s[i] == ':') ++i;
            (*m)[key] = Parse(); Ws();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == '}') { ++i; break; }
            break;
        }
        return v;
    }
};

// ===================== Lexer =====================
// Tok, Token, ScriptError and Lexer live in the engine-independent OkayScript core (okayscript/include/okayscript/,
// target okayscript_lang); they were moved there unchanged. The engine VM uses them under its own names.
using okayscript::Tok;
using okayscript::Token;
using okayscript::ScriptError;
using okayscript::Lexer;

// ===================== The language core =====================
// The parser, syntax tree and interpreter are the engine-independent OkayScript core (okayscript/: Parser.hpp,
// Runtime.hpp), instantiated in OkayScriptVM.cpp for the engine's value (vs::VsValue). The engine adds its scene
// names, object references and components through its okayscript::BasicHost (EngineHooks in OkayScriptVM.cpp).
using ReturnSignal = okayscript::ReturnSignal<Value>;
using okayscript::BreakSignal;
using okayscript::ContinueSignal;
using Environment = okayscript::BasicEnvironment<Value>;
using okayscript::IsNullish;
using okayscript::detail::IsClassInstance;

// ---- Object references (#888 Findings 108 and 111) ----------------------------
// A reference to a scene object is the object's instance id (a VsValue object),
// not its name: spawned copies all share one name, so a name alone picks the
// first copy. A reference still prints, concatenates and compares with text as
// the object's name. A plain name (old scripts, "Enemy") still works and means
// the first object with that name, exactly as before.
inline Value ObjectRef(const GameObject* g) { return g ? Value::Object(g->InstanceId()) : Value{}; }
/// The object `v` refers to: by instance id first (nullptr once destroyed), else
/// by name in `s` (the first object with that name).
inline GameObject* ResolveObject(Scene* s, const Value& v) {
    if (v.IsObject()) return v.AsObjectId() ? GameObject::FromInstanceId(v.AsObjectId()) : nullptr;
    return s ? s->Find(v.AsString()) : nullptr;
}
/// A member of a referenced object: other.name, other.tag, other.transform (the
/// reference itself), other.transform.position, ... False when it isn't one.
inline bool ObjectMember(GameObject* o, const Value& ref, const std::string& prop, Value& out) {
    if (prop == "gameObject" || prop == "transform" || prop == "collider") { out = ref; return true; }
    if (prop == "name") { out = Value{o ? o->name : (ref.IsObject() ? std::string() : ref.AsString())}; return true; }
    if (!o) return false;
    if (prop == "tag") { out = Value{o->tag}; return true; }
    if (prop == "activeSelf" || prop == "active") { out = Value{(bool)o->active}; return true; }
    if (prop == "activeInHierarchy") { out = Value{o->ActiveInHierarchy()}; return true; }
    Transform* t = o->transform;
    if (!t) return false;
    if (prop == "position")      { out = Value{t->Position()}; return true; }
    if (prop == "localPosition") { out = Value{t->localPosition}; return true; }
    if (prop == "localScale")    { out = Value{t->localScale}; return true; }
    if (prop == "eulerAngles" || prop == "localEulerAngles") { out = Value{t->localRotation.ToEuler()}; return true; }
    if (prop == "forward") { out = Value{t->Forward()}; return true; }
    if (prop == "up")      { out = Value{t->Up()}; return true; }
    if (prop == "right")   { out = Value{t->Right()}; return true; }
    return false;
}
/// Write a member of a referenced object: door.transform.position = v,
/// other.transform.localScale = v, door.transform.position.x = 3. False when
/// `path` isn't one that can be written.
inline bool SetObjectMember(GameObject* o, std::string path, const Value& v) {
    if (!o) return false;
    if (path.rfind("transform.", 0) == 0) path = path.substr(10);
    Transform* t = o->transform;
    if (!t) return false;
    auto axis = [&](const std::string& base, Vec3 cur, Vec3& out) -> bool {
        if (path == base) { out = v.AsVec3(); return true; }
        if (path.size() == base.size() + 2 && path.compare(0, base.size(), base) == 0 && path[base.size()] == '.') {
            const char c = path.back();
            if (c == 'x') cur.x = v.AsFloat(); else if (c == 'y') cur.y = v.AsFloat(); else if (c == 'z') cur.z = v.AsFloat();
            else return false;
            out = cur; return true;
        }
        return false;
    };
    Vec3 nv;
    if (axis("position", t->Position(), nv))   { t->SetPosition(nv); return true; }
    if (axis("localPosition", t->localPosition, nv)) { t->localPosition = nv; return true; }
    if (axis("localScale", t->localScale, nv)) { t->localScale = nv; return true; }
    if (path == "eulerAngles" || path == "localEulerAngles") { t->localRotation = Quat::Euler(v.AsVec3()); return true; }
    return false;
}
/// While alive, the script's host acts as `o` (its gameObject and transform), so
/// a builtin written for "this object" (transform.Translate, add_force3, the
/// Animator calls, ...) runs on another object. Restores the host on exit.
struct ActAsObject {
    ScriptHost* h; Transform* t = nullptr; GameObject* g = nullptr;
    ActAsObject(ScriptHost* host, GameObject* o) : h(host && o && o != host->gameObject ? host : nullptr) {
        if (!h) return;
        t = h->transform; g = h->gameObject;
        h->gameObject = o; h->transform = o->transform;
    }
    ~ActAsObject() { if (h) { h->transform = t; h->gameObject = g; } }
    ActAsObject(const ActAsObject&) = delete;
    ActAsObject& operator=(const ActAsObject&) = delete;
};

struct Runtime;
/// Gives a new Runtime the engine's hooks and settings (OkayScriptVM.cpp).
void InitEngineRuntime(Runtime& rt);

struct Runtime : okayscript::BasicRuntime<Value> {
    ScriptHost* host = nullptr;
    /// ref.Method(args) on an object reference (other.SetActive(false),
    /// door.transform.Translate(1, 0, 0), other.GetComponent<Rigidbody>()).
    Value CallOnObject(const Value& ref, const std::string& method, std::vector<Value>& args);
    /// The engine's own dotted names (Time.deltaTime, transform.position, gameObject.name, Color.red, a component
    /// or object reference in a variable): read / write by their whole text. EngineHooks hands them to the core.
    Value EngineDotted(const std::string& name, bool& ok);
    bool EngineSetDotted(const std::string& name, const Value& v);

    // Details of the most recent raycast()/raycast3() call, exposed to scripts
    // through ray_object()/ray_x()/ray_dist()/... accessors.
    struct LastHit {
        bool hit = false;
        std::string object;
        float px = 0, py = 0, pz = 0;   // hit point
        float nx = 0, ny = 0, nz = 0;   // surface normal
        float dist = 0;
    };
    LastHit lastHit2D;
    LastHit lastHit3D;
    std::vector<LastHit> lastHitAll;    // raycast_all / raycast_all3, nearest first

    // Scheduled callbacks (after / every). interval == 0 means fire once.
    struct Timer { float remaining; float interval; std::string fn; bool dead; };
    std::vector<Timer> timers;

    // Destroy(obj, seconds): {seconds left, object reference or name ("" = this object)}.
    std::vector<std::pair<float, Value>> pendingDestroy;

    // ---- C#-style coroutines -------------------------------------------------
    // StartCoroutine(Routine()) runs Routine on its own thread, but only one side
    // runs at a time: each `yield return` hands control back to the game loop,
    // which resumes the routine when its wait is over (next frame,
    // WaitForSeconds, or another coroutine finishing). The routine's local scope
    // frames are parked while it waits so the shared scope stack stays balanced.
    struct CoroutineAbort {};
    struct Coroutine;   // defined in OkayScriptVM_Coroutine.cpp: it holds the thread, mutex and condition variable
    std::vector<std::unique_ptr<Coroutine>> coroutines;
    Coroutine* currentCo = nullptr;
    int nextCoId = 1;
    long long frame = 0;
    Runtime();
    ~Runtime();
    Coroutine* FindCo(int id);
    void Resume(Coroutine* co);
    // Called on the coroutine's own thread by `yield return`.
    void YieldCoroutine(float wait, bool nextFrame, int waitFor, Value cond = Value{}, bool condWhile = false, bool real = false);
    int StartCoroutine(const std::string& fn, std::vector<Value> args, std::vector<std::string> argNames = {});
    void StopCoroutine(Coroutine* co);
    void StopCoroutines(const std::string& fn);   // "" = all
    void ReapCoroutines();
    void TickCoroutines(float dt);

    /// Advance scheduled timers by dt and invoke any that come due. Once-timers
    /// are removed after firing; repeating ones re-arm by their interval.
    void TickTimers(float dt) {
        if (!(dt > 0.0f) || !std::isfinite(dt)) dt = 0.0f;   // a NaN or negative frame time would stop every timer for good
        std::vector<std::string> due;
        const float kEps = 1e-4f;                // absorb fp dust at the boundary
        for (auto& t : timers) {
            t.remaining -= dt;
            int guard = 0;                       // cap multi-fires on a big dt
            while (t.remaining <= kEps && guard++ < 64) {
                due.push_back(t.fn);
                if (t.interval > 0.0f) t.remaining += t.interval;
                else { t.dead = true; break; }
            }
        }
        timers.erase(std::remove_if(timers.begin(), timers.end(),
                                    [](const Timer& t) { return t.dead; }), timers.end());
        std::vector<Value> none;
        for (const auto& fn : due) if (functions.count(fn)) Call(fn, none);
    }

    /// Set a global from outside (the Inspector): converted to its declared type
    /// when it has one; a value that can't convert is ignored.
    void SetGlobalChecked(const std::string& n, const Value& v) {
        Environment& g = Global();
        auto k = g.kinds.find(n);
        if (k == g.kinds.end()) { g.vars[n] = v; return; }
        try { g.vars[n] = okayscript::CoerceToKind<Value>(k->second, v, n); } catch (const ScriptError&) {}
    }
};

// A component reference returned by GetComponent<T>(): the string "@T" for this
// object's own component, "@T#<instance id>" for another object's (other.GetComponent<T>()).
inline bool IsComponentRef(const Value& v) {
    return v.IsString() && v.AsString().size() > 1 && v.AsString()[0] == '@';
}
float RealFps();   // Time.fps and fps(): defined in OkayScriptVM.cpp

// A function from the shared table (okay/Scripting/ScriptApiRegistry.hpp) as an OkayScript builtin. It is a
// named type so RegisterBuiltins can tell a shared function from a second, hand-written copy in the map.
struct SharedApiCall {
    std::shared_ptr<const ScriptApiEntry> entry;
    const Runtime* rt = nullptr;
    std::shared_ptr<bool> warned = std::make_shared<bool>(false);   // warn once per VM, not every frame
    Value operator()(std::vector<Value>& a) const {
        return CallScriptApiDynamic(*entry, rt->host ? rt->host->gameObject : nullptr, a, warned.get());
    }
};


// What the topic functions of OkayScriptVM::Impl (RegisterXxxBuiltins) share: the command table and the few
// small closures that more than one topic needs. Each topic copies the ones it uses at its top, so its body
// reads exactly as it did when it was part of one long function.
using BuiltinTable = decltype(Runtime::builtins);
struct HostTransformFn {
    Runtime* rt;
    Transform* operator()() const { return rt->host ? rt->host->transform : nullptr; }
};
struct HostObjectFn {
    Runtime* rt;
    GameObject* operator()() const { return rt->host ? rt->host->gameObject : nullptr; }
};
struct HostSceneFn {
    Runtime* rt;
    Scene* operator()() const { return (rt->host && rt->host->gameObject) ? rt->host->gameObject->scene() : nullptr; }
};
// An object reference resolves by its id; a name ("" / "self" = this object) by the first object with that name.
struct FindNamedFn {
    Runtime* rt;
    HostSceneFn sceneOf;
    GameObject* operator()(const Value& v) const {
        if (v.IsObject()) return ResolveObject(nullptr, v);
        const std::string n = v.AsString();
        if (n.empty() || n == "self") return rt->host ? rt->host->gameObject : nullptr;
        Scene* s = sceneOf(); return s ? s->Find(n) : nullptr;
    }
};
// alias("from", "to"): a second name for a builtin that is already registered.
struct BuiltinAliasFn {
    BuiltinTable* table;
    void operator()(const char* from, const char* to) const {
        auto it = table->find(to);
        if (it != table->end()) (*table)[from] = it->second;
    }
};
template <class T> struct HostAnimFn {
    HostObjectFn self;
    T* operator()() const { return FindAnimComponent<T>(self()); }
};
struct BuiltinCtx {
    explicit BuiltinCtx(Runtime& r)
        : b(r.builtins), tf{&r}, go{&r}, sceneOf{&r}, findNamed{&r, HostSceneFn{&r}}, alias{&r.builtins},
          selfObj{&r}, charSelf{HostObjectFn{&r}}, modelSelf{HostObjectFn{&r}}, smSelf{HostObjectFn{&r}} {}
    BuiltinTable& b;
    HostTransformFn tf;
    HostObjectFn go;
    HostSceneFn sceneOf;
    FindNamedFn findNamed;
    BuiltinAliasFn alias;
    HostObjectFn selfObj;
    HostAnimFn<Character> charSelf;
    HostAnimFn<ModelAnimator> modelSelf;
    HostAnimFn<AnimStateMachine> smSelf;
};
} // namespace script_vm

using namespace script_vm;

struct OkayScriptVM::Impl {
    // Every shared function under its OkayScript name and aliases. Runs first in RegisterBuiltins, so the
    // aliases made further down find it; the check at the end refuses a later hand-written copy.
    void InstallSharedApis() {
        for (const ScriptApiEntry& e : ScriptApis()) {
            auto shared = std::make_shared<const ScriptApiEntry>(e);
            SharedApiCall call{shared, &rt};
            rt.builtins[e.name] = call;
            for (const std::string& a : e.aliases) rt.builtins[a] = call;
        }
    }
    void CheckSharedApis() {
        for (const ScriptApiEntry& e : ScriptApis()) {
            std::vector<std::string> names = e.aliases;
            names.push_back(e.name);
            for (const std::string& n : names) {
                auto it = rt.builtins.find(n);
                if (it != rt.builtins.end() && it->second.target<SharedApiCall>()) continue;
                NoteScriptApiDuplicate("OkayScript", n);
                rt.builtins[n] = SharedApiCall{std::make_shared<const ScriptApiEntry>(e), &rt};
            }
        }
    }

    Runtime rt;
    // Tweens and timers outlive a call; their callbacks check this so they do
    // nothing once the script is gone (its component removed or reloaded).
    std::shared_ptr<int> life = std::make_shared<int>(0);
    std::unique_ptr<okayscript::Program> program;   // the loaded source: functions, classes and lambdas point into it
    bool loaded = false;
    ~Impl() { rt.StopCoroutines(""); }   // unwind every suspended coroutine thread
    // "Bare mode": a script with NO functions at all — its top-level statements ARE the
    // per-frame loop. Lets a whole script be one line, e.g. `on_key_move(5)`. Setup-style
    // top-level code (globals) still runs once at load whenever any function is defined.
    bool bareMode = false;
    // In bare mode the top-level runs once at Load (so globals exist immediately,
    // as for any script) AND every frame. To keep the per-frame tick count the
    // same as before, the first bare Update skips the re-run that Load already did.
    bool bareSkipFirstUpdate = false;

    // Last network message popped by net_poll(), exposed via net_msg_* builtins.
    std::string netMsgChannel, netMsgData;
    float       netMsgFrom = 0.0f;
    // Last object RPC popped by net_obj_rpc_poll().
    std::string netObjRpcName, netObjRpcData;
    float       netObjRpcFrom = 0.0f;

    // The scene's NetworkManager, if any (multiplayer is opt-in per scene).
    NetworkManager* Net() {
        Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
        return s ? s->FindObjectOfType<NetworkManager>() : nullptr;
    }
    // Find or create the scene's NetworkManager, wiring the host object as the
    // broadcast avatar and a default factory that mirrors remote peers as sprites.
    NetworkManager* EnsureNet() {
        if (NetworkManager* n = Net()) return n;
        if (!rt.host || !rt.host->gameObject || !rt.host->gameObject->scene()) return nullptr;
        Scene* s = rt.host->gameObject->scene();
        GameObject* netObj = s->CreateGameObject("__Network");
        NetworkManager* n = netObj->AddComponent<NetworkManager>();
        if (rt.host->transform) n->SetLocalAvatar(rt.host->transform, '@');
        n->SetRemoteFactory([s](std::uint32_t id, char) {
            GameObject* g = s->CreateGameObject("Peer" + std::to_string(id));
            g->AddComponent<SpriteRenderer>()->color = Color::FromBytes(230, 120, 90);
            return g;
        });
        return n;
    }

    void RegisterDebugBuiltins(BuiltinCtx& c);
    void RegisterScriptUiBuiltins(BuiltinCtx& c);
    void RegisterMotionHelperBuiltins(BuiltinCtx& c);
    void RegisterInputBuiltins(BuiltinCtx& c);
    void RegisterScoreSpawnBuiltins(BuiltinCtx& c);
    void RegisterObjectPoolBuiltins(BuiltinCtx& c);
    void RegisterNamedObjectBuiltins(BuiltinCtx& c);
    void RegisterNpcBuiltins(BuiltinCtx& c);
    void RegisterSpawnerBuiltins(BuiltinCtx& c);
    void RegisterBuildModeBuiltins(BuiltinCtx& c);
    void RegisterMovementHelperBuiltins(BuiltinCtx& c);
    void RegisterCameraControlBuiltins(BuiltinCtx& c);
    void RegisterCameraRigBuiltins(BuiltinCtx& c);
    void RegisterUiByNameBuiltins(BuiltinCtx& c);
    void RegisterUiRuntimeBuiltins(BuiltinCtx& c);
    void RegisterUiControlBuiltins(BuiltinCtx& c);
    void RegisterBody2DBuiltins(BuiltinCtx& c);
    void RegisterSpriteEffectBuiltins(BuiltinCtx& c);
    void RegisterAudioSaveBuiltins(BuiltinCtx& c);
    void RegisterDisplayBuiltins(BuiltinCtx& c);
    void RegisterStoryBuiltins(BuiltinCtx& c);
    void RegisterNavigationBuiltins(BuiltinCtx& c);
    void RegisterGridPathBuiltins(BuiltinCtx& c);
    void RegisterNavmeshQueryBuiltins(BuiltinCtx& c);
    void RegisterBehaviorTreeBuiltins(BuiltinCtx& c);
    void RegisterSplineBuiltins(BuiltinCtx& c);
    void RegisterMusicBuiltins(BuiltinCtx& c);
    void RegisterRaycast2DBuiltins(BuiltinCtx& c);
    void RegisterSceneLoadBuiltins(BuiltinCtx& c);
    void RegisterCharacterAnimBuiltins(BuiltinCtx& c);
    void RegisterAnimStateMachineBuiltins(BuiltinCtx& c);
    void RegisterBlendShapeBuiltins(BuiltinCtx& c);
    void RegisterIkBuiltins(BuiltinCtx& c);
    void RegisterInventoryBuiltins(BuiltinCtx& c);
    void RegisterNetworkBuiltins(BuiltinCtx& c);
    void RegisterMatchmakingQueueBuiltins(BuiltinCtx& c);
    void RegisterMatchResultBuiltins(BuiltinCtx& c);
    void RegisterServerBrowserBuiltins(BuiltinCtx& c);
    void RegisterSteamBuiltins(BuiltinCtx& c);
    void RegisterFriendsBuiltins(BuiltinCtx& c);
    void RegisterLobbyBuiltins(BuiltinCtx& c);
    void RegisterPlayFabBuiltins(BuiltinCtx& c);
    void RegisterAccountBuiltins(BuiltinCtx& c);
    void RegisterEasingBuiltins(BuiltinCtx& c);
    void RegisterBitsJsonStringBuiltins(BuiltinCtx& c);
    void RegisterTransformHelperBuiltins(BuiltinCtx& c);
    void RegisterPhysicsQueryBuiltins(BuiltinCtx& c);
    void RegisterMaterialEventBuiltins(BuiltinCtx& c);
    void RegisterMathUtilityBuiltins(BuiltinCtx& c);
    void RegisterTweenBuiltins(BuiltinCtx& c);
    void RegisterFriendlyAliasBuiltins(BuiltinCtx& c);
    void RegisterCsApiBuiltins(BuiltinCtx& c);
    void RegisterVehicleBuiltins(BuiltinCtx& c);
    void RegisterPlainAliasBuiltins(BuiltinCtx& c);
    void RegisterCsCompatBuiltins(BuiltinCtx& c);
    void RegisterCoroutineBuiltins(BuiltinCtx& c);
    void RegisterObjectLifecycleBuiltins(BuiltinCtx& c);
    void RegisterComponentAccessBuiltins(BuiltinCtx& c);
    void RegisterBuiltins();
};

} // namespace okay
