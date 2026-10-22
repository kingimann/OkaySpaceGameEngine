#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/UIAnimation.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UIButton.hpp"
#include <cmath>

using namespace okay;

// UI Animation: intros settle back on the authored layout, fades reach children,
// outros hide the object, loops move about the widget's centre.
int main() {
    RUN_SUITE("ui_animation");
    UICanvas::Set(1280, 720);

    {   // Fade in: alpha climbs to 1; a child under the panel fades with it.
        Scene s; s.physicsEnabled = false;
        GameObject* panel = s.CreateGameObject("Panel");
        auto* pn = panel->AddComponent<UIPanel>();
        pn->anchor = UIAnchor::Center; pn->position = {0, 0}; pn->size = {300, 200};
        GameObject* btn = s.CreateGameObject("Button");
        btn->AddComponent<UIButton>()->anchor = UIAnchor::Center;
        btn->transform->SetParent(panel->transform, false);
        auto* ua = panel->AddComponent<UIAnimation>();
        ua->intro = UIAnimation::Intro::FadeIn; ua->duration = 0.5f;
        s.Start();
        CHECK(UIOpacity(panel) < 0.2f);
        CHECK(UIOpacity(btn) < 0.2f);              // inherited
        s.Update(0.25f);
        CHECK(UIOpacity(btn) > 0.3f && UIOpacity(btn) < 1.0f);
        s.Update(0.3f);
        CHECK(std::fabs(UIOpacity(btn) - 1.0f) < 1e-4f);
    }

    {   // Slide in from the left with a delay: offset at first, exactly home at the end.
        Scene s; s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("B");
        auto* b = go->AddComponent<UIButton>();
        b->anchor = UIAnchor::TopRight; b->position = {20, 30}; b->size = {200, 60};
        auto* ua = go->AddComponent<UIAnimation>();
        ua->intro = UIAnimation::Intro::SlideLeft; ua->duration = 0.4f; ua->delay = 0.2f; ua->distance = 100.0f;
        s.Start();
        Vec2 o, sz;
        GetUIScreenRect(go, 1280, 720, o, sz);
        float homeX = 1280.0f - 20.0f - 200.0f;
        CHECK(std::fabs(o.x - (homeX - 100.0f)) < 1.0f);   // held at the start pose during the delay
        s.Update(0.1f);
        GetUIScreenRect(go, 1280, 720, o, sz);
        CHECK(std::fabs(o.x - (homeX - 100.0f)) < 1.0f);
        for (int i = 0; i < 10; ++i) s.Update(0.1f);
        CHECK(std::fabs(b->position.x - 20.0f) < 1e-3f && std::fabs(b->position.y - 30.0f) < 1e-3f);
        CHECK(std::fabs(b->size.x - 200.0f) < 1e-3f);
    }

    {   // Pulse grows about the centre; Stop restores the authored rect.
        Scene s; s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("P");
        auto* b = go->AddComponent<UIButton>();
        b->anchor = UIAnchor::TopLeft; b->position = {100, 100}; b->size = {200, 100};
        auto* ua = go->AddComponent<UIAnimation>();
        ua->intro = UIAnimation::Intro::None; ua->loop = UIAnimation::Loop::Pulse; ua->loopAmount = 0.2f;
        s.Start();
        s.Update(0.25f);   // quarter cycle: biggest
        Vec2 o, sz; GetUIScreenRect(go, 1280, 720, o, sz);
        CHECK(sz.x > 230.0f);
        CHECK(std::fabs((o.x + sz.x * 0.5f) - 200.0f) < 0.5f);   // centre kept
        CHECK(std::fabs((o.y + sz.y * 0.5f) - 150.0f) < 0.5f);
        ua->Stop();
        CHECK(std::fabs(b->position.x - 100.0f) < 1e-3f && std::fabs(b->size.x - 200.0f) < 1e-3f);
    }

    {   // Outro hides the object at the end (and restores its layout for next time).
        Scene s; s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("O");
        auto* b = go->AddComponent<UIButton>();
        b->anchor = UIAnchor::Center; b->position = {0, 0}; b->size = {120, 40};
        auto* ua = go->AddComponent<UIAnimation>();
        ua->intro = UIAnimation::Intro::None; ua->outro = UIAnimation::Outro::Shrink; ua->duration = 0.2f;
        s.Start();
        ua->PlayOut();
        s.Update(0.1f);
        CHECK(go->active && b->size.x < 120.0f);
        s.Update(0.15f);
        CHECK(!go->active);
        CHECK(std::fabs(b->size.x - 120.0f) < 1e-3f);
    }

    TEST_MAIN_RESULT();
}
