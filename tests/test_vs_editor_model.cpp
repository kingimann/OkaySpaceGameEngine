// The Visual Script editor's graph model (no drawing): what it saves must load
// back in the editor and in the runtime unchanged, quotes, '#' and all; paste
// never invents wires; and layout coordinates / ids can't hit undefined casts.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include <Okay.hpp>
#include <climits>
#include <set>
#include <cmath>
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

    Log::sink = sink;
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
