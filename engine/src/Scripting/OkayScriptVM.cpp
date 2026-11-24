#include "OkayScriptVMInternal.hpp"
#include "okayscript/detail/Eval.ipp"   // the core's definitions, instantiated below for the engine's value

// The OkayScript language itself (parser, syntax tree, interpreter) is the engine-independent core in okayscript/.
// This file instantiates it for vs::VsValue and adds what only the engine knows: scene names (Time.deltaTime,
// transform.position, gameObject.name), object references, components and coroutines, through EngineHooks.
template class okayscript::BasicRuntime<okay::vs::VsValue>;
template class okayscript::BasicArgs<okay::vs::VsValue>;

namespace okay {
namespace {
std::string CanonEvent(const std::string& name);   // the engine's event names (defined further down)
} // namespace
namespace script_vm {

// ---- dotted property access --------------------------------------
// Frames per second over the last half second of REAL frame times (the loop that is running the game keeps
// them, GameLoop::Stats()), so a pause, a slow-motion scale or a hit stop does not change it and a stall shows.
// Without a loop (a host that does not run one) the last frame's real time stands in.
float RealFps() {
    if (const GameLoop* loop = GameLoop::Current()) {
        const double f = loop->Stats().AverageFps(30);
        if (f > 0.0) return (float)f;
    }
    const float u = Time::UnscaledDeltaTime();
    return u > 1e-6f ? 1.0f / u : 0.0f;
}
namespace {
// Euler Z angle (degrees) of a quaternion, for transform.eulerAngles.z.
float EulerZ(const Quat& q) {
    return std::atan2(2.0f * (q.w * q.z + q.x * q.y),
                      1.0f - 2.0f * (q.y * q.y + q.z * q.z)) * Mathf::Rad2Deg;
}
} // namespace

Value Runtime::CallOnObject(const Value& ref, const std::string& method, std::vector<Value>& args) {
    if (method == "CompareTag" || method == "GetComponent" || method == "SetActive") {
        std::vector<Value> a{ref};
        a.insert(a.end(), args.begin(), args.end());
        return Call("__object_" + method, a);
    }
    // ref.transform.Translate(...), ref.transform.LookAt(...): this object's
    // transform calls, run on the referenced object.
    if (builtins.count("transform." + method)) {
        GameObject* o = ResolveObject(nullptr, ref);
        if (!o) throw ScriptError("can't call '" + method + "': the object no longer exists");
        ActAsObject as(host, o);
        return Call("transform." + method, args);
    }
    // Text methods on its name: other.StartsWith("Enemy"), other.ToString().
    Value out, nm{ref.AsString()};
    okayscript::detail::LambdaInvoker<Value> inv = [this](const Value& fv, std::vector<Value>& xs) { return CallLambda(fv, xs); };
    if (okayscript::detail::CallValueMethod<Value>(nm, method, args, out, &inv)) return out;
    throw ScriptError("no method '" + method + "' on an object reference");
}

// The engine's names, read and written by their whole dotted text (Time.deltaTime, transform.position.x, ...).
// `ok` false: not an engine name, the core goes on with its own rules.
Value Runtime::EngineDotted(const std::string& name, bool& ok) {
    ok = true;
    Transform* t = host ? host->transform : nullptr;
    GameObject* g = host ? host->gameObject : nullptr;

    // Time
    if (name == "Time.deltaTime")  return Value{host ? host->deltaTime : 0.0f};
    if (name == "Time.time")       return Value{Time::ElapsedTime()};
    if (name == "Time.timeScale")  return Value{Time::TimeScale()};
    if (name == "Time.frameCount") return Value{(float)Time::FrameCount()};
    // The step of a FixedUpdate and the real frame time were both answered with this frame's SCALED delta
    // (so Time.unscaledDeltaTime read 0 in a pause and half of itself at timeScale 0.5).
    if (name == "Time.fixedDeltaTime")      return Value{Time::FixedDeltaTime()};
    if (name == "Time.physicsSteps") {
        Scene* sc = g ? g->scene() : nullptr;
        return Value{sc ? sc->PhysicsStepsPerSecond() : 0.0f};
    }
    if (name == "Time.smoothPhysics") {
        Scene* sc = g ? g->scene() : nullptr;
        return Value{sc ? sc->SmoothPhysics() : false};
    }
    if (name == "Time.unscaledDeltaTime")   return Value{Time::UnscaledDeltaTime()};
    if (name == "Time.unscaledTime")        return Value{(float)Time::UnscaledTime()};
    // Since this Play began (the game's clocks that Time.time and Time.frameCount are the process's own).
    if (name == "Time.sessionTime")         return Value{(float)Time::SessionTime()};
    if (name == "Time.sessionUnscaledTime") return Value{(float)Time::SessionUnscaledTime()};
    if (name == "Time.sessionFrameCount")   return Value{(float)Time::SessionFrameCount()};
    if (name == "Time.fps")                 return Value{RealFps()};
    if (name == "Time.paused")              return Value{Game::Paused()};
    if (name == "Time.hitchCount")          return Value{(float)(GameLoop::Current() ? GameLoop::Current()->HitchCount() : 0)};
    if (name == "Space.World")     return Value{1.0f};
    if (name == "Space.Self")      return Value{0.0f};
    // ForceMode (3D) uses the engine's ForceMode order; ForceMode2D.Impulse is 1,
    // the same as the ForceMode2D.Impulse() call. #888 F109: these read as 0 before.
    if (name == "ForceMode.Force")          return Value{0.0f};
    if (name == "ForceMode.Acceleration")   return Value{1.0f};
    if (name == "ForceMode.Impulse")        return Value{2.0f};
    if (name == "ForceMode.VelocityChange") return Value{3.0f};
    if (name == "ForceMode2D.Force")        return Value{0.0f};
    if (name == "ForceMode2D.Impulse")      return Value{1.0f};
    if (name == "RigidbodyType2D.Dynamic")   return Value{0.0f};
    if (name == "RigidbodyType2D.Kinematic") return Value{1.0f};
    if (name == "RigidbodyType2D.Static")    return Value{2.0f};
    if (name == "null")            return Value{};
    // Mathf constants
    if (name == "Mathf.PI")        return Value{Mathf::PI};
    if (name == "Mathf.Deg2Rad")   return Value{Mathf::Deg2Rad};
    if (name == "Mathf.Rad2Deg")   return Value{Mathf::Rad2Deg};
    if (name == "Mathf.Infinity")  return Value{1e30f};
    if (name == "Mathf.Epsilon")   return Value{1e-6f};
    if (name == "Random.value")    return Value{Random::Shared().Range(0.0f, 1.0f)};
    // Screen / Input
    if (name == "Screen.width")        return Value{UICanvas::Width()};
    if (name == "Screen.height")       return Value{UICanvas::Height()};
    if (name == "Input.mousePosition.x") return Value{Input::MousePosition().x};
    if (name == "Input.mousePosition.y") return Value{Input::MousePosition().y};
    if (name == "Input.mousePosition")   { Vec2 m = Input::MousePosition(); return Value{Vec3{m.x, m.y, 0.0f}}; }
    // Vector constants
    if (name == "Vector3.zero" || name == "Vector2.zero") return Value{Vec3::Zero};
    if (name == "Vector3.one"  || name == "Vector2.one")  return Value{Vec3{1, 1, 1}};
    if (name == "Quaternion.identity") return Value{Vec3::Zero};   // euler (0,0,0)
    // Color constants (RGB as a Vec3; pass to set_color/set_tint).
    if (name == "Color.red")     return Value{Vec3{1, 0, 0}};
    if (name == "Color.green")   return Value{Vec3{0, 1, 0}};
    if (name == "Color.blue")    return Value{Vec3{0, 0, 1}};
    if (name == "Color.white")   return Value{Vec3{1, 1, 1}};
    if (name == "Color.black")   return Value{Vec3{0, 0, 0}};
    if (name == "Color.yellow")  return Value{Vec3{1, 1, 0}};
    if (name == "Color.cyan")    return Value{Vec3{0, 1, 1}};
    if (name == "Color.magenta") return Value{Vec3{1, 0, 1}};
    if (name == "Color.gray" || name == "Color.grey") return Value{Vec3{0.5f, 0.5f, 0.5f}};
    if (name == "Vector3.up")    return Value{Vec3{0, 1, 0}};
    if (name == "Vector3.down")  return Value{Vec3{0, -1, 0}};
    if (name == "Vector3.right") return Value{Vec3{1, 0, 0}};
    if (name == "Vector3.left")  return Value{Vec3{-1, 0, 0}};
    if (name == "Vector3.forward") return Value{Vec3{0, 0, 1}};
    if (name == "Vector3.back")    return Value{Vec3{0, 0, -1}};
    if (name == "Vector2.up")    return Value{Vec3{0, 1, 0}};
    if (name == "Vector2.down")  return Value{Vec3{0, -1, 0}};
    if (name == "Vector2.right") return Value{Vec3{1, 0, 0}};
    if (name == "Vector2.left")  return Value{Vec3{-1, 0, 0}};

    if (t && selfClass.empty()) {
        Vec3 wp = t->Position();
        if (name == "transform.position")   return Value{wp};
        if (name == "transform.position.x") return Value{wp.x};
        if (name == "transform.position.y") return Value{wp.y};
        if (name == "transform.position.z") return Value{wp.z};
        if (name == "transform.localPosition")   return Value{t->localPosition};
        if (name == "transform.localPosition.x") return Value{t->localPosition.x};
        if (name == "transform.localPosition.y") return Value{t->localPosition.y};
        if (name == "transform.localPosition.z") return Value{t->localPosition.z};
        if (name == "transform.localScale")   return Value{t->localScale};
        if (name == "transform.localScale.x") return Value{t->localScale.x};
        if (name == "transform.localScale.y") return Value{t->localScale.y};
        if (name == "transform.localScale.z") return Value{t->localScale.z};
        if (name == "transform.eulerAngles.z" || name == "transform.rotation")
            return Value{EulerZ(t->localRotation)};
        if (name == "transform.eulerAngles" || name == "transform.localEulerAngles") return Value{t->localRotation.ToEuler()};
        if (name == "transform.eulerAngles.x" || name == "transform.localEulerAngles.x") return Value{t->localRotation.ToEuler().x};
        if (name == "transform.eulerAngles.y" || name == "transform.localEulerAngles.y") return Value{t->localRotation.ToEuler().y};
        if (name == "transform.localEulerAngles.z") return Value{t->localRotation.ToEuler().z};
        if (name == "transform.forward") return Value{t->Forward()};
        if (name == "transform.up")      return Value{t->Up()};
        if (name == "transform.right")   return Value{t->Right()};
    }
    if (g && selfClass.empty()) {
        if (name == "gameObject.name") return Value{g->name};
        if (name == "gameObject.activeSelf" || name == "gameObject.active") return Value{g->active};
        if (name == "gameObject.tag")  return Value{g->tag};
    }
    ok = false;
    return Value{};
}

bool Runtime::EngineSetDotted(const std::string& name, const Value& v) {
    Transform* t = host ? host->transform : nullptr;
    GameObject* g = host ? host->gameObject : nullptr;

    // (`this.field` and a class object's fields were handled by the core before it asks here.)
    if (name == "Time.timeScale") { Time::SetTimeScale(v.AsFloat()); return true; }
    if (name == "Time.physicsSteps") {
        if (Scene* sc = g ? g->scene() : nullptr) sc->SetPhysicsStepsPerSecond(v.AsFloat());
        return true;
    }
    if (name == "Time.smoothPhysics") {
        if (Scene* sc = g ? g->scene() : nullptr) sc->SetSmoothPhysics(v.AsBool());
        return true;
    }
    // door.transform.position = v / other.transform.localScale = v through an
    // object reference (or a plain object name, for transform members) held in
    // a variable. Before #888 F111 these writes were dropped without a word.
    if (auto dot = name.find('.'); dot != std::string::npos) {
        const std::string base = name.substr(0, dot), rest = name.substr(dot + 1);
        if (base.find('.') == std::string::npos && !(base == "this" && IsClassInstance(self)) && Has(base)) {
            Value bv = Get(base);
            if (bv.IsObject()) {
                GameObject* o = ResolveObject(nullptr, bv);
                if (!o) throw ScriptError("can't set '" + name + "': the object no longer exists");
                if (!SetObjectMember(o, rest, v)) throw ScriptError("can't set '" + rest + "' on an object");
                return true;
            }
            if (bv.IsString() && !IsComponentRef(bv) && rest.rfind("transform.", 0) == 0 && g && g->scene()) {
                if (GameObject* o = ResolveObject(g->scene(), bv)) if (SetObjectMember(o, rest, v)) return true;
            }
        }
    }
    // rb.velocity = ... on a GetComponent<T>() reference held in a variable.
    if (auto dot = name.find('.'); dot != std::string::npos && name.find('.', dot + 1) == std::string::npos) {
        const std::string base = name.substr(0, dot);
        if (Has(base)) {
            Value bv = Get(base);
            if (IsComponentRef(bv)) {
                std::vector<Value> a{Value{bv.AsString().substr(1)}, Value{name.substr(dot + 1)}, v};
                Call("__set_component", a);
                return true;
            }
        }
    }
    if (t && selfClass.empty()) {
        if (name == "transform.position")        { t->SetPosition(v.AsVec3()); return true; }
        if (name == "transform.localPosition")   { t->localPosition = v.AsVec3(); return true; }
        if (name == "transform.localScale")      { t->localScale = v.AsVec3(); return true; }
        if (name == "transform.position.x")      { Vec3 p = t->Position(); p.x = v.AsFloat(); t->SetPosition(p); return true; }
        if (name == "transform.position.y")      { Vec3 p = t->Position(); p.y = v.AsFloat(); t->SetPosition(p); return true; }
        if (name == "transform.position.z")      { Vec3 p = t->Position(); p.z = v.AsFloat(); t->SetPosition(p); return true; }
        if (name == "transform.localPosition.x") { t->localPosition.x = v.AsFloat(); return true; }
        if (name == "transform.localPosition.y") { t->localPosition.y = v.AsFloat(); return true; }
        if (name == "transform.localPosition.z") { t->localPosition.z = v.AsFloat(); return true; }
        if (name == "transform.localScale.x")    { t->localScale.x = v.AsFloat(); return true; }
        if (name == "transform.localScale.y")    { t->localScale.y = v.AsFloat(); return true; }
        if (name == "transform.localScale.z")    { t->localScale.z = v.AsFloat(); return true; }
        if (name == "transform.eulerAngles.z")
            { t->localRotation = Quat::Euler({0, 0, v.AsFloat()}); return true; }
        if (name == "transform.eulerAngles" || name == "transform.localEulerAngles")
            { t->localRotation = Quat::Euler(v.AsVec3()); return true; }
        // transform.rotation accepts a Quaternion.Euler(...) (a Vec3 euler) or a
        // bare Z angle; use the Z component either way.
        if (name == "transform.rotation")
            { t->localRotation = Quat::Euler({0, 0, v.AsVec3().z}); return true; }
    }
    if (g && selfClass.empty()) {
        if (name == "gameObject.name")       { g->name = v.AsString(); return true; }
        if (name == "gameObject.activeSelf" || name == "gameObject.active") { g->active = v.AsBool(); return true; }
        if (name == "gameObject.tag")        { g->tag = v.AsString(); return true; }
    }
    return false;
}

namespace {
// What the engine adds to the language (okayscript::BasicHost): its names, object references, components, coroutines.
struct EngineHooks final : okayscript::BasicHost<Value> {
    static Runtime& R(okayscript::BasicRuntime<Value>& rt) { return static_cast<Runtime&>(rt); }

