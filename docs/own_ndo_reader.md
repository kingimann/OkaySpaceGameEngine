# Our own NDO reader (`.ndo`): a seam only, no geometry

Status: **header recognised, body NOT read.** Off by default (`OKAY_OWN_NDO`, slot 36). Do not switch it on: it refuses every file.

## What exists

`engine/src/Render/NdoReader.cpp` checks that a file starts with the text `nendo ` and reports the version text that follows; then it refuses the file with the message "Nendo file, version ..., recognised but not read". A file without that start is refused as "not a Nendo file". Nothing is allocated from the file; the file read is capped at 256 MiB. The seam (slot 36, extension `.ndo`, `OKAY_OWN_NDO`, CMake option, build-default test) is in place so a real reader can be dropped in. Tests: `ndo_reader`, `own_dispatch_e3`, `own_default_e3`.

## Why there is no geometry code

- No real `.ndo` file exists in this repository, on this machine, or in Assimp's test tree.
- The only descriptions of the Nendo body that exist are other projects' importer source code. Rule 19a forbids reading or following it. A web search for a specification found only that, so **no description of the body was used and none was guessed** (the search summary's passing facts are listed in the disclosure below). Inventing a layout would produce a reader that looks finished and is not.
- **Disclosure**: that search's summary text mentioned, in passing, that the header is `nendo 1.x`, that the file is big endian and that counts changed width at version 1.2. Only the `nendo ` header test uses that. Nothing else of it is used, but those three facts were seen, so the body is not "clean-room" untouched: whoever writes the body later should know that.
- `ImportableExtensions()` does not list `.ndo`, even with `OKAY_OWN_NDO=1`, because the reader refuses everything (tested in `own_dispatch_e3`).

## Checked again in batch E6 (2026-11-16)

A sparse checkout of the `test/models` and `test/models-nonbsd` directories of Assimp v5.4.3 (test files only, no importer source) holds **no `.ndo` file** (`git ls-tree` of the whole tag lists none), and no public description of the body was found that rule 19a lets us follow. Without a sample or a description a reader would be invented, so **the seam stays a seam**. This is the one importer of the six that still has no reader, by decision, not by oversight. What would unlock it: one real `.ndo` file (then it can be analysed the way `docs/own_sib_reader.md` was) or a description.

## What is needed to finish it

A format description we may follow (Izware's own, or a clean-room note by someone who did not copy code) or real files with a way to say what they contain (a Nendo install, to export the same model as OBJ and compare). Until then the format stays on Assimp. Honest status: **0% of the body.** Counting this row as done anywhere would be wrong.

## Provenance

Nothing beyond the header text `nendo ` was taken from anywhere. No importer source was read, copied or ported.
