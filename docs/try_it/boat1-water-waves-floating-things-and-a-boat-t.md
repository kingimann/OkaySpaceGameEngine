---
id: boat1
row: | boat1 | Water waves, floating things and a boat to drive | `examples\boat\boat_lake.okayscene` (File > Open, Play), **GameObject > Vehicles > Boat**, Add Component > Physics 3D > Buoyancy and Gameplay > Boat Controller, Water Inspector > Style | New in this change; headless tests only, Windows and GPU not run |
---
## boat1. Water waves, floating things and a boat to drive

**What it is.** The sea now has real waves that things can float on, crates bob and settle at the right depth, heavy things
sink, and there is a boat you drive with the keyboard. Details and the list of what is not done: `docs/water_boat.md`.

**Steps (the example lake).**
1. Open the editor (`Tools\OkayEngine.exe`). **File > Open...** and pick `<install folder>\examples\boat\boat_lake.okayscene`.
2. Press **Play**.
3. **W** goes forward, **S** goes back, **A** turns left, **D** turns right, **Space** brakes. The camera follows the boat.

**What you should see.** A lake with rolling waves, a wooden dock on the west shore, a white boat with a red stripe beside
the dock pointing out over the water, six wooden crates floating near it that bob and drift, and a grey stone that
sinks to the bottom. Hold W: the boat speeds up to about 12 metres per second, rises and falls with the waves, leaves two
white foam trails behind and throws spray at the bow. Tap A or D while moving: it turns. Let go of W: it coasts and slows.
Drive into a crate: it is pushed away and bobs. Pushing the boat onto the shore stops it. The Console says nothing about errors.

**Steps (make your own boat).**
1. New 3D scene. **GameObject > Vehicles > Boat**. If the scene has no Water, a lake is made with it. Press Play, W / S / A / D.
2. Or by hand: make a Cube, scale it 2 x 0.8 x 4.6, add **Rigidbody3D** (mass 2.5), **Box Collider 3D**, **Buoyancy** (Fullness 0.55) and
   **Boat Controller** (Add Component). Put a **Water** under it (GameObject > 3D Object > Water).

**Steps (waves).** Select the Water. In the Inspector set **Style** to **Ocean** (sharp crests) or **Calm lake**. Change
**Direction**, **Steepness**, **Wave count** and **Seed** (the same seed always makes the same sea), and **Current** (a stream:
floating crates drift with it). Wave Height, Length and Speed work as before. Classic keeps the old look.

**Steps (floating).** Make a Cube with Rigidbody3D (mass 0.5), Box Collider 3D and **Buoyancy** above the water, press Play: it falls,
splashes, bobs and settles half under. Change the mass: 0.25 floats higher, 2 sinks. Select it and look at the Inspector:
Grid X/Z, Fullness, Drag numbers, Splash Effect.

**Steps (no more bouncing).** Open any old scene with Water (Depth above 0) and a Rigidbody crate, or make one: Cube + Rigidbody3D + Box Collider 3D (mass 0.5) 3 m above a Water with Depth 5. Press Play: it splashes in, dips once, and settles within a couple of seconds at the same height whatever the frame rate. Before this change it kept hopping at the surface.

**How you know it worked.** The crate at mass 0.5 ends with its middle at the water line; the boat does not flip over in the
waves; with the boat in the air (drag it up in the scene before Play) the engine does nothing until it lands.

**Status.** Tested headless on Linux (`boat` test: wave formula, draft, sinking, frame rate 30/60/144, no NaN, energy, steering,
reverse, saving and loading, the example file). **Unverified:** the Windows release, the editor window (the menu entry, Add
Component entries and Water fields are syntax-checked only), a GPU, what the wake and spray look like on screen, and how the
boat feels with a real keyboard. Boarding a boat with E is not done.
