// The engine binding registry: every type, property and method a script can use.
// Each one calls real engine code; there are no placeholders. The checker
// resolves names against this table and the editor's autocomplete lists it.
#include "ScriptInternal.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Components/PlayerView.hpp"
#include "okay/Components/UIColorPicker.hpp"   // grok/ui-color
#include "okay/Components/Crosshair.hpp"          // grok/ui-hud
#include "okay/Components/UIRadialProgress.hpp"   // grok/ui-hud
#include "okay/Components/UISlotGrid.hpp"         // grok/ui-hud
#include "okay/Components/UIDropdown.hpp"      // grok/ui-controls
#include "okay/Components/UIScrollView.hpp"
#include "okay/Components/UITabs.hpp"
#include "okay/Components/UITooltip.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UIToast.hpp"    // grok/ui-toast
#include "okay/Components/UIModal.hpp"    // grok/ui-toast
#include "okay/Components/UITheme.hpp"          // grok/ui-theme
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "okay/Core/SaveGame.hpp"
#include "okay/Components/SequencePlayer.hpp"
#include "okay/Render/Atmosphere.hpp"   // PostProcess / Fog / Weather
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/DialogueRunner.hpp"   // Dialogue / Story / Quest statics
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/StoryState.hpp"
#include "okay/Components/BehaviorTreeRunner.hpp"
#include <cmath>
#include <set>

