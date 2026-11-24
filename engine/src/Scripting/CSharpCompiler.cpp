#include "okay/Scripting/CSharpCompiler.hpp"
#include "okay/Scripting/CSharpBridge.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace okay::csharp {
namespace fs = std::filesystem;

std::string ContentHash(const std::string& text) {
    std::uint64_t a = 1469598103934665603ull, b = 0x9E3779B97F4A7C15ull;
    for (unsigned char c : text) {
        a ^= c; a *= 1099511628211ull;
        b ^= (std::uint64_t)(c + 31); b *= 0x100000001B3ull; b ^= b >> 29;
    }
    a ^= text.size(); b ^= text.size() * 0x9E3779B97F4A7C15ull;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%016llx%016llx", (unsigned long long)a, (unsigned long long)b);
    return buf;
}

static bool IsIdent(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

std::string DetectScriptClass(const std::string& s) {
    // Skip comments and string literals crudely, then find `class <Name> : ... Behaviour`.
    std::string t; t.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') { while (i < s.size() && s[i] != '\n') ++i; t += '\n'; continue; }
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            i += 2; while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/')) { if (s[i] == '\n') t += '\n'; ++i; }
            ++i; t += ' '; continue;
        }
        if (s[i] == '"') { t += ' '; ++i; while (i < s.size() && s[i] != '"' && s[i] != '\n') { if (s[i] == '\\') ++i; ++i; } continue; }
        t += s[i];
    }
    std::size_t pos = 0;
    while ((pos = t.find("class", pos)) != std::string::npos) {
        bool startOk = pos == 0 || !IsIdent(t[pos - 1]);
        std::size_t i = pos + 5;
        pos += 5;
        if (!startOk || i >= t.size() || !std::isspace((unsigned char)t[i])) continue;
        while (i < t.size() && std::isspace((unsigned char)t[i])) ++i;
        std::size_t n0 = i;
        while (i < t.size() && IsIdent(t[i])) ++i;
        if (i == n0) continue;
        std::string name = t.substr(n0, i - n0);
        while (i < t.size() && std::isspace((unsigned char)t[i])) ++i;
        if (i >= t.size() || t[i] != ':') continue;
        ++i;
        std::size_t end = t.find('{', i);
        if (end == std::string::npos) end = t.size();
        std::string bases = t.substr(i, end - i);
        // base list items split on ','; accept `Behaviour` or `OkayScripting.Behaviour`
        std::stringstream ss(bases); std::string item;
        while (std::getline(ss, item, ',')) {
            std::string x; for (char c : item) if (!std::isspace((unsigned char)c)) x += c;
            if (x == "Behaviour" || x == "OkayScripting.Behaviour") return name;
        }
    }
    return {};
}

std::vector<CompileDiag> ParseBuildOutput(const std::string& out) {
    std::vector<CompileDiag> r;
    std::stringstream ss(out); std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // path(12,5): error CS1002: message [project]
        std::size_t lp = line.find('(');
        std::size_t rp = lp == std::string::npos ? lp : line.find("): ", lp);
        if (rp == std::string::npos) continue;
        CompileDiag d;
        int l = 0, c = 0;
        if (std::sscanf(line.c_str() + lp + 1, "%d,%d", &l, &c) < 1) continue;
        d.file = line.substr(0, lp); d.line = l; d.column = c;
        std::string rest = line.substr(rp + 3);
        if (rest.rfind("error ", 0) == 0) { d.error = true; rest = rest.substr(6); }
        else if (rest.rfind("warning ", 0) == 0) { d.error = false; rest = rest.substr(8); }
        else continue;
        std::size_t colon = rest.find(": ");
        if (colon == std::string::npos) continue;
        d.code = rest.substr(0, colon);
        d.message = rest.substr(colon + 2);
        std::size_t br = d.message.rfind(" [");
        if (br != std::string::npos && !d.message.empty() && d.message.back() == ']') d.message.erase(br);
        bool dup = false;
        for (auto& e : r) if (e.line == d.line && e.column == d.column && e.code == d.code && e.message == d.message) dup = true;
        if (!dup) r.push_back(d);
    }
    return r;
}

std::string DiagText(const CompileDiag& d) {
    return "line " + std::to_string(d.line) + ": " + d.message + (d.code.empty() ? "" : " (" + d.code + ")");
}

static std::string XmlEscape(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '"') o += "&quot;"; else o += c; }
    return o;
}

