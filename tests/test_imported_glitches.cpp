#include "test_framework.hpp"
#include "rig_harness.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <cstdio>

using namespace okay;

// Imported characters glitching now and then, one check per cause.
int main() {
    RUN_SUITE("imported_glitches");
    const float dt = 1.0f / 60.0f;

    // ---- Humanoid template: foot IK moves to the model's legs, and the blocky
    // part rig it was wired to stays hidden after the scene is saved and loaded.
    {
        Scene s("hum");
        GameObject* player = Templates::AddHumanoidPlayer(s, {0, 0, 0});
        GameObject* model = AttachCharacterModel(s, player, righarness::RigPath("mixamo_t_noclips.glb"));
        CHECK(model != nullptr);
        auto* fik = player->GetComponent<FootIK>();
        CHECK(fik && fik->leftFoot && fik->leftFoot->gameObject->IsSelfOrDescendantOf(model));
        Scene s2("hum2");
        CHECK(SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s)));
        s2.Start();
        s2.Update(dt);
        int shown = 0;
        GameObject* rig = s2.Find("Rig");
        for (const auto& up : s2.Objects()) {
            bool live = true;
            for (Transform* t = up->transform; t; t = t->Parent()) live = live && t->gameObject->active;
            auto* mr = up->GetComponent<MeshRenderer>();
            if (live && mr && mr->enabled && rig && up->IsSelfOrDescendantOf(rig)) ++shown;
        }
        CHECK(shown == 0);   // was all 15 body parts
    }

    // ---- Set Character Model again: the new model replaces the old one.
    {
        righarness::Rig r = righarness::PlayableFromFile("mixamo_t_noclips.glb");
        CHECK(r.model != nullptr);
        if (r) {
            r.Step(dt, {'W'}, 30);
            GameObject* fresh = AttachCharacterModel(*r.scene, r.player, righarness::RigPath("ue_a_blender.glb"));
            r.Step(dt, {'W'}, 2);
            int models = 0;
            for (Transform* c : r.player->transform->Children())
                if (c->gameObject->name != "Rig" && !c->gameObject->GetComponent<Camera>()) ++models;
            CHECK(models == 1);
            CHECK(FindCharacterModelRoot(*r.scene, r.player) == fresh);
            auto* fik = r.player->GetComponent<FootIK>();
            CHECK(fik && fik->leftFoot && fik->leftFoot->gameObject->IsSelfOrDescendantOf(fresh));
        }
    }

    // ---- Retargeted model, idle -> crouch: the pose crossfades from the first
    // frame (it used to flash the crouch for a frame, then blend back from idle).
    {
        righarness::Rig r = righarness::PlayableFromFile("mixamo_t_noclips.glb");
        CHECK(r.model != nullptr);
        if (r) {
            if (auto* f = r.player->GetComponent<FootIK>()) f->weight = 0.0f;   // the pose alone
            r.Step(dt, {}, 30);
            Transform* knee = r.Bone(10);
            Mat4 toBody = r.player->transform->LocalToWorldMatrix().Inverse();
            Vec3 prev = toBody.MultiplyPoint(knee->Position());
            float worst = 0.0f;
            r.Step(dt, {Input::KeyCtrl});
            for (int i = 0; i < 30; ++i) {
                Vec3 k = toBody.MultiplyPoint(knee->Position());
                worst = std::fmax(worst, (k - prev).Magnitude());
                prev = k;
                r.Step(dt, {});
            }
            std::printf("  crouch: knee moves at most %.3f m a frame\n", worst);
            CHECK(worst < 0.1f);   // was 0.33
        }
    }

    // ---- Model clips at an uneven frame rate: walking stays walking.
    {
        righarness::Rig r = righarness::PlayableFromFile("mixamo_t_cm_clips.glb");
        CHECK(r.model != nullptr);
        ModelAnimator* ma = nullptr;
        if (r) for (const auto& up : r.scene->Objects())
            if (up->IsSelfOrDescendantOf(r.model)) if ((ma = up->GetComponent<ModelAnimator>())) break;
        CHECK(ma != nullptr);
        if (ma) {
            r.Step(dt, {'W'}, 60);
            std::string last = ma->CurrentName();
            int switches = 0;
            for (int i = 0; i < 120; ++i) {
                r.Step(i & 1 ? 1.0f / 144.0f : 1.0f / 30.0f, {'W'});
                if (ma->CurrentName() != last) { ++switches; last = ma->CurrentName(); }
            }
            std::printf("  uneven frames: %d clip switches while walking\n", switches);
            CHECK(switches == 0);   // was walk/run every frame
        }
    }

    // ---- Model clips: stepping up a curb isn't a jump.
    {
        righarness::Rig r = righarness::PlayableFromFile("mixamo_t_cm_clips.glb");
        ModelAnimator* ma = nullptr;
        if (r) for (const auto& up : r.scene->Objects())
            if (up->IsSelfOrDescendantOf(r.model)) if ((ma = up->GetComponent<ModelAnimator>())) break;
        CHECK(ma != nullptr && !ma->jumpClip.empty());
        if (ma) {
            for (int k = 0; k < 4; ++k) {
                GameObject* g = r.scene->CreateGameObject("Curb");
                g->transform->localPosition = {0, 0.05f, -2.0f - 2.0f * k};
                g->transform->localScale = {4, 0.1f, 1.0f};
                g->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
                g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
            }
            int jumpFrames = 0;
            for (int i = 0; i < 300; ++i) { r.Step(dt, {'W'}); jumpFrames += ma->CurrentName() == ma->jumpClip; }
            CHECK(jumpFrames == 0);   // was the jump clip at every curb
        }
    }

    TEST_MAIN_RESULT();
}
