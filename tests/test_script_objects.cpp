#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("script_objects");

    // --- A script activates/deactivates other objects by name ---
    {
        Scene scene("S");
        GameObject* menu = scene.CreateGameObject("PauseMenu");
        menu->active = false;
        GameObject* enemy = scene.CreateGameObject("Enemy");

        GameObject* mgr = scene.CreateGameObject("Manager");
        auto* sc = mgr->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() {\n"
            "  activate(\"PauseMenu\");\n"
            "  deactivate(\"Enemy\");\n"
            "}\n"));
        scene.Start();
        CHECK(menu->active);
        CHECK(!enemy->active);
    }

    // --- exists / is_active queries drive behavior ---
    {
        Scene scene("Q");
        GameObject* boss = scene.CreateGameObject("Boss");
        boss->active = false;
        GameObject* probe = scene.CreateGameObject("Probe");
        auto* sc = probe->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() {\n"
            "  if (exists(\"Boss\")) { set_x(1); }\n"        // boss exists -> x = 1
            "  if (exists(\"Ghost\")) { set_x(99); }\n"      // no Ghost -> unchanged
            "  if (is_active(\"Boss\")) { set_y(1); }\n"     // boss inactive -> y stays 0
            "}\n"));
        scene.Start();
        CHECK_NEAR(probe->transform->localPosition.x, 1.0f, 0.001f);
        CHECK_NEAR(probe->transform->localPosition.y, 0.0f, 0.001f);
    }

    TEST_MAIN_RESULT();
}