std::string MakeScriptCsproj(const std::string& name, const std::string& file, const std::string& api) {
    return "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
           "  <PropertyGroup>\n"
           "    <TargetFramework>net8.0</TargetFramework>\n"
           "    <OutputType>Library</OutputType>\n"
           "    <AssemblyName>" + XmlEscape(name) + "</AssemblyName>\n"
           "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
           "    <ImplicitUsings>disable</ImplicitUsings>\n"
           "    <Nullable>disable</Nullable>\n"
           "    <DebugType>embedded</DebugType>\n"
           "    <GenerateAssemblyInfo>false</GenerateAssemblyInfo>\n"
           "    <NuGetAudit>false</NuGetAudit>\n"
           "  </PropertyGroup>\n"
           "  <ItemGroup>\n"
           "    <Compile Include=\"" + XmlEscape(file) + "\" />\n"
           "    <Reference Include=\"OkayScripting\"><HintPath>" + XmlEscape(api) + "</HintPath><Private>false</Private></Reference>\n"
           "  </ItemGroup>\n"
           "</Project>\n";
}

std::string MakeApiCsproj(const std::string& dir, const std::string& generated) {
    return "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
           "  <PropertyGroup>\n"
           "    <TargetFramework>net8.0</TargetFramework>\n"
           "    <OutputType>Library</OutputType>\n"
           "    <AssemblyName>OkayScripting</AssemblyName>\n"
           "    <RootNamespace>OkayScripting</RootNamespace>\n"
           "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
           "    <ImplicitUsings>disable</ImplicitUsings>\n"
           "    <Nullable>disable</Nullable>\n"
           "    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>\n"
           "    <DebugType>embedded</DebugType>\n"
           "    <NuGetAudit>false</NuGetAudit>\n"
           "  </PropertyGroup>\n"
           "  <ItemGroup>\n"
           "    <Compile Include=\"" + XmlEscape(dir) + "/**/*.cs\" Exclude=\"" + XmlEscape(dir) + "/obj/**;" + XmlEscape(dir) + "/bin/**\" />\n" +
           (generated.empty() ? std::string() : "    <Compile Include=\"" + XmlEscape(generated) + "\" />\n") +
           "  </ItemGroup>\n"
           "</Project>\n";
}

static bool Exists(const fs::path& p) { std::error_code ec; return fs::exists(p, ec); }

std::string FindDotnetCli(const DotnetEnv& env) {
    const char* exe = env.windows ? "dotnet.exe" : "dotnet";
    for (auto& root : RuntimeSearchRoots(env)) {
        if (Exists(fs::path(root) / "sdk") && Exists(fs::path(root) / exe)) return root + "/" + exe;
    }
    if (const char* path = env.systemPlaces ? std::getenv("PATH") : nullptr) {
        std::string p = path; char sep = env.windows ? ';' : ':';
        std::stringstream ss(p); std::string dir;
        while (std::getline(ss, dir, sep))
            if (!dir.empty() && Exists(fs::path(dir) / exe) && Exists(fs::path(dir) / "sdk")) return (fs::path(dir) / exe).string();
        // `dotnet` on PATH of a typical install lives beside sdk/; a bare host also counts if sdk is under DOTNET_ROOT
    }
    return {};
}

static std::string Q(const std::string& s) { return "\"" + s + "\""; }

std::string BuildCommand(const std::string& cli, const std::string& csproj, const std::string& outDir,
                         const std::string& log, bool windows) {
    std::string c = Q(cli) + " build " + Q(csproj) + " -c Release -nologo -v q -clp:NoSummary -o " + Q(outDir) +
                    " > " + Q(log) + " 2>&1";
    return windows ? "\"" + c + "\"" : c;   // cmd /C strips one outer quote pair
}

static std::string ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

std::string ApiSourceDir() {
    auto has = [](const fs::path& d) { return Exists(d / "Engine.cs") && Exists(d / "Interop"); };
    if (const char* e = std::getenv("OKAY_CSHARP_API_SRC")) if (*e && has(e)) return e;
    std::string exe = ExecutableDir();
    if (!exe.empty()) {
        fs::path d = exe;
        for (int i = 0; i < 4 && !d.empty(); ++i, d = d.parent_path())
            if (has(d / "managed" / "OkayScripting")) return (d / "managed" / "OkayScripting").string();
    }
#ifdef OKAY_MANAGED_SOURCE_DIR
    if (has(OKAY_MANAGED_SOURCE_DIR)) return OKAY_MANAGED_SOURCE_DIR;
#endif
    if (has("managed/OkayScripting")) return "managed/OkayScripting";
    return {};
}

std::string DefaultCacheDir() {
    if (const char* e = std::getenv("OKAY_CSHARP_CACHE")) return e;
    std::error_code ec;
    return (fs::temp_directory_path(ec) / "okay_csharp_cache").string();
}

std::string CachedAssembly(const std::string& source, const std::string& cacheDir) {
    std::string h = ContentHash(source);
    fs::path dll = fs::path(cacheDir) / h / (h + ".dll");
    return Exists(dll) ? dll.string() : std::string();
}

