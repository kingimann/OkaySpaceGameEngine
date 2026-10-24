#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Physics/PhysicsDebug.hpp"
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace okay {

class Scene;
class Collider3D;
class Rigidbody3D;
class GameObject;

/// Data passed to OnCollision3D callbacks describing a contact with another
/// object.
struct Collision3D {
    GameObject* gameObject = nullptr;  // the other object
    Collider3D* collider   = nullptr;  // the other collider
    Vec3  normal{0, 0, 0};             // contact normal pointing toward the other
    float penetration = 0.0f;          // overlap depth
    Vec3  point{0, 0, 0};              // world-space contact point
    /// How hard the two hit: closing speed along the normal when the contact was
    /// found (world units / second, >= 0). Near 0 while resting, large on a crash.
    float impactSpeed = 0.0f;
    Vec3  relativeVelocity{0, 0, 0};   // the other's velocity minus ours
};

/// Where a trigger overlap happened, readable from inside OnTrigger*3D callbacks
/// (Physics3D::CurrentTrigger), since those only receive the other collider.
struct TriggerContact3D {
    Collider3D* self  = nullptr;       // the collider whose object gets the callback
    Collider3D* other = nullptr;       // the collider it overlapped
    Vec3 point{0, 0, 0};               // approximate world point of the overlap
    Vec3 normal{0, 0, 0};              // from self toward other
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
/// scene queries (raycast / overlap / shape casts). Boxes are oriented (SAT).
///
/// Every query takes an optional `layerMask` (bit n = collider layer n, default
/// all layers) and `queryTriggers` (do trigger colliders count as hits?). Rays
/// and overlaps include triggers by default; shape casts skip them, as they
/// always did (casts are mostly used for movement).
///
/// Assumes physics bodies are top-level (un-parented) Transforms.
class Physics3D {
public:
    static constexpr std::uint32_t kAllLayers = 0xFFFFFFFFu;

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
                         float maxDistance = 1e9f, GameObject* ignore = nullptr,
                         std::uint32_t layerMask = kAllLayers, bool queryTriggers = true);
    /// Every collider (and terrain) the ray passes through, nearest first.
    std::vector<RaycastHit3D> RaycastAll(Scene& scene, const Vec3& origin, const Vec3& direction,
                                         float maxDistance = 1e9f, GameObject* ignore = nullptr,
                                         std::uint32_t layerMask = kAllLayers, bool queryTriggers = true);
    /// Cast between two points: the nearest hit on segment a→b.
    RaycastHit3D Linecast(Scene& scene, const Vec3& a, const Vec3& b, GameObject* ignore = nullptr,
                          std::uint32_t layerMask = kAllLayers, bool queryTriggers = true) {
        Vec3 d = b - a; float len = d.Magnitude();
        if (len < 1e-6f) return RaycastHit3D{};
        return Raycast(scene, a, d * (1.0f / len), len, ignore, layerMask, queryTriggers);
    }
    /// All colliders overlapping a sphere.
    std::vector<Collider3D*> OverlapSphere(Scene& scene, const Vec3& center, float radius,
                                           std::uint32_t layerMask = kAllLayers, bool queryTriggers = true,
                                           GameObject* ignore = nullptr);
    /// All colliders overlapping a box (center, half-extents, optional rotation).
    std::vector<Collider3D*> OverlapBox(Scene& scene, const Vec3& center, const Vec3& halfExtents,
                                        const Quat& rotation = Quat::Identity,
                                        std::uint32_t layerMask = kAllLayers, bool queryTriggers = true,
                                        GameObject* ignore = nullptr);
    /// All colliders overlapping a capsule (segment a→b thickened by radius).
    std::vector<Collider3D*> OverlapCapsule(Scene& scene, const Vec3& a, const Vec3& b, float radius,
                                            std::uint32_t layerMask = kAllLayers, bool queryTriggers = true,
                                            GameObject* ignore = nullptr);

    /// Sweep a sphere of `radius` from `origin` along `direction` and return the
    /// first collider it touches — a "thick ray" that
    /// won't slip through gaps a zero-width ray would. `ignore` skips a GameObject.
    /// The hit point is on the struck surface; the normal points back toward origin.
    RaycastHit3D SphereCast(Scene& scene, const Vec3& origin, const Vec3& direction,
                            float radius, float maxDistance = 1e9f, GameObject* ignore = nullptr,
                            std::uint32_t layerMask = kAllLayers, bool queryTriggers = false);
    /// Sweep a box (center, half-extents, rotation) along `direction` and return
    /// the first collider it touches (Unity's Physics.BoxCast).
    RaycastHit3D BoxCast(Scene& scene, const Vec3& center, const Vec3& halfExtents,
                         const Vec3& direction, const Quat& rotation = Quat::Identity,
                         float maxDistance = 1e9f, GameObject* ignore = nullptr,
                         std::uint32_t layerMask = kAllLayers, bool queryTriggers = false);
    /// Sweep a capsule (segment a→b, radius) along `direction` and return the
    /// first collider it touches (Unity's Physics.CapsuleCast) — "would my
    /// character fit through there?".
    RaycastHit3D CapsuleCast(Scene& scene, const Vec3& a, const Vec3& b, float radius,
                             const Vec3& direction, float maxDistance = 1e9f, GameObject* ignore = nullptr,
                             std::uint32_t layerMask = kAllLayers, bool queryTriggers = false);

    /// Push a sphere out of any SOLID colliders it overlaps (depenetration) and
    /// return the corrected centre — a lightweight "collide and slide" used to keep
    /// transform-only players (no Rigidbody3D) from clipping through walls/floors.
    /// `ignore` skips a GameObject (the mover itself); triggers never block.
    Vec3 ResolveSphere(Scene& scene, Vec3 center, float radius,
                       GameObject* ignore = nullptr, int iterations = 4);

    void Clear() { m_contacts.clear(); }

    /// The trigger overlap being reported, valid only inside an OnTrigger*3D
    /// callback (nullptr elsewhere) — lets scripts ask "where did it enter?".
    static const TriggerContact3D* CurrentTrigger();

    // ---- Debug view ----------------------------------------------------
    /// Forget last frame's debug rays / contacts (Scene::Update calls this).
    void BeginDebugFrame() { m_debugRays.clear(); m_debugContacts.clear(); }
    /// Record a query ray for the debug overlay. A no-op unless the debug view is
    /// collecting with "rays from scripts" on; scripts, visual scripts and Actions
    /// call it after their casts. `radius` > 0 marks a sphere-style cast.
    void RecordQueryRay(const Vec3& origin, const Vec3& direction, float maxDistance,
                        const RaycastHit3D& hit, float radius = 0.0f);
    const std::vector<PhysicsDebugRay3D>& DebugRays() const { return m_debugRays; }
    const std::vector<PhysicsDebugContact3D>& DebugContacts() const { return m_debugContacts; }

private:
    using Pair = std::pair<Collider3D*, Collider3D*>;
    std::set<Pair> m_contacts;          // contacts from the previous step
    std::uint32_t  m_layerMask[32];     // layer collision matrix (bit b of a)
    std::vector<PhysicsDebugRay3D>     m_debugRays;      // this frame (debug view only)
    std::vector<PhysicsDebugContact3D> m_debugContacts;
    /// Continuous collision for one fast body: sweep it along this step's motion
    /// and stop it at the first solid it would otherwise pass through.
    void SweepBody(Scene& scene, Rigidbody3D* rb, float dt);
};

} // namespace okay
