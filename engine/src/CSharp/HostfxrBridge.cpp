// The real managed backend: starts .NET through hostfxr, compiles scripts with the developer's SDK,
// and talks to OkayScripting.dll through a handful of UnmanagedCallersOnly functions (plain pointers
// and integers only). Compiled only with OKAY_WITH_CSHARP; otherwise CreateHostfxrBackend says so.
#include "okay/CSharp/CSharpCompiler.hpp"
#include "okay/CSharp/ManagedBridge.hpp"

namespace okay::csharp {

#if defined(OKAY_WITH_CSHARP)
namespace {
// Signatures of OkayScripting.Interop.Host (csharp/OkayScripting/Interop/Host.cs). Same order and types.
using InitFn     = int (*)(OkayNativeApi*);
using CreateFn   = int (*)(const char* asmPath, const char* cls, char* err, int errCap);
using HasFn      = int (*)(int h, const char* name);
using InvokeFn   = int (*)(int h, const char* name, double num, const char* str, char* err, int errCap);
using FieldsFn   = int (*)(int h, char* buf, int cap);
using SetFieldFn = int (*)(int h, const char* name, const char* value, char* err, int errCap);
using OwnerFn    = void (*)(int h, std::uint64_t id);
using DestroyFn  = void (*)(int h);

struct Entry {
    InitFn init = nullptr; CreateFn create = nullptr; HasFn has = nullptr; InvokeFn invoke = nullptr;
    FieldsFn fields = nullptr; SetFieldFn setField = nullptr; OwnerFn owner = nullptr; DestroyFn destroy = nullptr;
};

class Script : public IManagedScript {
public:
    Script(const Entry* e, int h) : m_e(e), m_h(h) {}
    ~Script() override { if (m_h > 0) m_e->destroy(m_h); }
    void SetOwner(std::uint64_t id) override { m_e->owner(m_h, id); }
    bool Has(const std::string& m) override { return m_e->has(m_h, m.c_str()) != 0; }
    bool Invoke(const std::string& m, double num, const std::string& str, std::string* error) override {
        char err[1024]; err[0] = 0;
        int rc = m_e->invoke(m_h, m.c_str(), num, str.c_str(), err, (int)sizeof err);
        if (rc != 0 && error) *error = err;
        return rc == 0;
    }
    std::string Fields() override {
        int need = m_e->fields(m_h, nullptr, 0);
        if (need <= 0) return {};
        std::string s((std::size_t)need + 1, '\0');
        int got = m_e->fields(m_h, &s[0], need + 1);
        s.resize((std::size_t)(got < need ? got : need));
        return s;
    }
    bool SetField(const std::string& n, const std::string& v, std::string* error) override {
        char err[512]; err[0] = 0;
        int rc = m_e->setField(m_h, n.c_str(), v.c_str(), err, (int)sizeof err);
        if (rc != 0 && error) *error = err;
        return rc == 0;
    }
private:
    const Entry* m_e; int m_h;
};

class Backend : public IManagedBackend {
public:
    bool Init(std::string* error) {
        DotnetEnv env = CurrentDotnetEnv();
        m_env = env;
        std::string srcDir = "csharp/OkayScripting";
        m_api = EnsureApiAssembly(srcDir, DefaultCacheDir(), env, error);
        if (m_api.empty()) return false;
        DotnetRuntime* rt = DotnetRuntime::Get(env, error);
        if (!rt) return false;
        const char* T = "OkayScripting.Interop.Host, OkayScripting";
        auto get = [&](const char* m) { return rt->GetFunction(m_api, T, m, error); };
        m_e.init = (InitFn)get("Init"); m_e.create = (CreateFn)get("Create"); m_e.has = (HasFn)get("Has");
        m_e.invoke = (InvokeFn)get("Invoke"); m_e.fields = (FieldsFn)get("Fields"); m_e.setField = (SetFieldFn)get("SetField");
        m_e.owner = (OwnerFn)get("SetOwner"); m_e.destroy = (DestroyFn)get("Destroy");
        if (!m_e.init || !m_e.create || !m_e.has || !m_e.invoke || !m_e.fields || !m_e.setField || !m_e.owner || !m_e.destroy) return false;
        if (m_e.init(GetNativeApi()) != 0) { if (error) *error = "OkayScripting.dll rejected the engine's function table (version mismatch)."; return false; }
        return true;
    }
    bool Compile(const std::string& source, std::string* path, std::vector<std::string>* errors) override {
        CompileResult r = CompileScript(source, DefaultCacheDir(), m_api, m_env);
        if (r.ok) { *path = r.assemblyPath; return true; }
        for (auto& d : r.diags) if (d.error) errors->push_back(DiagText(d));
        if (errors->empty()) errors->push_back(r.message.empty() ? "the script did not compile" : r.message);
        return false;
    }
    std::unique_ptr<IManagedScript> Create(const std::string& asmPath, const std::string& cls, std::string* error) override {
        char err[1024]; err[0] = 0;
        int h = m_e.create(asmPath.c_str(), cls.c_str(), err, (int)sizeof err);
        if (h <= 0) { if (error) *error = err[0] ? err : "could not create the script"; return nullptr; }
        return std::make_unique<Script>(&m_e, h);
    }
private:
    DotnetEnv m_env; std::string m_api; Entry m_e;
};
}  // namespace

IManagedBackend* CreateHostfxrBackend(std::string* error) {
    static Backend b;
    static bool tried = false, ok = false;
    static std::string err;
    if (!tried) { tried = true; ok = b.Init(&err); }
    if (!ok) { if (error) *error = err; return nullptr; }
    return &b;
}
#else
IManagedBackend* CreateHostfxrBackend(std::string* error) {
    if (error) *error = NotEnabledMessage();
    return nullptr;
}
#endif

} // namespace okay::csharp
