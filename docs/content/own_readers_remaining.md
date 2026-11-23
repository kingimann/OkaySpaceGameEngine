# Model formats Assimp reads that we have no own reader for

> **What this is:** Goal (owner): no third-party dependency. Assimp v5.4.3 is the last big one on the model-import side.
>
> **Status:** Reference page for this feature. Kept as written; not extended (rule 5).
>
> **Last checked:** 2026-11-20 (moved into `content/`, paths and links checked; the text itself was not re-checked against the code).

Goal (owner): no third-party dependency. Assimp v5.4.3 is the last big one on the model-import side. This page lists every
format it can read that we cannot yet read ourselves, says how hard each one is, whether Assimp's own test directory has real
files for it, and in what order to write them. The rule in `docs/status/own_code_roadmap.md` stays: a format is **not dropped
because it is hard**; its replacement is written, kept off by default, checked, and only flipped with the owner's approval.

Source of the list: `code/Common/ImporterRegistry.cpp` of the Assimp v5.4.3 checkout (the version `OKAY_USE_ASSIMP`
fetches), the extension list each importer declares, the importers `engine/CMakeLists.txt` really switches on, and the files
under `test/models` and `test/models-nonbsd` of that checkout (the real-file corpus of `docs/content/model_corpus.md`). The counts
below were made by counting those files by extension; where I could not check something it says so.

## The numbers

