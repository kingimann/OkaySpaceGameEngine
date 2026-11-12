#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Physics/TriMesh.hpp"
#include <cstdint>
#include <string>

namespace okay {

/// Base class for 3D colliders. Provides a world-space center and an AABB for
/// broad-phase tests; subclasses describe the actual shape (mirrors the usual
/// 3D collider family). Pair with a Rigidbody3D to be moved by Physics3D.
class Collider3D : public Component {
public:
    enum class Shape { Box, Sphere, Capsule, Cylinder, Mesh };

    /// Trigger colliders detect overlap but don't push objects apart.
    bool isTrigger = false;
    /// Offset of the collider from the Transform, in local units.
    Vec3 offset = Vec3::Zero;
    /// Collision layer [0, 31]; the Physics3D matrix decides which interact.
    int layer = 0;
    /// Keep the collider matched to the object's MeshRenderer bounds — refit each
    /// frame so it tracks size changes (see FitColliders / ColliderFit.hpp).
    bool autoFit = false;
    /// Trigger filter: a trigger only reacts to objects with this tag (blank = any
    /// tag), e.g. "Player" so pickups ignore enemies and bullets. Other objects
    /// pass through without any Enter/Stay/Exit events on either side.
    std::string triggerTag;
    /// Trigger filter: which collider layers set this trigger off (bit n = layer n).
    std::uint32_t triggerLayers = 0xFFFFFFFFu;

    virtual Shape shape() const = 0;

    /// World-space center of the collider.
    Vec3 WorldCenter() const {
        Vec3 p = transform ? transform->Position() : Vec3::Zero;
        return p + offset;
    }
    /// World-space axis-aligned bounds (broad phase).
    virtual void WorldAABB(Vec3& outMin, Vec3& outMax) const = 0;
};

/// Axis-aligned box collider sized in world units (scaled by the Transform).
class BoxCollider3D : public Collider3D {
public:
    Vec3 size = Vec3::One;

    Shape shape() const override { return Shape::Box; }

    Vec3 HalfExtents() const {
        Vec3 s = transform ? transform->LossyScale() : Vec3::One;
        return {Mathf::Abs(size.x * s.x) * 0.5f,
                Mathf::Abs(size.y * s.y) * 0.5f,
                Mathf::Abs(size.z * s.z) * 0.5f};
    }
    /// The box's three world-space unit axes (its local X/Y/Z after rotation).
    /// Drives oriented-box (SAT) collision so a rotated box collides by its true
    /// faces, not its axis-aligned bounds.
    void WorldAxes(Vec3& ax, Vec3& ay, Vec3& az) const {
        Quat q = transform ? transform->Rotation() : Quat::Identity;
        ax = q * Vec3{1, 0, 0}; ay = q * Vec3{0, 1, 0}; az = q * Vec3{0, 0, 1};
    }
    void WorldAABB(Vec3& mn, Vec3& mx) const override {
        Vec3 c = WorldCenter(), h = HalfExtents();
        Vec3 ax, ay, az; WorldAxes(ax, ay, az);
        // Expand to the rotated box's extent so a spun box isn't culled before the
        // narrow-phase SAT test (an unrotated box gives exactly h).
        Vec3 e{ Mathf::Abs(ax.x) * h.x + Mathf::Abs(ay.x) * h.y + Mathf::Abs(az.x) * h.z,
                Mathf::Abs(ax.y) * h.x + Mathf::Abs(ay.y) * h.y + Mathf::Abs(az.y) * h.z,
                Mathf::Abs(ax.z) * h.x + Mathf::Abs(ay.z) * h.y + Mathf::Abs(az.z) * h.z };
        mn = c - e; mx = c + e;
    }
};

/// Sphere collider with a radius in world units (scaled by the Transform).
class SphereCollider3D : public Collider3D {
public:
    float radius = 0.5f;

    Shape shape() const override { return Shape::Sphere; }

    float WorldRadius() const {
        Vec3 s = transform ? transform->LossyScale() : Vec3::One;
        return radius * Mathf::Max(Mathf::Abs(s.x),
                                   Mathf::Max(Mathf::Abs(s.y), Mathf::Abs(s.z)));
    }
    void WorldAABB(Vec3& mn, Vec3& mx) const override {
        Vec3 c = WorldCenter(); float r = WorldRadius();
        mn = {c.x - r, c.y - r, c.z - r};
        mx = {c.x + r, c.y + r, c.z + r};
    }
};

/// Capsule collider: a segment with a radius (two hemispherical caps). `height`
/// is the full tip-to-tip length; `axis` is the long axis (0=X, 1=Y, 2=Z).
class CapsuleCollider3D : public Collider3D {
public:
    float radius = 0.5f;
    float height = 2.0f;
    int   axis   = 1;   // 0=X, 1=Y, 2=Z

    Shape shape() const override { return Shape::Capsule; }

