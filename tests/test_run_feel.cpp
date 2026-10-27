#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace okay;

static float Horiz(const Rigidbody3D* rb) {
    return std::sqrt(rb->velocity.x * rb->velocity.x + rb->velocity.z * rb->velocity.z);
}

static float HeadingErr(const Rigidbody3D* rb, float wishX, float wishZ) {
    const float sp = Horiz(rb);
    if (sp < 0.2f) return 180.0f;
    const float dot = (rb->velocity.x * wishX + rb->velocity.z * wishZ) / sp;
    const float c = std::max(-1.0f, std::min(1.0f, dot));
    return std::acos(c) * 57.2957795f;
}

// Release-build model (v2.836): one acceleration for walk and run, no separate
// steer, hard stop. Printed so the measured curves have a before.
static void PrintBaseline() {
    const float dt = 1.0f / 60.0f;
    std::printf("baseline (accel 60, decel 55, air 0.40, turn 12, no steer split):\n");
    std::printf("  walk 0->90%% of 4.5: %.3f s\n", 4.05f / 60.0f);
    std::printf("  run  0->90%% of 8.0: %.3f s\n", 7.2f / 60.0f);
    std::printf("  stop from 8.0 to 0:  %.3f s (no tail)\n", 8.0f / 55.0f);
    std::printf("  180 deg at 8 m/s:    %.3f s\n", 16.0f / 60.0f);
    std::printf("  90 deg at 8 m/s:     %.3f s\n", (8.0f * std::sqrt(2.0f)) / 60.0f);
    std::printf("  90 deg in air:       %.3f s\n", (8.0f * std::sqrt(2.0f)) / (60.0f * 0.40f));
    std::printf("  body yaw 90->15 deg: %.3f s\n", std::log(90.0f / 15.0f) / 12.0f);
    std::printf("  dt=%.5f\n", dt);
}

struct Rig {
    Scene scene;
    GameObject* player = nullptr;
    Rigidbody3D* rb = nullptr;
    Character* ch = nullptr;
    Rig(const char* name, bool third) : scene(name) {
        GameObject* floor = scene.CreateGameObject("Floor");
        floor->transform->localPosition = {0, -0.5f, 0};
        floor->AddComponent<BoxCollider3D>()->size = {80, 1, 80};
        floor->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        player = scene.CreateGameObject("Player");
        player->transform->localPosition = {0, 0.2f, 0};
        ch = player->AddComponent<Character>();
        ch->Apply();
        rb = player->AddComponent<Rigidbody3D>();
        auto* box = player->AddComponent<BoxCollider3D>();
        box->size = {0.6f, 1.8f, 0.6f};
        box->offset = {0, 0.9f, 0};
        if (third) {
            auto* tp = player->AddComponent<ThirdPersonController>();
            tp->lookSmoothing = 0.0f;
            tp->orbitSmoothing = 0.0f;
            tp->autoRecenter = false;
            tp->lockCursor = false;
        } else {
            auto* fp = player->AddComponent<FirstPersonController>();
            fp->lookSmoothing = 0.0f;
            fp->lockCursor = false;
            GameObject* cam = scene.CreateGameObject("Eye");
            cam->AddComponent<Camera>();
            cam->transform->SetParent(player->transform, false);
            cam->transform->localPosition = {0, 1.6f, 0};
        }
        scene.Start();
        Input::FeedKeys({});
        for (int i = 0; i < 20; ++i) scene.Update(1.0f / 60.0f);
    }
};

static int FramesTo(Rig& r, const std::vector<char>& keys, float want, int cap = 90) {
    Input::FeedKeys(keys);
    for (int i = 1; i <= cap; ++i) {
        r.scene.Update(1.0f / 60.0f);
        if (Horiz(r.rb) >= want) return i;
    }
    return -1;
}

