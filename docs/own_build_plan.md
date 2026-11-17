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
- Not done: Windows host (fork/exec only), system headers are not tracked (-MMD), no spaces in paths, FNV-1a 64-bit hash
  (fine for change detection, not cryptographic), no pools, no restat flag, no generator steps, no `-k`, no build-dir option
  (outputs go next to the manifest), hashes all inputs every run (no stat shortcut).

## What Ninja/Make do that okay-build does not yet
Pools (limit link jobs), `restat` for generators, generator rules (custom commands with declared outputs), `-k` keep going,
`-t` tools (graph, targets, compdb of any rule), console pool, response files for huge command lines, jobserver, regeneration
of the build file when it changes, Windows process handling, very fast no-op checks on huge trees (Ninja ~8 ms vs ours ~27 ms
on 51 files; the gap is hashing and process start, and grows with tree size).

## Stage B (design only): CMake importer
Run the normal CMake configure once with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`. `okay-build import` reads
`compile_commands.json` (file, directory, arguments/command) and turns each entry into a compile action keyed by its output
(`-o`). Link and archive commands are not in that file: read them from `CMakeFiles/<t>.dir/link.txt` (plain command lines)
and map objects to targets by directory name. Emit `build.okay` with per-target flags factored out of the common set. Goal:
build the engine with okay-build and compare outputs/timings with Ninja, with CMake untouched. Needs an `action` block in the
manifest (raw command, inputs, output, optional depfile) because imported commands are not our structured targets.

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
