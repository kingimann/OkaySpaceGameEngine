#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Canvas.hpp"
#include "okay/Components/EventSystem.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/Cursor.hpp"
#include <string>
#include <vector>

namespace okay {

/// A ready-made pause menu: press the toggle key (Escape by default) to freeze the
/// game and pop up a centred card with Resume / Restart / Main Menu / Quit buttons,
/// a master-volume slider, and a hint line, over a dimmed backdrop. No scripting —
/// drop it on any object. It pauses via the global Game state (so all gameplay
/// freezes and the cursor is freed to click), drives the audio master volume, and
/// Quit asks the host to exit. Buttons also work with keyboard/gamepad navigation.
class PauseMenu : public Behaviour {
public:
    char  toggleKey = 27;                 ///< key that toggles pause (27 = Escape)
    std::string title = "Paused";
    std::string hint  = "Press Esc to resume";
    bool  showResume  = true;
    bool  showRestart = false;            ///< needs `restartScene` set
    bool  showQuit    = true;
    bool  showVolume  = true;             ///< a master-volume slider
    /// Scene to (re)load for Restart / Main Menu (empty = button hidden).
    std::string restartScene;
    std::string mainMenuScene;
    Color dimColor   = Color::FromBytes(8, 10, 16, 180);   ///< backdrop tint
    Color panelColor = Color::FromBytes(28, 32, 44, 235);

    void Update(float) override {
        if (Input::GetKeyDown(toggleKey)) Toggle();
        // Keep the overlay synced to the pause state (another source un-pausing
        // also closes it).
        if (m_built) SetOverlayActive(Game::Paused());
        if (Game::Paused()) {
            Cursor::Capture(false);                       // free the cursor to click
            if (m_volume) AudioMixer::masterVolume = m_volume->value;   // live volume
        }
    }

    // Poll the buttons AFTER they've updated this frame, so a click lands now.
    void LateUpdate(float) override {
        if (!m_built || !Game::Paused()) return;
        if (m_resume && m_resume->WasClicked()) { Resume(); return; }
        if (m_restart && m_restart->WasClicked()) { LoadScene(restartScene); return; }
        if (m_menu && m_menu->WasClicked()) { LoadScene(mainMenuScene); return; }
        if (m_quit && m_quit->WasClicked()) Game::RequestQuit();
    }

    void OnDestroy() override {
        if (Scene* s = GetScene()) for (GameObject* o : m_objects) if (o) s->Destroy(o);
        m_objects.clear(); m_built = false;
        if (Game::Paused()) Game::SetPaused(false);   // never leave the game frozen
    }

    /// Open/close the pause menu programmatically (e.g. from a HUD button).
    void Toggle() { Game::Paused() ? Resume() : Pause(); }
    void Pause()  { Build(); Game::SetPaused(true);  SetOverlayActive(true);  Cursor::Capture(false); }
    void Resume() { Game::SetPaused(false); SetOverlayActive(false); }

private:
    std::vector<GameObject*> m_objects;   ///< every overlay object (toggled together)
    UIButton* m_resume  = nullptr;
    UIButton* m_restart = nullptr;
    UIButton* m_menu    = nullptr;
    UIButton* m_quit    = nullptr;
    UISlider* m_volume  = nullptr;
    bool      m_built   = false;

    void LoadScene(const std::string& path) {
        if (!path.empty()) if (Scene* s = GetScene()) s->RequestLoad(path);
        Game::SetPaused(false);
    }

    // The scene treats `active` per-object (not "active in hierarchy"), so we must
    // toggle every overlay object, not just the root, to show/hide the menu.
    void SetOverlayActive(bool on) { for (GameObject* o : m_objects) if (o) o->active = on; }

    UIButton* AddButton(Scene& s, GameObject* parent, const char* label, float dy, Color col) {
        GameObject* b = s.CreateGameObject(std::string("Pause_") + label);
        auto* btn = b->AddComponent<UIButton>();
        btn->label = label;
        btn->anchor = UIAnchor::Center;
        btn->size = {260, 48};
        btn->position = {-130.0f, dy};      // centred (offset by half width), stacked by dy
        btn->cornerRadius = 10.0f;
        btn->fontScale = 2.4f;
        btn->color = col;
        b->transform->SetParent(parent->transform, false);
        m_objects.push_back(b);
        return btn;
    }

