#pragma once
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

/// How a NavMeshSurface turns the scene's static colliders into walkable polygons.
struct NavMeshBuildSettings {
    float agentRadius = 0.4f;   ///< walkable area is shrunk by this much from every wall / ledge
    float agentHeight = 1.8f;   ///< head room a floor needs to count as walkable
    float maxSlope    = 45.0f;  ///< steepest walkable surface, in degrees
    float maxStep     = 0.4f;   ///< tallest step / ledge an agent walks over
    float cellSize    = 0.25f;  ///< XZ resolution of the bake (smaller = more detail, slower)
    float cellHeight  = 0.2f;   ///< height tolerance: how far a polygon may bend before it's split
};

/// A baked navigation mesh: convex polygons (axis-aligned rectangles merged from the
/// walkable cells of a heightfield, so a mesh can have floors above floors), the
/// portals between neighbouring polygons, and the queries NPCs need — A* over the
/// polygon graph, string pulling (funnel) for a smooth path, nearest point, a
/// raycast along the mesh, a random point and the ground height at a point.
/// Queries never allocate after the first call (scratch buffers are kept), so a
/// dozen agents can plan every frame. Queries are for one thread at a time.
class NavMesh {
public:
    struct Poly {
        int   v[4] = {0, 0, 0, 0};   ///< corners: (minX,minZ) (maxX,minZ) (maxX,maxZ) (minX,maxZ)
        int   firstLink = 0;         ///< into `links`: this polygon's portals
        int   linkCount = 0;
        float minX = 0, maxX = 0, minZ = 0, maxZ = 0;
        float minY = 0, maxY = 0;
        Vec3  center;
    };
    struct Link {
        int  from = 0, to = 0;   ///< polygon indices
        Vec3 a, b;               ///< the portal: the part of the shared edge both polygons own
    };

    NavMeshBuildSettings settings;
    std::vector<Vec3> vertices;
    std::vector<Poly> polys;
    std::vector<Link> links;     ///< grouped per polygon (Poly::firstLink / linkCount)
    Vec3 boundsMin, boundsMax;

    bool Empty() const { return polys.empty(); }
    void Clear();
    /// Call after filling `vertices`, `polys` (corner indices) and `links`: computes
    /// each polygon's bounds / centre, groups the links per polygon and builds the
    /// lookup grid. Deserialize and the baker call it.
    void Finalize();

    // ---- Queries ---------------------------------------------------------
    /// The polygon closest to `p` within +-extents, or -1. `nearest` receives the
    /// closest point on it. Prefers the polygon closest in height, so a point on a
    /// bridge finds the bridge, not the floor underneath.
    int  FindNearestPoly(const Vec3& p, const Vec3& extents, Vec3* nearest = nullptr) const;
    /// The closest point on the mesh within `maxDistance` (horizontally; twice that vertically).
    bool SamplePosition(const Vec3& p, float maxDistance, Vec3& out, int* poly = nullptr) const;
    /// Does polygon `poly` contain the XZ point?
    bool Contains(int poly, float x, float z) const;
    /// Ground height on polygon `poly` at an XZ point (bilinear across its corners).
    float HeightAt(int poly, float x, float z) const;
    /// Ground height under `p` (the polygon nearest in height), or `fallback`.
    float SampleHeight(const Vec3& p, float fallback, float maxDistance = 2.0f) const;

    /// A path from `from` to `to` (both snapped to the mesh): `out` gets the
    /// corners, start first, destination last. With `smooth`, the funnel algorithm
    /// pulls the string tight across the portals; without it the path visits every
    /// polygon centre on the way (debugging, comparisons). False when either point
    /// is off the mesh or the destination is on another island.
    bool FindPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out, bool smooth = true) const;
    /// A* over the polygon graph: the polygon indices from `start` to `end`.
    bool FindPolyPath(int start, const Vec3& from, int end, const Vec3& to, std::vector<int>& out) const;
    /// Walk the mesh in a straight line from `from` toward `to`. True when a mesh
    /// edge is in the way (`hit` = where), false when the line stays on the mesh.
    bool Raycast(const Vec3& from, const Vec3& to, Vec3& hit, int* lastPoly = nullptr) const;
    /// A uniformly distributed random point on the mesh (area weighted). `seed` is
    /// advanced (a small LCG), so results are reproducible.
    bool RandomPoint(std::uint32_t& seed, Vec3& out) const;
    /// A random point on the mesh within `radius` of `center` (tries a few draws).
    bool RandomPointNear(const Vec3& center, float radius, std::uint32_t& seed, Vec3& out) const;
    /// Walkable area in square units.
    float Area() const;

    // ---- .okaynav files (text: "okaynav 1", settings, bounds, v / p / l records) ----
    std::string Serialize() const;
    bool Deserialize(const std::string& text, std::string* error = nullptr);
    bool SaveToFile(const std::string& path) const;
    bool LoadFromFile(const std::string& path, std::string* error = nullptr);

private:
    // Lookup grid over the XZ bounds: polygons overlapping each cell (CSR layout).
    float m_gridCell = 2.0f;
    int   m_gridW = 0, m_gridH = 0;
    std::vector<int> m_gridStart, m_gridItems;
    std::vector<float> m_cumArea;   // for RandomPoint
    void BuildGrid();
    void PolysNear(float minX, float minZ, float maxX, float maxZ, std::vector<int>& out) const;
    void PortalLeftRight(const Link& l, const Vec3& dir, Vec3& left, Vec3& right) const;

    // Per-query scratch, reused between calls (no allocation once warm).
    struct Scratch {
        std::vector<float> g;
        std::vector<int>   parent, parentLink;
        std::vector<std::uint32_t> epoch;
        std::vector<std::uint8_t>  closed;
        std::vector<std::pair<float, int>> heap;
        std::vector<int>  polyPath, pathLinks, near;
        std::vector<Vec3> left, right;
        std::vector<std::uint32_t> mark;   // PolysNear de-duplication
        std::uint32_t stamp = 0, markStamp = 0;
    };
    mutable Scratch m_s;
};

} // namespace okay
