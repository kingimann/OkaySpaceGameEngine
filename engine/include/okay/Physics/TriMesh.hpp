#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Render/Mesh.hpp"
#include <cstdint>
#include <vector>

namespace okay {

/// A static triangle soup with a bounding-volume hierarchy (BVH), the geometry
/// behind MeshCollider3D. Built once from a Mesh in the mesh's own (local) space;
/// world transforms are applied at query time, so moving, rotating or scaling the
/// object (even non-uniformly) never needs a rebuild. Degenerate triangles (zero
/// area, NaN, out-of-range indices) are dropped at build time.
///
/// Build is O(n log n) (median split on the longest centroid axis); a ray or a
/// small-shape query visits O(log n + hits) nodes, so a 100k-triangle mesh
/// answers in microseconds.
class TriMesh {
public:
    /// Rebuild from `mesh`. An empty / fully degenerate mesh leaves Empty() true.
    void Build(const Mesh& mesh);
    void Clear();

    bool Empty() const { return m_tri.empty(); }
    int  TriangleCount() const { return static_cast<int>(m_tri.size() / 3); }
    int  NodeCount() const { return static_cast<int>(m_nodes.size()); }
    /// Triangles dropped as degenerate by the last Build.
    int  DroppedTriangles() const { return m_dropped; }
    /// Local-space bounds of the kept triangles.
    const Vec3& BoundsMin() const { return m_bmin; }
    const Vec3& BoundsMax() const { return m_bmax; }
    /// Triangle i's local-space corners.
    void Triangle(int i, Vec3& a, Vec3& b, Vec3& c) const {
        a = m_tri[3 * i]; b = m_tri[3 * i + 1]; c = m_tri[3 * i + 2];
    }

    /// Nearest ray hit in LOCAL space. `d` need not be unit length; the returned t is
    /// in units of d. Triangles are two-sided. Returns the hit triangle in `tri`.
    bool RayLocal(const Vec3& o, const Vec3& d, float maxT, float& t, int& tri) const;

    /// Call fn(triangleIndex) for every triangle whose local bounds overlap
    /// [mn, mx] (local space). A conservative broad phase for shape queries.
    template <class Fn>
    void QueryAABB(const Vec3& mn, const Vec3& mx, Fn&& fn) const {
        if (m_nodes.empty()) return;
        int stack[64]; int sp = 0; stack[sp++] = 0;
        while (sp > 0) {
            const Node& n = m_nodes[stack[--sp]];
            if (n.mx.x < mn.x || n.mn.x > mx.x || n.mx.y < mn.y || n.mn.y > mx.y ||
                n.mx.z < mn.z || n.mn.z > mx.z) continue;
            if (n.count > 0) { for (int i = 0; i < n.count; ++i) fn(n.first + i); }
            else if (sp + 2 <= 64) { stack[sp++] = n.first; stack[sp++] = n.first + 1; }
        }
    }

private:
    struct Node {
        Vec3 mn, mx;
        int first = 0;    // leaf: first triangle; inner: index of the left child (right = +1)
        int count = 0;    // > 0 for a leaf
    };
    std::vector<Vec3> m_tri;       // 3 corners per triangle, in BVH leaf order
    std::vector<Node> m_nodes;
    Vec3 m_bmin, m_bmax;
    int  m_dropped = 0;
    int BuildRange(std::vector<int>& idx, std::vector<Vec3>& cen, int lo, int hi, int depth);
};

/// Geometry helpers shared by the collider code.
namespace trimesh {
/// Closest point on triangle (a,b,c) to p (Ericson, Real-Time Collision Detection).
Vec3 ClosestPointOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c);

/// Result of a shape-vs-mesh contact: `normal` points from the mesh surface toward
/// the shape, `penetration` is how far to push the shape along it to separate.
struct Contact {
    bool  hit = false;
    Vec3  normal{0, 1, 0};
    Vec3  point{0, 0, 0};      // point on the mesh surface
    float penetration = 0.0f;
};

/// A TriMesh placed in the world by `l2w` (local -> world). All queries are in
/// world space and handle rotation and non-uniform scale exactly: candidate
/// triangles are found in the BVH using the query's bounds mapped to local space,
/// then tested in world space.
class WorldView {
public:
    WorldView(const TriMesh& mesh, const Mat4& l2w);
    /// False when the transform is singular (a zero scale): nothing to hit.
    bool Valid() const { return m_valid && !m_mesh->Empty(); }
    /// World-space AABB of the placed mesh.
    void WorldBounds(Vec3& mn, Vec3& mx) const;

    /// Nearest two-sided ray hit. `dir` must be unit length; t is world distance.
    bool Raycast(const Vec3& origin, const Vec3& dir, float maxT, float& t, Vec3& normal) const;
    /// Deepest contact of a sphere / capsule (segment a-b, radius r) / oriented box.
    Contact Sphere(const Vec3& c, float r) const;
    Contact Capsule(const Vec3& a, const Vec3& b, float r) const;
    Contact Box(const Vec3& c, const Vec3& half, const Vec3& ax, const Vec3& ay, const Vec3& az) const;

private:
    const TriMesh* m_mesh;
    Mat4 m_l2w, m_w2l;
    bool m_valid = true;
    void LocalBounds(const Vec3& wmn, const Vec3& wmx, Vec3& lmn, Vec3& lmx) const;
    void WorldTri(int i, Vec3& a, Vec3& b, Vec3& c) const;
};
} // namespace trimesh

} // namespace okay
