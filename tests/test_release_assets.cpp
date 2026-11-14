// Which release files belong to which platform (shared/ReleaseAssets.hpp): the Windows names are exactly the ones used since the
// first release, Linux gets its own non-colliding names, macOS has none yet, and the pure "no build for you / missing checksum"
// decisions the launcher's updater makes. Header-only: links nothing.
#include "test_framework.hpp"
#include "../shared/ReleaseAssets.hpp"

#include <map>
#include <set>
#include <string>

using namespace okayrel;

int main() {
    RUN_SUITE("Windows names are unchanged");
    CHECK(std::string(IndexName(Platform::Windows, Role::Launcher)) == "OkaySpace.exe");
    CHECK(std::string(IndexName(Platform::Windows, Role::Editor)) == "OkayEngine.exe");
    CHECK(std::string(IndexName(Platform::Windows, Role::Player)) == "OkaySpacePlayer.exe");
    for (Role r : {Role::Launcher, Role::Editor, Role::Player})
        CHECK(std::string(DiskName(Platform::Windows, r)) == IndexName(Platform::Windows, r));
    CHECK((RequiredIndexNames(Platform::Windows) == std::vector<std::string>{"OkaySpace.exe", "OkayEngine.exe", "OkaySpacePlayer.exe"}));
    CHECK(AssetUrl("https://h/v1", Platform::Windows, Role::Editor) == "https://h/v1/OkayEngine.exe");
    CHECK(AssetUrl("https://h/v1/", Platform::Windows, Role::Editor) == "https://h/v1/OkayEngine.exe");

    RUN_SUITE("Linux names");
    CHECK(std::string(IndexName(Platform::Linux, Role::Launcher)) == "OkaySpace-linux-x64");
    CHECK(std::string(IndexName(Platform::Linux, Role::Editor)) == "OkayEngine-linux-x64");
    CHECK(std::string(IndexName(Platform::Linux, Role::Player)) == "OkaySpacePlayer-linux-x64");
    CHECK(std::string(DiskName(Platform::Linux, Role::Launcher)) == "OkaySpace");
    CHECK(std::string(DiskName(Platform::Linux, Role::Editor)) == "OkayEngine");
    CHECK(std::string(DiskName(Platform::Linux, Role::Player)) == "OkaySpacePlayer");
    CHECK(AssetUrl("https://h/releases/latest/download", Platform::Linux, Role::Player) == "https://h/releases/latest/download/OkaySpacePlayer-linux-x64");
    // Names are distinct across platforms, and no Linux name ends in .exe or looks like a Windows one.
    std::set<std::string> seen;
    for (Platform p : {Platform::Windows, Platform::Linux})
        for (Role r : {Role::Launcher, Role::Editor, Role::Player}) {
            const std::string n = IndexName(p, r);
            CHECK(seen.insert(n).second);
            CHECK(n.find('/') == std::string::npos && n.find("..") == std::string::npos && n.size() <= 64);   // fits versions.json / IsSafeFileName
            CHECK((p == Platform::Linux) == (n.size() < 4 || n.compare(n.size() - 4, 4, ".exe") != 0));
        }

    RUN_SUITE("macOS has no release yet");
    CHECK(!HasRelease(Platform::MacOS) && HasRelease(Platform::Linux) && HasRelease(Platform::Windows));
    CHECK(RequiredIndexNames(Platform::MacOS).empty());
    CHECK(AssetUrl("https://h", Platform::MacOS, Role::Editor).empty());

    RUN_SUITE("which platform a file name is from");
    Platform p = Platform::Windows;
    CHECK(PlatformOfIndexName("OkayEngine-linux-x64", &p) && p == Platform::Linux);
    CHECK(PlatformOfIndexName("OkaySpace.exe", &p) && p == Platform::Windows);
    CHECK(!PlatformOfIndexName("SDL2.dll") && !PlatformOfIndexName("") && !PlatformOfIndexName("okayspace-relay-linux-x64"));

    RUN_SUITE("checksums and the no-build refusal");
    const std::string sha(64, 'a');
    std::map<std::string, std::string> winOnly{{"OkaySpace.exe", sha}, {"OkayEngine.exe", sha}, {"OkaySpacePlayer.exe", sha}};
    CHECK(MissingChecksums(Platform::Windows, winOnly).empty());
    CHECK(MissingChecksums(Platform::Linux, winOnly).size() == 3);   // a Windows-only release has nothing for Linux
    CHECK(!PlatformBuildProblem(Platform::Linux, winOnly, "2.900.0").empty());
    CHECK(PlatformBuildProblem(Platform::Linux, winOnly, "2.900.0").find("no Linux build") != std::string::npos);
    CHECK(PlatformBuildProblem(Platform::Windows, {}, "2.900.0").empty());             // Windows: untouched, the updater's own per-file checks apply
    CHECK(PlatformBuildProblem(Platform::Windows, {{"x", ""}}, "2.900.0").empty());
    CHECK(!PlatformBuildProblem(Platform::MacOS, winOnly, "2.900.0").empty());
    std::map<std::string, std::string> both = winOnly;
    both["OkaySpace-linux-x64"] = sha; both["OkayEngine-linux-x64"] = sha; both["OkaySpacePlayer-linux-x64"] = sha;
    CHECK(MissingChecksums(Platform::Linux, both).empty() && MissingChecksums(Platform::Windows, both).empty());
    CHECK(PlatformBuildProblem(Platform::Linux, both, "2.900.0").empty());
    both["OkayEngine-linux-x64"] = "";                                                  // listed without a checksum
    CHECK((MissingChecksums(Platform::Linux, both) == std::vector<std::string>{"OkayEngine-linux-x64"}));
    both["OkayEngine-linux-x64"] = std::string(64, 'G');                                // not hex
    CHECK(MissingChecksums(Platform::Linux, both).size() == 1);
    both["OkayEngine-linux-x64"] = std::string(63, 'a');                                // wrong length
    CHECK(MissingChecksums(Platform::Linux, both).size() == 1);
    // Partly published (some Linux files listed) is not "no build": the per-file checksum refusal says which file.
    std::map<std::string, std::string> part = winOnly;
    part["OkaySpace-linux-x64"] = sha;
    CHECK(PlatformBuildProblem(Platform::Linux, part, "2.900.0").empty() && MissingChecksums(Platform::Linux, part).size() == 2);

    RUN_SUITE("host platform");
#if defined(_WIN32)
    CHECK(HostPlatform() == Platform::Windows);
#elif defined(__APPLE__)
    CHECK(HostPlatform() == Platform::MacOS);
#else
    CHECK(HostPlatform() == Platform::Linux);
#endif
    TEST_MAIN_RESULT();
}
