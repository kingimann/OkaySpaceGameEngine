#pragma once
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Physics/PhysicsDebug.hpp"
#include "okay/Physics/PhysicsMaterialAsset.hpp"
#include "okay/Physics/PhysicsQuality.hpp"
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace okay {

class Scene;
class Collider3D;
class Terrain;
class Rigidbody3D;
class GameObject;
class Joint3D;
namespace phys { class World; }

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
    /// The Physics Material pair of the contact (nullptr = no PhysicsSurface on that side): what the other surface is, and
    /// what ours is. `surface->impactSound`, `PhysicsMaterials::SurfaceName(*surface)` and so on tell a script or an audio
    /// hook how to react ("play the wood sound when a metal ball hits wood").
    const PhysicsMaterialData* surface = nullptr;
    const PhysicsMaterialData* ownSurface = nullptr;
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

/// The closest point on a solid collider to a world point. `distance` is 0 when the point is inside the solid.
/// Triggers are skipped. `hit` is false when nothing is within `maxDistance`.
struct NearestSolid3D {
    bool        hit = false;
    Collider3D* collider = nullptr;
    GameObject* gameObject = nullptr;
    Vec3  point{0, 0, 0};
    float distance = 0.0f;
    explicit operator bool() const { return hit; }
};

/// The scene's 3D physics. Step() hands every Rigidbody3D, collider and Joint3D to our own physics engine core
/// (okay::phys::World: tree broad phase, exact contact manifolds, a sequential-impulse solver, islands that sleep,
/// speculative contacts so fast bodies do not tunnel, joints with limits, motors and drives; docs/physics/physics_core.md)
/// and raises Enter/Stay/Exit collision and trigger callbacks. Scene queries (raycast / overlap / shape casts) are here.
///
/// Every query takes an optional `layerMask` (bit n = collider layer n, default
/// all layers) and `queryTriggers` (do trigger colliders count as hits?). Rays
/// and overlaps include triggers by default; shape casts skip them, as they
/// always did (casts are mostly used for movement).
///
/// A body may sit under a parent. The step moves it in world space, so a moving or
/// turning parent carries it and gravity stays world-down. Un-parenting keeps the
/// speed that parent was giving it (see docs/physics/physics.md).
class Physics3D {
public:
    static constexpr std::uint32_t kAllLayers = 0xFFFFFFFFu;

    Physics3D();          // (out of line: the engine world is an incomplete type here)
    ~Physics3D();
    Physics3D(const Physics3D&) = delete;
    Physics3D& operator=(const Physics3D&) = delete;

    Vec3 gravity{0.0f, -9.81f, 0.0f};

    /// Kept only so scene files round-trip: there is one physics path (our own engine core) and this value changes
    /// nothing. Old files saved Classic (0), Improved (1) or Engine (2) as `physicsquality`; the value read is written
    /// back as it was. See PhysicsQuality.hpp.
    PhysicsQuality quality = PhysicsQuality::Classic;
    /// How many times the solver repeats per step (more = stiffer stacks and joints, slower). 4 to 30.
    int solverIterations = 10;
    /// Kept for old files (fast bodies are always caught now: the solver stops them before they pass anything).
    bool autoContinuous = true;
    /// What the last Step did.
    const PhysicsStepStats& Stats() const { return m_stats; }

    /// Advance the simulation for `scene` by `dt` seconds.
    /// Longest slice solved in one go (seconds). A longer step (30 steps a second, a slow frame) is cut into equal slices
    /// no longer than this, so a tower of crates is as steady at 30 steps a second as at 60.
    static constexpr float kMaxSolveSlice = 1.0f / 45.0f;
    void Step(Scene& scene, float dt);

    // ---- Collision layers (0..31) -------------------------------------
    void SetLayerCollision(int a, int b, bool enabled) {
        if (a < 0 || a > 31 || b < 0 || b > 31) return;
        if (enabled) { m_layerMask[a] |= (1u << b); m_layerMask[b] |= (1u << a); }
        else { m_layerMask[a] &= ~(1u << b); m_layerMask[b] &= ~(1u << a); }
    }
    bool LayersCollide(int a, int b) const {
        return (m_layerMask[a & 31] >> (b & 31)) & 1u;   // wraps like the query masks do
    }
    /// Bit b set = layer b collides with `layer` (usable as a query layerMask).
    std::uint32_t CollisionMask(int layer) const { return m_layerMask[layer & 31]; }
    void ResetLayerCollisions() { for (int i = 0; i < 32; ++i) m_layerMask[i] = 0xFFFFFFFFu; }

    // ---- Scene queries -------------------------------------------------
    /// While one of these is alive, scene queries (raycasts, sweeps, overlaps)
    /// also skip every collider on `root` and on its children: a character's own
    /// parts (an imported model's pieces, a held item, hitboxes) never block its
    /// camera or its foot / step probes.
    ///   { Physics3D::IgnoreHierarchy own(player); h = physics.SphereCast(...); }
    class IgnoreHierarchy {
    public:
        explicit IgnoreHierarchy(const GameObject* root);
        ~IgnoreHierarchy();
        IgnoreHierarchy(const IgnoreHierarchy&) = delete;
        IgnoreHierarchy& operator=(const IgnoreHierarchy&) = delete;
    private:
        const GameObject* m_prev;
    };

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
    /// Every collider the sphere sweep touches, nearest first (a shot that should report the wall and
    /// what is behind it). Same rules as SphereCast, including triggers only when `queryTriggers` is set.
    std::vector<RaycastHit3D> SphereCastAll(Scene& scene, const Vec3& origin, const Vec3& direction,
                                            float radius, float maxDistance = 1e9f, GameObject* ignore = nullptr,
                                            std::uint32_t layerMask = kAllLayers, bool queryTriggers = false);
    /// Sweep a box (center, half-extents, rotation) along `direction` and return
    /// the first collider it touches.
    RaycastHit3D BoxCast(Scene& scene, const Vec3& center, const Vec3& halfExtents,
                         const Vec3& direction, const Quat& rotation = Quat::Identity,
                         float maxDistance = 1e9f, GameObject* ignore = nullptr,
                         std::uint32_t layerMask = kAllLayers, bool queryTriggers = false);
    /// Every collider the box sweep touches, nearest first.
    std::vector<RaycastHit3D> BoxCastAll(Scene& scene, const Vec3& center, const Vec3& halfExtents,
                                         const Vec3& direction, const Quat& rotation = Quat::Identity,
                                         float maxDistance = 1e9f, GameObject* ignore = nullptr,
                                         std::uint32_t layerMask = kAllLayers, bool queryTriggers = false);
    /// Sweep a capsule (segment a→b, radius) along `direction` and return the
    /// first collider it touches — "would my
    /// character fit through there?".
    RaycastHit3D CapsuleCast(Scene& scene, const Vec3& a, const Vec3& b, float radius,
                             const Vec3& direction, float maxDistance = 1e9f, GameObject* ignore = nullptr,
                             std::uint32_t layerMask = kAllLayers, bool queryTriggers = false);
    /// Every collider the capsule sweep touches, nearest first.
    std::vector<RaycastHit3D> CapsuleCastAll(Scene& scene, const Vec3& a, const Vec3& b, float radius,
                                             const Vec3& direction, float maxDistance = 1e9f,
                                             GameObject* ignore = nullptr, std::uint32_t layerMask = kAllLayers,
                                             bool queryTriggers = false);

    /// Push a sphere out of any SOLID colliders it overlaps (depenetration) and
    /// return the corrected centre — a lightweight "collide and slide" used to keep
    /// transform-only players (no Rigidbody3D) from clipping through walls/floors.
    /// `ignore` skips a GameObject (the mover itself); triggers never block.
    Vec3 ResolveSphere(Scene& scene, Vec3 center, float radius,
                       GameObject* ignore = nullptr, int iterations = 4);
    /// Push a capsule (segment centre, unit `axis`, distance from the centre to a cap centre, radius) out of
    /// solid colliders and return the corrected centre. A zero-length capsule is a sphere. Used so a
    /// transform-moved character keeps its head and feet out of ceilings and floors, which one sphere
    /// around the waist does not.
    Vec3 ResolveCapsule(Scene& scene, Vec3 center, Vec3 axis, float halfSegment, float radius,
                        GameObject* ignore = nullptr, int iterations = 4);
    /// The closest point on any solid collider to `point`, within `maxDistance`.
    NearestSolid3D ClosestPoint(Scene& scene, const Vec3& point, float maxDistance = 1e9f,
                                GameObject* ignore = nullptr, std::uint32_t layerMask = kAllLayers);

    /// What the scene queries above did since the last ResetQueryStats (PhysicsQueryStats).
    const PhysicsQueryStats& QueryStats() const { return m_queryStats; }
    void ResetQueryStats() { m_queryStats = PhysicsQueryStats{}; }
    /// Test switch: while true, a scene query hands every collider of the scene to its exact tests instead of asking
    /// the broad phase which ones are near. The code after that is the same, so tests compare the two answers.
    static bool& QueryFullScan();

    void Clear();   // forget the engine world (the scene is being cleared; Physics3DEngine.inl)

    // ---- the physics engine core underneath (okay::phys::World; docs/physics/physics_core.md) ----
    /// The engine world that runs the scene (made on the first step; nullptr before that). For advanced use, such as an
    /// active ragdoll that sets joint drive targets directly.
    phys::World* EngineWorld();
    /// The engine body of a Rigidbody3D (0xFFFFFFFF when it has none yet).
    std::uint32_t EngineBody(const Rigidbody3D* rb) const;
    /// The engine joint of a Joint3D (0xFFFFFFFF when it has none).
    std::uint32_t EngineJoint(const Joint3D* j) const;

    /// How still a body must be, and for how long, before it sleeps. Linear and angular speeds are
    /// metres and radians per second. The next step uses these. Defaults match the engine (0.05, 0.05, 0.5).
    void SetSleep(float linearSpeed, float angularSpeed, float seconds);

    /// The trigger overlap being reported, valid only inside an OnTrigger*3D
    /// callback (nullptr elsewhere) — lets scripts ask "where did it enter?".
    static const TriggerContact3D* CurrentTrigger();

    // ---- Debug view ----------------------------------------------------
    /// Forget last frame's debug rays / contacts (Scene::Update calls this).
    void BeginDebugFrame();
    /// Record a query ray for the debug overlay. A no-op unless the debug view is
    /// collecting with "rays from scripts" on; scripts, visual scripts and Actions
    /// call it after their casts. `radius` > 0 marks a sphere-style cast.
    void RecordQueryRay(const Vec3& origin, const Vec3& direction, float maxDistance,
                        const RaycastHit3D& hit, float radius = 0.0f);
    const std::vector<PhysicsDebugRay3D>& DebugRays() const { return m_debugRays; }
    const std::vector<PhysicsDebugContact3D>& DebugContacts() const { return m_debugContacts; }
    /// Bodies and joints of the last step (filled only while the debug view collects).
    const std::vector<PhysicsDebugBody3D>& DebugBodies() const { return m_debugBodies; }
    const std::vector<PhysicsDebugJoint3D>& DebugJoints() const { return m_debugJoints; }

private:
    PhysicsStepStats m_stats;
    PhysicsQueryStats m_queryStats;
    bool m_sleepSet = false;
    float m_sleepLinear = 0.05f, m_sleepAngular = 0.05f, m_sleepTime = 0.5f;
    struct EngineBridge;                // the engine world and the scene <-> world maps (Physics3DEngine.inl)
    std::unique_ptr<EngineBridge> m_engine;
    void StepEngine(Scene& scene, float dt);
    std::uint32_t  m_layerMask[32];     // layer collision matrix (bit b of a)
    std::vector<PhysicsDebugRay3D>     m_debugRays;      // this frame (debug view only)
    std::vector<PhysicsDebugContact3D> m_debugContacts;
    std::vector<PhysicsDebugBody3D>    m_debugBodies;
    std::vector<PhysicsDebugJoint3D>   m_debugJoints;
    void StepTerrain(Scene& scene);   // grounded-on-terrain signals and voxel terrain collision (after the engine step)
    /// The colliders and terrains ray queries test, gathered once and reused
    /// until the scene's objects or components change (not per ray).
    struct QueryCache {
        std::uint64_t scene = 0, structure = 0, components = 0;
        std::vector<Collider3D*> colliders;
        std::vector<Terrain*> terrains;
    };
    QueryCache m_query;
    const QueryCache& QueryLists(Scene& scene);
};

} // namespace okay
