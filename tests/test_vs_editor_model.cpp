// The Visual Script editor's graph model (no drawing): what it saves must load
// back in the editor and in the runtime unchanged, quotes, '#' and all; paste
// never invents wires; and layout coordinates / ids can't hit undefined casts.
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include "VisualScriptEditor.hpp"
#include "ActionExport.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Core/Random.hpp"
#include <Okay.hpp>
#include <climits>
#include <set>
#include <cmath>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor::vsed;

namespace {
struct VsValueStr { std::string s; };
// Run `text` on an object and return a variable it set in On Start.
VsValueStr RunVar(const std::string& text, const char* var, std::string* err = nullptr);
} // namespace

int main() {
    RUN_SUITE("vs_editor_model");
    okaytest::Sandbox sandbox("vs_editor_model", OKAY_SOURCE_DIR);   // files the nodes write land in a temp folder, not the checkout
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- Format "#{0}" with 7 in it gives "#7" (the '#' isn't a comment) ----
    {
        VsEditGraph g;
        const int start = g.add("OnStart", 0, 0), set = g.add("SetVar", 0, 0), fmt = g.add("Format", 0, 0), k = g.add("Const", 0, 0);
        g.find(set)->arg = "s"; g.find(fmt)->arg = "#{0}"; g.find(k)->arg = "7";
        g.linkExec(start, 0, set); g.linkData(set, 0, fmt, 0); g.linkData(fmt, 0, k, 0);
        const std::string text = g.toText();
        std::string err;
        CHECK(RunVar(text, "s", &err).s == "#7");                 // was "7": the arg was cut at '#'
        VsEditGraph back; back.fromText(text);
        CHECK(back.find(fmt) && back.find(fmt)->arg == "#{0}");
    }

    // ---- A Const with quotes inside keeps them ----
    {
        VsEditGraph g;
        const int start = g.add("OnStart", 0, 0), set = g.add("SetVar", 0, 0), k = g.add("Const", 0, 0);
        g.find(set)->arg = "c"; g.find(k)->arg = "say \"hi\" now";
        g.linkExec(start, 0, set); g.linkData(set, 0, k, 0);
        const std::string text = g.toText();
        CHECK(RunVar(text, "c").s == "say \"hi\" now");           // was say 'hi' now
        VsEditGraph back; back.fromText(text);
        CHECK(back.find(k) && back.find(k)->arg == "say \"hi\" now");
        // A Const typed as a "quoted" string still runs as that string.
        g.find(k)->arg = "\"hello\"";
        CHECK(RunVar(g.toText(), "c").s == "hello");
        g.find(k)->arg = "5";
        CHECK(g.toText().find("Const 5\n") != std::string::npos); // numbers stay bare
    }

    // ---- Event names with '#' or '"' survive the editor and register in the runtime ----
    {
        const char* types[] = {"OnEvent", "OnKey", "OnTimer", "OnAnimEvent", "OnVarChanged", "OnSceneVarChanged", "OnGlobalVarChanged"};
        const char* names[] = {"Hit#2", "a\"b"};
        for (const char* t : types)
            for (const char* nm : names) {
                VsEditGraph g;
                const int ev = g.add(t, 0, 0);
                g.find(ev)->arg = nm;
                const std::string text = g.toText();
                VsEditGraph back; back.fromText(text);
                CHECK(back.find(ev) && back.find(ev)->arg == nm);
                std::string err;
                auto graph = vs::NodeGraph::Parse(text, &err);
                CHECK(graph != nullptr);
                const std::string want = VsEntryName(t, nm);
                CHECK(graph && graph->AllEntries().count(want) == 1);
                if (!graph || !graph->AllEntries().count(want))
                    std::printf("  %s '%s': entry '%s' missing (%s)\n", t, nm, want.c_str(), err.c_str());
            }
    }

    // ---- Paste drops wires to nodes that weren't pasted ----
    {
        VsEditGraph g;
        g.add("OnStart", 0, 0);
        std::set<int> made = VsPasteText(g, "node 1 Print\nexec 99 0 1\ndata 1 0 77 0\n", 10, 10);
        CHECK(made.size() == 1);
        auto exists = [&](int id) { return g.find(id) != nullptr; };
        for (const auto& l : g.execLinks) CHECK(exists(l.from) && exists(l.to));
        for (const auto& l : g.dataLinks) CHECK(exists(l.consumer) && exists(l.producer));
        CHECK(g.execLinks.empty() && g.dataLinks.empty());       // both pointed outside the paste (were remapped to node 0)
        std::string err;
        CHECK(vs::NodeGraph::Parse(g.toText(), &err) != nullptr);
    }

    // ---- Layout coordinates and ids that don't fit an int ----
    {
        VsEditGraph g;
        const int a = g.add("Print", 0, 0);
        g.find(a)->x = std::nanf(""); g.find(a)->y = 1e30f;
        VsComment c; c.x = -1e30f; c.y = INFINITY; c.w = 3e9f; c.h = 10; g.comments.push_back(c);
        const std::string text = g.toText();                       // was undefined float -> int casts
        CHECK(text.find("pos " + std::to_string(a) + " 0 1000000000") != std::string::npos);
        CHECK(text.find("comment -1000000000 1000000000 1000000000 10") != std::string::npos);
        VsEditGraph big;
        big.fromText("node 2147483647 Print\nnode -4 Print\nnode 3 Print\n");
        CHECK(big.nodes.size() == 1);                               // INT_MAX would overflow the next id
        CHECK(big.nextId == 4);
        CHECK(big.add("Print", 0, 0) == 4);
    }

    // ---- Older files (no escapes) still load the same ----
    {
        const std::string old =
            "node 1 OnStart\nnode 2 SetVar t\nnode 3 Const \"it's a b\"\n"
            "data 2 0 3 0\nexec 1 0 2\nentry OnStart 1\n"
            "comment 0 0 10 10 0 \"C:\\saves\\\"\n";               // a backslash before the closing quote
        CHECK(RunVar(old, "t").s == "it's a b");
        VsEditGraph g; g.fromText(old);
        CHECK(g.comments.size() == 1 && g.comments[0].text == "C:\\saves\\");
        CHECK(g.find(3) && g.find(3)->arg == "it's a b");
        // The old editor wrote args bare whenever they had no space, so a lone
        // quote mid-arg is real old content: it stays exactly as written.
        const std::string lone =
            "node 1 OnStart\nnode 2 SetVar t\nnode 3 Format x\"y\nnode 4 SetVar u\nnode 5 Const ab\"c\n"
            "data 2 0 3 0\ndata 4 0 5 0\nexec 1 0 2\nexec 2 0 4\nentry OnStart 1\n";
        CHECK(RunVar(lone, "t").s == "x\"y");                     // was x"y" (a quote added at the end)
        CHECK(RunVar(lone, "u").s == "ab\"c");
        VsEditGraph lg; lg.fromText(lone);
        CHECK(lg.find(3) && lg.find(3)->arg == "x\"y");
        CHECK(lg.find(5) && lg.find(5)->arg == "ab\"c");
        std::vector<std::string> t = vs::TokenizeLine("node 3 Format x\"y");
        CHECK(t.size() == 4 && t[3] == "x\"y");
    }

    // ---- Loaded layout values are checked and clamped like saved ones ----------
    {
        VsEditGraph g;
        g.fromText("node 1 Print\nnode 2 Print\nnode 3 Print\n"
                   "pos 1 1e40 nan\npos 2 -1e30 7\npos 3 inf 12.5\n"
                   "comment 1e30 nan 3e9 -inf 0 \"c\"\n");
        auto ok = [](float v) { return std::isfinite(v) && std::fabs(v) <= 1.0e9f; };
        for (const auto& n : g.nodes) CHECK(ok(n.x) && ok(n.y));
        CHECK(g.find(1) && g.find(1)->x == 1.0e9f && g.find(1)->y == 0.0f);
        CHECK(g.find(2) && g.find(2)->x == -1.0e9f && g.find(2)->y == 7.0f);
        CHECK(g.find(3) && g.find(3)->x == 1.0e9f && g.find(3)->y == 12.5f);
        CHECK(g.comments.size() == 1);
        if (!g.comments.empty()) {
            const VsComment& c = g.comments[0];
            CHECK(ok(c.x) && ok(c.y) && ok(c.w) && ok(c.h));
        }
        VsEditGraph junk; junk.fromText("node 1 Print\npos 1 abc\n");   // unreadable: stays where it was
        CHECK(junk.find(1) && junk.find(1)->x == 0.0f && junk.find(1)->y == 0.0f);
    }

    // ---- A node skipped on load takes its wires with it ----------------------------
    {
        const std::string text =
            "node 1 OnStart\nnode 2147483647 Print\nnode -4 Print\nnode 3 Print\n"
            "exec 1 0 2147483647\nexec 1 0 -4\nexec 1 0 3\ndata 3 0 2147483647 0\ndata 3 0 -4 0\n"
            "exec 1 0\ndata 3\nentry OnStart 1\n";
        VsEditGraph g; g.fromText(text);
        CHECK(g.execLinks.size() == 1 && g.dataLinks.empty());
        for (const auto& l : g.execLinks) CHECK(g.find(l.from) && g.find(l.to));
        std::string err;
        CHECK(vs::NodeGraph::Parse(g.toText(), &err) != nullptr);   // what it saves loads again
        if (!err.empty()) std::printf("  resave: %s\n", err.c_str());
    }

    // ---- Graph -> OkayScript: odd text in any node's field still gives code that loads ----
    // For every node type, each awkward value in its text field (a path ending in a
    // backslash, a quote, a space, a '#', a '%'): the generated script must load in
    // the OkayScript VM, or say up front which nodes it couldn't translate.
    {
        const char* odd[] = {"C:\\saves\\", "a\"b", "a b", "#{0}", "{0}%"};
        int bad = 0, total = 0;
        for (const auto& d : Registry()) {
            const std::string type = d.type;
            for (const char* arg : odd) {
                VsEditGraph g;
                const bool isEvent = !d.execIn && type.rfind("On", 0) == 0;
                const int start = isEvent ? -1 : g.add("OnStart", 0, 0);
                const int n = g.add(type, 0, 0);
                g.find(n)->arg = d.param ? arg : "";
                if (d.param == 0 && !isEvent) continue;   // nothing typed into it: nothing to escape
                for (std::size_t i = 0; i < d.dataIn.size(); ++i) g.find(n)->pinDefault[(int)i] = arg;
                if (isEvent) {
                    const int p = g.add("Print", 0, 0);
                    g.find(p)->pinDefault[0] = arg;
                    g.linkExec(n, 0, p);
                } else if (d.execIn) {
                    g.linkExec(start, 0, n);
                } else if (!d.dataOut.empty()) {
                    const int set = g.add("SetVar", 0, 0);
                    g.find(set)->arg = arg;                 // a variable name that isn't an identifier
                    g.linkExec(start, 0, set);
                    g.linkData(set, 0, n, 0);
                } else continue;
                ++total;
                const std::string code = CompileToOkayScript(g);
                std::string err;
                auto vm = CreateScriptVM("okayscript");
                const bool loads = vm && vm->Load(code, &err);
                if (!loads && code.find("NOT TRANSLATED") == std::string::npos) {
                    ++bad;
                    if (bad <= 15) std::printf("  %s [%s]: %s\n", type.c_str(), arg, err.c_str());
                }
            }
        }
        CHECK(total > 100);
        CHECK(bad == 0);
        // The starter graph (what a new visual script begins as) still compiles to loading code.
        VsEditGraph starter; SeedStarterGraph(starter);
        std::string err;
        auto vm = CreateScriptVM("okayscript");
        CHECK(vm && vm->Load(CompileToOkayScript(starter), &err));
        if (bad) std::printf("  %d of %d generated scripts don't load\n", bad, total);
    }

    // A guid reference and a prefab reference survive the editor's save format.
    {
        VsEditGraph g;
        const int find = g.add("FindObject", 0, 0);
        g.find(find)->arg = "@guid:abc123";
        const int spawn = g.add("Spawn", 40, 0);
        g.find(spawn)->arg = "@prefab:Assets/shot.okayprefab";
        g.find(spawn)->pinDefault[1] = "@guid:abc123";
        const std::string text = g.toText();
        VsEditGraph back;
        back.fromText(text);
        CHECK(back.find(find) && back.find(find)->arg == "@guid:abc123");
        CHECK(back.find(spawn) && back.find(spawn)->arg == "@prefab:Assets/shot.okayprefab");
        CHECK(back.find(spawn)->pinDefault[1] == "@guid:abc123");
        auto runtime = vs::NodeGraph::Parse(text, nullptr);
        CHECK(runtime != nullptr);
    }
    CHECK(VsSamePath("Assets/move.okayvs", "Assets/move.okayvs"));
    CHECK(VsSamePath("/tmp/proj/Assets/move.okayvs", "Assets/move.okayvs"));
    CHECK(!VsSamePath("/tmp/proj/Assets/move.okayvs", "Assets/other.okayvs"));
    {
        Scene scene("refs");
        GameObject* cube = scene.CreateGameObject("Cube");
        VsEditGraph g;
        const int mv = g.add("MoveTowards", 0, 0);
        g.find(mv)->arg = "Cube";
        const int sp = g.add("Spawn", 0, 0);
        g.find(sp)->arg = "marker.okayprefab";
        CHECK(VsUpgradeGraph(g, scene));
        CHECK(g.find(mv)->arg == std::string("@guid:") + cube->Guid());
        CHECK(g.find(sp)->arg == "@prefab:marker.okayprefab");
        CHECK(!VsUpgradeGraph(g, scene));   // already references
    }
    // A new graph is its own tab. Opening a file focuses the tab that already has it.
    // Dirty tracks the saved text, so undo back to that text is clean again.
    {
        VsEditState a;
        a.g.add("OnStart", 0, 0);
        VsMarkClean(a);
        CHECK(!a.g.dirty);
        a.g.add("Print", 10, 10);
        VsRefreshDirty(a);
        CHECK(a.g.dirty);
        const std::string edited = a.g.toText();
        a.g.fromText(a.cleanText);
        VsRefreshDirty(a);
        CHECK(!a.g.dirty);
        a.g.fromText(edited);
        VsRefreshDirty(a);
        CHECK(a.g.dirty);
        // Two tabs: creating another does not replace the first.
        Session().tabs.clear();
        NewGraph(true);
        const int first = Session().active;
        const int firstNodes = (int)State().g.nodes.size();
        NewGraph(false);
        CHECK((int)Session().tabs.size() == 2);
        CHECK(Session().active != first);
        CHECK(Session().tabs[(std::size_t)first]->g.nodes.size() == (std::size_t)firstNodes);
        // A missing file is an error tab, not a crash, and the other tab stays.
        OpenFile("/tmp/okay_no_such_graph_e755.okayvs");
        CHECK(State().missing);
        CHECK((int)Session().tabs.size() == 3);
        OpenFile("/tmp/okay_no_such_graph_e755.okayvs");
        CHECK((int)Session().tabs.size() == 3);
        std::vector<int> hits;
        CHECK(VsFuzzyScore("brnach", "Branch", "flow if", hits) >= 180);
        CHECK(VsFuzzyScore("raycst", "Raycast 3D", "trace hit", hits) >= 30);
        CHECK(VsEditDistance("brnach", "branch") <= 1);
        Session().tabs.clear();
    }

    // ---- The tween, spring and ease nodes of the fifth pass: in the node search, matching the engine's pin table, ----
    // ---- and turned into script calls (or reported as not translated) ----------------------------------------------
    {
        const char* types[] = {"EaseCurve", "SpringStep", "TweenMove", "TweenMoveSpeed", "TweenKillTag", "TweenPauseTag", "TweenPlayTag"};
        for (const char* t : types) {
            const VsDef* def = nullptr;
            for (const auto& d : Registry()) if (std::string(d.type) == t) def = &d;
            CHECK(def != nullptr);
            if (!def) continue;
            const okay::vs::NodeSignature* sig = okay::vs::SignatureFor(t);
            CHECK(sig != nullptr);
            if (sig) CHECK(sig->execIn == def->execIn && sig->ins.size() == def->dataIn.size() && sig->outs.size() == def->dataOut.size());
            CHECK(std::string(def->title).size() > 3);
        }
        // Tween Move's last input is the Target the engine's table asks for.
        for (const auto& d : Registry())
            if (std::string(d.type) == "TweenMove") CHECK(d.dataIn.size() == 6 && std::string(d.dataIn.back()) == "Target");
        // Every node has a help text (the node palette's tooltip).
        for (const char* t : types) CHECK(VsNodeHelp(t) && *VsNodeHelp(t));

        // Tween Move: the three axes, the seconds and the ease become one tween_move3 call; a tag wraps it; an axis nobody feeds
        // stays where it is.
        {
            VsEditGraph g;
            const int start = g.add("OnStart", 0, 0), mv = g.add("TweenMove", 0, 0);
            g.find(mv)->arg = "menu";
            g.find(mv)->pinDefault[0] = "3"; g.find(mv)->pinDefault[3] = "2"; g.find(mv)->pinDefault[4] = "OutBack";
            g.linkExec(start, 0, mv);
            const std::string code = CompileToOkayScript(g);
            CHECK(code.find("tween_tag(tween_move3(3, transform.localPosition.y, transform.localPosition.z, 2, \"OutBack\"), \"menu\")") != std::string::npos);
            auto vm = CreateScriptVM("okayscript");
            std::string err;
            CHECK(vm && vm->Load(code, &err));
        }
        // By speed: the call is wrapped in tween_speed_based, the default speed is 5, and no tag means no tween_tag.
        {
            VsEditGraph g;
            const int start = g.add("OnStart", 0, 0), mv = g.add("TweenMoveSpeed", 0, 0);
            g.find(mv)->pinDefault[1] = "7";
            g.linkExec(start, 0, mv);
            const std::string code = CompileToOkayScript(g);
            CHECK(code.find("tween_speed_based(tween_move3(transform.localPosition.x, 7, transform.localPosition.z, 5))") != std::string::npos);
            CHECK(code.find("tween_tag(") == std::string::npos);
            auto vm = CreateScriptVM("okayscript");
            std::string err;
            CHECK(vm && vm->Load(code, &err));
        }
        // Kill / Pause / Play with a tag: a call that keeps its count in a field the Count output reads.
        {
            VsEditGraph g;
            const int start = g.add("OnStart", 0, 0), kill = g.add("TweenKillTag", 0, 0), pause = g.add("TweenPauseTag", 0, 0),
                      play = g.add("TweenPlayTag", 0, 0), set = g.add("SetVar", 0, 0);
            g.find(kill)->arg = "ui"; g.find(pause)->arg = "ui"; g.find(play)->arg = "fx"; g.find(set)->arg = "n";
            g.find(kill)->pinDefault[0] = "true";
            g.linkExec(start, 0, kill); g.linkExec(kill, 0, pause); g.linkExec(pause, 0, play); g.linkExec(play, 0, set);
            g.linkData(set, 0, kill, 0);   // n = the kill node's Count
            const std::string code = CompileToOkayScript(g);
            CHECK(code.find("tween_kill_tag(\"ui\", true)") != std::string::npos);
            CHECK(code.find("tween_pause_tag(\"ui\")") != std::string::npos);
            CHECK(code.find("tween_play_tag(\"fx\")") != std::string::npos);
            CHECK(code.find("n = _tw_" + std::to_string(kill)) != std::string::npos);
            CHECK(code.find("int _tw_" + std::to_string(kill) + " = 0;") != std::string::npos);
            CHECK(code.find("NOT TRANSLATED") == std::string::npos);
            auto vm = CreateScriptVM("okayscript");
            std::string err;
            CHECK(vm && vm->Load(code, &err));
        }
        // Ease With Curve: ease_curve(t, "<text>"), the same value as the node (both ends take the same text).
        {
            VsEditGraph g;
            const int start = g.add("OnStart", 0, 0), set = g.add("SetVar", 0, 0), ease = g.add("EaseCurve", 0, 0), k = g.add("Const", 0, 0);
            g.find(set)->arg = "v"; g.find(ease)->arg = "cubic-bezier(0.4, 0, 0.2, 1)"; g.find(k)->arg = "0.5";
            g.linkExec(start, 0, set); g.linkData(set, 0, ease, 0); g.linkData(ease, 0, k, 0);
            const std::string code = CompileToOkayScript(g);
            CHECK(code.find("ease_curve(0.5, \"cubic-bezier(0.4, 0, 0.2, 1)\")") != std::string::npos);
            auto vm = CreateScriptVM("okayscript");
            std::string err;
            CHECK(vm && vm->Load(code, &err));
            CHECK(std::atof(RunVar(g.toText(), "v").s.c_str()) > 0.5);          // cubic-bezier(0.4,0,0.2,1) is above the diagonal at 0.5
        }
        // Spring Step has no text-script equivalent: the generated code says so.
        {
            VsEditGraph g;
            const int start = g.add("OnUpdate", 0, 0), sp = g.add("SpringStep", 0, 0);
            g.linkExec(start, 0, sp);
            const std::string code = CompileToOkayScript(g);
            CHECK(code.find("NOT TRANSLATED") != std::string::npos && code.find("SpringStep #" + std::to_string(sp)) != std::string::npos);
        }
    }

    // ---- Problems list: jumping to the node a problem names ----
    {
        VsEditState st;
        const int a = st.g.add("OnStart", 0, 0), br = st.g.add("Branch", 300, 0), p1 = st.g.add("Print", 600, 0), p2 = st.g.add("Print", 900, 0);
        st.g.linkExec(a, 0, br);
        st.issues = okay::vs::ValidateGraphText(st.g.toText());
        CHECK(!st.issues.empty());
        // A row's node is selected and the view is asked to centre on it.
        CHECK(VsJumpToNode(st, br));
        CHECK(st.selected == br && st.sel == std::set<int>({br}) && st.focusNode == br);
        CHECK(!VsJumpToNode(st, 4242) && st.selected == br);       // a node that is not there changes nothing
        // A pin problem also points at the pin.
        CHECK(VsJumpToNode(st, p1, 0, 0) && st.flashNode == p1 && st.flashPin == 0 && st.flashT > 0.0f);
        CHECK(VsJumpToNode(st, p2) && st.flashNode == -1);
        // F8 walks the nodes that have a problem, Shift+F8 walks back, both wrap.
        st.selected = -1; st.sel.clear();
        const std::vector<int> bad = okay::vs::ProblemNodes(st.issues, VsNodeOrder(st.g), st.issueFilter);
        CHECK(bad.size() >= 2);
        CHECK(VsJumpToNextProblem(st, 1) == bad.front() && st.selected == bad.front() && st.sel.size() == 1);
        CHECK(VsJumpToNextProblem(st, 1) == bad[1]);
        CHECK(VsJumpToNextProblem(st, -1) == bad.front());
        CHECK(VsJumpToNextProblem(st, -1) == bad.back());
        // Filtering a severity out takes its nodes out of the walk.
        st.issueFilter.warnings = false; st.issueFilter.errors = false; st.issueFilter.hints = false;
        CHECK(VsJumpToNextProblem(st, 1) == -1);
        st.issueFilter = {};
        // A row that covers several nodes: each click goes to the next node.
        VsEditState s2;
        const int b1 = s2.g.add("Branch", 0, 0), b2 = s2.g.add("Branch", 300, 0), b3 = s2.g.add("Branch", 600, 0);
        s2.issues = okay::vs::ValidateGraphText(s2.g.toText());
        const auto rows = okay::vs::GroupProblems(s2.issues, VsNodeOrder(s2.g));
        const okay::vs::VsProblemGroup* req = nullptr;
        for (const auto& r : rows) if (r.rule == "required") req = &r;
        CHECK(req && req->count == 3 && req->nodes == std::vector<int>({b1, b2, b3}));
        if (req) {
            CHECK(VsJumpToGroup(s2, *req) == b1 && s2.selected == b1);
            CHECK(VsJumpToGroup(s2, *req) == b2 && VsJumpToGroup(s2, *req) == b3 && VsJumpToGroup(s2, *req) == b1);
        }
        // After a fix the list is empty again (the checker is re-run on the new text).
        s2.g.find(b1)->pinDefault[0] = "true"; s2.g.find(b2)->pinDefault[0] = "true"; s2.g.find(b3)->pinDefault[0] = "true";
        bool stillRequired = false;
        for (const auto& is : okay::vs::ValidateGraphText(s2.g.toText())) if (is.message.rfind("Required: condition", 0) == 0) stillRequired = true;
        CHECK(!stillRequired);
    }

    // ---- An exec output takes any number of wires; they run in the order made ----
    {
        VsEditGraph g;
        const int start = g.add("OnStart", 0, 0), a = g.add("SetVar", 0, 0), b = g.add("SetVar", 0, 0), c = g.add("SetVar", 0, 0);
        for (int id : {a, b, c}) g.find(id)->arg = "s";
        const int fa = g.add("Format", 0, 0), fb = g.add("Format", 0, 0), fc = g.add("Format", 0, 0), gv = g.add("GetVar", 0, 0);
        g.find(fa)->arg = "{0}A"; g.find(fb)->arg = "{0}B"; g.find(fc)->arg = "{0}C"; g.find(gv)->arg = "s";
        g.linkData(a, 0, fa, 0); g.linkData(b, 0, fb, 0); g.linkData(c, 0, fc, 0);
        for (int f : {fa, fb, fc}) g.linkData(f, 0, gv, 0);   // a data output feeds many inputs
        g.linkExec(start, 0, a); g.linkExec(start, 0, b); g.linkExec(start, 0, c);
        g.linkExec(start, 0, b);                               // the same wire twice is one wire
        CHECK(g.execLinks.size() == 3);                        // the 2nd and 3rd were added, not replacing the 1st
        CHECK((VsExecTargets(g, start, 0) == std::vector<int>{a, b, c}));
        CHECK(g.dataLinks.size() == 6);
        const std::string text = g.toText();
        VsEditGraph back; back.fromText(text);
        CHECK((VsExecTargets(back, start, 0) == std::vector<int>{a, b, c}));   // order survives save and load
        CHECK(back.toText() == text);
        // Runs all three, in the order the wires were made, each finishing first.
        CHECK(RunVar(text, "s").s == "0ABC");
        // Reversing the wire order reverses the run order.
        VsEditGraph rev; rev.fromText(text);
        rev.execLinks.clear(); rev.linkExec(start, 0, c); rev.linkExec(start, 0, b); rev.linkExec(start, 0, a);
        CHECK(RunVar(rev.toText(), "s").s == "0CBA");
        // Breaking one wire keeps the others (and their order).
        CHECK(back.unlinkExec(start, 0, b) && !back.unlinkExec(start, 0, b));
        CHECK((VsExecTargets(back, start, 0) == std::vector<int>{a, c}));
        CHECK(RunVar(back.toText(), "s").s == "0AC");
        // The checker raises no error and no "nothing runs this node" for any of them.
        for (const auto& i : okay::vs::ValidateGraphText(text)) CHECK(i.severity != 0 && i.message.find("nothing runs") == std::string::npos);
        // A graph with one wire per pin saves exactly as before.
        VsEditGraph one; const int s1 = one.add("OnStart", 0, 0), p1 = one.add("Print", 0, 0);
        one.linkExec(s1, 0, p1);
        VsEditGraph one2; one2.fromText(one.toText());
        CHECK(one2.toText() == one.toText());
    }

    // ---- A step that can't run does not skip the other wires on its pin ----------
    {
        const std::string text =
            "node 1 OnStart\nentry OnStart 1\n"
            "node 2 SetVar s\ndefault 2 0 \"a\"\n"
            "node 3 Translate\ndefault 3 3 \"nobody_here\"\n"   // its Target object does not exist: reported and skipped
            "node 4 SetVar t\ndefault 4 0 \"c\"\n"
            "exec 1 0 2\nexec 1 0 3\nexec 1 0 4\n";
        CHECK(RunVar(text, "s").s == "a");
        CHECK(RunVar(text, "t").s == "c");
    }

    // ---- A key / mouse input value can be logged; a Key node has no flow pins ----
    {
        VsEditGraph g;
        const int print = g.add("Print", 0, 0);
        const char* inputs[] = {"Key", "KeyDown", "KeyUp", "MouseButton", "MouseButtonDown", "MouseX", "MouseY", "MouseWheel", "MouseGround"};
        for (const char* t : inputs) {
            const int k = g.add(t, 0, 0);
            const VsDef* d = FindDef(t);
            CHECK(d && !d->execIn && d->execOut.empty());                     // value nodes: wired by their data pins
            for (int o = 0; o < (int)d->dataOut.size(); ++o) {
                std::string why;
                CHECK(VsWireOk(g, print, 0, k, o, &why));                     // any output prints (Bool becomes true / false)
                g.linkData(print, 0, k, o);
            }
        }
    }

    // ---- The exported script for Print: text, value, or both ----------------------------------
    {
        for (int mode = 0; mode < 3; ++mode) {
            VsEditGraph g;
            const int st = g.add("OnStart", 0, 0), pr = g.add("Print", 0, 0), k = g.add("Key", 0, 0);
            g.find(k)->arg = "space";
            if (mode != 1) g.find(pr)->pinDefault[0] = "msg";
            if (mode != 0) g.linkData(pr, 1, k, 0);
            g.linkExec(st, 0, pr);
            const std::string code = CompileToOkayScript(g);
            std::string err;
            auto vm = CreateScriptVM("okayscript");
            CHECK(vm && vm->Load(code, &err));
            CHECK(mode != 2 || code.find("\": \"") != std::string::npos);
        }
    }

    // ---- Declared variables: the editor model declares, types, edits, renames and saves them ----
    {
        VsEditGraph g;
        std::string err;
        CHECK(g.declare("health", "Number", "object", &err));
        CHECK(g.declare("path", "List<Vector3>", "object", &err));
        CHECK(g.declare("tint", "Color", "scene", &err));
        CHECK(!g.declare("health", "Number", "object", &err) && err.find("already") != std::string::npos);
        CHECK(g.declare("health", "Number", "global", &err));              // another scope may reuse the name
        CHECK(!g.declare("9bad", "Number", "object", &err));
        CHECK(!g.declare("x", "Banana", "object", &err));
        CHECK(!g.declare("x", "Number", "planet", &err));
        CHECK(g.declareStruct("Enemy", &err) && g.addStructField(0, "hp", "Number", &err) && g.addStructField(0, "pos", "Vector3", &err));
        CHECK(!g.addStructField(0, "hp", "Number", &err));                  // listed twice
        CHECK(!g.addStructField(0, "me", "Enemy", &err));                   // a struct can't hold itself
        CHECK(g.declare("boss", "Enemy", "object", &err));
        CHECK(g.setVarDefault(0, "80", &err));
        CHECK(!g.setVarDefault(0, "(1, 2, 3)", &err));                      // a vector isn't a number: refused, default kept
        CHECK(g.vars[0].value == "80");
        CHECK(g.setVarDefault(1, "[(0, 0, 0), (1, 1, 1)]", &err));
        CHECK(!g.setVarDefault(1, "[\"x\"]", &err) && err.find("item 0") != std::string::npos);
        CHECK(g.declError().empty());
        // the file reads back the same, in the editor and in the runtime
        const std::string text = g.toText();
        VsEditGraph back; back.fromText(text);
        CHECK(back.vars.size() == g.vars.size() && back.structs.size() == 1 && back.structs[0].fields.size() == 2);
        CHECK(back.vars[0].value == "80" && back.vars[1].type == "List<Vector3>" && back.vars[2].scope == "scene");
        CHECK(back.toText() == text);
        std::string perr;
        auto parsed = vs::NodeGraph::Parse(text, &perr);
        CHECK(parsed && parsed->Decls().size() == 5);
        // converting a type keeps what fits
        CHECK(g.setVarType(0, "Text", &err) && g.vars[0].value == "\"80\"");
        CHECK(g.setVarType(0, "Number", &err) && g.vars[0].value == "80");
        CHECK(g.setVarType(0, "Vector3", &err) && g.vars[0].value == "(80, 80, 80)");
        // a rename follows the nodes that use the variable
        const int get = g.add("GetVar", 0, 0), sget = g.add("GetSceneVar", 0, 0);
        g.find(get)->arg = "path"; g.find(sget)->arg = "path";
        CHECK(g.renameVar(1, "route", &err));
        CHECK(g.find(get)->arg == "route" && g.find(sget)->arg == "path");  // only the object-scope node moved
        CHECK(!g.renameVar(1, "health", &err));                              // taken (same scope)
        // the pins take the declared types, so a bad wire is refused when dropped
        VsEditGraph w;
        w.declare("pos", "Vector3", "object"); w.declare("n", "Number", "object");
        const int gp = w.add("GetVar", 0, 0), gn = w.add("GetVar", 0, 0), sum = w.add("Add", 0, 0), setn = w.add("SetVar", 0, 0);
        w.find(gp)->arg = "pos"; w.find(gn)->arg = "n"; w.find(setn)->arg = "n";
        CHECK(VsOutPinType(w, gp, 0) == vs::PinType::Vec3 && VsOutPinType(w, gn, 0) == vs::PinType::Number);
        CHECK(!VsWireOk(w, sum, 0, gp, 0));                                  // Vector3 -> Number input
        CHECK(VsWireOk(w, sum, 0, gn, 0));
        CHECK(!VsWireOk(w, setn, 0, gp, 0));                                 // Set Var n takes a Number
        CHECK(VsInPinType(w, setn, 0) == vs::PinType::Number);
        // editing a default in graph text changes just that line; an old var line becomes a typed one
        std::string out;
        const std::string src = "decl a Number object \"1\"\nvar b 2\nnode 1 OnStart\n# keep me\n";
        CHECK(VsSetDeclDefaultInText(src, "a", vs::VarScope::Object, "9", "Number", out) && out == "decl a Number object \"9\"\nvar b 2\nnode 1 OnStart\n# keep me\n");
        CHECK(VsSetDeclDefaultInText(src, "b", vs::VarScope::Object, "5", "Number", out) && out.find("decl b Number object \"5\"") != std::string::npos);
        CHECK(!VsSetDeclDefaultInText(src, "zzz", vs::VarScope::Object, "5", "Number", out));
        CHECK(!VsSetDeclDefaultInText(src, "a", vs::VarScope::Scene, "5", "Number", out));   // other scope: not that line
    }
    {   // the declarations the editor wrote drive a real run
        VsEditGraph g; std::string err;
        g.declare("pts", "List<Vector3>", "object");
        g.setVarDefault(0, "[(1, 0, 0), (0, 2, 0)]");
        const int start = g.add("OnStart", 0, 0), get = g.add("GetVar", 0, 0), add = g.add("ListAdd", 0, 0), mk = g.add("MakeVec3", 0, 0);
        g.find(get)->arg = "pts";
        g.linkExec(start, 0, add); g.linkData(add, 0, get, 0); g.linkData(add, 1, mk, 0);
        CHECK(RunVar(g.toText(), "pts").s == "[(1, 0, 0), (0, 2, 0), (0, 0, 0)]");
        for (const auto& i : vs::ValidateGraphText(g.toText())) CHECK(i.severity != 0);   // no errors
    }

    // export_wait_delay_matches_runtime: Wait and Delay export as after(seconds, "fn").
    // The variable after the node stays unset until the delay has passed, in the
    // exported script and in the graph. A comment (the old export) would set it in Start.
    {
        for (const char* kind : {"Wait", "Delay"}) {
            VsEditGraph g;
            const int start = g.add("OnStart", 0, 0);
            const int early = g.add("SetVar", 0, 0);
            const int pause = g.add(kind, 0, 0);
            const int hit = g.add("SetVar", 0, 0);
            const int oneA = g.add("Const", 0, 0);
            const int oneB = g.add("Const", 0, 0);
            g.find(early)->arg = "early";
            g.find(hit)->arg = "hit";
            g.find(pause)->arg = "0.5";
            g.find(oneA)->arg = "1";
            g.find(oneB)->arg = "1";
            g.linkExec(start, 0, early);
            g.linkExec(early, 0, pause);
            g.linkExec(pause, 0, hit);
            g.linkData(early, 0, oneA, 0);
            g.linkData(hit, 0, oneB, 0);

            const std::string code = CompileToOkayScript(g);
            const auto afterAt = code.find("after(0.5,");
            const auto earlyAt = code.find("early = 1");
            const auto hitAt = code.find("hit = 1");
            CHECK(afterAt != std::string::npos);
            CHECK(code.find("// Wait") == std::string::npos);
            CHECK(code.find("// Delay") == std::string::npos);
            CHECK(code.find("NOT TRANSLATED") == std::string::npos);
            CHECK(earlyAt != std::string::npos && hitAt != std::string::npos && afterAt != std::string::npos);
            CHECK(earlyAt < afterAt && afterAt < hitAt);

            std::string err;
            Scene graphScene(std::string("graph-") + kind);
            auto* vs = graphScene.CreateGameObject("G")->AddComponent<VisualScriptComponent>();
            CHECK(vs->LoadFromText(g.toText(), &err));
            if (!err.empty()) std::printf("  graph %s: %s\n", kind, err.c_str());

            Scene scriptScene(std::string("script-") + kind);
            auto* sc = scriptScene.CreateGameObject("S")->AddComponent<ScriptComponent>("okayscript");
            const bool loaded = sc->LoadSource(code, &err);
            if (!loaded) std::printf("  export %s failed: %s\n%s\n", kind, err.c_str(), code.c_str());
            CHECK(loaded);
            if (!loaded || !sc->VM()) continue;

            graphScene.Start();
            scriptScene.Start();
            auto gEarly = [&] { return vs->GetVariable("early").AsFloat(); };
            auto gHit = [&] { return vs->GetVariable("hit").AsFloat(); };
            auto sEarly = [&] { return sc->VM()->GetGlobal("early").AsFloat(); };
            auto sHit = [&] { return sc->VM()->GetGlobal("hit").AsFloat(); };
            auto dump = [&](const char* when) {
                std::printf("  %s %s graph early=%.3f hit=%.3f script early=%.3f hit=%.3f\n%s\n",
                            kind, when, gEarly(), gHit(), sEarly(), sHit(), code.c_str());
            };
            if (std::fabs(gEarly() - 1.f) > 0.001f || std::fabs(sEarly() - 1.f) > 0.001f ||
                std::fabs(gHit()) > 0.001f || std::fabs(sHit()) > 0.001f) dump("at start");
            CHECK_NEAR(gEarly(), 1.0f, 0.001f);
            CHECK_NEAR(sEarly(), 1.0f, 0.001f);
            CHECK_NEAR(gHit(), 0.0f, 0.001f);
            CHECK_NEAR(sHit(), 0.0f, 0.001f);   // a comment export would have set hit already

            graphScene.Update(0.2f);
            scriptScene.Update(0.2f);
            if (std::fabs(gHit()) > 0.001f || std::fabs(sHit()) > 0.001f) dump("at 0.2s");
            CHECK_NEAR(gHit(), 0.0f, 0.001f);
            CHECK_NEAR(sHit(), 0.0f, 0.001f);

            graphScene.Update(0.4f);
            scriptScene.Update(0.4f);
            if (std::fabs(gHit() - 1.f) > 0.001f || std::fabs(sHit() - 1.f) > 0.001f) dump("at 0.6s");
            CHECK_NEAR(gHit(), 1.0f, 0.001f);
            CHECK_NEAR(sHit(), 1.0f, 0.001f);
        }
    }

    // export_timer_matches_runtime: Timer passes at most once per interval and keeps
    // the leftover, including the four-interval clamp. Zero and negative pass every time.
    {
        auto run = [](const char* seconds, const std::vector<float>& dts) {
            VsEditGraph g;
            const int upd = g.add("OnUpdate", 0, 0);
            const int timer = g.add("Timer", 0, 0);
            const int add = g.add("AddVar", 0, 0);
            const int one = g.add("Const", 0, 0);
            g.find(timer)->arg = seconds;
            g.find(add)->arg = "pulses";
            g.find(one)->arg = "1";
            g.linkExec(upd, 0, timer);
            g.linkExec(timer, 0, add);
            g.linkData(add, 0, one, 0);

            std::vector<okay::vs::VsIssue> blocked;
            const std::string code = CompileToOkayScript(g, &blocked);
            CHECK(blocked.empty());
            CHECK(code.find("unsupported node") == std::string::npos);
            CHECK(code.find("// Every") == std::string::npos);
            const bool gated = std::strtod(seconds, nullptr) > 0.0;
            if (gated) {
                CHECK(code.find("_vs_timer_") != std::string::npos);
                CHECK(code.find("Time.deltaTime") != std::string::npos);
                CHECK(code.find("* 4") != std::string::npos);
            } else {
                CHECK(code.find("if (_vs_timer_") == std::string::npos);
            }

            std::string err;
            Scene graphScene("timer-graph");
            auto* vs = graphScene.CreateGameObject("G")->AddComponent<VisualScriptComponent>();
            CHECK(vs->LoadFromText(g.toText(), &err));
            Scene scriptScene("timer-script");
            auto* sc = scriptScene.CreateGameObject("S")->AddComponent<ScriptComponent>("okayscript");
            const bool loaded = sc->LoadSource(code, &err);
            if (!loaded) std::printf("  timer %s failed: %s\n%s\n", seconds, err.c_str(), code.c_str());
            CHECK(loaded);
            graphScene.Start();
            scriptScene.Start();
            auto gp = [&] { return vs->GetVariable("pulses").AsFloat(); };
            auto sp = [&] { return sc->VM() ? sc->VM()->GetGlobal("pulses").AsFloat() : -1.f; };
            CHECK_NEAR(gp(), 0.f, 0.001f);
            CHECK_NEAR(sp(), 0.f, 0.001f);
            for (float dt : dts) {
                graphScene.Update(dt);
                scriptScene.Update(dt);
                if (std::fabs(gp() - sp()) > 0.001f)
                    std::printf("  timer %s after %.3f: graph %.3f script %.3f\n%s\n", seconds, dt, gp(), sp(), code.c_str());
                CHECK_NEAR(gp(), sp(), 0.001f);
            }
            return std::make_pair(gp(), sp());
        };
        {
            auto end = run("0.5", {0.2f, 0.2f});
            CHECK_NEAR(end.first, 0.f, 0.001f);   // 0.4s accumulated, still short of 0.5
            end = run("0.5", {0.2f, 0.2f, 0.2f});
            CHECK_NEAR(end.first, 1.f, 0.001f);   // 0.6s fires once, leftover 0.1
            end = run("0.5", {0.2f, 0.2f, 0.2f, 0.4f});
            CHECK_NEAR(end.first, 2.f, 0.001f);   // leftover 0.1 + 0.4 fires again
        }
        {
            auto end = run("0.5", {10.f});
            CHECK_NEAR(end.first, 1.f, 0.001f);   // one pass, leftover clamped to 2s
            end = run("0.5", {10.f, 0.1f});
            CHECK_NEAR(end.first, 2.f, 0.001f);   // 2 + 0.1 fires again
        }
        CHECK_NEAR(run("0", {0.2f, 0.2f, 0.2f}).first, 3.f, 0.001f);
        CHECK_NEAR(run("-2", {0.2f, 0.2f}).first, 2.f, 0.001f);
        CHECK_NEAR(run("nan", {0.2f}).first, 1.f, 0.001f);
    }

    // A node with no script fails the export by name. It is not left as a comment.
    {
        VsEditGraph g;
        const int start = g.add("OnStart", 0, 0);
        const int bad = g.add("NoSuchExport", 0, 0);
        const int set = g.add("SetVar", 0, 0);
        g.find(set)->arg = "hit";
        g.linkExec(start, 0, bad);
        g.linkExec(bad, 0, set);
        std::vector<okay::vs::VsIssue> blocked;
        const std::string code = CompileToOkayScript(g, &blocked);
        CHECK(code.find("unsupported node") == std::string::npos);
        CHECK(blocked.size() == 1);
        CHECK(blocked[0].node == bad);
        CHECK(blocked[0].severity == 0);
        CHECK(blocked[0].message.find("NoSuchExport") != std::string::npos);
        CHECK(blocked[0].message.find("#" + std::to_string(bad)) != std::string::npos);
    }

    // export_actions_matches_runtime: every Actions step is in the exported script and
    // the script's list does the same thing as the Actions component for the same inputs.
    {
        auto ops = [] {
            std::ifstream in(std::string(OKAY_SOURCE_DIR) + "/editor/main.cpp");
            std::string line;
            bool on = false;
            std::vector<std::string> out;
            while (std::getline(in, line)) {
                if (line.find("kInstrOps[]") != std::string::npos) { on = true; continue; }
                if (!on) continue;
                if (line.find("};") != std::string::npos) break;
                if (line.find("{\"") == std::string::npos) continue;
                const auto a = line.find('"'), b = line.find('"', a + 1);
                if (a != std::string::npos && b != std::string::npos) out.push_back(line.substr(a + 1, b - a - 1));
            }
            return out;
        }();
        CHECK(ops.size() > 200);

        auto argsFor = [](const std::string& op) {
            if (op == "load_scene" || op == "save_prefs" || op == "game_save" || op == "game_load" ||
                op == "game_continue" || op == "game_autosave" || op == "game_delete_slot")
                return std::vector<std::string>{"../outside"};
            if (op == "load_scene_index") return std::vector<std::string>{"99999"};
            if (op == "net_host" || op == "net_join" || op == "net_host_relay" || op == "net_join_relay")
                return std::vector<std::string>{"127.0.0.1", "1", "code"};
            if (op == "playfab_login" || op == "playfab_name" || op == "playfab_set_stat" ||
                op == "playfab_leaderboard" || op == "playfab_set_data" || op == "playfab_get_data")
                return std::vector<std::string>{};   // no title: the call fails before any network request
            return std::vector<std::string>{"a", "1", "2", "3", "4", "5"};
        };
        auto near = [](float a, float b) {
            if (a != a && b != b) return true;
            const float d = std::fabs(a - b);
            return d <= 0.002f || d <= 0.002f * std::max(std::fabs(a), std::fabs(b));
        };
        struct World {
            bool alive = false, active = false, pending = false, paused = false, quit = false;
            std::string name, tag;
            float px = 0, py = 0, pz = 0, sx = 1, sy = 1, sz = 1, rx = 0, ry = 0, rz = 0, scale = 1;
            int objects = 0;
            std::map<std::string, float> vars, prefs;
            std::map<std::string, std::string> strs;
            std::map<std::string, std::vector<float>> arrays;
            std::map<std::string, std::map<std::string, float>> maps;
        };
        auto reset = [] {
            ActionList::ResetVars();
            Prefs::Clear();
            Time::SetTimeScale(1.f);
            Game::SetPaused(false);
            if (Game::QuitRequested()) Game::CancelQuit();
        };
        auto snap = [&](Scene& sc, GameObject* go) {
            ActionList::UseScene(&sc);
            World w;
            w.objects = (int)sc.Objects().size();
            w.scale = Time::BaseTimeScale();
            w.paused = Game::Paused();
            w.quit = Game::QuitRequested();
            w.alive = go && !go->IsDestroyed();
            if (w.alive && go->transform) {
                w.active = go->active;
                w.name = go->name;
                w.tag = go->tag;
                w.pending = sc.IsDestroyPending(go);
                const Vec3 p = go->transform->localPosition, s = go->transform->localScale, e = go->transform->localRotation.ToEuler();
                w.px = p.x; w.py = p.y; w.pz = p.z; w.sx = s.x; w.sy = s.y; w.sz = s.z; w.rx = e.x; w.ry = e.y; w.rz = e.z;
            }
            for (const auto& kv : ActionList::Vars()) w.vars.emplace(kv.first, kv.second);
            for (const auto& kv : ActionList::StrVars()) w.strs.emplace(kv.first, kv.second);
            for (const auto& kv : ActionList::Arrays()) w.arrays.emplace(kv.first, kv.second);
            for (const auto& kv : ActionList::Maps()) {
                std::map<std::string, float> inner;
                for (const auto& e : kv.second) inner.emplace(e.first, e.second);
                w.maps.emplace(kv.first, std::move(inner));
            }
            for (const char* k : {"a", "b", "1", "hit", "early", "n", "x", "score"})
                if (Prefs::Has(k)) w.prefs.emplace(k, Prefs::GetFloat(k, 0.f));
            return w;
        };
        auto same = [&](const World& a, const World& b, const char* op) {
            auto bad = [&](const std::string& what) {
                std::printf("  actions %s: %s\n", op, what.c_str());
                return false;
            };
            if (a.alive != b.alive || a.active != b.active || a.pending != b.pending || a.objects != b.objects) return bad("object");
            if (a.name != b.name || a.tag != b.tag) return bad("name");
            if (!near(a.px, b.px) || !near(a.py, b.py) || !near(a.pz, b.pz)) return bad("position");
            if (!near(a.sx, b.sx) || !near(a.sy, b.sy) || !near(a.sz, b.sz)) return bad("scale");
            if (!near(a.rx, b.rx) || !near(a.ry, b.ry) || !near(a.rz, b.rz)) return bad("rotation");
            if (!near(a.scale, b.scale) || a.paused != b.paused || a.quit != b.quit) return bad("time");
            if (a.vars.size() != b.vars.size() || a.strs.size() != b.strs.size() || a.prefs.size() != b.prefs.size() ||
                a.arrays.size() != b.arrays.size() || a.maps.size() != b.maps.size()) return bad("count");
            for (const auto& kv : a.vars) {
                auto it = b.vars.find(kv.first);
                if (it == b.vars.end() || !near(kv.second, it->second)) return bad("var " + kv.first);
            }
            for (const auto& kv : a.strs) {
                auto it = b.strs.find(kv.first);
                if (it == b.strs.end() || kv.second != it->second) return bad("text " + kv.first);
            }
            for (const auto& kv : a.prefs) {
                auto it = b.prefs.find(kv.first);
                if (it == b.prefs.end() || !near(kv.second, it->second)) return bad("pref " + kv.first);
            }
            for (const auto& kv : a.arrays) {
                auto it = b.arrays.find(kv.first);
                if (it == b.arrays.end() || kv.second.size() != it->second.size()) return bad("array " + kv.first);
                for (std::size_t i = 0; i < kv.second.size(); ++i)
                    if (!near(kv.second[i], it->second[i])) return bad("array " + kv.first);
            }
            for (const auto& kv : a.maps) {
                auto it = b.maps.find(kv.first);
                if (it == b.maps.end() || kv.second.size() != it->second.size()) return bad("map " + kv.first);
                for (const auto& e : kv.second) {
                    auto jt = it->second.find(e.first);
                    if (jt == it->second.end() || !near(e.second, jt->second)) return bad("map " + kv.first);
                }
            }
            return true;
        };
        auto pump = [](Scene& sc, int n, float dt) {
            for (int i = 0; i < n; ++i) { Time::Step(dt); sc.Update(dt); }
        };
        auto findG = [](Scene& sc) -> GameObject* {
            for (const auto& up : sc.Objects()) if (up && up->name == "G" && !up->IsDestroyed()) return up.get();
            return nullptr;
        };
        auto runList = [&](const ActionList& src, int updates, float dt) {
            const std::string code = okay::editor::ActionListToCode(src);
            reset();
            Scene actions("actions");
            GameObject* ago = actions.CreateGameObject("G");
            ActionList* al = ago->AddComponent<ActionList>();
            al->FromText(src.ToText());
            actions.Start();
            Random::Shared().Seed(42);
            pump(actions, updates, dt);
            World aw = snap(actions, findG(actions));

            reset();
            Scene scripts("scripts");
            GameObject* sgo = scripts.CreateGameObject("G");
            auto* sc = sgo->AddComponent<ScriptComponent>("okayscript");
            std::string err;
            const bool loaded = sc->LoadSource(code, &err);
            if (!loaded) std::printf("  actions export failed: %s\n%s\n", err.c_str(), code.c_str());
            CHECK(loaded);
            scripts.Start();
            Random::Shared().Seed(42);
            pump(scripts, updates, dt);
            World sw = snap(scripts, findG(scripts));
            reset();
            return std::make_pair(aw, sw);
        };

        for (const std::string& op : ops) {
            ActionList src;
            src.trigger = ActionList::Trigger::OnStart;
            src.instructions.push_back({op, argsFor(op)});
            const std::string code = okay::editor::ActionListToCode(src);
            CHECK(code.find("// TODO") == std::string::npos);
            CHECK(code.find("actions_load(") != std::string::npos);
            const auto at = code.find(op);
            CHECK(at != std::string::npos);
            if (at != std::string::npos) {
                const bool left = at == 0 || (code[at - 1] != '_' && !std::isalnum(static_cast<unsigned char>(code[at - 1])));
                const auto end = at + op.size();
                const bool right = end >= code.size() || (code[end] != '_' && !std::isalnum(static_cast<unsigned char>(code[end])));
                CHECK(left && right);
            }
            auto both = runList(src, 4, 0.2f);
            CHECK(same(both.first, both.second, op.c_str()));
        }

        // Wait holds the following step. The list runs on the first Update, then the
        // wait counts on the updates after that.
        {
            ActionList src;
            src.trigger = ActionList::Trigger::OnStart;
            src.instructions.push_back({"set_var", {"early", "1"}});
            src.instructions.push_back({"wait", {"0.5"}});
            src.instructions.push_back({"set_var", {"hit", "1"}});
            const std::string code = okay::editor::ActionListToCode(src);
            CHECK(code.find("wait") != std::string::npos);
            CHECK(code.find("// TODO") == std::string::npos);
            reset();
            Scene actions("wait-a");
            auto* al = actions.CreateGameObject("G")->AddComponent<ActionList>();
            al->FromText(src.ToText());
            Scene scripts("wait-s");
            auto* sc = scripts.CreateGameObject("G")->AddComponent<ScriptComponent>("okayscript");
            std::string err;
            CHECK(sc->LoadSource(code, &err));
            actions.Start();
            scripts.Start();
            auto val = [&](Scene& scn, const char* name) {
                ActionList::UseScene(&scn);
                return ActionList::GetVar(name);
            };
            CHECK_NEAR(val(actions, "hit"), 0.f, 0.001f);
            CHECK_NEAR(val(scripts, "hit"), 0.f, 0.001f);
            Time::Step(0.2f); actions.Update(0.2f); Time::Step(0.2f); scripts.Update(0.2f);
            CHECK_NEAR(val(actions, "early"), 1.f, 0.001f);
            CHECK_NEAR(val(scripts, "early"), 1.f, 0.001f);
            CHECK_NEAR(val(actions, "hit"), 0.f, 0.001f);
            CHECK_NEAR(val(scripts, "hit"), 0.f, 0.001f);
            for (int i = 0; i < 2; ++i) { Time::Step(0.2f); actions.Update(0.2f); Time::Step(0.2f); scripts.Update(0.2f); }
            CHECK_NEAR(val(actions, "hit"), 0.f, 0.001f);   // 0.1s of the wait is still left
            CHECK_NEAR(val(scripts, "hit"), 0.f, 0.001f);
            Time::Step(0.2f); actions.Update(0.2f); Time::Step(0.2f); scripts.Update(0.2f);
            CHECK_NEAR(val(actions, "hit"), 1.f, 0.001f);
            CHECK_NEAR(val(scripts, "hit"), 1.f, 0.001f);
            reset();
        }
        // If, else, and repeat run the same steps on both sides.
        {
            ActionList yes;
            yes.instructions = {{"set_var", {"x", "1"}}, {"if", {"x", "gt", "0"}}, {"set_var", {"hit", "1"}},
                                {"else", {}}, {"set_var", {"hit", "2"}}, {"end_if", {}}};
            auto both = runList(yes, 1, 0.2f);
            CHECK(same(both.first, both.second, "if"));
            CHECK(both.first.vars.count("hit") && near(both.first.vars.at("hit"), 1.f));
            ActionList no = yes;
            no.instructions[0].args = {"x", "0"};
            both = runList(no, 1, 0.2f);
            CHECK(same(both.first, both.second, "else"));
            CHECK(both.first.vars.count("hit") && near(both.first.vars.at("hit"), 2.f));
            ActionList loop;
            loop.instructions = {{"set_var", {"n", "0"}}, {"repeat", {"3"}}, {"add_var", {"n", "1"}}, {"end_repeat", {}}};
            both = runList(loop, 1, 0.2f);
            CHECK(same(both.first, both.second, "repeat"));
            CHECK(both.first.vars.count("n") && near(both.first.vars.at("n"), 3.f));
        }
        // Every trigger emits a real load, not a TODO comment.
        {
            using T = ActionList::Trigger;
            const T triggers[] = {T::OnStart, T::OnUpdate, T::OnKey, T::OnCollision, T::OnClick, T::OnKeyUp, T::OnMessage,
                                  T::OnTriggerEnter, T::OnTriggerExit, T::OnMouseEnter, T::OnMouseExit, T::OnMouseDown,
                                  T::OnMouseUp, T::OnMouseOver, T::OnCollisionExit, T::OnCollisionStay, T::OnInterval,
                                  T::OnLateUpdate, T::OnTriggerStay};
            for (T t : triggers) {
                ActionList src;
                src.trigger = t;
                src.triggerKey = (t == T::OnInterval) ? "0.5" : (t == T::OnMessage) ? "Hit" : "e";
                src.instructions.push_back({"set_var", {"hit", "1"}});
                const std::string code = okay::editor::ActionListToCode(src);
                CHECK(code.find("// TODO") == std::string::npos);
                CHECK(code.find("actions_load(") != std::string::npos);
                CHECK(code.find("set_var") != std::string::npos);
            }
        }
    }

    Log::sink = sink;
    CHECK(sandbox.Verify());   // nothing was written into the source tree
    TEST_MAIN_RESULT();
}

namespace {
VsValueStr RunVar(const std::string& text, const char* var, std::string* err) {
    Scene sc("M");
    auto* v = sc.CreateGameObject("M")->AddComponent<VisualScriptComponent>();
    std::string e;
    const bool ok = v->LoadFromText(text, &e);
    if (err) *err = e;
    if (!ok) { std::printf("  load failed: %s\n", e.c_str()); return {}; }
    sc.Start();
    return {v->GetVariable(var).AsString()};
}
} // namespace
