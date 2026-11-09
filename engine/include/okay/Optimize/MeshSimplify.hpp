#pragma once
// ---------------------------------------------------------------------------
// Mesh simplification for LOD generation (feature map 27.1).
//
// Quadric error metric edge collapse (Garland-Heckbert) on the engine's
// triangle Mesh. Pure CPU geometry: no scene, renderer or component types.
//
//   SimplifyMesh(mesh, opt)       one simplified copy of a mesh
//   GenerateLodChain(mesh, {...}) a whole LOD chain + switch distances
//
// What is preserved
//   - Topology: collapses obey the link condition, so a closed manifold mesh
//     stays closed and manifold (its Euler characteristic never changes).
//   - Boundaries: open boundary edges are only collapsed along the boundary,
//     and a heavy quadric penalty keeps the outline where it was.
//   - UV seams and hard normals: vertices are welded by position first, then
//     every corner keeps its own attributes ("wedges"). An edge whose two
//     sides disagree about UV or normal is a seam and is treated like a
//     boundary, so textures and hard edges do not tear or smear.
//   - Attributes: UVs, normals, per-triangle colours and (optionally) skin
//     joint indices/weights ride along. Skin weights are given through
//     SimplifyOptions::skin (the engine keeps them on SkinnedMesh, not on
//     Mesh); collapses that would blend incompatible weights are refused.
//     Set skinPolicy = Refuse to treat skinned meshes as unsupported.
//
// Dropped on purpose: Mesh::polyFace and Mesh::seams (authoring data) and
// cornerUVs (the result has exact per-vertex UVs).
// ---------------------------------------------------------------------------
#include "okay/Render/Mesh.hpp"
#include <array>
#include <string>
#include <vector>

namespace okay::optimize {

/// Skin weights parallel to Mesh::vertices (the same layout as SkinnedMesh's
/// jointIdx / jointWt).
struct SkinData {
    std::vector<std::array<int, 4>>   joints;
    std::vector<std::array<float, 4>> weights;
    bool Empty() const { return joints.empty(); }
    bool Matches(const Mesh& m) const {
        return joints.size() == m.vertices.size() && weights.size() == m.vertices.size();
    }
};

enum class SkinPolicy {
    Preserve,   ///< simplify, keeping weights consistent (default)
    Refuse,     ///< skinned meshes are unsupported: return the mesh unchanged and flag it
};

struct SimplifyOptions {
    /// Fraction of triangles to keep, (0, 1]. 1 returns the mesh unchanged.
    float targetRatio = 0.5f;
    /// Absolute triangle target. When >= 0 it overrides targetRatio.
    int   targetTriangles = -1;
    /// Stop collapsing once the next collapse would move the surface by more
    /// than this (world units, RMS estimate). < 0 means no limit. With a
    /// positive maxError and targetRatio = 0 the mesh is simplified as far as
    /// the error allows.
    float maxError = -1.0f;
    /// Weight of the penalty that keeps open boundaries in place.
    float boundaryWeight = 32.0f;
    /// Weight of the penalty that keeps UV seams / hard normal edges in place.
    float seamWeight = 8.0f;
    /// Weight of the UV stretch term added to the collapse cost.
    float uvWeight = 0.02f;
    /// Never move or remove vertices on an open boundary.
    bool  lockBoundary = false;
    /// Never move or remove vertices on a UV seam / hard normal edge.
    bool  lockSeams = false;
    /// Allow collapses to a better-than-endpoint position for interior vertices.
    bool  optimalPosition = true;
    /// Vertices closer than this (fraction of the bounding box diagonal) are welded.
    float weldTolerance = 1e-5f;
    /// A collapse is refused when it turns a neighbouring triangle's normal
    /// below this cosine (prevents flips and folds).
    float minNormalDot = 0.25f;
    /// Never go below this many triangles (closed meshes never go below 4).
    int   minTriangles = 2;
    /// Optional skin weights (parallel to mesh.vertices).
    const SkinData* skin = nullptr;
    SkinPolicy skinPolicy = SkinPolicy::Preserve;
    /// Largest per-joint weight difference a collapse may blend.
    float skinTolerance = 0.25f;
};

struct SimplifyResult {
    Mesh     mesh;
    SkinData skin;                 ///< parallel to mesh.vertices when skin was given
    int      trianglesBefore = 0;
    int      trianglesAfter = 0;
    int      verticesBefore = 0;
    int      verticesAfter = 0;
    int      collapses = 0;
    float    error = 0.0f;         ///< largest collapse error reached (world units, RMS estimate)
    bool     reachedTarget = false;///< false when constraints (boundaries, seams, maxError) stopped it early
    bool     skinned = false;      ///< a skin was supplied
    bool     skinSupported = true; ///< false: skinned input was refused or inconsistent; mesh is unchanged
    std::string note;              ///< human readable reason when something was refused
};

/// Simplify a mesh. Deterministic: the same input gives the same output.
SimplifyResult SimplifyMesh(const Mesh& mesh, const SimplifyOptions& opt = SimplifyOptions());

struct LodChainOptions {
    SimplifyOptions base;          ///< settings used for every level (target fields are overridden)
    float fovYDeg = 60.0f;         ///< camera used to turn error into distance
    float viewportHeightPx = 1080.0f;
    float pixelError = 1.0f;       ///< a level is used once its error projects below this many pixels
    /// Largest world-space size of the object (bounding sphere diameter). <= 0 measures it from the mesh.
    float objectSize = -1.0f;
};

struct LodMeshLevel {
    Mesh     mesh;
    SkinData skin;
    float    ratio = 1.0f;            ///< requested fraction of triangles
    int      triangles = 0;
    float    error = 0.0f;            ///< world-space error of this level
    float    switchDistance = 0.0f;   ///< use this level from this distance on (0 for LOD0)
    float    switchScreenHeight = 1.0f;///< screen-relative height at that distance (1 for LOD0)
    bool     reachedTarget = true;
};

struct LodChain {
    std::vector<LodMeshLevel> levels;
    float objectSize = 0.0f;
    bool  skinSupported = true;
};

/// Build a LOD chain. `ratios` are the fraction of the original triangles each
/// level keeps (the first is normally 1.0 = the original). Every level is
/// simplified from the original mesh, triangle counts never increase down the
/// chain, and switch distances grow with every level.
LodChain GenerateLodChain(const Mesh& mesh, const std::vector<float>& ratios,
                          const LodChainOptions& opt = LodChainOptions());

/// Helpers shared with LodGroup: world size on screen as a fraction of the
/// viewport height for an object of `size` at `distance`.
float LodScreenHeightFor(float size, float distance, float fovYDeg);
/// The distance at which an object of `size` fills `screenHeight` of the viewport.
float LodDistanceFor(float size, float screenHeight, float fovYDeg);

/// Total surface area of a mesh (for checks and reports).
float MeshSurfaceArea(const Mesh& m);

} // namespace okay::optimize
