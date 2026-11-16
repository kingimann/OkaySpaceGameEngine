#pragma once
// Smooth vertex normals for meshes whose vertices are known to sit at DIFFERENT places (a height grid: Terrain, Water).
//
// Mesh::ComputeSmoothNormals first welds vertices that share a position, using a std::map keyed by the rounded position, so
// the normal is averaged across seams. For a grid nothing shares a position, so the map does nothing but cost: a node
// allocation and a tree walk per vertex, which was more than half of Terrain::Apply (23 ms of 37 ms at 256 x 256) and of
// Water's per-frame rebuild. This does the same sums in the same order without the map: the numbers are bit for bit the
// ones ComputeSmoothNormals gives for such a mesh (tests/test_terrain_water_speed.cpp checks that on random grids).
// Written for this engine; the algorithm (add each face's un-normalised cross product to its three vertices, then
// normalise) is the one Mesh::ComputeSmoothNormals already used.
#include "okay/Render/Mesh.hpp"

namespace okay {
namespace meshnormals {

/// True when no two vertices would be welded by Mesh::ComputeSmoothNormals' rounding (1 / 2048 of a unit) because the
/// grid's cells are far bigger than that. A quick test for a regular grid: `cell` is its spacing.
inline bool GridCellsAreDistinct(float cell) { return cell > 0.01f; }

/// Same result as m.ComputeSmoothNormals() when every vertex has its own position. O(vertices + triangles).
inline void SmoothNoWeld(Mesh& m) {
    m.autoSmoothAngle = 0.0f;               // fully smooth supersedes auto-smooth, as ComputeSmoothNormals does
    m.normals.assign(m.vertices.size(), Vec3{0, 0, 0});
    for (std::size_t i = 0; i + 2 < m.triangles.size(); i += 3) {
        const int a = m.triangles[i], b = m.triangles[i + 1], c = m.triangles[i + 2];
        const Vec3 fn = Vec3::Cross(m.vertices[(std::size_t)b] - m.vertices[(std::size_t)a],
                                    m.vertices[(std::size_t)c] - m.vertices[(std::size_t)a]);
        m.normals[(std::size_t)a] += fn; m.normals[(std::size_t)b] += fn; m.normals[(std::size_t)c] += fn;
    }
    for (std::size_t i = 0; i < m.vertices.size(); ++i) {
        const Vec3 n = m.normals[i];
        const float mag = n.Magnitude();
        m.normals[i] = mag > 1e-8f ? n * (1.0f / mag) : Vec3{0, 1, 0};
    }
}

} // namespace meshnormals
} // namespace okay
