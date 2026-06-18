#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec2.hpp"

namespace okay {

/// Base class for 2D colliders. Provides a world-space center and an AABB for
/// broad-phase tests; subclasses describe the actual shape.
class Collider2D : public Component {
public:
    enum class Shape { Box, Circle };

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

} // namespace okay
