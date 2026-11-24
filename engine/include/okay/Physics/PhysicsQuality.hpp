#pragma once
// The physics quality setting shared by Physics2D and Physics3D, and the per-step numbers the Improved step reports.
#include <cstdint>

namespace okay {

/// The physics quality setting old scene files carry (`physicsquality`). There is ONE physics path now (3D: our own
/// engine core, docs/physics/physics_core.md; 2D: the 2D step), so the value changes nothing: it is read and written back
/// as it was, so old files load and save unchanged. Scenes that used Classic (the old default) or Improved now run the
/// engine core; their motion can differ slightly (tighter stacking, joints, fast objects).
enum class PhysicsQuality : int { Classic = 0, Improved = 1, Engine = 2 };

/// What the last Step did (Improved quality fills all of it; Classic fills the counts it knows). For tests, the debug view
/// and the profiler line.
struct PhysicsStepStats {
    int bodies = 0;          // dynamic bodies looked at
    int awake = 0;           // dynamic bodies awake at the end of the step
    int asleep = 0;          // dynamic bodies asleep at the end of the step
    int contacts = 0;        // contact points solved
    int manifolds = 0;       // touching pairs solved
    int islands = 0;         // groups of touching bodies
    int sweeps = 0;          // fast bodies swept by continuous collision
    int warmStarted = 0;     // contact points that started from last step's push
};

/// What the 3D scene queries (rays, overlaps, casts, closest point) did since Physics3D::ResetQueryStats. For tests and
/// the profiler: it shows how much of the scene a query had to look at.
struct PhysicsQueryStats {
    std::uint64_t queries = 0;        // query calls
    std::uint64_t considered = 0;     // colliders a query looked at one by one
    std::uint64_t shapeTests = 0;     // colliders that went on to an exact shape test
    std::uint64_t boundsUpdates = 0;  // collider boxes worked out again for the broad phase
    std::uint64_t gridBuilds = 0;     // height grids built for ClosestPoint (terrain, heightfield)
};

} // namespace okay
