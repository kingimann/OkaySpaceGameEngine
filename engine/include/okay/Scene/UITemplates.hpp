#pragma once
// Ready-made, fully-editable UI screens you can drop into an existing scene (the
// editor's "UI > Create" menu). Each builder finds or creates the scene's Canvas +
// EventSystem, then adds a grouped subtree (returned) so you can move, edit, or save
// it as a reusable prefab/template as one unit. Nothing here clears the scene.
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Canvas.hpp"
#include "okay/Components/EventSystem.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIAnchor.hpp"
#include "okay/Render/Color.hpp"
#include <string>

namespace okay { namespace UITemplates {

// Find or create the scene's UI Canvas (+ an EventSystem), so a prebuilt screen drops
// into an existing scene without clobbering anything already there.
inline GameObject* EnsureCanvas(Scene& scene) {
    GameObject* canvas = nullptr;
    if (Canvas* existing = scene.FindObjectOfType<Canvas>()) canvas = existing->gameObject;
    else {
        canvas = scene.CreateGameObject("Canvas");
        canvas->AddComponent<Canvas>()->scaleMode = Canvas::ScaleMode::ScaleWithScreenSize;
    }
    if (!scene.FindObjectOfType<EventSystem>())
        scene.CreateGameObject("EventSystem")->AddComponent<EventSystem>();
    return canvas;
}

namespace detail {
    inline GameObject* Group(Scene& s, GameObject* parent, const char* name) {
        GameObject* g = s.CreateGameObject(name);
        g->transform->SetParent(parent->transform, false);   // no UI rect: children anchor to the canvas
        return g;
    }
    inline GameObject* Panel(Scene& s, GameObject* parent, const char* name, UIAnchor anchor,
                             Vec2 pos, Vec2 size, Color col, float corner = 12.0f, bool gradient = true) {
        GameObject* g = s.CreateGameObject(name);
        auto* p = g->AddComponent<UIPanel>();
        p->anchor = anchor; p->position = pos; p->size = size; p->color = col;
        p->cornerRadius = corner; p->borderWidth = 1.0f;
        if (gradient) { p->useGradient = true; p->colorBottom = Color(col.r * 0.6f, col.g * 0.6f, col.b * 0.7f, col.a); }
        g->transform->SetParent(parent->transform, false);
        return g;
    }
    inline GameObject* Text(Scene& s, GameObject* parent, const char* name, const std::string& t,
                            UIAnchor anchor, Vec2 pos, float px, Color col = Color::White) {
        GameObject* g = s.CreateGameObject(name);
        auto* tr = g->AddComponent<TextRenderer>();
        tr->text = t; tr->screenSpace = true; tr->anchor = anchor; tr->screenPos = pos;
        tr->pixelSize = px; tr->color = col; tr->outline = true;
        g->transform->SetParent(parent->transform, false);
        return g;
    }
    inline GameObject* Button(Scene& s, GameObject* parent, const char* name, const std::string& label,
                              UIAnchor anchor, Vec2 pos, Vec2 size, Color col = Color::FromBytes(58, 96, 170)) {
        GameObject* g = s.CreateGameObject(name);
        auto* b = g->AddComponent<UIButton>();
        b->label = label; b->anchor = anchor; b->position = pos; b->size = size;
        b->cornerRadius = 8.0f; b->fontScale = 2.5f; b->color = col;
        g->transform->SetParent(parent->transform, false);
        return g;
    }
}

// Centered main menu: title + Play / Options / Quit.
inline GameObject* AddMainMenu(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Panel(scene, canvas, "Main Menu", UIAnchor::Center, {-180, -210}, {360, 420},
                             Color::FromBytes(30, 36, 54, 240), 16.0f);
    Text(scene, root, "Title", "MY GAME", UIAnchor::TopLeft, {40, 34}, 5.0f);
    Button(scene, root, "PlayButton",    "Play",    UIAnchor::TopLeft, {40, 120}, {280, 56});
    Button(scene, root, "OptionsButton", "Options", UIAnchor::TopLeft, {40, 196}, {280, 56});
    Button(scene, root, "QuitButton",    "Quit",    UIAnchor::TopLeft, {40, 272}, {280, 56}, Color::FromBytes(150, 70, 80));
    return root;
}

// Full-screen dim + centered card: PAUSED + Resume / Settings / Quit.
inline GameObject* AddPauseMenu(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Group(scene, canvas, "Pause Menu");
    Panel(scene, root, "Dim", UIAnchor::TopLeft, {0, 0}, {1280, 720}, Color::FromBytes(0, 0, 0, 150), 0.0f, false);
    GameObject* card = Panel(scene, root, "Card", UIAnchor::Center, {-170, -170}, {340, 340},
                             Color::FromBytes(30, 36, 54, 245), 16.0f);
    Text(scene, card, "Title", "PAUSED", UIAnchor::TopLeft, {40, 30}, 4.0f);
    Button(scene, card, "ResumeButton",   "Resume",   UIAnchor::TopLeft, {40, 100}, {260, 52});
    Button(scene, card, "SettingsButton", "Settings", UIAnchor::TopLeft, {40, 168}, {260, 52});
    Button(scene, card, "QuitButton",     "Quit",     UIAnchor::TopLeft, {40, 236}, {260, 52}, Color::FromBytes(150, 70, 80));
    return root;
}

// Settings panel: Music / SFX sliders, Fullscreen / Vsync toggles, Quality dropdown, Back.
inline GameObject* AddSettings(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Panel(scene, canvas, "Settings", UIAnchor::Center, {-210, -230}, {420, 460},
                             Color::FromBytes(30, 36, 54, 242), 16.0f);
    Text(scene, root, "Title", "SETTINGS", UIAnchor::TopLeft, {40, 30}, 4.0f);
    Text(scene, root, "MusicLabel", "Music", UIAnchor::TopLeft, {40, 96}, 2.5f);
    { auto* g = scene.CreateGameObject("MusicSlider"); auto* sl = g->AddComponent<UISlider>();
      sl->anchor = UIAnchor::TopLeft; sl->position = {150, 96}; sl->size = {230, 18}; sl->value = 0.8f; sl->cornerRadius = 6.0f;
      g->transform->SetParent(root->transform, false); }
    Text(scene, root, "SfxLabel", "SFX", UIAnchor::TopLeft, {40, 138}, 2.5f);
    { auto* g = scene.CreateGameObject("SfxSlider"); auto* sl = g->AddComponent<UISlider>();
      sl->anchor = UIAnchor::TopLeft; sl->position = {150, 138}; sl->size = {230, 18}; sl->value = 0.7f; sl->cornerRadius = 6.0f;
      g->transform->SetParent(root->transform, false); }
    { auto* g = scene.CreateGameObject("FullscreenToggle"); auto* tg = g->AddComponent<UIToggle>();
      tg->anchor = UIAnchor::TopLeft; tg->position = {40, 186}; tg->size = {26, 26}; tg->label = "Fullscreen"; tg->cornerRadius = 5.0f;
      g->transform->SetParent(root->transform, false); }
    { auto* g = scene.CreateGameObject("VsyncToggle"); auto* tg = g->AddComponent<UIToggle>();
      tg->anchor = UIAnchor::TopLeft; tg->position = {40, 226}; tg->size = {26, 26}; tg->label = "VSync"; tg->cornerRadius = 5.0f; tg->on = true;
      g->transform->SetParent(root->transform, false); }
    Text(scene, root, "QualityLabel", "Quality", UIAnchor::TopLeft, {40, 276}, 2.5f);
    { auto* g = scene.CreateGameObject("QualityDropdown"); auto* dd = g->AddComponent<UIDropdown>();
      dd->anchor = UIAnchor::TopLeft; dd->position = {150, 272}; dd->size = {230, 32}; dd->options = {"Low", "Medium", "High"}; dd->value = 2;
      g->transform->SetParent(root->transform, false); }
    Button(scene, root, "BackButton", "Back", UIAnchor::TopLeft, {40, 386}, {340, 48});
    return root;
}

// HUD: health bar (top-left) + score (top-right). Grouped, each anchored to a corner.
inline GameObject* AddHUD(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Group(scene, canvas, "HUD");
    { auto* g = scene.CreateGameObject("HealthBar"); auto* pb = g->AddComponent<UIProgressBar>();
      pb->anchor = UIAnchor::TopLeft; pb->position = {24, 24}; pb->size = {260, 26}; pb->value = 1.0f; pb->cornerRadius = 6.0f;
      g->transform->SetParent(root->transform, false); }
    Text(scene, root, "HealthLabel", "HP", UIAnchor::TopLeft, {30, 27}, 2.0f);
    Text(scene, root, "Score", "Score: 0", UIAnchor::TopRight, {-30, 24}, 3.0f);
    return root;
}

// Bottom dialogue box: speaker name + body text + Next.
inline GameObject* AddDialogBox(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Panel(scene, canvas, "Dialog Box", UIAnchor::BottomCenter, {-460, -180}, {920, 160},
                             Color::FromBytes(18, 20, 30, 235), 12.0f);
    Text(scene, root, "Speaker", "Name", UIAnchor::TopLeft, {28, 18}, 2.8f, Color::FromBytes(120, 200, 255));
    Text(scene, root, "Body", "Type your dialogue here...", UIAnchor::TopLeft, {28, 60}, 2.4f);
    Button(scene, root, "NextButton", "Next", UIAnchor::TopLeft, {780, 100}, {110, 40});
    return root;
}

// A standalone labeled health bar (top-left), for quick HUDs.
inline GameObject* AddHealthBar(Scene& scene) {
    using namespace detail;
    GameObject* canvas = EnsureCanvas(scene);
    GameObject* root = Group(scene, canvas, "Health Bar");
    { auto* g = scene.CreateGameObject("Bar"); auto* pb = g->AddComponent<UIProgressBar>();
      pb->anchor = UIAnchor::TopLeft; pb->position = {24, 24}; pb->size = {300, 28}; pb->value = 1.0f; pb->cornerRadius = 8.0f;
      g->transform->SetParent(root->transform, false); }
    Text(scene, root, "Label", "HEALTH", UIAnchor::TopLeft, {30, 28}, 2.0f);
    return root;
}

} } // namespace okay::UITemplates
