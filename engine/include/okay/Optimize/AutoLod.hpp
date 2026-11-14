#pragma once
// ---------------------------------------------------------------------------
// AutoLod: one call that turns a mesh into a ready LOD chain (phase 1 of the
// virtual geometry plan, docs/foundations/virtual_geometry.md).
//
// It is a safe front door over optimize::GenerateLodChain (the quadric edge
// collapse simplifier in MeshSimplify.hpp). It adds:
//   - input cleaning: triangles with bad indices, NaN/infinite points or two
//     equal corners are skipped, so hostile meshes cannot crash the simplifier;
//   - the default chain 100% / 50% / 25% / 12.5% of the triangles;
//   - an error budget: a level that moves the surface by more than
//     maxErrorFraction of the object size is dropped (and so are all below it);
//   - a check of every level (valid indices, no zero-area triangles); a level
//     that fails is replaced by the level above it;
//   - per level "leave this level below this screen size" values in the
//     meaning LODGroup::Level::screenSize already has.
// Nothing here is wired into the editor. FillLodGroup is the hook an editor
// button can call later. Pure CPU, no GPU.
// ---------------------------------------------------------------------------
#include "okay/Optimize/MeshSimplify.hpp"
#include <string>
#include <vector>

namespace okay { class LODGroup; }

namespace okay::optimize {

struct AutoLodOptions {
    /// Triangle fraction per level; the first is normally 1 (the original).
    std::vector<float> ratios = {1.0f, 0.5f, 0.25f, 0.125f};
    /// Simplifier settings and the camera model used to turn error into screen size.
    LodChainOptions chain;
    /// A level whose error is above this fraction of the object size is dropped.
    /// <= 0 means no limit.
    float maxErrorFraction = 0.05f;
    /// Skip levels that would keep fewer triangles than this.
    int minTriangles = 4;
};

struct AutoLodLevel {
    Mesh     mesh;
    SkinData skin;
    float    ratio = 1.0f;             ///< requested
    float    achievedRatio = 1.0f;     ///< triangles / original triangles
    int      triangles = 0;
    float    error = 0.0f;             ///< world units
    float    errorFraction = 0.0f;     ///< error / object size
    float    switchDistance = 0.0f;    ///< start using this level from this distance
    float    enterScreenSize = 1.0f;   ///< screen height fraction where this level starts
    float    leaveScreenSize = 0.0f;   ///< leave it below this (LODGroup::Level::screenSize); 0 for the last
};

struct AutoLodResult {
    std::vector<AutoLodLevel> levels;  ///< always at least one (the original) unless the mesh is empty
    float objectSize = 0.0f;
    int   skippedTriangles = 0;        ///< input triangles that were unusable and left out of LOD 1+
    int   droppedLevels = 0;           ///< levels removed by the error budget or the checks
    bool  simplified = false;          ///< levels.size() > 1
    std::string note;                  ///< why a chain is short or missing, empty when all is well
};

/// Build the chain. Deterministic. Never throws, never reads out of range.
AutoLodResult GenerateAutoLod(const Mesh& mesh, const AutoLodOptions& opt = AutoLodOptions(),
                              const SkinData* skin = nullptr);

/// True when every index is in range, every point is finite and no triangle has
/// zero area. `why` (optional) gets the first problem found.
bool MeshIsSound(const Mesh& m, std::string* why = nullptr);

/// Hook for the editor: fill a LODGroup from a chain (levels, screen sizes,
/// distances, lodMeshes). Level 0 stays the MeshRenderer's own mesh. Not called
/// anywhere yet. Respects LODGroup::kMaxLevels.
void FillLodGroup(okay::LODGroup& group, const AutoLodResult& lod);

} // namespace okay::optimize
