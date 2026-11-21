# Player audit: bugs, frame cost and what was done (branch `claude/player-deep`)

> **What this is:** The owner asked: "look into it deeper: the physics, the player, how the terrain works, and fix bugs, fps problem and improve".
>
> **Status:** Internal record (audit, plan or status): read it as a dated record, newest facts in the code.
>
> **Last checked:** 2026-11-20 (moved into `status/`, paths and links checked; the text itself was not re-checked against the code).

The owner asked: "look into it deeper: the physics, the player, how the terrain works, and fix bugs, fps problem and
improve". This page is the player part: the First Person and Third Person controllers (the Player Controller), the
Character Controller 3D, the camera, and how the body behaves on the physics, the platforms, the water and the
terrain. Physics, terrain digging and the walk/run gait are other branches' lanes.

Everything below was measured with small headless programs (no window, no GPU, Linux, not Windows):
`tests/probe_player_audit.cpp` (one line of numbers per case) and `tests/bench_player.cpp` (microseconds per frame).
Both build with the engine; the probe prints `ok`, `BUG` or `limit` after a case id. Its `BUG` labels are rough
thresholds: where the probe's own rule was wrong this page says so (E06). "Fixed" means a `player_deep_*` check in
`tests/test_player_deep.cpp` fails without the fix and passes with it.

How to see the old behaviour again: set the environment variable `OKAY_OLD_PLAYER=1` before starting the player, the
editor or any test. It brings back the earlier per-frame work and the earlier body behaviour for every change on this
page (the settings that are now saved are the one exception: a scene saved by the new build keeps them).

## Top findings

1. **The player cost far more per frame than it needed to, and the cost grew with the size of the scene.** One first
   person player took about 113 microseconds a frame in a 200-collider scene and about 1160 in a 2000-collider scene
   (a 60 fps frame is 16667). The causes: six physics queries a frame to "ease out of walls" even in open ground, five
   rays a frame for stair stepping while walking, two searches of the whole scene for water, two searches of the
   whole scene for pickups and bag keys every frame just to read the Q and E lean keys. After the change: about 20
   and about 300 microseconds. Details in "Frame cost".
