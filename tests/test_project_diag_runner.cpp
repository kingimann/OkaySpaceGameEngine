// The Problems list's checks (editor/ProjectDiagRunner.*): the project-wide check reads every text asset of the project, and the
// owner's okay_editor_perf.log (2026-11-25) shows UI.Diagnostics at 1.0-1.3 s in many frames: it ran in the frame on every scene
// change. Here a project of 300 scene files is checked while the scene changes 40 times: no text asset may be read on the
// calling thread, the project-wide check runs once (not 40 times), and its result (a missing file named by a scene on disk)
// still reaches the list, merged with the scene's own problems without doubles.
#include "test_framework.hpp"
#include "ProjectDiagRunner.hpp"
#include <Okay.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;
using namespace okay;

namespace {
int CountCode(const std::vector<Diagnostic>& v, const char* code) { int n = 0; for (const auto& d : v) if (d.code == code) ++n; return n; }
double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
}

int main() {
    const fs::path root = fs::temp_directory_path() / ("okay_diagrun_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "Assets" / "Levels");
    for (int i = 0; i < 300; ++i)
        std::ofstream(root / "Assets" / "Levels" / ("level" + std::to_string(i) + ".okayscene")) << "# scene " << i << "\n";
    std::ofstream(root / "Assets" / "broken.okaymat") << "texture \"Textures/gone.png\"\n";   // names a file that is not there

    RUN_SUITE("the project-wide check runs off the editor's thread, once, while the scene changes");
    {
        Scene scene;
        scene.CreateGameObject("A");
        oked::ProjectDiagRunner run;
        run.refreshSeconds = 1000.0;
        const std::uint64_t before = ProjectAssetFilesReadOnThisThread();
        const double t0 = Now();
        for (int i = 0; i < 40; ++i) {
            scene.CreateGameObject("B" + std::to_string(i));            // the scene changes every frame
            run.Tick(scene, (std::uint64_t)i + 1, root.generic_string(), t0 + i * 0.5);
        }
        CHECK(ProjectAssetFilesReadOnThisThread() == before);   // before the fix: 301 files read per scene change, here
        CHECK(run.AssetPassesStarted() == 1);
        const auto w0 = std::chrono::steady_clock::now();
        while (run.AssetPassRunning() && std::chrono::steady_clock::now() - w0 < std::chrono::seconds(20)) {
            run.Tick(scene, 41, root.generic_string(), t0 + 30.0);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        run.Tick(scene, 41, root.generic_string(), t0 + 30.0);
        CHECK(!run.AssetPassRunning());
        CHECK(CountCode(run.Items(), "asset.db.missing") == 1);   // the project-wide result is in the list
        CHECK(CountCode(run.Items(), "scene.no_camera") == 1);    // and the scene's own
        CHECK(run.AssetPassesStarted() == 1);
        // Files changed: one more pass.
        run.InvalidateFiles();
        run.Tick(scene, 41, root.generic_string(), t0 + 31.0);
        CHECK(run.AssetPassesStarted() == 2);
    }

    RUN_SUITE("another project while a pass runs: the old result is dropped; the runner can go away mid-pass");
    {
        Scene scene;
        oked::ProjectDiagRunner run;
        run.Tick(scene, 1, root.generic_string(), 0.0);
        run.Tick(scene, 1, "", 0.1);                              // project closed
        const auto w0 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - w0 < std::chrono::milliseconds(800)) { run.Tick(scene, 1, "", 0.2); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        CHECK(CountCode(run.Items(), "asset.db.missing") == 0);
        { oked::ProjectDiagRunner gone; gone.Tick(scene, 1, root.generic_string(), 0.0); }   // destroyed with its pass running
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    std::error_code ec;
    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
