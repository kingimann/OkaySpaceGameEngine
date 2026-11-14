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
#include "okay/Core/DisplayModes.hpp"
#include "okay/Core/AccessibilitySettings.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Input/Cursor.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace okay {

/// Binds the prebuilt Settings screen (Music / Effects, Monitor, Display mode,
/// Resolution, Refresh rate, VSync, Quality, the Accessibility rows, Apply and Done)
/// to the audio mixer, the window and the render quality preset and to Accessibility
/// (text size, colour filter, high contrast, reduced motion and flashing, subtitles,
/// sound captions, hold-to-toggle and aim assist).
/// Monitor, display mode, resolution and refresh wait for Apply, then ask
/// "Keep these settings?" for 10 seconds and revert on their own if nobody keeps them.
/// VSync, quality, volume and the accessibility rows apply as they change (saved to
/// Prefs when Done is pressed). A scene saved before a row existed simply has no control
/// for it, and that row is skipped, so an older Settings object still loads.
/// Done hides the screen (and keeps a pending display change) and, when a Pause Menu
/// is in the scene, shows that again. While the screen is active the cursor is free
/// and GameInputBlocked reports a modal.
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
        const bool picking = (m_monitor && m_monitor->open) || (m_mode && m_mode->open) ||
                             (m_resolution && m_resolution->open) || (m_refresh && m_refresh->open);
        if (!picking && DisplaySettings::Generation() != m_seenGen) PullDisplay();
        else FillDisplayChoices();
        Push();
        RefreshConfirm();
        PushFocusRows();
    }

    void LateUpdate(float) override {
        if (!m_open) return;
        if (m_apply && m_apply->WasClicked()) ApplyDraft();
        if (m_keep && m_keep->WasClicked()) { DisplaySettings::KeepTrial(); RefreshConfirm(); }
        if (m_revert && m_revert->WasClicked()) { DisplaySettings::RevertTrial(); PullDisplay(); RefreshConfirm(); }
        if (m_done && m_done->WasClicked()) {
            if (DisplaySettings::TrialActive()) DisplaySettings::KeepTrial();
            Close();
        }
    }

