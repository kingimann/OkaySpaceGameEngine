# Active Ragdoll (our own dynamic character)

> **What this is:** a humanoid whose body is simulated: it follows its animation with joint
> "muscles", keeps its balance when pushed, steps to catch itself, falls when it cannot (arms
> out, head protected), lies still, gets up face up or face down, and blends back into the
> animation. It is our own system, written from published research and general control methods.
> No code, data or assets from any commercial product were used or looked at.
>
> **Status:** first version (2026-11-22), on the physics core since 2026-11-25 (claude/ragdoll-core-1).
> Works in the unit tests on a flat floor; two scene tests still fail (see "What is not done"). The
> example scene runs in `active_ragdoll` but was not seen in a window after the move.

## Where it lives

| Part | File | Job |
|---|---|---|
| Physics layer | `engine/include/okay/Animation/RagdollPhysics.hpp`, `engine/src/Animation/CoreRagdoll.cpp` | `RagdollPhysics` (the thin interface the controller uses) and `CoreRagdoll` (its bodies and joints on the physics core) |
| Controller and behaviours | `engine/include/okay/Animation/DynamicCharacter.hpp`, `engine/src/Animation/DynamicCharacter.cpp` | states, drives, balance, the six behaviour modules |
| Component | `engine/include/okay/Components/Ragdoll.hpp`, `engine/src/Components/Ragdoll.cpp` (`ActiveRagdoll.hpp` is the old name, an alias) | bones, the scene's physics world, scene rays, HealthStat, controllers, root |
| Tests | `tests/test_active_ragdoll.cpp` (suite `active_ragdoll`), `tests/test_ragdoll.cpp` (`ragdoll`), `tests/test_ragdoll_collisions.cpp` (`ragdoll_collisions`) | balance, push thresholds, recovery time, get-up, following the pose, hostile input, determinism, the scene component, the example scene; limp falls, the world, joint limits, old scenes; limbs against crates and each other |
| Example | `examples/active_ragdoll/active_ragdoll.okayscene` | push, shoot, blast, knock off a ledge, watch it get up |

## How it works

**Bodies.** One capsule per humanoid bone (15 bodies, 14 joints). Masses come from the segment
fractions in Winter's table (below); capsule sizes are a fraction of the body height. The feet
have four contact spheres each (heel and toe, both sides) so standing has a real support polygon.

