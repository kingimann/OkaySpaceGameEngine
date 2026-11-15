# Vehicle Controller (arcade)

An arcade car controller in the same family as the First/Third-Person controllers.
Add it to a car body and drive — throttle pushes it along its facing, steering yaws
it (more as it speeds up, reversed when backing up), braking and a handbrake slow
it, and lateral **grip** bleeds off sideways slide so it corners instead of skating.

## Setup

1. Make a car object (a Cube or a mesh) with a **`Rigidbody3D`** and a collider.
   *Add Component ▸ Gameplay ▸ Vehicle Controller (Car)* adds a Rigidbody3D for you.
2. Give the scene some ground with a collider.
3. Press **Play** and drive.

Controls: **W/S** or **Up/Down** = gas / brake-reverse, **A/D** or **Left/Right** =
steer, **Space** = handbrake (drift).

## Tuning (Inspector)

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

## Scripting

`Speed()` returns the signed forward speed (units/s) and `IsGrounded()` whether the
down-ray hit — useful for engine-sound pitch, skid effects, or disabling control in
the air.

## Raycast suspension (opt-in)

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

## 2D vehicles (`VehicleController2D`)

The 2D sibling, *Add Component ▸ Gameplay ▸ Vehicle Controller 2D* (adds a
Rigidbody2D). Two modes:

- **Top-down** (default): throttle drives along the sprite's facing (its local +Y),
  A/D steer (speed-scaled, reverse-aware), grip/handbrake drift — the arcade model in
  the XY plane. Set `gravityScale = 0` on the Rigidbody2D.
- **Side view** (`sideView = true`): a platformer car — A/D drive along world X with
  gravity on, no steering.

Same feel knobs (max/reverse speed, acceleration, brake, drag, turn, grip,
handbrake). `Speed()` returns the signed forward speed.

## Driver seat and getting in (`VehicleSeat`)

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

Not done: no script or visual-script node for enter / exit / driver yet; **multiplayer
does not replicate seat state** (each machine decides on its own); no hands on the wheel and
no steering animation on the character; no mouse look from the seat in first person; the camera
does not blend back smoothly on exit (the player's own camera takes over at once).

## Wheels (looks only)

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
- Wheels are looked up **once, on the first frame the car is updated**. Wheels added later are not seen.
- A remote or proxy car (not locally controlled) does not animate its wheels: the animation runs after the local-control check.
- The Wheel's and Tire's local rotation is **overwritten every frame**, so a tire authored with a different rotation or axis is replaced by "axle along X". Because it is a normal rotation, a save or undo snapshot taken while playing writes the current steer and roll angles into the file (nothing restores the authored ones). Do not save a scene from play mode if you care about them.
- A car with no wheel objects still tracks `WheelSpin()` (as if the radius were 0.35 m).

### Try it yourself: the "Vehicle Test" scene

A ready scene: a red car about 4.1 m long and 1.8 m wide (hood, glass cabin, two seats, a steering wheel,
four wheels with rims, head and tail lights) on a two-lane road with edge lines and a dashed centre
line, a slalom of cones and some crates ahead, and a third-person player standing in the road by the
driver door.

1. In the editor use *File ▸ Open* and pick `examples/vehicle/vehicle_test.okayscene` from the engine
   folder. (It is not in the New Project chooser.) In code: `Templates::VehicleSeats3D(scene)`.
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

## Roadmap

The arcade model is the default; raycast suspension (above) is the opt-in sim layer.
A future upgrade would be true torque-based body dynamics (angular velocity / inertia
on the Rigidbody3D) so the chassis rolls from forces at the wheels rather than the
current ride-height-plus-tilt approximation.