CompileResult CompileScript(const std::string& source, const std::string& cacheDir, const std::string& apiDll,
                            const DotnetEnv& env) {
    CompileResult r;
    std::string h = ContentHash(source);
    fs::path dir = fs::path(cacheDir) / h;
    fs::path dll = dir / (h + ".dll");
    if (Exists(dll)) { r.ok = r.cached = true; r.assemblyPath = dll.string(); return r; }
    if (Exists(dir / "errors.txt")) {
        r.cached = true; r.diags = ParseBuildOutput(ReadAll(dir / "errors.txt"));
        r.message = r.diags.empty() ? "The script did not compile." : "";
        return r;
    }
    if (!DotnetRuntime::CompiledIn()) { r.message = NotEnabledMessage(); return r; }
    std::string cli = FindDotnetCli(env);
    if (cli.empty()) { r.message = SdkNotFoundMessage(); return r; }
    if (apiDll.empty() || !Exists(apiDll)) { r.message = "OkayScripting.dll was not found next to the program."; return r; }
    std::error_code ec;
    fs::create_directories(dir, ec);
    { std::ofstream f(dir / "Script.cs", std::ios::binary); f << source; }
    { std::ofstream f(dir / "build.csproj", std::ios::binary); f << MakeScriptCsproj(h, "Script.cs", apiDll); }
    fs::path log = dir / "build.log";
    int rc = std::system(BuildCommand(cli, (dir / "build.csproj").string(), dir.string(), log.string(), env.windows).c_str());
    std::string out = ReadAll(log);
    r.diags = ParseBuildOutput(out);
    if (Exists(dll)) { r.ok = true; r.assemblyPath = dll.string(); return r; }
    { std::ofstream f(dir / "errors.txt", std::ios::binary); f << out; }
    if (r.diags.empty()) r.message = rc != 0 ? "The .NET build failed:\n" + out.substr(0, 1500) : "The script did not compile.";
    return r;
}

std::string EnsureApiAssembly(const std::string& sourceDir, const std::string& cacheDir, const DotnetEnv& env, std::string* error) {
    // 1. shipped next to the program / game
    for (const std::string& d : {env.gameDir, std::string(".")}) {
        if (d.empty()) continue;
        fs::path p = fs::path(d) / "OkayScripting.dll";
        if (Exists(p)) return p.string();
    }
    // 2. built from managed/OkayScripting and the generated classes by the developer's SDK, cached by their hash
    const std::string generated = GenerateApiSource();
    std::string all = "Bindings.g.cs\n" + generated;
    bool anySource = false;
    std::error_code ec;
    std::vector<fs::path> files;   // sorted, so the hash (and the cached build) does not depend on folder order
    for (fs::recursive_directory_iterator it(sourceDir.empty() ? fs::path("managed/OkayScripting") : fs::path(sourceDir), ec), end;
         !ec && it != end; it.increment(ec)) {
        const std::string rel = it->path().generic_string();
        if (it->path().extension() == ".cs" && rel.find("/obj/") == std::string::npos && rel.find("/bin/") == std::string::npos)
            files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    for (auto& f : files) { anySource = true; all += f.filename().string() + "\n" + ReadAll(f); }
    if (!anySource) {
        if (error) *error = "OkayScripting.dll was not found and its sources are missing (" + (sourceDir.empty() ? std::string("managed/OkayScripting") : sourceDir) + ").";
        return {};
    }
    std::string h = ContentHash(all);
    fs::path dir = fs::path(cacheDir) / ("api_" + h);
    fs::path dll = dir / "OkayScripting.dll";
    if (Exists(dll)) return dll.string();
    std::string cli = FindDotnetCli(env);
    if (cli.empty()) { if (error) *error = SdkNotFoundMessage(); return {}; }
    fs::create_directories(dir, ec);
    { std::ofstream f(dir / "Bindings.g.cs", std::ios::binary); f << generated; }
    { std::ofstream f(dir / "OkayScripting.csproj", std::ios::binary);
      f << MakeApiCsproj(fs::absolute(sourceDir, ec).generic_string(), (dir / "Bindings.g.cs").generic_string()); }
    int rc = std::system(BuildCommand(cli, (dir / "OkayScripting.csproj").string(), dir.string(), (dir / "build.log").string(), env.windows).c_str());
    if (!Exists(dll)) {
        if (error) *error = "Building OkayScripting.dll failed (code " + std::to_string(rc) + "):\n" + ReadAll(dir / "build.log").substr(0, 1500);
        return {};
    }
    return dll.string();
}

static void CopyTree(const fs::path& from, const fs::path& to, const fs::path& base, ExportReport& r) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        fs::path rel = fs::relative(it->path(), from, ec);
        fs::path dst = to / rel;
        fs::create_directories(dst.parent_path(), ec);
        fs::copy_file(it->path(), dst, fs::copy_options::overwrite_existing, ec);
        if (ec) { r.ok = false; r.problems.push_back("Could not copy " + it->path().string() + ": " + ec.message()); ec.clear(); continue; }
        r.files.push_back(fs::relative(dst, base, ec).generic_string());
    }
}

