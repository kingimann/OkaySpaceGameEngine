# Model formats Assimp reads that we have no own reader for

Goal (owner): no third-party dependency. Assimp v5.4.3 is the last big one on the model-import side. This page lists every
format it can read that we cannot yet read ourselves, says how hard each one is, whether Assimp's own test directory has real
files for it, and in what order to write them. The rule in `docs/own_code_roadmap.md` stays: a format is **not dropped
because it is hard**; its replacement is written, kept off by default, checked, and only flipped with the owner's approval.

Source of the list: `code/Common/ImporterRegistry.cpp` of the Assimp v5.4.3 checkout (the version `OKAY_USE_ASSIMP`
fetches), the extension list each importer declares, the importers `engine/CMakeLists.txt` really switches on, and the files
under `test/models` and `test/models-nonbsd` of that checkout (the real-file corpus of `docs/model_corpus.md`). The counts
below were made by counting those files by extension; where I could not check something it says so.

## The numbers

| | Count |
|---|---|
| Importers in Assimp v5.4.3's registry | **51** (glTF 1.0 and glTF 2.0 are two importers) |
| Importers our Windows distribution switches on (`engine/CMakeLists.txt`) | **45** (not switched on: USD, M3D, Unreal, IFC, Assbin, and Cinema 4D, which needs the non-free Maxon SDK and is off in every build) |
| Importers we covered **before batch C** (including Blender, merged as #829, off by default) | 19 |
| Importers batch C added (MD2, MD3, Irrlicht mesh, B3D, DirectX .x) | 5 (24) |
| Importers batch D adds (AC3D, XGL / ZGL, Irrlicht scene, NFF, and OGRE **for the XML form only**) | 5 (**29** of the 45 switched on; OGRE counts as half: its binary `.mesh` / `.skeleton` and the `.skeleton.xml` are row 1 below) |
| Switched on in our build and still **without an own reader** | **16** (rows 2 to 17 below) plus OGRE's half |
| Not switched on and without an own reader | 6 |

"Covered" means an own reader exists, is off by default, passes its tests and was compared with Assimp on the real files of the
tree. **Nothing was flipped.** Which formats the 29 are: glTF 2.0 and GLB, OBJ, STL, PLY and FBX (ours, always on or ufbx),
COLLADA, 3DS, BVH, OFF, SMD, MD5, MS3D, ASE, 3MF, AMF, X3D, DXF, LWO, Blender, MD2, MD3, Irrlicht mesh, B3D, DirectX .x,
AC3D, OGRE (XML only), XGL / ZGL, Irrlicht scene, NFF. How the batch D readers compare with Assimp on the real files is in
`docs/model_corpus.md` and in each reader's own page (`docs/own_ac3d_reader.md`, `own_ogre_reader.md`, `own_xgl_reader.md`,
`own_irr_reader.md`, `own_nff_reader.md`).

## What is left

"How common": how often the format shows up in the wild today (my judgement, not a measurement). "How hard": my estimate of
the work for a reader of the same quality as the ones above (hostile-input tests, mutation fuzz, real-file comparison):
**easy** = a day of work, **medium** = several days, **hard** = weeks. "Real files" counts the files of that format in
Assimp's `test/models` and `test/models-nonbsd` (so the comparison with Assimp can be done); all of them are **real**,
none were invented.

