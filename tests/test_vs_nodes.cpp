#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
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
    TEST_MAIN_RESULT();
}
