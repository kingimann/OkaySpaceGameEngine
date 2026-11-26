# What is custom and what we build on

> **What this is:** OkaySpace is mostly our own code. This page lists what we wrote, and the outside libraries and services we build on, with their licences.
>
> **Status:** Reference page for this feature. Kept as written; not extended (rule 5).
>
> **Last checked:** 2026-11-26 (two sections added by the IP audit, `provenance_audit.md` 2026-11-26; the rest as on 2026-11-20).

OkaySpace is mostly our own code. This page lists what we wrote, and the outside libraries and
services we build on, with their licences. The engine itself is proprietary (see `LICENSE`); third-party components keep their own licences.

## Policy and goal

The goal is for everything to be our own code, and for every third-party library below to be **removed**, one piece
at a time. Our replacements are not a permanent second option and are not kept off by default (`CLAUDE.md` rule 17,
2026-11-22): each is built, tested against the library until it gives the same or better results, made THE path, and
the library code and its CMake option are deleted (rule 19c). Cryptography is the exception: `okay::crypto` stays off
until an outside review (rule 18). The per-library plan, state and blockers are in [own_code_roadmap.md](own_code_roadmap.md). Until a library is
removed it is allowed only because the replacement is not ready, and a new library must not be added when writing our
own is possible; add one only with the same removal plan.

Order: the small, easy libraries first (done), then model import (ufbx deleted 2026-11-23; Assimp left), then the platform layer (SDL2),
then the editor toolkit (Dear ImGui), and encryption (libsodium) last. For encryption, writing our own is a security
risk, so it needs an outside review before it can be the default; see [crypto_review_package.md](../platform/crypto_review_package.md).
Services run by other companies (Steamworks, Photon, our hosting) are not libraries and stay.

## Written by us (custom)

| Area | Where | What it is |
|---|---|---|
| Engine core | `engine/` | Scene and GameObject model, components, serialization, property registry, save games, localization, time and loop, object pooling |
| Rendering | `engine/src/Render`, `engine/include/okay/Render` | Our own software renderer plus OpenGL, Direct3D 11 and Direct3D 12 backends, camera stack, lighting, post-processing |
| Physics | `engine/` | 2D and 3D physics, joints, ragdolls, queries, fixed-step simulation |
| Animation and IK | `engine/src/Animation` | Animators, state machines, humanoid rig mapping, root motion, foot, hand and full-body IK |
| Audio | `engine/src/Audio` | Mixer, groups, music player, one-shots, and our own MP3, Ogg Vorbis and FLAC decoders |
| In-game UI | `engine/` UI, `okayui/` | Canvas, widgets, themes, layout, scrolling, rich text, UI Gallery |
| Scripting | `engine/src/Scripting`, `engine/src/VisualScript` | OkayScript language and VM, the visual-script graph engine and every node |
| Networking | `engine/src/Net`, `relay/` | UDP transport, server authority, rooms, matchmaking, anti-cheat, dedicated server, relay |
| Editor | `editor/` | The desktop editor: Hierarchy, Inspector, Project panel, Scene and Game views, script and visual-script editors, modeling, animation windows |
| Launcher | `launcher/` | Self-updating launcher, Versions page, projects, chat, friends, servers |
| Player | `player/` | The runtime that runs exported games |
| Tools and scripts | `tools/`, `scripts/`, `shared/` | Generators, release and publish scripts, shared headers (project upgrade, dev log) |
| Platform helpers | `shared/FileDialog*`, `engine/src/Graphics/Font.cpp`, `engine/src/Core/ImageWrite.cpp` | Native file open/save dialogs (Windows, zenity or kdialog on Linux, osascript on macOS, built-in browser fallback), PNG writer, built-in 8x8 bitmap font |
| Backend functions | `supabase/functions` | Our server-side functions for accounts and community features |

## Libraries we build on (all planned for removal)

