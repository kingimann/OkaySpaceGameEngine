# Model import: who reads which format

All model files come in through one door, `okay::ImportModel` (one merged mesh) and
`okay::ImportModelScene` (a node hierarchy with materials, skins and clips), both in
`okay/Render/ModelImport.hpp`. This page says which reader handles which format and how
the third-party reader (Assimp, optional) is kept behind a seam so it can be replaced one
format at a time. The plan is in `docs/own_code_roadmap.md`.

## Who reads what

| Format | Reader | Scene import (nodes, skins, clips) | Notes |
|---|---|---|---|
| `.gltf` `.glb` | our own, `Render/Gltf` (glTF 2.0 only; triangle, strip and fan primitives, points and lines are left out, a triangle with an index past the vertices is dropped; a file from which no mesh can be read, or that needs Draco or meshopt compression, fails the import with a message) | yes | always built in |
| `.fbx` | ufbx (vendored in `engine/third_party/ufbx`); Assimp only as a fallback for files ufbx rejects | yes | always built in |
| `.obj` (+ `.mtl`) | our own: `ReadObjFile` for scene import (one MeshRenderer per `usemtl`), `Mesh::LoadOBJ` for the merged-mesh path | materials only (no nodes) | always built in |
| `.stl` (ascii and binary) | our own, `ReadStlFile`. Assimp first when built with `OKAY_USE_ASSIMP` unless `OKAY_PREFER_OWN_READERS` | one mesh per `solid` | in every build since this change |
| `.ply` (ascii, little and big endian) | our own, `ReadPlyFile`. Same Assimp rule as STL | one mesh | in every build since this change |
| `.dae` | Assimp when built with `-DOKAY_USE_ASSIMP=ON`. Our own reader, `ReadColladaFile`, sits beside it and is **off by default** (see below) | yes (hierarchy, skins, matrix animation) | without Assimp and without switching ours on, `.dae` fails with a message saying how to switch it on |
| `.3ds` | Assimp when built with `-DOKAY_USE_ASSIMP=ON`. Our own reader, `Read3dsFile`, sits beside it and is **off by default** (`OKAY_OWN_3DS`, same rules as `.dae`; `docs/own_3ds_reader.md`) | yes (hierarchy, pivots, keyframes) | without Assimp and without switching ours on, `.3ds` fails with a message saying how to switch it on |
| `.bvh` | Assimp (a stick-figure mesh) when built with `-DOKAY_USE_ASSIMP=ON`. Our own reader, `ReadBvhFile`, is **off by default** (`OKAY_OWN_BVH`; `docs/own_bvh_reader.md`) and reads the skeleton and the motion with no mesh | skeleton and one clip, no mesh | same as `.3ds` |
| `.blend` `.x` `.md5mesh` `.smd` `.ms3d` `.lwo` `.dxf` `.off` `.ac` `.b3d` `.3mf` `.amf` `.ase` `.x3d` `.md2` `.md3` `.mdl` and the rest of Assimp's list | Assimp, only when built with `-DOKAY_USE_ASSIMP=ON` | yes (hierarchy, skins, clips) | without Assimp these fail with `ok = false` |

`ImportableExtensions()` lists what the current build reads; the editor's file pickers use it.

## The seam

`okay/Render/ModelData.hpp` holds our own result type, `ImportedModel` (nodes, mesh parts, materials, bone
weights, clips) and the one entry point `ImportModelData(path, out, backend, flatten, err)`.
Assimp's own headers are included in exactly one file, `engine/src/Render/ModelImportAssimp.cpp`
(compiled only with `OKAY_USE_ASSIMP`); it turns an `aiScene` into an `ImportedModel`.
`ModelImport.cpp` builds meshes and scenes from `ImportedModel` only (`MergeModelMeshes`,
`BuildSceneFromModel`), so no caller touches an Assimp type.

Choosing a backend, per format (`.obj`, `.stl`, `.ply`, `.dae`, `.3ds`, `.bvh` have one of ours):

