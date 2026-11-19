// C# through the hosted .NET runtime: the IScriptVM implementation and the native function table the
// managed side calls (OkayNativeApi). Written for this engine; the managed side is csharp/OkayScripting.
#include "okay/CSharp/CSharpScriptVM.hpp"
#include "okay/CSharp/CSharpCompiler.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <map>
#include <sstream>

namespace okay {
using namespace csharp;

namespace {
ScriptHost* g_host = nullptr;   // the script running right now (set around every managed call)

Scene* CurScene() { return g_host && g_host->gameObject ? g_host->gameObject->scene() : nullptr; }
GameObject* Go(std::uint64_t id) { return id ? GameObject::FromInstanceId(id) : nullptr; }
int CopyOut(const std::string& s, char* buf, int cap) {
    if (!buf || cap <= 0) return (int)s.size();
    int n = (int)s.size() < cap - 1 ? (int)s.size() : cap - 1;
    std::memcpy(buf, s.data(), (std::size_t)n); buf[n] = 0;
    return (int)s.size();
}

extern "C" {
void N_Log(int level, const char* msg) {
    std::string m = msg ? msg : "";
    if (level >= 2) Log::Error("[script] ", m); else if (level == 1) Log::Warning("[script] ", m); else Log::Info("[script] ", m);
}
int N_GetVec(std::uint64_t id, int what, float* o) {
    GameObject* g = Go(id); if (!g || !o) return 0;
    Transform* t = g->transform;
    if (!t) return 0;
    switch (what) {
        case 0: { Vec3 v = t->Position(); o[0] = v.x; o[1] = v.y; o[2] = v.z; return 1; }
        case 1: { Vec3 v = t->localPosition; o[0] = v.x; o[1] = v.y; o[2] = v.z; return 1; }
        case 2: { Vec3 v = t->EulerAngles(); o[0] = v.x; o[1] = v.y; o[2] = v.z; return 1; }
        case 3: { Vec3 v = t->LossyScale(); o[0] = v.x; o[1] = v.y; o[2] = v.z; return 1; }
        case 4: { Quat q = t->Rotation(); o[0] = q.x; o[1] = q.y; o[2] = q.z; o[3] = q.w; return 1; }
        case 5: { Vec3 v = t->LocalEulerAngles(); o[0] = v.x; o[1] = v.y; o[2] = v.z; return 1; }
        default: return 0;
    }
}
int N_SetVec(std::uint64_t id, int what, const float* in) {
    GameObject* g = Go(id); if (!g || !in) return 0;
    Transform* t = g->transform;
    if (!t) return 0;
    switch (what) {
        case 0: t->SetPosition(Vec3{in[0], in[1], in[2]}); return 1;
        case 1: t->localPosition = Vec3{in[0], in[1], in[2]}; return 1;
        case 2: t->SetEulerAngles(Vec3{in[0], in[1], in[2]}); return 1;
        case 3: t->SetScale(Vec3{in[0], in[1], in[2]}); return 1;
        case 4: { Quat q; q.x = in[0]; q.y = in[1]; q.z = in[2]; q.w = in[3]; t->SetRotation(q.Normalized()); return 1; }
        case 5: t->SetLocalEulerAngles(Vec3{in[0], in[1], in[2]}); return 1;
        default: return 0;
    }
}
double N_GetTime(int what) {
    switch (what) {
        case 0: return g_host && g_host->deltaTime > 0 ? g_host->deltaTime : Time::DeltaTime();
        case 1: return Time::ElapsedTime();
        case 2: return Time::TimeScale();
        case 3: return Time::UnscaledDeltaTime();
        case 4: return Time::FixedDeltaTime();
        case 5: return (double)Time::FrameCount();
        default: return 0;
    }
}
void N_SetTimeScale(float s) { Time::SetTimeScale(s); }
int N_InputKey(const char* name, int mode) {
    std::string n = name ? name : "";
    if (!Input::IsKnownKeyName(n)) return -1;
    char k = Input::KeyFromName(n);
    return (mode == 1 ? Input::GetKeyDownGame(k) : mode == 2 ? Input::GetKeyUp(k) : Input::GetKeyGame(k)) ? 1 : 0;
}
int N_InputMouse(int b, int mode) {
    return (mode == 1 ? Input::GetMouseButtonDown(b) : mode == 2 ? Input::GetMouseButtonUp(b) : Input::GetMouseButton(b)) ? 1 : 0;
}
void N_MousePosition(float* o) { Vec2 p = Input::MousePosition(); o[0] = p.x; o[1] = p.y; }
float N_InputAxis(const char* name) {
    std::string n = name ? name : "";
    Vec2 v = Input::AxisWASD();
    return n == "Horizontal" ? v.x : n == "Vertical" ? v.y : 0.0f;
}
std::uint64_t N_Find(const char* name) {
    Scene* s = CurScene(); if (!s || !name) return 0;
    GameObject* g = s->Find(name);
    return g ? g->InstanceId() : 0;
}
std::uint64_t N_Create(const char* name) {
    Scene* s = CurScene(); if (!s) return 0;
    GameObject* g = s->CreateGameObject(name ? name : "GameObject");
    return g ? g->InstanceId() : 0;
}
void N_Destroy(std::uint64_t id, float delay) {
    GameObject* g = Go(id); Scene* s = g ? g->scene() : nullptr; if (!s) return;
    if (delay > 0) s->Destroy(g, delay); else s->Destroy(g);
}
std::uint64_t N_Instantiate(std::uint64_t prefab, const float* p, const float* r) {
    GameObject* src = Go(prefab); Scene* s = src ? src->scene() : CurScene(); if (!src || !s) return 0;
    GameObject* g = nullptr;
    if (p && r) { Quat q; q.x = r[0]; q.y = r[1]; q.z = r[2]; q.w = r[3]; g = s->Instantiate(*src, Vec3{p[0], p[1], p[2]}, q.Normalized()); }
    else if (p) g = s->Instantiate(*src, Vec3{p[0], p[1], p[2]});
    else g = s->Instantiate(*src);
    return g ? g->InstanceId() : 0;
}
int N_IsAlive(std::uint64_t id) { return Go(id) ? 1 : 0; }
int N_GetName(std::uint64_t id, char* buf, int cap) { GameObject* g = Go(id); return g ? CopyOut(g->name, buf, cap) : -1; }
int N_GetActive(std::uint64_t id) { GameObject* g = Go(id); return g && g->active ? 1 : 0; }
void N_SetActive(std::uint64_t id, int a) { if (GameObject* g = Go(id)) g->active = a != 0; }
int N_Raycast(const float* o, const float* d, float maxD, float* pt, float* nm, float* dist, std::uint64_t* go) {
    Scene* s = CurScene(); if (!s || !o || !d) return 0;
    RaycastHit3D h = s->physics3D().Raycast(*s, Vec3{o[0], o[1], o[2]}, Vec3{d[0], d[1], d[2]}, maxD > 0 ? maxD : 1e9f);
    if (!h.hit) return 0;
    if (pt) { pt[0] = h.point.x; pt[1] = h.point.y; pt[2] = h.point.z; }
    if (nm) { nm[0] = h.normal.x; nm[1] = h.normal.y; nm[2] = h.normal.z; }
    if (dist) *dist = h.distance;
    if (go) *go = h.gameObject ? h.gameObject->InstanceId() : 0;
    return 1;
}
int N_HasComponent(std::uint64_t id, const char* type) {
    GameObject* g = Go(id); if (!g || !type) return 0;
    std::string t = type;
    if (t == "Transform") return 1;
    for (auto& c : g->Components()) { auto info = PropertyRegistry::FindFor(*c); if (info && info->name == t) return 1; }
    return 0;
}
int N_GetProperty(std::uint64_t id, const char* path, char* buf, int cap) {
    GameObject* g = Go(id); if (!g || !path) return -1;
    PropertyValue pv; std::string err;
    if (!PropertyRegistry::TryGetProperty(*g, path, pv, &err)) return -1;
    return CopyOut(PropertyRegistry::GetPropertyText(*g, path), buf, cap);
}
int N_SetProperty(std::uint64_t id, const char* path, const char* text) {
    GameObject* g = Go(id); if (!g || !path || !text) return 0;
    std::string err;
    return PropertyRegistry::SetPropertyFromText(*g, path, text, &err) ? 1 : 0;
}
}  // extern "C"

std::string FormatNum(double d) {
    char b[64]; std::snprintf(b, sizeof b, "%.9g", d); return b;
}
}  // namespace

namespace csharp {
OkayNativeApi* GetNativeApi() {
    static OkayNativeApi api = {
        (std::uint32_t)sizeof(OkayNativeApi), kNativeApiVersion,
        N_Log, N_GetVec, N_SetVec, N_GetTime, N_SetTimeScale, N_InputKey, N_InputMouse, N_MousePosition, N_InputAxis,
        N_Find, N_Create, N_Destroy, N_Instantiate, N_IsAlive, N_GetName, N_GetActive, N_SetActive, N_Raycast,
        N_HasComponent, N_GetProperty, N_SetProperty};
    return &api;
}

static IManagedBackend* g_testBackend = nullptr;
void SetManagedBackendForTests(IManagedBackend* b) { g_testBackend = b; }
IManagedBackend* GetManagedBackend(std::string* error) {
    if (g_testBackend) return g_testBackend;
    static IManagedBackend* real = nullptr;
    static bool tried = false;
    static std::string err;
    if (!tried) { tried = true; real = CreateHostfxrBackend(&err); }
    if (!real && error) *error = err.empty() ? NotEnabledMessage() : err;
    return real;
}
}  // namespace csharp

// ------------------------------------------------------------------------------------------------
struct CSharpScriptVM::Impl {
    ScriptHost* host = nullptr;
    std::unique_ptr<IManagedScript> script;
    std::string className, assembly, source;
    double budgetMs = 10.0;
    int overBudget = 0;
    std::map<std::string, int> hasCache;           // method -> 0/1
    std::map<std::string, int> errorRepeats;       // error text -> times seen
    std::string lastFields;

