#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("visualscript");

    // --- Build a graph in code: on update, move right by 2*dt. ---
    {
        Scene scene("VS");
        GameObject* go = scene.CreateGameObject("Mover");
        auto* vsc = go->AddComponent<VisualScriptComponent>();

        auto graph = std::make_unique<vs::NodeGraph>();
        auto* onUpdate = graph->Add<vs::EventNode>();
        auto* speed    = graph->Add<vs::ConstNode>(vs::VsValue{2.0f});
        auto* dt       = graph->Add<vs::DeltaTimeNode>();
        auto* mul      = graph->Add<vs::BinaryOpNode>(vs::BinaryOpNode::Op::Mul);
        auto* zero     = graph->Add<vs::ConstNode>(vs::VsValue{0.0f});
        auto* move     = graph->Add<vs::TranslateNode>();

        graph->ConnectData(mul->id, 0, speed->id, 0);
        graph->ConnectData(mul->id, 1, dt->id, 0);
        graph->ConnectData(move->id, 0, mul->id, 0); // x = speed*dt
        graph->ConnectData(move->id, 1, zero->id, 0); // y = 0
        graph->ConnectExec(onUpdate->id, 0, move->id);
        graph->SetEntry("OnUpdate", onUpdate->id);

        vsc->SetGraph(std::move(graph));
        scene.Start();
        for (int i = 0; i < 10; ++i) scene.Update(0.1f); // 10 * 0.1 = 1s

        // moved 2 units/sec for ~1s
        CHECK_NEAR(go->transform->localPosition.x, 2.0f, 0.001f);
        CHECK_NEAR(go->transform->localPosition.y, 0.0f, 0.001f);
    }

    // --- Parse a graph from text and verify a branch + variable. ---
    {
        const char* src = R"OKAYVS(
            # Count up on start, store result in 'score'
            node 0 OnStart
            node 1 Const 41
            node 2 Const 1
            node 3 Add
            node 4 SetVar score
            data 3 0 1 0
            data 3 1 2 0
            data 4 0 3 0
            exec 0 0 4
            entry OnStart 0
        )OKAYVS";

        Scene scene("VS2");
        GameObject* go = scene.CreateGameObject("Logic");
        auto* vsc = go->AddComponent<VisualScriptComponent>();
        std::string err;
        bool loaded = vsc->LoadFromText(src, &err);
        CHECK(loaded);
        if (!loaded) std::cerr << "  parse error: " << err << "\n";

        scene.Start();
        CHECK_NEAR(vsc->GetVariable("score").AsFloat(), 42.0f, 0.001f);
    }

    // --- Branch routing: pick different exec paths. ---
    {
        const char* src = R"OKAYVS(
            node 0 OnStart
            node 1 Const true
            node 2 Branch
            node 3 SetVar took_true
            node 4 SetVar took_false
            node 5 Const 1
            data 2 0 1 0
            data 3 0 5 0
            data 4 0 5 0
            exec 0 0 2
            exec 2 0 3
            exec 2 1 4
            entry OnStart 0
        )OKAYVS";

        Scene scene("VS3");
        GameObject* go = scene.CreateGameObject("Branchy");
        auto* vsc = go->AddComponent<VisualScriptComponent>();
        CHECK(vsc->LoadFromText(src));
        scene.Start();
        CHECK_NEAR(vsc->GetVariable("took_true").AsFloat(), 1.0f, 0.001f);
        CHECK_NEAR(vsc->GetVariable("took_false").AsFloat(), 0.0f, 0.001f);
    }

    TEST_MAIN_RESULT();
}
