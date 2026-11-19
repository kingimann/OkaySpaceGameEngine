#pragma once
// Finding and starting the .NET runtime (CoreCLR) through its documented hosting API (hostfxr).
// Written from Microsoft's public "Write a custom .NET host" description of the hostfxr entry points:
// we declare the few function signatures ourselves and load the library with LoadLibrary / dlopen.
// No .NET SDK header or library is used, copied or linked. The pure-logic parts (search order,
// version choice, runtimeconfig.json text, error text) are always compiled and tested without .NET.
// All of the native code is gated by OKAY_WITH_CSHARP (CMake option, OFF by default); without it
// LoadRuntime() reports "C# is not enabled in this build".
#include <cstdint>
#include <string>
#include <vector>

namespace okay::csharp {

/// The places a runtime may live, passed in so the search can be tested on any machine.
struct DotnetEnv {
    std::string dotnetRoot;         ///< DOTNET_ROOT (may be empty)
    std::string programFiles;       ///< %ProgramFiles% (Windows)
    std::string programFilesX86;    ///< %ProgramFiles(x86)% (Windows)
    std::string home;               ///< HOME / USERPROFILE
    std::string gameDir;            ///< folder of the game: a runtime shipped next to it ("runtime/") wins
    bool windows = false;
    bool mac = false;
};
DotnetEnv CurrentDotnetEnv(const std::string& gameDir = "");

/// Candidate .NET install roots, best first: next to the game, DOTNET_ROOT, then the standard places.
std::vector<std::string> RuntimeSearchRoots(const DotnetEnv& env);

struct DotnetVersion { int major = 0, minor = 0, patch = 0; bool preview = false; bool ok = false; };
DotnetVersion ParseDotnetVersion(const std::string& text);   ///< "8.0.11", "9.0.0-preview.1"
/// The newest installed version whose major is >= `minMajor`; "" if none. Stable beats preview of the same number.
std::string PickRuntimeVersion(const std::vector<std::string>& installed, int minMajor);

/// Minimal `<name>.runtimeconfig.json` for a framework-dependent host start.
std::string MakeRuntimeConfigJson(const std::string& frameworkVersion);

/// File name of the host resolver: hostfxr.dll / libhostfxr.so / libhostfxr.dylib.
std::string HostfxrFileName(const DotnetEnv& env);
/// `<root>/host/fxr/<newest version>/<hostfxr file>` in the first root that has one; "" if none.
/// `foundVersion` receives the folder name chosen.
std::string FindHostfxr(const DotnetEnv& env, std::string* foundVersion = nullptr);
/// Installed Microsoft.NETCore.App versions under `root/shared/Microsoft.NETCore.App/`.
std::vector<std::string> ListInstalledRuntimes(const std::string& root);

/// Plain-words messages.
std::string RuntimeNotFoundMessage(int major = 8);
std::string NotEnabledMessage();
std::string SdkNotFoundMessage(int major = 8);
constexpr const char* kDotnetDownloadUrl = "https://dotnet.microsoft.com/download";

/// Hostfxr result codes (documented) in plain words.
std::string HostfxrErrorText(std::int32_t code);

// ---------------------------------------------------------------------------------------------
// The started runtime.
/// Our own declarations of the documented signatures. char_t is wchar_t on Windows, char elsewhere.
#ifdef _WIN32
using HostChar = wchar_t;
#else
using HostChar = char;
#endif
using HostfxrHandle = void*;
using LoadAssemblyAndGetFunctionPointerFn = int (*)(const HostChar* assemblyPath, const HostChar* typeName,
                                                    const HostChar* methodName, const HostChar* delegateTypeName,
                                                    void* reserved, void** outDelegate);

class DotnetRuntime {
public:
    /// True when this build has the host code compiled in (OKAY_WITH_CSHARP).
    static bool CompiledIn();
    /// Is a runtime installed? Cheap (looks at folders only), never starts anything.
    static bool Available(const DotnetEnv& env, std::string* why = nullptr);
    /// Start the runtime once per process (later calls return the same one). Fills `error` in plain words.
    static DotnetRuntime* Get(const DotnetEnv& env, std::string* error);
    /// Resolve `Namespace.Type, Assembly` method `name` (marked UnmanagedCallersOnly) to a native function pointer.
    void* GetFunction(const std::string& assemblyPath, const std::string& typeName,
                      const std::string& methodName, std::string* error);
    const std::string& Version() const { return m_version; }
private:
    friend struct RuntimeImpl;
    HostfxrHandle m_ctx = nullptr;
    LoadAssemblyAndGetFunctionPointerFn m_load = nullptr;
    std::string m_version;
};

} // namespace okay::csharp
