// The Visual Script editor's IDE tools, at model level (no drawing):
//   1. pin-aware search: ranking by how well a node fits a dragged wire, and which plug the new node gets;
//   2. the debugger: watch values and the lit path while stepping a small graph inside a GameFiber;
//   3. collapse a selection to a function: the graph does the same thing before and after;
//   4. layout: distribute, centre, straighten, auto-arrange, wire knots kept across save;
//   5. find references across the graph files of a project, and bookmarks;
//   6. checker findings with one-click fixes that leave a valid graph;
//   7. a graph diff against an older version.
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include "VisualScriptModel.hpp"
#include "VsDebugWatch.hpp"
#include "VsCollapse.hpp"
#include "VsProjectRefs.hpp"
#include "VsGraphDiff.hpp"
#include "VsIdeProviders.hpp"
#include "GameFiber.hpp"
#include "okay/VisualScript/VsDebug.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor::vsed;
using okay::vs::PinType;

namespace {

int Add(VsEditGraph& g, const char* type, const char* arg = "", float x = 0, float y = 0) {
    const int id = g.add(type, x, y);
    if (VsEditNode* n = g.find(id)) n->arg = arg;
    return id;
}

// Run a graph's On Start on a fresh object and return the object variables it set (as text).
std::map<std::string, std::string> RunStart(const std::string& text, std::string* err = nullptr) {
    std::map<std::string, std::string> out;
    Scene sc("M");
    auto* v = sc.CreateGameObject("M")->AddComponent<VisualScriptComponent>();
    std::string e;
    if (!v->LoadFromText(text, &e)) { if (err) *err = e; std::printf("  load failed: %s\n", e.c_str()); return out; }
    sc.Start();
    for (const char* name : {"a", "b", "c", "r", "x", "y"}) out[name] = v->GetVariable(name).AsString();
    return out;
}

bool HasError(const std::vector<okay::vs::VsIssue>& is) {
    for (const auto& i : is) if (i.severity == 0) { std::printf("  error: %s (node %d)\n", i.message.c_str(), i.node); return true; }
    return false;
}

} // namespace

