// The scene text reader: ParseStep reads a slice of a document into a scene, one record at a time. Moved unchanged from
// SceneSerializer.cpp (docs/engine/modularization_plan.md, section 3.1, S1).
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/PrefabTools.hpp"
#include "okay/Scene/SceneText.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <iterator>
#include "okay/Core/TolerantNumbers.hpp"
#include "okay/Core/AssetDatabase.hpp"
#include <cerrno>
#include <iostream>
#include <charconv>
#include <limits>
#include <locale>
#include "okay/Animation/HumanoidLibrary.hpp"
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Components/Seat.hpp"
#include "okay/Components/VehicleSeat.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/LODGroup.hpp"
#include "okay/Components/Occluder.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/MeshModifiers.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/CharacterController2D.hpp"
#include "okay/Components/CharacterController3D.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/PlayerView.hpp"
#include "okay/Components/PlayerController.hpp"
#include "okay/Components/UITheme.hpp"          // grok/ui-theme
#include "okay/Components/UIStyleVariant.hpp"
#include "okay/Components/UIDamageNumbers.hpp"
#include "okay/Components/UIDialogueBox.hpp"
#include "okay/Components/UIInventoryBind.hpp"
#include "okay/Components/SequencePlayer.hpp"
#include "okay/Components/Buoyancy.hpp"
#include "okay/Components/BoatController.hpp"
#include "okay/Components/BlockBuilder.hpp"
#include "okay/Components/StructureBuilder.hpp"
#include "okay/Components/BuilderMode.hpp"
#include "okay/Components/BuildMode.hpp"
#include "okay/Components/LogicLinks.hpp"
#include "okay/Components/LoadingScreen.hpp"
#include "okay/Components/InventoryUI.hpp"
#include "okay/Components/GridInventory.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/Inventory/Recipe.hpp"
#include "okay/Components/GridInventoryUI.hpp"
#include "okay/Components/CraftingStation.hpp"
#include "okay/Components/ChestInventory.hpp"
#include "okay/Components/NetInventory.hpp"
#include "okay/Components/ItemPickup.hpp"
#include "okay/Components/LootSource.hpp"
#include "okay/Components/Equipment.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/SurvivalAfflictions.hpp"
#include "okay/Components/SurvivalSystems.hpp"
#include "okay/Components/SurvivalZone.hpp"
#include "okay/Components/Volume.hpp"
#include "okay/Components/WindZone.hpp"
#include "okay/Components/Consumables.hpp"
#include "okay/Components/DayNightCycle.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/BehaviorTreeRunner.hpp"
#include "okay/Components/Crafting.hpp"
#include "okay/Components/MeleeAttacker.hpp"
#include "okay/Components/RangedWeapon.hpp"
#include "okay/Components/Spawner.hpp"
#include "okay/Components/CraftingMenu.hpp"
#include "okay/Components/ThirdPersonShooterController.hpp"
#include "okay/Components/TopDownController.hpp"
#include "okay/Components/TopDownShooterController.hpp"
#include "okay/Components/Ragdoll.hpp"
#include "okay/Components/Platformer25DController.hpp"
#include "okay/Components/Projectile3D.hpp"
#include "okay/Components/TopDownController2D.hpp"
#include "okay/Components/FreeRoamController.hpp"
#include "okay/Components/ClickToMoveController.hpp"
#include "okay/Components/NavMeshSurface.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/NavMeshLink.hpp"
#include "okay/Components/NavMeshModifier.hpp"
#include "okay/Components/NavMeshObstacle.hpp"
#include "okay/Components/FollowTarget2D.hpp"
#include "okay/Components/GridPathAgent2D.hpp"
#include "okay/Components/GridCostLayer.hpp"
#include "okay/Components/GridObstacle2D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/ColliderShapes3D.hpp"
#include "okay/Components/Mover.hpp"
#include "okay/Components/Spinner.hpp"
#include "okay/Components/Lifetime.hpp"
#include "okay/Components/NoCode.hpp"
#include "okay/Components/Stats.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/TurnManager.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/DollyPath.hpp"
#include "okay/Components/Spline.hpp"
#include "okay/Components/SplineMesh.hpp"
#include "okay/Components/LineRenderer.hpp"
#include "okay/Components/VirtualCamera.hpp"
#include "okay/Components/CinemachineBrain.hpp"
#include "okay/Components/CameraRoom.hpp"
#include "okay/Components/CameraZone.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/SpriteAnimator.hpp"
#include "okay/Components/Animator.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/MusicPlayer.hpp"
#include "okay/Components/Tilemap.hpp"
#include "okay/Components/TilemapCollider2D.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/Canvas.hpp"
#include "okay/Components/UIScrollView.hpp"
#include "okay/Components/UILayoutGroup.hpp"
#include "okay/Components/UIFlexItem.hpp"
#include "okay/Components/UIMask.hpp"
#include "okay/Components/UIFrame.hpp"
#include "okay/Components/UIModal.hpp"
#include "okay/Components/UIToast.hpp"   // grok/ui-toast
#include "okay/Components/StoryState.hpp"      // dialogue + quests: story variables
#include "okay/Components/DialogueRunner.hpp"
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/UIContextMenu.hpp"
#include "okay/Components/UIKeybindField.hpp"
#include "okay/Components/UIElement.hpp"   // GetUIRect: settle loaded UIFrames
#include "okay/Components/UIListView.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/WorldUI.hpp"
#include "okay/Components/WorldSpaceUI.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/UITooltip.hpp"
#include "okay/Components/UIAnimation.hpp"
#include "okay/Components/TweenAnimation.hpp"
#include "okay/Components/UITextBind.hpp"
#include "okay/Components/UIBarBind.hpp"
#include "okay/Components/UIDraggable.hpp"
#include "okay/Components/Draggable.hpp"
#include "okay/Components/EventSystem.hpp"
#include "okay/Components/UIDocument.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Components/Foliage.hpp"
#include "okay/Components/Cable.hpp"
#include "okay/Components/Decal.hpp"
#include "okay/Components/TerrainDigger.hpp"
#include "okay/Components/WorldStreamer.hpp"
#include "okay/Components/Destructible.hpp"
#include "okay/Components/VoxelTerrain.hpp"
#include "okay/Components/VoxelDigger.hpp"
#include "okay/Components/Water.hpp"
#include "okay/Components/Flashlight.hpp"
#include "okay/Components/FirstPersonHand.hpp"
#include "okay/Components/FirstPersonHands.hpp"
#include "okay/Components/NetworkSync.hpp"
#include "okay/Components/NetworkObject.hpp"
#include "okay/Components/AntiCheatConfig.hpp"
#include "okay/Components/NetValidator.hpp"
#include "okay/Testing/SceneValidator.hpp"
#include <cmath>
#include "okay/Components/NetworkPlayerSpawner.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include "okay/Components/BlendShapes.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Joint3D.hpp"
#include "okay/Components/DebrisEmitter.hpp"
#include "okay/Components/Joint2D.hpp"
#include "okay/Components/AimIK.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/HumanoidRetarget.hpp"
#include "okay/Components/LimbIK.hpp"
#include "okay/Components/LedgeHandsIK.hpp"
#include "okay/Components/ChainIK.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/IKTarget.hpp"
#include "okay/Components/AimOffset.hpp"
#include "okay/Components/PoseCapture.hpp"
#include "okay/Components/ProceduralObjectHolding.hpp"
#include "okay/Components/HoldPrompt.hpp"
#include "okay/Components/LocalPlayerInput.hpp"
#include "okay/Components/RootMotion.hpp"
#include "okay/Components/PauseMenu.hpp"
#include "okay/Components/AutoSave.hpp"
#include "okay/Core/GameSaves.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/UIImage.hpp"
#include "okay/Components/UIRawImage.hpp"
#include "okay/Components/UISafeArea.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIRadialProgress.hpp"
#include "okay/Components/Minimap.hpp"
#include "okay/Components/UIColorPicker.hpp"   // grok/ui-color
#include "okay/Components/UISlotGrid.hpp"   // grok/ui-hud
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIStepper.hpp"
#include "okay/Components/UIWidgetsMore.hpp"
#include "okay/Components/UIRating.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UITabs.hpp"
#include <cctype>
#include <functional>
#include "okay/Core/DataPack.hpp"   // transparent unpack of obfuscated game data
#include "okay/Core/Utf8.hpp"
#include "okay/Core/FileCopy.hpp"
#include <fstream>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include "okay/Scene/ComponentSerializer.hpp"
#include <algorithm>
#include <vector>
#include "SceneSerializerInternal.hpp"

namespace okay {
namespace sceneser {

// The view-toggle keys of a PlayerView. `viewKey` names the start view: the
// legacy "playerview" record calls it "view", the Player Controller "start".
static void ReadPlayerViewKeys(KvIn& k, PlayerView* pv, const char* viewKey) {
    k.b("allow", pv->allowToggle);
    int view = (int)pv->defaultView;
    k.i(viewKey, view);
    pv->defaultView = (view == 0) ? PlayerView::View::First : PlayerView::View::Third;
    k.key("key", pv->toggleKey);
    k.i("pad", pv->toggleGamepadButton);
    k.f("blend", pv->blendTime);
    int ease = (int)pv->switchEase;
    k.i("ease", ease);
    if (ease < 0) ease = 0;
    if (ease > 3) ease = 3;
    pv->switchEase = (PlayerView::SwitchEase)ease;
    k.b("snap", pv->snapSwitch);
    k.f("ex", pv->eyeOffset.x); k.f("ey", pv->eyeOffset.y); k.f("ez", pv->eyeOffset.z);
    k.f("dist", pv->thirdPersonDistance);
    k.f("shoulder", pv->shoulderOffset);
    k.b("presetKeys", pv->presetHotkeys);
}

// Files from before version 3 came from starters whose First Person and Third
// Person players carried one controller each, and the view toggle faked the
// other view from it: a level orbit at eye height that still strafed like first
// person, or an eye that still steered like the orbit. A toggle on such a
// player gets the other controller, and the camera it will blend is mounted on
// the player where both controllers look for it, so the switch runs as it does
// on the current starters: first person is the eye controller, third person
// the orbit. A player that already has both, or neither, is left alone.
static void CompleteViewToggle(Scene& scene, GameObject* go) {
    auto* view = go ? go->GetComponent<PlayerView>() : nullptr;
    if (!view || !view->allowToggle || !go->transform) return;
    auto* fp = go->GetComponent<FirstPersonController>();
    auto* tp = go->GetComponent<ThirdPersonController>();
    if ((fp != nullptr) == (tp != nullptr)) return;
    const bool wasThird = tp != nullptr;
    if (!fp) fp = go->AddComponent<FirstPersonController>();
    if (!tp) tp = go->AddComponent<ThirdPersonController>();

    // A camera already under the player serves both controllers.
    for (Transform* c : go->transform->Children())
        if (c && c->gameObject && c->gameObject->GetComponent<Camera>()) return;
    // The old third-person starter left its camera loose in the scene: the main
    // one, else the first. One that already sits on another player stays there.
    Camera* cam = nullptr;
    for (const auto& obj : scene.Objects()) {
        auto* c = obj ? obj->GetComponent<Camera>() : nullptr;
        if (!c || !c->transform) continue;
        Transform* parent = c->transform->Parent();
        if (parent && parent->gameObject && parent->gameObject->GetComponent<PlayerView>()) continue;
        if (!cam || (c->main && !cam->main)) cam = c;
    }
    if (!cam) return;
    for (Transform* t = go->transform; t; t = t->Parent())
        if (t == cam->transform) return;   // the player hangs under this camera
    if (!cam->transform->SetParent(go->transform, /*worldPositionStays=*/false)) return;
    if (wasThird) {
        // The resting orbit, as the third-person starter poses it; the controller
        // writes the same pose on the first frame.
        const float rest = tp->pitch * Mathf::Deg2Rad;
        const float dist = tp->distance > 0.0f ? tp->distance : 5.0f;
        cam->transform->localPosition = {view->shoulderOffset, tp->cameraHeight + std::sin(rest) * dist, std::cos(rest) * dist};
        cam->transform->localRotation = Quat::Euler(-tp->pitch, 0.0f, 0.0f);
    } else {
        cam->transform->localPosition = {0.0f, fp->standEyeHeight, -0.20f};   // the eye, just in front of the face
        cam->transform->localRotation = Quat::Identity;
    }
}


// Parse (a slice of) a document into `scene`, which the caller has already
// cleared if it is a full load. If `clear` is false, objects are appended (used
// by Instantiate); `firstNew` receives the first created GameObject. Unknown
// records are skipped and named in `warnings`. With `maxObjects` > 0 the parse
// stops between objects after that many and sets `finished` false; call again
// with the same session to continue. This writes straight into `scene`, so
// ParseInto / SceneLoadJob only run it on a scene that a failure can't hurt (or
// roll back what it made).
bool ParseStep(Scene& scene, ParseSession& S, bool clear,
                      GameObject** firstNew, std::string* error,
                      std::vector<std::string>* warnings, int maxObjects, bool* finished) {
    if (finished) *finished = true;
    const std::string& text = S.text;
    std::istringstream& in = S.in;
    std::string token;

    if (!S.started) {
        if (!(in >> token) || token != "okayscene") {
            if (error) *error = "missing 'okayscene' header";
            return false;
        }
        int version = 0;
        if (!(in >> version) || version < 1) {
            if (error) *error = "bad 'okayscene' version";
            return false;
        }
        S.version = version;
        S.started = true;
        if (version > SceneSerializer::kSceneFormatVersion)
            S.warnings.push_back("scene is format version " + std::to_string(version) + ", newer than this build reads (" +
                                 std::to_string(SceneSerializer::kSceneFormatVersion) + "); loading what is understood");
    }
    const int version = S.version;
    auto warn = [&](const std::string& what) { S.warnings.push_back(what); };
    // An unknown record is read with ReadRecordLine (one per line, a quoted value that spans lines is part of it),
    // so it can be stepped over, and kept, without knowing what its values mean.
    auto& byIndex = S.byIndex;
    auto& parentLinks = S.parentLinks;
    auto& siblingHints = S.siblingHints;
    auto& seenGuids = S.seenGuids;
    int madeObjects = 0;

    // Record validation. Counts are checked against the input still unread (each
    // element takes at least `minChars` characters), so a bogus count can't make a
    // loader allocate or loop far past the end of the file.
    auto remaining = [&]() -> unsigned long long {
        // Ask the buffer, not tellg(): tellg() sets failbit once eof is reached.
        const std::streamoff at = in.rdbuf()->pubseekoff(0, std::ios::cur, std::ios::in);
        return at < 0 || (std::size_t)at > text.size() ? 0ull : (unsigned long long)(text.size() - (std::size_t)at);
    };
    auto countOk = [&](long long n, unsigned long long minChars) {
        return n >= 0 && (unsigned long long)n <= remaining() / minChars;   // divide: n * minChars can wrap
    };
    auto bad = [&](const std::string& what) {
        if (error) *error = what;
        return false;
    };

    while (in >> token) {
        // The file-level globals (name, gravity, lighting, fog) describe the WHOLE
        // scene, so a merge/additive load (clear == false) reads past them but keeps
        // the host scene's own settings — only a full load (clear) applies them.
        if (token == "name") {
            std::string nm = ReadQuoted(in); if (clear) scene.SetName(nm);
        } else if (token == "uifont") {
            std::string uf = ReadQuoted(in); if (clear) scene.uiFont = uf;
        } else if (token == "uiquality") {
            int q = 1; in >> q; if (clear) scene.uiQuality = q < 0 ? 0 : (q > 3 ? 3 : q);
        } else if (token == "layermatrix") {
            std::uint32_t rows[32]; bool ok = true;
            for (int i = 0; i < 32; ++i) { unsigned long long v = 0; if (!(in >> v) || v > 0xFFFFFFFFull) { ok = false; break; } rows[i] = (std::uint32_t)v; }
            if (!ok) return bad("bad 'layermatrix' record");
            if (clear)   // a pair collides only when both rows allow it
                for (int a = 0; a < 32; ++a)
                    for (int b = a; b < 32; ++b)
                        scene.SetLayerCollision(a, b, ((rows[a] >> b) & 1u) && ((rows[b] >> a) & 1u));
        } else if (token == "gravity") {
            Vec2 g; in >> g.x >> g.y;
            if (clear) scene.physics().gravity = g;
        } else if (token == "refimage") {
            modeling::RefImageRecord r; int fl = 0, vis = 1;
            r.path = ReadQuoted(in);
            in >> r.plane >> r.centre.x >> r.centre.y >> r.centre.z >> r.width >> r.opacity >> fl >> vis;
            if (clear && in && r.plane >= 0 && r.plane <= 2 && std::isfinite(r.width + r.opacity + r.centre.x + r.centre.y + r.centre.z)) {
                r.flipX = fl != 0; r.visible = vis != 0; scene.refImages.push_back(r);
            }
        } else if (token == "gravity3") {
            Vec3 g; in >> g.x >> g.y >> g.z;
            if (clear && in) scene.physics3D().gravity = g;
        } else if (token == "physicsquality") {
            int q3 = 0, it3 = 10, ac = 1, q2 = 0, it2 = 10;
            in >> q3 >> it3 >> ac;
            in >> std::ws; if (std::isdigit(static_cast<unsigned char>(in.peek()))) in >> q2 >> it2; else q2 = q3, it2 = it3;
            if (clear) {
                // (kept as read, so the file saves back unchanged; there is one physics path and the value changes nothing)
                scene.physics3D().quality = q3 == 2 ? PhysicsQuality::Engine : q3 == 1 ? PhysicsQuality::Improved : PhysicsQuality::Classic;
                scene.physics3D().solverIterations = it3 < 4 ? 4 : (it3 > 30 ? 30 : it3);
                scene.physics3D().autoContinuous = (ac != 0);
                scene.physics().quality = q2 == 2 ? PhysicsQuality::Engine : q2 == 1 ? PhysicsQuality::Improved : PhysicsQuality::Classic;
                scene.physics().solverIterations = it2 < 4 ? 4 : (it2 > 30 ? 30 : it2);
            }
        } else if (token == "rendersettings") {
            int sky = 1; Color top, hor, bot; float amb = 0.0f;
            in >> sky >> top.r >> top.g >> top.b
               >> hor.r >> hor.g >> hor.b
               >> bot.r >> bot.g >> bot.b >> amb;
            if (clear) {
                auto& rs = scene.renderSettings;
                rs.skybox = (sky != 0); rs.skyTop = top; rs.skyHorizon = hor;
                rs.skyBottom = bot; rs.ambient = amb;
            }
        } else if (token == "fog") {
            auto& rs = scene.renderSettings;
            int on = 0; Color fc; float fs = rs.fogStart, fe = rs.fogEnd;
            in >> on >> fc.r >> fc.g >> fc.b >> fs >> fe;
            if (clear) { rs.fog = (on != 0); rs.fogColor = fc; rs.fogStart = fs; rs.fogEnd = fe; }
        } else if (token == "vignette") {
            float v = 0.0f; in >> v;
            if (clear) scene.renderSettings.vignette = v;
        } else if (token == "tonemap") {
            int v = 0; in >> v;
            if (clear) scene.renderSettings.tonemap = (v != 0);
        } else if (token == "sky") {
            float hp = 0.5f; int sun = 0; float sx = 0.5f, sy = 0.3f, ss = 0.05f;
            Color sc = Color::FromBytes(255, 245, 214);
            in >> hp >> sun >> sx >> sy >> ss >> sc.r >> sc.g >> sc.b;
            if (clear) {
                auto& rs = scene.renderSettings;
                rs.skyHorizonPos = hp; rs.skySun = (sun != 0);
                rs.skySunX = sx; rs.skySunY = sy; rs.skySunSize = ss; rs.skySunColor = sc;
            }
        } else if (token == "skystars") {
            int on = 0; float density = 0.5f, bright = 0.9f;
            in >> on >> density >> bright;
            if (clear) {
                auto& rs = scene.renderSettings;
                rs.skyStars = (on != 0); rs.skyStarDensity = density; rs.skyStarBright = bright;
            }
        } else if (token == "ambientmode") {
            int m = Scene::RenderSettings::AmbientHemisphere; float tint = 0.7f; in >> m >> tint;
            if (m < 0 || m > 2) m = Scene::RenderSettings::AmbientHemisphere;
            if (clear) { scene.renderSettings.ambientMode = m; scene.renderSettings.ambientSkyTint = tint; }
        } else if (token == "probegrid") {
            LightProbeGrid g; int en = 1; std::size_t count = 0;
            in >> en >> g.spacing >> g.origin.x >> g.origin.y >> g.origin.z >> g.nx >> g.ny >> g.nz >> count;
            g.enabled = en != 0;
            const std::size_t want = (std::size_t)(g.nx > 0 ? g.nx : 0) * (g.ny > 0 ? g.ny : 0) * (g.nz > 0 ? g.nz : 0) * 12;
            if (count == want && count <= 12 * 12 * 6 * 12 * 4) { g.sh.resize(count); for (float& v : g.sh) in >> v; }
            else { g.Clear(); }
            if (!g.Baked()) g.Clear();
            if (clear) scene.renderSettings.probes = g;
        } else if (token == "lightunits") {
            int on = 1; in >> on;
            if (clear) scene.renderSettings.autoBrightness = (on != 0);
        } else if (token == "fogmode") {
            Scene::RenderSettings v;   // defaults for anything the record leaves out
            int ms = 0; in >> v.fogMode >> v.fogDensity >> v.fogHeight >> v.fogFalloff >> ms;
            if (v.fogMode < 0 || v.fogMode > 2) v.fogMode = Scene::RenderSettings::FogLinear;
            if (clear) {
                auto& rs = scene.renderSettings;
                rs.fogMode = v.fogMode; rs.fogDensity = v.fogDensity; rs.fogHeight = v.fogHeight;
                rs.fogFalloff = v.fogFalloff; rs.fogMatchSky = (ms != 0);
            }
        } else if (token == "skyproc") {
            Scene::RenderSettings v;
            in >> v.skyMode >> v.skySunDisc >> v.skySunHalo >> v.skyHaze >> v.cloudCover >> v.cloudDensity >> v.cloudSpeed >> v.cloudScale;
            if (v.skyMode < 0 || v.skyMode > 1) v.skyMode = Scene::RenderSettings::SkyColour;
            if (clear) {
                auto& rs = scene.renderSettings;
                rs.skyMode = v.skyMode; rs.skySunDisc = v.skySunDisc; rs.skySunHalo = v.skySunHalo; rs.skyHaze = v.skyHaze;
                rs.cloudCover = v.cloudCover; rs.cloudDensity = v.cloudDensity; rs.cloudSpeed = v.cloudSpeed; rs.cloudScale = v.cloudScale;
            }
        } else if (token == "post") {
            Scene::RenderSettings::PostSettings p;
            int bloom = 0, grading = 0, fxaa = 0, ssao = 0, dof = 0;
            in >> p.exposure >> p.tonemap >> bloom >> p.bloomThreshold >> p.bloomIntensity >> p.bloomRadius
               >> grading >> p.temperature >> p.tint >> p.saturation >> p.contrast
               >> p.lift.x >> p.lift.y >> p.lift.z >> p.gamma.x >> p.gamma.y >> p.gamma.z >> p.gain.x >> p.gain.y >> p.gain.z
               >> p.vignetteSmooth >> fxaa >> ssao >> p.ssaoRadius >> p.ssaoIntensity >> dof >> p.dofFocus >> p.dofRange;
            p.bloom = bloom != 0; p.grading = grading != 0; p.fxaa = fxaa != 0; p.ssao = ssao != 0; p.dof = dof != 0;
            if (p.tonemap < 0 || p.tonemap > 2) p.tonemap = Scene::RenderSettings::PostSettings::TonemapACES;
            if (clear) scene.renderSettings.post = p;
        } else if (token == "postq") {
            float soft = 0.0f, power = 1.0f;
            in >> soft >> power;
            if (clear && in) {
                scene.renderSettings.post.bloomSoftness = soft < 0.0f ? 0.0f : (soft > 1.0f ? 1.0f : soft);
                scene.renderSettings.post.ssaoPower = power < 0.25f ? 0.25f : (power > 4.0f ? 4.0f : power);
            }
        } else if (token == "weather") {
            Scene::RenderSettings::WeatherSettings w;
            in >> w.preset >> w.intensity >> w.fade >> w.wind.x >> w.wind.y >> w.wind.z;
            w.thunderSound = ReadQuoted(in);
            if (w.preset < 0 || w.preset > 3) w.preset = Scene::RenderSettings::WeatherSettings::None;
            if (clear) scene.renderSettings.weather = w;
        } else if (token == "gameobject") {
            int idx = -1;
            in >> idx;
            std::string name = ReadQuoted(in);
            if (!in) return bad("truncated gameobject header");
            if (byIndex.count(idx)) return bad("duplicate gameobject index " + std::to_string(idx));
            GameObject* go = scene.CreateGameObject(name);
            if (!S.first) S.first = go;
            if (firstNew && !*firstNew) *firstNew = go;
            byIndex[idx] = go;
            // The state machine the animsm* records apply to (layers add more).
            AnimStateMachine* curSM = nullptr;
            auto smFor = [&]() -> AnimStateMachine* {
                if (curSM) return curSM;
                curSM = go->GetComponent<AnimStateMachine>();
                if (!curSM) curSM = go->AddComponent<AnimStateMachine>();
                return curSM;
            };

            std::string field;
            bool sawEnd = false;
            std::vector<std::pair<std::string, int>> compOff;   // (type key, occurrence) switched off
            std::vector<std::string> compOrder;                  // `comporder`: type keys in the saved component order
            // Step over the rest of the record `field` and keep its text on the object, as it was read; the next
            // save writes it back (WriteComponents), so this build does not drop what it cannot use.
            auto keepRecord = [&]() {
                const std::string rest = ReadRecordLine(in);
                if (go->unknownRecords.size() < kMaxUnknownRecords && field.size() + rest.size() <= kMaxUnknownRecordSize)
                    go->unknownRecords.push_back(field + rest);
            };
            // A record that a module reads (its register.cpp), at this place in the chain. With the module off, or
            // on with no reader registered, the record is kept like one of a newer build: a scene saved by a
            // build without the module still has it when the module is back.
            auto moduleRecord = [&](const char* module) {
                auto entry = ModuleRegistry::Get().IsEnabled(module) ? ComponentSerializerRegistry::Find(field) : nullptr;
                if (entry && entry->read) { entry->read(*go, in); return; }
                warn("kept '" + field + "' on object " + std::to_string(idx) + " unread (module '" + module + "' is off or has no reader)");
                keepRecord();
            };
            int nFpFeel = 0, nTpFeel = 0, nFpStance = 0, nTpStance = 0;
            int nCc3dSt = 0, nTpsSt = 0, nTdSt = 0, nCtmSt = 0, nNpcSt = 0;
            int nIkAim = 0, nIkLook = 0, nIkLimb = 0, nIkChain = 0;
            while (in >> field) {
                if (field == "end") { sawEnd = true; break; }
                bool fieldDone = true;   // the chain is split in chunks: MSVC allows ~128 nested blocks (C1061)
                if (field == "active") { int a = 1; in >> a; go->active = (a != 0); }
                else if (field == "tag") { go->tag = ReadQuoted(in); }
                else if (field == "static") { int s = 0; in >> s; go->isStatic = (s != 0); }
                else if (field == "locked") { int l = 0; in >> l; go->editorLocked = (l != 0); }
                else if (field == "layer") {
                    int l = 0; in >> l;
                    if (l < 0 || l > 31) {   // renderers and the collision matrix read it modulo 32: say so, and store what they use
                        warn("layer out of range on object " + std::to_string(idx) + ": " + std::to_string(l) + " (used " + std::to_string(l & 31) + ")");
                        l &= 31;
                    }
                    go->layer = l;
                }
                else if (field == "uiorder") { in >> go->uiDrawOrder; }
                else if (field == "srcscene") { go->sourceScene = ReadQuoted(in); }
                else if (field == "prefab") { go->prefabSource = ReadQuoted(in); }
                else if (field == "parent") { int p = -1; in >> p; if (p >= 0) parentLinks.push_back({idx, p}); }
                else if (field == "transform") {
                    Vec3 p, s; Quat q;
                    in >> p.x >> p.y >> p.z >> q.x >> q.y >> q.z >> q.w >> s.x >> s.y >> s.z;
                    {   // a NaN or infinity (a physics blow-up that got saved) would poison every pose below it
                        auto fin = [](float f) { return std::isfinite(f); };
                        bool repaired = false;
                        if (!fin(p.x) || !fin(p.y) || !fin(p.z)) { p = Vec3::Zero; repaired = true; }
                        if (!fin(q.x) || !fin(q.y) || !fin(q.z) || !fin(q.w) ||
                            q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w < 1e-8f) { q = Quat::Identity; repaired = true; }   // also all zero: no rotation at all
                        if (!fin(s.x) || !fin(s.y) || !fin(s.z)) { s = Vec3::One; repaired = true; }
                        if (repaired) warn("non-finite transform on object " + std::to_string(idx) + " was reset");
                    }
                    go->transform->localPosition = p;
                    go->transform->localRotation = q;
                    go->transform->localScale = s;
                } else if (field == "sprite") {
                    int glyph = 0; Color c; Vec2 size;
                    in >> glyph >> c.r >> c.g >> c.b >> c.a >> size.x >> size.y;
                    auto* sr = go->AddComponent<SpriteRenderer>();
                    sr->glyph = static_cast<char>(glyph);
                    sr->color = c;
                    sr->size = size;
                    in >> std::ws; // optional texture path (quoted), then an optional "guid <id>"
                    if (in.peek() == '"') sr->texture = ReadQuoted(in);
                    in >> std::ws;
                    if (in.peek() == 'g') {
                        const auto mark = in.tellg();
                        std::string word;
                        in >> word;
                        if (word == "guid") {
                            std::string id;
                            in >> id;
                            sr->texture = ResolveSceneAssetPath(sr->texture, id);
                        } else if (mark != std::streampos(-1)) {
                            in.clear();
                            in.seekg(mark);
                        }
                    }
                    // Optional uv sub-region (4 floats) for sprite sheets.
                    in >> std::ws;
                    int pk = in.peek();
                    if (pk == '-' || pk == '.' || std::isdigit(pk)) {
                        in >> sr->uvMin.x >> sr->uvMin.y >> sr->uvMax.x >> sr->uvMax.y;
                        in >> std::ws; // optional sortOrder follows uv
                        int pk2 = in.peek();
                        if (pk2 == '-' || std::isdigit(pk2)) {
                            in >> sr->sortOrder;
                            in >> std::ws; // optional flipX/flipY follow sortOrder
                            if (std::isdigit(in.peek())) {
                                int fx = 0, fy = 0; in >> fx >> fy;
                                sr->flipX = (fx != 0); sr->flipY = (fy != 0);
                                in >> std::ws; // optional sortingLayer follows flips
                                if (in.peek() == '-' || std::isdigit(in.peek()))
                                    in >> sr->sortingLayer;
                                in >> std::ws; // optional texFilter follows sortingLayer
                                if (std::isdigit(in.peek())) {
                                    int tf = 0; in >> tf;
                                    sr->texFilter = (SpriteRenderer::TexFilter)tf;
                                }
                            }
                        }
                    }
                    in >> std::ws;   // optional guid=<id> after the positional fields
                    if (in.peek() == 'g') {
                        const auto mark = in.tellg();
                        std::string word;
                        in >> word;
                        if (word.size() > 5 && word.compare(0, 5, "guid=") == 0)
                            sr->texture = ResolveSceneAssetPath(sr->texture, word.substr(5));
                        else if (mark != std::streampos(-1)) { in.clear(); in.seekg(mark); }
                    }
                } else if (field == "camera") {
                    Color c; int proj = 0; float ortho = 5.0f, fov = 60.0f; int main = 1;
                    in >> proj >> ortho >> fov >> c.r >> c.g >> c.b >> c.a >> main;
                    auto* cam = go->AddComponent<Camera>();
                    cam->projection = (Camera::Projection)proj;
                    cam->orthographicSize = ortho;
                    cam->fieldOfView = fov;
                    cam->backgroundColor = c;
                    cam->main = (main != 0);
                    in >> std::ws; // optional clearFlags + depth + near + far (added later)
                    if (std::isdigit(in.peek()) || in.peek() == '-') {
                        int cf = 0; in >> cf >> cam->depth >> cam->nearClip >> cam->farClip;
                        cam->clearFlags = (Camera::ClearFlags)(cf >= 0 && cf <= 3 ? cf : 0);
                        in >> std::ws; // optional FOV axis + viewport rect (added later)
                        if (std::isdigit(in.peek())) {
                            int fax = 0; in >> fax >> cam->rectX >> cam->rectY >> cam->rectW >> cam->rectH;
                            cam->fovAxisHorizontal = (fax != 0);
                            in >> std::ws; // optional culling mask + physical camera (added later)
                            if (std::isdigit(in.peek()) || in.peek() == '-') {
                                int phys = 0;
                                in >> cam->cullingMask >> phys >> cam->focalLength >> cam->sensorHeight;
                                cam->physicalCamera = (phys != 0);
                                in >> std::ws; // optional zoom / dutch / lens shift / pixel perfect / shake (added later)
                                if (std::isdigit(in.peek()) || in.peek() == '-' || in.peek() == '.') {
                                    int pp = 0;
                                    in >> cam->zoom >> cam->dutch >> cam->lensShiftX >> cam->lensShiftY >> pp
                                       >> cam->pixelsPerUnit >> cam->shakeAmount >> cam->shakeAngle
                                       >> cam->shakeFrequency >> cam->shakeRecovery;
                                    cam->pixelPerfect = (pp != 0);
                                    if (!(cam->zoom > 0.01f)) cam->zoom = 1.0f;
                                    in >> std::ws;
                                    if (in.peek() == '"') cam->renderTarget = ReadQuoted(in);
                                }
                            }
                        }
                    }
                } else if (field == "mesh") {
                    std::string kind = ReadQuoted(in);
                    Color c; int wire = 1;
                    in >> c.r >> c.g >> c.b >> c.a >> wire;
                    auto* mr = go->AddComponent<MeshRenderer>();
                    mr->mesh = Mesh::FromName(kind);
                    mr->color = c;
                    mr->wireframe = (wire != 0);
                    in >> std::ws; // optional .OBJ model path (quoted)
                    if (in.peek() == '"') {
                        mr->meshPath = ReadQuoted(in);
                        if (!mr->meshPath.empty()) {
                            // .okaymesh or .obj; an exported game finds the file next to its scene.
                            Mesh loaded; std::string tex, err;
                            if (modeling::LoadMeshAsset(mr->meshPath, loaded, &tex, &err, g_loadingSceneDir)) {
                                mr->mesh = loaded;
                                mr->meshPathHash = modeling::GeometryHash(mr->mesh);
                                if (!tex.empty() && mr->texture.empty()) mr->texture = tex;
                            } else {
                                OKAY_WARN("Mesh file for '", go->name, "': ", err);
                            }
                        }
                    }
                    in >> std::ws; // optional double-sided flag
                    if (std::isdigit(in.peek())) { int ds = 0; in >> ds; mr->doubleSided = (ds != 0); }
                } else if (field == "meshgeo") {
                    // Custom/edited geometry written by the `meshgeo` record. It
                    // overrides the placeholder geometry the preceding `mesh` line
                    // built from a (now-empty) name. Operate on the existing
                    // MeshRenderer (add one if somehow absent).
                    auto* mr = LastOf(go->GetComponents<MeshRenderer>());
                    if (!mr) mr = go->AddComponent<MeshRenderer>();
                    int vcount = 0; in >> vcount;
                    if (!in || !countOk(vcount, 3)) return bad("meshgeo: bad vertex count");
                    mr->mesh.vertices.clear();
                    mr->mesh.vertices.reserve(vcount > 0 ? vcount : 0);
                    for (int i = 0; i < vcount && in; ++i) {
                        Vec3 v; in >> v.x >> v.y >> v.z; mr->mesh.vertices.push_back(v);
                    }
                    int icount = 0; in >> icount;
                    if (!in || !countOk(icount, 1)) return bad("meshgeo: bad index count");
                    mr->mesh.triangles.clear();
                    mr->mesh.triangles.reserve(icount > 0 ? icount : 0);
                    for (int i = 0; i < icount && in; ++i) { int idx = 0; in >> idx; mr->mesh.triangles.push_back(idx); }
                    for (int idx : mr->mesh.triangles)
                        if (idx < 0 || idx >= (int)mr->mesh.vertices.size()) return bad("meshgeo: triangle index " + std::to_string(idx) + " out of range");
                    mr->mesh.name = "";
                    mr->mesh.uvs.clear();
                    mr->mesh.triColors.clear();
                    mr->mesh.polyFace.clear();
                    mr->mesh.cornerUVs.clear();
                    mr->mesh.seams.clear();
                    // Flat (faceted) shading, like the built-in primitives — do NOT
                    // force smooth normals, or an edited box/ground reloads looking like
                    // a soft rounded blob instead of clean flat faces.
                    mr->mesh.normals.clear();
                } else if (field == "meshvar") {
                    // Procedural variant of a named prop: regenerate the varied
                    // shape (its face colors come from the later meshcolors record,
                    // or from this regeneration if that record is absent).
                    int v = 0; in >> v;
                    auto* mr = LastOf(go->GetComponents<MeshRenderer>());
                    if (!mr) mr = go->AddComponent<MeshRenderer>();
                    mr->meshVariant = v;
                    if (v != 0 && Mesh::NameHasVariants(mr->mesh.name))
                        mr->mesh = Mesh::FromNameSeeded(mr->mesh.name, v);
                } else if (field == "meshuv") {
                    // Per-vertex UVs for meshgeo geometry (optional record that
                    // follows meshgeo, which cleared the uvs array).
                    auto* mr = LastOf(go->GetComponents<MeshRenderer>());
                    if (!mr) mr = go->AddComponent<MeshRenderer>();
                    int uc = 0; in >> uc;
                    if (!in || !countOk(uc, 2)) return bad("meshuv: bad count");
                    mr->mesh.uvs.clear();
                    mr->mesh.uvs.reserve(uc > 0 ? uc : 0);
                    for (int i = 0; i < uc && in; ++i) {
                        Vec2 t; in >> t.x >> t.y; mr->mesh.uvs.push_back(t);
                    }
                } else if (field == "meshpoly") {
                    // Per-triangle polygon ids for meshgeo geometry (optional record).
                    auto* mr = LastOf(go->GetComponents<MeshRenderer>());
                    if (!mr) mr = go->AddComponent<MeshRenderer>();
                    int pc = 0; in >> pc;
                    if (!in || !countOk(pc, 1)) return bad("meshpoly: bad count");
                    mr->mesh.polyFace.clear();
                    mr->mesh.polyFace.reserve(pc > 0 ? pc : 0);
                    for (int i = 0; i < pc && in; ++i) { int f = 0; in >> f; mr->mesh.polyFace.push_back(f); }
                } else if (field == "meshseams") {
                    // UV seam edges of meshgeo geometry (optional record).
                    auto* mr = LastOf(go->GetComponents<MeshRenderer>());
                    if (!mr) mr = go->AddComponent<MeshRenderer>();
                    int sc = 0; in >> sc;
                    mr->mesh.seams.clear();
                    for (int i = 0; i < sc && in; ++i) {
                        int a = -1, b = -1; in >> a >> b;
                        if (a >= 0 && b >= 0 && a < (int)mr->mesh.vertices.size() && b < (int)mr->mesh.vertices.size() && a != b)
                            mr->mesh.seams.push_back({std::min(a, b), std::max(a, b)});
                    }
                } else if (field == "meshshade") {
                    // Optional: restore smooth (1) or angle-based auto-smooth (2)
                    // shading on meshgeo geometry (the meshgeo reader cleared
                    // normals for flat shading).
                    auto* mr = LastOf(go->GetComponents<MeshRenderer>());
                    if (!mr) mr = go->AddComponent<MeshRenderer>();
                    int s = 0; in >> s;
                    if (s == 2) { float a = 30.0f; in >> a; mr->mesh.ComputeAutoSmoothNormals(a); }
                    else if (s) mr->mesh.ComputeSmoothNormals();
                } else if (field == "meshcolors") {
                    // Per-face colors (vertex paint / baked lightmap). Follows the
                    // `mesh`/`meshgeo` geometry so the counts line up.
                    auto* mr = LastOf(go->GetComponents<MeshRenderer>());
                    if (!mr) mr = go->AddComponent<MeshRenderer>();
                    int cc = 0; in >> cc;
                    if (!in || !countOk(cc, 4)) return bad("meshcolors: bad count");
                    mr->mesh.triColors.clear();
                    mr->mesh.triColors.reserve(cc > 0 ? cc : 0);
                    for (int i = 0; i < cc && in; ++i) {
                        Color c; in >> c.r >> c.g >> c.b >> c.a; mr->mesh.triColors.push_back(c);
                    }
                } else if (field == "meshslots") {
                    // Per-triangle material slot; ignored later if the count doesn't match.
                    auto* mr = LastOf(go->GetComponents<MeshRenderer>());
                    if (!mr) mr = go->AddComponent<MeshRenderer>();
                    int sc = 0; in >> sc;
                    if (!in || !countOk(sc, 1)) return bad("meshslots: bad count");
                    mr->mesh.triSlot.clear();
                    mr->mesh.triSlot.reserve(sc > 0 ? sc : 0);
                    for (int i = 0; i < sc && in; ++i) {
                        int sl = 0; in >> sl;
                        mr->mesh.triSlot.push_back(sl < 0 ? 0 : (sl > 255 ? 255 : sl));
                    }
                } else if (field == "meshbevelw") {
                    auto* mr = LastOf(go->GetComponents<MeshRenderer>());
                    if (!mr) mr = go->AddComponent<MeshRenderer>();
                    int bn = 0; in >> bn;
                    if (!in || bn < 0 || !countOk(bn, 6)) return bad("meshbevelw: bad count");
                    mr->mesh.bevelWeights.clear();
                    for (int i = 0; i < bn && in; ++i) { EdgeCrease c; in >> c.a >> c.b >> c.w; if (c.a >= 0 && c.b >= 0 && std::isfinite(c.w) && c.w > 0.0f) { c.w = std::min(c.w, 1.0f); mr->mesh.bevelWeights.push_back(c); } }
                } else if (field == "meshcreases") {
                    auto* mr = LastOf(go->GetComponents<MeshRenderer>());
                    if (!mr) mr = go->AddComponent<MeshRenderer>();
                    int cn = 0; in >> cn;
                    if (!in || cn < 0 || !countOk(cn, 6)) return bad("meshcreases: bad count");
                    mr->mesh.creases.clear();
                    for (int i = 0; i < cn && in; ++i) { EdgeCrease c; in >> c.a >> c.b >> c.w; if (c.a >= 0 && c.b >= 0 && std::isfinite(c.w) && c.w > 0.0f) mr->mesh.creases.push_back(c); }
                } else if (field == "meshvcolors") {
                    auto* mr = LastOf(go->GetComponents<MeshRenderer>());
                    if (!mr) mr = go->AddComponent<MeshRenderer>();
                    int vc = 0; in >> vc;
                    if (!in || vc < 0 || !countOk(vc, 4)) return bad("meshvcolors: bad count");
                    mr->mesh.vertexColors.clear();
                    mr->mesh.vertexColors.reserve((std::size_t)vc);
                    for (int i = 0; i < vc && in; ++i) { Color c; in >> c.r >> c.g >> c.b >> c.a; mr->mesh.vertexColors.push_back(c); }
                } else if (field == "meshhidden") {
                    auto* mr = LastOf(go->GetComponents<MeshRenderer>());
                    if (!mr) mr = go->AddComponent<MeshRenderer>();
                    int hn = 0, huv = 0, hco = 0; in >> hn >> huv >> hco;
                    if (!in || hn < 0 || hn > 2000000 || !countOk(hn, 20)) return bad("meshhidden: bad count");
                    Mesh& hm = mr->mesh;
                    hm.hiddenPos.clear(); hm.hiddenUV.clear(); hm.hiddenSlot.clear(); hm.hiddenPoly.clear(); hm.hiddenColor.clear();
                    for (int t = 0; t < hn && in; ++t) {
                        for (int k = 0; k < 3; ++k) { Vec3 p; in >> p.x >> p.y >> p.z; hm.hiddenPos.push_back(p); }
                        int sl = -1, pl = -1; in >> sl >> pl; hm.hiddenSlot.push_back(sl); hm.hiddenPoly.push_back(pl);
                        if (huv) for (int k = 0; k < 3; ++k) { Vec2 u; in >> u.x >> u.y; hm.hiddenUV.push_back(u); }
                        if (hco) { Color c; in >> c.r >> c.g >> c.b >> c.a; hm.hiddenColor.push_back(c); }
                    }
                    if (!in || !hm.HiddenConsistent()) { hm.hiddenPos.clear(); hm.hiddenUV.clear(); hm.hiddenSlot.clear(); hm.hiddenPoly.clear(); hm.hiddenColor.clear(); return bad("meshhidden: damaged"); }
                } else if (field == "slottex") {
                    auto* mr = LastOf(go->GetComponents<MeshRenderer>());
                    if (!mr) mr = go->AddComponent<MeshRenderer>();
                    int tc = 0; in >> tc;
                    if (!in || tc < 0 || tc > 256) return bad("slottex: bad count");
                    mr->slotTextures.clear();
                    for (int i = 0; i < tc && in; ++i) { in >> std::ws; mr->slotTextures.push_back(in.peek() == '"' ? ReadQuoted(in) : std::string()); }
                } else if (field == "slotcolors") {
                    auto* mr = LastOf(go->GetComponents<MeshRenderer>());
                    if (!mr) mr = go->AddComponent<MeshRenderer>();
                    int kc = 0; in >> kc;
                    if (!in || !countOk(kc, 4) || kc > 256) return bad("slotcolors: bad count");
                    mr->mesh.slotColors.clear();
                    for (int i = 0; i < kc && in; ++i) {
                        Color c; in >> c.r >> c.g >> c.b >> c.a; mr->mesh.slotColors.push_back(c);
                    }
                } else if (field == "skinmesh") {
                    // Rebuild a SkinnedMesh from its bind geometry + weights + joint
                    // names + inverse-bind matrices. Joints are re-resolved by name at
                    // runtime (SkinnedMesh::Start); show the bind mesh now for the editor.
                    auto* sm = go->AddComponent<SkinnedMesh>();
                    Mesh bm;
                    // Every count is checked, and the result must be consistent:
                    // one weight set per vertex and triangles inside the vertex
                    // list (Skin() and WeldVertices index by them).
                    long vc = 0; in >> vc;
                    if (!in || !countOk(vc, 3)) return bad("skinmesh: bad vertex count");
                    for (long i = 0; i < vc && in; ++i) { Vec3 v; in >> v.x >> v.y >> v.z; bm.vertices.push_back(v); }
                    long nc = 0; in >> nc;
                    if (!in || !countOk(nc, 3)) return bad("skinmesh: bad normal count");
                    for (long i = 0; i < nc && in; ++i) { Vec3 n; in >> n.x >> n.y >> n.z; bm.normals.push_back(n); }
                    long uc = 0; in >> uc;
                    if (!in || !countOk(uc, 2)) return bad("skinmesh: bad uv count");
                    for (long i = 0; i < uc && in; ++i) { Vec2 u; in >> u.x >> u.y; bm.uvs.push_back(u); }
                    long tc = 0; in >> tc;
                    if (!in || !countOk(tc, 1)) return bad("skinmesh: bad triangle count");
                    for (long i = 0; i < tc && in; ++i) { int t = 0; in >> t; bm.triangles.push_back(t); }
                    long wc = 0; in >> wc;
                    if (!in || !countOk(wc, 8)) return bad("skinmesh: bad weight count");
                    if (wc != vc) return bad("skinmesh: " + std::to_string(wc) + " weights for " + std::to_string(vc) + " vertices");
                    sm->jointIdx.clear(); sm->jointWt.clear();
                    for (long i = 0; i < wc && in; ++i) {
                        std::array<int, 4> ji{}; std::array<float, 4> jw{};
                        in >> ji[0] >> ji[1] >> ji[2] >> ji[3] >> jw[0] >> jw[1] >> jw[2] >> jw[3];
                        sm->jointIdx.push_back(ji); sm->jointWt.push_back(jw);
                    }
                    long jc = 0; in >> jc;
                    if (!in || !countOk(jc, 1)) return bad("skinmesh: bad joint count");
                    sm->jointNames.clear();
                    for (long i = 0; i < jc && in; ++i) sm->jointNames.push_back(ReadQuoted(in));
                    long bc = 0; in >> bc;
                    if (!in || !countOk(bc, 16)) return bad("skinmesh: bad bind-matrix count");
                    sm->inverseBind.clear();
                    for (long i = 0; i < bc && in; ++i) { Mat4 M; for (int e = 0; e < 16; ++e) in >> M.m[e]; sm->inverseBind.push_back(M); }
                    if (!in) return bad("skinmesh: record cut short");
                    for (int t : bm.triangles)
                        if (t < 0 || t >= (int)bm.vertices.size()) return bad("skinmesh: triangle index " + std::to_string(t) + " out of range");
                    sm->bind = bm;
                    sm->joints.clear();   // re-resolved from names in Start()
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) mr->mesh = bm;   // show bind pose until skinned
                } else if (field == "blendshapes") {
                    auto* bs = go->AddComponent<BlendShapes>();
                    bs->shapes.clear(); bs->base = Mesh{};
                    long nshape = 0; in >> nshape;
                    for (long i = 0; i < nshape && in; ++i) {
                        BlendShapes::Shape sh;
                        sh.name = ReadQuoted(in);
                        long cnt = 0; int hasN = 0;
                        in >> sh.weight >> cnt >> hasN;
                        for (long k = 0; k < cnt && in; ++k) {
                            int v = 0; Vec3 d; in >> v >> d.x >> d.y >> d.z;
                            sh.index.push_back(v); sh.dPos.push_back(d);
                            if (hasN) { Vec3 nn; in >> nn.x >> nn.y >> nn.z; sh.dNrm.push_back(nn); }
                        }
                        bs->shapes.push_back(std::move(sh));
                    }
                    bs->Apply(true);
                } else if (field == "skinx") {
                    int m = 0; in >> m;
                    if (auto* sm = LastOf(go->GetComponents<SkinnedMesh>())) sm->method = m;
                } else if (field == "modelanim") {
                    auto* ma = go->AddComponent<ModelAnimator>();
                    int autop = 1, lp = 1; long clipCount = 0;
                    in >> ma->active >> autop >> ma->speed >> lp >> clipCount;
                    if (!in || !countOk(clipCount, 2)) return bad("modelanim: bad clip count");
                    ma->autoPlay = (autop != 0); ma->loop = (lp != 0);
                    ma->clips.clear();
                    for (long c = 0; c < clipCount && in; ++c) {
                        ModelAnimator::Clip clip;
                        clip.name = ReadQuoted(in);
                        long nodeCnt = 0; in >> nodeCnt;
                        if (!in || !countOk(nodeCnt, 2)) return bad("modelanim: bad node count");
                        for (long n = 0; n < nodeCnt && in; ++n) {
                            ModelAnimator::NodeClip nc;
                            nc.node = ReadQuoted(in);
                            nc.clip.smoothKeys = false;   // linear unless modelanimcubic says otherwise
                            long trackCnt = 0; in >> trackCnt;
                            if (!in || !countOk(trackCnt, 2)) return bad("modelanim: bad track count");
                            for (long tk = 0; tk < trackCnt && in; ++tk) {
                                std::string trackName = ReadQuoted(in);
                                long keyCnt = 0; in >> keyCnt;
                                if (!in || !countOk(keyCnt, 2)) return bad("modelanim: bad key count");
                                for (long k = 0; k < keyCnt && in; ++k) {
                                    float tm = 0, val = 0; in >> tm >> val;
                                    nc.clip.AddKey(trackName, tm, val);
                                }
                            }
                            clip.nodes.push_back(std::move(nc));
                        }
                        ma->clips.push_back(std::move(clip));
                    }
                } else if (field == "modelanimdrive") {
                    auto* ma = LastOf(go->GetComponents<ModelAnimator>());
                    if (!ma) ma = go->AddComponent<ModelAnimator>();
                    int dm = 0; in >> dm >> ma->walkThreshold >> ma->runThreshold;
                    ma->driveByMovement = (dm != 0);
                    ma->idleClip = ReadQuoted(in);
                    ma->walkClip = ReadQuoted(in);
                    ma->runClip  = ReadQuoted(in);
                    // Optional trailing fields (newer files): in-place playback.
                    { std::string rest; std::getline(in, rest);
                      std::istringstream rs(rest); int ip;
                      if (rs >> ip) ma->inPlace = (ip != 0); }
                } else if (field == "modelanimair") {
                    auto* ma = LastOf(go->GetComponents<ModelAnimator>());
                    if (!ma) ma = go->AddComponent<ModelAnimator>();
                    ma->jumpClip = ReadQuoted(in);
                    ma->fallClip = ReadQuoted(in);
                    ma->landClip = ReadQuoted(in);
                    in >> ma->airUpVel >> ma->airDownVel;
                } else if (field == "modelanimstance") {
                    auto* ma = LastOf(go->GetComponents<ModelAnimator>());
                    if (!ma) ma = go->AddComponent<ModelAnimator>();
                    ma->crouchClip     = ReadQuoted(in);
                    ma->crouchWalkClip = ReadQuoted(in);
                    ma->proneClip      = ReadQuoted(in);
                    ma->crawlClip      = ReadQuoted(in);
                    ma->getUpClip      = ReadQuoted(in);
                } else if (field == "modelanimdir") {
                    auto* ma = LastOf(go->GetComponents<ModelAnimator>());
                    if (!ma) ma = go->AddComponent<ModelAnimator>();
                    ma->strafeLeftClip  = ReadQuoted(in);
                    ma->strafeRightClip = ReadQuoted(in);
                    ma->walkBackClip    = ReadQuoted(in);
                    ma->runBackClip     = ReadQuoted(in);
                } else if (field == "modelanimlegs") {
                    auto* ma = LastOf(go->GetComponents<ModelAnimator>());
                    if (!ma) ma = go->AddComponent<ModelAnimator>();
                    int lf = 0; in >> lf;
                    ma->legsFollowTravel = (lf != 0);
                } else if (field == "modelanimblend") {
                    auto* ma = LastOf(go->GetComponents<ModelAnimator>());
                    if (!ma) ma = go->AddComponent<ModelAnimator>();
                    in >> ma->blendTime;
                } else if (field == "modelanimroot") {
                    auto* ma = LastOf(go->GetComponents<ModelAnimator>());
                    if (!ma) ma = go->AddComponent<ModelAnimator>();
                    int rm = 0; in >> rm;
                    ma->rootMotion = (rm != 0);
                    ma->rootMotionNode = ReadQuoted(in);
                    // Optional trailing field (newer files): vertical root motion.
                    { std::string rest; std::getline(in, rest);
                      std::istringstream rs(rest); int ry, rr, at;
                      if (rs >> ry) ma->rootMotionY = (ry != 0);
                      if (rs >> rr) ma->rootMotionRotation = (rr != 0);
                      if (rs >> at) ma->rootMotionApplyTo = at; }
                } else if (field == "modelanimsmooth") {
                    auto* ma = LastOf(go->GetComponents<ModelAnimator>());
                    if (!ma) ma = go->AddComponent<ModelAnimator>();
                    int sm = 0; in >> sm;
                    ma->smoothLocomotion = (sm != 0);
                } else if (field == "modelanimstride") {
                    auto* ma = LastOf(go->GetComponents<ModelAnimator>());
                    if (!ma) ma = go->AddComponent<ModelAnimator>();
                    int sm = 1, bf = 1; in >> sm >> bf;
                    ma->strideMatch = (sm != 0);
                    ma->builtinFallback = (bf != 0);
                    in >> ma->walkClipSpeed >> ma->runClipSpeed >> ma->crouchWalkClipSpeed >> ma->crawlClipSpeed;
                } else if (field == "modelanimcubic") {
                    auto* ma = LastOf(go->GetComponents<ModelAnimator>());
                    long cnt = 0; in >> cnt;
                    if (!in || !countOk(cnt, 1)) return bad("modelanimcubic: bad clip count");
                    for (long k = 0; k < cnt && in; ++k) {
                        long ci = -1; in >> ci;
                        if (!ma || ci < 0 || ci >= (long)ma->clips.size()) continue;
                        for (auto& nc : ma->clips[ci].nodes) { nc.clip.smoothKeys = true; nc.clip.SetSmooth("", true); }
                    }
                } else if (field == "modelanimevents") {
                    auto* ma = LastOf(go->GetComponents<ModelAnimator>());
                    if (!ma) ma = go->AddComponent<ModelAnimator>();
                    long ci = -1, cnt = 0; in >> ci >> cnt;
                    for (long k = 0; k < cnt && in; ++k) {
                        ModelAnimator::ClipEvent ev;
                        in >> ev.time; ev.name = ReadQuoted(in);
                        if (ci >= 0 && ci < (long)ma->clips.size())
                            ma->clips[ci].events.push_back(std::move(ev));
                    }
                } else if (field == "animsmlayerx") {
                    auto* sm2 = smFor();
                    int add = 0; long n = 0;
                    in >> add >> n;
                    if (sm2) sm2->additive = add != 0;
                    for (long i = 0; i < n && in; ++i) {
                        std::string b = ReadQuoted(in);
                        if (sm2 && !b.empty()) sm2->maskBones.push_back(std::move(b));
                    }
                } else if (field == "animsmlayer") {
                    curSM = go->AddComponent<AnimStateMachine>();
                    in >> curSM->layer >> curSM->layerWeight;
                    curSM->maskBone = ReadQuoted(in);
                } else if (field == "animsm2d") {
                    auto* sm2 = smFor();
                    long n = 0; in >> n;
                    for (long i = 0; i < n && in; ++i) {
                        AnimStateMachine::State dummy;
                        AnimStateMachine::State& st = i < (long)sm2->states.size() ? sm2->states[i] : dummy;
                        long nm = 0;
                        in >> st.blendDims;
                        st.blendParamY = ReadQuoted(in);
                        st.speedParam = ReadQuoted(in);
                        in >> nm;
                        for (long k = 0; k < nm && in; ++k) {
                            float y = 0; in >> y;
                            if (k < (long)st.motions.size()) st.motions[k].posY = y;
                        }
                    }
                } else if (field == "animsm") {
                    auto* sm2 = smFor();
                    sm2->entry = ReadQuoted(in);
                    long stCnt = 0; in >> stCnt;
                    sm2->states.clear();
                    for (long si = 0; si < stCnt && in; ++si) {
                        AnimStateMachine::State st;
                        st.name = ReadQuoted(in);
                        st.clip = ReadQuoted(in);
                        int lp = 1; long trCnt = 0;
                        in >> st.speed >> lp >> trCnt;
                        st.loop = (lp != 0);
                        for (long ti = 0; ti < trCnt && in; ++ti) {
                            AnimStateMachine::Transition tr;
                            tr.to = ReadQuoted(in);
                            int cd = 0; in >> cd;
                            tr.cond = (AnimStateMachine::Cond)cd;
                            tr.param = ReadQuoted(in);
                            in >> tr.value >> tr.blend;
                            st.transitions.push_back(std::move(tr));
                        }
                        sm2->states.push_back(std::move(st));
                    }
                } else if (field == "animsmx" || field == "animsmany") {
                    auto* sm2 = smFor();
                    auto readExt = [&](AnimStateMachine::Transition& tr) {
                        int he = 0, ci = 0; long nx = 0;
                        in >> he >> tr.exitTime >> ci >> nx;
                        tr.hasExitTime = he != 0; tr.canInterruptSelf = ci != 0;
                        for (long k = 0; k < nx && in; ++k) {
                            AnimStateMachine::Condition c; int cd = 0;
                            in >> cd; c.cond = (AnimStateMachine::Cond)cd;
                            c.param = ReadQuoted(in); in >> c.value;
                            tr.extra.push_back(std::move(c));
                        }
                    };
                    long n = 0; in >> n;
                    if (field == "animsmany") {
                        sm2->anyState.clear();
                        for (long i = 0; i < n && in; ++i) {
                            AnimStateMachine::Transition tr;
                            tr.to = ReadQuoted(in);
                            int cd = 0; in >> cd; tr.cond = (AnimStateMachine::Cond)cd;
                            tr.param = ReadQuoted(in);
                            in >> tr.value >> tr.blend;
                            readExt(tr);
                            sm2->anyState.push_back(std::move(tr));
                        }
                    } else {
                        for (long i = 0; i < n && in; ++i) {
                            AnimStateMachine::State dummy;
                            AnimStateMachine::State& st = i < (long)sm2->states.size() ? sm2->states[i] : dummy;
                            int bt = 0; long nm = 0;
                            in >> bt; st.blendTree = bt != 0;
                            st.blendParam = ReadQuoted(in);
                            in >> nm;
                            st.motions.clear();
                            for (long k = 0; k < nm && in; ++k) {
                                AnimStateMachine::Motion m; m.clip = ReadQuoted(in); in >> m.threshold;
                                st.motions.push_back(std::move(m));
                            }
                            long nt = 0; in >> nt;
                            for (long k = 0; k < nt && in; ++k) {
                                AnimStateMachine::Transition tmp;
                                readExt(k < (long)st.transitions.size() ? st.transitions[k] : tmp);
                            }
                        }
                    }
                } else if (field == "animsmmute") {
                    auto* sm2 = smFor();
                    long n = 0; in >> n;
                    for (long i = 0; i < n && in; ++i) {
                        long nt = 0; in >> nt;
                        for (long k = 0; k < nt && in; ++k) {
                            int m = 0; in >> m;
                            if (i < (long)sm2->states.size() && k < (long)sm2->states[i].transitions.size()) sm2->states[i].transitions[k].muted = m != 0;
                        }
                    }
                    long na = 0; in >> na;
                    for (long k = 0; k < na && in; ++k) { int m = 0; in >> m; if (k < (long)sm2->anyState.size()) sm2->anyState[k].muted = m != 0; }
                } else if (field == "animsmsync") {
                    auto* sm2 = smFor();
                    long n = 0; in >> n;
                    for (long i = 0; i < n && in; ++i) {
                        long nt = 0; in >> nt;
                        for (long k = 0; k < nt && in; ++k) {
                            int m = 0; in >> m;
                            if (i < (long)sm2->states.size() && k < (long)sm2->states[i].transitions.size()) sm2->states[i].transitions[k].syncPhase = m != 0;
                        }
                    }
                    long na = 0; in >> na;
                    for (long k = 0; k < na && in; ++k) { int m = 0; in >> m; if (k < (long)sm2->anyState.size()) sm2->anyState[k].syncPhase = m != 0; }
                } else { fieldDone = false; }
                if (!fieldDone) { fieldDone = true;
                if (field == "animsmstance") {
                    StanceTimes& t = smFor()->stanceTimes;
                    in >> t.standCrouch >> t.crouchProne >> t.proneStand;
                } else if (field == "animsmxfade") {
                    GaitCrossfades& t = smFor()->crossfades;
                    in >> t.idleWalk >> t.walkRun >> t.groundAir >> t.airGround;
                } else if (field == "animsmmir") {
                    auto* sm2 = smFor();
                    long n = 0; in >> n;
                    for (long i = 0; i < n && in; ++i) {
                        int mir = 0, rm = -1; in >> mir >> rm;
                        if (sm2 && i < (long)sm2->states.size()) {
                            sm2->states[i].mirror = mir != 0;
                            sm2->states[i].rootMotion = rm;
                        }
                    }
                } else if (field == "animsmmatch") {
                    auto* sm2 = smFor();
                    long n = 0; in >> n;
                    for (long i = 0; i < n && in; ++i) {
                        int f = 0; in >> f;
                        if (i < (long)sm2->states.size()) sm2->states[i].matchSpeed = f != 0;
                    }
                } else if (field == "animsmactions") {
                    int rev = 0; in >> rev;
                    smFor()->actionsRev = rev;
                } else if (field == "animsmnoint") {
                    auto* sm2 = smFor();
                    long n = 0; in >> n;
                    for (long i = 0; i < n && in; ++i) {
                        int f = 1; in >> f;
                        if (i < (long)sm2->states.size()) sm2->states[i].interruptible = f != 0;
                    }
                } else if (field == "animsmanyint") {
                    auto* sm2 = smFor();
                    long n = 0; in >> n;
                    for (long k = 0; k < n && in; ++k) {
                        int m = 0; in >> m;
                        if (m < 0 || m > 4) m = 0;
                        if (k < (long)sm2->anyState.size())
                            sm2->anyState[k].interruption = (AnimStateMachine::Transition::Interrupt)m;
                    }
                } else if (field == "animsmint") {
                    auto* sm2 = smFor();
                    long n = 0; in >> n;
                    for (long i = 0; i < n && in; ++i) {
                        long nt = 0; in >> nt;
                        for (long k = 0; k < nt && in; ++k) {
                            int m = 0; in >> m;
                            if (m < 0 || m > 4) m = 0;
                            if (i < (long)sm2->states.size() && k < (long)sm2->states[i].transitions.size())
                                sm2->states[i].transitions[k].interruption = (AnimStateMachine::Transition::Interrupt)m;
                        }
                    }
                } else if (field == "animsmev") {
                    auto* sm2 = smFor();
                    long n = 0; in >> n;
                    for (long i = 0; i < n && in; ++i) {
                        AnimStateMachine::State dummy;
                        AnimStateMachine::State& st = i < (long)sm2->states.size() ? sm2->states[i] : dummy;
                        st.enterEvent = ReadQuoted(in);
                        st.exitEvent = ReadQuoted(in);
                        long nt = 0; in >> st.ikWeight >> nt;
                        for (long k = 0; k < nt && in; ++k) {
                            float o = 0; in >> o;
                            if (k < (long)st.transitions.size()) st.transitions[k].offset = o;
                        }
                    }
                    long na = 0; in >> na;
                    for (long k = 0; k < na && in; ++k) {
                        float o = 0; in >> o;
                        if (k < (long)sm2->anyState.size()) sm2->anyState[k].offset = o;
                    }
                } else if (field == "animsmfb") {
                    auto* sm2 = smFor();
                    long n = 0; in >> n;
                    for (long i = 0; i < n && in; ++i) {
                        float w = -1.0f; in >> w;
                        if (i < (long)sm2->states.size()) sm2->states[i].fullBodyIkWeight = w;
                    }
                } else if (field == "animsmlk") {
                    auto* sm2 = smFor();
                    long n = 0; in >> n;
                    for (long i = 0; i < n && in; ++i) {
                        float w = -1.0f; in >> w;
                        if (i < (long)sm2->states.size()) sm2->states[i].lookWeight = w;
                    }
                } else if (field == "animsmfall") {
                    auto* sm2 = smFor();
                    long n = 0; in >> n;
                    for (long i = 0; i < n && in; ++i) {
                        std::string fb = ReadQuoted(in);
                        if (sm2 && i < (long)sm2->states.size()) sm2->states[i].fallbackClip = std::move(fb);
                    }
                } else if (field == "animsmikc") {
                    auto* sm2 = smFor();
                    long n = 0; in >> n;
                    for (long i = 0; i < n && in; ++i) {
                        long nk = 0; in >> nk;
                        std::vector<AnimStateMachine::State::IkKey> foot, body;
                        for (long k = 0; k < nk && in; ++k) {
                            AnimStateMachine::State::IkKey key; in >> key.t >> key.w;
                            foot.push_back(key);
                        }
                        long nb = 0; in >> nb;
                        for (long k = 0; k < nb && in; ++k) {
                            AnimStateMachine::State::IkKey key; in >> key.t >> key.w;
                            body.push_back(key);
                        }
                        if (sm2 && i < (long)sm2->states.size()) {
                            sm2->states[i].ikCurve = std::move(foot);
                            sm2->states[i].fullBodyIkCurve = std::move(body);
                        }
                    }
                } else if (field == "animsmparams") {
                    auto* sm2 = smFor();
                    long n = 0; in >> n;
                    for (long i = 0; i < n && in; ++i) {
                        AnimStateMachine::Param p;
                        p.name = ReadQuoted(in); in >> p.type;
                        if (sm2) sm2->params.push_back(std::move(p));
                    }
                } else if (field == "animsmpdef") {
                    // Parameter defaults (index-matched to animsmparams).
                    auto* sm2 = smFor();
                    long n = 0; in >> n;
                    for (long i = 0; i < n && in; ++i) {
                        float d = 0.0f; in >> d;
                        if (sm2 && i < (long)sm2->params.size()) sm2->params[i].def = d;
                    }
                } else if (field == "animsmlname") {
                    auto* sm2 = smFor();
                    std::string nm = ReadQuoted(in);
                    if (sm2) sm2->layerName = std::move(nm);
                } else if (field == "animsmanypos") {
                    auto* sm2 = smFor();
                    float x = 0, y = 0; in >> x >> y;
                    if (sm2) { sm2->anyX = x; sm2->anyY = y; }
                } else if (field == "animsmpos") {
                    // Animator graph node positions (index-matched to animsm's states).
                    auto* sm2 = smFor();
                    long n = 0; in >> n;
                    for (long i = 0; i < n && in; ++i) {
                        float x = 0, y = 0; in >> x >> y;
                        if (sm2 && i < (long)sm2->states.size()) { sm2->states[i].nx = x; sm2->states[i].ny = y; }
                    }
                } else if (field == "material") {
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) {
                        Color e; float spec = 0, shin = 16; int unlit = 0;
                        in >> e.r >> e.g >> e.b >> spec >> shin >> unlit;
                        mr->emissive = e; mr->specular = spec; mr->shininess = shin;
                        mr->unlit = (unlit != 0);
                        in >> std::ws; // optional texture path (quoted)
                        if (in.peek() == '"') mr->texture = ReadQuoted(in);
                        in >> std::ws; // optional texture tiling (u v)
                        int p = in.peek();
                        if (std::isdigit(p) || p == '-' || p == '.')
                            in >> mr->tiling.x >> mr->tiling.y;
                    }
                } else if (field == "texopts") {
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) {
                        int fl = 0; in >> fl >> mr->texOffset.x >> mr->texOffset.y;
                        mr->texFilter = (MeshRenderer::TexFilter)fl;
                    }
                } else if (field == "normalmap") {
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) {
                        in >> std::ws;
                        if (in.peek() == '"') mr->normalMap = ReadQuoted(in);
                        in >> std::ws;
                        int p = in.peek();
                        if (std::isdigit(p) || p == '-' || p == '.') in >> mr->normalStrength;
                    }
                } else if (field == "reflect") {
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) {
                        in >> mr->reflectivity;
                        in >> std::ws;                       // optional metalness
                        int p = in.peek();
                        if (std::isdigit(p) || p == '-' || p == '.') in >> mr->metallic;
                    }
                } else if (field == "specmap") {
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) {
                        in >> std::ws;
                        if (in.peek() == '"') mr->specularMap = ReadQuoted(in);
                    }
                } else if (field == "aomap") {
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) {
                        in >> std::ws;
                        if (in.peek() == '"') mr->aoMap = ReadQuoted(in);
                        in >> std::ws;
                        int p = in.peek();   // optional trailing strength
                        if (std::isdigit(p) || p == '-' || p == '.') in >> mr->aoStrength;
                    }
                } else if (field == "mrmap") {
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) {
                        in >> std::ws;
                        if (in.peek() == '"') mr->metallicRoughnessMap = ReadQuoted(in);
                    }
                } else if (field == "emissivemap") {
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) {
                        in >> std::ws;
                        if (in.peek() == '"') mr->emissiveMap = ReadQuoted(in);
                    }
                } else if (field == "shader") {
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) {
                        int s = 0; in >> s >> mr->toonBands;
                        mr->shader = (MeshRenderer::Shader)s;
                        in >> std::ws;   // optional gradient colours (appended later)
                        int gpk = in.peek();
                        if (std::isdigit(gpk) || gpk == '-' || gpk == '.') {
                            in >> mr->gradientTop.r >> mr->gradientTop.g >> mr->gradientTop.b
                               >> mr->gradientBottom.r >> mr->gradientBottom.g >> mr->gradientBottom.b;
                        }
                    }
                } else if (field == "customshader") {
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) {
                        in >> std::ws;
                        std::string p = in.peek() == '"' ? ReadQuoted(in) : std::string();
                        // A path written next to the scene ("lava.okayshader") is found in the scene's folder.
                        if (!p.empty()) {
                            const std::filesystem::path pp(p);
                            std::error_code ec;
                            if (!pp.is_absolute() && !g_loadingSceneDir.empty() && !std::filesystem::exists(pp, ec)) {
                                const std::filesystem::path alt = std::filesystem::path(g_loadingSceneDir) / pp;
                                if (std::filesystem::exists(alt, ec)) p = alt.string();
                            }
                        }
                        mr->customShader = p;
                        in >> std::ws;
                        if (in.peek() == '"') mr->shaderParams = ReadQuoted(in);
                    }
                } else if (field == "rim") {
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>()))
                        in >> mr->rimStrength >> mr->rimPower
                           >> mr->rimColor.r >> mr->rimColor.g >> mr->rimColor.b;
                } else if (field == "outline") {
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) {
                        in >> mr->outlineWidth >> mr->outlineColor.r >> mr->outlineColor.g >> mr->outlineColor.b;
                        mr->outline = true;
                    }
                } else if (field == "uvanim") {
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) {
                        int tp = 0; in >> mr->uvScroll.x >> mr->uvScroll.y >> tp;
                        mr->triplanar = (tp != 0);
                    }
                } else if (field == "modstack") {
                    auto* mm = go->AddComponent<MeshModifiers>();
                    KvIn k(in);
                    k.b("on", mm->enabled);
                } else if (field == "mod") {
                    auto* mm = LastOf(go->GetComponents<MeshModifiers>());
                    if (!mm) mm = go->AddComponent<MeshModifiers>();
                    KvIn k(in);
                    MeshModifier m;
                    int type = 0; k.i("type", type);
                    if (type < 0 || type >= (int)MeshModifier::Type::Count) type = 0;
                    m.type = (MeshModifier::Type)type;
                    k.b("on", m.enabled); k.b("edit", m.showInEditMode);
                    k.b("mx", m.mirrorX); k.b("my", m.mirrorY); k.b("mz", m.mirrorZ); k.b("merge", m.mirrorMerge);
                    k.i("n", m.count); k.b("rel", m.relative);
                    k.f("ox", m.offset.x); k.f("oy", m.offset.y); k.f("oz", m.offset.z);
                    k.i("lv", m.levels); k.b("smooth", m.smoothShading); k.f("th", m.thickness);
                    k.f("w", m.width); k.i("seg", m.segments); k.f("ang", m.angle);
                    k.str("tgt", m.target); k.i("op", m.boolOp); k.f("vox", m.voxel); k.f("cell", m.cellSize);
                    k.f("str", m.strength); k.f("freq", m.frequency); k.i("seed", m.seed); k.b("bw", m.useBevelWeights);
                    if (m.type == MeshModifier::Type::CurveArray) {
                        k.b("align", m.align);
                        std::string pts; k.str("path", pts);
                        std::stringstream ps(pts); std::string one;
                        while (std::getline(ps, one, ';') && m.path.size() < 1000) {
                            Vec3 v; char c1 = 0, c2 = 0;
                            std::istringstream vs(one);
                            if (vs >> v.x >> c1 >> v.y >> c2 >> v.z && c1 == ',' && c2 == ',') m.path.push_back(v);
                        }
                    }
                    mm->modifiers.push_back(m);
                } else if (field == "meshenabled") {
                    int en = 1; in >> en;
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) mr->enabled = (en != 0);
                } else if (field == "meshshadows") {
                    int cs = 1, rs = 1; in >> cs >> rs;
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) { mr->castShadows = (cs != 0); mr->receiveShadows = (rs != 0); }
                } else if (field == "meshglow") {
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) in >> mr->glowLightRange;
                } else if (field == "lightx") {
                    if (auto* li = LastOf(go->GetComponents<Light>())) {
                        int unit = 0;
                        in >> li->shadowSoftness >> li->areaWidth >> li->areaHeight >> li->cookieScale >> unit;
                        li->unit = unit == 1 ? Light::Unit::RealWorld : Light::Unit::Simple;
                        in >> std::ws;
                        if (in.peek() == '"') li->cookie = ReadQuoted(in);
                    }
                } else if (field == "groundshadow") {
                    if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) {
                        int gs = 1; in >> gs >> mr->groundShadowY >> mr->groundShadowStrength;
                        mr->groundShadow = (gs != 0);
                    }
                } else if (field == "light") {
                    auto* li = go->AddComponent<Light>();
                    Color c;
                    in >> c.r >> c.g >> c.b >> c.a >> li->ambient >> li->intensity;
                    li->color = c;
                    in >> std::ws; // optional type + range + spot angle (added later)
                    if (std::isdigit(in.peek())) {
                        int ty = 0; in >> ty >> li->range >> li->spotAngle;
                        li->type = (Light::Type)ty;
                    }
                    in >> std::ws; // optional softness + temperature + ambient tint
                    if (std::isdigit(in.peek())) {
                        int useT = 0;
                        in >> li->spotSoftness >> useT >> li->temperature
                           >> li->ambientColor.r >> li->ambientColor.g >> li->ambientColor.b;
                        li->useTemperature = (useT != 0);
                        in >> std::ws; // optional falloff mode (added later)
                        if (std::isdigit(in.peek())) { int fo = 0; in >> fo; li->falloff = (Light::Falloff)fo; }
                        in >> std::ws; // optional shadows + flicker + enabled (added later)
                        if (std::isdigit(in.peek())) {
                            int cs = 1, en = 1;
                            in >> cs >> li->shadowStrength >> li->shadowBias >> li->flicker
                               >> li->flickerSpeed >> en;
                            li->castShadows = (cs != 0);
                            li->enabled = (en != 0);
                        }
                    }
                } else if (field == "rigidbody2d") {
                    int bt = 0; float gs = 1, mass = 1, drag = 0, bounce = 0;
                    in >> bt >> gs >> mass >> drag >> bounce;
                    auto* rb = go->AddComponent<Rigidbody2D>();
                    rb->bodyType = (Rigidbody2D::BodyType)bt;
                    rb->gravityScale = gs; rb->mass = mass; rb->drag = drag; rb->bounciness = bounce;
                    in >> std::ws; if (std::isdigit(in.peek()) || in.peek() == '-') in >> rb->friction;  // trailing
                    in >> std::ws; if (std::isdigit(in.peek()) || in.peek() == '-') in >> rb->angularDrag;
                    in >> std::ws; if (std::isdigit(in.peek())) { int fr = 1; in >> fr; rb->freezeRotation = (fr != 0); }
                    in >> std::ws; if (std::isdigit(in.peek())) { int as = 1; in >> as; rb->allowSleep = (as != 0); }
                    in >> std::ws; if (std::isdigit(in.peek())) { int cc = 0; in >> cc; rb->continuousCollision = (cc != 0); }
                    in >> std::ws; if (std::isdigit(in.peek()) || in.peek() == '-') in >> rb->rollingResistance;   // trailing
                } else if (field == "physmat2d" || field == "physmat3d") {
                    float fr = -1.0f, bn = -1.0f; int fm = 0, bm = 0, index = -1;
                    in >> fr >> bn >> fm >> bm;
                    in >> std::ws; if (std::isdigit(static_cast<unsigned char>(in.peek()))) in >> index;
                    auto clampMode = [](int m) { return (CombineMode)(m < 0 || m > 4 ? 0 : m); };
                    if (field == "physmat2d") {
                        auto all = LegacyColliders2D(*go);
                        for (int i = 0; i < (int)all.size(); ++i)
                            if (index < 0 || i == index) { all[(std::size_t)i]->friction = fr; all[(std::size_t)i]->bounciness = bn; all[(std::size_t)i]->frictionCombine = clampMode(fm); all[(std::size_t)i]->bounceCombine = clampMode(bm); }
                    } else {
                        auto all = LegacyColliders3D(*go);
                        for (int i = 0; i < (int)all.size(); ++i)
                            if (index < 0 || i == index) { all[(std::size_t)i]->friction = fr; all[(std::size_t)i]->bounciness = bn; all[(std::size_t)i]->frictionCombine = clampMode(fm); all[(std::size_t)i]->bounceCombine = clampMode(bm); }
                    }
                } else if (field == "colskin2d") {
                    float off = 0.0f; int index = -1;
                    in >> off;
                    in >> std::ws; if (std::isdigit(static_cast<unsigned char>(in.peek()))) in >> index;
                    auto all = LegacyColliders2D(*go);
                    if (index >= 0 && index < (int)all.size() && std::isfinite(off) && off >= 0.0f) all[(std::size_t)index]->contactOffset = off;
                } else if (field == "colskin3d") {
                    float off = 0.0f; int index = -1;
                    in >> off;
                    in >> std::ws; if (std::isdigit(static_cast<unsigned char>(in.peek()))) in >> index;
                    auto all = LegacyColliders3D(*go);
                    if (index >= 0 && index < (int)all.size() && std::isfinite(off) && off >= 0.0f) all[(std::size_t)index]->contactOffset = off;
                } else { fieldDone = false; }
                }
                if (!fieldDone) { fieldDone = true;
                if (field == "trigfilter2d" || field == "trigfilter3d") {
                    std::string tag = ReadQuoted(in);
                    std::uint32_t layers = 0xFFFFFFFFu;
                    in >> std::ws; if (std::isdigit(static_cast<unsigned char>(in.peek()))) in >> layers;
                    in >> std::ws;
                    int index = -1;   // no index: the one record applies to every collider, as older files did
                    if (std::isdigit(static_cast<unsigned char>(in.peek()))) in >> index;
                    if (field == "trigfilter2d") {
                        auto all = LegacyColliders2D(*go);
                        if (index < 0) { for (Collider2D* c : all) { c->triggerTag = tag; c->triggerLayers = layers; } }
                        else if (index < (int)all.size()) { all[(std::size_t)index]->triggerTag = tag; all[(std::size_t)index]->triggerLayers = layers; }
                    } else {
                        auto all = LegacyColliders3D(*go);
                        if (index < 0) { for (Collider3D* c : all) { c->triggerTag = tag; c->triggerLayers = layers; } }
                        else if (index < (int)all.size()) { all[(std::size_t)index]->triggerTag = tag; all[(std::size_t)index]->triggerLayers = layers; }
                    }
                } else if (field == "boxcollider2d") {
                    Vec2 sz, off; int trig = 0, layer = 0, af = 0;
                    in >> sz.x >> sz.y >> off.x >> off.y >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer; // optional
                    in >> std::ws; if (std::isdigit(in.peek())) in >> af;    // optional autoFit
                    auto* bc = go->AddComponent<BoxCollider2D>();
                    bc->size = sz; bc->offset = off; bc->isTrigger = (trig != 0); bc->layer = layer;
                    bc->autoFit = (af != 0);
                } else if (field == "circlecollider2d") {
                    float r = 0.5f; Vec2 off; int trig = 0, layer = 0, af = 0;
                    in >> r >> off.x >> off.y >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer; // optional
                    in >> std::ws; if (std::isdigit(in.peek())) in >> af;    // optional autoFit
                    auto* cc = go->AddComponent<CircleCollider2D>();
                    cc->radius = r; cc->offset = off; cc->isTrigger = (trig != 0); cc->layer = layer;
                    cc->autoFit = (af != 0);
                } else if (field == "capsulecollider2d") {
                    Vec2 sz{1, 2}, off; int dir = 0, trig = 0, layer = 0, af = 0;
                    in >> sz.x >> sz.y >> dir >> off.x >> off.y >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer; // optional
                    in >> std::ws; if (std::isdigit(in.peek())) in >> af;    // optional autoFit
                    auto* cap = go->AddComponent<CapsuleCollider2D>();
                    cap->size = sz; cap->direction = (CapsuleCollider2D::Direction)dir;
                    cap->offset = off; cap->isTrigger = (trig != 0); cap->layer = layer;
                    cap->autoFit = (af != 0);
                } else if (field == "edgecollider2d") {
                    Vec2 off, n{0, 1}; int trig = 0, layer = 0, ow = 0; std::size_t count = 0;
                    in >> off.x >> off.y >> trig >> layer >> ow >> n.x >> n.y >> count;
                    auto* ec = go->AddComponent<EdgeCollider2D>();
                    ec->offset = off; ec->isTrigger = (trig != 0); ec->layer = layer;
                    ec->oneWay = (ow != 0); ec->oneWayNormal = n;
                    ec->points.clear();
                    for (std::size_t k = 0; k < count && in; ++k) { Vec2 p; in >> p.x >> p.y; ec->points.push_back(p); }
                } else if (field == "polygoncollider2d") {
                    Vec2 off; int trig = 0, layer = 0; std::size_t count = 0;
                    in >> off.x >> off.y >> trig >> layer >> count;
                    auto* pc = go->AddComponent<PolygonCollider2D>();
                    pc->offset = off; pc->isTrigger = (trig != 0); pc->layer = layer;
                    pc->points.clear();
                    for (std::size_t k = 0; k < count && in; ++k) { Vec2 p; in >> p.x >> p.y; pc->points.push_back(p); }
                } else if (field == "rigidbody3d") {
                    int bt = 0; float gs = 1, mass = 1, drag = 0, bounce = 0;
                    int fx = 0, fy = 0, fz = 0;
                    in >> bt >> gs >> mass >> drag >> bounce >> fx >> fy >> fz;
                    auto* rb = go->AddComponent<Rigidbody3D>();
                    rb->bodyType = (Rigidbody3D::BodyType)bt;
                    rb->gravityScale = gs; rb->mass = mass; rb->drag = drag; rb->bounciness = bounce;
                    rb->freezeX = (fx != 0); rb->freezeY = (fy != 0); rb->freezeZ = (fz != 0);
                    in >> std::ws; if (std::isdigit(in.peek()) || in.peek() == '-') in >> rb->maxFallSpeed;
                    in >> std::ws; if (std::isdigit(in.peek()) || in.peek() == '-') in >> rb->friction;  // trailing
                    in >> std::ws; if (std::isdigit(in.peek()) || in.peek() == '-') in >> rb->angularDrag;
                    in >> std::ws; if (std::isdigit(in.peek())) { int fr = 1; in >> fr; rb->freezeRotation = (fr != 0); }
                    in >> std::ws; if (std::isdigit(in.peek())) { int as = 1; in >> as; rb->allowSleep = (as != 0); }
                    in >> std::ws; if (std::isdigit(in.peek())) { int cc = 0; in >> cc; rb->continuousCollision = (cc != 0); }
                    in >> std::ws; if (std::isdigit(in.peek()) || in.peek() == '-') in >> rb->rollingResistance;   // trailing
                    in >> std::ws; if (std::isdigit(in.peek())) { int cch = 0; in >> cch; rb->compoundChildren = (cch != 0); }
                } else if (field == "joint3d") {
                    auto* j = go->AddComponent<Joint3D>();
                    int bk = 0, ac = 1;
                    in >> j->mode;
                    j->connectedBody = ReadQuoted(in);
                    in >> j->anchor.x >> j->anchor.y >> j->anchor.z
                       >> j->distance >> ac >> j->spring >> j->damper >> bk >> j->breakForce;
                    j->autoConfigure = (ac != 0); j->breakable = (bk != 0);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '-') {   // hinge fields (trailing)
                        int um = 0;
                        in >> j->axis.x >> j->axis.y >> j->axis.z >> um >> j->motorSpeed >> j->maxMotorTorque;
                        j->useMotor = (um != 0);
                        in >> std::ws;
                        if (std::isdigit(static_cast<unsigned char>(in.peek()))) {   // angle limits (trailing)
                            int ul = 0;
                            in >> ul >> j->minAngle >> j->maxAngle;
                            j->useLimits = (ul != 0);
                            in >> std::ws;
                            if (std::isdigit(static_cast<unsigned char>(in.peek())) || in.peek() == '-') {   // ball / cone twist (trailing)
                                int ud = 0;
                                in >> j->swingLimit1 >> j->swingLimit2 >> ud >> j->driveTarget.x >> j->driveTarget.y >> j->driveTarget.z
                                   >> j->driveStiffness >> j->driveDamping >> j->driveFrequency >> j->driveDampingRatio >> j->driveMaxTorque;
                                j->useDrive = (ud != 0);
                            }
                        }
                    }
                } else if (field == "jointlink") {   // the partner's guid, written after that joint
                    const std::string g = ReadQuoted(in);
                    auto joints = go->GetComponents<Joint3D>();
                    if (!joints.empty() && joints.back()) { joints.back()->connectedGuid = g; joints.back()->NoteLinkedName(); }
                } else if (field == "jointat") {   // the partner is object N of this same text: it stays the partner in a copy
                    int at = -1;
                    in >> at;
                    const std::size_t count = go->GetComponents<Joint3D>().size();
                    if (at >= 0 && count > 0) S.jointLinks.push_back({go, (int)count - 1, at});
                } else if (field == "joint2d") {
                    auto* j = go->AddComponent<Joint2D>();
                    int bk = 0, ac = 1;
                    in >> j->mode;
                    j->connectedBody = ReadQuoted(in);
                    in >> j->anchor.x >> j->anchor.y
                       >> j->distance >> ac >> j->spring >> j->damper >> bk >> j->breakForce;
                    j->autoConfigure = (ac != 0); j->breakable = (bk != 0);
                    in >> std::ws;
                    if (std::isdigit(in.peek())) {           // hinge fields (trailing)
                        int um = 0, ul = 0;
                        in >> um >> j->motorSpeed >> j->maxMotorTorque >> ul >> j->minAngle >> j->maxAngle;
                        j->useMotor = (um != 0); j->useLimits = (ul != 0);
                    }
                } else if (field == "aimik") {
                    auto* a = go->AddComponent<AimIK>();
                    a->boneName = ReadQuoted(in); a->targetName = ReadQuoted(in);
                    in >> a->aimAxis.x >> a->aimAxis.y >> a->aimAxis.z
                       >> a->upAxis.x >> a->upAxis.y >> a->upAxis.z
                       >> a->weight >> a->maxAngle
                       >> a->target.x >> a->target.y >> a->target.z;
                    // Optional trailing fields (newer files): rest-of-line parse so
                    // older records still load cleanly.
                    { std::string rest; std::getline(in, rest);
                      std::istringstream rs(rest); float sm;
                      if (rs >> sm) a->smoothing = sm; }
                } else if (field == "iktarget") {
                    auto* t = go->AddComponent<IKTarget>();
                    in >> t->limb;
                    t->characterName = ReadQuoted(in);
                    in >> t->weight >> t->blendTime;
                } else if (field == "aimoffset") {
                    auto* ao = go->AddComponent<AimOffset>();
                    int cam = 1;
                    in >> cam;
                    ao->useCamera = cam != 0;
                    ao->targetName = ReadQuoted(in);
                    in >> ao->weight >> ao->pitchClamp >> ao->yawClamp
                       >> ao->spineWeight >> ao->chestWeight >> ao->armWeight
                       >> ao->kick >> ao->recovery;
                } else if (field == "posecap") {
                    auto* pc = go->AddComponent<PoseCapture>();
                    pc->clipName = ReadQuoted(in);
                    in >> pc->recordSeconds;
                } else if (field == "lookatik") {
                    auto* l = go->AddComponent<LookAtIK>();
                    l->targetName = ReadQuoted(in);
                    in >> l->forwardAxis.x >> l->forwardAxis.y >> l->forwardAxis.z
                       >> l->weight >> l->maxAngle
                       >> l->target.x >> l->target.y >> l->target.z;
                    std::size_t n = 0; in >> n;
                    for (std::size_t k = 0; k < n && in; ++k) l->chainNames.push_back(ReadQuoted(in));
                    { std::string rest; std::getline(in, rest);
                      std::istringstream rs(rest); float sm;
                      if (rs >> sm) l->smoothing = sm;
                      // Files from before the clamp looked all the way round.
                      KvIn k(rs);
                      l->clampAngle = 0.0f;
                      k.f("clamp", l->clampAngle); k.f("eyeMax", l->eyeMaxAngle); k.f("eyeW", l->eyeWeight);
                      float point = l->aimAtPoint ? 1.0f : 0.0f;
                      if (k.find("point")) { k.f("point", point); l->aimAtPoint = point != 0.0f; }
                      k.f("maxDist", l->maxDistance); k.f("fadeDist", l->fadeDistance);
                      for (int i = 0; ; ++i) {
                          const std::string key = "sh" + std::to_string(i);
                          float v = 1.0f;
                          if (!k.find(key.c_str())) break;
                          k.f(key.c_str(), v); l->boneWeights.push_back(v);
                      }
                      for (int i = 0; ; ++i) {
                          const std::string key = "eye" + std::to_string(i);
                          std::string nm;
                          if (!k.find(key.c_str())) break;
                          k.str(key.c_str(), nm); l->eyeNames.push_back(nm);
                      } }
                } else if (field == "footik") {
                    auto* f = go->AddComponent<FootIK>();
                    f->leftHipName = ReadQuoted(in); f->leftKneeName = ReadQuoted(in); f->leftFootName = ReadQuoted(in);
                    f->rightHipName = ReadQuoted(in); f->rightKneeName = ReadQuoted(in); f->rightFootName = ReadQuoted(in);
                    f->pelvisName = ReadQuoted(in);
                    int ur = 1, ap = 0, pd = 0, ag = 0;
                    in >> f->weight >> f->footOffset >> ur >> f->groundY >> ap >> pd >> ag
                       >> f->minKneeBend >> f->maxKneeBend;
                    f->useRaycast = (ur != 0); f->adjustPelvis = (ap != 0); f->plantDown = (pd != 0); f->alignToGround = (ag != 0);
                    // Optional trailing fields (newer files): parse from the rest of
                    // the line so older records still load cleanly.
                    { std::string rest; std::getline(in, rest);
                      std::istringstream rs(rest); float sm; int o;
                      if (rs >> sm) f->smoothing = sm;
                      if (rs >> o)  f->orient = (o != 0);
                      KvIn k(rs);
                      k.f("rayUp", f->maxRayUp); k.f("rayDown", f->maxRayDown); k.f("pelvisShift", f->maxPelvisShift);
                      k.f("upX", f->footUpAxis.x); k.f("upY", f->footUpAxis.y); k.f("upZ", f->footUpAxis.z);
                      k.b("lock", f->footLock); k.f("lockSpeed", f->lockSpeed); k.f("lockHeight", f->lockHeight);
                      k.f("lockRelease", f->lockRelease);
                      k.b("probes", f->stepProbes); k.f("toeR", f->toeReach); k.f("heelR", f->heelReach);
                      // The right sole's axis: files from before it leave it at Up,
                      // which reads that foot's own rest pose (a mirrored rig's right
                      // sole isn't the left one's axis).
                      k.f("upRX", f->footUpAxisR.x); k.f("upRY", f->footUpAxisR.y); k.f("upRZ", f->footUpAxisR.z);
                      k.u("mask", f->groundMask);
                      // Files from before orient's migration loaded a Character's
                      // part rig with it off; Start() turns it on there.
                      if (!k.find("om") && !f->orient) f->orientMigrate = true;
                      ReadLimitKv(k, "hl", f->hipLimit); ReadLimitKv(k, "kl", f->kneeLimit); }
                } else if (field == "limbik") {
                    auto* lb = go->AddComponent<LimbIK>();
                    lb->targetBlendTime = 0.0f;   // files from before it snapped to a new goal
                    lb->upperName = ReadQuoted(in); lb->lowerName = ReadQuoted(in); lb->endName = ReadQuoted(in);
                    lb->targetName = ReadQuoted(in); lb->poleName = ReadQuoted(in);
                    int mr = 0;
                    in >> lb->weight >> lb->minBend >> lb->maxBend >> mr
                       >> lb->target.x >> lb->target.y >> lb->target.z
                       >> lb->pole.x >> lb->pole.y >> lb->pole.z;
                    lb->matchTargetRotation = (mr != 0);
                    // Optional trailing fields (newer files): orient bones toggle, pole mode,
                    // softness, target rotation. Files from before the pole mode bent toward
                    // the world direction.
                    { std::string rest; std::getline(in, rest);
                      std::istringstream rs(rest); int o, pm;
                      if (rs >> o) lb->orient = (o != 0);
                      lb->poleMode = (rs >> pm) ? (LimbIK::PoleMode)std::clamp(pm, 0, 2) : LimbIK::PoleMode::World;
                      Quat q;
                      if (rs >> lb->softness >> q.x >> q.y >> q.z >> q.w) lb->targetRotation = q;
                      // Newer still: joint limits as trailing key=value.
                      std::string tok;
                      while (rs >> tok) {
                          auto eq = tok.find('=');
                          if (eq == std::string::npos) continue;
                          const std::string k = tok.substr(0, eq);
                          const float v = std::strtof(tok.c_str() + eq + 1, nullptr);
                          if (k == "tb") { lb->targetBlendTime = v; continue; }
                          if (k == "st") { lb->stretch = v; continue; }
                          IKJointLimit& l = k.compare(0, 2, "ul") == 0 ? lb->upperLimit : lb->lowerLimit;
                          const std::string f = k.substr(2);
                          if      (f == "T")  l.type = (IKJointLimit::Type)std::clamp((int)v, 0, 2);
                          else if (f == "S")  l.swingMax = v;
                          else if (f == "H0") l.hingeMin = v;
                          else if (f == "H1") l.hingeMax = v;
                          else if (f == "W0") l.twistMin = v;
                          else if (f == "W1") l.twistMax = v;
                          else if (f == "TX") { l.twistAxis.x = v; l.autoAxes = false; }
                          else if (f == "TY") l.twistAxis.y = v;
                          else if (f == "TZ") l.twistAxis.z = v;
                          else if (f == "HX") l.hingeAxis.x = v;
                          else if (f == "HY") l.hingeAxis.y = v;
                          else if (f == "HZ") l.hingeAxis.z = v;
                      } }
                } else if (field == "chainik") {
                    auto* ci = go->AddComponent<ChainIK>();
                    ci->targetBlendTime = 0.0f;   // files from before it snapped to a new goal
                    ci->targetName = ReadQuoted(in);
                    int orient = 0;
                    in >> ci->weight >> ci->iterations >> ci->solver >> orient
                       >> ci->target.x >> ci->target.y >> ci->target.z;
                    ci->orient = (orient != 0);
                    std::size_t bn = 0; in >> bn;
                    for (std::size_t k = 0; k < bn && in; ++k) ci->boneNames.push_back(ReadQuoted(in));
                } else if (field == "chainx2") {
                    // Newer ChainIK data: pole mode/direction + per-joint limits.
                    if (auto* ci = LastOf(go->GetComponents<ChainIK>())) {
                        long long nl = 0;
                        in >> ci->poleMode >> ci->poleDir.x >> ci->poleDir.y >> ci->poleDir.z >> nl;
                        if (!in || !countOk(nl, 7)) return bad("chainx2: bad limit count");
                        ci->limits.assign((std::size_t)nl, {});
                        for (std::size_t k = 0; k < (std::size_t)nl && in; ++k) {
                            IKJointLimit& l = ci->limits[k];
                            int ty = 0, aa = 1;
                            in >> ty >> l.swingMax >> l.hingeMin >> l.hingeMax >> l.twistMin >> l.twistMax >> aa;
                            l.type = (IKJointLimit::Type)std::clamp(ty, 0, 2);
                            l.autoAxes = aa != 0;
                            if (!l.autoAxes)
                                in >> l.twistAxis.x >> l.twistAxis.y >> l.twistAxis.z
                                   >> l.hingeAxis.x >> l.hingeAxis.y >> l.hingeAxis.z;
                        }
                        KvIn k(in);
                        k.f("tb", ci->targetBlendTime);
                    }
                } else if (field == "fbik") {
                    auto* fb = go->AddComponent<FullBodyIK>();
                    for (auto& e : fb->effectors) e.targetBlendTime = 0.0f;   // files from before it snapped to a new goal
                    int act = 1, am = 1, le = 0;
                    in >> fb->weight >> act >> fb->fadeTime >> fb->softness >> fb->bodyPull >> fb->maxBodyOffset
                       >> fb->spineReach >> fb->maxSpineAngle >> am >> le;
                    fb->ikActive = act != 0; fb->autoMap = am != 0; fb->lookEnabled = le != 0;
                    fb->lookTargetName = ReadQuoted(in);
                    in >> fb->lookPosition.x >> fb->lookPosition.y >> fb->lookPosition.z >> fb->lookWeight >> fb->lookHeadShare
                       >> fb->maxLookAngle >> fb->headForward.x >> fb->headForward.y >> fb->headForward.z;
                    int nb = 0; in >> nb;
                    for (int i = 0; i < nb && in; ++i) { std::string n = ReadQuoted(in); if (i < FullBodyIK::BoneCount) fb->boneNames[i] = n; }
                    int ne = 0; in >> ne;
                    for (int i = 0; i < ne && in; ++i) {
                        FullBodyIK::Effector tmp;
                        FullBodyIK::Effector& e = i < FullBodyIK::LimbCount ? fb->effectors[i] : tmp;
                        int en = 0; in >> en; e.enabled = en != 0;
                        e.targetName = ReadQuoted(in);
                        in >> e.position.x >> e.position.y >> e.position.z >> e.positionWeight >> e.rotationWeight;
                        e.hintName = ReadQuoted(in);
                        in >> e.pull;
                    }
                } else if (field == "holdable") {
                    auto* h = go->AddComponent<HoldableObject>();
                    KvIn k(in);
                    int st = 0; k.i("style", st);
                    h->style = st >= 0 && st <= (int)HoldableObject::Style::OverShoulder ? (HoldableObject::Style)st : HoldableObject::Style::Automatic;
                    k.b("support", h->supportHand); k.b("two", h->twoHanded); k.f("weight", h->weight); k.str("socket", h->socket);
                    k.f("offX", h->holdOffset.x); k.f("offY", h->holdOffset.y); k.f("offZ", h->holdOffset.z);
                    k.f("rotX", h->holdRotation.x); k.f("rotY", h->holdRotation.y); k.f("rotZ", h->holdRotation.z);
                    k.f("handScale", h->inHandScale);
                    k.f("fpX", h->fpOffset.x); k.f("fpY", h->fpOffset.y); k.f("fpZ", h->fpOffset.z);
                    k.f("fpRX", h->fpRotation.x); k.f("fpRY", h->fpRotation.y); k.f("fpRZ", h->fpRotation.z); k.f("fpS", h->fpScale);
                    k.f("tpX", h->tpOffset.x); k.f("tpY", h->tpOffset.y); k.f("tpZ", h->tpOffset.z);
                    k.f("tpRX", h->tpRotation.x); k.f("tpRY", h->tpRotation.y); k.f("tpRZ", h->tpRotation.z);
                    HoldableObject::Grip* grips[2] = {&h->leftGrip, &h->rightGrip};
                    for (int g = 0; g < 2; ++g) {
                        const std::string p = g == 0 ? "l" : "r";
                        HoldableObject::Grip& gr = *grips[g];
                        k.b((p + "On").c_str(), gr.enabled);
                        k.f((p + "PX").c_str(), gr.position.x); k.f((p + "PY").c_str(), gr.position.y); k.f((p + "PZ").c_str(), gr.position.z);
                        k.f((p + "RX").c_str(), gr.rotation.x); k.f((p + "RY").c_str(), gr.rotation.y); k.f((p + "RZ").c_str(), gr.rotation.z);
                        k.f((p + "Curl").c_str(), gr.curl);
                    }
                } else if (field == "objholding") {
                    auto* ph = go->AddComponent<ProceduralObjectHolding>();
                    KvIn k(in);
                    int hand = 1, st = 0; k.i("hand", hand); k.i("style", st);
                    ph->primaryHand = hand == 0 ? ProceduralObjectHolding::Hand::Left
                                    : hand == 2 ? ProceduralObjectHolding::Hand::Both : ProceduralObjectHolding::Hand::Right;
                    ph->holdingStyle = st == 1 ? ProceduralObjectHolding::StyleMode::Custom : ProceduralObjectHolding::StyleMode::Automatic;
                    k.b("armIK", ph->armIK); k.f("transition", ph->transitionTime);
                    int carry = 0; k.b("body", ph->bodyAdjustment); k.i("carry", carry);
                    ph->carryAnimations = carry == 1 ? ProceduralObjectHolding::CarryAnim::Custom : ProceduralObjectHolding::CarryAnim::Automatic;
                    // Throw bindings came later. Missing keys keep the defaults, so an
                    // older objholding line still loads.
                    k.key("throwKey", ph->throwKey); k.i("throwMouse", ph->throwMouseButton); k.i("throwPad", ph->throwGamepadButton);
                    k.f("charge", ph->throwChargeTime); k.f("tmin", ph->throwMinSpeed); k.f("tmax", ph->throwMaxSpeed); k.f("reach", ph->pickupRange);
                } else if (field == "holdprompt") {
                    auto* hp = go->AddComponent<HoldPrompt>();
                    KvIn k(in);
                    k.key("drop", hp->dropKey); k.key("alt", hp->altDropKey); k.str("prompt", hp->promptObject);
                } else if (field == "fbikx") {
                    if (auto* fb = LastOf(go->GetComponents<FullBodyIK>())) {
                        in >> fb->blendSpeed >> fb->goalSmoothing >> fb->lookSmoothing >> fb->armMinBend >> fb->legMinBend;
                        KvIn k(in);
                        for (int l = 0; l < FullBodyIK::LimbCount; ++l) {
                            const std::string p = "e" + std::to_string(l);
                            ReadLimitKv(k, p + "u", fb->effectors[l].upperLimit);
                            ReadLimitKv(k, p + "l", fb->effectors[l].lowerLimit);
                            k.f((p + "b").c_str(), fb->effectors[l].targetBlendTime);
                        }
                        k.f("st", fb->stretch);
                    }
                } else if (field == "ikx") {
                    std::string kind; int act = 1; float fade = 0.0f;
                    in >> kind >> act >> fade;
                    if (kind == "aim")  { if (auto* a = NthOf<AimIK>(go, nIkAim))    { a->ikActive = act != 0; a->fadeTime = fade; } }
                    else if (kind == "look") { if (auto* l = NthOf<LookAtIK>(go, nIkLook)) { l->ikActive = act != 0; l->fadeTime = fade; } }
                    else if (kind == "limb") { if (auto* lb = NthOf<LimbIK>(go, nIkLimb)) { lb->ikActive = act != 0; lb->fadeTime = fade; } }
                    else if (kind == "chain") {
                        std::string poleName = ReadQuoted(in);
                        int up = 0; Vec3 pl{0, 0, 1}; float mb = 0.0f, tol = 1e-3f;
                        in >> up >> pl.x >> pl.y >> pl.z >> mb >> tol;
                        if (auto* ci = NthOf<ChainIK>(go, nIkChain)) {
                            ci->ikActive = act != 0; ci->fadeTime = fade;
                            ci->poleName = poleName; ci->usePole = up != 0; ci->pole = pl;
                            ci->maxJointBend = mb; ci->tolerance = tol;
                        }
                    }
                } else if (field == "rootmotion") {
                    auto* rm = go->AddComponent<RootMotion>();
                    rm->rootNodeName = ReadQuoted(in);
                    int lh = 1, ar = 0;
                    in >> rm->mode >> lh >> ar >> rm->loopThreshold;
                    rm->lockHeight = (lh != 0); rm->applyToRigidbody = (ar != 0);
                } else if (field == "boxcollider3d") {
                    Vec3 sz{1, 1, 1}, off; int trig = 0, layer = 0, af = 0;
                    in >> sz.x >> sz.y >> sz.z >> off.x >> off.y >> off.z >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> af;
                    auto* bc = go->AddComponent<BoxCollider3D>();
                    bc->size = sz; bc->offset = off; bc->isTrigger = (trig != 0); bc->layer = layer;
                    bc->autoFit = (af != 0);
                } else if (field == "spherecollider3d") {
                    float r = 0.5f; Vec3 off; int trig = 0, layer = 0, af = 0;
                    in >> r >> off.x >> off.y >> off.z >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> af;
                    auto* sc = go->AddComponent<SphereCollider3D>();
                    sc->radius = r; sc->offset = off; sc->isTrigger = (trig != 0); sc->layer = layer;
                    sc->autoFit = (af != 0);
                } else if (field == "capsulecollider3d") {
                    float r = 0.5f, h = 2.0f; int ax = 1; Vec3 off; int trig = 0, layer = 0, af = 0;
                    in >> r >> h >> ax >> off.x >> off.y >> off.z >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> af;
                    auto* cap = go->AddComponent<CapsuleCollider3D>();
                    cap->radius = r; cap->height = h; cap->axis = ax;
                    cap->offset = off; cap->isTrigger = (trig != 0); cap->layer = layer;
                    cap->autoFit = (af != 0);
                } else if (field == "meshcollider3d") {
                    Vec3 sz{1, 1, 1}, off; int trig = 0, layer = 0, af = 0;
                    in >> sz.x >> sz.y >> sz.z >> off.x >> off.y >> off.z >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> af;
                    auto* mc = go->AddComponent<MeshCollider3D>();
                    mc->size = sz; mc->offset = off; mc->isTrigger = (trig != 0); mc->layer = layer;
                    mc->autoFit = (af != 0);
                } else if (field == "cylindercollider3d") {
                    float r = 0.5f, h = 2.0f; int ax = 1; Vec3 off; int trig = 0, layer = 0, af = 0;
                    in >> r >> h >> ax >> off.x >> off.y >> off.z >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> af;
                    auto* cy = go->AddComponent<CylinderCollider3D>();
                    cy->radius = r; cy->height = h; cy->axis = ax;
                    cy->offset = off; cy->isTrigger = (trig != 0); cy->layer = layer;
                    cy->autoFit = (af != 0);
                } else if (field == "script") {
                    std::string lang = ReadQuoted(in);
                    std::string src  = ReadQuoted(in);
                    auto* sc = go->AddComponent<ScriptComponent>(lang);
                    sc->LoadSource(src);
                    // New format: an inline path + field-override block follows the
                    // source (a leading quote). Old scenes used separate lines.
                    in >> std::ws;
                    if (in.peek() == '"') {
                        sc->SetPath(ReadQuoted(in));
                        int n = 0; in >> n;
                        for (int i = 0; i < n && in; ++i) {
                            std::string k = ReadQuoted(in), v = ReadQuoted(in);
                            sc->fields[k] = v;
                        }
                        sc->ApplyFieldOverrides();
                    }
                } else if (field == "scriptpath") {
                    std::string p = ReadQuoted(in);
                    if (auto* sc = LastOf(go->GetComponents<ScriptComponent>())) sc->SetPath(p);
                } else if (field == "scriptfields") {
                    int n = 0; in >> n;
                    auto* sc = LastOf(go->GetComponents<ScriptComponent>());
                    for (int i = 0; i < n && in; ++i) {
                        std::string k = ReadQuoted(in), v = ReadQuoted(in);
                        if (sc) sc->fields[k] = v;
                    }
                    if (sc) sc->ApplyFieldOverrides();   // re-apply over the loaded defaults
                } else if (field == "visualscript") {
                    std::string src = ReadQuoted(in);
                    auto* vsc = go->AddComponent<VisualScriptComponent>();
                    vsc->LoadFromText(src);
                } else if (field == "visualscriptpath") {
                    // Prefer the file (it may have been edited since the scene was saved).
                    // A path written next to the scene ("movement.okayvs") is opened from
                    // the scene's folder when it is not already an absolute file.
                    // The component opens the found file; the scene keeps the text it had (#888 F42), see ScenePath.
                    const std::string written = ReadQuoted(in);
                    std::string p = written;
                    {
                        const std::filesystem::path pp(p);
                        if (!pp.is_absolute() && !g_loadingSceneDir.empty()) {
                            const std::filesystem::path alt = std::filesystem::path(g_loadingSceneDir) / pp;
                            std::error_code ec;
                            if (std::filesystem::exists(alt, ec)) p = alt.string();
                        }
                    }
                    auto graphs = go->GetComponents<VisualScriptComponent>();
                    if (auto* vsc = graphs.empty() ? nullptr : graphs.back()) {
                        vsc->SetPathFromScene(written, p);
                        std::ifstream probe(FilePath(p));
                        if (probe) vsc->Reload();
                    }
                } else if (field == "actions") {
                    go->AddComponent<ActionList>()->FromText(ReadQuoted(in));
                } else if (field == "charctrl2d") {
                    int m = 0; float sp = 5, jf = 9;
                    in >> m >> sp >> jf;
                    auto* cc = go->AddComponent<CharacterController2D>();
                    cc->mode = (CharacterController2D::Mode)m; cc->speed = sp; cc->jumpForce = jf;
                    // extended (optional): only overrides defaults when present.
                    std::string rs, skTok, ac, de, nd, ug, fs, mj, vj, jc, ct, jb, air, mfs, efg;
                    if (in >> rs >> skTok >> ac >> de >> nd >> ug >> fs >> mj >> vj >> jc >> ct >> jb >> air >> mfs >> efg) {
                        cc->runSpeed = (float)std::atof(rs.c_str());
                        cc->sprintKey = KeyFromToken(skTok);
                        cc->acceleration = (float)std::atof(ac.c_str());
                        cc->deceleration = (float)std::atof(de.c_str());
                        cc->normalizeDiagonal = (nd != "0");
                        cc->useGamepad = (ug != "0");
                        cc->flipSprite = (fs != "0");
                        cc->maxJumps = std::atoi(mj.c_str());
                        cc->variableJump = (vj != "0");
                        cc->jumpCutMultiplier = (float)std::atof(jc.c_str());
                        cc->coyoteTime = (float)std::atof(ct.c_str());
                        cc->jumpBuffer = (float)std::atof(jb.c_str());
                        cc->airControl = (float)std::atof(air.c_str());
                        cc->maxFallSpeed = (float)std::atof(mfs.c_str());
                        cc->extraFallGravity = (float)std::atof(efg.c_str());
                        // Wall slide / wall jump (optional trailing fields).
                        std::string wj, wss, wjf, wjp;
                        if (in >> wj >> wss >> wjf >> wjp) {
                            cc->wallJump = (wj != "0");
                            cc->wallSlideSpeed = (float)std::atof(wss.c_str());
                            cc->wallJumpForce = (float)std::atof(wjf.c_str());
                            cc->wallJumpPush = (float)std::atof(wjp.c_str());
                        }
                    }
                } else if (field == "ctmctrl") {
                    float ws = 4, rs = 7, sd = 0.15f, ts = 12, gy = 0; std::string rk = "-";
                    int mb = 0, hold = 0, anim = 1, useH = 1;
                    in >> ws >> rs >> rk >> sd >> ts >> mb >> hold >> anim >> useH >> gy;
                    auto* cm = go->AddComponent<ClickToMoveController>();
                    cm->walkSpeed = ws; cm->runSpeed = rs;
                    cm->runKey = KeyFromToken(rk);
                    cm->stopDistance = sd; cm->turnSpeed = ts; cm->mouseButton = mb;
                    cm->holdToMove = (hold != 0); cm->driveAnimation = (anim != 0);
                    cm->usePlayerHeight = (useH != 0); cm->groundY = gy;
                    // extended follow-camera fields (optional)
                    int fc = 1; float ch = 1, cd = 12, mnd = 4, mxd = 24, cy = 0, cpi = 50,
                                      mnp = 15, mxp = 85, rsp = 0.3f, krs = 90, cdamp = 0;
                    std::string rlk = "-", rrk = "-";
                    if (in >> fc >> ch >> cd >> mnd >> mxd >> cy >> cpi >> mnp >> mxp >> rsp >> krs >> rlk >> rrk >> cdamp) {
                        cm->followCamera = (fc != 0); cm->cameraHeight = ch; cm->cameraDistance = cd;
                        cm->minDistance = mnd; cm->maxDistance = mxd; cm->cameraYaw = cy;
                        cm->cameraPitch = cpi; cm->minPitch = mnp; cm->maxPitch = mxp;
                        cm->rotateSpeed = rsp; cm->keyRotateSpeed = krs;
                        cm->rotateLeftKey = KeyFromToken(rlk);
                        cm->rotateRightKey = KeyFromToken(rrk);
                        cm->cameraDamping = cdamp;
                        in >> std::ws; // optional arrival radius (added later)
                        if (LooksNumeric(in)) in >> cm->arriveRadius;
                        in >> std::ws; if (std::isdigit(in.peek())) { int fik = 0; in >> fik; cm->footIK = (fik != 0); }
                        in >> std::ws; if (std::isdigit(in.peek())) { int sc = 1; in >> sc; cm->showCursor = (sc != 0); }
                        in >> std::ws; if (std::isdigit(in.peek())) { int pf = 1; in >> pf; cm->usePathfinding = (pf != 0); }
                        if (LooksNumeric(in)) in >> cm->repathInterval;
                        // click on colliders, give up when stuck (added later; older scenes keep the defaults)
                        in >> std::ws; if (std::isdigit(in.peek())) { int pc = 1; in >> pc; cm->pickColliders = (pc != 0); }
                        in >> std::ws; if (std::isdigit(in.peek())) { int gu = 1; in >> gu; cm->giveUpWhenStuck = (gu != 0); }
                        if (LooksNumeric(in)) in >> cm->stuckTime;
                        if (LooksNumeric(in)) in >> cm->stepOffset;   // step limit (added later still)
                    }
                } else if (field == "follow2d") {
                    std::string tn = ReadQuoted(in);
                    float sp = 3, sd = 0; in >> sp >> sd;
                    auto* ft = go->AddComponent<FollowTarget2D>();
                    ft->target = tn; ft->speed = sp; ft->stopDistance = sd;
                } else if (field == "charctrl3d") {
                    float sp = 5, jf = 6; int cj = 1;
                    in >> sp >> jf >> cj;
                    auto* cc = go->AddComponent<CharacterController3D>();
                    cc->speed = sp; cc->jumpForce = jf; cc->canJump = (cj != 0);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '-') {   // trailing extras
                        int sk = 0, da = 1, fik = 0;
                        in >> cc->runSpeed >> sk >> cc->acceleration >> cc->deceleration
                           >> cc->airControl >> da >> fik;
                        cc->sprintKey = (char)sk; cc->driveAnimation = (da != 0); cc->footIK = (fik != 0);
                    }
                } else { fieldDone = false; }
                }
                if (!fieldDone) { fieldDone = true;
                if (field == "vehicle") {
                    moduleRecord("vehicles");
                } else if (field == "vehicletune") {
                    moduleRecord("vehicles");
                } else if (field == "vehiclesusp") {
                    moduleRecord("vehicles");
                } else if (field == "blockbuilder") {
                    auto* bb = go->AddComponent<BlockBuilder>();
                    in >> bb->blockSize >> bb->reach
                       >> bb->blockColor.r >> bb->blockColor.g >> bb->blockColor.b >> bb->blockColor.a
                       >> bb->placeButton >> bb->removeButton;
                    bb->blockTag = ReadQuoted(in);
                    bb->blockTexture = ReadQuoted(in);
                    // Optional trailing toggles, added over time — read each only if
                    // present (older scenes omit them; component lines start with a
                    // keyword, never a digit, so a peek tells us when they run out).
                    auto optBool = [&](bool& dst) {
                        in >> std::ws;
                        if (std::isdigit(in.peek())) { int v = 0; in >> v; dst = (v != 0); }
                    };
                    optBool(bb->showPreview);
                    optBool(bb->showCrosshair);
                    optBool(bb->placeInAir);
                } else if (field == "structurebuilder") {
                    auto* sb = go->AddComponent<StructureBuilder>();
                    int pc = 0; in >> pc; sb->piece = (StructureBuilder::Piece)pc;
                    in >> sb->cellSize >> sb->wallHeight >> sb->slabThickness
                       >> sb->wallThickness >> sb->pillarThickness >> sb->reach
                       >> sb->rotSteps >> sb->placeButton >> sb->removeButton;
                    int sp = 1, sc = 1, rr = 1; in >> sp >> sc >> rr;
                    sb->showPreview = (sp != 0); sb->showCrosshair = (sc != 0); sb->requireResources = (rr != 0);
                    in >> sb->refundFraction >> sb->costWood >> sb->costStone >> sb->costMetal;
                    sb->structureTag = ReadQuoted(in);
                    sb->structureTexture = ReadQuoted(in);
                    sb->woodItem = ReadQuoted(in);
                    sb->stoneItem = ReadQuoted(in);
                    sb->metalItem = ReadQuoted(in);
                } else if (field == "buildermode") {
                    auto* bm = go->AddComponent<BuilderMode>();
                    int br = 0; in >> br; bm->brush = (BuilderMode::Brush)br;
                    int sg = 1; in >> bm->gridSize >> sg; bm->snapToGrid = (sg != 0);
                    in >> bm->reach >> bm->rotateStepDeg >> bm->scaleStep
                       >> bm->minScale >> bm->maxScale
                       >> bm->partColor.r >> bm->partColor.g >> bm->partColor.b >> bm->partColor.a
                       >> bm->placeButton >> bm->removeButton;
                    int pm = 1, bh = 1, sp = 1, sc = 1; in >> pm >> bh >> sp >> sc;
                    bm->parentToModel = (pm != 0); bm->brushHotkeys = (bh != 0);
                    bm->showPreview = (sp != 0); bm->showCrosshair = (sc != 0);
                    bm->buildTag = ReadQuoted(in);
                    bm->partTexture = ReadQuoted(in);
                    bm->prefabPath = ReadQuoted(in);
                    bm->modelName = ReadQuoted(in);
                    // Optional trailing token (added later): the map-save path.
                    in >> std::ws;
                    if (in.peek() == '"') bm->mapPath = ReadQuoted(in);
                } else if (field == "buildpiece") {
                    auto* bp = go->AddComponent<BuildPiece>();
                    in >> bp->tier >> bp->health >> bp->maxHealth >> bp->cost;
                    bp->material = ReadQuoted(in);
                } else if (field == "buildable") {
                    auto* b = go->AddComponent<Buildable>();
                    KvIn k(in);
                    k.i("id", b->buildId); k.b("edit", b->editable); k.b("placed", b->placed);
                    k.str("prefab", b->prefab);
                } else if (field == "buildmode") {
                    auto* bm = go->AddComponent<BuildMode>();
                    BuildBindings& kb = bm->bindings;
                    KvIn k(in);
                    k.f("grid", bm->gridSize); k.f("rot", bm->rotationStep); k.f("reach", bm->maxReach);
                    if (const std::string* s = k.find("layers")) bm->buildLayers = (std::uint32_t)std::strtoul(s->c_str(), nullptr, 10);
                    k.i("max", bm->maxObjects); k.b("air", bm->allowAirPlacement); k.b("start", bm->startActive);
                    k.b("hotbar", bm->showHotbar); k.b("ghost", bm->showGhost); k.str("folder", bm->creationFolder);
                    k.col("ok", bm->validColor); k.col("bad", bm->invalidColor); k.col("hl", bm->highlightColor);
                    k.col("wire", bm->wireColor); k.col("wireOn", bm->wireOnColor);
                    k.key("kToggle", kb.toggle); k.i("kPlace", kb.place); k.i("kRemove", kb.remove);
                    k.key("kRotate", kb.rotate); k.key("kMove", kb.move); k.key("kDup", kb.duplicate);
                    k.key("kLink", kb.link); k.key("kUndo", kb.undo); k.key("kRedo", kb.redo);
                    k.key("kNext", kb.nextItem); k.key("kPrev", kb.prevItem);
                    k.b("kNum", kb.numberKeys); k.b("kWheel", kb.mouseWheel); k.key("kRemoveKey", kb.removeKey);
                    k.i("pToggle", kb.padToggle); k.i("pPlace", kb.padPlace); k.i("pRemove", kb.padRemove);
                    k.i("pRotate", kb.padRotate); k.i("pNext", kb.padNext); k.i("pUndo", kb.padUndo);
                    k.i("pMove", kb.padMove); k.i("pDup", kb.padDuplicate); k.i("pLink", kb.padLink);
                    k.i("pRedo", kb.padRedo); k.i("pPrev", kb.padPrev);
                    int pal = 0; k.i("pal", pal);
                    for (int i = 0; i < pal && i < 256; ++i) {
                        const std::string p = "p" + std::to_string(i);
                        BuildPaletteItem it;
                        k.str((p + "n").c_str(), it.name); k.str((p + "f").c_str(), it.prefab); k.str((p + "i").c_str(), it.icon);
                        bm->palette.push_back(it);
                    }
                } else if (field == "logicsource") {
                    auto* ls = go->AddComponent<LogicSource>();
                    KvIn k(in);
                    int kind = 0; k.i("kind", kind);
                    if (kind >= 0 && kind < LogicSource::kKindCount) ls->kind = (LogicSource::Kind)kind;
                    k.b("toggle", ls->toggle); k.f("hold", ls->holdTime); k.f("interval", ls->interval);
                    k.f("on", ls->onTime); k.b("startOn", ls->startOn);
                    k.f("zx", ls->zoneSize.x); k.f("zy", ls->zoneSize.y); k.f("zz", ls->zoneSize.z);
                    k.f("ox", ls->zoneOffset.x); k.f("oy", ls->zoneOffset.y); k.f("oz", ls->zoneOffset.z);
                    k.str("tag", ls->activatorTag); k.key("key", ls->pressKey); k.i("pad", ls->pressPadButton);
                    k.f("reach", ls->pressReach);
                    std::string links; k.str("links", links);
                    std::replace(links.begin(), links.end(), ',', ' ');
                    std::istringstream ids(links);
                    for (int id = 0; ids >> id;) if (id != 0) ls->links.push_back(id);
                    int nio = 0; k.i("io", nio);
                    for (int i = 0; i < nio && i < 256; ++i) {
                        const std::string p = "io" + std::to_string(i);
                        LogicSource::IoLink l;
                        k.str((p + "o").c_str(), l.output); k.str((p + "t").c_str(), l.target);
                        k.str((p + "i").c_str(), l.input); k.str((p + "p").c_str(), l.param);
                        k.f((p + "d").c_str(), l.delay); k.b((p + "x").c_str(), l.once); k.b((p + "e").c_str(), l.enabled);
                        if (!(l.delay >= 0.0f)) l.delay = 0.0f;
                        ls->ioLinks.push_back(l);
                    }
                } else if (field == "logictarget") {
                    auto* lt = go->AddComponent<LogicTarget>();
                    KvIn k(in);
                    int kind = 0, mode = 0; k.i("kind", kind); k.i("mode", mode);
                    if (kind >= 0 && kind < LogicTarget::kKindCount) lt->kind = (LogicTarget::Kind)kind;
                    lt->mode = mode == 1 ? LogicTarget::Mode::Toggle : LogicTarget::Mode::Follow;
                    k.b("invert", lt->invert);
                    k.f("dx", lt->openOffset.x); k.f("dy", lt->openOffset.y); k.f("dz", lt->openOffset.z);
                    k.f("yaw", lt->openYaw); k.f("time", lt->moveTime); k.f("light", lt->lightIntensity);
                    k.col("glow", lt->onEmissive); k.str("prefab", lt->spawnPrefab);
                    k.f("sx", lt->spawnOffset.x); k.f("sy", lt->spawnOffset.y); k.f("sz", lt->spawnOffset.z);
                    k.i("alive", lt->maxAlive); k.str("event", lt->eventName);
                } else if (field == "loadingscreen") {
                    auto* ls = go->AddComponent<LoadingScreen>();
                    ls->asyncLoad = false;   // a screen saved before background loading existed keeps loading in one go
                    auto& bg = ls->backgroundColor; auto& tc = ls->textColor;
                    auto& bb = ls->barBackground; auto& bf = ls->barFill;
                    in >> bg.r >> bg.g >> bg.b >> bg.a >> tc.r >> tc.g >> tc.b >> tc.a
                       >> bb.r >> bb.g >> bb.b >> bb.a >> bf.r >> bf.g >> bf.b >> bf.a;
                    int sb = 1, st = 1, so = 1; in >> sb >> st >> ls->duration >> so >> ls->fadeTime;
                    ls->showBar = (sb != 0); ls->showTitle = (st != 0); ls->showOnStart = (so != 0);
                    ls->title = ReadQuoted(in);
                    ls->targetScene = ReadQuoted(in);
                    ls->backgroundImage = ReadQuoted(in);
                    int n = 0; in >> n;
                    ls->tips.clear();
                    for (int i = 0; i < n && in; ++i) ls->tips.push_back(ReadQuoted(in));
                    in >> std::ws;   // optional style block (appended later)
                    int lpk = in.peek();
                    if (std::isdigit(lpk) || lpk == '-' || lpk == '.') {
                        auto rc = [&](Color& c) { in >> c.r >> c.g >> c.b >> c.a; };
                        int gb = 0, bb2 = 0, sp = 0, ss2 = 0;
                        in >> gb; rc(ls->backgroundColor2);
                        rc(ls->titleColor); in >> ls->titleScale >> ls->titleY;
                        rc(ls->tipColor);   in >> ls->tipScale >> ls->tipY;
                        in >> ls->barWidth >> ls->barHeight >> ls->barY >> ls->barRadius >> bb2; rc(ls->barBorderColor);
                        in >> sp >> ss2; rc(ls->spinnerColor);
                        in >> ls->spinnerRadius >> ls->spinnerDotSize >> ls->spinnerSpeed >> ls->spinnerY;
                        ls->gradientBackground = (gb != 0); ls->barBorder = (bb2 != 0);
                        ls->showPercent = (sp != 0); ls->showSpinner = (ss2 != 0);
                        in >> std::ws;   // optional layout block (added later)
                        int lpk2 = in.peek();
                        if (std::isdigit(lpk2) || lpk2 == '-' || lpk2 == '.') {
                            int lp = 1, kb = 0;
                            in >> ls->layout >> ls->barStyle >> ls->spinnerStyle >> ls->logoSize >> lp >> kb >> ls->tipInterval;
                            rc(ls->cardColor);
                            ls->subtitle = ReadQuoted(in);
                            ls->logoImage = ReadQuoted(in);
                            ls->logoPulse = (lp != 0); ls->kenBurns = (kb != 0);
                            in >> std::ws;   // optional background-loading block (added later)
                            if (std::isdigit(in.peek())) {
                                int al = 0, opf = ls->objectsPerFrame; in >> al >> opf;
                                ls->asyncLoad = (al != 0); ls->objectsPerFrame = std::clamp(opf, 1, 100000);
                            }
                        }
                    }
                } else if (field == "inventoryui") {
                    auto* ui = go->AddComponent<InventoryUI>();
                    int tk = 'e', sh = 1, ss = 1, dk = 1;
                    in >> ui->hotbarSlots >> ui->backpackRows >> ui->selected >> tk >> sh >> ss
                       >> ui->slotSize >> ui->slotGap >> dk;
                    ui->toggleKey = (char)tk; ui->selectHotkeys = (sh != 0);
                    ui->scrollSelect = (ss != 0); ui->darkenWhenOpen = (dk != 0);
                    auto rc = [&](Color& c) { in >> c.r >> c.g >> c.b >> c.a; };
                    rc(ui->slotColor); rc(ui->slotBorder); rc(ui->selectedColor); rc(ui->textColor); rc(ui->panelColor);
                    ui->iconFolder = ReadQuoted(in);
                    in >> std::ws;   // optional dragItems toggle (added later)
                    if (std::isdigit(in.peek())) { int d = 1; in >> d; ui->dragItems = (d != 0); }
                    in >> std::ws;   // optional layout/style block (added later; numeric)
                    int pk = in.peek();
                    if (std::isdigit(pk) || pk == '-' || pk == '.') {
                        int at = 0, scn = 1, snm = 1;
                        in >> ui->marginX >> ui->marginY >> at >> ui->cornerRadius >> ui->borderWidth
                           >> ui->labelScale >> scn >> snm
                           >> ui->countColor.r >> ui->countColor.g >> ui->countColor.b >> ui->countColor.a;
                        ui->anchorTop = (at != 0); ui->showCounts = (scn != 0); ui->showNames = (snm != 0);
                        in >> std::ws;   // optional second style block (panel / hover / held name)
                        int pk2 = in.peek();
                        if (std::isdigit(pk2) || pk2 == '-' || pk2 == '.') {
                            int sp = 0, ssn = 0;
                            in >> sp >> ui->panelPad
                               >> ui->hoverColor.r >> ui->hoverColor.g >> ui->hoverColor.b >> ui->hoverColor.a
                               >> ssn >> ui->nameChars;
                            ui->showPanel = (sp != 0); ui->showSelectedName = (ssn != 0);
                            in >> std::ws;   // optional features block (tooltips / numbers / sort)
                            int pk3 = in.peek();
                            if (std::isdigit(pk3) || pk3 == '-' || pk3 == '.') {
                                int st = 1, sn = 0, srt = 0;
                                in >> st >> sn >> srt;
                                ui->showTooltips = (st != 0); ui->slotNumbers = (sn != 0); ui->sortKey = (char)srt;
                                rc(ui->tooltipColor); rc(ui->tooltipText); rc(ui->numberColor);
                                in >> std::ws;   // optional split / quick-move / rarity block
                                int pk4 = in.peek();
                                if (std::isdigit(pk4) || pk4 == '-' || pk4 == '.') {
                                    int sr = 1, qm = 1; std::size_t rn = 0;
                                    in >> sr >> qm >> rn;
                                    ui->splitRightClick = (sr != 0); ui->shiftQuickMove = (qm != 0);
                                    ui->rarities.clear();
                                    for (std::size_t ri = 0; ri < rn && in; ++ri) {
                                        InventoryUI::RarityRule rr; rr.item = ReadQuoted(in); rc(rr.color);
                                        ui->rarities.push_back(rr);
                                    }
                                    in >> std::ws;   // optional scaling / split / icon block
                                    int pk5 = in.peek();
                                    if (std::isdigit(pk5) || pk5 == '-' || pk5 == '.') {
                                        int sts = 1, sh = 1;
                                        in >> sts >> ui->referenceHeight >> sh >> ui->iconInset;
                                        ui->scaleToScreen = (sts != 0); ui->splitHalf = (sh != 0);
                                        in >> std::ws;   // optional sort-mode / auto-sort / drop-key block
                                        int pk6 = in.peek();
                                        if (std::isdigit(pk6) || pk6 == '-' || pk6 == '.') {
                                            int sm = 0, soc = 0, dk = 0;
                                            in >> sm >> soc >> dk;
                                            ui->sortMode = (InventoryUI::SortMode)std::max(0, std::min(sm, (int)InventoryUI::SortMode::Value));
                                            ui->sortOnClose = (soc != 0);
                                            ui->dropKey = (char)dk;
                                            in >> std::ws;   // optional style block (align/pop/colours/border/title)
                                            int pk7 = in.peek();
                                            if (std::isdigit(pk7) || pk7 == '-' || pk7 == '.') {
                                                int ha = 0, st = 0;
                                                in >> ha; ui->hAlign = (InventoryUI::HAlign)ha;
                                                in >> ui->selectedScale;
                                                rc(ui->emptySlotColor); rc(ui->hotbarColor); rc(ui->panelBorder);
                                                in >> ui->panelBorderWidth >> st; ui->showTitle = (st != 0);
                                                rc(ui->titleColor);
                                                ui->backpackTitle = ReadQuoted(in);
                                                in >> std::ws;   // optional inventory target (quoted)
                                                if (in.peek() == '"') {
                                                    ui->inventoryTarget = ReadQuoted(in);
                                                    in >> std::ws;   // optional use key + click-to-use
                                                    if (std::isdigit(in.peek())) { int uk = 0, cu = 0; in >> uk >> cu; ui->useKey = (char)uk; ui->clickToUse = (cu != 0); }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else if (field == "gridinventory") {
                    auto* gi = go->AddComponent<GridInventory>();
                    gi->enforceWeight = false;   // older scenes only displayed the limit
                    int tk = 'i'; in >> gi->cols >> gi->rows >> tk; gi->toggleKey = (char)tk;
                    gi->title = ReadQuoted(in);
                    int n = 0; in >> n; gi->items.clear();
                    for (int i = 0; i < n && in; ++i) {
                        GridItem it; it.name = ItemId(ReadQuoted(in));
                        in >> it.w >> it.h >> it.x >> it.y >> it.count >> it.weight;
                        gi->items.push_back(it);
                    }
                    in >> std::ws;   // optional weight limit (appended later)
                    int wpk = in.peek();
                    if (std::isdigit(wpk) || wpk == '-' || wpk == '.') {
                        in >> gi->weightLimit;
                        in >> std::ws;   // optional Unturned role (category + worldItem)
                        if (in.peek() == '"') {
                            gi->category = ReadQuoted(in);
                            int wi = 0; in >> wi; gi->worldItem = (wi != 0);
                            in >> std::ws;   // optional enforced weight + stack size
                            int epk = in.peek();
                            if (std::isdigit(epk) || epk == '-') {
                                int ew = 0, no = 0; in >> ew >> gi->maxStack >> no; gi->enforceWeight = (ew != 0);
                                for (int i = 0; i < no && in; ++i) {
                                    GridItem it; it.name = ItemId(ReadQuoted(in));
                                    in >> it.w >> it.h >> it.count >> it.weight;
                                    gi->overflow.push_back(it);
                                }
                                ReadInstanceData(in, gi->items.size(), [&](std::size_t i) -> ItemInstanceData& { return gi->items[i].data; });
                                in >> std::ws;   // optional keep-on-scene-load
                                if (std::isdigit(in.peek())) { int keep = 0; in >> keep; gi->keepOnSceneLoad = (keep != 0); }
                            }
                        }
                    }
                    gi->Validate();   // overlapping / out-of-bounds / bad sizes from older or hand-edited files
                } else if (field == "gridinventoryui") {
                    auto* gu = go->AddComponent<GridInventoryUI>();
                    int dk = 1, sw = 1; in >> gu->cellSize >> gu->gap >> dk >> sw;
                    gu->darkenWhenOpen = (dk != 0); gu->showWeight = (sw != 0);
                    auto rc = [&](Color& c) { in >> c.r >> c.g >> c.b >> c.a; };
                    rc(gu->panelColor); rc(gu->cellColor); rc(gu->itemColor); rc(gu->itemBorder); rc(gu->textColor);
                    gu->iconFolder = ReadQuoted(in);
                    in >> std::ws;   // optional polish block (added later; numeric)
                    int gpk = in.peek();
                    if (std::isdigit(gpk) || gpk == '-' || gpk == '.') {
                        in >> gu->cornerRadius;
                        rc(gu->titleBar); rc(gu->hoverColor); rc(gu->dropOk); rc(gu->dropBad);
                        in >> std::ws;   // optional features block (tooltips / rotate)
                        int gpk2 = in.peek();
                        if (std::isdigit(gpk2) || gpk2 == '-' || gpk2 == '.') {
                            int st = 1, rkk = (int)(unsigned char)'r';
                            in >> st >> rkk;
                            gu->showTooltips = (st != 0); gu->rotateKey = (char)rkk;
                            rc(gu->tooltipColor); rc(gu->tooltipText);
                            in >> std::ws;   // optional overweight colour + rarity rules
                            int gpk3 = in.peek();
                            if (std::isdigit(gpk3) || gpk3 == '-' || gpk3 == '.') {
                                rc(gu->overweightColor);
                                std::size_t rn = 0; in >> rn;
                                gu->rarities.clear();
                                for (std::size_t ri = 0; ri < rn && in; ++ri) {
                                    GridInventoryUI::RarityRule rr; rr.item = ReadQuoted(in); rc(rr.color);
                                    gu->rarities.push_back(rr);
                                }
                                in >> std::ws;   // optional Unturned multi-container block
                                int gpk4 = in.peek();
                                if (std::isdigit(gpk4) || gpk4 == '-' || gpk4 == '.') {
                                    int mc = 1; in >> mc >> gu->nearbyRange;
                                    gu->multiContainer = (mc != 0);
                                    gu->nearbyTitle = ReadQuoted(in);
                                    in >> std::ws;   // optional layout + style block
                                    int gpk5 = in.peek();
                                    if (std::isdigit(gpk5) || gpk5 == '-' || gpk5 == '.') {
                                        int sm = 1, ug = 1, ds = 1, ab = 1, aa = 1;
                                        in >> gu->columnGap >> gu->panelPad >> gu->containerGap >> gu->headerHeight >> sm;
                                        gu->masterTitle = ReadQuoted(in);
                                        in >> ug >> ds >> ab >> aa;
                                        gu->showMasterTitle = (sm != 0); gu->useGradients = (ug != 0);
                                        gu->dropShadows = (ds != 0); gu->accentBars = (ab != 0); gu->autoAccent = (aa != 0);
                                        rc(gu->accentColor); rc(gu->headerColor); rc(gu->weightGood); rc(gu->weightWarn);
                                        in >> std::ws; int gpk6 = in.peek();
                                        if (std::isdigit(gpk6) || gpk6 == '-' || gpk6 == '.') {
                                            int sin = 1; in >> gu->titleScale >> gu->itemNameScale >> sin >> gu->backdropDim;
                                            gu->showItemNames = (sin != 0);
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else if (field == "craftingstation") {
                    auto* cs = go->AddComponent<CraftingStation>();
                    auto rc = [&](Color& c) { in >> c.r >> c.g >> c.b >> c.a; };
                    int op = 0, tk = 'c', cw = 0;
                    in >> op >> tk; cs->title = ReadQuoted(in); in >> cw
                       >> cs->rowHeight >> cs->width >> cs->marginX >> cs->marginY >> cs->cornerRadius;
                    cs->toggleKey = (char)tk; cs->closeWhenCrafted = (cw != 0);   // `op` (old saved open state) is ignored
                    cs->iconFolder = ReadQuoted(in);
                    rc(cs->panelColor); rc(cs->titleBar); rc(cs->rowColor); rc(cs->canColor);
                    rc(cs->cantColor); rc(cs->textColor); rc(cs->hoverColor);
                    std::size_t rn = 0; in >> rn; cs->recipes.clear();
                    for (std::size_t ri = 0; ri < rn && in; ++ri) {
                        CraftingStation::Recipe r; std::size_t ic = 0; in >> ic;
                        for (std::size_t ii = 0; ii < ic && in; ++ii) {
                            CraftingStation::Ingredient ing; ing.item = ItemId(ReadQuoted(in)); in >> ing.count;
                            r.inputs.push_back(ing);
                        }
                        r.output = ItemId(ReadQuoted(in)); in >> r.outputCount;
                        cs->recipes.push_back(r);
                    }
                    cs->range = 0.0f;   // older scenes: no range (opened from anywhere)
                    in >> std::ws;   // optional range + player name
                    int spk = in.peek();
                    if (std::isdigit(spk) || spk == '-' || spk == '.') {
                        in >> cs->range;
                        in >> std::ws;
                        if (in.peek() == '"') {
                            cs->playerName = ReadQuoted(in);
                            in >> std::ws;   // optional recipe book + station tag + recipe extras
                            if (in.peek() == '"') {
                                cs->recipeBook = ReadQuoted(in); cs->stationTag = ReadQuoted(in);
                                ReadRecipeExtras(in, cs->recipes);
                            }
                        }
                    }
                } else if (field == "chestinventory") {
                    auto* ch = go->AddComponent<ChestInventory>();
                    auto rc = [&](Color& c) { in >> c.r >> c.g >> c.b >> c.a; };
                    int op = 0, ok = 'f', dk = 1;
                    in >> op >> ok >> ch->range; ch->title = ReadQuoted(in);
                    in >> ch->cols >> ch->slotSize >> ch->gap >> ch->cornerRadius;
                    ch->iconFolder = ReadQuoted(in); in >> dk;
                    ch->openKey = (char)ok; ch->darkenWhenOpen = (dk != 0);   // `op` (old saved open state) is ignored
                    rc(ch->panelColor); rc(ch->titleBar); rc(ch->slotColor); rc(ch->slotBorder);
                    rc(ch->textColor); rc(ch->hoverColor);
                    in >> std::ws;   // optional player name
                    if (in.peek() == '"') ch->playerName = ReadQuoted(in);
                } else if (field == "netinventory") {
                    auto* ni = go->AddComponent<NetInventory>();
                    int hide = 1;
                    in >> ni->range >> ni->maxPerSecond >> ni->cheatThreshold >> ni->maxRequestBytes >> hide;
                    ni->hideUntilOpen = (hide != 0);
                    in >> std::ws;
                    if (in.peek() == '"') ni->saveDir = ReadQuoted(in);
                } else if (field == "vehicle2d") {
                    moduleRecord("vehicles");
                } else if (field == "survival") {
                    moduleRecord("survival");
                } else if (field == "stat_health") {
                    auto* c = go->AddComponent<HealthStat>();
                    in >> c->maxHealth >> c->armor >> c->regenPerSecond >> c->regenDelay >> c->lowThreshold;
                    ReadStatTail(in, c);
                    in >> std::ws;   // optional mitigation / overheal / respawn fields (added later)
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-') {
                        int gd = 0, dod = 0, rs = 0;
                        in >> c->resistance >> c->minDamage >> gd >> c->invincibleTime
                           >> c->overhealMax >> c->overhealDecay >> dod >> rs >> c->respawnDelay >> c->lives;
                        c->godMode = (gd != 0); c->destroyOnDeath = (dod != 0); c->respawn = (rs != 0);
                    }
                    in >> std::ws;   // optional: play the death animation (defaults on)
                    if (std::isdigit(in.peek())) {
                        int pda = 1; in >> pda; c->playDeathAnim = (pda != 0);
                    }
                    in >> std::ws;   // optional: light hit flinch (defaults off)
                    if (std::isdigit(in.peek())) {
                        int pha = 0; in >> pha; c->playHitAnim = (pha != 0);
                    }
                    in >> std::ws;   // optional: "Nothing" on death, then one resistance per damage kind
                    if (std::isdigit(in.peek())) {
                        // Read only the rest of this record's line, so a wrong or hostile count can neither
                        // loop for long nor eat the next record. At most 64 kinds; stop at the first bad value.
                        std::string rest; std::getline(in, rest);
                        std::istringstream ls(rest);
                        int ka = 0, n = 0;
                        if (ls >> ka) c->keepActiveOnDeath = (ka != 0);
                        if ((ls >> n) && n > 0 && n <= 64) {
                            for (int i = 0; i < n; ++i) {
                                float r = 0.0f;
                                if (!(ls >> r)) break;
                                if (i < kDamageTypeCount && std::isfinite(r)) c->typeResistance[i] = Mathf::Clamp(r, -1.0f, 1.0f);
                            }
                        }
                    }
                } else if (field == "seat") {
                    auto* c = go->AddComponent<Seat>();
                    in >> c->seatHeight >> c->sitPoint.x >> c->sitPoint.y >> c->sitPoint.z
                       >> c->facing >> c->type
                       >> c->exitPoint.x >> c->exitPoint.y >> c->exitPoint.z
                       >> c->useRadius;
                    in >> std::ws;
                    if (in.peek() == '"') c->prompt = ReadQuoted(in);
                } else if (field == "vehicleseat") {
                    auto* c = go->AddComponent<VehicleSeat>();
                    int dr = 1, ek = 'e';
                    in >> dr >> c->sitPoint.x >> c->sitPoint.y >> c->sitPoint.z
                       >> c->exitPoint.x >> c->exitPoint.y >> c->exitPoint.z
                       >> c->useRadius >> ek;
                    c->driver = (dr != 0); c->enterKey = (char)ek;
                    in >> std::ws;
                    if (in.peek() == '"') c->prompt = ReadQuoted(in);
                } else if (field == "stat_hunger") {
                    auto* c = go->AddComponent<HungerStat>();
                    in >> c->maxHunger >> c->drainPerSecond >> c->sprintMultiplier >> c->lowThreshold;
                    ReadStatTail(in, c);
                    in >> std::ws;   // optional starve/overeat/slow fields (added later)
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-') {
                        int sl = 0; in >> c->starveDamage >> c->overeatMax >> sl >> c->minSpeedFactor;
                        c->slowWhenStarving = (sl != 0);
                    }
                } else if (field == "stat_thirst") {
                    auto* c = go->AddComponent<ThirstStat>();
                    in >> c->maxThirst >> c->drainPerSecond >> c->sprintMultiplier >> c->lowThreshold;
                    ReadStatTail(in, c);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-') {
                        int sl = 0; in >> c->dehydrateDamage >> c->overdrinkMax >> sl >> c->minSpeedFactor;
                        c->slowWhenDehydrated = (sl != 0);
                    }
                } else if (field == "stat_stamina") {
                    auto* c = go->AddComponent<StaminaStat>();
                    in >> c->maxStamina >> c->regenPerSecond >> c->regenDelay >> c->sprintCost
                       >> c->jumpCost >> c->exhaustedUntil;
                    ReadStatTail(in, c);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-') {
                        int sl = 0; in >> c->lowThreshold >> sl >> c->minSpeedFactor >> c->regenScale;
                        c->slowWhenExhausted = (sl != 0);
                    }
                } else if (field == "stat_oxygen") {
                    auto* c = go->AddComponent<OxygenStat>();
                    in >> c->maxOxygen >> c->drainPerSecond >> c->refillPerSecond;
                    ReadStatTail(in, c);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-')
                        in >> c->lowThreshold >> c->drownDamage;
                } else if (field == "stat_temp") {
                    auto* c = go->AddComponent<TemperatureStat>();
                    in >> c->maxWarmth >> c->coldDrain >> c->warmRegen;
                    ReadStatTail(in, c);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-')
                        in >> c->lowThreshold >> c->freezeDamage;
                } else if (field == "stat_sleep") {
                    auto* c = go->AddComponent<SleepStat>();
                    in >> c->maxEnergy >> c->drainPerSecond >> c->restPerSecond >> c->tiredThreshold;
                    ReadStatTail(in, c);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-') {
                        int sl = 0; in >> c->exhaustDamage >> sl >> c->minSpeedFactor;
                        c->slowWhenTired = (sl != 0);
                    }
                } else if (field == "stat_sanity") {
                    auto* c = go->AddComponent<SanityStat>();
                    in >> c->maxSanity >> c->drainInDark >> c->regenInLight >> c->lowThreshold;
                    ReadStatTail(in, c);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-')
                        in >> c->insaneDamage;
                } else if (field == "stat_radiation") {
                    auto* c = go->AddComponent<RadiationStat>();
                    in >> c->maxRadiation >> c->gainPerSecond >> c->decayPerSecond
                       >> c->sickThreshold >> c->damagePerSecond;
                    ReadStatTail(in, c);
                } else if (field == "stat_bleed") {
                    auto* c = go->AddComponent<BleedingStat>();
                    in >> c->maxBleed >> c->damagePerSecond >> c->clotPerSecond;
                    ReadStatTail(in, c);
                } else if (field == "stat_poison") {
                    auto* c = go->AddComponent<PoisonStat>();
                    in >> c->maxPoison >> c->damagePerSecond >> c->decayPerSecond;
                    ReadStatTail(in, c);
                } else if (field == "stat_wet") {
                    auto* c = go->AddComponent<WetnessStat>();
                    in >> c->maxWetness >> c->soakPerSecond >> c->dryPerSecond
                       >> c->chillPerSecond >> c->soakedThreshold;
                    ReadStatTail(in, c);
                } else if (field == "stat_carry") {
                    auto* c = go->AddComponent<CarryWeightStat>();
                    in >> c->maxLoad >> c->overStaminaDrain >> c->minSpeedFactor;
                    ReadStatTail(in, c);
                } else if (field == "statuseffect") {
                    auto* c = go->AddComponent<StatusEffectStat>();
                    int sm = 1; in >> sm; c->sendMessages = (sm != 0);
                } else if (field == "survivalsave") {
                    auto* c = go->AddComponent<SurvivalSave>();
                    c->saveKey = ReadQuoted(in);
                    int lo = 1, sc = 0; in >> lo >> sc;
                    c->loadOnStart = (lo != 0); c->saveContinuously = (sc != 0);
                } else if (field == "survivalzone") {
                    auto* c = go->AddComponent<SurvivalZone>();
                    in >> c->effect >> c->amount >> c->duration;
                    c->effectName = ReadQuoted(in);
                } else if (field == "consumables") {
                    auto* c = go->AddComponent<Consumables>();
                    int ri = 1, cd = 1, dd = 1, n = 0;
                    in >> ri >> cd >> dd >> n;
                    c->requireInventory = (ri != 0); c->consumeOnDrop = (cd != 0); c->destroyDroppedItem = (dd != 0);
                    for (int i = 0; i < n && in; ++i) {
                        Consumables::Recipe r;
                        r.item = ItemId(ReadQuoted(in)); r.action = ReadQuoted(in); in >> r.amount;
                        c->recipes.push_back(r);
                    }
                } else if (field == "daynight") {
                    auto* c = go->AddComponent<DayNightCycle>();
                    int ps = 0, cs = 1, rs = 1, ck = 1;
                    in >> c->dayLengthSeconds >> c->time >> ps >> cs >> rs >> ck
                       >> c->dayIntensity >> c->nightIntensity >> c->dayAmbient >> c->nightAmbient;
                    c->paused = (ps != 0); c->controlSun = (cs != 0); c->rotateSun = (rs != 0); c->controlSky = (ck != 0);
                    auto col = [&](Color& k) { in >> k.r >> k.g >> k.b; k.a = 1.0f; };
                    col(c->dayLight); col(c->nightLight); col(c->skyDay); col(c->skyHorizon); col(c->skyNight);
                } else { fieldDone = false; }
                }
                if (!fieldDone) { fieldDone = true;
                if (field == "npc") {
                    auto* c = go->AddComponent<NPCController>();
                    int fm = 1;
                    in >> c->behavior >> c->moveSpeed >> c->sightRange >> c->wanderRadius
                       >> c->attackRange >> c->attackDamage >> c->attackInterval >> fm;
                    c->faceMovement = (fm != 0);
                    c->targetName = ReadQuoted(in);
                    in >> std::ws;   // optional combat health (added later)
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-') {
                        in >> c->maxHealth; c->health = c->maxHealth;
                        int iv = 0; in >> iv; c->invulnerable = (iv != 0);
                        // Smarter-AI trailing fields: read whatever remains (old files stop here).
                        std::string rest; std::getline(in, rest);
                        std::istringstream rs(rest); std::vector<float> v; float f;
                        while (rs >> f) v.push_back(f);
                        auto g = [&](std::size_t i, float d) { return i < v.size() ? v[i] : d; };
                        c->runSpeed       = g(0,  c->runSpeed);
                        c->turnSpeed      = g(1,  c->turnSpeed);
                        c->acceleration   = g(2,  c->acceleration);
                        c->stopDistance   = g(3,  c->stopDistance);
                        c->fieldOfView    = g(4,  c->fieldOfView);
                        c->lineOfSight    = g(5,  c->lineOfSight ? 1.f : 0.f) != 0.0f;
                        c->eyeHeight      = g(6,  c->eyeHeight);
                        c->hearingRange   = g(7,  c->hearingRange);
                        c->detectionTime  = g(8,  c->detectionTime);
                        c->loseSightTime  = g(9,  c->loseSightTime);
                        c->searchTime     = g(10, c->searchTime);
                        c->aggressive     = g(11, c->aggressive ? 1.f : 0.f) != 0.0f;
                        c->provokable     = g(12, c->provokable ? 1.f : 0.f) != 0.0f;
                        c->returnsHome    = g(13, c->returnsHome ? 1.f : 0.f) != 0.0f;
                        c->leashRange     = g(14, c->leashRange);
                        c->wanderPause    = g(15, c->wanderPause);
                        c->patrolLoop     = g(16, c->patrolLoop ? 1.f : 0.f) != 0.0f;
                        c->waypointWait   = g(17, c->waypointWait);
                        c->attackWindup   = g(18, c->attackWindup);
                        c->fleeHealthPct  = g(19, c->fleeHealthPct);
                        c->separationRadius = g(20, c->separationRadius);
                        c->driveAnimation = g(21, c->driveAnimation ? 1.f : 0.f) != 0.0f;
                        c->lookAtTarget   = g(22, c->lookAtTarget ? 1.f : 0.f) != 0.0f;
                        c->footIK         = g(23, c->footIK ? 1.f : 0.f) != 0.0f;
                        c->usePathfinding = g(24, c->usePathfinding ? 1.f : 0.f) != 0.0f;
                        c->repathInterval = g(25, c->repathInterval);
                    }
                } else if (field == "npcwp") {
                    if (auto* c = LastOf(go->GetComponents<NPCController>())) {
                        Vec3 w; in >> w.x >> w.y >> w.z; c->waypoints.push_back(w);
                    }
                } else if (field == "melee") {
                    auto* c = go->AddComponent<MeleeAttacker>();
                    int key = 'f', um = 1;
                    in >> c->damage >> c->range >> c->arc >> c->cooldown >> key >> um;
                    c->attackKey = (char)key; c->useMouse = (um != 0);
                    if (LooksNumeric(in)) {
                        int combo = c->comboSteps, hb = c->useHitbox ? 1 : 0;
                        float swing = c->swingTime, hs = c->hitStart, he = c->hitEnd, kb = c->knockback;
                        float cw = c->comboWindow, cb = c->comboBonus;
                        float bw = c->hitboxWidth, bh = c->hitboxHeight, bd = c->hitboxDepth;
                        if (in >> swing >> hs >> he >> kb >> combo >> cw >> cb >> hb >> bw >> bh >> bd) {
                            c->swingTime = swing; c->hitStart = hs; c->hitEnd = he; c->knockback = kb;
                            c->comboSteps = combo; c->comboWindow = cw; c->comboBonus = cb;
                            c->useHitbox = hb != 0; c->hitboxWidth = bw; c->hitboxHeight = bh; c->hitboxDepth = bd;
                        }
                    }
                    in >> std::ws;
                    if (in.peek() == '"') {
                        c->weapon = ReadQuoted(in);
                        in >> std::ws;
                        if (in.peek() == '"') c->weaponFile = ReadQuoted(in);
                    }
                } else if (field == "gun") {
                    auto* g = go->AddComponent<RangedWeapon>();
                    KvIn k(in);
                    k.f("dmg", g->damage); k.f("rate", g->fireRate); k.f("range", g->range);
                    k.i("mag", g->magazine); k.i("ammo", g->reserve); k.f("reload", g->reloadTime);
                    k.f("kb", g->knockback); k.f("spread", g->spread); k.b("hitscan", g->hitscan);
                    k.b("auto", g->autoReload); k.key("fireKey", g->fireKey); k.i("fireBtn", g->fireButton);
                    k.key("reloadKey", g->reloadKey); k.f("muz", g->muzzleHeight); k.f("speed", g->projectileSpeed);
                    k.str("weapon", g->weapon); k.str("file", g->weaponFile); k.str("proj", g->projectile);
                } else if (field == "spawner") {
                    auto* c = go->AddComponent<Spawner>();
                    c->templateName = ReadQuoted(in);
                    int dt = 1;
                    in >> c->interval >> c->maxAlive >> c->totalToSpawn >> c->spawnRadius
                       >> c->startDelay >> dt;
                    c->deactivateTemplate = (dt != 0);
                    // Trailing wave fields (newer scenes only).
                    in >> std::ws;
                    if (std::isdigit((unsigned char)in.peek())) {
                        int as = 1;
                        in >> c->count >> c->waves >> c->waveDelay >> as;
                        c->autoStart = (as != 0);
                        in >> std::ws;
                        if (in.peek() == '"') {
                            c->prefabPath = ReadQuoted(in);
                            in >> std::ws;
                            if (std::isdigit(in.peek())) { int up = 0; in >> up; c->usePool = (up != 0); }
                        }
                    }
                } else if (field == "craftmenu") {
                    auto* c = go->AddComponent<CraftingMenu>();
                    int key = 'c', op = 0, anch = 0;
                    in >> key >> op >> c->position.x >> c->position.y >> c->buttonSize.x
                       >> c->buttonSize.y >> c->spacing >> anch;
                    c->toggleKey = (char)key; c->open = (op != 0); c->anchor = (UIAnchor)anch;
                } else if (field == "crafting") {
                    auto* c = go->AddComponent<Crafting>();
                    int n = 0; in >> n;
                    for (int i = 0; i < n && in; ++i) {
                        Crafting::Recipe r;
                        r.output = ItemId(ReadQuoted(in)); in >> r.outputCount;
                        int ni = 0; in >> ni;
                        for (int j = 0; j < ni && in; ++j) {
                            Crafting::Ingredient ing;
                            ing.item = ItemId(ReadQuoted(in)); in >> ing.count;
                            r.inputs.push_back(ing);
                        }
                        c->recipes.push_back(std::move(r));
                    }
                    in >> std::ws;   // optional recipe book + recipe extras
                    if (in.peek() == '"') { c->recipeBook = ReadQuoted(in); ReadRecipeExtras(in, c->recipes); }
                } else if (field == "fpfeel" || field == "pcfirstfeel") {
                    if (auto* fp = NthOf<FirstPersonController>(go, nFpFeel)) {
                        int z = 1, sl = 1, sm = 1, ik = 1;
                        in >> fp->lookSmoothing >> fp->sprintFovBoost >> fp->fovLerp >> z >> fp->zoomFov >> fp->zoomSensitivity
                           >> fp->landDip >> sl >> fp->slideSpeed >> fp->slideTime >> fp->slideEyeHeight >> sm >> ik;
                        fp->zoomWithRightMouse = z != 0; fp->canSlide = sl != 0; fp->driveStateMachine = sm != 0; fp->ikLook = ik != 0;
                        // Optional head bob + eye smoothing block (absent in older scenes).
                        int hb = fp->headBob ? 1 : 0;
                        float ba = fp->bobAmount, bs = fp->bobSpeed, bw = fp->bobSway, br = fp->bobRunScale,
                              bb = fp->bobBlend, bg = fp->bobGroundGrace, es = fp->eyeStepSmoothing;
                        in >> std::ws;
                        if (std::isdigit(in.peek()) && (in >> hb >> ba >> bs >> bw >> br >> bb >> bg >> es)) {
                            fp->headBob = hb != 0; fp->bobAmount = ba; fp->bobSpeed = bs; fp->bobSway = bw;
                            fp->bobRunScale = br; fp->bobBlend = bb; fp->bobGroundGrace = bg; fp->eyeStepSmoothing = es;
                        }
                        in >> std::ws;   // optional dodge (off unless a game turns it on)
                        if (std::isdigit(in.peek())) {
                            int on = 0, key = 0, ifr = 0;
                            in >> on >> key >> fp->dodge.impulse >> fp->dodge.duration >> fp->dodge.cooldown
                               >> fp->dodge.stamina >> ifr;
                            fp->dodge.canDodge = on != 0; fp->dodge.key = (char)key; fp->dodge.iFrames = ifr != 0;
                        }
                        int cv = 0, cm = 0, cl = 0;
                        in >> std::ws;
                        if (std::isdigit(static_cast<unsigned char>(in.peek())) && (in >> cv >> cm >> cl)) {
                            fp->canVault = cv != 0; fp->canMantle = cm != 0; fp->canLedgeGrab = cl != 0;
                        }
                    }
                } else if (field == "tpfeel" || field == "pcthirdfeel") {
                    if (auto* tp = NthOf<ThirdPersonController>(go, nTpFeel)) {
                        int a = 1, rc = 0, sl = 1, sm = 1, ik = 1;
                        in >> tp->lookSmoothing >> tp->sprintFovBoost >> tp->fovLerp >> a >> tp->aimDistance >> tp->aimShoulder
                           >> tp->aimFov >> tp->aimSensitivity >> tp->aimLerp >> rc >> tp->recenterDelay >> tp->recenterSpeed
                           >> tp->landShake >> tp->strideLength >> sl >> tp->slideSpeed >> tp->slideTime >> sm >> ik;
                        tp->aimWithRightMouse = a != 0; tp->autoRecenter = rc != 0; tp->canSlide = sl != 0;
                        tp->driveStateMachine = sm != 0; tp->ikLook = ik != 0;
                        in >> std::ws;   // optional dodge (off unless a game turns it on)
                        if (std::isdigit(in.peek())) {
                            int on = 0, key = 0, ifr = 0;
                            in >> on >> key >> tp->dodge.impulse >> tp->dodge.duration >> tp->dodge.cooldown
                               >> tp->dodge.stamina >> ifr;
                            tp->dodge.canDodge = on != 0; tp->dodge.key = (char)key; tp->dodge.iFrames = ifr != 0;
                        }
                        int cv = 0, cm = 0, cl = 0;
                        in >> std::ws;
                        if (std::isdigit(static_cast<unsigned char>(in.peek())) && (in >> cv >> cm >> cl)) {
                            tp->canVault = cv != 0; tp->canMantle = cm != 0; tp->canLedgeGrab = cl != 0;
                        }
                        int cmode = -1;
                        in >> std::ws;
                        if (std::isdigit(static_cast<unsigned char>(in.peek())) && (in >> cmode)) {
                            if (cmode < 0) cmode = 0;
                            if (cmode > 2) cmode = 2;
                            tp->cursorMode = cmode;
                            tp->lockCursor = cmode != 0;
                        }
                    }
                } else if (field == "playercontroller") {
                    // One controller for both views. Its sections (pcfirst..., pcthird...)
                    // follow; a view the player does not have has none.
                    KvIn k(in);
                    int mode = (int)PlayerController::ViewMode::Both;
                    k.i("view", mode);
                    auto* pc = go->AddComponent<PlayerController>(
                        mode <= 0 ? PlayerController::ViewMode::First
                        : mode == 1 ? PlayerController::ViewMode::Third : PlayerController::ViewMode::Both);
                    if (pc->viewMode() == PlayerController::ViewMode::Both) {
                        auto* pv = go->AddComponent<PlayerView>();
                        ReadPlayerViewKeys(k, pv, "start");
                    }
                } else if (field == "playerview") {
                    // grok/view-toggle. key=value record; missing keys keep the defaults.
                    auto* pv = go->AddComponent<PlayerView>();
                    KvIn k(in);
                    ReadPlayerViewKeys(k, pv, "view");
                } else if (field == "uitheme") {
                    // grok/ui-theme. Missing keys keep the Studio defaults.
                    auto* th = go->AddComponent<UITheme>();
                    KvIn k(in);
                    k.str("name", th->themeName);
                    k.b("apply", th->applyOnStart);
                    k.i("preset", th->preset);
                    k.f("corner", th->cornerRadius);
                    k.i("mask", th->cornerMask);
                    k.f("border", th->borderWidth);
                    k.b("shadow", th->shadow);
                    k.f("soft", th->shadowSoftness);
                    k.f("ox", th->shadowOffset.x);
                    k.f("oy", th->shadowOffset.y);
                    k.f("pad", th->padding);
                    k.f("font", th->fontScale);
                    k.f("tpx", th->textPixelSize);
                    k.f("trans", th->transitionSpeed);
                    k.f("hscale", th->hoverScale);
                    k.f("glow", th->glow);
                    k.f("opa", th->opacity);
                    k.b("grad", th->gradient);
                    k.i("gdir", th->gradientDir);
                    k.b("sheen", th->sheen);
                    k.col("panel", th->panel);
                    k.col("pbot", th->panelBottom);
                    k.col("accent", th->accent);
                    k.col("text", th->text);
                    k.col("muted", th->muted);
                    k.col("button", th->button);
                    k.col("hover", th->buttonHover);
                    k.col("press", th->buttonPressed);
                    k.col("btext", th->buttonText);
                    k.col("off", th->disabled);
                    k.col("danger", th->danger);
                    k.col("ok", th->success);
                    k.col("edge", th->border);
                    k.col("sh", th->shadowCol);
                    k.col("glowc", th->glowColor);
                    k.col("fcol", th->focusColor);
                    k.f("fthick", th->focusThickness);
                    k.f("fpad", th->focusPadding);
                    k.b("fpulse", th->focusPulse);
                    k.str("fontPath", th->fontPath);
                    k.b("global", th->global);
                } else if (field == "uix") {
                    // Runtime UI extras: only the keys present are applied, so scenes saved before them load as they were.
                    // `i` (absent on the first, and on every older file) picks which instance.
                    KvIn k(in);
                    int idx = 0; k.i("i", idx);
                    if (auto* pn = AtComp(go->GetComponents<UIPanel>(), idx)) k.b("pblock", pn->blocksPointer);
                    if (auto* rtx = AtComp(go->GetComponents<TextRenderer>(), idx)) k.b("rich", rtx->richText);
                    if (auto* tg = AtComp(go->GetComponents<UIToggle>(), idx)) k.str("rgroup", tg->radioGroup);
                    if (auto* sg = AtComp(go->GetComponents<UISlotGrid>(), idx)) { k.f("sgFont", sg->fontScale); k.b("sgNum", sg->numberEmptySlots); k.col("sgText", sg->textColor); }
                    if (auto* dg = AtComp(go->GetComponents<UIDraggable>(), idx)) { k.b("dragParent", dg->dragParent); k.b("keepOnScreen", dg->keepOnScreen); }
                } else if (field == "uivariant") {
                    auto* sv = go->AddComponent<UIStyleVariant>();
                    KvIn k(in);
                    k.i("variant", sv->variant);
                    k.str("vtheme", sv->themeName);
                    k.str("vstyle", sv->style); k.str("vsize", sv->styleSize); k.str("vdens", sv->styleDensity);
                } else if (field == "uidamage") {
                    auto* dn = go->AddComponent<UIDamageNumbers>();
                    KvIn k(in);
                    k.f("dnLife", dn->lifetime); k.f("dnRise", dn->riseSpeed); k.f("dnGrav", dn->gravity);
                    k.f("dnSpread", dn->spread); k.f("dnSize", dn->textSize); k.f("dnCrit", dn->critScale);
                    k.f("dnPop", dn->popScale); k.i("dnMax", dn->maxActive);
                    k.f("dnAtX", dn->spawnAt.x); k.f("dnAtY", dn->spawnAt.y);
                    k.col("dnNormal", dn->normalColor); k.col("dnCritC", dn->critColor); k.col("dnHeal", dn->healColor);
                    k.col("dnMiss", dn->missColor); k.col("dnInfo", dn->infoColor);
                } else if (field == "uiinvbind") {
                    auto* ib = go->AddComponent<UIInventoryBind>();
                    KvIn k(in);
                    k.str("inv", ib->inventoryName); k.b("fit", ib->fitRows); k.f("fscale", ib->slotFontScale);
                } else if (field == "uidialoguebox") {
                    auto* db = go->AddComponent<UIDialogueBox>();
                    KvIn k(in);
                    k.f("dbCps", db->charsPerSecond); k.b("dbHide", db->hideWhenDone);
                    k.b("dbClick", db->advanceOnClick); k.b("dbStartHidden", db->startHidden);
                } else if (field == "fpctrl" || field == "pcfirst") {
                    float ws = 4.5f, rs = 8, jf = 6, ms = 0.15f; int cj = 1, da = 1, iy = 0;
                    in >> ws >> rs >> jf >> ms >> cj >> da;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> iy;   // optional invertY
                    auto* fp = go->AddComponent<FirstPersonController>();
                    fp->walkSpeed = ws; fp->runSpeed = rs; fp->jumpForce = jf;
                    fp->mouseSensitivity = ms; fp->canJump = (cj != 0); fp->driveAnimation = (da != 0);
                    fp->invertY = (iy != 0);
                    // Optional momentum + jump-feel block (absent in older scenes).
                    float ac = fp->acceleration, de = fp->deceleration, aircon = fp->airControl,
                          coy = fp->coyoteTime, jb = fp->jumpBufferTime;
                    in >> std::ws;
                    if ((std::isdigit(in.peek()) || in.peek() == '-') &&
                        (in >> ac >> de >> aircon >> coy >> jb)) {
                        fp->acceleration = ac; fp->deceleration = de; fp->airControl = aircon;
                        fp->coyoteTime = coy; fp->jumpBufferTime = jb;
                        // Optional run + stance block (absent in older scenes).
                        int sk = (unsigned char)fp->sprintKey, tr = fp->toggleRun ? 1 : 0,
                            ck = (unsigned char)fp->crouchKey, pk = (unsigned char)fp->proneKey,
                            tst = fp->toggleStance ? 1 : 0;
                        float csp = fp->crouchSpeed, psp = fp->proneSpeed, seh = fp->standEyeHeight,
                              ceh = fp->crouchEyeHeight, peh = fp->proneEyeHeight, sl = fp->stanceLerp;
                        in >> std::ws;
                        if ((std::isdigit(in.peek()) || in.peek() == '-') &&
                            (in >> sk >> tr >> ck >> pk >> tst >> csp >> psp >> seh >> ceh >> peh >> sl)) {
                            fp->sprintKey = (char)sk; fp->toggleRun = (tr != 0);
                            fp->crouchKey = (char)ck; fp->proneKey = (char)pk; fp->toggleStance = (tst != 0);
                            fp->crouchSpeed = csp; fp->proneSpeed = psp; fp->standEyeHeight = seh;
                            fp->crouchEyeHeight = ceh; fp->proneEyeHeight = peh; fp->stanceLerp = sl;
                            // Optional lean block.
                            int ll = (unsigned char)fp->leanLeftKey, lr = (unsigned char)fp->leanRightKey;
                            float la = fp->leanAngle, lo = fp->leanOffset, lsp = fp->leanSpeed;
                            in >> std::ws;
                            if ((std::isdigit(in.peek()) || in.peek() == '-') &&
                                (in >> ll >> lr >> la >> lo >> lsp)) {
                                fp->leanLeftKey = (char)ll; fp->leanRightKey = (char)lr;
                                fp->leanAngle = la; fp->leanOffset = lo; fp->leanSpeed = lsp;
                                in >> std::ws;
                                if (std::isdigit(in.peek())) { int mj = fp->maxJumps; in >> mj; fp->maxJumps = mj;
                                    in >> std::ws; if (std::isdigit(in.peek())) { int fik = 0; in >> fik; fp->footIK = (fik != 0); } }
                            }
                        }
                    }
                } else if (field == "tpctrl" || field == "pcthird") {
                    float ws = 4.5f, rs = 8, jf = 6, ms = 0.2f, ts = 12, ds = 5, ch = 1.5f; int cj = 1, da = 1;
                    in >> ws >> rs >> jf >> ms >> ts >> ds >> ch >> cj >> da;
                    auto* tp = go->AddComponent<ThirdPersonController>();
                    tp->walkSpeed = ws; tp->runSpeed = rs; tp->jumpForce = jf; tp->mouseSensitivity = ms;
                    tp->turnSpeed = ts; tp->distance = ds; tp->cameraHeight = ch;
                    tp->canJump = (cj != 0); tp->driveAnimation = (da != 0);
                    // extended (optional): defaults preserved when absent in older scenes
                    int ix = 0, iy = 0, fmode = (int)tp->faceMode;
                    float mnd = tp->minDistance, mxd = tp->maxDistance, zs = tp->zoomSpeed,
                          so = tp->shoulderOffset, cd = tp->pivotDampingXZ,
                          mnp = tp->minPitch, mxp = tp->maxPitch;
                    if (in >> ix >> iy >> mnd >> mxd >> zs >> so >> cd >> mnp >> mxp >> fmode) {
                        tp->invertX = (ix != 0); tp->invertY = (iy != 0);
                        tp->minDistance = mnd; tp->maxDistance = mxd; tp->zoomSpeed = zs;
                        tp->shoulderOffset = so;
                        // The old camera damping: now the pivot's horizontal damping
                        // (0 = instant still means instant, vertically too).
                        tp->pivotDampingXZ = cd;
                        if (cd <= 0.0f) tp->pivotDampingY = 0.0f;
                        tp->minPitch = mnp; tp->maxPitch = mxp;
                        tp->faceMode = (ThirdPersonController::FaceMode)fmode;
                        // Optional momentum + jump-feel block (absent in older scenes).
                        float ac = tp->acceleration, de = tp->deceleration, aircon = tp->airControl,
                              coy = tp->coyoteTime, jb = tp->jumpBufferTime;
                        if (in >> ac >> de >> aircon >> coy >> jb) {
                            tp->acceleration = ac; tp->deceleration = de; tp->airControl = aircon;
                            tp->coyoteTime = coy; tp->jumpBufferTime = jb;
                            // Optional camera-collision block (absent in older scenes).
                            int cc = tp->cameraCollision ? 1 : 0; float skin = tp->cameraCollisionSkin;
                            if (in >> cc >> skin) {
                                tp->cameraCollision = (cc != 0); tp->cameraCollisionSkin = skin;
                                // Optional run + stance block.
                                int sk = (unsigned char)tp->sprintKey, tr = tp->toggleRun ? 1 : 0,
                                    ck = (unsigned char)tp->crouchKey, pk = (unsigned char)tp->proneKey,
                                    tst = tp->toggleStance ? 1 : 0;
                                float csp = tp->crouchSpeed, psp = tp->proneSpeed,
                                      chd = tp->crouchHeightDrop, phd = tp->proneHeightDrop, sl = tp->stanceLerp;
                                in >> std::ws;
                                if ((std::isdigit(in.peek()) || in.peek() == '-') &&
                                    (in >> sk >> tr >> ck >> pk >> tst >> csp >> psp >> chd >> phd >> sl)) {
                                    tp->sprintKey = (char)sk; tp->toggleRun = (tr != 0);
                                    tp->crouchKey = (char)ck; tp->proneKey = (char)pk; tp->toggleStance = (tst != 0);
                                    tp->crouchSpeed = csp; tp->proneSpeed = psp;
                                    tp->crouchHeightDrop = chd; tp->proneHeightDrop = phd; tp->stanceLerp = sl;
                                    // Optional lean block.
                                    int ll = (unsigned char)tp->leanLeftKey, lr = (unsigned char)tp->leanRightKey;
                                    float la = tp->leanAngle, lo = tp->leanOffset, lsp = tp->leanSpeed;
                                    in >> std::ws;
                                    if ((std::isdigit(in.peek()) || in.peek() == '-') &&
                                        (in >> ll >> lr >> la >> lo >> lsp)) {
                                        tp->leanLeftKey = (char)ll; tp->leanRightKey = (char)lr;
                                        tp->leanAngle = la; tp->leanOffset = lo; tp->leanSpeed = lsp;
                                        in >> std::ws;
                                        if (std::isdigit(in.peek())) { int mj = tp->maxJumps; in >> mj; tp->maxJumps = mj;
                                            in >> std::ws; if (std::isdigit(in.peek())) { int fik = 0; in >> fik; tp->footIK = (fik != 0);
                                                // Optional camera follow + spring arm block.
                                                float pxz = tp->pivotDampingXZ, py = tp->pivotDampingY, lag = tp->maxPivotLag,
                                                      os = tp->orbitSmoothing, rad = tp->cameraCollisionRadius,
                                                      pin = tp->cameraCollisionPullIn, rec = tp->cameraCollisionRecover;
                                                std::uint32_t mask = tp->cameraCollisionMask;
                                                in >> std::ws;
                                                if (std::isdigit(in.peek()) && (in >> pxz >> py >> lag >> os >> rad >> mask >> pin >> rec)) {
                                                    tp->pivotDampingXZ = pxz; tp->pivotDampingY = py; tp->maxPivotLag = lag;
                                                    tp->orbitSmoothing = os; tp->cameraCollisionRadius = rad; tp->cameraCollisionMask = mask;
                                                    tp->cameraCollisionPullIn = pin; tp->cameraCollisionRecover = rec;
                                                }
                                            } }
                                    }
                                }
                            }
                        }
                    }
                } else if (field == "tdctrl") {
                    auto* td = go->AddComponent<TopDownController>();
                    std::string sk = "-"; int da = 1, rf = 1, cr = 0;
                    in >> td->walkSpeed >> td->runSpeed >> sk >> da >> rf >> td->turnSpeed
                       >> td->acceleration >> td->deceleration >> cr
                       >> td->cameraDistance >> td->cameraPitch >> td->cameraYaw
                       >> td->lookHeight >> td->cameraDamping;
                    td->sprintKey = KeyFromToken(sk);
                    td->driveAnimation = (da != 0); td->rotateToFace = (rf != 0); td->cameraRelative = (cr != 0);
                    in >> std::ws; if (std::isdigit(in.peek())) { int fik = 0; in >> fik; td->footIK = (fik != 0); }
                    if (LooksNumeric(in)) in >> td->stepOffset;   // step limit (added later; older scenes keep the default)
                } else if (field == "tdsctrl") {
                    auto* ts = go->AddComponent<TopDownShooterController>();
                    KvIn k(in);
                    k.f("walk", ts->walkSpeed); k.f("run", ts->runSpeed); k.key("sprint", ts->sprintKey);
                    k.f("accel", ts->acceleration); k.f("decel", ts->deceleration); k.f("backpedal", ts->backpedalMultiplier);
                    k.b("camRel", ts->cameraRelative); k.b("anim", ts->driveAnimation); k.i("aimMode", ts->aimMode);
                    k.f("turn", ts->turnSpeed); k.i("fireMode", ts->fireMode); k.i("fireBtn", ts->fireButton);
                    k.key("fireKey", ts->fireKey); k.i("firePad", ts->fireGamepadButton); k.b("auto", ts->autoFire);
                    k.f("rate", ts->fireRate); k.f("dmg", ts->damage); k.f("pspeed", ts->projectileSpeed);
                    k.f("plife", ts->projectileLifetime); k.f("range", ts->range); k.i("pellets", ts->pellets);
                    k.f("spread", ts->spread); k.f("force", ts->impactForce); k.f("bsize", ts->bulletSize);
                    k.col("bcol", ts->bulletColor); k.f("muzH", ts->muzzleHeight); k.f("muzF", ts->muzzleForward);
                    k.i("mag", ts->magazineSize); k.key("reloadKey", ts->reloadKey); k.f("reload", ts->reloadTime);
                    k.b("autoReload", ts->autoReload); k.key("dashKey", ts->dashKey); k.i("dashPad", ts->dashGamepadButton); k.f("dashSpeed", ts->dashSpeed);
                    k.f("dashTime", ts->dashTime); k.f("dashCd", ts->dashCooldown); k.b("dashInv", ts->dashInvulnerable);
                    k.f("camDist", ts->cameraDistance); k.f("camPitch", ts->cameraPitch); k.f("camYaw", ts->cameraYaw);
                    k.f("camDamp", ts->cameraDamping); k.f("lead", ts->aimLookAhead); k.f("maxLead", ts->maxLookAhead);
                    k.b("zoom", ts->wheelZoom); k.f("minDist", ts->minDistance); k.f("maxDist", ts->maxDistance);
                    k.f("shake", ts->fireShake); k.str("proj", ts->projectile); k.str("weapon", ts->weapon);
                    k.f("step", ts->stepOffset);   // step limit (added later; older scenes keep the default)
                } else if (field == "ragdoll") {
                    // The old Verlet Ragdoll (before 2026-11-25). It loads into the one Ragdoll: going limp on death and
                    // reacting to hits carry over; the particle settings (gravity, damping, friction, skin, iterations,
                    // speed cap, push sizes, recover time) have no meaning on the physics core and are dropped.
                    // Animated until it dies, as it was (no stumbling). An `activeragdoll` record of the same object,
                    // written after this one, fills this Ragdoll (its Adder) instead of adding a second one.
                    auto* rd = go->AddComponent<Ragdoll>();
                    rd->fromLegacyRecord = true;
                    rd->mode = 0;
                    rd->stumble = false;
                    KvIn k(in);
                    k.b("onDeath", rd->limpOnDeath);
                    k.b("hits", rd->reactToDamage);
                } else if (field == "fpstance" || field == "pcfirststance" || field == "tpstance" || field == "pcthirdstance") {
                    // Stance options for the controller declared just above (crouch/prone body).
                    KvIn k(in);
                    auto load = [&](auto* c) {
                        if (!c) return;
                        k.b("prone", c->allowProne); k.b("resize", c->resizeBody); k.f("crouchH", c->crouchHeight);
                        k.f("proneH", c->proneHeight); k.i("pad", c->stanceGamepadButton); k.f("hold", c->proneHoldTime);
                        k.f("walkA", c->walkAcceleration); k.f("steer", c->steerRate);
                        k.b("dive", c->diveOnProneWhileSprinting); k.f("diveHop", c->diveHop); k.f("diveSlide", c->diveSlideTime);
                        k.f("capSens", c->capturedSensitivity); k.f("capPx", c->capturedPixelCap);
                        k.f("slope", c->slopeLimit); k.f("step", c->stepOffset); k.f("fallY", c->fallResetY);
                        k.f("swim", c->swimSpeed); k.f("swimUp", c->swimUpSpeed); k.f("swimDown", c->swimDownSpeed);
                        k.b("varJump", c->variableJump); k.f("jumpCut", c->jumpCut); k.f("climb", c->climbSpeed); k.f("hardLand", c->hardLandSpeed); k.i("preset", c->controllerPreset); k.f("stickRamp", c->stickLookRamp);
                        // Saved crouched / prone: start that way (the collider was saved standing).
                        int st = 0; k.i("stance", st);
                        using S = typename std::remove_pointer_t<decltype(c)>::Stance;
                        if (st == (int)S::Crouch || st == (int)S::Prone) c->RestoreStance(static_cast<S>(st));
                    };
                    if (field == "fpstance" || field == "pcfirststance") load(NthOf<FirstPersonController>(go, nFpStance));
                    else                     load(NthOf<ThirdPersonController>(go, nTpStance));
                } else if (field == "cc3dstance") {
                    auto* c = NthOf<CharacterController3D>(go, nCc3dSt); ReadStanceControl(in, c ? &c->stanceControl : nullptr);
                } else if (field == "tpsstance") {
                    auto* c = NthOf<ThirdPersonShooterController>(go, nTpsSt); ReadStanceControl(in, c ? &c->stanceControl : nullptr);
                } else if (field == "tdstance") {
                    auto* c = NthOf<TopDownController>(go, nTdSt); ReadStanceControl(in, c ? &c->stanceControl : nullptr);
                } else if (field == "ctmstance") {
                    auto* c = NthOf<ClickToMoveController>(go, nCtmSt); ReadStanceControl(in, c ? &c->stanceControl : nullptr);
                } else if (field == "npcstance") {
                    auto* c = NthOf<NPCController>(go, nNpcSt); ReadStanceControl(in, c ? &c->stanceControl : nullptr);
                } else if (field == "p25ctrl") {
                    auto* pc = go->AddComponent<Platformer25DController>();
                    KvIn k(in);
                    k.f("walk", pc->walkSpeed); k.f("run", pc->runSpeed); k.key("sprint", pc->sprintKey);
                    k.f("accel", pc->acceleration); k.f("decel", pc->deceleration); k.f("air", pc->airControl);
                    k.b("anim", pc->driveAnimation); k.b("jump", pc->canJump); k.key("jumpKey", pc->jumpKey);
                    k.f("jumpForce", pc->jumpForce); k.i("jumps", pc->maxJumps); k.f("cut", pc->jumpCut);
                    k.f("fallG", pc->fallGravity); k.f("maxFall", pc->maxFallSpeed); k.f("coyote", pc->coyoteTime);
                    k.f("buffer", pc->jumpBufferTime); k.i("layers", pc->layerCount); k.f("spacing", pc->layerSpacing);
                    k.f("frontZ", pc->frontLayerZ); k.i("layer", pc->layer); k.key("backKey", pc->layerBackKey);
                    k.key("frontKey", pc->layerFrontKey); k.f("switch", pc->layerSwitchSpeed);
                    k.b("checkFree", pc->checkLayerFree); k.f("radius", pc->bodyRadius); k.f("turn", pc->turnSpeed);
                    k.f("faceCam", pc->faceCameraAngle); k.b("grab", pc->canGrab); k.key("grabKey", pc->grabKey);
                    k.i("grabPad", pc->grabGamepadButton); k.f("reach", pc->grabReach); k.f("pull", pc->grabPullSpeed);
                    k.b("hang", pc->grabHang); k.f("step", pc->stepHeight); k.f("stickRun", pc->stickRunThreshold);
                    k.i("jumpPad", pc->jumpGamepadButton); k.b("climb", pc->ledgeClimb); k.f("climbTime", pc->climbTime);
                    k.f("camDist", pc->cameraDistance); k.f("camH", pc->cameraHeight);
                    k.f("camPitch", pc->cameraPitch); k.f("lead", pc->lookAhead); k.f("camDamp", pc->cameraDamping);
                    k.f("vDamp", pc->verticalDamping); k.f("speedZoom", pc->speedZoom);
                    k.b("followLayer", pc->cameraFollowsLayer);
                    k.key("crouchKey", pc->crouchKey); k.key("proneKey", pc->proneKey); k.b("toggleStance", pc->toggleStance);
                    k.f("crouchSpeed", pc->crouchSpeed); k.f("proneSpeed", pc->proneSpeed); k.b("allowProne", pc->allowProne);
                    k.b("resize", pc->resizeBody); k.f("crouchH", pc->crouchHeight); k.f("proneH", pc->proneHeight);
                    k.i("stancePad", pc->stanceGamepadButton); k.f("proneHold", pc->proneHoldTime);
                    // Saved crouched / prone: start that way (the collider was saved standing).
                    using S = Platformer25DController::Stance;
                    int st = 0; k.i("stance", st);
                    if (st == (int)S::Crouch || st == (int)S::Prone) pc->RestoreStance(static_cast<S>(st));
                } else if (field == "proj3d") {
                    auto* pj = go->AddComponent<Projectile3D>();
                    KvIn k(in);
                    k.f("vx", pj->velocity.x); k.f("vy", pj->velocity.y); k.f("vz", pj->velocity.z);
                    k.f("dmg", pj->damage); k.f("life", pj->lifetime); k.f("grav", pj->gravityScale);
                    k.f("force", pj->impactForce); k.i("pierce", pj->pierce); k.b("bounce", pj->bounce);
                    k.i("bounces", pj->maxBounces);
                } else if (field == "tdctrl2d") {
                    auto* t2 = go->AddComponent<TopDownController2D>();
                    std::string sk = "-", dk = "-"; int nd = 1, ug = 1, da = 1, cb = 0, sw = 0;
                    in >> t2->speed >> t2->runSpeed >> sk >> nd >> ug
                       >> t2->acceleration >> t2->deceleration
                       >> dk >> t2->dashSpeed >> t2->dashDuration >> t2->dashCooldown
                       >> t2->faceMode >> t2->spriteForward >> t2->turnSpeed >> da
                       >> cb >> t2->boundsMin.x >> t2->boundsMin.y >> t2->boundsMax.x >> t2->boundsMax.y >> sw;
                    t2->sprintKey = KeyFromToken(sk);
                    t2->dashKey   = KeyFromToken(dk);
                    t2->normalizeDiagonal = (nd != 0); t2->useGamepad = (ug != 0);
                    t2->driveAnimation = (da != 0); t2->clampBounds = (cb != 0); t2->screenWrap = (sw != 0);
                    // Combat fields (appended later): only present if the next token is numeric.
                    in >> std::ws; int pk = in.peek();
                    if (pk == '-' || pk == '.' || std::isdigit(pk)) {
                        std::string fk = "-";
                        in >> t2->knockbackTime >> fk >> t2->fireButton >> t2->projectileSpeed >> t2->fireRate;
                        t2->fireKey = KeyFromToken(fk);
                        in >> std::ws; if (in.peek() == '"') t2->projectile = ReadQuoted(in);
                    }
                } else if (field == "frctrl") {
                    auto* fr = go->AddComponent<FreeRoamController>();
                    int sk = (unsigned char)fr->sprintKey, uk = (unsigned char)fr->upKey,
                        dk = (unsigned char)fr->downKey, iy = 0, lc = 0, rr = 1;
                    in >> fr->moveSpeed >> fr->boostMultiplier >> sk >> uk >> dk
                       >> fr->mouseSensitivity >> iy >> lc >> rr
                       >> fr->minPitch >> fr->maxPitch >> fr->acceleration >> fr->yaw >> fr->pitch;
                    fr->sprintKey = (char)sk; fr->upKey = (char)uk; fr->downKey = (char)dk;
                    fr->invertY = (iy != 0); fr->lockCursor = (lc != 0); fr->lookRequiresRightMouse = (rr != 0);
                } else if (field == "tpsctrl") {
                    auto* ts = go->AddComponent<ThirdPersonShooterController>();
                    std::string sk = "-"; int cj = 1, da = 1, iy = 0, cc = 1, af = 0, lc = 1;
                    in >> ts->walkSpeed >> ts->runSpeed >> ts->jumpForce >> sk >> cj >> da >> ts->turnSpeed
                       >> ts->acceleration >> ts->deceleration >> ts->airControl
                       >> ts->coyoteTime >> ts->jumpBufferTime
                       >> ts->mouseSensitivity >> iy >> ts->minPitch >> ts->maxPitch >> ts->distance >> ts->cameraHeight
                       >> ts->shoulderOffset >> cc >> ts->cameraCollisionSkin
                       >> ts->aimButton >> ts->aimDistance >> ts->aimShoulder >> ts->aimSpeed
                       >> ts->fireButton >> af >> ts->fireRate >> lc;
                    ts->sprintKey = KeyFromToken(sk);
                    ts->canJump = (cj != 0); ts->driveAnimation = (da != 0); ts->invertY = (iy != 0);
                    ts->cameraCollision = (cc != 0); ts->autoFire = (af != 0); ts->lockCursor = (lc != 0);
                    // Optional lean block (absent in older scenes).
                    int ll = (unsigned char)ts->leanLeftKey, lr = (unsigned char)ts->leanRightKey;
                    float la = ts->leanAngle, lo = ts->leanOffset, lsp = ts->leanSpeed;
                    in >> std::ws;
                    if ((std::isdigit(in.peek()) || in.peek() == '-') &&
                        (in >> ll >> lr >> la >> lo >> lsp)) {
                        ts->leanLeftKey = (char)ll; ts->leanRightKey = (char)lr;
                        ts->leanAngle = la; ts->leanOffset = lo; ts->leanSpeed = lsp;
                        in >> std::ws;
                        if (std::isdigit(in.peek())) { int mj = ts->maxJumps; in >> mj; ts->maxJumps = mj;
                            in >> std::ws; if (std::isdigit(in.peek())) { int fik = 0; in >> fik; ts->footIK = (fik != 0);
                                // Optional camera follow + spring arm block.
                                float pxz = ts->pivotDampingXZ, py = ts->pivotDampingY, lag = ts->maxPivotLag,
                                      rad = ts->cameraCollisionRadius, pin = ts->cameraCollisionPullIn,
                                      rec = ts->cameraCollisionRecover;
                                std::uint32_t mask = ts->cameraCollisionMask;
                                in >> std::ws;
                                if (std::isdigit(in.peek()) && (in >> pxz >> py >> lag >> rad >> mask >> pin >> rec)) {
                                    ts->pivotDampingXZ = pxz; ts->pivotDampingY = py; ts->maxPivotLag = lag;
                                    ts->cameraCollisionRadius = rad; ts->cameraCollisionMask = mask;
                                    ts->cameraCollisionPullIn = pin; ts->cameraCollisionRecover = rec;
                                    // Optional trailing step height (older scenes keep 0.35).
                                    float so = ts->stepOffset;
                                    in >> std::ws;
                                    if ((std::isdigit(in.peek()) || in.peek() == '-' || in.peek() == '.') && (in >> so)) {
                                        ts->stepOffset = so;
                                        float wa = ts->walkAcceleration, st = ts->steerRate;
                                        in >> std::ws;
                                        if ((std::isdigit(in.peek()) || in.peek() == '-' || in.peek() == '.') && (in >> wa >> st)) {
                                            ts->walkAcceleration = wa; ts->steerRate = st;
                                        }
                                    }
                                }
                            } }
                    }
                } else if (field == "mover") {
                    Vec3 v; in >> v.x >> v.y >> v.z;
                    auto* mv = go->AddComponent<Mover>();
                    mv->velocity = v;
                    in >> std::ws;   // optional: the space (0 local, 1 world, 2 self); anything else is the default
                    if (std::isdigit(in.peek()) || in.peek() == '-') { int sp = 0; in >> sp; mv->space = (sp >= 0 && sp <= 2) ? static_cast<Space>(sp) : Space::Local; }
                } else if (field == "spinner") {
                    Vec3 v; in >> v.x >> v.y >> v.z;
                    auto* sp = go->AddComponent<Spinner>();
                    sp->angularVelocity = v;
                    in >> std::ws;   // optional: the space, as for mover
                    if (std::isdigit(in.peek()) || in.peek() == '-') { int sv = 0; in >> sv; sp->space = (sv >= 0 && sv <= 2) ? static_cast<Space>(sv) : Space::Self; }
                } else if (field == "lifetime") {
                    float s = 1.0f; in >> s;
                    auto* lt = go->AddComponent<Lifetime>();
                    lt->seconds = s;
                    in >> std::ws;   // optional: seconds already lived (a saved game picks up where it was)
                    if (std::isdigit(in.peek()) || in.peek() == '-' || in.peek() == '.') { float el = 0.0f; in >> el; lt->SetElapsed(el); }
                } else if (field == "collectible") {
                    auto* co = go->AddComponent<Collectible>();
                    co->scoreVar = ReadQuoted(in);
                    int rs = 0; in >> co->points >> co->heal >> rs >> co->respawnDelay;
                    co->respawn = (rs != 0);
                    in >> std::ws; if (in.peek() == '"') co->collectorTag = ReadQuoted(in);
                } else if (field == "damagetouch") {
                    auto* dt = go->AddComponent<DamageOnTouch>();
                    int once = 0, ds = 0;
                    in >> dt->damage >> dt->interval >> once >> dt->knockback >> ds;
                    dt->once = (once != 0); dt->destroySelf = (ds != 0);
                    in >> std::ws; if (in.peek() == '"') dt->targetTag = ReadQuoted(in);
                } else if (field == "teleporter") {
                    auto* tp = go->AddComponent<Teleporter>();
                    tp->targetName = ReadQuoted(in);
                    in >> tp->destination.x >> tp->destination.y >> tp->destination.z >> tp->cooldown;
                    in >> std::ws; if (in.peek() == '"') tp->triggerTag = ReadQuoted(in);
                } else if (field == "jumppad") {
                    auto* jp = go->AddComponent<JumpPad>();
                    int uo = 1;
                    in >> jp->force >> uo >> jp->forwardBoost >> jp->cooldown;
                    jp->useObjectUp = (uo != 0);
                    jp->triggerTag = ReadQuoted(in);
                } else { fieldDone = false; }
                }
                if (!fieldDone) { fieldDone = true;
                if (field == "triggerzone") {
                    auto* tz = go->AddComponent<TriggerZone>();
                    int once = 1; in >> tz->action;
                    tz->varName = ReadQuoted(in);
                    in >> tz->amount >> once;
                    tz->once = (once != 0);
                    tz->targetName = ReadQuoted(in);
                    in >> std::ws; if (in.peek() == '"') tz->triggerTag = ReadQuoted(in);
                } else if (field == "stats") {
                    auto* st = go->AddComponent<Stats>();
                    in >> st->health >> st->maxHealth >> st->mana >> st->maxMana
                       >> st->level >> st->xp >> st->xpToNext
                       >> st->strength >> st->defense >> st->intelligence >> st->agility;
                } else if (field == "inventory") {
                    auto* inv = go->AddComponent<Inventory>();
                    std::size_t count = 0; in >> inv->capacity >> count;
                    inv->slots.clear();
                    for (std::size_t k = 0; k < count && in; ++k) {
                        Inventory::Slot s; s.item = ItemId(ReadQuoted(in)); in >> s.count;
                        inv->slots.push_back(s);
                    }
                    in >> std::ws;   // optional max stack size (appended later)
                    int mpk = in.peek();
                    if (std::isdigit(mpk) || mpk == '-') {
                        in >> inv->maxStack;
                        ReadInstanceData(in, inv->slots.size(), [&](std::size_t i) -> ItemInstanceData& { return inv->slots[i].data; });
                        in >> std::ws;   // optional weight limit + slow-down
                        const int wpk = in.peek();
                        if (std::isdigit(wpk) || wpk == '.' || wpk == '-') {
                            int slow = 0;
                            in >> inv->weightLimit >> slow >> inv->overWeightSpeed;
                            inv->slowWhenOverWeight = (slow != 0);
                            in >> std::ws;   // optional keep-on-scene-load
                            if (std::isdigit(in.peek())) { int keep = 0; in >> keep; inv->keepOnSceneLoad = (keep != 0); }
                            in >> std::ws;   // optional enforced weight limit
                            if (std::isdigit(in.peek())) { int ew = 0; in >> ew; inv->enforceWeight = (ew != 0); }
                        }
                    }
                } else if (field == "lootsource") {
                    auto* ls = go->AddComponent<LootSource>();
                    KvIn k(in);
                    int w = 0;
                    k.str("table", ls->table); k.i("when", w); k.f("refill", ls->refill); k.i("seed", ls->seed);
                    ls->when = (LootSource::When)std::max(0, std::min(w, 2));
                } else if (field == "equipment") {
                    auto* eq = go->AddComponent<Equipment>();
                    KvIn k(in);
                    k.b("show", eq->showOnCharacter); k.b("keep", eq->keepOnSceneLoad);
                    int n = -1; k.i("n", n);
                    if (n >= 0) eq->slots.assign((std::size_t)std::min(n, 64), Equipment::Slot{});
                    for (int i = 0; i < (int)eq->slots.size(); ++i) {
                        auto& s = eq->slots[i];
                        const std::string p = "s" + std::to_string(i);
                        int acc = 0;
                        k.str((p + "name").c_str(), s.name); k.i((p + "acc").c_str(), acc); k.str((p + "sock").c_str(), s.socket);
                        s.accepts = (EquipSlot)std::max(0, std::min(acc, (int)EquipSlot::OffHand));
                        k.str((p + "item").c_str(), s.item);
                        if (s.item.empty()) continue;
                        s.item = ItemId(s.item);
                        k.f((p + "dur").c_str(), s.data.durability);
                        if (const std::string* u = k.find((p + "uid").c_str())) s.data.uid = std::strtoull(u->c_str(), nullptr, 10);
                        int nv = 0; k.i((p + "vals").c_str(), nv);
                        for (int v = 0; v < nv && v < 256; ++v) {
                            std::string key, val;
                            k.str((p + "k" + std::to_string(v)).c_str(), key); k.str((p + "v" + std::to_string(v)).c_str(), val);
                            s.data.values.push_back({key, val});
                        }
                    }
                } else if (field == "itempickup") {
                    auto* pk = go->AddComponent<ItemPickup>();
                    KvIn k(in);
                    k.str("item", pk->item); k.i("count", pk->count); k.str("tag", pk->collectorTag); k.b("touch", pk->pickupOnTouch);
                    k.f("auto", pk->autoRadius); k.key("key", pk->interactKey); k.f("range", pk->interactRange);
                    k.f("magnet", pk->magnetRadius); k.f("magnetSpeed", pk->magnetSpeed); k.b("hold", pk->holdInHands);
                    pk->item = ItemId(pk->item);
                    k.f("dur", pk->data.durability);
                    if (const std::string* u = k.find("uid")) pk->data.uid = std::strtoull(u->c_str(), nullptr, 10);
                    int nv = 0; k.i("vals", nv);
                    for (int v = 0; v < nv && v < 256; ++v) {
                        std::string key, val;
                        k.str(("k" + std::to_string(v)).c_str(), key); k.str(("v" + std::to_string(v)).c_str(), val);
                        pk->data.values.push_back({key, val});
                    }
                } else if (field == "turnmgr") {
                    auto* tm = go->AddComponent<TurnManager>();
                    int as = 1; std::size_t count = 0;
                    in >> tm->current >> tm->round >> as >> count;
                    tm->autoStart = (as != 0);
                    tm->participants.clear();
                    for (std::size_t k = 0; k < count && in; ++k) tm->participants.push_back(ReadQuoted(in));
                } else if (field == "camerafollow") {
                    std::string tgt = ReadQuoted(in);
                    Vec3 off; float sm = 5.0f;
                    in >> off.x >> off.y >> off.z >> sm;
                    auto* cf = go->AddComponent<CameraFollow>();
                    cf->targetName = tgt; cf->offset = off; cf->smoothing = sm;
                } else if (field == "vcam") {
                    auto* vc = go->AddComponent<VirtualCamera>();
                    in >> vc->priority;
                    vc->follow = ReadQuoted(in);
                    vc->lookAt = ReadQuoted(in);
                    in >> vc->followOffset.x >> vc->followOffset.y >> vc->followOffset.z
                       >> vc->positionDamping >> vc->rotationDamping
                       >> vc->fieldOfView >> vc->shakeAmplitude >> vc->shakeFrequency;
                    // Optional extended fields (absent in older scenes).
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '-') {
                        int bm = 0;
                        in >> bm >> vc->lookAtOffset.x >> vc->lookAtOffset.y >> vc->lookAtOffset.z
                           >> vc->aimDeadZone >> vc->impulseDecay;
                        vc->bindingMode = (VirtualCamera::BindingMode)bm;
                    }
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '-') {
                        int fl = 0, oi = 1;
                        in >> fl >> vc->orbitYaw >> vc->orbitPitch >> vc->orbitRadius >> vc->orbitHeight
                           >> vc->orbitMinPitch >> vc->orbitMaxPitch >> oi >> vc->orbitButton >> vc->mouseSensitivity;
                        vc->freeLook = (fl != 0);
                        vc->orbitInput = (oi != 0);
                    }
                    in >> std::ws;
                    if (std::isdigit(in.peek())) {
                        int dl = 0, ad = 0;
                        in >> dl;
                        vc->dollyPath = ReadQuoted(in);
                        in >> vc->dollyPosition >> ad;
                        vc->dolly = (dl != 0);
                        vc->autoDolly = (ad != 0);
                    }
                    in >> std::ws;   // optional dutch + confiner (back-compatible)
                    if (std::isdigit(in.peek()) || in.peek() == '-') {
                        int cf = 0;
                        in >> vc->dutch >> cf
                           >> vc->confineMin.x >> vc->confineMin.y >> vc->confineMin.z
                           >> vc->confineMax.x >> vc->confineMax.y >> vc->confineMax.z;
                        vc->confine = (cf != 0);
                    }
                } else if (field == "dollypath") {
                    auto* dp = go->AddComponent<DollyPath>();
                    int lp = 0; long long cnt = 0;   // signed: "-1" must not read as a huge size_t
                    in >> lp >> cnt;
                    if (!in || !countOk(cnt, 3)) return bad("dollypath: bad waypoint count");
                    dp->looped = (lp != 0);
                    dp->waypoints.clear();
                    for (long long k = 0; k < cnt && in; ++k) {
                        Vec3 w; in >> w.x >> w.y >> w.z; dp->waypoints.push_back(w);
                    }
                } else if (field == "dollycart") {
                    auto* dc = go->AddComponent<DollyCart>();
                    dc->path = ReadQuoted(in);
                    int am = 1;
                    in >> dc->position >> dc->speed >> am;
                    dc->autoMove = (am != 0);
                    // metres per second, ping-pong, face along the path (added later; older scenes keep the defaults)
                    in >> std::ws; if (std::isdigit(in.peek())) { int v = 0; in >> v; dc->speedInMetres = (v != 0); }
                    in >> std::ws; if (std::isdigit(in.peek())) { int v = 0; in >> v; dc->pingPong = (v != 0); }
                    in >> std::ws; if (std::isdigit(in.peek())) { int v = 0; in >> v; dc->orientToPath = (v != 0); }
                } else if (field == "cmbrain") {
                    auto* cb = go->AddComponent<CinemachineBrain>();
                    in >> cb->blendTime;
                    in >> std::ws;
                    if (std::isdigit(in.peek())) { int e = 1; in >> e; cb->blendStyle = e != 0 ? cam::BlendStyle::EaseInOut : cam::BlendStyle::Linear; }
                } else if (field == "cmbrainx") {
                    KvIn k(in);
                    if (auto* cb = LastOf(go->GetComponents<CinemachineBrain>())) {
                        int st = (int)cb->blendStyle; k.i("style", st);
                        cb->blendStyle = (cam::BlendStyle)std::clamp(st, 0, (int)cam::BlendStyle::Custom);
                        k.f("c0", cb->customCurve[0]); k.f("c1", cb->customCurve[1]); k.f("c2", cb->customCurve[2]); k.f("c3", cb->customCurve[3]);
                        k.b("cutscene", cb->yieldToCutscene);
                    }
                } else if (field == "camzone") {
                    KvIn k(in);
                    auto* cz = go->AddComponent<CameraZone>();
                    int sh = (int)cz->shape; k.i("shape", sh); cz->shape = (VolumeShape)std::clamp(sh, 0, kVolumeShapeCount - 1);
                    k.f("sx", cz->size.x); k.f("sy", cz->size.y); k.f("sz", cz->size.z);
                    k.f("ox", cz->offset.x); k.f("oy", cz->offset.y); k.f("oz", cz->offset.z);
                    k.f("blend", cz->blend); k.i("prio", cz->priority); k.f("fov", cz->fieldOfView);
                    k.f("fx", cz->followOffset.x); k.f("fy", cz->followOffset.y); k.f("fz", cz->followOffset.z);
                    k.f("damp", cz->positionDamping); k.str("shot", cz->shot);
                } else if (field == "camroom") {
                    KvIn k(in);
                    auto* rm = go->AddComponent<CameraRoom>();
                    k.f("w", rm->size.x); k.f("h", rm->size.y);
                } else if (field == "camerax") {
                    KvIn k(in);
                    if (auto* cam = LastOf(go->GetComponents<Camera>())) k.f("hitPulse", cam->hitPulseVignette);
                } else if (field == "vcamx") {
                    KvIn k(in);
                    if (auto* vc = LastOf(go->GetComponents<VirtualCamera>())) {
                        int dm = (int)vc->dampingMode; k.i("damp", dm);
                        vc->dampingMode = dm == 1 ? VirtualCamera::DampingMode::Spring : VirtualCamera::DampingMode::Smooth;
                        k.f("gpad", vc->groupPadding); k.b("gfit", vc->groupFrame);
                        k.f("look", vc->lookaheadTime); k.f("lookMax", vc->lookaheadMax); k.f("lookSm", vc->lookaheadSmooth);
                        k.b("zones", vc->useZones); k.f("dzw", vc->deadZoneW); k.f("dzh", vc->deadZoneH);
                        k.f("szw", vc->softZoneW); k.f("szh", vc->softZoneH); k.f("zdamp", vc->zoneDamping);
                        k.b("o2d", vc->ortho2D); k.f("osize", vc->orthoSize); k.b("cview", vc->confineToView);
                        k.b("rooms", vc->useRooms); k.f("roomT", vc->roomBlendTime);
                        k.b("col", vc->collision); k.f("colR", vc->collisionRadius); k.f("colSkin", vc->collisionSkin);
                        k.f("colRec", vc->collisionRecover); k.f("colIn", vc->collisionPullIn); k.b("colSnap", vc->collisionSnapIn);
                        k.u("colMask", vc->collisionMask);
                        k.b("fade", vc->fadeOccluders); k.f("fadeA", vc->occlusionAlpha); k.f("fadeSpd", vc->occlusionFadeSpeed);
                        k.f("blendIn", vc->blendIn); k.i("blendInStyle", vc->blendInStyle); k.b("aimFollow", vc->aimAtFollow);
                        int ng = 0; k.i("ng", ng);
                        vc->groupTargets.clear();
                        for (int gi = 0; gi < std::min(ng, 256); ++gi) {
                            std::string n; k.str(("g" + std::to_string(gi)).c_str(), n);
                            if (!n.empty()) vc->groupTargets.push_back(n);
                        }
                    }
                } else if (field == "text") {
                    std::string str = ReadQuoted(in);
                    Color c; float px = 0.1f; int ss = 0; Vec2 sp;
                    in >> c.r >> c.g >> c.b >> c.a >> px >> ss >> sp.x >> sp.y;
                    auto* tr = go->AddComponent<TextRenderer>();
                    tr->text = str; tr->color = c; tr->pixelSize = px;
                    tr->screenSpace = (ss != 0); tr->screenPos = sp;
                    ReadAnchor(in, tr->anchor);   // optional trailing field
                    // Optional shadow block (added later; absent in older files).
                    in >> std::ws;
                    int sk = in.peek();
                    if (sk >= '0' && sk <= '9') {
                        int sh = 0; Color sc; Vec2 so;
                        in >> sh >> sc.r >> sc.g >> sc.b >> sc.a >> so.x >> so.y;
                        tr->shadow = (sh != 0); tr->shadowColor = sc; tr->shadowOffset = so;
                    }
                    in >> std::ws; // optional alignment (added later)
                    if (std::isdigit(in.peek())) in >> tr->align;
                    in >> std::ws; // optional outline (added later still)
                    if (std::isdigit(in.peek())) {
                        int ol = 0; Color oc;
                        in >> ol >> oc.r >> oc.g >> oc.b >> oc.a;
                        tr->outline = (ol != 0); tr->outlineColor = oc;
                    }
                    in >> std::ws; // optional bold flag (added later)
                    if (std::isdigit(in.peek())) { int bd = 0; in >> bd; tr->bold = (bd != 0); }
                    in >> std::ws; // optional box size + vcenter + background (added later)
                    if (std::isdigit(in.peek()) || in.peek() == '-') {
                        int vc = 1, bg = 0; Color bc;
                        in >> tr->size.x >> tr->size.y >> vc >> bg >> bc.r >> bc.g >> bc.b >> bc.a;
                        tr->vcenter = (vc != 0); tr->background = (bg != 0); tr->backgroundColor = bc;
                    }
                    in >> std::ws; // optional spacing + uppercase + wrap (added later)
                    if (std::isdigit(in.peek()) || in.peek() == '-') {
                        int uc = 0, wr = 0;
                        in >> tr->letterSpacing >> tr->lineSpacing >> uc >> wr;
                        tr->uppercase = (uc != 0); tr->wrap = (wr != 0);
                    }
                    in >> std::ws; // optional italic/gradient/typewriter/bottom-align (newer)
                    if (std::isdigit(in.peek()) || in.peek() == '-') {
                        int it = 0, gr = 0, ab = 0; Color cb;
                        in >> it >> gr >> cb.r >> cb.g >> cb.b >> cb.a
                           >> tr->visibleChars >> tr->typeSpeed >> ab;
                        tr->italic = (it != 0); tr->gradient = (gr != 0);
                        tr->colorBottom = cb; tr->alignBottom = (ab != 0);
                    }
                    in >> std::ws; // optional TTF font path (newest)
                    if (in.peek() == '"') tr->fontPath = ReadQuoted(in);
                    in >> std::ws; // optional best-fit auto-size (newest)
                    if (std::isdigit(in.peek()) || in.peek() == '-') {
                        int as = 0;
                        in >> as >> tr->autoSizeMin >> tr->autoSizeMax;
                        tr->autoSize = (as != 0);
                    }
                } else if (field == "spriteanim") {
                    float fps = 8.0f; int loop = 1, playing = 1, count = 0;
                    in >> fps >> loop >> playing >> count;
                    auto* an = go->AddComponent<SpriteAnimator>();
                    an->fps = fps; an->loop = (loop != 0); an->playing = (playing != 0);
                    for (int k = 0; k < count && in; ++k) an->frames.push_back(ReadQuoted(in));
                    in >> std::ws; // optional atlas fields (3 ints)
                    if (std::isdigit(in.peek()))
                        in >> an->atlasColumns >> an->atlasRows >> an->atlasCount;
                    in >> std::ws; // optional named clips (newest)
                    if (std::isdigit(in.peek())) {
                        int nc = 0; in >> nc;
                        for (int k = 0; k < nc && in; ++k) {
                            SpriteClip c;
                            c.name = ReadQuoted(in);
                            int cl = 1, nf = 0;
                            in >> c.row >> c.fps >> cl >> nf;
                            c.loop = (cl != 0);
                            for (int j = 0; j < nf && in; ++j) c.frames.push_back(ReadQuoted(in));
                            if (in) an->clips.push_back(std::move(c));
                        }
                    }
                } else if (field == "audio") {
                    std::string cp = ReadQuoted(in);
                    float vol = 1.0f; int loop = 0, poa = 0;
                    in >> vol >> loop >> poa;
                    auto* au = go->AddComponent<AudioSource>();
                    au->clipPath = cp; au->volume = vol;
                    au->loop = (loop != 0); au->playOnAwake = (poa != 0);
                    in >> std::ws; // optional 3D fields: spatial min max
                    if (std::isdigit(in.peek())) {
                        int sp = 0; in >> sp >> au->minDistance >> au->maxDistance;
                        au->spatial = (sp != 0);
                        // optional mixer fields: pitch pan group rolloff
                        if (LooksNumeric(in)) {
                            int grp = 0, ro = 0;
                            in >> au->pitch >> au->pan >> grp >> ro;
                            if (grp >= 0 && grp < (int)AudioGroup::Count) au->group = (AudioGroup)grp;
                            if (ro >= 0 && ro <= (int)AudioRolloff::Logarithmic) au->rolloff = (AudioRolloff)ro;
                            in >> std::ws;   // optional low-pass cutoff
                            if (std::isdigit(in.peek())) {
                                in >> au->lowPass;
                                in >> std::ws;   // optional stream flag
                                if (std::isdigit(in.peek())) {
                                    int stv = 0; in >> stv; au->stream = stv != 0;
                                    // optional spatial effects: doppler scale occlude volume hz distance-filter hz
                                    if (LooksNumeric(in)) {
                                        int dop = 0, occ = 0, dlp = 0;
                                        in >> dop >> au->dopplerScale >> occ >> au->occlusionVolume >> au->occlusionLowPass >> dlp >> au->distanceLowPassHz;
                                        au->doppler = dop != 0;
                                        au->occlude = occ != 0;
                                        au->distanceLowPass = dlp != 0;
                                    }
                                }
                                // optional caption (quoted), after the stream flag and any spatial effects
                                while (in && (in.peek() == ' ' || in.peek() == '\t')) in.get();
                                if (in.peek() == '"') au->caption = ReadQuoted(in);
                            }
                        }
                    }
                } else if (field == "music") {
                    auto* mp = go->AddComponent<MusicPlayer>();
                    int pos = 1, sh = 0, lp = 1; std::size_t n = 0;
                    in >> pos >> sh >> lp >> mp->crossfade >> mp->volume >> n;
                    mp->playOnStart = pos != 0; mp->shuffle = sh != 0; mp->loopPlaylist = lp != 0;
                    for (std::size_t t = 0; t < n && in; ++t) mp->tracks.push_back(ReadQuoted(in));
                    in >> std::ws;   // optional stream flag (older files stream by default)
                    if (std::isdigit(in.peek())) { int stv = 1; in >> stv; mp->stream = stv != 0; }
                } else if (field == "animator") {
                    float speed = 1.0f; int playing = 1, loop = 1, tc = 0;
                    in >> speed >> playing >> loop;
                    std::string cname = ReadQuoted(in);
                    in >> tc;
                    auto* anim = go->AddComponent<Animator>();
                    anim->speed = speed; anim->playing = (playing != 0);
                    anim->clip.loop = (loop != 0); anim->clip.name = cname;
                    for (int t = 0; t < tc && in; ++t) {
                        std::string tn = ReadQuoted(in);
                        int kc = 0; in >> kc;
                        for (int k = 0; k < kc && in; ++k) {
                            float kt = 0, kv = 0; in >> kt >> kv;
                            anim->clip.AddKey(tn, kt, kv);
                        }
                    }
                } else if (field == "animx") {
                    float span = -1.0f; int ne = 0, nl = 0;
                    in >> span >> ne;
                    auto* anim = LastOf(go->GetComponents<Animator>());
                    for (int e = 0; e < ne && in; ++e) {
                        float et = 0; in >> et; std::string en = ReadQuoted(in);
                        if (anim) anim->clip.AddEvent(et, en);
                    }
                    in >> nl;
                    for (int l = 0; l < nl && in; ++l) {
                        std::string tn = ReadQuoted(in);
                        if (anim) if (auto* c = anim->clip.Track(tn)) c->smooth = false;
                    }
                    if (anim && span >= 0.0f) anim->clip.SpanTracks(span);
                } else if (field == "tilemap") {
                    float ts = 1.0f; int tw = 0, th = 0;
                    in >> ts >> tw >> th;
                    if (!in || ((long long)std::max(tw, 0) * std::max(th, 0) > 0 && !countOk((long long)tw * th, 1)))
                        return bad("tilemap: bad size");
                    auto* tm = go->AddComponent<Tilemap>();
                    tm->tileSize = ts; tm->Resize(tw, th);
                    for (int y = 0; y < th && in; ++y)
                        for (int x = 0; x < tw && in; ++x) { int id = 0; in >> id; tm->SetTile(x, y, id); }
                } else if (field == "tilemapcollider") {
                    go->AddComponent<TilemapCollider2D>();
                } else if (field == "uibutton") {
                    auto* btn = go->AddComponent<UIButton>();
                    btn->label = ReadQuoted(in);
                    Color c, h, t;
                    in >> btn->position.x >> btn->position.y >> btn->size.x >> btn->size.y
                       >> c.r >> c.g >> c.b >> c.a >> h.r >> h.g >> h.b >> h.a
                       >> t.r >> t.g >> t.b >> t.a;
                    btn->color = c; btn->hoverColor = h; btn->textColor = t;
                    ReadAnchor(in, btn->anchor);
                    // Optional trailing block (pressed/disabled colors + interactable).
                    in >> std::ws;
                    int pk = in.peek();
                    if (pk >= '0' && pk <= '9') {
                        Color pc, dc; int inter = 1;
                        in >> pc.r >> pc.g >> pc.b >> pc.a
                           >> dc.r >> dc.g >> dc.b >> dc.a >> inter;
                        btn->pressedColor = pc; btn->disabledColor = dc;
                        btn->interactable = (inter != 0);
                        // focusable appended later still within this block.
                        in >> std::ws;
                        int fk = in.peek();
                        if (fk >= '0' && fk <= '9') { int foc = 1; in >> foc; btn->focusable = (foc != 0); }
                    }
                    // Optional customization block (corner/font/border, added later).
                    in >> std::ws;
                    if (std::isdigit(in.peek())) {
                        Color bc;
                        in >> btn->cornerRadius >> btn->fontScale >> btn->borderWidth
                           >> bc.r >> bc.g >> bc.b >> bc.a;
                        btn->borderColor = bc;
                        in >> std::ws; // optional hover scale (added later)
                        if (std::isdigit(in.peek())) in >> btn->hoverScale;
                        in >> std::ws; // optional icon path + size (added later)
                        if (in.peek() == '"') { btn->icon = ReadQuoted(in); in >> btn->iconSize; }
                        in >> std::ws; // optional hover-text/transition/toggle block (added later)
                        if (std::isdigit(in.peek()) || in.peek() == '-') {
                            Color ht; int tm = 0, on = 0, ir = 0, hr = 0;
                            in >> ht.r >> ht.g >> ht.b >> ht.a >> btn->transitionSpeed
                               >> tm >> on >> btn->pressOffset >> ir >> hr
                               >> btn->repeatDelay >> btn->repeatInterval;
                            btn->hoverTextColor = ht; btn->toggleMode = (tm != 0); btn->isOn = (on != 0);
                            btn->iconRight = (ir != 0); btn->holdRepeat = (hr != 0);
                            in >> std::ws; // optional shape + drop shadow (added later)
                            if (std::isdigit(in.peek())) {
                                int sp = 0, sh = 0; Color sc;
                                in >> sp >> sh >> sc.r >> sc.g >> sc.b >> sc.a
                                   >> btn->shadowOffset.x >> btn->shadowOffset.y;
                                btn->shape = (UIShape)sp; btn->shadow = (sh != 0); btn->shadowColor = sc;
                                in >> std::ws; // optional shadow softness (added later)
                                if (std::isdigit(in.peek()) || in.peek() == '.') in >> btn->shadowSoftness;
                                in >> std::ws; // optional assigned OnClick (target + function), added later
                                if (in.peek() == '"') {
                                    btn->clickTarget = ReadQuoted(in); btn->clickFunction = ReadQuoted(in);
                                    in >> std::ws; // optional OnClick argument (added later)
                                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-') in >> btn->clickArg;
                                    in >> std::ws; // optional TTF label font
                                    if (in.peek() == '"') btn->fontPath = ReadQuoted(in);
                                    in >> std::ws; // grok/ui-options: ease, duration, focus color
                                    if (std::isdigit(in.peek()) || in.peek() == '-' || in.peek() == '.') {
                                        int ez = 0, sf = 0; Color fc;
                                        in >> ez >> btn->transitionDuration >> sf
                                           >> fc.r >> fc.g >> fc.b >> fc.a;
                                        btn->transitionEase = ez; btn->separateFocus = (sf != 0);
                                        btn->focusColor = fc;
                                    }
                                }
                            }
                        }
                    }
                } else if (field == "uipanel") {
                    auto* pn = go->AddComponent<UIPanel>();
                    Color c;
                    in >> pn->position.x >> pn->position.y >> pn->size.x >> pn->size.y
                       >> c.r >> c.g >> c.b >> c.a;
                    pn->color = c;
                    ReadAnchor(in, pn->anchor);
                    // Optional customization block (corner/border, added later).
                    in >> std::ws;
                    if (std::isdigit(in.peek())) {
                        Color bc;
                        in >> pn->cornerRadius >> pn->borderWidth
                           >> bc.r >> bc.g >> bc.b >> bc.a;
                        pn->borderColor = bc;
                    }
                    in >> std::ws; // optional gradient (added later)
                    if (std::isdigit(in.peek())) {
                        int g = 0; Color gb;
                        in >> g >> gb.r >> gb.g >> gb.b >> gb.a;
                        pn->useGradient = (g != 0); pn->colorBottom = gb;
                    }
                    in >> std::ws; // optional drop shadow (added later)
                    if (std::isdigit(in.peek())) {
                        int sh = 0; Color sc;
                        in >> sh >> sc.r >> sc.g >> sc.b >> sc.a >> pn->shadowOffset.x >> pn->shadowOffset.y;
                        pn->shadow = (sh != 0); pn->shadowColor = sc;
                    }
                    in >> std::ws; // optional shape + gradient direction (added later)
                    if (std::isdigit(in.peek())) {
                        int sp = 0, gh = 0; in >> sp >> gh;
                        pn->shape = (UIShape)sp; pn->gradientHorizontal = (gh != 0);
                        pn->gradientDir = gh ? UIPanel::GradientDir::Horizontal   // seed from legacy flag
                                             : UIPanel::GradientDir::Vertical;
                        in >> std::ws; // optional shadow softness (added later)
                        if (std::isdigit(in.peek()) || in.peek() == '.') in >> pn->shadowSoftness;
                        in >> std::ws; // optional gradient dir enum + outline + top sheen (newest)
                        if (std::isdigit(in.peek())) {
                            int gd = 0; in >> gd; pn->gradientDir = (UIPanel::GradientDir)gd;
                            Color oc; in >> pn->outlineWidth >> oc.r >> oc.g >> oc.b >> oc.a;
                            pn->outlineColor = oc;
                            int th = 0; Color hc; in >> th >> hc.r >> hc.g >> hc.b >> hc.a;
                            pn->topHighlight = (th != 0); pn->highlightColor = hc;
                            in >> std::ws;   // optional per-corner rounding mask (newest)
                            if (std::isdigit(in.peek())) in >> pn->cornerMask;
                        }
                    }
                } else if (field == "uidocument") {
                    auto* doc = go->AddComponent<UIDocument>();
                    doc->markup = ReadQuoted(in);
                } else if (field == "terrain") {
                    auto* tr = go->AddComponent<Terrain>();
                    int res = 32; float sz = 50.0f; Color c; std::size_t count = 0;
                    in >> res >> sz >> c.r >> c.g >> c.b >> c.a >> count;
                    // The height grid is (res+1)^2 and is written in full, so a
                    // resolution far beyond the stored heights is corrupt.
                    const long long dim = (long long)std::max(res, 1) + 1;   // widen before +1 (res may be INT_MAX)
                    if (!in || !countOk((long long)count, 1) ||
                        dim * dim > std::max<long long>((long long)count, 33 * 33))
                        return bad("terrain: bad resolution / height count");
                    tr->Resize(res); tr->size = sz; tr->color = c;
                    for (std::size_t k = 0; k < count && k < tr->heights.size() && in; ++k)
                        in >> tr->heights[k];
                    // Optional trailing auto-color fields (added later).
                    auto tmore = [&]() -> bool {
                        while (in.peek() == ' ' || in.peek() == '\t') in.get();
                        int ch = in.peek();
                        return ch != '\n' && ch != '\r' && ch != EOF;
                    };
                    auto rcol = [&](Color& col) { in >> col.r >> col.g >> col.b >> col.a; };
                    if (tmore()) { int ac = 1; in >> ac; tr->autoColor = (ac != 0);
                        rcol(tr->waterColor); rcol(tr->sandColor); rcol(tr->grassColor);
                        rcol(tr->rockColor);  rcol(tr->snowColor);
                        in >> tr->waterLevel >> tr->snowLevel >> tr->rockSlope;
                        in >> std::ws;   // optional texturing block (quoted texture path)
                        if (in.peek() == '"') {
                            int tp = 1;
                            tr->texture = ReadQuoted(in);
                            in >> tr->textureTiling >> tp; tr->triplanarTex = (tp != 0);
                            in >> std::ws;
                            if (in.peek() == '"') { tr->normalMap = ReadQuoted(in); in >> tr->normalStrength; }
                        }
                    }
                    tr->Apply();   // rebuild the mesh into a MeshRenderer
                } else if (field == "terraindigger") {
                    auto* td = go->AddComponent<TerrainDigger>();
                    int md = 0, btn = 0, k = 0;
                    in >> md >> btn >> k >> td->radius >> td->strength >> td->range >> td->relax;
                    td->mode = (TerrainDigger::Mode)md;
                    td->button = btn;
                    td->key = (char)k;
                    // Optional trailing block (hardness, showBrush, brush rgba) —
                    // guarded so older scenes without it still load.
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '-' || in.peek() == '.') {
                        in >> td->hardness;
                        int sb = 1; in >> sb; td->showBrush = (sb != 0);
                        in >> td->brushColor.r >> td->brushColor.g >> td->brushColor.b >> td->brushColor.a;
                        in >> std::ws;
                        if (std::isdigit(in.peek()) || in.peek() == '-') in >> td->raiseButton;
                    }
                } else if (field == "voxelterrain") {
                    auto* v = go->AddComponent<VoxelTerrain>();
                    int X = 48, Y = 28, Z = 48, ac = 1;
                    in >> X >> Y >> Z;
                    // The density field is stored (base64, ~1.33 chars per voxel), so
                    // a grid far larger than the rest of the file is corrupt.
                    // Divide rather than multiply, so a huge grid can't overflow the check.
                    const unsigned long long cap = std::max(64ULL * 64 * 64, remaining());
                    const unsigned long long gx = (unsigned long long)std::max(X, 2), gy = (unsigned long long)std::max(Y, 2),
                                             gz = (unsigned long long)std::max(Z, 2);
                    if (!in || gx > cap || gy > cap / gx || gz > cap / (gx * gy))
                        return bad("voxelterrain: bad grid size");
                    v->Resize(X, Y, Z);
                    in >> v->voxelSize >> v->iso >> ac >> v->snowLevel >> v->rockSlope;
                    v->autoColor = (ac != 0);
                    in >> v->color.r >> v->color.g >> v->color.b >> v->color.a
                       >> v->grassColor.r >> v->grassColor.g >> v->grassColor.b
                       >> v->rockColor.r >> v->rockColor.g >> v->rockColor.b
                       >> v->soilColor.r >> v->soilColor.g >> v->soilColor.b
                       >> v->snowColor.r >> v->snowColor.g >> v->snowColor.b
                       >> v->textureTiling;
                    v->texture = ReadQuoted(in);
                    std::string blob; in >> blob;     // base64 density (whitespace-free)
                    v->DecodeDensity(blob);
                    v->Apply();
                } else if (field == "voxeldigger") {
                    auto* vd = go->AddComponent<VoxelDigger>();
                    int md = 0, btn = 0, k = 0, sb = 1;
                    in >> md >> btn >> k >> vd->radius >> vd->strength >> vd->range >> sb
                       >> vd->brushColor.r >> vd->brushColor.g >> vd->brushColor.b >> vd->brushColor.a;
                    vd->mode = (VoxelDigger::Mode)md;
                    vd->button = btn; vd->key = (char)k; vd->showBrush = (sb != 0);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '-') in >> vd->addButton;
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '-') {   // real-life options (optional: older scenes end above)
                        int col = 0, nm = 0, cr = 0, shp = 0, sn = 0, sl = 0, co = 0, gi = 0;
                        in >> col >> nm >> cr >> vd->addMaterial >> vd->power >> vd->hardnessScale >> shp >> vd->depthLimit >> vd->protectBelow
                           >> sn >> vd->debrisMax >> vd->debrisLife >> sl >> co >> gi;
                        vd->hudTarget = ReadQuoted(in);
                        in >> std::ws;
                        if (std::isdigit(in.peek())) {
                            int tf = 0, bf = 0;
                            in >> vd->slumpPasses; vd->slumpPasses = std::max(1, std::min(64, vd->slumpPasses));
                            in >> std::ws;
                            if (std::isdigit(in.peek())) { in >> tf >> bf >> vd->bareHandsPower; vd->tools = ReadQuoted(in); vd->toolsFromInventory = tf != 0; vd->buildFromItems = bf != 0;
                                in >> std::ws; if (std::isdigit(in.peek())) { in >> vd->caveInSpan; vd->caveInSpan = std::max(0, std::min(64, vd->caveInSpan));
                                    in >> std::ws; if (std::isdigit(in.peek())) { int pk = 0; in >> pk >> vd->pourCells; vd->pourKey = (char)pk; vd->pourCells = std::max(1, std::min(2000, vd->pourCells));
                                        in >> std::ws; if (std::isdigit(in.peek())) { int fl = 0; in >> vd->reach >> fl; vd->reach = std::max(0.5f, std::min(1000.0f, vd->reach));
                                            vd->playerAim = (fl & 1) != 0; vd->useActions = (fl & 2) != 0; vd->hotbarKeys = (fl & 4) != 0; vd->showProgress = (fl & 8) != 0; } } } }
                        }
                        vd->collect = (VoxelDigger::Collect)std::max(0, std::min(3, col));
                        vd->needsMaterial = nm != 0; vd->creative = cr != 0; vd->snapToGrid = sn != 0;
                        vd->shape = (VoxelTerrain::EditOptions::Shape)std::max(0, std::min(3, shp));
                        vd->slump = sl != 0; vd->collapse = co != 0; vd->giveItems = gi != 0;
                    }
                } else if (field == "voxelcatch") {
                    if (auto* v = LastOf(go->GetComponents<VoxelTerrain>())) { v->catchFalling = true; in >> v->catchMargin; }
                } else if (field == "voxelfoot") {
                    if (auto* v = LastOf(go->GetComponents<VoxelTerrain>())) {
                        int fb = 0; v->footprints = true; v->footprintTag = ReadQuoted(in);
                        in >> fb >> v->footprintStep >> v->footprintRadius >> v->footprintDepth >> v->footprintPower;
                        v->footprintBodies = fb != 0;
                        v->footprintStep = std::max(0.1f, v->footprintStep); v->footprintRadius = std::max(0.05f, std::min(5.0f, v->footprintRadius));
                    }
                } else if (field == "voxelfluid") {
                    if (auto* v = LastOf(go->GetComponents<VoxelTerrain>())) {
                        int gl = 0; v->fluidOn = true;
                        in >> v->fluidBudget >> v->fluidMaxCells >> v->fluidRate >> gl >> v->fluidColor.r >> v->fluidColor.g >> v->fluidColor.b >> v->fluidColor.a;
                        v->fluidGlow = gl != 0;
                        v->fluidBudget = std::max(1, std::min(100000, v->fluidBudget)); v->fluidMaxCells = std::max(0, std::min(2000000, v->fluidMaxCells));
                        v->fluidRate = std::max(0.1f, std::min(120.0f, v->fluidRate));
                    }
                } else if (field == "voxelmats") {
                    if (auto* v = LastOf(go->GetComponents<VoxelTerrain>())) {
                        std::size_t n = 0; std::string blob; in >> n >> blob;
                        v->DecodeMaterials(blob); v->matCountExpected_ = n;
                    }
                } else if (field == "voxelmat") {
                    if (auto* v = LastOf(go->GetComponents<VoxelTerrain>())) {
                        if (v->materials.size() >= 255) { /* ignore the rest */ }
                        else {
                            VoxelTerrain::VoxelMaterial m; int lo = 0;
                            m.name = ReadQuoted(in);
                            in >> m.color.r >> m.color.g >> m.color.b >> m.color.a >> m.hardness >> m.density >> lo;
                            m.loose = lo != 0; m.drop = ReadQuoted(in); in >> m.dropPerM3;
                            m.texture = ReadQuoted(in); m.digSound = ReadQuoted(in);
                            in >> m.particle.r >> m.particle.g >> m.particle.b;
                            if (v->materials.empty() || !v->loadedMaterialTable_) { v->materials.clear(); v->loadedMaterialTable_ = true; }
                            v->materials.push_back(m);
                            if (v->materials.size() == v->matCountExpected_) v->Apply();   // the table is complete: colour the surface by material
                        }
                    }
                } else if (field == "pausemenu") {
                    auto* pm = go->AddComponent<PauseMenu>();
                    int tk = 27, sr = 1, sq = 1;
                    in >> tk >> sr >> sq
                       >> pm->dimColor.r >> pm->dimColor.g >> pm->dimColor.b >> pm->dimColor.a
                       >> pm->panelColor.r >> pm->panelColor.g >> pm->panelColor.b >> pm->panelColor.a;
                    pm->toggleKey = (char)tk; pm->showResume = (sr != 0); pm->showQuit = (sq != 0);
                    pm->title = ReadQuoted(in);
                    pm->mainMenuScene = ReadQuoted(in);
                } else if (field == "saveable") {
                    auto* sv = go->AddComponent<Saveable>();
                    int dyn = 0, tr = 1, act = 1, comps = 1, vars = 0, svars = 0;
                    std::size_t np = 0;
                    in >> dyn >> tr >> act >> comps >> vars >> svars;
                    sv->dynamic = dyn != 0; sv->saveTransform = tr != 0; sv->saveActive = act != 0;
                    sv->saveComponents = comps != 0; sv->saveVariables = vars != 0; sv->saveSceneVariables = svars != 0;
                    sv->saveId = ReadQuoted(in);
                    sv->prefab = ReadQuoted(in);
                    in >> np;
                    for (std::size_t pi = 0; pi < np && pi < 1000 && in; ++pi) sv->properties.push_back(ReadQuoted(in));
                } else if (field == "savesettings") {
                    auto* ss = go->AddComponent<SaveConfig>();
                    int onStart = 0;
                    in >> ss->autosaveInterval >> onStart >> ss->autosaveSlots;
                    ss->autosaveOnSceneStart = onStart != 0;
                    ss->autosaveSlots = std::min(std::max(1, ss->autosaveSlots), 20);
                } else if (field == "autosave") {
                    auto* as = go->AddComponent<AutoSave>();
                    int sos = 0;
                    in >> as->interval >> sos;
                    as->saveOnStart = sos != 0;
                    as->slot = ReadQuoted(in);
                    as->label = ReadQuoted(in);
                } else { fieldDone = false; }
                }
                if (!fieldDone) { fieldDone = true;
                if (field == "flashlight") {
                    auto* fl = go->AddComponent<Flashlight>();
                    int tk = 'f', onv = 1;
                    in >> tk >> onv >> fl->range >> fl->angle >> fl->intensity
                       >> fl->color.r >> fl->color.g >> fl->color.b;
                    fl->toggleKey = (char)tk; fl->on = (onv != 0);
                } else if (field == "fphand") {
                    auto* fh = go->AddComponent<FirstPersonHand>();
                    int hs = 1;
                    in >> fh->attackButton >> hs;
                    fh->holdToSwing = (hs != 0);
                    // Optional trailing fields (older rows tolerated): showLeftArm,
                    // followPitch, bobbing. (Some builds wrote extra arm-styling tokens
                    // between showLeftArm and these; a stray middle token is harmless.)
                    std::string rest; std::getline(in, rest);
                    std::istringstream rs(rest);
                    std::vector<float> v; float f;
                    while (rs >> f) v.push_back(f);
                    if (v.size() >= 1) fh->showLeftArm = (v[0] != 0.0f);
                    if (v.size() >= 2) fh->followPitch = (v[1] != 0.0f);
                    if (v.size() >= 3) fh->bobbing     = (v[2] != 0.0f);
                    if (v.size() >= 4) fh->steadyArm   = (v[3] != 0.0f);
                    if (v.size() >= 5) fh->armRaise    = v[4];
                    if (v.size() >= 6) fh->armElbow    = v[5];
                    if (v.size() >= 7) fh->showWhileRunning = (v[6] != 0.0f);
                } else if (field == "fphands") {
                    auto* hh = go->AddComponent<FirstPersonHands>();
                    hh->ApplyPreset(FirstPersonHands::Preset::OldLook);   // a scene saved before presets keeps its look
                    auto side = [&](FirstPersonHands::HandSide& s, std::istringstream& rs) {
                        int en = 1, col = 0, sl = 0;
                        rs >> en >> s.position.x >> s.position.y >> s.position.z
                           >> s.rotation.x >> s.rotation.y >> s.rotation.z >> s.uniformScale
                           >> s.scale.x >> s.scale.y >> s.scale.z
                           >> s.size.x >> s.size.y >> s.size.z >> col
                           >> s.colour.r >> s.colour.g >> s.colour.b >> sl >> s.sleeveLength
                           >> s.sleeveColour.r >> s.sleeveColour.g >> s.sleeveColour.b;
                        s.enabled = en != 0; s.useColour = col != 0; s.sleeve = sl != 0;
                    };
                    int empty = 0, run = 0, mode = 0;
                    in >> empty >> run >> hh->bobAmount >> hh->swayAmount >> hh->punchAmount >> hh->handFov
                       >> hh->itemOffset.x >> hh->itemOffset.y >> hh->itemOffset.z
                       >> hh->itemRotation.x >> hh->itemRotation.y >> hh->itemRotation.z
                       >> hh->itemScale >> mode;
                    hh->showWhenEmpty = empty != 0;
                    hh->showWhileRunning = run != 0;
                    hh->holdMode = mode == 1 ? FirstPersonHands::HoldMode::Left
                                 : mode == 2 ? FirstPersonHands::HoldMode::Two
                                             : FirstPersonHands::HoldMode::Right;
                    std::string rest; std::getline(in, rest);
                    std::istringstream rs(rest);
                    side(hh->right, rs);
                    side(hh->left, rs);
                    rs >> hh->othersItemOffset.x >> hh->othersItemOffset.y >> hh->othersItemOffset.z
                       >> hh->othersItemRotation.x >> hh->othersItemRotation.y >> hh->othersItemRotation.z
                       >> hh->othersArmLift >> hh->othersArmForward;
                } else if (field == "fphands3") {
                    auto* hh = LastOf(go->GetComponents<FirstPersonHands>());
                    if (!hh) hh = go->AddComponent<FirstPersonHands>();
                    int pr = 5, sw = 0, ab = 0, mr = 0, ml = 0;
                    in >> pr >> sw >> ab >> mr >> ml;
                    if (pr < 0 || pr > 5) pr = 5;
                    hh->preset = (FirstPersonHands::Preset)pr;
                    hh->swingOnClick = sw != 0;
                    hh->attackButton = ab < 0 ? 0 : ab > 7 ? 7 : ab;
                    hh->right.matchShirt = mr != 0;
                    hh->left.matchShirt = ml != 0;
                } else if (field == "fphands2") {
                    auto* hh = LastOf(go->GetComponents<FirstPersonHands>());
                    if (!hh) hh = go->AddComponent<FirstPersonHands>();
                    auto extra = [&](FirstPersonHands::HandSide& s, std::istringstream& rs) {
                        int on = 0;
                        rs >> on >> s.forearmLength >> s.forearmThickness
                           >> s.forearmColour.r >> s.forearmColour.g >> s.forearmColour.b
                           >> s.wristOffset.x >> s.wristOffset.y >> s.wristOffset.z
                           >> s.wristRotation.x >> s.wristRotation.y >> s.wristRotation.z
                           >> s.elbowBend >> s.reach;
                        s.forearm = on != 0;
                    };
                    auto oarm = [&](FirstPersonHands::OthersArm& a, std::istringstream& rs) {
                        int on = 1;
                        rs >> on >> a.forearmLength >> a.forearmThickness
                           >> a.forearmColour.r >> a.forearmColour.g >> a.forearmColour.b
                           >> a.handSize.x >> a.handSize.y >> a.handSize.z
                           >> a.wristOffset.x >> a.wristOffset.y >> a.wristOffset.z
                           >> a.wristRotation.x >> a.wristRotation.y >> a.wristRotation.z
                           >> a.elbowBend >> a.reach;
                        a.forearm = on != 0;
                    };
                    std::string rest; std::getline(in, rest);
                    std::istringstream rs(rest);
                    extra(hh->right, rs); extra(hh->left, rs);
                    oarm(hh->othersRight, rs); oarm(hh->othersLeft, rs);
                } else if (field == "npspawner") {
                    auto* ps = go->AddComponent<NetworkPlayerSpawner>();
                    ps->playerTemplate = ReadQuoted(in);
                    ps->prefabFile = ReadQuoted(in);
                    int slp = 1, g = '@';
                    in >> ps->spawnPoint.x >> ps->spawnPoint.y >> ps->spawnPoint.z
                       >> ps->spawnSpread >> slp >> g;
                    ps->spawnLocalPlayer = (slp != 0); ps->glyph = (char)g;
                } else if (field == "anticheat") {
                    auto* ac = go->AddComponent<AntiCheatConfig>();
                    AntiCheatSettings& s = ac->settings;
                    s.requireEncryption = false;   // as before, unless an "anticheatsafety" line follows
                    int en = 1, own = 1;
                    in >> en >> s.maxSyncSpeed >> s.maxSyncVarsPerSecond >> s.kickAfterViolations >> own;
                    s.enabled = (en != 0); s.ownerOnlySyncVars = (own != 0);
                    if (!std::isfinite(s.maxSyncSpeed) || s.maxSyncSpeed < 0.0f) s.maxSyncSpeed = 0.0f;
                    if (s.maxSyncVarsPerSecond < 0) s.maxSyncVarsPerSecond = 0;
                    if (s.kickAfterViolations < 0) s.kickAfterViolations = 0;
                    ac->file = ReadQuoted(in);
                    const std::string allow = ReadQuoted(in);
                    s.spawnAllowlist.clear();
                    for (std::size_t a = 0; a < allow.size();) {
                        std::size_t b = allow.find(';', a);
                        if (b == std::string::npos) b = allow.size();
                        if (b > a) s.spawnAllowlist.push_back(allow.substr(a, b - a));
                        a = b + 1;
                    }
                    // RPC limits came later: a leading digit means they're here; an
                    // older scene keeps the component's defaults.
                    in >> std::ws;
                    if (std::isdigit(in.peek())) {
                        int rps = s.maxRpcsPerSecond, rb = s.maxRpcBytes;
                        in >> rps >> rb;
                        s.maxRpcsPerSecond = std::max(0, rps); s.maxRpcBytes = std::max(0, rb);
                        in >> std::ws;   // and the clock check came after those
                        if (std::isdigit(in.peek()) || in.peek() == '.') {
                            float drift = s.maxClockDrift;
                            in >> drift;
                            s.maxClockDrift = std::isfinite(drift) ? std::clamp(drift, 0.0f, 1.0f) : 0.0f;
                        }
                        in >> std::ws;   // then the server-only SyncVar prefixes, when there are any
                        if (in.peek() == '"') {
                            const std::string vars = ReadQuoted(in);
                            s.serverVars.clear();
                            for (std::size_t a = 0; a < vars.size();) {
                                std::size_t b = vars.find(';', a);
                                if (b == std::string::npos) b = vars.size();
                                if (b > a) s.serverVars.push_back(vars.substr(a, b - a));
                                a = b + 1;
                            }
                        }
                    }
                } else if (field == "anticheatsafety") {   // follows the object's "anticheat" line
                    int enc = 0, toggle = 0;
                    in >> enc >> toggle;
                    if (auto* ac = LastOf(go->GetComponents<AntiCheatConfig>())) {
                        ac->settings.requireEncryption = enc != 0;
                        ac->allowRuntimeToggle = toggle != 0;
                    }
                } else if (field == "anticheathit") {   // follows the object's "anticheat" line
                    float o = 2.0f, t = 1.0f, r = 0.0f, dmg = 10.0f, rewindRtt = AntiCheatSettings{}.maxRewindRtt; int los = 1, needMove = 1;
                    in >> o >> t >> r >> dmg >> los;
                    in >> std::ws;   // the move-check requirement and the round-trip cap came later
                    if (std::isdigit(in.peek())) in >> needMove;
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '.') in >> rewindRtt;
                    if (auto* ac = LastOf(go->GetComponents<AntiCheatConfig>())) {
                        AntiCheatSettings& s = ac->settings;
                        auto ok = [](float v) { return std::isfinite(v) && v >= 0.0f; };
                        if (ok(o)) s.hitOriginTolerance = o;
                        if (ok(t)) s.hitTolerance = t;
                        if (ok(r)) s.maxHitRange = r;
                        if (ok(dmg)) s.hitDamage = dmg;
                        s.hitLineOfSight = los != 0;
                        s.hitsNeedMoveCheck = needMove != 0;
                        if (ok(rewindRtt)) s.maxRewindRtt = rewindRtt;
                    }
                } else if (field == "anticheatweapon") {   // one per known weapon, after "anticheat"
                    const std::string w = ReadQuoted(in);
                    if (auto* ac = LastOf(go->GetComponents<AntiCheatConfig>()); ac && !w.empty()) ac->settings.weapons.push_back(w);
                } else if (field == "anticheatrule") {   // one ".okayac" line the "anticheat" line has no room for, after it
                    const std::string line = ReadQuoted(in);
                    if (auto* ac = LastOf(go->GetComponents<AntiCheatConfig>()); ac && !line.empty()) {
                        AntiCheatSettings one;   // a line this build doesn't know is skipped, not fatal
                        AntiCheatSettings& s = ac->settings;
                        if (AntiCheatSettings::Parse(line, one)) {
                            if (line.rfind("maxSpawnsPerSecond", 0) == 0) s.maxSpawnsPerSecond = one.maxSpawnsPerSecond;
                            else if (line.rfind("maxLiveSpawnsPerPeer", 0) == 0) s.maxLiveSpawnsPerPeer = one.maxLiveSpawnsPerPeer;
                            else if (line.rfind("spawnRadius", 0) == 0) s.spawnRadius = one.spawnRadius;
                            else if (line.rfind("clientVar", 0) == 0) s.clientVars.insert(s.clientVars.end(), one.clientVars.begin(), one.clientVars.end());
                            else if (line.rfind("ticketKey", 0) == 0) s.ticketKey = one.ticketKey;
                            else if (line.rfind("ticketAudience", 0) == 0) s.ticketAudience = one.ticketAudience;
                            else if (line.rfind("ticketAnyAudience", 0) == 0) s.ticketAnyAudience = one.ticketAnyAudience;
                        }
                    }
                } else if (field == "anticheatdetect") {   // one detection setting, "detect.<key> <value>", after "anticheat"
                    const std::string line = ReadQuoted(in);
                    if (auto* ac = LastOf(go->GetComponents<AntiCheatConfig>()); ac && line.rfind("detect.", 0) == 0) {
                        const std::size_t sp = line.find(' ');
                        std::string err;   // a line this build doesn't know is skipped, not fatal
                        ac->settings.detect.ParseLine(line.substr(7, sp == std::string::npos ? std::string::npos : sp - 7),
                                                      sp == std::string::npos ? std::string() : line.substr(sp + 1), &err);
                    }
                } else if (field == "netvalidator") {
                    auto* nv = go->AddComponent<NetValidator>();
                    nv->rpc = ReadQuoted(in);
                    int cm = 1;
                    in >> nv->maxBytes >> nv->maxPerSecond >> cm;
                    nv->clientsMaySend = (cm != 0);
                    if (nv->maxBytes < 0) nv->maxBytes = 0;
                    if (!std::isfinite(nv->maxPerSecond) || nv->maxPerSecond < 0.0f) nv->maxPerSecond = 0.0f;
                    in >> std::ws;   // cooldown / budget / refill came later (an older scene has none)
                    if (LooksNumeric(in)) {
                        in >> nv->cooldown >> nv->budget >> nv->refillPerSecond;
                        for (float* f : {&nv->cooldown, &nv->budget, &nv->refillPerSecond})
                            if (!std::isfinite(*f) || *f < 0.0f) *f = 0.0f;
                    }
                } else if (field == "netsync") {
                    auto* ns = go->AddComponent<NetworkSync>();
                    int au = 0, ow = 1; in >> au >> ow;
                    ns->authority = (NetworkSync::Authority)au; ns->owned = (ow != 0);
                    // syncAnimation is optional (added after the first netsync release):
                    // a leading digit means it's present; a quote means the old form.
                    in >> std::ws;
                    if (std::isdigit(in.peek())) { int sa = 1; in >> sa; ns->syncAnimation = (sa != 0); }
                    ns->netId = ReadQuoted(in);
                    in >> std::ws;   // clientClaimable came later
                    if (std::isdigit(in.peek())) { int cc = 0; in >> cc; ns->clientClaimable = cc != 0; }
                } else if (field == "netobject") {
                    auto* no = go->AddComponent<NetworkObject>();
                    int au = 0, ow = 1; in >> au >> ow;
                    no->authority = (NetworkObject::Authority)au; no->owned = (ow != 0);
                    no->netId = ReadQuoted(in);
                } else if (field == "nettransform") {
                    go->AddComponent<NetworkTransform>();
                } else if (field == "syncvar") {
                    auto* sv = go->AddComponent<SyncVar>();
                    sv->name = ReadQuoted(in);
                    sv->value = ReadQuoted(in);
                } else if (field == "water") {
                    auto* w = go->AddComponent<Water>();
                    in >> w->size >> w->resolution >> w->waveHeight >> w->waveLength >> w->waveSpeed
                       >> w->color.r >> w->color.g >> w->color.b >> w->opacity
                       >> w->reflectivity >> w->specular >> w->shininess >> w->flow.x >> w->flow.y;
                    w->color.a = w->opacity;
                    // depth, density and drag came later. A quote means an older water
                    // line (visual plane only, depth stays 0).
                    auto numeric = [](std::istream& in) {
                        in >> std::ws;
                        const int c = in.peek();
                        return c == '-' || c == '+' || c == '.' || std::isdigit(c);
                    };
                    if (numeric(in)) in >> w->depth;
                    if (numeric(in)) in >> w->fluidDensity;
                    if (numeric(in)) in >> w->linearDrag;
                    if (!std::isfinite(w->depth) || w->depth < 0.0f) w->depth = 0.0f;
                    if (!std::isfinite(w->fluidDensity) || w->fluidDensity < 0.0f) w->fluidDensity = 1.0f;
                    if (!std::isfinite(w->linearDrag) || w->linearDrag < 0.0f) w->linearDrag = 4.0f;
                    w->texture = ReadQuoted(in);
                    w->Apply();
                } else if (field == "worldstreamer") {
                    auto* ws = go->AddComponent<WorldStreamer>();
                    int once = 1;
                    in >> ws->cellSize >> ws->loadRadius >> ws->unloadRadius >> once;
                    ws->onlyOnCellChange = (once != 0);
                    ws->folder = ReadQuoted(in);
                    ws->prefix = ReadQuoted(in);
                    ws->ext    = ReadQuoted(in);
                    ws->target = ReadQuoted(in);
                    // Optional on the same line (files before predictive loading keep the default).
                    while (in.peek() == ' ' || in.peek() == '\t') in.get();
                    if (std::isdigit(in.peek()) || in.peek() == '.') {
                        float predict = 1.0f;
                        if (in >> predict && std::isfinite(predict) && predict >= 0.0f) ws->predictSeconds = predict;
                        in.clear();
                    }
                } else if (field == "destructible") {
                    auto* de = go->AddComponent<Destructible>();
                    int collapse = 1;
                    de->blockTag = ReadQuoted(in);
                    in >> de->voxelSize >> de->fractureChunks >> de->debrisLifetime
                       >> de->explosionForce >> de->upwardBias >> de->debrisGravityScale
                       >> de->debrisDrag >> collapse >> de->anchorY >> de->maxDebris
                       >> de->breakButton >> de->breakRadius >> de->reach;
                    de->collapseEnabled = (collapse != 0);
                } else if (field == "character") {
                    std::string rest; std::getline(in, rest);   // rest of the line after the field token
                    if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
                    auto* ch = go->AddComponent<Character>();
                    ch->FromText(rest);
                    ch->Apply();    // rebuild the mesh into a MeshRenderer
                } else if (field == "charenabled") {
                    // A custom model replaced the blocky body: keep the default
                    // Character (and the mesh Apply() just rebuilt) hidden.
                    int en = 1; in >> en;
                    if (auto* ch = LastOf(go->GetComponents<Character>())) ch->enabled = (en != 0);
                    if (!en) if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) mr->enabled = false;
                } else if (field == "charext") {
                    // External-rig mode: the Character computes the pose for a
                    // HumanoidRetarget and renders nothing itself.
                    int ex = 0; in >> ex;
                    if (auto* ch = LastOf(go->GetComponents<Character>())) ch->driveExternal = (ex != 0);
                    if (ex) if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) mr->enabled = false;
                } else if (field == "humretarget") {
                    auto* hr = go->AddComponent<HumanoidRetarget>();
                    in >> hr->weight;   // bones re-detect + rest re-captures on first play frame
                    // The rest of this line only: "lean <scale>" and "map <slot> <name>" pairs.
                    std::string rest; std::getline(in, rest);
                    std::istringstream ls(rest);
                    std::string tag;
                    while (ls >> tag) {
                        if (tag == "lean") { if (!(ls >> hr->leanScale)) break; continue; }
                        if (tag != "map") break;
                        int b = -1;
                        if (!(ls >> b)) break;
                        const std::string name = ReadQuoted(ls);
                        if (b >= 0 && b < humanoid::BoneCount) hr->boneMap[b] = name;
                    }
                } else if (field == "charbind") {
                    // Auto-rigged custom mesh: restore the bind geometry and re-rig.
                    Mesh bm;
                    long long nv = 0; in >> nv;   // signed: "-1" must not read as a huge size_t
                    if (!in || !countOk(nv, 3)) return bad("charbind: bad vertex count");
                    bm.vertices.reserve((std::size_t)nv);
                    for (long long k = 0; k < nv && in; ++k) { Vec3 v; in >> v.x >> v.y >> v.z; bm.vertices.push_back(v); }
                    long long nt = 0; in >> nt;
                    if (!in || !countOk(nt, 1)) return bad("charbind: bad triangle count");
                    bm.triangles.reserve((std::size_t)nt);
                    for (long long k = 0; k < nt && in; ++k) { int t = 0; in >> t; bm.triangles.push_back(t); }
                    long long nu = 0; in >> nu;
                    if (!in || !countOk(nu, 2)) return bad("charbind: bad uv count");
                    bm.uvs.reserve((std::size_t)nu);
                    for (long long k = 0; k < nu && in; ++k) { Vec2 t; in >> t.x >> t.y; bm.uvs.push_back(t); }
                    for (int t : bm.triangles)
                        if (t < 0 || t >= (int)bm.vertices.size()) return bad("charbind: triangle index " + std::to_string(t) + " out of range");
                    auto* ch = LastOf(go->GetComponents<Character>());
                    if (!ch) ch = go->AddComponent<Character>();
                    ch->BindCustomMesh(bm);
                    ch->Apply();
                } else if (field == "network") {
                    auto* nm = go->AddComponent<NetworkManager>();
                    int as = 0, port = 45000;
                    in >> as >> port;
                    nm->autoStart = (NetworkManager::AutoStart)as;
                    nm->autoPort = (std::uint16_t)port;
                    nm->autoHost = ReadQuoted(in);
                    nm->startName = ReadQuoted(in);
                    in >> std::ws;
                    if (in.peek() == '"') nm->startRoom = ReadQuoted(in);  // optional (newer)
                    in >> std::ws;
                    if (std::isdigit(in.peek())) { in >> nm->maxPlayers >> nm->snapshotRate; } // host settings
                    in >> std::ws;
                    if (in.peek() == '"') nm->serverName = ReadQuoted(in);
                    in >> std::ws;
                    if (in.peek() == '"') nm->password = ReadQuoted(in);
                    in >> std::ws;   // the fixed net tick came later
                    if (std::isdigit(in.peek())) {
                        in >> nm->netTickRate;
                        if (!std::isfinite(nm->netTickRate) || nm->netTickRate < 0.0f) nm->netTickRate = 0.0f;
                        if (nm->netTickRate > 240.0f) nm->netTickRate = 240.0f;
                        in >> std::ws;   // then authoritative movement
                        if (std::isdigit(in.peek())) { int am = 0; in >> am; nm->authoritativeMovement = am != 0; }
                        in >> std::ws;   // then the prediction's correction easing
                        if (std::isdigit(in.peek())) {
                            float rate = 12.0f, snap = 2.0f;
                            in >> rate >> snap;
                            if (std::isfinite(rate) && rate >= 0.0f) nm->reconcileRate = std::min(rate, 100.0f);
                            if (std::isfinite(snap) && snap > 0.0f) nm->reconcileSnap = std::min(snap, 100.0f);
                        }
                    }
                } else if (field == "netrelay") {   // the relay online play uses (NetworkManager::relayServer)
                    const std::string r = ReadQuoted(in);
                    if (auto* nm = LastOf(go->GetComponents<NetworkManager>()); nm && r.size() <= 255) nm->relayServer = r;
                } else if (field == "netprefab") {   // one of the NetworkManager's spawnable prefabs
                    const std::string pf = ReadQuoted(in);
                    if (auto* nm = LastOf(go->GetComponents<NetworkManager>()); nm && !pf.empty()) nm->spawnablePrefabs.push_back(pf);
                } else if (field == "worldui") {
                    auto* wu = go->AddComponent<WorldUI>();
                    wu->text = ReadQuoted(in);
                    Color c, bg, bc; Vec3 off; int sd = 1;
                    in >> c.r >> c.g >> c.b >> c.a
                       >> bg.r >> bg.g >> bg.b >> bg.a
                       >> off.x >> off.y >> off.z
                       >> wu->pixelSize >> sd >> wu->maxDistance >> wu->bar
                       >> bc.r >> bc.g >> bc.b >> bc.a;
                    wu->color = c; wu->background = bg; wu->worldOffset = off;
                    wu->scaleWithDistance = (sd != 0); wu->barColor = bc;
                } else if (field == "uiinput") {
                    auto* inp = go->AddComponent<UIInputField>();
                    int an = 0; Color c;
                    in >> inp->position.x >> inp->position.y >> inp->size.x >> inp->size.y >> an >> inp->maxLength;
                    inp->anchor = (UIAnchor)an;
                    inp->text = ReadQuoted(in); inp->placeholder = ReadQuoted(in);
                    in >> c.r >> c.g >> c.b >> c.a; inp->color = c;
                    in >> std::ws; // optional content type (added later)
                    if (std::isdigit(in.peek())) { int ct = 0; in >> ct; inp->contentType = (UIInputField::ContentType)ct; }
                    in >> std::ws; // optional shape + corner + focus ring (added later)
                    if (std::isdigit(in.peek())) {
                        int sp = 0; Color bc;
                        in >> sp >> inp->cornerRadius >> inp->borderWidth >> bc.r >> bc.g >> bc.b >> bc.a;
                        inp->shape = (UIShape)sp; inp->borderColor = bc;
                    }
                    in >> std::ws; // grok/ui-controls: optional multiline
                    if (std::isdigit(in.peek())) { int ml = 0; in >> ml; inp->multiline = (ml != 0); }
                    in >> std::ws; // optional focusable (older scenes: stays on)
                    if (std::isdigit(in.peek())) { int fo = 1; in >> fo; inp->focusable = (fo != 0); }
                } else if (field == "uitabs") {
                    auto* tb = go->AddComponent<UITabs>();
                    int an = 0, sp = 0, it = 1; std::size_t count = 0;
                    Color bg, se, tc, stc;
                    in >> tb->position.x >> tb->position.y >> tb->size.x >> tb->size.y >> an >> tb->value
                       >> bg.r >> bg.g >> bg.b >> bg.a >> se.r >> se.g >> se.b >> se.a
                       >> tc.r >> tc.g >> tc.b >> tc.a >> stc.r >> stc.g >> stc.b >> stc.a
                       >> sp >> tb->cornerRadius >> it >> count;
                    tb->anchor = (UIAnchor)an; tb->background = bg; tb->selected = se;
                    tb->textColor = tc; tb->selectedTextColor = stc;
                    tb->shape = (UIShape)sp; tb->interactable = (it != 0);
                    tb->tabs.clear();
                    for (std::size_t k = 0; k < count && in; ++k) tb->tabs.push_back(ReadQuoted(in));
                    in >> std::ws;   // optional per-tab content pages (added later)
                    if (std::isdigit(in.peek())) {
                        std::size_t pc = 0; in >> pc;
                        tb->pages.clear();
                        for (std::size_t k = 0; k < pc && in; ++k) tb->pages.push_back(ReadQuoted(in));
                    }
                    in >> std::ws;   // optional (v3): style, sizing, states, motion, keys, extras
                    if (std::isdigit(in.peek())) {
                        int st = 0, sz = 0, an = 1, hk = 1, wr = 0, fo = 1;
                        Color hc, pc, bc, btc;
                        in >> st >> sz >> tb->spacing >> tb->padding >> tb->fontScale >> tb->minTabSize
                           >> tb->indicatorThickness >> tb->iconSize
                           >> hc.r >> hc.g >> hc.b >> hc.a >> pc.r >> pc.g >> pc.b >> pc.a
                           >> bc.r >> bc.g >> bc.b >> bc.a >> btc.r >> btc.g >> btc.b >> btc.a
                           >> an >> tb->animSpeed >> hk;
                        tb->style = (UITabs::Style)st; tb->sizing = (UITabs::Sizing)sz;
                        tb->hoverColor = hc; tb->pressedColor = pc; tb->badgeColor = bc; tb->badgeTextColor = btc;
                        tb->animate = (an != 0); tb->hotkeys = (hk != 0);
                        tb->prevKey = ReadQuoted(in); tb->nextKey = ReadQuoted(in);
                        in >> wr >> fo;
                        tb->wrapAround = (wr != 0); tb->focusable = (fo != 0);
                        std::size_t ic = 0, bn = 0, dn = 0;
                        in >> ic; tb->icons.clear();
                        for (std::size_t k = 0; k < ic && in; ++k) tb->icons.push_back(ReadQuoted(in));
                        in >> bn; tb->badges.clear();
                        for (std::size_t k = 0; k < bn && in; ++k) { int b = 0; in >> b; tb->badges.push_back(b); }
                        in >> dn; tb->disabled.clear();
                        for (std::size_t k = 0; k < dn && in; ++k) { int d = 0; in >> d; tb->disabled.push_back(d != 0); }
                        in >> std::ws; // grok/ui-controls: optional closable
                        if (std::isdigit(in.peek())) { int cl = 0; in >> cl; tb->closable = (cl != 0); }
                    } else {
                        // Saved before tab keys / tab focus existed: keep Q/E, the gamepad
                        // shoulders and A/D doing whatever the game already uses them for.
                        tb->hotkeys = false; tb->focusable = false;
                    }
                } else if (field == "uidropdown") {
                    auto* dd = go->AddComponent<UIDropdown>();
                    int an = 0; std::size_t count = 0;
                    Color c, h, l, t, b;
                    in >> dd->position.x >> dd->position.y >> dd->size.x >> dd->size.y
                       >> an >> dd->value
                       >> c.r >> c.g >> c.b >> c.a >> h.r >> h.g >> h.b >> h.a
                       >> l.r >> l.g >> l.b >> l.a >> t.r >> t.g >> t.b >> t.a
                       >> b.r >> b.g >> b.b >> b.a >> count;
                    dd->anchor = (UIAnchor)an;
                    dd->color = c; dd->hoverColor = h; dd->listColor = l;
                    dd->textColor = t; dd->borderColor = b;
                    dd->options.clear();
                    for (std::size_t k = 0; k < count && in; ++k) dd->options.push_back(ReadQuoted(in));
                    in >> std::ws; // optional interactable (added later)
                    if (std::isdigit(in.peek())) { int it = 1; in >> it; dd->interactable = (it != 0); }
                    in >> std::ws; // optional placeholder (added later)
                    if (in.peek() == '"') dd->placeholder = ReadQuoted(in);
                    in >> std::ws; // optional shape + corner radius (added later)
                    if (std::isdigit(in.peek())) { int sp = 0; in >> sp >> dd->cornerRadius; dd->shape = (UIShape)sp; }
                    in >> std::ws; // grok/ui-controls: optional search, row cap, glyphs
                    if (std::isdigit(in.peek())) {
                        int se = 0; std::size_t ic = 0;
                        in >> se >> dd->maxVisible >> ic;
                        dd->searchable = (se != 0);
                        dd->optionIcons.clear();
                        for (std::size_t k = 0; k < ic && in; ++k) dd->optionIcons.push_back(ReadQuoted(in));
                    }
                } else if (field == "uibind") {
                    auto* tb = go->AddComponent<UITextBind>();
                    tb->format = ReadQuoted(in);
                } else if (field == "uibarbind") {
                    auto* bb = go->AddComponent<UIBarBind>();
                    bb->var = ReadQuoted(in);
                    in >> bb->min >> bb->max;
                } else if (field == "uidraggable") {
                    auto* dg = go->AddComponent<UIDraggable>();
                    int rs = 0, at = 0; in >> rs >> at;
                    dg->returnToStart = (rs != 0); dg->anyTarget = (at != 0);
                    in >> std::ws;
                    if (std::isdigit(in.peek())) { int sn = 0; in >> sn; dg->snapToSlot = (sn != 0); }
                    in >> std::ws;
                    if (std::isdigit(in.peek())) {
                        int ax = 0, bf = 0; in >> ax >> dg->dragThreshold >> bf;
                        dg->axis = (UIDraggable::Axis)ax; dg->bringToFront = (bf != 0);
                    }
                } else if (field == "uidroptarget") {
                    auto* dt = go->AddComponent<UIDropTarget>();
                    in >> std::ws;
                    if (in.peek() == '"') dt->acceptTag = ReadQuoted(in);
                    in >> std::ws;
                    if (std::isdigit(in.peek())) {
                        int sh = 1; in >> sh; dt->showHighlight = (sh != 0);
                        in >> dt->highlight.r >> dt->highlight.g
                           >> dt->highlight.b >> dt->highlight.a;
                    }
                    in >> std::ws;               // extended fields (optional)
                    if (std::isdigit(in.peek())) {
                        int bg = 0, sc = 1;
                        in >> bg >> dt->background.r >> dt->background.g >> dt->background.b >> dt->background.a
                           >> dt->cornerRadius >> dt->borderWidth
                           >> dt->borderColor.r >> dt->borderColor.g >> dt->borderColor.b >> dt->borderColor.a
                           >> dt->rejectHighlight.r >> dt->rejectHighlight.g >> dt->rejectHighlight.b >> dt->rejectHighlight.a
                           >> sc;
                        dt->drawBackground = (bg != 0); dt->snapToCenter = (sc != 0);
                    }
                } else if (field == "draggable") {
                    auto* dg = go->AddComponent<Draggable>();
                    int rs = 0, at = 0, sn = 0, ax = 0, bf = 0;
                    in >> rs >> at >> sn >> ax >> dg->dragThreshold >> bf;
                    dg->returnToStart = (rs != 0); dg->anyTarget = (at != 0);
                    dg->snapToZone = (sn != 0); dg->axis = (Draggable::Axis)ax;
                    dg->bringToFront = (bf != 0);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '-')
                        in >> dg->gridX >> dg->gridY >> dg->dragScale;
                } else if (field == "dropzone") {
                    auto* dz = go->AddComponent<DropZone>();
                    in >> std::ws;
                    if (in.peek() == '"') dz->acceptTag = ReadQuoted(in);
                } else if (field == "uianim") {
                    auto* ua = go->AddComponent<UIAnimation>();
                    int it = 1, lp = 0, ot = 0, es = (int)Ease::CubicOut, pos = 1, ros = 1, hao = 1;
                    in >> it >> lp >> ot >> ua->duration >> ua->delay >> ua->distance >> es
                       >> ua->loopSpeed >> ua->loopAmount >> pos >> ros >> hao;
                    ua->intro = (UIAnimation::Intro)it; ua->loop = (UIAnimation::Loop)lp; ua->outro = (UIAnimation::Outro)ot;
                    ua->ease = (Ease)es; ua->playOnStart = pos != 0; ua->replayOnShow = ros != 0; ua->hideAfterOutro = hao != 0;
                } else if (field == "tweenanim") {
                    auto* ta = go->AddComponent<TweenAnimation>();
                    int rel = 0, frm = 0, ap = 1, ak = 1, us = 0, en = 1;
                    in >> ta->type >> ta->endValue.x >> ta->endValue.y >> ta->endValue.z
                       >> ta->endColor.r >> ta->endColor.g >> ta->endColor.b >> ta->endColor.a
                       >> ta->endFloat >> rel >> frm >> ta->duration >> ta->delay >> ta->ease >> ta->loops >> ta->loopType
                       >> ap >> ak >> us >> ta->vibrato >> ta->elasticity >> ta->jumps >> en;
                    ta->relative = rel != 0; ta->from = frm != 0; ta->autoPlay = ap != 0; ta->autoKill = ak != 0;
                    ta->unscaled = us != 0; ta->enabled = en != 0;
                    ta->id = ReadQuoted(in);
                    in >> std::ws;   // optional tail: clock channel, ease as text, then decay, speed based, tag
                    if (in.peek() == '"') {
                        ta->clock = ReadQuoted(in);
                        in >> std::ws;
                        if (in.peek() == '"') ta->easeCurve = ReadQuoted(in);
                        in >> std::ws;
                        if (std::isdigit(in.peek())) {   // a number: the writer's second part (a quoted field would be a newer build's)
                            int sb = 0;
                            in >> ta->decay >> sb;
                            ta->speedBased = sb != 0;
                            in >> std::ws;
                            if (in.peek() == '"') ta->tag = ReadQuoted(in);
                        }
                    }
                } else if (field == "uitooltip") {
                    auto* tt = go->AddComponent<UITooltip>();
                    tt->text = ReadQuoted(in);
                    Color bg, tc, bc;
                    in >> tt->delay
                       >> bg.r >> bg.g >> bg.b >> bg.a
                       >> tc.r >> tc.g >> tc.b >> tc.a
                       >> bc.r >> bc.g >> bc.b >> bc.a;
                    tt->background = bg; tt->textColor = tc; tt->borderColor = bc;
                    in >> std::ws; // grok/ui-controls: optional rich text + cursor offset
                    if (std::isdigit(in.peek())) {
                        int rich = 0; in >> rich >> tt->offset.x >> tt->offset.y;
                        tt->richText = (rich != 0);
                    }
                } else { fieldDone = false; }
                }
                if (!fieldDone) { fieldDone = true;
                if (field == "uilayout") {
                    auto* lg = go->AddComponent<UILayoutGroup>();
                    std::string rest; std::getline(in, rest);   // whole line: newer files carry extra fields
                    std::istringstream rs(rest);
                    int dir = 0, an = 0, cols = -1; float sy = -1.0f;
                    rs >> dir >> an >> lg->origin.x >> lg->origin.y >> lg->spacing >> lg->padding;
                    rs >> cols; rs >> sy;                        // absent in older scenes -> keep defaults
                    lg->direction = (UILayoutGroup::Direction)dir; lg->anchor = (UIAnchor)an;
                    if (cols >= 1) lg->columns = cols;
                    if (sy >= 0.0f) lg->spacingY = sy;
                    float bw = 0.0f, bh = 0.0f; int js = 0, al = 0, wr = 0;
                    if (rs >> bw >> bh >> js >> al >> wr) {   // flex fields (newer scenes)
                        lg->boxSize = {bw, bh};
                        lg->justify = (UILayoutGroup::Justify)js; lg->align = (UILayoutGroup::Align)al; lg->wrap = wr != 0;
                    }
                    lg->Arrange();
                } else if (field == "uiflexitem") {
                    auto* fi = go->AddComponent<UIFlexItem>();
                    int as = 0;
                    in >> fi->grow >> fi->shrink >> fi->baseSize.x >> fi->baseSize.y >> as;
                    fi->alignSelf = (UIFlexItem::Align)as;
                } else if (field == "uiframe") {
                    // Read after or before the widget's own line: the end of the parse
                    // makes the frame the source of truth and mirrors it back.
                    // Rotation and scale are optional so a scene saved before them loads unchanged.
                    auto* fr = go->AddComponent<UIFrame>();
                    std::string rest;
                    std::getline(in, rest);
                    std::istringstream rs(rest);
                    rs >> fr->anchorMin.x >> fr->anchorMin.y >> fr->anchorMax.x >> fr->anchorMax.y
                       >> fr->pivot.x >> fr->pivot.y >> fr->position.x >> fr->position.y >> fr->size.x >> fr->size.y;
                    float rot = 0.0f, sx = 1.0f, sy = 1.0f;
                    if (rs >> rot >> sx >> sy) { fr->rotation = rot; fr->scale = {sx, sy}; }
                } else if (field == "uimodal") {
                    auto* md = go->AddComponent<UIModal>();
                    int dim = 1, cc = 1, co = 0;
                    in >> dim >> md->dimColor.r >> md->dimColor.g >> md->dimColor.b >> md->dimColor.a >> cc >> co;
                    md->dimBackground = dim != 0; md->closeOnCancel = cc != 0; md->closeOnClickOutside = co != 0;
                    // grok/ui-toast: the style tail is optional. A letter means the next field.
                    in >> std::ws;
                    if (std::isdigit(in.peek())) {
                        int styled = 0;
                        in >> styled >> md->cardRadius;
                        auto c4 = [&](Color& c) { in >> c.r >> c.g >> c.b >> c.a; };
                        c4(md->cardColor); c4(md->titleColor); c4(md->bodyColor);
                        c4(md->confirmColor); c4(md->cancelColor);
                        md->styleCard = styled != 0;
                        md->titleText = ReadQuoted(in);
                        md->bodyText = ReadQuoted(in);
                        md->confirmText = ReadQuoted(in);
                        md->cancelText = ReadQuoted(in);
                    }
                } else if (field == "uitoast") {   // grok/ui-toast
                    auto* toast = go->AddComponent<UIToast>();
                    int icon = 1;
                    in >> toast->corner >> toast->duration >> toast->maxStack
                       >> toast->margin >> toast->gap >> toast->width >> toast->height >> icon;
                    toast->showIcon = icon != 0;
                } else if (field == "uicontext") {
                    auto* cm = go->AddComponent<UIContextMenu>();
                    auto c4 = [&](Color& c) { in >> c.r >> c.g >> c.b >> c.a; };
                    int any = 0; std::size_t n = 0;
                    in >> any >> cm->itemHeight >> cm->width >> cm->textSize;
                    c4(cm->background); c4(cm->hoverColor); c4(cm->textColor); c4(cm->borderColor);
                    in >> n;
                    cm->anywhere = any != 0;
                    cm->items.clear();
                    for (std::size_t i = 0; i < n && in; ++i) cm->items.push_back(ReadQuoted(in));
                } else if (field == "uikeybind") {
                    auto* kb = go->AddComponent<UIKeybindField>();
                    auto c4 = [&](Color& c) { in >> c.r >> c.g >> c.b >> c.a; };
                    int an = 0, k = 32, dk = 32, inter = 1, foc = 1;
                    in >> kb->position.x >> kb->position.y >> kb->size.x >> kb->size.y >> an;
                    kb->action = ReadQuoted(in);
                    in >> k >> dk;
                    kb->prefKey = ReadQuoted(in);
                    in >> kb->textSize;
                    c4(kb->background); c4(kb->keyColor); c4(kb->listenColor); c4(kb->textColor);
                    in >> inter >> foc;
                    {   // optional (newer files): the InputActions action it rebinds
                        std::string rest; std::getline(in, rest);
                        std::istringstream rs(rest);
                        int gp = 0, pt = -1;
                        if (rs >> std::ws && rs.peek() == '"') {
                            kb->inputAction = ReadQuoted(rs);
                            if (rs >> gp) kb->gamepad = gp != 0;
                            if (rs >> pt) kb->part = pt;
                        }
                    }
                    kb->anchor = (UIAnchor)an; kb->key = (char)k; kb->defaultKey = (char)dk;
                    kb->interactable = inter != 0; kb->focusable = foc != 0;
                } else if (field == "uimask") {
                    auto* mk = go->AddComponent<UIMask>();
                    int sg = 1; in >> sg; mk->showGraphic = sg != 0;
                } else if (field == "uilist") {
                    auto* lv = go->AddComponent<UIListView>();
                    int an = 0; std::size_t n = 0;
                    in >> lv->position.x >> lv->position.y >> lv->size.x >> lv->size.y >> an
                       >> lv->background.r >> lv->background.g >> lv->background.b >> lv->background.a
                       >> lv->barColor.r >> lv->barColor.g >> lv->barColor.b >> lv->barColor.a
                       >> lv->itemHeight >> lv->spacing >> lv->itemCount >> lv->selected >> lv->wheelSpeed >> n;
                    lv->anchor = (UIAnchor)an;
                    for (std::size_t i = 0; i < n && in; ++i) lv->items.push_back(ReadQuoted(in));
                } else if (field == "uiscroll") {
                    auto* sv = go->AddComponent<UIScrollView>();
                    std::string rest; std::getline(in, rest);   // whole line: newer files carry extra fields
                    std::istringstream rs(rest);
                    int an = 0; Color c;
                    rs >> sv->position.x >> sv->position.y >> sv->size.x >> sv->size.y
                       >> an >> sv->contentHeight >> c.r >> c.g >> c.b >> c.a;
                    sv->anchor = (UIAnchor)an; sv->background = c;
                    int ax = 0, ac = 1; float cw = 0.0f;
                    if (rs >> ax >> cw >> ac) { sv->axis = (UIScrollView::Axis)ax; sv->contentWidth = cw; sv->autoContent = ac != 0; }
                    float ine = 0.0f; int el = 0, bs = 0;   // grok/ui-controls
                    if (rs >> ine >> el >> bs) { sv->inertia = ine; sv->elastic = el != 0; sv->barStyle = bs; }
                } else if (field == "canvas") {
                    auto* cv = go->AddComponent<Canvas>();
                    int sm = 0;
                    in >> sm >> cv->referenceResolution.x >> cv->referenceResolution.y
                       >> cv->matchWidthOrHeight >> cv->scaleFactor >> cv->sortOrder;
                    cv->scaleMode = (Canvas::ScaleMode)sm;
                    cv->screenMatch = Canvas::ScreenMatch::MatchWidthOrHeight;   // what canvases saved before it used
                    in >> std::ws;                       // optional visible + opacity
                    int p = in.peek();
                    if (std::isdigit(p) || p == '-') {
                        int vis = 1; in >> vis >> cv->opacity; cv->visible = (vis != 0);
                        in >> std::ws;                   // optional world-space canvas fields
                        int p2 = in.peek();
                        if (std::isdigit(p2) || p2 == '-') {
                            int ws = 0;
                            in >> ws >> cv->designResolution.x >> cv->designResolution.y >> cv->worldPixelsPerUnit;
                            cv->worldSpace = (ws != 0);
                            in >> std::ws;
                            int p3 = in.peek();
                            if (std::isdigit(p3) || p3 == '-') { int bb = 1; in >> bb; cv->billboard = (bb != 0); }
                            in >> std::ws;
                            if (LooksNumeric(in)) { int smm = 0; in >> smm; cv->screenMatch = (Canvas::ScreenMatch)std::clamp(smm, 0, 2); }
                        }
                    }
                } else if (field == "worldui3d") {
                    auto* w3 = go->AddComponent<WorldSpaceUI>();
                    in >> w3->pixelsPerUnit;
                    in >> std::ws;
                    if (std::isdigit(in.peek())) { int bb = 1; in >> bb; w3->billboard = (bb != 0); }
                    in >> std::ws;
                    if (std::isdigit(in.peek())) { int cs = 0; in >> cs; w3->constantSize = (cs != 0); }
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '-' || in.peek() == '.') in >> w3->constantScale;
                } else if (field == "eventsystem") {
                    auto* es = go->AddComponent<EventSystem>();
                    KvIn k(in);   // a bare record (older files) keeps every default
                    k.f("drag", es->dragThreshold); k.b("nav", es->navigation); k.b("blockWorld", es->blockWorldClicks);
                    k.str("submit", es->submitAction); k.str("cancel", es->cancelAction);
                    k.str("navigate", es->navigateAction); k.str("first", es->firstSelected);
                } else if (field == "uiprogress") {
                    auto* pb = go->AddComponent<UIProgressBar>();
                    Color bg, fl;
                    in >> pb->position.x >> pb->position.y >> pb->size.x >> pb->size.y >> pb->value
                       >> bg.r >> bg.g >> bg.b >> bg.a >> fl.r >> fl.g >> fl.b >> fl.a;
                    pb->background = bg; pb->fill = fl;
                    ReadAnchor(in, pb->anchor);
                    in >> std::ws; // optional style block (added later)
                    if (std::isdigit(in.peek())) {
                        int sp = 0; Color tc;
                        in >> pb->cornerRadius >> sp >> tc.r >> tc.g >> tc.b >> tc.a;
                        pb->showPercent = (sp != 0); pb->textColor = tc;
                        in >> std::ws; // optional fill direction (added later)
                        if (std::isdigit(in.peek())) { int fd = 0; in >> fd; pb->fillDir = (UIProgressBar::FillDir)fd; }
                        in >> std::ws; // optional shape + gradient fill (added later)
                        if (std::isdigit(in.peek())) {
                            int sp = 0, gf = 0; Color fe;
                            in >> sp >> gf >> fe.r >> fe.g >> fe.b >> fe.a;
                            pb->shape = (UIShape)sp; pb->gradientFill = (gf != 0); pb->fillEnd = fe;
                            in >> std::ws; // grok/ui-options: segments, trail, color-by-value, text
                            if (std::isdigit(in.peek()) || in.peek() == '-' || in.peek() == '.') {
                                int seg = 0, cb = 0; Color tc, lo, hi;
                                in >> seg >> pb->segmentGap >> pb->trailDelay
                                   >> tc.r >> tc.g >> tc.b >> tc.a >> cb
                                   >> lo.r >> lo.g >> lo.b >> lo.a
                                   >> hi.r >> hi.g >> hi.b >> hi.a;
                                pb->segments = seg; pb->trailColor = tc;
                                pb->colorByValue = (cb != 0); pb->lowColor = lo; pb->highColor = hi;
                                in >> std::ws;
                                if (in.peek() == '"') pb->textFormat = ReadQuoted(in);
                            }
                        }
                    }
                } else if (field == "uiradial") {
                    auto* rp = go->AddComponent<UIRadialProgress>();
                    int cw = 1, an = 0, sp = 0; Color bg, fl, tc;
                    in >> rp->position.x >> rp->position.y >> rp->size.x >> rp->size.y >> rp->value
                       >> rp->thickness >> rp->startAngle >> cw
                       >> bg.r >> bg.g >> bg.b >> bg.a >> fl.r >> fl.g >> fl.b >> fl.a
                       >> an >> sp >> tc.r >> tc.g >> tc.b >> tc.a;
                    rp->clockwise = (cw != 0); rp->background = bg; rp->fill = fl;
                    rp->anchor = (UIAnchor)an; rp->showPercent = (sp != 0); rp->textColor = tc;
                    in >> std::ws; // optional spinner (added later)
                    if (std::isdigit(in.peek())) { int sn = 0; in >> sn >> rp->spinSpeed; rp->spin = (sn != 0); }
                } else if (field == "minimap") {
                    auto* mm = go->AddComponent<Minimap>();
                    Color bg, bd, tc; int xz = 1, an = 0;
                    in >> mm->position.x >> mm->position.y >> mm->size.x >> mm->size.y
                       >> bg.r >> bg.g >> bg.b >> bg.a >> bd.r >> bd.g >> bd.b >> bd.a
                       >> mm->borderWidth;
                    mm->target = ReadQuoted(in);
                    in >> tc.r >> tc.g >> tc.b >> tc.a
                       >> mm->worldPerPixel >> xz >> mm->blipSize >> an;
                    mm->background = bg; mm->border = bd; mm->targetColor = tc;
                    mm->useXZ = (xz != 0); mm->anchor = (UIAnchor)an;
                    // Appended v2 fields (older scenes simply stop here).
                    in >> std::ws;
                    int pk = in.peek();
                    if (std::isdigit(pk) || pk == '-' || pk == '+' || pk == '.') {
                        int ci = 0, rt = 0, pa = 1, cb = 0, sg = 0;
                        Color gc;
                        in >> ci >> rt >> pa >> cb >> sg
                           >> gc.r >> gc.g >> gc.b >> gc.a >> mm->gridSpacing;
                        mm->circular = (ci != 0); mm->rotateWithTarget = (rt != 0);
                        mm->playerArrow = (pa != 0); mm->clampBlips = (cb != 0);
                        mm->showGrid = (sg != 0); mm->gridColor = gc;
                        // v3: GTA-style custom map (starts with a quoted texture path).
                        in >> std::ws;
                        if (in.peek() == '"') {
                            mm->mapTexture = ReadQuoted(in);
                            int fk = 'm';
                            in >> mm->mapWorldSize >> mm->mapWorldCenter.x >> mm->mapWorldCenter.y
                               >> fk >> mm->fullscreenZoom >> mm->fullscreenFrac;
                            mm->fullscreenKey = (char)fk;
                            // v4: zoom keys + waypoint markers.
                            in >> std::ws;
                            int pk7 = in.peek();
                            if (std::isdigit(pk7) || pk7 == '-' || pk7 == '+' || pk7 == '.') {
                                int zi = 0, zo = 0; std::size_t mn = 0;
                                in >> zi >> zo >> mm->minZoom >> mm->maxZoom >> mm->zoomStep >> mn;
                                mm->zoomInKey = (char)zi; mm->zoomOutKey = (char)zo;
                                mm->markers.clear();
                                for (std::size_t mi = 0; mi < mn && in; ++mi) {
                                    Minimap::Marker mk;
                                    in >> mk.world.x >> mk.world.y
                                       >> mk.color.r >> mk.color.g >> mk.color.b >> mk.color.a >> mk.size;
                                    mm->markers.push_back(mk);
                                }
                                // v5: self / view cone / range rings / north.
                                in >> std::ws;
                                int pk8 = in.peek();
                                if (std::isdigit(pk8) || pk8 == '-' || pk8 == '+' || pk8 == '.') {
                                    int ss = 1, vc = 0, sn = 0;
                                    Color vcc, rc2, nc;
                                    in >> ss >> vc >> mm->viewConeAngle >> mm->viewConeLength
                                       >> vcc.r >> vcc.g >> vcc.b >> vcc.a
                                       >> mm->rangeRings
                                       >> rc2.r >> rc2.g >> rc2.b >> rc2.a
                                       >> sn >> nc.r >> nc.g >> nc.b >> nc.a;
                                    mm->showSelf = (ss != 0); mm->viewCone = (vc != 0);
                                    mm->viewConeColor = vcc; mm->ringColor = rc2;
                                    mm->showNorth = (sn != 0); mm->northColor = nc;
                                    // v6: labels + route line + per-marker labels.
                                    in >> std::ws;
                                    int pk9 = in.peek();
                                    if (std::isdigit(pk9) || pk9 == '-' || pk9 == '+' || pk9 == '.') {
                                        int sl = 0; Color lc, rcl;
                                        in >> sl >> lc.r >> lc.g >> lc.b >> lc.a >> mm->labelSize
                                           >> mm->routeMarker
                                           >> rcl.r >> rcl.g >> rcl.b >> rcl.a >> mm->routeWidth;
                                        mm->showLabels = (sl != 0); mm->labelColor = lc; mm->routeColor = rcl;
                                        for (auto& mk : mm->markers) mk.label = ReadQuoted(in);
                                        // v7: authorable vector map shapes.
                                        in >> std::ws;
                                        int pk10 = in.peek();
                                        if (std::isdigit(pk10) || pk10 == '-' || pk10 == '+' || pk10 == '.') {
                                            std::size_t shn = 0; in >> shn;
                                            mm->mapShapes.clear();
                                            for (std::size_t si = 0; si < shn && in; ++si) {
                                                Minimap::MapShape sh; int kd = 0, fl = 1; std::size_t pn = 0;
                                                in >> kd >> sh.color.r >> sh.color.g >> sh.color.b >> sh.color.a
                                                   >> sh.thickness >> fl >> pn;
                                                sh.kind = (Minimap::MapShape::Kind)kd; sh.filled = (fl != 0);
                                                for (std::size_t pi = 0; pi < pn && in; ++pi) {
                                                    Vec2 p; in >> p.x >> p.y; sh.points.push_back(p);
                                                }
                                                mm->mapShapes.push_back(std::move(sh));
                                            }
                                            // v8: radar blip range.
                                            in >> std::ws;
                                            int pk11 = in.peek();
                                            if (std::isdigit(pk11) || pk11 == '-' || pk11 == '+' || pk11 == '.') {
                                                in >> mm->blipRange;
                                                // v9: user waypoint (click-to-place).
                                                in >> std::ws;
                                                int pk12 = in.peek();
                                                if (std::isdigit(pk12) || pk12 == '-' || pk12 == '+' || pk12 == '.') {
                                                    int mcw = 1, huw = 0; Color uwc;
                                                    in >> mcw >> huw >> mm->userWaypoint.x >> mm->userWaypoint.y
                                                       >> uwc.r >> uwc.g >> uwc.b >> uwc.a;
                                                    mm->mapClickWaypoint = (mcw != 0);
                                                    mm->hasUserWaypoint = (huw != 0);
                                                    mm->userWaypointColor = uwc;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else if (field == "minimapblip") {
                    auto* bl = go->AddComponent<MinimapBlip>();
                    Color c; int sq = 1;
                    in >> c.r >> c.g >> c.b >> c.a >> bl->size >> sq;
                    bl->color = c; bl->square = (sq != 0);
                    bl->shape = (sq != 0) ? MinimapBlip::Shape::Square : MinimapBlip::Shape::Dot;
                    // Appended v2 fields.
                    in >> std::ws;
                    int pk = in.peek();
                    if (std::isdigit(pk) || pk == '-' || pk == '+' || pk == '.') {
                        int sh = 0, rot = 0;
                        in >> sh >> rot;
                        bl->shape = (MinimapBlip::Shape)sh;
                        bl->rotateWithObject = (rot != 0);
                        in >> std::ws;
                        if (in.peek() == '"') bl->icon = ReadQuoted(in);    // v3: icon texture
                        in >> std::ws;
                        if (in.peek() == '"') bl->label = ReadQuoted(in);   // v4: label
                    }
                } else if (field == "crosshair") {
                    moduleRecord("weapons");
                } else if (field == "uislotgrid") {
                    auto* sg = go->AddComponent<UISlotGrid>();
                    int an = 0; std::size_t n = 0;
                    in >> sg->position.x >> sg->position.y >> an >> sg->columns >> sg->rows
                       >> sg->cell.x >> sg->cell.y >> sg->gap >> sg->value
                       >> sg->color.r >> sg->color.g >> sg->color.b >> sg->color.a
                       >> sg->selectedColor.r >> sg->selectedColor.g >> sg->selectedColor.b >> sg->selectedColor.a
                       >> sg->hoverColor.r >> sg->hoverColor.g >> sg->hoverColor.b >> sg->hoverColor.a
                       >> n;
                    sg->anchor = (UIAnchor)an;
                    sg->labels.clear();
                    for (std::size_t i = 0; i < n && in; ++i) sg->labels.push_back(ReadQuoted(in));
                    // Children are parented after every object exists. Update builds any slot
                    // that wasn't saved; calling Rebuild here would duplicate them.
                } else if (field == "uicolorpicker") { // grok/ui-color
                    auto* cp = go->AddComponent<UIColorPicker>();
                    int an = 0; std::size_t n = 0;
                    in >> cp->position.x >> cp->position.y >> an >> cp->swatch >> cp->gap >> cp->value >> n;
                    cp->anchor = (UIAnchor)an;
                    cp->swatches.clear();
                    for (std::size_t i = 0; i < n && in; ++i) {
                        Color c;
                        in >> c.r >> c.g >> c.b >> c.a;
                        cp->swatches.push_back(c);
                    }
                    {   // slice 3 fields; an older file ends after the swatches. Read into temporaries so a short line leaves the defaults.
                        int adv = 0, custom = 0; float hh = 0, ss = 0, vv = 0, aa = 1, sq = 140;
                        if (in >> adv >> hh >> ss >> vv >> aa >> custom >> sq) {
                            cp->advanced = adv != 0; cp->useCustom = custom != 0; cp->hue = hh; cp->sat = ss; cp->val = vv; cp->alpha = aa; cp->squareSize = sq;
                        } else in.clear();
                    }
                } else if (field == "uisafearea") {
                    auto* sa = go->AddComponent<UISafeArea>();
                    int l = 1, t = 1, r = 1, b = 1, en = 1;
                    in >> l >> t >> r >> b >> en;
                    sa->left = l != 0; sa->top = t != 0; sa->right = r != 0; sa->bottom = b != 0; sa->enabled = en != 0;
                } else if (field == "uirawimage") {
                    auto* ri = go->AddComponent<UIRawImage>();
                    int en = 1;
                    in >> ri->position.x >> ri->position.y >> ri->size.x >> ri->size.y
                       >> ri->color.r >> ri->color.g >> ri->color.b >> ri->color.a
                       >> ri->uvX >> ri->uvY >> ri->uvW >> ri->uvH >> ri->scroll.x >> ri->scroll.y >> en;
                    ri->enabled = en != 0;
                    ri->texture = ReadQuoted(in);
                    ReadAnchor(in, ri->anchor);
                } else if (field == "uiimage") {
                    auto* im = go->AddComponent<UIImage>();
                    Color c;
                    in >> im->position.x >> im->position.y >> im->size.x >> im->size.y
                       >> c.r >> c.g >> c.b >> c.a;
                    im->color = c;
                    im->texture = ReadQuoted(in);
                    ReadAnchor(in, im->anchor);
                    // Optional nine-slice block (added later; absent in older files).
                    in >> std::ws;
                    int nk = in.peek();
                    if (nk >= '0' && nk <= '9') {
                        int ns = 0; in >> ns >> im->border; im->nineSlice = (ns != 0);
                    }
                    in >> std::ws; // optional fill block (added later)
                    if (std::isdigit(in.peek())) {
                        int fm = 0; in >> fm >> im->fillAmount >> im->cornerRadius;
                        im->fillMode = (UIImage::FillMode)fm;
                    }
                    in >> std::ws; // optional shape (added later)
                    if (std::isdigit(in.peek())) {
                        int sp = 0; in >> sp; im->shape = (UIShape)sp;
                        in >> std::ws; // optional flip + preserve-aspect + frame border (newest)
                        if (std::isdigit(in.peek())) {
                            int fx = 0, fy = 0, pa = 0; Color bc;
                            in >> fx >> fy >> pa >> im->borderWidth
                               >> bc.r >> bc.g >> bc.b >> bc.a;
                            im->flipX = (fx != 0); im->flipY = (fy != 0);
                            im->preserveAspect = (pa != 0); im->borderColor = bc;
                            in >> std::ws;   // optional corner mask + drop shadow (newest)
                            if (std::isdigit(in.peek())) {
                                in >> im->cornerMask;
                                int sh = 0; Color sc;
                                in >> sh >> sc.r >> sc.g >> sc.b >> sc.a
                                   >> im->shadowOffset.x >> im->shadowOffset.y >> im->shadowSoftness;
                                im->shadow = (sh != 0); im->shadowColor = sc;
                            }
                        }
                    }
                } else if (field == "uislider") {
                    auto* sl = go->AddComponent<UISlider>();
                    Color bg, fl, kn;
                    in >> sl->position.x >> sl->position.y >> sl->size.x >> sl->size.y
                       >> sl->value >> sl->minValue >> sl->maxValue
                       >> bg.r >> bg.g >> bg.b >> bg.a >> fl.r >> fl.g >> fl.b >> fl.a
                       >> kn.r >> kn.g >> kn.b >> kn.a;
                    sl->background = bg; sl->fill = fl; sl->knob = kn;
                    ReadAnchor(in, sl->anchor);
                    in >> std::ws; // optional style block (added later)
                    if (std::isdigit(in.peek())) {
                        int sv = 0; Color tc;
                        in >> sl->cornerRadius >> sl->knobSize >> sv >> tc.r >> tc.g >> tc.b >> tc.a;
                        sl->showValue = (sv != 0); sl->textColor = tc;
                        in >> std::ws; // optional whole-numbers flag (added later)
                        if (std::isdigit(in.peek())) { int wn = 0; in >> wn; sl->wholeNumbers = (wn != 0); }
                        in >> std::ws;
                        if (std::isdigit(in.peek())) { int it = 1; in >> it; sl->interactable = (it != 0); }
                        in >> std::ws;
                        if (std::isdigit(in.peek())) { int vt = 0; in >> vt; sl->vertical = (vt != 0); }
                        in >> std::ws; // optional track shape + round knob (added later)
                        if (std::isdigit(in.peek())) {
                            int ts = 0, rk = 0; in >> ts >> rk;
                            sl->trackShape = (UIShape)ts; sl->roundKnob = (rk != 0);
                            in >> std::ws; // grok/ui-options: step, gradient, handle, format
                            if (std::isdigit(in.peek()) || in.peek() == '-' || in.peek() == '.') {
                                int fg = 0, ks = -1; Color fe;
                                in >> sl->step >> fg >> fe.r >> fe.g >> fe.b >> fe.a >> ks;
                                sl->fillGradient = (fg != 0); sl->fillEnd = fe; sl->knobShape = ks;
                                in >> std::ws;
                                if (in.peek() == '"') sl->valueFormat = ReadQuoted(in);
                            }
                        }
                    }
                } else if (field == "uistepper") {
                    auto* st = go->AddComponent<UIStepper>();
                    Color bg, bt, tc; int wn = 1, wr = 0, sh = 0, it = 1;
                    in >> st->position.x >> st->position.y >> st->size.x >> st->size.y
                       >> st->value >> st->minValue >> st->maxValue >> st->step
                       >> wn >> wr
                       >> bg.r >> bg.g >> bg.b >> bg.a >> bt.r >> bt.g >> bt.b >> bt.a
                       >> tc.r >> tc.g >> tc.b >> tc.a;
                    st->wholeNumbers = (wn != 0); st->wrap = (wr != 0);
                    st->background = bg; st->button = bt; st->textColor = tc;
                    ReadAnchor(in, st->anchor);
                    in >> sh >> st->cornerRadius >> it;
                    st->shape = (UIShape)sh; st->interactable = (it != 0);
                } else if (TryReadMoreUI(field, in, go)) {
                } else if (field == "uirating") {
                    auto* rt = go->AddComponent<UIRating>();
                    Color on, off; int ah = 0, ro = 0;
                    in >> rt->position.x >> rt->position.y >> rt->size.x >> rt->size.y
                       >> rt->count >> rt->value >> ah >> ro
                       >> on.r >> on.g >> on.b >> on.a >> off.r >> off.g >> off.b >> off.a;
                    rt->allowHalf = (ah != 0); rt->readOnly = (ro != 0);
                    rt->on = on; rt->off = off;
                    ReadAnchor(in, rt->anchor);
                } else if (field == "uitoggle") {
                    auto* tg = go->AddComponent<UIToggle>();
                    tg->label = ReadQuoted(in);
                    Color b, c, t;
                    int on = 0;
                    in >> tg->position.x >> tg->position.y >> tg->size.x >> tg->size.y >> on
                       >> b.r >> b.g >> b.b >> b.a >> c.r >> c.g >> c.b >> c.a
                       >> t.r >> t.g >> t.b >> t.a;
                    tg->on = (on != 0);
                    tg->boxColor = b; tg->checkColor = c; tg->textColor = t;
                    ReadAnchor(in, tg->anchor);
                    in >> std::ws; // optional corner radius (added later)
                    if (std::isdigit(in.peek())) in >> tg->cornerRadius;
                    in >> std::ws; // optional switch style + knob color (added later)
                    if (std::isdigit(in.peek())) {
                        int st = 0; Color kc;
                        in >> st >> kc.r >> kc.g >> kc.b >> kc.a;
                        tg->style = (UIToggle::Style)st; tg->knobColor = kc;
                        in >> std::ws;
                        if (std::isdigit(in.peek())) { int it = 1; in >> it; tg->interactable = (it != 0); }
                        in >> std::ws; // optional animation speed (added later)
                        if (std::isdigit(in.peek())) in >> tg->animSpeed;
                    }
                } else if (field == "particles") {
                    auto* ps = go->AddComponent<ParticleSystem>();
                    int playing = 1, fade = 1;
                    unsigned long long seed = 0;
                    in >> ps->emissionRate >> ps->maxParticles >> playing
                       >> ps->startLifetime >> ps->startSize
                       >> ps->startColor.r >> ps->startColor.g >> ps->startColor.b >> ps->startColor.a
                       >> ps->startVelocity.x >> ps->startVelocity.y >> ps->velocityRandom
                       >> ps->gravity.x >> ps->gravity.y >> fade >> seed;
                    ps->playing = (playing != 0);
                    ps->fadeOverLife = (fade != 0);
                    ps->seed = static_cast<std::uint64_t>(seed);
                    // Appended v2 fields (older scenes simply stop here).
                    in >> std::ws;
                    int pk = in.peek();
                    if (std::isdigit(pk) || pk == '-' || pk == '+' || pk == '.') {
                        int colLife = 0, szLife = 0, shapeI = 0, loopI = 1;
                        in >> ps->endColor.r >> ps->endColor.g >> ps->endColor.b >> ps->endColor.a
                           >> colLife >> ps->endSize >> szLife
                           >> ps->startLifetimeRandom >> ps->startSizeRandom >> ps->speedRandom
                           >> shapeI >> ps->shapeRadius >> ps->shapeAngle
                           >> ps->boxSize.x >> ps->boxSize.y
                           >> ps->gravityModifier >> ps->damping
                           >> ps->duration >> loopI >> ps->burstCount >> ps->burstTime;
                        ps->colorOverLife = (colLife != 0);
                        ps->sizeOverLife = (szLife != 0);
                        ps->loop = (loopI != 0);
                        ps->shape = (ParticleSystem::Shape)shapeI;
                        // Appended v3 fields: 3D z-components (older v2 scenes stop here,
                        // leaving z at its 0/1 default so they still load flat-but-valid).
                        in >> std::ws;
                        int pk3 = in.peek();
                        if (std::isdigit(pk3) || pk3 == '-' || pk3 == '+' || pk3 == '.') {
                            in >> ps->startVelocity.z >> ps->gravity.z >> ps->boxSize.z;
                            // Appended v4 fields: sprite texture (quoted) + billboard rotation.
                            in >> std::ws;
                            if (in.peek() == '"') {
                                ps->texture = ReadQuoted(in);
                                in >> std::ws;
                                int pk4 = in.peek();
                                if (std::isdigit(pk4) || pk4 == '-' || pk4 == '+' || pk4 == '.') {
                                    in >> ps->startRotation >> ps->startRotationRandom
                                       >> ps->rotationSpeed >> ps->rotationSpeedRandom;
                                    // Appended v5 fields: ground-plane collision.
                                    in >> std::ws;
                                    int pk5 = in.peek();
                                    if (std::isdigit(pk5) || pk5 == '-' || pk5 == '+' || pk5 == '.') {
                                        int coll = 0;
                                        in >> coll >> ps->collisionY >> ps->bounce
                                           >> ps->collisionFriction >> ps->collisionLifeLoss;
                                        ps->collision = (coll != 0);
                                        // Appended v6 fields: render mode + stretch scale.
                                        in >> std::ws;
                                        int pk6 = in.peek();
                                        if (std::isdigit(pk6) || pk6 == '-' || pk6 == '+' || pk6 == '.') {
                                            int rm = 0; in >> rm >> ps->stretchScale;
                                            ps->renderMode = (ParticleSystem::RenderMode)rm;
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else if (field == "particlesterrain") {
                    auto* ps = LastOf(go->GetComponents<ParticleSystem>());   // the one the `particles` record above made
                    if (!ps) ps = go->AddComponent<ParticleSystem>();
                    int on = 0; in >> on;
                    ps->collideTerrain = (on != 0);
                } else if (field == "particleswind") {
                    auto* ps = LastOf(go->GetComponents<ParticleSystem>());   // the one the `particles` record above made
                    if (!ps) ps = go->AddComponent<ParticleSystem>();
                    in >> ps->windInfluence;
                } else if (field == "particlesfx") {
                    // Gradients/curves + motion modules (follows the "particles" line).
                    auto* ps = LastOf(go->GetComponents<ParticleSystem>());   // the one that line made, when an object has several
                    if (!ps) ps = go->AddComponent<ParticleSystem>();
                    std::string rest; std::getline(in, rest);   // whole line: tolerant of short/extended data
                    std::istringstream rs(rest);
                    int ver = 0, space = 0, spdLife = 0;
                    rs >> ver >> space >> ps->inheritVelocity
                       >> ps->noiseStrength >> ps->noiseFrequency >> ps->noiseScroll
                       >> ps->orbitSpeed >> ps->orbitAxis.x >> ps->orbitAxis.y >> ps->orbitAxis.z
                       >> ps->vortexStrength >> ps->vortexPull >> spdLife;
                    ps->simulationSpace = space == 1 ? ParticleSystem::SimulationSpace::Local
                                                     : ParticleSystem::SimulationSpace::World;
                    ps->speedOverLife = (spdLife != 0);
                    // Counts are clamped so a hand-edited file can't overrun the fixed arrays.
                    int n = 0;
                    if (rs >> n) {
                        ParticleGradient& g = ps->colorGradient;
                        g.count = 0;
                        n = n < 0 ? 0 : (n > ParticleGradient::kMaxStops ? ParticleGradient::kMaxStops : n);
                        for (int i = 0; i < n; ++i) {
                            ParticleGradient::Stop s;
                            if (!(rs >> s.time >> s.color.r >> s.color.g >> s.color.b >> s.color.a)) break;
                            g.stops[g.count++] = s;
                        }
                    }
                    for (ParticleCurve* c : {&ps->sizeCurve, &ps->speedCurve}) {
                        int sm = 0;
                        if (!(rs >> n >> sm)) break;
                        c->count = 0; c->smooth = (sm != 0);
                        n = n < 0 ? 0 : (n > ParticleCurve::kMaxKeys ? ParticleCurve::kMaxKeys : n);
                        for (int i = 0; i < n; ++i) {
                            ParticleCurve::Key k;
                            if (!(rs >> k.time >> k.value)) break;
                            c->keys[c->count++] = k;
                        }
                    }
                } else if (field == "sequenceplayer") {
                    // Cutscene player. Missing keys keep the defaults.
                    auto* sp = go->AddComponent<SequencePlayer>();
                    KvIn k(in);
                    k.str("path", sp->sequencePath);
                    k.b("auto", sp->playOnStart);
                    k.b("loop", sp->loop);
                    k.f("speed", sp->timeScale);
                    k.key("skip", sp->skipKey);
                    k.b("lock", sp->lockPlayer);
                } else if (field == "loc") {
                    // Localized text: string-table keys for the components read above.
                    // `i` absent means the first instance (every file written before per-instance keys).
                    KvIn k(in);
                    int idx = 0; k.i("i", idx);
                    if (auto* tr = AtComp(go->GetComponents<TextRenderer>(), idx)) k.str("text", tr->loc.key);
                    if (auto* b = AtComp(go->GetComponents<UIButton>(), idx)) k.str("button", b->loc.key);
                    if (auto* t = AtComp(go->GetComponents<UIToggle>(), idx)) k.str("toggle", t->loc.key);
                    if (auto* tt = AtComp(go->GetComponents<UITooltip>(), idx)) k.str("tooltip", tt->loc.key);
                    if (auto* w = AtComp(go->GetComponents<WorldUI>(), idx)) k.str("world", w->loc.key);
                    if (auto* inf = AtComp(go->GetComponents<UIInputField>(), idx)) k.str("input", inf->loc.key);
                    if (auto* hp = AtComp(go->GetComponents<HoldPrompt>(), idx)) k.b("hold", hp->localized);
                    if (auto* pm = AtComp(go->GetComponents<PauseMenu>(), idx)) k.b("pause", pm->localized);
                    if (auto* lsc = AtComp(go->GetComponents<LoadingScreen>(), idx)) k.b("loading", lsc->localized);
                } else if (field == "navsurface") {
                    auto* ns = go->AddComponent<NavMeshSurface>();
                    KvIn k(in);
                    k.f("radius", ns->settings.agentRadius); k.f("height", ns->settings.agentHeight);
                    k.f("slope", ns->settings.maxSlope); k.f("step", ns->settings.maxStep);
                    k.f("cell", ns->settings.cellSize); k.f("cellh", ns->settings.cellHeight);
                    k.str("path", ns->navPath); k.b("autob", ns->autoBounds);
                    k.f("bcx", ns->boundsCenter.x); k.f("bcy", ns->boundsCenter.y); k.f("bcz", ns->boundsCenter.z);
                    k.f("bsx", ns->boundsSize.x); k.f("bsy", ns->boundsSize.y); k.f("bsz", ns->boundsSize.z);
                    k.b("dyn", ns->includeDynamic); k.u("layers", ns->layerMask); k.b("bakeplay", ns->bakeBeforePlay);
                    for (int a = 0; a < kNavAreaCount; ++a) {
                        k.f(("ac" + std::to_string(a)).c_str(), ns->areaCost[a]);
                        k.str(("an" + std::to_string(a)).c_str(), ns->areaName[a]);
                    }
                    ns->sceneDir = g_loadingSceneDir;   // the .okaynav is looked for next to the scene too
                } else if (field == "navagent") {
                    auto* na = go->AddComponent<NavMeshAgent>();
                    KvIn k(in);
                    k.f("radius", na->radius); k.f("height", na->height); k.f("speed", na->speed);
                    k.f("accel", na->acceleration); k.f("angular", na->angularSpeed); k.f("stop", na->stoppingDistance);
                    k.b("repath", na->autoRepath); k.f("repathT", na->repathInterval); k.i("prio", na->avoidancePriority);
                    k.f("avoid", na->avoidanceRange); k.b("brake", na->autoBraking); k.b("face", na->faceMovement);
                    k.b("anim", na->driveAnimation); k.f("base", na->baseOffset); k.u("mask", na->areaMask);
                } else if (field == "navmod") {
                    auto* nm = go->AddComponent<NavMeshModifier>();
                    KvIn k(in);
                    k.i("area", nm->area); k.b("kids", nm->applyToChildren);
                    nm->area = std::clamp(nm->area, 0, kNavAreaCount - 1);
                } else if (field == "navlink") {
                    auto* nl = go->AddComponent<NavMeshLink>();
                    KvIn k(in);
                    int ty = 0; k.i("type", ty); nl->type = (NavMeshLink::Type)std::clamp(ty, 0, 2);
                    k.f("sx", nl->startPoint.x); k.f("sy", nl->startPoint.y); k.f("sz", nl->startPoint.z);
                    k.f("ex", nl->endPoint.x); k.f("ey", nl->endPoint.y); k.f("ez", nl->endPoint.z);
                    k.b("both", nl->bidirectional); k.f("cost", nl->cost); k.i("area", nl->area);
                    k.f("speed", nl->speed); k.f("arc", nl->arcHeight);
                    nl->area = std::clamp(nl->area, 0, kNavAreaCount - 1);
                } else if (field == "navobstacle") {
                    auto* no = go->AddComponent<NavMeshObstacle>();
                    KvIn k(in);
                    k.f("sx", no->size.x); k.f("sy", no->size.y); k.f("sz", no->size.z);
                    k.f("cx", no->center.x); k.f("cy", no->center.y); k.f("cz", no->center.z);
                    int sh = 0; k.i("shape", sh); no->shape = (NavMeshObstacle::Shape)std::clamp(sh, 0, 1);
                    k.f("cr", no->capsuleRadius); k.f("ch", no->capsuleHeight);
                    k.b("carve", no->carve); k.b("still", no->carveOnlyWhenStationary);
                    k.f("tts", no->timeToStationary); k.f("move", no->moveThreshold);
                } else if (field == "storystate") {
                    // Dialogue + quests: the story's global variables and flags.
                    auto* st = go->AddComponent<StoryState>();
                    KvIn k(in);
                    int n = 0, f = 0;
                    k.i("n", n); k.i("f", f);
                    for (int i = 0; i < n && i < 100000; ++i) {
                        const std::string p = std::to_string(i);
                        std::string key, type = "n", val;
                        k.str(("k" + p).c_str(), key); k.str(("t" + p).c_str(), type); k.str(("v" + p).c_str(), val);
                        if (key.empty()) continue;
                        if (type == "b") st->vars[key] = vs::VsValue(val == "true" || val == "1");
                        else if (type == "s") st->vars[key] = vs::VsValue(val);
                        else st->vars[key] = vs::VsValue(std::strtof(val.c_str(), nullptr));
                    }
                    for (int i = 0; i < f && i < 100000; ++i) {
                        std::string flag;
                        k.str(("f" + std::to_string(i)).c_str(), flag);
                        if (!flag.empty()) st->flags.insert(flag);
                    }
                } else if (field == "dialoguerunner") {
                    auto* dr = go->AddComponent<DialogueRunner>();
                    KvIn k(in);
                    k.str("file", dr->graphFile); k.i("mode", dr->startMode); k.key("key", dr->interactKey); k.f("range", dr->interactRange);
                    k.str("prompt", dr->promptText); k.str("pobj", dr->promptObject); k.b("pause", dr->pausePlayer);
                    k.f("type", dr->typewriterSpeed); k.f("auto", dr->autoAdvance); k.b("ui", dr->drawUI); k.b("cancel", dr->canCancel);
                    k.i("start", dr->startNode); k.str("player", dr->playerName);
                    int shots = 0;
                    k.i("shots", shots);   // missing in records written before dialogue camera shots: none
                    for (int i = 0; i < shots && i < 100000; ++i) {
                        const std::string p = "sh" + std::to_string(i);
                        DialogueRunner::CameraShotEntry e;
                        k.i((p + "n").c_str(), e.node); k.str((p + "c").c_str(), e.camera);
                        if (!e.camera.empty()) dr->cameraShots.push_back(e);
                    }
                    k.f("shb", dr->shotBlend);
                } else if (field == "questlog") {
                    auto* ql = go->AddComponent<QuestLog>();
                    KvIn k(in);
                    k.str("file", ql->questFile); k.b("tracker", ql->showTracker); k.b("journal", ql->allowJournal); k.key("jkey", ql->journalKey);
                    k.b("notify", ql->showNotifications); k.b("ui", ql->drawUI); k.str("tracked", ql->tracked); k.str("player", ql->playerName);
                    int n = 0;
                    k.i("n", n);
                    for (int i = 0; i < n && i < 100000; ++i) {
                        const std::string p = "q" + std::to_string(i);
                        QuestRecord r;
                        int st = 0, no = 0;
                        k.str((p + "id").c_str(), r.id); k.i((p + "st").c_str(), st); k.i((p + "c").c_str(), r.completions); k.i((p + "n").c_str(), no);
                        if (r.id.empty()) continue;
                        r.status = (QuestStatus)std::max(0, std::min(st, 3));
                        for (int o = 0; o < no && o < 1000; ++o) {
                            int prog = 0, done = 0;
                            k.i((p + "p" + std::to_string(o)).c_str(), prog); k.i((p + "d" + std::to_string(o)).c_str(), done);
                            r.progress.push_back(prog); r.done.push_back(done ? 1 : 0);
                        }
                        ql->records.push_back(r);
                    }
                } else { fieldDone = false; }
                }
                if (!fieldDone) { fieldDone = true;
                if (field == "questlogdef") {
                    // One inline quest of the Quest Log read just before it.
                    KvIn k(in);
                    auto* ql = LastOf(go->GetComponents<QuestLog>());
                    if (ql) {
                        QuestDef q;
                        std::string pre;
                        int nobj = 0, nri = 0, nrv = 0;
                        k.str("id", q.id); k.str("title", q.title); k.str("desc", q.description); k.b("rep", q.repeatable); k.str("pre", pre);
                        {
                            std::string cur;
                            for (char c : pre + ",") { if (c == ',') { if (!cur.empty()) q.prerequisites.push_back(cur); cur.clear(); } else if (c != ' ') cur += c; }
                        }
                        k.i("nobj", nobj); k.i("nri", nri); k.i("nrv", nrv);
                        for (int i = 0; i < nobj && i < 1000; ++i) {
                            const std::string p = "o" + std::to_string(i);
                            QuestObjective ob;
                            std::string kind;
                            k.str((p + "k").c_str(), kind); k.str((p + "d").c_str(), ob.description); k.str((p + "t").c_str(), ob.target);
                            k.i((p + "n").c_str(), ob.count); k.b((p + "opt").c_str(), ob.optional);
                            QuestObjective::KindFromName(kind, ob.kind);
                            q.objectives.push_back(ob);
                        }
                        for (int i = 0; i < nri && i < 1000; ++i) {
                            std::string item; int cnt = 1;
                            k.str(("r" + std::to_string(i) + "i").c_str(), item); k.i(("r" + std::to_string(i) + "n").c_str(), cnt);
                            if (!item.empty()) q.reward.items.push_back({item, cnt});
                        }
                        for (int i = 0; i < nrv && i < 1000; ++i) {
                            std::string key, val;
                            k.str(("v" + std::to_string(i) + "k").c_str(), key); k.str(("v" + std::to_string(i) + "v").c_str(), val);
                            if (!key.empty()) q.reward.vars.push_back({key, val});
                        }
                        k.str("revent", q.reward.event);
                        ql->quests.push_back(q);
                    }
                } else if (field == "questtrigger") {
                    auto* qt = go->AddComponent<QuestTrigger>();
                    KvIn k(in);
                    k.str("zone", qt->zone); k.str("tag", qt->triggerTag); k.b("once", qt->once); k.str("quest", qt->questId); k.i("obj", qt->objective);
                } else if (field == "btrunner") {
                    auto* bt = go->AddComponent<BehaviorTreeRunner>();
                    KvIn k(in);
                    k.str("tree", bt->treePath); k.f("rate", bt->tickRate); k.b("auto", bt->runOnStart); k.b("debug", bt->debug);
                    k.b("homeset", bt->homeOverride); k.f("hx", bt->homePoint.x); k.f("hy", bt->homePoint.y); k.f("hz", bt->homePoint.z);
                    int n = 0; k.i("n", n);
                    for (int i = 0; i < n && i < 256; ++i) {
                        const std::string kk = "k" + std::to_string(i), vk = "v" + std::to_string(i);
                        std::string key, val;
                        k.str(kk.c_str(), key); k.str(vk.c_str(), val);
                        if (!key.empty()) bt->overrides.emplace_back(key, val);
                    }
                } else if (field == "spline") {
                    auto* sp = go->AddComponent<Spline>();
                    KvIn k(in);
                    int type = 0, up = 0;
                    k.i("type", type); k.i("up", up);
                    sp->type = type == 1 ? Spline::Type::CatmullRom : Spline::Type::Bezier;
                    sp->upMode = up == 1 ? Spline::UpMode::World : Spline::UpMode::Minimal;
                    k.b("closed", sp->closed); k.i("res", sp->resolution);
                } else if (field == "splinept") {
                    auto* sp = LastOf(go->GetComponents<Spline>());
                    if (!sp) sp = go->AddComponent<Spline>();
                    KvIn k(in);
                    SplinePoint p;
                    k.f("x", p.position.x); k.f("y", p.position.y); k.f("z", p.position.z);
                    k.f("ix", p.tangentIn.x); k.f("iy", p.tangentIn.y); k.f("iz", p.tangentIn.z);
                    k.f("ox", p.tangentOut.x); k.f("oy", p.tangentOut.y); k.f("oz", p.tangentOut.z);
                    int mode = 0; k.i("mode", mode);
                    p.mode = (SplinePoint::Mode)std::clamp(mode, 0, 3);
                    k.f("roll", p.roll); k.f("sx", p.scale.x); k.f("sy", p.scale.y);
                    sp->points.push_back(p);
                } else if (field == "splinefollower") {
                    auto* sf = go->AddComponent<SplineFollower>();
                    KvIn k(in);
                    int drive = 0, wrap = 0;
                    k.str("spline", sf->spline); k.i("drive", drive); k.i("wrap", wrap);
                    sf->drive = drive == 1 ? SplineFollower::Drive::Time : SplineFollower::Drive::Speed;
                    sf->wrap = (SplineFollower::Wrap)std::clamp(wrap, 0, 2);
                    k.f("speed", sf->speed); k.f("dur", sf->duration); k.f("start", sf->startAt);
                    k.b("auto", sf->playOnStart); k.b("orient", sf->orient);
                    k.f("offx", sf->offset.x); k.f("offy", sf->offset.y); k.f("offz", sf->offset.z);
                } else if (field == "splinemesh") {
                    auto* sm = go->AddComponent<SplineMesh>();
                    KvIn k(in);
                    int mode = 0, profile = 0, axis = 0;
                    k.str("spline", sm->spline); k.i("mode", mode); k.i("profile", profile);
                    sm->mode = mode == 1 ? SplineMesh::Mode::Repeat : SplineMesh::Mode::Extrude;
                    sm->profile = (SplineMesh::Profile)std::clamp(profile, 0, 4);
                    k.f("width", sm->width); k.f("height", sm->height); k.i("sides", sm->sides);
                    const std::vector<float> cp = ReadNumberList(k, "custom");
                    sm->customProfile.clear();
                    for (std::size_t i = 0; i + 1 < cp.size(); i += 2) sm->customProfile.push_back({cp[i], cp[i + 1]});
                    k.b("cclosed", sm->closedProfile); k.b("caps", sm->capEnds); k.f("seg", sm->segmentLength); k.f("uv", sm->uvTileLength);
                    k.str("src", sm->sourceMesh); k.i("axis", axis);
                    sm->meshAxis = axis == 2 ? SplineMesh::Axis::Z : SplineMesh::Axis::X;
                    k.f("spacing", sm->spacing);
                    k.f("msx", sm->meshScale.x); k.f("msy", sm->meshScale.y); k.f("msz", sm->meshScale.z);
                    k.b("fit", sm->stretchToFit); k.b("collider", sm->generateCollider);
                } else if (field == "splineinstancer") {
                    auto* si = go->AddComponent<SplineInstancer>();
                    KvIn k(in);
                    int align = 0;
                    k.str("spline", si->spline); k.str("tmpl", si->templateName); k.str("prefab", si->prefabPath);
                    k.str("mesh", si->meshName); k.f("spacing", si->spacing); k.f("start", si->startOffset);
                    k.f("side", si->sideOffset); k.b("both", si->bothSides); k.f("jitter", si->jitter);
                    k.f("rotj", si->rotationJitter); k.f("smin", si->scaleMin); k.f("smax", si->scaleMax);
                    k.u("seed", si->seed); k.i("align", align);
                    si->align = align == 1 ? SplineInstancer::Align::Curve : SplineInstancer::Align::Upright;
                    k.f("rx", si->rotationOffset.x); k.f("ry", si->rotationOffset.y); k.f("rz", si->rotationOffset.z);
                    k.i("max", si->maxCount);
                } else if (field == "linerenderer") {
                    auto* lr = go->AddComponent<LineRenderer>();
                    KvIn k(in);
                    const std::vector<float> pts = ReadNumberList(k, "pts");
                    for (std::size_t i = 0; i + 2 < pts.size(); i += 3) lr->positions.push_back({pts[i], pts[i + 1], pts[i + 2]});
                    int align = 0, texmode = 0;
                    k.b("world", lr->useWorldSpace); k.b("loop", lr->loop); k.f("width", lr->width); k.col("c", lr->color);
                    k.i("corner", lr->cornerVertices); k.i("cap", lr->capVertices); k.i("align", align); k.i("texmode", texmode);
                    lr->alignment = align == 1 ? LineAlignment::Flat : LineAlignment::View;
                    lr->textureMode = texmode == 1 ? LineTextureMode::Tile : LineTextureMode::Stretch;
                    k.f("tile", lr->tileLength); k.str("tex", lr->texture); k.str("spline", lr->spline);
                    k.i("samples", lr->splineSamples);
                    ReadCurveKv(k, "w", lr->widthCurve);
                    ReadGradientKv(k, "g", lr->colorGradient);
                } else if (field == "guid") {
                    const std::string g = ReadQuoted(in);
                    // A full load adopts the saved id (so references survive); an additive
                    // load (Instantiate, paste, merge) keeps the new object's own fresh one.
                    if (clear) {
                        if (seenGuids.count(g)) warn("duplicate guid on object " + std::to_string(idx) + "; gave it a new one");
                        else if (go->SetGuid(g)) seenGuids.insert(g);
                        else warn("ignored malformed guid on object " + std::to_string(idx));
                    }
                } else if (field == "sibling") {
                    int pos = -1; in >> pos;
                    if (pos >= 0) siblingHints[idx] = pos;
                } else if (field == "compoff") {
                    std::string key = ReadQuoted(in); int occ = 0; in >> occ;
                    if (key == "ActiveRagdoll") key = "Ragdoll";   // (one component since 2026-11-25)
                    compOff.push_back({key, occ});
                } else if (field == "comporder") {
                    std::string rest; std::getline(in, rest);
                    std::istringstream ls(rest);
                    while (ls >> std::ws && ls.peek() == '"') {
                        std::string key = ReadQuoted(ls);
                        if (key == "ActiveRagdoll") key = "Ragdoll";   // (one component since 2026-11-25)
                        compOrder.push_back(key);
                    }
                } else if (field == "override") {
                    GameObject::PrefabOverride ov; in >> ov.object; ov.line = ReadQuoted(in);
                    if (in && ov.object >= 0 && !ov.line.empty()) go->prefabOverrides.push_back(ov);
                } else if (auto reg = ComponentSerializerRegistry::Find(field); reg && reg->readWithNotes) {
                    // A component that registered itself and reports what it skipped (the generic record).
                    std::istringstream ls(ReadRecordLine(in));
                    std::vector<std::string> notes;
                    reg->readWithNotes(*go, ls, notes);
                    for (const auto& n : notes) warn(n + " on object " + std::to_string(idx));
                } else if (reg) {
                    // A component that registered itself (no branch in this chain).
                    std::string rest; std::getline(in, rest);
                    std::istringstream ls(rest);
                    reg->read(*go, ls);
                } else {
                    // A newer build's field: one record per line, so step over it. Its text is kept on the object and
                    // written back by the next save, so an older build does not drop what a newer one stored.
                    warn("skipped unknown field '" + field + "' on object " + std::to_string(idx));
                    keepRecord();
                }
                }
            }
            // A read that ran off the end (or choked) leaves the stream failed, so
            // the loop above stopped without finding 'end'.
            if (!sawEnd)
                return bad("object " + std::to_string(idx) + " is cut short (no 'end')");
            // Per-component enabled flags, applied last so they win over anything a
            // record set while building its component. A flag for a component that is
            // not there (an older or newer build's) is ignored.
            for (const auto& [key, occ] : compOff) {
                int seen = 0;
                bool found = false;
                for (const auto& c : go->Components())
                    if (c.get() != static_cast<Component*>(go->transform) && ComponentSerializerRegistry::TypeKey(*c) == key && seen++ == occ) {
                        c->enabled = false;
                        found = true;
                        break;
                    }
                // The flag may belong to a record that was kept unread above (its module is off): keep it with them,
                // written as WriteDisabledComponents writes it, so the component comes back switched off.
                if (!found && !go->unknownRecords.empty() && go->unknownRecords.size() < kMaxUnknownRecords)
                    go->unknownRecords.push_back("compoff " + Quote(key) + " " + std::to_string(occ));
            }
            if (!compOrder.empty()) ApplyComponentOrder(*go, compOrder);   // after the flags: they were counted in the writer's order
            // Every saved type keeps every instance (AllowsMultiple). A type that opts
            // out still loads both records and reports that a resave would keep only the first.
            {
                std::unordered_map<std::string, int> counts;
                for (const auto& c : go->Components()) {
                    if (c.get() == static_cast<Component*>(go->transform)) continue;
                    const std::string k = ComponentSerializerRegistry::TypeKey(*c);
                    if (++counts[k] == 2 && !AllowsMultiple(c.get()))
                        warn("object " + std::to_string(idx) + " has two '" + k + "' components; only the first is saved");
                }
            }
        } else {
            // A newer build's top-level record (see the field case above): kept by a full load, and only when it
            // looks like a record name (a stray word in a damaged file is not worth carrying along).
            warn("skipped unknown record '" + token + "'");
            const std::string rest = ReadRecordLine(in);
            bool name = !token.empty() && token.size() < 64;
            for (char ch : token) if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_')) name = false;
            if (clear && name && scene.unknownRecords.size() < kMaxUnknownRecords && token.size() + rest.size() <= kMaxUnknownRecordSize)
                scene.unknownRecords.push_back(token + rest);
        }
        if (in.fail()) return bad("malformed or truncated '" + token + "' record");
        if (token == "gameobject" && maxObjects > 0 && ++madeObjects >= maxObjects) {
            if (finished) *finished = false;   // paused between objects
            return true;
        }
    }

    // Resolve parent links now that every object exists. A link that would make
    // the hierarchy a loop (an object under itself) is refused, and so is the file.
    for (auto& [childIdx, parentIdx] : parentLinks) {
        auto c = byIndex.find(childIdx);
        auto p = byIndex.find(parentIdx);
        if (c != byIndex.end() && p != byIndex.end() &&
            !c->second->transform->SetParent(p->second->transform, /*worldPositionStays=*/false)) {
            if (error) *error = "object " + std::to_string(childIdx) + " has a parent cycle (parent " +
                                std::to_string(parentIdx) + ")";
            return false;
        }
    }
    // A joint that named its partner by number inside this text now links the object made from that number.
    for (const ParseSession::JointAt& link : S.jointLinks) {
        auto partner = byIndex.find(link.partnerIndex);
        const std::vector<Joint3D*> joints = link.owner->GetComponents<Joint3D>();
        if (partner != byIndex.end() && link.jointOrdinal < (int)joints.size() && joints[(std::size_t)link.jointOrdinal])
            joints[(std::size_t)link.jointOrdinal]->ConnectTo(partner->second);
    }
    S.jointLinks.clear();
    // Restore saved sibling order. Parent links append in file order, which is not the
    // order siblings had if they were reordered after creation.
    if (!siblingHints.empty()) {
        std::unordered_set<Transform*> parents;
        for (auto& [childIdx, pos] : siblingHints) {
            (void)pos;
            auto c = byIndex.find(childIdx);
            if (c != byIndex.end() && c->second->transform->Parent()) parents.insert(c->second->transform->Parent());
        }
        std::unordered_map<Transform*, int> hint;
        for (auto& [childIdx, pos] : siblingHints) {
            auto c = byIndex.find(childIdx);
            if (c != byIndex.end()) hint[c->second->transform] = pos;
        }
        for (Transform* par : parents) {
            const std::vector<Transform*> cur = par->Children();
            const std::size_t n = cur.size();
            std::vector<Transform*> target(n, nullptr);
            std::vector<Transform*> hinted;
            for (Transform* ch : cur) if (hint.count(ch)) hinted.push_back(ch);
            std::stable_sort(hinted.begin(), hinted.end(), [&](Transform* a, Transform* b) { return hint[a] < hint[b]; });
            for (Transform* ch : hinted) { std::size_t at = (std::size_t)hint[ch]; if (at < n && !target[at]) target[at] = ch; }
            std::size_t fill = 0;
            std::unordered_set<Transform*> placedSet;
            for (Transform* tc : target) if (tc) placedSet.insert(tc);
            for (Transform* ch : cur) {
                if (placedSet.count(ch)) continue;
                while (fill < n && target[fill]) ++fill;
                if (fill < n) target[fill++] = ch;
            }
            for (std::size_t i = 0; i < n; ++i)
                if (par->Children()[i] != target[i]) {
                    if (i == 0) par->MoveChildToEdge(target[0], true);
                    else par->ReorderChild(target[i], target[i - 1], true);
                }
        }
    }
    // Files from before version 2 gave a Character body its built-in animation
    // itself: give it the Animator that plays it now (its state clips carried over).
    if (version < 2)
        for (auto& [idx, go] : byIndex) { (void)idx; HumanoidLibrary::EnsureAnimator(go); }
    // Version 3 starters put both view controllers on a switchable player. A
    // whole scene only: a clone made at play keeps the layout it was given.
    // In file order, so the first player in the file is the one a loose
    // camera goes to.
    if (version < 3 && clear) {
        std::vector<int> order;
        order.reserve(byIndex.size());
        for (auto& [idx, go] : byIndex) { (void)go; order.push_back(idx); }
        std::sort(order.begin(), order.end());
        for (int idx : order) CompleteViewToggle(scene, byIndex[idx]);
    }
    // Bare first / third person controllers and view toggles (every scene saved
    // before the Player Controller) move under one. Whole scenes and clones alike.
    for (auto& [idx, go] : byIndex) { (void)idx; PlayerController::Adopt(go); }
    // Default graphs saved before a built-in action existed gain that action once.
    for (auto& [idx, go] : byIndex) {
        (void)idx;
        Character* body = go->GetComponent<Character>();
        if (!body) continue;
        for (AnimStateMachine* sm : go->GetComponents<AnimStateMachine>())
            if (sm && sm->layer == 0) HumanoidActions::MergeMissing(*sm, body, false);
    }
    // Make each loaded UIFrame the source of truth for its widget's fields now, so
    // a script that writes those fields in its Start moves the frame.
    for (auto& [idx, go] : byIndex) { (void)idx; if (go->GetComponent<UIFrame>()) GetUIRect(go); }
    if (warnings) warnings->insert(warnings->end(), S.warnings.begin(), S.warnings.end());
    return true;
}

} // namespace sceneser
} // namespace okay
