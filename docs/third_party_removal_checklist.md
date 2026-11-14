# Removal checklist for third-party libraries (nothing here is done)

This page lists, for each third-party library that `docs/own_code_roadmap.md` plans to remove, the exact files, CMake
options, CI jobs and release-script lines that would be deleted or edited, in order, and the criteria that must hold
first. It is a plan: **no dependency is deleted and no default is flipped by this page.** The last column is for the
owner's written approval (the PR link or the date) and is empty on purpose; per the roadmap nobody else declares a
replacement ready.

Every path, option and job name below was checked against the tree when this page was written (`grep`, same commit).
Line numbers drift; names are the reference. After each removal, update `docs/own_code_roadmap.md`,
`docs/THIRD_PARTY.md` and run `python3 scripts/gen_feature_map.py`.

General order from the roadmap: flip the default in one approved release with a release note one release ahead, keep the
library compiled and selectable for that release, delete it one release later.

Criteria that apply to every row (roadmap, "Replacement criteria"): (1) a real-file or real-use corpus passes, not only
fixtures we wrote; (2) platform tests pass on Windows and Linux (macOS and the web where the library is used there);
(3) fuzz under ASan/UBSan for parsers, a soak run for anything stateful, no crash and no hang; (4) no known bugs in our
replacement; (5) owner approval written down in the PR.

## 1. Assimp (`OKAY_USE_ASSIMP`)

Extra criteria from the roadmap row: the COLLADA default flip has happened (`OKAY_OWN_COLLADA`), the 3DS reader has
passed its criteria (or the 3DS decision in `docs/release_note_dropped_model_formats.md` is settled), the release note
`docs/release_note_dropped_model_formats.md` was published one release ahead. Own COLLADA needs a real-exporter corpus.

| # | What to delete or change | Where | Owner approval |
|---|---|---|---|
| 1 | Publish `docs/release_note_dropped_model_formats.md` one release ahead | docs / release notes | |
| 2 | Remove the Assimp CI job `assimp-models` ("Model readers with Assimp"), including its cache steps and the `ASSIMP_TAG` lookup | `.github/workflows/ci.yml` | |
| 3 | Remove `-DOKAY_USE_ASSIMP=ON` from the Windows distribution build | `scripts/build-windows-dist.sh` (the `-DOKAY_USE_ASSIMP=ON` line in the configure call) | |
| 4 | Remove `-DOKAY_USE_ASSIMP=OFF` and the Assimp half of the comment | `scripts/build-web.sh` | |
| 5 | Delete the FetchContent block `if(OKAY_USE_ASSIMP) ... endif()` (the `ASSIMP_BUILD_*` settings, `FetchContent_Declare(assimp)`, `target_link_libraries(okay_engine PUBLIC assimp)`, `OKAY_HAVE_ASSIMP`, the `src/Render/ModelImportAssimp.cpp` source). This also removes the zlib Assimp builds | `engine/CMakeLists.txt` | |
| 6 | Delete `engine/src/Render/ModelImportAssimp.cpp` and `engine/src/Render/ModelImportInternal.hpp` (only the Assimp bridge uses it; confirm with grep first) | `engine/src/Render/` | |
| 7 | Remove the `#ifdef OKAY_HAVE_ASSIMP` branches and the `ImportBackend::Assimp` value | `engine/src/Render/ModelImport.cpp`, `engine/include/okay/Render/ModelData.hpp` | |
| 8 | Remove the `#ifdef OKAY_HAVE_ASSIMP` test branches (both backends compared) | `tests/test_model_readers.cpp`, `tests/test_collada_reader.cpp`, comment in `tests/CMakeLists.txt` | |
| 9 | Delete the options `OKAY_USE_ASSIMP` and `OKAY_PREFER_OWN_READERS` (and its `OKAY_PREFER_OWN_READERS_DEFAULT` define); `OKAY_OWN_COLLADA` goes when COLLADA is the only reader and always on | `CMakeLists.txt`, `engine/CMakeLists.txt` | |
| 10 | Update the Assimp rows in `docs/own_code_roadmap.md` and `docs/THIRD_PARTY.md` | docs | |

