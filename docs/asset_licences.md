# Asset and library licences: what we hold, what is missing

Rule (CLAUDE.md 19a): code must be our own. Fonts, images, sounds, models and data that we cannot recreate may be used
only with permission, meaning a licence that allows our use. For each such asset we record the source and licence in
`docs/THIRD_PARTY.md`, keep the licence file next to it, ship the notices the licence requires, and never use an
asset whose licence is unknown.

Date of this inventory: 2026-11-14, from `origin/main` at `ef7d0404`. Method: read every tracked non-code file group,
the licence files, `git log` for how each file arrived, the generator scripts, and the fonts' own name tables (read
from the embedded font data). **Nothing was downloaded and no licence was guessed.** "Licence as the repo states it"
means only what is written in the repository itself.

Status words:

- **PERMITTED**: the licence is known, it allows our use, and the licence file is in the repository.
- **NEEDS NOTICE**: use is allowed (or says so by its own account) but the licence file or a notice is missing or
  incomplete. The owner has to supply the text; until then the notices file says "licence text missing: owner to supply".
- **UNKNOWN**: the repository does not say where it came from. The owner must answer.
- **OURS**: made by us (a generator in the repository, a render of our own program, or our own commit history says so).

## Counts

| Status | Items (a group of files counts once) |
|---|---|
| PERMITTED | 5 (JetBrains Mono, ImGuiColorTextEdit, ufbx, TweetNaCl-derived code, Wycheproof test vectors) |
| NEEDS NOTICE | 6 (DejaVu Sans, Roboto Medium, SDL2 / `SDL2.dll`, Dear ImGui, Assimp, libsodium) |
| UNKNOWN | 2 groups (the encoded audio fixtures; the three tone files) |
| OURS | 16 groups |

## The table

Where "shipped" says yes, the asset or library is in the Windows zip and installer (built by
`scripts/build-windows-dist.sh`). See `THIRD_PARTY_NOTICES.txt` and "What the packaging does now" below.

### Fonts

| Path | Size | What it is | Used in | Source and licence as the repo states it | Status |
|---|---|---|---|---|---|
| `assets/fonts/DejaVuSans.ttf` | 759,720 B | DejaVu Sans, version 2.37 (its name table) | Starter font copied to `Assets/Fonts/` in the distribution (shipped); `engine/src/Graphics/TtfFont.cpp` and `launcher/main.cpp` look for system copies; `tests/test_ttf_rasterizer.cpp` holds reference glyph bitmaps rendered from it (test only) | `docs/THIRD_PARTY.md`: "Free font licence". Licence file `assets/fonts/DejaVuSans-LICENSE.txt` is a Debian package copyright file: the Bitstream Vera licence, "Copyright (c) 2003 by Bitstream, Inc.", "DejaVu changes are in public domain". **Gap:** the font's own name table (ids 0 and 13) says it also contains glyphs from the Arev fonts, "Copyright (c) 2006 by Tavmjong Bah", with a licence notice; the repo's licence file has no Arev entry. The file also has a Debian packaging section (GPL-2+) that covers Debian's packaging, not the font | NEEDS NOTICE |
| `assets/fonts/DejaVuSans-LICENSE.txt` | 3,859 B | Licence file for the font above | shipped in `Assets/Fonts/` and `Licenses/` | n/a | licence file |
| `assets/fonts/JetBrainsMono-OFL.txt` | 4,399 B | SIL OFL 1.1 text and "Copyright 2020 The JetBrains Mono Project Authors" | shipped in `Assets/Fonts/` and `Licenses/` | Copied unmodified from the JetBrains tag v2.242 (see `docs/THIRD_PARTY.md`) | licence file |
| `shared/JetBrainsMonoFont.h`, `shared/JetBrainsMonoFontOwn.h` | 94,877 B and 95,393 B | JetBrains Mono Regular (name table: version 2.211) embedded, Dear ImGui format and our OKF1 container | Editor and launcher script editor; engine | OFL 1.1, header of `JetBrainsMonoFont.h` says so; licence file present (above) | PERMITTED |
| `shared/RobotoFont.h`, `shared/RobotoFontOwn.h` | 152,369 B and 147,744 B | Roboto Medium (name table: "Version 2.001152; 2014") embedded | Editor and launcher UI font; engine | **No licence file.** The font's own name table says "Copyright 2011 Google Inc." and "Licensed under the Apache License, Version 2.0"; `editor/main.cpp` line 702 calls it Apache 2.0; `RobotoFont.h` says it was exported from a Dear ImGui `misc/fonts/Roboto-Medium.ttf`. Those are leads, not a verified grant | NEEDS NOTICE |
| `assets/fonts/embedded_fonts.sha256` | 482 B | Size and SHA-256 pins of the two embedded fonts | test `embedded_font` | ours | OURS |
| 8x8 bitmap font in `engine/src/Graphics/Font.cpp` | in code | Built-in debug font | engine | `docs/THIRD_PARTY.md` lists it as written by us | OURS |