* Build time: `-DOKAY_PREFER_OWN_READERS=ON` makes STL and PLY use our readers first, with Assimp as the fallback when ours rejects a file.
* Run time: `okay::SetImportBackend(".stl", ImportBackend::Own)` (or `Assimp`, or `Default`; `"*"` sets every format). `EffectiveImportBackend(".stl")` says what will run. Set it at start-up, not while another thread is importing.
* **Collada, 3DS and BVH are opt-in.** Unlike STL and PLY, our `.dae`, `.3ds` and `.bvh` readers never run unless asked: run-time `SetImportBackend(".dae", ImportBackend::Own)` (or `".3ds"`, `".bvh"`), else the environment variable `OKAY_OWN_COLLADA=1` / `OKAY_OWN_3DS=1` / `OKAY_OWN_BVH=1` (`=0` forces it off), else the CMake option `-DOKAY_OWN_COLLADA=ON` / `-DOKAY_OWN_3DS=ON` / `-DOKAY_OWN_BVH=ON`. Switched on, it is tried first and Assimp (when built) is the fallback for files it rejects. Without Assimp and without the switch nothing reads that format, as before. `ImportableExtensions()` lists the extension only while ours is on or Assimp is built.
* The default is unchanged where Assimp was used before: with Assimp built, STL and PLY still go to Assimp; without it, they now go to our readers (before, they failed). OBJ has never gone through Assimp.

## What our readers do

* **OBJ + MTL**: `v` `vt` `vn` `f` with negative indices and `v//vn`; convex polygons fan-triangulated from their first corner, concave ones ear clipped (so is PLY); points and lines are not read (a file with no faces fails); one part per `usemtl`. From the `.mtl`: `Kd`, `Ke`, `Ns`, `Pm`, `Pr`, `map_Kd`, `map_Bump`/`bump`/`norm`, `map_Ks`/`map_Ns`; texture options (`-s`, `-o`, `-bm` ...) are skipped; a missing `.mtl` is not an error. UVs are kept as written (like `Mesh::LoadOBJ`).
* **STL**: ascii or binary (told apart by the size rule, so a binary file whose header says "solid" still works). Vertices are welded per position and normal; the file's facet normal is used, or the winding's when it is zero.
* **PLY**: `x y z`, `nx ny nz`, `s t` / `u v` / `texture_u texture_v`; list `vertex_indices`; other elements and vertex colours are skipped. UV `v` is flipped like the other bottom-up formats. Without normals, smooth normals are generated (what Assimp's `GenSmoothNormals` does).
* All three: every read is bounds-checked, element counts are checked against the bytes left before anything is allocated, indices are range-checked, non-finite numbers are rejected, files over 512 MiB are refused. They return `false` with a one-line message.

* **COLLADA 1.4 / 1.5** (`engine/src/Render/ColladaReader.cpp`, off by default): own bounds-checked XML parser (entities, CDATA, comments, namespaces; no external entities are ever fetched; nesting, element count and size are capped). Reads `triangles`, `polylist`, `polygons`, `tristrips` and `trifans` (convex polygons are fanned from their first corner, concave ones ear clipped, one part per primitive element, vertices welded per position+normal+uv, smooth normals generated when the file has none, `<ph>` outer ring only); materials from the effect (`phong`/`blinn`/`lambert`/`constant`: diffuse colour or texture, emission, shininess, specular texture, a `<bump>` texture as the normal map; sampler, surface and image chains and `file://` / `%20` / backslash paths resolved next to the file); the node tree (`matrix`, `translate`, `rotate`, `scale`, `instance_node`; node names are the ids, as skins and animations name them); skins (the four strongest weights, renormalised; the bind shape matrix is folded into the inverse bind matrix; joints found by sid, within the `<skeleton>` subtree first); animations (every channel names a transform element of a node by its `sid`: a whole `matrix`, `translate`, `rotate` or `scale`, or one component such as `location.X`, `rotationZ.ANGLE`, `translate(1)`, `mat(1)(3)`; `node/transform` also replaces the whole local matrix when the node has no element with that sid). All channels of a node are merged: the element values are sampled at the union of the channels' key times, recomposed into the node's local matrix and decomposed into position, rotation and scale keys. `INTERPOLATION` is read per key: `LINEAR` and `STEP` are honoured (a step is written as a hold key just before the next key, 0.1 ms ahead, because our clips only interpolate linearly); any other name (`BEZIER`, `HERMITE`, `CARDINAL`, `BSPLINE`) is read as `LINEAR` and reported in the notes. A rotation segment of more than 90 degrees is split so the clip's quaternion interpolation cannot take the short way round. When the file has `<library_animation_clips>`, each `<animation_clip>` is one clip (its animations' channels, cut to its `start` / `end` and moved to start at 0). Otherwise top-level `<animation>` elements that hold other animations are one clip each; plain top-level animations (one per bone or property) are merged into one clip named `animation` (or by name when there is only one). The root node carries the unit scale and the rotation that makes +Y up (`Z_UP` and `X_UP` files). UV `v` is flipped like the other bottom-up formats. Flattened import (`ImportModel`, the merged-mesh path) bakes every transform into the vertices and returns no skins or clips.
* **3DS** (`engine/src/Render/Max3dsReader.cpp`, off by default): meshes with smoothing-group normals and per-material parts, materials and texture names, the master scale, and the keyframe node hierarchy with pivots and position / rotation / scale tracks as one clip. Details, limits and what was checked against real files in `docs/own_3ds_reader.md`.
* **BVH** (`engine/src/Render/BvhReader.cpp`, off by default): the joint hierarchy as nodes and the motion as one clip with a position and rotation key per frame, no mesh. `docs/own_bvh_reader.md`.
* **What a reader did not read is said.** `ImportedModel::notes` lists it one line each, and `ImportModelData` logs every line as a warning. For Collada that covers: lines, line strips, geometry that is not a mesh, numbers written with a decimal comma (read, and said), vertex colours, UV sets after the first, morph (blend shape) controllers (the base mesh is imported), `<skew>` and `<lookat>`, animation channels whose target element or sid is not found, whose value count does not match, or whose keys are out of time order, interpolation other than LINEAR and STEP (read as linear), cameras and lights, malformed skins (the mesh is then imported unskinned), vertices with more than four weights, and skin joints that are not nodes of the scene.

