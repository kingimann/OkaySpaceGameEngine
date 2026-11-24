#pragma once
// Build-time compile of C# scripts with the developer's installed .NET SDK (a build tool, like MSVC).
// The pure-logic parts are tested without .NET: content hash, class detection, project text,
// compiler output parsing, SDK lookup. Nothing from the SDK is copied or linked.
#include "okay/Scripting/CSharpHost.hpp"
#include <string>
#include <vector>

namespace okay { class Scene; }

namespace okay::csharp {

struct CompileDiag {
    std::string file;
    int line = 0, column = 0;
    bool error = true;          ///< false: warning
    std::string code;           ///< "CS1002"
    std::string message;
};

/// 32 hex characters from the text (two FNV-1a passes). A cache key, not a security hash.
std::string ContentHash(const std::string& text);
/// The first `class X : Behaviour` (also `public sealed class X : Behaviour`); "" if none.
std::string DetectScriptClass(const std::string& source);
/// Compiler output lines `path(12,5): error CS1002: ; expected [project]` -> diagnostics.
std::vector<CompileDiag> ParseBuildOutput(const std::string& output);
/// "line 12: ; expected (CS1002)" for the Console and the Problems panel.
std::string DiagText(const CompileDiag& d);
/// SDK project that compiles `scriptFile` into <assemblyName>.dll against OkayScripting.dll.
std::string MakeScriptCsproj(const std::string& assemblyName, const std::string& scriptFile, const std::string& apiDllPath);
/// Project that builds OkayScripting.dll from the sources in `sourceDir`, plus `generatedFile` (the classes
/// generated from the binding table, GenerateApiSource) when it is not empty.
std::string MakeApiCsproj(const std::string& sourceDir, const std::string& generatedFile = "");
/// Where the hand-written managed sources are: $OKAY_CSHARP_API_SRC, then managed/OkayScripting next to the
/// program or up to three folders above it, then the source tree this engine was built from, then the
/// working folder. "" when none has them.
std::string ApiSourceDir();
/// `<root>/dotnet[.exe]` of the first root that has an `sdk/` folder, else "dotnet" if PATH has it, else "".
std::string FindDotnetCli(const DotnetEnv& env);
/// The command line for a quiet build (quoted for the platform's shell).
std::string BuildCommand(const std::string& dotnetCli, const std::string& csproj, const std::string& outDir,
                         const std::string& logFile, bool windows);

struct CompileResult {
    bool ok = false;
    bool cached = false;
    std::string assemblyPath;
    std::vector<CompileDiag> diags;
    std::string message;   ///< plain-words reason when not ok and there are no diagnostics (no SDK ...)
};
/// Compile one script into `<cacheDir>/<hash>/<hash>.dll` (skipped when it is already there).
/// Failed compiles are remembered (errors.txt) so the same text is not rebuilt until it changes.
CompileResult CompileScript(const std::string& source, const std::string& cacheDir, const std::string& apiDll,
                            const DotnetEnv& env);
/// Where a compiled script would be, without building: "" if not cached.
std::string CachedAssembly(const std::string& source, const std::string& cacheDir);
/// Builds OkayScripting.dll from the managed/OkayScripting sources and the generated component classes into the
/// cache, keyed by the hash of both (or finds it next to the program / game).
std::string EnsureApiAssembly(const std::string& sourceDir, const std::string& cacheDir, const DotnetEnv& env, std::string* error);
/// Editor "compile on save": build `source` (cached by hash) and describe the outcome as Console lines
/// (`path(line,col): error CSxxxx: message`, or a plain-words reason such as the "Install .NET" message
/// with its link). Returns true when the script compiled. Blocks until the SDK is done.
bool CompileForEditor(const std::string& source, const std::string& scriptPath, std::vector<std::string>* lines);
/// Export for "Build Game". Only for projects with C# scripts: `sources` is every C# script's text (empty list = nothing is
/// copied and `needed` is false). Copies, under `gameDir`: OkayScripting.dll, `Scripts/<hash>.dll` for each script (the
/// build must have compiled them: save in the editor), and the .NET runtime files (`runtime/host/fxr/<v>`,
/// `runtime/shared/Microsoft.NETCore.App/<v>`) plus the runtime's licence and notice files, from the developer's install.
struct ExportReport {
    bool needed = false;
    bool ok = true;
    std::vector<std::string> files;      ///< relative to gameDir
    std::vector<std::string> problems;   ///< plain words
};
ExportReport ExportCSharp(const std::vector<std::string>& sources, const std::string& gameDir, const DotnetEnv& env,
                          const std::string& cacheDir, const std::string& apiDll);
/// The text of every C# script (language "csharp" or "cs") on the scene's objects, in object order. Empty for a
/// scene without C#: such a game ships no .NET runtime.
std::vector<std::string> CollectCSharpSources(const Scene& scene);
/// Folder of the running program ("" if unknown).
std::string ExecutableDir();
/// Default cache folder (next to the project / program).
std::string DefaultCacheDir();

} // namespace okay::csharp
