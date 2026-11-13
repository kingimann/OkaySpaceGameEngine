#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/UIButton.hpp"
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
/// flashing, subtitles, sound captions, hold-to-toggle and aim assist: applied live, saved to
/// Prefs when Done is pressed). A scene saved before a row existed simply has no control for it,
/// and that row is skipped, so an older Settings object still loads. Done hides the screen and, when a Pause Menu object is in the scene,
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
    /// Aim assist dropdown: Off, Low, Medium, High. Medium is the saved default.
    static constexpr int kAimLevels = 4;
    static AimAssist AimLevel(int i) {
        static const AimAssist v[kAimLevels] = {AimAssist::Off, AimAssist::Low, AimAssist::Medium, AimAssist::High};
        return v[i < 0 ? 0 : i >= kAimLevels ? kAimLevels - 1 : i];
    }
    static int AimIndex(AimAssist level) {
        for (int i = 0; i < kAimLevels; ++i) if (AimLevel(i) == level) return i;
        return 2;
    }

    static void Ensure(Scene& scene) {
        GameObject* root = scene.Find("Settings");
        if (!root) return;
        if (!root->GetComponent<SettingsScreen>()) root->AddComponent<SettingsScreen>();
    }

    void Start() override {
        Bind();
        Pull();
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
        if (!m_open) {
            Bind();
            const bool dragging = (m_music && m_music->IsDragging()) || (m_effects && m_effects->IsDragging());
            if (!dragging) Pull();
            m_open = true;
        }
        Cursor::Capture(false);
        Push();
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
    UIToggle*   m_subtitles = nullptr;
    UIToggle*   m_soundCaptions = nullptr;
    UIToggle*   m_holdToggle = nullptr;
    UIDropdown* m_aim = nullptr;
    UIButton*   m_done = nullptr;
    GameObject* m_returnTo = nullptr;
    bool        m_open = false;
    bool        m_bound = false;

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
        m_subtitles = toggle("SubtitlesToggle");
        m_soundCaptions = toggle("SoundCaptionsToggle");
        m_holdToggle = toggle("HoldToggleToggle");
        if (GameObject* g = Child("AimAssistDropdown")) m_aim = g->GetComponent<UIDropdown>();
        if (GameObject* g = Child("BackButton")) m_done = g->GetComponent<UIButton>();
        m_bound = m_music || m_effects || m_full || m_vsync || m_quality || m_done ||
                  m_textSize || m_colorFilter || m_contrast || m_motion || m_flashing ||
                  m_subtitles || m_soundCaptions || m_holdToggle || m_aim;
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
        if (m_subtitles) m_subtitles->on = a.subtitles.enabled;
        if (m_soundCaptions) m_soundCaptions->on = a.subtitles.soundCaptions;
        if (m_holdToggle) m_holdToggle->on = a.toggleAllHolds;
        if (m_aim) m_aim->value = AimIndex(a.aimAssist);
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
        if (m_subtitles && m_subtitles->on != a.subtitles.enabled) {
            SubtitleSettings sub = a.subtitles;
            sub.enabled = m_subtitles->on;
            Accessibility::SetSubtitles(sub);
        }
        if (m_soundCaptions && m_soundCaptions->on != a.subtitles.soundCaptions) {
            SubtitleSettings sub = Accessibility::Get().subtitles;
            sub.soundCaptions = m_soundCaptions->on;
            Accessibility::SetSubtitles(sub);
        }
        if (m_holdToggle && m_holdToggle->on != a.toggleAllHolds) Accessibility::SetToggleAllHolds(m_holdToggle->on);
        if (m_aim && m_aim->value != AimIndex(a.aimAssist)) Accessibility::SetAimAssist(AimLevel(m_aim->value));
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
