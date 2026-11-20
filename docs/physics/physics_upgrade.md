# Physics upgrade: what exists, what was missing, what changed

Owner request: "improve physics". This page lists what the engine already had (so nothing was built twice), the gaps found by running small
simulation scenes, and what this branch (`claude/physics-3`) changed. Earlier work: [physics_plan.md](physics_plan.md) (the Improved solver),
[physics.md](physics.md) (recipes), [feature map 6](../feature_map/06-physics.md).

How the numbers were got: small headless C++ programs (no window) built a scene, stepped `Physics3D::Step` by hand at 30, 50, 60 and 144 steps a second and
printed positions. The same scenes are now tests (`tests/test_physics_upgrade.cpp`, `test_physics_materials.cpp`, `test_physics_toys.cpp`,
`test_physics_examples.cpp`). "Before" is the code of `main` at commit 962ffbc0; "after" is this branch. Linux only; nothing was run on Windows or on a screen.

## Existing / Gap

| # | Area | Existing (before this branch) | Gap seen (measured) | Status now |
|---|---|---|---|---|
| 1 | Stacking | Improved solver: sequential impulses, warm start, 4-point box faces, group sleeping (`Physics3DImproved.inl`, `ContactSolver.hpp`) | 10 crates with the default 10 passes: at 50 steps/s lean 11.8 degrees and drift 1.6 m, at 30 steps/s fall over (drift 11.5 m); only stood with 20 passes | Fixed: stands and sleeps at 30, 50, 60, 144 (drift 0.04 m or less, tilt under 0.6 degrees). A tower of 20 still falls at 50 and 60 steps/s (documented limit) |
| 2 | Frame-rate independence | Fixed step 1/50 s with render interpolation | The same scene behaved differently at 30 and 60 steps/s (the 30 Hz tower fell, the 60 Hz one did not) | Fixed for Improved: a step longer than 1/45 s is solved in equal slices, so 30 equals 60 (the numbers are identical) |
| 3 | Tunnelling | Automatic sweep of fast bodies against static colliders; Continuous Collision tick for everything | A 0.1 m ball at 100 m/s went through a 0.05 m plate that is itself free to move, at 30, 60 and 144 Hz | Fixed: fast bodies are also swept against thin moving bodies |
| 4 | Contact callbacks | `OnCollisionEnter3D` etc. | A speed set inside the callback (a jump pad, a blast) was overwritten by the solve: the existing JumpPad never launched in Improved | Fixed: a speed set by a script between finding the contact and solving it is kept |
| 5 | Joints, mass ratio | Distance, Spring, Pin (fixed), Hinge (limits, motor), Slider; break force | A rope of eight 1 kg balls with a 50 kg ball on the end hung 0.7 m, 6.1 m and 10.1 m (30, 60, 144 Hz) of the true 11 m | Better: 10.6, 10.6, 10.9 m. Joint passes run in both directions and grow with the chain (capped so a scene of hundreds of joints does not slow down). Not exact |
| 6 | Breakable joints | `breakable`, `breakForce` | A distance joint that snapped first removed the body's speed along the rod, so the freed body stopped | Fixed: the break is checked before the joint holds the body back |
| 7 | Pendulum energy | Implicit spring, joint passes | 5 percent lost in 10 s at 30 and 60 Hz, 2.5 percent at 144 | Unchanged, now tested (above 93 percent kept) |
| 8 | Materials | Per-collider friction, bounciness, combine modes (`PhysicsMaterial.hpp`) | No named material, no separate still and sliding grip, no density, no rolling or damping per surface, no surface name for gameplay | Added: the Physics Material asset, 14 presets, Physics Surface component ([physics_materials.md](physics_materials.md)) |
| 9 | No-code physics | JumpPad (`NoCode.hpp`), Spinner (turns only the picture), Mover, DebrisEmitter, Destructible (voxel blocks) | No conveyor, force field, gravity zone, platform that carries riders, physical turntable, explosion, breakable crate | Added ([physics_toys.md](physics_toys.md)) |
| 10 | Debug view | View > Scene Overlays > Physics Debug: colliders, contacts, joints, sleeping bodies grey | No velocity arrows and no numbers | Added in the editor code: cyan velocity arrows and a stats line (bodies, awake, asleep, contacts, groups, swept). **Editor code compiled-not-run** |
| - | Dominoes | Box contacts | A line of very thin dominoes (0.1 x 1.0 m, 0.6 m apart) did not always topple: at 60 Hz only 1 of 20 fell | Not fixed. Dominoes 0.15 x 1.2 m topple 14 of 14 at 50 and 60 Hz (the example uses those) |

