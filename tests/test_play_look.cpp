// Play look: a captured mouse must not pitch the orbit through the floor, the
// first sample after a cursor grab is not a look, and pitch stays inside the
// inspector limits. The same rules hold for the first-person eye.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Input/Cursor.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Render/Mesh.hpp"
#include <cmath>
#include <cstdio>

using namespace okay;

static void Floor(Scene& sc) {
    GameObject* g = sc.CreateGameObject("Ground");
    g->transform->localPosition = {0.0f, -0.5f, 0.0f};
    g->AddComponent<BoxCollider3D>()->size = {40.0f, 1.0f, 40.0f};
    g->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    auto* mr = g->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Plane(40.0f);
}

static void Wall(Scene& sc) {
    GameObject* w = sc.CreateGameObject("Wall");
    w->transform->localPosition = {0.0f, 1.5f, 2.2f};
    w->AddComponent<BoxCollider3D>()->size = {6.0f, 4.0f, 0.6f};
    w->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
}

static int Settle(Scene& sc, int frames = 8) {
    const float dt = 1.0f / 60.0f;
    Input::FeedMouse({400.0f, 300.0f}, 0);
    Input::FeedKeys({});
    for (int i = 0; i < frames; ++i) {
        Input::FeedMouse({400.0f, 300.0f}, 0);
        Input::FeedKeys({});
        sc.Update(dt);
    }
    return frames;
}

