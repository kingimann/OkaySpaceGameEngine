// The accessibility settings change what the game does (no window needed): reduced motion stops camera
// shake and UI slides and pulses, reduced flashing stops the flash action and the blink loop, the colour
// filter changes a software-rendered 3D frame, text size changes screen-space text layout, and every
// control on the Settings screen sets its setting, saves it to Prefs and applies it live.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/SettingsScreen.hpp"
#include "okay/Components/UIAnimation.hpp"
#include "okay/Scene/UITemplates.hpp"

#include <cmath>
#include <vector>

using namespace okay;

static ActionList::Item I(const std::string& op, std::vector<std::string> a = {}) {
    return ActionList::Item{op, std::move(a)};
}

static Color Byte(std::uint32_t abgr) {
    return Color::FromBytes((unsigned char)(abgr & 255), (unsigned char)((abgr >> 8) & 255), (unsigned char)((abgr >> 16) & 255), 255);
}

int main() {
    RUN_SUITE("accessibility_wired");
    UICanvas::Set(1280, 720);

    {   // ---- Reduced motion: camera shake offset is zero, and a shake in progress stops ----
        Accessibility::Reset();
        Scene s; s.physicsEnabled = false;
        GameObject* g = s.CreateGameObject("Cam"); auto* c = g->AddComponent<Camera>();
        g->transform->localPosition = {0, 0, 10};
        s.Start();
        c->Shake(1.0f);
        s.Update(0.05f);
        const Vec3 shaken = c->RenderPosition();
        CHECK(std::fabs(shaken.x) + std::fabs(shaken.y) > 1e-4f);     // the control: shake works with the setting off
        Accessibility::SetReduceMotion(true);
        s.Update(0.016f);
        CHECK(c->ShakeTrauma() == 0.0f);                              // switched on mid-shake: stops at once
        CHECK_NEAR(c->RenderPosition().x, 0.0f, 1e-6f);
        CHECK_NEAR(c->RenderPosition().y, 0.0f, 1e-6f);
        c->Shake(1.0f);                                               // new shakes are ignored
        s.Update(0.05f);
        CHECK(c->ShakeTrauma() == 0.0f);
        CHECK_NEAR(c->RenderPosition().x, 0.0f, 1e-6f);
        Accessibility::SetReduceMotion(false);
        c->Shake(1.0f); s.Update(0.05f);
        CHECK(std::fabs(c->RenderPosition().x) + std::fabs(c->RenderPosition().y) > 1e-4f);
    }

    {   // ---- Reduced motion: UI slide-in and pulse ----
        auto build = [](Scene& s, UIAnimation::Intro in, UIAnimation::Loop lp) {
            GameObject* go = s.CreateGameObject("B");
            auto* b = go->AddComponent<UIButton>();
            b->anchor = UIAnchor::TopLeft; b->position = {100, 100}; b->size = {200, 100};
            auto* ua = go->AddComponent<UIAnimation>();
            ua->intro = in; ua->loop = lp; ua->duration = 0.4f; ua->distance = 200.0f; ua->loopAmount = 0.2f;
            return go;
        };
        Vec2 o, sz;
        Accessibility::Reset();
        { Scene s; s.physicsEnabled = false;
          GameObject* go = build(s, UIAnimation::Intro::SlideLeft, UIAnimation::Loop::None);
          s.Start(); s.Update(0.05f);
          GetUIScreenRect(go, 1280, 720, o, sz);
          CHECK(o.x < 60.0f); }                                       // sliding: still well left of home (x = 100)
        { Scene s; s.physicsEnabled = false;
          GameObject* go = build(s, UIAnimation::Intro::None, UIAnimation::Loop::Pulse);
          s.Start(); s.Update(0.25f);
          GetUIScreenRect(go, 1280, 720, o, sz);
          CHECK(sz.x > 230.0f); }                                     // pulsing: bigger than 200
        Accessibility::SetReduceMotion(true);
        { Scene s; s.physicsEnabled = false;
          GameObject* go = build(s, UIAnimation::Intro::SlideLeft, UIAnimation::Loop::None);
          s.Start(); s.Update(0.05f);
          GetUIScreenRect(go, 1280, 720, o, sz);
          CHECK_NEAR(o.x, 100.0f, 0.5f);                              // no slide: at home from the first frame
          CHECK_NEAR(UIOpacity(go), 0.5f, 0.45f);                     // a short fade instead (quarter length: 0.1 s)
          CHECK(UIOpacity(go) < 0.999f);
          s.Update(0.2f);
          CHECK_NEAR(UIOpacity(go), 1.0f, 1e-3f); }
        { Scene s; s.physicsEnabled = false;
          GameObject* go = build(s, UIAnimation::Intro::None, UIAnimation::Loop::Pulse);
          s.Start(); s.Update(0.25f);
          GetUIScreenRect(go, 1280, 720, o, sz);
          CHECK_NEAR(sz.x, 200.0f, 0.5f); }                           // no pulse
        Accessibility::Reset();
    }

    {   // ---- Reduced flashing: the flash action and the blink loop ----
        Accessibility::Reset();
        auto flashRun = [](float& peakTint) {
            Scene s; s.physicsEnabled = false;
            GameObject* o = s.CreateGameObject("Sprite");
            auto* sr = o->AddComponent<SpriteRenderer>();
            sr->color = Color{0, 0, 0, 1};
            auto* al = o->AddComponent<ActionList>();
            al->trigger = ActionList::Trigger::OnStart;
            al->instructions = { I("flash", {"0.4", "1", "1", "1"}) };
            s.Start(); s.Update(0.016f);
            peakTint = 0.0f;
            for (int i = 0; i < 30; ++i) { s.Update(0.016f); peakTint = std::max(peakTint, sr->color.r); }
        };
        float peak = 0.0f;
        flashRun(peak);
        CHECK(peak > 0.5f);                                           // the control: it flashes
        Accessibility::SetReduceFlashing(true);
        flashRun(peak);
        CHECK(peak < 1e-4f);                                          // never leaves black
        Accessibility::Reset();

        auto blinkMin = []() {
            Scene s; s.physicsEnabled = false;
            GameObject* go = s.CreateGameObject("B");
            auto* b = go->AddComponent<UIButton>();
            b->anchor = UIAnchor::TopLeft; b->size = {100, 40};
            auto* ua = go->AddComponent<UIAnimation>();
            ua->intro = UIAnimation::Intro::None; ua->loop = UIAnimation::Loop::Blink; ua->loopSpeed = 1.0f;
            s.Start();
            float lo = 1.0f;
            for (int i = 0; i < 100; ++i) { s.Update(0.02f); lo = std::min(lo, UIOpacity(go)); }
            return lo;
        };
        CHECK(blinkMin() < 0.5f);
        Accessibility::SetReduceFlashing(true);
        CHECK(blinkMin() > 0.99f);
        Accessibility::Reset();
    }

    {   // ---- Colour filter on a software-rendered 3D frame ----
        Accessibility::Reset();
        Scene sc("cube"); sc.physicsEnabled = false;
        GameObject* cube = sc.CreateGameObject("Cube");
        auto* mr = cube->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube(2.0f);
        mr->color = Color{0.85f, 0.15f, 0.1f, 1.0f};
        GameObject* camGo = sc.CreateGameObject("Cam");
        auto* cam = camGo->AddComponent<Camera>();
        camGo->transform->localPosition = {0, 0, 6};
        sc.Start();
        ApplySceneLight(sc);
        const int W = 64, H = 64;
        Mat4 vp = cam->ProjectionMatrix(1.0f) * cam->ViewMatrix();
        Raster work; std::vector<std::uint32_t> out;
        const std::uint32_t* px = RenderMeshesSS(work, out, sc, vp, cam->transform->Position(), W, H, 1, nullptr);
        std::vector<std::uint32_t> frame(px, px + W * H);
        const std::uint32_t centre = frame[(std::size_t)(H / 2) * W + W / 2];
        CHECK((centre >> 24) == 255);                                 // the cube covers the middle of the frame
        std::vector<std::uint32_t> same = frame;
        Accessibility::FilterFrame((std::uint8_t*)same.data(), same.size());
        CHECK(same == frame);                                         // no filter set: untouched
        Accessibility::SetColorblind(ColorblindMode::Deuteranopia, 1.0f);
        std::vector<std::uint32_t> filtered = frame;
        Accessibility::FilterFrame((std::uint8_t*)filtered.data(), filtered.size());
        const std::uint32_t fc = filtered[(std::size_t)(H / 2) * W + W / 2];
        CHECK(fc != centre);                                          // the rendered pixel changed
        const Color expect = DaltonizeColor(Byte(centre), ColorblindMode::Deuteranopia, 1.0f);
        const Color got = Byte(fc);
        CHECK_NEAR(got.r, expect.r, 0.01f); CHECK_NEAR(got.g, expect.g, 0.01f); CHECK_NEAR(got.b, expect.b, 0.01f);
        CHECK((fc >> 24) == 255);                                     // alpha is kept
        const std::uint32_t bg = frame[0];                            // a black, empty corner stays as it was
        CHECK(filtered[0] == bg);
        Accessibility::Reset();
    }

    {   // ---- Text size: screen-space text grows and rewraps; world text does not ----
        Accessibility::Reset();
        Scene s; s.physicsEnabled = false;
        auto* ui = s.CreateGameObject("T")->AddComponent<TextRenderer>();
        ui->screenSpace = true; ui->pixelSize = 2.0f; ui->text = "alpha beta gamma delta"; ui->size = {200, 40};
        auto* world = s.CreateGameObject("W")->AddComponent<TextRenderer>();
        world->pixelSize = 0.1f;
        CHECK_NEAR(ui->EffectivePixelSize(), 2.0f, 1e-6f);
        Accessibility::SetTextScale(1.5f);
        CHECK_NEAR(ui->EffectivePixelSize(), 3.0f, 1e-6f);
        CHECK_NEAR(world->EffectivePixelSize(), 0.1f, 1e-6f);
        Accessibility::SetTextScale(2.0f);
        CHECK_NEAR(ui->EffectivePixelSize(), 4.0f, 1e-6f);
        ui->wrap = true; ui->pixelSize = 1.0f; ui->size = {300, 100};
        Accessibility::SetTextScale(1.0f);
        std::string scratch;
        const std::string wrapped1 = ui->DisplayText(scratch);
        const int rows1 = (int)std::count(wrapped1.begin(), wrapped1.end(), '\n');
        Accessibility::SetTextScale(2.0f);
        const std::string wrapped2 = ui->DisplayText(scratch);
        const int rows2 = (int)std::count(wrapped2.begin(), wrapped2.end(), '\n');
        CHECK(rows2 > rows1);                                         // bigger text wraps onto more lines
        Accessibility::Reset();
    }

    {   // ---- The Settings screen: every accessibility control sets, applies and saves ----
        Accessibility::Reset();
        Prefs::Clear();
        Scene s("Settings");
        GameObject* settings = UITemplates::AddSettings(s);
        s.Start();
        settings->active = true;
        auto* screen = settings->GetComponent<SettingsScreen>();
        CHECK(screen != nullptr);
        screen->Update(0.016f);
        auto dd = [&](const char* n) { GameObject* g = s.Find(n); return g ? g->GetComponent<UIDropdown>() : nullptr; };
        auto tg = [&](const char* n) { GameObject* g = s.Find(n); return g ? g->GetComponent<UIToggle>() : nullptr; };
        auto* size = dd("TextSizeDropdown");
        auto* filter = dd("ColorFilterDropdown");
        auto* contrast = tg("HighContrastToggle");
        auto* motion = tg("ReduceMotionToggle");
        auto* flashing = tg("ReduceFlashingToggle");
        CHECK(size && filter && contrast && motion && flashing);
        if (size && filter && contrast && motion && flashing) {
            CHECK(size->value == 1 && filter->value == 0 && !contrast->on && !motion->on && !flashing->on);   // defaults shown
            size->value = 4;      screen->Update(0.016f); CHECK_NEAR(Accessibility::Get().textScale, 2.0f, 1e-6f);
            size->value = 0;      screen->Update(0.016f); CHECK_NEAR(Accessibility::Get().textScale, 0.75f, 1e-6f);
            filter->value = 2;    screen->Update(0.016f); CHECK(Accessibility::Get().colorblind == ColorblindMode::Deuteranopia);
            filter->value = 3;    screen->Update(0.016f); CHECK(Accessibility::Get().colorblind == ColorblindMode::Tritanopia);
            contrast->on = true;  screen->Update(0.016f); CHECK(Accessibility::Get().highContrast);
            motion->on = true;    screen->Update(0.016f); CHECK(Accessibility::Get().reduceMotion);
            CHECK(Accessibility::MotionScale() == 0.0f);
            flashing->on = true;  screen->Update(0.016f); CHECK(Accessibility::Get().reduceFlashing);
            CHECK(Accessibility::FlashScale() == 0.0f);

            // Done saves to Prefs; a fresh start reads them back and the screen shows them.
            GameObject* done = s.Find("BackButton");
            auto* btn = done ? done->GetComponent<UIButton>() : nullptr;
            CHECK(btn != nullptr);
            UIWorld().rectOf = [](GameObject* go, Vec2& o, Vec2& sz) { return GetUIScreenRect(go, 1280, 720, o, sz); };
            Vec2 o, sz;
            if (btn && GetUIScreenRect(done, 1280, 720, o, sz)) {
                const Vec2 c{o.x + sz.x * 0.5f, o.y + sz.y * 0.5f};
                Input::FeedMouse(c, 0);
                Input::FeedMouse(c, 1u);
                btn->Update(0.016f);
                screen->LateUpdate(0.016f);
            }
            UIWorld().rectOf = nullptr;
            Input::FeedMouse({0, 0}, 0);
            CHECK(!settings->active);
            CHECK(Prefs::GetString("a11y.colorblind") == "tritanopia");
            CHECK_NEAR(Prefs::GetFloat("a11y.textScale"), 0.75f, 1e-6f);
            CHECK(Prefs::GetInt("a11y.highContrast") == 1 && Prefs::GetInt("a11y.reduceMotion") == 1 && Prefs::GetInt("a11y.reduceFlashing") == 1);

            Accessibility::Reset();                                   // "restart the game"
            Accessibility::Load();
            CHECK(Accessibility::Get().highContrast && Accessibility::Get().reduceMotion);
            CHECK(Accessibility::Get().colorblind == ColorblindMode::Tritanopia);
            settings->active = false; settings->active = true;
            screen->Update(0.016f);
            CHECK(size->value == 0 && filter->value == 3 && contrast->on && motion->on && flashing->on);
        }
        Accessibility::Reset();
        Prefs::Clear();
    }

    TEST_MAIN_RESULT();
}
