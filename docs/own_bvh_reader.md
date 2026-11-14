# Our own BVH reader (`.bvh`)

Status: **the default `.bvh` reader** (flipped on the owner's approval; rollback: `OKAY_OWN_BVH=0` in the environment or `-DOKAY_OWN_BVH=OFF`, which brings back Assimp's stick-figure mesh in a build with Assimp). Note the behaviour change for Assimp builds: with ours on, a `.bvh` gives a skeleton and a clip and no mesh. Until now `.bvh` only imported in a build with Assimp. Assimp builds a stick-figure mesh out of the skeleton; ours reads the skeleton and the motion and makes no mesh. Nothing was removed. This is the "BVH animation reader" step of `docs/own_code_roadmap.md`.

## What it is

Written from the public description of the Biovision hierarchy text format.

| File | What |
|---|---|
| `engine/src/Render/BvhReader.cpp` | `okay::ReadBvhBuffer` / `okay::ReadBvhFile` (declared in `engine/include/okay/Render/ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam (slot 5 of the backend switch); `BuildSceneFromModel` also accepts a model with nodes and clips but no mesh |
| `scripts/gen_bvh_fixtures.py` | Writes `tests/data/models/walk.bvh`, `walk_crlf.bvh`, `two_roots.bvh` |
| `tests/test_bvh_reader.cpp` | Hand-computed checks and the seam (`bvh_reader`) |
| `tests/test_3ds_bvh_hostile.cpp` | Hostile input and mutation fuzz, standalone, under sanitizers (`3ds_bvh_hostile`) |

## Turning it on

Same rules as the 3DS reader: `-DOKAY_OWN_BVH=ON`, environment variable `OKAY_OWN_BVH=1` / `0`, or `SetImportBackend(".bvh", ImportBackend::Own)` (the run-time choice wins). Off by default. With Assimp built, Assimp stays the default `.bvh` reader and ours is the choice when asked for.

## What it reads

- **Hierarchy**: `ROOT`, `JOINT` (names may contain spaces; a `{` may follow the name on the same line), `OFFSET`, `CHANNELS` (up to 24 per joint; `Xposition`, `Yposition`, `Zposition`, `Xrotation`, `Yrotation`, `Zrotation`, any case), `End Site`. Line breaks may be `\n`, `\r\n` or `\r`. Every joint becomes a node (`position` = `OFFSET`, no rotation, scale 1). End sites are not nodes (they are counted in the notes). A file with several `ROOT` joints gets a synthetic parent node `Skeleton`.
- **Motion**: `Frames:`, `Frame Time:` and one row of values per frame. One clip, `motion`, with a channel per joint that has channels: a position key per frame when the joint has position channels (`OFFSET` plus the channel values), a rotation key per frame when it has rotation channels. Times are `frame * frame time`.
- **Rotation order**: channels are composed in the order listed, the first listed outermost (`Zrotation Xrotation Yrotation` is `Rz * Rx * Ry` applied to column vectors), angles in degrees, positive angles counter-clockwise about each axis. This was checked against another importer's output on real files (below).
- **Scene import** (`ImportModelScene`) builds a hierarchy of empty objects with a `ModelAnimator` holding the clip, no mesh. The merged-mesh path (`ImportModel`) has nothing to merge and reports failure.

Units are kept as written (BVH has none); no rescaling and no axis change is done: the usual Y-up right-handed convention of the common exporters is assumed.

## Limits

| Limit | Value |
|---|---|
| File size | 512 MiB |
| Joints | 10,000 |
| Joint nesting depth | 256 |
| Frames | 1,000,000 |
| Samples (frames x channels) | 60,000,000, and never more than the text can hold (every value is at least two bytes), checked before allocating |
| Output keys | 30,000,000 |
| Values (offsets, channels) | finite, within +-1e15 |
| Frame time | greater than 0 and at most 1e6 s |

No recursion (joints use an explicit stack). A bad channel count or name, a channel line that disagrees with its count, a second `CHANNELS` line, an unclosed joint, a missing `MOTION`, too few values, `nan` / `inf` / hex numbers, text glued to a number all fail the read with a one-line message.

## Not handled

- No mesh, no skin: a BVH file has none. Another importer's stick figure is not reproduced.
- No resampling: one key per frame per channel. The engine's clip optimiser runs afterwards (`NormalizeImportedClips`).
- Rotation channels are applied as given; a file whose angles are clockwise, or whose axes differ from the common convention, plays mirrored.
- `End Site` offsets, `Frame Time` given with a comma decimal and files with several `MOTION` blocks are not read.
- A joint with more than three rotation channels or the same rotation channel twice is composed as listed without complaint.
- Not benchmarked beyond: a 3.4 MB file with 4,511 frames reads in about half a second.

## Tests and what was checked

- `bvh_reader`: both line-ending variants, joint names with spaces, parents, offsets, channel order (`Zrotation` before `Xrotation`), rotation-only and position-only joints, two roots, a brace on the name line, a file with no frames, `\r`-only breaks, eleven kinds of malformed input, the seam and the scene import.
- `3ds_bvh_hostile`: every truncation of every fixture, frame counts that claim a million rows, 100,000-deep and 20,000-wide skeletons, over-long names, non-finite and overflowing numbers, NUL bytes, then a deterministic mutation fuzz, built with `-fsanitize=address,undefined,float-cast-overflow`: clean, including 150,000 mutations.
- Real files: three motion capture files from an outside corpus (`docs/model_corpus.md`), 2,752 to 4,511 frames, 20 to 30 joints, channel orders differing between files. Against Assimp built from source, on two of the three files (01_01 and Boxing_Toes) the root's position and rotation keys, other joints' rotation keys and the duration agree to four decimals at the first and last frame; the third file was checked by frame count times frame time (22.925 s, 37.583 s, 30.680 s for the three).

## Before it can become the default

Run on motion capture from more than one source; Windows run; owner approval. There is nothing else for a BVH importer in the roadmap to replace: removing Assimp drops its stick-figure mesh for `.bvh`, which is announced with the other dropped formats.
