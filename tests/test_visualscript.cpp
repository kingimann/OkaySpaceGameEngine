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

    // --- Expanded data nodes: math, logic, select, vectors. ---
    {
        const char* src = R"OKAYVS(
            node 0 OnStart
            node 1 Const 7
            node 2 Const 3
            node 3 Mod
            node 4 SetVar m
            node 10 Const 2
            node 11 Const 9
            node 12 Max
            node 13 SetVar mx
            node 20 Const -5
            node 21 Abs
            node 22 SetVar ab
            node 30 Const true
            node 31 Const 100
            node 32 Const 200
            node 33 Select
            node 34 SetVar sel
            node 40 Const 5
            node 41 Const 6
            node 42 Const 7
            node 43 MakeVec3
            node 44 VecZ
            node 45 SetVar vz
            data 3 0 1 0
            data 3 1 2 0
            data 4 0 3 0
            data 12 0 10 0
            data 12 1 11 0
            data 13 0 12 0
            data 21 0 20 0
            data 22 0 21 0
            data 33 0 30 0
            data 33 1 31 0
            data 33 2 32 0
            data 34 0 33 0
            data 43 0 40 0
            data 43 1 41 0
            data 43 2 42 0
            data 44 0 43 0
            data 45 0 44 0
            exec 0 0 4
            exec 4 0 13
            exec 13 0 22
            exec 22 0 34
            exec 34 0 45
            entry OnStart 0
        )OKAYVS";
        Scene scene("VS4");
        auto* vsc = scene.CreateGameObject("Math")->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vsc->LoadFromText(src, &err));
        if (!err.empty()) std::cerr << "  parse: " << err << "\n";
        scene.Start();
        CHECK_NEAR(vsc->GetVariable("m").AsFloat(), 1.0f, 0.001f);    // 7 % 3
        CHECK_NEAR(vsc->GetVariable("mx").AsFloat(), 9.0f, 0.001f);   // max(2,9)
        CHECK_NEAR(vsc->GetVariable("ab").AsFloat(), 5.0f, 0.001f);   // abs(-5)
        CHECK_NEAR(vsc->GetVariable("sel").AsFloat(), 100.0f, 0.001f);// true -> a
        CHECK_NEAR(vsc->GetVariable("vz").AsFloat(), 7.0f, 0.001f);   // z of (5,6,7)
    }

    // --- Sequence fires every output in order. ---
    {
        const char* src = R"OKAYVS(
            node 0 OnStart
            node 1 Sequence 2
            node 2 SetVar a
            node 3 SetVar b
            node 4 Const 1
            data 2 0 4 0
            data 3 0 4 0
            exec 0 0 1
            exec 1 0 2
            exec 1 1 3
            entry OnStart 0
        )OKAYVS";
        Scene scene("VS5");
        auto* vsc = scene.CreateGameObject("Seq")->AddComponent<VisualScriptComponent>();
        CHECK(vsc->LoadFromText(src));
        scene.Start();
        CHECK_NEAR(vsc->GetVariable("a").AsFloat(), 1.0f, 0.001f);
        CHECK_NEAR(vsc->GetVariable("b").AsFloat(), 1.0f, 0.001f);
    }

    // --- Stateful flow: Once gates, Timer pulses, AddVar accumulates. ---
    {
        const char* src = R"OKAYVS(
            node 0 OnUpdate
            node 1 Sequence 2
            node 2 Once
            node 3 AddVar inits
            node 4 Const 1
            node 5 Timer 1.0
            node 6 AddVar ticks
            node 7 Const 1
            data 3 0 4 0
            data 6 0 7 0
            exec 0 0 1
            exec 1 0 2
            exec 1 1 5
            exec 2 0 3
            exec 5 0 6
            entry OnUpdate 0
        )OKAYVS";
        Scene scene("VS6");
        auto* vsc = scene.CreateGameObject("Flow")->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vsc->LoadFromText(src, &err));
        if (!err.empty()) std::cerr << "  parse: " << err << "\n";
        scene.Start();
        for (int i = 0; i < 12; ++i) scene.Update(0.25f);  // 3.0s -> Timer fires at 1,2,3s
        CHECK_NEAR(vsc->GetVariable("inits").AsFloat(), 1.0f, 0.001f);  // Once: ran a single time
        CHECK_NEAR(vsc->GetVariable("ticks").AsFloat(), 3.0f, 0.001f);  // Timer: 3 pulses
    }

    // --- Conditions: Equals, Between; instructions: Repeat. ---
    {
        const char* src = R"OKAYVS(
            node 0 OnStart
            node 1 Const 5
            node 2 Const 5
            node 3 Equals
            node 4 SetVar eq
            node 10 Const 5
            node 11 Const 1
            node 12 Const 10
            node 13 Between
            node 14 SetVar btw
            node 20 Repeat 3
            node 21 AddVar hits
            node 22 Const 1
            data 3 0 1 0
            data 3 1 2 0
            data 4 0 3 0
            data 13 0 10 0
            data 13 1 11 0
            data 13 2 12 0
            data 14 0 13 0
            data 21 0 22 0
            exec 0 0 4
            exec 4 0 14
            exec 14 0 20
            exec 20 0 21
            entry OnStart 0
        )OKAYVS";
        Scene scene("VS7");
        auto* vsc = scene.CreateGameObject("Cond")->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vsc->LoadFromText(src, &err));
        if (!err.empty()) std::cerr << "  parse: " << err << "\n";
        scene.Start();
        CHECK_NEAR(vsc->GetVariable("eq").AsFloat(), 1.0f, 0.001f);    // 5 == 5
        CHECK_NEAR(vsc->GetVariable("btw").AsFloat(), 1.0f, 0.001f);   // 1 <= 5 <= 10
        CHECK_NEAR(vsc->GetVariable("hits").AsFloat(), 3.0f, 0.001f);  // Repeat 3
    }

    // --- Wait: one-shot delay then passes through. ---
    {
        const char* src = R"OKAYVS(
            node 0 OnUpdate
            node 1 Wait 1.0
            node 2 AddVar after
            node 3 Const 1
            data 2 0 3 0
            exec 0 0 1
            exec 1 0 2
            entry OnUpdate 0
        )OKAYVS";
        Scene scene("VS8");
        auto* vsc = scene.CreateGameObject("Waiter")->AddComponent<VisualScriptComponent>();
        CHECK(vsc->LoadFromText(src));
        scene.Start();
        for (int i = 0; i < 8; ++i) scene.Update(0.25f);   // 2.0s; passes from 1.0s on (updates 4..8)
        CHECK_NEAR(vsc->GetVariable("after").AsFloat(), 5.0f, 0.001f);
    }

    // --- Multiplayer/Steam nodes are safe offline (no session / no Steam). ---
    {
        const char* src = R"OKAYVS(
            node 0 OnStart
            node 1 NetConnected
            node 2 SetVar conn
            node 3 NetPeers
            node 4 SetVar peers
            node 5 SteamIsUnlocked ACH_TEST
            node 6 SetVar unlocked
            data 2 0 1 0
            data 4 0 3 0
            data 6 0 5 0
            exec 0 0 2
            exec 2 0 4
            exec 4 0 6
            entry OnStart 0
        )OKAYVS";
        Scene scene("VS9"); scene.physicsEnabled = false;
        auto* vsc = scene.CreateGameObject("NetSteam")->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vsc->LoadFromText(src, &err));
        if (!err.empty()) std::cerr << "  parse: " << err << "\n";
        scene.Start();
        CHECK_NEAR(vsc->GetVariable("conn").AsFloat(), 0.0f, 0.001f);     // not connected
        CHECK_NEAR(vsc->GetVariable("peers").AsFloat(), 0.0f, 0.001f);    // no peers
        CHECK_NEAR(vsc->GetVariable("unlocked").AsFloat(), 0.0f, 0.001f); // no Steam -> false
    }

    TEST_MAIN_RESULT();
}