    float WorldRadius() const {
        Vec3 s = transform ? transform->LossyScale() : Vec3::One;
        // Radius scales by the two axes orthogonal to the capsule's long axis.
        float a = axis == 0 ? Mathf::Abs(s.y) : Mathf::Abs(s.x);
        float b = axis == 2 ? Mathf::Abs(s.y) : Mathf::Abs(s.z);
        return radius * Mathf::Max(a, b);
    }
    /// Half the distance between the two cap centers (segment half-length).
    virtual float SegmentHalf() const {
        Vec3 s = transform ? transform->LossyScale() : Vec3::One;
        float along = axis == 0 ? Mathf::Abs(s.x) : axis == 2 ? Mathf::Abs(s.z) : Mathf::Abs(s.y);
        float half = Mathf::Max(0.0f, (height * along) * 0.5f - WorldRadius());
        return half;
    }
    /// The capsule's inner segment endpoints in world space.
    virtual void Segment(Vec3& a, Vec3& b) const {
        Vec3 c = WorldCenter();
        Vec3 dir = axis == 0 ? Vec3{1, 0, 0} : axis == 2 ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
        float h = SegmentHalf();
        a = c - dir * h;
        b = c + dir * h;
    }
    void WorldAABB(Vec3& mn, Vec3& mx) const override {
        Vec3 a, b; Segment(a, b);
        float r = WorldRadius();
        mn = {Mathf::Min(a.x, b.x) - r, Mathf::Min(a.y, b.y) - r, Mathf::Min(a.z, b.z) - r};
        mx = {Mathf::Max(a.x, b.x) + r, Mathf::Max(a.y, b.y) + r, Mathf::Max(a.z, b.z) + r};
    }
};

/// Cylinder collider: a flat-capped tube. It reuses the capsule's segment + radius
/// for collision (so it behaves like a capsule with slightly rounded ends), which
/// is plenty for gameplay — barrels, pillars, tree trunks, coins on edge.
class CylinderCollider3D : public CapsuleCollider3D {
public:
    Shape shape() const override { return Shape::Cylinder; }
    /// A cylinder's straight section spans nearly the full height (no spherical
    /// caps eating into it), so use a longer inner segment than a capsule.
    float SegmentHalf() const {
        Vec3 s = transform ? transform->LossyScale() : Vec3::One;
        float along = axis == 0 ? Mathf::Abs(s.x) : axis == 2 ? Mathf::Abs(s.z) : Mathf::Abs(s.y);
        return Mathf::Max(0.0f, (height * along) * 0.5f - WorldRadius() * 0.05f);
    }
    void Segment(Vec3& a, Vec3& b) const {
        Vec3 c = WorldCenter();
        Vec3 dir = axis == 0 ? Vec3{1, 0, 0} : axis == 2 ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
        float h = SegmentHalf();
        a = c - dir * h; b = c + dir * h;
    }
};

/// Mesh collider: collides against the exact triangles of the object's MeshRenderer
/// mesh (the modifier result when there is one), placed by the object's world matrix
/// (rotation and non-uniform scale included). A BVH is built from the mesh and rebuilt
/// when the mesh changes, so raycasts, sphere / capsule / box overlaps and casts,
/// ResolveSphere and Rigidbody3D contact all work against arbitrary (concave, hollow,
/// holed) geometry.
///
/// STATIC ONLY: it is world geometry. Dynamic Rigidbody3D bodies (spheres, capsules,
/// boxes) collide with it, but a mesh collider cannot itself be a dynamic body: the
/// solver has no concave-vs-concave contact and no convex-hull shape. A mesh collider
/// on the same object as a non-kinematic Rigidbody3D is ignored by physics contact (a
/// warning is logged and the Inspector says so); use a Box / Sphere / Capsule collider
/// on moving things. Kinematic bodies may carry one (a moving platform). Queries
/// (raycast, overlap, casts) work either way.
///
/// With no MeshRenderer, or a mesh with no usable triangles, it falls back to the
/// `size` / `offset` bounding box exactly as before.
class MeshCollider3D : public BoxCollider3D {
public:
    Shape shape() const override { return Shape::Mesh; }

    /// True when the collider has real triangles (the BVH is refreshed first).
    bool HasTriangles() const;
    /// The built triangle mesh + BVH (local space); empty when !HasTriangles().
    const TriMesh& Tris() const;
    /// Local -> world matrix the triangles are placed with: the object's. (`offset` and `size`
    /// only describe the no-triangles fallback box; Fit sets them from the mesh bounds.)
    Mat4 WorldMatrix() const;
    /// Force a rebuild at the next query (the mesh data is also watched automatically:
    /// its size / buffers every query, its contents once per physics step or frame).
    void Invalidate() { m_valid = false; }
    /// Number of times the BVH was (re)built; handy to see that an edit was noticed.
    int BuildCount() const { return m_builds; }
    /// Triangles dropped as degenerate when the BVH was last built.
    int DroppedTriangles() const { return Tris().DroppedTriangles(); }
    /// Why the collider is not (fully) doing what its name says; empty when it is.
    /// Shown in the Inspector.
    std::string StatusMessage() const;
    /// True when a non-kinematic Rigidbody3D shares the object (unsupported, see above).
    bool IsDynamicConflict() const;
    /// Triangle world AABB, or the `size` box without triangles.
    void WorldAABB(Vec3& mn, Vec3& mx) const override;

    /// Call once per physics step / frame: lets every mesh collider re-check its mesh
    /// contents (a hash of the vertices and indices) on its next query.
    static void AdvanceEpoch();

    bool m_warnedDynamic = false;   // runtime: the dynamic-body warning was logged
private:
    void Refresh() const;
    mutable TriMesh m_tris;
    mutable bool m_valid = false;
    mutable std::uint64_t m_hash = 0, m_checkedEpoch = ~0ull;
    mutable const void *m_vp = nullptr, *m_ip = nullptr;
    mutable std::size_t m_vn = 0, m_in = 0;
    mutable int m_builds = 0;
};

} // namespace okay
