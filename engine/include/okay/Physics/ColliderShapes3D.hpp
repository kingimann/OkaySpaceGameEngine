#pragma once
// More 3D collider shapes, each a real solid (never its bounding box):
//   Cone, Convex Hull (from points / a mesh), Compound (several shapes on one object), Plane (an endless half-space)
//   and Heightfield (a grid of heights). The Cylinder collider (Collider3D.hpp) uses the same shape kernel.
// Written from general convex-geometry definitions (support functions, solid inertia, quadric ray tests).
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/ShapeKernel3D.hpp"
#include <array>
#include <string>
#include <vector>

namespace okay {

class Terrain;
struct Mesh;

namespace shape3d {
/// The world frame of a collider: centre, rotation and (absolute) scale. A local point maps to C + R * (S * p).
struct Frame { Vec3 c; Quat r; Vec3 s; };
Frame FrameOf(const Collider3D& c);
Vec3 ToLocalPoint(const Frame& f, const Vec3& w);
Vec3 ToLocalDir(const Frame& f, const Vec3& w);
Vec3 ToWorldPoint(const Frame& f, const Vec3& l);
Vec3 ToWorldNormal(const Frame& f, const Vec3& nLocal);     // inverse-transpose of the scale, then rotate
/// World support of a local convex shape sitting in `f`.
Vec3 WorldSupport(const Frame& f, const LocalShape3D& s, const Vec3& dirWorld);
void WorldBoundsOfShape(const Frame& f, const LocalShape3D& s, Vec3& mn, Vec3& mx);
bool WorldRay(const Frame& f, const LocalShape3D& s, const Vec3& o, const Vec3& d, float maxT, float& t, Vec3& n);
bool WorldMass(const Frame& f, const LocalShape3D& s, Vec3& com, float& vol, Vec3& unitInertia);
/// The shape a Cylinder collider describes (its radius, height and axis).
LocalShape3D CylinderShape(const CylinderCollider3D& c);
}  // namespace shape3d

/// Base for the colliders that are one convex solid described by a LocalShape3D.
class ShapeCollider3D : public Collider3D {
public:
    /// The unscaled local shape (the Transform's scale is applied on top).
    virtual LocalShape3D Local() const = 0;
    bool HasConvexSupport() const override { return true; }
    Vec3 ConvexSupport(const Vec3& dirWorld) const override;
    bool MassShape(Vec3& comWorld, float& volume, Vec3& unitInertia) const override;
    bool RayShape(const Vec3& origin, const Vec3& dir, float maxT, float& t, Vec3& n) const override;
    void WorldAABB(Vec3& mn, Vec3& mx) const override;
};

/// Cone collider: a round base and a point. `height` is base to tip along `axis`; the tip points to +axis.
class ConeCollider3D : public ShapeCollider3D {
public:
    float radius = 0.5f;
    float height = 1.0f;
    int   axis   = 1;   // 0=X, 1=Y, 2=Z
    Shape shape() const override { return Shape::Cone; }
    LocalShape3D Local() const override;
};

/// Convex Hull collider: the smallest solid that wraps a cloud of points (like shrink-wrap). `points` holds them as text
/// ("x y z; x y z; ..."), in the object's local units. Use SetFromMesh to wrap a model, or type your own points.
class ConvexHullCollider3D : public ShapeCollider3D {
public:
    std::string points;     ///< "x y z; x y z; ..." (blank = a unit box, so a new one is never empty)
    /// The kept points (as parsed from `points`); the hull is built from them.
    const std::vector<Vec3>& Points() const;
    void SetPoints(const std::vector<Vec3>& p, int maxPoints = 256);
    /// Wrap the hull around a mesh (its vertices). Returns false when the mesh has no volume.
    bool SetFromMesh(const Mesh& m, int maxPoints = 256);
    int PointCount() const { return (int)Points().size(); }
    Shape shape() const override { return Shape::ConvexHull; }
    LocalShape3D Local() const override;
private:
    mutable std::string m_key;
    mutable std::vector<Vec3> m_pts;
    mutable LocalShape3D m_shape;
    void Refresh() const;
};

/// Plane collider: an endless half-space. Everything below the plane (against the object's up direction, local +Y) is
/// solid. Use it for a ground that never ends.
class PlaneCollider3D : public Collider3D {
public:
    Shape shape() const override { return Shape::Plane; }
    /// Upward normal in world space.
    Vec3 WorldNormal() const { return transform ? transform->Rotation() * Vec3{0, 1, 0} : Vec3{0, 1, 0}; }
    void WorldAABB(Vec3& mn, Vec3& mx) const override;
    bool RayShape(const Vec3& origin, const Vec3& dir, float maxT, float& t, Vec3& n) const override;
};

/// Compound collider: several simple shapes glued into one rigid collider (a table = a top box + four leg boxes, a
/// dumbbell = a bar and two balls). `parts` holds them as text, one per ";": `kind px py pz rx ry rz a b c axis`
///   box      size x y z (a b c)
///   sphere   radius (a)
///   capsule / cylinder / cone   radius (a) height (b) axis (0 X, 1 Y, 2 Z)
/// Position (p) is in local units, rotation (r) in degrees.
class CompoundCollider3D : public Collider3D {
public:
    struct Part { LocalShape3D shape; Vec3 pos; Vec3 euler; Quat rot = Quat::Identity; };
    std::string parts;      ///< the text above (blank = no shapes: it collides with nothing)
    const std::vector<Part>& Parts() const;
    int PartCount() const { return (int)Parts().size(); }
    /// Append one part (kept as text).
    void AddBox(const Vec3& pos, const Vec3& size, const Vec3& eulerDeg = Vec3::Zero);
    void AddSphere(const Vec3& pos, float radius);
    void AddCapsule(const Vec3& pos, float radius, float height, int axis = 1, const Vec3& eulerDeg = Vec3::Zero);
    void AddCylinder(const Vec3& pos, float radius, float height, int axis = 1, const Vec3& eulerDeg = Vec3::Zero);
    void AddCone(const Vec3& pos, float radius, float height, int axis = 1, const Vec3& eulerDeg = Vec3::Zero);
    /// World frame of part i (its own centre, rotation and scale folded in) and its support / bounds.
    shape3d::Frame PartFrame(int i) const;
    Vec3 PartSupport(int i, const Vec3& dirWorld) const;
    void PartBounds(int i, Vec3& mn, Vec3& mx) const;
    Shape shape() const override { return Shape::Compound; }
    void WorldAABB(Vec3& mn, Vec3& mx) const override;
    bool MassShape(Vec3& comWorld, float& volume, Vec3& unitInertia) const override;
    bool RayShape(const Vec3& origin, const Vec3& dir, float maxT, float& t, Vec3& n) const override;
private:
    mutable std::string m_key;
    mutable std::vector<Part> m_parts;
    void Refresh() const;
};

/// Heightfield collider: a grid of ground heights (a terrain you can drive on without a Terrain component). `heights`
/// holds columns x rows numbers (text, row after row); the grid is centred on the object and `cellX` / `cellZ` metres apart.
/// Solid below the surface. SetFromTerrain copies a Terrain's hills.
class HeightfieldCollider3D : public Collider3D {
public:
    int   columns = 2;      ///< samples along X (>= 2)
    int   rows    = 2;      ///< samples along Z (>= 2)
    float cellX = 1.0f, cellZ = 1.0f;
    float heightScale = 1.0f;
    std::string heights = "0 0 0 0";
    Shape shape() const override { return Shape::Heightfield; }
    // The settings as the collision uses them: grid 2..2048 per side, cells never zero, negative or not a number.
    int Cols() const { return columns < 2 ? 2 : columns > 2048 ? 2048 : columns; }
    int Rows() const { return rows < 2 ? 2 : rows > 2048 ? 2048 : rows; }
    float CX() const { return (cellX >= 1e-4f && cellX <= 1e6f) ? cellX : (cellX > 1e6f ? 1e6f : 1e-4f); }
    float CZ() const { return (cellZ >= 1e-4f && cellZ <= 1e6f) ? cellZ : (cellZ > 1e6f ? 1e6f : 1e-4f); }

