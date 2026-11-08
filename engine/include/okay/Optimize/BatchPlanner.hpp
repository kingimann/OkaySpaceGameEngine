#pragma once
// ---------------------------------------------------------------------------
// Draw call batching (feature map 27.3): a planner plus the static mesh merge.
//
// PlanBatches() takes a flat list of draw items (mesh, material, shader state,
// transform, static flag, bounds) and returns the draw calls to issue:
//
//   Instanced     identical mesh + material + state, drawn once with many
//                 transforms (all dynamic repeats, and static repeats past
//                 `staticInstancingThreshold`, where instancing saves memory)
//   StaticMerged  static items sharing material + state, packed into groups
//                 under a vertex budget in Morton (spatial) order so a merged
//                 mesh stays compact and still culls well
//   Single        everything else
//
// Batches are ordered to minimise state changes: shader state first, then
// material, then mesh, then front to back. Transparent items are kept apart,
// drawn last and sorted back to front, one draw each, because blending needs
// that exact order. Skinned items are never merged or instanced.
//
// The plan only describes indices into the input list; BuildMergedMeshes()
// turns the StaticMerged batches into real combined meshes with the
// transforms baked in. Nothing here touches a scene or a renderer.
// ---------------------------------------------------------------------------
#include "okay/Math/Bounds.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Render/Mesh.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace okay::optimize {

struct DrawItem {
    int id = 0;                    ///< caller's handle (object id)
    int meshId = 0;
    int materialId = 0;
    std::uint64_t stateKey = 0;    ///< shader + render state (blend, cull, depth) packed by the caller
    Mat4 transform;
    bool isStatic = false;
    bool transparent = false;
    bool skinned = false;
    Bounds bounds;                 ///< world-space bounds
    int vertexCount = 0;
    int triangleCount = 0;
};

struct BatchOptions {
    bool  enableInstancing = true;
    int   minInstances = 2;                 ///< fewer identical items than this are drawn singly
    int   maxInstancesPerBatch = 1024;
    bool  mergeStatic = true;
    int   staticVertexBudget = 65535;       ///< largest merged mesh (vertices)
    int   minMergeItems = 2;                ///< a merge group smaller than this is not merged
    /// A static mesh repeated at least this many times is instanced instead of merged.
    int   staticInstancingThreshold = 8;
    Vec3  cameraPosition{0, 0, 0};          ///< for front-to-back and back-to-front ordering
    float shaderChangeCost = 4.0f;          ///< weights used only for the cost estimate in the stats
    float materialChangeCost = 2.0f;
    float meshChangeCost = 1.0f;
};

enum class BatchKind { Single, Instanced, StaticMerged };

struct Batch {
    BatchKind kind = BatchKind::Single;
    int  meshId = -1;                       ///< -1 for merged batches
    int  materialId = 0;
    std::uint64_t stateKey = 0;
    bool transparent = false;
    std::vector<int> items;                 ///< indices into the input list
    int  vertexCount = 0;                   ///< vertices uploaded for the batch (merged: the sum; instanced: one mesh)
    int  triangleCount = 0;                 ///< triangles drawn by the batch (all instances)
    Bounds bounds;                          ///< union of the items' bounds
    int  InstanceCount() const { return static_cast<int>(items.size()); }
};

struct StateChanges {
    int shader = 0, material = 0, mesh = 0;
    int Total() const { return shader + material + mesh; }
};

struct BatchStats {
    int items = 0;
    int drawCallsBefore = 0;                ///< one per item
    int drawCallsAfter = 0;
    int singleBatches = 0, instancedBatches = 0, mergedBatches = 0;
    int instancedItems = 0, mergedItems = 0;
    StateChanges changesBefore, changesAfter;   ///< binds counted from an empty state, input order vs plan order
    float costBefore = 0.0f, costAfter = 0.0f;  ///< weighted by BatchOptions' change costs
    long long triangles = 0;
};

struct BatchPlan {
    std::vector<Batch> batches;             ///< in draw order
    BatchStats stats;
};

BatchPlan PlanBatches(const std::vector<DrawItem>& items, const BatchOptions& opt = BatchOptions());

/// A mesh plus the transform to bake in.
struct MergeInput {
    const Mesh* mesh = nullptr;
    Mat4 transform;
};

struct MergeResult {
    Mesh mesh;
    std::vector<int> firstVertex;           ///< per input: first vertex in the merged mesh (-1 if skipped)
    std::vector<int> firstTriangle;         ///< per input: first triangle
    int skipped = 0;                        ///< inputs with a null or empty mesh
    int batchIndex = -1;                    ///< BuildMergedMeshes: the plan batch this mesh replaces
};

/// Combine meshes into one with every transform baked in. Normals follow the inverse transpose,
/// mirrored transforms get their winding flipped, UVs / normals / per-triangle colours are kept
/// (inputs without them get neutral values), polygon ids and seams are dropped.
MergeResult MergeStaticMeshes(const std::vector<MergeInput>& inputs);

using MeshLookup = std::function<const Mesh*(int meshId)>;
/// One merged mesh per StaticMerged batch of the plan, in plan order (other batches are skipped).
std::vector<MergeResult> BuildMergedMeshes(const BatchPlan& plan, const std::vector<DrawItem>& items,
                                           const MeshLookup& lookup);

} // namespace okay::optimize
