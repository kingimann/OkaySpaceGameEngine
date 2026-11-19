// The engine binding for our own Lua: GameObject, Transform, components (through the property
// registry), Input, Time, Log, Physics queries, vectors. Written for this engine (no code from other projects).
#include "okay/Lua/LuaScriptVM.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics2D.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Core/ScriptPaths.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "LuaCore.hpp"
#include <cmath>
#include <set>

namespace okay {
using namespace lua;

namespace {

const char* const kPrelude = R"LUA(
Vector3 = {}
local V = Vector3
function V.new(x, y, z) return setmetatable({x = x or 0, y = y or 0, z = z or 0}, V) end
setmetatable(V, {__call = function(_, x, y, z) return V.new(x, y, z) end})
V.__add = function(a, b) return V.new(a.x + b.x, a.y + b.y, a.z + b.z) end
V.__sub = function(a, b) return V.new(a.x - b.x, a.y - b.y, a.z - b.z) end
V.__unm = function(a) return V.new(-a.x, -a.y, -a.z) end
V.__mul = function(a, b)
  if type(a) == "number" then return V.new(a * b.x, a * b.y, a * b.z) end
  if type(b) == "number" then return V.new(a.x * b, a.y * b, a.z * b) end
  return V.new(a.x * b.x, a.y * b.y, a.z * b.z)
end
V.__div = function(a, b) return V.new(a.x / b, a.y / b, a.z / b) end
V.__eq = function(a, b) return a.x == b.x and a.y == b.y and a.z == b.z end
V.__tostring = function(a) return "(" .. a.x .. ", " .. a.y .. ", " .. a.z .. ")" end
V.__index = function(t, k)
  if k == "magnitude" then return math.sqrt(t.x * t.x + t.y * t.y + t.z * t.z) end
  if k == "sqrMagnitude" then return t.x * t.x + t.y * t.y + t.z * t.z end
  if k == "normalized" then
    local m = math.sqrt(t.x * t.x + t.y * t.y + t.z * t.z)
    if m > 1e-9 then return V.new(t.x / m, t.y / m, t.z / m) end
    return V.new(0, 0, 0)
  end
  return V[k]
end
function V.Dot(a, b) return a.x * b.x + a.y * b.y + a.z * b.z end
function V.Cross(a, b) return V.new(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x) end
function V.Distance(a, b) return (a - b).magnitude end
function V.Lerp(a, b, t) return a + (b - a) * t end
function V.Zero() return V.new(0, 0, 0) end
function V.One() return V.new(1, 1, 1) end
function V.Up() return V.new(0, 1, 0) end
function V.Right() return V.new(1, 0, 0) end
function V.Forward() return V.new(0, 0, 1) end

Mathf = {PI = math.pi, Deg2Rad = math.pi / 180, Rad2Deg = 180 / math.pi, Infinity = math.huge}
function Mathf.Clamp(x, lo, hi) if x < lo then return lo elseif x > hi then return hi end return x end
function Mathf.Clamp01(x) return Mathf.Clamp(x, 0, 1) end
function Mathf.Lerp(a, b, t) return a + (b - a) * Mathf.Clamp01(t) end
function Mathf.Sign(x) if x < 0 then return -1 end return 1 end
function Mathf.MoveTowards(cur, target, step)
  if math.abs(target - cur) <= step then return target end
  return cur + Mathf.Sign(target - cur) * step
end
function Mathf.Approximately(a, b) return math.abs(a - b) < 1e-6 end

-- Coroutine scheduler: StartCoroutine(fn, ...) runs fn as a coroutine; inside it, wait(seconds),
-- waitFrames(n) and waitUntil(fn) pause it. The engine ticks it before every Update.
local routines = {}
local tweenDt = 0
function StartCoroutine(fn, ...)
  local co = coroutine.create(fn)
  local r = {co = co, wake = 0, args = table.pack(...), first = true}
  routines[#routines + 1] = r
  return r
end
function StopCoroutine(r) if r then r.dead = true end end
function wait(seconds) coroutine.yield({seconds = seconds or 0}) end
function waitFrames(n) coroutine.yield({frames = n or 1}) end
function waitUntil(fn) coroutine.yield({untilFn = fn}) end
function __tick(dt)
  tweenDt = dt
  local i = 1
  while i <= #routines do
    local r = routines[i]
    local run = not r.dead
    if run then
      local w = r.waitFor
      if w then
        if w.seconds then w.seconds = w.seconds - dt; run = w.seconds <= 0
        elseif w.frames then w.frames = w.frames - 1; run = w.frames <= 0
        elseif w.untilFn then run = w.untilFn() and true or false end
      end
    end
    if run then
      local ok, res
      if r.first then r.first = false; ok, res = coroutine.resume(r.co, table.unpack(r.args, 1, r.args.n))
      else ok, res = coroutine.resume(r.co) end
      if not ok then r.dead = true; Log.error(tostring(res))
      elseif coroutine.status(r.co) == "dead" then r.dead = true
      else r.waitFor = type(res) == "table" and res or nil end
    end
    if r.dead then table.remove(routines, i) else i = i + 1 end
  end
end

-- Tweening: Tween.Value(from, to, seconds, onUpdate [, onDone [, ease]]), Tween.Move(transform, to, seconds [, ease]).
Tween = {}
Tween.Ease = {
  linear = function(t) return t end,
  inQuad = function(t) return t * t end,
  outQuad = function(t) return t * (2 - t) end,
  inOutQuad = function(t) if t < 0.5 then return 2 * t * t end return -1 + (4 - 2 * t) * t end,
  smooth = function(t) return t * t * (3 - 2 * t) end,
}
function Tween.Value(from, to, seconds, onUpdate, onDone, ease)
  local f = type(ease) == "function" and ease or Tween.Ease[ease or "linear"] or Tween.Ease.linear
  return StartCoroutine(function()
    local t = 0
    while t < seconds do
      onUpdate(from + (to - from) * f(t / seconds))
      wait(0)
      t = t + tweenDt
    end
    onUpdate(to)
    if onDone then onDone() end
  end)
end
function Tween.Move(tr, to, seconds, ease)
  local from = tr.position
  return Tween.Value(0, 1, seconds, function(k) tr.position = from + (to - from) * k end, nil, ease)
end
)LUA";

enum Kind { kGameObject = 1, kTransform = 2, kComponent = 3 };

const char* const kEngineSet[] = {"other", "contact_point", "contact_normal", "contact_x", "contact_y", "contact_z",
                                  "contact_nx", "contact_ny", "contact_nz", "impact_speed", "other_tag", "anim_event",
                                  "gameObject", "transform"};

bool IsEngineName(const std::string& n) {
    for (const char* e : kEngineSet) if (n == e) return true;
    return false;
}

std::string PascalOf(const std::string& snake) {
    std::string out; bool up = true;
    for (char c : snake) {
        if (c == '_') { up = true; continue; }
        out += up ? (char)std::toupper((unsigned char)c) : c; up = false;
    }
    return out;
}

}  // namespace

struct LuaScriptVM::Impl {
    std::unique_ptr<State> L;
    ScriptHost* host = nullptr;
    Table* vecMeta = nullptr;
    Table* goMeta = nullptr;
    Table* trMeta = nullptr;
    Table* compMeta = nullptr;
    Table* goMethods = nullptr;
    Table* trMethods = nullptr;
    std::set<std::string> baseline;
    std::int64_t instr = 5'000'000;
    double seconds = 0.5;
    std::size_t memory = 64u << 20;

