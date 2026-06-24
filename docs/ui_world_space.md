# World-Space UI (3D canvases)

There are two ways to put UI in the 3D world:

## 1. World UI label (`WorldUI` component)
A single text label (+ optional background panel and a health/progress bar) that
floats over a 3D point — nameplates, damage numbers, quest markers, interaction
prompts. Add a **World Label (3D)** object, set its text, and it projects over its
object through the main camera (scaling with distance). It also previews in the
editor **Scene view** so you can place it while editing.

## 2. World-Space Canvas (full UI in 3D)
For a real in-world panel — a shop terminal, a sign, a floating menu — put a
**Canvas** in **World Space** and build it with the *regular* UI widgets. Every
widget type works: buttons, panels, images, sliders, toggles, steppers, progress
bars, dropdowns, text — exactly the same components you use for a HUD.

Setup:
1. Add a **Canvas**, tick **World Space** (Inspector → Canvas → World Space (3D)).
2. Position/rotate the Canvas object where you want the panel in the world.
3. Add UI widgets as children and lay them out against the canvas's
   **Design Res** (its own pixel space, default 1280×720).
4. **Pixels / Unit** sets the in-world size (larger = smaller panel). **Billboard**
   keeps it facing the camera; turn it off to use the object's orientation.

It renders (and is clickable) in the **Game view** and the built game, projected
through the scene's main camera. A main Camera must exist.

### How it works
A world-space Canvas is projected by the shared UI layout helpers: the renderer
hands them a camera projector for the frame, widgets are laid out in design pixels,
and the canvas plane is projected to the screen (billboarded by default). Because
this rides the same `GetUIScreenRect` path every widget already uses, all widget
types come along automatically and screen-space UI is unaffected.

Notes / limits:
- Best results with **Billboard** on (or the panel roughly facing the camera);
  widgets are drawn as axis-aligned rects, so a steeply tilted panel in perspective
  is approximate.
- World-space canvases preview in the **Game view**; the editor **Scene view**
  shows world-UI *labels* but not full world canvases (use Play / Game view).
