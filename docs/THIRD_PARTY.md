# What is custom and what we build on

OkaySpace is mostly our own code. This page lists what we wrote, and the outside libraries and
services we build on, with their licences. The engine itself is MIT licensed (see `LICENSE`).

## Policy and goal

The goal is for everything to be our own code, slowly, one piece at a time. Until a piece is replaced, an
outside library is allowed only when writing our own is impossible or would be unsafe, and each one below
says how replaceable it is. Prefer replacing a library with our own code over adding a new one.

Order of replacement: the small, easy libraries first, then larger ones. The largest and riskiest are
left for last and need their own plan: the platform layer (SDL2), the editor interface toolkit (Dear
ImGui), and encryption (libsodium). For encryption, writing our own is a security risk, so that one needs
an expert review plan before any work starts. Services run by other companies (Steamworks, Photon, our
hosting) are not libraries and stay outside the goal unless we choose to self-host.

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

## Libraries we build on

| Library | Used for | Licence | How we get it |
|---|---|---|---|
| [SDL2](https://www.libsdl.org) | Windowing, input, audio device, OpenGL context | zlib | System package; `SDL2.dll` is bundled in Windows builds |
| [Dear ImGui](https://github.com/ocornut/imgui) | Editor and launcher UI | MIT | Downloaded at configure time, pinned to one commit |
| [Assimp](https://github.com/assimp/assimp) v5.4.3 | Importing many 3D model formats | BSD 3-clause | Downloaded at configure time (optional, `OKAY_USE_ASSIMP`). We are replacing it one format at a time: STL, PLY and OBJ readers are ours; a Collada (`.dae`) reader of ours exists side by side with it and is off by default (`OKAY_OWN_COLLADA`); the rest still need Assimp |
| [ufbx](https://github.com/ufbx/ufbx) | FBX import | MIT or public domain | Vendored in `engine/third_party/ufbx` |
| [DejaVu Sans](https://dejavu-fonts.github.io) | Default UI font | Free font licence (see `assets/fonts/DejaVuSans-LICENSE.txt`) | In `assets/fonts` |
| [libsodium](https://libsodium.org) | Authenticated packet encryption | ISC | System package (optional, `OKAY_USE_SODIUM`) |

## Optional integrations (not bundled)

- **Steamworks SDK** (`OKAY_WITH_STEAM`): Valve's SDK is proprietary and is not in this repository. Without it, a no-op Steam service is used.
- **Photon Realtime** (`OKAY_WITH_PHOTON`): needs the Photon C++ SDK. It is an extension point only; see [net_providers.md](net_providers.md).

## Tools and services

- **Build and test:** CMake, GCC, MinGW-w64 (Windows cross-compile), Emscripten (WebGL), GitHub Actions.
- **Hosting:** GitHub Releases (the permanent archive of every version) and Supabase (update bucket, accounts and community backend).
- **Sample models:** none are downloaded or bundled. The old Mixamo sample entries were removed.

If you add a library, add it to this page with its licence and how it is obtained.
