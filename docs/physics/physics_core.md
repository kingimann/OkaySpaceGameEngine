# Our own physics engine core (okay::phys) and the Engine physics quality

The owner asked (2026-11-22) for our own 3D physics at the level of a full engine such as Bullet, written only from
published papers and textbooks, with joints and motors solid enough for a Euphoria-style active ragdoll. This page is
the internal record of what exists, what each part was written from, how it plugs into the scene, and what is left.

No code from Bullet, PhysX, Jolt, Box2D, ODE, Havok or any other engine was read or used (CLAUDE.md 19a). Each source
file names the papers it follows in its header comment.

## Layout

The core is its own static library, `okay_physcore` (`engine/CMakeLists.txt`), with no engine dependencies, so its tests
build and link in seconds. `okay_engine` links it.

| File | What | Written from |
| --- | --- | --- |
| `Physics/Core/PMath.hpp` | vectors, quaternions, 3x3 matrices, transforms, boxes | textbook definitions |
| `Physics/Core/DynamicTree.hpp/.cpp` | broad phase: dynamic AABB tree, fat boxes, incremental insert / remove / move, box queries, ray casts | Ericson, *Real-Time Collision Detection* (2005) ch. 6; Goldsmith & Salmon 1987 (surface area cost); Bittner, Hapala, Havran 2015 (branch-and-bound sibling search); Kopta et al. 2012 (tree rotations) |
| `Physics/Core/Shape.hpp/.cpp` | sphere, box, capsule, cylinder, cone, convex hull (own incremental hull), triangle, plane, triangle meshes and height grids (`TriangleSource`, `TriMeshSource`, `HeightGridSource`); support points, support features, bounds, mass properties | van den Bergen 1999 (support mappings); de Berg et al., *Computational Geometry* ch. 11 (incremental hull); Blow & Binstock 2004 (hull inertia by tetrahedra) |
| `Physics/Core/Collide.hpp/.cpp` | narrow phase: GJK distance, EPA depth, box-box separating axis test, contact manifolds by clipping, reduction to 4 points, mesh pairs with per-point normals | Gilbert, Johnson, Keerthi 1988; van den Bergen 1999 / 2001 (GJK termination, EPA); Ericson 4.4, 5.1, 8.3.4, 9.5; Gregorius, GDC 2015 talk (contact reduction) |
| `Physics/Core/World.hpp/.cpp` | bodies, colliders, pairs, islands (union-find), sleeping, events, queries (ray, overlap, shape cast), fixed-step `Advance` with interpolation | Erleben, PhD thesis 2004 (islands, sleeping); Baraff, SIGGRAPH 2001 notes (rigid body state); Mirtich, PhD thesis 1996 (conservative advancement); Moller & Trumbore 1997 (ray-triangle) |
| `Physics/Core/Solver.cpp` | sequential impulses, warm starting, cone or pyramid friction, a block solver for the 2-4 normal points of a manifold, restitution pass, rolling resistance, position pass, speculative contacts | Catto 2005 / GDC 2006 (sequential impulses, accumulated clamping, warm starting); Catto GDC 2013 (speculative contacts); Murty 1988 (small LCPs by active sets); Erleben 2004 (position projection) |
| `Physics/Core/Joint.cpp` | one general joint: per-axis lock / limit / free, velocity motors, springs, elliptical swing cone, orientation drive, position drive, breaking; Fixed, Ball, Hinge, Slider, ConeTwist, Distance and SixDof are presets | Catto 2005 / GDC 2009 (constraint rows, point block); Catto GDC 2011 "Soft Constraints" (springs and drives from k, c or frequency and damping ratio); Grassia 1998 (exponential map, swing-twist) |
| `src/Physics/Physics3DEngine.inl` | the Engine quality: scene <-> world sync, events | written for this engine |

## How a step runs (World::Step)

1. **Broad phase.** Colliders whose real box left their fat box were re-inserted in the tree last step; only those look
   for new partners. Pairs are kept (sorted by collider id) while their fat boxes overlap.
2. **Narrow phase.** Every pair with a moving side gets a manifold (up to four points). Points closer than the
   speculative distance plus each body's motion this step are kept, so the solver stops a fast body before it reaches a
   wall (continuous collision without sub-steps). Runs on the parallel-for when there are many pairs; each pair writes
   only itself.
3. **Islands.** Awake dynamic bodies joined by contacts or joints form islands; a moving body touching a sleeping group
   wakes the whole group in the same step.