| Library | Used for | Licence | How we get it | Replacement and state |
|---|---|---|---|---|
| [SDL2](https://www.libsdl.org) | Windowing, input, audio device, OpenGL context | zlib | System package; `SDL2.dll` is bundled in Windows builds; Emscripten port on the web | `okay::platform` (`docs/platform/platform_layer.md`): the Linux/X11 backend is done and tested under Xvfb; the player's window, events, input and GL context go through the seam; the editor's and launcher's event loops and 2D drawing go through it too, with SDL2 still underneath on Windows (#1198, 2026-11-23, moved five more SDL calls behind the seam and gave the editor's Play keys and the player one shared key mapping); the launcher runs with no SDL on the own backend on Linux only; the Win32 backend compiles but has not run on real Windows (`okay-platform-selftest --ci` is wired into the Windows test workflow); macOS not written |
| [Dear ImGui](https://github.com/ocornut/imgui) | Editor and launcher UI | MIT | Downloaded at configure time, pinned to one commit | `okayui` (`docs/ui/own_ui_toolkit.md`): the editor's Crash Log, Audio Mixer and History are OkayUI only (#1192) and the script editor's code view is our own `OkayUI::CodeEdit` (#1200); other screens (What's New, Tips, Console, the Inspector preview) have OkayUI versions behind the Settings > Experimental switches, which start ticked; everything else is still Dear ImGui; nothing of this has run in a real window on Windows |
| ~~[Assimp](https://github.com/assimp/assimp) v5.4.3~~ | Was: importing 3D model formats beyond the built-in ones | BSD 3-clause | **Deleted 2026-11-24** (`claude/no-assimp-1`): `engine/src/Render/ModelImportAssimp.cpp`, the `OKAY_USE_ASSIMP` and `OKAY_PREFER_OWN_READERS` options, the FetchContent block (and the zlib it built), `-DOKAY_USE_ASSIMP=ON` in `scripts/build-windows-dist.sh`, `AssimpAvailable()` and `ImportBackend::Assimp` | Our own readers are the only model readers, for every extension Assimp read except Nendo `.ndo` (dropped: no public description of the format and no usable sample file; `docs/content/model_readers.md`, section Nendo). A file our reader refuses fails the import with the reason. CI still fetches the Assimp v5.4.3 source tree, as **data only**, for the real-file corpus in its `test/` folder (job `model-corpus`, `docs/content/model_corpus.md`); nothing from it is compiled or shipped |
| [ufbx](https://github.com/ufbx/ufbx) | FBX import | MIT or public domain | **Deleted 2026-11-23** (`claude/third-party-2`): `engine/third_party/ufbx` and every use are gone | Our own FBX reader `okay::fbx` is the only FBX reader (the `OKAY_OWN_FBX` switch is gone too). Before the deletion it agreed with ufbx on 42 of 43 real files (the 43rd is damaged) and all fixtures; those values are now fixed expected numbers in `fbx_reader`. Not run on real Windows after the deletion |
| [libsodium](https://libsodium.org) | Authenticated packet encryption, session tickets, host identity | ISC | System package (optional, `OKAY_USE_SODIUM`); MinGW prebuilt in CI | `okay::crypto`, off, unreviewed; needs an outside review first |
| ~~[ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit)~~ | Was: the script editor's code view (vendored in `editor/vendor/TextEditor.*`) | MIT | **Removed** (2026-11-23, branch `claude/third-party-4`). The script editor's code is our own `OkayUI::CodeEdit` (`docs/ui/own_ui_code_editor.md`); the palette and language types the editor took from its header are ours in `editor/ScriptEditorCodeEdit.hpp` | Done. Not run on Windows yet |
| ~~TweetNaCl (public domain, ported to C++)~~ | Was: Ed25519 and SHA-512 when there is no libsodium and no `OKAY_OWN_CRYPTO` (anti-cheat manifest, launcher) | Public domain | **Removed.** `shared/AntiCheatCrypto.hpp` is now a thin wrapper with no crypto code of its own | Done. Without libsodium (and with `OKAY_OWN_CRYPTO`) `okayac::ed25519` and `okayac::sha512::Hash` use `okay::crypto`, our own code (static library `okay_crypto`); libsodium stays the preferred Ed25519 where it is built. `okay::crypto` has **not** had its outside review yet (`CLAUDE.md` rule 18); the owner approved this replacement. `tests/test_anticheat_golden.cpp` pins what the old port answered (recorded before it was deleted) |
| curl (external program, not linked) | Downloads in the launcher and editor, HTTP calls on Linux and macOS | curl licence | Run as a program from the user's system | **No longer used by any shipped program (`claude/no-curl-1`, 2026-11-25):** our own HTTP/1.1 client `okay::ownhttp` (`docs/platform/own_http.md`) on Linux, with the system OpenSSL loaded at run time for TLS (a system library we call, not linked or shipped); WinHTTP on Windows; no client on macOS. The last curl leftover in shipped code, the sign-in error text that translated curl's exit codes, now translates our own client's errors instead (2026-11-26, `claude/own-code-quick-1`). Still run as a program only by CI and release scripts (`scripts/*.sh`, `.github/workflows/`) and by our own build tool's tarball download (`tools/okaybuild/okay-build.cpp`, not used by CMake, CI or releases) |

## Built into what we ship, but not ours (added 2026-11-26, `provenance_audit.md` A5 to A7)

Found by the 2026-11-26 audit. The "what" and "where" columns were checked in the files named; the licence column is
from memory and is **unverified** (the licence pages could not be reached). **Notice file (2026-11-26):**
`dist/THIRD_PARTY_NOTICES.txt` names every part below plus Dear ImGui, SDL2 and the fonts, with the licence texts
(taken from the Debian copyright files of the build machine's MinGW-w64, GCC, libsodium and SDL2 packages, Dear
ImGui's own LICENSE.txt and the font licence files; the Emscripten and Steamworks lines are still from memory and
marked UNVERIFIED in the file). It ships beside `OkaySpace.exe` and in `Tools\` (`scripts/build-windows-dist.sh`, so
also in the installer), in every exported desktop game (`editor/BuildGame.cpp`) and in every web build
(`scripts/web_package.py`, checked by `tests/scripts/test_web_package.py`).

| What | Where it gets in | Licence (unverified) | Replacement |
|---|---|---|---|
| MinGW-w64 GCC runtime: libgcc and libstdc++ | linked statically into every Windows program (`cmake/mingw-w64-x86_64.toolchain.cmake`, `-static-libgcc -static-libstdc++`) | GPLv3 with the GCC Runtime Library Exception | our own compilers (`tools/okaycc`, `tools/okayms`) and their own runtimes, not yet used for releases (rule 24a) |
| winpthreads (MinGW-w64) | linked statically, whole archive, into every Windows program (same file); its own version resource is taken out of the copy | MIT-style, asks for its notice in copies | none planned yet |
| libsodium | linked **statically** (`libsodium.a`, `SODIUM_STATIC`) into release builds, which must have it (`OKAY_REQUIRE_SODIUM`, `engine/CMakeLists.txt`) | ISC | `okay::crypto`, off until reviewed (rule 18) |
| Emscripten runtime and system libraries, and its SDL2 port | compiled into the web player `okay-player.js` / `.wasm` (`player/CMakeLists.txt`, `-sUSE_SDL=2`; CI uses Emscripten 3.1.51) | Emscripten MIT / NCSA; its C library (musl-based) MIT; libc++ Apache-2.0 with LLVM exception; SDL2 zlib | none planned yet |
| `steam_api64.dll` (Steamworks SDK redistributable) | copied into the Windows package only when `STEAMWORKS_SDK_PATH` is set (`scripts/build-windows-dist.sh`) | Valve's Steamworks SDK terms (proprietary) | none: an optional service hook (rule 19a) |

## Data in our code that came from outside (added 2026-11-26)

| Data | Where | Source as the code or file states it | Licence |
|---|---|---|---|
| Marching-cubes edge and triangle tables | `engine/src/Components/VoxelTerrain.cpp` (`kEdge`, `kTri`) | "Marching cubes tables (Paul Bourke)" (the code's own comment) | not recorded; owner decision pending (`provenance_audit.md` A1) |
| Single-byte encoding tables (the code points of bytes 0x80-0xFF for 27 encodings) and the encoding label list | `engine/src/Browser/Encoding.cpp` (`kHighHalf`, `kLabels`) | the WHATWG Encoding standard (encoding.spec.whatwg.org, its index files and its label table; the standard could not be reached, so the tables were written from the code pages' Unicode mappings, as Python's codecs print them, plus the standard's known differences, and are **not checked against the official index files**) | the standard is CC BY 4.0 (attribution); the Unicode mappings are facts |
| Unicode bidi class L ranges and Arabic joining forms | `engine/src/Core/Localization.cpp` | "worked out from the Unicode Character Database (UCD 14.0)" (the code's comment) | Unicode's data licence; whether a notice is needed is unverified |
| Test audio `tests/data/audio/ogg/`, `mp3/`, `flac/` | test data only, not shipped | encoded by ffmpeg (`Lavf60.16.100` in the files), with LAME 3.100 for MP3 and libvorbis for Ogg (encoder strings in the files); the source signal and the script that made them are not recorded | encoder output; nothing of the encoders is in the repository |
| Brotli static dictionary (122,784 bytes) and the 121 word transforms | `engine/src/Core/BrotliDictionary.inc`, compiled into `okay_brotli` (used by WOFF2 fonts in the browser, launcher and editor) | RFC 7932, Appendix A and Appendix B, Copyright (c) 2016 IETF Trust and the persons identified as the document authors; generated from the RFC's text by `scripts/gen_brotli_dictionary.py`, which checks the lengths and CRC-32 values the RFC states (dictionary 0x5136cb04, transforms 648 bytes / 0x3d965f81); the owner approved using it on 2026-11-26 ("Yes use WOFF2") | Used as an RFC Code Component under the Simplified BSD License (IETF Trust Legal Provisions section 4.e). The licence text is next to the data (`engine/src/Core/BrotliDictionary-LICENSE.txt`) and in the shipped font folder (`assets/fonts/RFC7932-Brotli-dictionary-LICENSE.txt`, copied to `Assets/Fonts/` of the Windows package). The licence text was written from the TLP 4.e wording as remembered, not fetched (trustee.ietf.org could not be reached): check it against the TLP. Whether a data table counts as a "Code Component" is our reading. The decoder code itself is ours (`engine/src/Core/Brotli.cpp`, written from the RFC's text only) |

Fonts are assets, not code, and stay with their licences:

| Font | Where | Licence as the repository states it |
|---|---|---|
| [DejaVu Sans](https://dejavu-fonts.github.io) | `assets/fonts/DejaVuSans.ttf` | Free font licence; the text is in `assets/fonts/DejaVuSans-LICENSE.txt` |
| JetBrains Mono (Regular) | `shared/JetBrainsMonoFont.h` (our own OKF1 container, read through `shared/EmbeddedFonts.hpp` by the editor, launcher and script editor) | SIL Open Font License 1.1, "Copyright 2020 The JetBrains Mono Project Authors". The licence and copyright text is now in `assets/fonts/JetBrainsMono-OFL.txt`, copied unmodified from `https://raw.githubusercontent.com/JetBrains/JetBrainsMono/v2.242/OFL.txt` (git blob 821a3dac22aff15a1f1c9689a1d79c45bb58ca39, fetched 2026-11-13). The embedded font file says "Version 2.211" in its own name table; I could not fetch a `v2.211` tag, and the licence text of the tags I could fetch (v2.242, v2.304, master) is the same apart from the URL line and whitespace. **Still to do:** check that the installer and release archives ship that file next to the font. |
| Roboto Medium | `shared/RobotoFont.h` (our own OKF1 container, read through `shared/EmbeddedFonts.hpp` by the engine, editor and launcher) | **Licence file added 2026-11-26:** `assets/fonts/Roboto-LICENSE.txt` is the Apache License 2.0 text (the same text as `tests/wycheproof/LICENSE`), the licence the font's own name table names, and `dist/THIRD_PARTY_NOTICES.txt` carries it with the font's copyright line. Where the font file came from is still for the owner to confirm. Earlier note: What exists: the font file's own name table (decoded from the embedded data) says "Copyright 2011 Google Inc. All Rights Reserved", "Version 2.001152; 2014" and, in its licence fields (name ids 13 and 14), "Licensed under the Apache License, Version 2.0" with the URL `http://www.apache.org/licenses/LICENSE-2.0`; `editor/main.cpp` and `launcher/main.cpp` carry a comment calling it Apache 2.0; the first comment line of the old base-85 `shared/RobotoFont.h` (replaced by the OKF1 data on 2026-11-26) said it was exported with Dear ImGui's tool from a `misc/fonts/Roboto-Medium.ttf`. Those are leads, not a verified grant: the owner must confirm the source and add the licence text. |

**To resolve (we do not guess a licence):** (1) the owner confirms where Roboto Medium was obtained and under which
licence, then adds that licence text next to it (for example `assets/fonts/Roboto-LICENSE.txt`) and a header line in
`shared/RobotoFont.h` (the generator writes it; edit `scripts/gen_embedded_fonts.py`);
(2) JetBrains Mono: the OFL text and copyright line are now in the repository (see the table); check that the Windows
installer and the release archives carry that file, as the OFL requires the notice to travel with the font;
(3) the same check for whatever licence file Roboto gets. Until (1) is done the licence status of Roboto is unconfirmed.

## Services we keep (optional integrations, not bundled)

- **Steamworks SDK** (`OKAY_WITH_STEAM`): Valve's SDK is proprietary and is not in this repository. Without it, a no-op Steam service is used.
- **Photon Realtime** (`OKAY_WITH_PHOTON`): needs the Photon C++ SDK. It is an extension point only; see [net_providers.md](../online/net_providers.md).

## Tools and services

- **Build and test:** CMake, GCC, MinGW-w64 (Windows cross-compile), Emscripten (WebGL), GitHub Actions.
- **Hosting:** GitHub Releases (the permanent archive of every version) and Supabase (update bucket, accounts and community backend).
- **Sample data files analysed for the batch E readers (MDC, HMP, CSM):** `test/models/MDC/spider.mdc`, `test/models/HMP/terrain.hmp`, `test/models-nonbsd/HMP/terrain_withtexture.hmp` and `test/models/CSM/ThomasFechten.csm` of an Assimp 5.4.3 source checkout (kept outside the repository, in a scratch area). They were **analysed as data only** (read byte by byte to work out the formats and to check the readers) and **are not committed or bundled**. **Their licence is unchecked**: `terrain_withtexture.hmp` comes from the `models-nonbsd` part of that tree (licence unknown, and it embeds a picture), and only `ThomasFechten.csm` carries a note ("free for any purpose"). Rule 19a needs the owner's permission and a record for data like this: this is the record, the permission is still to be given. See `docs/content/model_readers.md`, `own_hmp_reader.md`, `own_csm_reader.md`.
- **Sample models:** none are downloaded or bundled. The old sample-model entries were removed.
- **ffmpeg (as a program, for test fixtures only):** `tests/data/audio/wav_ffmpeg/` holds WAV files that ffmpeg 6.1.1 encoded from **our own synthetic signal**, and ffmpeg's decode of each, for the WAV decoder's differential test (`wav_ffmpeg_diff`; made by `scripts/gen_wav_ffmpeg_fixtures.py`). This is program output on our data: no ffmpeg code is in the repository, none was read, and ffmpeg is not linked or shipped.

If you add a library, add it to this page with its licence and how it is obtained.

Code provenance: [provenance_audit.md](provenance_audit.md) lists what may have been copied or ported from other projects (rule 19a), how serious each case is, and what to rewrite first. Scan tool: `scripts/provenance_scan.py`.

## UI library slice 4: nothing added

The 371 UI icons (`assets/ui/ui_icons.okaydata`) are drawn by our own code from simple shapes; no icon font, image or outside artwork was used.
The themes use only fonts already shipped (the built-in pixel font and DejaVu Sans, see above). The styles, themes and screens are our own data.
No library or SDK was added.

