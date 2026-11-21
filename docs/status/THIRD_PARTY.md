# What is custom and what we build on

> **What this is:** OkaySpace is mostly our own code. This page lists what we wrote, and the outside libraries and services we build on, with their licences.
>
> **Status:** Reference page for this feature. Kept as written; not extended (rule 5).
>
> **Last checked:** 2026-11-20 (moved into `status/`, paths and links checked; the text itself was not re-checked against the code).

OkaySpace is mostly our own code. This page lists what we wrote, and the outside libraries and
services we build on, with their licences. The engine itself is proprietary (see `LICENSE`); third-party components keep their own licences.

## Policy and goal

The goal is for everything to be our own code, and for every third-party library below to be **removed**, slowly,
one piece at a time. Our replacements are not a permanent second option: each stays off until the owner declares it
ready and bug free (real-file corpus, tests on Windows and Linux, soak or fuzz, no known bugs, owner approval), then
the default flips in a release the owner approves, and the library code and its CMake option are deleted one release
later. The per-library plan, state and blockers are in [own_code_roadmap.md](own_code_roadmap.md). Until a library is
removed it is allowed only because the replacement is not ready, and a new library must not be added when writing our
own is possible; add one only with the same removal plan.

Order: the small, easy libraries first (done), then model import (Assimp and ufbx), then the platform layer (SDL2),
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
| [SDL2](https://www.libsdl.org) | Windowing, input, audio device, OpenGL context | zlib | System package; `SDL2.dll` is bundled in Windows builds; Emscripten port on the web | `okay::platform`: Linux/X11 done, player uses it; Win32 untested, macOS not written; editor and launcher still on SDL2 |
| [Dear ImGui](https://github.com/ocornut/imgui) | Editor and launcher UI | MIT | Downloaded at configure time, pinned to one commit | `okayui`: headless harness and gap step 1 done; editor and launcher not ported |
| [ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit) | The script editor's code view | MIT | Vendored copy in `editor/vendor/TextEditor.*` | `OkayUI::CodeEdit` (`docs/ui/own_ui_code_editor.md`): built and tested headless, wired into the script editor behind the developer-only OkayUI backend, never run in a real window; the vendored widget is still the default |
| [Assimp](https://github.com/assimp/assimp) v5.4.3 | Importing 3D model formats beyond the built-in ones | BSD 3-clause | Downloaded at configure time (optional, `OKAY_USE_ASSIMP`; on in the Windows distribution) | glTF/GLB, OBJ+MTL, STL, PLY readers are ours (the Windows distribution still reads STL and PLY through Assimp first); COLLADA, 3DS and BVH readers ours, off by default, run on real exporter files (`docs/content/model_corpus.md`); of the 45 Assimp importers our Windows build switches on, **39 now have an own reader** (all off by default except the always-on glTF, OBJ, STL and PLY; counted from the import table in `ModelImport.cpp`, 2026-11-15); 6 are still without one (OpenGEX, MDL, Q3BSP, SIB, Nendo, glTF 1.0) plus the binary half of OGRE; OpenGEX and MDL (Quake 1 and Half-Life 1) exist on the unmerged branch `claude/readers-e4`; list, difficulty and order in `docs/content/own_readers_remaining.md`; which flips, in what order and what blocks deleting Assimp: `docs/status/own_flip_proposal.md`; owner approval is still needed |
| [ufbx](https://github.com/ufbx/ufbx) | FBX import | MIT or public domain | Vendored in `engine/third_party/ufbx` | Own FBX reader behind `OKAY_OWN_FBX`, off; checked against ufbx on 43 real files (`docs/content/model_corpus.md`), eight bugs fixed; speed and memory never measured; owner approval still needed (`docs/status/own_flip_proposal.md`) |
| [libsodium](https://libsodium.org) | Authenticated packet encryption, session tickets, host identity | ISC | System package (optional, `OKAY_USE_SODIUM`); MinGW prebuilt in CI | `okay::crypto`, off, unreviewed; needs an outside review first |
| ~~TweetNaCl (public domain, ported to C++)~~ | Was: Ed25519 and SHA-512 when there is no libsodium and no `OKAY_OWN_CRYPTO` (anti-cheat manifest, launcher) | Public domain | **Removed.** `shared/AntiCheatCrypto.hpp` is now a thin wrapper with no crypto code of its own | Done. Without libsodium (and with `OKAY_OWN_CRYPTO`) `okayac::ed25519` and `okayac::sha512::Hash` use `okay::crypto`, our own code (static library `okay_crypto`); libsodium stays the preferred Ed25519 where it is built. `okay::crypto` has **not** had its outside review yet (`CLAUDE.md` rule 18); the owner approved this replacement. `tests/test_anticheat_golden.cpp` pins what the old port answered (recorded before it was deleted) |
| curl (external program, not linked) | Downloads in the launcher and editor, HTTP calls on Linux and macOS | curl licence | Run as a program from the user's system | Own HTTP/1.1 client `okay::ownhttp` (`docs/platform/own_http.md`) behind `OKAY_OWN_HTTP`, off, with the system OpenSSL loaded at run time for TLS (OpenSSL is a system library we call, not linked or shipped); curl stays the default and the fallback; Windows already has WinHTTP; macOS has no TLS path yet |
| stb `stb_decompress` (public domain, one routine) | Used to read the embedded Dear ImGui font arrays in `engine/src/Graphics/EmbeddedFont.cpp` | Public domain | **Removed from the engine.** The engine now reads its own OKF1 container (`shared/OkfDecode.hpp`, data in `shared/RobotoFontOwn.h`, generator `scripts/gen_embedded_fonts.py`). The old base85 arrays stay in `shared/RobotoFont.h` and `shared/JetBrainsMonoFont.h` only because the editor and launcher still load them through Dear ImGui's own loader | The two legacy headers go with Dear ImGui (the editor and launcher can instead decode OKF1 and call `AddFontFromMemoryTTF`; not done, because it cannot be built and run here) |

Fonts are assets, not code, and stay with their licences:

| Font | Where | Licence as the repository states it |
|---|---|---|
| [DejaVu Sans](https://dejavu-fonts.github.io) | `assets/fonts/DejaVuSans.ttf` | Free font licence; the text is in `assets/fonts/DejaVuSans-LICENSE.txt` |
| JetBrains Mono (Regular) | `shared/JetBrainsMonoFont.h` (Dear ImGui format, editor and launcher) and `shared/JetBrainsMonoFontOwn.h` (OKF1, engine side); embedded for the script editor | SIL Open Font License 1.1, "Copyright 2020 The JetBrains Mono Project Authors". The licence and copyright text is now in `assets/fonts/JetBrainsMono-OFL.txt`, copied unmodified from `https://raw.githubusercontent.com/JetBrains/JetBrainsMono/v2.242/OFL.txt` (git blob 821a3dac22aff15a1f1c9689a1d79c45bb58ca39, fetched 2026-11-13). The embedded font file says "Version 2.211" in its own name table; I could not fetch a `v2.211` tag, and the licence text of the tags I could fetch (v2.242, v2.304, master) is the same apart from the URL line and whitespace. **Still to do:** check that the installer and release archives ship that file next to the font. |
| Roboto Medium | `shared/RobotoFont.h` (Dear ImGui format, editor and launcher) and `shared/RobotoFontOwn.h` (OKF1, engine side), embedded for the launcher | **The repository still has no licence file for it, and nothing was added here.** What exists: the font file's own name table (decoded from the embedded data) says "Copyright 2011 Google Inc. All Rights Reserved", "Version 2.001152; 2014" and, in its licence fields (name ids 13 and 14), "Licensed under the Apache License, Version 2.0" with the URL `http://www.apache.org/licenses/LICENSE-2.0`; `editor/main.cpp` and `launcher/main.cpp` carry a comment calling it Apache 2.0; the first comment line of `shared/RobotoFont.h` says it was exported with Dear ImGui's tool from a `misc/fonts/Roboto-Medium.ttf`. Those are leads, not a verified grant: the owner must confirm the source and add the licence text. |

**To resolve (we do not guess a licence):** (1) the owner confirms where Roboto Medium was obtained and under which
licence, then adds that licence text next to it (for example `assets/fonts/Roboto-LICENSE.txt`) and a header line in
`shared/RobotoFont.h` and `shared/RobotoFontOwn.h` (the generator writes the latter; edit `scripts/gen_embedded_fonts.py`);
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