    // ------------------------------------------------ value conversion
    Value MakeVec(float x, float y, float z) {
        Table* t = L->NewTable();
        t->Set(*L, L->NewStr("x"), Value::Num(x));
        t->Set(*L, L->NewStr("y"), Value::Num(y));
        t->Set(*L, L->NewStr("z"), Value::Num(z));
        t->meta = vecMeta;
        return Value::Of(t);
    }
    Value MakeVec(const Vec3& v) { return MakeVec(v.x, v.y, v.z); }
    bool GetNum(Table* t, const char* k, float& out) {
        Value v;
        if (!ToNumber(t->GetStr(k), v)) return false;
        out = (float)(v.t == T::Int ? (double)v.i : v.n);
        return true;
    }
    bool GetVec(const Value& v, Vec3& out) {
        if (v.t != T::Table) return false;
        auto* t = static_cast<Table*>(v.o);
        float x = 0, y = 0, z = 0;
        bool hx = GetNum(t, "x", x), hy = GetNum(t, "y", y);
        GetNum(t, "z", z);
        if (!hx || !hy) return false;
        out = Vec3{x, y, z};
        return true;
    }
    Vec3 NeedVec(const Value& v, const char* what) {
        Vec3 r;
        if (!GetVec(v, r)) L->Error(std::string(what) + " needs a vector, for example Vector3.new(1, 2, 3)");
        return r;
    }
    Value MakeObj(int kind, std::uint64_t id, const std::string& tag = "") {
        Table* m = kind == kGameObject ? goMeta : kind == kTransform ? trMeta : compMeta;
        User* u = L->NewUser(kind, id, m);
        u->tag = tag;
        return Value::Of(u);
    }
    Value ObjOrNil(GameObject* g) { return g ? MakeObj(kGameObject, g->InstanceId()) : Value(); }

    Value ToLua(const vs::VsValue& v, int depth = 0) {
        if (depth > 8) return Value();
        if (v.IsBool()) return Value::Bool(v.AsBool());
        if (v.IsNumber()) return Value::Num(v.AsFloat());
        if (v.IsString()) return L->NewStr(v.AsString());
        if (v.IsVec3()) return MakeVec(v.AsVec3());
        if (v.IsObject()) {
            GameObject* g = GameObject::FromInstanceId(v.AsObjectId());
            return g ? MakeObj(kGameObject, g->InstanceId()) : Value();
        }
        if (v.IsColor()) {
            vs::VsColor c = v.AsColor();
            Table* t = L->NewTable();
            t->Set(*L, L->NewStr("r"), Value::Num(c.r)); t->Set(*L, L->NewStr("g"), Value::Num(c.g));
            t->Set(*L, L->NewStr("b"), Value::Num(c.b)); t->Set(*L, L->NewStr("a"), Value::Num(c.a));
            return Value::Of(t);
        }
        if (v.IsArray()) {
            Table* t = L->NewTable();
            Value tv = Value::Of(t);
            std::vector<Value> pin{tv}; L->pins.push_back(&pin);
            auto a = v.AsArray();
            for (std::size_t i = 0; a && i < a->size(); ++i) t->Set(*L, Value::Int((std::int64_t)i + 1), ToLua((*a)[i], depth + 1));
            L->pins.pop_back();
            return tv;
        }
        if (v.IsMap()) {
            Table* t = L->NewTable();
            Value tv = Value::Of(t);
            std::vector<Value> pin{tv}; L->pins.push_back(&pin);
            auto m = v.AsMap();
            if (m) for (auto& kv : *m) t->Set(*L, L->NewStr(kv.first), ToLua(kv.second, depth + 1));
            L->pins.pop_back();
            return tv;
        }
        return Value();
    }
    vs::VsValue FromLua(const Value& v, int depth = 0) {
        if (depth > 8) return vs::VsValue{};
        switch (v.t) {
            case T::Bool: return vs::VsValue(v.b);
            case T::Int: return vs::VsValue((float)v.i);
            case T::Num: return vs::VsValue((float)v.n);
            case T::Str: return vs::VsValue(static_cast<Str*>(v.o)->s);
            case T::User: return vs::VsValue::Object(static_cast<User*>(v.o)->id);
            case T::Table: {
                auto* t = static_cast<Table*>(v.o);
                if (t->meta == vecMeta && vecMeta) { Vec3 r; if (GetVec(v, r)) return vs::VsValue(r); }
                if (!t->arr.empty() && t->ents.empty()) {
                    auto a = vs::VsValue::MakeArray();
                    for (auto& e : t->arr) a.AsArray()->push_back(FromLua(e, depth + 1));
                    return a;
                }
                auto m = vs::VsValue::MakeMap();
                for (std::size_t i = 0; i < t->arr.size(); ++i) (*m.AsMap())[std::to_string(i + 1)] = FromLua(t->arr[i], depth + 1);
                for (auto& e : t->ents) if (!e.second.IsNil()) (*m.AsMap())[L->RawToString(e.first)] = FromLua(e.second, depth + 1);
                return m;
            }
            default: return vs::VsValue{};
        }
    }

    // ------------------------------------------------ engine lookups
    GameObject* GoOf(const Value& v, bool mustExist = true) {
        if (v.t != T::User) { if (mustExist) L->Error("expected a GameObject (use gameObject or Find(name))"); return nullptr; }
        GameObject* g = GameObject::FromInstanceId(static_cast<User*>(v.o)->id);
        if (!g && mustExist) L->Error("this GameObject was destroyed");
        return g;
    }
    Scene* SceneOf() { return host && host->gameObject ? host->gameObject->scene() : nullptr; }

