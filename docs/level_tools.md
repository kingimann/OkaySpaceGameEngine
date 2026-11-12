# Level tools: foliage, terrain layers, decals, cables, measuring and orthographic views

Small tools for dressing a 3D level. Everything here is saved in the scene and has an undo step.
Feature map: 29.1.6, 29.2.4 to 29.2.7, 29.4, 15.3.12, 44.3.3.

## Foliage brush

A **Foliage** layer is one kind of plant (a built-in prop, a mesh file or a prefab) and the list of copies
you painted. Create one with GameObject > Foliage Layer (it goes under the first terrain), with
Add Component > Rendering > Foliage, or with Terrain Inspector > Scatter props > Add Foliage Layer.

With the layer selected, tick **Paint in the 3D view** and drag in the Scene view:

| Tool | What it does |
|---|---|
| Paint | Plants copies inside the brush circle. Hold Shift to erase instead. |
| Erase | Removes every copy under the brush. |
| Rescale | Gives the copies under the brush new random sizes from the Scale range. |

The ring shows the brush on the surface. Esc leaves the brush. Settings (saved with the layer): Radius,
Density (copies per square metre), Scale range, Max Slope, Height Band (world heights), Random Turn,
Follow Ground (lean with the slope). Copies are also kept apart (`spacing / sqrt(density)`), so dragging over the
same spot again cannot pile them up. A terrain is hit on its real height field; any other mesh is hit by its
triangles (no collider needed). One stroke is one undo step.

**What it costs.** Copies are stored as data (position, turn, scale, lean), not as objects. They are drawn
merged: one mesh per 16 m square of ground (Chunk Size), so a forest is a handful of draw calls and the
renderers skip squares that are out of view; only the squares you touch are rebuilt while painting. This is merged
geometry, not hardware instancing: the memory is the copies times the shape's triangles. **Max Triangles**
(default 300000) stops painting when the layer is full; the Inspector shows the copies, chunks and triangles.
Measured here (the Scene view drawn by the CPU renderer, no graphics card; a bare 8000-triangle terrain
runs at 13 frames a second): a layer of 4267 pines (600000 triangles) in view ran at 2 frames a second, and the same
forest looking the other way ran at 22, because squares outside the view cost nothing. The cost is the triangles you
can see, so keep the budget modest for the view you work in; "Cast Shadows" made no measurable difference there.
The chunks are children made again on load and never saved; the scene file keeps the list (`foliage` record,
about 40 bytes a copy).

**Limits.** One kind of plant per layer (use several layers for a mix). Copies have no collider and do not sway.
A prefab is flattened into one shape: its parts share the first part's texture, and each part's colour is kept.
Scene checks (Problems panel): `foliage.shape` (the plant cannot be built), `foliage.budget`, `foliage.empty`,
and missing mesh / prefab files.

The Terrain Inspector's **Scatter props** now plants into a Foliage layer under the terrain (one object per prop)
instead of making one object per copy; **Clear Scattered** removes those layers and the per-copy objects older
scenes have.

## Terrain texture layers

Terrain Inspector > Texture layers > **Add Layers** turns the single Ground Texture into up to four layers. Each
has a picture (optional), a tint and a tiling (repeats across the terrain). The first layer covers everything; the
others start empty. Select a layer (the round button), tick **Paint layers in the 3D view** and drag on the terrain
(Shift takes the layer away). Radius, Strength and Hardness are the brush. Fill With Selected, Remove and
Remove All Layers are there too. The weights are saved with the terrain (`terrainlayers`, run-length text, so
an untouched terrain costs a few bytes).

**How it is drawn.** The layers are blended into one ground picture (Detail: 256 to 2048 pixels a side) that the
terrain uses as its texture, so every renderer shows it through the shared texture cache (checked in the software
renderer, the Scene view and the player; the OpenGL and Direct3D renderers read the same picture but were not run
for this change). While layers exist they replace the Ground Texture and the auto-colours. Detail over a very
large terrain is limited by the picture size.

The sculpt brush now lands where the cursor meets the terrain surface (`Terrain::Raycast`), not on a flat plane at
the object's height.

## Decal projector

GameObject > Decal Projector. Aim the blue (forward) axis at a wall or floor, set a **Texture** and a **Size**
(width, height, reach), press **Project** (also done when you change a setting or move the decal). The decal is a
thin mesh cut from the triangles inside the box, so it follows bumps and corners and works in every renderer. Surfaces
turned more than **Max Angle** away get none. It is not updated when the surfaces move: press Project again.
Foliage and other decals are never projected onto.

## Cable

GameObject > Cable. Pick the **From** and **To** objects (or leave From empty to start at the cable's object and
give a To Offset). **Sag** is how far the middle hangs. It keeps a Line Renderer along a parabola and follows its
ends in the editor and in Play. It is a visual: it has no physics (Joint3D is the physical one).

## Measuring tool and orthographic views

Scene view, View options (the ... button): **Measure tool** (or M). Click two points on the surfaces; the label shows
the distance, the distance across the ground and the height difference. A third click starts a new measurement, Esc
leaves the tool.

Same menu: **Perspective**, **Orthographic**, and **Orthographic view** > Top, Bottom, Front, Back, Right, Left
(numpad 7, 1, 3; Ctrl for the opposite; numpad 5 switches projection). The projection is a true orthographic one.
Zoom with the wheel; orbiting keeps it orthographic until you choose Perspective.

## Tests

`foliage` (paint, spacing, limits, budget, chunks, save and load, surfaces, ray casts, terrain layers, cable,
decal, validator rule) and `save_coverage`.
