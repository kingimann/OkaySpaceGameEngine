#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("ui");

    // --- Contains / hover hit-testing ---
    {
        Scene scene("Hit");
        GameObject* go = scene.CreateGameObject("Btn");
        auto* b = go->AddComponent<UIButton>();
        b->position = {100, 50};
        b->size = {200, 60};
        CHECK(b->Contains({150, 80}));
        CHECK(!b->Contains({50, 80}));
        CHECK(!b->Contains({150, 200}));
    }

    // --- Clicking the button fires the script's on_click() ---
    {
        Scene scene("Click");
        GameObject* go = scene.CreateGameObject("PlayBtn");
        auto* b = go->AddComponent<UIButton>();
        b->position = {0, 0};
        b->size = {100, 40};
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_click() { set_x(42); }"));
        scene.Start();

        // Mouse over the button; press the left button this frame.
        Input::FeedMouse({50, 20}, 0);          // previous: not pressed
        Input::FeedMouse({50, 20}, 1u << 0);    // current: pressed -> Down edge
        scene.Update(0.016f);
        CHECK(b->IsHovered());
        CHECK(b->WasClicked());
        CHECK_NEAR(go->transform->localPosition.x, 42.0f, 0.001f);
    }

    // --- A click outside the button does nothing ---
    {
        Scene scene("Miss");
        GameObject* go = scene.CreateGameObject("Btn");
        auto* b = go->AddComponent<UIButton>();
        b->position = {0, 0};
        b->size = {100, 40};
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_click() { set_x(42); }"));
        scene.Start();
        Input::FeedMouse({500, 500}, 0);
        Input::FeedMouse({500, 500}, 1u << 0);
        scene.Update(0.016f);
        CHECK(!b->WasClicked());
        CHECK_NEAR(go->transform->localPosition.x, 0.0f, 0.001f);
    }

    // --- UIButton round-trips through serialization ---
    {
        Scene scene("Ser");
        GameObject* go = scene.CreateGameObject("B");
        auto* b = go->AddComponent<UIButton>();
        b->label = "Start Game";
        b->position = {12, 34};
        b->size = {220, 56};
        b->color = Color(0.1f, 0.2f, 0.3f, 1.0f);

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("B")->GetComponent<UIButton>();
        CHECK(r != nullptr);
        CHECK(r->label == "Start Game");
        CHECK_NEAR(r->position.x, 12.0f, 0.001f);
        CHECK_NEAR(r->size.y, 56.0f, 0.001f);
        CHECK_NEAR(r->color.b, 0.3f, 0.01f);
    }

    // --- UIProgressBar clamps its value and serializes ---
    {
        Scene scene("PB");
        GameObject* go = scene.CreateGameObject("Health");
        auto* pb = go->AddComponent<UIProgressBar>();
        pb->SetValue(1.5f);  // clamps to 1
        CHECK_NEAR(pb->Fraction(), 1.0f, 0.001f);
        pb->SetValue(-0.5f); // clamps to 0
        CHECK_NEAR(pb->Fraction(), 0.0f, 0.001f);
        pb->value = 0.4f;
        pb->position = {10, 20};
        pb->size = {300, 24};

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("Health")->GetComponent<UIProgressBar>();
        CHECK(r != nullptr);
        CHECK_NEAR(r->value, 0.4f, 0.001f);
        CHECK_NEAR(r->size.x, 300.0f, 0.001f);
    }

    // --- A script sets the progress bar via set_progress() ---
    {
        Scene scene("PBScript");
        GameObject* go = scene.CreateGameObject("Bar");
        auto* pb = go->AddComponent<UIProgressBar>();
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function start() { set_progress(0.25); }"));
        scene.Start();
        CHECK_NEAR(pb->value, 0.25f, 0.001f);
    }

    // --- UIPanel serializes ---
    {
        Scene scene("Pan");
        GameObject* go = scene.CreateGameObject("BG");
        auto* pn = go->AddComponent<UIPanel>();
        pn->position = {5, 6};
        pn->size = {640, 480};
        pn->color = Color(0.1f, 0.1f, 0.1f, 0.8f);

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("BG")->GetComponent<UIPanel>();
        CHECK(r != nullptr);
        CHECK_NEAR(r->size.x, 640.0f, 0.001f);
        CHECK_NEAR(r->color.a, 0.8f, 0.01f);
    }

    TEST_MAIN_RESULT();
}
