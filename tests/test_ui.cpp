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

    // --- UISlider: dragging maps mouse X to value and fires on_change() ---
    {
        Scene scene("Slide");
        GameObject* go = scene.CreateGameObject("Vol");
        auto* sl = go->AddComponent<UISlider>();
        sl->position = {0, 0};
        sl->size = {200, 20};
        sl->minValue = 0.0f; sl->maxValue = 1.0f; sl->value = 0.0f;
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_change() { set_y(slider_value()); }"));
        scene.Start();

        // Press at the midpoint of the track -> value ~0.5.
        Input::FeedMouse({100, 10}, 0);
        Input::FeedMouse({100, 10}, 1u << 0);
        scene.Update(0.016f);
        CHECK(sl->IsDragging());
        CHECK_NEAR(sl->value, 0.5f, 0.01f);
        CHECK_NEAR(go->transform->localPosition.y, 0.5f, 0.01f); // on_change ran

        // Release stops dragging.
        Input::FeedMouse({100, 10}, 0);
        scene.Update(0.016f);
        CHECK(!sl->IsDragging());
    }

    // --- UISlider clamps and respects a custom range; serializes ---
    {
        Scene scene("SlideSer");
        GameObject* go = scene.CreateGameObject("Sens");
        auto* sl = go->AddComponent<UISlider>();
        sl->minValue = 0.0f; sl->maxValue = 10.0f;
        sl->SetValue(15.0f);                       // clamps to max
        CHECK_NEAR(sl->value, 10.0f, 0.001f);
        CHECK_NEAR(sl->Fraction(), 1.0f, 0.001f);
        sl->SetValue(5.0f);
        CHECK_NEAR(sl->Fraction(), 0.5f, 0.001f);
        sl->position = {7, 8}; sl->size = {180, 22};

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("Sens")->GetComponent<UISlider>();
        CHECK(r != nullptr);
        CHECK_NEAR(r->value, 5.0f, 0.001f);
        CHECK_NEAR(r->maxValue, 10.0f, 0.001f);
        CHECK_NEAR(r->size.x, 180.0f, 0.001f);
    }

    // --- UIToggle: clicking flips state and fires on_toggle() ---
    {
        Scene scene("Tog");
        GameObject* go = scene.CreateGameObject("Mute");
        auto* tg = go->AddComponent<UIToggle>();
        tg->position = {0, 0};
        tg->size = {30, 30};
        tg->on = false;
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        // When toggled on, move x to 1; off -> 0.
        CHECK(sc->LoadSource(
            "function on_toggle() { if (toggle_on()) { set_x(1); } else { set_x(0); } }"));
        scene.Start();

        Input::FeedMouse({10, 10}, 0);
        Input::FeedMouse({10, 10}, 1u << 0);
        scene.Update(0.016f);
        CHECK(tg->on);
        CHECK_NEAR(go->transform->localPosition.x, 1.0f, 0.001f);

        // Click again -> off.
        Input::FeedMouse({10, 10}, 0);
        Input::FeedMouse({10, 10}, 1u << 0);
        scene.Update(0.016f);
        CHECK(!tg->on);
        CHECK_NEAR(go->transform->localPosition.x, 0.0f, 0.001f);
    }

    // --- UIToggle serializes (label + state) ---
    {
        Scene scene("TogSer");
        GameObject* go = scene.CreateGameObject("FS");
        auto* tg = go->AddComponent<UIToggle>();
        tg->label = "Fullscreen";
        tg->on = true;
        tg->position = {40, 50};
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("FS")->GetComponent<UIToggle>();
        CHECK(r != nullptr);
        CHECK(r->label == "Fullscreen");
        CHECK(r->on == true);
        CHECK_NEAR(r->position.y, 50.0f, 0.001f);
    }

    // --- UIImage serializes (texture + tint + rect) and is an asset ---
    {
        Scene scene("Img");
        GameObject* go = scene.CreateGameObject("Logo");
        auto* im = go->AddComponent<UIImage>();
        im->texture = "ui/logo.png";
        im->position = {64, 32};
        im->size = {256, 128};
        im->color = Color(1.0f, 0.5f, 0.25f, 0.9f);

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("Logo")->GetComponent<UIImage>();
        CHECK(r != nullptr);
        CHECK(r->texture == "ui/logo.png");
        CHECK_NEAR(r->size.x, 256.0f, 0.001f);
        CHECK_NEAR(r->color.g, 0.5f, 0.01f);
        CHECK_NEAR(r->color.a, 0.9f, 0.01f);

        // The image path is collected so Build Game bundles it.
        auto assets = SceneSerializer::CollectAssetPaths(scene);
        bool found = false;
        for (const auto& a : assets) if (a == "ui/logo.png") found = true;
        CHECK(found);
    }

    // --- ResolveAnchor maps offsets to screen corners/center ---
    {
        // 800x600 canvas, a 100x40 element.
        Vec2 sz{100, 40};
        Vec2 tl = ResolveAnchor(UIAnchor::TopLeft, {10, 10}, sz, 800, 600);
        CHECK_NEAR(tl.x, 10.0f, 0.001f);
        CHECK_NEAR(tl.y, 10.0f, 0.001f);

        Vec2 tr = ResolveAnchor(UIAnchor::TopRight, {-10, 10}, sz, 800, 600);
        CHECK_NEAR(tr.x, 800.0f - 100.0f - 10.0f, 0.001f); // 690
        CHECK_NEAR(tr.y, 10.0f, 0.001f);

        Vec2 c = ResolveAnchor(UIAnchor::Center, {0, 0}, sz, 800, 600);
        CHECK_NEAR(c.x, (800.0f - 100.0f) * 0.5f, 0.001f); // 350
        CHECK_NEAR(c.y, (600.0f - 40.0f) * 0.5f, 0.001f);  // 280

        Vec2 br = ResolveAnchor(UIAnchor::BottomRight, {0, 0}, sz, 800, 600);
        CHECK_NEAR(br.x, 700.0f, 0.001f);
        CHECK_NEAR(br.y, 560.0f, 0.001f);
    }

    // --- An anchored button hit-tests against the live canvas size ---
    {
        UICanvas::Set(800, 600);
        Scene scene("Anchor");
        GameObject* go = scene.CreateGameObject("Quit");
        auto* b = go->AddComponent<UIButton>();
        b->anchor = UIAnchor::BottomRight;
        b->position = {-110, -50};           // offset in from the bottom-right
        b->size = {100, 40};
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_click() { set_x(9); }"));
        scene.Start();

        // Resolved rect sits at (800-100-110, 600-40-50) = (590, 510)..(690,550).
        CHECK(b->Contains({640, 530}));
        CHECK(!b->Contains({100, 100}));     // top-left of screen misses
        Input::FeedMouse({640, 530}, 0);
        Input::FeedMouse({640, 530}, 1u << 0);
        scene.Update(0.016f);
        CHECK(b->WasClicked());
        CHECK_NEAR(go->transform->localPosition.x, 9.0f, 0.001f);
    }

    // --- Anchor round-trips through serialization ---
    {
        Scene scene("AnchorSer");
        GameObject* go = scene.CreateGameObject("HUD");
        go->AddComponent<UIPanel>()->anchor = UIAnchor::BottomCenter;
        go->AddComponent<UIProgressBar>()->anchor = UIAnchor::TopRight;
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        CHECK(loaded.Find("HUD")->GetComponent<UIPanel>()->anchor == UIAnchor::BottomCenter);
        CHECK(loaded.Find("HUD")->GetComponent<UIProgressBar>()->anchor == UIAnchor::TopRight);
    }

    TEST_MAIN_RESULT();
}
