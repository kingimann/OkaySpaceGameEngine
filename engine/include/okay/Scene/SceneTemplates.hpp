#pragma once
#include "okay/Scene/Scene.hpp"
#include <cmath>
#include <string>
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/Spinner.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UITextBind.hpp"
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
#include "okay/Components/PlayerController.hpp"
#include "okay/Components/FirstPersonHand.hpp"
#include "okay/Components/ThirdPersonShooterController.hpp"
#include "okay/Components/TopDownController.hpp"
#include "okay/Components/TopDownShooterController.hpp"
#include "okay/Components/Platformer25DController.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/TopDownController2D.hpp"
#include "okay/Components/ClickToMoveController.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Components/VehicleController2D.hpp"
#include "okay/Components/VehicleSeat.hpp"
#include "okay/Scene/VehicleBuilder.hpp"
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
#include "okay/Components/Buoyancy.hpp"
#include "okay/Components/BoatController.hpp"
#include "okay/Components/PauseMenu.hpp"
#include "okay/Components/Flashlight.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/HoldableObject.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "okay/Components/FirstPersonHands.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Components/RangedWeapon.hpp"
#include "okay/Components/HoldPrompt.hpp"

namespace okay {

/// Ready-made starter scenes so a new project isn't a blank canvas. Each builds
/// a small, playable layout wired with the engine's components — a good base to
/// learn from or extend. These touch only the public Scene/Component API, so the
/// editor and tests can both use them.
namespace Templates {

// The editor paints a PLAYING pill over the top-left of the Game view (about
// 32px). Top-left HUD text starts below it, and is top-aligned: a wrapped line
// on a narrow view is taller than its box, and vertical centering pulls the
// first line back up under the pill.
constexpr float kHudTop = 48.0f;

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
    pv->blendTime = 0.30f;
    pv->switchEase = PlayerView::SwitchEase::Smooth;
    pv->snapSwitch = false;
    return pv;
}

/// Hands, a one-handed block and a two-handed crate on a switchable player
/// (the First Person and Third Person starters both toggle with V). E picks a
/// prop up into the hands; E again or G drops it. The line at the bottom of the
/// screen is the Hold Prompt text.
void AddTemplateHoldables(Scene& scene, GameObject* player);

/// One Player Controller with both views (the V toggle), and First Person
/// Hands. First person is the eye controller either way; `start` is only which
/// view a fresh play begins in. The legacy single hand is not mounted.
inline PlayerView* AddSharedView(GameObject* player, PlayerView::View start, float shoulder = 0.0f) {
    auto* pc = PlayerController::Of(player);
    if (!pc) pc = PlayerController::Add(player, PlayerController::ViewMode::Both, start);
    if (!player->GetComponent<FirstPersonHands>()) player->AddComponent<FirstPersonHands>();
    PlayerView* pv = pc->toggle();
    if (pv) { pv->defaultView = start; pv->shoulderOffset = shoulder; }
    return pv;
}

/// The eye camera both views share. The orbit repositions this same camera
/// while the view is third person. It starts at the standing eye, just in
/// front of the face, looking level. No legacy hand.
inline GameObject* MountEyeCamera(Scene& scene, GameObject* player, const char* name) {
    GameObject* camObj = scene.CreateGameObject(name);
    auto* cam = camObj->AddComponent<Camera>();
    cam->projection = Camera::Projection::Perspective;
    cam->main = true;
    camObj->transform->SetParent(player->transform, false);
    camObj->transform->localPosition = {0.0f, 1.6f, -0.20f};
    camObj->transform->localRotation = Quat::Identity;
    return camObj;
}

/// Resting over-the-shoulder orbit in the player's space (pitch 18 looks
/// slightly down). The controller writes the same pose on the first frame.
inline void PoseRestingOrbit(GameObject* camObj) {
    const float rest = 18.0f * 0.0174532925f;
    camObj->transform->localPosition = {0.45f, 1.5f + std::sin(rest) * 5.0f, std::cos(rest) * 5.0f};
    camObj->transform->localRotation = Quat::Euler(-18.0f, 0.0f, 0.0f);
}

/// Add a third-person player. Same controllers and eye camera as first person;
/// play starts on the orbit. V switches to the eyes.
inline GameObject* AddThirdPersonPlayer(Scene& scene, const Vec3& pos = {0, 1, 0}) {
    GameObject* player = BuildPlayerBody(scene, pos, "Player");
    AddSharedView(player, PlayerView::View::Third);
    PoseRestingOrbit(MountEyeCamera(scene, player, "Main Camera"));
    return player;
}

/// Add a first-person player. Same controllers and eye camera as third person;
/// play starts at the eyes. V switches to the orbit.
inline GameObject* AddFirstPersonPlayer(Scene& scene, const Vec3& pos = {0, 1, 0}) {
    GameObject* player = BuildPlayerBody(scene, pos, "Player");
    AddSharedView(player, PlayerView::View::First);
    MountEyeCamera(scene, player, "Player Camera");
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
GameObject* AddTopDown2DPlayer(Scene& scene, const Vec3& pos = {0, 0, 0});

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
void WireCharacterIK(GameObject* player, Character* pc);

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
    PlayerController::Add(player, PlayerController::ViewMode::Third);
    EnsureMainCamera(scene);
    return player;
}

/// A side-scrolling platformer starter: a follow camera, a physics player on a
/// wide ground, and a spinning coin pickup.
void Platformer(Scene& scene);

/// A 3D platformer starter: a lit ground, a physics player cube you drive with
/// WASD + Space (CharacterController3D + Rigidbody3D), and a perspective camera.
void Platformer3D(Scene& scene);

/// A third-person starter: the blocky Character driven by a ThirdPersonController
/// (orbit camera behind, WASD relative to the camera, Space to jump, walk/run
/// animation), on a ground with a few crates. Lets you see and control your
/// character directly.
void ThirdPerson(Scene& scene);

/// A third-person SHOOTER starter: the blocky Character driven by a
/// ThirdPersonShooterController (over-the-shoulder aim, RMB aims, LMB fires), on a
/// ground with some cover crates and a row of shootable targets to aim at.
void ThirdPersonShooter(Scene& scene);

/// A point-and-click starter (RuneScape / Diablo style): the blocky Character
/// driven by a ClickToMoveController — click the ground and it walks there — under
/// a high, angled camera, on a ground with a few crates.
void PointAndClick(Scene& scene);

/// A first-person starter: a blocky Character player with a FirstPersonController
/// (mouse-look + WASD + jump) and a Camera mounted at eye height, on a ground
/// with a few crates to walk around. Press Play, then mouse + WASD.
void FPS(Scene& scene);

/// A top-down starter: a follow camera and a script-driven player that walks
/// with WASD, plus a couple of walls.
void TopDown(Scene& scene);

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
void TopDownShooter(Scene& scene);

/// A 2.5D platformer starter in the LittleBigPlanet style: a side-on level with
/// three depth layers. Walk right with A / D, jump with Space, press W / S to hop
/// to the layer behind / in front (walls on one layer are passed on another),
/// hold E to drag the crate or hang from the bar.
void Platformer25D(Scene& scene);

/// A 3D arcade driving starter: a car you steer with WASD/arrows on a wide ground,
/// the camera chasing from behind (VehicleController.followCamera), with a few blocks
/// to weave around. Space is the handbrake.
void Vehicle3D(Scene& scene);

/// "Vehicle Test": a third-person player standing in the road next to a small car (about 4.1 m long,
/// 1.8 m wide) that has a driver seat and a passenger seat. Walk to the car's driver door (the -X side), press E to get in: you sit
/// (the chair sit clip), the camera swings behind the car, drive with WASD / arrows (Space = handbrake),
/// press E to get out. The car only drives while someone sits in the driver seat.
/// The car faces +Z. Its wheels roll with the speed and the front two turn (VehicleController reads
/// the objects named "Wheel ..." with a child "Tire"). A road with lane lines and a slalom of cones
/// and crates lies ahead. examples/vehicle/vehicle_test.okayscene is this scene saved to a file
/// (regenerate it with SceneSerializer after changing this).
void VehicleSeats3D(Scene& scene);

/// "Driving World": a flat city-like grid of roads with ramps, a mud patch and an ice patch, a drift circle of cones, a
/// row of parked vehicles of every type, and a walking player. Walk to a vehicle (WASD), press E to get in, drive with
/// WASD / Space (handbrake) / Shift (boost), C changes the camera, E again to get out.
/// examples/driving/driving_world.okayscene is this scene saved to a file (see tests/test_vehicle_drive_scene.cpp).
void DrivingWorld(Scene& scene);

/// A 2D top-down driving starter: a car you steer with WASD/arrows (throttle + turn,
/// with grip so it corners and drifts), an orthographic camera following it, and a
/// few cones to weave around. Space is the handbrake.
void Vehicle2D(Scene& scene);

/// The simplest possible multiplayer: one player object whose script hosts (or
/// joins) a server and broadcasts its position, plus a HUD line of help. Drop
/// two copies of the built game on a LAN — one presses H to host, the other
/// presses J to join — and they see each other move. Shows how few lines the
/// net_* OkayScript API needs to get a session running, and lets players host
/// their own servers with no extra tooling.
void Multiplayer(Scene& scene);

/// A complete little game: drive the player with WASD to collect spinning
/// coins; a HUD counts the score. Shows off sprites, triggers, script events,
/// shared state (prefs), camera-follow, and text — a working sample to learn
/// from. Uses only OkayScript + components, so it runs in the player too.
void CoinCollector(Scene& scene);

/// A main-menu scene: a background panel, a title, a Start button, a volume slider,
/// a fullscreen toggle, a quality dropdown and a Quit button. Start hides the menu
/// and shows the "Game" object (a stand-in with a Back button), so the menu works the
/// moment the project is made: the button never points at a scene that is not there.
/// To load a real level instead, change the Start button's script to
/// load_scene("<path of your scene file>").
void MainMenu(Scene& scene);

/// A ready-made survival inventory on the real components: a 2D top-down Player
/// (tagged "Player") carrying an Inventory, the Minecraft-style hotbar + backpack
/// (E to open, 1-9 / wheel to pick), a loot chest (walk up, F) and a crafting bench
/// (walk up, C) that crafts from the player's bag. A starting point for survival,
/// RPG and crafting games — no scripting required.
void Inventory(Scene& scene);

/// A complete, playable game of Snake — written entirely in OkayScript on a
/// single Tilemap object (snake body via an array, board via tile editing,
/// food via randi, score via prefs). Steer with WASD/arrows. A compact showcase
/// of arrays + tilemap scripting + input.
void Snake(Scene& scene);

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
    tr->screenSpace = true; tr->screenPos = {12, kHudTop}; tr->pixelSize = 3.0f; tr->vcenter = false;
    tr->text = "Score: 0";
    hud->AddComponent<ScriptComponent>("okayscript")->LoadSource(
        "function update(d) { set_text(\"Score: \" + prefs_get(\"score\")); }\n");
}

