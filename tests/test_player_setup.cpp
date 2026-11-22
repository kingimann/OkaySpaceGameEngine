// Wire animation and IK: adds only what is missing, and a second press
// leaves weights, chains and a custom state graph alone.
#include "test_framework.hpp"
#include "rig_harness.hpp"
#include "okay/Animation/PlayerSetup.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Components/LedgeHandsIK.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Animation/HumanoidBones.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Render/Mesh.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneTemplates.hpp"

using namespace okay;

int main() {
    RUN_SUITE("player_setup");

    // A player with no model is told what to do, and gains no IK.
    {
        Scene bare;
        GameObject* player = Templates::AddThirdPersonPlayer(bare, {0, 1, 0});
        CHECK(player != nullptr);
        PlayerAnimStatus st = InspectPlayerAnimation(bare, player);
        CHECK(!st.hasModel);
        CHECK(!player->GetComponent<FootIK>());
        CHECK(!player->GetComponent<LookAtIK>());
        const int anims = (int)player->GetComponents<AnimStateMachine>().size();
        std::string msg = WirePlayerAnimation(bare, player);
        CHECK(msg.find("No character model") != std::string::npos);
        CHECK(!player->GetComponent<FootIK>());
        CHECK(!player->GetComponent<LookAtIK>());
        CHECK((int)player->GetComponents<AnimStateMachine>().size() == anims);
    }

    // A mesh that is not a humanoid is reported, and does not grow foot or look IK.
    {
        Scene box;
        GameObject* player = Templates::AddThirdPersonPlayer(box, {0, 1, 0});
        GameObject* prop = box.CreateGameObject("Crate");
        prop->transform->SetParent(player->transform, false);
        auto* mr = prop->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube(1.0f);
        PlayerAnimStatus st = InspectPlayerAnimation(box, player);
        CHECK(st.hasModel);
        CHECK(st.bones < 8);
        std::string msg = WirePlayerAnimation(box, player);
        CHECK(msg.find("Not enough humanoid bones") != std::string::npos);
        CHECK(!player->GetComponent<FootIK>());
        CHECK(!player->GetComponent<LookAtIK>());
    }

    // A Mixamo rig already wired by Make Playable stays that way, including a
    // weight the user changed and a state they added.
    {
        Scene scene;
        scene.physicsEnabled = false;
        GameObject* player = MakePlayableCharacter(scene, righarness::RigPath("mixamo_t_noclips.glb"), nullptr);
        CHECK(player != nullptr);
        if (!player) TEST_MAIN_RESULT();
        FootIK* foot = player->GetComponent<FootIK>();
        LookAtIK* look = player->GetComponent<LookAtIK>();
        CHECK(foot && look);
        if (!foot || !look) TEST_MAIN_RESULT();
        foot->weight = 0.25f;
        look->weight = 0.4f;
        const std::string chain = look->chainNames.empty() ? std::string() : look->chainNames.back();
        AnimStateMachine* sm = nullptr;
        for (AnimStateMachine* m : player->GetComponents<AnimStateMachine>())
            if (m->layer == 0) sm = m;
        CHECK(sm != nullptr);
        if (!sm) TEST_MAIN_RESULT();
        const int states = (int)sm->states.size();
        sm->states.push_back({});
        sm->states.back().name = "CustomIdle";
        const int feet = (int)player->GetComponents<FootIK>().size();
        const int looks = (int)player->GetComponents<LookAtIK>().size();

        std::string once = WirePlayerAnimation(scene, player);
        CHECK(once.find("Left") != std::string::npos);
        CHECK(player->GetComponent<FootIK>() == foot);
        CHECK(player->GetComponent<LookAtIK>() == look);
        CHECK(foot->weight == 0.25f);
        CHECK(look->weight == 0.4f);
        CHECK(look->chainNames.empty() || look->chainNames.back() == chain);
        CHECK((int)player->GetComponents<FootIK>().size() == feet);
        CHECK((int)player->GetComponents<LookAtIK>().size() == looks);
        bool kept = false;
        for (const auto& s : sm->states) if (s.name == "CustomIdle") kept = true;
        CHECK(kept);
        CHECK((int)sm->states.size() == states + 1);

        std::string twice = WirePlayerAnimation(scene, player);
        CHECK(twice.find("Added") == std::string::npos);
        CHECK(foot->weight == 0.25f);
        CHECK(look->weight == 0.4f);
        CHECK((int)player->GetComponents<FootIK>().size() == feet);
    }

    // Ledge grab on: the step adds Ledge Hands IK and links it to the two hand LimbIKs
    // (reusing one already on a hand). Ledge grab off: nothing is added.
    {
        Scene scene;
        scene.physicsEnabled = false;
        GameObject* player = MakePlayableCharacter(scene, righarness::RigPath("mixamo_t_noclips.glb"), nullptr);
        CHECK(player != nullptr);
        if (!player) TEST_MAIN_RESULT();
        auto* tp = player->GetComponent<ThirdPersonController>();
        CHECK(tp != nullptr);
        if (!tp) TEST_MAIN_RESULT();
        tp->canLedgeGrab = false;
        WirePlayerAnimation(scene, player);
        CHECK(!player->GetComponent<LedgeHandsIK>());

        GameObject* model = FindCharacterModelRoot(scene, player);
        const humanoid::BoneMap bm = humanoid::MapHumanoidBones(model);
        GameObject* lHand = bm.Get(humanoid::LHand);
        GameObject* rHand = bm.Get(humanoid::RHand);
        CHECK(lHand && rHand);
        if (!lHand || !rHand) TEST_MAIN_RESULT();
        // A LimbIK the user already put on the left hand (for holding things).
        auto* mine = player->AddComponent<LimbIK>();
        mine->upper = bm.Get(humanoid::LUpperArm)->transform; mine->lower = bm.Get(humanoid::LForeArm)->transform;
        mine->end = lHand->transform; mine->weight = 0.6f;

        tp->canLedgeGrab = true;
        std::string msg = WirePlayerAnimation(scene, player);
        CHECK(msg.find("Ledge Hands IK") != std::string::npos);
        auto* lh = player->GetComponent<LedgeHandsIK>();
        CHECK(lh != nullptr);
        if (!lh) TEST_MAIN_RESULT();
        CHECK(lh->leftArm == mine);
        CHECK(mine->weight == 0.6f);
        CHECK(lh->rightArm != nullptr && lh->rightArm != mine);
        CHECK(lh->rightArm && lh->rightArm->end == rHand->transform);
        // The added arm names its bones too: a scene file keeps LimbIK bones by name.
        CHECK(lh->rightArm && lh->rightArm->endName == rHand->name && !lh->rightArm->upperName.empty());
        const int limbs = (int)player->GetComponents<LimbIK>().size();
        CHECK(limbs == 2);

        std::string again = WirePlayerAnimation(scene, player);
        CHECK((int)player->GetComponents<LedgeHandsIK>().size() == 1);
        CHECK((int)player->GetComponents<LimbIK>().size() == limbs);
        CHECK(player->GetComponent<LedgeHandsIK>() == lh);
    }

    TEST_MAIN_RESULT();
}
