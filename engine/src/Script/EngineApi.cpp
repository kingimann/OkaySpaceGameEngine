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
