#pragma once
// UV unwrapping on a PolyMesh: flatten the surface into texture space so a
// texture lands on it without stretching.
//
//   Unwrap()          Least Squares Conformal Maps (Lévy et al. 2002): each
//                     island (faces joined by edges that aren't seams) is
//                     flattened keeping angles as true as possible, then all
//                     islands are scaled to the same texel density and packed
//                     into the 0..1 square. Only a disc lies flat: a closed
//                     part (a sphere) is first cut along the shortest path
//                     between its two farthest points, and a part with more
//                     than one border (a tube's two rims, a hole) gets its
//                     borders joined by shortest paths. Those cuts are marked
//                     as seams.
//   MarkSeamsByAngle  seams on every edge whose faces meet sharper than an
//                     angle (Smart Unwrap = this, then Unwrap).
//   PackUVIslands     re-pack existing UV islands (faces joined by edges with
//                     the same UVs on both sides) without re-flattening them.
//
// Faces are given by index; an empty list means every face. UVs are written
// per face corner (PolyFace::uvs), so seams split them.
#include "okay/Math/Vec2.hpp"
#include "okay/Modeling/PolyMesh.hpp"
#include <vector>

namespace okay::modeling {

struct UnwrapOptions {
    float margin = 0.02f;   ///< gap between packed islands, as a fraction of the UV square
    bool  pack = true;      ///< false: islands keep their solved place and scale
    bool  cutClosed = true; ///< cut parts that aren't discs open along shortest paths (marked as seams)
};

struct UnwrapReport {
    int islands = 0;        ///< islands flattened
    int openedClosed = 0;   ///< parts that weren't discs and were cut open
    int projected = 0;      ///< islands the solver couldn't flatten, given a planar projection instead
    int seamsAdded = 0;     ///< seam edges added by cutting closed parts
};

/// Faces joined across edges that are neither seams nor borders.
std::vector<std::vector<int>> SeamIslands(const PolyMesh& pm, const std::vector<int>& faces = {});
/// Faces joined across edges whose two sides have the same UVs.
std::vector<std::vector<int>> UVIslands(const PolyMesh& pm, const std::vector<int>& faces = {});

/// Flatten `faces` (all when empty) with LSCM. False when there was nothing to unwrap.
bool Unwrap(PolyMesh& pm, const std::vector<int>& faces = {}, const UnwrapOptions& opt = UnwrapOptions(),
            UnwrapReport* report = nullptr);

/// Mark a seam on every edge (between two of `faces`) whose faces meet at
/// `degrees` or sharper. Returns how many seams were added.
int MarkSeamsByAngle(PolyMesh& pm, float degrees, const std::vector<int>& faces = {});

/// Pack the UV islands of `faces` into the 0..1 square. `rescale` first gives
/// every island the same texel density (UV area in proportion to surface area).
void PackUVIslands(PolyMesh& pm, const std::vector<int>& faces = {}, float margin = 0.02f, bool rescale = false);

/// Angle distortion of the UVs of `faces`: the mean absolute difference, in
/// degrees, between each triangle corner's angle on the surface and in UV.
/// 0 = perfectly conformal.
float UVAngleDistortion(const PolyMesh& pm, const std::vector<int>& faces = {});

} // namespace okay::modeling
