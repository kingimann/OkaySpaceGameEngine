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
