# Roadmap: replacing the big libraries with our own code

Goal: everything custom, slowly. The big libraries are replaced in two stages so nothing breaks:

1. **Seam.** Put a thin interface of our own between the engine and the library, so no other code calls the library directly.
2. **Own backend, side by side.** Write our own implementation behind the seam, selectable by a CMake option and a test switch. The library stays the default until ours passes the same tests on every platform CI covers. Then ours becomes the default, and the library is removed one release later.

A step ships only when it is finished and tested. Nothing here is started yet unless it says so.

| Library | Seam | Our backend, in order | Status |
|---|---|---|---|
| Assimp (optional) | `okay::ModelImport` entry point | OBJ and glTF/GLB readers (glTF already exists), then Collada, 3DS, PLY, STL; FBX stays on ufbx until our own FBX reader exists | Not started |
| ufbx | same | Binary FBX reader, then ASCII FBX | Not started |
| SDL2 | `okay::Platform` (window, events, GL context, gamepad, audio device, clipboard, timers) | Win32 first (window, input, WGL/D3D), then X11, then macOS; gamepad and audio device per OS (XInput, evdev/ALSA, CoreAudio) | Not started |
| Dear ImGui | `okay::ui` editor toolkit built on `okayui` | Own immediate-mode core (layout, widgets, text, docking), then port panels one at a time | Not started |
| libsodium | `okay::crypto` wrapper | Last. Never ships without an outside expert review and test vectors; until then libsodium stays | Not started |

Rules:
- Each backend gets tests against reference data, plus fuzzing for anything that parses files.
- Each step is one PR with an honest NOT DONE list.
- Order inside the list: Assimp and ufbx, then SDL2, then ImGui, then libsodium.
