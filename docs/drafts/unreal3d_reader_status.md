# Unreal 1 mesh reader: paused draft (second Claude, 2026-11-15)

Status: **paused at a clean point** (the coordinator moved this reader queue behind the unfinished-features list).
`engine/src/Render/UnrealReader.cpp` is a first version that is **not compiled into the engine, not wired into the
importer and has no tests** on this branch. Nothing else is changed.

- Written from the public format description "Unreal File Format" (PantherD of Team Panther, edited by Paul Bourke,
  paulbourke.net/dataformats/unreal) only. One web-search result summary quoted a few lines of another importer's Unreal
  loader; that page was not opened and those lines were not used.
- Probed by hand (standalone, ASan/UBSan) on the 2 real models of Assimp's test tree (`models/3D/box`,
  `models-nonbsd/3D/mar_rifle`, through the `_d.3d`, `_a.3d` and `.uc` entry points): both read, faces point outward
  (positive signed volume), the class file's scale and texture file are applied.
- Left to do: wiring (slot 42, `OKAY_OWN_UNREAL3D`, `.3d` and `.uc`), the default-switch test, unit and hostile tests with a
  counting allocator, mutation checks, the comparison with Assimp, and the docs listed in the queue comment.
