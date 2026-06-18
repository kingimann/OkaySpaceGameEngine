#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

int main() {
    RUN_SUITE("script_camera");

    // --- set_cam / move_cam / cam_x|y and zoom drive the main camera ---
    {
        Scene scene("Cam");
        GameObject* camObj = scene.CreateGameObject("MainCam");
        auto* cam = camObj->AddComponent<Camera>();
        cam->orthographicSize = 5.0f;

        GameObject* director = scene.CreateGameObject("Director");
        auto* sc = director->AddComponent<ScriptComponent>("okayscript");
        // start positions the camera; update nudges it and reads values back
        // (onto the director's own transform so the test can observe them).
        CHECK(sc->LoadSource(
            "function start() { set_cam(3, 4); set_cam_zoom(8); }\n"
            "function update(dt) { move_cam(1, 0); set_x(cam_x()); set_y(cam_zoom()); }\n"));
        scene.Start();
        CHECK(scene.mainCamera == cam);
        CHECK_NEAR(camObj->transform->localPosition.x, 3.0f, 0.001f);
        CHECK_NEAR(camObj->transform->localPosition.y, 4.0f, 0.001f);
        CHECK_NEAR(cam->orthographicSize, 8.0f, 0.001f);

        scene.Update(0.016f);                       // move_cam(1,0) -> cam.x = 4
        CHECK_NEAR(camObj->transform->localPosition.x, 4.0f, 0.001f);
        // Readback: cam_x() and cam_zoom() observed by the director script.
        CHECK_NEAR(director->transform->localPosition.x, 4.0f, 0.001f);
        CHECK_NEAR(director->transform->localPosition.y, 8.0f, 0.001f);
    }

    // --- look_at rotates (about Z) so the object's local +X faces the target ---
    {
        Scene scene("Aim");
        GameObject* target = scene.CreateGameObject("Target");
        target->transform->localPosition = {0.0f, 5.0f, 0.0f};   // straight up (+Y)

        GameObject* turret = scene.CreateGameObject("Turret");
        auto* sc = turret->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function start() { look_at(\"Target\"); }"));
        scene.Start();

        // Facing +Y means a +90 deg Z rotation: local +X (Right) now points +Y.
        Vec3 right = turret->transform->Right();
        CHECK_NEAR(right.x, 0.0f, 0.01f);
        CHECK_NEAR(right.y, 1.0f, 0.01f);
    }

    TEST_MAIN_RESULT();
}
