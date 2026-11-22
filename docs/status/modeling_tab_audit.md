# Modeling tab audit: what a modeller expects, what we have (branch `claude2/modeling-tab-2`)

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
| Loop to region | Missing | not done |
| Extrude (faces, edges, individual, E) | Have | `Mesh::ExtrudeFaces`, `ExtrudeEdges` |
| Inset (live I, individual) | Have | `Mesh::InsetFaces` |
| Bevel edges (Ctrl+B, segments, profile), bevel vertices | Have; needs a face on both sides of each edge | `BevelEdges` |
| Loop cut (Ctrl+R, several cuts), edge slide (GG) | Have | `LoopCut`, `EdgeSlide` |
| Knife (K) | Have | `KnifeCut` |
| Merge at centre / first, merge by distance | Have | `MergeVerts`, `WeldVertices` |
| Merge at last | Missing (small) | not done |
| Dissolve edges / vertices | Have | `DissolveEdges`, `DissolveVerts` |
| Subdivide selection, poke, Catmull-Clark | Have | `Mesh::SubdivideFaces`, `CatmullClark` |
| Bridge edge loops / face groups, fill, grid fill | Have | `BridgeEdgeLoops`, `BridgeFaces`, `FillFaces`, `GridFill` |
| Spin, extrude along path | Have | `SpinEdges`, `ExtrudeAlongPath` |
| Rip (V), vertex slide, offset edge loops, connect vertex path | Missing | not done |
| Symmetry X / Y / Z for G / R / S | Have | `EditTransformSession` |
| Symmetry for the move-handle drag | **Broken**: X only, and a whole-mesh search per moved vertex every frame | fixed (see below) |
| Snapping (grid, vertex, edge, face) | Have | `SnapToGeometry` |
| Proportional editing (soft select) | Have | `EditTransformSession`, `MoveVerticesSoft` |
| **Pivot point (median / individual origins / active)** | **Missing**: R and S always turned about the selection centre | added: `EditPivot` |
| Transform orientation (local, normal axes for X / Y / Z) | Missing: locks are world axes only ("move along the normal" exists as a command) | not done |
| 3D cursor as pivot | Missing | not done |
| Numeric input during G / R / S, axis and plane locks | Have | `NumericInput`, `AxisLock` |
| Flip normals | **Broken**: ignored the selection (always the whole mesh) and turned a flat edit-mode mesh smooth | fixed (see below) |
| Recalculate outside, shade smooth / flat, auto smooth | Have | `OrientFacesOutward`, `ComputeAutoSmoothNormals` |
| Mark sharp per edge, edge split | Missing (documented gap; auto smooth covers shading by angle) | not done |
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
