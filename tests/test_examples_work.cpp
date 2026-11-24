// Every example scene we ship, and every File > Examples entry, is PROVEN to work.
//
// Data-driven over every examples/**/*.okayscene in the source tree (the folder is copied into a temp folder first, so
// nothing a scene writes can land in the checkout). For each scene:
//
//   load       the editor opens it (EditorState::Load, what File > Open and File > Examples do), and the engine's loader
//              skips no record it does not know (no "unknown records")
//   problems   the Problems panel's own model (CollectProjectDiagnostics) reports no error; the scene's folder is the
//              asset root, because a scene finds its files next to itself (docs/try_it.md)
//   play60     Play runs 60 fixed frames with no error line in the Console and no exception, and Stop restores the scene
//
// Then the File > Examples entries that are not plain files:
//
//   UI Gallery and UI Gallery (phone)   built by oked::BuildUIGalleryExample into a project: it saves, reopens, round-trips
//                                       to the same text, has no Problems error and plays
//   Visual Script Node Playground       every demo in VsPlaygroundDemos() has its scene and its graph file, and the object it
//                                       selects exists in the scene
//   Node Playground (flow and variables) found by NodePlaygroundScenes() the way the menu finds it
//
// And the packaging: every folder a menu entry needs is in the Windows release's example list (scripts/build-windows-dist.sh),
// so File > Examples is not empty on the release.
//
// A scene that cannot run in a plain headless build goes in kSkips with its REASON; the list is printed every run. It is
// meant to stay empty or tiny. Not covered: a real window, pixels, Windows, audio output.
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include "play_smoke.hpp"
#include "okay/Scripting/CSharpHost.hpp"   // DotnetRuntime::CompiledIn: C# scenes need a .NET build
#include "EditorState.hpp"
#include "ExampleScenes.hpp"
#include "NodePlayground.hpp"
#include "VsPlayground.hpp"
#include "../shared/ProjectCreate.hpp"
#include <Okay.hpp>
#include "okay/Debug/ProjectDiagnostics.hpp"
#include "okay/Components/WorldSpaceUI.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
using okay::editor::EditorState;
namespace fs = std::filesystem;

