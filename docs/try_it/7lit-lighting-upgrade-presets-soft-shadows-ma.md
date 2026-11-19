---
id: 7lit
row: | 7lit | Lighting upgrade: presets, soft shadows, masks, panels, real-world units | Editor: **View > Lighting** (Lighting Presets), **File > Examples > Lighting presets**, any Light's Inspector | New in this change (branch `claude/lighting-2`); only the software renderer was run, no GPU, no Windows |
---
## 7lit. Lighting upgrade: presets, soft shadows, masks, panels, real-world units

**What it is.** The lights and shadows got better. What changed and why: `docs/lighting_upgrade.md` (it has the Existing / Gap
table). Code: `ShadowMath.hpp`, `Lighting.hpp`, `LightingPresets.hpp`, `SoftwareRenderer.hpp`.

**Which renderer shows what.** Press **F3** in the editor: the overlay says which renderer is drawing. The Light Inspector prints a
grey note under Soft Edges, Mask and Panel Width/Height when the renderer ignores them.
* Software renderer: everything below.
* OpenGL and Direct3D 11: presets, real-world units, Auto brightness, Glow Light Range, the smooth spot cone and the stable sun
  shadows (no shimmer). Soft Edges, masks and rectangle panels are **not drawn** there yet (an Area light stays a soft point light).

**Steps: the five looks.**
1. Open the editor. **File > Examples > Lighting presets (sun, sunset, night, indoor, fog)** and pick `sunny_day`. (Windows release:
   `examples\lighting\sunny_day.okayscene` next to the programs.) Press **Play**.
2. Open **View > Lighting**. At the top is **Lighting Presets**: click **Sunset**, then **Night with lamps**, **Indoor**,
   **Spooky fog**, **Sunny day**. The sky, fog, ambient light and lights change at once. **Ctrl+Z** undoes a click.
3. Look in the Hierarchy: lights a preset added are named `Lighting: ...`. Select one and change it in the Inspector like any light.

**Steps: the Light Inspector.** Select a Light.
* **Units**: switch Simple to *Real world*. The sun is in lux (try the **Noon sun**, **Overcast**, **Moon** buttons), lamps in lumens
  (**Candle**, **Bulb**, **Street lamp**). In View > Lighting, **Auto brightness** keeps the scene readable (untick it to see the
  fixed level).
* **Panel Width / Height** (Area light only): set 2 and 1.4. The light becomes a rectangle shining along its blue arrow. 0 and 0 is
  the old soft glow.
* **Mask** (Directional or Spot): pick *window*. The sun now shines through window panes; *Metres per repeat* sets their size. A
  Spot light shows the mask inside its cone. *Picture file...* takes your own picture (white lets light through).
* **Soft Edges** (under Shadows): 0 = the same soft edge everywhere, 1 = a shadow is sharp where the object touches the ground and
  gets softer the farther it falls.
* On a Mesh Renderer, **Glow** section: give the object an Emissive colour, set **Glow Light Range** to 5: it now lights its
  surroundings.
* View > Lighting: **Classic lighting look** (software renderer only) goes back to the old shadows and light roll-off, to compare.

**What you should see.** Five clearly different moods; soft shadows that are sharp at the foot of an object; a window pattern on the
floor; a bright panel over a table. Sun shadows stay still when you fly the camera slowly.

**Status.** Verified here: the maths and the software renderer, headless (`test_lighting_upgrade`, 135 checks; `light_shadows`,
`renderer_features`; before and after pictures were looked at, not committed). **Not verified:** OpenGL, Direct3D 11 and Direct3D 12 (the
cascade fix, the slope bias and the smooth cone were edited in their code but never run on a GPU); Windows; the editor panels (the
new Inspector and Lighting-window code was written but, see `docs/lighting_upgrade.md`, only compile-checked as far as stated there).
