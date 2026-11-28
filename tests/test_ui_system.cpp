#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/UINavigation.hpp"
#include "okay/Input/GameInput.hpp"
#include <string>
#include <vector>

using namespace okay;

namespace {

// Records the Component pointer messages the Event System sends.
struct PointerLog : Component {
    std::vector<std::string> got;
    void OnMouseEnter() override { got.push_back("enter"); }
    void OnMouseExit() override  { got.push_back("exit"); }
    void OnMouseDown() override  { got.push_back("down"); }
    void OnMouseUp() override    { got.push_back("up"); }
    void OnMouseClick() override { got.push_back("click"); }
};

std::vector<std::string> Kinds(const EventSystem& es) {
    std::vector<std::string> out;
    for (const UIEvent& e : es.FrameEvents()) out.push_back(std::string(UIEventName(e.kind)) + ":" + (e.target ? e.target->name : "-"));
    return out;
}
bool Has(const std::vector<std::string>& v, const std::string& x) {
    for (const auto& s : v) if (s == x) return true;
    return false;
}

UIButton* AddButton(Scene& s, const char* name, Vec2 pos, Vec2 size) {
    auto* b = s.CreateGameObject(name)->AddComponent<UIButton>();
    b->anchor = UIAnchor::TopLeft; b->position = pos; b->size = size; b->label = "";
    return b;
}

// One frame the way the player runs it: the pointer, then the scene (the Event System pumps in its Update).
void PointerFrame(Scene& s, Vec2 at, bool held, std::vector<char> keys = {}) {
    Input::FeedKeys(keys);
    Input::FeedMouse(at, held ? 1u : 0u);
    s.Update(1.0f / 60.0f);
}

} // namespace

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

    // --- The Main Menu template builds the canvas UI structure -----------
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

    // --- Event System: pointer messages, enter/exit along the parent chain, click and drag ---
    {
        Scene s("es-events"); s.physicsEnabled = false;
        UICanvas::Set(800, 600);
        auto* es = s.CreateGameObject("EventSystem")->AddComponent<EventSystem>();
        GameObject* panel = s.CreateGameObject("Card");
        auto* pn = panel->AddComponent<UIPanel>();
        pn->anchor = UIAnchor::TopLeft; pn->position = {0, 0}; pn->size = {300, 300};
        auto* log = panel->AddComponent<PointerLog>();
        UIButton* b = AddButton(s, "Play", {50, 50}, {100, 40});
        b->gameObject->transform->SetParent(panel->transform, false);
        auto* blog = b->gameObject->AddComponent<PointerLog>();

        es->Pump(s, {10, 10}, false, false);                     // onto the card
        CHECK(Has(Kinds(*es), "enter:Card"));
        CHECK(log->got.size() == 1 && log->got[0] == "enter");
        es->Pump(s, {60, 60}, false, false);                     // onto its button: the card is not left
        CHECK(Has(Kinds(*es), "enter:Play"));
        CHECK(!Has(Kinds(*es), "exit:Card"));
        es->Pump(s, {60, 60}, true, true);                       // press
        CHECK(Has(Kinds(*es), "down:Play"));
        CHECK(Has(Kinds(*es), "select:Play"));
        CHECK(es->Selected() == b->gameObject);
        es->Pump(s, {62, 61}, false, false);                     // release on it: a click
        CHECK(Has(Kinds(*es), "up:Play"));
        CHECK(Has(Kinds(*es), "click:Play"));
        CHECK(es->LastEventText() == "click on Play");
        // The button fires its own on_click, so it gets no second click message.
        CHECK(blog->got.size() == 3 && blog->got[0] == "enter" && blog->got[1] == "down" && blog->got[2] == "up");

        // The card (not a button) does get the click message.
        log->got.clear();
        es->Pump(s, {250, 250}, false, false);                   // off the button, still on the card
        CHECK(Has(Kinds(*es), "exit:Play"));
        es->Pump(s, {250, 250}, true, true);
        es->Pump(s, {250, 250}, false, false);
        CHECK(Has(Kinds(*es), "click:Card"));
        CHECK(log->got.size() == 3 && log->got[2] == "click");

        // A drag: past the threshold it is a drag, and the release sends no click.
        es->dragThreshold = 10.0f;
        es->Pump(s, {200, 200}, true, true);
        es->Pump(s, {205, 200}, true, false);                    // 5 px: still a press
        CHECK(!es->Dragging());
        es->Pump(s, {215, 200}, true, false);                    // 15 px: a drag
        CHECK(es->Dragging());
        CHECK(Has(Kinds(*es), "drag begin:Card"));
        es->Pump(s, {230, 210}, true, false);
        CHECK(Has(Kinds(*es), "drag:Card"));
        es->Pump(s, {230, 210}, false, false);
        CHECK(Has(Kinds(*es), "drag end:Card"));
        CHECK(!Has(Kinds(*es), "click:Card"));
        CHECK(!es->Dragging() && es->Pressed() == nullptr);

        // Leaving the UI exits the whole chain.
        es->Pump(s, {700, 500}, false, false);
        CHECK(Has(Kinds(*es), "exit:Card"));
        CHECK(!es->IsPointerOverUI());
    }

    // --- Event System: widgets get the pointer through it (a covered button gets no click) ---
    {
        Scene s("es-occlude"); s.physicsEnabled = false;
        UICanvas::Set(800, 600);
        UIWorld().rectOf = [](GameObject* go, Vec2& o, Vec2& sz) { return GetUIScreenRect(go, 800, 600, o, sz); };
        UIWorld().clipContains = [](GameObject* go, const Vec2& p) { return UIClipHitTest(go, p, 800, 600); };
        UIButton* under = AddButton(s, "Under", {100, 100}, {200, 100});
        UIButton* over = AddButton(s, "Over", {150, 120}, {200, 100});   // later: drawn on top
        GameObject* label = s.CreateGameObject("Label");                  // a picture over "Under" only
        auto* im = label->AddComponent<UIImage>();
        im->anchor = UIAnchor::TopLeft; im->position = {100, 100}; im->size = {40, 40};
        auto click = [&](Vec2 at) {
            PointerFrame(s, at, false);
            PointerFrame(s, at, true);
        };
        // No Event System: both buttons under the point react (the old way), and nothing breaks.
        click({200, 150});
        CHECK(under->WasClicked());
        CHECK(over->WasClicked());
        PointerFrame(s, {200, 150}, false);
        // With one, only the top button gets the click.
        s.CreateGameObject("EventSystem")->AddComponent<EventSystem>();
        click({200, 150});
        CHECK(!under->WasClicked());
        CHECK(over->WasClicked());
        CHECK(!under->IsHovered());
        PointerFrame(s, {200, 150}, false);
        // A picture drawn over a button does not take its clicks.
        click({110, 110});
        CHECK(under->WasClicked());
        PointerFrame(s, {110, 110}, false);
        UIWorld().rectOf = nullptr;
        UIWorld().clipContains = nullptr;
    }

    // --- UIRaycast skips widgets that are not drawn ---
    {
        Scene s("es-hidden"); s.physicsEnabled = false;
        UICanvas::Set(800, 600);
        GameObject* menu = s.CreateGameObject("Menu");
        GameObject* btn = AddButton(s, "Quit", {0, 0}, {100, 100})->gameObject;
        btn->transform->SetParent(menu->transform, false);
        CHECK(UIRaycast(s, {50, 50}, 800, 600) == btn);
        menu->active = false;                                    // the menu is closed
        CHECK(UIRaycast(s, {50, 50}, 800, 600) == nullptr);
    }

    // --- Event System settings are saved with the scene ---
    {
        Scene s("es-save"); s.physicsEnabled = false;
        auto* es = s.CreateGameObject("EventSystem")->AddComponent<EventSystem>();
        const std::string plain = SceneSerializer::Serialize(s);
        CHECK(plain.find("  eventsystem\n") != std::string::npos);   // defaults: the bare record older builds read
        es->dragThreshold = 24.0f;
        es->navigation = false;
        es->blockWorldClicks = false;
        es->submitAction = "Jump";
        es->cancelAction = "Pause";
        es->navigateAction = "Move";
        es->firstSelected = "Start Game";
        const std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x");
        CHECK(SceneSerializer::Deserialize(s2, txt));
        auto* e2 = s2.Find("EventSystem") ? s2.Find("EventSystem")->GetComponent<EventSystem>() : nullptr;
        CHECK(e2 != nullptr);
        if (e2) {
            CHECK_NEAR(e2->dragThreshold, 24.0f, 1e-4f);
            CHECK(!e2->navigation);
            CHECK(!e2->blockWorldClicks);
            CHECK(e2->submitAction == "Jump");
            CHECK(e2->cancelAction == "Pause");
            CHECK(e2->navigateAction == "Move");
            CHECK(e2->firstSelected == "Start Game");
        }
        CHECK(SceneSerializer::Serialize(s2) == txt);
        // A bare record from an older file loads with the defaults.
        Scene s3("y");
        CHECK(SceneSerializer::Deserialize(s3, plain));
        auto* e3 = s3.Find("EventSystem") ? s3.Find("EventSystem")->GetComponent<EventSystem>() : nullptr;
        CHECK(e3 && e3->navigation && e3->blockWorldClicks && e3->submitAction == "Submit" && e3->firstSelected.empty());
        // The property registry knows the settings (tooltips, scripts, the generic paths).
        auto info = PropertyRegistry::FindByName("EventSystem");
        CHECK(info != nullptr);
        int found = 0;
        if (info) for (const auto& d : info->properties)
            if (d.name == "dragThreshold" || d.name == "navigation" || d.name == "blockWorldClicks" || d.name == "submitAction" ||
                d.name == "cancelAction" || d.name == "navigateAction" || d.name == "firstSelected") ++found;
        CHECK(found == 7);
    }

    // --- Event System: keyboard / gamepad navigation, first selected, Selected() follows the focus ---
    {
        Scene s("es-nav"); s.physicsEnabled = false;
        UICanvas::Set(800, 600);
        UIWorld().screenW = 800; UIWorld().screenH = 600;
        auto* es = s.CreateGameObject("EventSystem")->AddComponent<EventSystem>();
        UIButton* a = AddButton(s, "Start", {100, 100}, {200, 40});
        UIButton* b = AddButton(s, "Options", {100, 160}, {200, 40});
        UIButton* c = AddButton(s, "Quit", {100, 220}, {200, 40});
        es->firstSelected = "Options";
        Input::FeedKeys({});
        NavigateUI(s);                                           // nothing focused yet: the first selected one takes it
        CHECK(b->IsFocused() && !a->IsFocused());
        CHECK(es->Selected() == b->gameObject);
        Input::FeedKeys({'s'});
        NavigateUI(s);                                           // down
        CHECK(c->IsFocused());
        CHECK(es->Selected() == c->gameObject);
        Input::FeedKeys({});
        Input::FeedKeys({'\r'});
        CHECK(NavigateUI(s) == c->gameObject);                   // submit presses it
        CHECK(c->WasClicked());
        CHECK(Has(Kinds(*es), "submit:Quit"));
        es->Select(a->gameObject);                               // a script selects one: it takes the focus
        CHECK(a->IsFocused() && !c->IsFocused());
        CHECK(es->SelectedName() == "Start");
        // Navigation off: the keys move nothing.
        es->navigation = false;
        Input::FeedKeys({});
        Input::FeedKeys({'s'});
        NavigateUI(s);
        CHECK(a->IsFocused() && !b->IsFocused());
        es->navigation = true;
        // A project action adds to the keys, and a held direction steps once.
        InputActions::Define("EsTestNav", {"keys:i,k,j,l"}, true);   // up, down, left, right
        InputActions::Define("EsTestSubmit", {"key:m"});
        es->navigateAction = "EsTestNav";
        es->submitAction = "EsTestSubmit";
        Input::FeedKeys({'k'});
        InputActions::Tick();
        NavigateUI(s);
        CHECK(b->IsFocused());
        Input::FeedKeys({'k'});                                   // still held: no second step
        InputActions::Tick();
        NavigateUI(s);
        CHECK(b->IsFocused());
        Input::FeedKeys({});
        InputActions::Tick();
        NavigateUI(s);
        Input::FeedKeys({'m'});                                   // the submit action presses the focused button
        InputActions::Tick();
        CHECK(NavigateUI(s) == b->gameObject);
        CHECK(b->WasClicked());
        Input::FeedKeys({});
        InputActions::Tick();
        es->Select(a->gameObject);
        // Cancel sends a cancel event to the selected widget.
        Input::FeedKeys({static_cast<char>(27)});
        Input::FeedMouse({-50, -50}, 0);
        es->Update(0.016f);
        CHECK(Has(Kinds(*es), "cancel:Start"));
        Input::FeedKeys({});
    }

    // --- Event System: "block world clicks" and two Event Systems ---
    {
        Scene s("es-world"); s.physicsEnabled = false;
        UICanvas::Set(800, 600);
        AddButton(s, "Hud", {0, 0}, {200, 100});
        auto* es = s.CreateGameObject("EventSystem")->AddComponent<EventSystem>();
        Input::FeedMouse({50, 50}, 1u);
        CHECK(UIBlocksWorldPointer(s));
        CHECK(std::string(GameInputBlockReason(&s)) == "pointer");
        es->blockWorldClicks = false;                            // world clicks go through the HUD
        CHECK(!UIBlocksWorldPointer(s));
        CHECK(std::string(GameInputBlockReason(&s)).empty());
        es->blockWorldClicks = true;
        Input::FeedMouse({500, 500}, 1u);                        // not over UI
        CHECK(!UIBlocksWorldPointer(s));
        Input::FeedMouse({500, 500}, 0u);
        CHECK(!SceneHasSeveralEventSystems(s));
        s.CreateGameObject("EventSystem 2")->AddComponent<EventSystem>();
        CHECK(SceneHasSeveralEventSystems(s));
        CHECK(SceneEventSystem(s) == es);                        // the first one rules
    }

    TEST_MAIN_RESULT();
}