    Value PropToLua(const PropertyValue& p) {
        switch (p.Type()) {
            case PropertyType::Bool: return Value::Bool(p.AsBool());
            case PropertyType::Int: return Value::Int(p.AsInt());
            case PropertyType::Float: return Value::Num(p.AsFloat());
            case PropertyType::Enum: return Value::Int(p.AsEnum());
            case PropertyType::String: case PropertyType::AssetPath: case PropertyType::ObjectRef: return L->NewStr(p.AsString());
            case PropertyType::Vec2: { Vec2 v = p.AsVec2(); return MakeVec(v.x, v.y, 0); }
            case PropertyType::Vec3: return MakeVec(p.AsVec3());
            case PropertyType::Vec4: case PropertyType::Quat: {
                Vec4 v = p.Type() == PropertyType::Vec4 ? p.AsVec4() : Vec4{p.AsQuat().x, p.AsQuat().y, p.AsQuat().z, p.AsQuat().w};
                Table* t = L->NewTable();
                t->Set(*L, L->NewStr("x"), Value::Num(v.x)); t->Set(*L, L->NewStr("y"), Value::Num(v.y));
                t->Set(*L, L->NewStr("z"), Value::Num(v.z)); t->Set(*L, L->NewStr("w"), Value::Num(v.w));
                return Value::Of(t);
            }
            case PropertyType::Color: {
                Color c = p.AsColor();
                Table* t = L->NewTable();
                t->Set(*L, L->NewStr("r"), Value::Num(c.r)); t->Set(*L, L->NewStr("g"), Value::Num(c.g));
                t->Set(*L, L->NewStr("b"), Value::Num(c.b)); t->Set(*L, L->NewStr("a"), Value::Num(c.a));
                return Value::Of(t);
            }
            default: return Value();
        }
    }
    bool LuaToProp(const Value& v, PropertyType want, PropertyValue& out) {
        Value n;
        switch (want) {
            case PropertyType::Bool: out = PropertyValue::MakeBool(v.Truthy()); return true;
            case PropertyType::Int: { std::int64_t i; if (!ToInteger(v, i)) return false; out = PropertyValue::MakeInt(i); return true; }
            case PropertyType::Float: if (!ToNumber(v, n)) return false; out = PropertyValue::MakeFloat((float)(n.t == T::Int ? (double)n.i : n.n)); return true;
            case PropertyType::Enum: { std::int64_t i; if (!ToInteger(v, i)) return false; out = PropertyValue::MakeEnum((int)i); return true; }
            case PropertyType::String: if (v.t != T::Str) return false; out = PropertyValue::MakeString(static_cast<Str*>(v.o)->s); return true;
            case PropertyType::AssetPath: if (v.t != T::Str) return false; out = PropertyValue::MakeAssetPath(static_cast<Str*>(v.o)->s); return true;
            case PropertyType::Vec2: { Vec3 r; if (!GetVec(v, r)) return false; out = PropertyValue::MakeVec2(Vec2{r.x, r.y}); return true; }
            case PropertyType::Vec3: { Vec3 r; if (!GetVec(v, r)) return false; out = PropertyValue::MakeVec3(r); return true; }
            case PropertyType::Color: {
                if (v.t != T::Table) return false;
                auto* t = static_cast<Table*>(v.o);
                float r = 0, g = 0, b = 0, a = 1;
                if (!GetNum(t, "r", r) || !GetNum(t, "g", g) || !GetNum(t, "b", b)) return false;
                GetNum(t, "a", a);
                Color c; c.r = r; c.g = g; c.b = b; c.a = a;
                out = PropertyValue::MakeColor(c); return true;
            }
            default: return false;
        }
    }

    // ------------------------------------------------ registration
    void Def(Table* t, const char* name, NativeFn fn) { t->Set(*L, L->NewStr(name), L->Fn(name, std::move(fn))); }
    Table* NewMeta() { Table* m = L->NewTable(); L->extraRoots.push_back(m); return m; }

    std::string KeyOf(State& S, std::vector<Value>& a, std::size_t i) {
        if (i >= a.size() || a[i].t != T::Str) S.Error("the property name must be a string");
        return static_cast<Str*>(a[i].o)->s;
    }

    void Init() {
        L = std::make_unique<State>();
        L->limits.instructions = instr; L->limits.seconds = seconds; L->limits.memoryBytes = memory;
        L->OpenLibs();
        L->out = [](const std::string& s) { Log::Info("[script] ", s); };
        // Not part of the sandbox for game scripts.
        L->globals->Set(*L, L->NewStr("os"), Value());   // os.time/date are not needed; os.clock stays below
        std::string err;
        L->DoString(kPrelude, "prelude", &err);
        vecMeta = static_cast<Table*>(L->globals->GetStr("Vector3").o);
        L->extraRoots.push_back(vecMeta);
        RegisterEngine();
        for (std::size_t i = 0; i < L->globals->ents.size(); ++i)
            if (L->globals->ents[i].first.t == T::Str) baseline.insert(static_cast<Str*>(L->globals->ents[i].first.o)->s);
    }

