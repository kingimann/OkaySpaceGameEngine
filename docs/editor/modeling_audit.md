# Modeling audit (branch `claude/modeling-more`)

Written from the code and from headless experiments (small programs that build meshes through the existing API, run a
tool and check the result). Nothing here was tried in the real editor window or on Windows. Numbers are from a shared
4-core Linux machine under load, one run each, so treat them as rough.

## What the modeler can do today

- **Add**: 23 primitives, nature props, props and structures (all editable meshes).
- **Whole-mesh edit tools**: subdivide, smooth, relax, loop cut, fill holes, bridge, jitter, shear, symmetrize, snap grid,
  shade smooth/flat, auto smooth by angle, weld, recenter, ground, fit, OBJ export, mirror, spherify, twist, solidify,
  taper, bend, UV projections, remesh, decimate, boolean, convex hull, bisect, split, shrink/fatten, wireframe, displace.
- **Edit Mesh** (vertex, edge, face modes): G / R / S modal edit, soft select (proportional editing), symmetry X,
  snapping, extrude, inset, poke, individual extrude and inset, bevel, edge loops and rings, loop cut, edge slide,
  dissolve, bridge, knife, fill, grid fill, merge, flip normals, recalculate outside, merge by distance, UV unwrap with
  seams, UV editor, sculpt brushes, vertex paint (per-face colour), polygon stats.
- **Modifier stack** (non-destructive): Mirror, Array, Subdivision Surface (Catmull-Clark on real polygons), Solidify,
  Bevel, Boolean, Decimate, Displace; each can be enabled, moved, applied and removed.
- **Files**: `.okaymesh` (text, quads and n-gons, UVs, per-face colour, per-face material number, seams), OBJ import and
  export, scene save keeps edited geometry, UVs, polygons, seams, shading and face colours.
- **Undo**: edit-mode undo with selection, 100 steps / 4 million vertices.

So the long list in the brief (subdivision, boolean, decimate, solidify, bevel with profile, weld, auto smooth, soft
selection, snapping, mirror, repair pieces, stats) already exists. This change does not build a second copy of any of them.

## What was missing

1. **Per-face materials (M5b).** `PolyFace::material` existed and was written to `.okaymesh`, but the triangle `Mesh` had
   no per-triangle slot, so the number was lost the moment a mesh went `PolyMesh -> Mesh` (observed: a cube with slot
   numbers 0,1,2,0,1,2 came back with a sum of 0, expected 6), and the editor had no way to set it. Fixed here as slots
   with a colour each (see below); per-slot textures and shaders are **not** built.
2. **One-click repair.** The pieces (merge by distance, flip, recalc outside, fill holes) were four buttons; nothing ran
   them in the right order or said what changed. Added `Mesh::Repair()` and a **Repair Mesh** button.

## Bugs found (each has a test in `tests/test_modeling_more.cpp`, which failed before the fix)

| # | Observed | Cause | Fix |
|---|---|---|---|
| 1 | Dissolving 4 edges of a cube left **0 faces**. Dissolving every edge of any closed mesh left 0 or 1 faces. | The "remove vertices with two edges" clean-up ran on a two-face mesh and ate it; faces that collapse to nothing were kept as merges. | A merge that would leave a face with fewer than 3 corners is skipped; a clean-up never shrinks a face below 3 corners. |
| 2 | `DissolveEdges` on every edge of a 240k-triangle sphere ran for **more than 3 minutes** (it scans every face for every edge, then rebuilds the topology for every end vertex). 19k triangles took 0.2 s, 100k+ is quadratic. | Whole-mesh scan per edge. | Directed-edge table kept up to date: 120k edges now take **1.35 s**. |
| 3 | `Mesh::DeleteFaces({0,0,0})` deleted **three** faces (12 -> 9 triangles). | The list was not made unique. | Sorted and made unique. |
| 4 | `Mesh::Decimate(NaN)` erased the whole mesh (0 triangles). | `cellSize <= eps` is false for NaN. | `!(cellSize > eps)` is the guard. |
| 5 | `Mesh::WeldVertices(-1)` merged a cube into 4 vertices. | A negative distance was squared. | A negative or NaN distance means "exact repeats only". |
| 6 | `WeldVertices` on a 240k-triangle sphere took about **6 s** (compares every vertex with every kept vertex). | All-pairs search. | Grid of cells as wide as the distance; same result (first match wins), tested against an all-pairs search; under 0.3 s. |
| 7 | `Mesh::FillHoles` wiped **all painted face colours** (and would drop slots) whenever it filled a hole. | `triColors.clear()`. | Old faces keep colour and slot; the caps are white / slot 0. |
| 8 | In edit mode the polygon cache key hashed all triangle bytes **one byte at a time**: about **4.1 ms per call** at 240k triangles, several calls a frame. | Byte-wise FNV. | Eight bytes at a time: **0.8 ms**. |

