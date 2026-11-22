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

## Manifest keys
`name`, `title`, `area`, `description`, `default`, `core`, `requires` (comma separated), `provides`, `components`,
`serializers` (scene records), `vs_group`, `editor`, `sources` (space separated file patterns), `tests` (space separated test
name patterns), `planned` (files another branch is adding; read by nothing yet). The build reads `name`, `core`, `requires`,
`sources` and `tests`; the rest is for the engine's manifest parser (stage 0, PR 2).
