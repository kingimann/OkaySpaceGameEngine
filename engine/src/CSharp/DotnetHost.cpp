#include "okay/CSharp/DotnetHost.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <tuple>
#include <cstdio>

#if defined(OKAY_WITH_CSHARP)
  #ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
  #else
    #include <dlfcn.h>
  #endif
#endif

namespace okay::csharp {
namespace fs = std::filesystem;

static std::string Env(const char* n) { const char* v = std::getenv(n); return v ? v : ""; }

DotnetEnv CurrentDotnetEnv(const std::string& gameDir) {
    DotnetEnv e;
    e.dotnetRoot = Env("DOTNET_ROOT");
    e.gameDir = gameDir;
#ifdef _WIN32
    e.windows = true;
    e.programFiles = Env("ProgramFiles");
    e.programFilesX86 = Env("ProgramFiles(x86)");
    e.home = Env("USERPROFILE");
#else
    e.home = Env("HOME");
  #ifdef __APPLE__
    e.mac = true;
  #endif
#endif
    return e;
}

std::vector<std::string> RuntimeSearchRoots(const DotnetEnv& env) {
    std::vector<std::string> r;
    auto add = [&](const std::string& p) { if (!p.empty() && std::find(r.begin(), r.end(), p) == r.end()) r.push_back(p); };
    if (!env.gameDir.empty()) add(env.gameDir + "/runtime");     // shipped with an exported game
    add(env.dotnetRoot);
    if (env.windows) {
        if (!env.programFiles.empty()) add(env.programFiles + "/dotnet");
        else add("C:/Program Files/dotnet");
        if (!env.programFilesX86.empty()) add(env.programFilesX86 + "/dotnet");
    } else if (env.mac) {
        add("/usr/local/share/dotnet");
        add("/opt/homebrew/opt/dotnet/libexec");
    } else {
        add("/usr/share/dotnet");
        add("/usr/lib/dotnet");
        add("/usr/lib64/dotnet");
    }
    if (!env.home.empty()) add(env.home + "/.dotnet");
    return r;
}

DotnetVersion ParseDotnetVersion(const std::string& t) {
    DotnetVersion v;
    std::size_t i = 0;
    auto num = [&](int& out) {
        if (i >= t.size() || t[i] < '0' || t[i] > '9') return false;
        long n = 0;
        while (i < t.size() && t[i] >= '0' && t[i] <= '9') { n = n * 10 + (t[i] - '0'); if (n > 100000) return false; ++i; }
        out = (int)n; return true;
    };
    if (!num(v.major)) return v;
    if (i < t.size() && t[i] == '.') { ++i; if (!num(v.minor)) return v; } else return v;
    if (i < t.size() && t[i] == '.') { ++i; if (!num(v.patch)) return v; } else return v;
    if (i < t.size()) { if (t[i] != '-') return v; v.preview = true; }
    v.ok = true;
    return v;
}

std::string PickRuntimeVersion(const std::vector<std::string>& installed, int minMajor) {
    std::string best; DotnetVersion bv;
    for (auto& s : installed) {
        DotnetVersion v = ParseDotnetVersion(s);
        if (!v.ok || v.major < minMajor) continue;
        bool better = !bv.ok;
        if (!better) {
            auto key = [](const DotnetVersion& x) { return std::make_tuple(x.major, x.minor, x.patch, x.preview ? 0 : 1); };
            better = key(v) > key(bv);
        }
        if (better) { best = s; bv = v; }
    }
    return best;
}

std::string MakeRuntimeConfigJson(const std::string& frameworkVersion) {
    return "{\n  \"runtimeOptions\": {\n    \"tfm\": \"net" + [&] {
        DotnetVersion v = ParseDotnetVersion(frameworkVersion);
        return std::to_string(v.ok ? v.major : 8) + ".0";
    }() + "\",\n    \"framework\": {\n      \"name\": \"Microsoft.NETCore.App\",\n      \"version\": \"" +
        frameworkVersion + "\"\n    },\n    \"configProperties\": {\n      \"System.Runtime.TieredPGO\": false\n    }\n  }\n}\n";
}

std::string HostfxrFileName(const DotnetEnv& env) {
    if (env.windows) return "hostfxr.dll";
    if (env.mac) return "libhostfxr.dylib";
    return "libhostfxr.so";
}

static std::vector<std::string> SubDirs(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_directory(ec)) out.push_back(it->path().filename().string());
    return out;
}

