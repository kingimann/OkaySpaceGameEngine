#pragma once
#include <string>

namespace okay {

/// What this engine build is: version, compiler, configuration, platform. For the
/// startup log, an About box, crash reports, and the version a client sends a server.
/// The version is OKAY_ENGINE_VERSION (the CMake project version), "dev" if a build
/// did not set it.
class BuildInfo {
public:
    static const char* Version();        ///< e.g. "2.847.0"
    static const char* Compiler();       ///< e.g. "GCC 13.2" / "Clang 17.0" / "MSVC 1939"
    static const char* Config();         ///< "Debug" or "Release" (by NDEBUG)
    static const char* Platform();       ///< "Windows", "Linux", "macOS", ...
    static const char* Architecture();   ///< "x64", "arm64", "x86", ...
    static const char* Commit();         ///< OKAY_BUILD_COMMIT when the build set it, else "unknown"
    /// "OkaySpace 2.847.0 (Release, GCC 13.2, Linux x64)"
    static std::string Summary();

    /// "a.b.c" -> numbers (missing parts 0; false if it isn't numeric).
    static bool ParseVersion(const std::string& text, int& major, int& minor, int& patch);
    /// <0, 0, >0 like strcmp, comparing numeric parts: "2.10.0" is newer than "2.9.9".
    static int  CompareVersions(const std::string& a, const std::string& b);
    /// True when a client of version `theirs` is on the same major.minor as this build.
    static bool IsCompatible(const std::string& theirs);
};

} // namespace okay