int main() {
    RUN_SUITE("play_look");
    Cursor::Capture(false);

    // A low orbit on a floor stays above the slab. Pitch is allowed down to
    // -35, which at full distance would put the lens under the ground.
    {
        Scene sc("ground");
        sc.physicsEnabled = true;
        Floor(sc);
        GameObject* player = Templates::AddThirdPersonPlayer(sc, {0.0f, 0.0f, 0.0f});
        auto* tp = player->GetComponent<ThirdPersonController>();
        tp->cursorMode = 0;
        tp->lockCursor = false;
        tp->minPitch = -35.0f;
        tp->pitch = -35.0f;
        tp->cameraCollision = true;
        sc.Start();
        Settle(sc, 5);
        tp->pitch = -35.0f;
        for (int i = 0; i < 20; ++i) {
            Input::FeedMouse({400.0f, 300.0f}, 0);
            Input::FeedKeys({});
            sc.Update(1.0f / 60.0f);
        }
        Camera* cam = sc.mainCamera;
        CHECK(cam && cam->transform);
        const float y = cam->transform->Position().y;
        std::printf("  low orbit camera y %.3f pitch %.2f\n", y, tp->pitch);
        CHECK(tp->pitch >= tp->minPitch - 0.01f);
        CHECK(tp->pitch <= tp->maxPitch + 0.01f);
        CHECK(y > 0.30f);
    }

    // Pitch clamp, and one captured frame cannot spend the whole window.
    {
        Scene sc("clamp");
        sc.physicsEnabled = true;
        Floor(sc);
        GameObject* player = Templates::AddThirdPersonPlayer(sc, {0.0f, 0.0f, 0.0f});
        auto* tp = player->GetComponent<ThirdPersonController>();
        tp->cursorMode = 1;
        tp->minPitch = -20.0f;
        tp->maxPitch = 50.0f;
        tp->pitch = 18.0f;
        sc.Start();
        Cursor::Capture(true);
        Input::FeedMouse({400.0f, 300.0f}, 0);
        Input::FeedKeys({});
        sc.Update(1.0f / 60.0f);
        Input::FeedMouse({400.0f, 300.0f}, 0);
        sc.Update(1.0f / 60.0f);
        const float before = tp->pitch;
        // Mouse down raises orbit pitch. 400 px is under the warp drop, and
        // still far more than one captured frame is allowed to spend.
        Input::FeedMouse({400.0f, 700.0f}, 0);
        Input::FeedKeys({});
        sc.Update(1.0f / 60.0f);
        const float step = tp->pitch - before;
        std::printf("  captured step %.2f -> %.2f (limit %.2f)\n", before, tp->pitch, tp->maxPitch);
        CHECK(tp->pitch <= tp->maxPitch + 0.01f);
        CHECK(tp->pitch >= tp->minPitch - 0.01f);
        CHECK(step > 0.5f);
        CHECK(tp->pitch <= tp->maxPitch + 0.01f);
        // The other way, into the low stop.
        tp->pitch = -15.0f;
        Input::FeedMouse({400.0f, 700.0f}, 0);
        sc.Update(1.0f / 60.0f);
        Input::FeedMouse({400.0f, 400.0f}, 0);
        sc.Update(1.0f / 60.0f);
        for (int i = 0; i < 40; ++i) {
            Input::FeedMouse({400.0f, 100.0f - (i + 1) * 30.0f}, 0);
            sc.Update(1.0f / 60.0f);
        }
        std::printf("  pitched down to %.2f (min %.2f) camera y %.3f\n",
                    tp->pitch, tp->minPitch,
                    sc.mainCamera->transform->Position().y);
        CHECK(tp->pitch >= tp->minPitch - 0.01f);
        CHECK(sc.mainCamera->transform->Position().y > 0.30f);
        Cursor::Capture(false);
    }

    // The frame that grabs the cursor often has no delta. The warp arrives on
    // the next poll and must not be a look. The sample after that still is.
    {
        Scene sc("handoff");
        sc.physicsEnabled = true;
        Floor(sc);
        GameObject* player = Templates::AddThirdPersonPlayer(sc, {0.0f, 0.0f, 0.0f});
        auto* tp = player->GetComponent<ThirdPersonController>();
        tp->cursorMode = 0;
        tp->lockCursor = false;
        sc.Start();
        Input::FeedMouse({200.0f, 200.0f}, 0);
        Input::FeedKeys({});
        sc.Update(1.0f / 60.0f);
        Input::FeedMouse({200.0f, 200.0f}, 0);
        sc.Update(1.0f / 60.0f);
        const float pitch = tp->pitch;
        const float yaw = tp->yaw;
        NoteMouseSpaceChanged();
        Input::FeedMouse({200.0f, 200.0f}, 0);   // the grab itself: nothing moved
        sc.Update(1.0f / 60.0f);
        Input::FeedMouse({200.0f, 520.0f}, 0);   // the warp, one poll later
        sc.Update(1.0f / 60.0f);
        std::printf("  handoff pitch %.2f -> %.2f yaw %.2f -> %.2f\n",
                    pitch, tp->pitch, yaw, tp->yaw);
        CHECK_NEAR(tp->pitch, pitch, 0.05f);
        CHECK_NEAR(tp->yaw, yaw, 0.05f);
        Input::FeedMouse({200.0f, 520.0f}, 0);
        sc.Update(1.0f / 60.0f);
        Input::FeedMouse({240.0f, 520.0f}, 0);   // a real look
        sc.Update(1.0f / 60.0f);
        CHECK(std::fabs(tp->yaw - yaw) > 2.0f);
    }

    // A wall between the pivot and the lens pulls the camera in, clear of it.
    {
        Scene sc("wall");
        sc.physicsEnabled = true;
        Floor(sc);
        Wall(sc);
        GameObject* player = Templates::AddThirdPersonPlayer(sc, {0.0f, 0.0f, 0.0f});
        auto* tp = player->GetComponent<ThirdPersonController>();
        tp->cursorMode = 0;
        tp->lockCursor = false;
        tp->cameraCollision = true;
        tp->pitch = 10.0f;
        sc.Start();
        Settle(sc, 10);
        Camera* cam = sc.mainCamera;
        const float z = cam->transform->Position().z;
        std::printf("  wall pull-in camera z %.3f (wall occupies 1.9..2.5)\n", z);
        CHECK(z < 1.85f);
        CHECK(z > 0.2f);
        CHECK(cam->transform->Position().y > 0.30f);
    }

    // First person: pitch stays in range, the eye stays above the feet, and
    // the late warp after a grab is not a look.
    {
        Scene sc("fp");
        sc.physicsEnabled = true;
        Floor(sc);
        GameObject* player = Templates::AddFirstPersonPlayer(sc, {0.0f, 0.0f, 0.0f});
        auto* fp = player->GetComponent<FirstPersonController>();
        fp->lockCursor = true;
        fp->minPitch = -50.0f;
        fp->maxPitch = 60.0f;
        sc.Start();
        Cursor::Capture(true);
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < 3; ++i) {
            Input::FeedMouse({300.0f, 300.0f}, 0);
            Input::FeedKeys({});
            sc.Update(dt);
        }
        Input::FeedMouse({300.0f, 340.0f}, 0);   // may be the sample a pending grab drops
        sc.Update(dt);
        const float before = fp->pitch;
        Input::FeedMouse({300.0f, 260.0f}, 0);   // mouse up: look up
        sc.Update(dt);
        std::printf("  fp look-up %.2f -> %.2f\n", before, fp->pitch);
        CHECK(fp->pitch <= fp->maxPitch + 0.01f);
        CHECK(fp->pitch >= fp->minPitch - 0.01f);
        CHECK(fp->pitch > before + 0.5f);
        CHECK(fp->pitch <= fp->maxPitch + 0.01f);
        const float held = fp->pitch;
        NoteMouseSpaceChanged();
        Input::FeedMouse({300.0f, 260.0f}, 0);   // grab frame, no movement
        sc.Update(dt);
        Input::FeedMouse({300.0f, 500.0f}, 0);   // warp, next poll
        sc.Update(dt);
        std::printf("  fp handoff %.2f -> %.2f\n", held, fp->pitch);
        CHECK_NEAR(fp->pitch, held, 0.05f);
        for (int i = 0; i < 30; ++i) {
            Input::FeedMouse({300.0f, 900.0f + (i + 1) * 40.0f}, 0);
            sc.Update(1.0f / 60.0f);
        }
        Camera* cam = sc.mainCamera;
        std::printf("  fp pitch %.2f eye y %.3f\n", fp->pitch, cam->transform->Position().y);
        CHECK(fp->pitch >= fp->minPitch - 0.01f);
        CHECK(cam->transform->Position().y > 0.30f);
        Cursor::Capture(false);
    }

    TEST_MAIN_RESULT();
}
