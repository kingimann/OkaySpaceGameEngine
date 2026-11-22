// Lua scripts on the engine: IScriptVM for language "lua" on okay::lua.
//
// The engine API comes from the typed-language binding table (Script/EngineApi,
// feature 14.7): every type, property and method there is reachable from Lua,
// so Lua, the C#-style language and the visual-script Engine API nodes share
// one set of bindings. Engine objects are userdata holding an object id (and
// which component); a destroyed object reads as an error, never as freed
// memory. Vector3 is a userdata with x, y, z and the usual operators.
#include "okay/Lua/LuaScriptVM.hpp"
#include "okay/Lua/Lua.hpp"
#include "../Script/ScriptInternal.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace okay {

using LV = lua::Value;
using lua::State;
namespace sc = script;

namespace {
enum UdTag { kObjTag = 1, kVecTag = 2 };

const char* const kLuaKeywords[] = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function",
                                    "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return", "then",
                                    "true", "until", "while"};
// names the engine sets on a script that are not the script's own settings
const char* const kEngineSet[] = {"other", "contact_point", "contact_normal", "contact_x", "contact_y", "contact_z",
                                  "contact_nx", "contact_ny", "contact_nz", "impact_speed", "other_tag",
                                  "gameObject", "transform"};
bool IsEngineSet(const std::string& n) {
    for (const char* e : kEngineSet) if (n == e) return true;
    return false;
}
} // namespace

struct LuaScriptVM::Impl {
    State L;
    sc::CallContext ctx;
    bool loaded = false;
    bool tableStyle = false;
    int scriptRef = 0;            ///< the script table (table style)
    int running = 0;              ///< nested calls into this VM (the budget is set by the outermost one)
    lua::Table* objMeta = nullptr;
    lua::Table* vecMeta = nullptr;
    lua::Table* methodCache = nullptr;   ///< "Type.name" -> bound method closure
    std::set<std::string> builtinGlobals;
    LuaScriptVM* owner = nullptr;
    Impl() { L.userData = this; }
};

