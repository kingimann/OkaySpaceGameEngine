#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("templates");

    // --- Platformer starter wires camera-follow, a physics player, and a coin ---
    {
        Scene scene("x");
        Templates::Platformer(scene);

        GameObject* cam = scene.Find("MainCamera");
        CHECK(cam != nullptr);
        CHECK(cam->GetComponent<Camera>() != nullptr);
        auto* follow = cam->GetComponent<CameraFollow>();
        CHECK(follow != nullptr);
        CHECK(follow->targetName == "Player");

        GameObject* player = scene.Find("Player");
        CHECK(player != nullptr);
        CHECK(player->GetComponent<SpriteRenderer>() != nullptr);
        CHECK(player->GetComponent<Rigidbody2D>() != nullptr);
        CHECK(player->GetComponent<BoxCollider2D>() != nullptr);

        GameObject* coin = scene.Find("Coin");
        CHECK(coin != nullptr);
        CHECK(coin->GetComponent<Spinner>() != nullptr);

        CHECK(scene.Find("Ground") != nullptr);

        // It should survive a serialization round-trip (so Build Game works).
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("y");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        CHECK(loaded.Find("Player") != nullptr);
        CHECK(loaded.Find("MainCamera")->GetComponent<CameraFollow>() != nullptr);
    }

    // --- Top-down starter builds a scripted player and walls ---
    {
        Scene scene("x");
        Templates::TopDown(scene);

        GameObject* player = scene.Find("Player");
        CHECK(player != nullptr);
        CHECK(player->GetComponent<ScriptComponent>() != nullptr);
        CHECK(scene.Find("MainCamera")->GetComponent<CameraFollow>() != nullptr);
        CHECK(scene.Find("WallA") != nullptr);
        CHECK(scene.Find("WallB") != nullptr);

        // The player's script should actually move it under input.
        scene.Start();
        Input::FeedKeys({'d'}); // hold right
        Vec3 before = player->transform->localPosition;
        for (int i = 0; i < 5; ++i) { Input::FeedKeys({'d'}); scene.Update(0.1f); }
        CHECK(player->transform->localPosition.x > before.x);
    }

    TEST_MAIN_RESULT();
}
