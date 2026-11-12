# Roadmap: replacing the big libraries with our own code

Goal: everything custom, slowly, except services we support on purpose (Steamworks, Photon). The big libraries are replaced in two stages so nothing breaks:

1. **Seam.** Put a thin interface of our own between the engine and the library, so no other code calls the library directly.
2. **Own backend, side by side.** Write our own implementation behind the seam, selectable by a CMake option and a test switch. The library stays the default until ours passes the same tests on every platform CI covers. Then ours becomes the default, and the library is removed one release later.

A step ships only when it is finished and tested. Nothing here is started yet unless it says so.

| Library | Seam | Our backend, in order | Status |
|---|---|---|---|
| Assimp (optional) | `ImportModelData` and `ImportedModel` (all Assimp code in one file) | glTF/GLB, OBJ and MTL, STL and PLY readers are ours; a Collada reader is ours too, side by side with Assimp and off by default (`OKAY_OWN_COLLADA`, reads skins and per-component or matrix animation with LINEAR and STEP keys; tested on our own fixtures only, not on real exporter files); a 3DS reader is ours too, side by side and off by default (`OKAY_OWN_3DS`, not ready; objects, materials, textures, smoothing groups and the object matrix, no keyframes; tested on our own fixtures only); Blender and about fifteen other formats still need Assimp | Seam and first readers done; Collada and 3DS side by side, off by default, not ready |
| ufbx | same | Our own binary and ASCII FBX reader exists behind `OKAY_OWN_FBX` (off); not promoted, because it was only tested on fixtures our own script wrote, not on real Mixamo or Blender files | Side by side, merged, off by default |
| SDL2 | `okay::platform` (timers, audio devices, controllers, windows, events, GL context, clipboard) | Own Linux/X11 backend tested under Xvfb; Win32 compiles but is untested; macOS not written; the player's window, events, input and GL context now go through it (stage 2, `docs/platform_layer.md`); the editor and launcher windows still call SDL directly, and every app still draws through `SDL_Renderer` (they need our own 2D painter and an own ImGui platform backend first), so `own` cannot run an app yet | Seam and Linux backend done; stage 2 (player) done; editor, launcher and the 2D painter next |
| Dear ImGui | `okayui` (our existing own toolkit) is the replacement | Give OkayUI a headless software renderer and golden/input test harness first (in progress), then close the gaps the ImGui usage audit lists (docking, tables, tabs, plots, fonts, GPU backends), then port editor and launcher panels one at a time. Harness merged; gap step 1 (public draw list, cursor and measurement, item state, disabled blocks) built and tested headless, with the HAVE and GAP table by feature family in `docs/own_ui_toolkit.md` section 4; the editor and launcher are untouched and still on Dear ImGui | Started, non-default |
| libsodium | `okay::crypto` wrapper (`NetCrypto.hpp`) | Side by side: ChaCha20-Poly1305, BLAKE2b, X25519, Ed25519, checked against the published RFC and libsodium test vectors, constant-time by construction, fuzzed. It is not the default, and nothing switches over, until an outside expert has reviewed it | Primitives done, unreviewed, off by default (in review) |
| Steamworks, Photon | `okay::net` and Steam service interfaces (exist) | Not replaced. These are services our users want to publish on and connect through, so we keep supporting them as optional integrations, and keep improving the interfaces. Our own relay and matchmaking sit next to them as an option, not instead | Supported |

Rules:
- Each backend gets tests against reference data, plus fuzzing for anything that parses files.
- Each step is one PR with an honest NOT DONE list.
- Order inside the list: Assimp and ufbx, then SDL2, then ImGui, then libsodium.
- "Hard" does not mean "never". It means our version starts out side by side and non-default, and becomes the default only after it has earned it: matching tests, fuzzing, and for crypto an outside review. The library stays until then.

## Small libraries (done or in review)

| Library | Status |
|---|---|
| tinyfiledialogs, stb_image_write, font8x8, dr_mp3, stb_vorbis, stb_image, stb_truetype, Lua, Mono | Replaced or removed, merged |

Update this page and `docs/THIRD_PARTY.md` whenever one of these merges.
