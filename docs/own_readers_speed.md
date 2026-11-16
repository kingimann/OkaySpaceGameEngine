# Own model readers: speed and memory against Assimp

The owner's rule for switching a format from Assimp to our own reader: "we only switch when it's faster and up to par". That
means, on the same real files: **at least as fast, no more peak memory, and at least as good**. This page measures the first
two for every reader that sits behind the `ImportModelData` seam, and lists where ours and Assimp disagree. FBX has its own
page (`docs/own_fbx_reader.md`, its other side is ufbx); glTF does not go through this seam.

No default was changed. Every reader here is still off by default (`OKAY_OWN_*`), except OBJ, STL and PLY, which were ours
already.

## How it is measured

`tests/bench_model_readers.cpp` (built with the tests, never run by ctest). Configure with `-DOKAY_USE_ASSIMP=ON` so the
Assimp side exists, then:

```
OKAY_MODEL_CORPUS=<assimp v5.4.3 checkout>/test build/tests/bench_model_readers            # every file, table at the end
build/tests/bench_model_readers --format dae <dir>                                          # one reader
build/tests/bench_model_readers --digest  | --import-digest                                 # output hashes (see below)
build/tests/bench_model_readers --compare                                                   # counts, names, box against Assimp
build/tests/bench_model_readers --only own-read <file>                                      # one call, for a profiler
```

For every file of a format we have a reader for (the 355 files of the Assimp v5.4.3 test folders, `docs/model_corpus.md`) it
measures four calls:

- **read, ours**: `ImportModelData(path, Own)`: the file read and our reader's `ImportedModel`.
- **read, Assimp**: `ImportModelData(path, Assimp)`: Assimp's `ReadFile` with the post-process steps the engine asks for
  (triangulate, smooth normals, join identical vertices, flip UVs, limit bone weights), then the conversion to the same
  `ImportedModel`.
- **import, ours / Assimp**: `ImportModelScene` with every format switched to that backend: the whole import (file, reader,
  GameObjects, MeshRenderer / SkinnedMesh, clips).

Time is the median over at least 7 runs (small files run more often, until 50 ms have passed). Peak heap is the most bytes
held at once during one call above what was held before it, counted by replacing `malloc` / `free` / `realloc` / `calloc` /
`memalign` in the program, so Assimp's allocations and ours are counted the same way (glibc only). A reader is listed
"met" for time when its summed read time over the files both read is not above Assimp's, and "met" for memory when its
largest peak and its summed peaks are both not above Assimp's.

