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

    // ---- IsExportedGame: the launcher plays exported games only (owner, 2026-11-28) ----
    // Before this rule the launcher's IsBuiltGame looked only for a game.okayconfig beside the scene, so a data folder
    // with no program (or a project someone copied a config into) counted as a game; "Play (no export)" ran raw projects.
    {
        const fs::path ex = fs::temp_directory_path() / "okay_exported_game_test";
        fs::remove_all(ex, ec);
        auto put = [](const fs::path& p, const char* text) { fs::create_directories(p.parent_path()); std::ofstream(p) << text; };
        auto program = [&](const fs::path& p) {
            put(p, "MZ");
            std::error_code pe; fs::permissions(p, fs::perms::owner_all, fs::perm_options::add, pe);
        };
        // The folder layout: Racer.exe + Racer_Data/{game.okayconfig, game.okayscene}.
        program(ex / "Racer" / "Racer.exe");
        put(ex / "Racer" / "Racer_Data" / "game.okayconfig", "title=Racer\n");
        put(ex / "Racer" / "Racer_Data" / "game.okayscene", "okayscene 3\n");
        // The compact layout with a data folder (also anti-cheat): Hop.exe + Data/.
        program(ex / "Hop" / "Hop.exe");
        put(ex / "Hop" / "Data" / "game.okayconfig", "title=Hop\n");
        put(ex / "Hop" / "Data" / "game.okayscene", "okayscene 3\n");
        // The compact layout: everything in one folder.
        program(ex / "Flat" / "Flat.exe");
        put(ex / "Flat" / "game.okayconfig", "title=Flat\n");
        put(ex / "Flat" / "game.okayscene", "okayscene 3\n");
        // An export the editor put inside its project (the default output folder is relative to the project).
        put(ex / "Proj" / "project.okayproj", "name=Proj\n");
        put(ex / "Proj" / "Assets" / "main.okayscene", "okayscene 3\n");
        program(ex / "Proj" / "Builds" / "Proj" / "Proj.exe");
        put(ex / "Proj" / "Builds" / "Proj" / "Proj_Data" / "game.okayconfig", "title=Proj\n");
        put(ex / "Proj" / "Builds" / "Proj" / "Proj_Data" / "game.okayscene", "okayscene 3\n");
        // Not exported: a data folder with no program (a web/Android data export, or a half-copied game).
        put(ex / "NoExe" / "Data" / "game.okayconfig", "title=NoExe\n");
        put(ex / "NoExe" / "Data" / "game.okayscene", "okayscene 3\n");
        // Not exported: a project that has a game.okayconfig and a program copied in.
        put(ex / "Fake" / "project.okayproj", "name=Fake\n");
        put(ex / "Fake" / "game.okayconfig", "title=Fake\n");
        put(ex / "Fake" / "game.okayscene", "okayscene 3\n");
        program(ex / "Fake" / "Fake.exe");
        // Not exported: a loose scene, and a scene in a folder that only has a program.
        put(ex / "Loose" / "level.okayscene", "okayscene 3\n");
        program(ex / "ExeOnly" / "ExeOnly.exe");
        put(ex / "ExeOnly" / "level.okayscene", "okayscene 3\n");

        CHECK(okaylib::IsExportedGame(ex / "Racer" / "Racer_Data" / "game.okayscene"));
        CHECK(okaylib::ExportedGameRoot(ex / "Racer" / "Racer_Data" / "game.okayscene") == ex / "Racer");
        CHECK(okaylib::IsExportedGame(ex / "Racer"));                                  // the export's folder
        CHECK(okaylib::IsExportedGame(ex / "Hop" / "Data" / "game.okayscene"));
        CHECK(okaylib::ExportedGameRoot(ex / "Hop" / "Data" / "game.okayscene") == ex / "Hop");
        CHECK(okaylib::IsExportedGame(ex / "Flat" / "game.okayscene"));
        CHECK(okaylib::ExportedGameRoot(ex / "Flat") == ex / "Flat");
        CHECK(okaylib::IsExportedGame(ex / "Proj" / "Builds" / "Proj" / "Proj_Data" / "game.okayscene"));

        CHECK(!okaylib::IsExportedGame(ex / "Proj"));                                  // a project folder
        CHECK(!okaylib::IsExportedGame(ex / "Proj" / "project.okayproj"));             // an .okayproj
        CHECK(!okaylib::IsExportedGame(ex / "Proj" / "Assets" / "main.okayscene"));    // a project's scene
        CHECK(!okaylib::IsExportedGame(ex / "NoExe" / "Data" / "game.okayscene"));     // no program
        CHECK(!okaylib::IsExportedGame(ex / "NoExe"));
        CHECK(!okaylib::IsExportedGame(ex / "Fake" / "game.okayscene"));               // a project, whatever it holds
        CHECK(!okaylib::IsExportedGame(ex / "Fake"));
        CHECK(!okaylib::IsExportedGame(ex / "Loose" / "level.okayscene"));             // a loose scene
        CHECK(!okaylib::IsExportedGame(ex / "ExeOnly" / "level.okayscene"));           // no game.okayconfig
        CHECK(!okaylib::IsExportedGame(ex / "Missing" / "game.okayscene"));            // nothing there
        CHECK(!okaylib::IsExportedGame(fs::path()));
        CHECK(!okaylib::IsExportedGame(ex / "Racer" / "Racer_Data" / "game.okayconfig"));   // not a scene
        CHECK(std::string(okaylib::NotExportedMessage()).find("Export") != std::string::npos);
        fs::remove_all(ex, ec);
    }
    TEST_MAIN_RESULT();
}
