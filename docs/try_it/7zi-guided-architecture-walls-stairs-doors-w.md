---
id: 7zi
row: | 7zi | Guided architecture: walls, stairs, doors, windows, roofs and a building you can take apart and swap | Modeling > **Guided** > Architecture; File > Examples > Guided modeling > `guided_architecture`; `examples\modeling\guided_architecture.okayscene` | New in this change; engine rules tested on Linux, editor page compiles but was never opened in a real window (unverified) |
---
## 7zi. Guided architecture: walls, stairs, doors, windows, roofs, buildings

**What it is.** More things on the Modeling window's **Guided** page, built on the same rules as the fence (7zg): every
model is an ordinary mesh, parts have names, and rebuilding never overwrites your hand edits without asking.

- **Wall** along a straight run or a path with windows and a door cut in and filled with real windows and doors (corners are
  butt-joined, best at right angles). **Stairs**: straight, L, U and spiral with side boards and railings (landing edges have no
  railing). **Door**: flat, panelled, glazed, planks, double; frame, handle, can stand open. **Window**: five bar layouts, three
  frames, sill, shutters. **Roof**: gable, hip, shed, flat, with an overhang and trim. **Building**: walls, windows, a door and a roof
  composed as parts, 1 to 6 floors.

**Status.** Generation and rebuild rules are tested on Linux (`ctest -R guided_architecture`, about 1400 checks:
every design sound and closed, parts never overlap, every setting at its limits, a fuzz, swapping windows, door and roof of a
building, your edits and your own additions surviving). The page itself was never opened in a real window; nothing ran on Windows.
Pictures in `docs/images/guided_modeling/architecture_*.png` and `building_part_swap_before_after.png` are headless renders of the
generated meshes, not editor screenshots.

**Try it.**

1. **File > Examples > Guided modeling (fences, buildings, stairs...) > guided_architecture** (or File > Open... on
   `examples\modeling\guided_architecture.okayscene`). A grid of 29 models, one row per kind: walls, stairs, doors, windows, roofs,
   buildings (the rows are 14 m apart; use the Hierarchy and F to find them).
2. Click a building in the Hierarchy, open Modeling > Guided. Drag **Floors**, **Roof pitch**, **Width**: the building rebuilds.
3. Under **Parts and materials** change **Roof** from Hip to Gable, **Windows** to Colonial, **Front door** to Planks. Or open **Each
   part**, pick **Windows**, and give one window its own design. Press **Select** to jump into Edit Mesh with just that window selected,
   move it with G, return to Guided: its row says "[edited by you]". Change the roof: it asks nothing (the window is not touched).
   Change that window's design: it asks (Keep my edits / Discard my edits / Convert to regular mesh).
4. Click a wall, set **Door** on, **Windows** to 3. Draw your own wall: **Size > Draw in the Scene view**, then click points on the
   ground in the Scene view (they snap to the **Snap to grid** step). **Finish** or Esc ends. The same works for fences.
5. Click a staircase, switch **Shape** between Straight, L, U and Spiral; change **Steps** and **Total height**.
6. **Collision**: **Exact (mesh)** / **None** on the page (new models also get a "Solid (exact collider)" tick when you press Create).
7. To make your own: Modeling > Guided with nothing selected, Architecture, pick a thing, a starting design card, Create.

**Not done or not verified (said plainly).** No landing railings; wall corners are not mitred; click-to-draw has no preview line
in the view (the model itself shows the result after each click); per-face materials: colours are drawn today and the material slot
numbers are stored, and `claude/modeling-more` (not merged yet) adds per-face slots, which will pick the stored numbers up with no
change here; Vehicles, Characters, Environment and Props are not built.
