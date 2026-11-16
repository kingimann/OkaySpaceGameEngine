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

## Water

**GameObject > 3D Object > Water** builds an animated wave mesh with a reflective, semi-transparent material. It follows the global wind. Rigidbodies float according to `fluidDensity` and `linearDrag`.

## What is not here

Vegetation painting and scattering are separate. World streaming of terrain cells is the World Streamer component (prefab files named by cell), not an automatic split of one terrain.