## 2. ufbx (vendored)

Criteria from the roadmap row: a real-file corpus (files exported by common modelling tools) passes our FBX reader;
`OKAY_OWN_FBX` default flipped; then delete the vendored code and the option one release later. Also open in the roadmap:
PBR and other material property sets, a sanitizer run of the engine-linked `fbx_reader` test, a Windows run.

| # | What to delete or change | Where | Owner approval |
|---|---|---|---|
| 1 | Flip `OKAY_OWN_FBX` to ON in a release the owner approves, with a release note | `CMakeLists.txt` (option) | |
| 2 | One release later: delete the directory `engine/third_party/ufbx` (`ufbx.c`, `ufbx.h`, `LICENSE`) | `engine/third_party/ufbx/` | |
| 3 | Remove `third_party/ufbx/ufbx.c` from the engine sources, the `third_party/ufbx` include directory, and the `set_source_files_properties(third_party/ufbx/ufbx.c ...)` block (MSVC and non-MSVC) | `engine/CMakeLists.txt` | |
| 4 | Remove the `#include "ufbx.h"` path and the `ufbx_*` code (the default FBX reader) from `FbxScene.cpp` and `ModelImport.cpp` | `engine/src/Render/FbxScene.cpp`, `engine/src/Render/ModelImport.cpp` | |
| 5 | Remove the `OKAY_OWN_FBX` option and the `OKAY_OWN_FBX_DEFAULT` define; the run-time `OKAY_OWN_FBX` environment switch in `FbxScene.cpp` and `FbxReader.hpp` | `CMakeLists.txt`, `engine/CMakeLists.txt`, `engine/src/Render/FbxScene.cpp`, `engine/include/okay/Render/FbxReader.hpp` | |
| 6 | Drop the "side by side with ufbx" comparisons from the tests | `tests/test_fbx_reader.cpp`, `tests/test_fbx_import_limits.cpp`, `tests/CMakeLists.txt` | |
| 7 | Drop `C` from `project(... LANGUAGES C CXX)` only if no other C file remains (comment says it is for vendored single-file C libraries, ufbx) | `CMakeLists.txt` | |
| 8 | CI and release lines: none specific (no job or script names ufbx; it is compiled inside every build) | n/a | |
| 9 | Update the ufbx rows in `docs/own_code_roadmap.md` and `docs/THIRD_PARTY.md` | docs | |

## 3. libsodium (`OKAY_USE_SODIUM`)

Criteria from the roadmap row: **cannot be replaced on our word.** Flip only after an outside cryptographic review of
`okay::crypto` has passed and its findings are fixed (`docs/crypto_review_package.md`). Also open: Windows and macOS runs
and timing measurements on other CPUs, a real libFuzzer run, owner approval. Done last, together with TweetNaCl.

