# Our own physics engine core (okay::phys): the one physics path, 3D and 2D

The owner asked (2026-11-22) for our own 3D physics at the level of a full engine such as Bullet, written only from
published papers and textbooks, with joints and motors solid enough for a Euphoria-style active ragdoll. This page is
the internal record of what exists, what each part was written from, how it plugs into the scene, and what is left.

No code from Bullet, PhysX, Jolt, Box2D, ODE, Havok or any other engine was read or used (CLAUDE.md 19a). Each source
file names the papers it follows in its header comment.

## Layout

The core is its own static library, `okay_physcore` (`engine/CMakeLists.txt`), so its tests build and link in seconds.
It uses the engine's `StringId` hash for its state hash and signatures; `okay_engine` links it.

| File | What | Written from |
| --- | --- | --- |
| `Physics/Core/PMath.hpp` | vectors, quaternions, 3x3 matrices, transforms, boxes | textbook definitions |
| `Physics/Core/DynamicTree.hpp/.cpp` | broad phase: dynamic AABB tree, fat boxes, incremental insert / remove / move, box queries, ray casts | Ericson, *Real-Time Collision Detection* (2005) ch. 6; Goldsmith & Salmon 1987 (surface area cost); Bittner, Hapala, Havran 2015 (branch-and-bound sibling search); Kopta et al. 2012 (tree rotations) |
| `Physics/Core/Shape.hpp/.cpp` | sphere, box, capsule, cylinder, cone, convex hull (own incremental hull), triangle, plane, triangle meshes and height grids (`TriangleSource`, `TriMeshSource`, `HeightGridSource`); support points, support features, bounds, mass properties | van den Bergen 1999 (support mappings); de Berg et al., *Computational Geometry* ch. 11 (incremental hull); Blow & Binstock 2004 (hull inertia by tetrahedra) |
| `Physics/Core/Collide.hpp/.cpp` | narrow phase: GJK distance, EPA depth, box-box separating axis test, contact manifolds by clipping, reduction to 4 points, mesh pairs with per-point normals | Gilbert, Johnson, Keerthi 1988; van den Bergen 1999 / 2001 (GJK termination, EPA); Ericson 4.4, 5.1, 8.3.4, 9.5; Gregorius, GDC 2015 talk (contact reduction) |
| `Physics/Core/World.hpp/.cpp` | bodies, colliders, pairs, islands (union-find), sleeping, events, an overlap query, fixed-step `Advance` with interpolation | Erleben, PhD thesis 2004 (islands, sleeping); Baraff, SIGGRAPH 2001 notes (rigid body state); Mirtich, PhD thesis 1996 (conservative advancement); Moller & Trumbore 1997 (ray-triangle) |
| `Physics/Core/Solver.cpp` | sequential impulses, warm starting, cone or pyramid friction, a block solver for the 2-4 normal points of a manifold, restitution pass, rolling resistance, position pass, speculative contacts | Catto 2005 / GDC 2006 (sequential impulses, accumulated clamping, warm starting); Catto GDC 2013 (speculative contacts); Murty 1988 (small LCPs by active sets); Erleben 2004 (position projection) |
| `Physics/Core/Joint.cpp` | one general joint: per-axis lock / limit / free, velocity motors, springs, elliptical swing cone, orientation drive, position drive, breaking; Fixed, Ball, Hinge, Slider, ConeTwist, Distance and SixDof are presets | Catto 2005 / GDC 2009 (constraint rows, point block); Catto GDC 2011 "Soft Constraints" (springs and drives from k, c or frequency and damping ratio); Grassia 1998 (exponential map, swing-twist) |
| `Physics/Core/Horizon.hpp` | the horizon loop shared by the hull builder and EPA (edges of the faces a new point sees) | de Berg et al. ch. 11; van den Bergen 2001 |
| `src/Physics/Physics3DEngine.inl` | the bridge: scene <-> world sync, events, joint breaking, materials | written for this engine |
| `src/Physics/Physics2DEngine.inl` | the 2D bridge: the same for Rigidbody2D, the 2D colliders and Joint2D (section "2D on the core") | written for this engine |