### Images and icons

| Path | Size | What it is | Used in | Source and licence as the repo states it | Status |
|---|---|---|---|---|---|
| `assets/textures/{dirt,grass,grid,stone}.png` | 1,215,483 B (4 files) | Starter textures | copied to `Assets/Textures/` (shipped) | Made by `tools/gen_textures.cpp`, procedural with fixed seeds (`docs/provenance_audit.md`) | OURS |
| `editor/okayspace.ico`, `launcher/okayspace.ico` | 15,524 B each | The OkaySpace "planet and ring" mark, 16 to 256 px | Windows exe icon (shipped inside the exes) | Commit `657e7525` (a Claude session) says it made the multi-size icon; no generator in the repo. `docs/UNFINISHED.md` calls it a placeholder | OURS (unverified: no generator) |
| `dist/character_preview.png` | 33,747 B | A render of our character, 460x680 | not referenced by any code (only a leftover from commit `00751090`) | Commit message: "Rendered the character to an image" | OURS |
| `docs/images/anticheat/*.png` | 851,158 B (21 files) | Screenshots of our editor, launcher and player | docs only, not shipped | Screenshots of our own program | OURS |
| `okayui/tests/golden/*.png` (34), `tests/golden/**` (9) | 593,488 B and 85,683 B | Reference images our renderer writes; contain our UI text drawn with the embedded fonts | tests only | Produced by our own code | OURS |
| `tests/data/images/*` (gif, bmp, jpg, png, hdr, `ref/`) | 343,974 B (339 files) | Image decoder fixtures | tests only | `tests/data/images/gen_fixtures.py` makes them (fixed random seed; Pillow and ImageMagick are the tools, not sources); `ref/` are libjpeg-turbo decodes of our own JPEGs | OURS |
| `tests/assets/fbx/*` | 63,997 B (14 files incl. 2 70-byte PNGs) | FBX test files | tests only | `scripts/gen_fbx_fixtures.py` writes them | OURS |
| `tests/assets/rigs/*.glb` | 218,292 B (11 files) | Small skinned test rigs, including "fox.glb", "mixamo_*" and "vroid_t" | tests only | Each file's glTF header says `generator: "okay make_test_rigs.py"`; `tools/make_test_rigs.py` makes them (box-skinned 25-joint humanoid under different bone naming conventions). The names refer to naming conventions only; no Mixamo, VRoid or Fox model data is in them | OURS |

### Sounds

