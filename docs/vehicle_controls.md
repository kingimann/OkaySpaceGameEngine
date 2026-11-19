# Vehicle controls (open-world driving style)

Owner request (2026-11-19): "the vehicle controller improve it. i want the controls like gta style", then "add more
settings and functions for user to choose from". This page says what existed, what was wrong, what was built, and what is
not done. Plain steps to try it: `docs/try_it.md` row `vehicle-1`. The older arcade model is described in
`docs/vehicle_controller.md` (it is still there and still the default for old scenes).

## Existing / Gap

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

## What was built (one copy: the existing component grew)

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
  `Horn`, `Lights`, `Recover`, `Eject`, `Exit`, `Enter`, `Speed`, `Gear`, `Rpm`; each is also a Visual Script node
  (`Fn.Vehicle.*`) with plain help text.
* HUD: the driven vehicle publishes `vehicle_speed`, `vehicle_gear`, `vehicle_unit`, `vehicle_rpm`, `vehicle_boost`; the
  UI kit's Text Bind with `{vehicle_speed|0} {vehicle_unit|km/h}   Gear {vehicle_gear|N}` shows them (the example has it).

## How the driving model works (short)

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

## Settings

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

## Tests and numbers

`tests/test_vehicle_drive.cpp` (suite `vehicle_drive`) and `tests/test_vehicle_world.cpp` (suite `vehicle_world`), headless and
deterministic (the input is set with `SetInput`, not read from the keyboard). RESULTS_PLACEHOLDER

## Not done, and unverified

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
