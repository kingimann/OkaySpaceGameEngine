# Physics: hinges, mesh colliders, 2D casts, and step rate

Short recipes for the physics pieces people ask for first. The full map of what
exists is in [feature map 6](feature_map/06-physics.md).

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

The navigation bake still treats a mesh collider as its bounding box.

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
- **Smooth the motion** draws each body part way between its last two physics steps. At 144 frames a second and 10 physics steps a second the body still moves every frame, instead of jumping once every several frames. A body you move by hand still goes there at once, with no blend back.
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