## How a step runs (World::Step)

1. **Pair finding.** Every moving collider's box is first stretched over the motion it will make this step (its speed
   times the step), so a body spawned or teleported next to a wall, or moving fast, finds its partner before it can
   pass through. Pairs are kept (sorted by collider id) while their fat boxes overlap.
2. **Narrow phase.** Every pair with a moving side gets a manifold (up to four points). Points closer than the
   speculative distance plus each body's motion this step are kept, so the solver stops a fast body before it reaches a
   wall (continuous collision without sub-steps). Runs on the parallel-for when there are many pairs; each pair writes
   only itself. A pair that is closing fast enough to touch this step wakes a sleeping partner.
3. **Islands.** Awake dynamic bodies joined by contacts or joints form islands; a moving body touching a sleeping group
   wakes the whole group in the same step.
4. **Solve,** island by island (in parallel; each island touches only its own bodies, so the result is the same bits in
   any order and with 0, 1, 3 or 7 workers): integrate forces, warm start, velocity iterations (joints, then friction,
   then the normal points of each manifold solved together), restitution, integrate positions, position iterations.
   A body whose state became NaN or infinite is put back where it was last step, standing still.
5. **Sleep** islands whose bodies all stayed slow for `timeToSleep`, and move changed colliders in the tree.

## One physics path (owner, 2026-11-22: "No classic, replace with improved. I don't want duplicates.")

Every 3D and every 2D scene runs on this core. The old Classic and Improved 3D steps, the separate Engine choice and
the separate 2D step are gone (2D moved onto the core on 2026-11-24, branch `claude/physics-2d-core-1`).

The **physics quality** value is kept only so old scene files load and save back unchanged: a file that says Classic,
Improved, Engine or nothing loads, and saves the same line it had. The value changes nothing (`test_physics_one_path`
proves both). The editor no longer shows it; **Solver iterations** stays.

| Scene part | How it runs |
| --- | --- |
| Rigidbody3D | one engine body; mass, drag, angular drag, gravity scale, freeze axes, freeze rotation, continuous collision (a bigger speculative margin), allow sleep; forces and torques handed over each step; a transform or velocity set by game code is copied in. A kinematic body is driven to its new pose (it pushes what it meets) unless the jump is faster than 50 m/s, which is a teleport (no shove). A switched-off body's colliders become static. |
| Box, Sphere, Capsule, Cylinder, Cone, Convex Hull, Compound, Plane colliders | engine shapes on the body (a compound becomes several shapes); colliders on child objects of a body with Use Child Colliders join it. `contactOffset` is kept. |
| Heightfield collider, Terrain | an engine height grid (no copy of triangles; only the cells under a body are looked at) |
| Voxel terrain | the old voxel pass, kept as it was (`Physics3D::StepTerrain`), after the engine step; it also raises `groundedOnTerrain` |
| Mesh collider | static: an engine triangle mesh with its own tree, shared per mesh asset; on a dynamic body: its convex hull. A mesh collider with no mesh yet collides as its size box. |
| Triggers | sensors: OnTriggerEnter3D / Stay / Exit, static triggers included; the tag and layer filter is checked every step, so changing a tag or a filter while two things overlap raises Enter or Exit |
| Layers | the scene's layer matrix decides every pair |
| Materials | friction, still grip (static friction below 0.12 m/s), bounciness, rolling friction and the per-collider combine modes come from the collider's Physics Material (`CombineSurface`); surface linear and angular damping (mud, water) is applied after the step |
| Joint3D Distance, Spring, Pin, Hinge, Slider | engine Distance (rod or spring in N/m), Fixed, Hinge (limits, motor), Slider (limits, motor); Break Force breaks them, and a body yanked free keeps its speed. A limit's position correction turns the body about the joint's pivot, so a hinged arm stops at its stop without its far end overshooting |
| Joint3D Ball, ConeTwist | engine Ball and ConeTwist with `anchor` as the world pivot, swing cone (`swingLimit1`, `swingLimit2`), twist limits (`minAngle`, `maxAngle`), and a drive (`useDrive`, `driveTarget` Euler degrees, `driveStiffness` and `driveDamping` or `driveFrequency` and `driveDampingRatio`, `driveMaxTorque`). These drive field names are kept stable: the active ragdoll moves onto them. They are saved only when not default. |
| A joint whose partner object is destroyed | keeps its length and hangs from the partner's last position (it is not flung) |
| Events | OnCollisionEnter3D / Stay / Exit with normal, point, penetration and impact speed; one Enter and one Exit per pair of components (a compound's pieces or a collider rebuilt while playing do not repeat them); the event pass costs one look per touching pair |
| Queries | Raycast, shape casts, overlaps and closest point are `Physics3D`'s own exact tests (the core keeps no second ray or sweep code; it only has an overlap test built on its contact code). They ask a broad phase first: one `DynamicTree` (the core's tree class) over the scene's collider list, brought up to date at every query (`Physics3D.cpp`, "The broad phase of the scene queries"; test `physics_query_broadphase`) |
| Step length | a frame longer than 1/45 s is cut into equal slices no longer than that |
| Advanced | `Physics3D::EngineWorld()`, `EngineBody(rb)`, `EngineJoint(j)` reach the engine world (e.g. an active ragdoll setting drive targets every frame) |

