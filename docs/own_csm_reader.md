# Our own CSM reader (`.csm`)

Status: **side by side, off by default** (`OKAY_OWN_CSM`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is still the only other reader of CSM files. Nothing was flipped. **Checked on one real file (against numbers taken from the file by a separate script, not against Assimp), never on real Windows.**

## What it is

CSM is the optical motion-capture text export of Character Studio (3ds Max) and of capture systems such as Vicon: a header of `$` lines, the **names of the markers**, and one line per frame with the 3D position of every marker. There is no mesh and no skeleton: **markers only, like BVH without the joints' rotations.** The reader gives the markers as empty nodes and the motion as one clip of position keys.

| File | What |
|---|---|
| `engine/src/Render/CsmReader.cpp` | `okay::ReadCsmBuffer` / `okay::ReadCsmFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/TextReaderUtil.hpp`, `ReaderCommon.hpp` | Shared pieces: line reading, encoding fix, strict locale-independent number parsing |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 30 (extension `.csm`) |
| `tests/test_csm_reader.cpp`, `tests/batche_fixtures.hpp` | Hand-computed checks on synthetic files (`csm_reader`) |
| `tests/test_batche_hostile.cpp` | Mutation fuzz, the worst allowed file (time and memory), the real file (`batche_hostile`) |
| `tests/test_own_dispatch_e.cpp` | The import seam routes `.csm` to this reader (`own_dispatch_e`) |

## What it reads

- `$Filename`, `$Date`, `$Time`: skipped. `$Actor name`: named in `notes`. `$Comments`: the following lines are skipped. `$FirstFrame n`, `$LastFrame n`: only used to warn when they do not match the number of frame lines. `$Rate r`: frames per second (a number above 0 and up to 100,000, else the file is refused; without a `$Rate`, 30 is assumed and noted).
- `$Order`: the marker names, separated by white space, on this line and the following ones until the next `$` keyword. Names must be unique and at most 64 characters (a clip names its nodes, so a duplicate would be ambiguous).
- `$Points`: one line per frame: the frame number, then `x y z` for every marker in `$Order` sequence. A line is used only if it holds **exactly** `1 + 3 * markers` plain decimal numbers (no `nan`, no `inf`, no hex); otherwise it is skipped and counted (missing markers are **not** interpolated). A frame number that does not increase over the last used one is skipped and counted. Key time = `(frame - first frame) / rate` seconds.
- Result: root node `CSM` (renamed `CSM_root` if a marker has that name), one child node per marker positioned at its first-frame position, and one clip `csm` with a position key per used frame and marker. **Units are kept as written** (the sample file is in millimetres; head markers are at about 1,600). **The root node turns Z-up into Y-up**, because optical capture data is Z-up (in the sample the head markers' z is about 1,650 and the feet's about 60). Both are assumptions from that file, said in `notes`.
- UTF-8 (with or without a byte order mark) and UTF-16 with a byte order mark; lines end with `\n`, `\r\n` or `\r`.

## Not handled

Marker visibility flags or "missing" codes (if the format has any: none in the sample; a line with such a token is skipped); per-marker rotations (the format has none); a second `$Order` or `$Points` section (refused); unknown `$` keywords (counted and ignored; a `$` keyword after `$Points` ends the frame lines).

## Limits

File 128 MiB; at most **1,024 markers** and **2,000,000 position keys** (markers times frames); names up to 64 characters. A frame line is parsed in place and nothing is stored until the whole line is good, so a line with a million tokens costs no allocation. The worst allowed file (1,000 markers, 2,000 frames, 25 MB) reads in about 0.3 s and holds about 31 MB at once (`batche_hostile` fails above 96 MB or 2 s).

## Tests and what was checked

- `csm_reader` (311 checks): a synthetic file with three markers over two `$Order` lines and three frame lines with a gap in the frame numbers (frames 1, 2, 4 at 50 per second: key times 0, 0.02, 0.06), with hand-computed positions per marker and key, node positions, root rotation, in `\n`, `\r\n` and `\r` forms; the first / last frame mismatch note present and absent; UTF-16 and UTF-8 with a byte order mark; no `$Rate`; skipped lines (too short, `x`, `inf`, too long, backwards); a marker named like the root; about 15 refusal cases (empty, `$Points` before `$Order`, no `$Points`, no frame line, duplicate names, a second section, bad rates including `nan` and `1e99`, a name of 65 characters, odd-length UTF-16, 1,025 markers, the key budget); exactly 1,024 markers still reads; every truncation; a line with a million tokens.
- `batche_hostile`: mutations of two forms of the fixture, the worst allowed file and one frame over the budget, the real file.
- **Mutation checks:** time `* rate` instead of `/ rate` (19 failures), key budget removed (1), the backwards-frame check removed (2).

### Real file

`ThomasFechten.csm` (a Vicon IQ capture, July 2008, "free for any purpose" by its own note) from the `test/models/CSM` folder of an Assimp 5.4.3 checkout (`docs/model_corpus.md`; not in this repository). Read as data only. 43 markers (`LFHD` ... `RMT5`), 2,439 frames at 120 per second; every frame line has exactly 130 numbers (counted by a separate script). The reader gives 44 nodes (root and 43 markers), one clip, 43 x 2,439 = 104,877 keys, no skipped line, the first key of the first marker `(26.327553, 157.341293, 1656.915527)` at time 0 and the last at 2438 / 120 s. **Not compared with Assimp's output.**

## Provenance (what this file was written from)

`CsmReader.cpp` was written from the byte-level and line-level analysis of the one sample file, and from my knowledge of the Character Studio `.csm` header keywords (`$Order`, `$Points`, `$Rate`, ...), recalled and then checked against that file; I found no written specification to follow line by line. No importer source (Assimp or any other project) was read, copied, ported or followed, and Assimp was not run on this file either.

Unverified: files from other capture software or from Character Studio itself (any marker "missing" convention), other frame-number styles, a run on real Windows.
