// Language-neutral helpers for text-language script backends (see the header).
#include "ScriptLanguageGlue.hpp"
#include "../Script/CheckerInternal.hpp"   // EditDistance for "did you mean"
#include "okay/Core/Log.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scripting/ScriptBindings.hpp"
#include <cctype>

namespace okay::scriptglue {

namespace sc = script;
using script::checker_detail::EditDistance;

std::vector<std::string> HandlerNamesFor(const std::string& fn) {
    // engine name -> the PascalCase handler, then the old snake_case one
    struct Alias { const char* engine; const char* pascal; const char* snake; };
    // dup-allow-begin: Lua's copy of this table (LuaScriptVM.cpp HandlerNames) moves onto this file, handed over on #422
    static const Alias kAliases[] = {
        {"on_fixed_update", "FixedUpdate", "fixed_update"}, {"on_late_update", "LateUpdate", "late_update"},
        {"on_enable", "OnEnable", "on_enable"}, {"OnEnable", "OnEnable", "on_enable"},
        {"on_disable", "OnDisable", "on_disable"}, {"on_destroy", "OnDestroy", "on_destroy"},
        {"Start", "Start", "start"}, {"Awake", "Awake", "awake"}, {"Update", "Update", "update"},
    };
    // dup-allow-end
    for (const Alias& a : kAliases) {
        if (fn != a.engine) continue;
        std::vector<std::string> out{a.pascal, a.snake};   // dup-allow: Lua's copy (LuaScriptVM.cpp HandlerNames) moves here, #422
        if (fn != a.pascal && fn != a.snake) out.push_back(fn);
        return out;
    }
    std::vector<std::string> out{fn};
    if (fn.find('_') == std::string::npos) return out;
    std::string pascal;
    bool upper = true;
    for (char ch : fn) {
        if (ch == '_') { upper = true; continue; }
        pascal += upper ? (char)std::toupper((unsigned char)ch) : ch;
        upper = false;
    }
    out.push_back(pascal);
    return out;
}

bool IsEngineSetName(const std::string& n) {
    if (n == "other" || n == "other_tag" || n == "impact_speed" || n == "gameObject" || n == "transform") return true;
    return n.compare(0, 8, "contact_") == 0;
}

const std::vector<std::string>& LifecycleHandlers() {
    static const std::vector<std::string> names = {
        "Awake", "Start", "Update", "LateUpdate", "FixedUpdate", "OnEnable", "OnDisable", "OnDestroy",
        "OnTriggerEnter", "OnTriggerStay", "OnTriggerExit", "OnCollisionEnter", "OnCollisionStay", "OnCollisionExit"};
    return names;
}

std::string HandlerDoc(const std::string& name) {
    // dup-allow-begin: Lua's copy of these texts (LuaScriptVM.cpp CompletionDoc) moves here, handed over on #422
    static const std::pair<const char*, const char*> docs[] = {
        {"Awake", "called once when the scene starts, before Start"},
        {"Start", "called once before the first Update"},
        {"Update", "called every frame: Update(dt), dt = seconds since the last frame"},
        {"LateUpdate", "called every frame after every Update"},
        {"FixedUpdate", "called 50 times a second: FixedUpdate(dt)"},
        {"OnEnable", "called when the object is switched on"},
        {"OnDisable", "called when the object is switched off"},
        {"OnDestroy", "called when the object is destroyed"},
        {"OnTriggerEnter", "OnTriggerEnter(other): another object entered this trigger"},
        {"OnTriggerStay", "OnTriggerStay(other): another object is inside this trigger"},
        {"OnTriggerExit", "OnTriggerExit(other): another object left this trigger"},
        {"OnCollisionEnter", "OnCollisionEnter(other): this object hit another one"},
        {"OnCollisionStay", "OnCollisionStay(other): this object keeps touching another one"},
        {"OnCollisionExit", "OnCollisionExit(other): this object stopped touching another one"},
    };
    // dup-allow-end
    for (const auto& d : docs)
        if (name == d.first) return d.second;
    return "";
}

std::string HandlerTypo(const std::string& n) {
    if (n.size() < 4) return "";
    std::string nearName;
    for (const std::string& h : LifecycleHandlers()) {
        if (n == h) return "";
        std::string lower = h;
        for (char& ch : lower) ch = (char)std::tolower((unsigned char)ch);
        if (n == lower) return "";   // the old snake_case style: update, start
        if (nearName.empty() && EditDistance(n, h) <= 2) nearName = h;
    }
    if (nearName.empty()) return "";
    return n + " is never called by the engine. Did you mean " + nearName + "? (names are case-sensitive)";
}

std::size_t FirstWordPos(const std::string& source, const std::string& word) {
    auto isWord = [](char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '$'; };
    for (std::size_t at = source.find(word); at != std::string::npos; at = source.find(word, at + 1)) {
        std::size_t end = at + word.size();
        if ((at == 0 || !isWord(source[at - 1])) && (end >= source.size() || !isWord(source[end]))) return at;
    }
    return std::string::npos;
}

bool SettingText(const vs::VsValue& v, std::string& type, std::string& text, std::string (*number)(double)) {
    if (v.IsBool()) {
        type = "boolean";
        text = v.AsBool() ? "true" : "false";
    } else if (v.IsNumber()) {
        type = "number";
        text = number((double)v.AsFloat());
    } else if (v.IsString()) {
        type = "string";
        text = "\"" + v.AsString() + "\"";
    } else if (v.IsVec3()) {
        const Vec3 p = v.AsVec3();
        type = "Vector3";
        text = "Vector3(" + number(p.x) + ", " + number(p.y) + ", " + number(p.z) + ")";
    } else return false;
    return true;
}

void ReportHandlerError(const GameObject* self, const std::string& message) {
    Log::Error("Script on '", self ? self->name : std::string("?"), "', ", message);
}

std::string TypeWord(const sc::Type& t) {
    switch (t.kind) {   // dup-allow: Lua's copy (LuaScriptVM.cpp TypeWord) moves here, handed over on #422
        case sc::TK::Float: return "number";
        case sc::TK::Int: case sc::TK::Enum: return "whole number";
        case sc::TK::Bool: return "boolean";
        case sc::TK::String: return "string";
        case sc::TK::Vector3: return "Vector3";
        case sc::TK::Any: return "any value";
        case sc::TK::Object: return sc::TypeName(t);
        case sc::TK::Array: return "list of strings";
        default: return "value";
    }
}

std::string Signature(const std::string& owner, const char* sep, const sc::ApiMethod& m) {
    std::string s = owner + sep + m.name + "(";
    for (std::size_t i = 0; i < m.params.size(); ++i) {
        if (i) s += ", ";
        if (i < m.paramNames.size()) s += m.paramNames[i] + ": ";
        s += TypeWord(m.params[i]);
    }
    s += ")";
    if (m.ret.kind != sc::TK::Void) s += " -> " + TypeWord(m.ret);
    return s;
}

bool IsDenied(const sc::ApiType& t, const sc::ApiMethod& m) {
    std::vector<scriptbind::DenyParam> ps;
    ps.reserve(m.params.size());
    for (std::size_t i = 0; i < m.params.size(); ++i) {
        scriptbind::DenyParam p;   // dup-allow: Lua's copy (LuaScriptVM.cpp LuaDenied) moves here, #422
        p.isText = m.params[i].kind == sc::TK::String;
        if (i < m.paramNames.size()) p.name = m.paramNames[i];
        ps.push_back(p);
    }
    return scriptbind::IsDeniedMethod(t.name, m.name, ps);
}

bool HasCallable(const sc::ApiType& t, const std::vector<sc::ApiMethod>& list, const std::string& name) {
    for (const auto& m : list)
        if (m.name == name && !IsDenied(t, m)) return true;
    return false;
}

std::string LiteralProblem(const sc::ApiType& t, const sc::ApiMethod& m, const std::vector<sc::Value>& args) {
    if (!m.checkLiteral) return "";
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string* s = std::get_if<std::string>(&args[i]);
        if (!s) continue;
        std::string msg = m.checkLiteral((int)i, *s);
        if (!msg.empty()) return t.name + "." + m.name + ": " + msg;
    }
    return "";
}