| # | What to delete or change | Where | Owner approval |
|---|---|---|---|
| 1 | Flip `OKAY_OWN_CRYPTO` to ON by default (after the outside review), with a release note | `CMakeLists.txt` | |
| 2 | One release later: delete the `elseif(OKAY_USE_SODIUM)` branch (the `OKAY_SODIUM_PREFIX` handling, pkg-config lookup, `OKAY_HAVE_SODIUM`) | `engine/CMakeLists.txt` | |
| 3 | Delete the options `OKAY_USE_SODIUM` and the `OKAY_SODIUM_PREFIX` plumbing | `CMakeLists.txt`, `engine/CMakeLists.txt`, `scripts/build-windows-dist.sh` (the `OKAY_SODIUM_MINGW_PREFIX` lines) | |
| 4 | Remove `-DOKAY_USE_SODIUM=OFF` from every configure line | `.github/workflows/ci.yml` (jobs `sanitizers`, `own-crypto`, `crypto-sanitizers`, `assimp-models` and `windows-restart`), `scripts/build-web.sh`, `deploy/voice-server/install.sh` | |
| 5 | Remove the libsodium MinGW download: the "Fetch SDL2 and libsodium (MinGW)" step (keep the SDL2 part while SDL2 exists) and `-DOKAY_SODIUM_PREFIX` in the `windows-cross` job | `.github/workflows/ci.yml` | |
| 6 | Remove the step "Fetch libsodium (MinGW prebuilt)" in job `windows-exe`, and `libsodium-dev` from the `linux-server` apt install | `.github/workflows/release.yml` | |
| 7 | Remove the libsodium fetch and the `pkg-config --exists sdl2 libsodium` check and notes | `scripts/release-local.sh` | |
| 8 | Remove the libsodium side of the crypto backend comparison | `scripts/test_crypto_backends.sh` (the `sodium own` loop) | |
| 9 | Remove the libsodium cross-check targets: `test_crypto_sodium_xcheck`, `bench_crypto` sodium rows, `crypto_timing` sodium rows, `test_net_crypto_sodium` | `tests/CMakeLists.txt` and the matching `tests/test_crypto_sodium_xcheck.cpp`, `tests/test_net_crypto_sodium.cpp` | |
| 10 | Remove the `OKAY_HAVE_SODIUM` backend from the packet-encryption seam | `engine/include/okay/Net/NetCrypto.hpp` (header only) | |
| 11 | Remove the `OKAY_HAVE_SODIUM` branches | `shared/AntiCheatCrypto.hpp` | |
| 12 | Update the libsodium rows in `docs/own_code_roadmap.md`, `docs/THIRD_PARTY.md`, `docs/crypto.md` | docs | |

## 4. TweetNaCl-derived Ed25519 and SHA-512 (the `portable::` port)

Criteria from the roadmap row: delete the port once the outside review of `okay::crypto` has passed (same review as
libsodium), so only one Ed25519 exists in the tree. "Not deleted yet, by decision."

| # | What to delete or change | Where | Owner approval |
|---|---|---|---|
| 1 | Delete the `namespace portable { ... }` port and its SHA-512 | `shared/AntiCheatCrypto.hpp` | |
| 2 | Make `OKAY_OWN_CRYPTO` unconditional in that header (the launcher links `okay_crypto`) | `shared/AntiCheatCrypto.hpp`, `launcher/CMakeLists.txt` | |
| 3 | Delete the test target `test_anticheat_integrity_portable` (and its `add_test`); `test_anticheat_integrity_ownlib` and `test_anticheat_integrity` stay; remove the `portable::` reference comparisons inside | `tests/CMakeLists.txt`, `tests/test_anticheat_integrity.cpp` | |
| 4 | CI: the `own-crypto` job builds `test_anticheat_integrity_portable`; remove it from that job's target and test lists | `.github/workflows/ci.yml` | |
| 5 | Update the TweetNaCl rows in the roadmap and `docs/THIRD_PARTY.md` | docs | |

## 5. stb (already gone from the code; only leftovers)

Criteria from the roadmap: none outstanding; stb_image, stb_image_write, stb_truetype, stb_vorbis and the font
decompressor are already replaced and deleted. The directory `engine/third_party/stb` does **not** exist. What is left
is housekeeping:

| # | What to delete or change | Where | Owner approval |
|---|---|---|---|
| 1 | Remove the include directory `third_party/stb` (the directory is absent) and update the comment that names stb_truetype, stb_vorbis and dr_libs | `engine/CMakeLists.txt` (the `target_include_directories(okay_engine PRIVATE ...)` block) | |
| 2 | Reword the `-Wno-narrowing` comment that mentions "vendored tables (stb)" if no vendored single-header library remains | `engine/CMakeLists.txt` | |
| 3 | History comments only (leave or reword): `shared/OkfDecode.hpp` (the stb_compress reader it replaced) and `tests/data/images/manifest.txt` (stb_image convention cross-check) | those files | |
| 4 | Remove "stb" from the "already replaced" lines | `docs/own_code_roadmap.md`, `docs/THIRD_PARTY.md` | |

