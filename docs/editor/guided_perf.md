# Guided modeling: why it lagged, what was measured, what changed

Written for the change "Guided: stop the lag" (branch `claude/guided-perf-1`). All numbers are from a loaded 4-core Linux machine (other builds were running), release build,
`OKAY_GUIDED_PERF=1 ./test_guided_perf`. Treat them as relative, not absolute. Nothing here was measured inside the editor window.

## What the owner saw

Moving a slider, picking a style, or changing Detail on a guided model made the editor stall, worst at High and Ultra, and the panel cost time even when idle.

## The causes (found by measuring, in order of size)

| # | Cause | Where | Measured |
|---|-------|-------|----------|
| 1 | Every slider tick rebuilt the whole model only to show the "N triangles now" number (`DetailWidget` called `Create` whenever any setting changed). | `editor/GuidedPanel.cpp` | one full build per tick: car Ultra 23-27 ms, human Ultra 23-38 ms |
| 2 | Every frame the panel hashed the whole mesh and wrote the recipe text to see if it changed. | `DrawCustomise` | car Ultra 5.9 ms, human Ultra 6.1 ms, per frame, doing nothing |
| 3 | A released slider did the same work about five times: build for the count, analyse (builds the old design again), regenerate (builds the new design, and the old one again), analyse again afterwards, validate (builds again). Nothing was cached between them. | `Apply`, `Regenerate`, `Analyze`, `Validate` | commit at Ultra: human 711 ms plus 291 ms afterwards, car 587 + 236, building 294 + 111 |
| 4 | Face matching, piece finding, vertex welding and smooth shading used ordered maps keyed by vectors (one allocation per face or corner). Saving wrote 100,000+ numbers through an ostream. | `Guided.cpp`, `PolyMesh.cpp`, `Mesh.hpp` | human Ultra: save 397 ms, regenerate 189, read-back 105, smooth 54, triangulate 37 |
| 5 | Undo states kept a full copy of the polygon mesh (up to 64 of them). | `GuidedHistory` | memory, and 100 ms to make each one at Ultra |

Alongside the speed problems the measurement exposed three real bugs:

* **"Parts you edited by hand" asked about a model nobody touched** (27 of the 57 template and detail combinations at High and Ultra). Smooth shading splits corners
  into one vertex per smoothing group, and the polygons could not be rebuilt from the split triangles, so parts read as edited. `PolyMesh::FromMesh` now follows polygon outlines through
  vertices at the same position.
* **Smooth shading moved vertices.** Corners within 1/4096 of each other took one position, which squeezed thin parts (the car's 0.0002 wide wiper blades) flat. They keep their position now, and a
  corner keeps its exact UV.
* **Loose vertices piled up** after a rebuild of a smooth model (the unused copies a smoothing pass leaves). They are dropped now; vertices a user really left loose are kept.
* **Ultra pulled parts apart**: Catmull-Clark shrinks every part, so bus windows floated 10 cm off the body (buses had 14-26 separate pieces at Ultra). A subdivided part is stretched back to the box it had.
  The tipper truck's bed floated 7 cm over the chassis at every level; it sits on it now.

## What changed

* `Generate` caches the parts by recipe text (one cache; `Create`, the analysis, the regenerate and the check share it). `Registry::Register` clears it.
* `IsUntouched`, `RegenerateUntouched`, `UntouchedAnalysis`: a model with no hand edits is rebuilt without reading the mesh back or matching faces, and its analysis afterwards is known without running it.
* Analysis and read-back use hashed lookups; `Serialize` of a mesh writes through one buffer; `CoincidentReps` and the smooth-shading corner table are hashed / indexed.
* The panel: the mesh is fingerprinted at most four times a second and never while a control is held; the triangle count comes from the mesh on screen; sliders and colours drag a Medium-detail preview
  (`PreviewDetail`, `PreviewThrottle`: never more than a quarter of the time spent rebuilding) and the real model is built once on release (one undo step, one save); a time line shows the cost of the last change.
* Undo states of an untouched model keep only the recipe (`GuidedState::fromRecipe`).

## Numbers

Before (released slider at Ultra, ms; "extra" = work the panel did afterwards) and after (the same, with the untouched fast path). Medium-detail drag preview after.

| template (Ultra) | triangles | before: commit | before: extra | after: whole release | drag preview |
|------------------|-----------|----------------|---------------|----------------------|--------------|
| human | 119,584 | 711 | 291 | 318 | 1.6 |
| car | 107,272 | 587 | 236 | 275 | 0.7 |
| motorcycle | 50,976 | 231 | 99 | 150 | 1.0 |
| building | 46,732 | 294 | 111 | 124 | 1.0 |
| truck | 37,224 | 154 | 66 | 89 | 0.7 |
| tree | 18,928 | 172 | 53 | 62 | 0.5 |

Per frame with the panel open: car Ultra 5.9 ms to under 0.1 ms on average (one hash every quarter second, none while dragging).

What is left in a release is mostly writing the .okaymesh file (about 100 to 200 ms at 100,000 triangles) and the triangulation and smooth shading of the new mesh.

## Tests

`tests/test_guided_perf.cpp` (`ctest -R guided_perf`). Each fix has a check that failed on the old code (measured before the change: 27 combinations read as edited; wiper blades squeezed; five builds per release):
untouched after smoothing (every template, Low to Ultra), smoothing keeps positions, save and reopen stays untouched, builds are cached, the fast path equals `Regenerate`, no loose vertices,
removed and edited parts survive a rebuild at High and Ultra, undo states from the recipe, the preview throttle, and the timing table with generous budgets.
