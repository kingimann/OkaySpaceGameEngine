#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/PrefabTools.hpp"
#include "okay/Scene/SceneText.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <iterator>
#include "okay/Core/TolerantNumbers.hpp"
#include <cerrno>
#include <iostream>
#include <charconv>
#include <limits>
#include <locale>
#include <mutex>
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
#include "okay/Components/VehicleController.hpp"
#include "okay/Components/VehicleController2D.hpp"
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
#include "okay/Components/SurvivalStats.hpp"
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
#include "okay/Components/Crosshair.hpp"
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

// The folder of the scene file loaded last: restoring a scene from text (Stop after Play, Undo) finds mesh files next to it again.
static thread_local std::string g_lastSceneDir;


namespace detail {
std::uint64_t ComponentRulesGeneration();        // GameObject.cpp
std::uint64_t ComponentSerializerGeneration();   // ComponentSerializer.cpp
} // namespace detail

// Defined further down; the component-order check (WriteComponents) loads one object's records into a scratch scene.
static bool ParseInto(Scene& scene, const std::string& text, bool clear, GameObject** firstNew, std::string* error,
                      std::vector<std::string>* warnings);

using namespace sceneser;   // the shared record helpers (SceneSerializerInternal.hpp)

namespace {


// The first-person arm VIEWMODEL is spawned at runtime by FirstPersonHand (a "FP_Arm"
// child of the camera). Like the rig, it's regenerated each run, so it must not be
// written to the scene file — otherwise it would reload as a stale duplicate.
static bool IsGeneratedViewmodel(GameObject* go) {
    if (!go) return false;
    // The owner's arm copy is spawned beside the rig and rebuilt every run.
    if (go->name == "FP_OwnerArm" || go->name == "FP_OwnerFore" || go->name == "FP_OwnerHand"
        || go->name == "FP_OwnerArm2" || go->name == "FP_OwnerFore2" || go->name == "FP_OwnerHand2"
        || go->name == "FP_OwnerItem")
        return true;
    if (go->name != "FP_Arm" || !go->transform) return false;
    Transform* par = go->transform->Parent();
    return par && par->gameObject && par->gameObject->GetComponent<FirstPersonHand>();
}

// What keeps an object (and everything under it) out of a saved file, one bit per reason:
//  - runtime-only (`transient`);
//  - a Character's "Rig" (the separated part objects), a GENERATED view of the Character like
//    Terrain's mesh: rebuilt on load (BuildParts), which keeps part colours right and avoids
//    both file bloat and duplicate rigs;
//  - build mode's own runtime objects (placement ghost, link wires, hotbar), recreated each run.
// Each applies to the object itself and all its descendants, so a whole file is worked out with one
// pass that hands the parent's bits down (a walk up per object would be quadratic in the depth).
constexpr unsigned kSkipTransient = 1, kSkipRig = 2, kSkipBuildRuntime = 4;
static unsigned OwnSkipBits(const GameObject* g) {
    unsigned f = 0;
    if (g->transient) f |= kSkipTransient;
    if (g->name == "Rig" && g->transform) {
        const Transform* par = g->transform->Parent();
        if (par && par->gameObject && par->gameObject->GetComponent<Character>()) f |= kSkipRig;
    }
    if (g->tag == BuildMode::kRuntimeTag) f |= kSkipBuildRuntime;
    return f;
}



// `comporder "Type" "Type" ...`: the stored order of an object's components after the Transform.
void WriteComponentOrderLine(std::ostream& out, const GameObject* go) {
    std::string line;
    int n = 0;
    for (const auto& c : go->Components()) {
        if (c.get() == static_cast<const Component*>(go->transform)) continue;
        line += " " + Quote(ComponentSerializerRegistry::TypeKey(*c));
        ++n;
    }
    if (n >= 2) out << "  comporder" << line << "\n";
}

// The type names of an object's components after the Transform, in stored order.
std::vector<std::string> ComponentKeys(const GameObject* go) {
    std::vector<std::string> keys;
    for (const auto& c : go->Components()) {
        if (c.get() == static_cast<const Component*>(go->transform)) continue;
        keys.push_back(ComponentSerializerRegistry::TypeKey(*c));
    }
    return keys;
}

// Do two component lists agree on the order of the components they share? The Nth component of a type is
// the same component in both; one that only one list has (the loader added a required one, or the writer
// saved only the first of two) is ignored.
bool SameSharedOrder(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    auto number = [](const std::vector<std::string>& v) {
        std::unordered_map<std::string, int> seen;
        std::vector<std::string> ids;
        ids.reserve(v.size());
        for (const std::string& k : v) ids.push_back(k + "#" + std::to_string(seen[k]++));
        return ids;
    };
    const std::vector<std::string> ia = number(a), ib = number(b);
    const std::unordered_set<std::string> sa(ia.begin(), ia.end()), sb(ib.begin(), ib.end());
    std::vector<const std::string*> ca, cb;
    for (const std::string& id : ia) if (sb.count(id)) ca.push_back(&id);
    for (const std::string& id : ib) if (sa.count(id)) cb.push_back(&id);
    if (ca.size() != cb.size()) return false;
    for (std::size_t i = 0; i < ca.size(); ++i) if (*ca[i] != *cb[i]) return false;
    return true;
}

// Would a file made of these component records give the object its components in the order it has now?
// The loader adds a component when it meets its record, so the answer is what loading the records produces:
// found by loading them once into a scratch scene. It depends on which components there are and which records
// they wrote, not on their values, so the answer is kept per (type names, record names) and the next object of
// the same shape costs a lookup and no load. An answer that cannot be found (the text does not load) counts as
// "yes": nothing is written then, as before.
bool LoadsInStoredOrder(const GameObject* go, const std::string& records) {
    const std::vector<std::string> live = ComponentKeys(go);
    std::string key;
    for (const std::string& k : live) { key += k; key += ','; }
    key += '|';
    for (std::size_t at = 0; at < records.size();) {   // the first word of each record line
        std::size_t nl = records.find('\n', at);
        if (nl == std::string::npos) nl = records.size();
        std::size_t b = at;
        while (b < nl && records[b] == ' ') ++b;
        std::size_t e = b;
        while (e < nl && records[e] != ' ') ++e;
        key.append(records, b, e - b);
        key += ';';
        at = nl + 1;
    }
    // The answer also depends on the RequireComponent rules and on which serializers are registered: the memo is dropped when they change.
    const std::uint64_t generation = detail::ComponentRulesGeneration() * 1000003u + detail::ComponentSerializerGeneration();
    struct Memo { std::mutex mutex; std::unordered_map<std::string, bool> verdict; std::uint64_t generation = 0; };   // lint-allow: win32-thread (existing code, unchanged; the diff shows it as added after the split)
    static Memo memo;
    {
        std::lock_guard<std::mutex> lock(memo.mutex);   // lint-allow: win32-thread
        if (memo.generation != generation) { memo.verdict.clear(); memo.generation = generation; }
        auto it = memo.verdict.find(key);
        if (it != memo.verdict.end()) return it->second;
    }
    static thread_local bool probing = false;   // a load that saves (it should not) never starts another probe
    if (probing) return true;
    probing = true;
    bool natural = true;
    {
        Scene probe("component order");
        const std::string text = "okayscene 3\ngameobject 0 \"O\"\n  active 1\n  parent -1\n" + records + "end\n";
        GameObject* root = nullptr;
        std::string error;
        std::vector<std::string> warnings;   // kept here so nothing is printed
        if (ParseInto(probe, text, /*clear=*/false, &root, &error, &warnings) && root)
            natural = SameSharedOrder(live, ComponentKeys(root));
    }
    probing = false;
    std::lock_guard<std::mutex> lock(memo.mutex);   // lint-allow: win32-thread
    if (memo.generation == generation) {   // not if the rules changed while this was being worked out
        if (memo.verdict.size() > 4096) memo.verdict.clear();   // shapes are few; this only bounds a pathological run
        memo.verdict[key] = natural;
    }
    return natural;
}



// The components of an object come back in the order their records are written (see WriteComponentRecords), so an
// object whose components are stored in another order (added in another order, or arranged with MoveComponent /
// ReorderComponents) also writes a `comporder` record that puts them back. An object that already loads in its own
// order writes no extra line, so a scene made of such objects is saved exactly as before; so is an object with one
// component or none. An object that was arranged on purpose (HasCustomComponentOrder) always writes the record.
void WriteComponentsOf(std::ostream& out, GameObject* go) {
    std::size_t others = 0;
    for (const auto& c : go->Components()) if (c.get() != static_cast<const Component*>(go->transform)) ++others;
    if (others < 2) { WriteComponentRecords(out, go); return; }
    if (go->HasCustomComponentOrder()) { WriteComponentRecords(out, go); WriteComponentOrderLine(out, go); return; }
    std::ostringstream records;   // this object's records only: they are read back to see what order they load in
    records.copyfmt(out);
    WriteComponentRecords(records, go);
    const std::string text = records.str();
    out << text;
    if (!LoadsInStoredOrder(go, text)) WriteComponentOrderLine(out, go);
}
// The components, then the records this build did not understand when it read the object, as they were.
void WriteComponents(std::ostream& out, GameObject* go) {
    WriteComponentsOf(out, go);
    for (const std::string& r : go->unknownRecords) out << "  " << r << "\n";
}
} // namespace

