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

## Running it

```
OKAY_MODEL_CORPUS=<checkout>/test ctest -R "model_corpus|fbx_reader"
```

- `model_corpus` reads every manifest row with the reader named in it. A file recorded as read must still read with the same mesh, triangle, bone, skinned-mesh, clip and material counts; a file recorded as failing must still fail (a fix therefore shows up as a manifest change).
- `fbx_reader` (`tests/test_fbx_reader.cpp`) additionally imports every real `.fbx` through ufbx and through our reader and compares them the way it does for our own fixtures: object, mesh, joint, vertex, triangle and clip counts, names, bounding box, surface area, skin weights, inverse binds, clip samples.
- Without `OKAY_MODEL_CORPUS` both tests run only on our fixtures and `model_corpus` prints "skipped".
- `test_model_corpus --one|--list|--fbxdump|--fbxdiff|--fbxclips|--keys ...` print what the readers found, for comparing with another importer. `--list` takes the readers `fbx-own`, `dae`, `3ds`, `bvh`, `obj`, `stl`, `ply` and `gltf` (`.gltf` and `.glb`). For `obj`, `stl`, `ply` and `gltf` a row also carries `tex` (distinct texture files over all materials), `nodes`, `uv` and `nrm` (1 when some mesh has per-vertex UVs or normals); glTF sizes are the file's own (the importer's auto-scale of very large or tiny models is undone).
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

24 of 26 files read. The two that do not contain only cameras or lights (no mesh, which our reader reports as an error, as it always did); Assimp reads those two and rejects one `invalid/` file that ours reads.

Compared with Assimp built from source on the 24 (mesh and triangle counts, world bounding box, surface area, clip count, animation duration, material count): all match except the differences listed here.

Bugs the real files found, fixed with regression tests in `tests/test_collada_reader.cpp`:

