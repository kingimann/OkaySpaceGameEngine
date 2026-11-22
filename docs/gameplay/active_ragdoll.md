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
- 35 and 50 N s pushes in all four directions: no fall (0 to 3 steps). 70 N s forward: two steps, no fall.
  200 N s (2.9 m/s) in all four directions: falls every time. Between about 70 and 200 N s it depends on
  the direction (sideways falls come sooner: the stepping foot may not cross the other).
- Get-up from lying face up and face down: up and back to the animation in about 3.5 s
  (0.8 s still, 2.2 s get-up, 0.35 s blend).
- Falling forward the hands touch the ground before the head (0.65 s against 0.72 s); falling
  backwards both hands come within 0.22 m of the head.
- Hostile input (NaN and infinite pushes, 1e12 N s, NaN poses, negative and 10 s frames, zero and
  negative mass, a broken skeleton): stays finite, joints apart by under 4 mm, no reset needed.
- The same scripted sequence (push, shot, blast) run twice gives bit-identical bodies.

## The physics seam (for the new physics core)

The controller only uses `dyn::RagdollPhysics`: add bodies and joints, `SetDrive(joint,
target, stiffness, dampingRatio)`, impulses, forces, a pin, a helper spring, `Step`, poses and
contact flags. When the scene physics (branch `claude2/own-physics-engine`) has merged, a second
implementation can build `Rigidbody3D` bodies with its joints so ragdoll limbs also hit and are
hit by other dynamic bodies. What it needs from that core:

- a 6-DOF or cone-twist joint with a **spherical drive**: target rotation (child in parent frame),
  stiffness, damping, and a maximum torque;
- a hinge with angle limits (knees, elbows), swing and twist limits on the ball joints;
- collision filtering so a ragdoll's own limbs ignore each other next to a joint;
- an impulse at a world point; per-body contact flags (which spheres or shapes touch);
- a world-anchored soft spring (position and rotation) for the get-up helper, and a soft point pin;
- bit-identical stepping for the same inputs (our determinism test does this today).

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
