// The engine binding registry: every type, property and method a script can use.
// Each one calls real engine code; there are no placeholders. The checker
// resolves names against this table and the editor's autocomplete lists it.
#include "EngineApiInternal.hpp"

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

using namespace engine_api;

namespace {


// ---- The shared table (okay/Scripting/ScriptApiRegistry.hpp) ----
// A shared function is written once, for every script language; here it becomes a member of its type.
Type TypeOf(ScriptApiType t) {
    switch (t) {
        case ScriptApiType::Bool:    return Type::Of(TK::Bool);
        case ScriptApiType::Int:     return Type::Of(TK::Int);
        case ScriptApiType::Float:   return Type::Of(TK::Float);
        case ScriptApiType::String:  return Type::Of(TK::String);
        case ScriptApiType::Vector3: return Type::Of(TK::Vector3);
        default:                     return Type::Of(TK::Void);
    }
}
vs::VsValue ToShared(ScriptApiType t, const Value& v) {
    switch (t) {
        case ScriptApiType::Bool:    { auto* b = std::get_if<bool>(&v); return vs::VsValue(b ? *b : F(v) != 0.0f); }
        case ScriptApiType::Int:     return vs::VsValue((float)I(v));
        case ScriptApiType::String:  return vs::VsValue(S(v));
        case ScriptApiType::Vector3: return vs::VsValue(V(v));
        default:                     return vs::VsValue(F(v));
    }
}
Value FromShared(ScriptApiType t, const vs::VsValue& v) {
    switch (t) {
        case ScriptApiType::Void:    return Value{};
        case ScriptApiType::Bool:    return Value{v.AsBool()};
        case ScriptApiType::Int: {
            const float f = v.AsFloat();
            return Value{(std::int32_t)(std::isfinite(f) ? std::max(-2147483648.0f, std::min(2147483520.0f, f)) : 0.0f)};
        }
        case ScriptApiType::String:  return Value{v.AsString()};
        case ScriptApiType::Vector3: return Value{v.AsVec3()};
        default:                     return Value{v.AsFloat()};
    }
}
// Run a shared function from a typed call: `self` is the receiver for a Method, else the script's object.
Value CallShared(const ScriptApiEntry& e, const Value& self, std::vector<Value>& a, CallContext& c) {
    GameObject* g = e.kind == ScriptApiKind::Method ? Resolve(std::get<ObjRef>(self), c.loc) : c.self;
    std::vector<vs::VsValue> args;
    args.reserve(e.params.size());
    for (std::size_t i = 0; i < e.params.size(); ++i)
        args.push_back(i < a.size() ? ToShared(e.params[i].type, a[i]) : e.params[i].def);
    try {
        return FromShared(e.ret, e.fn(g, args));
    } catch (const ScriptApiError& ex) {
        throw RuntimeError(ex.what(), c.loc);
    }
}
void AddSharedBindings(EngineApi& api) {
    for (const ScriptApiEntry& src : ScriptApis()) {
        if (src.typedType.empty()) continue;   // OkayScript only
        auto e = std::make_shared<const ScriptApiEntry>(src);
        int ti = api.Find(e->typedType);
        if (ti < 0) {
            ApiType t; t.name = e->typedType; t.isStatic = true;
            api.types.push_back(std::move(t));
            ti = (int)api.types.size() - 1;
        }
        ApiType& t = api.types[(std::size_t)ti];
        if (e->kind == ScriptApiKind::StaticProperty) {
            for (const ApiProperty& p : t.staticProps)
                if (p.name == e->typedName) NoteScriptApiDuplicate("the typed language", t.name + "." + p.name);
            ApiProperty p; p.name = e->typedName; p.type = TypeOf(e->ret); p.doc = e->doc;
            p.get = [e](const Value& self, CallContext& c) { std::vector<Value> none; return CallShared(*e, self, none, c); };
            t.staticProps.push_back(std::move(p));
            continue;
        }
        std::vector<ApiMethod>& list = e->kind == ScriptApiKind::Method ? t.methods : t.staticMethods;
        const std::size_t all = e->params.size();
        const std::size_t fewest = e->typedMinParams < 0 ? all : std::min(all, (std::size_t)e->typedMinParams);
        for (std::size_t n = fewest; n <= all; ++n) {   // one overload per argument count
            for (const ApiMethod& m : list)
                if (m.name == e->typedName && m.params.size() == n)
                    NoteScriptApiDuplicate("the typed language", t.name + "." + m.name);
            ApiMethod m; m.name = e->typedName; m.ret = TypeOf(e->ret); m.doc = e->doc; m.pure = e->pure;
            for (std::size_t i = 0; i < n; ++i) { m.params.push_back(TypeOf(e->params[i].type)); m.paramNames.push_back(e->params[i].name); }
            m.checkLiteral = e->checkLiteral;
            m.fn = [e](const Value& self, std::vector<Value>& a, CallContext& c) { return CallShared(*e, self, a, c); };
            list.push_back(std::move(m));
        }
    }
}

} // namespace

