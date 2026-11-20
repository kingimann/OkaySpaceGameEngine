# Own build tool (okay-build): internal plan

> **What this is:** Internal note, not a guide. Owner, 2026-11-17: start, do not replace CMake tomorrow.
>
> **Status:** Internal record (audit, plan or status): read it as a dated record, newest facts in the code.
>
> **Last checked:** 2026-11-20 (moved into `platform/`, paths and links checked; the text itself was not re-checked against the code).

Internal note, not a guide. Owner, 2026-11-17: start, do not replace CMake tomorrow. OFF: not in CMake, CI or releases.
Written from general knowledge of build systems (action graph, compiler depfiles, content hashes, worker pool).
No code from CMake, Ninja, Make, Meson, Bazel, MSBuild, ccache or sccache was read or used (rule 19a).

## Audit, re-done at cf85d6db and updated for claude/okaybuild-2 (owner brief: 12 subsystems, one build system)
Read of `tools/okaybuild/okay-build.cpp` (about 2800 lines) and `test_okaybuild.cpp` on `grok/okaybuild-1` at `cf85d6db`, then each claim checked by running the tool (`sh tools/okaybuild/build.sh`: 194 checks passed before this branch's changes). One executable, one manifest format (`build.okay`), one state file (`.okaybuild/state`, binary OKB1). Nothing is wired into CMake, CI or releases. No Windows machine was used: Windows code is only cross-compiled with MinGW (a test does that) and, where Wine exists, run under Wine.

Dependency map, in the order a build walks them:

1. **CLI** (`main`) picks a command and options, then loads the manifest.
2. **Manifest parser** (`build.okay`: `toolchain`, `config`, `option`, `if`, `target`, `action`, `pool`, `package`, `dist`, `buildfile`) and its generators (`modules`, `native`, `import`).
3. **Configuration** (`config.okay`, `--config`, `--opt`) edits the toolchain flags. **Toolchain resolution** (`toolchain auto`, `--toolchain`) fills the compiler paths from detection. Both run before planning.
4. **Planner** turns targets and actions into a graph (compile, archive, link, raw command), orders links, finds cycles, applies MSVC flag spelling.
5. **Incremental check** loads the state file and decides each step from output, command hash, depfile headers and a stat shortcut. FNV-1a 64-bit, change detection only.
6. **Compiler cache** is asked before a compile runs: preprocess, key, restore or compile and store.
7. **Scheduler** (pools, `-k`, memory budget, jobserver, Ctrl+C, journal resume). POSIX uses `<thread>`, Windows uses `CreateProcessW` handles.
8. **Reports** (per-step notes, `report`, history, diff, json), **tests** (`test`), **packages** (`okay.lock`, `package`), **compdb**.

The coordinator's table (the first version of this note repeated it) was out of date, because Grok's rounds 3 to 6 had already added most of the missing rows. Corrected state, with what this branch adds:

| # | System | Exists | Incomplete | Missing |
|---|---|---|---|---|
| 1 | Configuration | Named configs, typed options, `config.okay`, `--config`, `--opt`, `if` blocks | Flags are written GNU-style and respelled for MSVC at plan time | Per-config build dirs (a config does not get its own output tree) |
| 2 | Build graph | Compile/archive/link/raw, link order, target and action cycles, `needs`, `graph`, `targets` | | |
| 3 | Scheduler | Pools, `-k`, memory budget, jobserver client and publisher, Ctrl+C, journal resume | `--memory N` is still a number the author picks; `--memory-mb N` uses measured peaks from the last run (POSIX only) | Measurement on Windows; peak is the largest single process, not a sum over a process tree |
| 4 | Incremental | Content hashes, depfiles, stat shortcut, restat, OKB1 state | | |
| 5 | Compiler cache | Preprocessed-text key, LRU size limit, `cache stats|clean`, imported gcc/clang/cl compiles. **Added here:** one cache shared by many build trees (`--cache-dir`, `OKAY_CACHE_DIR`, keys no longer depend on the output path), atomic entry writes, size-checked entries (damaged entry is a miss and is removed) | Shared cache is not locked across processes beyond rename; two builds can both miss and both store the same entry (harmless). `--cache-compress` is opt-in. `--remote-cache DIR` shares entries through a folder | Remote cache over a network (design below, no code) |
| 6 | Toolchain manager | Presets gcc/clang/mingw/msvc, `toolchains` probe including MSVC roots and (Windows host) registry. **Added here:** `toolchain auto` and `--toolchain NAME` use the detected paths | MSVC needs `INCLUDE`/`LIB` from a Visual Studio prompt: detection finds `cl.exe` but does not set them | Not run on a real Visual Studio layout (only fake folders) |
| 7 | Packages | Local packages, `okay.lock` with tree hashes | | Download, registry |
| 8 | Testing | `test` runs test exes in parallel with PASS/FAIL and check counts. **Added here:** `test NAME...`, a failing test's output, `--timeout`, `--junit FILE`, `--shard I/N` | | Per-test run time in XML, retry of flaky tests |
| 9 | Asset pipeline | `importer` blocks (pattern to command per file, partial-output rename, settings key), see Importers | No engine-side headless converter yet (coordinator adds it when asked) | |
| 10 | Packaging | `package` and the `dist` layout block | Not run against the real engine | |
| 11 | Diagnostics | Per-step time, hit/miss per step, report history and diff. **Added here:** cache changes in `report diff`, totals by step kind, `report json` | | |
| 12 | CLI | `tool` helpers, build, clean, compdb, import, graph, targets, modules, native, toolchains, report, test, cache, lock, package | | |

The scheduler gaps, packages, assets and packaging keep their old order. This branch did configurations/toolchains, the cache, diagnostics and testing, in that order, one commit each.

## Configurations and toolchain detection
Named configs live in the manifest: `config NAME` then `optimize` (0–3, emitted as `-O`), `debug` (on adds `-g`), `flags`, `link_flags`, `define`. `--config NAME` selects one. With no selection, the first config is used. A custom name is just another block.

Typed options are one line, the same shape in the manifest and in `config.okay`: `option NAME bool|int|string = VALUE`. `config.okay` sits next to the manifest and may also say `config = NAME`. `--opt name=value` overrides the file. `if OPTION` and `if OPTION == VALUE` add flags and defines when the value matches. Bool words are on/off, yes/no, true/false, 1/0. A bad value is an error at load time. The chosen flags become part of the command line, so the existing incremental check rebuilds when the config changes.

`okay-build toolchains` looks for gcc, clang and the MinGW cross compiler on PATH, plus any `--search DIR`. MSVC is `cl.exe` and `link.exe`. `--msvc-root DIR` walks `VC/Tools/MSVC/<version>/bin/Hostx64/x64`. On a Windows build the same walk also reads `VSINSTALLDIR`, `VCToolsInstallDir`, and `HKLM\SOFTWARE\Microsoft\VisualStudio\SxS\VS7` (and the WOW6432Node copy) through advapi32. It does not run vswhere. The registry read is compiled into the Windows program only. It was cross-compiled and started under Wine here. It was not run on Windows, and no Visual Studio install was present, so a real `cl.exe` discovery is unverified.

## Distribution layout
A `dist` block tells `okay-build package` to assemble the same shape as `scripts/build-windows-dist.sh`: the launcher and its DLL at the top, `Tools/` with the other programs and a second copy of the DLL, `VERSION.txt` in both places, `Assets/Textures`, `Assets/Fonts`, `examples/<folder>/`, and `README.txt`. The script itself is not run.

## Response files for MSVC
The `msvc` preset writes response files as UTF-16LE with a BOM (`FF FE`) and a CRLF after each argument. `rsp_utf16 = off` in the toolchain block turns that off. Other toolchains stay one byte per character.

## FIFO jobserver
If `MAKEFLAGS` or `OKAY_JOBSERVER` contains `--jobserver-auth=fifo:PATH`, okay-build opens that fifo and takes a byte before each command, the same way it takes a byte from the pipe form. It is a client only.

## Package checksums
`okay.lock` now ends each line with an FNV-1a 64-bit hash of the files under that package path (relative names, then bytes). A later build refuses to run when the tree hash differs. This is change detection, not a security hash.

## Report diff
`okay-build report diff A B` reads two saved reports (history numbers or file paths) and prints steps that are only in one of them, and steps whose time changed.

## Imported compiles in the cache
An imported command whose first word is gcc, g++, clang, clang++ or cl, and which passes `-c` or `/c`, is preprocessed and keyed like a structured compile. A later build that is missing the object copies it back and does not compile. Archives and links are left as ordinary commands.

## Native manifest (no CMake)
`okay-build native <repo>` reads the source tree, not a CMake build directory. It writes a `build.okay` with `okay_crypto`, `okay_platform`, `ufbx`, `okay_engine` and `test_math`. Files that are only compiled for optional backends (Assimp, Steamworks, the Win32 platform file) are left out. Includes and the Linux defines (`OKAY_HAVE_SODIUM`, the platform SDL/own/X11 switches) are written into that manifest. The real `test_math` run is recorded below when it has been measured.

## Compact state (OKB1)
The state file is now binary, version 1, magic `OKB1`. Paths are stored once. Hashes are 8-byte integers. The whole file is read in one shot (mapped, then copied into a pool the records point at). A file that still starts with `okaybuild-state 3` or `okaybuild-state 4` is read as the old text. A journal after a killed build is still text and is applied on top. The mtime+size shortcut is unchanged: a matching stamp is not hashed again. `--profile` prints plan time and state time apart.

## Benchmarks on the 339-step test_math tree (2026-11-17, this machine, OKB1)
Same configure as before: shallow clone, Ninja Debug `-O0 -g0`, tests on, player/editor/launcher/sandbox/assimp off, sodium on, `-j2`. Import is 335 compiles, 3 archives, 1 link. Not Windows. These numbers replace the earlier 63.5 ms no-op.

No-op, median of 20, process start included, after the binary state existed:
- okay-build: 29.6 ms (runs from 27.8 to 35.0). Profile of one of them: plan 2 ms, state 12 ms, dirty-check 8 ms, wall 23 ms inside the process.
- Ninja `test_math`: 28.7 ms (27.8 to 33.3).

The state file is 3.9 MB (`OKB1`) instead of 11 MB of text. The old gap of about 38 ms is about 1 ms on this run.

Cold build, clean tree, same `-j2`:
- Ninja: 439.7 s
- okay-build: 413.5 s. Profile: plan 3 ms, state 0 ms (nothing saved yet), dirty-check 5 ms, preprocess 0 ms, run command 819723 ms summed on two workers, depfile+hash 1495 ms, wall 413489 ms. Ran 339 of 339.

Preprocess is 0 because an imported step is the compiler command itself, not a structured compile that runs `-E` first. The cache therefore does not restore those outputs: deleting them and building again would compile them again. A cache-hit rebuild was not measured on this tree for that reason. The separate preprocess timer is in `--profile` for structured compiles.

## Asset pipeline (design only, not built)
A pipeline would be a rule in the manifest: a source suffix, a tool, an output suffix, and a folder to scan. okay-build would turn each match into an action edge. The engine's importers are inside the engine and the editor, not a program this tool can run. Until there is a command-line importer, assets stay as hand-written `action` blocks. No engine file was changed. The question for the owner is in the round report: is a small importer program allowed, or do assets stay as actions?

## MSVC flag spelling
The `msvc` toolchain preset drives `cl.exe`, `link.exe` and `lib.exe` (object `.obj`, library `.lib`, program `.exe`). Configs, defines and includes are spelled for that driver: `/Od` `/O1` `/O2` `/Ox`, `/Zi`, `/D`, `/I`, `/Fo` on a compile, `/OUT:` on `link.exe` and `lib.exe`, `/Fe` when the linker itself is `cl.exe`, `/LIBPATH:` and `name.lib` for libraries. A compile passes `/showIncludes`. Lines that say `including file:` are written into the same depfile the gcc path already reads, so a named header is a real dependency. Long commands use the existing response-file path (`tool @file`, one argument per line). This was checked with a fake `cl.exe` that records its arguments. It was not run against a real Microsoft compiler.

## Compiler cache
Before a compile that is going to run, okay-build preprocesses that file and builds a cache key from three pieces: the preprocessed text, the compile command, and the compiler file's own bytes. The hash is FNV-1a 64-bit. It is not collision-safe. Two different inputs can land on the same key. The cache is a speed-up, not a security boundary, and it is not a cryptographic check. `--no-cache` turns it off.

A hit copies the stored object and its depfile back and does not compile. A miss compiles and stores both under `.okaybuild/cache/<key>/`. Changing the source, a header that the preprocessor sees, the command line, or the compiler file produces a different key, so the old entry is simply not used. `--cache-max N` (bytes, or a `k`/`m`/`g` suffix) evicts the least recently used entries until the total fits. One entry that is itself bigger than the limit is kept. `okay-build cache stats` prints the entry count, the bytes, and the limit. `okay-build cache clean` deletes the cache.

## Report history and test check counts
Each build keeps a copy under `.okaybuild/reports/N.txt`. The last 20 are kept. `okay-build report history` lists how many. `okay-build test` reads a line such as `4 checks, 1 failed` from the program's output and repeats those counts next to PASS or FAIL.

## Diagnostics
Each step that runs records its kind, its description, how long it took, and whether the compiler cache hit, missed, or was not involved. The build writes that to `.okaybuild/report`. `okay-build report` prints the step count, the cache hit rate, the slowest steps (up to ten), and the summed time. `--profile` still prints the older plan/dirty/command/wall line. The report is the per-step view.

## Testing
`okay-build test` builds every exe marked `test = on` (a library cannot be a test) and then runs those programs. If none are marked, it runs exe targets whose names start with `test`. Anything else is left alone. The runs use `-j`, the same limit as the build. On Windows the wait is `WaitForMultipleObjects`, so the tool still does not use `<thread>`. The command prints `PASS` or `FAIL name exit N`, then `N tests, M failed`, and writes the same facts to `.okaybuild/test-report`. Exit status is 1 when a test fails.

## Scheduler memory limit
An action may say `memory = N` (a plain integer, the unit is whatever the manifest author means). `--memory N` is the budget. The scheduler will not start a step whose memory would push the running total over that budget, on top of the existing pools. A step that by itself needs more than the budget is refused before anything runs. With no `--memory`, nothing is counted and the old behaviour stays.

## Cancellation and a killed build
Each finished step is appended to `.okaybuild/state.journal` before the build ends. The final save folds that into `.okaybuild/state` and deletes the journal. If the process is killed first, the next run reads the journal and does not repeat a step that finished. Ctrl+C sets a flag, stops starting new steps, and ends the running command (its process group). The process then exits 130. ## Packaging
`okay-build package` builds an exe and copies it into `dist/<name>/` with each `runtime` file beside it and each `assets` folder kept as a folder. `PACKAGE.txt` lists every copied path. Windows loads a DLL from the exe's own directory, so runtime files sit next to the exe on purpose.

`scripts/build-windows-dist.sh` (read, not called) does more, and only for the whole engine: `OkaySpace.exe` and `SDL2.dll` at the top, a second `SDL2.dll` under `Tools/` next to the editor and the player, `VERSION.txt` in both places, `Assets/Textures`, `Assets/Fonts`, a curated `examples/` list, and `README.txt`. okay-build package is the general layout for one target. It does not assemble that full engine distribution.

## Packages and lockfiles
A manifest may declare local packages: `package NAME`, then `path`, `version`, and `depend = OTHER VERSION`. There is no download and no registry. Resolving checks that every dependency names a package in the same manifest and the same version. A mismatch is an error that names both versions. The first build writes `okay.lock` (name, version, path, sorted). Later builds refuse to run when the lock and the manifest disagree, so the set of packages stays reproducible. `okay-build lock` writes or checks the same file.

## Jobserver
`--jobserver` opens a pipe and puts `-j` minus one `+` tokens in it (this process keeps one slot without a token, which is the usual arrangement). It publishes the read and write ends in `OKAY_JOBSERVER` and in `MAKEFLAGS` as `--jobserver-auth=R,W`. A later okay-build sees that and takes a token before each real command, then puts it back. One slot stays implicit so a nested run can make progress without waiting for a token that its parent is holding for it. On Windows the same idea is a named semaphore (`OKAY_JOBSERVER=sem:NAME`) instead of a pipe. The test runs two child okay-builds, each with `-j8`, under a parent `-j2 --jobserver`, and the number of leaf commands in flight stays at 2.

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
- The Windows host is now in this file (see the later note). The hash is still FNV-1a 64-bit: fine for change
  detection, not collision-safe for security. Structured targets use -MMD (system headers not tracked),
  imported ones keep CMake's -MD.

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
This list is the gap as of the first prototype. Later sections on this page land pools, restat, generator outputs,
`-k`, `graph`/`targets`, the console pool, manifest regeneration and a faster no-op. Still open after that: compdb of
imported commands, a jobserver, and a Windows host (see the design note). The old timing (27 ms vs Ninja 8 ms on 51
files) is replaced by the measurements in "No-op speed".

## Stage B: CMake importer (done as prototype)
`okay-build compdb` writes structured compiles and also imported commands that contain `-c` (the source is the argument after `-c`; links are left out). The entry uses `command` with the raw text and `directory` from the action.
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

First slice (2026-11-17): `okay-build modules DIR [-o build.okay]` reads every `*.okaymodule` in that directory
(flat `key = values` lines: name, kind lib|exe, sources, include, export_include, define, deps) and writes one
`build.okay`. Unknown deps and a dep on an exe are errors. It does not build; the usual `okay-build` does that next.
It does not read the engine's real modules.

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

## Real engine executable through the importer (2026-11-17)
Configure: Ninja, Debug, `-O0 -g0`, tests ON, sandbox/launcher/editor/player/assimp/tools/OkayUI tests OFF,
libsodium found (packet encryption on). C files used clang (`/usr/bin/cc`), C++ used g++ (`/usr/bin/c++`).
`GL/gl.h` was missing at first (`libgl1-mesa-dev` installed, then the build continued). No code change in okay-build.

`ninja -j2 test_math`: 339 steps, exit 0. The program printed `15 checks, 0 failures`.
`okay-build import` of that target: 335 compile, 3 archive, 1 link, 2 order-only edges, 0 custom commands.
Built with `okay-build -j2` into a separate directory so Ninja's tree stayed put. 339 of 339 steps, exit 0.
Byte comparison of every `.o`, the three archives (`libokay_engine.a`, `libokay_crypto.a`, `libokay_platform.a`)
and `tests/test_math`: 339 files, 339 identical. The okay-build program also printed `15 checks, 0 failures`.
`libokay_engine.a` is 463,143,278 bytes on both sides.

No-op of that same 339-step import, measured after a clean okay-build so every stamp matched (median of 5, process start included):
okay-build 66 ms, Ninja 24 ms. Inside okay-build: plan+state load 42 ms, dirty check 9 ms, wall 52 ms, hashed 0, trusted 1835.
The state file is 11 MB and 105,057 rows because every compile records every header. A rewrite that interned paths and stored hashes as integers was not faster (state load about 54 ms, wall about 67 ms), so it was not kept. The gap with Ninja is that table, not re-reading file bytes.

## Player through the importer (2026-11-17)
Same configure as test_math, plus `-DOKAY_BUILD_PLAYER=ON` (SDL2 2.30 dev headers were installed). Ninja `okay-player`:
347 steps, `bin/okay-player` is 87 MB. okay-build import: 350 compile, 6 archive, 1 link, 14 order-only edges.
`okay-build -j2` in a separate directory: `ran 357 of 357 steps`, exit 0. Byte comparison of every `.o`, every
archive and `bin/okay-player`: 357 files, 357 identical. Both binaries exit 0 on `--help`. The help text differs
only because it prints its own path. The player was not given a scene and was not opened on a display.

## Windows host (2026-11-17, implemented, POSIX path unchanged)
`okay-build.cpp` compiles with `x86_64-w64-mingw32-g++-win32` (`-std=c++17 -O2 -static-libgcc -static-libstdc++`).
The Windows build does not include `<thread>`, `<mutex>`, `<condition_variable>` or `std::call_once`. Jobs are
started with CreateProcessW and the one thread waits on their handles with WaitForMultipleObjects (at most 64),
draining the stdout/stderr pipe every 20 ms so a full pipe cannot stall the child. Raw commands are written to a
`.cmd` file and run with `cmd.exe /C`, with the manifest `dir` as the process working directory. A tool invocation
longer than about 7000 characters, or any direct argv longer than 30000 (and never past 32767), is put in a
response file. File stamps use `GetFileAttributesExW` (100 ns ticks). A leading `/` path is tried as `Z:\...`
because Wine maps `/` to `Z:`. Under Wine (`WINEPREFIX=$HOME/.wine-okay`) a two-step `echo` graph and `-k` both
passed. The program was not tried on a real Windows machine.

## Windows host design note (2026-11-17, the plan this was written from)
The `toolchain mingw` block already names `x86_64-w64-mingw32-gcc-win32`, `g++-win32` and `x86_64-w64-mingw32-ar`,
object suffix `.o`, executable suffix `.exe`. On this machine that compiler linked a two-file static library and a
program. `file` reported `PE32+ executable (console) x86-64, for MS Windows`. The program was not run (no Wine).
The same shape is locked in by `testMingwSlice` in `tools/okaybuild/test_okaybuild.cpp`.

`okay-build.cpp` does not compile as a Windows program. Both the win32 and the posix MinGW g++ stop at
`#include <sys/wait.h>`. The tool runs commands with `fork`, `execvp`, `pipe` and `sh -c`. A Windows host has to
replace that, not just switch compilers.

What was checked about threads, because rule 21a says the win32 thread model cannot compile `<thread>`, `<mutex>`
or `<condition_variable>`: a small program that creates a thread, a mutex and a condition variable compiled and
linked with BOTH `x86_64-w64-mingw32-g++-win32` and `g++-posix` (MinGW-w64 gcc 13.2 on this machine). The win32
exe imported KERNEL32, msvcrt, libgcc and libstdc++. The posix exe also imported libwinpthread-1.dll. Neither exe
was run. This does not change rule 21a for the launcher, editor and player. It only says this particular compiler
accepted those headers. The build tool stays a POSIX program until the process code is replaced.

Replacement, when it is written (still not in this branch):
- `runCmd` becomes CreateProcessW. The working directory is the process argument (`dir` in the manifest), not a
  `cd` glued onto the command. Stdout and stderr come back through an inheritable pipe. Wait with
  WaitForSingleObject. One job at a time needs no thread header. A worker pool on the win32 compiler can use the
  threads that compiler accepted here, or a loop that waits on several process handles.
- Raw actions on Windows go through `cmd.exe /C`, because imported CMake lines use shell syntax (`: && ... && :`).
  A command with no shell metacharacters can be an argv array instead.
- Paths in the manifest already use `/`. Keep that until a tool rejects it.
- The command-line limit is 32767 characters. Response files already exist and the default threshold (30000) is
  under that limit. A Windows host should keep the threshold below 32767.
- Modification times: NTFS ticks are 100 ns, FAT is 2 seconds. The sub-millisecond shortcut is for stamps that
  actually change on every write. On a coarse Windows filesystem the 2 second rule is the one that is correct.
  That choice is part of writing the host, not something this note turns on.
- No cryptography. The change-detection hash stays a non-cryptographic 64-bit mix.

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

## Added on claude/okaybuild-2 (what was written from)
Written from general knowledge only (a content-addressed store, write-to-temp-then-rename, a process-group kill, JSON text), no other tool's code.
- `toolchain auto` / `--toolchain auto|gcc|clang|mingw|msvc`: runs the same probe as `okay-build toolchains` (`--search`, `--msvc-root`, PATH). `auto` takes the first found: msvc, clang, gcc, mingw on a Windows host; gcc, clang, mingw, msvc elsewhere. `cc`, `cxx`, `ld`, `ar` set by hand in the `toolchain` block are kept. Nothing found is an error that says so. How to try: `okay-build compdb --toolchain gcc` and read `compile_commands.json`.
- Cache: `--cache-dir DIR` or `OKAY_CACHE_DIR`. The key leaves out the values of `-o`, `-MF`, `-MT` and `/Fo`, `/Fd`, so a second build tree with the same sources hits. The key is still FNV-1a 64 bit: not cryptographic, not collision-safe, a speed-up only. Entry format `okaybuild-cache 2` (key, object size, depfile size); old format-1 entries are refused and rebuilt.
- `okay-build report json`; `report` ends with totals by step kind; `report diff A B` prints `cache: STEP miss -> hit`.
- `okay-build test [NAME...] [--timeout SECONDS]`. A timeout kills the test's process group (POSIX) or the process (Windows, `TerminateProcess`), exit code shown as 124. The Windows timeout code is only cross-compiled, never run.

## Round 6 on claude/okaybuild-3: engine library and `test_math` from a native manifest (2026-11-17)
Written from general knowledge only; no other tool's code was read. Linux only: nothing here was run on Windows.

How to try it (Linux, g++, SDL2 and libsodium dev headers, about 2 GB of disk for the tree):
`g++ -std=c++17 -O2 -pthread tools/okaybuild/okay-build.cpp -o /tmp/okay-build` then
`sh tools/okaybuild/examples/native_test_math.sh /tmp/okay-build /tmp/okb-tree 2`.
It runs `okay-build native` (writes a manifest from the source tree), builds `okay_crypto`, `okay_platform`, `ufbx`, `okay_engine` and `test_math` (340 steps, Debug `-O0 -g0`) with no CMake or Ninja, and runs `test_math`. You should see `15 checks, 0 failures`. Delete the tree afterwards.

What this found and fixed:
- The generated manifest missed the `shared/` include directory (`AntiCheatChecks.hpp`, `RobotoFontOwn.h`), so `Net/RelayServer.cpp` did not compile. `native` now adds `shared` to the include list when the folder exists. (CMake adds it per file; we add it for the whole engine target, which is a small difference in include paths and no difference in the result.)
- After a build that restored objects from the cache, every later no-op re-read those object files (the saved size+mtime no longer matched), about 300 ms for 52 objects. The saved stamps are now refreshed when the bytes still match (`refreshStamps`), also on a no-op run.

Result: the okay-build `test_math` printed `15 checks, 0 failures`, the same as the CMake/Ninja `test_math` built from the same tree. Not compared: object files byte for byte (the two builds use different flag sets, for example CMake adds `-DOKAY_DEFAULT_ACCOUNT_*` and per-file options), so only the final run result was compared.

Timings, this machine (4 cores, `-j2`, Debug `-O0 -g0`, g++, one run each for the long ones, same source tree, CMake 339 steps vs okay-build 340; not Windows):
| | okay-build | Ninja |
|---|---|---|
| cold build (cache on, 0 hits) | 8 min 40 s | 9 min 09 s |
| one header edit (`Core/Time.hpp`, 52 compiles + archive + link), cache on, all misses | 4 min 59 s | 4 min 01 s |
| same edit, `--no-cache` | 3 min 28 s | 4 min 01 s |
| no-op, median of 15 | 12 ms | 34 ms |
So with the compiler cache on, a miss costs one extra preprocess run per file (that is the 20% on the edit). The cache earns it back only on a hit: the same header edit as a comment only (same preprocessed text) took 7 s because all 52 compiles were hits. A cold build is compiler-bound in both tools, so the two cold numbers are close and one run each does not show a real difference. The first okay-build cold attempt (not counted) was disturbed by other work on the machine.

## Importers (claude/okaybuild-3, written from general knowledge, no other tool's code)
An `importer` block maps a file pattern to a command line, one action per matching file. It is a way to say "turn every `assets/*.png` into `out/*.tex` with this tool". The tool itself can be any program; okay-build links no engine code and the engine importers are untouched.
```
importer textures
  pattern = assets/**/*.png        # one folder, or ** for sub folders; * and ? match inside a file name
  output  = out/{dir}/{stem}.tex   # must use {stem}, {name} or {input}
  cmd     = sh tools/conv.sh {input} {output} {settings}
  settings = --quality 5           # part of the command key: changing it rebuilds every file
  inputs  = tools/conv.sh          # extra files every output depends on (the tool, a shared table)
  depfile = out/{stem}.d           # optional, make-style, like an action
  pool    = none                   # optional
end
```
Fillers: `{input}` the file, `{name}` its name, `{stem}` the name without extension, `{ext}` the extension without the dot, `{dir}` the folder under the pattern's base (empty at the top), `{output}`, `{depfile}`, `{settings}`.
- Each file becomes an ordinary `RUN` step, so it is tracked, hashed, listed by `targets` and `graph`, and can be built by output name or waited on with a target's `needs`.
- Only a changed asset rebuilds. A changed `settings`, `cmd` or `inputs` file rebuilds all. A deleted output rebuilds only itself. A new matching file is picked up on the next run. A removed source leaves its old output in place (nothing deletes outputs).
- `{output}` in the command is really `out/{stem}.partial.tex`: the tool writes there and okay-build renames it to the real name only when the command succeeded. A failing importer leaves neither the partial nor the real output (an older good copy is removed too, so a later step cannot use it by mistake).
How to try: `sh tools/okaybuild/build.sh /tmp/okb` runs the tests (`importer blocks ...` is the group). By hand: make `assets/a.txt`, `tools/conv.sh` containing `cat "$1" > "$2"`, the block above with `pattern = assets/*.txt`, `output = out/{stem}.bin`, `cmd = sh tools/conv.sh {input} {output}`, then `okay-build`, edit one file and run it again with `--explain`.
Also fixed on this round: `poolStr` (the string pool the state records point into) was written from several worker threads without a lock, which could make a record lose an input on a parallel build (seen once as a flaky "new input" rebuild in the new test); it has its own lock now.

## Per-configuration output folders and a build trace (claude/okaybuild-3)
Written from general knowledge only (the Chrome trace event format is a public JSON description: complete events with name, category, phase `X`, start and duration in microseconds, process and thread ids).
- `outdir = build/{config}` in the manifest: `{config}` becomes the selected configuration's name (`--config NAME`, `config.okay`, or the first `config` block), or `default` when the manifest has no config blocks. Programs go to `build/<config>/`, objects to `.okaybuild/obj/<config>/<target>/...`. Outputs are tracked by path, so building `release` after `debug` does not touch the debug files, and going back to `debug` rebuilds nothing. `clean --config release` removes only that folder. Without `{config}` in `outdir` nothing changes from before.
- `--trace FILE` writes the steps of that run as Chrome-trace JSON (`{"traceEvents":[...]}`): one `X` event per step (`cat` is the step kind CC/AR/LD/RUN, `args.cache` is hit/miss/-/fail), one row per worker thread (`worker N`, named by `M` events), times from the first step. Open it in a trace viewer that reads this format (for example a browser's trace page). A no-op run writes a file with no steps; a failed build still writes it. It is written to a temp file and renamed. Windows builds record every step on row 1 (that path runs steps from one thread).
How to try: `printf 'outdir = build/{config}\nconfig debug\n optimize = 0\nend\nconfig release\n optimize = 2\nend\ntarget app exe\n sources = main.cpp\nend\n' > build.okay`, then `okay-build --config debug --trace t.json`, `okay-build --config release`, `ls build`; the tests are the group `outdir with {config} ...` in `sh tools/okaybuild/build.sh /tmp/okb`.

## Round 7 on claude/okaybuild-4: test XML, test shards, toolchain versions (2026-11-18)
Written from general knowledge only. The JUnit-style XML layout (testsuites, testsuite, testcase, failure with a message) is a widely used public convention; no other tool's code was read. Linux run only; the Windows build was cross-compiled, not run.
- `okay-build test --junit FILE` writes the run as XML: one `testcase` per test, a `failure` element (message `exit N` or `timed out`, the last 4000 bytes of the test's output, XML-escaped) for each failing one. No run times are recorded.
- `okay-build test --shard I/N` runs the tests whose position in the sorted name list is I, I+N, I+2N and so on (1-based I). Only that shard's programs are built. An empty shard passes. Use it to split the tests over several machines.
- `okay-build toolchains --versions` adds `version=...` to each found toolchain: the first line of `cxx --version` for gcc/clang/mingw, and for MSVC the folder name under `VC/Tools/MSVC/<version>/` (`unknown` when the path has no such folder). It does not run `cl.exe` (that needs the Visual Studio environment). Choosing between several installs by version is not done.
How to try: `sh tools/okaybuild/build.sh /tmp/okb` (group `test --junit writes XML ...`); by hand, in a manifest with `test = on` programs, `okay-build test --junit r.xml`, `okay-build test --shard 1/2`, `okay-build toolchains --versions`.

## Round 8 on claude/okaybuild-4: MSVC version choice, `tool` helpers, compressed cache, first byte comparison (2026-11-18)
Written from general knowledge only; no other tool's code read. Linux only, nothing run on Windows.
- **MSVC versions.** Every install found (PATH, `--msvc-root`, registry on Windows) is a candidate with its version folder name. The old code sorted version names as text (14.9 after 14.10) and let the first root win. Now: the cl.exe on PATH still wins (someone opened a developer prompt on purpose); otherwise the highest version, compared number by number. `--msvc-version PREFIX` pins one (`14.3` matches `14.38.33130`), and a pin that matches nothing leaves msvc missing. Tested with fake folders only. No test was run on the old code first (the text-sort bug was seen by reading it).
- **`okay-build tool ...`** for importer and action commands: `copy SRC DST` (makes folders, writes via a `.partial` file and renames), `hash FILE...` (FNV-1a 64, not a security hash), `pack DIR OUT`, `unpack PACK DIR`, `list PACK`, `z IN OUT` and `unz IN OUT`. Pack format `OKPK` is our own (table of contents of sorted paths, sizes and hashes, then the bytes; no times, so the same folder always gives the same bytes). Unpack refuses paths that are absolute, hold `..` or `:`, cut-short files and damaged entries. No directories, links or permissions are stored.
- **`--cache-compress`.** New cache entries are stored compressed in our own LZ77 format `OKLZ` (written from the general idea of distance/length pairs; layout in the source comment). Off by default. Reading always works for both kinds, marked by a fifth `lz` line in the entry's `meta`. A damaged compressed entry is a miss and is removed. The test shows the object is under half the size on repetitive code. Speed was not measured. The hash is FNV-1a: damage detection only.
- **Manifest quoting (found by the comparison below).** A define could not hold a double quote, so the engine's `OKAY_DEFAULT_ACCOUNT_*` string defines could not be written. `\"` inside a value is now a literal quote (`define = "NAME=\"text\""`), and the manifest writers escape quotes the same way. Side effect: a value ending in backslash then quote, such as `"C:\dir\"`, now means something different. Use forward slashes. `okay-build native` does not add those two defines yet.
- **Byte comparison against CMake, small target.** `okay_crypto` (9 files), CMake Debug-less (`CMAKE_BUILD_TYPE=None`, `-O0 -g0`) with Ninja versus an okay-build manifest taken from `okay-build native` plus `-I engine/include` and the two account defines (`-g` from CMake's Debug type was avoided on purpose, because debug info records paths). Result: all 9 object files byte-identical. The archive differs only in member order (CMake lists them in its own order, we sort by source name); extracted members are identical. Warning flags (`-Wall` and so on) do not change the output. Only this one library was compared, not `okay_engine` or `test_math`, and with debug info on the paths would differ.
How to try: `sh tools/okaybuild/build.sh /tmp/okb`; by hand `okay-build tool pack some_folder out.okpk`, `okay-build tool list out.okpk`, `okay-build --cache-compress` then `okay-build cache stats`, `okay-build toolchains --versions --msvc-version 14.3`.

## Round 9 on claude/okaybuild-4: account defines, archive order, measured memory, engine objects against CMake (2026-11-18)
Written from general knowledge only; no other tool's code read. Linux only. The "wait4 resource usage" and "ELF object" facts are from the public OS documentation and by looking at our own outputs.
- **Account defines.** `account_defines = on` (a top-level line, written by `okay-build native`) adds `-DOKAY_DEFAULT_ACCOUNT_URL="..."` and `-DOKAY_DEFAULT_ACCOUNT_KEY="..."` to every compile, found the way `CMakeLists.txt` finds them: the environment when either variable is set, else `config/public-account.json` (`url`, `key`). The values are never put in a manifest, never printed (`-v`, `-n`, failure messages show `<hidden>`) and never written to `compile_commands.json`. The line `okay-build: account defines: url yes, key yes (from environment)` says only where they came from. Not hidden: the compiler's own output if the compiler itself prints the macro text, the preprocessed text held in memory for the cache key, and the object file (the strings are compiled in, as with CMake). A `-D` option is not supported (CMake's third source).
- **Archive member order.** `okay-build native` orders each target's sources by where the path first appears in `engine/CMakeLists.txt` (only that order is read), the rest by name. `okay_crypto` now gives a `libokay_crypto.a` byte-identical to CMake's. The full engine archive was not compared (the objects were, see below, but not the archive; the order rule is the same one).
- **Measured memory.** Each command's peak resident size comes from `wait4` (POSIX). Saved per step in `.okaybuild/memory`. `--memory-mb N` sets the budget in MiB and gives a step without a declared `memory` its measured peak from the previous run (rounded up, at most N). A step with no record counts as 0, so a first run is unthrottled. Not measured on Windows. Cache hits run nothing and keep the old value. The peak is the biggest single process in that step's tree, so two big children that run at once count once.
- **Engine objects against CMake (`okay_engine` plus the 6 platform files and ufbx: 337 files, `-O0 -g0`, `CMAKE_BUILD_TYPE=None`, g++ 13.3 and gcc, Linux).** Result: 337 of 337 byte-identical when the manifest spells include folders and source paths as absolute paths, the way CMake does (`OKB_ABS_INCLUDES=1`). Without that, 335 of 337: two files differ and only because of the path spelling:
  - `Render/ModelImport.cpp`: a header macro embeds `__FILE__`, which shows `engine/include/...` (relative) against `/home/.../engine/include/...` (absolute).
  - `third_party/ufbx/ufbx.c`: ufbx records `__FILE__` too, and ours also had `-std=c11` where CMake uses the compiler default; the flag was removed.
  Two more differences were handled in `native`: CMake gives `BuildInfo.cpp` `-DOKAY_ENGINE_VERSION="<project version>"` (now read from `CMakeLists.txt` `project(... VERSION x)` and given to the whole engine) and `ImageDecodeJpeg.cpp` gets `-Wno-narrowing` (now global). Neither changes any other object. Warning flags and `-isystem` against `-I` for SDL2 do not change an object.
  Cost of the absolute spelling: the object depends on where the source tree sits. The compiler cache key already ignores output paths but includes the preprocessed text, so two checkouts in different folders would not share entries for these files.
  How to try (about 20 minutes, a few hundred MB at a time): configure CMake (`-G Ninja -DCMAKE_BUILD_TYPE=None -DCMAKE_CXX_FLAGS="-O0 -g0" -DCMAKE_C_FLAGS="-O0 -g0"`), then `OKB_ABS_INCLUDES=1 python3 tools/okaybuild/examples/compare_objects.py /path/okay-build . /path/cmake-build /tmp/work 40`. It builds batches with both tools, compares, deletes. A first run of this script lost one batch to an interrupted ninja (the shell timed out and killed it); `OKB_ONLY=FILE` re-checks listed sources.
- Not compared: the linked `libokay_engine.a`, `test_math`, `-g` builds, other configures, Windows.

## Round 10 on claude/okaybuild-4: path_map, a shared-folder cache, remote-cache design (2026-11-18)
Written from general knowledge only; no other tool's code read. Tests for these parts were written before the code.

### path_map (checkout-independent objects)
`path_map = on` in the `toolchain` block (`okay-build native` writes it). For GNU-style compiles okay-build then spells the source file and every `-I` folder as an absolute path and adds `-ffile-prefix-map=<manifest folder>=/okay-src`. Everything the compiler records from a path (`__FILE__` in the file and in headers, and debug-info paths) then reads `/okay-src/...` wherever the tree sits. The command hash and the compiler-cache key replace the manifest folder by a fixed word, and so do the preprocessed text's line markers, so two checkouts in different folders share cache entries and do not rebuild when the tree is moved. Test: the same sources in two folders give byte-identical objects, print `/okay-src/a.cpp`, and the second folder hits the first folder's cache entry. Off, the relative spelling stays.
Are CMake builds still comparable? Yes, if CMake gets the same map: `-DCMAKE_CXX_FLAGS="-O0 -g0 -ffile-prefix-map=$PWD=/okay-src"` (and the same for `CMAKE_C_FLAGS`). CMake already spells everything absolutely. Without that flag CMake's objects hold the real folder and only files that embed a path differ (seen: `Render/ModelImport.cpp`, `ufbx.c`). The hash change also means the first build after upgrading rebuilds everything once (command hashes now use the fixed word for the manifest folder).
MSVC spelling (`/pathmap`) is not done.

### Shared-folder cache (`--remote-cache DIR`, `OKAY_REMOTE_CACHE`, `--remote-cache-readonly`)
Off by default. The local cache stays first. On a local miss okay-build looks in `DIR/<key>/`; a good entry is copied into the local cache and used (log line `remote cache hit`). A new local entry is copied to `DIR/<key>/` (unless read-only). The key and the entry layout (`meta`, `obj`, `dep`; compressed or not) are exactly the local ones, so a folder can be rsynced or put on a network share by hand. An entry is built as `<key>.tmpN` and renamed, so readers never see a half entry; a reader never deletes a bad remote entry (it counts as a miss) and the next push replaces it. A folder that cannot be used is a warning and the build goes on without it. Tests use fake folders only.
How to try: two clones, `okay-build --remote-cache /some/shared/dir`, build in the first, then in the second and look for `remote cache hit`.

### Design for a real remote cache (not written)
- **Same key.** FNV-1a of (preprocessed text with the checkout folder normalised, compile command without output names, compiler file bytes). FNV is not collision-safe: a shared network cache is a place where someone could plant a colliding entry, so before any network tier the key must become a strong hash (our own, written from a public description) and the entry must carry the hash of the object, checked on restore. Today's local and folder tiers are for one user or one trusted team folder.
- **Protocol.** `GET /<key>` returns the entry as one file (meta, obj, dep with lengths), `PUT /<key>` stores it, with plain HTTP over our own socket code (no library). Objects are compressed with `--cache-compress` before sending. A PUT never overwrites a good entry; a GET that fails or is slow (a few seconds) is a miss, so the network can never fail a build.
- **Trust and secrets.** Read-only for ordinary builds, write only for CI. A token comes from an environment variable and is never printed (the `redact` list already exists). No entry is executed, only copied.
- **Not decided:** eviction on the server (the local LRU idea), entry size limits, and whether to ship objects built with `-g` (their paths are covered by path_map).

### Linked outputs against CMake, and native_test_math.sh re-run (2026-11-18, Linux, `-O0 -g0`, g++ 13.3)
- `sh tools/okaybuild/examples/native_test_math.sh okay-build /tmp/tree 2` on the merged tree: 352 steps, 0 cache hits (cold), `15 checks, 0 failures`. Tree 1.2 GB including the cache.
- CMake `test_math` (351 steps) configured with `-O0 -g0 -ffile-prefix-map=<repo>=/okay-src`, against okay-build with `path_map = on` (the way `native` now writes it):
  - `libokay_engine.a`: our archive holds 330 members and CMake's 331. All 330 shared members are byte-identical, in the same order. The one extra is `ufbx.c.o`: CMake puts the ufbx object inside the engine archive (at position 304); we build ufbx as its own library, because a manifest target has one flag set per target and ufbx needs `-w -O2` and no C++ flags. Cause: manifest shape, not a compile difference.
  - `libokay_platform.a`: all 6 members byte-identical. The archives differed only in member order, because the platform files are listed in `cmake/OkayPlatform.cmake`, which `native` did not read. It does now (order hint only). The new order was checked on the real tree by looking at the generated list, not by rebuilding the archive.
  - `libokay_crypto.a`: byte-identical.
  - `test_math`: byte-identical (`cmp`), 31,784 bytes both.
- Not compared: `libufbx.a` (no CMake counterpart), builds with `-g`, other compilers, a Release configure.

## Round 11 on claude/okaybuild-4: okay-build reads the CMake files itself, runs add_test lines, per-file flags (2026-11-18)
Goal: get nearer to building the real engine (library, launcher, editor, player, tests) with okay-build instead of CMake, off by default (CMake stays the build).
Written from the public description of the CMake language (commands, quoting, `${}` expansion, `if()` conditions, lists, functions and macros) and from what ctest is for; no CMake, Ninja or other build-tool code or tests were read or used (rule 19a). The reader is `tools/okaybuild/cmake_reader.hpp`, included by `okay-build.cpp`.

**Measured before (at `69495456`).** `okay-build native` wrote 5 targets (`okay_crypto`, `okay_platform`, `ufbx`, `okay_engine`, `test_math`): 347 compile commands, 1 test program, no options, no per-file settings, a Debug-like `-O0 -g0` and libsodium switched on by hand. CMake's own list for the same tree is 1,183 sources in 820 targets with 834 `add_test` lines. Against CMake's `compile_commands.json` none of the 347 had the same `-D`/`-I`/flags (the hand-picked `-O0` and sodium defines, and no `shared` include folder on most). Nothing read the CMake files except to order archive members.

**What changed.**
- **`okay-build cmake <repo> [-o FILE] [--opt NAME=VALUE] [--build-type T] [--fetched NAME=DIR] [--audit]`.** Runs the CMake language subset the project uses and writes a manifest: variables and cache options (`--opt` is `-D`), `if/elseif/else`, `foreach` (lists, `IN LISTS`, `IN ITEMS`, `RANGE`), `function` and `macro` (parameters, `ARGN`, `PARENT_SCOPE`), `list`, `string` (including regex), `file(READ/STRINGS/GLOB)`, `find_package` (SDL2, Threads, OpenGL, PkgConfig), `find_program`, `find_path`, `check_include_file_cxx`, `pkg_check_modules` (asks `pkg-config`), `execute_process`, `add_subdirectory`, `include`, `FetchContent_*` (never downloads; `--fetched` names a folder), `add_executable`, `add_library` (static, interface, alias), every `target_*` command with PUBLIC/PRIVATE/INTERFACE propagation, `set_target_properties`, `set_source_files_properties`, `add_compile_options/definitions`, `add_link_options`, `include_directories`, `add_test`, `set_tests_properties`, and the generator expressions the project uses (`$<BUILD_INTERFACE>`, `$<TARGET_FILE>`, `$<TARGET_PROPERTY:t,INTERFACE_INCLUDE_DIRECTORIES>`). Anything else is counted under "not understood" in `--audit`, never guessed. A target whose source is missing or needs a download is left out with the reason (and so is everything that depends on it). The account-server defines are not written into the manifest: `account_defines = on` is written instead (they are read at build time, as before). The compiler's own include folders (`/usr/include`) are not passed as `-I`, because the compiler already searches them in its own order. `CMAKE_BINARY_DIR` reads `{bin}`, which a build turns into `<build dir>/<outdir>/ctest`.
- **Manifest: per-target `file_flag`, `file_define`, `file_include`** (`file_flag = src/a.cpp -w`): extra words for one source only (CMake's `set_source_files_properties`). A C file does not get `-std=c++17`/`-std=gnu++17`.
- **Manifest: `ctest NAME` blocks** (`cmd`, `cwd`, `timeout`, `will_fail`, `skip_code`, `serial`). In words, `{src}` is the checkout, `{bin}` the test folder and `{target:NAME}` a built program (it is built first). **`okay-build ctest [REGEX...]`** builds what the chosen tests need, runs them `-j` at a time (serial ones afterwards) in their folder (default `<build>/<outdir>/ctest/<folder of the CMakeLists>`, created), and prints `PASS`/`FAIL`/`SKIP` with the last 30 lines of a failing test. Default limit 1500 s (CTest's), `TIMEOUT` and `--timeout` override. Not done on Windows.
- **`output_name = NAME`** on a target (CMake's `OUTPUT_NAME`, for example the launcher that is built as `OkaySpace`); `ctest` and `package` follow it.
- **`tools/okaybuild/examples/compare_cmake_commands.py`** compares okay-build's compile commands with CMake's `compile_commands.json`.

**Measured after (Linux, g++ 13.3, this machine).**
- Default options: `okay-build cmake . --audit` reads the whole project in about 0.05 s: 3,447 commands run, 54 options, **815 of 820 targets written (the other 5 are 4 aliases and 1 generator target, not build targets), 1,338 compile commands, 830 of 834 tests written** (the 4 left reuse a name an earlier test already has; only the first is written). Not understood: 5 `string(MD5)` calls (their result is not used for any target).
- With the editor and player on (`--opt OKAY_BUILD_EDITOR=ON --opt OKAY_BUILD_PLAYER=ON --fetched imgui=...`): 818 targets, 1,391 compile commands.
- Against real CMake (configured on the same tree with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`): default options **1,183 of 1,183 sources have identical `-D`, `-I` and flag sets**; with editor, player and launcher on **1,217 of 1,217** (the build folder inside `OKAY_TEST_OUTPUT_DIR` is ignored on purpose, the account defines are hidden on purpose). Found and fixed on the way: unquoted `NAME="x"` words keep their quotes, `file(STRINGS)` escapes `;`, `string(REGEX REPLACE)` argument order, `$<TARGET_PROPERTY>`, `-I/usr/include`, `-std=c++17` on a C file. The script compares the first command of each source: a source compiled in two targets is compared once.
- Tests: `tools/okaybuild/test_okaybuild.cpp` has 378 checks that pass here and 7 that need wine or libsodium (the same 7 as before this round; the first 340 checks are unchanged). 42 new checks (a second test covers comments, [[bracket]] arguments, macros, functions, lists, string commands and conditions): a synthetic project with options, `if`, `foreach`, functions, regex, an ALIAS, an INTERFACE library, per-file defines with quotes, a skipped target, `--opt`, `message(FATAL_ERROR)`, then building it and running its tests (PASS, WILL_FAIL, SKIP, a failing test with output, a timeout, a bad filter, a working-folder check).
- Real run, no CMake or Ninja: `okay-build cmake . --build-type None`, then `okay-build -f build.cmake.okay -j2 --build-dir /tmp/tree test_math` built the engine and `test_math` from that manifest: 351 of 351 steps, cold, `-O0`, `-j2` (wall time not meaningful: the machine was also running the test suite), tree 1.6 GB. Then `okay-build ... ctest` ran `math`, `sandbox`, `scene`, `serialize`, `structure_version`, `parent_cycles`, `jobs` and `versions_index` (a Python script test): 8 of 8 PASS. This machine has no libsodium, so the engine built the way CMake configures it here (own crypto fallback, `OKAY_HAVE_SODIUM` not set). The editor, player and launcher were compared at the compile-command level only and were **not built or linked** with okay-build (the link order of the mutually dependent UI libraries is unverified).
- **Not done:** Windows and MSVC (the reader compiles for MinGW; nothing was run), `ctest` on Windows, downloads (FetchContent sources are given with `--fetched`), `install`, `configure_file` and custom commands (counted in `--audit`), shared and object libraries (the target is skipped with its reason), per-test `ENVIRONMENT`/`LABELS`/`DEPENDS`, test run time in JUnit XML for `ctest`, the CMake regular-expression dialect (the C++ standard library's is used; the patterns the project uses behave the same), and link-step flags that CMake adds (`-rdynamic`, RPATH). Source compiled by two targets is checked once by the comparison script. Next gaps in order: build and link the editor, player and launcher; compare the linked programs; `ctest` JUnit and `DEPENDS`; Windows run.
- How to try it: `docs/try_it/okay-build.md`.
