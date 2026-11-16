#pragma once
// The physics quality setting shared by Physics2D and Physics3D, and the per-step numbers the Improved step reports.
#include <cstdint>

namespace okay {

/// How the step solves contacts.
///  Classic  - the original single-pass step: every touching pair is resolved once, in order. Old scenes keep it, so they
///             play exactly as they did.
///  Improved - the iterated contact solver: boxes touch along a whole face (up to four contact points), the solver
///             repeats several times and remembers last step's pushes (so stacks stand still and settle), real inertia
///             for each shape, friction that holds, bodies that rest together fall asleep together and wake when what
///             held them up moves away, fast bodies are swept so they cannot pass through thin walls, and rolling
///             resistance stops a rolling ball. New scenes start with Improved; a scene file records it as `physicsquality 1`.
enum class PhysicsQuality : int { Classic = 0, Improved = 1 };

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
