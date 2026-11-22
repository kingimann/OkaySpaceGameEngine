#pragma once
// ---------------------------------------------------------------------------
// Convex shapes of our own physics core (okay::phys).
//
// Every shape is described in its own local frame (centred on the origin, long axis along +Y) as a "core" plus a
// radius: a sphere is a point core with a radius, a capsule a segment core with a radius, and a box, convex hull,
// cylinder, cone or triangle a solid core with radius 0. Collision code works on the core and adds the radius
// (C. Ericson, "Real-Time Collision Detection", 2005, ch. 4.5 and 9.5; G. van den Bergen, "Collision Detection in
// Interactive 3D Environments", 2003).
//
//  * Support(d): the core point farthest along d (support mappings for box, cylinder and cone: G. van den Bergen, "A
//    fast and robust GJK implementation for collision detection of convex objects", J. Graphics Tools 4(2), 1999).
//  * SupportFeature(d): the face, edge or vertex of the core that faces d (used to build contact manifolds by clipping,
//    Ericson 8.3.4 / Sutherland-Hodgman polygon clipping).
//  * Mass properties from the closed-form solids (textbook formulas, re-derived and checked in tests/test_physcore_shapes)
//    and, for hulls, the covariance method on tetrahedra: J. Blow and A. Binstock, "How to find the inertia tensor (or
//    other mass properties) of a 3D solid body represented by a triangle mesh" (2004).
//  * Convex hulls by the incremental algorithm: M. de Berg et al., "Computational Geometry: Algorithms and
//    Applications", ch. 11 (without the conflict graph; the hull is built once when the shape is made).
// No other engine's code was read or used.
// ---------------------------------------------------------------------------
#include "okay/Physics/Core/PMath.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace okay {
namespace phys {

enum class ShapeType : std::uint8_t { Sphere, Box, Capsule, Cylinder, Cone, Hull, Triangle, Plane, Mesh };

struct Tri3 { V3 a, b, c; };

/// Static triangle geometry (a triangle mesh, a height field, a terrain): anything that can list the triangles near a
/// box. A Mesh shape holds one; it collides as the union of its triangles and is for static bodies only.
class TriangleSource {
public:
    virtual ~TriangleSource() = default;
    /// Append the triangles (shape frame) that may overlap `box` (shape frame).
    virtual void Collect(const Aabb& box, std::vector<Tri3>& out) const = 0;
    /// Bounds of all triangles (shape frame).
    virtual Aabb Bounds() const = 0;
};

/// A triangle list with its own bounding volume tree (a DynamicTree built once).
class TriMeshSource : public TriangleSource {
public:
    explicit TriMeshSource(std::vector<Tri3> tris);
    ~TriMeshSource() override;
    void Collect(const Aabb& box, std::vector<Tri3>& out) const override;
    Aabb Bounds() const override { return m_bounds; }
    std::size_t Size() const { return m_tris.size(); }
private:
    std::vector<Tri3> m_tris;
    Aabb m_bounds;
    struct Tree;
    std::unique_ptr<Tree> m_tree;
};

/// A convex hull built from points: its vertices and triangles (outward, counter-clockwise seen from outside).
struct HullData {
    std::vector<V3> verts;
    struct Tri { std::uint16_t a, b, c; V3 n; float d; };   // plane: dot(n, x) == d
    std::vector<Tri> tris;
    V3 centroid;                                              // of the solid
    float volume = 0.0f;
    /// Build from points. Fewer than 4 points, or all on one plane / line, is grown into a thin solid so it still
    /// collides. Points are capped at kMaxInput (extra ones are ignored after the first kMaxInput).
    static std::shared_ptr<HullData> Build(const std::vector<V3>& points);
    static constexpr std::size_t kMaxInput = 4096;
};

struct MassProps {
    float mass = 0.0f;
    V3 com;            // centre of mass in the shape frame
    M33 inertia;       // about the centre of mass, in the shape frame
};

class Shape {
public:
    ShapeType type = ShapeType::Sphere;
    V3 halfExtents{0.5f, 0.5f, 0.5f};   // Box
    float radius = 0.5f;                // Sphere, Capsule (round radius), Cylinder and Cone (base radius)
    float halfHeight = 0.5f;            // Capsule (segment half length), Cylinder, Cone (half the height; base at -h, tip at +h)
    std::shared_ptr<const HullData> hull;   // Hull
    V3 tri[3];                          // Triangle (used for mesh and terrain pieces)
    V3 planeNormal{0, 1, 0};            // Plane: the solid side is dot(n, x) <= planeOffset (infinite; static only)
    float planeOffset = 0.0f;
    std::shared_ptr<const TriangleSource> mesh;   // Mesh (static only)

    static Shape Sphere(float r) { Shape s; s.type = ShapeType::Sphere; s.radius = r; return s; }
    static Shape Box(const V3& h) { Shape s; s.type = ShapeType::Box; s.halfExtents = h; s.radius = 0.0f; return s; }
    static Shape Capsule(float r, float halfLen) { Shape s; s.type = ShapeType::Capsule; s.radius = r; s.halfHeight = halfLen; return s; }
    static Shape Cylinder(float r, float halfH) { Shape s; s.type = ShapeType::Cylinder; s.radius = r; s.halfHeight = halfH; return s; }
    static Shape Cone(float r, float halfH) { Shape s; s.type = ShapeType::Cone; s.radius = r; s.halfHeight = halfH; return s; }
    static Shape Hull(const std::vector<V3>& pts) { Shape s; s.type = ShapeType::Hull; s.radius = 0.0f; s.hull = HullData::Build(pts); return s; }
    static Shape Triangle(const V3& a, const V3& b, const V3& c) { Shape s; s.type = ShapeType::Triangle; s.radius = 0.0f; s.tri[0] = a; s.tri[1] = b; s.tri[2] = c; return s; }
    static Shape Plane(const V3& n, float offset) { Shape s; s.type = ShapeType::Plane; s.radius = 0.0f; s.planeNormal = Normalize(n); s.planeOffset = offset; return s; }
    static Shape Mesh(std::shared_ptr<const TriangleSource> src) { Shape s; s.type = ShapeType::Mesh; s.radius = 0.0f; s.mesh = std::move(src); return s; }

    /// The radius added around the core (sphere and capsule; 0 for the solid shapes).
    float CoreRadius() const { return (type == ShapeType::Sphere || type == ShapeType::Capsule) ? radius : 0.0f; }
    /// Core point farthest along d (d need not be unit length).
    V3 Support(const V3& d) const;
    /// The core feature facing d: 1 point (vertex), 2 (edge) or a convex polygon (face, counter-clockwise seen from d).
    /// `normal` gets the feature's outward normal when it is a face (else d normalised). Up to 16 points.
    int SupportFeature(const V3& d, V3* out, V3& normal) const;
    /// World box of the whole shape (radius included) at transform x.
    Aabb Bounds(const Xf& x) const;
    /// Mass properties at the given density (mass per unit volume). Planes and triangles have none (static only).
    MassProps Mass(float density) const;
    /// The largest distance from the origin to any point of the shape (for motion bounds and continuous collision).
    float OuterRadius() const;
    /// The smallest half-size (for "is this body moving fast for its size", continuous collision).
    float InnerRadius() const;
    bool IsConvexSolid() const { return type != ShapeType::Plane && type != ShapeType::Mesh; }
};

/// Put `pts` in counter-clockwise order as seen from the side `n` points to (around their average).
void SortPolygon(V3* pts, int count, const V3& n);

} // namespace phys
} // namespace okay
