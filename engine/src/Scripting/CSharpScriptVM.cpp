#include "okay/Scripting/CSharpScriptVM.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Input/Input.hpp"

#include <mono/jit/jit.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/metadata/mono-config.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace okay {

namespace fs = std::filesystem;

// ---- Shared Mono runtime (one JIT domain for the whole process) -------
namespace {
MonoDomain* g_domain = nullptr;
ScriptHost* g_currentHost = nullptr; // set around each managed call

Transform* HostTransform() { return g_currentHost ? g_currentHost->transform : nullptr; }

// Internal calls bound into the managed `Okay` class.
void cs_Move(float x, float y)   { if (auto* t = HostTransform()) t->Translate({x, y, 0}); }
void cs_SetPos(float x, float y) { if (auto* t = HostTransform()) t->localPosition = {x, y, 0}; }
void cs_Rotate(float deg)        { if (auto* t = HostTransform()) t->Rotate({0, 0, deg}); }
float cs_PosX()  { auto* t = HostTransform(); return t ? t->localPosition.x : 0.0f; }
float cs_PosY()  { auto* t = HostTransform(); return t ? t->localPosition.y : 0.0f; }
float cs_Time()  { return Time::ElapsedTime(); }
float cs_Dt()    { return g_currentHost ? g_currentHost->deltaTime : 0.0f; }
float cs_AxisX() { return Input::AxisWASD().x; }
float cs_AxisY() { return Input::AxisWASD().y; }
int   cs_Key(MonoString* s) {
    char* u = mono_string_to_utf8(s);
    bool down = u && u[0] && Input::GetKey(u[0]);
    if (u) mono_free(u);
    return down ? 1 : 0;
}
void cs_Print(MonoString* s) {
    char* u = mono_string_to_utf8(s);
    Log::Info("[csharp] ", u ? u : "");
    if (u) mono_free(u);
}
void cs_SetVar(MonoString* k, float v) {
    if (!g_currentHost) return;
    char* u = mono_string_to_utf8(k);
    if (u) { g_currentHost->globals[u] = v; mono_free(u); }
}
float cs_GetVar(MonoString* k) {
    if (!g_currentHost) return 0.0f;
    char* u = mono_string_to_utf8(k);
    float out = 0.0f;
    if (u) {
        auto it = g_currentHost->globals.find(u);
        if (it != g_currentHost->globals.end()) out = it->second.AsFloat();
        mono_free(u);
    }
    return out;
}

void EnsureRuntime() {
    if (g_domain) return;
    g_domain = mono_jit_init("okayspace");
    mono_add_internal_call("Okay::Move",   (const void*)cs_Move);
    mono_add_internal_call("Okay::SetPos", (const void*)cs_SetPos);
    mono_add_internal_call("Okay::Rotate", (const void*)cs_Rotate);
    mono_add_internal_call("Okay::PosX",   (const void*)cs_PosX);
    mono_add_internal_call("Okay::PosY",   (const void*)cs_PosY);
    mono_add_internal_call("Okay::Time",   (const void*)cs_Time);
    mono_add_internal_call("Okay::Dt",     (const void*)cs_Dt);
    mono_add_internal_call("Okay::AxisX",  (const void*)cs_AxisX);
    mono_add_internal_call("Okay::AxisY",  (const void*)cs_AxisY);
    mono_add_internal_call("Okay::Key",    (const void*)cs_Key);
    mono_add_internal_call("Okay::Print",  (const void*)cs_Print);
    mono_add_internal_call("Okay::SetVar", (const void*)cs_SetVar);
    mono_add_internal_call("Okay::GetVar", (const void*)cs_GetVar);
}

// The managed host API, prepended to every user script before compilation.
const char* kPrelude = R"CS(
using System.Runtime.CompilerServices;
public static class Okay {
    [MethodImpl(MethodImplOptions.InternalCall)] public static extern void Move(float x, float y);
    [MethodImpl(MethodImplOptions.InternalCall)] public static extern void SetPos(float x, float y);
    [MethodImpl(MethodImplOptions.InternalCall)] public static extern void Rotate(float deg);
    [MethodImpl(MethodImplOptions.InternalCall)] public static extern float PosX();
    [MethodImpl(MethodImplOptions.InternalCall)] public static extern float PosY();
    [MethodImpl(MethodImplOptions.InternalCall)] public static extern float Time();
    [MethodImpl(MethodImplOptions.InternalCall)] public static extern float Dt();
    [MethodImpl(MethodImplOptions.InternalCall)] public static extern float AxisX();
    [MethodImpl(MethodImplOptions.InternalCall)] public static extern float AxisY();
    [MethodImpl(MethodImplOptions.InternalCall)] public static extern bool Key(string k);
    [MethodImpl(MethodImplOptions.InternalCall)] public static extern void Print(string s);
    [MethodImpl(MethodImplOptions.InternalCall)] public static extern void SetVar(string k, float v);
    [MethodImpl(MethodImplOptions.InternalCall)] public static extern float GetVar(string k);
}
)CS";
} // namespace

