# Our own RAW reader (`.raw`)

Status: **side by side, off by default** (`OKAY_OWN_RAW`, same rules as `docs/own_3mf_reader.md`). Assimp (when built) is the only other reader. Nothing was flipped. **Unverified against real files**: no `.raw` file is in this repository (Assimp's tree has 4, one empty; they are not available here).

## What it is

The "raw triangle" text format (for example the output of POV-Ray tools): one triangle per line, nine numbers, the x y z of the three corners.

| File | What |
|---|---|
| `engine/src/Render/RawReader.cpp` | `okay::ReadRawBuffer` / `okay::ReadRawFile` (declared in `ModelData.hpp`) |
| `engine/src/Render/ModelImport.cpp` | The seam: slot 33 (extension `.raw`) |
| `tests/test_raw_reader.cpp`, `tests/batche2_fixtures.hpp` | Hand-computed checks (`raw_reader`) |
| `tests/test_batche2_hostile.cpp`, `tests/test_batche2_budget.cpp`, `tests/test_own_dispatch_e2.cpp` | Hostile input, budgets and memory, the seam |

## Files Assimp is not given (2026-11-16)

Assimp 5.4.3's RAW importer builds a broken scene when a file has **two or more groups that hold triangles**: the root node says
it has one child per such group, but only the first child pointer is set and the others are null. Our scene walk then crashed
(the corpus files `WithColor.raw` and `WithTexture.raw`, and the test fixture of `own_dispatch_e2`, crashed every build with
`OKAY_USE_ASSIMP=ON`, which includes the shipped Windows build; not tried on Windows). Found from the crash's backtrace and by
running Assimp on small files.

`RawFileUnsafeForAssimp(path)` (`RawReader.cpp`, declared in `ModelData.hpp`) says whether a file is such a file. It reads the
file once, line by line, the way the importer splits it into groups, and errs on the side of "unsafe": UTF-16 text, a line of
4,000 characters or more (the importer copies lines into a 4,096-byte buffer) and more than 1,000 group names also count.
The import seam (`ImportModelData`) uses it whenever the choice for `.raw` is Assimp:

- default choice, or the per-format switch set to Assimp (`OKAY_OWN_RAW=0`, `SetImportBackend(".raw", Assimp)`): **our RAW
  reader reads the file instead** and the log says so (`... cannot read safely; reading it with our own RAW reader`); if ours
  refuses it, Assimp is not tried;
- an explicit `ImportModelData(path, ..., ImportBackend::Assimp)` (the bench, tests that compare the two): refused with the
  message "RAW: this file has more than one group of triangles, which crashes Assimp 5.4.3's RAW importer ...".

A file with one group (`Wuson.raw`) still goes to Assimp by default. The Assimp scene walk also skips null children and mesh
indexes past the mesh list now, as a second guard. Checked: the 11 small cases in `own_dispatch_e2` were each run through
Assimp itself (a scratch program that reads the file and looks for null children) and the guard agrees with it on every one,
and on the 3 corpus files.

**How to try it** (Windows release, which has Assimp): import a `.raw` file with two groups, for example

```
first
0 0 0  1 0 0  0 1 0
second
0 0 1  1 0 1  0 1 1
```

saved as `two.raw`. Before this change the editor crashed; now it imports two parts and the log has the line above.

## What it reads

- A line of nine numbers is a triangle. Twelve numbers: a red green blue colour first, then the nine (the colour is not imported). Nine numbers and a name after them (the texture-name variant): the triangle goes to the group of that name.
- A line whose first word is not a number names a group (the whole line is the name); the triangles after it belong to it. Before any name, the group is `RAW`. Lines starting with `#` and empty lines are skipped.
- One part per group. Triangles with exactly equal corner positions are welded inside a group and smooth normals are made (60 degree crease). Degenerate triangles are dropped. UTF-8 and UTF-16 byte order marks are accepted.

## Not handled

Binary raw files (a NUL byte is refused with that reason; no description of one was found). Quads or any other line length. The twelve-number reading as colour-then-nine follows the description's colour variant; whether real files with twelve numbers mean that is a guess (unverified). A triangle spread over several lines (one per line is required, although the description says any white space may separate the numbers). Colours and texture images.

## Limits

File 256 MiB; 500,000 triangles, 100,000 groups, 4,000,000 output vertices per file; 64 values per line (a longer number line is refused after the 65th token is seen; lines are read in place, so one huge line costs no memory: a 64 MB line of single-character words peaks at 0 KB); numbers finite and within 1e15 (`nan`, `inf`, hex and `1e999` are refused). The worst accepted file (500,000 triangles with all-different corners) reads in 2 s with a peak of 284 MB. No recursion; no other file is named, so `ResolveInsideDir` is not needed.

## Tests and what was checked

`raw_reader` (43 checks): groups, the default group, the texture-name line, the colour line (area 2 only when the nine numbers are read after the colour; breaking that offset turns the test red), `\n` `\r\n` `\r`, welding, number syntax, refusals. `batche2_hostile` (truncations, bit flips, garbage, 2 MB numbers, 5,000 groups) and `batche2_budget` (500,000 read, 500,001 refused naming the budget; removing the budget turns the test red).

## Provenance

Written from the public description "POV-Ray RAW triangle format" (Paul Bourke): nine numbers per triangle, the twelve-number colour variant, the texture-name variant, non-numeric identifiers separating groups. The `#` comment is my own choice (the description gives no comment syntax). No importer source and no code from any other project was read.
