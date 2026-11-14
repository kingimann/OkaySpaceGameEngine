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
| Importers we covered **before** this batch (including Blender, which is in review on its own branch) | 19 |
| Importers we cover **now** (this batch adds MD2, MD3, Irrlicht mesh, B3D, DirectX .x) | **24** of the 45 switched on |
| Switched on in our build and still **without an own reader** | **21** (the table below, first 21 rows, plus glTF 1.0) |
| Not switched on and without an own reader | 6 |

"Covered" means an own reader exists, is off by default, passes its tests and was compared with Assimp on the real files of the
tree. **Nothing was flipped.** Which formats the 24 are: glTF 2.0 and GLB, OBJ, STL, PLY and FBX (ours, always on or ufbx),
COLLADA, 3DS, BVH, OFF, SMD, MD5, MS3D, ASE, 3MF, AMF, X3D, DXF, LWO, Blender (in review), MD2, MD3, Irrlicht mesh, B3D, DirectX .x.

## What is left

"How common": how often the format shows up in the wild today (my judgement, not a measurement). "How hard": my estimate of
the work for a reader of the same quality as the ones above (hostile-input tests, mutation fuzz, real-file comparison):
**easy** = a day of work, **medium** = several days, **hard** = weeks. "Real files" counts the files of that format in
Assimp's `test/models` and `test/models-nonbsd` (so the comparison with Assimp can be done); all of them are **real**,
none were invented.

| # | Format (extensions) | What it is | How common | How hard | Real files in the tree | Switched on in our build |
|---|---|---|---|---|---|---|
| 1 | **AC3D** (`.ac` `.acc` `.ac3d`) | AC3D modeller; text; objects, surfaces, materials, UVs; used by FlightGear and many sims | medium | easy | 11 (UTF-8, UTF-16, BOM, lines only, no surfaces, subdivision) | yes |
| 2 | **Ogre** (`.mesh.xml`, `.mesh`, with `.skeleton`) | OGRE3D engine: XML and binary mesh, skeleton, animations, materials | medium-high (many game assets) | XML: easy-medium; binary `.mesh` and `.skeleton`: medium | 7 (`fish`, `ninja`, `razor` as xml and binary, `TheThing`) | yes |
| 3 | **XGL / ZGL** (`.xgl` `.zgl`) | XML scene; `.zgl` is the same, zlib compressed | low | easy (the XML and inflate parts exist) | 7 | yes |
| 4 | **Irrlicht scene** (`.irr`, `.xml`) | XML scene of the Irrlicht engine: nodes, transforms, meshes by reference, animations | low-medium | medium (our `.irrmesh` reader is the mesh half) | 21 | yes |
| 5 | **NFF / Enhanced NFF** (`.nff` `.enff`) | "Neutral File Format": text; polygons, spheres, cones, cylinders | low (old ray-tracer demos) | easy | 13 | yes |
| 6 | **OpenGEX** (`.ogex`) | Open Game Engine Exchange: text (OpenDDL), meshes, skins, animation, materials | low-medium (growing) | medium | 6 | yes |
| 7 | **MDL** (`.mdl`): Quake 1, Half-Life 1, 3D GameStudio MDL3/4/5/7 | four unrelated binary formats under one extension | medium (HL1 models are common) | Quake 1: easy; HL1: medium (bones, embedded textures); 3DGS: medium to hard (7 has skins and animation) | 43 (HL1, MDL3, MDL5, MDL7, alpha tests) | yes |
| 8 | **IQM** (`.iqm`) | Inter-Quake Model: binary, skeleton, animation; used by Cube 2, Sauerbraten, open engines | low-medium | medium | 1 (`mrfixit.iqm`, skinned and animated) | yes |
| 9 | **COB** (`.cob`) | Caligari trueSpace; binary and ASCII | low (old) | medium | 8 (dwarf, molecule, spider: binary and ASCII) | yes |
| 10 | **LWS / MOT** (`.lws` `.mot`) | LightWave **scene** (objects by reference, cameras, lights, motion curves) | low-medium | medium (our LWO reader covers the objects it names) | 15 (envelopes, pre/post behaviours) | yes |
| 11 | **MMD / PMX** (`.pmx`) | MikuMikuDance models: binary, bones, morphs, materials | medium in its community | medium | 1 (`Alicia_blade.pmx`) | yes |
| 12 | **Q3D** (`.q3o` `.q3s`) | Quick3D | very low | easy | 4 | yes |
| 13 | **SIB** (`.sib`) | Silo | very low | easy | 1 (`heffalump.sib`) | yes |
| 14 | **CSM** (`.csm`) | CharacterStudio motion capture (markers only, like BVH) | very low | easy | 1 | yes |
| 15 | **MDC** (`.mdc`) | Return to Castle Wolfenstein models (MD3 with compressed frames) | very low | easy (MD3 is done) | 1 (`spider.mdc`) | yes |
| 16 | **HMP** (`.hmp`) | 3D GameStudio terrain | very low | easy | 2 | yes |
| 17 | **Terragen** (`.ter`) | Terragen heightfield | low | easy | 2 | yes |
| 18 | **RAW** (`.raw`) | plain triangle list | very low | easy | 4 (one empty) | yes |
| 19 | **Quake III BSP** (`.bsp` in `.pk3`) | Quake 3 map in a zip | low | medium-hard (faces, patches, shaders) | 1 (`SGDTT3.pk3`) | yes |
| 20 | **Nendo** (`.ndo`) | Nendo modeller | almost none | easy-medium | **0: no file in the tree, so it cannot be checked against Assimp** | yes |
| 21 | **glTF 1.0** (`.gltf`, `.glb`) | the old glTF; our glTF reader reads 2.0 only | low (legacy, but real models exist) | medium | 7 (inside the 54 glTF files of the corpus, they fail in our reader on purpose) | yes |
| 22 | **M3D** (`.m3d` `.a3d`) | Model 3D: a newer compact binary and ASCII format | low | medium | 8 | no |
| 23 | **Unreal** (`.3d` `.uc`) | Unreal Engine 1 mesh pair (`_a.3d` / `_d.3d` and `.uc`) | very low | easy-medium | 6 | no |
| 24 | **USD** (`.usd` `.usda` `.usdc` `.usdz`) | Pixar Universal Scene Description: text (`usda`), binary crate (`usdc`), zip (`usdz`) | rising, large | `usda`: medium; `usdc` crate: hard; full USD (composition, references): very hard | 7 here plus a folder of 15 in `models-nonbsd/USD` | no |
| 25 | **IFC** (`.ifc` `.ifczip` `.step`) | building models; STEP files with the huge EXPRESS schema | low for games | **very hard**: the schema has about 700 entity types, geometry needs a boolean-operation and curve kernel; honest estimate: months, and the result stays partial | 1 (`AC14-FZK-Haus.ifc`) | no |
| 26 | **Assbin** (`.assbin`) | Assimp's own dump of its scene | none outside Assimp | not worth a reader; it can simply go | 0 in the tree | no |
| 27 | **Cinema 4D** (`.c4d`) | Maxon's format; Assimp's importer needs the non-free Maxon SDK and is off in every normal build | medium | the format is proprietary and undocumented: writing our own means reverse engineering | 0 in the tree | no (and not buildable without the SDK) |

