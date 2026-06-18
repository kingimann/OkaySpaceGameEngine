#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("script_events");

    // --- on_trigger(): a coin removes itself when the player overlaps it ---
    {
        Scene scene("Pickup");

        GameObject* coin = scene.CreateGameObject("Coin");
        auto* cc = coin->AddComponent<BoxCollider2D>();
        cc->size = {1, 1};
        cc->isTrigger = true;
        auto* sc = coin->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_trigger() { destroy(); }"));

        GameObject* player = scene.CreateGameObject("Player");
        player->AddComponent<BoxCollider2D>()->size = {1, 1}; // overlaps coin at origin

        scene.Start();
        CHECK(scene.Find("Coin") != nullptr);
        scene.Update(0.016f); // physics detects the overlap -> on_trigger -> destroy
        CHECK(scene.Find("Coin") == nullptr);
        CHECK(scene.Find("Player") != nullptr);
    }

    // --- a script with no handlers is unaffected by overlaps ---
    {
        Scene scene("NoHandler");
        GameObject* a = scene.CreateGameObject("A");
        a->AddComponent<BoxCollider2D>()->isTrigger = true;
        a->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "function update(d) { }");
        GameObject* b = scene.CreateGameObject("B");
        b->AddComponent<BoxCollider2D>();
        scene.Start();
        scene.Update(0.016f);
        CHECK(scene.Find("A") != nullptr); // still here; no on_trigger defined
    }

    TEST_MAIN_RESULT();
}
