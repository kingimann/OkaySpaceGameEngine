# Vehicles

> **What this is:** The Vehicle Controller: the open-world driving model and the older arcade model.
> **Status:** combined on 2026-11-20 from 2 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
> **Last checked:** 2026-11-20, paths and links against main.

## Contents

- [Vehicle controls (open-world driving style)](#vehicle-controls-open-world-driving-style) (was `docs/vehicle_controls.md`)
- [Vehicle Controller (arcade)](#vehicle-controller-arcade) (was `docs/vehicle_controller.md`)

## Vehicle controls (open-world driving style)

*Was `docs/vehicle_controls.md` until 2026-11-20.*

Owner request (2026-11-19): "the vehicle controller improve it. i want the controls like gta style", then "add more
settings and functions for user to choose from". This page says what existed, what was wrong, what was built, and what is
not done. Plain steps to try it: `docs/try_it.md` row `vehicle-1`. The older arcade model is described in
`docs/gameplay/vehicles.md` (it is still there and still the default for old scenes).

### Existing / Gap

Played headless first (a rigid body with gravity off, keys fed, 60 fps): flat-out run, steer at 20 m/s, handbrake turn,
brake from 20 m/s, hold S from rest.

| Part | Existing (before this change) | Gap found | Now |
|---|---|---|---|
| Throttle and brake | `VehicleController`: W/S ease the speed toward a target; S while rolling forward brakes, S at rest reverses | No engine: constant push to top speed in one gear, no revs. Reverse is a second target, not "hold the brake". Releasing the brake from reverse glides with `drag` only | Gears, rpm, torque curve, brake-to-reverse, engine braking |
| Steering | A/D yaw rate `turnSpeed` scaled by speed up to 4 m/s, reversed backing up | Steering gets *stronger* with speed (same rate at 4 and 40 m/s). Keys snap to full lock. No auto-centre. Nothing stops a turn the tyres could not give | Wheel angle with response and auto-centre, less lock at speed, grip limit on turn rate |
| Handbrake and drift | Lower lateral `grip` while Space held | The car keeps pointing where it was going: grip only kills sideways slide, so nothing makes the back swing round. No counter-steer help, no slip angle, no way to tell a drift | Handbrake swings the back round, slip angle, drift flag, counter-steer assist |
| Grip | One number, flat | Same grip on ice and mud. Braking cannot lock or be saved by ABS | Surface grip from the ground's tag (Ice, Mud, Grass), friction limit on accel, braking and turning, ABS, traction control |
| Drivetrain | none | RWD, FWD and AWD behave the same | Drivetrain with weight share and torque split |
| Body | `suspension` option: rays, spring, lean | Off by default; lean only with suspension; no air control; no flip recovery | Visual dive, squat and roll always; air control; flip recovery |
| Vehicle types | one set of numbers | No Sedan, Truck, Bus and so on | 9 presets |
| Input | keys and arrows only | No gamepad, no smoothing, no dead zone | keys, gamepad triggers and stick, smoothing, dead zone, invert, sensitivity |
| Enter and exit | `VehicleSeat`: E to sit, exit beside the door, camera follows | Pad Y did nothing; no chase camera blend with drift | Pad Y works; the seated player's camera uses the vehicle camera |
| Camera | `followCamera` chase | Fixed distance, no look-ahead, no views | chase, far, hood, bonnet (C), look behind (B), drift swing, look-ahead, speed FOV and distance, collision pull-in, impact shake |
| Editor | Inspector with 16 fields, Add Component entry | No presets, no groups | Simple / Advanced / Presets tabs, GameObject > Vehicles, Driving World template |
| Scripts | `Speed()`, `IsGrounded()` only | Nothing to drive from a script | `Vehicle.*` builtins and nodes |

No other vehicle code was found (`grep` of engine, docs, examples, branches `claude2/*`). `VehicleController2D` is the 2D
sibling and is untouched. The boat (`claude/boat-1`) and camera rigs (`claude/camera-2`) are separate branches that were
not merged here; this change does not depend on them.

### What was built (one copy: the existing component grew)

* `engine/include/okay/Components/VehicleTuning.hpp`: one table of about 85 settings (name, range, group, plain tooltip).
  The Inspector rows, scene save, `SetSetting("name", value)`, presets and `.okayvehicle` files all come from that table.
  Written from general vehicle ideas (bicycle-model steering, friction limit, torque curve and gear ratios); no code from
  another project.
* `VehicleController` now derives from `VehicleTuning`. `openWorld = false` (the default in code, and for every old scene)
  runs the old model unchanged. `openWorld = true` (vehicles made from the menu, from a preset, or by ticking it) runs the
  new one. Scenes save the new settings as one `vehicletune` line; scenes without it load as before.
* `engine/include/okay/Scene/VehicleBuilder.hpp`: cars from cubes and cylinders, with wheels that roll and steer,
  headlights, a driver seat and a passenger seat.
* `Templates::DrivingWorld` and `examples/driving/driving_world.okayscene`: roads, buildings, ramps, mud, ice, a drift
  circle of cones, nine parked vehicles and a walking player.
* `engine/include/okay/Core/DrivingSettings.hpp`: player options in the user prefs (`driving.mph`, `driving.steer`,
  `driving.camera`, `driving.assists`, `driving.invert`). A vehicle reads them when it starts.
* Editor: `editor/VehicleEditorUi.hpp` (Create menu "Vehicles", Inspector tabs, Save and Load preset), three one-line hooks
  in `editor/main.cpp`, the `create.vehicles` slot in `MenuBarModel.hpp`, the "Driving World" template.
* Scripts: `Vehicle.SetSetting`, `GetSetting`, `ApplyPreset`, `SetThrottle`, `SetBrake`, `SetSteer`, `SetHandbrake`, `Boost`,
  `Horn`, `Lights`, `Recover`, `Eject`, `Enter`, `Speed`, `Gear`, `Rpm`; each is also a Visual Script node
  (`Fn.Vehicle.*`) with plain help text.
* HUD: the driven vehicle publishes `vehicle_speed`, `vehicle_gear`, `vehicle_unit`, `vehicle_rpm`, `vehicle_boost`; the
  UI kit's Text Bind with `{vehicle_speed|0} {vehicle_unit|km/h}   Gear {vehicle_gear|N}` shows them (the example has it).

### How the driving model works (short)

Each frame is cut into steps of at most 1/120 s, so 30, 60 and 144 fps give the same drive.
1. Pedals: with "Hold brake to reverse", brake while moving forward brakes; at a standstill it reverses; gas while rolling
   backwards brakes. "Separate reverse button" uses `keyReverse` (Q).
2. Engine: gear i tops out at `maxSpeed * (i/gearCount)^0.8`; rpm follows speed in that gear; the push is
   `acceleration * ratio * torqueCurve(rpm)`. Automatic shifts up near the limiter and down at low revs, with `shiftTime`
   of no push. Manual and Sequential use X and Z (RB / LB).
3. Tyres: the most the ground can give is `mu * 9.81` (mu = tyre grip x surface). Driving force is capped by the share of
   weight over the driven wheels (RWD, FWD, AWD with `torqueSplit`); brakes are capped by the same; without ABS a brake
   demand over the cap locks the wheels and stops about 25 percent worse.
4. Steering: wheel angle (target = input x lock x speed factor) follows with `steerResponse`, returns with `autoCentre`;
   turn rate = speed / wheelBase x tan(angle), capped by the tyre limit (`steerAssist`). The velocity does not turn with the
   body, so the car slides when the body turns faster than the tyres can follow; sideways speed is then killed at
   `grip x mu` per second. Past `slipLimit` degrees the grip halves. The handbrake lowers rear grip and swings the back.
5. Counter-steer assist adds wheel angle against the slide. Stability control adds grip in a slide when the handbrake is
   off. The Handling slider scales grip, the turn limit and the assists.

### Settings

All of them are in the Inspector Advanced tab with tooltips, grouped: Engine (power curve `torquePeak`, idle and max rpm,
gear count, gearbox Automatic / Manual / Sequential, shift time, engine braking), Drivetrain (RWD / FWD / AWD, torque split,
differential lock), Steering (max angle, speed sensitivity, response, auto-centre, counter-steer assist, steering assist),
Brakes (power, front/rear bias, ABS, handbrake strength, reverse behaviour), Tyres (front and rear grip, slip limit, handbrake
grip, ice / mud / grass grip), Body (downforce, air drag, lean, anti-roll), Assists (traction control, stability control, auto
flip, air control), Boost (power, duration, recharge), Controls (steering sensitivity, smoothing, dead zone, invert, keys),
Gamepad (button numbers), Camera (view, field of view, speed FOV, speed distance, drift swing, look-ahead).

Presets: Sedan, Sports Car, Muscle Car, Pickup, Heavy Truck, Bus, Buggy, Rally Car, Drift Car. A preset resets every setting
to the default first, then sets its own, so it is the same wherever it is applied. A user preset is a `.okayvehicle` text file
(`okayvehicle 1`, `name ...`, then `setting value` lines; unknown lines are skipped so a newer file still loads).

### Tests and numbers

`tests/test_vehicle_drive.cpp` (suite `vehicle_drive`) and `tests/test_vehicle_world.cpp` (suite `vehicle_world`), headless and
deterministic (the input is set with `SetInput`, not read from the keyboard). Run on Linux, Release build, headless. 831 checks in `vehicle_drive` and 51 in `vehicle_world` pass, and the old
`vehicle` (29) and `vehicle_seat` (266) suites still pass. What they check: every preset applies and round-trips through
the `.okayvehicle` text and a scene file; an old scene stays on the older model; 0 to 100 km/h and top speed for five
vehicles; the same drive at 30, 60 and 144 fps agrees (speed within 3 percent, position within 5 percent); braking
distances on tarmac and on ice with and without ABS; all-wheel against rear-wheel on ice; automatic against manual gearbox;
hold-brake-to-reverse and roll on release; steering lock shrinking with speed and D turning right; drift entry (slip angle
over 15 degrees, flagged as drifting), exit (slip back under 6 degrees, speed kept) and a bus that slides less; a minute of
wild input at speed stays finite and under 80 m/s, plus 0.0001 s, 0.5 s and 2 s frames; boost raises top speed and empties;
a flipped car recovers after 2 s, or stays until Recover when auto flip is off; headlights switch; the shipped Driving World
scene loads, a player walks to the Sedan, E seats them, W drives it, D steers right, E gets out. Numbers printed by the test
(a 1.0 grip surface unless it says ice):

```
  Sports Car  0-100 km/h: 4.36 s, top speed after 40 s: 48.8 m/s (setting 48.0), gear 6
  Sedan       0-100 km/h: 5.70 s, top speed after 40 s: 34.5 m/s (setting 34.0), gear 5
  Muscle Car  0-100 km/h: 5.38 s, top speed after 40 s: 44.4 m/s (setting 44.0), gear 4
  Pickup      0-100 km/h: 5.66 s, top speed after 40 s: 30.5 m/s (setting 30.0), gear 5
  Bus         0-100 km/h: not reached s, top speed after 40 s: 20.3 m/s (setting 20.0), gear 5
  frame rate: z 148.6 / 148.1 / 147.9   speed 49.33 / 49.32 / 49.32 (30 / 60 / 144 fps)
  braking from 100 km/h on tarmac: 33.3 m. From 72 km/h on ice: ABS 66.6 m, no ABS 86.2 m
  ice, 5 s flat out: rear-wheel 22.1 m, all-wheel 36.2 m
  8 s flat out: automatic 34.5 m/s (gear 5), manual left in gear 1 9.7 m/s
  full steering: turns 60 deg/s at 6 m/s, 21 deg/s at 30 m/s
  drift: biggest slip angle 77 deg; after letting go: 0.0 deg, speed 24.6 m/s
  boost: top speed 34.5 m/s without, 48.0 with a full tank held 15 s (tank left 0.00)
```

Read the numbers as the model's, not as real cars: the Sports Car (RWD, 48 m/s top) reaches 100 km/h in 4.4 s because
the tyre limit (about 6.4 m/s^2 with its grip 1.15) rather than the engine is what holds it. The drift test slides to a
77 degree slip angle, which is a big drift: whether that looks right is a feel question nobody has judged yet. Other suites
re-run after the change and green: `save_coverage`, `property_registry` (every setting saves and loads alone), `templates`,
`templates_work` (the Driving World template: get in, drive, get out), `menu_bar_model`, `hierarchy_context_model`,
`scene_cameras`, `serialize`, `scene_serializer_audit`, `examples_work` (the example scene opens and plays), `vs_node_docs`,
`vs_palette_categories`, `vs_script_coverage` (every `Vehicle.*` builtin runs as a node; the coverage graph and doc were regenerated).
`duplicate_components` was not run to the end here (its compile was killed by the machine running out of memory twice).

### Not done, and unverified

Not done: Bike (two-wheel lean; a bike needs a different model, left out), engine and tyre sound (no audio synthesis added),
horn sound (the horn is a flag only), skid marks, tyre smoke, exhaust flames, brake-light and indicator visuals (headlight
objects named `Headlight...` do switch), damage, fuel, AI "Auto Drive", minimap hook, camera-relative steering, a Settings
window page "Driving" (the prefs keys and the model exist, no window draws them), the Input Actions "Vehicle" map with
rebinding (keys are Inspector settings and gamepad buttons are numbers there; the Input Actions window is untouched), tyre
smoke and skid marks, flips from physics (the model keeps the body level, so it only flips when something outside tips it),
camera blend when getting out, door animation, a camera-rig layer from `claude/camera-2` (the existing chase camera was
extended instead), physics-material surfaces from `claude/physics-3` (surfaces are read from the ground object's tag),
`Vehicle.*` for OkayScript properties beyond the list above, a driving panel in the visual script palette categories.

Unverified: the editor pieces were not built here (the editor is a Windows program; `VehicleEditorUi.hpp` is unbuilt); nothing
ran on Windows or with a GPU; nobody has driven it by hand, so the feel (the Handling slider's ends, how long a drift lasts) is
a first guess from numbers; the gamepad path is not run; the camera is checked by numbers only.

## Vehicle Controller (arcade)

*Was `docs/vehicle_controller.md` until 2026-11-20.*

> New: an open-world driving model (gears, brake to reverse, drifts, presets, camera) lives in the same component. See `docs/gameplay/vehicles.md`. This page describes the older arcade model, which old scenes keep.

An arcade car controller in the same family as the First/Third-Person controllers.
Add it to a car body and drive — throttle pushes it along its facing, steering yaws
it (more as it speeds up, reversed when backing up), braking and a handbrake slow
it, and lateral **grip** bleeds off sideways slide so it corners instead of skating.

### Setup

1. Make a car object (a Cube or a mesh) with a **`Rigidbody3D`** and a collider.
   *Add Component ▸ Gameplay ▸ Vehicle Controller (Car)* adds a Rigidbody3D for you.
2. Give the scene some ground with a collider.
3. Press **Play** and drive.

Controls: **W/S** or **Up/Down** = gas / brake-reverse, **A/D** or **Left/Right** =
steer, **Space** = handbrake (drift).

### Tuning (Inspector)

| Field | Meaning |
|-------|---------|
| `maxSpeed` / `reverseSpeed` | top forward / reverse speed |
| `acceleration` | how quickly it reaches top speed |
| `brakeForce` | deceleration when braking |
| `drag` | rolling resistance while coasting (no throttle) |
| `turnSpeed` | steering rate (deg/s), scaled by current speed |
| `grip` | how fast sideways slide is killed — high = sticky, low = slidey |
| `handbrakeGrip` | grip while the handbrake is held (low = drift) |
| `groundCheckDistance` | down-ray length used for `IsGrounded()` |
| `followCamera` (+ `camDistance`, `camHeight`, `camLerp`) | chase the scene's main camera behind the car |

Without a Rigidbody3D it still drives by moving the Transform directly (handy for a
quick top-down/kinematic car).

### Scripting

`Speed()` returns the signed forward speed (units/s) and `IsGrounded()` whether the
down-ray hit — useful for engine-sound pitch, skid effects, or disabling control in
the air.

### Raycast suspension (opt-in)

Tick **Raycast Suspension** on a `VehicleController` for a sim-ish ride. Four
down-rays at the `wheelBase × trackWidth` corners feed a critically-damped vertical
spring that holds the body at `rideHeight`, soaks up bumps, and keeps it grounded
over terrain. The chassis rolls and pitches from per-wheel compression (so it leans
with the ground) plus dynamic `bodyLean` from acceleration (squat/dive) and
cornering — clamped to `maxTilt` and eased by `tiltSmooth`.

The spring is applied through the Rigidbody3D's force accumulator with gravity
feed-forward, so it composes with normal gravity/collision and settles exactly at
ride height. Needs a Rigidbody3D (gravity on) and a collider; `suspensionTravel`
sets how far the wheels droop before the car is considered airborne. Leave it off
for the pure arcade feel — everything else (throttle, steering, grip, handbrake)
works the same either way.

### 2D vehicles (`VehicleController2D`)

The 2D sibling, *Add Component ▸ Gameplay ▸ Vehicle Controller 2D* (adds a
Rigidbody2D). Two modes:

- **Top-down** (default): throttle drives along the sprite's facing (its local +Y),
  A/D steer (speed-scaled, reverse-aware), grip/handbrake drift — the arcade model in
  the XY plane. Set `gravityScale = 0` on the Rigidbody2D.
- **Side view** (`sideView = true`): a platformer car — A/D drive along world X with
  gravity on, no steering.

Same feel knobs (max/reverse speed, acceleration, brake, drag, turn, grip,
handbrake). `Speed()` returns the signed forward speed.

### Driver seat and getting in (`VehicleSeat`)

Add *Add Component ▸ Gameplay ▸ Vehicle Seat (get in and drive)* on the car (or on a child of it). Then the
player gets in and out instead of the car always listening to the keyboard.

- **Get in:** walk within `useRadius` of the seat and press **E** (`enterKey`). The
  player controller and the body collider switch off, gravity is paused, and the body is
  carried at `sitPoint`. The car now reads the driver's keys (W/S/A/D, Space).
- **Sitting:** the person is posed sitting. It is the same sit the chair `Seat` uses (the humanoid
  animator's `SitDown` then `Sit` states, chair clip), started by `HumanoidActions::BeginVehicleSit`,
  and `StandUp` plays on exit. The body is turned to face the way the car drives. `sitPoint` is where
  the **feet** go (the cabin floor), not the cushion: the sit clip puts the hips 0.48 m above the feet.
  Passengers sit the same way. Covered: the built-in humanoid rig (the one every Player template
  builds). Any other body (a different rig, a plain mesh) is carried upright and does not play a sit
  (`PoseApplied()` is false for it). Hands are **not** on the wheel: the sit clip has the arms bent
  forward, and no hand-hold / IK to the steering wheel is wired in (not done).
- **Sit height fix:** the sit clips used to RAISE the body about 0.5 m (hipY was a hip height used as a drop). They now sit correctly for the chair, ground and knees sits. This also lowers the older chair `Seat`: a chair authored with `sitPoint` on the cushion (0.45 m up) will now sit too high. The body root is the feet, so put the chair `sitPoint` on the floor under the seat (the hip joint then lands 0.57 m up, about 0.12 m over a 0.45 m seat).
- **Camera:** when the scene's main camera belongs to the player (the Player templates), boarding
  takes it over: third person = a chase view behind and above the car (`camDistance`, `camHeight`,
  `camLerp` of the VehicleController; it eases in from where the orbit was); first person = at the
  seated eyes looking along the car (no mouse look while seated). On exit the camera gets its old
  pose back and the player controller drives it again. `VehicleSeat::CameraTarget()` is the car
  while that is on. A camera that is not the player's is left alone, and `followCamera` still works
  for it (it stays out of the way while a seat drives the camera).
- **Get out:** press **E** again. The player is put at `exitPoint` (local to the seat) and
  the controller, collider and gravity come back. If something solid is there, the
  mirrored sides are tried, then the roof (2.5 m above the seat).
- **Driver leaves:** the car brakes to a stop (`brakeForce`) and ignores the keys. The
  chase camera only follows while someone drives.
- **One driver per car.** A second get-in on a driver seat is refused. Turn off `driver`
  for a passenger seat: it carries the player but does not give them the wheel. One person
  sits in one seat at a time.
- **A car with no driver seat** keeps the old behaviour: it always reads the keys.
- **Safe on destroy:** if the driver, the seat or the car is destroyed while seated, the
  seat is freed and no pointer is left dangling; a surviving player is put back on foot.
- **Saved:** `driver`, `sitPoint`, `exitPoint`, `useRadius`, `enterKey`, `prompt`. The seat
  finds its car through the object hierarchy, so it survives save, load and scene copy.
  Who sits in it is runtime only and is not saved.
- **Who can board:** a local player: an object with a player controller (first or third
  person, character controller 2D / 3D, top-down 2D). AI objects (an NPC with a Character
  component) cannot. A dead occupant is put out of the seat, and a dead body cannot get in.
  Chair `Seat` and `VehicleSeat` do not mix: someone sitting in one cannot board the other.
- **Two seats, one key:** E lets the player out of one seat; they do not get into another
  seat of the same car on that same key press.
- **Exit:** the player is stood upright (yaw kept, no pitch or roll from the car). Colliders on
  the player and on its children switch off while seated and come back on exit.
- **Seat on the player's own car:** a seat refuses its own object and its parents.
- **Saving while seated:** a save, copy, prefab or undo snapshot made while someone sits writes
  the player with their controller, colliders and gravity as before they got in. The seat takes
  them again right after the save. The seat itself is not restored on load: the player stands
  where the seat was, free.
- **A car with only passenger seats** (or none) keeps the old behaviour: it always reads the keys.
  Only an enabled driver seat makes the car wait for a driver. Remove the seat, or turn `driver`
  off, and the car drives with the keys again.
- **Input:** the raw `enterKey` only. Gamepad buttons and the input action map are not read, and
  E is also first-person lean-right (change `enterKey` if that matters).
- **From C++:** `VehicleSeat::Enter(go)` (returns false if refused), `Exit()`,
  `Occupant()`, `VehicleController::Driver()`.

**Behaviour change for every existing vehicle scene (plain note for the release text):** two things changed in `VehicleController`.
1. **Steering direction.** D (steer right) used to yaw the car counter-clockwise seen from above, which is a LEFT turn; it now turns right (A turns left). Scenes that relied on the old mirrored steering will steer the other way round.
2. **Chase camera.** With `followCamera` on, the camera used to be aimed backwards (a camera looks down its -Z, the code aimed +Z at the car). It now looks at the car. Scenes with `followCamera` will see the camera facing the car for the first time.
Checked here: `tests/test_vehicle.cpp` (steering changes the heading, camera options still save and load), `tests/test_vehicle_seat.cpp` (loads `examples/vehicle/vehicle_test.okayscene` and drives it), `tests/test_templates.cpp` (the Vehicle 3D template builds). The `Vehicle3D` template (one box car with `followCamera`) was **not** looked at in a render after the change; only its tests ran. `VehicleController2D` was not changed.

**Fixed in this branch (found by an outside review of the merged seat code):**
- A seat **moved to another car** (re-parented, for example dragged in the Hierarchy during Play) used to leave the old car counting a driver seat that was gone, so it never drove again. If someone was seated, getting out left them as the old car's driver and that car then drove itself with nobody inside. The seat now remembers the car it was counted on and the car it made the person driver of, and frees exactly those.
- **Unticking `driver` while someone sits** used to leave them as the car's driver after they got out. Same fix.
- A `VehicleController` removed while a seat points at it (editor, undo, prefab apply) is safe: the seat checks that the controller it remembers is still alive (and is the same one) before touching it, and counts itself on a new controller if one is added.
- Still true: moving an occupied seat to another car does **not** put the person out. They keep sitting in it (now on the new car) until they press E.
- Tested: `tests/test_vehicle_seat.cpp` (nobody seated; seated then re-parent then E; two driver seats with `driver` unticked). Try it by hand: press Play, drive or sit in the car, drag the seat object onto another car in the Hierarchy.

Not done: no script or visual-script node for enter / exit / driver yet; **multiplayer
does not replicate seat state** (each machine decides on its own); no hands on the wheel and
no steering animation on the character; no mouse look from the seat in first person; the camera
does not blend back smoothly on exit (the player's own camera takes over at once).

### Wheels (looks only)

`VehicleController` animates wheel objects it finds under the car (no component to add, no new saved fields; but see the save note below):
every descendant whose name starts with `Wheel` and that has a child named `Tire`. The `Tire` is the
cylinder (a Cylinder mesh is radius 0.5, height 1 along Y: scale it to the tyre, e.g. `0.7, 0.25, 0.7`
for a 0.35 m radius, 0.25 m wide tyre, and lay it on its side with local rotation `0, 0, 90`). The
roll angle is distance divided by radius, so the radius comes from the Tire's local X scale (half of it).
A `Wheel` object whose name contains `Front` is also turned about Y for steering, up to
`VehicleController::kMaxSteerAngle` (30 degrees). The controller rewrites the Wheel's and the Tire's
local rotation each frame, so keep those two for the animation and hang rims, hubcaps and so on under
the Tire. `WheelSpin()` and `SteerAngle()` read the angles. This is visual only: the physics is the
arcade body, there are no wheel colliders and no suspension per wheel.

Limits, honestly:
- The name rule is a prefix: `Wheel...` (an object called `Rewheel` is not a wheel). The `Tire` child must be named exactly `Tire`; steering needs `Front` in the Wheel's name.
- Wheels are looked up on the first frame the car is updated and then **twice a second**, so wheels added later are found within half a second.
- A remote or proxy car (not locally controlled) rolls its wheels from how far it moved along its heading; its front wheels stay straight (the steering is not known). **Unverified:** no test runs a proxy car.
- The Wheel's and Tire's local rotation is **overwritten every frame**, so a tire authored with a different rotation or axis is replaced by "axle along X". Because it is a normal rotation, a save or undo snapshot taken while playing writes the current steer and roll angles into the file (nothing restores the authored ones). Do not save a scene from play mode if you care about them.
- A car with no wheel objects still tracks `WheelSpin()` (as if the radius were 0.35 m).

#### Try it yourself: the "Vehicle Test" scene

A ready scene: a red car about 4.1 m long and 1.8 m wide (hood, glass cabin, two seats, a steering wheel,
four wheels with rims, head and tail lights) on a two-lane road with edge lines and a dashed centre
line, a slalom of cones and some crates ahead, and a third-person player standing in the road by the
driver door.

1. In the editor use *File ▸ Open* and pick `examples/vehicle/vehicle_test.okayscene` from the engine
   folder. It is also in the New Project chooser as "Vehicle (get in and drive)", and the Windows zip and installer ship it under `examples/vehicle/`. In code: `Templates::VehicleSeats3D(scene)`.
2. Press **Play**.
3. Walk to the driver door (the -X side of the car, which is the car's right side seen from behind; WASD) and press **E**. You sit in the driver seat and the camera
   swings behind the car.
4. **W / S** = gas and brake / reverse, **A / D** = steer (the front wheels turn, all wheels roll),
   **Space** = handbrake. Drive through the cones.
5. Press **E** again to get out beside the car. The camera goes back to you. The car brakes to a stop
   and ignores the keys.
6. Walk to the other side (+X) and press **E** to ride as a passenger (you sit, you are carried, the car does not drive).
7. Press **V** before getting in to try first person: the camera sits at your eyes in the car.

How the file is made: the scene file is the output of `Templates::VehicleSeats3D` saved with
`SceneSerializer::SaveToFile` (the template is the source). If you change the template, save it again.

**What was checked and what was not.** The headless test `vehicle_seat` (Linux) loads the scene file and
plays steps 3 to 6 with simulated keys: the sit state is set and cleared, the wheels roll with the speed
and the front ones steer, and the camera follows the car and is the player's again after exit. The look
of the scene was checked only in software-renderer screenshots (1280x720, shadows on) from a chase
camera, not in a window or the editor. Unverified: first person from the seat (the eye position is
set by hand; not looked at in a window), a window / GPU render of the glass, the editor Inspector block
for the seat (never compiled by an editor build), Windows.
Known rough edges: the sit clip is a generic chair sit, so the hands rest near the wheel but do not
hold it; the car is a stack of boxes, not a modelled body.

### Roadmap

The arcade model is the default; raycast suspension (above) is the opt-in sim layer.
A future upgrade would be true torque-based body dynamics (angular velocity / inertia
on the Rigidbody3D) so the chassis rolls from forces at the wheels rather than the
current ride-height-plus-tilt approximation.