int main() {
    RUN_SUITE("vs_ide");
    okaytest::Sandbox sandbox("vs_ide", OKAY_SOURCE_DIR);
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- 1. Pin-aware node search ------------------------------------------------------------------------------
    {
        // Rank of one pin against the dragged type: exact, takes anything, converts, refuses.
        CHECK(VsPinFitRank(PinType::Number, PinType::Number, true) == 0);
        CHECK(VsPinFitRank(PinType::Number, PinType::Any, true) == 1);
        CHECK(VsPinFitRank(PinType::Number, PinType::Text, true) == 2);    // a number prints
        CHECK(VsPinFitRank(PinType::Number, PinType::Bool, true) == 2);
        CHECK(VsPinFitRank(PinType::Vec3, PinType::Number, true) == -1);
        CHECK(VsPinFitRank(PinType::Text, PinType::Number, false) == 2);   // dragged from a Text input: a Number output converts
        CHECK(VsPinFitRank(PinType::Object, PinType::Number, false) == -1);
        // A whole node: Substring (Text, Number, Number) has a Number input, so a dragged Number fits it exactly.
        const VsDef* sub = FindDef("Substring");
        const VsDef* print = FindDef("Print");
        const VsDef* translate = FindDef("Translate");
        CHECK(sub && print && translate);
        if (sub && print && translate) {
            CHECK(VsPinFitRank(*sub, false, true, PinType::Number) == 0);
            CHECK(VsPinFitRank(*print, false, true, PinType::Number) == 1);   // Print's Value takes anything
            CHECK(VsPinFitRank(*translate, false, true, PinType::Vec3) == -1);
            CHECK(VsPinFitRank(*translate, true, true) == 0);                  // a flow wire into a step
            CHECK(VsPinFitRank(*FindDef("Const"), true, true) == -1);          // a value block takes no flow
        }
        // The wire-drop list puts exact-type nodes first; a typed search lifts them too.
        CHECK(VsWireSearchBoost(0) > VsWireSearchBoost(1) && VsWireSearchBoost(1) > VsWireSearchBoost(2));
        const auto& fitting = VsNodesFittingPin(false, true, PinType::Number);
        CHECK(!fitting.empty());
        // Within one level, every exact-type node comes before every node that only converts.
        bool sawExact = false, orderOk = true;
        std::map<int, int> worstSeen;   // level -> worst rank listed so far
        for (const VsDef* d : fitting) {
            const int r = VsPinFitRank(*d, false, true, PinType::Number);
            CHECK(r >= 0);   // only nodes that fit are listed
            if (r == 0) sawExact = true;
            int& w = worstSeen[(int)d->level];
            if (r == 0 && w == 2) orderOk = false;
            w = std::max(w, r == 0 ? 0 : 2);
        }
        CHECK(sawExact);
        CHECK(orderOk);

        // Dragging a Number off a Const and picking Substring wires it into Start (the Number pin), not Text.
        VsEditGraph g;
        const int c = Add(g, "Const", "5", 0, 0);
        const int s = Add(g, "Substring", "", 300, 0);
        CHECK(VsPickWirePin(g, s, true, c, 0) == 1);
        CHECK(VsConnectPendingWire(g, s, false, true, c, 0));
        CHECK(g.dataLinks.size() == 1 && g.dataLinks[0].consumer == s && g.dataLinks[0].inPin == 1);
        // A second drag: pin 1 is taken now, so the other Number pin (End) gets it.
        const int c2 = Add(g, "Const", "2", 0, 100);
        CHECK(VsPickWirePin(g, s, true, c2, 0) == 2);
        // Dragging off Substring's Text input and picking Add: Add's Number output converts into it.
        const int a = Add(g, "Add", "", 0, 200);
        CHECK(VsPickWirePin(g, a, false, s, 0) == 0);
        CHECK(VsConnectPendingWire(g, a, false, false, s, 0));
        // Flow: dragging a "then" off On Start and picking Print joins them.
        const int st = Add(g, "OnStart", "", 0, 400);
        const int p = Add(g, "Print", "", 300, 400);
        CHECK(VsConnectPendingWire(g, p, true, true, st, 0));
        CHECK(VsExecTarget(g, st, 0) == p);
        // A value block can't take a flow wire.
        const int k = Add(g, "Const", "1", 0, 500);
        CHECK(!VsConnectPendingWire(g, k, true, true, st, 0));
    }

    // ---- 2. Debugger: step over / into / out in a GameFiber; watches, wire values, the lit path, the call stack ----
    {
        VsEditGraph eg;
        const int start = Add(eg, "OnStart");
        const int setX = Add(eg, "SetVar", "x", 300, 0);
        const int add = Add(eg, "Add", "", 100, 150);
        const int two = Add(eg, "Const", "2", 0, 150), three = Add(eg, "Const", "3", 0, 250);
        const int call = Add(eg, "CallFunction", "Double", 600, 0);
        const int getX = Add(eg, "GetVar", "x", 450, 150);
        const int setY = Add(eg, "SetVar", "y", 900, 0);
        const int fn = Add(eg, "FunctionStart", "Double(v: Number) -> r: Number", 0, 500);
        const int ret = Add(eg, "Return", "Double", 500, 500);
        const int mul = Add(eg, "Mul", "", 250, 600), k2 = Add(eg, "Const", "2", 0, 650);
        eg.linkExec(start, 0, setX); eg.linkExec(setX, 0, call); eg.linkExec(call, 0, setY); eg.linkExec(fn, 0, ret);
        eg.linkData(add, 0, two, 0); eg.linkData(add, 1, three, 0); eg.linkData(setX, 0, add, 0);
        eg.linkData(call, 0, getX, 0); eg.linkData(setY, 0, call, 0);
        eg.linkData(mul, 0, fn, 0); eg.linkData(mul, 1, k2, 0); eg.linkData(ret, 0, mul, 0);
        CHECK(!HasError(okay::vs::ValidateGraphText(eg.toText())));
        std::string err;
        auto graph = okay::vs::NodeGraph::Parse(eg.toText(), &err);
        CHECK(graph != nullptr);
        if (graph) {
            graph->debugKey = "dbg.okayvs";
            const bool tracingWas = okay::vs::NodeGraph::Tracing();
            okay::vs::NodeGraph::Tracing() = true;
            auto& dbg = okay::vs::VsDebugger::Get();
            dbg.ClearBreakpoints();
            dbg.SetBreakpoint("dbg.okayvs", setX, true);
            okay::editor::GameFiber fiber;
            struct Stop { int node, depth; std::vector<std::string> stack; std::string event; };
            std::vector<Stop> stops;
            dbg.onStop = [&](okay::vs::NodeGraph& gg, okay::vs::VsNode& n, okay::vs::VsContext&) {
                Stop s{n.fileId, gg.CallDepth(), {}, gg.CurrentEvent()};
                for (const auto& f : gg.DebugFrames()) s.stack.push_back(f.name);
                stops.push_back(s);
                fiber.YieldToEditor();   // the game freezes here, mid-frame, as in the editor
            };
            okay::vs::VsContext ctx;
            VsWatchList watches;
            CHECK(watches.toggle(add, 0));
            CHECK(watches.toggle(mul, 0));
            CHECK(!watches.toggle(mul, 0) && watches.items.size() == 1);   // toggling again removes it
            watches.toggle(mul, 0);
            std::map<int, unsigned> seen;

            fiber.Run([&] { graph->RunEntries("OnStart", ctx); });
            CHECK(fiber.Suspended());
            CHECK(stops.size() == 1 && stops.back().node == setX && stops.back().depth == 0);
            auto rows = VsWatchRows(eg, graph.get(), watches);
            CHECK(rows.size() == 2 && rows[0].value == "(not run yet)" && !rows[0].known);   // paused before Set x reads Add

            dbg.RequestStep(okay::vs::VsDebugger::Step::Over);
            fiber.Resume();
            CHECK(fiber.Suspended() && stops.size() == 2 && stops.back().node == call);
            rows = VsWatchRows(eg, graph.get(), watches);
            CHECK(rows[0].known && rows[0].value == "5");
            CHECK(rows[0].label.find("#" + std::to_string(add)) != std::string::npos);
            int setXWire = -1;
            for (int i = 0; i < (int)eg.dataLinks.size(); ++i) if (eg.dataLinks[(std::size_t)i].consumer == setX) setXWire = i;
            CHECK(VsWireValueText(eg, graph.get(), setXWire) == "5");   // hovering the wire into Set x shows what went through

            dbg.RequestStep(okay::vs::VsDebugger::Step::Into);
            fiber.Resume();
            CHECK(fiber.Suspended() && stops.size() == 3);
            if (stops.size() == 3) {
                CHECK(stops.back().depth == 1);
                CHECK(stops.back().stack.size() == 1 && stops.back().stack[0] == "Double");
            }
            const auto stack = VsCallStackLines(graph->CurrentEvent(), graph->DebugFrames());
            CHECK(stack.size() == 2 && stack[0] == "event OnStart" && stack[1] == "Double(5)");

            dbg.RequestStep(okay::vs::VsDebugger::Step::Out);
            fiber.Resume();
            CHECK(fiber.Suspended() && stops.size() == 4);
            if (stops.size() == 4) CHECK(stops.back().node == setY && stops.back().depth == 0);
            rows = VsWatchRows(eg, graph.get(), watches);
            CHECK(rows[1].known && rows[1].value == "10");

            dbg.RequestStep(okay::vs::VsDebugger::Step::None);
            fiber.Resume();
            CHECK(!fiber.Suspended());
            CHECK(ctx.variables.count("y") && ctx.variables["y"].AsFloat() == 10.0f);

            // The lit path: every step that ran this frame and the wires between them.
            const std::set<int> ran = VsRanSince(eg, graph.get(), seen);
            CHECK(ran.count(start) && ran.count(setX) && ran.count(call) && ran.count(setY));
            const VsLitWires lit = VsLitPath(eg, graph.get(), ran);
            for (std::size_t i = 0; i < eg.execLinks.size(); ++i)
                if (eg.execLinks[i].from == start) CHECK(lit.exec[i]);
            for (std::size_t i = 0; i < eg.dataLinks.size(); ++i)
                if (eg.dataLinks[i].consumer == add) CHECK(lit.data[i]);   // Const -> Add: read through Set x
            CHECK(VsRanSince(eg, graph.get(), seen).empty());   // nothing new ran since
            // Values read as the canvas shows them.
            CHECK(VsLiveValueText(okay::vs::VsValue(2.5f)) == "2.5");
            CHECK(VsLiveValueText(okay::vs::VsValue(std::string(40, 'a'))).size() < 40);
            dbg.onStop = nullptr;
            dbg.ClearBreakpoints();
            okay::vs::NodeGraph::Tracing() = tracingWas;
        }
    }

    // ---- 3. Collapse to function: the same results before and after ---------------------------------------------
    {
        VsEditGraph g;
        const int start = Add(g, "OnStart");
        const int setA = Add(g, "SetVar", "a", 300, 0);
        const int addN = Add(g, "Add", "", 150, 150);
        const int two = Add(g, "Const", "2", 0, 150);
        const int ten = Add(g, "Const", "10", 0, 250);            // stays outside: becomes a parameter
        const int setB = Add(g, "SetVar", "b", 600, 0);
        const int mul = Add(g, "Mul", "", 450, 150);
        const int getA = Add(g, "GetVar", "a", 300, 150);
        const int four = Add(g, "Const", "4", 300, 250);
        const int setC = Add(g, "SetVar", "c", 900, 0);            // outside: reads Mul through a return value
        const int addC = Add(g, "Add", "", 750, 150);
        const int one = Add(g, "Const", "1", 600, 250);
        g.linkExec(start, 0, setA); g.linkExec(setA, 0, setB); g.linkExec(setB, 0, setC);   // dup-allow: a test graph wired by hand, not the starter graph
        g.linkData(addN, 0, two, 0); g.linkData(addN, 1, ten, 0); g.linkData(setA, 0, addN, 0);
        g.linkData(mul, 0, getA, 0); g.linkData(mul, 1, four, 0); g.linkData(setB, 0, mul, 0);
        g.linkData(addC, 0, mul, 0); g.linkData(addC, 1, one, 0); g.linkData(setC, 0, addC, 0);
        const auto before = RunStart(g.toText());
        CHECK(before.at("a") == "12" && before.at("b") == "48" && before.at("c") == "49");

        // Refused selections leave the graph as it was.
        const std::string text0 = g.toText();
        VsCollapseResult bad = VsCollapseToFunction(g, {start, setA});
        CHECK(!bad.ok && !bad.why.empty() && g.toText() == text0);
        bad = VsCollapseToFunction(g, {addN, two});
        CHECK(!bad.ok && g.toText() == text0);   // values only: nothing for the flow to call
        CHECK(!VsCanCollapse(g, {}));

        const std::set<int> sel = {setA, addN, two, setB, mul, getA, four};
        std::string why;
        CHECK(VsCanCollapse(g, sel, &why));
        const VsCollapseResult r = VsCollapseToFunction(g, sel);
        CHECK(r.ok);
        if (!r.ok) std::printf("  collapse refused: %s\n", r.why.c_str());
        CHECK(r.name == "NewFunction" && r.params == 1 && r.returns == 1);
        const VsEditNode* fnNode = g.find(r.functionNode);
        CHECK(fnNode && fnNode->arg == "NewFunction(b: Number) -> out: Number");
        CHECK(VsExecTarget(g, start, 0) == r.callNode && VsExecTarget(g, r.callNode, 0) == setC);
        CHECK(!HasError(okay::vs::ValidateGraphText(g.toText())));
        const auto after = RunStart(g.toText());
        CHECK(after == before);
        CHECK(VsUniqueFunctionName(g) == "NewFunction2");

        // A Branch whose two ways both leave to the same place is one way out.
        VsEditGraph b;
        const int s2 = Add(b, "OnStart");
        const int br = Add(b, "Branch", "", 200, 0);
        if (VsEditNode* n = b.find(br)) n->pinDefault[0] = "true";
        const int x1 = Add(b, "SetVar", "x", 400, 0), x2 = Add(b, "SetVar", "x", 400, 150);
        if (VsEditNode* n = b.find(x1)) n->pinDefault[0] = "1";
        if (VsEditNode* n = b.find(x2)) n->pinDefault[0] = "2";
        const int z = Add(b, "SetVar", "y", 700, 0);
        if (VsEditNode* n = b.find(z)) n->pinDefault[0] = "7";
        b.linkExec(s2, 0, br); b.linkExec(br, 0, x1); b.linkExec(br, 1, x2); b.linkExec(x1, 0, z); b.linkExec(x2, 0, z);
        const auto bBefore = RunStart(b.toText());
        const VsCollapseResult rb = VsCollapseToFunction(b, {br, x1, x2}, "Pick");
        CHECK(rb.ok && rb.name == "Pick");
        CHECK(!HasError(okay::vs::ValidateGraphText(b.toText())));
        CHECK(RunStart(b.toText()) == bBefore);
        CHECK(bBefore.at("x") == "1" && bBefore.at("y") == "7");

        // A Sequence that leaves from one output and goes on inside from another would change the order: refused.
        VsEditGraph q;
        const int s3 = Add(q, "OnStart");
        const int seq = Add(q, "Sequence", "", 200, 0);
        const int outA = Add(q, "SetVar", "a", 400, 0), inB = Add(q, "SetVar", "b", 400, 150);
        q.linkExec(s3, 0, seq); q.linkExec(seq, 0, outA); q.linkExec(seq, 1, inB);
        CHECK(!VsCollapseToFunction(q, {seq, inB}).ok);
    }

    // ---- 4. Layout: align, distribute, straighten, auto-arrange, knots -----------------------------------------
    {
        std::vector<float> xs = {0, 100}, ys = {0, 0}, ws = {50, 150}, hs = {40, 40};
        VsAlignNodes(xs, ys, ws, hs, VsAlign::CenterX);
        CHECK_NEAR(xs[0] + 25, 125, 1e-4); CHECK_NEAR(xs[1] + 75, 125, 1e-4);
        xs = {0, 10, 300}; ys = {0, 50, 0}; ws = {100, 100, 100}; hs = {40, 40, 40};
        VsDistributeNodes(xs, ys, ws, hs, true);
        CHECK_NEAR(xs[0], 0, 1e-4); CHECK_NEAR(xs[1], 150, 1e-4); CHECK_NEAR(xs[2], 300, 1e-4);   // ends stay, gaps of 50
        xs = {0, 0, 0}; ys = {0, 500, 100}; hs = {40, 60, 40};
        VsDistributeNodes(xs, ys, ws, hs, false);   // order by position: 0, 100, 500; ends stay
        CHECK_NEAR(ys[0], 0, 1e-4); CHECK_NEAR(ys[1], 500, 1e-4); CHECK_NEAR(ys[2], 0 + 40 + (560 - 140) / 2.0f, 1e-3);
        CHECK_NEAR(VsStraightenY(100, 2, 500, 0, 20), 140, 1e-4);
        // Auto-arrange: 1 -> 2 -> 3, and a value block 4 read by 3 lands in the column before 3.
        const std::vector<int> ids = {1, 2, 3, 4};
        const auto pos = VsAutoArrange(ids, {500, 0, 200, 900}, {300, 0, 900, 50}, {100, 100, 100, 80}, {50, 50, 50, 30},
                                       {{1, 2}, {2, 3}, {4, 3}}, 40, 20);
        CHECK_NEAR(pos[0].first, 0, 1e-4); CHECK_NEAR(pos[0].second, 0, 1e-4);   // keeps the group's top-left
        CHECK(pos[1].first > pos[0].first && pos[2].first > pos[1].first);
        CHECK_NEAR(pos[3].first, pos[1].first, 1e-4);                          // same column as 2
        CHECK(std::fabs(pos[3].second - pos[1].second) >= 30);                  // stacked, not on top of each other
        // A loop back (3 -> 1) doesn't make it spin.
        const auto loop = VsAutoArrange(ids, {0, 0, 0, 0}, {0, 1, 2, 3}, {10, 10, 10, 10}, {10, 10, 10, 10}, {{1, 2}, {2, 3}, {3, 1}});
        CHECK(loop.size() == 4);

        // Commands on a tab: straighten a wire, align and auto-arrange the selection.
        VsEditState st;
        const int s = Add(st.g, "OnStart", "", 0, 0);
        const int p = Add(st.g, "Print", "", 300, 300);
        const int c = Add(st.g, "Const", "5", 0, 400);
        st.g.linkExec(s, 0, p);
        st.g.linkData(p, 0, c, 0);
        st.sel = {s, p};
        CHECK(VsRunCommand(st, "vs.straighten"));
        CHECK_NEAR(st.g.find(p)->y, st.g.find(s)->y, 1e-3);      // flow rows are row 0 on both: level
        st.sel = {p, c}; st.selected = -1;
        CHECK(VsRunCommand(st, "vs.alignLeft"));
        CHECK_NEAR(st.g.find(p)->x, st.g.find(c)->x, 1e-4);
        st.sel = {s, p, c};
        CHECK(VsRunCommand(st, "vs.autoArrange"));
        CHECK(st.g.find(p)->x > st.g.find(c)->x && st.g.find(c)->x > st.g.find(s)->x - 1);
        CHECK(!VsRunCommand(st, "vs.distributeH") || st.sel.size() >= 3);
        CHECK(!VsRunCommand(st, "no.such.command"));

        // Knots: kept across save / load, ignored by the runtime and the checker, gone with their wire.
        VsEditGraph g;
        const int a = Add(g, "OnStart"), b = Add(g, "Print", "", 400, 200), k = Add(g, "Const", "1", 0, 300);
        g.linkExec(a, 0, b);
        g.linkData(b, 0, k, 0);
        VsAddKnot(g, VsRefOfWire(g, true, 0), 200, 100);
        VsAddKnot(g, VsRefOfWire(g, false, 0), 150, 280);
        CHECK(g.knots.size() == 2);
        const std::string t = g.toText();
        CHECK(t.find("knot e") != std::string::npos && t.find("knot d") != std::string::npos);
        VsEditGraph back; back.fromText(t);
        CHECK(back.knots.size() == 2 && back.knots[0].exec && std::fabs(back.knots[0].x - 200) < 1);
        std::string perr;
        CHECK(okay::vs::NodeGraph::Parse(t, &perr) != nullptr);
        CHECK(!HasError(okay::vs::ValidateGraphText(t)));
        CHECK(VsCheckKey(t).find("knot") == std::string::npos);   // moving a knot doesn't re-run the checker
        back.unlinkExec(a, 0, b);
        CHECK(back.toText().find("knot e") == std::string::npos);
        back.pruneKnots();
        CHECK(back.knots.size() == 1);
    }

    // ---- 5. Find references across graph files; bookmarks ------------------------------------------------------
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::absolute("vs_ide_refs");
        fs::remove_all(root);
        fs::create_directories(root / "sub");
        fs::create_directories(root / ".hidden");
        auto write = [](const fs::path& p, const VsEditGraph& g) { std::ofstream(p, std::ios::binary) << g.toText(); };
        VsEditGraph ga, gb, gc;
        {
            const int s = Add(ga, "OnStart"), set = Add(ga, "SetVar", "score", 300, 0), call = Add(ga, "CallFunction", "Damage", 600, 0);
            const int ev = Add(ga, "OnEvent", "Hit", 0, 300), pr = Add(ga, "Print", "", 300, 300);
            ga.linkExec(s, 0, set); ga.linkExec(set, 0, call); ga.linkExec(ev, 0, pr);
            write(root / "a.okayvs", ga);
        }
        {
            const int f = Add(gb, "FunctionStart", "Damage(amount: Number)", 0, 0), get = Add(gb, "GetVar", "score", 0, 200);
            const int pr = Add(gb, "Print", "", 300, 0), send = Add(gb, "SendEvent", "Hit", 600, 0);
            gb.linkExec(f, 0, pr); gb.linkExec(pr, 0, send); gb.linkData(pr, 0, get, 0);
            gb.declare("score", "Number", "object");
            write(root / "sub" / "b.okayvs", gb);
        }
        { Add(gc, "OnUpdate"); write(root / "c.okayvs", gc); write(root / ".hidden" / "skip.okayvs", ga); }

        const std::string r = root.generic_string();
        const auto files = VsProjectGraphFiles(r);
        CHECK(files.size() == 3);   // the hidden folder is skipped
        auto refs = VsFindRefs(r, {VsFindWhat::Variable, "score"});
        int sets = 0, reads = 0, decls = 0;
        for (const auto& h : refs) { sets += h.role == "sets"; reads += h.role == "reads"; decls += h.role == "declares"; }
        CHECK(refs.size() == 3 && sets == 1 && reads == 1 && decls == 1);
        refs = VsFindRefs(r, {VsFindWhat::Function, "Damage"});
        CHECK(refs.size() == 2);
        bool defines = false, calls = false;
        for (const auto& h : refs) {
            if (h.role == "defines") { defines = true; CHECK(h.path.find("sub/b.okayvs") != std::string::npos && h.node == 1); }
            if (h.role == "calls") calls = true;
        }
        CHECK(defines && calls);
        refs = VsFindRefs(r, {VsFindWhat::Event, "Hit"});
        CHECK(refs.size() == 2);
        refs = VsFindRefs(r, {VsFindWhat::NodeType, "Print"});
        CHECK(refs.size() == 2);
        CHECK(VsFindRefs(r, {VsFindWhat::Event, "OnUpdate"}).size() == 1);
        // An open tab's unsaved text wins over the file.
        VsEditGraph edited; edited.fromText(ga.toText());
        for (auto& n : edited.nodes) if (n.type == "SetVar") n.arg = "points";
        const std::map<std::string, std::string> open = {{(root / "a.okayvs").generic_string(), edited.toText()}};
        CHECK(VsFindRefs(r, {VsFindWhat::Variable, "score"}, open).size() == 2);
        // What a block suggests searching for.
        CHECK(VsRefQueryFor(*gb.find(2)).kind == VsFindWhat::Variable && VsRefQueryFor(*gb.find(2)).name == "score");
        CHECK(VsRefQueryFor(*gb.find(1)).kind == VsFindWhat::Function && VsRefQueryFor(*gb.find(1)).name == "Damage");
        CHECK(VsRefQueryFor(*ga.find(4)).kind == VsFindWhat::Event && VsRefQueryFor(*ga.find(4)).name == "Hit");
        CHECK(VsRefQueryFor(*ga.find(5)).kind == VsFindWhat::NodeType);

        // Search Everywhere entries (the script IDE's box) find functions, events, variables and files.
        const auto se = VsSearchEverywhere(r, "damage");
        CHECK(!se.empty() && se[0].category == "Function" && se[0].title == "Damage" && se[0].node == 1);
        CHECK(!VsSearchEverywhere(r, "score").empty());
        CHECK(VsSearchEverywhere(r, "zzzznothing").empty());
        fs::remove_all(root);

        // Bookmarks.
        VsBookmarks bm;
        CHECK(bm.toggle("g.okayvs", 5, "Print") && bm.toggle("g.okayvs", 2) && bm.toggle("h.okayvs", 9));
        CHECK(bm.has("g.okayvs", 5) && !bm.has("g.okayvs", 9));
        CHECK(bm.next("g.okayvs", 2) == 5 && bm.next("g.okayvs", 5) == 2 && bm.next("g.okayvs", 5, false) == 2 && bm.next("g.okayvs", 2, false) == 5);
        CHECK(bm.next("none.okayvs", 0) == -1);
        VsBookmarks bm2; bm2.fromText(bm.toText() + "broken line\nx\tnotanumber\tl\n");
        CHECK(bm2.has("g.okayvs", 5) && bm2.has("h.okayvs", 9) && bm2.byPath["g.okayvs"].size() == 2);
        CHECK(!bm.toggle("g.okayvs", 5) && !bm.has("g.okayvs", 5));
        VsEditGraph one; Add(one, "OnStart");   // only block 1 exists
        bm.prune("g.okayvs", one);
        CHECK(!bm.has("g.okayvs", 2));
    }

    // ---- 6. Checker findings with fixes that leave a valid graph -------------------------------------------------
    {
        auto fixable = [](const VsEditGraph& g, int node) { return VsQuickFixesFor(g, okay::vs::ValidateGraphText(g.toText()), node); };
        auto finding = [](const VsEditGraph& g, const char* part) {
            for (const auto& i : okay::vs::ValidateGraphText(g.toText())) if (i.message.find(part) != std::string::npos) return true;
            return false;
        };
        // A Branch with nothing in Condition: the fix sets it to true.
        VsEditGraph g;
        const int s = Add(g, "OnStart"), br = Add(g, "Branch", "", 300, 0), pr = Add(g, "Print", "", 600, 0);
        g.linkExec(s, 0, br); g.linkExec(br, 0, pr);
        CHECK(finding(g, "Required: condition"));
        auto fixes = fixable(g, br);
        CHECK(fixes.size() == 1 && fixes[0].title == "Set Condition to true");
        CHECK(!fixes.empty() && VsApplyQuickFix(g, fixes[0]));
        CHECK(!finding(g, "Required: condition") && !HasError(okay::vs::ValidateGraphText(g.toText())));
        // A variable read but never set: the fix declares it.
        const int get = Add(g, "GetVar", "hp", 300, 200);
        g.linkData(pr, 0, get, 0);
        CHECK(finding(g, "never sets it"));
        fixes = fixable(g, get);
        CHECK(fixes.size() == 1 && fixes[0].title == "Create variable 'hp'");
        CHECK(!fixes.empty() && VsApplyQuickFix(g, fixes[0]));
        CHECK(!finding(g, "never sets it") && g.vars.size() == 1 && g.vars[0].name == "hp" && g.vars[0].typed);
        // Text into a Number input: the fix puts To Number between them.
        const int add = Add(g, "Add", "", 300, 400), txt = Add(g, "Const", "\"7\"", 0, 400), setV = Add(g, "SetVar", "hp", 900, 0);
        g.linkData(add, 0, txt, 0);
        g.linkExec(pr, 0, setV); g.linkData(setV, 0, add, 0);
        CHECK(HasError(okay::vs::ValidateGraphText(g.toText())));
        fixes = fixable(g, add);
        CHECK(fixes.size() == 1 && fixes[0].title.find("Insert") == 0);
        CHECK(!fixes.empty() && VsApplyQuickFix(g, fixes[0]));
        CHECK(!HasError(okay::vs::ValidateGraphText(g.toText())));
        // A step nothing runs: the fix plugs it in.
        const int lone = Add(g, "Print", "", 1200, 0);
        (void)lone;
        CHECK(finding(g, "nothing runs this node"));
        VsEditState st; st.g.fromText(g.toText());
        CHECK(VsRunCommand(st, "vs.fixAll"));
        CHECK(!finding(st.g, "nothing runs this node") && !HasError(okay::vs::ValidateGraphText(st.g.toText())));
        // No fix for a finding that has none (it's not offered).
        VsEditGraph h; const int e = Add(h, "OnStart"); (void)e;
        CHECK(fixable(h, e).empty());
    }

    // ---- 7. Graph diff against an earlier version ------------------------------------------------------------------
    {
        VsEditGraph a;
        const int s = Add(a, "OnStart"), p = Add(a, "Print", "", 300, 0), c = Add(a, "Const", "1", 0, 200);
        a.linkExec(s, 0, p); a.linkData(p, 0, c, 0);
        const std::string before = a.toText();
        VsEditGraph b; b.fromText(before);
        b.find(c)->arg = "2";                         // changed
        b.find(s)->x += 50;                           // moved only
        b.dataLinks.clear();                          // a wire removed
        const int w = Add(b, "Wait", "", 600, 0);     // added
        b.linkExec(p, 0, w);
        b.declare("speed", "Number", "object");
        const VsGraphDiff d = VsDiffGraphTexts(before, b.toText());
        CHECK(d.added.size() == 1 && d.added[0].id == w);
        CHECK(d.changed.size() == 1 && d.changed[0].id == c && d.changed[0].after.find("\"2\"") != std::string::npos);
        CHECK(d.moved.size() == 1 && d.moved[0].id == s);
        CHECK(d.removed.empty());
        CHECK(d.wiresRemoved.size() == 1 && d.wiresAdded.size() == 1 && d.varsAdded.size() == 1);
        CHECK(d.addedIds().count(w) && d.changedIds().count(c));
        CHECK(VsDiffGraphTexts(before, before).empty());
        b.removeNode(p);
        CHECK(VsDiffGraphTexts(before, b.toText()).removed.size() == 1);
    }

    // ---- The plugin layer's command table: one id per command, each one the canvas runs ---------------------------
    {
        std::set<std::string> idsSeen;
        for (const VsCommand& c : VsCommands()) CHECK(idsSeen.insert(c.id).second);
        CHECK(idsSeen.count("vs.collapseToFunction") && idsSeen.count("vs.findReferences"));
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
