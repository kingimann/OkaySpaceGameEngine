# Physics audit and plan

This page is the physics audit the owner asked for ("look into it deeper... fix bugs, fps problem and improve") and the plan
that came out of it. It says what the physics does today, what it does badly, how that was measured, what is fixed, and what is
left. It is written in plain words. Where a number is given, the way it was got is given too.

Contents: [how this was measured](#how-this-was-measured) - [what a game maker notices first](#what-a-game-maker-notices-first)
- [what already works](#what-already-works) - [frame rate findings](#frame-rate-findings) - [the plan](#the-plan) -
[what each new file was written from](#what-each-new-file-was-written-from) - [results](#results) - [what is not done](#what-is-not-done)

## How this was measured

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

## What a game maker notices first

Ranked by how soon a person making a game would run into it.

### 1. Stacks of crates fall over or sink (3D and 2D) - measured

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

### 2. Resting boxes buzz and never go to sleep, so the frame cost stays high - measured

- One box with free rotation resting on the floor wobbles 0.98 cm up and down every step (y 0.5000 to 0.5097) and tilts up to
  0.8 degrees. With rotation frozen it does not move at all.
- A box dropped flat with free rotation **never fell asleep** in 10 seconds. A stack of 2 with frozen rotation never slept either.
- 500 resting boxes with free rotation cost **3.6 ms per step with 0 asleep**; the same boxes with frozen rotation sleep and cost
  0.9 ms. So the "sleeping saves CPU" feature does not switch on for the bodies that need it most.

### 3. Bodies stick on slopes (3D only) - measured, cause being proven by a test

A box on a 30 degree ramp with friction 0.1 (combined 0.245) should slide about 5.8 m in 2 seconds (acceleration
g(sin 30 - mu cos 30) = 2.8 m/s2). It slid **0.23 m** (rotation frozen) and 0.32 m (rotation free). The same ramp in 2D slides
correctly (12.9 m in 2 s including the run-up). Cause (read): the 3D step has an extra rescue for bodies that fall through a thin
floor (`CatchTunneledFloor`); it measures the body's lowest corner of its bounding box against the floor under its centre, which
on a slope is always "below the floor", so every step it lifts the body and zeroes its downward speed. That is the stickiness.
A 20 degree ramp with friction 0.5 should hold the box still; in 2D it creeps 13 cm in 2 s.

### 4. Fast bodies go through thin walls unless "Continuous Collision" is ticked - measured

A 0.1 m ball at 60 m/s against a 0.1 m wall: tunnels through at 30, 50 and 144 steps a second. A 0.2 m box at 100 m/s: tunnels.
A 2D ball at 60 m/s: tunnels. With the tick on, all of them stop at the wall (maxX 9.85 m). Nothing tells a game maker that a
bullet needs the tick. (A box falling through a thin floor is already caught by the floor rescue in finding 3.)

### 5. Sleeping bodies are not woken when what holds them up goes away or moves (3D, 2D read) - measured

- A box asleep on a **moving platform**: the platform slid 4 m and the box did not move at all (an awake box is carried fine,
  reaching the platform speed in 12 steps).
- Two boxes asleep, one on the other. Remove the lower one's collider, teleport it away, disable its collider or deactivate its
  object: the upper box **stays floating** at its old height for ever (all four cases, 2 s later still at y 1.49).
- Cause (read and consistent): a pair that only just touches (overlap 0, which is what a sleeping body rests at) produces no
  contact at all, so nothing ever notices the support leave.

### 6. Springs blow up, chains stretch, pendulums lose energy - measured

- A spring joint with stiffness 20000 on a 1 kg ball at 50 steps a second ends as **NaN** (the body vanishes); 5000 at 30 steps
  a second, or 1000 on a 0.1 kg ball, does the same. The spring is applied as a plain explicit force, which is unstable once the
  stiffness times the step squared passes about 4 (this is a frame-rate dependent bug: stiffness 5000 is fine at 144 steps a second).
- A rope of 8 balls (1 kg each) hanging from a hook stretches to 10.9 m instead of 8 m; with a 50 kg ball on the end it stretches
  to **64 m**; a rope of 20 balls to 63 m instead of 20 m. 144 steps a second helps only a little (38 m).
- A pendulum (distance joint, 3 m, started at 45 degrees) loses 31 percent of its energy in 20 s at 50 Hz (16 percent at 144):
  it dies out too fast.
- Hinge limits overshoot by 3.4 degrees without the motor (the motor case is exact).

### 7. A ball never stops rolling - measured

A ball rolling at 5 m/s on a flat floor travels 47 m in 10 s, 230 m in 60 s and 445 m in 120 s, still going 3.6 m/s, and never
sleeps (only the angular damping 0.05 per second slows it). There is no rolling resistance.

### 8. Inertia of anything that is not a cube is wrong - measured

Turning force (torque) 1 about the long axis for 1 s, mass 1. A cube should reach 6.0 rad/s; it reaches 3.07. A 4 x 0.2 x 0.2
plank should reach 150 rad/s; it reaches 0.75 (200 times too stiff to spin about its length). The step uses one number for all
three axes taken from the bounding box, and the box of a turned body is bigger than the body.

### 9. A collider on a child object does not carry its parent - measured

A body (Rigidbody 3D) whose only collider sits on a child object falls for ever (y -18.8 after 2 s): the child collider counts as
a static wall that travels with the body, not as part of it. Compound bodies built from children (the usual way to make a table
or an L shape) therefore do not work. Two colliders on the same object do work (rests at 0.50).

### 10. Other bugs found

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

## What already works

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

## Frame rate findings

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

## The plan

Everything below is gated by one scene setting, **Physics quality** (Classic or Improved), so old scenes play exactly as they did:

- A scene file without a `physicsquality` record loads as **Classic**. New scenes made in the editor, every example scene in
  `examples/physics/` and the physics playground are **Improved**. Project Settings > Physics has the switch, and a script can read
  and set it.
- Bug fixes that cannot change a correct scene (findings 3, 5, 10) apply to both qualities.
- Findings 1, 2, 4, 7, 8, 9 and the spring and chain work are Improved-only.

### Batch 1 - bug fixes with a failing test first (both qualities)

| Fix | Test (`physics_deep_*`) |
|---|---|
| Slopes: the floor rescue measures the body's real lowest point under the lowest point | slope slide |
| Sleeping bodies wake when the thing under them moves, is disabled, removed or deactivated | wake on lost support, moving platform |
| A ray that starts inside a box gets a unit normal | ray inside box |
| Joints keep their partner instead of searching by name every step | joint partner cache |
| Springs stay stable at any stiffness and step rate (implicit damped spring) | spring stability |

### Batch 2 - the Improved solver (3D first, then 2D)

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

### Batch 3 - new things, each with an example scene and a try-it step

- `examples/physics/physics_playground.okayscene`: crate towers, a ball that bounces and rolls to a stop, a ramp, an ice floor and a
  rubber floor, a moving platform, a rope, a bullet and a thin wall, a heavy box pushing a light one, with a text label per station.
- Physics debug view: joints, sleeping bodies and push strength.
- Debris helper for the voxel terrain: many small bodies with a cap and a lifetime.

## What each new file was written from

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

## Results

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

### Frame rate (Release, one thread, median of 100 steps, `Physics3D::Step` only)

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

### Merge with `claude/perf-pass-1` (not merged here)

A trial merge (`git merge-tree`) conflicts in `engine/src/Physics/Physics3D.cpp` (seven places) and `docs/try_it.md` (the new 7u row and
section next to other branches' rows). The `Physics3D.cpp` spots, in the merged file's order: the include list at the top; where `Step` gathers
`bodies` and `colliders` (cached components); the pair loop header (the sort-and-sweep broad phase replaces the double loop: the Improved
contact skin `pad` and `improved ? BodyOfCollider : GetComponent` lines must be carried into the new loop); the contact branch inside the
loop; the end of the pair loop (my exit-order sort over `ended`, and the Classic rest tracking); `CatchTunneledFloor` and the end of `Step`
(debug body and joint lists, the `improved && autoContinuous` skip); and `Sweep` / `QueryLists` (my `only` list argument and the
bounds-before-overlap reorder, which `perf-pass-1` also touches). `Physics2D.cpp` merges cleanly.

## What is not done

* **The 2D Improved solver.** The setting is saved for 2D but the 2D step is unchanged (`Physics2DImproved.inl` only holds the offset fix). 2D stacks
  still sink and tip, and 2D has no slider, joint passes, debug joints or lost-support wake. The 3D solver core (`ContactSolver.hpp`) is written to be reused:
  a 2D body is a 3D body that turns about z only; the 2D box contact needs a two-point clip. Listed in `docs/UNFINISHED.md`.
* A long chain with a large mass ratio (50 kg on 1 kg links): 13 m instead of 8 m. Needs the joints solved together with the contacts.
* A touching pile of sleeping bodies costs more in Improved (2.56 ms for 500) than in Classic, because every sleeping pair still runs the contact
  test to send `OnCollisionStay`.
* Heightmap terrain ground-follow friction (18 percent per step): reported, not fixed (terrain lane).
* Not verified: Windows, a real window, a real GPU, the feel in a real game, the new Inspector rows and menu items (`editor/main.cpp` passes `g++ -fsyntax-only` with 0 errors; the editor was not linked
  or opened), and the exit-order fix against the old code.
* `DebrisPool` is not used by the voxel terrain yet: the voxel agent decides how to call it (`DebrisPool pool(scene); pool.Spawn(...); pool.Update(dt);`).