std::vector<std::string> ListInstalledRuntimes(const std::string& root) {
    return SubDirs(root + "/shared/Microsoft.NETCore.App");
}

std::string FindHostfxr(const DotnetEnv& env, std::string* foundVersion) {
    for (auto& root : RuntimeSearchRoots(env)) {
        std::string ver = PickRuntimeVersion(SubDirs(root + "/host/fxr"), 6);
        if (ver.empty()) continue;
        std::string p = root + "/host/fxr/" + ver + "/" + HostfxrFileName(env);
        std::error_code ec;
        if (fs::exists(p, ec)) { if (foundVersion) *foundVersion = ver; return p; }
    }
    return {};
}

std::string RuntimeNotFoundMessage(int major) {
    return "The .NET runtime was not found. Install .NET " + std::to_string(major) + " from " + kDotnetDownloadUrl +
           " (the \"Runtime\" is enough to run a game; the \"SDK\" is needed to build C# scripts), then start again.";
}
std::string SdkNotFoundMessage(int major) {
    return "The .NET SDK was not found, so C# scripts cannot be compiled. Install the .NET " + std::to_string(major) +
           " SDK from " + kDotnetDownloadUrl + " and try again.";
}
std::string NotEnabledMessage() {
    return "C# is not enabled in this build. Build the engine with -DOKAY_WITH_CSHARP=ON (Windows) to run C# scripts.";
}

std::string HostfxrErrorText(std::int32_t code) {
    auto u = (std::uint32_t)code;
    switch (u) {
        case 0: return "ok";
        case 1: return "ok (the runtime was already started with other settings)";
        case 2: return "ok (the runtime was already started)";
        case 0x80008083u: return "the .NET runtime is too old for this game; install a newer .NET";
        case 0x80008093u: case 0x80008096u: return "the .NET runtime version needed was not found; install .NET 8";
        case 0x8000808Bu: return "the runtimeconfig.json for the scripts is missing or invalid";
        case 0x80008092u: return "the .NET runtime files are damaged or incomplete; reinstall .NET";
        case 0x80008094u: return "the .NET framework named in the runtime config was not found; install .NET 8";
        case 0x800080A5u: return "the script assembly could not be found";
        case 0x800080A6u: return "the script method was not found in the assembly";
        case 0x80008088u: return "the .NET host could not find the runtime on this machine";
        default: break;
    }
    char b[64]; std::snprintf(b, sizeof b, "the .NET host failed (code 0x%08X)", u);
    return b;
}

// ------------------------------------------------------------------------------------------------
#if defined(OKAY_WITH_CSHARP)
bool DotnetRuntime::CompiledIn() { return true; }

namespace {
#ifdef _WIN32
using Lib = HMODULE;
Lib OpenLib(const std::string& p) { return LoadLibraryA(p.c_str()); }
void* Sym(Lib l, const char* n) { return (void*)GetProcAddress(l, n); }
std::wstring W(const std::string& s) { return std::wstring(s.begin(), s.end()); }   // paths are ASCII-safe enough; non-ASCII: unverified
#else
using Lib = void*;
Lib OpenLib(const std::string& p) { return dlopen(p.c_str(), RTLD_NOW | RTLD_LOCAL); }
void* Sym(Lib l, const char* n) { return dlsym(l, n); }
std::string W(const std::string& s) { return s; }
#endif
// The documented signatures, declared here.
using InitFn = int (*)(const HostChar* runtimeConfigPath, const void* params, HostfxrHandle* outHandle);
using GetDelegateFn = int (*)(HostfxrHandle, int delegateType, void** outDelegate);
using CloseFn = int (*)(HostfxrHandle);
constexpr int kLoadAssemblyAndGetFunctionPointer = 5;   // hdt_load_assembly_and_get_function_pointer
}  // namespace

