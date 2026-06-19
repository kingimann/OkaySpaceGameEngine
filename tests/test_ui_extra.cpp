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

        // Type "hi": feed each key as a down-edge.
        Input::FeedMouse({20, 20}, 0);      // release
        Input::FeedKeys({'h'}); s.Update(0.016f);
        Input::FeedKeys({});    s.Update(0.016f);
        Input::FeedKeys({'i'}); s.Update(0.016f);
        CHECK(in->text == "hi");

        // Backspace removes the last char.
        Input::FeedKeys({(char)8}); s.Update(0.016f);
        CHECK(in->text == "h");

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
        Input::FeedMouse({0, 0}, 0);
    }

    TEST_MAIN_RESULT();
}
