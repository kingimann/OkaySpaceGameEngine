// Every text-script builtin has a visual-script node, each of those nodes runs,
// and the escape hatches do what they say. The coverage scene runs without errors.
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include <Okay.hpp>
#include "okay/Components/Light.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/VsBuiltins.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>

using namespace okay;

static std::vector<std::string> g_errs;
static void OnErr(const vs::VsRuntimeError& e) { g_errs.push_back(e.nodeType + " " + e.message); }

static bool RunGraph(Scene& s, GameObject* go, const std::string& text) {
    auto* vc = go->GetComponent<VisualScriptComponent>();
    if (!vc) vc = go->AddComponent<VisualScriptComponent>();
    std::string err;
    if (!vc->LoadFromText(text, &err)) { std::printf("  load: %s\n", err.c_str()); return false; }
    g_errs.clear();
    s.Start();
    s.Update(1.0f / 60.0f);
    return true;
}

int main() {
    RUN_SUITE("vs_script_coverage");
    // Every builtin node runs with the dummy argument "x": the file builtins (prefs, creations, save slots)
    // would write files of that name into the folder the test was started from.
    okaytest::Sandbox sandbox("vs_script_coverage", OKAY_SOURCE_DIR);
    vs::SetRuntimeErrorHook(&OnErr);
    Time::SetTimeScale(1);

    OkayScriptVM listing;
    const auto names = listing.BuiltinNames();
    CHECK(names.size() > 100);
    const auto& nodes = vs::BuiltinNodes();
    std::set<std::string> have;
    for (const auto& n : nodes) have.insert(n.builtin);
    int missing = 0;
    for (const std::string& name : names) {
        bool token = !name.empty();
        for (char c : name)
            if (!(std::isalnum((unsigned char)c) || c == '_' || c == '.')) token = false;
        if (!token) continue;   // not a single graph token; documented in the coverage page
        if (!have.count(name) || !vs::IsBuiltinNode("Fn." + name)) {
            std::printf("  no visual-script node for builtin %s\n", name.c_str());
            ++missing;
        }
    }
    CHECK(missing == 0);
    std::printf("  builtin parity %d / %d token builtins\n", (int)have.size(), (int)names.size());

    // Table-driven: every builtin node actually runs on an object. A builtin that
    // refuses its arguments reports a node error; that still counts as running.
    {
        Scene s; s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Host");
        int ran = 0;
        for (const auto& n : nodes) {
            const std::string text =
                "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 " + n.type + "\n"
                "default 2 0 \"x\"\ndefault 2 1 1\nexec 1 0 2\n";
            if (!RunGraph(s, go, text)) { std::printf("  builtin graph failed: %s\n", n.builtin.c_str()); CHECK(false); break; }
            ++ran;
        }
        CHECK(ran == (int)nodes.size());
        std::printf("  ran %d builtin nodes\n", ran);
    }

    // Escape hatches.
    {
        Scene s; s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Host");
        go->tag = "player";
        auto* light = go->AddComponent<Light>();
        light->intensity = 3.5f;
        CHECK(RunGraph(s, go,
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 GetProperty Light.intensity\n"
            "node 3 SetVar bright\nexec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n"));
        auto* vc = go->GetComponent<VisualScriptComponent>();
        CHECK(vc);
        CHECK_NEAR(vc->GetVariable("bright").AsFloat(), 3.5f, 0.05f);
        CHECK(g_errs.empty());

        CHECK(RunGraph(s, go,
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 SetProperty GameObject.tag\ndefault 2 0 hero\nexec 1 0 2\n"));
        CHECK(go->tag == "hero");
        CHECK(g_errs.empty());

        CHECK(RunGraph(s, go,
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 CallMethod Mathf.Abs\ndefault 2 0 -4\n"
            "node 3 SetVar abs\nexec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n"));
        CHECK_NEAR(vc->GetVariable("abs").AsFloat(), 4.0f, 0.01f);
        CHECK(g_errs.empty());

        CHECK(RunGraph(s, go,
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 GetComponent Light\nnode 3 SetVar has\n"
            "exec 1 0 2\nexec 2 0 3\ndata 3 0 2 1\n"));
        CHECK(vc->GetVariable("has").AsBool());

        GameObject* other = s.CreateGameObject("Plain");
        CHECK(RunGraph(s, other, "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 AddComponent Light\nexec 1 0 2\n"));
        CHECK(other->GetComponent<Light>() != nullptr);
        CHECK(g_errs.empty());

        go->tag = "coin";
        CHECK(RunGraph(s, go,
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 FindByTag coin\nnode 3 SetVar id\n"
            "exec 1 0 3\ndata 3 0 2 0\n"));
        CHECK(vc->GetVariable("id").AsObjectId() == go->InstanceId());

        CHECK(RunGraph(s, go,
            "node 1 OnEvent ping\nentry OnEvent:ping 1\nnode 2 SetVar heard\ndefault 2 0 1\nexec 1 0 2\n"
            "node 3 OnUpdate\nentry OnUpdate 3\nnode 4 SendMessage ping\nexec 3 0 4\n"));
        CHECK(vc->GetVariable("heard").AsBool());

        CHECK(RunGraph(s, go,
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 RunSnippet \"2 + 3\"\nnode 3 SetVar sum\n"
            "exec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n"));
        if (!g_errs.empty()) for (const auto& e : g_errs) std::printf("  snippet: %s\n", e.c_str());
        CHECK(g_errs.empty());
        CHECK_NEAR(vc->GetVariable("sum").AsFloat(), 5.0f, 0.01f);

        auto* sc = other->AddComponent<ScriptComponent>();
        std::string serr;
        CHECK(sc->LoadSource("function pinged() { return 7 }\n", &serr));
        CHECK(RunGraph(s, other,
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 CallScript pinged\nnode 3 SetVar ret\n"
            "exec 1 0 2\nexec 2 0 3\ndata 3 0 2 0\n"));
        CHECK_NEAR(other->GetComponent<VisualScriptComponent>()->GetVariable("ret").AsFloat(), 7.0f, 0.01f);
    }

    // The playground graph names every builtin node and the escape hatches, and runs.
    {
        const std::string dir = std::string(OKAY_SOURCE_DIR) + "/examples/visual_script/playground/";
        if (std::getenv("OKAY_WRITE_COVERAGE")) {
            std::ofstream graph(dir + "coverage.okayvs");
            graph << "# Every text-script builtin, plus the escape hatches. Only the safe chain runs.\n";
            graph << "node 1 OnStart\nentry OnStart 1\n";
            graph << "node 2 Fn.print\ndefault 2 0 \"playground\"\nexec 1 0 2\n";
            graph << "node 3 GetProperty GameObject.name\nnode 4 SetVar name\nexec 2 0 3\nexec 3 0 4\ndata 4 0 3 0\n";
            graph << "node 5 SetProperty GameObject.tag\ndefault 5 0 demo\nexec 4 0 5\n";
            graph << "node 6 CallMethod Mathf.Abs\ndefault 6 0 -2\n";
            graph << "node 7 GetComponent Transform\nnode 8 AddComponent Light\n";
            graph << "node 9 FindByTag demo\nnode 10 AllWithTag demo\n";
            graph << "node 11 SendMessage ping\nnode 12 Broadcast ping\n";
            graph << "node 13 RunSnippet \"1 + 1\"\nnode 14 CallScript pinged\n";
            int id = 100;
            for (const auto& n : nodes) {
                if (n.type == "Fn.print") continue;
                graph << "node " << id++ << " " << n.type << "\n";
            }
            std::ofstream md(std::string(OKAY_SOURCE_DIR) + "/docs/visual_script_coverage.md");
            md << vs::CoverageTableMarkdown();
            std::printf("  wrote coverage graph and docs\n");
        }
        std::ifstream in(dir + "coverage.okayvs");
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        int absent = 0;
        for (const auto& n : nodes)
            if (text.find(" " + n.type + "\n") == std::string::npos && text.find(" " + n.type + " ") == std::string::npos) {
                if (absent < 8) std::printf("  coverage graph missing %s\n", n.type.c_str());
                ++absent;
            }
        for (const char* t : {"GetProperty","SetProperty","CallMethod","GetComponent","AddComponent",
                              "FindByTag","SendMessage","Broadcast","RunSnippet","CallScript","AllWithTag"})
            if (text.find(t) == std::string::npos) { std::printf("  coverage graph missing %s\n", t); ++absent; }
        CHECK(absent == 0);
        Scene scene;
        std::string err;
        CHECK(SceneSerializer::LoadFromFile(scene, dir + "coverage.okayscene", &err));
        g_errs.clear();
        scene.Start();
        for (int i = 0; i < 30; ++i) scene.Update(1.0f / 60.0f);
        if (!g_errs.empty()) for (const auto& e : g_errs) std::printf("  coverage scene: %s\n", e.c_str());
        CHECK(g_errs.empty());
    }

    if (std::getenv("OKAY_WRITE_COVERAGE")) {
        std::ofstream out(std::string(OKAY_SOURCE_DIR) + "/docs/visual_script_coverage.md");
        out << vs::CoverageTableMarkdown();
        std::printf("  wrote docs/visual_script_coverage.md\n");
    } else {
        std::ifstream doc(std::string(OKAY_SOURCE_DIR) + "/docs/visual_script_coverage.md");
        std::string md((std::istreambuf_iterator<char>(doc)), std::istreambuf_iterator<char>());
        CHECK(md.find("Visual script coverage") != std::string::npos);
        CHECK(md.find("Fn.print") != std::string::npos);
        CHECK(md.find("Get Property") != std::string::npos);
    }

    CHECK(sandbox.Verify());   // nothing was written into the source tree
    vs::SetRuntimeErrorHook(nullptr);
    Time::SetTimeScale(1);
    TEST_MAIN_RESULT();
}
