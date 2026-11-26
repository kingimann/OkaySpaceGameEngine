// okay engine: build the editor, launcher and player from an engine checkout. See Engine.hpp.
// The steps are scripts/build-local.ps1's (CMake options, targets, Release/Debug, jobs, <build>/bin), written again in
// C++ from the CMake command-line documentation and vswhere's documented options. Our own code. No <thread> or <mutex>
// (CLAUDE.md rule 21a): the processor count comes from GetSystemInfo / sysconf.
#include "Engine.hpp"
#include "CliInternal.hpp"
#include "Process.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "okay/Core/CommandLine.hpp"
#include "okay/Core/StringUtil.hpp"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace okaycli::engine {
namespace fs = std::filesystem;
using okay::str::Trim;
using namespace okaycli::detail;

namespace {

#if defined(_WIN32)
constexpr bool kWindows = true;
#else
constexpr bool kWindows = false;
#endif

const char* kSdl2Version = "2.30.9";   // the SDL2 the release builds use (.github/workflows SDL2_VERSION)

int Processors() {
#if defined(_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int)si.dwNumberOfProcessors;
#else
    const long n = ::sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
#endif
}

bool IsFile(const fs::path& p) { std::error_code ec; return fs::is_regular_file(p, ec); }
bool IsDir(const fs::path& p) { std::error_code ec; return fs::is_directory(p, ec); }

std::string Abs(const std::string& p) {
    std::error_code ec;
    const fs::path a = fs::absolute(p, ec);
    return (ec ? fs::path(p) : a).lexically_normal().string();
}

/// A path for comparing: absolute, with links resolved where the folders exist, normal, '/' separators, no trailing
/// '/', and in lower case when the file system ignores case (Windows: "C:\\Games" and "c:/games" are one folder).
std::string PathKey(const fs::path& p, bool ignoreCase) {
    std::error_code ec;
    fs::path a = fs::absolute(p, ec);
    if (ec) a = p;
    const fs::path c = fs::weakly_canonical(a, ec);
    std::string s = (ec ? a : c).lexically_normal().generic_string();
    while (s.size() > 1 && s.back() == '/' && !(s.size() == 3 && s[1] == ':')) s.pop_back();
    return ignoreCase ? okay::str::LowerAscii(s) : s;
}

} // namespace

bool PathWithin(const std::string& inner, const std::string& outer, bool ignoreCase) {
    const std::string i = PathKey(inner, ignoreCase), o = PathKey(outer, ignoreCase);
    if (i.empty() || o.empty()) return false;
    if (i == o) return true;
    const std::string prefix = o.back() == '/' ? o : o + "/";
    return i.compare(0, prefix.size(), prefix) == 0;
}

std::string CleanRefusal(const std::string& buildDir, const std::string& source, bool ignoreCase) {
    const fs::path b(buildDir);
    if (!IsFile(b / "CMakeCache.txt")) return buildDir + " is not a CMake build folder (no CMakeCache.txt), so --clean did not delete it.";
    // A folder that holds a project or sources is never a build folder, whatever cache file is in it.
    for (const char* f : {"CMakeLists.txt", "project.okayproj", "ProjectSettings/project.okayproj"})
        if (fs::exists(b / f)) return buildDir + " holds " + f + ", so it is not only a build folder; --clean did not delete it.";
    if (IsDir(b / "Assets")) return buildDir + " holds an Assets folder (a game project?), so --clean did not delete it.";
    // The cache must be one this engine checkout made: CMake writes the source folder as CMAKE_HOME_DIRECTORY.
    std::ifstream f(b / "CMakeCache.txt", std::ios::binary);
    std::string line, home;
    bool found = false;
    while (std::getline(f, line)) {
        if (line.rfind("CMAKE_HOME_DIRECTORY:", 0) != 0) continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        home = Trim(line.substr(eq + 1));
        found = true;
        break;
    }
    if (!found || home.empty())
        return buildDir + "'s CMakeCache.txt does not say which source folder it was made from, so --clean did not delete it.";
    if (PathKey(home, ignoreCase) != PathKey(source, ignoreCase))
        return buildDir + " was made from " + home + ", not from this engine checkout (" + source + "), so --clean did not delete it.";
    return "";
}