| Path | Size | What it is | Used in | Source and licence as the repo states it | Status |
|---|---|---|---|---|---|
| `tests/data/audio/ogg/syn_*`, `tests/data/audio/mp3/synth_*` (with their `.s16` references) | about 1.3 MB (48 files) | Synthetic streams that reach every decoder feature | tests only | Commits #642 and #644 say they are synthetic streams generated for the tests. The generator is not in the repo | OURS (unverified: no generator) |
| `tests/data/audio/ogg/enc_*`, `tests/data/audio/mp3/*` (not `synth_*`), `tests/data/audio/flac/*` (with `.s16`, `.s32` references) | about 0.8 MB | Real encoder output (the files contain "Lavf60.16.100", the ffmpeg writer tag; the manifests say ffmpeg, LAME and libvorbis) with decoded references | tests only | The manifests say how they were decoded, not what the input signal was. Names such as `sine_mono_44k`, `noise_mono_8k`, `tones_stereo_22k` suggest generated signals. The generator is not in the repo | UNKNOWN |
| `examples/spatial_audio/tone.wav` | 176,444 B | 2 s, mono, 44.1 kHz tone | the Spatial Audio example scene | Added in #787. No note, no generator. Looks synthetic (a steady tone), but the repo does not say | UNKNOWN |
| `tests/data/audio/tone_mono.mp3`, `tests/data/audio/tone_stereo.ogg` | 2,551 B and 4,182 B | Short tones encoded with ffmpeg | tests (`test_audio_*`) | Same: no note, no generator | UNKNOWN |

### Models and data

| Path | Size | What it is | Used in | Source and licence as the repo states it | Status |
|---|---|---|---|---|---|
| `tests/data/models/*` | 104,291 B (43 files) | Small cube, rig and skeleton fixtures in many formats | tests only | Headers say "Hand-written test fixture" or "synthetic fixture"; `scripts/gen_*_fixtures.py` write the binary ones (`.3ds`, `.ms3d`, `.blend`) | OURS |
| `tests/data/model_corpus_manifest.tsv` | text | Paths and counts of Assimp's own test models | tests (skipped without the corpus) | Lists paths only; the model files are **not** in the repo (`docs/model_corpus.md`) | OURS |
| `tests/wycheproof/*.json`, `LICENSE`, `README.md` | 1,333,266 B (11 files) | Project Wycheproof crypto test vectors | `crypto_wycheproof` test only, not shipped | `README.md`: copied unmodified from github.com/C2SP/wycheproof commit 65ca494727a52944c204441c860b039e6786b99b, Apache-2.0; the repo's `LICENSE` is beside them | PERMITTED (the owner has still to say whether test data from another project is acceptable, `docs/provenance_audit.md` F9) |
| `examples/**/*.okay*` (135 files), `examples/*/main.cpp` | text | Example scenes, graphs, prefabs | examples | Written by us | OURS |
| `tests/data/vs_core_legacy.okayvs`, `tests/export/*`, `editor/generated/*.inc`, `*.pyc` build leftovers | text and bytecode | Generated or ours | tests, build | Generated by our scripts (`.pyc` files are compiled copies of our own scripts and should not be tracked) | OURS |
| `dist/OkaySpace*.exe`, `dist/OkaySpacePlayer.exe` | 26.8 MB (5 files) | Old committed build outputs (June 2026) | not packaged: the release workflow builds fresh ones | Our programs; they contain the third-party code listed below, so the notices apply to them too | OURS |

### Libraries that ship (code, listed here because the notice duty is the same)

| Library | Where | Licence as the repo states it | Licence file in repo | Status |
|---|---|---|---|---|
| Dear ImGui (docking, commit `036bf939...`) | downloaded at configure time; linked into launcher and editor | MIT (`docs/THIRD_PARTY.md`) | none | NEEDS NOTICE |
| ImGuiColorTextEdit | `editor/vendor/TextEditor.*` | MIT, "Copyright (c) 2017 BalazsJako" | `editor/vendor/TextEditor.LICENSE` | PERMITTED |
| ufbx 0.21.4 | `engine/third_party/ufbx/` | MIT or public domain (both offered; "Copyright (c) 2020 Samuli Raivio" for the MIT option) | `engine/third_party/ufbx/LICENSE` | PERMITTED |
| SDL2 2.30.9, `dist/SDL2.dll` (1,764,864 B, committed) and the DLL the release builds copy | `SDL2.dll` in the zip and installer | zlib (`docs/THIRD_PARTY.md`) | none | NEEDS NOTICE |
| Assimp 5.4.3 | downloaded at configure time; linked into the Windows build | BSD 3-clause | none | NEEDS NOTICE |
| libsodium 1.0.20 | MinGW prebuilt, statically linked in release builds | ISC | none | NEEDS NOTICE |
| TweetNaCl-derived port | `shared/AntiCheatCrypto.hpp` | public domain, per the header; no licence text exists or is needed. Needs the owner's approval under rule 19a (`docs/provenance_audit.md` F2) | not needed | PERMITTED |

