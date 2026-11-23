#pragma once
#include "okay/AI/GridAStar.hpp"
#include "okay/AI/NavMesh.hpp"
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
class NavMeshLink;
class Tilemap;

/// Pathfinding for a scene: the baked NavMesh when the scene has a NavMeshSurface
/// with one, otherwise the lazily sampled NavGrid3D (so NPCs and click-to-move
/// keep working in scenes that never baked). NPCController, ClickToMoveController
/// and NavMeshAgent all go through here.
///
/// Grid paths for 2D games and tilemaps are part of the same system (GridPath,
/// TilemapPath below; the 2D agent is GridPathAgent2D). They and the 3D grid
/// fallback run the one A* search in GridAStar.hpp.
class Navigation {
public:
    // ---- Grid paths (2D games, tilemaps, tower defence) ----------------------------------

    /// The most cells (width * height) a grid search will take on: 4096 x 4096. A larger grid has no
    /// path (an empty result), so a bad size can never size an allocation or wrap an index.
    static constexpr long long kMaxGridCells = 1LL << 24;

    /// A path from (sx, sy) to (gx, gy) over a width*height grid where
    /// `walkable[y * width + x]` is true for passable cells. Returns the cells
    /// from start to goal inclusive, or an empty vector if no path exists.
    ///
    /// Defaults: four directions, every open cell costs 1, every cell kept.
    /// `allowDiagonal` adds the four diagonal steps. A diagonal never squeezes
    /// between two blocked cells (both orthogonal neighbours must be open).
    /// `costs`, when set, is one price per cell in the same order as `walkable`.
    /// Stepping into a cell costs the straight or diagonal length times that
    /// price. A price of 0 or less blocks the cell. A null pointer, or a vector
    /// of the wrong length, leaves every open cell at price 1.
    /// `smooth` pulls the path straight wherever the line of sight is clear
    /// (string pulling). It does not re-price the route: it only drops cells
    /// the line does not need, and it never crosses a blocked cell.
    static std::vector<GridCell> GridPath(const std::vector<bool>& walkable, int width, int height,
                                          int sx, int sy, int gx, int gy, bool allowDiagonal = false,
                                          const std::vector<float>* costs = nullptr, bool smooth = false);

    /// A grid path over a Tilemap. With no cost table, tile id 0 is walkable and
    /// any other id is a wall. `tileCost`, when set and non-empty, is the price
    /// of each tile id (index = id). A price of 0 or less is a wall. An id past
    /// the end of the table is a wall, except id 0, which stays price 1.
    static std::vector<GridCell> TilemapPath(const Tilemap& map, int sx, int sy, int gx, int gy,
                                             bool allowDiagonal = false,
                                             const std::vector<float>* tileCost = nullptr, bool smooth = false);

    /// True when the straight line between the two cell centres stays on open
    /// cells. A line that passes exactly through a grid corner is blocked when
    /// either cell beside that corner is a wall, the same rule as a diagonal step.
    static bool GridLineClear(const std::vector<bool>& walkable, int width, int height, GridCell a, GridCell b);

    /// String-pull `path`: keep a later cell whenever the line of sight from the
    /// current one reaches it. The result is a subsequence of `path`.
    static std::vector<GridCell> SmoothGridPath(const std::vector<bool>& walkable, int width, int height,
                                                const std::vector<GridCell>& path);

    // ---- Scene navigation (navmesh, or the 3D grid when nothing is baked) ----------------

    /// The scene's active NavMeshSurface with a baked mesh (nullptr = none).
    static NavMeshSurface* Surface(Scene& scene);
    /// The mesh queries use, or nullptr when the scene paths over the grid: the baked mesh with the holes
    /// of every carving NavMeshObstacle cut out (see NavMeshObstacle), rebuilt when they change.
    static const NavMesh* Mesh(Scene& scene);
    /// The mesh as baked, without any obstacle holes (what the .okaynav file holds).
    static const NavMesh* BakedMesh(Scene& scene);
    /// Goes up whenever the carved mesh changes (an obstacle came to rest, moved or went away). Agents
    /// compare it with the value they planned under and replan. 0 without a mesh.
    static std::uint32_t CarveGeneration(Scene& scene);

    /// Waypoints from `from` to `to` (start first, destination last), empty when no
    /// route exists (callers steer straight at the goal, as they always did).
    /// `agentRadius` and `self` only matter to the grid fallback: a baked mesh was
    /// already shrunk by its own agent radius and never contains moving things.
    static std::vector<Vec3> FindPath(Scene& scene, const Vec3& from, const Vec3& to,
                                      float agentRadius = 0.4f, GameObject* self = nullptr);
    /// The same, into a caller-owned vector (no allocation once it has capacity).
    static bool FindPath(Scene& scene, const Vec3& from, const Vec3& to, std::vector<Vec3>& out,
                         float agentRadius = 0.4f, GameObject* self = nullptr);
    /// Like FindPath for an agent with an area mask: honours the surface's area costs and the
    /// mask (a forbidden area is never entered; an unreachable goal gives false) and plans
    /// through the scene's enabled NavMeshLinks whose area the mask allows. `legLinks` is
    /// parallel to `out`: non-null at a corner means "the leg that ends here is this link's
    /// crossing from the previous corner". Without a baked mesh this is the grid path with
    /// no links.
    static bool FindPathForAgent(Scene& scene, const Vec3& from, const Vec3& to, std::vector<Vec3>& out,
                                 std::vector<NavMeshLink*>& legLinks, std::uint32_t areaMask,
                                 float agentRadius = 0.4f, GameObject* self = nullptr);
    /// Every NavMeshLink in the scene (cached with the agents).
    static const std::vector<NavMeshLink*>& Links(Scene& scene);
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