Assimp crashed on two corpus files (`RAW/WithColor.raw`, `RAW/WithTexture.raw`: a node's child list holds null pointers, in
Assimp 5.4.3's RAW importer; since the third round the seam never hands such a file to Assimp, see below) and dies (killed by a signal) on `invalid/OutOfMemory.off`, a file that claims 353 billion vertices. The bench therefore reads every
file with Assimp in a child process first and reports such a file as "crash" instead of dying. The shipped Windows build
reads `.raw` through Assimp, so importing those two files there crashed (not tried on Windows); now our RAW reader reads
them (`docs/own_raw_reader.md`, "Files Assimp is not given").

The machine was a shared 4-core Linux container with other builds running, so a small file's time can move by 20% from run
to run; the instruction counts quoted below (callgrind) do not move.

## Results (Linux, GCC 13, Release)

| Reader | files | both read | read time ours / Assimp (before -> now) | time | largest peak heap ours / Assimp MB (before -> now) | memory | import time ours / Assimp | import peak ours / Assimp | files slower / heavier |
|---|---:|---:|---|---|---|---|---|---|---|
| 3ds | 16 | 15 | 0.22x -> 0.21x (8.2 / 38.7 ms) | met | 4.91 / 13.32 -> 4.91 / 13.32 | met | 0.23x | 4.91 / 13.32 MB | 0 / 0 |
| 3mf | 2 | 2 | 0.67x -> 0.86x (0.1 / 0.1 ms) | met | 0.02 / 0.11 -> 0.02 / 0.11 | met | 0.84x | 0.02 / 0.11 MB | 1 / 0 |
| ac | 11 | 7 | 0.57x -> 0.40x (183.5 / 457.9 ms) | met | 57.93 / 73.92 -> 57.01 / 73.92 | met | 0.39x | 57.01 / 73.92 MB | 4 / 5 |
| amf | 11 | 11 | 0.86x -> 0.84x (0.4 / 0.5 ms) | met | 0.04 / 0.13 -> 0.04 / 0.13 | met | 0.80x | 0.06 / 0.13 MB | 2 / 0 |
| ase | 17 | 16 | 1.05x -> 0.72x (5.5 / 7.6 ms) | met | 1.99 / 2.40 -> 1.99 / 2.40 | met | 0.69x | 1.99 / 2.40 MB | 0 / 0 |
| b3d | 3 | 3 | 0.35x -> 0.35x (1.4 / 4.1 ms) | met | 1.02 / 1.75 -> 1.02 / 1.75 | met | 0.75x | 1.52 / 1.75 MB | 0 / 0 |
| blend | 28 | 27 | 0.12x -> 0.09x (55.3 / 591.5 ms) | met | 4.00 / 84.07 -> 4.00 / 84.07 | met | 0.10x | 4.00 / 84.07 MB | 1 / 11 |
| bvh | 3 | 3 | 0.40x -> 0.39x (27.8 / 72.1 ms) | met | 7.66 / 14.53 -> 7.66 / 14.53 | met | 0.94x | 8.89 / 14.53 MB | 0 / 0 |
| cob | 8 | 6 | 0.92x -> 0.92x (6.2 / 6.8 ms) | met | 1.19 / 1.31 -> 1.19 / 1.31 | met | 0.90x | 1.19 / 1.31 MB | 2 / 1 |
| csm | 1 | 1 | 1.45x -> 0.90x (5.8 / 6.4 ms) | met | 6.22 / 6.73 -> 5.11 / 6.73 | met | 0.96x | 5.88 / 7.21 MB | 0 / 0 |
| dae | 26 | 25 | 0.79x -> 0.73x (25.4 / 34.9 ms) | met | 3.26 / 3.45 -> 3.26 / 3.45 | met | 0.83x | 3.26 / 3.45 MB | 2 / 0 |
| dxf | 5 | 5 | 0.63x -> 0.54x (4.2 / 7.8 ms) | met | 1.39 / 1.60 -> 1.45 / 1.60 | met | 0.58x | 1.45 / 1.60 MB | 0 / 1 |
| hmp | 2 | 2 | 0.07x -> 0.06x (0.1 / 1.7 ms) | met | 0.64 / 1.58 -> 0.64 / 1.58 | met | 0.07x | 0.64 / 1.58 MB | 0 / 0 |
| iqm | 1 | 1 | 0.80x -> 0.84x (0.6 / 0.7 ms) | met | 0.68 / 0.69 -> 0.68 / 0.69 | met | 5.17x | 1.46 / 0.69 MB | 0 / 0 |
| irr | 21 | 21 | 0.35x -> 0.38x (37.0 / 97.5 ms) | met | 1.73 / 1.91 -> 1.73 / 1.91 | met | 0.37x | 1.84 / 2.40 MB | 0 / 0 |
| irrmesh | 5 | 5 | 0.77x -> 0.71x (3.6 / 5.1 ms) | met | 0.74 / 0.76 -> 0.72 / 0.76 | met | 0.66x | 0.72 / 0.76 MB | 0 / 0 |
| lwo | 54 | 53 | 0.89x -> 0.56x (19.2 / 34.3 ms) | met | 1.66 / 1.60 -> 1.01 / 1.60 | met | 0.54x | 1.01 / 1.60 MB | 0 / 0 |
| lws | 15 | 15 | 0.54x -> 0.44x (12.9 / 29.5 ms) | met | 0.02 / 0.11 -> 1.78 / 3.67 | met | 0.62x | 1.78 / 3.67 MB | 0 / 0 |
| md2 | 5 | 5 | 0.56x -> 0.43x (0.6 / 1.3 ms) | met | 0.44 / 0.43 -> 0.34 / 0.43 | met | 0.43x | 0.34 / 0.43 MB | 0 / 0 |
| md3 | 3 | 3 | 0.39x -> 0.24x (0.2 / 0.7 ms) | met | 0.12 / 0.30 -> 0.12 / 0.30 | met | 0.25x | 0.12 / 0.30 MB | 0 / 0 |
| md5 | 5 | 4 | 0.54x -> 0.49x (5.6 / 11.5 ms) | met | 0.59 / 1.26 -> 0.59 / 1.26 | met | 0.77x | 1.25 / 1.26 MB | 0 / 0 |
| mdc | 1 | 1 | 0.54x -> 0.45x (0.4 / 1.0 ms) | met | 1.05 / 1.30 -> 1.05 / 1.30 | met | 0.49x | 1.05 / 1.30 MB | 0 / 0 |
| ms3d | 4 | 4 | 0.69x -> 0.74x (2.4 / 3.2 ms) | met | 1.09 / 1.56 -> 1.09 / 1.56 | met | 0.74x | 1.09 / 1.56 MB | 0 / 0 |
| nff | 13 | 12 | 1.19x -> 0.77x (72.1 / 93.4 ms) | met | 6.76 / 13.09 -> 6.27 / 13.09 | met | 0.67x | 6.27 / 13.09 MB | 0 / 2 |
| obj | 28 | 25 | 0.12x -> 0.12x (6.4 / 53.4 ms) | met | 0.85 / 17.28 -> 0.85 / 17.28 | met | 0.13x | 0.85 / 17.28 MB | 0 / 0 |
| off | 5 | 2 | 0.93x -> 0.95x (1.4 / 1.5 ms) | met | 0.55 / 0.97 -> 0.55 / 0.97 | met | 0.86x | 0.55 / 0.97 MB | 0 / 0; Assimp crashes on 1 |
| ogre | 4 | 4 | 0.72x -> 0.74x -> 0.63x (13.2 / 20.9 ms) | met | 3.51 / 4.20 -> 3.49 / 3.38 -> 2.78 / 4.20 | met (every file below Assimp, third round) | 0.58x | 2.78 / 4.20 MB | 0 / 0 |
| ply | 12 | 9 | 0.37x -> 0.28x (20.6 / 73.2 ms) | met | 5.12 / 6.62 -> 5.12 / 6.62 | met | 0.27x | 5.51 / 6.62 MB | 0 / 0 |
| pmx | 1 | 1 | 0.21x -> 0.10x (1.3 / 13.2 ms) | met | 1.50 / 3.39 -> 1.50 / 3.39 | met | 0.23x | 1.51 / 3.39 MB | 0 / 0 |
| q3d | 4 | 4 | 0.34x (6.7 / 19.8 ms) | met | 1.28 / 4.10 | met | 0.83x | 1.28 / 4.10 MB | 0 / 0 |
| raw | 4 | 1 | 1.04x -> 0.70x (2.0 / 2.8 ms) | met | 1.06 / 1.43 -> 1.00 / 1.43 | met | 0.85x | 1.00 / 1.43 MB | 0 / 0; Assimp crashed on 2 (not given to it since the third round) |
| smd | 4 | 3 | 1.20x -> 0.66x (3.2 / 4.8 ms) | met | 2.64 / 3.30 -> 2.66 / 3.30 | met | 0.70x | 2.66 / 3.30 MB | 0 / 0 |
| stl | 8 | 8 | 0.64x -> 0.68x (5.2 / 7.6 ms) | met | 1.29 / 2.10 -> 1.30 / 2.10 | met | 0.48x | 1.30 / 2.10 MB | 2 / 0 |
| ter | 2 | 2 | 0.01x -> 0.01x (6.3 / 733.4 ms) | met | 16.54 / 157.51 -> 16.54 / 157.51 | met | 0.01x | 28.07 / 157.51 MB | 0 / 0 |
| x | 12 | 11 | 0.34x -> 0.28x (12.6 / 45.4 ms) | met | 2.41 / 3.23 -> 2.41 / 3.23 | met | 0.35x | 2.41 / 3.23 MB | 0 / 0 |
| x3d | 4 | 3 | 1.11x -> 0.77x (2.4 / 3.1 ms) | met | 0.32 / 0.67 -> 0.32 / 0.67 | met | 0.81x | 0.43 / 0.67 MB | 1 / 1 |
| xgl | 7 | 7 | 0.04x -> 0.03x (81.3 / 2545.3 ms) | met | 12.61 / 15.15 -> 12.60 / 15.15 | met | 0.03x | 12.60 / 15.15 MB | 0 / 0 |

`files slower / heavier` counts the files where ours took longer, or held more heap, than Assimp. "before" is the head of
`claude2/own-collada-speed` (the first round, below); "now" is the head of `claude2/own-readers-speed-2`. LWS "before" had 14
files both read (`QuickDraw v.2.2.lws` was refused); Q3D had none.

### Where it is not met, and why

All 37 readers were measured (Q3D for the first time). **36 meet both time and memory in this run; OGRE misses memory**
(fixed in the third round, below: all 37 meet both now):

- **OGRE, memory (not changed by this work).** Ours is heavier than Assimp on 3 of the 3 files both read in this run
  (`TheThing/Mesh.mesh.xml` 3.49 / 3.38 MB, `razor` 1.17 / 1.02, `fish` 1.79 / 1.73). Whether the reader "meets" memory
  depends on which files Assimp reads: **Assimp's OGRE importer fails on `ninja.mesh.xml` in some runs and on `fish.mesh.xml` in
  others** (same build, same files); when it reads `ninja` (4.20 MB) the largest peak is Assimp's and the line says met (as in
  the first round and in a `--format ogre` run after this one, 3.49 / 4.20). The per-file heap is the honest number: ours is
  1.04x to 1.14x on three of four files. Not worked on here.

