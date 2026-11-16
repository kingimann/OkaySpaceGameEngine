# Walk versus run (the natural walk and run)

The owner's words: "the character walking is basically running. could you fix it so its different." This page says what was
wrong, what the numbers were before and are now, what changed, how to try it, how to get the old look back to compare, and
what is not done. Try it in the Windows release: [try_it.md](try_it.md), row **7r**.

## 1. What was wrong (measured before any change)

Plain W moved the player at **4.5 m/s** and Shift at **8.0 m/s**. A person walks at about 1.4 m/s and tops out near 2; 4.5 m/s
is a jog and 8.0 a sprint. So both looked like running, and the built-in character stepped like this
(`tests/test_walk_vs_run.cpp`, run with `OKAY_OLD_GAIT=1`, the same program that checks the new gait; 60 frames a second, feet read
from the skinned mesh, 8 s after a 3 s settle):

@@BEFORE@@

How to read the table: `steps` are steps a minute, `step` the metres of ground per step, `flight` the share of the time both feet are
off the ground, `double` the share both are on it, `arm`, `elbow` and `lean` degrees, `bounce` the hips' up and down in centimetres.

Why. Four things added up:

1. **The default speeds were a jog and a sprint** (the controllers: 4.5 and 8.0; the shooter 4.5 and 7.5).
2. **The walk stride was tuned for a person hurrying at up to about 1.8 m/s and turned frantic above that.** The planted foot cannot
   sweep more than about 0.96 m under the hips, so the walk raised its cadence with the speed (270 steps a minute at 4.5 m/s,
   about 350 at 5.5), faster than the run's own (250).
3. **The walk-to-run blend started too early.** `Character::DriveLocomotion` began mixing in the run's style at 65 percent of the
   controller's walk speed and reached a full run at the run speed, so plain W at the default speed was already about a third of a run.
   The legs of that mix floated: both feet were off the ground for over 40 percent of every step.
4. **A new project plays baked clips, not the live stride.** `Templates::BuildPlayerBody` adds the default Animator graph
   (`HumanoidLibrary::EnsureAnimator`), whose clips are baked from the same stride laws and picked by ground speed. At 4.5 m/s that was
   the "Walk 4.5" clip, a jog, and Shift played the 8 m/s sprint. (`DriveLocomotion` only feeds that graph its parameters.)

## 2. The natural targets for this rig, and why

The rig is a blocky 1.8 m person with a leg of **0.80 m from hip to ankle** (0.92 m from hip to sole). These are general facts of
human gait, scaled to that leg; nothing here comes from another project.

| Measure | Walk (W at the default walk speed) | Run (Shift at the default run speed) | Gap the test asserts |
|---|---|---|---|
| Steps a minute | 90 to 135 (a stroll 85, an easy walk 110, brisk 125 to 130; past that people jog) | 150 to 200 (a jog 160, a quick run 185) | at least 40 more |
| Step length | up to 1.0 m | 1.4 m and more | at least 1.6 times |
| Both feet off the ground (flight) | none (under 1 percent): a foot is always down | 15 percent and more | at least 15 points more |
| Both feet on the ground (double support) | 5 percent and more | none (under 1 percent) | the walk has more |
| Arm swing (half the range) | up to 25 degrees, calm | 35 degrees and more | at least 1.5 times |
| Elbow bend | up to 25 degrees, nearly straight | 70 degrees and more, near a right angle | at least 45 degrees more |
| Forward lean of the chest | up to 4 degrees | 8 degrees and more | at least 4 degrees more |
| Hip bounce (up and down) | up to 10 cm | more than the walk | at least 2 cm more |

Two limits come from the leg, not from taste. First, a person's walk turns into a run at a Froude number v squared over g times
leg length of about 0.5, which for a 0.92 m leg is **2.1 m/s**: the top of a natural walk. Second, a planted foot can only sweep as far as
the leg reaches before the hips would have to drop too far (the hips dip about 8 cm at a sweep of 1.0 m and 14 cm at 1.1 m). A brisk
walk is therefore about 130 steps a minute at 2 m/s, and anything faster needs the run's style.

## 3. What changed

