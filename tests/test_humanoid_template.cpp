#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// The one-click humanoid: rig + capsule + camera + foot IK + root motion, wired.
int main() {
    RUN_SUITE("humanoid_template");

    Scene s("H");
    GameObject* player = Templates::AddHumanoidPlayer(s, {0, 1, 0});
    CHECK(player != nullptr);

    auto* pc = player->GetComponent<Character>();
    CHECK(pc != nullptr);
    CHECK(pc->PartsBuilt());                       // the bone rig was built for IK wiring

    // Physics: a capsule + body.
    CHECK(player->GetComponent<Rigidbody3D>() != nullptr);
    CHECK(player->GetComponent<CapsuleCollider3D>() != nullptr);

    // A controller and a camera came along.
    CHECK(player->GetComponent<ThirdPersonController>() != nullptr);
    bool hasCam = false;
    for (const auto& go : s.Objects()) if (go->GetComponent<Camera>()) hasCam = true;
    CHECK(hasCam);

    // Foot IK is wired to the actual leg bones (L thigh/shin/foot = 9/10/11, R = 12/13/14).
    auto* fik = player->GetComponent<FootIK>();
    CHECK(fik != nullptr);
    CHECK(fik->leftHip   == pc->Part(9)->transform);
    CHECK(fik->leftKnee  == pc->Part(10)->transform);
    CHECK(fik->leftFoot  == pc->Part(11)->transform);
    CHECK(fik->rightHip  == pc->Part(12)->transform);
    CHECK(fik->rightKnee == pc->Part(13)->transform);
    CHECK(fik->rightFoot == pc->Part(14)->transform);

    // Root motion is wired to the hips, left Disabled (procedural anims bob the hips).
    auto* rm = player->GetComponent<RootMotion>();
    CHECK(rm != nullptr);
    CHECK(rm->rootNode == pc->Part(0)->transform);
    CHECK(rm->mode == (int)RootMotion::Mode::Disabled);

    // It runs without blowing up and stays upright (rotation frozen by default).
    s.Start();
    for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
    CHECK(player->transform->Position().y > -1.0f);   // didn't fall through the world

    TEST_MAIN_RESULT();
}
