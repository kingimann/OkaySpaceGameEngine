// A modal UI (open inventory / chest) must freeze camera look + movement. This
// guards the regression where the third-person shooter (and free-roam) controllers
// ignored Input::UICaptured() and kept rotating the camera while a bag was open,
// and the snap that happened when the cursor's coordinate space switched on close.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

int main() {
    RUN_SUITE("uicapture_look");

    // ---- Third-person shooter: look frozen while UICaptured ----
    {
        Scene s("tps"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Player");
        auto* c = go->AddComponent<ThirdPersonShooterController>();
        c->mouseSensitivity = 0.5f;

        Input::SetUICaptured(false);
        Input::FeedMouse(Vec2{100, 100}, 0);
        c->Update(0.016f);                 // establish baseline
        float yaw0 = c->yaw;
        Input::FeedMouse(Vec2{200, 100}, 0);
        c->Update(0.016f);                 // mouse moved 100px right -> camera turns
        CHECK(std::fabs(c->yaw - yaw0) > 1.0f);

        // Now open a bag: look must NOT change however far the mouse moves.
        Input::SetUICaptured(true);
        float yawLocked = c->yaw;
        Input::FeedMouse(Vec2{800, 600}, 0);
        c->Update(0.016f);
        Input::FeedMouse(Vec2{50, 50}, 0);
        c->Update(0.016f);
        CHECK_NEAR(c->yaw, yawLocked, 1e-3f);

        // Close the bag: the FIRST frame must not snap from the big cursor jump.
        Input::SetUICaptured(false);
        Input::FeedMouse(Vec2{55, 55}, 0);   // small real motion after close
        c->Update(0.016f);
        CHECK_NEAR(c->yaw, yawLocked, 0.05f);   // ~no snap (only the 5px settles in)
    }

    // ---- First-person: same guarantee ----
    {
        Scene s("fps"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Player");
        GameObject* cam = s.CreateGameObject("Cam");
        cam->transform->SetParent(go->transform, false);
        cam->AddComponent<Camera>();
        auto* c = go->AddComponent<FirstPersonController>();
        c->lockCursor = false;             // don't fight the headless cursor
        c->mouseSensitivity = 0.5f;

        Input::SetUICaptured(false);
        Input::FeedMouse(Vec2{100, 100}, 0);
        c->Update(0.016f);
        float yaw0 = c->yaw;
        Input::FeedMouse(Vec2{200, 100}, 0);
        c->Update(0.016f);
        CHECK(std::fabs(c->yaw - yaw0) > 1.0f);

        Input::SetUICaptured(true);
        float yawLocked = c->yaw;
        Input::FeedMouse(Vec2{900, 700}, 0);
        c->Update(0.016f);
        CHECK_NEAR(c->yaw, yawLocked, 1e-3f);

        Input::SetUICaptured(false);
        Input::FeedMouse(Vec2{905, 705}, 0);
        c->Update(0.016f);
        CHECK_NEAR(c->yaw, yawLocked, 0.05f);   // no snap on close
    }

    Input::SetUICaptured(false);
    TEST_MAIN_RESULT();
}
