// Runtime UI audit fixes and additions: typing vs. menu navigation, gamepad reach of steppers and
// ratings, draw-order hit testing, pointer-blocking panels, radio buttons, the 12 themes and their
// roles, floating numbers, the dialogue box, draggable windows, the gallery, and save / load of all of it.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UIGallery.hpp"
#include "okay/Components/EventSystem.hpp"
#include <set>

using namespace okay;

static void PlayerHooks(float w, float h) {
    UICanvas::Set(w, h);
    UIWorld().rectOf = [w, h](GameObject* go, Vec2& o, Vec2& sz) {
        if (UIHidden(go)) return false;
        return GetUIScreenRect(go, w, h, o, sz);
    };
    UIWorld().clipContains = [w, h](GameObject* go, const Vec2& p) { return UIClipHitTest(go, p, w, h); };
}
static void NoHooks() { UIWorld().rectOf = nullptr; UIWorld().clipContains = nullptr; UICanvas::Set(1280, 720); }

static void Press(const char* keys) {
    std::vector<char> v; for (const char* c = keys; *c; ++c) v.push_back(*c);
    Input::FeedKeys({}); Input::FeedKeys(v);
}
static void MouseTo(float x, float y, bool down) {
    Input::FeedMouse(Vec2{x, y}, down ? 1u : 0u);
}
static void Click(float x, float y) { MouseTo(x, y, false); MouseTo(x, y, true); }

static UIButton* MakeButton(Scene& s, const char* name, Vec2 pos, Vec2 size, GameObject* parent = nullptr) {
    GameObject* g = s.CreateGameObject(name);
    if (parent) g->transform->SetParent(parent->transform, false);
    auto* b = g->AddComponent<UIButton>();
    b->position = pos; b->size = size; b->shape = UIShape::Rectangle;
    return b;
}

