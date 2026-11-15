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
does not replicate seat state** (each machine decides on its own); no sit or steering
animation on the character; no per-seat camera.

### Try it yourself: the "Vehicle Test" scene

A ready scene has a red car with a driver seat (left front) and a passenger seat (right front),
four wheels, a flat ground, and a third-person player standing by the driver door.

1. In the editor use *File ▸ Open* and pick `examples/vehicle/vehicle_test.okayscene` from the engine
   folder. It is also in the New Project chooser as "Vehicle (get in and drive)", and the Windows zip and installer ship it under `examples/vehicle/`. In code: `Templates::VehicleSeats3D(scene)`.
2. Press **Play**.
3. Walk to the driver door (WASD) and press **E**. You sit in the car.
4. **W / S** = gas and brake / reverse, **A / D** = steer, **Space** = handbrake.
5. Press **E** again to get out beside the car. The car then brakes to a stop and ignores the keys.
6. Walk to the right side and press **E** to ride as a passenger (you are carried, the car does not drive).

**Unverified in play:** I (the agent) ran none of this in a real editor or a window. What was run is the
headless test `vehicle_seat`, which loads `examples/vehicle/vehicle_test.okayscene` from disk and plays
steps 3 to 6 with simulated key presses, on Linux only. The editor Inspector block for the seat was added but never compiled.
The wheels are for looks: they do not spin or turn.

## Roadmap

The arcade model is the default; raycast suspension (above) is the opt-in sim layer.
A future upgrade would be true torque-based body dynamics (angular velocity / inertia
on the Rigidbody3D) so the chassis rolls from forces at the wheels rather than the
current ride-height-plus-tilt approximation.
