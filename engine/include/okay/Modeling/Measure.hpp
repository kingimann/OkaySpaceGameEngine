#pragma once
// Measuring and snapping for the modeling window, written from the plain geometry
// (distances, triangle areas, closest point on a segment or triangle).
//
//   MeasureMesh / MeasureSelection  sizes, edge lengths, distance between two vertices, face area
//   SnapToGeometry                  the nearest vertex, edge point or face point to a position
//
// Everything is in the mesh's own space; pass `scale` to read sizes in world units.
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Mesh.hpp"
#include <string>
#include <vector>

namespace okay::modeling {

struct Measurement {
    int   verts = 0, edges = 0, faces = 0;   ///< how many were measured
    Vec3  size{0, 0, 0};                     ///< bounding box size (x, y, z) of what was measured
    float diagonal = 0.0f;                   ///< length of that box's diagonal
    bool  hasDistance = false;               ///< exactly two vertices selected
    float distance = 0.0f;                   ///< ... and the distance between them
    Vec3  delta{0, 0, 0};                    ///< ... and the x, y, z difference (second minus first)
    float edgeLength = 0.0f;                 ///< total length of the selected edges
    float area = 0.0f;                       ///< total area of the selected faces
    float volume = 0.0f;                     ///< signed volume of the whole mesh (MeasureMesh only; > 0 = faces out)
    std::string Summary() const;             ///< one short readout line
};

/// The whole mesh: bounding box, area, volume.
Measurement MeasureMesh(const Mesh& m, const Vec3& scale = Vec3{1, 1, 1});
/// What is selected: `verts` are vertex numbers, `tris` triangle numbers, `edges` vertex pairs.
/// With nothing selected the result is empty (all zero). Out-of-range numbers are ignored.
Measurement MeasureSelection(const Mesh& m, const std::vector<int>& verts, const std::vector<int>& tris,
                             const std::vector<std::pair<int, int>>& edges, const Vec3& scale = Vec3{1, 1, 1});

enum class SnapTarget { Grid = 0, Vertex = 1, Edge = 2, Face = 3 };
const char* SnapTargetName(SnapTarget t);

struct SnapHit {
    bool found = false;
    Vec3 point{0, 0, 0};
    float distance = 0.0f;
    int  a = -1, b = -1;       ///< the vertex (a), edge (a, b) or triangle (a) it snapped to
};
/// The nearest vertex, point on an edge, or point on a face of `m` within `radius` of `p`.
/// `skipVerts` (vertex numbers) are left out, with everything that uses them: the vertices
/// being moved must not snap to themselves. Grid is not geometry and finds nothing here.
SnapHit SnapToGeometry(const Mesh& m, const Vec3& p, SnapTarget target, float radius, const std::vector<int>& skipVerts = {});

/// The closest point to `p` on the segment a-b / on the triangle abc (exposed for tests).
Vec3 ClosestOnSegment(const Vec3& p, const Vec3& a, const Vec3& b);
Vec3 ClosestOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c);

} // namespace okay::modeling
