# Water, floating things and the boat

Owner request (2026-11-19): "improve water physics, add a basic boat I can use on water". This page says what we already
had, what was missing, what was built, and how to try it. Tests: `tests/test_boat.cpp` (CTest name `boat`).

## Existing / Gap

Searched first: `docs/terrain_water.md`, `Components/Water.hpp`, `Environment/Wind.hpp`, the Rigidbody3D / Physics3D headers,
`VehicleController`, `VehicleSeat`, the feature map (2.6.5 Water, 4.2.6 Swimming), `docs/UNFINISHED.md` (no water or boat row),
and the branches `claude2/water-vehicle-fixed-step`, `claude2/vehicle-seat`, `claude/guided-vehicles`.

| Area | Existing | Gap | What this change does |
|---|---|---|---|
| Wave look | `Water` makes a subdivided mesh and moves it with 3 crossing sines plus a wind wave (`WaveAt`) | no real wave formula, no sharp crests, no direction, no seed, nothing a boat could ask "how high is the sea here" except the private `WaveAt` | New `WaveStyle` on the same `Water` component: **Classic** (the old sines, still the default so old scenes look the same), **Ocean**, **Calm lake**, built from a Gerstner wave sum (`Environment/WaterWaves.hpp`) |
| Surface query | `Water::Column(world, surfaceY, floorY)` (height and floor only, needs depth > 0), `ProbeAt` for swimmers | no normal, no flow, no way to ask a flat sheet | `Water::SurfaceAt(world, Surface&)` gives height, floor, normal and flow velocity for every style; `Water::SampleIn` picks the best water among several |
| Drawn surface vs physics | Mesh is moved in `Animate()` with the same `WaveAt` the physics reads | for a sum of waves with sideways movement the mesh must move sideways too | Ocean and Calm move each mesh vertex with the same formula (`Field::Displace`); a test checks every vertex lies on the height the physics reads |
| Floating things | `Water::BuoyBodies`: one box test per body from its AABB, one upward force at the centre, one drag number; runs per frame | no tilting (force at the centre), no hull shape, no wave slope, no per-direction drag, no splash, drag is frame-rate dependent | New **Buoyancy** component (Physics 3D): several sample points, an equal share of the hull volume per point, force where each point is, drag per hull direction (linear and quadratic) relative to the moving water, angular damping, splash events. Runs in `FixedUpdate`. Water's old prop float skips bodies that have a Buoyancy (`IgnoresBuoyancy`) and is unchanged for everything else |
| Water moving things | `flow` only scrolls the texture | a current never moved anything | `Water.current` (m/s) adds to the flow the drag works against, so a crate drifts with the stream |
| Vehicle | `VehicleController` (wheeled car), `VehicleSeat` (get in and drive), `VehicleController2D` | nothing for water | New **Boat Controller** (Vehicles). `VehicleSeat` only talks to `VehicleController`, so a person cannot yet press E to board a boat (see "Not done") |
| Camera | `VehicleController.followCamera` chase code (private to it) | not reusable from outside | Boat Controller has its own small chase camera with the same settings (`camDistance`, `camHeight`, `camLerp`) |
| Effects | `ParticleSystem` (`Emit(n)`, world space), `TrailRenderer` (flat ribbons) | no water effects | Boat wake (two foam trails) and bow splash (spray) built from those, plus a splash spray from Buoyancy |
| Scene file | hand-written chain plus `ComponentSerializerRegistry` | | New records `waterwaves`, `buoyancy`, `boat` through the registry; old files load unchanged |
| Example | `examples/vehicle/` (car) | no water example | `examples/boat/boat_lake.okayscene` (`Templates::BoatLake`) and **GameObject > Vehicles > Boat** |
| Fixed step for Water's own prop float | branch `claude2/water-vehicle-fixed-step` (unmerged, Claude 2) moves `Water::BuoyBodies` to `FixedUpdate` | | Superseded for floating: plain props now get a Buoyancy that runs in `FixedUpdate` (see the section above). When that branch merges, `Water.hpp` needs a conflict fix (both edit `BuoyBodies`); keep this change's version

## What was written, and from what (rule 19a)

Every new file was written by us from general knowledge, not copied from or modelled on any other project.