    /// Number of numbers actually present (it may be fewer than columns * rows: missing ones read as 0).
    float HeightAt(int col, int row) const;
    void  SetHeights(int cols, int rws, const std::vector<float>& h);
    /// Copy a Terrain's heights and size. Returns false when the terrain has no heights.
    bool  SetFromTerrain(const Terrain& t);
    /// Local height of the surface at local (x, z) (bilinear on the same two triangles the collision uses); false outside the grid.
    bool  LocalHeight(float x, float z, float& y) const;
    /// The surface triangles whose cells touch the world box, in world space.
    void  CollectTriangles(const Vec3& mn, const Vec3& mx, std::vector<std::array<Vec3, 3>>& out) const;
    /// How far below the surface the solid reaches (so a body that sinks is pushed back up, not through).
    float Depth() const;
    void  WorldAABB(Vec3& mn, Vec3& mx) const override;
    bool  RayShape(const Vec3& origin, const Vec3& dir, float maxT, float& t, Vec3& n) const override;
private:
    mutable std::string m_key;
    mutable std::vector<float> m_h;
    mutable float m_lo = 0.0f, m_hi = 0.0f;
    void Refresh() const;
};

/// True for the collider types in this file and the Cylinder (the shapes the generic narrow phase handles).
bool IsGenericShape(const Collider3D* c);
/// Read / write the "x y z; x y z" text of a point list (used by the Convex Hull and the saved scene).
std::string PointsToText(const std::vector<Vec3>& p);
std::vector<Vec3> PointsFromText(const std::string& s);
/// The Inspector rows and saved-scene records of the new colliders (called with the other built-in registrations).
void RegisterColliderShapeProperties();
namespace detail { void RegisterColliderShapeSerializers(); }

} // namespace okay