Fixed later: `ExtrudeFaces`, `InsetFaces`, the Individual versions and `BevelEdges` given NaN produced NaN vertices; they now do nothing (Bevel says why).

Not fixed, reported: `BevelEdges` leaves the 8 original corner vertices unused (by design: tools keep vertex numbers, the editor
compacts them); `CatmullClark` has no cap on `levels` (the modifier and the UI cap it at 4; calling it with 50 asks for 6 * 4^50 faces).

## Checked and fine

Empty selections and out-of-range face numbers are ignored by extrude, inset, delete, subdivide, poke and individual
extrude (no crash, mesh unchanged). Loop cut on a mesh without UVs adds none; on an unwrapped cube every new face keeps
UVs (10 of 10). Subdivision of a cube: 24, 96, 384 faces, all closed, nothing non-finite. Bevel clamps a huge width
(width 5 on a unit cube gives the same shape as 0.5). Poke with a repeated face number pokes it once.

## High-poly timing (headless, 240k triangles, one run)

| Step | Time |
|---|---|
| `PolyMesh::FromMesh` | 241 ms |
| `PolyMesh::ToMesh` | 80 ms |
| `PolyMesh::Edges` | 32 ms |
| `PolyTopology` build | 45 ms |
| Polygon cache key (per call, before / after) | 4.1 ms / 0.8 ms |
| `WeldVertices` (before / after) | about 6 s / under 0.3 s |
| `DissolveEdges`, every edge (before / after) | over 3 min / 1.35 s |

Selection, G/R/S and the modifier stack were **not** measured here: they run in the editor window, which this machine
cannot open. The modifier stack already hashes its input with an eight-byte hash.

## 105k-triangle sphere, headless (one run, shared machine, load about 2.7)

Header-only parts built at -O2: Bisect 139 ms, WeldVertices after the fix 95 ms (Mirror = bisect + copy + weld, so about 0.3 s).
Engine parts, built at **-O0** (so only the order of size is meaningful): modifier-stack input hash 0.45 ms per frame, geometry hash
3.9 ms, one undo copy 0.31 ms, `FromMesh` 338 ms, Solidify 8 ms, Displace 33 ms, Decimate 33 ms, Array 23 ms, smooth normals 14 ms.
Nothing here measures the editor window itself (selection, drawing): unmeasured. Before/after exists only for the items in the table above.

## New tools at 105k triangles (headless, -O2, one run each, median of 7, shared machine, load about 2.4)

| Step | Before the tuning | After |
|---|---|---|
| Measure the whole mesh (size, area, volume) | 0.8 to 1.2 ms | same; the editor now recomputes it twice a second, not every frame |
| Measure a selection of 50% of the faces | 11.4 ms | 7.5 ms (marks instead of sets); also only twice a second |
| Grow Selection on 50% of the faces | 131 ms | 26 ms (hash table + marks) |
| Select Similar (facing) / Select Up-Facing | 0.8 ms / 0.7 ms | - |
| Snap to a vertex while moving (per frame) | 0.05 ms | - |
| Snap to an edge / a face while moving (per frame) | 1.8 ms / 2.9 ms | - |

Per-frame work that was removed before it shipped: the command list now guesses "are edges selected" from the selection counts instead
of working out the real edges every frame (that took milliseconds on big meshes). Not measured: drawing, selection picking, G / R / S in the real window.

## Operator fuzz test (tests/test_modeling_fuzz.cpp, `modeling_fuzz`)

Every Mesh and polygon tool, the selection helpers, the recipes and every modifier are run on random meshes (cube, sphere, cylinder, torus, joined, open, degenerate, empty) with hostile selections (nothing, everything, out-of-range numbers, repeats) and hostile amounts (0, negative, 1e9, NaN, infinity). Nothing may crash or leave a non-finite vertex or a triangle outside the vertex list. At 3000 rounds per operator it found and I fixed: NaN or infinite amounts turning every vertex into NaN in Poke, Bevel Vertices, Snap Grid, Soft Move, Smooth, Spherify, Twist, Taper, Bend, Stretch, Solidify, Displace (also absurd amounts overflowing its noise), Loop Cut position, Edge Slide, Split Edge, Merge Verts, Extrude Along Path; modifiers loaded with a NaN setting (every modifier now refuses with "A setting is not a number"); and Fill, Grid Fill and Bridge accepting edges that name vertices that do not exist (a face with a bad index). Default run is 120 rounds; set OKAY_FUZZ_ROUNDS for more. Undo restoring is by whole-mesh copy, so it is not fuzzed separately.