* `engine/include/okay/Environment/WaterWaves.hpp`: the published Gerstner (trochoidal) wave formula (a point of water moves
  on a circle: `x = x0 + Q A D cos(theta)`, `y = A sin(theta)`, `theta = k D.x0 - omega t + phase`), deep-water dispersion
  `omega = sqrt(g k)`, a fixed-point loop to find the rest position under a world point, derivatives for the normal and
  velocity, and SplitMix64 for the seeded numbers.
* `engine/include/okay/Components/Buoyancy.hpp`: Archimedes (upward push = weight of displaced water), drag as an exact
  "slow down by a factor" step (so it can never add energy), our own point layout.
* `engine/include/okay/Components/BoatController.hpp`: a small design of our own: a cruise-style engine push, a rudder that
  needs flowing water, control scaled by how much of the hull is in the water.
* `Templates::AddBoat`, `Templates::BoatLake`: our own block models.

## Why things bounced on the water, and the fix

Owner question: "why do things start bouncing on the water". Cause, found in `Water::BuoyBodies` (the old float) and shown by
the `boat` test (it runs the old code with `Water.autoBuoyancy` off, then the new code):

1. It ran in `Update` with the frame time, but physics runs in fixed steps. The push it added per frame was applied once
   per step, so the same scene floated at a different height at 30, 60 and 144 fps.
2. One force at the box centre, from the water height at one column; no tilt, no slope.
3. The push switched off hard (`submerged <= 0.0001`), and the drag only worked while under water and was one number, so at the
   surface gravity and the push took turns.
4. Weak vertical damping, so the energy never left.

Measured (1 m crate dropped from 3 m onto flat water, density 1; "motion" = how far it still moves between 5 s and 10 s):

| Crate mass | fps | OLD bounces / motion / rest height | NEW bounces / motion / rest height |
|---|---|---|---|
| 0.5 (right answer 0.00) | 30 | 2 / 0.146 m / -0.324 | 1 / 0.0000 m / 0.000 |
| 0.5 | 60 | 3 / 0.020 m / 0.084 | 1 / 0.0000 m / 0.000 |
| 0.5 | 144 | 4 / 0.012 m / 0.326 | 1 / 0.0000 m / 0.000 |
| 0.9 (right answer -0.40) | 30 | 0 / 6.5 m / -11.3 (it sank) | 1 / 0.0000 m / -0.400 |
| 0.9 | 60 | 1 / 0.003 m / -0.247 | 1 / 0.0000 m / -0.400 |
| 0.9 | 144 | 2 / 0.002 m / 0.188 | 1 / 0.0000 m / -0.400 |

On waves (Classic style, height 0.35 and 1.0) the crate follows the water at both 30 and 144 fps (mean height 0.04 and 0.10)
and never moves more than the waves do. A ball with bounciness 1 dropped from 3 m goes in and rises 0.25 m at most (Water has
no collider, so it cannot be bounced off).

The fix is one system: a plain prop that touches a Water at Play gets a Buoyancy (not saved; `Water.autoBuoyancy`, on by default;
off keeps the old single-force code only to compare). So old scenes keep working with the same fields (`depth`, `fluidDensity`,
`linearDrag`), but now: fixed step, several points, a push that rises smoothly with depth (no on/off switch), up and down
damping against the water's own velocity (a wave lifting the crate does not fling it) that is at full strength once a quarter of
the hull is wet, angular damping, a cap on the push for very light bodies, and balls and cylinders use their real volume (pi/6, pi/4
of the box). `SwimVertical` (the swimmer) is not touched.

Honest limits: a body only a little heavier or lighter than water (mass 0.9) sinks deep after a hard drop and rises slowly,
as it would in real water; the test drops it from 3 m and it is at rest by 5 s because the entry is damped hard.

## The water surface

`Water` (Environment > Water) has new settings:

| Setting | Meaning |
|---|---|
| Style | Classic (old sines), Ocean (travelling waves, sharp crests), Calm lake (the same, low and gentle) |
| Wave Height, Wave Length, Wave Speed | existing: crest to trough, distance between crests, how fast the waves run |
| Wave Direction | degrees from +X towards +Z, which way the main swell runs |
| Wave Steepness | 0 round .. 1 sharp crests (never folds over) |
| Wave Count | 1 to 8 waves added together |
| Wave Seed | the same number gives the same sea |
| Current | a stream in the water, metres per second along X and Z |
| Depth | existing: 0 is a visual sheet. For the swimmer and the old prop float, depth must be above 0. A Buoyancy treats depth 0 as a flat sheet with no bottom |