bool HandleOnObject(GameObject& g, const std::string& typeName, vs::VsValue& out) {
    const scriptbind::ClassBinding* cls = scriptbind::FindClass(typeName);
    if (!cls) return false;
    Component* c = scriptbind::FindComponent(g, *cls, 0);
    if (!c) return false;
    out = scriptbind::MakeHandle(c, cls);
    return true;
}

bool HandleHasMethod(const vs::VsValue& handle, const std::string& name) {
    const scriptbind::ClassBinding* bound = nullptr;
    Component* c = scriptbind::Resolve(handle, &bound);
    if (!c) return false;
    const scriptbind::ClassBinding* classes[2] = {bound, scriptbind::ClassOf(*c)};
    for (const scriptbind::ClassBinding* k : classes)
        if (k)
            for (const auto& m : k->methods)
                if (name == m.name) return true;
    return false;
}

bool ColorFromFields(double (*field)(void* ctx, const char* name, double fallback), void* ctx, vs::VsValue& out) {
    if (field(ctx, "r", -1) < 0 && field(ctx, "g", -1) < 0 && field(ctx, "b", -1) < 0) return false;
    out = vs::VsValue::Color((float)field(ctx, "r", 0), (float)field(ctx, "g", 0), (float)field(ctx, "b", 0),
                             (float)field(ctx, "a", 1));
    return true;
}

