#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("script_physics");

    // A wall at the origin with a 2x2 box collider.
    auto makeWall = [](Scene& s) {
        GameObject* wall = s.CreateGameObject("Wall");
        wall->transform->localPosition = {0, 0, 0};
        auto* bc = wall->AddComponent<BoxCollider2D>();
        bc->size = {2, 2};
    };

    // --- overlap(x, y): inside the wall is a hit, far away is not.
    // The probe encodes results in its transform: x=1 if inside, y=1 if "outside" hit.
    {
        Scene scene("Ov");
        makeWall(scene);
        GameObject* probe = scene.CreateGameObject("Probe");
        auto* sc = probe->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() {\n"
            "  if (overlap(0, 0))   { set_x(1); }\n"
            "  if (overlap(50, 50)) { set_y(1); }\n"
            "}\n"));
        scene.Start();
        CHECK_NEAR(probe->transform->localPosition.x, 1.0f, 0.001f); // inside -> hit
        CHECK_NEAR(probe->transform->localPosition.y, 0.0f, 0.001f); // outside -> miss
    }

    // --- raycast_hit(): a ray toward the wall hits, away from it misses ---
    {
        Scene scene("Rc");
        makeWall(scene);
        GameObject* probe = scene.CreateGameObject("Probe");
        auto* sc = probe->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() {\n"
            "  if (raycast_hit(-10, 0, 1, 0, 100))  { set_x(1); }\n"  // toward +X
            "  if (raycast_hit(-10, 0, -1, 0, 100)) { set_y(1); }\n"  // away -X
            "}\n"));
        scene.Start();
        CHECK_NEAR(probe->transform->localPosition.x, 1.0f, 0.001f); // toward -> hit
        CHECK_NEAR(probe->transform->localPosition.y, 0.0f, 0.001f); // away -> miss
    }

    TEST_MAIN_RESULT();
}
