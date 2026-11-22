# Modeling tab audit: what a modeller expects, what we have (branches `claude2/modeling-tab-2` and `claude2/modeling-tab-3`)

The owner asked (2026-11-22): "modeling still needs more work, improve, fix bugs and add features, improve the modeling tab."
This is the internal record of that pass. It was written by reading `editor/main.cpp` (Edit Mesh, the 3D view key and click
handling, the command list behind Ctrl+K), `engine/include/okay/Modeling/*`, `engine/include/okay/Render/Mesh.hpp` and the
tests, and by running small headless programs against the mesh tools. Built on PR #1106 (`claude2/area3-mesh-tools`: edge
slide keeps UVs, Bridge on faces). Nothing here was tried on Windows or with a real GPU.

## The checklist

"Have" means the code exists and a headless test covers it, unless the line says otherwise. "Editor only" means it exists in
`editor/main.cpp` but no test reaches it (the editor window cannot be driven headless here).

| Workflow | State before this branch | Where |
|---|---|---|
| Vertex / edge / face select, click, Shift-click | Have (editor only) | 3D view, keys 1 / 2 / 3 |
| Loop select (Alt+click, double-click), ring (Ctrl+Alt+click) | Have | `EdgeLoop`, `EdgeRing` (poly_ops) |
| Grow / shrink / linked / invert / all / none | Have | `GrowFaces`, `ShrinkFaces`, `GrownVertexSelection` |
| Select similar (facing, plane, area, slot), up / down facing, small faces | Have | `SelectOps` |
| Box / circle / lasso select, X-ray | Have | `ScreenSelect` |
| Hide / reveal / isolate | Have | `Mesh::HideTriangles` |
| **Shortest path select (Ctrl+click)** | **Missing** | added: `ShortestVertPath`, `ShortestFacePath` |
| **Checker deselect** | **Missing** | added: `CheckerVerts`, `CheckerFaces` |
| **Select boundary loop (region to loop)** | **Missing** | added: `RegionOutline` |
| **Select open / non-manifold edges** | **Missing** (only counted in the stats line) | added: `ProblemEdges` |
| Loop to region | Missing | round 3: `LoopToRegion` |
| Extrude (faces, edges, individual, E) | Have | `Mesh::ExtrudeFaces`, `ExtrudeEdges` |
| Inset (live I, individual) | Have | `Mesh::InsetFaces` |
| Bevel edges (Ctrl+B, segments, profile), bevel vertices | Have; needs a face on both sides of each edge | `BevelEdges` |
| Loop cut (Ctrl+R, several cuts), edge slide (GG) | Have | `LoopCut`, `EdgeSlide` |
| Knife (K) | Have | `KnifeCut` |
| Merge at centre / first, merge by distance | Have | `MergeVerts`, `WeldVertices` |
| Merge at last | Missing (small) | round 3: `MergeVertsAt` (Center / First / Last) |
| Dissolve edges / vertices | Have | `DissolveEdges`, `DissolveVerts` |
| Subdivide selection, poke, Catmull-Clark | Have | `Mesh::SubdivideFaces`, `CatmullClark` |
| Bridge edge loops / face groups, fill, grid fill | Have | `BridgeEdgeLoops`, `BridgeFaces`, `FillFaces`, `GridFill` |
| Spin, extrude along path | Have | `SpinEdges`, `ExtrudeAlongPath` |
| Rip (V), vertex slide, offset edge loops, connect vertex path | Missing | round 3: `RipEdges`, `VertexSlide`, `OffsetEdgeLoops`; connect vertex path still missing |
| Symmetry X / Y / Z for G / R / S | Have | `EditTransformSession` |
| Symmetry for the move-handle drag | **Broken**: X only, and a whole-mesh search per moved vertex every frame | fixed (see below) |
| Snapping (grid, vertex, edge, face) | Have | `SnapToGeometry` |
| Proportional editing (soft select) | Have | `EditTransformSession`, `MoveVerticesSoft` |
| **Pivot point (median / individual origins / active)** | **Missing**: R and S always turned about the selection centre | added: `EditPivot` |
| Transform orientation (local, normal axes for X / Y / Z) | Missing: locks are world axes only ("move along the normal" exists as a command) | round 3: `EditOrient`, `OrientationAxes` |
| 3D cursor as pivot | Missing | round 3: `EditPivot::Cursor`, the editor's 3D cursor |
| Numeric input during G / R / S, axis and plane locks | Have | `NumericInput`, `AxisLock` |
| Flip normals | **Broken**: ignored the selection (always the whole mesh) and turned a flat edit-mode mesh smooth | fixed (see below) |
| Recalculate outside, shade smooth / flat, auto smooth | Have | `OrientFacesOutward`, `ComputeAutoSmoothNormals` |
| Mark sharp per edge, edge split | Missing (documented gap; auto smooth covers shading by angle) | round 3: `Mesh::sharpEdges`, `EdgeSplit` |
| UV seams, LSCM unwrap, pack, UV editor | Have | `UVUnwrap` (uv test) |
| Undo for each operation | Have: one mesh step per tool (`ModelOp`, `EditUndoPoint`, `ApplyPoly`) | `MeshUndo`, `ModelOpUndo` |
| Keyboard shortcuts | Have: Tab, 1/2/3, G/R/S, E, I, Ctrl+R, Ctrl+B, K, H, B/C/L, Shift+D, Ctrl+K, Shift+R; added `.` (pivot) and Ctrl+click (path) | editor |
| Viewport overlays | Have: polygon wireframe, seams red, creases pink, bevel weights green, selection, measure readout, stats line | editor |

