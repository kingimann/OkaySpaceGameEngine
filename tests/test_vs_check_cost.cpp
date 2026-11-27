// The Visual Script panel's per-frame work (the editor's Visual Script window, now the Visual Script app over editor/VisualScriptModel.hpp). The owner's okay_editor_perf.log (2026-11-25) shows
// UI.VisualScript at 263-352 ms in single frames, again and again. The graph checker (a full parse of the graph text, a look on disk
// for every prefab a node names, then the whole OkayScript export) ran whenever the graph's text changed, and dragging a node changes
// its position line in every frame of the drag. Here a 600-node graph is dragged for 120 frames: the checker must run once (for the
// first frame), not 120 times; editing a value still re-checks. A Play Clip / Load Scene / Play Music node's dropdown walked the whole
// Assets folder every frame; now one walk per 2 s. Work counts are the bounds; the times are printed for the report.
#include "test_framework.hpp"
#include "VisualScriptModel.hpp"
#include <Okay.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
using namespace okay::editor::vsed;

namespace {
double Ms(std::chrono::steady_clock::time_point a) { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count(); }

// 100 event chains of six nodes: On Update -> Branch -> Set Var <- Add <- (Get Var, Const), and a Spawn naming a prefab.
void Build(VsEditGraph& g) {
    for (int i = 0; i < 100; ++i) {
        const float x = (float)(i % 10) * 400.0f, y = (float)(i / 10) * 300.0f;
        const int ev = g.add("OnUpdate", x, y), br = g.add("Branch", x + 60, y), set = g.add("SetVar", x + 120, y);
        const int add = g.add("Add", x + 120, y + 80), get = g.add("GetVar", x + 60, y + 80), k = g.add("Const", x + 60, y + 140);
        g.find(set)->arg = "v" + std::to_string(i);
        g.find(get)->arg = "v" + std::to_string(i);
        g.find(k)->arg = std::to_string(i);
        g.linkExec(ev, 0, br);
        g.linkExec(br, 0, set);
        g.linkData(set, 0, add, 0);
        g.linkData(add, 0, get, 0);
        g.linkData(add, 1, k, 0);
    }
}
}  // namespace

int main() {
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};
    RUN_SUITE("dragging a node does not re-run the graph checker every frame");
    {
        VsEditState st;
        Build(st.g);
        CHECK(st.g.nodes.size() == 600);
        VsCheckRuns() = 0;
        CHECK(VsRefreshChecks(st, st.g, st.g.toText(), ""));
        const std::size_t issuesBefore = st.issues.size();
        const auto t0 = std::chrono::steady_clock::now();
        for (int frame = 0; frame < 120; ++frame) {   // the drag: one node moves a little in every frame
            st.g.nodes[7].x += 3.0f;
            st.g.nodes[7].y += 1.0f;
            VsRefreshChecks(st, st.g, st.g.toText(), "");
        }
        const double dragMs = Ms(t0);
        CHECK(VsCheckRuns() == 1);
        CHECK(st.issues.size() == issuesBefore);
        // What every drag frame cost before: the checker itself.
        const auto t1 = std::chrono::steady_clock::now();
        for (int frame = 0; frame < 5; ++frame) VsCheckGraph(st.g, st.g.toText(), "", &st.exportIssues);
        const double checkMs = Ms(t1) / 5.0;
        std::printf("  600 nodes: checker %.2f ms per run (was run in every drag frame); a drag frame now %.3f ms\n", checkMs, dragMs / 120.0);
        // A real change still re-checks: a value typed into a node, a new wire, a removed node.
        VsCheckRuns() = 0;
        st.g.find(st.g.nodes[5].id)->arg = "42";
        CHECK(VsRefreshChecks(st, st.g, st.g.toText(), ""));
        st.g.linkExec(st.g.nodes[0].id, 0, st.g.nodes[8].id);
        CHECK(VsRefreshChecks(st, st.g, st.g.toText(), ""));
        CHECK(!VsRefreshChecks(st, st.g, st.g.toText(), ""));
        CHECK(VsCheckRuns() == 2);
    }

    RUN_SUITE("the check key drops only position and comment-frame lines");
    {
        const std::string text = "# head\nnode 1 OnStart\npos 1 10 20\nnode 2 SetVar x\npos 2 30 40\nexec 1 0 2\ncomment 0 0 50 50 1 \"hi\"\n";
        CHECK(VsCheckKey(text) == "# head\nnode 1 OnStart\nnode 2 SetVar x\nexec 1 0 2\n");
        CHECK(VsCheckKey("node 1 OnStart") == "node 1 OnStart");   // no final newline
    }

    RUN_SUITE("FindDef: every registry type is found, first entry wins, unknown is null");
    {
        int found = 0;
        for (const VsDef& d : Registry()) {
            const VsDef* f = FindDef(d.type);
            if (f && f->type == d.type) ++found;
            CHECK(f == nullptr || f <= &d);   // the first entry of a type, as the old scan returned
        }
        CHECK(found == (int)Registry().size());
        CHECK(FindDef("NoSuchNodeType") == nullptr);
        const auto t0 = std::chrono::steady_clock::now();
        int hits = 0;
        for (int i = 0; i < 100000; ++i) hits += FindDef(Registry()[(std::size_t)(i % Registry().size())].type) ? 1 : 0;
        std::printf("  FindDef: %zu types, %.1f ns per lookup\n", Registry().size(), Ms(t0) * 1e6 / 100000.0);
        CHECK(hits == 100000);
    }
    RUN_SUITE("dropdown lists of project files: one Assets walk per 2 s, not one per node per frame");
    {
        namespace fs = std::filesystem;
        const fs::path assets = fs::temp_directory_path() / ("okay_vschoices_" + std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count())) / "Assets";
        fs::create_directories(assets / "Anim");
        for (int i = 0; i < 40; ++i) std::ofstream(assets / "Anim" / ("clip" + std::to_string(i) + (i % 4 ? ".okayanim" : ".png"))) << "x";
        VsChoiceScans() = 0;
        std::size_t n = 0;
        for (int frame = 0; frame < 60; ++frame)          // 60 frames at 60 FPS ...
            for (int node = 0; node < 20; ++node)         // ... of a graph with 20 Play Clip nodes
                n = VsAssetStems(assets, ".okayanim", 10.0 + frame / 60.0).size();
        CHECK(n == 30);
        CHECK(VsChoiceScans() == 1);                      // was 1200 walks of the folder
        std::ofstream(assets / "Anim" / "late.okayanim") << "x";
        CHECK(VsAssetStems(assets, ".okayanim", 12.5).size() == 31);   // a new file shows within 2 s
        CHECK(VsChoiceScans() == 2);
        std::error_code ec;
        fs::remove_all(assets.parent_path(), ec);
    }

    RUN_SUITE("VsFind: the id index follows added and removed nodes");
    {
        VsEditGraph g;
        Build(g);
        for (const auto& n : g.nodes) CHECK(VsFind(g, n.id) == &n);
        const int gone = g.nodes[10].id;
        g.nodes.erase(g.nodes.begin() + 10);
        CHECK(VsFind(g, gone) == nullptr);
        for (const auto& n : g.nodes) CHECK(VsFind(g, n.id) == &n);
        const int added = g.add("Branch", 0, 0);
        CHECK(VsFind(g, added) && VsFind(g, added)->type == "Branch");
        VsEditGraph other;
        const int o = other.add("OnStart", 0, 0);
        CHECK(VsFind(other, o) == &other.nodes[0]);       // another graph (another tab) is looked up in its own nodes
        CHECK(VsFind(g, added) && VsFind(g, added)->id == added);
    }
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