std::string CleanRefusal(const std::string& buildDir, const std::string& source) { return CleanRefusal(buildDir, source, kWindows); }

namespace {

/// `inner` is `outer` or inside it, by the file system's case rule.
bool Within(const fs::path& inner, const fs::path& outer) { return PathWithin(inner.string(), outer.string(), kWindows); }

std::string ExeName(const std::string& base, bool windows) { return windows ? base + ".exe" : base; }

std::string Sdl2Help(bool mingw) {
    return std::string("SDL2 is missing (the editor, launcher and player still need it). Download SDL2 ") + kSdl2Version +
           " (SDL2, not SDL3) from\n  https://github.com/libsdl-org/SDL/releases/tag/release-" + kSdl2Version + "\n" +
           "and take " + (mingw ? std::string("SDL2-devel-") + kSdl2Version + "-mingw.zip (you build with MinGW)"
                                : std::string("SDL2-devel-") + kSdl2Version + "-VC.zip (you build with Visual Studio)") +
           ". Unpack it into C:\\SDL2 (okay looks there), or pass --sdl2 <the unpacked folder>.";
}
const char* kCmakeHelp = "CMake is missing. Get CMake 3.20 or newer from https://cmake.org/download/ and tick \"Add CMake to the PATH\".";
const char* kCompilerHelp =
    "No C++ compiler was found. Install Visual Studio 2022 Build Tools with \"Desktop development with C++\" from\n"
    "  https://visualstudio.microsoft.com/downloads/ (under Tools for Visual Studio),\n"
    "or MinGW-w64 (for example from https://winlibs.com) and put its bin folder on PATH.";
const char* kNinjaHelp = "Ninja is missing (MinGW builds need it). Get ninja.exe from https://github.com/ninja-build/ninja/releases and put it on PATH.";

} // namespace

// ---- arguments ---------------------------------------------------------------------------------------------------

bool ParseBuildArgs(const std::vector<std::string>& args, BuildOptions& o, std::string& error) {
    okay::CommandLine cl;
    cl.Flag("help").Short('h');
    cl.Flag("debug").Flag("clean").Flag("run");
    cl.Value("jobs", "N").Short('j');
    cl.Value("dir", "BUILD").Value("source", "DIR").Value("sdl2", "DIR");
    if (!cl.Parse(args)) { error = cl.Error(); return false; }
    if (cl.Positionals().size() > 1) { error = "Build one thing: editor, launcher, player or all."; return false; }
    const std::string what = okay::str::LowerAscii(cl.Positional(0, "all"));
    if (what != "editor" && what != "launcher" && what != "player" && what != "all") {
        error = "\"" + cl.Positional(0) + "\" is not something okay can build. Pick editor, launcher, player or all.";
        return false;
    }
    o.what = what;
    o.debug = cl.Has("debug");
    o.clean = cl.Has("clean");
    o.run = cl.Has("run");
    if (cl.Has("jobs") && !cl.TryGetInt("jobs", 1, 1024, o.jobs)) { error = "--jobs needs a number from 1 to 1024."; return false; }
    if (cl.Has("dir")) {
        o.dir = cl.Get("dir");
        if (Trim(o.dir).empty()) { error = "--dir needs a folder."; return false; }
    }
    if (cl.Has("source")) o.source = cl.Get("source");
    if (cl.Has("sdl2")) o.sdl2 = cl.Get("sdl2");
    return true;
}

// ---- finding the checkout, the tools and SDL2 ---------------------------------------------------------------------

