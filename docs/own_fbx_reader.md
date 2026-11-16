# Our own FBX reader (`okay::fbx`)

Status: **side by side, off by default.** ufbx (`engine/third_party/ufbx`) is still the default FBX reader and is still compiled in. Nothing has been removed. This follows stage 2 of `docs/own_code_roadmap.md`: our own backend behind the existing seam, selectable, and promoted only after it has earned it.

## What it is

Written from the public description of the FBX format, with no code from ufbx, the FBX SDK or Assimp.

| File | What |
|---|---|
| `engine/include/okay/Render/FbxReader.hpp` | Public API: `okay::fbx::Parse`, `Build`, `LoadFile`, `Inflate`, the `Scene` result structs, the on/off switch |
| `engine/src/Render/FbxReader.cpp` | Our own zlib/deflate decoder, binary FBX 7.x reader (32-bit and 64-bit record offsets), ASCII FBX reader |
| `engine/src/Render/FbxScene.cpp` | Element tree to scene: nodes, transforms, meshes, skins, blend shapes, materials, textures, baked animation |
| `engine/src/Render/ModelImport.cpp` | The adapter (`ImportFbxSceneGraphOwn`, `ImportFbxMergedOwn`): builds the same GameObjects, `MeshRenderer`, `SkinnedMesh`, `BlendShapes` and `ModelAnimator` clips the ufbx path builds |
| `scripts/gen_fbx_fixtures.py` | Writes the fixtures in `tests/assets/fbx/` (binary 7.4, binary 7.5, ASCII) |
| `tests/bench_fbx_reader.cpp`, `scripts/gen_fbx_bench.py` | Speed and peak-memory bench against ufbx, and the large synthetic files it reads (section "Speed and memory") |

