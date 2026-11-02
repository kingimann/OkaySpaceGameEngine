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
#include "okay/Input/Cursor.hpp"
#include <string>

namespace okay {

/// Binds the prebuilt Settings screen (Music / Effects sliders, Fullscreen,
/// VSync, Quality, Done) to the audio mixer, the window and the render quality
/// preset. Done hides the screen and, when a Pause Menu object is in the scene,
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
        if (GameObject* g = Child("BackButton")) m_done = g->GetComponent<UIButton>();
        m_bound = m_music || m_effects || m_full || m_vsync || m_quality || m_done;
    }

    void Pull() {
        DisplaySettings::Load();
        AudioMixer::LoadSettings();
        if (m_music) m_music->value = AudioMixer::groupVolume[(int)AudioGroup::Music];
        if (m_effects) m_effects->value = AudioMixer::groupVolume[(int)AudioGroup::SFX];
        if (m_full) m_full->on = DisplaySettings::Fullscreen();
        if (m_vsync) m_vsync->on = DisplaySettings::VSync();
        if (m_quality) m_quality->value = DisplaySettings::Quality();
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
    }

    void Close() {
        AudioMixer::SaveSettings();
        DisplaySettings::Save();
        m_open = false;
        if (gameObject) gameObject->active = false;
        if (m_returnTo) m_returnTo->active = true;
        else if (Scene* s = GetScene())
            if (GameObject* pause = s->Find("Pause Menu")) pause->active = true;
    }
};

} // namespace okay