Ids: a body, collider or joint id carries a generation in its top 8 bits, so an id kept after its object was destroyed
never reaches a new object that took the same slot.

## 2D on the core (`Physics2DEngine.inl`, 2026-11-24)

`Physics2D` owns its own `phys::World` (2D and 3D things never met, and still do not). The public `Physics2D` API, the
components and the saved-scene fields are unchanged.

| 2D part | How it runs |
| --- | --- |
| Rigidbody2D | one core body with linear z and angular x, y locked (`SetLocks(4, 3)`; Freeze Rotation locks z as well). The object's z position and its other angles are left alone. Mass, drag, angular drag, gravity scale, allow sleep, Continuous Collision (a wider margin; every body is stopped by speculative contacts anyway). A pose or speed set by game code is copied in; a kinematic body moved by game code is driven there (it carries and pushes) unless the jump is faster than 50 units a second |
| Box, Circle, Capsule, Polygon, Composite, Edge colliders | the collider's convex pieces (`shape2d::PiecesOf`) pushed out along z: rectangle -> box, circle -> ball, capsule -> capsule, convex polygon -> prism (hull), edge segment -> a strip 2 mm thick. Each shape is at least twice as deep along z as it reaches in the plane, so the shallowest way out of an overlap is always in the plane. Each piece's density is set so it weighs its 2D area, and the body's turning inertia is the real 2D figure (`World::SetInertia`): a disc turns as a disc, not as a ball |
| A collider with no Rigidbody2D on its object | a kinematic core body of its own that never moves by itself. When game code moves the object (a platform on a tween) it is driven there like a kinematic body, so it lifts and carries what stands on it; a jump faster than 50 units a second is a teleport |
| Triggers, layers, materials | sensors with the tag and layer filter checked every step; the scene's layer matrix; per-collider friction and bounciness with the combine modes (`CombineSurface`); hits slower than 0.5 units a second do not bounce (the old 2D rule) |
| One-way platforms (Edge collider) | `World::contactEnabled`: asked every step for each close pair; the pair is switched off while the other collider's centre is on the far side of the edge (the old rule) |
| Joint2D Distance, Spring, Pin, Hinge | core Distance (rod, or spring in N/m), Fixed, Hinge about z with limits and motor. A partner object without a body is followed when it moves. `breakForce` is a stretch length, as before |
| Events | OnCollisionEnter2D / Stay / Exit and OnTriggerEnter2D / Stay / Exit from the world's touching pairs, one per pair of components. Two solid colliders that cannot be pushed (two fixed ones, a kinematic body or a collider moved by its transform against a wall) still raise OnCollision while they overlap, as the old 2D step did (`World::reportStillPairs`; nothing is pushed) |
| Queries | `Physics2D`'s own ray, overlap and cast code, unchanged (it never used the step) |
| Step length | a step longer than 1/45 s is solved in equal slices (forces are applied in every slice) |