private:
    UISlider*   m_music = nullptr;
    UISlider*   m_effects = nullptr;
    UIToggle*   m_full = nullptr;
    UIDropdown* m_monitor = nullptr;
    UIDropdown* m_mode = nullptr;
    UIDropdown* m_resolution = nullptr;
    UIDropdown* m_refresh = nullptr;
    UIButton*   m_apply = nullptr;
    UIButton*   m_keep = nullptr;
    UIButton*   m_revert = nullptr;
    GameObject* m_confirm = nullptr;
    GameObject* m_keepGo = nullptr;
    GameObject* m_revertGo = nullptr;
    TextRenderer* m_confirmText = nullptr;
    UIToggle*   m_vsync = nullptr;
    UIDropdown* m_quality = nullptr;
    int         m_seenGen = -1;
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
        if (GameObject* g = Child("MonitorDropdown")) m_monitor = g->GetComponent<UIDropdown>();
        if (GameObject* g = Child("DisplayModeDropdown")) m_mode = g->GetComponent<UIDropdown>();
        if (GameObject* g = Child("ResolutionDropdown")) m_resolution = g->GetComponent<UIDropdown>();
        if (GameObject* g = Child("RefreshDropdown")) m_refresh = g->GetComponent<UIDropdown>();
        if (GameObject* g = Child("ApplyButton")) m_apply = g->GetComponent<UIButton>();
        if (GameObject* g = Child("KeepButton")) { m_keepGo = g; m_keep = g->GetComponent<UIButton>(); }
        if (GameObject* g = Child("RevertButton")) { m_revertGo = g; m_revert = g->GetComponent<UIButton>(); }
        if (GameObject* g = Child("ConfirmLabel")) { m_confirm = g; m_confirmText = g->GetComponent<TextRenderer>(); }
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
        m_bound = m_music || m_effects || m_full || m_vsync || m_quality || m_done || m_monitor || m_mode ||
                  m_resolution || m_refresh || m_apply ||
                  m_textSize || m_colorFilter || m_contrast || m_motion || m_flashing ||
                  m_subtitles || m_soundCaptions || m_holdToggle || m_aim;
    }

    void FillDisplayChoices() {
        if (m_monitor) {
            std::vector<std::string> names;
            const auto& mons = DisplaySettings::Monitors();
            if (mons.empty()) names.push_back("This display");
            else for (const DisplayMonitor& m : mons)
                names.push_back(m.name.empty() ? "Display " + std::to_string(m.index + 1) : m.name);
            if (m_monitor->options != names) m_monitor->options = std::move(names);
            if (m_monitor->value >= (int)m_monitor->options.size()) m_monitor->value = (int)m_monitor->options.size() - 1;
            if (m_monitor->value < 0) m_monitor->value = 0;
        }
        if (m_mode) {
            const std::vector<std::string> modes = {"Windowed", "Borderless", "Fullscreen"};
            if (m_mode->options != modes) m_mode->options = modes;
            if (m_mode->value < 0) m_mode->value = 0;
            if (m_mode->value > 2) m_mode->value = 2;
        }
        const int mon = m_monitor ? m_monitor->value : 0;
        const bool haveMons = !DisplaySettings::Monitors().empty();
        const std::vector<DisplayMode> modeList = haveMons ? DisplaySettings::MonitorModes(mon) : std::vector<DisplayMode>{};
        const std::vector<DisplayResolution> sizes = modeList.empty() ? DisplaySettings::Resolutions() : ResolutionsFromModes(modeList);
        if (m_resolution) {
            std::vector<std::string> labels;
            for (const DisplayResolution& r : sizes) labels.push_back(r.Label());
            if (labels.empty()) labels.push_back("Default");
            if (m_resolution->options != labels) m_resolution->options = std::move(labels);
            if (m_resolution->value >= (int)m_resolution->options.size()) m_resolution->value = (int)m_resolution->options.size() - 1;
            if (m_resolution->value < 0) m_resolution->value = 0;
        }
        int rw = 0, rh = 0;
        if (m_resolution && !sizes.empty()) {
            int i = m_resolution->value;
            if (i < 0) i = 0;
            if (i >= (int)sizes.size()) i = (int)sizes.size() - 1;
            rw = sizes[(std::size_t)i].width;
            rh = sizes[(std::size_t)i].height;
        }
        if (m_refresh) {
            const std::vector<int> rates = modeList.empty() ? std::vector<int>{} : RefreshRatesFor(modeList, rw, rh);
            std::vector<std::string> labels;
            if (rates.empty()) labels.push_back("Default");
            else for (int hz : rates) labels.push_back(std::to_string(hz) + " Hz");
            if (m_refresh->options != labels) m_refresh->options = std::move(labels);
            if (m_refresh->value >= (int)m_refresh->options.size()) m_refresh->value = (int)m_refresh->options.size() - 1;
            if (m_refresh->value < 0) m_refresh->value = 0;
        }
    }

    void PullDisplay() {
        FillDisplayChoices();
        if (m_mode) m_mode->value = (int)DisplaySettings::Mode();
        if (m_monitor) {
            int idx = DisplaySettings::MonitorIndex();
            if (idx < 0 || idx >= (int)m_monitor->options.size()) idx = 0;
            m_monitor->value = idx;
        }
        FillDisplayChoices();
        if (m_resolution && DisplaySettings::HasResolution()) {
            const std::string label = DisplaySettings::Resolution().Label();
            for (int i = 0; i < (int)m_resolution->options.size(); ++i)
                if (m_resolution->options[(std::size_t)i] == label) m_resolution->value = i;
        }
        if (m_refresh) {
            m_refresh->value = 0;
            if (DisplaySettings::RefreshHz() > 0) {
                const std::string label = std::to_string(DisplaySettings::RefreshHz()) + " Hz";
                for (int i = 0; i < (int)m_refresh->options.size(); ++i)
                    if (m_refresh->options[(std::size_t)i] == label) m_refresh->value = i;
            }
        }
        m_seenGen = DisplaySettings::Generation();
    }

    void RefreshConfirm() {
        const bool on = DisplaySettings::TrialActive();
        if (m_confirm) m_confirm->active = on;
        if (on && m_confirmText) {
            int secs = (int)(DisplaySettings::TrialSecondsLeft() + 0.999f);
            if (secs < 1) secs = 1;
            m_confirmText->text = "Keep these settings? " + std::to_string(secs);
        }
        if (m_keepGo) m_keepGo->active = on;
        if (m_revertGo) m_revertGo->active = on;
    }

    void ApplyDraft() {
        WindowMode mode = m_mode ? (WindowMode)std::clamp(m_mode->value, 0, 2) : DisplaySettings::Mode();
        int w = 0, h = 0;
        if (m_resolution && m_resolution->value >= 0 && m_resolution->value < (int)m_resolution->options.size()) {
            const std::string& label = m_resolution->options[(std::size_t)m_resolution->value];
            if (std::sscanf(label.c_str(), "%d x %d", &w, &h) != 2) { w = 0; h = 0; }
        }
        int hz = 0;
        if (m_refresh && m_refresh->value >= 0 && m_refresh->value < (int)m_refresh->options.size()) {
            const std::string& label = m_refresh->options[(std::size_t)m_refresh->value];
            if (label != "Default") hz = std::atoi(label.c_str());
        }
        const int mon = m_monitor ? m_monitor->value : -1;
        const bool monitorChange = !DisplaySettings::Monitors().empty() && mon >= 0 &&
            mon != DisplaySettings::MonitorIndex() && !(DisplaySettings::MonitorIndex() < 0 && mon == 0);
        const bool modeChange = mode != DisplaySettings::Mode();
        const bool sizeChange = w > 0 && (w != DisplaySettings::Width() || h != DisplaySettings::Height());
        const bool rateChange = hz != DisplaySettings::RefreshHz();
        if (!monitorChange && !modeChange && !sizeChange && !rateChange) return;
        if (!DisplaySettings::TrialActive()) DisplaySettings::BeginTrial(10.f);
        if (monitorChange) DisplaySettings::SetMonitorIndex(mon);
        if (modeChange) DisplaySettings::SetMode(mode);
        if (sizeChange) DisplaySettings::SetResolution(w, h);
        if (rateChange) DisplaySettings::SetRefreshHz(hz);
        m_seenGen = DisplaySettings::Generation();
        RefreshConfirm();
    }

    void Pull() {
        DisplaySettings::Load();
        AudioMixer::LoadSettings();
        if (m_music) m_music->value = AudioMixer::groupVolume[(int)AudioGroup::Music];
        if (m_effects) m_effects->value = AudioMixer::groupVolume[(int)AudioGroup::SFX];
        if (m_full) m_full->on = DisplaySettings::Fullscreen();
        PullDisplay();
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
