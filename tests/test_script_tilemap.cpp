#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("script_tilemap");

    // --- A script builds a tilemap procedurally on its sibling Tilemap ---
    {
        Scene scene("Gen");
        GameObject* go = scene.CreateGameObject("Level");
        auto* tm = go->AddComponent<Tilemap>();
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() {\n"
            "  tile_resize(8, 4);\n"
            "  for (var x = 0; x < tile_w(); x = x + 1) { set_tile(x, 0, 1); }\n" // ground row
            "  set_tile(3, 2, 7);\n"
            "}\n"));
        scene.Start();
        CHECK(tm->Width() == 8);
        CHECK(tm->Height() == 4);
        CHECK(tm->GetTile(0, 0) == 1);
        CHECK(tm->GetTile(7, 0) == 1);
        CHECK(tm->GetTile(3, 2) == 7);
        CHECK(tm->GetTile(3, 3) == 0);
    }

    // --- get_tile reads back what the script wrote ---
    {
        Scene scene("Read");
        GameObject* go = scene.CreateGameObject("L");
        go->AddComponent<Tilemap>()->Resize(3, 1);
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() { set_tile(1, 0, 5); set_x(get_tile(1, 0)); }"));
        scene.Start();
        CHECK_NEAR(go->transform->localPosition.x, 5.0f, 0.001f);
    }

    TEST_MAIN_RESULT();
}