bool IsEngineSource(const std::string& dir) {
    const fs::path f = fs::path(dir) / "CMakeLists.txt";
    if (!IsFile(f)) return false;
    // project(OkaySpaceGameEngine ...) (the real checkout) or project(OkaySpace ...), allowing spaces before the name.
    const std::string t = ReadFile(f.string());
    for (std::size_t at = t.find("project"); at != std::string::npos; at = t.find("project", at + 1)) {
        if (at > 0 && (std::isalnum((unsigned char)t[at - 1]) || t[at - 1] == '_')) continue;
        std::size_t i = at + 7;
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t')) ++i;
        if (i >= t.size() || t[i] != '(') continue;
        ++i;
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i;
        for (const std::string name : {"OkaySpaceGameEngine", "OkaySpace"}) {
            const std::size_t n = name.size();
            if (t.compare(i, n, name) == 0 && (i + n >= t.size() || !(std::isalnum((unsigned char)t[i + n]) || t[i + n] == '_')))
                return true;
        }
    }
    return false;
}

std::string FindEngineSource(const std::string& given, const std::string& start) {
    if (!given.empty()) {
        const std::string g = Abs(given);
        return IsEngineSource(g) ? g : std::string();
    }
    fs::path p = Abs(start.empty() ? "." : start);
    if (p.filename().empty() && p.has_parent_path()) p = p.parent_path();
    for (fs::path q = p; !q.empty(); q = q.parent_path()) {
        if (IsEngineSource(q.string())) return q.string();
        if (q == q.parent_path()) break;
    }
    return "";
}

