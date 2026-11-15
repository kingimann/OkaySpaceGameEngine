// KNOWN BUG (issue #888, bug 3): a scene file that starts with a UTF-8 byte-order mark (EF BB BF, which some text editors
// add when they save) does not open with the engine's own loader, SceneSerializer::LoadFromFile: it says "missing 'okayscene'
// header". The upgrade (editor/ProjectUpgrade.cpp) now reads such files and writes them back without the mark, but the loader
// is not changed here (engine/src/Scene/SceneSerializer.cpp, ParseStep's header check, belongs to another change).
// Registered DISABLED unless CMake is given -DOKAY_RUN_KNOWN_BUG_TESTS=ON, like test_upgrade_bugs.cpp.
#include "test_framework.hpp"
#include <Okay.hpp>

#include <chrono>
#include <fstream>

namespace fs = std::filesystem;
using namespace okay;

int main() {
    RUN_SUITE("scene_bom_bug");
    const fs::path dir = fs::temp_directory_path() /
        ("okay_scenebom_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    fs::create_directories(dir, ec);

    Scene made("Bom");
    made.CreateGameObject("Hero");
    const std::string text = SceneSerializer::Serialize(made);
    {
        std::ofstream(dir / "plain.okayscene", std::ios::binary) << text;
        std::ofstream(dir / "bom.okayscene", std::ios::binary) << std::string("\xEF\xBB\xBF") + text;
    }

    {   // control: the same scene without the mark loads
        Scene s("probe");
        std::string err;
        CHECK(SceneSerializer::LoadFromFile(s, (dir / "plain.okayscene").string(), &err));
        CHECK(s.Find("Hero") != nullptr);
    }
    {
        Scene s("probe");
        std::string err;
        const bool ok = SceneSerializer::LoadFromFile(s, (dir / "bom.okayscene").string(), &err);
        std::cerr << "[scene_bom_bug_load_from_file] ok=" << ok << " error='" << err << "'\n";
        CHECK(ok);                                    // KNOWN BUG: "missing 'okayscene' header"
        CHECK(ok && s.Find("Hero") != nullptr);
    }
    fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
