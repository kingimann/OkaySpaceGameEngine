// Focus ring behaviour without pixels: focus moves, a click hides the ring, the theme and the
// high-visibility setting change the style, script and visual script can set focus, and a
// focused widget scrolls into view.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UITemplates.hpp"
#include "okay/Components/UINavigation.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/UI/UIFocus.hpp"

#include <cmath>
#include <string>

using namespace okay;

namespace {

void ReleaseInput() {
    Input::FeedKeys({});
    Input::FeedMouse({0, 0}, 0);
    Input::FeedGamepad({0, 0}, 0);
}

UIButton* Button(Scene& s, const char* name, Vec2 pos) {
    auto* b = s.CreateGameObject(name)->AddComponent<UIButton>();
    b->position = pos;
    b->size = {120, 36};
    return b;
}

void Press(Scene& s, char key) {
    Input::FeedKeys({});
    Input::FeedKeys({key});
    NavigateUI(s);
}

} // namespace

int main() {
    RUN_SUITE("ui_focus");
    UIFocus::Reset();
    Accessibility::Reset();
    ReleaseInput();

    {   // Focus moves with the keyboard. The ring stays hidden until a key, and a click hides it.
        Scene s("move");
        s.physicsEnabled = false;
        UIButton* top = Button(s, "Top", {20, 20});
        UIButton* bot = Button(s, "Bot", {20, 80});
        NavigateUI(s);
        CHECK(top->IsFocused());
        CHECK(!UIFocus::RingVisible());                 // no key yet: focused, but no ring
        Press(s, 's');
        CHECK(bot->IsFocused() && !top->IsFocused());
        CHECK(UIFocus::RingVisible());
        CHECK(UIFocus::Current() == UIFocus::Source::Keyboard);
        Input::FeedKeys({});
        Input::FeedMouse({30, 30}, 0);
        Input::FeedMouse({30, 30}, 1u);
        NavigateUI(s);
        CHECK(!UIFocus::RingVisible());                 // the click hides the ring
        CHECK(bot->IsFocused());                        // the widget stays focused
        Input::FeedMouse({30, 30}, 0);
        Input::FeedGamepad({0, 0}, 0);
        Input::FeedGamepad({0, 0}, 1u << 8);            // D-pad up
        NavigateUI(s);
        CHECK(top->IsFocused());
        CHECK(UIFocus::RingVisible());                  // the pad shows it again
        ReleaseInput();
        UIFocus::Reset();
    }

    {   // Theme style, thickness scale, high visibility, and reduce motion.
        Scene s("style");
        s.physicsEnabled = false;
        auto* theme = s.CreateGameObject("Theme")->AddComponent<UITheme>();
        theme->ApplyPreset((int)UITheme::Preset::Neon);
        GameObject* go = s.CreateGameObject("Btn");
        go->transform->SetParent(theme->gameObject->transform, false);
        go->AddComponent<UIButton>();
        auto* other = s.CreateGameObject("Other")->AddComponent<UITheme>();
        other->ApplyPreset((int)UITheme::Preset::Horror);

        UIFocusStyle st = ResolveUIFocusStyle(go);
        CHECK(st.pulse);
        CHECK(st.color.g > 0.9f && st.color.b > 0.9f && st.color.r < 0.1f);   // neon cyan, not horror red
        CHECK_NEAR(st.thickness, 2.0f, 0.01f);
        CHECK_NEAR(st.padding, 4.0f, 0.01f);

        Accessibility::SetFocusThicknessScale(2.0f);
        st = ResolveUIFocusStyle(go);
        CHECK_NEAR(st.thickness, 4.0f, 0.01f);

        Accessibility::SetFocusHighVisibility(true);
        st = ResolveUIFocusStyle(go);
        CHECK(st.highVisibility);
        CHECK(!st.pulse);
        CHECK(st.color.r > 0.9f && st.color.g > 0.7f && st.color.b < 0.1f);   // yellow
        CHECK(st.thickness >= 6.0f);

        Accessibility::SetFocusHighVisibility(false);
        Accessibility::SetReduceMotion(true);
        st = ResolveUIFocusStyle(go);
        CHECK(!st.pulse);                                                     // a breathing ring holds still
        UIFocus::SetPulseTime(0.0f);
        const float low = UIFocusPulseAlpha(UIFocusStyle{});
        UIFocusStyle breathing;
        breathing.pulse = true;
        UIFocus::SetPulseTime(0.0f);
        const float a0 = UIFocusPulseAlpha(breathing);
        UIFocus::SetPulseTime(0.25f);
        const float a1 = UIFocusPulseAlpha(breathing);
        CHECK(low == 1.0f);
        CHECK(a0 < a1);
        UIFocus::ClearManualPulse();
        Accessibility::Reset();
    }

    {   // The focus look is saved with the theme.
        Scene s("save");
        s.physicsEnabled = false;
        auto* th = s.CreateGameObject("T")->AddComponent<UITheme>();
        th->ApplyPreset((int)UITheme::Preset::Cartoon);
        std::string err;
        Scene loaded("L");
        CHECK(SceneSerializer::Deserialize(loaded, SceneSerializer::Serialize(s), &err));
        auto* back = loaded.Find("T")->GetComponent<UITheme>();
        CHECK(back != nullptr);
        CHECK_NEAR(back->focusThickness, th->focusThickness, 0.01f);
        CHECK_NEAR(back->focusPadding, th->focusPadding, 0.01f);
        CHECK_NEAR(back->focusColor.r, th->focusColor.r, 0.02f);
        CHECK(back->focusPulse == th->focusPulse);
    }

    {   // Text script and visual script set focus, show the ring, and scroll the widget into view.
        Scene s("script");
        s.physicsEnabled = false;
        UIWorld().rectOf = [](GameObject* go, Vec2& o, Vec2& sz) { return GetUIScreenRect(go, 800, 600, o, sz); };
        UIWorld().screenW = 800;
        UIWorld().screenH = 600;
        auto* sv = s.CreateGameObject("sv")->AddComponent<UIScrollView>();
        sv->position = {100, 100};
        sv->size = {300, 200};
        sv->contentHeight = 2000.0f;
        std::vector<UIButton*> rows;
        for (int i = 0; i < 10; ++i)
            rows.push_back(Button(s, ("row" + std::to_string(i)).c_str(), {110, 110.0f + i * 50.0f}));
        for (auto* b : rows) b->gameObject->transform->SetParent(sv->gameObject->transform, false);
        auto* sc = s.CreateGameObject("Ctrl")->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "var missed = 0;\n"
            "function start() {\n"
            "  missed = ui_set_focus(\"missing\");\n"
            "  ui_set_focus(\"row9\");\n"
            "}\n"));
        UIFocus::Reset();
        s.Start();
        CHECK_NEAR(sc->VM()->GetGlobal("missed").AsFloat(), 0.0f, 0.01f);
        CHECK(rows[9]->IsFocused());
        CHECK(UIFocus::RingVisible());
        CHECK(UIFocus::Current() == UIFocus::Source::Script);
        CHECK(sv->scroll > 100.0f);                               // row 9 was below the view

        Scene g("graph");
        g.physicsEnabled = false;
        UIButton* play = Button(g, "Play", {10, 10});
        Button(g, "Quit", {10, 70});
        auto* host = g.CreateGameObject("Host");
        auto* vc = host->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vc->LoadFromText(
            "node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Fn.ui_set_focus\n"
            "default 2 0 \"Play\"\nexec 1 0 2\n", &err));
        UIFocus::Reset();
        g.Start();
        g.Update(1.0f / 60.0f);
        CHECK(play->IsFocused());
        CHECK(UIFocus::RingVisible());
        UIWorld().rectOf = nullptr;
        ReleaseInput();
        UIFocus::Reset();
    }

    {   // The Settings screen grows the two rows, and they save.
        Accessibility::Reset();
        Prefs::Clear();
        Scene s("Settings");
        s.physicsEnabled = false;
        GameObject* settings = UITemplates::AddSettings(s);
        s.Start();
        settings->active = true;
        auto* screen = settings->GetComponent<SettingsScreen>();
        CHECK(screen != nullptr);
        screen->Update(0.016f);
        auto* tg = s.Find("FocusHighVisToggle");
        auto* sl = s.Find("FocusThicknessSlider");
        CHECK(tg && tg->GetComponent<UIToggle>());
        CHECK(sl && sl->GetComponent<UISlider>());
        auto* toggle = tg->GetComponent<UIToggle>();
        auto* slider = sl->GetComponent<UISlider>();
        CHECK(!toggle->on);
        CHECK_NEAR(slider->value, 1.0f, 0.01f);
        CHECK(slider->minValue < 1.0f && slider->maxValue > 1.0f);
        toggle->on = true;
        slider->value = 2.0f;
        screen->Update(0.016f);
        CHECK(Accessibility::Get().focusHighVisibility);
        CHECK_NEAR(Accessibility::Get().focusThicknessScale, 2.0f, 0.01f);
        UIFocusStyle st = ResolveUIFocusStyle(nullptr);
        CHECK(st.highVisibility);
        CHECK(st.thickness >= 6.0f);
        Accessibility::Save();
        CHECK(Prefs::GetInt("a11y.focusHighVis") == 1);
        CHECK_NEAR(Prefs::GetFloat("a11y.focusThickness"), 2.0f, 0.01f);
        Accessibility::Reset();
        Accessibility::Load();
        CHECK(Accessibility::Get().focusHighVisibility);
        CHECK_NEAR(Accessibility::Get().focusThicknessScale, 2.0f, 0.01f);
        Accessibility::Reset();
        Prefs::Clear();
    }

    UIFocus::Reset();
    Accessibility::Reset();
    ReleaseInput();
    TEST_MAIN_RESULT();
}