## Bugs fixed here

| # | Seen | Cause | Fix | Test (failed before the fix) |
|---|---|---|---|---|
| 1 | Flip Normals in Edit Mesh turned a box into a blobby smooth-shaded shape; with faces selected it still flipped the whole mesh | `Mesh::FlipNormals` always called `ComputeSmoothNormals`; the button and Ctrl+K command never looked at the selection | `FlipNormals` keeps the shading (flat stays flat, stored normals turn round); new `Mesh::FlipFaces(tris)`; the button and command flip the selected faces in Face mode | `meshedit`: "FlipNormals keeps the shading" |
| 2 | Fill on border edges that run in a straight line (three edges along one side of a plane) made a four-corner face with no area: invisible, no usable normal, closed nothing | `FillFaces` made a face from any open chain of 3+ border vertices | a chain whose area is nothing next to its perimeter is skipped; with nothing else to fill, Fill says the vertices are in a straight line | `poly_ops`: "Fill refuses a chain of vertices in a straight line" (177 checks, 4 failures before; 0 after) |
| 3 | Dragging a move handle (the coloured axis arrows) in Edit Mesh mirrored across X only, even with Symmetry Y or Z on; on a big mesh with many vertices selected it froze (for each moved vertex it searched every vertex for its partner, every frame) | its own copy of the move code | the drag now runs through `EditTransformSession` (the same code as G: soft select, symmetry X / Y / Z, partners found once at the start) | editor-only wiring; the session it uses is covered by `edit_transform` (no new failing test: the editor window cannot be driven headless) |

Found while trying the new tools in the editor (Xvfb run, Linux, software drawing) and fixed before this branch was offered:

- Individual Origins first joined parts through any edge between two selected vertices, so in Face mode two faces with an
  unselected face between them scaled as one part. Parts are now joined only through the selected faces (Face mode) or the
  selected edges (Edge mode) and positions they share. Test: `edit_transform` "Individual Origins in Face / Edge mode" (104
  checks, 2 failures before; 0 after). Faces that touch only at a corner (a checkerboard) still share that vertex, so they are
  one part: select faces that do not touch to grow each in place.
- The G / R / S status line was centred even when wider than the 3D view, so its start (tool name and value) was cut off on
  the left in a narrow view. It now starts at the view's left edge (editor only, seen on the screenshots).

Found by the hostile-input probe and left as they are (not reachable from the editor, or by design): `SplitEdge` with t outside 0..1
puts the new vertex beyond the edge (the editor only splits at the middle); `LoopCut` has no upper limit on `cuts` (the editor caps it
at 32); a knife cut between two points on the same edge reports one new edge but splits no face; `MergeVerts` on two opposite
corners of a quad removes that quad (as other modellers do).

## Features added here

- **Select Shortest Path (Ctrl+click)** in all three modes: vertices along edges by length, edges by joining the nearest ends,
  faces through shared edges by centre distance. Written from Dijkstra's shortest-path algorithm.
- **Checker Deselect**: keeps the selected elements an even number of breadth-first steps from the first one picked in each
  connected part (a grid of faces becomes a checkerboard).
- **Select Boundary Loop**: the outline of the selected faces, as edges.
- **Select Open Edges / Select Non-Manifold Edges**: edges with one face, or three or more.
- **Pivot for R and S**: Median Point (as before), Individual Origins (each connected part of the selection, joined by an
  edge or by sharing a position, turns about its own centre; soft-selected vertices follow their nearest selected vertex's
  part; symmetry partners turn about the mirrored centre), Active Vertex (the last vertex picked; falls back to the median when
  it is not selected). Combo in Edit Mesh, `.` cycles it in the 3D view, three Ctrl+K entries, shown on the status line.
- **Flip selected faces** (see bug 1).

All are on the Edit Mesh page (Pick parts group: Checker, Boundary Loop, Open Edges, Non-Manifold, Pivot) and in the Ctrl+K
command list. Selection tools only change the selection, so they add no undo step (as Grow and Shrink).

## Round 3 (branch `claude2/modeling-tab-3`, on top of round 2)

### The three hostile-input leftovers, fixed

