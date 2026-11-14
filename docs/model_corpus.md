# Real-file validation of the own model readers

Until this page the own readers had only been run on files we wrote ourselves. This page records what happened when they were run on files that real exporters wrote: first FBX, COLLADA, 3DS and BVH, then (pre-flip verification) glTF/GLB, OBJ+MTL, STL and PLY against Assimp, the engine-linked reader tests under sanitizers and valgrind, a Windows (MinGW under wine) run, and FBX materials against ufbx. The last section says which of the roadmap's replacement criteria that does and does not meet.

## The corpus

The files are the `test/models` and `test/models-nonbsd` directories of an Assimp **v5.4.3** source checkout (the version `OKAY_USE_ASSIMP` fetches). They were obtained with

```
git clone --depth 1 --branch v5.4.3 https://github.com/assimp/assimp <scratch dir>
```

**None of the files are copied into this repository.** The `models` tree is under the library's BSD licence; the `models-nonbsd` tree carries a mix of licences. Instead `tests/data/model_corpus_manifest.tsv` lists every file by its path in that checkout together with the result each reader gave, and `tests/test_model_corpus.cpp` re-reads them.

| Format | Files in the corpus | Notes |
|---|---|---|
| FBX | 43 | binary and ASCII, FBX 7.1 to 7.5, skinned, animated, embedded textures, several takes |
| COLLADA | 26 | 25 in `Collada/` and one in `invalid/`; geometry, skins, animation, instancing, cameras and lights only, three encodings |
| 3DS | 16 | static and keyframed, camera animation, textured, one empty file |
| BVH | 3 | motion capture, 2,752 to 4,511 frames |
| glTF / GLB | 54 (48 `.gltf`, 6 `.glb`) | glTF 2.0 samples, embedded and external buffers, skins, morph targets, texture transform, primitive modes, extensions, and about 15 deliberately broken files; 7 are glTF 1.0 (6 `.gltf` and one `.glb`) |
| OBJ | 28 | `models/OBJ` (23), `models-nonbsd/OBJ` (2), `models/invalid` (3): groups, `usemtl`, concave polygons, point and line elements, vertex colours, UTF-16 |
| STL | 8 | ascii and binary, several solids, an empty solid |
| PLY | 12 | ascii, binary little and big endian, point clouds, a truncated file, odd headers |
| OFF | 5 | `models/OFF` (3) and `models/invalid` (2): a cube, a 3,205-vertex figure, an invalid one, an empty file, one that claims 353 billion vertices (readers batch A) |
| SMD | 4 | `models/SMD` (3: a one-triangle file, a 3,732-triangle figure with one bone, a 896-triangle prop) and `models/invalid` (1 empty file) |
| MD5 | 5 | `.md5mesh` 4 (`SimpleCube`, `Bob`, `BoarMan`, one empty file) and `.md5anim` 1 (`Bob`, 140 frames at 24 per second, 33 joints) |
| MS3D | 4 | `Wuson` (3,732 triangles), `jeep1` (7 groups, 2,032 triangles), `twospheres` and `twospheres_withmats`; none has joints |
| ASE | 17 | `models/ASE` (14: static scenes, camera and object animation, a 26-object motion capture rig, UV transform tests, UTF-8 and UTF-16), `models-nonbsd/ASE` (2 rifles) and one empty file |
| Blender | 28 | `models/BLEND` (26) and `models-nonbsd/BLEND` (2): Blender 2.41 to 2.93, 32 and 64 bit, little and big endian, plain and gzip |
| 3MF | 1 | one box, a deflate-compressed package |
| AMF | 11 | hand-made files: pyramids with materials, colours, volumes and constellations |
| X3D | 4 | a keyboard (Extrusion), a sphere with text, a line set, a car model with 17 shapes |
| DXF | 5 | 3DFACE meshes, polyface and polyline meshes in blocks, a line test |
| LWO | 54 | LWO2 and LWOB: mapping modes, shaders, layers, concave and non-planar polygons, one empty file |

## Running it

```
OKAY_MODEL_CORPUS=<checkout>/test ctest -R "model_corpus|fbx_reader"
```

- `model_corpus` reads every manifest row with the reader named in it. A file recorded as read must still read with the same mesh, triangle, bone, skinned-mesh, clip and material counts; a file recorded as failing must still fail (a fix therefore shows up as a manifest change).
- `fbx_reader` (`tests/test_fbx_reader.cpp`) additionally imports every real `.fbx` through ufbx and through our reader and compares them the way it does for our own fixtures: object, mesh, joint, vertex, triangle and clip counts, names, bounding box, surface area, skin weights, inverse binds, clip samples.
- Without `OKAY_MODEL_CORPUS` both tests run only on our fixtures and `model_corpus` prints "skipped".
- `test_model_corpus --one|--list|--fbxdump|--fbxdiff|--fbxclips|--keys ...` print what the readers found, for comparing with another importer. `--list` takes the readers `fbx-own`, `dae`, `3ds`, `bvh`, `obj`, `stl`, `ply` and `gltf` (`.gltf` and `.glb`); the batch-B readers `3mf`, `amf`, `x3d`, `dxf` and `lwo` take the same arguments. For `obj`, `stl`, `ply` and `gltf` a row also carries `tex` (distinct texture files over all materials), `nodes`, `uv` and `nrm` (1 when some mesh has per-vertex UVs or normals); glTF sizes are the file's own (the importer's auto-scale of very large or tiny models is undone).
- The Assimp side of the glTF/OBJ/STL/PLY comparison: `scripts/assimp_corpus_dump.cpp` prints Assimp's view of a file in the same TSV shape (meshes, triangles, area, bounding box, materials, distinct textures, nodes, animations and their length, UV and normal presence; only `aiProcess_Triangulate` is applied), and `scripts/compare_model_corpus.py ours.tsv assimp.tsv` lists the differences. It does not print two differences that are by design: Assimp adds a default material to a file whose meshes have none (its material count is one higher), and our readers generate normals a file lacks (Assimp leaves them absent). Build Assimp with only the importers needed: `cmake -DASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT=OFF -DASSIMP_BUILD_OBJ_IMPORTER=ON -DASSIMP_BUILD_STL_IMPORTER=ON -DASSIMP_BUILD_PLY_IMPORTER=ON -DASSIMP_BUILD_GLTF_IMPORTER=ON -DASSIMP_NO_EXPORT=ON` on the same v5.4.3 checkout.

