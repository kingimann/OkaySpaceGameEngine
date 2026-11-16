// What the text export cannot translate is not a graph error. The Visual Script
// window's Problems list (VsGraphProblems, the same function the window calls)
// shows the graph checker's real errors; nodes the export can't turn into text
// script are at most one Info line there, and the export itself still names
// each one. Text-script builtin nodes (Fn.print, Fn.Mathf.Abs, ...) export as a
// plain call, name(args), and the exported script does what the graph does.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include <Okay.hpp>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace okay;
using namespace okay::editor::vsed;

namespace {
// The safe chain of examples/visual_script/playground/coverage.okayvs.
const char* kSafeChain =
    "node 1 OnStart\n"
    "entry OnStart 1\n"
    "node 2 Fn.print\n"
    "default 2 0 \"playground\"\n"
    "exec 1 0 2\n"
    "node 3 GetProperty GameObject.name\n"
    "node 4 SetVar name\n"
    "exec 2 0 3\n"
    "exec 3 0 4\n"
    "data 4 0 3 0\n"
    "node 5 SetProperty GameObject.tag\n"
    "default 5 0 demo\n"
    "exec 4 0 5\n";

int Count(const std::vector<okay::vs::VsIssue>& v, int severity) {
    int n = 0;
    for (const auto& is : v) if (is.severity == severity) ++n;
    return n;
}
bool HasNode(const std::vector<okay::vs::VsIssue>& v, int node) {
    for (const auto& is : v) if (is.node == node) return true;
    return false;
}
void Dump(const char* what, const std::vector<okay::vs::VsIssue>& v) {
    for (const auto& is : v)
        std::printf("  %s: severity %d node %d: %s\n", what, is.severity, is.node, is.message.c_str());
}
} // namespace

