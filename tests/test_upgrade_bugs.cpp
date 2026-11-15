// KNOWN BUG (issue #888 / Upgrade project): the tests below describe how "Upgrade project" SHOULD behave and fail until it is fixed.
// They are a starting point for whoever fixes the feature. They are NOT part of the normal test run: they are registered with
// DISABLED unless CMake is given -DOKAY_RUN_KNOWN_BUG_TESTS=ON (then `ctest -R upgrade_bug`). Written by reading the code and by running
// small experiments on temp folders; each CHECK says what it expects. No engine code is changed by this file.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/SceneMigration.hpp"
#include "../editor/ProjectUpgrade.hpp"
#include "../editor/ProjectState.hpp"

#include <chrono>
#include <fstream>

namespace fs = std::filesystem;
using namespace okay;
using namespace okayup;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_upgbug_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec; fs::remove_all(path, ec); fs::create_directories(path, ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
};
void WriteFile(const fs::path& p, const std::string& s) {
    std::error_code ec; fs::create_directories(p.parent_path(), ec);
    std::ofstream(p, std::ios::binary | std::ios::trunc) << s;
}
std::string ReadFile(const fs::path& p) { std::string s; okayproj::ReadText(p.string(), s); return s; }
bool Has(const std::string& h, const std::string& n) { return h.find(n) != std::string::npos; }

const char* kV1 =
    "okayscene 1\nname \"Old Level\"\ngravity 0 -9.81\n"
    "gameobject 0 \"Hero\"\n  active 1\n  parent -1\n  transform 1 2 0 0 0 0 1 1 1 1\n  sprite 64 1 0 0 1 1 1\n  rigidbody2d 1 1 1 0 0\n  boxcollider2d 1 1 0 0 0\nend\n";

std::string Current(int objects) {
    Scene s("S");
    for (int i = 0; i < objects; ++i) s.CreateGameObject("Obj" + std::to_string(i));
    return SceneSerializer::Serialize(s);
}
std::string ToCrlf(const std::string& t) { std::string o; for (char c : t) { if (c == '\n') o += '\r'; o += c; } return o; }

Options Opts(const fs::path& root, const std::string& from) {
    Options o; o.projectDir = root.string(); o.fromVersion = from; o.toVersion = "2.874.0"; return o;
}
}  // namespace

