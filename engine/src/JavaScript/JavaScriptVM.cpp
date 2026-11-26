// JavaScript game scripts: IScriptVM for language "javascript" on okay::js.
//
// The engine API is the one Lua and the C#-style language use (no second binding
// system, CLAUDE.md 19c): the typed binding table (Script/EngineApi, feature 14.7)
// for GameObject, Transform, Vector3, Input, Time, Debug, Mathf, ..., then the
// generated binding layer (Scripting/ScriptBindings, the one OkayScript and Lua
// use) for every other component and for members the typed table lacks. Engine
// objects are host objects holding an object id (and which component); a
// destroyed object reads as an error, never as freed memory. The same engine
// object always gives the same JavaScript object, so === works on them.
// Static types (Input, Time, ...) are global objects whose members are accessor
// properties and functions; Vector3 is also a constructor (new Vector3(1, 2, 3)).
#include "okay/JavaScript/JavaScriptVM.hpp"
#include "JsInternal.hpp"
#include "okay/Scripting/ScriptBindings.hpp"
#include "../Script/ScriptInternal.hpp"
#include "../Scripting/ScriptLanguageGlue.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <set>
#include <sstream>
#include <unordered_map>

namespace okay {

using js::Value;
namespace sc = script;
namespace glue = scriptglue;

struct JavaScriptVM::Impl {
    js::Runtime rt;
    sc::CallContext ctx;
    bool loaded = false;
    int style = 0;           ///< 0 global functions, 1 an exported object, 2 an instance of the exported class
    int scriptRef = -1;      ///< the script object (styles 1 and 2)
    int running = 0;         ///< nested calls into this VM (the budget is set by the outermost one)
    std::uint32_t kObj = 0, kVec = 0, kComp = 0;
    js::Object* objProto = nullptr;
    js::Object* vecProto = nullptr;
    js::Object* compProto = nullptr;
    js::Object* getComponentFn = nullptr;
    std::unordered_map<std::string, int> methodRefs;            ///< "type:static:name" -> bound native (a ref)
    std::unordered_map<std::uint64_t, js::HostObj*> objCache;   ///< (id, type) -> its one host object
    std::unordered_map<std::string, js::HostObj*> compCache;    ///< "id:class:index" -> its one host object
    std::set<std::string> builtinGlobals;
    Impl() { rt.userData = this; }
    // The runtime is destroyed after the caches below it; it must not call back into them while it frees.
    ~Impl() { rt.onHostFree = nullptr; }
};

namespace {

using Impl = JavaScriptVM::Impl;

Impl& Ctx(js::Runtime& rt) { return *static_cast<Impl*>(rt.userData); }
const sc::EngineApi& Api() { return sc::EngineApi::Get(); }

// Engine types JavaScript does not see by that name: denied classes and names the language already uses.
bool Hidden(const sc::ApiType& t) {
    if (t.name.empty() || scriptbind::IsDeniedClass(t.name)) return true;
    static const char* const taken[] = {"Object", "Array", "String", "Number", "Boolean", "Math", "JSON", "Map", "Set",
                                        "Symbol", "Promise", "Error", "Function", "Reflect", "console", "globalThis",
                                        "string", "int", "float", "bool", "double", "object", "void", "var"};
    for (const char* n : taken)
        if (t.name == n) return true;
    return false;
}
int FindType(const std::string& name) {
    int idx = Api().Find(name);
    return idx >= 0 && Hidden(Api().types[(std::size_t)idx]) ? -1 : idx;
}

js::String* S(js::Runtime& rt, const std::string& s) { return rt.NewStringUtf8(s); }

// An engine float as the shortest double that reads back as the same float, so an Inspector 0.1 is 0.1 in
// the script and not 0.10000000149011612.
double Nice(float f) {
    if (!std::isfinite(f) || f == std::trunc(f)) return (double)f;
    char buf[32];
    for (int p = 1; p <= 9; ++p) {
        std::snprintf(buf, sizeof buf, "%.*g", p, (double)f);
        double d = std::strtod(buf, nullptr);
        if ((float)d == f) return d;
    }
    return (double)f;
}
std::string U8(const js::String* s) { return js::Utf16To8(s->s); }

int CurrentLine(js::Runtime& rt) {
    std::string w = rt.Where();
    std::size_t c = w.rfind(':');
    return c == std::string::npos ? 0 : std::atoi(w.c_str() + c + 1);
}
void Prepare(js::Runtime& rt) {
    Impl& I = Ctx(rt);
    I.ctx.loc.line = CurrentLine(rt);
    I.ctx.loc.col = 0;
}

bool IsHost(const Value& v, std::uint32_t cls) {
    return v.IsObj() && v.O()->cls == js::Cls::Host && static_cast<js::HostObj*>(v.O())->hostClass == cls;
}
js::HostObj* H(const Value& v) { return static_cast<js::HostObj*>(v.O()); }

Value MakeVec(js::Runtime& rt, const Vec3& v) {
    Impl& I = Ctx(rt);
    js::HostObj* h = rt.NewHost(I.kVec, I.vecProto);
    h->v[0] = v.x;
    h->v[1] = v.y;
    h->v[2] = v.z;
    return Value::Obj(h);
}
Vec3 VecOf(const Value& v) { return Vec3{(float)H(v)->v[0], (float)H(v)->v[1], (float)H(v)->v[2]}; }

Value MakeObj(js::Runtime& rt, std::uint64_t id, int type) {
    if (id == 0) return Value::Null();
    Impl& I = Ctx(rt);
    std::uint64_t key = (id << 12) ^ (std::uint64_t)(type & 0xFFF);
    auto it = I.objCache.find(key);
    if (it != I.objCache.end() && it->second->id == id && it->second->i32 == type) return Value::Obj(it->second);
    js::HostObj* h = rt.NewHost(I.kObj, I.objProto);
    h->id = id;
    h->i32 = type;
    I.objCache[key] = h;
    return Value::Obj(h);
}

std::string CompKey(const vs::VsObject& r) {
    return std::to_string(r.id) + ":" + std::to_string(r.comp) + ":" + std::to_string(r.index);
}
Value MakeComp(js::Runtime& rt, const vs::VsValue& handle) {
    if (!scriptbind::Resolve(handle)) return Value::Null();
    Impl& I = Ctx(rt);
    const vs::VsObject r = handle.AsObjectRef();
    std::string key = CompKey(r);
    auto it = I.compCache.find(key);
    if (it != I.compCache.end()) return Value::Obj(it->second);
    js::HostObj* h = rt.NewHost(I.kComp, I.compProto);
    h->id = r.id;
    h->i32 = (std::int32_t)r.comp;
    h->u32 = r.index;
    I.compCache[key] = h;
    return Value::Obj(h);
}
vs::VsValue CompHandle(const Value& v) {
    return vs::VsValue::ComponentHandle(H(v)->id, (std::uint32_t)H(v)->i32, H(v)->u32);
}

std::string JsTypeWord(js::Runtime& rt, const Value& v) {
    Impl& I = Ctx(rt);
    if (IsHost(v, I.kVec)) return "Vector3";
    if (IsHost(v, I.kObj)) {
        int t = H(v)->i32;
        return t >= 0 && t < (int)Api().types.size() ? Api().types[(std::size_t)t].name : "object";
    }
    if (IsHost(v, I.kComp)) {
        const scriptbind::ClassBinding* c = scriptbind::ClassByKey((std::uint32_t)H(v)->i32);
        return c ? c->name : "component";
    }
    if (v.IsNull()) return "null";
    if (rt.IsArray(v)) return "an array";
    return rt.TypeOf(v);
}

// A JavaScript value as an engine value of type t, or false (with why).
bool FromJs(js::Runtime& rt, const Value& v, const sc::Type& t, sc::Value& out, std::string& why) {
    Impl& I = Ctx(rt);
    auto wrong = [&]() {
        why = glue::TypeWord(t) + " expected, got " + JsTypeWord(rt, v);
        return false;
    };
    switch (t.kind) {
        case sc::TK::Float:
            if (!v.IsNum()) return wrong();
            out = sc::Value{(float)v.n};
            return true;
        case sc::TK::Int: case sc::TK::Enum: {
            if (!v.IsNum()) return wrong();
            double d = std::trunc(v.n);
            if (!(d >= -2147483648.0 && d <= 2147483647.0)) { why = "number out of range for a whole number"; return false; }
            out = sc::Value{(std::int32_t)d};
            return true;
        }
        case sc::TK::Bool:
            if (!v.IsBool()) return wrong();
            out = sc::Value{v.b};
            return true;
        case sc::TK::String:
            if (v.IsStr()) { out = sc::Value{U8(v.S())}; return true; }
            if (v.IsNum()) { out = sc::Value{js::Runtime::NumberToStdString(v.n)}; return true; }
            return wrong();
        case sc::TK::Vector3:
            if (!IsHost(v, I.kVec)) return wrong();
            out = sc::Value{VecOf(v)};
            return true;
        case sc::TK::Object:
            if (v.IsNullish()) { out = sc::Value{}; return true; }
            if (!IsHost(v, I.kObj)) return wrong();
            out = sc::Value{sc::ObjRef{H(v)->id, t.id}};
            return true;
        case sc::TK::Any:
            switch (v.t) {
                case js::Tag::Undefined: case js::Tag::Null: out = sc::Value{}; return true;
                case js::Tag::Bool: out = sc::Value{v.b}; return true;
                case js::Tag::Number:
                    if (v.n == std::trunc(v.n) && v.n >= -2147483648.0 && v.n <= 2147483647.0) out = sc::Value{(std::int32_t)v.n};
                    else out = sc::Value{(float)v.n};
                    return true;
                case js::Tag::String: out = sc::Value{U8(v.S())}; return true;
                default:
                    if (IsHost(v, I.kVec)) { out = sc::Value{VecOf(v)}; return true; }
                    if (IsHost(v, I.kObj)) { out = sc::Value{sc::ObjRef{H(v)->id, H(v)->i32}}; return true; }
                    out = sc::Value{rt.Display(v)};
                    return true;
            }
        case sc::TK::Array: {
            if (!rt.IsArray(v)) return wrong();
            auto arr = std::make_shared<sc::ArrayObj>();
            arr->typeName = "string[]";
            std::uint64_t n = std::min<std::uint64_t>(rt.LengthOf(v), 100000);
            for (std::uint64_t i = 0; i < n; ++i) {
                Value e = rt.GetIndex(v, (std::uint32_t)i);
                arr->items.push_back(sc::Value{e.IsStr() ? U8(e.S()) : rt.Display(e)});
            }
            out = sc::Value{arr};
            return true;
        }
        default:
            why = "this kind of value can't be passed from JavaScript";
            return false;
    }
}

Value ToJs(js::Runtime& rt, const sc::Value& v, int depth = 0) {
    if (std::holds_alternative<std::monostate>(v)) return Value();
    if (auto* b = std::get_if<bool>(&v)) return Value::Bool(*b);
    if (auto* i = std::get_if<std::int32_t>(&v)) return Value::Num(*i);
    if (auto* f = std::get_if<float>(&v)) return Value::Num(Nice(*f));
    if (auto* s = std::get_if<std::string>(&v)) return Value::Str(S(rt, *s));
    if (auto* p = std::get_if<Vec3>(&v)) return MakeVec(rt, *p);
    if (auto* o = std::get_if<sc::ObjRef>(&v)) return MakeObj(rt, o->go, o->type);
    if (auto* a = std::get_if<sc::ArrayRef>(&v)) {
        if (!*a || depth > 4) return Value();
        js::ArrayObj* arr = rt.NewArray((*a)->items.size());
        for (const sc::Value& x : (*a)->items) {
            Value e = ToJs(rt, x, depth + 1);
            rt.DefineIndex(arr, arr->length, e);
        }
        return Value::Obj(arr);
    }
    return Value();
}

sc::Value SelfValue(js::Runtime& rt, const Value& v) {
    Impl& I = Ctx(rt);
    if (IsHost(v, I.kVec)) return sc::Value{VecOf(v)};
    if (IsHost(v, I.kObj)) return sc::Value{sc::ObjRef{H(v)->id, H(v)->i32}};
    return sc::Value{};
}

vs::VsValue ToVs(js::Runtime& rt, const Value& v) {
    Impl& I = Ctx(rt);
    switch (v.t) {
        case js::Tag::Bool: return vs::VsValue{v.b};
        case js::Tag::Number: return vs::VsValue{(float)v.n};
        case js::Tag::String: return vs::VsValue{U8(v.S())};
        default: break;
    }
    if (IsHost(v, I.kVec)) return vs::VsValue{VecOf(v)};
    if (IsHost(v, I.kObj)) return vs::VsValue::Object(H(v)->id);
    if (IsHost(v, I.kComp)) return CompHandle(v);
    if (v.IsObj() && !v.O()->IsCallable() && v.O()->cls == js::Cls::Ordinary) {   // {r, g, b[, a]}: a colour
        struct Fields { js::Runtime* rt; js::Object* o; } f{&rt, v.O()};
        vs::VsValue c;
        auto field = [](void* ctx, const char* k, double fallback) {
            Fields* x = static_cast<Fields*>(ctx);
            js::Prop* p = x->o->FindRaw(x->rt->Intern(k));
            return p && p->v.IsNum() ? p->v.n : fallback;
        };
        if (glue::ColorFromFields(field, &f, c)) return c;
    }
    return vs::VsValue{};
}

Value FromVs(js::Runtime& rt, const vs::VsValue& v) {
    if (v.IsBool()) return Value::Bool(v.AsBool());
    if (v.IsNumber()) return Value::Num(Nice(v.AsFloat()));
    if (v.IsString()) return Value::Str(S(rt, v.AsString()));
    if (v.IsVec3()) return MakeVec(rt, v.AsVec3());
    if (v.IsComponentHandle()) return MakeComp(rt, v);
    if (v.IsColor()) {
        const vs::VsColor c = v.AsColor();
        js::Object* o = rt.NewObject();
        rt.DefineData(o, rt.Intern("r"), Value::Num(c.r));
        rt.DefineData(o, rt.Intern("g"), Value::Num(c.g));
        rt.DefineData(o, rt.Intern("b"), Value::Num(c.b));
        rt.DefineData(o, rt.Intern("a"), Value::Num(c.a));
        return Value::Obj(o);
    }
    if (v.IsObject()) {
        GameObject* g = GameObject::FromInstanceId(v.AsObjectId());
        return g ? MakeObj(rt, g->InstanceId(), Api().gameObject) : Value::Null();
    }
    if (v.IsArray()) {
        js::ArrayObj* arr = rt.NewArray();
        if (auto a = v.AsArray())
            for (const auto& x : *a) {
                Value e = FromVs(rt, x);
                rt.DefineIndex(arr, arr->length, e);
            }
        return Value::Obj(arr);
    }
    return Value();
}

bool IsSetting(js::Runtime& rt, const Value& v) {
    return v.IsNum() || v.IsStr() || v.IsBool() || IsHost(v, Ctx(rt).kVec);
}

[[noreturn]] void EngineError(js::Runtime& rt, const std::exception& e) { rt.ThrowError(js::ErrorType::Error, e.what()); }

GameObject* ResolveObj(js::Runtime& rt, const Value& v) {
    if (!IsHost(v, Ctx(rt).kObj)) rt.ThrowType("an engine object is needed here, not " + JsTypeWord(rt, v));
    GameObject* g = GameObject::FromInstanceId(H(v)->id);
    if (!g) rt.ThrowError(js::ErrorType::Error, "This object was destroyed, so it can't be used any more");
    return g;
}

// ---- calling a typed-table method: slots {type index, static?, name}
Value ApiCall(js::Runtime& rt, js::CallArgs& a) {
    Impl& I = Ctx(rt);
    int typeIdx = (int)a.callee->slots[0].n;
    bool isStatic = a.callee->slots[1].b;
    const std::string name = U8(a.callee->slots[2].S());
    const sc::ApiType& t = Api().types[(std::size_t)typeIdx];
    sc::Value selfV;
    if (!isStatic) {
        bool ok = t.isValueType ? IsHost(a.thisv, I.kVec) : IsHost(a.thisv, I.kObj);
        if (!ok)
            rt.ThrowType("call " + name + " on a " + t.name + " (obj." + name + "(...)), not on " + JsTypeWord(rt, a.thisv));
        selfV = SelfValue(rt, a.thisv);
        if (IsHost(a.thisv, I.kObj)) std::get<sc::ObjRef>(selfV).type = typeIdx;
    }
    const auto& list = isStatic ? t.staticMethods : t.methods;
    std::string why, expected;
    for (const sc::ApiMethod& m : list) {
        if (m.name != name || glue::IsDenied(t, m)) continue;
        std::string sig = glue::Signature("", "", m);
        expected += (expected.empty() ? "" : " or ") + sig.substr(0, sig.find(" ->"));
        if ((int)m.params.size() != a.argc) continue;
        std::vector<sc::Value> args(m.params.size());
        std::size_t converted = 0;
        for (std::string w; converted < m.params.size(); ++converted)
            if (!FromJs(rt, a[(int)converted], m.params[converted], args[converted], w)) {
                why = "argument " + std::to_string(converted + 1) + ": " + w;
                break;
            }
        if (converted < m.params.size()) continue;
        std::string literal = glue::LiteralProblem(t, m, args);
        if (!literal.empty()) rt.ThrowError(js::ErrorType::Error, literal);
        Prepare(rt);
        sc::Value r;
        try {
            r = m.fn(selfV, args, I.ctx);
        } catch (const std::exception& e) {
            EngineError(rt, e);
        }
        if (m.ret.kind == sc::TK::Void) return Value();
        return ToJs(rt, r);
    }
    std::string msg = "bad arguments to " + t.name + "." + name + " (expected " + expected + ")";
    if (!why.empty()) msg += ": " + why;
    rt.ThrowType(msg);
}

Value BoundMethod(js::Runtime& rt, int typeIdx, bool isStatic, const std::string& name) {
    Impl& I = Ctx(rt);
    std::string key = std::to_string(typeIdx) + (isStatic ? ":s:" : ":i:") + name;
    auto it = I.methodRefs.find(key);
    if (it != I.methodRefs.end()) return rt.GetRef(it->second);
    js::NativeFunc* f = rt.NewNative(ApiCall, name.c_str(), 0, {Value::Num(typeIdx), Value::Bool(isStatic), Value::Str(S(rt, name))});
    I.methodRefs[key] = rt.Ref(Value::Obj(f));
    return Value::Obj(f);
}

bool HasMethod(const sc::ApiType& t, const std::vector<sc::ApiMethod>& list, const std::string& name) {
    return glue::HasCallable(t, list, name);
}

// ---- the generated binding layer (Scripting/ScriptBindings): handles go through ToVs / FromVs
bool HandleOf(js::Runtime& rt, const Value& self, vs::VsValue& out) {
    Impl& I = Ctx(rt);
    if (IsHost(self, I.kComp)) { out = CompHandle(self); return true; }
    if (!IsHost(self, I.kObj)) return false;
    int typeIdx = H(self)->i32;
    if (typeIdx < 0 || typeIdx >= (int)Api().types.size()) return false;
    return glue::HandleOnObject(*ResolveObj(rt, self), Api().types[(std::size_t)typeIdx].name, out);
}

Value BindCall(js::Runtime& rt, js::CallArgs& a) {   // slots {method name}
    const std::string name = U8(a.callee->slots[0].S());
    vs::VsValue h;
    if (!HandleOf(rt, a.thisv, h)) rt.ThrowType("call " + name + " on a component (comp." + name + "(...))");
    std::vector<vs::VsValue> args;
    for (int i = 0; i < a.argc; ++i) args.push_back(ToVs(rt, a[i]));
    vs::VsValue out;
    std::string err;
    Prepare(rt);
    scriptbind::Result r = scriptbind::CallMethod(h, name, args, out, err);
    if (r == scriptbind::Result::NotFound) rt.ThrowType("the component has no method '" + name + "'");
    if (r == scriptbind::Result::Error) rt.ThrowError(js::ErrorType::Error, err);
    return FromVs(rt, out);
}

bool BindIndex(js::Runtime& rt, const Value& self, const std::string& key, Value& out) {
    vs::VsValue h;
    if (!HandleOf(rt, self, h)) return false;
    vs::VsValue v;
    std::string err;
    scriptbind::Result r = scriptbind::GetMember(h, key, v, err);
    if (r == scriptbind::Result::Error) rt.ThrowError(js::ErrorType::Error, err);
    if (r == scriptbind::Result::Ok) {
        out = key == "gameObject" ? MakeObj(rt, v.AsObjectId(), Api().gameObject) : FromVs(rt, v);
        return true;
    }
    if (!glue::HandleHasMethod(h, key)) return false;
    Impl& I = Ctx(rt);
    std::string ck = "bind:" + key;
    auto it = I.methodRefs.find(ck);
    if (it != I.methodRefs.end()) { out = rt.GetRef(it->second); return true; }
    js::NativeFunc* f = rt.NewNative(BindCall, key.c_str(), 0, {Value::Str(S(rt, key))});
    I.methodRefs[ck] = rt.Ref(Value::Obj(f));
    out = Value::Obj(f);
    return true;
}

bool BindNewIndex(js::Runtime& rt, const Value& self, const std::string& key, const Value& val) {
    vs::VsValue h;
    if (!HandleOf(rt, self, h)) return false;
    std::string err;
    scriptbind::Result r = scriptbind::SetMember(h, key, ToVs(rt, val), err);
    if (r == scriptbind::Result::Error) rt.ThrowError(js::ErrorType::Error, err);
    return r == scriptbind::Result::Ok;
}

bool BindGetComponent(js::Runtime& rt, GameObject* g, const std::string& name, Value& out) {
    const scriptbind::ClassBinding* cls = scriptbind::FindClass(name);
    if (!cls) return false;
    Component* c = scriptbind::FindComponent(*g, *cls, 0);
    out = c ? MakeComp(rt, scriptbind::MakeHandle(c, cls)) : Value::Null();
    return true;
}

Value GetComponentFn(js::Runtime& rt, js::CallArgs& a) {
    Impl& I = Ctx(rt);
    GameObject* g = nullptr;
    if (IsHost(a.thisv, I.kObj)) g = ResolveObj(rt, a.thisv);
    else if (IsHost(a.thisv, I.kComp)) {
        Component* c = scriptbind::Resolve(CompHandle(a.thisv));
        if (!c || !c->gameObject) rt.ThrowError(js::ErrorType::Error, "the component is null (its object was destroyed or the component was removed)");
        g = c->gameObject;
    } else g = I.ctx.self;
    if (!g) rt.ThrowError(js::ErrorType::Error, "This script isn't attached to an object");
    if (!a[0].IsStr()) rt.ThrowType("GetComponent needs a component name, e.g. GetComponent(\"Rigidbody\")");
    std::string name = U8(a[0].S());
    int idx = FindType(name);
    if (idx < 0 || !Api().types[(std::size_t)idx].isComponent) {
        Value out;
        if (BindGetComponent(rt, g, name, out)) return out;
        rt.ThrowType("'" + name + "' isn't a component type scripts can use");
    }
    const sc::ApiType& t = Api().types[(std::size_t)idx];
    if (t.find && t.find(g)) return MakeObj(rt, g->InstanceId(), idx);
    return Value::Null();
}

Value DestroyFn(js::Runtime& rt, js::CallArgs& a) {
    GameObject* g = ResolveObj(rt, a[0]);
    if (Scene* s = g->scene()) s->Destroy(g);
    return Value();
}

// ---- engine objects (host class kObj)
bool ObjGet(js::Runtime& rt, js::HostObj* h, js::String* key, Value& out) {
    const std::string k = U8(key);
    int typeIdx = h->i32;
    const auto& api = Api();
    if (typeIdx < 0 || typeIdx >= (int)api.types.size()) return false;
    const sc::ApiType& t = api.types[(std::size_t)typeIdx];
    Value self = Value::Obj(h);
    for (const sc::ApiProperty& p : t.props) {
        if (p.name != k) continue;
        Prepare(rt);
        sc::Value r;
        try {
            r = p.get(SelfValue(rt, self), Ctx(rt).ctx);
        } catch (const std::exception& e) {
            EngineError(rt, e);
        }
        out = ToJs(rt, r);
        return true;
    }
    if (HasMethod(t, t.methods, k)) { out = BoundMethod(rt, typeIdx, false, k); return true; }
    if (k == "GetComponent") { out = Value::Obj(Ctx(rt).getComponentFn); return true; }
    if (k == "gameObject" || k == "transform") {
        GameObject* g = ResolveObj(rt, self);
        out = MakeObj(rt, g->InstanceId(), k == "gameObject" ? api.gameObject : api.transform);
        return true;
    }
    if (k == "name" && typeIdx != api.gameObject) {
        out = Value::Str(S(rt, ResolveObj(rt, self)->name));
        return true;
    }
    if (t.isComponent && BindIndex(rt, self, k, out)) return true;
    return false;
}

bool ObjSet(js::Runtime& rt, js::HostObj* h, js::String* key, const Value& val) {
    const std::string k = U8(key);
    const sc::ApiType& t = Api().types[(std::size_t)h->i32];
    Value self = Value::Obj(h);
    for (const sc::ApiProperty& p : t.props) {
        if (p.name != k) continue;
        if (!p.set) rt.ThrowType(t.name + "." + k + " is read-only");
        sc::Value v;
        std::string why;
        if (!FromJs(rt, val, p.type, v, why)) rt.ThrowType("can't set " + t.name + "." + k + ": " + why);
        Prepare(rt);
        try {
            p.set(SelfValue(rt, self), v, Ctx(rt).ctx);
        } catch (const std::exception& e) {
            EngineError(rt, e);
        }
        return true;
    }
    if (t.isComponent && BindNewIndex(rt, self, k, val)) return true;
    rt.ThrowType(t.name + " has no settable property '" + k + "'");
}

std::string ObjText(js::Runtime& rt, js::HostObj* h) {
    GameObject* g = GameObject::FromInstanceId(h->id);
    std::string tn = JsTypeWord(rt, Value::Obj(h));
    return tn + "(" + (g ? g->name : std::string("destroyed")) + ")";
}

// ---- Vector3 (host class kVec)
bool VecGet(js::Runtime& rt, js::HostObj* h, js::String* key, Value& out) {
    const std::u16string& k16 = key->s;
    if (k16.size() == 1 && k16[0] >= u'x' && k16[0] <= u'z') { out = Value::Num(h->v[k16[0] - u'x']); return true; }
    const std::string k = U8(key);
    const sc::ApiType& t = Api().types[(std::size_t)Api().vector3];
    for (const sc::ApiProperty& p : t.props) {
        if (p.name != k) continue;
        sc::Value r;
        try {
            r = p.get(SelfValue(rt, Value::Obj(h)), Ctx(rt).ctx);
        } catch (const std::exception& e) {
            EngineError(rt, e);
        }
        out = ToJs(rt, r);
        return true;
    }
    if (k == "sqrMagnitude") {
        out = Value::Num(h->v[0] * h->v[0] + h->v[1] * h->v[1] + h->v[2] * h->v[2]);
        return true;
    }
    if (HasMethod(t, t.methods, k)) { out = BoundMethod(rt, Api().vector3, false, k); return true; }
    return false;   // the Vector3 prototype: add, sub, scale, ...
}
bool VecSet(js::Runtime& rt, js::HostObj* h, js::String* key, const Value& v) {
    const std::u16string& k = key->s;
    if (k.size() == 1 && k[0] >= u'x' && k[0] <= u'z') {
        if (!v.IsNum()) rt.ThrowType("Vector3." + U8(key) + " must be a number");
        h->v[k[0] - u'x'] = v.n;
        return true;
    }
    rt.ThrowType("Vector3 has only x, y and z to set");
}
std::string VecText(js::Runtime&, js::HostObj* h) {
    auto f = [](double d) { return js::Runtime::NumberToStdString((double)(float)d); };
    return "(" + f(h->v[0]) + ", " + f(h->v[1]) + ", " + f(h->v[2]) + ")";
}
std::vector<std::string> VecKeys(js::Runtime&, js::HostObj*) { return {"x", "y", "z"}; }

Vec3 VecArg(js::Runtime& rt, const Value& v, const char* what) {
    if (!IsHost(v, Ctx(rt).kVec)) rt.ThrowType(std::string("Vector3 expected for ") + what + ", got " + JsTypeWord(rt, v));
    return VecOf(v);
}
Value VecCtor(js::Runtime& rt, js::CallArgs& a) {
    double c[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        Value v = a[i];
        if (v.IsUndef()) continue;
        if (!v.IsNum()) rt.ThrowType("Vector3 takes numbers (x, y, z)");
        c[i] = v.n;
    }
    return MakeVec(rt, Vec3{(float)c[0], (float)c[1], (float)c[2]});
}

// ---- component handles of the generated layer (host class kComp)
bool CompGet(js::Runtime& rt, js::HostObj* h, js::String* key, Value& out) {
    const std::string k = U8(key);
    Value self = Value::Obj(h);
    if (k == "GetComponent") { out = Value::Obj(Ctx(rt).getComponentFn); return true; }
    if (k == "transform" || k == "name") {
        Component* c = scriptbind::Resolve(CompHandle(self));
        if (!c || !c->gameObject) rt.ThrowError(js::ErrorType::Error, "the component is null (its object was destroyed or the component was removed)");
        out = k == "name" ? Value::Str(S(rt, c->gameObject->name)) : MakeObj(rt, c->gameObject->InstanceId(), Api().transform);
        return true;
    }
    return BindIndex(rt, self, k, out);
}
bool CompSet(js::Runtime& rt, js::HostObj* h, js::String* key, const Value& val) {
    const std::string k = U8(key);
    if (BindNewIndex(rt, Value::Obj(h), k, val)) return true;
    const scriptbind::ClassBinding* cls = scriptbind::ClassByKey((std::uint32_t)h->i32);
    rt.ThrowType((cls ? cls->name : std::string("component")) + " has no settable property '" + k + "'");
}
std::string CompText(js::Runtime&, js::HostObj* h) {
    return vs::VsComponentName(vs::VsValue::ComponentHandle(h->id, (std::uint32_t)h->i32, h->u32).AsObjectRef());
}

// ---- static engine types (Input, Time, Debug, Mathf, GameObject, Vector3, ...): slots {type index, name}
Value StaticGet(js::Runtime& rt, js::CallArgs& a) {
    const sc::ApiType& t = Api().types[(std::size_t)a.callee->slots[0].n];
    const std::string name = U8(a.callee->slots[1].S());
    for (const sc::ApiProperty& p : t.staticProps) {
        if (p.name != name) continue;
        Prepare(rt);
        sc::Value r;
        try {
            r = p.get(sc::Value{}, Ctx(rt).ctx);
        } catch (const std::exception& e) {
            EngineError(rt, e);
        }
        return ToJs(rt, r);
    }
    return Value();
}
Value StaticSet(js::Runtime& rt, js::CallArgs& a) {
    const sc::ApiType& t = Api().types[(std::size_t)a.callee->slots[0].n];
    const std::string name = U8(a.callee->slots[1].S());
    for (const sc::ApiProperty& p : t.staticProps) {
        if (p.name != name) continue;
        sc::Value v;
        std::string why;
        if (!FromJs(rt, a[0], p.type, v, why)) rt.ThrowType("can't set " + t.name + "." + name + ": " + why);
        Prepare(rt);
        try {
            p.set(sc::Value{}, v, Ctx(rt).ctx);
        } catch (const std::exception& e) {
            EngineError(rt, e);
        }
    }
    return Value();
}

void InstallBindings(Impl& I, bool quiet) {
    js::Runtime& rt = I.rt;
    const auto& api = Api();
    if (!quiet) rt.printSink = [](const std::string& s) { Log::Info(s); };
    I.kObj = rt.AddHostClass({"engine object", ObjGet, ObjSet, ObjText, nullptr});
    I.kVec = rt.AddHostClass({"Vector3", VecGet, VecSet, VecText, VecKeys});
    I.kComp = rt.AddHostClass({"engine component", CompGet, CompSet, CompText, nullptr});
    Impl* ip = &I;
    rt.onHostFree = [ip](js::HostObj* h) {
        if (h->hostClass == ip->kObj) {
            std::uint64_t key = (h->id << 12) ^ (std::uint64_t)(h->i32 & 0xFFF);
            auto it = ip->objCache.find(key);
            if (it != ip->objCache.end() && it->second == h) ip->objCache.erase(it);
        } else if (h->hostClass == ip->kComp) {
            std::string key = std::to_string(h->id) + ":" + std::to_string(h->i32) + ":" + std::to_string(h->u32);
            auto it = ip->compCache.find(key);
            if (it != ip->compCache.end() && it->second == h) ip->compCache.erase(it);
        }
    };
    I.objProto = rt.NewObject();
    rt.Ref(Value::Obj(I.objProto));
    js::DefNative(rt, I.objProto, "toString", [](js::Runtime& rt, js::CallArgs& a) {
        if (!IsHost(a.thisv, Ctx(rt).kObj)) rt.ThrowType("toString needs an engine object");
        return Value::Str(S(rt, ObjText(rt, H(a.thisv))));
    }, 0);
    I.compProto = rt.NewObject();
    rt.Ref(Value::Obj(I.compProto));
    js::DefNative(rt, I.compProto, "toString", [](js::Runtime& rt, js::CallArgs& a) {
        if (!IsHost(a.thisv, Ctx(rt).kComp)) rt.ThrowType("toString needs a component");
        return Value::Str(S(rt, CompText(rt, H(a.thisv))));
    }, 0);
    // Vector3: a constructor with the static members, its prototype holds the JavaScript helpers
    I.vecProto = rt.NewObject();
    rt.Ref(Value::Obj(I.vecProto));
    js::DefNative(rt, I.vecProto, "toString", [](js::Runtime& rt, js::CallArgs& a) { return Value::Str(S(rt, VecText(rt, H(Value::Obj(rt.ToObject(a.thisv)))))); }, 0);
    js::DefNative(rt, I.vecProto, "add", [](js::Runtime& rt, js::CallArgs& a) { return MakeVec(rt, VecArg(rt, a.thisv, "add") + VecArg(rt, a[0], "add")); }, 1);
    js::DefNative(rt, I.vecProto, "sub", [](js::Runtime& rt, js::CallArgs& a) { return MakeVec(rt, VecArg(rt, a.thisv, "sub") - VecArg(rt, a[0], "sub")); }, 1);
    js::DefNative(rt, I.vecProto, "scale", [](js::Runtime& rt, js::CallArgs& a) {
        Vec3 v = VecArg(rt, a.thisv, "scale");
        if (a[0].IsNum()) return MakeVec(rt, v * (float)a[0].n);
        Vec3 w = VecArg(rt, a[0], "scale");
        return MakeVec(rt, Vec3{v.x * w.x, v.y * w.y, v.z * w.z});
    }, 1);
    js::DefNative(rt, I.vecProto, "div", [](js::Runtime& rt, js::CallArgs& a) {
        Vec3 v = VecArg(rt, a.thisv, "div");
        if (!a[0].IsNum()) rt.ThrowType("Vector3.div needs a number");
        float d = (float)a[0].n;
        return MakeVec(rt, Vec3{v.x / d, v.y / d, v.z / d});
    }, 1);
    js::DefNative(rt, I.vecProto, "negate", [](js::Runtime& rt, js::CallArgs& a) { return MakeVec(rt, VecArg(rt, a.thisv, "negate") * -1.0f); }, 0);
    js::DefNative(rt, I.vecProto, "clone", [](js::Runtime& rt, js::CallArgs& a) { return MakeVec(rt, VecArg(rt, a.thisv, "clone")); }, 0);
    js::DefNative(rt, I.vecProto, "equals", [](js::Runtime& rt, js::CallArgs& a) {
        Vec3 v = VecArg(rt, a.thisv, "equals");
        if (!IsHost(a[0], Ctx(rt).kVec)) return Value::Bool(false);
        Vec3 w = VecOf(a[0]);
        return Value::Bool(v.x == w.x && v.y == w.y && v.z == w.z);
    }, 1);
    js::DefNative(rt, I.vecProto, "set", [](js::Runtime& rt, js::CallArgs& a) {
        VecArg(rt, a.thisv, "set");
        for (int i = 0; i < 3; ++i) {
            if (!a[i].IsNum()) rt.ThrowType("Vector3.set takes numbers (x, y, z)");
            H(a.thisv)->v[i] = a[i].n;
        }
        return a.thisv;
    }, 3);
    I.getComponentFn = rt.NewNative(GetComponentFn, "GetComponent", 1);
    rt.Ref(Value::Obj(I.getComponentFn));
    // one global per engine type with static members
    for (std::size_t i = 0; i < api.types.size(); ++i) {
        const sc::ApiType& t = api.types[i];
        if (Hidden(t)) continue;
        bool isVec = (int)i == api.vector3;
        if (t.staticProps.empty() && t.staticMethods.empty() && !isVec) continue;
        js::Object* holder;
        if (isVec) {
            js::NativeFunc* c = rt.NewNative(VecCtor, "Vector3", 3);
            c->ctor = true;
            rt.MakeLazyProps(c);
            c->AddRaw(rt.atoms->prototype, Value::Obj(I.vecProto), 0);
            I.vecProto->AddRaw(rt.atoms->constructor, Value::Obj(c), js::kHidden);
            holder = c;
        } else holder = rt.NewObject();
        for (const sc::ApiProperty& p : t.staticProps) {
            js::String* nm = rt.Intern(js::Utf8To16(p.name));
            if (holder->FindRaw(nm)) continue;
            js::NativeFunc* g = rt.NewNative(StaticGet, ("get " + p.name).c_str(), 0, {Value::Num((double)i), Value::Str(S(rt, p.name))});
            js::NativeFunc* s = p.set ? rt.NewNative(StaticSet, ("set " + p.name).c_str(), 1, {Value::Num((double)i), Value::Str(S(rt, p.name))}) : nullptr;
            rt.DefineAccessor(holder, nm, g, s, js::kEnumerable | js::kConfigurable);
        }
        for (const sc::ApiMethod& m : t.staticMethods) {
            if (glue::IsDenied(t, m)) continue;
            js::String* nm = rt.Intern(js::Utf8To16(m.name));
            if (holder->FindRaw(nm)) continue;
            holder->AddRaw(nm, BoundMethod(rt, (int)i, true, m.name), js::kHidden);
        }
        rt.SetGlobal(t.name.c_str(), Value::Obj(holder));
    }
    rt.SetGlobal("GetComponent", Value::Obj(I.getComponentFn));
    js::DefNative(rt, rt.global, "Destroy", DestroyFn, 1);
    for (js::GcObj* k : rt.OwnKeys(rt.global, true, false)) I.builtinGlobals.insert(U8(static_cast<js::String*>(k)));
}

} // namespace

// ============================================================ the IScriptVM
JavaScriptVM::JavaScriptVM() : m_impl(std::make_unique<Impl>()) { InstallBindings(*m_impl, false); }
JavaScriptVM::~JavaScriptVM() = default;

void JavaScriptVM::SetMemoryLimit(std::size_t bytes) {
    m_memoryLimit = bytes;
    m_impl->rt.memoryLimit = bytes;
}

bool JavaScriptVM::Validate(const std::string& source, std::string* error) {
    js::Runtime tmp;
    auto errs = tmp.CheckSyntax(source, "script");
    if (errs.empty()) return true;
    if (error) *error = "line " + std::to_string(errs[0].line) + ": " + errs[0].message;
    return false;
}

std::vector<ScriptDiagnostic> JavaScriptVM::ValidateAll(const std::string& source) {
    js::Runtime tmp;
    std::vector<ScriptDiagnostic> out;
    for (const js::SyntaxError& e : tmp.CheckSyntax(source, "script")) out.push_back(ScriptDiagnostic{e.line, e.message, 0, e.column});
    std::vector<ScriptDiagnostic> warn = Lint(source);
    out.insert(out.end(), warn.begin(), warn.end());
    return out;
}

bool JavaScriptVM::Load(const std::string& source, std::string* error) {
    m_impl = std::make_unique<Impl>();   // a reload starts clean
    InstallBindings(*m_impl, m_quiet);
    js::Runtime& rt = m_impl->rt;
    rt.memoryLimit = m_memoryLimit;
    if (m_host && m_host->gameObject) {
        rt.SetGlobal("gameObject", MakeObj(rt, m_host->gameObject->InstanceId(), Api().gameObject));
        rt.SetGlobal("transform", MakeObj(rt, m_host->gameObject->InstanceId(), Api().transform));
    }
    js::Value fn;
    js::SyntaxError se;
    if (!rt.Compile(source, "script", fn, &se)) {
        if (error) *error = "line " + std::to_string(se.line) + ": " + se.message;
        return false;
    }
    rt.SetBudget(m_budget);
    std::string err;
    js::Value exported;
    if (!rt.CallHost(fn, Value(), {}, &exported, &err)) {
        if (error) *error = err;
        return false;
    }
    m_impl->style = 0;
    if (exported.IsObj()) {
        if (rt.IsConstructor(exported) && exported.O()->cls == js::Cls::Closure &&
            (static_cast<js::Closure*>(exported.O())->code->flags & js::PF_ClassCtor)) {
            int keep = rt.Ref(exported);
            js::Value inst;
            rt.SetBudget(m_budget);
            bool ok = rt.ConstructHost(exported, {}, &inst, &err);
            rt.Unref(keep);
            if (!ok) {
                if (error) *error = err;
                return false;
            }
            m_impl->style = 2;
            m_impl->scriptRef = rt.Ref(inst);
        } else if (!exported.O()->IsCallable()) {
            m_impl->style = 1;
            m_impl->scriptRef = rt.Ref(exported);
        }
    }
    m_impl->loaded = true;
    if (m_host) Bind(m_host);
    return true;
}

void JavaScriptVM::Bind(ScriptHost* host) {
    m_host = host;
    if (!m_impl->loaded || !host) return;
    js::Runtime& rt = m_impl->rt;
    m_impl->ctx.self = host->gameObject;
    m_impl->ctx.component = host->component;
    try {
        BindObjects(rt, host);
    } catch (const js::JsTerminate&) {
        // the heap is full; the next callback reports it
    }
}

void JavaScriptVM::BindObjects(js::Runtime& rt, ScriptHost* host) {
    m_impl->ctx.component = host->component;
    Value go = host->gameObject ? MakeObj(rt, host->gameObject->InstanceId(), Api().gameObject) : Value::Null();
    Value tr = host->gameObject ? MakeObj(rt, host->gameObject->InstanceId(), Api().transform) : Value::Null();
    rt.SetGlobal("gameObject", go);
    rt.SetGlobal("transform", tr);
    if (m_impl->style != 0) {
        js::Object* o = rt.GetRef(m_impl->scriptRef).O();
        try {
            rt.DefineData(o, rt.Intern("gameObject"), go, js::kHidden);
            rt.DefineData(o, rt.Intern("transform"), tr, js::kHidden);
        } catch (const js::JsThrow&) {
            rt.exception = Value();   // a frozen script object keeps its own
        } catch (const js::JsTerminate&) {
        }
    }
}

bool JavaScriptVM::Run(const std::string& engineName, const std::vector<vs::VsValue>& args, vs::VsValue* ret,
                       std::string* error) {
    if (!m_impl->loaded) return false;
    js::Runtime& rt = m_impl->rt;
    Value holder = m_impl->style != 0 ? rt.GetRef(m_impl->scriptRef) : Value::Obj(rt.global);
    Value fn;
    std::string used;
    for (const std::string& n : glue::HandlerNamesFor(engineName)) {
        Value f;
        js::String* key = rt.Intern(js::Utf8To16(n));
        if (m_impl->style == 0) {
            if (js::Prop* p = rt.lexicals->FindRaw(key)) f = p->v;
            else if (js::Prop* g = rt.global->FindRaw(key)) f = (g->flags & js::kAccessor) ? Value() : g->v;
        } else {
            // the script object and its prototype chain (methods of the exported class)
            for (js::Object* o = holder.O(); o && f.IsUndef(); o = o->proto) {
                js::Prop* p = o->FindRaw(key);
                if (p && !(p->flags & js::kAccessor)) f = p->v;
            }
        }
        if (rt.IsCallable(f)) {
            fn = f;
            used = n;
            break;
        }
    }
    if (fn.IsUndef()) return false;
    if (m_host) {
        m_impl->ctx.self = m_host->gameObject;
        m_impl->ctx.component = m_host->component;
        m_impl->ctx.deltaTime = m_host->deltaTime;
    }
    std::vector<Value> jsArgs;
    try {
        for (const auto& a : args) jsArgs.push_back(FromVs(rt, a));
    } catch (const js::JsTerminate& t) {   // the heap is full: report it like any other stop
        if (error) *error = used + "(): " + t.why;
        else glue::ReportHandlerError(m_impl->ctx.self, used + "(): " + t.why);
        return true;
    }
    if (m_impl->running == 0) rt.SetBudget(m_budget);
    ++m_impl->running;
    std::string err;
    Value r;
    bool ok = rt.CallHost(fn, m_impl->style != 0 ? holder : Value(), jsArgs, &r, &err);
    --m_impl->running;
    if (ret) *ret = ok ? ToVs(rt, r) : vs::VsValue{};
    if (!ok) {
        std::string msg = used + "(): " + err;
        if (error) {
            *error = msg;
            return true;
        }
        ++m_errorCount;
        m_lastError = err;
        glue::ReportHandlerError(m_impl->ctx.self, msg);
    }
    return true;
}

void JavaScriptVM::CallStart() {
    Run("Awake", {}, nullptr, nullptr);
    Run("OnEnable", {}, nullptr, nullptr);
    Run("Start", {}, nullptr, nullptr);
}

void JavaScriptVM::CallUpdate(float deltaTime) {
    if (m_host) m_host->deltaTime = deltaTime;
    m_impl->ctx.deltaTime = deltaTime;
    Run("Update", {vs::VsValue{deltaTime}}, nullptr, nullptr);
}

void JavaScriptVM::CallEvent(const std::string& function) {
    std::vector<vs::VsValue> args;
    if (function == "on_fixed_update" && m_host) args.push_back(vs::VsValue{m_host->deltaTime});
    Run(function, args, nullptr, nullptr);
}

void JavaScriptVM::CallEventWith(const std::string& function, const vs::VsValue& arg) { Run(function, {arg}, nullptr, nullptr); }

vs::VsValue JavaScriptVM::CallEventArgs(const std::string& function, const std::vector<vs::VsValue>& args, std::string* error) {
    vs::VsValue r;
    Run(function, args, &r, error);
    return r;
}

bool JavaScriptVM::HasEvent(const std::string& function) const {
    if (!m_impl->loaded) return false;
    js::Runtime& rt = m_impl->rt;
    for (const std::string& n : glue::HandlerNamesFor(function)) {
        js::String* key = rt.Intern(js::Utf8To16(n));
        Value f;
        if (m_impl->style == 0) {
            if (js::Prop* p = rt.lexicals->FindRaw(key)) f = p->v;
            else if (js::Prop* g = rt.global->FindRaw(key)) f = g->v;
        } else
            for (js::Object* o = rt.GetRef(m_impl->scriptRef).O(); o && f.IsUndef(); o = o->proto)
                if (js::Prop* p = o->FindRaw(key)) f = (p->flags & js::kAccessor) ? Value() : p->v;
        if (rt.IsCallable(f)) return true;
    }
    return false;
}

vs::VsValue JavaScriptVM::GetGlobal(const std::string& name) const {
    if (!m_impl->loaded) return {};
    js::Runtime& rt = m_impl->rt;
    js::String* key = rt.Intern(js::Utf8To16(name));
    if (m_impl->style != 0) {
        js::Object* o = rt.GetRef(m_impl->scriptRef).O();
        if (js::Prop* p = o->FindRaw(key))
            if (!(p->flags & js::kAccessor)) return ToVs(rt, p->v);
    }
    if (js::Prop* p = rt.lexicals->FindRaw(key)) return p->v.IsEmpty() ? vs::VsValue{} : ToVs(rt, p->v);
    if (js::Prop* p = rt.global->FindRaw(key)) return (p->flags & js::kAccessor) ? vs::VsValue{} : ToVs(rt, p->v);
    return {};
}

void JavaScriptVM::SetGlobal(const std::string& name, const vs::VsValue& v) {
    if (!m_impl->loaded) return;
    js::Runtime& rt = m_impl->rt;
    js::String* key;
    Value nv;
    try {
        key = rt.Intern(js::Utf8To16(name));
        nv = FromVs(rt, v);
    } catch (const js::JsTerminate&) {
        return;   // the heap is full; the next callback reports it
    }
    if (m_impl->style != 0) {
        js::Object* o = rt.GetRef(m_impl->scriptRef).O();
        js::Prop* p = o->FindRaw(key);
        if (p && !(p->flags & js::kAccessor) && (p->flags & js::kWritable) && !rt.IsCallable(p->v)) {
            p->v = nv;   // one of the script's settings
            return;
        }
    }
    if (js::Prop* p = rt.lexicals->FindRaw(key)) {
        if (p->flags & js::kWritable) p->v = nv;
        return;
    }
    if (js::Prop* p = rt.global->FindRaw(key)) {
        if (!(p->flags & js::kAccessor) && (p->flags & js::kWritable)) p->v = nv;
        return;
    }
    rt.global->AddRaw(key, nv, js::kDefault);
}

std::vector<std::string> JavaScriptVM::GlobalNames() const {
    std::vector<std::string> out;
    if (!m_impl->loaded) return out;
    js::Runtime& rt = m_impl->rt;
    auto take = [&](js::Object* o, bool skipBuiltins) {
        for (const js::Prop& p : o->props) {
            if (p.key->kind != js::Kind::String || (p.flags & js::kAccessor)) continue;
            if (!(p.flags & js::kEnumerable) && o != rt.lexicals) continue;
            if (!IsSetting(rt, p.v)) continue;
            std::string n = U8(static_cast<js::String*>(p.key));
            if (glue::IsEngineSetName(n)) continue;
            if (skipBuiltins && m_impl->builtinGlobals.count(n)) continue;
            out.push_back(n);
        }
    };
    if (m_impl->style != 0) take(rt.GetRef(m_impl->scriptRef).O(), false);
    else {
        take(rt.lexicals, false);
        take(rt.global, true);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// ============================================================ editor support
namespace {

const char* const kLibMembers[][2] = {
    {"Math", "abs acos acosh asin asinh atan atan2 atanh cbrt ceil clz32 cos cosh E exp expm1 floor fround hypot imul LN10 LN2 log log10 log1p log2 LOG10E LOG2E max min PI pow random round sign sin sinh sqrt SQRT1_2 SQRT2 tan tanh trunc"},
    {"JSON", "parse stringify"},
    {"console", "log info warn error debug"},
    {"Object", "assign create defineProperties defineProperty entries freeze fromEntries getOwnPropertyDescriptor groupBy getOwnPropertyDescriptors getOwnPropertyNames getOwnPropertySymbols getPrototypeOf hasOwn is isExtensible isFrozen isSealed keys preventExtensions seal setPrototypeOf values"},
    {"Array", "from isArray of"},
    {"Number", "EPSILON isFinite isInteger isNaN isSafeInteger MAX_SAFE_INTEGER MAX_VALUE MIN_SAFE_INTEGER MIN_VALUE NaN NEGATIVE_INFINITY parseFloat parseInt POSITIVE_INFINITY"},
    {"String", "fromCharCode fromCodePoint raw"},
    {"Promise", "all allSettled any race reject resolve withResolvers"},
    {"Symbol", "for keyFor iterator asyncIterator toPrimitive toStringTag hasInstance"},
    {"RegExp", "prototype"},
    {"Reflect", "apply construct defineProperty deleteProperty get getOwnPropertyDescriptor getPrototypeOf has isExtensible ownKeys preventExtensions set setPrototypeOf"},
    {"Date", "now parse UTC"},
    {"BigInt", "asIntN asUintN"},
    {"Intl", "Collator DateTimeFormat ListFormat NumberFormat PluralRules RelativeTimeFormat getCanonicalLocales"},
};
const char* const kGlobals[] = {"console", "Math", "JSON", "Object", "Array", "String", "Number", "Boolean", "Symbol", "RegExp",
                                "Map", "Set", "Promise", "Error", "TypeError", "RangeError", "ReferenceError",
                                "SyntaxError", "Reflect", "parseInt", "parseFloat", "isNaN", "isFinite", "globalThis",
                                "queueMicrotask", "Date", "BigInt", "Intl", "WeakMap", "WeakSet", "WeakRef",
                                "FinalizationRegistry", "encodeURIComponent", "decodeURIComponent", "encodeURI", "decodeURI",
                                "GetComponent", "Destroy", "gameObject", "transform", "this",
                                "undefined", "null", "NaN", "Infinity", "console.log"};

// The space-separated names of a library row.
std::vector<std::string> Names(const char* list) {
    std::istringstream in(list);
    return {std::istream_iterator<std::string>(in), std::istream_iterator<std::string>()};
}

bool IsJsKeyword(const std::string& w) {
    for (int i = 0; i < js::kKeywordCount; ++i)
        if (w == js::kKeywords[i]) return true;
    return false;
}

struct LintTok {
    std::string text;
    int line, col;
};

// Names and the punctuation the lint looks at, from our lexer (so strings, templates and comments are skipped
// exactly as the engine reads them). A source that does not tokenize gives none: its syntax error is reported.
std::vector<LintTok> LintTokens(const std::string& s) {
    std::vector<js::Token> toks;
    js::SyntaxError err;
    std::vector<LintTok> out;
    if (!js::Tokenize(s, toks, err)) return out;
    for (const js::Token& t : toks) {
        if (t.t == js::TT::Name) out.push_back(LintTok{js::Utf16To8(t.s), t.line, t.col});
        else if (t.t == js::TT::Punct && (t.p == js::P_Dot || t.p == js::P_LParen || t.p == js::P_RParen || t.p == js::P_LBrace))
            out.push_back(LintTok{t.p == js::P_Dot ? "." : t.p == js::P_LParen ? "(" : t.p == js::P_RParen ? ")" : "{", t.line, t.col});
        else if (t.t != js::TT::End) out.push_back(LintTok{"", t.line, t.col});
    }
    return out;
}

} // namespace

std::vector<std::string> JavaScriptVM::CompletionNames() {
    std::set<std::string> names;
    for (int i = 0; i < js::kKeywordCount; ++i) names.insert(js::kKeywords[i]);
    for (const char* g : kGlobals) names.insert(g);
    for (const auto& lib : kLibMembers)
        for (const std::string& w : Names(lib[1])) names.insert(std::string(lib[0]) + "." + w);
    glue::AddApiNames(names, Hidden);
    return {names.begin(), names.end()};
}

std::vector<std::string> JavaScriptVM::BuiltinNames() const { return CompletionNames(); }

std::string JavaScriptVM::CompletionDoc(const std::string& name) {
    if (IsJsKeyword(name)) return "JavaScript keyword";
    for (const auto& lib : kLibMembers)
        if (name == lib[0]) return std::string("JavaScript library ") + lib[0] + ": " + lib[1];
    std::string h = glue::HandlerDoc(name);
    if (!h.empty()) return h;
    if (name == "GetComponent") return "GetComponent(\"Rigidbody\") or obj.GetComponent(\"Rigidbody\"): a component of the object, or null";
    if (name == "Destroy") return "Destroy(obj): removes an object from the scene";
    if (name == "console.log") return "console.log(...values): writes a line to the Console";
    return glue::ApiDoc(name, ".", Hidden);
}

std::vector<std::string> JavaScriptVM::CompletionMembers(const std::string& receiver) {
    std::set<std::string> out;
    for (const auto& lib : kLibMembers)
        if (receiver == lib[0])
            for (const std::string& w : Names(lib[1])) out.insert(w);
    if (out.empty()) {
        if (receiver == "this") {
            out.insert("gameObject");
            out.insert("transform");
        } else if (glue::ApiReceiverMembers(receiver, Hidden, out) == 3) {
            for (const char* n : {"x", "y", "z", "magnitude", "normalized", "sqrMagnitude", "add", "sub", "scale", "div",
                                  "negate", "equals", "clone", "set"})
                out.insert(n);
        }
    }
    return {out.begin(), out.end()};
}

std::vector<ScriptDiagnostic> JavaScriptVM::Lint(const std::string& source) {
    std::vector<ScriptDiagnostic> out;
    std::vector<LintTok> t = LintTokens(source);
    static const std::vector<std::string> kExtra = {"prototype", "name", "length"};
    // Type.member on an engine type with static members
    for (std::size_t i = 0; i + 2 < t.size(); ++i) {
        if (t[i + 1].text != "." || (i > 0 && t[i - 1].text == ".")) continue;
        std::string msg = glue::StaticMemberProblem(t[i].text, t[i + 2].text, Hidden, kExtra);
        if (!msg.empty()) out.push_back(ScriptDiagnostic{t[i + 2].line, msg, 1, t[i + 2].col});
    }
    // a function or method definition whose name is close to a handler's: `function Name(` or `Name(...) {`
    for (std::size_t i = 0; i + 1 < t.size(); ++i) {
        if (t[i + 1].text != "(" || !std::isalpha((unsigned char)t[i].text[0])) continue;
        bool def = i > 0 && t[i - 1].text == "function";
        if (!def && (i == 0 || t[i - 1].text != ".")) {
            int depth = 0;
            for (std::size_t k = i + 1; k < t.size(); ++k) {
                if (t[k].text == "(") ++depth;
                else if (t[k].text == ")" && --depth == 0) {
                    def = k + 1 < t.size() && t[k + 1].text == "{";
                    break;
                }
            }
        }
        if (!def) continue;
        std::string msg = glue::HandlerTypo(t[i].text);
        if (!msg.empty()) out.push_back(ScriptDiagnostic{t[i].line, msg, 1, t[i].col});
    }
    std::stable_sort(out.begin(), out.end(), [](const ScriptDiagnostic& a, const ScriptDiagnostic& b) { return a.line < b.line; });
    return out;
}

std::vector<JavaScriptVM::Setting> JavaScriptVM::Settings(const std::string& source) {
    std::vector<Setting> out;
    JavaScriptVM vm;
    vm.SetBudget(1'000'000);
    vm.SetMemoryLimit(32u << 20);
    vm.m_quiet = true;
    if (!vm.Load(source, nullptr)) return out;
    std::vector<std::pair<std::size_t, Setting>> found;
    auto num = [](double x) {   // the shortest text that reads back as the same float (settings are floats)
        char buf[32];
        for (int p = 1; p <= 9; ++p) {
            std::snprintf(buf, sizeof buf, "%.*g", p, x);
            if ((float)std::strtod(buf, nullptr) == (float)x) break;
        }
        return js::Runtime::NumberToStdString(std::strtod(buf, nullptr));
    };
    for (const std::string& n : vm.GlobalNames()) {
        Setting st;
        st.name = n;
        if (!glue::SettingText(vm.GetGlobal(n), st.type, st.defaultText, num)) continue;
        found.push_back({glue::FirstWordPos(source, n), st});
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& f : found) out.push_back(f.second);
    return out;
}

std::string JavaScriptVM::Format(const std::string& src) {
    // Every open bracket remembers the indent of the line it was opened on. A line inside it is
    // indented one step more; a line that starts with closers goes back to the indent of the line
    // that opened the last of them, so "foo(() => {" adds one step, not two. Brackets inside strings,
    // comments and template text do not count; lines that start inside a template literal or a
    // block comment are kept as written.
    std::string out;
    std::vector<int> opens;        // indent of the line each open bracket is on
    std::size_t i = 0;
    bool inBlockComment = false;
    std::vector<int> tplStack;     // template literals open: -1 in text, else the bracket count at ${
    while (i <= src.size()) {
        std::size_t e = src.find('\n', i);
        if (e == std::string::npos) e = src.size();
        std::string line = src.substr(i, e - i);
        bool verbatim = inBlockComment || (!tplStack.empty() && tplStack.back() == -1);
        // indent of this line: count the closers it starts with (outside strings and comments)
        int ind = opens.empty() ? 0 : opens.back() + 1;
        if (!verbatim) {
            std::size_t lead = 0;
            for (std::size_t k = 0; k < line.size(); ++k) {
                char c = line[k];
                if (c == ' ' || c == '\t' || c == '\r') continue;
                if (c == '}' || c == ']' || c == ')') { ++lead; continue; }
                break;
            }
            if (lead > 0) ind = lead <= opens.size() ? opens[opens.size() - lead] : 0;
        }
        for (std::size_t k = 0; k < line.size(); ++k) {
            char c = line[k];
            if (inBlockComment) {
                if (c == '*' && k + 1 < line.size() && line[k + 1] == '/') { inBlockComment = false; ++k; }
                continue;
            }
            if (!tplStack.empty() && tplStack.back() == -1) {   // inside template text
                if (c == '\\') { ++k; continue; }
                if (c == '`') { tplStack.pop_back(); continue; }
                if (c == '$' && k + 1 < line.size() && line[k + 1] == '{') {
                    tplStack.back() = (int)opens.size(); opens.push_back(ind); ++k;
                }
                continue;
            }
            if (c == '/' && k + 1 < line.size() && line[k + 1] == '/') break;
            if (c == '/' && k + 1 < line.size() && line[k + 1] == '*') { inBlockComment = true; ++k; continue; }
            if (c == '"' || c == '\'') {
                for (++k; k < line.size() && line[k] != c; ++k)
                    if (line[k] == '\\') ++k;
                continue;
            }
            if (c == '`') { tplStack.push_back(-1); continue; }
            if (c == '{' || c == '[' || c == '(') { opens.push_back(ind); continue; }
            if (c == '}' || c == ']' || c == ')') {
                if (!opens.empty()) opens.pop_back();
                if (!tplStack.empty() && c == '}' && tplStack.back() == (int)opens.size()) tplStack.back() = -1;
                continue;
            }
        }
        if (i) out += '\n';
        std::size_t a = line.find_first_not_of(" \t");
        if (verbatim) out += line;
        else if (a != std::string::npos) {
            std::size_t b = line.find_last_not_of(" \t\r");
            out += std::string((std::size_t)std::max(0, ind) * 4, ' ') + line.substr(a, b - a + 1);
        }
        if (e >= src.size()) break;
        i = e + 1;
    }
    return out;
}

} // namespace okay
