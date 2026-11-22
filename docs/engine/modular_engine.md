# Modular engine (internal notes)

Design, plan and decisions: `../design/modular_gameplay_contracts.md` (sections 9 to 11). Stage by stage state:
`../design/modular_status.md`.

## What is on main (stage 0, PR 1: module groups in the build)
- 40 module manifests, `engine/modules/<name>/module.okaymodule`. Each says its name, what it requires and which engine files
  are its own (`sources=`, patterns relative to `engine/`; the most specific pattern wins when two match).
- `cmake/OkayModules.cmake`: `okay_modules_read()` reads the manifests and fails configure on an unknown requirement or a
  requirement cycle; `okay_modules_group(okay_engine)` puts each engine source in its module (or core) and writes
  `okay_modules.tsv` in the build folder; `okay_modules_label_tests()` gives every test the label `module:<name>`
  (`tests=` patterns in the manifest; `module:core` when no module claims it). `ctest -L module:physics3d` runs one module's tests.
- Nothing is stripped and nothing moved: `okay_engine` keeps its one source list in the same order, so the programs are built
  from the same objects as before.

## Stage 0, PR 2: the module list at run time and the dependency check
- `okay/Modules/ModuleManifest.hpp` parses a manifest (unknown keys are ignored, so older programs read newer manifests).
- `okay/Modules/ModuleRegistry.hpp`: `ModuleRegistry::Get()` is the list of modules in this build, filled from
  `modules_enabled.cpp`, which `okay_modules_generate()` writes from the manifests (their text is embedded). It answers
  what exists, what each requires (`TransitiveRequires`, `DependencyOrder`) and what is wrong with the list (`Validate`).
  Every module is on in this stage. A module's `engine/modules/<name>/register.cpp` (none yet) gets a `ModuleContext`
  through `Register_<name>`, called in dependency order on first use.
- The player logs `[okay] modules: 40 modules, 40 on` at start.
- `scripts/check_modules.py` (the dependency check): a file of module A may include a header of B only if B is A, core,
  or in A's requirements; core includes no module; no requirement cycles; manifest patterns, components and records
  exist; engine code does not include `Okay.hpp`. Today's ~800 forbidden includes (the serializer, the script VM and the
  renderer name every component) are listed in `scripts/module_deps_baseline.txt`; a new one is reported. The list may
  only shrink: `--update-baseline` is for after a fix. CI runs it in report mode (a warning); design PR 14 makes it blocking.

## Manifest keys
`name`, `title`, `area`, `description`, `default`, `core`, `requires` (comma separated), `provides`, `components`,
`serializers` (scene records), `vs_group`, `editor`, `sources` (space separated file patterns), `tests` (space separated test
name patterns), `planned` (files another branch is adding; read by nothing yet). The build reads `name`, `core`, `requires`,
`sources` and `tests`; the engine's parser reads the rest (`tests` and `planned` it ignores).
