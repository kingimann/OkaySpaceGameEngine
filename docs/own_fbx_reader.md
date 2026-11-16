# Our own FBX reader (`okay::fbx`)

Status: **side by side, off by default.** ufbx (`engine/third_party/ufbx`) is still the default FBX reader and is still compiled in. Nothing has been removed. This follows stage 2 of `docs/own_code_roadmap.md`: our own backend behind the existing seam, selectable, and promoted only after it has earned it.

## What it is

Written from the public description of the FBX format, with no code from ufbx, the FBX SDK or Assimp.

| File | What |
|---|---|
| `engine/include/okay/Render/FbxReader.hpp` | Public API: `okay::fbx::Parse`, `Build`, `LoadFile`, `Inflate`, the `Scene` result structs, the on/off switch |
| `engine/src/Render/FbxReader.cpp` | Binary FBX 7.x reader (32-bit and 64-bit record offsets), ASCII FBX reader; zlib arrays go through the engine's one inflate. The element tree points into the parsed bytes (keep them alive while the `Document` is used) |
| `engine/src/Render/FbxScene.cpp` | Element tree to scene: nodes, transforms, meshes, skins, blend shapes, materials, textures, baked animation |
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
- Slower than ufbx on the reader alone (about 2 times on files with meshes, because our reader also triangulates and finds the distinct corners, which ufbx leaves to the import) and on files with long animations (we bake every animated node at 30 Hz). The whole import is about 1.08 times ufbx's time on the real files together (0.98 without the one animation-heavy file) and 0.82 times on the large files (section "Speed and memory" below).

## Speed and memory

Measured on 2026-11-16 with `bench_fbx_reader` (the last row of the step table and the tables after it: the same day, later, after steps 10 to 14) (`tests/bench_fbx_reader.cpp`; how to run it: `docs/try_it.md`, "FBX reader
speed and memory"). Linux, GCC 13, Release (`-O3`, no `-march`), one core of a shared 4-core cloud machine while other
builds ran, so a single number can be off by 10 to 20 % (the ufbx columns moved by that much between runs of the same
code). **Not measured on Windows.**

What each column is: **load** is the reader alone, file bytes already in memory, into its own structures
(`okay::fbx::LoadMemory`: element tree plus the scene build with baked clips, against `ufbx_load_memory` with the engine's
ufbx options). **Import** is `ImportModelScene` through the seam, with our reader switched on and off: file read, reader,
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

\* Step 2 alone made the reader slower on big meshes (it now finds equal corners, which the import used to do) and the
import faster; the step after it won the time back.

**Against the targets** (the owner's: our reader and our import at or below ufbx's time, with peak memory no higher;
after step 14, the middle of three full runs; the three runs gave 1.07x, 1.08x and 1.15x for the real-file import):

| Target | Real files and fixtures | Large synthetic files |
|---|---|---|
| Import time at or below ufbx | **Not met: 1.08x** in total (56.4 against 52.2 ms; 1.07x to 1.15x over three runs). Without `anims_with_full_rotations_between_keys.fbx` the total is **0.98x** (49.1 against 50.4 ms); that file alone is 7.2 against 1.7 ms (the 30 Hz bake, see the proposal below) | **Met: 0.82x** in total; per file the binary grid 0.64x, the ASCII grid 0.86x and the rig 0.94x are below; the 3,000 nodes are at 1.07x in this run (0.88x to 1.07x over the runs of this work) |
| Import peak memory not higher | **Met** for the largest file (7.5 against 24.1 MB); the animation-heavy file is above (4.6 against 1.7 MB) | **Met for all four** (was not for two): grid 27.4 / 120.3, ASCII grid 8.6 / 30.3, 3,000 nodes 28.0 / 29.3 (was 30.6), rig 24.7 / 28.3 MB (was 31.4) |
| Reader alone at or below ufbx | **Not met: 1.74x** (was 1.89x) | **Not met: 1.98x** (was 2.23x) |
| Reader peak memory not higher | **Met for the largest real file**: `pyramob.fbx` (ASCII) 3.79 against 3.84 MB (was 5.1); the binary `pyramob.fbx` is still above (3.66 against 3.21 MB), and so is the animation-heavy file (3.5 against 1.7 MB) | Met for three of four (rig 21.7 / 24.5, ASCII grid 5.2 / 9.2, 3,000 nodes 23.8 / 29.3); not for the binary grid (25.0 against 19.8 MB) |

**The large files** (final, after step 14; same run as the totals above):

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

**What is still slower, and why:**

- **The reader alone is about 2x ufbx** on files with meshes. Our `LoadMemory` triangulates every polygon, finds the
  distinct corners and builds the parts per material; ufbx's load does none of that (it keeps polygons and per-attribute
  indices and the ufbx import path triangulates and de-duplicates afterwards), which is why our whole import is at or below
  ufbx's on the big meshes while our reader alone is not. Moving that work out of the reader would change what `Scene`
  holds; it was not done.
