#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Canvas.hpp"
#include "okay/Components/EventSystem.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/Cursor.hpp"
#include <string>

namespace okay {

/// A ready-made pause menu: press the toggle key (Escape by default) to freeze the
/// game and pop up a centred panel with Resume / Quit (and an optional Main Menu)
/// button, plus a dimmed backdrop. No scripting — drop it on any object and you
/// have a working pause + quit flow. It pauses via the global Game state (so all
/// gameplay freezes and the cursor is freed to click), and Quit asks the host to
/// exit. The overlay is built lazily the first time you pause.
class PauseMenu : public Behaviour {
public:
    char  toggleKey = 27;                 ///< key that toggles pause (27 = Escape)
    std::string title = "Paused";
    bool  showResume  = true;
    bool  showQuit    = true;
    /// Optional scene to load on "Main Menu" (empty = no Main Menu button).
    std::string mainMenuScene;
    Color dimColor   = Color::FromBytes(8, 10, 16, 180);   ///< backdrop tint
    Color panelColor = Color::FromBytes(28, 32, 44, 235);

    void Update(float) override {
        if (Input::GetKeyDown(toggleKey)) Toggle();
        // Keep the overlay's visibility synced to the pause state (so another
        // source un-pausing also hides it).
        if (m_built) SetOverlayActive(Game::Paused());
        if (Game::Paused()) Cursor::Capture(false);   // hold the cursor free to click
    }

    // Poll the buttons AFTER they've updated this frame (LateUpdate), so a click
    // registers the same frame.
    void LateUpdate(float) override {
        if (!m_built || !Game::Paused()) return;
        if (m_resume && m_resume->WasClicked()) { Resume(); return; }
        if (m_menu && m_menu->WasClicked()) {
            if (Scene* s = GetScene()) s->RequestLoad(mainMenuScene);
            Game::SetPaused(false);
            return;
        }
        if (m_quit && m_quit->WasClicked()) Game::RequestQuit();
    }

    void OnDestroy() override {
        if (Scene* s = GetScene()) { if (m_root) s->Destroy(m_root); }
        m_root = nullptr; m_built = false;
        if (Game::Paused()) Game::SetPaused(false);   // don't leave the game frozen
    }

    /// Open/close the pause menu programmatically (e.g. from a HUD button).
    void Toggle() { Game::Paused() ? Resume() : Pause(); }
    void Pause()  { Build(); Game::SetPaused(true);  SetOverlayActive(true);  Cursor::Capture(false); }
    void Resume() { Game::SetPaused(false); SetOverlayActive(false); }

private:
    GameObject* m_root   = nullptr;
    UIButton*   m_resume = nullptr;
    UIButton*   m_quit   = nullptr;
    UIButton*   m_menu   = nullptr;
    bool        m_built  = false;

    void SetOverlayActive(bool on) { if (m_root) m_root->active = on; }

    UIButton* AddButton(Scene& s, GameObject* canvas, const char* label, float dy, Color col) {
        GameObject* b = s.CreateGameObject(std::string("Pause_") + label);
        auto* btn = b->AddComponent<UIButton>();
        btn->label = label;
        btn->anchor = UIAnchor::Center;
        btn->size = {260, 52};
        btn->position = {-130.0f, dy};      // centred (offset by half width), stacked by dy
        btn->cornerRadius = 10.0f;
        btn->fontScale = 2.5f;
        btn->color = col;
        b->transform->SetParent(canvas->transform, false);
        return btn;
    }

    void Build() {
        if (m_built) return;
        Scene* s = GetScene();
        if (!s) return;

        // A UI root with its own Event System (needed for button input) and a Canvas
        // that scales with the screen, sorted on top of the game's HUD.
        m_root = s->CreateGameObject("PauseMenu UI");
        auto* cv = m_root->AddComponent<Canvas>();
        cv->scaleMode = Canvas::ScaleMode::ScaleWithScreenSize;
        cv->sortOrder = 1000;                // draw above everything else
        if (!s->FindObjectOfType<EventSystem>())
            s->CreateGameObject("EventSystem")->AddComponent<EventSystem>();

        // Full-screen dim backdrop (a big centred panel).
        GameObject* dim = s->CreateGameObject("Pause_Dim");
        auto* dp = dim->AddComponent<UIPanel>();
        dp->anchor = UIAnchor::Center;
        dp->size = {6000, 6000};
        dp->position = {-3000, -3000};
        dp->color = dimColor;
        dim->transform->SetParent(m_root->transform, false);

        // The menu card.
        GameObject* card = s->CreateGameObject("Pause_Card");
        auto* pn = card->AddComponent<UIPanel>();
        pn->anchor = UIAnchor::Center;
        pn->size = {320, 300};
        pn->position = {-160, -150};
        pn->color = panelColor;
        pn->cornerRadius = 14.0f;
        pn->borderWidth = 1.0f;
        card->transform->SetParent(m_root->transform, false);

        GameObject* ttl = s->CreateGameObject("Pause_Title");
        auto* tr = ttl->AddComponent<TextRenderer>();
        tr->text = title;
        tr->screenSpace = true;
        tr->anchor = UIAnchor::Center;
        tr->screenPos = {-70, -120};
        tr->pixelSize = 4.0f;
        tr->outline = true;
        ttl->transform->SetParent(m_root->transform, false);

        float dy = -60.0f;
        if (showResume) { m_resume = AddButton(*s, m_root, "Resume", dy, Color::FromBytes(60, 130, 90)); dy += 64.0f; }
        if (!mainMenuScene.empty()) { m_menu = AddButton(*s, m_root, "Main Menu", dy, Color::FromBytes(70, 90, 140)); dy += 64.0f; }
        if (showQuit) { m_quit = AddButton(*s, m_root, "Quit", dy, Color::FromBytes(150, 70, 70)); dy += 64.0f; }

        m_root->active = false;
        m_built = true;
    }
};

} // namespace okay
