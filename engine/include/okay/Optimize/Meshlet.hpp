#pragma once
// ---------------------------------------------------------------------------
// Meshlets: a mesh cut into small clusters (<= 64 vertices, <= 126 triangles)
// with a bounding sphere and a normal cone each. This is phase 2 of the virtual
// geometry plan (docs/foundations/virtual_geometry.md). The point of a cluster
// is that a whole cluster can be thrown away with one test (outside the view,
// facing away, too far) instead of one test per triangle.
//
//   BuildMeshlets(mesh, opt)          mesh -> clusters
//   CullMeshlets(set, params, out)    CPU reference for GPU-driven culling
//
// 64 / 126 are the limits GPU mesh shaders like (a triangle index byte list
// fits 128 triangles with one spare slot; 126 keeps the primitive count a
// multiple the hardware packs well). Nothing here talks to a GPU: this file is
// the reference a future compute or mesh shader pass must match, and the data
// layout (vertex list + 8-bit local triangle indices) is what that pass reads.
// ---------------------------------------------------------------------------
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Culling.hpp"
#include "okay/Render/Mesh.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay::optimize {

constexpr int kMeshletMaxVertices  = 64;
constexpr int kMeshletMaxTriangles = 126;

struct Meshlet {
    std::uint32_t vertexOffset = 0;    ///< start in MeshletSet::vertices
    std::uint32_t vertexCount = 0;     ///< <= 64
    std::uint32_t triangleOffset = 0;  ///< start in MeshletSet::triangles, counted in triangles (x3 for bytes)
    std::uint32_t triangleCount = 0;   ///< <= 126
    Vec3  center{0, 0, 0};             ///< bounding sphere (mesh space)
    float radius = 0.0f;
    Vec3  coneAxis{0, 0, 1};           ///< average facing of the triangles (unit)
    float coneCutoff = 1.0f;           ///< 1 = no cone (cannot be back-face culled as a whole)
};

struct MeshletSet {
    std::vector<Meshlet>       meshlets;
    std::vector<std::uint32_t> vertices;   ///< mesh vertex indices, per meshlet in order
    std::vector<std::uint8_t>  triangles;  ///< 3 local indices (into the meshlet's vertex list) per triangle
    std::vector<int>           sourceTriangle; ///< parallel to meshlet triangles: the mesh triangle it came from
    int skippedTriangles = 0;              ///< unusable input triangles (bad index, NaN, zero area)
    int sourceTriangles = 0;
    int TriangleTotal() const { return (int)(triangles.size() / 3); }
};

struct MeshletOptions {
    int maxVertices = kMeshletMaxVertices;      ///< clamped to [3, 64]
    int maxTriangles = kMeshletMaxTriangles;    ///< clamped to [1, 126]
};

/// Deterministic. Every usable triangle lands in exactly one meshlet. Triangles
/// are ordered for locality: meshlets start in Morton order of triangle centres
/// and grow by sharing vertices, then closeness.
MeshletSet BuildMeshlets(const Mesh& mesh, const MeshletOptions& opt = MeshletOptions());

struct MeshletCullParams {
    Frustum frustum;                       ///< world space (Frustum::FromVP)
    Vec3    cameraPos{0, 0, 0};            ///< world space
    Mat4    world;                         ///< mesh -> world; rotation, translation and ONE uniform scale only
    float   maxDistance = 0.0f;            ///< <= 0: no distance limit
    bool    useFrustum = true;
    bool    useCone = true;                ///< back-face cone test (turn off for double-sided meshes)
    bool    useDistance = true;
};

struct MeshletCullStats {
    int total = 0, visible = 0;
    int culledFrustum = 0, culledCone = 0, culledDistance = 0;
    int visibleTriangles = 0;
};

/// Indices of the meshlets that survive. Never culls a meshlet that holds a
/// triangle BruteForceVisibleTriangles() would keep (the test suite checks this).
MeshletCullStats CullMeshlets(const MeshletSet& set, const MeshletCullParams& p, std::vector<std::uint32_t>& visible);

/// The slow truth: per triangle, using the same rules without any cluster data.
/// A triangle is visible when it is not wholly outside one frustum plane, faces
/// the camera (winding, counter-clockwise = front) and its nearest corner is
/// within maxDistance. Returns the mesh triangle numbers.
std::vector<int> BruteForceVisibleTriangles(const Mesh& mesh, const MeshletCullParams& p);

/// Mesh triangle numbers held by the given meshlets (sorted).
std::vector<int> TrianglesOfMeshlets(const MeshletSet& set, const std::vector<std::uint32_t>& meshletIds);

/// Checks the invariants (limits, index ranges, each triangle once, spheres
/// contain their points, cones hold their normals). Empty string = all good.
std::string ValidateMeshlets(const Mesh& mesh, const MeshletSet& set);

} // namespace okay::optimize
