#include "test_framework.hpp"
#include "rig_harness.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <cmath>
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

    // ---- Model clips: crouch and prone play the model's own stance clips (the
    // model used to stay upright while the collider shrank, head through the
    // ceiling), crossfaded over the stance times, with the get-up from prone.
    {
        righarness::Rig r = righarness::PlayableFromFile("mixamo_t_stance_clips.glb");
        ModelAnimator* ma = nullptr;
        if (r) for (const auto& up : r.scene->Objects())
            if (up->IsSelfOrDescendantOf(r.model)) if ((ma = up->GetComponent<ModelAnimator>())) break;
        CHECK(ma != nullptr);
        Transform* hips = r.Bone(0);
        CHECK(hips != nullptr);
        if (ma && hips) {
            CHECK(ma->idleClip == "Idle" && ma->walkClip == "Walk" && ma->runClip == "Run");
            CHECK(ma->crouchClip == "Crouch Idle");
            CHECK(ma->crouchWalkClip == "Crouched Walking");
            CHECK(ma->proneClip == "Prone Idle");
            CHECK(ma->crawlClip == "Crawling");
            CHECK(ma->getUpClip == "Getting Up");
            CHECK(ma->jumpClip.empty() && ma->fallClip.empty() && ma->landClip.empty());   // "Standing To Crouched" is no state
            float last = 0.0f, worst = 0.0f;
            auto run = [&](std::vector<char> first, std::vector<char> keys, int frames) {
                for (int i = 0; i < frames; ++i) {
                    r.Step(dt, i == 0 ? first : keys);
                    float y = hips->Position().y;
                    worst = std::fmax(worst, std::fabs(y - last));
                    last = y;
                }
            };
            r.Step(dt, {}, 60);
            const float stand = last = hips->Position().y;
            run({Input::KeyCtrl}, {}, 40);
            CHECK(ma->StanceClips() == 1 && ma->CurrentName() == "Crouch Idle");
            CHECK(hips->Position().y < stand - 0.25f);
            run({'W'}, {'W'}, 40);
            CHECK(ma->CurrentName() == "Crouched Walking");
            run({'z'}, {}, 60);
            CHECK(ma->StanceClips() == 2 && ma->CurrentName() == "Prone Idle");
            CHECK(hips->Position().y < stand - 0.6f);
            run({'W'}, {'W'}, 40);
            CHECK(ma->CurrentName() == "Crawling");
            run({}, {}, 10);
            run({'z'}, {}, 3);
            CHECK(ma->CurrentName() == "Getting Up");
            run({}, {}, 90);
            CHECK(ma->StanceClips() == 0 && ma->CurrentName() == "Idle");
            CHECK(std::fabs(hips->Position().y - stand) < 0.05f);
            std::printf("  stance changes: biggest hips move %.3f m per frame\n", worst);
            CHECK(worst < 0.05f);   // eased, never snapped

            std::string text = SceneSerializer::Serialize(*r.scene);
            Scene s2("load");
            CHECK(SceneSerializer::Deserialize(s2, text));
            ModelAnimator* m2 = nullptr;
            for (const auto& up : s2.Objects()) if ((m2 = up->GetComponent<ModelAnimator>())) break;
            CHECK(m2 && m2->crouchClip == "Crouch Idle" && m2->crawlClip == "Crawling" && m2->getUpClip == "Getting Up");
        }
    }

    TEST_MAIN_RESULT();
}