namespace {

LuaScriptVM::Impl& Ctx(State& L) { return *static_cast<LuaScriptVM::Impl*>(L.userData); }
const sc::EngineApi& Api() { return sc::EngineApi::Get(); }
LV Str(State& L, const std::string& s) { return LV::Obj(L.NewString(s)); }

int CallerLine(State& L) {
    std::string w = L.Where(1);   // "chunk:line: "
    std::size_t c = w.rfind(':', w.size() >= 2 ? w.size() - 3 : 0);
    if (c == std::string::npos) return 0;
    return std::atoi(w.c_str() + c + 1);
}

LV MakeVec(State& L, const Vec3& v) {
    lua::UData* u = L.NewUData();
    u->tag = kVecTag;
    u->meta = Ctx(L).vecMeta;
    u->v[0] = v.x;
    u->v[1] = v.y;
    u->v[2] = v.z;
    return LV::Obj(u);
}

LV MakeObj(State& L, std::uint64_t id, int type) {
    if (id == 0) return LV();
    lua::UData* u = L.NewUData();
    u->tag = kObjTag;
    u->meta = Ctx(L).objMeta;
    u->id = id;
    u->i32 = type;
    return LV::Obj(u);
}

bool IsVec(const LV& v) { return v.t == lua::Type::Userdata && v.UD()->tag == kVecTag; }
bool IsObj(const LV& v) { return v.t == lua::Type::Userdata && v.UD()->tag == kObjTag; }
Vec3 VecOf(const LV& v) { return Vec3{(float)v.UD()->v[0], (float)v.UD()->v[1], (float)v.UD()->v[2]}; }

const char* LuaTypeWord(State& L, const LV& v) {
    if (IsVec(v)) return "Vector3";
    if (IsObj(v)) {
        int t = v.UD()->i32;
        return t >= 0 && t < (int)Api().types.size() ? Api().types[(std::size_t)t].name.c_str() : "object";
    }
    return L.TypeName(v);
}

std::string TypeWord(const sc::Type& t) {
    switch (t.kind) {
        case sc::TK::Float: return "number";
        case sc::TK::Int: case sc::TK::Enum: return "whole number";
        case sc::TK::Bool: return "boolean";
        case sc::TK::String: return "string";
        case sc::TK::Vector3: return "Vector3";
        case sc::TK::Any: return "any value";
        case sc::TK::Object: return sc::TypeName(t);
        case sc::TK::Array: return "table of strings";
        default: return "value";
    }
}

// A Lua value as an engine value of type t, or false (with why).
bool FromLua(State& L, const LV& v, const sc::Type& t, sc::Value& out, std::string& why) {
    auto wrong = [&]() { why = TypeWord(t) + " expected, got " + LuaTypeWord(L, v); return false; };
    switch (t.kind) {
        case sc::TK::Float:
            if (!v.IsNumber()) return wrong();
            out = sc::Value{(float)v.AsNum()};
            return true;
        case sc::TK::Int: case sc::TK::Enum: {
            if (!v.IsNumber()) return wrong();
            double d = std::trunc(v.AsNum());
            if (!(d >= -2147483648.0 && d <= 2147483647.0)) { why = "number out of range for a whole number"; return false; }
            out = sc::Value{(std::int32_t)d};
            return true;
        }
        case sc::TK::Bool:
            if (v.t != lua::Type::Bool) return wrong();
            out = sc::Value{v.b};
            return true;
        case sc::TK::String:
            if (v.IsString()) { out = sc::Value{v.Str()->s}; return true; }
            if (v.IsNumber()) { out = sc::Value{State::NumberToString(v)}; return true; }
            return wrong();
        case sc::TK::Vector3:
            if (!IsVec(v)) return wrong();
            out = sc::Value{VecOf(v)};
            return true;
        case sc::TK::Object:
            if (v.IsNil()) { out = sc::Value{}; return true; }
            if (!IsObj(v)) return wrong();
            out = sc::Value{sc::ObjRef{v.UD()->id, t.id}};
            return true;
        case sc::TK::Any:
            switch (v.t) {
                case lua::Type::Nil: out = sc::Value{}; return true;
                case lua::Type::Bool: out = sc::Value{v.b}; return true;
                case lua::Type::Int:
                    if (v.i >= INT32_MIN && v.i <= INT32_MAX) out = sc::Value{(std::int32_t)v.i};
                    else out = sc::Value{(float)v.i};
                    return true;
                case lua::Type::Float: out = sc::Value{(float)v.n}; return true;
                case lua::Type::String: out = sc::Value{v.Str()->s}; return true;
                default:
                    if (IsVec(v)) { out = sc::Value{VecOf(v)}; return true; }
                    if (IsObj(v)) { out = sc::Value{sc::ObjRef{v.UD()->id, v.UD()->i32}}; return true; }
                    out = sc::Value{L.ToStringMeta(v)};
                    return true;
            }
        case sc::TK::Array: {
            if (!v.IsTable()) return wrong();
            auto arr = std::make_shared<sc::ArrayObj>();
            arr->typeName = "string[]";
            std::int64_t n = v.Tab()->Length();
            for (std::int64_t i = 1; i <= n && i <= 100000; ++i) {
                LV e = v.Tab()->GetInt(i);
                arr->items.push_back(sc::Value{e.IsString() ? e.Str()->s : L.ToStringBasic(e)});
            }
            out = sc::Value{arr};
            return true;
        }
        default:
            why = "this kind of value can't be passed from Lua";
            return false;
    }
}

LV ToLua(State& L, const sc::Value& v, int depth = 0) {
    if (std::holds_alternative<std::monostate>(v)) return LV();
    if (auto* b = std::get_if<bool>(&v)) return LV::Bool(*b);
    if (auto* i = std::get_if<std::int32_t>(&v)) return LV::Int(*i);
    if (auto* f = std::get_if<float>(&v)) return LV::Num((double)*f);
    if (auto* s = std::get_if<std::string>(&v)) return Str(L, *s);
    if (auto* p = std::get_if<Vec3>(&v)) return MakeVec(L, *p);
    if (auto* o = std::get_if<sc::ObjRef>(&v)) return MakeObj(L, o->go, o->type);
    if (auto* a = std::get_if<sc::ArrayRef>(&v)) {
        if (!*a || depth > 4) return LV();
        lua::Table* t = L.NewTable((*a)->items.size(), 0);
        L.Push(LV::Obj(t));   // reachable while it fills
        std::int64_t k = 1;
        for (const sc::Value& x : (*a)->items) t->SetInt(L, k++, ToLua(L, x, depth + 1));
        L.Pop();
        return LV::Obj(t);
    }
    return LV();
}

sc::Value SelfValue(const LV& v) {
    if (IsVec(v)) return sc::Value{VecOf(v)};
    if (IsObj(v)) return sc::Value{sc::ObjRef{v.UD()->id, v.UD()->i32}};
    return sc::Value{};
}

void Prepare(State& L) {
    LuaScriptVM::Impl& I = Ctx(L);
    I.ctx.loc.line = CallerLine(L);
    I.ctx.loc.col = 0;
}

[[noreturn]] void EngineError(State& L, const std::exception& e) {
    L.Error(e.what());
}

// ---- calling a bound method: up = {type index, static?, name}
int ApiCall(State& L, int n) {
    lua::NClosure* self = L.Current()->frames.back().ncl;
    int typeIdx = (int)self->up[0].i;
    bool isStatic = self->up[1].b;
    const std::string name = self->up[2].Str()->s;
    const sc::ApiType& t = Api().types[(std::size_t)typeIdx];
    int first = 0;
    sc::Value selfV;
    if (!isStatic) {
        LV a0 = L.Arg(0);
        bool ok = (t.isValueType ? IsVec(a0) : IsObj(a0));
        if (!ok)
            L.Error("call " + name + " with ':' on " + t.name + " (obj:" + name + "(...)), got " + LuaTypeWord(L, a0) +
                    " as the object");
        selfV = SelfValue(a0);
        if (IsObj(a0)) std::get<sc::ObjRef>(selfV).type = typeIdx;
        first = 1;
    }
    const auto& list = isStatic ? t.staticMethods : t.methods;
    int nargs = n - first;
    std::string why, expected;
    for (const sc::ApiMethod& m : list) {
        if (m.name != name) continue;
        std::string sig = name + "(";
        for (std::size_t i = 0; i < m.params.size(); ++i)
            sig += (i ? ", " : "") + (i < m.paramNames.size() ? m.paramNames[i] + ": " : std::string()) + TypeWord(m.params[i]);
        sig += ")";
        expected += (expected.empty() ? "" : " or ") + sig;
        if ((int)m.params.size() != nargs) continue;
        std::vector<sc::Value> args(m.params.size());
        bool ok = true;
        for (std::size_t i = 0; i < m.params.size() && ok; ++i) {
            std::string w;
            if (!FromLua(L, L.Arg(first + (int)i), m.params[i], args[i], w)) {
                why = "argument #" + std::to_string(i + 1) + ": " + w;
                ok = false;
            } else if (m.checkLiteral) {
                if (auto* s = std::get_if<std::string>(&args[i])) {
                    std::string msg = m.checkLiteral((int)i, *s);
                    if (!msg.empty()) L.Error(t.name + "." + name + ": " + msg);
                }
            }
        }
        if (!ok) continue;
        Prepare(L);
        sc::Value r;
        try {
            r = m.fn(selfV, args, Ctx(L).ctx);
        } catch (const sc::RuntimeError& e) {
            EngineError(L, e);
        } catch (const std::exception& e) {
            EngineError(L, e);
        }
        if (m.ret.kind == sc::TK::Void) return 0;
        L.Push(ToLua(L, r));
        return 1;
    }
    std::string msg = "bad arguments to " + t.name + "." + name + " (expected " + expected + ")";
    if (!why.empty()) msg += ": " + why;
    L.Error(msg);
}

LV BoundMethod(State& L, int typeIdx, bool isStatic, const std::string& name) {
    LuaScriptVM::Impl& I = Ctx(L);
    std::string key = std::to_string(typeIdx) + (isStatic ? ":" : ".") + name;
    LV k = Str(L, key);
    L.Push(k);
    LV f = I.methodCache->Get(k);
    if (f.IsNil()) {
        f = LV::Obj(L.NewNative(ApiCall, "engine method", {LV::Int(typeIdx), LV::Bool(isStatic), Str(L, name)}));
        I.methodCache->Set(L, k, f);
    }
    L.Pop();
    return f;
}

bool HasMethod(const std::vector<sc::ApiMethod>& list, const std::string& name) {
    for (const auto& m : list) if (m.name == name) return true;
    return false;
}

GameObject* ResolveObj(State& L, const LV& v) {
    if (!IsObj(v)) L.Error(std::string("object expected, got ") + LuaTypeWord(L, v));
    GameObject* g = GameObject::FromInstanceId(v.UD()->id);
    if (!g) L.Error("This object was destroyed, so it can't be used any more");
    return g;
}

int GetComponentFn(State& L, int n) {
    GameObject* g = nullptr;
    int nameArg = 0;
    if (n >= 1 && IsObj(L.Arg(0))) { g = ResolveObj(L, L.Arg(0)); nameArg = 1; }
    else g = Ctx(L).ctx.self;
    if (!g) L.Error("This script isn't attached to an object");
    LV nm = L.Arg(nameArg);
    if (!nm.IsString()) L.Error("GetComponent needs a component name, e.g. GetComponent(\"Rigidbody\")");
    int idx = Api().Find(nm.Str()->s);
    if (idx < 0 || !Api().types[(std::size_t)idx].isComponent)
        L.Error("'" + nm.Str()->s + "' isn't a component type scripts can use");
    const sc::ApiType& t = Api().types[(std::size_t)idx];
    if (t.find && t.find(g)) L.Push(MakeObj(L, g->InstanceId(), idx));
    else L.PushNil();
    return 1;
}

int DestroyFn(State& L, int n) {
    (void)n;
    GameObject* g = ResolveObj(L, L.Arg(0));
    if (Scene* s = g->scene()) s->Destroy(g);
    return 0;
}

// ---- engine objects
int ObjIndex(State& L, int n) {
    (void)n;
    LV o = L.Arg(0), key = L.Arg(1);
    int typeIdx = o.UD()->i32;
    const auto& api = Api();
    if (typeIdx < 0 || typeIdx >= (int)api.types.size()) L.Error("bad engine object");
    const sc::ApiType& t = api.types[(std::size_t)typeIdx];
    if (!key.IsString()) L.Error(t.name + " fields are named with strings");
    const std::string& k = key.Str()->s;
    for (const sc::ApiProperty& p : t.props) {
        if (p.name != k) continue;
        Prepare(L);
        sc::Value r;
        try {
            r = p.get(SelfValue(o), Ctx(L).ctx);
        } catch (const std::exception& e) {
            EngineError(L, e);
        }
        L.Push(ToLua(L, r));
        return 1;
    }
    if (HasMethod(t.methods, k)) { L.Push(BoundMethod(L, typeIdx, false, k)); return 1; }
    if (k == "GetComponent") { L.Push(LV::Obj(L.NewNative(GetComponentFn, "GetComponent"))); return 1; }
    if (k == "gameObject" || k == "transform") {
        GameObject* g = ResolveObj(L, o);
        L.Push(MakeObj(L, g->InstanceId(), k == "gameObject" ? api.gameObject : api.transform));
        return 1;
    }
    if (k == "name" && typeIdx != api.gameObject) {
        L.Push(Str(L, ResolveObj(L, o)->name));
        return 1;
    }
    L.Error(t.name + " has no member '" + k + "'");
}

int ObjNewIndex(State& L, int n) {
    (void)n;
    LV o = L.Arg(0), key = L.Arg(1), val = L.Arg(2);
    const sc::ApiType& t = Api().types[(std::size_t)o.UD()->i32];
    if (!key.IsString()) L.Error(t.name + " fields are named with strings");
    const std::string& k = key.Str()->s;
    for (const sc::ApiProperty& p : t.props) {
        if (p.name != k) continue;
        if (!p.set) L.Error(t.name + "." + k + " is read-only");
        sc::Value v;
        std::string why;
        if (!FromLua(L, val, p.type, v, why)) L.Error("can't set " + t.name + "." + k + ": " + why);
        Prepare(L);
        try {
            p.set(SelfValue(o), v, Ctx(L).ctx);
        } catch (const std::exception& e) {
            EngineError(L, e);
        }
        return 0;
    }
    L.Error(t.name + " has no settable property '" + k + "'");
}

int ObjEq(State& L, int n) {
    (void)n;
    LV a = L.Arg(0), b = L.Arg(1);
    L.Push(LV::Bool(IsObj(a) && IsObj(b) && a.UD()->id == b.UD()->id && a.UD()->i32 == b.UD()->i32));
    return 1;
}

int ObjToString(State& L, int n) {
    (void)n;
    LV o = L.Arg(0);
    GameObject* g = GameObject::FromInstanceId(o.UD()->id);
    std::string tn = LuaTypeWord(L, o);
    L.Push(Str(L, tn + "(" + (g ? g->name : std::string("destroyed")) + ")"));
    return 1;
}

// ---- Vector3
Vec3 CheckVec(State& L, int arg, const char* what) {
    LV v = L.Arg(arg);
    if (!IsVec(v)) L.Error(std::string("Vector3 expected for ") + what + ", got " + LuaTypeWord(L, v));
    return VecOf(v);
}

int VecIndex(State& L, int n) {
    (void)n;
    LV o = L.Arg(0), key = L.Arg(1);
    if (!key.IsString()) L.Error("Vector3 fields are x, y and z");
    const std::string& k = key.Str()->s;
    if (k.size() == 1 && (k[0] == 'x' || k[0] == 'y' || k[0] == 'z')) { L.Push(LV::Num(o.UD()->v[k[0] - 'x'])); return 1; }
    const sc::ApiType& t = Api().types[(std::size_t)Api().vector3];
    for (const sc::ApiProperty& p : t.props) {
        if (p.name != k) continue;
        sc::Value r;
        try { r = p.get(SelfValue(o), Ctx(L).ctx); } catch (const std::exception& e) { EngineError(L, e); }
        L.Push(ToLua(L, r));
        return 1;
    }
    if (k == "sqrMagnitude") { Vec3 v = VecOf(o); L.Push(LV::Num((double)Vec3::Dot(v, v))); return 1; }
    if (HasMethod(t.methods, k)) { L.Push(BoundMethod(L, Api().vector3, false, k)); return 1; }
    L.Error("Vector3 has no member '" + k + "'");
}

int VecNewIndex(State& L, int n) {
    (void)n;
    LV o = L.Arg(0), key = L.Arg(1), val = L.Arg(2);
    if (key.IsString() && key.Str()->s.size() == 1 && key.Str()->s[0] >= 'x' && key.Str()->s[0] <= 'z') {
        if (!val.IsNumber()) L.Error("Vector3." + key.Str()->s + " must be a number");
        o.UD()->v[key.Str()->s[0] - 'x'] = val.AsNum();
        return 0;
    }
    L.Error("Vector3 has only x, y and z to set");
}

int VecAdd(State& L, int n) { (void)n; L.Push(MakeVec(L, CheckVec(L, 0, "+") + CheckVec(L, 1, "+"))); return 1; }
int VecSub(State& L, int n) { (void)n; L.Push(MakeVec(L, CheckVec(L, 0, "-") - CheckVec(L, 1, "-"))); return 1; }
int VecUnm(State& L, int n) { (void)n; L.Push(MakeVec(L, CheckVec(L, 0, "-") * -1.0f)); return 1; }
int VecMul(State& L, int n) {
    (void)n;
    LV a = L.Arg(0), b = L.Arg(1);
    if (IsVec(a) && b.IsNumber()) { L.Push(MakeVec(L, VecOf(a) * (float)b.AsNum())); return 1; }
    if (a.IsNumber() && IsVec(b)) { L.Push(MakeVec(L, VecOf(b) * (float)a.AsNum())); return 1; }
    if (IsVec(a) && IsVec(b)) { Vec3 x = VecOf(a), y = VecOf(b); L.Push(MakeVec(L, Vec3{x.x * y.x, x.y * y.y, x.z * y.z})); return 1; }
    L.Error("Vector3 can be multiplied by a number or another Vector3");
}
int VecDiv(State& L, int n) {
    (void)n;
    LV a = L.Arg(0), b = L.Arg(1);
    if (IsVec(a) && b.IsNumber()) { float d = (float)b.AsNum(); Vec3 v = VecOf(a); L.Push(MakeVec(L, Vec3{v.x / d, v.y / d, v.z / d})); return 1; }
    L.Error("Vector3 can be divided by a number");
}
int VecEq(State& L, int n) {
    (void)n;
    LV a = L.Arg(0), b = L.Arg(1);
    bool eq = IsVec(a) && IsVec(b) && a.UD()->v[0] == b.UD()->v[0] && a.UD()->v[1] == b.UD()->v[1] && a.UD()->v[2] == b.UD()->v[2];
    L.Push(LV::Bool(eq));
    return 1;
}
std::string VecText(const LV& v) {
    auto f = [](double d) { return State::NumberToString(LV::Num(d)); };
    return "(" + f(v.UD()->v[0]) + ", " + f(v.UD()->v[1]) + ", " + f(v.UD()->v[2]) + ")";
}
int VecToString(State& L, int n) { (void)n; L.Push(Str(L, VecText(L.Arg(0)))); return 1; }
int AnyConcat(State& L, int n) {
    (void)n;
    LV a = L.Arg(0), b = L.Arg(1);
    std::string s = (a.IsString() || a.IsNumber()) ? L.ToStringBasic(a) : L.ToStringMeta(a);
    s += (b.IsString() || b.IsNumber()) ? L.ToStringBasic(b) : L.ToStringMeta(b);
    L.Push(Str(L, s));
    return 1;
}
int VecNew(State& L, int n) {
    // Vector3.new(x, y, z) and Vector3(x, y, z) (the table itself is argument 0 there)
    int first = (n >= 1 && L.Arg(0).IsTable()) ? 1 : 0;
    double c[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        LV v = L.Arg(first + i);
        if (v.IsNil()) continue;
        if (!v.IsNumber()) L.Error("Vector3.new takes numbers (x, y, z)");
        c[i] = v.AsNum();
    }
    L.Push(MakeVec(L, Vec3{(float)c[0], (float)c[1], (float)c[2]}));
    return 1;
}

// ---- static types (Input, Time, Debug, ...): the global table's __index; up = {type index}
int StaticIndex(State& L, int n) {
    (void)n;
    int typeIdx = (int)L.Current()->frames.back().ncl->up[0].i;
    const sc::ApiType& t = Api().types[(std::size_t)typeIdx];
    LV key = L.Arg(1);
    if (!key.IsString()) { L.PushNil(); return 1; }
    const std::string& k = key.Str()->s;
    for (const sc::ApiProperty& p : t.staticProps) {
        if (p.name != k) continue;
        Prepare(L);
        sc::Value r;
        try { r = p.get(sc::Value{}, Ctx(L).ctx); } catch (const std::exception& e) { EngineError(L, e); }
        L.Push(ToLua(L, r));
        return 1;
    }
    if (HasMethod(t.staticMethods, k)) { L.Push(BoundMethod(L, typeIdx, true, k)); return 1; }
    L.Error(t.name + " has no member '" + k + "'");
}

int StaticNewIndex(State& L, int n) {
    (void)n;
    int typeIdx = (int)L.Current()->frames.back().ncl->up[0].i;
    const sc::ApiType& t = Api().types[(std::size_t)typeIdx];
    LV key = L.Arg(1), val = L.Arg(2);
    if (key.IsString())
        for (const sc::ApiProperty& p : t.staticProps) {
            if (p.name != key.Str()->s) continue;
            if (!p.set) L.Error(t.name + "." + p.name + " is read-only");
            sc::Value v;
            std::string why;
            if (!FromLua(L, val, p.type, v, why)) L.Error("can't set " + t.name + "." + p.name + ": " + why);
            Prepare(L);
            try { p.set(sc::Value{}, v, Ctx(L).ctx); } catch (const std::exception& e) { EngineError(L, e); }
            return 0;
        }
    L.Error(t.name + " has no settable property '" + (key.IsString() ? key.Str()->s : std::string("?")) + "'");
}

void SetField(State& L, lua::Table* t, const char* k, const LV& v) { t->Set(L, Str(L, k), v); }

void InstallBindings(LuaScriptVM::Impl& I) {
    State& L = I.L;
    const auto& api = Api();
    L.printSink = [](const std::string& s) { Log::Info(s); };
    I.objMeta = L.NewTable();
    L.Ref(LV::Obj(I.objMeta));
    SetField(L, I.objMeta, "__index", LV::Obj(L.NewNative(ObjIndex, "__index")));
    SetField(L, I.objMeta, "__newindex", LV::Obj(L.NewNative(ObjNewIndex, "__newindex")));
    SetField(L, I.objMeta, "__eq", LV::Obj(L.NewNative(ObjEq, "__eq")));
    SetField(L, I.objMeta, "__tostring", LV::Obj(L.NewNative(ObjToString, "__tostring")));
    SetField(L, I.objMeta, "__concat", LV::Obj(L.NewNative(AnyConcat, "__concat")));
    SetField(L, I.objMeta, "__metatable", Str(L, "engine object"));
    I.vecMeta = L.NewTable();
    L.Ref(LV::Obj(I.vecMeta));
    SetField(L, I.vecMeta, "__index", LV::Obj(L.NewNative(VecIndex, "__index")));
    SetField(L, I.vecMeta, "__newindex", LV::Obj(L.NewNative(VecNewIndex, "__newindex")));
    SetField(L, I.vecMeta, "__add", LV::Obj(L.NewNative(VecAdd, "__add")));
    SetField(L, I.vecMeta, "__sub", LV::Obj(L.NewNative(VecSub, "__sub")));
    SetField(L, I.vecMeta, "__mul", LV::Obj(L.NewNative(VecMul, "__mul")));
    SetField(L, I.vecMeta, "__div", LV::Obj(L.NewNative(VecDiv, "__div")));
    SetField(L, I.vecMeta, "__unm", LV::Obj(L.NewNative(VecUnm, "__unm")));
    SetField(L, I.vecMeta, "__eq", LV::Obj(L.NewNative(VecEq, "__eq")));
    SetField(L, I.vecMeta, "__tostring", LV::Obj(L.NewNative(VecToString, "__tostring")));
    SetField(L, I.vecMeta, "__concat", LV::Obj(L.NewNative(AnyConcat, "__concat")));
    SetField(L, I.vecMeta, "__metatable", Str(L, "Vector3"));
    I.methodCache = L.NewTable();
    L.Ref(LV::Obj(I.methodCache));
    // one global table per engine type with static members (Input, Time, Debug, Mathf, GameObject, Vector3, ...)
    for (std::size_t i = 0; i < api.types.size(); ++i) {
        const sc::ApiType& t = api.types[i];
        if (t.name.empty()) continue;
        if (t.staticProps.empty() && t.staticMethods.empty() && (int)i != api.vector3) continue;
        lua::Table* g = L.NewTable();
        L.SetGlobal(t.name.c_str(), LV::Obj(g));
        lua::Table* mt = L.NewTable();
        g->meta = mt;
        SetField(L, mt, "__index", LV::Obj(L.NewNative(StaticIndex, "__index", {LV::Int((std::int64_t)i)})));
        SetField(L, mt, "__newindex", LV::Obj(L.NewNative(StaticNewIndex, "__newindex", {LV::Int((std::int64_t)i)})));
        if ((int)i == api.vector3) {
            SetField(L, g, "new", LV::Obj(L.NewNative(VecNew, "new")));
            SetField(L, mt, "__call", LV::Obj(L.NewNative(VecNew, "Vector3")));
        }
    }
    L.SetGlobal("GetComponent", LV::Obj(L.NewNative(GetComponentFn, "GetComponent")));
    L.SetGlobal("Destroy", LV::Obj(L.NewNative(DestroyFn, "Destroy")));
    // what is there before the script runs is not the script's own
    LV k, v;
    while (L.Globals()->Next(L, k, v))
        if (k.IsString()) I.builtinGlobals.insert(k.Str()->s);
}

std::string CamelCase(const std::string& snake) {
    std::string out;
    bool up = true;
    for (char ch : snake) {
        if (ch == '_') { up = true; continue; }
        out += up ? (char)std::toupper((unsigned char)ch) : ch;
        up = false;
    }
    return out;
}

// The handler names to look for, in order, for an engine name.
std::vector<std::string> HandlerNames(const std::string& fn) {
    static const struct { const char* engine; const char* a; const char* b; } map[] = {
        {"on_fixed_update", "FixedUpdate", "fixed_update"}, {"on_late_update", "LateUpdate", "late_update"},
        {"on_enable", "OnEnable", "on_enable"}, {"on_disable", "OnDisable", "on_disable"},
        {"on_destroy", "OnDestroy", "on_destroy"}, {"Start", "Start", "start"}, {"Awake", "Awake", "awake"},
        {"Update", "Update", "update"}, {"OnEnable", "OnEnable", "on_enable"},
    };
    std::vector<std::string> out;
    for (const auto& m : map)
        if (fn == m.engine) { out.push_back(m.a); out.push_back(m.b); if (fn != m.a && fn != m.b) out.push_back(fn); return out; }
    out.push_back(fn);
    if (fn.find('_') != std::string::npos) out.push_back(CamelCase(fn));
    return out;
}

vs::VsValue ToVs(const LV& v) {
    switch (v.t) {
        case lua::Type::Bool: return vs::VsValue{v.b};
        case lua::Type::Int: case lua::Type::Float: return vs::VsValue{(float)v.AsNum()};
        case lua::Type::String: return vs::VsValue{v.Str()->s};
        default:
            if (IsVec(v)) return vs::VsValue{VecOf(v)};
            if (IsObj(v)) return vs::VsValue::Object(v.UD()->id);
            return vs::VsValue{};
    }
}

LV FromVs(State& L, const vs::VsValue& v, const LV& like = LV()) {
    if (v.IsBool()) return LV::Bool(v.AsBool());
    if (v.IsNumber()) {
        double d = v.AsFloat();
        // a whole number stays an integer where the script had one
        if ((like.t == lua::Type::Int || like.IsNil()) && d == std::floor(d) && std::fabs(d) < 9.0e15) return LV::Int((std::int64_t)d);
        return LV::Num(d);
    }
    if (v.IsString()) return Str(L, v.AsString());
    if (v.IsVec3()) return MakeVec(L, v.AsVec3());
    if (v.IsObject()) {
        GameObject* g = GameObject::FromInstanceId(v.AsObjectId());
        return g ? MakeObj(L, g->InstanceId(), Api().gameObject) : LV();
    }
    if (v.IsArray()) {
        lua::Table* t = L.NewTable();
        L.Push(LV::Obj(t));
        std::int64_t k = 1;
        if (auto a = v.AsArray())
            for (const auto& x : *a) t->SetInt(L, k++, FromVs(L, x));
        L.Pop();
        return LV::Obj(t);
    }
    return LV();
}

bool IsSetting(const LV& v) { return v.IsNumber() || v.IsString() || v.t == lua::Type::Bool || IsVec(v); }

} // namespace

