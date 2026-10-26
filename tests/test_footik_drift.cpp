#include "test_framework.hpp"
#include "rig_harness.hpp"
#include <cstdio>

using namespace okay;

// Foot IK on an imported rig driven by HumanoidRetarget (rotations only):
// walking 5 s over 10 cm curbs must keep the feet on the ground and the body
// upright. Foot IK used to read "the pose wasn't rewritten" off WORLD
// positions, so once the player moved it took its own last shift for animation
// and added it again every frame: the first curb sank the hips a metre into the
// floor. (On flat ground nothing is corrected, so nothing drifted.)
int main() {
    RUN_SUITE("footik_drift");

    for (const char* file : {"mixamo_t_noclips.glb", "ue_a_blender.glb"}) {
        righarness::Rig r = righarness::PlayableFromFile(file);
        CHECK(r.model != nullptr);
        if (!r) continue;
        for (int k = 0; k < 8; ++k) {   // curbs across the path (walking -Z)
            GameObject* g = r.scene->CreateGameObject("Curb");
            g->transform->localPosition = {0, 0.05f, -3.0f - 2.5f * k};
            g->transform->localScale = {4, 0.1f, 1.2f};
            g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
            g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        }
        auto* fik = r.player->GetComponent<FootIK>();
        CHECK(fik != nullptr && fik->weight > 0.0f);
        CHECK(r.player->GetComponent<HumanoidRetarget>() != nullptr);
        r.Step(1.0f / 60.0f, {}, 30);
        float lo = 1e9f, hi = -1e9f, top = 1e9f, kneeBack = 0.0f, shinOff = 0.0f;
        Transform *hip = r.Bone(9), *knee = r.Bone(10), *foot = r.Bone(11);
        CHECK(hip && knee && foot);
        if (!hip || !knee || !foot) continue;
        const Vec3 kneeRest = knee->localPosition;
        Vec3 start = r.player->transform->Position();
        for (int i = 0; i < 300; ++i) {
            r.Step(1.0f / 60.0f, {'W'});
            lo = std::fmin(lo, r.FeetY());
            hi = std::fmax(hi, r.FeetY());
            top = std::fmin(top, r.TopY());
            // The knee sits ahead of the hip-foot line (the body faces -Z), and
            // stays where the thigh bone puts it, or the skinned leg tears.
            Mat4 toBody = r.player->transform->LocalToWorldMatrix().Inverse();
            Vec3 h = toBody.MultiplyPoint(hip->Position()), k = toBody.MultiplyPoint(knee->Position()),
                 f = toBody.MultiplyPoint(foot->Position());
            Vec3 ax = (f - h).Normalized();
            Vec3 bend = (k - h) - ax * Vec3::Dot(k - h, ax);
            kneeBack = std::fmax(kneeBack, bend.z);
            shinOff = std::fmax(shinOff, (knee->localPosition - kneeRest).Magnitude() / kneeRest.Magnitude());
        }
        float moved = (r.player->transform->Position() - start).Magnitude();
        std::printf("  %s: feet %.3f..%.3f, lowest top %.3f, moved %.2f m, knee back %.3f, knee off its bone %.1f%%\n",
                    file, lo, hi, top, moved, kneeBack, shinOff * 100.0f);
        CHECK(moved > 3.0f);
        CHECK(lo > -0.08f);
        CHECK(hi < 0.35f + 0.1f);   // + the curb height
        CHECK(top > 1.5f);
        CHECK(kneeBack < 0.02f);    // folded backwards: up to 0.24 m
        CHECK(shinOff < 0.02f);     // knee moved off the thigh: up to 50-60 deg
    }

    TEST_MAIN_RESULT();
}