namespace {

constexpr float DT = 1.0f / 60.0f;

// A scene that cannot be run headless, by its path under examples/, and why. Keep it tiny and visible.
struct Skip { const char* path; const char* why; };
const std::vector<Skip> kSkips = {
    // {"folder/scene.okayscene", "needs a network / a missing optional asset / an own-reader switch / another build option"},
};

using okaytest::LogCatch;   // what the Console says while a scene runs (tests/play_smoke.hpp)

std::string ReadAll(const fs::path& p) { std::string s; okayproj::ReadText(p.string(), s); return s; }

struct Result {
    std::string rel;
    bool skipped = false;
    std::string skipWhy;
    std::vector<std::string> fails;     // each one a failed check, in words
    std::size_t objects = 0;
    int problemWarnings = 0;
    int playWarnings = 0;
};

// Play `ed` for `frames` fixed frames; returns what went wrong (empty = nothing). When `needSight`, the picture must show
// something once the first frame has run (a camera pointing at nothing is a scene that "works" and shows a black screen).
std::vector<std::string> PlayFrames(EditorState& ed, int frames, LogCatch& logs, bool needSight = true) {
    std::vector<std::string> bad;
    const std::size_t errorsBefore = logs.errors.size();
    Input::FeedKeys({});
    Input::FeedMouse({640, 360}, 0);
    try {
        ed.Play();
        if (!ed.isPlaying()) bad.push_back("Play did not start: " + ed.PlayBlockedReason());
        for (int i = 0; i < frames && ed.isPlaying(); ++i) {
            Input::FeedKeys({}); Input::FeedMouse({640, 360}, 0); ed.Tick(DT);
            if (i == 0 && needSight) {
                std::string seen;
                if (!okaytest::SomethingVisible(ed.scene(), &seen)) bad.push_back("when Play starts the camera shows nothing: " + seen);
                int drawn = 0, inView = 0;
                okaytest::DrawnInView(ed.scene(), drawn, inView);
                if (drawn > 0 && inView == 0)
                    bad.push_back("the scene has " + std::to_string(drawn) + " drawn objects and none is in front of the camera: the camera looks away from its content (the engine's camera looks down -Z)");
            }
        }
        ed.Stop();
    } catch (const std::exception& e) { bad.push_back(std::string("exception: ") + e.what()); }
    catch (...) { bad.push_back("unknown exception"); }
    for (std::size_t i = errorsBefore; i < logs.errors.size(); ++i) {
        // examples/security_paths shows the file-path gate refusing bad names on purpose (docs/online/security.md): those lines are the point.
        if (logs.errors[i].find("[script] ") == 0 && logs.errors[i].find(": refusing file '") != std::string::npos) continue;
        bad.push_back("Console error during Play: " + logs.errors[i]);
    }
    return bad;
}

void RunScene(const fs::path& copyRoot, const fs::path& scene, Result& r) {
    r.rel = scene.lexically_relative(copyRoot).generic_string();
    for (const Skip& s : kSkips) if (r.rel == s.path) { r.skipped = true; r.skipWhy = s.why; return; }
    // A C# scene runs only where the build hosts .NET (OKAY_WITH_CSHARP, off by default; rule 24c).
    if (!okay::csharp::DotnetRuntime::CompiledIn() && ReadAll(scene).find("script \"csharp\"") != std::string::npos) {
        r.skipped = true;
        r.skipWhy = "a C# scene: this build does not host .NET (OKAY_WITH_CSHARP is OFF)";
        return;
    }
    LogCatch logs;

    // load: the engine's loader on its own (to see skipped records), then the editor
    {
        Scene s("check");
        std::string err; std::vector<std::string> warn;
        const bool ok = SceneSerializer::LoadFromFile(s, scene.string(), &err, &warn);
        if (!ok) { r.fails.push_back("load: " + err); return; }
        for (const std::string& w : warn) r.fails.push_back("load skipped a record it does not know: " + w);
        std::size_t unknown = s.unknownRecords.size();
        for (const auto& up : s.Objects()) unknown += up->unknownRecords.size();
        if (unknown) r.fails.push_back("load kept " + std::to_string(unknown) + " unknown records");
        r.objects = s.Objects().size();

        // problems: the Problems panel's own model, the scene's folder as the asset root
        ProjectDiagOptions opt;
        opt.assetRoot = scene.parent_path().string();
        opt.checkAssetIndex = false;   // a first scan writes .okaymeta files; this test only reads
        for (const Diagnostic& d : CollectProjectDiagnostics(s, opt).items) {
            if (d.severity == DiagSeverity::Error)
                r.fails.push_back("problems: [" + d.code + "] " + d.message + (d.source.objectPath.empty() ? "" : " (" + d.source.objectPath + ")"));
            else if (d.severity == DiagSeverity::Warning) ++r.problemWarnings;
        }
    }
    EditorState ed;
    std::string err;
    if (!ed.Load(scene.string(), &err)) { r.fails.push_back("the editor could not open it: " + err); return; }
    const std::string before = SceneSerializer::Serialize(ed.scene());
    for (const std::string& b : PlayFrames(ed, 60, logs)) r.fails.push_back("play60: " + b);
    if (SceneSerializer::Serialize(ed.scene()) != before) r.fails.push_back("play60: Stop did not put the scene back as it was");
    r.playWarnings = (int)logs.warnings.size();
}

// ---- the packaging: which example folders the Windows release carries ---------------------------------------------------

std::vector<std::string> DistExampleDirs() {
    // EXAMPLE_DIRS="..." lists one folder per line (sorted); the list ends at the closing quote, which may be on a later line.
    std::vector<std::string> dirs;
    const std::string text = ReadAll(fs::path(OKAY_SOURCE_DIR) / "scripts" / "build-windows-dist.sh");
    const std::string key = "\nEXAMPLE_DIRS=\"";
    std::size_t at = text.find(key);
    if (at == std::string::npos) return dirs;
    at += key.size();
    const std::size_t end = text.find('"', at);
    if (end == std::string::npos) return dirs;
    std::istringstream words(text.substr(at, end - at));
    std::string w;
    while (words >> w) dirs.push_back(w);
    return dirs;
}

}  // namespace