**Physics (`CoreRagdoll`, since claude/ragdoll-core-1).** Each bone is a body of the physics core
(`okay::phys::World`, docs/physics/physics_core.md) with one collider: a capsule, a box for the chest and the
pelvis (as wide as the shoulders and hips) and for each foot (sole to ankle). In a scene the bodies live in the
scene's own physics world as a guest (`Physics3D::JoinEngineWorld`), so the core's solver moves them together
with everything else: limbs collide with static and dynamic bodies and with each other (limbs that overlap in
the build pose, such as a hand on a thigh, never collide with each other; neither do a limb and the object's own
colliders). Each joint is a core joint: ConeTwist (a swing cone and a twist limit about the child's long axis)
or a Hinge with limits for knees and elbows, with both joint frames placed so the angles are 0 in the build pose.
The muscle is the core joint's own orientation drive. Pins (grab) and the get-up helper are core SixDof joints to
the world with springs. The controller runs in the scene's `FixedUpdate` (before each physics step) and reads
contacts after it; outside a scene (unit tests) the ragdoll steps a world of its own. Animated, the bodies are
kinematic and have no colliders. Each foot is three times its table weight (taken from the chest): a 1 kg foot
under a 70 kg body did not settle on the ground with one solver step per frame. The old XpbdRagdoll (a second
rigid-body and joint solver of the ragdoll's own) and the Verlet-particle Ragdoll were deleted in the same change.

**Drives (pose tracking).** Each joint's target is the animation's relative rotation of that bone.
Stiffness is `stiffnessPerKg x mass the joint carries x region strength` (spine, neck, arms, legs,
and a global `strength`); behaviours scale it per joint each frame.

**States.** `Animated` (the animation shows; bodies follow it kinematically) -> `Balancing` after a
push or hit -> back via `Blending` (0.35 s from the last physics pose to the animation, with the
object's root moved under the body), or -> `Falling` -> `Down` -> `GettingUp` -> `Blending`.
`Dead` is limp with no get-up; revive gets up. Mode `Powered` stays physical, `Limp` is a ragdoll.

**Behaviour modules** (each a small `DynBehaviour` with a name; switch them off on the component):

| Module | What it does | Written from |
|---|---|---|
| `HitReaction` | a struck body and its neighbours go soft for `hitRelaxTime` | joint-stiffness relaxation for hit reactions as in Zordan & Hodgins 2002 |
| `StumbleRecover` | centre of mass over the support polygon with a virtual force (feet push back); pelvis held upright through the hip drives against the stance legs; leg IK to the planted feet; a step when the capture point leaves the feet, landing where the capture point will be when the foot lands (re-planned during the swing), with the swinging foot carried by a hip force pair; the foot that can reach best steps and never crosses the other; falls if it cannot catch itself (too far, too many steps, too much lean, legs collapsed, no ground). Two **helpers** are added on top and stated as such: `balanceHelp` (a capped outside push on the pelvis toward keeping the capture point over the feet, 0.2 of body weight) and `uprightHelp` (the chest held upright from the world while stumbling). With both at 0 the body balances only with its own forces, but it falls from much smaller pushes | virtual model control (Pratt et al. 2001); SIMBICON torso and stance-hip control and foot placement from centre-of-mass position and velocity (Yin et al. 2007); capture point (Pratt et al. 2006) / extrapolated centre of mass (Hof 2008) |
| `Grab` | reaching for a wall or table top within arm's length in the fall direction, then a soft pin that lets go when pulled too far | our own; reaching for support is a protective response described in the falling literature |
| `BraceForFall` | arms reach to where the body will meet the ground, a bit apart, stiffer | protective arm extension (Ha, Ye & Liu 2012) |
| `ProtectHead` | falling backwards or the head moving fast down: hands beside the head, chin tucked | same literature; the pose is ours |
| `GetUp` | face down: all fours, kneel, rise; face up: sit, squat, rise; then blend to the animation | the poses are ours; a **helper** spring lifts and turns the pelvis and chest (stated: this is not physical; it is common practice for game characters). `helperForce` 0 turns it off (then it cannot get up) |

**Shots and blasts.** `Shot` is a hit with a small impulse at the ray's hit point (a leg shot
weakens the whole leg). `Explosion` pushes every body inside the radius away from the centre and
up, fading with distance; a strong one goes straight to `Falling`.

**Hooks.** HealthStat damage pushes the struck part (`damageImpulse` N s per point), death goes
limp (`limpOnDeath`), revive gets up. While physical, movement controllers on the object
(`IPlayerSim` controllers, NPC, click-to-move, free roam, 2.5D, top-down, shooter, Rigidbody3D)
are switched off and back on after it recovers (`handOverControl`). Scripts:
`active_ragdoll(name)` (knockdown, getup, die, revive, limp, powered, reactive, stumble),
`active_ragdoll_push(ix, iy, iz[, px, py, pz])`, `active_ragdoll_shoot(ox, oy, oz, dx, dy, dz[, power])`,
`active_ragdoll_explode(cx, cy, cz, force, radius)`, `active_ragdoll_state()`; each is also a
visual-script node `Fn.<name>` (Character Actions > Stances & Ragdoll). C++: `Ragdoll::Push`,
`Hit`, `Shoot`, `Explode`, `Trigger`, `SetMode`, `onStateChanged`, `Core()`.

## Measured on the physics core (2026-11-25, Linux, g++ Release)

- `active_ragdoll` 458 of 458 checks pass on the core (the push thresholds, recovery, get-up, hostile input and
  determinism checks below are the same checks). Recovery time after a 35 N s push: 0.88 s.
- Powered, the animation lifts an arm 70 degrees: the physical arm is 1.6 degrees off it after 2 s, still standing.
- Limp on flat ground (`ragdoll`): every part's centre stays above the floor (lowest 0.042 m), joints apart by
  under 6 mm, at most 0.065 rad past a limit while landing and 0 at rest; it settles.
- Free fall: the pelvis drops 4.99 m in 1 s.
- Against dynamic crates (`ragdoll_collisions`): a 4 kg crate is shoved 0.21 m, a 400 kg one 0.6 mm; no limb ends
  inside either; legs do not pass through each other (smallest gap -0.5 mm) or the torso.
- Two fixes found by these tests: lying on the floor the arms sank 0.09 m into it (the elbow stopped at -0.05 rad,
  and the floor needed a few degrees past straight; now -0.2 rad), and with the scene's 10 solver iterations a body
  landing on its leg pushed the shin 0.15 m into a crate under it (the ragdoll asks for 20 while it is physical,
  `Physics3D::EngineGuest::GuestSolverIterations`).

## Measured before the move (on the old XpbdRagdoll; the tests that assert these pass on the core)

- Standing powered for 4 s: pelvis within 1 % of its standing height, torso lean under 1 degree.
- A 35 N s push at the chest (0.5 m/s): stays up, back to the animation after 0.87 s (recovery time).
- 35 and 50 N s pushes in all four directions: no fall. 55 to 90 N s forward: stays up in 5 of 7 (up to
  5 steps). 200 N s (2.9 m/s) in all four directions: falls every time and gets up again every time. Between about 70 and 200 N s it depends on
  the direction (sideways falls come sooner: the stepping foot may not cross the other).
- Get-up from lying face up and face down: up and back to the animation in about 3.5 s
  (0.8 s still, 2.2 s get-up, 0.35 s blend).
- Falling forward the hands touch the ground before the head (0.62 s against 0.65 s); falling
  backwards both hands come within 0.25 m of the head (standing they hang 0.85 m away).
- Hostile input (NaN and infinite pushes, 1e12 N s, NaN poses, negative and 10 s frames, zero and
  negative mass, a broken skeleton): stays finite, joints apart by under 4 mm, no reset needed.
- The same scripted sequence (push, shot, blast) run twice gives bit-identical bodies.
- CI (another machine and compiler) gave different answers on borderline pushes. Found and fixed:
  a standing foot that bobs a millimetre made heel and toe contacts flicker, the capture point "left"
  the feet for one frame, and a 35 N s backward push stepped six times and fell. Foot support is now
  held for 4 steps after contact is lost, and the step budget refills once it stands calmly. The core
  tests pass built with g++ and clang++, -O2 and -O3, with and without fused multiply-add. The tests
  now assert properties (35 N s never falls, 200 N s always falls and gets up, a step can catch it,
  every push ends standing, the hand reaches the wall) and print the borderline counts.
- Balance near the edge is chaotic: a change in rounding (for example moving the leg IK onto the
  shared two-bone solve) can turn one 70 N s push from "stays up" into "falls". The tests therefore
  print how many of 7 pushes between 55 and 90 N s it stands through and how often it grips.
- Muscle limits are real since 2026-11-22: the drive's torque cap is in N m (on the core: the joint drive's
  `driveMaxTorque`).
- Review of #1138 (fixed, each with a test): draped over a ledge the get-up hoisted the pelvis up to 1 m
  too high (a foot on the ledge moved the "ground"; a remembered contact plane was an invisible floor
  up to 1 m past an edge); a respawn was undone; Powered mode never gave control back; a remote copy
  moved its own root.
- Get-up starts when the body as a whole is still (centre of mass), or after 4 s down; it only counts as
  up when a foot is on the ground (found in the example's frames: a body draped over the ledge never
  counted as still, and once lifted by the helper stood in the air beyond the edge; now it drops off and
  gets up below).
- On the blocky Character a push from the front of 30 N s can already need several steps (its foot
  has a short heel); the example's J and L pushes are 30 N s.

## The physics seam

The controller only uses `dyn::RagdollPhysics`: add bodies and joints, `SetDrive(joint, JointDrive)`,
impulses, forces, a pin, a helper spring, `SetActive`, `Step`, poses and contact flags. `CoreRagdoll` is its one
implementation: `SetDrive` writes the core joint's drive fields (`drive`, `driveTarget` turned into the joint's
frame, `driveStiffness`, `driveDamping` from the damping ratio and the two limbs' inertia, `driveMaxTorque`),
joint friction is extra drive damping, and a body's sphere probes ("is the heel on the ground?") are matched to
the contact points of its collider. The core gained two small things for it: `World::SetBodiesCollide` (a pair
that never collides) and `Physics3D::EngineGuest` (engine code with bodies of its own in the scene's world, which
decides what its colliders pass through).

## Control, respawn and multiplayer

- **Hand-over.** Reactive mode: the movement controllers on the object are off whenever the body is
  physical (stumbling, falling, down, getting up) and back on when it blends into the animation.
  Powered mode: the body is always physical, so the controllers stay on while it only balances and go
  off only while it falls, lies down or gets up.
- **Respawn.** When a HealthStat respawn moves the object (`Respawn`/`Revive` with Respawn on), the body
  comes back standing in the animation at the new spot (`DynamicCharacter::SnapToAnimation`), like the
  Ragdoll. A revive in place gets up from the floor.
- **Get-up tries.** A get-up that ends without a foot on the ground counts as failed; after 3 failures
  in a row it stops trying by itself and waits for `GetUp()` (`active_ragdoll("getup")`). The floor
  height for a get-up is read once when it starts (a cast under the pelvis), and only upward-facing
  contacts count as ground.
- **Multiplayer.** A copy that another peer owns (`NetworkObject::IsLocallyOwned()` false) never writes
  its root and never switches its controllers: the network places it. Its body is still simulated and
  shown on this machine from the pushes and hits it gets here, so two machines can show a slightly
  different fall. Ragdoll poses are not sent over the network (not done).

## One ragdoll, shared parts

There is one Ragdoll component. Until claude/ragdoll-core-1 there were two: a Verlet-particle `Ragdoll` (limp on
death, flinch on hits) and the `ActiveRagdoll`; if an object had both, the ActiveRagdoll switched the other off.
Old scenes load into the one component: the `activeragdoll` record (Ragdoll's record name, from its property
entry) as it was, and the old `ragdoll` record with its limp-on-death and hit-reaction switches (its particle
settings mean nothing on the core and are dropped; it stays animated until it dies, no stumbling). Both records on
one object make one Ragdoll with the `activeragdoll` settings. `compoff` and `comporder` lines naming
`ActiveRagdoll` name the Ragdoll. The old Verlet calls stay: `Activate` goes limp (as on death), `Deactivate`
stands straight back up in the animation, `IsActive` is "limp or lying down"; scripts `ragdoll()`, `ragdoll(0)`,
`ragdoll_push(vx, vy, vz)` (a push of that speed on 0.3 of the body's mass) and `is_ragdoll()`.

`Components/RagdollShared` holds the helpers: finding the 15 bones (`FindBones`), the one list of components that
drive a body (`DriverKind`: animation and IK, or movement controllers and Rigidbody3D) with `SwitchOff` /
`SwitchBackOn`, and `HealthWatch` (death, revive and hits from a HealthStat). The Ragdoll switches off movement
only while physical, because its muscles follow the animation. Rays (grab, get-up floor) go through
`ragdoll::CastIgnoringSelf` (one nearest-hit `Physics3D::Raycast` under an `IgnoreHierarchy`). The leg IK uses the
shared `Math/TwoBoneIK` solve.

## Sources

- M. Mueller, M. Macklin, N. Chentanez, S. Jeschke, T.-Y. Kim, "Detailed Rigid Body Simulation
  with Extended Position Based Dynamics", SCA 2020 - the structure of the old XpbdRagdoll (deleted).
- K. Yin, K. Loken, M. van de Panne, "SIMBICON: Simple Biped Locomotion Control", SIGGRAPH 2007 -
  torso held in the world frame through the stance hip, foot placement from centre of mass.
- J. Pratt, C.-M. Chew, A. Torres, P. Dilworth, G. Pratt, "Virtual Model Control", IJRR 2001 -
  the balance force between pelvis and feet.
- J. Pratt, J. Carff, S. Drakunov, A. Goswami, "Capture Point: A Step toward Humanoid Push
  Recovery", Humanoids 2006 - where to step.
- A. L. Hof, "The 'extrapolated center of mass' concept suggests a simple control of balance in
  walking", Human Movement Science 2008 - the same point for standing and walking.
- V. Zordan, J. Hodgins, "Motion capture-driven simulations that hit and react", SCA 2002 -
  tracking an animation with joint PD control and softening on hits.
- S. Ha, Y. Ye, C. K. Liu, "Falling and landing motion control for character animation",
  SIGGRAPH Asia 2012 - protective arm and landing behaviour.
- D. A. Winter, "Biomechanics and Motor Control of Human Movement", 4th ed., 2009, table 4.1 -
  segment mass fractions.
- Swing / twist split of a joint rotation and two-bone IK by the law of cosines: general methods.

## What is not done

- Two scene tests fail (2026-11-25): (1) `ragdoll` "table drop": face down across a table edge, the knees bend
  about 0.25 rad past their limit under the legs' weight (the joints open up to 6 cm) and the shins end up to
  0.096 m inside the table edge. The core's joint limits give under that load at 20 iterations; not fixed.
  (2) `ragdoll_collisions` "pile": dropped standing onto a pile of loose 6 kg boxes, the body lands on its feet
  and topples off onto the floor (no limb inside a box), so "lies on the boxes" and "the boxes moved 1 cm" fail;
  and the 400 kg crate's top speed is 0.001 m/s against the 0.005 the test asks (it moves 0.6 mm). The tests
  were written before the code and are left as they are.
- The unit tests' own world (outside a scene) still steps with 10 solver iterations; only the scene uses 20.

- No walking while physical: the character stands still while it stumbles (the controller is
  off), then takes over again.
- Getting up uses a helper force; a fully physical get-up is research work.
- Stepping is tuned on flat ground; stairs and slopes are unverified.
- Imported rigs map through `MapHumanoidBones` and are untested in this first version (the tests
  use the blocky Character).
