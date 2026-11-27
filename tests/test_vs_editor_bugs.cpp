// Every example graph opens in the editor with no red problems: the checker
// (ValidateGraphText plus the script-export pass the editor runs) finds nothing
// that is an error in a graph we ship, and load -> save -> load gives the same text.
#include "test_framework.hpp"
#include "VisualScriptModel.hpp"
#include <Okay.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>

using namespace okay::editor::vsed;
namespace fs = std::filesystem;


// No wire may point at a node that is not there, and ids are unique.
static bool Consistent(const VsEditGraph& g) {
    std::set<int> ids;
    for (const auto& n : g.nodes) { if (!ids.insert(n.id).second) return false; if (n.id >= g.nextId) return false; }
    for (const auto& l : g.execLinks) if (!ids.count(l.from) || !ids.count(l.to)) return false;
    for (const auto& l : g.dataLinks) if (!ids.count(l.consumer) || !ids.count(l.producer)) return false;
    return true;
}

static void FuzzEdits() {
    std::mt19937 rng(1234);
    const char* types[] = {"OnStart", "OnUpdate", "Print", "Branch", "SetVar", "GetVar", "Add", "Const", "Wait", "Compare", "Sequence"};
    VsEditGraph g;
    std::vector<std::string> undo, redo;
    std::string clip;
    int bad = 0;
    for (int step = 0; step < 600; ++step) {
        const std::string before = g.toText();
        const int op = (int)(rng() % 8);
        auto pick = [&]() { return g.nodes.empty() ? -1 : g.nodes[rng() % g.nodes.size()].id; };
        bool edit = true;
        switch (op) {
        case 0: g.add(types[rng() % 11], (float)(rng() % 800), (float)(rng() % 600)); break;
        case 1: { int id = pick(); if (id >= 0) g.removeNode(id); break; }
        case 2: { int a = pick(), b = pick(); if (a >= 0 && b >= 0 && a != b) g.linkExec(a, (int)(rng() % 2), b); break; }
        case 3: { int a = pick(), b = pick(); if (a >= 0 && b >= 0 && a != b) g.linkData(a, (int)(rng() % 2), b, 0); break; }
        case 4: { std::set<int> sel; for (const auto& n : g.nodes) if (rng() % 2) sel.insert(n.id); clip = VsSelectionText(g, sel); edit = false; break; }
        case 5: if (!clip.empty() && g.nodes.size() < 120) VsPasteText(g, clip, (float)(rng() % 500), (float)(rng() % 500)); break;
        case 6: if (!undo.empty()) { redo.push_back(before); g.fromText(undo.back()); undo.pop_back(); } edit = false; break;
        case 7: if (!redo.empty()) { undo.push_back(before); g.fromText(redo.back()); redo.pop_back(); } edit = false; break;
        }
        if (edit && g.toText() != before) { undo.push_back(before); redo.clear(); }
        if (op == 6 && !redo.empty() && g.toText() == redo.back()) ++bad;   // undo must change something it recorded
        if (!Consistent(g)) { ++bad; std::printf("  fuzz step %d op %d: graph inconsistent\n", step, op); break; }
        VsEditGraph r; r.fromText(g.toText());
        if (r.toText() != g.toText()) { ++bad; std::printf("  fuzz step %d op %d: save/load changes the graph\n", step, op); break; }
    }
    CHECK(bad == 0);
}

// Copy carries the declared variables (and the record types they use) its nodes read, so a Get Var pasted into
// another graph still knows its variable.
static void CopyKeepsDeclaredVariables() {
    VsEditGraph src;
    src.fromText("struct Enemy hp:Number=100\ndecl boss Enemy object\ndecl total Number object 5\ndecl unused Text object \"x\"\n"
                 "node 1 GetVar total\nnode 2 GetVar boss\n");
    const std::string clip = VsSelectionText(src, {1, 2});
    VsEditGraph dst;
    VsPasteText(dst, clip, 0, 0);
    bool total = false, boss = false, unused = false;
    for (const auto& v : dst.vars) { total |= v.name == "total"; boss |= v.name == "boss"; unused |= v.name == "unused"; }
    CHECK(total);
    CHECK(boss);
    CHECK(!unused);
    CHECK(dst.structs.size() == 1);
    CHECK(dst.declError().empty());
}

int main() {
#ifdef OKAY_SOURCE_DIR
    fs::current_path(OKAY_SOURCE_DIR);
#endif
    RUN_SUITE("vs_editor_bugs");
    auto sink = okay::Log::sink;
    okay::Log::sink = [](okay::Log::Level, const std::string&) {};
    int graphs = 0;
    for (const auto& e : fs::recursive_directory_iterator("examples")) {
        if (!e.is_regular_file() || e.path().extension() != ".okayvs") continue;
        std::ifstream in(e.path());
        std::stringstream ss; ss << in.rdbuf();
        VsEditGraph g; g.fromText(ss.str());
        const std::string t1 = g.toText();
        VsEditGraph g2; g2.fromText(t1);
        const std::string t2 = g2.toText();
        CHECK(t1 == t2);
        if (t1 != t2) std::printf("  %s: save/load/save differs\n", e.path().string().c_str());
        ++graphs;
        std::vector<okay::vs::VsIssue> exp;
        const auto issues = VsCheckGraph(g, t1, e.path().parent_path().string(), &exp);
        int errs = 0;
        for (const auto& is : issues) {
            if (is.severity != 0) continue;
            ++errs;
            std::printf("  %s: %s\n", e.path().string().c_str(), is.message.c_str());
        }
        CHECK(errs == 0);
    }
    CHECK(graphs >= 30);
    std::printf("  examples checked: %d\n", graphs);
    std::fflush(stdout);
    FuzzEdits();
    CopyKeepsDeclaredVariables();
    okay::Log::sink = sink;
    TEST_MAIN_RESULT();
}