| Part | What it does now | File |
|---|---|---|
| Default speeds | First Person and Third Person walk **2.0**, run **5.5** m/s (were 4.5 and 8.0); the Third Person Shooter walks 2.0 and runs 5.0 (was 4.5 and 7.5). `Character::Locomotion` has the same defaults. | `Components/NaturalGait.hpp` (the numbers), the three controllers, `Components/Character.hpp` |
| Crouch and prone speed | 1.0 and 0.5 m/s (were 2.2 and 1.1): crouching must stay under the walk. The shooter's `StanceControl` starts the same. | the controllers |
| Walk stride | Calmer: the stance shrinks as the speed rises (double support is gone at 2.5 m/s, where a walk ends), the planted foot lands closer and pushes off farther back with a longer heel rise, and the ankle **rolls over the planted foot** (it no longer stays pinned while the foot pitches, so the sole does not slide). The hips bob about 8 cm instead of 14. | `Animation/HumanoidMotion.cpp` (`GaitForNatural`, `RockerDrift`) |
| Run stride | Cadence 160 steps a minute at 2.5 m/s rising to about 190 at 6, a longer stance, so a run has flight but steps like a runner, not a sprinter (it stepped 227 times a minute at 5.5 m/s). | `Animation/HumanoidMotion.cpp` |
| Walk-to-run look | Follows the **real ground speed** (a walk up to 2.4 m/s, a jog from 3.4, a smooth change between), not the controller's settings. A controller whose walk is 2 m/s shows a walk, one whose "walk" is 4.5 shows the jog it really is. The run state keeps its own hysteresis (starts at 1.15 times the walk speed, ends at 1.05), so nothing flickers. | `Components/Character.cpp` (`DriveLocomotion`) |
| Baked clips | The default Animator graph's clips come from the same laws; a 3 m/s run clip was added so a slow sprint or a remote player the owner calls a run at 3.2 m/s is not a 4 m/s clip in slow motion. | `Animation/HumanoidLibrary.cpp` |
| Footsteps | Third Person footsteps land once per step of the Character's gait (the first-person bob already did): the sound keeps time with the feet at any speed. | `Components/ThirdPersonController.hpp` |
| Imported rigs | Attaching a character model still fits the controller's speeds to the model's clips when they were never changed; now the old defaults a scene saved earlier spells out (4.5, 8.0, 7.5) count as never changed. The clip choice by speed and its playback-rate cap (0.6 to 1.6 times, the walk clip alone up to 1.25 times its own speed, the run clip beyond that) were already there and are checked by a test. | `Render/ModelImport.cpp`, `Components/ModelAnimator.hpp` (unchanged) |
| Remote players and NPCs | Unchanged thresholds: a remote avatar runs from 3.2 m/s, an NPC runs at its own run speed. Both now get the natural stride. | (no change) |
| Inspector | A button **Use natural walk and run speeds** on the Player Controller, on a bare First or Third Person controller and on the Third Person Shooter, and a hint under Walk Speed when it is above 3. The Tactical and Grounded Feel presets start at the natural walk. | `editor/main.cpp` |
| The way back | `OKAY_OLD_GAIT=1` (section 5). | `HumanoidMotion::OldGait()` |

### Older projects

A saved scene spells its speeds out, so an older project looks the same after the update until its owner presses the button. That is on
purpose: **speeds are never changed silently on load.** In the Inspector, select the player, open **Player Controller** and press **Use natural
walk and run speeds** (Ctrl+Z undoes it). It sets Walk Speed to 2.0 and Run Speed to 5.5 (5.0 for the shooter), and Crouch and Prone Speed to
1.0 and 0.5 only if they are still the old 2.2 and 1.1.

An older project that keeps 4.5 and 8.0 now shows an honest jog for W (the legs follow the real speed) and a sprint for Shift, not the frantic
half-and-half it showed before.

