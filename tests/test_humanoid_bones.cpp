#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Animation/HumanoidBones.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Render/ModelImport.hpp"
#include <string>

using namespace okay;
namespace hb = okay::humanoid;

// The shared humanoid bone mapper: one mapping for the retarget, Foot IK, Full
// Body IK and the importer, and rigs that only have humanoid-looking names (a
// dog, a robot arm, mixed-up sides) are turned away.
namespace {
struct Builder {
    Scene& scene;
    GameObject* root;
    explicit Builder(Scene& s) : scene(s), root(s.CreateGameObject("Model")) {}
    GameObject* mk(const char* n, GameObject* parent, Vec3 p) {
        GameObject* g = scene.CreateGameObject(n);
        g->transform->SetParent((parent ? parent : root)->transform, false);
        g->transform->localPosition = p;
        return g;
    }
};

// A Mixamo-style biped, Y up. `swapArms` mislabels the arms' sides.
GameObject* Mixamo(Scene& s, bool swapArms = false) {
    Builder b(s);
    GameObject* hips = b.mk("mixamorig:Hips", nullptr, {0, 1.0f, 0});
    GameObject* s0 = b.mk("mixamorig:Spine", hips, {0, 0.1f, 0});
    GameObject* s1 = b.mk("mixamorig:Spine1", s0, {0, 0.12f, 0});
    GameObject* s2 = b.mk("mixamorig:Spine2", s1, {0, 0.12f, 0});
    GameObject* neck = b.mk("mixamorig:Neck", s2, {0, 0.15f, 0});
    b.mk("mixamorig:Head", neck, {0, 0.1f, 0});
    const char* la[3] = {"mixamorig:LeftArm", "mixamorig:LeftForeArm", "mixamorig:LeftHand"};
    const char* ra[3] = {"mixamorig:RightArm", "mixamorig:RightForeArm", "mixamorig:RightHand"};
    for (int side = 0; side < 2; ++side) {
        const float x = side ? -1.0f : 1.0f;
        const char* const* n = (side == 0) != swapArms ? la : ra;
        GameObject* sh = b.mk(side ? "mixamorig:RightShoulder" : "mixamorig:LeftShoulder", s2, {0.05f * x, 0.1f, 0});
        GameObject* u = b.mk(n[0], sh, {0.1f * x, 0, 0});
        GameObject* f = b.mk(n[1], u, {0.25f * x, 0, 0});
        GameObject* h = b.mk(n[2], f, {0.25f * x, 0, 0});
        b.mk(side ? "mixamorig:RightHandIndex1" : "mixamorig:LeftHandIndex1", h, {0.05f * x, 0, 0});
        GameObject* t = b.mk(side ? "mixamorig:RightUpLeg" : "mixamorig:LeftUpLeg", hips, {0.1f * x, -0.05f, 0});
        GameObject* k = b.mk(side ? "mixamorig:RightLeg" : "mixamorig:LeftLeg", t, {0, -0.45f, 0});
        GameObject* ft = b.mk(side ? "mixamorig:RightFoot" : "mixamorig:LeftFoot", k, {0, -0.45f, 0});
        b.mk(side ? "mixamorig:RightToeBase" : "mixamorig:LeftToeBase", ft, {0, -0.05f, 0.1f});
    }
    return b.root;
}
} // namespace

