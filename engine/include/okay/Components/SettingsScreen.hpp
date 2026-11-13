#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIElement.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "okay/Core/DisplaySettings.hpp"
#include "okay/Core/AccessibilitySettings.hpp"
#include "okay/Input/Cursor.hpp"
#include <cmath>
#include <string>

namespace okay {

/// Binds the prebuilt Settings screen (Music / Effects sliders, Fullscreen,
/// VSync, Quality, the Accessibility rows, Done) to the audio mixer, the window and the render quality
/// preset and to Accessibility (text size, colour filter, high contrast, reduced motion and
/// flashing: applied live, saved to Prefs when Done is pressed). Done hides the screen and, when a Pause Menu object is in the scene,
/// shows that again. While the screen is active the cursor is free and
/// GameInputBlocked reports a modal.
class SettingsScreen : public Behaviour {
public:
    static bool AnyOpen(const Scene* scene) {
        if (!scene) return false;
        for (auto* s : scene->FindObjectsOfType<SettingsScreen>())
            if (s && s->enabled && s->gameObject && s->gameObject->active) return true;
        return false;
    }

    /// The template panel is named "Settings". Play on a scene saved before this
    /// component existed still gets the bindings.
    /// The Text size dropdown's choices and the scale each stands for.
    static constexpr int kTextSizes = 5;
    static float TextSizeScale(int i) {
        static const float v[kTextSizes] = {0.75f, 1.0f, 1.25f, 1.5f, 2.0f};
        return v[i < 0 ? 0 : i >= kTextSizes ? kTextSizes - 1 : i];
    }
    static int TextSizeIndex(float scale) {   // the nearest choice
        int best = 0;
        for (int i = 1; i < kTextSizes; ++i)
            if (std::fabs(TextSizeScale(i) - scale) < std::fabs(TextSizeScale(best) - scale)) best = i;
        return best;
    }
    /// The Colour filter dropdown's choices (Off, Protanopia, Deuteranopia, Tritanopia).
    static constexpr int kColorFilters = 4;
    static ColorblindMode ColorFilterMode(int i) {
        static const ColorblindMode v[kColorFilters] = {ColorblindMode::None, ColorblindMode::Protanopia,
                                                        ColorblindMode::Deuteranopia, ColorblindMode::Tritanopia};
        return v[i < 0 ? 0 : i >= kColorFilters ? kColorFilters - 1 : i];
    }
    static int ColorFilterIndex(ColorblindMode m) {
        for (int i = 0; i < kColorFilters; ++i) if (ColorFilterMode(i) == m) return i;
        return 0;   // Achromatopsia has no correction to apply: shown as Off
    }

    static void Ensure(Scene& scene) {
        GameObject* root = scene.Find("Settings");
        if (!root) return;
        if (!root->GetComponent<SettingsScreen>()) root->AddComponent<SettingsScreen>();
        EnsureFocusRows(root);
    }

    /// High-visibility focus and thickness, added under the last row when a saved
    /// Settings screen does not have them yet. Grows the panel so Done stays clear.
    static void EnsureFocusRows(GameObject* root) {
        if (!root || !root->scene() || !root->transform) return;
        auto named = [&](const char* name) -> GameObject* {
            for (Transform* c : root->transform->Children())
                if (c && c->gameObject && c->gameObject->name == name) return c->gameObject;
            return nullptr;
        };
        if (named("FocusHighVisToggle") && named("FocusThicknessSlider")) return;
        float bottom = 0.0f;
        for (Transform* c : root->transform->Children()) {
            if (!c || !c->gameObject || c->gameObject->name == "BackButton") continue;
            UIRect r = GetUIRect(c->gameObject);
            if (!r.valid || !r.position || !r.sizePtr) continue;
            if (r.anchor != UIAnchor::TopLeft && r.anchor != UIAnchor::TopCenter && r.anchor != UIAnchor::TopRight)
                continue;
            bottom = std::max(bottom, r.position->y + r.sizePtr->y);
        }
        const float yToggle = bottom + 12.0f;
        const float ySlider = yToggle + 36.0f;
        Scene& scene = *root->scene();
        auto add = [&](const char* name) {
            GameObject* g = scene.CreateGameObject(name);
            if (g->transform) g->transform->SetParent(root->transform, false);
            return g;
        };
        if (!named("FocusHighVisLabel")) {
            auto* t = add("FocusHighVisLabel")->AddComponent<TextRenderer>();
            t->text = "High-vis focus"; t->screenSpace = true; t->anchor = UIAnchor::TopLeft;
            t->screenPos = {32.0f, yToggle}; t->size = {300.0f, 24.0f}; t->pixelSize = 2.0f;
        }
        if (!named("FocusHighVisToggle")) {
            auto* tg = add("FocusHighVisToggle")->AddComponent<UIToggle>();
            tg->anchor = UIAnchor::TopLeft; tg->position = {364.0f, yToggle}; tg->size = {44.0f, 24.0f};
            tg->style = UIToggle::Style::Switch; tg->label = ""; tg->on = false;
        }
        if (!named("FocusThicknessLabel")) {
            auto* t = add("FocusThicknessLabel")->AddComponent<TextRenderer>();
            t->text = "Thickness"; t->screenSpace = true; t->anchor = UIAnchor::TopLeft;
            t->screenPos = {32.0f, ySlider}; t->size = {140.0f, 24.0f}; t->pixelSize = 2.0f;
        }
        if (!named("FocusThicknessSlider")) {
            auto* sl = add("FocusThicknessSlider")->AddComponent<UISlider>();
            sl->anchor = UIAnchor::TopLeft; sl->position = {180.0f, ySlider + 6.0f}; sl->size = {220.0f, 12.0f};
            sl->minValue = 0.5f; sl->maxValue = 3.0f; sl->value = 1.0f; sl->step = 0.25f;
        }
        if (auto* panel = root->GetComponent<UIPanel>()) {
            float parentH = panel->size.y;
            float buttonTop = parentH - 76.0f;
            if (GameObject* back = named("BackButton")) {
                if (auto* b = back->GetComponent<UIButton>()) {
                    if (b->anchor == UIAnchor::BottomLeft || b->anchor == UIAnchor::BottomCenter ||
                        b->anchor == UIAnchor::BottomRight)
                        buttonTop = b->position.y + parentH - b->size.y;
                    else buttonTop = b->position.y;
                }
            }
            const float needTop = ySlider + 28.0f + 8.0f;
            if (buttonTop < needTop) panel->size.y = parentH + (needTop - buttonTop);
        }
    }

