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

### Standalone 3D widgets (`WorldSpaceUI` component)
Any single UI widget (button, text, panel, image…) becomes its own in-world 3D
object by adding the **World Space UI (3D)** component — no Canvas needed. It
renders at the object's Transform position (move it with the gizmo), keeps its
anchor and pixel offset, and its child widgets (e.g. a button's label) come along.
Create them from any **Create ▸ UI ▸ 3D UI (in-world)** menu.

- **Pixels / Unit** — design px per world unit (smaller = bigger in-world).
- **Billboard** — always face the camera.
- **Constant size** — keep a fixed on-screen size regardless of camera distance
  (it still moves in 3D, it just never shrinks or grows). **Size Scale** multiplies
  that fixed size (1.0 = the widget's authored design pixels). Great for markers,
  nameplates and waypoint icons that should stay readable at any range.

What is scaled with the projection: positions, text sizes, **panels** (size, corner radius, border, shadow, outline) and
**progress bars** (size, corner radius, segments), so a plate drawn at any distance keeps the proportions it was built with.
Text inside a world widget honours its own **Align** (left, centre, right) and **Vertical centre / bottom** inside its box, the
same as the same text on screen. (Before this, panels and bars were drawn at their design size while their places and the
text were scaled, so at any scale but 1:1 a child could stick out of its parent, and every text sat at the top-left of its box.)
Not checked and not changed: the other widget kinds (images, sliders, toggles, steppers, ratings, tabs, dropdowns, inputs, the
touch controls) draw their own sizes in world space as before.

Nameplates: **GameObject > UI > Prebuilt Screens > Nameplate (in the world)** makes one with **Constant size** on (it keeps its
220 x 62 design pixels at any distance, so the text stays readable far away) hanging above the object's point, centred on
it. Make it a child of a character, move it to the character's head, and it shows that character's name, level and health
(`owner.` keys, see [ui_runtime.md](ui_runtime.md)). The sample scene `examples/ui_text` has two.

Notes / limits:
- Best results with **Billboard** on (or the panel roughly facing the camera);
  widgets are drawn as axis-aligned rects, so a steeply tilted panel in perspective
  is approximate.
- World-space canvases preview in the **Game view**; the editor **Scene view**
  shows world-UI *labels* but not full world canvases (use Play / Game view).
