#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <fstream>

using namespace okay;

int main() {
    RUN_SUITE("ui_extra");

    // --- Tweening: tween_move animates the transform to the target ------
    {
        Scene s("Tw"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Mover");
        go->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "function start() { tween_move(5, 3, 1.0, \"out_quad\"); }\n");
        s.Start();
        for (int i = 0; i < 15; ++i) s.Update(0.1f);   // 1.5s, past the 1s tween
        CHECK_NEAR(go->transform->localPosition.x, 5.0f, 0.05f);
        CHECK_NEAR(go->transform->localPosition.y, 3.0f, 0.05f);
    }

    // --- Save system: save_game writes a slot file; exists/delete work --
    {
        std::remove("saves/utest.okaysave");
        Scene s("Sv"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Hero");
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        sc->LoadSource(
            "var saved = 0; var existed = 0;\n"
            "function start() {\n"
            "  saved = save_game(\"utest\");\n"
            "  existed = save_exists(\"utest\");\n"
            "}\n");
        s.Start();
        CHECK_NEAR(sc->VM()->GetGlobal("saved").AsFloat(), 1.0f, 1e-4f);
        CHECK_NEAR(sc->VM()->GetGlobal("existed").AsFloat(), 1.0f, 1e-4f);
        CHECK(std::ifstream("saves/utest.okaysave").good());
        std::remove("saves/utest.okaysave");
    }

    // --- Scroll View: owning lookup, clamped scroll, child offset, save -
    {
        Scene s("Sc"); s.physicsEnabled = false;
        GameObject* svGo = s.CreateGameObject("Scroll");
        auto* sv = svGo->AddComponent<UIScrollView>();
        sv->position = {0, 0}; sv->size = {200, 100}; sv->contentHeight = 400;

        GameObject* item = s.CreateGameObject("Item");
        auto* btn = item->AddComponent<UIButton>();
        btn->anchor = UIAnchor::TopLeft; btn->position = {10, 150}; btn->size = {100, 30};
        item->transform->SetParent(svGo->transform, false);

        CHECK(OwningScrollView(item) == sv);
        CHECK_NEAR(sv->ScrollMax(), 300.0f, 1e-3f);   // 400 - 100
        sv->ScrollBy(1000.0f);                        // clamps to max
        CHECK_NEAR(sv->scroll, 300.0f, 1e-3f);
        sv->SetScroll(50.0f);

        // The child's resolved screen Y is shifted up by the scroll amount.
        Vec2 o, sz;
        GetUIScreenRect(item, 1280, 720, o, sz);
        CHECK_NEAR(o.y, 150.0f - 50.0f, 0.5f);

        // Serialization round-trips the scroll view.
        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* sv2 = s2.Find("Scroll")->GetComponent<UIScrollView>();
        CHECK(sv2 != nullptr);
        if (sv2) { CHECK_NEAR(sv2->size.x, 200.0f, 1e-3f); CHECK_NEAR(sv2->contentHeight, 400.0f, 1e-3f); }
    }

    // --- Layout Group: stacks children by their sizes + spacing --------
    {
        Scene s("Lay"); s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("List");
        auto* lg = g->AddComponent<UILayoutGroup>();
        lg->direction = UILayoutGroup::Direction::Vertical;
        lg->origin = {10, 20}; lg->spacing = 5.0f;

        auto addBtn = [&](float h) {
            GameObject* it = s.CreateGameObject("It");
            auto* b = it->AddComponent<UIButton>();
            b->size = {120, h};
            it->transform->SetParent(g->transform, false);
            return b;
        };
        UIButton* b0 = addBtn(30);
        UIButton* b1 = addBtn(40);
        UIButton* b2 = addBtn(20);
        lg->Arrange();

        CHECK_NEAR(b0->position.y, 20.0f, 1e-3f);             // origin.y
        CHECK_NEAR(b1->position.y, 20.0f + 30 + 5, 1e-3f);    // + b0 height + spacing
        CHECK_NEAR(b2->position.y, 20.0f + 30 + 5 + 40 + 5, 1e-3f);
        CHECK_NEAR(b0->position.x, 10.0f, 1e-3f);             // origin.x (vertical)
        CHECK_NEAR(lg->ContentSize(), 30.0f + 5 + 40 + 5 + 20, 1e-3f);

        // Horizontal lays out along X.
        lg->direction = UILayoutGroup::Direction::Horizontal;
        lg->Arrange();
        CHECK_NEAR(b1->position.x, 10.0f + 120 + 5, 1e-3f);
    }

    // --- Input field: focus on click, type, backspace, submit ----------
    {
        Scene s("In"); s.physicsEnabled = false;
        UICanvas::Set(1280, 720);
        GameObject* go = s.CreateGameObject("Field");
        auto* in = go->AddComponent<UIInputField>();
        in->anchor = UIAnchor::TopLeft; in->position = {0, 0}; in->size = {200, 40};
        s.Start();

        // Click inside to focus.
        Input::FeedMouse({20, 20}, 1u << 0);
        s.Update(0.016f);
        CHECK(in->focused);

        // Type via the OS text channel — uppercase + symbols work now.
        Input::FeedMouse({20, 20}, 0);      // release
        Input::ClearTypedText(); Input::FeedText("H"); s.Update(0.016f);
        Input::ClearTypedText(); Input::FeedText("i"); s.Update(0.016f);
        Input::ClearTypedText(); Input::FeedText("!"); s.Update(0.016f);
        CHECK(in->text == "Hi!");

        // Backspace removes the last char (edge-detected from the key state).
        Input::ClearTypedText();
        Input::FeedKeys({}); s.Update(0.016f);
        Input::FeedKeys({(char)8}); s.Update(0.016f);
        CHECK(in->text == "Hi");

        // Password mode masks the display but keeps the real text.
        in->contentType = UIInputField::ContentType::Password;
        CHECK(in->DisplayText() == "**");
        CHECK(in->text == "Hi");

        // Content type filters input: Integer rejects letters/symbols.
        in->contentType = UIInputField::ContentType::Integer;
        in->text.clear();
        Input::FeedKeys({});   // release the backspace key from the edge state
        Input::ClearTypedText(); Input::FeedText("4a2!"); s.Update(0.016f);
        CHECK(in->text == "42");
        in->contentType = UIInputField::ContentType::Standard;

        // Clicking outside removes focus.
        Input::FeedKeys({});
        Input::FeedMouse({500, 500}, 1u << 0);
        s.Update(0.016f);
        CHECK(!in->focused);

        // Serializes its text + placeholder.
        in->text = "saved"; in->placeholder = "name";
        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* in2 = s2.Find("Field")->GetComponent<UIInputField>();
        CHECK(in2 && in2->text == "saved");
        CHECK(in2 && in2->placeholder == "name");
    }

    // --- UI style customization round-trips through serialization -------
    {
        Scene s("style"); s.physicsEnabled = false;
        auto* pn = s.CreateGameObject("Panel")->AddComponent<UIPanel>();
        pn->cornerRadius = 12.0f;
        pn->borderWidth = 3.0f;
        pn->borderColor = Color::FromBytes(10, 20, 30, 40);
        auto* btn = s.CreateGameObject("Btn")->AddComponent<UIButton>();
        btn->cornerRadius = 8.0f;
        btn->fontScale = 3.5f;
        btn->borderWidth = 2.0f;
        btn->borderColor = Color::FromBytes(200, 100, 50, 255);
        pn->useGradient = true;
        pn->colorBottom = Color::FromBytes(5, 6, 7, 8);
        auto* tr = s.CreateGameObject("Txt")->AddComponent<TextRenderer>();
        tr->screenSpace = true;
        tr->align = 2;
        tr->outline = true;
        tr->outlineColor = Color::FromBytes(1, 2, 3, 4);
        tr->bold = true;

        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* pn2 = s2.Find("Panel")->GetComponent<UIPanel>();
        CHECK_NEAR(pn2->cornerRadius, 12.0f, 1e-4f);
        CHECK_NEAR(pn2->borderWidth, 3.0f, 1e-4f);
        CHECK_NEAR(pn2->borderColor.r, Color::FromBytes(10, 20, 30, 40).r, 1e-3f);
        CHECK(pn2->useGradient);
        CHECK_NEAR(pn2->colorBottom.a, Color::FromBytes(5, 6, 7, 8).a, 1e-3f);
        auto* btn2 = s2.Find("Btn")->GetComponent<UIButton>();
        CHECK_NEAR(btn2->cornerRadius, 8.0f, 1e-4f);
        CHECK_NEAR(btn2->fontScale, 3.5f, 1e-4f);
        CHECK_NEAR(btn2->borderWidth, 2.0f, 1e-4f);
        auto* tr2 = s2.Find("Txt")->GetComponent<TextRenderer>();
        CHECK(tr2->align == 2);
        CHECK(tr2->outline);
        CHECK(tr2->bold);
        CHECK_NEAR(tr2->outlineColor.g, Color::FromBytes(1, 2, 3, 4).g, 1e-3f);
    }

    // --- Dropdown: open on click, pick an option, fire on_change --------
    {
        Scene s("Dd"); s.physicsEnabled = false;
        UICanvas::Set(1280, 720);
        GameObject* go = s.CreateGameObject("Drop");
        auto* dd = go->AddComponent<UIDropdown>();
        dd->anchor = UIAnchor::TopLeft; dd->position = {0, 0}; dd->size = {200, 40};
        dd->options = {"A", "B", "C"}; dd->value = 0;
        dd->placeholder = "Pick one";
        // value = -1 shows the placeholder, not an option.
        dd->value = -1;
        CHECK(!dd->HasSelection() && dd->HeaderText() == "Pick one");
        dd->value = 0;
        s.Start();

        // Clear any leftover button state from earlier blocks, then click.
        Input::FeedMouse({10, 10}, 0);
        s.Update(0.016f);
        // Click the header to open.
        Input::FeedMouse({10, 10}, 1u << 0);
        s.Update(0.016f);
        CHECK(dd->open);

        // Release, then click the 3rd option (rows start at y=40, each 40 tall).
        Input::FeedMouse({10, 10}, 0);
        s.Update(0.016f);
        Input::FeedMouse({10, 40 + 80 + 10}, 1u << 0);   // row index 2 -> "C"
        s.Update(0.016f);
        CHECK(dd->value == 2);
        CHECK(!dd->open);
        CHECK(dd->Selected() == "C");

        // Round-trips options + selection through serialization.
        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* dd2 = s2.Find("Drop")->GetComponent<UIDropdown>();
        CHECK(dd2 && dd2->value == 2);
        CHECK(dd2 && dd2->options.size() == 3);
        CHECK(dd2 && dd2->options[1] == "B");
        CHECK(dd2 && dd2->placeholder == "Pick one");
        Input::FeedMouse({0, 0}, 0);
    }

    // --- UI script API: read/drive widgets by name from a script -------
    {
        Scene s("UiApi"); s.physicsEnabled = false;
        // Target widgets.
        auto* sl = s.CreateGameObject("Vol")->AddComponent<UISlider>();
        sl->value = 0.25f;
        auto* tg = s.CreateGameObject("Mute")->AddComponent<UIToggle>();
        tg->on = false;
        auto* dd = s.CreateGameObject("Quality")->AddComponent<UIDropdown>();
        dd->options = {"Low", "Medium", "High"}; dd->value = 0;
        auto* lbl = s.CreateGameObject("Label")->AddComponent<TextRenderer>();
        lbl->text = "old";

        // A controller script drives them all by name.
        auto* ctrl = s.CreateGameObject("Ctrl")->AddComponent<ScriptComponent>("okayscript");
        ctrl->LoadSource(
            "var read = 0; var pick = \"\";\n"
            "function start() {\n"
            "  ui_set_slider(\"Vol\", 0.8);\n"
            "  read = ui_slider_value(\"Vol\");\n"
            "  ui_set_toggle(\"Mute\", 1);\n"
            "  ui_set_dropdown(\"Quality\", 2);\n"
            "  pick = ui_dropdown_text(\"Quality\");\n"
            "  ui_set_text(\"Label\", \"new\");\n"
            "}\n");
        s.Start();

        CHECK_NEAR(sl->value, 0.8f, 1e-4f);
        CHECK(tg->on);
        CHECK(dd->value == 2);
        CHECK(lbl->text == "new");
        CHECK_NEAR(ctrl->VM()->GetGlobal("read").AsFloat(), 0.8f, 1e-4f);
        CHECK(ctrl->VM()->GetGlobal("pick").AsString() == "High");
    }

    // --- UIImage fill: FilledRect geometry + serialization -------------
    {
        Scene s("Fill"); s.physicsEnabled = false;
        auto* im = s.CreateGameObject("Bar")->AddComponent<UIImage>();
        im->size = {100, 20};
        im->cornerRadius = 6.0f;

        float ox, oy, fw, fh;
        // Right fill at 0.25 -> left quarter visible.
        im->fillMode = UIImage::FillMode::Right; im->fillAmount = 0.25f;
        im->FilledRect(100, 20, ox, oy, fw, fh);
        CHECK_NEAR(ox, 0.0f, 1e-4f); CHECK_NEAR(fw, 25.0f, 1e-4f);
        // Left fill at 0.25 -> right quarter visible (origin shifts).
        im->fillMode = UIImage::FillMode::Left;
        im->FilledRect(100, 20, ox, oy, fw, fh);
        CHECK_NEAR(ox, 75.0f, 1e-4f); CHECK_NEAR(fw, 25.0f, 1e-4f);
        // Up fill at 0.5 -> bottom half visible.
        im->fillMode = UIImage::FillMode::Up; im->fillAmount = 0.5f;
        im->FilledRect(100, 20, ox, oy, fw, fh);
        CHECK_NEAR(oy, 10.0f, 1e-4f); CHECK_NEAR(fh, 10.0f, 1e-4f);

        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* im2 = s2.Find("Bar")->GetComponent<UIImage>();
        CHECK(im2 && im2->fillMode == UIImage::FillMode::Up);
        CHECK_NEAR(im2->fillAmount, 0.5f, 1e-4f);
        CHECK_NEAR(im2->cornerRadius, 6.0f, 1e-4f);
    }

    // --- Slider / progress / toggle style round-trip -------------------
    {
        Scene s("Sty2"); s.physicsEnabled = false;
        auto* sl = s.CreateGameObject("Sld")->AddComponent<UISlider>();
        sl->cornerRadius = 7.0f; sl->knobSize = 1.2f; sl->showValue = true;
        sl->textColor = Color::FromBytes(11, 22, 33, 44);
        auto* pb = s.CreateGameObject("Bar")->AddComponent<UIProgressBar>();
        pb->cornerRadius = 9.0f; pb->showPercent = true;
        auto* tg = s.CreateGameObject("Tg")->AddComponent<UIToggle>();
        tg->cornerRadius = 5.0f;

        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* sl2 = s2.Find("Sld")->GetComponent<UISlider>();
        CHECK_NEAR(sl2->cornerRadius, 7.0f, 1e-4f);
        CHECK_NEAR(sl2->knobSize, 1.2f, 1e-4f);
        CHECK(sl2->showValue);
        CHECK_NEAR(sl2->textColor.a, Color::FromBytes(11, 22, 33, 44).a, 1e-3f);
        auto* pb2 = s2.Find("Bar")->GetComponent<UIProgressBar>();
        CHECK_NEAR(pb2->cornerRadius, 9.0f, 1e-4f);
        CHECK(pb2->showPercent);
        auto* tg2 = s2.Find("Tg")->GetComponent<UIToggle>();
        CHECK_NEAR(tg2->cornerRadius, 5.0f, 1e-4f);
    }

    // --- Tooltip: hover timing + serialization -------------------------
    {
        Scene s("Tt"); s.physicsEnabled = false;
        UICanvas::Set(1280, 720);
        GameObject* go = s.CreateGameObject("Help");
        auto* bt = go->AddComponent<UIButton>();
        bt->anchor = UIAnchor::TopLeft; bt->position = {0, 0}; bt->size = {100, 40};
        auto* tt = go->AddComponent<UITooltip>();
        tt->text = "Click me"; tt->delay = 0.3f;
        s.Start();

        // Pointer off the widget -> never ready.
        Input::FeedMouse({500, 500}, 0);
        s.Update(0.5f);
        CHECK(!tt->Ready());

        // Hover the widget; needs to accumulate past the delay.
        Input::FeedMouse({10, 10}, 0); s.Update(0.2f);
        CHECK(!tt->Ready());
        s.Update(0.2f);                 // total 0.4s > 0.3 delay
        CHECK(tt->Ready());

        // Leaving resets immediately.
        Input::FeedMouse({500, 500}, 0); s.Update(0.016f);
        CHECK(!tt->Ready());

        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* tt2 = s2.Find("Help")->GetComponent<UITooltip>();
        CHECK(tt2 && tt2->text == "Click me");
        CHECK(tt2 && tt2->delay > 0.29f && tt2->delay < 0.31f);
    }

    // --- Slider whole-numbers + progress fill direction ----------------
    {
        Scene s("set"); s.physicsEnabled = false;
        auto* sl = s.CreateGameObject("S")->AddComponent<UISlider>();
        sl->minValue = 0; sl->maxValue = 10; sl->wholeNumbers = true; sl->vertical = true;
        sl->SetValue(3.7f);
        CHECK_NEAR(sl->value, 4.0f, 1e-4f);    // snapped to integer

        auto* pb = s.CreateGameObject("P")->AddComponent<UIProgressBar>();
        pb->value = 0.25f; pb->fillDir = UIProgressBar::FillDir::BottomTop;
        float ox, oy, fw, fh;
        pb->FillRect(100, 80, ox, oy, fw, fh);
        CHECK_NEAR(fh, 20.0f, 1e-4f); CHECK_NEAR(oy, 60.0f, 1e-4f);  // grows from bottom

        auto* bn = s.CreateGameObject("Bn")->AddComponent<UIButton>();
        bn->hoverScale = 1.15f;
        auto* pl = s.CreateGameObject("Pl")->AddComponent<UIPanel>();
        pl->shadow = true; pl->shadowOffset = {7, 9};

        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* sl2 = s2.Find("S")->GetComponent<UISlider>();
        CHECK(sl2->wholeNumbers);
        CHECK(sl2->vertical);
        auto* pb2 = s2.Find("P")->GetComponent<UIProgressBar>();
        CHECK(pb2->fillDir == UIProgressBar::FillDir::BottomTop);
        auto* bn2 = s2.Find("Bn")->GetComponent<UIButton>();
        CHECK_NEAR(bn2->hoverScale, 1.15f, 1e-4f);
        auto* pl2 = s2.Find("Pl")->GetComponent<UIPanel>();
        CHECK(pl2->shadow && pl2->shadowOffset.x > 6.9f && pl2->shadowOffset.y > 8.9f);
    }

    // --- Disabled widgets ignore input ---------------------------------
    {
        Scene s("dis"); s.physicsEnabled = false;
        UICanvas::Set(1280, 720);
        auto* tg = s.CreateGameObject("T")->AddComponent<UIToggle>();
        tg->position = {0, 0}; tg->size = {30, 30}; tg->interactable = false;
        s.Start();
        Input::FeedMouse({10, 10}, 0); s.Update(0.016f);
        Input::FeedMouse({10, 10}, 1u << 0); s.Update(0.016f);
        CHECK(!tg->on);   // disabled toggle ignored the click
        // Round-trips.
        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        CHECK(!s2.Find("T")->GetComponent<UIToggle>()->interactable);
        Input::FeedMouse({0, 0}, 0);
    }

    // --- Scroll View wheel-scrolls in the built game (Input wheel) -----
    {
        Scene s("scroll"); s.physicsEnabled = false;
        UICanvas::Set(1280, 720);
        auto* sv = s.CreateGameObject("SV")->AddComponent<UIScrollView>();
        sv->position = {0, 0}; sv->size = {200, 100}; sv->contentHeight = 400;
        s.Start();
        CHECK_NEAR(sv->scroll, 0.0f, 1e-4f);
        // Wheel down over the viewport scrolls; clamped to ScrollMax (300).
        Input::ClearTypedText(); Input::FeedMouseWheel(-3.0f);
        Input::FeedMouse({20, 20}, 0);
        s.Update(0.016f);
        CHECK(sv->scroll > 0.0f);
        // Wheel only scrolls when the pointer is over the viewport.
        float was = sv->scroll;
        Input::ClearTypedText(); Input::FeedMouseWheel(-3.0f);
        Input::FeedMouse({900, 900}, 0);
        s.Update(0.016f);
        CHECK_NEAR(sv->scroll, was, 1e-4f);
        Input::ClearTypedText();
    }

    // --- Menu navigation reaches toggles, sliders, dropdowns -----------
    {
        Scene s("nav"); s.physicsEnabled = false;
        auto* bt = s.CreateGameObject("B")->AddComponent<UIButton>();
        bt->position = {0, 0};
        auto* tg = s.CreateGameObject("T")->AddComponent<UIToggle>();
        tg->position = {0, 50}; tg->on = false;
        auto* sl = s.CreateGameObject("S")->AddComponent<UISlider>();
        sl->position = {0, 100}; sl->minValue = 0; sl->maxValue = 1; sl->value = 0.5f;

        Input::FeedKeys({}); NavigateUI(s);           // focus the first (button)
        CHECK(bt->IsFocused());
        Input::FeedKeys({'s'}); NavigateUI(s);        // down -> toggle
        CHECK(tg->IsFocused() && !bt->IsFocused());
        Input::FeedKeys({}); Input::FeedKeys({' '}); NavigateUI(s);  // activate toggle
        CHECK(tg->on);
        Input::FeedKeys({}); Input::FeedKeys({'s'}); NavigateUI(s);  // down -> slider
        CHECK(sl->IsFocused());
        Input::FeedKeys({}); Input::FeedKeys({'d'}); NavigateUI(s);  // right -> +5%
        CHECK(sl->value > 0.5f);
        Input::FeedKeys({});
    }

    TEST_MAIN_RESULT();
}
