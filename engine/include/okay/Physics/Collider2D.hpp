#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Physics/PhysicsMaterial.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

/// Base class for 2D colliders. Provides a world-space center and an AABB for
/// broad-phase tests; subclasses describe the actual shape.
class Collider2D : public Component {
public:
    enum class Shape { Box, Circle, Capsule, Edge, Polygon, Composite };

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
    /// Trigger filter: a trigger only reacts to objects with this tag (blank = any
    /// tag), e.g. "Player" so pickups ignore enemies and bullets. Other objects
    /// pass through without any Enter/Stay/Exit events on either side.
    std::string triggerTag;
    /// Trigger filter: which collider layers set this trigger off (bit n = layer n).
    std::uint32_t triggerLayers = 0xFFFFFFFFu;

    // ---- Physics material (per collider) ----
    /// This collider's friction coefficient. -1 (the default) means "use the Rigidbody2D's friction", exactly as before.
    float friction = -1.0f;
    /// This collider's bounciness [0,1]. -1 means "use the Rigidbody2D's bounciness".
    float bounciness = -1.0f;
    /// How this collider's friction is combined with the one it touches (default: geometric mean).
    CombineMode frictionCombine = CombineMode::Default;
    /// How this collider's bounciness is combined with the one it touches (default: the larger).
    CombineMode bounceCombine = CombineMode::Default;
    /// Contact offset: the collider counts as this much thicker when it touches something (a skin, in units). 0 (the
    /// default) is the exact shape.
    float contactOffset = 0.0f;

    virtual Shape shape() const = 0;

    /// The object's turn about Z in radians (2D colliders turn with their object).
    float WorldRotation() const {
        Quat q = transform ? transform->Rotation() : Quat::Identity;
        return q.ToEuler().z * Mathf::Deg2Rad;
    }

    /// World-space center of the collider: the offset turns and scales with the object (as a child at that local
    /// position would).
    Vec2 WorldCenter() const;
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
    /// Z rotation of the box in radians (2D boxes rotate about Z only). Drives
    /// oriented-box (SAT) collision so rotated crates collide by their true edges,
    /// not their axis-aligned bounds.
    float WorldAngle() const {
        Quat q = transform ? transform->Rotation() : Quat::Identity;
        return q.ToEuler().z * Mathf::Deg2Rad;
    }
    void WorldAABB(Vec2& outMin, Vec2& outMax) const override {
        Vec2 c = WorldCenter(), h = HalfExtents();
        // Expand the broad-phase bounds to the rotated box's extent so a spun box
        // isn't culled before the narrow-phase SAT test (unrotated -> exactly h).
        float a = WorldAngle();
        float ca = Mathf::Abs(Mathf::Cos(a)), sa = Mathf::Abs(Mathf::Sin(a));
        float ex = h.x * ca + h.y * sa;
        float ey = h.x * sa + h.y * ca;
        outMin = {c.x - ex, c.y - ey};
        outMax = {c.x + ex, c.y + ey};
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
    /// The capsule's inner segment endpoints in world space (the object's turn about Z carries the capsule with it).
    void Segment(Vec2& a, Vec2& b) const {
        Vec3 s = transform ? transform->LossyScale() : Vec3::One;
        Vec2 c = WorldCenter();
        float r = WorldRadius();
        float half = direction == Direction::Vertical ? Mathf::Max(0.0f, Mathf::Abs(size.y * s.y) * 0.5f - r)
                                                      : Mathf::Max(0.0f, Mathf::Abs(size.x * s.x) * 0.5f - r);
        float ang = WorldRotation(), ca = Mathf::Cos(ang), sa = Mathf::Sin(ang);
        Vec2 dir = direction == Direction::Vertical ? Vec2{-sa, ca} : Vec2{ca, sa};
        a = c - dir * half; b = c + dir * half;
    }
    void WorldAABB(Vec2& outMin, Vec2& outMax) const override {
        Vec2 a, b; Segment(a, b);
        float r = WorldRadius();
        outMin = {Mathf::Min(a.x, b.x) - r, Mathf::Min(a.y, b.y) - r};
        outMax = {Mathf::Max(a.x, b.x) + r, Mathf::Max(a.y, b.y) + r};
    }
};

/// Shared base for vertex-list colliders (edges and polygons). Vertices are in
/// local space, scaled, turned and positioned by the Transform. Edges are a line of
/// segments; Polygons are solid. Both collide with every other 2D shape through the
/// convex narrow phase (ConvexContact2D.hpp).
class PolyShapeCollider2D : public Collider2D {
public:
    std::vector<Vec2> points;          ///< local-space vertices

    /// Vertices transformed into world space.
    std::vector<Vec2> WorldPoints() const {
        Vec3 s = transform ? transform->LossyScale() : Vec3::One;
        Vec2 c = WorldCenter();
        float ang = WorldRotation(), ca = Mathf::Cos(ang), sa = Mathf::Sin(ang);
        std::vector<Vec2> out; out.reserve(points.size());
        for (const Vec2& v : points) {              // scale, turn with the object, then place
            float x = v.x * s.x, y = v.y * s.y;
            out.push_back({c.x + x * ca - y * sa, c.y + x * sa + y * ca});
        }
        return out;
    }
    /// True for polygons (the last vertex connects back to the first).
    virtual bool closedLoop() const = 0;

    void WorldAABB(Vec2& outMin, Vec2& outMax) const override {
        std::vector<Vec2> wp = WorldPoints();   // (the offset and turn are inside WorldPoints)
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

/// Polygon collider: a closed loop of vertices, solid inside (a concave outline is split into convex pieces). Bodies are
/// pushed out of the whole shape, not just its outline. Use it for ramps, platforms and arbitrary shapes.
class PolygonCollider2D : public PolyShapeCollider2D {
public:
    PolygonCollider2D() { points = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.0f, 0.5f}}; }
    Shape shape() const override { return Shape::Polygon; }
    bool  closedLoop() const override { return true; }
};

/// Composite collider: several simple shapes glued into one rigid collider (a plus sign, a dumbbell, a table). `parts` holds
/// them as text, one per ";":
///   circle   x y radius
///   box      x y width height angleDeg
///   capsule  x y radius length angleDeg      (length = distance between the two round ends' centres)
///   polygon  x y  x y  x y ...               (three or more corners; a concave one is split into convex pieces)
/// Positions are in local units, angle in degrees.
class CompositeCollider2D : public Collider2D {
public:
    std::string parts;       ///< the text above (blank = it collides with nothing)
    Shape shape() const override { return Shape::Composite; }
    void AddCircle(const Vec2& pos, float radius);
    void AddBox(const Vec2& pos, const Vec2& size, float angleDeg = 0.0f);
    void AddCapsule(const Vec2& pos, float radius, float length, float angleDeg = 0.0f);
    void AddPolygon(const std::vector<Vec2>& pts);
    void WorldAABB(Vec2& outMin, Vec2& outMax) const override;
};

} // namespace okay