    void Build() {
        if (m_built) return;
        Scene* s = GetScene();
        if (!s) return;

        // UI root: a Canvas that scales with the screen, sorted on top of the HUD,
        // plus an Event System (button input) if the scene doesn't already have one.
        GameObject* root = s->CreateGameObject("PauseMenu UI");
        auto* cv = root->AddComponent<Canvas>();
        cv->scaleMode = Canvas::ScaleMode::ScaleWithScreenSize;
        cv->sortOrder = 1000;
        m_objects.push_back(root);
        if (!s->FindObjectOfType<EventSystem>())
            s->CreateGameObject("EventSystem")->AddComponent<EventSystem>();

        // Full-screen dim backdrop.
        GameObject* dim = s->CreateGameObject("Pause_Dim");
        auto* dp = dim->AddComponent<UIPanel>();
        dp->anchor = UIAnchor::Center; dp->size = {6000, 6000}; dp->position = {-3000, -3000};
        dp->color = dimColor;
        dim->transform->SetParent(root->transform, false);
        m_objects.push_back(dim);

        // The menu card.
        GameObject* card = s->CreateGameObject("Pause_Card");
        auto* pn = card->AddComponent<UIPanel>();
        pn->anchor = UIAnchor::Center; pn->size = {340, 380}; pn->position = {-170, -190};
        pn->color = panelColor; pn->cornerRadius = 14.0f; pn->borderWidth = 1.0f;
        card->transform->SetParent(root->transform, false);
        m_objects.push_back(card);

        GameObject* ttl = s->CreateGameObject("Pause_Title");
        auto* tr = ttl->AddComponent<TextRenderer>();
        tr->text = title; tr->screenSpace = true; tr->anchor = UIAnchor::Center;
        tr->screenPos = {-70, -160}; tr->pixelSize = 4.0f; tr->outline = true;
        ttl->transform->SetParent(root->transform, false);
        m_objects.push_back(ttl);

        float dy = -110.0f;
        if (showResume) { m_resume = AddButton(*s, root, "Resume", dy, Color::FromBytes(60, 130, 90)); dy += 56.0f; }
        if (showRestart && !restartScene.empty()) { m_restart = AddButton(*s, root, "Restart", dy, Color::FromBytes(70, 110, 150)); dy += 56.0f; }
        if (!mainMenuScene.empty()) { m_menu = AddButton(*s, root, "Main Menu", dy, Color::FromBytes(70, 90, 140)); dy += 56.0f; }
        if (showQuit) { m_quit = AddButton(*s, root, "Quit", dy, Color::FromBytes(150, 70, 70)); dy += 56.0f; }

        if (showVolume) {
            GameObject* vlbl = s->CreateGameObject("Pause_VolLabel");
            auto* vt = vlbl->AddComponent<TextRenderer>();
            vt->text = "Volume"; vt->screenSpace = true; vt->anchor = UIAnchor::Center;
            vt->screenPos = {-130, dy + 6.0f}; vt->pixelSize = 1.6f;
            vlbl->transform->SetParent(root->transform, false);
            m_objects.push_back(vlbl);

            GameObject* vol = s->CreateGameObject("Pause_Volume");
            m_volume = vol->AddComponent<UISlider>();
            m_volume->anchor = UIAnchor::Center;
            m_volume->size = {260, 16}; m_volume->position = {-130.0f, dy + 26.0f};
            m_volume->value = AudioMixer::masterVolume;
            m_volume->cornerRadius = 6.0f;
            vol->transform->SetParent(root->transform, false);
            m_objects.push_back(vol);
            dy += 50.0f;
        }

        if (!hint.empty()) {
            GameObject* h = s->CreateGameObject("Pause_Hint");
            auto* htr = h->AddComponent<TextRenderer>();
            htr->text = hint; htr->screenSpace = true; htr->anchor = UIAnchor::Center;
            htr->screenPos = {-110, dy + 16.0f}; htr->pixelSize = 1.4f;
            htr->color = Color::FromBytes(170, 175, 190);
            h->transform->SetParent(root->transform, false);
            m_objects.push_back(h);
        }

        SetOverlayActive(false);
        m_built = true;
    }
};

} // namespace okay