Things that already existed and were only checked by reading, not changed: raycast, spherecast, box and capsule casts, overlaps with layer masks, all-hits
(`Physics3D.hpp`); the layer collision matrix (`SetLayerCollision`); trigger enter, stay, exit; the character controller's slope limit, step offset, ground snap and moving
platform ride (`CharacterController3D.hpp`). **Not run:** the character on slopes and on a moving platform, pushing bodies with a mass-aware force, and a plain-words
layer-matrix editor: none of these were measured or changed in this branch. They are listed in `docs/status/UNFINISHED.md`.

## Numbers before and after

| Test (all at default settings, 10 s) | Before | After |
|---|---|---|
| Tower of 10 crates, 30 steps/s: top y (want 9.5), drift | 0.50, 11.5 m (fell) | 9.494, 0.04 m, asleep 10 of 10 |
| Tower of 10, 50 steps/s | 9.42, 1.6 m, tilt 11.8 deg, not asleep | 9.491, 0.06 m, tilt 0.55 deg, asleep 10 of 10 |
| Tower of 10, 60 steps/s | 9.47, 0.21 m, tilt 1.8 deg, not asleep | 9.494, 0.04 m, asleep 10 of 10 |
| Tower of 10, 144 steps/s | 9.497, 0.013 m, asleep | 9.499, 0.005 m, asleep |
| Tower of 10 built top first, 50 steps/s | (not measured) | 9.49 |
| 5 crates with a 30 kg top crate, 30 steps/s: top y (want 4.5) | 2.1 (fell) | 4.49 |
| Ball 100 m/s against a thin free plate (30, 60, 144 steps/s) | goes through at 30 and 144 | stops at the plate, plate pushed on |
| Rope, 8 x 1 kg + 50 kg at 30, 60, 144 steps/s: end y (true 11) | 0.74, 6.07, 10.14 | 10.61, 10.61, 10.93 |
| Fixed joint 50 kg to 1 kg, 30, 60, 144 steps/s: gap error | 0.000 | 0.000 |
| Elastic hit of 1 kg and 3 kg: momentum and energy | exact at all rates | exact at all rates (1.000 and 9.500) |
| Ball launched by a jump pad (speed set in a callback) | peak 1.5 m (no launch) | peak 8.2 m for 12 m/s |
| Crates resting 10 s (sleep off): jitter | 0 | 0 |
| NaN anywhere | none seen | none seen |

Existing physics tests were re-run against this branch's `Physics3D.cpp` (physics_deep_improved, solver, more, bugs, save, manifold; physics3d, joints, angular3d,
hinge3d, nan, ragdoll, vehicle, destructible, player_controller, character_feel, guided_vehicles, callback_removal, queries_ext, explode, script_physics): see the report.

## What each new file was written from

(Rule 19a: nothing is copied from another engine.)

| File or change | Written from |
|---|---|
| Sliced solve (`Physics3D::Step` cuts a step into equal slices of 1/45 s or less) | the plain idea of substepping: the same simulation with a shorter time step is the same physics with less error |
| Passes that grow with the group (`ScaleIterations`) and the bottom-up row order (`SortBottomUp`) | Gauss-Seidel iteration: information crosses one body per pass, so a group of n bodies needs about 2n passes; ordering rows along the direction information flows converges faster; union-find for the groups |
| Sweep against thin moving bodies (`SweepBodyBy`) | the existing sampled sweep, with the thin bodies' bounds added to the list |
| Speed set inside a callback kept (`Solver::Solve`) | comparing each body's speed with the one it had when its slot was made |
| Joint passes in both directions and growing with the chain | Gauss-Seidel again; the cap passes x joints under 6000 keeps big scenes cheap |
| `PhysicsMaterialAsset.hpp`, `PhysicsSurface.hpp` | textbook friction (still and sliding), restitution and density; see [physics_materials.md](physics_materials.md) |
| `PhysicsToys.hpp`, `PhysicsToyPresets.hpp`, `PhysicsToys.cpp` | this engine's overlap queries, rigid bodies and property list; plain vector maths |

## What is not done

* The layer collision matrix has no plain-words editor in this branch (the engine has the matrix; not checked in the editor).
* Character controller: no changes (slopes, steps, platforms, pushing bodies, ground friction from a material).
* Vehicles do not read the ground material.
* 2D: nothing changed (the 2D step still has no Improved solver; see `docs/status/UNFINISHED.md`).
* Windows, the editor on a screen and a real window were not run.