LuaScriptVM::LuaScriptVM() : m_impl(std::make_unique<Impl>()) {
    m_impl->owner = this;
    m_impl->L.OpenLibs();
    InstallBindings(*m_impl);
}

LuaScriptVM::~LuaScriptVM() = default;

std::vector<std::string> LuaScriptVM::CompletionNames() {
    std::set<std::string> names;
    for (const char* k : kLuaKeywords) names.insert(k);
    for (const char* n : {"print", "pairs", "ipairs", "next", "select", "type", "tostring", "tonumber", "pcall", "xpcall",
                          "error", "assert", "setmetatable", "getmetatable", "rawget", "rawset", "rawequal", "rawlen",
                          "load", "string", "table", "math", "coroutine", "os", "self", "GetComponent", "Destroy",
                          "gameObject", "transform", "string.format", "string.sub", "string.find", "string.gsub",
                          "string.match", "string.gmatch", "string.rep", "string.upper", "string.lower", "string.len",
                          "table.insert", "table.remove", "table.concat", "table.sort", "table.unpack", "math.floor",
                          "math.ceil", "math.abs", "math.sqrt", "math.random", "math.min", "math.max", "math.pi",
                          "math.huge", "math.sin", "math.cos", "coroutine.create", "coroutine.resume",
                          "coroutine.yield", "coroutine.wrap", "coroutine.status", "os.clock", "os.time", "Vector3.new"})
        names.insert(n);
    for (const sc::ApiType& t : Api().types) {
        if (t.name.empty()) continue;
        names.insert(t.name);
        for (auto& p : t.props) names.insert(p.name);
        for (auto& m : t.methods) names.insert(m.name);
        for (auto& p : t.staticProps) names.insert(t.name + "." + p.name);
        for (auto& m : t.staticMethods) names.insert(t.name + "." + m.name);
    }
    for (const char* l : {"Awake", "Start", "Update", "LateUpdate", "FixedUpdate", "OnEnable", "OnDisable", "OnDestroy",
                          "OnTriggerEnter", "OnTriggerStay", "OnTriggerExit", "OnCollisionEnter", "OnCollisionStay",
                          "OnCollisionExit"})
        names.insert(l);
    return {names.begin(), names.end()};
}