namespace {
// While a scene or object is written, whoever sits in a vehicle gets their own controller, colliders
// and gravity back, so a copy, undo snapshot or save never holds a frozen player.
struct VehicleSeatSaveGuard {
    VehicleSeatSaveGuard() { VehicleSeat::SuspendAllForSave(); }
    ~VehicleSeatSaveGuard() { VehicleSeat::ResumeAllAfterSave(); }
};
} // namespace

std::string SceneSerializer::Serialize(const Scene& scene) {
    VehicleSeatSaveGuard seatGuard;
    std::ostringstream out;
    UseRoundTripFloats(out);
    out << "okayscene 3\n";
    out << "name " << Quote(scene.Name()) << "\n";
    if (!scene.uiFont.empty()) out << "uifont " << Quote(scene.uiFont) << "\n";
    if (scene.uiQuality != 1) out << "uiquality " << scene.uiQuality << "\n";
    for (const std::string& r : scene.unknownRecords) out << r << "\n";   // what a newer build wrote at the top, as it was
    out << "gravity " << scene.physics().gravity.x << " " << scene.physics().gravity.y << "\n";
    if (scene.HasCustomLayerCollisions()) {   // optional: older scenes have none (everything collides)
        out << "layermatrix";
        for (int i = 0; i < 32; ++i) out << " " << scene.LayerCollisionMask(i);
        out << "\n";
    }
    {   // 3D gravity (a record older builds skip as unknown; a file without it loads the default)
        const Vec3& g3 = scene.physics3D().gravity;
        out << "gravity3 " << g3.x << " " << g3.y << " " << g3.z << "\n";
    }
    for (const modeling::RefImageRecord& r : scene.refImages)   // reference pictures (a record older builds skip as unknown)
        out << "refimage " << Quote(r.path) << " " << r.plane << " " << r.centre.x << " " << r.centre.y << " " << r.centre.z << " "
            << r.width << " " << r.opacity << " " << (r.flipX ? 1 : 0) << " " << (r.visible ? 1 : 0) << "\n";
    // (written whenever it holds anything but the defaults; the quality value is kept as it was read: old files round-trip)
    if (scene.physics3D().quality != PhysicsQuality::Classic || scene.physics().quality != PhysicsQuality::Classic ||
        scene.physics3D().solverIterations != 10 || !scene.physics3D().autoContinuous || scene.physics().solverIterations != 10) {
        // Physics quality (Improved = the iterated contact solver). Written only when it is not Classic, so a Classic scene
        // saves the same file as before; a file without the record loads as Classic. Older builds skip it as unknown.
        out << "physicsquality " << (int)scene.physics3D().quality << " " << scene.physics3D().solverIterations
            << " " << (scene.physics3D().autoContinuous ? 1 : 0) << " " << (int)scene.physics().quality
            << " " << scene.physics().solverIterations << "\n";
    }
    {
        const auto& rs = scene.renderSettings;
        out << "rendersettings " << (rs.skybox ? 1 : 0) << " "
            << rs.skyTop.r << " " << rs.skyTop.g << " " << rs.skyTop.b << " "
            << rs.skyHorizon.r << " " << rs.skyHorizon.g << " " << rs.skyHorizon.b << " "
            << rs.skyBottom.r << " " << rs.skyBottom.g << " " << rs.skyBottom.b << " "
            << rs.ambient << "\n";
        out << "fog " << (rs.fog ? 1 : 0) << " "
            << rs.fogColor.r << " " << rs.fogColor.g << " " << rs.fogColor.b << " "
            << rs.fogStart << " " << rs.fogEnd << "\n";
        if (rs.vignette > 0.0f) out << "vignette " << rs.vignette << "\n";
        // Always write the value (not just when true): the default is ON, so a
        // scene that turned filmic OFF must record that or it re-enables on load.
        out << "tonemap " << (rs.tonemap ? 1 : 0) << "\n";
        // Skybox extras (horizon position + optional sun disc). Written when non-default.
        if (rs.skyHorizonPos != 0.5f || rs.skySun)
            out << "sky " << rs.skyHorizonPos << " " << (rs.skySun ? 1 : 0) << " "
                << rs.skySunX << " " << rs.skySunY << " " << rs.skySunSize << " "
                << rs.skySunColor.r << " " << rs.skySunColor.g << " " << rs.skySunColor.b << "\n";
        // Star field (separate append-only record so older engines still load the scene). Also written with the
        // overlay off when the density or brightness changed: the Procedural sky draws its night stars with them.
        const Scene::RenderSettings starDefaults;
        if (rs.skyStars || rs.skyStarDensity != starDefaults.skyStarDensity || rs.skyStarBright != starDefaults.skyStarBright)
            out << "skystars " << (rs.skyStars ? 1 : 0) << " " << rs.skyStarDensity << " " << rs.skyStarBright << "\n";
        if (rs.ambientMode != Scene::RenderSettings::AmbientHemisphere || rs.ambientSkyTint != 0.7f)
            out << "ambientmode " << rs.ambientMode << " " << rs.ambientSkyTint << "\n";
        if (!rs.autoBrightness) out << "lightunits 0\n";
        if (rs.probes.Baked() || !rs.probes.enabled) {   // baked light probes (append-only record; older builds keep it as unknown)
            out << "probegrid " << (rs.probes.enabled ? 1 : 0) << " " << rs.probes.spacing << " " << rs.probes.origin.x << " " << rs.probes.origin.y
                << " " << rs.probes.origin.z << " " << rs.probes.nx << " " << rs.probes.ny << " " << rs.probes.nz << " " << rs.probes.sh.size();
            for (float v : rs.probes.sh) out << " " << v;
            out << "\n";
        }
        // Fog modes, the procedural sky, post-processing and weather: each an
        // append-only record of its own, written only when it differs from the
        // defaults, so an untouched scene's text stays exactly as it was.
        {
            const Scene::RenderSettings d;
            if (rs.fogMode != d.fogMode || rs.fogDensity != d.fogDensity || rs.fogHeight != d.fogHeight ||
                rs.fogFalloff != d.fogFalloff || rs.fogMatchSky != d.fogMatchSky)
                out << "fogmode " << rs.fogMode << " " << rs.fogDensity << " " << rs.fogHeight << " "
                    << rs.fogFalloff << " " << (rs.fogMatchSky ? 1 : 0) << "\n";
            if (rs.skyMode != d.skyMode || rs.skySunDisc != d.skySunDisc || rs.skySunHalo != d.skySunHalo ||
                rs.skyHaze != d.skyHaze || rs.cloudCover != d.cloudCover || rs.cloudDensity != d.cloudDensity ||
                rs.cloudSpeed != d.cloudSpeed || rs.cloudScale != d.cloudScale)
                out << "skyproc " << rs.skyMode << " " << rs.skySunDisc << " " << rs.skySunHalo << " " << rs.skyHaze << " "
                    << rs.cloudCover << " " << rs.cloudDensity << " " << rs.cloudSpeed << " " << rs.cloudScale << "\n";
            const auto& p = rs.post; const auto& q = d.post;
            auto same3 = [](const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
            const bool postDefault =
                p.exposure == q.exposure && p.tonemap == q.tonemap && p.bloom == q.bloom &&
                p.bloomThreshold == q.bloomThreshold && p.bloomIntensity == q.bloomIntensity && p.bloomRadius == q.bloomRadius &&
                p.grading == q.grading && p.temperature == q.temperature && p.tint == q.tint &&
                p.saturation == q.saturation && p.contrast == q.contrast &&
                same3(p.lift, q.lift) && same3(p.gamma, q.gamma) && same3(p.gain, q.gain) &&
                p.vignetteSmooth == q.vignetteSmooth && p.fxaa == q.fxaa &&
                p.ssao == q.ssao && p.ssaoRadius == q.ssaoRadius && p.ssaoIntensity == q.ssaoIntensity &&
                p.dof == q.dof && p.dofFocus == q.dofFocus && p.dofRange == q.dofRange &&
                p.bloomSoftness == q.bloomSoftness && p.ssaoPower == q.ssaoPower;
            if (!postDefault)
                out << "post " << p.exposure << " " << p.tonemap << " " << (p.bloom ? 1 : 0) << " "
                    << p.bloomThreshold << " " << p.bloomIntensity << " " << p.bloomRadius << " "
                    << (p.grading ? 1 : 0) << " " << p.temperature << " " << p.tint << " " << p.saturation << " " << p.contrast << " "
                    << p.lift.x << " " << p.lift.y << " " << p.lift.z << " "
                    << p.gamma.x << " " << p.gamma.y << " " << p.gamma.z << " "
                    << p.gain.x << " " << p.gain.y << " " << p.gain.z << " "
                    << p.vignetteSmooth << " " << (p.fxaa ? 1 : 0) << " "
                    << (p.ssao ? 1 : 0) << " " << p.ssaoRadius << " " << p.ssaoIntensity << " "
                    << (p.dof ? 1 : 0) << " " << p.dofFocus << " " << p.dofRange << "\n";
            // Newer quality settings live on their own line so older files and older readers are unaffected.
            if (p.bloomSoftness != q.bloomSoftness || p.ssaoPower != q.ssaoPower)
                out << "postq " << p.bloomSoftness << " " << p.ssaoPower << "\n";
            const auto& w = rs.weather; const auto& dw = d.weather;
            if (w.preset != dw.preset || w.intensity != dw.intensity || w.fade != dw.fade ||
                !same3(w.wind, dw.wind) || !w.thunderSound.empty())
                out << "weather " << w.preset << " " << w.intensity << " " << w.fade << " "
                    << w.wind.x << " " << w.wind.y << " " << w.wind.z << " " << Quote(w.thunderSound) << "\n";
        }
    }
    const auto& objs = scene.Objects();
    // A runtime-only (transient) object is never written, and neither is anything under it: a
    // child left in the file would come back as a loose root, its local pose meant for a parent
    // that is not there. The same goes for a generated rig and build mode's runtime objects. The
    // bits are worked out once per object from its parent's (remembered), so a deep chain stays linear.
    std::unordered_map<const GameObject*, unsigned> skipBits;
    auto skipBitsOf = [&skipBits](const GameObject* start) {
        std::vector<const GameObject*> path;   // from `start` up to the first object already known
        unsigned above = 0;
        for (const GameObject* g = start; g;) {
            auto m = skipBits.find(g);
            if (m != skipBits.end()) { above = m->second; break; }
            path.push_back(g);
            const Transform* p = g->transform ? g->transform->Parent() : nullptr;
            g = p ? p->gameObject : nullptr;
        }
        for (std::size_t i = path.size(); i-- > 0;) { above |= OwnSkipBits(path[i]); skipBits[path[i]] = above; }
        return above;
    };
    auto skipped = [&skipBitsOf](GameObject* go) {
        return skipBitsOf(go) != 0 || IsGeneratedViewmodel(go);   // a Character's part rig (and the FP viewmodel arm) is regenerated on load
    };
    std::unordered_map<const GameObject*, int> indexOf;
    indexOf.reserve(objs.size());
    for (std::size_t i = 0; i < objs.size(); ++i) indexOf[objs[i].get()] = (int)i;
    // Sibling order. Loading attaches children in file order, so a parent whose
    // children were reordered later needs the real position written (`sibling`).
    std::unordered_map<const GameObject*, int> siblingPos;
    {
        std::unordered_map<const Transform*, bool> done;
        for (std::size_t i = 0; i < objs.size(); ++i) {
            const Transform* par = objs[i]->transform->Parent();
            if (!par || done[par]) continue;
            done[par] = true;
            std::vector<std::pair<int, const GameObject*>> kids;   // (scene index, child) in sibling order
            for (const Transform* ch : par->Children())
                if (!skipped(ch->gameObject)) { auto ci = indexOf.find(ch->gameObject); if (ci != indexOf.end()) kids.push_back({ci->second, ch->gameObject}); }
            std::vector<int> sorted;
            for (auto& k : kids) sorted.push_back(k.first);
            std::sort(sorted.begin(), sorted.end());
            for (std::size_t pos = 0; pos < kids.size(); ++pos)
                if (sorted[pos] != kids[pos].first) {   // not where file order would put it
                    for (std::size_t q = 0; q < kids.size(); ++q) siblingPos[kids[q].second] = (int)q;
                    break;
                }
        }
    }
    for (std::size_t i = 0; i < objs.size(); ++i) {
        GameObject* go = objs[i].get();
        if (skipped(go)) continue;
        Transform* t = go->transform;
        out << "gameobject " << i << " " << Quote(go->name) << "\n";
        out << "  guid " << Quote(go->Guid()) << "\n";
        out << "  active " << (go->active ? 1 : 0) << "\n";
        if (!go->tag.empty()) out << "  tag " << Quote(go->tag) << "\n";
        if (go->isStatic) out << "  static 1\n";
        if (go->editorLocked) out << "  locked 1\n";
        if (go->layer != 0) out << "  layer " << go->layer << "\n";
        if (go->uiDrawOrder != 0) out << "  uiorder " << go->uiDrawOrder << "\n";
        if (!go->sourceScene.empty()) out << "  srcscene " << Quote(go->sourceScene) << "\n";
        if (!go->prefabSource.empty()) out << "  prefab " << Quote(go->prefabSource) << "\n";
        for (const auto& ov : go->prefabOverrides) out << "  override " << ov.object << " " << Quote(ov.line) << "\n";
        int parent = -1;
        if (t->Parent()) { auto pit = indexOf.find(t->Parent()->gameObject); if (pit != indexOf.end()) parent = pit->second; }
        out << "  parent " << parent << "\n";
        auto sp = siblingPos.find(go);
        if (sp != siblingPos.end()) out << "  sibling " << sp->second << "\n";
        WriteComponents(out, go);
        out << "end\n";
    }
    return out.str();
}

namespace {
// The decoded text of recently read prefab files, keyed by path and checked against the file's
// size and time, so spawning the same prefab again does not reopen and decode it. A file that was
// modified in the last two seconds is never cached (its time cannot yet tell two writes apart),
// and every write the engine makes clears the cache.
struct CachedPrefab { std::filesystem::file_time_type time; std::uintmax_t size = 0; std::string text; };
std::mutex& PrefabCacheMutex() { static std::mutex m; return m; }
std::unordered_map<std::string, CachedPrefab>& PrefabCacheMap() {
    static std::unordered_map<std::string, CachedPrefab> c;
    return c;
}
struct ClearPrefabCacheOnExit { ~ClearPrefabCacheOnExit() { SceneSerializer::ClearPrefabCache(); } };
} // namespace

void SceneSerializer::ClearPrefabCache() {
    std::lock_guard<std::mutex> lock(PrefabCacheMutex());
    PrefabCacheMap().clear();
}

bool SceneSerializer::SaveToFile(const Scene& scene, const std::string& path, std::string* error) {
    ClearPrefabCacheOnExit clearPrefabCache;   // whatever happens below, the next read sees the new file
    namespace fs = std::filesystem;
    // Atomic save: the new text goes to "<path>.savetmp" first and is checked, then renamed over the
    // target, so a crash or a full disk mid-write can never leave a truncated scene. The target is
    // never moved or removed before the new file is complete: one rolling "<path>.bak" keeps the
    // previous good copy (a copy, so a failed rename leaves the scene exactly where it was).
    auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
    std::error_code ec;
    const fs::path target = FilePath(path);
    if (fs::exists(target, ec)) {
        if (fs::is_directory(target, ec)) return fail("\"" + path + "\" is a folder, not a scene file.");
        const fs::perms perm = fs::status(target, ec).permissions();
        if (!ec && (perm & (fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write)) == fs::perms::none)
            return fail("\"" + target.filename().string() + "\" is read-only. Make it writable, or save under another name.");
    }
    if (target.has_parent_path() && !fs::is_directory(target.parent_path(), ec))
        return fail("The folder \"" + target.parent_path().string() + "\" doesn't exist.");
    const std::string text = Serialize(scene);
    const std::string tmp = path + ".savetmp";
    {
        errno = 0;
        std::ofstream f(FilePath(tmp), std::ios::binary | std::ios::trunc);
        if (!f) return fail("Couldn't write \"" + tmp + "\": " + (errno ? std::error_code(errno, std::generic_category()).message() : std::string("no permission")));
        f.write(text.data(), (std::streamsize)text.size());
        f.flush();
        if (!f) {
            const int e = errno;
            f.close();
            fs::remove(FilePath(tmp), ec);
            return fail("Couldn't write \"" + path + "\": " + (e ? std::error_code(e, std::generic_category()).message() : std::string("the disk may be full")));
        }
    }
    const std::uintmax_t written = fs::file_size(FilePath(tmp), ec);
    if (ec || written != text.size()) {
        fs::remove(FilePath(tmp), ec);
        return fail("Couldn't write \"" + path + "\": the file came out the wrong size (is the disk full?).");
    }
    if (fs::exists(target, ec)) {   // best-effort previous-version backup; the target itself stays put
        std::error_code be;
        CopyFileOver(target, FilePath(path + ".bak"), be);   // a plain copy_file never refreshed an existing .bak on Windows (MinGW)
    }
    ec.clear();
    fs::rename(FilePath(tmp), target, ec);      // atomic replace on the same filesystem
    if (ec) {
        // Locked or a platform that won't rename over an existing file: overwrite by copy. The old file is
        // still in place until this succeeds, and the backup above holds its content if it half-fails.
        std::error_code ce;
        CopyFileOver(FilePath(tmp), target, ce);
        std::error_code rc;
        fs::remove(FilePath(tmp), rc);
        if (ce) return fail("Couldn't replace \"" + path + "\": " + ce.message());
    }
    return true;
}


static bool ParseCore(Scene& scene, const std::string& text, bool clear,
                      GameObject** firstNew, std::string* error,
                      std::vector<std::string>* warnings) {
    ParseSession S(text);
    return ParseStep(scene, S, clear, firstNew, error, warnings);
}

// Parse `text` into `scene` or leave `scene` as it was. A full load (clear) is
// checked first by building everything in a throwaway Scene, and the live scene
// is cleared and filled only once that pass succeeded. An appended load
// (Instantiate, merge) is a hot path, so it parses once, straight into the
// scene, and on failure destroys what it added. Everything a parse creates is
// new (parent links only join objects of the same document), so nothing that
// was already in the scene points at the rolled-back objects.
static bool ParseInto(Scene& scene, const std::string& text, bool clear,
                      GameObject** firstNew, std::string* error,
                      std::vector<std::string>* warnings = nullptr) {
    SceneFileLoadScope loadingFile;   // don't invent components the file didn't write
    std::vector<std::string> found;
    auto report = [&]() {
        if (warnings) warnings->insert(warnings->end(), found.begin(), found.end());
        else for (const auto& w : found) std::cerr << "[scene] warning: " << w << "\n";
    };
    if (clear) {
        {
            Scene probe("probe");
            if (!ParseCore(probe, text, true, nullptr, error, &found)) return false;
        }
        report();
        if (scene.IsUpdating()) { scene.RequestLoadText(text); return true; }   // objects are in use: loaded at the end of the frame
        scene.Clear();
        return ParseCore(scene, text, true, firstNew, error, nullptr);   // accepted above
    }
    const std::size_t base = scene.Objects().size();
    GameObject* first = nullptr;
    if (ParseCore(scene, text, false, &first, error, &found)) {
        report();
        if (firstNew && !*firstNew) *firstNew = first;
        return true;
    }
    std::vector<GameObject*> added;
    for (std::size_t i = base; i < scene.Objects().size(); ++i) added.push_back(scene.Objects()[i].get());
    for (GameObject* go : added) scene.Destroy(go);   // detaches each from any parent too
    scene.FlushDestroyed();
    return false;
}

bool SceneSerializer::Deserialize(Scene& scene, const std::string& text, std::string* error,
                                  std::vector<std::string>* warnings) {
    if (g_loadingSceneDir.empty() && !g_lastSceneDir.empty()) {
        g_loadingSceneDir = g_lastSceneDir;
        struct Reset { ~Reset() { g_loadingSceneDir.clear(); } } reset;
        return ParseInto(scene, text, /*clear=*/true, /*firstNew=*/nullptr, error, warnings);
    }
    return ParseInto(scene, text, /*clear=*/true, /*firstNew=*/nullptr, error, warnings);
}

SceneLoadJob::~SceneLoadJob() = default;

std::unique_ptr<SceneLoadJob> SceneLoadJob::OpenText(std::string text, bool additive, const Vec3& offset) {
    std::unique_ptr<SceneLoadJob> j(new SceneLoadJob());
    j->m_size = text.empty() ? 1 : text.size();
    j->m_session = std::make_unique<ParseSession>(std::move(text));
    j->m_additive = additive;
    j->m_offset = offset;
    return j;
}

std::unique_ptr<SceneLoadJob> SceneLoadJob::OpenFile(const std::string& path, bool additive,
                                                     const Vec3& offset, std::string* error) {
    std::ifstream f(FilePath(path), std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return nullptr; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string raw = ss.str();
    std::string text = DataPack::Unpack(raw);
    if (text.empty() && DataPack::IsPacked(raw)) { if (error) *error = "tampered or unreadable " + path; return nullptr; }
    auto j = OpenText(std::move(text), additive, offset);
    const std::size_t slash = path.find_last_of("/\\");
    j->m_dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    return j;
}

std::vector<std::uint64_t> SceneLoadJob::AllIds() const {
    std::vector<std::uint64_t> ids;
    if (m_session) for (auto& kv : m_session->byIndex) ids.push_back(kv.second->InstanceId());
    return ids;
}

float SceneLoadJob::Progress() const {
    if (m_finished) return m_failed ? 0.0f : 1.0f;
    if (!m_session || !m_started) return 0.0f;
    const std::streamoff at = m_session->in.rdbuf()->pubseekoff(0, std::ios::cur, std::ios::in);
    if (at < 0) return 0.0f;
    const float f = (float)((double)at / (double)m_size);
    return f > 0.999f ? 0.999f : f;   // 1 only once the links are resolved
}

bool SceneLoadJob::Step(Scene& scene, int maxObjects, std::string* error) {
    if (m_finished) return !m_failed;
    SceneFileLoadScope loadingFile;   // as in ParseInto: don't invent components the file didn't write
    const std::string prevDir = g_loadingSceneDir;
    g_loadingSceneDir = m_dir;
    struct Restore { const std::string& d; ~Restore() { g_loadingSceneDir = d; } } restore{prevDir};
    auto fail = [&]() {
        m_finished = true; m_failed = true;
        return false;
    };
    if (!m_started) {
        m_started = true;
        // A whole-scene load is checked in full first, in a throwaway scene, and the
        // live scene is cleared only once that passed (the same all-or-nothing rule
        // as Deserialize). One frame pays for this pass; the objects themselves are
        // then made a few per frame.
        if (!m_additive) {
            Scene probe("probe");
            ParseSession P(m_session->text);
            if (!ParseStep(probe, P, true, nullptr, error, nullptr)) return fail();
            scene.Clear();
        }
    }
    bool done = true;
    const bool ok = ParseStep(scene, *m_session, /*clear=*/!m_additive, nullptr, error, nullptr, maxObjects, &done);
    m_created = (int)m_session->byIndex.size();
    if (!ok) {
        // An additive load stops half-way: take back what it made so the scene is
        // left as it was (a whole-scene load cannot get here; the probe passed).
        std::vector<GameObject*> made;
        for (auto& kv : m_session->byIndex) made.push_back(kv.second);
        for (GameObject* go : made) scene.Destroy(go);
        scene.FlushDestroyed();
        m_session->byIndex.clear();
        m_created = 0;
        return fail();
    }
    if (done) {
        for (const auto& w : m_session->warnings) std::cerr << "[scene] warning: " << w << "\n";
        std::vector<int> order;
        for (auto& kv : m_session->byIndex) order.push_back(kv.first);
        std::sort(order.begin(), order.end());
        const bool shift = m_additive && (m_offset.x != 0.0f || m_offset.y != 0.0f || m_offset.z != 0.0f);
        for (int idx : order) {
            GameObject* go = m_session->byIndex[idx];
            if (!go->transform || go->transform->Parent()) continue;
            if (shift) go->transform->localPosition = go->transform->localPosition + m_offset;
            m_roots.push_back(go->InstanceId());
        }
        m_finished = true;
    }
    return true;
}


std::string SceneSerializer::SerializeObject(const GameObject& root) {
    VehicleSeatSaveGuard seatGuard;
    // Gather the object and all its descendants (depth-first, in sibling order). A stack, not a
    // recursion: a hierarchy 100000 deep must not overflow the call stack. `skipBits` says why an
    // object is not written (see OwnSkipBits): inherited from its parent inside the subtree. Above
    // the root only a generated rig or a build mode object counts (as it always did), not `transient`.
    std::vector<GameObject*> subtree;
    std::vector<unsigned> skipBits;
    {
        unsigned rootBits = 0;
        for (const Transform* p = root.transform ? root.transform->Parent() : nullptr; p; p = p->Parent())
            if (p->gameObject) rootBits |= OwnSkipBits(p->gameObject) & ~kSkipTransient;
        std::vector<std::pair<GameObject*, unsigned>> stack{{const_cast<GameObject*>(&root), rootBits}};
        while (!stack.empty()) {
            auto [go, above] = stack.back(); stack.pop_back();
            const unsigned bits = above | OwnSkipBits(go);
            subtree.push_back(go);
            skipBits.push_back(bits);
            const auto& kids = go->transform->Children();
            for (std::size_t i = kids.size(); i-- > 0;) stack.push_back({kids[i]->gameObject, bits});
        }
    }

    std::unordered_map<const GameObject*, int> localIndex;
    for (std::size_t i = 0; i < subtree.size(); ++i) localIndex[subtree[i]] = (int)i;

    std::ostringstream out;
    UseRoundTripFloats(out);
    out << "okayscene 3\n";
    for (std::size_t i = 0; i < subtree.size(); ++i) {
        GameObject* go = subtree[i];
        if (skipBits[i] != 0 || IsGeneratedViewmodel(go)) continue;   // a Character's part rig (and the FP viewmodel arm) is regenerated on load
        out << "gameobject " << i << " " << Quote(go->name) << "\n";
        out << "  active " << (go->active ? 1 : 0) << "\n";
        if (!go->tag.empty()) out << "  tag " << Quote(go->tag) << "\n";
        if (go->isStatic) out << "  static 1\n";
        if (go->editorLocked) out << "  locked 1\n";
        if (go->layer != 0) out << "  layer " << go->layer << "\n";
        if (go->uiDrawOrder != 0) out << "  uiorder " << go->uiDrawOrder << "\n";
        if (!go->sourceScene.empty()) out << "  srcscene " << Quote(go->sourceScene) << "\n";
        Transform* parent = go->transform->Parent();
        int pIdx = -1;
        if (parent) {
            auto it = localIndex.find(parent->gameObject);
            if (it != localIndex.end()) pIdx = it->second;
        }
        if (!go->prefabSource.empty()) out << "  prefab " << Quote(go->prefabSource) << "\n";
        for (const auto& ov : go->prefabOverrides) out << "  override " << ov.object << " " << Quote(ov.line) << "\n";
        out << "  parent " << pIdx << "\n";
        WriteComponents(out, go);
        out << "end\n";
    }
    return out.str();
}

GameObject* SceneSerializer::Instantiate(Scene& scene, const GameObject& prefab) {
    std::string text = SerializeObject(prefab);
    GameObject* root = nullptr;
    std::string error;
    if (!ParseInto(scene, text, /*clear=*/false, &root, &error)) {
        std::cerr << "[scene] Instantiate failed: " << error << "\n";
        return nullptr;
    }
    return root;
}

bool SceneSerializer::SaveObjectToFile(const GameObject& root, const std::string& path) {
    ClearPrefabCacheOnExit clearPrefabCache;
    std::ofstream f(FilePath(path), std::ios::binary | std::ios::trunc);   // binary: LF on every platform (text mode wrote CRLF on Windows)
    if (!f) return false;
    f << SerializeObject(root);
    return static_cast<bool>(f);
}

GameObject* SceneSerializer::InstantiateFromFile(Scene& scene, const std::string& path,
                                                 std::string* error, std::vector<std::string>* warnings) {
    std::mutex& cacheMutex = PrefabCacheMutex();
    auto& cache = PrefabCacheMap();
    std::string text;
    {
        std::error_code ec1, ec2;
        const auto mtime = std::filesystem::last_write_time(FilePath(path), ec1);
        const auto fsize = std::filesystem::file_size(FilePath(path), ec2);
        bool hit = false;
        if (!ec1 && !ec2) {
            std::lock_guard<std::mutex> lock(cacheMutex);
            auto it = cache.find(path);
            if (it != cache.end() && it->second.time == mtime && it->second.size == fsize) { text = it->second.text; hit = true; }
        }
        if (!hit) {
            std::ifstream f(FilePath(path), std::ios::binary);
            if (!f) { if (error) *error = "cannot open " + path; return nullptr; }
            std::stringstream ss; ss << f.rdbuf();
            text = DataPack::Unpack(ss.str());   // decode if obfuscated
            if (text.empty() && DataPack::IsPacked(ss.str())) { if (error) *error = "tampered or unreadable " + path; return nullptr; }
            if (!ec1 && !ec2 && (std::filesystem::file_time_type::clock::now() - mtime) > std::chrono::seconds(2)) {
                std::lock_guard<std::mutex> lock(cacheMutex);
                if (cache.size() >= 32) cache.clear();   // small and simple: start over when full
                cache[path] = CachedPrefab{mtime, fsize, text};
            }
        }
    }
    if (PrefabTools::UsesPrefabStructure(text)) return PrefabTools::Instantiate(scene, path, {}, error, warnings);   // nested prefabs / variant
    GameObject* root = nullptr;
    if (!ParseInto(scene, text, /*clear=*/false, &root, error, warnings)) return nullptr;
    // Link the new instance to its source prefab so the editor can apply/revert
    // and reload it. Only the root carries the link.
    if (root) root->prefabSource = path;
    return root;
}

GameObject* SceneSerializer::InstantiateFromText(Scene& scene, const std::string& text,
                                                 std::string* error, std::vector<std::string>* warnings) {
    GameObject* root = nullptr;
    if (!ParseInto(scene, text, /*clear=*/false, &root, error, warnings)) return nullptr;
    return root;
}

std::vector<std::string> SceneSerializer::CollectAssetPaths(const Scene& scene) {
    std::vector<std::string> out;
    auto add = [&](const std::string& p) {
        if (p.empty() || p[0] == '@') return;   // '@name' is a picture made in memory (a terrain's baked layers): no file to ship
        for (const auto& e : out) if (e == p) return; // unique
        out.push_back(p);
    };
    for (const auto& go : scene.Objects()) {
        for (auto* sr : go->GetComponents<SpriteRenderer>()) add(sr->texture);
        for (auto* au : go->GetComponents<AudioSource>())  add(au->clipPath);   // every source: a music bed and an effect can share an object
        for (auto* mp : go->GetComponents<MusicPlayer>())    for (const auto& t : mp->tracks) add(t);
        for (auto* mr : go->GetComponents<MeshRenderer>())   { add(mr->meshPath); add(mr->texture); for (const std::string& st : mr->slotTextures) add(st); add(mr->normalMap); add(mr->specularMap); add(mr->aoMap); add(mr->metallicRoughnessMap); add(mr->emissiveMap); }
        for (auto* terrain : go->GetComponents<Terrain>())         { add(terrain->texture); add(terrain->normalMap); for (const auto& l : terrain->layers) add(l.texture); }
        for (auto* fo : go->GetComponents<Foliage>())       { add(fo->meshPath); add(fo->prefabPath); }
        for (auto* de : go->GetComponents<Decal>())         add(de->texture);
        for (auto* ps : go->GetComponents<ParticleSystem>()) add(ps->texture);
        for (auto* sw : go->GetComponents<Spawner>())       add(sw->prefabPath);   // the prefab a spawner makes ships with the scene
        for (auto* im : go->GetComponents<UIImage>())         add(im->texture);
        for (auto* ri : go->GetComponents<UIRawImage>())      add(ri->texture);
        for (auto* button : go->GetComponents<UIButton>())        add(button->icon);
        for (auto* an : go->GetComponents<SpriteAnimator>()) {
            for (const auto& f : an->frames) add(f);
            for (const auto& c : an->clips) for (const auto& f : c.frames) add(f);
        }
        for (auto* bm : go->GetComponents<BuildMode>())
            for (const auto& it : bm->palette) { add(it.prefab); add(it.icon); }
        for (auto* sp : go->GetComponents<SequencePlayer>()) {
            add(sp->sequencePath);
            Sequence seq;   // the sounds its audio cues play ship with it
            if (Sequence::LoadFromFile(sp->ResolvedPath(), seq))
                for (const auto& trk : seq.audio) for (const auto& c : trk.cues) add(c.file);
        }
        for (auto* lt : go->GetComponents<LogicTarget>()) add(lt->spawnPrefab);
        for (auto* ls : go->GetComponents<LootSource>()) add(ls->table);
        for (auto* ns : go->GetComponents<NavMeshSurface>()) add(ns->navPath);   // the baked navmesh ships with the scene
        for (auto* runner : go->GetComponents<BehaviorTreeRunner>()) {
            add(runner->treePath);   // the tree ships with the scene, and so does every tree it runs
            std::vector<std::string> nested;
            BehaviorTree::CollectReferencedFiles(runner->treePath, nested);
            for (const std::string& p : nested) add(p);
        }
        for (auto* sm : go->GetComponents<SplineMesh>()) { const std::string& m = sm->sourceMesh; if (m.find('.') != std::string::npos) add(m); }
        for (auto* si : go->GetComponents<SplineInstancer>()) add(si->prefabPath);
        for (auto* lr : go->GetComponents<LineRenderer>()) add(lr->texture);
        for (auto* trail : go->GetComponents<TrailRenderer>()) add(trail->texture);
    }
    // The item database (and its icons / drop prefabs) ships with any scene that has items.
    bool hasItems = false;
    for (const auto& go : scene.Objects())
        if (go->GetComponent<Inventory>() || go->GetComponent<GridInventory>() || go->GetComponent<ItemPickup>() || go->GetComponent<LootSource>() || go->GetComponent<Equipment>()) { hasItems = true; break; }
    const ItemDatabase& db = ItemDatabase::Get();
    if (hasItems && !db.Path().empty()) {
        add(db.Path());
        for (const auto& d : db.Items()) if (!d.loose) { add(d.icon); add(d.worldPrefab); add(d.Custom("equipPrefab")); }
    }
    return out;
}

bool SceneSerializer::LoadFromFile(Scene& scene, const std::string& path, std::string* error,
                                   std::vector<std::string>* warnings) {
    std::ifstream f(FilePath(path), std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string raw = ss.str();
    std::string text = DataPack::Unpack(raw);   // decode if obfuscated (plaintext passes through)
    if (text.empty() && DataPack::IsPacked(raw)) { if (error) *error = "tampered or unreadable " + path; return false; }
    if (scene.IsUpdating()) { scene.RequestLoad(path); return true; }   // objects are in use (a callback): loaded at the end of the frame
    // Mesh files the scene points at are also looked for next to it.
    const std::size_t slash = path.find_last_of("/\\");
    const std::string prevDir = g_loadingSceneDir;
    g_loadingSceneDir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
    const bool ok = Deserialize(scene, text, error, warnings);
    if (ok && !g_loadingSceneDir.empty()) g_lastSceneDir = g_loadingSceneDir;
    g_loadingSceneDir = prevDir;
    return ok;
}

bool SceneSerializer::MergeFromText(Scene& scene, const std::string& text, const Vec3& offset,
                                    std::vector<GameObject*>* addedRoots, std::string* error,
                                    std::vector<std::string>* warnings) {
    std::size_t base = scene.Objects().size();          // everything from here is new
    GameObject* first = nullptr;
    if (!ParseInto(scene, text, /*clear=*/false, &first, error, warnings)) return false;
    const bool shift = (offset.x != 0.0f || offset.y != 0.0f || offset.z != 0.0f);
    const auto& objs = scene.Objects();
    for (std::size_t i = base; i < objs.size(); ++i) {
        GameObject* go = objs[i].get();
        // Roots of the merged set (parents were linked within the merge during parse).
        if (go->transform && go->transform->Parent() == nullptr) {
            if (shift) go->transform->localPosition = go->transform->localPosition + offset;
            if (addedRoots) addedRoots->push_back(go);
        }
    }
    return true;
}

bool SceneSerializer::MergeFromFile(Scene& scene, const std::string& path, const Vec3& offset,
                                    std::vector<GameObject*>* addedRoots, std::string* error,
                                    std::vector<std::string>* warnings) {
    std::ifstream f(FilePath(path), std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string raw = ss.str();
    std::string text = DataPack::Unpack(raw);
    if (text.empty() && DataPack::IsPacked(raw)) { if (error) *error = "tampered or unreadable " + path; return false; }
    return MergeFromText(scene, text, offset, addedRoots, error, warnings);
}



// ---- Prefab overrides -----------------------------------------------------
namespace {
struct TextObject {
    std::string header;                 // the `gameobject <i> "name"` line
    std::vector<std::string> lines;     // its records, without the closing `end`
};

// Split serialized scene text into the lines before the first object and the objects.
void SplitObjects(const std::string& text, std::string& preamble, std::vector<TextObject>& objs) {
    std::istringstream in(text);
    std::string line;
    bool inObj = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!inObj) {
            if (line.rfind("gameobject ", 0) == 0) { objs.push_back({line, {}}); inObj = true; }
            else preamble += line + "\n";
        } else if (line == "end") inObj = false;
        else objs.back().lines.push_back(line);
    }
}

std::string JoinObjects(const std::string& preamble, const std::vector<TextObject>& objs) {
    std::string out = preamble;
    for (const auto& o : objs) {
        out += o.header + "\n";
        for (const auto& l : o.lines) out += l + "\n";
        out += "end\n";
    }
    return out;
}

std::string FirstToken(const std::string& line) {
    std::istringstream ls(line);
    std::string t; ls >> t;
    return t;
}

// Records that describe the object's link to its prefab or its place in a
// document, not its content: never overridden. `comporder` is derived from the components the object has and the
// order they were added in, which a plain AddComponent now records (an instance that gained a component also
// reads as having a different order); the component record is what differs, so this one is not listed twice.
bool IsBookkeepingRecord(const std::string& tok) {
    return tok == "prefab" || tok == "override" || tok == "guid" || tok == "sibling" || tok == "parent" ||
           tok == "srcscene" || tok == "locked" || tok == "comporder" || tok.empty();
}

// A record's identity inside one object: its name plus which occurrence of it (so two
// colliders stay distinct). `compoff` lines are identified by their whole text.
std::string RecordKey(const std::string& line, std::unordered_map<std::string, int>& seen) {
    const std::string tok = FirstToken(line);
    if (tok == "compoff") return line;
    return tok + "#" + std::to_string(seen[tok]++);
}

bool ReadFileText(const std::string& path, std::string& text, std::string* error) {
    std::ifstream f(FilePath(path), std::ios::binary);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    const std::string raw = ss.str();
    text = DataPack::Unpack(raw);
    if (text.empty() && DataPack::IsPacked(raw)) { if (error) *error = "tampered or unreadable " + path; return false; }
    return true;
}

// The prefab text with each override's record swapped in (or added, when the
// prefab has no such record). Overrides that name an object the prefab no longer
// has are dropped.
std::string SpliceOverrides(const std::string& prefabText, const std::vector<GameObject::PrefabOverride>& overrides,
                            std::vector<std::string>* warnings) {
    std::string preamble;
    std::vector<TextObject> objs;
    SplitObjects(prefabText, preamble, objs);
    for (const auto& ov : overrides) {
        if (ov.object < 0 || (std::size_t)ov.object >= objs.size()) {
            if (warnings) warnings->push_back("prefab override for object " + std::to_string(ov.object) + " dropped: the prefab no longer has it");
            continue;
        }
        TextObject& o = objs[(std::size_t)ov.object];
        const std::string want = "  " + ov.line;
        std::unordered_map<std::string, int> seen;
        std::string ovSeen;
        const std::string ovKeyTok = FirstToken(ov.line);
        // Which occurrence of its record name does this override mean? Overrides are
        // captured in record order, so count the earlier overrides of the same name.
        int occ = 0;
        for (const auto& prev : overrides) {
            if (&prev == &ov) break;
            if (prev.object == ov.object && FirstToken(prev.line) == ovKeyTok) ++occ;
        }
        bool replaced = false;
        if (ovKeyTok != "compoff") {
            int n = 0;
            for (auto& l : o.lines)
                if (FirstToken(l) == ovKeyTok && n++ == occ) { l = want; replaced = true; break; }
        } else {
            for (auto& l : o.lines) if (l == want) { replaced = true; break; }
        }
        if (!replaced) o.lines.push_back(want);
    }
    return JoinObjects(preamble, objs);
}

} // namespace

bool SceneSerializer::CapturePrefabOverrides(GameObject& instanceRoot, std::string* error) {
    if (instanceRoot.prefabSource.empty()) { if (error) *error = "object is not a prefab instance"; return false; }
    std::string fileText;
    if (!ReadFileText(instanceRoot.prefabSource, fileText, error)) return false;
    if (PrefabTools::UsesPrefabStructure(fileText)) return PrefabTools::CaptureOverrides(instanceRoot, error);   // nested prefabs / variant
    // Canonical form of the prefab: load it into a scratch scene and write it back out,
    // so both sides of the comparison come from the same writer.
    Scene scratch("prefab");
    GameObject* proot = nullptr;
    if (!ParseInto(scratch, fileText, /*clear=*/false, &proot, error, nullptr) || !proot) {
        if (error && error->empty()) *error = "empty prefab " + instanceRoot.prefabSource;
        return false;
    }
    std::string pPre, iPre;
    std::vector<TextObject> pObjs, iObjs;
    SplitObjects(SerializeObject(*proot), pPre, pObjs);
    SplitObjects(SerializeObject(instanceRoot), iPre, iObjs);

    std::vector<GameObject::PrefabOverride> found;
    const std::size_t n = std::min(pObjs.size(), iObjs.size());
    for (std::size_t i = 0; i < n; ++i) {
        std::unordered_map<std::string, std::string> base;   // key -> prefab line
        std::unordered_map<std::string, int> seen;
        for (const auto& l : pObjs[i].lines) {
            const std::string tok = FirstToken(l);
            if (!IsBookkeepingRecord(tok)) base[RecordKey(l, seen)] = l;
        }
        seen.clear();
        for (const auto& l : iObjs[i].lines) {
            const std::string tok = FirstToken(l);
            if (IsBookkeepingRecord(tok)) continue;
            auto b = base.find(RecordKey(l, seen));
            if (b == base.end() || b->second != l) {
                std::size_t at = l.find_first_not_of(' ');
                found.push_back({(int)i, l.substr(at == std::string::npos ? 0 : at)});
            }
        }
    }
    instanceRoot.prefabOverrides = std::move(found);
    return true;
}

GameObject* SceneSerializer::InstantiatePrefab(Scene& scene, const std::string& path,
                                               const std::vector<GameObject::PrefabOverride>& overrides,
                                               std::string* error, std::vector<std::string>* warnings) {
    std::string fileText;
    if (!ReadFileText(path, fileText, error)) return nullptr;
    if (PrefabTools::UsesPrefabStructure(fileText)) return PrefabTools::Instantiate(scene, path, overrides, error, warnings);   // nested prefabs / variant
    std::vector<std::string> own;
    const std::string text = SpliceOverrides(fileText, overrides, &own);
    GameObject* root = nullptr;
    if (!ParseInto(scene, text, /*clear=*/false, &root, error, warnings)) return nullptr;
    if (warnings) warnings->insert(warnings->end(), own.begin(), own.end());
    if (root) { root->prefabSource = path; root->prefabOverrides = overrides; }
    return root;
}

GameObject* SceneSerializer::ReapplyPrefab(Scene& scene, GameObject& instanceRoot, std::string* error,
                                           std::vector<std::string>* warnings) {
    if (instanceRoot.prefabSource.empty()) { if (error) *error = "object is not a prefab instance"; return nullptr; }
    // Every prefab file, plain, nested or variant, is rebuilt by PrefabTools::Reapply: it keeps the instance's
    // name, place and ids, and moves objects added under the instance onto the rebuilt one. (The plain path
    // used to build a fresh copy here and lost those added objects.)
    return PrefabTools::Reapply(scene, instanceRoot, error, warnings);
}

// ---- Validation ---------------------------------------------------------

namespace {

struct ScanObj { int index = -1; std::string name; int parent = -1; };

// The text pass both validators share: object names, parent links and guids, read straight from the
// scene text (one record per line) before anything is loaded, because the loader repairs these things
// (a duplicate guid is renamed, a parent link into nowhere is dropped) and would hide them.
void ScanSceneLinks(const std::string& text, std::vector<ScanObj>& objs, SceneValidationReport& report) {
    auto add = [&](SceneIssue::Kind k, std::string obj, std::string msg) {
        report.issues.push_back(SceneIssue{k, std::move(obj), std::move(msg)});
    };
    std::unordered_map<std::string, std::string> guidOwner;   // guid -> object label
    std::unordered_map<int, bool> known;
    {
        std::istringstream in(text);
        std::string line;
        ScanObj* cur = nullptr;
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            std::string word;
            if (!(ls >> word)) continue;
            const bool indented = !line.empty() && (line[0] == ' ' || line[0] == '\t');
            if (!indented && word == "gameobject") {
                ScanObj o; ls >> o.index; o.name = ReadQuoted(ls);
                // A guid may also sit on the header line after the name (the loader takes it):
                // check it here too, so duplicates and malformed ids are reported wherever they sit.
                std::string tailWord;
                while (ls >> tailWord) {
                    if (tailWord != "guid") continue;
                    std::string g = ReadQuoted(ls);
                    if (g.empty()) { ls.clear(); ls >> g; }
                    const std::string label = o.name + " (#" + std::to_string(o.index) + ")";
                    if (g.empty() || !SceneText::IsValidGuid(g)) { add(SceneIssue::Kind::DuplicateGuid, label, "malformed guid in the object header"); continue; }
                    auto ins = guidOwner.emplace(g, label);
                    if (!ins.second) add(SceneIssue::Kind::DuplicateGuid, label, "guid " + g + " is also used by " + ins.first->second);
                }
                objs.push_back(o); cur = &objs.back();
                known[o.index] = true;
            } else if (!indented && word == "end") {
                cur = nullptr;
            } else if (indented && cur && word == "parent") {
                ls >> cur->parent;
            } else if (indented && cur && word == "guid") {
                std::string g = ReadQuoted(ls);
                if (g.empty()) { ls.clear(); ls >> g; }
                const std::string label = cur->name + " (#" + std::to_string(cur->index) + ")";
                auto ins = guidOwner.emplace(g, label);
                if (!ins.second) add(SceneIssue::Kind::DuplicateGuid, label, "guid " + g + " is also used by " + ins.first->second);
            }
        }
    }
    for (const ScanObj& o : objs)
        if (o.parent >= 0 && !known.count(o.parent))
            add(SceneIssue::Kind::BrokenParentLink, o.name + " (#" + std::to_string(o.index) + ")",
                "parent " + std::to_string(o.parent) + " is not in the scene");
}

} // namespace

SceneValidationReport ValidateSceneLinks(const std::string& text) {
    std::vector<ScanObj> objs;
    SceneValidationReport report;
    ScanSceneLinks(text, objs, report);
    return report;
}

SceneValidationReport SceneSerializer::Validate(const std::string& text, const std::string& assetRoot) {
    SceneValidationReport report;
    auto add = [&](SceneIssue::Kind k, std::string obj, std::string msg) {
        report.issues.push_back(SceneIssue{k, std::move(obj), std::move(msg)});
    };

    using Obj = ScanObj;
    std::vector<Obj> objs;
    ScanSceneLinks(text, objs, report);

    // Load pass in a throwaway scene: unknown records, and the assets the loaded scene points at.
    Scene probe("validate");
    std::string error;
    std::vector<std::string> warnings;
    if (!ParseCore(probe, text, true, nullptr, &error, &warnings)) {
        add(SceneIssue::Kind::LoadError, "", error);
        return report;
    }
    for (const std::string& w : warnings) {
        const std::string fieldKey = "skipped unknown field '";
        const std::string recordKey = "skipped unknown record '";
        if (w.compare(0, fieldKey.size(), fieldKey) == 0) {
            std::size_t q = w.find('\'', fieldKey.size());
            std::string field = w.substr(fieldKey.size(), q == std::string::npos ? std::string::npos : q - fieldKey.size());
            std::string obj;
            const std::size_t at = w.rfind("object ");
            if (at != std::string::npos) {
                int idx = std::atoi(w.c_str() + at + 7);
                for (const Obj& o : objs) if (o.index == idx) { obj = o.name + " (#" + std::to_string(idx) + ")"; break; }
            }
            if (field == "guid") continue;   // an object id record: checked for duplicates above, not a component
            add(SceneIssue::Kind::MissingComponentType, obj, "unknown component record '" + field + "'");
        } else if (w.compare(0, recordKey.size(), recordKey) == 0) {
            std::size_t q = w.find('\'', recordKey.size());
            add(SceneIssue::Kind::UnknownField, "", "unknown record '" +
                w.substr(recordKey.size(), q == std::string::npos ? std::string::npos : q - recordKey.size()) + "'");
        } else if (w.compare(0, 25, "duplicate guid on object ") == 0) {
            continue;   // the load repaired it; the text pass above already reported it as DuplicateGuid
        } else if (w.rfind("layer out of range on object ", 0) == 0) {
            const int idx = std::atoi(w.c_str() + 29);
            std::string obj;
            for (const Obj& o : objs) if (o.index == idx) { obj = o.name + " (#" + std::to_string(idx) + ")"; break; }
            add(SceneIssue::Kind::BadLayer, obj, w);
        } else {
            add(SceneIssue::Kind::UnknownField, "", w);
        }
    }
    if (Tags::Strict())   // free-form tags are the norm; a project that lists its tags wants the others found
        for (const auto& o : probe.Objects())
            if (!o->tag.empty() && !Tags::Contains(o->tag))
                add(SceneIssue::Kind::UnknownTag, o->name, "tag '" + std::string(o->tag) + "' is not in the project tag list");
    // Whether a file exists is decided once, in SceneValidator::AssetExists (the same rule the semantic validator
    // uses): a built-in "proc:" texture or a URL is not a missing file, and the working directory is tried as well
    // as the asset root.
    testing::ValidatorOptions vopt;
    vopt.assetRoot = assetRoot;
    vopt.searchDirs.push_back("");
    std::unordered_set<std::string> reported;
    for (const std::string& p : CollectAssetPaths(probe)) {
        if (testing::SceneValidator::AssetExists(p, vopt)) continue;
        reported.insert(p);
        add(SceneIssue::Kind::MissingAsset, "", "file not found: " + p);
    }
    // CollectAssetPaths lists what a build must ship, so it follows the first component of each kind and leaves out
    // fonts. The per-object list that SceneValidator keeps (every component, fonts, voxel terrain, ...) fills the gap
    // and says which object points at the file.
    for (const auto& up : probe.Objects()) {
        if (up->IsDestroyed()) continue;
        for (const testing::AssetRef& ref : testing::SceneValidator::AssetReferences(*up)) {
            if (reported.count(ref.path) || testing::SceneValidator::AssetExists(ref.path, vopt)) continue;
            reported.insert(ref.path);
            add(SceneIssue::Kind::MissingAsset, up->name, "file not found: " + ref.path + " (" + ref.component + "." + ref.field + ")");
        }
    }
    return report;
}

SceneValidationReport SceneSerializer::ValidateFile(const std::string& path, const std::string& assetRoot) {
    std::ifstream f(FilePath(path), std::ios::binary);
    if (!f) {
        SceneValidationReport r;
        r.issues.push_back(SceneIssue{SceneIssue::Kind::LoadError, "", "cannot open " + path});
        return r;
    }
    std::stringstream ss; ss << f.rdbuf();
    bool packOk = true; std::string packError;
    const std::string text = DataPack::Unpack(ss.str(), &packOk, &packError);
    if (!packOk) {   // a damaged pack is not an empty scene: say so (it used to unpack to "" and report a missing header)
        SceneValidationReport r;
        r.issues.push_back(SceneIssue{SceneIssue::Kind::LoadError, "", "tampered or unreadable " + path + " (" + packError + ")"});
        return r;
    }
    return Validate(text, assetRoot);
}

} // namespace okay