Not assessed: the MinGW-w64 and GCC runtime code that every Windows build links (libgcc, libstdc++, winpthreads, MinGW
runtime) and the Steamworks redistributable `steam_api64.dll` (only when the release is built with the Steamworks SDK,
which is not in the repo). Their licences are outside the repository, so the notices file does not cover them yet.

## UNKNOWN items the owner must answer

1. **Audio fixtures** (`tests/data/audio/ogg/enc_*`, `tests/data/audio/mp3/*` except `synth_*`, `tests/data/audio/flac/*`):
   what signals were encoded? Evidence: ffmpeg's `Lavf60.16.100` writer tag, manifests that name `ffmpeg` and `libvorbis`
   decodes, file names `sine_*`, `noise_*`, `tones_*`. If they were generated by us, add the generator script
   (the status becomes OURS); if any came from someone else's recording, say whose and under which licence.
2. **Tone files** (`examples/spatial_audio/tone.wav`, `tests/data/audio/tone_mono.mp3`, `tests/data/audio/tone_stereo.ogg`):
   same question. Evidence: no note, no generator; the wav is a steady 2 s mono tone and the other two carry the ffmpeg tag.

The `syn_*` and `synth_*` streams and the `okayspace.ico` icon are marked OURS because commit messages say so, but their
generators are not in the repo; please confirm.

## NEEDS NOTICE items and what is missing

- **Roboto Medium**: owner to say where the file came from and add the Apache 2.0 text (for example
  `assets/fonts/Roboto-LICENSE.txt`, then add that path to `scripts/third_party_notices_data.json`).
- **DejaVu Sans**: the licence file lacks the Arev glyph notice that the font itself declares. The owner can supply the
  upstream `LICENSE` of the DejaVu release the font came from (it has both the Bitstream Vera and the Arev parts).
- **SDL2, Dear ImGui, Assimp, libsodium**: no licence file in the repo. Supply the licence text (and the copyright line)
  from the exact version we build. They are not vendored, so there is no file to copy; I did not write them from memory.

When a text is added, put its path into `licence_files` in `scripts/third_party_notices_data.json`, set the status, and
run `python3 scripts/gen_third_party_notices.py`.

## What the packaging does now

- `THIRD_PARTY_NOTICES.txt` (repo root) is generated by `scripts/gen_third_party_notices.py` from
  `scripts/third_party_notices_data.json`. `--check` fails when it is stale (ctest `third_party_notices`).
- `scripts/build-windows-dist.sh` runs `--check`, then `--stage build-win-dist`, which copies the notices to the top of
  the folder and every licence file to `Licenses/`, then runs `scripts/check_dist_notices.py build-win-dist`, which fails
  the build if they are missing or differ from the repository's. The zip and the NSIS installer are made from that folder
  (`File /r`), so both carry them. The font licence files also still go to `Assets/Fonts/`.
- `.github/workflows/release.yml` runs `check_dist_notices.py` on the finished zip. The check is not `--strict`, so a
  release is not blocked by the five missing texts; it prints a warning naming them. Flip to `--strict` when they are in.
- The uninstaller removes `THIRD_PARTY_NOTICES.txt` and `Licenses/`.
- Not covered: the launcher's self-update downloads single files (the exes); the notices arrive with the installer or the
  zip, not with a self-update. The editor and launcher have no Licences screen (proposed in `docs/UNFINISHED.md`).
- Unverified: no zip or installer was built in this change. The check script was tested on a staged folder and a
  synthetic zip only (`check_dist_notices.py --selftest`).
