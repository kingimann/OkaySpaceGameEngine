// The component serializer registry's own parts: the generic property-registry path (WritePropertyFields /
// ReadPropertyFields) and the built-in component serializers (RegisterBuiltinComponentSerializers). Moved unchanged
// from SceneSerializer.cpp (docs/engine/modularization_plan.md, section 3.1, S1).
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
using namespace sceneser;

// ---- The generic path: a component saved from its PropertyRegistry entry ------------------------------------
// ComponentSerializerRegistry::RegisterFromProperties (ComponentSerializer.cpp) builds a record from these two. The
// format is the key=value form the newer records already use (see KvOut): `  <record> key=value key="text" ...`.
// Written from docs/engine/properties.md (the value text forms) and the scene format notes in
// docs/engine/scene_serializer.md; no code from elsewhere.
namespace {
// Saved: what a person sets (not read-only, runtime-only, derived or structural). `enabled` is kept by the
// `compoff` record for every component, and an object reference is left out: the object it names may load later.
bool GenericSaved(const PropertyDesc& d) {
    if (d.IsReadOnly() || d.IsRuntimeOnly() || d.IsDerived() || (d.flags & PropertyFlag::Structural) != 0) return false;
    if (d.name == "enabled" || d.type == PropertyType::ObjectRef) return false;
    return d.name.find_first_of("=\"") == std::string::npos;   // the key must be one token
}
// Read: the saved ones, plus `enabled` when a hand-written record carries it.
bool GenericRead(const PropertyDesc& d) {
    return GenericSaved(d) || (d.name == "enabled" && !d.IsReadOnly());
}
} // namespace

void ComponentSerializerRegistry::WritePropertyFields(const Component& c, const ComponentTypeInfo& info, std::ostream& out) {
    for (const PropertyDesc& d : info.properties) {
        if (!GenericSaved(d)) continue;
        const PropertyValue v = d.Get(c);
        if (!v.IsValid()) continue;
        out << ' ' << d.name << '=';
        // Numbers and switches as they are; everything that may hold a space (text, vectors, "1 Point") quoted.
        if (v.Type() == PropertyType::Bool || v.Type() == PropertyType::Int || v.Type() == PropertyType::Float) out << v.ToString();
        else out << Quote(v.ToString());
    }
}

int ComponentSerializerRegistry::ReadPropertyFields(Component& c, const ComponentTypeInfo& info, std::istream& in,
                                                     std::vector<std::string>* notes) {
    auto note = [&](const std::string& n) { if (notes) notes->push_back(n); };
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::istringstream ls(text);
    // key -> value text, by property; a key given twice keeps the last one
    std::unordered_map<const PropertyDesc*, std::string> given;
    while (true) {
        ls >> std::ws;
        if (!ls || ls.peek() == EOF) break;
        std::string key;
        char ch = 0;
        while (ls.get(ch) && ch != '=' && !std::isspace((unsigned char)ch)) key += ch;
        if (ch != '=') {
            if (!key.empty()) note("skipped '" + key + "' (no '=')");
            continue;
        }
        std::string value;
        if (ls.peek() == '"') value = ReadQuoted(ls);
        else ls >> value;
        const PropertyDesc* d = info.Find(key);
        if (!d || !GenericRead(*d)) { note("skipped unknown field '" + key + "'"); continue; }
        given[d] = value;
    }
    // Apply in the entry's order, whatever order the file had, so a setter that looks at another field sees the
    // same thing every time.
    int set = 0;
    for (const PropertyDesc& d : info.properties) {
        auto it = given.find(&d);
        if (it == given.end()) continue;
        PropertyValue v;
        if (!d.ParseText(it->second, v) || !d.Set(c, v)) {
            note("kept the default of '" + d.name + "': could not use '" + it->second + "'");
            continue;
        }
        ++set;
    }
    return set;
}