2. **Avatars of other players (not locally controlled) still ran the player's wall and camera code every frame**
   (E24): a first-person remote avatar overlapping a wall was shoved 0.64 m by the local controller (the network is
   the only thing that should place it), and every remote avatar cost 116 to 145 microseconds a frame (100 of them:
   13 to 17 ms a frame). Fixed: the cost is now about zero; the frame with 100 remote avatars went from 13.4 ms to
   1.6 ms (the rest is the avatars' own animation and sync, not the controller).
3. **A floor whose pivot is not under the player's feet gave one jump and no more** (E28): the controllers decided
   "is this ground" by comparing the other object's pivot height with the player's. Fixed: they read the contact
   normal.
4. **A player standing still when the floor disappears (a trapdoor, a destroyed floor) hovered in the air** (E27),
   because an idle body sleeps and nothing woke it. Fixed.
5. **Stairs down: the box body hopped from tread to tread** (E07): in the air on 45 to 49% of frames walking down 12
   steps. Fixed: 0%.
6. **Walls grab you** (E02): walking at a wall at 15 to 45 degrees slid along it at 28 to 36% of the speed a smooth
   slide gives (steering cancelled the slide each frame, contact friction slowed it more). Fixed: 90 to 100%.
7. **Slopes**: standing still on a gentle slope crept 0.5 to 1.3 m in 3 seconds (heightmap 25 to 49 degrees, box and
   mesh ramps 40 to 49 degrees) (E05). Fixed for box, mesh and heightmap ramps (see the table for the one remaining
   case). The slope limit also never applied to box and mesh ground; it now reads the contact normal.
8. **Jumping off a moving platform lost the platform's speed** (E12b): 0 m/s kept of 3. Fixed: the whole 3 m/s.
9. **Settings that existed but could not be changed or saved**: the First and Third Person controllers have a Slope
   Limit, Step Offset (only the Third Person panel showed it, and it was lost on save), Fall Reset Y and the swim
   speeds. None was written to the scene file; Slope Limit was not in any inspector. Fixed: saved, in the property
   list, and both panels show Step Offset and Slope Limit.

## Findings table

Id is the probe case. Test is the `player_deep_*` check (all in `tests/test_player_deep.cpp`).

| Id | What was tried | Result before | Test | State |
|---|---|---|---|---|
| E01 | walk straight into a wall, 30 and 144 fps, fixed and variable step | ok: stops at the face, no jitter | | ok |
| E02 | walk into a wall at 15 to 80 degrees | BUG: slide 28 to 36% of a smooth slide | `player_deep_wall_slide` | fixed |
| E04 | sprint at a 2 cm, 10 cm and 50 cm wall at 30, 60, 144, 240 fps, both step modes | ok: never through (the body is boxed in by the fixed physics step) | | ok |
| E05 | ramps 10 to 75 degrees (heightmap, box, mesh): stand still, climb, jump | BUG: creeps while standing on 25 to 49 degrees; the limit was only enforced on heightmap | `player_deep_slope_hold` | fixed; ramps above 50 degrees slide the body back (as the limit says) |
| E06 | steps 0.05 to 1.0 m, walk and run, box and mesh | ok: steps up to 0.35 m are climbed, 0.45 m and higher block. (The probe's own "blocked" label for 0.15 to 0.35 is wrong: the body climbed and walked off the far side, see `reached top at`) | | ok |
| E07 | 12 stairs of 0.18 m, up and down, box and capsule body | BUG down: not on the ground on 45 to 49% of frames (box body); up ok | `player_deep_stairs_down_grounded` | fixed (0%) |
| E08 | stand with the body centre past a ledge edge | limit: a box body still stands with its centre 0.29 m past the edge (a capsule falls at 0.15 m) | | not fixed (a box balances on any overlap; the template body is a box) |
| E09 | crouch under a 1.3 m roof, ask to stand, walk out | ok: stand request refused, stands after leaving | | ok |
| E10 | jump under a ceiling, also a ceiling whose pivot is below the player | ok: no air jumps | | ok |
| E11 | walk and run over 40 flush boxes, boxes with 1 cm gaps, 2 mm lips, a mesh grid | ok: grounded on every frame, no height noise | | ok |
| E12 | ride a kinematic or script-moved platform sideways at 3 and 8 m/s, up at 2 m/s, a turntable | ok sideways and turning (no slip); BUG riding UP on a kinematic body: not grounded on 25 to 30% of frames, height wobble 6 cm | | not fixed (see below) |
| E12b | jump off a platform moving 3 m/s | limit: 0 m/s kept | `player_deep_platform_momentum` | fixed |
| E12c | a kinematic wall squeezes the player against a static wall | BUG: the player ends up 3.5 m inside and past the static wall | | not fixed: physics (kinematic against dynamic contact), reported |
| E13 | push crates of 1 to 1000 kg, be hit by a thrown crate, a 50 kg crate on the head | info: pushing and being pushed behave; a 50 kg crate dropped from 6 m pressed the player 0.3 m into the floor before it recovered | | not fixed (physics) |
| E14 | start 0.1 to 3 m inside the floor, inside walls | BUG: embedded 1.5 m or deeper in the floor, or in a 6 m thick wall, the body fell out of the world (to Fall Reset Y); up to 0.9 m it pops out in 0.05 to 0.1 s with no launch | | not fixed (needs a solid-fill test in physics) |
| E15 | fall 20 and 100 m onto 1 m, 10 cm, 2 cm boxes, a mesh plane, heightmap, at 30 to 240 fps, both step modes | ok: never tunnelled, 44 m/s at 100 m | | ok |
| E17 | walk, sprint, jump height at 30, 60, 144, 240 fps | fixed step (what the player and editor Play use): walk 8.77 to 8.82 m (0.7%), jump 1.774 to 1.775 m (0.1%). Variable step (tests and tools): jump 1.736 to 1.822 m (4.7%), stop distance 0.27 to 0.41 m | | ok in the runtime |
| E18 | third-person camera: wall 0.8, 1.5, 3 m behind, a fence, a 1.2 m corridor | ok: the lens never inside a solid, stays 0.49 m off the wall, no pumping at the fence | | ok |
| E19 | fall into a pool and swim out onto a bank | limit: swims at 2.2 m/s and floats at the surface, but cannot climb out onto any bank (the feet hang 1.2 m under the water line, a step is 0.35 m). (The beach case in the probe is not a valid ramp; ignore it) | | not fixed |
| E20 | voxel terrain: walk, dig the floor away, add a mound over the player, bury the whole body | ok: grounded every frame, falls 1.3 s after the floor is dug, pushed out in about 1 s, no stuck case | | ok |
| E21 | walk off the edge of a 50 m heightmap | info: falls to Fall Reset Y (-100) and is returned to the start after 4.5 s | | ok |
| E22 | player scale 0.5 and 2 | BUG: the collider centre offset is not scaled, so a 2x player floats 0.9 m above the floor and a 0.5x player sinks 0.45 m | | not fixed: `Collider3D::WorldCenter` (physics header), reported |
| E23 | teleport 60 m while running, first and third person; fall reset | ok: no camera swoop, the camera is back on the body in the first frame | | ok |
| E24 | remote avatars: third person in free space, first person overlapping a wall, both views while the local player presses V | BUG: the wall case moved the remote avatar 0.64 m | `player_deep_remote_not_eased` | fixed (also camera placement and the V key for non-local avatars) |
| E26 | V toggle first/third/first, with a wall behind | ok: biggest camera step 0.36 to 0.5 m per frame, lens never in a wall | | ok |
| E27 | floor destroyed, collider disabled or object switched off under an idle player | BUG: hovers | `player_deep_floor_gone_falls` | fixed |
| E28 | floor with its pivot 5 m above the surface: four jumps | BUG: 1 of 4 | `player_deep_pivot_floor_jump` | fixed |
| E29 | which way is the contact normal | info: it points from the body toward the other collider (down on a floor, `(0,-0.87,-0.5)` on a 30 degree ramp), the same for box and mesh | | used by the fixes |
| settings | Slope Limit, Step Offset, Fall Reset Y, swim speeds saved and shown | BUG: not saved, Slope Limit not shown | `player_deep_settings_saved` | fixed (the old build loses them; this check cannot fail with `OKAY_OLD_PLAYER=1` because saving has no switch) |
| equal results | the new early-outs against the old queries over random scenes (400 poses) | | `player_deep_same_answers` (the random-scene block) | the answers are identical |
| numbers | the player's own numbers on screen | no such thing | `player_deep_debug_lines` | new (F3, see below) |

## Frame cost

Machine: shared 4-core Linux box, load average 2 to 4 during every run, `-O3 -DNDEBUG`, 60 fps frames, 50 Hz
fixed physics with smoothing (as the player and the editor Play run), the template body (blocky Character, animator,
box collider), the player walking. The number is the median `Scene::Update` time with the controllers on minus the
same scene with the controllers switched off, so it is what the controllers (and the Player View) add. Numbers move
10 to 20% with the machine load; read the big differences, not the small ones.

| Case (200 colliders) | Before (us) | After (us) |
|---|---|---|
| 1 first-person player, walking | 113 | 19 to 21 |
| 1 first-person player, idle | 97 | 20 |
| 1 third-person player | 137 | 35 |
| 1 player with both views | 165 | 58 |
| 50 local first-person bodies, per body | 182 | 25 |
| 100 remote avatars, per avatar (first person) | 116 | about 0 |
| 100 remote avatars, per avatar (third person) | 145 | about 0 |
| Character Controller 3D, 1 / 50 / 100 bodies | 31 / 9 / 9 | 8 to 15 / 8 / 9 (little to gain) |
| NPC Controller, 100 NPCs | 11 | 9 (little to gain) |

| Case (2000 colliders) | Before (us) | After (us) |
|---|---|---|
| 1 first-person player, walking | 1160 | 305 |
| 1 first-person player, idle | 787 | 216 |
| 1 third-person player | 1025 | 269 |

What a first-person frame did before (callgrind, instruction counts over 100 frames, share of `Scene::Update`):
`EaseOutOfSolids` 12.8% (two box overlap queries that each search every object of the scene, plus four rays, every
frame even in open ground), `Physics3D::Raycast` 17.9% (stair probe: five rays while walking, one ground ray for the
platform, four from the ease-out), `Water::ProbeAt` 3.0% (twice a frame, a search of every object each time),
`LeanKeyTaken` (two searches for pickups and use prompts every frame in the 3rd person case, 51% of a remote
third-person avatar's update). What was done, with the same answers (tests below):

* `EaseOutOfSolids`: one bounds test over a cached list of the scene's colliders (`AnySolidNear`) decides "nothing
  solid near"; the six queries run only with a solid within about a metre.
* `TryStepUp`: the same bounds test over the stair band; scenes that contain a heightmap Terrain keep the five rays
  (a ray can hit a terrain, a bounds test cannot see it).
* Water: the controllers keep the scene's list of water volumes (rebuilt only when objects or components change);
  the fall-reset check asks about water only once the body is below the line.
* Lean keys: a key that is neither down nor just pressed no longer searches the scene for who owns it.
* Remote avatars: no wall easing, no camera placement, no V key.

What is still per frame and not changed: one ground ray for the platform, the animation and IK work of the Character
(the same with the controllers off), `Physics3D::Overlap*` and `Sweep` scanning every object of the scene on every
call instead of the cached collider list `Raycast` uses (physics lane, reported), each ray costing a pass over all
colliders (a broad phase for queries would help everything).

Both the old and the new path are in one binary: `bench_player --old`-style comparisons are `OKAY_OLD_PLAYER=1` against
unset. Run `tests/bench_player.cpp` (target not in CMake on purpose; build it like `tests/test_player_fixes.cpp`).

## What was added

* **The player's numbers on screen.** Press **F3** in the player (or open the Game view stats in the editor while
  playing): after the frame lines come the player's lines: grounded or in the air (and "held on slope"), stance, slope
  of the ground in degrees against the limit ("TOO STEEP"), speed, velocity, the ground normal, and what is stood on
  ("moving"). Code: `engine/include/okay/Components/PlayerDebug.hpp`.
* **Stay on the treads** when walking down stairs and curbs (a swept body test, so a body resting on a slope or a
  tread edge is not pulled down).
* **Hold still on slopes** the player may stand on.
* **Jump off a platform keeps its speed**; **slide along walls**; **no contact friction on a body the controller
  drives** (only when its friction is still the untouched 0.5).
* **Settings saved and shown**: Slope Limit, Step Offset, Fall Reset Y, Swim Speed, Swim Up Speed, Swim Down Speed.
* **The obstacle course** `examples/player/player_obstacle_course.okayscene` (see `docs/try_it.md` row 7w).

## Second round (same branch)

Fixed, each with a `player_deep_*` check that fails with `OKAY_OLD_PLAYER=1`: riding a kinematic platform up (the
rise was added twice: rider height range 6 cm and 25% of frames airborne, now 0 and 0%; downward carry kept),
climbing out of water onto a bank (`player_deep_water_climb_out`), variable jump height (off by default, saved,
Inspector: `variableJump`, `jumpCut`), ladders (a box trigger on an object tagged `Climbable`; `climbSpeed`),
a hard-landing event (`onHardLand`, Action List message `player_hard_land`, `hardLandSpeed` 12; event only, nothing is
hurt), buried-in-solid recovery (a body whose centre is inside a solid at start, after a teleport or while falling
fast is stood on open ground instead of falling out of the world), and the Scene-view gizmo (`PlayerGizmo`).
Coyote time and jump buffering already existed (on by default, saved, in the Inspector); `player_deep_coyote_jump`
now pins them. Pictures of the course: a picture folder (pictures are no longer stored in the repository).

## Not done, and bugs for other lanes

* A kinematic body pushing the player into a static wall pushes it through (E12c): contact solver (physics lane).
  Probe: `E12c` in `tests/probe_player_audit.cpp`.
* Scaled colliders ignore the scale in their centre offset (E22): `Collider3D::WorldCenter` (physics header).
* `Physics3D::Overlap*` / `Sweep` search the whole scene on every call (see above): a one-line change per function to
  use the cached collider list; routed to the physics lane rather than edited here.
* No ledge slide-off for the box body (E08). The Scene-view gizmo and the F3 lines were never seen on a real screen.
* Unverified: everything on Windows, the editor panels (only compiled in the sense of "read"; `editor/main.cpp` was not
  built here), the F3 lines on screen, any picture, real game feel with a keyboard and mouse.

## Busy scene (branch `claude/player-feel`)

`tests/bench_busy.cpp` (not in CMake): 2154 objects: 100 NPCs, 2000 static box colliders, 50 animated characters, a voxel terrain,
one first-person player; 800 frames after a warm-up, release build, one core, a shared machine (so read the ratio, not the
absolute number). Scene::Update median, with `OKAY_OLD_SCENE_QUERIES=1` (old) and without (new); two runs each:

| | old | new |
|---|---|---|
| median, run 1 | 16 583 us | 15 732 us |
| median, run 2 | 16 453 us | 15 456 us |

Wall-clock on this shared machine is noisy (20.8 / 20.9 / 21.1 ms old against 16.0 / 20.0 / 23.9 ms new in a later set of runs), so
the stable number is instructions counted by callgrind over 20 frames of `Scene::Update`: **9 150 M old, 8 298 M new (-9.3%)**, with
both fixes below.
Where the frame goes (profiler rows): Physics 15.0 ms, Scripts 0.4 ms, Animation 0.45 ms,
everything else 0. A callgrind run (20 frames) puts 84% of all instructions inside `Physics3D::Step` itself (the broad phase and
contact work on 2000 colliders), 3.4% in `BoxCollider3D::WorldAABB`, 2.4% in `dynamic_cast`, 0.8% in `Scene::Find` called by each
NPC every frame. The two fixes made: `Scene::FindObjectsOfType<T>` keeps its answer per type until the scene changes (a pointer
list copy instead of a walk with `dynamic_cast` over every object; the editor, the player and `Physics3D::Overlap*` all use it),
and `NPCController` finds its target by name once per scene change instead of every frame. **The biggest waste, `Physics3D::Step`, was
not touched** (`Physics3D.cpp` is a physics lane file): it should be looked at first (static colliders should not be tested against
each other, and a grid or tree should replace the all-pairs pass). Third waste not done.
