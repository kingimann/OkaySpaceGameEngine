#include "okay/CSharp/CSharpCompiler.hpp"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

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

std::string MakeApiCsproj(const std::string& dir) {
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
           "    <Compile Include=\"" + XmlEscape(dir) + "/**/*.cs\" />\n"
           "  </ItemGroup>\n"
           "</Project>\n";
}

static bool Exists(const fs::path& p) { std::error_code ec; return fs::exists(p, ec); }

std::string FindDotnetCli(const DotnetEnv& env) {
    const char* exe = env.windows ? "dotnet.exe" : "dotnet";
    for (auto& root : RuntimeSearchRoots(env)) {
        if (Exists(fs::path(root) / "sdk") && Exists(fs::path(root) / exe)) return root + "/" + exe;
    }
    if (const char* path = std::getenv("PATH")) {
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
    // 2. built from csharp/OkayScripting by the developer's SDK, cached by the sources' hash
    std::string all;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(sourceDir, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".cs") all += it->path().filename().string() + "\n" + ReadAll(it->path());
    if (all.empty()) { if (error) *error = "OkayScripting.dll was not found and its sources are missing (" + sourceDir + ")."; return {}; }
    std::string h = ContentHash(all);
    fs::path dir = fs::path(cacheDir) / ("api_" + h);
    fs::path dll = dir / "OkayScripting.dll";
    if (Exists(dll)) return dll.string();
    std::string cli = FindDotnetCli(env);
    if (cli.empty()) { if (error) *error = SdkNotFoundMessage(); return {}; }
    fs::create_directories(dir, ec);
    { std::ofstream f(dir / "OkayScripting.csproj", std::ios::binary); f << MakeApiCsproj(fs::absolute(sourceDir, ec).generic_string()); }
    int rc = std::system(BuildCommand(cli, (dir / "OkayScripting.csproj").string(), dir.string(), (dir / "build.log").string(), env.windows).c_str());
    if (!Exists(dll)) {
        if (error) *error = "Building OkayScripting.dll failed (code " + std::to_string(rc) + "):\n" + ReadAll(dir / "build.log").substr(0, 1500);
        return {};
    }
    return dll.string();
}

} // namespace okay::csharp
