#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>

using namespace okay;

int main() {
    RUN_SUITE("script_spawn");

    // Build a tiny prefab file to spawn.
    const char* prefabPath = "test_bullet.okayprefab";
    {
        Scene tmp("p");
        GameObject* b = tmp.CreateGameObject("Bullet");
        b->AddComponent<SpriteRenderer>()->color = Color::Red;
        CHECK(SceneSerializer::SaveObjectToFile(*b, prefabPath));
    }

    // --- A script can spawn a prefab at a position ---
    {
        Scene scene("Spawner");
        GameObject* go = scene.CreateGameObject("Gun");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        std::string src =
            "var done = 0;\n"
            "function update(d) {\n"
            "  if (done == 0) { spawn(\"" + std::string(prefabPath) + "\", 3, 4); done = 1; }\n"
            "}\n";
        std::string err;
        CHECK(sc->LoadSource(src, &err));

        scene.Start();
        std::size_t before = scene.Objects().size();
        scene.Update(0.016f);            // spawn requested this frame
        scene.Update(0.016f);            // adopted on next FlushPending
        CHECK(scene.Objects().size() == before + 1);
        GameObject* spawned = scene.Find("Bullet");
        CHECK(spawned != nullptr);
        CHECK_NEAR(spawned->transform->localPosition.x, 3.0f, 0.001f);
        CHECK_NEAR(spawned->transform->localPosition.y, 4.0f, 0.001f);
    }

    // --- A script can destroy its own GameObject ---
    {
        Scene scene("Doomed");
        GameObject* keep = scene.CreateGameObject("Keep");
        (void)keep;
        GameObject* go = scene.CreateGameObject("SelfDestruct");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function update(d) { destroy(); }"));

        scene.Start();
        CHECK(scene.Objects().size() == 2);
        scene.Update(0.016f);            // destroy() queued, removed end of frame
        CHECK(scene.Find("SelfDestruct") == nullptr);
        CHECK(scene.Find("Keep") != nullptr);
    }

    std::remove(prefabPath);
    TEST_MAIN_RESULT();
}
