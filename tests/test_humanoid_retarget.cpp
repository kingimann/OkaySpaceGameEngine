#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/HumanoidRetarget.hpp"
#include "okay/Components/Character.hpp"
#include <cmath>
#include <string>

using namespace okay;

// Built-in animations on imported rigs: bones are matched by name across the
// usual conventions, and look-alike helper bones (IK targets, twist bones,
// toes, fingers) never take a limb's place — whatever order the file lists
// them in.
int main() {
    RUN_SUITE("humanoid_retarget");

    {   // Name classification.
        int sc = 0;
        CHECK(HumanoidRetarget::ClassifyBone("pelvis", sc) == 0 && sc == 2);
        CHECK(HumanoidRetarget::ClassifyBone("mixamorig:Hips", sc) == 0);
        CHECK(HumanoidRetarget::ClassifyBone("spine_03", sc) == 1);
        CHECK(HumanoidRetarget::ClassifyBone("Head", sc) == 2);
        CHECK(HumanoidRetarget::ClassifyBone("upperarm_l", sc) == 3 && sc == 2);
        CHECK(HumanoidRetarget::ClassifyBone("lowerarm_r", sc) == 7);
        CHECK(HumanoidRetarget::ClassifyBone("hand_l", sc) == 5);
        CHECK(HumanoidRetarget::ClassifyBone("thigh_r", sc) == 12);
        CHECK(HumanoidRetarget::ClassifyBone("calf_l", sc) == 10);
        CHECK(HumanoidRetarget::ClassifyBone("foot_r", sc) == 14);
        CHECK(HumanoidRetarget::ClassifyBone("mixamorig:LeftUpLeg", sc) == 9);
        CHECK(HumanoidRetarget::ClassifyBone("mixamorig:RightForeArm", sc) == 7);
        CHECK(HumanoidRetarget::ClassifyBone("DEF-upper_arm.L", sc) == 3);
        CHECK(HumanoidRetarget::ClassifyBone("Armature|forearm.R", sc) == 7);
        // Look-alikes are rejected.
        for (const char* bad : {"ik_hand_l", "ik_foot_r", "upperarm_twist_01_l", "thigh_twist_01_r", "ball_l",
                                "thumb_01_l", "index_02_r", "clavicle_l", "neck_01", "HeadTop_End", "ik_hand_gun",
                                "mixamorig:LeftHandIndex1", "mixamorig:LeftToeBase", "hand_l_end"})
            CHECK(HumanoidRetarget::ClassifyBone(bad, sc) < 0);
    }

    {   // A UE5-mannequin style rig whose IK / twist helpers come FIRST in the file.
        Scene scene; scene.physicsEnabled = false;
        GameObject* player = scene.CreateGameObject("Player");
        player->AddComponent<Character>()->driveExternal = true;
        auto* rt = player->AddComponent<HumanoidRetarget>();
        GameObject* model = scene.CreateGameObject("Superhero");
        model->transform->SetParent(player->transform, false);
        auto mk = [&](const char* n, GameObject* parent, Vec3 p) {
            GameObject* g = scene.CreateGameObject(n);
            g->transform->SetParent(parent->transform, false);
            g->transform->localPosition = p;
            return g;
        };
        GameObject* arm = mk("Armature", model, {0, 0, 0});
        GameObject* root = mk("root", arm, {0, 0, 0});
        // decoys first
        GameObject* ikr = mk("ik_hand_root", root, {0, 0, 0});
        GameObject* ikh = mk("ik_hand_l", ikr, {0.6f, 1.4f, 0});
        GameObject* ikf = mk("ik_foot_l", root, {0.1f, 0.05f, 0});
        GameObject* pelvis = mk("pelvis", root, {0, 1.0f, 0});
        GameObject* s1 = mk("spine_01", pelvis, {0, 0.1f, 0});
        GameObject* s3 = mk("spine_03", s1, {0, 0.2f, 0});
        GameObject* neck = mk("neck_01", s3, {0, 0.2f, 0});
        mk("head", neck, {0, 0.1f, 0});
        GameObject* clav = mk("clavicle_l", s3, {0.05f, 0.15f, 0});
        GameObject* ua = mk("upperarm_l", clav, {0.1f, 0, 0});
        mk("upperarm_twist_01_l", ua, {0.1f, 0, 0});
        GameObject* la = mk("lowerarm_l", ua, {0.25f, 0, 0});
        GameObject* hand = mk("hand_l", la, {0.25f, 0, 0});
        mk("thumb_01_l", hand, {0.03f, 0, 0.02f});
        GameObject* clavr = mk("clavicle_r", s3, {-0.05f, 0.15f, 0});
        GameObject* uar = mk("upperarm_r", clavr, {-0.1f, 0, 0});
        GameObject* lar = mk("lowerarm_r", uar, {-0.25f, 0, 0});
        mk("hand_r", lar, {-0.25f, 0, 0});
        GameObject* th = mk("thigh_l", pelvis, {0.1f, -0.05f, 0});
        mk("thigh_twist_01_l", th, {0, -0.2f, 0});
        GameObject* calf = mk("calf_l", th, {0, -0.45f, 0});
        GameObject* foot = mk("foot_l", calf, {0, -0.45f, 0});
        mk("ball_l", foot, {0, -0.05f, 0.1f});
        GameObject* thr = mk("thigh_r", pelvis, {-0.1f, -0.05f, 0});
        GameObject* calfr = mk("calf_r", thr, {0, -0.45f, 0});
        mk("foot_r", calfr, {0, -0.45f, 0});
        (void)ikh; (void)ikf;

        CHECK(rt->DetectAndWire(scene, model) == 15);
        CHECK(rt->MappedNode(0) == pelvis);
        CHECK(rt->MappedNode(1) == s3);
        CHECK(rt->MappedNode(3) == ua);
        CHECK(rt->MappedNode(4) == la);
        CHECK(rt->MappedNode(5) == hand);
        CHECK(rt->MappedNode(9) == th);
        CHECK(rt->MappedNode(10) == calf);
        CHECK(rt->MappedNode(11) == foot);

        // The built-in walk actually moves the imported bones.
        auto* ch = player->GetComponent<Character>();
        ch->anim = 2;   // walk
        Quat before = th->transform->localRotation;
        scene.Start();
        for (int i = 0; i < 20; ++i) scene.Update(1.0f / 60.0f);
        Quat after = th->transform->localRotation;
        float dq = std::fabs(before.x - after.x) + std::fabs(before.y - after.y) + std::fabs(before.z - after.z) + std::fabs(before.w - after.w);
        CHECK(dq > 1e-3f);
        // Deleting the armature while the player lives must not crash.
        scene.Destroy(arm);
        for (int i = 0; i < 5; ++i) scene.Update(1.0f / 60.0f);
        CHECK(true);
    }
    TEST_MAIN_RESULT();
}
