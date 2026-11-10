# Asset database

`okay::AssetDatabase` (`okay/Core/AssetDatabase.hpp`) is the data layer an asset browser sits on: persistent GUIDs
in a sidecar meta file per asset, move and rename tracking, a dependency graph, typed import settings, a
thumbnail cache and a search / filter / sort / page query. There is no UI here; an editor window is a thin layer
over these calls. The editor's Project panel uses it (see `project_panel.md`); it does not change how assets are loaded
(see `asset_manager.md` for that).

Headers (none include `<mutex>`, `<thread>` or `<condition_variable>`):

| Header | What |
|---|---|
| `okay/Core/AssetDatabase.hpp` | `Guid`, `AssetRecord`, `AssetDatabase` (scan, lookup, graph, search), the extractors |
| `okay/Core/ImportSettings.hpp` | `AssetType`, `ImportSettings` (texture, model, audio), versions and migration |
| `okay/Core/ThumbnailCache.hpp` | `ThumbnailCache`, `ThumbnailGenerator`, `BoxDownscale`, placeholders |

A database is not thread-safe: use one from one thread (the editor's main thread).

```cpp
#include "okay/Core/AssetDatabase.hpp"
#include "okay/Core/ThumbnailCache.hpp"

okay::AssetDatabase db;
okay::ScanReport rep = db.Scan("MyGame/Assets");        // creates missing .okaymeta files, follows moves
okay::Guid hero = db.PathToGuid("art/hero.png");        // "art/hero.png" is relative to the scanned root
std::string now = db.GuidToPath(hero);                  // still right after the file was moved
auto users = db.UsedBy(hero);                           // scenes, prefabs and materials that use it
auto ship  = db.BuildClosurePaths({"scenes/main.okayscene"});   // exactly what a build must include
```

## GUIDs and meta files

Every asset file `foo.png` gets a sidecar `foo.png.okaymeta` (text). Commit it with the asset; it is what keeps
references valid when the file moves. The asset itself is never opened for writing.

```
okaymeta 1
guid 0123456789abcdef0123456789abcdef
type texture
hash 9a3f0c11d2e4b7a8
size 20311
label hero
label ui
importer texture 2
set srgb 1
set filter bilinear
set wrap repeat
set maxSize 2048
set mipmaps 1
set compress 0
```

- `okaymeta <n>` is the meta layout version (`AssetDatabase::kMetaVersion`, 1 today). A meta written by a newer
  layout is read as far as it is understood and **never rewritten** (`AssetRecord::metaReadOnly`); changing its
  labels or settings fails with an error instead of damaging it. Unknown lines in a same-version meta are kept.
- `hash` and `size` describe the asset content (FNV-1a 64 of the bytes; files over 512 MB hash their first and last
  4 MB plus the size). They let a meta find its asset again after a move, even in a later session, and are how a
  source change is noticed. Turn off writing them with `ScanOptions::storeHashInMeta = false` (then only moves
  within one session are matched by content).
- A meta that cannot be read is moved aside as `foo.png.okaymeta.corrupt` and replaced; nothing is deleted.
- Hidden files and folders (leading `.`), `*.okaymeta`, and the extensions `okayasset part tmp bak swp corrupt`
  are not assets. `ScanOptions::ignoreDirs` skips more folders by name. Symlinked folders are not followed.
- Paths in the database are relative to the scanned root, with `/`. Case is folded for lookups on Windows only,
  the same policy as `AssetManager::Normalize`.

### Scan

`Scan(root)` is incremental: call it again after files change (an editor does so when its window regains focus).
It returns a `ScanReport`: counts, `moved`, `duplicates`, `orphanMetas`, `removed`, the `reimports` it raised and
`warnings` (also sent to the engine log unless `ScanOptions::logWarnings` is off).

Moves and renames are detected in this order, so references stay valid:

1. **Same GUID, new path.** The asset and its meta moved together (git, the editor, a sync tool). Reported as
   `how = "guid"`.
2. **Same content.** Only the asset moved and the meta was left behind (an orphan meta), or both were lost but the
   database remembers the asset from earlier in the session. The meta is renamed next to its asset again (or
   rewritten from memory with its labels and settings). `how = "hash"`. A meta that was deleted while the asset
   stayed put gets its GUID back from memory.
3. **Same name, edited.** Exactly one orphan meta and one new file share a file name and type: the file takes the
   GUID, with a warning. `how = "name"`. Switch off with `ScanOptions::matchByName = false`.

Anything else is a new asset: a fresh GUID and a new meta, and an `Added` reimport signal.

- **Duplicates.** Copying an asset together with its meta gives two metas with one GUID. The original keeps it
  (the file the database already knew, else the one with the older meta); the copy gets a new GUID and a warning,
  and keeps the labels and settings it was copied with (`ScanReport::duplicates`).
- **Orphan metas.** A meta whose asset is gone is reported (`orphanMetas`, `OrphanGuids()`) and left on disk.
  `RemoveOrphanMetas()` or `ScanOptions::deleteOrphanMetas` deletes them.
- **Old paths keep resolving.** After a move `Resolve("art/hero.png")` still returns the asset (with
  `viaRedirect = true`), and the dependency graph keeps the edge. `MoveAsset(from, to)` moves an asset and its meta
  together; use it for renames made inside the editor.

Lookups: `GuidToPath`, `PathToGuid` (exact project path; also accepts an absolute path inside the root, `./` and
`..` forms and backslashes), `Resolve(guidOrPath)` (a GUID in text form, a path, or an old path), `Find`, `All`,
`AbsolutePath`, `ToProjectPath`.

## Dependencies and references

Each text asset with a registered extractor is read when it is new or its content changed, and the paths in it
become edges of a graph keyed by GUID. A reference is matched to a file as written (relative to the root), then
relative to the referring file's folder, then without a leading project-folder name (`Assets/x.png` when the root
is `.../Assets`), then through the old-path table of moved assets. Packed (`DataPack`) files are read through
`DataPack::Unpack`.

