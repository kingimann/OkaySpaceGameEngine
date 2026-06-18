#pragma once
#include "okay/Math/Vec2.hpp"
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace okay {

class Scene;
class Collider2D;
class GameObject;

/// Data passed to OnCollision2D callbacks describing a contact with another
/// object (mirrors Unity's Collision2D, simplified).
struct Collision2D {
    GameObject* gameObject = nullptr;  // the other object
    Collider2D* collider   = nullptr;  // the other collider
    Vec2  normal{0, 0};                // contact normal pointing toward the other
    float penetration = 0.0f;          // overlap depth
};

/// Result of a 2D raycast.
struct RaycastHit2D {
    bool        hit = false;
    Collider2D* collider = nullptr;
    GameObject* gameObject = nullptr;
    Vec2  point{0, 0};
    Vec2  normal{0, 0};
    float distance = 0.0f;
    explicit operator bool() const { return hit; }
};

/// A small but real 2D physics engine: semi-implicit Euler integration with
/// gravity/drag, box & circle collision detection, impulse + positional
/// resolution, and Enter/Stay/Exit collision and trigger callbacks. Also offers
/// scene queries (raycast / overlap).
///
/// Assumes physics bodies are top-level (un-parented) Transforms.
class Physics2D {
public:
    Vec2 gravity{0.0f, -9.81f};

    /// Advance the simulation for `scene` by `dt` seconds.
    void Step(Scene& scene, float dt);

    // ---- Scene queries -------------------------------------------------
    /// Cast a ray and return the nearest collider hit (within maxDistance).
    RaycastHit2D Raycast(Scene& scene, const Vec2& origin, const Vec2& direction,
                         float maxDistance = 1e9f);
    /// The first collider containing the point, or nullptr.
    Collider2D* OverlapPoint(Scene& scene, const Vec2& point);
    /// All colliders overlapping a circle.
    std::vector<Collider2D*> OverlapCircle(Scene& scene, const Vec2& center, float radius);
    /// All colliders overlapping an axis-aligned box (center + half-extents).
    std::vector<Collider2D*> OverlapBox(Scene& scene, const Vec2& center, const Vec2& halfExtents);

    void Clear() { m_contacts.clear(); }

private:
    using Pair = std::pair<Collider2D*, Collider2D*>;
    std::set<Pair> m_contacts; // contacts from the previous step
};

} // namespace okay
