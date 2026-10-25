#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include <chrono>
#include <cmath>
#include <string>

using namespace okay;

// The second wave of visual-script nodes, run through the real text format +
// VisualScriptComponent: math, vectors, text, object queries, actions and the
// collision event entry.
int main() {
    RUN_SUITE("vs_nodes");
    Scene scene;
    GameObject* self = scene.CreateGameObject("Hero");
    GameObject* target = scene.CreateGameObject("Target");
    target->transform->SetPosition({0, 0, 10});
    target->tag = "enemy";
    auto* vs = self->AddComponent<VisualScriptComponent>();
    std::string g =
        "node 1 OnStart\n"
        "node 2 SetVar remap\n"   "node 3 Remap\n" "node 4 Const 5\n" "node 5 Const 0\n" "node 6 Const 10\n" "node 7 Const 100\n"
        "data 3 0 4 0\n" "data 3 1 5 0\n" "data 3 2 6 0\n" "data 3 3 5 0\n" "data 3 4 7 0\n" "data 2 0 3 0\n"
        "node 8 SetVar pp\n" "node 9 PingPong\n" "node 10 Const 3\n" "node 11 Const 2\n"
        "data 9 0 10 0\n" "data 9 1 11 0\n" "data 8 0 9 0\n"
        "node 12 SetVar dist\n" "node 13 Vec3Distance\n" "node 14 MakeVec3\n" "node 15 MakeVec3\n" "node 16 Const 3\n" "node 17 Const 4\n"
        "data 15 0 16 0\n" "data 15 1 17 0\n" "data 13 0 14 0\n" "data 13 1 15 0\n" "data 12 0 13 0\n"
        "node 18 SetVar fixed\n" "node 19 ToFixed 2\n" "node 20 Const 3.14159\n" "data 19 0 20 0\n" "data 18 0 19 0\n"
        "node 21 SetVar to\n" "node 22 DistanceTo Target\n" "data 21 0 22 0\n"
        "node 23 SetVar sm\n" "node 24 SmoothStep\n" "node 25 Const 0.5\n" "node 26 Const 1\n"
        "data 24 0 5 0\n" "data 24 1 26 0\n" "data 24 2 25 0\n" "data 23 0 24 0\n"
        "node 27 SetActive Target\n" "node 28 Const false\n" "data 27 0 28 0\n"
        "exec 1 0 2\n" "exec 2 0 8\n" "exec 8 0 12\n" "exec 12 0 18\n" "exec 18 0 21\n" "exec 21 0 23\n" "exec 23 0 27\n"
        "entry OnStart 1\n"
        // Collision event: remember who we hit and their tag.
        "node 40 OnCollision\n" "node 41 SetVar hit\n" "node 42 OtherName\n" "node 43 SetVar hitTag\n" "node 44 OtherTag\n"
        "data 41 0 42 0\n" "data 43 0 44 0\n" "exec 40 0 41\n" "exec 41 0 43\n"
        "entry OnCollision 40\n";
    std::string err;
    CHECK(vs->LoadFromText(g, &err));
    CHECK(err.empty());
    scene.Start();
    CHECK_NEAR(vs->GetVariable("remap").AsFloat(), 50.0f, 1e-4f);
    CHECK_NEAR(vs->GetVariable("pp").AsFloat(), 1.0f, 1e-4f);
    CHECK_NEAR(vs->GetVariable("dist").AsFloat(), 5.0f, 1e-4f);
    CHECK(vs->GetVariable("fixed").AsString() == "3.14");
    CHECK_NEAR(vs->GetVariable("to").AsFloat(), 10.0f, 1e-4f);
    CHECK_NEAR(vs->GetVariable("sm").AsFloat(), 0.5f, 1e-4f);
    CHECK(!target->active);

    Collision3D c; c.gameObject = target;
    vs->OnCollisionEnter3D(c);
    CHECK(vs->GetVariable("hit").AsString() == "Target");
    CHECK(vs->GetVariable("hitTag").AsString() == "enemy");

    // Every new node type is known to the factory.
    for (const char* t : {"Tan", "Atan2", "Exp", "Log", "InverseLerp", "Repeat01", "MoveTowardsValue", "DeltaAngle",
                          "Vec3Add", "Vec3Sub", "Vec3Scale", "Vec3Length", "Vec3Normalize", "Vec3Dot", "ToString",
                          "TextLength", "Contains", "Upper", "Lower", "ObjectExists", "ObjectPosition", "SelfName",
                          "MouseButton", "MouseButtonDown", "LookAt", "MoveTowards", "SetText", "LoadScene", "Cooldown",
                          "AnimTrigger", "AnimSetFloat", "AnimSetBool", "AnimState", "IkLook", "IkHand", "OnTrigger", "OnClick"}) {
        std::string e;
        auto n = vs::MakeNode(t, {"x"}, &e);
        CHECK(n != nullptr);
        if (!n) std::printf("  missing node type: %s (%s)\n", t, e.c_str());
    }
    // --- A loop's Body wired back into the loop itself: an error, not a C++ stack overflow ---
    {
        const char* loops[] = {"ForLoop", "Repeat 3", "While", "Sequence 2"};
        for (const char* l : loops) {
            Scene sc;
            auto* v = sc.CreateGameObject("Loop")->AddComponent<VisualScriptComponent>();
            std::string src = std::string("node 1 OnStart\nnode 2 ") + l + "\nnode 3 Const 0\nnode 4 Const 5\nnode 5 Const true\n"
                              "exec 1 0 2\nexec 2 0 2\nentry OnStart 1\n";
            if (std::string(l) == "ForLoop") src += "data 2 0 3 0\ndata 2 1 4 0\n";
            if (std::string(l) == "While") src += "data 2 0 5 0\n";
            std::string err;
            CHECK(v->LoadFromText(src, &err));
            auto sink = Log::sink; Log::sink = [](Log::Level, const std::string&) {};
            sc.Start();                                  // used to recurse until the stack overflowed
            Log::sink = sink;
            CHECK(v->Graph() && v->Graph()->ErrorCount() > 0);
            if (v->Graph() && v->Graph()->ErrorCount() == 0) std::printf("  no error for %s\n", l);
        }
    }

    // --- The step budget counts data pulls: an exec self-loop pulling a deep chain stops fast ---
    {
        std::string src = "node 1 OnStart\nnode 2 SetVar x\nnode 3 Const 1\n";
        int prev = 3;
        for (int i = 0; i < 200; ++i) {           // Add(prev, 1) x 200
            const int id = 10 + i;
            src += "node " + std::to_string(id) + " Add\n";
            src += "data " + std::to_string(id) + " 0 " + std::to_string(prev) + " 0\n";
            src += "data " + std::to_string(id) + " 1 3 0\n";
            prev = id;
        }
        // SetVar x <- chain, then AddVar n += 1, looping back: n counts the iterations.
        src += "data 2 0 " + std::to_string(prev) + " 0\nnode 4 AddVar n\nnode 5 Const 1\ndata 4 0 5 0\n"
               "exec 1 0 2\nexec 2 0 4\nexec 4 0 2\nentry OnStart 1\n";
        Scene sc;
        auto* v = sc.CreateGameObject("Busy")->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(v->LoadFromText(src, &err));
        auto sink = Log::sink; Log::sink = [](Log::Level, const std::string&) {};
        const auto t0 = std::chrono::steady_clock::now();
        sc.Start();
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        Log::sink = sink;
        // The reads have their own 4M budget: ~4M / ~402 reads an iteration, not
        // the ~500k (1M exec steps / 2 nodes) it did before. Counted, not timed,
        // so slow sanitizer / debug builds give the same answer.
        const float n = v->GetVariable("n").AsFloat();
        CHECK(n > 1000.0f && n < 20000.0f);
        CHECK(v->Graph() && v->Graph()->ErrorCount() > 0);
        CHECK_NEAR(v->GetVariable("x").AsFloat(), 201.0f, 1e-3f);   // the work it did do was right
        std::printf("  deep pull chain: %.0f iterations in %.2fs\n", n, secs);
    }

    // --- A big but finite loop with reads still finishes: reads don't eat the node-run budget ---
    {
        // Repeat 300000 x AddVar n <- Add(Const 1, Const 0): 300k node runs, 900k reads.
        // With one shared 1M budget this stopped as a "runaway loop" at ~250k.
        const std::string src =
            "node 1 OnStart\nnode 2 Repeat 300000\nnode 3 AddVar n\nnode 4 Add\nnode 5 Const 1\nnode 6 Const 0\n"
            "data 3 0 4 0\ndata 4 0 5 0\ndata 4 1 6 0\nexec 1 0 2\nexec 2 0 3\nentry OnStart 1\n";
        Scene sc;
        auto* v = sc.CreateGameObject("Grid")->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(v->LoadFromText(src, &err));
        auto sink = Log::sink; Log::sink = [](Log::Level, const std::string&) {};
        sc.Start();
        Log::sink = sink;
        CHECK_NEAR(v->GetVariable("n").AsFloat(), 300000.0f, 0.5f);
        CHECK(v->Graph() && v->Graph()->ErrorCount() == 0);
    }

    TEST_MAIN_RESULT();
}