/// A complete, playable Brick Breaker. The paddle is OkayScript (A/D), the ball
/// is OkayScript (walls, paddle, and brick hits), and each brick is a trigger
/// the ball removes by name. A HUD reads the shared score. Misses reset the
/// ball; clearing the wall wins. Uses only OkayScript + components.
void BrickBreaker(Scene& scene);

/// A terrain-digging sandbox: a procedurally generated, eroded hilly terrain you
/// can walk on (first-person) and reshape live in Play with a TerrainDigger — hold
/// the mouse to carve craters, with a ring marker showing where you're digging.
/// A ready base for survival / mining / building games.
void TerrainSandbox(Scene& scene);

/// The one unified sandbox: a single smooth voxel terrain you walk on AND dig real
/// caves/tunnels/overhangs into (no separate heightmap terrain), with a lake of
/// animated water, a first-person player with full voxel collision (you stand on
/// the ground and in caves, not fall through), and a pause menu. Left mouse digs,
/// right mouse fills, Space jumps, Esc pauses.
/// `grid` sizes the voxel field (cells along X, Y and Z); the default 72 x 44 x 72 is the owner's Sandbox. A bigger grid
/// is the same scene on a bigger hill (examples/voxel/voxel_big_world.okayscene is 128 x 56 x 128: the digging frame
/// rate matters much more there).
void VoxelSandbox(Scene& scene, int gridX, int gridY, int gridZ,
                         float hillHeight = 9.0f, unsigned seed = 2024u);
