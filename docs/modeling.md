# Modeling

The **Modeling** window is OkaySpace's built-in mesh editor — enough of
Blender to block out levels and props without leaving the engine. Everything
operates on the selected object's Mesh Renderer, every destructive op is
undoable (Ctrl+Z), and edited geometry is saved with the scene (including
its texture coordinates).

## Add library

The **Add** section at the top is an organized object library (Blender's Add
menu + F3 search). Type in the filter box to narrow it, or expand a category:

- **Primitives** — 23 shapes: Cube, Sphere, Cylinder, Cone, Pyramid, Wedge,
  Quad, Plane, Tube, Torus, Torus Knot, Capsule, Icosphere, Grid,
  Hemisphere, Stairs, Gear, Prism, Octahedron, Disc, Tetrahedron,
  Bipyramid, Rounded Box.
- **Nature** — ready-made, pre-colored props: Tree, Pine, Palm Tree, Dead
  Tree, Bush, Rock, Mushroom, Cactus, Crystal. Built from the primitives +
  path-sweeping + color mottling, so they read as real materials, not flat
  plastic, and they scatter over terrain (Terrain tab → Scatter). All nine
  nature props support **procedural variants**: the Shape tab shows a
  **Variant** stepper + **Randomize** (0 = the base model), and the terrain
  Scatter tool gives every instance its own variant, so a forest is a crowd
  of distinct shapes rather than identical clones. A variant is stored as a
  single number per object and regenerates the exact shape on load.
- **Props** — Barrel, Crate, Fence, Well, Street Lamp, Campfire, Lantern,
  Chest, Signpost, Cart, Tent, Bench. (Campfire and Lantern have emissive
  cores — turn on Bloom for a glow.)
- **Structures** — House, Tower, Windmill, Bridge, Rock Arch.

Everything in Nature / Props / Structures is a normal editable mesh: tick
**Edit Mesh** and reshape it, or **Export OBJ** to reuse it. Or select any
existing mesh object — imported models included. The editing toolset is
grouped into tabs:

## Shape

Swap the primitive, set the base color, toggle wireframe, and choose the
collision mode: **Box (bounds)** is a fast fitted box, **Mesh (exact)**
collides against the live triangles — the right choice after you extrude a
floor or carve a doorway. Colliders auto-refit whenever the mesh changes.

## Edit — whole-mesh operations

- **Subdivide** (whole mesh, or just the faces selected in Edit Mesh),
  **Subdiv + Smooth** (rounds without shrinking), **Smooth** (subdivide +
  spherify), **Relax** (Laplacian smoothing — evens out lumpy sculpts and
  imports without adding triangles; drag the slider for strength).
