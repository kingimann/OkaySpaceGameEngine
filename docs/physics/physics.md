# Physics: quality, materials, joints, debris, hinges, mesh colliders, 2D casts, and step rate

Short recipes for the physics pieces people ask for first. The full map of what
exists is in [feature map 6](../feature_map/06-physics.md).

## Physics quality: Classic and Improved

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

### Bug fixes that apply to both qualities

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

## Physics materials

Every collider has its own **Physics Material** (Inspector, collider > Physics Material): **Own Friction** and **Own Bounciness**, and how each
is mixed with the surface it touches (Default, Average, Minimum, Multiply, Maximum). With the boxes off a collider uses its Rigidbody's numbers,
exactly as before. This is how a floor with no Rigidbody is made icy (friction 0) or bouncy (bounciness 0.9). Default mixing: friction is the
geometric mean of the two, bounciness the larger. When two colliders ask for different rules the stronger wins (Average < Minimum < Multiply
< Maximum). Used by the Improved solver; the Classic step reads the Rigidbody numbers only.

## Joints: slider, ropes, debug view

- **Slider (prismatic)** is a new Joint 3D mode: the body moves only along **Axis** (a world direction taken when Play starts), cannot turn,
  can have **Min Travel / Max Travel** limits in metres from where it started and a motor (**Motor Speed** in m/s, **Max Motor Force**). A
  drawer, a lift, a piston. Scene files store it as mode 4.
- In Improved the constraint joints (distance, pin, hinge, slider) are solved as many times as the contact solver runs, so a rope of
  8 balls hangs 8.03 m long instead of 10.9 m. A 50 kg weight on 1 kg links still stretches (13 m instead of 64 m for a rope that should be
  8 m): a long chain with a big mass ratio needs a better solver than repeated passes (see `docs/status/UNFINISHED.md`). Springs are forces and are applied once.
- The physics debug view (View > Scene Overlays > Physics Debug > Show physics (Scene + Game); Scene and Game view) now draws **joints** (orange line, red when broken); sleeping bodies
  were already grey.

## Debris for breaking and digging games (`DebrisPool`)

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

## A door or a lever that stops

Add **Joint 3D** (Add Component > Physics) and set **Mode** to **Hinge**. The
body spins around the hinge and, until you say otherwise, it can spin the whole
way around.

Turn on **Use Limits**. **Min Angle** and **Max Angle** are degrees from the
pose the joint had when play started. A door set to 0 and 90 opens a quarter
turn and stops. A motor keeps pushing, but it cannot grind past the stop: the
outward spin is dropped when the hinge is against the limit.

Leave **Use Limits** off and the hinge behaves as it always did. An older scene
file that has no limit fields still loads, with limits off.

## A mesh that can move

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

## Sweeping a box or a capsule in 2D

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

## Physics step rate and parented bodies

Physics can run at its own rate, and a rigidbody can live under another object.

### Steps per second

Project Settings > Physics has **Steps per second** and **Smooth the motion**.

- **50** is the rate Play and a built game already used. A project saved before this setting existed stays at 50 with smoothing on, so an old scene plays the same.
- A bare scene that nobody turned the fixed step on for still takes **one physics step per frame**. That is the old default for tools and tests.
- The same drop lands in the same place at a low step rate and a high one, within a small tolerance. A slower rate is a slightly coarser step, not a different game.
- **Smooth the motion** draws each body part way between its last two physics steps. At 144 frames a second and 10 physics steps a second the body still moves every frame, instead of jumping once every several frames. A body you move by hand still goes there at once, with no blend back. A script that only turns a body (sets its rotation each frame, like the third-person controller) does not count as moving it: the position is still smoothed and your rotation is drawn as you set it. Rotation made by physics itself (spin) is still blended between steps. Any change to the rotation counts as game-set, however small, so a slow smoothed turn (a fraction of a degree per frame) arrives at its target. Checked with the real third-person controller (test `controller_interpolation`: a rigged character walking in a tight circle, 50 physics steps a second, drawn at 30, 60 and 144 frames a second): the drawn position changes by an even amount each frame, to within 2 mm. What is left is the same as when walking straight: about 1.3 mm of unevenness at 30 frames a second (cause not looked into) and nothing measurable at 60 and 144. The orbit camera, which eases toward the mouse, stays within 1 mm of the straight-walk figure. Not covered: other rigs, other turn rates, root motion from animation clips, and a real screen. Limit: if game code sets the rotation of a body that physics also spins, the game's rotation wins that frame: it is drawn as the steps of that frame left it, not blended between steps.
- While the game runs, a script can read and set the live rate. Text scripts: `physics_steps()`, `set_physics_steps(n)`, `physics_smooth()`, `set_physics_smooth(on)`. The same calls are visual-script nodes `Fn.physics_steps`, `Fn.set_physics_steps`, `Fn.physics_smooth` and `Fn.set_physics_smooth`. Typed scripts: `Time.physicsSteps` and `Time.smoothPhysics`. `n` is steps per second, from 1 to 500. `0` goes back to one step per frame.
- A build writes the project rate into `game.okayconfig` (`physics_hz`, `physics_smooth`). A build from before those lines keeps 50 and smoothing on. A dedicated server still sends the real physics pose, not the smoothed one.

### A body under a parent

The solvers move a body in **world** space, whether or not it has a parent.

- **Moving the parent** carries the body with it. The body's own speed, gravity and collisions are applied on top, in the world.
- **Turning the parent** carries the body around with it. Gravity still pulls straight down. A turned parent does not tilt gravity, and a scaled parent does not stretch a 1 metre-per-second step into 2.
- **Unparenting** keeps the pose you see and adds the speed the parent was giving the body, including the swing from a parent that is turning. After that it is an ordinary free body. A static body is not given a speed.
- An un-parented body is unchanged: its local pose is its world pose, and the step writes that the way it always did.
- A hinge on a parented body still measures its angle in the parent's space. Put that body at the top of the hierarchy when the hinge must spin in the world.

The Rigidbody 3D inspector says this when the object has a parent.