Tools FindTools(const std::string& vswhere) {
    Tools t;
    t.cmake = FindOnPath("cmake");
    t.ninja = FindOnPath("ninja");
    t.gxx = FindOnPath("g++");
    t.gcc = FindOnPath("gcc");
    t.mingwMake = kWindows ? FindOnPath("mingw32-make") : std::string();
    t.cl = kWindows ? FindOnPath("cl") : std::string();
    std::string vw = vswhere;
    if (vw.empty() && kWindows) {
        const char* pf = std::getenv("ProgramFiles(x86)");
        const fs::path p = fs::path(pf && *pf ? pf : "C:\\Program Files (x86)") / "Microsoft Visual Studio" / "Installer" / "vswhere.exe";
        if (IsFile(p)) vw = p.string();
    }
    if (!vw.empty()) {
        const RunResult r = Run({vw, "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                                 "-property", "installationPath", "-nologo"}, "", true);
        if (r.started && r.exitCode == 0) {
            std::istringstream in(r.out);
            std::string line;
            if (std::getline(in, line)) {
                line = Trim(line);
                if (!line.empty() && IsDir(line)) t.visualStudio = line;
            }
        }
    }
    return t;
}

std::string Sdl2PrefixIn(const std::string& dirIn, bool mingw) {
    if (dirIn.empty()) return "";
    const fs::path dir = Abs(dirIn);
    auto hasConfig = [](const fs::path& d) {
        return IsFile(d / "SDL2Config.cmake") || IsFile(d / "sdl2-config.cmake");
    };
    auto isPrefix = [&](const fs::path& d) { return hasConfig(d / "lib" / "cmake" / "SDL2") || hasConfig(d / "cmake"); };
    // SDL2_DIR may name the config folder itself: .../lib/cmake/SDL2 or .../cmake.
    if (hasConfig(dir)) {
        if (dir.filename() == "SDL2" && dir.parent_path().filename() == "cmake" && dir.parent_path().parent_path().filename() == "lib")
            return dir.parent_path().parent_path().parent_path().string();
        if (dir.filename() == "cmake") return dir.parent_path().string();
    }
    // The MinGW download holds one prefix per CPU; the 64-bit one is what the engine builds for.
    if (mingw && isPrefix(dir / "x86_64-w64-mingw32")) return (dir / "x86_64-w64-mingw32").string();
    if (isPrefix(dir)) return dir.string();
    if (!mingw && isPrefix(dir / "x86_64-w64-mingw32")) return (dir / "x86_64-w64-mingw32").string();
    return "";
}

std::string FindSdl2(const std::string& given, const std::string& source, bool mingw, const std::vector<std::string>& moreRoots) {
    if (!given.empty()) return Sdl2PrefixIn(given, mingw);
    std::vector<fs::path> roots;
    for (const char* env : {"SDL2_DIR", "SDL2_ROOT", "SDL2DIR"})
        if (const char* v = std::getenv(env)) if (*v) roots.push_back(v);
    for (const std::string& r : moreRoots) roots.push_back(r);
    if (!source.empty()) {
        const fs::path s = source;
        for (const char* sub : {"third_party", "external", "deps", "libs"}) { roots.push_back(s / sub / "SDL2"); roots.push_back(s / sub); }
        roots.push_back(s.parent_path() / "SDL2");
        roots.push_back(s.parent_path());
    }
    if (kWindows) {
        for (const char* r : {"C:\\SDL2", "C:\\libs\\SDL2", "C:\\dev\\SDL2", "C:\\libs", "C:\\dev", "C:\\"}) roots.push_back(r);
        if (mingw) roots.push_back("C:\\msys64\\mingw64");
    }
    for (const fs::path& r : roots) {
        const std::string direct = Sdl2PrefixIn(r.string(), mingw);
        if (!direct.empty()) return direct;
        // An unpacked download: <root>/SDL2-2.30.9. The newest version name first.
        std::vector<fs::path> versions;
        std::error_code ec;
        if (!IsDir(r)) continue;
        for (fs::directory_iterator it(r, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string n = it->path().filename().string();
            if (n.rfind("SDL2-", 0) == 0 && it->is_directory(ec)) versions.push_back(it->path());
        }
        std::sort(versions.begin(), versions.end(), [](const fs::path& a, const fs::path& b) { return a.filename().string() > b.filename().string(); });
        for (const fs::path& v : versions) {
            const std::string p = Sdl2PrefixIn(v.string(), mingw);
            if (!p.empty()) return p;
        }
    }
    return "";
}

// ---- the plan and cmake's command lines ---------------------------------------------------------------------------

bool MakePlan(const BuildOptions& o, const Tools& t, const std::string& source, const std::string& sdl2, Plan& p, std::string& error) {
    if (source.empty()) {
        error = "This is not inside an engine checkout (a folder whose CMakeLists.txt says project(OkaySpace).\n"
                "Run okay engine build from the checkout, or pass --source <folder>.";
        return false;
    }
    if (t.cmake.empty()) { error = kCmakeHelp; return false; }
    p = Plan{};
    p.windows = t.windows;
    p.source = source;
    p.buildDir = fs::path(o.dir).is_absolute() ? fs::path(o.dir).lexically_normal().string()
                                               : (fs::path(source) / o.dir).lexically_normal().string();
    while (p.buildDir.size() > 1 && (p.buildDir.back() == '/' || p.buildDir.back() == '\\')) p.buildDir.pop_back();
    if (Within(fs::path(source), fs::path(p.buildDir))) {
        error = "The build folder can't be the checkout itself or a folder above it (" + p.buildDir + ").";
        return false;
    }
    p.config = o.debug ? "Debug" : "Release";
    p.editor = o.what == "all" || o.what == "editor";
    p.launcher = o.what == "all" || o.what == "launcher";
    p.player = o.what == "all" || o.what == "player";
    p.sandbox = o.what == "all";   // build-local.ps1's All builds the sandbox demo too
    p.jobs = o.jobs > 0 ? o.jobs : std::max(2, Processors() / 2);
    // The compiler. MinGW g++ with Ninja (or mingw32-make); cl.exe with Ninja inside a developer prompt; else the
    // Visual Studio that vswhere found (CMake's default generator picks it, as build-local.ps1 leaves it).
    if (!t.gxx.empty() && (!t.windows || t.visualStudio.empty() || !t.ninja.empty())) {
        p.cxxCompiler = t.gxx;
        p.cCompiler = t.gcc;
        if (!t.ninja.empty()) { p.generator = "Ninja"; p.makeProgram = t.ninja; }
        else if (t.windows && !t.mingwMake.empty()) { p.generator = "MinGW Makefiles"; p.makeProgram = t.mingwMake; }
        else if (t.windows) { error = kNinjaHelp; return false; }
        p.mingw = t.windows;
    } else if (t.windows && !t.cl.empty() && !t.ninja.empty()) {
        p.generator = "Ninja";
        p.makeProgram = t.ninja;
    } else if (t.windows && !t.visualStudio.empty()) {
        p.generator.clear();
        p.multiConfig = true;
    } else if (!t.windows && !t.ninja.empty()) {
        p.generator = "Ninja";   // CMake finds the system compiler (cc / c++)
        p.makeProgram = t.ninja;
    } else if (!t.windows) {
        p.generator.clear();     // Unix Makefiles
    } else {
        error = kCompilerHelp;
        return false;
    }
    p.sdl2 = sdl2;
    if (p.sdl2.empty() && t.windows) { error = Sdl2Help(p.mingw); return false; }
    return true;
}

std::vector<std::string> ConfigureArgs(const Plan& p) {
    auto onOff = [](bool b) { return std::string(b ? "ON" : "OFF"); };
    // The same order as build-local.ps1, so the two read alike.
    std::vector<std::string> a = {"-S", p.source, "-B", p.buildDir,
                                  "-DOKAY_BUILD_EDITOR=" + onOff(p.editor), "-DOKAY_BUILD_LAUNCHER=" + onOff(p.launcher),
                                  "-DOKAY_BUILD_PLAYER=" + onOff(p.player), "-DOKAY_BUILD_SANDBOX=" + onOff(p.sandbox),
                                  "-DOKAY_BUILD_TESTS=OFF"};
    if (!p.generator.empty()) { a.push_back("-G"); a.push_back(p.generator); }
    if (!p.multiConfig) a.push_back("-DCMAKE_BUILD_TYPE=" + p.config);
    if (!p.sdl2.empty()) a.push_back("-DCMAKE_PREFIX_PATH=" + p.sdl2);
    // What okay adds: the tools by full path, so CMake never picks one up from the current folder.
    if (!p.makeProgram.empty()) a.push_back("-DCMAKE_MAKE_PROGRAM=" + p.makeProgram);
    if (!p.cCompiler.empty()) a.push_back("-DCMAKE_C_COMPILER=" + p.cCompiler);
    if (!p.cxxCompiler.empty()) a.push_back("-DCMAKE_CXX_COMPILER=" + p.cxxCompiler);
    return a;
}

std::vector<std::string> BuildArgs(const Plan& p) {
    std::vector<std::string> a = {"--build", p.buildDir, "--config", p.config, "--parallel", std::to_string(p.jobs)};
    std::vector<std::string> targets;
    if (p.editor) targets.push_back("okay-editor");
    if (p.launcher) targets.push_back("okayspace-launcher");
    if (p.player) targets.push_back("okay-player");
    if (p.sandbox) targets.push_back("sandbox");
    if (!targets.empty()) { a.push_back("--target"); a.insert(a.end(), targets.begin(), targets.end()); }
    return a;
}

std::string BinDir(const Plan& p) {
    const fs::path bin = fs::path(p.buildDir) / "bin";
    if (p.multiConfig && IsDir(bin / p.config)) return (bin / p.config).string();
    return bin.string();
}

// ---- okay engine doctor -------------------------------------------------------------------------------------------

std::string DoctorText(const Tools& t, const std::string& source, const std::string& sdl2, int* problems) {
    int bad = 0;
    std::string s;
    auto good = [&](const std::string& m) { s += "  ok    " + m + "\n"; };
    auto miss = [&](const std::string& m) { s += "  MISSING  " + m + "\n"; ++bad; };
    if (source.empty()) miss("The engine checkout: run okay engine from inside it, or pass --source <folder>.");
    else good("Engine checkout: " + source);
    if (t.cmake.empty()) miss(kCmakeHelp); else good("CMake: " + t.cmake);
    const bool mingw = !t.gxx.empty() && (t.visualStudio.empty() || !t.ninja.empty());
    if (!t.visualStudio.empty()) good("Visual Studio C++ tools: " + t.visualStudio);
    if (!t.gxx.empty()) good(std::string(t.windows ? "MinGW " : "") + "g++: " + t.gxx);
    if (!t.cl.empty()) good("cl.exe (a developer prompt): " + t.cl);
    if (t.visualStudio.empty() && t.gxx.empty() && (t.cl.empty() || t.ninja.empty())) {
        if (t.windows) miss(kCompilerHelp);
        else miss("No g++ was found. Install your system's C++ compiler (for example the build-essential package).");
    }
    if (!t.ninja.empty()) good("Ninja: " + t.ninja);
    else if (t.windows && mingw && t.mingwMake.empty()) miss(kNinjaHelp);
    else s += "  note  Ninja is not on PATH (optional" + std::string(t.windows ? " with Visual Studio" : "") + "; builds are faster with it).\n";
    if (!sdl2.empty()) good("SDL2: " + sdl2);
    else if (t.windows) miss(Sdl2Help(mingw));
    else s += "  note  No SDL2 download was found; CMake will look for the system's SDL2 (for example the libsdl2-dev package).\n";
    if (problems) *problems = bad;
    s += bad == 0 ? "Everything needed to build the engine is here. Build with: okay engine build\n"
                  : std::to_string(bad) + (bad == 1 ? " thing is" : " things are") + " missing.\n";
    return "Building the engine on this computer:\n" + s;
}

// ---- the commands -------------------------------------------------------------------------------------------------

namespace {

const char* kDoctorUsage = "Usage: okay engine doctor [--source DIR] [--sdl2 DIR]\n\nSay what building the engine needs on this computer, what is missing and where to get it.\n";
const char* kBuildUsage =
    "Usage: okay engine build [editor|launcher|player|all] [--debug] [--jobs N] [--clean] [--run] [--dir BUILD]\n"
    "                         [--source DIR] [--sdl2 DIR]\n\n"
    "Build the engine's own programs from an engine checkout: the editor (OkayEngine.exe), the launcher\n"
    "(OkaySpace.exe) and the player (OkaySpacePlayer.exe). It needs CMake, a C++ compiler (Visual Studio or MinGW)\n"
    "and the SDL2 development files; okay engine doctor says what is missing and where to get it.\n"
    "  --debug   a Debug build (default Release)       --jobs N   compile N files at once\n"
    "  --clean   delete the build folder first         --run      start the editor or launcher afterwards\n"
    "  --dir     the build folder (default build-local in the checkout)\n"
    "  --source  the engine checkout (default: this folder or a parent of it)\n"
    "  --sdl2    the unpacked SDL2 download (default: C:\\SDL2, SDL2_DIR, the checkout's third_party ...)\n";

Tools ToolsHere() { return FindTools(); }

int Doctor(const std::vector<std::string>& args, const Io& io) {
    okay::CommandLine cl;
    cl.Flag("help").Short('h');
    cl.Value("source", "DIR").Value("sdl2", "DIR");
    if (!cl.Parse(args)) { io.Warn(cl.Error() + "\n" + kDoctorUsage); return 2; }
    if (cl.Has("help")) { io.Say(kDoctorUsage); return 0; }
    const Tools t = ToolsHere();
    const std::string source = FindEngineSource(cl.Get("source", ""), ".");
    const bool mingw = t.windows && !t.gxx.empty() && (t.visualStudio.empty() || !t.ninja.empty());
    const std::string sdl2 = FindSdl2(cl.Get("sdl2", ""), source, mingw);
    int problems = 0;
    io.Say(DoctorText(t, source, sdl2, &problems));
    return problems == 0 ? 0 : 1;
}

int Build(const std::vector<std::string>& args, const Io& io) {
    for (const std::string& a : args) if (a == "--help" || a == "-h") { io.Say(kBuildUsage); return 0; }
    BuildOptions o;
    std::string error;
    if (!ParseBuildArgs(args, o, error)) { io.Warn(error + "\n" + kBuildUsage); return 2; }
    const std::string source = FindEngineSource(o.source, ".");
    if (source.empty() && !o.source.empty()) { io.Warn(o.source + " is not an engine checkout (its CMakeLists.txt has no project(OkaySpace)."); return 1; }
    const Tools t = ToolsHere();
    const bool mingw = t.windows && !t.gxx.empty() && (t.visualStudio.empty() || !t.ninja.empty());
    const std::string sdl2 = FindSdl2(o.sdl2, source, mingw);
    if (!o.sdl2.empty() && sdl2.empty()) {
        io.Warn(o.sdl2 + " does not hold the SDL2 development files (no lib/cmake/SDL2 or cmake folder with SDL2's CMake file).\n" + Sdl2Help(mingw));
        return 1;
    }
    Plan p;
    if (!MakePlan(o, t, source, sdl2, p, error)) { io.Warn(error); return 1; }
    if (p.sdl2.empty()) io.Say("Note: no SDL2 download was found; CMake will look for the system's SDL2.");
    std::error_code ec;
    if (o.clean && fs::exists(p.buildDir, ec)) {
        // Only a CMake build folder of THIS checkout is ever deleted, never a project or other files given by mistake.
        const std::string refusal = CleanRefusal(p.buildDir, source);
        if (!refusal.empty()) { io.Warn(refusal); return 1; }
        io.Say("Removing " + p.buildDir);
        fs::remove_all(p.buildDir, ec);
        if (ec) { io.Warn("Couldn't delete " + p.buildDir + ": " + ec.message()); return 1; }
    }
    std::vector<std::string> configure = {t.cmake};
    for (const std::string& a : ConfigureArgs(p)) configure.push_back(a);
    io.Say("Configuring (" + o.what + ", " + p.config + ") in " + p.buildDir +
           (p.generator.empty() ? std::string(p.multiConfig ? " with Visual Studio" : "") : " with " + p.generator) + ".");
    RunResult r = Run(configure, source, false);
    if (!r.started) { io.Warn("Couldn't start CMake (" + t.cmake + ")."); return 1; }
    if (r.exitCode != 0) {
        io.Warn("CMake could not set up the build. The first error above is the one to read.\n"
                "If it says SDL2 is missing, pass --sdl2 <folder>. If it says the generator does not match, add --clean.");
        return 1;
    }
    std::vector<std::string> build = {t.cmake};
    for (const std::string& a : BuildArgs(p)) build.push_back(a);
    io.Say("Building with " + std::to_string(p.jobs) + " jobs.");
    r = Run(build, source, false);
    if (!r.started || r.exitCode != 0) { io.Warn("The build failed. The first error above is the one to read."); return 1; }
    const std::string bin = BinDir(p);
    std::string t2 = "Build finished. Programs are in " + bin + ":\n";
    struct Made { bool want; const char* base; const char* what; };
    for (const Made& m : {Made{p.editor, "OkayEngine", "the editor"}, Made{p.launcher, "OkaySpace", "the launcher"},
                          Made{p.player || p.editor, "OkaySpacePlayer", "the player"}}) {
        if (!m.want) continue;
        const fs::path exe = fs::path(bin) / ExeName(m.base, p.windows);
        t2 += "  " + std::string(m.what) + ": " + (IsFile(exe) ? exe.string() : exe.filename().string() + " was not made (look for errors above)") + "\n";
    }
    io.Say(t2);
    if (o.run) {
        const char* base = p.editor && !p.launcher ? "OkayEngine" : p.launcher ? "OkaySpace" : nullptr;
        const fs::path exe = base ? fs::path(bin) / ExeName(base, p.windows) : fs::path();
        if (!base || !IsFile(exe)) io.Say("--run: nothing to start for " + o.what + ".");
        else if (!StartDetached({exe.string()}, bin)) { io.Warn("Couldn't start " + exe.string() + "."); return 1; }
        else io.Say("Started " + exe.filename().string() + ".");
    }
    return 0;
}

} // namespace

const char* BuildUsage() { return kBuildUsage; }
const char* DoctorUsage() { return kDoctorUsage; }

int EngineMain(const std::vector<std::string>& args, const Io& io) {
    if (args.empty() || args[0] == "--help" || args[0] == "-h" || args[0] == "help") { io.Say(HelpText("engine")); return 0; }
    const std::vector<std::string> more(args.begin() + 1, args.end());
    if (args[0] == "build") return Build(more, io);
    if (args[0] == "doctor") return Doctor(more, io);
    io.Warn("There is no okay engine command called \"" + args[0] + "\".\n" + HelpText("engine"));
    return 2;
}

} // namespace okaycli::engine