- **Loop Cut X/Y/Z** inserts N evenly-spaced edge loops across the mesh on
  an axis (Blender's Ctrl+R) — add editable divisions to a wall/box/cylinder
  before pushing or pulling them.
- **Fill Holes** caps every open boundary (e.g. after Delete Faces) so the
  mesh is watertight again. **Bridge** connects exactly two open boundaries
  with a wall — delete two facing faces on a Combined mesh and bridge them
  into a tunnel. **Jitter** nudges every vertex randomly for an instant
  hand-made / rocky look. **Shear X/Z** slants the mesh with height
  (leaning towers, italic props).
- **Symmetrize X/Y/Z** makes the mesh perfectly symmetric: the positive
  half (about the pivot) is kept and mirrored over the other side.
  **Snap Grid** quantizes every vertex to a step — crisp low-poly look,
  modular kit alignment.
- **Shade Smooth / Flat** switches between blended lighting (organic
  shapes) and faceted lighting (low-poly look); the choice is saved with
  the scene.
- **Auto Smooth** (with an angle) is Blender's angle-based shading: edges
  meeting shallower than the angle shade smooth, sharper ones stay crisp —
  a cylinder gets a smooth barrel with hard cap rims in one click. The
  angle is saved with the scene and restored on load.
- **Weld** duplicates, **Recenter** the pivot, **Ground** the pivot to the
  base, **Fit 1u** to scale into a unit cube. **Export OBJ** writes the mesh
  (with UVs) to `Assets/<name>.obj`.
- Symmetry / deform: **Mirror X/Y/Z**, **Spherify**, **Twist Y**,
  **Solidify** (give a surface thickness), **Taper Y**, **Bend X**.
  **Mirror** never makes overlapping geometry:
  - **A mesh crossing the mirror plane** is cut there first, keeping the + side, so a centred cube stays one closed cube.
  - **A closed half model** loses the faces lying on the plane before the copy is added, so no faces are left back to back inside the model.

### UV Unwrap

Quick projections that give every face corner its own texture coordinate, so
materials tile properly on modeled geometry:

- **Box** — tri-planar: each face projects along its dominant axis, turned so
  the texture reads the right way round on all six sides. The go-to for
  buildings and hand-edited shapes.
- **Planar Y** — straight down: floors, terrain, tabletops.
- **Cylinder** — wraps around Y: columns, cans, lathe results.
- **Sphere** — from the centre out: planets, rocks, heads.

**tile** sets how many texture repeats span the mesh. Projected UVs survive
scene save/load and OBJ export. For a real unwrap with seams, see
[UVs and seams](#uvs-and-seams) under Edit Mesh.

UVs follow the engine's convention everywhere: **v = 0 is the top of the
image** (as in glTF and on the GPU renderers), and a face seen from outside
shows the texture the right way round, not mirrored.

## Modifiers

Blender-style one-shot modifiers (they change the mesh right away; for
modifiers you can keep tweaking, see [Modifier stack](#modifier-stack-non-destructive)):
**Array X** (repeat with offset),
**Remesh** (watertight voxel rebuild), **Decimate** (cut triangle count),
**Boolean** Union / Subtract / Intersect against any other mesh object in
the scene, **Convex Hull**, **Bisect** (slice through the centre and keep a
capped half), **Split in two objects** (keep BOTH capped halves — break a
rock apart, cut doors from a wall), **Shrink/Fatten**, **Wireframe** (turn
edges into beams), **Displace** (noise roughen), **Cast Cyl Y**, and
**Stretch X/Y/Z**.

## Modifier stack (non-destructive)

Add **Modifiers (non-destructive)** to a mesh object (Inspector → Add
Component → Rendering, or **Add Modifier** in its Modifiers section). The
object's mesh stays the editable base; the stack runs on top of it, in order,
and its result is what is drawn, casts shadows, collides (Mesh Collider and
the collider auto-fit) and is exported to OBJ. Change a setting, toggle a
modifier, edit the base in Edit Mesh or move a Boolean cutter, and the result
is rebuilt. Nothing is lost until you press **Apply**.

| Modifier | What it does | Settings |
|---|---|---|
| **Mirror** | Reflects across the object's own X/Y/Z planes. A mesh crossing a plane is cut there first (the + side is kept), so the halves never overlap. | axes, Merge (weld the seam) |
| **Array** | Copies side by side. | count, offset, Relative (offset times the mesh size; 1.1 = 10% gap) |
| **Subdivision Surface** | Catmull-Clark on the real polygons: quads stay quads, n-gons become n quads, open borders keep their outline. | levels (0–4), Smooth shading |
| **Solidify** | Gives a surface a thickness. | thickness |
| **Bevel** | Rounds every edge whose faces meet sharper than the angle. | width, segments, angle |
| **Boolean** | Union / Subtract / Intersect with another mesh object (by name), using its *modified* mesh. Moving or editing the cutter updates the result. **Show cutter** hides or shows the cutter object. | object, operation, voxel size |
| **Decimate** | Merges vertices within cells of the given size. | cell size |
| **Displace** | Roughens along the normals with noise. | strength, frequency, seed |

Each modifier has an on/off checkbox and a **...** menu (or right-click its
name) with **Move Up** / **Move Down**, **Apply** (bake just that one into the
base — it runs on the base mesh, as in Blender, so apply from the top) and
**Remove**. **Show in edit mode** off hides it while you edit the base in Edit
Mesh, where the base's vertices are shown as a cage over the result. Every
change is one undo step. **Apply All** bakes the
whole stack. A modifier that can't run (no cutter, too many faces for the
subdivision level, nothing to bevel) shows the reason in red and is skipped;
the rest of the stack still runs.

Subdivision refuses a result above 1.5 million faces. Booleans are voxel
based, like the one-shot Boolean, so their cost depends on the voxel size.

The stack is saved with the scene (`modstack` / `mod` lines) and rebuilt on
load; in a running game it refreshes in Update, so a script that moves a
Boolean cutter sees the cut follow it.

## Generate

Profile-driven surfaces. Edit the 2D point list — each point is
`(radius, height)`, bottom to top — or start from the **Vase**, **Goblet**,
or **Column** preset, then:

- **Lathe** — revolve the profile around Y (segments sets roundness).
- **Screw** — sweep it in a helix: set turns, pitch (rise per turn), and
  segments for springs, screw threads, and spiral ramps.
- **Extrude Outline** — treat the points as a flat outline and give it
  depth along Z for logos, arrows, and flat props.
- **Pipe** — sweep a round cross-section along a path (Straight, Arc,
  Helix, S-Curve, Zigzag presets) with twist-free frames at bends: pipes,
  rails, cables, springs. Set the radius and ring segments.

## Edit Mesh — vertex/face editing in the viewport

Press **Tab** with the mouse over the 3D view, or tick **Edit Mesh**, to enter edit mode.
The mesh is auto-welded, so shared corners move together.

- **Mode header:** a header at the top of the 3D view shows **Object** / **Edit**. While editing it also shows **Vertex** / **Edge** / **Face**. Click them, or press **1** / **2** / **3**.
- **Selecting:** click in the 3D view to select. Shift-click adds to the selection.
  - **Alt+Click** an edge to grab its whole edge loop.
  - **Ctrl+Alt+Click** an edge to grab its ring: the edges across the quads beside it.
- **Real polygons:** the wireframe shows the mesh's quads and n-gons, not the triangles they're drawn with. Clicking a face in Face mode selects the whole polygon.
- **Moving:** drag the axis handles to move, and hold **V** to snap to nearby vertices.

### Move, rotate and scale: G / R / S

With something selected, press **G** (move), **R** (rotate) or **S** (scale) with the mouse over the 3D view, then move the mouse.

| Key | What it does |
|---|---|
| **X** / **Y** / **Z** | Lock to that world axis (press again to unlock) |
| **Shift+X** / **Y** / **Z** | Lock to the plane of the other two axes |
| Digits, **.**, **-**, Backspace | Type an exact value: a distance, an angle in degrees, or a scale factor |
| **Ctrl** (or the Snap toggle) | Snap: moves to the grid size, rotations to the rotate-snap angle, scale to 0.1 steps |
| **G** / **R** / **S** again | Switch to that tool, starting over |
| **Enter** or click | Confirm: one undo step |
| **Esc** or right-click | Cancel: everything goes back exactly as it was |

- **What it turns around:** rotate and scale work about the centre of the selection. With no axis lock, rotate turns about the view direction and scale is uniform.
- **Where a typed distance goes:** along the locked axis, or along X when nothing is locked.
- **Other tools still apply:** **Soft Select** and **Symmetry X** work with G / R / S too.
- **Status line:** the line at the bottom of the view shows the tool, the lock, the current value and the keys.
- **Undo:** you stay in edit mode after undo and redo.

### Undo in edit mode

While you're in edit mode, **Ctrl+Z** / **Ctrl+Y** (and Edit > Undo / Redo) step through your mesh edits one operation at a time.
A move, an extrude or a sculpt or paint stroke is each one step.
When you leave edit mode, the whole session becomes a single scene undo step. Undoing that step later puts the mesh back the way it was before you entered.

### Polygon stats

The Edit Mesh page shows what the mesh is made of: its vertices and faces, how many faces are quads, triangles and n-gons, and whether it's **closed**.
If it isn't closed, the line says how many open or non-manifold edges it has.
Faces are counted after merging flat triangle pairs back into quads.

### Tools greyed out

A tool that needs a selection you don't have yet is greyed out; hover it to see what to select. Extrude Faces, for example, needs faces in Face mode.

### Edge loops

These tools work on the mesh's polygons. Each one is a single undo step.

| In the 3D view | What it does |
|---|---|
| **Ctrl+R** | **Loop cut.** Point at an edge: a yellow preview shows the new loop across the ring of quads through it. Scroll (or **+** / **-**) for more cuts. Click to cut. With one cut you then slide it into place: move the mouse and click (Esc keeps it in the middle). |
| **G** then **G** | **Edge slide.** Slide the selected edge loop along the faces beside it. Move the mouse, or type a value from -1 to 1; Ctrl snaps to 0.1 steps. |
| **Ctrl+B** | **Bevel.** Chamfer the selected edges: move the mouse away from them for a wider bevel, or type the width. Scroll for more segments (a rounded bevel). |
| **K** | **Knife.** Click points on edges or vertices, Enter to cut. Each step from one point to the next must stay within one face. Backspace removes the last point; Esc cancels. |

Enter or a click confirms; Esc or a right-click cancels. The **Edge loops** group on the Edit Mesh page has the same tools as buttons, plus:

- **Select Loop / Select Ring:** grow the selected edges into their loops or rings.
- **Loop Cut:** cut through the first selected edge, with a cut count and (for one cut) its position.
- **Bevel:** with a typed width, the number of **segments** and, for more than one, the **profile** (0.5 is round, lower is flatter, 1 pulls out toward the old sharp corner).
- **Dissolve Edges:** remove edges, merging the faces on either side. Vertices left in the middle of a straight side go too.
- **Dissolve Verts:** remove vertices, merging the faces around each one.
- **Connect Verts:** split the face two selected vertices share with a new edge.
- **Bridge Edge Loops:** join two open borders with a band of quads, for example the ends of two tubes or the rims of two holes. Both need the same number of edges.
- **Fill:** close a hole with one face across its rim. Select the rim with Alt+Click.
- **Grid Fill:** close a hole with a grid of quads that blends its four sides. The rim needs an even number of edges; **span** sets how many quads run along the first side (0 = automatic).
- **Merge at Center / Merge at First:** merge the selected vertices (or the corners of the selected edges or faces) into one, at their centre or at the first one you clicked. Faces that collapse are removed.

Limits:

- **Bevel:** it needs edges with a face on both sides, running through vertices that have faces all the way round them. Where three or more bevels meet, the corner is closed with one face, not a rounded grid.
- **Not here yet:** seams, sharp edges and edge split. These need the drawn mesh to split vertices that the editable mesh keeps joined, so they come with the UV milestone.
- **Bridge:** works on open borders only. It doesn't remove faces first.
- **Edge slide:** UVs aren't adjusted, so a textured surface stretches with it.

#### How the polygons are kept

Each mesh can store which triangles make up each of its polygons.

- **What it's used for:** tools, the wireframe and **.okaymesh** files get back the exact quads and n-gons, even skewed ones.
- **Saving:** a scene saves this with edited geometry.
- **Triangle tools:** a tool that works on triangles (Subdivide, Extrude...) may lose that grouping. The polygons are then found again from flat pairs of triangles.

Selection tools: **Grow** adds every vertex touching the selection,
**Shrink** peels its rim off, **Linked** flood-fills everything connected
to the first selected vertex (grab one island of a combined mesh), and
**Invert** flips the selection in any mode.

- **Symmetry X** mirrors every move and sculpt stroke across the local
  X = 0 plane — edit half, get both.
- **Soft Select** is proportional editing: unselected vertices near the
  selection follow with a smooth falloff (set the radius).
- Face tools: **Extrude Faces** (with distance), **Inset Faces**,
  **Subdivide Sel**, **Delete Faces**, **Flip Normals**,
  **Recalc Outside** (Blender's Recalculate Normals Outside: makes every
  face's winding consistent with its neighbours and points closed shells
  outward — the one-click fix for dark/inside-out faces after welds,
  bridges, booleans, mirrored halves or imports),
  **Merge by Distance**, **Duplicate Sel** (copy the selected faces in
  place, copies stay selected — drag them away).
- Per-face detailing (Blender's Individual ops): **Poke** fans each face
  from a raised centre (spikes, pyramids, dimples); **Extrude Indiv** pushes
  each face out along its own normal as a separate stud (rivets, greebles);
  **Inset Indiv** insets each face on its own (panels, frames). Poke +
  Extrude Indiv + Inset Indiv together cover most hard-surface detailing.
- Edge tools: **Collapse Edges** (merge each selected edge's endpoints),
  **Split Edges** (insert a midpoint, cutting both neighbour faces),
  **Extrude Edges** (pull a wall out of each selected edge along its face's
  normal — build walls up from a floor plate's rim; the new outer edges
  stay selected so you can keep going).
- **Snap Sel** quantizes the selected vertices to a grid step.
- **Shrink/Fatten** moves the selected vertices along their own normals —
  inflate (+) or carve (-) a patch without dragging an axis (Blender's
  Alt+S). **To Sphere** rounds the selection toward a sphere around its
  centre (Blender's Shift+Alt+S; 0 = off, 1 = fully round).
- Selection tools: **Bevel Sel** chamfers the selected corners (cut back and
  capped flat — knock the sharp corners off a box), **Relax Sel** smooths
  just the selected vertices, **Flatten X/Y/Z** snaps them to their shared
  average (level a rim, square a wall), and **Separate** splits the selected
  faces off into a new object with the material carried over.

### UVs and seams

In Edit Mesh, the **UVs & Seams** section flattens the surface into texture
space so a texture lands on it without stretching:

- **Mark Seam / Clear Seam** on the selected edges (Edge mode). Seams show in
  red; unwrapping cuts the surface open along them. Put them where a cut won't
  be seen: under arms, along the back, around caps. **Clear All** removes them.
- **Seams from Angle** marks every edge whose faces meet at the angle or
  sharper (66° by default: the hard edges of a box, a cylinder's caps).
- **Unwrap** flattens the selected faces (Face mode), or the whole mesh, with
  *least squares conformal maps*: each island (faces joined by edges that
  aren't seams) is flattened keeping its angles as true as possible, all
  islands get the same texel density, and they're packed into the 0–1 square.
  Only a disc lies flat, so a part that isn't one is cut open first and the cut
  is marked as seams:
  - a closed part like a sphere along the shortest path between its two farthest points;
  - a part with more than one border (a tube's two rims) along a path joining them;
  - a part with a handle (a torus) along the loops around it.
- **Smart Unwrap** = Seams from Angle, then Unwrap: one island per flat-ish
  region, the quickest good result for hard-surface props.
- **Pack Islands** arranges the existing islands into the 0–1 square without
  reshaping them; **margin** sets the gap between them.

The console reports what happened: islands, parts cut open, seams added, and
the *angle distortion* (0° = angles kept exactly). A 24k-vertex sphere unwraps
in about 3 seconds.

UVs are kept per face corner while you edit, so moving vertices, loop cuts,
bevels and the other polygon tools keep the texture where it was, and leaving
edit mode, saving and reopening keeps seams and UVs exactly. (Tools that
rebuild triangles outside the polygon core, such as the classic extrude, fall
back to one UV per vertex where they touch.)

### UV Editor

**View > Editors & Tools > UV Editor** (or **UV Editor** in the UVs & Seams section) shows
the selected mesh's UV layout over its texture (a checker when it has none).
Wheel zooms at the cursor; middle or right drag pans; **Frame** resets the view.

In mesh edit mode it edits:

- **Selecting:** click a UV point (Shift+click adds or removes), drag a box,
  or double-click to take a whole island. **Islands** grows the selection to the
  islands it touches, and **From 3D** takes the faces selected in the viewport.
- **Moving:** drag a selected point to move the selection; hold Ctrl to step in
  1/16ths.
- **Transforming:** **Rot ±90**, **Flip U/V**, **x0.5 / x2** and **Fit**
  (scale to fill 0–1) work on the selection around its centre.
- **Re-laying out:** **Unwrap** and **Pack** do the same as in Edit Mesh.

Each edit is one undo step (Ctrl+Z), and the viewport shows the change live.

### Sculpt

Tick **Sculpt Brush** and drag directly on the mesh:

- **Grab** — push along the drag direction.
- **Inflate** — puff along vertex normals.
- **Smooth** — relax toward the local average.
- **Flatten** — press the region onto its own average plane (hard surfaces,
  plateaus).
- **Pinch** — gather vertices toward the brush centre (sharpen creases).

Radius and strength are adjustable; Symmetry X applies to sculpting too.
Subdivide first if the mesh is too coarse for the detail you want.

### Paint

Tick **Paint Brush** to color faces directly in the viewport — pick a
color, radius, and blend, then drag over the mesh. **Fill Sel** bucket-fills
the selected faces, **Clear Paint** removes all face colors. Painted colors
multiply with the material color, save with the scene, and work on
primitives without turning them into custom geometry (Symmetry X mirrors
paint strokes too).

## Import

Load an `.obj` or `.okaymesh` file into the renderer by path (the **Mesh File** field), and one-click **Add Physics**
(Rigidbody + fitted box). For FBX/GLB/DAE and other formats, drag the file
into the viewport or use the Project panel — see `docs/animation.md` for
what animated imports bring in.

## Mesh assets (.okaymesh)

**Save as Mesh Asset** (on the Edit Mesh and Import pages) writes the selected mesh to its own `.okaymesh` file and links the object to it.
The file goes in the project's `Assets` folder, or next to the scene when there's no project.

- **Shared:** several objects (or scenes) can use the same file.
- **Saving:** while an object is linked, saving the scene (**Ctrl+S**) also writes your edits back to the file. Every object using it picks the change up.
- **Building:** Build Game copies the file into the game's `Data` folder, and the game finds it there.

An `.okaymesh` file is plain text and keeps real polygons, not just triangles:

```
okaymesh 1
verts 8
v -0.5 -0.5 -0.5
...
faces 6
f 4 0 3 2 1            # vertex count, then the vertex numbers
f 4 6 7 4 5 uv ...     # optional: per-corner UVs, color r g b a, mat k
```

- **Faces:** a face can have any number of corners (quads and n-gons included).
- **Vertex order:** stays the same when you edit, so vertex 5 is still vertex 5 after a save.
- **What a face can carry:** per-corner UVs, a face color and a material slot.

## How edits are saved

- **Primitive:** saves by name and regenerates on load.
- **Mesh loaded from a file** (`.obj` or `.okaymesh`): saves the path.
- **Hand-edited mesh:** saves its geometry verbatim in the scene file (positions, triangles and UVs), so your edits and texturing are exactly restored.
- **File-backed mesh that you edit:** keeps the edit.
  - A `.okaymesh` asset is written back to its file when you save.
  - An `.obj` stays untouched, and the edited geometry is saved in the scene instead.
  - Before this, editing an `.obj`-backed mesh and reopening the scene lost the edit.

## Guided creation (the Guided page)

The Modeling window has a **Guided** page (first in the list on the left). It helps you start something instead of modeling it from nothing, and it is part of the same modeler: what it makes is an ordinary mesh, so Edit Mesh, the modifiers, UVs, materials and the Project panel all work on it as before. Nothing about existing model files or workflows changes.

How it works, in short (the full design with the reasons is in [guided_modeling_design.md](guided_modeling_design.md)):

- **Pick, design, create.** Choose a category (Architecture, Vehicles, Characters, Environment, Props; a category with nothing in it says so), a thing (today: the **Fence**), a starting design (7 fence styles and any templates you saved) and press **Create**. You get a normal object with a mesh asset in `Assets/` and a small recipe file next to it (`Fence.okayguided`).
- **Customise.** Select the object and open the Guided page: sliders for length, height, post spacing and size, rails, pickets or bars, an optional gate, a ground slope, a path of points, a design for each kind of part (posts, caps, rails, pickets, gate) and a colour for each. Changing a setting rebuilds the model.
- **Parts.** Every generated piece has a name (`post/3`, `infill/0/5`). Select faces in Edit Mesh and the page shows which part they belong to; or open **Each part**, press **Select**, pick another design for just that part, or **Detach** it.
- **Your hand work is safe.** The rule: rebuilding never overwrites something you changed without asking. Your own added geometry is always copied through untouched. A part you moved, reshaped, deleted or attached something to is "edited by you"; if a setting change would rebuild it, nothing happens and you are asked: **Keep my edits** (the part stops following the design), **Discard my edits** (that part is rebuilt) or **Convert to regular mesh** (forget the design, keep every face). A change that does not touch an edited part (a colour, another part's size) goes ahead without asking and leaves your edit alone.
- **Convert and detach** are explicit buttons. **Undo guided step / Redo guided step** step through guided changes; the scene's Ctrl+Z also restores the mesh.
- **Your own parts and templates.** Select faces and press **Save selection as part**: it becomes a design for a kind of part (it is stretched to fit that part's box). **Save as template** keeps all the settings as a starting design. Both are plain text files in `Assets/GuidedTemplates/` (`.okaypart`, `.okaytemplate`), versioned, and a recipe carries the parts it uses so the file works on another machine.

Files: `.okayguided` (text, `okayguided 1`) holds the recipe: template and version, settings, design per kind of part, colours, path, detached part ids and embedded user parts. An editor that does not know the file simply never looks at it and opens the `.okaymesh` as usual; a recipe or template newer than the editor is reported and the model stays a regular mesh.

**Architecture** (same page, same rules): Wall (straight or along a path, with windows and a door cut in and filled), Stairs (straight, L, U, spiral, railings), Door, Window, Roof (gable, hip, shed, flat) and Building (walls, windows, a door and a roof composed as swappable parts, 1 to 6 floors). A window, the door or the roof of a building is a part you can select and give another design; the roof shape is just a design of the roof part. **Draw in the Scene view** (fence and wall) lets you click the path on the ground. **Collision**: Exact (mesh) or None, and a tick on Create. Tests: `guided_architecture` (`tests/test_guided_architecture.cpp`). Pictures and steps: [try_it.md](try_it.md) 7zi.

**Characters, more looks and animation.** A human has a **Style** (realistic, stylised low poly, cartoon, blocky), 13 hairstyles, 7 head shapes, 7 tops, 8 bottoms and a scarf. **Animation (skeleton)** on the page builds a skinned copy with 15 bones that the engine's own idle, walk and run play on (Humanoid Retarget). Builds are cached by their settings and parts of the same shape are built once (the page shows the counts). See try_it 7zk and 7zn.

**Vehicles** (same page, same rules): Car (9 designs: sedan, hatchback, coupe, SUV, pickup, sports car, van, taxi, patrol car), Truck (6: box, conventional with flatbed, tanker, light, tipper, long-haul), Bus (6: city, coach, minibus, school, shuttle, long city), Motorcycle (6: standard, cruiser, sport, scooter, dirt, touring) and Trailer (6: box, open, flatbed, tanker, caravan, utility). Each is made of parts you can select and change one by one: wheels (6 designs, radius, width, wheelbase, axle position, number of axles), headlights (4), tail lights (3), bumpers (4), mirrors, grille, spoilers (car), roof fittings (car: light bar, taxi sign, roof rack), exhausts (car, truck, motorcycle), body style and proportions (length, width, height, ground clearance, cabin size and position), glass, doors and windows (bus), cargo (truck, trailer). Motorcycles have real frame geometry: rake and fork length, a swingarm or a scooter swing-engine unit, mono or dual shocks, five frames/forks/handlebars/engines, fairings and luggage (see try_it 7zj). They are still blocky shapes at Medium.

**Characters** (same page, same rules): Human (9 designs: average, athletic, heavy, child, tall and slim, adventurer, caped hero, casual, person in a dress), Robot (6: standard, heavy, scout drone, cyclops, android, rover) and Creature (11: dog, horse, bear, stag, eagle, duck, dragon, wyvern, beetle, ant, spider, on five body plans). Parts you can select and change one by one: head (4 to 6 shapes), face, hair (6), neck, torso (3 to 4), hips, arms and legs (round, boxy, thick, piston), hands (block, mitten, fist, fingers, claw, pincer), feet (block, boot, sneaker, round shoe, tread, hover disc), top (T-shirt, jacket, vest, plates), bottoms (shorts, trousers, skirt, greaves), hat (cap, top hat, helmet, beanie), glasses, backpack (small, large, jet pack), belt, cape (short, long), shoulder pads and an antenna (robots); creatures have body, head (snout, long muzzle, beak, dragon snout, mandibles), ears, horns (short, curved, antlers), tail (short, long, bushy, fan), wings (feathered, bat, insect), legs, antennae and an abdomen. Proportion sliders: height, shoulder width, chest depth, head size, leg and arm length, limb thickness, neck length, arm angle, elbow bend, leg spread. Characters are blocky and rough (see try_it 7zk).

**Detail level** (every template, one setting): Low, Medium, High, Ultra, with the triangle count shown next to it; see [guided_detail.md](guided_detail.md).

New categories are added by registering a template (its settings, kinds of part and a generate function) in `engine/src/Modeling/Guided*.cpp`; the page, the files and the rebuild rules need no change. Tests: `guided_modeling` (`tests/test_guided.cpp`).