    void Start() override {
        EnsureFocusRows(gameObject);
        Bind();
        BindFocusRows();
        Pull();
        PullFocusRows();
        Scene* s = GetScene();
        if (!s || !gameObject) return;
        // A pause menu in the same scene is the screen the player sees first.
        if (GameObject* pause = s->Find("Pause Menu")) {
            m_returnTo = pause;
            gameObject->active = false;
        }
    }

    void Update(float) override {
        if (!gameObject || !gameObject->active) { m_open = false; return; }
        EnsureFocusRows(gameObject);
        if (!m_open) {
            Bind();
            BindFocusRows();
            const bool dragging = (m_music && m_music->IsDragging()) || (m_effects && m_effects->IsDragging());
            if (!dragging) { Pull(); PullFocusRows(); }
            m_open = true;
        }
        Cursor::Capture(false);
        Push();
        PushFocusRows();
    }

    void LateUpdate(float) override {
        if (!m_open || !m_done || !m_done->WasClicked()) return;
        Close();
    }

private:
    UISlider*   m_music = nullptr;
    UISlider*   m_effects = nullptr;
    UIToggle*   m_full = nullptr;
    UIToggle*   m_vsync = nullptr;
    UIDropdown* m_quality = nullptr;
    UIDropdown* m_textSize = nullptr;
    UIDropdown* m_colorFilter = nullptr;
    UIToggle*   m_contrast = nullptr;
    UIToggle*   m_motion = nullptr;
    UIToggle*   m_flashing = nullptr;
    UIButton*   m_done = nullptr;
    GameObject* m_returnTo = nullptr;
    bool        m_open = false;
    bool        m_bound = false;
    UIToggle*   m_focusHigh = nullptr;
    UISlider*   m_focusThick = nullptr;

    GameObject* Child(const char* name) const {
        if (!gameObject || !gameObject->transform) return nullptr;
        for (Transform* c : gameObject->transform->Children())
            if (c && c->gameObject && c->gameObject->name == name) return c->gameObject;
        return gameObject->scene() ? gameObject->scene()->Find(name) : nullptr;
    }

    void Bind() {
        if (m_bound) return;
        auto slider = [&](const char* n) -> UISlider* {
            GameObject* g = Child(n);
            return g ? g->GetComponent<UISlider>() : nullptr;
        };
        auto toggle = [&](const char* n) -> UIToggle* {
            GameObject* g = Child(n);
            return g ? g->GetComponent<UIToggle>() : nullptr;
        };
        m_music = slider("MusicSlider");
        m_effects = slider("SfxSlider");
        m_full = toggle("FullscreenToggle");
        m_vsync = toggle("VsyncToggle");
        if (GameObject* g = Child("QualityDropdown")) m_quality = g->GetComponent<UIDropdown>();
        if (GameObject* g = Child("TextSizeDropdown")) m_textSize = g->GetComponent<UIDropdown>();
        if (GameObject* g = Child("ColorFilterDropdown")) m_colorFilter = g->GetComponent<UIDropdown>();
        m_contrast = toggle("HighContrastToggle");
        m_motion = toggle("ReduceMotionToggle");
        m_flashing = toggle("ReduceFlashingToggle");
        if (GameObject* g = Child("BackButton")) m_done = g->GetComponent<UIButton>();
        m_bound = m_music || m_effects || m_full || m_vsync || m_quality || m_done ||
                  m_textSize || m_colorFilter || m_contrast || m_motion || m_flashing;
    }

