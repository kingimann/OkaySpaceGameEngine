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

Assimp crashes on two corpus files (`RAW/WithColor.raw`, `RAW/WithTexture.raw`: a node's child list holds a bad pointer, in
Assimp 5.4.3's RAW importer) and dies (killed by a signal) on `invalid/OutOfMemory.off`, a file that claims 353 billion vertices. The bench therefore reads every
file with Assimp in a child process first and reports such a file as "crash" instead of dying. **The shipped Windows build
reads `.raw` through Assimp**, so importing those two files there probably crashes the editor (not tried on Windows).

The machine was a shared 4-core Linux container with other builds running, so a small file's time can move by 20% from run
to run; the instruction counts quoted below (callgrind) do not move.

## Results (Linux, GCC 13, Release)

| Reader | files | both read | read time ours / Assimp (before -> now) | time | largest peak heap ours / Assimp MB (before -> now) | memory | import time ours / Assimp | import peak ours / Assimp | files slower / heavier |
|---|---:|---:|---|---|---|---|---|---|---|
| 3ds | 16 | 15 | 0.25x -> 0.22x (8.7 / 39.9 ms) | met | 5.24 / 13.32 -> 4.91 / 13.32 | met | 0.24x | 4.91 / 13.32 MB | 0 / 0 |
| 3mf | 2 | 2 | 0.71x -> 0.67x (0.1 / 0.2 ms) | met | 0.02 / 0.11 -> 0.02 / 0.11 | met | 0.80x | 0.02 / 0.11 MB | 0 / 0 |
| ac | 11 | 7 | 0.47x -> 0.57x (239.4 / 422.8 ms) | met | 57.93 / 73.92 -> 57.93 / 73.92 | met | 0.41x | 57.93 / 73.92 MB | 4 / 3 |
| amf | 11 | 11 | 0.84x -> 0.86x (0.4 / 0.5 ms) | met | 0.05 / 0.13 -> 0.04 / 0.13 | met | 0.81x | 0.06 / 0.13 MB | 3 / 0 |
| ase | 17 | 16 | 4.44x -> 1.05x (7.3 / 7.0 ms) | **not met** | 3.99 / 2.40 -> 1.99 / 2.40 | met | 1.07x | 1.99 / 2.40 MB | 4 / 0 |
| b3d | 3 | 3 | 0.35x -> 0.35x (1.4 / 4.1 ms) | met | 1.21 / 1.75 -> 1.02 / 1.75 | met | 0.71x | 1.52 / 1.75 MB | 0 / 0 |
| blend | 28 | 27 | 0.10x -> 0.12x (59.8 / 517.0 ms) | met | 4.60 / 84.07 -> 4.00 / 84.07 | met | 0.12x | 4.00 / 84.07 MB | 2 / 11 |
| bvh | 3 | 3 | 1.24x -> 0.40x (28.4 / 70.6 ms) | met | 8.40 / 14.53 -> 7.66 / 14.53 | met | 0.94x | 8.89 / 14.53 MB | 0 / 0 |
| cob | 8 | 6 | 1.03x -> 0.92x (7.9 / 8.6 ms) | met | 1.69 / 1.31 -> 1.19 / 1.31 | met | 0.96x | 1.19 / 1.31 MB | 1 / 1 |
| csm | 1 | 1 | 7.14x -> 1.45x (9.9 / 6.8 ms) | **not met** | 6.73 / 6.73 -> 6.22 / 6.73 | met | 1.04x | 6.97 / 7.21 MB | 1 / 0 |
| dae | 26 | 25 | 4.32x -> 0.79x (26.0 / 32.7 ms) | met | 4.46 / 3.44 -> 3.26 / 3.45 | met | 0.78x | 3.26 / 3.45 MB | 2 / 0 |
| dxf | 5 | 5 | 0.99x -> 0.63x (5.0 / 8.0 ms) | met | 1.43 / 1.60 -> 1.39 / 1.60 | met | 0.62x | 1.39 / 1.60 MB | 0 / 1 |
| hmp | 2 | 2 | 0.08x -> 0.07x (0.1 / 1.9 ms) | met | 1.50 / 1.58 -> 0.64 / 1.58 | met | 0.07x | 0.64 / 1.58 MB | 0 / 0 |
| iqm | 1 | 1 | 0.83x -> 0.80x (0.6 / 0.7 ms) | met | 0.68 / 0.69 -> 0.68 / 0.69 | met | 5.18x | 1.46 / 0.69 MB | 0 / 0 |
| irr | 21 | 21 | 0.72x -> 0.35x (36.0 / 104.1 ms) | met | 2.32 / 1.91 -> 1.73 / 1.91 | met | 0.37x | 1.84 / 2.41 MB | 0 / 0 |
| irrmesh | 5 | 5 | 1.70x -> 0.77x (3.6 / 4.7 ms) | met | 1.25 / 0.76 -> 0.74 / 0.76 | met | 0.79x | 0.74 / 0.76 MB | 0 / 0 |
| lwo | 54 | 53 | 0.88x -> 0.89x (31.8 / 35.9 ms) | met | 1.83 / 1.60 -> 1.66 / 1.60 | **not met** | 0.89x | 1.66 / 1.60 MB | 14 / 1 |
| lws | 15 | 14 | 0.66x -> 0.54x (1.6 / 3.1 ms) | met | 0.03 / 0.11 -> 0.02 / 0.11 | met | 0.60x | 0.02 / 0.11 MB | 0 / 0 |
| md2 | 5 | 5 | 0.51x -> 0.56x (0.7 / 1.3 ms) | met | 0.75 / 0.43 -> 0.44 / 0.43 | **not met** | 0.49x | 0.44 / 0.43 MB | 0 / 2 |
| md3 | 3 | 3 | 0.34x -> 0.39x (0.3 / 0.8 ms) | met | 0.13 / 0.30 -> 0.12 / 0.30 | met | 0.39x | 0.13 / 0.30 MB | 0 / 0 |
| md5 | 5 | 4 | 1.20x -> 0.54x (5.6 / 10.3 ms) | met | 1.61 / 1.26 -> 0.59 / 1.26 | met | 0.82x | 1.25 / 1.26 MB | 0 / 0 |
| mdc | 1 | 1 | 0.62x -> 0.54x (0.8 / 1.5 ms) | met | 1.50 / 1.30 -> 1.05 / 1.30 | met | 0.58x | 1.05 / 1.30 MB | 0 / 0 |
| ms3d | 4 | 4 | 0.72x -> 0.69x (2.3 / 3.4 ms) | met | 1.28 / 1.56 -> 1.09 / 1.56 | met | 0.65x | 1.09 / 1.56 MB | 0 / 0 |
| nff | 13 | 12 | 1.19x -> 1.19x (113.3 / 95.1 ms) | **not met** | 6.76 / 13.09 -> 6.76 / 13.09 | met | 1.02x | 6.76 / 13.09 MB | 6 / 2 |
| obj | 28 | 25 | 0.13x -> 0.12x (6.5 / 53.9 ms) | met | 0.86 / 17.28 -> 0.85 / 17.28 | met | 0.12x | 0.85 / 17.28 MB | 0 / 0 |
| off | 5 | 2 | 1.96x -> 0.93x (1.4 / 1.5 ms) | met | 0.65 / 0.97 -> 0.55 / 0.97 | met | 0.91x | 0.55 / 0.97 MB | 0 / 0; Assimp crashes on 1 |
| ogre | 4 | 3 | 0.89x -> 0.72x (10.4 / 14.4 ms) | met | 5.43 / 4.20 -> 3.51 / 4.20 | met | 0.63x | 3.51 / 4.20 MB | 1 / 2 |
| ply | 12 | 9 | 0.32x -> 0.37x (18.4 / 50.5 ms) | met | 7.05 / 6.62 -> 5.12 / 6.62 | met | 0.34x | 5.51 / 6.62 MB | 1 / 0 |
| pmx | 1 | 1 | 0.22x -> 0.21x (1.5 / 7.2 ms) | met | 1.70 / 3.39 -> 1.50 / 3.39 | met | 0.18x | 1.51 / 3.39 MB | 0 / 0 |
| q3d | 4 | 0 | - | - | - | - | - | - | 4 only Assimp |
| raw | 4 | 1 | 2.35x -> 1.04x (2.4 / 2.3 ms) | **not met** | 1.26 / 1.43 -> 1.06 / 1.43 | met | 0.97x | 1.06 / 1.43 MB | 1 / 0; Assimp crashes on 2 |
| smd | 4 | 3 | 4.49x -> 1.20x (5.5 / 4.6 ms) | **not met** | 3.64 / 3.30 -> 2.64 / 3.30 | met | 1.16x | 2.64 / 3.30 MB | 2 / 0 |
| stl | 8 | 8 | 0.66x -> 0.64x (5.4 / 8.5 ms) | met | 1.37 / 2.10 -> 1.29 / 2.10 | met | 0.71x | 1.29 / 2.10 MB | 2 / 0 |
| ter | 2 | 2 | 0.01x -> 0.01x (6.5 / 618.3 ms) | met | 17.04 / 157.51 -> 16.54 / 157.51 | met | 0.02x | 28.07 / 157.51 MB | 0 / 0 |
| x | 12 | 11 | 0.64x -> 0.34x (15.9 / 46.6 ms) | met | 2.61 / 3.23 -> 2.41 / 3.23 | met | 0.37x | 2.41 / 3.23 MB | 0 / 0 |
| x3d | 4 | 3 | 1.07x -> 1.11x (3.2 / 2.8 ms) | **not met** | 0.39 / 0.67 -> 0.32 / 0.67 | met | 1.17x | 0.43 / 0.67 MB | 1 / 1 |
| xgl | 7 | 7 | 0.04x -> 0.04x (90.8 / 2505.9 ms) | met | 24.04 / 15.15 -> 12.61 / 15.15 | met | 0.04x | 12.61 / 15.15 MB | 1 / 0 |

`files slower / heavier` counts the files where ours took longer, or held more heap, than Assimp. "before" is the code before this work
(Collada: a run of main's code; the other readers: a run after only the Collada changes, which did not touch them); "now"
is the head of `claude2/own-collada-speed`.

### Where it is not met, and why

Of the 36 readers measured (Q3D could not be: ours refuses all four corpus files, see below), **28 meet both time and memory**.
The rest:

- **ASE, 1.05x time.** Inside the noise on small files, but real on the biggest file: on `MotionCaptureROM.ase` ours runs
  54 million instructions to Assimp's 39. What is left is the line dispatch (a chain of block-name compares per line) and
  number parsing, which reads every number exactly as `strtod` would (Assimp's parser is faster and not correctly rounded).
  Next step: dispatch on a code computed once per block.
- **CSM, 1.45x time** (one file, 3.6 MB of numbers: 138 million instructions to Assimp's 82, down from 482). Number parsing
  is now most of it.
- **NFF, 1.19x time.** Equal instruction counts on the largest file (`HOME4.NFF`: 93 million, Assimp 94) but slower in
  time: the mesh building (`rc::BuildMesh`, a hash map per vertex) misses the cache more. The classic files with spheres
  (`ManyEarthsNotJustOne`, `positionTest`) are also where ours tessellates differently from Assimp.
- **SMD, 1.20x time** (51 million instructions to Assimp's 39 on `WusonSMD.smd`, down from 95).
- **RAW, 1.04x time**: one file both read (Assimp crashes on the other two); within the noise.
- **X3D, 1.11x time**: all of it is `ComputerKeyboard.x3d`, where ours builds 100 parts and 2,020 triangles from the
  Extrusion nodes and Assimp builds 4 parts and 100 triangles; on the other two files ours is faster. Not a like-for-like
  comparison.
- **LWO, memory**: one file (`QuickDraw--Chasis.lwo`, 88 KB) peaks at 1.66 MB to Assimp's 1.60; summed over the 53 files
  ours needs 0.68x. The rest of the peak is the per-surface triangle soup (`rc::Soup`), which grows as it is filled.
- **MD2, memory**: 0.44 MB to 0.43 MB on the largest file, 0.87x summed: the file buffer is most of both peaks.

The import columns (the whole scene import) follow the read columns, except **IQM**: the import with ours takes 5.4x the
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

The two number helpers are written from the arithmetic, not from any library: `detail::FastDecimal` is the exact fast case
of decimal conversion (W. D. Clinger, "How to read floating point numbers accurately", 1990: digits that fit in 53 bits and a
power of ten of at most 22 are both exact doubles, so one multiplication or division rounds correctly and gives the value
`strtod` gives); anything outside it goes the old way. A randomized check of 3 million tokens (2.5 million answered by the
fast case) found no difference from `strtod`. `std::from_chars` is used only where the C library also reads '.' as the
decimal point (it is correctly rounded like `strtod`); under another locale the old code path runs unchanged.

## Accuracy: where our reader and Assimp disagree

`--compare` reads each file with both and compares part, triangle, vertex, node, material, clip and bone counts, the
bounding box in world space (1/1000 of the diagonal) and the node names. Of the 355 files, 61 agree on everything, 14 are
refused by both, and the rest differ in at least one number. Most differences are by design and documented in each reader's
page: vertex counts (Assimp joins identical vertices, we weld per corner key), node counts (Assimp adds or merges nodes), material counts
(Assimp adds a default material), and boxes for readers that turn Z-up files to Y-up at the root when Assimp does not.

Differences that are not by design, or that are worth a second look (counts are ours / Assimp's):

- **Q3D: ours refuses all four corpus files that Assimp reads** (`E-AT-AT.q3o`, `earth.q3o`: "bad vertex count";
  `WusonOrange.q3o` / `.q3s`: "an unexpected or repeated chunk type 158"). The Q3D reader is not in the `model_corpus`
  manifest, so no test caught it. It needs fixing before Q3D can switch.
- **RAW: Assimp crashes on two files ours reads** (`WithColor.raw`, `WithTexture.raw`); see above.
- **LWS: `QuickDraw v.2.2.lws` is refused by ours** and read by Assimp. **OGRE `fish.mesh.xml`, COB `dwarf_ascii.cob` and
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
  `ComputerKeyboard.x3d` (Extrusion), `ChevyTahoe.x3d` 1,298 / 1,234, the Irrlicht scenes with spheres and animators
  (480 / 1,280 and 3,140 / 4,740: sphere tessellation), `HelloX3dTrademark.x3d` 528 / 1,280 (sphere).
- **ASE UTF-16**: Assimp reads the two UTF-16 `ThreeCubesGreen` files as one part of 8 triangles; ours reads the 3 cubes
  (36 triangles), like the UTF-8 file.
- Bounding boxes differ on 68 files, almost all where one side turns Z-up to Y-up at the root and the other does not, or
  where the triangle counts above differ; clip counts differ on 17: Assimp makes clips that ours does not on 10 Irrlicht
  scenes (animators), the 3 SMD files (a one-frame pose), `ninja.mesh.xml` (20, from the skeleton file ours does not read),
  `CameraRollAnim.3ds` and `move_x_oldformat_56.lws`; ours makes one where Assimp makes none on `mrfixit.iqm`. Bone counts
  differ on 28 files.

The full per-file list: run `bench_model_readers --compare` (one tab-separated line per file, `DIFF` and the fields).

## Not verified

- Windows. Nothing here ran on Windows: not the timings, not the MinGW or MSVC build of `std::from_chars` for floats (the
  code falls back to `strtof` / `strtod` when the library does not declare it), not the RAW crash in the shipped build.
- Timings on other machines and compilers. The ratios are from one noisy Linux container.
- Files from other exporters: the corpus is one library's test folder.
