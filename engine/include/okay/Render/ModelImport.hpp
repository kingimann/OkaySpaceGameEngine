#pragma once
// ---------------------------------------------------------------------------
// ModelImport — one entry point for loading a 3D model file into a Mesh.
//
//   * .obj                -> built-in Mesh::LoadOBJ (always available)
//   * .gltf / .glb        -> built-in okay::LoadGLTF (always available)
//   * .fbx .dae .stl .ply .3ds .blend .x .md5mesh .smd .ms3d .lwo .dxf .off .ac .b3d
//                         -> Assimp, IF the engine was built with -DOKAY_USE_ASSIMP=ON
//                            (FetchContent pulls Assimp). Without it these return ok=false.
//
// This keeps the DEFAULT build fully self-contained (no downloads) while letting a
// project opt in to the long tail of formats via one CMake flag.
// ---------------------------------------------------------------------------
#include "okay/Render/Mesh.hpp"
#include <string>
#include <vector>

namespace okay {

class Scene;
class GameObject;
class ModelAnimator;

/// True if this build includes the Assimp importer (compiled with OKAY_USE_ASSIMP).
bool AssimpAvailable();

/// Space-separated, lower-case list of extensions this build can import
/// (".obj .gltf .glb .fbx" built in, plus every format the Assimp build reads).
std::string ImportableExtensions();

/// Import a model file into one merged Mesh, routing by extension (see header notes).
/// `outTexture` receives a diffuse texture path when the format/loader provides one.
Mesh ImportModel(const std::string& path, bool* ok = nullptr, std::string* outTexture = nullptr);

/// Import a model into the SCENE as a node hierarchy: one GameObject per glTF node
/// (with its local transform), meshes attached as MeshRenderers, and node animations
/// imported onto an Animator per animated node (rigid/hierarchical TRS — skinned-mesh
/// deformation isn't supported yet). Returns the root GameObject (or nullptr). For
/// non-glTF formats it falls back to a single mesh object. Use this from the editor's
/// import so a rigged glTF brings its parts + animation in, not just one static mesh.
GameObject* ImportModelScene(Scene& scene, const std::string& path, bool* ok = nullptr);

/// What an imported model came in with, for the editor's import message:
/// meshes, skinned meshes, bones (and whether they form one connected
/// skeleton), weights, and clips. `warnings` lists anything that will look
/// wrong (missing bones, unweighted vertices, a split skeleton...).
struct ModelReport {
    int objects = 0, meshes = 0, skinnedMeshes = 0, bones = 0, vertices = 0, triangles = 0;
    int maxInfluences = 0, unweightedVertices = 0, missingBones = 0, skeletonRoots = 0;
    int clips = 0;
    int blendShapes = 0;                ///< morph targets across the model's meshes
    std::size_t keys = 0, keysBeforeReduction = 0;   ///< animation keys now / as imported
    std::string rootBone;               ///< top of the skeleton ("" if not rigged)
    std::vector<std::string> clipNames;
    std::vector<std::string> warnings;
    std::string Summary() const;        ///< one line for the console
};
ModelReport AnalyzeModel(Scene& scene, GameObject* root);

/// Make an imported humanoid play the engine's BUILT-IN animations (idle,
/// walk, run, jump, gestures, emotions, authored clips) on its OWN skeleton and
/// skin: adds a Character (drive-external) + HumanoidRetarget to `modelRoot`,
/// matching bones by name.
/// Returns how many of the 15 humanoid bones were found (< 8 = not set up;
/// `outLog` says why).
int EnableBuiltinAnimations(Scene& scene, GameObject* modelRoot, std::string* outLog = nullptr);
/// How many of the 15 humanoid bones a model's skeleton has (0 = not a humanoid rig).
int CountHumanoidBones(Scene& scene, GameObject* modelRoot, std::string* why = nullptr);

/// World-space bounds of every mesh under `root` as currently posed (skinned
/// meshes deformed from their joints, whatever the renderer's skinning mode).
bool ModelBounds(Scene& scene, GameObject* root, Vec3& lo, Vec3& hi);
/// The imported model shown as `player`'s body: a child holding meshes /
/// a skeleton (not the blocky "Rig", not a camera), or the player itself when
/// a controller was added straight onto an imported model. nullptr if none.
GameObject* FindCharacterModelRoot(Scene& scene, GameObject* player);
/// Turn a character model so it faces the way controllers move (local -Z).
/// turnDegrees == 0: detect the facing from the skeleton's left/right bones
/// and fix it (returns how many degrees it was off, -1 if undetectable).
/// Otherwise: turn it by exactly that much about the player's up axis.
int FaceCharacterModel(Scene& scene, GameObject* player, GameObject* modelRoot = nullptr,
                       float turnDegrees = 0.0f);
/// Fit the player's Box/Capsule collider around its model (adding a capsule
/// and a Rigidbody3D when missing) so the controller collides with the body
/// you see. Returns a short summary for the console.
std::string FitCharacterCollider(Scene& scene, GameObject* player, GameObject* modelRoot = nullptr);

/// Make an imported model THE playable character of `player` (a controller
/// object): import `path` as a child, scale it to the character's height,
/// ground its feet on the player's origin, face it the way the body faces,
/// wire its ModelAnimator locomotion (idle/walk/run auto-switch from movement,
/// mapped from the clip names), and hide the default blocky Character body.
/// Returns the imported model root (or nullptr on failure). `outLog`, when
/// given, receives a one-line human-readable summary of what was wired.
GameObject* AttachCharacterModel(Scene& scene, GameObject* player,
                                 const std::string& path, std::string* outLog = nullptr);

/// What drives a player's imported model: which locomotion slots (Idle, Walk,
/// Run, Jump...) play one of the model's own clips and which play the engine's
/// built-in animation retargeted onto the skeleton.
struct CharacterModelInfo {
    struct Slot {
        int slot = 0;                 ///< ModelAnimator::Slot
        const char* label = "";
        std::string clip;             ///< the clip named in the slot ("" = none)
        int source = 0;               ///< 0 empty, 1 imported clip, 2 built-in animation
    };
    bool valid = false;               ///< the player has an imported model
    std::string model;                ///< the model object's name
    int mappedBones = 0, totalBones = 15;   ///< humanoid bones the built-in animation can pose
    int source = 0;                   ///< 0 none, 1 imported clips, 2 built-in retargeted, 3 mixed
    int importedSlots = 0, builtinSlots = 0;
    bool builtinAvailable = false;    ///< a retargeted Character is ready to fill empty slots
    bool characterOff = false;        ///< the default blocky Character is disabled (the model replaced it)
    ModelAnimator* animator = nullptr;
    std::vector<Slot> slots;
};
const char* CharacterAnimationSourceName(int source);
/// Read how `player`'s imported model is animated. False when it has none.
bool DescribeCharacterModel(Scene& scene, GameObject* player, CharacterModelInfo& out);
/// After a locomotion slot's clip changed on a retargeted model: clips drive the
/// slots that have one (movement-driven, in place) and the built-in animation
/// covers the rest. Does nothing for a model that isn't retargeted.
void SettleCharacterLocomotion(ModelAnimator* ma);
/// Re-run the clip auto-mapping on `player`'s model and switch the built-in
/// animation on for every locomotion slot still without a clip. Returns a
/// one-line summary for the console.
std::string FixCharacterAnimations(Scene& scene, GameObject* player);

/// Add the animation clips stored in `path` (Mixamo "without skin" downloads,
/// any FBX/glTF with takes) to the model under `modelRoot`. Bones are matched
/// by name, ignoring rig prefixes ("mixamorig:LeftArm" == "LeftArm"). When the
/// source skeleton is proportioned differently, only rotations transfer plus the
/// hips' motion scaled to this model's height (humanoid retargeting), so a clip
/// made for another character doesn't stretch this one. Idle/walk/run/jump are
/// mapped from the clip names if not set yet. Returns how many clips were added;
/// `outLog` gets a one-line summary (or why nothing was added).
/// Fill a model's empty locomotion clip slots (idle, walk, run, jump, fall,
/// land, crouch, crouch walk, prone, crawl, get up) from its clip names.
void AutoMapLocomotion(ModelAnimator* ma);
int AddAnimationsFromFile(Scene& scene, GameObject* modelRoot, const std::string& path,
                          std::string* outLog = nullptr);

} // namespace okay
