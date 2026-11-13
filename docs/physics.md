# Physics: hinges, mesh colliders, and 2D casts

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
