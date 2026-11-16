// The Visual Script editor and graphs saved without `pos` lines (issue #888,
// findings 17 and 18): such nodes used to load at (0, 0) as one pile, clicks went
// to the bottom node of a pile, and the "why can't I drop here" text never showed
// during a wire drag. Checks the editor's own loader and its pure helpers.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace okay::editor::vsed;

namespace {
std::string ReadAll(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
// Ids that have a `pos` line in the file text.
std::set<int> PosIds(const std::string& text) {
    std::set<int> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line); std::string k; int id = 0;
        if ((ls >> k) && k == "pos" && (ls >> id)) out.insert(id);
    }
    return out;
}
VsNodeBox BoxOf(const VsEditGraph& g, const VsEditNode& n) {
    float w = 0, h = 0;
    VsEstimateNodeSize(g, n, w, h);
    return {n.id, n.x, n.y, n.x + w, n.y + h};
}
bool Overlap(const VsNodeBox& a, const VsNodeBox& b) {
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}
// Pairs (auto-placed node, any other node) whose boxes overlap. Sweep on x.
int AutoOverlaps(const VsEditGraph& g, const std::set<int>& filePos) {
    std::vector<VsNodeBox> boxes;
    for (const auto& n : g.nodes) boxes.push_back(BoxOf(g, n));
    std::sort(boxes.begin(), boxes.end(), [](const VsNodeBox& a, const VsNodeBox& b) { return a.x0 < b.x0; });
    int bad = 0;
    for (std::size_t i = 0; i < boxes.size(); ++i)
        for (std::size_t j = i + 1; j < boxes.size() && boxes[j].x0 < boxes[i].x1; ++j) {
            if (filePos.count(boxes[i].id) && filePos.count(boxes[j].id)) continue;   // the file's own layout
            if (Overlap(boxes[i], boxes[j])) ++bad;
        }
    return bad;
}
} // namespace

