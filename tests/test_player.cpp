#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

using namespace okay;

int main() {
    RUN_SUITE("player");

    // --- Third-person controller drives the Character animation from movement ---
    {
        Scene sc("s");
        GameObject* p = sc.CreateGameObject("Player");
        auto* ch = p->AddComponent<Character>();
        auto* tp = p->AddComponent<ThirdPersonController>();
        ch->anim = 0;

        Input::FeedKeys({});            // no input -> idle
        tp->Update(0.016f);
        CHECK(ch->anim == 1);

        Input::FeedKeys({'w'});         // forward -> walk
        tp->Update(0.016f);
        CHECK(ch->anim == 2);

        tp->sprintKey = 'r';            // run while moving + sprint held
        Input::FeedKeys({'w', 'r'});
        tp->Update(0.016f);
        CHECK(ch->anim == 3);
    }

    // --- Third-person camera spring arm: a wall pulls the camera in (no clipping) ---
    {
        Scene sc("cam");
        GameObject* cam = sc.CreateGameObject("Camera");
        cam->AddComponent<Camera>();                         // becomes the main camera

        GameObject* p = sc.CreateGameObject("Player");
        auto* tp = p->AddComponent<ThirdPersonController>();
        tp->yaw = 0.0f; tp->pitch = 0.0f;                    // camera straight behind (+Z)
        tp->distance = 6.0f; tp->cameraHeight = 1.5f;
        tp->pivotDampingXZ = tp->pivotDampingY = 0.0f;       // instant, for a clean check
        sc.Start();                                          // Camera::Awake registers mainCamera

        // Baseline: no obstacle -> camera sits a full `distance` behind the head.
        tp->LateUpdate(0.016f);
        Vec3 head{0, 1.5f, 0};
        float farDist = (cam->transform->Position() - head).Magnitude();
        CHECK_NEAR(farDist, 6.0f, 0.3f);

        // Put a wall between the head and the camera (around z = +3).
        GameObject* wall = sc.CreateGameObject("Wall");
        wall->transform->localPosition = {0, 1.5f, 3.0f};
        wall->AddComponent<BoxCollider3D>()->size = {4, 4, 0.5f};

        tp->LateUpdate(0.016f);
        float nearDist = (cam->transform->Position() - head).Magnitude();
        CHECK(nearDist < farDist);                           // pulled in by the wall
        CHECK(nearDist < 3.2f);                              // sits at ~the wall, not past it
    }

    // --- First-person controller: same animation driving, and it moves ---
    {
        Scene sc("s");
        GameObject* p = sc.CreateGameObject("Player");
        auto* ch = p->AddComponent<Character>();
        auto* fp = p->AddComponent<FirstPersonController>();
        ch->anim = 0;

        Vec3 start = p->transform->localPosition;
        Input::FeedKeys({'w'});
        fp->Update(0.1f);
        CHECK(ch->anim == 2);                                   // walking
        CHECK((p->transform->localPosition - start).Magnitude() > 0.01f);  // moved
    }

    // --- Premade-player shortcuts drop a fully wired player into a scene ---
    {
        Scene sc("tp");
        GameObject* p = Templates::AddThirdPersonPlayer(sc);
        CHECK(p != nullptr);
        CHECK(p->GetComponent<ThirdPersonController>() != nullptr);
        CHECK(p->GetComponent<Character>() != nullptr);
        CHECK(p->GetComponent<PlayerView>() != nullptr);
        CHECK_NEAR(p->GetComponent<PlayerView>()->blendTime, 0.30f, 1e-4f);
        CHECK(p->GetComponent<PlayerView>()->switchEase == PlayerView::SwitchEase::Smooth);
        CHECK(!p->GetComponent<PlayerView>()->snapSwitch);
        CHECK(p->GetComponent<Rigidbody3D>() != nullptr);
        CHECK(p->GetComponent<BoxCollider3D>() != nullptr);
        // A camera exists in the scene (created since there was none).
        CHECK(Templates::EnsureMainCamera(sc) != nullptr);
    }
    {
        Scene sc("fp");
        GameObject* p = Templates::AddFirstPersonPlayer(sc);
        CHECK(p && p->GetComponent<FirstPersonController>() != nullptr);
        CHECK(p->GetComponent<PlayerView>() != nullptr);
        CHECK_NEAR(p->GetComponent<PlayerView>()->blendTime, 0.30f, 1e-4f);
        CHECK(!p->GetComponent<PlayerView>()->snapSwitch);
        // The camera is a child of the player (eye-mounted).
        GameObject* camChild = sc.Find("Player Camera");
        CHECK(camChild && camChild->transform->Parent() == p->transform);
        CHECK(camChild && camChild->GetComponent<Camera>() != nullptr);
    }
    {
        Scene sc("ctm");
        GameObject* p = Templates::AddClickToMovePlayer(sc);
        CHECK(p && p->GetComponent<ClickToMoveController>() != nullptr);
    }
    {
        Scene sc("td");
        GameObject* p = Templates::AddTopDownPlayer(sc);
        CHECK(p && p->GetComponent<TopDownController>() != nullptr);
    }

    // --- Transform-only player (no Rigidbody3D) is stopped by a wall (no clipping) ---
    {
        Scene sc("noclip"); sc.physicsEnabled = false;
        GameObject* wall = sc.CreateGameObject("Wall");
        wall->transform->localPosition = {1.5f, 0.9f, 0.0f};
        wall->AddComponent<BoxCollider3D>()->size = {1, 2, 1};   // AABB x in [1,2]

        GameObject* p = sc.CreateGameObject("Player");           // no rigidbody, no collider
        auto* td = p->AddComponent<TopDownController>();
        td->walkSpeed = 6.0f;

        Input::FeedKeys({'d'});                                  // walk +X toward the wall
        for (int i = 0; i < 120; ++i) td->Update(1.0f / 60.0f);

        float px = p->transform->localPosition.x;
        CHECK(px > 0.1f);          // it did move toward the wall
        CHECK(px < 0.7f);          // but stopped at the wall (~1.0 - 0.4 radius), not past it
    }

    // --- Third-person shooter: premade wiring, cursor lock on Start, and fire ---
    {
        Scene sc("tps"); sc.physicsEnabled = false;
        GameObject* p = Templates::AddThirdPersonShooterPlayer(sc);
        auto* ts = p->GetComponent<ThirdPersonShooterController>();
        CHECK(ts != nullptr);
        Cursor::Capture(false);                          // start unlocked
        sc.Start();                                      // controller's Start locks it
        CHECK(Cursor::IsLocked());
        CHECK(!Cursor::visible);

        // Round-trip the shooter through the scene.
        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(sc));
        auto* ts2 = s2.Find("Player") ? s2.Find("Player")->GetComponent<ThirdPersonShooterController>() : nullptr;
        CHECK(ts2 != nullptr);
        if (ts2) { CHECK(ts2->lockCursor); CHECK_NEAR(ts2->aimDistance, ts->aimDistance, 1e-4f); }
    }

    // --- Third-person shooter SCENE template builds a playable scene ---
    {
        Scene sc("tpl");
        Templates::ThirdPersonShooter(sc);
        GameObject* p = sc.Find("Player");
        CHECK(p != nullptr);
        CHECK(p && p->GetComponent<ThirdPersonShooterController>() != nullptr);
        CHECK(p && p->GetComponent<Rigidbody3D>() != nullptr);
        CHECK(sc.Find("Main Camera") != nullptr);
        CHECK(sc.Find("Target") != nullptr);                 // shootable targets exist
        CHECK(sc.Find("Ground") != nullptr);
    }

    // --- Top-down controller moves world-relative and drives walk/run animation ---
    {
        Scene sc("tdmove"); sc.physicsEnabled = false;
        GameObject* p = sc.CreateGameObject("Player");
        auto* ch = p->AddComponent<Character>();
        auto* td = p->AddComponent<TopDownController>();
        td->acceleration = 1000.0f;                  // effectively instant for the check
        ch->anim = 0;

        Input::FeedKeys({});
        td->Update(0.016f);
        CHECK(ch->anim == 1);                         // idle

        Vec3 start = p->transform->localPosition;
        Input::FeedKeys({'w'});                       // forward = -Z (into the screen)
        td->Update(0.1f);
        CHECK(ch->anim == 2);                         // walking
        CHECK((p->transform->localPosition - start).Magnitude() > 0.01f);

        td->sprintKey = 'r';
        Input::FeedKeys({'w', 'r'});
        td->Update(0.05f);
        CHECK(ch->anim == 3);                         // running
    }

    // --- View toggle off: the third-person orbit is the controller's own camera ---
    {
        Scene sc("viewoff"); sc.physicsEnabled = false;
        GameObject* cam = sc.CreateGameObject("Camera");
        cam->AddComponent<Camera>();
        GameObject* p = sc.CreateGameObject("Player");
        auto* tp = p->AddComponent<ThirdPersonController>();
        tp->yaw = 0.0f; tp->pitch = 0.0f; tp->distance = 5.0f; tp->cameraHeight = 1.5f;
        tp->pivotDampingXZ = tp->pivotDampingY = 0.0f; tp->orbitSmoothing = 0.0f; tp->lookSmoothing = 0.0f;
        tp->autoRecenter = false;
        auto* pv = p->AddComponent<PlayerView>();
        pv->allowToggle = false; pv->defaultView = PlayerView::View::Third;
        pv->thirdPersonDistance = -1.0f; pv->shoulderOffset = 0.0f;
        sc.Start();
        Input::FeedKeys({}); sc.Update(1.0f / 60.0f);
        const Vec3 head{0, 1.5f, 0};
        const float farDist = (cam->transform->Position() - head).Magnitude();
        CHECK_NEAR(farDist, 5.0f, 0.35f);
        CHECK(pv->GetView() == 1);
        Input::FeedKeys({'v'}); sc.Update(1.0f / 60.0f);   // toggle is off: the key does nothing
        CHECK_NEAR((cam->transform->Position() - head).Magnitude(), farDist, 0.05f);
        CHECK(pv->GetView() == 1);
        Input::FeedKeys({});
    }

    // --- Smooth, interruptible blend from the orbit to the eyes and back ---
    {
        Scene sc("blend"); sc.physicsEnabled = false;
        GameObject* cam = sc.CreateGameObject("Camera");
        cam->AddComponent<Camera>();
        GameObject* p = sc.CreateGameObject("Player");
        auto* ch = p->AddComponent<Character>(); ch->Apply();
        auto* tp = p->AddComponent<ThirdPersonController>();
        tp->yaw = 0.0f; tp->pitch = 0.0f; tp->distance = 5.0f; tp->cameraHeight = 1.5f;
        tp->pivotDampingXZ = tp->pivotDampingY = 0.0f; tp->orbitSmoothing = 0.0f; tp->lookSmoothing = 0.0f;
        tp->autoRecenter = false;
        auto* pv = p->AddComponent<PlayerView>();
        pv->allowToggle = true; pv->defaultView = PlayerView::View::Third;
        pv->thirdPersonDistance = -1.0f; pv->shoulderOffset = 0.0f; pv->blendTime = 0.35f;
        sc.Start();
        Input::FeedKeys({}); sc.Update(1.0f / 60.0f);
        const Vec3 head{0, 1.5f, 0};
        const float farDist = (cam->transform->Position() - head).Magnitude();
        CHECK_NEAR(farDist, 5.0f, 0.35f);                 // settled third person matches the controller
        CHECK(!ch->hideOwnHead);

        pv->SetView(0);
        float prev = farDist; bool smooth = true; float mid = farDist;
        for (int i = 0; i < 30; ++i) {
            Input::FeedKeys({});
            sc.Update(1.0f / 60.0f);
            const float d = (cam->transform->Position() - head).Magnitude();
            // The eye settles a little in front of the head centre, so the last
            // step can move away from this reference. A snap is still a fail.
            if ((d > prev + 0.05f && prev > 0.40f) || (prev - d) > 0.55f) smooth = false;
            prev = d;
            if (i == 8) mid = d;
        }
        CHECK(smooth);
        CHECK(mid < farDist - 0.5f && mid > 0.3f);        // part-way, not snapped to either end
        CHECK((cam->transform->Position() - head).Magnitude() < 0.45f);   // arrived at the eyes
        CHECK(pv->GetView() == 0);
        CHECK(!pv->Blending());
        CHECK(ch->PartsBuilt());
        CHECK(ch->Part(2) && ch->Part(2)->active && !ch->Part(2)->firstPersonViewmodel);   // head stays for everyone else
        CHECK(ch->Part(2)->transform && (ch->Part(2)->transform->localScale - Vec3::One).Magnitude() < 1e-3f);
        if (auto* hmr = ch->Part(2)->GetComponent<MeshRenderer>()) CHECK(hmr->castShadows && hmr->enabled);
        CHECK(!ch->hideOwnHead);
        CHECK(ch->firstPersonArm);                            // same arm a native first-person hand raises
        CHECK(ch->fpArmBase == 3);
        CHECK(cam->GetComponent<Camera>()->ignoreObject == p);
        CHECK(cam->GetComponent<Camera>()->nearClip < 0.06f);
        const Vec3 arrived = cam->transform->Position() - p->transform->Position();
        CHECK_NEAR(arrived.y, 1.6f, 0.08f);                   // eye socket, not the orbit look-at
        CHECK(arrived.z < -0.18f);                            // in front of the face, collar behind the lens

        pv->SetView(1);
        for (int i = 0; i < 10; ++i) { Input::FeedKeys({}); sc.Update(1.0f / 60.0f); }
        const float half = (cam->transform->Position() - head).Magnitude();
        CHECK(pv->Blending());
        CHECK(half > 0.8f && half < 4.5f);
        pv->SetView(0);                                   // reverse before it arrives
        float back = half;
        for (int i = 0; i < 8; ++i) { Input::FeedKeys({}); sc.Update(1.0f / 60.0f); back = (cam->transform->Position() - head).Magnitude(); }
        CHECK(back < half - 0.15f);

        Input::FeedKeys({}); sc.Update(1.0f / 60.0f);
        Input::FeedKeys({'v'}); sc.Update(1.0f / 60.0f);  // V flips the requested view
        CHECK(pv->GetView() == 1);
        Input::FeedKeys({});                                 // release V before the pad edge
        Input::FeedGamepad({0, 0}, 0); sc.Update(1.0f / 60.0f);
        Input::FeedKeys({});
        Input::FeedGamepad({0, 0}, 1u << 4); sc.Update(1.0f / 60.0f);   // Back flips it again
        CHECK(pv->GetView() == 0);
        Input::FeedKeys({}); Input::FeedGamepad({0, 0}, 0);
    }

    // --- Third to first and back, several times: the eye, the body, then all of it restored ---
    {
        Scene sc("toggle"); sc.physicsEnabled = false;
        GameObject* cam = sc.CreateGameObject("Camera");
        auto* camera = cam->AddComponent<Camera>();
        const float nearBefore = camera->nearClip;
        GameObject* p = sc.CreateGameObject("Player");
        auto* ch = p->AddComponent<Character>(); ch->Apply();
        auto* tp = p->AddComponent<ThirdPersonController>();
        tp->yaw = 0.0f; tp->pitch = 0.0f; tp->distance = 5.0f; tp->cameraHeight = 1.5f;
        tp->pivotDampingXZ = tp->pivotDampingY = 0.0f; tp->orbitSmoothing = 0.0f; tp->lookSmoothing = 0.0f;
        tp->autoRecenter = false;
        auto* pv = p->AddComponent<PlayerView>();
        pv->allowToggle = true; pv->defaultView = PlayerView::View::Third;
        pv->thirdPersonDistance = -1.0f; pv->shoulderOffset = 0.45f; pv->blendTime = 0.05f;
        sc.Start();
        Input::FeedKeys({}); sc.Update(1.0f / 60.0f);
        auto neckOf = [&]() -> GameObject* {
            GameObject* torso = ch->Part(1);
            if (!torso || !torso->transform) return nullptr;
            for (Transform* c : torso->transform->Children())
                if (c && c->gameObject && c->gameObject->name == "Neck") return c->gameObject;
            return nullptr;
        };
        for (int cycle = 0; cycle < 3; ++cycle) {
            pv->SetView(0);
            for (int i = 0; i < 20; ++i) { Input::FeedKeys({}); sc.Update(1.0f / 60.0f); }
            CHECK(!pv->Blending());
            CHECK(pv->GetView() == 0);
            CHECK(ch->PartsBuilt());
            CHECK(ch->firstPersonArm);
            CHECK(ch->fpArmBase == 3);
            CHECK(ch->fpArmUp == 0.0f);   // empty hands draw nothing until a hold or Show When Empty
            CHECK(ch->Part(2) && ch->Part(2)->active && !ch->Part(2)->firstPersonViewmodel);
            CHECK(ch->Part(2)->transform && (ch->Part(2)->transform->localScale - Vec3::One).Magnitude() < 1e-3f);
            if (auto* hmr = ch->Part(2)->GetComponent<MeshRenderer>()) CHECK(hmr->castShadows);
            GameObject* neck = neckOf();
            CHECK(neck && neck->active && !neck->firstPersonViewmodel);
            if (neck) if (auto* nmr = neck->GetComponent<MeshRenderer>()) CHECK(nmr->castShadows);
            CHECK(ch->Part(1) && ch->Part(1)->active && ch->Part(1)->firstPersonViewmodel); // shirt lid stays in this camera
            CHECK(camera->ignoreObject == p);
            CHECK(camera->nearClip < 0.06f);
            Vec3 eye = cam->transform->Position() - p->transform->Position();
            CHECK_NEAR(eye.y, 1.6f, 0.08f);
            CHECK(eye.z < -0.18f);

            tp->pitch = 80.0f;   // looking down: the eye steps forward and up, out of the chest
            for (int i = 0; i < 5; ++i) { Input::FeedKeys({}); sc.Update(1.0f / 60.0f); }
            eye = cam->transform->Position() - p->transform->Position();
            CHECK(eye.y > 1.70f);
            CHECK(eye.z < -0.28f);
            tp->pitch = 0.0f;
            for (int i = 0; i < 3; ++i) { Input::FeedKeys({}); sc.Update(1.0f / 60.0f); }

            tp->driveAnimation = false;
            ch->anim = 3;
            sc.Update(1.0f / 60.0f);
            CHECK(ch->HideFirstPersonHands());
            CHECK(ch->fpArmUp == 0.0f);
            CHECK(ch->Part(3) && !ch->Part(3)->firstPersonViewmodel);
            CHECK(ch->Part(5) && !ch->Part(5)->firstPersonViewmodel);
            CHECK(ch->Part(6) && !ch->Part(6)->firstPersonViewmodel);
            CHECK(ch->Part(1) && ch->Part(1)->active && ch->Part(1)->firstPersonViewmodel);
            ch->anim = 1;
            tp->driveAnimation = true;
            sc.Update(1.0f / 60.0f);

            pv->SetView(1);
            for (int i = 0; i < 20; ++i) { Input::FeedKeys({}); sc.Update(1.0f / 60.0f); }
            CHECK(!pv->Blending());
            CHECK(pv->GetView() == 1);
            CHECK(!ch->firstPersonArm);
            CHECK(ch->fpArmBase == -1);
            CHECK(ch->Part(2) && ch->Part(2)->active);
            neck = neckOf();
            CHECK(neck && neck->active);
            CHECK(camera->ignoreObject == nullptr);
            CHECK_NEAR(camera->nearClip, nearBefore, 1e-4f);
            const float back = (cam->transform->Position() - Vec3{0, 1.5f, 0}).Magnitude();
            CHECK_NEAR(back, 5.0f, 0.45f);
        }
        Input::FeedKeys({});
    }

    // --- Halfway to the eyes the head is still drawn. It drops out once the lens is within a head of the skull. ---
    {
        Scene sc("half"); sc.physicsEnabled = false;
        GameObject* cam = sc.CreateGameObject("Camera");
        auto* camera = cam->AddComponent<Camera>();
        GameObject* p = sc.CreateGameObject("Player");
        auto* ch = p->AddComponent<Character>(); ch->Apply();
        auto* tp = p->AddComponent<ThirdPersonController>();
        tp->yaw = 0.0f; tp->pitch = 0.0f; tp->distance = 5.0f; tp->cameraHeight = 1.5f;
        tp->pivotDampingXZ = tp->pivotDampingY = 0.0f; tp->orbitSmoothing = 0.0f; tp->lookSmoothing = 0.0f;
        tp->autoRecenter = false;
        auto* pv = p->AddComponent<PlayerView>();
        pv->allowToggle = true; pv->defaultView = PlayerView::View::Third;
        pv->thirdPersonDistance = -1.0f; pv->shoulderOffset = 0.0f; pv->blendTime = 0.50f;
        sc.Start();
        Input::FeedKeys({}); sc.Update(1.0f / 60.0f);
        pv->SetView(0);
        for (int i = 0; i < 15; ++i) { Input::FeedKeys({}); sc.Update(1.0f / 60.0f); } // past the rig build, not yet at the face
        CHECK(pv->Blending());
        const Vec3 rel = cam->transform->Position() - p->transform->Position();
        CHECK(rel.z > -0.16f);
        CHECK(camera->ignoreObject == nullptr);
        CHECK(ch->PartsBuilt());
        CHECK(ch->Part(2) && ch->Part(2)->active && !ch->Part(2)->firstPersonViewmodel);
        Input::FeedKeys({});
    }

    // --- First person on its own camera, then out to an orbit ---
    {
        Scene sc("fpblend"); sc.physicsEnabled = false;
        GameObject* p = sc.CreateGameObject("Player");
        p->AddComponent<Character>()->Apply();
        auto* fp = p->AddComponent<FirstPersonController>();
        fp->lookSmoothing = 0.0f;
        GameObject* camObj = sc.CreateGameObject("Eye");
        camObj->AddComponent<Camera>();
        camObj->transform->SetParent(p->transform, false);
        camObj->transform->localPosition = {0, 1.6f, 0};
        auto* pv = p->AddComponent<PlayerView>();
        pv->allowToggle = true; pv->defaultView = PlayerView::View::First;
        pv->thirdPersonDistance = 4.5f; pv->shoulderOffset = 0.0f; pv->blendTime = 0.30f;
        sc.Start();
        Input::FeedKeys({}); sc.Update(1.0f / 60.0f);
        CHECK_NEAR(camObj->transform->localPosition.y, 1.6f, 0.05f);   // still the controller's eye
        pv->SetView(1);
        for (int i = 0; i < 40; ++i) { Input::FeedKeys({}); sc.Update(1.0f / 60.0f); }
        const float out = (camObj->transform->Position() - p->transform->Position()).Magnitude();
        CHECK(out > 3.0f);                                // pulled back to the orbit
        CHECK(pv->GetView() == 1);
        CHECK(!pv->Blending());
        Input::FeedKeys({});
    }

    // --- The head is not deleted from the mesh, even if the old flag is set ---
    {
        Scene sc("head"); sc.physicsEnabled = false;
        GameObject* p = sc.CreateGameObject("P");
        auto* ch = p->AddComponent<Character>();
        ch->Apply();
        sc.Start();
        sc.Update(1.0f / 60.0f);
        auto maxY = [&]() {
            float y = -1.0e9f;
            auto* mr = p->GetComponent<MeshRenderer>();
            for (const Vec3& v : mr->mesh.vertices) if (v.y > y) y = v.y;
            return y;
        };
        const float shown = maxY();
        ch->hideOwnHead = true;
        sc.Update(1.0f / 60.0f);
        CHECK_NEAR(maxY(), shown, 0.02f);   // the skull stays; the camera skips it instead
        CHECK(shown > 1.7f);
    }

    // --- playerview save record round-trips ---
    {
        Scene s("pv");
        auto* pv = s.CreateGameObject("P")->AddComponent<PlayerView>();
        pv->allowToggle = true;
        pv->defaultView = PlayerView::View::First;
        pv->toggleKey = 'g';
        pv->toggleGamepadButton = 5;
        pv->blendTime = 0.2f;
        pv->eyeOffset = {0.1f, -0.05f, 0.02f};
        pv->thirdPersonDistance = 3.5f;
        pv->shoulderOffset = 0.4f;
        Scene s2("pv2");
        CHECK(SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s)));
        auto* p2 = s2.Find("P") ? s2.Find("P")->GetComponent<PlayerView>() : nullptr;
        CHECK(p2 != nullptr);
        if (p2) {
            CHECK(p2->allowToggle);
            CHECK(p2->defaultView == PlayerView::View::First);
            CHECK(p2->toggleKey == 'g');
            CHECK(p2->toggleGamepadButton == 5);
            CHECK_NEAR(p2->blendTime, 0.2f, 1e-4f);
            CHECK_NEAR(p2->eyeOffset.x, 0.1f, 1e-4f);
            CHECK_NEAR(p2->eyeOffset.y, -0.05f, 1e-4f);
            CHECK_NEAR(p2->eyeOffset.z, 0.02f, 1e-4f);
            CHECK_NEAR(p2->thirdPersonDistance, 3.5f, 1e-4f);
            CHECK_NEAR(p2->shoulderOffset, 0.4f, 1e-4f);
            CHECK(p2->switchEase == PlayerView::SwitchEase::Smooth);   // missing key keeps the default
            CHECK(!p2->snapSwitch);
        }
    }
    {
        Scene s("pv-ease");
        auto* pv = s.CreateGameObject("P")->AddComponent<PlayerView>();
        pv->switchEase = PlayerView::SwitchEase::In;
        pv->snapSwitch = true;
        pv->blendTime = 0.0f;
        Scene s2("pv-ease2");
        CHECK(SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s)));
        auto* p2 = s2.Find("P") ? s2.Find("P")->GetComponent<PlayerView>() : nullptr;
        CHECK(p2 != nullptr);
        if (p2) {
            CHECK(p2->switchEase == PlayerView::SwitchEase::In);
            CHECK(p2->snapSwitch);
            CHECK_NEAR(p2->blendTime, 0.0f, 1e-4f);
        }
    }

    // --- The blend eases, can snap, and does not pop the head on the way out ---
    {
        Scene sc("ease"); sc.physicsEnabled = false;
        GameObject* cam = sc.CreateGameObject("Camera");
        auto* camera = cam->AddComponent<Camera>();
        GameObject* p = sc.CreateGameObject("Player");
        auto* ch = p->AddComponent<Character>(); ch->Apply();
        auto* tp = p->AddComponent<ThirdPersonController>();
        tp->yaw = 0.0f; tp->pitch = 0.0f; tp->distance = 5.0f; tp->cameraHeight = 1.5f;
        tp->pivotDampingXZ = tp->pivotDampingY = 0.0f; tp->orbitSmoothing = 0.0f; tp->lookSmoothing = 0.0f;
        tp->autoRecenter = false;
        auto* pv = p->AddComponent<PlayerView>();
        pv->allowToggle = true; pv->defaultView = PlayerView::View::Third;
        pv->thirdPersonDistance = -1.0f; pv->shoulderOffset = 0.0f; pv->blendTime = 0.35f;
        pv->switchEase = PlayerView::SwitchEase::Smooth;
        sc.Start();
        Input::FeedKeys({}); sc.Update(1.0f / 60.0f);
        const float farDist = (cam->transform->Position() - Vec3{0, 1.5f, 0}).Magnitude();
        pv->SetView(0);
        for (int i = 0; i < 3; ++i) { Input::FeedKeys({}); sc.Update(1.0f / 60.0f); }
        const float eased = (cam->transform->Position() - Vec3{0, 1.5f, 0}).Magnitude();
        CHECK(farDist - eased < 0.45f);     // smoothstep has barely left the orbit
        CHECK(farDist - eased > 0.02f);     // and it did leave
        for (int i = 0; i < 40; ++i) { Input::FeedKeys({}); sc.Update(1.0f / 60.0f); }
        CHECK(!pv->Blending());
        CHECK(camera->ignoreObject == p);

        pv->SetView(1);
        Input::FeedKeys({}); sc.Update(1.0f / 60.0f);   // one frame: the lens is still at the eyes
        CHECK(pv->Blending());
        CHECK(camera->ignoreObject == p);               // head stays hidden until the camera pulls out
        CHECK(ch->Part(2) && ch->Part(2)->active);
        CHECK(ch->Part(2)->HiddenFromCamera(p));
        for (int i = 0; i < 40; ++i) { Input::FeedKeys({}); sc.Update(1.0f / 60.0f); }
        CHECK(!pv->Blending());
        CHECK(camera->ignoreObject == nullptr);
        CHECK(ch->Part(2) && !ch->Part(2)->HiddenFromCamera(nullptr));

        pv->snapSwitch = true;
        pv->SetView(0);
        Input::FeedKeys({}); sc.Update(1.0f / 60.0f);
        CHECK(!pv->Blending());
        CHECK((cam->transform->Position() - Vec3{0, 1.5f, 0}).Magnitude() < 0.45f);
        CHECK(camera->ignoreObject == p);
        pv->SetView(1);
        Input::FeedKeys({}); sc.Update(1.0f / 60.0f);
        CHECK(!pv->Blending());
        CHECK_NEAR((cam->transform->Position() - Vec3{0, 1.5f, 0}).Magnitude(), 5.0f, 0.45f);
        CHECK(camera->ignoreObject == nullptr);
        pv->snapSwitch = false;
        Input::FeedKeys({});
    }

    // --- A look third person cannot hold eases home. A legal orbit pitch stays. ---
    {
        Scene sc("pitch"); sc.physicsEnabled = false;
        GameObject* cam = sc.CreateGameObject("Camera");
        cam->AddComponent<Camera>();
        GameObject* p = sc.CreateGameObject("Player");
        p->AddComponent<Character>()->Apply();
        auto* tp = p->AddComponent<ThirdPersonController>();
        tp->yaw = 0.0f; tp->pitch = 0.0f; tp->distance = 5.0f; tp->cameraHeight = 1.5f;
        tp->pivotDampingXZ = tp->pivotDampingY = 0.0f; tp->orbitSmoothing = 0.0f; tp->lookSmoothing = 0.0f;
        tp->autoRecenter = false;
        auto* pv = p->AddComponent<PlayerView>();
        pv->allowToggle = true; pv->defaultView = PlayerView::View::Third;
        pv->blendTime = 0.30f; pv->shoulderOffset = 0.0f; pv->thirdPersonDistance = -1.0f;
        sc.Start();
        Input::FeedKeys({}); sc.Update(1.0f / 60.0f);
        pv->SetView(0);
        for (int i = 0; i < 30; ++i) { Input::FeedKeys({}); sc.Update(1.0f / 60.0f); }
        tp->pitch = 80.0f;
        pv->SetView(1);
        float prev = tp->pitch; bool snapped = false;
        for (int i = 0; i < 40; ++i) {
            Input::FeedKeys({}); sc.Update(1.0f / 60.0f);
            if (std::fabs(tp->pitch - prev) > 20.0f) snapped = true;
            prev = tp->pitch;
        }
        CHECK(!snapped);
        CHECK_NEAR(tp->pitch, 0.0f, 0.5f);
        tp->pitch = 30.0f;
        pv->SetView(0);
        for (int i = 0; i < 30; ++i) { Input::FeedKeys({}); sc.Update(1.0f / 60.0f); }
        pv->SetView(1);
        for (int i = 0; i < 30; ++i) { Input::FeedKeys({}); sc.Update(1.0f / 60.0f); }
        CHECK_NEAR(tp->pitch, 30.0f, 0.5f);
        Input::FeedKeys({});
    }

    // --- Locomotion, crouch, prone and lean carry across the switch ---
    {
        Scene sc("carry"); sc.physicsEnabled = false;
        GameObject* cam = sc.CreateGameObject("Camera");
        cam->AddComponent<Camera>();
        GameObject* p = sc.CreateGameObject("Player");
        auto* ch = p->AddComponent<Character>(); ch->Apply();
        auto* tp = p->AddComponent<ThirdPersonController>();
        tp->yaw = 0.0f; tp->pitch = 0.0f; tp->distance = 5.0f; tp->cameraHeight = 1.5f;
        tp->pivotDampingXZ = tp->pivotDampingY = 0.0f; tp->orbitSmoothing = 0.0f; tp->lookSmoothing = 0.0f;
        tp->autoRecenter = false; tp->driveAnimation = false;
        auto* pv = p->AddComponent<PlayerView>();
        pv->allowToggle = true; pv->defaultView = PlayerView::View::Third;
        pv->blendTime = 0.30f; pv->shoulderOffset = 0.0f; pv->thirdPersonDistance = -1.0f;
        sc.Start();
        Input::FeedKeys({});
        ch->anim = 1; ch->bodyLean = 14.0f; ch->lookYaw = 22.0f;
        sc.Update(1.0f / 60.0f);
        auto across = [&](int state) {
            ch->anim = state;
            ch->moveSpeed = (state == 2 || state == 3 || state == 17) ? 1.6f : 0.0f;
            for (int i = 0; i < 20; ++i) sc.Update(1.0f / 60.0f);
            const float phase0 = ch->GaitPhase();
            const float time0 = ch->animTime;
            pv->SetView(0);
            float prev = phase0; bool phaseJump = false;
            for (int i = 0; i < 20; ++i) {
                sc.Update(1.0f / 60.0f);
                float d = ch->GaitPhase() - prev;
                if (d > 0.5f) d -= 1.0f;
                if (d < -0.5f) d += 1.0f;
                if (std::fabs(d) > 0.2f) phaseJump = true;
                prev = ch->GaitPhase();
            }
            CHECK(ch->anim == state);
            CHECK(!phaseJump);
            CHECK(ch->animTime + 0.02f >= time0);
            CHECK_NEAR(ch->bodyLean, 14.0f, 1e-3f);
            CHECK_NEAR(ch->lookYaw, 22.0f, 1e-3f);
            pv->SetView(1);
            for (int i = 0; i < 25; ++i) sc.Update(1.0f / 60.0f);
            CHECK(ch->anim == state);
            CHECK(!pv->Blending());
        };
        across(1);
        across(2);
        across(3);
        across(6);
        across(7);
        across(17);
        Input::FeedKeys({});
    }

    // --- A held item slides between the grip and the first-person placement ---
    {
        auto holdBlend = [](bool two) {
            Scene sc(two ? "hold2" : "hold1"); sc.physicsEnabled = false;
            GameObject* cam = sc.CreateGameObject("Camera");
            cam->AddComponent<Camera>();
            GameObject* watch = sc.CreateGameObject("Watcher");
            auto* other = watch->AddComponent<Camera>();
            other->main = false;
            GameObject* p = sc.CreateGameObject("Player");
            auto* ch = p->AddComponent<Character>(); ch->Apply();
            auto* tp = p->AddComponent<ThirdPersonController>();
            tp->yaw = 0.0f; tp->pitch = 0.0f; tp->distance = 5.0f; tp->cameraHeight = 1.5f;
            tp->pivotDampingXZ = tp->pivotDampingY = 0.0f; tp->orbitSmoothing = 0.0f; tp->lookSmoothing = 0.0f;
            tp->autoRecenter = false; tp->driveAnimation = false;
            ch->anim = two ? 1 : 2;
            ch->moveSpeed = two ? 0.0f : 1.4f;
            auto* pv = p->AddComponent<PlayerView>();
            pv->allowToggle = true; pv->defaultView = PlayerView::View::Third;
            pv->blendTime = 0.30f; pv->shoulderOffset = 0.0f; pv->thirdPersonDistance = -1.0f;
            p->AddComponent<ProceduralObjectHolding>();
            GameObject* item = sc.CreateGameObject(two ? "Crate" : "Block");
            item->transform->localPosition = {0.4f, 1.0f, -0.6f};
            item->transform->localScale = two ? Vec3{0.45f, 0.35f, 0.45f} : Vec3{0.18f, 0.18f, 0.18f};
            auto* mr = item->AddComponent<MeshRenderer>();
            mr->mesh = Mesh::Cube(1.0f);
            auto* ho = item->AddComponent<HoldableObject>();
            if (two) { ho->style = HoldableObject::Style::Box; ho->twoHanded = true; }
            sc.Start();
            Input::FeedKeys({});
            auto* hold = p->GetComponent<ProceduralObjectHolding>();
            CHECK(hold->Pickup(item));
            for (int i = 0; i < 45; ++i) sc.Update(1.0f / 60.0f);
            CHECK(hold->IsHolding());
            const float phase0 = ch->GaitPhase();
            pv->SetView(0);
            Vec3 prev = item->transform->Position();
            bool jumped = false;
            for (int i = 0; i < 25; ++i) {
                sc.Update(1.0f / 60.0f);
                GameObject* copy = sc.Find("FP_OwnerItem");
                const Vec3 shown = (copy && copy->active && copy->transform)
                    ? copy->transform->Position() : item->transform->Position();
                if ((shown - prev).Magnitude() > 0.35f) jumped = true;
                prev = shown;
                CHECK((item->transform->Position() - p->transform->Position()).Magnitude() < 2.2f);
                CHECK(ch->Part(2) && !ch->Part(2)->HiddenFromCamera(nullptr));
                if (copy && copy->active) CHECK(copy->HiddenFromCamera(nullptr));
                (void)other;
            }
            CHECK(!jumped);
            CHECK(ch->anim == (two ? 1 : 2));
            if (!two) {
                float d = ch->GaitPhase() - phase0;
                if (d > 0.5f) d -= 1.0f;
                if (d < -0.5f) d += 1.0f;
                CHECK(std::fabs(d) < 0.45f);
                CHECK(std::fabs(d) > 0.01f);
            }
            for (int i = 0; i < 20; ++i) sc.Update(1.0f / 60.0f);
            CHECK(!pv->Blending());
            GameObject* copy = sc.Find("FP_OwnerItem");
            CHECK(copy && copy->active);
            CHECK(cam->GetComponent<Camera>()->ignoreObject == p);
            pv->SetView(1);
            prev = copy->transform->Position();
            jumped = false;
            for (int i = 0; i < 25; ++i) {
                sc.Update(1.0f / 60.0f);
                GameObject* c2 = sc.Find("FP_OwnerItem");
                const Vec3 shown = (c2 && c2->active && c2->transform)
                    ? c2->transform->Position() : item->transform->Position();
                if ((shown - prev).Magnitude() > 0.35f) jumped = true;
                prev = shown;
            }
            CHECK(!jumped);
            Input::FeedKeys({});
        };
        holdBlend(false);
        holdBlend(true);
    }

    // --- V every frame reverses cleanly and settles on the last request ---
    {
        Scene sc("spam"); sc.physicsEnabled = false;
        GameObject* cam = sc.CreateGameObject("Camera");
        cam->AddComponent<Camera>();
        GameObject* p = sc.CreateGameObject("Player");
        auto* ch = p->AddComponent<Character>(); ch->Apply();
        auto* tp = p->AddComponent<ThirdPersonController>();
        tp->yaw = 0.0f; tp->pitch = 0.0f; tp->distance = 5.0f; tp->cameraHeight = 1.5f;
        tp->pivotDampingXZ = tp->pivotDampingY = 0.0f; tp->orbitSmoothing = 0.0f; tp->lookSmoothing = 0.0f;
        tp->autoRecenter = false;
        auto* pv = p->AddComponent<PlayerView>();
        pv->allowToggle = true; pv->defaultView = PlayerView::View::Third;
        pv->blendTime = 0.30f; pv->shoulderOffset = 0.0f; pv->thirdPersonDistance = -1.0f;
        sc.Start();
        Input::FeedKeys({}); sc.Update(1.0f / 60.0f);
        for (int i = 0; i < 40; ++i) {
            pv->Toggle();
            Input::FeedKeys({});
            sc.Update(1.0f / 60.0f);
            const Vec3 at = cam->transform->Position();
            CHECK(std::isfinite(at.x) && std::isfinite(at.y) && std::isfinite(at.z));
            if (ch->Part(2)) CHECK(ch->Part(2)->active);
        }
        CHECK(pv->GetView() == 1);   // started in third; 40 flips lands on third
        for (int i = 0; i < 30; ++i) { Input::FeedKeys({}); sc.Update(1.0f / 60.0f); }
        CHECK(!pv->Blending());
        CHECK(pv->GetView() == 1);
        CHECK(cam->GetComponent<Camera>()->ignoreObject == nullptr);
        CHECK_NEAR((cam->transform->Position() - Vec3{0, 1.5f, 0}).Magnitude(), 5.0f, 0.5f);
        if (ch->Part(2)) CHECK(ch->Part(2)->active && !ch->Part(2)->HiddenFromCamera(nullptr));
        Input::FeedKeys({});
    }

    // --- The blend does not travel through a wall that sits across the slide ---
    {
        Scene sc("blendwall");
        GameObject* p = sc.CreateGameObject("Player");
        p->AddComponent<Character>()->Apply();
        auto* fp = p->AddComponent<FirstPersonController>();
        fp->lookSmoothing = 0.0f; fp->yaw = 0.0f; fp->pitch = 0.0f;
        GameObject* camObj = sc.CreateGameObject("Eye");
        camObj->AddComponent<Camera>();
        camObj->transform->SetParent(p->transform, false);
        camObj->transform->localPosition = {0, 1.6f, -0.2f};
        auto* pv = p->AddComponent<PlayerView>();
        pv->allowToggle = true; pv->defaultView = PlayerView::View::First;
        pv->thirdPersonDistance = 4.5f; pv->shoulderOffset = 0.0f; pv->blendTime = 0.30f;
        sc.Start();
        Input::FeedKeys({});
        for (int i = 0; i < 3; ++i) sc.Update(1.0f / 60.0f);
        pv->SetView(1);
        for (int i = 0; i < 30; ++i) sc.Update(1.0f / 60.0f);
        const float openZ = p->transform->InverseTransformPoint(camObj->transform->Position()).z;
        CHECK(openZ > 1.0f);   // nothing in the way: the orbit is behind the body
        pv->SetView(0);
        for (int i = 0; i < 30; ++i) sc.Update(1.0f / 60.0f);
        GameObject* wall = sc.CreateGameObject("Wall");
        wall->transform->localPosition = {0, 1.6f, -0.05f};
        wall->AddComponent<BoxCollider3D>()->size = {2.0f, 2.0f, 0.08f};
        pv->SetView(1);
        bool crossed = false;
        for (int i = 0; i < 40; ++i) {
            sc.Update(1.0f / 60.0f);
            const float z = p->transform->InverseTransformPoint(camObj->transform->Position()).z;
            if (z > 0.05f) crossed = true;
        }
        CHECK(!crossed);
        Input::FeedKeys({});
    }

    // --- OkayScript set_view / get_view ---
    {
        Scene sc("scr"); sc.physicsEnabled = false;
        GameObject* cam = sc.CreateGameObject("Camera");
        cam->AddComponent<Camera>();
        GameObject* p = sc.CreateGameObject("Player");
        p->AddComponent<ThirdPersonController>();
        auto* scp = p->AddComponent<ScriptComponent>("okayscript");
        scp->LoadSource(
            "var ok = 0; var v = -2;\n"
            "function start() { ok = set_view(0); v = get_view(); }\n");
        sc.Start();
        auto* pv = p->GetComponent<PlayerView>();
        CHECK(pv != nullptr);                             // set_view added one
        CHECK_NEAR(scp->VM()->GetGlobal("ok").AsFloat(), 1.0f, 0.001f);
        CHECK_NEAR(scp->VM()->GetGlobal("v").AsFloat(), 0.0f, 0.001f);
        if (pv) CHECK(pv->GetView() == 0);
    }
    {
        Scene empty("none"); empty.physicsEnabled = false;
        GameObject* lone = empty.CreateGameObject("Lone");
        auto* loneSc = lone->AddComponent<ScriptComponent>("okayscript");
        loneSc->LoadSource(
            "var ok = 1; var v = 0;\n"
            "function start() { ok = set_view(1); v = get_view(); }\n");
        empty.Start();
        CHECK_NEAR(loneSc->VM()->GetGlobal("ok").AsFloat(), 0.0f, 0.001f);
        CHECK_NEAR(loneSc->VM()->GetGlobal("v").AsFloat(), -1.0f, 0.001f);
    }

    // Switching to first person and back must leave the whole character in the
    // third-person camera. The hide is a camera flag; it is put back, including
    // when V is tapped in the middle of the ease. The edit-mode guess that hides
    // a first-person body must not run while playing, or the Game view skips the
    // player after the flag has been cleared.
    {
        Scene edit("edit-hide"); edit.physicsEnabled = false;
        GameObject* owner = edit.CreateGameObject("Player");
        owner->AddComponent<FirstPersonController>()->showBody = false;
        GameObject* camGo = edit.CreateGameObject("Cam");
        camGo->transform->SetParent(owner->transform, false);
        auto* editCam = camGo->AddComponent<Camera>();
        CHECK(GameViewBodyIgnore(editCam, false) == owner);
        CHECK(GameViewBodyIgnore(editCam, true) == nullptr);
        editCam->ignoreObject = owner;
        CHECK(GameViewBodyIgnore(editCam, true) == owner);
    }
    auto characterPixels = [](Scene& sc, Camera* cam, GameObject* player) {
        const int W = 160, H = 90;
        auto grab = [&]() {
            ApplySceneLight(sc);
            Mat4 vp = cam->ProjectionMatrix((float)W / (float)H) * cam->ViewMatrix();
            Raster work; std::vector<std::uint32_t> out;
            const std::uint32_t* px = RenderMeshesSS(work, out, sc, vp, cam->transform->Position(), W, H, 1,
                                                     GameViewBodyIgnore(cam, true));
            Image img(W, H);
            for (int i = 0; i < W * H; ++i) {
                std::uint32_t p = px[i];
                unsigned r = p & 255u, g = (p >> 8) & 255u, b = (p >> 16) & 255u, a = (p >> 24) & 255u;
                if (a == 0) { r = 166; g = 206; b = 235; }
                img.SetPixel(i % W, i / W, Color(r / 255.f, g / 255.f, b / 255.f, 1.f));
            }
            return img;
        };
        auto differ = [](const Image& a, const Image& b) {
            int n = 0;
            for (int y = 0; y < a.Height(); ++y)
                for (int x = 0; x < a.Width(); ++x) {
                    Color ca = a.GetPixel(x, y), cb = b.GetPixel(x, y);
                    if (std::fabs(ca.r - cb.r) + std::fabs(ca.g - cb.g) + std::fabs(ca.b - cb.b) > 0.04f) ++n;
                }
            return n;
        };
        Image on = grab();
        std::vector<MeshRenderer*> off;
        std::function<void(GameObject*)> hide = [&](GameObject* go) {
            if (!go) return;
            if (auto* mr = go->GetComponent<MeshRenderer>())
                if (mr->enabled) { mr->enabled = false; off.push_back(mr); }
            if (!go->transform) return;
            for (Transform* c : go->transform->Children())
                if (c && c->gameObject && !c->gameObject->GetComponent<Camera>()) hide(c->gameObject);
        };
        hide(player);
        Image gone = grab();
        for (MeshRenderer* mr : off) mr->enabled = true;
        return differ(on, gone);
    };
    auto pressV = [](Scene& sc) {
        Input::FeedKeys({});
        sc.Update(1.0f / 60.0f);
        Input::FeedKeys({'v'});
        sc.Update(1.0f / 60.0f);
        Input::FeedKeys({});
    };
    auto settle = [](Scene& sc, int n) {
        Input::FeedKeys({});
        for (int i = 0; i < n; ++i) sc.Update(1.0f / 60.0f);
    };
    auto checkThird = [&](Scene& sc, GameObject* player, Camera* cam, Character* ch, int before) {
        auto* pv = player->GetComponent<PlayerView>();
        CHECK(pv && !pv->TargetFirst() && !pv->Blending());
        CHECK(cam->ignoreObject == nullptr);
        CHECK_NEAR(cam->nearClip, 0.3f, 1e-3f);
        CHECK(ch->PartsBuilt());
        CHECK(!ch->firstPersonArm);
        CHECK(!ch->hideOwnHead);
        auto* root = player->GetComponent<MeshRenderer>();
        CHECK(root && !root->enabled);                 // the rig draws the body; the baked mesh stays off
        GameObject* head = ch->Part(2);
        CHECK(head && head->active && !head->firstPersonViewmodel);
        CHECK(head->transform && (head->transform->localScale - Vec3::One).Magnitude() < 1e-3f);
        if (auto* hmr = head->GetComponent<MeshRenderer>()) CHECK(hmr->castShadows && hmr->enabled);
        int px = characterPixels(sc, cam, player);
        CHECK(px > before / 2);                        // the whole character, not an empty orbit
        CHECK(px > 80);
        settle(sc, 30);                                // the controller must not put the hide back
        CHECK(cam->ignoreObject == nullptr);
        CHECK_NEAR(cam->nearClip, 0.3f, 1e-3f);
        CHECK(characterPixels(sc, cam, player) > 80);
    };
    {
        Scene sc("tp-round"); sc.physicsEnabled = false;
        Templates::ThirdPerson(sc);
        GameObject* player = nullptr; Camera* cam = nullptr; Character* ch = nullptr;
        for (auto& o : sc.Objects()) {
            if (o->name == "Player") { player = o.get(); ch = o->GetComponent<Character>(); }
            if (auto* c = o->GetComponent<Camera>()) if (c->main) cam = c;
        }
        sc.Start();
        settle(sc, 5);
        const int before = characterPixels(sc, cam, player);
        CHECK(before > 80);
        for (int trip = 0; trip < 5; ++trip) {
            pressV(sc); settle(sc, 40);               // into the eyes
            pressV(sc); settle(sc, 40);               // back to the orbit
            checkThird(sc, player, cam, ch, before);
        }
        for (int i = 0; i < 8; ++i) { pressV(sc); settle(sc, 2); }   // tap V through the ease
        player->GetComponent<PlayerView>()->SetView(0);
        settle(sc, 6);
        player->GetComponent<PlayerView>()->SetView(1);
        settle(sc, 4);
        player->GetComponent<PlayerView>()->SetView(0);
        settle(sc, 3);
        player->GetComponent<PlayerView>()->SetView(1);
        settle(sc, 40);
        checkThird(sc, player, cam, ch, before);
    }
    {
        Scene sc("fp-round"); sc.physicsEnabled = false;
        Templates::FPS(sc);
        GameObject* player = nullptr; Camera* cam = nullptr; Character* ch = nullptr;
        for (auto& o : sc.Objects()) {
            if (o->name == "Player") { player = o.get(); ch = o->GetComponent<Character>(); }
            if (auto* c = o->GetComponent<Camera>()) if (c->main) cam = c;
        }
        sc.Start();
        settle(sc, 5);
        CHECK(cam->ignoreObject == player);            // native first person still hides the owner
        for (int trip = 0; trip < 5; ++trip) {
            pressV(sc); settle(sc, 40);               // over the shoulder
            checkThird(sc, player, cam, ch, 200);
            pressV(sc); settle(sc, 40);               // back to the eyes
            CHECK(cam->ignoreObject == player);
            CHECK(cam->nearClip < 0.06f);
        }
        for (int i = 0; i < 8; ++i) { pressV(sc); settle(sc, 2); }
        player->GetComponent<PlayerView>()->SetView(0);
        settle(sc, 6);
        player->GetComponent<PlayerView>()->SetView(1);
        settle(sc, 40);
        checkThird(sc, player, cam, ch, 200);
    }

    // Third Person template, the playtest: looking down must not draw the inside
    // of the shirt, the plain hand shows unless the body is running, and the
    // orbit pitch eases back to the angle play started at. The head stays
    // hidden while the lens is still at the eyes and comes back once it pulls out.
    {
        Scene sc("playtest"); sc.physicsEnabled = false;
        Templates::ThirdPerson(sc);
        GameObject* player = nullptr; Camera* cam = nullptr; Character* ch = nullptr;
        ThirdPersonController* tp = nullptr;
        for (auto& o : sc.Objects()) {
            if (o->name == "Player") {
                player = o.get();
                ch = o->GetComponent<Character>();
                tp = o->GetComponent<ThirdPersonController>();
            }
            if (auto* c = o->GetComponent<Camera>()) if (c->main) cam = c;
        }
        auto* pv = player->GetComponent<PlayerView>();
        pv->blendTime = 2.0f;                         // a long ease must not delay the restore
        sc.Start();
        settle(sc, 5);
        const float restPitch = tp->pitch;
        CHECK_NEAR(restPitch, 18.0f, 0.01f);
        pv->SetView(0);
        settle(sc, 150);                               // arrive at the eyes despite the long blend
        CHECK(!pv->Blending());
        CHECK(cam->ignoreObject == player);
        CHECK(ch->fpArmUp == 0.0f);                // nothing in the view until a hold
        auto* hands = player->GetComponent<FirstPersonHands>();
        CHECK(hands);
        if (hands) hands->showWhenEmpty = true;
        settle(sc, 48);                            // the copy eases in
        CHECK(ch->Part(2) && !ch->Part(2)->firstPersonViewmodel);
        CHECK(ch->Part(1) && ch->Part(1)->firstPersonViewmodel); // the shirt is drawn for this camera
        tp->driveAnimation = false;
        ch->anim = 1;
        tp->pitch = 80.0f;                         // steep look down, where the shirt enters the frame
        settle(sc, 8);
        const Vec3 eye = player->transform->InverseTransformPoint(cam->transform->Position());
        CHECK(eye.z < -0.30f);                     // in front of the chest, not inside the shirt
        CHECK(eye.y > 1.70f);
        auto partDiff = [&](GameObject* part) {
            const int W = 160, H = 90;
            auto grab = [&]() {
                ApplySceneLight(sc);
                Mat4 vp = cam->ProjectionMatrix((float)W / (float)H) * cam->ViewMatrix();
                Raster work; std::vector<std::uint32_t> out;
                const std::uint32_t* px = RenderMeshesSS(work, out, sc, vp, cam->transform->Position(),
                                                         W, H, 1, cam->ignoreObject);
                return std::vector<std::uint32_t>(px, px + W * H);
            };
            auto a = grab();
            auto* mr = part->GetComponent<MeshRenderer>();
            bool en = mr->enabled;
            mr->enabled = false;
            auto b = grab();
            mr->enabled = en;
            int n = 0;
            for (std::size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) ++n;
            return n;
        };
        GameObject* ownerHand = nullptr;
        if (GameObject* arm = ch->OwnerViewArm())
            if (arm->transform)
                for (Transform* fore : arm->transform->Children())
                    if (fore)
                        for (Transform* h : fore->Children())
                            if (h && h->gameObject && h->gameObject->name == "FP_OwnerHand")
                                ownerHand = h->gameObject;
        CHECK(partDiff(ch->Part(1)) > 40);             // the shirt is drawn, not an open hole
        CHECK(ownerHand && ownerHand->active);         // looking down keeps the copy in the corner
        CHECK(partDiff(ownerHand) > 8);
        CHECK(partDiff(ch->Part(5)) == 0);            // the real hand is not in this camera
        // A block inside the chest must not show through the cloth.
        GameObject* marker = sc.CreateGameObject("ChestMarker");
        marker->transform->localPosition = player->transform->Position() + Vec3{0.0f, 1.25f, 0.0f};
        marker->transform->localScale = {0.12f, 0.12f, 0.12f};
        auto* mmr = marker->AddComponent<MeshRenderer>();
        mmr->mesh = Mesh::Cube();
        mmr->color = Color::FromBytes(255, 0, 255);
        CHECK(partDiff(marker) == 0);
        ch->anim = 3;
        for (int i = 0; i < 40; ++i) sc.Update(1.0f / 60.0f);   // the copy eases off the corner
        CHECK(ch->HideFirstPersonHands());
        CHECK(partDiff(ch->Part(5)) == 0);             // a run keeps the real hand off the screen
        CHECK(partDiff(ch->Part(3)) == 0);
        CHECK(!ownerHand->active || partDiff(ownerHand) == 0);
        ch->anim = 1;
        tp->pitch = restPitch;                         // level again: a small hand, not the whole right third
        settle(sc, 8);
        CHECK(ownerHand->active);
        {
            const int handPx = partDiff(ownerHand);
            CHECK(handPx > 15);
            CHECK(handPx < 2500);
        }
        // Back toward third person. One frame has not left the eyes, so the
        // head stays hidden and the pitch has only begun to ease home.
        pressV(sc);
        CHECK(pv->GetView() == 1);
        CHECK(pv->Blending());
        CHECK_NEAR(tp->pitch, restPitch, 0.01f);   // the look was put back before the switch
        CHECK(cam->ignoreObject == player);
        CHECK(ch->Part(2) && ch->Part(2)->active && ch->Part(2)->HiddenFromCamera(player));
        CHECK(ch->Part(1) && ch->Part(1)->active);
        GameObject* neck = nullptr;
        if (ch->Part(1) && ch->Part(1)->transform)
            for (Transform* c : ch->Part(1)->transform->Children())
                if (c && c->gameObject && c->gameObject->name == "Neck") neck = c->gameObject;
        CHECK(neck && neck->active && neck->HiddenFromCamera(player));
        // Four quick taps. While the lens is still at the eyes the head stays
        // hidden; once it has pulled out the head is drawn again.
        for (int tap = 0; tap < 4; ++tap) {
            pressV(sc);
            settle(sc, 2);
            if (pv->GetView() == 1) {
                const Vec3 rel = player->transform->InverseTransformPoint(cam->transform->Position());
                Vec3 headAt{0.0f, 1.62f, 0.0f};
                if (ch->Part(2) && ch->Part(2)->transform)
                    headAt = player->transform->InverseTransformPoint(ch->Part(2)->transform->Position());
                const bool nearHead = (rel - headAt).Magnitude() < 1.05f;
                if (nearHead) CHECK(cam->ignoreObject == player);
                else CHECK(cam->ignoreObject == nullptr);
                CHECK(ch->Part(2) && ch->Part(2)->active);
                CHECK(std::isfinite(tp->pitch));
                CHECK(tp->pitch <= 80.0f + 0.01f);
            }
        }
        pv->SetView(1);
        settle(sc, 150);
        CHECK(!pv->Blending());
        CHECK_NEAR(tp->pitch, restPitch, 0.01f);
        const Vec3 rel = player->transform->InverseTransformPoint(cam->transform->Position());
        CHECK(rel.z > 2.0f);                           // behind the body, not a top-down perch
        CHECK(rel.y < 4.5f);
        CHECK(partDiff(ch->Part(2)) > 40);             // the head is in the third-person frame
        CHECK(cam->ignoreObject == nullptr);
        CHECK_NEAR(cam->nearClip, 0.3f, 1e-3f);
    }

    // --- Every blend frame: the owner's camera never sees the inside of the head.
    //     Standing and walking, fixed 1/60 steps. Shadows stay on the mesh. ---
    {
        auto pass = [](bool walking) {
            Scene sc(walking ? "blend-walk" : "blend-stand");
            sc.physicsEnabled = false;
            GameObject* light = sc.CreateGameObject("Directional Light");
            light->AddComponent<Light>();
            light->transform->localRotation = Quat::Euler({50, -30, 0});
            GameObject* ground = sc.CreateGameObject("Ground");
            ground->transform->localPosition = {0, -0.5f, 0};
            ground->transform->localScale = {16, 1, 16};
            auto* gmr = ground->AddComponent<MeshRenderer>();
            gmr->mesh = Mesh::Cube();
            gmr->color = Color::FromBytes(140, 150, 140);
            GameObject* camGo = sc.CreateGameObject("Camera");
            auto* cam = camGo->AddComponent<Camera>();
            cam->nearClip = 0.3f;
            GameObject* player = sc.CreateGameObject("Player");
            auto* ch = player->AddComponent<Character>();
            ch->Apply();
            auto* tp = player->AddComponent<ThirdPersonController>();
            tp->yaw = 0.0f; tp->pitch = 18.0f; tp->distance = 4.5f; tp->cameraHeight = 1.5f;
            tp->pivotDampingXZ = tp->pivotDampingY = 0.0f;
            tp->orbitSmoothing = 0.0f; tp->lookSmoothing = 0.0f;
            tp->autoRecenter = false; tp->driveAnimation = false;
            tp->cameraCollision = false;
            auto* pv = player->AddComponent<PlayerView>();
            pv->allowToggle = true; pv->defaultView = PlayerView::View::Third;
            pv->blendTime = 0.30f; pv->shoulderOffset = 0.45f; pv->thirdPersonDistance = -1.0f;
            pv->switchEase = PlayerView::SwitchEase::Smooth;
            sc.Start();
            Input::FeedKeys({});
            sc.Update(1.0f / 60.0f);
            if (walking) {
                ch->anim = 2;
                ch->moveSpeed = 1.6f;
                for (int i = 0; i < 20; ++i) sc.Update(1.0f / 60.0f);
            }
            pv->SetView(0);
            GameObject* sideGo = sc.CreateGameObject("Side");
            auto* side = sideGo->AddComponent<Camera>();
            side->main = false;
            side->nearClip = 0.05f;

            auto neckOf = [&]() -> GameObject* {
                GameObject* torso = ch->Part(1);
                if (!torso || !torso->transform) return nullptr;
                for (Transform* c : torso->transform->Children())
                    if (c && c->gameObject && c->gameObject->name == "Neck") return c->gameObject;
                return nullptr;
            };
            auto differ = [](const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b) {
                int n = 0;
                for (std::size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) ++n;
                return n;
            };
            const int W = 160, H = 90;
            auto grab = [&](Camera* which, const GameObject* ignore) {
                ApplySceneLight(sc);
                Mat4 vp = which->ProjectionMatrix((float)W / (float)H) * which->ViewMatrix();
                Raster work; std::vector<std::uint32_t> out;
                const std::uint32_t* px = RenderMeshesSS(work, out, sc, vp, which->transform->Position(),
                                                         W, H, 1, ignore);
                return std::vector<std::uint32_t>(px, px + W * H);
            };
            auto partPx = [&](GameObject* part, Camera* which, const GameObject* ignore) {
                if (!part) return 0;
                auto* mr = part->GetComponent<MeshRenderer>();
                if (!mr) return 0;
                auto a = grab(which, ignore);
                const bool en = mr->enabled;
                mr->enabled = false;
                auto b = grab(which, ignore);
                mr->enabled = en;
                return differ(a, b);
            };

            bool sawHidden = false;
            bool sawInsideSlot = false;
            for (int frame = 0; frame < 40; ++frame) {
                Input::FeedKeys({});
                sc.Update(1.0f / 60.0f);
                GameObject* head = ch->Part(2);
                GameObject* neck = neckOf();
                CHECK(head && head->active && head->transform);
                CHECK(neck && neck->active);
                auto* hmr = head->GetComponent<MeshRenderer>();
                auto* nmr = neck->GetComponent<MeshRenderer>();
                CHECK(hmr && hmr->castShadows && hmr->enabled);
                CHECK(nmr && nmr->castShadows && nmr->enabled);

                Vec3 lo, hi;
                hmr->mesh.Bounds(lo, hi);
                const Vec3 local = head->transform->InverseTransformPoint(cam->transform->Position());
                const bool inside = local.x > lo.x && local.x < hi.x && local.y > lo.y && local.y < hi.y
                    && local.z > lo.z && local.z < hi.z;
                const Vec3 ext = hi - lo;
                const float size = std::max(ext.x, std::max(ext.y, ext.z));
                const float dist = (cam->transform->Position() - head->transform->Position()).Magnitude();
                const float radius = 0.5f * std::sqrt(ext.x * ext.x + ext.y * ext.y + ext.z * ext.z);
                const bool clips = dist < radius + cam->nearClip + 0.02f;
                const int headPx = partPx(head, cam, cam->ignoreObject);
                const int neckPx = partPx(neck, cam, cam->ignoreObject);
                if (inside || clips) {
                    CHECK(headPx == 0);
                    CHECK(neckPx == 0);
                    sawInsideSlot = true;
                }
                if (dist < std::max(size * 1.5f + 0.12f, 0.90f)) {
                    CHECK(cam->ignoreObject == player);
                    CHECK(head->HiddenFromCamera(player));
                    CHECK(neck->HiddenFromCamera(player));
                    CHECK(headPx == 0);
                    CHECK(neckPx == 0);
                    sawHidden = true;
                }
            }
            CHECK(sawHidden);
            CHECK(sawInsideSlot);
            CHECK(!pv->Blending());
            // Another camera still draws the head while the owner is at the eyes.
            const Vec3 headAt = ch->Part(2)->transform->Position();
            sideGo->transform->SetPosition(headAt + Vec3{1.2f, 0.2f, 0.8f});
            sideGo->transform->SetRotation(Quat::LookRotation(sideGo->transform->Position() - headAt));
            CHECK(partPx(ch->Part(2), side, nullptr) > 40);
            CHECK(ch->Part(2)->GetComponent<MeshRenderer>()->castShadows);
        };
        pass(false);
        pass(true);
    }

    // --- Looking down in first person with empty hands: no stubby arm ends. ---
    {
        Scene sc("lookdown");
        sc.physicsEnabled = false;
        GameObject* light = sc.CreateGameObject("Directional Light");
        light->AddComponent<Light>();
        light->transform->localRotation = Quat::Euler({50, -30, 0});
        GameObject* ground = sc.CreateGameObject("Ground");
        ground->transform->localPosition = {0, -0.5f, 0};
        ground->transform->localScale = {12, 1, 12};
        ground->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        GameObject* camGo = sc.CreateGameObject("Camera");
        auto* cam = camGo->AddComponent<Camera>();
        GameObject* player = sc.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>();
        ch->Apply();
        player->AddComponent<FirstPersonHands>();   // showWhenEmpty stays false
        auto* tp = player->AddComponent<ThirdPersonController>();
        tp->yaw = 0.0f; tp->pitch = 0.0f; tp->distance = 4.5f; tp->cameraHeight = 1.5f;
        tp->pivotDampingXZ = tp->pivotDampingY = 0.0f;
        tp->orbitSmoothing = 0.0f; tp->lookSmoothing = 0.0f;
        tp->autoRecenter = false; tp->driveAnimation = false; tp->cameraCollision = false;
        auto* pv = player->AddComponent<PlayerView>();
        pv->allowToggle = true; pv->defaultView = PlayerView::View::Third;
        pv->blendTime = 0.05f; pv->shoulderOffset = 0.0f; pv->thirdPersonDistance = -1.0f;
        sc.Start();
        Input::FeedKeys({});
        sc.Update(1.0f / 60.0f);
        pv->SetView(0);
        for (int i = 0; i < 20; ++i) sc.Update(1.0f / 60.0f);
        CHECK(!pv->Blending());
        tp->pitch = 80.0f;   // orbit pitch is positive looking down
        for (int i = 0; i < 6; ++i) sc.Update(1.0f / 60.0f);
        CHECK(ch->fpArmUp == 0.0f);
        CHECK(cam->ignoreObject == player);
        const Vec3 at = player->transform->InverseTransformPoint(camGo->transform->Position());
        CHECK(at.z < -0.28f);
        CHECK(at.y > 1.70f);
        CHECK(std::fabs(tp->pitch - 80.0f) < 0.01f);
        auto px = [&](GameObject* part) {
            const int W = 160, H = 90;
            auto grab = [&]() {
                ApplySceneLight(sc);
                Mat4 vp = cam->ProjectionMatrix((float)W / (float)H) * cam->ViewMatrix();
                Raster work; std::vector<std::uint32_t> out;
                const std::uint32_t* p = RenderMeshesSS(work, out, sc, vp, camGo->transform->Position(),
                                                        W, H, 1, cam->ignoreObject);
                return std::vector<std::uint32_t>(p, p + W * H);
            };
            auto a = grab();
            auto* mr = part->GetComponent<MeshRenderer>();
            const bool en = mr->enabled;
            mr->enabled = false;
            auto b = grab();
            mr->enabled = en;
            int n = 0;
            for (std::size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) ++n;
            return n;
        };
        CHECK(px(ch->Part(1)) > 20);     // the shirt is the solid shoulder
        for (int b : {3, 4, 5, 6, 7, 8}) // both arms, forearms and hands
            CHECK(px(ch->Part(b)) == 0);
        CHECK(px(ch->Part(2)) == 0);     // and not the head
        Input::FeedKeys({});
    }

    TEST_MAIN_RESULT();
}
