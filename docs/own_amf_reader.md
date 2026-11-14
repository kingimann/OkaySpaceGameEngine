# Our own AMF reader (`.amf`)

Status: **side by side, off by default** (`OKAY_OWN_AMF`, same rules as `docs/own_3mf_reader.md`, which also describes the shared XML and zip readers and their limits). Assimp (when built) is still the only other reader of `.amf`.

## What it is

Written from the public description of the ISO/ASTM 52915 Additive Manufacturing File Format, with no code from any other importer.

| File | What |
|---|---|
| `engine/src/Render/AmfReader.cpp`, `engine/include/okay/Render/AmfReader.hpp` | `okay::ReadAmfBuffer` / `okay::ReadAmfFile` |
| `engine/src/Render/XmlLite.hpp`, `ZipLite.hpp`, `ReaderCommon.hpp` | Shared with 3MF (see `docs/own_3mf_reader.md`) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 12 of the batch-B table |
| `tests/test_amf_reader.cpp` | Hand-computed checks on synthetic files and the seam (`amf_reader`) |
| `tests/test_batchb_hostile.cpp` | Hostile input and mutation fuzz (`batchb_hostile`) |

## What it reads

- **Container**: plain XML, or a zip archive (the first entry ending `.amf`, else the first file). Zip entries are checked as for 3MF (size caps against bombs, CRC, no encryption).
- **Objects**: `object` / `mesh` / `vertices` / `vertex` / `coordinates` and `volume` / `triangle` (`v1`, `v2`, `v3`). One node per object instance (named by its `name` metadata, else `object<id>`) and one part per resolved colour.
- **Colours**, in the format's order of priority: a triangle's `color`, else the volume's `materialid` material, else the volume's `color`, else the object's `color`. A `material` gives its `color` and its `name` metadata. Channels are clamped to 0..1; alpha becomes the diffuse colour's alpha. Colours that are formulas or composites are not read (noted).
- **Constellations**: the first `constellation`: each `instance` places an object with `deltax/y/z` and a rotation `rx`, `ry`, `rz` in degrees (x first, then y, then z, then the move); a negative determinant mirrors (winding follows). Objects not in the constellation are not output (noted). Without a constellation every object is output where it is. A second constellation and nested constellations are not read (noted).
- **Axes and units**: Z-up like 3MF (root node rotation, baked by the flattened path); units kept as written, a unit other than millimetre noted. Smooth normals with a 45 degree crease angle (the format's own vertex normals are not used, noted).

## Not handled (each is also in `notes`)

Textures (`texture`, `texmap`, `map`), vertex colours (the format puts them above every other colour; `Mesh` has no vertex colours, so they are reported), vertex normals, curved edges (`edge`), colour formulas and composite materials, further and nested constellations, INSERT-style arrays. The XML tree is limited to 5,000,000 elements. Limits and the hostile-input rules are those of `docs/own_3mf_reader.md`; the instance count is capped at 100,000.

## Tests and what was checked

- `amf_reader` (`tests/test_amf_reader.cpp`): **synthetic** documents: a tetrahedron (area, flattened Z-up bounds, flat normals), materials and the colour priority (triangle over volume material over object colour), formula colours and textures (noted), a constellation with a rotation and a move computed by hand (`rz = 90`, delta 10: `(x, y) -> (-y + 10, x)`), the x-then-y-then-z rotation order, two instances of one object, units, a zip-compressed AMF (also with a leading unrelated entry), an out-of-range index, every refusal, and the seam.
- `batchb_hostile` (AMF section): every truncation, XML bombs, bad numbers and indices, absurd constellation values (`1e300`, `nan`), 100,001 instances (refused) and 3,000 (read), 3,000 distinct triangle colours, zip bombs and truncated zips, then the mutation fuzz over plain and zipped files. Sanitizers: clean.

### Real files

The 11 files in Assimp's `test/models/AMF` (hand-made by its authors, under 3 kB each) all read; Assimp 5.4.3 reads the same 11. **5 agree on everything compared** (meshes, triangles, materials, surface area, bounding box after the Z-up turn): `test1`, `test2`, `test3`, `test5`, `test5a`. The other 6 differ, and the reasons were checked in the files:

- `test4`, `test6`: one volume with a volume colour and a triangle with its own colour (the format puts the triangle's colour first); ours makes one part and one material per colour (2 and 2), Assimp one mesh with one material.
- `test7`, `test_with_mat`: an object with two volumes. Assimp returns only the first volume's triangles (4 of 8; the area is exactly half); ours reads both (a bipyramid, area 18.22).
- `test8`, `test9`: constellations. Ours places every instance with its delta and rotation (`test8`: two pyramids at x = -5 and 5, turned -45 and 45 degrees about x, box x in [-5.5, 6]). Assimp's box is 10 times wider (x in [-45.5, 46]); the offset it uses equals the rotation value, not the delta, so it looks like an Assimp bug; we did not debug it. `test9` has two constellations, ours uses the first and says so.

Notes our reader printed on the real files: 6 files with vertex colours (not read), `test_with_mat` has unit `inch` (kept), `test9` a second constellation. Not verified on real files: zip-compressed AMF, textures, curved edges, files from a slicer or CAD tool.
