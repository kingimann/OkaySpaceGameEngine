// Language-neutral helpers for text-language script backends (see the header).
#include "ScriptLanguageGlue.hpp"
#include "okay/Scripting/ScriptBindings.hpp"
#include <cctype>

namespace okay::scriptglue {

namespace sc = script;

std::vector<std::string> HandlerNamesFor(const std::string& fn) {
    // engine name -> the PascalCase handler, then the old snake_case one
    struct Alias { const char* engine; const char* pascal; const char* snake; };
    static const Alias kAliases[] = {
        {"on_fixed_update", "FixedUpdate", "fixed_update"}, {"on_late_update", "LateUpdate", "late_update"},
        {"on_enable", "OnEnable", "on_enable"}, {"OnEnable", "OnEnable", "on_enable"},
        {"on_disable", "OnDisable", "on_disable"}, {"on_destroy", "OnDestroy", "on_destroy"},
        {"Start", "Start", "start"}, {"Awake", "Awake", "awake"}, {"Update", "Update", "update"},
    };
    std::vector<std::string> out;
    for (const Alias& a : kAliases) {
        if (fn != a.engine) continue;
        out = {a.pascal, a.snake};
        if (fn != a.pascal && fn != a.snake) out.push_back(fn);
        return out;
    }
    out.push_back(fn);
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
    for (const auto& d : docs)
        if (name == d.first) return d.second;
    return "";
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
    for (std::size_t i = 0; i < m.params.size(); ++i)
        ps.push_back({m.params[i].kind == sc::TK::String, i < m.paramNames.size() ? m.paramNames[i] : std::string()});
    return scriptbind::IsDeniedMethod(t.name, m.name, ps);
}

} // namespace okay::scriptglue
