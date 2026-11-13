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
| TweetNaCl (public domain, ported to C++) | Ed25519 and SHA-512 when there is no libsodium (anti-cheat manifest, launcher) | Public domain | Inside `shared/AntiCheatCrypto.hpp` | Replace with `okay::crypto` after the same review |
| curl (external program, not linked) | Downloads in the launcher and editor, HTTP calls on Linux and macOS | curl licence | Run as a program from the user's system | Built-in client on the platform's own TLS API; Windows already has one (WinHTTP) |
| stb `stb_decompress` (public domain, one routine) | Reads the embedded Dear ImGui font arrays in `engine/src/Graphics/EmbeddedFont.cpp` | Public domain | Copied into that file | Goes when the fonts are re-encoded, with Dear ImGui |

Fonts are assets, not code, and stay with their licences:

| Font | Where | Licence as the repository states it |
|---|---|---|
| [DejaVu Sans](https://dejavu-fonts.github.io) | `assets/fonts/DejaVuSans.ttf` | Free font licence; the text is in `assets/fonts/DejaVuSans-LICENSE.txt` |
| JetBrains Mono (Regular) | `shared/JetBrainsMonoFont.h`, embedded for the script editor | The file header says: "(c) 2020 The JetBrains Mono Project Authors. SIL Open Font License 1.1." (source link in that header: github.com/JetBrains/JetBrainsMono). **The repository has no copy of the OFL text or the copyright file for it.** |
| Roboto Medium | `shared/RobotoFont.h`, embedded for the launcher | **The repository states no licence for it.** The only provenance is the file's first comment line, which says it was exported with Dear ImGui's `binary_to_compressed_c` from a `Roboto-Medium.ttf` in a Dear ImGui checkout (`misc/fonts`). No licence file, no copyright line. |

**To resolve (we do not guess a licence):** (1) find out the licence Roboto Medium was obtained under from its upstream
source, add that licence text next to it (for example `assets/fonts/Roboto-LICENSE.txt`) and a header line in
`shared/RobotoFont.h`; (2) add the OFL 1.1 text and the JetBrains Mono copyright notice next to the JetBrains Mono
file, as the OFL requires the notice to travel with the font; (3) check that the Windows installer and the release
archives carry these notices. Until then the licence status of the two embedded fonts is unconfirmed.

## Services we keep (optional integrations, not bundled)

- **Steamworks SDK** (`OKAY_WITH_STEAM`): Valve's SDK is proprietary and is not in this repository. Without it, a no-op Steam service is used.
- **Photon Realtime** (`OKAY_WITH_PHOTON`): needs the Photon C++ SDK. It is an extension point only; see [net_providers.md](net_providers.md).

## Tools and services

- **Build and test:** CMake, GCC, MinGW-w64 (Windows cross-compile), Emscripten (WebGL), GitHub Actions.
- **Hosting:** GitHub Releases (the permanent archive of every version) and Supabase (update bucket, accounts and community backend).
- **Sample models:** none are downloaded or bundled. The old sample-model entries were removed.

If you add a library, add it to this page with its licence and how it is obtained.