struct RuntimeImpl {
    static DotnetRuntime* Start(const DotnetEnv& env, std::string* error) {
        static DotnetRuntime rt;
        static bool tried = false, ok = false;
        static std::string err;
        if (tried) { if (!ok && error) *error = err; return ok ? &rt : nullptr; }
        tried = true;
        auto fail = [&](const std::string& m) { err = m; if (error) *error = m; return nullptr; };
        std::string ver;
        std::string path = FindHostfxr(env, &ver);
        if (path.empty()) return fail(RuntimeNotFoundMessage());
        Lib lib = OpenLib(path);
        if (!lib) return fail("The .NET host library could not be loaded (" + path + "). Reinstall .NET.");
        auto init = (InitFn)Sym(lib, "hostfxr_initialize_for_runtime_config");
        auto getDel = (GetDelegateFn)Sym(lib, "hostfxr_get_runtime_delegate");
        auto close = (CloseFn)Sym(lib, "hostfxr_close");
        (void)close;
        if (!init || !getDel) return fail("The .NET host library is damaged (missing entry points). Reinstall .NET.");
        // Write the runtimeconfig next to the game (temp dir when that is not writable).
        std::error_code ec;
        std::string dir = (env.gameDir.empty() ? fs::temp_directory_path(ec).string() : env.gameDir) + "/okay_dotnet";
        fs::create_directories(dir, ec);
        std::string rt = PickRuntimeVersion([&] {
            for (auto& root : RuntimeSearchRoots(env)) { auto v = ListInstalledRuntimes(root); if (!v.empty()) return v; }
            return std::vector<std::string>{};
        }(), 8);
        if (rt.empty()) return fail(RuntimeNotFoundMessage());
        std::string cfg = dir + "/OkayScripting.runtimeconfig.json";
        { std::ofstream f(cfg, std::ios::binary); f << MakeRuntimeConfigJson(rt); }
        HostfxrHandle ctx = nullptr;
        int rc = init(W(cfg).c_str(), nullptr, &ctx);
        if (rc < 0 || !ctx) return fail("Could not start .NET: " + HostfxrErrorText(rc));
        void* del = nullptr;
        rc = getDel(ctx, kLoadAssemblyAndGetFunctionPointer, &del);
        if (rc != 0 || !del) return fail("Could not start .NET: " + HostfxrErrorText(rc));
        rt.m_ctx = ctx; rt.m_load = (LoadAssemblyAndGetFunctionPointerFn)del; rt.m_version = ver;
        ok = true;
        return &rt;
    }
};

bool DotnetRuntime::Available(const DotnetEnv& env, std::string* why) {
    bool ok = !FindHostfxr(env).empty();
    if (!ok && why) *why = RuntimeNotFoundMessage();
    return ok;
}
DotnetRuntime* DotnetRuntime::Get(const DotnetEnv& env, std::string* error) { return RuntimeImpl::Start(env, error); }

void* DotnetRuntime::GetFunction(const std::string& assemblyPath, const std::string& typeName,
                                 const std::string& methodName, std::string* error) {
    if (!m_load) { if (error) *error = RuntimeNotFoundMessage(); return nullptr; }
    void* fn = nullptr;
    // UNMANAGEDCALLERSONLY_METHOD is the documented value (const char_t*)-1.
    int rc = m_load(W(assemblyPath).c_str(), W(typeName).c_str(), W(methodName).c_str(),
                    (const HostChar*)(intptr_t)-1, nullptr, &fn);
    if (rc != 0 || !fn) { if (error) *error = "Could not load " + typeName + "." + methodName + ": " + HostfxrErrorText(rc); return nullptr; }
    return fn;
}
#else
bool DotnetRuntime::CompiledIn() { return false; }
bool DotnetRuntime::Available(const DotnetEnv&, std::string* why) { if (why) *why = NotEnabledMessage(); return false; }
DotnetRuntime* DotnetRuntime::Get(const DotnetEnv&, std::string* error) { if (error) *error = NotEnabledMessage(); return nullptr; }
void* DotnetRuntime::GetFunction(const std::string&, const std::string&, const std::string&, std::string* error) {
    if (error) *error = NotEnabledMessage();
    return nullptr;
}
#endif

} // namespace okay::csharp
