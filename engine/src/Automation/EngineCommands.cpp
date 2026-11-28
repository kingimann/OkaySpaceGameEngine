// Automation: the engine's own commands (okay/Automation/Automation.hpp). Each one calls the system that already does
// the work; nothing here is a second copy of it:
//   automation.*  the command table itself                    api.*, script.*  the shared script table (ScriptApiRegistry)
//   component.*   the reflection the script languages use (ScriptBindings: the generated classes and methods) and the
//                 property table the Inspector edits (PropertyRegistry: validated, clamped, change hooks fired)
//   scene.*       Scene queries and Scene::Validate / vs::ValidateSceneGraphs (the editor's Problems checks)
//   transform.*   Transform                                   physics.*  Physics3D / Physics2D queries, gravity
//   anim.*        Animator (the clip player) and AnimStateMachine (the state graph)
//   viewport.*    the software renderer's headless path (testing::RenderSceneToImage) and ImageWrite's PNG encoder
//   log.*         the engine log, kept by LogRing below
// Our own code, written on those APIs.
#include "okay/Automation/Automation.hpp"

#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Animator.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Core/Base64.hpp"
#include "okay/Core/ImageWrite.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Debug/DebugUtil.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Physics2D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scripting/ScriptApiRegistry.hpp"
#include "okay/Scripting/ScriptBindings.hpp"
#include "okay/Testing/Regression.hpp"
#include "okay/VisualScript/VsTypes.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace okay::automation {

using json::Value;

// ---- The log ring (log.read) ---------------------------------------------------------------------------------------

namespace {
struct Ring {
    debugutil::SpinLock lock;   // Log may be called from worker threads; held only to copy a line in or out
    std::deque<LogLine> lines;
    std::uint64_t next = 1;
    std::size_t keep = 2000;
    bool installed = false;
};
Ring& TheRing() { static Ring r; return r; }
}

void InstallLogRing(std::size_t keep) {
    Ring& r = TheRing();
    if (r.installed) return;
    r.installed = true;
    r.keep = keep < 16 ? 16 : keep;
    Log::Sink before = Log::sink;   // whoever listened before still hears every line
    Log::sink = [before](Log::Level level, const std::string& text) {
        if (before) before(level, text);
        Ring& g = TheRing();
        debugutil::SpinGuard hold(g.lock);
        g.lines.push_back({g.next++, (int)level, text});
        while (g.lines.size() > g.keep) g.lines.pop_front();
    };
}

bool LogRingInstalled() { return TheRing().installed; }

std::vector<LogLine> LogSince(std::uint64_t after, std::size_t max, int minLevel) {
    Ring& g = TheRing();
    std::vector<LogLine> out;
    debugutil::SpinGuard hold(g.lock);
    for (const LogLine& l : g.lines) {
        if (l.seq <= after || l.level < minLevel) continue;
        out.push_back(l);
    }
    if (max && out.size() > max) out.erase(out.begin(), out.end() - (std::ptrdiff_t)max);
    return out;
}

// ---- Values: JSON <-> script values and property values -----------------------------------------------------------

Value ScriptValueToJson(const vs::VsValue& v) {
    if (v.IsNumber()) return Value::Num(v.AsFloat());
    if (v.IsBool()) return Value::Boolean(v.AsBool());
    if (v.IsString()) return Value::Str(v.AsString());
    if (v.IsVec3()) { const Vec3 p = v.AsVec3(); return Vec3Json(p.x, p.y, p.z); }
    if (v.IsColor()) {
        const vs::VsColor c = v.AsColor();
        return Value::Obj().Set("r", Value::Num(c.r)).Set("g", Value::Num(c.g)).Set("b", Value::Num(c.b)).Set("a", Value::Num(c.a));
    }
    if (v.IsComponentHandle()) {
        const scriptbind::ClassBinding* cls = nullptr;
        Component* c = scriptbind::Resolve(v, &cls);
        if (!c || !c->gameObject) return Value();
        return Value::Obj().Set("object", Value::Str(c->gameObject->Guid())).Set("component", Value::Str(cls ? cls->name : "Component"));
    }
    if (v.IsObject()) {
        const GameObject* g = v.AsObjectId() ? GameObject::FromInstanceId(v.AsObjectId()) : nullptr;
        if (!g) return Value();
        return Value::Obj().Set("object", Value::Str(g->Guid())).Set("name", Value::Str(g->name));
    }
    if (v.IsArray()) {
        Value a = Value::Arr();
        if (auto arr = v.AsArray()) for (const vs::VsValue& e : *arr) a.Push(ScriptValueToJson(e));
        return a;
    }
    if (v.IsMap()) {
        Value o = Value::Obj();
        if (auto m = v.AsMap()) for (const auto& kv : *m) o.Set(kv.first, ScriptValueToJson(kv.second));
        return o;
    }
    return Value();
}

bool JsonToScriptValue(Scene& scene, const Value& j, vs::VsValue& out, std::string& error) {
    switch (j.type) {
    case json::Type::Null: out = vs::VsValue(); return true;
    case json::Type::Bool: out = vs::VsValue(j.boolean); return true;
    case json::Type::Number: out = vs::VsValue((double)j.number); return true;
    case json::Type::String: out = vs::VsValue(j.string); return true;
    case json::Type::Array: {
        out = vs::VsValue::MakeArray();
        for (const Value& e : j.array) {
            vs::VsValue x;
            if (!JsonToScriptValue(scene, e, x, error)) return false;
            out.AsArray()->push_back(std::move(x));
        }
        return true;
    }
    case json::Type::Object: {
        if (const Value* ref = j.Get("object")) {   // {"object": id | path | name}: a GameObject reference
            if (!ref->IsString()) { error = "an object reference is {\"object\": \"<id, path or name>\"}"; return false; }
            Error e;
            GameObject* g = FindObject(scene, ref->string, e);
            if (!g) { error = e.message; return false; }
            out = vs::VsValue::Object(g->InstanceId());
            return true;
        }
        auto num = [&](const char* k, float& f) { const Value* v = j.Get(k); if (!v || !v->IsNumber()) return false; f = (float)v->number; return true; };
        float a = 0, b = 0, c = 0, d = 1;
        if (num("r", a) && num("g", b) && num("b", c)) { num("a", d); out = vs::VsValue::Color(a, b, c, d); return true; }
        if (num("x", a) && num("y", b)) {
            c = 0;
            num("z", c);
            float w = 0;
            if (num("w", w)) out = vs::VsValue::Color(a, b, c, w);   // a Vec4 travels as a colour (ScriptBindings ArgVec4)
            else out = vs::VsValue(Vec3{a, b, c});
            return true;
        }
        out = vs::VsValue::MakeMap();
        for (const auto& kv : j.object) {
            vs::VsValue x;
            if (!JsonToScriptValue(scene, kv.second, x, error)) return false;
            (*out.AsMap())[kv.first] = std::move(x);
        }
        return true;
    }
    }
    return true;
}

Value PropertyToJson(const PropertyValue& v) {
    switch (v.Type()) {
    case PropertyType::None: return Value();
    case PropertyType::Bool: return Value::Boolean(v.AsBool());
    case PropertyType::Int: return Value::Num((double)v.AsInt());
    case PropertyType::Float: return Value::Num(v.AsFloat());
    case PropertyType::String: case PropertyType::AssetPath: case PropertyType::ObjectRef: return Value::Str(v.AsString());
    case PropertyType::Vec2: { const Vec2 p = v.AsVec2(); return Value::Obj().Set("x", Value::Num(p.x)).Set("y", Value::Num(p.y)); }
    case PropertyType::Vec3: { const Vec3 p = v.AsVec3(); return Vec3Json(p.x, p.y, p.z); }
    case PropertyType::Vec4: { const Vec4 p = v.AsVec4(); return Vec3Json(p.x, p.y, p.z).Set("w", Value::Num(p.w)); }
    case PropertyType::Quat: { const Quat q = v.AsQuat(); return Vec3Json(q.x, q.y, q.z).Set("w", Value::Num(q.w)); }
    case PropertyType::Color: {
        const Color c = v.AsColor();
        return Value::Obj().Set("r", Value::Num(c.r)).Set("g", Value::Num(c.g)).Set("b", Value::Num(c.b)).Set("a", Value::Num(c.a));
    }
    case PropertyType::Enum: return Value::Str(v.EnumName().empty() ? std::to_string(v.AsEnum()) : v.EnumName());
    }
    return Value();
}