Added to the core for this: `World::contactEnabled` (a per-step switch for a pair), `World::reportStillPairs` (solid pairs
without a dynamic body are reported; off in 3D), `World::SetInertia` (a given inertia instead of the shapes') and `World::Configure` (the per-step body settings in one call; the 3D bridge uses it
too). One core fault was fixed: a joint that holds a point (Fixed, Ball, Hinge) did nothing on a body with a locked
axis, because the point constraint's matrix had an empty row and no inverse; an axis neither body can move along is
now left out (`PointInverse` in `Joint.cpp`, tested in `physcore_world` and by the 2D pin and hinge tests). The solver,
the narrow phase and the locks are the ones 3D uses.

What a 2D game can notice (stacks, resting depth, waking and the carried crate were measured in `physics2d_core`; the
rest follows from the code and was not measured): stacks stand instead of sinking; resting
things sit 1 to 2 mm into each other, about 1 cm under a 50 kg crate (the core's slop and soft position correction), instead of exactly touching;
bodies fall asleep in groups and wake when their support goes; a collider without a Rigidbody2D that game code moves now
carries what stands on it sideways; a Pin joint is a weld (it also holds the turn, as in
3D; the old 2D pin only held the position); a hinge pushes back on its partner body; Rolling Resistance (default 0.03)
now slows a rolling ball; a collider's own friction and bounciness (>= 0) are used, before only the body's were; a body
going up through a one-way platform raises no collision callbacks until it lands; two solid colliders that only touch
edge to edge without a dynamic body raise no callbacks (they must overlap); speeds are capped at 10,000 units a second.

## Memory

* A static Mesh collider's triangles live once per mesh asset: one `TriMeshSource` (the triangles, 36 bytes each, plus
  a tree node of about 48 bytes per triangle, so about 84 bytes per triangle) shared by every collider using that mesh;
  each collider adds only a small scaled view (`ScaledTriangleSource`). A cached mesh that no collider uses any more is
  dropped. The scene's own `MeshCollider3D` keeps its `TriMesh` for queries, so a static mesh is held twice (once for
  queries, once for contacts) until the query code moves onto the core.
* Height fields and terrains are copied as a grid of floats (4 bytes per sample), never as triangles.
* Convex hulls are taken as the collider already built them (`HullData::FromTriangles`), not wrapped again.

## What was replaced (deleted on 2026-11-23, branch `claude2/physics-no-classic`)

| Part | Engine core | Old copy that was deleted |
| --- | --- | --- |
| Broad phase (3D) | `Core/DynamicTree` | the sort-and-sweep pass of the 3D step (`Physics/Broadphase.hpp` stays for 2D) |
| Convex distance and depth | `Core/Collide` (GJK, EPA) | the Improved step's use of `Physics/ConvexContact.hpp` (the file stays: the scene queries use it) |
| Box contacts | `Core/Collide` (box SAT + clipping) | `Physics/BoxManifold.hpp` (222 lines) |
| Contact solver | `Core/Solver.cpp` | `Physics/ContactSolver.hpp` (219 lines), `Physics/Physics3DImproved.inl` (683 lines), the Classic pair loop in `Physics/Physics3D.cpp` |
| Islands and sleeping | `Core/World.cpp` | group sleeping in `Physics3DImproved.inl`, rest contacts in `Physics3D.cpp` |
| Fast bodies | speculative contacts and stretched boxes in `Core/World.cpp` | the sweep CCD (`SweepBody`, `SweepBodyBy`, `CatchTunneledFloor`) in `Physics3D.cpp` |
| Joint3D solving | `Core/Joint.cpp` | the joint pass in `Physics3D::StepSlice` (`Math/JointLimit.hpp` stays: the IK components use it) |
| Mass and inertia | `Core/Shape.cpp` | `ComputeMassProps` in `Physics3DImproved.inl`, `InvInertia` in `Physics3D.cpp` |
| Quality choice | none | the Quality list and "Catch fast bodies automatically" in Project Settings > Physics |

