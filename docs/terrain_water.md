# Terrain and water

Heightmap terrain, voxel terrain, and water are scene components. They build a mesh on a Mesh Renderer and save their settings with the scene (the generated mesh is rebuilt on load).

## Heightmap terrain

Add one with **GameObject > Create > Terrain**. The Terrain component owns a square height grid. The Inspector and the Terrain sculpt tools raise, smooth, flatten, add noise, or erode it. Hydraulic and thermal erosion, terraces, and heightmap PNG import and export are on the component. Digging in Play (no caves) is a Terrain Digger on the same object.

The sibling Mesh Renderer is what every backend draws. Sculpting updates that mesh.

## Voxel terrain

**GameObject > Create > Voxel Terrain** makes a 3D density field meshed with marching cubes, so the ground can have caves, tunnels, and overhangs. Dig, Add, Smooth, Dig Box, and Remove Floaters edit the field. A Voxel Digger uses the left mouse to dig and the right mouse to add while playing.

## Water

**GameObject > Create > Water** builds an animated wave mesh with a reflective, semi-transparent material. It follows the global wind. Rigidbodies float according to `fluidDensity` and `linearDrag`.

## What is not here

Vegetation painting and scattering are separate. World streaming of terrain cells is the World Streamer component (prefab files named by cell), not an automatic split of one terrain.