What was not met before this round, and where it is now (instruction counts are callgrind's, whole program):

- **ASE 1.05x -> 0.72x** (`MotionCaptureROM.ase` 52.5 -> 36.8 M, Assimp 39.3).
- **CSM 1.45x -> 0.90x** (118.4 -> 76.6 M, Assimp 82.3); peak 6.22 -> 5.11 MB.
- **NFF 1.19x -> 0.77x** (`positionTest` 303.6 -> 137.0 M, Assimp 288.1; `HOME4` 93.3 -> 77.3 M, Assimp 95.3; `teapot` 39.2 -> 24.2 M,
  Assimp 32.8). No NFF file is slower now.
- **SMD 1.20x -> 0.66x** (`WusonSMD.smd` 50.5 -> 33.3 M, Assimp 39.3).
- **RAW 1.04x -> 0.70x** (`Wuson.raw` 28.3 -> 21.4 M, Assimp 27.1; one file both read).
- **X3D 1.11x -> 0.77x**: the reader was not changed for speed; the shared weld made `ComputerKeyboard.x3d` faster. That file is
  still the one slower X3D file, and it is not like for like: ours builds the 98 keys the file instances, Assimp builds 2 (see
  "Accuracy").
- **LWO memory 1.66 / 1.60 -> 1.01 / 1.60 MB** (and time 0.89x -> 0.56x).
- **MD2 memory 0.44 / 0.43 -> 0.34 / 0.43 MB.**

Small changes elsewhere from the shared weld table: DXF's largest peak 1.39 -> 1.45 MB (Assimp 1.60), the others equal or lower.
Timings move by up to 20% from run to run on this machine (3MF and STL read slower in this run than in the last one; neither was
changed).

The import columns (the whole scene import) follow the read columns, except **IQM**: the import with ours takes 5.2x the
time and 2.1x the heap of Assimp's because ours imports the file's animation clip and Assimp does not (a difference in what
is read, not in speed).

## What was changed (output unchanged)

Every step was checked the same way before it was committed: `--digest` (a hash of every field of our `ImportedModel`, bit
for bit) and `--import-digest` (a hash of the whole imported scene as `SceneSerializer` writes it, object ids left out) are
identical to the hashes of the code before the work on all 355 files, and `model_corpus` still gives 3871 checks, 0
failures (380 files read, 34 recorded as failing).

| Step | What it does | Effect on the corpus |
|---|---|---|
| Collada XML: bounded `&` search | The XML reader looked for `&` with `std::string::find` from every piece of text to the end of the file: quadratic on files without entities. Now `memchr` inside the piece. | `teapots.DAE` 32.6 -> 6.7 ms |
| Collada XML: text as pieces of the file | An element's text is a list of pieces of the file, joined only when asked for; big number arrays are read in place, never copied. Tag and attribute names are cut without temporary strings; the element list is reserved from a count of start tags. | Collada peak 4.46 -> 3.26 MB (Assimp 3.45) |
| Collada numbers | One-pass scanner with a character table; `std::from_chars` for decimal tokens (also the decimal-comma ones); arrays reserve their announced `count` (capped by the text length); `<p>` lists parse straight into the corner list, growing geometrically. | |
| Collada sources | A mesh's primitive sets (one per material) name the same sources, which were parsed again for every set; now once per `<mesh>` (the decimal-comma count is still added per use, so the notes are the same). | Collada 151.7 -> 26.0 ms over 25 files (Assimp 32.7) |
| File read: reserve the size | The shared file readers (`ReadAllBounded`, `ReadFileCapped`, `ReadAllBytes`) and the Blender, BVH, 3DS and Collada readers grew the buffer by doubling (up to twice the file at once); they reserve the file's size first. | every reader |
| Text readers: hand the bytes over | AC3D, Irrlicht mesh and scene, LWS, NFF, OGRE, ASE, SMD and MD5 copied the whole text once more before parsing; their `Read*File` now move the bytes in (`Read*Buffer` still copies). COB, LWO and MD5 free the file once it is parsed. | ASE 3.99 -> 1.99 MB, SMD 3.64 -> 2.64, MD5 1.61 -> 0.59, COB 1.69 -> 1.19 |
| Numbers: exact fast path | `detail::FastDecimal` and `detail::DecimalWithoutStrtod` (see below) in `txt::ToDouble`, `rc::ParseNumber` (AMF, CSM, DXF, Irrlicht, LWS, OGRE, RAW, 3MF, X3D, X, XGL) and the BVH motion values. | CSM 482 -> 138 M instructions; BVH 179 -> 101 M (Assimp 179) |
| Tokens as views | `txt::Lines::Next` and `txt::SplitInto` have forms that return views into the text (no copy per line) and find line ends with `memchr`; `SplitInto` refills one vector; `ToDouble` / `ToInt` take views. Used by ASE, SMD, CSM, OFF, COB and NFF. | ASE 93 -> 54 M, SMD 95 -> 51 M, NFF 113 -> 93 M instructions |
| XML reader (`XmlLite`) | Reserves the element array from a count of start tags and gives each element its attributes at their exact size. | OGRE 5.43 -> 3.51 MB, XGL/ZGL 24.0 -> 12.6 MB |
| LWO polygons | Each polygon list is reserved from a count. | |

### Second round (branch `claude2/own-readers-speed-2`, 2026-11-16)

The same check after every step: `--digest` and `--import-digest` identical on every corpus file except the five files whose
reading was fixed on purpose (four Q3D files, `QuickDraw v.2.2.lws`; see "Accuracy"). Instruction counts are callgrind's for
one `--only own-read` run of the whole program (about 3.5 M of each is the program's start, the same for both sides).

| Step | What it does | Effect on the corpus |
|---|---|---|
| ASE: block codes | Each open block gets a code once, when it opens; a line is dispatched on the code instead of a chain of block-name compares. | |
| Number lines in one pass | `detail::ScanDecimal` (`NumParse.hpp`) reads the FastDecimal case of the token at hand while it finds the token's end, four digits at a time (`FourDigits`, little endian); it gives FastDecimal's value or declines, and then the token is read the old way (`ToDouble` / `ParseNumber`), so every value is the same. `txt::LineScan` reads a line's tokens straight from the text. Used for ASE vertex, normal, texture, face and animation-sample lines, SMD vertex, material and pose lines, CSM frame lines and RAW triangle lines, each only when the line has the usual layout (anything else goes the general way, which reports errors as before). | ASE 52.5 -> 36.8 M (Assimp 39.3), SMD 50.5 -> 33.3 M (39.3), CSM 118.4 -> 76.6 M (82.3), RAW 28.3 -> 21.4 M (27.1) |
| One weld table (`WeldMap.hpp`) | The keys side by side in one array and a table of key numbers (open addressing) instead of `unordered_map`s with a node per vertex (keyed by a `std::string` in ASE and SMD). Used by `rc::BuildMesh` (AMF, X3D, DXF, LWO, NFF, AC3D, Irrlicht, OGRE, XGL, Q3D, RAW, ...), ASE, SMD, RAW and the LWO point map. Only the lookup changed: the vertices and their order are the same. | NFF `positionTest` 303.6 -> 137.0 M (Assimp 288.1) with the next step |
| NFF solids | A sphere's triangles go straight into the soup (a triangle triangulates to itself) instead of three vectors per triangle; Sense8 polygons reuse their buffers and the group of the polygon before. | `teapot.nff` 39.2 -> 24.2 M (Assimp 32.8) |
| CSM keys | The key arrays reserve the frames `$FirstFrame` / `$LastFrame` announce (capped by the text left and the key budget). | CSM peak 6.22 -> 5.11 MB (Assimp 6.73) |
| LWO memory | Once a layer's polygons are in the per-surface soups, the layer's points, polygons and UV maps are freed, and each soup is freed when its mesh is built. | `QuickDraw--Chasis.lwo` peak 1.66 -> 1.01 MB (Assimp 1.60) |
| MD2 memory | `ReadMd2File` frees the file once its tables are read, before the mesh is built (`ReadMd2Buffer` does not own its buffer and is unchanged). | `faerie.md2` peak 0.43 -> 0.34 MB (Assimp 0.42) |
| RAW weld | The weld table of a group is freed before its mesh is built. | |

### Third round (branch `claude2/own-readers-speed-3`, 2026-11-16)

Same check: `--digest` and `--import-digest` identical on every corpus file except `LWS/move_x_oldformat_56.lws` (read on
purpose: its motion, see below).

| Step | What it does | Effect |
|---|---|---|
| OGRE memory | `XmlLite` gets a `keepText` switch (default on, the other XML readers unchanged); OGRE turns it off, so the white space between elements is checked but not stored, and the reader frees the file's bytes once the document is parsed. | Peak ours / Assimp MB: `fish` 1.79 -> 1.35 / 1.73, `ninja` 2.47 -> 1.89 / 4.20, `razor` 1.17 -> 0.91 / 1.02, `TheThing` 3.49 -> 2.78 / 3.37: no OGRE file is heavier than Assimp now (`docs/own_ogre_reader.md`) |
| RAW files Assimp breaks on | `RawFileUnsafeForAssimp`: a file with two or more groups of triangles goes to our RAW reader even when `.raw` is set to Assimp; an explicit Assimp request is refused with a message. The Assimp scene walk skips null children. | `own_dispatch_e2` no longer crashes; `WithColor.raw` and `WithTexture.raw` import |
| LWS older motion layout | Scene versions 1 and 2 keep motion as a channel count, then per channel a key count and lines of value, time and span, with angles in degrees; ours read none of it. | `move_x_oldformat_56.lws`: clip 0 -> 1 and box as Assimp's; it now gives exactly what `move_x.lws` gives (`docs/own_lws_reader.md`) |
| Number scanner switch | `OKAY_NUMPARSE_NO_FOUR_DIGITS` forces the digit-by-digit path (what a big-endian machine runs); `reader_numparse` and `reader_numparse_portable` run the same checks both ways. | Both: 3,865,709 corpus tokens and 340,000 others, 0 differ from FastDecimal |

`move_x.lws` (an older note said Assimp makes 36 triangles there, ours 12): not reproduced in 12 runs, both make 12, and 12 is
right by the format (one layer of six quads).

The two number helpers are written from the arithmetic, not from any library: `detail::FastDecimal` is the exact fast case
of decimal conversion (W. D. Clinger, "How to read floating point numbers accurately", 1990: digits that fit in 53 bits and a
power of ten of at most 22 are both exact doubles, so one multiplication or division rounds correctly and gives the value
`strtod` gives); anything outside it goes the old way. A randomized check of 3 million tokens (2.5 million answered by the
fast case) found no difference from `strtod`. `std::from_chars` is used only where the C library also reads '.' as the
decimal point (it is correctly rounded like `strtod`); under another locale the old code path runs unchanged.

## Accuracy: where our reader and Assimp disagree

`--compare` reads each file with both and compares part, triangle, vertex, node, material, clip and bone counts, the
bounding box in world space (1/1000 of the diagonal) and the node names. Of the 355 files, 63 agree on everything (62 after the second round, which added `WusonOrange.q3o`; the third added `move_x_oldformat_56.lws`), 14 are
refused by both, and the rest differ in at least one number. Most differences are by design and documented in each reader's
page: vertex counts (Assimp joins identical vertices, we weld per corner key), node counts (Assimp adds or merges nodes), material counts
(Assimp adds a default material), and boxes for readers that turn Z-up files to Y-up at the root when Assimp does not.

Differences that are not by design, or that are worth a second look (counts are ours / Assimp's):

- **Q3D: fixed (2026-11-16).** Ours refused all four corpus files (`E-AT-AT.q3o`, `earth.q3o`: "bad vertex count";
  `WusonOrange.q3o` / `.q3s`: "an unexpected or repeated chunk type 158"). Cause, from the bytes of the files: a mesh always
  holds a texture coordinate count (the reader left it out when the header names no texture), a material ends with a
  texture index (not read), and the `.q3s` file says `quick3Do` but has a scene chunk. All four read now, with Assimp's
  triangle and material counts and boxes (the `.q3s` box differs: Assimp applies the scene chunk's placement, ours does not
  read it); one part per mesh and material, as before. Q3D is in the `model_corpus` manifest now (`docs/own_q3d_reader.md`).
- **LWS: fixed (2026-11-16).** `QuickDraw v.2.2.lws` was refused: its objects are `../LWO/LWO2/LWSReferences/*.lwo` and the
  scene-folder rule refused every `..`. LightWave objects with exactly one leading `../` are now looked up in the folder
  above the scene (LightWave's content directory), and `PivotPosition` is applied, which the file needs (without it the
  parts sat twice as far out). It reads with Assimp's box and 22,144 / 22,073 triangles (the LWO concave-polygon
  differences below). In the manifest now (`docs/own_lws_reader.md`).
- **X3D `ComputerKeyboard.x3d`: ours is right.** 98 keys are `USE`s of one `DEF`ed key (some inside a group that is itself
  used three times); X3D draws a `USE` wherever it is used, so 100 shapes and 2,020 triangles. Assimp builds 4 meshes and 100
  triangles: it drops 96 keys (`docs/own_x3d_reader.md`).
- **LWS `move_x_oldformat_56.lws`: fixed (third round).** Ours read no motion from the version 2 layout; it does now and agrees
  with Assimp (clip and box).
- **Assimp's OGRE importer is not repeatable**: in one run it fails on `ninja.mesh.xml`, in another on `fish.mesh.xml`.
- **RAW: Assimp crashed on two files ours reads** (`WithColor.raw`, `WithTexture.raw`); such files are now read by ours (see above).
- **OGRE `fish.mesh.xml` (in some runs), COB `dwarf_ascii.cob` and
  `molecule_ascii.cob`, NFF `cone.nff`, AC3D `SphereWithLight_UTF16LE.ac` and Blender `BlenderDefault_262.blend`** are read
  by ours and refused by Assimp. PLY `ant-half.ply` (truncated), OFF `invalid.off`, X3D `IndexedLineSet.x3d` and the three
  line-only AC3D files are refused by ours and read by Assimp (as meshes without triangles); all known and documented.
- **Blender: modifiers.** Assimp applies subdivision and mirror modifiers, ours does not (documented): `SuzanneSubdiv_252`
  968 / 251,904 triangles, `yxa_1` 1,808 / 115,712, `HUMAN` 267 / 8,560, `MirroredCube_252` 24 / 36, `CubeHierarchy_248`
  122 / 86.
- **Skeleton-only files** (BVH, CSM, MD5 `.md5anim`, Collada `cameras.dae` and `lights.dae`): Assimp adds a stand-in mesh
  (164, 352 and 200 triangles), ours adds none (deliberate).
- **Triangle counts that differ by a few per cent**: concave polygons (Collada, OBJ and LWO `ConcavePolygon` 64 / 58,
  `concave_self_intersecting.lwo` 12 / 9, `nonplanar_polygon.lwo` 15 / 11), the spider model in COB, Irrlicht mesh and MDC
  (1,312 / 1,368: zero-area triangles ours drops), `PinkEggFromLW.dxf` 528 / 576 (the same), `horse.md2` 683 / 690, three
  LWO files of the LWS references (+24 each) and two LWO vertex-colour files (-13 to -22), `box_longline.obj` 944 / 634.
- **More geometry in ours**: AMF `test7`, `test8`, `test_with_mat` (Assimp reads one volume of several),
  `ComputerKeyboard.x3d` (instanced keys, above), `ChevyTahoe.x3d` 1,298 / 1,234, the Irrlicht scenes with spheres and animators
  (480 / 1,280 and 3,140 / 4,740: sphere tessellation), `HelloX3dTrademark.x3d` 528 / 1,280 (sphere).
- **ASE UTF-16**: Assimp reads the two UTF-16 `ThreeCubesGreen` files as one part of 8 triangles; ours reads the 3 cubes
  (36 triangles), like the UTF-8 file.
- Bounding boxes differ on 68 files, almost all where one side turns Z-up to Y-up at the root and the other does not, or
  where the triangle counts above differ; clip counts differ on 16 (17 before the third round): Assimp makes clips that ours does not on 10 Irrlicht
  scenes (animators), the 3 SMD files (a one-frame pose), `ninja.mesh.xml` (20, from the skeleton file ours does not read),
  `CameraRollAnim.3ds` (and `move_x_oldformat_56.lws` before the third round); ours makes one where Assimp makes none on `mrfixit.iqm`. Bone counts
  differ on 28 files.

The full per-file list: run `bench_model_readers --compare` (one tab-separated line per file, `DIFF` and the fields).

## Tests run (Linux, 2026-11-16)

Third round (`claude2/own-readers-speed-3`):

- `ctest -R "model|import|collada|reader|obj|gltf|mesh|anim"` with `OKAY_MODEL_CORPUS` set, Release, `OKAY_USE_ASSIMP=ON`:
  **106 of 106 passed** (the two new `reader_numparse` tests included); `model_corpus`: 4,080 checks, 0 failures. Also
  passed (18): `own_dispatch`, `own_dispatch_c`, `_d`, `_e`, `_e3`, `_e5`, `own_pmx_lws_default`, `ase_hostile`,
  `smd_hostile`, `batchb_hostile` to `batche_hostile`, `batchd_budget`, `batche2_hostile`, `pmxlws_hostile`; and
  **`own_dispatch_e2`, which crashed before**: 58 checks, 0 failures (52 without the corpus).
- **Engine-linked tests under ASan + UBSan + float-cast-overflow** for the first time on this work: a whole-engine build at
  `-O1 -g0`, `-fno-sanitize-recover=all`, Assimp off (our readers only). `model_readers_fuzz` 1,437 checks, `model_corpus`
  (corpus set) 4,080 checks, `reader_numparse` and `reader_numparse_portable` (corpus set): all passed, **no sanitizer report,
  so nothing to fix**. The standalone reader tests of the second round (11, rebuilt by hand with the same flags) passed again
  with no report.
- `--digest` / `--import-digest`: identical on every corpus file except `move_x_oldformat_56.lws` (the intentional LWS fix).

Second round (`claude2/own-readers-speed-2`):

- `ctest -R "model|import|collada|reader|obj|gltf|mesh|anim"` with `OKAY_MODEL_CORPUS` set, Release, `OKAY_USE_ASSIMP=ON`:
  104 of 104 passed; `model_corpus`: 4,080 checks, 0 failures (433 rows: 399 read, 34 recorded failing). Also passed:
  `batche2_hostile`, `batche2_budget`, `pmxlws_hostile`, `own_pmx_lws_default`, `own_dispatch_e5`, `ase_hostile`,
  `smd_hostile`, `batchb_hostile` to `batche_hostile`.
- Under ASan + UBSan + float-cast-overflow (`-O1`, `-fno-sanitize-recover=all`, reader sources only, built by hand):
  `ase_hostile`, `smd_hostile`, `batchb_hostile` (LWO, X3D, DXF, AMF, 3MF), `batchc_hostile` (MD2), `batchd_hostile` (NFF,
  AC3D, OGRE, XGL, IRR), `batche_hostile` (CSM, with the real file), `batche2_hostile`, `q3d_reader`, `raw_reader`,
  `lws_reader`, `pmxlws_hostile`: 11 of 11 passed, no sanitizer report. The engine-linked tests (`model_readers_fuzz`,
  `model_corpus`) were **not** run under sanitizers this round.
- **Found, not fixed then (fixed in the third round):** `own_dispatch_e2` crashed in a build with `OKAY_USE_ASSIMP=ON`
  (a null child node that Assimp's RAW importer leaves, met by our scene walk). It is registered in ctest but not matched by
  the regex above.

First round:

- `ctest -R "model|import|collada|reader|obj|gltf|mesh|anim"` with `OKAY_MODEL_CORPUS` set, in a Release build with
  `OKAY_USE_ASSIMP=ON`: 104 of 104 passed (`model_corpus`: 3871 checks, 0 failures, 380 read, 34 recorded failing). Five of
  them (`3mf_reader`, `amf_reader`, `x3d_reader`, `dxf_reader`, `lwo_reader`) failed in such a build before this branch:
  a check meant for builds without Assimp ran in builds with it; it now runs only when no backend reads the format.
- Under ASan + UBSan + float-cast-overflow (no Assimp): `model_readers_fuzz`, `collada_reader`, the hostile and mutation
  tests of every reader changed here (`3ds_bvh_hostile`, `off_hostile`, `smd_hostile`, `md5_hostile`, `ms3d_hostile`,
  `ase_hostile`, `batchb_hostile` to `batche_hostile`, `pmxlws_hostile`, `terragen_hostile`) and `model_corpus` on the
  corpus: 15 of 15 passed, no sanitizer report.

## Not verified

- Windows. Nothing here ran on Windows: not the timings, not the MinGW or MSVC build of `std::from_chars` for floats (the
  code falls back to `strtof` / `strtod` when the library does not declare it), not the RAW crash in the shipped build.
- Timings on other machines and compilers. The ratios are from one noisy Linux container.
- Files from other exporters: the corpus is one library's test folder.
