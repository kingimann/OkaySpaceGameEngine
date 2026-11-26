#pragma once
// Mesh files a MeshRenderer can point at (MeshRenderer::meshPath): .okaymesh
// (the polygon mesh format, see PolyMesh) or .obj.
#include "okay/Render/Mesh.hpp"
#include <cstdint>
#include <string>

namespace okay::modeling {

/// Load `path` into `out`. If it isn't there, `fallbackDir` + its file name is
/// tried too (an exported game keeps its mesh files next to its scenes).
/// `texture` receives an OBJ's material texture, when it names one.
bool LoadMeshAsset(const std::string& path, Mesh& out, std::string* texture = nullptr,
                   std::string* error = nullptr, const std::string& fallbackDir = "");

/// The file LoadMeshAsset would open for `path` (the same fallback rule), or "" when none exists.
std::string ResolveMeshAssetPath(const std::string& path, const std::string& fallbackDir = "");

/// A decoded mesh file as the asset streamer keeps it (AssetManager type "MeshFile"): the geometry and an OBJ's
/// material texture. When the streamer already decoded a file on a job thread and it has not changed on disk since,
/// LoadMeshAsset copies it from there instead of reading the file again (docs/engine/asset_streaming.md).
struct MeshFile {
    Mesh mesh;
    std::string texture;
};
/// Decode a mesh file without looking in the streamer's cache (the "MeshFile" loader).
bool DecodeMeshFile(const std::string& file, MeshFile& out, std::string* error = nullptr);

/// Save `mesh` as a .okaymesh file (quads recovered from triangle pairs).
bool SaveMeshAsset(const std::string& path, const Mesh& mesh, std::string* error = nullptr);

/// A fingerprint of the geometry (positions, triangles, UVs, face colours), to
/// tell whether a mesh still matches the file it was loaded from.
std::uint64_t GeometryHash(const Mesh& mesh);

} // namespace okay::modeling