Built-in extractors:

| Extractor | Extensions | What it reads |
|---|---|---|
| `scene` | `okayscene`, `okayprefab` | Quoted strings that end in a known asset extension, and the paths on `prefab`, `srcscene`, `netprefab` and `visualscriptpath` lines. `gameobject`, `name` and `tag` lines are skipped, so a name like `"hero.png"` is not a reference. The scene writer quotes every path field (`CollectAssetPaths` reads the same fields from a live scene), and a test checks both agree on a scene that uses 17 component types. |
| `material` | `okaymat` | The `texture <path>` line. |
| `text` | `okayanim`, `okayclip`, `okaydialog`, `okayquest`, `okaybt`, `okayvs`, `okayseq`, `okayloot`, `okayitems`, `okayrecipes`, `okaydata`, `okaynav`, `gltf` | Quoted strings, `key=value` values and bare words that end in a known asset extension (dialogue portraits and sounds, behavior tree parameters, cutscene audio cues, glTF `uri`s). |

A value counts as an asset path only when its extension belongs to a known asset type (`AssetTypeFromExtension`),
it has no control characters, is not a URL or `data:` URI, and a value with spaces has a folder part (so a
dialogue sentence is not a path). `.txt` and `.json` are never taken as references. Add more with
`RegisterExtractor(name, {"ext"}, fn)` (`fn(assetPath, text, out)` pushes `FoundRef`s with the path and its span
in the text); several extractors can cover one extension.

Queries:

| Call | Result |
|---|---|
| `DependenciesOf(guid, recursive)` | What an asset references, each once, in order of first appearance. |
| `UsedBy(guid, recursive)` | What references it, sorted by path. |
| `BuildClosure(roots)` / `BuildClosurePaths(roots)` | The roots and everything they reach, each once, breadth first. Cycles are fine. Ship exactly these. |
| `FindUnused(roots)` | Assets not reachable from `roots`; empty roots means every scene. |
| `FindUnreferenced()` | Assets nothing references. |
| `FindMissing()` | References to files that exist neither in the index nor on disk (`MissingRef`: referrer, text as written). |
| `FindStaleReferences()` | References that only resolve through an old path of a moved asset. |
| `FixStaleReferences()` | Rewrites those paths in the text files (only the path text changes; the file is written atomically; relative references stay relative; packed files are skipped). Raises `SourceChanged` for each file. |

## Import settings

`ImportSettings` holds the typed block for the asset's type; scenes, prefabs and other types have none.

| Type | Field | Meta key | Values (default) |
|---|---|---|---|
| texture | `sRGB` | `srgb` | 1 / 0 (1) |
| | `filter` | `filter` | `point`, `bilinear`, `trilinear` (`bilinear`) |
| | `wrap` | `wrap` | `repeat`, `clamp`, `mirror` (`repeat`) |
| | `maxSize` | `maxSize` | 0 (no limit) or 32..16384 (2048) |
| | `mipmaps` | `mipmaps` | 1 / 0 (1) |
| | `compress` | `compress` | 1 / 0 (0), asks the renderer for a compressed format |
| model | `scale` | `scale` | 0.0001..10000 (1) |
| | `normals` | `normals` | `import`, `calculate`, `flat` (`import`) |
| | `importAnimations` | `importAnimations` | 1 / 0 (1) |
| | `smoothingAngle` | `smoothingAngle` | 0..180 degrees (60) |
| audio | `loadMode` | `loadMode` | `decode`, `stream` (`decode`) |
| | `forceMono` | `forceMono` | 1 / 0 (0) |
| | `quality` | `quality` | 0..1 (0.7) |

Missing keys take the default; a bad value takes the default and adds a warning; numbers are clamped. Keys this
build does not know are kept (`ImportSettings::extra`) so a round trip loses nothing.

**Versions and migration.** The `importer <type> <version>` line says which layout wrote the block
(`ImportSettings::CurrentVersion`: texture 2, model 1, audio 2). An older block is migrated when read: texture v1
(`linear`, `filter nearest|linear`, `maxsize`, `mips`) and audio v1 (`stream`, `mono`, `quality` in percent) become
the current keys. `Scan` rewrites such a meta in the current layout (`ScanOptions::upgradeMetas`, on by default;
off leaves old metas byte for byte as they are and migrates in memory only). Migration raises no reimport signal.

