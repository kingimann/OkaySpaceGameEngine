# Our own SIB reader (`.sib`, Nevercenter Silo)

Status: **written from one sample file, side by side, off by default** (`OKAY_OWN_SIB`, slot 32 of the table in `ModelImport.cpp`; same rules as `docs/own_3mf_reader.md`). It replaces the earlier "not written" page. Assimp (when built) is still the only other reader of `.sib`. Nothing was flipped.

**Read this first.** There is no public description of the Silo format that we may follow, and the only Silo file we have is `heffalump.sib` (Richard Mitton; the readme next to it says "I waive all rights to this and release it into the public domain"; it comes from the `test/models/SIB` directory of Assimp v5.4.3 and is shipped as a sample in `examples/own_readers` with its two pictures, recorded in `docs/THIRD_PARTY.md`). The reader follows **the structure that file shows**. Everything marked "assumption" in the table below is a guess from that one file and may be wrong for files made by other Silo versions or with other features (mirrors, instances, subdivision, other UV sets). Do not treat this as a finished SIB reader.

## How to try it

Steps for the Windows release are in `docs/try_it.md`, section "Batch E6 readers". Short form: start the editor from a Command Prompt with `set OKAY_OWN_SIB=1` (or build with `-DOKAY_OWN_SIB=ON`), choose **File > Import Model...** and pick `examples\own_readers\heffalump.sib` (keep the two `.png` files next to it). You get a large flat ground square with the Heffalump (a stuffed elephant-like figure), a block, a cylinder and a "Mirrored" cylinder on it. The Console shows lines starting `SIB:` (what is not read); Assimp writes none.

## What it is

| File | What |
|---|---|
| `engine/src/Render/SibReader.cpp` | `okay::ReadSibFile` / `okay::ReadSibBuffer` (declared in `ModelData.hpp`) |
| `tests/test_sib_reader.cpp`, `tests/sib_fixtures.hpp` | Hand-computed checks on synthetic files, every truncation, 30,000 mutations, every aligned word at extreme values, budgets (`sib_reader`) |
| `tests/test_own_sib.cpp`, `examples/own_readers/heffalump.sib` | The seam routes `.sib`; `-DOKAY_OWN_SIB=ON` makes it the default; the real file imports with the numbers worked out from its bytes (`own_sib`) |

## The structure we found (all from `heffalump.sib`)

A file is a list of **chunks**: four ASCII letters, a 32 bit little endian byte size, that many bytes. The file starts with `HEAD` (size 4, the value 1: the version; any other value is refused). Top level chunks in the sample: `MATR` x4, `SHAP` x5, `INST` x4, `LGHT` x3, `TEXP` x5, `GRPS` x1.

| Chunk | Layout (as read) | Basis |
|---|---|---|
| `MATR` | 16 floats (four colours of four), a 32 bit flags word, the name, the texture file name; a string is a 32 bit byte count then UTF-16LE | sizes add up exactly in all four materials. **Assumption:** the first colour is the diffuse colour (the other two non-zero groups may be ambient and specular); it is the only one used |
| `SHAP` | sub-chunks, see below | consistent in all five shapes |
| `SHAP/SNAM` | the shape name, UTF-16LE, no length word | |
| `SHAP/VRTS` | `n` vertices of three floats | **Assumption:** world positions. The cube's vertices centre exactly on its `AXIS` origin, so the axes are not applied again |
| `SHAP/FACS` | per face: a count `k`, then `k` vertex numbers (n-gons: 3, 4 and 5 corners in the sample) | the counts add up to the chunk size in all shapes; the winding gives a **positive signed volume** for the closed shapes (16.157, 0.207, 1.04), so the faces are counter clockwise from outside and are kept as they are |
| `SHAP/FTVS` | per face: an index word, a corner count equal to the face's, then the `(u, v)` pairs | consistent in all shapes. **Assumption:** v points up, so v is flipped (`v' = 1 - v`) like our other bottom-up formats. The cube's v values run from -1 to 0, which fits a v-up convention but does not prove it |
| `SHAP/FAMA` | pairs `(first face, material)`; a material applies from that face to the next pair | the cube has `(0,1) (2,2) (3,1) (4,2) (5,1)`, the single-material shapes `(0,m)`; a material of -1 means none. **Assumption:** run-length meaning (the cube's pattern fits it) |
| `SHAP/IMRP` | two ints; the second is 1 on the shape named "Mirrored" | **Assumption:** a mirror plane (`MIRP`) is switched on. The mirrored half is **not generated**; a note says so |
| `DINF PINF AXIS MIRP VMIR EDGS ECRS TXSM FAHS FMIR` | seen, not used | edges and mirror correspondences are not needed to build triangles |
| `INST` | an instance of a shape: a name (`SNAM`), `INSI` (a shape number), an `AXIS`, an `SMTX` of 64 bytes | **not read**: the four instances (copies of the cube) are not placed, because how `AXIS` and `SMTX` combine is not understood. A note says so |
| `LGHT`, `GRPS`, `TEXP` | seen, not used | `TEXP` repeats each shape with an extra `TEXF` chunk; not understood |

The model is one root node `SIB` with one child node per shape (named by `SNAM`) and one part per (shape, material). Faces with more than 3 corners are triangulated (ear clipping for concave ones up to 32 corners and a per-file work budget, a fan beyond that; a note counts them). There are **no stored normals**: smooth normals are generated. The file is Y up (the sample stands on y = 0), so no axis turn. A texture file name must lie inside the model's folder (no absolute path, no `..`, no link out), like the scene readers.

## Budgets

File 256 MB; chunks 2,000,000; shapes 100,000; materials 100,000; 4,000,000 vertices per shape and per file; 16,000,000 face corners; 4,000,000 triangles. Every count is tested against the bytes that are there before anything is allocated.

## What was checked

* **Synthetic files** (`sib_reader`): a cube whose six faces are split over two materials (two parts of 3 faces), an L shaped concave hexagon (area 3, no triangle leaves the L), v flipped, the winding kept, the texture rules, bad vertex numbers, short faces, a mirror note, a material number past the table, truncated chunks, every truncation of the file, 30,000 mutations (set `OKAY_READER_FUZZ_ITERS` for more), every aligned word at five extreme values, a 40,000 corner face (fanned), five million triangles (refused naming the budget).
* **The real file**, numbers worked out from the bytes by a separate Python script before the reader was run: 6 nodes (root plus 5 shapes: Heffalump, Original, Grid, Cylinder, Mirrored), 4 materials (`HeffalumpSkin`, `UpMtl`, `Mapping`, `Ground`; `UpMtl` and `Mapping` point at the two pictures), 6 parts, **676 triangles** (342 + 12 + 2 + 120 + 200), surface area 158.60 (fan), bounding box x and z from -4.91 to 4.91, y from -0.023 to 3.165, signed volume of the Heffalump 16.157. The reader gives the same (`own_sib`). **Not compared with Assimp** (not run), so differences in instances, mirrors, normals and colours are unknown.

## Provenance (rule 19a)

Written from an inspection of `heffalump.sib` only (hex dumps and our own parsing scripts), not from any description or importer. **No importer source of any other project was read**: the Assimp checkout used to get the test files was a sparse checkout of the `test/models` directories only (no `code/` directory). Before this the page said "no sample available"; the sample was found in that test tree.

## Not done

Instances, mirror generation, lights, groups, TEXP, other colours of a material, subdivision data, files from other Silo versions, any file but the one sample, and a comparison with Assimp's output.
