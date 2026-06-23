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
        tp->cameraDamping = 0.0f;                            // instant, for a clean check
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

    TEST_MAIN_RESULT();
}
