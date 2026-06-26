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

    // --- Inspector-assigned OnClick: calls a named function on a target object ---
    {
        Scene scene("ClickTarget");
        GameObject* logic = scene.CreateGameObject("Logic");
        auto* lsc = logic->AddComponent<ScriptComponent>("okayscript");
        CHECK(lsc->LoadSource("var fired = 0; function boom() { fired = 1; }"));
        GameObject* go = scene.CreateGameObject("Btn");
        auto* b = go->AddComponent<UIButton>();
        b->position = {0, 0}; b->size = {100, 40};
        b->clickTarget = "Logic"; b->clickFunction = "boom";
        scene.Start();
        Input::FeedMouse({50, 20}, 0);
        Input::FeedMouse({50, 20}, 1u << 0);
        scene.Update(0.016f);
        CHECK(b->WasClicked());
        CHECK_NEAR(lsc->VM()->GetGlobal("fired").AsFloat(), 1.0f, 0.001f);
    }

    // --- Text options: typewriter reveal + alignment fields round-trip ---
    {
        Scene scene("Txt");
        GameObject* go = scene.CreateGameObject("Label");
        auto* tr = go->AddComponent<TextRenderer>();
        tr->text = "HELLO";
        tr->italic = true; tr->gradient = true; tr->alignBottom = true;
        // Manual reveal: only the first 3 chars.
        tr->visibleChars = 3;
        CHECK(tr->DisplayText() == "HEL");
        tr->visibleChars = -1;
        CHECK(tr->DisplayText() == "HELLO");
        // Auto typewriter: 10 chars/sec, advances with Update.
        tr->typeSpeed = 10.0f; tr->ResetReveal();
        CHECK(tr->DisplayText().empty());           // nothing revealed yet
        tr->Update(0.25f);                           // 2.5 chars -> 2 visible
        CHECK(tr->DisplayText() == "HE");
        tr->Update(1.0f);                            // well past the end
        CHECK(tr->DisplayText() == "HELLO");
        // New fields survive serialization.
        std::string txt = SceneSerializer::Serialize(scene);
        Scene loaded("L"); CHECK(SceneSerializer::Deserialize(loaded, txt));
        auto* lt = loaded.Find("Label")->GetComponent<TextRenderer>();
        CHECK(lt && lt->italic && lt->gradient && lt->alignBottom);
        CHECK_NEAR(lt->typeSpeed, 10.0f, 0.001f);
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

    // --- UIImage nine-slice flag + border survive serialization ---
    {
        Scene scene("NineSlice");
        GameObject* go = scene.CreateGameObject("Frame");
        auto* im = go->AddComponent<UIImage>();
        im->texture = "ui/panel.png";
        im->nineSlice = true;
        im->border = 24.0f;
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("Frame")->GetComponent<UIImage>();
        CHECK(r != nullptr);
        CHECK(r->nineSlice == true);
        CHECK_NEAR(r->border, 24.0f, 0.001f);

        // Back-compat: an older uiimage line (texture + anchor, no nine-slice block).
        std::string old =
            "okayscene 1\nname \"S\"\ngravity 0 0\n"
            "gameobject 0 \"I\"\n  active 1\n  parent -1\n"
            "  uiimage 0 0 64 64 1 1 1 1 \"x.png\" 4\n"   // ...anchor(4), no nine-slice
            "end\n";
        Scene l2("L2");
        CHECK(SceneSerializer::Deserialize(l2, old, &err));
        auto* r2 = l2.Find("I")->GetComponent<UIImage>();
        CHECK(r2 != nullptr);
        CHECK(r2->anchor == UIAnchor::Center);
        CHECK(r2->nineSlice == false);     // defaulted
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

    // --- A non-interactable button ignores clicks and reports its state ---
    {
        Scene scene("Disabled");
        GameObject* go = scene.CreateGameObject("Continue");
        auto* b = go->AddComponent<UIButton>();
        b->position = {0, 0};
        b->size = {100, 40};
        b->interactable = false;
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function on_click() { set_x(1); }"));
        scene.Start();
        Input::FeedMouse({50, 20}, 0);
        Input::FeedMouse({50, 20}, 1u << 0);
        scene.Update(0.016f);
        CHECK(!b->WasClicked());
        CHECK(!b->IsHovered());
        CHECK_NEAR(go->transform->localPosition.x, 0.0f, 0.001f);
        // CurrentColor returns the disabled tint when not interactable.
        CHECK(b->CurrentColor().r == b->disabledColor.r);
    }

    // --- Pressed state + CurrentColor track the mouse on an enabled button ---
    {
        Scene scene("Press");
        GameObject* go = scene.CreateGameObject("Btn");
        auto* b = go->AddComponent<UIButton>();
        b->position = {0, 0};
        b->size = {100, 40};
        scene.Start();
        Input::FeedMouse({50, 20}, 0);          // hover, not pressed
        Input::FeedMouse({50, 20}, 1u << 0);    // press (held)
        scene.Update(0.016f);
        CHECK(b->IsPressed());
        CHECK(b->CurrentColor().r == b->pressedColor.r);
        // Release: no longer pressed, falls back to hover color.
        Input::FeedMouse({50, 20}, 0);
        scene.Update(0.016f);
        CHECK(!b->IsPressed());
        CHECK(b->CurrentColor().r == b->hoverColor.r);
    }

    // --- set_interactable() from script toggles the button ---
    {
        Scene scene("ScriptDisable");
        GameObject* go = scene.CreateGameObject("Btn");
        go->AddComponent<UIButton>();
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function start() { set_interactable(false); }"));
        scene.Start();
        CHECK(go->GetComponent<UIButton>()->interactable == false);
    }

    // --- Button state colors + interactable survive serialization ---
    {
        Scene scene("BtnSer");
        GameObject* go = scene.CreateGameObject("B");
        auto* b = go->AddComponent<UIButton>();
        b->interactable = false;
        b->pressedColor = Color(0.1f, 0.2f, 0.3f, 1.0f);
        b->disabledColor = Color(0.4f, 0.4f, 0.4f, 1.0f);
        b->anchor = UIAnchor::Center;
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("B")->GetComponent<UIButton>();
        CHECK(r != nullptr);
        CHECK(r->interactable == false);
        CHECK(r->anchor == UIAnchor::Center);
        CHECK_NEAR(r->pressedColor.b, 0.3f, 0.01f);
        CHECK_NEAR(r->disabledColor.r, 0.4f, 0.01f);
    }

    // --- Keyboard/gamepad menu navigation moves focus and activates ---
    {
        Scene scene("Nav");
        auto mkBtn = [&](const char* nm, float y) {
            GameObject* g = scene.CreateGameObject(nm);
            auto* b = g->AddComponent<UIButton>();
            b->position = {0, y};            // ordered top-to-bottom by y
            b->size = {100, 40};
            auto* sc = g->AddComponent<ScriptComponent>("okayscript");
            sc->LoadSource("function on_click() { set_x(99); }");
            return b;
        };
        UIButton* top = mkBtn("Top", 10);
        UIButton* mid = mkBtn("Mid", 60);
        UIButton* bot = mkBtn("Bot", 110);
        scene.Start();

        // First call with no input focuses the first (top) button.
        Input::FeedKeys({});
        NavigateUI(scene);
        CHECK(top->IsFocused());
        CHECK(!mid->IsFocused());

        // Press down ('s') twice -> focus moves Top -> Mid -> Bot.
        Input::FeedKeys({}); Input::FeedKeys({'s'});   // down-edge
        NavigateUI(scene);
        CHECK(mid->IsFocused());
        Input::FeedKeys({}); Input::FeedKeys({'s'});
        NavigateUI(scene);
        CHECK(bot->IsFocused());

        // Wrap around: down again -> back to Top.
        Input::FeedKeys({}); Input::FeedKeys({'s'});
        NavigateUI(scene);
        CHECK(top->IsFocused());

        // Activate with Enter -> fires the focused (Top) button's on_click.
        Input::FeedKeys({}); Input::FeedKeys({'\r'});
        GameObject* fired = NavigateUI(scene);
        CHECK(fired == top->gameObject);
        CHECK(top->WasClicked());
        CHECK_NEAR(scene.Find("Top")->transform->localPosition.x, 99.0f, 0.001f);
    }

    // --- Non-focusable / disabled buttons are skipped by navigation ---
    {
        Scene scene("NavSkip");
        GameObject* a = scene.CreateGameObject("A");
        auto* ba = a->AddComponent<UIButton>(); ba->position = {0, 0}; ba->focusable = false;
        GameObject* b = scene.CreateGameObject("B");
        auto* bb = b->AddComponent<UIButton>(); bb->position = {0, 50};
        scene.Start();
        Input::FeedKeys({});
        NavigateUI(scene);
        CHECK(!ba->IsFocused());      // skipped (not focusable)
        CHECK(bb->IsFocused());       // only focusable one gets focus
    }

    // --- Button focusable flag round-trips through serialization ---
    {
        Scene scene("FocusSer");
        GameObject* go = scene.CreateGameObject("B");
        go->AddComponent<UIButton>()->focusable = false;
        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        CHECK(loaded.Find("B")->GetComponent<UIButton>()->focusable == false);
    }

    // --- Backward compat: a uibutton line ending at the anchor (pre-state
    //     format) still loads, defaulting interactable to true ---
    {
        std::string text =
            "okayscene 1\n"
            "name \"S\"\n"
            "gravity 0 0\n"
            "gameobject 0 \"B\"\n"
            "  active 1\n"
            "  parent -1\n"
            "  uibutton \"Go\" 10 20 100 40 "
            "0.2 0.3 0.5 1 0.3 0.4 0.7 1 1 1 1 1 4\n"   // ...colors + anchor(4)=Center
            "end\n";
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("B")->GetComponent<UIButton>();
        CHECK(r != nullptr);
        CHECK(r->anchor == UIAnchor::Center);
        CHECK(r->interactable == true);              // defaulted (no trailing block)
        CHECK_NEAR(r->position.x, 10.0f, 0.001f);
    }

    // --- UITabs content pages: selecting a tab shows its page, hides the rest ---
    {
        Scene s("tabs"); s.physicsEnabled = false;
        GameObject* bar = s.CreateGameObject("Tabs");
        auto* tb = bar->AddComponent<UITabs>();
        tb->tabs = {"A", "B", "C"};
        GameObject* pa = s.CreateGameObject("PageA");
        GameObject* pb = s.CreateGameObject("PageB");
        GameObject* pc = s.CreateGameObject("PageC");
        tb->pages = {"PageA", "PageB", "PageC"};

        s.Start();
        s.Update(0.016f);              // first Update applies the initial selection (0)
        CHECK(pa->active == true);
        CHECK(pb->active == false);
        CHECK(pc->active == false);

        tb->Select(2);                 // switch to tab C
        CHECK(pc->active == true);
        CHECK(pa->active == false);
        CHECK(pb->active == false);

        // Round-trips through serialization with pages intact.
        std::string text = SceneSerializer::Serialize(s);
        Scene loaded("L"); std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* lt = loaded.Find("Tabs")->GetComponent<UITabs>();
        CHECK(lt != nullptr);
        CHECK(lt->tabs.size() == 3);
        CHECK(lt->pages.size() == 3);
        CHECK(lt->pages[1] == "PageB");
    }

    // --- Visual-scripting vars surface on UI text + progress bars ---------------
    {
        ActionList::ResetVars();
        ActionList::Vars()["hp"] = 75.0f;     // a visual-script variable

        // Text bind interpolates the var (and prettifies the number).
        CHECK(UITextBind::Resolve("HP: {hp}") == "HP: 75");
        CHECK(UITextBind::Resolve("{missing}") == "");

        Scene s("bind"); s.physicsEnabled = false;
        GameObject* bar = s.CreateGameObject("HealthBar");
        auto* pb = bar->AddComponent<UIProgressBar>();
        auto* bb = bar->AddComponent<UIBarBind>();
        bb->var = "hp"; bb->min = 0.0f; bb->max = 100.0f;
        s.Start();
        s.Update(0.016f);
        CHECK_NEAR(pb->value, 0.75f, 0.001f);   // 75 / 100

        // Lower the var and the bar follows.
        ActionList::Vars()["hp"] = 20.0f;
        s.Update(0.016f);
        CHECK_NEAR(pb->value, 0.20f, 0.001f);

        // Round-trips through serialization.
        std::string text = SceneSerializer::Serialize(s);
        Scene loaded("L"); std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* lb = loaded.Find("HealthBar")->GetComponent<UIBarBind>();
        CHECK(lb != nullptr);
        CHECK(lb->var == "hp");
        CHECK_NEAR(lb->max, 100.0f, 0.001f);
        ActionList::ResetVars();
    }

    TEST_MAIN_RESULT();
}
