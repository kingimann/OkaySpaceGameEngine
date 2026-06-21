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

    // --- on_trigger_exit(): fires when the overlap ends ---
    {
        Scene scene("TriggerExit");
        GameObject* a = scene.CreateGameObject("A");
        a->AddComponent<BoxCollider2D>()->isTrigger = true;
        auto* sc = a->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "var entered = 0; var exited = 0;\n"
            "function on_trigger_enter() { entered = entered + 1; }\n"
            "function on_trigger_exit() { exited = exited + 1; }"));
        GameObject* b = scene.CreateGameObject("B");
        b->AddComponent<BoxCollider2D>()->size = {1, 1}; // overlaps A at origin

        scene.Start();
        scene.Update(0.016f);                       // overlap -> enter
        CHECK(sc->VM()->GetGlobal("entered").AsFloat() == 1.0f);
        b->transform->SetPosition({100, 0, 0});     // move far away
        scene.Update(0.016f);                       // separation -> exit
        CHECK(sc->VM()->GetGlobal("exited").AsFloat() == 1.0f);
    }

    // --- on_mouse_down / on_click: press + release over a sprite ---
    {
        UICanvas::Set(800, 600);
        Scene scene("Mouse");
        GameObject* camO = scene.CreateGameObject("Cam");
        auto* cam = camO->AddComponent<Camera>();
        cam->projection = Camera::Projection::Orthographic;
        cam->orthographicSize = 5.0f; cam->main = true;
        scene.mainCamera = cam;

        GameObject* obj = scene.CreateGameObject("Clickable");
        obj->AddComponent<SpriteRenderer>()->size = {4, 4}; // big target at origin
        auto* sc = obj->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "var downs = 0; var clicks = 0;\n"
            "function on_mouse_down() { downs = downs + 1; }\n"
            "function on_click() { clicks = clicks + 1; }"));
        scene.Start();

        Input::FeedMouse(Vec2{400, 300}, 1);   // center of view = world origin, button down
        scene.Update(0.016f);                  // press edge -> on_mouse_down
        CHECK(sc->VM()->GetGlobal("downs").AsFloat() == 1.0f);
        Input::FeedMouse(Vec2{400, 300}, 0);   // release over the same object
        scene.Update(0.016f);                  // release edge -> on_click
        CHECK(sc->VM()->GetGlobal("clicks").AsFloat() == 1.0f);
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
