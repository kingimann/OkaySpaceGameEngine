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

    // --- Coin Collector: a full playable loop (move onto coin -> score++) ---
    {
        Scene scene("x");
        Templates::CoinCollector(scene);
        CHECK(scene.Find("Player") != nullptr);
        CHECK(scene.Find("HUD") != nullptr);

        int coins = 0;
        for (const auto& up : scene.Objects()) if (up->name == "Coin") ++coins;
        CHECK(coins == 3);

        scene.Start();                 // player start() resets score to 0
        CHECK(Prefs::GetInt("score") == 0);

        // Move the player onto the first coin's position, then step physics.
        GameObject* coin = scene.Find("Coin");
        Vec3 cp = coin->transform->Position();
        scene.Find("Player")->transform->localPosition = cp;
        scene.Update(0.016f);          // trigger fires -> coin scores + destroys

        CHECK(Prefs::GetInt("score") == 1);
        // One coin consumed; two remain.
        int left = 0;
        for (const auto& up : scene.Objects()) if (up->name == "Coin") ++left;
        CHECK(left == 2);

        // The HUD script reflects the score in its text.
        scene.Update(0.016f);
        CHECK(scene.Find("HUD")->GetComponent<TextRenderer>()->text == "Score: 1");
    }

    TEST_MAIN_RESULT();
}
