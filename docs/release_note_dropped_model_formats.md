# Release note draft: model formats that stop importing when Assimp is removed

Status: DRAFT (format lists brought up to date with `main` on 2026-11-14). Nothing in this note has happened yet: Assimp is still in the build (off by default, on in the Windows
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
- OFF (`.off`), Valve SMD (`.smd`), id Tech 4 MD5 (`.md5mesh`, `.md5anim`), MilkShape 3D (`.ms3d`), 3ds Max ASE (`.ase`)
- 3MF (`.3mf`), AMF (`.amf`), X3D (`.x3d`), AutoCAD DXF (`.dxf`), LightWave objects (`.lwo`)

**These formats will stop importing** when Assimp is removed: `.blend`, DirectX `.x`, MD2, MD3, MDL, AC3D (`.ac`), B3D,
LightWave scenes (`.lws`) and the rest of Assimp's list. (A reader of our own for `.blend` is being written but is not
in the main code yet: if it passes its criteria before the removal release, move `.blend` to the list above.)

**What to do:** open the file in your modelling tool and export it as glTF 2.0 (`.glb`) or FBX, then import that file.
Models that are already in a project keep working only if they were imported before; re-importing the original file in a
dropped format will fail with a "format not supported" message after the removal.

Until `<REMOVAL RELEASE>`, nothing changes: every format above still imports in the version you have now.

---

## Which of our own readers cover each format

"Own reader" means code in this repository (`engine/src/Render/ModelReaders.cpp`, `ColladaReader.cpp`, `FbxReader.cpp`,
`FbxScene.cpp`, `Max3dsReader.cpp`, `BvhReader.cpp`, `OffReader.cpp`, `SmdReader.cpp`, `Md5Reader.cpp`, `Ms3dReader.cpp`, `AseReader.cpp`, `ThreeMfReader.cpp`, `AmfReader.cpp`, `X3dReader.cpp`, `DxfReader.cpp`, `LwoReader.cpp`, and the glTF loader in `ModelImport.cpp`). State is from the code today, taken from
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
| OFF, SMD, MD5 (mesh and anim), MS3D, ASE | Yes, **off by default** (`OKAY_OWN_OFF`, `OKAY_OWN_SMD`, `OKAY_OWN_MD5`, `OKAY_OWN_MS3D`, `OKAY_OWN_ASE`) | Readers batch A: hand-computed fixtures, hostile-input tests, fuzz, and a comparison with Assimp on the real files of its test directory (see `docs/own_code_roadmap.md` and `docs/model_corpus.md` for the counts). |
| 3MF, AMF, X3D, DXF, LWO | Yes, **off by default** (`OKAY_OWN_3MF`, `OKAY_OWN_AMF`, `OKAY_OWN_X3D`, `OKAY_OWN_DXF`, `OKAY_OWN_LWO`) | Readers batch B: synthetic fixtures with hand-computed numbers, hostile-input and mutation tests. Newer than batch A: check `docs/own_code_roadmap.md` for how far the real-file comparison has got. |
| `.blend` | **Not yet** | A reader is in progress and is not on `main`. Until it lands and passes its criteria, `.blend` is in the dropped list. |
| `.x`, MD2, MD3, MDL, AC, B3D, LWS and the rest of Assimp's list | **No, by decision** | Dropped, not replaced. |

Not part of the announcement, but a precondition (see `docs/third_party_removal_checklist.md`): the COLLADA and FBX
defaults have been flipped under their own release notes, the 3DS question above is settled, and the owner has approved.
