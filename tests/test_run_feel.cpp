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
        // Still holding: the copy is the hand. The real arm stays off this camera.
        CHECK(ch->fpArmUp > 0.5f);
        CHECK(!ch->Part(HumanoidMotion::B_LUPARM)->firstPersonViewmodel);
        CHECK(item->ownerCameraSkip == p);
        std::printf("fp walk shows the held copy, not the real arm\n");
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

    // Template first person and third person switched with V. Same controllers,
    // same eye camera, same hand. The numbers below are the settled first-person
    // view from each entry, after the same inputs.
    {
        Scene fps("fp-entry");
        Scene tps("tp-entry");
        Templates::FPS(fps);
        Templates::ThirdPerson(tps);
        GameObject* a = fps.Find("Player");
        GameObject* b = tps.Find("Player");
        auto* af = a->GetComponent<FirstPersonController>();
        auto* bf = b->GetComponent<FirstPersonController>();
        auto* at = a->GetComponent<ThirdPersonController>();
        auto* bt = b->GetComponent<ThirdPersonController>();
        auto* av = a->GetComponent<PlayerView>();
        auto* bv = b->GetComponent<PlayerView>();
        GameObject* aCam = fps.Find("FPS Camera");
        GameObject* bCam = tps.Find("Main Camera");
        auto* aHand = a->GetComponent<FirstPersonHands>();
        auto* bHand = b->GetComponent<FirstPersonHands>();
        CHECK(af && bf && at && bt && av && bv && aCam && bCam && aHand && bHand);
        CHECK(aCam->GetComponent<FirstPersonHand>() == nullptr);
        CHECK(bCam->GetComponent<FirstPersonHand>() == nullptr);
        CHECK(aCam->transform->Parent() == a->transform);
        CHECK(bCam->transform->Parent() == b->transform);
        fps.Start();
        tps.Start();
        Camera* aC = aCam->GetComponent<Camera>();
        Camera* bC = tps.mainCamera ? tps.mainCamera : bCam->GetComponent<Camera>();
        CHECK(aC && bC);

        auto step = [&](const std::vector<char>& keys, int n) {
            for (int i = 0; i < n; ++i) {
                Input::FeedKeys(keys);
                fps.Update(dt);
                tps.Update(dt);
            }
        };
        auto yawOf = [](const Quat& q) {
            Vec3 f = q * Vec3{0, 0, -1};
            return std::atan2(f.x, f.z) * 57.2957795f;
        };
        auto ang = [](float a, float b) {
            float d = std::fmod(a - b + 540.0f, 360.0f) - 180.0f;
            return std::fabs(d);
        };

        step({}, 50);
        CHECK(bC->ignoreObject == nullptr);                 // third person still shows the body
        CHECK(bv->GetView() == 1);
        bv->SetView(0);
        step({}, 50);
        CHECK(!bv->Blending());
        CHECK(bv->GetView() == 0);
        CHECK(av->GetView() == 0);

        const float eyeA = aCam->transform->localPosition.y;
        const float eyeB = bCam->transform->localPosition.y;
        std::printf("rest eye  template %.3f  switched %.3f  (want 1.60)\n", eyeA, eyeB);
        std::printf("rest pitch template %.2f  switched %.2f  (want 0, level)\n", af->pitch, bf->pitch);
        std::printf("rest fov   template %.2f  switched %.2f\n", aC->fieldOfView, bC->fieldOfView);
        std::printf("sens       template %.3f  switched %.3f\n", af->mouseSensitivity, bf->mouseSensitivity);
        CHECK_NEAR(eyeA, 1.60f, 0.03f);
        CHECK_NEAR(eyeB, 1.60f, 0.03f);
        CHECK_NEAR(eyeA, eyeB, 0.02f);
        CHECK_NEAR(af->pitch, 0.0f, 0.5f);
        CHECK_NEAR(bf->pitch, 0.0f, 0.5f);
        CHECK_NEAR(aC->fieldOfView, bC->fieldOfView, 0.4f);
        CHECK_NEAR(aC->fieldOfView, 60.0f, 1.0f);
        CHECK_NEAR(af->mouseSensitivity, 0.15f, 1e-4f);
        CHECK_NEAR(bf->mouseSensitivity, 0.15f, 1e-4f);
        CHECK(aC->ignoreObject == a);
        CHECK(bC->ignoreObject == b);

        Input::FeedKeys({});
        Input::FeedMouse({0, 0}, 0);
        step({}, 1);
        const float yawBeforeA = af->yaw, yawBeforeB = bf->yaw;
        Input::FeedMouse({100, 0}, 0);
        step({}, 1);
        const float dYawA = af->yaw - yawBeforeA, dYawB = bf->yaw - yawBeforeB;
        std::printf("mouse 100px yaw  template %.2f  switched %.2f  (want -15, sens 0.15)\n", dYawA, dYawB);
        CHECK_NEAR(dYawA, -15.0f, 0.05f);
        CHECK_NEAR(dYawB, -15.0f, 0.05f);
        CHECK_NEAR(at->yaw, 0.0f, 0.05f);                    // the orbit did not eat the look
        CHECK_NEAR(bt->yaw, 0.0f, 0.05f);
        const float lagA = ang(yawOf(a->transform->Rotation()), yawOf(aCam->transform->Rotation()));
        const float lagB = ang(yawOf(b->transform->Rotation()), yawOf(bCam->transform->Rotation()));
        std::printf("body/camera yaw lag  template %.2f  switched %.2f deg\n", lagA, lagB);
        CHECK(lagA < 2.0f);
        CHECK(lagB < 2.0f);

        // The two templates put crates in different places. Plant both on the
        // open ground at the origin so the comparison is the controller.
        auto plant = [&](unsigned buttons) {
            Input::FeedMouse({0, 0}, buttons);
            step({}, 1);                                 // absorb the pointer warp
            a->transform->SetPosition({0, 0.2f, 0});
            b->transform->SetPosition({0, 0.2f, 0});
            af->yaw = bf->yaw = 0.0f;
            af->pitch = bf->pitch = 0.0f;
            if (auto* rb = a->GetComponent<Rigidbody3D>()) rb->velocity = Vec3{0, 0, 0};
            if (auto* rb = b->GetComponent<Rigidbody3D>()) rb->velocity = Vec3{0, 0, 0};
            step({}, 24);
        };
        plant(0);

        float bobA = 0.0f, bobB = 0.0f;
        for (int i = 0; i < 40; ++i) {
            Input::FeedKeys({'w'});
            fps.Update(dt);
            tps.Update(dt);
            if (i >= 20) {
                bobA = std::max(bobA, std::fabs(aCam->transform->localPosition.y - 1.60f));
                bobB = std::max(bobB, std::fabs(bCam->transform->localPosition.y - 1.60f));
            }
        }
        const float spA = Horiz(a->GetComponent<Rigidbody3D>());
        const float spB = Horiz(b->GetComponent<Rigidbody3D>());
        std::printf("walk speed template %.2f  switched %.2f\n", spA, spB);
        std::printf("walk bob   template %.3f  switched %.3f\n", bobA, bobB);
        CHECK_NEAR(spA, spB, 0.08f);
        CHECK(spA > 3.0f);
        CHECK(bobA > 0.008f);
        CHECK(bobB > 0.008f);
        CHECK_NEAR(bobA, bobB, 0.012f);
        auto* aCh = a->GetComponent<Character>();
        auto* bCh = b->GetComponent<Character>();
        // Empty hands draw nothing. The shirt stays, so a look down is a solid shoulder.
        CHECK(!aCh->Part(HumanoidMotion::B_LUPARM)->firstPersonViewmodel);
        CHECK(!bCh->Part(HumanoidMotion::B_LUPARM)->firstPersonViewmodel);
        CHECK(aCh->fpArmUp <= 0.05f);
        CHECK(bCh->fpArmUp <= 0.05f);
        CHECK(aCh->Part(HumanoidMotion::B_TORSO)->firstPersonViewmodel);
        CHECK(bCh->Part(HumanoidMotion::B_TORSO)->firstPersonViewmodel);

        step({}, 15);
        step({(char)Input::KeyCtrl}, 1);
        step({}, 40);
        std::printf("crouch eye template %.3f  switched %.3f  (want 1.20)\n",
                    aCam->transform->localPosition.y, bCam->transform->localPosition.y);
        CHECK(af->stance() == FirstPersonController::Stance::Crouch);
        CHECK(bf->stance() == FirstPersonController::Stance::Crouch);
        CHECK_NEAR(aCam->transform->localPosition.y, 1.20f, 0.05f);
        CHECK_NEAR(bCam->transform->localPosition.y, 1.20f, 0.05f);
        step({(char)Input::KeyCtrl}, 1);
        step({}, 30);
        CHECK(af->stance() == FirstPersonController::Stance::Stand);
        CHECK(bf->stance() == FirstPersonController::Stance::Stand);

        step({'z'}, 1);
        step({}, 80);   // stand -> prone is 0.75 s, then the eye eases the rest of the way
        std::printf("prone eye  template %.3f  switched %.3f  (want 0.40)\n",
                    aCam->transform->localPosition.y, bCam->transform->localPosition.y);
        CHECK(af->stance() == FirstPersonController::Stance::Prone);
        CHECK(bf->stance() == FirstPersonController::Stance::Prone);
        CHECK_NEAR(aCam->transform->localPosition.y, 0.40f, 0.06f);
        CHECK_NEAR(bCam->transform->localPosition.y, 0.40f, 0.06f);
        step({'z'}, 1);
        step({}, 30);

        step({'q'}, 25);
        std::printf("lean x     template %.3f  switched %.3f  (want -0.35)\n",
                    aCam->transform->localPosition.x, bCam->transform->localPosition.x);
        CHECK_NEAR(aCam->transform->localPosition.x, -0.35f, 0.06f);
        CHECK_NEAR(bCam->transform->localPosition.x, -0.35f, 0.06f);
        step({}, 20);
        plant(0);

        auto giveItem = [](Scene& sc, GameObject* p) {
            GameObject* item = sc.CreateGameObject("Held");
            item->transform->localPosition = p->transform->Position() + Vec3{0.4f, 0.2f, 0.4f};
            item->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
            item->AddComponent<BoxCollider3D>();
            item->AddComponent<Rigidbody3D>();
            // The template already holds with one component. A second one would
            // take the pickup while the hands still read the empty first.
            auto* hold = p->GetComponent<ProceduralObjectHolding>();
            if (!hold) hold = p->AddComponent<ProceduralObjectHolding>();
            hold->Pickup(item);
            return item;
        };
        GameObject* itemA = giveItem(fps, a);
        GameObject* itemB = giveItem(tps, b);
        step({'w', shift}, 40);
        std::printf("sprint fov template %.2f  switched %.2f  (boost +8 -> ~68)\n",
                    aC->fieldOfView, bC->fieldOfView);
        std::printf("sprint arms hidden template %d switched %d\n",
                    armHidden(aCh) ? 1 : 0, armHidden(bCh) ? 1 : 0);
        CHECK(aCh->SprintingPose());
        CHECK(bCh->SprintingPose());
        CHECK(aC->fieldOfView > 64.0f);
        CHECK(bC->fieldOfView > 64.0f);
        CHECK_NEAR(aC->fieldOfView, bC->fieldOfView, 0.6f);
        CHECK(armHidden(aCh));
        CHECK(armHidden(bCh));
        CHECK(aC->ignoreObject == a);
        CHECK(bC->ignoreObject == b);
        CHECK(itemA->ownerCameraSkip == a);
        CHECK(itemB->ownerCameraSkip == b);
        CHECK(!aCh->Part(HumanoidMotion::B_HEAD)->firstPersonViewmodel);
        CHECK(!bCh->Part(HumanoidMotion::B_HEAD)->firstPersonViewmodel);

        aHand->showWhileRunning = true;
        bHand->showWhileRunning = true;
        step({'w', shift}, 20);
        CHECK(aCh->SprintingPose());
        CHECK(bCh->SprintingPose());
        std::printf("panel show-while-running copy template %.2f switched %.2f\n",
                    aCh->fpArmUp, bCh->fpArmUp);
        CHECK(aCh->fpArmUp > 0.5f);
        CHECK(bCh->fpArmUp > 0.5f);
        CHECK(!aCh->Part(HumanoidMotion::B_LUPARM)->firstPersonViewmodel);
        CHECK(!bCh->Part(HumanoidMotion::B_LUPARM)->firstPersonViewmodel);
        CHECK(itemA->ownerCameraSkip == a);
        CHECK(itemB->ownerCameraSkip == b);

        plant(1u << 1);
        step({'w', shift}, 30);
        const float zoomSpA = Horiz(a->GetComponent<Rigidbody3D>());
        const float zoomSpB = Horiz(b->GetComponent<Rigidbody3D>());
        std::printf("rmb+sprint speed template %.2f switched %.2f  fov %.1f %.1f (zoom 40, sprint kept)\n",
                    zoomSpA, zoomSpB, aC->fieldOfView, bC->fieldOfView);
        CHECK(zoomSpA > 6.5f);
        CHECK(zoomSpB > 6.5f);
        CHECK(aC->fieldOfView < 45.0f);
        CHECK(bC->fieldOfView < 45.0f);
        const float z0A = af->yaw, z0B = bf->yaw;
        Input::FeedMouse({80, 0}, 1u << 1);
        step({'w', shift}, 1);
        const float zDA = af->yaw - z0A, zDB = bf->yaw - z0B;
        std::printf("zoomed 80px yaw template %.2f switched %.2f (want -6.6)\n", zDA, zDB);
        CHECK_NEAR(zDA, -6.6f, 0.4f);
        CHECK_NEAR(zDB, -6.6f, 0.4f);

        Input::FeedMouse({80, 0}, 0);
        av->SetView(1);
        bv->SetView(1);
        step({}, 45);
        const float orbitA = (aCam->transform->Position() - a->transform->Position()).Magnitude();
        const float orbitB = (bCam->transform->Position() - b->transform->Position()).Magnitude();
        std::printf("back to orbit dist template %.2f switched %.2f\n", orbitA, orbitB);
        CHECK(orbitA > 3.5f);
        CHECK(orbitB > 3.5f);
        CHECK_NEAR(orbitA, orbitB, 0.35f);
        CHECK(aC->ignoreObject == nullptr);
        CHECK(bC->ignoreObject == nullptr);
        CHECK(!aCh->firstPersonArm);
        CHECK(!bCh->firstPersonArm);
        Input::FeedKeys({});
    }

    TEST_MAIN_RESULT();
}
