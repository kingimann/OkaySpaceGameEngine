---
id: water1
row: | water1 | Water that is never flat: presets, sea states, foam, splashes | `examples\water\lake.okayscene`, `ocean`, `river`, `shore` (File > Open, Play), **GameObject > 3D Object > Water** (pick a kind), Water Inspector > **Look** | New in this change; software-renderer pictures and headless tests only, Windows, GPU and the editor window not run |
---
## water1. Water that is never flat: presets, sea states, foam, splashes

**What it is.** The water now has real wave shapes (sharp crests), small ripples that never stop, turquoise shallows and deep blue
depths, white foam on the waves and along the shore, a glow in the crests, rings and spray when something falls in, and a blue-green
murk when the camera goes under. There are eight ready kinds of water and four sea states. Details: `docs/rendering/terrain_water.md`.

**Steps (the example scenes).**
1. Open the editor (`Tools\OkayEngine.exe`). **File > Open...** and pick `<install folder>\examples\water\lake.okayscene`. Press **Play**.
2. Look at the lake: the surface is never one flat colour. Small ripples drift in two directions, the water is turquoise near the shore and
   dark blue in the middle, the sky is mirrored in it. A dock, a rock, a red rowing target and three crates float or stand in it.
3. Press **Stop**. In the Hierarchy select **Crate 1**, drag it up in the Scene view, press **Play**: it falls in, rings spread across the
   water and a little spray jumps up.
4. Open `ocean.okayscene` and press Play: big rolling waves with white caps. Select **Water**. In the Inspector, **Look > Sea State**:
   Calm, Light, Rough, Storm. The waves, their sharpness and the foam change at once. The crates ride the same waves.
5. Open `river.okayscene`: ripples and foam flow to the right and the crates drift downstream. Open `shore.okayscene`: a white foam line
   surges along the waterline and round the rocks; the water goes from clear turquoise to deep blue.

**Steps (make your own).**
1. **GameObject > 3D Object > Water** and pick **Calm Lake**, **Pond**, **Ocean**, **Stormy Sea**, **River**, **Swamp**, **Tropical** or **Ice-cold**.
2. Select it and open **Look** in the Inspector: **Preset** switches kind, **Realistic look** turns the whole new look on or off (off = the old
   plain blue plane), **Clarity**, **Foam**, **Shore foam**, **Ripples**, **Rings of detail**, **Splashes and ripples**, **Underwater look**, **Bubbles**.
3. Put a Terrain under it: the colour and shore foam follow the real depth. Move the Scene camera under the surface (or Play with a camera
   below it): blue-green fog, and bubbles with **Bubbles** on.

**How you know it worked.** With **Realistic look** off the water looks like it did before this change (a scene saved before it loads with
the look off). With it on, the Ocean preset shows white caps and a sea that has no edge near you.

**Status.** Pictures made with the software renderer on Linux and a headless test (`water_visuals`). **Unverified:** the Windows release, the
editor window (menu, Inspector fields: syntax-checked only), the GL and D3D11 renderers (they draw the same vertex colours and normal map but
nothing was run), per-vertex clear water over the bottom (see the Not done list in `docs/rendering/terrain_water.md`).