| # | Format (extensions) | What it is | How common | How hard | Real files in the tree | Switched on in our build |
|---|---|---|---|---|---|---|
| 1 | **Ogre, the rest** (`.mesh`, `.skeleton`, `.skeleton.xml`) | OGRE3D: the binary mesh, and the skeleton in both forms with bones, bind pose and animations (the XML mesh geometry and materials are done, batch D) | medium-high | binary `.mesh`: medium; skeleton: medium (needs the skin and clip path of the other skinned readers) | 6 (`fish`, `ninja`, `razor` as `.mesh` and the skeletons as binary and XML) | yes |
| 2 | **OpenGEX** (`.ogex`) | Open Game Engine Exchange: text (OpenDDL), meshes, skins, animation, materials | low-medium (growing) | medium | 6 | yes |
| 3 | **MDL** (`.mdl`): Quake 1, Half-Life 1, 3D GameStudio MDL3/4/5/7 | four unrelated binary formats under one extension | medium (HL1 models are common) | Quake 1: easy; HL1: medium (bones, embedded textures); 3DGS: medium to hard (7 has skins and animation) | 43 (HL1, MDL3, MDL5, MDL7, alpha tests) | yes |
| 4 | **IQM** (`.iqm`) | Inter-Quake Model: binary, skeleton, animation; used by Cube 2, Sauerbraten, open engines | low-medium | medium | 1 (`mrfixit.iqm`, skinned and animated) | yes |
| 5 | **COB** (`.cob`) | Caligari trueSpace; binary and ASCII | low (old) | medium | 8 (dwarf, molecule, spider: binary and ASCII) | yes |
| 6 | **LWS / MOT** (`.lws` `.mot`) | LightWave **scene** (objects by reference, cameras, lights, motion curves) | low-medium | medium (our LWO reader covers the objects it names) | 15 (envelopes, pre/post behaviours) | yes |
| 7 | **MMD / PMX** (`.pmx`) | MikuMikuDance models: binary, bones, morphs, materials | medium in its community | medium | 1 (`Alicia_blade.pmx`) | yes |
| 8 | **Q3D** (`.q3o` `.q3s`) | Quick3D | very low | easy | 4 | yes |
| 9 | **SIB** (`.sib`) | Silo | very low | easy | 1 (`heffalump.sib`) | yes |
| 10 | **CSM** (`.csm`) | CharacterStudio motion capture (markers only, like BVH) | very low | easy | 1 | yes |
| 11 | **MDC** (`.mdc`) | Return to Castle Wolfenstein models (MD3 with compressed frames) | very low | easy (MD3 is done) | 1 (`spider.mdc`) | yes |
| 12 | **HMP** (`.hmp`) | 3D GameStudio terrain | very low | easy | 2 | yes |
| 13 | **Terragen** (`.ter`) | Terragen heightfield | low | easy | 2 | yes |
| 14 | **RAW** (`.raw`) | plain triangle list | very low | easy | 4 (one empty) | yes |
| 15 | **Quake III BSP** (`.bsp` in `.pk3`) | Quake 3 map in a zip | low | medium-hard (faces, patches, shaders) | 1 (`SGDTT3.pk3`) | yes |
| 16 | **Nendo** (`.ndo`) | Nendo modeller | almost none | easy-medium | **0: no file in the tree, so it cannot be checked against Assimp** | yes |
| 17 | **glTF 1.0** (`.gltf`, `.glb`) | the old glTF; our glTF reader reads 2.0 only | low (legacy, but real models exist) | medium | 7 (inside the 54 glTF files of the corpus, they fail in our reader on purpose) | yes |
| 18 | **M3D** (`.m3d` `.a3d`) | Model 3D: a newer compact binary and ASCII format | low | medium | 8 | no |
| 19 | **Unreal** (`.3d` `.uc`) | Unreal Engine 1 mesh pair (`_a.3d` / `_d.3d` and `.uc`) | very low | easy-medium | 6 | no |
| 20 | **USD** (`.usd` `.usda` `.usdc` `.usdz`) | Pixar Universal Scene Description: text (`usda`), binary crate (`usdc`), zip (`usdz`) | rising, large | `usda`: medium; `usdc` crate: hard; full USD (composition, references): very hard | 7 here plus a folder of 15 in `models-nonbsd/USD` | no |
| 21 | **IFC** (`.ifc` `.ifczip` `.step`) | building models; STEP files with the huge EXPRESS schema | low for games | **very hard**: the schema has about 700 entity types, geometry needs a boolean-operation and curve kernel; honest estimate: months, and the result stays partial | 1 (`AC14-FZK-Haus.ifc`) | no |
| 22 | **Assbin** (`.assbin`) | Assimp's own dump of its scene | none outside Assimp | not worth a reader; it can simply go | 0 in the tree | no |
| 23 | **Cinema 4D** (`.c4d`) | Maxon's format; Assimp's importer needs the non-free Maxon SDK and is off in every normal build | medium | the format is proprietary and undocumented: writing our own means reverse engineering | 0 in the tree | no (and not buildable without the SDK) |

Rows 1 to 17 are what is left of the 45 importers our Windows build switches on (row 1 is the half of OGRE; glTF 1.0 is row 17); rows 18 to 23 are not switched
on, so removing Assimp does not remove anything a user has today for them.

## Proposed order for the next readers

Order of what to write next, by (how likely a user meets it) x (how small it is) x (whether real files exist to check against):

Done in batch D: AC3D, XGL / ZGL, the Irrlicht scene and NFF (all four with real files compared), and the XML half of OGRE.

1. **Ogre, the rest**: `.skeleton.xml` (bones, bind pose, animations, vertex weights) on the XML reader, then the binary `.mesh` and `.skeleton`: many real game assets.
2. **MDL**, in this order: Quake 1, then Half-Life 1, then 3D GameStudio 3/5/7 (43 real files).
3. **IQM**, **OpenGEX**, **COB**, **LWS**, **PMX**: one real file or a handful each; each is a few days.
4. The tiny ones in one batch: **Q3D, SIB, CSM, MDC, HMP, Terragen, RAW, Unreal**.
5. **Quake III BSP** and **glTF 1.0**: medium; glTF 1.0 is the one with the largest real-file count (7) after MDL.
6. **Nendo**: last, and only from a published description, because no file exists to check it against; honest status "unverified".
7. Not in our build, so not needed to remove Assimp from the Windows distribution: **M3D, USD, IFC, Assbin, C4D**. If the owner wants them anyway: USD `usda` first, then `usdc`; IFC is a project of its own; Assbin can be dropped; C4D needs a decision about reverse engineering.

## How far this is from removing Assimp

See the last section of `docs/own_code_roadmap.md` (the Assimp row) and `docs/model_corpus.md` ("Flip readiness"). In short: 16 importers
our build still switches on have no own reader and OGRE has half of one; the 29 that have one are all off by default; none was checked on real Windows (only
MinGW under wine for the first batch); no default may be flipped without the owner's approval; and the release note that announces
the dropped formats one release ahead is not written. The remaining formats are almost all small; the real cost is the
hostile-input work and the real-file comparison for each, about the same per format as the ten done in batches C and D.