int main() {
    RUN_SUITE("runtime_ui");
    Input::SetUICaptured(false);
    NoHooks();

    // ---- Typing in a text field must not drive the menu ----
    {
        Scene s("Typing");
        UIButton* top = MakeButton(s, "Top", {0, 10}, {100, 40});
        UIButton* bot = MakeButton(s, "Bot", {0, 80}, {100, 40});
        auto* field = s.CreateGameObject("Name")->AddComponent<UIInputField>();
        field->position = {200, 10};
        s.Start();
        Input::FeedKeys({});
        NavigateUI(s);
        CHECK(top->IsFocused());
        field->focused = true;                       // the player clicked the name box and types "s w a d"
        Press("s"); NavigateUI(s);
        CHECK(top->IsFocused() && !bot->IsFocused());   // focus did not move
        Press(" ");
        GameObject* fired = NavigateUI(s);
        CHECK(fired == nullptr && !top->WasClicked());  // Space typed a space, it did not press the button
        field->focused = false;
        Press("s"); NavigateUI(s);
        CHECK(bot->IsFocused());                     // back to normal once the field lets go
        Input::FeedKeys({});
    }

    // ---- Gamepad / keyboard reach steppers and ratings ----
    {
        Scene s("Reach");
        auto* st = s.CreateGameObject("Players")->AddComponent<UIStepper>();
        st->position = {0, 0}; st->value = 2; st->minValue = 0; st->maxValue = 5;
        auto* rt = s.CreateGameObject("Stars")->AddComponent<UIRating>();
        rt->position = {0, 60}; rt->value = 2;
        s.Start();
        Input::FeedKeys({});
        NavigateUI(s);
        CHECK(st->IsFocused());
        Press("d"); NavigateUI(s);                    // right on a stepper = +1
        CHECK_NEAR(st->value, 3.0f, 1e-4f);
        Press("a"); NavigateUI(s);
        CHECK_NEAR(st->value, 2.0f, 1e-4f);
        Press("s"); NavigateUI(s);                    // down moves to the rating
        CHECK(rt->IsFocused() && !st->IsFocused());
        Press("d"); NavigateUI(s);
        CHECK_NEAR(rt->value, 3.0f, 1e-4f);
        rt->readOnly = true;
        CHECK(!IsNavFocusable(rt->gameObject));       // a display-only rating is not a focus stop
        Input::FeedKeys({});
    }

    // ---- Hit testing follows draw order, not creation order ----
    {
        Scene s("Order");
        GameObject* cvA = s.CreateGameObject("CanvasLow"); cvA->AddComponent<Canvas>()->sortOrder = 0;
        GameObject* cvB = s.CreateGameObject("CanvasHigh"); cvB->AddComponent<Canvas>()->sortOrder = 5;
        // the high canvas is created FIRST, yet it draws on top
        UIButton* hi = MakeButton(s, "Hi", {10, 10}, {100, 50}, cvB);
        UIButton* lo = MakeButton(s, "Lo", {10, 10}, {100, 50}, cvA);
        CHECK(UIDrawsAbove(hi->gameObject, lo->gameObject));
        CHECK(!UIDrawsAbove(lo->gameObject, hi->gameObject));
        CHECK(UIRaycast(s, {20, 20}, 1280, 720) == hi->gameObject);
        // same canvas: a button draws over a panel whatever the order; the later sibling wins on a tie
        GameObject* cv = s.CreateGameObject("Canvas"); cv->AddComponent<Canvas>();
        UIButton* b = MakeButton(s, "B", {400, 10}, {100, 50}, cv);
        GameObject* pg = s.CreateGameObject("P"); pg->transform->SetParent(cv->transform, false);
        auto* pn = pg->AddComponent<UIPanel>(); pn->position = {400, 10}; pn->size = {100, 50};
        CHECK(UIRaycast(s, {420, 20}, 1280, 720) == b->gameObject);       // panel is created later but sits under the button
        b->gameObject->uiDrawOrder = 1;
        pg->uiDrawOrder = 50;                                              // an explicit order puts the panel on top
        CHECK(UIRaycast(s, {420, 20}, 1280, 720) == pg);
    }

    // ---- A pointer-blocking panel stops clicks reaching widgets under it ----
    {
        PlayerHooks(1280, 720);
        Scene s("Block");
        GameObject* hud = s.CreateGameObject("Hud"); hud->AddComponent<Canvas>()->sortOrder = 0;
        GameObject* popup = s.CreateGameObject("Popup"); popup->AddComponent<Canvas>()->sortOrder = 10;
        UIButton* under = MakeButton(s, "Under", {100, 100}, {120, 50}, hud);
        GameObject* card = s.CreateGameObject("Card"); card->transform->SetParent(popup->transform, false);
        auto* cp = card->AddComponent<UIPanel>(); cp->position = {50, 50}; cp->size = {300, 200};
        UIButton* onCard = MakeButton(s, "OnCard", {20, 20}, {100, 40}, card);
        s.Start();
        MouseTo(0, 0, false); s.Update(0.016f);
        Click(130, 120); s.Update(0.016f);
        CHECK(under->WasClicked());                       // without the flag it is clickable through the card (as before)
        MouseTo(0, 0, false); s.Update(0.016f);
        cp->blocksPointer = true;
        Click(130, 120); s.Update(0.016f);
        CHECK(!under->WasClicked());                      // now the card soaks the click
        MouseTo(0, 0, false); s.Update(0.016f);
        Click(80, 80); s.Update(0.016f);                  // a button ON the card still works
        CHECK(onCard->WasClicked());
        MouseTo(0, 0, false); s.Update(0.016f);
        Click(700, 500); s.Update(0.016f);
        CHECK(!under->WasClicked());
        MouseTo(0, 0, false); s.Update(0.016f);
        under->gameObject->transform->SetParent(popup->transform, false);   // same canvas as the card, drawn later: on top
        MouseTo(0, 0, false);
        NoHooks();
    }

    // ---- Radio buttons ----
    {
        Scene s("Radio");
        GameObject* row = s.CreateGameObject("Row");
        std::vector<UIToggle*> r;
        for (int i = 0; i < 3; ++i) {
            GameObject* g = s.CreateGameObject(("R" + std::to_string(i)).c_str());
            g->transform->SetParent(row->transform, false);
            auto* t = g->AddComponent<UIToggle>();
            t->style = UIToggle::Style::Radio; t->position = {0.0f, 40.0f * i}; t->size = {24, 24};
            r.push_back(t);
        }
        r[0]->on = true;
        r[1]->Toggle();
        CHECK(!r[0]->on && r[1]->on && !r[2]->on);        // picking one clears the others under the same parent
        r[1]->Toggle();
        CHECK(r[1]->on);                                  // clicking the picked one keeps it
        CHECK(r[2]->RadioSelected() == r[1]);
        // a click through the mouse path does the same
        Input::FeedMouse(Vec2{0, 0}, 0);
        Click(10, 90);
        s.Update(0.016f);
        CHECK(r[2]->on && !r[1]->on);
        // groups by name span parents
        Scene g("Groups");
        auto mk = [&](const char* n, const char* grp) {
            auto* t = g.CreateGameObject(n)->AddComponent<UIToggle>();
            t->style = UIToggle::Style::Radio; t->radioGroup = grp; return t;
        };
        UIToggle* a1 = mk("a1", "A"); UIToggle* a2 = mk("a2", "A"); UIToggle* b1 = mk("b1", "B");
        a1->Toggle(); b1->Toggle(); a2->Toggle();
        CHECK(!a1->on && a2->on && b1->on);
        CHECK(g.Find("a1")->GetComponent<UIToggle>()->RadioGroupMembers().size() == 2);
        Input::FeedMouse(Vec2{0, 0}, 0);
    }

    // ---- Themes: 12 looks, roles, names ----
    {
        std::set<std::string> names;
        std::set<unsigned> signature;
        for (int p = 1; p < UITheme::kPresetCount; ++p) {
            UITheme th;
            th.ApplyPreset(p);
            CHECK(th.preset == p);
            names.insert(UITheme::PresetName(p));
            CHECK(UITheme::PresetFromName(UITheme::PresetName(p)) == p);
            CHECK(th.text.a > 0.5f && th.buttonText.a > 0.5f && th.accent.a > 0.5f);
            unsigned sig = (unsigned)(th.panel.r * 255) * 31u + (unsigned)(th.accent.g * 255) * 17u + (unsigned)(th.accent.r * 255) * 7u
                         + (unsigned)(th.cornerRadius * 3) + (unsigned)(th.borderWidth * 11) + (unsigned)(th.button.b * 255) * 5u;
            signature.insert(sig);
        }
        CHECK(names.size() == 12);                         // twelve distinct names
        CHECK(signature.size() >= 11);                     // and (nearly) as many distinct looks
        CHECK(UITheme::PresetFromName("retro pixel") == (int)UITheme::Preset::RetroPixel);
        CHECK(UITheme::PresetFromName("Sci-Fi") == (int)UITheme::Preset::SciFi);
        CHECK(UITheme::PresetFromName("nothing") == -1);
        CHECK(UITheme::VariantFromName("DANGER") == (int)UITheme::Variant::Danger);
        CHECK(UITheme::VariantFromName("huh") == -1);

        Scene s("Roles");
        GameObject* root = s.CreateGameObject("Root");
        auto* th = root->AddComponent<UITheme>();
        th->ApplyPreset((int)UITheme::Preset::Neon);
        UIButton* prim = MakeButton(s, "Prim", {0, 0}, {100, 40}, root);
        UIButton* dang = MakeButton(s, "Dang", {0, 50}, {100, 40}, root);
        UIButton* ghost = MakeButton(s, "Ghost", {0, 100}, {100, 40}, root);
        UIButton* outl = MakeButton(s, "Outl", {0, 150}, {100, 40}, root);
        UIButton* pill = MakeButton(s, "Pill", {0, 200}, {100, 40}, root);
        prim->gameObject->AddComponent<UIStyleVariant>()->variant = (int)UITheme::Variant::Primary;
        dang->gameObject->AddComponent<UIStyleVariant>()->variant = (int)UITheme::Variant::Danger;
        ghost->gameObject->AddComponent<UIStyleVariant>()->variant = (int)UITheme::Variant::Ghost;
        outl->gameObject->AddComponent<UIStyleVariant>()->variant = (int)UITheme::Variant::Outline;
        pill->gameObject->AddComponent<UIStyleVariant>()->variant = (int)UITheme::Variant::Pill;
        th->Apply();
        CHECK_NEAR(prim->color.r, th->accent.r, 1e-4f);    // primary = the accent
        CHECK_NEAR(dang->color.r, th->danger.r, 1e-4f);    // danger = the danger red
        CHECK(ghost->color.a < 0.01f && ghost->borderWidth == 0.0f);
        CHECK(outl->color.a < 0.01f && outl->borderWidth >= 2.0f);
        CHECK(pill->shape == UIShape::Pill);
        // changing the look changes every role with it
        th->ApplyPreset((int)UITheme::Preset::Military);
        th->Apply();
        CHECK_NEAR(prim->color.r, th->accent.r, 1e-4f);
        CHECK(std::fabs(prim->color.r - th->danger.r) > 0.01f || std::fabs(prim->color.g - th->danger.g) > 0.01f);
        // text on a coloured button stays readable
        CHECK(prim->textColor.r + prim->textColor.g + prim->textColor.b < 0.5f || prim->textColor.r + prim->textColor.g + prim->textColor.b > 2.0f);
        // global: restyle a widget that is NOT under the theme object
        UIButton* far = MakeButton(s, "Far", {400, 0}, {100, 40});
        th->global = true;
        th->Apply();
        CHECK_NEAR(far->color.r, th->button.r, 1e-4f);
        // fonts
        th->fontPath = "Assets/Fonts/Test.ttf";
        th->Apply();
        CHECK(far->fontPath == "Assets/Fonts/Test.ttf");
        // an unknown number from a damaged file is harmless
        th->ApplyPreset(99);
        CHECK(th->preset == 99);
        th->ApplyVariant(far->gameObject, (UITheme::Variant)3);
    }

    // ---- Floating numbers ----
    {
        Scene s("Numbers");
        GameObject* host = s.CreateGameObject("Numbers");
        auto* dn = host->AddComponent<UIDamageNumbers>();
        dn->lifetime = 1.0f; dn->riseSpeed = 100.0f; dn->maxActive = 3;
        s.Start();
        GameObject* a = dn->Spawn("-25", UIDamageNumbers::Kind::Normal, {300, 300});
        CHECK(a != nullptr && dn->ActiveCount() == 1);
        auto* tr = a->GetComponent<TextRenderer>();
        CHECK(tr && tr->screenSpace && tr->text == "-25");
        const float y0 = tr->screenPos.y;
        s.Update(0.25f);
        CHECK(tr->screenPos.y < y0 - 20.0f);              // it rises
        CHECK(tr->color.a > 0.99f);
        s.Update(0.5f);                                    // t = 0.75: fading
        CHECK(tr->color.a < 0.9f && tr->color.a > 0.0f);
        s.Update(0.5f);                                    // past its life: gone
        CHECK(dn->ActiveCount() == 0);
        s.Update(0.016f);
        // maxActive keeps the oldest from piling up
        for (int i = 0; i < 6; ++i) dn->Spawn("x", UIDamageNumbers::Kind::Crit, {100, 100});
        CHECK(dn->ActiveCount() == 3);
        // the same spawns give the same drift every run (no clock, no random device)
        Scene s2("Numbers2");
        auto* dn2 = s2.CreateGameObject("N")->AddComponent<UIDamageNumbers>();
        s2.Start();
        auto* da = dn2->Spawn("1", UIDamageNumbers::Kind::Heal, {50, 50})->GetComponent<TextRenderer>();
        auto* dc = dn->NumberObject(0)->GetComponent<TextRenderer>();
        CHECK(da->color.g > da->color.r);                 // heals are green
        CHECK(dc->color.r > dc->color.b);                 // crits are warm
        // actions a button can call
        CHECK(dn2->RunAction("Hit", 40.0f) && dn2->ActiveCount() == 2);
        CHECK(!dn2->RunAction("Nonsense", 1.0f));
        CHECK(UIDamageNumbers::KindFromName("CRIT") == UIDamageNumbers::Kind::Crit);
        dn2->Clear();
        CHECK(dn2->ActiveCount() == 0);
        // Clear on disable: nothing left behind after Stop
        dn2->Spawn("z", UIDamageNumbers::Kind::Normal, {1, 1});
        dn2->OnDisable();
        CHECK(dn2->ActiveCount() == 0);
    }
    {   // a world point is followed through the camera projector
        Scene s("World");
        auto* dn = s.CreateGameObject("N")->AddComponent<UIDamageNumbers>();
        s.Start();
        UIWorld().project = [](const Vec3& p, Vec2& out, float& depth) { out = Vec2{p.x * 100.0f, 400.0f - p.y * 100.0f}; depth = 5.0f; return true; };
        UIWorld().screenW = 1280; UIWorld().screenH = 720;
        GameObject* g = dn->SpawnAtWorld("hit", UIDamageNumbers::Kind::Normal, Vec3{4, 1, 0});
        auto* tr = g->GetComponent<TextRenderer>();
        const float cx = tr->screenPos.x + tr->size.x * 0.5f;
        CHECK(cx > 380.0f && cx < 440.0f);                // near x = 400 (plus a small drift)
        UIWorld().project = nullptr;
        UIWorld().screenW = 0; UIWorld().screenH = 0;
    }

    // ---- Dialogue box ----
    {
        Scene s("Dialogue");
        GameObject* canvas = UITemplates::EnsureCanvas(s);
        GameObject* root = UIGallery::AddDialogueScreen(s);
        auto* db = root->GetComponent<UIDialogueBox>();
        auto* script = root->AddComponent<ScriptComponent>("okayscript");
        script->LoadSource("var picked = -1; var lines = 0; var done = 0;\n"
                           "function on_dialogue_choice() { picked = ui_dialogue_choice(); }\n"
                           "function on_dialogue_line() { lines = lines + 1; }\n"
                           "function on_dialogue_done() { done = done + 1; }\n");
        (void)canvas;
        s.Start();
        GameObject* box = s.Find("Box");
        CHECK(box && !box->active && !db->IsOpen());       // hidden until a line is shown
        db->charsPerSecond = 10.0f;
        db->Show("Mara", "Hello there!", {"Hi", "Go away"});
        CHECK(db->IsOpen() && db->IsTyping() && box->active);
        auto* body = s.Find("Body")->GetComponent<TextRenderer>();
        CHECK(body->text == "Hello there!" && body->visibleChars == 0);
        CHECK(!s.Find("Choice1")->active);                 // choices wait for the end of the line
        s.Update(0.5f);
        CHECK(body->visibleChars == 5 && db->IsTyping());
        Press(" "); s.Update(0.016f);                      // first press skips to the end of the line
        CHECK(!db->IsTyping() && body->visibleChars == -1);
        Input::FeedKeys({});
        CHECK(s.Find("Choice1")->active && s.Find("Choice2")->active && !s.Find("Choice3")->active);
        CHECK(s.Find("Choice2")->GetComponent<UIButton>()->label == "Go away");
        CHECK(db->WaitingForChoice());
        Press(" "); s.Update(0.016f); Input::FeedKeys({});
        CHECK(db->IsOpen());                               // space cannot pick for the player
        CHECK(!db->Choose(5) && !db->Choose(-1));
        // the choice button works with no script: it calls Choose(1) on the box
        auto* cb = s.Find("Choice2")->GetComponent<UIButton>();
        CHECK(cb->clickFunction == "Choose" && cb->clickTarget == "Dialogue" && cb->clickArg == 1.0f);
        CHECK(InvokeNativeUIAction(root, cb->clickFunction, cb->clickArg));
        CHECK(db->LastChoice() == 1 && !db->IsOpen() && !box->active);
        CHECK_NEAR(script->VM()->GetGlobal("picked").AsFloat(), 1.0f, 1e-4f);
        CHECK_NEAR(script->VM()->GetGlobal("lines").AsFloat(), 1.0f, 1e-4f);
        // a line without choices closes when advanced
        db->charsPerSecond = 0.0f;
        db->Show("Mara", "Bye.");
        CHECK(!db->IsTyping() && s.Find("Next")->active);
        s.Update(0.016f);
        db->Advance();
        CHECK(!db->IsOpen());
        CHECK_NEAR(script->VM()->GetGlobal("done").AsFloat(), 1.0f, 1e-4f);
        s.Destroy(root);
        s.Update(0.016f);
    }
    {   // a follow-up line shown from the choice handler keeps the box up
        Scene s("Follow");
        GameObject* root = UIGallery::AddDialogueScreen(s);
        auto* db = root->GetComponent<UIDialogueBox>();
        root->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "function on_dialogue_choice() { ui_dialogue_show(\"Mara\", \"Then go.\"); }\n");
        s.Start();
        db->charsPerSecond = 0.0f;
        db->Show("Mara", "Pick", {"A"});
        db->Choose(0);
        CHECK(db->IsOpen() && db->Line() == "Then go.");
        s.Destroy(root);
        s.Update(0.016f);
    }

    // ---- Draggable window ----
    {
        PlayerHooks(1280, 720);
        Scene s("Window");
        GameObject* canvas = UITemplates::EnsureCanvas(s);
        GameObject* win = UIGallery::AddWindow(s, canvas, "Win", "Title", {100, 100}, {300, 200});
        auto* wp = win->GetComponent<UIPanel>();
        s.Start();
        MouseTo(0, 0, false); s.Update(0.016f);
        MouseTo(150, 115, false); s.Update(0.016f);        // on the title bar
        MouseTo(150, 115, true);  s.Update(0.016f);
        MouseTo(250, 215, true);  s.Update(0.016f);
        MouseTo(250, 215, false); s.Update(0.016f);
        CHECK_NEAR(wp->position.x, 200.0f, 2.0f);          // the whole window moved, not just the bar
        CHECK_NEAR(wp->position.y, 200.0f, 2.0f);
        CHECK_NEAR(win->transform->Children()[0]->gameObject->GetComponent<UIPanel>()->position.x, 0.0f, 1e-3f);
        // keep on screen: throw it far off to the right
        MouseTo(260, 215, false); s.Update(0.016f);
        MouseTo(260, 215, true);  s.Update(0.016f);
        MouseTo(5000, 215, true); s.Update(0.016f);
        MouseTo(5000, 215, false); s.Update(0.016f);
        CHECK(wp->position.x + wp->size.x <= 1280.5f);
        // the close button hides it with no script
        GameObject* close = s.Find("Close");
        auto* cbtn = close->GetComponent<UIButton>();
        CHECK(InvokeNativeUIAction(s.Find(cbtn->clickTarget), cbtn->clickFunction, 0.0f));
        CHECK(!win->active);
        MouseTo(0, 0, false);
        NoHooks();
    }


    // ---- Anchors and the Canvas scaler keep every widget on screen and clickable at any resolution ----
    {
        struct Res { float w, h; } sizes[] = {{800, 600}, {1280, 720}, {1920, 1080}, {3840, 2160}, {1080, 1920}, {3440, 1440}, {640, 360}};
        const UIAnchor anchors[] = {UIAnchor::TopLeft, UIAnchor::TopCenter, UIAnchor::TopRight, UIAnchor::MiddleLeft, UIAnchor::Center,
                                    UIAnchor::MiddleRight, UIAnchor::BottomLeft, UIAnchor::BottomCenter, UIAnchor::BottomRight};
        for (const Res& r : sizes) {
            PlayerHooks(r.w, r.h);
            Scene s("Res");
            GameObject* cv = s.CreateGameObject("Canvas");
            auto* canvas = cv->AddComponent<Canvas>();
            canvas->scaleMode = Canvas::ScaleMode::ScaleWithScreenSize;
            canvas->screenMatch = Canvas::ScreenMatch::Expand;
            std::vector<UIButton*> bs;
            for (int i = 0; i < 9; ++i) {
                UIButton* b = MakeButton(s, ("B" + std::to_string(i)).c_str(), {0, 0}, {120, 48}, cv);
                b->anchor = anchors[i];
                // an inset from the edge it is anchored to (never pushes it off a corner)
                const bool right = i % 3 == 2, bottom = i >= 6;
                b->position = {right ? -20.0f : (i % 3 == 0 ? 20.0f : 0.0f), bottom ? -20.0f : (i < 3 ? 20.0f : 0.0f)};
                bs.push_back(b);
            }
            s.Start();
            for (UIButton* b : bs) {
                Vec2 o, sz;
                CHECK(GetUIScreenRect(b->gameObject, r.w, r.h, o, sz));
                CHECK(o.x >= -0.5f && o.y >= -0.5f && o.x + sz.x <= r.w + 0.5f && o.y + sz.y <= r.h + 0.5f);   // on screen
                // a click at the middle of where it is drawn hits it, a click on a neighbour's middle does not
                MouseTo(0, 0, false); s.Update(0.016f);
                Click(o.x + sz.x * 0.5f, o.y + sz.y * 0.5f); s.Update(0.016f);
                CHECK(b->WasClicked());
                int others = 0; for (UIButton* c : bs) if (c != b && c->WasClicked()) ++others;
                CHECK(others == 0);
                MouseTo(0, 0, false); s.Update(0.016f);
            }
        }
        MouseTo(0, 0, false);
        NoHooks();
    }

    // ---- Play / Stop: nothing from a closed scene leaks into the next one ----
    {
        {
            Scene a("A");
            GameObject* m = a.CreateGameObject("Modal");
            auto* modal = m->AddComponent<UIModal>();
            modal->Open();
            GameObject* bg = a.CreateGameObject("Behind");
            bg->AddComponent<UIButton>();
            a.Start();
            UIModal::RefreshOpenOrder();
            CHECK(UIModal::Blocks(bg));
            auto* dn = a.CreateGameObject("N")->AddComponent<UIDamageNumbers>();
            dn->Spawn("x", UIDamageNumbers::Kind::Normal, {1, 1});
            a.Update(0.1f);
        }   // the scene (with an open modal and a live number) is destroyed here
        Scene b("B");
        GameObject* free = b.CreateGameObject("Free");
        free->AddComponent<UIButton>();
        UIModal::RefreshOpenOrder();
        CHECK(!UIModal::Blocks(free));                       // the old modal does not block the new scene
        CHECK(UIModal::Top(&b) == nullptr);
        // a restart of the same scene starts clean: no focus ring, no number left on screen
        Scene c("C");
        auto* btn = MakeButton(c, "Btn", {0, 0}, {50, 50});
        auto* dn = c.CreateGameObject("N")->AddComponent<UIDamageNumbers>();
        c.Start();
        btn->SetFocused(true);
        dn->Spawn("y", UIDamageNumbers::Kind::Normal, {1, 1});
        dn->OnDisable();
        CHECK(dn->ActiveCount() == 0);
        c.Update(0.016f);
        int texts = 0; for (const auto& o : c.Objects()) if (o->name == "DamageNumber") ++texts;
        CHECK(texts == 0);
    }


    // ---- Text fields keep whole characters ----
    {
        Scene s("Text");
        auto* in = s.CreateGameObject("In")->AddComponent<UIInputField>();
        in->maxLength = 5;
        CHECK(in->TypeText("h\xC3\xA9llo wor"));                     // "héllo wor" into a 5-character field
        CHECK(in->text == "h\xC3\xA9llo");                           // 5 characters (6 bytes), nothing cut in half
        CHECK(in->Backspace() && in->text == "h\xC3\xA9ll");
        in->text = "ab\xC3\xA9";
        CHECK(in->Backspace() && in->text == "ab");                  // the whole 'é', not one byte of it
        in->text = "\xE2\x82\xAC";                                    // a 3-byte character
        CHECK(in->Backspace() && in->text.empty());
        CHECK(!in->Backspace());
        in->maxLength = 64;
        in->TypeText("a\x01\x7F" "b\xFF" "c");                      // control characters and a broken byte are dropped
        CHECK(in->text == "abc");
        in->text.clear();
        in->contentType = UIInputField::ContentType::Integer;
        in->TypeText("1\xC3\xA9" "2x3");
        CHECK(in->text == "123");                                    // a number field takes digits only
        in->contentType = UIInputField::ContentType::Password;
        in->text = "p\xC3\xA9" "ss";
        CHECK(in->DisplayText() == "****");                          // one mask per character
        // broken text left by an older version never loops or erases more than a character
        in->text = "x\x80\x80\x80\x80";
        CHECK(in->Backspace() && in->text == "x\x80\x80\x80");
        // typing through the real update path
        in->contentType = UIInputField::ContentType::Standard;
        in->text.clear(); in->maxLength = 3; in->focused = true;
        Input::ClearTypedText();
        s.Start();
        s.Update(0.016f);
        CHECK(in->text.empty());
    }

    // ---- Save and load: new fields, old scenes ----
    {
        Scene a("Save");
        GameObject* canvas = UITemplates::EnsureCanvas(a);
        GameObject* g = a.CreateGameObject("Card");
        g->transform->SetParent(canvas->transform, false);
        g->AddComponent<UIPanel>()->blocksPointer = true;
        GameObject* r = a.CreateGameObject("R");
        auto* tg = r->AddComponent<UIToggle>();
        tg->style = UIToggle::Style::Radio; tg->radioGroup = "Quality"; tg->on = true;
        GameObject* w = a.CreateGameObject("W");
        w->AddComponent<UIPanel>();
        auto* dg = w->AddComponent<UIDraggable>(); dg->dragParent = true; dg->keepOnScreen = true;
        GameObject* v = a.CreateGameObject("V");
        v->AddComponent<UIButton>();
        auto* sv = v->AddComponent<UIStyleVariant>(); sv->variant = 3; sv->themeName = "T";
        GameObject* t = a.CreateGameObject("T");
        auto* th = t->AddComponent<UITheme>(); th->ApplyPreset((int)UITheme::Preset::Horror); th->fontPath = "f.ttf"; th->global = true;
        auto* dn = a.CreateGameObject("D")->AddComponent<UIDamageNumbers>(); dn->lifetime = 2.5f; dn->spawnAt = {12, 34}; dn->healColor = Color(0.1f, 0.2f, 0.3f, 1.0f);
        auto* db = a.CreateGameObject("B")->AddComponent<UIDialogueBox>(); db->charsPerSecond = 77.0f; db->hideWhenDone = false;
        const std::string text = SceneSerializer::Serialize(a);
        Scene b("Load");
        std::string err;
        CHECK(SceneSerializer::Deserialize(b, text, &err, nullptr));
        CHECK(b.Find("Card")->GetComponent<UIPanel>()->blocksPointer);
        auto* tg2 = b.Find("R")->GetComponent<UIToggle>();
        CHECK(tg2->style == UIToggle::Style::Radio && tg2->radioGroup == "Quality" && tg2->on);
        auto* dg2 = b.Find("W")->GetComponent<UIDraggable>();
        CHECK(dg2->dragParent && dg2->keepOnScreen);
        auto* sv2 = b.Find("V")->GetComponent<UIStyleVariant>();
        CHECK(sv2 && sv2->variant == 3 && sv2->themeName == "T");
        auto* th2 = b.Find("T")->GetComponent<UITheme>();
        CHECK(th2->preset == (int)UITheme::Preset::Horror && th2->fontPath == "f.ttf" && th2->global);
        CHECK_NEAR(th2->accent.r, th->accent.r, 1e-5f);
        auto* dn2 = b.Find("D")->GetComponent<UIDamageNumbers>();
        CHECK(dn2 && dn2->lifetime == 2.5f && dn2->spawnAt.x == 12.0f && dn2->spawnAt.y == 34.0f && dn2->healColor.g == 0.2f);
        auto* db2 = b.Find("B")->GetComponent<UIDialogueBox>();
        CHECK(db2 && db2->charsPerSecond == 77.0f && !db2->hideWhenDone);
        CHECK(SceneSerializer::Serialize(b) == text);       // stable
        // a scene saved before these fields: nothing extra is written, nothing breaks
        Scene plain("Plain");
        plain.CreateGameObject("P")->AddComponent<UIPanel>();
        plain.CreateGameObject("T")->AddComponent<UIToggle>();
        const std::string old = SceneSerializer::Serialize(plain);
        CHECK(old.find("uix") == std::string::npos);
        Scene back("Back");
        CHECK(SceneSerializer::Deserialize(back, old, &err, nullptr));
        CHECK(!back.Find("P")->GetComponent<UIPanel>()->blocksPointer);
        CHECK(back.Find("T")->GetComponent<UIToggle>()->radioGroup.empty());
        // a hand-edited record with a bad number is read as far as it goes
        std::string bad = text;
        const std::size_t at = bad.find("variant=3");
        if (at != std::string::npos) bad.replace(at, 9, "variant=banana");
        Scene c("Bad");
        CHECK(SceneSerializer::Deserialize(c, bad, &err, nullptr));
    }

    // ---- The gallery ----
    {
        Scene g("Gallery");
        GameObject* root = UIGallery::BuildGallery(g, (int)UITheme::Preset::Neon);
        CHECK(root != nullptr);
        for (const char* n : {"Primary", "Secondary", "Danger", "Ghost", "Outline", "Pill", "Music", "Fullscreen", "Low", "Medium", "High",
                              "Volume", "Players", "Rating", "Region", "Name", "HealthBar", "ManaBar", "StaminaBar", "Ring", "Pie", "Spinner",
                              "Tabs", "Crosshair", "Slots", "DialogueBox", "Window", "SwatchInsetNameplate", "Numbers", "ThemePick"})
            CHECK(g.Find(n) != nullptr);
        int widgets = 0;
        for (const auto& o : g.Objects()) if (GetUIRect(o.get()).valid) ++widgets;
        CHECK(widgets >= 90);
        // radio group: exactly one picked
        int on = 0; for (const char* n : {"Low", "Medium", "High"}) on += g.Find(n)->GetComponent<UIToggle>()->on ? 1 : 0;
        CHECK(on == 1);
        // the theme is baked: the primary button wears the neon accent
        UITheme* th = g.FindObjectOfType<UITheme>();
        CHECK(th && th->preset == (int)UITheme::Preset::Neon);
        CHECK_NEAR(g.Find("Primary")->GetComponent<UIButton>()->color.r, th->accent.r, 1e-4f);
        // every look can be applied and no widget ends up unreadable (zero alpha text)
        for (int p = 1; p < UITheme::kPresetCount; ++p) {
            UIGallery::ApplyThemePreset(g, p);
            CHECK(g.FindObjectOfType<UITheme>()->preset == p);
            for (const auto& o : g.Objects())
                if (auto* tr = o->GetComponent<TextRenderer>()) CHECK(tr->color.a > 0.2f);
        }
        // the theme dropdown restyles the scene through the script API
        g.Start();
        auto* dd = g.Find("ThemePick")->GetComponent<UIDropdown>();
        dd->SetValue(9);                                     // index 9 = Glass (the list starts at preset 1)
        g.Update(0.016f);
        CHECK(g.FindObjectOfType<UITheme>()->preset == (int)UITheme::Preset::Glass);
        // save, load, save again: the whole gallery is stable text
        const std::string text = SceneSerializer::Serialize(g);
        Scene h("Gallery2");
        std::string err;
        CHECK(SceneSerializer::Deserialize(h, text, &err, nullptr));
        CHECK(SceneSerializer::Serialize(h) == text);
        CHECK(h.Find("Medium")->GetComponent<UIToggle>()->radioGroup == "Quality");
        // the other screens
        Scene s2("Screens");
        CHECK(UIGallery::AddInventoryScreen(s2) != nullptr);
        CHECK(UIGallery::AddNameplate(s2, true)->GetComponent<WorldSpaceUI>() != nullptr);
        s2.Start();
        s2.Update(0.016f);
    }

    // ---- Script API ----
    {
        Scene s("Script");
        UIGallery::BuildGallery(s, (int)UITheme::Preset::Dark);
        auto* ctrl = s.CreateGameObject("Ctrl")->AddComponent<ScriptComponent>("okayscript");
        ctrl->LoadSource(
            "var q = -2; var ok = 0;\n"
            "function update() {\n"
            "  if (ok == 0) {\n"
            "    ok = ui_set_variant(\"Secondary\", \"danger\");\n"
            "    ui_set_radio(\"High\");\n"
            "    q = ui_radio_value(\"Quality\");\n"
            "    ui_theme_preset(\"Military\");\n"
            "    ui_damage_number(\"-5\", 100, 100, \"crit\");\n"
            "  }\n"
            "}\n");
        s.Start();
        s.Update(0.016f);
        s.Update(0.016f);
        CHECK_NEAR(ctrl->VM()->GetGlobal("ok").AsFloat(), 1.0f, 1e-4f);
        CHECK_NEAR(ctrl->VM()->GetGlobal("q").AsFloat(), 2.0f, 1e-4f);
        UITheme* th = s.FindObjectOfType<UITheme>();
        CHECK(th->preset == (int)UITheme::Preset::Military);
        CHECK_NEAR(s.Find("Secondary")->GetComponent<UIButton>()->color.r, th->danger.r, 1e-3f);
        CHECK(s.FindObjectOfType<UIDamageNumbers>()->ActiveCount() == 1);
    }

    NoHooks();
    Input::FeedKeys({});
    TEST_MAIN_RESULT();
}