    void Pull() {
        DisplaySettings::Load();
        AudioMixer::LoadSettings();
        if (m_music) m_music->value = AudioMixer::groupVolume[(int)AudioGroup::Music];
        if (m_effects) m_effects->value = AudioMixer::groupVolume[(int)AudioGroup::SFX];
        if (m_full) m_full->on = DisplaySettings::Fullscreen();
        if (m_vsync) m_vsync->on = DisplaySettings::VSync();
        if (m_quality) m_quality->value = DisplaySettings::Quality();
        const AccessibilitySettings& a = Accessibility::Get();
        if (m_textSize) m_textSize->value = TextSizeIndex(a.textScale);
        if (m_colorFilter) m_colorFilter->value = ColorFilterIndex(a.colorblind);
        if (m_contrast) m_contrast->on = a.highContrast;
        if (m_motion) m_motion->on = a.reduceMotion;
        if (m_flashing) m_flashing->on = a.reduceFlashing;
    }

    void Push() {
        bool audio = false;
        if (m_music && m_music->value != AudioMixer::groupVolume[(int)AudioGroup::Music]) {
            AudioMixer::groupVolume[(int)AudioGroup::Music] = m_music->value;
            audio = true;
        }
        if (m_effects && m_effects->value != AudioMixer::groupVolume[(int)AudioGroup::SFX]) {
            AudioMixer::groupVolume[(int)AudioGroup::SFX] = m_effects->value;
            AudioMixer::groupVolume[(int)AudioGroup::UI] = m_effects->value;
            audio = true;
        }
        if (audio) AudioMixer::SaveSettings();
        if (m_full && m_full->on != DisplaySettings::Fullscreen()) DisplaySettings::SetFullscreen(m_full->on);
        if (m_vsync && m_vsync->on != DisplaySettings::VSync()) DisplaySettings::SetVSync(m_vsync->on);
        if (m_quality && m_quality->value != DisplaySettings::Quality()) DisplaySettings::SetQuality(m_quality->value);
        // Accessibility: each row changes its setting the moment it changes, so the screen redraws at the new size.
        const AccessibilitySettings& a = Accessibility::Get();
        if (m_textSize && m_textSize->value != TextSizeIndex(a.textScale)) Accessibility::SetTextScale(TextSizeScale(m_textSize->value));
        if (m_colorFilter && m_colorFilter->value != ColorFilterIndex(a.colorblind))
            Accessibility::SetColorblind(ColorFilterMode(m_colorFilter->value), 1.0f);
        if (m_contrast && m_contrast->on != a.highContrast) Accessibility::SetHighContrast(m_contrast->on);
        if (m_motion && m_motion->on != a.reduceMotion) Accessibility::SetReduceMotion(m_motion->on);
        if (m_flashing && m_flashing->on != a.reduceFlashing) Accessibility::SetReduceFlashing(m_flashing->on);
    }

    void BindFocusRows() {
        m_focusHigh = nullptr;
        m_focusThick = nullptr;
        if (GameObject* g = Child("FocusHighVisToggle")) m_focusHigh = g->GetComponent<UIToggle>();
        if (GameObject* g = Child("FocusThicknessSlider")) m_focusThick = g->GetComponent<UISlider>();
    }
    void PullFocusRows() {
        const AccessibilitySettings& a = Accessibility::Get();
        if (m_focusHigh) m_focusHigh->on = a.focusHighVisibility;
        if (m_focusThick) m_focusThick->value = a.focusThicknessScale;
    }
    void PushFocusRows() {
        const AccessibilitySettings& a = Accessibility::Get();
        if (m_focusHigh && m_focusHigh->on != a.focusHighVisibility)
            Accessibility::SetFocusHighVisibility(m_focusHigh->on);
        const AccessibilitySettings& now = Accessibility::Get();
        if (m_focusThick && std::fabs(m_focusThick->value - now.focusThicknessScale) > 1e-4f)
            Accessibility::SetFocusThicknessScale(m_focusThick->value);
    }

    void Close() {
        AudioMixer::SaveSettings();
        DisplaySettings::Save();
        Accessibility::Save();
        m_open = false;
        if (gameObject) gameObject->active = false;
        if (m_returnTo) m_returnTo->active = true;
        else if (Scene* s = GetScene())
            if (GameObject* pause = s->Find("Pause Menu")) pause->active = true;
    }
};

} // namespace okay