    void RegisterEngine() {
        State& S = *L;
        Table* G = S.globals;
        goMeta = NewMeta(); trMeta = NewMeta(); compMeta = NewMeta();
        goMethods = NewMeta(); trMethods = NewMeta();
        Impl* me = this;

        auto eq = [](State&, std::vector<Value>& a, std::vector<Value>& r) {
            bool same = a.size() >= 2 && a[0].t == T::User && a[1].t == T::User &&
                        static_cast<User*>(a[0].o)->kind == static_cast<User*>(a[1].o)->kind &&
                        static_cast<User*>(a[0].o)->id == static_cast<User*>(a[1].o)->id &&
                        static_cast<User*>(a[0].o)->tag == static_cast<User*>(a[1].o)->tag;
            r.push_back(Value::Bool(same));
        };
        for (Table* m : {goMeta, trMeta, compMeta}) { Def(m, "__eq", eq); }
        Def(goMeta, "__tostring", [me](State& S, std::vector<Value>& a, std::vector<Value>& r) {
            GameObject* g = me->GoOf(a.empty() ? Value() : a[0], false);
            r.push_back(S.NewStr(g ? "GameObject(" + g->name + ")" : "GameObject(destroyed)"));
        });
        Def(trMeta, "__tostring", [me](State& S, std::vector<Value>& a, std::vector<Value>& r) {
            GameObject* g = me->GoOf(a.empty() ? Value() : a[0], false);
            r.push_back(S.NewStr(g ? "Transform(" + g->name + ")" : "Transform(destroyed)"));
        });
        Def(compMeta, "__tostring", [me](State& S, std::vector<Value>& a, std::vector<Value>& r) {
            (void)me; r.push_back(S.NewStr(a.empty() || a[0].t != T::User ? "Component" : static_cast<User*>(a[0].o)->tag + " component"));
        });

        // ---- GameObject
        Def(goMethods, "GetComponent", [me](State& S, std::vector<Value>& a, std::vector<Value>& r) {
            GameObject* g = me->GoOf(a.empty() ? Value() : a[0]);
            std::string type = me->KeyOf(S, a, 1);
            if (type == "Transform") { r.push_back(me->MakeObj(kTransform, g->InstanceId())); return; }
            for (auto& c : g->Components()) {
                auto info = PropertyRegistry::FindFor(*c);
                if (info && info->name == type) { r.push_back(me->MakeObj(kComponent, g->InstanceId(), type)); return; }
            }
            r.push_back(Value());
        });
        Def(goMethods, "SetActive", [me](State&, std::vector<Value>& a, std::vector<Value>&) {
            GameObject* g = me->GoOf(a.empty() ? Value() : a[0]);
            g->active = a.size() > 1 && a[1].Truthy();
        });
        Def(goMethods, "IsAlive", [me](State&, std::vector<Value>& a, std::vector<Value>& r) {
            r.push_back(Value::Bool(me->GoOf(a.empty() ? Value() : a[0], false) != nullptr));
        });
        Def(goMethods, "Destroy", [me](State& S, std::vector<Value>& a, std::vector<Value>&) {
            GameObject* g = me->GoOf(a.empty() ? Value() : a[0]);
            Scene* sc = g->scene();
            if (!sc) S.Error("the object is not in a scene");
            Value secs; if (a.size() > 1 && ToNumber(a[1], secs)) sc->Destroy(g, (float)(secs.t == T::Int ? (double)secs.i : secs.n));
            else sc->Destroy(g);
        });
        Def(goMeta, "__index", [me](State& S, std::vector<Value>& a, std::vector<Value>& r) {
            std::string k = me->KeyOf(S, a, 1);
            Value m = me->goMethods->GetStr(k);
            if (!m.IsNil()) { r.push_back(m); return; }
            GameObject* g = me->GoOf(a[0], false);
            if (!g) { if (k == "name" || k == "tag") r.push_back(Value()); else S.Error("this GameObject was destroyed"); return; }
            if (k == "name") r.push_back(S.NewStr(g->name));
            else if (k == "tag") r.push_back(S.NewStr(std::string(g->tag)));
            else if (k == "active") r.push_back(Value::Bool(g->active));
            else if (k == "transform") r.push_back(me->MakeObj(kTransform, g->InstanceId()));
            else if (k == "id") r.push_back(Value::Int((std::int64_t)g->InstanceId()));
            else r.push_back(Value());
        });
        Def(goMeta, "__newindex", [me](State& S, std::vector<Value>& a, std::vector<Value>&) {
            std::string k = me->KeyOf(S, a, 1);
            GameObject* g = me->GoOf(a[0]);
            Value v = a.size() > 2 ? a[2] : Value();
            if (k == "name" && v.t == T::Str) g->name = static_cast<Str*>(v.o)->s;
            else if (k == "tag" && v.t == T::Str) g->tag = static_cast<Str*>(v.o)->s;
            else if (k == "active") g->active = v.Truthy();
            else S.Error("cannot set '" + k + "' on a GameObject (name, tag and active can be set)");
        });

        // ---- Transform
        Def(trMethods, "Translate", [me](State&, std::vector<Value>& a, std::vector<Value>&) {
            GameObject* g = me->GoOf(a.empty() ? Value() : a[0]);
            g->transform->Translate(me->NeedVec(a.size() > 1 ? a[1] : Value(), "Translate"));
        });
        Def(trMethods, "Rotate", [me](State&, std::vector<Value>& a, std::vector<Value>&) {
            GameObject* g = me->GoOf(a.empty() ? Value() : a[0]);
            g->transform->Rotate(me->NeedVec(a.size() > 1 ? a[1] : Value(), "Rotate"));
        });
        Def(trMethods, "LookAt", [me](State&, std::vector<Value>& a, std::vector<Value>&) {
            GameObject* g = me->GoOf(a.empty() ? Value() : a[0]);
            g->transform->LookAt(me->NeedVec(a.size() > 1 ? a[1] : Value(), "LookAt"));
        });
        Def(trMeta, "__index", [me](State& S, std::vector<Value>& a, std::vector<Value>& r) {
            std::string k = me->KeyOf(S, a, 1);
            Value m = me->trMethods->GetStr(k);
            if (!m.IsNil()) { r.push_back(m); return; }
            GameObject* g = me->GoOf(a[0]);
            Transform* t = g->transform;
            if (k == "position") r.push_back(me->MakeVec(t->Position()));
            else if (k == "localPosition") r.push_back(me->MakeVec(t->localPosition));
            else if (k == "localScale") r.push_back(me->MakeVec(t->localScale));
            else if (k == "localEulerAngles" || k == "eulerAngles") r.push_back(me->MakeVec(t->LocalEulerAngles()));
            else if (k == "forward") r.push_back(me->MakeVec(t->Forward()));
            else if (k == "right") r.push_back(me->MakeVec(t->Right()));
            else if (k == "up") r.push_back(me->MakeVec(t->Up()));
            else if (k == "gameObject") r.push_back(me->MakeObj(kGameObject, g->InstanceId()));
            else r.push_back(Value());
        });
        Def(trMeta, "__newindex", [me](State& S, std::vector<Value>& a, std::vector<Value>&) {
            std::string k = me->KeyOf(S, a, 1);
            GameObject* g = me->GoOf(a[0]);
            Transform* t = g->transform;
            if (k != "position" && k != "localPosition" && k != "localScale" && k != "localEulerAngles" && k != "eulerAngles")
                S.Error("cannot set '" + k + "' on a Transform (position, localPosition, localScale, localEulerAngles)");
            Vec3 v = me->NeedVec(a.size() > 2 ? a[2] : Value(), ("transform." + k).c_str());
            if (k == "position") t->SetPosition(v);
            else if (k == "localPosition") t->localPosition = v;
            else if (k == "localScale") t->localScale = v;
            else if (k == "localEulerAngles" || k == "eulerAngles") t->SetLocalEulerAngles(v);
            else S.Error("cannot set '" + k + "' on a Transform (position, localPosition, localScale, localEulerAngles)");
        });

        // ---- components by property path ("Rigidbody3D.mass")
        Def(compMeta, "__index", [me](State& S, std::vector<Value>& a, std::vector<Value>& r) {
            std::string k = me->KeyOf(S, a, 1);
            GameObject* g = me->GoOf(a[0]);
            const std::string type = static_cast<User*>(a[0].o)->tag;
            PropertyValue pv; std::string err;
            if (!PropertyRegistry::TryGetProperty(*g, type + "." + k, pv, &err)) S.Error("component " + type + " has no property '" + k + "'");
            r.push_back(me->PropToLua(pv));
        });
        Def(compMeta, "__newindex", [me](State& S, std::vector<Value>& a, std::vector<Value>&) {
            std::string k = me->KeyOf(S, a, 1);
            GameObject* g = me->GoOf(a[0]);
            const std::string type = static_cast<User*>(a[0].o)->tag, path = type + "." + k;
            PropertyValue cur, nv; std::string err;
            if (!PropertyRegistry::TryGetProperty(*g, path, cur, &err)) S.Error("component " + type + " has no property '" + k + "'");
            if (!me->LuaToProp(a.size() > 2 ? a[2] : Value(), cur.Type(), nv))
                S.Error("cannot store that value in " + type + "." + k + " (it is a " + PropertyTypeName(cur.Type()) + ")");
            if (!PropertyRegistry::SetProperty(*g, path, nv, &err)) S.Error("cannot set " + type + "." + k + ": " + err);
        });

        // ---- globals
        Def(G, "Find", [me](State& S, std::vector<Value>& a, std::vector<Value>& r) {
            Scene* sc = me->SceneOf(); if (!sc) S.Error("Find: there is no scene");
            r.push_back(me->ObjOrNil(sc->Find(me->KeyOf(S, a, 0))));
        });
        Def(G, "CreateGameObject", [me](State& S, std::vector<Value>& a, std::vector<Value>& r) {
            Scene* sc = me->SceneOf(); if (!sc) S.Error("CreateGameObject: there is no scene");
            r.push_back(me->ObjOrNil(sc->CreateGameObject(a.empty() ? std::string("GameObject") : me->KeyOf(S, a, 0))));
        });
        Def(G, "Destroy", [me](State& S, std::vector<Value>& a, std::vector<Value>&) {
            GameObject* g = me->GoOf(a.empty() ? Value() : a[0]);
            Scene* sc = g->scene(); if (!sc) S.Error("Destroy: the object is not in a scene");
            Value secs; if (a.size() > 1 && ToNumber(a[1], secs)) sc->Destroy(g, (float)(secs.t == T::Int ? (double)secs.i : secs.n));
            else sc->Destroy(g);
        });

        Table* Tm = NewMeta();   // Time: read-only properties
        Table* Time_ = S.NewTable(); Time_->meta = Tm;
        G->Set(S, S.NewStr("Time"), Value::Of(Time_));
        Def(Tm, "__index", [me](State& S, std::vector<Value>& a, std::vector<Value>& r) {
            std::string k = me->KeyOf(S, a, 1);
            if (k == "deltaTime") r.push_back(Value::Num(me->host && me->host->deltaTime > 0 ? me->host->deltaTime : Time::DeltaTime()));
            else if (k == "time") r.push_back(Value::Num(Time::ElapsedTime()));
            else if (k == "timeScale") r.push_back(Value::Num(Time::TimeScale()));
            else if (k == "unscaledDeltaTime") r.push_back(Value::Num(Time::UnscaledDeltaTime()));
            else if (k == "fixedDeltaTime") r.push_back(Value::Num(Time::FixedDeltaTime()));
            else if (k == "frameCount") r.push_back(Value::Int((std::int64_t)Time::FrameCount()));
            else r.push_back(Value());
        });
        Def(Tm, "__newindex", [me](State& S, std::vector<Value>& a, std::vector<Value>&) {
            std::string k = me->KeyOf(S, a, 1);
            Value v;
            if (k == "timeScale" && a.size() > 2 && ToNumber(a[2], v)) { Time::SetTimeScale((float)(v.t == T::Int ? (double)v.i : v.n)); return; }
            S.Error("Time." + k + " is read-only (only Time.timeScale can be set)");
        });

        Table* Log_ = S.NewTable();
        G->Set(S, S.NewStr("Log"), Value::Of(Log_));
        auto joined = [](State& S, std::vector<Value>& a) { std::string s; for (std::size_t i = 0; i < a.size(); ++i) { if (i) s += ' '; s += S.ToStringValue(a[i]); } return s; };
        Def(Log_, "info", [joined](State& S, std::vector<Value>& a, std::vector<Value>&) { Log::Info("[script] ", joined(S, a)); });
        Def(Log_, "warn", [joined](State& S, std::vector<Value>& a, std::vector<Value>&) { Log::Warning("[script] ", joined(S, a)); });
        Def(Log_, "warning", [joined](State& S, std::vector<Value>& a, std::vector<Value>&) { Log::Warning("[script] ", joined(S, a)); });
        Def(Log_, "error", [joined](State& S, std::vector<Value>& a, std::vector<Value>&) { Log::Error("[script] ", joined(S, a)); });
        Def(Log_, "trace", [joined](State& S, std::vector<Value>& a, std::vector<Value>&) { Log::Trace("[script] ", joined(S, a)); });

        Table* In = S.NewTable();
        G->Set(S, S.NewStr("Input"), Value::Of(In));
        auto key = [me](State& S, std::vector<Value>& a) -> char {
            std::string n = me->KeyOf(S, a, 0);
            if (!Input::IsKnownKeyName(n)) S.Error("Input: unknown key name '" + n + "'");
            return Input::KeyFromName(n);
        };
        Def(In, "GetKey", [key](State& S, std::vector<Value>& a, std::vector<Value>& r) { r.push_back(Value::Bool(Input::GetKeyGame(key(S, a)))); });
        Def(In, "GetKeyDown", [key](State& S, std::vector<Value>& a, std::vector<Value>& r) { r.push_back(Value::Bool(Input::GetKeyDownGame(key(S, a)))); });
        Def(In, "GetKeyUp", [key](State& S, std::vector<Value>& a, std::vector<Value>& r) { r.push_back(Value::Bool(Input::GetKeyUp(key(S, a)))); });
        auto btn = [](State& S, std::vector<Value>& a) { std::int64_t i; if (a.empty() || !ToInteger(a[0], i)) S.Error("Input: the mouse button must be a number (0 left, 1 right, 2 middle)"); return (int)i; };
        Def(In, "GetMouseButton", [btn](State& S, std::vector<Value>& a, std::vector<Value>& r) { r.push_back(Value::Bool(Input::GetMouseButton(btn(S, a)))); });
        Def(In, "GetMouseButtonDown", [btn](State& S, std::vector<Value>& a, std::vector<Value>& r) { r.push_back(Value::Bool(Input::GetMouseButtonDown(btn(S, a)))); });
        Def(In, "GetMouseButtonUp", [btn](State& S, std::vector<Value>& a, std::vector<Value>& r) { r.push_back(Value::Bool(Input::GetMouseButtonUp(btn(S, a)))); });
        Def(In, "MousePosition", [me](State&, std::vector<Value>&, std::vector<Value>& r) { Vec2 p = Input::MousePosition(); r.push_back(me->MakeVec(p.x, p.y, 0)); });
        Def(In, "GetAxis", [me](State& S, std::vector<Value>& a, std::vector<Value>& r) {
            std::string n = me->KeyOf(S, a, 0);
            Vec2 v = Input::AxisWASD();
            if (n == "Horizontal") r.push_back(Value::Num(v.x));
            else if (n == "Vertical") r.push_back(Value::Num(v.y));
            else S.Error("Input.GetAxis: use \"Horizontal\" or \"Vertical\"");
        });

        Table* Ph = S.NewTable();
        G->Set(S, S.NewStr("Physics"), Value::Of(Ph));
        Def(Ph, "Raycast", [me](State& S, std::vector<Value>& a, std::vector<Value>& r) {
            Scene* sc = me->SceneOf(); if (!sc) S.Error("Physics.Raycast: there is no scene");
            Vec3 o = me->NeedVec(a.size() > 0 ? a[0] : Value(), "Physics.Raycast origin");
            Vec3 d = me->NeedVec(a.size() > 1 ? a[1] : Value(), "Physics.Raycast direction");
            Value md; float maxD = 1e9f; if (a.size() > 2 && ToNumber(a[2], md)) maxD = (float)(md.t == T::Int ? (double)md.i : md.n);
            RaycastHit3D h = sc->physics3D().Raycast(*sc, o, d, maxD);
            if (!h.hit) { r.push_back(Value()); return; }
            Table* t = S.NewTable(); Value tv = Value::Of(t);
            std::vector<Value> pin{tv}; S.pins.push_back(&pin);
            t->Set(S, S.NewStr("point"), me->MakeVec(h.point));
            t->Set(S, S.NewStr("normal"), me->MakeVec(h.normal));
            t->Set(S, S.NewStr("distance"), Value::Num(h.distance));
            t->Set(S, S.NewStr("gameObject"), me->ObjOrNil(h.gameObject));
            S.pins.pop_back();
            r.push_back(tv);
        });
        Def(Ph, "OverlapSphere", [me](State& S, std::vector<Value>& a, std::vector<Value>& r) {
            Scene* sc = me->SceneOf(); if (!sc) S.Error("Physics.OverlapSphere: there is no scene");
            Vec3 c = me->NeedVec(a.size() > 0 ? a[0] : Value(), "Physics.OverlapSphere center");
            Value rv; if (a.size() < 2 || !ToNumber(a[1], rv)) S.Error("Physics.OverlapSphere needs a radius");
            auto hits = sc->physics3D().OverlapSphere(*sc, c, (float)(rv.t == T::Int ? (double)rv.i : rv.n));
            Table* t = S.NewTable(); Value tv = Value::Of(t);
            std::vector<Value> pin{tv}; S.pins.push_back(&pin);
            std::int64_t i = 1;
            for (Collider3D* col : hits) if (col && col->gameObject) t->Set(S, Value::Int(i++), me->ObjOrNil(col->gameObject));
            S.pins.pop_back();
            r.push_back(tv);
        });

        // ---- spawning, 2D physics, UI text, audio, saved values
        auto num = [](const Value& v, float d) { Value n; return ToNumber(v, n) ? (float)(n.t == T::Int ? (double)n.i : n.n) : d; };
        Def(G, "Instantiate", [me, num](State& S, std::vector<Value>& a, std::vector<Value>& r) {
            Scene* sc = me->SceneOf(); if (!sc) S.Error("Instantiate: there is no scene");
            std::string path = me->KeyOf(S, a, 0);
            if (!ScriptAssetPathOk(path, "Instantiate")) { r.push_back(Value()); return; }
            GameObject* g = SceneSerializer::InstantiateFromFile(*sc, path, nullptr);
            if (!g) { r.push_back(Value()); return; }
            Vec3 p;
            if (a.size() > 1 && me->GetVec(a[1], p)) g->transform->localPosition = p;
            else if (a.size() > 1) g->transform->localPosition = {num(a[1], 0), a.size() > 2 ? num(a[2], 0) : 0.0f, a.size() > 3 ? num(a[3], 0) : 0.0f};
            r.push_back(me->ObjOrNil(g));
        });
        Table* P2 = S.NewTable();
        G->Set(S, S.NewStr("Physics2D"), Value::Of(P2));
        Def(P2, "Raycast", [me, num](State& S, std::vector<Value>& a, std::vector<Value>& r) {
            Scene* sc = me->SceneOf(); if (!sc) S.Error("Physics2D.Raycast: there is no scene");
            Vec3 o = me->NeedVec(a.size() > 0 ? a[0] : Value(), "Physics2D.Raycast origin");
            Vec3 d = me->NeedVec(a.size() > 1 ? a[1] : Value(), "Physics2D.Raycast direction");
            RaycastHit2D h = sc->physics().Raycast(*sc, Vec2{o.x, o.y}, Vec2{d.x, d.y}, a.size() > 2 ? num(a[2], 1e9f) : 1e9f);
            if (!h.hit) { r.push_back(Value()); return; }
            Table* t = S.NewTable(); Value tv = Value::Of(t);
            std::vector<Value> pin{tv}; S.pins.push_back(&pin);
            t->Set(S, S.NewStr("point"), me->MakeVec(h.point.x, h.point.y, 0));
            t->Set(S, S.NewStr("normal"), me->MakeVec(h.normal.x, h.normal.y, 0));
            t->Set(S, S.NewStr("distance"), Value::Num(h.distance));
            t->Set(S, S.NewStr("gameObject"), me->ObjOrNil(h.gameObject));
            S.pins.pop_back();
            r.push_back(tv);
        });
        Def(P2, "OverlapCircle", [me, num](State& S, std::vector<Value>& a, std::vector<Value>& r) {
            Scene* sc = me->SceneOf(); if (!sc) S.Error("Physics2D.OverlapCircle: there is no scene");
            Vec3 c = me->NeedVec(a.size() > 0 ? a[0] : Value(), "Physics2D.OverlapCircle center");
            auto hits = sc->physics().OverlapCircle(*sc, Vec2{c.x, c.y}, a.size() > 1 ? num(a[1], 0) : 0.0f);
            Table* t = S.NewTable(); Value tv = Value::Of(t);
            std::vector<Value> pin{tv}; S.pins.push_back(&pin);
            std::int64_t i = 1;
            for (Collider2D* col : hits) if (col && col->gameObject) t->Set(S, Value::Int(i++), me->ObjOrNil(col->gameObject));
            S.pins.pop_back();
            r.push_back(tv);
        });
        Table* UI_ = S.NewTable();
        G->Set(S, S.NewStr("UI"), Value::Of(UI_));
        Def(UI_, "SetText", [me](State& S, std::vector<Value>& a, std::vector<Value>&) {
            GameObject* g = nullptr;
            if (!a.empty() && a[0].t == T::Str) { Scene* sc = me->SceneOf(); g = sc ? sc->Find(static_cast<Str*>(a[0].o)->s) : nullptr; }
            else if (!a.empty()) g = me->GoOf(a[0]);
            if (!g) S.Error("UI.SetText: no such object (pass a GameObject or its name)");
            std::string text = a.size() > 1 ? S.ToStringValue(a[1]) : std::string();
            if (auto* tr = g->GetComponent<TextRenderer>()) tr->text = text;
            else if (auto* bt = g->GetComponent<UIButton>()) bt->label = text;
            else S.Error("UI.SetText: '" + g->name + "' has no text to set");
        });
        Table* Au = S.NewTable();
        G->Set(S, S.NewStr("Audio"), Value::Of(Au));
        Def(Au, "Play", [me, num](State& S, std::vector<Value>& a, std::vector<Value>&) {
            GameObject* g = !a.empty() && a[0].t == T::User ? me->GoOf(a[0]) : (me->host ? me->host->gameObject : nullptr);
            auto* src = g ? g->GetComponent<AudioSource>() : nullptr;
            if (!src) S.Error("Audio.Play: the object has no AudioSource");
            size_t vi = !a.empty() && a[0].t == T::User ? 1 : 0;
            if (a.size() > vi) src->PlayOneShot(num(a[vi], 1.0f)); else src->Play();
        });
        Table* Pr = S.NewTable();
        G->Set(S, S.NewStr("Prefs"), Value::Of(Pr));
        Def(Pr, "SetNumber", [me, num](State& S, std::vector<Value>& a, std::vector<Value>&) { Prefs::SetFloat(me->KeyOf(S, a, 0), num(a.size() > 1 ? a[1] : Value(), 0)); });
        Def(Pr, "GetNumber", [me, num](State& S, std::vector<Value>& a, std::vector<Value>& r) { r.push_back(Value::Num(Prefs::GetFloat(me->KeyOf(S, a, 0), num(a.size() > 1 ? a[1] : Value(), 0)))); });
        Def(Pr, "SetString", [me](State& S, std::vector<Value>& a, std::vector<Value>&) { Prefs::SetString(me->KeyOf(S, a, 0), a.size() > 1 ? S.ToStringValue(a[1]) : std::string()); });
        Def(Pr, "GetString", [me](State& S, std::vector<Value>& a, std::vector<Value>& r) { r.push_back(S.NewStr(Prefs::GetString(me->KeyOf(S, a, 0), a.size() > 1 && a[1].t == T::Str ? static_cast<Str*>(a[1].o)->s : std::string()))); });
    }

