#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec2.hpp"

namespace okay {

/// Base class for 2D colliders. Provides a world-space center and an AABB for
/// broad-phase tests; subclasses describe the actual shape.
class Collider2D : public Component {
public:
    enum class Shape { Box, Circle, Capsule };

    /// Trigger colliders detect overlap but don't push objects apart.
    bool isTrigger = false;
    /// Offset of the collider from the Transform, in local units.
    Vec2 offset = Vec2::Zero;
    /// Collision layer [0, 31]. The Physics2D collision matrix decides which
    /// layers interact (all do by default).
    int layer = 0;

    virtual Shape shape() const = 0;

    /// World-space center of the collider.
    Vec2 WorldCenter() const {
        Vec3 p = transform ? transform->Position() : Vec3::Zero;
        return {p.x + offset.x, p.y + offset.y};
    }
    /// World-space axis-aligned bounds (broad phase).
    virtual void WorldAABB(Vec2& outMin, Vec2& outMax) const = 0;
};

/// Axis-aligned box collider sized in world units (scaled by the Transform).
class BoxCollider2D : public Collider2D {
public:
    Vec2 size = Vec2::One;

    Shape shape() const override { return Shape::Box; }

    Vec2 HalfExtents() const {
        Vec3 s = transform ? transform->LossyScale() : Vec3::One;
        return {size.x * s.x * 0.5f, size.y * s.y * 0.5f};
    }
    void WorldAABB(Vec2& outMin, Vec2& outMax) const override {
        Vec2 c = WorldCenter(), h = HalfExtents();
        outMin = {c.x - h.x, c.y - h.y};
        outMax = {c.x + h.x, c.y + h.y};
    }
};

/// Circle collider with a radius in world units (scaled by the Transform).
class CircleCollider2D : public Collider2D {
public:
    float radius = 0.5f;

    Shape shape() const override { return Shape::Circle; }

    float WorldRadius() const {
        Vec3 s = transform ? transform->LossyScale() : Vec3::One;
        return radius * Mathf::Max(Mathf::Abs(s.x), Mathf::Abs(s.y));
    }
    void WorldAABB(Vec2& outMin, Vec2& outMax) const override {
        Vec2 c = WorldCenter();
        float r = WorldRadius();
        outMin = {c.x - r, c.y - r};
        outMax = {c.x + r, c.y + r};
    }
};

/// Capsule collider: a segment with a radius (rounded ends). `size` is the full
/// width and height of the capsule's bounding box; the longer of the two picks
/// the segment axis (vertical when taller, horizontal when wider).
class CapsuleCollider2D : public Collider2D {
public:
    Vec2 size = {1.0f, 2.0f};
    enum class Direction { Vertical, Horizontal };
    Direction direction = Direction::Vertical;

    Shape shape() const override { return Shape::Capsule; }

    float WorldRadius() const {
        Vec3 s = transform ? transform->LossyScale() : Vec3::One;
        // Radius is half the capsule's thin dimension.
        return direction == Direction::Vertical
            ? Mathf::Abs(size.x * s.x) * 0.5f
            : Mathf::Abs(size.y * s.y) * 0.5f;
    }
    /// The capsule's inner segment endpoints in world space.
    void Segment(Vec2& a, Vec2& b) const {
        Vec3 s = transform ? transform->LossyScale() : Vec3::One;
        Vec2 c = WorldCenter();
        float r = WorldRadius();
        if (direction == Direction::Vertical) {
            float half = Mathf::Max(0.0f, Mathf::Abs(size.y * s.y) * 0.5f - r);
            a = {c.x, c.y - half}; b = {c.x, c.y + half};
        } else {
            float half = Mathf::Max(0.0f, Mathf::Abs(size.x * s.x) * 0.5f - r);
            a = {c.x - half, c.y}; b = {c.x + half, c.y};
        }
    }
    void WorldAABB(Vec2& outMin, Vec2& outMax) const override {
        Vec2 a, b; Segment(a, b);
        float r = WorldRadius();
        outMin = {Mathf::Min(a.x, b.x) - r, Mathf::Min(a.y, b.y) - r};
        outMax = {Mathf::Max(a.x, b.x) + r, Mathf::Max(a.y, b.y) + r};
    }
};

} // namespace okay