1. **Node chains deeper than 64** (a real file nests 65 boxes) failed with "hierarchy too deep"; the cap is 256 now (XML depth 512).
2. **Decimal commas.** Some files were written in a locale with a decimal comma (`21,920870`) in the vertex arrays and in `<unit meter="0,010000">`; they failed, or read at 100 times the size. A token with a comma and no point is read with the comma as the point (and the model's notes say so); `1,2,3` and `1.5,2` stay errors.
3. **UTF-16** files (little and big endian, with or without a byte-order mark) failed with an XML error; they are converted to UTF-8 first.
4. **Triangle strips and fans** were skipped (the manifest file read as an empty scene); they are read, with the strip's alternating winding.
5. **Concave polygons.** Polygons were fanned from their first corner, which covers the notch of a concave polygon; the surface area of the real concave test file was 13 times too large. Convex polygons still fan (same triangles as before); concave ones are ear clipped, bridges and coincident corners included, and zero-area slivers are dropped.
6. **Named takes** (`<library_animation_clips>`): all animations were merged into one clip; each `<animation_clip>` is now a clip, cut to its `start` / `end` and moved to start at 0.

Differences that remain: Assimp merges identical meshes (`meshes` / `tris` differ on an instanced-node file, the geometry and bounding box are equal); Assimp adds a default material to files without one (`materials` is one higher); on a file with a degenerate concave polygon Assimp drops two more slivers (58 against 64 triangles; surface area 0.234 against ours 0.245, the exact polygon area is 0.2455); on one skinned file the bone count differs by one (Assimp 4, ours 5; not investigated).

### 3DS (own reader vs Assimp)

See `docs/own_3ds_reader.md`: 15 of 16 read (the 16th is empty). Mesh count, triangle count, bounding box and surface area match Assimp on all 15; two files with a mesh matrix and animated rotation settled the row-vector convention and the clockwise rotation, and the last animation key of the rotating cube equals Assimp's to four decimals. The camera-only animation files give no clip for us.

### BVH (own reader vs Assimp)

See `docs/own_bvh_reader.md`: 3 of 3 read; first and last position and rotation keys equal to four decimals.

### glTF / GLB, OBJ + MTL, STL, PLY (own readers vs Assimp)

These four readers are always built, and today the shipped Windows distribution reads STL and PLY through Assimp first (`OKAY_PREFER_OWN_READERS` is off), so these are the readers whose behaviour changes for those users when Assimp goes. All 102 files of the four formats under `test/models` and `test/models-nonbsd` were read by ours and by Assimp 5.4.3 and compared on mesh, triangle and material counts, distinct textures, UV and normal presence, world bounding box, surface area, node count, animation count and length.

| Format | Files | Both read | Agree on everything compared | Only Assimp reads | Only ours reads | Neither |
|---|---|---|---|---|---|---|
| STL | 8 | 8 | 8 | 0 | 0 | 0 |
| PLY | 12 | 6 | 6 | 4 | 0 | 2 |
| OBJ | 28 | 22 | 14 | 4 | 0 | 2 |
| glTF / GLB | 54 | 25 | 23 | 15 | 6 | 8 |

What the differences are (each is in the manifest as the row's recorded result):

- **By design, not listed above as differences:** Assimp's extra default material; normals we generate when the file has none; node counts (ours adds one wrapper root, and a glTF mesh with several primitives becomes one child object per primitive); our importer scales a very large or tiny model to about two units (the corpus tool undoes that for glTF).
- **OBJ.** Part counts differ on files with groups (`regr01.obj` 12 parts against Assimp's 55 meshes, `spider.obj` 4 against 19, `segment.obj`, `cube_usemtl.obj`, `regr_3429812.obj`): ours makes one part per material, Assimp one mesh per group and material; triangles, area, bounding box, materials and textures are equal. `box_longline.obj` has one face of several hundred repeated corners: ours fans all of them (944 triangles), Assimp gives 634. The two concave-polygon files: ours 64 triangles, area 0.2455 (the exact polygon area); Assimp drops two zero-area slivers (58 triangles, area 0.2339). Not read by ours: OBJ files with no faces, only points or lines (`point_cloud.obj`, `testpoints.obj`, `testline.obj`: "no faces"), and a UTF-16 file (`box_UTF16BE.obj`: Assimp returns an empty scene for it, ours reports no faces).
- **PLY.** Not read by ours: two point clouds (`points.ply`, `pond.0.ply`: "no faces"; Assimp makes a mesh of points), `issue623.ply` (a list property in the vertex element, no face element), and `ant-half.ply`, whose header promises 912 faces and whose body holds 259: ours rejects the truncated file; Assimp reports 912 faces for it although the body holds 259, so its numbers are not a reference.
- **glTF.** Not read by ours: the seven glTF **1.0** files (a different layout, refused with a message saying so; Assimp reads the six `.gltf`, not the `.glb`), `SceneWithoutNodes.gltf` (no node, no mesh: Assimp returns an empty scene, ours fails the import) and eight files whose only geometry is points or lines (`Mesh_PrimitiveMode_00..03` and `07..10`; Assimp makes meshes with no faces). Ours reads six files Assimp rejects on strict schema checks (five `wrongTypes/*.gltf` and `issue_3269/texcoord_crash.gltf`). `IncorrectVertexArrays/Cube.gltf`: Assimp counts the line meshes (8 meshes, larger bounding box), ours the 4 triangle ones, triangles equal. `AnimatedMorphCube.gltf`: ours plays the morph-weight animation (4.2 s clip), Assimp reports none. Both fail on the Draco file, the missing `.bin`, the file with all indices out of range and the recursive node file.

Bugs the real files found, all fixed with a regression test (`tests/test_model_readers.cpp`, section "Found by the real-file corpus"):

1. **OBJ and PLY fanned concave polygons** from their first corner, which covers the notch (the concave L-shaped test polygon came out with area 3.22 against the true 0.25 on the real file). They are now ear clipped like COLLADA and FBX (convex polygons still fan, same triangles as before; the code is shared, `engine/src/Render/PolygonTriangulate.hpp`).
2. **PLY rejected a valid file with a free-text header line.** A Blender 2.4x exporter wrote `Created by Blender3D ...` without the `comment` keyword; the whole file (`Wuson.ply`) failed with "unknown PLY header line". Unknown header lines are now ignored, as `PlyFileDeclaresDuplicateVertex` already did.
3. **glTF ignored the primitive `mode`.** Triangle strips and fans were read as plain triangle lists (wrong triangles), lines and points were read as triangles (`IncorrectVertexArrays/Cube.gltf` had twice the triangles, a points primitive produced 341 triangles). Strips and fans are now expanded (alternating strip winding, degenerate triangles dropped), points and lines are left out, and trailing indices that do not make a whole triangle are dropped.
4. **A glTF triangle with an out-of-range index rejected the whole primitive**; only that triangle is dropped now (Assimp does the same).
5. **A glTF with unreadable meshes imported as an empty scene with `ok = true`** (missing `.bin`, Draco or meshopt compression, only points or lines). It now fails with a one-line error, as Assimp does; glTF 1.0 gets its own message.

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
- COLLADA: one skinned file differs by one bone (Assimp 4, ours 5; not investigated); two camera- or light-only files are an error for ours (no mesh), Assimp reads them.
- OBJ and PLY point clouds, OBJ line elements, glTF files whose only geometry is points or lines: ours fails the import ("no faces" / "none could be read"); Assimp makes meshes with no faces. glTF 1.0: refused with a message. PLY with a list property in the vertex element: not read.
- Material values for COLLADA, 3DS, glTF and OBJ were compared only by count.
- The release note naming the dropped formats is not written (the owner decides its text).

ufbx row:
- Real Windows run, other exporters, left-handed source axes and `InheritType` on real files (criterion 1 and 2).
- Material gap, what is done and what is not: the common PBR property sets of 3ds Max (Physical, PBR metal/rough, PBR spec/gloss, glTF, OpenPBR) are read like ufbx reads them; **not done**: Maya Arnold / OSL standard-surface and ShaderFX graph materials (they live in separate `Shader` objects), UV set names (first UV set only, as the ufbx path), textures other than colour and normal map, and the PBR slots the engine has no field for.
- The remaining differences from ufbx on animation and geometry in `docs/own_fbx_reader.md` ("What is NOT handled yet").

### The formats that would be dropped

Removing Assimp removes these importers, which `engine/CMakeLists.txt` enables in the Windows distribution build (`OKAY_USE_ASSIMP=ON`) and which no reader of ours covers: **`.blend`, `.x`, MD5 (`.md5mesh`), SMD, MilkShape (`.ms3d`), LightWave (`.lwo`, `.lws`), DXF, OFF, AC3D (`.ac`), B3D, 3MF, AMF, ASE, X3D, Ogre XML mesh, OpenGEX, Q3D (`.q3o`, `.q3s`), Q3BSP, `.irr` and `.irrmesh`, MD2, MD3, MDC, MDL, HMP, NFF, RAW, COB, CSM, Terragen, `.sib`, PMX, IQM, `.ndo`, XGL**. That is 36 importer switches.

What changes for the formats we do read, in the Windows distribution: STL and PLY move from Assimp-first to ours (verified above: STL 8 of 8 agree, PLY 6 of 12 read with 2 more rejected by both and 4 not read by ours, all listed); `.dae`, `.3ds` and `.bvh` move from Assimp to ours (each opt-in today), and `.fbx` from ufbx to ours. glTF/GLB and OBJ are already ours in every build. Build options with Assimp off are unaffected; they never had these formats.

