#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("gameplay");

    // --- Mover translates at constant velocity ---
    {
        Scene scene("Mv");
        GameObject* go = scene.CreateGameObject("Bullet");
        go->AddComponent<Mover>()->velocity = {3, 0, 0};
        scene.Start();
        for (int i = 0; i < 10; ++i) scene.Update(0.1f); // ~1 second
        CHECK_NEAR(go->transform->localPosition.x, 3.0f, 0.01f);
    }

    // --- Spinner changes rotation over time ---
    {
        Scene scene("Sp");
        GameObject* go = scene.CreateGameObject("Coin");
        go->AddComponent<Spinner>()->angularVelocity = {0, 0, 180};
        Quat before = go->transform->localRotation;
        scene.Start();
        scene.Update(0.5f); // 90 degrees
        // Rotation should have changed.
        CHECK(!(go->transform->localRotation.x == before.x &&
                go->transform->localRotation.y == before.y &&
                go->transform->localRotation.z == before.z &&
                go->transform->localRotation.w == before.w));
    }

    // --- Lifetime destroys the object after its time runs out ---
    {
        Scene scene("Lt");
        GameObject* go = scene.CreateGameObject("Temp");
        go->AddComponent<Lifetime>()->seconds = 0.3f;
        scene.CreateGameObject("Keep"); // a survivor
        scene.Start();
        (void)go;
        CHECK(scene.Objects().size() == 2);
        for (int i = 0; i < 5; ++i) scene.Update(0.1f); // 0.5s > 0.3s
        CHECK(scene.Objects().size() == 1);
        CHECK(scene.Find("Keep") != nullptr);
        CHECK(scene.Find("Temp") == nullptr);
    }

    // --- CameraFollow eases the camera toward its target, snaps when smoothing=0 ---
    {
        Scene scene("Follow");
        GameObject* target = scene.CreateGameObject("Player");
        target->transform->localPosition = {10, 5, 0};
        GameObject* cam = scene.CreateGameObject("Cam");
        auto* cf = cam->AddComponent<CameraFollow>();
        cf->targetName = "Player";
        cf->smoothing = 0.0f; // instant snap
        scene.Start();
        scene.Update(0.1f);
        CHECK_NEAR(cam->transform->localPosition.x, 10.0f, 0.001f);
        CHECK_NEAR(cam->transform->localPosition.y, 5.0f, 0.001f);

        // With smoothing it should approach but (after one small step) not arrive.
        GameObject* cam2 = scene.CreateGameObject("Cam2");
        auto* cf2 = cam2->AddComponent<CameraFollow>();
        cf2->targetName = "Player";
        cf2->smoothing = 5.0f;
        cf2->offset = {0, 0, 0};
        cam2->transform->localPosition = {0, 0, 0};
        cf2->LateUpdate(0.05f);
        CHECK(cam2->transform->localPosition.x > 0.0f);
        CHECK(cam2->transform->localPosition.x < 10.0f);
    }

    // --- All round-trip through serialization ---
    {
        Scene scene("Ser");
        GameObject* go = scene.CreateGameObject("Obj");
        go->AddComponent<Mover>()->velocity = {1, 2, 3};
        go->AddComponent<Spinner>()->angularVelocity = {10, 20, 30};
        go->AddComponent<Lifetime>()->seconds = 4.5f;
        auto* cf = go->AddComponent<CameraFollow>();
        cf->targetName = "Hero"; cf->offset = {0, 1, -10}; cf->smoothing = 7.0f;

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        GameObject* r = loaded.Find("Obj");
        CHECK(r != nullptr);
        CHECK(r->GetComponent<Mover>() != nullptr);
        CHECK_NEAR(r->GetComponent<Mover>()->velocity.z, 3.0f, 0.001f);
        CHECK_NEAR(r->GetComponent<Spinner>()->angularVelocity.y, 20.0f, 0.001f);
        CHECK_NEAR(r->GetComponent<Lifetime>()->seconds, 4.5f, 0.001f);
        auto* rcf = r->GetComponent<CameraFollow>();
        CHECK(rcf != nullptr);
        CHECK(rcf->targetName == "Hero");
        CHECK_NEAR(rcf->offset.z, -10.0f, 0.001f);
        CHECK_NEAR(rcf->smoothing, 7.0f, 0.001f);
    }

    TEST_MAIN_RESULT();
}