Rows 1 to 21 are what is left of the 45 importers our Windows build switches on (glTF 1.0 is the 21st); 22 to 27 are not switched
on, so removing Assimp does not remove anything a user has today for them.

## Proposed order for the next readers

Order of what to write next, by (how likely a user meets it) x (how small it is) x (whether real files exist to check against):

1. **AC3D**: text, a day, 11 real files. Also gives the shared "UTF-16 / BOM text" path a second user.
2. **Ogre** `.mesh.xml` + `.skeleton.xml` first (on the XML reader), then the binary `.mesh` and `.skeleton`: many real game assets, 7 files.
3. **XGL / ZGL**: nearly free once the XML reader and inflate exist, 7 files.
4. **Irrlicht scene (`.irr`)**: 21 real files, completes the Irrlicht pair started with `.irrmesh`.
5. **NFF**: trivial text, 13 files.
6. **MDL**, in this order: Quake 1, then Half-Life 1, then 3D GameStudio 3/5/7 (43 real files).
7. **IQM**, **OpenGEX**, **COB**, **LWS**, **PMX**: one real file or a handful each; each is a few days.
8. The tiny ones in one batch: **Q3D, SIB, CSM, MDC, HMP, Terragen, RAW, Unreal**.
9. **Quake III BSP** and **glTF 1.0**: medium; glTF 1.0 is the one with the largest real-file count (7) after MDL.
10. **Nendo**: last, and only from a published description, because no file exists to check it against; honest status "unverified".
11. Not in our build, so not needed to remove Assimp from the Windows distribution: **M3D, USD, IFC, Assbin, C4D**. If the owner wants them anyway: USD `usda` first, then `usdc`; IFC is a project of its own; Assbin can be dropped; C4D needs a decision about reverse engineering.

## How far this is from removing Assimp

See the last section of `docs/own_code_roadmap.md` (the Assimp row) and `docs/model_corpus.md` ("Flip readiness"). In short: 21 importers
our build still switches on have no own reader; the 24 that do are all off by default; none was checked on real Windows (only
MinGW under wine for the first batch); no default may be flipped without the owner's approval; and the release note that announces
the dropped formats one release ahead is not written. The remaining formats are almost all small; the real cost is the
hostile-input work and the real-file comparison for each, about the same per format as the five done here.