## Results

### FBX (own reader vs ufbx)

42 of 43 files read by our reader; the 43rd is a damaged file (every NUL byte in its header was replaced by a space) that ufbx and Assimp reject too. On the 42, **ufbx and ours agree** on object, mesh, skinned-mesh, joint, vertex (within 0.5 %, see below), triangle, blend-shape and clip counts and names; on bounding boxes and surface area (within 0.2 %); on skin weights and inverse binds of the two skinned files; and on clip samples (within 0.015 in a quaternion component, on two files; 0.002 elsewhere).

Bugs the real files found, all fixed with a regression test (fixtures `takes_bin.fbx`, `bridge_bin.fbx` in `tests/assets/fbx`, written by `scripts/gen_fbx_fixtures.py`):

1. **Byte-array properties (`c`)** that newer SDKs write (thumbnails) were rejected as "unknown property type": two files did not load at all.
2. **Takes sharing a timeline.** A file with several takes keeps one set of curves and gives each take a time span (`LocalStart` / `LocalStop`); we baked every take over the whole curves, so all takes played the same motion. Each take now plays its own span, starting at 0.
3. **Stored slopes of auto keys.** The slopes a file stores for auto / TCB keys were ignored in favour of ones derived from the neighbours, which overshot on keys next to a spike; the stored slopes are used when present.
4. **Bridged polygons** (a polygon with a hole written as one ring with repeated corners): ear clipping treated the repeated corners as being inside every ear and fell back to a fan over the hole. The area of one file came out 13 times too large.
5. **Undefined behaviour on hostile numbers**: converting a huge double from `GlobalSettings`, a rotation order, key flags or an object id to an integer was undefined (found by running the existing hostile test with `-fsanitize=float-cast-overflow`, which the earlier run did not use). Every such conversion is now clamped.

Known differences from ufbx that remain (none changes a count the tests compare, all are recorded in `docs/own_fbx_reader.md`): ufbx keeps a few more exactly duplicated vertices (up to 0.1 %); a take whose curves animate only custom properties gets a constant clip from ufbx and none from us (two files); ufbx keeps keys beyond a take's end in its clip, ours cuts at the span; a take with only held poses is as long as its span for us and zero or span for ufbx depending on the file; quads of non-planar faces can be split along the other diagonal (surface area differs by up to 0.05 % on a duck model); bezier segments with user tangent weights differ from ufbx by up to 0.009 in a quaternion component.

Not verified on real files: left-handed source axes, `InheritType` other than the default, FBX 6.x, NURBS and other non-mesh geometry, PBR material property sets (the two PBR files in the corpus read and match on geometry and counts, but their material values were not compared), dual-quaternion skinning.

**Materials, compared with ufbx (pre-flip verification).** `fbx_reader` now compares, for every fixture and every real file, what both paths put on each mesh object: colour, emission, metalness, glossiness (shininess, specular, reflectivity), the texture file and the normal-map file. All 42 real files and all 12 fixtures agree, with the documented exceptions below. Three things came out of it:

1. **3ds Max PBR materials were not read.** Two real files (`maxPbrMaterial_metalRough.fbx`, `maxPbrMaterial_specGloss.fbx`) carry 3ds Max's PBR property sets under `3dsMax|main|...`. ufbx recognises such sets by the tool's class id; ours only read the classic Lambert/Phong properties, so metalness and roughness were lost. Ours now reads five of the sets ufbx knows (only the two 3ds Max PBR ones were checked on real files; Physical, glTF and OpenPBR only on the generated fixture): 3ds Max Physical Material, 3ds Max PBR metal/rough and spec/gloss, the 3ds Max glTF material and OpenPBR (base colour, roughness or glossiness, metalness, emission colour and factor, the normal map and the ambient-occlusion map), with the same rule ufbx uses (a material that says lambert or phong keeps the classic reading). The real files showed what 3ds Max actually writes, which is not what the property tables say: the colour is `basecolor`, its texture is connected to `base_color_map`, other textures to `<name>_map`, and glossiness is only glossiness when `useGlossiness` is about 1; ours matches names the way the files write them.
2. **A texture's picture can sit in a Video that is not the texture's.** `box_orphant_embedded_texture.fbx`: the Video connected to the texture has the file name but no bytes, another Video with the same file name has them. ufbx finds them; ours now does too (match by file name).
3. Fixtures `pbr_bin.fbx` (one triangle per property set, class ids written as signed 32-bit integers as the SDK does, plus a phong material that carries a class id) and `orphan_video_bin.fbx`.