## Tests

`collada_reader` (`tests/test_collada_reader.cpp`): five hand-written fixtures (`two_materials.dae`, `zup_cm.dae`, `skinned_strip.dae`, `component_anim.dae`, `unsupported.dae`) covering triangles and polylist, two materials with the sampler/surface/image chain, node transforms checked as world-space points, `Z_UP`/`X_UP` and unit, polygon fan and generated normals, skin weights and bone names, matrix animation, per-component and float3 channels merged per node, STEP, the BEZIER note, the 270 degree split,  every note, XML entities/CDATA/namespaces, the opt-in switch (environment, run-time, `*`), merged-mesh and scene import. In a build with Assimp it reads the same files through both backends and compares part, vertex, triangle, bone and clip counts and world-space bounds. `model_readers_fuzz` (also built on its own under `-fsanitize=address,undefined,float-cast-overflow` from `ModelReaders.cpp` + `ColladaReader.cpp`; clean) adds the Collada malformed-input cases and puts the four `.dae` fixtures into the truncation and mutation sweep.

`3ds_reader`, `bvh_reader` and the standalone `3ds_bvh_hostile` (hostile input, mutation fuzz, sanitizers) test the 3DS and BVH readers; `model_corpus` re-reads real exporter files from outside the repository when `OKAY_MODEL_CORPUS` is set (`docs/model_corpus.md`).

`model_readers` (`tests/test_model_readers.cpp`, fixtures in `tests/data/models/`): every format and encoding, ascii equals binary, MTL fields, malformed input, a deterministic fuzz sweep (every truncation of every fixture plus mutations; run it under `-DOKAY_SANITIZE=address,undefined`), the backend switch, scene import of a two-material OBJ. In a build with Assimp it also reads the same files through both backends and compares part, vertex and triangle counts, the triangles themselves (positions, normals, UVs), the OBJ materials, and, for the glTF rig fixtures in `tests/assets/rigs`, Assimp's bone, skinned-mesh and clip counts against the glTF reader's.

