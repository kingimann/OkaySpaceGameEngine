# Particles, lines, and trails

Particle systems, line renderers, and trail renderers are built on the CPU and drawn by the editor and the player on top of the 3D image. They are not inside the GPU mesh shaders.

## Particles

A Particle System emits from a point, circle, sphere, hemisphere, box, cone, edge, or the skin of the object's mesh. It has a rate, a rate per distance moved, bursts, lifetime, gravity, damping, wind (the field is called Affected by wind, `windInfluence`), and a ground plane or the scene's colliders. Colour, size, speed, spin, push and gravity can change over the particle's life. There are 17 one-click presets: Fire, Campfire, Smoke, Steam, Sparks, Explosion, Magic sparkle, Rain, Snow, Leaves, Dust, Confetti, Hit spark, Muzzle flash, Fireflies, Portal and Waterfall mist (GameObject > Effects > Particle Effects, or Add Component > Particle effect presets). Motion modules add noise, orbit, vortex, drift and inherited velocity.

Billboards face the camera. Stretch mode draws a streak along the velocity. Flat, Upright and Cubes modes, picture sheets, ribbon trails and soft edges are in Look extras. The whole list of changes, the tests and the numbers are in `docs/particles_upgrade.md`.

## Lines and trails

A Line Renderer is a ribbon through points you set in the Inspector, from a script, or from a spline. A Trail Renderer leaves a fading ribbon behind a moving object. Both can face the camera or lie flat, and can stretch or tile a texture.

## Depth

They are drawn after the meshes, back to front. Where a depth buffer is available they are hidden behind solid geometry:

- the software renderer publishes its depth every frame
- the player, and the editor when a GPU backend drew the frame, run a half-resolution depth pass when any particles, lines, or trails exist

Turn that pass off only by having none of those components in the scene.
