---
id: vehicle-1
row: | vehicle-1 | Open-world driving: cars, presets, Driving World | New Project template **Driving World (open-world cars)**, or `examples\driving\driving_world.okayscene`; **GameObject > Vehicles**; Inspector on a Vehicle Controller | New; built and tested on Linux only, not run on Windows or by a person |
---
## vehicle-1. Open-world driving: cars, presets, Driving World

**What it is.** The vehicle controller now has an "open-world driving" model: hold the brake to reverse, gears and engine
revs, steering that gets lighter at speed, a handbrake that starts a drift, ABS, boost, flip recovery, nine vehicle presets
(Sedan, Sports Car, Muscle Car, Pickup, Heavy Truck, Bus, Buggy, Rally Car, Drift Car) and a chase camera that swings out in
drifts. Details: `docs/vehicle_controls.md`.

**Steps (the example world).**
1. Open the editor (`Tools\OkayEngine.exe`). **File > New Project**, pick the template **Driving World (open-world cars)**.
   Or **File > Open...** and paste `<install folder>\examples\driving\driving_world.okayscene`.
2. Press **Play**. You are a walking person next to a red Sedan. Nine vehicles stand in a row; a mud patch, an ice patch,
   two ramps and a circle of cones are further away.
3. **W A S D** walk to the driver door (the side facing the road's start, -X). The prompt says **Drive**. Press **E**.
4. Drive: **W** gas, **S** brake and, when stopped, reverse (let go to roll), **A / D** steer, **Space** handbrake,
   **Left Shift** boost, **R** put the car back on its wheels, **H** horn flag, **L** headlights, **C** changes camera
   (chase, far, hood, bonnet), **B** hold to look behind. **E** gets you out beside the door.
5. Top-left text shows speed and gear.

**Gamepad.** Right trigger gas, left trigger brake and reverse, left stick steer, **A** handbrake, **B** boost, **Y** get
in or out, left stick click horn, right stick click camera, **RB / LB** gear up and down (manual gearbox only), **Back** recover.

**Steps (your own vehicle).** **GameObject > Vehicles > Pickup** (or any type) puts a ready vehicle at the Scene view's
target. Add a player with **GameObject > Player**, press Play, walk up, press **E**.

**Steps (tuning).** Select a vehicle, open the **Vehicle Controller** in the Inspector. **Simple** tab: Vehicle preset,
Handling slider (Realistic to Arcade), Top speed, Acceleration, Grip, Drift amount. **Advanced** tab: every setting in groups
(Engine, Drivetrain, Steering, Brakes, Tyres, Body, Assists, Boost, Controls, Gamepad, Camera) with tooltips. **Presets**
tab: type a file name and press **Save as preset** or **Load preset** (a `.okayvehicle` text file).
Slippery ground: tag an object **Ice**, **Mud** or **Grass** and the car grips less on it.

**Switches.** Old scenes keep the older model; the new model is on for vehicles made from the menu or a preset
(Inspector: **Open-world driving**). Player options are stored in the user prefs keys `driving.*` (km/h or mph, steering
sensitivity, camera, assists). There is no Settings window page for them yet (see the doc).

**What you should see.** In the Sedan, W for a few seconds: speed climbs and the gear digit goes 1, 2, 3. Pull Space while
steering at speed: the back slides, the camera swings out; let go and the car straightens. On the ice patch the same car takes
much longer to stop.

**Status.** Built and tested headless on Linux (`test_vehicle_drive`, `test_vehicle_world`; numbers in the doc). The editor
menus and Inspector tabs are written but the editor was not built here. Not run on Windows, no GPU, and nobody has driven it by
hand: the feel is unverified.
