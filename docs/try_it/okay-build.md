---
id: okay-build
---
## okay-build: our own replacement for CMake (not part of the Windows release; Linux checkout only, off by default)

CMake stays the default build. okay-build is a second way to build the engine that does not need CMake or Ninja. Nothing in the release
uses it. Plan and numbers: [own_build_plan.md](own_build_plan.md), sections "Round 11" and "Round 12".

Needs g++, the SDL2 development headers and about 2 GB of disk. Run from a checkout of the repository.

1. Build the tool and its tests: `sh tools/okaybuild/build.sh /tmp/okb`. You should see `okaybuild tests: N checks passed` (a few checks need
   wine or libsodium and may fail on a machine without them; the failing names are printed).
2. Read the project's CMake files with okay-build itself and see what it can express:
   `/tmp/okb/okay-build cmake . -o build.cmake.okay --audit`.
   You should see `okay-build cmake: 815 of 820 targets written ...` and a table (targets, sources, definitions, tests, what was not understood).
   Options work like `-D`: `--opt OKAY_BUILD_EDITOR=ON --opt OKAY_BUILD_PLAYER=ON`. `--build-type None` gives plain `-O0` flags (Release is the default,
   as in CMake). `--fetched imgui=/folder/with/imgui` says where the Dear ImGui sources already are (okay-build does not download).
3. Build one program and its tests with no CMake: `/tmp/okb/okay-build -f build.cmake.okay -j2 --build-dir /tmp/okb-tree test_math`
   (about 350 steps; delete `/tmp/okb-tree` and `build.cmake.okay` afterwards).
4. Run the project's `add_test` lines like ctest: `/tmp/okb/okay-build -f build.cmake.okay -j2 --build-dir /tmp/okb-tree ctest '^(math|scene|serialize)$'`.
   You should see `PASS math 0.0s` lines and `3 tests, 0 failed, 0 skipped`. The argument is a regular expression on the test name; with none, every test runs.
5. Compare with CMake (needs cmake and ninja): configure with `cmake -G Ninja -B /tmp/cm -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, then
   `okay-build -f build.cmake.okay compdb --build-dir /tmp/okb-tree` and
   `python3 tools/okaybuild/examples/compare_cmake_commands.py /tmp/cm/compile_commands.json compile_commands.json $PWD`.
   You should see `identical settings: N, different: 0`.

6. More `ctest` selection: `-R REGEX` (name), `-E REGEX` (leave out), `-L REGEX` (label), `--junit out/ctest.xml`. Tests keep their `ENVIRONMENT`,
   `LABELS` and `DEPENDS` from the CMake files. Example: `okay-build -f build.cmake.okay ctest -L fast -E slow --junit out/j.xml`.
7. Downloads are a separate, explicit step (a build never downloads): `okay-build -f build.cmake.okay fetch imgui` fetches Dear ImGui at the pinned git
   commit into `_deps/imgui-src` (needs git and network), checks the commit, and drops the history. A tarball block (`url` + `sha256`) is checked before it is unpacked.
   `okay-build tool sha256 FILE` prints our own SHA-256 of a file. After `fetch` the launcher/editor/player build with no `--fetched`.
8. Build the real programs: `okay-build -f build.cmake.okay -j2 --build-dir /tmp/okb-tree okay-player okay-editor okayspace-launcher` (needs the SDL2 headers, X11/GL headers
   are only read). The programs appear as `/tmp/okb-tree/build/okay-player`, `OkayEngine` and `OkaySpace`.
9. Time a no-op rebuild against Ninja: `sh tools/okaybuild/examples/noop_vs_ninja.sh OKAY_BUILD MANIFEST BUILD_DIR NINJA_DIR HEADER TARGET...`.

Unverified: Windows (the reader and `ctest` were only run on Linux; `ctest` is not done on Windows), MSVC.
