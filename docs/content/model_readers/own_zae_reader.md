# Our own ZAE reader (`.zae`: COLLADA in a ZIP archive)

Status: **side by side, off by default** (`OKAY_OWN_ZAE`, slot 44 of the import table in `ModelImport.cpp`; added 2026-11-16 in the
"Assimp gap" batch, `docs/status/own_code_roadmap.md`, section "What still needs Assimp"). Assimp (when built) is the other reader of
`.zae` and the fallback when ours refuses a file. Nothing was flipped.

## What it is

A ZAE file is a ZIP archive that carries a COLLADA document and the files it uses (textures). The COLLADA 1.5 specification
describes it: the archive's root may hold `manifest.xml`, whose `<dae_root>` element names the root document; without a
manifest the document is the `.dae` in the archive.

| File | What |
|---|---|
| `engine/src/Render/ZaeReader.cpp` | `okay::ReadZaeBuffer` / `okay::ReadZaeFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/ZipLite.hpp` | The engine's own ZIP reader (stored and deflate, with the shared inflate; caps on archive size, entry count, entry and total sizes; CRC checked) |
| `engine/src/Render/ColladaReader.cpp` | Our COLLADA reader, which reads the document |
| `tests/test_gap_readers.cpp`, `tests/gap_fixtures.hpp` | Fixtures, truncation, mutation sweep, memory and time caps (`gap_readers`) |
| `tests/test_own_dispatch_gap.cpp` | `.zae` reaches slot 44, the switch, the shipped sample (`own_dispatch_gap`) |

## What it reads

- **The root document**: `<dae_root>` of `manifest.xml` (percent escapes such as `%20` decoded, `./` and `../` worked out inside
  the archive); when there is no manifest, or it names nothing that is in the archive, the `.dae` nearest the archive's top (then
  the first in name order), with a note. A name that climbs out of the archive is not looked up.
- **Everything in the document**, through the COLLADA reader (`docs/content/model_corpus.md`, Collada rows): meshes, nodes, materials,
  skins, animation; the `flatten` switch is passed on.
- **Textures inside the archive.** An image the document names that is in the archive is written next to the `.zae` as
  `<name>_zae_<file>` (for example `duck_zae_duck zaeCM.png`), once per image, and the material points at that file, so the
  renderer can load it. When a file of the entry's size is already there (an earlier read), it is used as it is: not extracted
  or written again. (Assimp's path writes embedded
  textures next to the model in the same way, `<name>_tex<n>.<ext>`.) The buffer version (`ReadZaeBuffer`) writes nothing:
  those textures are left empty, with a note. An image that is not in the archive is looked up next to the `.zae`.

## Not handled

Everything the COLLADA reader does not read (its notes are kept). Documents other than the root one (external references
between documents). ZIP64, encrypted entries and compression other than stored and deflate are refused by the ZIP reader.

## Limits

File 512 MiB; the ZIP reader's caps: 20,000 entries, 256 MiB per entry and 384 MiB per archive uncompressed, and the inflated
size must equal the size the directory declares (zip bombs are refused before they are inflated); `manifest.xml` at most 1,000
XML elements (more: it is ignored, with a note). Then the COLLADA reader's own caps.

## Tests and what was checked

- `gap_readers`: **synthetic** archives built in memory: a manifest that names `sub/doc%20one.dae` while a decoy `.dae` sits at
  the top (the decoy must not be read), no manifest (the top-most `.dae`), a manifest that names a missing file, a manifest that
  climbs out, not a ZIP, a ZIP without a `.dae`; the path version writes the image next to the file and a second read keeps it;
  a texture outside the archive. Then every 64th truncation, 1,500 mutations (20,000 in a longer run; `OKAY_GAP_FUZZ_ITERS`),
  an entry that claims 300 MB and a manifest of 200,000 elements, each under a counting `operator new` (under 64 MB) and a time
  limit. Clean under ASan + UBSan + float-cast-overflow (5,000 mutations).
- `own_dispatch_gap`: `.zae` through `ImportModelData`, `OKAY_OWN_ZAE=1` / `0`, off by default, the shipped sample.

### Real files

The 3 `.zae` files of Assimp's test folder (`duck.zae`, `duck_nomanifest.zae`, `human.zae`) all read. **Against Assimp v5.4.3
(`bench_model_readers --compare`)**: the two duck files equal Assimp on parts (1), triangles (4,212), vertices (2,399), nodes (4),
materials (1) and box; `human.zae` equals Assimp on parts (2), triangles (28,796), vertices (15,593), nodes, materials (2) and
box. Until 2026-11-16 it gave 0 materials to Assimp's 2: its two `<polylist>` elements have no `material` attribute, and the
COLLADA reader only bound a material through that symbol. COLLADA leaves an unnamed primitive's material to the application;
the reader now uses the instance's material when its `<bind_material>` binds exactly one (both instances here do), and none
when it binds several (`docs/content/model_import.md`, Collada). That change moved only this file's digest among the corpus files;
the manifest row is now `mat=2 tex=1` (the eye texture `textures/brown_eye.png` is inside the archive and is written next to the
`.zae` as `human_zae_brown_eye.png`). Each duck's texture is written next to the file and the material points at it. In the `model_corpus` manifest (reader `zae`). Speed and memory (`docs/content/own_readers_speed.md`, row `zae`): 0.68x Assimp's read time and 0.54x its largest peak heap over the
three files; no file slower or heavier.

Unverified: archives from other exporters (SketchUp, Blender add-ons), a manifest with a namespace prefix, a run on real Windows
(the sidecar files are written with a UTF-8 path through `std::filesystem`, as the FBX texture path does).

## Provenance (what this file was written from)

`ZaeReader.cpp` was written from scratch for this project from the description of the ZAE archive in the COLLADA 1.5
specification (Khronos) and from looking at the entry lists of the three sample archives as data (with Python's `zipfile`, a
dump only). No code, comment or test from Assimp or any other importer was read, copied or ported. Assimp was run only as a
black box on the sample files for the comparison above.
