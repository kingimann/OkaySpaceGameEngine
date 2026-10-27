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
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/CharacterController2D.hpp"
#include "okay/Components/CharacterController3D.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Animation/HumanoidLibrary.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/RootMotion.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/PlayerView.hpp"
#include "okay/Components/ThirdPersonShooterController.hpp"
#include "okay/Components/TopDownController.hpp"
#include "okay/Components/TopDownShooterController.hpp"
#include "okay/Components/Platformer25DController.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/TopDownController2D.hpp"
#include "okay/Components/ClickToMoveController.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Components/VehicleController2D.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/UITooltip.hpp"
#include "okay/Components/UIDraggable.hpp"
#include "okay/Components/CraftingStation.hpp"
#include "okay/Components/ChestInventory.hpp"
#include "okay/Components/InventoryUI.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/Canvas.hpp"
#include "okay/Components/EventSystem.hpp"
#include "okay/Components/Tilemap.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Components/TerrainDigger.hpp"
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Components/VoxelDigger.hpp"
#include "okay/Components/Water.hpp"
#include "okay/Components/PauseMenu.hpp"
#include "okay/Components/Flashlight.hpp"
#include "okay/Components/Light.hpp"

namespace okay {

/// Ready-made starter scenes so a new project isn't a blank canvas. Each builds
/// a small, playable layout wired with the engine's components — a good base to
/// learn from or extend. These touch only the public Scene/Component API, so the
/// editor and tests can both use them.
namespace Templates {

// ---- Premade players (added to an EXISTING scene) --------------------------
// These drop a ready-to-play player into the current scene (rather than building
// a whole new scene), so the editor can offer "Add > Player" shortcuts. Each
// returns the player GameObject so the caller can select it.

/// A standard physics body for a player: the blocky Character + Rigidbody3D + a
/// capsule-ish box collider wrapping feet->head.
inline GameObject* BuildPlayerBody(Scene& scene, const Vec3& pos, const char* name) {
    GameObject* player = scene.CreateGameObject(name);
    player->transform->localPosition = pos;
    player->AddComponent<Character>()->Apply();
    HumanoidLibrary::EnsureAnimator(player);
    player->AddComponent<Rigidbody3D>();
    auto* col = player->AddComponent<BoxCollider3D>();
    col->size = {0.6f, 1.8f, 0.6f};
    col->offset = {0.0f, 0.9f, 0.0f};
    return player;
}

/// Find an existing Camera in the scene, or create a "Main Camera" if there is none.
inline GameObject* EnsureMainCamera(Scene& scene) {
    for (const auto& go : scene.Objects())
        if (go->GetComponent<Camera>()) return go.get();
    GameObject* camObj = scene.CreateGameObject("Main Camera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Perspective;
    cam->main = true;
    camObj->transform->localPosition = {0, 3, 6};
    return camObj;
}

/// GTA-style view switch (V / gamepad Back) on a first- or third-person player.
/// Shoulder 0 and an inherited distance leave that controller's camera where it
/// was; the scene starters pass a shoulder so the orbit sits over the shoulder.
inline PlayerView* AddViewToggle(GameObject* player, PlayerView::View view, float shoulder = 0.0f) {
    auto* pv = player->AddComponent<PlayerView>();
    pv->allowToggle = true;
    pv->defaultView = view;
    pv->shoulderOffset = shoulder;
    pv->thirdPersonDistance = -1.0f;
    return pv;
}

/// Add a third-person player (orbit camera positioned by the controller).
inline GameObject* AddThirdPersonPlayer(Scene& scene, const Vec3& pos = {0, 1, 0}) {
    GameObject* player = BuildPlayerBody(scene, pos, "Player");
    player->AddComponent<ThirdPersonController>();
    AddViewToggle(player, PlayerView::View::Third);
    EnsureMainCamera(scene);
    return player;
}

/// Add a first-person player (camera mounted as a child at eye height).
inline GameObject* AddFirstPersonPlayer(Scene& scene, const Vec3& pos = {0, 1, 0}) {
    GameObject* player = BuildPlayerBody(scene, pos, "Player");
    player->AddComponent<FirstPersonController>();
    AddViewToggle(player, PlayerView::View::First);
    GameObject* camObj = scene.CreateGameObject("Player Camera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Perspective;
    cam->main = true;
    camObj->transform->SetParent(player->transform, false);
    camObj->transform->localPosition = {0, 1.62f, 0.0f};   // eye height
    return player;
}

/// Add a click-to-move player (point-and-click; controller drives a follow camera).
inline GameObject* AddClickToMovePlayer(Scene& scene, const Vec3& pos = {0, 1, 0}) {
    GameObject* player = BuildPlayerBody(scene, pos, "Player");
    player->AddComponent<ClickToMoveController>();
    EnsureMainCamera(scene);
    return player;
}

/// Add a top-down player (twin-stick / ARPG; fixed high angled follow camera).
inline GameObject* AddTopDownPlayer(Scene& scene, const Vec3& pos = {0, 1, 0}) {
    GameObject* player = BuildPlayerBody(scene, pos, "Player");
    player->AddComponent<TopDownController>();
    EnsureMainCamera(scene);
    return player;
}

/// Add a 3D top-down shooter player (twin-stick: WASD moves, the mouse aims, left
/// mouse fires, Space dashes) with a HealthStat so enemies can hurt it.
inline GameObject* AddTopDownShooterPlayer(Scene& scene, const Vec3& pos = {0, 1, 0}) {
    GameObject* player = BuildPlayerBody(scene, pos, "Player");
    player->AddComponent<TopDownShooterController>();
    if (!player->GetComponent<HealthStat>()) player->AddComponent<HealthStat>();
    EnsureMainCamera(scene);
    return player;
}

/// Add a 2.5D (LittleBigPlanet-style) platformer player: side view, run + jump,
/// W / S hop between depth layers, E grabs, Ctrl crouches, Z goes prone.
inline GameObject* AddPlatformer25DPlayer(Scene& scene, const Vec3& pos = {0, 1, 0}) {
    GameObject* player = BuildPlayerBody(scene, pos, "Player");
    auto* pc = player->AddComponent<Platformer25DController>();
    pc->frontLayerZ = pos.z;
    EnsureMainCamera(scene);
    return player;
}

/// Add a 2D top-down player: a sprite driven by the native TopDownController2D
/// (WASD/arrows, dash, optional shooting), on a gravity-free Rigidbody2D + box
/// collider, with an orthographic camera that follows it. The 2D counterpart to
/// AddTopDownPlayer — a ready-to-play twin-stick / ARPG player for 2D games.
inline GameObject* AddTopDown2DPlayer(Scene& scene, const Vec3& pos = {0, 0, 0}) {
    GameObject* player = scene.CreateGameObject("Player");
    player->transform->localPosition = pos;
    auto* sr = player->AddComponent<SpriteRenderer>();
    sr->color = Color::FromBytes(90, 170, 240);
    auto* rb = player->AddComponent<Rigidbody2D>();
    rb->bodyType = Rigidbody2D::BodyType::Dynamic;
    rb->gravityScale = 0.0f;                 // top-down: no falling
    player->AddComponent<BoxCollider2D>();
    player->AddComponent<TopDownController2D>();

    // An orthographic camera that follows the player (2D view).
    GameObject* camObj = scene.Find("MainCamera");
    if (!camObj) camObj = scene.CreateGameObject("MainCamera");
    auto* cam = camObj->GetComponent<Camera>();
    if (!cam) cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Orthographic;
    cam->orthographicSize = 6.0f;
    cam->main = true;
    if (!camObj->GetComponent<CameraFollow>()) {
        auto* follow = camObj->AddComponent<CameraFollow>();
        follow->targetName = "Player";
        follow->offset = {0, 0, 0};
        follow->smoothing = 6.0f;
    }
    return player;
}

/// Add a third-person shooter player (over-the-shoulder aim; cursor locked).
inline GameObject* AddThirdPersonShooterPlayer(Scene& scene, const Vec3& pos = {0, 1, 0}) {
    GameObject* player = BuildPlayerBody(scene, pos, "Player");
    player->AddComponent<ThirdPersonShooterController>();
    EnsureMainCamera(scene);
    return player;
}

/// Wire foot IK + root motion onto a part-rigged Character — the "humanoid setup"
/// a model import gives you for free. Foot IK plants the feet on the ground; root
/// motion is wired to the hips and left Disabled (the procedural anims bob the
/// hips, so it's only meaningful for imported clips with real root translation —
/// flip its mode to AnimDrivesMotion for those).
inline void WireCharacterIK(GameObject* player, Character* pc) {
    if (!pc) return;
    pc->separateParts = true;
    if (!pc->PartsBuilt()) pc->BuildParts();
    // Bone indices (see Character.cpp): hips=0; L thigh/shin/foot = 9/10/11; R = 12/13/14.
    auto T = [&](int b) -> Transform* { GameObject* g = pc->Part(b); return g ? g->transform : nullptr; };
    auto* fik = player->AddComponent<FootIK>();
    fik->leftHip  = T(9);  fik->leftKnee  = T(10); fik->leftFoot  = T(11);
    fik->rightHip = T(12); fik->rightKnee = T(13); fik->rightFoot = T(14);
    fik->pelvis = T(0);
    fik->useRaycast = true; fik->weight = 1.0f;
    // Full plant setup out of the box: sink the pelvis so a downhill foot reaches,
    // press feet onto lower ground, and tilt soles to the slope — this is what makes
    // foot IK VISIBLE on stairs and slopes (lift-only barely reads on flat ground).
    fik->adjustPelvis = true; fik->plantDown = true; fik->alignToGround = true;
    fik->orient = true;   // turn the thigh and shin parts with the joints, or they come apart
    fik->maxPelvisShift = 0.35f;
    fik->hipLimit = IKJointLimit::Hip(); fik->kneeLimit = IKJointLimit::Knee();
    auto* rm = player->AddComponent<RootMotion>();
    rm->rootNode = T(0);
    rm->mode = (int)RootMotion::Mode::Disabled;
}

/// Build a fully-wired humanoid body: the part-rigged Character, a physics capsule,
/// and foot IK + root motion already hooked to the right bones — "drop in a
/// humanoid" in one call. Add a controller + camera to drive it.
inline GameObject* BuildHumanoidBody(Scene& scene, const Vec3& pos, const char* name) {
    GameObject* player = scene.CreateGameObject(name);
    player->transform->localPosition = pos;
    auto* pc = player->AddComponent<Character>();
    pc->Apply();
    HumanoidLibrary::EnsureAnimator(player);
    player->AddComponent<Rigidbody3D>();
    auto* col = player->AddComponent<CapsuleCollider3D>();   // physics capsule, feet->head
    col->radius = 0.3f; col->height = 1.8f; col->offset = {0.0f, 0.9f, 0.0f};
    WireCharacterIK(player, pc);                             // foot IK + root motion
    return player;
}

/// Add a one-click humanoid third-person player: rig + capsule + camera + foot IK,
/// all wired. This is the "model.transform just works" row made real.
inline GameObject* AddHumanoidPlayer(Scene& scene, const Vec3& pos = {0, 1, 0}) {
    GameObject* player = BuildHumanoidBody(scene, pos, "Player");
    player->AddComponent<ThirdPersonController>();
    EnsureMainCamera(scene);
    return player;
}

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
    // Movement: left/right + jump (Space / W), no scripting needed. This was the
    // missing piece — the player had a body but nothing drove it.
    auto* cc = player->AddComponent<CharacterController2D>();
    cc->mode = CharacterController2D::Mode::Platformer;
    cc->speed = 5.0f;
    cc->jumpForce = 9.0f;

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

/// A 3D platformer starter: a lit ground, a physics player cube you drive with
/// WASD + Space (CharacterController3D + Rigidbody3D), and a perspective camera.
inline void Platformer3D(Scene& scene) {
    scene.Clear();
    scene.SetName("Platformer 3D");

    GameObject* camObj = scene.CreateGameObject("MainCamera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Perspective;
    cam->main = true;
    camObj->transform->localPosition = {0, 6, 12};
    camObj->transform->localRotation = Quat::Euler({-25, 0, 0});
    // Follow the player (keeps the same down-tilt; just trails its position).
    auto* follow = camObj->AddComponent<CameraFollow>();
    follow->targetName = "Player";
    follow->offset = {0, 6, 12};
    follow->smoothing = 6.0f;

    GameObject* light = scene.CreateGameObject("Directional Light");
    light->AddComponent<Light>();
    light->transform->localRotation = Quat::Euler({50, -30, 0});

    GameObject* ground = scene.CreateGameObject("Ground");
    ground->transform->localPosition = {0, -0.5f, 0};
    ground->transform->localScale = {24, 1, 24};
    auto* gmr = ground->AddComponent<MeshRenderer>();
    gmr->mesh = Mesh::Cube();        // solid block floor (visible ground == collider shape)
    gmr->color = Color::FromBytes(90, 110, 90);
    auto* gbc = ground->AddComponent<BoxCollider3D>();
    gbc->size = {1, 1, 1};   // unit cube; Transform scale (24) sizes the collider
    ground->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;

    // The blocky Character as the player (instead of a plain cube).
    GameObject* player = scene.CreateGameObject("Player");
    player->transform->localPosition = {0, 2, 0};
    player->AddComponent<Character>()->Apply();
    HumanoidLibrary::EnsureAnimator(player);
    player->AddComponent<Rigidbody3D>();
    {
        auto* col = player->AddComponent<BoxCollider3D>();
        col->size = {0.6f, 1.8f, 0.6f};
        col->offset = {0.0f, 0.9f, 0.0f};   // origin is at the feet — wrap feet->head
    }
    auto* cc = player->AddComponent<CharacterController3D>();
    cc->speed = 5.0f; cc->jumpForce = 7.0f;
}

/// A third-person starter: the blocky Character driven by a ThirdPersonController
/// (orbit camera behind, WASD relative to the camera, Space to jump, walk/run
/// animation), on a ground with a few crates. Lets you see and control your
/// character directly.
inline void ThirdPerson(Scene& scene) {
    scene.Clear();
    scene.SetName("Third Person");

    GameObject* light = scene.CreateGameObject("Directional Light");
    light->AddComponent<Light>();
    light->transform->localRotation = Quat::Euler({50, -30, 0});

    GameObject* ground = scene.CreateGameObject("Ground");
    ground->transform->localPosition = {0, -0.5f, 0};
    ground->transform->localScale = {40, 1, 40};
    auto* gmr = ground->AddComponent<MeshRenderer>();
    gmr->mesh = Mesh::Cube();        // solid block floor: the visible ground and the
                                     // collider are the same solid shape (no hanging box)
    gmr->color = Color::FromBytes(95, 110, 95);
    ground->AddComponent<BoxCollider3D>()->size = {1, 1, 1};   // unit cube; Transform scale sizes it
    ground->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;

    const Vec3 crates[4] = {{3, 0.5f, -3}, {-3, 0.5f, -4}, {3, 0.5f, 3}, {-3, 0.5f, 4}};
    for (int i = 0; i < 4; ++i) {
        GameObject* c = scene.CreateGameObject("Crate");
        c->transform->localPosition = crates[i];
        auto* mr = c->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube();
        mr->color = Color::FromBytes(150, 120, 80);
        c->AddComponent<BoxCollider3D>();
        c->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    }

    GameObject* player = scene.CreateGameObject("Player");
    player->transform->localPosition = {0, 1.0f, 0};
    player->AddComponent<Character>()->Apply();
    HumanoidLibrary::EnsureAnimator(player);
    player->AddComponent<Rigidbody3D>();
    {
        auto* col = player->AddComponent<BoxCollider3D>();
        col->size = {0.6f, 1.8f, 0.6f};
        col->offset = {0.0f, 0.9f, 0.0f};   // wrap the body feet->head
    }
    player->AddComponent<ThirdPersonController>();
    AddViewToggle(player, PlayerView::View::Third, 0.45f);   // V switches to the eyes

    // The controller positions this camera behind the player each frame.
    GameObject* camObj = scene.CreateGameObject("Main Camera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Perspective;
    cam->main = true;
    camObj->transform->localPosition = {0, 3, 6};
}

/// A "drop-in humanoid" starter: the same third-person scene, but the player is a
/// fully-wired humanoid (part rig + physics capsule + foot IK that plants the feet
/// on uneven ground + root motion hooked to the hips). Shows the one-call setup
/// AddHumanoidPlayer() gives you. Stepped ground so the foot IK is visible.
inline void Humanoid(Scene& scene) {
    scene.Clear();
    scene.SetName("Humanoid");

    GameObject* light = scene.CreateGameObject("Directional Light");
    light->AddComponent<Light>();
    light->transform->localRotation = Quat::Euler({50, -30, 0});

    GameObject* ground = scene.CreateGameObject("Ground");
    ground->transform->localPosition = {0, -0.5f, 0};
    ground->transform->localScale = {40, 1, 40};
    auto* gmr = ground->AddComponent<MeshRenderer>();
    gmr->mesh = Mesh::Cube();
    gmr->color = Color::FromBytes(95, 110, 95);
    ground->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    ground->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;

    // A couple of low steps so foot IK has uneven ground to plant on.
    const Vec3 steps[2] = {{2.0f, 0.1f, 0}, {3.0f, 0.25f, 0}};
    for (int i = 0; i < 2; ++i) {
        GameObject* s = scene.CreateGameObject("Step");
        s->transform->localPosition = steps[i];
        s->transform->localScale = {1.5f, 0.2f + 0.3f * i, 1.5f};
        auto* mr = s->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube();
        mr->color = Color::FromBytes(120, 120, 130);
        s->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
        s->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    }

    AddHumanoidPlayer(scene, {0, 1.0f, 0});   // rig + capsule + camera + foot IK, wired
}

/// A third-person SHOOTER starter: the blocky Character driven by a
/// ThirdPersonShooterController (over-the-shoulder aim, RMB aims, LMB fires), on a
/// ground with some cover crates and a row of shootable targets to aim at.
inline void ThirdPersonShooter(Scene& scene) {
    scene.Clear();
    scene.SetName("Third Person Shooter");

    GameObject* light = scene.CreateGameObject("Directional Light");
    light->AddComponent<Light>();
    light->transform->localRotation = Quat::Euler({50, -35, 0});

    GameObject* ground = scene.CreateGameObject("Ground");
    ground->transform->localPosition = {0, -0.5f, 0};
    ground->transform->localScale = {40, 1, 40};
    auto* gmr = ground->AddComponent<MeshRenderer>();
    gmr->mesh = Mesh::Cube();
    gmr->color = Color::FromBytes(88, 96, 104);
    ground->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    ground->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;

    // Cover crates to strafe around.
    const Vec3 crates[4] = {{4, 0.5f, -5}, {-4, 0.5f, -6}, {6, 0.5f, 2}, {-6, 0.5f, 3}};
    for (int i = 0; i < 4; ++i) {
        GameObject* c = scene.CreateGameObject("Crate");
        c->transform->localPosition = crates[i];
        auto* mr = c->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube();
        mr->color = Color::FromBytes(150, 120, 80);
        c->AddComponent<BoxCollider3D>();
        c->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    }

    // A row of red "targets" downrange to aim at.
    for (int i = 0; i < 5; ++i) {
        GameObject* t = scene.CreateGameObject("Target");
        t->transform->localPosition = {(float)(i - 2) * 2.5f, 1.0f, -12.0f};
        auto* mr = t->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Sphere();
        mr->color = Color::FromBytes(200, 70, 60);
        mr->emissive = Color::FromBytes(60, 10, 10);
        t->AddComponent<SphereCollider3D>();
    }

    GameObject* player = scene.CreateGameObject("Player");
    player->transform->localPosition = {0, 1.0f, 0};
    player->AddComponent<Character>()->Apply();
    HumanoidLibrary::EnsureAnimator(player);
    player->AddComponent<Rigidbody3D>();
    {
        auto* col = player->AddComponent<BoxCollider3D>();
        col->size = {0.6f, 1.8f, 0.6f};
        col->offset = {0.0f, 0.9f, 0.0f};
    }
    player->AddComponent<ThirdPersonShooterController>();

    GameObject* camObj = scene.CreateGameObject("Main Camera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Perspective;
    cam->main = true;
    camObj->transform->localPosition = {0, 2, 5};
}

/// A point-and-click starter (RuneScape / Diablo style): the blocky Character
/// driven by a ClickToMoveController — click the ground and it walks there — under
/// a high, angled camera, on a ground with a few crates.
inline void PointAndClick(Scene& scene) {
    scene.Clear();
    scene.SetName("Point & Click");

    GameObject* light = scene.CreateGameObject("Directional Light");
    light->AddComponent<Light>();
    light->transform->localRotation = Quat::Euler({55, -30, 0});

    GameObject* ground = scene.CreateGameObject("Ground");
    ground->transform->localPosition = {0, -0.5f, 0};
    ground->transform->localScale = {40, 1, 40};
    auto* gmr = ground->AddComponent<MeshRenderer>();
    gmr->mesh = Mesh::Cube();        // solid block floor: the visible ground and the
                                     // collider are the same solid shape (no hanging box)
    gmr->color = Color::FromBytes(95, 110, 95);
    ground->AddComponent<BoxCollider3D>()->size = {1, 1, 1};   // unit cube; Transform scale sizes it
    ground->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;

    const Vec3 crates[4] = {{4, 0.5f, -3}, {-4, 0.5f, -4}, {4, 0.5f, 4}, {-4, 0.5f, 3}};
    for (int i = 0; i < 4; ++i) {
        GameObject* c = scene.CreateGameObject("Crate");
        c->transform->localPosition = crates[i];
        auto* mr = c->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube();
        mr->color = Color::FromBytes(150, 120, 80);
        c->AddComponent<BoxCollider3D>();
        c->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    }

    GameObject* player = scene.CreateGameObject("Player");
    player->transform->localPosition = {0, 1.0f, 0};
    player->AddComponent<Character>()->Apply();
    HumanoidLibrary::EnsureAnimator(player);
    player->AddComponent<Rigidbody3D>();
    {
        auto* col = player->AddComponent<BoxCollider3D>();
        col->size = {0.6f, 1.8f, 0.6f};
        col->offset = {0.0f, 0.9f, 0.0f};
    }
    player->AddComponent<ClickToMoveController>();

    // A high, angled camera reads the click target well (isometric-ish).
    GameObject* camObj = scene.CreateGameObject("Main Camera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Perspective;
    cam->main = true;
    camObj->transform->localPosition = {0, 12, 10};
    camObj->transform->localRotation = Quat::Euler({50, 0, 0});
}

/// A first-person starter: a blocky Character player with a FirstPersonController
/// (mouse-look + WASD + jump) and a Camera mounted at eye height, on a ground
/// with a few crates to walk around. Press Play, then mouse + WASD.
inline void FPS(Scene& scene) {
    scene.Clear();
    scene.SetName("First Person");

    GameObject* light = scene.CreateGameObject("Directional Light");
    light->AddComponent<Light>();
    light->transform->localRotation = Quat::Euler({50, -30, 0});

    GameObject* ground = scene.CreateGameObject("Ground");
    ground->transform->localPosition = {0, -0.5f, 0};
    ground->transform->localScale = {40, 1, 40};
    auto* gmr = ground->AddComponent<MeshRenderer>();
    gmr->mesh = Mesh::Cube();        // solid block floor: the visible ground and the
                                     // collider are the same solid shape (no hanging box)
    gmr->color = Color::FromBytes(95, 110, 95);
    ground->AddComponent<BoxCollider3D>()->size = {1, 1, 1};   // unit cube; Transform scale sizes it
    ground->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;

    const Vec3 crates[5] = {{4, 0.5f, -2}, {-4, 0.5f, -3}, {2, 0.5f, -6}, {-2, 0.5f, -7}, {0, 0.5f, -10}};
    for (int i = 0; i < 5; ++i) {
        GameObject* c = scene.CreateGameObject("Crate");
        c->transform->localPosition = crates[i];
        auto* mr = c->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube();
        mr->color = Color::FromBytes(150, 120, 80);
        c->AddComponent<BoxCollider3D>();
        c->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    }

    // Player: the blocky Character + a first-person controller + physics body.
    GameObject* player = scene.CreateGameObject("Player");
    player->transform->localPosition = {0, 1.0f, 0};
    player->AddComponent<Character>()->Apply();
    HumanoidLibrary::EnsureAnimator(player);
    player->AddComponent<Rigidbody3D>();
    {
        auto* col = player->AddComponent<BoxCollider3D>();
        col->size = {0.6f, 1.8f, 0.6f};
        col->offset = {0.0f, 0.9f, 0.0f};   // the character's origin is at its feet,
                                            // so lift the box to wrap feet->head
    }
    player->AddComponent<FirstPersonController>();
    AddViewToggle(player, PlayerView::View::First, 0.45f);   // V switches to over the shoulder

    // Camera mounted on the player at eye height, just in front of the face so you
    // don't see the inside of your own head.
    GameObject* camObj = scene.CreateGameObject("FPS Camera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Perspective;
    cam->main = true;
    camObj->transform->SetParent(player->transform, false);
    camObj->transform->localPosition = {0, 1.62f, 0.0f};   // eye height (body hidden in first person)
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
    // Native, full-featured top-down movement (momentum + sprint + dash + facing) —
    // no script needed. Shift to sprint, Space to dash.
    auto* tdc = player->AddComponent<TopDownController2D>();
    tdc->speed = 5.0f; tdc->runSpeed = 8.0f;
    tdc->dashKey = ' ';             // Space to dash
    tdc->acceleration = 60.0f; tdc->deceleration = 55.0f;

    for (int i = 0; i < 2; ++i) {
        GameObject* wall = scene.CreateGameObject(i == 0 ? "WallA" : "WallB");
        wall->transform->localPosition = {i == 0 ? -4.0f : 4.0f, 0, 0};
        auto* wsr = wall->AddComponent<SpriteRenderer>();
        wsr->color = Color::FromBytes(110, 110, 130);
        wsr->size = {1, 4};
    }
}

namespace detail {
inline GameObject* Block(Scene& scene, const char* name, const Vec3& pos, const Vec3& scale, Color color,
                         Rigidbody3D::BodyType body = Rigidbody3D::BodyType::Static) {
    GameObject* o = scene.CreateGameObject(name);
    o->transform->localPosition = pos;
    o->transform->localScale = scale;
    auto* mr = o->AddComponent<MeshRenderer>();
    mr->mesh = Mesh::Cube(); mr->color = color;
    o->AddComponent<BoxCollider3D>()->size = {1, 1, 1};   // sized by the Transform scale
    o->AddComponent<Rigidbody3D>()->bodyType = body;
    return o;
}
} // namespace detail

/// A 3D top-down shooter starter (twin-stick): an arena with cover, shootable
/// target dummies that come back after a few seconds, a few crates the bullets
/// knock around, and a player you move with WASD, aim with the mouse, fire with
/// the left mouse and dash with Space.
inline void TopDownShooter(Scene& scene) {
    scene.Clear();
    scene.SetName("Top-Down Shooter");

    GameObject* light = scene.CreateGameObject("Directional Light");
    light->AddComponent<Light>();
    light->transform->localRotation = Quat::Euler({60, -30, 0});

    detail::Block(scene, "Ground", {0, -0.5f, 0}, {40, 1, 40}, Color::FromBytes(70, 76, 86));
    // Arena walls.
    const Color wall = Color::FromBytes(110, 116, 128);
    detail::Block(scene, "Wall N", {0, 1, -20}, {40, 3, 1}, wall);
    detail::Block(scene, "Wall S", {0, 1, 20}, {40, 3, 1}, wall);
    detail::Block(scene, "Wall E", {20, 1, 0}, {1, 3, 40}, wall);
    detail::Block(scene, "Wall W", {-20, 1, 0}, {1, 3, 40}, wall);
    // Cover.
    const Vec3 cover[] = {{-6, 0.6f, -5}, {6, 0.6f, -5}, {0, 0.6f, -10}, {-9, 0.6f, 6}, {9, 0.6f, 6}};
    for (const auto& c : cover) detail::Block(scene, "Cover", c, {3, 1.2f, 1}, Color::FromBytes(150, 120, 80));
    // Crates the bullets push around.
    for (int i = 0; i < 4; ++i)
        detail::Block(scene, "Crate", {-3.0f + (float)i * 2.0f, 0.4f, 4.0f}, {0.8f, 0.8f, 0.8f},
                      Color::FromBytes(180, 140, 90), Rigidbody3D::BodyType::Dynamic);
    // Target dummies: they take damage, disappear and come back.
    const Vec3 dummies[] = {{-8, 0.9f, -13}, {-3, 0.9f, -15}, {3, 0.9f, -15}, {8, 0.9f, -13}, {12, 0.9f, -2}, {-12, 0.9f, -2}};
    for (const auto& d : dummies) {
        GameObject* t = detail::Block(scene, "Target Dummy", d, {0.9f, 1.8f, 0.9f}, Color::FromBytes(205, 70, 60));
        t->GetComponent<MeshRenderer>()->emissive = Color::FromBytes(50, 8, 8);
        auto* hp = t->AddComponent<HealthStat>();
        hp->maxHealth = hp->health = 50.0f;
        hp->respawn = true; hp->respawnDelay = 3.0f;
    }

    AddTopDownShooterPlayer(scene, {0, 0.05f, 8});
    if (GameObject* cam = scene.Find("Main Camera"))
        if (auto* c = cam->GetComponent<Camera>()) { c->projection = Camera::Projection::Perspective; c->main = true; }
}

/// A 2.5D platformer starter in the LittleBigPlanet style: a side-on level with
/// three depth layers. Walk right with A / D, jump with Space, press W / S to hop
/// to the layer behind / in front (walls on one layer are passed on another),
/// hold E to drag the crate or hang from the bar.
inline void Platformer25D(Scene& scene) {
    scene.Clear();
    scene.SetName("2.5D Platformer");

    GameObject* light = scene.CreateGameObject("Directional Light");
    light->AddComponent<Light>();
    light->transform->localRotation = Quat::Euler({40, 155, 0});   // from in front, so the faces you see are lit

    const float sp = 1.5f;                       // layer spacing (matches the controller)
    auto lz = [&](int layer) { return -(float)layer * sp; };
    // Floor across all three layers, and a backdrop.
    detail::Block(scene, "Ground", {10, -0.5f, lz(1)}, {60, 1, sp * 3.0f}, Color::FromBytes(120, 150, 95));
    detail::Block(scene, "Backdrop", {10, 4, lz(2) - 1.5f}, {60, 10, 0.5f}, Color::FromBytes(150, 170, 200));
    const Color wood = Color::FromBytes(170, 125, 80), stone = Color::FromBytes(130, 130, 140);
    // A wall on the front layer only: hop back (W) to walk past it.
    detail::Block(scene, "Front Wall", {6, 1.25f, lz(0)}, {1, 2.5f, sp * 0.95f}, stone);
    // Steps up on the middle layer to a ledge.
    detail::Block(scene, "Step 1", {11, 0.4f, lz(1)}, {2, 0.8f, sp * 0.95f}, wood);
    detail::Block(scene, "Step 2", {13, 0.9f, lz(1)}, {2, 1.8f, sp * 0.95f}, wood);
    detail::Block(scene, "Ledge", {17, 1.4f, lz(1)}, {6, 0.4f, sp * 0.95f}, wood);
    // A back-layer platform reached by jumping and hopping back.
    detail::Block(scene, "Back Platform", {21, 2.6f, lz(2)}, {4, 0.4f, sp * 0.95f}, wood);
    // A pushable crate on the front layer (hold E to drag it).
    detail::Block(scene, "Crate", {3, 0.5f, lz(0)}, {1, 1, 1}, Color::FromBytes(200, 160, 90), Rigidbody3D::BodyType::Dynamic);
    // A bar to hang from (jump, then hold E).
    detail::Block(scene, "Hang Bar", {26, 3.4f, lz(0)}, {3, 0.25f, 0.25f}, stone);
    // A gap and a far platform.
    detail::Block(scene, "Far Platform", {32, 1.0f, lz(0)}, {6, 0.4f, sp * 0.95f}, wood);

    AddPlatformer25DPlayer(scene, {0, 0.05f, lz(0)});
    if (GameObject* cam = scene.Find("Main Camera"))
        if (auto* c = cam->GetComponent<Camera>()) { c->projection = Camera::Projection::Perspective; c->main = true; }
}

/// A 3D arcade driving starter: a car you steer with WASD/arrows on a wide ground,
/// the camera chasing from behind (VehicleController.followCamera), with a few blocks
/// to weave around. Space is the handbrake.
inline void Vehicle3D(Scene& scene) {
    scene.Clear();
    scene.SetName("Vehicle 3D");

    GameObject* light = scene.CreateGameObject("Directional Light");
    light->AddComponent<Light>();
    light->transform->localRotation = Quat::Euler({50, -30, 0});

    GameObject* ground = scene.CreateGameObject("Ground");
    ground->transform->localPosition = {0, -0.5f, 0};
    ground->transform->localScale = {120, 1, 120};
    auto* gmr = ground->AddComponent<MeshRenderer>();
    gmr->mesh = Mesh::Cube(); gmr->color = Color::FromBytes(80, 84, 92);
    ground->AddComponent<BoxCollider3D>()->size = {1, 1, 1};
    ground->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;

    const Vec3 blocks[] = {{8, 0.5f, -10}, {-9, 0.5f, -14}, {12, 0.5f, 6}, {-12, 0.5f, 10}, {0, 0.5f, -22}};
    for (const auto& b : blocks) {
        GameObject* o = scene.CreateGameObject("Block");
        o->transform->localPosition = b;
        o->transform->localScale = {2, 1, 2};
        auto* mr = o->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube(); mr->color = Color::FromBytes(150, 120, 80);
        o->AddComponent<BoxCollider3D>();
        o->AddComponent<Rigidbody3D>()->bodyType = Rigidbody3D::BodyType::Static;
    }

    GameObject* car = scene.CreateGameObject("Car");
    car->transform->localPosition = {0, 0.6f, 0};
    car->transform->localScale = {1.6f, 0.6f, 3.0f};   // a car-shaped block
    auto* cmr = car->AddComponent<MeshRenderer>();
    cmr->mesh = Mesh::Cube(); cmr->color = Color::FromBytes(205, 60, 55);
    car->AddComponent<Rigidbody3D>();
    car->AddComponent<BoxCollider3D>()->size = {1, 1, 1};   // sized by the Transform scale
    auto* vc = car->AddComponent<VehicleController>();
    vc->followCamera = true;

    GameObject* camObj = scene.CreateGameObject("Main Camera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Perspective;
    cam->main = true;
    camObj->transform->localPosition = {0, 4, -8};         // the controller keeps it behind the car
}

/// A 2D top-down driving starter: a car you steer with WASD/arrows (throttle + turn,
/// with grip so it corners and drifts), an orthographic camera following it, and a
/// few cones to weave around. Space is the handbrake.
inline void Vehicle2D(Scene& scene) {
    scene.Clear();
    scene.SetName("Vehicle 2D");

    GameObject* camObj = scene.CreateGameObject("Main Camera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Orthographic;
    cam->orthographicSize = 8.0f;
    cam->main = true;
    auto* follow = camObj->AddComponent<CameraFollow>();
    follow->targetName = "Car";
    follow->smoothing = 6.0f;

    const Vec2 cones[] = {{-6, 4}, {6, 4}, {-6, -4}, {6, -4}, {0, 7}, {0, -7}};
    for (const auto& c : cones) {
        GameObject* o = scene.CreateGameObject("Cone");
        o->transform->localPosition = {c.x, c.y, 0};
        auto* sr = o->AddComponent<SpriteRenderer>();
        sr->color = Color::FromBytes(230, 150, 60);
        sr->size = {0.6f, 0.6f};
    }

    GameObject* car = scene.CreateGameObject("Car");
    auto* sr = car->AddComponent<SpriteRenderer>();
    sr->color = Color::FromBytes(70, 140, 210);
    sr->size = {1.0f, 1.8f};                                // longer than wide = nose up (+Y)
    auto* rb = car->AddComponent<Rigidbody2D>();
    rb->gravityScale = 0.0f;                                // top-down: no gravity
    car->AddComponent<BoxCollider2D>()->size = {1.0f, 1.8f};
    car->AddComponent<VehicleController2D>();               // top-down (drives along +Y)
}

/// The simplest possible multiplayer: one player object whose script hosts (or
/// joins) a server and broadcasts its position, plus a HUD line of help. Drop
/// two copies of the built game on a LAN — one presses H to host, the other
/// presses J to join — and they see each other move. Shows how few lines the
/// net_* OkayScript API needs to get a session running, and lets players host
/// their own servers with no extra tooling.
inline void Multiplayer(Scene& scene) {
    scene.Clear();
    scene.SetName("Multiplayer");

    GameObject* camObj = scene.CreateGameObject("MainCamera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Orthographic;
    cam->orthographicSize = 6.0f;
    cam->main = true;

    GameObject* player = scene.CreateGameObject("Player");
    player->AddComponent<SpriteRenderer>()->color = Color::FromBytes(90, 170, 240);
    auto* sc = player->AddComponent<ScriptComponent>("okayscript");
    // Host with H, join 127.0.0.1 with J, then move with WASD. Every connected peer
    // appears automatically (the NetworkManager spawns a sprite per peer and keeps
    // it in sync), so two windows show each other move with zero extra wiring.
    sc->LoadSource(
        "# Multiplayer starter — H host, J join, WASD move.\n"
        "var started = 0;\n"
        "function start() { net_name(\"Player\"); }\n"
        "function update(d) {\n"
        "  if (started == 0) {\n"
        "    if (key_down(\"h\")) { net_host(45000); started = 1; }\n"
        "    if (key_down(\"j\")) { net_join(\"127.0.0.1\", 45000); started = 1; }\n"
        "  }\n"
        "  var speed = 5;\n"
        "  move(axis_x() * speed * d, axis_y() * speed * d);\n"
        "}\n");

    GameObject* help = scene.CreateGameObject("Help");
    auto* htr = help->AddComponent<TextRenderer>();
    htr->text = "H = host    J = join 127.0.0.1    WASD = move";
    htr->screenSpace = true; htr->screenPos = {12, 12}; htr->pixelSize = 2.0f;

    // Live status line driven by the net_* builtins — a ready-made connection HUD.
    GameObject* status = scene.CreateGameObject("NetStatus");
    auto* str = status->AddComponent<TextRenderer>();
    str->text = "Net: offline"; str->screenSpace = true; str->screenPos = {12, 40}; str->pixelSize = 2.0f;
    auto* ssc = status->AddComponent<ScriptComponent>("okayscript");
    ssc->LoadSource(
        "function update(d) {\n"
        "  if (net_connected() == 0) { set_text(\"Net: offline\"); }\n"
        "  else if (net_is_server() == 1) { set_text($\"Net: HOSTING   peers: {net_peers()}\"); }\n"
        "  else { set_text($\"Net: CLIENT   ping: {net_ping()} ms\"); }\n"
        "}\n");
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

    // UI root: a Canvas holding the widgets, plus an Event System.
    GameObject* canvas = scene.CreateGameObject("Canvas");
    auto* cv = canvas->AddComponent<Canvas>();
    cv->screenMatch = Canvas::ScreenMatch::Expand;
    cv->scaleMode = Canvas::ScaleMode::ScaleWithScreenSize;
    scene.CreateGameObject("EventSystem")->AddComponent<EventSystem>();

    // A styled card panel (rounded, subtle border, vertical gradient).
    GameObject* panel = scene.CreateGameObject("Panel");
    auto* pn = panel->AddComponent<UIPanel>();
    pn->position = {40, 40};
    pn->size = {360, 380};
    pn->color = Color::FromBytes(34, 40, 58, 235);
    pn->cornerRadius = 14.0f;
    pn->borderWidth = 1.0f;
    pn->useGradient = true;
    pn->colorBottom = Color::FromBytes(20, 24, 38, 235);
    panel->transform->SetParent(canvas->transform, false);

    GameObject* title = scene.CreateGameObject("Title");
    auto* tr = title->AddComponent<TextRenderer>();
    tr->text = "MY GAME";
    tr->screenSpace = true;
    tr->screenPos = {70, 70};
    tr->pixelSize = 5.0f;
    tr->outline = true;                       // reads on any background
    title->transform->SetParent(canvas->transform, false);

    GameObject* start = scene.CreateGameObject("StartButton");
    auto* b = start->AddComponent<UIButton>();
    b->label = "Start";
    b->position = {70, 150};
    b->size = {300, 56};
    b->cornerRadius = 10.0f;
    b->fontScale = 3.0f;
    start->AddComponent<ScriptComponent>("okayscript")->LoadSource(
        "function on_click() { load_scene(\"game.okayscene\"); }\n");
    start->AddComponent<UITooltip>()->text = "Begin the game";
    start->transform->SetParent(canvas->transform, false);

    // Volume slider with a live value readout, bound to a pref.
    GameObject* vol = scene.CreateGameObject("Volume");
    auto* sl = vol->AddComponent<UISlider>();
    sl->position = {70, 230}; sl->size = {300, 18};
    sl->value = 0.8f; sl->showValue = true; sl->cornerRadius = 6.0f;
    vol->AddComponent<ScriptComponent>("okayscript")->LoadSource(
        "function on_change() { prefs_set(\"volume\", ui_slider_value(\"Volume\")); }\n");
    vol->transform->SetParent(canvas->transform, false);

    // Fullscreen toggle.
    GameObject* fs = scene.CreateGameObject("Fullscreen");
    auto* tg = fs->AddComponent<UIToggle>();
    tg->position = {70, 270}; tg->size = {26, 26};
    tg->label = "Fullscreen"; tg->cornerRadius = 5.0f;
    fs->transform->SetParent(canvas->transform, false);

    // Quality dropdown.
    GameObject* q = scene.CreateGameObject("Quality");
    auto* dd = q->AddComponent<UIDropdown>();
    dd->position = {70, 310}; dd->size = {200, 30};
    dd->options = {"Low", "Medium", "High"}; dd->value = 2;
    q->transform->SetParent(canvas->transform, false);

    // Quit button.
    GameObject* quit = scene.CreateGameObject("QuitButton");
    auto* qb = quit->AddComponent<UIButton>();
    qb->label = "Quit"; qb->position = {70, 350}; qb->size = {300, 44};
    qb->cornerRadius = 10.0f;
    qb->color = Color::FromBytes(110, 60, 70);
    quit->transform->SetParent(canvas->transform, false);
}

/// A ready-made survival inventory on the real components: a 2D top-down Player
/// (tagged "Player") carrying an Inventory, the Minecraft-style hotbar + backpack
/// (E to open, 1-9 / wheel to pick), a loot chest (walk up, F) and a crafting bench
/// (walk up, C) that crafts from the player's bag. A starting point for survival,
/// RPG and crafting games — no scripting required.
inline void Inventory(Scene& scene) {
    scene.Clear();
    scene.SetName("Inventory");

    GameObject* camObj = scene.CreateGameObject("MainCamera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Orthographic;
    cam->main = true;
    cam->backgroundColor = Color::FromBytes(18, 20, 28);

    GameObject* player = AddTopDown2DPlayer(scene, {0, 0, 0});
    player->tag = "Player";
    auto* bag = player->AddComponent<okay::Inventory>();
    bag->capacity = 36;   // = the UI's 9 hotbar + 27 backpack slots
    bag->Add("Wood", 8); bag->Add("Stone", 4); bag->Add("Torch", 2);

    GameObject* canvas = scene.CreateGameObject("Canvas");
    auto* cv = canvas->AddComponent<Canvas>();
    cv->screenMatch = Canvas::ScreenMatch::Expand;
    cv->scaleMode = Canvas::ScaleMode::ScaleWithScreenSize;
    scene.CreateGameObject("EventSystem")->AddComponent<EventSystem>();
    GameObject* hud = scene.CreateGameObject("InventoryUI");
    hud->AddComponent<InventoryUI>()->inventoryTarget = "Player";
    hud->transform->SetParent(canvas->transform, false);

    GameObject* chest = scene.CreateGameObject("Chest");
    chest->transform->localPosition = {3.0f, 0.0f, 0.0f};
    auto* csr = chest->AddComponent<SpriteRenderer>();
    csr->color = Color::FromBytes(150, 100, 60);
    auto* loot = chest->AddComponent<okay::Inventory>();
    loot->capacity = 18;
    loot->Add("Apple", 5); loot->Add("Iron", 6); loot->Add("Sword", 1);
    chest->AddComponent<ChestInventory>()->title = "Chest";

    GameObject* bench = scene.CreateGameObject("Workbench");
    bench->transform->localPosition = {-3.0f, 0.0f, 0.0f};
    auto* bsr = bench->AddComponent<SpriteRenderer>();
    bsr->color = Color::FromBytes(120, 150, 90);
    auto* cs = bench->AddComponent<CraftingStation>();
    cs->title = "Workbench";
    cs->recipes.push_back({{{"Wood", 2}}, "Plank", 4});
    cs->recipes.push_back({{{"Wood", 1}, {"Stone", 1}}, "Torch", 2});
    cs->recipes.push_back({{{"Plank", 4}, {"Iron", 2}}, "Pickaxe", 1});

    GameObject* help = scene.CreateGameObject("Help");
    auto* ht = help->AddComponent<TextRenderer>();
    ht->text = "WASD to move. E: bag. Walk to the chest and press F, to the bench and press C.";
    ht->screenSpace = true;
    ht->screenPos = {20, 20}; ht->pixelSize = 1.5f;
    help->transform->SetParent(canvas->transform, false);
}

/// A complete, playable game of Snake — written entirely in OkayScript on a
/// single Tilemap object (snake body via an array, board via tile editing,
/// food via randi, score via prefs). Steer with WASD/arrows. A compact showcase
/// of arrays + tilemap scripting + input.
inline void Snake(Scene& scene) {
    scene.Clear();
    scene.SetName("Snake");

    const int W = 16, H = 16;
    GameObject* camObj = scene.CreateGameObject("MainCamera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Orthographic;
    cam->orthographicSize = H * 0.5f + 1.0f;
    cam->main = true;
    cam->backgroundColor = Color::FromBytes(15, 18, 24);
    camObj->transform->localPosition = {W * 0.5f, H * 0.5f, 0};

    GameObject* board = scene.CreateGameObject("Board");
    board->AddComponent<Tilemap>();
    board->AddComponent<ScriptComponent>("okayscript")->LoadSource(R"OKAY(
var W = 16; var H = 16;
var bx = []; var by = [];      # snake body cells (index 0 = tail, last = head)
var dx = 1; var dy = 0;
var fx = 0; var fy = 0;
var timer = 0; var step = 0.14;

function place_food() { fx = randi(0, W - 1); fy = randi(0, H - 1); }

function reset_game() {
    bx = []; by = [];
    push(bx, 7); push(by, 8);
    push(bx, 8); push(by, 8);   # head at (8,8) moving right
    dx = 1; dy = 0;
    prefs_set("score", 0);
    place_food();
}

function redraw() {
    for (var y = 0; y < H; y = y + 1) {
        for (var x = 0; x < W; x = x + 1) { set_tile(x, y, 0); }
    }
    for (var i = 0; i < count(bx); i = i + 1) { set_tile(bx[i], by[i], 1); }
    set_tile(fx, fy, 2);
}

function set_dir(nx, ny) {
    if (nx == 0 - dx && ny == 0 - dy) { return; }   # no instant reverse
    dx = nx; dy = ny;
}

function start() { tile_resize(W, H); reset_game(); redraw(); }

function update(d) {
    if (key("w")) { set_dir(0, 1); }
    if (key("s")) { set_dir(0, 0 - 1); }
    if (key("a")) { set_dir(0 - 1, 0); }
    if (key("d")) { set_dir(1, 0); }

    timer = timer + d;
    if (timer < step) { return; }
    timer = 0;

    var n = count(bx);
    var hx = bx[n - 1] + dx;
    var hy = by[n - 1] + dy;
    if (hx < 0 || hy < 0 || hx >= W || hy >= H) { reset_game(); redraw(); return; }
    for (var i = 0; i < count(bx); i = i + 1) {
        if (bx[i] == hx && by[i] == hy) { reset_game(); redraw(); return; }
    }
    push(bx, hx); push(by, hy);
    if (hx == fx && hy == fy) {
        prefs_set("score", prefs_get("score") + 1);
        place_food();
    } else {
        remove_at(bx, 0); remove_at(by, 0);
    }
    redraw();
}
)OKAY");

    GameObject* hud = scene.CreateGameObject("HUD");
    auto* tr = hud->AddComponent<TextRenderer>();
    tr->screenSpace = true; tr->screenPos = {12, 12}; tr->pixelSize = 3.0f;
    tr->text = "Score: 0";
    hud->AddComponent<ScriptComponent>("okayscript")->LoadSource(
        "function update(d) { set_text(\"Score: \" + prefs_get(\"score\")); }\n");
}

/// A terrain-digging sandbox: a procedurally generated, eroded hilly terrain you
/// can walk on (first-person) and reshape live in Play with a TerrainDigger — hold
/// the mouse to carve craters, with a ring marker showing where you're digging.
/// A ready base for survival / mining / building games.
inline void TerrainSandbox(Scene& scene) {
    scene.Clear();
    scene.SetName("Terrain Sandbox");

    GameObject* light = scene.CreateGameObject("Directional Light");
    light->AddComponent<Light>();
    light->transform->localRotation = Quat::Euler({50, -30, 0});

    // The terrain: a generated hilly landscape, lightly eroded so it reads natural.
    GameObject* ground = scene.CreateGameObject("Terrain");
    auto* terr = ground->AddComponent<Terrain>();
    terr->resolution = 64;
    terr->Resize(64);
    terr->size = 100.0f;
    terr->Generate(1, 10.0f, 3.0f, 5, 2024u);   // rolling hills
    terr->Erode(20000, 0.35f, 7u);              // carve a few valleys
    terr->autoColor = true;
    terr->Apply();

    // The digger lives on the terrain object: hold Left Mouse to dig, with a ring
    // marker showing the brush. (It finds the main camera for the aim ray.)
    auto* dig = ground->AddComponent<TerrainDigger>();
    dig->mode = TerrainDigger::Mode::Dig;
    dig->radius = 4.0f;
    dig->strength = 12.0f;
    dig->showBrush = true;

    // A first-person player that walks on the terrain (Physics3D grounds it on the
    // heightmap) and aims the digger with the camera.
    GameObject* player = scene.CreateGameObject("Player");
    player->transform->localPosition = {0, 12, 0};   // drop in from above; it settles
    player->AddComponent<Character>()->Apply();
    HumanoidLibrary::EnsureAnimator(player);
    player->AddComponent<Rigidbody3D>();
    {
        auto* col = player->AddComponent<BoxCollider3D>();
        col->size = {0.6f, 1.8f, 0.6f};
        col->offset = {0.0f, 0.9f, 0.0f};
    }
    player->AddComponent<FirstPersonController>();

    GameObject* camObj = scene.CreateGameObject("FPS Camera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Perspective;
    cam->main = true;
    camObj->transform->SetParent(player->transform, false);
    camObj->transform->localPosition = {0, 1.62f, 0.0f};   // eye height

    // A lake: animated reflective water sitting in the low ground.
    GameObject* lake = scene.CreateGameObject("Water");
    lake->transform->localPosition = {0, 3.0f, 0};
    auto* water = lake->AddComponent<Water>();
    water->size = 110.0f;
    water->Apply();

    GameObject* help = scene.CreateGameObject("Help");
    auto* ht = help->AddComponent<TextRenderer>();
    ht->text = "WASD + mouse to move    Left Mouse dig / Right Mouse raise    Space jump";
    ht->screenSpace = true; ht->screenPos = {12, 12}; ht->pixelSize = 2.0f;

    scene.CreateGameObject("Pause Menu")->AddComponent<PauseMenu>();   // Esc to pause / quit
}

/// The one unified sandbox: a single smooth voxel terrain you walk on AND dig real
/// caves/tunnels/overhangs into (no separate heightmap terrain), with a lake of
/// animated water, a first-person player with full voxel collision (you stand on
/// the ground and in caves, not fall through), and a pause menu. Left mouse digs,
/// right mouse fills, Space jumps, Esc pauses.
inline void VoxelSandbox(Scene& scene) {
    scene.Clear();
    scene.SetName("Sandbox");

    GameObject* light = scene.CreateGameObject("Directional Light");
    light->AddComponent<Light>();
    light->transform->localRotation = Quat::Euler({50, -30, 0});

    // Atmospheric distance fog so the far edges of a big voxel world melt into
    // the horizon instead of popping at the camera's far clip plane. Now honoured
    // by all four renderers (software, GL, D3D11, D3D12).
    scene.renderSettings.fog      = true;
    scene.renderSettings.fogColor = scene.renderSettings.skyHorizon;
    scene.renderSettings.fogStart = 120.0f;
    scene.renderSettings.fogEnd   = 520.0f;

    // The single terrain: a voxel field with rolling hills and a cave network.
    GameObject* ground = scene.CreateGameObject("Terrain");
    auto* vox = ground->AddComponent<VoxelTerrain>();
    vox->Resize(72, 44, 72);
    vox->voxelSize = 1.5f;
    vox->Generate(0.55f, 9.0f, 0.0f, 2024u);   // solid hills — NO pre-cut caves; dig your own
    vox->Apply();
    auto* dig = ground->AddComponent<VoxelDigger>();
    dig->radius = 3.0f; dig->strength = 10.0f;   // left = dig, right = add (defaults)

    // A lake in the low ground. 0.32 * SizeY is below every hill this Generate
    // builds (surface = SizeY * 0.55 ± amplitude), so the water used to sit
    // inside solid terrain and never showed. Use the real surface instead:
    // low enough that valleys flood, high enough that the peaks stay dry.
    float minSurf = 1.0e9f, maxSurf = -1.0e9f;
    const float sample = 4.0f;
    for (float z = -vox->HalfZ(); z <= vox->HalfZ(); z += sample)
        for (float x = -vox->HalfX(); x <= vox->HalfX(); x += sample) {
            float y = 0.0f;
            if (!vox->SurfaceY(x, z, y)) continue;
            if (y < minSurf) minSurf = y;
            if (y > maxSurf) maxSurf = y;
        }
    const float waterY = (minSurf < maxSurf) ? minSurf + (maxSurf - minSurf) * 0.22f
                                              : vox->SizeY() * 0.5f;
    GameObject* lake = scene.CreateGameObject("Water");
    lake->transform->localPosition = {0, waterY, 0};
    auto* water = lake->AddComponent<Water>();
    water->size = vox->HalfX() * 2.4f;
    water->Apply();

    // First-person player. Spawned above a solid column so it drops onto the
    // surface (voxel collision in Physics3D catches it).
    GameObject* player = scene.CreateGameObject("Player");
    float surfY = 0.0f; vox->SurfaceY(0, 0, surfY);
    player->transform->localPosition = {0, surfY + 3.0f, 0};
    { auto* pc = player->AddComponent<Character>(); pc->Apply(); pc->separateParts = true; }   // part rig: the FP hand raises your real arm and hides the rest of the body from your own camera only
    HumanoidLibrary::EnsureAnimator(player);
    player->AddComponent<Rigidbody3D>();
    {
        auto* col = player->AddComponent<BoxCollider3D>();
        col->size = {0.6f, 1.8f, 0.6f};
        col->offset = {0.0f, 0.9f, 0.0f};
    }
    player->AddComponent<FirstPersonController>();

    GameObject* camObj = scene.CreateGameObject("FPS Camera");
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Perspective;
    cam->main = true;
    camObj->transform->SetParent(player->transform, false);
    camObj->transform->localPosition = {0, 1.62f, 0.0f};
    camObj->AddComponent<Flashlight>();   // F to toggle — so you can see inside caves
    camObj->AddComponent<FirstPersonHand>();   // Minecraft-style arm + punch on LMB

    GameObject* help = scene.CreateGameObject("Help");
    auto* ht = help->AddComponent<TextRenderer>();
    ht->text = "WASD + mouse   Dig: LMB   Fill: RMB   Jump: Space   Light: F   Pause: Esc";
    ht->screenSpace = true; ht->screenPos = {12, 12}; ht->pixelSize = 2.0f;

    scene.CreateGameObject("Pause Menu")->AddComponent<PauseMenu>();
}

} // namespace Templates

} // namespace okay
