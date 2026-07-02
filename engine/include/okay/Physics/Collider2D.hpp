#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec2.hpp"
#include <vector>

namespace okay {

/// Base class for 2D colliders. Provides a world-space center and an AABB for
/// broad-phase tests; subclasses describe the actual shape.
class Collider2D : public Component {
public:
    enum class Shape { Box, Circle, Capsule, Edge, Polygon };

    /// Trigger colliders detect overlap but don't push objects apart.
    bool isTrigger = false;
    /// Offset of the collider from the Transform, in local units.
    Vec2 offset = Vec2::Zero;
    /// Collision layer [0, 31]. The Physics2D collision matrix decides which
    /// layers interact (all do by default).
    int layer = 0;
    /// Keep the collider matched to the object's SpriteRenderer bounds — refit
    /// each frame so it tracks size changes (see FitColliders / ColliderFit.hpp).
    bool autoFit = false;

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

/// Shared base for vertex-list colliders (edges and polygons). Vertices are in
/// local space, scaled and positioned by the Transform. The solver reduces these
/// to the closest point on their segment chain, so they collide with box, circle
/// and capsule colliders without any new shape-vs-shape math.
class PolyShapeCollider2D : public Collider2D {
public:
    std::vector<Vec2> points;          ///< local-space vertices

    /// Vertices transformed into world space.
    std::vector<Vec2> WorldPoints() const {
        Vec3 p = transform ? transform->Position() : Vec3::Zero;
        Vec3 s = transform ? transform->LossyScale() : Vec3::One;
        std::vector<Vec2> out; out.reserve(points.size());
        for (const Vec2& v : points)
            out.push_back({p.x + offset.x + v.x * s.x, p.y + offset.y + v.y * s.y});
        return out;
    }
    /// True for polygons (the last vertex connects back to the first).
    virtual bool closedLoop() const = 0;

    void WorldAABB(Vec2& outMin, Vec2& outMax) const override {
        std::vector<Vec2> wp = WorldPoints();
        if (wp.empty()) { outMin = outMax = WorldCenter(); return; }
        outMin = outMax = wp[0];
        for (const Vec2& v : wp) { outMin = Vec2::Min(outMin, v); outMax = Vec2::Max(outMax, v); }
    }
};

/// Edge collider: an open polyline (ground, slopes, curved terrain, walls). With
/// `oneWay` set it only blocks bodies approaching from the `oneWayNormal` side —
/// the classic one-way platform you can jump up through but land on top of.
class EdgeCollider2D : public PolyShapeCollider2D {
public:
    bool oneWay = false;
    Vec2 oneWayNormal = Vec2::Up;      ///< the side bodies are blocked from

    EdgeCollider2D() { points = {{-1.0f, 0.0f}, {1.0f, 0.0f}}; }
    Shape shape() const override { return Shape::Edge; }
    bool  closedLoop() const override { return false; }
};

/// Polygon collider: a closed loop of vertices (convex works best). Bodies bounce
/// off its boundary. Use it for ramps, platforms and arbitrary static shapes.
class PolygonCollider2D : public PolyShapeCollider2D {
public:
    PolygonCollider2D() { points = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.0f, 0.5f}}; }
    Shape shape() const override { return Shape::Polygon; }
    bool  closedLoop() const override { return true; }
};

} // namespace okay