| # | Seen | Fix | Test (failed before the fix) |
|---|---|---|---|
| 1 | `SplitEdge` with t outside 0..1 put the new vertex beyond the edge | t is clamped to 0..1 (not a number is still refused) | `poly_ops`: "SplitEdge with t outside 0..1" |
| 2 | `LoopCut` had no upper limit on `cuts` (only the editor's 32) | `kLoopCutMax` = 64 in the engine | `poly_ops`: "LoopCut had no upper limit" |
| 3 | A knife cut between two points on the same edge reported a new edge but split no face | a cut that splits no face is refused with a message, the mesh unchanged | `poly_ops`: "A knife cut between two points on the same edge" |

Before / after numbers are in the round-3 report (`poly_ops` run on the old code with the new tests).

### Found while adding the tools, fixed

- **Auto Smooth left edge lists pointing at the wrong vertices.** `ComputeAutoSmoothNormals` renumbers the vertices but kept
  UV seams, creases and bevel weights with the old numbers (so after Auto Smooth a seam could sit on an unrelated edge). They now
  follow each old vertex to its first copy. Vertex colours, left at the old size before (so the renderer ignored them), now
  follow their copies too. Test: `meshedit` "Auto Smooth used to leave UV seams, creases and bevel weights pointing at the old
  vertex numbers".
- **Edit Mesh lost Auto Smooth.** The weld that opens Edit Mesh rebuilt the normals fully smooth and cleared the angle, so an
  Auto Smooth mesh came back from Edit Mesh fully smooth (or flat, after a move cleared the normals). Leaving Edit Mesh now
  rebuilds Auto Smooth with the angle the mesh had when it was opened (editor only; the engine part is covered by `meshedit`).

### Tools added

All are one undo step, keep existing vertex numbers (new ones are appended), give new corners UVs from their face, have a
button on the Edit Mesh page (Edge loops and joins, or Pick parts for the cursor), a Ctrl+K entry and an entry in
`editor/ModelingToolIndex.hpp`. Written from the plain description of what each tool does to the faces (no other program's code).

- **Transform orientation (Axes, `,`):** Global, Local (the object's axes, straightened to right angles), Normal (Z = the
  area-weighted normal of the selected faces, or of the faces around selected edges / vertices; X level with the world).
  `OrientationAxes`, `MoveMatrix` / `ScaleMatrix` / `MoveFromRays` overloads that take the axes. G / R / S, the typed value and
  the drawn axis line use them; E (extrude and move) now always moves along the faces' normal (before: only when the normal
  was close to a world axis).
- **3D cursor and the Cursor pivot:** `EditPivot::Cursor` with `EditTransformOptions::cursorWorld`; the editor draws a red and
  white ring, Shift+right-click places it on the mesh (or the ground), Cursor to Selected / Cursor to Origin.
- **Mark Sharp / Clear Sharp:** `Mesh::sharpEdges` (and `PolyMesh::sharp`), kept through weld, Combine, ToMesh / FromMesh,
  CompactVerts, SplitEdge, `.okaymesh` ("sharp a b") and the scene file (`meshsharp`, written before `meshshade`).
  `ComputeAutoSmoothNormals` splits a smoothing group where the faces only join across a sharp edge (matched by position,
  so seam copies count). Drawn cyan in Edit Mesh. Shade Smooth outside Edit Mesh uses Auto Smooth at 180 degrees when there are
  sharp edges.
- **Edge Split:** `EdgeSplit`: every vertex of the edges gets one copy per fan of faces the edges cut apart around it; the
  edges and their copies are marked sharp.
- **Vertex Slide (Shift+V):** `VertexSlide` / `VertexSlideRail`; live in the 3D view like edge slide (shares its start state,
  UV following and undo).
- **Offset Edge Loops (Ctrl+Shift+R):** `OffsetEdgeLoops`: one new loop per side with faces, at a fraction of each edge leaving
  the loop; refuses 0.5 or more when an edge joins two picked edges' corners (the loops would cross).
- **Rip (V):** `RipEdges`: the faces on the side the mouse is on get copies of the edges' vertices; ends inside the surface stay
  joined; the torn vertices are selected and a move starts.
- **Merge at Last:** `MergeVertsAt` (Center / First / Last); the page's merge buttons and three new Ctrl+K entries share it.
- **Loop to Region:** `LoopToRegion`: the smaller group of faces the selected loop cuts off.

### Limits found and left

- In Edit Mesh the mesh is welded by position on entry, so the two sides of an Edge Split or a Rip left at the same place are
  joined again the next time Edit Mesh opens (the sharp marks stay, so the shading stays hard). The hard shading itself shows
  after leaving Edit Mesh: while editing, a smooth mesh is shaded fully smooth (welded) or flat (after a move).
- Edge Split of one edge alone splits nothing (its ends stay joined round the other side); it only marks the edge sharp.
- Rip of a single edge inside the surface is refused (both ends stay joined); vertex-only rip (one vertex, no edges) is not done.
- Normal orientation's X axis is chosen level with the world, not along a selected edge.
- Snapping during a move with Local or Normal axes still rounds the world-space offset to the grid.
- Connect vertex path is still missing.
