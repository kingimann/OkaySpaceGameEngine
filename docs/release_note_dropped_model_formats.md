# Release note draft: model formats that stop importing when Assimp is removed

Status: DRAFT. Nothing in this note has happened yet: Assimp is still in the build (off by default, on in the Windows
distribution) and no default has been changed. The roadmap (`docs/own_code_roadmap.md`, Assimp row and order step 3)
requires this announcement to be published **one release before** Assimp is removed. The owner decides the release
numbers and the date; the text between the lines below is the announcement, with the two release numbers left to fill in.

Update (own Blender reader): **`.blend` moves from "dropped" to "own reader in progress".** Our own `.blend` reader exists
(`docs/own_blend_reader.md`, `OKAY_OWN_BLEND`, off by default) and was checked on 28 real files from Blender 2.41 to 2.93, but it is
not yet something the owner has approved as a replacement: until that happens `.blend` stays on this note's "may stop importing"
list in the sense that it is conditional on the flip criteria in `docs/model_corpus.md`. Not read by it (named at import time):
modifiers (subdivision, mirror, ...), constraints, shape keys, vertex colours, lights, cameras, curves and text objects, pre-2.5
Ipo animation, packed images; zstd compressed files (Blender 3.0+ "Compress File") and the Blender 5.0 container.

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
- Blender (`.blend`, **pending**: our own reader is built and tested but not yet approved as the replacement; see the update above)

**These formats have an own reader that is built and tested but not yet approved as the replacement** (off by default, like Blender above):
OFF, SMD, MD5, MS3D, ASE, 3MF, AMF, X3D, DXF, LWO, MD2, MD3, Irrlicht meshes (`.irrmesh`), B3D and DirectX (`.x`).

**These formats will stop importing** when Assimp is removed, **unless their readers are written first** (the owner's rule is to
write replacements, not to drop formats; the list, the difficulty of each and the order are in `docs/own_readers_remaining.md`):
AC3D (`.ac`), Ogre meshes, OpenGEX, Q3D, Quake III BSP, Irrlicht scenes (`.irr`), MDC, MDL (Quake, Half-Life, 3D GameStudio),
HMP, NFF, RAW, COB, CSM, Terragen, SIB, MMD (`.pmx`), IQM, Nendo (`.ndo`), XGL / ZGL, LightWave scenes (`.lws`) and glTF 1.0.
(Not switched on in our Windows build, so nothing a user has today: M3D, USD, Unreal, IFC, Assimp binary, Cinema 4D.)

**What to do (for `.blend`, only if the owner decides against the own reader):** open the file in your modelling tool and export it as glTF 2.0 (`.glb`) or FBX, then import that file.
Models that are already in a project keep working only if they were imported before; re-importing the original file in a
dropped format will fail with a "format not supported" message after the removal.

Until `<REMOVAL RELEASE>`, nothing changes: every format above still imports in the version you have now.

---

## Which of our own readers cover each format

"Own reader" means code in this repository (`engine/src/Render/ModelReaders.cpp`, `ColladaReader.cpp`, `FbxReader.cpp`,
`FbxScene.cpp`, `Max3dsReader.cpp`, `BvhReader.cpp`, `BlendReader.cpp`, and the glTF loader in `ModelImport.cpp`). State is from the code today, taken from
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
| Blender (`.blend`) | Own reader in progress, **off by default** (`OKAY_OWN_BLEND`) | Checked on 28 real files (Blender 2.41 to 2.93, all read) and compared with Assimp; modifiers, constraints, shape keys, vertex colours and zstd compression are not read and are named at import. No real Blender 3.x / 4.x file was available to test. |
| OFF, SMD, MD5, MS3D, ASE, 3MF, AMF, X3D, DXF, LWO, MD2, MD3, Irrlicht mesh, B3D, DirectX `.x` | Yes, **off by default** (`OKAY_OWN_<FORMAT>`) | Each checked on every real file of its format in Assimp's test directory and compared with Assimp (`docs/model_corpus.md`); one page each (`docs/own_off_reader.md` ... `docs/own_x_reader.md`). |
| AC3D, Ogre, OpenGEX, Q3D, Q3BSP, Irrlicht scenes, MDC, MDL, HMP, NFF, RAW, COB, CSM, Terragen, SIB, PMX, IQM, Nendo, XGL, LWS, glTF 1.0 | **Not yet** | To be written, in the order of `docs/own_readers_remaining.md`. Until then they would stop importing with Assimp. |

Not part of the announcement, but a precondition (see `docs/third_party_removal_checklist.md`): the COLLADA and FBX
defaults have been flipped under their own release notes, the 3DS question above is settled, and the owner has approved.
