# Terrain and water

> **What this is:** Heightmap and voxel terrain, water surfaces, floating things and the boat, the terrain audit and the voxel digging speed work.
> **Status:** combined on 2026-11-20 from 5 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
> **Last checked:** 2026-11-20, paths and links against main.

## Contents

- [Terrain and water](#terrain-and-water-1) (was `docs/terrain_water.md`)
- [Water that is not a flat plane](#water-that-is-not-a-flat-plane) (was `docs/water_visuals.md`)
- [Water, floating things and the boat](#water-floating-things-and-the-boat) (was `docs/water_boat.md`)
- [Terrain audit: voxel terrain, heightmap terrain, water, foliage, streaming](#terrain-audit-voxel-terrain-heightmap-terrain-water-foliage-streaming) (was `docs/terrain_audit.md`)
- [Voxel terrain: why digging dropped the frame rate, and what was done](#voxel-terrain-why-digging-dropped-the-frame-rate-and-what-was-done) (was `docs/voxel_edit_perf.md`)

## Terrain and water

*Was `docs/terrain_water.md` until 2026-11-20.*

Heightmap terrain, voxel terrain, and water are scene components. They build a mesh on a Mesh Renderer and save their settings with the scene (the generated mesh is rebuilt on load).

### Heightmap terrain

Add one with **GameObject > 3D Object > Terrain**. The Terrain component owns a square height grid. The Inspector and the Terrain sculpt tools raise, smooth, flatten, add noise, or erode it. Hydraulic and thermal erosion, terraces, and heightmap PNG import and export are on the component. Digging in Play (no caves) is a Terrain Digger on the same object.

The sibling Mesh Renderer is what every backend draws. Sculpting updates that mesh.

### Voxel terrain

**GameObject > 3D Object > Voxel Terrain (caves)** makes a 3D density field meshed with marching cubes, so the ground can have caves, tunnels, and overhangs. Dig, Add, Smooth, Dig Box, and Remove Floaters edit the field. A Voxel Digger uses the left mouse to dig and the right mouse to add while playing.

#### Digging cost (how the surface reaches the screen)

The terrain is meshed in 16-cube chunks, and only the chunks an edit reached are meshed again. A vertex normal reads the
density two cells past its cube, so "reached" includes the chunks next to the edit (the old dirty box missed them and left
stale normals beside every hole; `tests/test_voxel_edit.cpp`).

How the surface goes to the renderer (`VoxelTerrain::renderMode`, set in code, not saved):

* **Auto** (default). While a scene is built or edited (and in the editor's Edit mode) the terrain keeps one combined mesh on
  its own Mesh Renderer, exactly as before. Once the RUNNING game edits it (the Voxel Digger, a script), the terrain switches to
  one small renderer object per chunk, named `Voxel Chunk x,y,z`, children of the terrain, never saved, with the terrain's
  own Mesh Renderer left empty. They copy its settings (shader, shadows, texture and the rest; later changes follow on the next
  frame). A dig then re-uploads only the chunks it touched instead of the whole world, and the renderers can cull chunks that
  are out of view. The picture is the same (checked pixel for pixel in the software renderer).
* **Single**. Always one combined mesh. **Chunked**. Always chunk objects.
* Set the environment variable `OKAY_VOXEL_OLD_REMESH=1` before starting the editor or player to get the previous behaviour back
  (one combined mesh, rebuilt and re-uploaded in full on every edit, with the original marching-cubes loop). Use it to compare.

Read what an edit costs in **View > Panels > Profiler**: `Voxel.Edit` (the brush and the dig effects), `Voxel.Mesh` (marching
cubes for the touched chunks) and `Voxel.Upload` (handing the result to the renderer). `VoxelTerrain::lastStats` has the same
numbers in code, and the Voxel Terrain card shows the last build. The numbers measured here are in `docs/rendering/terrain_water.md`.

Other costs that were removed on the way: Smooth copied the whole field on every call (now only the brush box), Remove
Floaters walked the whole field on every release (now only around the cells the stroke touched, after one full sweep), a
brush with radius 0 or a NaN strength wrote NaN into the field (now ignored), and one very long frame no longer digs a crater
(a stroke counts at most a tenth of a second per frame).

#### Real-life digging (materials, debris, building back)

Everything here is OFF in an old scene: a digger saved before this change digs and builds exactly as before. Turn it on from the
Inspector (Voxel Terrain card: **Materials**; Voxel Digger card: **Real life**) or from a script. Tests:
`tests/test_voxel_materials.cpp`, `tests/test_voxel_digger_real.cpp`; costs: `tests/bench_voxel_real.cpp`.

**Materials.** `VoxelTerrain::materials` is a table (name, colour, texture, hardness, density in kg/m3, loose, drop item, drop
per m3, dig sound, particle) and `material` holds one byte per voxel. `DefaultMaterials()` gives Dirt 0, Grass 1, Sand 2, Clay 3,
Stone 4, Ore 5, Snow 6. `GenerateMaterials(seed[, LayerRules])` fills it by depth, slope and height: grass on gentle top soil, dirt
under it with clay lenses, sand low down, snow in the top 15 percent, bare stone on steep slopes, stone deeper with ore pockets.
`EnableMaterials(id)` turns it on with every voxel the same material, `DisableMaterials()` turns it off. The marching-cubes
surface takes its colour from the most solid corner's material. Scenes save the bytes in a `voxelmats` record and the table in
`voxelmat` records; a scene without them loads as before.

**Nothing disappears.** `Edit(centre, radius, amount, EditOptions)` is `Dig`/`Add` with options and returns the exact volume taken or
added, per material (`removedBy`). A voxel counts the fraction of itself under the surface, so `SolidVolume()` changes by exactly
`removed - added` (checked to 1e-6). What the digger does with it is **Dug Ground**:

| Setting | What happens to the ground you dig |
|---|---|
| Vanish | It is gone (the old behaviour). |
| Store | It goes into the digger's store at once (`VoxelDigger::store`, cubic metres per material). |
| Debris | Small bodies fall; when one lies still (or its life runs out) its volume is built back into the ground where it lies. |
| Both | Bodies fall; walking up to one (main camera within 2.2 m) puts it into the store. Left alone it goes back into the ground. |

The debris limit (`debrisMax`, default 48) is strict: any more is put straight into the store. **Building needs material** takes
the volume you add out of the store (nothing happens with an empty store); **Creative** makes the store unlimited. The count can
be shown by a Text object (`hudTarget`), e.g. `Dirt 3.20 m3`; scripts read `store`, `Stored(id)` and `StoredByName("Dirt")`.
`giveItems` also hands each material's `drop` item to the scene's Inventory.

**Hardness and tools.** Digging a material is slowed by `hardness * hardnessScale`; a tool with `power` below the material's hardness
cannot dig it (default tool power is unlimited). Stone (4) digs about four times slower than dirt (1): measured 3.8 m3 against 13.0 m3
for the same two strokes.

**Brush options** (`EditOptions` / the digger's fields): shape (Sphere, Cube, Cylinder, flat Plane disc), depth limit below where
the stroke started, bedrock height (nothing below it can be dug), snap to the voxel grid. **Ctrl+Z** in Play (and
`VoxelDigger::UndoLastStroke()`) takes back the last whole stroke, terrain and materials. In the editor, Ctrl+Z is left to the game
while Playing.

**Real-life collapse.** After a dig, **Slump** moves loose voxels (sand, dirt, clay, snow) down and sideways to their angle of repose;
it only looks at the cells near the edit and moves at most 40 per frame, never the whole field (0.006 ms measured on a 655k voxel
field). **Collapse**: on release, pieces cut loose from the ground are lifted out as rigid bodies with their own surface mesh,
fall, land, and are built back into the ground (pieces under 8 voxels go to the store). `ExtractFloaters` and `PieceMesh` do this
in code.

**Presets** (`ApplyPreset`): Realistic = Both, building needs material, slump, collapse, tool power 4 (ore, 5, needs a better
tool); Arcade = everything vanishes, building is free (the old behaviour); Creative = unlimited store.

**More options (second pass).**
* **Moved, turned, scaled terrain**: aim, brush marker, dust, debris spawn and landing all work in the terrain's own space. The brush
  radius and debris volume are in terrain units.
* **Dug material feedback**: the dust colour is the dug material's `particle` colour, and when the digger's object has an Audio Source
  the material's `digSound` file becomes its clip path while that material is dug (`lastDigMaterial` says which). The windowed
  runtime loads `clipPath` files when Play starts, so a sound file only plays if that file was already loaded for the source;
  this part is unverified.
* **World-edge catch** (`catchFalling`, `catchMargin`, `onFall`): a body that fell more than `catchMargin` voxels below the grid is put back
  on the ground at the nearest point inside it with its speed cleared, or handed to `onFall` (a respawn hook). Checked every
  sixth frame over the scene's rigid bodies (0.017 ms per pass for 300 bodies, `bench_voxel_real`). Inside the grid's own width a body below
  the grid is pushed up by the ground, so the catch matters past the sides. Off by default; the Voxel Terrain card has the switch.
* **Avalanche** (`slumpPasses`): several bounded slump passes per frame, so loose ground runs farther down a slope.
* **Seed**: the Voxel Terrain card has a Seed field; the same seed gives the same ground and layers.

**Third pass.**
* **Tools from the inventory** (`toolsFromInventory`, `tools` = `item:power` list, `bareHandsPower`): the dig power is the best listed
  item the scene's Inventory holds. **Build from items** (`buildFromItems`): building spends the material's `drop` items
  (`dropPerM3` per cubic metre) and does nothing without them; `giveItems` makes dug ground arrive as the same items, so a stack
  can be picked up and placed again. Both are off by default and saved.
* **Ore veins** (`LayerRules::oreVeins`, `veinThickness`): winding worms of ore through the stone, deterministic for the seed.
  The Voxel Terrain card has sliders for topsoil, dirt depth, rock slope, ore pockets and veins.
* **Dig sound**: the digger loads the dug material's `digSound` file through the audio loader the first time it is used and
  plays it on its Audio Source.
* A tool too weak for the ground no longer erodes the surface at all (it used to lower the air cells next to it).

**Fourth pass.** All of these are off by default.
* **Cave-in rule** (`VoxelDigger::caveInSpan`, `VoxelTerrain::CaveIn(span)`): a ceiling (ground with air right under it) farther than
  `span` voxels sideways from a wall fails, with up to `span` voxels of ground above it. The failed ground falls as debris pieces when
  you let go of the mouse. A tunnel wider than about twice the span brings its roof down; a narrow one holds. It looks only around the
  edit plus `span + 2` and skips work boxes over 400000 cells; at most 6 pieces per release (the rest stay).
* **Budgeted generation for big worlds** (`BeginGenerate` / `StepGenerate(budgetMs)` / `Generating()` / `GenerateProgress()`): the very
  same world `Generate` + `GenerateMaterials` gives (tested bit for bit), filled one slice at a time within a time budget. In the running
  game the terrain steps itself by `generateBudgetMs` (default 4 ms) and skins itself when done. The field is only partly filled meanwhile.
* **Water and lava** (`fluidOn`, `PourFluid`, `fluidBudget`, `fluidRate`, `fluidMaxCells`, `fluidColor`, `fluidGlow`; the digger's `pourKey`
  pours at the aim point): a binary cellular fluid. A cell falls, or slides down a diagonal, through open air, and rests on ground or other
  fluid, so a pour fills a hole from the bottom and settles with a slope of at most 45 degrees (not a perfectly flat sheet; this is a limit).
  Bounds: `fluidBudget` active cells looked at per step (600), `fluidRate` steps per second (20, at most 4 per frame), `fluidMaxCells`
  (30000, more pouring is refused). Resting cells cost nothing; digging next to fluid wakes it; building ground over it removes that fluid.
  Drawn as one transient child object "Voxel Fluid", rebuilt about 10 times a second only while it moves. The fluid is not saved (the
  settings are). Fluid does not push or float rigid bodies and does not hurt anyone: it is only drawn and flows.
* **Footprints and tracks** (`footprints`, `footprintTag` "Player", `footprintBodies`, `footprintStep`, `footprintRadius`, `footprintDepth`,
  `footprintPower`): objects with the tag press a shallow dent into soft ground (power 2: dirt, grass, sand, snow, clay, not stone) every step
  metres they travel on the ground. At most 64 objects are looked at, every third frame. Dents are not part of a dig stroke, so Ctrl+Z does not undo them.
* **Foliage on voxel terrain**: `Foliage::SceneSurface` asks an unturned, evenly scaled voxel terrain's density field for the ground
  instead of ray casting its whole mesh; other surfaces are still ray cast.

Costs (CPU only, Linux, `-O2` build, per `Scene::Update` while holding the mouse; `bench_voxel_real`): the old digger 0.4 to 0.6 ms
median, the Realistic preset 1.0 to 1.3 ms, debris-heavy (150 live bodies, big brush) 4 to 8 ms, mostly physics. The frame that
carries a collapse costs more (p95 5 ms). Limits: one body per piece uses a box collider (a piece lands on its bounding box), and
the debris bodies assume the terrain object is at the origin with no rotation or scale.

### Water

**GameObject > 3D Object > Water** builds an animated wave mesh with a reflective, semi-transparent material. It follows the global wind. Rigidbodies float according to `fluidDensity` and `linearDrag`. Water also has wave styles (Classic, Ocean, Calm lake), a current and `SurfaceAt`, and a Buoyancy component floats a hull on several points and a Boat Controller drives it: see `docs/rendering/terrain_water.md`.

### What is not here

Vegetation painting and scattering are separate. World streaming of terrain cells is the World Streamer component (prefab files named by cell), not an automatic split of one terrain.

## Water that is not a flat plane

*Was `docs/water_visuals.md` until 2026-11-20.*

Owner request (2026-11-19): "improve the water so it's not just a flat plane". This page says what we already had, what looked flat or
fake, what was built and from what, the numbers, how to try it, and what is not done. Tests: `tests/test_water_visuals.cpp` (CTest
name `water_visuals`). Pictures, example scenes and the speed table come from `tests/water_shot.cpp` (`water_shot --shots|--scenes|--bench`).

The wave maths is shared with the boat work: `Environment/WaterWaves.hpp` (the `waves::Field`, written for `docs/rendering/terrain_water.md`) is the
one wave function. The drawn mesh, `Water::SurfaceAt` and the boat all call it, so the picture and the physics agree (a test checks that
every drawn vertex sits on the height `SurfaceAt` returns, to 2 cm). Nothing in the physics (buoyancy, forces) was changed here.

### Existing / Gap

Looked at first: `Components/Water.hpp`, the three renderers (`Render/SoftwareRenderer.hpp`, `GLRenderer`, `D3D11Renderer`), `docs/rendering/terrain_water.md`,
`docs/engine/volumes.md`, the sky, fog and wind code (`Render/Atmosphere.hpp`, `Scene::RenderSettings`, `Environment/Wind.hpp`), `ParticleSystem`, `Terrain`, and
the examples (`examples/boat`). Pictures of calm lake, sea, shore and river were rendered headless and looked at (not committed).

| Area | Existing | What looked flat or fake | What this change does |
|---|---|---|---|
| Wave shape | 3 crossing sines plus a wind wave; boat branch added Gerstner waves (`Ocean`, `Calm lake`) | the sines have round crests; a calm lake had nothing moving but one soft swell | **Sea State** (Calm, Light, Rough, Storm) sets the Gerstner numbers; ripples on top (below) |
| Small detail | `uvScroll` of an optional texture (none by default) | nothing fine moved on a calm surface: it looked like a tinted plane | `proc:water_normal` (our own tileable ripple normal map) as the normal map, plus per-vertex ripples from two noise layers drifting in two directions |
| Mesh | one square grid, same cell size everywhere, fixed on the object | waves near the camera were blocky, far ones wasted triangles; the edge of the square showed | **Rings of detail**: square rings, each twice as coarse as the one inside, centred on the camera and snapped to a lattice (no swimming), cracks closed; **Sea with no edge** |
| Colour | one tint for everything (`color`) | the water was the same blue over a beach and over deep water | depth colour (turquoise to deep blue) from the Terrain under it, or `Depth`, or `Assumed depth` |
| Reflection and glint | software renderer: Fresnel (Schlick) sky mirror, sun glint, `reflectivity`/`specular` (already there) | fine | kept as it is; the ripple normals now break the mirror up |
| Foam | none | no white on crests, none at the shore | white caps from crest height and slope, a surging foam line where the water meets the ground (depth near 0), foam on ring ripples |
| Light in the crests | none | thin waves looked like opaque plastic | green-blue glow in crests when the sun is behind them |
| See-through | one `opacity` for the whole surface | the bottom is equally hidden near the shore and far out | **not done** (the renderers have no per-vertex transparency); only the colour follows depth |
| Things in the water | none visible (buoyancy only) | a crate falling in made no splash | ring ripples that fade, spray from the existing `ParticleSystem`, a wake hook (`AddWake`) a boat can call |
| Underwater | none | camera under the surface saw the same as above | blue-green exponential fog from the scene's own fog settings (restored after), bubbles option |
| Rivers | `flow` scrolls a texture | the current did nothing you could see | `current` (boat branch) drags both ripple layers, and `FlowVector()` gives the world flow for the physics |
| Settings | many numbers, no presets | hard to get a good look | 8 presets, a Sea State list, a **Look** section with plain tooltips, **GameObject > 3D Object > Water** submenu |

### What was written, and from what (rule 19a)

All new code is ours, written from general published ideas, not from any other project.

* `engine/include/okay/Environment/WaterShading.hpp`: value noise and fractal sums (lattice hash and smooth interpolation); Schlick's
  Fresnel approximation; Beer-Lambert absorption (`T = exp(-sigma d)`, red faster than blue); a backlit-crest scattering term; a ring wave
  (`cos(k(d - ct))` under a moving Gaussian that fades with age and distance); caustics as ridges where two moving noise fields cross; sea-state
  numbers; the clipmap-style ring grid (nested square rings, doubling cell size, shared snapped centre, odd edge vertices pulled onto the
  neighbour's edge). Constants were chosen by us and checked by tests.
* `engine/include/okay/Render/ProcTexture.hpp` (`water_normal`): fractal noise heights, normal from wrapped neighbour differences.
* `Components/Water.hpp`: the look code (vertex colours, normals, rings, splashes, underwater) and the presets.
* `tests/water_scenes.hpp`: the example scenes, built from cubes, spheres, a Terrain and the Water.
* Wave formula: Gerstner (trochoidal) waves, in `WaterWaves.hpp` (see `docs/rendering/terrain_water.md`).

### How it works

`Water` has new settings (all off or neutral in an old scene; a scene saved before this change loads with the look off and looks as before):

| Setting | Meaning |
|---|---|
| Preset | Calm Lake, Pond, Ocean, Stormy Sea, River, Swamp, Tropical, Ice-cold: sets waves, colours, foam, ripples, current |
| Sea State | Custom, Calm, Light, Rough, Storm: wave height, length, sharpness, count, speed and foam |
| Realistic look | turns on everything below that paints the surface (off = the old plain plane) |
| Shallow / Deep colour, Clarity, Assumed depth | depth colour; Clarity = metres before it looks deep; the Terrain under the water gives the real depth |
| Foam, Shore foam | white caps (0..1); width in metres of the foam line at the shore (0 = none) |
| Crest glow | the glow when the sun is behind a wave |
| Ripples, Ripple size | the small never-stopping ripples (0 off .. 1 strong) and their size |
| Rings of detail, Rings, Sea with no edge | fine near the camera; `Detail` (resolution) is the cells across each ring |
| Splashes and ripples | things that fall in or move through make rings and spray while playing |
| Underwater look, Bubbles | fog under the surface; rising bubbles |

How the picture is made: every frame the mesh is rebuilt (as before). Each vertex gets its height (and sideways shift) from the wave field,
a normal from the field's slope bent by the ripple slope, a colour from `WaterShading` (depth colour, foam, glow), and a world-anchored UV
for the ripple normal map. The colour is stored as the mesh's vertex colour and the normal as the mesh normal, which the software, GL and D3D11
renderers all already draw, so no renderer needed a new feature (the GL and D3D11 look is therefore unverified, not missing).

Code hooks: `water->AddRipple(worldPoint, strength)`, `water->AddWake(worldPoint, velocity, dt, id)` (call every frame from a boat; it lays one ring
per half metre travelled, at any frame rate), `water->FlowVector()` (world flow, metres per second; the water does not push anything itself),
`water->CameraIsUnder()`, `water->SetSeaState(n)`, `water->ApplyPreset(n)`.

### Numbers
Test: `water_visuals`, 204 checks, 0 failures (Linux, software renderer, headless). It covers: noise, ripples (never flat, off = flat, drift with the
current), Fresnel (2 percent straight down, 100 percent grazing, rising), absorption and depth colour, crest and shore foam, crest glow, ring waves
(front speed 1.5 m/s, fade, a splash lifts the surface by centimetres, never metres), caustics, underwater fog, sea states (height, steepness, foam all rise
Calm to Storm), the ring grid (exact area of the square, no overlap, every vertex on its level's lattice, the whole grid moves by exactly one coarse cell when
the camera does, a small camera move changes nothing, stitched vertices sit half way between their neighbours, clamping keeps a lake's edge), the component
(old water unchanged, vertex data finite and in range, a beach gives turquoise shallows and a lighter foam line, rings dense near the camera, 215 of 217 sampled
vertices within 2 cm of the surface the boat reads (the others are the seam vertices pulled onto the coarse edge), 30 and 144 fps give the same surface to 2 mm,
splash rings the same at 30 and 144 fps, the wake hook lays the same number of rings at 20 and 60 fps, rings are capped at 64, a crate dropped in makes
rings and spray, fog turns on under the surface and the old fog comes back, 8 presets, sea states), saving and loading (round trip, old file without the record,
damaged numbers repaired), properties, and pictures with tolerances: a calm lake is not one flat colour, a storm varies more and has 14 percent of its vertices
painted foam against 0 for the lake, the same picture twice is identical, 30 and 120 fps give the same picture to within 2.5 grey levels, the old look is
clearly different from the new one (variance 10 against 47), and under the surface the picture is bluer.

Speed of the water mesh (`water_shot --bench`), a 100 x 100 m surface, milliseconds per frame, Release, one core, **measured while the machine was
heavily loaded by other jobs (load average 13 on 4 cores), so real numbers are lower, perhaps by a factor of 2 to 4**. Mesh build only (no drawing):

| Resolution | Old look (Classic sines) | Realistic look | Realistic look + rings of detail (4 rings) |
|---|---|---|---|
| 48 | 0.4 | 3.5 | 9.7 |
| 96 | 1.0 | 12.3 | 36 |
| 192 | 4.0 | 49 | 151 |

Rings multiply the vertex count (resolution 48 with 4 rings is about 7,700 vertices, the finest cell is 26 cm, against 2.1 m cells in the flat grid), so they are
the costly setting; the presets use Detail 48 (the editor's menu sets it). The mesh is rebuilt every frame as it always was; a later step could rebuild only
the far rings less often.

Pictures (software renderer, made by `water_shot --shots <dir>`, not committed): `lake`, `ocean_calm`, `ocean_rough`, `ocean_storm`, `ocean_old_look`, `river`,
`shore`, `underwater`. Seen by the author: the old look is a flat pale sheet; the new ocean has rolling crests with white streaks, the river has flowing ripples
between banks, the shore goes from clear turquoise towards deeper blue (the foam line is only faintly visible from the example camera: shore foam is checked by numbers, not yet by eye), and under the surface the view is green-blue fog.

### Not done

* Refraction (the bottom shifted by the surface) and see-through that grows with depth: the renderers have one opacity per material and no
  per-vertex alpha, and no refraction hook. The bottom is seen through the same opacity everywhere; only the colour follows depth.
* Caustic light on the bottom: `watershade::Caustics` exists and is tested, but no renderer hook puts a light pattern on other objects.
* GL and D3D11: no shader work was needed (vertex colours and the normal map are existing features) and none was run. Windows was not run.
* Foam is painted per vertex, so it is as fine as the grid (rings make it fine near the camera); there is no foam texture that follows the wave.
* Foam from a wave folding over (a Jacobian) is not used; the foam uses crest height and slope.
* The wake hook is called by a script or the boat (the boat agent can adopt it); nothing in the boat code was changed here.
* Rings have no effect on floating things (visual only).

## Water, floating things and the boat

*Was `docs/water_boat.md` until 2026-11-20.*

Owner request (2026-11-19): "improve water physics, add a basic boat I can use on water". This page says what we already
had, what was missing, what was built, and how to try it. Tests: `tests/test_boat.cpp` (CTest name `boat`).

### Existing / Gap

Searched first: `docs/rendering/terrain_water.md`, `Components/Water.hpp`, `Environment/Wind.hpp`, the Rigidbody3D / Physics3D headers,
`VehicleController`, `VehicleSeat`, the feature map (2.6.5 Water, 4.2.6 Swimming), `docs/status/UNFINISHED.md` (no water or boat row),
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

### What was written, and from what (rule 19a)

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

### Why things bounced on the water, and the fix

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

### The water surface

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

### Buoyancy

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

### Boat Controller

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

### How to try it

See `docs/try_it.md`, entry `boat1`.

### Not done (said plainly)

* Boarding by pressing E (`VehicleSeat`) is not wired to boats: `VehicleSeat` is written against `VehicleController`. The
  boat drives itself when `Is Player` is ticked. Making the seat accept either kind of vehicle is the next step.
* The sample points use a vertical layer per point, not the true hull shape; a hull far from a box (a pointy V hull) is
  only approximated by `Fullness` and your own points.
* No GPU-specific water shader: Water is a CPU-animated mesh for every renderer, so the software renderer and the GPU paths
  draw the same mesh (this is also why they match). It has not been looked at on a GPU or on Windows.
* No foam texture, no underwater tint, no waves breaking on the shore.
* The editor changes (GameObject > Vehicles menu, Add Component entries) are written but the editor is not built here, so they
  are unbuilt.

## Terrain audit: voxel terrain, heightmap terrain, water, foliage, streaming

*Was `docs/terrain_audit.md` until 2026-11-20.*

What each part does, whether it is **OK**, a **BUG**, **SLOW** or a **LIMIT** (limitation), the number or behaviour seen, and what
was done. Written on branch `claude/voxel-edit-fps`. Machine: shared 4 cores, `-O2` for the benches (the player's physics step
and a few probes ran against an `-O0` library, said where). No GPU, no Windows. Tests for the fixes: `test_voxel_edit`,
`test_voxel_chunks`, `test_mesh_hash`, `test_terrain_water_speed`, `test_terrain_audit`. The dig frame rate itself is in
`docs/rendering/terrain_water.md`.

### Voxel terrain (`VoxelTerrain`, `VoxelDigger`)

| Area | What it does | Verdict | Seen | Done |
|---|---|---|---|---|
| Generation | Fractal noise surface + optional caves, clamped to a band of 3 voxels, bottom row sealed | OK | 1 ms (Sandbox), 5 ms (160x64x160), 13 ms (256x64x256) | - |
| Density storage | one `float` per cell, row-major | OK / LIMIT | 0.9 MB Sandbox, 6.2 MB large, 16 MB huge; the Inspector lets the grid go to 512 per side (134 M cells, 537 MB, no guard) | not changed (listed) |
| Save / load | one byte per cell (signed, over the clamp band), base64, one text line | OK / LIMIT | text 0.29 MB / 2.1 MB / 5.3 MB; save 9 / 16 / 38 ms; load + first mesh 13 / 72 / 308 ms; worst density error 0.018 (voxel 1.5), 0.012 (voxel 1.0); a blob of the wrong size is dropped without a message (the terrain loads empty) | per-face colours no longer saved (0.4 MB of the Sandbox scene, rebuilt on load); density text unchanged |
| Marching cubes | 16-cube chunks, gradient normals read the field half a cell either side | OK, was SLOW | 0.7 ms for 2 chunks | 2.7x faster, bit-identical to the kept reference loop (`BuildMeshReference`) |
| Normals at chunk borders | a vertex reads cells c-1 .. c+2 | **BUG** | the dirty box marked only c-1 .. c: 4 of 60 random edits left a mesh different from a full rebuild (stale normals beside every hole) | exact dirty set; border and grid-edge cells tested one at a time |
| Edits near the grid edge | brush box is clamped to the grid; `SampleDensity` clamps coordinates | OK | no crash or hole in 25 random edits on 7 grids | - |
| Brush input | `Dig`, `Add`, `SmoothAt`, `DigBox` | **BUG** | radius 0 (cell exactly under the brush), a NaN strength or position wrote NaN into the field; 5 of 7 checks failed | ignored now |
| Smooth | relaxes the box from a snapshot | SLOW | copied the whole field every call: 0.5 ms (large), 3.2 ms (huge) per frame | copies the box only; results bit-identical (tested) |
| Remove Floaters | flood fill on release | SLOW | 2-3 ms Sandbox, 20 ms large, 86 ms huge on EVERY release; found crumbs re-meshed every chunk (12 ms Sandbox, 190 ms huge) | one full sweep, then local (0.004-0.02 ms) and only those chunks re-meshed; same cells cleared (40 random rounds tested) |
| Stroke strength | `strength x dt` per frame | LIMIT | one 0.5 s hitch dug 0.5 s worth at once | at most 0.1 s per frame |
| Aim | ray from the camera | **BUG** | used the first camera in the scene (a minimap camera that comes first steered the brush); searched the scene every frame | scene main camera; remembers the terrain |
| Memory | chunk meshes (kept) + combined mesh | OK / SLOW | the old path held the surface twice (17 MB x 2 on the huge terrain) | chunk objects hold it once |
| Draw | one mesh | SLOW | the whole terrain re-hashed, re-expanded and uploaded per edit | chunk objects (see perf doc) |
| World edge | collision only inside the grid | LIMIT | a body 3 m past the edge fell to y = -106 m in 5 s; the sides of the block are open in the picture | listed in UNFINISHED |
| Physics vs voxels | Physics3D section 5: capsule of probe spheres against the field | LIMIT (physics lane) | 100 bodies 1.1 ms, 400 bodies 14 ms per step (`-O0` library, so about 3x less in a release); my probe bodies never reached "asleep" (unconfirmed why) | not touched |
| Foliage on a voxel terrain | brush casts rays at every mesh | SLOW | `RaycastSurfaces` 1.1 ms a ray (Sandbox), 5.1 ms (large) against one mesh, 0.32 / 1.7 ms against chunk objects (`-O0` library); a brush stamp is up to 1600 rays | listed (needs a field raycast) |
| Editor | Generate / Solid Slab / Clear on the card; no Scene-view dig tool (digging is Play only) | LIMIT | - | build line added to the card |

### Heightmap terrain (`Terrain`, `TerrainDigger`)

| Area | What it does | Verdict | Seen | Done |
|---|---|---|---|---|
| `Terrain::Apply` | rebuilds the whole mesh; normals by `Mesh::ComputeSmoothNormals`, which welds every vertex through a `std::map` | **SLOW** | per Apply, digging calls it every frame: 1.9 ms at 64x64, 8.2 ms at 128, 35 ms at 256, 169 ms at 512 | normals added up without the weld: 0.28 / 1.3 / 4.8 / 24 ms; the normals are bit-identical (7 grids tested), `SmoothNoWeld` itself 17.7 -> 0.7 ms on 66 k vertices |
| `TerrainDigger` input | digs while held | **BUG** | dug while a bag/chest/menu or text box was open (it only checked pause); aimed from the first camera | `GameInputBlocked` and the main camera, like the Voxel Digger |
| Brushes | `BrushSpan` limits the cells touched | OK | brush 0.003 ms | - |
| Dig floor | `digDepth` stops a held dig | OK | - | - |
| Save | heights as text, layers as run-length | not measured | - | - |

### Water

| Area | What it does | Verdict | Seen | Done |
|---|---|---|---|---|
| Wave mesh | rebuilt every frame: positions, indices, `ComputeSmoothNormals` | **SLOW** | 0.55 ms (48), 2.4 ms (96), 4.5 ms (128) per frame, always, even idle; the Sandbox lake is 48 | same normals without the weld: 0.077 / 0.32 / 0.58 ms |
| Buoyancy | per body, from the collider box | OK | - | - |
| Renderers | a new mesh each frame, so GL / D3D11 expand and upload it each frame | LIMIT | 4608 triangles in the Sandbox | listed |

### World streaming (`WorldStreamer`)

| Area | What it does | Verdict | Seen | Done |
|---|---|---|---|---|
| Cells | loads prefab files near the target, unloads past the ring | OK | `test_worldstreamer` 75 checks | - |
| Edited terrain | an unloaded cell is destroyed | **BUG** | a dug voxel cell came back whole after walking away and returning | a cell whose terrain was edited (`VoxelTerrain::editCount`) is kept loaded |
| Hitches | cells load synchronously, all new ones in one frame | LIMIT | by reading: a ring of 5 new cells in one frame | listed |

### Second pass (real-life digging and a moved terrain)

| Area | What it does | Verdict | Seen | Done |
|---|---|---|---|---|
| Voxel Digger aim | the ray was turned into the terrain's space by subtracting its position only | **BUG** | by reading: a terrain that is rotated or scaled was dug at the wrong place, the brush marker and the dust too | the aim, the marker, the dust and the debris spawn use the terrain's own transform (`tests/test_voxel_more.cpp`, three moved, turned and scaled terrains) |
| Debris landing | the landing spot was the debris position minus the terrain position | **BUG** | same: volume was built back in the wrong place on a turned or scaled terrain | worked out with `InverseTransformPoint`; debris also takes the terrain's rotation and scale |
| World edge | a body past the side of the grid falls for ever | **BUG** (was a listed limit) | y = -106 m after 5 s | option `VoxelTerrain::catchFalling` puts it back on the ground, or calls `onFall`; off by default |
| Debris cap | counted at the first frame of a piece, so a frame could go over the cap | **BUG** | peak 51 with a cap of 48 | counted when the piece is made |
| Piece colliders | one box per piece | LIMIT | by reading | listed |
| Foliage ray cost on voxel meshes | 1.1 ms per ray single, 0.32 ms chunked | **SLOW** | measured earlier | the foliage brush asks an unturned voxel terrain's density field (`tests/test_voxel_more.cpp` times both) |
| Network | dig edits are local only: in multiplayer each player's terrain would differ | LIMIT | by reading: `VoxelTerrain` has no replication, edits are not sent | listed (the host would have to own the field and send `Edit` calls) |

### Bug hunt (third pass, `tests/test_voxel_bughunt.cpp` and `tests/test_voxel_more.cpp`)

Each row was tried with a test. "OK" means the test passed without a change.

| Way to break it | Result | Done |
|---|---|---|
| Grids of 0, 1, 2, 3x1x5 cells: every brush, slump, loose pieces, undo, save, load, Update | OK (a grid is never smaller than 2 cells per side, `Resize` clamps) | - |
| NaN, infinite, enormous (1e30) and tiny (1e-30) radius, centre, amount, tool power, bedrock height, depth limit, volume | OK, the field stays finite; `AddVolume` of 1e12 stops | - |
| Edits on every grid corner, edge and just outside the grid, with and without materials, then compare the drawn surface with a full rebuild | OK, 0 triangles differ (the chunked surface and normals at the borders agree) | - |
| 3000 fast random edits in one stroke with slumps, then Ctrl+Z | OK, exact restore of density and materials | - |
| A stroke begun twice without ending | OK | - |
| Save, load, save, load | OK, worst density error 0.015, no drift on later round trips, materials equal | - |
| Play, dig with the Realistic digger, Stop (snapshot restored) | OK, field back within 0.05, no undo history, store not saved, debris not saved | - |
| Two terrains in one scene, camera over the second | OK, only the aimed terrain changes; destroying one leaves the other's chunks | - |
| Terrain under an inactive parent | OK, reactivating shows the dug terrain | - |
| A tool too weak for the ground (`power` below the hardness) | **BUG**: it still lowered the air cells beside the surface, so the surface eroded a little although `removed` was 0 | an air cell may now only change if a solid neighbour is a material the tool can dig |
| Brush marker with Cube, Cylinder or Plane brush | **BUG** (looks): the marker was always a sphere | the marker mesh and scale follow the shape |
| Editor undo and redo of voxel edits | UNVERIFIED: the Inspector buttons (Generate, Solid Slab, Clear, Layered ground) are scene edits; Ctrl+Z in Play belongs to the digger. No headless way to press the editor's undo | listed |
| Streaming of edited voxel cells | covered by the first audit (`editCount` keeps the cell) | - |
| Dig edits over the network | LIMIT: not replicated | listed |

### Whole-frame hunt: "low fps in the Sandbox, and it drops when I dig" (release 2.878.0)

Measured headless with `tests/bench_voxel_frame.cpp` (Linux, `-O2`, CPU only; no GPU, no Windows window). The New Project > Sandbox template and
`voxel_big_world.okayscene` are built exactly as the editor builds them and stepped with the running game's input.

**What the scene is.** Sandbox: 10 objects, 18010 triangles (the terrain is 13102 of them, 75 chunks), so a real GPU has almost nothing to draw. Big world:
43912 triangles, 256 chunks. A frame of the game's own update (`Scene::Update`: player, animator, physics against the density field, water,
digger, terrain) costs 0.17 ms idle, 0.18 ms walking, 0.17 ms looking around and 0.45 ms (worst 5 ms, the first click that builds the chunk objects)
while digging, on the Sandbox; 0.94 ms while digging in the big world. Taking parts away: no physics 0.15, no water 0.09, no player 0.10, no digger 0.15
ms. Nothing in the update is slow. The dig click itself (field edit, remesh, hand-off) is 0.45 to 0.75 ms (`last rebuild` line of the bench).

| Where | Finding | Verdict | Before | After |
|---|---|---|---|---|
| GPU hosts (player, editor): the half-resolution software depth pass for particles | `FillOcclusionDepth` is a **whole software render of the scene, with three 2048 x 2048 shadow cascades**. It ran every frame whenever ANY ParticleSystem existed, even with no live particle. The Voxel Digger creates its dust ParticleSystem on the first click and keeps it, so from the first dig on every frame paid for it | **BUG, the cause of the dig drop** | 213 to 249 ms per frame at 640 x 360 (Sandbox), 355 ms big world, from the first dig on | 10.5 ms at the 427 x 240 cap now used (Sandbox), 29 ms big world, and only while live particles, lines or trails exist (`SceneHasOverlayNeedingDepth`) |
| The same pass: what it draws | it also ran FXAA, tone mapping, SSAO, bloom and grading on a picture nobody sees | SLOW | included above | switched off for that pass (the depth is the same to within edge pixels: test) |
| Software renderer (a machine where the GPU renderer did not start or failed): shadows | three 2048 x 2048 cascades are cleared and drawn every frame | **SLOW** | 244 ms per 960 x 540 frame (4 fps); the shadow maps alone 160 to 256 ms | `SoftwareShadowCap` (default 1024 texels, `OKAY_SOFTWARE_SHADOW_CAP=4096` brings the old sharpness back; the GPU renderers are not affected): 114 to 125 ms; shadows off is 26 ms |
| Player: when the GPU renderer fails 3 times in a row | the player never tries it again (`gpuFails >= 3`) and stays on the software renderer for the rest of the run | LIMIT | by reading | listed; the F3 panel now says which renderer is drawing |
| Editor Game view: the same fallback | retries after 5 s | OK | - | - |
| Per-frame work in VoxelTerrain / Digger / foliage / water / fluid / footprints when switched off | `Update` returns after a few flag tests; no scans, no allocation, no hashing | OK | 0.0002 ms (terrain) and 0.016 ms (digger) smoothed, shown in F3 | - |
| Physics against the voxel field | samples the density, no collider is built or rebuilt on an edit | OK | - | - |
| Undo record of a stroke | the stroke log keeps one entry per changed cell, not the field | OK | - | - |
| Mesh upload on an edit (GL / D3D11) | only the chunks that changed get a new buffer; D3D12 (opt-in) still re-expands every visible mesh every frame | LIMIT (D3D12) | - | listed |
| Draw calls and frustum culling of the one combined terrain mesh (before the first dig) | one draw call, not culled by chunk | LIMIT | 13102 triangles: trivial for a GPU | listed |

**Not measured (no GPU, no Windows window here):** the real GPU time, the read-back of the finished frame that the player does every frame, driver upload
costs, Direct3D 11 / OpenGL draw-call time, and whether the owner's machine runs on the software renderer at all. The F3 panel and the Game view stats
now print the renderer in use, the voxel triangle and chunk counts and the terrain and digger cost per frame, so the owner can read where the time goes.

### What is not covered

Physics core (another lane), the player controller (another lane), vegetation placement quality, heightmap terrain save size, Direct3D 12.

## Voxel terrain: why digging dropped the frame rate, and what was done

*Was `docs/voxel_edit_perf.md` until 2026-11-20.*

Owner report: "when i try to dig or add my fps drops". This page says what was measured, what the cause was, what changed and what
is still unproven. Branch `claude/voxel-edit-fps`. Written from the CPU numbers of a headless run; there was **no GPU** here.

### How it was measured

`tests/bench_voxel_edit.cpp` (target `bench_voxel_edit`, never run by ctest) drives the real **Voxel Digger**: a camera 6 m above
the ground sweeps at about 2 m/s, the left mouse button is held (`Input::FeedMouse`), 60 frames a second, brush radius 2.5,
strength 8 per second, 150 frames. Per frame it records the whole `Scene::Update`, the Profiler rows `Voxel.Edit`, `Voxel.Mesh`
and `Voxel.Upload`, and a CPU **replica** of what the OpenGL renderer does with every mesh that changed
(`GLRenderer::EnsureMeshVB`: the change check, the expansion into its interleaved layout, the exact content hash). The GPU
upload and the driver are not measured. Median of the frames, three repeats, `-O2`, a shared 4-core machine at load about 3
(`scratchpad` output kept in the PR). Four terrains: the class default 64x36x64 @0.8, the owner's **Sandbox** 72x44x72 @1.5,
a large 160x64x160 and a huge 256x64x256.

### What the numbers said (before)

CPU milliseconds per edit frame, median:

| terrain | triangles | Voxel.Mesh | combine | renderer: expand + hash | bytes re-expanded for upload | total |
|---|---|---|---|---|---|---|
| default 64x36x64 | 11 k | 1.9 | 0.2 | 0.18 + 1.44 | 1.7 MB | **3.8** |
| Sandbox 72x44x72 | 13 k | 0.7 | 0.2 | 0.22 + 1.70 | 2.1 MB | **2.9** |
| large 160x64x160 | 60 k | 1.2 | 1.0 | 1.1 + 7.8 | 9.6 MB | **11.3** |
| huge 256x64x256 | 144 k | 1.3 | 3.4 | 3.5 + 19.2 | 23 MB | **27.4** |

The brush itself costs nothing (0.002 ms). The cost grows with the size of the WHOLE terrain, not of the brush: every edit frame
the terrain copied every chunk into a new combined mesh, handed the renderers a "new" mesh, and each renderer expanded all of it
again, hashed all of it byte by byte (about 1.3 ns a byte, 1.7 ms for the Sandbox's 1.5 MB) and uploaded it. Other costs found:

* the hash is also run once a second on every static mesh (1.7 ms Sandbox, 19 ms huge), and its cheap per-frame check costs about
  25 microseconds per mesh in view;
* releasing the button swept the whole field for floating crumbs: 2-3 ms (Sandbox), 20 ms (large), 86 ms (huge), and when it
  found anything it re-meshed EVERY chunk (12 ms Sandbox, 190 ms huge);
* Smooth copied the whole field on every call (0.5 ms large, 3.2 ms huge per frame);
* the dirty box ignored that a vertex normal reads two cells past its cube, so cubes next to a dig kept stale normals (4 of 60
  random edits gave a mesh different from a full rebuild; `tests/test_voxel_edit.cpp` failed on the old code);
* a brush with radius 0 or a NaN strength or position wrote NaN into the field (5 of 7 checks failed on the old code).

### What changed

* **Chunk objects** (`VoxelTerrain::renderMode`, default Auto). Scenes, tests and the editor's Edit mode keep one combined mesh,
  exactly as before. Once the RUNNING game edits the terrain it is drawn by one small transient renderer object per 16-cube chunk
  (`Voxel Chunk x,y,z`, never saved, material copied from the terrain's Mesh Renderer, which is left empty). A dig re-uploads only
  the chunks it touched (0.17-0.34 MB a frame instead of 1.3-17 MB) and renderers can cull chunks out of view. Same picture:
  `test_voxel_chunks` renders both ways with the software renderer and compares every pixel.
* **Exact dirty set**, including the neighbours a normal reads; edits near chunk borders and grid edges are tested one cell at a time.
* **Faster marching cubes, same bits**: cubes with no surface are dropped before any work, density is read without clamping when
  the sample cube is inside the grid, and each shared edge vertex gets its normal once. The original loop is kept as
  `BuildMeshReference()` and `test_voxel_chunks` demands identical bytes from both on seven grids after random edits. About 2.7x
  faster per chunk.
* **Renderer hash** (`engine/include/okay/Render/MeshHash.hpp`, used by `GLRenderer.cpp` and `D3D11Renderer.cpp`): four 8-byte lanes
  instead of one byte at a time, about 15x faster (`tests/test_mesh_hash.cpp`: every single bit of buffers of 26 sizes changes
  the hash). It only compares values made by itself, so nothing depends on the old numbers. This also speeds up every other mesh.
* **Floaters**: after one full sweep, a release only looks around the cells the stroke touched (0.004-0.02 ms) and only re-meshes
  those chunks. The first sweep (and an explicit `RemoveFloaters`) is a little slower than before (4/34/104 ms for Sandbox/large/huge).
* Smooth copies only its brush box. Bad brush numbers are ignored. One long frame digs at most a tenth of a second's worth.
  The digger aims from the scene's main camera and remembers where the terrain is instead of searching the scene every frame.
* Profiler rows `Voxel.Edit`, `Voxel.Mesh`, `Voxel.Upload`; `VoxelTerrain::lastStats`; a line on the Voxel Terrain card.
* Saved scenes are smaller: the terrain's per-face colours (0.4 MB of the Sandbox scene) were written and rebuilt on load; they are
  no longer written. The density text is unchanged, old scenes load the same.

### After

Same machine, same runs (total CPU per edit frame, ms; "chunks" = chunk objects with the OLD hash, "after" = chunk objects and the new hash):

| terrain | before | chunks only | after | meshes the renderer checks per frame |
|---|---|---|---|---|
| default 64x36x64 | 3.83 | 1.69 | **0.87** | 1 -> 26 |
| Sandbox 72x44x72 | 2.96 | 1.11 | **0.44** | 1 -> 26 |
| large 160x64x160 | 11.3 | 4.1 | **1.05** | 1 -> 147 |
| huge 256x64x256 | 27.4 | 8.8 | **1.71** | 1 -> 310 |

Chunk objects alone cost a per-frame check for every chunk in view (3 ms for 147 meshes with the old hash), which is why the faster
hash belongs with them. Idle exact hash of the whole terrain once a second: 1.7 -> 0.23 ms (Sandbox), 19 -> 3.7 ms (huge).

### What is not known

* GPU upload and driver time, real frame rates, and Windows: none of it was run. The CPU numbers are a replica of the OpenGL path;
  Direct3D 11 has the same code; Direct3D 12 expands every visible mesh every frame whether or not you dig (not changed).
* The editor click path (Inspector line, Profiler rows) was compile-checked only (`editor/main.cpp` syntax-checked).
* Software renderer: per-frame cost is the triangles drawn, so digging is not special there.

### Try it

`docs/try_it.md` section 7s. Switch back to the old behaviour with `OKAY_VOXEL_OLD_REMESH=1`.