/// The property text form (PropertyValue::FromString) of a JSON value: numbers, true / false, text, and the
/// components of {x,y,z,w} / {r,g,b,a} / [..] separated by spaces. False for anything else.
bool JsonToPropertyText(const Value& j, std::string& out) {
    auto num = [](double d) { std::string s; json::WriteNumber(d, s); return s; };
    switch (j.type) {
    case json::Type::String: out = j.string; return true;
    case json::Type::Bool: out = j.boolean ? "true" : "false"; return true;
    case json::Type::Number: out = num(j.number); return true;
    case json::Type::Array:
        out.clear();
        for (const Value& e : j.array) { if (!e.IsNumber()) return false; out += (out.empty() ? "" : " ") + num(e.number); }
        return !out.empty();
    case json::Type::Object: {
        out.clear();
        const bool colour = j.Get("r") != nullptr;
        for (const char* k : colour ? std::vector<const char*>{"r", "g", "b", "a"} : std::vector<const char*>{"x", "y", "z", "w"}) {
            const Value* v = j.Get(k);
            if (!v) continue;
            if (!v->IsNumber()) return false;
            out += (out.empty() ? "" : " ") + num(v->number);
        }
        return !out.empty();
    }
    case json::Type::Null: return false;
    }
    return false;
}

namespace {

Value PropertyDescJson(const PropertyDesc& d) {
    Value p = Value::Obj();
    p.Set("name", Value::Str(d.name));
    p.Set("type", Value::Str(PropertyTypeName(d.type)));
    if (!d.label.empty()) p.Set("label", Value::Str(d.label));
    if (!d.tooltip.empty()) p.Set("tooltip", Value::Str(d.tooltip));
    if (!d.group.empty()) p.Set("group", Value::Str(d.group));
    if (!d.unit.empty()) p.Set("unit", Value::Str(d.unit));
    if (d.hasRange) p.Set("min", Value::Num(d.rangeMin)).Set("max", Value::Num(d.rangeMax));
    if (!d.enumOptions.empty()) {
        Value o = Value::Arr();
        for (const auto& s : d.enumOptions) o.Push(Value::Str(s));
        p.Set("options", std::move(o));
    }
    if (!d.assetKind.empty()) p.Set("assetKind", Value::Str(d.assetKind));
    p.Set("readOnly", Value::Boolean(d.IsReadOnly()));
    if (d.defaultValue.IsValid()) p.Set("default", PropertyToJson(d.defaultValue));
    return p;
}

Value MethodJson(const scriptbind::MethodBinding& m) {
    Value j = Value::Obj();
    j.Set("name", Value::Str(m.name));
    Value ps = Value::Arr();
    for (scriptbind::ArgKind k : m.params) ps.Push(Value::Str(scriptbind::ArgKindTypeName(k)));
    j.Set("params", std::move(ps));
    j.Set("required", Value::Num(m.required));
    j.Set("returns", Value::Str(scriptbind::ArgKindTypeName(m.ret)));
    j.Set("readOnly", Value::Boolean(m.isConst));
    return j;
}

/// The component type `name` names: its property-table entry and its bound class (either may be missing).
struct TypeRef {
    std::shared_ptr<const ComponentTypeInfo> info;
    const scriptbind::ClassBinding* cls = nullptr;
    std::string name;
};
bool FindType(const std::string& name, TypeRef& t) {
    t.info = PropertyRegistry::FindByName(name);
    t.cls = scriptbind::FindClass(name);
    if (!t.info && t.cls) t.info = PropertyRegistry::FindByType(t.cls->type);
    if (!t.cls && t.info) for (const auto& c : scriptbind::AllClasses()) if (c.type == t.info->type) { t.cls = &c; break; }
    if (!t.info && !t.cls) return false;
    t.name = t.cls ? t.cls->name : t.info->name;
    return true;
}

/// The `index`-th component of type `t` on `go`.
Component* FindOn(const GameObject& go, const TypeRef& t, int index) {
    if (t.cls) return scriptbind::FindComponent(go, *t.cls, index);
    int n = 0;
    for (const auto& c : go.Components())
        if (c && std::type_index(typeid(*c)) == t.info->type && n++ == index) return c.get();
    return nullptr;
}

Value ComponentJson(const Component& c, bool withProps) {
    Value j = Value::Obj();
    const scriptbind::ClassBinding* cls = scriptbind::ClassOf(c);
    auto info = PropertyRegistry::FindFor(c);
    j.Set("type", Value::Str(cls ? cls->name : info ? info->name : std::string("Component")));
    j.Set("enabled", Value::Boolean(c.enabled));
    if (withProps && info) {
        Value props = Value::Obj();
        for (const PropertyDesc& d : info->properties) {
            if (d.IsHidden() || !d.getter) continue;
            props.Set(d.name, PropertyToJson(d.getter(c)));
        }
        j.Set("properties", std::move(props));
    }
    return j;
}

const Param kObject = Arg("object", ParamType::String, "The object: its id (GUID), a path such as Parent/Child, or a unique name.", true);

const char* ApiTypeName(ScriptApiType t) {
    switch (t) {
    case ScriptApiType::Void: return "void";
    case ScriptApiType::Bool: return "bool";
    case ScriptApiType::Int: return "int";
    case ScriptApiType::Float: return "float";
    case ScriptApiType::String: return "string";
    case ScriptApiType::Vector3: return "Vector3";
    }
    return "any";
}

bool Contains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    auto low = [](std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    return low(hay).find(low(needle)) != std::string::npos;
}

void HierarchyInto(const GameObject& go, int depth, int maxDepth, bool deep, Value& out) {
    Value node = ObjectSummary(go, deep);
    if (go.transform && (maxDepth < 0 || depth < maxDepth)) {
        Value kids = Value::Arr();
        for (Transform* k : go.transform->Children())
            if (k && k->gameObject && !k->gameObject->IsDestroyed() && !k->gameObject->transient) HierarchyInto(*k->gameObject, depth + 1, maxDepth, deep, kids);
        node.Set("children", std::move(kids));
    }
    out.Push(std::move(node));
}

Value HitJson(bool hit, const GameObject* go, const Vec3& point, const Vec3& normal, float distance) {
    Value h = Value::Obj().Set("hit", Value::Boolean(hit));
    if (!hit) return h;
    h.Set("object", go ? Value::Str(go->Guid()) : Value());
    h.Set("name", go ? Value::Str(go->name) : Value());
    h.Set("point", Vec3Json(point.x, point.y, point.z));
    h.Set("normal", Vec3Json(normal.x, normal.y, normal.z));
    h.Set("distance", Value::Num(distance));
    return h;
}

// ---- automation.* ----------------------------------------------------------------------------------------------------

void AddAutomation(Registry& r) {
    r.Add(MakeCommand("automation.list", "List the automation commands (all of them, or those whose name starts with a prefix).",
              {Arg("prefix", ParamType::String, "Only names that start with this, such as \"scene.\".")},
              "{count, commands: [{name, summary, flags}]}", 0, [&r](Call& c) {
                  Value list = Value::Arr();
                  for (const Command* k : r.List(c.Str("prefix"))) {
                      Value d = Registry::Describe(*k);
                      list.Push(Value::Obj().Set("name", Value::Str(k->name)).Set("summary", Value::Str(k->summary)).Set("flags", *d.Get("flags")));
                  }
                  const double n = (double)list.array.size();
                  return Result::Ok(Value::Obj().Set("count", Value::Num(n)).Set("commands", std::move(list)));
              }));
    r.Add(MakeCommand("automation.describe", "Describe one command in full: its parameters (with the JSON Schema), what it returns and its errors.",
              {Arg("name", ParamType::String, "The command, such as \"component.set\".", true)},
              "{name, summary, flags, params, inputSchema, result, errors}", 0, [&r](Call& c) {
                  const Command* k = r.Find(c.Str("name"));
                  if (!k) return Result::Fail(code::NotFound, "There is no command called '" + c.Str("name") + "'.");
                  return Result::Ok(Registry::Describe(*k));
              }, {code::NotFound}));
    r.Add(MakeCommand("automation.host", "Say where commands run: the running editor or a headless host, the project, the scene, and whether the game is playing.",
              {}, "{kind, editor, playing, project, scene, objects, commands}", 0, [&r](Call& c) {
                  Value v = Value::Obj();
                  v.Set("kind", Value::Str(c.host().kind()));
                  v.Set("editor", Value::Boolean(c.host().hasEditor()));
                  v.Set("playing", Value::Boolean(c.host().isPlaying()));
                  v.Set("project", Value::Str(c.host().projectDir()));
                  v.Set("scene", Value::Str(c.scene().Name()));
                  v.Set("objects", Value::Num((double)c.scene().Objects().size()));
                  v.Set("commands", Value::Num((double)r.Size()));
                  return Result::Ok(std::move(v));
              }));
}

// ---- api.* and script.* ------------------------------------------------------------------------------------------------

void AddApi(Registry& r) {
    r.Add(MakeCommand("api.functions", "List the engine functions every script language shares (the script API table), with parameters and documentation.",
              {Arg("filter", ParamType::String, "Only functions whose name, type or text contains this.")},
              "{count, functions: [{name, aliases, typed, params:[{name,type,optional}], returns, doc, pure, module}]}", 0, [](Call& c) {
                  const std::string f = c.Str("filter");
                  Value list = Value::Arr();
                  for (const ScriptApiEntry& e : ScriptApis()) {
                      const std::string typed = e.typedType.empty() ? std::string() : e.typedType + "." + e.typedName;
                      if (!Contains(e.name, f) && !Contains(typed, f) && !Contains(e.doc, f)) continue;
                      Value j = Value::Obj();
                      j.Set("name", Value::Str(e.name));
                      Value al = Value::Arr();
                      for (const auto& a : e.aliases) al.Push(Value::Str(a));
                      j.Set("aliases", std::move(al));
                      j.Set("typed", Value::Str(typed));
                      Value ps = Value::Arr();
                      for (const ScriptApiParam& p : e.params)
                          ps.Push(Value::Obj().Set("name", Value::Str(p.name)).Set("type", Value::Str(ApiTypeName(p.type))).Set("optional", Value::Boolean(p.optional)));
                      j.Set("params", std::move(ps));
                      j.Set("returns", Value::Str(e.typedType.empty() && e.params.empty() ? "any" : ApiTypeName(e.ret)));
                      j.Set("doc", Value::Str(e.doc));
                      j.Set("pure", Value::Boolean(e.pure));
                      if (!e.module.empty()) j.Set("module", Value::Str(e.module));
                      list.Push(std::move(j));
                  }
                  const double n = (double)list.array.size();
                  return Result::Ok(Value::Obj().Set("count", Value::Num(n)).Set("functions", std::move(list)));
              }));
    r.Add(MakeCommand("api.classes", "List the component classes scripts can hold (the generated script bindings), with method and property counts.",
              {Arg("filter", ParamType::String, "Only classes whose name contains this.")},
              "{count, classes: [{name, methods, properties, addable, category}]}", 0, [](Call& c) {
                  const std::string f = c.Str("filter");
                  Value list = Value::Arr();
                  for (const scriptbind::ClassBinding& k : scriptbind::AllClasses()) {
                      if (!Contains(k.name, f)) continue;
                      auto info = PropertyRegistry::FindByType(k.type);
                      Value j = Value::Obj();
                      j.Set("name", Value::Str(k.name));
                      j.Set("methods", Value::Num((double)k.methods.size()));
                      j.Set("properties", Value::Num(info ? (double)info->properties.size() : 0.0));
                      j.Set("addable", Value::Boolean(k.add != nullptr || (info && info->add)));
                      if (info && !info->category.empty()) j.Set("category", Value::Str(info->category));
                      list.Push(std::move(j));
                  }
                  const double n = (double)list.array.size();
                  return Result::Ok(Value::Obj().Set("count", Value::Num(n)).Set("classes", std::move(list)));
              }));
    r.Add(MakeCommand("script.call", "Call a shared script function by its OkayScript name, as a script would (the script API table: save_game, play_sound, find ...).",
              {Arg("function", ParamType::String, "The function's OkayScript name or alias (api.functions lists them).", true),
               ArgDefault("args", ParamType::Array, "The arguments in order: numbers, true/false, text, {x,y,z}, {\"object\": id}.", Value::Arr()),
               Arg("object", ParamType::String, "The object the call acts for (a script's own object); none when left out.")},
              "{value}", flag::Undoable | flag::AllowInPlay, [](Call& c) {
                  const ScriptApiEntry* e = FindScriptApi(c.Str("function"));
                  if (!e) return Result::Fail(code::NotFound, "There is no script function '" + c.Str("function") + "' (api.functions lists them).");
                  GameObject* self = nullptr;
                  if (c.Has("object")) {
                      Error err;
                      self = FindObject(c.scene(), c.Str("object"), err);
                      if (!self) return Fail(err);
                  }
                  std::vector<vs::VsValue> args;
                  if (const Value* a = c.Get("args")) for (const Value& j : a->array) {
                      vs::VsValue v;
                      std::string why;
                      if (!JsonToScriptValue(c.scene(), j, v, why)) return Result::Fail(code::InvalidParams, why);
                      args.push_back(std::move(v));
                  }
                  std::string why;
                  vs::VsValue out = CallScriptApiDynamic(*e, self, args, nullptr, &why);
                  if (!why.empty()) return Result::Fail(code::Failed, e->name + ": " + why);
                  return Result::Ok(Value::Obj().Set("value", ScriptValueToJson(out)));
              }, {code::NotFound, code::Failed}));
}

// ---- scene.* and object.inspect -----------------------------------------------------------------------------------------

void AddScene(Registry& r) {
    r.Add(MakeCommand("scene.info", "The open scene's name, size and settings: objects, roots, depth, components by type, gravity and the main camera.",
              {}, "{name, objects, roots, maxDepth, components, byType, gravity, gravity2d, mainCamera}", 0, [](Call& c) {
                  Scene& s = c.scene();
                  const SceneGraphStats st = s.Stats();
                  Value v = Value::Obj();
                  v.Set("name", Value::Str(s.Name()));
                  v.Set("objects", Value::Num(st.objects)).Set("roots", Value::Num(st.roots)).Set("maxDepth", Value::Num(st.maxDepth));
                  v.Set("components", Value::Num(st.components));
                  Value types = Value::Obj();
                  for (const auto& t : st.byType) types.Set(t.type, Value::Num(t.count));
                  v.Set("byType", std::move(types));
                  const Vec3 g = s.physics3D().gravity;
                  v.Set("gravity", Vec3Json(g.x, g.y, g.z));
                  v.Set("gravity2d", Value::Obj().Set("x", Value::Num(s.physics().gravity.x)).Set("y", Value::Num(s.physics().gravity.y)));
                  v.Set("mainCamera", s.mainCamera && s.mainCamera->gameObject ? Value::Str(s.mainCamera->gameObject->Guid()) : Value());
                  return Result::Ok(std::move(v));
              }));
    r.Add(MakeCommand("scene.hierarchy", "The scene's objects as a tree: id, name, path, components, and with deep also transforms.",
              {Arg("root", ParamType::String, "Start at this object instead of the scene's roots."),
               ArgDefault("depth", ParamType::Integer, "How many levels below the start (-1 = all).", Value::Num(-1)),
               ArgDefault("deep", ParamType::Boolean, "Also each object's transform, tag and layer.", Value::Boolean(false))},
              "{objects: [{id, name, path, active, components, children: [...]}]}", 0, [](Call& c) {
                  Value out = Value::Arr();
                  const int depth = (int)c.Int("depth", -1);
                  const bool deep = c.Bool("deep", false);
                  if (c.Has("root")) {
                      Error e;
                      GameObject* g = FindObject(c.scene(), c.Str("root"), e);
                      if (!g) return Fail(e);
                      HierarchyInto(*g, 0, depth, deep, out);
                  } else {
                      for (GameObject* g : c.scene().Roots()) if (g && !g->IsDestroyed() && !g->transient) HierarchyInto(*g, 0, depth, deep, out);
                  }
                  return Result::Ok(Value::Obj().Set("objects", std::move(out)));
              }, {code::NotFound}));
    r.Add(MakeCommand("scene.find", "Find objects by name (part of it), tag or component type.",
              {Arg("name", ParamType::String, "Objects whose name contains this (any case)."),
               Arg("tag", ParamType::String, "Objects with this tag."),
               Arg("component", ParamType::String, "Objects with a component of this type (\"Light\", \"Rigidbody3D\").")},
              "{count, objects: [{id, name, path, active, components}]}", 0, [](Call& c) {
                  TypeRef t;
                  const bool byType = c.Has("component");
                  if (byType && !FindType(c.Str("component"), t)) return Result::Fail(code::NotFound, "There is no component type '" + c.Str("component") + "'.");
                  Value out = Value::Arr();
                  for (const auto& up : c.scene().Objects()) {
                      const GameObject& g = *up;
                      if (g.IsDestroyed() || g.transient) continue;
                      if (c.Has("name") && !Contains(g.name, c.Str("name"))) continue;
                      if (c.Has("tag") && !g.CompareTag(c.Str("tag"))) continue;
                      if (byType && !FindOn(g, t, 0)) continue;
                      out.Push(ObjectSummary(g));
                  }
                  const double n = (double)out.array.size();
                  return Result::Ok(Value::Obj().Set("count", Value::Num(n)).Set("objects", std::move(out)));
              }, {code::NotFound}));
    r.Add(MakeCommand("scene.validate", "Check the open scene as the editor's Problems panel does: scene problems (broken links, missing assets, unknown tags) and every visual script graph.",
              {}, "{ok, issues: [text], graphIssues: [{object, graph, node, severity, message}]}", 0, [](Call& c) {
                  const std::string root = c.host().projectDir();
                  const SceneValidationReport rep = c.scene().Validate(root);
                  Value issues = Value::Arr();
                  std::string text = rep.ToString();
                  std::size_t at = 0;
                  while (at < text.size()) {
                      std::size_t e = text.find('\n', at);
                      if (e == std::string::npos) e = text.size();
                      if (e > at) issues.Push(Value::Str(text.substr(at, e - at)));
                      at = e + 1;
                  }
                  Value graphs = Value::Arr();
                  int errors = 0;
                  for (const vs::SceneGraphIssue& g : vs::ValidateSceneGraphs(c.scene(), root)) {
                      if (g.issue.severity == 0) ++errors;
                      graphs.Push(Value::Obj().Set("object", Value::Str(g.object)).Set("graph", Value::Str(g.graph))
                                      .Set("node", Value::Num(g.issue.node)).Set("severity", Value::Str(g.issue.severity == 0 ? "error" : "warning"))
                                      .Set("message", Value::Str(g.Describe())));
                  }
                  return Result::Ok(Value::Obj().Set("ok", Value::Boolean(rep.Ok() && errors == 0)).Set("issues", std::move(issues)).Set("graphIssues", std::move(graphs)));
              }));
    r.Add(MakeCommand("object.inspect", "Everything about one object: transform, tag, layer, parent, children, and every component with its properties' values.",
              {kObject}, "{id, name, path, active, position, rotation, scale, worldPosition, parent, children, tag, layer, components: [{type, enabled, properties}]}",
              0, [](Call& c) {
                  Error e;
                  GameObject* g = FindObject(c.scene(), c.Str("object"), e);
                  if (!g) return Fail(e);
                  Value v = ObjectSummary(*g, true);
                  Value comps = Value::Arr();
                  for (const auto& k : g->Components()) if (k) comps.Push(ComponentJson(*k, true));
                  v.Set("components", std::move(comps));
                  if (!g->prefabSource.empty()) v.Set("prefab", Value::Str(g->prefabSource));
                  return Result::Ok(std::move(v));
              }, {code::NotFound, code::Ambiguous}));
}

// ---- component.* -----------------------------------------------------------------------------------------------------

void AddComponents(Registry& r) {
    r.Add(MakeCommand("component.types", "List the component types (the property table the Inspector uses, joined with the script bindings).",
              {Arg("filter", ParamType::String, "Only types whose name or category contains this.")},
              "{count, types: [{name, record, category, description, addable, properties, methods}]}", 0, [](Call& c) {
                  const std::string f = c.Str("filter");
                  Value list = Value::Arr();
                  for (const auto& info : PropertyRegistry::All()) {
                      if (!info || info->objectLevel) continue;
                      if (!Contains(info->name, f) && !Contains(info->category, f)) continue;
                      TypeRef t;
                      FindType(info->name, t);
                      Value j = Value::Obj();
                      j.Set("name", Value::Str(info->name));
                      j.Set("record", Value::Str(info->recordName));
                      if (!info->category.empty()) j.Set("category", Value::Str(info->category));
                      if (!info->description.empty()) j.Set("description", Value::Str(info->description));
                      j.Set("addable", Value::Boolean(info->add != nullptr || (t.cls && t.cls->add)));
                      j.Set("properties", Value::Num((double)info->properties.size()));
                      j.Set("methods", Value::Num(t.cls ? (double)t.cls->methods.size() : 0.0));
                      list.Push(std::move(j));
                  }
                  const double n = (double)list.array.size();
                  return Result::Ok(Value::Obj().Set("count", Value::Num(n)).Set("types", std::move(list)));
              }));
    r.Add(MakeCommand("component.describe", "Describe a component type: every property (type, range, options, read-only) and every method scripts can call.",
              {Arg("type", ParamType::String, "The component type (\"Light\", \"rigidbody3d\", \"Rigidbody\").", true)},
              "{name, category, description, properties: [{name, type, min, max, options, readOnly, default}], methods: [{name, params, required, returns}]}",
              0, [](Call& c) {
                  TypeRef t;
                  if (!FindType(c.Str("type"), t)) return Result::Fail(code::NotFound, "There is no component type '" + c.Str("type") + "' (component.types lists them).");
                  Value v = Value::Obj();
                  v.Set("name", Value::Str(t.name));
                  Value props = Value::Arr();
                  if (t.info) {
                      if (!t.info->category.empty()) v.Set("category", Value::Str(t.info->category));
                      if (!t.info->description.empty()) v.Set("description", Value::Str(t.info->description));
                      for (const PropertyDesc& d : t.info->properties) if (!d.IsHidden()) props.Push(PropertyDescJson(d));
                  }
                  v.Set("properties", std::move(props));
                  Value methods = Value::Arr();
                  if (t.cls) for (const auto& m : t.cls->methods) methods.Push(MethodJson(m));
                  v.Set("methods", std::move(methods));
                  return Result::Ok(std::move(v));
              }, {code::NotFound}));
    r.Add(MakeCommand("component.get", "Read a property by its path on an object, as the Inspector shows it (\"Light.intensity\", \"Transform.position\", \"Camera#1.depth\", \"GameObject.tag\").",
              {kObject, Arg("path", ParamType::String, "Component.property, optionally Component#n.property and .x/.y/.z after a vector.", true)},
              "{path, type, value, text}", 0, [](Call& c) {
                  Error e;
                  GameObject* g = FindObject(c.scene(), c.Str("object"), e);
                  if (!g) return Fail(e);
                  PropertyValue v;
                  std::string why;
                  if (!PropertyRegistry::TryGetProperty(*g, c.Str("path"), v, &why)) return Result::Fail(code::NotFound, why.empty() ? "That property path does not resolve." : why);
                  return Result::Ok(Value::Obj().Set("path", Value::Str(c.Str("path"))).Set("type", Value::Str(PropertyTypeName(v.Type())))
                                        .Set("value", PropertyToJson(v)).Set("text", Value::Str(v.ToString())));
              }, {code::NotFound, code::Ambiguous}));
    r.Add(MakeCommand("component.set", "Set a property by its path, through the property table (checked, clamped to its range, change hooks fired), as one undo step.",
              {kObject, Arg("path", ParamType::String, "Component.property, as component.get takes.", true),
               Arg("value", ParamType::Any, "The new value: a number, true/false, text (also an enum option name), {x,y,z}, {r,g,b,a} or [..].", true)},
              "{path, old, value, changed}", flag::Undoable, [](Call& c) {
                  Error e;
                  GameObject* g = FindObject(c.scene(), c.Str("object"), e);
                  if (!g) return Fail(e);
                  std::string text;
                  if (!JsonToPropertyText(*c.Get("value"), text)) return Result::Fail(code::InvalidParams, "That value has no property form; give a number, true/false, text, {x,y,z} or {r,g,b,a}.");
                  const PropertyWriteResult w = PropertyRegistry::WritePropertyFromText(*g, c.Str("path"), text);
                  if (!w.ok) return Result::Fail(code::Failed, w.error.empty() ? "The property refused the value." : w.error);
                  return Result::Ok(Value::Obj().Set("path", Value::Str(w.path)).Set("old", PropertyToJson(w.oldValue))
                                        .Set("value", PropertyToJson(w.newValue)).Set("changed", Value::Boolean(w.changed)));
              }, {code::NotFound, code::Failed}));
    r.Add(MakeCommand("component.call", "Call a method of a component, through the generated script bindings (the same table OkayScript, C# and visual scripts use).",
              {kObject, Arg("component", ParamType::String, "The component type (\"Rigidbody3D\", \"Light\").", true),
               Arg("method", ParamType::String, "The method (component.describe lists them).", true),
               ArgDefault("args", ParamType::Array, "The arguments: numbers, true/false, text, {x,y,z}, {r,g,b,a}, {\"object\": id}.", Value::Arr()),
               ArgDefault("index", ParamType::Integer, "Which component of that type on the object (0 = the first).", Value::Num(0))},
              "{value}", flag::Undoable | flag::AllowInPlay, [](Call& c) {
                  Error e;
                  GameObject* g = FindObject(c.scene(), c.Str("object"), e);
                  if (!g) return Fail(e);
                  TypeRef t;
                  if (!FindType(c.Str("component"), t) || !t.cls) return Result::Fail(code::NotFound, "Scripts cannot hold a component type '" + c.Str("component") + "' (api.classes lists them).");
                  Component* comp = FindOn(*g, t, (int)c.Int("index", 0));
                  if (!comp) return Result::Fail(code::NotFound, g->name + " has no " + t.name + " component.");
                  std::vector<vs::VsValue> args;
                  if (const Value* a = c.Get("args")) for (const Value& j : a->array) {
                      vs::VsValue v;
                      std::string why;
                      if (!JsonToScriptValue(c.scene(), j, v, why)) return Result::Fail(code::InvalidParams, why);
                      args.push_back(std::move(v));
                  }
                  vs::VsValue out;
                  std::string why;
                  const scriptbind::Result res = scriptbind::CallOn(*comp, *t.cls, c.Str("method"), args, out, why);
                  if (res == scriptbind::Result::NotFound) return Result::Fail(code::NotFound, t.name + " has no method '" + c.Str("method") + "' (component.describe lists them).");
                  if (res == scriptbind::Result::Error) return Result::Fail(code::Failed, why);
                  return Result::Ok(Value::Obj().Set("value", ScriptValueToJson(out)));
              }, {code::NotFound, code::Failed}));
    r.Add(MakeCommand("component.add", "Add a component to an object, as the Add Component menu does (required components come with it), as one undo step.",
              {kObject, Arg("type", ParamType::String, "The component type (component.types lists the addable ones).", true)},
              "{object, type, index}", flag::Undoable, [](Call& c) {
                  Error e;
                  GameObject* g = FindObject(c.scene(), c.Str("object"), e);
                  if (!g) return Fail(e);
                  TypeRef t;
                  if (!FindType(c.Str("type"), t)) return Result::Fail(code::NotFound, "There is no component type '" + c.Str("type") + "'.");
                  Component* added = nullptr;
                  if (t.info && t.info->add) added = t.info->add(*g);
                  else if (t.cls && t.cls->add) added = t.cls->add(*g);
                  else return Result::Fail(code::Refused, t.name + " cannot be added by hand.");
                  if (!added) return Result::Fail(code::Failed, t.name + " was not added (an object may hold only one, or it needs something this object lacks).");
                  int index = 0;
                  for (const auto& k : g->Components()) { if (k.get() == added) break; if (k && FindOn(*g, t, index) == k.get()) ++index; }
                  return Result::Ok(Value::Obj().Set("object", Value::Str(g->Guid())).Set("type", Value::Str(t.name)).Set("index", Value::Num(index)));
              }, {code::NotFound, code::Failed}));
    r.Add(MakeCommand("component.remove", "Remove a component from an object (refused when another component needs it), as one undo step.",
              {kObject, Arg("type", ParamType::String, "The component type.", true),
               ArgDefault("index", ParamType::Integer, "Which component of that type (0 = the first).", Value::Num(0))},
              "{object, type, removed}", flag::Undoable, [](Call& c) {
                  Error e;
                  GameObject* g = FindObject(c.scene(), c.Str("object"), e);
                  if (!g) return Fail(e);
                  TypeRef t;
                  if (!FindType(c.Str("type"), t)) return Result::Fail(code::NotFound, "There is no component type '" + c.Str("type") + "'.");
                  Component* comp = FindOn(*g, t, (int)c.Int("index", 0));
                  if (!comp) return Result::Fail(code::NotFound, g->name + " has no " + t.name + " component.");
                  if (comp == g->transform) return Result::Fail(code::Refused, "The Transform cannot be removed.");
                  if (Component* need = g->RequiredBy(comp)) {
                      const scriptbind::ClassBinding* nc = scriptbind::ClassOf(*need);
                      return Result::Fail(code::Refused, t.name + " is needed by " + (nc ? nc->name : std::string("another component")) + " on this object.");
                  }
                  if (!g->RemoveComponent(comp)) return Result::Fail(code::Failed, t.name + " could not be removed.");
                  return Result::Ok(Value::Obj().Set("object", Value::Str(g->Guid())).Set("type", Value::Str(t.name)).Set("removed", Value::Boolean(true)));
              }, {code::NotFound, code::Refused, code::Failed}));
}

// ---- transform.* -------------------------------------------------------------------------------------------------------

void AddTransform(Registry& r) {
    r.Add(MakeCommand("transform.get", "An object's position, rotation (Euler degrees) and scale, local and world.",
              {kObject}, "{position, rotation, scale, worldPosition, worldRotation, worldScale, parent}", 0, [](Call& c) {
                  Error e;
                  GameObject* g = FindObject(c.scene(), c.Str("object"), e);
                  if (!g || !g->transform) return Fail(e);
                  const Transform& t = *g->transform;
                  const Vec3 le = t.LocalEulerAngles(), wp = t.Position(), we = t.EulerAngles(), ws = t.LossyScale();
                  Value v = Value::Obj();
                  v.Set("position", Vec3Json(t.localPosition.x, t.localPosition.y, t.localPosition.z));
                  v.Set("rotation", Vec3Json(le.x, le.y, le.z));
                  v.Set("scale", Vec3Json(t.localScale.x, t.localScale.y, t.localScale.z));
                  v.Set("worldPosition", Vec3Json(wp.x, wp.y, wp.z));
                  v.Set("worldRotation", Vec3Json(we.x, we.y, we.z));
                  v.Set("worldScale", Vec3Json(ws.x, ws.y, ws.z));
                  v.Set("parent", t.Parent() && t.Parent()->gameObject ? Value::Str(t.Parent()->gameObject->Guid()) : Value());
                  return Result::Ok(std::move(v));
              }, {code::NotFound, code::Ambiguous}));
    r.Add(MakeCommand("transform.set", "Move, turn or scale an object (any of position, rotation in Euler degrees, scale), locally or in world space, as one undo step.",
              {kObject, Arg("position", ParamType::Vec3, "The new position."), Arg("rotation", ParamType::Vec3, "The new rotation, Euler angles in degrees."),
               Arg("scale", ParamType::Vec3, "The new scale."), ArgChoice("space", {"local", "world"}, "local: relative to the parent; world: in the scene.", "local")},
              "{position, rotation, scale} (local, after the change)", flag::Undoable, [](Call& c) {
                  Error e;
                  GameObject* g = FindObject(c.scene(), c.Str("object"), e);
                  if (!g || !g->transform) return Fail(e);
                  if (!c.Has("position") && !c.Has("rotation") && !c.Has("scale")) return Result::Fail(code::InvalidParams, "Give position, rotation or scale.");
                  Transform& t = *g->transform;
                  const bool world = c.Str("space", "local") == "world";
                  float f[3];
                  if (c.Has("position")) { c.Vec("position", f); if (world) t.SetPosition(Vec3{f[0], f[1], f[2]}); else t.localPosition = Vec3{f[0], f[1], f[2]}; }
                  if (c.Has("rotation")) { c.Vec("rotation", f); if (world) t.SetEulerAngles(Vec3{f[0], f[1], f[2]}); else t.SetLocalEulerAngles(Vec3{f[0], f[1], f[2]}); }
                  if (c.Has("scale")) { c.Vec("scale", f); if (world) t.SetLossyScale(Vec3{f[0], f[1], f[2]}); else t.localScale = Vec3{f[0], f[1], f[2]}; }
                  const Vec3 le = t.LocalEulerAngles();
                  return Result::Ok(Value::Obj().Set("position", Vec3Json(t.localPosition.x, t.localPosition.y, t.localPosition.z))
                                        .Set("rotation", Vec3Json(le.x, le.y, le.z)).Set("scale", Vec3Json(t.localScale.x, t.localScale.y, t.localScale.z)));
              }, {code::NotFound, code::Ambiguous}));
}

// ---- physics.* ---------------------------------------------------------------------------------------------------------

void AddPhysics(Registry& r) {
    r.Add(MakeCommand("physics.raycast", "Cast a ray through the scene's colliders (3D, or 2D) and say what it hits first, or every hit.",
              {Arg("origin", ParamType::Vec3, "Where the ray starts.", true), Arg("direction", ParamType::Vec3, "Which way it goes (any length).", true),
               ArgDefault("maxDistance", ParamType::Number, "How far it reaches.", Value::Num(1000)),
               ArgChoice("mode", {"3d", "2d"}, "3d: Physics3D (colliders and terrain); 2d: Physics2D (x and y only).", "3d"),
               ArgDefault("all", ParamType::Boolean, "Every hit, nearest first, instead of the first.", Value::Boolean(false)),
               ArgDefault("triggers", ParamType::Boolean, "Count trigger colliders.", Value::Boolean(true))},
              "{hit, object, name, point, normal, distance} or {hits: [...]}", 0, [](Call& c) {
                  float o[3], d[3];
                  c.Vec("origin", o);
                  c.Vec("direction", d);
                  const float maxD = (float)c.Num("maxDistance", 1000);
                  const bool trig = c.Bool("triggers", true);
                  const bool all = c.Bool("all", false);
                  Scene& s = c.scene();
                  if (c.Str("mode", "3d") == "2d") {
                      const Vec2 org{o[0], o[1]};
                      Vec2 dir{d[0], d[1]};
                      const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                      if (!(len > 1e-6f)) return Result::Fail(code::InvalidParams, "The direction has no length in x and y.");
                      dir = Vec2{dir.x / len, dir.y / len};
                      auto one = [](const RaycastHit2D& h) {
                          return HitJson((bool)h, h.gameObject, Vec3{h.point.x, h.point.y, 0}, Vec3{h.normal.x, h.normal.y, 0}, h.distance);
                      };
                      if (all) {
                          Value hits = Value::Arr();
                          for (const RaycastHit2D& h : s.physics().RaycastAll(s, org, dir, maxD, nullptr, Physics2D::kAllLayers, trig)) hits.Push(one(h));
                          return Result::Ok(Value::Obj().Set("hits", std::move(hits)));
                      }
                      return Result::Ok(one(s.physics().Raycast(s, org, dir, maxD, nullptr, Physics2D::kAllLayers, trig)));
                  }
                  const Vec3 dir{d[0], d[1], d[2]};
                  if (!(dir.Magnitude() > 1e-6f)) return Result::Fail(code::InvalidParams, "The direction has no length.");
                  const Vec3 n = dir / dir.Magnitude();
                  auto one = [](const RaycastHit3D& h) { return HitJson(h.hit, h.gameObject, h.point, h.normal, h.distance); };
                  if (all) {
                      Value hits = Value::Arr();
                      for (const RaycastHit3D& h : s.physics3D().RaycastAll(s, Vec3{o[0], o[1], o[2]}, n, maxD, nullptr, Physics3D::kAllLayers, trig)) hits.Push(one(h));
                      return Result::Ok(Value::Obj().Set("hits", std::move(hits)));
                  }
                  return Result::Ok(one(s.physics3D().Raycast(s, Vec3{o[0], o[1], o[2]}, n, maxD, nullptr, Physics3D::kAllLayers, trig)));
              }));
    r.Add(MakeCommand("physics.overlap", "List the 3D colliders inside a sphere.",
              {Arg("center", ParamType::Vec3, "The sphere's centre.", true), Arg("radius", ParamType::Number, "Its radius.", true)},
              "{count, objects: [{id, name}]}", 0, [](Call& c) {
                  float p[3];
                  c.Vec("center", p);
                  const float rad = (float)c.Num("radius", 1);
                  if (!(rad > 0.0f)) return Result::Fail(code::InvalidParams, "The radius must be above 0.");
                  Value out = Value::Arr();
                  for (Collider3D* col : c.scene().physics3D().OverlapSphere(c.scene(), Vec3{p[0], p[1], p[2]}, rad))
                      if (col && col->gameObject) out.Push(Value::Obj().Set("id", Value::Str(col->gameObject->Guid())).Set("name", Value::Str(col->gameObject->name)));
                  const double n = (double)out.array.size();
                  return Result::Ok(Value::Obj().Set("count", Value::Num(n)).Set("objects", std::move(out)));
              }));
    r.Add(MakeCommand("physics.settings", "Read the scene's gravity (3D and 2D) and physics rate; give gravity or gravity2d to change them (saved with the scene, one undo step).",
              {Arg("gravity", ParamType::Vec3, "The 3D gravity (m/s^2)."), Arg("gravity2d", ParamType::Vec3, "The 2D gravity (x and y)."),
               Arg("stepsPerSecond", ParamType::Number, "Fixed physics steps per second while playing.")},
              "{gravity, gravity2d, stepsPerSecond, smooth}", flag::Undoable, [](Call& c) {
                  Scene& s = c.scene();
                  float f[3];
                  if (c.Has("gravity")) { c.Vec("gravity", f); s.physics3D().gravity = Vec3{f[0], f[1], f[2]}; }
                  if (c.Has("gravity2d")) { c.Vec("gravity2d", f); s.physics().gravity = Vec2{f[0], f[1]}; }
                  if (c.Has("stepsPerSecond")) {
                      const double sps = c.Num("stepsPerSecond", 50);
                      if (!(sps >= 1 && sps <= 1000)) return Result::Fail(code::InvalidParams, "stepsPerSecond must be 1 to 1000.");
                      s.SetPhysicsStepsPerSecond((float)sps);
                  }
                  const Vec3 g = s.physics3D().gravity;
                  return Result::Ok(Value::Obj().Set("gravity", Vec3Json(g.x, g.y, g.z))
                                        .Set("gravity2d", Value::Obj().Set("x", Value::Num(s.physics().gravity.x)).Set("y", Value::Num(s.physics().gravity.y)))
                                        .Set("stepsPerSecond", Value::Num(s.PhysicsStepsPerSecond())).Set("smooth", Value::Boolean(s.SmoothPhysics())));
              }));
    r.Add(MakeCommand("physics.bodies", "List the rigid bodies (3D and 2D) with their position, velocity and mass: watch them while play.step runs.",
              {}, "{bodies: [{id, name, kind, position, velocity, mass}]}", 0, [](Call& c) {
                  Value out = Value::Arr();
                  for (Rigidbody3D* b : c.scene().FindObjectsOfType<Rigidbody3D>()) {
                      if (!b || !b->gameObject || !b->transform) continue;
                      const Vec3 p = b->transform->Position();
                      out.Push(Value::Obj().Set("id", Value::Str(b->gameObject->Guid())).Set("name", Value::Str(b->gameObject->name)).Set("kind", Value::Str("3d"))
                                   .Set("position", Vec3Json(p.x, p.y, p.z)).Set("velocity", Vec3Json(b->velocity.x, b->velocity.y, b->velocity.z))
                                   .Set("mass", Value::Num(b->mass)));
                  }
                  for (Rigidbody2D* b : c.scene().FindObjectsOfType<Rigidbody2D>()) {
                      if (!b || !b->gameObject || !b->transform) continue;
                      const Vec3 p = b->transform->Position();
                      out.Push(Value::Obj().Set("id", Value::Str(b->gameObject->Guid())).Set("name", Value::Str(b->gameObject->name)).Set("kind", Value::Str("2d"))
                                   .Set("position", Vec3Json(p.x, p.y, p.z)).Set("velocity", Vec3Json(b->velocity.x, b->velocity.y, 0))
                                   .Set("mass", Value::Num(b->mass)));
                  }
                  return Result::Ok(Value::Obj().Set("bodies", std::move(out)));
              }));
}

// ---- anim.* ------------------------------------------------------------------------------------------------------------

void AddAnimation(Registry& r) {
    r.Add(MakeCommand("anim.list", "List the animation players in the scene: clip players (Animator: tracks, length, time) and state graphs (AnimStateMachine: states, the current one, parameters).",
              {}, "{clips: [{object, tracks, length, time, playing}], graphs: [{object, states, current, params}]}", 0, [](Call& c) {
                  Value clips = Value::Arr(), graphs = Value::Arr();
                  for (Animator* a : c.scene().FindObjectsOfType<Animator>()) {
                      if (!a || !a->gameObject) continue;
                      Value tracks = Value::Arr();
                      std::vector<std::string> names;
                      for (const auto& kv : a->clip.Tracks()) names.push_back(kv.first);
                      std::sort(names.begin(), names.end());
                      for (const auto& n : names) tracks.Push(Value::Str(n));
                      clips.Push(Value::Obj().Set("object", Value::Str(a->gameObject->Guid())).Set("name", Value::Str(a->gameObject->name))
                                     .Set("tracks", std::move(tracks)).Set("length", Value::Num(a->clip.Length())).Set("time", Value::Num(a->Time()))
                                     .Set("playing", Value::Boolean(a->playing)).Set("speed", Value::Num(a->speed)));
                  }
                  for (AnimStateMachine* m : c.scene().FindObjectsOfType<AnimStateMachine>()) {
                      if (!m || !m->gameObject) continue;
                      Value states = Value::Arr(), params = Value::Arr();
                      for (const auto& s : m->states) states.Push(Value::Obj().Set("name", Value::Str(s.name)).Set("clip", Value::Str(s.clip)));
                      static const char* kKinds[] = {"float", "bool", "trigger", "int"};
                      for (const auto& p : m->params) {
                          Value pj = Value::Obj().Set("name", Value::Str(p.name)).Set("type", Value::Str(p.type >= 0 && p.type < 4 ? kKinds[p.type] : "float"));
                          if (p.type == 1) pj.Set("value", Value::Boolean(m->GetBool(p.name)));
                          else if (p.type != 2) pj.Set("value", Value::Num(m->GetFloat(p.name)));
                          params.Push(std::move(pj));
                      }
                      graphs.Push(Value::Obj().Set("object", Value::Str(m->gameObject->Guid())).Set("name", Value::Str(m->gameObject->name))
                                      .Set("states", std::move(states)).Set("current", Value::Str(m->Current())).Set("params", std::move(params)));
                  }
                  return Result::Ok(Value::Obj().Set("clips", std::move(clips)).Set("graphs", std::move(graphs)));
              }));
    r.Add(MakeCommand("anim.sample", "Pose an object's clip player at a time (Animator::SetTime) and return the transform it gives.",
              {kObject, Arg("time", ParamType::Number, "Seconds into the clip.", true)},
              "{time, position, rotation, scale}", flag::Undoable | flag::AllowInPlay, [](Call& c) {
                  Error e;
                  GameObject* g = FindObject(c.scene(), c.Str("object"), e);
                  if (!g) return Fail(e);
                  Animator* a = g->GetComponent<Animator>();
                  if (!a) return Result::Fail(code::NotFound, g->name + " has no clip player (Animator).");
                  a->SetTime((float)c.Num("time", 0));
                  const Transform& t = *g->transform;
                  const Vec3 le = t.LocalEulerAngles();
                  return Result::Ok(Value::Obj().Set("time", Value::Num(a->Time())).Set("position", Vec3Json(t.localPosition.x, t.localPosition.y, t.localPosition.z))
                                        .Set("rotation", Vec3Json(le.x, le.y, le.z)).Set("scale", Vec3Json(t.localScale.x, t.localScale.y, t.localScale.z)));
              }, {code::NotFound}));
    r.Add(MakeCommand("anim.set_key", "Add or move a key on a clip player's track (\"position.x\", \"rotation.z\", \"scale.y\" ...), as one undo step; adds the Animator when the object has none.",
              {kObject, Arg("track", ParamType::String, "The track.", true), Arg("time", ParamType::Number, "Seconds.", true),
               Arg("value", ParamType::Number, "The value at that time.", true)},
              "{track, keys, length}", flag::Undoable, [](Call& c) {
                  Error e;
                  GameObject* g = FindObject(c.scene(), c.Str("object"), e);
                  if (!g) return Fail(e);
                  const double t = c.Num("time", 0), v = c.Num("value", 0);
                  if (!(t >= 0 && std::isfinite(t)) || !std::isfinite(v)) return Result::Fail(code::InvalidParams, "time must be 0 or more and value a finite number.");
                  Animator* a = g->GetOrAddComponent<Animator>();
                  if (!a) return Result::Fail(code::Failed, "A clip player could not be added to " + g->name + ".");
                  a->clip.AddKey(c.Str("track"), (float)t, (float)v);
                  const AnimationCurve* curve = a->clip.Track(c.Str("track"));
                  return Result::Ok(Value::Obj().Set("track", Value::Str(c.Str("track"))).Set("keys", Value::Num(curve ? (double)curve->Count() : 0.0))
                                        .Set("length", Value::Num(a->clip.Length())));
              }, {code::NotFound}));
    r.Add(MakeCommand("anim.set_param", "Set a state graph's parameter (float, int, bool or trigger) or jump to a state, as the game's scripts do.",
              {kObject, Arg("param", ParamType::String, "The parameter to set."), Arg("value", ParamType::Any, "Its value: a number or true/false (a trigger needs none)."),
               Arg("state", ParamType::String, "Jump to this state instead (AnimStateMachine::GoTo).")},
              "{current, param, value}", flag::Mutates | flag::AllowInPlay, [](Call& c) {
                  Error e;
                  GameObject* g = FindObject(c.scene(), c.Str("object"), e);
                  if (!g) return Fail(e);
                  AnimStateMachine* m = g->GetComponent<AnimStateMachine>();
                  if (!m) return Result::Fail(code::NotFound, g->name + " has no state graph (AnimStateMachine).");
                  if (c.Has("state")) {
                      if (!m->GoTo(c.Str("state"))) return Result::Fail(code::NotFound, "The graph has no state '" + c.Str("state") + "'.");
                      return Result::Ok(Value::Obj().Set("current", Value::Str(m->Current())));
                  }
                  if (!c.Has("param")) return Result::Fail(code::InvalidParams, "Give param (with value) or state.");
                  const std::string p = c.Str("param");
                  int kind = -1;
                  for (const auto& d : m->params) if (d.name == p) kind = d.type;
                  if (kind < 0) return Result::Fail(code::NotFound, "The graph has no parameter '" + p + "'.");
                  const Value* v = c.Get("value");
                  if (kind == 2) m->SetTrigger(p);
                  else if (kind == 1) { if (!v || !v->IsBool()) return Result::Fail(code::InvalidParams, p + " is a bool: give true or false."); m->SetBool(p, v->boolean); }
                  else { if (!v || !v->IsNumber()) return Result::Fail(code::InvalidParams, p + " is a number: give one."); if (kind == 3) m->SetInt(p, (int)v->number); else m->SetFloat(p, (float)v->number); }
                  return Result::Ok(Value::Obj().Set("current", Value::Str(m->Current())).Set("param", Value::Str(p))
                                        .Set("value", kind == 1 ? Value::Boolean(m->GetBool(p)) : kind == 2 ? Value::Boolean(true) : Value::Num(m->GetFloat(p))));
              }, {code::NotFound}));
}

// ---- viewport.* ------------------------------------------------------------------------------------------------------

void AddViewport(Registry& r) {
    r.Add(MakeCommand("viewport.screenshot", "Draw the scene with the engine's software renderer (meshes and lights; no sprites, UI or sky) to a PNG: from the main camera, a named camera object, the editor's Scene view, or a given eye and target.",
              {ArgDefault("width", ParamType::Integer, "Picture width in pixels (1 to 4096).", Value::Num(480)),
               ArgDefault("height", ParamType::Integer, "Picture height in pixels (1 to 4096).", Value::Num(270)),
               Arg("camera", ParamType::String, "An object with a Camera to draw from (default: the scene's main camera)."),
               ArgChoice("view", {"camera", "editor", "custom"}, "camera: a scene camera; editor: the editor's Scene view camera; custom: eye and target.", "camera"),
               Arg("eye", ParamType::Vec3, "custom: where the view is from."), Arg("target", ParamType::Vec3, "custom: what it looks at."),
               ArgDefault("fov", ParamType::Number, "custom / editor: vertical field of view in degrees.", Value::Num(60)),
               ArgDefault("supersample", ParamType::Integer, "Draw this many times larger and average down (1 to 4) for smoother edges.", Value::Num(1)),
               Arg("path", ParamType::String, "Where to write the PNG (default: a file in the system's temporary folder)."),
               ArgDefault("inline", ParamType::Boolean, "Also return the picture itself (MCP image content).", Value::Boolean(false))},
              "{path, width, height, bytes, drawnPixels}", 0, [](Call& c) {
                  testing::SceneRenderOptions opt;
                  opt.width = (int)c.Int("width", 480);
                  opt.height = (int)c.Int("height", 270);
                  if (opt.width < 1 || opt.width > 4096 || opt.height < 1 || opt.height > 4096) return Result::Fail(code::InvalidParams, "width and height must be 1 to 4096.");
                  opt.supersample = std::max(1, std::min(4, (int)c.Int("supersample", 1)));
                  const std::string view = c.Str("view", "camera");
                  if (view == "camera" && c.Has("camera")) {
                      Error e;
                      GameObject* g = FindObject(c.scene(), c.Str("camera"), e);
                      if (!g) return Fail(e);
                      opt.camera = g->GetComponent<Camera>();
                      if (!opt.camera) return Result::Fail(code::NotFound, g->name + " has no Camera.");
                  } else if (view == "editor" || view == "custom") {
                      float eye[3], target[3];
                      bool ortho = false;
                      float size = 5.0f;
                      if (view == "editor") {
                          if (!c.host().EditorView(eye, target, ortho, size)) return Result::Fail(code::Unavailable, "This host has no editor view; use view camera or custom.");
                      } else {
                          if (!c.Has("eye") || !c.Has("target")) return Result::Fail(code::InvalidParams, "view custom needs eye and target.");
                          c.Vec("eye", eye);
                          c.Vec("target", target);
                      }
                      opt.view.use = true;
                      opt.view.eye = Vec3{eye[0], eye[1], eye[2]};
                      opt.view.target = Vec3{target[0], target[1], target[2]};
                      opt.view.fovDegrees = (float)c.Num("fov", 60);
                      opt.view.orthographic = ortho;
                      opt.view.orthographicSize = size;
                  }
                  Image img;
                  std::string why;
                  if (!testing::RenderSceneToImage(c.scene(), img, opt, &why)) return Result::Fail(code::Failed, why);
                  // How much of the picture the scene drew (not the background): a quick "is anything there" for an agent.
                  const std::uint8_t* px = img.Data();
                  const std::uint8_t bg[3] = {(std::uint8_t)(opt.background.r * 255.0f + 0.5f), (std::uint8_t)(opt.background.g * 255.0f + 0.5f), (std::uint8_t)(opt.background.b * 255.0f + 0.5f)};
                  std::size_t drawn = 0;
                  for (int i = 0; i < img.Width() * img.Height(); ++i)
                      if (std::abs(px[i * 4] - bg[0]) > 2 || std::abs(px[i * 4 + 1] - bg[1]) > 2 || std::abs(px[i * 4 + 2] - bg[2]) > 2) ++drawn;
                  const std::vector<std::uint8_t> png = ImageWrite::EncodePNG(img.Width(), img.Height(), 4, img.Data());
                  if (png.empty()) return Result::Fail(code::Failed, "The picture could not be encoded as PNG.");
                  std::string path = c.Str("path");
                  std::error_code ec;
                  if (path.empty()) {
                      static int shot = 0;
                      path = (std::filesystem::temp_directory_path(ec) / ("okay_automation_shot_" + std::to_string(++shot) + ".png")).string();
                  }
                  {
                      std::ofstream f(path, std::ios::binary | std::ios::trunc);
                      f.write((const char*)png.data(), (std::streamsize)png.size());
                      if (!f) return Result::Fail(code::Failed, "The picture could not be written to " + path + ".");
                  }
                  Result res = Result::Ok(Value::Obj().Set("path", Value::Str(path)).Set("width", Value::Num(img.Width())).Set("height", Value::Num(img.Height()))
                                              .Set("bytes", Value::Num((double)png.size())).Set("drawnPixels", Value::Num((double)drawn)));
                  if (c.Bool("inline", false)) res.attachments.push_back({"image/png", base64::Encode(png)});
                  return res;
              }, {code::NotFound, code::Failed}));
}

// ---- log.* ----------------------------------------------------------------------------------------------------------

void AddLog(Registry& r) {
    r.Add(MakeCommand("log.read", "Read the engine log's newest lines (errors, warnings, script prints), or only those after a line number already read.",
              {ArgDefault("after", ParamType::Integer, "Only lines numbered above this (the last number you read).", Value::Num(0)),
               ArgDefault("max", ParamType::Integer, "At most this many (the newest).", Value::Num(200)),
               ArgChoice("level", {"trace", "info", "warning", "error"}, "The lowest level to include.", "info")},
              "{last, lines: [{n, level, text}]}", 0, [](Call& c) {
                  if (!LogRingInstalled()) return Result::Fail(code::Unavailable, "This host does not keep the log.");
                  const std::string lv = c.Str("level", "info");
                  const int minLevel = lv == "trace" ? (int)Log::Level::Trace : lv == "warning" ? (int)Log::Level::Warning
                                     : lv == "error" ? (int)Log::Level::Error : (int)Log::Level::Info;
                  static const char* kNames[] = {"trace", "info", "warning", "error"};   // Log::Level's order
                  Value lines = Value::Arr();
                  std::uint64_t last = (std::uint64_t)std::max<long long>(0, c.Int("after", 0));
                  for (const LogLine& l : LogSince(last, (std::size_t)std::max<long long>(1, c.Int("max", 200)), minLevel)) {
                      lines.Push(Value::Obj().Set("n", Value::Num((double)l.seq)).Set("level", Value::Str(l.level >= 0 && l.level < 4 ? kNames[l.level] : "info"))
                                     .Set("text", Value::Str(l.text)));
                      last = l.seq;
                  }
                  return Result::Ok(Value::Obj().Set("last", Value::Num((double)last)).Set("lines", std::move(lines)));
              }));
}

} // namespace

namespace detail {
void RegisterEngineCommands(Registry& r) {
    AddAutomation(r);
    AddApi(r);
    AddScene(r);
    AddComponents(r);
    AddTransform(r);
    AddPhysics(r);
    AddAnimation(r);
    AddViewport(r);
    AddLog(r);
}
} // namespace detail

} // namespace okay::automation