    std::string Where() const { return className.empty() ? std::string("script") : className; }

    void ReportError(const std::string& method, const std::string& err, std::string* out) {
        std::string line = Where() + "." + method + ": " + err;
        if (out) { *out = line; return; }
        int n = ++errorRepeats[line];
        if (n == 1) Log::Error("[script] ", line);
        else if (n % 300 == 0) Log::Error("[script] ", line, " (repeated ", std::to_string(n), " times)");
    }
    bool Has(const std::string& method) {
        if (!script) return false;
        auto it = hasCache.find(method);
        if (it != hasCache.end()) return it->second != 0;
        bool h = script->Has(method);
        hasCache[method] = h ? 1 : 0;
        return h;
    }
    bool Call(const std::string& method, double num, const std::string& str, std::string* errOut = nullptr) {
        if (!script) return false;
        if (!Has(method)) return true;
        ScriptHost* prev = g_host; g_host = host;
        auto t0 = std::chrono::steady_clock::now();
        std::string err;
        bool ok = script->Invoke(method, num, str, &err);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        g_host = prev;
        if (!ok) ReportError(method, err.empty() ? "the script failed" : err, errOut);
        if (ms > budgetMs) {
            ++overBudget;
            if (overBudget == 1 || overBudget % 60 == 0) {
                char b[64]; std::snprintf(b, sizeof b, "%.1f", ms);
                Log::Warning("[script] ", Where(), ".", method, " took ", std::string(b), " ms (budget ", FormatNum(budgetMs),
                             " ms). C# scripts cannot be stopped in the middle of a call, so a loop that never ends freezes the game; keep per-frame work short.");
            }
        }
        return ok;
    }
    struct Field { std::string name, type, value; };
    std::vector<Field> ParseFields() const {
        std::vector<Field> out;
        if (!script) return out;
        std::stringstream ss(script->Fields()); std::string line;
        while (std::getline(ss, line)) {
            std::size_t a = line.find('\t'); if (a == std::string::npos) continue;
            std::size_t b = line.find('\t', a + 1); if (b == std::string::npos) continue;
            out.push_back({line.substr(0, a), line.substr(a + 1, b - a - 1), line.substr(b + 1)});
        }
        return out;
    }
};

CSharpScriptVM::CSharpScriptVM() : m_impl(new Impl) {}
CSharpScriptVM::~CSharpScriptVM() = default;
void CSharpScriptVM::SetBudgetMs(double ms) { m_impl->budgetMs = ms; }
int CSharpScriptVM::OverBudgetCount() const { return m_impl->overBudget; }

std::vector<std::string> CSharpScriptVM::BuiltinNames() const {
    return {"Debug.Log", "Debug.LogWarning", "Debug.LogError", "Time.deltaTime", "Time.time", "Input.GetKey", "Input.GetKeyDown",
            "Physics.Raycast", "GameObject.Find", "Object.Destroy", "Object.Instantiate", "Mathf.Clamp", "Mathf.Lerp",
            "Vector3", "Quaternion", "transform", "gameObject", "GetComponent"};
}

std::string CSharpScriptVM::ManagedMethodName(const std::string& ev) {
    if (ev.empty()) return ev;
    if (ev.find('_') == std::string::npos && !std::islower((unsigned char)ev[0])) return ev;   // already Pascal
    std::string pas; bool up = true;
    for (char c : ev) { if (c == '_') { up = true; continue; } pas += up ? (char)std::toupper((unsigned char)c) : c; up = false; }
    static const char* const kPlain[] = {"Awake", "Start", "Update", "FixedUpdate", "LateUpdate"};
    for (const char* p : kPlain) {
        if (pas == p) return pas;
        if (pas == std::string("On") + p) return p;
    }
    return pas.rfind("On", 0) == 0 ? pas : "On" + pas;
}

bool CSharpScriptVM::Load(const std::string& source, std::string* error) {
    std::string err;
    IManagedBackend* be = GetManagedBackend(&err);
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    if (!be) return fail(err);
    std::string cls = DetectScriptClass(source);
    if (cls.empty()) return fail("line 1: no class that inherits from Behaviour was found. Write: public class MyScript : Behaviour { }");
    std::string asmPath; std::vector<std::string> errs;
    if (!be->Compile(source, &asmPath, &errs)) {
        std::string all; for (auto& e : errs) all += (all.empty() ? "" : "\n") + e;
        return fail(all.empty() ? "the C# script did not compile" : all);
    }
    auto inst = be->Create(asmPath, cls, &err);
    if (!inst) return fail(err.empty() ? "could not create the script " + cls : err);
    m_impl->script = std::move(inst);
    m_impl->className = cls; m_impl->assembly = asmPath; m_impl->source = source;
    m_impl->hasCache.clear(); m_impl->errorRepeats.clear();
    if (m_impl->host && m_impl->host->gameObject) m_impl->script->SetOwner(m_impl->host->gameObject->InstanceId());
    return true;
}

bool CSharpScriptVM::Validate(const std::string& source, std::string* error) {
    auto d = ValidateAll(source);
    for (auto& x : d) if (x.severity == 0) { if (error) *error = "line " + std::to_string(x.line) + ": " + x.message; return false; }
    return true;
}

std::vector<ScriptDiagnostic> CSharpScriptVM::ValidateAll(const std::string& source) {
    std::vector<ScriptDiagnostic> out;
    {   std::string why; if (!GetManagedBackend(&why)) { out.push_back({1, why, 0, 1}); return out; } }
    if (DetectScriptClass(source).empty()) {
        out.push_back({1, "no class that inherits from Behaviour was found", 0, 1});
        return out;
    }
    // Only what a build already said (compile-on-save fills the cache); building here would block typing.
    std::string cache = DefaultCacheDir();
    std::string h = ContentHash(source);
    std::error_code ec;
    std::ifstream f(std::filesystem::path(cache) / h / "errors.txt", std::ios::binary);
    if (f) {
        std::stringstream ss; ss << f.rdbuf();
        for (auto& d : ParseBuildOutput(ss.str())) out.push_back({d.line, d.message + " (" + d.code + ")", d.error ? 0 : 1, d.column});
    }
    return out;
}

bool CSharpScriptVM::HotReload(const std::string& source, std::string* error) {
    auto fresh = std::make_unique<CSharpScriptVM>();
    fresh->m_impl->host = m_impl->host;
    fresh->m_impl->budgetMs = m_impl->budgetMs;
    if (!fresh->Load(source, error)) return false;           // the running script is left alone
    for (auto& f : m_impl->ParseFields()) {                  // keep public field values both versions have
        std::string e;
        fresh->m_impl->script->SetField(f.name, f.value, &e);
    }
    m_impl->script = std::move(fresh->m_impl->script);       // the old instance (and its load context) is released here
    m_impl->className = fresh->m_impl->className; m_impl->assembly = fresh->m_impl->assembly; m_impl->source = source;
    m_impl->hasCache.clear();
    return true;
}

void CSharpScriptVM::Bind(ScriptHost* host) {
    m_impl->host = host;
    if (m_impl->script && host && host->gameObject) m_impl->script->SetOwner(host->gameObject->InstanceId());
}
void CSharpScriptVM::CallStart() {
    for (const char* n : {"Awake", "OnEnable", "Start"}) m_impl->Call(n, 0, "");
}
void CSharpScriptVM::CallUpdate(float dt) { m_impl->Call("Update", dt, ""); }
void CSharpScriptVM::CallEvent(const std::string& f) { m_impl->Call(ManagedMethodName(f), 0, ""); }
void CSharpScriptVM::CallEventWith(const std::string& f, const vs::VsValue& arg) {
    m_impl->Call(ManagedMethodName(f), arg.IsNumber() ? arg.AsFloat() : 0, arg.IsString() ? arg.AsString() : std::string());
}
vs::VsValue CSharpScriptVM::CallEventArgs(const std::string& f, const std::vector<vs::VsValue>& args, std::string* error) {
    const vs::VsValue a = args.empty() ? vs::VsValue{} : args[0];
    m_impl->Call(ManagedMethodName(f), a.IsNumber() ? a.AsFloat() : 0, a.IsString() ? a.AsString() : std::string(), error);
    return vs::VsValue{};
}
bool CSharpScriptVM::HasEvent(const std::string& f) const { return m_impl->Has(ManagedMethodName(f)); }

vs::VsValue CSharpScriptVM::GetGlobal(const std::string& name) const {
    for (auto& f : m_impl->ParseFields()) {
        if (f.name != name) continue;
        if (f.type == "float" || f.type == "int" || f.type == "double") return vs::VsValue((float)std::atof(f.value.c_str()));
        if (f.type == "bool") return vs::VsValue(f.value == "true" || f.value == "True" || f.value == "1");
        if (f.type == "Vector3") {
            float x = 0, y = 0, z = 0; std::sscanf(f.value.c_str(), "%f,%f,%f", &x, &y, &z);
            return vs::VsValue(Vec3{x, y, z});
        }
        return vs::VsValue(f.value);
    }
    return vs::VsValue{};
}
void CSharpScriptVM::SetGlobal(const std::string& name, const vs::VsValue& v) {
    if (!m_impl->script) return;
    std::string text;
    if (v.IsBool()) text = v.AsBool() ? "true" : "false";
    else if (v.IsNumber()) text = FormatNum(v.AsFloat());
    else if (v.IsString()) text = v.AsString();
    else if (v.IsVec3()) { Vec3 x = v.AsVec3(); text = FormatNum(x.x) + "," + FormatNum(x.y) + "," + FormatNum(x.z); }
    else return;
    std::string e;
    m_impl->script->SetField(name, text, &e);   // engine names (other, contact_point ...) are not fields: ignored
}
std::vector<std::string> CSharpScriptVM::GlobalNames() const {
    std::vector<std::string> n;
    for (auto& f : m_impl->ParseFields()) n.push_back(f.name);
    return n;
}

} // namespace okay