int main() {
    RUN_SUITE("vs_autolayout");
    const std::string dir = std::string(OKAY_SOURCE_DIR) + "/examples/visual_script";

    // ---- coverage.okayvs: 1451 nodes, no pos lines ----
    {
        const std::string path = dir + "/playground/coverage.okayvs";
        const std::string fileText = ReadAll(path);
        CHECK(PosIds(fileText).empty());
        VsEditGraph g;
        CHECK(g.load(path));
        CHECK(g.nodes.size() == 1451);
        std::set<std::pair<float, float>> distinct;
        for (const auto& n : g.nodes) distinct.insert({n.x, n.y});
        std::printf("  coverage: nodes=%zu distinct positions=%zu\n", g.nodes.size(), distinct.size());
        CHECK(distinct.size() == g.nodes.size());          // was 1: every node at (0, 0)
        const int ov = AutoOverlaps(g, {});
        std::printf("  coverage: overlapping node pairs=%d\n", ov);
        CHECK(ov == 0);
        CHECK(!g.dirty);                                    // a load is not an edit
        // The editor's own clean / dirty bookkeeping agrees.
        VsEditState st; st.g = g; VsMarkClean(st); VsRefreshDirty(st);
        CHECK(!st.g.dirty);
        // Same file, same layout.
        VsEditGraph again; again.load(path);
        CHECK(again.toText() == g.toText());
        // The file itself is untouched by loading.
        CHECK(ReadAll(path) == fileText);
        // The On Start chain reads left to right.
        const VsEditNode* start = g.find(1);
        const VsEditNode* next = nullptr;
        for (const auto& l : g.execLinks) if (l.from == 1) next = g.find(l.to);
        CHECK(start && next && next->x > start->x);
    }

    // ---- Saved positions stay exactly; new ones don't land on them ----
    {
        const std::string text =
            "node 1 OnStart\nentry OnStart 1\npos 1 40 60\n"
            "node 2 Print\nexec 1 0 2\n"
            "node 3 Print\npos 3 300 60\nexec 2 0 3\n"
            "node 4 Add\n"
            "node 5 Const 3\n"
            "node 6 Print\npos 6 -120 900\n"
            "data 4 0 5 0\n";
        VsEditGraph g; g.fromText(text);
        CHECK(!g.dirty);
        CHECK(g.find(1)->x == 40.0f && g.find(1)->y == 60.0f);
        CHECK(g.find(3)->x == 300.0f && g.find(3)->y == 60.0f);
        CHECK(g.find(6)->x == -120.0f && g.find(6)->y == 900.0f);
        CHECK(AutoOverlaps(g, PosIds(text)) == 0);
        std::set<std::pair<float, float>> d;
        for (const auto& n : g.nodes) d.insert({n.x, n.y});
        CHECK(d.size() == g.nodes.size());
        // Fully placed graphs are not moved at all.
        VsEditGraph h; h.fromText("node 1 OnStart\npos 1 0 0\nnode 2 Print\npos 2 0 0\n");
        CHECK(h.find(1)->x == 0.0f && h.find(2)->x == 0.0f && h.find(2)->y == 0.0f);
        // A save after an edit writes the new places; a load before it did not change the text on disk.
        VsEditGraph r; r.fromText(g.toText());
        for (const auto& n : g.nodes) CHECK(r.find(n.id) && r.find(n.id)->x == n.x && r.find(n.id)->y == n.y);
    }

    // ---- Every shipped graph: nothing auto-placed overlaps anything ----
    {
        int files = 0, autoNodes = 0, bad = 0;
        for (const auto& e : std::filesystem::recursive_directory_iterator(std::string(OKAY_SOURCE_DIR) + "/examples")) {
            if (e.path().extension() != ".okayvs") continue;
            const std::string text = ReadAll(e.path().string());
            VsEditGraph g; g.fromText(text);
            const std::set<int> pos = PosIds(text);
            for (const auto& n : g.nodes) if (!pos.count(n.id)) ++autoNodes;
            const int ov = AutoOverlaps(g, pos);
            if (ov) { ++bad; std::printf("  overlap in %s: %d\n", e.path().string().c_str(), ov); }
            ++files;
        }
        std::printf("  examples: %d graphs, %d nodes placed on load\n", files, autoNodes);
        CHECK(files > 10);
        CHECK(bad == 0);
    }

    // ---- The node drawn on top gets the click ----
    {
        // Draw order: first is at the bottom, last is on top.
        std::vector<VsNodeBox> order = {{7, 0, 0, 100, 100}, {8, 50, 50, 150, 150}, {9, 0, 0, 100, 100}};
        CHECK(VsTopNodeAt(order, 10, 10) == 9);      // was 7: the bottom node
        CHECK(VsTopNodeAt(order, 120, 120) == 8);
        CHECK(VsTopNodeAt(order, 500, 500) == -1);
        // Wire drop: two pins at the same spot, the one on the higher-drawn node wins.
        std::vector<VsSnapCand> c = {{100, ImVec2(10, 10), 5}, {200, ImVec2(10, 10), 9}, {300, ImVec2(60, 60), 99}};
        CHECK(VsPickSnap(c, ImVec2(11, 11), 30.0f) == 200);   // was 100: the lowest id
        CHECK(VsPickSnap(c, ImVec2(59, 59), 30.0f) == 300);
        CHECK(VsPickSnap(c, ImVec2(400, 400), 30.0f) == -1);
    }

    // ---- The "why" text shows while a wire is dragged over a blocked pin ----
    {
        const std::string why = "This wire carries flow (the triangle), not a value. Drop it on a white triangle.";
        CHECK(VsDragWhyAt(false, why, ImVec2(0, 0), ImVec2(32, 32), ImVec2(16, 16)) == why);   // was never shown
        CHECK(VsDragWhyAt(false, why, ImVec2(0, 0), ImVec2(32, 32), ImVec2(40, 16)).empty());   // mouse elsewhere
        CHECK(VsDragWhyAt(true, why, ImVec2(0, 0), ImVec2(32, 32), ImVec2(16, 16)).empty());    // the pin takes it
        CHECK(VsDragWhyAt(false, "", ImVec2(0, 0), ImVec2(32, 32), ImVec2(16, 16)).empty());
    }

    // ---- coverage's 1444 "nothing runs this node" warnings fold into one row ----
    {
        const std::string path = std::string(OKAY_SOURCE_DIR) + "/examples/visual_script/playground/coverage.okayvs";
        const auto issues = okay::vs::ValidateGraphText(ReadAll(path), std::string(OKAY_SOURCE_DIR) + "/examples/visual_script/playground");
        const std::size_t un = VsCountUnreached(issues);
        std::printf("  coverage: %zu checker issues, %zu \"nothing runs this node\"\n", issues.size(), un);
        CHECK(un == 1444);
        CHECK(un > kVsFoldUnreached);
    }

    TEST_MAIN_RESULT();
}