## 6. SDL2

Criteria from the roadmap row: own backend as default per platform, then delete the SDL2 dependency, the CI and release
downloads and the bundled `SDL2.dll`. Open: Windows backend tested on real Windows; macOS backend; web path has no own
backend; editor and launcher off direct SDL calls; own 2D painter ready for the editor and launcher; Dear ImGui removal
(its platform backend uses SDL); owner approval. Depends on the Dear ImGui row, so this is the last window-system step.

| # | What to delete or change | Where | Owner approval |
|---|---|---|---|
| 1 | Flip `OKAY_PLATFORM_BACKEND` default from `sdl2` to `own` per platform, with a release note | `cmake/OkayPlatform.cmake` | |
| 2 | One release later: delete the SDL2 find and link logic (`find_package(SDL2)`, `OKAY_PLATFORM_HAVE_SDL2`), the `OKAY_PLATFORM_BACKEND` cache option | `cmake/OkayPlatform.cmake` | |
| 3 | Delete `src/Platform/SdlPainter.cpp` and its `OKAY_PLATFORM_SDL_INCLUDES` / `OKAY_PLATFORM_SDL_OPTIONS` handling | `engine/CMakeLists.txt`, `engine/src/Platform/SdlPainter.cpp` | |
| 4 | Remove the SDL2 link variants: option `OKAY_EDITOR_STATIC_SDL` (`SDL2::SDL2-static`) | `editor/CMakeLists.txt`, `launcher/CMakeLists.txt`, `player/CMakeLists.txt`, `cmake/OkayPlatform.cmake` | |
| 5 | Remove the "Ship SDL2.dll beside the binaries" block and `OKAY_STATIC_SDL` | `CMakeLists.txt`, `cmake/mingw-w64-x86_64.toolchain.cmake` | |
| 6 | Remove `SDL2_MINGW_PREFIX`, `SDL2.dll` checks, the `-DCMAKE_PREFIX_PATH`/`-DCMAKE_FIND_ROOT_PATH` SDL2 entries and the copies of `SDL2.dll` into the output | `scripts/build-windows-dist.sh` | |
| 7 | Remove the SDL2 MinGW fetch (`SDL2_VERSION`, "Fetch SDL2 and libsodium (MinGW)" SDL2 part), the SDL2 prefix in the configure of job `windows-cross` and the `SDL2.dll` copy into `win-tests/` | `.github/workflows/ci.yml` | |
| 8 | Remove `SDL2_VERSION`, the step "Fetch SDL2 (MinGW dev)", `libsdl2-dev` in the `linux-server` apt install, and `build-win-dist/SDL2.dll` from the uploaded `files:` list of job `windows-exe` | `.github/workflows/release.yml` | |
| 9 | Remove the SDL2 fetch (`SDL2_VERSION`, `$CACHE/sdl2`), `SDL2.dll` in the file checks and archive list, `pkg-config --exists sdl2` | `scripts/release-local.sh` | |
| 10 | Remove `Delete "$INSTDIR\SDL2.dll"` and `Delete "$INSTDIR\Tools\SDL2.dll"` the uninstall section of the installer | `scripts/okayspace-installer.nsi` | |
| 11 | Remove `libsdl2-dev` from the apt installs of every Linux job | `.github/workflows/ci.yml` (jobs `build-and-test`, `player`, `editor`, `export-check`, `sanitizers`, `own-crypto`, `crypto-sanitizers`, `assimp-models`) | |
| 12 | Remove the SDL2 text in the option descriptions ("needs SDL2") | `CMakeLists.txt` (`OKAY_BUILD_EDITOR`, `OKAY_BUILD_PLAYER`, `OKAY_BUILD_OKAYUI_TESTS`) | |
| 13 | Update the SDL2 rows in the roadmap and `docs/THIRD_PARTY.md`; the editor and launcher text in `docs/platform_layer.md` | docs | |