Known differences kept (in the tests as exceptions): on the two real 3ds Max files and on `pbr_bin.fbx` the base colour and the glossiness are bound to textures whose picture files are not in the corpus; ufbx reports no value for a textured slot, so its importer keeps white and no gloss, while ours reads the value stored beside the texture and uses it while the picture is missing. Not read by either path: UV set names (both read the first UV set), PBR slots the engine has no field for (coat, sheen, transmission, subsurface, anisotropy, opacity), textures for emission, metalness, roughness and ambient occlusion (the importer sets only the colour and normal-map files), OSL / Arnold standard-surface and ShaderFX graph materials (they need the `Shader` / `Implementation` objects; the reader does not follow them).

### COLLADA (own reader vs Assimp)

26 of 26 files read. Two of them (`cameras.dae`, `lights.dae`) contain only cameras, lights and their nodes: our reader used to fail them ("the scene has no mesh"); it now reads them as an empty mesh set with the node hierarchy and a note, and `BuildSceneFromModel` builds the nodes (a scene with no node at all is still refused). Assimp "reads" them too, but only because it builds a stick-figure mesh from the nodes when a COLLADA file has no mesh (36 and 60 triangles); ours deliberately makes no such mesh. Assimp rejects one `invalid/` file (`box_nested_animation_4286.dae`, "Not enough data for accessor") that ours reads.

Compared with Assimp built from source on the 24 (mesh and triangle counts, world bounding box, surface area, clip count, animation duration, material count): all match except the differences listed here.

Bugs the real files found, fixed with regression tests in `tests/test_collada_reader.cpp`:

1. **Node chains deeper than 64** (a real file nests 65 boxes) failed with "hierarchy too deep"; the cap is 256 now (XML depth 512).
2. **Decimal commas.** Some files were written in a locale with a decimal comma (`21,920870`) in the vertex arrays and in `<unit meter="0,010000">`; they failed, or read at 100 times the size. A token with a comma and no point is read with the comma as the point (and the model's notes say so); `1,2,3` and `1.5,2` stay errors.
3. **UTF-16** files (little and big endian, with or without a byte-order mark) failed with an XML error; they are converted to UTF-8 first.
4. **Triangle strips and fans** were skipped (the manifest file read as an empty scene); they are read, with the strip's alternating winding.
5. **Concave polygons.** Polygons were fanned from their first corner, which covers the notch of a concave polygon; the surface area of the real concave test file was 13 times too large. Convex polygons still fan (same triangles as before); concave ones are ear clipped, bridges and coincident corners included, and zero-area slivers are dropped.
6. **Named takes** (`<library_animation_clips>`): all animations were merged into one clip; each `<animation_clip>` is now a clip, cut to its `start` / `end` and moved to start at 0.

Differences that remain: Assimp merges identical meshes (`meshes` / `tris` differ on an instanced-node file, the geometry and bounding box are equal); Assimp adds a default material to files without one (`materials` is one higher); on a file with a degenerate concave polygon Assimp drops two more slivers (58 against 64 triangles; surface area 0.234 against ours 0.245, the exact polygon area is 0.2455); on one skinned file the bone count differs by one (Assimp 4, ours 5): investigated, ours is right. `library_animation_clips.dae` has five joints in its skin and the weights use joints 0 to 3 only; joint 4 has no weight, Assimp drops a bone that influences no vertex, ours keeps all five joints the skin declares (the joint indices stay aligned with the file and the joint still moves its child nodes). Nothing to fix.

### 3DS (own reader vs Assimp)

See `docs/own_3ds_reader.md`: 15 of 16 read (the 16th is empty). Mesh count, triangle count, bounding box and surface area match Assimp on all 15; two files with a mesh matrix and animated rotation settled the row-vector convention and the clockwise rotation, and the last animation key of the rotating cube equals Assimp's to four decimals. The camera-only animation files give no clip for us.

### BVH (own reader vs Assimp)

See `docs/own_bvh_reader.md`: 3 of 3 read; first and last position and rotation keys equal to four decimals.

### Blender (own reader vs Assimp)

See `docs/own_blend_reader.md`: 28 of 28 read (Assimp reads 27; it fails `BlenderDefault_262.blend`).

### glTF / GLB, OBJ + MTL, STL, PLY (own readers vs Assimp)

These four readers are always built, and today the shipped Windows distribution reads STL and PLY through Assimp first (`OKAY_PREFER_OWN_READERS` is off), so these are the readers whose behaviour changes for those users when Assimp goes. All 102 files of the four formats under `test/models` and `test/models-nonbsd` were read by ours and by Assimp 5.4.3 and compared on mesh, triangle and material counts, distinct textures, UV and normal presence, world bounding box, surface area, node count, animation count and length.

| Format | Files | Both read | Agree on everything compared | Only Assimp reads | Only ours reads | Neither |
|---|---|---|---|---|---|---|
| STL | 8 | 8 | 8 | 0 | 0 | 0 |
| PLY | 12 | 9 | 9 | 1 | 0 | 2 |
| OBJ | 28 | 25 | 15 | 1 | 0 | 2 |
| glTF / GLB | 54 | 34 | 33 | 6 | 6 | 8 |

The PLY, OBJ and glTF rows above were updated by hand after the gap-closing change, not by re-running `compare_model_corpus.py` (the Assimp build used for these formats was not kept): the newly read files were checked against the Assimp numbers recorded earlier (meshes, triangles, box, and for PLY the vertex counts); for OBJ only `point_cloud.obj` agrees on everything, `testline.obj` and `testpoints.obj` differ in vertex count and material count (see below). For glTF, `IncorrectVertexArrays/Cube.gltf` now agrees (8 meshes) and the nine newly read files match, so 33 of 34 agree (the one that does not is `AnimatedMorphCube.gltf`, whose morph animation Assimp does not report).

What the differences are (each is in the manifest as the row's recorded result):

- **By design, not listed above as differences:** Assimp's extra default material; normals we generate when the file has none; node counts (ours adds one wrapper root, and a glTF mesh with several primitives becomes one child object per primitive); our importer scales a very large or tiny model to about two units (the corpus tool undoes that for glTF).
- **OBJ.** Part counts differ on files with groups (`regr01.obj` 12 parts against Assimp's 55 meshes, `spider.obj` 4 against 19, `segment.obj`, `cube_usemtl.obj`, `regr_3429812.obj`): ours makes one part per material, Assimp one mesh per group and material; triangles, area, bounding box, materials and textures are equal. `box_longline.obj` has one face of several hundred repeated corners: ours fans all of them (944 triangles), Assimp gives 634. The two concave-polygon files: ours 64 triangles, area 0.2455 (the exact polygon area); Assimp drops two zero-area slivers (58 triangles, area 0.2339). Not read by ours: OBJ files with no faces, only points or lines (`point_cloud.obj`, `testpoints.obj`, `testline.obj`: "no faces"), and a UTF-16 file (`box_UTF16BE.obj`: Assimp returns an empty scene for it, ours reports no faces).
- **PLY.** The two point clouds (`points.ply`, `pond.0.ply`) and `issue623.ply` (a list property written into the vertex element, no face element; its data lines carry no list count, which Assimp reads as an empty list and ours now does too) used to fail ("no faces", "PLY list too long"); they are read now as one mesh with the vertices and no triangles, as Assimp does (1 mesh, 0 triangles, the same vertex counts 4, 70,051 and 24 and the same boxes as Assimp's earlier numbers for these files). Still not read by ours: `ant-half.ply`, whose header promises 912 faces and whose body holds 259: ours rejects the truncated file; Assimp reports 912 faces for it although the body holds 259, so its numbers are not a reference.
- **glTF.** Not read by ours: the seven glTF **1.0** files (a different layout, refused with a message saying so; Assimp reads the six `.gltf`, not the `.glb`). The eight files whose only geometry is points or lines (`Mesh_PrimitiveMode_00..03` and `07..10`) and `SceneWithoutNodes.gltf` (no node, no mesh) used to fail; they import now (a points or lines primitive keeps its vertices and makes no triangle, as Assimp's mesh with no faces; a file with a scene but no mesh is an empty scene; one with no scene at all, `NoScene.gltf`, and one whose triangles are all out of range, `AllIndicesOutOfRange.gltf`, still fail, as in Assimp). For the eight, meshes, triangles and bounding box equal the Assimp numbers from the earlier run. Ours reads six files Assimp rejects on strict schema checks (five `wrongTypes/*.gltf` and `issue_3269/texcoord_crash.gltf`). `IncorrectVertexArrays/Cube.gltf`: now 8 meshes and the larger bounding box, as Assimp (it used to leave the line meshes out). `AnimatedMorphCube.gltf`: ours plays the morph-weight animation (4.2 s clip), Assimp reports none. Both fail on the Draco file, the missing `.bin`, the file with all indices out of range and the recursive node file.

Bugs the real files found, all fixed with a regression test (`tests/test_model_readers.cpp`, section "Found by the real-file corpus"):

1. **OBJ and PLY fanned concave polygons** from their first corner, which covers the notch (the concave L-shaped test polygon came out with area 3.22 against the true 0.25 on the real file). They are now ear clipped like COLLADA and FBX (convex polygons still fan, same triangles as before; the code is shared, `engine/src/Render/PolygonTriangulate.hpp`).
2. **PLY rejected a valid file with a free-text header line.** A Blender 2.4x exporter wrote `Created by Blender3D ...` without the `comment` keyword; the whole file (`Wuson.ply`) failed with "unknown PLY header line". Unknown header lines are now ignored, as `PlyFileDeclaresDuplicateVertex` already did.
3. **glTF ignored the primitive `mode`.** Triangle strips and fans were read as plain triangle lists (wrong triangles), lines and points were read as triangles (`IncorrectVertexArrays/Cube.gltf` had twice the triangles, a points primitive produced 341 triangles). Strips and fans are now expanded (alternating strip winding, degenerate triangles dropped), points and lines are left out, and trailing indices that do not make a whole triangle are dropped.
4. **A glTF triangle with an out-of-range index rejected the whole primitive**; only that triangle is dropped now (Assimp does the same).
5. **A glTF with unreadable meshes imported as an empty scene with `ok = true`** (missing `.bin`, Draco or meshopt compression). It now fails with a one-line error, as Assimp does; glTF 1.0 gets its own message. (Files whose only geometry is points or lines were failed here too until the gap-closing change below; they import now.)

### 3MF, AMF, X3D, DXF and LWO (own readers vs Assimp)

The batch-B readers were run on every file of their formats under `test/models` and `test/models-nonbsd` of the same Assimp v5.4.3 checkout and compared with Assimp built from those sources (`scripts/assimp_corpus_dump.cpp` with only the 3MF, AMF, X3D, DXF and LWO importers, `scripts/compare_model_corpus.py`; the rows are in the manifest). Assimp's test files for these formats are few and small: the only real-world evidence there is. Everything the files do not exercise is tested on synthetic fixtures built in memory and labelled as such in each reader's page. The standalone hostile test (`batchb_hostile`) was also run with 150,000 mutations per reader under `-fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all` (`OKAY_BATCHB_FUZZ_ITERS=150000`): 924,301 checks, no failure; the engine-linked reader tests (`3mf_reader` ... `lwo_reader`) and `model_corpus` (all 265 manifest rows) were run without sanitizers.

#### 3MF

One real file (`models/3MF/box.3mf`, a 10 x 20 x 30 millimetre box in a deflate-compressed package). Ours reads it and equals Assimp 5.4.3 on meshes, triangles, surface area and bounding box (Assimp's box turned the same Z-up way: `compare_model_corpus.py` does it for `.3mf` and `.amf`). Assimp keeps 8 shared vertices, ours has 24 (the 90 degree edges stay sharp). Details and what is **not** verified (no real file with colours, textures or extensions exists in the tree) in `docs/own_3mf_reader.md`.

#### AMF

11 files, all read by ours and by Assimp. 5 agree on everything compared. 6 differ and each was checked in the file: two have a volume colour and one triangle with its own colour (ours one part and material per colour, Assimp one mesh and material), two have an object with two volumes (Assimp returns only the first volume's triangles, half the area), two have constellations (ours places each instance with its delta and rotation; Assimp's box is ten times wider, with the rotation value used as the offset). `docs/own_amf_reader.md`.

#### X3D

4 files. Ours reads 3; `IndexedLineSet.x3d` has only lines and fails with a message (Assimp returns a mesh with 0 triangles). On the 3 that read, Assimp's numbers differ from ours and each difference was checked against the file with an independent count: `ChevyTahoe.x3d` ours 1298 triangles (the file's index lists give 1298; Assimp drops one of the four `USE`d wheels, 1234), `ComputerKeyboard.x3d` ours 100 shapes and 2020 triangles (independent count: the same; Assimp returns 4 meshes and 100 triangles, the `USE`d keys are not instanced), `HelloX3dTrademark.x3d` a sphere tessellated 24 x 12 by ours (528 triangles) and finer by Assimp (1280). `docs/own_x3d_reader.md`.

#### DXF

5 files, all read by ours and by Assimp; 4 agree on everything compared, the fifth (`PinkEggFromLW.dxf`) has the same area and box and 48 fewer triangles in ours (zero-area pole triangles, dropped). Assimp turns every DXF Z-up to Y-up as `(x, y, z) -> (x, z, -y)`; ours does the same and the boxes are equal on all five. `docs/own_dxf_reader.md`.

#### LWO

54 files (LWO2, 5 LWOB, one empty). Ours reads 53, as Assimp does; both reject the empty one. 29 agreed on everything compared and 25 differed: 16 only in `tex` (planar, cylindrical and spherical image maps, which Assimp turns into UVs and ours did not apply), `UglyVertexColors.lwo` in `tex` and 22 zero-area triangles, and 8 in triangle counts (concave, non-planar and self-intersecting polygons, zero-area triangles that ours drops, polygons with repeated indices). Bounding boxes equal Assimp's on all 53 files after the reader negates Z (LightWave is left-handed). `docs/own_lwo_reader.md`.

**Gap closing (planar, cylindrical and spherical image maps).** Those 17 files now list their texture and get UVs: the surface's image map is no longer dropped when its projection is not UV; UVs are generated from the surface's bounding box the way Assimp's `GenUVCoords` step does it (`docs/own_lwo_reader.md`). The manifest rows changed from `tex=0 uv=0` to `tex=1 uv=1` (and a few vertex counts by 2 where a seam splits a vertex), so the 16 `tex` differences are gone and what is left is the 9 triangle-count rows. **Unverified:** the generated UV values were checked by hand-computed fixtures (`tests/test_lwo_reader.cpp`, suite "projected image maps") and by sanity (all finite, 0 to 1 plus the wrap amount), not against Assimp's numbers, because the Assimp comparison tool applies only `aiProcess_Triangulate` and the Assimp build used here no longer had the LWO importer; the map's own centre, size and rotation are not applied (Assimp does not either); cubic and front projection keep the texture and make no UVs.

### Sanitizers and valgrind (engine-linked tests)

The engine-linked reader tests were never run under sanitizers before this step. Built from the whole engine with `-DOKAY_SANITIZE=address,undefined,float-cast-overflow` (GCC 13, `-O1 -g1`, `-fno-sanitize-recover=all`, so any finding aborts; the full `okay_engine` library and each test were compiled instrumented, no hand-built subset): `fbx_reader` (fixtures, and with the corpus: 42 real files through ufbx and ours, 2514 checks), `fbx_import_limits`, `collada_reader`, `3ds_reader`, `bvh_reader`, `model_readers` (including the glTF mode fuzz, the locale and the UTF-8 path tests), `model_readers_fuzz` (33,151 mutated inputs), `gltf_import`, `loaders_robust` and `model_corpus` (all 190 manifest rows). No finding. The same binaries built without sanitizers (`-O1 -g`) ran under `valgrind --leak-check=full` (3.22.0, definite and indirect leaks count as errors): `3ds_reader`, `bvh_reader`, `collada_reader`, `model_readers`, `gltf_import`, `loaders_robust`, `fbx_reader` with and without the corpus, and `model_corpus` on all 190 files: no error, no leak.

### Windows (MinGW-w64 under wine)

The engine and the reader tests were cross-compiled with MinGW-w64 (GCC 13, `cmake/mingw-w64-x86_64.toolchain.cmake`) and run under wine 9.0 (`wine64`, a fresh prefix): `model_readers`, `collada_reader`, `3ds_reader`, `bvh_reader`, `fbx_reader` (with the corpus), `gltf_import`, `model_readers_fuzz` and `model_corpus` pass, and the corpus gives **the same result on all 190 rows as on Linux**. Two Windows-only defects were found and fixed:

1. **Every reader failed to open a file in a folder or with a file name that is not in the ANSI code page** ("Müller", a Japanese name): `fopen` / `ifstream` take the narrow path through the ANSI code page. Files are now opened by UTF-8 path (`_wfopen` / `std::filesystem::u8path`), and the texture helpers use `u8path` / `u8string`. `model_readers` copies the fixtures under such names and reads each format.
2. **Locale.** The readers called `strtod` / `strtof`, which follow the process locale. After `setlocale()` in a German or French locale (a host application can do that) a `1.5` read as `1`, or the file failed. Found with a decimal-comma locale on Linux (`localedef`) and confirmed on wine's `de_DE`; the test failed on five of six files before the change. All text readers (OBJ and MTL, ASCII STL, ASCII PLY, BVH, COLLADA, ASCII FBX) now read the point whatever `LC_NUMERIC` says (`engine/src/Render/NumParse.hpp`).

What this run is not: wine is not Windows (its C runtime and file layer are its own), nothing was built with MSVC, and macOS was not touched.

### Readers batch A: OFF, SMD, MD5, MS3D, ASE (own readers vs Assimp)

Same corpus, same Assimp v5.4.3 sources, rebuilt with the OFF, SMD, MD5, MS3D, ASE importers (and the 3DS one, which Assimp's ASE code needs: without it ASE compiles to nothing and every `.ase` fails with "no suitable reader"). The Assimp side was printed by the scratch tool described above (triangles, area, bounding box, clip length; `aiProcess_Triangulate` only); the own side by `test_model_corpus --list <reader> <dir>`. The rows are in the manifest (`off`, `smd`, `md5`, `md5anim`, `ms3d`, `ase`).

**OFF** (`docs/own_off_reader.md`): `Cube.off` and `Wuson.off` read by both with equal triangle count (12, 3,732), area (6.0000, 9.0258) and bounding box. `invalid.off` (declares 4 faces, holds fewer): Assimp reads it as 2 triangles of zero area, ours refuses it, naming the face. `OutOfMemory.off` (claims 353,535,235,358 vertices): Assimp tried to allocate it and was killed by the kernel's out-of-memory handler during this run; ours refuses it before allocating. `empty.off`: both refuse.

**SMD** (`docs/own_smd_reader.md`): all three files read by both. Triangle counts (1, 896, 3,732) and surface areas (0.5000, 1202.6489 against our 1202.6491, 9.0258) agree. The bounding boxes agree once Assimp's still-Z-up result is turned the way our root node turns it ((x, y, z) to (x, z, -y)). Assimp's vertex counts are unwelded (11,196 on Wuson against our 2,117). All three carry only a bind pose (no animation frames, no rotated bones with weights to speak of), so the Euler order and the animation path are checked on synthetic fixtures only. The winding of the real files agrees with their vertex normals (3,730 of 3,732 triangles on Wuson), so it is kept as written.

**MD5** (`docs/own_md5_reader.md`): `SimpleCube`, `Bob` (with its `Bob.md5anim`) and `BoarMan` read by both. Triangle counts (12, 1,027, 2,812), surface areas (24,576.0073 against Assimp's 24,576.0005; 6,832.9236 against 6,832.9229; 2,146.1729 against 2,146.1725: float rounding of the weighted vertex sums), bounding boxes (Assimp turns Z-up exactly as our root node does) and clip length (5.792 s) agree. Bob's 33 joint tracks have 140 position keys and 140 rotation keys each in both; first and last keys agree to four decimals on 31 tracks, and on the other two differ by one unit in the fourth decimal or by the sign of zero. `BoarMan` has 13 empty meshes (ours skips them and says so). Assimp counts only joints that carry weights as bones (32 on Bob), ours counts every joint (33). Read on its own, `Bob.md5anim` gives Assimp a 200-triangle stick-figure mesh, ours the skeleton and the clip. Vertex counts differ because ours are welded (875 against 3,081 on Bob). `empty.md5mesh`: both refuse.

**MS3D** (`docs/own_ms3d_reader.md`): all four read by both with equal mesh counts (1, 7, 2, 2), triangle counts (3,732, 2,032, 240, 240), surface areas (9.0258, 755.1954, 122,847.3647, 122,847.3647) and bounding boxes. Assimp's vertex counts are unwelded (11,196 on Wuson against our 2,117). **None of the four files has a joint**, so the skeleton, the weights (including the unverified percent scale of the version 4 extra block), the keys and the `rest * key` composition are checked on synthetic fixtures only.

**ASE** (`docs/own_ase_reader.md`): Assimp's ASE importer only exists in a build that also has its 3DS importer (the ASE sources are wrapped in `#ifndef ASSIMP_BUILD_NO_3DS_IMPORTER`), so the comparison build had both. The 14 UTF-8 files (12 in `models/ASE`, 2 in `models-nonbsd/ASE`) agree: triangle counts, surface areas (e.g. 840,000.1201 against Assimp's 839,999.9844 on `ThreeCubesGreen`, float noise at that size), bounding boxes (Assimp turns Z-up the same way) and clip lengths (e.g. 45.167 s on the 26-object `MotionCaptureROM.ase`, 6.000 s on `RotatingCube.ase`). Rotation keys were compared on `RotatingCube`, `anim2` and `CameraRollAnimWithChildObject`: equal at the first and last sample to four decimals, which also confirms the relative accumulation and 3ds Max's rotation sign. Position, rotation and scale key counts per node agree (101 / 95 / 45 on `Box02`); differences: Assimp makes a `.Target` node and channel for target cameras (ours ignores the target's block and says so), and Assimp's scene pre-processing adds a one-key track from the node's rest transform where a channel has no keys of that kind (ours leaves that track empty). The two UTF-16 files (`ThreeCubesGreen_UTF16BE.ASE`, `_LE.ASE`) are read by Assimp as 1 mesh with 8 triangles of zero area; ours reads the same 3 cubes, 36 triangles, area 840,000.1201 as the UTF-8 original. Assimp's vertex counts are unwelded (108 against our 72 on `ThreeCubesGreen`); our material count follows the sub-materials a face actually uses (Assimp lists one material per mesh). `empty.ase`: both refuse. The texture transform tests (`UVTransform_*.ASE`) read, but tiling, offset and rotation are not applied by us (counted in a note).

## What this does not show

- **All of this is one corpus** from the test directory of one library: a handful of exporters, many files written by the same few tools, several of them tiny test cases. A file from an exporter not in it can still break a reader. The manifest is a regression net, not a proof.
- The comparison reference is another importer (ufbx for FBX, Assimp for the rest), not the original authoring tool's own view. Where both of them are wrong in the same way this does not notice.
- Material values (colours, roughness mapping, texture paths) were compared for the 3DS and COLLADA files only by count; embedded-texture extraction was not compared. FBX material values were compared with ufbx (above); glTF and OBJ material values only by count of materials and distinct texture files.
- Windows was a MinGW build under wine, not Windows itself and not MSVC; macOS and the web were not run.
- The Assimp used for the numbers was built from the same v5.4.3 sources with the FBX, COLLADA, 3DS and BVH importers (first runs) and with the glTF, OBJ, STL and PLY importers (pre-flip verification).
- Many files in the corpus are tiny test cases or deliberately broken files, not production assets; the glTF files are sample models and a few generators' outputs, not the kind of files users export from their tools (the OBJ, PLY and STL files include a few exporter files). Real-world glTF from other tools is covered only by the repository's own rig fixtures (`tests/assets/rigs`).
- The release note for the dropped formats, the default flips and the deletion of Assimp and ufbx have not been done; they wait for the owner (`docs/own_code_roadmap.md`).

## Flip readiness (pre-flip verification, not an approval)

This section is a status report for the owner. It flips nothing, deletes nothing and is not the release announcement. The roadmap's five replacement criteria (`docs/own_code_roadmap.md`, "Replacement criteria"), checked for the two rows that gate the plan (announce the dropped formats one release ahead, flip our readers to the default the release after, delete Assimp and ufbx the release after that).

### Criteria

| # | Criterion | Assimp row (COLLADA, 3DS, BVH default flip; glTF, OBJ, STL, PLY already ours) | ufbx row (FBX default flip) |
|---|---|---|---|
| 1 | Real-file corpus passes | **Met for the one corpus we have**: 26 COLLADA, 16 3DS, 3 BVH and 102 glTF/OBJ/STL/PLY files from Assimp's test directory, every difference listed above. **Not met in breadth**: it is one library's test directory (a handful of exporters, many tiny cases, deliberately broken files); no file from a current Blender, Maya or 3ds Max export of COLLADA, 3DS or glTF beyond the glTF sample models. | **Met for the one corpus**: 42 real FBX files plus 12 fixtures agree with ufbx on counts, names, boxes, area, skins, clips and now material values (exceptions above). **Not met in breadth**: same limits, and no file with a left-handed source axis system, `InheritType` other than the default, FBX 6.x, or NURBS. |
| 2 | Platform tests pass on Windows and Linux | **Linux met. Windows not met**: a MinGW build under wine passes, which found and fixed two Windows-only defects, but wine is not Windows and nothing was built with MSVC. The Windows distribution is the build that ships Assimp, so a pass on real Windows (and of the MSVC build, if that is what ships) is what is missing. | Same. |
| 3 | Fuzz / sanitizers | **Met**: engine-linked tests under ASan + UBSan + float-cast-overflow (no recover) and under valgrind, 33,151-input mutation sweep, the standalone hostile test; all clean. No libFuzzer or multi-hour run. | **Met**, same, plus the standalone FBX hostile and mutation test. No libFuzzer or multi-hour run. |
| 4 | No known bugs in the replacement | **Not met as stated**: nothing found is left unfixed, but the known gaps below are real behaviour differences from Assimp (not crashes). | **Not met as stated**: gaps below. |
| 5 | Owner approval | **Not given.** | **Not given.** |

Verdict: **the criteria for flipping the default are not all met.** The evidence supports "no crash, no leak, no wrong geometry on 102 + 43 + 26 + 16 + 3 real files, on Linux, and the same on a Windows build under wine". What is missing is a run on real Windows, files from more exporters, and the owner's approval. The readers are in good shape to be tried by the owner on the build that ships; they are not proven for every user file.

### What is still open

Assimp row:
- Real Windows run (criterion 2); files from other exporters (criterion 1).
- 3DS frame rate (30) and BVH axes are assumptions; BVH gives a skeleton and a clip with no mesh, where Assimp gives a stick-figure mesh (a deliberate difference).
- COLLADA: the one-bone difference is explained (Assimp drops a bone with no weights; ours keeps it); camera- and light-only files now import as nodes with no mesh (Assimp builds a stick-figure mesh instead, a deliberate difference).
- OBJ and PLY point clouds, OBJ point and line elements, PLY with a list property in the vertex element, glTF points and lines: now read as a mesh with vertices and no triangles, as Assimp does. Remaining differences: Assimp does not weld the vertices of OBJ `p` / `l` elements (`testline.obj` 36 vertices, `testpoints.obj` 24, against ours 8, one per named vertex), and OBJ line and point sets give no material of ours where Assimp adds a default one. glTF 1.0: still refused with a message (a different file layout; estimate: a day or two for a reader of the 1.0 layout, techniques and shaders ignored).
- The one 3DS file that fails (`invalid/empty.3ds`, no bytes) fails in Assimp too ("File is empty"): not a gap.
- LWO planar, cylindrical and spherical image maps: UVs are generated now (see the LWO section); not applied, as in Assimp: the map's own centre, size and rotation.
- SMD and MS3D skeletons on real files: none of the corpus files has a joint beyond the bind pose (SMD) or any joint (MS3D), so there was nothing to check and no defect was found; unverified on real animated files.
- Not done, and large: glTF 1.0 (above), Maya Arnold / OSL / ShaderFX materials for FBX (separate `Shader` objects; estimate: several days with real Maya files to test against, which the corpus does not have).
- Material values for COLLADA, 3DS, glTF and OBJ were compared only by count.
- The release note naming the dropped formats is not written (the owner decides its text).

ufbx row:
- Real Windows run, other exporters, left-handed source axes and `InheritType` on real files (criterion 1 and 2).
- Material gap, what is done and what is not: the common PBR property sets of 3ds Max (Physical, PBR metal/rough, PBR spec/gloss, glTF, OpenPBR) are read like ufbx reads them; **not done**: Maya Arnold / OSL standard-surface and ShaderFX graph materials (they live in separate `Shader` objects), UV set names (first UV set only, as the ufbx path), textures other than colour and normal map, and the PBR slots the engine has no field for.
- The remaining differences from ufbx on animation and geometry in `docs/own_fbx_reader.md` ("What is NOT handled yet").

### The formats that would be dropped

Removing Assimp removes these importers, which `engine/CMakeLists.txt` enables in the Windows distribution build (`OKAY_USE_ASSIMP=ON`) and which no reader of ours covers: **`.blend`, `.x`, MD5 (`.md5mesh`), SMD, MilkShape (`.ms3d`), LightWave scenes (`.lws`), OFF, AC3D (`.ac`), B3D, ASE, Ogre XML mesh, OpenGEX, Q3D (`.q3o`, `.q3s`), Q3BSP, `.irr` and `.irrmesh`, MD2, MD3, MDC, MDL, HMP, NFF, RAW, COB, CSM, Terragen, `.sib`, PMX, IQM, `.ndo`, XGL**. That is 31 importer switches: of the 36 the Windows build enables, the five readers of batch B (3MF, AMF, X3D, DXF and LWO objects, each off by default, `docs/own_3mf_reader.md` and siblings) now cover five (LightWave scenes are not covered; the readers of other batches are listed in their own pages).

What changes for the formats we do read, in the Windows distribution: STL and PLY move from Assimp-first to ours (verified above: STL 8 of 8 agree, PLY 9 of 12 read with 2 more rejected by both and 1 not read by ours (the truncated file), all listed); `.dae`, `.3ds` and `.bvh` move from Assimp to ours (each opt-in today), and `.fbx` from ufbx to ours. glTF/GLB and OBJ are already ours in every build. Build options with Assimp off are unaffected; they never had these formats.

