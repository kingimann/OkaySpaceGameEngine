#pragma once
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/Spinner.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"

namespace okay {

/// Ready-made starter scenes so a new project isn't a blank canvas. Each builds
/// a small, playable layout wired with the engine's components — a good base to
/// learn from or extend. These touch only the public Scene/Component API, so the
/// editor and tests can both use them.
namespace Templates {

/// A side-scrolling platformer starter: a follow camera, a physics player on a
/// wide ground, and a spinning coin pickup.
inline void Platformer(Scene& scene) {
    scene.Clear();
    scene.SetName("Platformer");

    GameObject* camObj = scene.CreateGameObject("MainCamera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Orthographic;
    cam->orthographicSize = 6.0f;
    cam->main = true;
    auto* follow = camObj->AddComponent<CameraFollow>();
    follow->targetName = "Player";
    follow->offset = {0, 1, 0};
    follow->smoothing = 5.0f;

    GameObject* player = scene.CreateGameObject("Player");
    player->transform->localPosition = {0, 1, 0};
    auto* psr = player->AddComponent<SpriteRenderer>();
    psr->color = Color::FromBytes(80, 160, 240);
    auto* rb = player->AddComponent<Rigidbody2D>();
    rb->bodyType = Rigidbody2D::BodyType::Dynamic;
    player->AddComponent<BoxCollider2D>();

    GameObject* ground = scene.CreateGameObject("Ground");
    ground->transform->localPosition = {0, -3, 0};
    auto* gsr = ground->AddComponent<SpriteRenderer>();
    gsr->color = Color::FromBytes(90, 110, 90);
    gsr->size = {20, 1};
    auto* gbc = ground->AddComponent<BoxCollider2D>();
    gbc->size = {20, 1};
    auto* grb = ground->AddComponent<Rigidbody2D>();
    grb->bodyType = Rigidbody2D::BodyType::Static;

    GameObject* coin = scene.CreateGameObject("Coin");
    coin->transform->localPosition = {4, 0, 0};
    auto* csr = coin->AddComponent<SpriteRenderer>();
    csr->color = Color::FromBytes(240, 210, 70);
    csr->size = {0.6f, 0.6f};
    coin->AddComponent<Spinner>()->angularVelocity = {0, 0, 120};
}

/// A top-down starter: a follow camera and a script-driven player that walks
/// with WASD, plus a couple of walls.
inline void TopDown(Scene& scene) {
    scene.Clear();
    scene.SetName("Top-Down");

    GameObject* camObj = scene.CreateGameObject("MainCamera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Orthographic;
    cam->orthographicSize = 6.0f;
    cam->main = true;
    auto* follow = camObj->AddComponent<CameraFollow>();
    follow->targetName = "Player";
    follow->smoothing = 8.0f;

    GameObject* player = scene.CreateGameObject("Player");
    auto* psr = player->AddComponent<SpriteRenderer>();
    psr->color = Color::FromBytes(230, 120, 90);
    auto* sc = player->AddComponent<ScriptComponent>("okayscript");
    sc->LoadSource(
        "function update(d) {\n"
        "  var speed = 5;\n"
        "  move(axis_x() * speed * d, axis_y() * speed * d);\n"
        "}\n");

    for (int i = 0; i < 2; ++i) {
        GameObject* wall = scene.CreateGameObject(i == 0 ? "WallA" : "WallB");
        wall->transform->localPosition = {i == 0 ? -4.0f : 4.0f, 0, 0};
        auto* wsr = wall->AddComponent<SpriteRenderer>();
        wsr->color = Color::FromBytes(110, 110, 130);
        wsr->size = {1, 4};
    }
}

/// A complete little game: drive the player with WASD to collect spinning
/// coins; a HUD counts the score. Shows off sprites, triggers, script events,
/// shared state (prefs), camera-follow, and text — a working sample to learn
/// from. Uses only OkayScript + components, so it runs in the player too.
inline void CoinCollector(Scene& scene) {
    scene.Clear();
    scene.SetName("Coin Collector");

    GameObject* camObj = scene.CreateGameObject("MainCamera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Orthographic;
    cam->orthographicSize = 6.0f;
    cam->main = true;
    auto* follow = camObj->AddComponent<CameraFollow>();
    follow->targetName = "Player";
    follow->smoothing = 6.0f;

    GameObject* player = scene.CreateGameObject("Player");
    player->AddComponent<SpriteRenderer>()->color = Color::FromBytes(80, 160, 240);
    player->AddComponent<BoxCollider2D>()->size = {1, 1};
    auto* psc = player->AddComponent<ScriptComponent>("okayscript");
    psc->LoadSource(
        "function start() { prefs_set(\"score\", 0); }\n"
        "function update(d) {\n"
        "  var speed = 6;\n"
        "  move(axis_x() * speed * d, axis_y() * speed * d);\n"
        "}\n");

    GameObject* hud = scene.CreateGameObject("HUD");
    auto* txt = hud->AddComponent<TextRenderer>();
    txt->screenSpace = true;
    txt->screenPos = {16, 16};
    txt->pixelSize = 3.0f;
    txt->text = "Score: 0";
    hud->AddComponent<ScriptComponent>("okayscript")->LoadSource(
        "function update(d) { set_text(\"Score: \" + prefs_get(\"score\")); }\n");

    // A few collectible coins.
    const float xs[] = {-3.0f, 0.0f, 3.0f};
    for (int i = 0; i < 3; ++i) {
        GameObject* coin = scene.CreateGameObject("Coin");
        coin->transform->localPosition = {xs[i], 2.0f, 0.0f};
        auto* sr = coin->AddComponent<SpriteRenderer>();
        sr->color = Color::FromBytes(240, 210, 70);
        sr->size = {0.6f, 0.6f};
        auto* cc = coin->AddComponent<BoxCollider2D>();
        cc->size = {0.6f, 0.6f};
        cc->isTrigger = true;
        coin->AddComponent<Spinner>()->angularVelocity = {0, 0, 120};
        coin->AddComponent<ScriptComponent>("okayscript")->LoadSource(
            "function on_trigger() { prefs_set(\"score\", prefs_get(\"score\") + 1); destroy(); }\n");
    }
}

/// A main-menu scene: a background panel, a title, and a Start button that loads
/// the game scene on click. Shows how UI + scripting + scene-loading fit
/// together. (The Start button loads "game.okayscene" — build your game to that
/// name, or change the path.)
inline void MainMenu(Scene& scene) {
    scene.Clear();
    scene.SetName("Main Menu");

    GameObject* camObj = scene.CreateGameObject("MainCamera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Orthographic;
    cam->main = true;
    cam->backgroundColor = Color::FromBytes(18, 20, 28);

    GameObject* panel = scene.CreateGameObject("Panel");
    auto* pn = panel->AddComponent<UIPanel>();
    pn->position = {40, 40};
    pn->size = {360, 240};
    pn->color = Color::FromBytes(30, 36, 52, 220);

    GameObject* title = scene.CreateGameObject("Title");
    auto* tr = title->AddComponent<TextRenderer>();
    tr->text = "MY GAME";
    tr->screenSpace = true;
    tr->screenPos = {70, 70};
    tr->pixelSize = 5.0f;

    GameObject* start = scene.CreateGameObject("StartButton");
    auto* b = start->AddComponent<UIButton>();
    b->label = "Start";
    b->position = {70, 170};
    b->size = {300, 60};
    start->AddComponent<ScriptComponent>("okayscript")->LoadSource(
        "function on_click() { load_scene(\"game.okayscene\"); }\n");
}

} // namespace Templates

} // namespace okay
