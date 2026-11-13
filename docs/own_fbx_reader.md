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

The reader files use only the standard library (no engine headers, no `<mutex>`/`<thread>`), so the hostile-input test builds them on their own under sanitizers.

## Turning it on

- CMake: `-DOKAY_OWN_FBX=ON` makes it the default reader for that build (default `OFF`).
- Run time: environment variable `OKAY_OWN_FBX=1` (or `0`) overrides the build default; `okay::fbx::SetOwnReaderEnabled(true)` does the same from code and is what the tests use.
- When our reader is selected and cannot read a file, the importer falls back to ufbx for that file.

## What it handles

- Binary FBX 7.1 to 7.7 (32-bit offsets below 7500, 64-bit from 7500) and ASCII FBX 7.x. All array property types (`f d l i b` and the byte array `c`), raw and zlib-compressed. Our own inflater: stored, fixed and dynamic Huffman blocks.
- Embedded textures (`Video` / `Content`, raw bytes in binary, base64 in ASCII) and texture file names.
- Geometry (`Mesh`): positions, polygon vertex indices with the negative end marker, normals, UVs, vertex colors, per-polygon material ids. Mapping modes `ByPolygonVertex`, `ByVertice`, `ByPolygon`, `AllSame` and `ByEdge`; reference modes `Direct` and `IndexToDirect`. Missing normals are generated flat, one per polygon (ufbx does the same without smoothing data; smoothing groups are not used). Polygons with fewer than 3 corners or out-of-range indices are skipped.
- Triangulation: triangles directly, quads and n-gons by ear clipping on the polygon's dominant plane (concave polygons keep their winding), fan fallback for degenerate input and for polygons over 256 corners.
- Meshes split per material slot, in ascending slot order.
- Materials: diffuse color, emissive color and factor, shininess mapped to roughness (approximate), diffuse and normal-map texture connections.
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
- Materials: only Lambert/Phong style properties (`DiffuseColor`, `EmissiveColor`, `EmissiveFactor`, `ShininessExponent`, `NormalMap` textures). No `Bump`, specular, opacity or PBR/"Stingray"/"3dsMax|" property sets, and no metalness. Our roughness mapping from shininess is a guess and will not match ufbx's.
- Skinning: only the first skin deformer; dual-quaternion skinning type and additive cluster link modes are ignored; for a geometry shared by several nodes the first node's geometric transform is used for the inverse binds.
- Blend shapes: in-between shapes (`FullWeights` with several targets) are not interpolated, only the last target is used.
- Animation: constant (step) curves are baked at 30 Hz, so a jump becomes a one-frame ramp (ufbx emits a key pair at the jump); we emit every channel of an animated node at every sample (ufbx emits only animated channels with fewer keys); custom tangent weights and velocities are ignored; TCB tangents are treated like auto; auto-clamp is approximated; layer blending is simplified (first layer overrides, later layers add or lerp by weight, rotations are blended as Euler angles); only translation, rotation, scaling and `DeformPercent` are animated (not pivots, pre/post rotation or other properties); a take plays the span its `LocalStart` / `LocalStop` give (several takes can share one set of curves), else the range of its keys; ufbx keeps keys beyond the span in its clip, ours cuts there; a take whose curves animate only custom properties gives no clip (ufbx gives a constant one); no key reduction (ufbx reduces keys; the engine's `OptimizeClips` runs afterwards on both paths).
- Left-handed source axis systems: the root node gets the (mirroring) axis matrix and nothing else is done about winding; untested against ufbx. `Transform` is assumed to be bone-relative as above; an exporter that writes the mesh's world matrix there instead would skin wrongly (ufbx makes the same assumption, but this has only been checked on our own fixtures).
- No repair of damaged files. ufbx recovers from many malformed files; ours rejects them. Big-endian files, zlib checksums (Adler-32 is not verified), encrypted FBX and streaming are not supported.
- Not benchmarked. Nothing has been measured against ufbx for speed or memory.

## Tests

- `fbx_reader` (`tests/test_fbx_reader.cpp`): inflater against zlib output (stored, fixed, dynamic, limits, truncation); hand-computed checks on each fixture (axis/unit conversion, ear clipping area and winding, materials, skin weights and inverse binds, blend shape, baked rotation/translation/scale values, pivot and pre/post-rotation transform against an independent evaluation); a hand-computed Hermite check (two keys with different outgoing and incoming slopes, sampled at several times, against the spline formula, through our reader and through ufbx); then **every fixture in `tests/assets/fbx` imported through ufbx and through our reader** and compared: object, mesh, skinned-mesh, joint, vertex, triangle, blend shape, clip and track counts, object names, world-space bounding box and surface area, skin weights, joint indices and inverse binds, clip samples within tolerance, and the single-mesh `ImportModel` path.
- `fbx_import_limits` (`tests/test_fbx_import_limits.cpp`): hostile files through `ImportModelScene` on the ufbx path and with our reader switched on: a 100000-model parent chain, a chain just past the depth cap, too many stacks, layers and curves, and many takes sharing one curve; each must fail with `ok == false` and a legal deep chain must still import. Files go to the temp directory.
- `fbx_hostile` (`tests/test_fbx_hostile.cpp`): standalone, no engine code. Every truncation of every fixture, bad end offsets, huge property/array/string counts, bad encodings, 200000-deep nesting in binary and ASCII, a decompression bomb, an array-byte budget shared across the file, a 5000-deep, a 100000-deep and a cyclic model hierarchy, animation counts (stacks, layers, curves, keys, a claimed 10-million-key curve) and the whole-file bake budget with many takes sharing one curve, a 100000-corner polygon, out-of-range polygon indices, and a deterministic mutation fuzz (`OKAY_FBX_FUZZ_ITERS=20000` for a longer run). Run under `-fsanitize=address,undefined` (standalone build) it was clean, including 200000 mutations. The engine-linked `fbx_reader` and `fbx_import_limits` tests were not run under sanitizers.
- `fbx_reader` also reads every real file of the outside corpus when `OKAY_MODEL_CORPUS` is set (`docs/model_corpus.md`), with slightly looser tolerances for vertex count (0.5 %), surface area (0.2 %) and clip samples (0.015) than for our fixtures, and `model_corpus` records the counts of each real file in `tests/data/model_corpus_manifest.tsv`.
- Fixtures `takes_bin.fbx` (three takes with their own spans, a `c` thumbnail) and `bridge_bin.fbx` (a polygon with a bridged hole) were added for the bugs the real files showed.
- The hostile test was re-run with `-fsanitize=address,undefined,float-cast-overflow` (standalone build, 30,000 mutations per fixture): clean after the integer-conversion fix; the engine-linked `fbx_reader` test still has not been run under sanitizers.
- Regenerate fixtures with `python3 scripts/gen_fbx_fixtures.py`. They are 2 to 12 KB each.
