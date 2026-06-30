#pragma once
// ---------------------------------------------------------------------------
// ModelImport — one entry point for loading a 3D model file into a Mesh.
//
//   * .obj                -> built-in Mesh::LoadOBJ (always available)
//   * .gltf / .glb        -> built-in okay::LoadGLTF (always available)
//   * .fbx .dae .stl .ply -> Assimp, IF the engine was built with -DOKAY_USE_ASSIMP=ON
//                            (FetchContent pulls Assimp). Without it these return ok=false.
//
// This keeps the DEFAULT build fully self-contained (no downloads) while letting a
// project opt in to the long tail of formats via one CMake flag.
// ---------------------------------------------------------------------------
#include "okay/Render/Mesh.hpp"
#include <string>

namespace okay {

/// True if this build includes the Assimp importer (compiled with OKAY_USE_ASSIMP).
bool AssimpAvailable();

/// Space-separated list of extensions this build can import (depends on Assimp).
std::string ImportableExtensions();

/// Import a model file into one merged Mesh, routing by extension (see header notes).
/// `outTexture` receives a diffuse texture path when the format/loader provides one.
Mesh ImportModel(const std::string& path, bool* ok = nullptr, std::string* outTexture = nullptr);

} // namespace okay
