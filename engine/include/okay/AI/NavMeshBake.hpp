#pragma once
#include "okay/AI/NavMesh.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Quat.hpp"
#include <atomic>
#include <cstdint>
#include <vector>

namespace okay {

class Scene;
class GameObject;

/// The static geometry a bake works from, copied out of the scene on the main
/// thread so the bake itself can run on a worker thread while the editor stays
/// responsive. Colliders become oriented boxes / spheres / capsules; heightmap
/// terrains are copied as heightfields.
struct NavBakeInput {
    struct Box     { Vec3 center, half; Quat rot; };
    struct Sphere  { Vec3 center; float radius; };
    struct Capsule { Vec3 a, b; float radius; };
    struct Heightfield {
        Mat4 l2w, w2l;
        float size = 0.0f;           // local width / depth (centred on the object)
        int   dim = 0;               // heights per side
        std::vector<float> heights;  // row-major, dim * dim
        Vec3 aabbMin, aabbMax;       // world bounds
        float Height(int x, int z) const {
            if (x < 0 || z < 0 || x >= dim || z >= dim) return 0.0f;
            return heights[(std::size_t)z * dim + x];
        }
        float Sample(float lx, float lz) const;   // bilinear, local XZ
        Vec3  Normal(float lx, float lz) const;   // local, from the height gradient
    };
    std::vector<Box> boxes;
    std::vector<Sphere> spheres;
    std::vector<Capsule> capsules;
    std::vector<Heightfield> terrains;
    /// The world box to bake; without it the union of the geometry (padded) is used.
    bool hasBounds = false;
    Vec3 boundsMin, boundsMax;
};

struct NavBakeStats {
    int columns = 0;     ///< heightfield columns (cellSize cells over the bounds)
    int cells = 0;       ///< floor candidates found (several per column where floors stack)
    int walkable = 0;    ///< cells left after the slope / step / clearance / radius filters
    int polys = 0;
    int links = 0;
    int vertices = 0;
    double seconds = 0.0;
};

/// Builds a NavMesh from NavBakeInput: rasterizes the geometry into a heightfield
/// of solid spans per column (so floors above floors and bridges work), keeps the
/// span tops with head room as floor cells, drops the ones steeper than the max
/// slope, connects neighbours within the step height, erodes the walkable area by
/// the agent radius, merges the cells into convex polygons (axis-aligned
/// rectangles that stay within the cell-height tolerance of a plane) and joins
/// neighbouring polygons with portals the funnel algorithm pulls paths through.
class NavMeshBaker {
public:
    /// Copy the scene's static collision geometry (main thread only). Leaves out
    /// triggers, disabled colliders, moving things (NavMeshAgent / NavMeshObstacle
    /// objects, player and NPC controllers, Characters) and, unless asked, bodies
    /// with a dynamic Rigidbody3D. `layerMask` bit n keeps collider layer n.
    static void Gather(Scene& scene, NavBakeInput& out, bool includeDynamic = false,
                       std::uint32_t layerMask = 0xFFFFFFFFu);
    /// Build the mesh (any thread). `progress` is written 0..1; set `cancel` to stop
    /// early (returns false). Returns false on a cancelled or oversized bake.
    static bool Bake(const NavBakeInput& in, const NavMeshBuildSettings& settings, NavMesh& out,
                     std::atomic<float>* progress = nullptr, std::atomic<bool>* cancel = nullptr,
                     NavBakeStats* stats = nullptr);
    /// Is this object (or an ancestor) something that moves and so must not be baked?
    static bool IsMovingThing(const GameObject* go);
};

} // namespace okay
