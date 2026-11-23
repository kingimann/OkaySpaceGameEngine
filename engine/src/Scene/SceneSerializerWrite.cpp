// Writes one object's Transform and every component record (WriteComponentRecords). Moved unchanged from
// SceneSerializer.cpp (docs/engine/modularization_plan.md, section 3.1, S1).
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
namespace sceneser {

// Write a GameObject's Transform + all known components (no header/parent line). The records come in a fixed
// order; WriteComponents adds the `comporder` record when the object's own order differs from what they load as.
void WriteComponentRecords(std::ostream& out, GameObject* go) {
    Transform* t = go->transform;
    const Vec3& p = t->localPosition;
    const Quat& q = t->localRotation;
    const Vec3& s = t->localScale;
    out << "  transform " << p.x << " " << p.y << " " << p.z << " "
        << q.x << " " << q.y << " " << q.z << " " << q.w << " "
        << s.x << " " << s.y << " " << s.z << "\n";
    for (auto* sr : go->GetComponents<SpriteRenderer>()) {
        out << "  sprite " << static_cast<int>(sr->glyph) << " "
            << sr->color.r << " " << sr->color.g << " " << sr->color.b << " "
            << sr->color.a << " " << sr->size.x << " " << sr->size.y << " "
            << Quote(sr->texture)
            << " " << sr->uvMin.x << " " << sr->uvMin.y
            << " " << sr->uvMax.x << " " << sr->uvMax.y
            << " " << sr->sortOrder
            << " " << (sr->flipX ? 1 : 0) << " " << (sr->flipY ? 1 : 0)
            << " " << sr->sortingLayer
            << " " << (int)sr->texFilter << "\n";   // trailing (back-compatible)
    }
    for (auto* cam : go->GetComponents<Camera>()) {
        out << "  camera " << (int)cam->projection << " " << cam->orthographicSize << " "
            << cam->fieldOfView << " "
            << cam->backgroundColor.r << " " << cam->backgroundColor.g << " "
            << cam->backgroundColor.b << " " << cam->backgroundColor.a << " "
            << (cam->main ? 1 : 0)
            << " " << (int)cam->clearFlags << " " << cam->depth
            << " " << cam->nearClip << " " << cam->farClip
            << " " << (cam->fovAxisHorizontal ? 1 : 0)
            << " " << cam->rectX << " " << cam->rectY << " " << cam->rectW << " " << cam->rectH
            << " " << cam->cullingMask
            << " " << (cam->physicalCamera ? 1 : 0) << " " << cam->focalLength << " " << cam->sensorHeight
            << " " << cam->zoom << " " << cam->dutch << " " << cam->lensShiftX << " " << cam->lensShiftY
            << " " << (cam->pixelPerfect ? 1 : 0) << " " << cam->pixelsPerUnit
            << " " << cam->shakeAmount << " " << cam->shakeAngle << " " << cam->shakeFrequency << " " << cam->shakeRecovery;
        if (!cam->renderTarget.empty()) out << " " << Quote(cam->renderTarget);
        out << "\n";
    }
    for (auto* cam : go->GetComponents<Camera>()) {   // keyed extension (docs/gameplay/cameras.md); older readers skip the line
        if (cam->hitPulseVignette != 0.5f) {   // only when changed, so scenes that never touch it save exactly as before
            out << "  camerax";
            KvOut k{out};
            k("hitPulse", cam->hitPulseVignette);
            out << "\n";
        }
    }
    // A Terrain owns its (generated) mesh, so don't serialize the big MeshRenderer
    // geometry for it — the component below rebuilds it on load.
    if (!go->GetComponent<Terrain>() && !go->GetComponent<Character>() && !go->GetComponent<Decal>()) {  // a decal's mesh is cut from the scene again on load
    for (auto* mr : go->GetComponents<MeshRenderer>()) {
        out << "  mesh " << Quote(mr->mesh.name) << " "
            << mr->color.r << " " << mr->color.g << " " << mr->color.b << " "
            << (mr->fadeBaseAlpha >= 0.0f ? mr->fadeBaseAlpha : mr->color.a) << " " << (mr->wireframe ? 1 : 0) << " "
            << Quote(mr->meshPath) << " " << (mr->doubleSided ? 1 : 0) << "\n";
        // Procedural variant of a named nature prop (compact: one int regenerates
        // the exact varied shape on load, instead of baking meshgeo/meshcolors).
        if (mr->meshVariant != 0 && Mesh::NameHasVariants(mr->mesh.name))
            out << "  meshvar " << mr->meshVariant << "\n";
        // Persist edited/custom geometry: a mesh with no primitive name and no
        // .OBJ path is hand-edited, so its vertices/triangles can't be regenerated
        // from a name — write them verbatim. This record follows the `mesh` line
        // and overwrites the placeholder geometry on load.
        // (Skip when a SkinnedMesh owns this renderer — it serializes the BIND mesh via
        // `skinmesh` below and rebuilds the deformed mesh at runtime; storing the live
        // deformed pose here would be wrong and redundant.)
        // A mesh loaded from a file (meshPath) that has been edited since is written
        // too, so the edit isn't lost when the scene is opened again (the `meshgeo`
        // record wins over the file on load).
        bool editedFromFile = false;
        if (!mr->meshPath.empty() && mr->mesh.name.empty() && !mr->mesh.vertices.empty()) {
            if (mr->meshPathHash == 0) {   // not known yet: fingerprint what the file holds
                Mesh onDisk; std::string tex;
                if (modeling::LoadMeshAsset(mr->meshPath, onDisk, &tex, nullptr)) mr->meshPathHash = modeling::GeometryHash(onDisk);
            }
            editedFromFile = modeling::GeometryHash(mr->mesh) != mr->meshPathHash;
        }
        // A Spline Mesh builds its renderer's mesh again on load: none of it is saved.
        // (A Voxel Terrain does too: its per-face colours were 0.4 MB of every saved Sandbox scene, rebuilt on load anyway.)
        const bool generated = go->GetComponent<SplineMesh>() != nullptr || go->GetComponent<VoxelTerrain>() != nullptr;
        if (mr->mesh.name.empty() && (mr->meshPath.empty() || editedFromFile) && !mr->mesh.vertices.empty()
            && !go->GetComponent<SkinnedMesh>() && !generated) {
            // A mesh in edit mode is welded with its exact UVs per corner: write it
            // split at the UV seams, the form it's drawn and loaded in.
            Mesh split;
            if (mr->mesh.HasCornerUVs()) split = mr->mesh.SplitCornerUVs();
            const Mesh& gm = mr->mesh.HasCornerUVs() ? split : mr->mesh;
            out << "  meshgeo " << gm.vertices.size();
            for (const Vec3& v : gm.vertices)
                out << " " << v.x << " " << v.y << " " << v.z;
            out << " " << gm.triangles.size();
            for (int t : gm.triangles) out << " " << t;
            out << "\n";
            // Per-vertex UVs for the edited geometry (separate optional record so
            // older scenes still load). Written after meshgeo because the meshgeo
            // reader clears the uvs array.
            if (gm.uvs.size() == gm.vertices.size() && !gm.uvs.empty()) {
                out << "  meshuv " << gm.uvs.size();
                for (const Vec2& t : gm.uvs) out << " " << t.x << " " << t.y;
                out << "\n";
            }
            // Which triangles make up each quad / n-gon (optional record), so the
            // polygons come back exactly for modeling after a reload.
            if ((int)gm.polyFace.size() == gm.TriangleCount() && !gm.polyFace.empty()) {
                out << "  meshpoly " << gm.polyFace.size();
                for (int f : gm.polyFace) out << " " << f;
                out << "\n";
            }
            // UV seams (vertex pairs; the split keeps the original vertex numbers).
            if (!gm.seams.empty()) {
                out << "  meshseams " << gm.seams.size();
                for (const auto& e : gm.seams) out << " " << e.first << " " << e.second;
                out << "\n";
            }
            // Shading mode: meshgeo loads flat by default; remember when the user
            // chose Shade Smooth (1) or Auto Smooth by angle (2 <angle>) so it
            // survives save/load (optional record).
            if (mr->mesh.HasNormals()) {
                if (mr->mesh.autoSmoothAngle > 0.0f)
                    out << "  meshshade 2 " << mr->mesh.autoSmoothAngle << "\n";
                else
                    out << "  meshshade 1\n";
            }
        }
        // Per-face colors (vertex-painting / baked lightmap) — separate record so
        // older scenes still load. Written whenever populated, for named primitives
        // too (a baked cube carries its baked lighting).
        if (mr->mesh.HasFaceColors() && !generated) {
            out << "  meshcolors " << mr->mesh.triColors.size();
            for (const Color& c : mr->mesh.triColors)
                out << " " << c.r << " " << c.g << " " << c.b << " " << c.a;
            out << "\n";
        }
        // Material slots (per-face material: which slot each triangle uses, and each
        // slot's colour). Separate records so older scenes still load; they follow the
        // geometry so the triangle count lines up.
        if (mr->mesh.HasSlots() && !generated) {
            out << "  meshslots " << mr->mesh.triSlot.size();
            for (int sl : mr->mesh.triSlot) out << " " << sl;
            out << "\n  slotcolors " << mr->mesh.slotColors.size();
            for (const Color& c : mr->mesh.slotColors) out << " " << c.r << " " << c.g << " " << c.b << " " << c.a;
            out << "\n";
        }
        // Bevel weights (vertex pairs + weight), optional record.
        if (!mr->mesh.bevelWeights.empty() && !generated) {
            out << "  meshbevelw " << mr->mesh.bevelWeights.size();
            for (const EdgeCrease& c : mr->mesh.bevelWeights) out << " " << c.a << " " << c.b << " " << c.w;
            out << "\n";
        }
        // Subdivision creases (vertex pairs + sharpness), optional record.
        if (!mr->mesh.creases.empty() && !generated) {
            out << "  meshcreases " << mr->mesh.creases.size();
            for (const EdgeCrease& c : mr->mesh.creases) out << " " << c.a << " " << c.b << " " << c.w;
            out << "\n";
        }
        // Per-vertex colours (smooth painting): optional record, follows the geometry so the count lines up.
        if (mr->mesh.HasVertexColors() && !generated) {
            out << "  meshvcolors " << mr->mesh.vertexColors.size();
            for (const Color& c : mr->mesh.vertexColors) out << " " << c.r << " " << c.g << " " << c.b << " " << c.a;
            out << "\n";
        }
        // Hidden faces (Hide / Isolate in edit mode): kept with the mesh so a saved scene still has them to reveal.
        if (mr->mesh.HasHidden() && mr->mesh.HiddenConsistent() && !generated) {
            const int hn = mr->mesh.HiddenTriangleCount();
            const bool huv = !mr->mesh.hiddenUV.empty(), hco = !mr->mesh.hiddenColor.empty();
            out << "  meshhidden " << hn << " " << (huv ? 1 : 0) << " " << (hco ? 1 : 0);
            for (int t = 0; t < hn; ++t) {
                for (int k = 0; k < 3; ++k) { const Vec3& p = mr->mesh.hiddenPos[t * 3 + k]; out << " " << p.x << " " << p.y << " " << p.z; }
                out << " " << mr->mesh.hiddenSlot[t] << " " << mr->mesh.hiddenPoly[t];
                if (huv) for (int k = 0; k < 3; ++k) out << " " << mr->mesh.hiddenUV[t * 3 + k].x << " " << mr->mesh.hiddenUV[t * 3 + k].y;
                if (hco) { const Color& c = mr->mesh.hiddenColor[t]; out << " " << c.r << " " << c.g << " " << c.b << " " << c.a; }
            }
            out << "\n";
        }
        // A picture per material slot (optional record; empty entries mean "no picture for this slot").
        {
            bool any = false;
            for (const std::string& st : mr->slotTextures) if (!st.empty()) any = true;
            if (any) {
                out << "  slottex " << mr->slotTextures.size();
                for (const std::string& st : mr->slotTextures) out << " " << Quote(st);
                out << "\n";
            }
        }
        // Material (emissive rgb, specular, shininess, unlit) — separate record
        // so older scenes without it still load.
        out << "  material " << mr->emissive.r << " " << mr->emissive.g << " "
            << mr->emissive.b << " " << mr->specular << " " << mr->shininess << " "
            << (mr->unlit ? 1 : 0) << " " << Quote(mr->texture) << " "
            << mr->tiling.x << " " << mr->tiling.y << "\n";
        // Texture filter + offset (separate record; written when non-default).
        if (mr->texFilter != MeshRenderer::TexFilter::Smooth ||
            mr->texOffset.x != 0.0f || mr->texOffset.y != 0.0f)
            out << "  texopts " << (int)mr->texFilter << " "
                << mr->texOffset.x << " " << mr->texOffset.y << "\n";
        // Normal map (separate record so older scenes still load).
        if (!mr->normalMap.empty())
            out << "  normalmap " << Quote(mr->normalMap) << " " << mr->normalStrength << "\n";
        // Environment reflectivity + metalness (separate record, metalness is an
        // optional trailing field so older scenes still load).
        if (mr->reflectivity > 0.0f || mr->metallic > 0.0f)
            out << "  reflect " << mr->reflectivity << " " << mr->metallic << "\n";
        // Specular/gloss map (separate record so older scenes still load).
        if (!mr->specularMap.empty())
            out << "  specmap " << Quote(mr->specularMap) << "\n";
        // Ambient-occlusion map + strength (separate record so older scenes still load).
        if (!mr->aoMap.empty())
            out << "  aomap " << Quote(mr->aoMap) << " " << mr->aoStrength << "\n";
        if (!mr->metallicRoughnessMap.empty())
            out << "  mrmap " << Quote(mr->metallicRoughnessMap) << "\n";
        if (!mr->emissiveMap.empty())
            out << "  emissivemap " << Quote(mr->emissiveMap) << "\n";
        // Shading model (separate record so older scenes still load).
        if (mr->shader != MeshRenderer::Shader::Standard)
            out << "  shader " << (int)mr->shader << " " << mr->toonBands
                << " " << mr->gradientTop.r << " " << mr->gradientTop.g << " " << mr->gradientTop.b
                << " " << mr->gradientBottom.r << " " << mr->gradientBottom.g << " " << mr->gradientBottom.b << "\n";
        // Custom shader file + its property values (separate record so older scenes still load).
        if (!mr->customShader.empty() || !mr->shaderParams.empty())
            out << "  customshader " << Quote(mr->customShader) << " " << Quote(mr->shaderParams) << "\n";
        // Per-material rim (Fresnel) backlight (separate record).
        if (mr->rimStrength > 0.0f)
            out << "  rim " << mr->rimStrength << " " << mr->rimPower << " "
                << mr->rimColor.r << " " << mr->rimColor.g << " " << mr->rimColor.b << "\n";
        // Silhouette outline (separate record).
        if (mr->outline)
            out << "  outline " << mr->outlineWidth << " "
                << mr->outlineColor.r << " " << mr->outlineColor.g << " " << mr->outlineColor.b << "\n";
        // Scrolling UV + triplanar mapping (separate record).
        if (mr->uvScroll.x != 0.0f || mr->uvScroll.y != 0.0f || mr->triplanar)
            out << "  uvanim " << mr->uvScroll.x << " " << mr->uvScroll.y << " "
                << (mr->triplanar ? 1 : 0) << "\n";
        // Ground contact shadow (separate record; written when it differs from default).
        if (!mr->groundShadow || mr->groundShadowY != 0.0f || mr->groundShadowStrength != 0.5f)
            out << "  groundshadow " << (mr->groundShadow ? 1 : 0) << " "
                << mr->groundShadowY << " " << mr->groundShadowStrength << "\n";
        if (!mr->castShadows || !mr->receiveShadows)
            out << "  meshshadows " << (mr->castShadows ? 1 : 0) << " " << (mr->receiveShadows ? 1 : 0) << "\n";
        if (mr->glowLightRange != 0.0f) out << "  meshglow " << mr->glowLightRange << "\n";
        // A hidden mesh (e.g. a Boolean modifier's cutter) stays hidden after a reload.
        if (!mr->enabled) out << "  meshenabled 0\n";
    }
    }
    // Modifier stack: one header line, then one line per modifier (in order).
    for (auto* mm : go->GetComponents<MeshModifiers>()) {
        out << "  modstack";
        KvOut{out}.b("on", mm->enabled)("count", (int)mm->modifiers.size());
        out << "\n";
        for (const MeshModifier& m : mm->modifiers) {
            out << "  mod";
            KvOut k{out};
            k("type", (int)m.type).b("on", m.enabled).b("edit", m.showInEditMode)
             .b("mx", m.mirrorX).b("my", m.mirrorY).b("mz", m.mirrorZ).b("merge", m.mirrorMerge)
             ("n", m.count).b("rel", m.relative)("ox", m.offset.x)("oy", m.offset.y)("oz", m.offset.z)
             ("lv", m.levels).b("smooth", m.smoothShading)("th", m.thickness)
             ("w", m.width)("seg", m.segments)("ang", m.angle)
             .str("tgt", m.target)("op", m.boolOp)("vox", m.voxel)("cell", m.cellSize)
             ("str", m.strength)("freq", m.frequency)("seed", m.seed).b("bw", m.useBevelWeights);
            if (m.type == MeshModifier::Type::CurveArray) {      // the curve: "x,y,z;x,y,z;..."
                std::ostringstream pt;
                for (std::size_t i = 0; i < m.path.size(); ++i)
                    pt << (i ? ";" : "") << m.path[i].x << "," << m.path[i].y << "," << m.path[i].z;
                k.b("align", m.align).str("path", pt.str());
            }
            out << "\n";
        }
    }
    // Skinned mesh: the bind-pose geometry + per-vertex bone weights + joint names +
    // inverse-bind matrices, so an imported rigged character survives save/reload
    // (the joints are re-resolved by name and the mesh re-skins at runtime).
    int skinBlendAt = 0;
    const auto skinBlends = go->GetComponents<BlendShapes>();
    for (auto* sm : go->GetComponents<SkinnedMesh>()) {
        // Blend shapes morph the bind in place: store the unmorphed one. The Nth shape set
        // belongs to the Nth skinned mesh; one of each is the first, as before.
        const auto* bsh = skinBlendAt < (int)skinBlends.size() ? skinBlends[(std::size_t)skinBlendAt] : nullptr;
        ++skinBlendAt;
        const Mesh& bm = (bsh && bsh->base.vertices.size() == sm->bind.vertices.size()) ? bsh->base : sm->bind;
        out << "  skinmesh " << bm.vertices.size();
        for (const Vec3& v : bm.vertices) out << " " << v.x << " " << v.y << " " << v.z;
        out << " " << bm.normals.size();
        for (const Vec3& n : bm.normals) out << " " << n.x << " " << n.y << " " << n.z;
        out << " " << bm.uvs.size();
        for (const Vec2& u : bm.uvs) out << " " << u.x << " " << u.y;
        out << " " << bm.triangles.size();
        for (int t : bm.triangles) out << " " << t;
        out << " " << sm->jointIdx.size();
        for (std::size_t i = 0; i < sm->jointIdx.size(); ++i) {
            const auto& ji = sm->jointIdx[i]; const auto& jw = sm->jointWt[i];
            out << " " << ji[0] << " " << ji[1] << " " << ji[2] << " " << ji[3]
                << " " << jw[0] << " " << jw[1] << " " << jw[2] << " " << jw[3];
        }
        out << " " << sm->jointNames.size();
        for (const std::string& nm : sm->jointNames) out << " " << Quote(nm);
        out << " " << sm->inverseBind.size();
        for (const Mat4& M : sm->inverseBind) for (int e = 0; e < 16; ++e) out << " " << M.m[e];
        out << "\n";
        if (sm->method != 0) out << "  skinx " << sm->method << "\n";   // separate record: older builds skip it
    }
    // Blend shapes (after skinmesh, whose bind they apply to): sparse offsets.
    for (auto* bs : go->GetComponents<BlendShapes>()) {
        out << "  blendshapes " << bs->shapes.size();
        for (const auto& sh : bs->shapes) {
            const bool n = sh.dNrm.size() == sh.index.size() && !sh.index.empty();
            out << " " << Quote(sh.name) << " " << sh.weight << " " << sh.index.size() << " " << (n ? 1 : 0);
            for (std::size_t k = 0; k < sh.index.size(); ++k) {
                out << " " << sh.index[k] << " " << sh.dPos[k].x << " " << sh.dPos[k].y << " " << sh.dPos[k].z;
                if (n) out << " " << sh.dNrm[k].x << " " << sh.dNrm[k].y << " " << sh.dNrm[k].z;
            }
        }
        out << "\n";
    }
    // Model animation library: every imported clip (idle/walk/run) as per-node tracks,
    // so a saved model keeps all its animations and the picker still works on reload.
    for (auto* ma : go->GetComponents<ModelAnimator>()) {
        out << "  modelanim " << ma->active << " " << (ma->autoPlay ? 1 : 0)
            << " " << ma->speed << " " << (ma->loop ? 1 : 0) << " " << ma->clips.size();
        for (const auto& clip : ma->clips) {
            out << " " << Quote(clip.name) << " " << clip.nodes.size();
            for (const auto& nc : clip.nodes) {
                out << " " << Quote(nc.node) << " " << nc.clip.Tracks().size();
                for (const auto& tr : nc.clip.Tracks()) {
                    out << " " << Quote(tr.first) << " " << tr.second.Keys().size();
                    for (const auto& key : tr.second.Keys()) out << " " << key.time << " " << key.value;
                }
            }
        }
        out << "\n";
        // Locomotion (auto idle/walk/run) — separate record so it's optional.
        if (ma->driveByMovement || ma->inPlace || !ma->idleClip.empty() || !ma->walkClip.empty() || !ma->runClip.empty())
            out << "  modelanimdrive " << (ma->driveByMovement ? 1 : 0)
                << " " << ma->walkThreshold << " " << ma->runThreshold
                << " " << Quote(ma->idleClip) << " " << Quote(ma->walkClip) << " " << Quote(ma->runClip)
                << " " << (ma->inPlace ? 1 : 0) << "\n";
        // Air states (jump/fall/land by vertical motion) — optional record.
        if (!ma->jumpClip.empty() || !ma->fallClip.empty() || !ma->landClip.empty())
            out << "  modelanimair " << Quote(ma->jumpClip) << " " << Quote(ma->fallClip)
                << " " << Quote(ma->landClip) << " " << ma->airUpVel << " " << ma->airDownVel << "\n";
        // Stance clips (crouch / prone / get up) — optional record.
        if (!ma->crouchClip.empty() || !ma->crouchWalkClip.empty() || !ma->proneClip.empty() ||
            !ma->crawlClip.empty() || !ma->getUpClip.empty())
            out << "  modelanimstance " << Quote(ma->crouchClip) << " " << Quote(ma->crouchWalkClip)
                << " " << Quote(ma->proneClip) << " " << Quote(ma->crawlClip) << " " << Quote(ma->getUpClip) << "\n";
        if (ma->HasDirectionalClips())
            out << "  modelanimdir " << Quote(ma->strafeLeftClip) << " " << Quote(ma->strafeRightClip)
                << " " << Quote(ma->walkBackClip) << " " << Quote(ma->runBackClip) << "\n";
        if (ma->legsFollowTravel)
            out << "  modelanimlegs 1\n";
        // Crossfade + clip events — separate optional records (older scenes lack them).
        out << "  modelanimblend " << ma->blendTime << "\n";
        if (ma->rootMotion || ma->rootMotionY || !ma->rootMotionNode.empty() || ma->rootMotionRotation || ma->rootMotionApplyTo)
            out << "  modelanimroot " << (ma->rootMotion ? 1 : 0) << " " << Quote(ma->rootMotionNode)
                << " " << (ma->rootMotionY ? 1 : 0) << " " << (ma->rootMotionRotation ? 1 : 0)
                << " " << ma->rootMotionApplyTo << "\n";
        if (ma->smoothLocomotion)
            out << "  modelanimsmooth 1\n";
        // Stride matching + built-in fallback: only written when changed from the defaults,
        // so older scenes (which lack the record) and untouched models stay as they were.
        {
            const ModelAnimator d;
            if (ma->strideMatch != d.strideMatch || ma->builtinFallback != d.builtinFallback ||
                ma->walkClipSpeed != d.walkClipSpeed || ma->runClipSpeed != d.runClipSpeed ||
                ma->crouchWalkClipSpeed != d.crouchWalkClipSpeed || ma->crawlClipSpeed != d.crawlClipSpeed)
                out << "  modelanimstride " << (ma->strideMatch ? 1 : 0) << " " << (ma->builtinFallback ? 1 : 0)
                    << " " << ma->walkClipSpeed << " " << ma->runClipSpeed << " " << ma->crouchWalkClipSpeed
                    << " " << ma->crawlClipSpeed << "\n";
        }
        // Clips with smooth (cubic) keys; the rest are linear, as imported.
        {
            std::vector<std::size_t> smoothClips;
            for (std::size_t ci = 0; ci < ma->clips.size(); ++ci)
                for (const auto& nc : ma->clips[ci].nodes)
                    if (nc.clip.smoothKeys) { smoothClips.push_back(ci); break; }
            if (!smoothClips.empty()) {
                out << "  modelanimcubic " << smoothClips.size();
                for (std::size_t ci : smoothClips) out << " " << ci;
                out << "\n";
            }
        }
        for (std::size_t ci = 0; ci < ma->clips.size(); ++ci) {
            if (ma->clips[ci].events.empty()) continue;
            out << "  modelanimevents " << ci << " " << ma->clips[ci].events.size();
            for (const auto& ev : ma->clips[ci].events)
                out << " " << ev.time << " " << Quote(ev.name);
            out << "\n";
        }
    }
    int asmAt = 0;
    for (AnimStateMachine* asm2 : go->GetComponents<AnimStateMachine>()) {
        // Layers (machines after the base) start with a header record; every
        // record that follows belongs to that machine until the next header.
        // A second machine on layer 0 needs the header too, or its records would
        // overwrite the first. One layer-0 machine still writes no header.
        if (asmAt > 0 || asm2->layer > 0) {
            out << "  animsmlayer " << asm2->layer << " " << asm2->layerWeight << " " << Quote(asm2->maskBone) << "\n";
            if (asm2->additive || !asm2->maskBones.empty()) {
                out << "  animsmlayerx " << (asm2->additive ? 1 : 0) << " " << asm2->maskBones.size();
                for (const std::string& b : asm2->maskBones) out << " " << Quote(b);
                out << "\n";
            }
            if (!asm2->layerName.empty()) out << "  animsmlname " << Quote(asm2->layerName) << "\n";
        }
        ++asmAt;
        out << "  animsm " << Quote(asm2->entry) << " " << asm2->states.size();
        for (const auto& st : asm2->states) {
            out << " " << Quote(st.name) << " " << Quote(st.clip) << " " << st.speed
                << " " << (st.loop ? 1 : 0) << " " << st.transitions.size();
            for (const auto& tr : st.transitions)
                out << " " << Quote(tr.to) << " " << (int)tr.cond << " " << Quote(tr.param)
                    << " " << tr.value << " " << tr.blend;
        }
        out << "\n";
        // Animator graph node positions (separate record so old files load; the
        // graph auto-lays-out when it's absent). Order matches `states`.
        out << "  animsmpos " << asm2->states.size();
        for (const auto& st : asm2->states) out << " " << st.nx << " " << st.ny;
        out << "\n";
        if (asm2->anyX != 0.0f || asm2->anyY != 0.0f) out << "  animsmanypos " << asm2->anyX << " " << asm2->anyY << "\n";
        if (!asm2->params.empty()) {
            out << "  animsmparams " << asm2->params.size();
            for (const auto& p : asm2->params) out << " " << Quote(p.name) << " " << p.type;
            out << "\n";
            // Parameter defaults (index-matched; only when one is set).
            bool anyDef = false;
            for (const auto& p : asm2->params) anyDef = anyDef || p.def != 0.0f;
            if (anyDef) {
                out << "  animsmpdef " << asm2->params.size();
                for (const auto& p : asm2->params) out << " " << p.def;
                out << "\n";
            }
        }
        // Extended transition/state data (exit time, extra conditions, blend trees)
        // and Any State transitions — separate records so older builds still load.
        auto writeExt = [&](const AnimStateMachine::Transition& tr) {
            out << " " << (tr.hasExitTime ? 1 : 0) << " " << tr.exitTime << " " << (tr.canInterruptSelf ? 1 : 0)
                << " " << tr.extra.size();
            for (const auto& c : tr.extra) out << " " << (int)c.cond << " " << Quote(c.param) << " " << c.value;
        };
        out << "  animsmx " << asm2->states.size();
        for (const auto& st : asm2->states) {
            out << " " << (st.blendTree ? 1 : 0) << " " << Quote(st.blendParam) << " " << st.motions.size();
            for (const auto& m : st.motions) out << " " << Quote(m.clip) << " " << m.threshold;
            out << " " << st.transitions.size();
            for (const auto& tr : st.transitions) writeExt(tr);
        }
        out << "\n";
        if (!asm2->anyState.empty()) {
            out << "  animsmany " << asm2->anyState.size();
            for (const auto& tr : asm2->anyState) {
                out << " " << Quote(tr.to) << " " << (int)tr.cond << " " << Quote(tr.param)
                    << " " << tr.value << " " << tr.blend;
                writeExt(tr);
            }
            out << "\n";
        }
        // 2D blend trees and speed parameters.
        bool any2 = false;
        for (const auto& st : asm2->states) if (st.blendDims != 1 || !st.speedParam.empty()) any2 = true;
        if (any2) {
            out << "  animsm2d " << asm2->states.size();
            for (const auto& st : asm2->states) {
                out << " " << st.blendDims << " " << Quote(st.blendParamY) << " " << Quote(st.speedParam) << " " << st.motions.size();
                for (const auto& m : st.motions) out << " " << m.posY;
            }
            out << "\n";
        }
        // State events, per-state IK weight, transition start offsets.
        bool anyEv = false;
        for (const auto& st : asm2->states) {
            if (!st.enterEvent.empty() || !st.exitEvent.empty() || st.ikWeight != 1.0f) anyEv = true;
            for (const auto& tr : st.transitions) if (tr.offset != 0.0f) anyEv = true;
        }
        for (const auto& tr : asm2->anyState) if (tr.offset != 0.0f) anyEv = true;
        if (anyEv) {
            out << "  animsmev " << asm2->states.size();
            for (const auto& st : asm2->states) {
                out << " " << Quote(st.enterEvent) << " " << Quote(st.exitEvent) << " " << st.ikWeight << " " << st.transitions.size();
                for (const auto& tr : st.transitions) out << " " << tr.offset;
            }
            out << " " << asm2->anyState.size();
            for (const auto& tr : asm2->anyState) out << " " << tr.offset;
            out << "\n";
        }
        // Per-state full-body IK weight, where it stops following ikWeight
        // (a newer record; older builds skip it).
        bool anyFb = false;
        for (const auto& st : asm2->states) anyFb = anyFb || st.fullBodyIkWeight >= 0.0f;
        if (anyFb) {
            out << "  animsmfb " << asm2->states.size();
            for (const auto& st : asm2->states) out << " " << st.fullBodyIkWeight;
            out << "\n";
        }
        // Look-At / Aim weight, where it stops following the foot weight.
        bool anyLook = false;
        for (const auto& st : asm2->states) anyLook = anyLook || st.lookWeight != 1.0f;
        if (anyLook) {
            out << "  animsmlk " << asm2->states.size();
            for (const auto& st : asm2->states) out << " " << st.lookWeight;
            out << "\n";
        }
        // Per-state IK weight curves (foot, then full-body). Written only when
        // a state has keys, so older scenes and older builds stay unchanged.
        bool anyIkC = false;
        for (const auto& st : asm2->states) anyIkC = anyIkC || !st.ikCurve.empty() || !st.fullBodyIkCurve.empty();
        if (anyIkC) {
            out << "  animsmikc " << asm2->states.size();
            for (const auto& st : asm2->states) {
                out << " " << st.ikCurve.size();
                for (const auto& k : st.ikCurve) out << " " << k.t << " " << k.w;
                out << " " << st.fullBodyIkCurve.size();
                for (const auto& k : st.fullBodyIkCurve) out << " " << k.t << " " << k.w;
            }
            out << "\n";
        }
        // Muted transitions.
        bool anyMute = false;
        for (const auto& st : asm2->states) for (const auto& tr : st.transitions) anyMute = anyMute || tr.muted;
        for (const auto& tr : asm2->anyState) anyMute = anyMute || tr.muted;
        if (anyMute) {
            out << "  animsmmute " << asm2->states.size();
            for (const auto& st : asm2->states) {
                out << " " << st.transitions.size();
                for (const auto& tr : st.transitions) out << " " << (tr.muted ? 1 : 0);
            }
            out << " " << asm2->anyState.size();
            for (const auto& tr : asm2->anyState) out << " " << (tr.muted ? 1 : 0);
            out << "\n";
        }
        bool anyInt = false;
        for (const auto& st : asm2->states) for (const auto& tr : st.transitions)
            anyInt = anyInt || tr.interruption != AnimStateMachine::Transition::Interrupt::NextState;
        if (anyInt) {   // interruption source per state transition
            out << "  animsmint " << asm2->states.size();
            for (const auto& st : asm2->states) {
                out << " " << st.transitions.size();
                for (const auto& tr : st.transitions) out << " " << (int)tr.interruption;
            }
            out << "\n";
        }
        // Transitions that keep the cycle's phase (same layout as animsmmute).
        bool anySync = false;
        for (const auto& st : asm2->states) for (const auto& tr : st.transitions) anySync = anySync || tr.syncPhase;
        for (const auto& tr : asm2->anyState) anySync = anySync || tr.syncPhase;
        if (anySync) {
            out << "  animsmsync " << asm2->states.size();
            for (const auto& st : asm2->states) {
                out << " " << st.transitions.size();
                for (const auto& tr : st.transitions) out << " " << (tr.syncPhase ? 1 : 0);
            }
            out << " " << asm2->anyState.size();
            for (const auto& tr : asm2->anyState) out << " " << (tr.syncPhase ? 1 : 0);
            out << "\n";
        }
        {
            const StanceTimes def;
            const StanceTimes& t = asm2->stanceTimes;
            if (t.standCrouch != def.standCrouch || t.crouchProne != def.crouchProne || t.proneStand != def.proneStand)
                out << "  animsmstance " << t.standCrouch << " " << t.crouchProne << " " << t.proneStand << "\n";
        }
        {
            const GaitCrossfades def;
            const GaitCrossfades& t = asm2->crossfades;
            if (t.idleWalk != def.idleWalk || t.walkRun != def.walkRun ||
                t.groundAir != def.groundAir || t.airGround != def.airGround)
                out << "  animsmxfade " << t.idleWalk << " " << t.walkRun << " " << t.groundAir << " " << t.airGround << "\n";
        }
        bool anyMatch = false;
        for (const auto& st : asm2->states) anyMatch = anyMatch || st.matchSpeed;
        if (anyMatch) {   // blend trees that keep pace with the body's speed
            out << "  animsmmatch " << asm2->states.size();
            for (const auto& st : asm2->states) out << " " << (st.matchSpeed ? 1 : 0);
            out << "\n";
        }
        if (asm2->actionsRev > 0)
            out << "  animsmactions " << asm2->actionsRev << "\n";
        bool anyLock = false;
        for (const auto& st : asm2->states) anyLock = anyLock || !st.interruptible;
        if (anyLock) {
            out << "  animsmnoint " << asm2->states.size();
            for (const auto& st : asm2->states) out << " " << (st.interruptible ? 1 : 0);
            out << "\n";
        }
        bool anyAnyInt = false;
        for (const auto& tr : asm2->anyState)
            anyAnyInt = anyAnyInt || tr.interruption != AnimStateMachine::Transition::Interrupt::NextState;
        if (anyAnyInt) {
            out << "  animsmanyint " << asm2->anyState.size();
            for (const auto& tr : asm2->anyState) out << " " << (int)tr.interruption;
            out << "\n";
        }
        bool anyFall = false;
        for (const auto& st : asm2->states) anyFall = anyFall || !st.fallbackClip.empty();
        if (anyFall) {   // per-state stand-in when the named clip is missing
            out << "  animsmfall " << asm2->states.size();
            for (const auto& st : asm2->states) out << " " << Quote(st.fallbackClip);
            out << "\n";
        }
        bool anyMir = false;
        for (const auto& st : asm2->states) anyMir = anyMir || st.mirror || st.rootMotion >= 0;
        if (anyMir) {
            out << "  animsmmir " << asm2->states.size();
            for (const auto& st : asm2->states)
                out << " " << (st.mirror ? 1 : 0) << " " << st.rootMotion;
            out << "\n";
        }
    }
    for (auto* tr : go->GetComponents<Terrain>()) {
        out << "  terrain " << tr->resolution << " " << tr->size << " "
            << tr->color.r << " " << tr->color.g << " " << tr->color.b << " " << tr->color.a
            << " " << tr->heights.size();
        for (float h : tr->heights) out << " " << h;
        // Auto-color "layers" (optional trailing fields; older scenes lack them).
        auto wcol = [&](const Color& c) { out << " " << c.r << " " << c.g << " " << c.b << " " << c.a; };
        out << " " << (tr->autoColor ? 1 : 0);
        wcol(tr->waterColor); wcol(tr->sandColor); wcol(tr->grassColor);
        wcol(tr->rockColor); wcol(tr->snowColor);
        out << " " << tr->waterLevel << " " << tr->snowLevel << " " << tr->rockSlope;
        // Texturing (appended; quote-guarded on read so older scenes still load).
        out << " " << Quote(tr->texture) << " " << tr->textureTiling << " " << (tr->triplanarTex ? 1 : 0)
            << " " << Quote(tr->normalMap) << " " << tr->normalStrength;
        out << "\n";
    }
    for (auto* td : go->GetComponents<TerrainDigger>()) {
        out << "  terraindigger " << (int)td->mode << " " << td->button
            << " " << (int)(unsigned char)td->key << " " << td->radius << " " << td->strength
            << " " << td->range << " " << td->relax
            << " " << td->hardness << " " << (td->showBrush ? 1 : 0)
            << " " << td->brushColor.r << " " << td->brushColor.g
            << " " << td->brushColor.b << " " << td->brushColor.a
            << " " << td->raiseButton << "\n";
    }
    for (auto* v : go->GetComponents<VoxelTerrain>()) {
        out << "  voxelterrain " << v->nx << " " << v->ny << " " << v->nz
            << " " << v->voxelSize << " " << v->iso << " " << (v->autoColor ? 1 : 0)
            << " " << v->snowLevel << " " << v->rockSlope
            << " " << v->color.r << " " << v->color.g << " " << v->color.b << " " << v->color.a
            << " " << v->grassColor.r << " " << v->grassColor.g << " " << v->grassColor.b
            << " " << v->rockColor.r << " " << v->rockColor.g << " " << v->rockColor.b
            << " " << v->soilColor.r << " " << v->soilColor.g << " " << v->soilColor.b
            << " " << v->snowColor.r << " " << v->snowColor.g << " " << v->snowColor.b
            << " " << v->textureTiling << " " << Quote(v->texture)
            << " " << v->EncodeDensity() << "\n";
        if (v->catchFalling) out << "  voxelcatch " << v->catchMargin << "\n";
        if (v->footprints) out << "  voxelfoot " << Quote(v->footprintTag) << " " << (v->footprintBodies ? 1 : 0) << " " << v->footprintStep << " " << v->footprintRadius << " " << v->footprintDepth << " " << v->footprintPower << "\n";
        if (v->fluidOn) out << "  voxelfluid " << v->fluidBudget << " " << v->fluidMaxCells << " " << v->fluidRate << " " << (v->fluidGlow ? 1 : 0) << " "
                            << v->fluidColor.r << " " << v->fluidColor.g << " " << v->fluidColor.b << " " << v->fluidColor.a << "\n";
        // Materials (optional records after the terrain, so a build that does not know them keeps them as unknown records)
        if (v->UseMaterials()) {
            out << "  voxelmats " << v->materials.size() << " " << v->EncodeMaterials() << "\n";
            for (const auto& m : v->materials)
                out << "  voxelmat " << Quote(m.name) << " " << m.color.r << " " << m.color.g << " " << m.color.b << " " << m.color.a
                    << " " << m.hardness << " " << m.density << " " << (m.loose ? 1 : 0) << " " << Quote(m.drop) << " " << m.dropPerM3
                    << " " << Quote(m.texture) << " " << Quote(m.digSound)
                    << " " << m.particle.r << " " << m.particle.g << " " << m.particle.b << "\n";
        }
    }
    for (auto* vd : go->GetComponents<VoxelDigger>()) {
        out << "  voxeldigger " << (int)vd->mode << " " << vd->button
            << " " << (int)(unsigned char)vd->key << " " << vd->radius << " " << vd->strength
            << " " << vd->range << " " << (vd->showBrush ? 1 : 0)
            << " " << vd->brushColor.r << " " << vd->brushColor.g
            << " " << vd->brushColor.b << " " << vd->brushColor.a
            << " " << vd->addButton
            << " " << (int)vd->collect << " " << (vd->needsMaterial ? 1 : 0) << " " << (vd->creative ? 1 : 0) << " " << vd->addMaterial
            << " " << vd->power << " " << vd->hardnessScale << " " << (int)vd->shape << " " << vd->depthLimit << " " << vd->protectBelow
            << " " << (vd->snapToGrid ? 1 : 0) << " " << vd->debrisMax << " " << vd->debrisLife << " " << (vd->slump ? 1 : 0)
            << " " << (vd->collapse ? 1 : 0) << " " << (vd->giveItems ? 1 : 0) << " " << Quote(vd->hudTarget) << " " << vd->slumpPasses << " " << (vd->toolsFromInventory ? 1 : 0) << " " << (vd->buildFromItems ? 1 : 0)
            << " " << vd->bareHandsPower << " " << Quote(vd->tools) << " " << vd->caveInSpan << " " << (int)(unsigned char)vd->pourKey << " " << vd->pourCells
            << " " << vd->reach << " " << ((vd->playerAim ? 1 : 0) | (vd->useActions ? 2 : 0) | (vd->hotbarKeys ? 4 : 0) | (vd->showProgress ? 8 : 0)) << "\n";
    }
    for (auto* pm : go->GetComponents<PauseMenu>()) {
        out << "  pausemenu " << (int)(unsigned char)pm->toggleKey
            << " " << (pm->showResume ? 1 : 0) << " " << (pm->showQuit ? 1 : 0)
            << " " << pm->dimColor.r << " " << pm->dimColor.g << " " << pm->dimColor.b << " " << pm->dimColor.a
            << " " << pm->panelColor.r << " " << pm->panelColor.g << " " << pm->panelColor.b << " " << pm->panelColor.a
            << " " << Quote(pm->title) << " " << Quote(pm->mainMenuScene) << "\n";
    }
    // Saveable: flags, then the save id, prefab, and the property paths ("Light.intensity" ...).
    for (auto* sv : go->GetComponents<Saveable>()) {
        out << "  saveable " << (sv->dynamic ? 1 : 0) << " " << (sv->saveTransform ? 1 : 0)
            << " " << (sv->saveActive ? 1 : 0) << " " << (sv->saveComponents ? 1 : 0)
            << " " << (sv->saveVariables ? 1 : 0) << " " << (sv->saveSceneVariables ? 1 : 0)
            << " " << Quote(sv->saveId) << " " << Quote(sv->prefab) << " " << sv->properties.size();
        for (const std::string& p : sv->properties) out << " " << Quote(p);
        out << "\n";
    }
    for (auto* ss : go->GetComponents<SaveConfig>())
        out << "  savesettings " << ss->autosaveInterval << " " << (ss->autosaveOnSceneStart ? 1 : 0)
            << " " << ss->autosaveSlots << "\n";
    for (auto* as : go->GetComponents<AutoSave>())
        out << "  autosave " << as->interval << " " << (as->saveOnStart ? 1 : 0)
            << " " << Quote(as->slot) << " " << Quote(as->label) << "\n";
    for (auto* fl : go->GetComponents<Flashlight>()) {
        out << "  flashlight " << (int)(unsigned char)fl->toggleKey << " " << (fl->on ? 1 : 0)
            << " " << fl->range << " " << fl->angle << " " << fl->intensity
            << " " << fl->color.r << " " << fl->color.g << " " << fl->color.b << "\n";
    }
    for (auto* fh : go->GetComponents<FirstPersonHand>()) {
        out << "  fphand " << fh->attackButton << " " << (fh->holdToSwing ? 1 : 0)
            << " " << (fh->showLeftArm ? 1 : 0)
            << " " << (fh->followPitch ? 1 : 0) << " " << (fh->bobbing ? 1 : 0)
            << " " << (fh->steadyArm ? 1 : 0) << " " << fh->armRaise << " " << fh->armElbow
            << " " << (fh->showWhileRunning ? 1 : 0) << "\n";
    }
    for (auto* hh : go->GetComponents<FirstPersonHands>()) {
        auto side = [&](const FirstPersonHands::HandSide& s) {
            out << " " << (s.enabled ? 1 : 0)
                << " " << s.position.x << " " << s.position.y << " " << s.position.z
                << " " << s.rotation.x << " " << s.rotation.y << " " << s.rotation.z
                << " " << s.uniformScale
                << " " << s.scale.x << " " << s.scale.y << " " << s.scale.z
                << " " << s.size.x << " " << s.size.y << " " << s.size.z
                << " " << (s.useColour ? 1 : 0)
                << " " << s.colour.r << " " << s.colour.g << " " << s.colour.b
                << " " << (s.sleeve ? 1 : 0) << " " << s.sleeveLength
                << " " << s.sleeveColour.r << " " << s.sleeveColour.g << " " << s.sleeveColour.b;
        };
        out << "  fphands " << (hh->showWhenEmpty ? 1 : 0) << " " << (hh->showWhileRunning ? 1 : 0)
            << " " << hh->bobAmount << " " << hh->swayAmount << " " << hh->punchAmount << " " << hh->handFov
            << " " << hh->itemOffset.x << " " << hh->itemOffset.y << " " << hh->itemOffset.z
            << " " << hh->itemRotation.x << " " << hh->itemRotation.y << " " << hh->itemRotation.z
            << " " << hh->itemScale << " " << (int)hh->holdMode;
        side(hh->right);
        side(hh->left);
        out << " " << hh->othersItemOffset.x << " " << hh->othersItemOffset.y << " " << hh->othersItemOffset.z
            << " " << hh->othersItemRotation.x << " " << hh->othersItemRotation.y << " " << hh->othersItemRotation.z
            << " " << hh->othersArmLift << " " << hh->othersArmForward << "\n";
        auto extra = [&](const FirstPersonHands::HandSide& s) {
            out << " " << (s.forearm ? 1 : 0) << " " << s.forearmLength << " " << s.forearmThickness
                << " " << s.forearmColour.r << " " << s.forearmColour.g << " " << s.forearmColour.b
                << " " << s.wristOffset.x << " " << s.wristOffset.y << " " << s.wristOffset.z
                << " " << s.wristRotation.x << " " << s.wristRotation.y << " " << s.wristRotation.z
                << " " << s.elbowBend << " " << s.reach;
        };
        auto oarm = [&](const FirstPersonHands::OthersArm& a) {
            out << " " << (a.forearm ? 1 : 0) << " " << a.forearmLength << " " << a.forearmThickness
                << " " << a.forearmColour.r << " " << a.forearmColour.g << " " << a.forearmColour.b
                << " " << a.handSize.x << " " << a.handSize.y << " " << a.handSize.z
                << " " << a.wristOffset.x << " " << a.wristOffset.y << " " << a.wristOffset.z
                << " " << a.wristRotation.x << " " << a.wristRotation.y << " " << a.wristRotation.z
                << " " << a.elbowBend << " " << a.reach;
        };
        out << "  fphands2";
        extra(hh->right); extra(hh->left); oarm(hh->othersRight); oarm(hh->othersLeft);
        out << "\n";
        // Presets, click swing and shirt colours came later. A scene without this line is the Old look.
        out << "  fphands3 " << (int)hh->preset << " " << (hh->swingOnClick ? 1 : 0) << " " << hh->attackButton
            << " " << (hh->right.matchShirt ? 1 : 0) << " " << (hh->left.matchShirt ? 1 : 0) << "\n";
    }
    for (auto* ps : go->GetComponents<NetworkPlayerSpawner>()) {
        out << "  npspawner " << Quote(ps->playerTemplate) << " " << Quote(ps->prefabFile)
            << " " << ps->spawnPoint.x << " " << ps->spawnPoint.y << " " << ps->spawnPoint.z
            << " " << ps->spawnSpread << " " << (ps->spawnLocalPlayer ? 1 : 0)
            << " " << (int)(unsigned char)ps->glyph << "\n";
    }
    for (auto* ns : go->GetComponents<NetworkSync>()) {
        out << "  netsync " << (int)ns->authority << " " << (ns->owned ? 1 : 0)
            << " " << (ns->syncAnimation ? 1 : 0) << " " << Quote(ns->netId);
        if (ns->clientClaimable) out << " 1";   // only when set: other scenes don't change
        out << "\n";
    }
    // netobject / nettransform / syncvar are their own records. An older engine
    // refuses a scene that contains them; scenes without these components are unchanged.
    for (auto* no : go->GetComponents<NetworkObject>()) {
        out << "  netobject " << (int)no->authority << " " << (no->owned ? 1 : 0)
            << " " << Quote(no->netId) << "\n";
    }
    for (NetworkTransform* dupKeep_ : go->GetComponents<NetworkTransform>()) { (void)dupKeep_; out << "  nettransform\n"; }
    for (auto* sv : go->GetComponents<SyncVar>())
        out << "  syncvar " << Quote(sv->name) << " " << Quote(sv->value) << "\n";
    for (auto* ac : go->GetComponents<AntiCheatConfig>()) {
        const AntiCheatSettings& s = ac->settings;
        std::string allow;
        for (const auto& p : s.spawnAllowlist) { if (!allow.empty()) allow += ';'; allow += p; }
        out << "  anticheat " << (s.enabled ? 1 : 0) << " " << s.maxSyncSpeed << " " << s.maxSyncVarsPerSecond
            << " " << s.kickAfterViolations << " " << (s.ownerOnlySyncVars ? 1 : 0)
            << " " << Quote(ac->file) << " " << Quote(allow)
            << " " << s.maxRpcsPerSecond << " " << s.maxRpcBytes;
        if (s.maxClockDrift > 0.0f) out << " " << s.maxClockDrift;   // only when on: other scenes don't change
        if (!s.serverVars.empty()) {
            std::string vars;
            for (const auto& v : s.serverVars) { if (!vars.empty()) vars += ';'; vars += v; }
            out << " " << Quote(vars);
        }
        out << "\n";
        out << "  anticheatsafety " << (s.requireEncryption ? 1 : 0) << " " << (ac->allowRuntimeToggle ? 1 : 0) << "\n";
        const AntiCheatSettings d;   // hit-claim tuning: its own line, only when changed
        if (s.hitOriginTolerance != d.hitOriginTolerance || s.hitTolerance != d.hitTolerance || s.maxHitRange != d.maxHitRange ||
            s.hitDamage != d.hitDamage || s.hitLineOfSight != d.hitLineOfSight || s.hitsNeedMoveCheck != d.hitsNeedMoveCheck ||
            s.maxRewindRtt != d.maxRewindRtt)
            out << "  anticheathit " << s.hitOriginTolerance << " " << s.hitTolerance << " " << s.maxHitRange
                << " " << s.hitDamage << " " << (s.hitLineOfSight ? 1 : 0) << " " << (s.hitsNeedMoveCheck ? 1 : 0)
                << " " << s.maxRewindRtt << "\n";
        for (const auto& w : s.weapons) out << "  anticheatweapon " << Quote(w) << "\n";
        {   // the rules the "anticheat" line has no room for (spawn limits, client-writable prefixes, the ticket check): one ".okayac" line each, only when set
            auto rule = [&](const std::string& l) { out << "  anticheatrule " << Quote(l) << "\n"; };
            std::ostringstream num;
            if (s.maxSpawnsPerSecond != d.maxSpawnsPerSecond) rule("maxSpawnsPerSecond " + std::to_string(s.maxSpawnsPerSecond));
            if (s.maxLiveSpawnsPerPeer != d.maxLiveSpawnsPerPeer) rule("maxLiveSpawnsPerPeer " + std::to_string(s.maxLiveSpawnsPerPeer));
            if (s.spawnRadius != d.spawnRadius) { num << "spawnRadius " << s.spawnRadius; rule(num.str()); }
            for (const auto& v : s.clientVars) rule("clientVar " + v);
            if (!s.ticketKey.empty()) rule("ticketKey " + s.ticketKey);
            if (!s.ticketAudience.empty()) rule("ticketAudience " + s.ticketAudience);
            if (s.ticketAnyAudience) rule("ticketAnyAudience true");
        }
        {   // the detection engine's keys (only those that differ from the defaults), one quoted ".okayac" line each
            std::istringstream dl(s.detect.ToText());
            for (std::string line; std::getline(dl, line);)
                if (!line.empty()) out << "  anticheatdetect " << Quote(line) << "\n";
        }
    }
    for (auto* nv : go->GetComponents<NetValidator>()) {
        out << "  netvalidator " << Quote(nv->rpc) << " " << nv->maxBytes << " " << nv->maxPerSecond
            << " " << (nv->clientsMaySend ? 1 : 0);
        if (nv->cooldown > 0.0f || nv->budget > 0.0f || nv->refillPerSecond > 0.0f)   // only when set
            out << " " << nv->cooldown << " " << nv->budget << " " << nv->refillPerSecond;
        out << "\n";
    }
    for (auto* w : go->GetComponents<Water>()) {
        out << "  water " << w->size << " " << w->resolution << " " << w->waveHeight
            << " " << w->waveLength << " " << w->waveSpeed
            << " " << w->color.r << " " << w->color.g << " " << w->color.b
            << " " << w->opacity << " " << w->reflectivity << " " << w->specular
            << " " << w->shininess << " " << w->flow.x << " " << w->flow.y
            << " " << w->depth << " " << w->fluidDensity << " " << w->linearDrag
            << " " << Quote(w->texture) << "\n";
    }
    for (auto* ws : go->GetComponents<WorldStreamer>()) {
        out << "  worldstreamer " << ws->cellSize << " " << ws->loadRadius
            << " " << ws->unloadRadius << " " << (ws->onlyOnCellChange ? 1 : 0)
            << " " << Quote(ws->folder) << " " << Quote(ws->prefix)
            << " " << Quote(ws->ext) << " " << Quote(ws->target) << "\n";
    }
    for (auto* de : go->GetComponents<Destructible>()) {
        out << "  destructible " << Quote(de->blockTag) << " " << de->voxelSize
            << " " << de->fractureChunks << " " << de->debrisLifetime
            << " " << de->explosionForce << " " << de->upwardBias
            << " " << de->debrisGravityScale << " " << de->debrisDrag
            << " " << (de->collapseEnabled ? 1 : 0) << " " << de->anchorY
            << " " << de->maxDebris << " " << de->breakButton
            << " " << de->breakRadius << " " << de->reach << "\n";
    }
    bool wroteCharacterMaterial = false;
    for (auto* ch : go->GetComponents<Character>()) {
        out << "  character " << ch->ToText() << "\n";
        // Disabled = a custom model replaced the blocky body (AttachCharacterModel):
        // remember it so the default character stays hidden after a load.
        if (!ch->enabled) out << "  charenabled 0\n";
        // External-rig mode: a HumanoidRetarget next to this Character drives an
        // imported skeleton from its pose (renders nothing itself).
        if (ch->driveExternal) out << "  charext 1\n";
        // Auto-rigged custom mesh (BindCustomMesh): store the bind geometry so the
        // rigged model survives save/load (verts + tris + optional uvs).
        if (ch->HasCustomBind()) {
            const Mesh& bm = ch->CustomBindMesh();
            out << "  charbind " << bm.vertices.size();
            for (const Vec3& v : bm.vertices) out << " " << v.x << " " << v.y << " " << v.z;
            out << " " << bm.triangles.size();
            for (int t : bm.triangles) out << " " << t;
            out << " " << (bm.uvs.size() == bm.vertices.size() ? bm.uvs.size() : 0);
            if (bm.uvs.size() == bm.vertices.size())
                for (const Vec2& t : bm.uvs) out << " " << t.x << " " << t.y;
            out << "\n";
            // A rigged model keeps its material/texture — Character objects skip
            // the normal mesh/material records, so write one here (same format).
            if (!wroteCharacterMaterial)
                if (auto* mr = LastOf(go->GetComponents<MeshRenderer>())) {
                    wroteCharacterMaterial = true;
                    out << "  material " << mr->emissive.r << " " << mr->emissive.g << " "
                        << mr->emissive.b << " " << mr->specular << " " << mr->shininess << " "
                        << (mr->unlit ? 1 : 0) << " " << Quote(mr->texture) << " "
                        << mr->tiling.x << " " << mr->tiling.y << "\n";
                }
        }
    }
    for (auto* li : go->GetComponents<Light>()) {   // every instance (a lamp with a point and a spot light)
        out << "  light " << li->color.r << " " << li->color.g << " " << li->color.b << " "
            << li->color.a << " " << li->ambient << " " << li->intensity
            << " " << (int)li->type << " " << li->range << " " << li->spotAngle
            << " " << li->spotSoftness << " " << (li->useTemperature ? 1 : 0) << " " << li->temperature
            << " " << li->ambientColor.r << " " << li->ambientColor.g << " " << li->ambientColor.b
            << " " << (int)li->falloff;   // trailing (back-compatible)
        // Shadow/flicker/enabled block: only when something differs from the
        // defaults, so plain lights still load in older engines.
        if (!li->castShadows || li->shadowStrength != 1.0f || li->shadowBias != 1.0f ||
            li->flicker != 0.0f || li->flickerSpeed != 8.0f || !li->enabled)
            out << " " << (li->castShadows ? 1 : 0) << " " << li->shadowStrength << " " << li->shadowBias
                << " " << li->flicker << " " << li->flickerSpeed << " " << (li->enabled ? 1 : 0);
        out << "\n";
        // Newer light settings: an append-only record of its own for the light just written, only when something differs
        // from the defaults. Older builds keep it as an unknown record.
        if (li->shadowSoftness != 0.5f || li->areaWidth != 0.0f || li->areaHeight != 0.0f || !li->cookie.empty() ||
            li->cookieScale != 4.0f || li->unit != Light::Unit::Simple)
            out << "  lightx " << li->shadowSoftness << " " << li->areaWidth << " " << li->areaHeight << " "
                << li->cookieScale << " " << (int)li->unit << " " << Quote(li->cookie) << "\n";
    }
    for (auto* rb : go->GetComponents<Rigidbody2D>()) {
        out << "  rigidbody2d " << (int)rb->bodyType << " " << rb->gravityScale << " "
            << rb->mass << " " << rb->drag << " " << rb->bounciness
            << " " << rb->friction                                    // trailing (back-compatible)
            << " " << rb->angularDrag << " " << (rb->freezeRotation ? 1 : 0)
            << " " << (rb->allowSleep ? 1 : 0)
            << " " << (rb->continuousCollision ? 1 : 0)
            << " " << rb->rollingResistance << "\n";   // trailing (back-compatible)
    }
    for (auto* bc : go->GetComponents<BoxCollider2D>()) {   // every instance: two colliders make a compound shape
        out << "  boxcollider2d " << bc->size.x << " " << bc->size.y << " "
            << bc->offset.x << " " << bc->offset.y << " " << (bc->isTrigger ? 1 : 0)
            << " " << bc->layer << " " << (bc->autoFit ? 1 : 0) << "\n";
    }
    for (auto* cc : go->GetComponents<CircleCollider2D>()) {   // every instance: two colliders make a compound shape
        out << "  circlecollider2d " << cc->radius << " "
            << cc->offset.x << " " << cc->offset.y << " " << (cc->isTrigger ? 1 : 0)
            << " " << cc->layer << " " << (cc->autoFit ? 1 : 0) << "\n";
    }
    for (auto* cap : go->GetComponents<CapsuleCollider2D>()) {   // every instance: two colliders make a compound shape
        out << "  capsulecollider2d " << cap->size.x << " " << cap->size.y << " "
            << (int)cap->direction << " " << cap->offset.x << " " << cap->offset.y << " "
            << (cap->isTrigger ? 1 : 0) << " " << cap->layer << " " << (cap->autoFit ? 1 : 0) << "\n";
    }
    for (auto* ec : go->GetComponents<EdgeCollider2D>()) {   // every instance: two colliders make a compound shape
        out << "  edgecollider2d " << ec->offset.x << " " << ec->offset.y << " "
            << (ec->isTrigger ? 1 : 0) << " " << ec->layer << " "
            << (ec->oneWay ? 1 : 0) << " " << ec->oneWayNormal.x << " " << ec->oneWayNormal.y
            << " " << ec->points.size();
        for (const Vec2& p : ec->points) out << " " << p.x << " " << p.y;
        out << "\n";
    }
    for (auto* pc : go->GetComponents<PolygonCollider2D>()) {   // every instance: two colliders make a compound shape
        out << "  polygoncollider2d " << pc->offset.x << " " << pc->offset.y << " "
            << (pc->isTrigger ? 1 : 0) << " " << pc->layer
            << " " << pc->points.size();
        for (const Vec2& p : pc->points) out << " " << p.x << " " << p.y;
        out << "\n";
    }
    // Trigger tag / layer filter. One record when every collider shares it (older scenes
    // read that as applying to all). Differing filters are one indexed record each.
    WriteTriggerFilters(out, "trigfilter2d", LegacyColliders2D(*go));
    WritePhysicsMaterials(out, "physmat2d", LegacyColliders2D(*go));
    WriteContactOffsets(out, "colskin2d", LegacyColliders2D(*go));
    for (auto* rb : go->GetComponents<Rigidbody3D>()) {
        out << "  rigidbody3d " << (int)rb->bodyType << " " << rb->gravityScale << " "
            << rb->mass << " " << rb->drag << " " << rb->bounciness << " "
            << (rb->freezeX ? 1 : 0) << " " << (rb->freezeY ? 1 : 0) << " "
            << (rb->freezeZ ? 1 : 0)
            << " " << rb->maxFallSpeed
            << " " << rb->friction                                    // trailing (back-compatible)
            << " " << rb->angularDrag << " " << (rb->freezeRotation ? 1 : 0)
            << " " << (rb->allowSleep ? 1 : 0)
            << " " << (rb->continuousCollision ? 1 : 0)
            << " " << rb->rollingResistance << " " << (rb->compoundChildren ? 1 : 0) << "\n";   // trailing (back-compatible)
    }
    for (auto* j : go->GetComponents<Joint3D>()) {   // every instance: a chain link holds two joints
        out << "  joint3d " << j->mode << " " << Quote(j->connectedBody)
            << " " << j->anchor.x << " " << j->anchor.y << " " << j->anchor.z
            << " " << j->distance << " " << (j->autoConfigure ? 1 : 0)
            << " " << j->spring << " " << j->damper
            << " " << (j->breakable ? 1 : 0) << " " << j->breakForce
            << " " << j->axis.x << " " << j->axis.y << " " << j->axis.z     // hinge (trailing)
            << " " << (j->useMotor ? 1 : 0) << " " << j->motorSpeed << " " << j->maxMotorTorque
            << " " << (j->useLimits ? 1 : 0) << " " << j->minAngle << " " << j->maxAngle;
        {   // ball / cone twist fields (trailing), written only when one differs from the default: old scenes save as before
            const Joint3D d;
            if (j->swingLimit1 != d.swingLimit1 || j->swingLimit2 != d.swingLimit2 || j->useDrive != d.useDrive ||
                !(j->driveTarget == d.driveTarget) || j->driveStiffness != d.driveStiffness || j->driveDamping != d.driveDamping ||
                j->driveFrequency != d.driveFrequency || j->driveDampingRatio != d.driveDampingRatio || j->driveMaxTorque != d.driveMaxTorque)
                out << " " << j->swingLimit1 << " " << j->swingLimit2 << " " << (j->useDrive ? 1 : 0)
                    << " " << j->driveTarget.x << " " << j->driveTarget.y << " " << j->driveTarget.z
                    << " " << j->driveStiffness << " " << j->driveDamping << " " << j->driveFrequency
                    << " " << j->driveDampingRatio << " " << j->driveMaxTorque;
        }
        out << "\n";
    }
    for (auto* j : go->GetComponents<Joint2D>()) {   // every instance: a chain link holds two joints
        out << "  joint2d " << j->mode << " " << Quote(j->connectedBody)
            << " " << j->anchor.x << " " << j->anchor.y
            << " " << j->distance << " " << (j->autoConfigure ? 1 : 0)
            << " " << j->spring << " " << j->damper
            << " " << (j->breakable ? 1 : 0) << " " << j->breakForce
            << " " << (j->useMotor ? 1 : 0) << " " << j->motorSpeed << " " << j->maxMotorTorque   // hinge (trailing)
            << " " << (j->useLimits ? 1 : 0) << " " << j->minAngle << " " << j->maxAngle << "\n";
    }
    for (auto* a : go->GetComponents<AimIK>()) {
        out << "  aimik " << Quote(a->boneName) << " " << Quote(a->targetName)
            << " " << a->aimAxis.x << " " << a->aimAxis.y << " " << a->aimAxis.z
            << " " << a->upAxis.x << " " << a->upAxis.y << " " << a->upAxis.z
            << " " << a->weight << " " << a->maxAngle
            << " " << a->target.x << " " << a->target.y << " " << a->target.z
            << " " << a->smoothing << "\n";
    }
    for (auto* t : go->GetComponents<IKTarget>()) {
        out << "  iktarget " << t->limb << " " << Quote(t->characterName)
            << " " << t->weight << " " << t->blendTime << "\n";
    }
    for (auto* ao : go->GetComponents<AimOffset>()) {
        out << "  aimoffset " << (ao->useCamera ? 1 : 0) << " " << Quote(ao->targetName)
            << " " << ao->weight << " " << ao->pitchClamp << " " << ao->yawClamp
            << " " << ao->spineWeight << " " << ao->chestWeight << " " << ao->armWeight
            << " " << ao->kick << " " << ao->recovery << "\n";
    }
    for (auto* pc : go->GetComponents<PoseCapture>()) {
        out << "  posecap " << Quote(pc->clipName) << " " << pc->recordSeconds << "\n";
    }
    for (auto* l : go->GetComponents<LookAtIK>()) {
        out << "  lookatik " << Quote(l->targetName)
            << " " << l->forwardAxis.x << " " << l->forwardAxis.y << " " << l->forwardAxis.z
            << " " << l->weight << " " << l->maxAngle
            << " " << l->target.x << " " << l->target.y << " " << l->target.z
            << " " << l->chainNames.size();
        for (const std::string& n : l->chainNames) out << " " << Quote(n);
        out << " " << l->smoothing;
        // The rest as key=value (older builds ignore trailing tokens here).
        KvOut k{out};
        k("clamp", l->clampAngle)("eyeMax", l->eyeMaxAngle)("eyeW", l->eyeWeight)
         ("maxDist", l->maxDistance)("fadeDist", l->fadeDistance);
        if (!l->aimAtPoint) k("point", 0.0f);
        for (std::size_t i = 0; i < l->boneWeights.size(); ++i) k(("sh" + std::to_string(i)).c_str(), l->boneWeights[i]);
        for (std::size_t i = 0; i < l->eyeNames.size(); ++i) k.str(("eye" + std::to_string(i)).c_str(), l->eyeNames[i]);
        out << "\n";
    }
    // Written BEFORE footik on purpose: components load in record order, and
    // the retarget must pose the rig before FootIK plants the feet on it.
    for (auto* hr : go->GetComponents<HumanoidRetarget>()) {
        out << "  humretarget " << hr->weight;
        if (hr->leanScale != 1.0f) out << " lean " << hr->leanScale;
        // Bones picked by hand: "map <slot> <name>" pairs after the weight (older
        // builds read the weight and ignore the rest of the line).
        for (int b = 0; b < humanoid::BoneCount; ++b)
            if (!hr->boneMap[b].empty()) out << " map " << b << " " << Quote(hr->boneMap[b]);
        out << "\n";
    }
    for (auto* f : go->GetComponents<FootIK>()) {
        out << "  footik " << Quote(f->leftHipName) << " " << Quote(f->leftKneeName) << " " << Quote(f->leftFootName)
            << " " << Quote(f->rightHipName) << " " << Quote(f->rightKneeName) << " " << Quote(f->rightFootName)
            << " " << Quote(f->pelvisName)
            << " " << f->weight << " " << f->footOffset << " " << (f->useRaycast ? 1 : 0) << " " << f->groundY
            << " " << (f->adjustPelvis ? 1 : 0) << " " << (f->plantDown ? 1 : 0) << " " << (f->alignToGround ? 1 : 0)
            << " " << f->minKneeBend << " " << f->maxKneeBend << " " << f->smoothing
            << " " << (f->orient ? 1 : 0);
        // The rest of the tuning, key=value at the end of the line: older builds
        // ignore trailing tokens here, older files lack them (defaults apply).
        KvOut k{out};
        k("rayUp", f->maxRayUp)("rayDown", f->maxRayDown)("pelvisShift", f->maxPelvisShift)
         ("upX", f->footUpAxis.x)("upY", f->footUpAxis.y)("upZ", f->footUpAxis.z)
         .b("lock", f->footLock)("lockSpeed", f->lockSpeed)("lockHeight", f->lockHeight)
         ("lockRelease", f->lockRelease)
         .b("probes", f->stepProbes)("toeR", f->toeReach)("heelR", f->heelReach)
         ("upRX", f->footUpAxisR.x)("upRY", f->footUpAxisR.y)("upRZ", f->footUpAxisR.z)
         ("om", 1)   // orient is saved after its migration: keep it as written
         ("mask", f->groundMask);
        WriteLimitKv(k, "hl", f->hipLimit); WriteLimitKv(k, "kl", f->kneeLimit);
        out << "\n";
    }
    for (auto* lb : go->GetComponents<LimbIK>()) {
        out << "  limbik " << Quote(lb->upperName) << " " << Quote(lb->lowerName) << " " << Quote(lb->endName)
            << " " << Quote(lb->targetName) << " " << Quote(lb->poleName)
            << " " << lb->weight << " " << lb->minBend << " " << lb->maxBend << " " << (lb->matchTargetRotation ? 1 : 0)
            << " " << lb->target.x << " " << lb->target.y << " " << lb->target.z
            << " " << lb->pole.x << " " << lb->pole.y << " " << lb->pole.z
            << " " << (lb->orient ? 1 : 0) << " " << (int)lb->poleMode << " " << lb->softness
            << " " << lb->targetRotation.x << " " << lb->targetRotation.y << " " << lb->targetRotation.z
            << " " << lb->targetRotation.w;
        // Optional joint limits as trailing key=value (older builds skip them).
        auto lim = [&out](const char* p, const IKJointLimit& l) {
            if (!l.Active()) return;
            out << ' ' << p << "T=" << (int)l.type << ' ' << p << "S=" << l.swingMax
                << ' ' << p << "H0=" << l.hingeMin << ' ' << p << "H1=" << l.hingeMax
                << ' ' << p << "W0=" << l.twistMin << ' ' << p << "W1=" << l.twistMax;
            if (!l.autoAxes)
                out << ' ' << p << "TX=" << l.twistAxis.x << ' ' << p << "TY=" << l.twistAxis.y << ' ' << p << "TZ=" << l.twistAxis.z
                    << ' ' << p << "HX=" << l.hingeAxis.x << ' ' << p << "HY=" << l.hingeAxis.y << ' ' << p << "HZ=" << l.hingeAxis.z;
        };
        lim("ul", lb->upperLimit); lim("ll", lb->lowerLimit);
        if (lb->targetBlendTime != 0.0f) out << " tb=" << lb->targetBlendTime;
        if (lb->stretch != 0.0f) out << " st=" << lb->stretch;
        out << "\n";
    }
    for (auto* ci : go->GetComponents<ChainIK>()) {
        out << "  chainik " << Quote(ci->targetName) << " " << ci->weight << " " << ci->iterations
            << " " << ci->solver << " " << (ci->orient ? 1 : 0)
            << " " << ci->target.x << " " << ci->target.y << " " << ci->target.z
            << " " << ci->boneNames.size();
        for (const std::string& n : ci->boneNames) out << " " << Quote(n);
        out << "\n";
        // Pole mode/direction and per-joint limits (a newer record).
        bool anyLim = false;
        for (const auto& l : ci->limits) anyLim = anyLim || l.Active();
        if (ci->poleMode >= 0 || anyLim || ci->targetBlendTime != 0.0f) {
            out << "  chainx2 " << ci->poleMode
                << " " << ci->poleDir.x << " " << ci->poleDir.y << " " << ci->poleDir.z
                << " " << (anyLim ? ci->limits.size() : 0);
            if (anyLim) for (const auto& l : ci->limits) {
                out << " " << (int)l.type << " " << l.swingMax << " " << l.hingeMin << " " << l.hingeMax
                    << " " << l.twistMin << " " << l.twistMax << " " << (l.autoAxes ? 1 : 0);
                if (!l.autoAxes)
                    out << " " << l.twistAxis.x << " " << l.twistAxis.y << " " << l.twistAxis.z
                        << " " << l.hingeAxis.x << " " << l.hingeAxis.y << " " << l.hingeAxis.z;
            }
            if (ci->targetBlendTime != 0.0f) out << " tb=" << ci->targetBlendTime;
            out << "\n";
        }
    }
    // IK fade + ChainIK constraints (separate records so older builds still load).
    auto ikFade = [&](const char* kind, bool active, float fade) {
        out << "  ikx " << kind << " " << (active ? 1 : 0) << " " << fade;
    };
    for (auto* a : go->GetComponents<AimIK>())    { ikFade("aim", a->ikActive, a->fadeTime); out << "\n"; }
    for (auto* l : go->GetComponents<LookAtIK>()) { ikFade("look", l->ikActive, l->fadeTime); out << "\n"; }
    for (auto* lb : go->GetComponents<LimbIK>())  { ikFade("limb", lb->ikActive, lb->fadeTime); out << "\n"; }
    for (auto* ci : go->GetComponents<ChainIK>()) {
        ikFade("chain", ci->ikActive, ci->fadeTime);
        out << " " << Quote(ci->poleName) << " " << (ci->usePole ? 1 : 0) << " " << ci->pole.x << " " << ci->pole.y
            << " " << ci->pole.z << " " << ci->maxJointBend << " " << ci->tolerance << "\n";
    }
    for (auto* fb : go->GetComponents<FullBodyIK>()) {
        out << "  fbik " << fb->weight << " " << (fb->ikActive ? 1 : 0) << " " << fb->fadeTime << " " << fb->softness
            << " " << fb->bodyPull << " " << fb->maxBodyOffset << " " << fb->spineReach << " " << fb->maxSpineAngle
            << " " << (fb->autoMap ? 1 : 0)
            << " " << (fb->lookEnabled ? 1 : 0) << " " << Quote(fb->lookTargetName) << " " << fb->lookPosition.x << " "
            << fb->lookPosition.y << " " << fb->lookPosition.z << " " << fb->lookWeight << " " << fb->lookHeadShare << " "
            << fb->maxLookAngle << " " << fb->headForward.x << " " << fb->headForward.y << " " << fb->headForward.z
            << " " << (int)FullBodyIK::BoneCount;
        for (const auto& n : fb->boneNames) out << " " << Quote(n);
        out << " " << (int)FullBodyIK::LimbCount;
        for (const auto& e : fb->effectors)
            out << " " << (e.enabled ? 1 : 0) << " " << Quote(e.targetName) << " " << e.position.x << " " << e.position.y
                << " " << e.position.z << " " << e.positionWeight << " " << e.rotationWeight << " " << Quote(e.hintName) << " " << e.pull;
        out << "\n";
        // Easing, smoothing and joint limits (separate record: older builds skip it).
        out << "  fbikx " << fb->blendSpeed << " " << fb->goalSmoothing << " " << fb->lookSmoothing
            << " " << fb->armMinBend << " " << fb->legMinBend;
        // Per-limb joint limits (e<limb>u / e<limb>l keys), only when set.
        KvOut k{out};
        for (int l = 0; l < FullBodyIK::LimbCount; ++l) {
            const std::string p = "e" + std::to_string(l);
            WriteLimitKv(k, p + "u", fb->effectors[l].upperLimit);
            WriteLimitKv(k, p + "l", fb->effectors[l].lowerLimit);
            if (fb->effectors[l].targetBlendTime != 0.0f) k((p + "b").c_str(), fb->effectors[l].targetBlendTime);
        }
        if (fb->stretch != 0.0f) k("st", fb->stretch);
        out << "\n";
    }
    for (auto* h : go->GetComponents<HoldableObject>()) {
        out << "  holdable";
        KvOut k{out};
        k("style", (int)h->style).b("support", h->supportHand).b("two", h->twoHanded)("weight", h->weight).str("socket", h->socket)
         ("offX", h->holdOffset.x)("offY", h->holdOffset.y)("offZ", h->holdOffset.z)
         ("rotX", h->holdRotation.x)("rotY", h->holdRotation.y)("rotZ", h->holdRotation.z)
         ("handScale", h->inHandScale)
         ("fpX", h->fpOffset.x)("fpY", h->fpOffset.y)("fpZ", h->fpOffset.z)
         ("fpRX", h->fpRotation.x)("fpRY", h->fpRotation.y)("fpRZ", h->fpRotation.z)("fpS", h->fpScale)
         ("tpX", h->tpOffset.x)("tpY", h->tpOffset.y)("tpZ", h->tpOffset.z)
         ("tpRX", h->tpRotation.x)("tpRY", h->tpRotation.y)("tpRZ", h->tpRotation.z);
        const HoldableObject::Grip* grips[2] = {&h->leftGrip, &h->rightGrip};
        for (int g = 0; g < 2; ++g) {
            const std::string p = g == 0 ? "l" : "r";
            const HoldableObject::Grip& gr = *grips[g];
            k.b((p + "On").c_str(), gr.enabled)
             ((p + "PX").c_str(), gr.position.x)((p + "PY").c_str(), gr.position.y)((p + "PZ").c_str(), gr.position.z)
             ((p + "RX").c_str(), gr.rotation.x)((p + "RY").c_str(), gr.rotation.y)((p + "RZ").c_str(), gr.rotation.z)
             ((p + "Curl").c_str(), gr.curl);
        }
        out << "\n";
    }
    for (auto* ph : go->GetComponents<ProceduralObjectHolding>()) {
        out << "  objholding";
        KvOut k{out};
        k("hand", (int)ph->primaryHand)("style", (int)ph->holdingStyle).b("armIK", ph->armIK)("transition", ph->transitionTime)
         .b("body", ph->bodyAdjustment)("carry", (int)ph->carryAnimations)
         .key("throwKey", ph->throwKey)("throwMouse", ph->throwMouseButton)("throwPad", ph->throwGamepadButton)
         ("charge", ph->throwChargeTime)("tmin", ph->throwMinSpeed)("tmax", ph->throwMaxSpeed)("reach", ph->pickupRange);
        out << "\n";
    }
    for (auto* hp : go->GetComponents<HoldPrompt>()) {
        out << "  holdprompt";
        KvOut k{out};
        k.key("drop", hp->dropKey).key("alt", hp->altDropKey).str("prompt", hp->promptObject);
        out << "\n";
    }
    for (auto* rm : go->GetComponents<RootMotion>()) {
        out << "  rootmotion " << Quote(rm->rootNodeName) << " " << rm->mode
            << " " << (rm->lockHeight ? 1 : 0) << " " << (rm->applyToRigidbody ? 1 : 0)
            << " " << rm->loopThreshold << "\n";
    }
    // Mesh + Cylinder colliders derive from Box/Capsule, so write them first and guard
    // the base records by exact shape() (else GetComponent<BoxCollider3D> matches a mesh).
    for (auto* mc : go->GetComponents<MeshCollider3D>()) {
        out << "  meshcollider3d " << mc->size.x << " " << mc->size.y << " " << mc->size.z << " "
            << mc->offset.x << " " << mc->offset.y << " " << mc->offset.z << " "
            << (mc->isTrigger ? 1 : 0) << " " << mc->layer << " " << (mc->autoFit ? 1 : 0) << "\n";
    }
    for (auto* cy : go->GetComponents<CylinderCollider3D>()) {
        out << "  cylindercollider3d " << cy->radius << " " << cy->height << " " << cy->axis << " "
            << cy->offset.x << " " << cy->offset.y << " " << cy->offset.z << " "
            << (cy->isTrigger ? 1 : 0) << " " << cy->layer << " " << (cy->autoFit ? 1 : 0) << "\n";
    }
    // A crouched / prone player has a shrunken collider; save the standing one.
    // When both view controllers are present, the one that isn't running still
    // has a standing record — save the body the active view actually resized.
    const StanceBody* stance = nullptr;
    auto* fpcStance = go->GetComponent<FirstPersonController>();
    auto* tpcStance = go->GetComponent<ThirdPersonController>();
    auto captured = [](const FirstPersonController* c) { return c && c->stanceBody().captured; };
    auto capturedTp = [](const ThirdPersonController* c) { return c && c->stanceBody().captured; };
    if (fpcStance && tpcStance) {
        const bool fp = FirstPersonOwns(go);
        if (fp && captured(fpcStance)) stance = &fpcStance->stanceBody();
        else if (!fp && capturedTp(tpcStance)) stance = &tpcStance->stanceBody();
    }
    if (!stance && captured(fpcStance)) stance = &fpcStance->stanceBody();
    if (!stance && capturedTp(tpcStance)) stance = &tpcStance->stanceBody();
    if (!stance && fpcStance) stance = &fpcStance->stanceBody();
    if (!stance && tpcStance) stance = &tpcStance->stanceBody();
    if (!stance) {
        if (auto* p25 = go->GetComponent<Platformer25DController>()) stance = &p25->stanceBody();
        else if (auto* c3 = go->GetComponent<CharacterController3D>()) stance = &c3->stanceControl.Body();
        else if (auto* tps = go->GetComponent<ThirdPersonShooterController>()) stance = &tps->stanceControl.Body();
        else if (auto* tdc = go->GetComponent<TopDownController>()) stance = &tdc->stanceControl.Body();
        else if (auto* ctm = go->GetComponent<ClickToMoveController>()) stance = &ctm->stanceControl.Body();
        else if (auto* npc = go->GetComponent<NPCController>()) stance = &npc->stanceControl.Body();
    }
    const bool resized = stance && stance->captured && std::fabs(stance->curH - stance->standH) > 1e-4f;
    const float standOffY = resized ? stance->feetY + stance->standH * 0.5f : 0.0f;
    bool firstBox = true, firstCapsule = true;   // a crouched body resizes the first one only, as before
    for (auto* bc : go->GetComponents<BoxCollider3D>()) {
        if (bc->shape() != Collider3D::Shape::Box) continue;   // a Mesh collider derives from Box: its own record above
        const bool rs = resized && firstBox;
        firstBox = false;
        out << "  boxcollider3d " << bc->size.x << " " << (rs ? stance->standH : bc->size.y) << " " << bc->size.z << " "
            << bc->offset.x << " " << (rs ? standOffY : bc->offset.y) << " " << bc->offset.z << " "
            << (bc->isTrigger ? 1 : 0) << " " << bc->layer << " " << (bc->autoFit ? 1 : 0) << "\n";
    }
    for (auto* sc : go->GetComponents<SphereCollider3D>()) {
        out << "  spherecollider3d " << sc->radius << " "
            << sc->offset.x << " " << sc->offset.y << " " << sc->offset.z << " "
            << (sc->isTrigger ? 1 : 0) << " " << sc->layer << " " << (sc->autoFit ? 1 : 0) << "\n";
    }
    for (auto* cap : go->GetComponents<CapsuleCollider3D>()) {
        if (cap->shape() != Collider3D::Shape::Capsule) continue;   // a Cylinder collider derives from Capsule: its own record above
        const bool rs = resized && firstCapsule;
        firstCapsule = false;
        out << "  capsulecollider3d " << cap->radius << " " << (rs ? stance->standH : cap->height) << " " << cap->axis << " "
            << cap->offset.x << " " << (rs ? standOffY : cap->offset.y) << " " << cap->offset.z << " "
            << (cap->isTrigger ? 1 : 0) << " " << cap->layer << " " << (cap->autoFit ? 1 : 0) << "\n";
    }
    // See the 2D filter: one shared record, or one indexed record per collider when they differ.
    WriteTriggerFilters(out, "trigfilter3d", LegacyColliders3D(*go));
    WritePhysicsMaterials(out, "physmat3d", LegacyColliders3D(*go));
    WriteContactOffsets(out, "colskin3d", LegacyColliders3D(*go));
    // Each script is one self-contained line (lang, source, then optional path
    // and field overrides) so a GameObject can carry several scripts.
    for (auto* sc : go->GetComponents<ScriptComponent>()) {
        out << "  script " << Quote(sc->Language()) << " " << Quote(sc->Source())
            << " " << Quote(sc->Path()) << " " << sc->fields.size();
        for (const auto& kv : sc->fields) out << " " << Quote(kv.first) << " " << Quote(kv.second);
        out << "\n";
    }
    // Every graph on the object (each `visualscriptpath` belongs to the graph above it).
    for (auto* vsc : go->GetComponents<VisualScriptComponent>()) {
        out << "  visualscript " << Quote(vsc->Source()) << "\n";
        if (!vsc->Path().empty()) out << "  visualscriptpath " << Quote(vsc->Path()) << "\n";
    }
    for (auto* al : go->GetComponents<ActionList>()) {
        out << "  actions " << Quote(al->ToText()) << "\n";
    }
    for (auto* mv : go->GetComponents<Mover>()) {
        out << "  mover " << mv->velocity.x << " " << mv->velocity.y << " " << mv->velocity.z;
        if (mv->space != Space::Local) out << " " << static_cast<int>(mv->space);   // optional trailing field
        out << "\n";
    }
    for (auto* cc : go->GetComponents<CharacterController2D>()) {
        out << "  charctrl2d " << (int)cc->mode << " " << cc->speed << " " << cc->jumpForce
            // extended (optional, back-compatible trailing fields)
            << " " << cc->runSpeed << " " << KeyToken(cc->sprintKey)
            << " " << cc->acceleration << " " << cc->deceleration
            << " " << (cc->normalizeDiagonal ? 1 : 0) << " " << (cc->useGamepad ? 1 : 0)
            << " " << (cc->flipSprite ? 1 : 0) << " " << cc->maxJumps
            << " " << (cc->variableJump ? 1 : 0) << " " << cc->jumpCutMultiplier
            << " " << cc->coyoteTime << " " << cc->jumpBuffer << " " << cc->airControl
            << " " << cc->maxFallSpeed << " " << cc->extraFallGravity
            << " " << (cc->wallJump ? 1 : 0) << " " << cc->wallSlideSpeed
            << " " << cc->wallJumpForce << " " << cc->wallJumpPush << "\n";
    }
    for (auto* cc : go->GetComponents<CharacterController3D>()) {
        out << "  charctrl3d " << cc->speed << " " << cc->jumpForce << " " << (cc->canJump ? 1 : 0)
            << " " << cc->runSpeed << " " << (int)(unsigned char)cc->sprintKey         // trailing (back-compatible)
            << " " << cc->acceleration << " " << cc->deceleration << " " << cc->airControl
            << " " << (cc->driveAnimation ? 1 : 0) << " " << (cc->footIK ? 1 : 0) << "\n";
    }
    for (auto* v : go->GetComponents<VehicleController>()) {
        out << "  vehicle " << v->acceleration << " " << v->maxSpeed << " " << v->reverseSpeed
            << " " << v->brakeForce << " " << v->drag << " " << v->turnSpeed << " " << v->grip
            << " " << v->handbrakeGrip << " " << v->groundCheckDistance
            << " " << (v->followCamera ? 1 : 0) << " " << v->camDistance << " " << v->camHeight
            << " " << v->camLerp << " " << (int)(unsigned char)v->handbrakeKey << "\n";
        {   // the open-world settings that differ from the defaults, plus the wheel spacing (the model steers by it; it used to be saved only with suspension)
            std::ostringstream tl; tl << v->ToLine();
            if (std::fabs(v->wheelBase - 2.4f) > 1e-4f) tl << "wheelBase=" << v->wheelBase << " ";
            if (std::fabs(v->trackWidth - 1.6f) > 1e-4f) tl << "trackWidth=" << v->trackWidth << " ";
            if (!tl.str().empty()) out << "  vehicletune " << tl.str() << "\n";
        }   // the open-world settings that differ from the defaults
        if (v->suspension)
            out << "  vehiclesusp 1 " << v->rideHeight << " " << v->springStrength << " "
                << v->springDamping << " " << v->suspensionTravel << " " << v->wheelBase << " "
                << v->trackWidth << " " << v->bodyLean << " " << v->maxTilt << " " << v->tiltSmooth << "\n";
    }
    for (auto* bb : go->GetComponents<BlockBuilder>()) {
        out << "  blockbuilder " << bb->blockSize << " " << bb->reach << " "
            << bb->blockColor.r << " " << bb->blockColor.g << " " << bb->blockColor.b << " " << bb->blockColor.a
            << " " << bb->placeButton << " " << bb->removeButton
            << " " << Quote(bb->blockTag) << " " << Quote(bb->blockTexture)
            << " " << (bb->showPreview ? 1 : 0) << " " << (bb->showCrosshair ? 1 : 0)
            << " " << (bb->placeInAir ? 1 : 0) << "\n";
    }
    for (auto* sb : go->GetComponents<StructureBuilder>()) {
        out << "  structurebuilder " << (int)sb->piece << " " << sb->cellSize << " " << sb->wallHeight
            << " " << sb->slabThickness << " " << sb->wallThickness << " " << sb->pillarThickness
            << " " << sb->reach << " " << sb->rotSteps << " " << sb->placeButton << " " << sb->removeButton
            << " " << (sb->showPreview ? 1 : 0) << " " << (sb->showCrosshair ? 1 : 0)
            << " " << (sb->requireResources ? 1 : 0) << " " << sb->refundFraction
            << " " << sb->costWood << " " << sb->costStone << " " << sb->costMetal
            << " " << Quote(sb->structureTag) << " " << Quote(sb->structureTexture)
            << " " << Quote(sb->woodItem) << " " << Quote(sb->stoneItem) << " " << Quote(sb->metalItem) << "\n";
    }
    for (auto* bm : go->GetComponents<BuilderMode>()) {
        out << "  buildermode " << (int)bm->brush << " " << bm->gridSize << " " << (bm->snapToGrid ? 1 : 0)
            << " " << bm->reach << " " << bm->rotateStepDeg << " " << bm->scaleStep
            << " " << bm->minScale << " " << bm->maxScale
            << " " << bm->partColor.r << " " << bm->partColor.g << " " << bm->partColor.b << " " << bm->partColor.a
            << " " << bm->placeButton << " " << bm->removeButton
            << " " << (bm->parentToModel ? 1 : 0) << " " << (bm->brushHotkeys ? 1 : 0)
            << " " << (bm->showPreview ? 1 : 0) << " " << (bm->showCrosshair ? 1 : 0)
            << " " << Quote(bm->buildTag) << " " << Quote(bm->partTexture)
            << " " << Quote(bm->prefabPath) << " " << Quote(bm->modelName)
            << " " << Quote(bm->mapPath) << "\n";
    }
    for (auto* bp : go->GetComponents<BuildPiece>()) {
        out << "  buildpiece " << bp->tier << " " << bp->health << " " << bp->maxHealth
            << " " << bp->cost << " " << Quote(bp->material) << "\n";
    }
    // In-game build mode + logic links (key=value records: fields can be added freely).
    for (auto* b : go->GetComponents<Buildable>()) {
        out << "  buildable";
        KvOut k{out};
        k("id", b->buildId).b("edit", b->editable).b("placed", b->placed).str("prefab", b->prefab);
        out << "\n";
    }
    for (auto* bm : go->GetComponents<BuildMode>()) {
        const BuildBindings& kb = bm->bindings;
        out << "  buildmode";
        KvOut k{out};
        k("grid", bm->gridSize)("rot", bm->rotationStep)("reach", bm->maxReach)("layers", bm->buildLayers)
         ("max", bm->maxObjects).b("air", bm->allowAirPlacement).b("start", bm->startActive)
         .b("hotbar", bm->showHotbar).b("ghost", bm->showGhost).str("folder", bm->creationFolder)
         .col("ok", bm->validColor).col("bad", bm->invalidColor).col("hl", bm->highlightColor)
         .col("wire", bm->wireColor).col("wireOn", bm->wireOnColor)
         .key("kToggle", kb.toggle)("kPlace", kb.place)("kRemove", kb.remove).key("kRotate", kb.rotate)
         .key("kMove", kb.move).key("kDup", kb.duplicate).key("kLink", kb.link).key("kUndo", kb.undo)
         .key("kRedo", kb.redo).key("kNext", kb.nextItem).key("kPrev", kb.prevItem)
         .b("kNum", kb.numberKeys).b("kWheel", kb.mouseWheel).key("kRemoveKey", kb.removeKey)
         ("pToggle", kb.padToggle)("pPlace", kb.padPlace)("pRemove", kb.padRemove)("pRotate", kb.padRotate)
         ("pNext", kb.padNext)("pUndo", kb.padUndo)("pMove", kb.padMove)("pDup", kb.padDuplicate)
         ("pLink", kb.padLink)("pRedo", kb.padRedo)("pPrev", kb.padPrev)("pal", bm->palette.size());
        for (std::size_t i = 0; i < bm->palette.size(); ++i) {
            const std::string p = "p" + std::to_string(i);
            k.str((p + "n").c_str(), bm->palette[i].name).str((p + "f").c_str(), bm->palette[i].prefab)
             .str((p + "i").c_str(), bm->palette[i].icon);
        }
        out << "\n";
    }
    for (auto* ls : go->GetComponents<LogicSource>()) {
        std::string links;
        for (int id : ls->links) links += (links.empty() ? "" : ",") + std::to_string(id);
        out << "  logicsource";
        KvOut k{out};
        k("kind", (int)ls->kind).b("toggle", ls->toggle)("hold", ls->holdTime)("interval", ls->interval)
         ("on", ls->onTime).b("startOn", ls->startOn)
         ("zx", ls->zoneSize.x)("zy", ls->zoneSize.y)("zz", ls->zoneSize.z)
         ("ox", ls->zoneOffset.x)("oy", ls->zoneOffset.y)("oz", ls->zoneOffset.z)
         .str("tag", ls->activatorTag).key("key", ls->pressKey)("pad", ls->pressPadButton)
         ("reach", ls->pressReach).str("links", links);
        if (!ls->ioLinks.empty()) {
            k("io", ls->ioLinks.size());
            for (std::size_t i = 0; i < ls->ioLinks.size(); ++i) {
                const auto& l = ls->ioLinks[i];
                const std::string p = "io" + std::to_string(i);
                k.str((p + "o").c_str(), l.output).str((p + "t").c_str(), l.target)
                 .str((p + "i").c_str(), l.input).str((p + "p").c_str(), l.param)
                 ((p + "d").c_str(), l.delay).b((p + "x").c_str(), l.once).b((p + "e").c_str(), l.enabled);
            }
        }
        out << "\n";
    }
    for (auto* lt : go->GetComponents<LogicTarget>()) {
        out << "  logictarget";
        KvOut k{out};
        k("kind", (int)lt->kind)("mode", (int)lt->mode).b("invert", lt->invert)
         ("dx", lt->openOffset.x)("dy", lt->openOffset.y)("dz", lt->openOffset.z)("yaw", lt->openYaw)
         ("time", lt->moveTime)("light", lt->lightIntensity).col("glow", lt->onEmissive)
         .str("prefab", lt->spawnPrefab)("sx", lt->spawnOffset.x)("sy", lt->spawnOffset.y)
         ("sz", lt->spawnOffset.z)("alive", lt->maxAlive).str("event", lt->eventName);
        out << "\n";
    }
    for (auto* ls : go->GetComponents<LoadingScreen>()) {
        auto& bg = ls->backgroundColor; auto& tc = ls->textColor;
        auto& bb = ls->barBackground; auto& bf = ls->barFill;
        out << "  loadingscreen " << bg.r << " " << bg.g << " " << bg.b << " " << bg.a
            << " " << tc.r << " " << tc.g << " " << tc.b << " " << tc.a
            << " " << bb.r << " " << bb.g << " " << bb.b << " " << bb.a
            << " " << bf.r << " " << bf.g << " " << bf.b << " " << bf.a
            << " " << (ls->showBar ? 1 : 0) << " " << (ls->showTitle ? 1 : 0) << " " << ls->duration
            << " " << (ls->showOnStart ? 1 : 0) << " " << ls->fadeTime
            << " " << Quote(ls->title) << " " << Quote(ls->targetScene) << " " << Quote(ls->backgroundImage)
            << " " << ls->tips.size();
        for (const auto& t : ls->tips) out << " " << Quote(t);
        // Style block (appended; numeric-peek guarded on read).
        auto wc = [&](const Color& c) { out << " " << c.r << " " << c.g << " " << c.b << " " << c.a; };
        out << " " << (ls->gradientBackground ? 1 : 0); wc(ls->backgroundColor2);
        wc(ls->titleColor); out << " " << ls->titleScale << " " << ls->titleY;
        wc(ls->tipColor);   out << " " << ls->tipScale << " " << ls->tipY;
        out << " " << ls->barWidth << " " << ls->barHeight << " " << ls->barY << " " << ls->barRadius
            << " " << (ls->barBorder ? 1 : 0); wc(ls->barBorderColor);
        out << " " << (ls->showPercent ? 1 : 0) << " " << (ls->showSpinner ? 1 : 0); wc(ls->spinnerColor);
        out << " " << ls->spinnerRadius << " " << ls->spinnerDotSize << " " << ls->spinnerSpeed << " " << ls->spinnerY;
        // Layout block (appended later; numeric-peek guarded on read).
        out << " " << ls->layout << " " << ls->barStyle << " " << ls->spinnerStyle << " " << ls->logoSize
            << " " << (ls->logoPulse ? 1 : 0) << " " << (ls->kenBurns ? 1 : 0) << " " << ls->tipInterval;
        wc(ls->cardColor);
        out << " " << Quote(ls->subtitle) << " " << Quote(ls->logoImage);
        out << " " << (ls->asyncLoad ? 1 : 0) << " " << ls->objectsPerFrame;   // background loading (optional on read)
        out << "\n";
    }
    for (auto* ui : go->GetComponents<InventoryUI>()) {
        auto wc = [&](const Color& c) { out << " " << c.r << " " << c.g << " " << c.b << " " << c.a; };
        out << "  inventoryui " << ui->hotbarSlots << " " << ui->backpackRows << " " << ui->selected
            << " " << (int)(unsigned char)ui->toggleKey << " " << (ui->selectHotkeys ? 1 : 0)
            << " " << (ui->scrollSelect ? 1 : 0) << " " << ui->slotSize << " " << ui->slotGap
            << " " << (ui->darkenWhenOpen ? 1 : 0);
        wc(ui->slotColor); wc(ui->slotBorder); wc(ui->selectedColor); wc(ui->textColor); wc(ui->panelColor);
        out << " " << Quote(ui->iconFolder) << " " << (ui->dragItems ? 1 : 0)
            << " " << ui->marginX << " " << ui->marginY << " " << (ui->anchorTop ? 1 : 0)
            << " " << ui->cornerRadius << " " << ui->borderWidth << " " << ui->labelScale
            << " " << (ui->showCounts ? 1 : 0) << " " << (ui->showNames ? 1 : 0);
        wc(ui->countColor);
        out << " " << (ui->showPanel ? 1 : 0) << " " << ui->panelPad;
        wc(ui->hoverColor);
        out << " " << (ui->showSelectedName ? 1 : 0) << " " << ui->nameChars;
        // Features (appended; read with numeric-peek guards for back-compat).
        out << " " << (ui->showTooltips ? 1 : 0) << " " << (ui->slotNumbers ? 1 : 0)
            << " " << (int)(unsigned char)ui->sortKey;
        wc(ui->tooltipColor); wc(ui->tooltipText); wc(ui->numberColor);
        // Split / quick-move / rarity rules (appended; numeric-peek guarded).
        out << " " << (ui->splitRightClick ? 1 : 0) << " " << (ui->shiftQuickMove ? 1 : 0)
            << " " << ui->rarities.size();
        for (const auto& rr : ui->rarities) { out << " " << Quote(rr.item); wc(rr.color); }
        // Scaling / split / icon (appended; numeric-peek guarded).
        out << " " << (ui->scaleToScreen ? 1 : 0) << " " << ui->referenceHeight
            << " " << (ui->splitHalf ? 1 : 0) << " " << ui->iconInset;
        // Sort mode / auto-sort / drop key (appended; numeric-peek guarded).
        out << " " << (int)ui->sortMode << " " << (ui->sortOnClose ? 1 : 0)
            << " " << (int)(unsigned char)ui->dropKey;
        // Style: align / selected pop / empty + hotbar colours / panel border / title
        // (appended; numeric-peek + quote guarded on read).
        out << " " << (int)ui->hAlign << " " << ui->selectedScale;
        wc(ui->emptySlotColor); wc(ui->hotbarColor); wc(ui->panelBorder);
        out << " " << ui->panelBorderWidth << " " << (ui->showTitle ? 1 : 0);
        wc(ui->titleColor);
        out << " " << Quote(ui->backpackTitle);
        out << " " << Quote(ui->inventoryTarget);   // appended (quote-peek guarded)
        out << " " << (int)(unsigned char)ui->useKey << " " << (ui->clickToUse ? 1 : 0);   // appended (numeric-peek guarded)
        out << "\n";
    }
    for (auto* gi : go->GetComponents<GridInventory>()) {
        out << "  gridinventory " << gi->cols << " " << gi->rows
            << " " << (int)(unsigned char)gi->toggleKey << " " << Quote(gi->title) << " " << gi->items.size();
        for (const auto& it : gi->items)
            out << " " << Quote(it.name) << " " << it.w << " " << it.h << " " << it.x
                << " " << it.y << " " << it.count << " " << it.weight;
        out << " " << gi->weightLimit;   // appended (numeric-peek guarded on read)
        out << " " << Quote(gi->category) << " " << (gi->worldItem ? 1 : 0);   // Unturned role (guarded)
        out << " " << (gi->enforceWeight ? 1 : 0) << " " << gi->maxStack      // appended (numeric-peek guarded)
            << " " << gi->overflow.size();
        for (const auto& it : gi->overflow)
            out << " " << Quote(it.name) << " " << it.w << " " << it.h << " " << it.count << " " << it.weight;
        WriteInstanceData(out, gi->items.size(), [&](std::size_t i) -> const ItemInstanceData& { return gi->items[i].data; });
        out << " " << (gi->keepOnSceneLoad ? 1 : 0);   // appended (numeric-peek guarded)
        out << "\n";
    }
    for (auto* gu : go->GetComponents<GridInventoryUI>()) {
        auto wc = [&](const Color& c) { out << " " << c.r << " " << c.g << " " << c.b << " " << c.a; };
        out << "  gridinventoryui " << gu->cellSize << " " << gu->gap
            << " " << (gu->darkenWhenOpen ? 1 : 0) << " " << (gu->showWeight ? 1 : 0);
        wc(gu->panelColor); wc(gu->cellColor); wc(gu->itemColor); wc(gu->itemBorder); wc(gu->textColor);
        out << " " << Quote(gu->iconFolder) << " " << gu->cornerRadius;
        wc(gu->titleBar); wc(gu->hoverColor); wc(gu->dropOk); wc(gu->dropBad);
        // Features (appended; read with numeric-peek guards for back-compat).
        out << " " << (gu->showTooltips ? 1 : 0) << " " << (int)(unsigned char)gu->rotateKey;
        wc(gu->tooltipColor); wc(gu->tooltipText);
        // Overweight colour + rarity rules (appended; numeric-peek guarded).
        wc(gu->overweightColor);
        out << " " << gu->rarities.size();
        for (const auto& rr : gu->rarities) { out << " " << Quote(rr.item); wc(rr.color); }
        // Unturned multi-container screen (appended; numeric-peek guarded on read).
        out << " " << (gu->multiContainer ? 1 : 0) << " " << gu->nearbyRange << " " << Quote(gu->nearbyTitle);
        // Layout + style customisation (appended; quote-peek guarded on read).
        out << " " << gu->columnGap << " " << gu->panelPad << " " << gu->containerGap << " " << gu->headerHeight
            << " " << (gu->showMasterTitle ? 1 : 0) << " " << Quote(gu->masterTitle)
            << " " << (gu->useGradients ? 1 : 0) << " " << (gu->dropShadows ? 1 : 0)
            << " " << (gu->accentBars ? 1 : 0) << " " << (gu->autoAccent ? 1 : 0);
        wc(gu->accentColor); wc(gu->headerColor); wc(gu->weightGood); wc(gu->weightWarn);
        out << " " << gu->titleScale << " " << gu->itemNameScale
            << " " << (gu->showItemNames ? 1 : 0) << " " << gu->backdropDim;
        out << "\n";
    }
    for (auto* cs : go->GetComponents<CraftingStation>()) {
        auto wc = [&](const Color& c) { out << " " << c.r << " " << c.g << " " << c.b << " " << c.a; };
        out << "  craftingstation 0 " << (int)(unsigned char)cs->toggleKey   // 0: `open` is runtime state, not saved
            << " " << Quote(cs->title) << " " << (cs->closeWhenCrafted ? 1 : 0)
            << " " << cs->rowHeight << " " << cs->width << " " << cs->marginX << " " << cs->marginY
            << " " << cs->cornerRadius << " " << Quote(cs->iconFolder);
        wc(cs->panelColor); wc(cs->titleBar); wc(cs->rowColor); wc(cs->canColor);
        wc(cs->cantColor); wc(cs->textColor); wc(cs->hoverColor);
        out << " " << cs->recipes.size();
        for (const auto& r : cs->recipes) {
            out << " " << r.inputs.size();
            for (const auto& in : r.inputs) out << " " << Quote(in.item) << " " << in.count;
            out << " " << Quote(r.output) << " " << r.outputCount;
        }
        out << " " << cs->range << " " << Quote(cs->playerName);   // appended (numeric-peek guarded)
        out << " " << Quote(cs->recipeBook) << " " << Quote(cs->stationTag);   // appended (quote-peek guarded)
        WriteRecipeExtras(out, cs->recipes);
        out << "\n";
    }
    for (auto* ch : go->GetComponents<ChestInventory>()) {
        auto wc = [&](const Color& c) { out << " " << c.r << " " << c.g << " " << c.b << " " << c.a; };
        out << "  chestinventory 0 " << (int)(unsigned char)ch->openKey   // 0: `open` is runtime state, not saved
            << " " << ch->range << " " << Quote(ch->title) << " " << ch->cols
            << " " << ch->slotSize << " " << ch->gap << " " << ch->cornerRadius
            << " " << Quote(ch->iconFolder) << " " << (ch->darkenWhenOpen ? 1 : 0);
        wc(ch->panelColor); wc(ch->titleBar); wc(ch->slotColor); wc(ch->slotBorder);
        wc(ch->textColor); wc(ch->hoverColor);
        out << " " << Quote(ch->playerName);   // appended (quote-peek guarded)
        out << "\n";
    }
    for (auto* ni : go->GetComponents<NetInventory>()) {
        // New record. Old scenes have no line, so they still load.
        out << "  netinventory " << ni->range << " " << ni->maxPerSecond << " " << ni->cheatThreshold
            << " " << ni->maxRequestBytes << " " << (ni->hideUntilOpen ? 1 : 0) << " " << Quote(ni->saveDir) << "\n";
    }
    for (auto* v2 : go->GetComponents<VehicleController2D>()) {
        out << "  vehicle2d " << v2->acceleration << " " << v2->maxSpeed << " " << v2->reverseSpeed
            << " " << v2->brakeForce << " " << v2->drag << " " << v2->turnSpeed << " " << v2->grip
            << " " << v2->handbrakeGrip << " " << (v2->sideView ? 1 : 0)
            << " " << (int)(unsigned char)v2->handbrakeKey << "\n";
    }
    for (auto* sv : go->GetComponents<SurvivalStats>()) {
        out << "  survival " << sv->maxHealth << " " << sv->armor << " " << sv->regenWhenFed
            << " " << sv->regenDelay << " " << sv->maxHunger << " " << sv->hungerDrain << " "
            << sv->starveDamage << " " << sv->maxThirst << " " << sv->thirstDrain << " "
            << sv->dehydrateDamage << " " << sv->maxStamina << " " << sv->staminaRegen << " "
            << sv->sprintCost << " " << sv->sprintDrainMult << " " << sv->maxOxygen << " "
            << sv->oxygenDrain << " " << sv->oxygenRefill << " " << sv->drownDamage << " "
            << sv->maxWarmth << " " << sv->coldDrain << " " << sv->warmRegen << " "
            << sv->freezeDamage << " " << (sv->publishPrefs ? 1 : 0) << " "
            << (sv->publishBars ? 1 : 0) << " " << (sv->sendMessages ? 1 : 0)
            << " " << sv->resistance << " " << (sv->godMode ? 1 : 0) << " " << sv->invincibleTime << "\n";
    }
    auto statTail = [](std::ostream& o, StatComponent* c) {
        o << " " << (c->publishPrefs ? 1 : 0) << " " << (c->publishBar ? 1 : 0)
          << " " << (c->sendMessages ? 1 : 0) << "\n";
    };
    // The 3 shared toggles without the trailing newline, so extra fields can follow.
    auto statToggles = [](std::ostream& o, StatComponent* c) {
        o << " " << (c->publishPrefs ? 1 : 0) << " " << (c->publishBar ? 1 : 0)
          << " " << (c->sendMessages ? 1 : 0);
    };
    for (auto* c : go->GetComponents<HealthStat>()) {
        out << "  stat_health " << c->maxHealth << " " << c->armor << " " << c->regenPerSecond
            << " " << c->regenDelay << " " << c->lowThreshold;
        statToggles(out, c);
        out << " " << c->resistance << " " << c->minDamage << " " << (c->godMode ? 1 : 0)
            << " " << c->invincibleTime << " " << c->overhealMax << " " << c->overhealDecay
            << " " << (c->destroyOnDeath ? 1 : 0) << " " << (c->respawn ? 1 : 0)
            << " " << c->respawnDelay << " " << c->lives
            << " " << (c->playDeathAnim ? 1 : 0) << " " << (c->playHitAnim ? 1 : 0)
            << " " << (c->keepActiveOnDeath ? 1 : 0) << " " << kDamageTypeCount;   // then one resistance per damage kind
        for (int i = 0; i < kDamageTypeCount; ++i) out << " " << c->typeResistance[i];
        out << "\n";
    }
    for (auto* c : go->GetComponents<Seat>()) {
        out << "  seat " << c->seatHeight << " "
            << c->sitPoint.x << " " << c->sitPoint.y << " " << c->sitPoint.z << " "
            << c->facing << " " << c->type << " "
            << c->exitPoint.x << " " << c->exitPoint.y << " " << c->exitPoint.z << " "
            << c->useRadius << " " << Quote(c->prompt) << "\n";
    }
    for (auto* c : go->GetComponents<VehicleSeat>()) {
        out << "  vehicleseat " << (c->driver ? 1 : 0) << " "
            << c->sitPoint.x << " " << c->sitPoint.y << " " << c->sitPoint.z << " "
            << c->exitPoint.x << " " << c->exitPoint.y << " " << c->exitPoint.z << " "
            << c->useRadius << " " << (int)(unsigned char)c->enterKey << " " << Quote(c->prompt) << "\n";
    }
    for (auto* c : go->GetComponents<HungerStat>()) {
        out << "  stat_hunger " << c->maxHunger << " " << c->drainPerSecond << " "
            << c->sprintMultiplier << " " << c->lowThreshold;
        statToggles(out, c);
        out << " " << c->starveDamage << " " << c->overeatMax << " " << (c->slowWhenStarving ? 1 : 0)
            << " " << c->minSpeedFactor << "\n";
    }
    for (auto* c : go->GetComponents<ThirstStat>()) {
        out << "  stat_thirst " << c->maxThirst << " " << c->drainPerSecond << " "
            << c->sprintMultiplier << " " << c->lowThreshold;
        statToggles(out, c);
        out << " " << c->dehydrateDamage << " " << c->overdrinkMax << " "
            << (c->slowWhenDehydrated ? 1 : 0) << " " << c->minSpeedFactor << "\n";
    }
    for (auto* c : go->GetComponents<StaminaStat>()) {
        out << "  stat_stamina " << c->maxStamina << " " << c->regenPerSecond << " " << c->regenDelay
            << " " << c->sprintCost << " " << c->jumpCost << " " << c->exhaustedUntil;
        statToggles(out, c);
        out << " " << c->lowThreshold << " " << (c->slowWhenExhausted ? 1 : 0) << " "
            << c->minSpeedFactor << " " << c->regenScale << "\n";
    }
    for (auto* c : go->GetComponents<OxygenStat>()) {
        out << "  stat_oxygen " << c->maxOxygen << " " << c->drainPerSecond << " "
            << c->refillPerSecond;
        statToggles(out, c);
        out << " " << c->lowThreshold << " " << c->drownDamage << "\n";
    }
    for (auto* c : go->GetComponents<TemperatureStat>()) {
        out << "  stat_temp " << c->maxWarmth << " " << c->coldDrain << " "
            << c->warmRegen;
        statToggles(out, c);
        out << " " << c->lowThreshold << " " << c->freezeDamage << "\n";
    }
    for (auto* c : go->GetComponents<SleepStat>()) {
        out << "  stat_sleep " << c->maxEnergy << " " << c->drainPerSecond << " "
            << c->restPerSecond << " " << c->tiredThreshold;
        statToggles(out, c);
        out << " " << c->exhaustDamage << " " << (c->slowWhenTired ? 1 : 0) << " "
            << c->minSpeedFactor << "\n";
    }
    for (auto* c : go->GetComponents<SanityStat>()) {
        out << "  stat_sanity " << c->maxSanity << " " << c->drainInDark << " "
            << c->regenInLight << " " << c->lowThreshold;
        statToggles(out, c);
        out << " " << c->insaneDamage << "\n";
    }
    for (auto* c : go->GetComponents<RadiationStat>()) {
        out << "  stat_radiation " << c->maxRadiation << " " << c->gainPerSecond << " "
            << c->decayPerSecond << " " << c->sickThreshold << " " << c->damagePerSecond; statTail(out, c);
    }
    for (auto* c : go->GetComponents<BleedingStat>()) {
        out << "  stat_bleed " << c->maxBleed << " " << c->damagePerSecond << " "
            << c->clotPerSecond; statTail(out, c);
    }
    for (auto* c : go->GetComponents<PoisonStat>()) {
        out << "  stat_poison " << c->maxPoison << " " << c->damagePerSecond << " "
            << c->decayPerSecond; statTail(out, c);
    }
    for (auto* c : go->GetComponents<WetnessStat>()) {
        out << "  stat_wet " << c->maxWetness << " " << c->soakPerSecond << " " << c->dryPerSecond
            << " " << c->chillPerSecond << " " << c->soakedThreshold; statTail(out, c);
    }
    for (auto* c : go->GetComponents<CarryWeightStat>()) {
        out << "  stat_carry " << c->maxLoad << " " << c->overStaminaDrain << " "
            << c->minSpeedFactor; statTail(out, c);
    }
    for (auto* c : go->GetComponents<StatusEffectStat>()) {
        out << "  statuseffect " << (c->sendMessages ? 1 : 0) << "\n";
    }
    for (auto* c : go->GetComponents<SurvivalSave>()) {
        out << "  survivalsave " << Quote(c->saveKey) << " " << (c->loadOnStart ? 1 : 0)
            << " " << (c->saveContinuously ? 1 : 0) << "\n";
    }
    for (auto* c : go->GetComponents<SurvivalZone>()) {
        out << "  survivalzone " << c->effect << " " << c->amount << " " << c->duration
            << " " << Quote(c->effectName) << "\n";
    }
    for (auto* c : go->GetComponents<Consumables>()) {
        out << "  consumables " << (c->requireInventory ? 1 : 0) << " " << (c->consumeOnDrop ? 1 : 0)
            << " " << (c->destroyDroppedItem ? 1 : 0) << " " << c->recipes.size();
        for (const auto& r : c->recipes)
            out << " " << Quote(r.item) << " " << Quote(r.action) << " " << r.amount;
        out << "\n";
    }
    for (auto* c : go->GetComponents<DayNightCycle>()) {
        auto col = [&](const Color& k) { out << " " << k.r << " " << k.g << " " << k.b; };
        out << "  daynight " << c->dayLengthSeconds << " " << c->time << " " << (c->paused ? 1 : 0)
            << " " << (c->controlSun ? 1 : 0) << " " << (c->rotateSun ? 1 : 0) << " " << (c->controlSky ? 1 : 0)
            << " " << c->dayIntensity << " " << c->nightIntensity << " " << c->dayAmbient << " " << c->nightAmbient;
        col(c->dayLight); col(c->nightLight); col(c->skyDay); col(c->skyHorizon); col(c->skyNight);
        out << "\n";
    }
    for (auto* c : go->GetComponents<NPCController>()) {
        out << "  npc " << c->behavior << " " << c->moveSpeed << " " << c->sightRange << " "
            << c->wanderRadius << " " << c->attackRange << " " << c->attackDamage << " "
            << c->attackInterval << " " << (c->faceMovement ? 1 : 0) << " " << Quote(c->targetName)
            << " " << c->maxHealth << " " << (c->invulnerable ? 1 : 0)
            // smarter-AI trailing fields (older files stop after invulnerable):
            << " " << c->runSpeed << " " << c->turnSpeed << " " << c->acceleration
            << " " << c->stopDistance << " " << c->fieldOfView << " " << (c->lineOfSight ? 1 : 0)
            << " " << c->eyeHeight << " " << c->hearingRange << " " << c->detectionTime
            << " " << c->loseSightTime << " " << c->searchTime << " " << (c->aggressive ? 1 : 0)
            << " " << (c->provokable ? 1 : 0) << " " << (c->returnsHome ? 1 : 0)
            << " " << c->leashRange << " " << c->wanderPause << " " << (c->patrolLoop ? 1 : 0)
            << " " << c->waypointWait << " " << c->attackWindup << " " << c->fleeHealthPct
            << " " << c->separationRadius << " " << (c->driveAnimation ? 1 : 0)
            << " " << (c->lookAtTarget ? 1 : 0)
            << " " << (c->footIK ? 1 : 0)
            << " " << (c->usePathfinding ? 1 : 0) << " " << c->repathInterval << "\n";
        for (const Vec3& w : c->waypoints)
            out << "  npcwp " << w.x << " " << w.y << " " << w.z << "\n";
    }
    for (auto* c : go->GetComponents<MeleeAttacker>()) {
        out << "  melee " << c->damage << " " << c->range << " " << c->arc << " " << c->cooldown
            << " " << (int)(unsigned char)c->attackKey << " " << (c->useMouse ? 1 : 0)
            << " " << c->swingTime << " " << c->hitStart << " " << c->hitEnd << " " << c->knockback
            << " " << c->comboSteps << " " << c->comboWindow << " " << c->comboBonus
            << " " << (c->useHitbox ? 1 : 0) << " " << c->hitboxWidth << " " << c->hitboxHeight
            << " " << c->hitboxDepth << " " << Quote(c->weapon) << " " << Quote(c->weaponFile) << "\n";
    }
    if (auto* g = go->GetComponent<RangedWeapon>()) {
        out << "  gun";
        KvOut k{out};
        k("dmg", g->damage)("rate", g->fireRate)("range", g->range)("mag", g->magazine)("ammo", g->reserve)
         ("reload", g->reloadTime)("kb", g->knockback)("spread", g->spread).b("hitscan", g->hitscan)
         .b("auto", g->autoReload).key("fireKey", g->fireKey)("fireBtn", g->fireButton).key("reloadKey", g->reloadKey)
         ("muz", g->muzzleHeight)("speed", g->projectileSpeed).str("weapon", g->weapon).str("file", g->weaponFile)
         .str("proj", g->projectile);
        out << "\n";
    }
    for (auto* c : go->GetComponents<Spawner>()) {   // every instance: one manager object spawns enemies and pickups
        out << "  spawner " << Quote(c->templateName) << " " << c->interval << " " << c->maxAlive
            << " " << c->totalToSpawn << " " << c->spawnRadius << " " << c->startDelay
            << " " << (c->deactivateTemplate ? 1 : 0)
            // Wave fields appended (numeric-peek guarded on read, old scenes fine).
            << " " << c->count << " " << c->waves << " " << c->waveDelay
            << " " << (c->autoStart ? 1 : 0) << " " << Quote(c->prefabPath);
        if (c->usePool) out << " 1";   // optional: older builds read it as an unknown field and warn
        out << "\n";
    }
    for (auto* c : go->GetComponents<CraftingMenu>()) {
        out << "  craftmenu " << (int)(unsigned char)c->toggleKey << " " << (c->open ? 1 : 0)
            << " " << c->position.x << " " << c->position.y << " " << c->buttonSize.x << " "
            << c->buttonSize.y << " " << c->spacing << " " << (int)c->anchor << "\n";
    }
    for (auto* c : go->GetComponents<Crafting>()) {
        out << "  crafting " << c->recipes.size();
        for (const auto& r : c->recipes) {
            out << " " << Quote(r.output) << " " << r.outputCount << " " << r.inputs.size();
            for (const auto& in : r.inputs) out << " " << Quote(in.item) << " " << in.count;
        }
        out << " " << Quote(c->recipeBook);   // appended (quote-peek guarded)
        WriteRecipeExtras(out, c->recipes);
        out << "\n";
    }
    // A Player Controller is one record plus a section per view it has. Bare
    // controllers (code that adds them by hand) keep their own records, which
    // load back into a Player Controller.
    const auto playerControllers = go->GetComponents<PlayerController>();
    const auto* pcOwner = playerControllers.empty() ? nullptr : playerControllers.front();
    // A bare First or Third Person controller on its own (no Player View, no Player Controller)
    // is written the way the loader will hold it, as a Player Controller of that one view, so
    // that saving the loaded scene gives the same text. Combinations with a Player View or both
    // controllers keep the old records: the loader decides their View on load.
    const auto bareFp = go->GetComponents<FirstPersonController>();
    const auto bareTp = go->GetComponents<ThirdPersonController>();
    const bool bareSingle = !pcOwner && go->GetComponents<PlayerView>().empty() &&
                            bareFp.size() + bareTp.size() == 1;
    const bool newForm = pcOwner || bareSingle;
    if (bareSingle) out << "  playercontroller view=" << (bareFp.empty() ? 1 : 0) << "\n";
    for (const PlayerController* pc : playerControllers) {
        out << "  playercontroller";
        KvOut k{out};
        k("view", (int)pc->viewMode());
        if (auto* pv = pc->toggle()) {
            k.b("allow", pv->allowToggle)("start", (int)pv->defaultView).key("key", pv->toggleKey)
             ("pad", pv->toggleGamepadButton)("blend", pv->blendTime)
             ("ease", (int)pv->switchEase).b("snap", pv->snapSwitch)
             ("ex", pv->eyeOffset.x)("ey", pv->eyeOffset.y)("ez", pv->eyeOffset.z)
             ("dist", pv->thirdPersonDistance)("shoulder", pv->shoulderOffset).b("presetKeys", pv->presetHotkeys);
        }
        out << "\n";
    }
    const char* fpName = newForm ? "pcfirst" : "fpctrl";
    const char* tpName = newForm ? "pcthird" : "tpctrl";
    for (auto* fp : go->GetComponents<FirstPersonController>()) {
        out << "  " << fpName << " " << fp->walkSpeed << " " << fp->runSpeed << " " << fp->jumpForce << " "
            << fp->mouseSensitivity << " " << (fp->canJump ? 1 : 0) << " " << (fp->driveAnimation ? 1 : 0)
            << " " << (fp->invertY ? 1 : 0)
            // extended (back-compatible trailing fields): momentum + jump feel
            << " " << fp->acceleration << " " << fp->deceleration << " " << fp->airControl
            << " " << fp->coyoteTime << " " << fp->jumpBufferTime
            // extended: run + stance (sprint/crouch/prone). Keys as ints.
            << " " << (int)(unsigned char)fp->sprintKey << " " << (fp->toggleRun ? 1 : 0)
            << " " << (int)(unsigned char)fp->crouchKey << " " << (int)(unsigned char)fp->proneKey
            << " " << (fp->toggleStance ? 1 : 0)
            << " " << fp->crouchSpeed << " " << fp->proneSpeed
            << " " << fp->standEyeHeight << " " << fp->crouchEyeHeight << " " << fp->proneEyeHeight
            << " " << fp->stanceLerp
            // extended: lean (peek with Q/E)
            << " " << (int)(unsigned char)fp->leanLeftKey << " " << (int)(unsigned char)fp->leanRightKey
            << " " << fp->leanAngle << " " << fp->leanOffset << " " << fp->leanSpeed
            << " " << fp->maxJumps
            << " " << (fp->footIK ? 1 : 0) << "\n";
    }
    for (auto* fp : go->GetComponents<FirstPersonController>()) {
        // Feel / slide / hooks (separate record: older builds skip it).
        out << "  " << (newForm ? "pcfirstfeel" : "fpfeel") << " " << fp->lookSmoothing << " " << fp->sprintFovBoost << " " << fp->fovLerp
            << " " << (fp->zoomWithRightMouse ? 1 : 0) << " " << fp->zoomFov << " " << fp->zoomSensitivity
            << " " << fp->landDip << " " << (fp->canSlide ? 1 : 0) << " " << fp->slideSpeed << " " << fp->slideTime
            << " " << fp->slideEyeHeight << " " << (fp->driveStateMachine ? 1 : 0) << " " << (fp->ikLook ? 1 : 0)
            // extended: the head bob and the eye smoothing
            << " " << (fp->headBob ? 1 : 0) << " " << fp->bobAmount << " " << fp->bobSpeed << " " << fp->bobSway
            << " " << fp->bobRunScale << " " << fp->bobBlend << " " << fp->bobGroundGrace << " " << fp->eyeStepSmoothing
            << " " << (fp->dodge.canDodge ? 1 : 0) << " " << (int)(unsigned char)fp->dodge.key
            << " " << fp->dodge.impulse << " " << fp->dodge.duration << " " << fp->dodge.cooldown
            << " " << fp->dodge.stamina << " " << (fp->dodge.iFrames ? 1 : 0)
            // Traversal flags. Older loaders do not skip these trailing ints.
            << " " << (fp->canVault ? 1 : 0) << " " << (fp->canMantle ? 1 : 0) << " " << (fp->canLedgeGrab ? 1 : 0) << "\n";
        out << "  " << (newForm ? "pcfirststance" : "fpstance");
        KvOut k{out};
        k.b("prone", fp->allowProne).b("resize", fp->resizeBody)("crouchH", fp->crouchHeight)("proneH", fp->proneHeight)
         ("pad", fp->stanceGamepadButton)("hold", fp->proneHoldTime)
         ("walkA", fp->walkAcceleration)("steer", fp->steerRate)
         .b("dive", fp->diveOnProneWhileSprinting)("diveHop", fp->diveHop)("diveSlide", fp->diveSlideTime)
         ("capSens", fp->capturedSensitivity)("capPx", fp->capturedPixelCap)
         ("slope", fp->slopeLimit)("step", fp->stepOffset)("fallY", fp->fallResetY)   // settings that were never saved
         ("swim", fp->swimSpeed)("swimUp", fp->swimUpSpeed)("swimDown", fp->swimDownSpeed)
         .b("varJump", fp->variableJump)("jumpCut", fp->jumpCut)("climb", fp->climbSpeed)("hardLand", fp->hardLandSpeed)("preset", fp->controllerPreset)("stickRamp", fp->stickLookRamp);
        if (fp->stance() != std::remove_pointer_t<decltype(fp)>::Stance::Stand) k("stance", (int)fp->stance());   // saved crouched / prone
        out << "\n";
    }
    for (auto* tp : go->GetComponents<ThirdPersonController>()) {
        out << "  " << tpName << " " << tp->walkSpeed << " " << tp->runSpeed << " " << tp->jumpForce << " "
            << tp->mouseSensitivity << " " << tp->turnSpeed << " " << tp->distance << " "
            << tp->cameraHeight << " " << (tp->canJump ? 1 : 0) << " " << (tp->driveAnimation ? 1 : 0)
            // extended (optional, back-compatible trailing fields)
            << " " << (tp->invertX ? 1 : 0) << " " << (tp->invertY ? 1 : 0)
            << " " << tp->minDistance << " " << tp->maxDistance << " " << tp->zoomSpeed
            << " " << tp->shoulderOffset << " " << tp->pivotDampingXZ   // the old camera-damping slot
            << " " << tp->minPitch << " " << tp->maxPitch << " " << (int)tp->faceMode
            // extended (back-compatible trailing fields): momentum + jump feel
            << " " << tp->acceleration << " " << tp->deceleration << " " << tp->airControl
            << " " << tp->coyoteTime << " " << tp->jumpBufferTime
            // camera collision (spring arm)
            << " " << (tp->cameraCollision ? 1 : 0) << " " << tp->cameraCollisionSkin
            // extended: run + stance (sprint/crouch/prone). Keys as ints.
            << " " << (int)(unsigned char)tp->sprintKey << " " << (tp->toggleRun ? 1 : 0)
            << " " << (int)(unsigned char)tp->crouchKey << " " << (int)(unsigned char)tp->proneKey
            << " " << (tp->toggleStance ? 1 : 0)
            << " " << tp->crouchSpeed << " " << tp->proneSpeed
            << " " << tp->crouchHeightDrop << " " << tp->proneHeightDrop
            << " " << tp->stanceLerp
            // extended: lean (peek with Q/E)
            << " " << (int)(unsigned char)tp->leanLeftKey << " " << (int)(unsigned char)tp->leanRightKey
            << " " << tp->leanAngle << " " << tp->leanOffset << " " << tp->leanSpeed
            << " " << tp->maxJumps
            << " " << (tp->footIK ? 1 : 0)
            // extended: the camera follow (pivot) and the spring arm
            << " " << tp->pivotDampingXZ << " " << tp->pivotDampingY << " " << tp->maxPivotLag
            << " " << tp->orbitSmoothing << " " << tp->cameraCollisionRadius << " " << tp->cameraCollisionMask
            << " " << tp->cameraCollisionPullIn << " " << tp->cameraCollisionRecover << "\n";
        out << "  " << (newForm ? "pcthirdstance" : "tpstance");
        KvOut k{out};
        k.b("prone", tp->allowProne).b("resize", tp->resizeBody)("crouchH", tp->crouchHeight)("proneH", tp->proneHeight)
         ("pad", tp->stanceGamepadButton)("hold", tp->proneHoldTime)
         ("walkA", tp->walkAcceleration)("steer", tp->steerRate)
         .b("dive", tp->diveOnProneWhileSprinting)("diveHop", tp->diveHop)("diveSlide", tp->diveSlideTime)
         ("capSens", tp->capturedSensitivity)("capPx", tp->capturedPixelCap)
         ("slope", tp->slopeLimit)("step", tp->stepOffset)("fallY", tp->fallResetY)   // settings that were never saved
         ("swim", tp->swimSpeed)("swimUp", tp->swimUpSpeed)("swimDown", tp->swimDownSpeed)
         .b("varJump", tp->variableJump)("jumpCut", tp->jumpCut)("climb", tp->climbSpeed)("hardLand", tp->hardLandSpeed)("preset", tp->controllerPreset)("stickRamp", tp->stickLookRamp);
        if (tp->stance() != std::remove_pointer_t<decltype(tp)>::Stance::Stand) k("stance", (int)tp->stance());   // saved crouched / prone
        out << "\n";
    }
    for (auto* tp : go->GetComponents<ThirdPersonController>()) {
        out << "  " << (newForm ? "pcthirdfeel" : "tpfeel") << " " << tp->lookSmoothing << " " << tp->sprintFovBoost << " " << tp->fovLerp
            << " " << (tp->aimWithRightMouse ? 1 : 0) << " " << tp->aimDistance << " " << tp->aimShoulder
            << " " << tp->aimFov << " " << tp->aimSensitivity << " " << tp->aimLerp
            << " " << (tp->autoRecenter ? 1 : 0) << " " << tp->recenterDelay << " " << tp->recenterSpeed
            << " " << tp->landShake << " " << tp->strideLength << " " << (tp->canSlide ? 1 : 0)
            << " " << tp->slideSpeed << " " << tp->slideTime << " " << (tp->driveStateMachine ? 1 : 0)
            << " " << (tp->ikLook ? 1 : 0)
            << " " << (tp->dodge.canDodge ? 1 : 0) << " " << (int)(unsigned char)tp->dodge.key
            << " " << tp->dodge.impulse << " " << tp->dodge.duration << " " << tp->dodge.cooldown
            << " " << tp->dodge.stamina << " " << (tp->dodge.iFrames ? 1 : 0)
            // Traversal flags. Older loaders do not skip these trailing ints.
            << " " << (tp->canVault ? 1 : 0) << " " << (tp->canMantle ? 1 : 0) << " " << (tp->canLedgeGrab ? 1 : 0)
            << " " << tp->cursorMode << "\n";
    }
    // Player view toggle (grok/view-toggle). Its own record, so older scenes skip it.
    // Fields: allow, view (0 first / 1 third), key, pad, blend, ex ey ez, dist, shoulder.
    if (!pcOwner)
    for (auto* pv : go->GetComponents<PlayerView>()) {
        out << "  playerview";
        KvOut k{out};
        k.b("allow", pv->allowToggle)("view", (int)pv->defaultView).key("key", pv->toggleKey)
         ("pad", pv->toggleGamepadButton)("blend", pv->blendTime)
         ("ease", (int)pv->switchEase).b("snap", pv->snapSwitch)
         ("ex", pv->eyeOffset.x)("ey", pv->eyeOffset.y)("ez", pv->eyeOffset.z)
         ("dist", pv->thirdPersonDistance)("shoulder", pv->shoulderOffset).b("presetKeys", pv->presetHotkeys);
        out << "\n";
    }
    // grok/ui-theme. Own record so older scenes skip it. apply=1 bakes on play.
    for (auto* th : go->GetComponents<UITheme>()) {
        out << "  uitheme";
        KvOut k{out};
        k.str("name", th->themeName).b("apply", th->applyOnStart)("preset", th->preset)
         ("corner", th->cornerRadius)("mask", th->cornerMask)("border", th->borderWidth).b("shadow", th->shadow)
         ("soft", th->shadowSoftness)("ox", th->shadowOffset.x)("oy", th->shadowOffset.y)
         ("pad", th->padding)("font", th->fontScale)("tpx", th->textPixelSize)("trans", th->transitionSpeed)
         ("hscale", th->hoverScale)("glow", th->glow)("opa", th->opacity)
         .b("grad", th->gradient)("gdir", th->gradientDir).b("sheen", th->sheen)
         .col("panel", th->panel).col("pbot", th->panelBottom).col("accent", th->accent)
         .col("text", th->text).col("muted", th->muted)
         .col("button", th->button).col("hover", th->buttonHover).col("press", th->buttonPressed)
         .col("btext", th->buttonText)
         .col("off", th->disabled).col("danger", th->danger).col("ok", th->success)
         .col("edge", th->border).col("sh", th->shadowCol).col("glowc", th->glowColor)
         .col("fcol", th->focusColor)("fthick", th->focusThickness)("fpad", th->focusPadding).b("fpulse", th->focusPulse);
        if (!th->fontPath.empty()) k.str("fontPath", th->fontPath);   // added later: absent = no font
        if (th->global) k.b("global", true);
        out << "\n";
    }
    for (auto* td : go->GetComponents<TopDownController>()) {
        out << "  tdctrl " << td->walkSpeed << " " << td->runSpeed << " "
            << KeyToken(td->sprintKey) << " "
            << (td->driveAnimation ? 1 : 0) << " " << (td->rotateToFace ? 1 : 0) << " " << td->turnSpeed << " "
            << td->acceleration << " " << td->deceleration << " " << (td->cameraRelative ? 1 : 0) << " "
            << td->cameraDistance << " " << td->cameraPitch << " " << td->cameraYaw << " "
            << td->lookHeight << " " << td->cameraDamping
            << " " << (td->footIK ? 1 : 0) << "\n";
    }
    for (auto* ts : go->GetComponents<TopDownShooterController>()) {
        out << "  tdsctrl";
        KvOut k{out};
        k("walk", ts->walkSpeed)("run", ts->runSpeed).key("sprint", ts->sprintKey)("accel", ts->acceleration)
         ("decel", ts->deceleration)("backpedal", ts->backpedalMultiplier).b("camRel", ts->cameraRelative)
         .b("anim", ts->driveAnimation)("aimMode", ts->aimMode)("turn", ts->turnSpeed)
         ("fireMode", ts->fireMode)("fireBtn", ts->fireButton).key("fireKey", ts->fireKey)("firePad", ts->fireGamepadButton)
         .b("auto", ts->autoFire)("rate", ts->fireRate)("dmg", ts->damage)("pspeed", ts->projectileSpeed)
         ("plife", ts->projectileLifetime)("range", ts->range)("pellets", ts->pellets)("spread", ts->spread)
         ("force", ts->impactForce)("bsize", ts->bulletSize).col("bcol", ts->bulletColor)
         ("muzH", ts->muzzleHeight)("muzF", ts->muzzleForward)
         ("mag", ts->magazineSize).key("reloadKey", ts->reloadKey)("reload", ts->reloadTime).b("autoReload", ts->autoReload)
         .key("dashKey", ts->dashKey)("dashPad", ts->dashGamepadButton)("dashSpeed", ts->dashSpeed)("dashTime", ts->dashTime)("dashCd", ts->dashCooldown)
         .b("dashInv", ts->dashInvulnerable)
         ("camDist", ts->cameraDistance)("camPitch", ts->cameraPitch)("camYaw", ts->cameraYaw)("camDamp", ts->cameraDamping)
         ("lead", ts->aimLookAhead)("maxLead", ts->maxLookAhead).b("zoom", ts->wheelZoom)("minDist", ts->minDistance)
         ("maxDist", ts->maxDistance)("shake", ts->fireShake).str("proj", ts->projectile);
        if (!ts->weapon.empty()) k.str("weapon", ts->weapon);
        out << "\n";
    }
    for (auto* rd : go->GetComponents<Ragdoll>()) {
        out << "  ragdoll";
        KvOut k{out};
        k("grav", rd->gravity)("damp", rd->damping)("fric", rd->friction)("radius", rd->radius)
         ("iters", rd->iterations)("maxSpeed", rd->maxSpeed).b("onDeath", rd->ragdollOnDeath)
         ("deathPush", rd->deathImpulse).b("hits", rd->hitReactions)("hitPush", rd->hitImpulse)
         ("recover", rd->hitRecover);
        out << "\n";
    }
    for (auto* pc : go->GetComponents<Platformer25DController>()) {
        out << "  p25ctrl";
        KvOut k{out};
        k("walk", pc->walkSpeed)("run", pc->runSpeed).key("sprint", pc->sprintKey)("accel", pc->acceleration)
         ("decel", pc->deceleration)("air", pc->airControl).b("anim", pc->driveAnimation)
         .b("jump", pc->canJump).key("jumpKey", pc->jumpKey)("jumpForce", pc->jumpForce)("jumps", pc->maxJumps)
         ("cut", pc->jumpCut)("fallG", pc->fallGravity)("maxFall", pc->maxFallSpeed)("coyote", pc->coyoteTime)
         ("buffer", pc->jumpBufferTime)
         ("layers", pc->layerCount)("spacing", pc->layerSpacing)("frontZ", pc->frontLayerZ)("layer", pc->layer)
         .key("backKey", pc->layerBackKey).key("frontKey", pc->layerFrontKey)("switch", pc->layerSwitchSpeed)
         .b("checkFree", pc->checkLayerFree)("radius", pc->bodyRadius)
         ("turn", pc->turnSpeed)("faceCam", pc->faceCameraAngle)
         .b("grab", pc->canGrab).key("grabKey", pc->grabKey)("grabPad", pc->grabGamepadButton)("reach", pc->grabReach)
         ("pull", pc->grabPullSpeed).b("hang", pc->grabHang)
         ("step", pc->stepHeight)("stickRun", pc->stickRunThreshold)("jumpPad", pc->jumpGamepadButton)
         .b("climb", pc->ledgeClimb)("climbTime", pc->climbTime)
         ("camDist", pc->cameraDistance)("camH", pc->cameraHeight)("camPitch", pc->cameraPitch)("lead", pc->lookAhead)
         ("camDamp", pc->cameraDamping)("vDamp", pc->verticalDamping)("speedZoom", pc->speedZoom)
         .b("followLayer", pc->cameraFollowsLayer)
         .key("crouchKey", pc->crouchKey).key("proneKey", pc->proneKey).b("toggleStance", pc->toggleStance)
         ("crouchSpeed", pc->crouchSpeed)("proneSpeed", pc->proneSpeed).b("allowProne", pc->allowProne)
         .b("resize", pc->resizeBody)("crouchH", pc->crouchHeight)("proneH", pc->proneHeight)
         ("stancePad", pc->stanceGamepadButton)("proneHold", pc->proneHoldTime);
        if (pc->stance() != Platformer25DController::Stance::Stand) k("stance", (int)pc->stance());   // saved crouched / prone
        out << "\n";
    }
    for (auto* pj : go->GetComponents<Projectile3D>()) {
        out << "  proj3d";
        KvOut k{out};
        k("vx", pj->velocity.x)("vy", pj->velocity.y)("vz", pj->velocity.z)("dmg", pj->damage)("life", pj->lifetime)
         ("grav", pj->gravityScale)("force", pj->impactForce)("pierce", pj->pierce).b("bounce", pj->bounce)
         ("bounces", pj->maxBounces);
        out << "\n";
    }
    for (auto* t2 : go->GetComponents<TopDownController2D>()) {
        out << "  tdctrl2d " << t2->speed << " " << t2->runSpeed << " " << KeyToken(t2->sprintKey)
            << " " << (t2->normalizeDiagonal ? 1 : 0) << " " << (t2->useGamepad ? 1 : 0)
            << " " << t2->acceleration << " " << t2->deceleration
            << " " << KeyToken(t2->dashKey) << " " << t2->dashSpeed << " " << t2->dashDuration << " " << t2->dashCooldown
            << " " << t2->faceMode << " " << t2->spriteForward << " " << t2->turnSpeed << " " << (t2->driveAnimation ? 1 : 0)
            << " " << (t2->clampBounds ? 1 : 0) << " " << t2->boundsMin.x << " " << t2->boundsMin.y
            << " " << t2->boundsMax.x << " " << t2->boundsMax.y << " " << (t2->screenWrap ? 1 : 0)
            << " " << t2->knockbackTime << " " << KeyToken(t2->fireKey) << " " << t2->fireButton
            << " " << t2->projectileSpeed << " " << t2->fireRate << " " << Quote(t2->projectile) << "\n";
    }
    for (auto* fr : go->GetComponents<FreeRoamController>()) {
        out << "  frctrl " << fr->moveSpeed << " " << fr->boostMultiplier << " "
            << (int)(unsigned char)fr->sprintKey << " " << (int)(unsigned char)fr->upKey << " "
            << (int)(unsigned char)fr->downKey << " " << fr->mouseSensitivity << " "
            << (fr->invertY ? 1 : 0) << " " << (fr->lockCursor ? 1 : 0) << " "
            << (fr->lookRequiresRightMouse ? 1 : 0) << " " << fr->minPitch << " " << fr->maxPitch << " "
            << fr->acceleration << " " << fr->yaw << " " << fr->pitch << "\n";
    }
    for (auto* ts : go->GetComponents<ThirdPersonShooterController>()) {
        out << "  tpsctrl " << ts->walkSpeed << " " << ts->runSpeed << " " << ts->jumpForce << " "
            << KeyToken(ts->sprintKey) << " " << (ts->canJump ? 1 : 0) << " "
            << (ts->driveAnimation ? 1 : 0) << " " << ts->turnSpeed << " "
            << ts->acceleration << " " << ts->deceleration << " " << ts->airControl << " "
            << ts->coyoteTime << " " << ts->jumpBufferTime << " "
            << ts->mouseSensitivity << " " << (ts->invertY ? 1 : 0) << " "
            << ts->minPitch << " " << ts->maxPitch << " " << ts->distance << " " << ts->cameraHeight << " "
            << ts->shoulderOffset << " " << (ts->cameraCollision ? 1 : 0) << " " << ts->cameraCollisionSkin << " "
            << ts->aimButton << " " << ts->aimDistance << " " << ts->aimShoulder << " " << ts->aimSpeed << " "
            << ts->fireButton << " " << (ts->autoFire ? 1 : 0) << " " << ts->fireRate << " "
            << (ts->lockCursor ? 1 : 0)
            // extended: lean (peek with Q/E)
            << " " << (int)(unsigned char)ts->leanLeftKey << " " << (int)(unsigned char)ts->leanRightKey
            << " " << ts->leanAngle << " " << ts->leanOffset << " " << ts->leanSpeed
            << " " << ts->maxJumps
            << " " << (ts->footIK ? 1 : 0)
            // extended: the camera follow (pivot) and the spring arm
            << " " << ts->pivotDampingXZ << " " << ts->pivotDampingY << " " << ts->maxPivotLag
            << " " << ts->cameraCollisionRadius << " " << ts->cameraCollisionMask
            << " " << ts->cameraCollisionPullIn << " " << ts->cameraCollisionRecover
            << " " << ts->stepOffset
            << " " << ts->walkAcceleration << " " << ts->steerRate << "\n";
    }
    for (auto* cm : go->GetComponents<ClickToMoveController>()) {
        out << "  ctmctrl " << cm->walkSpeed << " " << cm->runSpeed << " "
            << KeyToken(cm->runKey) << " " << cm->stopDistance << " " << cm->turnSpeed
            << " " << cm->mouseButton << " " << (cm->holdToMove ? 1 : 0) << " "
            << (cm->driveAnimation ? 1 : 0) << " " << (cm->usePlayerHeight ? 1 : 0)
            << " " << cm->groundY
            // extended: follow camera (optional, back-compatible trailing fields)
            << " " << (cm->followCamera ? 1 : 0) << " " << cm->cameraHeight << " " << cm->cameraDistance
            << " " << cm->minDistance << " " << cm->maxDistance << " " << cm->cameraYaw
            << " " << cm->cameraPitch << " " << cm->minPitch << " " << cm->maxPitch
            << " " << cm->rotateSpeed << " " << cm->keyRotateSpeed
            << " " << KeyToken(cm->rotateLeftKey)
            << " " << KeyToken(cm->rotateRightKey)
            << " " << cm->cameraDamping
            << " " << cm->arriveRadius                       // extended (back-compatible trailing field)
            << " " << (cm->footIK ? 1 : 0) << " " << (cm->showCursor ? 1 : 0)
            << " " << (cm->usePathfinding ? 1 : 0) << " " << cm->repathInterval;
        // extended (back-compatible trailing fields): click on colliders, give up when stuck. Written only when one differs
        // from its default, so a scene saved before they existed saves byte for byte the same (registry_serializer).
        if (!cm->pickColliders || !cm->giveUpWhenStuck || cm->stuckTime != 1.0f)
            out << " " << (cm->pickColliders ? 1 : 0) << " " << (cm->giveUpWhenStuck ? 1 : 0) << " " << cm->stuckTime;
        out << "\n";
    }
    for (auto* ft : go->GetComponents<FollowTarget2D>()) {
        out << "  follow2d " << Quote(ft->target) << " " << ft->speed << " " << ft->stopDistance << "\n";
    }
    // The other movement controllers' crouch / prone (after their own records, so they exist on load).
    for (auto* c : go->GetComponents<CharacterController3D>())        WriteStanceControl(out, "cc3dstance", c->stanceControl);
    for (auto* c : go->GetComponents<ThirdPersonShooterController>()) WriteStanceControl(out, "tpsstance", c->stanceControl);
    for (auto* c : go->GetComponents<TopDownController>())            WriteStanceControl(out, "tdstance", c->stanceControl);
    for (auto* c : go->GetComponents<ClickToMoveController>())        WriteStanceControl(out, "ctmstance", c->stanceControl);
    for (auto* c : go->GetComponents<NPCController>())                WriteStanceControl(out, "npcstance", c->stanceControl);
    for (auto* sp : go->GetComponents<Spinner>()) {
        out << "  spinner " << sp->angularVelocity.x << " " << sp->angularVelocity.y
            << " " << sp->angularVelocity.z;
        if (sp->space != Space::Self) out << " " << static_cast<int>(sp->space);   // optional trailing field
        out << "\n";
    }
    for (auto* lt : go->GetComponents<Lifetime>()) {
        out << "  lifetime " << lt->seconds;
        if (lt->Elapsed() > 0.0f) out << " " << lt->Elapsed();   // optional: a running timer is saved with the game
        out << "\n";
    }
    for (auto* co : go->GetComponents<Collectible>()) {
        out << "  collectible " << Quote(co->scoreVar) << " " << co->points << " " << co->heal
            << " " << (co->respawn ? 1 : 0) << " " << co->respawnDelay << " " << Quote(co->collectorTag) << "\n";
    }
    for (auto* dt : go->GetComponents<DamageOnTouch>()) {
        out << "  damagetouch " << dt->damage << " " << dt->interval << " " << (dt->once ? 1 : 0)
            << " " << dt->knockback << " " << (dt->destroySelf ? 1 : 0) << " " << Quote(dt->targetTag) << "\n";
    }
    for (auto* tp : go->GetComponents<Teleporter>()) {
        out << "  teleporter " << Quote(tp->targetName) << " " << tp->destination.x << " " << tp->destination.y
            << " " << tp->destination.z << " " << tp->cooldown << " " << Quote(tp->triggerTag) << "\n";
    }
    for (auto* jp : go->GetComponents<JumpPad>()) {
        out << "  jumppad " << jp->force << " " << (jp->useObjectUp ? 1 : 0) << " "
            << jp->forwardBoost << " " << jp->cooldown << " " << Quote(jp->triggerTag) << "\n";
    }
    for (auto* tz : go->GetComponents<TriggerZone>()) {   // every instance
        out << "  triggerzone " << tz->action << " " << Quote(tz->varName) << " " << tz->amount
            << " " << (tz->once ? 1 : 0) << " " << Quote(tz->targetName) << " " << Quote(tz->triggerTag) << "\n";
    }
    for (auto* st : go->GetComponents<Stats>()) {
        out << "  stats " << st->health << " " << st->maxHealth << " " << st->mana << " " << st->maxMana
            << " " << st->level << " " << st->xp << " " << st->xpToNext
            << " " << st->strength << " " << st->defense << " " << st->intelligence << " " << st->agility << "\n";
    }
    for (auto* inv : go->GetComponents<Inventory>()) {
        out << "  inventory " << inv->capacity << " " << inv->slots.size();
        for (const auto& s : inv->slots) out << " " << Quote(s.item) << " " << s.count;
        out << " " << inv->maxStack;   // appended (numeric-peek guarded on read)
        WriteInstanceData(out, inv->slots.size(), [&](std::size_t i) -> const ItemInstanceData& { return inv->slots[i].data; });
        out << " " << inv->weightLimit << " " << (inv->slowWhenOverWeight ? 1 : 0) << " " << inv->overWeightSpeed   // appended (guarded)
            << " " << (inv->keepOnSceneLoad ? 1 : 0)
            << " " << (inv->enforceWeight ? 1 : 0);   // appended (guarded on read)
        out << "\n";
    }
    for (auto* pk : go->GetComponents<ItemPickup>()) {
        out << "  itempickup";
        KvOut k{out};
        k.str("item", pk->item)("count", pk->count).str("tag", pk->collectorTag).b("touch", pk->pickupOnTouch)
         ("auto", pk->autoRadius).key("key", pk->interactKey)("range", pk->interactRange)
         ("magnet", pk->magnetRadius)("magnetSpeed", pk->magnetSpeed).b("hold", pk->holdInHands);
        if (!pk->data.Empty()) {
            k("dur", pk->data.durability)("uid", pk->data.uid)("vals", pk->data.values.size());
            for (std::size_t v = 0; v < pk->data.values.size(); ++v) {
                const std::string n = std::to_string(v);
                k.str(("k" + n).c_str(), pk->data.values[v].first).str(("v" + n).c_str(), pk->data.values[v].second);
            }
        }
        out << "\n";
    }
    for (auto* eq : go->GetComponents<Equipment>()) {
        // One line: the flags, then per slot sN* keys (name, accepted type, socket, item and
        // its instance data, a worn bag's contents included).
        out << "  equipment";
        KvOut k{out};
        k.b("show", eq->showOnCharacter).b("keep", eq->keepOnSceneLoad)("n", eq->slots.size());
        for (std::size_t i = 0; i < eq->slots.size(); ++i) {
            const auto& s = eq->slots[i];
            const std::string p = "s" + std::to_string(i);
            k.str((p + "name").c_str(), s.name)((p + "acc").c_str(), (int)s.accepts).str((p + "sock").c_str(), s.socket);
            if (s.item.empty()) continue;
            const ItemInstanceData d = eq->CurrentData((int)i);
            k.str((p + "item").c_str(), s.item)((p + "dur").c_str(), d.durability)((p + "uid").c_str(), d.uid)((p + "vals").c_str(), d.values.size());
            for (std::size_t v = 0; v < d.values.size(); ++v) {
                const std::string n = std::to_string(v);
                k.str((p + "k" + n).c_str(), d.values[v].first).str((p + "v" + n).c_str(), d.values[v].second);
            }
        }
        out << "\n";
    }
    for (auto* ls : go->GetComponents<LootSource>()) {
        out << "  lootsource";
        KvOut{out}.str("table", ls->table)("when", (int)ls->when)("refill", ls->refill)("seed", ls->seed);
        out << "\n";
    }
    for (auto* tm : go->GetComponents<TurnManager>()) {
        out << "  turnmgr " << tm->current << " " << tm->round << " " << (tm->autoStart ? 1 : 0)
            << " " << tm->participants.size();
        for (const auto& p : tm->participants) out << " " << Quote(p);
        out << "\n";
    }
    for (auto* cf : go->GetComponents<CameraFollow>()) {
        out << "  camerafollow " << Quote(cf->targetName) << " "
            << cf->offset.x << " " << cf->offset.y << " " << cf->offset.z << " "
            << cf->smoothing << "\n";
    }
    const VirtualCamera defaultVcam;
    for (auto* vc : go->GetComponents<VirtualCamera>()) {
        out << "  vcam " << vc->priority << " " << Quote(vc->follow) << " " << Quote(vc->lookAt)
            << " " << vc->followOffset.x << " " << vc->followOffset.y << " " << vc->followOffset.z
            << " " << vc->positionDamping << " " << vc->rotationDamping
            << " " << vc->fieldOfView << " " << vc->shakeAmplitude << " " << vc->shakeFrequency
            // extended (back-compatible trailing fields)
            << " " << (int)vc->bindingMode
            << " " << vc->lookAtOffset.x << " " << vc->lookAtOffset.y << " " << vc->lookAtOffset.z
            << " " << vc->aimDeadZone << " " << vc->impulseDecay
            // freelook block (back-compatible trailing fields)
            << " " << (vc->freeLook ? 1 : 0) << " " << vc->orbitYaw << " " << vc->orbitPitch
            << " " << vc->orbitRadius << " " << vc->orbitHeight
            << " " << vc->orbitMinPitch << " " << vc->orbitMaxPitch
            << " " << (vc->orbitInput ? 1 : 0) << " " << vc->orbitButton << " " << vc->mouseSensitivity
            // dolly block (back-compatible trailing fields)
            << " " << (vc->dolly ? 1 : 0) << " " << Quote(vc->dollyPath)
            << " " << vc->dollyPosition << " " << (vc->autoDolly ? 1 : 0)
            // dutch + confiner (back-compatible trailing fields)
            << " " << vc->dutch << " " << (vc->confine ? 1 : 0)
            << " " << vc->confineMin.x << " " << vc->confineMin.y << " " << vc->confineMin.z
            << " " << vc->confineMax.x << " " << vc->confineMax.y << " " << vc->confineMax.z << "\n";
        // The camera rig layer, as keys (written right after its vcam line; read back onto that vcam).
        // Written only when something differs from a new Virtual Camera, so scenes that never use it save as before.
        auto rigKeys = [](const VirtualCamera* v) {
            std::ostringstream o;
            KvOut k{o};
            k("damp", (int)v->dampingMode)("gpad", v->groupPadding).b("gfit", v->groupFrame)
             ("look", v->lookaheadTime)("lookMax", v->lookaheadMax)("lookSm", v->lookaheadSmooth)
             .b("zones", v->useZones)("dzw", v->deadZoneW)("dzh", v->deadZoneH)("szw", v->softZoneW)("szh", v->softZoneH)
             ("zdamp", v->zoneDamping).b("o2d", v->ortho2D)("osize", v->orthoSize).b("cview", v->confineToView)
             .b("rooms", v->useRooms)("roomT", v->roomBlendTime)
             .b("col", v->collision)("colR", v->collisionRadius)("colSkin", v->collisionSkin)("colRec", v->collisionRecover)
             ("colIn", v->collisionPullIn).b("colSnap", v->collisionSnapIn)("colMask", v->collisionMask)
             .b("fade", v->fadeOccluders)("fadeA", v->occlusionAlpha)("fadeSpd", v->occlusionFadeSpeed)
             ("blendIn", v->blendIn)("blendInStyle", v->blendInStyle).b("aimFollow", v->aimAtFollow)
             ("ng", (int)v->groupTargets.size());
            for (std::size_t gi = 0; gi < v->groupTargets.size(); ++gi) k.str(("g" + std::to_string(gi)).c_str(), v->groupTargets[gi]);
            return o.str();
        };
        const std::string rig = rigKeys(vc), rigDefault = rigKeys(&defaultVcam);
        if (rig != rigDefault) out << "  vcamx" << rig << "\n";
    }
    for (auto* cb : go->GetComponents<CinemachineBrain>()) {
        out << "  cmbrain " << cb->blendTime << " " << (cb->blendStyle == cam::BlendStyle::Linear ? 0 : 1) << "\n";
        const CinemachineBrain defaultBrain;
        if (cb->blendStyle != defaultBrain.blendStyle || cb->yieldToCutscene != defaultBrain.yieldToCutscene ||
            cb->customCurve[0] != defaultBrain.customCurve[0] || cb->customCurve[1] != defaultBrain.customCurve[1] ||
            cb->customCurve[2] != defaultBrain.customCurve[2] || cb->customCurve[3] != defaultBrain.customCurve[3]) {
            out << "  cmbrainx";
            KvOut k{out};
            k("style", (int)cb->blendStyle)("c0", cb->customCurve[0])("c1", cb->customCurve[1])("c2", cb->customCurve[2])("c3", cb->customCurve[3])
             .b("cutscene", cb->yieldToCutscene);
            out << "\n";
        }
    }
    for (auto* cz : go->GetComponents<CameraZone>()) {
        out << "  camzone";
        KvOut k{out};
        k("shape", (int)cz->shape)("sx", cz->size.x)("sy", cz->size.y)("sz", cz->size.z)("ox", cz->offset.x)("oy", cz->offset.y)("oz", cz->offset.z)
         ("blend", cz->blend)("prio", cz->priority)("fov", cz->fieldOfView)("fx", cz->followOffset.x)("fy", cz->followOffset.y)("fz", cz->followOffset.z)
         ("damp", cz->positionDamping).str("shot", cz->shot);
        out << "\n";
    }
    for (auto* rm : go->GetComponents<CameraRoom>()) {
        out << "  camroom";
        KvOut k{out};
        k("w", rm->size.x)("h", rm->size.y);
        out << "\n";
    }
    for (auto* dp : go->GetComponents<DollyPath>()) {
        out << "  dollypath " << (dp->looped ? 1 : 0) << " " << dp->waypoints.size();
        for (const auto& w : dp->waypoints) out << " " << w.x << " " << w.y << " " << w.z;
        out << "\n";
    }
    for (auto* dc : go->GetComponents<DollyCart>()) {
        out << "  dollycart " << Quote(dc->path) << " " << dc->position << " "
            << dc->speed << " " << (dc->autoMove ? 1 : 0);
        // extended (trailing): only when one is on, so older scenes save byte for byte the same (registry_serializer)
        if (dc->speedInMetres || dc->pingPong || dc->orientToPath)
            out << " " << (dc->speedInMetres ? 1 : 0) << " " << (dc->pingPong ? 1 : 0) << " " << (dc->orientToPath ? 1 : 0);
        out << "\n";
    }
    for (auto* tr : go->GetComponents<TextRenderer>()) {
        out << "  text " << Quote(tr->text) << " "
            << tr->color.r << " " << tr->color.g << " " << tr->color.b << " " << tr->color.a << " "
            << tr->pixelSize << " " << (tr->screenSpace ? 1 : 0) << " "
            << tr->screenPos.x << " " << tr->screenPos.y << " " << (int)tr->anchor << " "
            << (tr->shadow ? 1 : 0) << " "
            << tr->shadowColor.r << " " << tr->shadowColor.g << " " << tr->shadowColor.b << " " << tr->shadowColor.a << " "
            << tr->shadowOffset.x << " " << tr->shadowOffset.y << " " << tr->align << " "
            << (tr->outline ? 1 : 0) << " "
            << tr->outlineColor.r << " " << tr->outlineColor.g << " " << tr->outlineColor.b << " " << tr->outlineColor.a
            << " " << (tr->bold ? 1 : 0)
            << " " << tr->size.x << " " << tr->size.y << " " << (tr->vcenter ? 1 : 0)
            << " " << (tr->background ? 1 : 0) << " "
            << tr->backgroundColor.r << " " << tr->backgroundColor.g << " "
            << tr->backgroundColor.b << " " << tr->backgroundColor.a
            << " " << tr->letterSpacing << " " << tr->lineSpacing
            << " " << (tr->uppercase ? 1 : 0) << " " << (tr->wrap ? 1 : 0)
            // Newer options (optional, back-compat): italic, gradient + bottom color,
            // typewriter (visibleChars + typeSpeed), bottom-align.
            << " " << (tr->italic ? 1 : 0) << " " << (tr->gradient ? 1 : 0) << " "
            << tr->colorBottom.r << " " << tr->colorBottom.g << " " << tr->colorBottom.b << " " << tr->colorBottom.a
            << " " << tr->visibleChars << " " << tr->typeSpeed << " " << (tr->alignBottom ? 1 : 0)
            << " " << Quote(tr->fontPath)            // optional TTF font path
            << " " << (tr->autoSize ? 1 : 0) << " " << tr->autoSizeMin << " " << tr->autoSizeMax
            << "\n";   // best-fit auto-size (newest)
    }
    for (auto* an : go->GetComponents<SpriteAnimator>()) {
        out << "  spriteanim " << an->fps << " " << (an->loop ? 1 : 0) << " "
            << (an->playing ? 1 : 0) << " " << an->frames.size();
        for (const auto& f : an->frames) out << " " << Quote(f);
        out << " " << an->atlasColumns << " " << an->atlasRows << " " << an->atlasCount;
        if (!an->clips.empty()) {   // optional named clips (newest): n {name row fps loop nframes frames...}
            out << " " << an->clips.size();
            for (const auto& c : an->clips) {
                out << " " << Quote(c.name) << " " << c.row << " " << c.fps << " " << (c.loop ? 1 : 0)
                    << " " << c.frames.size();
                for (const auto& f : c.frames) out << " " << Quote(f);
            }
        }
        out << "\n";
    }
    for (auto* au : go->GetComponents<AudioSource>()) {   // a music bed and a sound effect can sit on one object
        out << "  audio " << Quote(au->clipPath) << " " << au->volume << " "
            << (au->loop ? 1 : 0) << " " << (au->playOnAwake ? 1 : 0) << " "
            << (au->spatial ? 1 : 0) << " " << au->minDistance << " " << au->maxDistance << " "
            << au->pitch << " " << au->pan << " " << (int)au->group << " " << (int)au->rolloff << " "
            << au->lowPass << " " << (au->stream ? 1 : 0);
        // The spatial-effect fields are only written when one of them is not the
        // default, so an older reader (and the stream flag an older file strips)
        // still sees the line it always did. All off keeps today's sound.
        const bool spatialFx = au->doppler || std::fabs(au->dopplerScale - 1.0f) > 1.0e-6f
            || au->occlude || std::fabs(au->occlusionVolume - 0.25f) > 1.0e-6f
            || std::fabs(au->occlusionLowPass - 800.0f) > 1.0e-3f
            || au->distanceLowPass || std::fabs(au->distanceLowPassHz - 2000.0f) > 1.0e-3f;
        if (spatialFx)
            out << " " << (au->doppler ? 1 : 0) << " " << au->dopplerScale << " "
                << (au->occlude ? 1 : 0) << " " << au->occlusionVolume << " " << au->occlusionLowPass << " "
                << (au->distanceLowPass ? 1 : 0) << " " << au->distanceLowPassHz;
        if (!au->caption.empty()) out << " " << Quote(au->caption);   // after the optional spatial effects
        out << "\n";
    }
    for (auto* mp : go->GetComponents<MusicPlayer>()) {
        out << "  music " << (mp->playOnStart ? 1 : 0) << " " << (mp->shuffle ? 1 : 0) << " "
            << (mp->loopPlaylist ? 1 : 0) << " " << mp->crossfade << " " << mp->volume << " " << mp->tracks.size();
        for (const auto& t : mp->tracks) out << " " << Quote(t);
        out << " " << (mp->stream ? 1 : 0) << "\n";
    }
    for (auto* anim : go->GetComponents<Animator>()) {
        out << "  animator " << anim->speed << " " << (anim->playing ? 1 : 0) << " "
            << (anim->clip.loop ? 1 : 0) << " " << Quote(anim->clip.name)
            << " " << anim->clip.Tracks().size();
        for (const auto& tr : anim->clip.Tracks()) {
            out << " " << Quote(tr.first) << " " << tr.second.Keys().size();
            for (const auto& k : tr.second.Keys()) out << " " << k.time << " " << k.value;
        }
        out << "\n";
        // Clip length, per-track interpolation and events (separate record so
        // older builds still load the animator line).
        const AnimationClip& ac = anim->clip;
        bool anyLinear = false;
        for (const auto& tr : ac.Tracks()) if (!tr.second.smooth) anyLinear = true;
        if (ac.Spanned() || anyLinear || !ac.events.empty()) {
            out << "  animx " << (ac.Spanned() ? ac.SpanLength() : -1.0f) << " " << ac.events.size();
            for (const auto& e : ac.events) out << " " << e.time << " " << Quote(e.name);
            int nl = 0; for (const auto& tr : ac.Tracks()) if (!tr.second.smooth) ++nl;
            out << " " << nl;
            for (const auto& tr : ac.Tracks()) if (!tr.second.smooth) out << " " << Quote(tr.first);
            out << "\n";
        }
    }
    for (auto* tm : go->GetComponents<Tilemap>()) {
        out << "  tilemap " << tm->tileSize << " " << tm->Width() << " " << tm->Height();
        for (int t : tm->Tiles()) out << " " << t;
        out << "\n";
    }
    for (TilemapCollider2D* dupKeep_ : go->GetComponents<TilemapCollider2D>()) { (void)dupKeep_; out << "  tilemapcollider\n"; }
    for (auto* btn : go->GetComponents<UIButton>()) {
        out << "  uibutton " << Quote(btn->label) << " "
            << btn->position.x << " " << btn->position.y << " "
            << btn->size.x << " " << btn->size.y << " "
            << btn->color.r << " " << btn->color.g << " " << btn->color.b << " " << btn->color.a << " "
            << btn->hoverColor.r << " " << btn->hoverColor.g << " " << btn->hoverColor.b << " " << btn->hoverColor.a << " "
            << btn->textColor.r << " " << btn->textColor.g << " " << btn->textColor.b << " " << btn->textColor.a
            << " " << (int)btn->anchor << " "
            << btn->pressedColor.r << " " << btn->pressedColor.g << " " << btn->pressedColor.b << " " << btn->pressedColor.a << " "
            << btn->disabledColor.r << " " << btn->disabledColor.g << " " << btn->disabledColor.b << " " << btn->disabledColor.a << " "
            << (btn->interactable ? 1 : 0) << " " << (btn->focusable ? 1 : 0) << " "
            << btn->cornerRadius << " " << btn->fontScale << " " << btn->borderWidth << " "
            << btn->borderColor.r << " " << btn->borderColor.g << " " << btn->borderColor.b << " " << btn->borderColor.a
            << " " << btn->hoverScale
            << " " << Quote(btn->icon) << " " << btn->iconSize
            << " " << btn->hoverTextColor.r << " " << btn->hoverTextColor.g << " "
            << btn->hoverTextColor.b << " " << btn->hoverTextColor.a
            << " " << btn->transitionSpeed << " " << (btn->toggleMode ? 1 : 0)
            << " " << (btn->isOn ? 1 : 0) << " " << btn->pressOffset
            << " " << (btn->iconRight ? 1 : 0) << " " << (btn->holdRepeat ? 1 : 0)
            << " " << btn->repeatDelay << " " << btn->repeatInterval
            // extended (back-compatible trailing fields): shape + drop shadow (+softness)
            << " " << (int)btn->shape << " " << (btn->shadow ? 1 : 0) << " "
            << btn->shadowColor.r << " " << btn->shadowColor.g << " " << btn->shadowColor.b << " " << btn->shadowColor.a
            << " " << btn->shadowOffset.x << " " << btn->shadowOffset.y
            << " " << btn->shadowSoftness
            // extended: assigned OnClick action (target object + public function)
            << " " << Quote(btn->clickTarget) << " " << Quote(btn->clickFunction)
            << " " << btn->clickArg
            << " " << Quote(btn->fontPath)   // optional TTF label font
            // grok/ui-options: ease, duration, optional focus color (trailing, older scenes omit it)
            << " " << btn->transitionEase << " " << btn->transitionDuration
            << " " << (btn->separateFocus ? 1 : 0) << " "
            << btn->focusColor.r << " " << btn->focusColor.g << " "
            << btn->focusColor.b << " " << btn->focusColor.a << "\n";
    }
    for (auto* pn : go->GetComponents<UIPanel>()) {
        out << "  uipanel " << pn->position.x << " " << pn->position.y << " "
            << pn->size.x << " " << pn->size.y << " "
            << pn->color.r << " " << pn->color.g << " " << pn->color.b << " " << pn->color.a
            << " " << (int)pn->anchor << " "
            << pn->cornerRadius << " " << pn->borderWidth << " "
            << pn->borderColor.r << " " << pn->borderColor.g << " " << pn->borderColor.b << " " << pn->borderColor.a << " "
            << (pn->useGradient ? 1 : 0) << " "
            << pn->colorBottom.r << " " << pn->colorBottom.g << " " << pn->colorBottom.b << " " << pn->colorBottom.a << " "
            << (pn->shadow ? 1 : 0) << " "
            << pn->shadowColor.r << " " << pn->shadowColor.g << " " << pn->shadowColor.b << " " << pn->shadowColor.a << " "
            << pn->shadowOffset.x << " " << pn->shadowOffset.y
            // extended (back-compatible trailing fields): shape + gradient direction + soft shadow
            << " " << (int)pn->shape << " " << (pn->gradientHorizontal ? 1 : 0)
            << " " << pn->shadowSoftness
            // newest trailing block: gradient direction enum + outer outline + top sheen
            << " " << (int)pn->gradientDir
            << " " << pn->outlineWidth << " "
            << pn->outlineColor.r << " " << pn->outlineColor.g << " " << pn->outlineColor.b << " " << pn->outlineColor.a
            << " " << (pn->topHighlight ? 1 : 0) << " "
            << pn->highlightColor.r << " " << pn->highlightColor.g << " " << pn->highlightColor.b << " " << pn->highlightColor.a
            << " " << pn->cornerMask   // newest: per-corner rounding bitmask
            << "\n";
    }
    for (auto* doc : go->GetComponents<UIDocument>()) {
        out << "  uidocument " << Quote(doc->markup) << "\n";
    }
    for (auto* nm : go->GetComponents<NetworkManager>()) {
        out << "  network " << (int)nm->autoStart << " " << nm->autoPort << " "
            << Quote(nm->autoHost) << " " << Quote(nm->startName) << " "
            << Quote(nm->startRoom) << " "
            << nm->maxPlayers << " " << nm->snapshotRate << " "
            << Quote(nm->serverName) << " " << Quote(nm->password);
        const bool tunedPrediction = nm->reconcileRate != 12.0f || nm->reconcileSnap != 2.0f;
        if (nm->netTickRate > 0.0f || nm->authoritativeMovement || tunedPrediction)   // only when set: other scenes don't change
            out << " " << nm->netTickRate << " " << (nm->authoritativeMovement ? 1 : 0);
        if (tunedPrediction) out << " " << nm->reconcileRate << " " << nm->reconcileSnap;
        out << "\n";
        for (const auto& pf : nm->spawnablePrefabs) out << "  netprefab " << Quote(pf) << "\n";
    }
    for (auto* wu : go->GetComponents<WorldUI>()) {
        out << "  worldui " << Quote(wu->text) << " "
            << wu->color.r << " " << wu->color.g << " " << wu->color.b << " " << wu->color.a << " "
            << wu->background.r << " " << wu->background.g << " " << wu->background.b << " " << wu->background.a << " "
            << wu->worldOffset.x << " " << wu->worldOffset.y << " " << wu->worldOffset.z << " "
            << wu->pixelSize << " " << (wu->scaleWithDistance ? 1 : 0) << " "
            << wu->maxDistance << " " << wu->bar << " "
            << wu->barColor.r << " " << wu->barColor.g << " " << wu->barColor.b << " " << wu->barColor.a << "\n";
    }
    for (auto* in : go->GetComponents<UIInputField>()) {
        out << "  uiinput " << in->position.x << " " << in->position.y << " "
            << in->size.x << " " << in->size.y << " " << (int)in->anchor << " "
            << in->maxLength << " " << Quote(in->text) << " " << Quote(in->placeholder) << " "
            << in->color.r << " " << in->color.g << " " << in->color.b << " " << in->color.a << " "
            << (int)in->contentType
            // extended (back-compatible trailing fields): shape + corner + focus ring
            << " " << (int)in->shape << " " << in->cornerRadius << " " << in->borderWidth << " "
            << in->borderColor.r << " " << in->borderColor.g << " " << in->borderColor.b << " " << in->borderColor.a
            << " " << (in->multiline ? 1 : 0);   // grok/ui-controls
        // pad / keyboard menu navigation: written only when off, so default scenes stay byte for byte the same (absent = on)
        if (!in->focusable) out << " 0";
        out << "\n";
    }
    for (auto* tb : go->GetComponents<UITabs>()) {
        out << "  uitabs " << tb->position.x << " " << tb->position.y << " "
            << tb->size.x << " " << tb->size.y << " " << (int)tb->anchor << " " << tb->value << " "
            << tb->background.r << " " << tb->background.g << " " << tb->background.b << " " << tb->background.a << " "
            << tb->selected.r << " " << tb->selected.g << " " << tb->selected.b << " " << tb->selected.a << " "
            << tb->textColor.r << " " << tb->textColor.g << " " << tb->textColor.b << " " << tb->textColor.a << " "
            << tb->selectedTextColor.r << " " << tb->selectedTextColor.g << " " << tb->selectedTextColor.b << " " << tb->selectedTextColor.a << " "
            << (int)tb->shape << " " << tb->cornerRadius << " " << (tb->interactable ? 1 : 0) << " "
            << tb->tabs.size();
        for (const auto& t : tb->tabs) out << " " << Quote(t);
        // Extended (back-compatible trailing): content page name per tab.
        out << " " << tb->pages.size();
        for (const auto& p : tb->pages) out << " " << Quote(p);
        // --- appended (v3): tab style, sizing, states, motion, keys, per-tab extras ---
        out << " " << (int)tb->style << " " << (int)tb->sizing << " " << tb->spacing << " " << tb->padding
            << " " << tb->fontScale << " " << tb->minTabSize << " " << tb->indicatorThickness << " " << tb->iconSize
            << " " << tb->hoverColor.r << " " << tb->hoverColor.g << " " << tb->hoverColor.b << " " << tb->hoverColor.a
            << " " << tb->pressedColor.r << " " << tb->pressedColor.g << " " << tb->pressedColor.b << " " << tb->pressedColor.a
            << " " << tb->badgeColor.r << " " << tb->badgeColor.g << " " << tb->badgeColor.b << " " << tb->badgeColor.a
            << " " << tb->badgeTextColor.r << " " << tb->badgeTextColor.g << " " << tb->badgeTextColor.b << " " << tb->badgeTextColor.a
            << " " << (tb->animate ? 1 : 0) << " " << tb->animSpeed
            << " " << (tb->hotkeys ? 1 : 0) << " " << Quote(tb->prevKey) << " " << Quote(tb->nextKey)
            << " " << (tb->wrapAround ? 1 : 0) << " " << (tb->focusable ? 1 : 0)
            << " " << tb->icons.size();
        for (const auto& ic : tb->icons) out << " " << Quote(ic);
        out << " " << tb->badges.size();
        for (int b : tb->badges) out << " " << b;
        out << " " << tb->disabled.size();
        for (bool d : tb->disabled) out << " " << (d ? 1 : 0);
        out << " " << (tb->closable ? 1 : 0) << "\n";   // grok/ui-controls
    }
    for (auto* dd : go->GetComponents<UIDropdown>()) {
        out << "  uidropdown " << dd->position.x << " " << dd->position.y << " "
            << dd->size.x << " " << dd->size.y << " " << (int)dd->anchor << " "
            << dd->value << " "
            << dd->color.r << " " << dd->color.g << " " << dd->color.b << " " << dd->color.a << " "
            << dd->hoverColor.r << " " << dd->hoverColor.g << " " << dd->hoverColor.b << " " << dd->hoverColor.a << " "
            << dd->listColor.r << " " << dd->listColor.g << " " << dd->listColor.b << " " << dd->listColor.a << " "
            << dd->textColor.r << " " << dd->textColor.g << " " << dd->textColor.b << " " << dd->textColor.a << " "
            << dd->borderColor.r << " " << dd->borderColor.g << " " << dd->borderColor.b << " " << dd->borderColor.a << " "
            << dd->options.size();
        for (const auto& opt : dd->options) out << " " << Quote(opt);
        out << " " << (dd->interactable ? 1 : 0) << " " << Quote(dd->placeholder)
            << " " << (int)dd->shape << " " << dd->cornerRadius
            << " " << (dd->searchable ? 1 : 0) << " " << dd->maxVisible << " " << dd->optionIcons.size();
        for (const auto& ic : dd->optionIcons) out << " " << Quote(ic);   // grok/ui-controls
        out << "\n";
    }
    for (auto* tb : go->GetComponents<UITextBind>()) {
        out << "  uibind " << Quote(tb->format) << "\n";
    }
    for (auto* bb : go->GetComponents<UIBarBind>()) {
        out << "  uibarbind " << Quote(bb->var) << " " << bb->min << " " << bb->max << "\n";
    }
    for (auto* dg : go->GetComponents<UIDraggable>()) {
        out << "  uidraggable " << (dg->returnToStart ? 1 : 0) << " " << (dg->anyTarget ? 1 : 0)
            << " " << (dg->snapToSlot ? 1 : 0)
            << " " << (int)dg->axis << " " << dg->dragThreshold << " " << (dg->bringToFront ? 1 : 0) << "\n";
    }
    for (auto* dt : go->GetComponents<UIDropTarget>()) {
        out << "  uidroptarget " << Quote(dt->acceptTag)
            << " " << (dt->showHighlight ? 1 : 0)
            << " " << dt->highlight.r << " " << dt->highlight.g
            << " " << dt->highlight.b << " " << dt->highlight.a
            // extended (optional, back-compat): bg + border + reject + snap
            << " " << (dt->drawBackground ? 1 : 0)
            << " " << dt->background.r << " " << dt->background.g << " " << dt->background.b << " " << dt->background.a
            << " " << dt->cornerRadius << " " << dt->borderWidth
            << " " << dt->borderColor.r << " " << dt->borderColor.g << " " << dt->borderColor.b << " " << dt->borderColor.a
            << " " << dt->rejectHighlight.r << " " << dt->rejectHighlight.g << " " << dt->rejectHighlight.b << " " << dt->rejectHighlight.a
            << " " << (dt->snapToCenter ? 1 : 0) << "\n";
    }
    for (auto* dg : go->GetComponents<Draggable>()) {
        out << "  draggable " << (dg->returnToStart ? 1 : 0) << " " << (dg->anyTarget ? 1 : 0)
            << " " << (dg->snapToZone ? 1 : 0)
            << " " << (int)dg->axis << " " << dg->dragThreshold << " " << (dg->bringToFront ? 1 : 0)
            << " " << dg->gridX << " " << dg->gridY << " " << dg->dragScale << "\n";
    }
    for (auto* dz : go->GetComponents<DropZone>()) {
        out << "  dropzone " << Quote(dz->acceptTag) << "\n";
    }
    for (auto* tt : go->GetComponents<UITooltip>()) {
        out << "  uitooltip " << Quote(tt->text) << " " << tt->delay << " "
            << tt->background.r << " " << tt->background.g << " " << tt->background.b << " " << tt->background.a << " "
            << tt->textColor.r << " " << tt->textColor.g << " " << tt->textColor.b << " " << tt->textColor.a << " "
            << tt->borderColor.r << " " << tt->borderColor.g << " " << tt->borderColor.b << " " << tt->borderColor.a
            << " " << (tt->richText ? 1 : 0) << " " << tt->offset.x << " " << tt->offset.y << "\n";   // grok/ui-controls
    }
    for (auto* ua : go->GetComponents<UIAnimation>()) {   // every instance
        out << "  uianim " << (int)ua->intro << " " << (int)ua->loop << " " << (int)ua->outro << " "
            << ua->duration << " " << ua->delay << " " << ua->distance << " " << (int)ua->ease << " "
            << ua->loopSpeed << " " << ua->loopAmount << " " << (ua->playOnStart ? 1 : 0) << " "
            << (ua->replayOnShow ? 1 : 0) << " " << (ua->hideAfterOutro ? 1 : 0) << "\n";
    }
    for (TweenAnimation* ta : go->GetComponents<TweenAnimation>()) {
        out << "  tweenanim " << ta->type << " " << ta->endValue.x << " " << ta->endValue.y << " " << ta->endValue.z
            << " " << ta->endColor.r << " " << ta->endColor.g << " " << ta->endColor.b << " " << ta->endColor.a
            << " " << ta->endFloat << " " << (ta->relative ? 1 : 0) << " " << (ta->from ? 1 : 0)
            << " " << ta->duration << " " << ta->delay << " " << ta->ease << " " << ta->loops << " " << ta->loopType
            << " " << (ta->autoPlay ? 1 : 0) << " " << (ta->autoKill ? 1 : 0) << " " << (ta->unscaled ? 1 : 0)
            << " " << ta->vibrato << " " << ta->elasticity << " " << ta->jumps << " " << (ta->enabled ? 1 : 0)
            << " " << Quote(ta->id);
        // Optional tail (older files stop after the id): clock channel, ease as text; then, only when one of
        // them is set, decay, speed based and tag. A reader that stops after the ease text sees the numbers as
        // an unknown field and steps over the rest of the line.
        const float decay = std::isfinite(ta->decay) && ta->decay > 0.0f ? ta->decay : 0.0f;
        const bool newTail = decay > 0.0f || ta->speedBased || !ta->tag.empty();
        if (!ta->clock.empty() || !ta->easeCurve.empty() || newTail) out << " " << Quote(ta->clock) << " " << Quote(ta->easeCurve);
        if (newTail) out << " " << decay << " " << (ta->speedBased ? 1 : 0) << " " << Quote(ta->tag);
        out << "\n";
    }
    for (auto* lg : go->GetComponents<UILayoutGroup>()) {
        out << "  uilayout " << (int)lg->direction << " " << (int)lg->anchor << " "
            << lg->origin.x << " " << lg->origin.y << " " << lg->spacing << " " << lg->padding
            << " " << lg->columns << " " << lg->spacingY
            << " " << lg->boxSize.x << " " << lg->boxSize.y << " " << (int)lg->justify << " " << (int)lg->align
            << " " << (lg->wrap ? 1 : 0) << "\n";   // flex fields (optional, newer)
    }
    for (auto* fi : go->GetComponents<UIFlexItem>()) {
        out << "  uiflexitem " << fi->grow << " " << fi->shrink << " " << fi->baseSize.x << " " << fi->baseSize.y
            << " " << (int)fi->alignSelf << "\n";
    }
    for (auto* mk : go->GetComponents<UIMask>()) out << "  uimask " << (mk->showGraphic ? 1 : 0) << "\n";
    for (auto* md : go->GetComponents<UIModal>()) {
        auto c4 = [&](const Color& c) { out << " " << c.r << " " << c.g << " " << c.b << " " << c.a; };
        out << "  uimodal " << (md->dimBackground ? 1 : 0) << " "
            << md->dimColor.r << " " << md->dimColor.g << " " << md->dimColor.b << " " << md->dimColor.a << " "
            << (md->closeOnCancel ? 1 : 0) << " " << (md->closeOnClickOutside ? 1 : 0);
        // grok/ui-toast: optional style tail. Old scenes stop after the two flags.
        out << " " << (md->styleCard ? 1 : 0) << " " << md->cardRadius;
        c4(md->cardColor); c4(md->titleColor); c4(md->bodyColor); c4(md->confirmColor); c4(md->cancelColor);
        out << " " << Quote(md->titleText) << " " << Quote(md->bodyText)
            << " " << Quote(md->confirmText) << " " << Quote(md->cancelText) << "\n";
    }
    for (auto* toast : go->GetComponents<UIToast>()) {
        // grok/ui-toast. Live toast cards are ordinary children and are not listed here.
        out << "  uitoast " << toast->corner << " " << toast->duration << " " << toast->maxStack << " "
            << toast->margin << " " << toast->gap << " " << toast->width << " " << toast->height << " "
            << (toast->showIcon ? 1 : 0) << "\n";
    }
    for (auto* cm : go->GetComponents<UIContextMenu>()) {
        auto c4 = [&](const Color& c) { out << " " << c.r << " " << c.g << " " << c.b << " " << c.a; };
        out << "  uicontext " << (cm->anywhere ? 1 : 0) << " " << cm->itemHeight << " " << cm->width << " " << cm->textSize;
        c4(cm->background); c4(cm->hoverColor); c4(cm->textColor); c4(cm->borderColor);
        out << " " << cm->items.size();
        for (const std::string& it : cm->items) out << " " << Quote(it);
        out << "\n";
    }
    for (auto* kb : go->GetComponents<UIKeybindField>()) {
        auto c4 = [&](const Color& c) { out << " " << c.r << " " << c.g << " " << c.b << " " << c.a; };
        out << "  uikeybind " << kb->position.x << " " << kb->position.y << " " << kb->size.x << " " << kb->size.y
            << " " << (int)kb->anchor << " " << Quote(kb->action) << " " << (int)(unsigned char)kb->key
            << " " << (int)(unsigned char)kb->defaultKey << " " << Quote(kb->prefKey) << " " << kb->textSize;
        c4(kb->background); c4(kb->keyColor); c4(kb->listenColor); c4(kb->textColor);
        out << " " << (kb->interactable ? 1 : 0) << " " << (kb->focusable ? 1 : 0)
            << " " << Quote(kb->inputAction) << " " << (kb->gamepad ? 1 : 0) << " " << kb->part << "\n";
    }
    for (auto* fr : go->GetComponents<UIFrame>()) {
        out << "  uiframe " << fr->anchorMin.x << " " << fr->anchorMin.y << " " << fr->anchorMax.x << " " << fr->anchorMax.y
            << " " << fr->pivot.x << " " << fr->pivot.y << " " << fr->position.x << " " << fr->position.y
            << " " << fr->size.x << " " << fr->size.y
            << " " << fr->rotation << " " << fr->scale.x << " " << fr->scale.y << "\n";
    }
    for (auto* sv : go->GetComponents<UIScrollView>()) {
        out << "  uiscroll " << sv->position.x << " " << sv->position.y << " "
            << sv->size.x << " " << sv->size.y << " " << (int)sv->anchor << " "
            << sv->contentHeight << " "
            << sv->background.r << " " << sv->background.g << " " << sv->background.b << " " << sv->background.a
            << " " << (int)sv->axis << " " << sv->contentWidth << " " << (sv->autoContent ? 1 : 0)
            << " " << sv->inertia << " " << (sv->elastic ? 1 : 0) << " " << sv->barStyle << "\n";   // grok/ui-controls
    }
    for (auto* lv : go->GetComponents<UIListView>()) {
        out << "  uilist " << lv->position.x << " " << lv->position.y << " " << lv->size.x << " " << lv->size.y
            << " " << (int)lv->anchor << " "
            << lv->background.r << " " << lv->background.g << " " << lv->background.b << " " << lv->background.a << " "
            << lv->barColor.r << " " << lv->barColor.g << " " << lv->barColor.b << " " << lv->barColor.a << " "
            << lv->itemHeight << " " << lv->spacing << " " << lv->itemCount << " " << lv->selected << " "
            << lv->wheelSpeed << " " << lv->items.size();
        for (const std::string& it : lv->items) out << " " << Quote(it);
        out << "\n";
    }
    for (auto* cv : go->GetComponents<Canvas>()) {
        out << "  canvas " << (int)cv->scaleMode << " "
            << cv->referenceResolution.x << " " << cv->referenceResolution.y << " "
            << cv->matchWidthOrHeight << " " << cv->scaleFactor << " " << cv->sortOrder
            << " " << (cv->visible ? 1 : 0) << " " << cv->opacity            // optional, back-compat
            << " " << (cv->worldSpace ? 1 : 0) << " " << cv->designResolution.x
            << " " << cv->designResolution.y << " " << cv->worldPixelsPerUnit
            << " " << (cv->billboard ? 1 : 0)                                  // world-space canvas (optional)
            << " " << (int)cv->screenMatch << "\n";                             // newest: screen match mode
    }
    for (EventSystem* dupKeep_ : go->GetComponents<EventSystem>()) { (void)dupKeep_;
        out << "  eventsystem\n";
    }
    for (auto* w3 : go->GetComponents<WorldSpaceUI>()) {
        out << "  worldui3d " << w3->pixelsPerUnit << " " << (w3->billboard ? 1 : 0)
            << " " << (w3->constantSize ? 1 : 0) << " " << w3->constantScale << "\n";
    }
    for (auto* pb : go->GetComponents<UIProgressBar>()) {
        out << "  uiprogress " << pb->position.x << " " << pb->position.y << " "
            << pb->size.x << " " << pb->size.y << " " << pb->value << " "
            << pb->background.r << " " << pb->background.g << " " << pb->background.b << " " << pb->background.a << " "
            << pb->fill.r << " " << pb->fill.g << " " << pb->fill.b << " " << pb->fill.a
            << " " << (int)pb->anchor << " "
            << pb->cornerRadius << " " << (pb->showPercent ? 1 : 0) << " "
            << pb->textColor.r << " " << pb->textColor.g << " " << pb->textColor.b << " " << pb->textColor.a
            << " " << (int)pb->fillDir
            // extended (back-compatible trailing fields): shape + gradient fill
            << " " << (int)pb->shape << " " << (pb->gradientFill ? 1 : 0) << " "
            << pb->fillEnd.r << " " << pb->fillEnd.g << " " << pb->fillEnd.b << " " << pb->fillEnd.a
            // grok/ui-options: segments, damage trail, color-by-value, text format
            << " " << pb->segments << " " << pb->segmentGap << " " << pb->trailDelay << " "
            << pb->trailColor.r << " " << pb->trailColor.g << " " << pb->trailColor.b << " " << pb->trailColor.a
            << " " << (pb->colorByValue ? 1 : 0) << " "
            << pb->lowColor.r << " " << pb->lowColor.g << " " << pb->lowColor.b << " " << pb->lowColor.a << " "
            << pb->highColor.r << " " << pb->highColor.g << " " << pb->highColor.b << " " << pb->highColor.a
            << " " << Quote(pb->textFormat) << "\n";
    }
    for (auto* rp : go->GetComponents<UIRadialProgress>()) {
        out << "  uiradial " << rp->position.x << " " << rp->position.y << " "
            << rp->size.x << " " << rp->size.y << " " << rp->value << " "
            << rp->thickness << " " << rp->startAngle << " " << (rp->clockwise ? 1 : 0) << " "
            << rp->background.r << " " << rp->background.g << " " << rp->background.b << " " << rp->background.a << " "
            << rp->fill.r << " " << rp->fill.g << " " << rp->fill.b << " " << rp->fill.a
            << " " << (int)rp->anchor << " " << (rp->showPercent ? 1 : 0) << " "
            << rp->textColor.r << " " << rp->textColor.g << " " << rp->textColor.b << " " << rp->textColor.a
            << " " << (rp->spin ? 1 : 0) << " " << rp->spinSpeed << "\n";   // extended (back-compatible)
    }
    for (auto* mm : go->GetComponents<Minimap>()) {
        out << "  minimap " << mm->position.x << " " << mm->position.y << " "
            << mm->size.x << " " << mm->size.y << " "
            << mm->background.r << " " << mm->background.g << " " << mm->background.b << " " << mm->background.a << " "
            << mm->border.r << " " << mm->border.g << " " << mm->border.b << " " << mm->border.a << " "
            << mm->borderWidth << " " << Quote(mm->target) << " "
            << mm->targetColor.r << " " << mm->targetColor.g << " " << mm->targetColor.b << " " << mm->targetColor.a << " "
            << mm->worldPerPixel << " " << (mm->useXZ ? 1 : 0) << " " << mm->blipSize
            << " " << (int)mm->anchor << " "
            // --- appended (v2): circular / rotation / arrow / clamp / grid ---
            << (mm->circular ? 1 : 0) << " " << (mm->rotateWithTarget ? 1 : 0) << " "
            << (mm->playerArrow ? 1 : 0) << " " << (mm->clampBlips ? 1 : 0) << " "
            << (mm->showGrid ? 1 : 0) << " "
            << mm->gridColor.r << " " << mm->gridColor.g << " " << mm->gridColor.b << " " << mm->gridColor.a << " "
            << mm->gridSpacing
            // --- appended (v3): GTA-style custom map ---
            << " " << Quote(mm->mapTexture) << " " << mm->mapWorldSize
            << " " << mm->mapWorldCenter.x << " " << mm->mapWorldCenter.y
            << " " << (int)(unsigned char)mm->fullscreenKey << " " << mm->fullscreenZoom
            << " " << mm->fullscreenFrac
            // --- appended (v4): zoom keys + waypoint markers ---
            << " " << (int)(unsigned char)mm->zoomInKey << " " << (int)(unsigned char)mm->zoomOutKey
            << " " << mm->minZoom << " " << mm->maxZoom << " " << mm->zoomStep
            << " " << mm->markers.size();
        for (const auto& mk : mm->markers)
            out << " " << mk.world.x << " " << mk.world.y << " "
                << mk.color.r << " " << mk.color.g << " " << mk.color.b << " " << mk.color.a << " " << mk.size;
        // --- appended (v5): self/cone/rings/north ---
        out << " " << (mm->showSelf ? 1 : 0)
            << " " << (mm->viewCone ? 1 : 0) << " " << mm->viewConeAngle << " " << mm->viewConeLength
            << " " << mm->viewConeColor.r << " " << mm->viewConeColor.g << " " << mm->viewConeColor.b << " " << mm->viewConeColor.a
            << " " << mm->rangeRings
            << " " << mm->ringColor.r << " " << mm->ringColor.g << " " << mm->ringColor.b << " " << mm->ringColor.a
            << " " << (mm->showNorth ? 1 : 0)
            << " " << mm->northColor.r << " " << mm->northColor.g << " " << mm->northColor.b << " " << mm->northColor.a
            // --- appended (v6): labels + route line + per-marker labels ---
            << " " << (mm->showLabels ? 1 : 0)
            << " " << mm->labelColor.r << " " << mm->labelColor.g << " " << mm->labelColor.b << " " << mm->labelColor.a
            << " " << mm->labelSize
            << " " << mm->routeMarker
            << " " << mm->routeColor.r << " " << mm->routeColor.g << " " << mm->routeColor.b << " " << mm->routeColor.a
            << " " << mm->routeWidth;
        for (const auto& mk : mm->markers) out << " " << Quote(mk.label);
        // --- appended (v7): authorable vector map shapes ---
        out << " " << mm->mapShapes.size();
        for (const auto& sh : mm->mapShapes) {
            out << " " << (int)sh.kind
                << " " << sh.color.r << " " << sh.color.g << " " << sh.color.b << " " << sh.color.a
                << " " << sh.thickness << " " << (sh.filled ? 1 : 0)
                << " " << sh.points.size();
            for (const auto& p : sh.points) out << " " << p.x << " " << p.y;
        }
        // --- appended (v8): radar blip range ---
        out << " " << mm->blipRange;
        // --- appended (v9): user waypoint (click-to-place) ---
        out << " " << (mm->mapClickWaypoint ? 1 : 0) << " " << (mm->hasUserWaypoint ? 1 : 0)
            << " " << mm->userWaypoint.x << " " << mm->userWaypoint.y
            << " " << mm->userWaypointColor.r << " " << mm->userWaypointColor.g
            << " " << mm->userWaypointColor.b << " " << mm->userWaypointColor.a;
        out << "\n";
    }
    for (auto* bl : go->GetComponents<MinimapBlip>()) {
        out << "  minimapblip " << bl->color.r << " " << bl->color.g << " " << bl->color.b << " " << bl->color.a << " "
            << bl->size << " " << (bl->square ? 1 : 0) << " "
            // --- appended (v2): shape enum + heading rotation ---
            << (int)bl->shape << " " << (bl->rotateWithObject ? 1 : 0)
            // --- appended (v3): icon texture, (v4): label ---
            << " " << Quote(bl->icon) << " " << Quote(bl->label) << "\n";
    }
    for (auto* cr : go->GetComponents<Crosshair>()) {
        out << "  crosshair " << cr->position.x << " " << cr->position.y << " "
            << cr->size.x << " " << cr->size.y << " "
            << cr->color.r << " " << cr->color.g << " " << cr->color.b << " " << cr->color.a << " "
            << cr->thickness << " " << cr->gap << " " << cr->length << " "
            << (cr->showLines ? 1 : 0) << " " << (cr->dot ? 1 : 0) << " " << cr->dotSize << " "
            << cr->dotColor.r << " " << cr->dotColor.g << " " << cr->dotColor.b << " " << cr->dotColor.a << " "
            << (cr->outline ? 1 : 0) << " "
            << cr->outlineColor.r << " " << cr->outlineColor.g << " " << cr->outlineColor.b << " " << cr->outlineColor.a
            << " " << (int)cr->anchor;
        // Style block (appended; numeric-peek guarded on read).
        out << " " << (cr->lineUp ? 1 : 0) << " " << (cr->lineDown ? 1 : 0)
            << " " << (cr->lineLeft ? 1 : 0) << " " << (cr->lineRight ? 1 : 0) << " " << cr->spread
            << " " << (cr->circle ? 1 : 0) << " " << cr->circleRadius << " " << cr->circleThickness
            << " " << cr->circleColor.r << " " << cr->circleColor.g << " " << cr->circleColor.b << " " << cr->circleColor.a
            << " " << cr->spreadRecover << " " << cr->spreadMax;   // grok/ui-hud
        out << "\n";
    }
    for (auto* sg : go->GetComponents<UISlotGrid>()) {
        out << "  uislotgrid " << sg->position.x << " " << sg->position.y << " " << (int)sg->anchor << " "
            << sg->columns << " " << sg->rows << " " << sg->cell.x << " " << sg->cell.y << " " << sg->gap << " "
            << sg->value << " "
            << sg->color.r << " " << sg->color.g << " " << sg->color.b << " " << sg->color.a << " "
            << sg->selectedColor.r << " " << sg->selectedColor.g << " " << sg->selectedColor.b << " " << sg->selectedColor.a << " "
            << sg->hoverColor.r << " " << sg->hoverColor.g << " " << sg->hoverColor.b << " " << sg->hoverColor.a << " "
            << sg->labels.size();
        for (const auto& lb : sg->labels) out << " " << Quote(lb);
        out << "\n";
    }
    for (auto* cp : go->GetComponents<UIColorPicker>()) { // grok/ui-color
        out << "  uicolorpicker " << cp->position.x << " " << cp->position.y << " " << (int)cp->anchor << " "
            << cp->swatch << " " << cp->gap << " " << cp->value << " " << cp->swatches.size();
        for (const Color& c : cp->swatches)
            out << " " << c.r << " " << c.g << " " << c.b << " " << c.a;
        if (cp->advanced || cp->useCustom)   // slice 3: the advanced mode, appended so an older reader stops after the swatches
            out << " " << (cp->advanced ? 1 : 0) << " " << cp->hue << " " << cp->sat << " " << cp->val << " " << cp->alpha << " "
                << (cp->useCustom ? 1 : 0) << " " << cp->squareSize;
        out << "\n";
    }
    for (auto* sa : go->GetComponents<UISafeArea>())
        out << "  uisafearea " << (sa->left ? 1 : 0) << " " << (sa->top ? 1 : 0) << " " << (sa->right ? 1 : 0)
            << " " << (sa->bottom ? 1 : 0) << " " << (sa->enabled ? 1 : 0) << "\n";
    for (auto* ri : go->GetComponents<UIRawImage>()) {
        out << "  uirawimage " << ri->position.x << " " << ri->position.y << " " << ri->size.x << " " << ri->size.y
            << " " << ri->color.r << " " << ri->color.g << " " << ri->color.b << " " << ri->color.a
            << " " << ri->uvX << " " << ri->uvY << " " << ri->uvW << " " << ri->uvH
            << " " << ri->scroll.x << " " << ri->scroll.y << " " << (ri->enabled ? 1 : 0)
            << " " << Quote(ri->texture) << " " << (int)ri->anchor << "\n";
    }
    for (auto* im : go->GetComponents<UIImage>()) {
        out << "  uiimage " << im->position.x << " " << im->position.y << " "
            << im->size.x << " " << im->size.y << " "
            << im->color.r << " " << im->color.g << " " << im->color.b << " " << im->color.a << " "
            << Quote(im->texture) << " " << (int)im->anchor << " "
            << (im->nineSlice ? 1 : 0) << " " << im->border << " "
            << (int)im->fillMode << " " << im->fillAmount << " " << im->cornerRadius
            << " " << (int)im->shape   // extended (back-compatible trailing field)
            // newest trailing block: flip + preserve-aspect + frame border
            << " " << (im->flipX ? 1 : 0) << " " << (im->flipY ? 1 : 0)
            << " " << (im->preserveAspect ? 1 : 0)
            << " " << im->borderWidth << " "
            << im->borderColor.r << " " << im->borderColor.g << " " << im->borderColor.b << " " << im->borderColor.a
            // newest: per-corner rounding mask + drop shadow
            << " " << im->cornerMask
            << " " << (im->shadow ? 1 : 0) << " "
            << im->shadowColor.r << " " << im->shadowColor.g << " " << im->shadowColor.b << " " << im->shadowColor.a << " "
            << im->shadowOffset.x << " " << im->shadowOffset.y << " " << im->shadowSoftness
            << "\n";
    }
    for (auto* sl : go->GetComponents<UISlider>()) {
        out << "  uislider " << sl->position.x << " " << sl->position.y << " "
            << sl->size.x << " " << sl->size.y << " "
            << sl->value << " " << sl->minValue << " " << sl->maxValue << " "
            << sl->background.r << " " << sl->background.g << " " << sl->background.b << " " << sl->background.a << " "
            << sl->fill.r << " " << sl->fill.g << " " << sl->fill.b << " " << sl->fill.a << " "
            << sl->knob.r << " " << sl->knob.g << " " << sl->knob.b << " " << sl->knob.a
            << " " << (int)sl->anchor << " "
            << sl->cornerRadius << " " << sl->knobSize << " " << (sl->showValue ? 1 : 0) << " "
            << sl->textColor.r << " " << sl->textColor.g << " " << sl->textColor.b << " " << sl->textColor.a
            << " " << (sl->wholeNumbers ? 1 : 0)
            << " " << (sl->interactable ? 1 : 0)
            << " " << (sl->vertical ? 1 : 0)
            // extended (back-compatible trailing fields): track shape + round knob
            << " " << (int)sl->trackShape << " " << (sl->roundKnob ? 1 : 0)
            // grok/ui-options: step, fill gradient, handle shape, value format
            << " " << sl->step << " " << (sl->fillGradient ? 1 : 0) << " "
            << sl->fillEnd.r << " " << sl->fillEnd.g << " " << sl->fillEnd.b << " " << sl->fillEnd.a
            << " " << sl->knobShape << " " << Quote(sl->valueFormat) << "\n";
    }
    for (auto* st : go->GetComponents<UIStepper>()) {
        out << "  uistepper " << st->position.x << " " << st->position.y << " "
            << st->size.x << " " << st->size.y << " "
            << st->value << " " << st->minValue << " " << st->maxValue << " " << st->step << " "
            << (st->wholeNumbers ? 1 : 0) << " " << (st->wrap ? 1 : 0) << " "
            << st->background.r << " " << st->background.g << " " << st->background.b << " " << st->background.a << " "
            << st->button.r << " " << st->button.g << " " << st->button.b << " " << st->button.a << " "
            << st->textColor.r << " " << st->textColor.g << " " << st->textColor.b << " " << st->textColor.a << " "
            << (int)st->anchor << " " << (int)st->shape << " " << st->cornerRadius << " "
            << (st->interactable ? 1 : 0) << "\n";
    }
    WriteMoreUI(out, go);
    for (auto* rt : go->GetComponents<UIRating>()) {
        out << "  uirating " << rt->position.x << " " << rt->position.y << " "
            << rt->size.x << " " << rt->size.y << " "
            << rt->count << " " << rt->value << " " << (rt->allowHalf ? 1 : 0) << " " << (rt->readOnly ? 1 : 0) << " "
            << rt->on.r << " " << rt->on.g << " " << rt->on.b << " " << rt->on.a << " "
            << rt->off.r << " " << rt->off.g << " " << rt->off.b << " " << rt->off.a << " "
            << (int)rt->anchor << "\n";
    }
    for (auto* tg : go->GetComponents<UIToggle>()) {
        out << "  uitoggle " << Quote(tg->label) << " "
            << tg->position.x << " " << tg->position.y << " "
            << tg->size.x << " " << tg->size.y << " " << (tg->on ? 1 : 0) << " "
            << tg->boxColor.r << " " << tg->boxColor.g << " " << tg->boxColor.b << " " << tg->boxColor.a << " "
            << tg->checkColor.r << " " << tg->checkColor.g << " " << tg->checkColor.b << " " << tg->checkColor.a << " "
            << tg->textColor.r << " " << tg->textColor.g << " " << tg->textColor.b << " " << tg->textColor.a
            << " " << (int)tg->anchor << " " << tg->cornerRadius
            << " " << (int)tg->style << " "
            << tg->knobColor.r << " " << tg->knobColor.g << " " << tg->knobColor.b << " " << tg->knobColor.a
            << " " << (tg->interactable ? 1 : 0)
            << " " << tg->animSpeed << "\n";   // extended (back-compatible trailing field)
    }
    for (auto* ps : go->GetComponents<ParticleSystem>()) {   // every instance; its particlesfx / particleswind lines follow it
        out << "  particles " << ps->emissionRate << " " << ps->maxParticles << " "
            << (ps->playing ? 1 : 0) << " " << ps->startLifetime << " " << ps->startSize << " "
            << ps->startColor.r << " " << ps->startColor.g << " " << ps->startColor.b << " "
            << ps->startColor.a << " " << ps->startVelocity.x << " " << ps->startVelocity.y << " "
            << ps->velocityRandom << " " << ps->gravity.x << " " << ps->gravity.y << " "
            << (ps->fadeOverLife ? 1 : 0) << " " << ps->seed << " "
            // --- appended (v2): over-lifetime, shapes, bursts, duration ---
            << ps->endColor.r << " " << ps->endColor.g << " " << ps->endColor.b << " "
            << ps->endColor.a << " " << (ps->colorOverLife ? 1 : 0) << " "
            << ps->endSize << " " << (ps->sizeOverLife ? 1 : 0) << " "
            << ps->startLifetimeRandom << " " << ps->startSizeRandom << " " << ps->speedRandom << " "
            << (int)ps->shape << " " << ps->shapeRadius << " " << ps->shapeAngle << " "
            << ps->boxSize.x << " " << ps->boxSize.y << " "
            << ps->gravityModifier << " " << ps->damping << " "
            << ps->duration << " " << (ps->loop ? 1 : 0) << " "
            << ps->burstCount << " " << ps->burstTime << " "
            // --- appended (v3): 3D z-components for the now-3D vectors ---
            << ps->startVelocity.z << " " << ps->gravity.z << " " << ps->boxSize.z << " "
            // --- appended (v4): sprite texture + billboard rotation ---
            << Quote(ps->texture) << " " << ps->startRotation << " " << ps->startRotationRandom << " "
            << ps->rotationSpeed << " " << ps->rotationSpeedRandom << " "
            // --- appended (v5): ground-plane collision ---
            << (ps->collision ? 1 : 0) << " " << ps->collisionY << " " << ps->bounce << " "
            << ps->collisionFriction << " " << ps->collisionLifeLoss << " "
            // --- appended (v6): render mode (billboard/stretch) ---
            << (int)ps->renderMode << " " << ps->stretchScale << "\n";
        // Gradients/curves + motion modules live on their own line (read after the
        // "particles" line above); older scenes simply don't have it.
        out << "  particlesfx 1 " << (int)ps->simulationSpace << " " << ps->inheritVelocity << " "
            << ps->noiseStrength << " " << ps->noiseFrequency << " " << ps->noiseScroll << " "
            << ps->orbitSpeed << " " << ps->orbitAxis.x << " " << ps->orbitAxis.y << " " << ps->orbitAxis.z << " "
            << ps->vortexStrength << " " << ps->vortexPull << " " << (ps->speedOverLife ? 1 : 0);
        const ParticleGradient& g = ps->colorGradient;
        out << " " << g.count;
        for (int i = 0; i < g.count; ++i)
            out << " " << g.stops[i].time << " " << g.stops[i].color.r << " " << g.stops[i].color.g
                << " " << g.stops[i].color.b << " " << g.stops[i].color.a;
        for (const ParticleCurve* c : {&ps->sizeCurve, &ps->speedCurve}) {
            out << " " << c->count << " " << (c->smooth ? 1 : 0);
            for (int i = 0; i < c->count; ++i) out << " " << c->keys[i].time << " " << c->keys[i].value;
        }
        out << "\n";
        // How much the scene's wind carries the particles (own record; 1 = the default).
        if (ps->windInfluence != 1.0f) out << "  particleswind " << ps->windInfluence << "\n";
        // Collide with Terrain (own record, written only when on; older files and readers do without it).
        if (ps->collideTerrain) out << "  particlesterrain 1\n";
    }
    // Sequence player (cutscenes). key=value record; the playback state is not saved.
    for (auto* sp : go->GetComponents<SequencePlayer>()) {   // every instance
        out << "  sequenceplayer";
        KvOut k{out};
        k.str("path", sp->sequencePath).b("auto", sp->playOnStart).b("loop", sp->loop)("speed", sp->timeScale)
         .key("skip", sp->skipKey).b("lock", sp->lockPlayer);
        out << "\n";
    }
    // The runtime UI components added with the twelve looks: one key=value record each (older builds skip them).
    for (auto* sv : go->GetComponents<UIStyleVariant>()) {
        out << "  uivariant";
        KvOut k{out};
        k("variant", sv->variant);
        if (!sv->themeName.empty()) k.str("vtheme", sv->themeName);
        if (!sv->style.empty()) k.str("vstyle", sv->style);
        if (!sv->styleSize.empty()) k.str("vsize", sv->styleSize);
        if (!sv->styleDensity.empty()) k.str("vdens", sv->styleDensity);
        out << "\n";
    }
    for (auto* dn : go->GetComponents<UIDamageNumbers>()) {
        out << "  uidamage";
        KvOut k{out};
        k("dnLife", dn->lifetime)("dnRise", dn->riseSpeed)("dnGrav", dn->gravity)("dnSpread", dn->spread)
         ("dnSize", dn->textSize)("dnCrit", dn->critScale)("dnPop", dn->popScale)("dnMax", dn->maxActive)
         ("dnAtX", dn->spawnAt.x)("dnAtY", dn->spawnAt.y)
         .col("dnNormal", dn->normalColor).col("dnCritC", dn->critColor).col("dnHeal", dn->healColor)
         .col("dnMiss", dn->missColor).col("dnInfo", dn->infoColor);
        out << "\n";
    }
    for (auto* db : go->GetComponents<UIDialogueBox>()) {
        out << "  uidialoguebox";
        KvOut k{out};
        k("dbCps", db->charsPerSecond).b("dbHide", db->hideWhenDone).b("dbClick", db->advanceOnClick).b("dbStartHidden", db->startHidden);
        out << "\n";
    }
    for (auto* ib : go->GetComponents<UIInventoryBind>()) {
        out << "  uiinvbind";
        KvOut k{out};
        k.str("inv", ib->inventoryName).b("fit", ib->fitRows)("fscale", ib->slotFontScale);
        out << "\n";
    }
    // Runtime UI extras (radio groups, pointer-blocking panels, draggable windows, theme roles, floating
    // numbers, dialogue boxes). One key=value record that is only written when something is non-default,
    // so older scenes are byte-for-byte the same and older builds skip it.
    {
        const auto panels = go->GetComponents<UIPanel>();
        const auto richTexts = go->GetComponents<TextRenderer>();
        const auto uixToggles = go->GetComponents<UIToggle>();
        const auto grids = go->GetComponents<UISlotGrid>();
        const auto drags = go->GetComponents<UIDraggable>();
        const int n = std::max({(int)panels.size(), (int)richTexts.size(), (int)uixToggles.size(),
                                (int)grids.size(), (int)drags.size()});
        for (int i = 0; i < n; ++i) {
            std::ostringstream xs; UseRoundTripFloats(xs);
            KvOut k{xs};
            if (auto* pn = AtComp(panels, i); pn && pn->blocksPointer) k.b("pblock", true);
            if (auto* rtx = AtComp(richTexts, i); rtx && rtx->richText) k.b("rich", true);
            if (auto* tg = AtComp(uixToggles, i); tg && !tg->radioGroup.empty()) k.str("rgroup", tg->radioGroup);
            if (auto* sg = AtComp(grids, i); sg && (sg->fontScale != 1.5f || !sg->numberEmptySlots || sg->textColor.r != 1.0f || sg->textColor.g != 1.0f || sg->textColor.b != 1.0f || sg->textColor.a != 1.0f))
                k("sgFont", sg->fontScale).b("sgNum", sg->numberEmptySlots).col("sgText", sg->textColor);
            if (auto* dg = AtComp(drags, i)) {
                if (dg->dragParent) k.b("dragParent", true);
                if (dg->keepOnScreen) k.b("keepOnScreen", true);
            }
            if (xs.str().empty()) continue;
            if (i > 0) k("i", i);
            out << "  uix" << xs.str() << "\n";
        }
    }
    // Localization keys, one record per instance. The first instance omits `i` (the line a
    // single text field has always written). A later instance adds `i=<index>`.
    {
        const auto texts = go->GetComponents<TextRenderer>();
        const auto buttons = go->GetComponents<UIButton>();
        const auto toggles = go->GetComponents<UIToggle>();
        const auto tips = go->GetComponents<UITooltip>();
        const auto worlds = go->GetComponents<WorldUI>();
        const auto inputs = go->GetComponents<UIInputField>();
        const auto holds = go->GetComponents<HoldPrompt>();
        const auto pauses = go->GetComponents<PauseMenu>();
        const auto loads = go->GetComponents<LoadingScreen>();
        const int n = std::max({(int)texts.size(), (int)buttons.size(), (int)toggles.size(), (int)tips.size(),
                                (int)worlds.size(), (int)inputs.size(), (int)holds.size(), (int)pauses.size(),
                                (int)loads.size()});
        for (int i = 0; i < n; ++i) {
            std::ostringstream ls; UseRoundTripFloats(ls);
            KvOut k{ls};
            if (auto* tr = AtComp(texts, i); tr && !tr->loc.key.empty()) k.str("text", tr->loc.key);
            if (auto* b = AtComp(buttons, i); b && !b->loc.key.empty()) k.str("button", b->loc.key);
            if (auto* t = AtComp(toggles, i); t && !t->loc.key.empty()) k.str("toggle", t->loc.key);
            if (auto* tt = AtComp(tips, i); tt && !tt->loc.key.empty()) k.str("tooltip", tt->loc.key);
            if (auto* w = AtComp(worlds, i); w && !w->loc.key.empty()) k.str("world", w->loc.key);
            if (auto* inf = AtComp(inputs, i); inf && !inf->loc.key.empty()) k.str("input", inf->loc.key);
            if (auto* hp = AtComp(holds, i); hp && hp->localized) k.b("hold", true);
            if (auto* pm = AtComp(pauses, i); pm && pm->localized) k.b("pause", true);
            if (auto* lsc = AtComp(loads, i); lsc && lsc->localized) k.b("loading", true);
            if (ls.str().empty()) continue;
            if (i > 0) k("i", i);
            out << "  loc" << ls.str() << "\n";
        }
    }
    // Navigation: key=value records of their own, so older scenes load unchanged.
    // The baked mesh stays in the .okaynav file the surface points at.
    for (auto* ns : go->GetComponents<NavMeshSurface>()) {
        out << "  navsurface";
        KvOut k{out};
        k("radius", ns->settings.agentRadius)("height", ns->settings.agentHeight)("slope", ns->settings.maxSlope)
         ("step", ns->settings.maxStep)("cell", ns->settings.cellSize)("cellh", ns->settings.cellHeight)
         .str("path", ns->navPath).b("autob", ns->autoBounds)
         ("bcx", ns->boundsCenter.x)("bcy", ns->boundsCenter.y)("bcz", ns->boundsCenter.z)
         ("bsx", ns->boundsSize.x)("bsy", ns->boundsSize.y)("bsz", ns->boundsSize.z)
         .b("dyn", ns->includeDynamic)("layers", ns->layerMask).b("bakeplay", ns->bakeBeforePlay);
        for (int a = 0; a < kNavAreaCount; ++a) {   // only what differs from the defaults
            if (ns->areaCost[a] != NavAreaDefaultCost(a)) k(("ac" + std::to_string(a)).c_str(), ns->areaCost[a]);
            if (!ns->areaName[a].empty()) k.str(("an" + std::to_string(a)).c_str(), ns->areaName[a]);
        }
        out << "\n";
    }
    for (auto* na : go->GetComponents<NavMeshAgent>()) {
        out << "  navagent";
        KvOut k{out};
        k("radius", na->radius)("height", na->height)("speed", na->speed)("accel", na->acceleration)
         ("angular", na->angularSpeed)("stop", na->stoppingDistance).b("repath", na->autoRepath)
         ("repathT", na->repathInterval)("prio", na->avoidancePriority)("avoid", na->avoidanceRange)
         .b("brake", na->autoBraking).b("face", na->faceMovement).b("anim", na->driveAnimation)("base", na->baseOffset)
         ("mask", na->areaMask);
        out << "\n";
    }
    for (auto* nm : go->GetComponents<NavMeshModifier>()) {
        out << "  navmod";
        KvOut k{out};
        k("area", nm->area).b("kids", nm->applyToChildren);
        out << "\n";
    }
    for (auto* nl : go->GetComponents<NavMeshLink>()) {
        out << "  navlink";
        KvOut k{out};
        k("type", (int)nl->type)("sx", nl->startPoint.x)("sy", nl->startPoint.y)("sz", nl->startPoint.z)
         ("ex", nl->endPoint.x)("ey", nl->endPoint.y)("ez", nl->endPoint.z).b("both", nl->bidirectional)
         ("cost", nl->cost)("area", nl->area)("speed", nl->speed)("arc", nl->arcHeight);
        out << "\n";
    }
    for (auto* no : go->GetComponents<NavMeshObstacle>()) {
        out << "  navobstacle";
        KvOut k{out};
        k("sx", no->size.x)("sy", no->size.y)("sz", no->size.z)("cx", no->center.x)("cy", no->center.y)("cz", no->center.z)
         ("shape", (int)no->shape)("cr", no->capsuleRadius)("ch", no->capsuleHeight).b("carve", no->carve)
         .b("still", no->carveOnlyWhenStationary)("tts", no->timeToStationary)("move", no->moveThreshold);
        out << "\n";
    }
    // ---- Dialogue and quests: own key=value records, so older scenes skip nothing
    // they don't have and newer keys are ignored by older builds. ----
    for (auto* st : go->GetComponents<StoryState>()) {
        out << "  storystate";
        KvOut k{out};
        k("n", (int)st->vars.size());
        int i = 0;
        for (const auto& kv : st->vars) {
            const std::string p = std::to_string(i++);
            const char* type = kv.second.IsBool() ? "b" : kv.second.IsString() ? "s" : "n";
            k.str(("k" + p).c_str(), kv.first).str(("t" + p).c_str(), type).str(("v" + p).c_str(), kv.second.AsString());
        }
        k("f", (int)st->flags.size());
        i = 0;
        for (const auto& f : st->flags) k.str(("f" + std::to_string(i++)).c_str(), f);
        out << "\n";
    }
    for (auto* dr : go->GetComponents<DialogueRunner>()) {
        out << "  dialoguerunner";
        KvOut k{out};
        k.str("file", dr->graphFile)("mode", dr->startMode).key("key", dr->interactKey)("range", dr->interactRange)
         .str("prompt", dr->promptText).str("pobj", dr->promptObject).b("pause", dr->pausePlayer)
         ("type", dr->typewriterSpeed)("auto", dr->autoAdvance).b("ui", dr->drawUI).b("cancel", dr->canCancel)
         ("start", dr->startNode).str("player", dr->playerName);
        if (!dr->cameraShots.empty()) {   // per-line camera shots; not written when there are none (old records stay the same)
            k("shots", (int)dr->cameraShots.size());
            for (std::size_t i = 0; i < dr->cameraShots.size(); ++i) {
                const std::string p = "sh" + std::to_string(i);
                k((p + "n").c_str(), dr->cameraShots[i].node).str((p + "c").c_str(), dr->cameraShots[i].camera);
            }
        }
        if (dr->shotBlend != -1.0f) k("shb", dr->shotBlend);
        out << "\n";
    }
    for (auto* ql : go->GetComponents<QuestLog>()) {
        out << "  questlog";
        KvOut k{out};
        k.str("file", ql->questFile).b("tracker", ql->showTracker).b("journal", ql->allowJournal).key("jkey", ql->journalKey)
         .b("notify", ql->showNotifications).b("ui", ql->drawUI).str("tracked", ql->tracked).str("player", ql->playerName)
         ("n", (int)ql->records.size());
        for (std::size_t i = 0; i < ql->records.size(); ++i) {
            const QuestRecord& r = ql->records[i];
            const std::string p = "q" + std::to_string(i);
            k.str((p + "id").c_str(), r.id)((p + "st").c_str(), (int)r.status)((p + "c").c_str(), r.completions)
             ((p + "n").c_str(), (int)r.progress.size());
            for (std::size_t o = 0; o < r.progress.size(); ++o) {
                const std::string q = p + "p" + std::to_string(o);
                k(q.c_str(), r.progress[o])((p + "d" + std::to_string(o)).c_str(), o < r.done.size() ? r.done[o] : 0);
            }
        }
        out << "\n";
        // The quests typed into the inspector, one record each, right after the log.
        for (const QuestDef& q : ql->quests) {
            out << "  questlogdef";
            KvOut kq{out};
            std::string pre;
            for (const auto& x : q.prerequisites) { if (!pre.empty()) pre += ','; pre += x; }
            kq.str("id", q.id).str("title", q.title).str("desc", q.description).b("rep", q.repeatable).str("pre", pre)
              ("nobj", (int)q.objectives.size());
            for (std::size_t i = 0; i < q.objectives.size(); ++i) {
                const auto& ob = q.objectives[i];
                const std::string p = "o" + std::to_string(i);
                kq.str((p + "k").c_str(), QuestObjective::KindName(ob.kind)).str((p + "d").c_str(), ob.description)
                  .str((p + "t").c_str(), ob.target)((p + "n").c_str(), ob.count).b((p + "opt").c_str(), ob.optional);
            }
            kq("nri", (int)q.reward.items.size());
            for (std::size_t i = 0; i < q.reward.items.size(); ++i)
                kq.str(("r" + std::to_string(i) + "i").c_str(), q.reward.items[i].first)(("r" + std::to_string(i) + "n").c_str(), q.reward.items[i].second);
            kq("nrv", (int)q.reward.vars.size());
            for (std::size_t i = 0; i < q.reward.vars.size(); ++i)
                kq.str(("v" + std::to_string(i) + "k").c_str(), q.reward.vars[i].first).str(("v" + std::to_string(i) + "v").c_str(), q.reward.vars[i].second);
            kq.str("revent", q.reward.event);
            out << "\n";
        }
    }
    for (auto* qt : go->GetComponents<QuestTrigger>()) {   // every instance
        out << "  questtrigger";
        KvOut{out}.str("zone", qt->zone).str("tag", qt->triggerTag).b("once", qt->once).str("quest", qt->questId)("obj", qt->objective);
        out << "\n";
    }
    // Behavior tree runner: a key=value record of its own, so older scenes load
    // unchanged. The tree itself stays in the .okaybt file it points at.
    for (auto* bt : go->GetComponents<BehaviorTreeRunner>()) {
        out << "  btrunner";
        KvOut k{out};
        k.str("tree", bt->treePath)("rate", bt->tickRate).b("auto", bt->runOnStart).b("debug", bt->debug)
         .b("homeset", bt->homeOverride)("hx", bt->homePoint.x)("hy", bt->homePoint.y)("hz", bt->homePoint.z)
         ("n", (int)bt->overrides.size());
        for (std::size_t i = 0; i < bt->overrides.size(); ++i) {
            const std::string kk = "k" + std::to_string(i), vk = "v" + std::to_string(i);
            k.str(kk.c_str(), bt->overrides[i].first).str(vk.c_str(), bt->overrides[i].second);
        }
        out << "\n";
    }
    // Splines and what builds on them, lines and trails: key=value records of their
    // own (a spline's points one per line), so older scenes load unchanged. Generated
    // meshes and placed items are not saved: they are built again from these.
    for (auto* sp : go->GetComponents<Spline>()) {
        out << "  spline";
        KvOut{out}("type", (int)sp->type).b("closed", sp->closed)("up", (int)sp->upMode)("res", sp->resolution);
        out << "\n";
        for (const SplinePoint& p : sp->points) {
            out << "  splinept";
            KvOut{out}("x", p.position.x)("y", p.position.y)("z", p.position.z)
                ("ix", p.tangentIn.x)("iy", p.tangentIn.y)("iz", p.tangentIn.z)
                ("ox", p.tangentOut.x)("oy", p.tangentOut.y)("oz", p.tangentOut.z)
                ("mode", (int)p.mode)("roll", p.roll)("sx", p.scale.x)("sy", p.scale.y);
            out << "\n";
        }
    }
    for (auto* sf : go->GetComponents<SplineFollower>()) {
        out << "  splinefollower";
        KvOut{out}.str("spline", sf->spline)("drive", (int)sf->drive)("wrap", (int)sf->wrap)("speed", sf->speed)
            ("dur", sf->duration)("start", sf->startAt).b("auto", sf->playOnStart).b("orient", sf->orient)
            ("offx", sf->offset.x)("offy", sf->offset.y)("offz", sf->offset.z);
        out << "\n";
    }
    for (auto* sm : go->GetComponents<SplineMesh>()) {
        out << "  splinemesh";
        KvOut{out}.str("spline", sm->spline)("mode", (int)sm->mode)("profile", (int)sm->profile)("width", sm->width)
            ("height", sm->height)("sides", sm->sides)
            .str("custom", PointList(sm->customProfile))
            .b("cclosed", sm->closedProfile).b("caps", sm->capEnds)("seg", sm->segmentLength)("uv", sm->uvTileLength).str("src", sm->sourceMesh)
            ("axis", (int)sm->meshAxis)("spacing", sm->spacing)("msx", sm->meshScale.x)("msy", sm->meshScale.y)
            ("msz", sm->meshScale.z).b("fit", sm->stretchToFit).b("collider", sm->generateCollider);
        out << "\n";
    }
    for (auto* si : go->GetComponents<SplineInstancer>()) {
        out << "  splineinstancer";
        KvOut{out}.str("spline", si->spline).str("tmpl", si->templateName).str("prefab", si->prefabPath)
            .str("mesh", si->meshName)("spacing", si->spacing)("start", si->startOffset)("side", si->sideOffset)
            .b("both", si->bothSides)("jitter", si->jitter)("rotj", si->rotationJitter)("smin", si->scaleMin)
            ("smax", si->scaleMax)("seed", si->seed)("align", (int)si->align)("rx", si->rotationOffset.x)
            ("ry", si->rotationOffset.y)("rz", si->rotationOffset.z)("max", si->maxCount);
        out << "\n";
    }
    for (auto* lr : go->GetComponents<LineRenderer>()) {
        out << "  linerenderer";
        KvOut k{out};
        k.str("pts", PointList(lr->positions))
         .b("world", lr->useWorldSpace).b("loop", lr->loop)("width", lr->width).col("c", lr->color)
         ("corner", lr->cornerVertices)("cap", lr->capVertices)("align", (int)lr->alignment)
         ("texmode", (int)lr->textureMode)("tile", lr->tileLength).str("tex", lr->texture)
         .str("spline", lr->spline)("samples", lr->splineSamples);
        WriteCurveKv(k, "w", lr->widthCurve);
        WriteGradientKv(k, "g", lr->colorGradient);
        out << "\n";
    }
    // Components that registered with ComponentSerializerRegistry (trailrenderer, ...).
    ComponentSerializerRegistry::WriteAll(*go, out);
    WriteDisabledComponents(out, go);
}

} // namespace sceneser
} // namespace okay