int main() {
    RUN_SUITE("vs_export_not_error");
    auto sink = Log::sink;
    std::vector<std::string> logged;
    Log::sink = [&](Log::Level, const std::string& m) { logged.push_back(m); };

    // ---- The safe chain: no errors in the Problems list ----
    {
        VsEditGraph g;
        g.fromText(kSafeChain);
        std::vector<okay::vs::VsIssue> exp;
        const auto problems = VsGraphProblems(g, "", &exp);
        if (Count(problems, 0) != 0) Dump("problem", problems);
        CHECK(Count(problems, 0) == 0);
        // Nothing in the list mentions a node as "Cannot export": at most one
        // graph-wide Info line sums the export up.
        int perNode = 0, info = 0;
        for (const auto& is : problems) {
            if (is.message.rfind("Cannot export node ", 0) == 0) ++perNode;
            if (is.severity == 2) {
                ++info;
                CHECK(is.node == -1);
                CHECK(is.message.find("can't be exported to text script") != std::string::npos);
                CHECK(is.message.find("GetProperty #3") != std::string::npos);
                CHECK(is.message.find("SetProperty #5") != std::string::npos);
                CHECK(is.message.find("Fn.print") == std::string::npos);
            }
        }
        CHECK(perNode == 0);
        CHECK(info == 1);

        // The export, called directly, still names the nodes it truly can't
        // translate, as errors on those nodes; Fn.print is not one of them.
        std::vector<okay::vs::VsIssue> blocked;
        CompileToOkayScript(g, &blocked);
        if (blocked.size() != 2) Dump("blocked", blocked);
        CHECK(blocked.size() == 2);
        CHECK(HasNode(blocked, 3));   // GetProperty
        CHECK(HasNode(blocked, 5));   // SetProperty
        CHECK(!HasNode(blocked, 2));  // Fn.print exports
        CHECK(Count(blocked, 0) == (int)blocked.size());
        CHECK(exp.size() == blocked.size());
    }

    // ---- A graph with a real error still shows it as an error ----
    {
        VsEditGraph g;
        g.fromText("node 1 OnStart\nentry OnStart 1\nnode 2 GetVar never_set\nnode 3 Print\nexec 1 0 3\ndata 3 0 2 0\n"
                   "node 4 Const 1\nnode 5 Add\ndata 5 0 4 0\nnode 6 OnKey nosuchkey\nentry OnKey:nosuchkey 6\n");
        const auto mine = VsGraphProblems(g, "");
        const auto checker = okay::vs::ValidateGraphText(g.toText(), "");
        // Every checker finding is in the list unchanged, in its order, first.
        CHECK(mine.size() >= checker.size());
        for (std::size_t i = 0; i < checker.size() && i < mine.size(); ++i) {
            CHECK(mine[i].severity == checker[i].severity);
            CHECK(mine[i].node == checker[i].node);
            CHECK(mine[i].message == checker[i].message);
        }
        CHECK(Count(checker, 0) > 0);   // the bad key name is a real error
        CHECK(Count(mine, 0) == Count(checker, 0));
    }

    // ---- Fn.print exports as print(...) and runs ----
    {
        VsEditGraph g;
        g.fromText("node 1 OnStart\nentry OnStart 1\nnode 2 Fn.print\ndefault 2 0 \"hello from export\"\nexec 1 0 2\n"
                   "node 3 SetVar done\nnode 4 Const 1\nexec 2 0 3\ndata 3 0 4 0\n");
        std::vector<okay::vs::VsIssue> blocked;
        const std::string code = CompileToOkayScript(g, &blocked);
        if (!blocked.empty()) Dump("blocked", blocked);
        CHECK(blocked.empty());
        CHECK(code.find("print(\"hello from export\")") != std::string::npos);
        CHECK(code.find("NOT TRANSLATED") == std::string::npos);

        Scene scene("fn-print");
        auto* sc = scene.CreateGameObject("S")->AddComponent<ScriptComponent>("okayscript");
        std::string err;
        const bool loaded = sc->LoadSource(code, &err);
        if (!loaded) std::printf("  load failed: %s\n%s\n", err.c_str(), code.c_str());
        CHECK(loaded);
        logged.clear();
        scene.Start();
        bool printed = false;
        for (const auto& m : logged) if (m.find("hello from export") != std::string::npos) printed = true;
        if (!printed) std::printf("  print did not log; code:\n%s\n", code.c_str());
        CHECK(printed);
        CHECK(sc->VM() && std::fabs(sc->VM()->GetGlobal("done").AsFloat() - 1.0f) < 0.001f);
    }

    // ---- A builtin with a result: the value wire reads what the call gave ----
    {
        VsEditGraph g;
        g.fromText("node 1 OnStart\nentry OnStart 1\nnode 2 Fn.Mathf.Abs\ndefault 2 0 -2.5\nexec 1 0 2\n"
                   "node 3 SetVar a\nexec 2 0 3\ndata 3 0 2 0\n"
                   "node 4 Fn.max\ndefault 4 0 3\ndefault 4 1 7\nexec 3 0 4\n"
                   "node 5 SetVar b\nexec 4 0 5\ndata 5 0 4 0\n");
        std::vector<okay::vs::VsIssue> blocked;
        const std::string code = CompileToOkayScript(g, &blocked);
        if (!blocked.empty()) Dump("blocked", blocked);
        CHECK(blocked.empty());
        CHECK(code.find("Mathf.Abs(-2.5)") != std::string::npos);
        CHECK(code.find("max(3, 7)") != std::string::npos);

        Scene graphScene("fn-graph");
        auto* vs = graphScene.CreateGameObject("G")->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vs->LoadFromText(g.toText(), &err));
        graphScene.Start();

        Scene scriptScene("fn-script");
        auto* sc = scriptScene.CreateGameObject("S")->AddComponent<ScriptComponent>("okayscript");
        const bool loaded = sc->LoadSource(code, &err);
        if (!loaded) std::printf("  load failed: %s\n%s\n", err.c_str(), code.c_str());
        CHECK(loaded);
        scriptScene.Start();
        const float ga = vs->GetVariable("a").AsFloat(), gb = vs->GetVariable("b").AsFloat();
        const float sa = sc->VM() ? sc->VM()->GetGlobal("a").AsFloat() : -1.f;
        const float sb = sc->VM() ? sc->VM()->GetGlobal("b").AsFloat() : -1.f;
        if (std::fabs(ga - sa) > 0.001f || std::fabs(gb - sb) > 0.001f)
            std::printf("  graph a=%.3f b=%.3f, script a=%.3f b=%.3f\n%s\n", ga, gb, sa, sb, code.c_str());
        CHECK(std::fabs(ga - 2.5f) < 0.001f);
        CHECK(std::fabs(sa - 2.5f) < 0.001f);
        CHECK(std::fabs(gb - 7.0f) < 0.001f);
        CHECK(std::fabs(sb - 7.0f) < 0.001f);
    }

    // ---- The whole coverage example: no "Cannot export" errors in its list ----
    {
        std::ifstream in(std::string(OKAY_SOURCE_DIR) + "/examples/visual_script/playground/coverage.okayvs");
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(!text.empty());
        VsEditGraph g;
        g.fromText(text);
        std::vector<okay::vs::VsIssue> exp;
        const auto problems = VsGraphProblems(g, "", &exp);
        int perNode = 0, info = 0;
        for (const auto& is : problems) {
            if (is.message.rfind("Cannot export node ", 0) == 0) ++perNode;
            if (is.severity == 2) ++info;
        }
        CHECK(perNode == 0);
        CHECK(info <= 1);
        CHECK(Count(problems, 0) == Count(okay::vs::ValidateGraphText(g.toText(), ""), 0));
        for (const auto& is : exp) CHECK(is.message.find("Fn.") == std::string::npos);
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
