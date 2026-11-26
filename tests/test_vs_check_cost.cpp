// The Visual Script panel's per-frame work (editor/VisualScriptEditor.hpp). The owner's okay_editor_perf.log (2026-11-25) shows
// UI.VisualScript at 263-352 ms in single frames, again and again. The graph checker (a full parse of the graph text, a look on disk
// for every prefab a node names, then the whole OkayScript export) ran whenever the graph's text changed, and dragging a node changes
// its position line in every frame of the drag. Here a 600-node graph is dragged for 120 frames: the checker must run once (for the
// first frame), not 120 times; editing a value still re-checks. Work counts are the bounds; the times are printed for the report.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include <Okay.hpp>

#include <chrono>
#include <cstdio>
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
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