// ---- Per-script state -------------------------------------------------
struct CSharpScriptVM::State {
    MonoAssembly* assembly = nullptr;
    MonoImage*    image    = nullptr;
    MonoClass*    klass    = nullptr;
    MonoMethod*   startMethod  = nullptr;
    MonoMethod*   updateMethod = nullptr;
    fs::path      dllPath;
};

CSharpScriptVM::CSharpScriptVM() { m_state = new State(); EnsureRuntime(); }

CSharpScriptVM::~CSharpScriptVM() {
    if (m_state) {
        std::error_code ec;
        if (!m_state->dllPath.empty()) fs::remove(m_state->dllPath, ec);
        delete m_state;
    }
}

void CSharpScriptVM::Bind(ScriptHost* host) { m_host = host; }

bool CSharpScriptVM::Load(const std::string& source, std::string* error) {
    if (!g_domain) { if (error) *error = "Mono runtime unavailable"; return false; }

    // Write prelude + user source to a temp .cs and compile it with mcs.
    fs::path tmp = fs::temp_directory_path();
    fs::path cs  = tmp / ("okay_script_" + std::to_string((std::uintptr_t)this) + ".cs");
    fs::path dll = tmp / ("okay_script_" + std::to_string((std::uintptr_t)this) + ".dll");
    {
        std::ofstream f(cs);
        if (!f) { if (error) *error = "cannot write temp script"; return false; }
        f << kPrelude << "\n" << source << "\n";
    }

    std::string cmd = "mcs -nologo -target:library -out:\"" + dll.string() +
                      "\" \"" + cs.string() + "\" 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    std::string out;
    if (pipe) {
        char buf[256];
        while (fgets(buf, sizeof(buf), pipe)) out += buf;
    }
    int rc = pipe ? pclose(pipe) : -1;
    std::error_code ec; fs::remove(cs, ec);

    if (rc != 0 || !fs::exists(dll)) {
        if (error) *error = "C# compilation failed: " + out;
        Log::Error("C#: ", out);
        return false;
    }

    m_state->dllPath  = dll;
    m_state->assembly = mono_domain_assembly_open(g_domain, dll.string().c_str());
    if (!m_state->assembly) { if (error) *error = "failed to load compiled assembly"; return false; }
    m_state->image = mono_assembly_get_image(m_state->assembly);
    m_state->klass = mono_class_from_name(m_state->image, "", "Script");
    if (!m_state->klass) { if (error) *error = "script must define 'public static class Script'"; return false; }
    m_state->startMethod  = mono_class_get_method_from_name(m_state->klass, "Start", 0);
    m_state->updateMethod = mono_class_get_method_from_name(m_state->klass, "Update", 1);
    m_loaded = true;
    return true;
}

void CSharpScriptVM::CallStart() {
    if (!m_loaded || !m_state->startMethod) return;
    g_currentHost = m_host;
    MonoObject* exc = nullptr;
    mono_runtime_invoke(m_state->startMethod, nullptr, nullptr, &exc);
    if (exc) Log::Error("C# Start() threw an exception");
    g_currentHost = nullptr;
}

void CSharpScriptVM::CallUpdate(float deltaTime) {
    if (m_host) m_host->deltaTime = deltaTime;
    if (!m_loaded || !m_state->updateMethod) return;
    g_currentHost = m_host;
    void* args[1] = { &deltaTime };
    MonoObject* exc = nullptr;
    mono_runtime_invoke(m_state->updateMethod, nullptr, args, &exc);
    if (exc) Log::Error("C# Update() threw an exception");
    g_currentHost = nullptr;
}

vs::VsValue CSharpScriptVM::GetGlobal(const std::string& name) const {
    // C# scripts communicate results through the shared blackboard (Okay.SetVar).
    if (m_host) {
        auto it = m_host->globals.find(name);
        if (it != m_host->globals.end()) return it->second;
    }
    return {};
}

} // namespace okay
