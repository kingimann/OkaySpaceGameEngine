#include "okay/Scripting/NextScriptVM.hpp"
#include "ScriptInternal.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/GameObject.hpp"
#include <set>

namespace okay {

using namespace script;

struct NextScriptVM::Impl {
    std::shared_ptr<Program> prog;
    std::unique_ptr<Instance> inst;
};

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
    prog->statics = SharedStatics(source);
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

vs::VsValue NextScriptVM::GetGlobal(const std::string& name) const {
    if (!m_impl->inst) return vs::VsValue{};
    Value v = m_impl->inst->GetField(name);
    if (auto* b = std::get_if<bool>(&v)) return vs::VsValue{*b};
    if (auto* i = std::get_if<std::int32_t>(&v)) return vs::VsValue{(float)*i};
    if (auto* f = std::get_if<float>(&v)) return vs::VsValue{*f};
    if (auto* s = std::get_if<std::string>(&v)) return vs::VsValue{*s};
    if (auto* p = std::get_if<Vec3>(&v)) return vs::VsValue{*p};
    // An object field (GameObject / Transform / component) is its object.
    if (auto* o = std::get_if<ObjRef>(&v)) return vs::VsValue::Object(o->go);
    return vs::VsValue{};
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
