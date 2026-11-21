---
id: okay-build
---
## okay-build: our own replacement for CMake (not part of the Windows release; Linux checkout only, off by default)

CMake stays the default build. okay-build is a second way to build the engine that does not need CMake or Ninja. Nothing in the release
uses it. Plan and numbers: [own_build_plan.md](platform/own_build_plan.md), section "Round 11".

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

Unverified: Windows (the reader and `ctest` were only run on Linux; `ctest` is not done on Windows), MSVC.