ExportReport ExportCSharp(const std::vector<std::string>& sources, const std::string& gameDir, const DotnetEnv& env,
                          const std::string& cacheDir, const std::string& apiDll) {
    ExportReport r;
    if (sources.empty()) return r;
    r.needed = true;
    std::error_code ec;
    fs::path game(gameDir);
    auto put = [&](const fs::path& src, const fs::path& rel) {
        fs::path dst = game / rel;
        fs::create_directories(dst.parent_path(), ec);
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) { r.ok = false; r.problems.push_back("Could not copy " + src.string() + ": " + ec.message()); ec.clear(); return; }
        r.files.push_back(rel.generic_string());
    };
    if (apiDll.empty() || !Exists(apiDll)) { r.ok = false; r.problems.push_back("OkayScripting.dll was not found, so the C# scripts cannot ship. Save a C# script in the editor first (it builds the file)."); }
    else put(apiDll, "OkayScripting.dll");
    int n = 0;
    for (auto& src : sources) {
        ++n;
        std::string dll = CachedAssembly(src, cacheDir);
        if (dll.empty()) { r.ok = false; r.problems.push_back("C# script " + std::to_string(n) + " has not been compiled. Open it in the editor and save it (compile on save), then build again."); continue; }
        put(dll, fs::path("Scripts") / (ContentHash(src) + ".dll"));
    }
    // The runtime: the install that holds the newest hostfxr.
    DotnetEnv plain = env; plain.gameDir.clear();
    std::string ver;
    std::string fxr = FindHostfxr(plain, &ver);
    if (fxr.empty()) { r.ok = false; r.problems.push_back(RuntimeNotFoundMessage() + " It is needed to ship a C# game."); return r; }
    fs::path root = fs::path(fxr).parent_path().parent_path().parent_path().parent_path();   // <root>/host/fxr/<ver>/hostfxr -> <root>
    CopyTree(root / "host" / "fxr" / ver, game / "runtime" / "host" / "fxr" / ver, game, r);
    std::string rtVer = PickRuntimeVersion(ListInstalledRuntimes(root.string()), 8);
    if (rtVer.empty()) { r.ok = false; r.problems.push_back("The .NET runtime itself (Microsoft.NETCore.App 8 or newer) was not found next to hostfxr. Install .NET 8."); return r; }
    CopyTree(root / "shared" / "Microsoft.NETCore.App" / rtVer, game / "runtime" / "shared" / "Microsoft.NETCore.App" / rtVer, game, r);
    for (const char* notice : {"LICENSE.txt", "ThirdPartyNotices.txt"})
        if (Exists(root / notice)) put(root / notice, fs::path("runtime") / notice);
    return r;
}

std::vector<std::string> CollectCSharpSources(const Scene& scene) {
    std::vector<std::string> out;
    for (const auto& up : scene.Objects()) {
        if (!up) continue;
        for (auto* sc : up->GetComponents<ScriptComponent>())
            if (sc && (sc->Language() == "csharp" || sc->Language() == "cs")) out.push_back(sc->Source());
    }
    return out;
}

std::string ExecutableDir() {
#if defined(_WIN32)
    char buf[1024];
    DWORD n = GetModuleFileNameA(nullptr, buf, (DWORD)sizeof buf);
    if (n == 0 || n >= sizeof buf) return {};
    return fs::path(std::string(buf, n)).parent_path().string();   // unverified on Windows
#else
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? std::string() : p.parent_path().string();
#endif
}

bool CompileForEditor(const std::string& source, const std::string& scriptPath, std::vector<std::string>* lines) {
    auto say = [&](const std::string& m) { if (lines) lines->push_back(m); };
    if (!DotnetRuntime::CompiledIn()) { say(NotEnabledMessage()); return false; }
    if (DetectScriptClass(source).empty()) {
        say(scriptPath + "(1,1): error: no class that inherits from Behaviour was found. Write: public class MyScript : Behaviour { }");
        return false;
    }
    DotnetEnv env = CurrentDotnetEnv();
    std::string err;
    std::string api = EnsureApiAssembly(ApiSourceDir(), DefaultCacheDir(), env, &err);
    if (api.empty()) { say(err); return false; }
    CompileResult r = CompileScript(source, DefaultCacheDir(), api, env);
    if (r.ok) return true;
    for (auto& d : r.diags)
        say(scriptPath + "(" + std::to_string(d.line) + "," + std::to_string(d.column) + "): " + (d.error ? "error " : "warning ") +
            d.code + ": " + d.message);
    if (r.diags.empty()) say(r.message.empty() ? "The C# script did not compile." : r.message);
    return false;
}

} // namespace okay::csharp
