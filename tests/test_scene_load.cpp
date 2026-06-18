#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>

using namespace okay;

int main() {
    RUN_SUITE("scene_load");

    // Write a "level 2" scene file to load at runtime.
    const char* level2 = "test_level2.okayscene";
    {
        Scene lvl("Level2");
        lvl.CreateGameObject("Goal");
        lvl.CreateGameObject("Door");
        CHECK(SceneSerializer::SaveToFile(lvl, level2));
    }

    // --- RequestLoad swaps the scene at the end of Update ---
    {
        Scene scene("Level1");
        scene.CreateGameObject("Player");
        scene.Start();
        CHECK(scene.Find("Player") != nullptr);

        scene.RequestLoad(level2);
        CHECK(scene.HasPendingLoad());
        scene.Update(0.016f); // load applied at end of this Update
        CHECK(!scene.HasPendingLoad());
        CHECK(scene.Name() == "Level2");
        CHECK(scene.Find("Goal") != nullptr);
        CHECK(scene.Find("Door") != nullptr);
        CHECK(scene.Find("Player") == nullptr); // old scene replaced
    }

    // --- A script can trigger the load via load_scene() ---
    {
        Scene scene("Start");
        GameObject* go = scene.CreateGameObject("Trigger");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function update(d) { load_scene(\"" +
                             std::string(level2) + "\"); }"));
        scene.Start();
        scene.Update(0.016f);
        CHECK(scene.Name() == "Level2");
        CHECK(scene.Find("Goal") != nullptr);
    }

    std::remove(level2);
    TEST_MAIN_RESULT();
}