## FBX limits (hostile files)

An FBX file is untrusted input, so both FBX paths (ufbx, the default, and our own `okay::fbx` reader) refuse a file that goes past the numbers in `okay::fbx::Limits` (table in `docs/own_fbx_reader.md`). The refusal is a clean failure: `ok = false`, an error string in the log, no partial scene, and no other reader is tried on the same file.

- **Node hierarchy.** At most 2048 levels deep and 1 Mi nodes. A flat file can declare an arbitrarily deep Model chain through its connections, so the ufbx path sets ufbx's `node_depth_limit` and builds the engine hierarchy with an explicit stack, not recursion.
- **Animation counts.** At most 128 stacks (takes), 1024 layers, 128 Ki curves and curve nodes, and 4 Mi keys over all curves, checked right after the file is read.
- **Bake budget.** Stacks can share curves, so a small file can ask for many full clips. Baking is charged against one whole-file budget of 4 Mi baked keys/evaluations (a few hundred MB at most). On the ufbx path every bake is also given an allocation limit equal to what is left of that budget; our reader additionally caps each clip at 20000 samples.

Cubic FBX keys store two slopes: the key's own outgoing (right) slope and the incoming (next-left) slope that the following key arrives with. Both paths interpolate between key k and k+1 with the Hermite spline using the outgoing slope of k and the incoming slope stored on k (test `fbx_reader`, "cubic Hermite"). glTF `CUBICSPLINE` is baked with the same basis, using the out-tangent of k and the in-tangent of k+1.

## Not covered yet (still Assimp-only)

Blender (`.blend`), DirectX `.x`, MD2/MD3/MD5/MDL, SMD, MS3D, LWO, DXF, OFF, AC3D, B3D, 3MF, AMF, ASE, X3D and the rest of the Assimp list. FBX stays on ufbx unless `OKAY_OWN_FBX` is switched on. PLY triangle strips, PLY/STL vertex colours, STL colour attributes, and OBJ free-form curves and surfaces are not read.

## Collada: what is proven and what is not

Our reader was checked on fixtures we wrote and, in an Assimp build, against Assimp on those; since then it has also been run on 26 files from real exporters (`docs/model_corpus.md`: 24 read, two have no mesh) and compared with Assimp built from source, which found and fixed six bugs (deep node chains, decimal commas, UTF-16, triangle strips and fans, concave polygons, named takes). It stays off by default until more exporters have been tried, Windows has run it, and the owner approves.

Known gaps: no morph targets, no `lines` / `linestrips`, no vertex colours or second UV set, no embedded texture data, no physics, kinematics or `extra` techniques other than a bump texture, polygons with holes keep only the outer ring (a hole written as one bridged ring is triangulated correctly), BEZIER/HERMITE/CARDINAL/BSPLINE curves are flattened to straight lines between keys (tangents are ignored), STEP is approximated by a 0.1 ms ramp, rotation is interpolated as a quaternion between the sampled keys, not as the file's angle, so very dense or very fast rotations are only as exact as the key spacing, a target sid is matched against the first node with that id so instanced nodes share one animation, animated materials, visibility and camera or light channels are not read, no COLLADA 1.5 `<instance_effect>` parameter overrides, and normals under non-uniform scale in flattened mode use the plain upper 3x3 matrix. With `<library_animation_clips>` present, animations no clip names are not imported.

## Windows and locale

Paths are UTF-8 strings. The readers open files by UTF-8 path (`_wfopen` on Windows), so a folder or file name outside the ANSI code page works, and they read numbers with `.` as the decimal point whatever `LC_NUMERIC` the host process set (`docs/model_corpus.md`: both found on the MinGW build under wine).

## Known inconsistency

`.obj` UVs are kept as written (v up) while FBX, PLY and everything Assimp reads are flipped to v down. This predates the seam (`Mesh::LoadOBJ` never flipped) and was left as is so existing OBJ imports do not change; the readers and the comparison test record it.