void AddMembers(const sc::ApiType& t, bool statics, std::set<std::string>& out) {
    for (auto& p : statics ? t.staticProps : t.props) out.insert(p.name);
    for (auto& m : statics ? t.staticMethods : t.methods) out.insert(m.name);
}

void AddApiNames(std::set<std::string>& out, HiddenFn hidden) {
    for (const sc::ApiType& t : sc::EngineApi::Get().types) {
        if (hidden(t)) continue;
        out.insert(t.name);
        AddMembers(t, false, out);
        for (auto& p : t.staticProps) out.insert(t.name + "." + p.name);
        for (auto& m : t.staticMethods) out.insert(t.name + "." + m.name);
    }
    for (const std::string& h : LifecycleHandlers()) out.insert(h);
}

namespace {
int FindVisible(const std::string& name, HiddenFn hidden) {
    const auto& api = sc::EngineApi::Get();
    int idx = api.Find(name);
    return idx >= 0 && hidden(api.types[(std::size_t)idx]) ? -1 : idx;
}
std::string PropDoc(const sc::ApiType& t, const sc::ApiProperty& p) { return t.name + "." + p.name + ": " + TypeWord(p.type) + ". " + p.doc; }
std::string MethodDoc(const sc::ApiType& t, const char* sep, const sc::ApiMethod& m) { return Signature(t.name, sep, m) + ". " + m.doc; }
} // namespace

std::string ApiDoc(const std::string& name, const char* instanceSep, HiddenFn hidden) {
    const auto& api = sc::EngineApi::Get();
    std::size_t dot = name.find('.');
    if (dot != std::string::npos) {   // Type.member
        int idx = FindVisible(name.substr(0, dot), hidden);
        if (idx < 0) return "";
        const sc::ApiType& t = api.types[(std::size_t)idx];
        const std::string m = name.substr(dot + 1);
        for (const auto& p : t.staticProps) if (p.name == m) return PropDoc(t, p);
        for (const auto& f : t.staticMethods) if (f.name == m) return MethodDoc(t, ".", f);
        return "";
    }
    int idx = FindVisible(name, hidden);
    if (idx >= 0) return "engine type " + name + ": " + api.types[(std::size_t)idx].doc;
    // an instance member first, then a static member named alone (the editor's hover word for "Input.GetKey" is "GetKey")
    for (int pass = 0; pass < 2; ++pass)
        for (const sc::ApiType& t : api.types) {
            if (hidden(t)) continue;
            for (const auto& p : pass ? t.staticProps : t.props) if (p.name == name) return PropDoc(t, p);
            for (const auto& f : pass ? t.staticMethods : t.methods) if (f.name == name) return MethodDoc(t, pass ? "." : instanceSep, f);
        }
    return "";
}

int ApiReceiverMembers(const std::string& receiver, HiddenFn hidden, std::set<std::string>& out) {
    const auto& api = sc::EngineApi::Get();
    std::size_t d = receiver.find_last_of(".:");
    const std::string last = d == std::string::npos ? receiver : receiver.substr(d + 1);
    int idx = FindVisible(receiver, hidden);
    if (idx >= 0) { AddMembers(api.types[(std::size_t)idx], true, out); return 1; }
    if (last == "transform" || last == "gameObject" || last == "other") {
        AddMembers(api.types[(std::size_t)(last == "transform" ? api.transform : api.gameObject)], false, out);
        out.insert("GetComponent");
        return 1;
    }
    for (const sc::ApiType& t : api.types)
        for (const sc::ApiProperty& p : t.props)
            if (p.name == last && p.type.kind == sc::TK::Vector3) return 3;
    return 0;
}

std::string StaticMemberProblem(const std::string& typeName, const std::string& member, HiddenFn hidden,
                                const std::vector<std::string>& extra) {
    const auto& api = sc::EngineApi::Get();
    int idx = FindVisible(typeName, hidden);
    if (idx < 0) return "";
    const sc::ApiType& ty = api.types[(std::size_t)idx];
    if (ty.staticProps.empty() && ty.staticMethods.empty() && idx != api.vector3) return "";
    if (member.empty() || !(std::isalpha((unsigned char)member[0]) || member[0] == '_')) return "";
    std::set<std::string> names(extra.begin(), extra.end());
    AddMembers(ty, true, names);
    if (names.count(member)) return "";
    std::string best;
    int bestDistance = 3;
    for (const std::string& n : names) {
        int dd = EditDistance(member, n);
        if (dd < bestDistance) { bestDistance = dd; best = n; }
    }
    std::string msg = ty.name + " has no member '" + member + "'";
    if (!best.empty()) msg += ". Did you mean " + best + "?";
    return msg;
}

} // namespace okay::scriptglue