- **Animation is baked at 30 Hz** for every animated node: 23,040 samples for the 64 nodes of
  `anims_with_full_rotations_between_keys.fbx` (each a Euler rotation with three sines and cosines, a matrix chain and a
  decomposition), where ufbx keeps the file's keys. This one file is almost all of the remaining import gap on the real
  files. Emitting only the needed keys would change the output, so it is not part of this work. A proposal to keep the file's own keys instead is in the section "Proposal: the file's own keys instead of the 30 Hz bake" below; it is the owner's decision.
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

## Proposal: the file's own keys instead of the 30 Hz bake

**For the owner to decide; not done.** Today every animated node is sampled 30 times a second over the whole take, and
the importer then drops keys that lie on a straight line. `anims_with_full_rotations_between_keys.fbx` has 192 curves
with **2 keys each** (start and end), and we evaluate 23,040 samples (64 nodes x 360): almost all of the remaining
import gap on the real files is that one file (our import about 7 ms against ufbx's 1.7 ms).

**What would change.** The clip would get keys at the file's own key times (exact times, not rounded to 1/30 s) plus,
where needed, extra keys in between: the engine plays tracks linearly per component (rotations as quaternion
components), while FBX interpolates each Euler angle with its own curve (constant, linear or cubic Hermite from the
slopes). Two keys a full turn apart are the same orientation, so keys at the file's times alone would make this very
file stand still; between two file keys we would add keys until the linear playback stays within a set tolerance of the
true curve (for example 0.01 degrees and 1e-5 of the scale, checked by evaluating the curve between the keys), at
least one per half turn of an angle. A constant (step) curve would get a key pair at the jump instead of today's
one-frame ramp, which is what ufbx emits. The clips' key counts and values would differ from today's (the import digest
changes), so the agreement tests would compare sampled poses with a tolerance instead.

**Why it is arguably better.** Keys land where the artist put them, not on the nearest 30 Hz frame (a 24 or 60 fps
animation is no longer resampled); motion faster than 1/30 s between keys is no longer cut off; the error between keys
has a stated bound (today it has none: between two samples playback is linear whatever the curve did); clips get
smaller; and the import does work in proportion to the keys and the curvature instead of to the take's length.

**Expected speed.** For this file, about 20 keys per node instead of 360 samples: the bake drops from about 4 ms to
well under 1 ms, and the import of the file from about 7 ms to about 2.5 ms (ufbx 1.7 ms). The real files' import total
would then be at about 1.0x ufbx (it is 1.00x without this file today). On the rig (1,800 keys per curve over 60
seconds, so the 30 Hz grid is the key grid) nothing would change. These are estimates from the counts above, not
measurements.

## Tests

- `fbx_reader` (`tests/test_fbx_reader.cpp`): inflater against zlib output (stored, fixed, dynamic, limits, truncation); hand-computed checks on each fixture (axis/unit conversion, ear clipping area and winding, materials, skin weights and inverse binds, blend shape, baked rotation/translation/scale values, pivot and pre/post-rotation transform against an independent evaluation); a hand-computed Hermite check (two keys with different outgoing and incoming slopes, sampled at several times, against the spline formula, through our reader and through ufbx); then **every fixture in `tests/assets/fbx` imported through ufbx and through our reader** and compared: object, mesh, skinned-mesh, joint, vertex, triangle, blend shape, clip and track counts, object names, world-space bounding box and surface area, skin weights, joint indices and inverse binds, clip samples within tolerance, and the single-mesh `ImportModel` path.
- `fbx_import_limits` (`tests/test_fbx_import_limits.cpp`): hostile files through `ImportModelScene` on the ufbx path and with our reader switched on: a 100000-model parent chain, a chain just past the depth cap, too many stacks, layers and curves, and many takes sharing one curve; each must fail with `ok == false` and a legal deep chain must still import. Files go to the temp directory.
- `fbx_hostile` (`tests/test_fbx_hostile.cpp`): standalone, no engine code. Every truncation of every fixture, bad end offsets, huge property/array/string counts, bad encodings, 200000-deep nesting in binary and ASCII, a decompression bomb, an array-byte budget shared across the file, a 5000-deep, a 100000-deep and a cyclic model hierarchy, animation counts (stacks, layers, curves, keys, a claimed 10-million-key curve) and the whole-file bake budget with many takes sharing one curve, a 100000-corner polygon, out-of-range polygon indices, and a deterministic mutation fuzz (`OKAY_FBX_FUZZ_ITERS=20000` for a longer run). Run under `-fsanitize=address,undefined` (standalone build) it was clean, including 200000 mutations. The engine-linked `fbx_reader` and `fbx_import_limits` tests were later run under the same sanitizers built with the whole engine, and `fbx_reader` under valgrind, with no finding (`docs/model_corpus.md`).
- `fbx_reader` also compares, for every fixture and real file, what both paths put on each mesh object as material (colour, emission, metalness, glossiness, texture and normal-map file name), and has reader-level checks for the PBR property sets (`pbr_bin.fbx`) and the unconnected-Video picture (`orphan_video_bin.fbx`).
- `fbx_reader` also reads every real file of the outside corpus when `OKAY_MODEL_CORPUS` is set (`docs/model_corpus.md`), with slightly looser tolerances for vertex count (0.5 %), surface area (0.2 %) and clip samples (0.015) than for our fixtures, and `model_corpus` records the counts of each real file in `tests/data/model_corpus_manifest.tsv`.
- `decimal_to_double` (`tests/test_decimal_to_double.cpp`): the exact decimal-to-double conversion against `strtod`, bit for bit, and its table re-derived with big integers (section "Exact ASCII numbers").
- Fixtures `pbr_bin.fbx` and `orphan_video_bin.fbx` came from the pre-flip verification. Fixtures `takes_bin.fbx` (three takes with their own spans, a `c` thumbnail) and `bridge_bin.fbx` (a polygon with a bridged hole) were added for the bugs the real files showed.
- The hostile test was re-run with `-fsanitize=address,undefined,float-cast-overflow` (standalone build, 30,000 mutations per fixture): clean after the integer-conversion fix; the engine-linked tests were then run too (see above).
- Regenerate fixtures with `python3 scripts/gen_fbx_fixtures.py`. They are 2 to 12 KB each.
- `fbx_exact` (`tests/test_fbx_exact.cpp` with `tests/test_fbx_exact_numbers.cpp`): white-box, it compiles `FbxScene.cpp` and `FbxReader.cpp` itself. The speed shortcuts against the plain code they replaced, on random inputs that include -0, infinities, NaN and huge values: the matrix products without their zero terms, the Euler rotations, `LocalMatrix` and its short path (300,000 cases each), curve evaluation with a remembered key segment and with flags and slopes kept per attribute set, against the plain search over one copy per key (600,000 samples), Base64 four characters at a time against the bit-by-bit decoder (20,000 strings with junk characters), the one-pass ASCII number conversion (400,000 tokens: same accepted set, same values, and the values are `strtod`'s) and ASCII arrays kept as int32 or float32 (3,000 arrays: every element reads back as `strtod`'s double for its token). Bit for bit, except that any NaN equals any NaN (see "Speed and memory"). About one second; clean under ASan, UBSan and float-cast-overflow.
