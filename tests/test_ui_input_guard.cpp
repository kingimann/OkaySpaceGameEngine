#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/SettingsScreen.hpp"
#include "okay/Scene/UITemplates.hpp"
#include <string>

using namespace okay;

static bool Inside(const Vec2& o, const Vec2& sz, const Vec2& co, const Vec2& cs) {
    return o.x >= co.x - 1.0f && o.y >= co.y - 1.0f &&
           o.x + sz.x <= co.x + cs.x + 1.0f && o.y + sz.y <= co.y + cs.y + 1.0f;
}

static void CheckPauseLayout(float W, float H) {
    UICanvas::Set(W, H);
    Scene s("Pause");
    s.CreateGameObject("Holder")->AddComponent<PauseMenu>()->EnsureBuilt();
    GameObject* card = s.Find("Pause_Card");
    Vec2 co, cs;
    CHECK(card && GetUIScreenRect(card, W, H, co, cs));
    CHECK_NEAR(co.x + cs.x * 0.5f, W * 0.5f, 1.5f);
    CHECK_NEAR(co.y + cs.y * 0.5f, H * 0.5f, 1.5f);
    CHECK(co.x >= -1.0f && co.y >= -1.0f);
    CHECK(co.x + cs.x <= W + 1.0f && co.y + cs.y <= H + 1.0f);
    for (const char* name : {"Pause_Title", "Pause_Resume", "Pause_Quit",
                             "Pause_Volume", "Pause_Music", "Pause_Effects"}) {
        GameObject* g = s.Find(name);
        Vec2 o, sz;
        CHECK(g && GetUIScreenRect(g, W, H, o, sz));
        CHECK(sz.x > 8.0f && sz.y > 4.0f);
        CHECK(Inside(o, sz, co, cs));
        CHECK_NEAR(o.x + sz.x * 0.5f, co.x + cs.x * 0.5f, 2.0f);
    }
}

