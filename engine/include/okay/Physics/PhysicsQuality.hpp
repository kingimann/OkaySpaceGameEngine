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

} // namespace okay