/// A boat you can drive, ready to float: a hull with a Rigidbody3D, a box collider, Buoyancy and a Boat Controller
/// (W/S throttle, A/D steer, Space brake, camera behind it). It has a cabin, a windscreen and an engine as plain
/// look-only blocks. The nose points along +Z of the object. `pos` is where the waterline of the hull goes (the
/// hull sits half under it). If the scene has no Water, a lake is made under it so the boat floats at once.
/// Written from our own design (docs/rendering/terrain_water.md); not taken from any other engine.
GameObject* AddBoat(Scene& scene, const Vec3& pos = {0, 0, 0}, float yawDegrees = 0.0f);

/// "Boat Lake": a lake with rolling waves, a wooden dock, floating crates, a stone that sinks and a boat to drive.
/// Open it, press Play, drive with W / S (go, back) and A / D (steer), Space brakes. examples/boat/boat_lake.okayscene
/// is this scene saved to a file (tests/test_boat.cpp checks the file still matches; regenerate with OKAY_REGEN_BOAT=1).
void BoatLake(Scene& scene);

/// The Sandbox at its default size. A separate overload (not default arguments) so `&Templates::VoxelSandbox` still converts to `void (*)(Scene&)`,
/// which the template table and several tests rely on.
inline void VoxelSandbox(Scene& scene) { VoxelSandbox(scene, 72, 44, 72); }

} // namespace Templates

} // namespace okay
