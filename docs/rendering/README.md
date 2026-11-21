# rendering/: How things look

> **What this is:** the list of pages in `docs/rendering/`, one line each. How things look: renderers, lighting, terrain and water, particles, textures.
>
> **Status:** index, made 2026-11-20 when the docs were sorted into folders. Back to the [start page](../README.md).
>
> **Last checked:** 2026-11-20.

- [Lighting](lighting.md): Lights, shadows, the lighting upgrade record and the day / night cycle.
- [Our own texture library (`okay::tex`)](own_texture_library.md): Mipmaps, atlases, format conversion, block compression and a streaming planner, written by us.
- [Particles, lines, and trails](particles.md): Particle systems, line renderers, and trail renderers are built on the CPU and drawn by the editor and the player on top of the 3D image.
- [Rendering](rendering.md): How a frame is drawn: post-processing, sky, fog, weather, and the honest status of each renderer (software, OpenGL, Direct3D 11, Direct3D 12).
- [Terrain and water](terrain_water.md): Heightmap and voxel terrain, water surfaces, floating things and the boat, the terrain audit and the voxel digging speed work.
