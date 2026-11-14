# Our own MD3 reader (`.md3`)

Status: **side by side, off by default** (`OKAY_OWN_MD3`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is still the only other reader of `.md3`. Nothing was flipped.

## What it is

MD3 is the model format of Quake III Arena (1999) and the many games built on its engine. A file has several **surfaces** (each with its own shader and triangles), per-frame vertex positions, and **tags** (named attachment points that a weapon or a head is fixed to). Written from the public description of the format with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/Md3Reader.cpp` | `okay::ReadMd3Buffer` / `okay::ReadMd3File` (declared in `ModelData.hpp`) |
| `engine/src/Render/BinReader.hpp`, `ReaderCommon.hpp` | Shared cursor and mesh builder (see `docs/own_md2_reader.md`) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 18 of the opt-in table |
| `tests/test_md3_reader.cpp`, `tests/batchc_fixtures.hpp` | Hand-computed checks on a synthetic file (`md3_reader`) |
| `tests/test_batchc_hostile.cpp` | Hostile input and mutation fuzz (`batchc_hostile`) |

## What it reads

- Header (`IDP3`, version 15), every surface: **frame 0** positions (16-bit values in 1/64 units), the **stored normals** (two bytes: latitude and longitude), texture coordinates (as written, top-down, see `docs/own_md2_reader.md`), triangles (winding reversed), the first shader name as the texture. One part and one material per surface.
- **Tags** of frame 0 become empty child nodes of the root (position, and a rotation made from the three axis vectors; axes that are not a rotation, for example scaled or mirrored, leave the node unrotated and say so). The root turns Z-up into Y-up.

## Not handled (each is a line in `out.notes`)

- Frames after the first (vertex animation) and tag animation: the engine has no vertex animation clip.
- `.skin` and `.shader` files. Texture names are the shader names as written; Quake paths are relative to the game folder, not to the model, so they are **not** joined to the model's folder.
- Multi-part player models (`lower.md3`, `upper.md3`, `head.md3` joined by tags) are read one file at a time; they are not assembled.

## Limits

File 256 MiB; surfaces 4,096; vertices per surface 2,000,000; triangles per surface 4,000,000; tags 100,000; output vertices 20,000,000. Every offset (relative to the file or to its surface) and count is checked against the real file size before anything is allocated. Texture coordinates that are not finite become 0 and are counted.

## Tests and what was checked

- `md3_reader`: **synthetic** file built in memory (one triangle with edges 1 and 2, area 1 by hand; decoded normals (0,0,1), (1,0,0), (0,1,0); an empty second surface; a second frame that must be ignored; a tag at (5,6,7) turned 90 degrees about Z), texture coordinates, winding, node tree, notes, a skewed tag axis, refusals and **every truncation**.
- `batchc_hostile` (MD3 section): every header and surface-header field at 0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF and at and over each cap; NaN, infinity and 3.4e38 in tags and texture coordinates; every truncation; 200,000 mutations. Sanitizers (`address,undefined,float-cast-overflow`): clean.

### Real files

Assimp's tree has 3 MD3 files (`watercan.md3`, `watercan_dmg.md3`, and `european_fnt_v2.md3`, a five-surface map object). **All 3 read and equal Assimp v5.4.3 on triangles, surface area (to 2e-7 relative) and bounding box.** Assimp keeps three vertices per triangle, ours welds (2,034 against 703 on the map object).

Not verified: files with tags (none of the three has one: the tag path rests on the synthetic fixture), player models, files from tools other than Quake III's, a run on real Windows.
