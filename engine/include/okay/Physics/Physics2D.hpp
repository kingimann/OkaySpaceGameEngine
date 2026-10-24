#pragma once
#include "okay/Math/Vec2.hpp"
#include "okay/Physics/PhysicsDebug.hpp"
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace okay {

class Scene;
class Collider2D;
class Rigidbody2D;
class GameObject;

/// Data passed to OnCollision2D callbacks describing a contact with another
/// object.
struct Collision2D {
    GameObject* gameObject = nullptr;  // the other object
    Collider2D* collider   = nullptr;  // the other collider
    Vec2  normal{0, 0};                // contact normal pointing toward the other
    float penetration = 0.0f;          // overlap depth
    Vec2  point{0, 0};                 // world-space contact point
    /// How hard the two hit: closing speed along the normal when the contact was
    /// found (world units / second, >= 0). Near 0 while resting, large on a crash.
    float impactSpeed = 0.0f;
    Vec2  relativeVelocity{0, 0};      // the other's velocity minus ours
};

/// Where a trigger overlap happened, readable from inside OnTrigger*2D callbacks
/// (Physics2D::CurrentTrigger), since those only receive the other collider.
struct TriggerContact2D {
    Collider2D* self  = nullptr;       // the collider whose object gets the callback
    Collider2D* other = nullptr;       // the collider it overlapped
    Vec2 point{0, 0};                  // approximate world point of the overlap
    Vec2 normal{0, 0};                 // from self toward other
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
/// scene queries (raycast / overlap / circle cast).
///
/// Queries take an optional `ignore` object, a `layerMask` (bit n = collider
/// layer n, default all) and `queryTriggers` (do triggers count as hits?).
/// Rays and overlaps include triggers by default; CircleCast skips them.
///
/// Assumes physics bodies are top-level (un-parented) Transforms.
class Physics2D {
public:
    static constexpr std::uint32_t kAllLayers = 0xFFFFFFFFu;

    Physics2D() { for (int i = 0; i < 32; ++i) m_layerMask[i] = 0xFFFFFFFFu; }

    Vec2 gravity{0.0f, -9.81f};

    /// Advance the simulation for `scene` by `dt` seconds.
    void Step(Scene& scene, float dt);

    // ---- Collision layers (0..31) -------------------------------------
    /// Enable/disable collisions between two layers (default: all enabled).
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
    RaycastHit2D Raycast(Scene& scene, const Vec2& origin, const Vec2& direction,
                         float maxDistance = 1e9f, GameObject* ignore = nullptr,
                         std::uint32_t layerMask = kAllLayers, bool queryTriggers = true);
    /// Every collider the ray passes through, nearest first.
    std::vector<RaycastHit2D> RaycastAll(Scene& scene, const Vec2& origin, const Vec2& direction,
                                         float maxDistance = 1e9f, GameObject* ignore = nullptr,
                                         std::uint32_t layerMask = kAllLayers, bool queryTriggers = true);
    /// Cast between two points: the nearest hit on segment a→b.
    RaycastHit2D Linecast(Scene& scene, const Vec2& a, const Vec2& b, GameObject* ignore = nullptr,
                          std::uint32_t layerMask = kAllLayers, bool queryTriggers = true) {
        Vec2 d = b - a; float len = d.Magnitude();
        if (len < 1e-6f) return RaycastHit2D{};
        return Raycast(scene, a, d / len, len, ignore, layerMask, queryTriggers);
    }
    /// The first collider containing the point, or nullptr.
    Collider2D* OverlapPoint(Scene& scene, const Vec2& point,
                             std::uint32_t layerMask = kAllLayers, bool queryTriggers = true);
    /// All colliders overlapping a circle.
    std::vector<Collider2D*> OverlapCircle(Scene& scene, const Vec2& center, float radius,
                                           std::uint32_t layerMask = kAllLayers, bool queryTriggers = true);
    /// All colliders overlapping an axis-aligned box (center + half-extents).
    std::vector<Collider2D*> OverlapBox(Scene& scene, const Vec2& center, const Vec2& halfExtents,
                                        std::uint32_t layerMask = kAllLayers, bool queryTriggers = true);

    /// Sweep a circle of `radius` from `origin` along `direction` and return the
    /// first collider it touches — a "thick ray"
    /// that won't slip through gaps a zero-width ray would. The hit point is on the
    /// struck surface; the normal points back toward origin.
    RaycastHit2D CircleCast(Scene& scene, const Vec2& origin, const Vec2& direction,
                            float radius, float maxDistance = 1e9f, GameObject* ignore = nullptr,
                            std::uint32_t layerMask = kAllLayers, bool queryTriggers = false);

    void Clear() { m_contacts.clear(); }

    /// The trigger overlap being reported, valid only inside an OnTrigger*2D
    /// callback (nullptr elsewhere) — lets scripts ask "where did it enter?".
    static const TriggerContact2D* CurrentTrigger();

    // ---- Debug view ----------------------------------------------------
    /// Forget last frame's debug rays / contacts (Scene::Update calls this).
    void BeginDebugFrame() { m_debugRays.clear(); m_debugContacts.clear(); }
    /// Record a query ray for the debug overlay (no-op unless the debug view is
    /// collecting rays from scripts).
    void RecordQueryRay(const Vec2& origin, const Vec2& direction, float maxDistance,
                        const RaycastHit2D& hit, float radius = 0.0f);
    const std::vector<PhysicsDebugRay2D>& DebugRays() const { return m_debugRays; }
    const std::vector<PhysicsDebugContact2D>& DebugContacts() const { return m_debugContacts; }

private:
    using Pair = std::pair<Collider2D*, Collider2D*>;
    std::set<Pair> m_contacts;          // contacts from the previous step
    std::uint32_t  m_layerMask[32];     // layer collision matrix (bit b of a)
    std::vector<PhysicsDebugRay2D>     m_debugRays;      // this frame (debug view only)
    std::vector<PhysicsDebugContact2D> m_debugContacts;
    /// Continuous collision for one fast body: sweep it along this step's motion
    /// and stop it at the first solid it would otherwise pass through.
    void SweepBody(Scene& scene, Rigidbody2D* rb, float dt);
};

} // namespace okay
