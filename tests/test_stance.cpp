#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// Run / crouch / prone stance handling on the first- and third-person controllers,
// plus the scene round-trip of the new options. Input is driven directly via
// Input::FeedKeys (the windowed runtime does the same from SDL).
int main() {
    RUN_SUITE("stance");

    auto press = [](char k) { Input::FeedKeys({k}); };
    auto release = []() { Input::FeedKeys({}); };

    // --- First person: crouch / prone toggle drives speed, anim and eye height ---
    {
        Scene s("FP"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        auto* fp = player->AddComponent<FirstPersonController>();
        fp->toggleStance = true;
        auto* ch = player->AddComponent<Character>();
        GameObject* cam = s.CreateGameObject("Cam");
        cam->transform->SetParent(player->transform);
        cam->AddComponent<Camera>();
        cam->transform->localPosition = {0, fp->standEyeHeight, 0};
        s.Start();

        // Defaults: stance starts standing; sprint defaults to Shift.
        CHECK(fp->stance() == FirstPersonController::Stance::Stand);
        CHECK(fp->sprintKey == Input::KeyShift);

        // Tap crouch -> Crouch (anim 6), eye height eases down over a few frames.
        release();          s.Update(0.016f);
        press(fp->crouchKey); s.Update(0.016f);
        CHECK(fp->stance() == FirstPersonController::Stance::Crouch);
        CHECK(ch->anim == 6);
        release();
        for (int i = 0; i < 120; ++i) s.Update(0.016f);
        CHECK_NEAR(cam->transform->localPosition.y, fp->crouchEyeHeight, 0.02f);

        // Tap prone -> Prone (anim 7), even lower.
        press(fp->proneKey); s.Update(0.016f);
        CHECK(fp->stance() == FirstPersonController::Stance::Prone);
        CHECK(ch->anim == 7);
        release();
        for (int i = 0; i < 120; ++i) s.Update(0.016f);
        CHECK_NEAR(cam->transform->localPosition.y, fp->proneEyeHeight, 0.02f);

        // Tap prone again -> back to standing.
        press(fp->proneKey); s.Update(0.016f);
        CHECK(fp->stance() == FirstPersonController::Stance::Stand);
        release();
    }

    // --- First person hold-mode: crouch only while the key is held ---
    {
        Scene s("FPH"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        auto* fp = player->AddComponent<FirstPersonController>();
        fp->toggleStance = false;
        s.Start();
        press(fp->crouchKey); s.Update(0.016f);
        CHECK(fp->stance() == FirstPersonController::Stance::Crouch);
        release(); s.Update(0.016f);
        CHECK(fp->stance() == FirstPersonController::Stance::Stand);
    }

    // --- Third person: crouch toggle drives anim ---
    {
        Scene s("TP"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        auto* tp = player->AddComponent<ThirdPersonController>();
        auto* ch = player->AddComponent<Character>();
        GameObject* cam = s.CreateGameObject("Cam");
        cam->AddComponent<Camera>();
        s.mainCamera = cam->GetComponent<Camera>();
        s.Start();

        release(); s.Update(0.016f);
        press(tp->crouchKey); s.Update(0.016f);
        CHECK(tp->stance() == ThirdPersonController::Stance::Crouch);
        CHECK(ch->anim == 6);
        release();
    }

    // --- Serialization round-trip of the new run/stance fields ---
    {
        Scene s("RT"); s.physicsEnabled = false;
        GameObject* a = s.CreateGameObject("FP");
        auto* fp = a->AddComponent<FirstPersonController>();
        fp->sprintKey = 'c'; fp->toggleRun = true; fp->crouchKey = 'x'; fp->proneKey = 'v';
        fp->toggleStance = false; fp->crouchSpeed = 3.3f; fp->proneSpeed = 1.7f;
        fp->standEyeHeight = 1.55f; fp->crouchEyeHeight = 0.8f; fp->proneEyeHeight = 0.3f; fp->stanceLerp = 9.0f;

        GameObject* b = s.CreateGameObject("TP");
        auto* tp = b->AddComponent<ThirdPersonController>();
        tp->sprintKey = Input::KeyCtrl; tp->toggleRun = true; tp->crouchKey = 'c'; tp->proneKey = 'z';
        tp->toggleStance = false; tp->crouchSpeed = 2.9f; tp->proneSpeed = 1.2f;
        tp->crouchHeightDrop = 0.7f; tp->proneHeightDrop = 1.3f; tp->stanceLerp = 8.0f;

        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* fp2 = s2.Find("FP") ? s2.Find("FP")->GetComponent<FirstPersonController>() : nullptr;
        auto* tp2 = s2.Find("TP") ? s2.Find("TP")->GetComponent<ThirdPersonController>() : nullptr;
        CHECK(fp2 && fp2->sprintKey == 'c' && fp2->toggleRun && fp2->crouchKey == 'x' && fp2->proneKey == 'v');
        CHECK(fp2 && !fp2->toggleStance);
        CHECK(fp2 && fp2->crouchEyeHeight > 0.79f && fp2->crouchEyeHeight < 0.81f);
        CHECK(tp2 && tp2->sprintKey == Input::KeyCtrl && tp2->toggleRun && tp2->crouchKey == 'c' && tp2->proneKey == 'z');
        CHECK(tp2 && tp2->proneHeightDrop > 1.29f && tp2->proneHeightDrop < 1.31f);

        // Lean keys/params survive the round-trip too.
        fp->leanLeftKey = 'q'; fp->leanRightKey = 'e'; fp->leanAngle = 18.0f; fp->leanOffset = 0.5f;
        tp->leanLeftKey = 'q'; tp->leanRightKey = 'e'; tp->leanAngle = 9.0f;
        Scene s3("y"); SceneSerializer::Deserialize(s3, SceneSerializer::Serialize(s));
        auto* fp3 = s3.Find("FP") ? s3.Find("FP")->GetComponent<FirstPersonController>() : nullptr;
        auto* tp3 = s3.Find("TP") ? s3.Find("TP")->GetComponent<ThirdPersonController>() : nullptr;
        CHECK(fp3 && fp3->leanLeftKey == 'q' && fp3->leanRightKey == 'e');
        CHECK(fp3 && fp3->leanAngle > 17.9f && fp3->leanAngle < 18.1f);
        CHECK(tp3 && tp3->leanAngle > 8.9f && tp3->leanAngle < 9.1f);
    }

    // Lean easing: holding Q/E ramps the lean toward -1 / +1.
    {
        Scene s("LN"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        auto* fp = player->AddComponent<FirstPersonController>();
        GameObject* cam = s.CreateGameObject("Cam");
        cam->transform->SetParent(player->transform);
        cam->AddComponent<Camera>();
        s.Start();
        for (int i = 0; i < 60; ++i) { Input::FeedKeys({fp->leanRightKey}); s.Update(0.016f); }
        CHECK(fp->lean() > 0.8f);                  // leaned right
        for (int i = 0; i < 60; ++i) { Input::FeedKeys({fp->leanLeftKey}); s.Update(0.016f); }
        CHECK(fp->lean() < -0.8f);                 // leaned left
        Input::FeedKeys({});
    }

    release();

    // --- Free-roam fly camera: WASD flies along the look dir, Space rises, round-trip ---
    {
        Scene s("FR"); s.physicsEnabled = false;
        GameObject* cam = s.CreateGameObject("FlyCam");
        auto* fr = cam->AddComponent<FreeRoamController>();
        fr->lockCursor = true;             // always looks; no RMB needed
        fr->acceleration = 0.0f;           // instant velocity for a deterministic test
        s.Start();

        // Facing default (yaw 0, pitch 0) -> forward is -Z. Holding W flies -Z.
        for (int i = 0; i < 10; ++i) { Input::FeedKeys({'w'}); s.Update(0.016f); }
        CHECK(cam->transform->Position().z < -0.1f);

        // Space rises (+Y).
        float y0 = cam->transform->Position().y;
        for (int i = 0; i < 10; ++i) { Input::FeedKeys({' '}); s.Update(0.016f); }
        CHECK(cam->transform->Position().y > y0 + 0.1f);
        Input::FeedKeys({});

        // Settings round-trip.
        fr->moveSpeed = 13.0f; fr->boostMultiplier = 4.0f; fr->invertY = true; fr->downKey = 'x';
        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* fr2 = s2.Find("FlyCam") ? s2.Find("FlyCam")->GetComponent<FreeRoamController>() : nullptr;
        CHECK(fr2 != nullptr);
        if (fr2) {
            CHECK_NEAR(fr2->moveSpeed, 13.0f, 1e-3f);
            CHECK_NEAR(fr2->boostMultiplier, 4.0f, 1e-3f);
            CHECK(fr2->invertY && fr2->downKey == 'x' && fr2->lockCursor);
        }
    }

    TEST_MAIN_RESULT();
}
