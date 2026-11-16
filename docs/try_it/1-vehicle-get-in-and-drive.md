---
id: 1
row: | 1 | Vehicle: get in a car and drive | New Project template **Vehicle (get in and drive)**, or `examples\vehicle\vehicle_test.okayscene` | Template and example new in this change |
---
## 1. Vehicle: get in and drive

**What it is.** A car with a driver seat and a passenger seat. You walk up, press E, and the car listens to your keys.
Code: `VehicleSeat` and `VehicleController` (`docs/vehicle_controller.md`).

**Steps (template).**
1. Open the editor (`Tools\OkayEngine.exe`) or the launcher's Create page. Choose **New Project**, pick the template
   **Vehicle (get in and drive)** (group "Game kits"), give it a name, create it.
2. The scene opens on a two-lane road with a red car (about 4 m long) and a person standing at its driver door, which is
   on the -X side: the car's right side seen from behind. Cones and crates are further down the road. The Hierarchy
   shows `Car` (with its wheels, seats and glass as children), `Passenger Seat`, `Player`, `Road`, `Ground`.
3. Press **Play**.

**Steps (example file).** Same scene without making a project: **File > Open...**, paste
`<install folder>\examples\vehicle\vehicle_test.okayscene`, press Play.

**In play.**
* Walk to the driver door (the side with the steering wheel) with **W A S D** and the mouse (third person: the camera orbits behind you).
* Within about two metres of the driver door a prompt says **Drive**. Press **E**: you sit down in the driver seat (the
  chair sit pose, hands forward near the wheel) and the camera swings behind and above the car.
* **W / S** gas and brake or reverse, **A / D** steer (D turns right), **Space** handbrake. The wheels roll and the
  front wheels turn. The chase camera follows the car. Weave through the cones.
* Press **E** again: you stand up beside the car, the camera goes back to you, and the car brakes to a stop and ignores the keys.
* Walk to the other door (+X side), the prompt says **Ride**. Press **E**: you sit in the passenger seat and are carried
  along, but the car does not drive.
* **First person:** press **V** before getting in. After **E** the camera sits at your eyes in the car.
* **Moving a seat to another car** (a bug fix, see `docs/vehicle_controller.md`): add a second car, press Play, sit in
  the car, drag the seat object onto the other car in the Hierarchy, press **E**. The first car must not drive by itself.

**How you know it worked.** Before you press E the car does not move when you press W. After E, W moves the car and
the prompt is gone. After the second E you are standing outside again and W moves you, not the car.

**First or third person.** The scene's player is third person. A first-person player can also board (the seat accepts
a player with a first or third person controller). To try it, select `Player`, remove the Third Person Controller and
add a First Person Controller in the Inspector. Unverified in the editor.

**Known rough.** From `docs/vehicle_controller.md`: hands do not hold the wheel (generic chair sit); the car is a stack
of boxes; first person has no mouse look from the seat; the look was only checked in software-renderer screenshots; multiplayer does not replicate seats; no script or visual
script node for enter and exit yet; a scene saved while someone sits stores the person standing free.

**Tested on main:** the headless test `vehicle_seat` loads `examples/vehicle/vehicle_test.okayscene` from disk and plays
the get in, drive, get out and ride steps with simulated key presses. **Unverified:** the real editor window, the
chooser card, the Windows exe, the camera feel. The editor method `NewVehicleSeats3D` was added by copying
`NewVehicle3D` and was not compiled on this machine (the editor is not built here).
