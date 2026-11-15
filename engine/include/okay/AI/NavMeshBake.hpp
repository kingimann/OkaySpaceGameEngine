#pragma once
#include "okay/AI/NavMesh.hpp"
#include "okay/Math/Mat4.hpp"
#include "okay/Math/Quat.hpp"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

class Scene;
class GameObject;

/// The static geometry a bake works from, copied out of the scene on the main
/// thread so the bake itself can run on a worker thread while the editor stays
/// responsive. Colliders become oriented boxes / spheres / capsules; heightmap
/// terrains are copied as heightfields. A Mesh Collider with triangles is copied as its world-space triangles
/// (wound so their front faces point out of the surface; see `Triangles`).
struct NavBakeInput {
    struct Box     { Vec3 center, half; Quat rot; int area = 0; };
    struct Sphere  { Vec3 center; float radius; int area = 0; };
    struct Capsule { Vec3 a, b; float radius; int area = 0; };
    struct Heightfield {
        Mat4 l2w, w2l;
        float size = 0.0f;           // local width / depth (centred on the object)
        int   dim = 0;               // heights per side
        std::vector<float> heights;  // row-major, dim * dim
        Vec3 aabbMin, aabbMax;       // world bounds
        int  area = 0;               // NavArea of the whole terrain
        float Height(int x, int z) const {
            if (x < 0 || z < 0 || x >= dim || z >= dim) return 0.0f;
            return heights[(std::size_t)z * dim + x];
        }
        float Sample(float lx, float lz) const;   // bilinear, local XZ
        Vec3  Normal(float lx, float lz) const;   // local, from the height gradient
    };
    /// A triangle surface (a Mesh Collider's triangles placed in the world). Each triangle's corners a, b, c are wound so
    /// that cross(b - a, c - a) points out of the surface (the engine's winding; a mirrored transform is undone). A
    /// surface whose front faces point down is a ceiling, not a floor: it blocks but is never walked on.
    /// `twoSided`: the facing is ignored and a triangle counts as walkable by how level it is, whichever way it
    /// is wound. Gather sets it for an open mesh (a plane, a terrain sheet, a floor imported with the wrong
    /// winding), which physics treats as solid from both sides; a closed mesh keeps its winding, so the inside
    /// face of a room's ceiling is never a floor.
    struct Triangles { std::vector<Vec3> corners; int area = 0; bool twoSided = false; };   // 3 corners per triangle, world space
    std::vector<Box> boxes;
    std::vector<Triangles> meshes;
    std::vector<Sphere> spheres;
    std::vector<Capsule> capsules;
    std::vector<Heightfield> terrains;
    /// The world box to bake; without it the union of the geometry (padded) is used.
    bool hasBounds = false;
    Vec3 boundsMin, boundsMax;
    /// Work limits for mesh triangles, so a hostile or huge mesh cannot hang or exhaust a bake. Gather copies at
    /// most `maxMeshTriangles` triangles in total (the rest are counted in `meshTrianglesDropped`); Bake stops
    /// rasterizing meshes after `maxMeshColumnVisits` (triangle, column) visits and reports it in NavBakeStats.
    std::size_t maxMeshTriangles = 2000000;
    std::size_t maxMeshColumnVisits = 12000000;
    std::size_t meshTrianglesDropped = 0;
    std::size_t closedTestTriangles = 0;   ///< triangles Gather ran the closed-mesh edge test on (a mesh over the cap is not tested)
};

struct NavBakeStats {
    int columns = 0;     ///< heightfield columns (cellSize cells over the bounds)
    int cells = 0;       ///< floor candidates found (several per column where floors stack)
    int walkable = 0;    ///< cells left after the slope / step / clearance / radius filters
    int polys = 0;
    int links = 0;
    int vertices = 0;
    double seconds = 0.0;
    int meshTrianglesDropped = 0;   ///< mesh triangles Gather left out because of the triangle cap
    int meshTrianglesSkipped = 0;   ///< mesh triangles skipped for non-finite or absurdly large (> 1e7) coordinates
    bool meshBudgetExceeded = false; ///< the column-visit budget ran out: some mesh triangles were not rasterized
    bool incomplete = false;        ///< any of the above: the result may be missing geometry (show `warning`)
    std::string warning;            ///< plain-language explanation when `incomplete`, else empty
};

/// Builds a NavMesh from NavBakeInput: rasterizes the geometry into a heightfield
/// of solid spans per column (so floors above floors and bridges work), keeps the
/// span tops with head room as floor cells, drops the ones steeper than the max
/// slope, connects neighbours within the step height, erodes the walkable area by
/// the agent radius, merges the cells into convex polygons (axis-aligned
/// rectangles that stay within the cell-height tolerance of a plane) and joins
/// neighbouring polygons with portals the funnel algorithm pulls paths through.
/// Each collider's area comes from the NavMeshModifier on its object (or an ancestor
/// that applies to children): the top of a span takes the area of the surface on top,
/// a NotWalkable top is never walkable, and only cells of one area merge into a polygon.
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
    /// The navigation area of this object's colliders: its NavMeshModifier, else the nearest
    /// ancestor's that applies to children, else Walkable.
    static int AreaFor(const GameObject* go);
    /// Is this object (or an ancestor) something that moves and so must not be baked?
    static bool IsMovingThing(const GameObject* go);
};

} // namespace okay