namespace okay::script {

GameObject* Resolve(const ObjRef& r, SrcLoc loc) {
    if (r.go == 0) throw RuntimeError("This reference is null (it doesn't point at an object)", loc);
    GameObject* g = GameObject::FromInstanceId(r.go);
    if (!g) throw RuntimeError("This object was destroyed, so it can't be used any more", loc);
    return g;
}

Value MakeRef(GameObject* go, int apiType) {
    if (!go) return Value{};
    return Value{ObjRef{go->InstanceId(), apiType}};
}

int EngineApi::Find(const std::string& name) const {
    for (std::size_t i = 0; i < types.size(); ++i) if (types[i].name == name) return (int)i;
    return -1;
}

namespace {

float F(const Value& v) {
    if (auto* f = std::get_if<float>(&v)) return *f;
    if (auto* i = std::get_if<std::int32_t>(&v)) return (float)*i;
    return 0.0f;
}
std::int32_t I(const Value& v) { if (auto* i = std::get_if<std::int32_t>(&v)) return *i; return (std::int32_t)F(v); }
const std::string& S(const Value& v) { static const std::string e; auto* s = std::get_if<std::string>(&v); return s ? *s : e; }
Vec3 V(const Value& v) { if (auto* x = std::get_if<Vec3>(&v)) return *x; return Vec3::Zero; }

Transform* SelfTransform(const Value& self, CallContext& c) {
    return Resolve(std::get<ObjRef>(self), c.loc)->transform;
}

// Key names a script may use, and the engine key each maps to.
const std::unordered_map<std::string, char>& KeyNames() {
    static const std::unordered_map<std::string, char> k = [] {
        std::unordered_map<std::string, char> m;
        for (char c = 'a'; c <= 'z'; ++c) m[std::string(1, c)] = c;
        for (char c = '0'; c <= '9'; ++c) m[std::string(1, c)] = c;
        m["space"] = ' '; m["enter"] = '\r'; m["escape"] = (char)27; m["tab"] = '\t'; m["backspace"] = (char)8;
        m["shift"] = Input::KeyShift; m["ctrl"] = Input::KeyCtrl;
        // The arrow keys arrive as W/A/S/D, so these read the same keys.
        m["up"] = 'w'; m["down"] = 's'; m["left"] = 'a'; m["right"] = 'd';
        return m;
    }();
    return k;
}
std::string KeyListHint() { return "a-z, 0-9, space, enter, escape, tab, backspace, shift, ctrl, up, down, left, right"; }
char KeyOf(const std::string& name, SrcLoc loc) {
    std::string k;
    for (char c : name) k += (char)std::tolower((unsigned char)c);
    auto it = KeyNames().find(k);
    if (it == KeyNames().end()) throw RuntimeError("\"" + name + "\" isn't a key name. Use one of: " + KeyListHint(), loc);
    return it->second;
}
std::string CheckKeyLiteral(int, const std::string& lit) {
    std::string k;
    for (char c : lit) k += (char)std::tolower((unsigned char)c);
    return KeyNames().count(k) ? "" : "\"" + lit + "\" isn't a key name. Use one of: " + KeyListHint();
}
std::string CheckAxisLiteral(int, const std::string& lit) {
    return (lit == "Horizontal" || lit == "Vertical") ? "" : "\"" + lit + "\" isn't an axis. Use \"Horizontal\" or \"Vertical\"";
}
std::string CheckMouseButtonLiteral(int, const std::string&) { return ""; }

Vec2 Axes() {
    Vec2 a = Input::AxisWASD();
    Vec2 pad = Input::GamepadAxis();
    if (std::fabs(pad.x) > std::fabs(a.x)) a.x = pad.x;
    if (std::fabs(pad.y) > std::fabs(a.y)) a.y = pad.y;
    return a;
}

std::string LogText(const Value& v) { return ValueToString(v); }

Rigidbody3D* Body(const Value& self, CallContext& c) {
    GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
    auto* rb = g->GetComponent<Rigidbody3D>();
    if (!rb) throw RuntimeError("'" + g->name + "' has no Rigidbody (add one in the Inspector)", c.loc);
    return rb;
}

EngineApi Build() {
    EngineApi api;
    const Type tFloat = Type::Of(TK::Float), tInt = Type::Of(TK::Int), tBool = Type::Of(TK::Bool),
               tString = Type::Of(TK::String), tVec = Type::Of(TK::Vector3), tVoid = Type::Of(TK::Void),
               tAny = Type::Of(TK::Any);
    auto prop = [](std::string n, Type t, auto get, auto set, std::string doc) {
        ApiProperty p; p.name = std::move(n); p.type = t; p.get = get; p.set = set; p.doc = std::move(doc); return p;
    };
    auto ro = [](std::string n, Type t, auto get, std::string doc) {
        ApiProperty p; p.name = std::move(n); p.type = t; p.get = get; p.doc = std::move(doc); return p;
    };
    auto method = [](std::string n, std::vector<Type> params, std::vector<std::string> names, Type ret, auto fn, std::string doc) {
        ApiMethod m; m.name = std::move(n); m.params = std::move(params); m.paramNames = std::move(names);
        m.ret = ret; m.fn = fn; m.doc = std::move(doc); return m;
    };

    // ---- GameObject ----
    api.gameObject = (int)api.types.size();
    api.types.push_back({});
    // ---- Transform ----
    api.transform = (int)api.types.size();
    api.types.push_back({});
    // ---- Vector3 ----
    api.vector3 = (int)api.types.size();
    api.types.push_back({});
    const Type tGO = Type::Obj(api.gameObject), tTr = Type::Obj(api.transform);
    const int goIdx = api.gameObject, trIdx = api.transform;

    {
        ApiType& g = api.types[(std::size_t)api.gameObject];
        g.name = "GameObject";
        g.doc = "An object in the scene.";
        g.props.push_back(prop("name", tString,
            [](const Value& s, CallContext& c) { return Value{Resolve(std::get<ObjRef>(s), c.loc)->name}; },
            [](const Value& s, const Value& v, CallContext& c) { Resolve(std::get<ObjRef>(s), c.loc)->name = S(v); },
            "The object's name (as shown in the Hierarchy)."));
        g.props.push_back(prop("tag", tString,
            [](const Value& s, CallContext& c) { return Value{Resolve(std::get<ObjRef>(s), c.loc)->tag}; },
            [](const Value& s, const Value& v, CallContext& c) { Resolve(std::get<ObjRef>(s), c.loc)->tag = S(v); },
            "The object's tag."));
        g.props.push_back(prop("active", tBool,
            [](const Value& s, CallContext& c) { return Value{Resolve(std::get<ObjRef>(s), c.loc)->active}; },
            [](const Value& s, const Value& v, CallContext& c) { Resolve(std::get<ObjRef>(s), c.loc)->active = std::get<bool>(v); },
            "Whether the object is active. Inactive objects don't update or draw."));
        g.props.push_back(ro("transform", tTr,
            [trIdx](const Value& s, CallContext& c) { return MakeRef(Resolve(std::get<ObjRef>(s), c.loc), trIdx); },
            "The object's Transform (position, rotation, scale)."));
        g.staticMethods.push_back(method("Find", {tString}, {"name"}, tGO,
            [goIdx](const Value&, std::vector<Value>& a, CallContext& c) {
                Scene* sc = c.self ? c.self->scene() : nullptr;
                return sc ? MakeRef(sc->Find(S(a[0])), goIdx) : Value{};
            }, "The first active object with this name, or null if there is none."));
        g.staticMethods.push_back(method("FindWithTag", {tString}, {"tag"}, tGO,
            [goIdx](const Value&, std::vector<Value>& a, CallContext& c) {
                Scene* sc = c.self ? c.self->scene() : nullptr;
                return sc ? MakeRef(sc->FindWithTag(S(a[0])), goIdx) : Value{};
            }, "The first active object with this tag, or null if there is none."));
    }
    {
        ApiType& t = api.types[(std::size_t)api.transform];
        t.name = "Transform";
        t.isComponent = true;
        t.doc = "Position, rotation and scale of an object.";
        t.find = [](GameObject* go) -> Component* { return go ? go->transform : nullptr; };
        t.props.push_back(prop("position", tVec,
            [](const Value& s, CallContext& c) { return Value{SelfTransform(s, c)->Position()}; },
            [](const Value& s, const Value& v, CallContext& c) { SelfTransform(s, c)->SetPosition(V(v)); },
            "Position in the world."));
        t.props.push_back(prop("localPosition", tVec,
            [](const Value& s, CallContext& c) { return Value{SelfTransform(s, c)->localPosition}; },
            [](const Value& s, const Value& v, CallContext& c) { SelfTransform(s, c)->localPosition = V(v); },
            "Position relative to the parent."));
        t.props.push_back(prop("eulerAngles", tVec,
            [](const Value& s, CallContext& c) { return Value{SelfTransform(s, c)->Rotation().ToEuler()}; },
            [](const Value& s, const Value& v, CallContext& c) { SelfTransform(s, c)->SetRotation(Quat::Euler(V(v))); },
            "Rotation in the world, as X / Y / Z angles in degrees."));
        t.props.push_back(prop("localEulerAngles", tVec,
            [](const Value& s, CallContext& c) { return Value{SelfTransform(s, c)->localRotation.ToEuler()}; },
            [](const Value& s, const Value& v, CallContext& c) { SelfTransform(s, c)->localRotation = Quat::Euler(V(v)); },
            "Rotation relative to the parent, in degrees."));
        t.props.push_back(prop("localScale", tVec,
            [](const Value& s, CallContext& c) { return Value{SelfTransform(s, c)->localScale}; },
            [](const Value& s, const Value& v, CallContext& c) { SelfTransform(s, c)->localScale = V(v); },
            "Scale relative to the parent."));
        t.props.push_back(ro("forward", tVec, [](const Value& s, CallContext& c) { return Value{SelfTransform(s, c)->Forward()}; },
            "The direction the object faces (its +Z), in the world."));
        t.props.push_back(ro("right", tVec, [](const Value& s, CallContext& c) { return Value{SelfTransform(s, c)->Right()}; },
            "The object's right (+X), in the world."));
        t.props.push_back(ro("up", tVec, [](const Value& s, CallContext& c) { return Value{SelfTransform(s, c)->Up()}; },
            "The object's up (+Y), in the world."));
        t.props.push_back(ro("gameObject", tGO,
            [goIdx](const Value& s, CallContext& c) { return MakeRef(Resolve(std::get<ObjRef>(s), c.loc), goIdx); },
            "The object this Transform belongs to."));
        t.methods.push_back(method("Translate", {tVec}, {"offset"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) { SelfTransform(s, c)->Translate(V(a[0])); return Value{}; },
            "Move by an offset (relative to the parent)."));
        t.methods.push_back(method("Translate", {tFloat, tFloat, tFloat}, {"x", "y", "z"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) { SelfTransform(s, c)->Translate({F(a[0]), F(a[1]), F(a[2])}); return Value{}; },
            "Move by x, y, z (relative to the parent)."));
        t.methods.push_back(method("Rotate", {tVec}, {"degrees"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) { SelfTransform(s, c)->Rotate(V(a[0])); return Value{}; },
            "Turn by X / Y / Z degrees around the object's own axes."));
        t.methods.push_back(method("Rotate", {tFloat, tFloat, tFloat}, {"x", "y", "z"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) { SelfTransform(s, c)->Rotate({F(a[0]), F(a[1]), F(a[2])}); return Value{}; },
            "Turn by x, y, z degrees around the object's own axes."));
        t.methods.push_back(method("LookAt", {tVec}, {"target"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                Transform* tr = SelfTransform(s, c);
                Vec3 d = V(a[0]) - tr->Position();
                if (d.Magnitude() > 1e-6f) tr->SetRotation(Quat::LookRotation(d));
                return Value{};
            }, "Turn so forward points at a world position."));
    }
    {
        ApiType& v = api.types[(std::size_t)api.vector3];
        v.name = "Vector3";
        v.isValueType = true;
        v.doc = "A point or direction: x, y, z.";
        v.props.push_back(ro("magnitude", tFloat, [](const Value& s, CallContext&) { return Value{V(s).Magnitude()}; }, "The length."));
        v.props.push_back(ro("normalized", tVec, [](const Value& s, CallContext&) { return Value{V(s).Normalized()}; }, "The same direction with length 1."));
        auto constant = [&](const char* n, Vec3 c, const char* doc) {
            v.staticProps.push_back(ro(n, tVec, [c](const Value&, CallContext&) { return Value{c}; }, doc));
        };
        constant("zero", Vec3::Zero, "(0, 0, 0)");
        constant("one", Vec3::One, "(1, 1, 1)");
        constant("up", Vec3::Up, "(0, 1, 0)");
        constant("down", Vec3::Down, "(0, -1, 0)");
        constant("right", Vec3::Right, "(1, 0, 0)");
        constant("left", Vec3::Left, "(-1, 0, 0)");
        constant("forward", Vec3::Forward, "(0, 0, 1)");
        constant("back", Vec3{0, 0, -1}, "(0, 0, -1)");
        v.staticMethods.push_back(method("Distance", {tVec, tVec}, {"a", "b"}, tFloat,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{Vec3::Distance(V(a[0]), V(a[1]))}; }, "Distance between two points."));
        v.staticMethods.push_back(method("Dot", {tVec, tVec}, {"a", "b"}, tFloat,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{Vec3::Dot(V(a[0]), V(a[1]))}; }, "Dot product."));
        v.staticMethods.push_back(method("Cross", {tVec, tVec}, {"a", "b"}, tVec,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{Vec3::Cross(V(a[0]), V(a[1]))}; }, "Cross product."));
        v.staticMethods.push_back(method("Lerp", {tVec, tVec, tFloat}, {"a", "b", "t"}, tVec,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{Vec3::LerpUnclamped(V(a[0]), V(a[1]), Mathf::Clamp(F(a[2]), 0.0f, 1.0f))}; },
            "Blend from a (t = 0) to b (t = 1)."));
        v.staticMethods.push_back(method("MoveTowards", {tVec, tVec, tFloat}, {"current", "target", "maxDistance"}, tVec,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{Vec3::MoveTowards(V(a[0]), V(a[1]), F(a[2]))}; },
            "Step from current toward target, moving at most maxDistance."));
    }
    // ---- Rigidbody ----
    {
        api.rigidbody = (int)api.types.size();
        ApiType r; r.name = "Rigidbody"; r.isComponent = true;
        r.doc = "A physics body: velocity, mass and forces. The object needs a Rigidbody component.";
        r.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<Rigidbody3D>() : nullptr; };
        r.props.push_back(prop("velocity", tVec,
            [](const Value& s, CallContext& c) { return Value{Body(s, c)->velocity}; },
            [](const Value& s, const Value& v, CallContext& c) { Rigidbody3D* b = Body(s, c); b->velocity = V(v); b->WakeUp(); },
            "Speed and direction in units per second."));
        r.props.push_back(prop("angularVelocity", tVec,
            [](const Value& s, CallContext& c) { return Value{Body(s, c)->angularVelocity}; },
            [](const Value& s, const Value& v, CallContext& c) { Rigidbody3D* b = Body(s, c); b->angularVelocity = V(v); b->WakeUp(); },
            "Spin around X / Y / Z in radians per second."));
        r.props.push_back(prop("mass", tFloat,
            [](const Value& s, CallContext& c) { return Value{Body(s, c)->mass}; },
            [](const Value& s, const Value& v, CallContext& c) {
                if (F(v) <= 0.0f) throw RuntimeError("mass must be more than 0", c.loc);
                Body(s, c)->mass = F(v);
            }, "How heavy the body is (more than 0)."));
        r.props.push_back(prop("gravityScale", tFloat,
            [](const Value& s, CallContext& c) { return Value{Body(s, c)->gravityScale}; },
            [](const Value& s, const Value& v, CallContext& c) { Body(s, c)->gravityScale = F(v); },
            "How strongly gravity pulls it: 1 normal, 0 none."));
        r.props.push_back(prop("drag", tFloat,
            [](const Value& s, CallContext& c) { return Value{Body(s, c)->drag}; },
            [](const Value& s, const Value& v, CallContext& c) { Body(s, c)->drag = std::max(0.0f, F(v)); },
            "How quickly it slows down by itself (0 = never)."));
        r.props.push_back(prop("isKinematic", tBool,
            [](const Value& s, CallContext& c) { return Value{Body(s, c)->bodyType == Rigidbody3D::BodyType::Kinematic}; },
            [](const Value& s, const Value& v, CallContext& c) {
                Body(s, c)->bodyType = std::get<bool>(v) ? Rigidbody3D::BodyType::Kinematic : Rigidbody3D::BodyType::Dynamic;
            }, "When true, forces and gravity don't move it; only scripts do."));
        r.methods.push_back(method("AddForce", {tVec}, {"force"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) { Body(s, c)->AddForce(V(a[0])); return Value{}; },
            "Push it, over the next physics step (heavier bodies move less)."));
        r.methods.push_back(method("AddImpulse", {tVec}, {"impulse"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) { Body(s, c)->AddImpulse(V(a[0])); return Value{}; },
            "Kick it at once (a jump, a hit)."));
        r.methods.push_back(method("AddTorque", {tVec}, {"torque"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) { Body(s, c)->AddTorque(V(a[0])); return Value{}; },
            "Twist it around X / Y / Z."));
        r.methods.push_back(method("AddForceAtPosition", {tVec, tVec}, {"force", "point"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) { Body(s, c)->AddForceAtPosition(V(a[0]), V(a[1])); return Value{}; },
            "Push it at a world point (off-centre pushes also spin it)."));
        api.types.push_back(std::move(r));
    }
    // ---- PlayerView (grok/view-toggle). Visual-script nodes come from this type. ----
    {
        ApiType pv; pv.name = "PlayerView"; pv.isComponent = true;
        pv.doc = "Switches one player between first person and third person. The object needs a Player View.";
        pv.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<PlayerView>() : nullptr; };
        auto viewOf = [](const Value& self, CallContext& c) -> PlayerView* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* v = g->GetComponent<PlayerView>();
            if (!v) throw RuntimeError("'" + g->name + "' has no Player View (add one in the Inspector)", c.loc);
            return v;
        };
        pv.methods.push_back(method("SetView", {tInt}, {"view"}, tVoid,
            [viewOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                viewOf(s, c)->SetView(I(a[0]));
                return Value{};
            }, "Ease to a view: 0 first person (camera at the eyes), 1 third person (over the shoulder)."));
        ApiMethod get = method("GetView", {}, {}, tInt,
            [viewOf](const Value& s, std::vector<Value>&, CallContext& c) {
                return Value{(std::int32_t)viewOf(s, c)->GetView()};
            }, "The view asked for: 0 first person, 1 third person. The camera may still be on its way there.");
        get.pure = true;
        pv.methods.push_back(std::move(get));
        api.types.push_back(std::move(pv));
    }
    // ---- grok/ui-color ----
    {
        ApiType cp; cp.name = "UIColorPicker"; cp.isComponent = true;
        cp.doc = "A row of color swatches. The chosen swatch is the current color.";
        cp.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIColorPicker>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UIColorPicker* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* p = g->GetComponent<UIColorPicker>();
            if (!p) throw RuntimeError("'" + g->name + "' has no Color Picker (add one in the Inspector)", c.loc);
            return p;
        };
        cp.methods.push_back(method("SetValue", {tInt}, {"index"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetValue(I(a[0])); return Value{}; },
            "Select a swatch by index. The chip shows that color."));
        ApiMethod val = method("Value", {}, {}, tInt,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{(std::int32_t)of(s, c)->Value()}; },
            "The selected swatch, or -1.");
        val.pure = true;
        cp.methods.push_back(std::move(val));
        api.types.push_back(std::move(cp));
    }
    // ---- grok/ui-hud: crosshair recovery, radial options, slot grid ----
    {
        auto need = [](auto* comp, GameObject* g, CallContext& c, const char* what) {
            if (!comp) throw RuntimeError("'" + g->name + "' has no " + std::string(what) + " (add one in the Inspector)", c.loc);
            return comp;
        };
        {
            ApiType ch; ch.name = "Crosshair"; ch.isComponent = true;
            ch.doc = "An aim reticle. SpreadRecover pulls an extra gap back to 0; 0 keeps the authored spread.";
            ch.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<Crosshair>() : nullptr; };
            auto of = [need](const Value& self, CallContext& c) -> Crosshair* {
                GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
                return need(g->GetComponent<Crosshair>(), g, c, "Crosshair");
            };
            ch.methods.push_back(method("SetSpread", {tFloat}, {"pixels"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetSpread(F(a[0])); return Value{}; },
                "Set the extra gap around the ticks, in pixels."));
            ch.methods.push_back(method("SetSpreadRecover", {tFloat}, {"pixelsPerSecond"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetSpreadRecover(F(a[0])); return Value{}; },
                "How fast the extra gap eases back to 0. 0 leaves the spread where it is."));
            ch.methods.push_back(method("KickSpread", {tFloat}, {"pixels"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->KickSpread(F(a[0])); return Value{}; },
                "Push the ticks apart. They ease back while spread recover is above 0."));
            ApiMethod spread = method("Spread", {}, {}, tFloat,
                [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Spread()}; },
                "The extra gap around the ticks, in pixels.");
            spread.pure = true;
            ch.methods.push_back(std::move(spread));
            api.types.push_back(std::move(ch));
        }
        {
            ApiType rp; rp.name = "UIRadialProgress"; rp.isComponent = true;
            rp.doc = "A ring or pie. Thickness 0 is a pie. Spin turns the arc into a loader.";
            rp.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIRadialProgress>() : nullptr; };
            auto of = [need](const Value& self, CallContext& c) -> UIRadialProgress* {
                GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
                return need(g->GetComponent<UIRadialProgress>(), g, c, "Radial Progress");
            };
            auto flag = [](const Value& v) {
                if (auto* b = std::get_if<bool>(&v)) return *b;
                return F(v) != 0.0f;
            };
            rp.methods.push_back(method("SetValue", {tFloat}, {"value"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetValue(F(a[0])); return Value{}; },
                "Fill fraction from 0 to 1."));
            rp.methods.push_back(method("SetThickness", {tFloat}, {"pixels"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetThickness(F(a[0])); return Value{}; },
                "Ring width in pixels. 0 draws a pie."));
            rp.methods.push_back(method("SetStartAngle", {tFloat}, {"degrees"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetStartAngle(F(a[0])); return Value{}; },
                "Where the arc starts. 0 is 12 o'clock."));
            rp.methods.push_back(method("SetClockwise", {tBool}, {"on"}, tVoid,
                [of, flag](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetClockwise(flag(a[0])); return Value{}; },
                "Fill clockwise when on."));
            rp.methods.push_back(method("SetSpin", {tBool}, {"on"}, tVoid,
                [of, flag](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetSpin(flag(a[0])); return Value{}; },
                "Spin the arc as a loader."));
            rp.methods.push_back(method("SetShowPercent", {tBool}, {"on"}, tVoid,
                [of, flag](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetShowPercent(flag(a[0])); return Value{}; },
                "Draw the fill as a percent in the middle."));
            api.types.push_back(std::move(rp));
        }
        {
            ApiType sg; sg.name = "UISlotGrid"; sg.isComponent = true;
            sg.doc = "A grid of buttons. Selecting one fires on_change. It does not know about inventory.";
            sg.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UISlotGrid>() : nullptr; };
            auto of = [need](const Value& self, CallContext& c) -> UISlotGrid* {
                GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
                return need(g->GetComponent<UISlotGrid>(), g, c, "Slot Grid");
            };
            sg.methods.push_back(method("SetValue", {tInt}, {"index"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetValue(I(a[0])); return Value{}; },
                "Select a slot by index. -1 clears the selection."));
            sg.methods.push_back(method("SetColumns", {tInt}, {"columns"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetColumns(I(a[0])); of(s, c)->Rebuild(); return Value{}; },
                "How many slots across (1 to 16)."));
            sg.methods.push_back(method("SetRows", {tInt}, {"rows"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetRows(I(a[0])); of(s, c)->Rebuild(); return Value{}; },
                "How many slots down (1 to 16)."));
            ApiMethod val = method("Value", {}, {}, tInt,
                [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{(std::int32_t)of(s, c)->Value()}; },
                "The selected slot, or -1.");
            val.pure = true;
            sg.methods.push_back(std::move(val));
            api.types.push_back(std::move(sg));
        }
    }
    // ---- grok/ui-controls: visual-script nodes for the new widget options. ----
    {
        auto need = [](auto* c, const char* what, CallContext& ctx) {
            if (!c) throw RuntimeError(std::string("This object has no ") + what, ctx.loc);
            return c;
        };
        {
            ApiType dd; dd.name = "UIDropdown"; dd.isComponent = true;
            dd.doc = "A dropdown. Search, a row cap and per-option glyphs are optional.";
            dd.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIDropdown>() : nullptr; };
            auto of = [need](const Value& self, CallContext& c) {
                return need(Resolve(std::get<ObjRef>(self), c.loc)->GetComponent<UIDropdown>(), "UI Dropdown", c);
            };
            dd.methods.push_back(method("SetSearchable", {tBool}, {"on"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetSearchable(std::get<bool>(a[0])); return Value{}; },
                "Show a search row that filters the options while the list is open."));
            dd.methods.push_back(method("SetMaxVisible", {tInt}, {"rows"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetMaxVisible(I(a[0])); return Value{}; },
                "How many matching options the open list shows. 0 shows every match."));
            dd.methods.push_back(method("SetOptionIcon", {tInt, tString}, {"index", "glyph"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetOptionIcon(I(a[0]), S(a[0 + 1])); return Value{}; },
                "A short glyph drawn in front of one option."));
            ApiMethod q = method("Searchable", {}, {}, tBool,
                [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->Searchable()}; },
                "True when the open list has a search row.");
            q.pure = true; dd.methods.push_back(std::move(q));
            api.types.push_back(std::move(dd));
        }
        {
            ApiType sv; sv.name = "UIScrollView"; sv.isComponent = true;
            sv.doc = "A scroll view. Inertia, elastic edges and the bar style are optional.";
            sv.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIScrollView>() : nullptr; };
            auto of = [need](const Value& self, CallContext& c) {
                return need(Resolve(std::get<ObjRef>(self), c.loc)->GetComponent<UIScrollView>(), "UI Scroll View", c);
            };
            sv.methods.push_back(method("SetInertia", {tFloat}, {"rate"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetInertia(F(a[0])); return Value{}; },
                "Wheel glide. 0 scrolls one notch immediately. Higher stops the glide sooner."));
            sv.methods.push_back(method("SetElastic", {tBool}, {"on"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetElastic(std::get<bool>(a[0])); return Value{}; },
                "Let the content pass the ends, then spring back."));
            sv.methods.push_back(method("SetBarStyle", {tInt}, {"style"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetBarStyle(I(a[0])); return Value{}; },
                "Scrollbar: 0 the usual bar, 1 thin, 2 hidden until the view is scrolling."));
            api.types.push_back(std::move(sv));
        }
        {
            ApiType tb; tb.name = "UITabs"; tb.isComponent = true;
            tb.doc = "A tab bar. Closable tabs fire on_tab_close. Vertical is a style.";
            tb.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UITabs>() : nullptr; };
            auto of = [need](const Value& self, CallContext& c) {
                return need(Resolve(std::get<ObjRef>(self), c.loc)->GetComponent<UITabs>(), "UI Tabs", c);
            };
            tb.methods.push_back(method("SetClosable", {tBool}, {"on"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetClosable(std::get<bool>(a[0])); return Value{}; },
                "Draw an X on each tab. Clicking it removes that tab."));
            tb.methods.push_back(method("CloseTab", {tInt}, {"index"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->CloseTab(I(a[0])); return Value{}; },
                "Remove one tab and fire on_tab_close."));
            ApiMethod n = method("Count", {}, {}, tInt,
                [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{(std::int32_t)of(s, c)->Count()}; },
                "How many tabs are left.");
            n.pure = true; tb.methods.push_back(std::move(n));
            api.types.push_back(std::move(tb));
        }
        {
            ApiType tt; tt.name = "UITooltip"; tt.isComponent = true;
            tt.doc = "A hover tooltip. It already waits and follows the cursor. Rich text and the offset are optional.";
            tt.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UITooltip>() : nullptr; };
            auto of = [need](const Value& self, CallContext& c) {
                return need(Resolve(std::get<ObjRef>(self), c.loc)->GetComponent<UITooltip>(), "UI Tooltip", c);
            };
            tt.methods.push_back(method("SetRichText", {tBool}, {"on"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetRichText(std::get<bool>(a[0])); return Value{}; },
                "Draw <b> and #rrggbb in the tooltip text."));
            tt.methods.push_back(method("SetOffset", {tFloat, tFloat}, {"x", "y"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetOffset(F(a[0]), F(a[1])); return Value{}; },
                "Pixels from the cursor to the box. 14, 14 is the usual spot."));
            tt.methods.push_back(method("SetDelay", {tFloat}, {"seconds"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetDelay(F(a[0])); return Value{}; },
                "How long the pointer must hover before the tooltip shows."));
            api.types.push_back(std::move(tt));
        }
        {
            ApiType inf; inf.name = "UIInputField"; inf.isComponent = true;
            inf.doc = "A text field. Password, a character limit and integer or decimal mode already exist. Multiline is optional.";
            inf.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIInputField>() : nullptr; };
            auto of = [need](const Value& self, CallContext& c) {
                return need(Resolve(std::get<ObjRef>(self), c.loc)->GetComponent<UIInputField>(), "UI Input Field", c);
            };
            inf.methods.push_back(method("SetMultiline", {tBool}, {"on"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetMultiline(std::get<bool>(a[0])); return Value{}; },
                "Enter inserts a newline instead of submitting."));
            inf.methods.push_back(method("SetMaxLength", {tInt}, {"length"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetMaxLength(I(a[0])); return Value{}; },
                "The most characters the field accepts."));
            inf.methods.push_back(method("SetContentType", {tInt}, {"type"}, tVoid,
                [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetContentType(I(a[0])); return Value{}; },
                "0 standard, 1 integer, 2 decimal, 3 password."));
            api.types.push_back(std::move(inf));
        }
    }
    // ---- UIToast / UIModal (grok/ui-toast). Visual-script nodes come from these types. ----
    {
        auto asBool = [](const Value& v) {
            if (auto* b = std::get_if<bool>(&v)) return *b;
            return F(v) != 0.0f;
        };
        ApiType tt; tt.name = "UIToast"; tt.isComponent = true;
        tt.doc = "A stack of toast notifications. The object needs a UI Toast.";
        tt.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIToast>() : nullptr; };
        auto toastOf = [](const Value& self, CallContext& c) -> UIToast* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* t = g->GetComponent<UIToast>();
            if (!t) throw RuntimeError("'" + g->name + "' has no UI Toast (add one in the Inspector)", c.loc);
            return t;
        };
        tt.methods.push_back(method("Push", {tString, tString, tInt}, {"title", "body", "kind"}, tVoid,
            [toastOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                toastOf(s, c)->Push(S(a[0]), S(a[1]), I(a[2]));
                return Value{};
            }, "Show a toast. kind: 0 info, 1 success, 2 warning, 3 error."));
        tt.methods.push_back(method("SetDuration", {tFloat}, {"seconds"}, tVoid,
            [toastOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                toastOf(s, c)->SetDuration(F(a[0]));
                return Value{};
            }, "How long a new toast stays up, in seconds."));
        tt.methods.push_back(method("SetCorner", {tInt}, {"corner"}, tVoid,
            [toastOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                toastOf(s, c)->SetCorner(I(a[0]));
                return Value{};
            }, "Where the stack sits: 0 top-right, 1 top-left, 2 bottom-right, 3 bottom-left, 4 top-center."));
        tt.methods.push_back(method("SetMaxStack", {tInt}, {"count"}, tVoid,
            [toastOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                toastOf(s, c)->SetMaxStack(I(a[0]));
                return Value{};
            }, "How many toasts stay on screen. The oldest one drops off past this."));
        ApiMethod count = method("Count", {}, {}, tInt,
            [toastOf](const Value& s, std::vector<Value>&, CallContext& c) {
                return Value{(std::int32_t)toastOf(s, c)->Count()};
            }, "How many toasts are on screen right now.");
        count.pure = true;
        tt.methods.push_back(std::move(count));
        api.types.push_back(std::move(tt));

        ApiType md; md.name = "UIModal"; md.isComponent = true;
        md.doc = "A modal dialog. Style Card restyles the Card, Title, Message, Confirm and Cancel inside it.";
        md.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIModal>() : nullptr; };
        auto modalOf = [](const Value& self, CallContext& c) -> UIModal* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* m = g->GetComponent<UIModal>();
            if (!m) throw RuntimeError("'" + g->name + "' has no UI Modal (add one in the Inspector)", c.loc);
            return m;
        };
        md.methods.push_back(method("Open", {}, {}, tVoid,
            [modalOf](const Value& s, std::vector<Value>&, CallContext& c) { modalOf(s, c)->Open(); return Value{}; },
            "Show the dialog."));
        md.methods.push_back(method("Close", {}, {}, tVoid,
            [modalOf](const Value& s, std::vector<Value>&, CallContext& c) { modalOf(s, c)->Close(); return Value{}; },
            "Hide the dialog. Fires on_modal_close."));
        md.methods.push_back(method("SetStyleCard", {tBool}, {"on"}, tVoid,
            [modalOf, asBool](const Value& s, std::vector<Value>& a, CallContext& c) {
                modalOf(s, c)->SetStyleCard(asBool(a[0]));
                return Value{};
            }, "Turn the card styling on or off. Off leaves the dialog as it was authored."));
        md.methods.push_back(method("SetCardRadius", {tFloat}, {"radius"}, tVoid,
            [modalOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                modalOf(s, c)->SetCardRadius(F(a[0]));
                return Value{};
            }, "Corner radius of the card named Card, in pixels."));
        md.methods.push_back(method("SetTitle", {tString}, {"text"}, tVoid,
            [modalOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                modalOf(s, c)->SetTitle(S(a[0]));
                return Value{};
            }, "Title text. Empty leaves whatever the Title label already says."));
        md.methods.push_back(method("SetMessage", {tString}, {"text"}, tVoid,
            [modalOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                modalOf(s, c)->SetMessage(S(a[0]));
                return Value{};
            }, "Body text. Empty leaves whatever the Message label already says."));
        md.methods.push_back(method("SetConfirmText", {tString}, {"text"}, tVoid,
            [modalOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                modalOf(s, c)->SetConfirmText(S(a[0]));
                return Value{};
            }, "Confirm button label. Empty leaves the button's own label."));
        md.methods.push_back(method("SetCancelText", {tString}, {"text"}, tVoid,
            [modalOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                modalOf(s, c)->SetCancelText(S(a[0]));
                return Value{};
            }, "Cancel button label. Empty leaves the button's own label."));
        ApiMethod open = method("IsOpen", {}, {}, tBool,
            [modalOf](const Value& s, std::vector<Value>&, CallContext& c) {
                return Value{modalOf(s, c)->IsOpen()};
            }, "True while the dialog is shown.");
        open.pure = true;
        md.methods.push_back(std::move(open));
        api.types.push_back(std::move(md));
    }
    // ---- UITheme (grok/ui-theme). Visual-script nodes come from this type. ----
    {
        ApiType th; th.name = "UITheme"; th.isComponent = true;
        th.doc = "A saved skin for panels, buttons, sliders and the rest of the UI kit. The object needs a UI Theme.";
        th.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UITheme>() : nullptr; };
        auto themeOf = [](const Value& self, CallContext& c) -> UITheme* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* t = g->GetComponent<UITheme>();
            if (!t) throw RuntimeError("'" + g->name + "' has no UI Theme (add one in the Inspector)", c.loc);
            return t;
        };
        th.methods.push_back(method("SetPreset", {tInt}, {"preset"}, tVoid,
            [themeOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                UITheme* t = themeOf(s, c);
                t->ApplyPreset(I(a[0]));
                t->Apply();
                return Value{};
            }, "Switch to a built-in look and bake it: 0 custom, 1 dark, 2 light, 3 sci-fi, 4 fantasy."));
        th.methods.push_back(method("Apply", {}, {}, tVoid,
            [themeOf](const Value& s, std::vector<Value>&, CallContext& c) {
                themeOf(s, c)->Apply();
                return Value{};
            }, "Bake this skin onto this object and its children. Widgets keep their positions."));
        th.methods.push_back(method("ApplyTo", {tString}, {"objectName"}, tVoid,
            [themeOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                UITheme* t = themeOf(s, c);
                GameObject* root = t->gameObject;
                if (t->gameObject && t->gameObject->scene())
                    if (GameObject* named = t->gameObject->scene()->Find(S(a[0]))) root = named;
                t->ApplyTo(root);
                return Value{};
            }, "Bake this skin onto the named object and its children."));
        api.types.push_back(std::move(th));
    }
    // ---- UI widgets (grok/ui-options). Visual-script nodes come from these types.
    // Value changes still raise on_change / OnValueChanged; clicks raise on_click / OnClick.
    {
        ApiType sl; sl.name = "UISlider"; sl.isComponent = true;
        sl.doc = "A draggable value. The object needs a UI Slider. on_change fires when the value moves.";
        sl.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UISlider>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UISlider* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* w = g->GetComponent<UISlider>();
            if (!w) throw RuntimeError("'" + g->name + "' has no UI Slider", c.loc);
            return w;
        };
        sl.methods.push_back(method("SetValue", {tFloat}, {"value"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetValue(F(a[0])); return Value{}; },
            "Set the value (snaps to the step). Fires OnValueChanged when it changes."));
        sl.methods.push_back(method("SetStep", {tFloat}, {"step"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetStep(F(a[0])); return Value{}; },
            "Snap spacing. 0 keeps the slider continuous."));
        sl.methods.push_back(method("SetFormat", {tString}, {"format"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetFormat(S(a[0])); return Value{}; },
            "Value label. \"{v}\" is the number. Empty keeps two decimals."));
        ApiMethod gv = method("GetValue", {}, {}, tFloat,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { return Value{of(s, c)->value}; },
            "The current value.");
        gv.pure = true;
        sl.methods.push_back(std::move(gv));
        api.types.push_back(std::move(sl));
    }
    {
        ApiType pb; pb.name = "UIProgressBar"; pb.isComponent = true;
        pb.doc = "A fill bar (health, loading). The object needs a UI Progress Bar.";
        pb.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIProgressBar>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UIProgressBar* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* w = g->GetComponent<UIProgressBar>();
            if (!w) throw RuntimeError("'" + g->name + "' has no UI Progress Bar", c.loc);
            return w;
        };
        pb.methods.push_back(method("SetValue", {tFloat}, {"value"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetValue(F(a[0])); return Value{}; },
            "Fill amount, 0 to 1. A damage trail eases down when Trail is set."));
        pb.methods.push_back(method("SetSegments", {tInt}, {"count"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetSegments(I(a[0])); return Value{}; },
            "Chunk count. 0 or 1 is one solid bar."));
        pb.methods.push_back(method("SetTrail", {tFloat}, {"seconds"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetTrail(F(a[0])); return Value{}; },
            "Seconds for the damage trail to catch a drop. 0 hides it."));
        pb.methods.push_back(method("SetTextFormat", {tString}, {"format"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) { of(s, c)->SetTextFormat(S(a[0])); return Value{}; },
            "Label on the bar. \"{n}\" is the percent, \"{v}\" is 0..1."));
        api.types.push_back(std::move(pb));
    }
    {
        ApiType bt; bt.name = "UIButton"; bt.isComponent = true;
        bt.doc = "A clickable button. The object needs a UI Button. on_click fires when it is pressed.";
        bt.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<UIButton>() : nullptr; };
        auto of = [](const Value& self, CallContext& c) -> UIButton* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* w = g->GetComponent<UIButton>();
            if (!w) throw RuntimeError("'" + g->name + "' has no UI Button", c.loc);
            return w;
        };
        bt.methods.push_back(method("SetEase", {tInt, tFloat}, {"ease", "seconds"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) {
                of(s, c)->SetEase(I(a[0]), F(a[1])); return Value{};
            }, "Hover/press ease: 0 linear, 1 smooth, 2 ease-out, 3 ease-in, and how many seconds it takes."));
        bt.methods.push_back(method("SetInteractable", {tBool}, {"on"}, tVoid,
            [of](const Value& s, std::vector<Value>& a, CallContext& c) {
                of(s, c)->SetInteractable(std::get<bool>(a[0])); return Value{};
            }, "False greys the button out and ignores clicks."));
        bt.methods.push_back(method("Press", {}, {}, tVoid,
            [of](const Value& s, std::vector<Value>&, CallContext& c) { of(s, c)->Press(); return Value{}; },
            "Click the button. Fires OnClick."));
        api.types.push_back(std::move(bt));
    }
    // ---- Input ----
    {
        ApiType in; in.name = "Input"; in.isStatic = true; in.doc = "Keyboard, mouse and gamepad.";
        auto key = [&](const char* n, bool (*fn)(char), const char* doc) {
            ApiMethod m = method(n, {tString}, {"key"}, tBool,
                [fn](const Value&, std::vector<Value>& a, CallContext& c) { return Value{fn(KeyOf(S(a[0]), c.loc))}; }, doc);
            m.checkLiteral = CheckKeyLiteral;
            in.staticMethods.push_back(m);
        };
        key("GetKey", &Input::GetKey, "True while the key is held.");
        key("GetKeyDown", &Input::GetKeyDown, "True on the frame the key is pressed.");
        key("GetKeyUp", &Input::GetKeyUp, "True on the frame the key is released.");
        ApiMethod axis = method("GetAxis", {tString}, {"axis"}, tFloat,
            [](const Value&, std::vector<Value>& a, CallContext& c) {
                const std::string& n = S(a[0]);
                if (n == "Horizontal") return Value{Axes().x};
                if (n == "Vertical") return Value{Axes().y};
                throw RuntimeError("\"" + n + "\" isn't an axis. Use \"Horizontal\" or \"Vertical\"", c.loc);
            }, "\"Horizontal\" (A/D, left/right, stick) or \"Vertical\" (W/S, up/down, stick): -1 to 1.");
        axis.checkLiteral = CheckAxisLiteral;
        in.staticMethods.push_back(axis);
        auto mouse = [&](const char* n, bool (*fn)(int), const char* doc) {
            in.staticMethods.push_back(method(n, {tInt}, {"button"}, tBool,
                [fn](const Value&, std::vector<Value>& a, CallContext& c) {
                    int b = I(a[0]);
                    if (b < 0 || b > 2) throw RuntimeError("Mouse buttons are 0 (left), 1 (right) and 2 (middle)", c.loc);
                    return Value{fn(b)};
                }, doc));
        };
        mouse("GetMouseButton", &Input::GetMouseButton, "True while the mouse button is held (0 left, 1 right, 2 middle).");
        mouse("GetMouseButtonDown", &Input::GetMouseButtonDown, "True on the frame the mouse button is pressed.");
        mouse("GetMouseButtonUp", &Input::GetMouseButtonUp, "True on the frame the mouse button is released.");
        in.staticProps.push_back(ro("mousePosition", tVec,
            [](const Value&, CallContext&) { Vec2 m = Input::MousePosition(); return Value{Vec3{m.x, m.y, 0.0f}}; },
            "The cursor in screen pixels (x, y; z is 0)."));
        // Game actions (InputActions): what the player pressed, on any device, after
        // their own rebinding. Prefer these over raw keys.
        auto actionOf = [](const std::string& n, CallContext& c) {
            if (!InputActions::Has(n))
                throw RuntimeError("There's no input action called \"" + n + "\" (Move, Look, Jump, Sprint, Crouch, Fire, Aim, Interact, Reload, Inventory, Pause, or one the game defined)", c.loc);
            return n;
        };
        auto button = [&](const char* n, bool (*fn)(const std::string&), const char* doc) {
            in.staticMethods.push_back(method(n, {tString}, {"action"}, tBool,
                [fn, actionOf](const Value&, std::vector<Value>& a, CallContext& c) { return Value{fn(actionOf(S(a[0]), c))}; }, doc));
        };
        button("GetButton", &InputActions::Get, "True while the action is held (\"Jump\", \"Fire\"...), on keyboard, mouse or controller.");
        button("GetButtonDown", &InputActions::Down, "True on the frame the action is pressed.");
        button("GetButtonUp", &InputActions::Up, "True on the frame the action is released.");
        in.staticMethods.push_back(method("GetActionValue", {tString}, {"action"}, tFloat,
            [actionOf](const Value&, std::vector<Value>& a, CallContext& c) { return Value{InputActions::Value(actionOf(S(a[0]), c))}; },
            "How far the action is pressed, 0 to 1 (a trigger can be part way)."));
        in.staticMethods.push_back(method("GetActionVector", {tString}, {"action"}, tVec,
            [actionOf](const Value&, std::vector<Value>& a, CallContext& c) {
                const Vec2 v = InputActions::Vector(actionOf(S(a[0]), c));
                return Value{Vec3{v.x, v.y, 0.0f}};
            }, "A direction action (\"Move\", \"Look\") as x, y in -1..1 (y up; z is 0)."));
        in.staticMethods.push_back(method("Rumble", {tFloat, tFloat, tFloat}, {"low", "high", "seconds"}, tVoid,
            [](const Value&, std::vector<Value>& a, CallContext&) { Input::Rumble(F(a[0]), F(a[1]), F(a[2])); return Value{}; },
            "Shake the controller: low (heavy) and high (light) motor strength 0 to 1, for some seconds."));
        in.staticMethods.push_back(method("RebindAction", {tString, tString}, {"action", "binding"}, tBool,
            [actionOf](const Value&, std::vector<Value>& a, CallContext& c) { return Value{InputActions::Rebind(actionOf(S(a[0]), c), S(a[1]))}; },
            "Bind an action to \"key:e\", \"mouse:right\", \"pad:rt\"... for this player (saved). False if the binding isn't understood."));
        in.staticMethods.push_back(method("DefineAction", {tString, tString, tString}, {"action", "binding", "binding2"}, tVoid,
            [](const Value&, std::vector<Value>& a, CallContext&) {
                std::vector<std::string> b;
                for (int i = 1; i < 3; ++i) if (!S(a[i]).empty()) b.push_back(S(a[i]));
                InputActions::Define(S(a[0]), b, false);
                return Value{};
            }, "Add a game action (or change its defaults), e.g. (\"Dash\", \"key:q\", \"pad:rs\"). Use \"\" for an unused binding."));
        in.staticMethods.push_back(method("DefineDirection", {tString, tString, tString}, {"action", "binding", "binding2"}, tVoid,
            [](const Value&, std::vector<Value>& a, CallContext&) {
                std::vector<std::string> b;
                for (int i = 1; i < 3; ++i) if (!S(a[i]).empty()) b.push_back(S(a[i]));
                InputActions::Define(S(a[0]), b, true);
                return Value{};
            }, "Add a direction action, e.g. (\"Steer\", \"keys:i,k,j,l\", \"stick:right\")."));
        in.staticProps.push_back(ro("gamepadConnected", tBool,
            [](const Value&, CallContext&) { return Value{Input::GamepadConnected()}; }, "True while a controller is plugged in."));
        api.types.push_back(std::move(in));
    }
    // ---- Time ----
    {
        ApiType t; t.name = "Time"; t.isStatic = true; t.doc = "Frame timing.";
        t.staticProps.push_back(ro("deltaTime", tFloat, [](const Value&, CallContext& c) { return Value{c.deltaTime}; },
            "Seconds since the last frame (inside FixedUpdate: the fixed step)."));
        t.staticProps.push_back(ro("fixedDeltaTime", tFloat, [](const Value&, CallContext& c) { return Value{c.fixedDeltaTime}; },
            "The FixedUpdate step in seconds."));
        t.staticProps.push_back(ro("time", tFloat, [](const Value&, CallContext&) { return Value{Time::ElapsedTime()}; },
            "Seconds since the game started."));
        t.staticProps.push_back(ro("frameCount", tInt, [](const Value&, CallContext&) { return Value{(std::int32_t)Time::FrameCount()}; },
            "Frames since the game started."));
        t.staticProps.push_back(prop("timeScale", tFloat,
            [](const Value&, CallContext&) { return Value{Time::TimeScale()}; },
            [](const Value&, const Value& v, CallContext& c) {
                if (F(v) < 0.0f) throw RuntimeError("timeScale can't be negative", c.loc);
                Time::SetTimeScale(F(v));
            }, "How fast time passes: 1 normal, 0.5 slow motion, 0 paused."));
        api.types.push_back(std::move(t));
    }
    // ---- Debug ----
    {
        ApiType d; d.name = "Debug"; d.isStatic = true; d.doc = "Messages to the Console.";
        d.staticMethods.push_back(method("Log", {tAny}, {"message"}, tVoid,
            [](const Value&, std::vector<Value>& a, CallContext&) { Log::Info("[script] ", LogText(a[0])); return Value{}; },
            "Print a message to the Console."));
        d.staticMethods.push_back(method("LogWarning", {tAny}, {"message"}, tVoid,
            [](const Value&, std::vector<Value>& a, CallContext&) { Log::Warning("[script] ", LogText(a[0])); return Value{}; },
            "Print a warning to the Console."));
        d.staticMethods.push_back(method("LogError", {tAny}, {"message"}, tVoid,
            [](const Value&, std::vector<Value>& a, CallContext&) { Log::Error("[script] ", LogText(a[0])); return Value{}; },
            "Print an error to the Console."));
        api.types.push_back(std::move(d));
    }
    // ---- Audio ----
    {
        ApiType au; au.name = "Audio"; au.isStatic = true;
        au.doc = "The mixer: overall volume and each group's (Music, SFX, UI, Voice), as a settings menu sets them.";
        au.staticProps.push_back(prop("masterVolume", tFloat,
            [](const Value&, CallContext&) { return Value{AudioMixer::masterVolume}; },
            [](const Value&, const Value& v, CallContext&) { AudioMixer::masterVolume = Mathf::Clamp(F(v), 0.0f, 1.0f); },
            "Overall volume, 0 to 1."));
        au.staticProps.push_back(prop("muted", tBool,
            [](const Value&, CallContext&) { return Value{AudioMixer::muted}; },
            [](const Value&, const Value& v, CallContext&) { AudioMixer::muted = std::get<bool>(v); },
            "Silence everything."));
        auto group = [](const std::string& name, CallContext& c) {
            AudioGroup g;
            if (!AudioMixer::GroupFromName(name.c_str(), g))
                throw RuntimeError("There's no audio group called \"" + name + "\" (use Music, SFX, UI or Voice)", c.loc);
            return (int)g;
        };
        au.staticMethods.push_back(method("SetGroupVolume", {tString, tFloat}, {"group", "volume"}, tVoid,
            [group](const Value&, std::vector<Value>& a, CallContext& c) {
                AudioMixer::groupVolume[group(S(a[0]), c)] = Mathf::Clamp(F(a[1]), 0.0f, 1.0f); return Value{}; },
            "Set one group's volume, 0 to 1 (\"Music\", \"SFX\", \"UI\" or \"Voice\")."));
        au.staticMethods.push_back(method("GetGroupVolume", {tString}, {"group"}, tFloat,
            [group](const Value&, std::vector<Value>& a, CallContext& c) { return Value{AudioMixer::groupVolume[group(S(a[0]), c)]}; },
            "One group's volume."));
        au.staticMethods.back().pure = true;   // a value node in a visual script
        au.staticMethods.push_back(method("MuteGroup", {tString, tBool}, {"group", "mute"}, tVoid,
            [group](const Value&, std::vector<Value>& a, CallContext& c) {
                AudioMixer::groupMuted[group(S(a[0]), c)] = std::get<bool>(a[1]); return Value{}; },
            "Mute or unmute one group."));
        au.staticMethods.push_back(method("SaveSettings", {}, {}, tVoid,
            [](const Value&, std::vector<Value>&, CallContext&) { AudioMixer::SaveSettings(); return Value{}; },
            "Store the volumes in the player's prefs now (they are also saved when the game closes)."));
        api.types.push_back(std::move(au));
    }
    // ---- SaveGame ----
    {
        ApiType sg; sg.name = "SaveGame"; sg.isStatic = true;
        sg.doc = "Whole-game saves in named slots: every object, the player, inventories, script variables.";
        auto sceneOf = [](CallContext& c) -> Scene& {
            Scene* s = c.self ? c.self->scene() : nullptr;
            if (!s) throw RuntimeError("SaveGame needs a script running in a scene", c.loc);
            return *s;
        };
        sg.staticMethods.push_back(method("Save", {tString, tString}, {"slot", "label"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                std::string err;
                const bool ok = SaveGame::Save(sceneOf(c), S(a[0]), S(a[1]), &err);
                if (!ok) Log::Warning("[script] SaveGame.Save: ", err);
                return Value{ok};
            }, "Save the game to a slot (\"1\", \"quick\"...) with a label for the load menu. False if it couldn't."));
        sg.staticMethods.push_back(method("Load", {tString}, {"slot"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                std::string err;
                const bool ok = SaveGame::Load(sceneOf(c), S(a[0]), &err);
                if (!ok) Log::Warning("[script] SaveGame.Load: ", err);
                return Value{ok};
            }, "Load a slot at the end of this frame. False if there's no save there."));
        sg.staticMethods.push_back(method("Exists", {tString}, {"slot"}, tBool,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{SaveGame::Exists(S(a[0]))}; },
            "True if the slot has a save."));
        sg.staticMethods.back().pure = true;
        sg.staticMethods.push_back(method("Delete", {tString}, {"slot"}, tBool,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{SaveGame::Delete(S(a[0]))}; },
            "Delete the save in a slot."));
        sg.staticMethods.push_back(method("Label", {tString}, {"slot"}, tString,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{SaveGame::Info(S(a[0])).label}; },
            "The label a slot was saved with (\"\" if empty)."));
        sg.staticMethods.back().pure = true;
        sg.staticProps.push_back(ro("playTime", tFloat,
            [](const Value&, CallContext&) { return Value{SaveGame::PlayTime()}; },
            "Seconds played, counting the time in the save that was loaded."));
        api.types.push_back(std::move(sg));
    }
    // ---- Sequence (cutscenes) ----
    {
        ApiType sq; sq.name = "Sequence"; sq.isStatic = true;
        sq.doc = "Cutscenes: play a .okayseq timeline through a Sequence Player in the scene.";
        auto sceneOf = [](CallContext& c) -> Scene* { return c.self ? c.self->scene() : nullptr; };
        sq.staticMethods.push_back(method("Play", {tString}, {"name"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                Scene* s = sceneOf(c);
                return Value{s && SequencePlayer::PlayByName(*s, c.self, S(a[0]))};
            }, "Play the sequence named like this (a Sequence Player's file, or Assets/<name>.okayseq). False if there is none."));
        sq.staticMethods.push_back(method("Stop", {}, {}, tVoid,
            [sceneOf](const Value&, std::vector<Value>&, CallContext& c) { if (Scene* s = sceneOf(c)) SequencePlayer::StopAll(*s); return Value{}; },
            "Stop every playing sequence; the camera goes back to the game."));
        sq.staticProps.push_back(ro("isPlaying", tBool,
            [sceneOf](const Value&, CallContext& c) { Scene* s = sceneOf(c); return Value{s && SequencePlayer::Playing(*s) != nullptr}; },
            "True while a sequence plays."));
        sq.staticProps.push_back(ro("time", tFloat,
            [sceneOf](const Value&, CallContext& c) {
                Scene* s = sceneOf(c);
                SequencePlayer* sp = s ? SequencePlayer::Playing(*s) : nullptr;
                return Value{sp ? sp->Time() : 0.0f};
            }, "Seconds into the playing sequence (0 when none plays)."));
        sq.staticProps.push_back(ro("lastEvent", tString,
            [](const Value&, CallContext&) { return Value{SequencePlayer::LastEvent()}; },
            "The event being delivered: read it in OnSequenceEvent()."));
        api.types.push_back(std::move(sq));
    }
    // ---- PostProcess / Fog / Weather: the scene's look (docs/rendering.md) ----
    {
        auto look = [](CallContext& c) -> Scene::RenderSettings& {
            Scene* s = c.self ? c.self->scene() : nullptr;
            if (!s) throw RuntimeError("This needs a script running in a scene", c.loc);
            return s->renderSettings;
        };
        ApiType pp; pp.name = "PostProcess"; pp.isStatic = true;
        pp.doc = "The scene's post-processing: exposure, tonemap, bloom, colour grading, vignette, FXAA, SSAO and depth of field (saved with the scene; the Lighting panel edits the same values).";
        pp.staticMethods.push_back(method("Set", {tString, tFloat}, {"name", "value"}, tBool,
            [look](const Value&, std::vector<Value>& a, CallContext& c) {
                const bool ok = PostSet(look(c), S(a[0]), F(a[1]));
                if (!ok) Log::Warning("[script] PostProcess.Set: unknown setting \"", S(a[0]), "\"");
                return Value{ok};
            }, "Set one value by name: \"exposure\", \"tonemap\" (0 none, 1 ACES, 2 filmic), \"bloom\" (0/1), \"bloom_threshold\", "
               "\"bloom_intensity\", \"bloom_radius\", \"grading\", \"temperature\", \"tint\", \"saturation\", \"contrast\", "
               "\"vignette\", \"vignette_smoothness\", \"fxaa\", \"ssao\", \"ssao_radius\", \"ssao_intensity\", \"dof\", "
               "\"dof_focus\", \"dof_range\". False for a name it doesn't know."));
        pp.staticProps.push_back(prop("exposure", tFloat,
            [look](const Value&, CallContext& c) { return Value{look(c).post.exposure}; },
            [look](const Value&, const Value& v, CallContext& c) { PostSet(look(c), "exposure", F(v)); },
            "Scene brightness multiplier (1 = as lit)."));
        api.types.push_back(std::move(pp));

        ApiType fg; fg.name = "Fog"; fg.isStatic = true;
        fg.doc = "The scene's fog: linear (start / end), exponential or height fog, in the colour you set or the sky's.";
        fg.staticMethods.push_back(method("Set", {tBool, tFloat, tFloat}, {"enabled", "start", "end"}, tVoid,
            [look](const Value&, std::vector<Value>& a, CallContext& c) {
                auto& rs = look(c); rs.fog = std::get<bool>(a[0]); rs.fogStart = F(a[1]); rs.fogEnd = F(a[2]); return Value{}; },
            "Turn the fog on or off, fading from start to end (world units from the camera) in Linear mode."));
        fg.staticMethods.push_back(method("SetColor", {tFloat, tFloat, tFloat}, {"r", "g", "b"}, tVoid,
            [look](const Value&, std::vector<Value>& a, CallContext& c) {
                auto& rs = look(c); rs.fogColor = {Mathf::Clamp01(F(a[0])), Mathf::Clamp01(F(a[1])), Mathf::Clamp01(F(a[2])), 1.0f};
                rs.fogMatchSky = false; return Value{}; },
            "The fog colour (0 to 1 each). Stops matching the sky's horizon."));
        fg.staticMethods.push_back(method("SetMode", {tString, tFloat}, {"mode", "density"}, tVoid,
            [look](const Value&, std::vector<Value>& a, CallContext& c) {
                auto& rs = look(c); rs.fogMode = FogModeFromName(S(a[0])); rs.fogDensity = Mathf::Clamp(F(a[1]), 0.0f, 10.0f); return Value{}; },
            "\"linear\", \"exponential\" or \"height\", with the density the last two thicken by per unit of distance."));
        fg.staticProps.push_back(prop("enabled", tBool,
            [look](const Value&, CallContext& c) { return Value{look(c).fog}; },
            [look](const Value&, const Value& v, CallContext& c) { look(c).fog = std::get<bool>(v); },
            "Whether the fog is on."));
        api.types.push_back(std::move(fg));

        ApiType we; we.name = "Weather"; we.isStatic = true;
        we.doc = "Rain, snow and storms falling around the camera, and the wind particles and water follow.";
        we.staticMethods.push_back(method("Set", {tString, tFloat}, {"preset", "intensity"}, tVoid,
            [look](const Value&, std::vector<Value>& a, CallContext& c) {
                auto& w = look(c).weather; w.preset = WeatherPresetFromName(S(a[0])); w.intensity = Mathf::Clamp01(F(a[1])); return Value{}; },
            "\"none\", \"rain\", \"snow\" or \"storm\" at an intensity from 0 to 1; it cross-fades over the scene's weather fade time."));
        we.staticMethods.push_back(method("SetWind", {tFloat, tFloat, tFloat}, {"x", "y", "z"}, tVoid,
            [look](const Value&, std::vector<Value>& a, CallContext& c) { look(c).weather.wind = {F(a[0]), F(a[1]), F(a[2])}; return Value{}; },
            "The wind (world units per second) that particles drift with and water waves follow."));
        we.staticProps.push_back(ro("preset", tString,
            [look](const Value&, CallContext& c) { return Value{std::string(WeatherPresetName(look(c).weather.preset))}; },
            "The current preset's name."));
        api.types.push_back(std::move(we));
    }
    // ---- NavMeshAgent ----
    {
        ApiType na; na.name = "NavMeshAgent"; na.isComponent = true;
        na.doc = "Walks the object along the scene's navigation mesh (the grid when none is baked). The object needs a Nav Mesh Agent.";
        na.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<NavMeshAgent>() : nullptr; };
        auto agentOf = [](const Value& self, CallContext& c) -> NavMeshAgent* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* a = g->GetComponent<NavMeshAgent>();
            if (!a) throw RuntimeError("'" + g->name + "' has no Nav Mesh Agent (add one in the Inspector)", c.loc);
            return a;
        };
        na.methods.push_back(method("SetDestination", {tVec}, {"point"}, tVoid,
            [agentOf](const Value& s, std::vector<Value>& a, CallContext& c) { agentOf(s, c)->SetDestination(V(a[0])); return Value{}; },
            "Plan a path to a world point and start walking there."));
        na.methods.push_back(method("Stop", {}, {}, tVoid,
            [agentOf](const Value& s, std::vector<Value>&, CallContext& c) { agentOf(s, c)->Stop(); return Value{}; },
            "Stop where it is (Resume continues to the same point)."));
        na.methods.push_back(method("Resume", {}, {}, tVoid,
            [agentOf](const Value& s, std::vector<Value>&, CallContext& c) { agentOf(s, c)->Resume(); return Value{}; },
            "Carry on to the destination after Stop."));
        ApiMethod hasPath = method("HasPath", {}, {}, tBool,
            [agentOf](const Value& s, std::vector<Value>&, CallContext& c) { return Value{agentOf(s, c)->HasPath()}; },
            "True while it is walking to a destination.");
        hasPath.pure = true; na.methods.push_back(std::move(hasPath));
        ApiMethod remaining = method("RemainingDistance", {}, {}, tFloat,
            [agentOf](const Value& s, std::vector<Value>&, CallContext& c) { return Value{agentOf(s, c)->RemainingDistance()}; },
            "Path length left to the destination (0 when there is none).");
        remaining.pure = true; na.methods.push_back(std::move(remaining));
        ApiMethod arrived = method("Arrived", {}, {}, tBool,
            [agentOf](const Value& s, std::vector<Value>&, CallContext& c) { return Value{agentOf(s, c)->Arrived()}; },
            "True once the last destination was reached (until the next SetDestination).");
        arrived.pure = true; na.methods.push_back(std::move(arrived));
        na.props.push_back(prop("speed", tFloat,
            [agentOf](const Value& s, CallContext& c) { return Value{agentOf(s, c)->speed}; },
            [agentOf](const Value& s, const Value& v, CallContext& c) { agentOf(s, c)->speed = std::max(0.0f, F(v)); },
            "Top speed in units per second."));
        na.props.push_back(prop("stoppingDistance", tFloat,
            [agentOf](const Value& s, CallContext& c) { return Value{agentOf(s, c)->stoppingDistance}; },
            [agentOf](const Value& s, const Value& v, CallContext& c) { agentOf(s, c)->stoppingDistance = std::max(0.0f, F(v)); },
            "How close to the destination counts as there."));
        na.props.push_back(ro("isStopped", tBool,
            [agentOf](const Value& s, CallContext& c) { return Value{agentOf(s, c)->IsStopped()}; },
            "True after Stop, until Resume or a new destination."));
        na.props.push_back(ro("velocity", tVec,
            [agentOf](const Value& s, CallContext& c) { return Value{agentOf(s, c)->Velocity()}; },
            "How it is moving right now, in units per second."));
        na.props.push_back(ro("destination", tVec,
            [agentOf](const Value& s, CallContext& c) { return Value{agentOf(s, c)->Destination()}; },
            "The point it was last sent to."));
        api.types.push_back(std::move(na));
    }
    // ---- Dialogue / Story / Quest ----
    {
        auto sceneOf = [](CallContext& c) -> Scene* { return c.self ? c.self->scene() : nullptr; };
        ApiType dg; dg.name = "Dialogue"; dg.isStatic = true;
        dg.doc = "Conversations: start an object's Dialogue Runner (by the object's or the graph's name) and ask whether one runs.";
        dg.staticMethods.push_back(method("Start", {tString}, {"name"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                DialogueRunner* r = nullptr;
                if (S(a[0]).empty() && c.self) r = c.self->GetComponent<DialogueRunner>();
                if (!r) r = DialogueRunner::FindByName(sceneOf(c), S(a[0]));
                if (!r) { Log::Warning("[script] Dialogue.Start: no Dialogue Runner named ", S(a[0])); return Value{false}; }
                return Value{r->StartDialogue()};
            }, "Start the conversation on the object (or graph) with this name; \"\" means this object's runner. False when there is none."));
        dg.staticMethods.push_back(method("Stop", {}, {}, tVoid,
            [sceneOf](const Value&, std::vector<Value>&, CallContext& c) { if (DialogueRunner* r = DialogueRunner::ActiveIn(sceneOf(c))) r->Stop(); return Value{}; },
            "End the running conversation."));
        dg.staticMethods.push_back(method("IsActive", {}, {}, tBool,
            [sceneOf](const Value&, std::vector<Value>&, CallContext& c) { return Value{DialogueRunner::ActiveIn(sceneOf(c)) != nullptr}; },
            "True while a conversation runs."));
        dg.staticMethods.back().pure = true;
        api.types.push_back(std::move(dg));

        ApiType st; st.name = "Story"; st.isStatic = true;
        st.doc = "The story's global variables and flags (the scene's Story State; saved with the game).";
        st.staticMethods.push_back(method("Set", {tString, tFloat}, {"name", "value"}, tVoid,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                if (StoryState* s = StoryState::Get(sceneOf(c), true)) s->Set(S(a[0]), vs::VsValue(F(a[1])));
                return Value{};
            }, "Set a number variable."));
        st.staticMethods.push_back(method("SetText", {tString, tString}, {"name", "value"}, tVoid,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                if (StoryState* s = StoryState::Get(sceneOf(c), true)) s->Set(S(a[0]), vs::VsValue(S(a[1])));
                return Value{};
            }, "Set a text variable."));
        st.staticMethods.push_back(method("Get", {tString}, {"name"}, tFloat,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                StoryState* s = StoryState::Get(sceneOf(c), false);
                return Value{s ? s->Get(S(a[0])).AsFloat() : 0.0f};
            }, "A variable as a number (0 when unset)."));
        st.staticMethods.back().pure = true;
        st.staticMethods.push_back(method("GetText", {tString}, {"name"}, tString,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                StoryState* s = StoryState::Get(sceneOf(c), false);
                return Value{s ? s->Get(S(a[0])).AsString() : std::string()};
            }, "A variable as text (\"\" when unset)."));
        st.staticMethods.back().pure = true;
        st.staticMethods.push_back(method("Has", {tString}, {"name"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                StoryState* s = StoryState::Get(sceneOf(c), false);
                return Value{s && s->Has(S(a[0]))};
            }, "True once the variable was set."));
        st.staticMethods.back().pure = true;
        st.staticMethods.push_back(method("SetFlag", {tString, tBool}, {"name", "on"}, tVoid,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                if (StoryState* s = StoryState::Get(sceneOf(c), true)) s->SetFlag(S(a[0]), std::get<bool>(a[1]));
                return Value{};
            }, "Set or clear a flag."));
        st.staticMethods.push_back(method("HasFlag", {tString}, {"name"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                StoryState* s = StoryState::Get(sceneOf(c), false);
                return Value{s && s->HasFlag(S(a[0]))};
            }, "True while the flag is set."));
        st.staticMethods.back().pure = true;
        api.types.push_back(std::move(st));

        ApiType q; q.name = "Quest"; q.isStatic = true;
        q.doc = "Quests in the scene's Quest Log: start, complete objectives, read states.";
        q.staticMethods.push_back(method("Start", {tString}, {"id"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                QuestLog* l = QuestLog::Get(sceneOf(c), true);
                return Value{l && l->StartQuest(S(a[0]))};
            }, "Start a quest. False when it is unknown, already active or its prerequisites aren't done."));
        q.staticMethods.push_back(method("Complete", {tString}, {"id"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                QuestLog* l = QuestLog::Get(sceneOf(c), false);
                return Value{l && l->CompleteQuest(S(a[0]))};
            }, "Complete an active quest outright (its rewards are granted)."));
        q.staticMethods.push_back(method("CompleteObjective", {tString, tInt}, {"id", "index"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                QuestLog* l = QuestLog::Get(sceneOf(c), true);
                return Value{l && l->CompleteObjective(S(a[0]), I(a[1]))};
            }, "Mark one objective (0-based) of an active quest done."));
        q.staticMethods.push_back(method("AddProgress", {tString, tInt, tInt}, {"id", "index", "amount"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                QuestLog* l = QuestLog::Get(sceneOf(c), true);
                return Value{l && l->AddProgress(S(a[0]), I(a[1]), I(a[2]))};
            }, "Count toward a collect / kill style objective."));
        q.staticMethods.push_back(method("Fail", {tString}, {"id"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                QuestLog* l = QuestLog::Get(sceneOf(c), false);
                return Value{l && l->FailQuest(S(a[0]))};
            }, "Fail an active quest."));
        q.staticMethods.push_back(method("State", {tString}, {"id"}, tString,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                QuestLog* l = QuestLog::Get(sceneOf(c), false);
                return Value{std::string(l ? l->StatusName(S(a[0])) : "inactive")};
            }, "\"inactive\", \"active\", \"completed\" or \"failed\"."));
        q.staticMethods.back().pure = true;
        q.staticMethods.push_back(method("IsActive", {tString}, {"id"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                QuestLog* l = QuestLog::Get(sceneOf(c), false);
                return Value{l && l->IsActive(S(a[0]))};
            }, "True while the quest is active."));
        q.staticMethods.back().pure = true;
        api.types.push_back(std::move(q));
    }
    // ---- BehaviorTree ----
    {
        ApiType bt; bt.name = "BehaviorTree"; bt.isStatic = true;
        bt.doc = "This object's Behavior Tree Runner: its blackboard, and starting or stopping its tree.";
        auto runnerOf = [](CallContext& c) -> BehaviorTreeRunner* {
            auto* r = c.self ? c.self->GetComponent<BehaviorTreeRunner>() : nullptr;
            if (!r) throw RuntimeError("'" + (c.self ? c.self->name : std::string("?")) + "' has no Behavior Tree Runner (add one in the Inspector)", c.loc);
            return r;
        };
        auto toBt = [](const Value& v) -> BtValue {
            if (auto* b = std::get_if<bool>(&v)) return BtValue::Bool(*b);
            if (auto* i = std::get_if<std::int32_t>(&v)) return BtValue::Number((float)*i);
            if (auto* f = std::get_if<float>(&v)) return BtValue::Number(*f);
            if (auto* s = std::get_if<std::string>(&v)) return BtValue::String(*s);
            if (auto* x = std::get_if<Vec3>(&v)) return BtValue::Vector(*x);
            if (auto* o = std::get_if<ObjRef>(&v)) {
                GameObject* g = o->go ? GameObject::FromInstanceId(o->go) : nullptr;
                return BtValue::Object(g ? g->name : std::string(), o->go);
            }
            return BtValue::String(ValueToString(v));
        };
        auto fromBt = [goIdx](const BtValue& v, CallContext& c) -> Value {
            switch (v.type) {
                case BtValue::Type::Number: return Value{v.number};
                case BtValue::Type::Bool:   return Value{v.boolean};
                case BtValue::Type::String: return Value{v.text};
                case BtValue::Type::Vector: return Value{v.vector};
                case BtValue::Type::Object: return MakeRef(v.AsObject(c.self ? c.self->scene() : nullptr), goIdx);
            }
            return Value{};
        };
        bt.staticMethods.push_back(method("Set", {tString, tAny}, {"key", "value"}, tVoid,
            [runnerOf, toBt](const Value&, std::vector<Value>& a, CallContext& c) { runnerOf(c)->blackboard.Set(S(a[0]), toBt(a[1])); return Value{}; },
            "Write a blackboard value: a number, bool, string, Vector3 or GameObject."));
        bt.staticMethods.push_back(method("Get", {tString}, {"key"}, tAny,
            [runnerOf, fromBt](const Value&, std::vector<Value>& a, CallContext& c) {
                const BtValue* v = runnerOf(c)->blackboard.Find(S(a[0]));
                return v ? fromBt(*v, c) : Value{};
            }, "Read a blackboard value as whatever it is (use GetFloat / GetBool / GetString / GetVector / GetObject for a typed one)."));
        bt.staticMethods.back().pure = true;
        bt.staticMethods.push_back(method("GetFloat", {tString}, {"key"}, tFloat,
            [runnerOf](const Value&, std::vector<Value>& a, CallContext& c) { return Value{runnerOf(c)->blackboard.GetNumber(S(a[0]))}; },
            "A blackboard number (0 when the key is missing)."));
        bt.staticMethods.back().pure = true;
        bt.staticMethods.push_back(method("GetBool", {tString}, {"key"}, tBool,
            [runnerOf](const Value&, std::vector<Value>& a, CallContext& c) { return Value{runnerOf(c)->blackboard.GetBool(S(a[0]))}; },
            "A blackboard bool (false when the key is missing)."));
        bt.staticMethods.back().pure = true;
        bt.staticMethods.push_back(method("GetString", {tString}, {"key"}, tString,
            [runnerOf](const Value&, std::vector<Value>& a, CallContext& c) { return Value{runnerOf(c)->blackboard.GetString(S(a[0]))}; },
            "A blackboard string (\"\" when the key is missing)."));
        bt.staticMethods.back().pure = true;
        bt.staticMethods.push_back(method("GetVector", {tString}, {"key"}, tVec,
            [runnerOf](const Value&, std::vector<Value>& a, CallContext& c) { return Value{runnerOf(c)->blackboard.GetVector(S(a[0]))}; },
            "A blackboard vector, or an object key's position."));
        bt.staticMethods.back().pure = true;
        bt.staticMethods.push_back(method("GetObject", {tString}, {"key"}, tGO,
            [runnerOf, goIdx](const Value&, std::vector<Value>& a, CallContext& c) {
                return MakeRef(runnerOf(c)->blackboard.GetGameObject(S(a[0]), c.self ? c.self->scene() : nullptr), goIdx);
            }, "The object a blackboard key refers to, or null."));
        bt.staticMethods.back().pure = true;
        bt.staticMethods.push_back(method("Run", {tString}, {"tree"}, tBool,
            [runnerOf](const Value&, std::vector<Value>& a, CallContext& c) {
                BehaviorTreeRunner* r = runnerOf(c);
                if (!S(a[0]).empty()) {
                    std::string err;
                    if (!r->LoadTree(S(a[0]), &err)) { Log::Warning("[script] BehaviorTree.Run: ", err); return Value{false}; }
                }
                r->Run();
                return Value{r->IsRunning()};
            }, "Load a .okaybt file and start it (\"\" starts the current tree over). False when the file can't be read."));
        bt.staticMethods.push_back(method("Stop", {}, {}, tVoid,
            [runnerOf](const Value&, std::vector<Value>&, CallContext& c) { runnerOf(c)->Stop(); return Value{}; },
            "Stop the tree; running tasks are aborted and the NPC stops."));
        bt.staticProps.push_back(ro("isRunning", tBool,
            [runnerOf](const Value&, CallContext& c) { return Value{runnerOf(c)->IsRunning()}; },
            "True while the tree is being ticked."));
        api.types.push_back(std::move(bt));
    }
    // ---- Mathf ----
    {
        ApiType m; m.name = "Mathf"; m.isStatic = true; m.doc = "Maths helpers.";
        m.staticProps.push_back(ro("PI", tFloat, [](const Value&, CallContext&) { return Value{Mathf::PI}; }, "3.14159..."));
        auto f1 = [&](const char* n, float (*fn)(float), const char* doc) {
            m.staticMethods.push_back(method(n, {tFloat}, {"value"}, tFloat,
                [fn](const Value&, std::vector<Value>& a, CallContext&) { return Value{fn(F(a[0]))}; }, doc));
        };
        f1("Abs", [](float x) { return std::fabs(x); }, "The value without its sign.");
        f1("Sqrt", [](float x) { return std::sqrt(std::max(0.0f, x)); }, "Square root.");
        f1("Sin", [](float x) { return std::sin(x); }, "Sine (radians).");
        f1("Cos", [](float x) { return std::cos(x); }, "Cosine (radians).");
        f1("Tan", [](float x) { return std::tan(x); }, "Tangent (radians).");
        f1("Floor", [](float x) { return std::floor(x); }, "Round down.");
        f1("Ceil", [](float x) { return std::ceil(x); }, "Round up.");
        f1("Round", [](float x) { return std::round(x); }, "Round to the nearest whole number.");
        f1("Sign", [](float x) { return x < 0.0f ? -1.0f : 1.0f; }, "-1 for negative values, else 1.");
        f1("Clamp01", [](float x) { return Mathf::Clamp(x, 0.0f, 1.0f); }, "Keep between 0 and 1.");
        auto f2 = [&](const char* n, float (*fn)(float, float), const char* a, const char* b, const char* doc) {
            m.staticMethods.push_back(method(n, {tFloat, tFloat}, {a, b}, tFloat,
                [fn](const Value&, std::vector<Value>& x, CallContext&) { return Value{fn(F(x[0]), F(x[1]))}; }, doc));
        };
        f2("Min", [](float a, float b) { return std::min(a, b); }, "a", "b", "The smaller value.");
        f2("Max", [](float a, float b) { return std::max(a, b); }, "a", "b", "The larger value.");
        f2("Pow", [](float a, float b) { return std::pow(a, b); }, "value", "power", "value to the power.");
        f2("Atan2", [](float y, float x) { return std::atan2(y, x); }, "y", "x", "Angle of (x, y) in radians.");
        auto f3 = [&](const char* n, float (*fn)(float, float, float), std::vector<std::string> names, const char* doc) {
            m.staticMethods.push_back(method(n, {tFloat, tFloat, tFloat}, std::move(names), tFloat,
                [fn](const Value&, std::vector<Value>& x, CallContext&) { return Value{fn(F(x[0]), F(x[1]), F(x[2]))}; }, doc));
        };
        f3("Clamp", [](float v, float lo, float hi) { return Mathf::Clamp(v, lo, hi); }, {"value", "min", "max"}, "Keep between min and max.");
        f3("Lerp", [](float a, float b, float t) { return a + (b - a) * Mathf::Clamp(t, 0.0f, 1.0f); }, {"a", "b", "t"}, "Blend from a (t = 0) to b (t = 1).");
        f3("MoveTowards", [](float c, float t, float d) { return std::fabs(t - c) <= d ? t : c + (t > c ? d : -d); },
           {"current", "target", "maxDelta"}, "Step from current toward target by at most maxDelta.");
        api.types.push_back(std::move(m));
    }
    // Queries and maths have no side effects: in a visual script they're value
    // nodes, not steps in the flow.
    for (ApiType& t : api.types)
        if (t.name == "GameObject" || t.name == "Vector3" || t.name == "Input" || t.name == "Mathf")
            for (ApiMethod& m : t.staticMethods) m.pure = m.name != "Rumble" && m.name != "RebindAction" &&
                                                      m.name != "DefineAction" && m.name != "DefineDirection";
    return api;
}

} // namespace

const EngineApi& EngineApi::Get() {
    static const EngineApi api = Build();
    return api;
}

} // namespace okay::script
