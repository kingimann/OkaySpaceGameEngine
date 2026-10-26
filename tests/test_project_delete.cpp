#include "test_framework.hpp"
#include "ProjectDelete.hpp"
#include <filesystem>
#include <fstream>

// The launcher's "Delete project": only a real project folder can go, never a
// drive root, the home folder, the launcher's folder or the projects folder.
int main() {
    RUN_SUITE("project_delete");
    namespace fs = std::filesystem;
    using okayproj::DeleteRefusal;

    const fs::path base = fs::temp_directory_path() / "okay_project_delete_test";
    fs::remove_all(base);
    const fs::path projects = base / "Projects", home = base / "home", launcher = base / "launcher";
    fs::create_directories(projects / "Game" / "Assets");
    { std::ofstream(projects / "Game" / "Assets" / "Game.okayscene") << "name \"Game\"\n"; }
    fs::create_directories(projects / "Shell" / "Assets");
    fs::create_directories(projects / "Plain" / "Assets");
    { std::ofstream(projects / "Plain" / "Assets" / "readme.txt") << "hi\n"; }
    fs::create_directories(projects / "Loose");
    { std::ofstream(projects / "Loose" / "level.okayscene") << "name \"Loose\"\n"; }
    fs::create_directories(home / "Assets");
    { std::ofstream(home / "Assets" / "x.okayscene") << "\n"; }
    fs::create_directories(launcher);
    const std::vector<okayproj::Kept> keep = {
        {home, "your home folder"}, {launcher, "the launcher"}, {projects, "your projects folder"}};

    CHECK(okayproj::RootOfScene(projects / "Game" / "Assets" / "Game.okayscene") == projects / "Game");
    CHECK(okayproj::RootOfScene(projects / "Loose" / "level.okayscene") == projects / "Loose");

    CHECK(okayproj::IsProjectFolder(projects / "Game"));
    CHECK(okayproj::IsProjectFolder(projects / "Shell"));      // just made, scene not saved yet
    CHECK(!okayproj::IsProjectFolder(projects / "Plain"));
    CHECK(!okayproj::IsProjectFolder(projects / "Loose"));
    CHECK(!okayproj::IsProjectFolder(projects / "Missing"));

    CHECK(DeleteRefusal(projects / "Game", keep).empty());
    CHECK(DeleteRefusal(projects / "Shell", keep).empty());
    CHECK(DeleteRefusal(fs::path((projects / "Game").string() + "/"), keep).empty());

    CHECK(!DeleteRefusal("", keep).empty());
    CHECK(!DeleteRefusal(fs::path("/"), keep).empty());
    CHECK(!DeleteRefusal(projects.root_path(), keep).empty());
    CHECK(!DeleteRefusal(projects / "Missing", keep).empty());
    CHECK(!DeleteRefusal(projects / "Plain", keep).empty());
    CHECK(!DeleteRefusal(projects / "Loose", keep).empty());
    CHECK(!DeleteRefusal(home, keep).empty());
    CHECK(!DeleteRefusal(launcher, keep).empty());

    // A folder that holds a kept one, even when it looks like a project.
    fs::create_directories(projects / "Assets");
    { std::ofstream(projects / "Assets" / "y.okayscene") << "\n"; }
    fs::create_directories(base / "Assets");
    { std::ofstream(base / "Assets" / "z.okayscene") << "\n"; }
    CHECK(okayproj::IsProjectFolder(projects) && okayproj::IsProjectFolder(base));
    CHECK(DeleteRefusal(projects, keep).find("projects folder") != std::string::npos);
    CHECK(!DeleteRefusal(base, keep).empty());
    CHECK(!DeleteRefusal(projects / "Game" / "..", keep).empty());
    CHECK(!DeleteRefusal(fs::path(projects.string() + "/"), keep).empty());
    CHECK(DeleteRefusal(projects / "Game", {{fs::path(projects.string() + "/"), "x"}}).empty());

    // A sibling whose name starts with a kept folder's isn't inside it.
    fs::create_directories(base / "home2" / "Assets");
    CHECK(DeleteRefusal(base / "home2", keep).empty());
    CHECK(!okayproj::IsSameOrInside(base / "home2", home));
    CHECK(okayproj::IsSameOrInside(home / "a" / "b", home));

    // A link to a project is not the project.
    std::error_code lec;
    fs::create_directory_symlink(projects / "Game", base / "GameLink", lec);
    if (!lec) {
        CHECK(!DeleteRefusal(base / "GameLink", keep).empty());
        CHECK(!DeleteRefusal(fs::path((base / "GameLink").string() + "/"), keep).empty());
    }

    fs::remove_all(base);
    TEST_MAIN_RESULT();
}
