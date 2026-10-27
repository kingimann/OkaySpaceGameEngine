// The engine binding registry: every type, property and method a script can use.
// Each one calls real engine code; there are no placeholders. The checker
// resolves names against this table and the editor's autocomplete lists it.
#include "ScriptInternal.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Components/PlayerView.hpp"
#include "okay/Components/Crosshair.hpp"          // grok/ui-hud
#include "okay/Components/UIRadialProgress.hpp"   // grok/ui-hud
#include "okay/Components/UISlotGrid.hpp"         // grok/ui-hud
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
            for (ApiMethod& m : t.staticMethods) m.pure = true;
    return api;
}

} // namespace

const EngineApi& EngineApi::Get() {
    static const EngineApi api = Build();
    return api;
}

} // namespace okay::script
