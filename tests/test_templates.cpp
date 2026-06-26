#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>

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

    // --- 3D Platformer: perspective camera, lit, physics player on ground ---
    {
        Scene scene("x");
        Templates::Platformer3D(scene);

        GameObject* cam = scene.Find("MainCamera");
        CHECK(cam != nullptr);
        CHECK(cam->GetComponent<Camera>()->projection == Camera::Projection::Perspective);
        CHECK(scene.Find("Directional Light")->GetComponent<Light>() != nullptr);

        GameObject* ground = scene.Find("Ground");
        CHECK(ground != nullptr);
        CHECK(ground->GetComponent<BoxCollider3D>() != nullptr);
        CHECK(ground->GetComponent<Rigidbody3D>()->bodyType == Rigidbody3D::BodyType::Static);

        GameObject* player = scene.Find("Player");
        CHECK(player != nullptr);
        CHECK(player->GetComponent<Rigidbody3D>() != nullptr);
        CHECK(player->GetComponent<BoxCollider3D>() != nullptr);
        CHECK(player->GetComponent<CharacterController3D>() != nullptr);

        // Survives a serialization round-trip (so Build Game works).
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("y"); std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        CHECK(loaded.Find("Player")->GetComponent<CharacterController3D>() != nullptr);
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

    // --- Vehicle 3D: a drivable car (controller + physics) chasing camera ---
    {
        Scene scene("x");
        Templates::Vehicle3D(scene);

        GameObject* car = scene.Find("Car");
        CHECK(car != nullptr);
        CHECK(car->GetComponent<VehicleController>() != nullptr);
        CHECK(car->GetComponent<VehicleController>()->followCamera == true);
        CHECK(car->GetComponent<Rigidbody3D>() != nullptr);
        CHECK(car->GetComponent<BoxCollider3D>() != nullptr);

        GameObject* ground = scene.Find("Ground");
        CHECK(ground != nullptr);
        CHECK(ground->GetComponent<Rigidbody3D>()->bodyType == Rigidbody3D::BodyType::Static);

        GameObject* cam = scene.Find("Main Camera");
        CHECK(cam != nullptr);
        CHECK(cam->GetComponent<Camera>()->projection == Camera::Projection::Perspective);
        CHECK(scene.Find("Directional Light")->GetComponent<Light>() != nullptr);

        // Survives a serialization round-trip (so Build Game works).
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("y"); std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        CHECK(loaded.Find("Car")->GetComponent<VehicleController>() != nullptr);
    }

    // --- Vehicle 2D: a top-down car (controller + physics) + follow camera ---
    {
        Scene scene("x");
        Templates::Vehicle2D(scene);

        GameObject* car = scene.Find("Car");
        CHECK(car != nullptr);
        CHECK(car->GetComponent<VehicleController2D>() != nullptr);
        CHECK(car->GetComponent<Rigidbody2D>() != nullptr);
        CHECK(car->GetComponent<Rigidbody2D>()->gravityScale == 0.0f);
        CHECK(car->GetComponent<BoxCollider2D>() != nullptr);

        GameObject* cam = scene.Find("Main Camera");
        CHECK(cam != nullptr);
        CHECK(cam->GetComponent<Camera>()->projection == Camera::Projection::Orthographic);
        auto* follow = cam->GetComponent<CameraFollow>();
        CHECK(follow != nullptr);
        CHECK(follow->targetName == "Car");

        // Survives a serialization round-trip (so Build Game works).
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("y"); std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        CHECK(loaded.Find("Car")->GetComponent<VehicleController2D>() != nullptr);
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

    // --- Main Menu: panel + title + Start button that loads the game on click ---
    {
        Scene scene("x");
        Templates::MainMenu(scene);
        CHECK(scene.Find("Panel")->GetComponent<UIPanel>() != nullptr);
        CHECK(scene.Find("Title")->GetComponent<TextRenderer>() != nullptr);
        GameObject* start = scene.Find("StartButton");
        CHECK(start->GetComponent<UIButton>() != nullptr);
        CHECK(start->GetComponent<ScriptComponent>() != nullptr);

        // Provide the target scene, then "click" Start and verify it requests a load.
        Scene game("Game");
        game.CreateGameObject("Hero");
        CHECK(SceneSerializer::SaveToFile(game, "game.okayscene"));

        scene.Start();
        auto* b = start->GetComponent<UIButton>();
        Input::FeedMouse({b->position.x + 5, b->position.y + 5}, 0);
        Input::FeedMouse({b->position.x + 5, b->position.y + 5}, 1u << 0);
        // Click -> on_click -> load_scene; the load is applied at the end of the
        // same Update (deferred after iteration), so one step swaps the scene.
        scene.Update(0.016f);
        CHECK(scene.Name() == "Game");
        CHECK(scene.Find("Hero") != nullptr);
        std::remove("game.okayscene");
    }

    // --- Snake: a full game in OkayScript on one Tilemap object ---
    {
        Scene scene("x");
        Templates::Snake(scene);
        GameObject* board = scene.Find("Board");
        CHECK(board != nullptr);
        auto* tm = board->GetComponent<Tilemap>();
        CHECK(tm != nullptr);
        CHECK(board->GetComponent<ScriptComponent>() != nullptr);

        scene.Start();
        CHECK(tm->Width() == 16 && tm->Height() == 16);
        // Board is drawn: snake cells (id 1) and one food cell (id 2).
        int snake = 0, food = 0;
        for (int y = 0; y < tm->Height(); ++y)
            for (int x = 0; x < tm->Width(); ++x) {
                int t = tm->GetTile(x, y);
                if (t == 1) ++snake; else if (t == 2) ++food;
            }
        CHECK(snake >= 1);   // snake body drawn
        CHECK(food == 1);    // exactly one food

        // Drive it: hold right and step past the move interval a few times.
        for (int i = 0; i < 20; ++i) { Input::FeedKeys({'d'}); scene.Update(0.1f); }
        // Still running: snake tiles are still on the board (didn't vanish/crash).
        int snake2 = 0;
        for (int y = 0; y < tm->Height(); ++y)
            for (int x = 0; x < tm->Width(); ++x)
                if (tm->GetTile(x, y) == 1) ++snake2;
        CHECK(snake2 >= 1);
    }

    TEST_MAIN_RESULT();
}