### Reimport signal

The database tells consumers when an asset must be imported again:

| Reason | When |
|---|---|
| `Added` | A new asset got its meta. |
| `SourceChanged` | The content hash differs from the one in the meta or from the last scan (a scan, `RefreshFile`, a hot reload, or `FixStaleReferences` on a text file). |
| `SettingsChanged` | `SetSettings` stored different values, or a scan found the meta's settings edited on disk. |

Read them with `SetReimportCallback(fn)` (called after the change is stored), `TakeReimports()` (drains a queue)
or `ScanReport::reimports`. `SetSettings(guid, settings)` stores and signals only when the values differ.
`SetLabels / AddLabel / RemoveLabel` write the meta but are not a reimport (labels do not affect an import).

**Hot reload.** `db.AttachHotReload(assetManager, chain)` installs a reload callback on an `AssetManager`: when its
hot reload reloads a file in place, the database rehashes that file (`RefreshFile`), refreshes its references and
raises `SourceChanged`, then calls `chain`. It replaces the manager's reload callback, and the callback keeps a
pointer to the database, so clear it (`SetReloadCallback({})`) before the database goes away. `ReloadHook()`
returns the same function for hand wiring. The database does not itself ask the manager to reload on a settings
change: the consumer reacts to `SettingsChanged` (for example by loading the asset again with the new options).

## Search for an asset browser

`Search(AssetQuery)` returns an `AssetPage` (`items`, `total`, `page`, `pageCount`). Every filter that is set
must match:

| Field | Meaning |
|---|---|
| `text`, `matchFolders` | Case-insensitive substring of the file name (or of the whole path). |
| `types`, `extensions` | Any of these types or extensions (`.PNG` and `png` are the same). |
| `labels`, `anyLabel` | All of the labels (any, with `anyLabel`); case-insensitive. |
| `folder`, `recursive` | Only under a folder (project path, either slash), with or without sub-folders. |
| `modifiedAfter`, `modifiedWithinSeconds`, `now` | Recently modified; `now` overrides the clock. |
| `referencing`, `referencedBy`, `transitive` | Assets that use a given asset, or that it uses. |
| `unusedOnly`, `withMissingRefsOnly` | Nothing references it; it has a reference to a missing file. |
| `sort`, `descending` | `Name` (default, case-insensitive), `Path`, `Type`, `Modified`, `Size`; ties by name then path. |
| `pageSize`, `page` | 0-based pages; a page past the end returns the last one. |

`CountByType()` and `CountByType(query)` give the per-type counts for a filter bar, `AllLabels()` the label chips.
Result pointers stay valid until the next `Scan` or change.

## Thumbnails

`ThumbnailCache` returns a picture per asset, keyed by GUID and content hash, so an edited file never shows a
stale picture. It has a least-recently-used memory layer (`maxEntries`, `maxBytes`) and, with `Options::diskDir`, a
PNG per thumbnail in `<dir>/<first two hex digits of the guid>/<guid>_<hash>_<size>_<tag>.png`; stale files of the
same GUID are removed when a new one is written. Bump `Options::tag` to invalidate the disk cache when a generator
changes.

```cpp
okay::ThumbnailCache::Options o;
o.size = 128;
o.diskDir = project + "/.okay/thumbs";            // hidden, so the scan skips it
okay::ThumbnailCache thumbs(o);
auto t = thumbs.Get(db, hero);                     // t.image is never null; t.source says where it came from
thumbs.AddGenerator({"okaymesh"}, [](const std::string& path) { return RenderMeshPreview(path); });
```

`Get` order: memory, disk, a generator, then a placeholder. Generators (`render(path) -> Image`, asked newest
first) return an empty image when they cannot; the result is scaled down with `BoxDownscale` (area-weighted, alpha
aware, never enlarges). The built-in `ImageThumbnailGenerator` handles `png jpg jpeg bmp tga gif psd hdr` through
`Image::Load`. Types without a generator get a coloured placeholder tile with the extension; a failing generator
(or an unreadable image) gets a red one, and the failure is remembered for that content so it is not retried every
frame. `Thumb::placeholder` and `Thumb::failed` tell them apart. `Invalidate(guid)`, `Clear()`, `ClearDisk()` and
`GetStats()` (memory hits, disk hits, generated, placeholders, failures, evictions) round it out.

## Not covered

- No UI here. The editor's Project panel is the one consumer (see `project_panel.md`); the build/export code does not call this yet.
- References are found by text scanning, not by loading. A path built at runtime, or stored in a binary file, is not
  seen; a name that happens to end in a known extension inside a quoted string can be taken for a path.
- Binary formats are not parsed: a `.fbx` that points at external textures has no edges (a `.gltf` does).
- Import settings are stored and signalled but no importer consumes them yet.
- GUID references inside scene files do not exist yet (scenes store paths); `FixStaleReferences` is the bridge.
- Meta files are matched by content hash, which is not a cryptographic hash.

Tests: `tests/test_asset_database.cpp` (ctest name `asset_database`).
