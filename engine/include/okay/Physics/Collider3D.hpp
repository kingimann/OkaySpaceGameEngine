#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"

namespace okay {

/// Base class for 3D colliders. Provides a world-space center and an AABB for
/// broad-phase tests; subclasses describe the actual shape (mirrors Unity's
/// 3D collider family). Pair with a Rigidbody3D to be moved by Physics3D.
class Collider3D : public Component {
public:
    enum class Shape { Box, Sphere, Capsule };

    /// Trigger colliders detect overlap but don't push objects apart.
    bool isTrigger = false;
    /// Offset of the collider from the Transform, in local units.
    Vec3 offset = Vec3::Zero;
    /// Collision layer [0, 31]; the Physics3D matrix decides which interact.
    int layer = 0;

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
    void WorldAABB(Vec3& mn, Vec3& mx) const override {
        Vec3 c = WorldCenter(), h = HalfExtents();
        mn = c - h; mx = c + h;
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
    float SegmentHalf() const {
        Vec3 s = transform ? transform->LossyScale() : Vec3::One;
        float along = axis == 0 ? Mathf::Abs(s.x) : axis == 2 ? Mathf::Abs(s.z) : Mathf::Abs(s.y);
        float half = Mathf::Max(0.0f, (height * along) * 0.5f - WorldRadius());
        return half;
    }
    /// The capsule's inner segment endpoints in world space.
    void Segment(Vec3& a, Vec3& b) const {
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

} // namespace okay