The reader files use the standard library and one header-only file of our own, `engine/include/okay/Core/Inflate.hpp` (the engine's shared inflate; no `<mutex>`/`<thread>`, no engine library), so the hostile-input test still builds them on their own under sanitizers (it only needs `engine/include` on the include path).

## Turning it on

- CMake: `-DOKAY_OWN_FBX=ON` makes it the default reader for that build (default `OFF`).
- Run time: environment variable `OKAY_OWN_FBX=1` (or `0`) overrides the build default; `okay::fbx::SetOwnReaderEnabled(true)` does the same from code and is what the tests use.
- When our reader is selected and cannot read a file, the importer falls back to ufbx for that file.

## What it handles

- Binary FBX 7.1 to 7.7 (32-bit offsets below 7500, 64-bit from 7500) and ASCII FBX 7.x. All array property types (`f d l i b` and the byte array `c`), raw and zlib-compressed. The zlib arrays are inflated by the engine's ONE inflate (`engine/include/okay/Core/Inflate.hpp`: stored, fixed and dynamic Huffman blocks); `okay::fbx::Inflate` is a thin wrapper that keeps the forgiving behaviour this reader always had (the Adler-32 and bytes after the stream are not checked). The FBX reader no longer has a decoder of its own.
- Embedded textures (`Video` / `Content`, raw bytes in binary, base64 in ASCII) and texture file names.
- Geometry (`Mesh`): positions, polygon vertex indices with the negative end marker, normals, UVs, vertex colors, per-polygon material ids. Mapping modes `ByPolygonVertex`, `ByVertice`, `ByPolygon`, `AllSame` and `ByEdge`; reference modes `Direct` and `IndexToDirect`. Missing normals are generated flat, one per polygon (ufbx does the same without smoothing data; smoothing groups are not used). Polygons with fewer than 3 corners or out-of-range indices are skipped.
- Triangulation: triangles directly, quads and n-gons by ear clipping on the polygon's dominant plane (concave polygons keep their winding), fan fallback for degenerate input and for polygons over 256 corners.
- Meshes split per material slot, in ascending slot order.
- Materials: diffuse color, emissive color and factor, shininess mapped to roughness (`1 - sqrt(shininess) * 0.1`, the mapping ufbx uses), diffuse and normal-map texture connections. PBR property sets written by 3ds Max (Physical Material, PBR metal/rough, PBR spec/gloss, the glTF material, OpenPBR), recognised by the class id in the material the way ufbx does (a material that says lambert or phong keeps the classic reading): base colour, roughness (glossiness inverted when `useGlossiness` is about 1), metalness, emission colour and factor, and the colour, normal and ambient-occlusion texture slots, which 3ds Max connects as `<name>_map`. A texture whose own Video has no picture takes it from another Video with the same file name.
- Node hierarchy and transforms: Lcl translation, rotation, scaling, rotation order (all six), pre- and post-rotation, rotation and scaling pivots and offsets, geometric translation/rotation/scaling, local and world matrices, decomposed to translation, quaternion and scale.
- Axis and unit settings from `GlobalSettings` (up, front, coord axes and signs, unit scale) are converted to right-handed Y-up, meters, the same way ufbx's "modify geometry" mode does it: the unit scale multiplies every translation, vertex, inverse bind and blend offset, and only the root nodes get the axis rotation, so every node below keeps the local axes the file was authored in (important for retargeting). Checked against ufbx on the fixtures (Z-up/cm and Y-up/m).
- Skins: first skin deformer, clusters, per-control-point weights sorted strongest first, the cluster's `Transform` (mesh node relative to the bone, as Blender writes it and as ufbx reads it) times the geometric transform as the inverse bind (`geometryToBone`), bone node per cluster. `TransformLink` is not used.
- Blend shapes: blend deformers, channels, `DeformPercent`, the channel's last target shape with position and normal offsets.
- Animation: stacks, layers, curve nodes, curves (`KeyTime`, `KeyValueFloat`, `KeyAttrFlags`, `KeyAttrDataFloat`, `KeyAttrRefCount`). Constant, linear and cubic interpolation (Hermite from the slopes the file stores, or neighbour-derived for auto keys when it stores none). Translation, rotation and scaling of nodes plus blend `DeformPercent` are baked at 30 Hz (capped at 20000 samples), start trimmed to 0, one clip per stack.
- Limits against hostile files (`okay::fbx::Limits`). Going past one fails the load with an error string (never a crash); parent cycles among models are broken instead of failing. The ufbx path in `ModelImport` enforces the same node and animation numbers (see `docs/model_import.md`).

  | Limit | Default | What it bounds |
  |---|---|---|
  | `maxDepth` | 96 | element nesting while parsing |
  | `maxElements` | 4 Mi | elements in the whole tree, and objects in a scene |
  | `maxArrayBytes` | 512 MiB | decoded array bytes, whole file |
  | `maxStringBytes` | 256 MiB | one string or raw blob |
  | `maxCorners` | 8 Mi | triangle corners built, whole scene |
  | `maxNodes` | 1 Mi | scene nodes (models) |
  | `maxNodeDepth` | 2048 | scene hierarchy depth (the walk is iterative, so depth is never call depth) |
  | `maxBakedSamples` | 20000 | baked samples per clip |
  | `maxAnimStacks` | 128 | `AnimationStack` objects |
  | `maxAnimLayers` | 1024 | `AnimationLayer` objects |
  | `maxAnimCurves` | 128 Ki | `AnimationCurve` objects, and `AnimationCurveNode` objects, each |
  | `maxAnimKeys` | 4 Mi | keys over all curves |
  | `maxTotalBakedSamples` | 4 Mi | baked evaluations over all clips (samples times animated curve nodes). Several stacks can point at the same curves, so each clip is charged against this one budget before it is baked |

## What is NOT handled yet

Say plainly what a user might trip over:

- **Real files: one outside corpus only.** 43 files from the test directories of an Assimp 5.4.3 checkout were read (`docs/model_corpus.md`): 42 read and agree with ufbx on counts, names, bounding boxes, surface area, skin weights, inverse binds and clip samples (within the tolerances listed there); one damaged file is rejected by every reader. Five bugs came out of it and are fixed (byte-array properties, takes sharing a timeline, stored slopes of auto keys, bridged polygons, integer conversions of hostile doubles). That is a handful of exporters and several tiny test cases: a file from another exporter can still break us. Keep ufbx as the default.
- FBX 6.x and older (no `Objects` section / different layout) and binary versions below 7100. `Properties60` is not read.
- `InheritType` other than the default (`RrSs`, `Rrs` scale inheritance) is treated as the default. ufbx can compensate for these.
- Only `Mesh` geometry. NURBS, patches, lines, cameras, lights, skeleton node attributes, `Pose`/`BindPose` objects, templates in `Definitions` and visibility are ignored (clusters carry the bind matrices we use).
- Mesh data: only the first UV set; vertex colors are read but the engine does not use them; tangent, binormal and smoothing-group layers are ignored; `ByEdge` mapping is approximate (a corner takes the edge that starts at it).
- Materials: no `Bump`, specular, opacity, transmission, coat, sheen or subsurface values, no Maya / Arnold / OSL standard-surface or ShaderFX / Stingray graph materials (they sit in separate `Shader` objects the reader does not follow), no textures for emission, metalness, roughness or opacity (the importer takes the colour and normal-map files), UV set names are not read (first UV set only, as in the ufbx path). Material values agree with ufbx on the 42 real files and the 12 fixtures except where the picture file of a textured colour or glossiness slot is missing (`docs/model_corpus.md`).
- Skinning: only the first skin deformer; dual-quaternion skinning type and additive cluster link modes are ignored; for a geometry shared by several nodes the first node's geometric transform is used for the inverse binds.
- Blend shapes: in-between shapes (`FullWeights` with several targets) are not interpolated, only the last target is used.
- Animation: constant (step) curves are baked at 30 Hz, so a jump becomes a one-frame ramp (ufbx emits a key pair at the jump); we emit every channel of an animated node at every sample (ufbx emits only animated channels with fewer keys); custom tangent weights and velocities are ignored; TCB tangents are treated like auto; auto-clamp is approximated; layer blending is simplified (first layer overrides, later layers add or lerp by weight, rotations are blended as Euler angles); only translation, rotation, scaling and `DeformPercent` are animated (not pivots, pre/post rotation or other properties); a take plays the span its `LocalStart` / `LocalStop` give (several takes can share one set of curves), else the range of its keys; ufbx keeps keys beyond the span in its clip, ours cuts there; a take whose curves animate only custom properties gives no clip (ufbx gives a constant one); no key reduction (ufbx reduces keys; the engine's `OptimizeClips` runs afterwards on both paths).
- Left-handed source axis systems: the root node gets the (mirroring) axis matrix and nothing else is done about winding; untested against ufbx. `Transform` is assumed to be bone-relative as above; an exporter that writes the mesh's world matrix there instead would skin wrongly (ufbx makes the same assumption, but this has only been checked on our own fixtures).
- No repair of damaged files. ufbx recovers from many malformed files; ours rejects them. Big-endian files, zlib checksums (Adler-32 is not verified), encrypted FBX and streaming are not supported.
- Slower than ufbx: about 3 times on the reader alone and up to 2 times on the whole import (section "Speed and memory" below). Animated files are the worst case for the reader alone, because ours bakes every animated node at 30 Hz.

## Speed and memory

Measured on 2026-11-16 with `bench_fbx_reader` (`tests/bench_fbx_reader.cpp`; how to run it: `docs/try_it.md`, "FBX reader
speed and memory"). Linux, GCC 13, Release (`-O3`), one core of a shared 4-core cloud machine while other builds ran, so a
single number can be off by 10 to 20 %; the ratios were stable over repeated runs. **Not measured on Windows.**

What each column is: **load** is the reader alone, file bytes already in memory, into its own structures
(`okay::fbx::LoadMemory`: element tree plus the scene build with baked clips, against `ufbx_load_memory` with the engine's
ufbx options). **Import** is `ImportModelScene` through the seam, with our reader switched on and off: file read, reader,
GameObjects, meshes, skins and clips, the work an editor import does. **Peak** is the most heap the call had allocated at one
time, counted by replacing `malloc`/`free` in the bench (so ufbx's C allocations and our `new` count the same way). Times
are medians of at least 7 runs.

Files: the 42 readable FBX files of the outside corpus (`docs/model_corpus.md`; the 43rd is the damaged file both reject),
the 12 fixtures, and four large synthetic files from `scripts/gen_fbx_bench.py` (not committed): a 204,800-triangle grid
(binary 7.5), a 51,200-triangle grid in ASCII, 3,000 nodes each with its own box, and a 64-bone skinned rig with 1,800-key
curves on every bone (345,600 keys, 60 seconds).

**Totals** (sum of the medians; before = the code on main, after = this change):

| Set | Our load, before | Our load, after | ufbx load | Our import, before | Our import, after | ufbx import | Largest peak, our load / ufbx load | Largest peak, our import / ufbx import |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 54 real files and fixtures (12 MB) | 128.9 ms (4.8x) | 77.4 ms (2.9x) | 26.6 ms | 241.6 ms (4.6x) | 103.4 ms (1.9x) | 55.1 ms | 9.5 MB / 3.8 MB | 13.2 MB / 24.1 MB |
| 4 synthetic files (13 MB) | 333.1 ms (4.8x) | 210.3 ms (3.2x) | 65.6 ms | 1,316 ms (5.9x) | 303.0 ms (1.4x) | 213.4 ms | 56.4 MB / 29.3 MB | 83.2 MB / 120.3 MB |

**The large files** (after):

| File | KB | Our load | ufbx load | Ratio | Our peak | ufbx peak | Our import | ufbx import | Ratio | Import peak ours / ufbx |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| grid, 204,800 triangles, binary | 2,488 | 66.0 ms | 18.0 ms | 3.7x | 56.4 MB | 19.8 MB | 116.1 ms | 97.1 ms | 1.2x | 83.2 / 120.3 MB |
| grid, 51,200 triangles, ASCII | 3,486 | 24.3 ms | 8.8 ms | 2.8x | 14.1 MB | 9.2 MB | 31.6 ms | 19.8 ms | 1.6x | 20.8 / 30.3 MB |
| 3,000 nodes and boxes | 4,262 | 51.1 ms | 19.9 ms | 2.6x | 41.3 MB | 29.3 MB | 63.2 ms | 32.1 ms | 2.0x | 45.5 / 29.3 MB |
| 64-bone rig, 345,600 keys | 3,127 | 68.8 ms | 18.8 ms | 3.7x | 32.0 MB | 24.5 MB | 92.0 ms | 64.4 ms | 1.4x | 35.1 / 28.3 MB |
| `pyramob.fbx` (binary, the largest real file) | 826 | 15.4 ms | 4.9 ms | 3.2x | 9.2 MB | 3.2 MB | 17.8 ms | 11.5 ms | 1.6x | 13.1 / 24.1 MB |
| `pyramob.fbx` (ASCII) | 3,798 | 17.5 ms | 6.3 ms | 2.8x | 9.5 MB | 3.8 MB | 26.3 ms | 14.3 ms | 1.8x | 13.2 / 24.1 MB |

**Outliers.** Worst ratio on a real file: `anims_with_full_rotations_between_keys.fbx`, load 10.3 ms against 0.6 ms (17x)
and import 15.5 ms against 2.0 ms (7.7x; it was 63 ms, 35x, before this change). The time is the 30 Hz bake: every
animated node gets a full transform (Euler rotations, about 10 matrix products, a decomposition) at every sample, while
ufbx keeps the file's keys. The fixtures are smaller than 15 KB: on the eight without animation or PBR materials ours takes
0.74 to 1.06 times ufbx's time, on the animated ones and `pbr_bin.fbx` 2.4 to 3 times (0.08 to 0.16 ms), and on all of them
ours needs less memory (ufbx starts at about 0.1 MB). Worst peak-memory ratio on the reader alone: 2.9x (`pyramob.fbx`,
the 204,800-triangle grid). On the import, which is what a user waits for, ours uses **less** peak memory than the ufbx
path on every large file except the 3,000-node one (1.6x), because the ufbx path holds the whole ufbx scene while it builds
the engine meshes.

**What was slow, and fixed** (each output-preserving: `bench_fbx_reader --digest` hashes every field our reader produces,
bit for bit, and the hash of all 58 files is the same before and after; the agreement tests below pass unchanged):

1. **Key reduction was quadratic on held tracks** (`AnimationCurve::Reduce`, shared by every import path): extending a
   straight segment re-checked every key in it, so a constant track of n keys cost n²/2 checks. Ours bakes every channel of
   an animated node, so most tracks are constant: 86 % of the rig import was here. A run of equal values now skips the
   re-check (the result is provably the same; `anim_clip_tools` compares it with the plain rule on 400 random curves with
   flat runs, signed zeros, infinities, NaN and repeated times). The rig import went from 928 ms to 92 ms.
2. **Mesh corners grew by doubling.** Each part's corner list is now allocated once at its final size (the triangle count
   is known after one pass over the polygons). Peak on the 204,800-triangle grid: 103.5 MB to 56.4 MB.
3. **Vertex de-duplication allocated a string per corner** in the own import path; it is now an open-addressing table of
   indices (same first-occurrence order).
4. **zlib arrays grew by doubling and were zero-filled twice**: the decoded size is known from the array header and is now
   allocated once, and the numbers are converted as the array is built.
5. **ASCII numbers** went through a `std::string` and `strtod` each; plain decimals up to 15 digits with a power of ten up
   to 22 are now converted exactly by one multiplication or division (Clinger's observation, 1990), anything else still
   goes to `strtod`. Checked bit for bit against `strtod` on 10 million generated tokens.
6. **Smaller ones:** pre- and post-rotation are computed once per node for a bake instead of once per sample; the shared
   inflate refills its bit buffer 8 bytes at a time (`inflate_differential` and `own_http_inflate` pass); `LoadFile` reads
   the file in one call.

**What is still slower, and why** (not fixed here):

- The reader alone stays about 3x ufbx. The design has two passes: a general element tree in which every number of every
  array is stored as a `double` (an index array takes twice its file size, a float array twice), then the scene build. ufbx
  reads in one pass into typed arrays. Typed arrays in `Prop` would change the reader's public structs; that is the next
  step if the time matters.
- Our scene stores meshes as triangle corners (48 bytes each: position, normal, UV, colour, control point), not indexed
  vertices: the grid's 614,400 corners are 29.5 MB. The import de-duplicates them afterwards.
- Baking at 30 Hz (above) costs time on long clips and on files with many animated nodes. ufbx does not bake.
- The shared inflate decodes 30 to 50 % more instructions per byte than ufbx's on float arrays.

## Tests

- `fbx_reader` (`tests/test_fbx_reader.cpp`): inflater against zlib output (stored, fixed, dynamic, limits, truncation); hand-computed checks on each fixture (axis/unit conversion, ear clipping area and winding, materials, skin weights and inverse binds, blend shape, baked rotation/translation/scale values, pivot and pre/post-rotation transform against an independent evaluation); a hand-computed Hermite check (two keys with different outgoing and incoming slopes, sampled at several times, against the spline formula, through our reader and through ufbx); then **every fixture in `tests/assets/fbx` imported through ufbx and through our reader** and compared: object, mesh, skinned-mesh, joint, vertex, triangle, blend shape, clip and track counts, object names, world-space bounding box and surface area, skin weights, joint indices and inverse binds, clip samples within tolerance, and the single-mesh `ImportModel` path.
- `fbx_import_limits` (`tests/test_fbx_import_limits.cpp`): hostile files through `ImportModelScene` on the ufbx path and with our reader switched on: a 100000-model parent chain, a chain just past the depth cap, too many stacks, layers and curves, and many takes sharing one curve; each must fail with `ok == false` and a legal deep chain must still import. Files go to the temp directory.
- `fbx_hostile` (`tests/test_fbx_hostile.cpp`): standalone, no engine code. Every truncation of every fixture, bad end offsets, huge property/array/string counts, bad encodings, 200000-deep nesting in binary and ASCII, a decompression bomb, an array-byte budget shared across the file, a 5000-deep, a 100000-deep and a cyclic model hierarchy, animation counts (stacks, layers, curves, keys, a claimed 10-million-key curve) and the whole-file bake budget with many takes sharing one curve, a 100000-corner polygon, out-of-range polygon indices, and a deterministic mutation fuzz (`OKAY_FBX_FUZZ_ITERS=20000` for a longer run). Run under `-fsanitize=address,undefined` (standalone build) it was clean, including 200000 mutations. The engine-linked `fbx_reader` and `fbx_import_limits` tests were later run under the same sanitizers built with the whole engine, and `fbx_reader` under valgrind, with no finding (`docs/model_corpus.md`).
- `fbx_reader` also compares, for every fixture and real file, what both paths put on each mesh object as material (colour, emission, metalness, glossiness, texture and normal-map file name), and has reader-level checks for the PBR property sets (`pbr_bin.fbx`) and the unconnected-Video picture (`orphan_video_bin.fbx`).
- `fbx_reader` also reads every real file of the outside corpus when `OKAY_MODEL_CORPUS` is set (`docs/model_corpus.md`), with slightly looser tolerances for vertex count (0.5 %), surface area (0.2 %) and clip samples (0.015) than for our fixtures, and `model_corpus` records the counts of each real file in `tests/data/model_corpus_manifest.tsv`.
- Fixtures `pbr_bin.fbx` and `orphan_video_bin.fbx` came from the pre-flip verification. Fixtures `takes_bin.fbx` (three takes with their own spans, a `c` thumbnail) and `bridge_bin.fbx` (a polygon with a bridged hole) were added for the bugs the real files showed.
- The hostile test was re-run with `-fsanitize=address,undefined,float-cast-overflow` (standalone build, 30,000 mutations per fixture): clean after the integer-conversion fix; the engine-linked tests were then run too (see above).
- Regenerate fixtures with `python3 scripts/gen_fbx_fixtures.py`. They are 2 to 12 KB each.