int main() {
    RUN_SUITE("humanoid_bones");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- A Mixamo biped maps fully, with its whole spine chain ----
    {
        Scene s; s.physicsEnabled = false;
        GameObject* model = Mixamo(s);
        const hb::BoneMap m = hb::MapHumanoidBones(model);
        CHECK(m.humanoid && m.legsOk && m.why.empty());
        CHECK(m.found == 15);
        CHECK(m.bone[hb::Hips]->name == "mixamorig:Hips");
        CHECK(m.bone[hb::Torso]->name == "mixamorig:Spine2");   // the link the arms hang from
        CHECK(m.spine.size() == 3 && m.spine.front()->name == "mixamorig:Spine");
        CHECK(m.neck && m.neck->name == "mixamorig:Neck");
        CHECK(m.bone[hb::LHand]->name == "mixamorig:LeftHand");   // not the index finger
        CHECK(m.bone[hb::RFoot]->name == "mixamorig:RightFoot");  // not the toe
        CHECK(CountHumanoidBones(s, model) == 15);

        auto* fb = model->AddComponent<FullBodyIK>();
        CHECK(fb->MapBones(true) == FullBodyIK::BoneCount);
        CHECK(fb->bones[FullBodyIK::Spine]->gameObject->name == "mixamorig:Spine");
        CHECK(fb->bones[FullBodyIK::Chest]->gameObject->name == "mixamorig:Spine2");
        CHECK(fb->bones[FullBodyIK::Neck]->gameObject->name == "mixamorig:Neck");
        CHECK(fb->MapProblem().empty());

        FootIK fik;
        CHECK(fik.DetectHumanoidBones(s, model) == 6);
        CHECK(fik.leftHipName == "mixamorig:LeftUpLeg" && fik.rightFootName == "mixamorig:RightFoot");
        CHECK(fik.pelvisName == "mixamorig:Hips");
    }

    // ---- Left and right that disagree between the arms and the legs ----
    {
        Scene s; s.physicsEnabled = false;
        GameObject* model = Mixamo(s, /*swapArms=*/true);
        const hb::BoneMap m = hb::MapHumanoidBones(model);
        CHECK(!m.humanoid && m.legsOk);
        CHECK(m.why.find("sides") != std::string::npos);
        CHECK(CountHumanoidBones(s, model) == 0);
        std::string log;
        CHECK(EnableBuiltinAnimations(s, model, &log) < 8);
        CHECK(log.find("isn't a humanoid") != std::string::npos);
    }

    // ---- A four-legged rig named like legs ----
    {
        Scene s; s.physicsEnabled = false;
        Builder b(s);
        GameObject* pelvis = b.mk("pelvis", nullptr, {0, 0.6f, -0.4f});
        GameObject* spine = b.mk("spine_01", pelvis, {0, 0, 0.4f});
        b.mk("head", spine, {0, 0.2f, 0.4f});
        for (int side = 0; side < 2; ++side) {
            const float x = side ? -0.15f : 0.15f;
            const char* nm[2][3] = {{"front_thigh_l", "front_calf_l", "front_foot_l"}, {"back_thigh_l", "back_calf_l", "back_foot_l"}};
            const char* nr[2][3] = {{"front_thigh_r", "front_calf_r", "front_foot_r"}, {"back_thigh_r", "back_calf_r", "back_foot_r"}};
            for (int leg = 0; leg < 2; ++leg) {
                const char* const* n = side ? nr[leg] : nm[leg];
                GameObject* t = b.mk(n[0], leg ? pelvis : spine, {x, -0.1f, 0});
                GameObject* c = b.mk(n[1], t, {0, -0.25f, 0});
                b.mk(n[2], c, {0, -0.25f, 0});
            }
        }
        const hb::BoneMap m = hb::MapHumanoidBones(b.root);
        CHECK(!m.humanoid && !m.legsOk);
        CHECK(m.why.find("four-legged") != std::string::npos);
        FootIK fik;
        std::string why;
        CHECK(fik.DetectHumanoidBones(s, b.root, &why) == 0 && !why.empty());
        CHECK(fik.leftHipName.empty());   // nothing half-mapped
        auto* fb = b.root->AddComponent<FullBodyIK>();
        CHECK(fb->MapBones(true) == 0 && !fb->MapProblem().empty());
    }

    // ---- A robot arm with humanoid arm names and no legs ----
    {
        Scene s; s.physicsEnabled = false;
        Builder b(s);
        GameObject* base = b.mk("base", nullptr, {0, 0, 0});
        GameObject* u = b.mk("upperarm_l", base, {0, 0.3f, 0});
        GameObject* f = b.mk("lowerarm_l", u, {0, 0.4f, 0});
        b.mk("hand_l", f, {0, 0.3f, 0});
        const hb::BoneMap m = hb::MapHumanoidBones(b.root);
        CHECK(!m.humanoid && !m.legsOk);
        CHECK(m.why.find("legs") != std::string::npos);
    }

    // ---- Legs whose bones aren't a chain (all children of the hips) ----
    {
        Scene s; s.physicsEnabled = false;
        Builder b(s);
        GameObject* hips = b.mk("Hips", nullptr, {0, 1, 0});
        b.mk("Head", hips, {0, 0.6f, 0});
        for (const char* n : {"thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r"})
            b.mk(n, hips, {n[std::string(n).size() - 1] == 'l' ? 0.1f : -0.1f, -0.5f, 0});
        const hb::BoneMap m = hb::MapHumanoidBones(b.root);
        CHECK(!m.legsOk && m.why.find("chains") != std::string::npos);
    }

    // ---- Feet above the hips (an upside-down or non-humanoid layout) ----
    {
        Scene s; s.physicsEnabled = false;
        Builder b(s);
        GameObject* hips = b.mk("Hips", nullptr, {0, 1, 0});
        b.mk("Head", hips, {0, 0.6f, 0});
        for (int side = 0; side < 2; ++side) {
            const float x = side ? -0.1f : 0.1f;
            GameObject* t = b.mk(side ? "thigh_r" : "thigh_l", hips, {x, 0.1f, 0});
            GameObject* c = b.mk(side ? "calf_r" : "calf_l", t, {0, 0.4f, 0});
            b.mk(side ? "foot_r" : "foot_l", c, {0, 0.4f, 0});
        }
        const hb::BoneMap m = hb::MapHumanoidBones(b.root);
        CHECK(!m.legsOk && m.why.find("below") != std::string::npos);
    }

    // ---- Names: the shared classifier, and the old entry points agree ----
    {
        int sc = 0;
        CHECK(hb::ClassifyBone("hip.L", sc) == hb::LThigh);   // Blender's sided "hip" is the thigh
        CHECK(hb::ClassifyBone("Hip", sc) == hb::Hips);
        CHECK(hb::ClassifyBone("mixamorig:LeftToeBase", sc) < 0);
        CHECK(hb::IsNeckName("neck_01") && !hb::IsNeckName("spine_01"));
        CHECK(FullBodyIK::Classify("neck_01") == FullBodyIK::Neck);
        CHECK(FullBodyIK::Classify("spine_01") == FullBodyIK::Chest);   // as before
        CHECK(FullBodyIK::Classify("spine") == FullBodyIK::Spine);
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
