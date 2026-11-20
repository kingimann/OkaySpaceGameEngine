# Guided modeling

> **What this is:** Guided procedural modeling: the design, detail levels, the speed work and the generator quality audit.
>
> **Status:** combined on 2026-11-20 from 4 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
>
> **Last checked:** 2026-11-20, paths and links against main.

## Contents

- [Guided Procedural Modeling: inspection and design](#guided-procedural-modeling-inspection-and-design) (was `docs/guided_modeling_design.md`)
- [Guided modeling: detail levels](#guided-modeling-detail-levels) (was `docs/guided_detail.md`)
- [Guided modeling: why it lagged, what was measured, what changed](#guided-modeling-why-it-lagged-what-was-measured-what-changed) (was `docs/guided_perf.md`)
- [Guided generator quality audit](#guided-generator-quality-audit) (was `docs/generator_quality_audit.md`)

## Guided Procedural Modeling: inspection and design

*Was `docs/guided_modeling_design.md` until 2026-11-20.*

Owner request: extend the EXISTING 3D modeler with a guided assistant ("pick Fence, pick a design, get an editable
fence, customise it, swap parts, keep editing by hand"). No new modeler, no new workspace. Phase 1 (this change) is the
foundation plus one complete example, the fence. Phase 2 (architecture, vehicles, characters, environment, props) is
built later by registering more templates; nothing in the core has to change for that.

### 1. What exists (inspected)

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

### 2. Where Guided Creation lives

A new **Guided** page in the existing Modeling window sidebar (listed first, it needs no selected mesh to start).
It shows: the five categories (Architecture, Vehicles, Characters, Environment, Props); a category with no templates
says "No designs here yet"; Architecture shows Fence (phase 1). Picking a template shows the **design browser**:
cards with thumbnails drawn by our own renderer from the generated mesh (same path as `GetPrimitiveThumb`), the built-in
starting designs plus the user's saved templates. **Create** makes an object (MeshRenderer) in the scene, writes
`<Assets>/<Name>.okaymesh` and the sidecar, selects it. When the selected object is a guided model the page shows the
parameter panel, the parts list and the part actions. Nothing is a new window; everything else (Edit Mesh page, Tab edit
mode, Modifiers, materials) is unchanged and works on the generated mesh because it is an ordinary mesh.

### 3. Data model: the recipe

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

### 4. The regeneration contract (never silently destroys edits)

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

### 5. Parts, user parts and user templates

* A **slot design** is `PolyMesh f(Frame, Recipe)`; a `Frame` is a box (origin at bottom centre, tangent, up, normal,
  size). Built-in designs and user parts both fill the frame, so any design can replace any other in a slot.
* **Save selection as part** (`.okaypart`): the selected faces of the mesh, normalised to a unit box, saved with a name
  and the slot it fits, in `<Assets>/GuidedTemplates/`. It then appears as a design `user:<id>` for that slot; choosing it
  embeds the part in the recipe.
* **Save as template** (`.okaytemplate`): the recipe with a name, shown among the starting designs of its template.
* Both are plain text and versioned (`okaypart 1`, `okaytemplate 1`), loadable on any machine.

### 6. Extension API

`guided::Registry::Register(Template)`; a `Template` is data (id, version, category, parameter definitions, slot
definitions with their designs, starting designs) plus one generate function. A new category adds a source file that
calls `Register` from `RegisterBuiltinTemplates()`; the core, the editor page and the tests iterate the registry.

### 7. Files (phase 1)

New: `engine/include/okay/Modeling/Guided.hpp`, `engine/src/Modeling/Guided.cpp` (recipe, registry, analysis, regeneration,
user parts, history), `engine/src/Modeling/GuidedFence.cpp` (fence, written from general parametric modelling: points
along a path, boxes and extruded profiles), `tests/test_guided.cpp`, `tools/guided_pictures.cpp` (headless renders),
`examples/modeling/`, a picture folder (pictures are no longer stored in the repository).
Changed: `engine/CMakeLists.txt`, `tests/CMakeLists.txt`, `editor/main.cpp` (a Guided page and its actions only),
`docs/editor/modeling.md`, `docs/try_it.md`, `docs/status/UNFINISHED.md`, feature map, `scripts/build-windows-dist.sh`.
Reused: `PolyMesh` (`ToMesh`, `FromMesh`, `Serialize`, `Parse`, `FaceNormal`, `Check`), `SaveMeshAsset`, `ed.PushUndo`,
`GetPrimitiveThumb` pattern, `SetMeshEditMode`, the Modeling window page list.

## Guided modeling: detail levels

*Was `docs/guided_detail.md` until 2026-11-20.*

Every guided template (fence, architecture, vehicles, characters) has a shared **Detail** setting: Low, Medium, High, Ultra. It is
stored in the recipe (`param detail 0..3`); a recipe without it (older files) is Medium, which is exactly the original shape.

| Level | What changes |
|---|---|
| Low | Curves use half the segments (round posts, wheels, columns, balls are blocky); everything else as Medium. Flat-shaded. |
| Medium | The standard shapes (the default, and what every older model is). |
| High | Twice the segments on curves; every box gets chamfered long edges; extra trim (wall cornices, wheel lug nuts); smooth shading in the editor. |
| Ultra | Four times the segments; box edges rounded (three steps per corner); a second ledge under cornices, eight lug nuts; smooth shading. |

Rules (tested in `tests/test_guided_detail.cpp`, ctest `guided_detail`): the same recipe always gives the same mesh at a level;
every part at every level is a closed, valid, finite solid with outward faces and no two parts share a corner; the triangle
count never goes down with the level and each step from Medium up adds at least 20 percent; switching level goes through the
normal rebuild, so part swaps stay, your own added geometry is copied through untouched and a part you edited by hand is asked about
(keep / discard / convert). Smooth shading (High and Ultra) is applied by the Guided page to the model's mesh, not stored in the recipe.

Known limit: a wall panel (one shell with holes) and the box-like parts cannot get fewer polygons at Low than a box has, so a
box-only design (for example the picket fence) has the same triangle count at Low and Medium.

#### Fine detail added in this round (Ultra triangles, before and after)

Cars at High and Ultra now carry purposeful small parts instead of subdivided arches: door handles and panel seams, window rubber and door pillar, wipers, grille bars, lamp lenses and tail-light ribs, mirror glass, number plate and frame, exhaust tips, underbody parts (gearbox tunnel, tank, silencer, propeller shaft), brake discs with calipers and ridges on the tyre walls. Buildings get rows of roof tiles on every slope, gutters and a down pipe. Humans get buttons, a placket, pockets and a hem band on the top. All of them are parts of their own (slot "Fine fittings" / "Tiles and gutters") that can be removed. Cars and roofs are no longer subdivided at Ultra (their outlines are already smooth or have hard edges that subdivision would melt).

| Model | Ultra before | Ultra after |
|---|---|---|
| car sedan | 32200 | 107272 |
| car sports | 45512 | 120584 |
| car police | 39864 | 114936 |
| building cottage | 34964 | 46732 |
| building townhouse | 65352 | 90176 |
| building barn | 38924 | 52996 |
| building shed | 25296 | 32412 |
| building flat_block | 113080 | 113908 |
| human average | 114976 | 119584 |
| human hero | 120832 | 122944 |
| roof gable | 912 | 12680 |
| roof hip | 912 | 23432 |

### Triangle counts (every start design of every template, regenerated with the picture tool's counting; Windows and Linux give the same numbers)

| Template | Start design | Low | Medium | High | Ultra |
|---|---|---|---|---|---|
| fence | picket | 768 | 768 | 928 | 17552 |
| fence | post_rail | 236 | 476 | 1020 | 26112 |
| fence | ranch | 240 | 240 | 560 | 15360 |
| fence | wrought_iron | 1070 | 2046 | 4542 | 108416 |
| fence | chain_link | 4328 | 4520 | 10472 | 39312 |
| fence | stockade | 672 | 672 | 944 | 20544 |
| fence | low_decorative | 1328 | 2452 | 5276 | 116592 |
| wall | plaster | 340 | 340 | 1164 | 6100 |
| wall | brick_door | 536 | 536 | 1732 | 17048 |
| wall | garden | 24 | 24 | 96 | 5568 |
| wall | barn | 800 | 800 | 2404 | 13520 |
| wall | blank | 12 | 12 | 236 | 3504 |
| stairs | straight | 384 | 384 | 864 | 23424 |
| stairs | open_slab | 816 | 816 | 1872 | 33792 |
| stairs | l_shaped | 540 | 540 | 1260 | 34560 |
| stairs | u_shaped | 732 | 732 | 1708 | 46848 |
| stairs | spiral | 712 | 864 | 1792 | 37440 |
| door | flat | 96 | 96 | 224 | 6144 |
| door | panelled | 256 | 336 | 1008 | 5496 |
| door | glazed | 120 | 120 | 280 | 7680 |
| door | barn | 204 | 204 | 476 | 7296 |
| door | double | 420 | 420 | 980 | 11328 |
| window | plain | 72 | 72 | 168 | 4608 |
| window | casement | 96 | 96 | 224 | 6144 |
| window | cross | 132 | 132 | 308 | 8448 |
| window | colonial | 240 | 240 | 560 | 10176 |
| window | tall_ornate | 756 | 756 | 1764 | 17280 |
| roof | gable | 20 | 20 | 1444 | 12680 |
| roof | hip | 20 | 20 | 2684 | 23432 |
| roof | shed | 24 | 24 | 1248 | 4776 |
| roof | flat | 60 | 60 | 140 | 3132 |
| building | cottage | 1024 | 1024 | 5248 | 46732 |
| building | townhouse | 3032 | 3032 | 14720 | 90176 |
| building | barn | 1816 | 1816 | 8252 | 52996 |
| building | shed | 432 | 432 | 2868 | 32412 |
| building | flat_block | 5128 | 5128 | 20136 | 113908 |
| car | sedan | 528 | 916 | 8928 | 107272 |
| car | hatchback | 612 | 1120 | 9612 | 111736 |
| car | coupe | 788 | 1160 | 9508 | 109240 |
| car | suv | 584 | 1012 | 9136 | 104184 |
| car | pickup | 552 | 884 | 8880 | 106504 |
| car | sports | 1008 | 1460 | 10432 | 120584 |
| car | van | 584 | 1012 | 9136 | 104184 |
| car | taxi | 656 | 1140 | 9408 | 111096 |
| car | police | 688 | 1212 | 9560 | 114936 |
| truck | box_truck | 656 | 1180 | 7836 | 37224 |
| truck | semi_cab | 652 | 1148 | 7796 | 49296 |
| truck | tanker | 892 | 1680 | 10680 | 55328 |
| truck | light_truck | 416 | 656 | 4960 | 26960 |
| truck | tipper | 804 | 1424 | 8384 | 51576 |
| truck | long_haul | 1500 | 2116 | 11780 | 58816 |
| bus | city | 584 | 920 | 5528 | 33136 |
| bus | coach | 644 | 1124 | 7980 | 49456 |
| bus | minibus | 464 | 760 | 5176 | 32336 |
| bus | school | 636 | 1108 | 6164 | 44528 |
| bus | shuttle | 500 | 836 | 5332 | 36224 |
| bus | long_city | 808 | 1352 | 8224 | 47640 |
| motorcycle | standard | 672 | 1636 | 7164 | 50976 |
| motorcycle | cruiser | 868 | 2052 | 8668 | 55832 |
| motorcycle | sport | 920 | 1888 | 7488 | 47136 |
| motorcycle | scooter | 580 | 1408 | 6272 | 39452 |
| motorcycle | dirt | 672 | 1456 | 6880 | 41952 |
| motorcycle | touring | 764 | 1880 | 8168 | 57280 |
| trailer | box | 228 | 396 | 2612 | 12392 |
| trailer | open | 340 | 596 | 4836 | 23648 |
| trailer | flatbed | 372 | 708 | 5044 | 19984 |
| trailer | tanker | 540 | 1084 | 7604 | 30648 |
| trailer | caravan | 232 | 368 | 2552 | 15184 |
| trailer | utility | 196 | 284 | 1964 | 14584 |
| human | average | 436 | 2756 | 11716 | 119584 |
| human | athletic | 418 | 2770 | 12354 | 124270 |
| human | heavy | 392 | 2628 | 11300 | 116512 |
| human | child | 400 | 2788 | 12548 | 125200 |
| human | tall_slim | 588 | 3312 | 13648 | 119662 |
| human | adventurer | 556 | 3196 | 13032 | 129590 |
| human | hero | 416 | 2892 | 12492 | 122944 |
| human | casual | 556 | 3112 | 13052 | 121566 |
| human | dress | 436 | 2636 | 11612 | 133104 |
| robot | standard | 312 | 312 | 680 | 18432 |
| robot | heavy_bot | 356 | 564 | 1476 | 34752 |
| robot | scout | 412 | 784 | 1916 | 39856 |
| robot | cyclops | 284 | 556 | 1444 | 34048 |
| robot | android | 280 | 536 | 1372 | 25840 |
| robot | rover | 384 | 536 | 1384 | 32448 |
| creature | dog | 332 | 752 | 2064 | 27548 |
| creature | horse | 272 | 660 | 2004 | 25564 |
| creature | bear | 276 | 716 | 2228 | 33948 |
| creature | stag | 368 | 848 | 2248 | 37788 |
| creature | eagle | 222 | 482 | 1442 | 17894 |
| creature | duck | 226 | 506 | 1490 | 19046 |
| creature | dragon | 380 | 856 | 2264 | 33212 |
| creature | wyvern | 368 | 836 | 2220 | 31676 |
| creature | beetle | 312 | 792 | 2356 | 31644 |
| creature | ant | 292 | 812 | 2532 | 31148 |
| creature | spider | 316 | 876 | 2536 | 36240 |
| tree | oak | 222 | 760 | 2766 | 18928 |
| tree | pine | 90 | 210 | 450 | 3200 |
| tree | spruce | 102 | 238 | 510 | 3712 |
| tree | birch | 272 | 1120 | 4436 | 23088 |
| tree | palm | 180 | 344 | 1028 | 4560 |
| tree | willow | 314 | 756 | 2550 | 13848 |
| tree | dead | 178 | 396 | 862 | 8192 |
| tree | apple | 304 | 1224 | 5028 | 25808 |
| tree | blossom | 376 | 1600 | 6732 | 29328 |
| rock | boulder | 20 | 100 | 440 | 1840 |
| rock | pile | 120 | 600 | 2640 | 11040 |
| rock | flat_stones | 60 | 60 | 140 | 3840 |
| rock | spikes | 30 | 40 | 90 | 3200 |
| rock | basalt | 168 | 420 | 924 | 6048 |
| rock | standing | 12 | 12 | 28 | 768 |
| rock | pebbles | 280 | 1400 | 6160 | 25760 |
| rock | mossy | 112 | 592 | 2656 | 23232 |
| rock | cliff_stack | 72 | 72 | 168 | 4608 |
| road | street | 108 | 108 | 252 | 6912 |
| road | country | 36 | 36 | 84 | 2304 |
| road | dirt_path | 12 | 12 | 28 | 768 |
| road | cobble | 2916 | 2916 | 6804 | 15996 |
| road | highway | 60 | 60 | 108 | 2688 |
| road | footpath | 252 | 252 | 588 | 4032 |
| road | railway | 432 | 432 | 1008 | 2160 |
| road | gravel_drive | 1236 | 3036 | 15084 | 67596 |
| bridge | footbridge | 684 | 684 | 1596 | 20160 |
| bridge | stone_arch | 72 | 92 | 180 | 4464 |
| bridge | rope_bridge | 968 | 1096 | 2536 | 13248 |
| bridge | suspension | 892 | 1132 | 2588 | 11224 |
| bridge | truss | 1176 | 1176 | 2744 | 5880 |
| bridge | log | 312 | 632 | 1368 | 7088 |
| hedge | box | 12 | 12 | 28 | 768 |
| hedge | rounded | 20 | 28 | 100 | 4416 |
| hedge | tall_screen | 12 | 12 | 28 | 768 |
| hedge | low_border | 20 | 28 | 100 | 4416 |
| hedge | domed | 20 | 28 | 100 | 4416 |
| hedge | topiary_balls | 60 | 204 | 892 | 4416 |
| hedge | topiary_cones | 38 | 70 | 190 | 7488 |
| plant | bush | 32 | 192 | 896 | 3840 |
| plant | flowers | 612 | 1512 | 6516 | 27216 |
| plant | sunflowers | 540 | 1340 | 6120 | 26120 |
| plant | ferns | 144 | 216 | 1080 | 18432 |
| plant | cactus | 44 | 108 | 236 | 6144 |
| plant | mushrooms | 110 | 440 | 1700 | 6620 |
| plant | reeds | 432 | 432 | 1008 | 6912 |
| plant | grass | 192 | 192 | 1056 | 18432 |
| plant | potted | 52 | 236 | 988 | 6144 |
| plant | planter | 320 | 720 | 3008 | 15168 |
| streetlamp | victorian | 118 | 230 | 486 | 6656 |
| streetlamp | modern | 64 | 120 | 264 | 6912 |
| streetlamp | bollard | 48 | 148 | 544 | 4528 |
| streetlamp | double_arm | 116 | 324 | 1076 | 10492 |
| streetlamp | lantern_post | 54 | 54 | 118 | 3200 |
| streetlamp | rural | 72 | 186 | 590 | 6590 |
| furniture | chair | 144 | 144 | 336 | 9216 |
| furniture | table | 380 | 780 | 1660 | 13440 |
| furniture | round_table | 132 | 260 | 548 | 13824 |
| furniture | armchair | 168 | 240 | 640 | 6336 |
| furniture | sofa | 136 | 288 | 848 | 12576 |
| furniture | bed | 276 | 276 | 644 | 7872 |
| furniture | bookshelf | 108 | 108 | 252 | 6912 |
| furniture | desk | 160 | 160 | 336 | 8832 |
| furniture | wardrobe | 108 | 108 | 252 | 6912 |
| container | crate | 48 | 48 | 112 | 3072 |
| container | barrel | 168 | 364 | 756 | 10752 |
| container | chest | 76 | 76 | 156 | 4080 |
| container | bucket | 36 | 72 | 144 | 3648 |
| container | jar | 132 | 332 | 876 | 4800 |
| container | sack | 48 | 144 | 584 | 3744 |
| tool | hammer | 48 | 108 | 356 | 7424 |
| tool | axe | 96 | 192 | 400 | 3264 |
| tool | pickaxe | 40 | 40 | 72 | 1824 |
| tool | shovel | 44 | 76 | 140 | 3360 |
| tool | wrench | 116 | 252 | 668 | 8576 |
| tool | sword | 108 | 244 | 628 | 7280 |
| decoration | signpost | 48 | 48 | 112 | 3072 |
| decoration | arrow_sign | 36 | 36 | 52 | 1104 |
| decoration | flagpole | 60 | 148 | 420 | 8624 |
| decoration | statue | 136 | 340 | 1100 | 6592 |
| decoration | garden_lantern | 42 | 42 | 90 | 2432 |
| decoration | fountain | 184 | 396 | 820 | 7392 |

## Guided modeling: why it lagged, what was measured, what changed

*Was `docs/guided_perf.md` until 2026-11-20.*

Written for the change "Guided: stop the lag" (branch `claude/guided-perf-1`). All numbers are from a loaded 4-core Linux machine (other builds were running), release build,
`OKAY_GUIDED_PERF=1 ./test_guided_perf`. Treat them as relative, not absolute. Nothing here was measured inside the editor window.

### What the owner saw

Moving a slider, picking a style, or changing Detail on a guided model made the editor stall, worst at High and Ultra, and the panel cost time even when idle.

### The causes (found by measuring, in order of size)

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

### What changed

* `Generate` caches the parts by recipe text (one cache; `Create`, the analysis, the regenerate and the check share it). `Registry::Register` clears it.
* `IsUntouched`, `RegenerateUntouched`, `UntouchedAnalysis`: a model with no hand edits is rebuilt without reading the mesh back or matching faces, and its analysis afterwards is known without running it.
* Analysis and read-back use hashed lookups; `Serialize` of a mesh writes through one buffer; `CoincidentReps` and the smooth-shading corner table are hashed / indexed.
* The panel: the mesh is fingerprinted at most four times a second and never while a control is held; the triangle count comes from the mesh on screen; sliders and colours drag a Medium-detail preview
  (`PreviewDetail`, `PreviewThrottle`: never more than a quarter of the time spent rebuilding) and the real model is built once on release (one undo step, one save); a time line shows the cost of the last change.
* Undo states of an untouched model keep only the recipe (`GuidedState::fromRecipe`).

### Numbers

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

### Tests

`tests/test_guided_perf.cpp` (`ctest -R guided_perf`). Each fix has a check that failed on the old code (measured before the change: 27 combinations read as edited; wiper blades squeezed; five builds per release):
untouched after smoothing (every template, Low to Ultra), smoothing keeps positions, save and reopen stays untouched, builds are cached, the fast path equals `Regenerate`, no loose vertices,
removed and edited parts survive a rebuild at High and Ultra, undo states from the recipe, the preview throttle, and the timing table with generous budgets.

## Guided generator quality audit

*Was `docs/generator_quality_audit.md` until 2026-11-20.*

Why the first guided models looked alike, what was measured, what was changed, and what is still weak.
Measured with `tests/test_guided_quality.cpp` (`guided_quality`, which also runs as `guided_roundtrip`). Written from our own
measurements; no outside code or models were used.

### How a "shape difference" is measured

The side view of each car body style is filled in (176 x 60 grid, same length, height, width, wheelbase and clearance for every
style, no spoiler or roof fitting). The overlap of two outlines is their intersection over union (IoU). 1.000 means the same
outline, lower means more different.

| | Before the rework | After the rework |
|---|---|---|
| Mean pairwise IoU (all style pairs) | 0.835 | 0.800 |
| Worst pair | sedan / taxi 1.000 (same body) | 0.920 (a style pair, under the 0.93 limit) |
| Coupe / sports | 0.950 | below 0.93 |
| SUV / van | 0.936 | below 0.93 |

The taxi and the patrol car are a sedan body with other fittings on purpose, so they are not counted as separate styles.
The test fails if any pair of the seven body styles is above 0.93 or the mean is above 0.84.

### Findings (cars)

1. One profile formula for every body: only a few numbers moved a shared trapezoid. Fixed: each style has its own table of
   hood, windscreen, roof, rear window and trunk positions (`CarStyle` in `GuidedVehicles.cpp`), smoothed with corner cutting
   so the roofline is a curve at Medium and above.
2. The body was a box with the cabin on top. Fixed: the body is a side-view outline pushed across the width.
3. Wheels sat beside the body and floated. Fixed: round arches are cut into the body, each wheel is centred on its axle and
   stands on the ground, and an axle part joins the pair. Tests: `guided_quality` "arches" checks.
4. Small parts floated (mirror arms stopped short of the cabin, a chrome bumper started beyond the nose). Found by the new
   validator (parts connected by touching bounding boxes) and fixed. Every car start design is now one connected object.
5. Extreme slider values folded the body (tall wheels on a short car, cabin below the belt line). Fixed by limiting the
   wheel size to the length, keeping the arches apart and inside the body, and keeping a minimum cabin height.

### Validator and variation

`Validate()` checks each part for bad numbers, bad indices, degenerate faces, open or non-manifold edges and inward faces,
and counts connected groups. `Randomise()` moves the numbers inside a style's range and may change designs, then applies the
template's compatibility rules (for example no wing on a pickup, SUV or van). Result: 1000 seeds for each of the 15 templates
(15,000 models) with 0 validation failures; the same seed always gives the same model; 25 or more distinct models in the
first 30 seeds.

### Speed (standalone build, -O1, one thread, this machine; first start design of each template)

Generation takes under 1 ms at Low and Medium for most templates. At Ultra (Catmull-Clark subdivision) the worst cases
are the human at 29 ms (115k triangles), the car at 10 ms (42k) and the building at 8 ms (35k). The test only guards a
loose limit (5 s) so it is not flaky.

### Still weak (honest list)

- Motorcycles still use the old rough generator (next in the order).
- Characters: organic body exists from Medium up; styles, proportion sliders and clothing parts are not finished.
- The silhouette limit is a floor, not proof that every style looks good. Pictures: a picture folder (pictures are no longer stored in the repository)
  and `cars_three_quarter.png` (headless software renders).
- Parts that touch are tested by bounding boxes, not by exact surfaces.
- Mesh and draw-call counts, caching by parameter hash and instancing are not done yet.
