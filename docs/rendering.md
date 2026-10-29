# Rendering: post-processing, sky, fog and weather

The look of a scene beyond its lights lives in the **Lighting** panel (*View →
Panels → Lighting*) in four sections: **Post-processing**, **Sky**, **Fog** and
**Weather**. Everything there is saved with the scene and previews live in the
Scene and Game views. With every setting at its default the frame is exactly
what the engine drew before these existed: no post pass runs, the sky is the
authored gradient, there is no fog and no weather.

Scripts change the same values at runtime (see [Scripting](#scripting)).

## Look presets

The **Look Preset** combo at the top of *Post-processing* fills all four
sections at once: **Default**, **Cinematic**, **Vivid**, **Noir**, **Sunset**,
**Night**, **Foggy morning** and **Storm**. Sunset, Night, Foggy morning and
Storm also aim the scene's first directional light at the sun height the look
needs (low, below the horizon, ...). **Reset** puts the four sections back to
their defaults and keeps the sky gradient colours. Both go through Undo.

## Post-processing

Applied by the GPU renderers (OpenGL in the editor and the Linux / web player,
Direct3D 11 in the Windows player). The software renderer keeps its own output.

When any effect is on, the scene renders into a float (HDR) target, so
highlights above white survive for bloom and the tonemap, and the chain runs:
SSAO → bloom → exposure / tonemap / grading / vignette → depth of field → FXAA.

| Setting | What it does |
|---------|--------------|
| **Filmic Rendering** | The linear-lighting pipeline (on by default). Off = the legacy flat response, and no tonemap curve. |
| **Exposure** | Scene brightness multiplier, 1 = as lit. |
| **Tonemap** | How highlights roll off: **ACES** (the default, punchy), **Filmic** (a softer shoulder), **None** (clip). |
| **Bloom** | Glow around bright pixels: **Threshold** (brightness that starts to glow; with Filmic Rendering 1 = white), **Intensity**, **Radius**. Blurred at half and quarter resolution, so it stays cheap at 1080p. |
| **Colour Grading** | **Temperature** (cool ↔ warm) and **Tint** white balance, **Saturation**, **Contrast**, and per-channel **Lift** (blacks), **Gamma** (mid-tones), **Gain** (whites). |
| **Vignette** | Darkens the edges and corners, with a **Smoothness** for how far in it reaches. 0 = off. The post chain draws it when it runs; otherwise it is an overlay that looks the same on every renderer. |
| **Anti-aliasing (FXAA)** | A luma-driven edge filter on the finished frame. |
| **Ambient Occlusion (SSAO)** | Darkens creases and contact points: **Radius** (world units) and **Intensity**. OpenGL only. |
| **Depth of Field** | Blurs outside the **Focus Range** either side of the **Focus Distance**. OpenGL only. |

A lightning strike (see Weather) spikes the exposure for a few frames, so a
storm blinks even with every other effect off.

The *Rendering (Lighting & Post FX)* section below these is the editor's own
process-wide switches (shadow quality, the software renderer's effects); it is
not saved with the scene.

## Sky

**Sky Mode** chooses between:

- **Colour** — the gradient in *Environment (Sky & Light)*: Sky Top / Horizon /
  Sky Bottom, the horizon position, the screen-space sun disc and star field.
  This is what every scene had before.
- **Procedural** — a sky the GPU renderers draw behind everything, following the
  camera's rotation and field of view. Its colours come from the **sun's
  height**: the scene's directional light is the sun (a **Day/Night Cycle**
  component rotates it, so the sky turns from day blue through dusk orange to a
  dark night with stars as the clock runs). It has a **Sun Disc** (angular
  radius in degrees) with a **Sun Halo**, **Horizon Haze**, stars once the sun
  has set (**Star Density** / **Star Brightness**) and a cloud layer: **Cloud
  Cover** (clear → overcast), **Cloud Density**, **Cloud Speed** and **Cloud
  Scale**. Clouds are lit by the sun and drift with the scene's wind.

Reflective materials and the *Sky colors* ambient mode take the procedural
sky's colours too. The software renderer shows the sun's colours as a plain
gradient (no disc, stars or clouds). In the Scene view the *Debug View → Sky
gradient* toggle hides the procedural sky as well.

## Fog

**Enable Fog**, then pick a **Fog Mode**:

- **Linear** — fades from **Fog Start** to **Fog End** (world units from the camera).
- **Exponential** — thickens with distance by **Density**.
- **Height** — a layer that thins above **Height** by **Falloff**: a valley
  mist, a low morning fog. Looking down into it fills with fog; looking up out
  of it clears.

**Match Sky Colour** takes the fog colour from the sky's horizon (the gradient's
in Colour mode, the sun's in Procedural mode), so distant geometry dissolves
into the sky instead of a flat band; otherwise set **Fog Color**. Fog is applied
in the scene shaders on every renderer, including the software one.

## Weather

**Weather Preset**: **None**, **Rain**, **Snow** or **Storm**, at an
**Intensity** (0–1) that scales how much falls. The precipitation is built from
the particle system (the *Rain* and *Snow* looks) on a transient
"Weather (runtime)" object that follows the active camera, so the player is
always under it; it is never saved. A preset change cross-fades over **Fade**
seconds, and "None" takes the rig down once the last drops have landed. In edit
mode the preview follows the Scene camera.

**Storm** is heavy rain plus gusts on top of the wind and lightning: each strike
is a brief exposure flash, and **Thunder Sound** (a clip from the Project
panel) plays a moment later.

**Wind** is a global vector (world units per second). Every particle system
drifts with it (a system's **Wind Influence** scales that; 1 = the wind's
speed, 0 = ignore it) and a **Water** surface's swell quickens and runs
downwind. A scene with no wind leaves both exactly as authored.

## Scripting

OkayScript builtins (also in [scripting.md](scripting.md)):

```cs
post_set("bloom", 1);              // any setting PostProcess.Set knows; 0 / 1 for switches
post_set("tonemap", "filmic");     // or 0 none, 1 ACES, 2 filmic
fog_set(true, "#c8d8e6", 20, 90);  // on, optional "#rrggbb" colour, start, end
fog_set(true, 10, 60, "height", 0.05);   // ... then a mode and its density
weather_set("storm", 0.8);         // none / rain / snow / storm, intensity
float hour = time_of_day();        // the scene's Day/Night Cycle; time_of_day(18.5) sets it
```

C#-style statics: `PostProcess.Set(name, value)` and `PostProcess.exposure`,
`Fog.Set(enabled, start, end)`, `Fog.SetColor(r, g, b)`, `Fog.SetMode(mode,
density)`, `Fog.enabled`, `Weather.Set(preset, intensity)`,
`Weather.SetWind(x, y, z)`, `Weather.preset` (see
[engine_api.md](engine_api.md)).

Visual-script nodes: **Set Weather** (preset in the node, Intensity pin),
**Set Fog** (On, Start, End pins) and **Set Post Effect** (effect name in the
node, Value pin), plus the engine-API nodes made from the statics above.

## Scene file

The settings are extra header records of the scene file, written only when
they differ from the defaults: `fogmode`, `skyproc`, `post` and `weather`
lines after `rendersettings`, and a `particleswind` line on a particle system
that doesn't take the full wind. A scene that never touched them is written
exactly as before and loads with everything off.
