// Automation: the command table, parameter checking and the one call path (okay/Automation/Automation.hpp). Our own
// code, written from the JSON Schema core vocabulary (draft 2020-12) for the schemas it writes.
#include "okay/Automation/Automation.hpp"

#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scripting/ScriptBindings.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace okay::automation {

namespace detail {
// The engine's own commands (EngineCommands.cpp). Named here, not handed over by a static initialiser, because a
// static library drops an object file nothing refers to.
void RegisterEngineCommands(Registry& r);
}

const char* ParamTypeName(ParamType t) {
    switch (t) {
    case ParamType::String: return "string";
    case ParamType::Number: return "number";
    case ParamType::Integer: return "integer";
    case ParamType::Boolean: return "boolean";
    case ParamType::Vec3: return "vec3";
    case ParamType::Object: return "object";
    case ParamType::Array: return "array";
    case ParamType::Any: return "any";
    }
    return "any";
}

// ---- Result ------------------------------------------------------------------------------------------------------

Result Result::Ok(json::Value data) {
    Result r;
    r.ok = true;
    r.data = data.IsNull() ? json::Value::Obj() : std::move(data);
    return r;
}

Result Result::Fail(std::string code, std::string message, json::Value details) {
    Result r;
    r.ok = false;
    r.error.code = std::move(code);
    r.error.message = std::move(message);
    r.error.details = std::move(details);
    return r;
}

json::Value Result::ToJson() const {
    json::Value v = json::Value::Obj();
    v.Set("ok", json::Value::Boolean(ok));
    if (ok) v.Set("data", data);
    else {
        json::Value e = json::Value::Obj();
        e.Set("code", json::Value::Str(error.code));
        e.Set("message", json::Value::Str(error.message));
        if (!error.details.IsNull()) e.Set("details", error.details);
        v.Set("error", std::move(e));
    }
    return v;
}

// ---- Call ----------------------------------------------------------------------------------------------------------

Scene& Call::scene() const { return *m_host.scene(); }

const json::Value* Call::Get(const char* name) const {
    const json::Value* v = m_params.Get(name);
    return (v && !v->IsNull()) ? v : nullptr;
}
bool Call::Has(const char* name) const { return Get(name) != nullptr; }

std::string Call::Str(const char* name, const std::string& def) const {
    const json::Value* v = Get(name);
    return v && v->IsString() ? v->string : def;
}
double Call::Num(const char* name, double def) const {
    const json::Value* v = Get(name);
    return v && v->IsNumber() ? v->number : def;
}
long long Call::Int(const char* name, long long def) const {
    const json::Value* v = Get(name);
    return v && v->IsNumber() ? (long long)v->number : def;
}
bool Call::Bool(const char* name, bool def) const {
    const json::Value* v = Get(name);
    return v && v->IsBool() ? v->boolean : def;
}

