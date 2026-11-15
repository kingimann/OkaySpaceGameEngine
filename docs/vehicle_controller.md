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

## Getting in and driving (Seat)

Add a **Seat** to the car, or to a child object placed where the driver sits. That makes it a vehicle:

- The **first Seat** in the car's hierarchy (the car object, then its children in order) is the **driver's seat**.
  Further Seats are passenger seats.
- With a driver's seat, the car takes the keys only while a player sits there (`HasDriver()`). Empty, it brakes to a
  stop and stays parked, and W or S do nothing.
- Walk up to the seat (within its Use Radius) and press **E** to get in. The player sits at the seat's Sit Point and
  rides along. The move keys drive the car instead of standing the player up. **E** again gets out at the seat's
  Exit Point (put it beside the door, in the seat's local space).
- While seated in a vehicle, the player's colliders are switched off so the body does not push the car; they come
  back on when the player gets out or dies.
- A car with no Seat drives from the keys at once, as before.

Code: `DriverSeat()`, `HasDriver()` on `VehicleController`; `Seat::OnVehicle()`, `Seat::IsDriverSeat()`;
`HumanoidActions::InVehicleSeat(player)`. Test: `vehicle_seat`.

Not done yet: multiplayer (the car still drives from the local keys of whoever owns it, and no seat state is sent to
other players), a getting-in animation (the seat's sit-down is used), a dedicated in-car camera (use `followCamera`
or the player's own camera), and a way to choose the driver's seat other than putting it first.

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

## Roadmap

The arcade model is the default; raycast suspension (above) is the opt-in sim layer.
A future upgrade would be true torque-based body dynamics (angular velocity / inertia
on the Rigidbody3D) so the chassis rolls from forces at the wheels rather than the
current ride-height-plus-tilt approximation.