In all, `Physics3D.cpp` lost 1,336 lines (71 added), and three files went: 1,124 lines. The engine and editor are about
2,200 lines smaller. Two tests of the deleted solver went with it (`test_physics_deep_manifold.cpp`,
`test_physics_deep_solver.cpp`).

`Core/PMath.hpp` is a deliberate small copy of vector / quaternion / matrix maths: it keeps every operation that
bit-identical results depend on in one place. It stays.

## Numbers (measured on the shared Linux build machine; machine was loaded by other builds)

* Broad phase alone (`physcore_tree`), a tenth of the boxes moving per step: 1,000 boxes 0.04 ms vs 1.05 ms for
  sort-and-sweep from scratch; 5,000: 1.2 vs 10.5 ms; 20,000: 7.9 vs 101.7 ms.
* In a scene (`physics_engine_mode`, 400 to 3,000 mixed bodies incl. compounds, -O1 build, loaded machine): 1,000
  bodies 19 ms per frame in Engine against about 3,100 ms in Improved; 3,000 bodies 108 ms against about 1,900 ms.
  (Improved is this slow on this mixed scene with compound bodies; the 400-body determinism scene of
  `physics3d_determinism` is the usual Improved reference.)
* Whole world (`physcore_world`, boxes and balls falling into piles, serial): 1,000 bodies 4 to 6 ms per step, 5,000
  bodies 12 to 22 ms per step.
* 10-box stack: the top box ends within 1 micrometre of its start sideways and the stack sleeps after about a second;
  55-box pyramid: no box moves more than 6.6 mm; 400 m/s 5 cm balls never pass a 2 cm wall; ball-joint pendulum
  length error 1.4e-6 m; 20-link chain worst gap 2.2 cm while swinging; a 5 kg, 1 m arm held level by a 4 Hz drive
  sags 2.4 cm and needs the expected 24.5 N*m.

## Tests

`physics2d_core` (2D: a 10-crate stack at 30, 60 and 120 steps a second, waking on lost support, heavy on light, a fast
ball at a thin wall). `physcore_tree`, `physcore_collide`, `physcore_world` (core only, link `okay::physcore`); `physics_engine_mode` (the
core in real scenes: stacks, callbacks, triggers, fast bodies, teleports, every joint mode, every collider and floor
kind, save and load, determinism with 0 and 3 job workers); `physics_one_path` (old files with any quality value load
and save back unchanged; the value changes nothing); `physics_core_regressions` (the 13 problems the independent
review found, each with a test written first: stale ids, joint id reuse, a sleeping body hit, tunnelling at spawn,
NaN, jointed bodies sleeping, the event cost, compound and rebuild callbacks, an empty mesh collider, a lost joint
partner, kinematic teleports, determinism with 0, 1, 3 and 7 workers). Every older physics, joint, ragdoll, vehicle
and controller test runs on this one path.

## Not done yet

* Scene queries on the world's own tree and shapes. Today they use a tree of their own (the same `DynamicTree` class,
  over the scene's collider list, checked against the scene at every query) and `Physics3D`'s exact tests, because the
  world only knows what the last step saw and scripts move things between steps.
* Voxel terrain inside the core (it runs as the old pass after the core step); mesh-mesh pairs (static meshes only
  meet convex shapes); a terrain raises no collision callbacks (as before).
* A dedicated configurable 6-DOF Joint3D mode in the editor (the core's SixDof joint is reachable from code).
* 2D: a slider joint, 2D bodies and joints in the debug view, 2D queries through the core tree.
