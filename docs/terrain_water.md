# Terrain and water

Heightmap terrain, voxel terrain, and water are scene components. They build a mesh on a Mesh Renderer and save their settings with the scene (the generated mesh is rebuilt on load).

## Heightmap terrain

Add one with **GameObject > 3D Object > Terrain**. The Terrain component owns a square height grid. The Inspector and the Terrain sculpt tools raise, smooth, flatten, add noise, or erode it. Hydraulic and thermal erosion, terraces, and heightmap PNG import and export are on the component. Digging in Play (no caves) is a Terrain Digger on the same object.

The sibling Mesh Renderer is what every backend draws. Sculpting updates that mesh.

## Voxel terrain

**GameObject > 3D Object > Voxel Terrain (caves)** makes a 3D density field meshed with marching cubes, so the ground can have caves, tunnels, and overhangs. Dig, Add, Smooth, Dig Box, and Remove Floaters edit the field. A Voxel Digger uses the left mouse to dig and the right mouse to add while playing.

### Digging cost (how the surface reaches the screen)

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
numbers in code, and the Voxel Terrain card shows the last build. The numbers measured here are in `docs/voxel_edit_perf.md`.

Other costs that were removed on the way: Smooth copied the whole field on every call (now only the brush box), Remove
Floaters walked the whole field on every release (now only around the cells the stroke touched, after one full sweep), a
brush with radius 0 or a NaN strength wrote NaN into the field (now ignored), and one very long frame no longer digs a crater
(a stroke counts at most a tenth of a second per frame).

### Real-life digging (materials, debris, building back)

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
  sixth frame over the scene's rigid bodies. Off by default; the Voxel Terrain card has the switch.
* **Avalanche** (`slumpPasses`): several bounded slump passes per frame, so loose ground runs farther down a slope.
* **Seed**: the Voxel Terrain card has a Seed field; the same seed gives the same ground and layers.

Costs (CPU only, Linux, `-O2` build, per `Scene::Update` while holding the mouse; `bench_voxel_real`): the old digger 0.4 to 0.6 ms
median, the Realistic preset 1.0 to 1.3 ms, debris-heavy (150 live bodies, big brush) 4 to 8 ms, mostly physics. The frame that
carries a collapse costs more (p95 5 ms). Limits: one body per piece uses a box collider (a piece lands on its bounding box), and
the debris bodies assume the terrain object is at the origin with no rotation or scale.

## Water

**GameObject > 3D Object > Water** builds an animated wave mesh with a reflective, semi-transparent material. It follows the global wind. Rigidbodies float according to `fluidDensity` and `linearDrag`.

## What is not here

Vegetation painting and scattering are separate. World streaming of terrain cells is the World Streamer component (prefab files named by cell), not an automatic split of one terrain.
