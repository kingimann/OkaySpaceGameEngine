#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("ui_system");

    // --- Canvas scale modes ---------------------------------------------
    {
        Canvas cv;
        cv.scaleMode = Canvas::ScaleMode::ConstantPixelSize;
        cv.scaleFactor = 1.0f;
        CHECK_NEAR(cv.ScaleFactor(1920, 1080), 1.0f, 1e-5f);  // constant ignores size
        cv.scaleFactor = 2.0f;
        CHECK_NEAR(cv.ScaleFactor(640, 480), 2.0f, 1e-5f);

        cv.scaleMode = Canvas::ScaleMode::ScaleWithScreenSize;
        cv.referenceResolution = {1280, 720};
        // At the reference resolution the factor is exactly 1.
        cv.matchWidthOrHeight = 0.5f;
        CHECK_NEAR(cv.ScaleFactor(1280, 720), 1.0f, 1e-4f);
        // Double the width, match=0 (follow width) -> 2x.
        cv.matchWidthOrHeight = 0.0f;
        CHECK_NEAR(cv.ScaleFactor(2560, 720), 2.0f, 1e-3f);
        // Double the height, match=1 (follow height) -> 2x.
        cv.matchWidthOrHeight = 1.0f;
        CHECK_NEAR(cv.ScaleFactor(1280, 1440), 2.0f, 1e-3f);
    }

    // --- GetUIRect collapses any widget to a uniform rect ---------------
    {
        Scene s("ui"); s.physicsEnabled = false;
        GameObject* b = s.CreateGameObject("Btn");
        auto* btn = b->AddComponent<UIButton>();
        btn->anchor = UIAnchor::TopLeft;
        btn->position = {10, 20};
        btn->size = {100, 40};

        UIRect r = GetUIRect(b);
        CHECK(r.valid);
        CHECK(IsUIElement(b));
        CHECK(r.Contains({15, 25}, 1280, 720));   // inside
        CHECK(!r.Contains({200, 200}, 1280, 720)); // outside

        // Editing through the rect's position pointer moves the widget.
        r.position->x += 5;
        CHECK_NEAR(btn->position.x, 15.0f, 1e-4f);

        GameObject* empty = s.CreateGameObject("Empty");
        CHECK(!IsUIElement(empty));
    }

    // --- UIRaycast picks the top-most widget under a point --------------
    {
        Scene s("ray"); s.physicsEnabled = false;
        UICanvas::Set(1280, 720);

        GameObject* panel = s.CreateGameObject("Panel");
        auto* pn = panel->AddComponent<UIPanel>();
        pn->anchor = UIAnchor::TopLeft; pn->position = {0, 0}; pn->size = {400, 300};

        GameObject* btn = s.CreateGameObject("Btn");   // created later -> on top
        auto* b = btn->AddComponent<UIButton>();
        b->anchor = UIAnchor::TopLeft; b->position = {50, 50}; b->size = {100, 40};

        // Point over both: the button (drawn later) wins.
        CHECK(UIRaycast(s, {60, 60}, 1280, 720) == btn);
        // Point over only the panel.
        CHECK(UIRaycast(s, {300, 250}, 1280, 720) == panel);
        // Point over nothing.
        CHECK(UIRaycast(s, {1000, 700}, 1280, 720) == nullptr);

        // Inactive widgets are ignored.
        btn->active = false;
        CHECK(UIRaycast(s, {60, 60}, 1280, 720) == panel);
    }

    // --- EventSystem tracks hover / press / selection -------------------
    {
        Scene s("es"); s.physicsEnabled = false;
        UICanvas::Set(1280, 720);
        GameObject* esGo = s.CreateGameObject("EventSystem");
        auto* es = esGo->AddComponent<EventSystem>();

        GameObject* btn = s.CreateGameObject("Btn");
        auto* b = btn->AddComponent<UIButton>();
        b->anchor = UIAnchor::TopLeft; b->position = {0, 0}; b->size = {100, 100};

        // Hover with no press: hovered set, nothing selected yet.
        es->Pump(s, {50, 50}, /*down*/false, /*pressed*/false);
        CHECK(es->Hovered() == btn);
        CHECK(es->IsPointerOverUI());
        CHECK(es->Selected() == nullptr);

        // Press over the button: it becomes pressed + selected.
        es->Pump(s, {50, 50}, true, true);
        CHECK(es->Pressed() == btn);
        CHECK(es->Selected() == btn);

        // Release: no longer pressed, selection sticks.
        es->Pump(s, {50, 50}, false, false);
        CHECK(es->Pressed() == nullptr);
        CHECK(es->Selected() == btn);

        // Click empty space clears the selection.
        es->Pump(s, {500, 500}, true, true);
        CHECK(es->Hovered() == nullptr);
        CHECK(es->Selected() == nullptr);
    }

    // --- Canvas + EventSystem survive a serialization round-trip --------
    {
        Scene s("ser"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("UIRoot");
        auto* cv = go->AddComponent<Canvas>();
        cv->scaleMode = Canvas::ScaleMode::ScaleWithScreenSize;
        cv->referenceResolution = {1920, 1080};
        cv->matchWidthOrHeight = 0.25f;
        cv->sortOrder = 3;
        go->AddComponent<EventSystem>();

        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        GameObject* g2 = s2.Find("UIRoot");
        CHECK(g2 != nullptr);
        auto* c2 = g2->GetComponent<Canvas>();
        CHECK(c2 != nullptr);
        CHECK(c2->scaleMode == Canvas::ScaleMode::ScaleWithScreenSize);
        CHECK_NEAR(c2->referenceResolution.x, 1920.0f, 1e-3f);
        CHECK_NEAR(c2->matchWidthOrHeight, 0.25f, 1e-4f);
        CHECK(c2->sortOrder == 3);
        CHECK(g2->GetComponent<EventSystem>() != nullptr);
    }

    TEST_MAIN_RESULT();
}