    // ------------------------------------------------ running
    Value FindFn(const std::string& name) const {
        Value v = L->globals->GetStr(name);
        return v.t == T::Func ? v : Value();   // only script-defined functions are handlers, never engine API (Destroy, Find ...)
    }
    Value ResolveEvent(const std::string& name) const {
        Value v = FindFn(name);
        if (!v.IsNil()) return v;
        std::string snake;   // OnEnable / FixedUpdate -> on_enable / fixed_update
        for (std::size_t i = 0; i < name.size(); ++i) {
            char c = name[i];
            if (std::isupper((unsigned char)c)) { if (i) snake += '_'; snake += (char)std::tolower((unsigned char)c); }
            else snake += c;
        }
        std::string stem = snake.rfind("on_", 0) == 0 ? snake.substr(3) : snake;
        std::string pas = PascalOf(stem);
        std::vector<std::string> cand{snake, "On" + pas, "on" + pas};
        // The plain lifecycle names (Start, Update, FixedUpdate ...) also work without "On".
        static const char* const kLifecycle[] = {"awake", "start", "update", "fixed_update", "late_update"};
        for (const char* l : kLifecycle)
            if (stem == l) {
                cand.push_back(pas); cand.push_back(stem);
                cand.push_back(std::string(1, (char)std::tolower((unsigned char)pas[0])) + pas.substr(1));
            }
        for (auto& c : cand) { v = FindFn(c); if (!v.IsNil()) return v; }
        return Value();
    }
    bool Invoke(const Value& fn, std::vector<Value> args, std::vector<Value>* rets, std::string* error) {
        std::string err;
        std::vector<Value> out;
        if (L->CallProtected(fn, std::move(args), &out, &err)) { if (rets) *rets = std::move(out); return true; }
        if (error) *error = err; else Log::Error(err);
        return false;
    }
    void BindGlobals() {
        if (!L) return;
        L->globals->Set(*L, L->NewStr("gameObject"), host && host->gameObject ? MakeObj(kGameObject, host->gameObject->InstanceId()) : Value());
        L->globals->Set(*L, L->NewStr("transform"), host && host->gameObject ? MakeObj(kTransform, host->gameObject->InstanceId()) : Value());
    }
    bool RunScript(const std::string& source, std::string* error) {
        std::string err;
        if (!L->DoString(source, "script.lua", &err)) { if (error) *error = err; return false; }
        return true;
    }
    std::vector<std::string> OwnNames() const {
        std::vector<std::string> names;
        for (auto& e : L->globals->ents) {
            if (e.first.t != T::Str || e.second.IsNil()) continue;
            const std::string& n = static_cast<Str*>(e.first.o)->s;
            if (baseline.count(n) || IsEngineName(n)) continue;
            if (e.second.t == T::Func || e.second.t == T::Native) continue;
            names.push_back(n);
        }
        return names;
    }
};

LuaScriptVM::LuaScriptVM() : m_impl(new Impl) { m_impl->Init(); }
LuaScriptVM::~LuaScriptVM() = default;

void LuaScriptVM::SetLimits(std::int64_t instructions, double seconds, std::size_t memoryBytes) {
    m_impl->instr = instructions; m_impl->seconds = seconds; m_impl->memory = memoryBytes;
    m_impl->L->limits.instructions = instructions; m_impl->L->limits.seconds = seconds; m_impl->L->limits.memoryBytes = memoryBytes;
}
std::size_t LuaScriptVM::MemoryUsed() const { return m_impl->L->allocated; }

std::vector<std::string> LuaScriptVM::BuiltinNames() const {
    return {"Find", "CreateGameObject", "Destroy", "Time.deltaTime", "Time.time", "Time.timeScale", "Log.info", "Log.warn", "Log.error",
            "Input.GetKey", "Input.GetKeyDown", "Input.GetKeyUp", "Input.GetMouseButton", "Input.GetAxis", "Input.MousePosition",
            "Physics.Raycast", "Physics.OverlapSphere", "Physics2D.Raycast", "Physics2D.OverlapCircle", "Instantiate", "StartCoroutine", "wait", "waitFrames", "waitUntil", "Tween.Value", "Tween.Move", "UI.SetText", "Audio.Play", "Prefs.GetNumber", "Prefs.SetNumber", "Prefs.GetString", "Prefs.SetString", "Vector3.new", "Vector3.Dot", "Vector3.Cross", "Vector3.Distance", "Vector3.Lerp",
            "Mathf.Clamp", "Mathf.Lerp", "Mathf.Sign", "Mathf.MoveTowards", "gameObject", "transform"};
}

namespace {
// "script.lua:12: message ..." -> line 12.
ScriptDiagnostic DiagFrom(const std::string& e) {
    ScriptDiagnostic d; d.message = e;
    std::size_t c1 = e.find(':');
    if (c1 != std::string::npos) {
        std::size_t c2 = e.find(':', c1 + 1);
        if (c2 != std::string::npos) {
            d.line = std::atoi(e.c_str() + c1 + 1);
            std::string m = e.substr(c2 + 1);
            while (!m.empty() && m[0] == ' ') m.erase(0, 1);
            d.message = m;
        }
    }
    return d;
}
}  // namespace

bool LuaScriptVM::Load(const std::string& source, std::string* error) {
    ScriptHost* h = m_impl->host;
    auto fresh = std::make_unique<Impl>();
    fresh->instr = m_impl->instr; fresh->seconds = m_impl->seconds; fresh->memory = m_impl->memory;
    fresh->Init();
    fresh->host = h;
    fresh->BindGlobals();
    m_impl = std::move(fresh);
    std::string err;
    if (!m_impl->RunScript(source, &err)) { if (error) *error = err; return false; }
    return true;
}

bool LuaScriptVM::Validate(const std::string& source, std::string* error) {
    State tmp;
    std::string e;
    if (tmp.Compile(source, "script.lua", &e)) return true;
    ScriptDiagnostic d = DiagFrom(e);
    if (error) *error = "line " + std::to_string(d.line) + ": " + d.message;
    return false;
}
std::vector<ScriptDiagnostic> LuaScriptVM::ValidateAll(const std::string& source) {
    State tmp;
    std::string e;
    if (tmp.Compile(source, "script.lua", &e)) return {};
    return {DiagFrom(e)};
}

bool LuaScriptVM::HotReload(const std::string& source, std::string* error) {
    auto fresh = std::make_unique<Impl>();
    fresh->instr = m_impl->instr; fresh->seconds = m_impl->seconds; fresh->memory = m_impl->memory;
    fresh->Init();
    fresh->host = m_impl->host;
    fresh->BindGlobals();
    std::string err;
    if (!fresh->RunScript(source, &err)) { if (error) *error = err; return false; }
    // Keep the values of the fields both versions have.
    std::set<std::string> newNames;
    for (auto& n : fresh->OwnNames()) newNames.insert(n);
    for (const std::string& n : m_impl->OwnNames()) {
        if (!newNames.count(n)) continue;
        vs::VsValue v = m_impl->FromLua(m_impl->L->globals->GetStr(n));
        fresh->L->globals->Set(*fresh->L, fresh->L->NewStr(n), fresh->ToLua(v));
    }
    m_impl = std::move(fresh);
    return true;
}

void LuaScriptVM::Bind(ScriptHost* host) {
    m_impl->host = host;
    m_impl->BindGlobals();
}

void LuaScriptVM::CallStart() {
    Impl& I = *m_impl;
    for (const char* n : {"Awake", "OnEnable", "Start"}) {
        Value f = I.ResolveEvent(std::string(n) == "Awake" ? "awake" : std::string(n) == "OnEnable" ? "on_enable" : "start");
        if (!f.IsNil()) I.Invoke(f, {}, nullptr, nullptr);
    }
}
void LuaScriptVM::CallUpdate(float dt) {
    Value tick = m_impl->FindFn("__tick");   // runs StartCoroutine/wait/Tween before Update
    if (!tick.IsNil()) m_impl->Invoke(tick, {Value::Num(dt)}, nullptr, nullptr);
    Value f = m_impl->ResolveEvent("update");
    if (!f.IsNil()) m_impl->Invoke(f, {Value::Num(dt)}, nullptr, nullptr);
}
void LuaScriptVM::CallEvent(const std::string& function) {
    Value f = m_impl->ResolveEvent(function);
    if (!f.IsNil()) m_impl->Invoke(f, {}, nullptr, nullptr);
}
void LuaScriptVM::CallEventWith(const std::string& function, const vs::VsValue& arg) {
    Value f = m_impl->ResolveEvent(function);
    if (!f.IsNil()) m_impl->Invoke(f, {m_impl->ToLua(arg)}, nullptr, nullptr);
}
vs::VsValue LuaScriptVM::CallEventArgs(const std::string& function, const std::vector<vs::VsValue>& args, std::string* error) {
    Impl& I = *m_impl;
    Value f = I.ResolveEvent(function);
    if (f.IsNil()) return vs::VsValue{};
    std::vector<Value> la;
    std::vector<Value> pin;
    I.L->pins.push_back(&pin);
    for (auto& a : args) { Value v = I.ToLua(a); la.push_back(v); pin.push_back(v); }
    std::vector<Value> rets;
    bool ok = I.Invoke(f, la, &rets, error);
    I.L->pins.pop_back();
    return ok && !rets.empty() ? I.FromLua(rets[0]) : vs::VsValue{};
}
bool LuaScriptVM::HasEvent(const std::string& function) const { return !m_impl->ResolveEvent(function).IsNil(); }

vs::VsValue LuaScriptVM::GetGlobal(const std::string& name) const {
    return m_impl->FromLua(m_impl->L->globals->GetStr(name));
}
void LuaScriptVM::SetGlobal(const std::string& name, const vs::VsValue& v) {
    Impl& I = *m_impl;
    Value lv = I.ToLua(v);
    I.L->globals->Set(*I.L, I.L->NewStr(name), lv);
}
std::vector<std::string> LuaScriptVM::GlobalNames() const { return m_impl->OwnNames(); }

} // namespace okay
