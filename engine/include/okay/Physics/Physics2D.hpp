#pragma once
#include "okay/Math/Vec2.hpp"
#include <cstdint>
#include <set>
#include <utility>

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

/// A small but real 2D physics engine: semi-implicit Euler integration with
/// gravity/drag, box & circle collision detection, impulse + positional
/// resolution, and Enter/Stay/Exit collision and trigger callbacks.
///
/// Assumes physics bodies are top-level (un-parented) Transforms.
class Physics2D {
public:
    Vec2 gravity{0.0f, -9.81f};

    /// Advance the simulation for `scene` by `dt` seconds.
    void Step(Scene& scene, float dt);

    void Clear() { m_contacts.clear(); }

private:
    using Pair = std::pair<Collider2D*, Collider2D*>;
    std::set<Pair> m_contacts; // contacts from the previous step
};

} // namespace okay