4. **Solve,** island by island (in parallel; each island touches only its own bodies, so the result is the same bits in
   any order): integrate forces, warm start, velocity iterations (joints, then friction, then the normal points of
   each manifold solved together), restitution, integrate positions, position iterations.
5. **Sleep** islands whose bodies all stayed slow for `timeToSleep`, and move changed colliders in the tree.

## The Engine physics quality (`PhysicsQuality::Engine`, saved as `physicsquality 2`)

Classic and Improved stay as they were (old scenes play exactly as before). Engine is a third choice for 3D:

| Scene part | In the Engine quality |
| --- | --- |
| Rigidbody3D | one engine body; mass, drag, angular drag, gravity scale, freeze axes, freeze rotation, continuous collision (a bigger speculative margin), allow sleep; forces and torques handed over each step; a transform or velocity set by game code is copied in (a teleport) |
| Box, Sphere, Capsule, Cylinder, Cone, Convex Hull, Compound, Plane colliders | engine shapes on the body (a compound becomes several shapes); colliders on child objects of a body with Use Child Colliders join it |
| Heightfield collider, Terrain | an engine height grid (no copy of triangles; only the cells under a body are looked at) |
| Mesh collider | static: an engine triangle mesh with its own tree; on a dynamic body: its convex hull |
| Triggers | sensors: OnTriggerEnter3D / Stay / Exit (tag and layer filters kept) |
| Layers | the scene's layer matrix decides every pair |
| Joint3D Distance, Spring, Pin, Hinge, Slider | engine Distance (rod or spring in N/m), Fixed, Hinge (limits, motor), Slider (limits, motor) |
| Joint3D Ball, ConeTwist (new) | engine Ball and ConeTwist with `anchor` as the world pivot, swing cone (`swingLimit1`, `swingLimit2`), twist limits (`minAngle`, `maxAngle`), and a drive (`useDrive`, `driveTarget` Euler degrees, stiffness and damping or frequency and damping ratio, `driveMaxTorque`). In Classic and Improved these two modes only keep the distance between the two points. |
| Events | OnCollisionEnter3D / Stay / Exit with normal, point, penetration and impact speed |
| Queries | Raycast, shape casts and overlaps still use the scene's colliders directly (they see the same poses) |
| Advanced | `Physics3D::EngineWorld()`, `EngineBody(rb)`, `EngineJoint(j)` reach the engine world (e.g. an active ragdoll setting drive targets every frame) |

What is different from Improved: friction combines as the square root of the two values and bounciness as the larger
one (the per-collider combine modes are not read yet); a terrain raises no collision callbacks (as before); voxel
terrains are not in the Engine world yet.

## Numbers (measured on the shared Linux build machine; machine was loaded by other builds)

* Broad phase alone (`physcore_tree`), a tenth of the boxes moving per step: 1,000 boxes 0.04 ms vs 1.05 ms for
  sort-and-sweep from scratch; 5,000: 1.2 vs 10.5 ms; 20,000: 7.9 vs 101.7 ms.
* Whole world (`physcore_world`, boxes and balls falling into piles, serial): 1,000 bodies 4 to 6 ms per step, 5,000
  bodies 12 to 22 ms per step.
* 10-box stack: the top box ends within 1 micrometre of its start sideways and the stack sleeps after about a second;
  55-box pyramid: no box moves more than 6.6 mm; 400 m/s 5 cm balls never pass a 2 cm wall; ball-joint pendulum
  length error 1.4e-6 m; 20-link chain worst gap 2.2 cm while swinging; a 5 kg, 1 m arm held level by a 4 Hz drive
  sags 2.4 cm and needs the expected 24.5 N*m.

## Tests

`physcore_tree`, `physcore_collide`, `physcore_world` (core only, link `okay::physcore`), `physics_engine_mode` (the
Engine quality in real scenes: stacks, callbacks, triggers, fast bodies, teleports, every joint mode, every collider
and floor kind, save and load, determinism with 0 and 3 job workers, timing against Improved).

## Not done yet

* Engine-quality scene queries through the engine tree (today the scene's own query code runs).
* Per-collider friction / bounce combine modes; voxel terrain; mesh-mesh pairs (static meshes only meet convex shapes).
* A dedicated configurable 6-DOF Joint3D mode in the editor (the core's SixDof joint is reachable from code).
* Flipping new scenes to Engine by default (owner's decision, CLAUDE.md 17: after the criteria are met).
