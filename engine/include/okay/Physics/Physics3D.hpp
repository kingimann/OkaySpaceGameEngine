#pragma once
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace okay {

class Scene;
class Collider3D;
class GameObject;

/// Data passed to OnCollision3D callbacks describing a contact with another
/// object (mirrors Unity's Collision, simplified).
struct Collision3D {
    GameObject* gameObject = nullptr;  // the other object
    Collider3D* collider   = nullptr;  // the other collider
    Vec3  normal{0, 0, 0};             // contact normal pointing toward the other
    float penetration = 0.0f;          // overlap depth
};

/// Result of a 3D raycast.
struct RaycastHit3D {
    bool        hit = false;
    Collider3D* collider = nullptr;
    GameObject* gameObject = nullptr;
    Vec3  point{0, 0, 0};
    Vec3  normal{0, 0, 0};
    float distance = 0.0f;
    explicit operator bool() const { return hit; }
};

/// A small but real 3D physics engine: semi-implicit Euler integration with
/// gravity/drag, box / sphere / capsule collision detection, impulse +
/// positional resolution, Enter/Stay/Exit collision and trigger callbacks, and
/// scene queries (raycast / overlap). Box colliders are treated as axis-aligned.
///
/// Assumes physics bodies are top-level (un-parented) Transforms.
class Physics3D {
public:
    Physics3D() { for (int i = 0; i < 32; ++i) m_layerMask[i] = 0xFFFFFFFFu; }

    Vec3 gravity{0.0f, -9.81f, 0.0f};

    /// Advance the simulation for `scene` by `dt` seconds.
    void Step(Scene& scene, float dt);

    // ---- Collision layers (0..31) -------------------------------------
    void SetLayerCollision(int a, int b, bool enabled) {
        if (a < 0 || a > 31 || b < 0 || b > 31) return;
        if (enabled) { m_layerMask[a] |= (1u << b); m_layerMask[b] |= (1u << a); }
        else { m_layerMask[a] &= ~(1u << b); m_layerMask[b] &= ~(1u << a); }
    }
    bool LayersCollide(int a, int b) const {
        if (a < 0 || a > 31 || b < 0 || b > 31) return true;
        return (m_layerMask[a] >> b) & 1u;
    }

    // ---- Scene queries -------------------------------------------------
    /// Cast a ray and return the nearest collider hit (within maxDistance).
    /// `ignore` (optional) skips a GameObject's colliders — e.g. the camera ray
    /// ignoring the player it's attached to.
    RaycastHit3D Raycast(Scene& scene, const Vec3& origin, const Vec3& direction,
                         float maxDistance = 1e9f, GameObject* ignore = nullptr);
    /// All colliders overlapping a sphere.
    std::vector<Collider3D*> OverlapSphere(Scene& scene, const Vec3& center, float radius);

    /// Push a sphere out of any SOLID colliders it overlaps (depenetration) and
    /// return the corrected centre — a lightweight "collide and slide" used to keep
    /// transform-only players (no Rigidbody3D) from clipping through walls/floors.
    /// `ignore` skips a GameObject (the mover itself); triggers never block.
    Vec3 ResolveSphere(Scene& scene, Vec3 center, float radius,
                       GameObject* ignore = nullptr, int iterations = 4);

    void Clear() { m_contacts.clear(); }

private:
    using Pair = std::pair<Collider3D*, Collider3D*>;
    std::set<Pair> m_contacts;          // contacts from the previous step
    std::uint32_t  m_layerMask[32];     // layer collision matrix (bit b of a)
};

} // namespace okay