int main() {
    RUN_SUITE("ui_input_guard");
    Game::SetPaused(false);
    Input::SetUICaptured(false);
    UIWorld().rectOf = nullptr;

    // --- Stretch-anchor hit testing: zero margins are the whole canvas. ---
    {
        UICanvas::Set(800, 600);
        Scene s("StretchHit");
        auto* b = s.CreateGameObject("Fill")->AddComponent<UIButton>();
        b->anchor = UIAnchor::StretchFull;
        b->position = {0, 0};
        b->size = {0, 0};
        b->shape = UIShape::Rectangle;
        CHECK(b->Contains({1, 1}));
        CHECK(b->Contains({400, 300}));
        CHECK(b->Contains({799, 599}));
        CHECK(!b->Contains({-20, 10}));
        b->position = {10, 20};
        b->size = {30, 40};                 // right / bottom margins
        CHECK(b->Contains({15, 25}));
        CHECK(!b->Contains({5, 25}));       // left of the left margin
        CHECK(!b->Contains({780, 100}));    // inside the right margin
        CHECK(!b->Contains({100, 580}));    // inside the bottom margin

        auto* sl = s.CreateGameObject("Bar")->AddComponent<UISlider>();
        sl->anchor = UIAnchor::StretchHorizontal;
        sl->position = {8, 40};
        sl->size = {8, 16};                 // right margin, height
        CHECK(sl->Contains({20, 44}));
        CHECK(!sl->Contains({2, 44}));
        CHECK(!sl->Contains({20, 80}));
    }

    // --- Game input blocked, independent of UICaptured. ---
    {
        Scene s("Block");
        CHECK(!GameInputBlocked(&s));
        Game::SetPaused(true);
        CHECK(std::string(GameInputBlockReason(&s)) == "pause");
        CHECK(GameInputBlocked(&s, (unsigned)GameInputMask::Keys));
        Game::SetPaused(false);

        auto* bag = s.CreateGameObject("Bag")->AddComponent<InventoryUI>();
        bag->open = true;
        CHECK(std::string(GameInputBlockReason(&s)) == "bag");
        CHECK(!GameInputBlocked(&s, (unsigned)GameInputMask::Keys));   // E must still close it
        bag->open = false;

        auto* field = s.CreateGameObject("Name")->AddComponent<UIInputField>();
        field->focused = true;
        CHECK(std::string(GameInputBlockReason(&s)) == "text");
        CHECK(GameInputBlocked(&s, (unsigned)GameInputMask::Keys));
        field->focused = false;

        UICanvas::Set(400, 300);
        auto* btn = s.CreateGameObject("Hud")->AddComponent<UIButton>();
        btn->position = {10, 10};
        btn->size = {80, 30};
        Input::FeedMouse({20, 20}, 0);
        CHECK(!GameInputBlocked(&s));              // hovering is not a click
        Input::FeedMouse({20, 20}, 1u);
        CHECK(std::string(GameInputBlockReason(&s)) == "pointer");
        Input::FeedMouse({200, 200}, 1u);
        CHECK(!GameInputBlocked(&s));              // click on the world
        Input::FeedMouse({0, 0}, 0);

        auto* melee = s.CreateGameObject("Fighter")->AddComponent<MeleeAttacker>();
        Game::SetPaused(true);
        Input::FeedKeys({});
        Input::FeedKeys({'f'});
        melee->Update(0.016f);
        CHECK(melee->Ready());                     // the swing did not start
        Game::SetPaused(false);
        Input::FeedKeys({});
        Input::FeedKeys({'f'});
        melee->Update(0.016f);
        CHECK(!melee->Ready());
        Input::FeedKeys({});
    }

    // --- One press: pickup or drop owns E, otherwise the bag toggles. ---
    {
        Scene s("EE");
        GameObject* player = s.CreateGameObject("Player");
        player->tag = "Player";
        player->AddComponent<Inventory>();
        auto* ui = player->AddComponent<InventoryUI>();
        ui->toggleKey = 'e';
        GameObject* gem = s.CreateGameObject("Gem");
        gem->transform->SetPosition({1, 0, 0});
        auto* pk = gem->AddComponent<ItemPickup>();
        pk->item = "gem";
        pk->interactKey = 'e';
        pk->interactRange = 3.0f;
        pk->pickupOnTouch = false;
        CHECK(InteractClaimsKey(&s, 'e'));
        Input::FeedKeys({});
        Input::FeedKeys({'e'});
        s.Update(0.016f);
        CHECK(!ui->open);                          // the pickup took the press
        CHECK(s.Find("Gem") == nullptr || pk->count < 1);

        Input::FeedKeys({});
        Input::FeedKeys({'e'});
        s.Update(0.016f);
        CHECK(ui->open);                           // nothing in range: the bag toggles
        Input::FeedKeys({});
    }

    // --- Lean steps aside for a use or bag press (Q / E clash). ---
    {
        Scene s("Lean");
        GameObject* player = s.CreateGameObject("Player");
        player->tag = "Player";
        player->AddComponent<Inventory>();
        auto* ui = player->AddComponent<InventoryUI>();
        ui->toggleKey = 'e';
        Input::FeedKeys({});
        CHECK(!LeanKeyTaken(&s, 'e'));             // not pressed: a held lean is fine
        Input::FeedKeys({'e'});
        CHECK(LeanKeyTaken(&s, 'e'));              // the press that opens the bag is not a lean
        CHECK(!LeanKeyTaken(&s, 'q'));             // Q is nobody else's key
        ui->toggleKey = 'b';
        CHECK(!LeanKeyTaken(&s, 'e'));
        Input::FeedKeys({});
    }

    // --- A top-down player does not shoot through an open bag or a text field. ---
    {
        Scene s("TDFire");
        GameObject* p = s.CreateGameObject("Player");
        auto* c = p->AddComponent<TopDownController2D>();
        c->fireButton = 0;
        Input::FeedMouse({10, 10}, 1u);
        CHECK((c->ReadLocalInput().held & PlayerInput::Fire) != 0);
        auto* bag = s.CreateGameObject("Bag")->AddComponent<InventoryUI>();
        bag->open = true;
        Input::FeedMouse({10, 10}, 1u);
        CHECK((c->ReadLocalInput().held & PlayerInput::Fire) == 0);
        bag->open = false;
        auto* field = s.CreateGameObject("Chat")->AddComponent<UIInputField>();
        field->focused = true;
        CHECK((c->ReadLocalInput().held & PlayerInput::Fire) == 0);
        field->focused = false;
        CHECK((c->ReadLocalInput().held & PlayerInput::Fire) != 0);
        Input::FeedMouse({0, 0}, 0);
    }

    // --- Settings screen bindings. ---
    {
        UICanvas::Set(1280, 720);
        UIWorld().rectOf = [](GameObject* go, Vec2& o, Vec2& sz) {
            return GetUIScreenRect(go, 1280, 720, o, sz);
        };
        Scene s("Settings");
        UITemplates::AddPauseMenu(s);
        GameObject* settings = UITemplates::AddSettings(s);
        GameObject* pause = s.Find("Pause Menu");
        s.Start();
        CHECK(settings->active == false);          // pause menu owns the first screen
        CHECK(pause && pause->active);
        settings->active = true;
        pause->active = false;
        auto* screen = settings->GetComponent<SettingsScreen>();
        CHECK(screen && SettingsScreen::AnyOpen(&s));
        CHECK(GameInputBlocked(&s));
        screen->Update(0.016f);                    // pull saved values
        GameObject* music = s.Find("MusicSlider");
        auto* sl = music ? music->GetComponent<UISlider>() : nullptr;
        CHECK(sl != nullptr);
        if (sl) sl->value = 0.25f;
        screen->Update(0.016f);
        CHECK_NEAR(AudioMixer::groupVolume[(int)AudioGroup::Music], 0.25f, 0.001f);
        auto* fx = s.Find("SfxSlider") ? s.Find("SfxSlider")->GetComponent<UISlider>() : nullptr;
        if (fx) fx->value = 0.4f;
        screen->Update(0.016f);
        CHECK_NEAR(AudioMixer::groupVolume[(int)AudioGroup::SFX], 0.4f, 0.001f);
        CHECK_NEAR(AudioMixer::groupVolume[(int)AudioGroup::UI], 0.4f, 0.001f);
        auto* mode = s.Find("DisplayModeDropdown") ? s.Find("DisplayModeDropdown")->GetComponent<UIDropdown>() : nullptr;
        CHECK(mode != nullptr);
        if (mode) mode->value = 1;                 // Borderless
        screen->Update(0.016f);
        GameObject* applyGo = s.Find("ApplyButton");
        auto* apply = applyGo ? applyGo->GetComponent<UIButton>() : nullptr;
        CHECK(apply != nullptr);
        if (apply) {
            Vec2 o, sz;
            CHECK(GetUIScreenRect(applyGo, 1280, 720, o, sz));
            Vec2 c{o.x + sz.x * 0.5f, o.y + sz.y * 0.5f};
            Input::FeedMouse(c, 0);
            Input::FeedMouse(c, 1u);
            apply->Update(0.016f);
            screen->LateUpdate(0.016f);
            Input::FeedMouse({0, 0}, 0);
        }
        CHECK(DisplaySettings::Fullscreen() && DisplaySettings::Mode() == WindowMode::Borderless);
        CHECK(DisplaySettings::TrialActive());
        auto* vs = s.Find("VsyncToggle") ? s.Find("VsyncToggle")->GetComponent<UIToggle>() : nullptr;
        if (vs) vs->on = false;
        screen->Update(0.016f);
        CHECK(!DisplaySettings::VSync());
        auto* q = s.Find("QualityDropdown") ? s.Find("QualityDropdown")->GetComponent<UIDropdown>() : nullptr;
        CHECK(q != nullptr);
        if (q) q->value = 0;
        screen->Update(0.016f);
        CHECK(DisplaySettings::Quality() == 0);
        CHECK(!ShadowsEnabled());

        GameObject* done = s.Find("BackButton");
        auto* btn = done ? done->GetComponent<UIButton>() : nullptr;
        CHECK(btn != nullptr);
        if (btn) {
            Vec2 o, sz;
            CHECK(GetUIScreenRect(done, 1280, 720, o, sz));
            Vec2 c{o.x + sz.x * 0.5f, o.y + sz.y * 0.5f};
            Input::FeedMouse(c, 0);
            Input::FeedMouse(c, 1u);
            btn->Update(0.016f);
            screen->LateUpdate(0.016f);
        }
        CHECK(settings->active == false);
        CHECK(pause->active);                      // back to the pause menu
        CHECK(!SettingsScreen::AnyOpen(&s));
        CHECK(!DisplaySettings::TrialActive());    // Done keeps the display change
        UIWorld().rectOf = nullptr;
        DisplaySettings::SetFullscreen(false);
        DisplaySettings::SetVSync(true);
        DisplaySettings::SetQuality(2);
        Input::FeedMouse({0, 0}, 0);
    }

    // --- An open dropdown list swallows the click under it. ---
    {
        UICanvas::Set(400, 300);
        UIWorld().rectOf = nullptr;
        Scene s("Drop");
        auto* dd = s.CreateGameObject("Quality")->AddComponent<UIDropdown>();
        dd->position = {10, 10};
        dd->size = {120, 24};
        dd->options = {"Low", "Medium", "High"};
        dd->value = 1;
        dd->open = true;
        auto* done = s.CreateGameObject("Done")->AddComponent<UIButton>();
        done->position = {10, 82};          // the third row, "High"
        done->size = {120, 24};
        auto clickHigh = [&]() {
            Input::FeedMouse({40, 90}, 0);
            Input::FeedMouse({40, 90}, 1u);
        };
        clickHigh();
        done->Update(0.016f);               // button first: the list is still open
        dd->Update(0.016f);
        CHECK(dd->value == 2);
        CHECK(!dd->open);
        CHECK(!done->WasClicked());

        dd->open = true;
        dd->value = 1;
        clickHigh();
        dd->Update(0.016f);                 // list closes first
        done->Update(0.016f);
        CHECK(dd->value == 2);
        CHECK(!done->WasClicked());
        Input::FeedMouse({0, 0}, 0);
    }

    // --- Hiding or disabling a widget drops its focus ring. ---
    {
        Scene s("Focus");
        auto* panel = s.CreateGameObject("Panel");
        auto* start = s.CreateGameObject("Start");
        start->transform->SetParent(panel->transform, false);
        auto* b = start->AddComponent<UIButton>();
        b->SetFocused(true);
        panel->active = false;
        NavigateUI(s);
        CHECK(!b->IsFocused());
        panel->active = true;
        b->SetFocused(true);
        b->enabled = false;
        NavigateUI(s);
        CHECK(!b->IsFocused());
    }

    // --- Top-left HUD text clears the PLAYING badge, including a wrapped 9:16 line. ---
    {
        auto firstLine = [](void (*build)(Scene&), float W, float H) {
            UICanvas::Set(W, H);
            Scene s("Hud");
            build(s);
            auto* tr = s.Find("Help") ? s.Find("Help")->GetComponent<TextRenderer>()
                                      : s.Find("HUD")->GetComponent<TextRenderer>();
            CHECK(tr && !tr->vcenter);
            return tr ? tr->ResolvedScreenPos(W, H).y : -1.0f;
        };
        // 9:16 wraps the Sandbox line past its box. Centering used to pull it to y≈0.
        CHECK(firstLine(&Templates::VoxelSandbox, 405, 720) >= 40.0f);
        CHECK(firstLine(&Templates::TerrainSandbox, 405, 720) >= 40.0f);
        CHECK(firstLine(&Templates::Inventory, 405, 720) >= 40.0f);
        CHECK(firstLine(&Templates::VoxelSandbox, 1280, 720) >= 40.0f);
        CHECK(firstLine(&Templates::VoxelSandbox, 960, 720) >= 40.0f);
    }

    // --- Pause card at three aspects: centred, sliders inside, fully on screen. ---
    CheckPauseLayout(1280, 720);    // 16:9
    CheckPauseLayout(960, 720);     // 4:3
    CheckPauseLayout(405, 720);     // 9:16

    Game::SetPaused(false);
    Input::SetUICaptured(false);
    Input::FeedKeys({});
    TEST_MAIN_RESULT();
}
