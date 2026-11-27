#pragma once
// okay engine: build the engine's own programs (the editor OkayEngine.exe, the launcher OkaySpace.exe, the player
// OkaySpacePlayer.exe) from an engine source checkout, with the person's installed CMake and compiler underneath.
// The recipe is the one in scripts/build-local.ps1 (the same CMake options, targets and output folder), done in C++ so
// no PowerShell is needed; tests/test_okay_cli.cpp checks that the two still agree. build-local.ps1 stays as the way to
// build okay itself on a PC that has no okay yet.
// Programs are found by full path (Process.hpp FindOnPath, never the current folder) and started only through
// Process.hpp. Written from the CMake command-line documentation (cmake -S/-B/-G/-D, cmake --build --config
// --parallel --target, CMAKE_PREFIX_PATH and find_package's search paths) and vswhere's documented options. Our own code.
#include <string>
#include <vector>

#include "Cli.hpp"

namespace okaycli::engine {

/// What `okay engine build` was asked for.
struct BuildOptions {
    std::string what = "all";        ///< editor | launcher | player | all
    bool debug = false;              ///< Debug instead of Release
    int jobs = 0;                    ///< parallel compile jobs; 0 = half the processors, at least 2
    bool clean = false;              ///< delete the build folder first
    bool run = false;                ///< start the editor or launcher afterwards
    std::string dir = "build-local"; ///< the build folder (relative to the source checkout unless absolute)
    std::string source;              ///< the engine checkout (--source); "" = this folder or a parent
};
/// Read `okay engine build` arguments (after "build"). False with a plain reason in `error`.
bool ParseBuildArgs(const std::vector<std::string>& args, BuildOptions& o, std::string& error);

/// `dir` holds an engine checkout: a CMakeLists.txt with project(OkaySpaceGameEngine (or project(OkaySpace).
bool IsEngineSource(const std::string& dir);
/// The engine checkout: `given` when set (and real), else `start` or the nearest parent that is one. "" when none.
std::string FindEngineSource(const std::string& given, const std::string& start);

/// The build programs found on this computer, each a full path ("" when missing).
struct Tools {
    std::string cmake, ninja, gxx, gcc, cl;   ///< from PATH's absolute folders only (cl only on Windows)
    std::string mingwMake;                    ///< mingw32-make (Windows only)
    std::string visualStudio;                 ///< a Visual Studio with the C++ tools (vswhere), its install folder
#if defined(_WIN32)
    bool windows = true;                      ///< building for Windows (tests set it to check the Windows choices)
#else
    bool windows = false;
#endif
};
/// Look for the tools. `vswhere` is the full path of vswhere.exe to ask ("" = the standard place on Windows).
Tools FindTools(const std::string& vswhere = "");

/// Everything one build needs, worked out before anything runs.
struct Plan {
    std::string source, buildDir;     ///< absolute
    std::string config = "Release";   ///< Release | Debug
    std::string generator;            ///< "Ninja", "MinGW Makefiles", or "" (CMake's default: Visual Studio / Unix Makefiles)
    bool multiConfig = false;         ///< Visual Studio: the config is chosen at build time, not CMAKE_BUILD_TYPE
    bool windows = false, mingw = false;
    std::string makeProgram, cCompiler, cxxCompiler;   ///< full paths given to CMake ("" = CMake finds them)
    bool editor = false, launcher = false, player = false, sandbox = false;
    int jobs = 2;
};
/// Make the plan from the options and the tools found. False with a plain reason (what is missing and where to get it).
bool MakePlan(const BuildOptions& o, const Tools& t, const std::string& source, Plan& plan,
              std::string& error);
/// cmake's arguments (after the program name) to configure, as build-local.ps1 passes them, plus the full tool paths.
std::vector<std::string> ConfigureArgs(const Plan& p);
/// cmake's arguments to build the chosen targets.
std::vector<std::string> BuildArgs(const Plan& p);
/// Where the programs are after a build: <build>/bin, or <build>/bin/<config> for a Visual Studio build.
std::string BinDir(const Plan& p);

/// What `okay engine doctor` says: every tool found or missing, in plain words with where to get it.
std::string DoctorText(const Tools& t, const std::string& source, int* problems);

/// `inner` is the folder `outer` or inside it; `ignoreCase` compares as Windows does ("C:/Games" is "c:/games").
bool PathWithin(const std::string& inner, const std::string& outer, bool ignoreCase);
/// Why `okay engine build --clean` may not delete `buildDir`, or "" when it may. It may only when the folder has a
/// CMakeCache.txt whose CMAKE_HOME_DIRECTORY is `source` (this engine checkout), and holds no CMakeLists.txt,
/// project.okayproj or Assets folder (a game project with a cache in it is never deleted). Paths are compared as the
/// file system does: without case when `ignoreCase` (the second form: the host's rule, so Windows ignores case).
std::string CleanRefusal(const std::string& buildDir, const std::string& source, bool ignoreCase);
std::string CleanRefusal(const std::string& buildDir, const std::string& source);

/// The help of `okay engine build` and `okay engine doctor`.
const char* BuildUsage();
const char* DoctorUsage();
/// `okay engine <build|doctor> ...` (args start after "engine").
int EngineMain(const std::vector<std::string>& args, const Io& io);

} // namespace okaycli::engine
