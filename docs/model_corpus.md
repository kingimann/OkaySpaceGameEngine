# Real-file validation of the own model readers

Until this page the own readers (FBX, COLLADA, and the new 3DS and BVH) had only been run on files we wrote ourselves. This page records what happened when they were run on files that real exporters wrote.

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

## Running it

```
OKAY_MODEL_CORPUS=<checkout>/test ctest -R "model_corpus|fbx_reader"
```

- `model_corpus` reads every manifest row with the reader named in it. A file recorded as read must still read with the same mesh, triangle, bone, skinned-mesh, clip and material counts; a file recorded as failing must still fail (a fix therefore shows up as a manifest change).
- `fbx_reader` (`tests/test_fbx_reader.cpp`) additionally imports every real `.fbx` through ufbx and through our reader and compares them the way it does for our own fixtures: object, mesh, joint, vertex, triangle and clip counts, names, bounding box, surface area, skin weights, inverse binds, clip samples.
- Without `OKAY_MODEL_CORPUS` both tests run only on our fixtures and `model_corpus` prints "skipped".
- `test_model_corpus --one|--list|--fbxdump|--fbxdiff|--fbxclips|--keys ...` print what the readers found, for comparing with another importer.

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

## What this does not show

- **All of this is one corpus** from the test directory of one library: a handful of exporters, many files written by the same few tools, several of them tiny test cases. A file from an exporter not in it can still break a reader. The manifest is a regression net, not a proof.
- The comparison reference is another importer (ufbx for FBX, Assimp for the rest), not the original authoring tool's own view. Where both of them are wrong in the same way this does not notice.
- Material values (colours, roughness mapping, texture paths) were compared for the 3DS and COLLADA files only by count; embedded-texture extraction was not compared.
- Nothing was run on Windows.
- The Assimp used for the numbers was built from the same v5.4.3 sources with only the FBX, COLLADA, 3DS and BVH importers.
- The release note for the dropped formats, the default flips and the deletion of Assimp and ufbx have not been done; they wait for the owner (`docs/own_code_roadmap.md`).
