# Particles, lines, and trails

Particle systems, line renderers, and trail renderers are built on the CPU and drawn by the editor and the player on top of the 3D image. They are not inside the GPU mesh shaders.

## Particles

A Particle System emits from a point, circle, sphere, box, cone, or edge. It has a rate, bursts, lifetime, gravity, damping, wind, and an optional ground plane. Colour, size, and speed can change over the particle's life. Presets cover fire, smoke, sparks, explosion, rain, snow, magic, and dust. Motion modules add noise, orbit, vortex, and inherited velocity.

Billboards face the camera. Stretch mode draws a streak along the velocity.

## Lines and trails

A Line Renderer is a ribbon through points you set in the Inspector, from a script, or from a spline. A Trail Renderer leaves a fading ribbon behind a moving object. Both can face the camera or lie flat, and can stretch or tile a texture.

## Depth

They are drawn after the meshes, back to front. Where a depth buffer is available they are hidden behind solid geometry:

- the software renderer publishes its depth every frame
- the player, and the editor when a GPU backend drew the frame, run a half-resolution depth pass when any particles, lines, or trails exist

Turn that pass off only by having none of those components in the scene.