The wave sum: wave 1 gets the main wavelength and the main direction (a little jitter); each next wave is shorter (divided by
1 + 0.55 i), turned by up to the spread (35 degrees), has 0.65 times the amplitude of the one before, and a seeded phase.
The amplitudes add up to half of Wave Height. Wave Speed scales time as it always did, and the scene's wind still quickens
the swell.

Code: `water->SurfaceAt(worldPoint, surface)` gives `height`, `floorY`, `normal` and `velocity`. `Water::SampleIn(list, point,
surface)` picks the highest water of several. `water->WaveTime()` and `SetWaveTime()` read or put the wave clock (tests, replays).

## Buoyancy

Add it to an object that has a Rigidbody3D and a collider (Add Component > Physics 3D > Buoyancy). Every physics step:

1. The hull has sample points: a Grid X by Grid Z layout across the collider box (3 by 3 by default), or your own list in
   `Points` (text `x,y,z;x,y,z`, in the object's space).
2. Each point stands for an equal share of the hull volume (`Volume`, or the collider box times `Fullness`). It pushes up
   with `density * 9.81 * volume * submerged fraction` at its own place. The submerged fraction runs from 0 to 1 as the
   water rises through a layer of `Hull Height`.
3. Drag slows the body relative to the moving water, separately along the nose, sideways and up, each with a linear
   (per second) and a quadratic (per metre per second) number.
4. Angular damping calms spin; the push is capped at `Max Push Acceleration` so a very light body stays calm.
5. A point that enters or leaves the water faster than `Splash Min Speed` raises a splash: `SplashCount()`, `LastSplash()`,
   the `onSplash` callback, and a spray of white particles.

A crate of mass 0.5 and size 1 m in water of density 1 floats with half of it under; mass 0.25 floats a quarter under; mass
above 1 sinks. Read-outs: `Submerged()`, `InWater()`, `Draft()`, `SurfaceUnder()`.

## Boat Controller

Add Component > Gameplay > Boat Controller on a hull that has a Rigidbody3D and a collider (a Buoyancy is added at Play if
missing), or use **GameObject > Vehicles > Boat**.

* **W / S** (or up / down, or the left stick, the "Move" action): throttle ahead and reverse. The throttle sets the speed it
  aims for: full = Max Speed (12 m/s), half = half of it. Letting go coasts. Pulling back while going forward brakes.
* **A / D**: steer. A turns left, D turns right. The rudder needs water flowing past it (speed, or the propeller wash from
  the throttle), so a boat at rest with no throttle does not turn; in reverse the steering flips like a car's.
* **Space** (the "Jump" action): brake.
* Out of the water the engine and the rudder do nothing (control follows how much of the hull is submerged).
* `Is Player` on reads the keys; off, only `throttleInput`, `steerInput` and `brakeInput` set from code move it (an AI boat).
* Hooks: `EngineRevs()` (0 to 1), the `onEngine(revs, speed)` callback, and an Audio Source on the same object has its pitch
  and volume tuned to the engine (Engine Sound).
* Looks: a foam wake (two flat trails behind the stern) and a bow splash. Camera: `Follow Camera`, `Cam Distance`,
  `Cam Height`, `Cam Lerp`.

## How to try it

See `docs/try_it.md`, entry `boat1`.

## Not done (said plainly)

* Boarding by pressing E (`VehicleSeat`) is not wired to boats: `VehicleSeat` is written against `VehicleController`. The
  boat drives itself when `Is Player` is ticked. Making the seat accept either kind of vehicle is the next step.
* The sample points use a vertical layer per point, not the true hull shape; a hull far from a box (a pointy V hull) is
  only approximated by `Fullness` and your own points.
* No GPU-specific water shader: Water is a CPU-animated mesh for every renderer, so the software renderer and the GPU paths
  draw the same mesh (this is also why they match). It has not been looked at on a GPU or on Windows.
* No foam texture, no underwater tint, no waves breaking on the shore.
* The editor changes (GameObject > Vehicles menu, Add Component entries) are written but the editor is not built here, so they
  are unbuilt.
