#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("gravity");

    // --- Scene gravity round-trips through serialization ---
    {
        Scene scene("G");
        scene.physics().gravity = {0.0f, 0.0f}; // top-down: no gravity
        scene.CreateGameObject("Thing");

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        loaded.physics().gravity = {1.0f, -50.0f}; // ensure it gets overwritten
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        CHECK_NEAR(loaded.physics().gravity.x, 0.0f, 0.001f);
        CHECK_NEAR(loaded.physics().gravity.y, 0.0f, 0.001f);
    }

    // --- A script can change gravity (e.g. flip it) ---
    {
        Scene scene("Flip");
        GameObject* go = scene.CreateGameObject("Mgr");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function start() { set_gravity(0, 20); }"));
        scene.Start();
        CHECK_NEAR(scene.physics().gravity.y, 20.0f, 0.001f);
        CHECK_NEAR(scene.physics().gravity.x, 0.0f, 0.001f);
    }

    TEST_MAIN_RESULT();
}
