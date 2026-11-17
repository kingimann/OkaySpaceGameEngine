# Own build tool (okay-build): internal plan

Internal note, not a guide. Owner, 2026-11-17: start, do not replace CMake tomorrow. OFF: not in CMake, CI or releases.
Written from general knowledge of build systems (action graph, compiler depfiles, content hashes, worker pool).
No code from CMake, Ninja, Make, Meson or Bazel was read or used (rule 19a).

## Stage A (done, prototype): `tools/okaybuild/`
- `okay-build.cpp` (one file, ~350 lines, C++17, POSIX fork/exec). `build.sh` builds it and runs `test_okaybuild.cpp`.
- Manifest `build.okay`: `toolchain [gcc|clang|mingw]` block (cc, cxx, ar, flags, link_flags, suffixes), `target NAME exe|lib`
  block (sources, include, export_include, define, flags, link_flags, libs, deps), `outdir = DIR`.
- Commands: `build` (default), `clean`, `compdb`; options `-f FILE`, `-j N`, `--dry-run`, `--explain`, `-v`.
- Graph: compile -> archive/link actions; link order is computed (dependents before dependencies); target cycles reported.
- Up to date = output exists AND stored hash of (command line) AND hashes of source + every header the compiler listed
  (`-MMD -MF`, parsed by our own make-syntax reader) all match `.okaybuild/state`. Touching without editing rebuilds nothing;
  a recompiled object with identical bytes does not relink.
- Stage B additions (2026-11-17): stat shortcut (size+mtime trusted only if the file was already 2 s older than the moment
  it was recorded; `--no-stat` turns it off; `-v` prints hashed/trusted counts), response files for long link/archive lines
  (`rsp_threshold` in the toolchain block, default 30000 chars), quoted words so paths may contain spaces, `--build-dir DIR`
  (state, objects and outputs go there; manifest stays in the source dir), raw `action` blocks and `okay-build import`.
- Still not done: Windows host (fork/exec only), FNV-1a 64-bit hash (fine for change detection, not cryptographic), no pools,
  no restat, no generator steps, no `-k`; structured targets use -MMD (system headers not tracked), imported ones keep CMake's -MD.

## What Ninja/Make do that okay-build does not yet
Pools (limit link jobs), `restat` for generators, generator rules (custom commands with declared outputs), `-k` keep going,
`-t` tools (graph, targets, compdb of any rule), console pool, response files for huge command lines, jobserver, regeneration
of the build file when it changes, Windows process handling, very fast no-op checks on huge trees (Ninja ~8 ms vs ours ~27 ms
on 51 files; the gap is hashing and process start, and grows with tree size).

## Stage B: CMake importer (done as prototype)
`okay-build import <cmake build dir> [--run-dir DIR] [-o build.okay] [ninja targets/objects...]` runs `ninja -t commands` in an
already configured build dir (simplest robust source: every compile, archive, link and custom command, exact text, in dependency
order, with CMake's -MD -MF depfile flags). compile_commands.json is not enough alone (no link steps, no depfile flag), so it is
only a cross-check: the compile count matched (1284 = 1284). Each command becomes an `action` block (`cmd` is run verbatim by
`sh -c "cd DIR && CMD"`); edges are inferred from inputs/outputs (objects -> archive -> link); compiles wait for any custom step
that precedes them (`after`). `--run-dir` builds into another directory so Ninja's own tree is untouched (output paths in CMake
commands are relative). Result on the engine configure (Debug -O0, tests ON, editor/launcher OFF): all 2078 commands imported
(1284 compile, 7 archive, 787 link, 0 custom); dry run of the whole graph 0.14 s.
Real slice: the 53 `src/Core` objects of okay_engine + libokay_crypto.a (62 compiles + 1 archive), -j4, 4 cores:
Ninja 80.3 s, okay-build 87.8 s; no-op Ninja 35 ms, okay-build 82 ms (660 inputs trusted by stat, 9 hashed); all 53 Core objects
and the archive byte-identical to Ninja's; header edit (AssetManager.hpp): both rebuild 3 steps, 8.5 s vs 8.6 s; touch only: Ninja
rebuilds 3, okay-build 0. Where it fails / is weak on real commands: nothing broke on this slice, but untested: a real link of an
engine exe (needs the full 330-file engine lib), PCH (none in this configure), generated files (no custom steps in this
configure; order-only edges are not in `ninja -t commands`, so only the conservative `after` rule protects us), MinGW/Windows
configures, multi-config generators, `cmake -E` helper steps (need cmake installed), depfiles with unusual escapes. Imported
commands are CMake's, so include order and flags are exact by construction; the weak point is edge inference by file name.

## Stage C (design only): manifests from modules
Per-module manifests (the idea on branch `claude/modular-1`, `engine/modules/*/module.okaymodule`) would list sources,
public include dirs, defines and deps. A generator step turns them into `build.okay`. Not depending on that branch yet.

## Stage D (design only): CMake option logic
Today CMake decides: which backends (SDL/own), platform, MinGW cross, crypto (libsodium default), tests, packaging. Replace by a
`config.okay` of named options with typed values, `if` conditions on options/platform in the manifest, and a printed config
summary. Last step, only after B and C work and the owner agrees (rule 19).

## Honest effort estimate to replace CMake and Ninja for this repo
Ninja role: Stage A is about 70% of it; pools, restat, Windows host and speed work are about 1-2 weeks.
CMake role: import (B) about 1 week for compile/link of the engine; the hard part is what CMake also does here: ctest tests,
install/package steps, shader and asset generation, third-party fetch/config, MinGW cross toolchain quirks, WebGL/Emscripten
job. Realistic total 6-10 weeks of agent time to match the current CI matrix, and CMake must stay until every CI job passes
with okay-build. Worth continuing only as the staged, optional path: B first (cheap, shows whether the engine builds).

## Windows host plan (not implemented)
Replace `runCmd` (fork/exec/pipe) with CreateProcessW and an inheritable pipe; run raw actions with `cmd /C` instead of `sh -c`
(or run argv directly); lexically-normal paths use `/` already; mtime via `std::filesystem` is fine; 32767-char command limit
makes response files mandatory (done, threshold must be below it); no `cd` chaining, so set the working directory in
CreateProcess. Threads: use `std::thread`/`mutex` only in this dev tool, never in headers the launcher/editor/player reach
(rule 21a), and MinGW "win32" thread model cannot compile them, so build the tool with the posix MinGW or MSVC.
