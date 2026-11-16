# Guided Procedural Modeling: inspection and design

Owner request: extend the EXISTING 3D modeler with a guided assistant ("pick Fence, pick a design, get an editable
fence, customise it, swap parts, keep editing by hand"). No new modeler, no new workspace. Phase 1 (this change) is the
foundation plus one complete example, the fence. Phase 2 (architecture, vehicles, characters, environment, props) is
built later by registering more templates; nothing in the core has to change for that.

## 1. What exists (inspected)

| Need | What the engine already has | Where |
|---|---|---|
| Authoring geometry | `PolyMesh` (quads and n-gons, UV per corner, colour and material slot per face, seams) | `engine/include/okay/Modeling/PolyMesh.hpp` |
| Render form | `okay::Mesh` (triangles, `polyFace`, `triColors`, `cornerUVs`); `PolyMesh::ToMesh` / `FromMesh` convert both ways (`FromMesh` uses `polyFace` exactly when set) | `Render/Mesh.hpp`, `PolyMesh.cpp` |
| Model files | `.okaymesh` text (`okaymesh 1`, `v`, `f ... uv ... color ... mat`, `seam`); `Parse` skips `#` lines and rejects unknown records and versions above 1; `LoadMeshAsset` / `SaveMeshAsset` | `PolyMesh.cpp`, `MeshAsset.cpp` |
| Mesh editing | `PolyOps` (extrude, bevel, loop cut, bridge, fill, dissolve ...), `EditTransform`, `UVUnwrap`; editor mesh edit mode (`g_meshEdit`, `SetMeshEditMode`, `ApplyPoly`, `EditPoly`, selection sets `g_meshSelFaces/Verts/Edges`, mesh-level undo `g_meshUndo`) | `engine/src/Modeling/*`, `editor/main.cpp` ~3180-3500 |
| Non-destructive stack | `MeshModifiers` component (Mirror, Array, Subdivision ...) evaluates over the base mesh; it never touches the base | `Modifiers.hpp` |
| Storage in scenes | `MeshRenderer::meshPath` (+ `meshPathHash`); saving the scene writes an edited mesh back to the file or inline | `SceneSerializer.cpp` ~880-905 |
| UI | The editor's **Modeling** window is a sidebar of pages (`kPages` in `DrawModeling`: Add, Shape, Edit, Modifiers, Generate, Edit Mesh, Import). Thumbnails: `GetPrimitiveThumb`, `RenderObjectThumb` | `editor/main.cpp` ~27554 |
| Materials | per-face material slot index is stored in `.okaymesh` (`mat N`) but not yet drawn per face (UNFINISHED M5b, not touched here); per-face colours ARE drawn (`triColors`) | `Mesh.hpp` |
| Tests | `test_polymesh`, `test_poly_ops`, `test_modifiers`, `test_mesh_asset`, `test_uv` | `tests/` |

There is no existing procedural template, recipe or "part" concept for models (`find_existing.py` finds nothing).

## 2. Where Guided Creation lives

A new **Guided** page in the existing Modeling window sidebar (listed first, it needs no selected mesh to start).
It shows: the five categories (Architecture, Vehicles, Characters, Environment, Props); a category with no templates
says "No designs here yet"; Architecture shows Fence (phase 1). Picking a template shows the **design browser**:
cards with thumbnails drawn by our own renderer from the generated mesh (same path as `GetPrimitiveThumb`), the built-in
starting designs plus the user's saved templates. **Create** makes an object (MeshRenderer) in the scene, writes
`<Assets>/<Name>.okaymesh` and the sidecar, selects it. When the selected object is a guided model the page shows the
parameter panel, the parts list and the part actions. Nothing is a new window; everything else (Edit Mesh page, Tab edit
mode, Modifiers, materials) is unchanged and works on the generated mesh because it is an ordinary mesh.

## 3. Data model: the recipe

`okay::modeling::guided::Recipe` (text, versioned):

* `template id` and `template version`; typed parameters (number, whole number, on/off, choice stored as a number);
* **slots** (post, cap, rail, infill, gate for the fence): each has a design id, optionally overridden per part id
  (`partdesign post/3 round`);
* colour and material slot per slot (colour is drawn today; the slot number is stored for M5b);
* an optional ground **path** (polyline of 3D points; empty means a straight run of `length` with `slope`);
* the list of **detached** part ids; embedded **user parts** used by the recipe (so a file opens on any machine).

Parts have stable ids built from slot and index (`post/3`, `rail/1/0`, `infill/2/5`, `gate/leaf`). Generation is a pure
function `Generate(recipe) -> parts` and is deterministic (same recipe, same bytes; tested).

Storage: next to the model asset, `Fence.okaymesh` + `Fence.okayguided` (sidecar, `okayguided 1`). The `.okaymesh` stays
a normal file, so every existing tool, the scene loader and OLD editors open it as before. An old editor simply ignores the
sidecar (it never looks at it). A newer sidecar version than the editor knows (or a template version newer than the code)
is reported and the model stays a regular mesh; nothing is deleted.

## 4. The regeneration contract (never silently destroys edits)

No tags are stored in the mesh (edit tools rebuild faces freely). Instead the baseline is recomputed:
`baseline = Generate(stored recipe)`, which is exact because generation is deterministic. `Analyze(current, stored)` then:

1. gives each current face a signature (its corner positions on a 1e-4 grid, rotation-normalised) and matches it to a
   baseline face; a match is "pristine" only if UVs, colour and material also equal the baseline's;
2. groups faces into connected pieces by shared corner positions;
3. a part is **Intact** when all its baseline faces are found pristine and every piece it lives in contains only pristine
   generated faces; **Edited** when some of its faces are changed or missing, or other geometry is attached to it; a piece
   that matches nothing but has the same rigid shape (face sizes) as one unclaimed baseline part is that part **moved**
   (Edited); a baseline part with no faces left is **Deleted** (counts as edited, it stays deleted);
4. everything else is **Custom**: the user's own additions.

`Regenerate(current, stored, next, options)`:

* Custom pieces are copied through untouched (positions, UVs, colours, seams).
* Intact parts are replaced by the freshly generated parts of `next` (this is what a parameter change is).
* Edited or Deleted parts are NEVER overwritten silently. If any exist and the caller gave no decision, the result is
  `needsDecision` with the list, and the mesh is not changed. The decision per part: **Keep** (the part detaches: its
  geometry stays as regular mesh and the part id is added to `detached`, so it is no longer generated), **Discard** (its
  geometry, and any geometry attached to it, is removed and the part is generated fresh), or the whole model can be
  **converted to a regular mesh** (sidecar dropped, mesh untouched). Keep wins whenever a piece belongs to several parts.
* Parts that exist only in `next` are added; parts that vanish from `next` and are Intact are removed; vanished but Edited
  parts follow the same decision.
* If the template version in the stored recipe is not the code's version the baseline cannot be trusted: regeneration
  refuses and offers convert.

Explicit tools: `DetachPart` (one part becomes plain mesh), `ConvertToRegular`. Undo: `GuidedHistory` keeps (mesh, recipe)
snapshots; the editor also pushes its normal scene undo step.

Known limits (listed in UNFINISHED): geometry welded to a part at an exact shared corner position counts as an edit of
that part (conservative: it asks); a moved part made of several separate shells is reported Deleted plus Custom;
after the editor's global Ctrl+Z the sidecar may be newer than the mesh, in which case parts show as edited (asks, never
destroys).

