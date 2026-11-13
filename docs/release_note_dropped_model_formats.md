# Release note draft: model formats that stop importing when Assimp is removed

Status: DRAFT. Nothing in this note has happened yet: Assimp is still in the build (off by default, on in the Windows
distribution) and no default has been changed. The roadmap (`docs/own_code_roadmap.md`, Assimp row and order step 3)
requires this announcement to be published **one release before** Assimp is removed. The owner decides the release
numbers and the date; the text between the lines below is the announcement, with the two release numbers left to fill in.

Decision made (owner delegated it): **`.3ds` and `.bvh` are covered, not dropped.** Our own 3DS reader and BVH reader exist
(PR #795, `docs/own_3ds_reader.md`, `docs/own_bvh_reader.md`) and were checked on real files (15 of 16 and 3 of 3). Both
are off by default until the release that makes our readers the default; the sequence is: this announcement in the next
release, our readers as default in the release after, Assimp and ufbx deleted the release after that. The flip is
conditional on `docs/model_corpus.md` ("flip readiness") saying the criteria are met.

---

## Announcement text

**Model import: some rarely used formats will stop importing in version `<REMOVAL RELEASE>`**

OkaySpace is replacing the third-party model importer (Assimp) with import code we wrote ourselves. In version
`<REMOVAL RELEASE>` the Assimp importer is removed. This note is one release ahead so you can convert files in time.

**These formats will keep importing** (our own readers):

- glTF 2.0 (`.gltf`, `.glb`)
- Wavefront OBJ with MTL materials (`.obj`)
- STL (`.stl`)
- PLY (`.ply`)
- COLLADA (`.dae`)
- FBX (`.fbx`)
- 3D Studio (`.3ds`)
- BVH motion capture (`.bvh`, animation only)

**These formats will stop importing** when Assimp is removed: `.blend`, `.x`, MD2/MD3/MD5/MDL, SMD, MS3D, LWO, DXF, OFF,
AC, B3D, 3MF, AMF, ASE, X3D and the rest of Assimp's list.

**What to do:** open the file in your modelling tool and export it as glTF 2.0 (`.glb`) or FBX, then import that file.
Models that are already in a project keep working only if they were imported before; re-importing the original file in a
dropped format will fail with a "format not supported" message after the removal.

Until `<REMOVAL RELEASE>`, nothing changes: every format above still imports in the version you have now.

---

## Which of our own readers cover each format

"Own reader" means code in this repository (`engine/src/Render/ModelReaders.cpp`, `ColladaReader.cpp`, `FbxReader.cpp`,
`FbxScene.cpp`, `Max3dsReader.cpp`, `BvhReader.cpp`, and the glTF loader in `ModelImport.cpp`). State is from the code today, taken from
`docs/own_code_roadmap.md`.

| Format | Covered by an own reader? | State today |
|---|---|---|
| glTF / GLB | Yes | Ours, always built. |
| OBJ + MTL | Yes | Ours, always built. |
| STL | Yes | Ours, always built. |
| PLY | Yes | Ours, always built. |
| COLLADA (`.dae`) | Yes, **off by default** (`OKAY_OWN_COLLADA`) | Checked on 26 real exporter files (24 read; the other two hold only cameras or lights) and on our own fixtures. Without Assimp this reader is the only way `.dae` loads. |
| FBX | Yes, **off by default** (`OKAY_OWN_FBX`); ufbx is the default reader | Checked on 43 real exported files (42 agree with ufbx; the 43rd is damaged and every reader rejects it). Stays on ufbx until the flip release. |
| 3DS | Yes, **off by default** (`OKAY_OWN_3DS`) | Checked on 16 real files (15 read; the 16th is empty); meshes, triangles, bounding boxes and area match Assimp. |
| BVH | Yes, **off by default** (`OKAY_OWN_BVH`) | Checked on 3 real files; animation only (skeleton and one clip, no mesh). |
| `.blend`, `.x`, MD2, MD3, MD5, MDL, SMD, MS3D, LWO, DXF, OFF, AC, B3D, 3MF, AMF, ASE, X3D and the rest of Assimp's list | **No, by decision** | Dropped, not replaced. |

Not part of the announcement, but a precondition (see `docs/third_party_removal_checklist.md`): the COLLADA and FBX
defaults have been flipped under their own release notes, the 3DS question above is settled, and the owner has approved.
