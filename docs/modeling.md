# Modeling

The **Modeling** window is OkaySpace's built-in mesh editor — enough of
Blender to block out levels and props without leaving the engine. Everything
operates on the selected object's Mesh Renderer, every destructive op is
undoable (Ctrl+Z), and edited geometry is saved with the scene (including
its texture coordinates).

Create a primitive with the buttons at the top (23 shapes: Cube, Sphere,
Stairs, Gear, Torus Knot, Rounded Box, ...), or select any mesh object —
imported OBJs included. The toolset is grouped into tabs:

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
- **Fill Holes** caps every open boundary (e.g. after Delete Faces) so the
  mesh is watertight again. **Jitter** nudges every vertex randomly for an
  instant hand-made / rocky look. **Shear X/Z** slants the mesh with height
  (leaning towers, italic props).
- **Weld** duplicates, **Recenter** the pivot, **Ground** the pivot to the
  base, **Fit 1u** to scale into a unit cube. **Export OBJ** writes the mesh
  (with UVs) to `Assets/<name>.obj`.
- Symmetry / deform: **Mirror X/Y/Z**, **Spherify**, **Twist Y**,
  **Solidify** (give a surface thickness), **Taper Y**, **Bend X**.

### UV Unwrap

Quick projections that fill per-vertex texture coordinates so materials tile
properly on modeled geometry:

- **Box** — tri-planar: each face projects along its dominant axis. The
  go-to for buildings and hand-edited shapes.
- **Planar Y** — straight down: floors, terrain, tabletops.
- **Cylinder** — wraps around Y: columns, cans, lathe results.
- **Sphere** — from the centre out: planets, rocks, heads.

**tile** sets how many texture repeats span the mesh. Projected UVs survive
scene save/load and OBJ export.

## Modifiers

Blender-style one-shot modifiers: **Array X** (repeat with offset),
**Remesh** (watertight voxel rebuild), **Decimate** (cut triangle count),
**Boolean** Union / Subtract / Intersect against any other mesh object in
the scene, **Convex Hull**, **Bisect** (slice through the centre and keep a
capped half), **Split in two objects** (keep BOTH capped halves — break a
rock apart, cut doors from a wall), **Shrink/Fatten**, **Wireframe** (turn
edges into beams), **Displace** (noise roughen), **Cast Cyl Y**, and
**Stretch X/Y/Z**.

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

Tick **Edit Mesh** to enter edit mode (the mesh is auto-welded so shared
corners move together). Click in the 3D view to select **Vertices** or
**Faces** (Shift-click to add), drag the axis handles to move, hold **V**
to snap to nearby vertices.

- **Symmetry X** mirrors every move and sculpt stroke across the local
  X = 0 plane — edit half, get both.
- **Soft Select** is proportional editing: unselected vertices near the
  selection follow with a smooth falloff (set the radius).
- Face tools: **Extrude Faces** (with distance), **Inset Faces**,
  **Subdivide Sel**, **Delete Faces**, **Flip Normals**,
  **Merge by Distance**.
- Selection tools: **Bevel Sel** chamfers the selected corners (cut back and
  capped flat — knock the sharp corners off a box), **Relax Sel** smooths
  just the selected vertices, **Flatten X/Y/Z** snaps them to their shared
  average (level a rim, square a wall), and **Separate** splits the selected
  faces off into a new object with the material carried over.

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

## Import

Load an `.obj` into the renderer by path, and one-click **Add Physics**
(Rigidbody + fitted box). For FBX/GLB/DAE and other formats, drag the file
into the viewport or use the Project panel — see `docs/animation.md` for
what animated imports bring in.

## How edits are saved

- A **primitive** saves by name and regenerates on load.
- A mesh **loaded from a path** saves the path.
- A **hand-edited** mesh saves its geometry verbatim in the scene file —
  positions, triangles, and UVs — so your edits (and texturing) are exactly
  restored.