int main() {
    RUN_SUITE("run_feel");
    PrintBaseline();

    const float dt = 1.0f / 60.0f;
    const char shift = (char)Input::KeyShift;

    // Walk and run ramps, then a sprint stop with the soft tail.
    {
        Rig fp("fp", false);
        const int walkF = FramesTo(fp, {'w'}, 4.5f * 0.90f);
        std::printf("fp walk to 90%%: %d frames (%.3f s) speed %.2f\n", walkF, walkF * dt, Horiz(fp.rb));
        CHECK(walkF > 0 && walkF < 20);

        Input::FeedKeys({});
        for (int i = 0; i < 40; ++i) fp.scene.Update(dt);
        const int runF = FramesTo(fp, {'w', shift}, 8.0f * 0.90f);
        std::printf("fp run to 90%%: %d frames (%.3f s) speed %.2f\n", runF, runF * dt, Horiz(fp.rb));
        CHECK(runF > 0 && runF < 20);

        std::vector<float> stop;
        stop.push_back(Horiz(fp.rb));
        Input::FeedKeys({});
        for (int i = 0; i < 60; ++i) {
            fp.scene.Update(dt);
            stop.push_back(Horiz(fp.rb));
            if (stop.back() < 0.02f) break;
        }
        std::printf("fp stop:");
        for (float v : stop) std::printf(" %.2f", v);
        std::printf("\n");
        CHECK(stop.front() > 6.0f);
        CHECK(stop.back() < 0.05f);
        int at22 = -1, atRest = (int)stop.size() - 1;
        for (int i = 0; i < (int)stop.size(); ++i) if (at22 < 0 && stop[i] <= 2.2f) at22 = i;
        std::printf("  frames to 2.2 m/s: %d, frames to rest: %d\n", at22, atRest);
        CHECK(at22 > 0);
        CHECK(atRest > at22 + at22);   // the tail is longer than the hard part
    }

    // A 90° and a 180° at a sprint, on the ground and in the air.
    auto turn = [&](bool third, const char* name, std::vector<char> from, std::vector<char> to,
                    float wishX, float wishZ, bool jump) {
        Rig r(name, third);
        Input::FeedKeys(from);
        for (int i = 0; i < 40; ++i) r.scene.Update(dt);
        CHECK(Horiz(r.rb) > 6.0f);
        if (jump) {
            std::vector<char> up = from;
            up.push_back(' ');
            Input::FeedKeys(up);
            bool air = false;
            for (int i = 0; i < 20; ++i) {
                r.scene.Update(dt);
                if (r.rb->velocity.y > 1.0f) { air = true; break; }
            }
            CHECK(air);
        }
        Input::FeedKeys(to);
        int frames = -1;
        float last = 180.0f;
        for (int i = 1; i <= 40; ++i) {
            r.scene.Update(dt);
            last = HeadingErr(r.rb, wishX, wishZ);
            if (last < 15.0f && Horiz(r.rb) > 3.0f) { frames = i; break; }
        }
        std::printf("%s heading <15 deg: %d frames (err %.1f speed %.2f)\n",
                    name, frames, last, Horiz(r.rb));
        CHECK(frames > 0 && frames <= 12);
        return frames;
    };
    turn(false, "fp 90", {'w', shift}, {'d', shift}, 1, 0, false);
    turn(false, "fp 180", {'w', shift}, {'s', shift}, 0, 1, false);
    turn(true,  "tp 90", {'w', shift}, {'d', shift}, 1, 0, false);
    turn(false, "fp air 90", {'w', shift}, {'d', shift}, 1, 0, true);

    // Body yaw follows a sprint turn without a second smoother.
    {
        Rig tp("yaw", true);
        Input::FeedKeys({'w', shift});
        for (int i = 0; i < 30; ++i) tp.scene.Update(dt);
        Input::FeedKeys({'d', shift});
        int frames = -1;
        for (int i = 1; i <= 30; ++i) {
            tp.scene.Update(dt);
            Vec3 f = tp.player->transform->localRotation * Vec3{0, 0, -1};
            const float err = std::acos(std::max(-1.0f, std::min(1.0f, f.x))) * 57.2957795f;
            if (err < 15.0f) { frames = i; break; }
        }
        std::printf("tp body yaw to +X: %d frames\n", frames);
        CHECK(frames > 0 && frames <= 10);
    }

    // Local camera: sprint hides arms, sleeves and the held item. Third person
    // still shows them. Walking shows the first-person arm again.
    auto armHidden = [](Character* ch) {
        for (int i = HumanoidMotion::B_LUPARM; i <= HumanoidMotion::B_RHAND; ++i)
            if (!ch->Part(i) || ch->Part(i)->firstPersonViewmodel) return false;
        return true;
    };
    {
        Scene sc("view");
        GameObject* floor = sc.CreateGameObject("Floor");
        floor->transform->localPosition = {0, -0.5f, 0};
        floor->AddComponent<BoxCollider3D>()->size = {80, 1, 80};
        floor->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
        GameObject* p = sc.CreateGameObject("Player");
        p->transform->localPosition = {0, 0.2f, 0};
        auto* ch = p->AddComponent<Character>(); ch->Apply();
        p->AddComponent<Rigidbody3D>();
        auto* box = p->AddComponent<BoxCollider3D>();
        box->size = {0.6f, 1.8f, 0.6f}; box->offset = {0, 0.9f, 0};
        auto* fp = p->AddComponent<FirstPersonController>();
        fp->lookSmoothing = 0; fp->lockCursor = false;
        auto* pv = p->AddComponent<PlayerView>();
        pv->allowToggle = true; pv->defaultView = PlayerView::View::First; pv->blendTime = 0.01f;
        GameObject* cam = sc.CreateGameObject("Eye");
        auto* camera = cam->AddComponent<Camera>();
        cam->transform->SetParent(p->transform, false);
        cam->transform->localPosition = {0, 1.6f, 0};
        GameObject* item = sc.CreateGameObject("Crate");
        item->transform->localPosition = {1.2f, 1.0f, 0};
        sc.Start();
        p->AddComponent<ProceduralObjectHolding>()->Pickup(item);
        Input::FeedKeys({});
        for (int i = 0; i < 15; ++i) sc.Update(dt);

        Input::FeedKeys({'w', shift});
        bool ran = false;
        for (int i = 0; i < 50; ++i) {
            sc.Update(dt);
            if (ch->SprintingPose() && ch->PartsBuilt()) { ran = true; break; }
        }
        CHECK(ran);
        CHECK(armHidden(ch));
        CHECK(ch->Part(HumanoidMotion::B_TORSO) && ch->Part(HumanoidMotion::B_TORSO)->firstPersonViewmodel);
        CHECK(ch->Part(HumanoidMotion::B_HEAD) && !ch->Part(HumanoidMotion::B_HEAD)->firstPersonViewmodel);
        CHECK(camera->ignoreObject == p);
        CHECK(item->ownerCameraSkip == p);
        std::printf("fp sprint arms hidden, item skipped by owner camera\n");

        pv->SetView(1);
        for (int i = 0; i < 20; ++i) sc.Update(dt);
        CHECK(pv->GetView() == 1);
        CHECK(camera->ignoreObject == nullptr);
        CHECK(item->ownerCameraSkip == nullptr);
        CHECK(armHidden(ch));   // not viewmodels: the third-person camera draws them with the body
        std::printf("tp sprint arms drawn (ignore cleared)\n");

        pv->SetView(0);
        Input::FeedKeys({'w'});
        for (int i = 0; i < 40; ++i) sc.Update(dt);
        CHECK(!ch->SprintingPose());
        CHECK(ch->Part(HumanoidMotion::B_LUPARM)->firstPersonViewmodel);
        CHECK(item->ownerCameraSkip == nullptr);
        std::printf("fp walk arms visible again\n");
    }

    // Both scene templates, and the switchable third-person starter.
    {
        Scene fps("FPS");
        Templates::FPS(fps);
        GameObject* p = fps.Find("Player");
        auto* ch = p->GetComponent<Character>();
        auto* camGo = fps.Find("FPS Camera");
        Camera* cam = camGo ? camGo->GetComponent<Camera>() : nullptr;
        CHECK(p && ch && cam);
        fps.Start();
        Input::FeedKeys({'w', shift});
        bool hid = false;
        for (int i = 0; i < 70; ++i) {
            fps.Update(dt);
            if (ch->SprintingPose() && ch->PartsBuilt() && armHidden(ch) && cam->ignoreObject == p) {
                hid = true; break;
            }
        }
        CHECK(hid);
        std::printf("template FPS sprint hides arms\n");
    }
    {
        Scene tp("TP");
        Templates::ThirdPerson(tp);
        tp.Start();
        GameObject* p = tp.Find("Player");
        auto* ch = p ? p->GetComponent<Character>() : nullptr;
        auto* pv = p ? p->GetComponent<PlayerView>() : nullptr;
        Camera* cam = tp.mainCamera;
        CHECK(p && ch && pv && cam);
        if (p && ch && pv && cam) {
        pv->SetView(0);
        Input::FeedKeys({'w', shift});
        bool hid = false;
        for (int i = 0; i < 80; ++i) {
            tp.Update(dt);
            if (pv->FirstAmount() > 0.8f && ch->SprintingPose() && ch->PartsBuilt() &&
                armHidden(ch) && cam->ignoreObject == p) { hid = true; break; }
        }
        CHECK(hid);
        std::printf("template Third Person (switched to first) sprint hides arms\n");

        pv->SetView(1);
        for (int i = 0; i < 30; ++i) tp.Update(dt);
        CHECK(cam->ignoreObject == nullptr);
        std::printf("template Third Person orbit keeps the body\n");
        }
    }

    TEST_MAIN_RESULT();
}
