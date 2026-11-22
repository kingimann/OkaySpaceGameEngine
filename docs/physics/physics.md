# Physics

> **What this is:** Everything about physics in one place: quality modes, materials, joints, the physics toys, ragdolls, the audit and plan, and the upgrade record.
>
> **Status:** combined on 2026-11-20 from 6 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
>
> **Last checked:** 2026-11-20, paths and links against main.

## Contents

- [Physics: quality, materials, joints, debris, hinges, mesh colliders, 2D casts, and step rate](#physics-quality-materials-joints-debris-hinges-mesh-colliders-2d-casts-and-step-rate) (was `docs/physics.md`)
- [Physics upgrade: what exists, what was missing, what changed](#physics-upgrade-what-exists-what-was-missing-what-changed) (was `docs/physics_upgrade.md`)
- [Physics Material: how a surface feels](#physics-material-how-a-surface-feels) (was `docs/physics_materials.md`)
- [Physics toys: ready-made physics, no code](#physics-toys-ready-made-physics-no-code) (was `docs/physics_toys.md`)
- [Ragdolls and hit reactions](#ragdolls-and-hit-reactions) (was `docs/ragdolls.md`)
- [Physics audit and plan](#physics-audit-and-plan) (was `docs/physics_plan.md`)
- [The Engine quality: our own physics engine](#the-engine-quality-our-own-physics-engine) (details in `physics_core.md`)

## Physics: quality, materials, joints, debris, hinges, mesh colliders, 2D casts, and step rate

*Was `docs/physics.md` until 2026-11-20.*

Short recipes for the physics pieces people ask for first. The full map of what
exists is in [feature map 6](../feature_map/06-physics.md).

### Physics quality: Classic and Improved

A scene has a **Physics quality** (Project Settings > Physics > "Physics quality of the open scene"). It is saved in the scene file.

- **Classic** is the step every older scene was made with: each touching pair is solved once per step, in order. An old scene file has no
  quality record and loads as Classic, so it plays as it always did (apart from the bug fixes below).
- **Improved** solves contacts several times per step and remembers last step's pushes, so a tower of crates stands, settles and goes to
  sleep, a box on a gentle slope stays put, fast bodies stop at thin walls without ticking Continuous Collision, balls roll to a stop, and
  every shape turns the way its size says. New scenes made in the editor, `examples/physics/physics_playground.okayscene` and a scene you
  make with `SceneSerializer` after setting `scene.physics3D().quality = PhysicsQuality::Improved` use it.
- In the same place: **Solver iterations** (4 to 30, default 10; more = taller stable stacks, more time) and **Catch fast bodies
  automatically** (a body that moves farther in one step than half its size is swept against walls and floors).
- A script can read and set `scene.physics3D().quality` (C++ scripts) and the iterations.

What Improved does, in plain words:

| You notice | What Improved does |
|---|---|
| Crates fall over or sink into each other | A box on a box touches along its whole face (up to 4 contact points); the solver repeats 10 times and starts each step from the last step's push; the push on a body that is much heavier than the one under it is limited so a heavy crate does not sink through a light one. |
| Resting crates buzz and never sleep | Bodies that touch go to sleep together once every body in the group has been still for half a second; a sleeping body wakes when what held it up moves, is switched off or is removed (also fixed in Classic). |
| Bullets go through walls | Fast bodies are swept along their path against static colliders. Tick **Continuous Collision** on a body to sweep it against everything. |
| A ball never stops rolling | **Rolling Resistance** on the Rigidbody 3D (default 0.03). 0 keeps the old roll-on. |
| A plank spins as if it were a cube | A real inertia tensor per shape (box, sphere, capsule, cylinder). |
| A table made of a top and four legs falls through | Tick **Use Child Colliders** (Compound Children) on the Rigidbody 3D: the colliders on its child objects (children that have no body of their own) are part of the body, and mass, centre of mass and inertia are worked out for the whole shape. |
| A collider's offset ignores the object's turn and scale | In Improved the offset turns and scales with the object. |

Written from: sequential impulses (projected Gauss-Seidel on the contact constraints, impulses accumulated and clamped, warm starting),
Sutherland-Hodgman clipping of the incident box face against the reference face (C. Ericson, Real-Time Collision Detection, 2005, chapter 5),
inertia of simple solids from any mechanics textbook, the parallel-axis theorem and union-find for sleeping groups. No code was taken
from another engine. Files: `Physics/ContactSolver.hpp`, `Physics/BoxManifold.hpp`, `engine/src/Physics/Physics3DImproved.inl`.

**2D:** the quality setting is saved for 2D too, but the 2D step has no Improved solver yet (see `docs/status/UNFINISHED.md`): 2D scenes play
exactly as before whichever quality is chosen.

#### Bug fixes that apply to both qualities

- A box on a ramp now slides (the floor rescue in the 3D step measured the body's bounding box under its centre, which on a slope is always
  "below the floor", and lifted the body every step: 0.23 m in 2 s instead of 5.8 m).
- A sleeping body whose support moves, is disabled, switched off or removed wakes up (it used to float for ever), and a box asleep on a
  moving platform is carried.
- A ray that starts inside a box gets a unit normal (it was zero length).
- A stiff spring joint no longer blows up (stiffness 20000 on a 1 kg ball at 50 steps a second ended as NaN): too stiff for a plain force at
  the step, the spring is solved implicitly (softness gamma = 1/(h(c+hk)), bias beta = hk/(c+hk)).
- Joints remember the partner they found by name until the scene's objects change; 300 joints no longer search the scene every step.
- `OnCollisionExit` / `OnTriggerExit` arrive in the order the scene lists the colliders, the same in every run (they used to arrive in
  memory-address order).

### Physics materials

Every collider has its own **Physics Material** (Inspector, collider > Physics Material): **Own Friction** and **Own Bounciness**, and how each
is mixed with the surface it touches (Default, Average, Minimum, Multiply, Maximum). With the boxes off a collider uses its Rigidbody's numbers,
exactly as before. This is how a floor with no Rigidbody is made icy (friction 0) or bouncy (bounciness 0.9). Default mixing: friction is the
geometric mean of the two, bounciness the larger. When two colliders ask for different rules the stronger wins (Average < Minimum < Multiply
< Maximum). Used by the Improved solver; the Classic step reads the Rigidbody numbers only.

### Joints: slider, ropes, debug view

- **Slider (prismatic)** is a new Joint 3D mode: the body moves only along **Axis** (a world direction taken when Play starts), cannot turn,
  can have **Min Travel / Max Travel** limits in metres from where it started and a motor (**Motor Speed** in m/s, **Max Motor Force**). A
  drawer, a lift, a piston. Scene files store it as mode 4.
- In Improved the constraint joints (distance, pin, hinge, slider) are solved as many times as the contact solver runs, so a rope of
  8 balls hangs 8.03 m long instead of 10.9 m. A 50 kg weight on 1 kg links still stretches (13 m instead of 64 m for a rope that should be
  8 m): a long chain with a big mass ratio needs a better solver than repeated passes (see `docs/status/UNFINISHED.md`). Springs are forces and are applied once.
- The physics debug view (View > Scene Overlays > Physics Debug > Show physics (Scene + Game); Scene and Game view) now draws **joints** (orange line, red when broken); sleeping bodies
  were already grey.

### Debris for breaking and digging games (`DebrisPool`)

`#include <Okay.hpp>` (header `okay/Physics/DebrisPool.hpp`). Many small rigid chunks that fly, land, settle and fade, with a hard cap:

```cpp
DebrisPool debris(scene);                        // once; DebrisSettings: maxActive 200, lifetime 12 s, settledLife 4 s, ...
debris.Spawn(position, {0.2f, 0.2f, 0.2f}, velocity, Color::FromBytes(120, 90, 60));   // as often as you like
debris.Update(dt);                               // once per frame
```

- Built on the engine's `ObjectPool`: a finished chunk is switched off and reused, nothing is made or freed per chunk.
- The cap is never passed: spawning past `maxActive` recycles the oldest chunk.
- A chunk lives `lifetime` seconds, or `settledLife` seconds after it first goes to sleep, then shrinks away over `fadeTime`.
- Chunks use collision layer 30 and by default do not collide with each other (500 chunks cost 500 floor contacts, not 125 000 pairs);
  set `collideWithEachOther` for chunks that must stack. This changes the scene's layer rule for that layer.
- Use it with the Improved quality: a settled chunk then sleeps almost for free.
- Measured (test `physics_deep_more`, 120 chunks spawned into a cap of 50): 50 alive, 70 recycled, all 50 asleep 3 s after the last spawn,
  all expired 10 s later.

### A door or a lever that stops

Add **Joint 3D** (Add Component > Physics) and set **Mode** to **Hinge**. The
body spins around the hinge and, until you say otherwise, it can spin the whole
way around.

Turn on **Use Limits**. **Min Angle** and **Max Angle** are degrees from the
pose the joint had when play started. A door set to 0 and 90 opens a quarter
turn and stops. A motor keeps pushing, but it cannot grind past the stop: the
outward spin is dropped when the hinge is against the limit.

Leave **Use Limits** off and the hinge behaves as it always did. An older scene
file that has no limit fields still loads, with limits off.

### A mesh that can move

A **Mesh Collider 3D** on its own, or on a kinematic body, collides as the
mesh's triangles. Bowls, holes and other concave shapes stay exact.

Put a **dynamic Rigidbody 3D** on the same object and the collider switches to
the convex hull of the mesh, so the body can move and still bump into things.
A flat mesh is given a little thickness so it still has a shape. Concave gaps
are filled in: a ray through a notch in a moving mesh can miss, because the
hull covers the notch. The Inspector and the collider gizmo say which shape is
in use (triangles, or the hull).

A body that lands on a solid mesh raises **On Collision Enter**, **On Collision
Stay** and **On Collision Exit**, including the exit when it leaves.

The navigation bake reads a mesh collider's real triangles (see `docs/gameplay/navigation.md`).

### Sweeping a box or a capsule in 2D

Side-view games can ask what a flat box or a capsule would hit if it moved.

| Call | What it answers |
|---|---|
| `box_cast(cx, cy, hx, hy, dx, dy[, dist [, layers [, ignore_triggers [, angle]]]])` | The first thing a box of half-size `hx`, `hy` hits, moving from `cx`, `cy` along `dx`, `dy`. `angle` is degrees. |
| `capsule_cast(ax, ay, bx, by, r, dx, dy[, dist])` | The same for a capsule from `ax`, `ay` to `bx`, `by` with radius `r`. |
| `overlap_box` / `overlap_capsule` | The names of whatever is already inside that shape, without sweeping. |

Triggers are skipped unless you ask. A hit fills the same `ray_*` details as
`raycast`: `ray_hit`, `ray_object`, `ray_x`, `ray_y`, `ray_nx`, `ray_ny`,
`ray_dist`. The normal points back toward the shape.

`add_explosion_force(cx, cy, force, radius[, up])` on a **Rigidbody 2D** shoves
it away from a point. The push fades in a straight line and is nothing at
`radius`. `up` adds an upward bias. Kinematic and static bodies are left alone,
the same as a 3D body.

In a visual script, the Physics nodes are **Box Cast**, **Capsule Cast**,
**Overlap Rectangle**, **Overlap Capsule** and **Add Explosion Force**. Leave a
pin unwired and it keeps its default; the node tooltip says what that default
does.

`examples/physics2d/casts_and_blast.okayscene` is a small Play scene: the near
crate flies, the far crate does not, and a lookout's box cast names the wall.

### Physics step rate and parented bodies

Physics can run at its own rate, and a rigidbody can live under another object.

#### Steps per second

Project Settings > Physics has **Steps per second** and **Smooth the motion**.

- **50** is the rate Play and a built game already used. A project saved before this setting existed stays at 50 with smoothing on, so an old scene plays the same.
- A bare scene that nobody turned the fixed step on for still takes **one physics step per frame**. That is the old default for tools and tests.
- The same drop lands in the same place at a low step rate and a high one, within a small tolerance. A slower rate is a slightly coarser step, not a different game.
- **Smooth the motion** draws each body part way between its last two physics steps. At 144 frames a second and 10 physics steps a second the body still moves every frame, instead of jumping once every several frames. A body you move by hand still goes there at once, with no blend back. A script that only turns a body (sets its rotation each frame, like the third-person controller) does not count as moving it: the position is still smoothed and your rotation is drawn as you set it. Rotation made by physics itself (spin) is still blended between steps. Any change to the rotation counts as game-set, however small, so a slow smoothed turn (a fraction of a degree per frame) arrives at its target. Checked with the real third-person controller (test `controller_interpolation`: a rigged character walking in a tight circle, 50 physics steps a second, drawn at 30, 60 and 144 frames a second): the drawn position changes by an even amount each frame, to within 2 mm. What is left is the same as when walking straight: about 1.3 mm of unevenness at 30 frames a second (cause not looked into) and nothing measurable at 60 and 144. The orbit camera, which eases toward the mouse, stays within 1 mm of the straight-walk figure. Not covered: other rigs, other turn rates, root motion from animation clips, and a real screen. Limit: if game code sets the rotation of a body that physics also spins, the game's rotation wins that frame: it is drawn as the steps of that frame left it, not blended between steps.
- While the game runs, a script can read and set the live rate. Text scripts: `physics_steps()`, `set_physics_steps(n)`, `physics_smooth()`, `set_physics_smooth(on)`. The same calls are visual-script nodes `Fn.physics_steps`, `Fn.set_physics_steps`, `Fn.physics_smooth` and `Fn.set_physics_smooth`. Typed scripts: `Time.physicsSteps` and `Time.smoothPhysics`. `n` is steps per second, from 1 to 500. `0` goes back to one step per frame.
- A build writes the project rate into `game.okayconfig` (`physics_hz`, `physics_smooth`). A build from before those lines keeps 50 and smoothing on. A dedicated server still sends the real physics pose, not the smoothed one.

#### A body under a parent

The solvers move a body in **world** space, whether or not it has a parent.

- **Moving the parent** carries the body with it. The body's own speed, gravity and collisions are applied on top, in the world.
- **Turning the parent** carries the body around with it. Gravity still pulls straight down. A turned parent does not tilt gravity, and a scaled parent does not stretch a 1 metre-per-second step into 2.
- **Unparenting** keeps the pose you see and adds the speed the parent was giving the body, including the swing from a parent that is turning. After that it is an ordinary free body. A static body is not given a speed.
- An un-parented body is unchanged: its local pose is its world pose, and the step writes that the way it always did.
- A hinge on a parented body still measures its angle in the parent's space. Put that body at the top of the hierarchy when the hinge must spin in the world.

The Rigidbody 3D inspector says this when the object has a parent.

## Physics upgrade: what exists, what was missing, what changed

*Was `docs/physics_upgrade.md` until 2026-11-20.*

Owner request: "improve physics". This page lists what the engine already had (so nothing was built twice), the gaps found by running small
simulation scenes, and what this branch (`claude/physics-3`) changed. Earlier work: [physics_plan.md](physics.md#physics-audit-and-plan) (the Improved solver),
[physics.md](physics.md#physics-quality-materials-joints-debris-hinges-mesh-colliders-2d-casts-and-step-rate) (recipes), [feature map 6](../feature_map/06-physics.md).

How the numbers were got: small headless C++ programs (no window) built a scene, stepped `Physics3D::Step` by hand at 30, 50, 60 and 144 steps a second and
printed positions. The same scenes are now tests (`tests/test_physics_upgrade.cpp`, `test_physics_materials.cpp`, `test_physics_toys.cpp`,
`test_physics_examples.cpp`). "Before" is the code of `main` at commit 962ffbc0; "after" is this branch. Linux only; nothing was run on Windows or on a screen.

### Existing / Gap

| # | Area | Existing (before this branch) | Gap seen (measured) | Status now |
|---|---|---|---|---|
| 1 | Stacking | Improved solver: sequential impulses, warm start, 4-point box faces, group sleeping (`Physics3DImproved.inl`, `ContactSolver.hpp`) | 10 crates with the default 10 passes: at 50 steps/s lean 11.8 degrees and drift 1.6 m, at 30 steps/s fall over (drift 11.5 m); only stood with 20 passes | Fixed: stands and sleeps at 30, 50, 60, 144 (drift 0.04 m or less, tilt under 0.6 degrees). A tower of 20 still falls at 50 and 60 steps/s (documented limit) |
| 2 | Frame-rate independence | Fixed step 1/50 s with render interpolation | The same scene behaved differently at 30 and 60 steps/s (the 30 Hz tower fell, the 60 Hz one did not) | Fixed for Improved: a step longer than 1/45 s is solved in equal slices, so 30 equals 60 (the numbers are identical) |
| 3 | Tunnelling | Automatic sweep of fast bodies against static colliders; Continuous Collision tick for everything | A 0.1 m ball at 100 m/s went through a 0.05 m plate that is itself free to move, at 30, 60 and 144 Hz | Fixed: fast bodies are also swept against thin moving bodies |
| 4 | Contact callbacks | `OnCollisionEnter3D` etc. | A speed set inside the callback (a jump pad, a blast) was overwritten by the solve: the existing JumpPad never launched in Improved | Fixed: a speed set by a script between finding the contact and solving it is kept |
| 5 | Joints, mass ratio | Distance, Spring, Pin (fixed), Hinge (limits, motor), Slider; break force | A rope of eight 1 kg balls with a 50 kg ball on the end hung 0.7 m, 6.1 m and 10.1 m (30, 60, 144 Hz) of the true 11 m | Better: 10.6, 10.6, 10.9 m. Joint passes run in both directions and grow with the chain (capped so a scene of hundreds of joints does not slow down). Not exact |
| 6 | Breakable joints | `breakable`, `breakForce` | A distance joint that snapped first removed the body's speed along the rod, so the freed body stopped | Fixed: the break is checked before the joint holds the body back |
| 7 | Pendulum energy | Implicit spring, joint passes | 5 percent lost in 10 s at 30 and 60 Hz, 2.5 percent at 144 | Unchanged, now tested (above 93 percent kept) |
| 8 | Materials | Per-collider friction, bounciness, combine modes (`PhysicsMaterial.hpp`) | No named material, no separate still and sliding grip, no density, no rolling or damping per surface, no surface name for gameplay | Added: the Physics Material asset, 14 presets, Physics Surface component ([physics_materials.md](physics.md#physics-material-how-a-surface-feels)) |
| 9 | No-code physics | JumpPad (`NoCode.hpp`), Spinner (turns only the picture), Mover, DebrisEmitter, Destructible (voxel blocks) | No conveyor, force field, gravity zone, platform that carries riders, physical turntable, explosion, breakable crate | Added ([physics_toys.md](physics.md#physics-toys-ready-made-physics-no-code)) |
| 10 | Debug view | View > Scene Overlays > Physics Debug: colliders, contacts, joints, sleeping bodies grey | No velocity arrows and no numbers | Added in the editor code: cyan velocity arrows and a stats line (bodies, awake, asleep, contacts, groups, swept). **Editor code compiled-not-run** |
| - | Dominoes | Box contacts | A line of very thin dominoes (0.1 x 1.0 m, 0.6 m apart) did not always topple: at 60 Hz only 1 of 20 fell | Not fixed. Dominoes 0.15 x 1.2 m topple 14 of 14 at 50 and 60 Hz (the example uses those) |

Things that already existed and were only checked by reading, not changed: raycast, spherecast, box and capsule casts, overlaps with layer masks, all-hits
(`Physics3D.hpp`); the layer collision matrix (`SetLayerCollision`); trigger enter, stay, exit; the character controller's slope limit, step offset, ground snap and moving
platform ride (`CharacterController3D.hpp`). **Not run:** the character on slopes and on a moving platform, pushing bodies with a mass-aware force, and a plain-words
layer-matrix editor: none of these were measured or changed in this branch. They are listed in `docs/status/UNFINISHED.md`.

### Numbers before and after

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

### What each new file was written from

(Rule 19a: nothing is copied from another engine.)

| File or change | Written from |
|---|---|
| Sliced solve (`Physics3D::Step` cuts a step into equal slices of 1/45 s or less) | the plain idea of substepping: the same simulation with a shorter time step is the same physics with less error |
| Passes that grow with the group (`ScaleIterations`) and the bottom-up row order (`SortBottomUp`) | Gauss-Seidel iteration: information crosses one body per pass, so a group of n bodies needs about 2n passes; ordering rows along the direction information flows converges faster; union-find for the groups |
| Sweep against thin moving bodies (`SweepBodyBy`) | the existing sampled sweep, with the thin bodies' bounds added to the list |
| Speed set inside a callback kept (`Solver::Solve`) | comparing each body's speed with the one it had when its slot was made |
| Joint passes in both directions and growing with the chain | Gauss-Seidel again; the cap passes x joints under 6000 keeps big scenes cheap |
| `PhysicsMaterialAsset.hpp`, `PhysicsSurface.hpp` | textbook friction (still and sliding), restitution and density; see [physics_materials.md](physics.md#physics-material-how-a-surface-feels) |
| `PhysicsToys.hpp`, `PhysicsToyPresets.hpp`, `PhysicsToys.cpp` | this engine's overlap queries, rigid bodies and property list; plain vector maths |

### What is not done

* The layer collision matrix has no plain-words editor in this branch (the engine has the matrix; not checked in the editor).
* Character controller: no changes (slopes, steps, platforms, pushing bodies, ground friction from a material).
* Vehicles do not read the ground material.
* 2D: nothing changed (the 2D step still has no Improved solver; see `docs/status/UNFINISHED.md`).
* Windows, the editor on a screen and a real window were not run.

## Physics Material: how a surface feels

*Was `docs/physics_materials.md` until 2026-11-20.*

A **Physics Material** is a named bundle that says how a surface feels. Give an object one (Add Component > Physics 3D > **Physics Surface**, then type the
material name in **Material**) and every collider on it, and on its children if you tick **Include Children**, feels that way.

### The fourteen built-in materials

They are always there, no file needed. Numbers: grip while still / grip while sliding / bounciness / density in kg per cubic metre.

| Name | Still | Sliding | Bounce | Density | Notes |
|---|---|---|---|---|---|
| Default | 0.60 | 0.50 | 0.00 | 1000 | what an object with no material uses |
| Ice | 0.05 | 0.02 | 0.05 | 917 | mixing: the smaller grip wins, so anything slides on it |
| Rubber | 1.20 | 1.00 | 0.60 | 1100 | mixing: the larger wins |
| Bouncy Ball | 0.80 | 0.60 | 0.92 | 1100 | |
| Metal | 0.50 | 0.40 | 0.20 | 7800 | |
| Wood | 0.55 | 0.45 | 0.30 | 600 | |
| Stone | 0.70 | 0.60 | 0.10 | 2600 | |
| Glass | 0.45 | 0.35 | 0.40 | 2500 | |
| Mud | 1.00 | 0.90 | 0.00 | 1700 | slows what touches it (damping 1.5, spin damping 3, rolling 0.3) |
| Sand | 0.80 | 0.70 | 0.00 | 1600 | damping 0.8 |
| Sticky | 2.50 | 2.00 | 0.00 | 1200 | grip on slopes x2 |
| Slippery | 0.02 | 0.01 | 0.00 | 1000 | |
| Trampoline | 0.60 | 0.50 | 0.97 | 400 | the larger bounce wins |
| Cloth | 0.80 | 0.70 | 0.02 | 300 | light damping |

Each also names an impact sound, a slide sound and a hit effect (`impact_wood`, `slide_ice`, `fx_sparks`...). They are names only: a script or an audio hook reads them; the engine
does not ship those sounds.

### Your own material: a `.okayphysmat` file

Plain text. The first line is the format name and version; every other line is `key value` (text values run to the end of the line). Unknown keys are skipped, a bad
number keeps the default, a newer format version is refused.

```
okayphysmat 1
name Gel
surface Gel
staticFriction 1.5
dynamicFriction 0.7
bounciness 0.35
frictionCombine Maximum
bounceCombine Minimum
density 1100
rollingFriction 0.2
linearDamping 0.5
angularDamping 1.5
slopeGrip 1.7
impactSound splat
slideSound squish
hitEffect fx_goo
```

Load it with `PhysicsMaterials::LoadFile(path)` or a whole folder with `PhysicsMaterials::ScanFolder(dir)`. A Physics Surface whose material name is not a preset scans the project's asset folder once by itself, in the editor and in the player, so a `.okayphysmat` file just works by name. A project material with the same name as a preset replaces it.

### What the physics does with it

* Sliding grip, bounciness and the combine modes go into the collider's existing fields (so the Classic step also feels them).
* Improved quality also uses: **still grip** (below 0.12 m/s sliding the still grip applies, so a crate stays on a slope that it would slide down once moving), rolling friction,
  damping and slope grip.
* **Mass from density**: tick it and the body's mass is density x the volume of its colliders (a 1 m cube of Metal is 7800 kg).
* A contact tells scripts the two materials: `Collision3D::surface` (the other side) and `ownSurface`.
* `PhysicsSurface::SetMaterial("Ice")`, or `AssignPhysicsMaterial(object, "Ice")`, changes it while the game runs. The property `PhysicsSurface.material` works from scripts too.
* Old scenes load unchanged: an object with no Physics Surface keeps its collider's own friction and bounciness exactly as before.

Written from textbook contact friction (a still and a sliding coefficient, still at least sliding), restitution and density; see [physics_upgrade.md](physics.md#physics-upgrade-what-exists-what-was-missing-what-changed).

### Not done

* The Inspector part (dropdown of presets and project materials, "+ New material", live sliders inside the collider's Physics Material section) is editor source that was **compiled-not-run**. Edited project materials are saved to `Materials/` under the asset folder.
* The character controller (`CharacterController3D`) and `VehicleController` now scale acceleration, braking and sideways grip by the ground material (`Physics/GroundGrip.hpp`; ice 0.04, rubber 2). Tested for the character (slides 7.5 m on ice, 0.24 m on plain ground); the vehicle change is not tested by a driving test.
* Other controllers (first/third person players) are not changed.
* Terrain / voxel material defaults.
* Visual Script nodes ("Make this object icy"): use the property `PhysicsSurface.material`.

## Physics toys: ready-made physics, no code

*Was `docs/physics_toys.md` until 2026-11-20.*

Add one from **Add Component > Physics 3D**, set a few numbers, press Play. Or open `examples\physics_toys\physics_toys.okayscene` (ten stations, each labelled).

| Toy | What it does | Main numbers |
|---|---|---|
| Conveyor Belt | carries bodies standing on its top (needs a Box Collider 3D on the same object) | Direction, Speed (negative = backwards), Grip, Reach |
| Force Field | pulls, pushes, a gravity well (inverse square) or a whirlpool, inside a radius; on and off | Mode, Falloff, Radius, Strength (acceleration), Core Radius, Active |
| Gravity Zone | a box of space with its own gravity (floating, moon) | Size, Gravity |
| Turntable | a physical plate turning at a steady speed that carries things (the older Spinner only turns the picture) | Axis, Degrees Per Second, Ramp Time |
| Moving Platform | glides through up to four points, pauses, eases, riders go along | Mode (Ping Pong, Loop), Point Count, Offsets, Speed, Pause, Easing |
| Explosion | a radial impulse (light things fly further), upward kick, a damage hook; by timer (`On Start`) or `Trigger()` from a script; `Explosion::At(scene, pos, force, radius)` for a one-off | Force, Radius, Upward Bias, Damage, On Start, Delay |
| Breakable | a crate that breaks into pieces x pieces x pieces smaller crates when hit harder than Break Speed; pieces vanish after Piece Life | Break Speed, Pieces, Piece Life, Burst Speed |

The Jump Pad already existed (Add Component > Gameplay > Jump Pad).

Code presets (`Components/PhysicsToyPresets.hpp`): `ConveyorBeltPreset`, `JumpPadPreset`, `ForceFieldPreset`, `GravityZonePreset`, `TurntablePreset`, `MovingPlatformPreset`, `BombPreset`,
`BreakableCratePreset`, `SurfacePatchPreset` (Ice, Sticky, Trampoline, Mud...), `PendulumPreset`, `RopePreset`, `WreckingBallPreset`, `DominoPackPreset`, `DoorPreset`. The editor menu
GameObject > 3D Object > Physics Toys calls them (editor code compiled-not-run).

All of them are saved in scenes and listed in the Inspector with plain tooltips (generic property list), work through property paths from scripts (for example `ForceField.active`), and
look at bodies with a physics query each fixed step. They do not touch water, buoyancy or wind.

Written from: this engine's `Physics3D::OverlapBox / OverlapSphere`, `Rigidbody3D` and `Scene`; plain vector maths.

### Not done

One-Way Platform (3D), Rope/Chain as a component (a preset builds one), Vehicle Wheel Collider and Car preset (a vehicle controller already exists), Ragdoll on hit, Glue and Ice surfaces as separate
components (use a Physics Surface material), Visual Script nodes and OkayScript built-ins for the toys.

## Ragdolls and hit reactions

*Was `docs/ragdolls.md` until 2026-11-20.*

Add **Ragdoll** (Add Component → Ragdoll (death + hit reactions)) to a humanoid: the blocky
Character or an imported rig that maps as a humanoid. Nothing else is needed.

- **Death**: when the body's HealthStat dies (or a script calls `die()`), the
  body goes limp and falls away from where the hit came from. It keeps the
  motion it had, so a running body tumbles forward. A revive stands it back up.
- **Hits**: a hit the body survives shoves the struck part, which springs back
  to the animation over **Recover Time**.

While limp, the body's animators, IK and movement controllers are switched off
and the root Rigidbody3D is stopped. They come back on when the ragdoll ends.

### How it works

Each of the 15 humanoid bones is a particle, plus a tip past the head, hands
and feet. Bone lengths are held by distance constraints, the torso is braced
into a box, and elbows, knees and the spine can bend but not fold flat. The
particles collide with the scene's 3D colliders and terrain by ray, ignoring
the body's own colliders. The simulation runs at 60 Hz after animation and IK,
and poses the bones from the particles.

### Settings

| Setting | What it does |
|---|---|
| Ragdoll On Death | Go limp when the body dies |
| Death Push | Speed away from the killing hit (m/s) |
| Hit Reactions | Flinch on hits the body survives |
| Hit Push | Speed given to the struck part (m/s) |
| Recover Time | Seconds to spring back to the animation |
| Gravity, Damping, Friction | How the body falls, slows and slides |
| Skin Radius | Gap kept between the particles and surfaces |
| Iterations | Constraint passes per step (stiffer joints with more) |
| Max Speed | Cap on particle speed |

### Scripts

```
ragdoll()                 // go limp
ragdoll(0)                // stand back up
ragdoll_push(0, 2, 5)     // shove the chest (m/s)
ragdoll_push(0, 2, 5, x, y, z)   // shove the part nearest a world point
is_ragdoll()              // 1 while limp
```

Damage with a source (`HealthStat::Damage(amount, from)`) aims the death fall
and the flinch. In C++: `Ragdoll::Activate()`, `Deactivate()`,
`Hit(point, velocity)`, `IsActive()`, `IsSettled()`.

## Physics audit and plan

*Was `docs/physics_plan.md` until 2026-11-20.*

This page is the physics audit the owner asked for ("look into it deeper... fix bugs, fps problem and improve") and the plan
that came out of it. It says what the physics does today, what it does badly, how that was measured, what is fixed, and what is
left. It is written in plain words. Where a number is given, the way it was got is given too.

Contents: [how this was measured](#how-this-was-measured) - [what a game maker notices first](#what-a-game-maker-notices-first)
- [what already works](#what-already-works) - [frame rate findings](#frame-rate-findings) - [the plan](#the-plan) -
[what each new file was written from](#what-each-new-file-was-written-from-1) - [results](#results) - [what is not done](#what-is-not-done-1)

### How this was measured

- Small headless programs (no window) built a scene, ran the physics step by hand at a chosen rate and printed numbers. They link
  the real engine library built in Release mode (`-O3`, the same flags as a release). They live in the scratch folder of the
  session, not in the repository. Each finding that is **measured** below was seen in one of those runs on the unchanged
  `main` code (commit `24a7b0b7`).
- A finding that is only **read** was found by reading the code and has not been run. Those are marked "read".
- The machine is shared (4 cores, other builds running). Timings are medians of 100 steps with the load average noted
  (1.8 to 3.6 while measuring); they are good to about 10 percent, so only big differences mean something.
- Rates used: 50 steps per second (what Play and a built game use), and 30 and 144 to look for frame-rate dependence.
- Every fix below starts with a test that fails on the old code (tests named `physics_deep_*`), and every behaviour change has a
  switch (see "the plan").

### What a game maker notices first

Ranked by how soon a person making a game would run into it.

#### 1. Stacks of crates fall over or sink (3D and 2D) - measured

A column of 1 m crates, mass 1, with rotation allowed (Freeze Rotation off), standing on a floor, 10 seconds at 50 Hz:

| Stack | What happened |
|---|---|
| 3 crates, perfectly lined up | tilts 25 degrees in the first second, lies flat on the floor by 3 seconds, never sleeps |
| 5 crates | same, ends upside down (tilt 180 degrees) |
| 10 crates | same, ends 4 m away from where it started |
| 3 crates with 100 kg at the bottom, 10 kg, 1 kg on top | still falls over |
| 5 crates at 144 steps a second or at 30 | still falls over |

With rotation frozen (the default), crates do not fall over but they **sink into each other**: the top of a 5-crate stack sits
16 cm too low and 8 cm of each pair overlaps; a 10-crate stack ends 3.7 m too low (crates pass through each other). 2D is the
same: a 3-box stack falls over, a frozen 5-stack sinks 0.44 m, a frozen 10-stack sinks 4.4 m.

Cause (read, and consistent with the numbers): every touching pair is solved exactly once per step, one after the other, with no
repeats and no memory of the last step, and a box that sits on another box is given one single contact point (one corner), so
the push through that corner twists it. The inertia used is also wrong (finding 8).

#### 2. Resting boxes buzz and never go to sleep, so the frame cost stays high - measured

- One box with free rotation resting on the floor wobbles 0.98 cm up and down every step (y 0.5000 to 0.5097) and tilts up to
  0.8 degrees. With rotation frozen it does not move at all.
- A box dropped flat with free rotation **never fell asleep** in 10 seconds. A stack of 2 with frozen rotation never slept either.
- 500 resting boxes with free rotation cost **3.6 ms per step with 0 asleep**; the same boxes with frozen rotation sleep and cost
  0.9 ms. So the "sleeping saves CPU" feature does not switch on for the bodies that need it most.

#### 3. Bodies stick on slopes (3D only) - measured, cause being proven by a test

A box on a 30 degree ramp with friction 0.1 (combined 0.245) should slide about 5.8 m in 2 seconds (acceleration
g(sin 30 - mu cos 30) = 2.8 m/s2). It slid **0.23 m** (rotation frozen) and 0.32 m (rotation free). The same ramp in 2D slides
correctly (12.9 m in 2 s including the run-up). Cause (read): the 3D step has an extra rescue for bodies that fall through a thin
floor (`CatchTunneledFloor`); it measures the body's lowest corner of its bounding box against the floor under its centre, which
on a slope is always "below the floor", so every step it lifts the body and zeroes its downward speed. That is the stickiness.
A 20 degree ramp with friction 0.5 should hold the box still; in 2D it creeps 13 cm in 2 s.

#### 4. Fast bodies go through thin walls unless "Continuous Collision" is ticked - measured

A 0.1 m ball at 60 m/s against a 0.1 m wall: tunnels through at 30, 50 and 144 steps a second. A 0.2 m box at 100 m/s: tunnels.
A 2D ball at 60 m/s: tunnels. With the tick on, all of them stop at the wall (maxX 9.85 m). Nothing tells a game maker that a
bullet needs the tick. (A box falling through a thin floor is already caught by the floor rescue in finding 3.)

#### 5. Sleeping bodies are not woken when what holds them up goes away or moves (3D, 2D read) - measured

- A box asleep on a **moving platform**: the platform slid 4 m and the box did not move at all (an awake box is carried fine,
  reaching the platform speed in 12 steps).
- Two boxes asleep, one on the other. Remove the lower one's collider, teleport it away, disable its collider or deactivate its
  object: the upper box **stays floating** at its old height for ever (all four cases, 2 s later still at y 1.49).
- Cause (read and consistent): a pair that only just touches (overlap 0, which is what a sleeping body rests at) produces no
  contact at all, so nothing ever notices the support leave.

#### 6. Springs blow up, chains stretch, pendulums lose energy - measured

- A spring joint with stiffness 20000 on a 1 kg ball at 50 steps a second ends as **NaN** (the body vanishes); 5000 at 30 steps
  a second, or 1000 on a 0.1 kg ball, does the same. The spring is applied as a plain explicit force, which is unstable once the
  stiffness times the step squared passes about 4 (this is a frame-rate dependent bug: stiffness 5000 is fine at 144 steps a second).
- A rope of 8 balls (1 kg each) hanging from a hook stretches to 10.9 m instead of 8 m; with a 50 kg ball on the end it stretches
  to **64 m**; a rope of 20 balls to 63 m instead of 20 m. 144 steps a second helps only a little (38 m).
- A pendulum (distance joint, 3 m, started at 45 degrees) loses 31 percent of its energy in 20 s at 50 Hz (16 percent at 144):
  it dies out too fast.
- Hinge limits overshoot by 3.4 degrees without the motor (the motor case is exact).

#### 7. A ball never stops rolling - measured

A ball rolling at 5 m/s on a flat floor travels 47 m in 10 s, 230 m in 60 s and 445 m in 120 s, still going 3.6 m/s, and never
sleeps (only the angular damping 0.05 per second slows it). There is no rolling resistance.

#### 8. Inertia of anything that is not a cube is wrong - measured

Turning force (torque) 1 about the long axis for 1 s, mass 1. A cube should reach 6.0 rad/s; it reaches 3.07. A 4 x 0.2 x 0.2
plank should reach 150 rad/s; it reaches 0.75 (200 times too stiff to spin about its length). The step uses one number for all
three axes taken from the bounding box, and the box of a turned body is bigger than the body.

#### 9. A collider on a child object does not carry its parent - measured

A body (Rigidbody 3D) whose only collider sits on a child object falls for ever (y -18.8 after 2 s): the child collider counts as
a static wall that travels with the body, not as part of it. Compound bodies built from children (the usual way to make a table
or an L shape) therefore do not work. Two colliders on the same object do work (rests at 0.50).

#### 10. Other bugs found

- **A ray that starts inside a box reports a hit at distance 0 with a zero-length normal** (a sphere and a capsule give a unit
  normal). Anything that normalises the normal gets NaN. Measured.
- **Per-collider materials do not exist**: friction and bounciness live on the Rigidbody, so a floor with no Rigidbody cannot be
  made icy or bouncy, and the stand-in friction for a body-less collider differs between 3D (0.6) and 2D (0.4); a Rigidbody 3D
  has 0.5 and a Rigidbody 2D 0.4. Read.
- **Heightmap terrain ground-follow takes 18 percent of the sideways speed away on every step the body touches the ground**
  (`friction = 0.18f` in `Physics3D::Step`), so the slowing depends on the step rate (0.82 per step at 50 Hz is 0.00005 per second;
  at 144 Hz it is much harsher). Read; the terrain is another agent's lane, so it is reported, not fixed here.
- **The exit callbacks (`OnCollisionExit`) arrive in memory-address order** (the old contacts are kept in a set ordered by
  pointer), which is not the same from run to run; a replay or a rollback can see them in another order. Read.
- **The debug view shows contacts only**: no joints, no sleeping bodies, no push strength. Read.
- Friction accuracy and bounce are fine at normal settings (see below) but the bounce height drops 8 percent at 30 steps a second.

### What already works

Measured on the unchanged code unless said otherwise:

- Sliding with friction, rotation frozen: stops after 2.38 m at 50 Hz against a theory of 2.33 m (2 to 3 percent, for 30 to 144 Hz).
- Bounce: a ball with bounciness 0.8 dropped from 2 m rises to 1.24 m (theory 1.28 m) at 50 Hz, 1.18 m at 30 Hz, 1.26 m at 144 Hz.
- Equal-mass elastic hit (three balls in a row): the end ball takes all the speed, whichever order the objects were made in
  (no order dependence in this case).
- A 100 kg box pushing a 1 kg box: both end at 4.95 m/s (the right answer for a no-bounce hit).
- Triggers: one enter, one exit and a stay every step in between.
- A body under an inactive parent does not move until the parent is switched on; a box under a scaled transform rests on its real
  face (y 0.250 for a 0.5 high box).
- The 2D slope slides correctly, so the stickiness in finding 3 is a 3D-only bug.
- Continuous Collision (when ticked) stops a ball, a box and a 2D ball at 60 to 100 m/s on a thin wall at 30, 50 and 144 steps a second.
- An awake box is carried by a platform moving at 2 m/s (it takes 12 steps to match the speed).
- Zero-gap lifts and no sinking when a platform carries a box upward.

### Frame rate findings

Release build, one thread, medians of 100 steps, `Physics3D::Step` only (load 2.0 to 2.2):

| Scene | Before |
|---|---|
| 500 resting boxes, free rotation (none ever sleeps) | 3.59 ms per step |
| 500 resting boxes, free rotation, sleeping switched off | 3.71 ms |
| 500 resting boxes, rotation frozen (all sleep) | 0.92 ms |
| 500 boxes touching their neighbours, rotation frozen (all sleep) | 0.91 ms |
| 100 falling and bouncing balls (first 5 s) | 0.13 ms (p95 0.25) |
| 100 falling boxes | 0.28 ms (p95 0.41) |
| 300 falling balls | 0.58 ms (p95 1.90) |
| Cost of N boxes that touch nothing: 100, 250, 500, 1000, 2000 | 0.10, 0.33, 0.96, 3.04, 10.8 ms |
| 20 chains of 15 joints (300 joints) in a 2000-object scene | 0.86 ms; one chain 0.06 ms |

- The cost grows with the square of the body count (2000 bodies that touch nothing take 10.8 ms: more than half of a 20 ms step).
  `claude/perf-pass-1` already has a sort-and-sweep broad phase for this, so it is **not repeated here**; this branch keeps its
  edits away from the pair loop header so the two merge.
- A resting pile that never sleeps (finding 2) is the biggest avoidable cost: 4 times the cost of the same pile asleep.
- Every joint looks up its partner **by name every step** (`FindNearest`, which can walk the whole scene): 300 joints in a
  2000-object scene cost 2.9 microseconds each. It scales with the size of the scene. Cache it.
- The step asks the scene for its bodies and colliders about a dozen times per step (`FindObjectsOfType`, which walks all objects
  and dynamic-casts every component) and builds a std::set of pairs; `claude/perf-pass-1` caches the lists, so also not repeated.
- The sweep that continuous collision uses tests **every collider in the scene** against the start pose before looking at where
  the body is going (`Sweep(..., skipStartOverlaps)` does the test before the cheap bounds test). With many fast bodies that is
  the cost, which matters for the voxel debris (many small fast bodies). Read.

### The plan

Everything below is gated by one scene setting, **Physics quality** (Classic or Improved), so old scenes play exactly as they did:

- A scene file without a `physicsquality` record loads as **Classic**. New scenes made in the editor, every example scene in
  `examples/physics/` and the physics playground are **Improved**. Project Settings > Physics has the switch, and a script can read
  and set it.
- Bug fixes that cannot change a correct scene (findings 3, 5, 10) apply to both qualities.
- Findings 1, 2, 4, 7, 8, 9 and the spring and chain work are Improved-only.

#### Batch 1 - bug fixes with a failing test first (both qualities)

| Fix | Test (`physics_deep_*`) |
|---|---|
| Slopes: the floor rescue measures the body's real lowest point under the lowest point | slope slide |
| Sleeping bodies wake when the thing under them moves, is disabled, removed or deactivated | wake on lost support, moving platform |
| A ray that starts inside a box gets a unit normal | ray inside box |
| Joints keep their partner instead of searching by name every step | joint partner cache |
| Springs stay stable at any stiffness and step rate (implicit damped spring) | spring stability |

#### Batch 2 - the Improved solver (3D first, then 2D)

- Contacts: a box on a box touches along a whole face (up to 4 points, found by clipping the faces); every other pair keeps its
  single contact point. A contact is also kept for a small gap (a skin) so a body at rest stays "touching".
- Solver: repeat the contact solve several times per step (setting: 4 to 30, default 10); remember last step's push per contact and
  start from it (warm start); friction is solved with the push so it holds (a box stays put on a gentle slope); positional correction
  with a small allowed overlap so there is no jitter.
- Inertia: a real inertia tensor for each shape (box, sphere, capsule, cylinder, the box around a mesh), in the body's own axes.
- Compound bodies: colliders on child objects (that have no body of their own) and several colliders on one object are one body;
  mass is shared by volume and the centre of mass and inertia are worked out for the whole.
- Sleeping: bodies that touch rest and wake as a group; a group sleeps only when every body in it has been still; a sleeping body
  wakes when anything that touched it moves away or is gone (finding 5), including a moving platform.
- Rolling resistance (finding 7): `Rigidbody 3D / 2D > Rolling Resistance` (default 0.03).
- Continuous collision for every fast body (finding 4): a body that moves farther in a step than half its own size is swept against
  the static colliders along its path (cheap: only the colliders near the path are tested). The old tick keeps its meaning. A
  setting turns the automatic part off.
- Per-collider physics materials (finding 10): friction, bounciness and a combine rule on every collider.
- Joints: chains and heavy-on-light stay together because the joint solve repeats with the contacts (Improved).

#### Batch 3 - new things, each with an example scene and a try-it step

- `examples/physics/physics_playground.okayscene`: crate towers, a ball that bounces and rolls to a stop, a ramp, an ice floor and a
  rubber floor, a moving platform, a rope, a bullet and a thin wall, a heavy box pushing a light one, with a text label per station.
- Physics debug view: joints, sleeping bodies and push strength.
- Debris helper for the voxel terrain: many small bodies with a cap and a lifetime.

### What each new file was written from

(Rule 19a: nothing here is copied or ported from another physics engine; these are public ideas worked out here and checked with tests.)

| File | Written from |
|---|---|
| `Physics/PhysicsMaterial.hpp` | the plain idea that a contact needs one friction and one bounce number; the combine rules are the usual four (average, smaller, larger, product) |
| `Physics/PhysicsQuality.hpp` | this engine's own setting |
| `Physics/ContactSolver.hpp`, `Physics/BoxManifold.hpp`, `Physics/Physics3DImproved.inl` (`Physics2DSolver` is not written yet) | sequential impulses (projected Gauss-Seidel on the contact constraints, accumulated impulses clamped, warm starting), the box-face clipping idea from *Real-Time Collision Detection* (C. Ericson, 2005, chapter 5: Sutherland-Hodgman clipping of the incident face against the reference face's side planes), rigid body inertia of boxes, spheres, cylinders and capsules from any mechanics textbook, the parallel-axis theorem for compound bodies, union-find for groups of touching bodies |
| spring change | the standard damped harmonic oscillator solved implicitly: the constraint's softness (gamma) and bias come from stiffness k, damping c and the step h as gamma = 1/(h(c + hk)), beta = hk/(c + hk) |
| `Physics/DebrisPool.hpp`, `Components/DebrisEmitter.hpp` | this engine's `ObjectPool`, `Rigidbody3D` and `Scene`: a capped list of pooled boxes with an age, a sleep timer and a shrink at the end |
| Slider joint | the textbook prismatic constraint: remove the relative motion across the axis (position and speed), clamp the travel to the limits, drive the speed along the axis with a force-limited motor |
| joint passes | repeating the constraint solve (Gauss-Seidel); the spring stays a force and is applied once |
| continuous collision | the existing conservative sampled sweep in `Physics3D.cpp`, restricted to the colliders near the swept path |

### Results

Status of every finding at the end of this change. "Tested" names the test file (all in `tests/`, run on Linux with g++, Release engine
library). Numbers are medians or single runs as said; the machine is shared (load 2 to 3 on 4 cores while timing), so differences under
about 10 percent mean nothing.

| Finding | Status | Evidence |
|---|---|---|
| 1 Stacks fall over or sink | Fixed in Improved | `physics_deep_improved`: 3 crates drift 1.2 mm, tilt 0.03 degrees, asleep 3 of 3; 5 crates drift 7.9 mm, tilt 0.16 degrees; frozen 5-stack top 4.496 (start 4.5; was 16 cm too low); frozen 10-stack top 9.491 (start 9.5; was 3.7 m too low); crates with 100 kg under 1 kg and 1 kg under 100 kg stand (`physics_deep_solver`) |
| 2 Resting boxes buzz, never sleep | Fixed in Improved | free-rotation box wobble 0 mm; 500 resting free-rotation crates: all 500 asleep (was 0), 1.09 ms a step (was 3.6) |
| 3 Bodies stick on slopes | Fixed in both | `physics_deep_bugs`: 30 degree ramp slid 11.44 m in 2 s (Classic) and 11.35 m (Improved), theory 11.30 m (was 0.23 m of 5.8 m for the same 2 s without the run-up); a grippy 20 degree ramp holds in Improved (0 m), Classic creeps 13 cm in 2 s as the 2D step always did |
| 4 Fast bodies tunnel | Fixed in Improved (automatic sweep) | ball at 60 m/s against a 0.1 m wall stops at x 9.87 at 30, 50 and 144 steps a second without ticking Continuous Collision |
| 5 Sleepers not woken | Fixed in both | support removed, teleported, collider disabled, object deactivated: the upper crate falls to y 0.5 (was 1.49 for ever); a box asleep on a platform moving at 2 m/s is carried to x 2.7 of 3.0 in 1.5 s |
| 6 Springs blow up | Spring fixed (both); ropes better (Improved) | spring joint tests in `physics_joints` stay green, stiff springs no longer NaN; rope of 8 balls hangs 8.03 m (was 10.9), rope with a 50 kg end hangs 13.2 m (was 64; true 8, **not fully fixed**) |
| 7 A ball never stops rolling | Fixed in Improved | rolling ball stops after 10.9 s having travelled 26.7 m (was 47 m in 10 s and still rolling at 120 s) |
| 8 Inertia wrong | Fixed in Improved | torque 1 for 1 s: cube 5.99996 rad/s (true 6), 4 x 0.2 x 0.2 plank 149.98 rad/s (true 150) |
| 9 Child collider does not carry its parent | Fixed in Improved (tick Use Child Colliders) | a table (top and four legs) rests at y 0.545 with no tilt |
| 10 Ray from inside a box, per-collider materials, exit order, debug view | Fixed | ray normal is unit length; materials: icy collider slides 20 m, rough floor 2.28 m; a rubber floor bounces a 0-bounce ball to 1.78 m from 2 m; exit callbacks arrive in scene order in both qualities (written after the fix; **not run against the old code**); debug view lists bodies and joints (`physics_deep_more`) |
| Joint partner looked up by name every step | Fixed (both) | 300 joints in a 2000-object scene: 0.84 ms before, 0.47 ms after |
| Heightmap terrain takes 18 percent of the sideways speed per step | Not fixed (terrain lane); reported | read only |

Found while finishing, and fixed (each has a test that failed first):

* A bouncing ball in Improved lost its bounce: the old floor rescue (`CatchTunneledFloor`) lifted it and cleared its speed the step it
  crossed the floor surface. The rescue is skipped when automatic continuous collision is on. (`physics_deep_improved`: rubber floor test, peak 0 before, 1.78 m after.)
* A scaled or small body was never held by the Improved solver once it moved fast: pairs that are apart by less than the contact skin were
  dropped by the broad-phase test before the skin was looked at, so a body that stopped 2 mm above the floor fell on for ever (found with
  `DebrisPool`: small chunks reached 30 m/s while "resting"). The broad phase now allows for the skin.
* A heavy crate shoving a light one was slowed as if it were only 8 times heavier: the mass-ratio limit now applies to up-down contacts only.
* In Classic, a first version of "wake a sleeper when its support goes" made a box that rests exactly touching wake, fall, sleep and
  wake again (8 enters and 7 exits in 4 seconds, 28 percent slower). A pair that is still resting no longer counts as lost
  (`physics_deep_more`: one enter, one exit, asleep).

#### Frame rate (Release, one thread, median of 100 steps, `Physics3D::Step` only)

Same program built against `main` (unchanged code), this branch with the scene on Classic, and this branch on Improved. Load average was 2.2 to 3.2.

| Scene | main | Classic (this branch) | Improved |
|---|---|---|---|
| 500 resting boxes, free rotation (sleep allowed) | 3.60 ms (0 asleep) | 3.66 ms (0 asleep) | **1.09 ms (500 asleep)** |
| 500 resting boxes, free rotation, sleeping off | 3.55 ms | 3.68 ms | 3.54 ms |
| 500 resting boxes, frozen rotation, 1.5 m apart | 0.78 ms | 0.81 ms | 1.13 ms |
| 500 frozen boxes touching each other | 0.90 ms | 1.22 ms | 2.56 ms |
| 500 touching frozen boxes, sleeping off | 1.13 ms | 1.24 ms | 7.92 ms |
| 100 moving balls (5 s) | 0.113 ms (p95 0.24) | 0.117 (p95 0.26) | 0.262 (p95 0.45); 51 asleep at the end |
| 100 moving boxes (5 s) | 0.284 ms | 0.292 | **0.140**; all 100 asleep at the end |
| 300 moving balls (5 s) | 0.552 ms (p95 1.9) | 0.572 | 1.171 (p95 2.3) |
| 300 joints (20 chains) in a 2000-object scene | 0.84 ms | **0.47** | 0.47 |

Honest reading: Classic costs what `main` costs (the 36 percent on the touching sleeping pile comes from the new rest tracking and is
the one Classic regression left; see "what is not done"). Improved wins by a lot where bodies can sleep (the resting pile: 3.3 times
cheaper; 100 boxes: twice) and costs about twice as much while bodies are awake and moving (the iterated solver), and a touching pile that
may not sleep costs 7 times as much (4 contact points per pair, 10 passes). The O(n squared) pair loop is untouched here; `claude/perf-pass-1`
replaces it.

#### Merge with `claude/perf-pass-1` (not merged here)

A trial merge (`git merge-tree`) conflicts in `engine/src/Physics/Physics3D.cpp` (seven places) and `docs/try_it.md` (the new 7u row and
section next to other branches' rows). The `Physics3D.cpp` spots, in the merged file's order: the include list at the top; where `Step` gathers
`bodies` and `colliders` (cached components); the pair loop header (the sort-and-sweep broad phase replaces the double loop: the Improved
contact skin `pad` and `improved ? BodyOfCollider : GetComponent` lines must be carried into the new loop); the contact branch inside the
loop; the end of the pair loop (my exit-order sort over `ended`, and the Classic rest tracking); `CatchTunneledFloor` and the end of `Step`
(debug body and joint lists, the `improved && autoContinuous` skip); and `Sweep` / `QueryLists` (my `only` list argument and the
bounds-before-overlap reorder, which `perf-pass-1` also touches). `Physics2D.cpp` merges cleanly.

### What is not done

* **The 2D Improved solver.** The setting is saved for 2D but the 2D step is unchanged (`Physics2DImproved.inl` only holds the offset fix). 2D stacks
  still sink and tip, and 2D has no slider, joint passes, debug joints or lost-support wake. The 3D solver core (`ContactSolver.hpp`) is written to be reused:
  a 2D body is a 3D body that turns about z only; the 2D box contact needs a two-point clip. Listed in `docs/status/UNFINISHED.md`.
* A long chain with a large mass ratio (50 kg on 1 kg links): 13 m instead of 8 m. Needs the joints solved together with the contacts.
* A touching pile of sleeping bodies costs more in Improved (2.56 ms for 500) than in Classic, because every sleeping pair still runs the contact
  test to send `OnCollisionStay`.
* Heightmap terrain ground-follow friction (18 percent per step): reported, not fixed (terrain lane).
* Not verified: Windows, a real window, a real GPU, the feel in a real game, the new Inspector rows and menu items (`editor/main.cpp` passes `g++ -fsyntax-only` with 0 errors; the editor was not linked
  or opened), and the exit-order fix against the old code.
* `DebrisPool` is not used by the voxel terrain yet: the voxel agent decides how to call it (`DebrisPool pool(scene); pool.Spawn(...); pool.Update(dt);`).

## The Engine quality: our own physics engine

Project Settings > Physics > "Physics quality of the open scene" has a third choice, **Engine** (3D). It runs the scene
on our own physics engine core instead of the Classic or Improved step. Classic and Improved are unchanged.

What you get in Engine:

- **Many bodies.** Touching pairs are found with a tree, so only bodies that moved are looked at; groups of touching
  bodies are solved on every CPU core with exactly the same result as on one core.
- **Steady stacks.** The contact points of each resting face are solved together, so towers and pyramids stand still and
  fall asleep.
- **Nothing tunnels.** A fast body is stopped before it reaches a wall, without ticking Continuous Collision.
- **Every shape:** box, sphere, capsule, cylinder, cone, convex hull, compound, plane, mesh collider (static), height
  field and terrain.
- **Joints:** Distance, Spring, Pin, Hinge and Slider as before, plus **Ball** and **Cone Twist** for ragdolls. Both turn
  about **Anchor** (a world point). Cone Twist keeps the swing inside **Swing Limit 1 / 2** and the twist inside Min /
  Max Twist. **Use Drive** pulls the body toward **Drive Target** (degrees from its start pose) like a damped spring:
  **Drive Frequency** (Hz) and **Drive Damping Ratio** (1 = no overshoot), or **Drive Stiffness** and **Drive Damping**
  when the frequency is 0; **Drive Max Torque** caps it. Scripts set Drive Target every frame for an active ragdoll.
  In Classic and Improved, Ball and Cone Twist are only a distance rod (the Console says so once).
- Callbacks (OnCollisionEnter3D / Stay / Exit, OnTriggerEnter3D / Stay / Exit), layers, trigger filters, teleports and
  set velocities work as in the other qualities. Raycasts and shape casts are the same code in every quality.

Not yet in Engine: the per-collider friction / bounce combine modes (friction combines as the square root of the two,
bounce as the larger), voxel terrain. Examples: `examples/physics_engine/physics_engine.okayscene` and
`physics_engine_5k.okayscene` (try it: pe1). How it works and what it was written from: [physics_core.md](physics_core.md).