| | Count |
|---|---|
| Importers in Assimp v5.4.3's registry | **51** (glTF 1.0 and glTF 2.0 are two importers) |
| Importers our Windows distribution switches on (`engine/CMakeLists.txt`) | **45** (not switched on: USD, M3D, Unreal, IFC, Assbin, and Cinema 4D, which needs the non-free Maxon SDK and is off in every build) |
| Importers we covered **before batch C** (including Blender, merged as #829, off by default) | 19 |
| Importers batch C added (MD2, MD3, Irrlicht mesh, B3D, DirectX .x) | 5 (24) |
| Importers batch D adds (AC3D, XGL / ZGL, Irrlicht scene, NFF, and OGRE **for the XML form only**) | 5 (**29** of the 45 switched on; OGRE counts as half: its binary `.mesh` / `.skeleton` and the `.skeleton.xml` are row 1 below) |
| Terragen (`.ter`, `docs/content/model_readers.md`), after batch D | 1 (**30** of the 45 switched on) |
| Importers batch E adds (MDC, HMP, CSM; `docs/content/model_readers.md`, `own_hmp_reader.md`, `own_csm_reader.md`) | 3 (**33** of the 45 switched on, with Terragen) |
| Importers batch E adds (Q3D and RAW, written from public format descriptions, unverified against real files; SIB was not written, see row 9) | 2 (**35** of the 45 switched on, with Terragen and MDC, HMP, CSM) |
| Importers batch E3 adds (IQM, COB; **Nendo is a seam only, no geometry**) | 2 (**37** of the 45 switched on, hand-derived: 35 from main plus 2; Nendo not counted) |
| Importers batch E5 adds (LWS / MOT, PMX), off by default, **no real file to check them against** | 2 (**39** of the 45 switched on, hand-derived: 37 from main plus 2) |
| Switched on in our build and still **without an own reader** | **6** (rows 2, 3, 9, 15, 16, 17 below, with Nendo's row 16 still open) plus OGRE's half; hand-derived (45 minus 39), not computed |
| Not switched on and without an own reader | 6 |

"Covered" means an own reader exists, is off by default, passes its tests and was compared with Assimp on the real files of the
tree. **Nothing was flipped.** Which formats the 39 are (hand-derived): glTF 2.0 and GLB, OBJ, STL, PLY and FBX (ours, always on or ufbx),
COLLADA, 3DS, BVH, OFF, SMD, MD5, MS3D, ASE, 3MF, AMF, X3D, DXF, LWO, Blender, MD2, MD3, Irrlicht mesh, B3D, DirectX .x,
AC3D, OGRE (XML only), XGL / ZGL, Irrlicht scene, NFF, Terragen, and in batch E MDC, HMP, CSM, Q3D and RAW, in batch E3 IQM and COB, and in batch E5 LWS / MOT and PMX. How the batch D readers compare with Assimp on the real files is in
`docs/content/model_corpus.md` and in each reader's own page (`docs/content/model_readers.md`, `own_ogre_reader.md`, `own_xgl_reader.md`,
`own_irr_reader.md`, `own_nff_reader.md`).

## Extensions, not only importers (2026-11-16)

Counting importers hides extensions: an importer we cover can still take an extension our reader does not. The full list of
the 64 extensions the shipped Windows build hands to Assimp, with corpus counts and who reads each, is in
`docs/status/own_code_roadmap.md`, section **"What still needs Assimp"**. The gap batch of 2026-11-16 added `.mesh` (OGRE binary),
`.zae` (COLLADA in a ZIP) and `.xml` (by root element), all off by default. Still without a reader of ours: `.ask`, `.prj`,
`.md5camera`, `.vta`, `.lxo`, `.scn`, `.x3db`, `.vrm`, the Nendo body (`.ndo`), the OGRE skeletons, and SIB. **2026-11-22** (branch `claude2/own-readers-last`): the glTF 1.0, OpenGEX, MDL (Quake 1 and Half-Life 1) and Quake III
BSP readers written on `claude/own-readers-6` are on main now, each off by default (`OKAY_OWN_GLTF1`, `OKAY_OWN_OPENGEX`,
`OKAY_OWN_MDL`, `OKAY_OWN_Q3BSP`), checked on the real corpus files and measured against Assimp (`docs/content/own_readers_speed.md`).
The SIB reader of that branch was **not** brought over: it was worked out from the one sample file, and there is no public
description of the format (the rule for this work is a public specification or nothing).

## What is left

"How common": how often the format shows up in the wild today (my judgement, not a measurement). "How hard": my estimate of
the work for a reader of the same quality as the ones above (hostile-input tests, mutation fuzz, real-file comparison):
**easy** = a day of work, **medium** = several days, **hard** = weeks. "Real files" counts the files of that format in
Assimp's `test/models` and `test/models-nonbsd` (so the comparison with Assimp can be done); all of them are **real**,
none were invented.

| # | Format (extensions) | What it is | How common | How hard | Real files in the tree | Switched on in our build |
|---|---|---|---|---|---|---|
| 1 | **Ogre, the rest** (`.skeleton`, `.skeleton.xml`; the binary `.mesh` is **done**, 2026-11-16, off by default, `OKAY_OWN_OGREBIN`, `docs/content/model_readers.md`; the XML skeleton and the skinning of both mesh forms with it are done, 2026-11-20; the binary `.skeleton` is written, 2026-11-21, found next to a mesh only with `OKAY_OWN_OGRE_SKELBIN=1`, off by default, and equals the XML twins on the real fish and ninja) | OGRE3D: the skeleton in both forms with bones, bind pose and animations (the XML and binary mesh geometry and materials are done) | medium-high | skeleton: medium (needs the skin and clip path of the other skinned readers) | 6 (`fish`, `ninja`, `razor` as `.mesh` and the skeletons as binary and XML) | yes |
| 2 | ~~**OpenGEX** (`.ogex`)~~ **done 2026-11-22, off by default** (`docs/content/model_readers.md`; 4 of 6 real files read, the other 2 have no node) | Open Game Engine Exchange: text (OpenDDL), meshes, skins, animation, materials | low-medium (growing) | medium | 6 | yes |
| 3 | **MDL** (`.mdl`): Quake 1 and Half-Life 1 **done 2026-11-22, off by default** (`docs/content/model_readers.md`; 17 of 43 real files read); 3D GameStudio MDL3/4/5 **done 2026-11-23** from Conitec's published layout and MDL7 from our analysis of three real files (all 6 corpus files read; MDL4 unverified) | four unrelated binary formats under one extension | medium (HL1 models are common) | Quake 1: easy; HL1: medium (bones, embedded textures); 3DGS: medium to hard (7 has skins and animation) | 43 (HL1, MDL3, MDL5, MDL7, alpha tests) | yes |
| 4 | ~~**IQM** (`.iqm`)~~ **DONE in batch E3, off by default** (`docs/content/model_readers.md`): skeleton, skin and animation; read on the real `mrfixit.iqm` (numbers match its header); not compared with Assimp's output | low-medium | done | 1 (`mrfixit.iqm`, skinned and animated) | yes |
| 5 | ~~**COB** (`.cob`)~~ **DONE in batch E3, off by default** (`docs/content/model_readers.md`): binary and ASCII, objects, materials, textures; all 8 real files read and the two encodings agree; **no animation, bones or lights**; not compared with Assimp's output | low (old) | done (geometry only) | 8 (dwarf, molecule, spider: binary and ASCII) | yes |
| 6 | ~~**LWS / MOT** (`.lws` `.mot`)~~ **done, batch E5, off by default** (`docs/content/model_readers.md`) | LightWave **scene** (objects by reference, cameras, lights, motion curves) | low-medium | medium (our LWO reader covers the objects it names) | 15 (envelopes, pre/post behaviours) **not in this repository: unverified against real files** | yes |
| 7 | ~~**MMD / PMX** (`.pmx`)~~ **done, batch E5, off by default** (`docs/content/model_readers.md`) | MikuMikuDance models: binary, bones, morphs, materials | medium in its community | medium | 1 (`Alicia_blade.pmx`) **not in this repository: unverified against real files** | yes |
| 8 | ~~**Q3D** (`.q3o` `.q3s`)~~ **done, off by default** (`docs/content/model_readers.md`, unverified against real files) | Quick3D | very low | easy | 4 | yes |
| 9 | **SIB** (`.sib`): **written 2026-11-23, off by default** (`OKAY_OWN_SIB`), worked out from the one sample file (no public description exists; `docs/content/model_readers.md`) | Silo | very low | easy once a description or the sample is at hand | 1 (`heffalump.sib`) | yes |
| 10 | **CSM** (`.csm`): **own reader written, off by default** (`OKAY_OWN_CSM`, `docs/content/model_readers.md`) | CharacterStudio motion capture (markers only, like BVH) | very low | easy | 1 (read; checked against numbers from the file, not against Assimp) | yes |
| 11 | **MDC** (`.mdc`): **own reader written, off by default** (`OKAY_OWN_MDC`, `docs/content/model_readers.md`) | Return to Castle Wolfenstein models (MD3 with compressed frames) | very low | easy (MD3 is done) | 1 (`spider.mdc`, read; not compared with Assimp) | yes |
| 12 | **HMP** (`.hmp`): **own reader for HMP4, HMP5 and HMP7** (HMP4 and HMP5 added 2026-11-23 from Conitec's published HMP5 description; `OKAY_OWN_HMP`, `docs/content/model_readers.md`) | 3D GameStudio terrain | very low | easy | 2 (both are HMP7 and read; not compared with Assimp; other HMP versions are refused) | yes |
| 13 | **Terragen** (`.ter`): **own reader written, off by default** (`OKAY_OWN_TER`, `docs/content/model_readers.md`) | Terragen heightfield | low | easy | 2 (both read and equal Assimp) | yes |
| 14 | ~~**RAW** (`.raw`)~~ **done, off by default** (`docs/content/model_readers.md`; text form only, unverified against real files) | plain triangle list | very low | easy | 4 (one empty) | yes |
| 15 | **Quake III BSP** (`.bsp` in `.pk3`) | Quake 3 map in a zip | low | medium-hard (faces, patches, shaders) | 1 (`SGDTT3.pk3`) | yes |
| 16 | **Nendo** (`.ndo`) | Nendo modeller | almost none | easy-medium | **0: no file in the tree, so it cannot be checked against Assimp** | yes. **Batch E3 added only the seam and the header check** (`docs/content/model_readers.md`): the body is not read, because no description of it exists that rule 19a lets us follow; it still needs a spec or real files |
| 17 | ~~**glTF 1.0** (`.gltf`, `.glb`)~~ **done 2026-11-22, off by default** (`docs/content/model_readers.md`; all 7 real files read) | the old glTF; our glTF reader reads 2.0 only | low (legacy, but real models exist) | medium | 7 (inside the 54 glTF files of the corpus, they fail in our reader on purpose) | yes |
| 18 | **M3D** (`.m3d` `.a3d`) | Model 3D: a newer compact binary and ASCII format | low | medium | 8 | no |
| 19 | **Unreal** (`.3d` `.uc`) | Unreal Engine 1 mesh pair (`_a.3d` / `_d.3d` and `.uc`) | very low | easy-medium | 6 | no |
| 20 | **USD** (`.usd` `.usda` `.usdc` `.usdz`) | Pixar Universal Scene Description: text (`usda`), binary crate (`usdc`), zip (`usdz`) | rising, large | `usda`: medium; `usdc` crate: hard; full USD (composition, references): very hard | 7 here plus a folder of 15 in `models-nonbsd/USD` | no |
| 21 | **IFC** (`.ifc` `.ifczip` `.step`) | building models; STEP files with the huge EXPRESS schema | low for games | **very hard**: the schema has about 700 entity types, geometry needs a boolean-operation and curve kernel; honest estimate: months, and the result stays partial | 1 (`AC14-FZK-Haus.ifc`) | no |
| 22 | **Assbin** (`.assbin`) | Assimp's own dump of its scene | none outside Assimp | not worth a reader; it can simply go | 0 in the tree | no |
| 23 | **Cinema 4D** (`.c4d`) | Maxon's format; Assimp's importer needs the non-free Maxon SDK and is off in every normal build | medium | the format is proprietary and undocumented: writing our own means reverse engineering | 0 in the tree | no (and not buildable without the SDK) |

Rows 1 to 17 (rows 6 and 7 are done, see their pages) are what was left of the 45 importers our Windows build switches on (row 1 is the half of OGRE; glTF 1.0 is row 17); rows 18 to 23 are not switched
on, so removing Assimp does not remove anything a user has today for them.

## Proposed order for the next readers

Order of what to write next, by (how likely a user meets it) x (how small it is) x (whether real files exist to check against):

Done in batch D: AC3D, XGL / ZGL, the Irrlicht scene and NFF (all four with real files compared), and the XML half of OGRE. After an outside review the batch has per-file budgets (triangles, vertices, subdivided corners), exception-to-refusal at the import seam, OGRE strips and fans read as strips and fans, and scene files that may only name files inside their own folder (`tests/test_batchd_budget.cpp`).

1. **Ogre, the rest**: `.skeleton.xml` (bones, bind pose, animations, vertex weights) on the XML reader, then the binary `.mesh` and `.skeleton`: many real game assets.
2. **MDL**, in this order: Quake 1, then Half-Life 1, then 3D GameStudio 3/5/7 (43 real files).
3. **OpenGEX**: one real file or a handful; a few days. (IQM and COB are done, batch E3; **LWS / MOT** and **PMX** are written, batch E5, off by default, unverified on real files: the next step for them is a comparison on Assimp's `test/models` files, especially the radians and bank-first assumptions in the LWS reader.)
4. The tiny ones in one batch: **Q3D, SIB, RAW, Unreal** (Terragen is done: `docs/content/model_readers.md`; CSM, MDC and HMP are done, batch E: off by default, written from format descriptions and sample files only, with no importer source read and Assimp not run on them).
5. ~~**Quake III BSP** and **glTF 1.0**~~: done 2026-11-22 (with OpenGEX and Quake 1 / Half-Life 1 MDL), off by default.
6. **Nendo**: last, and only from a published description, because no file exists to check it against; honest status "unverified".
7. Not in our build, so not needed to remove Assimp from the Windows distribution: **M3D, USD, IFC, Assbin, C4D**. If the owner wants them anyway: USD `usda` first, then `usdc`; IFC is a project of its own; Assbin can be dropped; C4D needs a decision about reverse engineering.

## How far this is from removing Assimp

See the last section of `docs/status/own_code_roadmap.md` (the Assimp row) and `docs/content/model_corpus.md` ("Flip readiness"). In short: 6 importers
our build still switches on have no own reader and OGRE has half of one; the 39 that have one are all off by default; none was checked on real Windows (only
MinGW under wine for the first batch); no default may be flipped without the owner's approval; and the release note that announces
the dropped formats one release ahead is not written. The remaining formats are almost all small; the real cost is the
hostile-input work and the real-file comparison for each, about the same per format as the ten done in batches C and D.
