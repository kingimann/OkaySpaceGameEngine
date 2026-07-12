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

namespace okay {

class Scene;
class GameObject;

/// True if this build includes the Assimp importer (compiled with OKAY_USE_ASSIMP).
bool AssimpAvailable();

/// Space-separated list of extensions this build can import (depends on Assimp).
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

/// Make an imported model THE playable character of `player` (a controller
/// object): import `path` as a child, scale it to the character's height,
/// ground its feet on the player's origin, face it the way the body faces,
/// wire its ModelAnimator locomotion (idle/walk/run auto-switch from movement,
/// mapped from the clip names), and hide the default blocky Character body.
/// Returns the imported model root (or nullptr on failure). `outLog`, when
/// given, receives a one-line human-readable summary of what was wired.
GameObject* AttachCharacterModel(Scene& scene, GameObject* player,
                                 const std::string& path, std::string* outLog = nullptr);

} // namespace okay
