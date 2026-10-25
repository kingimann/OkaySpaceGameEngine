# Lighting and shadows

Every scene is lit by its **Light** objects (add one with *Add Component →
Lighting → Light*), plus an ambient term. A scene with no Light gets a default
sun so it never renders black.

## Light types

| Type | What it is | Shadows |
|------|------------|---------|
| **Directional** | The sun/moon: parallel light along the object's +Z. | Cascaded sun shadows (only the **first** enabled Directional light casts them). |
| **Point** | A bulb: radiates in all directions out to **Range**. | Six shadow-map faces (a cube) — walls block it. |
| **Spot** | A cone of **Spot Angle** degrees with a **Softness** edge. | One perspective shadow map. |
| **Area** | A broad soft panel (windows, screens, studio fill). | Like a point light (six faces). |

Common settings: **Color** or **Use Temperature** (Kelvin, with Candle → Shade
presets), **Intensity**, **Range** and **Falloff** (Linear or physical Inverse
Square) for local lights, and **Ambient** + **Ambient Tint** (the first light's
ambient sets the scene's unlit floor). Unticking a Light component's enabled box
turns the light off (saved with the scene).

### Shadows (per light)

- **Cast Shadows** — on by default for every light.
- **Strength** — 0 = invisible, 1 = fully dark (down to the ambient light).
  Lower it for a bright overcast feel or a weak fill light.
- **Bias** — raise it if lit surfaces show dark speckles or stripes ("shadow
  acne"); lower it if shadows look detached from the objects casting them.

Spot, point and area lights share a budget of **16 shadow tiles** per frame: a
spot light takes 1, a point/area light takes 6. Lights closest to the camera get
tiles first; lights whose reach is entirely off screen are skipped. A light that
doesn't get tiles still lights the scene — just without shadows. Turn **Cast
Shadows** off on minor fill lights to leave the budget for the important ones.

### Flicker

**Amount** (0–1) randomly dims the light by up to that fraction while the game
runs; **Speed** sets how many changes per second. Presets: *Candle*, *Torch*,
*Faulty bulb*. Each light flickers independently. In edit mode the light stays at
full intensity so you can still light the scene; flicker plays in Play mode and in
the built game.

## Mesh shadow settings

Each **Mesh Renderer** has:

- **Cast Shadows** — this mesh blocks light. Turn it off for skyboxes, decals or
  floors that never occlude anything (saves shadow-map time too).
- **Receive Shadows** — shadows from other meshes darken this one.
- **Ground Shadow** — a cheap flat "blob" projected onto a ground plane. It is a
  **fallback** only drawn while real shadows are off (Rendering → Shadows
  unticked, or the sun's Cast Shadows off); with shadow maps on it would
  double-darken shadows and show through anything between the object and the
  plane.

Transparent materials (opacity < 1) never cast shadows.

## Scene ambient

In **Environment / Render Settings**, **Ambient Mode** picks how the ambient
(indirect) light varies with surface direction:

- **Flat** — the same everywhere.
- **Hemisphere** (default) — brighter on top, darker underneath.
- **Sky colors** — tops take the sky gradient's colours and undersides the Sky
  Bottom colour; **Sky Tint** sets how strongly (0–1.5).

**Ambient** there is the level used when the scene has no Light object.

## Shadow quality

**Rendering (Lighting & Post FX)** in the editor (and **Build Settings →
Graphics** for the exported game):

- **Shadow softness** — the soft-edge radius in shadow-map texels (0 = hard).
- **Shadow distance** — how far sun shadows reach in front of the camera. 0 =
  the legacy single map fit to the whole scene.
- **Shadow cascades** (1–4) — the sun's shadow is split into rings of
  increasing size: crisp up close, still present far away. Neighbouring
  cascades blend so there is no visible seam, and the last one fades out at the
  shadow distance.
- **Shadow resolution** — texels per cascade (512–4096). Spot/point light tiles
  are a quarter of this (256–1024).

## How it works

The GPU renderers (OpenGL in the editor and Linux player, Direct3D 11 in the
Windows player) share one plan (`engine/include/okay/Render/ShadowSetup.hpp`):

- The sun's cascades are orthographic depth maps laid side by side in one
  atlas, each fit to a sphere in front of the camera (radii grow geometrically
  out to the shadow distance, capped at the scene's size) and snapped to the
  texel grid in light space, so shadows don't shimmer as the camera moves.
- Spot lights render one perspective depth map; point and area lights render
  six cube faces (slightly wider than 90° so the soft filter never reads past a
  face edge). All local maps live in one 4×4 tile atlas.
- The shader picks the tightest cascade (or the cube face by the dominant axis
  from the light), pushes the receiver a few texels along its normal and toward
  the light (world-space bias, the same for every map type), then filters with
  12 Poisson taps — rotated per pixel — each a hardware bilinear depth compare.

Highlights (specular) take each light's colour and intensity.

### Limitations

- Only the first Directional light casts shadows; other directional lights
  light the scene unshadowed.
- The software renderer (used when no GPU is available) has sun shadows only —
  no spot/point light shadows. The opt-in Direct3D 12 backend has a single sun
  shadow map (no cascades, no spot/point shadows).
- At most 16 lights are shaded per frame on the GPU (the first 16 Light objects).
