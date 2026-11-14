# Provenance audit: code that may have been copied or ported

Date: 2026-11-14. Branch `claude/provenance-audit`, cut from main at `ff89d98a`. Read-only on source code: nothing
in `engine/`, `shared/`, `editor/` and so on was changed. Only this page and `scripts/provenance_scan.py` were added.

The rule being checked is CLAUDE.md rule 19a: no copyrighted code, write everything ourselves; ideas, algorithms and
file formats are fine, code is not; public-domain code needs the owner's approval first.

**Short answer.** One clear case of a port was found (the LWO projected-UV code, from Assimp, BSD-3). One close port of
public-domain code was found that is already listed (the TweetNaCl port in `shared/AntiCheatCrypto.hpp`), and one
possible port of public-domain constants that is **not** listed (the reverb tuning numbers, which match Freeverb). Two
notice problems exist (Roboto has no licence file, and nothing ships the notice files). Everything else the scan
found is a standard table, standard test data, "behaves like Assimp" wording, or a false alarm. A text scan cannot prove
that nothing else was copied; see "Method and limits".

## Summary, most serious first

| # | Severity | Where | Possible source and licence | Action |
|---|---|---|---|---|
| 1 | **Clear copy / close port** | `engine/src/Render/LwoReader.cpp` lines 70 to 134 (`ProjectUvs`) | Assimp `ComputeUVMappingProcess.cpp`, BSD 3-clause | Rewrite from the idea (section F1) |
| 2 | **Close port of public-domain code (listed)** | `shared/AntiCheatCrypto.hpp` lines 1 to about 400 (SHA-512, Ed25519) | TweetNaCl, public domain | Needs the owner's approval (rule 19a); already planned for removal behind `okay::crypto` (F2) |
| 3 | **Probable copy of public-domain constants (not listed)** | `engine/src/Audio/SpatialFx.cpp` lines 12 to 15 | Freeverb by Jezar, public domain | Ask the owner, or pick our own delay lengths (F3) |
| 4 | Possible close re-derivation, unverified | `engine/src/Crypto/Fe25519_16.hpp` (16 limbs of 16 bits, fold by 38) | TweetNaCl's field code, public domain | Unverified; part of the outside crypto review (F4) |
| 5 | Vendored third-party code (allowed, listed) | `editor/vendor/TextEditor.*`, `engine/third_party/ufbx/` | ImGuiColorTextEdit (MIT), ufbx (MIT or public domain) | Keep until replaced; ufbx public-domain option needs the owner's approval (F5) |
| 6 | Notice or licence gap | Roboto Medium (no licence file); no packaging step copies any notice file | Roboto: Apache 2.0 (per the font's own name table) | Owner must supply the licence; add notices to release archives (F6) |
| 7 | Behaviour copied, code not (judgement call) | Own model readers, comments such as "as Assimp does", "imports like Assimp" | Assimp, BSD 3-clause | Rule 19a does not forbid matching behaviour; ask the owner to confirm (F7) |
| 8 | Standard table / standard algorithm | MP3 Huffman tables and window, SHA-2 constants, CRC-32, inflate tables, JPEG tables, Ed25519 constants | ISO, FIPS, RFC, ITU specs | Keep; say so in the file headers (F8) |
| 9 | Test data from elsewhere (licensed, notice present) | `tests/wycheproof/*.json` | Project Wycheproof, Apache 2.0, `LICENSE` and `README.md` present | Keep; owner to confirm that test data counts as allowed (F9) |
| 10 | False alarm | "mirrors Box2D / PhysX", ImGui start-up calls, `0x9E3779B97F4A7C15`, `copy of`, "Blender-style" | none | None |

Counts: **1 clear copy or close port (not approved)**, **1 close port of public-domain code (listed, approval pending)**,
**1 probable public-domain constants (not listed)**, **1 unverified**, **2 vendored libraries (listed)**, **2 notice
gaps**, **1 judgement call**, **3 standard-data or test-data groups**, **1 group of false alarms**.

## Known third-party pieces (step 1)

All of these are in `docs/THIRD_PARTY.md`. Notice files were checked by looking in the repository.

| Piece | Where | Licence status | Notice file present? |
|---|---|---|---|
| SDL2 | system package, `SDL2.dll` bundled in Windows builds | zlib | No file in the repository (not vendored). The Windows distribution ships the DLL; whether it ships `COPYING.txt` is **unverified** |
| Dear ImGui | downloaded at configure time (`editor/CMakeLists.txt`, `launcher/CMakeLists.txt`, `tests/CMakeLists.txt`) | MIT | Not vendored. A binary that contains ImGui needs its MIT notice; none is shipped by any script (F6) |
| ImGuiColorTextEdit | `editor/vendor/TextEditor.*` | MIT, "Copyright (c) 2017 BalazsJako" | Yes: `editor/vendor/TextEditor.LICENSE` |
| ufbx | `engine/third_party/ufbx/` | MIT or public domain, both offered | Yes: `engine/third_party/ufbx/LICENSE`. Which ufbx version this is was not checked |
| Assimp 5.4.3 | downloaded at configure time (`engine/CMakeLists.txt`, optional) | BSD 3-clause | Not vendored. A binary that links it needs its notice (F6) |
| libsodium | system package, optional | ISC | Not vendored |
| TweetNaCl port | `shared/AntiCheatCrypto.hpp` | Public domain | Not needed legally; **needs the owner's approval under rule 19a** (F2) |
| stb `stb_decompress` | removed from the engine; the old base85 arrays remain in `shared/RobotoFont.h` and `shared/JetBrainsMonoFont.h` | Public domain | n/a. The arrays are font data compressed with that format; the decoder is no longer used by the engine |
| DejaVu Sans | `assets/fonts/DejaVuSans.ttf` | Free font licence | Yes: `assets/fonts/DejaVuSans-LICENSE.txt` (a Debian copyright file) |
| JetBrains Mono | embedded in `shared/JetBrainsMonoFont.h`, `shared/JetBrainsMonoFontOwn.h` | SIL OFL 1.1 | Yes: `assets/fonts/JetBrainsMono-OFL.txt` |
| Roboto Medium | embedded in `shared/RobotoFont.h`, `shared/RobotoFontOwn.h` | Apache 2.0 according to the font's own name table; **not confirmed** | **No** |
| Starter textures | `assets/textures/*.png` | Ours: made by `tools/gen_textures.cpp` (procedural, fixed seeds) | n/a |

Earlier third-party code that git history shows was already removed: stb_vorbis (#644), stb_image_write (#639),
tinyfiledialogs (#640), stb_decompress (engine side).

## Findings in detail

### F1. LWO planar, cylindrical and spherical UV projection (clear copy / close port)

- **File and lines:** `engine/src/Render/LwoReader.cpp`, `ProjectUvs`, about lines 70 to 134; the call site near 495 to 505.
- **Evidence from our own text:** line 71, "This is Assimp's ComputeUVMappingProcess"; line 83, "the two axes after `ax` in Assimp's formulas"; line 498, "the same mapping as Assimp's GenUVCoords step"; `docs/own_lwo_reader.md` line 23 says the same. The file's first lines say "no code from any other importer", which is therefore wrong for this function.
- **Evidence from comparing with the source:** a copy of Assimp 5.4.3 was already on this machine (a scratch build from an earlier session) and was read, not copied. The structure matches in ways that a re-derivation from the idea would not give:
  the seam repair has the same flags (zero, one, round-to-zero, a "small" and a "large" corner), the same 0.1 / 0.9 thresholds and the same 0.01 / 0.99 epsilons, and the same three-way branch when both a zero and a one corner exist; the angle uses the same `atan2` argument pairs per axis; the spherical case uses the same `asin` form. The plain cylinder and sphere maths is a standard formula, but the seam-repair block and the flag logic are Assimp's, restructured and renamed.
- **Possible source licence:** BSD 3-clause (Assimp). It requires the notice to be kept when the code is redistributed.
- **Severity:** close port (verified by comparison, not just by the comment).
- **Action:** rewrite from scratch, from the idea only (project the vertex onto the map's axis, compute an angle around the axis, handle the wrap at U = 0 and U = 1 in our own way, for example by deciding per triangle from the shortest way round the circle instead of threshold flags). Then re-run the LWO corpus comparison. Correct the header comment and `docs/own_lwo_reader.md`. Note that the old threshold behaviour may differ from the new in rare files; that is acceptable and should be recorded.

### F2. TweetNaCl port in `shared/AntiCheatCrypto.hpp` (close port, public domain, listed)

- **Evidence:** the header comment (lines 6 to 8) says "a C++ port of TweetNaCl's public-domain ed25519 (Bernstein et al.)". The code uses TweetNaCl's field representation (`typedef std::int64_t gf[16]` at line 189), TweetNaCl's helper names (`Carry`, `Sel`, `Pack`, `Unpack`, `Inv`, `Pow2523`, `ScalarMult`, `ScalarBase`, `UnpackNeg`), its curve constants named `D`, `D2`, `X`, `Y`, `I` and its group-order array `l`. It also holds the SHA-512 round-constant table (`K[80]`), which is standard FIPS 180-4 data.
- **Licence:** public domain. Rule 19a says public-domain code needs the owner's approval; `docs/THIRD_PARTY.md` lists it as allowed only until `okay::crypto` is reviewed. I did not find a recorded approval; I could not verify that one was given elsewhere.
- **Severity:** close port.
- **Action:** ask the owner (question 2). The planned path already exists: `okay::crypto` (`engine/src/Crypto/`) is written from RFC 8032, RFC 7748, FIPS 180-4 and RFC 8439 and its headers say so. Rule 18 says crypto needs an outside review before it replaces anything. Until then, keep the port and keep it listed.

### F3. Reverb delay lengths (probable copy of public-domain constants, not listed)

- **File and lines:** `engine/src/Audio/SpatialFx.cpp` lines 12 to 15: `kCombTuning`, `kAllpassTuning`, `kSpread` (23), `kInputGain` (0.015), `kAllpassFeedback` (0.5).
- **Evidence:** the eight comb lengths (1116 to 1617), four all-pass lengths (556 to 225), the stereo spread of 23, the input gain 0.015 and the all-pass feedback 0.5 are, from memory, the published constants of Freeverb by Jezar at Dreampoint (public domain). I could not check them against the original here (no copy on this machine, none fetched), so the match is **unverified**. The class is called `SchroederReverb`, which is the name of the general technique, and its `Comb` and `Allpass` functions are the textbook structure. No file says where the numbers came from. `docs/audio.md` also mentions the reverb.
- **Severity:** probable copy of data; the code around it is the standard comb and all-pass network and was not compared.
- **Action:** ask the owner (question 3). If the answer is "not allowed", choose our own delay lengths (mutually prime, scaled to 44.1 kHz) and our own gains, and listen-test them.

### F4. `Fe25519_16.hpp` (possible close re-derivation, unverified)

- **File:** `engine/src/Crypto/Fe25519_16.hpp`, 149 lines.
- **Evidence:** sixteen 16-bit limbs in `int64_t`, carry that folds the top limb back times 38, and a 31-entry product array folded with `38 * t[i + 16]`. This is the same representation and the same shape as TweetNaCl's field code. The comment does not claim a source. The representation is a well-known technique, and the code is written with its own types and names (`Fe`, `Carry`, `Mul`, `MulSmall`, `Freeze`).
- **Severity:** unverified; could be an independent write-up of a standard technique, could be a close port. Not enough to say.
- **Action:** include in the outside crypto review package (`docs/crypto_review_package.md`) with the question "was this written from the field arithmetic, or from TweetNaCl". If the owner wants certainty, rewrite on a different representation (the 51-bit version `Fe25519_64.hpp` already exists, so the portable fallback could use, for example, ten 25.5-bit limbs).

### F5. Vendored libraries (allowed, listed)

- `editor/vendor/TextEditor.cpp` and `.h` (ImGuiColorTextEdit, MIT, notice present). Not scanned for modifications.
- `engine/third_party/ufbx/ufbx.c` and `.h` (MIT or public domain; the file offers a choice of two licences). To stay clear of rule 19a's "public-domain needs approval", the MIT option is the one to choose; the owner should record that (question 5).
- Both are on the removal plan in `docs/own_code_roadmap.md`. The own FBX reader (`FbxReader.cpp`, `FbxScene.cpp`) says "no third-party code". An overlap run of our sources against `ufbx` found no matching runs of 3 or more lines.

### F6. Notice gaps

- **Roboto Medium** has no licence file in the repository. `docs/THIRD_PARTY.md` already says so and lists the leads.
- **No script or workflow copies any notice file** into release archives or the installer. A search of `scripts/`, `.github/workflows/`, `dist/` and the CMake files for `OFL`, `LICENSE` and the notice file names found only documentation. The OFL (JetBrains Mono), Apache 2.0 (Roboto), the DejaVu licence, MIT (Dear ImGui, TextEditor, ufbx) and BSD (Assimp) all need their notice to travel with a binary or a font that contains them. I did not inspect a built release archive, so "not shipped" is **unverified**; "no step in the repository adds it" is what was found.
- **Action:** add one `THIRD_PARTY_NOTICES.txt` to every release archive (the text of each licence above), and add `assets/fonts/Roboto-LICENSE.txt` once the owner confirms the source. This is a docs and packaging task, not a source rewrite.

### F7. Behaviour matched to Assimp (judgement call)

The own readers were checked against Assimp on a real-file corpus (`docs/model_corpus.md`). Comments and tests say things like "imports like Assimp", "as Assimp reads it", "what Assimp's GenSmoothNormals does", "as Assimp's pre-transform step does". Counts of comments that name Assimp: `ModelImport.cpp` 35, `ModelImportAssimp.cpp` 16 (the backend itself, expected), `LwoReader.cpp` 5, `ModelReaders.cpp` 4, `ColladaReader.cpp` 1, `Gltf.hpp` 2, `DxfReader.hpp` 1.

- **Corpus:** the Assimp test files are not copied into the repository. `tests/data/model_corpus_manifest.tsv` lists paths and the counts our readers gave. No Assimp expected-result file is copied.
- **Code overlap:** a line-by-line comparison of all our own sources with Assimp's `code/` and `contrib/` found only one hit, three generic struct-member lines in `FbxReader.hpp` (a false alarm). See "Method and limits" for what that does and does not show.
- **Reading of rule 19a:** matching what another program outputs is behaviour, not code. It is allowed by the rule as written. The risk is that behaviour matched by reading the other source (as F1) turns into code. Recommend: keep behaviour matching, but compare against Assimp only by running it, never by reading its source (question 4).

### F8. Standard tables and algorithms (keep, say so)

These are numeric data or algorithms fixed by a specification. Typing the same numbers is unavoidable; copying them from a library's source file is not necessary. Where a file already says where its data comes from, that is noted.

| Data | Where | Source of truth | Note |
|---|---|---|---|
| MP3 Huffman tables 1 to 3, 5 to 13, 15, 16, 24, count1 table, 512-entry synthesis window | `engine/src/Audio/Mp3Tables.hpp` | ISO/IEC 11172-3 Table B.7 and the synthesis window | The header cites the standard. The layout (separate length and code arrays per table) and the window "first 257 of 512 in units of 1/65536" is our own choice, as far as I can see. I could not compare with minimp3, stb or other decoders (none on this machine), so a match with a library's table file is **unverified**. The numbers themselves are standard |
| MP3, Vorbis, FLAC decoders | `Mp3Decoder.cpp`, `VorbisDecoder.cpp`, `FlacDecoder.cpp` | ISO 11172-3 and 13818-3; Vorbis I spec; FLAC format spec | Headers say "written from the spec". Not compared line by line with any library |
| SHA-256, SHA-512 constants | `engine/src/Crypto/Sha256.cpp`, `Sha512.cpp`, `AntiCheatCrypto.hpp` | FIPS 180-4 | Standard; own comments explain how they are defined |
| Ed25519 constants | `engine/src/Crypto/Ed25519.cpp` | RFC 8032 | The file says d, sqrt(-1) and the base point "are derived from their definitions on first use"; nothing is pasted in. Good practice |
| CRC-32 | `ZipLite.hpp`, `ImageWrite.cpp`, `OkfDecode.hpp` | zlib and PNG specs | One-line generator loop, not a pasted table |
| Inflate length and distance tables | `FbxReader.cpp` line about 70 (the PNG decoder in `ImageDecode.cpp` was not checked for its own copy) | RFC 1951 | Standard |
| JPEG quantisation and Huffman tables | `ImageDecodeJpeg.cpp` | ITU T.81 Annex K | Standard (tables not compared with any library). The header says "libjpeg-style fancy upsampling", which is a named algorithm (triangle filter), not code |
| Mix constant `0x9E3779B97F4A7C15`, PCG and splitmix constants | many files | public algorithms | Standard constants |
| Perlin or SH constants | searched, none found as pasted tables | n/a | n/a |

### F9. Test data from other projects

- `tests/wycheproof/*.json`: copied unmodified from Project Wycheproof (Apache 2.0, commit `65ca4947...` per `tests/wycheproof/README.md`), with the repository's `LICENSE` beside them. Test data only; not linked or included in any program. This is the one place where third-party material is stored in the repository besides the vendored libraries. Whether that fits "no test files from other projects" in rule 19a is a question for the owner (question 6). The rule text lists test files, so by a strict reading it does not fit; the alternative is to download them at test time, as the model corpus is.
- `tests/data/` (images, audio, models): the fixture generators (`tests/data/images/gen_fixtures.py`, `scripts/gen_fbx_fixtures.py`) suggest these are generated by us. Not checked file by file; **unverified**.

### F10. False alarms

- "Mirrors Box2D's rest optimisation" and "Mirrors the PhysX rest optimisation" (`Physics2D.cpp` line 318, `Physics3D.cpp` line 507): names a well-known idea (sleep when at rest), not code.
- "Blender-style", "Blender's Auto Smooth" and similar in `Mesh.hpp`: names of editing tools whose behaviour is described; no code.
- ImGui start-up and shutdown calls in `editor/VsGraphShot.cpp` and `launcher/main.cpp`: the documented API sequence for using the library, not copied library code.
- `okayui/` is a Dear ImGui style API ("ImGui-style" auto layout). API shape is an idea. An overlap run against a Dear ImGui source tree found no matching runs in `okayui/`.
- "taken from", "copy of", "derived from" in comments, almost all about program data ("a copy of the prefab"), not about code origin.

## Method and limits

1. **Known pieces.** Read `CLAUDE.md` and `docs/THIRD_PARTY.md`; looked for each piece and its notice file in the repository.
2. **Marker scan.** `python3 scripts/provenance_scan.py markers` lists comment lines in `engine/ shared/ editor/ launcher/ player/ okayui/ relay/ tools/ scripts/ tests/` (skipping `editor/vendor`, `engine/third_party`, `tests/wycheproof`, `tests/data`) that contain porting words ("ported from", "based on", "adapted from", "taken from", "translated from", "copied from", "à la", "the way X does") or names of other projects and libraries. It printed 527 lines. I read the ones not about Assimp, Dear ImGui, SDL or Blender by eye; the rest are counted above.
3. **Line overlap.** `python3 scripts/provenance_scan.py overlap <reference folders>` compares our own C and C++ files with a local source tree of another project. It ignores whitespace, comments and preprocessor lines and reports runs of consecutive matching lines. It was run against: the Assimp 5.4.3 source (`code/` and `contrib/`), a Dear ImGui source tree, and the vendored ufbx. Results: one false-alarm hit in `FbxReader.hpp`; start-up boilerplate hits in `editor/VsGraphShot.cpp` and `launcher/main.cpp`; nothing for ufbx. It prints file names and line numbers only, never the other code.
4. **History.** `git log -S` and message searches for "port", "ported from", "copied from", "stb", "Assimp", "vendored". They show the removed libraries listed above and the Assimp-comparison work; no message says that code was ported from a named library except the LWO change (#835, "the way Assimp's GenUVCoords step does").
5. **Reading.** The LWO function was compared with the Assimp source by eye (F1). Headers of every own model reader, every decoder, `okay::crypto`, the TTF rasteriser and image decoders were read for stated sources.

**Limits. A text scan cannot prove that nothing was copied.**

- The overlap check only finds code that was copied nearly line for line. A port that renames identifiers, reorders statements or changes the loop shape will pass. F1 would **not** have been found by the overlap run (it did not report `LwoReader.cpp`); it was found by the comment and confirmed by reading. So the overlap result is weak evidence.
- It can only compare with source trees that exist on this machine: Assimp, Dear ImGui, ufbx. **Not compared (no copy available, nothing fetched):** minimp3, stb_vorbis, stb_image, dr_libs, miniaudio, libsodium, TweetNaCl, meshoptimizer, Box2D, Bullet, PhysX, Recast and Detour, Godot, Unreal, Unity, Freeverb, FreeType, libjpeg, zlib or miniz, lodepng. Findings that depend on those (F3, F4, the MP3 tables) are from memory and marked unverified.
- Own style differences (long files by one author versus short ones) were looked at only by sampling headers; I did not read the 2,858 tracked files. Large hand-written files not read in full include `launcher/main.cpp`, `editor/main.cpp`, `OkayScriptVM.cpp`, `Mesh.hpp`, the renderers, the physics code, `TtfRaster.cpp`, `ImageDecode.cpp`, `VorbisDecoder.cpp`, `FlacDecoder.cpp`, `Mp3Decoder.cpp`.
- Comments and documents written by the author may be missing, wrong or untrue ("no code from any other importer" is false for one function).
- Shaders (GLSL and HLSL strings in the renderers) were not compared with any known shader source.
- Data tables inside the Dear ImGui-format font headers are font files, not code.
- Licence statements here are from the repository and from memory; they are not legal advice.

## Questions for the owner

1. **LWO UV projection (F1).** Approve a rewrite from scratch of `ProjectUvs` now? It is the only clear port found and the fix is small (about 70 lines plus doc wording).
2. **TweetNaCl port (F2).** Do you approve keeping it, as public-domain code, until `okay::crypto` has passed the outside review? If not, what should the launcher use in the meantime?
3. **Freeverb numbers (F3).** Are public-domain reverb constants acceptable, or should the delay lengths and gains be re-chosen by us?
4. **Behaviour matching with Assimp (F7).** Confirm that matching Assimp's output (by running it) is acceptable, and that reading Assimp's source to do so is not.
5. **ufbx licence choice (F5).** Record that we take ufbx under the MIT option, not the public-domain option?
6. **Wycheproof test vectors (F9).** Apache-2.0 test data stored in the repository: keep, or download at test time only?
7. **Roboto (F6).** Where did the font come from, and can you add its licence text?
8. **Notices in releases (F6).** Should I add a `THIRD_PARTY_NOTICES.txt` to the release archives and installer (docs and packaging only)?
9. **Order of work.** Should the audit be repeated with other reference trees (minimp3, stb, libsodium, Freeverb, meshoptimizer) fetched into a scratch folder? This needs network access and would make F3, F4 and the MP3 table check verified.

## Recommended order for rewriting

1. `ProjectUvs` in `LwoReader.cpp` (F1): clear port, small, testable against the existing LWO corpus.
2. The reverb constants in `SpatialFx.cpp` (F3), if the owner says not to keep them: a few numbers and a listening test.
3. `Fe25519_16.hpp` (F4): only after the owner's answer and the reviewer's view.
4. The TweetNaCl port (F2): removed by the existing crypto plan, after the outside review (rule 18), not before.
5. Packaging notices (F6): not a rewrite, but it is a licence obligation that exists today.