namespace {

/// A Vec3 in either JSON form; false when `v` is neither (or a part is not a finite number).
bool ReadVec3(const json::Value& v, float out[3]) {
    const char* keys[3] = {"x", "y", "z"};
    for (int i = 0; i < 3; ++i) {
        const json::Value* c = nullptr;
        if (v.IsArray()) { if (v.array.size() != 3) return false; c = &v.array[(std::size_t)i]; }
        else if (v.IsObject()) c = v.Get(keys[i]);
        if (!c || !c->IsNumber() || !std::isfinite(c->number)) return false;
        out[i] = (float)c->number;
    }
    return true;
}

bool ValidName(const std::string& n) {
    if (n.empty() || n.size() > 64 || n.front() == '.' || n.back() == '.') return false;
    bool dot = false;
    for (std::size_t i = 0; i < n.size(); ++i) {
        const char c = n[i];
        if (c == '.') { if (n[i - 1] == '.') return false; dot = true; continue; }
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return dot;
}

json::Value StrList(const std::vector<std::string>& v) {
    json::Value a = json::Value::Arr();
    for (const auto& s : v) a.Push(json::Value::Str(s));
    return a;
}

json::Value FlagList(unsigned f) {
    static const std::pair<unsigned, const char*> kNames[] = {
        {flag::Mutates, "mutates"}, {flag::Undoable, "undoable"}, {flag::NeedsEditor, "needs_editor"},
        {flag::NeedsPlay, "needs_play"}, {flag::AllowInPlay, "allow_in_play"}, {flag::Slow, "slow"},
    };
    json::Value a = json::Value::Arr();
    for (const auto& n : kNames) if (f & n.first) a.Push(json::Value::Str(n.second));
    return a;
}

json::Value ParamSchema(const Param& p) {
    json::Value s = json::Value::Obj();
    switch (p.type) {
    case ParamType::String: s.Set("type", json::Value::Str("string")); break;
    case ParamType::Number: s.Set("type", json::Value::Str("number")); break;
    case ParamType::Integer: s.Set("type", json::Value::Str("integer")); break;
    case ParamType::Boolean: s.Set("type", json::Value::Str("boolean")); break;
    case ParamType::Object: s.Set("type", json::Value::Str("object")); break;
    case ParamType::Array: s.Set("type", json::Value::Str("array")); break;
    case ParamType::Vec3: {
        // One form in the schema (agents write objects most reliably); Validate also takes [x, y, z].
        s.Set("type", json::Value::Str("object"));
        json::Value props = json::Value::Obj();
        json::Value req = json::Value::Arr();
        for (const char* k : {"x", "y", "z"}) {
            props.Set(k, json::Value::Obj().Set("type", json::Value::Str("number")));
            req.Push(json::Value::Str(k));
        }
        s.Set("properties", std::move(props));
        s.Set("required", std::move(req));
        break;
    }
    case ParamType::Any: break;   // no "type": anything
    }
    if (!p.description.empty()) s.Set("description", json::Value::Str(p.description));
    if (!p.options.empty()) s.Set("enum", StrList(p.options));
    if (!p.defaultValue.IsNull()) s.Set("default", p.defaultValue);
    return s;
}

bool Fits(ParamType t, const json::Value& v) {
    switch (t) {
    case ParamType::String: return v.IsString();
    case ParamType::Number: return v.IsNumber();
    case ParamType::Integer: return v.IsNumber() && std::isfinite(v.number) && v.number == std::floor(v.number);
    case ParamType::Boolean: return v.IsBool();
    case ParamType::Object: return v.IsObject();
    case ParamType::Array: return v.IsArray();
    case ParamType::Vec3: { float f[3]; return ReadVec3(v, f); }
    case ParamType::Any: return true;
    }
    return false;
}

std::string Plain(const std::string& code, const std::string& what) { return code + ": " + what; }

} // namespace

void Call::Vec(const char* name, float out[3], float dx, float dy, float dz) const {
    out[0] = dx; out[1] = dy; out[2] = dz;
    if (const json::Value* v = Get(name)) { float f[3]; if (ReadVec3(*v, f)) { out[0] = f[0]; out[1] = f[1]; out[2] = f[2]; } }
}

// ---- Registry ------------------------------------------------------------------------------------------------------

bool Registry::Add(Command c) {
    if (!ValidName(c.name) || !c.handler || Find(c.name)) return false;
    auto flat = [](std::string n) { for (char& ch : n) if (ch == '.') ch = '_'; return n; };   // the MCP tool name (Mcp.hpp)
    for (const Command& k : m_commands) if (flat(k.name) == flat(c.name)) return false;
    for (std::size_t i = 0; i < c.params.size(); ++i) {
        if (c.params[i].name.empty()) return false;
        for (std::size_t j = 0; j < i; ++j) if (c.params[j].name == c.params[i].name) return false;
    }
    if (c.flags & flag::Undoable) c.flags |= flag::Mutates;
    const auto at = std::lower_bound(m_commands.begin(), m_commands.end(), c.name,
                                     [](const Command& a, const std::string& n) { return a.name < n; });
    m_commands.insert(at, std::move(c));
    return true;
}

bool Registry::AddAlias(const std::string& alias, const std::string& target) {
    const Command* t = Find(target);
    if (!t) return false;
    Command c = *t;
    c.name = alias;
    c.aliasOf = t->aliasOf.empty() ? t->name : t->aliasOf;
    c.summary = "Same as " + c.aliasOf + ": " + t->summary;
    return Add(std::move(c));
}

const Command* Registry::Find(const std::string& name) const {
    const auto at = std::lower_bound(m_commands.begin(), m_commands.end(), name,
                                     [](const Command& a, const std::string& n) { return a.name < n; });
    return at != m_commands.end() && at->name == name ? &*at : nullptr;
}

std::vector<const Command*> Registry::List(const std::string& prefix) const {
    std::vector<const Command*> v;
    for (const Command& c : m_commands) if (c.name.compare(0, prefix.size(), prefix) == 0) v.push_back(&c);
    return v;
}

json::Value Registry::InputSchema(const Command& c) {
    json::Value s = json::Value::Obj();
    s.Set("type", json::Value::Str("object"));
    json::Value props = json::Value::Obj();
    json::Value req = json::Value::Arr();
    for (const Param& p : c.params) {
        props.Set(p.name, ParamSchema(p));
        if (p.required) req.Push(json::Value::Str(p.name));
    }
    s.Set("properties", std::move(props));
    if (!req.array.empty()) s.Set("required", std::move(req));
    s.Set("additionalProperties", json::Value::Boolean(false));
    return s;
}

json::Value Registry::Describe(const Command& c) {
    json::Value d = json::Value::Obj();
    d.Set("name", json::Value::Str(c.name));
    d.Set("summary", json::Value::Str(c.summary));
    d.Set("flags", FlagList(c.flags));
    if (!c.aliasOf.empty()) d.Set("aliasOf", json::Value::Str(c.aliasOf));
    json::Value ps = json::Value::Arr();
    for (const Param& p : c.params) {
        json::Value e = json::Value::Obj();
        e.Set("name", json::Value::Str(p.name));
        e.Set("type", json::Value::Str(ParamTypeName(p.type)));
        e.Set("required", json::Value::Boolean(p.required));
        if (!p.description.empty()) e.Set("description", json::Value::Str(p.description));
        if (!p.options.empty()) e.Set("options", StrList(p.options));
        if (!p.defaultValue.IsNull()) e.Set("default", p.defaultValue);
        ps.Push(std::move(e));
    }
    d.Set("params", std::move(ps));
    d.Set("inputSchema", InputSchema(c));
    d.Set("result", json::Value::Str(c.result));
    std::vector<std::string> errs = {code::InvalidParams, code::Internal};
    if (c.flags & flag::NeedsEditor) errs.push_back(code::Unavailable);
    if ((c.flags & flag::Mutates) && !(c.flags & flag::AllowInPlay)) errs.push_back(code::Refused);
    for (const auto& e : c.errors) if (std::find(errs.begin(), errs.end(), e) == errs.end()) errs.push_back(e);
    d.Set("errors", StrList(errs));
    return d;
}

std::string Registry::Validate(const Command& c, const json::Value& params) {
    if (params.IsNull()) {
        for (const Param& p : c.params) if (p.required) return "the parameter '" + p.name + "' is needed";
        return {};
    }
    if (!params.IsObject()) return "the parameters must be a JSON object";
    for (const auto& kv : params.object) {
        const Param* p = nullptr;
        for (const Param& q : c.params) if (q.name == kv.first) { p = &q; break; }
        if (!p) {
            std::string known;
            for (const Param& q : c.params) known += (known.empty() ? "" : ", ") + q.name;
            return "'" + kv.first + "' is not a parameter of " + c.name + (known.empty() ? " (it takes none)" : " (it takes " + known + ")");
        }
        if (kv.second.IsNull()) continue;   // null = left out
        if (!Fits(p->type, kv.second))
            return "'" + p->name + "' must be " + (p->type == ParamType::Vec3 ? std::string("{\"x\":..,\"y\":..,\"z\":..}") : std::string("a ") + ParamTypeName(p->type));
        if (!p->options.empty() && std::find(p->options.begin(), p->options.end(), kv.second.string) == p->options.end()) {
            std::string opts;
            for (const auto& o : p->options) opts += (opts.empty() ? "" : ", ") + o;
            return "'" + p->name + "' must be one of: " + opts;
        }
    }
    for (const Param& p : c.params) {
        if (!p.required) continue;
        const json::Value* v = params.Get(p.name);
        if (!v || v->IsNull()) return "the parameter '" + p.name + "' is needed";
    }
    return {};
}

Result Registry::Run(Host& host, const std::string& name, const json::Value& params) const {
    const Command* c = Find(name);
    if (!c) {
        // Name the commands that share its namespace, so a caller with a typo sees what exists.
        json::Value similar = json::Value::Arr();
        const std::string ns = name.substr(0, name.find('.') == std::string::npos ? name.size() : name.find('.') + 1);
        for (const Command* k : List(ns)) similar.Push(json::Value::Str(k->name));
        return Result::Fail(code::UnknownCommand, "There is no command called '" + name + "'.",
                            similar.array.empty() ? json::Value() : json::Value::Obj().Set("similar", std::move(similar)));
    }
    const std::string bad = Validate(*c, params);
    if (!bad.empty()) return Result::Fail(code::InvalidParams, bad + ".");
    if ((c->flags & flag::NeedsEditor) && !host.hasEditor())
        return Result::Fail(code::Unavailable, c->name + " needs the editor's state; this host has none.");
    if (!host.scene()) return Result::Fail(code::Unavailable, "This host has no scene.");
    const bool playing = host.isPlaying();
    if ((c->flags & flag::NeedsPlay) && !playing) return Result::Fail(code::Refused, c->name + " works only while the game is playing (play.start).");
    if ((c->flags & flag::Mutates) && !(c->flags & flag::AllowInPlay) && playing)
        return Result::Fail(code::Refused, "The game is playing; stop it first (play.stop). Edits made while playing would be thrown away.");

    static const json::Value kNone = json::Value::Obj();
    Call call(host, params.IsNull() ? kNone : params);
    const bool undoable = (c->flags & flag::Undoable) && !playing;
    if (undoable) host.BeginEdit(c->undoName);
    Result r;
    try {
        r = c->handler(call);
    } catch (const std::exception& ex) {
        r = Result::Fail(code::Internal, Plain(c->name, ex.what()));
    } catch (...) {
        r = Result::Fail(code::Internal, c->name + " stopped with an unknown C++ exception.");
    }
    if (undoable) host.EndEdit(r.ok);
    if (r.ok && r.data.IsNull()) r.data = json::Value::Obj();
    return r;
}

// ---- The process's table -------------------------------------------------------------------------------------------

namespace {
struct Provider { const char* system; void (*fn)(Registry&); bool done; };
std::vector<Provider>& Providers() { static std::vector<Provider> v; return v; }
}

void AddProvider(const char* system, void (*fn)(Registry&)) {
    if (!fn) return;
    for (const Provider& p : Providers()) if (p.fn == fn) return;
    Providers().push_back({system ? system : "", fn, false});
}

Registry& Commands() {
    static Registry r;
    static bool engine = false;
    if (!engine) { engine = true; detail::RegisterEngineCommands(r); }
    for (Provider& p : Providers()) {   // a provider handed over late still joins: the loop sees it next time
        if (p.done) continue;
        p.done = true;
        p.fn(r);
    }
    return r;
}

// ---- Shared helpers ------------------------------------------------------------------------------------------------

Param Arg(const char* name, ParamType type, const char* description, bool required) {
    Param p;
    p.name = name;
    p.type = type;
    p.description = description;
    p.required = required;
    return p;
}

Param ArgChoice(const char* name, std::vector<std::string> options, const char* description, const char* def) {
    Param p = Arg(name, ParamType::String, description);
    p.options = std::move(options);
    if (def) p.defaultValue = json::Value::Str(def);
    return p;
}

Param ArgDefault(const char* name, ParamType type, const char* description, json::Value def) {
    Param p = Arg(name, type, description);
    p.defaultValue = std::move(def);
    return p;
}

Command MakeCommand(const char* name, const char* summary, std::vector<Param> params, const char* result, unsigned flags,
                    Handler handler, std::vector<std::string> errors) {
    Command c;
    c.name = name;
    c.summary = summary;
    c.params = std::move(params);
    c.result = result;
    c.flags = flags;
    c.handler = std::move(handler);
    c.errors = std::move(errors);
    return c;
}

Result Fail(const Error& e) { return Result::Fail(e.code, e.message, e.details); }

GameObject* FindObject(Scene& scene, const std::string& ref, Error& error) {
    if (ref.empty()) { error = {code::InvalidParams, "No object was named.", {}}; return nullptr; }
    if (GameObject* g = scene.FindByGuid(ref)) if (!g->IsDestroyed()) return g;
    if (ref.find('/') != std::string::npos) {
        const std::vector<GameObject*> all = scene.FindAllByPath(ref);
        if (all.size() == 1) return all[0];
        if (all.size() > 1) { error = {code::Ambiguous, std::to_string(all.size()) + " objects have the path '" + ref + "'; use the id.", {}}; return nullptr; }
    }
    std::vector<GameObject*> named = scene.FindGameObjectsWithName(ref);
    named.erase(std::remove_if(named.begin(), named.end(), [](GameObject* g) { return !g || g->IsDestroyed(); }), named.end());
    if (named.size() == 1) return named[0];
    if (named.size() > 1) {
        json::Value ids = json::Value::Arr();
        for (GameObject* g : named) ids.Push(json::Value::Str(g->Guid()));
        error = {code::Ambiguous, std::to_string(named.size()) + " objects are called '" + ref + "'; use one of their ids.",
                 json::Value::Obj().Set("ids", std::move(ids))};
        return nullptr;
    }
    error = {code::NotFound, "There is no object '" + ref + "' (an id, a path such as Parent/Child, or a name).", {}};
    return nullptr;
}

json::Value Vec3Json(float x, float y, float z) {
    return json::Value::Obj().Set("x", json::Value::Num(x)).Set("y", json::Value::Num(y)).Set("z", json::Value::Num(z));
}

namespace {
/// The type name of a component as the scene file and scripts know it: the reflection table's (ScriptBindings) class
/// name, else the property table's.
std::string ComponentName(const Component& c) {
    if (const scriptbind::ClassBinding* b = scriptbind::ClassOf(c)) return b->name;
    if (auto info = PropertyRegistry::FindFor(c)) return info->name;
    return "Component";
}
}

json::Value ObjectSummary(const GameObject& go, bool deep) {
    json::Value o = json::Value::Obj();
    o.Set("id", json::Value::Str(go.Guid()));
    o.Set("name", json::Value::Str(go.name));
    o.Set("path", json::Value::Str(go.Path()));
    o.Set("active", json::Value::Boolean((bool)go.active));
    json::Value comps = json::Value::Arr();
    for (const auto& c : go.Components()) if (c) comps.Push(json::Value::Str(ComponentName(*c)));
    o.Set("components", std::move(comps));
    if (deep && go.transform) {
        const Transform& t = *go.transform;
        const Vec3 e = t.LocalEulerAngles();
        o.Set("position", Vec3Json(t.localPosition.x, t.localPosition.y, t.localPosition.z));
        o.Set("rotation", Vec3Json(e.x, e.y, e.z));
        o.Set("scale", Vec3Json(t.localScale.x, t.localScale.y, t.localScale.z));
        const Vec3 w = t.Position();
        o.Set("worldPosition", Vec3Json(w.x, w.y, w.z));
        o.Set("parent", t.Parent() && t.Parent()->gameObject ? json::Value::Str(t.Parent()->gameObject->Guid()) : json::Value());
        json::Value kids = json::Value::Arr();
        for (Transform* k : t.Children()) if (k && k->gameObject && !k->gameObject->IsDestroyed()) kids.Push(json::Value::Str(k->gameObject->Guid()));
        o.Set("children", std::move(kids));
        o.Set("tag", json::Value::Str(std::string(go.tag)));
        o.Set("layer", json::Value::Num(go.layer));
    }
    return o;
}

} // namespace okay::automation
