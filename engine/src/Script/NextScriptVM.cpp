#include "okay/Scripting/NextScriptVM.hpp"
#include "ScriptInternal.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/GameObject.hpp"
#include <cmath>
#include <set>

namespace okay {

using namespace script;

struct NextScriptVM::Impl {
    std::shared_ptr<Program> prog;
    std::unique_ptr<Instance> inst;
    std::string source;   ///< statics are shared by the objects in a scene running this exact source
};

void NextScriptVM::ForgetSceneStatics(const Scene* scene) { script::ForgetSceneStatics(scene); }

NextScriptVM::NextScriptVM() : m_impl(std::make_unique<Impl>()) {}
NextScriptVM::~NextScriptVM() = default;

std::vector<std::string> NextScriptVM::BuiltinNames() const {
    std::set<std::string> names;
    for (const ApiType& t : EngineApi::Get().types) {
        names.insert(t.name);
        for (auto& p : t.props) names.insert(p.name);
        for (auto& p : t.staticProps) names.insert(p.name);
        for (auto& m : t.methods) names.insert(m.name);
        for (auto& m : t.staticMethods) names.insert(m.name);
    }
    for (const auto& l : LifecycleMethods()) names.insert(l.name);
    for (const char* n : {"gameObject", "transform", "enabled", "GetComponent", "Destroy", "Behaviour"}) names.insert(n);
    return {names.begin(), names.end()};
}

namespace {
std::string FirstError(const Program& p) {
    for (const auto& d : p.diags)
        if (d.severity == 0) return "line " + std::to_string(d.line) + ": " + d.message;
    return "the script has no class extending Behaviour";
}
} // namespace

bool NextScriptVM::Load(const std::string& source, std::string* error) {
    auto prog = Compile(source);
    if (!prog->ok()) {
        if (error) *error = FirstError(*prog);
        m_impl->prog.reset();
        m_impl->inst.reset();
        return false;
    }
    m_impl->source = source;
    m_impl->prog = prog;
    m_impl->inst = std::make_unique<Instance>(prog, m_host ? m_host->gameObject : nullptr);
    try {
        m_impl->inst->Init();
    } catch (const RuntimeError& e) {
        if (error) *error = "line " + std::to_string(e.loc.line) + ": " + e.what();
        m_impl->inst.reset();
        return false;
    }
    return true;
}

bool NextScriptVM::Validate(const std::string& source, std::string* error) {
    auto prog = Compile(source);
    if (prog->ok()) return true;
    if (error) *error = FirstError(*prog);
    return false;
}

std::vector<ScriptDiagnostic> NextScriptVM::ValidateAll(const std::string& source) {
    return Compile(source)->diags;
}

void NextScriptVM::Bind(ScriptHost* host) {
    m_host = host;
    if (m_impl->inst && host) {
        m_impl->inst->Context().self = host->gameObject;
        m_impl->inst->Context().component = host->component;
        if (host->gameObject && host->gameObject->scene()) {
            try {
                m_impl->inst->AttachStatics(SceneStatics(host->gameObject->scene(), m_impl->source));
            } catch (const RuntimeError& e) {
                ++m_errorCount;
                m_lastError = "line " + std::to_string(e.loc.line) + ": " + e.what();
                Log::Error("Script on '", host->gameObject->name, "', static field line ", e.loc.line, ": ", e.what());
            }
        }
    }
}

bool NextScriptVM::RunMethod(const std::string& name) {
    if (!m_impl->inst) return false;
    int idx = m_impl->prog->MethodIndex(name);
    if (idx < 0) return false;
    CallContext& c = m_impl->inst->Context();
    if (m_host) {
        c.self = m_host->gameObject;
        c.component = m_host->component;
        c.deltaTime = m_host->deltaTime;
    }
    try {
        m_impl->inst->Call(idx, {});
    } catch (const RuntimeError& e) {
        ++m_errorCount;
        const std::string obj = c.self ? c.self->name : std::string("?");
        m_lastError = "line " + std::to_string(e.loc.line) + ": " + e.what();
        Log::Error("Script on '", obj, "', ", name, "() line ", e.loc.line, ": ", e.what());
    }
    return true;
}

void NextScriptVM::CallStart() {
    RunMethod("Awake");
    RunMethod("OnEnable");
    RunMethod("Start");
}

void NextScriptVM::CallUpdate(float deltaTime) {
    if (m_host) m_host->deltaTime = deltaTime;
    RunMethod("Update");
}

void NextScriptVM::CallEvent(const std::string& function) {
    // Engine lifecycle events arrive under their engine names.
    if (function == "on_fixed_update") { RunMethod("FixedUpdate"); return; }
    if (function == "on_late_update") { RunMethod("LateUpdate"); return; }
    if (function == "on_enable") { RunMethod("OnEnable"); return; }
    if (function == "on_disable") { RunMethod("OnDisable"); return; }
    if (function == "on_destroy") { RunMethod("OnDestroy"); return; }
    // Anything else is a public method by name (e.g. a UI button's On Click).
    if (!m_impl->prog) return;
    int idx = m_impl->prog->MethodIndex(function);
    if (idx < 0) return;
    const MethodDecl& m = m_impl->prog->Component()->methods[(std::size_t)idx];
    if (m.isPublic && m.params.empty() && m.lifecycle < 0) RunMethod(function);
}

namespace {
// A script value as an engine value (for the engine, tests and the Inspector).
vs::VsValue ToVs(const Value& v) {
    if (auto* b = std::get_if<bool>(&v)) return vs::VsValue{*b};
    if (auto* i = std::get_if<std::int32_t>(&v)) return vs::VsValue{(float)*i};
    if (auto* f = std::get_if<float>(&v)) return vs::VsValue{*f};
    if (auto* s = std::get_if<std::string>(&v)) return vs::VsValue{*s};
    if (auto* p = std::get_if<Vec3>(&v)) return vs::VsValue{*p};
    // An object (GameObject / Transform / component) is its object.
    if (auto* o = std::get_if<ObjRef>(&v)) return vs::VsValue::Object(o->go);
    return vs::VsValue{};
}

std::string Describe(const vs::VsValue& v) {
    if (v.IsString()) return "text";
    if (v.IsBool()) return "true/false";
    if (v.IsNumber()) return "a number";
    if (v.IsVec3()) return "a Vector3";
    if (v.IsObject()) return "an object";
    return "nothing";
}
// An engine value as a parameter of type `t`, or false (and why) if it isn't one.
bool FromVs(const vs::VsValue& v, const Type& t, const std::string& param, Value& out, std::string& why) {
    auto wrong = [&](const char* needs) { why = "'" + param + "' needs " + needs + ", not " + Describe(v); return false; };
    switch (t.kind) {
        case TK::String: if (!v.IsString()) return wrong("text"); out = Value{v.AsString()}; return true;
        case TK::Bool: if (!v.IsBool()) return wrong("true/false"); out = Value{v.AsBool()}; return true;
        case TK::Float: if (!v.IsNumber()) return wrong("a number"); out = Value{v.AsFloat()}; return true;
        case TK::Int: case TK::Enum: {
            if (!v.IsNumber()) return wrong("a whole number");
            const double d = std::trunc((double)v.AsFloat());
            if (!std::isfinite(d) || d < -2147483648.0 || d > 2147483647.0) return wrong("a whole number that fits an int");
            out = Value{(std::int32_t)d};
            return true;
        }
        case TK::Vector3: if (!v.IsVec3()) return wrong("a Vector3"); out = Value{v.AsVec3()}; return true;
        case TK::Object:   // GameObject or a component: the object's id, resolved where it's used
            if (!v.IsObject()) return wrong("an object");
            out = v.AsObjectId() ? Value{ObjRef{v.AsObjectId(), t.id}} : Value{};
            return true;
        default:
            why = "'" + param + "' can't be given by the engine (only text, numbers, true/false, Vector3 and objects)";
            return false;
    }
}
Value DefaultOf(const Type& t) {
    switch (t.kind) {
        case TK::Bool: return Value{false};
        case TK::Int: case TK::Enum: return Value{std::int32_t{0}};
        case TK::Float: return Value{0.0f};
        case TK::String: return Value{std::string()};
        case TK::Vector3: return Value{Vec3::Zero};
        default: return Value{};
    }
}
} // namespace

void NextScriptVM::CallEventWith(const std::string& function, const vs::VsValue& arg) {
    CallEventArgs(function, std::vector<vs::VsValue>{arg});
}

bool NextScriptVM::HasEvent(const std::string& function) const {
    return m_impl->prog && m_impl->prog->MethodIndex(function) >= 0;
}

vs::VsValue NextScriptVM::CallEventArgs(const std::string& function, const std::vector<vs::VsValue>& given,
                                        std::string* error) {
    if (!m_impl->inst) return {};
    const int idx = m_impl->prog->MethodIndex(function);
    if (idx < 0) { CallEvent(function); return {}; }   // engine lifecycle names, or no such method
    const MethodDecl& m = m_impl->prog->Component()->methods[(std::size_t)idx];
    auto fail = [&](const std::string& msg) {
        if (error) { *error = msg; return; }
        ++m_errorCount;
        m_lastError = msg;
        const GameObject* self = m_impl->inst->Context().self;
        Log::Error("Script on '", self ? self->name : std::string("?"), "', ", msg);
    };
    // As many values as the method takes: extra ones are dropped, missing ones are the type's default.
    std::vector<Value> args;
    for (std::size_t i = 0; i < m.params.size(); ++i) {
        if (i >= given.size()) { args.push_back(DefaultOf(m.params[i].resolved)); continue; }
        Value v;
        std::string why;
        if (!FromVs(given[i], m.params[i].resolved, m.params[i].name, v, why)) { fail(function + "(): " + why); return {}; }
        args.push_back(std::move(v));
    }
    CallContext& c = m_impl->inst->Context();
    if (m_host) {
        c.self = m_host->gameObject;
        c.component = m_host->component;
        c.deltaTime = m_host->deltaTime;
    }
    try {
        return ToVs(m_impl->inst->Call(idx, std::move(args)));
    } catch (const RuntimeError& e) {
        fail(function + "() line " + std::to_string(e.loc.line) + ": " + e.what());
    }
    return {};
}

vs::VsValue NextScriptVM::GetGlobal(const std::string& name) const {
    if (!m_impl->inst) return vs::VsValue{};
    return ToVs(m_impl->inst->GetField(name));
}

void NextScriptVM::SetGlobal(const std::string& name, const vs::VsValue& v) {
    if (!m_impl->inst) return;
    Value x;
    if (v.IsBool()) x = Value{v.AsBool()};
    else if (v.IsString()) x = Value{v.AsString()};
    else if (v.IsVec3()) x = Value{v.AsVec3()};
    else x = Value{v.AsFloat()};
    m_impl->inst->SetField(name, x);
}

} // namespace okay
