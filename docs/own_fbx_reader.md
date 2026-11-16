# Our own FBX reader (`okay::fbx`)

Status: **side by side, off by default.** ufbx (`engine/third_party/ufbx`) is still the default FBX reader and is still compiled in. Nothing has been removed. This follows stage 2 of `docs/own_code_roadmap.md`: our own backend behind the existing seam, selectable, and promoted only after it has earned it.

## What it is

Written from the public description of the FBX format, with no code from ufbx, the FBX SDK or Assimp.

| File | What |
|---|---|
| `engine/include/okay/Render/FbxReader.hpp` | Public API: `okay::fbx::Parse`, `Build`, `LoadFile`, `Inflate`, the import steps `BuildParts` and `BuildClips`, the `Scene` result structs, the on/off switch |
| `engine/src/Render/FbxReader.cpp` | Binary FBX 7.x reader (32-bit and 64-bit record offsets), ASCII FBX reader; zlib arrays go through the engine's one inflate. The element tree points into the parsed bytes (keep them alive while the `Document` is used) |
| `engine/src/Render/FbxScene.cpp` | Element tree to scene: nodes, transforms, meshes as the file's polygons, skins, blend shapes, materials, textures, animation curves and the clips they make; and the two import steps: `BuildParts` (triangulation, equal corners shared, parts per material) and `BuildClips` (the clips' keys) |
| `engine/src/Render/ModelImport.cpp` | The adapter (`ImportFbxSceneGraphOwn`, `ImportFbxMergedOwn`): builds the same GameObjects, `MeshRenderer`, `SkinnedMesh`, `BlendShapes` and `ModelAnimator` clips the ufbx path builds |
| `engine/src/Render/DecimalToDouble.hpp`, `engine/src/Render/Pow5Table.inc`, `scripts/gen_pow5_table.py` | Exact decimal-to-double conversion for ASCII numbers and its generated 5^q table (section "Exact ASCII numbers"); `tests/test_decimal_to_double.cpp` checks it against `strtod` |
| `scripts/gen_fbx_fixtures.py` | Writes the fixtures in `tests/assets/fbx/` (binary 7.4, binary 7.5, ASCII) |
| `tests/bench_fbx_reader.cpp`, `scripts/gen_fbx_bench.py` | Speed and peak-memory bench against ufbx, and the large synthetic files it reads (section "Speed and memory"); `--digest` and `--import-digest` show that a change kept the output |
| `tests/test_fbx_exact.cpp`, `tests/test_fbx_exact_numbers.cpp` | The arithmetic shortcuts of the reader against the plain code they replaced, on random inputs (section "Tests") |