## 5. Parts, user parts and user templates

* A **slot design** is `PolyMesh f(Frame, Recipe)`; a `Frame` is a box (origin at bottom centre, tangent, up, normal,
  size). Built-in designs and user parts both fill the frame, so any design can replace any other in a slot.
* **Save selection as part** (`.okaypart`): the selected faces of the mesh, normalised to a unit box, saved with a name
  and the slot it fits, in `<Assets>/GuidedTemplates/`. It then appears as a design `user:<id>` for that slot; choosing it
  embeds the part in the recipe.
* **Save as template** (`.okaytemplate`): the recipe with a name, shown among the starting designs of its template.
* Both are plain text and versioned (`okaypart 1`, `okaytemplate 1`), loadable on any machine.

## 6. Extension API

`guided::Registry::Register(Template)`; a `Template` is data (id, version, category, parameter definitions, slot
definitions with their designs, starting designs) plus one generate function. A new category adds a source file that
calls `Register` from `RegisterBuiltinTemplates()`; the core, the editor page and the tests iterate the registry.

## 7. Files (phase 1)

New: `engine/include/okay/Modeling/Guided.hpp`, `engine/src/Modeling/Guided.cpp` (recipe, registry, analysis, regeneration,
user parts, history), `engine/src/Modeling/GuidedFence.cpp` (fence, written from general parametric modelling: points
along a path, boxes and extruded profiles), `tests/test_guided.cpp`, `tools/guided_pictures.cpp` (headless renders),
`examples/modeling/`, `docs/images/guided_modeling/`.
Changed: `engine/CMakeLists.txt`, `tests/CMakeLists.txt`, `editor/main.cpp` (a Guided page and its actions only),
`docs/modeling.md`, `docs/try_it.md`, `docs/UNFINISHED.md`, feature map, `scripts/build-windows-dist.sh`.
Reused: `PolyMesh` (`ToMesh`, `FromMesh`, `Serialize`, `Parse`, `FaceNormal`, `Check`), `SaveMeshAsset`, `ed.PushUndo`,
`GetPrimitiveThumb` pattern, `SetMeshEditMode`, the Modeling window page list.
