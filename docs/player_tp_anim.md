# Third-person body animation of the Player Controller

Branch `claude/player-tp-anim-1`. The owner's request: "third person controller needs a lot more work, especially the
hands and animations". This page says what was there, the gap that was found, what was added and how it was measured.
Try it yourself: [try_it.md](try_it.md), entry `7tp`.

## What existed (read first, extended, not copied)

* `Character::DriveLocomotion` and `AdvanceAnimClock` (`engine/src/Components/Character.cpp`): the state (idle, walk,
  run, crouch, prone, jump, fall) from the measured ground speed, a stride laid out from the real speed, the gait
  direction (strafe: hips and legs turn up to 75 degrees, the rest is a side-step; backpedal: the phase runs
  backwards), turn-in-place foot step, takeoff crouch, landing absorb, motion lean.
* `HumanoidMotion` (`engine/src/Animation/HumanoidMotion.cpp`): the stride, hip bob, knee bend, arm swing against the
  legs, heel-to-toe, and the default clips baked from it in eight directions (`HumanoidLibrary.cpp`).
* Holding: `ProceduralObjectHolding` and `HoldableObject` (styles pistol, rifle, box, flashlight, sword, cup, heavy,
  one hand, two handed, carry in front, over the shoulder; grips; support hand; body lean under load) driving the
  Full Body IK. Foot IK on the blocky character. The third-person camera (spring arm with collision, shoulder
  offset, aim distance and field of view). Network: the anim state, speed, angle, look yaw and pitch are in the
  `NetworkSync` message.
* Tests: `test_player_feel` (foot slip walking and running), `test_locomotion`, `test_gait_continuity`,
  `test_strafe_blend`, `test_object_holding`, `test_walk_vs_run`.
* `origin/claude/player-feel-2` (not merged to main; it is 64 files, many outside the animation): its
  `tests/test_player_feel2.cpp` measured the stop and wrote "target not met: feet slid 0.63 m for 0.35 m of body
  travel". Its fixes (landing and crouch follow the collider) were not taken in this branch; the stop was fixed here.

## The gap that was found

| Gap | Status in this branch |
|---|---|
| Stopping from a run slides the planted foot (the stride shrinks with the speed within a few frames) | fixed: stop step (see animation.md "Stopping"); measured in `test_player_tp_anim` |
| Arms swing the same forward-and-back amount in a sidestep and a backpedal | fixed: eight-way weights set the swing and a chest roll |
| While aiming or holding, only the head follows the look | fixed: upper-body aim (chest takes 45%) |
| No test of the eight directions, the continuity of the pose, the camera or the held poses | added: `tests/test_player_tp_anim.cpp` |
| No place to try it in the release | added: `examples/player/third_person_anim_lab.okayscene` |
| Hands are one block with no fingers; the grip `curl` is stored and not applied | NOT done (needs a finger mesh and bones on the blocky body) |
| Over-the-shoulder side swap key, a per-shoulder camera collision test | not done (the offset and its sign are tested) |
| Mixamo clip graph stop step | not done (imported rigs keep their clips' own stop) |

## Numbers

Printed by `test_player_tp_anim` (`[tp_anim]` lines; Linux, headless, built with -O0, 120 fps steps, 4399 checks, 0 failures):

| Measure | Before | Now |
|---|---|---|
| Stop from a run: planted feet slid / body moved | 0.687 m / 0.348 m | 0.368 m / 0.348 m (bar: slide <= travel + 0.10 m); still after 0.81 s |
| Walking 2 m/s in 8 directions, planted-foot median speed | n/a | 6 to 8% of ground speed (bar 30%) |
| Biggest one-frame bone rotation, idle/walk/run/stop/back/run (120 fps) | n/a | 14.8 degrees (a running shin; bar 16) |
| Turn in place, planted-foot median speed | n/a | under 0.25 m/s |
| Chest aim: torso pitch with aim on vs off, gaze (torso + head) | head only | torso -0.6 to -17.7 degrees, gaze unchanged (-38.6) |
| Camera: open / wall behind / aiming / shoulder swing | n/a | 4.76 m / 2.51 m / 2.19 m / 1.80 m; offset change per frame while running 0.0000 m |

**Known gap found, not fixed:** taking off from a stand, the shin rotates about 37 degrees in one frame (120 fps) when the
Animator's blend into the Jump pose ends: the bone angle goes past 90 degrees and the clip writes it as a flipped Euler triple
(70.4, -180, -180). The test prints it and only guards against it getting worse (bar 45). Landing is 10 degrees per frame.
The takeoff crouch (`jumpCrouch*`) adds to that bend; `test_character_feel` requires it to be at full weight at takeoff, so it was not eased in.
