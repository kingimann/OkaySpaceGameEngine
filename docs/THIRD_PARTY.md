# What is custom and what we build on

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
risk, so it needs an outside review before it can be the default; see [crypto_review_package.md](crypto_review_package.md).
Services run by other companies (Steamworks, Photon, our hosting) are not libraries and stay.

## Written by us (custom)

| Area | Where | What it is |
|---|---|---|
| Engine core | `engine/` | Scene and GameObject model, components, serialization, property registry, save games, localization, time and loop, object pooling |
| Rendering | `engine/src/Render`, `engine/include/okay/Render` | Our own software renderer plus OpenGL, Direct3D 11 and Direct3D 12 backends, camera stack, lighting, post-processing |
| Physics | `engine/` | 2D and 3D physics, joints, ragdolls, queries, fixed-step simulation |
| Animation and IK | `engine/src/Animation` | Animators, state machines, humanoid rig mapping, root motion, foot, hand and full-body IK |
| Audio | `engine/src/Audio` | Mixer, groups, music player, one-shots, and our own MP3 and Ogg Vorbis decoders |
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
| [ImGuiColorTextEdit](https://github.com/BalazsJako/ImGuiColorTextEdit) | The script editor's code view | MIT | Vendored copy in `editor/vendor/TextEditor.*` | OkayUI code-editor widget; not started |
| [Assimp](https://github.com/assimp/assimp) v5.4.3 | Importing 3D model formats beyond the built-in ones | BSD 3-clause | Downloaded at configure time (optional, `OKAY_USE_ASSIMP`; on in the Windows distribution) | glTF/GLB, OBJ+MTL, STL, PLY readers are ours; COLLADA, 3DS and BVH readers ours, off by default, run on real exporter files (`docs/model_corpus.md`); the rarer formats are dropped at removal (list in the roadmap); owner approval and a release note are still needed |
| [ufbx](https://github.com/ufbx/ufbx) | FBX import | MIT or public domain | Vendored in `engine/third_party/ufbx` | Own FBX reader behind `OKAY_OWN_FBX`, off; checked against ufbx on 43 real files (`docs/model_corpus.md`), five bugs fixed; owner approval still needed |
| [libsodium](https://libsodium.org) | Authenticated packet encryption, session tickets, host identity | ISC | System package (optional, `OKAY_USE_SODIUM`); MinGW prebuilt in CI | `okay::crypto`, off, unreviewed; needs an outside review first |
| TweetNaCl (public domain, ported to C++) | Ed25519 and SHA-512 when there is no libsodium and no `OKAY_OWN_CRYPTO` (anti-cheat manifest, launcher) | Public domain | Inside `shared/AntiCheatCrypto.hpp` | With `OKAY_OWN_CRYPTO=ON` the whole of `okayac::ed25519` and `okayac::sha512::Hash` already go to `okay::crypto`, which is now a separate static library (`okay_crypto`) the launcher can link without the engine. The port stays (not deleted) until the outside review of `okay::crypto` has passed |
| curl (external program, not linked) | Downloads in the launcher and editor, HTTP calls on Linux and macOS | curl licence | Run as a program from the user's system | Built-in client on the platform's own TLS API; Windows already has one (WinHTTP) |
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
- **Photon Realtime** (`OKAY_WITH_PHOTON`): needs the Photon C++ SDK. It is an extension point only; see [net_providers.md](net_providers.md).

## Tools and services

- **Build and test:** CMake, GCC, MinGW-w64 (Windows cross-compile), Emscripten (WebGL), GitHub Actions.
- **Hosting:** GitHub Releases (the permanent archive of every version) and Supabase (update bucket, accounts and community backend).
- **Sample models:** none are downloaded or bundled. The old sample-model entries were removed.

If you add a library, add it to this page with its licence and how it is obtained.
