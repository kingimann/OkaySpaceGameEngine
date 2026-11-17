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
- Still not done: Windows host (fork/exec only), FNV-1a 64-bit hash (fine for change detection, not cryptographic).
  Structured targets use -MMD (system headers not tracked), imported ones keep CMake's -MD.

## No-op speed (2026-11-17, measured on this machine)
The old "27 ms vs Ninja 8 ms on 51 files" was before the -O2 and stat work. Re-measured here, median of several runs,
process start included, nothing else busy on the machine:
- 52 translation units, just built (105 inputs). Before: okay-build 2.54 ms and it read every input (hashed 105, trusted 0);
  Ninja 1.94 ms. After: okay-build 2.00 ms, hashed 0, trusted 105; Ninja 1.82 ms.
- 200 copies of a 64 KiB file, just built. Before: okay-build 12.7 ms (hashed all 200). Ninja 2.2 ms.
  After: okay-build 2.61 ms (hashed 0, trusted 200). Ninja 2.08 ms.
What changed: a modification time with sub-millisecond digits is trusted immediately (an ordinary write cannot keep it).
Whole-second and whole-millisecond stamps still wait until they were 2 s old when recorded, because two writes can share
them. A clean tree is checked on the calling thread and returns without starting workers. Reading a file, when it is
still required, uses one `read` loop. Forged timestamps that keep the same size are still not detected.

## Regenerating the manifest (2026-11-17)
A single `buildfile` block names a command, `inputs`, and an optional `depfile` (same make-style reader as compile
depfiles). `okay-build build` hashes those files. If the hashes differ from `.okaybuild/buildfile`, or there is no
record yet, it runs the command and reads the manifest again (at most twice). A hand edit of `build.okay` is left
alone until an input changes. `clean` deletes the record, so the next build regenerates. `graph`, `targets`,
`compdb` and `clean` do not regenerate. `--dry-run` does regenerate, because the planned steps come from the new file.

## Stage A follow-up (2026-11-17, grok/okaybuild-1): pools, generators, restat, -k, listings
Written from the same general ideas (a named slot counter beside the worker count, declared outputs as edges,
content hash of an output after the command, keep scheduling work that does not depend on a failure). No other
build tool's code was read.
- `pool NAME` / `depth = N` (at least 1). A step runs only when a worker is free and its pool has a free slot.
  Link steps (`LD`) use the built-in pool `link` with depth 1 unless the manifest sets `pool link` or `pool = none`
  on the target. `pool = console` is depth 1 and the command writes straight to the terminal.
- An action may list `outputs` (more than one), `restat = 1` and `pool`. A target may list `needs` (order-only:
  wait for the action that produces that file) and `pool` (the link or archive step only). A source that is some
  action's output does not have to exist when the graph is planned.
- `restat`: after the command, if every declared output has the same bytes as last time, the previous modification
  time is put back and dependents are not rebuilt. Dependents already skip identical bytes; restoring the time is
  what a later mtime check can see.
- `-k` / `--keep-going`: a failed step does not run its dependents, and other ready steps still run. Without `-k`
  the build stops taking new steps (steps already running finish).
- `okay-build targets` prints `name<TAB>lib|exe` and one line per raw output. `okay-build graph [name...]` prints
  each step as `id<TAB>kind<TAB>desc<TAB>output<TAB>pool=NAME` plus `deps` lines.
- Bug fixed (2026-11-17): a `#` used to end the line even inside quotes and inside `cmd`, so `sources = "a#b.cpp"` and
  `cmd = printf '#define V 3'` were cut in half. Comments are now stripped only outside quotes, and a `cmd` line is kept whole.
  The same test aborted the process: an output in the current directory has an empty parent path, and
  `create_directories` on that path throws out of the worker thread (`std::terminate`). Empty parents are skipped, and a
  worker exception fails that step instead of killing the process.

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

## Stage B follow-up (2026-11-17, second round)
Machine was loaded by other agents (load 5-8 on 4 cores) and disk fell under 4.5 GB, so some measurements are partial.
- Whole engine library (334 compiles + 3 archives, -O0 -g0, -j3): okay-build 7 min wall (423 s) when the machine was quieter; the
  Ninja run on the same configure took 725 s wall, 939 s user, 61 s sys (heavily loaded; wall times are NOT comparable, CPU times are).
  Objects: 333 of 334 byte-identical to Ninja's. `third_party/ufbx/ufbx.c.o` differs (47 bytes, same size; cause not found, no
  __DATE__ string seen) so libokay_engine.a differs too. No-op: okay-build 381 ms before the speed work, Ninja 52 ms.
- Test exes through okay-build (tests/test_serialize + tests/test_math, 341 steps incl. 2 links, 792 s wall under load, 977 s user):
  both run and pass (67 checks / 15 checks, 0 failures). The Ninja-built twin was NOT compared: disk ran low, so unverified.
  No-op of those 341 steps: 180 ms (before the speed work: not measured on this tree).
- Order-only edges: `ninja -t commands` drops them, so the importer reads `build.ninja` (build statements, phony nodes) and keeps
  every edge that leads to another imported output as `after`. On the engine configure only 2 exist (archive after crypto/platform
  archives); no real generated file exists in the Linux configure (only `configure_file` at configure time and a Windows version
  resource in cmake/OkayWinVersion.cmake). The mechanism is tested with a real CMake+Ninja tiny project (custom command makes gen.h).
  Custom steps: their output is found by matching a build.ninja CUSTOM_COMMAND output against the command words (if none matches
  the step always runs); absolute existing file inputs of the edge are tracked. With `--run-dir`, absolute paths of the build tree in
  commands (`cd BUILD`, `-IBUILD`) are rewritten to the run tree.
- Speed: the "9% slower" earlier was load noise. `--profile` on the 62-file slice: user CPU 70-72 s for both tools; okay-build's own
  work is plan 80 ms, dirty checks 9 ms, depfile+hash 40 ms in total (below 0.3% of the run). The real problem was the no-op:
  copying each state record, stringstream parsing, two filesystem calls per file, and building the tool with -O0. Fixes: no copy,
  hand-parsed state, one stat(2), hash maps, `build.sh` now -O2. Slice no-op 250 ms -> 23 ms at -O2 (30 ms at -O0); Ninja 25-29 ms.
- MinGW: importing a MinGW configure of the engine works after two importer fixes (archive tool is `x86_64-w64-mingw32-ar`,
  objects end in `.obj`); the Core slice imports as 62 compiles + 1 archive. It was NOT built (disk stop), so bytes vs Ninja are
  unverified for MinGW. The CMake configure used: toolchain cmake/mingw-w64-x86_64.toolchain.cmake, tests/sandbox/launcher/
  player/editor/assimp OFF (the launcher/player/editor targets need SDL2 for MinGW).