// ---- Components that use the registry ------------------------------------
namespace detail {
void RegisterBuiltinComponentSerializers() {
    RegisterPhysicsToySerializers();
    RegisterColliderShape2DSerializers(); // Composite 2D collider
    RegisterColliderShapeSerializers();   // Cone, Convex Hull, Plane, Compound, Heightfield colliders
    ComponentSerializerRegistry::Entry trail;
    trail.name = "trailrenderer";
    trail.write = [](const GameObject& go, std::ostream& out) {
        for (auto* tr : go.GetComponents<TrailRenderer>()) {
        out << "  trailrenderer";
        KvOut k{out};
        k("time", tr->time)("mindist", tr->minVertexDistance)("width", tr->width).col("c", tr->color)
         .b("emit", tr->emitting)("align", (int)tr->alignment)("texmode", (int)tr->textureMode)
         ("tile", tr->tileLength).str("tex", tr->texture)("teleport", tr->teleportDistance);
        WriteCurveKv(k, "w", tr->widthCurve);
        WriteGradientKv(k, "g", tr->colorGradient);
        out << "\n";
        }
    };
    trail.read = [](GameObject& go, std::istream& in) {
        auto* tr = go.AddComponent<TrailRenderer>();
        KvIn k(in);
        int align = 0, texmode = 0;
        k.f("time", tr->time); k.f("mindist", tr->minVertexDistance); k.f("width", tr->width);
        k.col("c", tr->color); k.b("emit", tr->emitting); k.i("align", align); k.i("texmode", texmode);
        tr->alignment = align == 1 ? LineAlignment::Flat : LineAlignment::View;
        tr->textureMode = texmode == 1 ? LineTextureMode::Tile : LineTextureMode::Stretch;
        k.f("tile", tr->tileLength); k.str("tex", tr->texture); k.f("teleport", tr->teleportDistance);
        ReadCurveKv(k, "w", tr->widthCurve);
        ReadGradientKv(k, "g", tr->colorGradient);
    };
    ComponentSerializerRegistry::Register(std::move(trail));

    // Water waves beyond the old sines (style, direction, steepness, count, seed, current), kept as their own record
    // so older scene files and older readers are untouched.
    ComponentSerializerRegistry::Entry waterwaves;
    waterwaves.name = "waterwaves";
    waterwaves.write = [](const GameObject& go, std::ostream& out) {
        for (auto* w : go.GetComponents<Water>()) {
            const Water d;   // a fresh one: write the record only when something differs from it
            if (w->style == d.style && w->waveDirection == d.waveDirection && w->waveSteepness == d.waveSteepness &&
                w->waveCount == d.waveCount && w->waveSeed == d.waveSeed && w->current.x == d.current.x && w->current.y == d.current.y) continue;
            out << "  waterwaves";
            KvOut k{out};
            k("style", (int)w->style)("dir", w->waveDirection)("steep", w->waveSteepness)("count", w->waveCount)
             ("seed", w->waveSeed)("curx", w->current.x)("cury", w->current.y);
            out << "\n";
        }
    };
    waterwaves.read = [](GameObject& go, std::istream& in) {
        auto ws = go.GetComponents<Water>();
        Water* w = ws.empty() ? nullptr : ws.back();
        KvIn k(in);
        if (!w) return;
        int style = 0;
        k.i("style", style); k.f("dir", w->waveDirection); k.f("steep", w->waveSteepness); k.i("count", w->waveCount);
        k.i("seed", w->waveSeed); k.f("curx", w->current.x); k.f("cury", w->current.y);
        w->style = style == 1 ? Water::WaveStyle::Ocean : style == 2 ? Water::WaveStyle::Calm : Water::WaveStyle::Classic;
        if (!std::isfinite(w->waveDirection)) w->waveDirection = 0.0f;
        if (!std::isfinite(w->waveSteepness)) w->waveSteepness = 0.5f;
        w->waveSteepness = std::min(1.0f, std::max(0.0f, w->waveSteepness));
        w->waveCount = std::min(8, std::max(1, w->waveCount));
        if (!std::isfinite(w->current.x) || !std::isfinite(w->current.y)) w->current = Vec2{0.0f, 0.0f};
    };
    ComponentSerializerRegistry::Register(std::move(waterwaves));

    // How the water looks (sea state, depth colours, foam, ripples, rings, splashes, underwater). A scene without this record
    // loads with every look setting off, so old scenes look as they did. docs/rendering/terrain_water.md.
    ComponentSerializerRegistry::Entry watervis;
    watervis.name = "watervis";
    watervis.write = [](const GameObject& go, std::ostream& out) {
        for (auto* w : go.GetComponents<Water>()) {
            const Water def;
            auto sameCol = [](const Color& x, const Color& y) { return x.r == y.r && x.g == y.g && x.b == y.b && x.a == y.a; };
            if (!w->realisticLook && !w->lod && !w->interactive && !w->underwaterEffect && !w->bubbles && w->seaState == 0 &&
                w->clarity == def.clarity && w->assumedDepth == def.assumedDepth && w->foamAmount == def.foamAmount &&
                w->shoreFoamWidth == def.shoreFoamWidth && w->scatter == def.scatter && w->ripples == def.ripples &&
                w->rippleSize == def.rippleSize && w->lodRings == def.lodRings && w->infinite == def.infinite &&
                sameCol(w->shallowColor, def.shallowColor) && sameCol(w->deepColor, def.deepColor) && sameCol(w->foamColor, def.foamColor)) continue;
            out << "  watervis";
            KvOut k{out};
            k("sea", w->seaState).b("rich", w->realisticLook).col("sh", w->shallowColor).col("dp", w->deepColor)
             ("clar", w->clarity)("adep", w->assumedDepth).col("fm", w->foamColor)
             ("foam", w->foamAmount)("shore", w->shoreFoamWidth)
             ("scat", w->scatter)("rip", w->ripples)("ripsz", w->rippleSize).b("lod", w->lod)("rings", w->lodRings)
             .b("inf", w->infinite).b("inter", w->interactive).b("under", w->underwaterEffect).b("bub", w->bubbles);
            out << "\n";
        }
    };
    watervis.read = [](GameObject& go, std::istream& in) {
        auto ws = go.GetComponents<Water>();
        Water* w = ws.empty() ? nullptr : ws.back();
        KvIn k(in);
        if (!w) return;
        auto fin = [](float& v, float lo, float hi, float fallback) { if (!std::isfinite(v)) v = fallback; v = std::min(hi, std::max(lo, v)); };
        k.i("sea", w->seaState); k.b("rich", w->realisticLook);
        k.col("sh", w->shallowColor); k.col("dp", w->deepColor); k.col("fm", w->foamColor);
        k.f("clar", w->clarity); k.f("adep", w->assumedDepth); k.f("foam", w->foamAmount); k.f("shore", w->shoreFoamWidth);
        k.f("scat", w->scatter); k.f("rip", w->ripples); k.f("ripsz", w->rippleSize);
        k.b("lod", w->lod); k.i("rings", w->lodRings); k.b("inf", w->infinite); k.b("inter", w->interactive);
        k.b("under", w->underwaterEffect); k.b("bub", w->bubbles);
        w->seaState = std::min(4, std::max(0, w->seaState));
        w->lodRings = std::min(8, std::max(1, w->lodRings));
        fin(w->clarity, 0.05f, 200.0f, 6.0f); fin(w->assumedDepth, 0.0f, 10000.0f, 12.0f); fin(w->foamAmount, 0.0f, 1.0f, 0.5f);
        fin(w->shoreFoamWidth, 0.0f, 50.0f, 1.2f); fin(w->scatter, 0.0f, 1.0f, 0.4f); fin(w->ripples, 0.0f, 1.0f, 0.0f);
        fin(w->rippleSize, 0.05f, 20.0f, 1.0f);
        for (float* c : {&w->shallowColor.r, &w->shallowColor.g, &w->shallowColor.b, &w->shallowColor.a, &w->deepColor.r, &w->deepColor.g,
                         &w->deepColor.b, &w->deepColor.a, &w->foamColor.r, &w->foamColor.g, &w->foamColor.b, &w->foamColor.a}) fin(*c, 0.0f, 1000.0f, 0.5f);
        if (w->realisticLook || w->lod) w->Apply();     // the look decides the material colour, normal map and mesh
    };
    ComponentSerializerRegistry::Register(std::move(watervis));

    ComponentSerializerRegistry::Entry buoy;
    buoy.name = "buoyancy";
    buoy.write = [](const GameObject& go, std::ostream& out) {
        for (auto* b : go.GetComponents<Buoyancy>()) {
            if (b->autoAdded) continue;   // made by a Water at Play for a plain prop
            out << "  buoyancy";
            KvOut k{out};
            k("gridX", b->gridX)("gridZ", b->gridZ)("volume", b->volume)("fullness", b->fullness)("hullH", b->hullHeight)
             ("density", b->density)("dfw", b->dragForward)("dfw2", b->dragForward2)("dsd", b->dragSide)("dsd2", b->dragSide2)
             ("dup", b->dragUp)("dup2", b->dragUp2)("angd", b->angularDamping)("maxpush", b->maxPushAcceleration)
             .b("tilt", b->letTilt).b("splash", b->splashEffect)("splashmin", b->splashMinSpeed)
             .str("points", b->PointsText());
            out << "\n";
        }
    };
    buoy.read = [](GameObject& go, std::istream& in) {
        auto* b = go.AddComponent<Buoyancy>();
        KvIn k(in);
        k.i("gridX", b->gridX); k.i("gridZ", b->gridZ); k.f("volume", b->volume); k.f("fullness", b->fullness);
        k.f("hullH", b->hullHeight); k.f("density", b->density);
        k.f("dfw", b->dragForward); k.f("dfw2", b->dragForward2); k.f("dsd", b->dragSide); k.f("dsd2", b->dragSide2);
        k.f("dup", b->dragUp); k.f("dup2", b->dragUp2); k.f("angd", b->angularDamping); k.f("maxpush", b->maxPushAcceleration);
        k.b("tilt", b->letTilt); k.b("splash", b->splashEffect); k.f("splashmin", b->splashMinSpeed);
        std::string pts; k.str("points", pts); b->SetPointsText(pts);
        b->gridX = std::min(16, std::max(1, b->gridX)); b->gridZ = std::min(16, std::max(1, b->gridZ));
    };
    ComponentSerializerRegistry::Register(std::move(buoy));

    ComponentSerializerRegistry::Entry boat;
    boat.name = "boat";
    boat.write = [](const GameObject& go, std::ostream& out) {
        for (auto* b : go.GetComponents<BoatController>()) {
            out << "  boat";
            KvOut k{out};
            k.b("player", b->isPlayer)("maxSpeed", b->maxSpeed)("revSpeed", b->reverseSpeed)("power", b->enginePower)
             ("brake", b->brakePower)("turn", b->turnRate)("steerResp", b->steerResponse)
             .b("wake", b->wake).b("bow", b->bowSplash).b("sound", b->engineSound).b("cam", b->followCamera)
             ("camDist", b->camDistance)("camH", b->camHeight)("camLerp", b->camLerp);
            out << "\n";
        }
    };
    boat.read = [](GameObject& go, std::istream& in) {
        auto* b = go.AddComponent<BoatController>();
        KvIn k(in);
        k.b("player", b->isPlayer); k.f("maxSpeed", b->maxSpeed); k.f("revSpeed", b->reverseSpeed); k.f("power", b->enginePower);
        k.f("brake", b->brakePower); k.f("turn", b->turnRate); k.f("steerResp", b->steerResponse);
        k.b("wake", b->wake); k.b("bow", b->bowSplash); k.b("sound", b->engineSound); k.b("cam", b->followCamera);
        k.f("camDist", b->camDistance); k.f("camH", b->camHeight); k.f("camLerp", b->camLerp);
    };
    ComponentSerializerRegistry::Register(std::move(boat));

    ComponentSerializerRegistry::Entry lod;
    lod.name = "lodgroup";
    lod.write = [](const GameObject& go, std::ostream& out) {
        for (auto* lg : go.GetComponents<LODGroup>()) {
        if (!lg || lg->levels.empty()) continue;
        out << "  lodgroup";
        KvOut k{out};
        k.b("screen", lg->useScreenSize)("hyst", lg->hysteresis)("cullD", lg->cullDistance)
         ("cullS", lg->cullScreen)("force", lg->forceLevel).b("tint", lg->debugTint)
         ("n", (int)lg->levels.size());
        const int n = std::min((int)lg->levels.size(), LODGroup::kMaxLevels);
        for (int i = 0; i < n; ++i) {
            const auto& L = lg->levels[(std::size_t)i];
            k(("d" + std::to_string(i)).c_str(), L.distance);
            k(("s" + std::to_string(i)).c_str(), L.screenSize);
            k(("r" + std::to_string(i)).c_str(), L.ratio);
        }
        out << "\n";
        }
    };
    lod.read = [](GameObject& go, std::istream& in) {
        auto* lg = go.AddComponent<LODGroup>();
        KvIn k(in);
        int n = 0, force = -1;
        k.b("screen", lg->useScreenSize); k.f("hyst", lg->hysteresis);
        k.f("cullD", lg->cullDistance); k.f("cullS", lg->cullScreen);
        k.i("force", force); lg->forceLevel = force;
        k.b("tint", lg->debugTint); k.i("n", n);
        if (n < 1) n = 1;
        if (n > LODGroup::kMaxLevels) n = LODGroup::kMaxLevels;
        lg->levels.assign((std::size_t)n, {});
        for (int i = 0; i < n; ++i) {
            k.f(("d" + std::to_string(i)).c_str(), lg->levels[(std::size_t)i].distance);
            k.f(("s" + std::to_string(i)).c_str(), lg->levels[(std::size_t)i].screenSize);
            k.f(("r" + std::to_string(i)).c_str(), lg->levels[(std::size_t)i].ratio);
        }
        // Only the first group rebuilds the shared mesh. A second group keeps its own
        // ratios; rebuilding again would replace the mesh the first group just made.
        if (lg == go.GetComponent<LODGroup>())
            if (auto* mr = go.GetComponent<MeshRenderer>(); mr && !mr->mesh.vertices.empty() && n > 1)
                lg->RebuildFromRatios(mr->DisplayMesh());
    };
    ComponentSerializerRegistry::Register(std::move(lod));

    ComponentSerializerRegistry::Entry debris;
    debris.name = "debrisemitter";
    debris.write = [](const GameObject& go, std::ostream& out) {
        for (auto* de : go.GetComponents<DebrisEmitter>()) {
            out << "  debrisemitter";
            KvOut k{out};
            k("count", de->count)("interval", de->interval)("delay", de->startDelay)("smin", de->sizeMin)("smax", de->sizeMax)
             ("speed", de->speed)("spread", de->spread).col("c", de->color)("max", de->maxActive)("life", de->lifetime)
             ("settled", de->settledLife).b("mutual", de->collideWithEachOther);
            out << "\n";
        }
    };
    debris.read = [](GameObject& go, std::istream& in) {
        auto* de = go.AddComponent<DebrisEmitter>();
        KvIn k(in);
        k.i("count", de->count); k.f("interval", de->interval); k.f("delay", de->startDelay); k.f("smin", de->sizeMin);
        k.f("smax", de->sizeMax); k.f("speed", de->speed); k.f("spread", de->spread); k.col("c", de->color);
        k.i("max", de->maxActive); k.f("life", de->lifetime); k.f("settled", de->settledLife); k.b("mutual", de->collideWithEachOther);
    };
    ComponentSerializerRegistry::Register(std::move(debris));

    ComponentSerializerRegistry::Entry pin;
    pin.name = "playerinput";
    pin.write = [](const GameObject& go, std::ostream& out) {
        for (const LocalPlayerInput* pi : go.GetComponents<LocalPlayerInput>()) {
            if (!pi) continue;
            out << "  playerinput";
            KvOut k{out};
            k.str("actions", pi->actions)("device", (int)pi->device)("pad", pi->controller).str("scheme", pi->scheme).b("auto", pi->autoSwitch).str("maps", pi->startMaps);
            out << "\n";
        }
    };
    pin.read = [](GameObject& go, std::istream& in) {
        auto* pi = go.AddComponent<LocalPlayerInput>();
        KvIn k(in);
        int dev = (int)pi->device;
        k.str("actions", pi->actions); k.i("device", dev); k.i("pad", pi->controller); k.str("scheme", pi->scheme); k.b("auto", pi->autoSwitch); k.str("maps", pi->startMaps);
        pi->device = dev < 0 || dev > 2 ? LocalPlayerInput::Device::Any : (LocalPlayerInput::Device)dev;
    };
    ComponentSerializerRegistry::Register(std::move(pin));

    ComponentSerializerRegistry::Entry vol;
    vol.name = "volume";
    vol.write = [](const GameObject& go, std::ostream& out) {
        for (const Volume* v : go.GetComponents<Volume>()) {
            if (!v) continue;
            out << "  volume";
            KvOut k{out};
            k("kind", (int)v->kind)("shape", (int)v->shape)
             ("sx", v->size.x)("sy", v->size.y)("sz", v->size.z)
             ("ox", v->offset.x)("oy", v->offset.y)("oz", v->offset.z)
             .str("tag", v->activatorTag).str("event", v->eventName)
             ("kact", v->killAction)("kdmg", v->killDamage)
             ("fshape", v->forceShape)("fx", v->forceDirection.x)("fy", v->forceDirection.y)("fz", v->forceDirection.z)
             .b("flocal", v->forceLocal)("fstr", v->forceStrength)
             .col("fog", v->fogColor)("fstart", v->fogStart)("fend", v->fogEnd)("vig", v->vignette)("blend", v->blend)
             ("fade", v->fadeTime)("rpre", v->reverbPreset)("ramt", v->reverbAmount);
            out << "\n";
        }
    };
    vol.read = [](GameObject& go, std::istream& in) {
        auto* v = go.AddComponent<Volume>();
        KvIn k(in);
        int kind = 0, shape = 0;
        k.i("kind", kind); k.i("shape", shape);
        v->kind = (Volume::Kind)std::clamp(kind, 0, Volume::kKindCount - 1);
        v->shape = (VolumeShape)std::clamp(shape, 0, kVolumeShapeCount - 1);
        k.f("sx", v->size.x); k.f("sy", v->size.y); k.f("sz", v->size.z);
        k.f("ox", v->offset.x); k.f("oy", v->offset.y); k.f("oz", v->offset.z);
        k.str("tag", v->activatorTag); k.str("event", v->eventName);
        k.i("kact", v->killAction); k.f("kdmg", v->killDamage);
        k.i("fshape", v->forceShape); k.f("fx", v->forceDirection.x); k.f("fy", v->forceDirection.y); k.f("fz", v->forceDirection.z);
        k.b("flocal", v->forceLocal); k.f("fstr", v->forceStrength);
        k.col("fog", v->fogColor); k.f("fstart", v->fogStart); k.f("fend", v->fogEnd); k.f("vig", v->vignette); k.f("blend", v->blend);
        k.f("fade", v->fadeTime); k.i("rpre", v->reverbPreset); k.f("ramt", v->reverbAmount);
        if (v->reverbPreset < 0 || v->reverbPreset > 3) v->reverbPreset = 0;
        if (!(v->reverbAmount >= 0.0f)) v->reverbAmount = 0.0f;
        if (v->reverbAmount > 1.0f) v->reverbAmount = 1.0f;
    };
    ComponentSerializerRegistry::Register(std::move(vol));

    // Wind (docs/gameplay/wind.md): the zone and the three things that listen to it.
    ComponentSerializerRegistry::Entry wz;
    wz.name = "windzone";
    wz.write = [](const GameObject& go, std::ostream& out) {
        for (const WindZone* w : go.GetComponents<WindZone>()) {
            if (!w) continue;
            out << "  windzone";
            KvOut k{out};
            k("mode", (int)w->mode)("shape", w->shape)
             ("sx", w->size.x)("sy", w->size.y)("sz", w->size.z)("ox", w->offset.x)("oy", w->offset.y)("oz", w->offset.z)
             ("edge", w->edge)("falloff", w->falloff)
             ("dx", w->direction.x)("dy", w->direction.y)("dz", w->direction.z)("str", w->strength)
             ("gust", w->gustAmount)("gfreq", w->gustFrequency)("gspeed", w->gustSpeed)
             ("turb", w->turbulence)("tscale", w->turbulenceScale)("seed", w->seed)
             .str("curve", w->curveKeys)("clen", w->curveLength)("prio", w->priority).b("add", w->additive);
            out << "\n";
        }
    };
    wz.read = [](GameObject& go, std::istream& in) {
        auto* w = go.AddComponent<WindZone>();
        KvIn k(in);
        int mode = (int)w->mode;
        k.i("mode", mode); k.i("shape", w->shape);
        w->mode = (WindZone::Mode)std::clamp(mode, 0, 2);
        w->shape = std::clamp(w->shape, 0, 2);
        k.f("sx", w->size.x); k.f("sy", w->size.y); k.f("sz", w->size.z);
        k.f("ox", w->offset.x); k.f("oy", w->offset.y); k.f("oz", w->offset.z);
        k.f("edge", w->edge); k.f("falloff", w->falloff);
        k.f("dx", w->direction.x); k.f("dy", w->direction.y); k.f("dz", w->direction.z); k.f("str", w->strength);
        k.f("gust", w->gustAmount); k.f("gfreq", w->gustFrequency); k.f("gspeed", w->gustSpeed);
        k.f("turb", w->turbulence); k.f("tscale", w->turbulenceScale); k.i("seed", w->seed);
        k.str("curve", w->curveKeys); k.f("clen", w->curveLength); k.i("prio", w->priority); k.b("add", w->additive);
    };
    ComponentSerializerRegistry::Register(std::move(wz));

    ComponentSerializerRegistry::Entry wd;
    wd.name = "winddrag";
    wd.write = [](const GameObject& go, std::ostream& out) {
        for (const WindDrag* w : go.GetComponents<WindDrag>()) {
            if (!w) continue;
            out << "  winddrag";
            KvOut{out}("area", w->area)("cd", w->dragCoefficient)("str", w->strength).b("kids", w->includeChildren);
            out << "\n";
        }
    };
    wd.read = [](GameObject& go, std::istream& in) {
        auto* w = go.AddComponent<WindDrag>();
        KvIn k(in);
        k.f("area", w->area); k.f("cd", w->dragCoefficient); k.f("str", w->strength); k.b("kids", w->includeChildren);
    };
    ComponentSerializerRegistry::Register(std::move(wd));

    ComponentSerializerRegistry::Entry wsw;
    wsw.name = "windsway";
    wsw.write = [](const GameObject& go, std::ostream& out) {
        for (const WindSway* w : go.GetComponents<WindSway>()) {
            if (!w || w->weight == WindSway::Weight::Custom) continue;   // Custom weights belong to a Foliage chunk, which is never saved
            out << "  windsway";
            KvOut{out}("weight", (int)w->weight)("amount", w->amount)("height", w->height)("stiff", w->stiffness)
                ("flutter", w->flutter)("fspeed", w->flutterSpeed)("rate", w->updateRate)("dist", w->maxDistance).b("turn", w->turnToWind);
            out << "\n";
        }
    };
    wsw.read = [](GameObject& go, std::istream& in) {
        auto* w = go.AddComponent<WindSway>();
        KvIn k(in);
        int wt = (int)w->weight;
        k.i("weight", wt); w->weight = (WindSway::Weight)std::clamp(wt, 0, 1);
        k.f("amount", w->amount); k.f("height", w->height); k.f("stiff", w->stiffness);
        k.f("flutter", w->flutter); k.f("fspeed", w->flutterSpeed); k.f("rate", w->updateRate); k.f("dist", w->maxDistance); k.b("turn", w->turnToWind);
    };
    ComponentSerializerRegistry::Register(std::move(wsw));

    // Hands on the ledge while hanging (LedgeHandsIK). Each arm is the GUID of the object holding that LimbIK
    // and which LimbIK on it; the component turns the link back into the arm at Start().
    ComponentSerializerRegistry::Entry lhik;
    lhik.name = "ledgehandsik";
    lhik.write = [](const GameObject& go, std::ostream& out) {
        for (const LedgeHandsIK* h : go.GetComponents<LedgeHandsIK>()) {
            out << "  ledgehandsik";
            KvOut k{out};
            k("setback", h->setback)("spread", h->handSpread)("over", h->overEdge)("fade", h->fadeTime).b("auto", h->autoWire);
            const LedgeHandsIK::ArmLink l = h->LinkFor(0), r = h->LinkFor(1);
            if (!l.object.empty()) k.str("left", l.object)("lefti", l.index);
            if (!r.object.empty()) k.str("right", r.object)("righti", r.index);
            out << "\n";
        }
    };
    lhik.read = [](GameObject& go, std::istream& in) {
        auto* h = go.AddComponent<LedgeHandsIK>();
        KvIn k(in);
        k.f("setback", h->setback); k.f("spread", h->handSpread); k.f("over", h->overEdge); k.f("fade", h->fadeTime);
        k.b("auto", h->autoWire);
        auto fin = [](float& v, float d) { if (!std::isfinite(v) || v < 0.0f) v = d; };
        fin(h->setback, 0.25f); fin(h->handSpread, 0.22f); fin(h->overEdge, 0.05f); fin(h->fadeTime, 0.2f);
        k.str("left", h->leftArmLink.object); k.i("lefti", h->leftArmLink.index);
        k.str("right", h->rightArmLink.object); k.i("righti", h->rightArmLink.index);
    };
    ComponentSerializerRegistry::Register(std::move(lhik));

    ComponentSerializerRegistry::Entry wam;
    wam.name = "windambience";
    wam.write = [](const GameObject& go, std::ostream& out) {
        for (const WindAmbience* w : go.GetComponents<WindAmbience>()) {
            if (!w) continue;
            out << "  windambience";
            KvOut{out}("vol", w->volume)("full", w->fullAt)("quiet", w->quietBelow)("smooth", w->smoothing);
            out << "\n";
        }
    };
    wam.read = [](GameObject& go, std::istream& in) {
        auto* w = go.AddComponent<WindAmbience>();
        KvIn k(in);
        k.f("vol", w->volume); k.f("full", w->fullAt); k.f("quiet", w->quietBelow); k.f("smooth", w->smoothing);
    };
    ComponentSerializerRegistry::Register(std::move(wam));

    ComponentSerializerRegistry::Entry occ;
    occ.name = "occluder";
    occ.write = [](const GameObject& go, std::ostream& out) {
        for (auto* o : go.GetComponents<Occluder>()) {
        out << "  occluder";
        KvOut{out}.b("mesh", o->useMesh);
        out << "\n";
        }
    };
    occ.read = [](GameObject& go, std::istream& in) {
        auto* o = go.AddComponent<Occluder>();
        KvIn k(in);
        k.b("mesh", o->useMesh);
    };
    ComponentSerializerRegistry::Register(std::move(occ));
    // Terrain texture layers: the layer list and the painted weights (run-length text). Written after the
    // `terrain` record so the Terrain exists when this is read.
    ComponentSerializerRegistry::Entry tl;
    tl.name = "terrainlayers";
    tl.write = [](const GameObject& go, std::ostream& out) {
        const auto terrains = go.GetComponents<Terrain>();
        for (int which = 0; which < (int)terrains.size(); ++which) {
        auto* t = terrains[(std::size_t)which];
        if (!t || t->layers.empty()) continue;
        out << "  terrainlayers";
        KvOut k{out};
        if (which > 0) k("i", which);   // the first terrain omits it: an older file has no index
        k("n", (int)t->layers.size())("res", t->splatResolution);
        for (std::size_t i = 0; i < t->layers.size(); ++i) {
            const std::string p = "l" + std::to_string(i);
            const TerrainLayer& l = t->layers[i];
            k.str((p + "n").c_str(), l.name).str((p + "t").c_str(), l.texture).col((p + "c").c_str(), l.tint)((p + "s").c_str(), l.tiling);
        }
        k.str("w", t->EncodeSplat());
        out << "\n";
        }
    };
    tl.read = [](GameObject& go, std::istream& in) {
        KvIn k(in);
        int which = 0;
        k.i("i", which);
        const auto terrains = go.GetComponents<Terrain>();
        auto* t = (which >= 0 && which < (int)terrains.size()) ? terrains[(std::size_t)which] : nullptr;
        if (!t) return;
        int n = 0;
        k.i("n", n); k.i("res", t->splatResolution);
        t->splatResolution = std::min(4096, std::max(64, t->splatResolution));
        n = std::min(n, Terrain::kMaxLayers);
        t->layers.clear();
        for (int i = 0; i < n; ++i) {
            const std::string p = "l" + std::to_string(i);
            TerrainLayer l;
            k.str((p + "n").c_str(), l.name); k.str((p + "t").c_str(), l.texture);
            k.col((p + "c").c_str(), l.tint); k.f((p + "s").c_str(), l.tiling);
            t->layers.push_back(l);
        }
        t->ResetSplatPublic();
        std::string w;
        k.str("w", w);
        if (!w.empty()) t->DecodeSplat(w);   // a damaged string keeps the base-only weights
        t->Apply();
    };
    ComponentSerializerRegistry::Register(std::move(tl));

    ComponentSerializerRegistry::Entry fol;
    fol.name = "foliage";
    fol.write = [](const GameObject& go, std::ostream& out) {
        for (auto* f : go.GetComponents<Foliage>()) {
        out << "  foliage";
        KvOut k{out};
        k.str("mesh", f->mesh).str("path", f->meshPath).str("prefab", f->prefabPath).col("t", f->tint)("var", f->variants)
         ("r", f->radius)("dens", f->density)("smin", f->minScale)("smax", f->maxScale)("slope", f->maxSlope)
         ("hmin", f->minHeight)("hmax", f->maxHeight).b("yaw", f->randomYaw)("align", f->alignToSurface)
         ("gap", f->spacing)("seed", f->seed)("tris", f->maxTriangles)("chunk", f->chunkSize).b("shadow", f->castShadows)("wind", f->windSway);
        std::string s;
        char buf[160];
        for (const auto& i : f->instances) {
            std::snprintf(buf, sizeof(buf), "%.2f,%.2f,%.2f,%.0f,%.2f,%d,%.3f,%.3f;", i.position.x, i.position.y, i.position.z,
                          i.yaw, i.scale, i.variant, i.leanX, i.leanZ);
            s += buf;
        }
        k.str("inst", s);
        out << "\n";
        }
    };
    fol.read = [](GameObject& go, std::istream& in) {
        auto* f = go.AddComponent<Foliage>();
        KvIn k(in);
        k.str("mesh", f->mesh); k.str("path", f->meshPath); k.str("prefab", f->prefabPath); k.col("t", f->tint);
        k.i("var", f->variants); k.f("r", f->radius); k.f("dens", f->density); k.f("smin", f->minScale); k.f("smax", f->maxScale);
        k.f("slope", f->maxSlope); k.f("hmin", f->minHeight); k.f("hmax", f->maxHeight); k.b("yaw", f->randomYaw);
        k.f("align", f->alignToSurface); k.f("gap", f->spacing); k.u("seed", f->seed); k.i("tris", f->maxTriangles);
        k.f("chunk", f->chunkSize); k.b("shadow", f->castShadows); k.f("wind", f->windSway);
        std::string s;
        k.str("inst", s);
        const char* c = s.c_str();
        while (*c) {
            FoliageInstance i;
            char* e = nullptr;
            i.position.x = std::strtof(c, &e); if (*e != ',') break; c = e + 1;
            i.position.y = std::strtof(c, &e); if (*e != ',') break; c = e + 1;
            i.position.z = std::strtof(c, &e); if (*e != ',') break; c = e + 1;
            i.yaw = std::strtof(c, &e); if (*e != ',') break; c = e + 1;
            i.scale = std::strtof(c, &e); if (*e != ',') break; c = e + 1;
            i.variant = (int)std::strtol(c, &e, 10); if (*e != ',') break; c = e + 1;
            i.leanX = std::strtof(c, &e); if (*e != ',') break; c = e + 1;
            i.leanZ = std::strtof(c, &e); if (*e != ';') break; c = e + 1;
            if (i.variant < 0) i.variant = 0;
            f->instances.push_back(i);
            if (f->instances.size() > 2000000) break;
        }
    };
    ComponentSerializerRegistry::Register(std::move(fol));
    ComponentSerializerRegistry::Entry cab;
    cab.name = "cable";
    cab.write = [](const GameObject& go, std::ostream& out) {
        for (auto* c : go.GetComponents<Cable>()) {
        out << "  cable";
        KvOut{out}.str("from", c->startObject).str("to", c->endObject)("sx", c->startOffset.x)("sy", c->startOffset.y)
            ("sz", c->startOffset.z)("ex", c->endOffset.x)("ey", c->endOffset.y)("ez", c->endOffset.z)("sag", c->sag)
            ("seg", c->segments)("w", c->width).col("c", c->color)("swing", c->windSwing);
        out << "\n";
        }
    };
    cab.read = [](GameObject& go, std::istream& in) {
        auto* c = go.AddComponent<Cable>();
        KvIn k(in);
        k.str("from", c->startObject); k.str("to", c->endObject);
        k.f("sx", c->startOffset.x); k.f("sy", c->startOffset.y); k.f("sz", c->startOffset.z);
        k.f("ex", c->endOffset.x); k.f("ey", c->endOffset.y); k.f("ez", c->endOffset.z);
        k.f("sag", c->sag); k.i("seg", c->segments); k.f("w", c->width); k.col("c", c->color); k.f("swing", c->windSwing);
    };
    ComponentSerializerRegistry::Register(std::move(cab));

    ComponentSerializerRegistry::Entry dec;
    dec.name = "decal";
    dec.write = [](const GameObject& go, std::ostream& out) {
        for (auto* d : go.GetComponents<Decal>()) {
        out << "  decal";
        KvOut{out}.str("tex", d->texture).col("t", d->tint)("x", d->size.x)("y", d->size.y)("z", d->size.z)
            ("angle", d->maxAngle)("lift", d->lift);
        out << "\n";
        }
    };
    dec.read = [](GameObject& go, std::istream& in) {
        auto* d = go.AddComponent<Decal>();
        KvIn k(in);
        k.str("tex", d->texture); k.col("t", d->tint); k.f("x", d->size.x); k.f("y", d->size.y); k.f("z", d->size.z);
        k.f("angle", d->maxAngle); k.f("lift", d->lift);
    };
    ComponentSerializerRegistry::Register(std::move(dec));

    ComponentSerializerRegistry::Entry gridAgent;
    gridAgent.name = "gridpath2d";
    gridAgent.write = [](const GameObject& go, std::ostream& out) {
        for (const GridPathAgent2D* a : go.GetComponents<GridPathAgent2D>()) {
            if (!a) continue;
            out << "  gridpath2d";
            KvOut k{out};
            k.str("map", a->mapName)("speed", a->speed)("stop", a->stopDistance)
             .b("diag", a->allowDiagonal).b("smooth", a->smoothPath).b("costs", a->useCosts)
             ("repath", a->repathInterval);
            out << "\n";
        }
    };
    gridAgent.read = [](GameObject& go, std::istream& in) {
        auto* a = go.AddComponent<GridPathAgent2D>();
        KvIn k(in);
        k.str("map", a->mapName); k.f("speed", a->speed); k.f("stop", a->stopDistance);
        k.b("diag", a->allowDiagonal); k.b("smooth", a->smoothPath); k.b("costs", a->useCosts);
        k.f("repath", a->repathInterval);
    };
    ComponentSerializerRegistry::Register(std::move(gridAgent));

    ComponentSerializerRegistry::Entry gridCost;
    gridCost.name = "gridcost2d";
    gridCost.write = [](const GameObject& go, std::ostream& out) {
        for (const GridCostLayer* c : go.GetComponents<GridCostLayer>()) {
            if (!c) continue;
            out << "  gridcost2d";
            KvOut k{out};
            k("road", c->roadTile)("roadc", c->roadCost)("mud", c->mudTile)("mudc", c->mudCost)
             .str("extra", c->ExtraTiles()).str("cells", c->CellText());
            out << "\n";
        }
    };
    gridCost.read = [](GameObject& go, std::istream& in) {
        auto* c = go.AddComponent<GridCostLayer>();
        KvIn k(in);
        k.i("road", c->roadTile); k.f("roadc", c->roadCost);
        k.i("mud", c->mudTile); k.f("mudc", c->mudCost);
        std::string extra, cells;
        k.str("extra", extra); k.str("cells", cells);
        c->SetExtraTiles(extra);
        c->SetCellText(cells);
    };
    ComponentSerializerRegistry::Register(std::move(gridCost));

    ComponentSerializerRegistry::Entry gridBlock;
    gridBlock.name = "gridblock2d";
    gridBlock.write = [](const GameObject& go, std::ostream& out) {
        for (const GridObstacle2D* o : go.GetComponents<GridObstacle2D>()) {
            if (!o) continue;
            out << "  gridblock2d";
            KvOut k{out};
            k.str("map", o->mapName)("wide", o->cellsWide)("tall", o->cellsTall).b("block", o->blocks);
            out << "\n";
        }
    };
    gridBlock.read = [](GameObject& go, std::istream& in) {
        auto* o = go.AddComponent<GridObstacle2D>();
        KvIn k(in);
        k.str("map", o->mapName); k.i("wide", o->cellsWide); k.i("tall", o->cellsTall);
        k.b("block", o->blocks);
        if (o->cellsWide < 1) o->cellsWide = 1;
        if (o->cellsTall < 1) o->cellsTall = 1;
    };
    ComponentSerializerRegistry::Register(std::move(gridBlock));

    ComponentSerializerRegistry::Entry stick;
    stick.name = "virtualjoystick";
    stick.write = [](const GameObject& go, std::ostream& out) {
        auto* j = go.GetComponent<VirtualJoystick>();
        if (!j) return;
        out << "  virtualjoystick";
        KvOut{out}("x", j->position.x)("y", j->position.y)("w", j->size.x)("h", j->size.y)
            ("anchor", (int)j->anchor).str("action", j->action).b("floating", j->floating)
            ("dead", j->deadZone).b("touchOnly", j->onlyOnTouch).b("fade", j->fadeWhenIdle)
            ("knob", j->knobScale).col("base", j->baseColor).col("cap", j->knobColor);
        out << "\n";
    };
    stick.read = [](GameObject& go, std::istream& in) {
        auto* j = go.AddComponent<VirtualJoystick>();
        KvIn k(in);
        int anchor = (int)j->anchor;
        k.f("x", j->position.x); k.f("y", j->position.y); k.f("w", j->size.x); k.f("h", j->size.y);
        k.i("anchor", anchor); j->anchor = (UIAnchor)anchor;
        k.str("action", j->action); k.b("floating", j->floating); k.f("dead", j->deadZone);
        k.b("touchOnly", j->onlyOnTouch); k.b("fade", j->fadeWhenIdle); k.f("knob", j->knobScale);
        k.col("base", j->baseColor); k.col("cap", j->knobColor);
    };
    ComponentSerializerRegistry::Register(std::move(stick));

    ComponentSerializerRegistry::Entry pad;
    pad.name = "touchdpad";
    pad.write = [](const GameObject& go, std::ostream& out) {
        auto* d = go.GetComponent<TouchDPad>();
        if (!d) return;
        out << "  touchdpad";
        KvOut{out}("x", d->position.x)("y", d->position.y)("w", d->size.x)("h", d->size.y)
            ("anchor", (int)d->anchor).str("action", d->action)("dead", d->deadZone)
            .b("diag", d->diagonals).b("touchOnly", d->onlyOnTouch)
            .col("c", d->color).col("p", d->pressedColor);
        out << "\n";
    };
    pad.read = [](GameObject& go, std::istream& in) {
        auto* d = go.AddComponent<TouchDPad>();
        KvIn k(in);
        int anchor = (int)d->anchor;
        k.f("x", d->position.x); k.f("y", d->position.y); k.f("w", d->size.x); k.f("h", d->size.y);
        k.i("anchor", anchor); d->anchor = (UIAnchor)anchor;
        k.str("action", d->action); k.f("dead", d->deadZone); k.b("diag", d->diagonals);
        k.b("touchOnly", d->onlyOnTouch); k.col("c", d->color); k.col("p", d->pressedColor);
    };
    ComponentSerializerRegistry::Register(std::move(pad));

    ComponentSerializerRegistry::Entry tbtn;
    tbtn.name = "touchbutton";
    tbtn.write = [](const GameObject& go, std::ostream& out) {
        auto* b = go.GetComponent<TouchButton>();
        if (!b) return;
        out << "  touchbutton";
        KvOut{out}("x", b->position.x)("y", b->position.y)("w", b->size.x)("h", b->size.y)
            ("anchor", (int)b->anchor).str("action", b->action).str("label", b->label)
            .b("touchOnly", b->onlyOnTouch).col("c", b->color).col("p", b->pressedColor).col("t", b->textColor);
        out << "\n";
    };
    tbtn.read = [](GameObject& go, std::istream& in) {
        auto* b = go.AddComponent<TouchButton>();
        KvIn k(in);
        int anchor = (int)b->anchor;
        k.f("x", b->position.x); k.f("y", b->position.y); k.f("w", b->size.x); k.f("h", b->size.y);
        k.i("anchor", anchor); b->anchor = (UIAnchor)anchor;
        k.str("action", b->action); k.str("label", b->label); k.b("touchOnly", b->onlyOnTouch);
        k.col("c", b->color); k.col("p", b->pressedColor); k.col("t", b->textColor);
    };
    ComponentSerializerRegistry::Register(std::move(tbtn));
    // Last, so every record written before keeps its place. Saved from their PropertyRegistry entries, no code of their own (none of them was saved before).
    for (const char* type : {"CheckpointTrigger", "TimedDestroy", "NetworkAvatarMotion", "ActiveRagdoll"})
        ComponentSerializerRegistry::RegisterFromProperties(type);
}
} // namespace detail

} // namespace okay