int main() {
    RUN_SUITE("upgrade_bug");


    // upgrade_bug_0 (issue #888, Grok's finding): editor freeze at startup when the "Project version" question and the "Restore recovered
    // work?" question are both due. Both re-open their own modal every frame, at the same popup level, and close each other: an invisible modal,
    // a dead editor, and the --open scene never opens. There is no headless ImGui in the tests, so this is a SOURCE-LEVEL check of the contract
    // "the recovery popup must not open while the project gate is busy" (the startup chooser already does that with ProjectGateBusy()).
    // Replace it with a real test of the fixed decision (e.g. a pure "which startup modal may ask" function) when Claude 2 writes one.
    {
        const std::string src = ReadFile(fs::path(OKAY_SOURCE_DIR) / "editor" / "main.cpp");
        const std::size_t at = src.find("static void DrawDocRecoveryPopup(EditorState& ed) {");
        CHECK(at != std::string::npos);
        const std::string body = at == std::string::npos ? std::string() : src.substr(at, 600);
        std::cerr << "[upgrade_bug_0_two_startup_modals] recovery popup mentions ProjectGateBusy=" << Has(body, "ProjectGateBusy") << "\n";
        CHECK(Has(body, "ProjectGateBusy"));                     // KNOWN BUG: it opens its modal even while the project gate's modal is open
    }

    // upgrade_bug_1: a project's scene in a folder that is not called "Assets" is not "in the project", so the version question is skipped.
    // Where: editor/ProjectUpgrade.cpp ProjectRootForScene (only an "Assets" ancestor, or the scene's own folder, counts).
    {
        TempDir td;
        const fs::path root = td.path / "Game";
        WriteFile(root / "project.okayproj", "engine_version=2.850.0\n");
        WriteFile(root / "Levels" / "a.okayscene", Current(1));
        const std::string scene = (root / "Levels" / "a.okayscene").string();
        std::cerr << "[upgrade_bug_1_scene_in_subfolder_not_called_Assets] root='" << ProjectRootForScene(scene) << "'\n";
        CHECK(ProjectRootForScene(scene) == root.string());     // KNOWN BUG: returns "" so Open() proceeds without asking and MayLoad() says yes
        GateEnv env; env.running = "2.874.0";
        ProjectGate g(env);
        int go = 0;
        g.Open(scene, [&] { ++go; });
        CHECK(g.Busy() && go == 0);                              // KNOWN BUG: an Older project must ask, wherever the scene sits in it
        std::string why;
        CHECK(!ProjectGate(env).MayLoad(scene, &why));           // KNOWN BUG: the safety net lets the load through too
    }

    // upgrade_bug_2: the upgrade must keep records of a scene this version does not know ("kept as they are" says the log).
    {
        TempDir td;
        const fs::path root = td.path / "Game";
        WriteFile(root / "Assets" / "Old.okayscene", std::string(kV1) + "futurerecord 1 2 3\n");
        Options o = Opts(root, "2.850.0");
        UpgradeJob j(o); j.Run();
        const std::string after = ReadFile(root / "Assets" / "Old.okayscene");
        std::cerr << "[upgrade_bug_2_unknown_records] ok=" << j.result().ok << " error='" << j.result().error << "' has_future=" << Has(after, "futurerecord 1 2 3") << "\n";
        CHECK(j.result().ok);
        CHECK(Has(after, "futurerecord 1 2 3"));                 // KNOWN BUG if this fails: the rewrite drops what it does not know
    }

    // upgrade_bug_3: scene files with Windows line endings (git autocrlf, Notepad) must upgrade like any other.
    {
        TempDir td;
        const fs::path root = td.path / "Game";
        WriteFile(root / "Assets" / "Crlf.okayscene", ToCrlf(Current(2)));
        WriteFile(root / "Assets" / "CrlfOld.okayscene", ToCrlf(kV1));
        UpgradeJob j(Opts(root, "2.850.0")); j.Run();
        std::cerr << "[upgrade_bug_3_crlf_scenes] ok=" << j.result().ok << " error='" << j.result().error << "'\n";
        CHECK(j.result().ok);                                    // KNOWN BUG if it fails: one CRLF scene blocks the whole upgrade
    }

    // upgrade_bug_4: a scene with a UTF-8 BOM (saved by some editors) must upgrade too.
    {
        TempDir td;
        const fs::path root = td.path / "Game";
        WriteFile(root / "Assets" / "Bom.okayscene", std::string("\xEF\xBB\xBF") + Current(1));
        UpgradeJob j(Opts(root, "2.850.0")); j.Run();
        std::cerr << "[upgrade_bug_4_bom_scene] ok=" << j.result().ok << " error='" << j.result().error << "'\n";
        CHECK(j.result().ok);
    }

    // upgrade_bug_5: an unknown-version project (no project.okayproj) with a scene outside Assets/ must still be upgraded and stamped.
    {
        TempDir td;
        const fs::path root = td.path / "Game";
        WriteFile(root / "Assets" / "Main.okayscene", Current(1));
        UpgradeJob j(Opts(root, "")); j.Run();
        CHECK(j.result().ok);
        CHECK(okayver::ReadProjectVersion(root.string()) == "2.874.0");
    }

    // upgrade_bug_6: versions: numeric compare, 4th component, patch lines (controls: these are expected to pass).
    {
        CHECK(okayver::Decide("2.99.0", "2.100.0") == okayver::Decision::Older);
        CHECK(okayver::Decide("2.100.0", "2.99.0") == okayver::Decision::Newer);
        CHECK(okayver::Decide("2.874.0", "2.874.1") == okayver::Decision::SamePatchLine);
        CHECK(okayver::Decide("2.874.0.5", "2.874.0") == okayver::Decision::SamePatchLine);
    }

    // upgrade_bug_7: running the upgrade twice: the second run must find nothing to rewrite and make a second, separate backup.
    {
        TempDir td;
        const fs::path root = td.path / "Game";
        WriteFile(root / "Assets" / "Old.okayscene", kV1);
        UpgradeJob a(Opts(root, "2.850.0")); a.Run();
        UpgradeJob b(Opts(root, "2.874.0")); b.Run();
        CHECK(a.result().ok && b.result().ok && b.result().rewritten == 0);
        CHECK(a.result().backupDir != b.result().backupDir);
    }

    // upgrade_bug_8: a loose scene in a folder that only holds a ".okay" folder (what the editor leaves in a scene's folder
    // after opening it) is not a project: the editor must not ask to "upgrade" it (that would copy the whole folder).
    {
        TempDir td;
        WriteFile(td.path / "examples" / "vehicle" / "vehicle_test.okayscene", Current(1));
        const std::string scene = (td.path / "examples" / "vehicle" / "vehicle_test.okayscene").string();
        GateEnv env; env.running = "2.874.0";
        { ProjectGate g(env); int go = 0; g.Open(scene, [&] { ++go; }); CHECK(go == 1 && !g.Busy()); }   // control: untouched folder, no question
        WriteFile(td.path / "examples" / "vehicle" / ".okay" / "autosave.txt", "x");
        ProjectGate g(env); int go = 0; g.Open(scene, [&] { ++go; });
        std::cerr << "[upgrade_bug_8_loose_scene_folder_with_dot_okay] asked=" << g.Busy() << "\n";
        CHECK(go == 1 && !g.Busy());                              // KNOWN BUG (design): IsProjectRoot() counts a bare .okay folder as a project
    }

    TEST_MAIN_RESULT();
}
