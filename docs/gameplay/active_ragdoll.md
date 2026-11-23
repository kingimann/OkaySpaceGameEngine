# Active Ragdoll (our own dynamic character)

> **What this is:** a humanoid whose body is simulated: it follows its animation with joint
> "muscles", keeps its balance when pushed, steps to catch itself, falls when it cannot (arms
> out, head protected), lies still, gets up face up or face down, and blends back into the
> animation. It is our own system, written from published research and general control methods.
> No code, data or assets from any commercial product were used or looked at.
>
> **Status:** first version (2026-11-22). Works in the unit tests on a flat floor and in the
> example scene; see "What is not done".

## Where it lives

| Part | File | Job |
|---|---|---|
| Physics layer | `engine/include/okay/Animation/RagdollPhysics.hpp`, `engine/src/Animation/RagdollPhysics.cpp` | `RagdollPhysics` (the thin interface the controller uses) and `XpbdRagdoll` (our solver) |
| Controller and behaviours | `engine/include/okay/Animation/DynamicCharacter.hpp`, `engine/src/Animation/DynamicCharacter.cpp` | states, drives, balance, the six behaviour modules |
| Component | `engine/include/okay/Components/ActiveRagdoll.hpp`, `engine/src/Components/ActiveRagdoll.cpp` | bones, scene rays, HealthStat, controllers, root |
| Tests | `tests/test_active_ragdoll.cpp` (suite `active_ragdoll`) | balance, push thresholds, recovery time, get-up, hostile input, determinism, the scene component, the example scene |
| Example | `examples/active_ragdoll/active_ragdoll.okayscene` | push, shoot, blast, knock off a ledge, watch it get up |

## How it works

**Bodies.** One capsule per humanoid bone (15 bodies, 14 joints). Masses come from the segment
fractions in Winter's table (below); capsule sizes are a fraction of the body height. The feet
have four contact spheres each (heel and toe, both sides) so standing has a real support polygon.

**Solver (`XpbdRagdoll`).** Each 1/60 s frame is cut into 12 substeps. Each substep predicts the
poses, runs one pass of corrections (joint drives, swing and twist limits, hinges for knees and
elbows, the joint points meeting, pins, helpers, contacts with friction), derives velocities from
the pose change and then runs a velocity pass (drive damping, joint friction, resting contacts).
A drive is an angular correction toward the target with compliance 1 / stiffness, which acts as
the P of a PD controller; the velocity pass gives the D, scaled to a damping ratio. Contacts use
one ray per sphere per frame (the surface plane is kept for the substeps). Non-finite states are
caught and the bodies go back to their last good pose (`Resets()` counts it).

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
visual-script node `Fn.<name>` (Character Actions > Stances & Ragdoll). C++: `ActiveRagdoll::Push`,
`Hit`, `Shoot`, `Explode`, `Trigger`, `SetMode`, `onStateChanged`, `Core()`.

## Measured (unit tests, 1.8 m, 70 kg rig on flat ground)

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
- Muscle limits are real since 2026-11-22: the drive's torque cap (and its damping) is in N m
  (XPBD multipliers are positional, torque = lambda / h^2); before, the cap was 720 times too loose.
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

## The physics seam and the plan for the physics core

The controller only uses `dyn::RagdollPhysics`: add bodies and joints, `SetDrive(joint, JointDrive)`,
impulses, forces, a pin, a helper spring, `Step`, poses and contact flags. `JointDrive` is field for
field the drive of the scene physics' Ball / ConeTwist `Joint3D` on branch `claude2/own-physics-engine`
(`useDrive`, `driveTarget` = the turn from the build pose, `driveStiffness`, `driveDamping`,
`driveFrequency`, `driveDampingRatio`, `driveMaxTorque`), so the two speak the same language.

**`XpbdRagdoll` is a second rigid-body and joint solver, a copy, and it will be deleted.** The engine is
getting one physics path: the new core's joints become the only joint solver (branch
`claude2/physics-no-classic`). Nothing new is built on `XpbdRagdoll`. The next step, once that branch lands:
1. Write `CoreRagdollPhysics : RagdollPhysics`: one `Rigidbody3D` + `CapsuleCollider3D` per bone on hidden
   child objects, one ConeTwist `Joint3D` per joint (knees and elbows Hinge), `SetDrive` copying
   `JointDrive` into the joint's drive fields (`driveTarget = target.ToEuler()`), contacts from the bodies'
   collision callbacks.
2. Switch `DynamicCharacter` to it (one line: `m_phys.reset(new CoreRagdollPhysics(scene))`) and run
   `active_ragdoll` on it.
3. Delete `XpbdRagdoll` (and `SimpleWorld`, which only feeds it in the unit tests; those tests move to a
   scene).
4. What it needs from the core: drives that honour the max torque including damping, a hinge with
   limits, collision filtering between neighbouring limbs, an impulse at a point, per-body contact
   flags with normals, a world-anchored soft spring (get-up helper) and a soft point pin (grab),
   bit-identical stepping.

Tracked in `docs/status/UNFINISHED.md` ("ActiveRagdoll on the physics core joints").

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

## One ragdoll per body, shared parts

`Components/RagdollShared` holds what `Ragdoll` and `ActiveRagdoll` share: finding the 15 bones
(`FindBones`), the one list of components that drive a body (`DriverKind`: animation and IK, or
movement controllers and Rigidbody3D) with `SwitchOff` / `SwitchBackOn`, and `HealthWatch` (death,
revive and hits from a HealthStat). A limp `Ragdoll` switches off both kinds on the whole body; the
`ActiveRagdoll` switches off movement only, because its muscles follow the animation. Since this
change a limp `Ragdoll` also switches off Click To Move, Free Roam, 2.5D platformer, Top Down and
both shooter controllers (before, only `IPlayerSim` controllers and NPCs; a Click To Move controller
kept steering a limp body; a controller with a follow camera also stops moving that camera while the
body is limp). If an object has both, the ActiveRagdoll takes over at Start: it switches
the Ragdoll off and logs it, and the Ragdoll's Inspector says so; switching the ActiveRagdoll off (or
removing it) switches the Ragdoll back on. Both components cast through `ragdoll::CastIgnoringSelf`
(one nearest-hit `Physics3D::Raycast` under an `IgnoreHierarchy`), and the ray tests against boxes and
body capsules use `LocalShape3D::Ray` (Physics/ShapeKernel3D). The leg IK uses the shared
`Math/TwoBoneIK` solve and the twist limit the shared `Math/JointLimit` `TwistAbout`.

## Sources

- M. Mueller, M. Macklin, N. Chentanez, S. Jeschke, T.-Y. Kim, "Detailed Rigid Body Simulation
  with Extended Position Based Dynamics", SCA 2020 - the solver structure.
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

- Limbs do not collide with each other or with other dynamic bodies (only with static scene
  colliders and terrain through rays); waits for the physics seam above.
- No walking while physical: the character stands still while it stumbles (the controller is
  off), then takes over again.
- Getting up uses a helper force; a fully physical get-up is research work.
- Stepping is tuned on flat ground; stairs and slopes are unverified.
- Imported rigs map through `MapHumanoidBones` and are untested in this first version (the tests
  use the blocky Character).