int main() {
    RUN_SUITE("examples_work");
    okaytest::Sandbox sandbox("examples_work", OKAY_SOURCE_DIR);   // the working folder is a temp folder; the source tree is guarded
    Log::console = false;
    std::error_code ec;

    // ---- the packaging ----
    {
        const std::vector<std::string> dist = DistExampleDirs();
        CHECK(!dist.empty());   // if the script's list moved, update DistExampleDirs() (this must not skip silently)
        for (const std::string& d : dist) {
            CHECK(fs::is_directory(fs::path(OKAY_EXAMPLES_DIR) / d, ec));   // a listed folder that is not there breaks the release build
            if (!fs::is_directory(fs::path(OKAY_EXAMPLES_DIR) / d, ec)) std::fprintf(stderr, "  scripts/build-windows-dist.sh lists examples/%s, which is not there\n", d.c_str());
        }
        // The folders the File > Examples menu opens scenes from must be in the release.
        for (const char* needed : {"node_playground", "visual_script/playground"}) {
            const bool shipped = std::find(dist.begin(), dist.end(), needed) != dist.end();
            CHECK(shipped);
            if (!shipped) std::fprintf(stderr, "  File > Examples opens scenes from examples/%s but scripts/build-windows-dist.sh does not ship it\n", needed);
        }
    }

    // ---- every scene in the examples folder, from a copy ----
    const fs::path copy = sandbox.Dir() / "examples";
    fs::copy(OKAY_EXAMPLES_DIR, copy, fs::copy_options::recursive, ec);
    CHECK(!ec);
    std::vector<fs::path> scenes;
    for (const auto& e : fs::recursive_directory_iterator(copy, ec))
        if (e.is_regular_file() && e.path().extension() == ".okayscene") scenes.push_back(e.path());
    std::sort(scenes.begin(), scenes.end());
    CHECK(scenes.size() >= 40);   // the examples are there (52 at the time of writing)

    std::vector<Result> results(scenes.size());
    for (std::size_t i = 0; i < scenes.size(); ++i) RunScene(copy, scenes[i], results[i]);

    int failedScenes = 0, skipped = 0;
    for (const Result& r : results) {
        if (r.skipped) { ++skipped; continue; }
        CHECK(r.fails.empty());
        if (!r.fails.empty()) {
            ++failedScenes;
            std::fprintf(stderr, "  FAIL examples/%s\n", r.rel.c_str());
            for (const std::string& f : r.fails) std::fprintf(stderr, "      %s\n", f.c_str());
        }
    }
    // a skip that names a scene that is not there is a stale skip
    for (const Skip& s : kSkips) {
        const bool there = fs::is_regular_file(copy / s.path, ec);
        CHECK(there);
        if (!there) std::fprintf(stderr, "  kSkips names %s, which is not in the examples folder: remove it\n", s.path);
    }
    std::printf("\n%-52s %-8s %-8s %s\n", "example scene", "objects", "warnings", "result");
    for (const Result& r : results)
        std::printf("%-52s %-8zu %d/%d      %s\n", r.rel.c_str(), r.objects, r.problemWarnings, r.playWarnings,
                    r.skipped ? ("SKIPPED: " + r.skipWhy).c_str() : r.fails.empty() ? "PASS" : "FAIL");
    std::printf("\n%zu scenes, %d failed, %d skipped (warnings: Problems/Console)\n", results.size(), failedScenes, skipped);
    if (!kSkips.empty()) {
        std::printf("\nSkipped on purpose:\n");
        for (const Skip& s : kSkips) std::printf("  %s: %s\n", s.path, s.why);
    }

    // ---- File > Examples > UI Gallery (and the phone layout) ----
    {
        LogCatch logs;
        const fs::path root = sandbox.Dir() / "gallery_project";
        okayproj::Made made; std::string err;
        CHECK(okayproj::MakeSkeleton(root, made, err));
        for (bool portrait : {false, true}) {
            EditorState ed;
            ed.setProjectDir(root.string());
            std::string where, berr;
            CHECK(oked::BuildUIGalleryExample(ed, portrait, &where, &berr));
            CHECK(!where.empty() && fs::is_regular_file(where, ec));
            CHECK(!ed.HasUnsavedChanges());
            const std::string text = SceneSerializer::Serialize(ed.scene());
            Scene s2("round"); std::vector<std::string> warn;
            CHECK(SceneSerializer::Deserialize(s2, text, nullptr, &warn) && warn.empty());
            CHECK(SceneSerializer::Serialize(s2) == text);   // what is built is what a save gives back
            ProjectDiagOptions opt; opt.assetRoot = root.string();
            for (const Diagnostic& d : CollectProjectDiagnostics(ed.scene(), opt).items)
                if (d.severity == DiagSeverity::Error) { CHECK(false); std::fprintf(stderr, "  UI Gallery Problems error: [%s] %s\n", d.code.c_str(), d.message.c_str()); }
            const auto bad = PlayFrames(ed, 60, logs);
            for (const std::string& b : bad) std::fprintf(stderr, "  UI Gallery%s: %s\n", portrait ? " (phone)" : "", b.c_str());
            CHECK(bad.empty());
            EditorState again;   // and it reopens
            CHECK(again.Load(where, &berr));
        }
    }

    // ---- File > Examples > Visual Script Node Playground ----
    {
        int n = 0;
        const okay::editor::vsed::VsPlaygroundDemo* demos = okay::editor::vsed::VsPlaygroundDemos(&n);
        CHECK(n >= 30);   // 39 at the time of writing
        std::set<std::string> ids;
        const fs::path dir = copy / "visual_script" / "playground";
        for (int i = 0; i < n; ++i) {
            const auto& d = demos[i];
            CHECK(d.id && d.id[0] && d.title && d.title[0] && d.blurb && d.blurb[0]);
            CHECK(ids.insert(d.id).second);   // no id twice
            const bool sceneThere = fs::is_regular_file(dir / d.scene, ec), graphThere = fs::is_regular_file(dir / d.graph, ec);
            CHECK(sceneThere && graphThere);
            if (!sceneThere) std::fprintf(stderr, "  demo '%s' names scene %s, which is not in examples/visual_script/playground\n", d.id, d.scene);
            if (!graphThere) std::fprintf(stderr, "  demo '%s' names graph %s, which is not in examples/visual_script/playground\n", d.id, d.graph);
            if (!sceneThere) continue;
            Scene s("demo");
            CHECK(SceneSerializer::LoadFromFile(s, (dir / d.scene).string()));
            CHECK(s.Find(d.focus) != nullptr);   // the object the menu selects so Play shows the graph's pins
            if (!s.Find(d.focus)) std::fprintf(stderr, "  demo '%s' selects '%s', which is not in %s\n", d.id, d.focus, d.scene);
            bool hostsGraph = false;
            for (const auto& up : s.Objects())
                for (auto* vc : up->GetComponents<VisualScriptComponent>())
                    if (fs::path(vc->Path()).filename().string() == d.graph) hostsGraph = true;
            CHECK(hostsGraph);   // some object in the scene runs the graph the editor opens
            if (!hostsGraph) std::fprintf(stderr, "  demo '%s': no object in %s runs %s\n", d.id, d.scene, d.graph);
        }
        // Every playground scene is offered by some demo (a scene nobody can open from the menu is wasted), except the
        // ones named here, which are test fixtures.
        const std::set<std::string> fixtures = {"coverage.okayscene"};   // test_vs_script_coverage: every node type on one graph
        std::set<std::string> offered;
        for (int i = 0; i < n; ++i) offered.insert(demos[i].scene);
        for (const auto& e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".okayscene" && !offered.count(e.path().filename().string()) && !fixtures.count(e.path().filename().string())) {
                CHECK(false);
                std::fprintf(stderr, "  %s is in examples/visual_script/playground but no File > Examples entry opens it\n", e.path().filename().string().c_str());
            }
    }

    // ---- File > Examples > Node Playground (flow and variables): found from the install folder, as the menu does ----
    {
        const std::vector<std::string> found = okay::editor::NodePlaygroundScenes();   // looks at examples/node_playground from the working folder
        std::set<std::string> onDisk;
        for (const auto& e : fs::directory_iterator(copy / "node_playground", ec))
            if (e.path().extension() == ".okayscene") onDisk.insert(e.path().filename().string());
        CHECK(!onDisk.empty());
        // The working folder is the sandbox, and the copy is its examples/ folder: the menu finds node_playground there
        // the way it finds it next to the program in an install folder.
        std::set<std::string> listed;
        for (const std::string& p : found) listed.insert(fs::path(p).filename().string());
        CHECK(listed == onDisk);
        for (const std::string& p : found) { EditorState ed; std::string err; CHECK(ed.Load(p, &err)); }
    }

    // ---- a scene opens in the view it is made for (it used to keep the editor's view, so 3D scenes opened flattened
    // into the 2D view and the physics playground's depth-separated labels were drawn on top of each other) ----
    {
        EditorState ed; std::string err;
        const std::string physics = (copy / "physics" / "physics_playground.okayscene").string();
        const std::string flat = (copy / "ui_text" / "ui_text.okayscene").string();
        CHECK(ed.Load(physics, &err) && ed.view3D);                                  // perspective camera: the 3D view
        CHECK(EditorState::SceneViewFor(ed.scene()) == 1);
        CHECK(ed.Load(flat, &err) && !ed.view3D);                                    // orthographic camera: the 2D view
        CHECK(EditorState::SceneViewFor(ed.scene()) == 0);
        CHECK(ed.Load(physics, &err) && ed.view3D);                                  // and back
        int labels = 0;                                                              // its station labels are 3D text, drawn in the
        for (const auto& up : ed.scene().Objects()) {                                // 3D view and the game (world text only shows in 2D)
            if (up->name.size() != 7 || up->name.substr(1) != " Label") continue;
            const auto* tr = up->GetComponent<TextRenderer>();
            CHECK(tr && tr->screenSpace && up->GetComponent<WorldSpaceUI>() != nullptr);
            ++labels;
        }
        CHECK(labels == 9);
        Scene empty;
        CHECK(EditorState::SceneViewFor(empty) == -1);                               // no camera: the view is kept
    }

    CHECK(sandbox.Verify());   // nothing was written into the source tree
    TEST_MAIN_RESULT();
}
