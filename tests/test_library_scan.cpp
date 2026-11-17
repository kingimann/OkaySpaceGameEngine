#include "test_framework.hpp"
#include "LibraryScan.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>

// The launcher's library scan (shared/LibraryScan.hpp). The launcher walks its own folder and games/, Projects/,
// community/ under it, so the same scene is reached twice; the Community page showed every game twice.
int main() {
    RUN_SUITE("library_scan");
    namespace fs = std::filesystem;
    const fs::path base = fs::temp_directory_path() / "okay_library_scan_test";
    std::error_code ec;
    fs::remove_all(base, ec);
    auto touch = [](const fs::path& p) { fs::create_directories(p.parent_path()); std::ofstream(p) << "okayscene 3\n"; };
    touch(base / "community" / "Puzzle Pack" / "level1.okayscene");
    touch(base / "community" / "Neon Drift" / "neon.okayscene");
    touch(base / "community" / "Orbit Hopper" / "Orbit Hopper.okayscene");
    touch(base / "games" / "Racer" / "racer.okayscene");
    touch(base / "Games" / "Official" / "official.okayscene");          // the official game: its own card, not the library
    touch(base / "Projects" / "Mine" / "Assets" / "main.okayscene");
    { std::ofstream(base / "community" / "Neon Drift" / "notes.txt") << "not a scene\n"; }

    const std::vector<fs::path> roots = {base, base / "games", base / "Projects", base / "community",
                                         base / "community" / "Neon Drift",   // a user library inside community/
                                         base / "missing"};                  // a folder that is gone: ignored
    auto skipOfficial = [&](const fs::directory_entry& e) { return e.path().filename() == "Official"; };
    const std::vector<fs::path> found = okaylib::FindScenesUnder(roots, skipOfficial);

    auto count = [&](const std::string& name) {
        return (int)std::count_if(found.begin(), found.end(), [&](const fs::path& p) { return p.filename() == name; });
    };
    CHECK(found.size() == 5);                       // five scenes, each once (it was 11: the overlap listed them again)
    CHECK(count("level1.okayscene") == 1);
    CHECK(count("neon.okayscene") == 1);            // found under base, community and the user folder: still once
    CHECK(count("Orbit Hopper.okayscene") == 1);
    CHECK(count("racer.okayscene") == 1);
    CHECK(count("main.okayscene") == 1);
    CHECK(count("official.okayscene") == 0);        // skipped folder
    CHECK(count("notes.txt") == 0);

    // The same file reached through a "..": the same key.
    CHECK(okaylib::SceneKey(base / "community" / ".." / "games" / "Racer" / "racer.okayscene") ==
          okaylib::SceneKey(base / "games" / "Racer" / "racer.okayscene"));

    // Without a skip rule the official game is listed (once).
    const std::vector<fs::path> all = okaylib::FindScenesUnder({base, base / "Games"});
    CHECK(all.size() == 6);

    // Nothing to scan: nothing found.
    CHECK(okaylib::FindScenesUnder({}).empty());
    CHECK(okaylib::FindScenesUnder({base / "missing"}).empty());

    fs::remove_all(base, ec);
    TEST_MAIN_RESULT();
}