The reader files use the standard library and one header-only file of our own, `engine/include/okay/Core/Inflate.hpp` (the engine's shared inflate; no `<mutex>`/`<thread>`, no engine library), so the hostile-input test still builds them on their own under sanitizers (it only needs `engine/include` on the include path).

## Turning it on

- CMake: `-DOKAY_OWN_FBX=ON` makes it the default reader for that build (default `OFF`).
- Run time: environment variable `OKAY_OWN_FBX=1` (or `0`) overrides the build default; `okay::fbx::SetOwnReaderEnabled(true)` does the same from code and is what the tests use.
- When our reader is selected and cannot read a file, the importer falls back to ufbx for that file.

## What it handles

- Binary FBX 7.1 to 7.7 (32-bit offsets below 7500, 64-bit from 7500) and ASCII FBX 7.x. All array property types (`f d l i b` and the byte array `c`), raw and zlib-compressed. The zlib arrays are inflated by the engine's ONE inflate (`engine/include/okay/Core/Inflate.hpp`: stored, fixed and dynamic Huffman blocks); `okay::fbx::Inflate` is a thin wrapper that keeps the forgiving behaviour this reader always had (the Adler-32 and bytes after the stream are not checked). The FBX reader no longer has a decoder of its own.
- Embedded textures (`Video` / `Content`, raw bytes in binary, base64 in ASCII) and texture file names.
- Geometry (`Mesh`): positions, polygon vertex indices with the negative end marker, normals, UVs, vertex colors, per-polygon material ids. Mapping modes `ByPolygonVertex`, `ByVertice`, `ByPolygon`, `AllSame` and `ByEdge`; reference modes `Direct` and `IndexToDirect`. Missing normals are generated flat, one per polygon (ufbx does the same without smoothing data; smoothing groups are not used). Polygons with fewer than 3 corners or out-of-range indices are skipped.
- Triangulation (the import step `BuildParts`, since 2026-11-16; the reader keeps the file's polygons in `Mesh::polygons`): triangles directly, quads and n-gons by ear clipping on the polygon's dominant plane (concave polygons keep their winding), fan fallback for degenerate input and for polygons over 256 corners.
- Meshes split per material slot, in ascending slot order (`BuildParts`).
- Materials: diffuse color, emissive color and factor, shininess mapped to roughness (`1 - sqrt(shininess) * 0.1`, the mapping ufbx uses), diffuse and normal-map texture connections. PBR property sets written by 3ds Max (Physical Material, PBR metal/rough, PBR spec/gloss, the glTF material, OpenPBR), recognised by the class id in the material the way ufbx does (a material that says lambert or phong keeps the classic reading): base colour, roughness (glossiness inverted when `useGlossiness` is about 1), metalness, emission colour and factor, and the colour, normal and ambient-occlusion texture slots, which 3ds Max connects as `<name>_map`. A texture whose own Video has no picture takes it from another Video with the same file name.
- Node hierarchy and transforms: Lcl translation, rotation, scaling, rotation order (all six), pre- and post-rotation, rotation and scaling pivots and offsets, geometric translation/rotation/scaling, local and world matrices, decomposed to translation, quaternion and scale.
- Axis and unit settings from `GlobalSettings` (up, front, coord axes and signs, unit scale) are converted to right-handed Y-up, meters, the same way ufbx's "modify geometry" mode does it: the unit scale multiplies every translation, vertex, inverse bind and blend offset, and only the root nodes get the axis rotation, so every node below keeps the local axes the file was authored in (important for retargeting). Checked against ufbx on the fixtures (Z-up/cm and Y-up/m).
- Skins: first skin deformer, clusters, per-control-point weights sorted strongest first, the cluster's `Transform` (mesh node relative to the bone, as Blender writes it and as ufbx reads it) times the geometric transform as the inverse bind (`geometryToBone`), bone node per cluster. `TransformLink` is not used.
- Blend shapes: blend deformers, channels, `DeformPercent`, the channel's last target shape with position and normal offsets.
- Animation: stacks, layers, curve nodes, curves (`KeyTime`, `KeyValueFloat`, `KeyAttrFlags`, `KeyAttrDataFloat`, `KeyAttrRefCount`). Constant, linear and cubic interpolation (Hermite from the slopes the file stores, or neighbour-derived for auto keys when it stores none). The reader reads the curves and plans one clip per take (name, start, length); the keys are made by the import step `BuildClips` (since 2026-11-16). Translation, rotation and scaling of nodes plus blend `DeformPercent` become keys at the file's own key times plus the keys needed to stay within a stated tolerance (section "Animation keys"; `OKAY_FBX_BAKE30=1` brings back the old bake at 30 Hz, capped at 20000 samples), start trimmed to 0, one clip per stack.
- Limits against hostile files (`okay::fbx::Limits`). Going past one fails the load with an error string (never a crash); parent cycles among models are broken instead of failing. The ufbx path in `ModelImport` enforces the same node and animation numbers (see `docs/model_import.md`).

  | Limit | Default | What it bounds |
  |---|---|---|
  | `maxDepth` | 96 | element nesting while parsing |
  | `maxElements` | 4 Mi | elements in the whole tree, and objects in a scene |
  | `maxArrayBytes` | 512 MiB | decoded array bytes, whole file |
  | `maxStringBytes` | 256 MiB | one string or raw blob |
  | `maxCorners` | 8 Mi | triangle corners the polygons give, whole scene (counted by the reader, before `BuildParts` makes them) |
  | `maxNodes` | 1 Mi | scene nodes (models) |
  | `maxNodeDepth` | 2048 | scene hierarchy depth (the walk is iterative, so depth is never call depth) |
  | `maxBakedSamples` | 20000 | baked samples per clip (the 30 Hz bake, `OKAY_FBX_BAKE30=1`) |
  | `maxAnimStacks` | 128 | `AnimationStack` objects |
  | `maxAnimLayers` | 1024 | `AnimationLayer` objects |
  | `maxAnimCurves` | 128 Ki | `AnimationCurve` objects, and `AnimationCurveNode` objects, each |
  | `maxAnimKeys` | 4 Mi | keys over all curves |
  | `maxTotalBakedSamples` | 4 Mi | baked evaluations over all clips (samples times animated curve nodes). Several stacks can point at the same curves, so each clip is charged against this one budget before it is baked, at its 30 Hz sample count whichever way its keys are made. The keys at the file's own times have a second budget of four times this for the evaluations they actually do |

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
- Animation: keys at the file's key times plus added ones (section "Animation keys"); a step jump becomes a key pair 0.2 ms apart (the engine merges keys closer than 0.1 ms), a ramp that short; between file keys at most 1/30 s apart nothing is added, so there the error is what linear playback between the file's own keys gives (as the 30 Hz bake gave on the same grid); we emit every channel of an animated node at every key (ufbx emits only animated channels); custom tangent weights and velocities are ignored; TCB tangents are treated like auto; auto-clamp is approximated; layer blending is simplified (first layer overrides, later layers add or lerp by weight, rotations are blended as Euler angles); only translation, rotation, scaling and `DeformPercent` are animated (not pivots, pre/post rotation or other properties); a take plays the span its `LocalStart` / `LocalStop` give (several takes can share one set of curves), else the range of its keys; ufbx keeps keys beyond the span in its clip, ours cuts there; a take whose curves animate only custom properties gives no clip (ufbx gives a constant one); no key reduction (ufbx reduces keys; the engine's `OptimizeClips` runs afterwards on both paths).
- Left-handed source axis systems: the root node gets the (mirroring) axis matrix and nothing else is done about winding; untested against ufbx. `Transform` is assumed to be bone-relative as above; an exporter that writes the mesh's world matrix there instead would skin wrongly (ufbx makes the same assumption, but this has only been checked on our own fixtures).
- No repair of damaged files. ufbx recovers from many malformed files; ours rejects them. Big-endian files, zlib checksums (Adler-32 is not verified), encrypted FBX and streaming are not supported.
- Speed (section "Speed and memory" below; Linux only, a busy shared machine): since the triangulation and the clip keys moved into the import steps (2026-11-16), the reader alone is about 0.82 times ufbx's time on the real files together and 0.89 times on the large files, the whole import about 0.95 and 0.78 times, with less peak memory everywhere measured. Single files are still slower on the reader alone in some runs: the binary `pyramob.fbx` (1.04x to 1.22x, inflate) and the ASCII grid (up to 1.21x, number parsing).

## Speed and memory

Measured on 2026-11-16 with `bench_fbx_reader` (the last row of the step table and the tables after it: the same day, later, after steps 10 to 14) (`tests/bench_fbx_reader.cpp`; how to run it: `docs/try_it.md`, "FBX reader
speed and memory"). Linux, GCC 13, Release (`-O3`, no `-march`), one core of a shared 4-core cloud machine while other
builds ran, so a single number can be off by 10 to 20 % (the ufbx columns moved by that much between runs of the same
code). **Not measured on Windows.**

What each column is: **load** is the reader alone, file bytes already in memory, into its own structures
(`okay::fbx::LoadMemory`: element tree plus the scene build, against `ufbx_load_memory` with the engine's ufbx options;
until step 15 that build also triangulated the meshes and made the clip keys, since step 16 those are import steps, as
they are on the ufbx side). **Import** is `ImportModelScene` through the seam, with our reader switched on and off: file read, reader,
GameObjects, meshes, skins and clips, the work an editor import does. **Peak** is the most heap the call had allocated at one
time, counted by replacing `malloc`/`free` in the bench (so ufbx's C allocations and our `new` count the same way). Times
are medians of at least 9 runs.

Files: the 42 readable FBX files of the outside corpus (`docs/model_corpus.md`; the 43rd is the damaged file both reject),
the 12 fixtures, and four large synthetic files from `scripts/gen_fbx_bench.py` (not committed): a 204,800-triangle grid
(binary 7.5), a 51,200-triangle grid in ASCII, 3,000 nodes each with its own box, and a 64-bone skinned rig with 1,800-key
curves on every bone (345,600 keys, 60 seconds).

**Totals, step by step** (sum of the medians over the set; "start" is the branch this work started from, which already had
the first six fixes listed further down; each later row is a full bench run after that commit):

| After | Real + fixtures: our load / ufbx | Real: our import / ufbx | Synthetic: our load / ufbx | Synthetic: our import / ufbx | Largest peak, our load / ufbx (real; synthetic) | Largest import peak, ours / ufbx (real; synthetic) |
|---|---:|---:|---:|---:|---:|---:|
| start (`2f60bda6`) | 77.4 / 26.9 ms (2.87x) | 108.1 / 56.5 ms (1.91x) | 219.6 / 70.4 ms (3.12x) | 341.5 / 243.5 ms (1.40x) | 9.5 / 3.8 MB; 56.4 / 29.3 MB | 13.2 / 24.1 MB; 83.2 / 120.3 MB |
| 1. tree without copies, typed arrays, ASCII numbers | 59.5 / 25.9 (2.30x) | 86.6 / 53.8 (1.61x) | 191.8 / 68.4 (2.80x) | 288.8 / 223.3 (1.29x) | 9.5 / 3.8; 53.2 / 29.3 | 13.2 / 24.1; 83.2 / 120.3 |
| 2. indexed mesh parts | 69.6 / 28.5 (2.44x) * | 92.4 / 54.7 (1.69x) | 230.1 / 76.6 (3.00x) * | 271.4 / 223.2 (1.22x) | 6.3 / 3.8; 35.0 / 29.3 | 10.1 / 24.1; 39.2 / 120.3 |
| 3 and 4. lean mesh build, inflate fast path, cheaper bake | 55.4 / 26.7 (2.07x) | 71.2 / 53.2 (1.34x) | 170.1 / 63.4 (2.68x) | 230.3 / 219.1 (1.05x) | 4.9 / 3.8; 35.0 / 29.3 | 8.7 / 24.1; 39.2 / 120.3 |
| 5 to 7. bulk clip keys, corner batches, smaller tree, small zlib arrays, ASCII digit runs | 50.6 / 26.0 (1.94x) | 62.8 / 54.8 (1.15x) | 161.3 / 70.4 (2.29x) | 190.2 / 241.7 (0.79x) | 5.1 / 3.8; 30.0 / 29.3 | 8.8 / 24.1; 33.0 / 120.3 |
| 8 and 9. flat id tables, curve storage | 48.0 / 25.4 (1.89x) | 59.7 / 52.9 (1.13x) | 136.3 / 61.1 (2.23x) | 170.6 / 210.4 (0.81x) | 5.1 / 3.8; 28.4 / 29.3 | 8.8 / 24.1; 31.4 / 120.3 |
| 10 to 14. exact ASCII numbers, curve sets, smaller tree, packed ASCII arrays, Base64 and Euler (**final**, branch `claude2/own-fbx-faster-2`; middle of three runs) | 43.7 / 25.2 (1.74x) | 56.4 / 52.2 (1.08x) | 129.4 / 65.3 (1.98x) | 176.3 / 215.4 (0.82x) | 3.8 / 3.8; 25.0 / 29.3 | 7.5 / 24.1; 28.0 / 120.3 |
| 15. keys at the file's own key times (branch `claude2/own-fbx-file-keys`, **changes the clips**, section "Animation keys"; middle of three runs, against a re-run of row 14 that gave 1.14x) | 42.0 / 24.5 (1.72x) | 51.3 / 53.7 (**0.96x**) | 140.4 / 62.8 (2.24x) | 167.7 / 220.8 (0.76x) | 3.8 / 3.8; 25.0 / 29.3 | 7.5 / 24.1; 28.0 / 120.3 |
| 16 to 21. triangles and clip keys made by the import, mesh arrays handed over, faster inflate (branch `claude2/own-fbx-faster-3`, **changes what `LoadMemory` returns**, see below; middle of three runs for each column, a machine with a load of 4 to 6 on 4 cores) | 27.1 / 33.1 (**0.82x**; 0.79x to 0.95x) | 57.5 / 60.7 (**0.95x**; 0.77x to 1.01x) | 66.4 / 74.6 (**0.89x**; 0.85x to 0.89x) | 225.3 / 290.5 (0.78x; 0.72x to 0.87x) | 2.6 / 3.8; 24.0 / 29.3 | 6.4 / 24.1; 28.1 / 120.3 |

\* Step 2 alone made the reader slower on big meshes (it now finds equal corners, which the import used to do) and the
import faster; the step after it won the time back.

**Against the targets after steps 16 to 21** (branch `claude2/own-fbx-faster-3`, 2026-11-16; three full runs, the middle
value of each; the machine was shared, so single files moved by up to 40 % between runs, ufbx's numbers as much as ours):

| Target | Real files and fixtures | Large synthetic files |
|---|---|---|
| Reader alone at or below ufbx | **Met in total: 0.82x** (0.79x, 0.82x, 0.95x). Per file not everywhere: the binary `pyramob.fbx` is 1.04x to 1.22x (its zlib arrays: our inflate is still slower than ufbx's on them) | **Met in total: 0.89x** (0.85x to 0.89x). Per file: the rig 0.72x to 0.84x, the 3,000 nodes 0.81x to 0.87x, the binary grid 0.88x to 1.02x, the ASCII grid **0.93x to 1.21x** (not met in two of three runs) |
| Reader peak memory not higher | **Met for every file listed**: binary `pyramob.fbx` 2.14 against 3.21 MB (was 3.66), ASCII `pyramob.fbx` 2.64 against 3.84, the animation-heavy file 1.27 against 1.66 (was 3.5) | **Met for all four**: binary grid 16.4 against 19.8 MB (was 25.0), rig 10.1 / 24.5, ASCII grid 4.5 / 9.2, 3,000 nodes 24.0 / 29.3 |
| Import time at or below ufbx | **Met in the middle run, 0.95x** (0.77x, 0.95x, 1.01x); the animation-heavy file alone is 1.09x to 1.18x (2.2 against 1.9 ms) | **Met: 0.78x** (0.72x to 0.87x); the 3,000 nodes alone 0.96x to 1.13x |
| Import peak memory not higher | **Met**, now also for the animation-heavy file (1.63 against 1.68 MB) | **Met for all four** (grid 25.0 / 120.3, rig 20.8 / 28.3, ASCII grid 7.9 / 30.3, nodes 28.1 / 29.3) |

`--import-digest` (the whole imported scene) is the same on all 58 files before and after these steps, and so is
`--parts-digest` (the scene after the import steps, hashed as `--digest` hashed it before) against the old `--digest`.
`--digest` itself changed on purpose: what the reader returns is different now (polygons instead of triangles, clips
without keys until `BuildClips`), and it now hashes the polygon arrays. **The comparison is fairer now, not just faster:**
ufbx's `ufbx_load_memory` never triangulated or made clip keys either (the engine's ufbx path does both in its import),
so "reader alone" now measures the same work on both sides; the import columns still include everything.

**Before steps 16 to 21** (after step 14, the middle of three full runs; the three runs gave 1.07x, 1.08x and 1.15x for the
real-file import):

| Target | Real files and fixtures | Large synthetic files |
|---|---|---|
| Import time at or below ufbx | After step 14: **not met, 1.08x** in total (56.4 against 52.2 ms; 1.07x to 1.15x over three runs); `anims_with_full_rotations_between_keys.fbx` alone was 7.2 against 1.7 ms (the 30 Hz bake). After step 15 (the file's own keys): **met in the middle run, 0.96x** (51.3 against 53.7 ms; 0.94x, 0.96x and 1.01x over three runs); that file is 1.9 against 1.8 ms | **Met: 0.82x** in total; per file the binary grid 0.64x, the ASCII grid 0.86x and the rig 0.94x are below; the 3,000 nodes are at 1.07x in this run (0.88x to 1.07x over the runs of this work) |
| Import peak memory not higher | **Met** for the largest file (7.5 against 24.1 MB); the animation-heavy file is above (4.6 against 1.7 MB after step 14, 1.8 against 1.7 MB after step 15) | **Met for all four** (was not for two): grid 27.4 / 120.3, ASCII grid 8.6 / 30.3, 3,000 nodes 28.0 / 29.3 (was 30.6), rig 24.7 / 28.3 MB (was 31.4) |
| Reader alone at or below ufbx | **Not met: 1.74x** (was 1.89x) | **Not met: 1.98x** (was 2.23x) |
| Reader peak memory not higher | **Met for the largest real file**: `pyramob.fbx` (ASCII) 3.79 against 3.84 MB (was 5.1); the binary `pyramob.fbx` is still above (3.66 against 3.21 MB), and so is the animation-heavy file (3.5 against 1.7 MB) | Met for three of four (rig 21.7 / 24.5, ASCII grid 5.2 / 9.2, 3,000 nodes 23.8 / 29.3); not for the binary grid (25.0 against 19.8 MB) |

**The large files after steps 16 to 21** (the middle run; the other two runs are in the targets table above):

| File | KB | Our load | ufbx load | Ratio | Our peak | ufbx peak | Our import | ufbx import | Ratio | Import peak ours / ufbx |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| grid, 204,800 triangles, binary | 2,488 | 29.9 ms | 34.0 ms | 0.88x | 16.4 MB | 19.8 MB | 77.6 ms | 122.8 ms | 0.63x | 25.0 / 120.3 MB |
| grid, 51,200 triangles, ASCII | 3,486 | 12.5 ms | 11.1 ms | 1.12x | 4.5 MB | 9.2 MB | 21.0 ms | 27.2 ms | 0.77x | 7.9 / 30.3 MB |
| 3,000 nodes and boxes | 4,262 | 25.6 ms | 31.4 ms | 0.81x | 24.0 MB | 29.3 MB | 54.9 ms | 52.7 ms | 1.04x | 28.1 / 29.3 MB |
| 64-bone rig, 345,600 keys | 3,127 | 25.9 ms | 34.0 ms | 0.76x | 10.1 MB | 24.5 MB | 71.8 ms | 87.9 ms | 0.82x | 20.8 / 28.3 MB |
| `pyramob.fbx` (binary, the largest real file) | 826 | 5.7 ms | 4.7 ms | 1.22x | 2.1 MB | 3.2 MB | 10.8 ms | 12.0 ms | 0.90x | 3.7 / 24.1 MB |
| `pyramob.fbx` (ASCII) | 3,798 | 6.2 ms | 10.0 ms | 0.62x | 2.6 MB | 3.8 MB | 17.1 ms | 16.6 ms | 1.03x | 6.4 / 24.1 MB |
| `anims_with_full_rotations_between_keys.fbx` | 359 | 0.47 ms | 0.60 ms | 0.79x | 1.3 MB | 1.7 MB | 2.2 ms | 1.9 ms | 1.18x | 1.6 / 1.7 MB |

**The large files** (after step 14; same run as the "before" totals above):

| File | KB | Our load | ufbx load | Ratio | Our peak | ufbx peak | Our import | ufbx import | Ratio | Import peak ours / ufbx |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| grid, 204,800 triangles, binary | 2,488 | 48.1 ms | 17.3 ms | 2.78x | 25.0 MB | 19.8 MB | 61.2 ms | 95.1 ms | 0.64x | 27.4 / 120.3 MB |
| grid, 51,200 triangles, ASCII | 3,486 | 16.0 ms | 8.1 ms | 1.99x | 5.2 MB | 9.2 MB | 21.5 ms | 25.0 ms | 0.86x | 8.6 / 30.3 MB |
| 3,000 nodes and boxes | 4,262 | 22.8 ms | 19.7 ms | 1.16x | 23.8 MB | 29.3 MB | 34.2 ms | 31.8 ms | 1.07x | 28.0 / 29.3 MB |
| 64-bone rig, 345,600 keys | 3,127 | 42.5 ms | 20.3 ms | 2.10x | 21.7 MB | 24.5 MB | 59.4 ms | 63.5 ms | 0.94x | 24.7 / 28.3 MB |
| `pyramob.fbx` (binary, the largest real file) | 826 | 10.3 ms | 4.4 ms | 2.33x | 3.7 MB | 3.2 MB | 12.7 ms | 12.1 ms | 1.05x | 4.5 / 24.1 MB |
| `pyramob.fbx` (ASCII) | 3,798 | 9.7 ms | 5.3 ms | 1.82x | 3.8 MB | 3.8 MB | 12.2 ms | 12.6 ms | 0.97x | 7.5 / 24.1 MB |
| `anims_with_full_rotations_between_keys.fbx` | 359 | 4.0 ms | 0.56 ms | 7.1x | 3.5 MB | 1.7 MB | 7.2 ms | 1.7 ms | 4.2x | 4.6 / 1.7 MB |

**What was slow, and fixed.** Each change keeps the output identical: `bench_fbx_reader --digest` hashes every field our
reader produces, bit for bit, and `--import-digest` hashes the whole imported scene as the scene file writes it (without
the random object guids); both hashes are the same on all 58 files before and after every step, and the agreement tests
below pass unchanged. Where a change is an arithmetic shortcut it is also checked against the plain code on random inputs
by `fbx_exact` (below).

1. **Key reduction was quadratic on held tracks** (`AnimationCurve::Reduce`, shared by every import path): extending a
   straight segment re-checked every key in it, so a constant track of n keys cost n²/2 checks. Ours bakes every channel of
   an animated node, so most tracks are constant: 86 % of the rig import was here. A run of equal values now skips the
   re-check (the result is provably the same; `anim_clip_tools` compares it with the plain rule on 400 random curves with
   flat runs, signed zeros, infinities, NaN and repeated times). The rig import went from 928 ms to 92 ms.
2. **Mesh corners grew by doubling.** Each part's corner list is allocated once at its final size.
3. **Vertex de-duplication allocated a string per corner** in the own import path; it is now an open-addressing table of
   indices (same first-occurrence order).
4. **zlib arrays grew by doubling and were zero-filled twice**: the decoded size is known from the array header and is now
   allocated once.
5. **ASCII numbers** went through a `std::string` and `strtod` each; plain decimals up to 15 digits with a power of ten up
   to 22 are converted exactly by one multiplication or division (Clinger's observation, 1990), anything else still
   goes to `strtod`.
6. **Smaller ones:** pre- and post-rotation once per node for a bake; the shared inflate refills its bit buffer 8 bytes at a
   time; `LoadFile` reads the file in one call.
7. **The element tree copied everything and widened every number** (step 1). It now points into the file: names, strings,
   raw blobs and uncompressed binary arrays are views of the bytes given to `Parse` (so those bytes must stay alive while
   the `Document` is used; `LoadMemory` and `LoadFile` do that). Arrays keep the type the file stores them in (f32, f64,
   i32, i64, bool, u8) and are read on demand (`Array`), so a float array is no longer twice its size. Elements and
   properties live in two flat arrays of the `Document` (sized once for binary files, by a first walk over the record
   headers); a property is 48 bytes. `Properties70` values are looked up on demand instead of being copied into a map per
   object. ASCII arrays are read in one pass, eight digits at a time.
8. **Meshes were unindexed corners** (step 2): a `Part` now holds the distinct corners (equal byte for byte share one,
   numbered in order of first use) and an index list, and the importer builds one engine vertex per reader vertex (same
   vertices and triangles as before, by the first-use argument in `BuildOwnPart`). The 204,800-triangle grid's corners went
   from 29.5 MB to 5.3 MB of vertices and 2.4 MB of indices. The corner list is still available as `Part::ExpandCorners()`.
9. **Mesh building** (steps 3 and 6): positions and polygon corners are read from the file's arrays where they are used
   instead of per-mesh copies (fresh memory cost more time than the copying: about 0.3 ms per MB on this machine), the
   edge table is built only for a `ByEdge` layer, triangles with file normals skip the polygon set-up, ear clipping keeps
   its corner list on the stack and divides nothing, and corners are made in batches of 2,048 and then looked up (about a
   third faster on big meshes than looking each one up as it is made).
10. **The bake** (step 4): curves remember their last key segment, so samples move forward instead of a binary search per
    sample; matrix products leave out the terms that are exactly zero when every entry is finite (a sum that starts at +0
    is never -0 in round-to-nearest, and adding a zero to such a sum changes nothing), which covers the affine products,
    the translations, the scale, the one-axis rotations of the Euler angles and identity pre/post rotations; nodes
    without offsets or pivots take a short path; a sample whose inputs equal the previous one's is copied. The curves'
    keys are allocated once, without slope arrays when a curve stores no slopes (step 9).
11. **The shared inflate** (`Inflate.hpp`, also used by PNG and HTTP; steps 3 and 6): the middle of a stream runs a loop with
    the reader and output state in locals, refilling only below 48 bits, with 8-byte match copies; the code-length code
    of each dynamic block uses a 128-entry table instead of 1,024 and bit reversal no longer loops (the 3,000-node file has
    9,000 small zlib arrays). The same checks in the same order: `inflate_differential` and `own_http_inflate` pass, also
    under ASan and UBSan.
12. **The own import** adds the baked keys a track at a time (`AnimationClip::AddKeys`, the same result as one `AddKey`
    per key, tracks created in the same order).
13. **Object lookups** (step 8): object ids, connections by parent and by child and the per-object tables are flat
    open-addressing arrays and ranges instead of maps of vectors, the node hierarchy is walked over arrays, and a model's
    transform properties are read in one pass over its P records (the same rule: the last record with a number wins).

14. **ASCII numbers of any length** (step 10): every plain decimal is converted by our own exact method
    (`engine/src/Render/DecimalToDouble.hpp`, section "Exact ASCII numbers" below), not only those of up to 15 digits;
    `strtod` is left for the rare inputs the method cannot decide. The synthetic ASCII grid (Python `repr`, 17 digits)
    reads its tree in about two thirds of the time.
15. **Curve keys share their attribute sets** (step 11): flags and both slopes were stored once per key; keys share the
    file's attribute sets (`KeyAttrRefCount`), so they are now stored once per set, with a set number per key only when
    the keys use more than one set. The 64-bone rig's reader peak went from 28.4 to 21.8 MB.
16. **A smaller element tree** (step 12): an element is 48 bytes instead of 64 (the parse-time indices share the bytes
    of the pointers they become) and a property 40 instead of 48 (its number and its array share their bytes; the type
    says which is in use). The 3,000-node file's import peak went from 30.6 to 28.0 MB.
17. **Smaller ASCII arrays and mesh buffers** (step 13): an ASCII array whose values are all 32-bit integers is kept as
    int32 and one of exact floats as float32 (each value reads back as the same double, checked per value; `-0` stays a
    double); the element and property arrays give back their doubling slack before the scene is built; the corner table
    is at most two thirds full instead of half; the corner batch is 56 bytes a corner instead of 64. The largest real
    file's reader peak (`pyramob.fbx`, ASCII) went from 5.05 to 3.79 MB.
18. **Base64 and Euler** (step 14): embedded ASCII textures are decoded four characters at a time straight into the
    texture (`box.FBX`: about 1.2 to 0.25 ms), and the bake's Euler rotations turn the matrix in place with the same sums
    in the same order (about 12 % off the animation-heavy file's load).

19. **Triangles and clip keys made by the import** (steps 16 and 17, 2026-11-16). `LoadMemory` now returns each mesh as the
    file's polygons (`Mesh::polygons`: positions, the polygon index list and the normal, UV, colour and material layers
    with their mapping and index arrays) and each take as a planned clip (name, start, length; the curves kept in
    `Scene::clipSource`). The import calls `fbx::BuildParts` (the same triangulation and corner sharing as before, moved
    unchanged) and `fbx::BuildClips` (the same key making, moved unchanged into its own object). `ImportModel` (the
    merged single mesh) does not make clip keys at all. The reader still refuses what it refused before: the corner
    budget is counted from the polygon index list and every animation count budget is checked when the clips are
    planned; only the evaluation budget of the key making is checked while the keys are made, as before. This is what
    ufbx's load does too (it keeps polygons and curves; the engine's ufbx import triangulates and bakes).
20. **Mesh arrays handed over, not copied** (step 18): the decoded zlib arrays and parsed ASCII arrays a mesh keeps are
    moved from the `Document` into one store per mesh (one allocation), found by an index the parser records in the
    property's padding (`Prop::own`); arrays that are views into the caller's bytes are copied. The reader's peak on the
    binary grid went from 25.0 to 16.4 MB.
21. **Inflate** (steps 18 to 21; shared with PNG and HTTP, `inflate_differential` and `own_http_inflate` pass, also under
    ASan and UBSan): a Huffman table is only as wide as the longest code when that is under 10 bits (the 3,000-node
    file's 3,000 small blocks fill 32-entry tables instead of 1,024) and is not cleared when the code fills it; the fast
    path takes up to two more literals from the bit buffer before refilling, refills without masking, and leaves out the
    output-cap tests it can never reach there (the general path keeps them); matches 2 to 7 bytes back are copied 8 bytes
    a step; decoded FBX arrays (and ASCII number arrays) are not zero-filled before they are written
    (`fbx::ByteBuffer`, an allocator that leaves new bytes unset). Instructions per file (callgrind): the binary grid's
    load from 184 to 136 million, `pyramob.fbx` (binary) from 46 to 38 million, the 3,000-node file from 178 to 133
    million.
22. **Curve keys read directly** (step 20): `KeyTime` (int64) and `KeyValueFloat` (float) without the general accessor.

**What is still slower, and why:**

- **The binary `pyramob.fbx` and the ASCII grid** are still at or above ufbx on the reader alone in some runs (1.04x to
  1.22x and 0.93x to 1.21x). The first is all inflate: its arrays decode to many short matches, and ufbx's inflate is
  faster than ours on them. The second is ASCII number parsing (17-digit numbers, about 10 ns each).
- **The reader alone was about 2x ufbx** on files with meshes until steps 16 to 21, because our `LoadMemory` triangulated
  every polygon and found the distinct corners, which ufbx's load does not do. That work is now in the import step
  `BuildParts` (item 19).
- **Animation was baked at 30 Hz** for every animated node: 23,040 samples for the 64 nodes of
  `anims_with_full_rotations_between_keys.fbx`, where ufbx keeps the file's keys; that one file was almost all of the
  remaining import gap on the real files. Since step 15 (the owner's decision, section "Animation keys") the clip has keys
  at the file's own key times plus the ones a tolerance needs: 1,216 keys for that file, which now imports in about
  1.9 ms against ufbx's 1.8 ms.
- **Fresh memory is expensive** on this machine (about 0.3 ms per MB the first time it is touched). The binary grid
  inflates 16.4 MB of arrays; ufbx needs about the same, but our tree, scene and tables add their own.
- **ASCII numbers** are now exact for any length (item 14), but the ASCII tree still costs about 10 ns a number on
  this machine; together with the triangulation and corner work the reader alone is about 2x ufbx on the ASCII grid,
  while the whole import is faster than ufbx's.
- **Inflate** decodes one symbol per table lookup; the literal-heavy arrays (vertex positions) decode at about 2.5 ns per
  output byte, faster than the system zlib on the same arrays but likely slower than ufbx's.
- **NaN results**: the shortcuts keep every finite and infinite result bit for bit; which NaN (sign and payload) a product
  of NaNs gives depends on the compiler's operand order, so the old code did not define it either, and `fbx_exact` treats
  any NaN as equal to any NaN.

## Exact ASCII numbers

ASCII FBX files store every number as text. `engine/src/Render/DecimalToDouble.hpp` turns a plain decimal (`12`, `-0.5`,
`1.25e-3`, `0.30000000000000004`, any number of digits) into the double `strtod` gives, bit for bit, in round-to-nearest.
Written for this engine from published descriptions, not from any library's code (CLAUDE.md rule 19a):

- **Short numbers** (at most 2^53 and a power of ten up to 22, as written by most exporters): one correctly rounded
  multiplication or division gives the answer, because both operands are exact doubles (W. D. Clinger, "How to read
  floating point numbers accurately", 1990).
- **The rest**: the idea of D. Lemire, "Number Parsing at a Gigabyte per Second" (2021), from the paper's description:
  the first 19 significant digits `w` are multiplied by a 128-bit approximation of 5^q, and the double's bits are read
  from the top of the 192-bit product. The table holds 5^q for q = -342 to 308 **rounded down** to 128 bits; it is
  generated from exact integers by `scripts/gen_pow5_table.py` (`--check` compares it with the committed
  `Pow5Table.inc`) and re-derived with the test's own big integers. The error bound was worked out here for that table
  and is written next to the code: the product is below the true value by less than 2^64 units, so the result is known
  unless a carry from below could reach the kept bits; exact table entries (0 <= q <= 55) decide ties to even, and for
  the others the remainder is never zero, so there is no tie. Numbers with more than 19 digits are converted twice (as
  `w` and as `w + 1`): when both give the same double, that is the answer. Decimals that are exactly a binary fraction
  with a negative power of ten (`123456789012.000000`) are converted through their integer value.
- **Giving up**: when the product cannot decide, the reader uses `strtod` as before (counted below: mostly exact
  midpoints between two doubles written with more than 19 digits, which no exporter writes).

**Verification** (`tests/test_decimal_to_double.cpp`, ctest `decimal_to_double`): **100,000,000 inputs** compared with
the C library's `strtod` bit for bit on 2026-11-16 (`OKAY_DECIMAL_ITERS=100000000`, about four and a half minutes): doubles printed with 1 to 25
digits and in fixed point, random digit strings of 1 to 40 digits (19 and 20 on purpose), exact midpoints between two
doubles and the same cut short or nudged by one in the last digit, subnormals and the edges of the range (`DBL_MAX` and
the halfway point above it, `2^-1074` and half of it, `1e309`, `1e-400`), mantissas of 50 to 800 digits, garbage tokens
over the number alphabet (the scanner must accept exactly what `strtod` reads whole), and FBX-like values. **0 differ.**
88.04 million were decided by our code; 11.96 million went to `strtod`, 11.94 million of them in the deliberately hard
midpoint group (the 80 hand-picked inputs: 9 go to `strtod`, all exact or nearly exact midpoints with more than 19
digits). The plain C++ fallbacks for the 128-bit product and the leading-zero count (used where the compiler has no
built-in) were checked on 5 million inputs (`-DOKAY_DECIMAL_PORTABLE`); the MSVC `_umul128` branch is not compiled or
run here (the Windows release is built with MinGW, which takes the GCC branch). ctest runs 2 million inputs (about five
seconds); clean under ASan, UBSan and float-cast-overflow.

## Animation keys

**The owner's decision, 2026-11-16 (in chat):** our FBX import keeps the file's own animation key times and adds keys
only where they are needed for accuracy, instead of resampling every animation at 30 Hz. The owner accepted that imported
animations change slightly, within a set tolerance. This is the default of our reader (`BuildOptions::fileKeys`); the
ufbx path is unchanged. **`OKAY_FBX_BAKE30=1`** brings back the 30 Hz bake for comparison (both bench digests are then
identical to before on all 58 files).

**What the clip gets** (`engine/src/Render/FbxScene.cpp`, `BakeNodeFileKeys`; written for this engine from cubic
interpolation and quaternion algebra, no outside code):

1. A key at every key time of the node's curves inside the take (translation, rotation and scaling, every layer of the
   take), and at the take's start and end. The take's span (`LocalStart` / `LocalStop`), the clip length and the values
   before the first and after the last key (held) are as before.
2. A step (constant) key, or two keys at the same time with different values, makes a jump: the clip gets a key 0.2 ms
   before it (or 2^-17 of the clip time when that is more) with the value before the jump, and a key at it. (The engine's
   clips merge keys less than 0.1 ms apart, so a closer pair would become a ramp.) Before, a jump became a ramp of one
   30 Hz frame.
3. Between two neighbouring keys every curve is one cubic piece. The poses at 1/3 and 2/3 of the way are evaluated and
   compared with what the engine plays there (translation and scale linear, rotation the normalised linear blend of the
   two quaternions, signs matched as the importer does). If the segment is outside the tolerances below, it is cut into
   equal parts, as many as the error asks for (an Euler angle's move shrinks in proportion to the distance between keys,
   an error at least with its square), and each part is checked the same way.
4. The values come from the same curve evaluation, layer blending, matrix chain and decomposition as the 30 Hz bake: at
   the same time both give the same numbers bit for bit.

**Tolerances** (`fbx::KeyTolerance`, all can be changed per load):

| What | Value | Why this value |
|---|---|---|
| Translation | 1e-5 m + 1e-4 x the node's largest translation at its keys | a tenth of a millimetre on a bone 1 m from its parent; the floor keeps tiny offsets from asking for endless keys |
| Rotation | 0.05 degrees | far below what anyone can see on a character (a 1 m arm moves under 1 mm) |
| Scale | 1e-4 x max(1, the node's largest scale) | the same relative size as translation |
| Blend shape weight | 0.01 (DeformPercent, 0 to 100) | 1e-4 of the full weight |
| Euler step | at most 45 degrees of any Euler angle between two keys | at least 8 keys a turn, so a quaternion blend never takes the short way round when the file turns the long way (a full turn between two file keys is not a stand-still) |
| Smallest added gap | 1/60 s | added keys stay more than 1/60 s from their neighbours. File keys 1/30 s apart or closer (any file keyed per frame at 30 Hz or faster) get nothing in between; at 24 or 25 Hz a failing segment gets one key in the middle |

How exact the check is: on a node without offsets and pivots the translation and scale between two keys are exact cubic
bounds (the curves' own cubic through the four points, its largest distance from the straight line), and an Euler angle's
move is the exact range of its cubic. Rotations, and every channel of a node with pivots or offsets, are judged from the
poses at 1/3 and 2/3 against **half** the tolerance. The test below then checks the result at 240 samples a second.

**Where the tolerance is not enforced.** Between file keys 1/30 s apart or closer the reader adds nothing: playback there is
linear between the file's own keys, which is what the 30 Hz bake gave on a file keyed at 30 Hz. If the file's curves bend
between such keys, the clip is as far off as linear playback between them is. On the 64-bone synthetic rig (a key every
frame with flat tangents, so the true curve eases in and out every frame) that is up to 0.35 degrees, the same as the 30 Hz
clip. The test checks that in these parts the new clip is never worse than the old 30 Hz clip on that node.

**Accuracy measured** (`fbx_file_keys`, 2026-11-16: the 12 fixtures and the 42 real files; 15 files animated, 119 animated
nodes, 217,033 samples at 240 Hz against the curves themselves):

| File | Keys now | Keys at 30 Hz | Worst error / tolerance | Worst rotation error | 30 Hz clip's worst rotation error |
|---|---:|---:|---:|---:|---:|
| `anims_with_full_rotations_between_keys.fbx` | 1,216 | 23,040 | 0.20 | 0.0098 deg | 0.0000 deg |
| `huesitos.fbx` (25 Hz keys) | 311 | 480 | 0.56 | 0.028 deg | 1.18 deg |
| `animation_with_skeleton.fbx` | 42 | 390 | 0.003 | 0.0001 deg | 0.21 deg |
| `multiple_animations_test.fbx` | 94 | 55 | 0.000 | 0.0000 deg | 0.56 deg |
| `rig_bin75.fbx` / `rig_ascii.fbx` (fixtures) | 87 / 85 | 244 / 244 | 0.04 | 0.002 deg | 0.19 deg |
| the other 9 animated files (`Granate`, `mar_rifle`, `mp5_sil`, `pyramob`, both encodings; `takes_bin`) | 2 to 10 each | 123 to 755 each | 0 | 0 | 0 |
| **all** | **1,878** | **27,294** | **0.56** | 0.028 deg | 1.18 deg |

Every file key time inside a take is a key of the clip (1,548 checked, found independently from the file's own objects
and connections). Translation and scale errors are at rounding level on these files (their animated translations and
scales are linear or held). `multiple_animations_test.fbx` is the one file with more keys than before (94 against 55):
its 30 Hz clip was 0.56 degrees off. The synthetic 64-bone rig (`scripts/gen_fbx_bench.py`, a key every frame) keeps
exactly its 115,200 keys.

**Speed and memory** (`bench_fbx_reader`, three full runs each, before = branch `claude2/own-fbx-faster-2` at
`b7a149ff`, the middle run by the real-file import ratio; same machine and conditions as "Speed and memory"):

| | Before | After |
|---|---:|---:|
| Real files and fixtures: our import / ufbx | 61.8 / 54.3 ms (**1.14x**; runs 1.06x, 1.14x, 1.21x) | 51.3 / 53.7 ms (**0.96x**; runs 0.94x, 0.96x, 1.01x) |
| Real files and fixtures: our reader alone / ufbx | 50.5 / 30.1 ms (1.68x) | 42.0 / 24.5 ms (1.72x) |
| `anims_with_full_rotations_between_keys.fbx`: our import / ufbx | 7.40 / 2.90 ms | 1.90 / 1.77 ms (1.08x) |
| the same file: our reader alone / ufbx | 4.11 / 0.57 ms | 1.27 / 0.58 ms |
| the same file: reader peak, ours / ufbx | 3.52 / 1.66 MB | 1.44 / 1.66 MB |
| the same file: import peak, ours / ufbx | 4.58 / 1.68 MB | 1.80 / 1.68 MB |
| 64-bone rig: our import / ufbx; import peak | 69.2 / 77.0 ms; 24.7 / 28.3 MB | 60.7 / 65.9 ms; 25.1 / 28.3 MB |
| Synthetic files: our import / ufbx | 185.4 / 235.0 ms (0.79x) | 167.7 / 220.8 ms (0.76x) |

The real-file import is now **at or below ufbx's time in two of three runs (0.94x, 0.96x, 1.01x)**. The one file is still
a little slower than ufbx (1.08x: about 20 keys a node, each with its rotation checked) and its import peak is 0.1 MB
above ufbx's. A shared machine moves single numbers by 10 to 20 %. Not measured on Windows.

## Tests

- `fbx_reader` (`tests/test_fbx_reader.cpp`): inflater against zlib output (stored, fixed, dynamic, limits, truncation); hand-computed checks on each fixture (axis/unit conversion, ear clipping area and winding, materials, skin weights and inverse binds, blend shape, baked rotation/translation/scale values, pivot and pre/post-rotation transform against an independent evaluation); a hand-computed Hermite check (two keys with different outgoing and incoming slopes, sampled at several times, against the spline formula, through our reader's 30 Hz bake, through its default clip played between its keys (within the key tolerance) and through ufbx); then **every fixture in `tests/assets/fbx` imported through ufbx and through our reader** and compared: object, mesh, skinned-mesh, joint, vertex, triangle, blend shape, clip and track counts, object names, world-space bounding box and surface area, skin weights, joint indices and inverse binds, clip samples within tolerance, and the single-mesh `ImportModel` path.
- `fbx_file_keys` (`tests/test_fbx_file_keys.cpp`): standalone, no engine code. For every animated node and blend weight of every fixture, of every real file when `OKAY_MODEL_CORPUS` is set, and of files given on the command line: the default clip, played the way the engine plays it, against the curves (the same reader's bake at 240 Hz) within the tolerances where the reader may add keys, and no worse than the 30 Hz clip between file keys 1/30 s apart or closer; every file key time inside a take is a key (found independently by walking the file's stacks, layers, curve nodes and connections); key times strictly increasing from 0 to the clip length; key counts printed per file and not higher in total than at 30 Hz. Clean under ASan, UBSan and float-cast-overflow (standalone build, with the real files). Section "Animation keys".
- `fbx_import_limits` (`tests/test_fbx_import_limits.cpp`): hostile files through `ImportModelScene` on the ufbx path and with our reader switched on: a 100000-model parent chain, a chain just past the depth cap, too many stacks, layers and curves, and many takes sharing one curve; each must fail with `ok == false` and a legal deep chain must still import. Files go to the temp directory.
- `fbx_hostile` (`tests/test_fbx_hostile.cpp`): standalone, no engine code. Every truncation of every fixture, bad end offsets, huge property/array/string counts, bad encodings, 200000-deep nesting in binary and ASCII, a decompression bomb, an array-byte budget shared across the file, a 5000-deep, a 100000-deep and a cyclic model hierarchy, animation counts (stacks, layers, curves, keys, a claimed 10-million-key curve) and the whole-file bake budget with many takes sharing one curve, a 100000-corner polygon, out-of-range polygon indices, and a deterministic mutation fuzz (`OKAY_FBX_FUZZ_ITERS=20000` for a longer run). Run under `-fsanitize=address,undefined` (standalone build) it was clean, including 200000 mutations. The engine-linked `fbx_reader` and `fbx_import_limits` tests were later run under the same sanitizers built with the whole engine, and `fbx_reader` under valgrind, with no finding (`docs/model_corpus.md`). With the file's own keys (2026-11-16) the fuzz runs through the new key code by default; 30,000 mutations per fixture under ASan, UBSan and float-cast-overflow were clean, and the 20,000-sample cap of the bake budget check is now checked with `fileKeys = false` (it is a 30 Hz property).
- `fbx_reader` also compares, for every fixture and real file, what both paths put on each mesh object as material (colour, emission, metalness, glossiness, texture and normal-map file name), and has reader-level checks for the PBR property sets (`pbr_bin.fbx`) and the unconnected-Video picture (`orphan_video_bin.fbx`).
- `fbx_reader` also reads every real file of the outside corpus when `OKAY_MODEL_CORPUS` is set (`docs/model_corpus.md`), with slightly looser tolerances for vertex count (0.5 %), surface area (0.2 %) and clip samples (0.015) than for our fixtures, and `model_corpus` records the counts of each real file in `tests/data/model_corpus_manifest.tsv`.
- `decimal_to_double` (`tests/test_decimal_to_double.cpp`): the exact decimal-to-double conversion against `strtod`, bit for bit, and its table re-derived with big integers (section "Exact ASCII numbers").
- Fixtures `pbr_bin.fbx` and `orphan_video_bin.fbx` came from the pre-flip verification. Fixtures `takes_bin.fbx` (three takes with their own spans, a `c` thumbnail) and `bridge_bin.fbx` (a polygon with a bridged hole) were added for the bugs the real files showed.
- The hostile test was re-run with `-fsanitize=address,undefined,float-cast-overflow` (standalone build, 30,000 mutations per fixture): clean after the integer-conversion fix; the engine-linked tests were then run too (see above).
- Regenerate fixtures with `python3 scripts/gen_fbx_fixtures.py`. They are 2 to 12 KB each.
- Since the triangulation and the clip keys moved into the import steps (2026-11-16), the tests that look at parts or keys
  call them after loading: `fbx_reader` (its `LoadBuilt` helper: `LoadFile`, `BuildParts`, `BuildClips`), `fbx_file_keys`
  (`BuildClips`) and `fbx_hostile` (both, after every load that succeeds, so the fuzz still runs through the
  triangulation and the key code). No check was added or removed; the counts are the same (2,559, 217,758 and 153).
- `fbx_exact` (`tests/test_fbx_exact.cpp` with `tests/test_fbx_exact_numbers.cpp`): white-box, it compiles `FbxScene.cpp` and `FbxReader.cpp` itself. The speed shortcuts against the plain code they replaced, on random inputs that include -0, infinities, NaN and huge values: the matrix products without their zero terms, the Euler rotations, `LocalMatrix` and its short path (300,000 cases each), curve evaluation with a remembered key segment and with flags and slopes kept per attribute set, against the plain search over one copy per key (600,000 samples), Base64 four characters at a time against the bit-by-bit decoder (20,000 strings with junk characters), the one-pass ASCII number conversion (400,000 tokens: same accepted set, same values, and the values are `strtod`'s) and ASCII arrays kept as int32 or float32 (3,000 arrays: every element reads back as `strtod`'s double for its token). Bit for bit, except that any NaN equals any NaN (see "Speed and memory"). About one second; clean under ASan, UBSan and float-cast-overflow.
