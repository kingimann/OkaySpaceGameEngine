#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("remove_component");

    Scene scene("R");
    GameObject* go = scene.CreateGameObject("Obj");

    auto* sr = go->AddComponent<SpriteRenderer>();
    CHECK(go->GetComponent<SpriteRenderer>() == sr);
    CHECK(go->RemoveComponent<SpriteRenderer>());
    CHECK(go->GetComponent<SpriteRenderer>() == nullptr);
    CHECK(!go->RemoveComponent<SpriteRenderer>()); // already gone

    // The Transform is permanent.
    CHECK(!go->RemoveComponent(go->transform));
    CHECK(go->transform != nullptr);

    // Removing the camera clears Scene::mainCamera.
    GameObject* camObj = scene.CreateGameObject("Cam");
    auto* cam = camObj->AddComponent<Camera>();
    scene.Start();
    CHECK(scene.mainCamera == cam);
    CHECK(camObj->RemoveComponent<Camera>());
    CHECK(scene.mainCamera == nullptr);

    // After removal, stepping the scene must not touch freed components.
    GameObject* go2 = scene.CreateGameObject("Obj2");
    go2->AddComponent<Rigidbody2D>();
    scene.Update(0.016f);
    CHECK(go2->RemoveComponent<Rigidbody2D>());
    scene.Update(0.016f); // would crash if the raw pointer dangled
    CHECK(go2->GetComponent<Rigidbody2D>() == nullptr);

    TEST_MAIN_RESULT();
}