**Why the project upgrade does not do it** (the question the owner asked in point 4 of the brief). The upgrade (`editor/ProjectUpgrade.*`,
[project_upgrade.md](project_upgrade.md)) rewrites scenes only through the scene format migrations (`SceneMigration`, one registered step per
`okayscene N -> N+1`). A speed change is data, not format, so doing it there would mean:
1. a new migration step and a bump of `okayscene` (every scene saved afterwards would say N+1 and an older editor, which the launcher's "Keep
   v2.850.x" depends on, would refuse them);
2. a step that finds the `pcfirst`, `pcthird`, `fpctrl`, `tpctrl` and `tpsctrl` records that still hold exactly 4.5 and 8.0 (or 4.5 and 7.5), plus their
   crouch and prone speeds, and lists each change in the result window (the log already has a place for it);
3. an opt-in in the upgrade window ("Also switch to the natural walk and run speeds"), because it must never run without being asked, and a test.
That is a format decision for the coordinator and the owner, not a small change, so it is **not built**.

## 4. Before and after

All rows: the built-in character, 60 frames a second, feet read from the mesh. "New project" is the Character with the default Animator
graph (what a template gives); "procedural" is the Character on its own (NPCs, scripts).

@@AFTER@@

@@PICTURES@@

## 5. Compare with the old gait in the same build

Set the environment variable **`OKAY_OLD_GAIT=1`** before starting the program (the editor, the player or a test). It brings back, together:

* the old stride laws (`GaitForOld`: byte for byte the old numbers; checked against a build of the code before the change),
* the old walk-to-run blend in `Character::DriveLocomotion`,
* the old default speeds for every controller made while it is set: 4.5 and 8.0 (4.5 and 7.5 for the shooter), crouch 2.2, prone 1.1,
* nothing else: imported rigs, remote players and NPCs have no old behaviour of their own to bring back (they never changed).

In a Windows command window: `set OKAY_OLD_GAIT=1` and then `Tools\OkayEngine.exe`; in PowerShell: `$env:OKAY_OLD_GAIT = "1"` and then `.\Tools\OkayEngine.exe`.
To go back, close the editor and start it again without the variable.

**How to tell which one is on:** (1) a new Player Controller shows **Walk Speed 2 and Run Speed 5.5** with the natural gait and **4.5 and 8** with the old one;
(2) with the old gait on, the Inspector shows an amber line under the button ("The OLD walk and run is on (OKAY_OLD_GAIT is set) ..."); (3) the program prints one line
to stderr, `[okay] gait: OLD walk and run (OKAY_OLD_GAIT is set) ...`, when it is on.

The variable is read once, when the program starts. The baked clips of the default Animator graph are made once per run, so it cannot be changed half way:
use a fresh start to compare. (A test switches the procedural stride in code with `HumanoidMotion::SetOldGait`, which does not re-bake the clips.)

## 6. What is not done, and what is unverified

* **Not run on Windows, and not seen on a GPU renderer.** Everything above is measured on Linux with a headless test. The pictures are software renders
  (the engine's own software rasteriser), not screenshots of the Windows player. The Direct3D renderers draw the same poses; the poses themselves come from the code that was tested.
* **The feel in a real game is unverified.** 2.0 m/s is a real walk, and it is slower than the old 4.5. The numbers are in one place (`NaturalGait.hpp`) and in the Inspector: change them if a game wants a
  faster walk (above about 2.4 m/s the legs start to jog; that is real, not a bug). This is a decision for the owner.
* **No real Mixamo model was used.** The imported-rig checks use the test rigs in `tests/assets/rigs` and synthetic leg-swing clips (`test_mixamo_locomotion`).
* **Other controllers keep their numbers** (2.5D platformer 5.0 and 7.5, top-down 5.0 and 8.0, top-down shooter 6.0 and 8.5, click to move 4.0 and 7.0, `CharacterController3D`, NPC 2.5 and 4.5). Their W now shows the jog it really is.
  Say if they should get natural numbers too.
* **No automatic upgrade of saved projects** (see "Older projects"): the button is the way.
* **The walk's bounce is about 8 cm, not the 3 to 5 of a person.** The leg model is straight-legged with a rolling foot and no stance-knee flex or pelvic tilt; getting lower would need those.
* The run is a little floaty at the top: flight is about 45 percent of a step at 5.5 m/s (a person's is about 35).

## 7. What each new file was written from

* `engine/include/okay/Components/NaturalGait.hpp`: the general facts above (a walk tops out near 2 m/s, a jog is 2.5 to 3.5, a quick run 5 to 6) and the existing controllers' fields.
* `engine/src/Animation/HumanoidMotion.cpp` (the new laws and `RockerDrift`): the foot's own shape (toe 0.20 ahead of the ankle, heel 0.10 behind it, ankle 0.12 above the sole) worked into geometry, the cadence and stance
  figures of human gait, and measurements of this rig's feet. No other project's code, tables or constants.
* `tests/walk_vs_run_measure.hpp`, `tests/test_walk_vs_run.cpp`, `tests/walk_vs_run_strips.cpp`: the engine's own `Character`, `Raster` and `ImageWrite`; the targets are section 2.
