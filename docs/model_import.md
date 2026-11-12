# Model import: who reads which format

All model files come in through one door, `okay::ImportModel` (one merged mesh) and
`okay::ImportModelScene` (a node hierarchy with materials, skins and clips), both in
`okay/Render/ModelImport.hpp`. This page says which reader handles which format and how
the third-party reader (Assimp, optional) is kept behind a seam so it can be replaced one
format at a time. The plan is in `docs/own_code_roadmap.md`.

## Who reads what

| Format | Reader | Scene import (nodes, skins, clips) | Notes |
|---|---|---|---|
| `.gltf` `.glb` | our own, `Render/Gltf` | yes | always built in |
| `.fbx` | ufbx (vendored in `engine/third_party/ufbx`); Assimp only as a fallback for files ufbx rejects | yes | always built in |
| `.obj` (+ `.mtl`) | our own: `ReadObjFile` for scene import (one MeshRenderer per `usemtl`), `Mesh::LoadOBJ` for the merged-mesh path | materials only (no nodes) | always built in |
| `.stl` (ascii and binary) | our own, `ReadStlFile`. Assimp first when built with `OKAY_USE_ASSIMP` unless `OKAY_PREFER_OWN_READERS` | one mesh per `solid` | in every build since this change |
| `.ply` (ascii, little and big endian) | our own, `ReadPlyFile`. Same Assimp rule as STL | one mesh | in every build since this change |
| `.dae` `.3ds` `.blend` `.x` `.md5mesh` `.smd` `.ms3d` `.lwo` `.dxf` `.off` `.ac` `.b3d` `.3mf` `.amf` `.ase` `.x3d` `.md2` `.md3` `.mdl` `.bvh` and the rest of Assimp's list | Assimp, only when built with `-DOKAY_USE_ASSIMP=ON` | yes (hierarchy, skins, clips) | without Assimp these fail with `ok = false` |

`ImportableExtensions()` lists what the current build reads; the editor's file pickers use it.

## The seam

`okay/Render/ModelData.hpp` holds our own result type, `ImportedModel` (nodes, mesh parts, materials, bone
weights, clips) and the one entry point `ImportModelData(path, out, backend, flatten, err)`.
Assimp's own headers are included in exactly one file, `engine/src/Render/ModelImportAssimp.cpp`
(compiled only with `OKAY_USE_ASSIMP`); it turns an `aiScene` into an `ImportedModel`.
`ModelImport.cpp` builds meshes and scenes from `ImportedModel` only (`MergeModelMeshes`,
`BuildSceneFromModel`), so no caller touches an Assimp type.

Choosing a backend, per format (`.obj`, `.stl`, `.ply` have one of ours):

* Build time: `-DOKAY_PREFER_OWN_READERS=ON` makes STL and PLY use our readers first, with Assimp as the fallback when ours rejects a file.
* Run time: `okay::SetImportBackend(".stl", ImportBackend::Own)` (or `Assimp`, or `Default`; `"*"` sets every format). `EffectiveImportBackend(".stl")` says what will run. Set it at start-up, not while another thread is importing.
* The default is unchanged where Assimp was used before: with Assimp built, STL and PLY still go to Assimp; without it, they now go to our readers (before, they failed). OBJ has never gone through Assimp.

## What our readers do

* **OBJ + MTL**: `v` `vt` `vn` `f` with negative indices and `v//vn`; polygons fan-triangulated; one part per `usemtl`. From the `.mtl`: `Kd`, `Ke`, `Ns`, `Pm`, `Pr`, `map_Kd`, `map_Bump`/`bump`/`norm`, `map_Ks`/`map_Ns`; texture options (`-s`, `-o`, `-bm` ...) are skipped; a missing `.mtl` is not an error. UVs are kept as written (like `Mesh::LoadOBJ`).
* **STL**: ascii or binary (told apart by the size rule, so a binary file whose header says "solid" still works). Vertices are welded per position and normal; the file's facet normal is used, or the winding's when it is zero.
* **PLY**: `x y z`, `nx ny nz`, `s t` / `u v` / `texture_u texture_v`; list `vertex_indices`; other elements and vertex colours are skipped. UV `v` is flipped like the other bottom-up formats. Without normals, smooth normals are generated (what Assimp's `GenSmoothNormals` does).
* All three: every read is bounds-checked, element counts are checked against the bytes left before anything is allocated, indices are range-checked, non-finite numbers are rejected, files over 512 MiB are refused. They return `false` with a one-line message.

## Tests

`model_readers` (`tests/test_model_readers.cpp`, fixtures in `tests/data/models/`): every format and encoding, ascii equals binary, MTL fields, malformed input, a deterministic fuzz sweep (every truncation of every fixture plus mutations; run it under `-DOKAY_SANITIZE=address,undefined`), the backend switch, scene import of a two-material OBJ. In a build with Assimp it also reads the same files through both backends and compares part, vertex and triangle counts, the triangles themselves (positions, normals, UVs), the OBJ materials, and, for the glTF rig fixtures in `tests/assets/rigs`, Assimp's bone, skinned-mesh and clip counts against the glTF reader's.

## Not covered yet (still Assimp-only)

Collada (`.dae`), 3DS, Blender (`.blend`), DirectX `.x`, MD2/MD3/MD5/MDL, SMD, MS3D, LWO, DXF, OFF, AC3D, B3D, 3MF, AMF, ASE, X3D, BVH and the rest of the Assimp list. FBX stays on ufbx. PLY triangle strips, PLY/STL vertex colours, STL colour attributes, and OBJ free-form curves and surfaces are not read.

## Known inconsistency

`.obj` UVs are kept as written (v up) while FBX, PLY and everything Assimp reads are flipped to v down. This predates the seam (`Mesh::LoadOBJ` never flipped) and was left as is so existing OBJ imports do not change; the readers and the comparison test record it.