std::vector<std::string> LuaScriptVM::BuiltinNames() const { return CompletionNames(); }

namespace {
const char* const kLibMembers[][2] = {
    {"string", "byte char find format gmatch gsub len lower match rep reverse sub upper"},
    {"table", "concat insert move pack remove sort unpack"},
    {"math", "abs acos asin atan ceil cos deg exp floor fmod huge log max maxinteger min mininteger modf pi rad random randomseed sin sqrt tan tointeger type ult"},
    {"coroutine", "close create isyieldable resume running status wrap yield"},
    {"os", "clock date difftime time"},
};
const char* const kHandlers[] = {"Awake", "Start", "Update", "LateUpdate", "FixedUpdate", "OnEnable", "OnDisable",
                                 "OnDestroy", "OnTriggerEnter", "OnTriggerStay", "OnTriggerExit", "OnCollisionEnter",
                                 "OnCollisionStay", "OnCollisionExit"};

std::vector<std::string> Words(const char* s) {
    std::vector<std::string> out;
    std::string w;
    for (const char* p = s;; ++p) {
        if (*p == ' ' || *p == 0) { if (!w.empty()) out.push_back(w); w.clear(); if (!*p) break; }
        else w += *p;
    }
    return out;
}

void AddTypeMembers(const sc::ApiType& t, bool statics, std::set<std::string>& out) {
    if (statics) {
        for (auto& p : t.staticProps) out.insert(p.name);
        for (auto& m : t.staticMethods) out.insert(m.name);
    } else {
        for (auto& p : t.props) out.insert(p.name);
        for (auto& m : t.methods) out.insert(m.name);
    }
}

int EditDistance(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = (int)j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = (int)i;
        for (std::size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

struct LintTok { std::string text; int line, col; };

// Identifiers and punctuation of a Lua source, skipping strings and comments.
std::vector<LintTok> LintTokens(const std::string& s) {
    std::vector<LintTok> out;
    int line = 1, col = 1;
    std::size_t i = 0;
    auto adv = [&](std::size_t n) {
        for (std::size_t k = 0; k < n && i < s.size(); ++k, ++i) {
            if (s[i] == '\n') { ++line; col = 1; } else ++col;
        }
    };
    auto longLevel = [&](std::size_t at) -> int {   // at '[': level of a long bracket, or -1
        std::size_t k = at + 1;
        int lvl = 0;
        while (k < s.size() && s[k] == '=') { ++lvl; ++k; }
        return (k < s.size() && s[k] == '[') ? lvl : -1;
    };
    auto skipLong = [&](int lvl) {
        adv((std::size_t)lvl + 2);
        std::string close = "]" + std::string((std::size_t)lvl, '=') + "]";
        std::size_t e = s.find(close, i);
        adv(e == std::string::npos ? s.size() - i : e - i + close.size());
    };
    while (i < s.size()) {
        char c = s[i];
        if (c == '-' && i + 1 < s.size() && s[i + 1] == '-') {
            adv(2);
            if (i < s.size() && s[i] == '[' && longLevel(i) >= 0) { skipLong(longLevel(i)); continue; }
            while (i < s.size() && s[i] != '\n') adv(1);
            continue;
        }
        if (c == '[' && longLevel(i) >= 0) { skipLong(longLevel(i)); continue; }
        if (c == '"' || c == '\'') {
            adv(1);
            while (i < s.size() && s[i] != c && s[i] != '\n') adv(s[i] == '\\' ? 2 : 1);
            adv(1);
            continue;
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            LintTok t{"", line, col};
            while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) { t.text += s[i]; adv(1); }
            out.push_back(t);
            continue;
        }
        if (c == '.' || c == ':' || c == '(') { out.push_back(LintTok{std::string(1, c), line, col}); adv(1); continue; }
        adv(1);
    }
    return out;
}
} // namespace

std::vector<std::string> LuaScriptVM::CompletionMembers(const std::string& receiver) {
    std::string last = receiver;
    std::size_t d = last.find_last_of(".:");
    if (d != std::string::npos) last = last.substr(d + 1);
    std::set<std::string> out;
    const auto& api = Api();
    for (const auto& lib : kLibMembers)
        if (receiver == lib[0])
            for (const std::string& w : Words(lib[1])) out.insert(w);
    if (out.empty()) {
        int idx = api.Find(receiver);
        if (idx >= 0) {   // a static type: Input., Time., Vector3.
            AddTypeMembers(api.types[(std::size_t)idx], true, out);
            if (idx == api.vector3) out.insert("new");
        } else if (receiver == "self") {
            out.insert("gameObject");
            out.insert("transform");
        } else if (last == "transform") {
            AddTypeMembers(api.types[(std::size_t)api.transform], false, out);
            out.insert("GetComponent");
        } else if (last == "gameObject" || last == "other") {
            AddTypeMembers(api.types[(std::size_t)api.gameObject], false, out);
            out.insert("GetComponent");
        } else {
            // a Vector3-valued property (position, forward, velocity, ...)
            bool vec = false;
            for (const sc::ApiType& t : api.types)
                for (const sc::ApiProperty& p : t.props)
                    if (p.name == last && p.type.kind == sc::TK::Vector3) vec = true;
            if (vec) {
                for (const char* n : {"x", "y", "z", "magnitude", "normalized", "sqrMagnitude"}) out.insert(n);
            }
        }
    }
    return {out.begin(), out.end()};
}

std::vector<LuaScriptVM::Setting> LuaScriptVM::Settings(const std::string& source) {
    std::vector<Setting> out;
    LuaScriptVM vm;
    vm.SetBudget(1'000'000);
    vm.m_quiet = true;
    if (!vm.Load(source, nullptr)) return out;
    std::vector<std::pair<std::size_t, Setting>> found;
    for (const std::string& n : vm.GlobalNames()) {
        vs::VsValue v = vm.GetGlobal(n);
        Setting st;
        st.name = n;
        if (v.IsBool()) { st.type = "boolean"; st.defaultText = v.AsBool() ? "true" : "false"; }
        else if (v.IsNumber()) {
            st.type = "number";
            st.defaultText = State::NumberToString(LV::Num((double)v.AsFloat()));
            if (st.defaultText.size() > 2 && st.defaultText.compare(st.defaultText.size() - 2, 2, ".0") == 0)
                st.defaultText.resize(st.defaultText.size() - 2);
        } else if (v.IsString()) { st.type = "string"; st.defaultText = "\"" + v.AsString() + "\""; }
        else if (v.IsVec3()) {
            Vec3 p = v.AsVec3();
            auto f = [](float x) { std::string t = State::NumberToString(LV::Num((double)x)); if (t.size() > 2 && t.compare(t.size() - 2, 2, ".0") == 0) t.resize(t.size() - 2); return t; };
            st.type = "Vector3";
            st.defaultText = "Vector3(" + f(p.x) + ", " + f(p.y) + ", " + f(p.z) + ")";
        } else continue;
        // where the name is first written in the source, for the Inspector's order
        std::size_t pos = std::string::npos;
        for (std::size_t at = source.find(n); at != std::string::npos; at = source.find(n, at + 1)) {
            bool startOk = at == 0 || !(std::isalnum((unsigned char)source[at - 1]) || source[at - 1] == '_');
            std::size_t e = at + n.size();
            bool endOk = e >= source.size() || !(std::isalnum((unsigned char)source[e]) || source[e] == '_');
            if (startOk && endOk) { pos = at; break; }
        }
        found.push_back({pos, st});
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& f : found) out.push_back(f.second);
    return out;
}

std::vector<ScriptDiagnostic> LuaScriptVM::Lint(const std::string& source) {
    std::vector<ScriptDiagnostic> out;
    const auto& api = Api();
    std::vector<LintTok> t = LintTokens(source);
    for (std::size_t i = 0; i + 2 < t.size(); ++i) {
        // Type.member on an engine type with static members
        if ((t[i + 1].text == "." || t[i + 1].text == ":") && (i == 0 || (t[i - 1].text != "." && t[i - 1].text != ":"))) {
            int idx = api.Find(t[i].text);
            if (idx < 0) continue;
            const sc::ApiType& ty = api.types[(std::size_t)idx];
            if (ty.staticProps.empty() && ty.staticMethods.empty() && idx != api.vector3) continue;
            const std::string& m = t[i + 2].text;
            if (!(std::isalpha((unsigned char)m[0]) || m[0] == '_')) continue;
            std::set<std::string> names;
            AddTypeMembers(ty, true, names);
            if (idx == api.vector3) names.insert("new");
            if (names.count(m)) continue;
            std::string best;
            int bestD = 3;
            for (const std::string& n : names) {
                int dd = EditDistance(m, n);
                if (dd < bestD) { bestD = dd; best = n; }
            }
            std::string msg = ty.name + " has no member '" + m + "'";
            if (!best.empty()) msg += ". Did you mean " + best + "?";
            out.push_back(ScriptDiagnostic{t[i + 2].line, msg, 1, t[i + 2].col});
        }
    }
    // function X:Name( / function X.Name( / function Name( close to a handler name
    for (std::size_t i = 0; i + 1 < t.size(); ++i) {
        if (t[i].text != "function") continue;
        std::size_t k = i + 1;
        while (k + 2 < t.size() && (t[k + 1].text == "." || t[k + 1].text == ":")) k += 2;
        if (k >= t.size()) continue;
        const std::string& n = t[k].text;
        bool known = false;
        std::string near;
        for (const char* h : kHandlers) {
            if (n == h) { known = true; break; }
            std::string lower = h;
            for (char& ch : lower) ch = (char)std::tolower((unsigned char)ch);
            if (n == lower) { known = true; break; }   // old style: update, start
            if (near.empty() && EditDistance(n, h) <= 2 && n.size() >= 4) near = h;
        }
        if (!known && !near.empty())
            out.push_back(ScriptDiagnostic{t[k].line, n + " is never called by the engine. Did you mean " + near + "? (names are case-sensitive)", 1, t[k].col});
    }
    std::stable_sort(out.begin(), out.end(), [](const ScriptDiagnostic& a, const ScriptDiagnostic& b) { return a.line < b.line; });
    return out;
}

bool LuaScriptVM::Validate(const std::string& source, std::string* error) {
    State tmp;
    auto errs = tmp.CheckSyntax(source, "script");
    if (errs.empty()) return true;
    if (error) *error = "line " + std::to_string(errs[0].line) + ": " + errs[0].bare;
    return false;
}

std::vector<ScriptDiagnostic> LuaScriptVM::ValidateAll(const std::string& source) {
    State tmp;
    std::vector<ScriptDiagnostic> out;
    for (const lua::SyntaxError& e : tmp.CheckSyntax(source, "script"))
        out.push_back(ScriptDiagnostic{e.line, e.bare, 0, e.column});
    std::vector<ScriptDiagnostic> warn = Lint(source);
    out.insert(out.end(), warn.begin(), warn.end());
    return out;
}

bool LuaScriptVM::Load(const std::string& source, std::string* error) {
    // a fresh interpreter for each load (a reload starts clean)
    m_impl = std::make_unique<Impl>();
    m_impl->owner = this;
    m_impl->L.OpenLibs();
    InstallBindings(*m_impl);
    State& L = m_impl->L;
    if (m_quiet) L.printSink = nullptr;
    lua::SyntaxError se;
    if (!L.Load(source, "script", &se)) {
        if (error) *error = "line " + std::to_string(se.line) + ": " + se.bare;
        return false;
    }
    L.SetBudget(m_budget);
    std::string err;
    int before = L.Top() - 1;
    if (!L.PCallHost(0, 1, &err)) {
        if (error) *error = err;
        L.SetTop(before);
        return false;
    }
    LV r = L.Pop();
    L.SetTop(before);
    m_impl->tableStyle = r.IsTable();
    if (m_impl->tableStyle) m_impl->scriptRef = L.Ref(r);
    m_impl->loaded = true;
    if (m_host) Bind(m_host);
    return true;
}

void LuaScriptVM::Bind(ScriptHost* host) {
    m_host = host;
    if (!m_impl->loaded || !host) return;
    State& L = m_impl->L;
    m_impl->ctx.self = host->gameObject;
    m_impl->ctx.component = host->component;
    LV go = host->gameObject ? MakeObj(L, host->gameObject->InstanceId(), Api().gameObject) : LV();
    L.Push(go);
    LV tr = host->gameObject ? MakeObj(L, host->gameObject->InstanceId(), Api().transform) : LV();
    L.Push(tr);
    L.SetGlobal("gameObject", go);
    L.SetGlobal("transform", tr);
    if (m_impl->tableStyle) {
        LV t = L.GetRef(m_impl->scriptRef);
        t.Tab()->Set(L, Str(L, "gameObject"), go);
        t.Tab()->Set(L, Str(L, "transform"), tr);
    }
    L.SetTop(L.Top() - 2);
}

bool LuaScriptVM::Run(const std::string& engineName, const std::vector<vs::VsValue>& args, vs::VsValue* ret,
                      std::string* error) {
    if (!m_impl->loaded) return false;
    State& L = m_impl->L;
    // find the handler
    LV fn, self;
    std::string used;
    LV holder = m_impl->tableStyle ? L.GetRef(m_impl->scriptRef) : LV::Obj(L.Globals());
    for (const std::string& n : HandlerNames(engineName)) {
        LV f = holder.Tab()->Get(Str(L, n));
        if (f.IsNil() && holder.Tab()->meta) {
            // a script table with a metatable (class-style inheritance)
            L.Push(holder);
            L.Push(Str(L, n));
            try { f = L.Index(holder, L.At(-1)); } catch (...) { f = LV(); }
            L.SetTop(L.Top() - 2);
        }
        if (f.IsFunction()) { fn = f; used = n; break; }
    }
    if (fn.IsNil()) return false;
    if (m_host) {
        m_impl->ctx.self = m_host->gameObject;
        m_impl->ctx.component = m_host->component;
        m_impl->ctx.deltaTime = m_host->deltaTime;
    }
    int top = L.Top();
    L.Push(fn);
    int nargs = 0;
    if (m_impl->tableStyle) { L.Push(holder); ++nargs; }
    for (const auto& a : args) { L.Push(FromVs(L, a)); ++nargs; }
    if (m_impl->running == 0) L.SetBudget(m_budget);
    ++m_impl->running;
    std::string err;
    bool ok = L.PCallHost(nargs, 1, &err);
    --m_impl->running;
    LV r = ok ? L.At(-1) : LV();
    if (ret) *ret = ToVs(r);
    L.SetTop(top);
    if (!ok) {
        std::string msg = used + "(): " + err;
        if (error) { *error = msg; return true; }
        ++m_errorCount;
        m_lastError = err;
        const GameObject* g = m_impl->ctx.self;
        Log::Error("Script on '", g ? g->name : std::string("?"), "', ", msg);
    }
    return true;
}

void LuaScriptVM::CallStart() {
    Run("Awake", {}, nullptr, nullptr);
    Run("OnEnable", {}, nullptr, nullptr);
    Run("Start", {}, nullptr, nullptr);
}

void LuaScriptVM::CallUpdate(float deltaTime) {
    if (m_host) m_host->deltaTime = deltaTime;
    m_impl->ctx.deltaTime = deltaTime;
    Run("Update", {vs::VsValue{deltaTime}}, nullptr, nullptr);
}

void LuaScriptVM::CallEvent(const std::string& function) {
    std::vector<vs::VsValue> args;
    if (function == "on_fixed_update" && m_host) args.push_back(vs::VsValue{m_host->deltaTime});
    Run(function, args, nullptr, nullptr);
}

void LuaScriptVM::CallEventWith(const std::string& function, const vs::VsValue& arg) {
    Run(function, {arg}, nullptr, nullptr);
}

vs::VsValue LuaScriptVM::CallEventArgs(const std::string& function, const std::vector<vs::VsValue>& args,
                                       std::string* error) {
    vs::VsValue r;
    Run(function, args, &r, error);
    return r;
}

bool LuaScriptVM::HasEvent(const std::string& function) const {
    if (!m_impl->loaded) return false;
    State& L = m_impl->L;
    LV holder = m_impl->tableStyle ? L.GetRef(m_impl->scriptRef) : LV::Obj(L.Globals());
    for (const std::string& n : HandlerNames(function))
        if (holder.Tab()->Get(Str(L, n)).IsFunction()) return true;
    return false;
}

vs::VsValue LuaScriptVM::GetGlobal(const std::string& name) const {
    if (!m_impl->loaded) return {};
    State& L = m_impl->L;
    LV k = Str(L, name);
    if (m_impl->tableStyle) {
        LV v = L.GetRef(m_impl->scriptRef).Tab()->Get(k);
        if (!v.IsNil()) return ToVs(v);
    }
    return ToVs(L.Globals()->Get(k));
}

void LuaScriptVM::SetGlobal(const std::string& name, const vs::VsValue& v) {
    if (!m_impl->loaded) return;
    State& L = m_impl->L;
    LV k = Str(L, name);
    L.Push(k);
    if (m_impl->tableStyle) {
        lua::Table* t = L.GetRef(m_impl->scriptRef).Tab();
        LV cur = t->Get(k);
        if (!cur.IsNil() && !cur.IsFunction()) {   // one of the script's settings
            LV nv = FromVs(L, v, cur);
            t->Set(L, k, nv);
            L.Pop();
            return;
        }
    }
    LV cur = L.Globals()->Get(k);
    LV nv = FromVs(L, v, cur);
    L.Globals()->Set(L, k, nv);
    L.Pop();
}

std::vector<std::string> LuaScriptVM::GlobalNames() const {
    std::vector<std::string> out;
    if (!m_impl->loaded) return out;
    State& L = m_impl->L;
    lua::Table* t = m_impl->tableStyle ? L.GetRef(m_impl->scriptRef).Tab() : L.Globals();
    LV k, v;
    while (t->Next(L, k, v)) {
        if (!k.IsString() || !IsSetting(v)) continue;
        const std::string& n = k.Str()->s;
        if (IsEngineSet(n)) continue;
        if (!m_impl->tableStyle && m_impl->builtinGlobals.count(n)) continue;
        out.push_back(n);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace okay
