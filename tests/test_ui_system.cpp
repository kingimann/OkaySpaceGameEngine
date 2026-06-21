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
        cv.scaleFactor = 1.0f;             // scaleFactor now multiplies in BOTH modes
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
        // The user scaleFactor multiplies the screen-size scale too.
        cv.scaleFactor = 1.5f;
        CHECK_NEAR(cv.ScaleFactor(1280, 720), 1.5f, 1e-4f);
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

    // --- Owning Canvas + scaling: widgets follow their parent Canvas --------
    {
        Scene s("canvas-parent"); s.physicsEnabled = false;
        GameObject* canvas = s.CreateGameObject("Canvas");
        auto* cv = canvas->AddComponent<Canvas>();
        cv->scaleMode = Canvas::ScaleMode::ScaleWithScreenSize;
        cv->referenceResolution = {1280, 720};
        cv->matchWidthOrHeight = 0.0f;   // follow width

        GameObject* btn = s.CreateGameObject("Btn");
        auto* b = btn->AddComponent<UIButton>();
        b->anchor = UIAnchor::TopLeft; b->position = {100, 50}; b->size = {200, 60};
        btn->transform->SetParent(canvas->transform, false);

        // Unparented widget reports no canvas; parented one finds it.
        GameObject* loose = s.CreateGameObject("Loose");
        loose->AddComponent<UIPanel>();
        CHECK(OwningCanvas(loose) == nullptr);
        CHECK(OwningCanvas(btn) == cv);
        CHECK_NEAR(UIScaleFor(loose, 2560, 720), 1.0f, 1e-4f);  // no canvas -> 1

        // At reference width the scale is 1; at double width it is 2.
        CHECK_NEAR(UIScaleFor(btn, 1280, 720), 1.0f, 1e-3f);
        CHECK_NEAR(UIScaleFor(btn, 2560, 720), 2.0f, 1e-3f);

        // The resolved screen rect scales position and size together.
        Vec2 o, sz;
        CHECK(GetUIScreenRect(btn, 2560, 720, o, sz));
        CHECK_NEAR(sz.x, 400.0f, 0.5f);   // 200 * 2
        CHECK_NEAR(sz.y, 120.0f, 0.5f);   // 60 * 2
        CHECK_NEAR(o.x, 200.0f, 0.5f);    // pos 100 * 2 from top-left anchor
        CHECK_NEAR(o.y, 100.0f, 0.5f);

        // Hit-testing uses the scaled rect: (400,150) is inside at 2x (rect
        // x[200,600] y[100,220]) but outside at 1x (rect x[100,300] y[50,110]).
        CHECK(UIScreenContains(btn, {400, 150}, 2560, 720));
        CHECK(!UIScreenContains(btn, {400, 150}, 1280, 720));
    }

    // --- SceneEventSystem finds the scene's event system ----------------
    {
        Scene s("es-find"); s.physicsEnabled = false;
        CHECK(SceneEventSystem(s) == nullptr);
        CHECK(!SceneHasEventSystem(s));
        GameObject* go = s.CreateGameObject("EventSystem");
        auto* es = go->AddComponent<EventSystem>();
        CHECK(SceneEventSystem(s) == es);
        CHECK(SceneHasEventSystem(s));
    }

    // --- The Main Menu template builds the Unity UI structure -----------
    {
        Scene s("mm");
        Templates::MainMenu(s);
        CHECK(SceneHasEventSystem(s));
        GameObject* canvas = s.Find("Canvas");
        CHECK(canvas != nullptr);
        CHECK(canvas->GetComponent<Canvas>() != nullptr);
        GameObject* start = s.Find("StartButton");
        CHECK(start != nullptr);
        CHECK(OwningCanvas(start) == canvas->GetComponent<Canvas>());  // parented to it
    }

    // --- Every UI widget is scene-selectable + survives save/load -------
    // Guards the regression where new widgets (scroll view, layout group, …)
    // weren't in GetUIRect and so couldn't be clicked/resized in the editor.
    {
        Scene s("allui"); s.physicsEnabled = false;
        s.CreateGameObject("Btn")->AddComponent<UIButton>();
        s.CreateGameObject("Pan")->AddComponent<UIPanel>();
        s.CreateGameObject("Img")->AddComponent<UIImage>();
        s.CreateGameObject("Sld")->AddComponent<UISlider>();
        s.CreateGameObject("Tog")->AddComponent<UIToggle>();
        s.CreateGameObject("Prg")->AddComponent<UIProgressBar>();
        s.CreateGameObject("Inp")->AddComponent<UIInputField>();
        s.CreateGameObject("Drp")->AddComponent<UIDropdown>();
        s.CreateGameObject("Scr")->AddComponent<UIScrollView>();
        s.CreateGameObject("Lay")->AddComponent<UILayoutGroup>();
        { auto* t = s.CreateGameObject("Txt")->AddComponent<TextRenderer>(); t->screenSpace = true; }

        // Every one reports as a selectable UI element (valid GetUIRect).
        const char* names[] = {"Btn","Pan","Img","Sld","Tog","Prg","Inp","Drp","Scr","Lay","Txt"};
        for (const char* n : names) CHECK(IsUIElement(s.Find(n)));

        // Sizable widgets expose a size pointer (resize handles); the layout
        // group is move-only (a controller, no size).
        CHECK(GetUIRect(s.Find("Scr")).sizePtr != nullptr);
        CHECK(GetUIRect(s.Find("Drp")).sizePtr != nullptr);
        CHECK(GetUIRect(s.Find("Lay")).sizePtr == nullptr);
        CHECK(GetUIRect(s.Find("Lay")).valid);   // still selectable/movable

        // All of them round-trip through the serializer.
        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        for (const char* n : names) CHECK(s2.Find(n) && IsUIElement(s2.Find(n)));
    }

    // --- Draw-order: MoveToFront/Back reorder + topmost picks -----------
    {
        Scene s("order"); s.physicsEnabled = false;
        UICanvas::Set(800, 600);
        GameObject* a = s.CreateGameObject("A");
        a->AddComponent<UIPanel>()->size = {200, 200};
        GameObject* b = s.CreateGameObject("B");
        b->AddComponent<UIPanel>()->size = {200, 200};   // overlaps A at (0,0)

        // Both overlap the point; UIRaycast returns the last (topmost) = B.
        CHECK(UIRaycast(s, {10, 10}, 800, 600) == b);
        s.MoveToFront(a);                 // A now drawn last -> on top
        CHECK(s.Objects().back().get() == a);
        CHECK(UIRaycast(s, {10, 10}, 800, 600) == a);
        s.MoveToBack(a);                  // A behind again
        CHECK(s.Objects().front().get() == a);
        CHECK(UIRaycast(s, {10, 10}, 800, 600) == b);
    }

    TEST_MAIN_RESULT();
}
