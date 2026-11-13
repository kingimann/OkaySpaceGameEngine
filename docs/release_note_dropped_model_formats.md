# Release note draft: model formats that stop importing when Assimp is removed

Status: DRAFT. Nothing in this note has happened yet: Assimp is still in the build (off by default, on in the Windows
distribution) and no default has been changed. The roadmap (`docs/own_code_roadmap.md`, Assimp row and order step 3)
requires this announcement to be published **one release before** Assimp is removed. The owner decides the release
numbers and the date; the text between the lines below is the announcement, with the two release numbers left to fill in.

Open decision for the owner before publishing: **3DS and BVH have no reader of our own yet.** The roadmap's supported
set after removal lists 3DS (and BVH animation "later"), but neither is written. Either the 3DS reader passes its
criteria before Assimp is removed (then delete the 3DS line from the dropped list below), or `.3ds` is announced as
dropped too. The text below announces `.3ds` and `.bvh` as dropped, which is the safe reading of today's code; change it
only when a reader exists and has passed the criteria.

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

**These formats will stop importing** when Assimp is removed: `.blend`, `.x`, MD2/MD3/MD5/MDL, SMD, MS3D, LWO, DXF, OFF,
AC, B3D, 3MF, AMF, ASE, X3D and the rest of Assimp's list. Also `.3ds` and `.bvh`, which do not have a reader of our own
yet.

**What to do:** open the file in your modelling tool and export it as glTF 2.0 (`.glb`) or FBX, then import that file.
Models that are already in a project keep working only if they were imported before; re-importing the original file in a
dropped format will fail with a "format not supported" message after the removal.

Until `<REMOVAL RELEASE>`, nothing changes: every format above still imports in the version you have now.

---

## Which of our own readers cover each format

"Own reader" means code in this repository (`engine/src/Render/ModelReaders.cpp`, `ColladaReader.cpp`, `FbxReader.cpp`,
`FbxScene.cpp`, and the glTF loader in `ModelImport.cpp`). State is from the code today, taken from
`docs/own_code_roadmap.md`.

| Format | Covered by an own reader? | State today |
|---|---|---|
| glTF / GLB | Yes | Ours, always built. |
| OBJ + MTL | Yes | Ours, always built. |
| STL | Yes | Ours, always built. |
| PLY | Yes | Ours, always built. |
| COLLADA (`.dae`) | Yes, **off by default** (`OKAY_OWN_COLLADA`) | Tested on fixtures our own scripts wrote; no real-exporter corpus yet. Without Assimp this reader is the only way `.dae` loads, so it must be on by default before Assimp goes. |
| FBX | Yes, **off by default** (`OKAY_OWN_FBX`); ufbx is the default reader | Tested on fixtures only; no real exported file has been read by it. Stays on ufbx until its own criteria hold. |
| 3DS | **No** | Reader not written (Assimp is the only importer today). |
| BVH | **No** | Animation reader not written (Assimp is the only importer today). |
| `.blend`, `.x`, MD2, MD3, MD5, MDL, SMD, MS3D, LWO, DXF, OFF, AC, B3D, 3MF, AMF, ASE, X3D and the rest of Assimp's list | **No, by decision** | Dropped, not replaced. |

Not part of the announcement, but a precondition (see `docs/third_party_removal_checklist.md`): the COLLADA and FBX
defaults have been flipped under their own release notes, the 3DS question above is settled, and the owner has approved.
