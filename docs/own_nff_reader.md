# Our own NFF reader (`.nff` `.enff`)

Status: **side by side, off by default** (`OKAY_OWN_NFF`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is still the only other reader of NFF files. Nothing was flipped.

## What it is

NFF, the "Neutral File Format", is Eric Haines' text format for ray-tracing test scenes (the Standard Procedural Databases): a viewpoint, lights, fill colours and then polygons, polygonal patches, spheres and cones or cylinders. Two flavours are in use and both are read:

- **Classic NFF** (the SPD scenes, many small demo files), with the common extensions: ellipsoids, `tess`, and the regular solids `tet`, `hex`, `oct`, `dod`, `ico`.
- **Sense8 "nff" object files** (WorldToolKit): an `nff` header line, a name, a vertex list (optionally with `uv`), polygons by index with `matid`, `both` and a texture name, and an `mtable` file of materials (`.mat`).

| File | What |
|---|---|
| `engine/src/Render/NffReader.cpp` | `okay::ReadNffBuffer` / `okay::ReadNffFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/TextReaderUtil.hpp`, `ReaderCommon.hpp`, `PolygonTriangulate.hpp` | Shared pieces: line and token reading, number parsing, mesh builder, polygon triangulation |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 26 (extensions `.nff`, `.enff`) |
| `tests/test_nff_reader.cpp`, `tests/batchd_fixtures.hpp` | Hand-computed checks on synthetic files (`nff_reader`) |
| `tests/test_batchd_hostile.cpp` | Hostile input and mutation fuzz (`batchd_hostile`) |

## What it reads

**Classic.** `f r g b [Kd Ks Shine T IOR] [texture]` sets the current material (the colour, the shininess when given, and a texture name; the other coefficients are read and not applied: `T` in the sample files is a stray `1`, which would make everything invisible). `p n` (n vertex lines) is a polygon, `pp n` a polygonal patch (`x y z nx ny nz`: the normals are kept); consecutive polygons with the same material become one part, flat shaded. `s x y z r [ry rz]` is a sphere, or an ellipsoid when two more radii follow (`r` is the x radius). `c` with two lines (`base x y z radius`, `apex x y z radius`) is a cone or cylinder with both end caps (an end of radius 0 is a point). `tet`, `hex`, `oct`, `dod`, `ico` take `cx cy cz r [ry rz]`: a regular solid of circumradius `r` (per axis when three are given), centred there. `tess n` (default 4, at most 6) sets the fineness: a sphere is an icosahedron subdivided `n` times (20 x 4^n triangles, smooth normals), a cone or cylinder has 4^n segments. `v` / `from` / `at` / `up` / `angle` / `hither` / `resolution`, `b` and `l` lines are skipped and counted. Each sphere, cone and solid is a part of its own.

**Sense8.** The first line `nff` (a `//` comment after it is allowed; comments are `//`). `version`, `viewpos`, `viewdir` and similar header lines are skipped; then the object name, optionally `mtable file.mat` and the vertex count; vertex lines `x y z [uv u v]`; the polygon count; polygon lines `n i0 i1 ... [matid k] [0xcolour] [both] [texture]` (0-based indices). Polygons with the same `matid` and texture make one part. The `.mat` file next to the model supplies the materials (`matdef` blocks: `ambientdiffuse` as the colour, `emission`, `shininess`, `opacity` as alpha). More than one object in a file are read one after the other.

## Not handled (each is a line in `out.notes`)

Lights, the viewpoint, the background and the resolution; the diffuse, specular, transmission and refraction coefficients of `f`; polygon colour words (`0x...`); the two-sided flag `both` (the engine's meshes are one-sided); polygons with fewer than 3 vertices; solids of zero or negative size; unknown keywords (counted, the first named). **The exact tessellation of other programs is not reproduced**: ours is the rule above.

## Limits

File 256 MiB; 1,000,000 vertices per polygon; 10,000,000 vertices in a Sense8 list and 10,000,000 polygons; 2,000,000 primitives; 40,000,000 output vertices; `tess` at most 6. Counts must be whole numbers within the caps and the text must really hold that many lines, or the file is refused before anything large is allocated. Numbers go through `NumParse.hpp` (finite, within 1e15, locale independent). The `mtable` file is read from the model's folder only (16 MiB at most); a name that climbs out of it is read like any other path but cannot do more than fail.

## Tests and what was checked

- `nff_reader`: **synthetic** classic file (viewpoint, background, light, a red fill with shine 20, a polygon, a patch with normals, a `tess 1` cylinder, a green fill, a sphere and a cube solid): parts in file order, areas by hand (polygon 4, patch 4.5, cylinder `8 sqrt 2 + 4`, cube 8, sphere between the icosahedron's 9.5746 and 4 pi), triangle counts (cylinder 16, sphere 80, cube 12), unit-sphere vertices and normals, materials and shininess, notes; an ellipsoid with radii 1, 2, 3 (box and 320 triangles), the other solids (areas of the regular tetrahedron and dodecahedron by formula), `tess 0`; a synthetic Sense8 file with a real `.mat` file in a temporary folder (uv, matid, texture, alpha, both) with `\n` and `\r\n`; no table; refusals and **every truncation** of both flavours.
- `batchd_hostile` (NFF section): counts at, past and far past the caps with tiny text, `nan`, `inf`, `1e999` and friends in every kind of line, degenerate and huge cones and spheres, 600 spheres at `tess 6` and 300 cylinders at `tess 6` (the output cap), 100,000 polygons, a polygon of 100,000 vertices, a Sense8 file of 100,000 polygons, 5,000 fills, 3 MB tokens, `mtable` paths that climb out, name a folder or are 3 MB long; then 3,000 mutations of each flavour (30,000 in the sanitizer run).

### Real files

Assimp's tree has 13 `.nff` files (`models/NFF/NFF`: the platonic solids, spheres, cones, a camera file; `models-nonbsd/NFF/NFFSense8`: `teapot`, `HOME4`, `cokecan` with its `.mat`). **All 13 read in ours; 12 read in Assimp (`cone.nff` fails there: it does not accept the texture name on its fill line) and on those 12 the area and bounding box agree**: the platonic solids `tetrahedron`, `hexahedron`, `octahedron`, `dodecahedron` exactly (same areas, same boxes, same triangle counts), `spheres.nff` (area 734.92, box, 15,380 triangles: the same), `cylinder.nff` (1,024 triangles, area 1,532.0748 against 1,532.0757), `positionTest.nff` (72,704 triangles, area 2,690.3634 against 2,690.3630), `ManyEarthsNotJustOne.nff` (51,200 triangles, 10 parts) and `WithCamera.nff`, and the Sense8 files `teapot.nff` (3,752 triangles, area 44.9005 both), `HOME4.NFF` (12,174 triangles, area 104,514.4052 both) and `cokecan.nff` (28 triangles, area 2,831.1551 both, with its `.mat`). The tessellation rule above was worked out from Assimp's output on small synthetic probes (sphere 20 x 4^n triangles; tube segments 4^n); the one place it differs is `tess 3`, where Assimp produces 260 triangles for a cylinder (not 4^3 x 4 = 256 and not a regular shape: its own quirk, not copied). The material counts differ (Assimp makes one per primitive, ours merges equal ones), ours groups the Sense8 polygons of `cokecan` into 9 parts and Assimp into 6, and `HOME4` is 1 mesh in ours and 2 in Assimp (triangles, area and box are equal on all 12).

Unverified: the NFF of other programs (SPD scenes beyond the sample files), `.enff` files (none in the tree), per-vertex colours, a run on real Windows.

## Provenance (what this file was written from)

`NffReader.cpp` was written from scratch for this project from the public description of the Neutral File Format (E. Haines, "A Proposal for Standard Graphics Environments", IEEE CG&A 1987, and the SPD distribution's NFF page), the ellipsoid and regular-solid extensions as they appear in the sample files, and the Sense8 WorldToolKit "nff" and "mat" file descriptions as far as the sample files show them; the platonic solids are generated from their definitions (the icosahedron's edge length and the dodecahedron's face directions are computed, not typed in). Sample files of Assimp's `test/models*/NFF*` were read as data (never the Assimp source). Assimp was used only as a **black box**: run on the samples and on small synthetic files made for this purpose, its output showed the `tess` rule, that extra numbers after a sphere's radius are the y and z radii, and the orientation and size of the regular solids. No code, comment, table or test from any other importer was read, copied or ported.
