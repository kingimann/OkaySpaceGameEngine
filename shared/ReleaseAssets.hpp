// Which release files belong to which platform. Header-only and free of SDL / ImGui / the engine so it can be tested on its own
// (tests/test_release_assets.cpp).
//
// A release publishes one set of files per platform, under names that cannot collide (the same names are GitHub release assets, bucket
// files "v<version>/<name>" and the keys of versions.json "files"):
//
//   Windows  OkaySpace.exe           OkayEngine.exe           OkaySpacePlayer.exe           (unchanged since the first release)
//   Linux    OkaySpace-linux-x64     OkayEngine-linux-x64     OkaySpacePlayer-linux-x64
//
// The "index name" is the name on the server and in versions.json; the "disk name" is what the file is called once installed
// (Windows keeps ".exe"; Linux uses the plain program name, like the tarball layout in docs/packaging.md). macOS has no release
// files yet: its names are empty, and the launcher says so instead of downloading a Windows program.
#pragma once
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace okayrel {

enum class Platform { Windows, Linux, MacOS };
enum class Role { Launcher, Editor, Player };

/// The platform this program was compiled for.
constexpr Platform HostPlatform() {
#if defined(_WIN32)
    return Platform::Windows;
#elif defined(__APPLE__)
    return Platform::MacOS;
#else
    return Platform::Linux;
#endif
}

constexpr const char* PlatformName(Platform p) {
    return p == Platform::Windows ? "Windows" : p == Platform::Linux ? "Linux" : "macOS";
}

/// The name on the server / in versions.json. "" = this platform has no release file for that role.
constexpr const char* IndexName(Platform p, Role r) {
    switch (p) {
        case Platform::Windows:
            return r == Role::Launcher ? "OkaySpace.exe" : r == Role::Editor ? "OkayEngine.exe" : "OkaySpacePlayer.exe";
        case Platform::Linux:
            return r == Role::Launcher ? "OkaySpace-linux-x64" : r == Role::Editor ? "OkayEngine-linux-x64" : "OkaySpacePlayer-linux-x64";
        case Platform::MacOS:
            return "";
    }
    return "";
}

/// The name of the installed file. Same as the index name on Windows; the plain program name elsewhere.
constexpr const char* DiskName(Platform p, Role r) {
    if (p == Platform::Windows) return IndexName(p, r);
    return r == Role::Launcher ? "OkaySpace" : r == Role::Editor ? "OkayEngine" : "OkaySpacePlayer";
}

/// True when releases carry files for this platform.
constexpr bool HasRelease(Platform p) { return IndexName(p, Role::Launcher)[0] != '\0'; }

/// Every file the updater downloads for a platform (empty when there is no release for it).
inline std::vector<std::string> RequiredIndexNames(Platform p) {
    std::vector<std::string> v;
    if (!HasRelease(p)) return v;
    for (Role r : {Role::Launcher, Role::Editor, Role::Player}) v.push_back(IndexName(p, r));
    return v;
}

/// The platform a versions.json file name belongs to, or false when it is none of the known release files.
inline bool PlatformOfIndexName(const std::string& name, Platform* out = nullptr) {
    for (Platform p : {Platform::Windows, Platform::Linux})
        for (Role r : {Role::Launcher, Role::Editor, Role::Player})
            if (name == IndexName(p, r)) { if (out) *out = p; return true; }
    return false;
}

/// "<base>/<index name>" with exactly one slash between them; "" when the platform has no file for the role.
inline std::string AssetUrl(std::string base, Platform p, Role r) {
    const std::string name = IndexName(p, r);
    if (name.empty()) return {};
    if (!base.empty() && base.back() != '/') base += '/';
    return base + name;
}

inline bool IsSha256Hex(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

/// Which of the platform's release files are missing a checksum, given the index's checksum by file name ("" = listed without one).
inline std::vector<std::string> MissingChecksums(Platform p, const std::map<std::string, std::string>& shaByName) {
    std::vector<std::string> out;
    for (const std::string& n : RequiredIndexNames(p)) {
        const auto it = shaByName.find(n);
        if (it == shaByName.end() || !IsSha256Hex(it->second)) out.push_back(n);
    }
    return out;
}

/// Why a release cannot be installed on this platform, judged from which of ITS files the index lists ("" = fine, or Windows: those
/// files are checked one by one by the updater, exactly as before). Only a non-Windows platform gets the "no build for you" wording, so
/// someone on Linux who sees a Windows-only release is told that, rather than that a file is "not listed".
inline std::string PlatformBuildProblem(Platform p, const std::map<std::string, std::string>& shaByName, const std::string& version) {
    if (p == Platform::Windows) return {};
    if (!HasRelease(p))
        return std::string("There is no ") + PlatformName(p) + " build of OkaySpace to update to yet, so nothing was installed.";
    bool any = false;
    for (const std::string& n : RequiredIndexNames(p)) if (shaByName.count(n)) any = true;
    if (!any)
        return "v" + version + " has no " + PlatformName(p) + " build (its release lists no " + PlatformName(p) +
               " files). Nothing was installed and your current version is unchanged; a " + PlatformName(p) +
               " build may be published a little later, so try again.";
    return {};
}

}  // namespace okayrel