namespace engine_api {

void Ctx::Init() {
    // ---- GameObject ----
    api.gameObject = (int)api.types.size();
    api.types.push_back({});
    // ---- Transform ----
    api.transform = (int)api.types.size();
    api.types.push_back({});
    // ---- Vector3 ----
    api.vector3 = (int)api.types.size();
    api.types.push_back({});
    tGO = Type::Obj(api.gameObject); tTr = Type::Obj(api.transform);
    goSlot = api.gameObject; trSlot = api.transform;
}

void Ctx::BindAnimation() {
    // ---- Animator (graph on this object or its model) ----
    {
        ApiType an; an.name = "Animator"; an.isComponent = true;
        an.doc = "The animation graph. On a player it is often on the imported model, and these calls find it.";
        auto graphOf = [](GameObject* go) -> AnimStateMachine* {
            if (!go || !go->scene()) return nullptr;
            if (auto* sm = go->GetComponent<AnimStateMachine>()) if (sm->layer == 0) return sm;
            for (const auto& up : go->scene()->Objects())
                if (up && up->IsSelfOrDescendantOf(go))
                    if (auto* sm = up->GetComponent<AnimStateMachine>())
                        if (sm->layer == 0) return sm;
            return nullptr;
        };
        auto modelOf = [](GameObject* go) -> ModelAnimator* {
            if (!go || !go->scene()) return nullptr;
            if (auto* m = go->GetComponent<ModelAnimator>()) return m;
            for (const auto& up : go->scene()->Objects())
                if (up && up->IsSelfOrDescendantOf(go))
                    if (auto* m = up->GetComponent<ModelAnimator>()) return m;
            return nullptr;
        };
        an.find = [](GameObject* go) -> Component* { return FindAnimatorComponent(go); };
        an.methods.push_back(method("SetTrigger", {tString}, {"name"}, tVoid,
            [graphOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                AnimStateMachine* sm = graphOf(Resolve(std::get<ObjRef>(s), c.loc));
                if (!sm) throw RuntimeError("This object has no Animator graph", c.loc);
                sm->SetTrigger(S(a[0])); return Value{}; },
            "Fire a trigger parameter (jump, for example)."));
        an.methods.push_back(method("SetBool", {tString, tBool}, {"name", "on"}, tVoid,
            [graphOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                AnimStateMachine* sm = graphOf(Resolve(std::get<ObjRef>(s), c.loc));
                if (!sm) throw RuntimeError("This object has no Animator graph", c.loc);
                bool on = false;
                if (auto* b = std::get_if<bool>(&a[1])) on = *b; else on = F(a[1]) != 0.0f;
                sm->SetBool(S(a[0]), on); return Value{}; },
            "Set a bool parameter (crouch, prone, grounded)."));
        an.methods.push_back(method("SetFloat", {tString, tFloat}, {"name", "value"}, tVoid,
            [graphOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                AnimStateMachine* sm = graphOf(Resolve(std::get<ObjRef>(s), c.loc));
                if (!sm) throw RuntimeError("This object has no Animator graph", c.loc);
                sm->SetFloat(S(a[0]), F(a[1])); return Value{}; },
            "Set a float parameter (speed)."));
        an.methods.push_back(method("Play", {tString}, {"clip"}, tVoid,
            [modelOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                ModelAnimator* m = modelOf(Resolve(std::get<ObjRef>(s), c.loc));
                if (!m || !m->Play(S(a[0]))) throw RuntimeError("This object has no clip by that name", c.loc);
                return Value{}; },
            "Play one of the model's clips by name."));
        {
            ApiMethod m = method("GetFloat", {tString}, {"name"}, tFloat,
                [graphOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                    AnimStateMachine* sm = graphOf(Resolve(std::get<ObjRef>(s), c.loc));
                    return Value{sm ? sm->GetFloat(S(a[0])) : 0.0f}; },
                "Read a float parameter (speed). 0 if the graph has no such name.");
            m.pure = true; an.methods.push_back(std::move(m));
        }
        {
            ApiMethod m = method("GetBool", {tString}, {"name"}, tBool,
                [graphOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                    AnimStateMachine* sm = graphOf(Resolve(std::get<ObjRef>(s), c.loc));
                    return Value{sm && sm->GetBool(S(a[0]))}; },
                "Read a bool parameter (crouch, grounded).");
            m.pure = true; an.methods.push_back(std::move(m));
        }
        {
            ApiMethod m = method("GetInt", {tString}, {"name"}, tInt,
                [graphOf](const Value& s, std::vector<Value>& a, CallContext& c) {
                    AnimStateMachine* sm = graphOf(Resolve(std::get<ObjRef>(s), c.loc));
                    return Value{(std::int32_t)(sm ? sm->GetInt(S(a[0])) : 0)}; },
                "Read an int parameter.");
            m.pure = true; an.methods.push_back(std::move(m));
        }
        an.methods.push_back(method("SetLayerWeight", {tString, tFloat}, {"layer", "weight"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                GameObject* g = Resolve(std::get<ObjRef>(s), c.loc);
                AnimStateMachine* sm = FindAnimLayer(g, true, S(a[0]), 0);
                if (!sm) return Value{false};
                sm->layerWeight = F(a[1]);
                return Value{true}; },
            "Set the mix weight of the Animator layer with this name (on this object or its model)."));
        an.methods.push_back(method("SetLayerWeight", {tInt, tFloat}, {"layer", "weight"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                GameObject* g = Resolve(std::get<ObjRef>(s), c.loc);
                AnimStateMachine* sm = FindAnimLayer(g, false, {}, I(a[0]));
                if (!sm) return Value{false};
                sm->layerWeight = F(a[1]);
                return Value{true}; },
            "Set the mix weight of the Animator layer with this index (0 is the base)."));
        {
            ApiMethod m = method("GetLayerWeight", {tString}, {"layer"}, tFloat,
                [](const Value& s, std::vector<Value>& a, CallContext& c) {
                    AnimStateMachine* sm = FindAnimLayer(Resolve(std::get<ObjRef>(s), c.loc), true, S(a[0]), 0);
                    return Value{sm ? sm->layerWeight : 0.0f}; },
                "Mix weight of the Animator layer with this name. 0 if there is no such layer.");
            m.pure = true; an.methods.push_back(std::move(m));
        }
        {
            ApiMethod m = method("GetLayerWeight", {tInt}, {"layer"}, tFloat,
                [](const Value& s, std::vector<Value>& a, CallContext& c) {
                    AnimStateMachine* sm = FindAnimLayer(Resolve(std::get<ObjRef>(s), c.loc), false, {}, I(a[0]));
                    return Value{sm ? sm->layerWeight : 0.0f}; },
                "Mix weight of the Animator layer with this index. 0 if there is no such layer.");
            m.pure = true; an.methods.push_back(std::move(m));
        }
        an.methods.push_back(method("SetRootMotion", {tBool}, {"on"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                bool on = false;
                if (auto* b = std::get_if<bool>(&a[0])) on = *b; else on = F(a[0]) != 0.0f;
                return Value{SetRootMotionOn(Resolve(std::get<ObjRef>(s), c.loc), on)}; },
            "Turn root motion on or off for the model on this object (or its child)."));
        an.methods.push_back(method("SetCharacterModel", {tString}, {"path"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                return Value{SetCharacterModelPath(Resolve(std::get<ObjRef>(s), c.loc), S(a[0]))}; },
            "Swap the character model for the file at this path. False if the file does not import."));
        an.props.push_back(ro("state", tString,
            [graphOf](const Value& s, CallContext& c) {
                AnimStateMachine* sm = graphOf(Resolve(std::get<ObjRef>(s), c.loc));
                return Value{sm ? sm->Current() : std::string{}}; },
            "Name of the state the graph is in."));
        api.types.push_back(std::move(an));
    }
    // ---- IK ----
    {
        ApiType ik; ik.name = "IK"; ik.isComponent = true;
        ik.doc = "Look-At, feet and full-body IK on this object.";
        ik.find = [](GameObject* go) -> Component* { return FindIkComponent(go); };
        ik.methods.push_back(method("LookAt", {tString}, {"objectName"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                GameObject* g = Resolve(std::get<ObjRef>(s), c.loc);
                if (!g->scene()) throw RuntimeError("This object is not in a scene", c.loc);
                GameObject* tgt = g->scene()->Find(S(a[0]));
                if (!tgt) throw RuntimeError("No object named '" + S(a[0]) + "'", c.loc);
                LookAtIK* look = g->GetComponent<LookAtIK>();
                if (!look) look = g->AddComponent<LookAtIK>();
                look->targetObject = tgt->transform;
                look->targetName = tgt->name;
                look->ikActive = true;
                look->weight = look->weight > 0.0f ? look->weight : 1.0f;
                return Value{}; },
            "Aim Look-At IK at the object with this name."));
        ik.methods.push_back(method("SetWeight", {tFloat}, {"weight"}, tVoid,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                GameObject* g = Resolve(std::get<ObjRef>(s), c.loc);
                const float w = F(a[0]);
                if (auto* l = g->GetComponent<LookAtIK>()) l->weight = w;
                if (auto* f = g->GetComponent<FootIK>()) f->weight = w;
                if (auto* b = g->GetComponent<FullBodyIK>()) b->weight = w;
                return Value{}; },
            "Set the weight (0..1) of Look-At, Foot and Full Body IK on this object."));
        ik.methods.push_back(method("SetKindWeight", {tString, tFloat}, {"kind", "weight"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                return Value{SetIkKindWeight(Resolve(std::get<ObjRef>(s), c.loc), S(a[0]), F(a[1]))}; },
            "Set one IK weight: foot, look, aim, hand_l, hand_r or full. Also looks on children."));
        ik.methods.push_back(method("SetLimbTarget", {tString, tString}, {"limb", "objectName"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                GameObject* g = Resolve(std::get<ObjRef>(s), c.loc);
                if (!g->scene()) return Value{false};
                GameObject* named = g->scene()->Find(S(a[1]));
                if (!named) return Value{false};
                return Value{SetIkLimbTarget(g, S(a[0]), named, nullptr)}; },
            "Reach or look at the object with this name. Limb is hand_l, hand_r, foot_l, foot_r, look, aim or head."));
        ik.methods.push_back(method("SetLimbTarget", {tString, tFloat, tFloat, tFloat}, {"limb", "x", "y", "z"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                Vec3 p{F(a[1]), F(a[2]), F(a[3])};
                return Value{SetIkLimbTarget(Resolve(std::get<ObjRef>(s), c.loc), S(a[0]), nullptr, &p)}; },
            "Reach or look at a world point. Limb is hand_l, hand_r, foot_l, foot_r, look, aim or head."));
        ik.methods.push_back(method("ClearLimbTarget", {tString}, {"limb"}, tBool,
            [](const Value& s, std::vector<Value>& a, CallContext& c) {
                return Value{ClearIkLimbTarget(Resolve(std::get<ObjRef>(s), c.loc), S(a[0]))}; },
            "Drop the goal for that limb so the animation owns it again."));
        api.types.push_back(std::move(ik));
    }
}

void Ctx::BindMathf() {
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
}

} // namespace engine_api

namespace {

EngineApi Build() {
    Ctx ctx;
    EngineApi& api = ctx.api;
    ctx.Init();
    ctx.BindCore();
    ctx.BindGameplay();
    ctx.BindUiControls();
    ctx.BindUiApp();
    ctx.BindUiLibrary();
    ctx.BindInput();
    ctx.BindRuntime();
    ctx.BindRendering();
    ctx.BindNavigation();
    ctx.BindStory();
    ctx.BindSplines();
    ctx.BindAnimation();
    ctx.BindTrail();
    ctx.BindMathf();
    AddSharedBindings(api);
    // Queries and maths have no side effects: in a visual script they're value
    // nodes, not steps in the flow.
    for (ApiType& t : api.types)
        if (t.name == "GameObject" || t.name == "Vector3" || t.name == "Input" || t.name == "Mathf")
            for (ApiMethod& m : t.staticMethods) m.pure = m.name != "Rumble" && m.name != "RebindAction" &&
                                                      m.name != "DefineAction" && m.name != "DefineDirection";
    return std::move(api);
}

} // namespace


const EngineApi& EngineApi::Get() {
    static const EngineApi api = Build();
    return api;
}

} // namespace okay::script
