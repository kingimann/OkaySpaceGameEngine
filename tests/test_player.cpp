#include "test_framework.hpp"
#include <Okay.hpp>

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
        CHECK(p->GetComponent<Rigidbody3D>() != nullptr);
        CHECK(p->GetComponent<BoxCollider3D>() != nullptr);
        // A camera exists in the scene (created since there was none).
        CHECK(Templates::EnsureMainCamera(sc) != nullptr);
    }
    {
        Scene sc("fp");
        GameObject* p = Templates::AddFirstPersonPlayer(sc);
        CHECK(p && p->GetComponent<FirstPersonController>() != nullptr);
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
        CHECK(!ch->hideOwnHead);
        CHECK(ch->firstPersonArm);                            // same arm a native first-person hand raises
        CHECK(ch->fpArmBase == 3);
        CHECK(cam->GetComponent<Camera>()->ignoreObject == p);
        CHECK(cam->GetComponent<Camera>()->nearClip < 0.06f);
        const Vec3 arrived = cam->transform->Position() - p->transform->Position();
        CHECK_NEAR(arrived.y, 1.6f, 0.08f);                   // eye socket, not the orbit look-at
        CHECK(arrived.z < -0.10f);

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
            CHECK(ch->fpArmUp == 1.0f);
            CHECK(ch->Part(2) && ch->Part(2)->active && !ch->Part(2)->firstPersonViewmodel);
            GameObject* neck = neckOf();
            CHECK(neck && neck->active && !neck->firstPersonViewmodel);
            CHECK(ch->Part(1) && ch->Part(1)->firstPersonViewmodel);
            CHECK(camera->ignoreObject == p);
            CHECK(camera->nearClip < 0.06f);
            Vec3 eye = cam->transform->Position() - p->transform->Position();
            CHECK_NEAR(eye.y, 1.6f, 0.08f);
            CHECK(eye.z < -0.10f);

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
            CHECK(ch->Part(1) && ch->Part(1)->firstPersonViewmodel);
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

    // --- Shrinking the head drops the skull out of the mesh ---
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
        const float hidden = maxY();
        CHECK(shown > hidden + 0.08f);
        CHECK(hidden < 1.65f);
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
        }
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

    TEST_MAIN_RESULT();
}
