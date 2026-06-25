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

## Roadmap

This is the arcade model. Per-wheel **raycast suspension** (spring/damper per wheel,
weight transfer, bump handling) can layer on top later for a more simulation feel —
the arcade controller stays as the simple default.
