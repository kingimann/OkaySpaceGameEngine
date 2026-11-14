// Switch Number, Latch Gate, For Each Map and Run Another Graph each have a Node Playground
// scene. This loads each one, presses Play, and checks what the player would read on screen.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/Camera.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/VisualScript/VsTypes.hpp"
#include <cstdio>
#include <fstream>
#include <string>

using namespace okay;

static std::vector<std::string> g_errs;
static void OnErr(const vs::VsRuntimeError& e) { g_errs.push_back(e.nodeType + ": " + e.message); }

static std::string ReadFile(const std::string& p) {
    std::ifstream in(p);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void Build(Scene& sc, const std::string& graph, const std::string& hint) {
    GameObject* cam = sc.CreateGameObject("Main Camera");
    cam->transform->SetPosition(Vec3(0, 6, -10));
    cam->AddComponent<Camera>();
    cam->transform->LookAt(Vec3(0, 0, 0));
    GameObject* help = sc.CreateGameObject("Instructions");
    auto* ht = help->AddComponent<TextRenderer>();
    ht->text = "Press Play. " + hint;
    ht->screenSpace = true; ht->screenPos = {24, 28}; ht->pixelSize = 2.2f;
    ht->size = {900, 120}; ht->wrap = true; ht->color = Color::White;
    GameObject* read = sc.CreateGameObject("Readout");
    auto* rt = read->AddComponent<TextRenderer>();
    rt->text = "Press Play";
    rt->screenSpace = true; rt->screenPos = {24, 200}; rt->pixelSize = 3.2f;
    rt->size = {900, 80}; rt->color = Color{0.45f, 1.0f, 0.55f, 1.0f};
    GameObject* runner = sc.CreateGameObject("Runner");
    runner->transform->SetPosition(Vec3(0, 0.5f, 0));
    runner->AddComponent<MeshRenderer>()->color = Color(0.9f, 0.9f, 0.95f, 1);
    runner->AddComponent<VisualScriptComponent>()->SetPath(graph);
}

int main() {
    RUN_SUITE("vs_flow_playground");
    struct Row { const char* node; const char* base; const char* hint; const char* readout; int frames; const char* var; float varWant; };
    const Row rows[] = {
        {"SwitchCases", "flow_switch", "The number 5 matches the third case, so the readout says case 3.", "Switch Number: 5 chose case 3", 2, "chosen", 3.0f},
        {"LatchGate", "flow_latch", "The gate flips open and closed each frame. The readout counts what passed.", "Latch Gate passes: 3", 6, "passes", 3.0f},
        {"ForEachMap", "flow_map", "A map of three things is added up. The readout shows the total, 13.", "Map total: 13", 2, "total", 13.0f},
        {"RunGraph", "flow_run_graph", "A second graph sets a number. The readout shows it, 7.", "Run Another Graph: 7", 2, "from_sub", 7.0f},
    };
    const std::string dir = std::string(OKAY_SOURCE_DIR) + "/examples/visual_script/playground/";
    vs::SetRuntimeErrorHook(&OnErr);
    for (const Row& r : rows) {
        const std::string base = r.base;
        if (const char* w = std::getenv("OKAY_WRITE_GL_PLAYGROUND"); w && w[0] == '1') {
            Scene writing; Build(writing, base + ".okayvs", r.hint);
            CHECK(SceneSerializer::SaveToFile(writing, dir + base + ".okayscene"));
            std::remove((dir + base + ".okayscene.bak").c_str());
        }
        const std::string graph = ReadFile(dir + base + ".okayvs");
        CHECK(!graph.empty());
        CHECK(graph.find(std::string(" ") + r.node) != std::string::npos);   // the node is in the graph
        for (const auto& is : vs::ValidateGraphText(graph, dir)) { std::printf("  %s: %s\n", r.base, is.message.c_str()); CHECK(false); }
        Scene scene; std::string err;
        CHECK(SceneSerializer::LoadFromFile(scene, dir + base + ".okayscene", &err));
        if (!err.empty()) std::printf("  load %s: %s\n", r.base, err.c_str());
        g_errs.clear();
        scene.Start();
        for (int i = 0; i < r.frames; ++i) scene.Update(0.016f);
        GameObject* runner = scene.Find("Runner");
        GameObject* read = scene.Find("Readout");
        CHECK(runner && read);
        if (!runner || !read) continue;
        auto* vc = runner->GetComponent<VisualScriptComponent>();
        auto* tr = read->GetComponent<TextRenderer>();
        CHECK(vc && tr);
        if (!vc || !tr) continue;
        if (tr->text != r.readout) std::printf("  %s: readout is \"%s\", wanted \"%s\"\n", r.base, tr->text.c_str(), r.readout);
        CHECK(tr->text == r.readout);
        CHECK_NEAR(vc->GetVariable(r.var).AsFloat(), r.varWant, 0.01f);
        for (const auto& e : g_errs) std::printf("  %s: %s\n", r.base, e.c_str());
        CHECK(g_errs.empty());
    }
    vs::SetRuntimeErrorHook(nullptr);
    TEST_MAIN_RESULT();
}
