#pragma once
#include "okay/Math/Vec3.hpp"
#include <cstdint>
#include <vector>

namespace okay {

class Scene;
class GameObject;
class NavMesh;
class NavMeshSurface;
class NavMeshAgent;
class NavMeshObstacle;

/// Pathfinding for a scene: the baked NavMesh when the scene has a NavMeshSurface
/// with one, otherwise the lazily sampled NavGrid3D (so NPCs and click-to-move
/// keep working in scenes that never baked). NPCController, ClickToMoveController
/// and NavMeshAgent all go through here.
class Navigation {
public:
    /// The scene's active NavMeshSurface with a baked mesh (nullptr = none).
    static NavMeshSurface* Surface(Scene& scene);
    /// The baked mesh, or nullptr when the scene paths over the grid.
    static const NavMesh* Mesh(Scene& scene);

    /// Waypoints from `from` to `to` (start first, destination last), empty when no
    /// route exists (callers steer straight at the goal, as they always did).
    /// `agentRadius` and `self` only matter to the grid fallback: a baked mesh was
    /// already shrunk by its own agent radius and never contains moving things.
    static std::vector<Vec3> FindPath(Scene& scene, const Vec3& from, const Vec3& to,
                                      float agentRadius = 0.4f, GameObject* self = nullptr);
    /// The same, into a caller-owned vector (no allocation once it has capacity).
    static bool FindPath(Scene& scene, const Vec3& from, const Vec3& to, std::vector<Vec3>& out,
                         float agentRadius = 0.4f, GameObject* self = nullptr);
    /// The closest walkable point within `maxDistance` (mesh only; false without one).
    static bool SamplePosition(Scene& scene, const Vec3& p, float maxDistance, Vec3& out);
    /// Straight-line walkability along the mesh: true when blocked, `hit` = where.
    static bool Raycast(Scene& scene, const Vec3& from, const Vec3& to, Vec3& hit);
    /// A random walkable point within `radius` of `center` (wandering NPCs).
    static bool RandomPoint(Scene& scene, const Vec3& center, float radius, std::uint32_t& seed, Vec3& out);
    /// Ground height under `p` on the mesh, or `fallback`.
    static float SampleHeight(Scene& scene, const Vec3& p, float fallback);

    /// Every NavMeshAgent / NavMeshObstacle in the scene (cached until the scene's
    /// objects or components change), for local avoidance.
    static const std::vector<NavMeshAgent*>& Agents(Scene& scene);
    static const std::vector<NavMeshObstacle*>& Obstacles(Scene& scene);
};

} // namespace okay