    bool ReadName(okayscript::BasicRuntime<Value>& brt, const std::string& name, Value& out) override {
        Runtime& r = R(brt);
        if (name.find('.') == std::string::npos) {
            // Bare `gameObject` (as in Destroy(gameObject)) is a reference to this object (prints as its name).
            if (name == "gameObject" && r.host && r.host->gameObject) { out = ObjectRef(r.host->gameObject); return true; }
            return false;
        }
        bool ok = false;
        out = r.EngineDotted(name, ok);
        return ok;
    }
    bool WriteName(okayscript::BasicRuntime<Value>& brt, const std::string& name, const Value& v) override {
        return R(brt).EngineSetDotted(name, v);
    }
    // other.tag, other.transform, GetComponent<T>().enabled ... on an object reference or a component reference.
    bool GetMember(okayscript::BasicRuntime<Value>& brt, const Value& v, const std::string& prop, Value& out) override {
        Runtime& r = R(brt);
        if (IsComponentRef(v)) {
            std::vector<Value> a{Value{v.AsString().substr(1)}, Value{prop}};
            out = r.Call("__get_component", a);
            return true;
        }
        if (v.IsObject()) {   // GameObject.Find("Door").transform.position
            GameObject* o = ResolveObject(nullptr, v);
            if (ObjectMember(o, v, prop, out)) return true;
            if (!o) throw ScriptError("can't read '" + prop + "': the object no longer exists");
        }
        return false;
    }
    // An object's name held as text (`other` in a collision handler): other.name, other.tag, other.transform.position.
    bool GetMemberLast(okayscript::BasicRuntime<Value>& brt, const Value& v, const std::string& prop, Value& out) override {
        Runtime& r = R(brt);
        GameObject* g = r.host ? r.host->gameObject : nullptr;
        if (!v.IsString() || IsComponentRef(v) || !g || !g->scene()) return false;
        if (prop == "name" || prop == "gameObject" || prop == "transform") { out = v; return true; }
        if (GameObject* o = g->scene()->Find(v.AsString())) {
            if (prop == "tag") { out = Value{o->tag}; return true; }
            if (prop == "position" && o->transform) { out = Value{o->transform->Position()}; return true; }
            if (prop == "activeSelf") { out = Value{o->active}; return true; }
        }
        return false;
    }
    // expr.member = value on an object reference or a component reference.
    bool SetMember(okayscript::BasicRuntime<Value>& brt, const Value& target, const std::string& name, const Value& v) override {
        Runtime& r = R(brt);
        if (target.IsObject()) {   // GameObject.Find("Door").transform.position = v
            GameObject* o = ResolveObject(nullptr, target);
            if (!o) throw ScriptError("can't set '" + name + "': the object no longer exists");
            if (!SetObjectMember(o, name, v)) throw ScriptError("can't set '" + name + "' on an object");
            return true;
        }
        if (IsComponentRef(target)) {
            std::vector<Value> a{Value{target.AsString().substr(1)}, Value{name}, v};
            r.Call("__set_component", a);
            return true;
        }
        if (target.IsNumber() && target.AsFloat() == 0.0f)
            throw ScriptError("can't set '" + name + "': the object is null (e.g. GetComponent found no such component)");
        if (!target.IsMap()) throw ScriptError("can't assign '" + name + "' on this value");
        return false;
    }
    // ref.Method(args) on an object reference or a component reference.
    bool CallMember(okayscript::BasicRuntime<Value>& brt, const Value& v, const std::string& method, std::vector<Value>& args,
                    Value& out) override {
        Runtime& r = R(brt);
        if (IsComponentRef(v)) {   // rb.AddForce(...), anim.SetTrigger("Jump")
            std::vector<Value> a{Value{v.AsString().substr(1)}, Value{method}};
            a.insert(a.end(), args.begin(), args.end());
            out = r.Call("__call_component", a);
            return true;
        }
        if (v.IsObject()) { out = r.CallOnObject(v, method, args); return true; }   // other.SetActive(false)
        return false;
    }
    // `door.transform.Translate(...)` with `door` an object's name, and `other.CompareTag(...)` with `other` a name.
    bool CallUnknown(okayscript::BasicRuntime<Value>& brt, const std::string& name, std::vector<Value>& args, Value& out) override {
        Runtime& r = R(brt);
        const auto dot = name.rfind('.');
        if (dot == std::string::npos || dot == 0) return false;
        const std::string base = name.substr(0, dot), method = name.substr(dot + 1);
        bool ok = false;
        Value obj;
        if (base.find('.') == std::string::npos) {
            if (r.Has(base)) { obj = r.Get(base); ok = true; }
        }
        if (!ok) obj = r.GetDotted(base, ok);
        if (!ok) return false;
        if (obj.IsString() && base.size() > 10 && base.compare(base.size() - 10, 10, ".transform") == 0 &&
            r.builtins.count("transform." + method)) {
            GameObject* me = r.host ? r.host->gameObject : nullptr;
            GameObject* o = ResolveObject(me ? me->scene() : nullptr, obj);
            if (!o) throw ScriptError("can't call '" + method + "': no object named '" + obj.AsString() + "'");
            ActAsObject as(r.host, o);
            out = r.Call("transform." + method, args);
            return true;
        }
        if (method == "CompareTag" || method == "GetComponent" || method == "SetActive") {
            std::vector<Value> a{obj};
            a.insert(a.end(), args.begin(), args.end());
            out = r.Call("__object_" + method, a);
            return true;
        }
        return false;
    }
    // yield return <wait>;  — inside a coroutine: hand control back to the game loop.
    bool YieldValue(okayscript::BasicRuntime<Value>& brt, const Value& v) override {
        Runtime& r = R(brt);
        if (auto m = v.AsMap()) {
            bool real = m->count("__real") > 0;
            if (auto it = m->find("__wait"); it != m->end()) { r.YieldCoroutine(it->second.AsFloat(), false, 0, Value{}, false, real); return true; }
            if (auto it = m->find("__until"); it != m->end()) { r.YieldCoroutine(0.0f, false, 0, it->second, false); return true; }
            if (auto it = m->find("__while"); it != m->end()) { r.YieldCoroutine(0.0f, false, 0, it->second, true); return true; }
            if (auto it = m->find("__co"); it != m->end()) { r.YieldCoroutine(0.0f, false, SafeInt(it->second.AsFloat())); return true; }
        }
        r.YieldCoroutine(0.0f, true, 0);   // null / WaitForEndOfFrame / anything else: next frame
        return true;
    }
};
EngineHooks g_engineHooks;   // stateless: one for every VM
} // namespace

void InitEngineRuntime(Runtime& rt) {
    rt.hooks = &g_engineHooks;
    rt.limits.runtimeErrorLines = false;          // engine messages stay as they were: no line on runtime errors
    rt.namedArgBuiltins.insert("StartCoroutine"); // StartCoroutine(Blink(delay: 0.2)) passes the names on
    rt.canonEvent = CanonEvent;
}

} // namespace script_vm

void OkayScriptVM::Impl::RegisterBuiltins() {
        InstallSharedApis();
        BuiltinCtx c(rt);
        RegisterDebugBuiltins(c);
        RegisterScriptUiBuiltins(c);
        RegisterMotionHelperBuiltins(c);
        RegisterInputBuiltins(c);
        RegisterScoreSpawnBuiltins(c);
        RegisterObjectPoolBuiltins(c);
        RegisterNamedObjectBuiltins(c);
        RegisterNpcBuiltins(c);
        RegisterSpawnerBuiltins(c);
        RegisterBuildModeBuiltins(c);
        RegisterMovementHelperBuiltins(c);
        RegisterCameraControlBuiltins(c);
        RegisterCameraRigBuiltins(c);
        RegisterUiByNameBuiltins(c);
        RegisterUiRuntimeBuiltins(c);
        RegisterUiControlBuiltins(c);
        RegisterBody2DBuiltins(c);
        RegisterSpriteEffectBuiltins(c);
        RegisterAudioSaveBuiltins(c);
        RegisterDisplayBuiltins(c);
        RegisterStoryBuiltins(c);
        RegisterNavigationBuiltins(c);
        RegisterGridPathBuiltins(c);
        RegisterNavmeshQueryBuiltins(c);
        RegisterBehaviorTreeBuiltins(c);
        RegisterSplineBuiltins(c);
        RegisterMusicBuiltins(c);
        RegisterRaycast2DBuiltins(c);
        RegisterSceneLoadBuiltins(c);
        RegisterCharacterAnimBuiltins(c);
        RegisterAnimStateMachineBuiltins(c);
        RegisterBlendShapeBuiltins(c);
        RegisterIkBuiltins(c);
        RegisterInventoryBuiltins(c);
        RegisterNetworkBuiltins(c);
        RegisterMatchmakingQueueBuiltins(c);
        RegisterMatchResultBuiltins(c);
        RegisterServerBrowserBuiltins(c);
        RegisterSteamBuiltins(c);
        RegisterFriendsBuiltins(c);
        RegisterLobbyBuiltins(c);
        RegisterPlayFabBuiltins(c);
        RegisterAccountBuiltins(c);
        RegisterEasingBuiltins(c);
        RegisterBitsJsonStringBuiltins(c);
        RegisterTransformHelperBuiltins(c);
        RegisterPhysicsQueryBuiltins(c);
        RegisterMaterialEventBuiltins(c);
        RegisterMathUtilityBuiltins(c);
        RegisterTweenBuiltins(c);
        RegisterFriendlyAliasBuiltins(c);
        RegisterCsApiBuiltins(c);
        RegisterVehicleBuiltins(c);
        RegisterPlainAliasBuiltins(c);
        RegisterCsCompatBuiltins(c);
        RegisterCoroutineBuiltins(c);
        RegisterObjectLifecycleBuiltins(c);
        RegisterComponentAccessBuiltins(c);
        CheckSharedApis();
}

OkayScriptVM::OkayScriptVM() : m_impl(std::make_unique<Impl>()) {
    m_impl->RegisterBuiltins();
}
OkayScriptVM::~OkayScriptVM() = default;

bool OkayScriptVM::Load(const std::string& source, std::string* error) {
    try {
        m_impl->rt.StopCoroutines("");
        m_impl->rt.pendingDestroy.clear();
        m_impl->rt.functions.clear();
        m_impl->rt.subs.clear();
        m_impl->rt.classes.clear();
        m_impl->rt.self = Value{};
        m_impl->rt.selfClass.clear();
        m_impl->rt.lambdas.clear();                  // they point into the program being replaced
        m_impl->rt.statics = Runtime::SharedStatics(source);
        m_impl->program = std::make_unique<okayscript::Program>();
        okayscript::ParseSource(source, *m_impl->program);   // as before, a syntax error leaves nothing loaded
        for (auto& kv : m_impl->program->functions) m_impl->rt.functions[kv.first] = &kv.second;
        m_impl->rt.BumpCalls();   // cached call targets in bytecode are stale
        for (auto& kv : m_impl->program->classes) m_impl->rt.classes[kv.first] = &kv.second;
        m_impl->rt.localNames = m_impl->program->localNames;
        const okayscript::StmtList& topLevel = m_impl->program->top;
        // Bare mode: no functions defined -> the top-level statements ARE the update
        // loop (run every frame), so a whole script can be a single line. Otherwise run
        // top-level once now for globals/setup.
        m_impl->bareMode = m_impl->rt.functions.empty() && !topLevel.empty();
        // Always run the top-level once now so globals/setup exist right after
        // Load (the contract every caller relies on). In bare mode the top-level
        // is also the per-frame loop, so this first run is the first tick — the
        // first Update skips re-running it to keep the overall tick count intact.
        {
            const okayscript::Flow f = m_impl->rt.RunBody(topLevel, m_impl->program->topCode);
            if (f == okayscript::Flow::Return) return true;   // a bare top-level return is harmless (as before, the
                                                                // script then counts as not loaded)
            if (f != okayscript::Flow::Normal) { m_impl->loaded = true; return true; }   // stray break/continue
        }
        m_impl->bareSkipFirstUpdate = m_impl->bareMode;
        m_impl->loaded = true;
        return true;
    } catch (const ScriptError& e) {
        // Prefix the source line when known, so editors can show a diagnostic.
        std::string msg = e.what();
        if (e.line > 0) {
            std::string where = "line " + std::to_string(e.line);
            if (e.column > 0) where += ", column " + std::to_string(e.column);
            msg = where + ": " + msg;
        }
        if (error) *error = msg;
        Log::Error("OkayScript: ", msg);
        m_impl->loaded = false;
        return false;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        Log::Error("OkayScript: ", e.what());
        m_impl->loaded = false;
        return false;
    }
}

void OkayScriptVM::AddBuiltin(const std::string& name, Builtin fn) {
    m_impl->rt.builtins[name] = std::move(fn);
}

vs::VsValue OkayScriptVM::CallBuiltin(const std::string& name, std::vector<vs::VsValue> args, std::string* error) {
    auto it = m_impl->rt.builtins.find(name);
    if (it == m_impl->rt.builtins.end()) {
        if (error) *error = "no text-script builtin named \"" + name + "\"";
        return {};
    }
    try {
        return it->second(args);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return {};
    }
}

std::vector<std::string> OkayScriptVM::BuiltinNames() const {
    std::vector<std::string> out;
    out.reserve(m_impl->rt.builtins.size());
    for (const auto& kv : m_impl->rt.builtins) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

bool OkayScriptVM::Validate(const std::string& source, std::string* error) {
    // Lex + parse ONLY — never Exec — so live editor diagnostics have no side
    // effects (no logging, no globals mutated, no scene calls). Catches syntax and
    // parse errors; runtime/semantic errors still surface on a real Load/Run.
    try {
        okayscript::Program throwaway;
        okayscript::ParseSource(source, throwaway);
        return true;
    } catch (const ScriptError& e) {
        if (error) {
            std::string msg = e.what();
            if (e.line > 0) {
                std::string where = "line " + std::to_string(e.line);
                if (e.column > 0) where += ", column " + std::to_string(e.column);
                msg = where + ": " + msg;
            }
            *error = msg;
        }
        return false;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return false;
    }
}

std::vector<ScriptDiagnostic> OkayScriptVM::ValidateAll(const std::string& source) {
    // Like Validate, but recovers after each parse error to report ALL of them for
    // the editor's Problems panel. Lexer errors (unterminated string, bad char) can't
    // be recovered from, so those surface as a single diagnostic.
    std::vector<ScriptDiagnostic> diags;
    for (const okayscript::Diagnostic& d : okayscript::ValidateSource(source))
        diags.push_back({d.line, d.message, d.severity, d.column});
    return diags;
}

void OkayScriptVM::Bind(ScriptHost* host) { m_impl->rt.host = host; }

namespace {
// Call the first of `names` that the script defines (with the given args).
// Lets a script use PascalCase (Start/Update) or the classic
// lowercase (start/update) names interchangeably.
void CallFirst(Runtime& rt, std::initializer_list<const char*> names,
               std::vector<Value>& args, const char* label) {
    for (const char* n : names) {
        if (rt.functions.count(n)) {
            try { rt.CallFunction(n, args); }   // the script's own, even when a builtin has its name
            catch (const std::exception& e) { Log::Error("OkayScript ", label, "(): ", e.what()); }
            return;
        }
    }
}
} // namespace

void OkayScriptVM::CallStart() {
    if (!m_impl->loaded) return;
    std::vector<Value> none;
    CallFirst(m_impl->rt, {"Awake", "awake"}, none, "Awake");   // Awake, then OnEnable, then Start
    CallFirst(m_impl->rt, {"OnEnable", "on_enable"}, none, "OnEnable");
    CallFirst(m_impl->rt, {"Start", "start"}, none, "Start");
}

void OkayScriptVM::CallUpdate(float deltaTime) {
    if (!m_impl->loaded) return;
    if (m_impl->rt.host) m_impl->rt.host->deltaTime = deltaTime;
    std::vector<Value> args{Value{deltaTime}};
    // Bare mode: re-run the top-level statements every frame (they ARE the loop).
    // Load already ran them once, so skip that first re-run to avoid double-ticking.
    if (m_impl->bareMode) {
        if (m_impl->bareSkipFirstUpdate) {
            m_impl->bareSkipFirstUpdate = false;
        } else {
            // A stray top-level return/break/continue ends this frame's run, like it does in Load.
            try {
                if (m_impl->program) m_impl->rt.RunBody(m_impl->program->top, m_impl->program->topCode);
            }
            catch (const std::exception& e) { Log::Error("OkayScript (bare): ", e.what()); }
        }
    }
    CallFirst(m_impl->rt, {"Update", "update"}, args, "Update");
    CallFirst(m_impl->rt, {"LateUpdate", "late_update"}, args, "LateUpdate");
    // Scheduled after()/every() callbacks tick even without an update().
    if (!m_impl->rt.timers.empty()) {
        try { m_impl->rt.TickTimers(deltaTime); }
        catch (const std::exception& e) { Log::Error("OkayScript timer: ", e.what()); }
    }
    Runtime& rt = m_impl->rt;
    if (!rt.coroutines.empty()) {
        try { rt.TickCoroutines(deltaTime); }
        catch (const std::exception& e) { Log::Error("OkayScript coroutine: ", e.what()); }
    }
    ++rt.frame;
    // Destroy(obj, seconds) countdowns.
    if (!rt.pendingDestroy.empty() && rt.host && rt.host->gameObject && rt.host->gameObject->scene()) {
        Scene* sc = rt.host->gameObject->scene();
        std::vector<Value> due;
        for (auto& pd : rt.pendingDestroy) { pd.first -= deltaTime; if (pd.first <= 0.0f) due.push_back(pd.second); }
        rt.pendingDestroy.erase(std::remove_if(rt.pendingDestroy.begin(), rt.pendingDestroy.end(),
                                               [](const auto& pd) { return pd.first <= 0.0f; }), rt.pendingDestroy.end());
        GameObject* me = rt.host->gameObject;
        for (const auto& n : due) {
            GameObject* g = (!n.IsObject() && n.AsString().empty()) ? me : ResolveObject(sc, n);
            if (g) sc->Destroy(g);
            if (g == me) break;   // this script is going away with its object
        }
    }
}

void OkayScriptVM::CallEvent(const std::string& function) {
    CallEventWith(function, vs::VsValue{});
}

namespace {
const std::unordered_map<std::string, std::vector<std::string>>& EventAliases() {
    static const std::unordered_map<std::string, std::vector<std::string>> kAliases = {
        // Inventory events (C#-style handler names work too).
        {"on_item_added",       {"OnItemAdded"}},
        {"on_item_removed",     {"OnItemRemoved"}},
        {"on_inventory_full",   {"OnInventoryFull"}},
        {"on_item_used",        {"OnItemUsed"}},
        {"on_item_dropped",     {"OnItemDropped"}},
        {"on_item_picked_up",   {"OnItemPickedUp"}},
        {"on_equip",            {"OnEquip"}},
        {"on_unequip",          {"OnUnequip"}},
        {"on_craft",            {"OnCraft"}},
        {"on_craft_failed",     {"OnCraftFailed"}},
        {"on_chest_opened",     {"OnChestOpened"}},
        {"on_chest_closed",     {"OnChestClosed"}},
        // Physics — distinct enter/stay/exit. The legacy on_trigger/on_collision
        // are accepted as enter handlers so older scripts keep working.
        {"on_trigger_enter",   {"OnTriggerEnter", "OnTriggerEnter2D", "on_trigger"}},
        {"on_trigger_stay",    {"OnTriggerStay",  "OnTriggerStay2D"}},
        {"on_trigger_exit",    {"OnTriggerExit",  "OnTriggerExit2D"}},
        {"on_collision_enter", {"OnCollisionEnter", "OnCollisionEnter2D", "on_collision"}},
        {"on_collision_stay",  {"OnCollisionStay", "OnCollisionStay2D"}},
        {"on_collision_exit",  {"OnCollisionExit", "OnCollisionExit2D"}},
        {"on_collision", {"OnCollisionEnter", "OnCollisionEnter2D"}},
        {"on_trigger",   {"OnTriggerEnter",   "OnTriggerEnter2D"}},
        // Pointer / mouse.
        {"on_mouse_enter", {"OnMouseEnter", "OnPointerEnter"}},
        {"on_mouse_exit",  {"OnMouseExit",  "OnPointerExit"}},
        {"on_mouse_over",  {"OnMouseOver"}},
        {"on_mouse_down",  {"OnMouseDown"}},
        {"on_mouse_up",    {"OnMouseUp", "OnMouseUpAsButton"}},
        {"on_click",     {"OnClick", "OnMouseClick", "OnMouseUpAsButton", "OnPointerClick"}},
        {"on_modal_close", {"OnModalClose"}},   // grok/ui-toast
        {"on_change",    {"OnValueChanged"}},
        {"on_submit",    {"OnSubmit"}},
        {"on_tab_close", {"OnTabClose"}},
        {"on_toggle",    {"OnToggle", "OnValueChanged"}},
        {"on_drop",      {"OnDrop"}},
        {"on_receive",   {"OnReceive"}},
        {"on_drag",      {"OnDrag"}},
        {"on_drag_start",{"OnBeginDrag"}},
        // Lifecycle the ScriptComponent drives.
        {"on_fixed_update", {"FixedUpdate", "fixed_update"}},
        {"on_enable",       {"OnEnable"}},
        {"on_disable",      {"OnDisable"}},
        {"on_destroy",      {"OnDestroy"}},
        // Build mode / logic links (BuildMode, LogicTarget).
        {"on_object_placed",  {"OnObjectPlaced"}},
        {"on_object_removed", {"OnObjectRemoved"}},
        {"on_logic_signal",   {"OnLogicSignal"}},
        // Anti-cheat (AntiCheatConfig raises these on the host).
        {"on_cheat_detected", {"OnCheatDetected"}},
        {"on_validate_rpc",   {"OnValidateRpc"}},
        {"on_validate_hit",   {"OnValidateHit"}},
        // Cutscenes (SequencePlayer).
        {"on_sequence_event",    {"OnSequenceEvent"}},
        {"on_sequence_started",  {"OnSequenceStarted"}},
        {"on_sequence_finished", {"OnSequenceFinished"}},
        // Localization (the player picked another language).
        {"on_language_changed",  {"OnLanguageChanged"}},
        // Splines (SplineFollower: "end" or "start").
        {"on_spline_end",        {"OnSplineEnd"}},
    };
    return kAliases;
}

// The handler a script defines for an engine event name: the requested name
// wins; otherwise an alias, so scripts can name handlers in PascalCase
// (OnCollisionEnter, OnClick, ...). "" = none.
template <class Functions>
std::string ResolveEventHandler(const Functions& functions, const std::string& function) {
    const auto& kAliases = EventAliases();
    if (functions.count(function)) return function;
    auto it = kAliases.find(function);
    if (it != kAliases.end())
        for (const auto& alt : it->second)
            if (functions.count(alt)) return alt;
    return {};
}

// "OnClick" and "on_click" are the same event. A name that isn't in the table
// (OnHurt, OnEvent_Hit) stays as written.
std::string CanonEvent(const std::string& name) {
    const auto& kAliases = EventAliases();
    if (kAliases.count(name)) return name;
    for (const auto& kv : kAliases)
        for (const auto& alt : kv.second)
            if (alt == name) return kv.first;
    return name;
}

} // namespace

void OkayScriptVM::CallEventWith(const std::string& function, const vs::VsValue& arg) {
    CallEventArgs(function, std::vector<vs::VsValue>{arg});
}

bool OkayScriptVM::HasEvent(const std::string& function) const {
    return m_impl->loaded &&
           (!ResolveEventHandler(m_impl->rt.functions, function).empty() || m_impl->rt.HasSub(function));
}

vs::VsValue OkayScriptVM::CallEventArgs(const std::string& function, const std::vector<vs::VsValue>& given,
                                        std::string* error) {
    if (!m_impl->loaded) return {};
    std::vector<Value> all(given.begin(), given.end());
    const std::string fn = ResolveEventHandler(m_impl->rt.functions, function);
    vs::VsValue ret;
    if (!fn.empty()) {
        // Up to as many arguments as the handler declares (OnTriggerEnter(Collider
        // other) takes one; a handler with none gets none). Call gives any it
        // declares beyond those its usual default for the parameter's type.
        const std::size_t want = std::min(m_impl->rt.functions[fn]->params.size(), all.size());
        std::vector<Value> args(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(want));
        try { ret = m_impl->rt.CallFunction(fn, args); }   // the script's own, even when a builtin has its name (shake)
        catch (const std::exception& e) {
            if (error) *error = e.what();
            else Log::Error("OkayScript ", fn.c_str(), "(): ", e.what());
        }
    }
    // Listeners added with += run as well, including when no function is defined.
    try { m_impl->rt.FireSubs(function, all); }
    catch (const std::exception& e) {
        if (error && error->empty()) *error = e.what();
        else if (!error) Log::Error("OkayScript ", function.c_str(), " listener: ", e.what());
    }
    return ret;
}

vs::VsValue OkayScriptVM::GetGlobal(const std::string& name) const {
    auto& g = m_impl->rt.Global().vars;
    auto it = g.find(name);
    return it != g.end() ? it->second : vs::VsValue{};
}

std::vector<std::string> OkayScriptVM::GlobalNames() const {
    std::vector<std::string> out;
    for (const auto& kv : m_impl->rt.Global().vars) out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

void OkayScriptVM::SetGlobal(const std::string& name, const vs::VsValue& v) {
    m_impl->rt.SetGlobalChecked(name, v);
}

} // namespace okay
