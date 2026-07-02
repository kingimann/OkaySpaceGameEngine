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

    // Build the overlay up front (hidden), so pausing is an instant show/hide instead
    // of creating UI objects mid-frame (which popped in a frame late and read as a
    // glitch). Created objects flush next frame; the menu stays hidden until paused.
    void Start() override { EnsureBuilt(); SetOverlayActive(false); m_shown = false; }

    void Update(float) override {
        if (!m_built) EnsureBuilt();
        if (Input::GetKeyDown(toggleKey)) Toggle();
        // Keep the overlay synced to the pause state (another source un-pausing also
        // closes it). Only touch it on a change so we don't fight other systems.
        bool paused = Game::Paused();
        if (paused != m_shown) { SetOverlayActive(paused); m_shown = paused; }
        if (paused) {
            Cursor::Capture(false);            // free the cursor so the buttons are clickable
            Input::SetUICaptured(true);        // the world ignores clicks/keys behind the menu
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
        // The UI lives in the scene now (editable + serialized), so DON'T destroy it
        // here — just drop our references and never leave the game frozen.
        m_objects.clear(); m_root = nullptr; m_built = false; m_shown = false;
        m_resume = nullptr; m_restart = nullptr; m_menu = nullptr; m_quit = nullptr; m_volume = nullptr;
        if (Game::Paused()) { Game::SetPaused(false); Input::SetUICaptured(false); }
    }

    /// Open/close the pause menu programmatically (e.g. from a HUD button).
    void Toggle() { Game::Paused() ? Resume() : Pause(); }
    void Pause()  { EnsureBuilt(); Game::SetPaused(true);  SetOverlayActive(true);  m_shown = true;
                    Cursor::Capture(false); Input::SetUICaptured(true); }

    /// Create the editable pause-menu UI (Canvas, panels, buttons, slider) as real
    /// child objects if it isn't there yet — call this from the editor when the
    /// component is added so the UI is in the scene to customize (and saved), instead
    /// of being spawned at Play. Returns the UI root.
    GameObject* EnsureBuilt() {
        if (m_built) return m_root;
        if (!GetScene()) return nullptr;
        if (GameObject* r = FindUIRoot()) Adopt(r);   // already in the scene — reuse it
        else Build();                                 // first time — create the editable UI
        return m_root;
    }
    bool HasUI() const { return FindUIRoot() != nullptr; }
    void Resume() { Game::SetPaused(false); SetOverlayActive(false); m_shown = false; Input::SetUICaptured(false); }

private:
    std::vector<GameObject*> m_objects;   ///< every overlay object (toggled together)
    GameObject* m_root  = nullptr;
    UIButton* m_resume  = nullptr;
    UIButton* m_restart = nullptr;
    UIButton* m_menu    = nullptr;
    UIButton* m_quit    = nullptr;
    UISlider* m_volume  = nullptr;
    bool      m_built   = false;
    bool      m_shown   = false;

    // The existing UI root (a child "PauseMenu UI"), if the editable menu is already
    // in the scene — so Play reuses it instead of spawning a new one.
    GameObject* FindUIRoot() const {
        if (!gameObject || !gameObject->transform) return nullptr;
        for (Transform* c : gameObject->transform->Children())
            if (c && c->gameObject && c->gameObject->name == "PauseMenu UI") return c->gameObject;
        return nullptr;
    }
    static void Collect(GameObject* o, std::vector<GameObject*>& out) {
        if (!o) return;
        out.push_back(o);
        if (o->transform) for (Transform* c : o->transform->Children()) if (c) Collect(c->gameObject, out);
    }
    // Reuse the pre-built editable UI: gather its objects + wire the buttons by name.
    void Adopt(GameObject* root) {
        m_root = root;
        m_objects.clear();
        Collect(root, m_objects);
        for (GameObject* o : m_objects) {
            if (!o) continue;
            if (o->name == "Pause_Resume")    m_resume  = o->GetComponent<UIButton>();
            else if (o->name == "Pause_Restart")   m_restart = o->GetComponent<UIButton>();
            else if (o->name == "Pause_Main Menu") m_menu    = o->GetComponent<UIButton>();
            else if (o->name == "Pause_Quit")      m_quit    = o->GetComponent<UIButton>();
            else if (o->name == "Pause_Volume")    m_volume  = o->GetComponent<UISlider>();
        }
        m_built = true;   // adopt visibility as-is; Start() hides at Play
    }

    void LoadScene(const std::string& path) {
        if (!path.empty()) if (Scene* s = GetScene()) s->RequestLoad(path);
        Game::SetPaused(false);
    }

    // The scene treats `active` per-object (not "active in hierarchy"), so we must
    // toggle every overlay object, not just the root, to show/hide the menu.
    void SetOverlayActive(bool on) { for (GameObject* o : m_objects) if (o) o->active = on; }

    // Layout constants (unscaled pixels; the Canvas scales them to the screen).
    static constexpr float kCardW = 360.0f, kPad = 20.0f;
    static constexpr float kTitleH = 54.0f, kBtnW = 264.0f, kBtnH = 46.0f, kBtnGap = 10.0f;
    static constexpr float kVolLblH = 18.0f, kVolBarH = 16.0f, kVolGap = 6.0f;
    static constexpr float kHintH = 18.0f, kSection = 16.0f;

    UIButton* AddButton(Scene& s, GameObject* parent, const char* label, float y, Color col) {
        GameObject* b = s.CreateGameObject(std::string("Pause_") + label);
        auto* btn = b->AddComponent<UIButton>();
        btn->label = label;
        btn->anchor = UIAnchor::Center;
        btn->size = {kBtnW, kBtnH};
        btn->position = {-kBtnW * 0.5f, y};     // centred horizontally, stacked vertically
        btn->cornerRadius = 10.0f;
        btn->fontScale = 2.2f;
        btn->color = col;
        btn->hoverColor = Color{col.r + (1 - col.r) * 0.22f, col.g + (1 - col.g) * 0.22f, col.b + (1 - col.b) * 0.22f, col.a};
        btn->pressedColor = Color{col.r * 0.8f, col.g * 0.8f, col.b * 0.8f, col.a};
        btn->hoverScale = 1.03f;
        btn->shadow = true; btn->shadowOffset = {0.0f, 3.0f}; btn->shadowColor = Color::FromBytes(0, 0, 0, 130);
        btn->borderWidth = 1.0f; btn->borderColor = Color{1, 1, 1, 0.18f};
        b->transform->SetParent(parent->transform, false);
        m_objects.push_back(b);
        return btn;
    }

    // A centred screen-space label inside a full-width box (so it never drifts off-centre).
    void AddLabel(Scene& s, GameObject* parent, const char* name, const std::string& text,
                  float y, float h, float px, Color colr, bool outline) {
        GameObject* t = s.CreateGameObject(name);
        auto* tr = t->AddComponent<TextRenderer>();
        tr->text = text; tr->screenSpace = true; tr->anchor = UIAnchor::Center;
        tr->size = {kCardW - kPad * 2, h};
        tr->screenPos = {-(kCardW - kPad * 2) * 0.5f, y};
        tr->align = 1; tr->vcenter = true; tr->pixelSize = px; tr->color = colr; tr->outline = outline;
        t->transform->SetParent(parent->transform, false);
        m_objects.push_back(t);
    }

    void Build() {
        if (m_built) return;
        Scene* s = GetScene();
        if (!s) return;

        // Count visible buttons so the card hugs its content (no empty space / overflow).
        bool wantRestart = showRestart && !restartScene.empty();
        bool wantMenu    = !mainMenuScene.empty();
        int  nBtn = (showResume ? 1 : 0) + (wantRestart ? 1 : 0) + (wantMenu ? 1 : 0) + (showQuit ? 1 : 0);

        float bodyH = kTitleH;
        if (nBtn > 0) bodyH += kSection + nBtn * kBtnH + (nBtn - 1) * kBtnGap;
        if (showVolume) bodyH += kSection + kVolLblH + kVolGap + kVolBarH;
        if (!hint.empty()) bodyH += kSection + kHintH;
        float cardH = bodyH + kPad * 2.0f;

        // UI root: a Canvas that scales with the screen, sorted on top of the HUD,
        // plus an Event System (button input) if the scene doesn't already have one.
        GameObject* root = s->CreateGameObject("PauseMenu UI");
        auto* cv = root->AddComponent<Canvas>();
        cv->scaleMode = Canvas::ScaleMode::ScaleWithScreenSize;
        cv->sortOrder = 1000;
        if (gameObject && gameObject->transform) root->transform->SetParent(gameObject->transform, false);
        m_root = root;
        m_objects.push_back(root);
        if (!s->FindObjectOfType<EventSystem>())
            s->CreateGameObject("EventSystem")->AddComponent<EventSystem>();

        // Full-screen dim backdrop (created first so it draws behind the card).
        GameObject* dim = s->CreateGameObject("Pause_Dim");
        auto* dp = dim->AddComponent<UIPanel>();
        dp->anchor = UIAnchor::Center; dp->size = {8000, 8000}; dp->position = {-4000, -4000};
        dp->color = dimColor;
        dim->transform->SetParent(root->transform, false);
        m_objects.push_back(dim);

        // The menu card, centred and sized to its content.
        GameObject* card = s->CreateGameObject("Pause_Card");
        auto* pn = card->AddComponent<UIPanel>();
        pn->anchor = UIAnchor::Center; pn->size = {kCardW, cardH};
        pn->position = {-kCardW * 0.5f, -cardH * 0.5f};
        pn->color = panelColor; pn->cornerRadius = 14.0f; pn->borderWidth = 1.0f;
        card->transform->SetParent(root->transform, false);
        m_objects.push_back(card);

        // Lay everything out top-down, centred about the screen centre.
        float y = -bodyH * 0.5f;
        AddLabel(*s, root, "Pause_Title", title, y, kTitleH, 3.6f, Color::FromBytes(245, 247, 255), true);
        y += kTitleH;

        if (nBtn > 0) {
            y += kSection;
            if (showResume)  { m_resume  = AddButton(*s, root, "Resume",    y, Color::FromBytes(60, 130, 90));  y += kBtnH + kBtnGap; }
            if (wantRestart) { m_restart = AddButton(*s, root, "Restart",   y, Color::FromBytes(70, 110, 150)); y += kBtnH + kBtnGap; }
            if (wantMenu)    { m_menu    = AddButton(*s, root, "Main Menu", y, Color::FromBytes(70, 90, 140));  y += kBtnH + kBtnGap; }
            if (showQuit)    { m_quit    = AddButton(*s, root, "Quit",      y, Color::FromBytes(150, 70, 70));  y += kBtnH + kBtnGap; }
            y -= kBtnGap;
        }

        if (showVolume) {
            y += kSection;
            AddLabel(*s, root, "Pause_VolLabel", "Volume", y, kVolLblH, 1.6f, Color::FromBytes(200, 205, 220), false);
            y += kVolLblH + kVolGap;
            GameObject* vol = s->CreateGameObject("Pause_Volume");
            m_volume = vol->AddComponent<UISlider>();
            m_volume->anchor = UIAnchor::Center;
            m_volume->size = {kBtnW, kVolBarH}; m_volume->position = {-kBtnW * 0.5f, y};
            m_volume->value = AudioMixer::masterVolume;
            m_volume->cornerRadius = 6.0f;
            vol->transform->SetParent(root->transform, false);
            m_objects.push_back(vol);
            y += kVolBarH;
        }

        if (!hint.empty()) {
            y += kSection;
            AddLabel(*s, root, "Pause_Hint", hint, y, kHintH, 1.4f, Color::FromBytes(170, 175, 190), false);
        }

        m_built = true;   // created visible (for editing); Start() hides it at Play
    }
};

} // namespace okay
