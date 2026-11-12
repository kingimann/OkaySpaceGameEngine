# 20-35. Planned areas

These 16 areas are the features the owner asked for and ranked (priority is in each area heading). Each sub-area says what the code already does, so nothing is built twice, what is missing, and the work in small shippable pieces. Most engine-side data layers already exist; the editor windows that use them mostly do not.
Facts were checked against origin/main as of 2026-11-09: #571 (core wave 4), #572 (undo and recovery, testing, save framework), #574 (profiling and debugging data layers, optimization, localization tools, accessibility), #575 (scene hierarchy and validation) and #578 (core wave 5: nested prefabs, property registry, jobs, scene text tools) are all merged. 2.851.0 is live (it holds #571 and #572 from 2.849.0, #574, #575, #578 and #579 from 2.850.0, and the loop and time pass #581); 2.852.0 is being cut (PR #588) with the scene graph, timers and tweens and loop and time waves #586, #587 and #585. A number in brackets after a part is the merged PR that added it.
The pattern across these areas: the engine-side data layers now exist and are tested; almost none of them is called by the editor, the renderers or the player yet. Each Status line says which. "In flight" now only means a branch or PR that is not on main (Grok's animation work, the updater fix #584, the 2.852.0 release PR #588).
About 61 sub-areas. Work packages use S (a day or less), M (a few days), L (a week or more), then what each one depends on.

## 20. Asset management (Critical)

### 20.1 Asset browser
- **What:** The editor window where you find, preview, organise and use project files. Today that is the Project panel; its listing, search, filters, counts, folder tree, dependency lookups and pictures come from the asset database and the thumbnail cache.
- **Parts:**
  - 20.1.1 Folder tree, grid and list views - `editor/main.cpp` - browse the Assets folder with breadcrumb, back/forward, favourite folders, tile size and sort saved in `okay_projectview.txt`
  - 20.1.2 Search and smart filters - `editor/main.cpp` - "All Models/Textures/Audio/...", Recently Changed, Unused Assets, colour labels; search words `t:model`, `l:red`, `ext:png`
  - 20.1.3 Asset actions - `editor/main.cpp` - create script/scene/material/prefab, import files, rename (F2), delete with confirm, copy/cut/paste, multi-select, drag assets onto Inspector fields or into the scene
  - 20.1.4 Details strip and previews - `editor/main.cpp` - preview, size, dimensions, path, GUID, dependency and user counts, tags, a broken-reference warning, "used in scene", Find References, Show Dependencies and Find References In Scene for the selected asset
  - 20.1.5 Asset Manager window - `editor/main.cpp` - lists Marketplace downloads (`.okayasset` sidecars written by the launcher) and imports them into the open project
  - 20.1.6 Database search model - `Core/AssetDatabase`, `editor/ProjectBrowser.*` - `Search(AssetQuery)` (folder, label, recent, unused, used-by, depends-on) behind `Browser::List`; the panel adds name, type-word and extension matching on top
- **Code:** `editor/main.cpp`, `Core/AssetDatabase`, `Core/AssetDatabaseSearch`, `Core/AssetDatabaseInternal`
- **API:** `AssetDatabase::Search`, `AssetQuery`, `AssetPage`, `CountByType`, `AllLabels`; editor: View > Panels > Project and Asset Manager
- **Editor:** Project panel (`DrawProject`), Asset Manager window (`DrawAssetManager`)
- **Docs:** `docs/asset_database.md`, `docs/editor.md`
- **Tests:** `asset_database`, `project_panel`, `project_panel_db` (the model: listing, search, references, thumbnails, a 5,000-asset refresh)
- **Status:** Partial - the Project panel is driven by `AssetDatabase` and `ThumbnailCache`; the rows below are what is still open.
- **Exists today:** Project panel in `editor/main.cpp` and `editor/ProjectBrowser.*`: tree, grid and list from the database, search and type filters, colour labels, tags, favourites, Unused Assets from the dependency graph, clipboard, rename, delete confirm, info strip. Asset Manager window for Marketplace downloads.
- **Work packages:**
  1. S - Own one `AssetDatabase` in `EditorState`, call `Scan` when a project opens and when the editor window regains focus; no UI change. Depends on: #568 (on main).
  2. M - Drive the Project panel's list, filters and counts from `AssetDatabase::Search` and `CountByType`, keeping the view prefs. Depends on: package 1.
  3. S - Replace the "Unused Assets" smart filter and "used in scene" text scan with `FindUnused` / `UsedBy`. Depends on: package 2.
  4. S - Add GUID, type, import settings summary and "Used by" list to the details strip. Depends on: package 2, 20.3.
  5. Done - Broken References list (missing and stale) with a Fix button and undo (`Browser::MissingRefs`, `StaleRefs`, `FixStaleRefs`).
- **Open:** Two sources of truth for labels (the editor's own colour labels in `.okaylabels` vs tags in `.okaymeta`); changes outside the editor are seen by polling the incremental scan (2 s or more), there is no file watcher; Unused Assets reads the references the database extracts (scenes, prefabs, materials, scripts, clips, dialogue, quests and the other text formats), not paths a script builds at run time; the window has no automated UI test (the model, including the missing and stale lists and Fix, is tested).
- **Related:** 20.2, 20.3, 20.5, 15.4, 3.6
- **Words:** Project panel, asset browser, content browser, Assets folder, favorites, smart filters, Recently Changed, Unused Assets, asset labels, Asset Manager, Marketplace downloads, okayasset, AssetQuery
- **Seen as:** cannot find a texture or model; labels or favourites vanish; downloaded asset not in my project; "unused" list shows things that are used
- **Ask me like:** "list the assets that depend on this prefab"; "show a Fix button for stale references"

### 20.2 Dependencies and references
- **What:** Knowing which assets use which other assets, so a build ships exactly what a game needs and a moved or deleted file shows up before it breaks a scene.
- **Parts:**
  - 20.2.1 Dependency graph and extractors - `Core/AssetDatabaseDeps` - reads scenes, prefabs, materials, dialogue, quests, behavior trees, glTF for file paths; custom extractors can be registered
  - 20.2.2 Closure and reports - `Core/AssetDatabase` - `BuildClosurePaths`, `FindUnused`, `FindUnreferenced`, `FindMissing`, `FindStaleReferences`, `FixStaleReferences`
  - 20.2.3 Live-scene asset list - `Scene/SceneSerializer` - `CollectAssetPaths` of an open scene, used by Build Game to copy files into the build
  - 20.2.4 Editor reference tools - `editor/main.cpp` - Find References In Scene (by file name), Unused Assets filter (text scan)
- **Code:** `Core/AssetDatabase`, `Core/AssetDatabaseDeps`, `Scene/SceneSerializer`, `editor/main.cpp`
- **API:** `AssetDatabase::DependenciesOf`, `UsedBy`, `BuildClosure`, `FindMissing`, `RegisterExtractor`; `SceneSerializer::CollectAssetPaths`
- **Editor:** Project panel (Unused Assets, Find References In Scene), Build Settings (asset copy)
- **Docs:** `docs/asset_database.md`
- **Tests:** `asset_database`, `assets`
- **Status:** Partial - the graph and reports are written and tested but nothing calls them; Build Game uses the older per-scene list.
- **Exists today:** `AssetDatabase` keeps a dependency graph keyed by GUID, gives the exact closure of a set of scenes, unused and missing reports and a fixer for stale paths. Build Game (`editor/main.cpp`, packing loop) copies assets named by `CollectAssetPaths(ed.scene())`, the open scene only; extra scenes in the build are copied as files but their assets are not collected there.
- **Work packages:**
  1. S - Build Game asks `BuildClosurePaths` for every scene in the build (and prefabs, dialogue, quests) instead of the open scene only. Depends on: 20.1 package 1.
  2. S - Build report lists "referenced but missing" files from `FindMissing` before building. Depends on: package 1.
  3. M - Editor "Dependencies" panel: select an asset, see what it uses and what uses it, jump to either. Depends on: 20.1 package 2.
  4. S - Rename or move inside the editor goes through `MoveAsset` so references are kept; offer `FixStaleReferences` after external moves. Depends on: 20.3.
  5. M - Extractors for the remaining binary or custom formats (FBX external textures, scripts that load files by name). Depends on: package 1.
- **Open:** possible bug: Build Game collects assets from the open scene only (extra scenes in the build list are copied but not scanned); references are found by reading text, so a path built at run time is never seen; binary FBX files have no edges.
- **Related:** 20.1, 20.3, 18.1, 15.7, 1.5
- **Words:** dependencies, references, used by, unused assets, missing references, build closure, asset graph, CollectAssetPaths, Build Game copies assets
- **Seen as:** the built game is missing a texture or sound; the build contains files nothing uses; a scene shows pink or empty after I moved a file; Find References finds nothing
- **Ask me like:** "make Build Game ship exactly the assets the scenes in the build use"; "list every asset nothing uses"

### 20.3 GUIDs
- **What:** A permanent identity for every asset file, kept in a small `.okaymeta` file beside it, so references survive moves and renames.
- **Parts:**
  - 20.3.1 Meta files and scan - `Core/AssetDatabase` - `foo.png.okaymeta` with guid, type, content hash, labels, import settings; incremental `Scan`
  - 20.3.2 Move, rename and duplicate tracking - `Core/AssetDatabase` - match by same GUID, same content, or same name; copied metas get a new GUID; old paths keep resolving
  - 20.3.3 Resolve and lookup - `Core/AssetDatabase` - `GuidToPath`, `PathToGuid`, `Resolve(guidOrPath)`
  - 20.3.4 Object GUIDs (different thing) - `Scene/SceneSerializer` - each GameObject has a persistent id (`GameObject::Guid()`, `Scene::FindByGuid`), merged in #570
- **Code:** `Core/AssetDatabase`, `Core/AssetDatabaseInternal`, `Scene/SceneSerializer`
- **API:** `Guid`, `AssetRecord`, `AssetDatabase::Scan`, `GuidToPath`, `PathToGuid`, `Resolve`, `MoveAsset`; `GameObject::Guid`
- **Editor:** none yet
- **Docs:** `docs/asset_database.md`, `docs/prefabs.md`
- **Tests:** `asset_database`, `scene_ids_prefabs`
- **Status:** Partial - the meta files and tracking are written and tested; scenes still store plain paths, and the editor never creates metas.
- **Exists today:** `AssetDatabase::Scan` creates and repairs `.okaymeta` files and reports moves, duplicates and orphan metas; the dependency graph survives moves. Scene files do not contain asset GUIDs (`docs/asset_database.md` says so); `FixStaleReferences` rewrites paths as a bridge.
- **Work packages:**
  1. S - Create metas when a project opens and show "meta written" in the Console; commit-friendly defaults (`.okaymeta` not ignored). Depends on: 20.1 package 1.
  2. M - Editor file operations (rename, move, duplicate, delete in the Project panel) call `MoveAsset` and keep the meta together. Depends on: package 1.
  3. L - Scenes and prefabs store asset GUIDs next to the path and resolve GUID first, path as fallback; old files keep loading. Depends on: package 2, scene serializer (1.5).
  4. S - Check script: scene or prefab referencing a GUID with no asset reports `MissingAsset`. Depends on: package 3, 33.2.
- **Open:** meta files are matched by content hash (FNV-1a, not cryptographic); files over 512 MB hash only head and tail; a project already under version control needs a note telling people to commit the metas; no editor UI shows a GUID.
- **Related:** 20.1, 20.2, 21.4, 28.1, 1.5
- **Words:** GUID, meta file, okaymeta, asset id, stable reference, rename keeps references, move asset, duplicate asset, FindByGuid
- **Seen as:** renaming a file breaks scenes that used it; two copies of an asset fight over one id; merge conflicts in id files
- **Ask me like:** "make scenes reference textures by GUID"; "keep references when I move a file in the Project panel"

### 20.4 Import settings
- **What:** Per-asset options that decide how a file is brought into the engine (texture filtering and size, model scale and normals, audio streaming), stored in the asset's meta file.
- **Parts:**
  - 20.4.1 Typed settings and migration - `Core/ImportSettings` - texture, model, audio blocks with defaults, clamping, version upgrade of old layouts
  - 20.4.2 Reimport signal - `Core/AssetDatabase` - `Added`, `SourceChanged`, `SettingsChanged` events, callback or queue
  - 20.4.3 Hot reload bridge - `Core/AssetDatabase`, `Core/AssetManager` - file change reloads in place and raises SourceChanged
  - 20.4.4 Import today - `Render/ModelImport`, `Components/MeshRenderer` - a model dropped or double-clicked is imported with fixed rules (hierarchy, textures, rig, clips, size normalization); texture filter, tiling and normal map are set per object
- **Code:** `Core/ImportSettings`, `Core/AssetDatabase`, `Core/AssetManager`, `Render/ModelImport`
- **API:** `ImportSettings`, `TextureImportSettings`, `ModelImportSettings`, `AudioImportSettings`, `AssetDatabase::SetSettings`, `SetReimportCallback`, `TakeReimports`
- **Editor:** none yet (no per-asset options; model import uses fixed rules, see 3.6)
- **Docs:** `docs/asset_database.md`
- **Tests:** `asset_database`
- **Status:** Partial - settings are stored and signalled; no importer reads them (stated in `docs/asset_database.md`).
- **Exists today:** Settings round-trip through the meta with unknown keys kept; changing them raises a reimport event. The renderers and loaders still take texture filter, wrap and so on from the object using the texture (`MeshRenderer`), not from the asset.
- **Work packages:**
  1. M - Inspector for the selected asset (texture, model, audio blocks) writing through `SetSettings`. Depends on: 20.1 package 2.
  2. M - Texture loader applies `maxSize`, `mipmaps`, `sRGB` from the meta when the image is loaded (`AssetManager` Image loader). Depends on: package 1.
  3. M - Model importer applies `scale`, `normals`, `smoothingAngle`, `importAnimations`. Depends on: package 1, 3.6.
  4. S - Audio loader honours `loadMode`, `forceMono`, `quality`. Depends on: package 1, 7.2.
  5. S - On `SettingsChanged` reload the asset in the open scene. Depends on: packages 2-4.
- **Open:** no UI; two places currently own texture options (the asset meta and the MeshRenderer fields), the plan must say which wins; compressed GPU formats are only a flag (`compress`).
- **Related:** 20.1, 3.6, 2.2, 7.2
- **Words:** import settings, texture import, model import, sRGB, mipmaps, max size, compress, reimport, importer, scale factor
- **Seen as:** textures blurry or too large; imported model is the wrong size; normal maps look wrong because they are treated as colour; I changed a setting and nothing happened
- **Ask me like:** "add an import settings Inspector for textures"; "make the model importer use the scale from import settings"

### 20.5 Thumbnails
- **What:** Small preview pictures for assets in the browser, cached so the grid stays fast.
- **Parts:**
  - 20.5.1 Editor renderers - `editor/main.cpp` - prefabs, scenes, materials (a sphere), models and `.okaymesh` files are rendered into 128 px pictures by generators registered with the cache; primitives in the Modeling panel keep their own small cache
  - 20.5.2 Thumbnail cache - `Core/ThumbnailCache`, `editor/ProjectBrowser.*` - memory plus disk cache keyed by GUID and content hash (a prefab, scene or material also by what it uses), generators per type, placeholders, stats; the Project panel and the Scene Manager use it
  - 20.5.3 Image generator and downscale - `Core/ThumbnailCache` - `ImageThumbnailGenerator`, area-weighted `BoxDownscale`
- **Code:** `Core/ThumbnailCache`, `editor/main.cpp`
- **API:** `ThumbnailCache::Get`, `AddGenerator`, `Invalidate`, `Options::diskDir`
- **Editor:** Project panel tiles and details strip
- **Docs:** `docs/asset_database.md`
- **Tests:** `asset_database`, `project_panel_db`
- **Status:** Partial - the Project panel gets every picture from the shared cache (kept on disk, made again when the content changes); pictures are made on the main thread, inside a frame time budget.
- **Exists today:** `Browser::Thumb` and the generators in `editor/main.cpp` (scene and prefab previews instantiate into a throwaway scene with scripts off); `GetPrimitiveThumb` still serves the Modeling panel. `ThumbnailCache` adds the disk cache, failure memory and the audio waveform generator.
- **Work packages:**
  1. M - Project panel gets its pictures from `ThumbnailCache`, with the editor's renderers registered as generators for prefab, scene, model and mesh. Depends on: 20.1 package 2.
  2. S - Disk cache under `<project>/.okay/thumbs` (hidden so scans skip it). Depends on: package 1.
  3. S - Invalidate on `SourceChanged` so an edited file refreshes its picture. Depends on: package 1, 20.4.
  4. S - Generate thumbnails off the UI thread or spread over frames for big folders. Depends on: package 1.
- **Open:** pictures are made on the main thread inside a time budget, not on a worker thread (packages 1 to 3 are done; package 4 is the budget, not a thread); a scene thumbnail loads the whole scene; no thumbnail for fonts or data assets.
- **Related:** 20.1, 15.4, 3.5
- **Words:** thumbnail, preview picture, asset preview, ThumbnailCache, tile icon, model preview, prefab preview
- **Seen as:** blank tiles; slow first open of a folder full of models; stale preview after editing a texture
- **Ask me like:** "cache asset thumbnails on disk"; "show a waveform thumbnail for audio files"

## 21. Prefab system (Critical)

### 21.1 Reusable objects
- **What:** Save an object and its children as a `.okayprefab` file and place copies of it in any scene or spawn it from a script.
- **Parts:**
  - 21.1.1 Prefab files and instantiate - `Scene/SceneSerializer` - `SaveObjectToFile`, `InstantiateFromFile`, `Instantiate` (duplicate/paste); an instance root remembers its file in `prefabSource`
  - 21.1.2 Editor save and place - `editor/main.cpp`, `editor/PrefabEdit.*` - Save As Prefab (Hierarchy menu and Inspector button), drag an object into the Project panel, drop a prefab into the scene, UI prefabs from the UI tools; an object holding prefab instances is saved with `PrefabTools::SavePrefab` so the nested links stay (`SaveAsPrefabFile`)
  - 21.1.3 Spawning and pooling - `Scene/ObjectPool` - `spawn("prefab", x, y)` in scripts, `net_spawn` for replicated spawns, `ObjectPool` reuses spawned objects
  - 21.1.4 Starter templates - `Scene/SceneTemplates`, `Scene/UITemplates` - built-in object and UI templates the editor can create (different from prefab files)
  - 21.1.5 Prefab mode - `editor/EditorState` (`OpenPrefab`, `SavePrefab`, `RevertPrefab`, `ClosePrefab`), `editor/main.cpp` bar - double-click a prefab in the Project panel, or Open Prefab in the Hierarchy menu or the Inspector's Prefab box: the prefab opens alone with a Scene > Prefab > Nested bar, Back, Save, Revert; nested prefabs drill in; each level has its own undo history and unsaved mark; saving then going Back rebuilds every instance in the scene keeping its overrides; Play is off meanwhile
- **Code:** `Scene/SceneSerializer`, `Scene/ObjectPool`, `Scene/SceneTemplates`, `Scene/UITemplates`, `editor/main.cpp`
- **API:** `SceneSerializer::InstantiateFromFile`, `SaveObjectToFile`, `GameObject::prefabSource`, `ObjectPool`, script `spawn`, `spawn3`, `net_spawn`
- **Editor:** Hierarchy right-click > Save As Prefab and the Prefab submenu (Open enters prefab mode), Project panel (drag in and out, double-click or right-click > Open Prefab, Place in Scene), Inspector prefab header (21.4; Open enters prefab mode), the prefab mode bar above the Scene view
- **Docs:** `docs/prefabs.md`, `docs/prefab_mode.md`, `docs/scene_serializer.md`, `docs/scene_management.md`
- **Tests:** `instantiate`, `scene_ids_prefabs`, `scene_load_pool`, `templates`, `ui_templates`, `prefab_mode`
- **Status:** Works - save, place and spawn are in daily use and covered by tests.
- **Exists today:** Everything in the Parts list on main. #575 added a cache of the last 32 decoded prefab files (`SceneSerializer::ClearPrefabCache` drops it) so repeated spawns skip the file read; #578's `PrefabTools` is reached through `InstantiateFromFile` for nested and variant files.
- **Work packages:**
  1. S - Prefab asset thumbnail already exists; add "Used in N scenes" via `UsedBy`. Depends on: 20.2.
  2. M - Spawn presets (a prefab list on a Spawner that validates files at load). Depends on: 33.2.
- **Open:** a prefab file stores one object tree, not a set; no "create prefab from selection of several roots"; prefab mode does not open variants, saves only the prefab's root tree and keeps no crash-recovery copy of a prefab being edited (`docs/prefab_mode.md`).
- **Related:** 1.6, 15.1, 15.4, 11.6, 21.2, 21.4
- **Words:** prefab, okayprefab, instantiate, spawn, template, Save As Prefab, prefab instance, ObjectPool, prefabSource
- **Seen as:** I changed the prefab but my placed copies did not change; dragging an object into Project does not create what I expected; spawned bullets stutter (pooling)
- **Ask me like:** "make a prefab from this enemy"; "spawn this prefab at the player"

### 21.2 Nested prefabs
- **What:** A prefab that contains other prefab instances, which keep their link to their own prefab so editing the inner one updates the outer one.
- **Parts:**
  - 21.2.1 Nested prefab expansion (#578) - `Scene/PrefabTools` - a prefab file may hold objects marked `nestedprefab "<file>"` with `override`, `remove`, `unset` and objects added inside; instantiating the outer file builds every nested instance
  - 21.2.2 Safety limits (#578) - `Scene/PrefabTools` - cycle detection (`A holds B holds A`), depth limit 16, 200000-object cap, a damaged nested file skips only that part with a warning
  - 21.2.3 Saving nested structures (#578) - `Scene/PrefabTools` - `SavePrefab(root, path)` writes nested instances as `nestedprefab` objects and refuses a result that would not load
  - 21.2.4 Queries and validator (#578) - `Scene/PrefabTools` - `IsPrefabInstance`, `PrefabRoot`, `NestedInstances`, `ValidateAsset`, `ValidateInstance`
  - 21.2.5 Plain prefab records - `Scene/SceneSerializer` - a `prefab` record is written for any object with `prefabSource`; plain prefab files still take the old path, only files with `nestedprefab` or `variant` go to `PrefabTools`
  - 21.2.6 Nested prefabs in the editor - `editor/PrefabEdit.*`, `editor/main.cpp`, `editor/InspectorPanel.*` - `DescribePrefabInstance` (root, outer instance, kind), Hierarchy badge and tooltip, Inspector header, `SaveAsPrefabFile`, Validate Prefab; Apply through the inner instance updates every user of the inner prefab
- **Code:** `Scene/PrefabTools`, `Scene/SceneSerializer`
- **API:** `PrefabTools::SavePrefab`, `NestedInstances`, `ValidateAsset`; `SceneSerializer::InstantiateFromFile` routes nested files to `PrefabTools`
- **Editor:** Hierarchy marks nested prefab instances (a square in a square) and variants (violet), the Inspector header names the kind ("nested in Car.okayprefab") with Select Asset, Save As Prefab keeps nested links, Project panel > Validate Prefab
- **Docs:** `docs/prefab_system.md`; `docs/prefabs.md`
- **Tests:** `prefab_nested`; `prefab_editor`
- **Status:** Partial - the data layer (#578) and the editor side are on main: nested instances are marked in the Hierarchy, named in the Inspector, saved without losing their links and checked by Validate Prefab (tests `prefab_nested`, `prefab_editor`); a Console list of nesting errors with the file chain and the "Used in N scenes" dependency are not built.
- **Exists today:** The whole data layer above, with about 700 lines of tests, plus the flat model (one source file and a list of overrides) for plain prefabs, and the editor side in 21.2.6 (`docs/prefab_system.md`, "In the editor").
- **Work packages:**
  1. S - Validate Prefab on Build (33.2); the Project panel entry exists (`ValidateAsset` result in the Console). Depends on: 21.2.6.
  2. S - Prefab nesting depth and cycle errors shown in the Console with the file chain when a scene loads. Depends on: 21.2.6.
  3. S - Wire "Used in N scenes" and the asset database's dependency extractor to understand `nestedprefab` and `variant` lines (check `Core/AssetDatabaseDeps`). Depends on: 20.2.
- **Open:** objects are matched by depth-first position and name, so reordering many objects can pair the wrong ones (the page says so); the editor's Apply updates the other instances in the scene, but not a second instance of the applied file inside the same outer instance.
- **Related:** 21.1, 21.3, 21.4, 1.6
- **Words:** nested prefab, prefab inside prefab, child prefab, nestedprefab, prefab hierarchy, instance of instance, PrefabTools
- **Seen as:** a prefab built from other prefabs loses their link; editing a part prefab does not update the big one
- **Ask me like:** "make a car prefab out of wheel and body prefabs"; "show nested prefab roots with an icon in the Hierarchy"

### 21.3 Variants
- **What:** A prefab that is based on another prefab and only stores its differences (a red enemy variant of the base enemy).
- **Parts:**
  - 21.3.1 Variant file format (#578) - `Scene/PrefabTools` - a prefab file with `variant "<base>"` plus `override`, `unset`, `remove` and added objects (`under <i>`); a variant of a variant works
  - 21.3.2 Save a variant from an edited instance (#578) - `Scene/PrefabTools` - `SaveVariant(instance, path)` writes the differences and never touches the base
  - 21.3.3 Variant creation UI - `editor/PrefabEdit.*`, `editor/InspectorPanel.*`, `editor/main.cpp` - Inspector **Create Variant** (name box; `SaveVariant`, then the instance is linked to the new variant; one undo step that also removes the file), Hierarchy Prefab menu, Project panel **Create Variant** on a prefab (a variant with no differences yet), `PrefabNewVariantFile`
  - 21.3.4 Variant display - `editor/PrefabEdit.*`, `editor/InspectorPanel.*` - violet Hierarchy mark, header "Prefab Variant of <base>" with **Select Base**; Apply on an instance of a variant writes the variant's override and never the base, and updates the other instances of the variant
- **Code:** `Scene/PrefabTools`
- **API:** `PrefabTools::SaveVariant`, `Instantiate`, `FileUsesPrefabStructure`; `oked::PrefabCreateVariant`, `PrefabNewVariantFile`, `PrefabKindOfFile`, `ResolveVariantBase`
- **Editor:** Inspector prefab header > Create Variant / Select Base, Hierarchy row menu > Prefab > Create Variant..., Project panel right-click > Create Variant
- **Docs:** `docs/prefab_system.md`
- **Tests:** `prefab_variants`; `prefab_editor`
- **Status:** Partial - variants can be created, shown and applied into from the editor (`prefab_editor`); a variant badge in the Project panel and "changing the base shows its effect on variants" feedback are not built.
- **Exists today:** Variant files, chains with cycle and depth checks, `SaveVariant`, compatibility (an older build loads only the added objects with warnings), and the editor side in 21.3.3 and 21.3.4. Editing the base and pressing Apply or Update All rebuilds the variants' placed instances (each keeps its own overrides).
- **Work packages:**
  1. S - Variant badge on the prefab's tile in the Project panel (the Hierarchy and Inspector already show it). Depends on: 21.3.4.
- **Open:** scene files never contain variant records (only prefab files do); objects added to or lost from an instance of a variant cannot be applied into the variant (Create Variant keeps them); override unit is a whole record, with field-level apply and revert done by writing the whole record.
- **Related:** 21.2, 21.4
- **Words:** prefab variant, derived prefab, inherit prefab, base prefab, variant of variant, enemy variants, SaveVariant
- **Seen as:** I need ten similar enemies and copy-pasting prefabs makes them drift apart
- **Ask me like:** "create a variant of the goblin prefab with more health"

### 21.4 Overrides
- **What:** Remember how a placed instance differs from its prefab, and offer to apply, revert or re-apply those differences.
- **Parts:**
  - 21.4.1 Override data layer - `Scene/SceneSerializer`, `Scene/GameObject` - `CapturePrefabOverrides`, `InstantiatePrefab(scene, path, overrides)`, `ReapplyPrefab`; overrides saved as `override <object> "<line>"` records (merged #570)
  - 21.4.2 Editor prefab header - `editor/InspectorPanel.*`, `editor/PrefabEdit.*` - **Prefab: <name>** with Open, Select Asset, Revert All, Apply All, Create Variant, Update All, Unpack (the old whole-file Apply, Revert, Reload All and Unlink buttons are gone)
  - 21.4.3 Per-field diff, revert and apply (#578) - `Scene/PrefabTools` - `Diff` lists changed, added and removed records and fields ("Transform position.x: 0 -> 4"), `RevertOverride`, `RevertAll`, `ApplyToSource`, `ApplyAllToSource`; apply writes atomically into the prefab, a variant's override or a nested instance's override
  - 21.4.4 Unpack (#578) - `Scene/PrefabTools` - `Unpack(go, completely)` cuts the prefab link and keeps the objects and their ids
  - 21.4.5 Per-field apply through the property registry - planned - `Scene/PropertyRegistry` (#578, 1.19) can list a component's fields by name (`Light.intensity`), snapshot, diff and revert them; `PrefabTools::Diff` still uses its own fixed field list (the editor maps an Inspector label to the diff's field names with `ChangedFieldsForLabel`)
  - 21.4.6 Per-property display - `editor/InspectorPanel.*` (`PrefabFieldMark`), `editor/PrefabEdit.*` - a blue bar on an overridden property's row (generic card and hand-written fields), right-click **Revert Property** / **Apply Property to Prefab**; component header bar and menu **Revert / Apply Component to Prefab**
  - 21.4.7 Changes list - `editor/InspectorPanel.*` (`DrawPrefabChanges`) - per component and per field (old -> new) with Revert / Apply, components added or lost, child objects added or lost (Revert deletes or restores them; Apply writes them into a plain prefab)
  - 21.4.8 Apply and Revert actions with undo - `editor/PrefabEdit.*`, `editor/EditorState.*` - `PrefabRevert`, `PrefabApply`, `PrefabApplyAll`, `PrefabRevertObject`, `PrefabApplyObject`, `PrefabUnpack`, `PrefabUpdateInstances`; one undo step each; the prefab files an Apply wrote travel with the step (`AttachUndoFiles`); Apply captures the other instances' overrides against the old file and rebuilds them from the new one, so an instance that changed the same field keeps its value
- **Code:** `Scene/SceneSerializer`, `Scene/GameObject`, `Scene/PrefabTools`, `editor/main.cpp`
- **API:** `SceneSerializer::CapturePrefabOverrides`, `ReapplyPrefab`, `InstantiatePrefab`; `GameObject::prefabOverrides`; `PrefabTools::Diff`, `RevertOverride`, `ApplyToSource`, `Unpack`
- **Editor:** Inspector prefab header and Changed from the prefab list, right-click a marked property, component `...` menu, Hierarchy row menu > Prefab
- **Docs:** `docs/prefabs.md`; `docs/prefab_system.md`
- **Tests:** `scene_ids_prefabs`; `prefab_apply_revert`; `prefab_editor`; `inspector_model`
- **Status:** Works - the editor reads, shows, reverts and applies overrides per property, component, child object and instance through `PrefabTools`, with undo and with the other instances updated (tests `prefab_editor`, `prefab_apply_revert`); the registry-based field list (21.4.5) and an "are you sure" before Apply All are not built.
- **Exists today:** The data layer for overrides, per-field diff, revert and apply, unpack and the validator, all called from the editor (21.4.2 to 21.4.8). Undo of an Apply restores the prefab file text together with the scene: the file edits are a command in the same `UndoStack` group as the scene change (`docs/prefab_system.md`, "In the editor").
- **Work packages:**
  1. M - Per-field apply for any registered property through the property registry (names like `Light.intensity`, 1.19) instead of the fixed field list. Depends on: #578 (on main).
  2. S - Apply All asks before overwriting the prefab file. Depends on: 21.4.8.
- **Open:** `Diff` is exact for well-known records and falls back to whole records for others; `ApplyToSource` only handles records, so the editor applies an added or lost child object itself (`SavePrefab`, plain prefab files only; variants and nested instances say why it is off); a second instance of the applied file inside the same outer instance is not updated.
- **Related:** 21.1, 21.2, 21.3, 15.2, 1.6, 22.1
- **Words:** prefab overrides, apply, revert, reload all, unlink, unpack, overridden, instance differences, ReapplyPrefab, CapturePrefabOverrides, PrefabTools, per-field override
- **Seen as:** I tweaked one instance and it lost the change after the prefab was edited; Apply overwrote things I did not mean to; no way to see what is different
- **Ask me like:** "show which fields of this instance differ from its prefab"; "make Revert keep my other overrides"

## 22. Undo/redo & recovery (Critical)

### 22.1 Undo history
- **What:** Step back and forward through your edits with Ctrl+Z and Ctrl+Y, and jump to any earlier step from a history list.
- **Parts:**
  - 22.1.1 Scene undo on `UndoStack` - `editor/EditorState.*`, `editor/SceneUndo.hpp`, `editor/EditUndo.hpp` - `PushUndo` opens a step, closed at the next boundary as a `SceneDeltaCommand` (scene-text delta, selection by GUID); 100 steps; named steps; drags and repeated edits of one widget merged; `Undo`, `Redo`, `UndoName`, `UndoDepth`; empty history on New / Open; off during Play
  - 22.1.2 History window - `editor/main.cpp` - View > History lists the step names ("After: ...", "Before: ..."); click one to jump there; Edit > Undo / Redo show the name and grey out when empty
  - 22.1.3 Separate undo stacks - `editor/main.cpp` - mesh edit mode keeps its own stack (`g_meshUndo`), the Localization, Timeline and Project panels have their own; the animation draft is now a command in the main stack (`AnimDraftCommand`); Ctrl+Z goes to whichever has focus
  - 22.1.4 Command-based undo stack (#572) - `Core/Undo`, `Core/UndoCommands` - `UndoStack` with groups, merging of slider drags, count and memory caps, dirty tracking; `SetProperty`, `ListEdit`, object create/delete/reparent by id
  - 22.1.6 Asset files inside an undo step - `editor/EditorState.*` - `AttachUndoFiles(edits)` adds a file command (`MakeFileUndo`, before and after text of files an edit wrote: a prefab Apply, a new variant) to the step `PushUndo` opened, as `PushUndo(name, alsoUndo)` does; Undo and Redo move the files with the scene in one `UndoStack` group
  - 22.1.5 Property change hook as an undo source - planned - `Scene/PropertyRegistry` (#578, 1.19) fires a hook with old and new value on every real property change and has `ScopedHookSuppression` so an undo does not record itself; `Core/UndoCommands` has a `SetProperty` command, nothing connects the two or the editor
- **Code:** `editor/EditorState.*`, `editor/SceneUndo.hpp`, `editor/EditUndo.hpp`, `editor/main.cpp`, `Core/Undo`, `Core/UndoCommands`
- **API:** `EditorState::PushUndo(name)`, `PushUndoText`, `CommitUndo`, `Undo`, `Redo`, `CanUndo`, `UndoName`, `undoStack()`; `SceneDeltaCommand`, `TextDelta`, `DescribeSceneChange`; `UndoStack`, `UndoSet`, `UndoRecordSet`, `SnapshotRecorder`, `CreateObjectCommand`, `DeleteObjectCommand`, `ObjectOps`
- **Editor:** Edit > Undo / Redo, View > History
- **Docs:** `docs/editor.md`; `docs/editor_undo.md` (audit of every edit); `docs/undo_recovery.md`
- **Tests:** `editor_undo` (delta, command, 25 edit kinds by scene text, merge, GUID restore, limit, New / Open, Play, saved tracking); editor `--selftest` covers undo around a creation; `undo`
- **Status:** Partial - the editor records scene edits on the engine `UndoStack` (headless tests above). Not done: per-object and per-property commands (each Undo rebuilds the scene), folding mesh edit / Timeline / Localization / Project undo into one stack, an asset move keeps the scene history. The GUI paths (Edit menu, shortcuts, History panel, `WatchEditUndo`) were compiled but not run.
- **Exists today:** One step costs a scene serialisation at the boundary and stores only the differing stretch of text; an undo rebuilds the scene from text, as the snapshot undo did. Undo history survives Play and Stop (it is off, and records nothing, while Play runs).
- **Work packages:**
  1. Done - one `UndoStack` in `EditorState`; `IsDirty()` tracks saved state (`MarkClean` is the save point; the title-bar mark still reads `dirty`, kept in step on undo and redo).
  2. Done in effect - a drag is one step and costs the changed text, not a scene copy (`WatchEditUndo` + `TextDelta`); same-widget edits merge. The property registry hook is not used. Depends on: package 1.
  3. L - Create, delete, duplicate and reparent use object commands keyed by `GameObject::Guid()` (ids must survive a restore). Depends on: package 1, #570 (on main).
  4. M - Fold the mesh-edit, animation-draft and localization stacks into the same stack so Ctrl+Z always does the expected thing. Depends on: package 1.
  5. Done for names (History and Edit menu); the memory figure is not shown. Depends on: package 1.
  6. Done - history is kept across Play and Stop; Undo is off and nothing is recorded while Play runs. Depends on: package 1, 34.1.
- **Open:** per-object commands (`ObjectOps`, Guid) would avoid the rebuild and keep Inspector state; asset moves clear the scene history; several stacks remain (mesh, Timeline, Localization, Project) and Ctrl+Z goes to the focused one; the property registry (#578, 1.19) could feed `SetProperty` commands.
- **Related:** 15.9, 15.2, 22.2, 34.1
- **Words:** undo, redo, Ctrl+Z, history window, PushUndo, UndoStack, command undo, snapshot undo, SetProperty, merge slider drags
- **Seen as:** undo is slow in a big scene; Ctrl+Z undoes the wrong thing after mesh editing; undo does nothing after an asset was moved
- **Ask me like:** "make delete / reparent per-object undo commands"; "keep the scene history when an asset is moved"

### 22.2 Autosave
- **What:** The editor quietly saves a recovery copy of your work every few minutes so a crash costs little.
- **Parts:**
  - 22.2.1 Scene autosave sidecar - `editor/main.cpp`, `editor/DocRecovery.hpp` - leftover `<scene>.autosave` files are still offered on open when newer than the scene; new sessions do not write one (test `doc_recovery`)
  - 22.2.2 Rotating autosave with session locks (#572) - `Core/Recovery`, `editor/DocRecovery.hpp`, `editor/main.cpp` - `RecoveryManager` autosaves dirty graphs, scripts and the scene into `okay_recovery` every 30 s (File > Autosave) and when the window loses focus; atomic checksummed slots; untitled documents included; the real file is overwritten only if "Also save the real file" is on
  - 22.2.3 In-game Auto Save component - `Components/AutoSave` - saves the game (not the editor scene) to a slot on a timer or when a scene starts; see 35.2
- **Code:** `editor/main.cpp`, `Core/Recovery`, `Components/AutoSave`
- **API:** File > Autosave (Enabled, Interval); `RecoveryManager::SetProvider`, `StartSession`, `Tick`, `OnSaved`, `EndSession`
- **Editor:** File > Autosave submenu
- **Docs:** `docs/editor.md`; `docs/undo_recovery.md`
- **Tests:** `recovery`, `doc_recovery`
- **Status:** Partial - graphs, scripts and the scene autosave through `RecoveryManager` (test `doc_recovery`). The older `<scene>.autosave` sidecar is read when it is newer than the scene and is no longer written.
- **Exists today:** Recovery copies go to `okay_recovery`, including untitled graphs and scripts. A leftover `<scene>.autosave` that is newer than the scene is still offered by Recover Autosave. `WritesLegacySceneSidecar()` is false.
- **Work packages:**
  1. M - Replace the sidecar with `RecoveryManager` (rotating slots, per-session lock, clean-exit cleanup). Depends on: #572 (on main).
  2. S - Autosave unsaved "Untitled" scenes too (recovery folder under the project). Depends on: package 1.
  3. S - Autosave interval and slot count in the project settings window. Depends on: package 1, 25.2.
  4. M - Autosave other editor documents (script tabs, visual scripts, dialogue graphs). Depends on: package 1.
- **Open:** a leftover `.autosave` from an older editor can still sit beside the scene until it is recovered or discarded; a project under git may pick that file up.
- **Related:** 22.3, 22.4, 15.9
- **Words:** autosave, recovery copy, `.autosave`, RecoveryManager, interval, auto backup, Untitled scene
- **Seen as:** lost an hour of work after a crash; autosave file shows up in git; no autosave for a scene I never saved
- **Ask me like:** "autosave scripts and graphs too"; "keep three rotating autosaves"

### 22.3 Crash recovery
- **What:** After a crash or a killed editor, offer the last autosave, and keep a record of what crashed.
- **Parts:**
  - 22.3.1 Recover Autosave prompt - `editor/main.cpp` - opening a scene that has a newer `.autosave` shows "Recover Autosave"
  - 22.3.2 Crash handlers and breadcrumbs - `editor/main.cpp` - Windows exception filter and a Linux/macOS signal handler write `okay_crash.txt` and append `okay_crashlog.txt`; `OKAY_TRACE` records the last step
  - 22.3.3 Crash Log tab and launcher page - `editor/main.cpp`, `shared/CrashReports.hpp`, `launcher/main.cpp` - View > Crash Log; Settings > Crash reports in the launcher with a notification per new crash
  - 22.3.4 Unclean-shutdown detection (#572) - `Core/Recovery`, `editor/main.cpp` - a killed editor leaves its lock; the next start offers Restore, Compare or Discard and keeps the copy until one of those is chosen
- **Code:** `editor/main.cpp`, `shared/CrashReports.hpp`, `launcher/main.cpp`, `Core/Recovery`
- **API:** `OKAY_TRACE`, `okaycrash::ParseLog`, `okaycrash::Load`; `RecoveryManager::FindRecoverable`, `Restore`, `Discard`
- **Editor:** "Recover Autosave" dialog, View > Crash Log
- **Docs:** `docs/editor.md`, `docs/launcher-multiplayer.md`
- **Tests:** `crash_reports`; `recovery`
- **Status:** Partial - the editor offers recovered graphs, scripts and the scene after a kill (`DrawDocRecoveryPopup`); the player still has no crash handler of its own.
- **Exists today:** All of Parts 22.3.1-22.3.3 are used by the editor; 22.3.4 exists only as engine code. The recover popup compares the autosave with the scene; the reports are plain text with version, last step, exception and backtrace.
- **Work packages:**
  1. M - Recovery dialog lists every recoverable session (`FindRecoverable`) with time and size, restore or discard. Depends on: 22.2 package 1.
  2. S - Mark the editor session in the recovery folder so a crash is detected on next start (not just on opening the same scene). Depends on: 22.2 package 1.
  3. M - Crash handler in the player that writes a report next to the game and offers "open report folder". Depends on: 48 proposal (crash reporting).
  4. S - Recover prompt also covers script and graph buffers. Depends on: 22.2 package 4.
- **Open:** the report format is only parsed by the launcher; no upload; the player has no handler.
- **Related:** 22.2, 22.4, 15.9, 17.1, 48.1
- **Words:** crash recovery, Recover Autosave, crash log, okay_crash.txt, okay_crashlog.txt, OKAY_TRACE, unclean shutdown, crash reports
- **Seen as:** editor closed by itself and I want my scene back; "OkaySpace crashed" box; launcher says the editor crashed
- **Ask me like:** "list every recoverable autosave after a crash"; "add a crash handler to the player"

### 22.4 Backups
- **What:** Older versions of your files are kept so a bad save or a bad edit can be rolled back.
- **Parts:**
  - 22.4.1 Rolling `.bak` of scene and game saves - `Scene/SceneSerializer`, `Core/SaveGame` - saving a scene keeps the previous good file as `<scene>.bak`; game save slots keep a `.bak` and load it if the slot is damaged
  - 22.4.2 Timestamped backups with a prune policy (#572) - `Core/Backup` - `BackupManager::SaveWithBackup`, keep-last N plus one per day, size cap, restore after backing up the current file
- **Code:** `Scene/SceneSerializer`, `Core/SaveGame`, `Core/Backup`
- **API:** `SceneSerializer::SaveToFile`; `BackupManager`, `BackupPolicy`
- **Editor:** Visual Script > Graph > Restore previous version (the last 5 saves of that graph, via `BackupManager`)
- **Docs:** `docs/save_games.md`; `docs/undo_recovery.md`
- **Tests:** `durable_saves`, `save_game`; `backup`; `doc_recovery` (keeps the newest 5)
- **Status:** Partial - graph and script saves keep the last 5 timestamped backups in `okay_backups`; scene saves still keep a single `.bak`.
- **Exists today:** Scene saves are atomic (temp file then rename) and keep one `.bak` (`SceneSerializer::SaveToFile`). `Core/Backup` adds many timestamped backups and pruning, not called by the editor.
- **Work packages:**
  1. S - Editor Save goes through `BackupManager::SaveWithBackup` (backups under `<project>/.okay/backups`). Depends on: #572 (on main).
  2. M - File > Restore from Backup window (list, preview time and size, restore). Depends on: package 1.
  3. S - Backups of prefabs, materials and other text assets on save. Depends on: package 1.
  4. S - Backup settings (count, days, size) in project settings. Depends on: 25.2.
- **Open:** a scene save still keeps one `.bak` beside the scene, and a leftover `.autosave` from an older editor may too; a project under git may pick them up.
- **Related:** 22.2, 28.3, 35.4, 1.7
- **Words:** backup, `.bak`, restore from backup, BackupManager, previous version, rollback, prune
- **Seen as:** saved over the wrong scene; scene file broken and no older copy; too many backup files
- **Ask me like:** "add File > Restore from Backup"; "keep daily backups for two weeks"

## 23. Profiling & debugging (Critical)

### 23.1 CPU/GPU profiler
- **What:** See where each frame's time goes: which systems are slow, which frames spike, and whether the CPU or the GPU is the limit.
- **Parts:**
  - 23.1.1 Frame profiler core - `Core/Profiler` - `Prof()`, `OKAY_PROFILE("Name")` scopes (about 60 in engine and editor), ring of the last 240 frames, per-section min/avg/max, Chrome trace, CSV, `DumpFrame`, `Summary`
  - 23.1.2 Profiler window - `editor/main.cpp` - pause and scrub, stacked CPU categories (Scripts, Physics, Render, Other), 60/30 fps guide lines, per-section table with self/total/calls/percent, plain-language diagnosis, Copy CSV
  - 23.1.3 Stats panel - `editor/main.cpp` - FPS plot, object, triangle, light, collider and script counts, drawn and culled counts after frustum culling
  - 23.1.4 "GPU" line - `editor/main.cpp` - fed by the CPU time of the render call (`Prof().Gpu(ms)`), not a GPU timer query
  - 23.1.5 Frame history, GPU timer and render stats (#574) - `Debug/FrameHistory`, `Debug/GpuTimer`, `Debug/RenderStats` - percentiles, spikes with the culprit scope, flame graph, GPU scopes and counters
- **Code:** `Core/Profiler`, `editor/main.cpp`, `Debug/FrameHistory`, `Debug/GpuTimer`, `Debug/RenderStats`, `Debug/DebugStatsOverlay`, `Debug/Debug`, `Debug/DebugUtil`
- **API:** `Prof()`, `OKAY_PROFILE`, `Profiler::ExportChromeTrace`, `ExportCsv`; `FrameHistory`, `GpuTimer`, `GpuScope`, `RenderStatsCollector`
- **Editor:** View > Panels > Profiler and Stats
- **Docs:** `docs/core_logging_profiling.md`; `docs/debug_tools.md`
- **Tests:** `profiler`, `profiler_export`; `debug_frame_history`, `debug_gpu_stats`, `debug_overlay`
- **Status:** Partial - the Profiler window works on CPU data; the trace and CSV exports exist in code but have no button (CSV of one frame can be copied); real GPU timing exists only as a data layer.
- **Exists today:** Frame profiler and window as above. The data layer from #574 (`FrameHistory`, `GpuTimer`, `RenderStats`, `DebugStatsOverlay`) is on main and tested but not wired: "No renderer calls the hooks yet" (`docs/debug_tools.md`; grep finds no `GpuScope` outside `Debug/`).
- **Work packages:**
  1. S - Profiler window: "Export Chrome trace" and "Export CSV" buttons using the existing `ExportChromeTraceToFile`. Depends on: nothing.
  2. M - Window reads `FrameHistory` (longer history, percentiles, spike list, flame graph view). Depends on: #574 (on main).
  3. M - Renderers call `GpuScope` around shadow, opaque, transparent and post passes; `CpuFenceGpuTimer` as fallback. Depends on: #574 (on main).
  4. S - Profile scopes for scripts per object and for networking. Depends on: package 2.
  5. S - In-game stats overlay (`DebugStatsOverlay`) toggled by a key or a cvar. Depends on: #574, 54 proposal.
- **Open:** the "GPU" number is CPU wall time around the render call; the ring is only 4 seconds at 60 fps; `FrameHistory` keeps 600 frames by default (`Capture(Prof())` copies the profiler's ring) but nothing calls it from the live frame yet (`GameLoop::Stats()` from #581 keeps a window of loop frames and can sync into it, 1.1.13, but no host calls it).
- **Related:** 1.10, 2.1, 15.6, 23.2, 23.3, 58.1
- **Words:** profiler, CPU profiler, GPU profiler, frame time, spike, flame graph, OKAY_PROFILE, Chrome trace, Perfetto, FrameHistory, GpuTimer, Stats panel, FPS
- **Seen as:** game stutters and I cannot tell why; FPS drops with many objects; profiler shows GPU time that does not change
- **Ask me like:** "add an Export Chrome Trace button to the Profiler"; "show real GPU time per render pass"

### 23.2 Memory tracking
- **What:** See how much memory each system uses, find leaks and see what the asset cache holds.
- **Parts:**
  - 23.2.1 Asset cache report - `Core/AssetManager` - `AssetManager::Report()` lists every cached asset with bytes, type and reference count, and totals per type
  - 23.2.2 Memory tracker (#574) - `Debug/MemoryTracker` - named categories, peaks, per-frame deltas, leak suspects, snapshot diff
  - 23.2.3 Heap counters (#574) - `Debug/MemoryHooks` - opt-in global `operator new/delete` counters
- **Code:** `Core/AssetManager`, `Debug/MemoryTracker`, `Debug/MemoryHooks`
- **API:** `AssetManager::Report`; `MemoryTracker::Global`, `Add`, `Remove`, `MemoryHooks`
- **Editor:** none yet
- **Docs:** `docs/asset_manager.md`; `docs/debug_tools.md`
- **Tests:** `assetmanager`; `debug_memory`, `debug_memory_hooks`
- **Status:** Partial - the tracker and the heap counters are on main (#574, tests `debug_memory`, `debug_memory_hooks`); nothing reports into them and no window shows them.
- **Exists today:** `AssetManager::Report()` (empty in practice, because nothing loads through the asset manager yet, 20.1), plus `MemoryTracker` (named categories, peaks, deltas, leak suspects, snapshot diff) and the opt-in heap counters from #574, which nothing reports into. The editor has no memory view.
- **Work packages:**
  1. S - Report asset cache bytes into `MemoryTracker` categories (textures, meshes, audio). Depends on: #574 (on main), 20.1.
  2. M - Systems that own big buffers (scene, particles, mesh data, audio mixer) report through `Add/Remove`. Depends on: package 1.
  3. M - Memory window: categories, peak, leak suspects, snapshot compare. Depends on: package 2.
  4. S - Memory lines in the stats overlay. Depends on: package 2.
- **Open:** no platform memory numbers (process working set); heap hooks off by default.
- **Related:** 20.1, 23.1, 2.2, 27.4
- **Words:** memory, memory tracker, leak, heap, allocations, MemoryTracker, MemoryHooks, asset memory, VRAM
- **Seen as:** memory grows while playing; game slow after many scene loads; no idea what takes RAM
- **Ask me like:** "add a memory window with leak suspects"; "count texture memory per scene"

### 23.3 Frame debugger
- **What:** Capture one frame and step through its draw calls to see what was drawn, with which material and state, and why something is missing.
- **Parts:**
  - 23.3.1 Draw call recorder (#574) - `Debug/DrawCallRecorder` - capture one frame's draw list, step through, filter, export
  - 23.3.2 Cull and draw counters on main - `Render/Culling` - `LastCullCounters` (drawn, culled) shown in the Stats panel
  - 23.3.3 Physics debug view - `Physics/PhysicsDebug` - contacts and script rays recorded for the editor's debug overlay (see 6.5)
- **Code:** `Debug/DrawCallRecorder`, `Render/Culling`, `Physics/PhysicsDebug`
- **API:** `DrawCallRecorder`; `LastCullCounters`, `FrustumCullingEnabled`, `PhysicsDebug::Collect`
- **Editor:** none yet (Stats panel shows counts)
- **Docs:** `docs/debug_tools.md`
- **Tests:** `frustum_culling`; `debug_draw_calls`
- **Status:** Partial - the recorder is on main (#574, test `debug_draw_calls`) but no renderer feeds it and there is no window.
- **Exists today:** Culling can be switched off to debug a missing object (`FrustumCullingEnabled`). `DrawCallRecorder` (#574) can hold and export a frame's draw list but no renderer feeds it, so there is still no draw list view and no way to step through draws.
- **Work packages:**
  1. M - GL and D3D11 renderers report each draw (mesh, material, shader, triangles) to `DrawCallRecorder` when capturing. Depends on: #574 (on main).
  2. M - Frame Debugger window: Capture button, draw list, per-draw details, "highlight in scene". Depends on: package 1.
  3. M - Step slider that draws only the first N calls in the Scene view. Depends on: package 2.
  4. S - Software renderer reports too, for parity in tests. Depends on: package 1.
- **Open:** each renderer (software, GL, D3D11, D3D12) draws differently, so four hooks are needed.
- **Related:** 2.1, 2.8, 6.5, 23.1, 27.3
- **Words:** frame debugger, draw calls, render capture, step through, draw list, DrawCallRecorder, overdraw, batches
- **Seen as:** an object does not draw and I cannot tell if it was culled; too many draw calls
- **Ask me like:** "build a frame debugger window"; "show why this object was not drawn"

### 23.4 Error console
- **What:** The panel that shows log messages, warnings and errors, with filters and links back to what caused them.
- **Parts:**
  - 23.4.1 Console panel - `editor/ConsolePanel.*` - draws `ConsoleModel::Rows()`; error, warning, info toggles with counts, category dropdown, search, collapse of repeats, details pane, clear on play, pause on error, Open Script, Copy and Save to `console_log.txt`; tab badge shows error and warning counts
  - 23.4.2 Open script at the error line - `editor/ConsolePanel.*` - the details pane offers Open Script (line) for script errors
  - 23.4.3 Log core - `Core/Log` - levels, categories, `OKAY_LOG` string, rotating file sink, sink hook used by the Console
  - 23.4.4 Console model (#574) - `Debug/ConsoleModel` - ring buffer with collapse, filters, source links to objects and assets, pause-on-error request; `Add` takes an explicit `ConsoleSource` (guid, component, node) and a `ConsoleSourceScope` stamps runtime lines
  - 23.4.5 Diagnostics - `Debug/Diagnostics`, `Debug/ProjectDiagnostics` - severity, stable code, message, how-to-fix hint, source (object guid or path, component, asset, script file and line, graph and node); dedupe, clear by source, clear by owner; new rows log through `Log` so the Console shows them
  - 23.4.6 Problems and build gate - `editor/ProblemsPanel.*` - the live list (15.11.8); Build stops on errors, lists warnings, and links to Problems
- **Code:** `editor/ConsolePanel.*`, `editor/ProblemsPanel.*`, `Core/Log`, `Debug/ConsoleModel`, `Debug/Diagnostics`, `Debug/ProjectDiagnostics`
- **API:** `ConsoleLog(msg, level)`, `Log::Info/Warning/Error`, `Log::Category`, `Log::Configure`; `ConsoleModel`, `ParseConsoleSource`, `ConsoleSourceScope`; `EditorDiagnostics`, `CollectProjectDiagnostics`, `ApplyDiagnosticFix`, `AddNodeRequirement`
- **Editor:** Console tab; Problems tab; status-bar Problems count
- **Docs:** `docs/core_logging_profiling.md`; `docs/debug_tools.md`
- **Tests:** `log`; `debug_console`; `diagnostics`
- **Status:** Works - the Console draws `ConsoleModel::Rows()` (category dropdown, collapse, severity filters, Error Pause, Open Script); clicking a row selects its source object; runtime script and graph errors carry that object; Problems is the single list of what is wrong.
- **Exists today:** Parts 23.4.1-23.4.6. A leading `[tag]` on a log line (no colon or slash) is the category. Validators in `CollectProjectDiagnostics` merge `Scene::Validate` and `SceneValidator` and add RequireComponent, player collider and rigidbody, an Animator with no states, a Nav Mesh Agent with no bake, script compile errors, visual-script graph errors (including `AddNodeRequirement`) and `AssetDatabase::FindMissing`.
- **Work packages:**
  1. M - Console reads from `ConsoleModel` (category filter, source links). Depends on: #574 (on main). Done.
  2. M - Click an entry with an object source selects it in the Hierarchy; a script line stays available from the details pane. Depends on: package 1. Done.
  3. S - Script and scene-load errors use the linkable message form. Depends on: package 1. Diagnostics log `[Object:]` / `[Asset:]`, and Play wraps script and graph calls in `ConsoleSourceScope`.
  4. S - Category dropdown (net, physics, script) using `Log::Category`. Depends on: package 1. Done: the dropdown lists categories seen on stored rows.
- **Open:** categories are taken from a leading `[tag]` on the message; subsystems that never write one do not appear in the dropdown.
- **Related:** 1.10, 15.9, 13.5, 33.2
- **Words:** console, error console, log, warnings, errors, ConsoleLog, ConsoleModel, collapse, pause on error, console_log.txt
- **Seen as:** thousands of repeated lines; cannot find the object that logged an error; log lost when the editor closes
- **Ask me like:** "click a console error to select the object"; "filter the console by category"

## 24. Plugin & extension system (Critical)

### 24.1 C++ plugins
- **What:** Let a developer build a native library (DLL or shared object) that adds components, tools or systems to the engine and editor without changing the engine source.
- **Parts:**
  - 24.1.1 Engine as a static library - `engine/CMakeLists.txt` - games in C++ link `okay::engine` (see the `examples/` demos); no dynamic loading and no install rules
  - 24.1.2 Component serializer registry - `Scene/ComponentSerializer` - a new component can register how it is saved and loaded without editing the big if-chain
- **Code:** `engine/CMakeLists.txt`, `Scene/ComponentSerializer`, `examples/CMakeLists.txt`
- **API:** `ComponentSerializerRegistry::Register`
- **Editor:** none
- **Docs:** `docs/scene_serializer.md`
- **Tests:** `scene_ids_prefabs` (registry)
- **Status:** Planned - no code loads a library at run time (no `dlopen` or `LoadLibrary` anywhere in engine, editor, player or launcher).
- **Exists today:** Only the static library and the component serializer registry above.
- **Work packages:**
  1. S - Design note: a small C ABI (`OkayPluginInfo`, `okay_plugin_init(host)`, version number) with a host table of function pointers. Depends on: nothing.
  2. M - Loader in the editor and player: scan a `Plugins/` folder, check version, load, catch and report failures, unload. Depends on: package 1.
  3. M - Host API: log, scene access, register a component, serializer entry, menu item. Depends on: package 2, the property registry (on main, #578) for exposing component fields.
  4. M - Sample plugin plus CMake template. Depends on: package 3.
  5. S - Trust prompt before loading unsigned native code. Depends on: package 2.
- **Open:** native plugins can crash the editor; ABI and ownership of memory need an explicit rule; use the property registry (on main, #578, 1.19) to expose component fields instead of inventing another one.
- **Related:** 24.2, 24.4, 13.4, 15.8
- **Words:** C++ plugin, native plugin, DLL, shared library, plugin ABI, extension, add a component from outside, SDK
- **Seen as:** I cannot add my own C++ component without editing the engine; need a DLL for a platform service
- **Ask me like:** "design the C++ plugin ABI"; "load plugins from a Plugins folder"

### 24.2 Editor extensions
- **What:** Add windows, buttons and menu commands to the editor itself, written in OkayScript, working on the open scene.
- **Parts:**
  - 24.2.1 Plugin files and live reload - `editor/main.cpp` - `<project>/EditorPlugins/*.okay` and `<editor>/plugins/*.okay`; header comments `@name`, `@description`, `@window`; saving a plugin reloads it
  - 24.2.2 Plugins menu and Plugin Manager - `editor/main.cpp` - New Plugin template, enable and disable (remembered), errors marked, open folder
  - 24.2.3 Hooks - `editor/main.cpp` - `start()`, `on_gui()`, `on_menu()`, `on_selection()`, `on_tick()`
  - 24.2.4 `editor_*` API and panel widgets - `editor/main.cpp` - selection, create, delete, rename, transform, undo point, log, plus `ui_*` widgets and `editor_input`, `editor_heading`, `editor_hint`
  - 24.2.5 Example plugins - `examples/editor-plugins/AlignTools.okay`, `examples/editor-plugins/CleanupNames.okay` - two sample plugins to copy into a project
- **Code:** `editor/main.cpp`, `Scripting/OkayScriptVM`, `Scripting/ScriptUI`, `examples/editor-plugins/AlignTools.okay`, `examples/editor-plugins/CleanupNames.okay`
- **API:** plugin hooks above; `editor_selected`, `editor_create`, `editor_set_pos`, `editor_undo_point`, `editor_log`
- **Editor:** Plugins menu, Plugin Manager window
- **Docs:** `docs/editor-plugins.md`
- **Tests:** `editor_plugins`
- **Status:** Works - script plugins load, reload, draw panels and act on the scene (read in `editor/main.cpp`; one ctest).
- **Exists today:** All of the Parts. Scene objects are addressed by name only, and a plugin cannot add an Inspector section, a toolbar button, a shortcut or a gizmo.
- **Work packages:**
  1. S - `editor_api_version()` builtin so a plugin can check what it can call. Depends on: nothing.
  2. M - Extension points for Inspector sections of a given component (the registry's `PropertyBuilder` describes fields) and for toolbar buttons and shortcuts. Depends on: the property registry (on main, #578, 1.19).
  3. M - Plugins can add Project panel context-menu items and asset importers. Depends on: 20.1 package 2.
  4. M - API for assets and project files (create a prefab, list assets) through the database. Depends on: 20.1 package 1.
  5. S - Address objects by Guid as well as name. Depends on: #570 (on main).
- **Open:** objects are found by name, so duplicates are ambiguous; every plugin runs in the editor process with full file access (OkayScript builtins include files).
- **Related:** 15.8, 13.5, 14.4, 24.4
- **Words:** editor plugin, Plugins menu, Plugin Manager, on_gui, editor_*, EditorPlugins, extension, custom window, Align Tools
- **Seen as:** a plugin panel does not appear; plugin shows "(error)"; cannot add my own Inspector section
- **Ask me like:** "let plugins add an Inspector section"; "add toolbar buttons from a plugin"

### 24.3 Custom nodes
- **What:** Add new node types to the visual scripting graph from outside the engine (from a script or a plugin) with their own pins and help text.
- **Parts:**
  - 24.3.1 Built-in node factory - `VisualScript/Nodes`, `VisualScript/NodeGraph` - `MakeNode(type, args)` builds nodes from a fixed list; no registration from outside
  - 24.3.2 User functions - `VisualScript/NodesFunctions` - a graph can define functions with inputs and outputs and call them (reusable sub-graphs, not new node types)
  - 24.3.3 Generated Engine API nodes - `Script/EngineApi`, `docs/engine_api.md` - one node per script property and method, generated from the binding table
- **Code:** `VisualScript/Nodes`, `VisualScript/NodeGraph`, `VisualScript/NodesFunctions`, `VisualScript/NodesExtra`, `Script/EngineApi`
- **API:** `vs::MakeNode`, `CallFunctionNode`
- **Editor:** Visual Script editor node palette
- **Docs:** `docs/visual_scripting.md`, `docs/engine_api.md`
- **Tests:** `vs_nodes`, `vs_types`, `vs_acceptance`
- **Status:** Planned - nothing lets outside code register a node.
- **Exists today:** About a dozen node header files plus generated Engine API nodes (a test fails when `docs/engine_api.md` is out of date). Functions give reuse inside one graph.
- **Work packages:**
  1. M - Node registry: `RegisterNodeType(name, factory, pins, help)` consulted by `MakeNode` and the editor palette. Depends on: nothing.
  2. M - Script-defined nodes: an OkayScript function with a `// @node` header becomes a node. Depends on: package 1.
  3. S - Palette shows the registered nodes in a "Custom" group with their help text. Depends on: package 1.
  4. M - Plugin-provided nodes through the C++ plugin ABI (24.1). Depends on: package 1, 24.1 package 3.
  5. M - Generate get and set nodes from the property registry (`PropertyDesc` has type, range and tooltip). Depends on: the property registry (on main, #578, 1.19).
- **Open:** node text format must keep loading old graphs when a custom node type is missing (placeholder node).
- **Related:** 14.3, 14.4, 24.1, 24.2
- **Words:** custom node, visual script node, node registry, MakeNode, plugin node, node SDK, Engine API nodes
- **Seen as:** a graph from a plugin author loads with missing nodes; I want a node for my own system
- **Ask me like:** "add a node registry so plugins can add nodes"; "turn an OkayScript function into a node"

### 24.4 Plugin SDK
- **What:** The package a third party downloads to build plugins: headers, libraries, documentation and a starter project, with version rules.
- **Parts:**
  - 24.4.1 Script plugin starter - `docs/editor-plugins.md`, `examples/editor-plugins/AlignTools.okay` - the guide, two examples and Plugins > New Plugin template
  - 24.4.2 C++ SDK package - `-` - not built; the engine has no CMake install or export rules
- **Code:** `examples/editor-plugins/AlignTools.okay`, `examples/editor-plugins/CleanupNames.okay`, `engine/CMakeLists.txt`
- **API:** Plugins > New Plugin
- **Editor:** Plugins menu
- **Docs:** `docs/editor-plugins.md`
- **Tests:** `editor_plugins`
- **Status:** Partial - the script side has docs and examples; the C++ SDK does not exist.
- **Exists today:** Guide plus two sample plugins. Releases ship the editor, player and launcher but no SDK zip.
- **Work packages:**
  1. S - Mark the editor API stable-by-name and publish a changelog section in `docs/editor-plugins.md`. Depends on: 24.2 package 1.
  2. M - CMake `install()` and exported targets so `find_package(Okay)` works. Depends on: nothing.
  3. M - Release workflow builds an SDK zip (headers, libs, sample plugin, docs). Depends on: package 2, 24.1 package 4.
  4. S - Plugins > New C++ Plugin template. Depends on: package 3.
- **Open:** versioning rule for the engine ABI is undecided (see 25.3).
- **Related:** 24.1, 24.2, 18.4, 25.4
- **Words:** plugin SDK, SDK, developer kit, install headers, find_package, sample plugin, plugin template
- **Seen as:** where do I get the headers to build a plugin; plugin built for an older editor breaks
- **Ask me like:** "add install rules so other projects can link the engine"; "ship an SDK zip with each release"

## 25. Project management (High)

### 25.1 Templates
- **What:** Starting points for a new project: a blank scene, a genre starter or a complete small game.
- **Parts:**
  - 25.1.1 Launcher Create page - `launcher/main.cpp` - 21 templates in four groups (All, Start simple, Game kits, Examples and systems) with search; `CreateProject` makes the folder and `Assets`, the editor fills in the template scene headless
  - 25.1.2 Editor starter content - `editor/main.cpp`, `Scene/SceneTemplates` - New Scene templates (3D, 2D) and built-in object templates
  - 25.1.3 Marketplace templates - `Platform/Workshop` - Workshop items of kind "template" that the launcher downloads
- **Code:** `launcher/main.cpp`, `editor/main.cpp`, `Scene/SceneTemplates`, `Platform/Workshop`
- **API:** launcher Create page; editor `--template` option
- **Editor:** File > New, launcher Create
- **Docs:** `docs/editor.md`, `docs/making_a_game.md`
- **Tests:** `templates`, `humanoid_template`, `ui_templates`
- **Status:** Works - creating a project from a template is used daily (read in `launcher/main.cpp`).
- **Exists today:** 21 template cards (3D Scene, 2D Scene, Empty, First Person, Third Person, Third Person Shooter, Platformer, Top-Down, Top-Down Shooter, 2.5D Platformer, Point and Click, two Vehicles, Sandbox, Multiplayer, Coin Collector, Snake, Brick Breaker, Main Menu, Inventory, Humanoid). No user-made templates.
- **Work packages:**
  1. M - "Save project as template" copies a project minus caches into a template folder the Create page lists. Depends on: 25.2.
  2. S - Template metadata file (name, description, engine version, thumbnail). Depends on: package 1.
  3. S - Template picks a `.gitignore` and project settings preset. Depends on: 28.1 package 1.
  4. M - Template thumbnails rendered from the template scene. Depends on: 20.5.
- **Open:** template list is hard-coded in the launcher (and must match the editor's list "exactly" per a comment); no template versioning.
- **Related:** 1.6, 17.1, 15.7, 51.1
- **Words:** project template, Create page, New Project, starter, genre template, kit, template gallery, Humanoid template
- **Seen as:** new project is empty or missing the controller I expected; template differs between launcher and editor
- **Ask me like:** "let users save their project as a template"; "add a Roguelike template to the Create page"

### 25.2 Project settings
- **What:** One place for project-wide choices such as game name, window size, physics gravity and rendering defaults.
- **Parts:**
  - 25.2.1 Project Settings window - `editor/main.cpp` - Edit > Project Settings: company, version, default window size, 2D and 3D gravity, ambient, skybox, editor loading screen; Save writes `project.okayproj`
  - 25.2.2 Custom tags - `editor/main.cpp` - project-wide tag list kept in `project.okayproj`
  - 25.2.3 Build Settings - `editor/main.cpp` - window options, scenes in build and named build profiles (see 15.7)
  - 25.2.4 Layer and tag data in scenes - `Scene/Layers`, `Scene/Tags` - layer matrix and tags (#570); not in project settings yet
- **Code:** `editor/main.cpp`, `Scene/Layers`, `Scene/Tags`
- **API:** Edit > Project Settings, `project.okayproj` keys `company`, `version`, `width`, `height`, `gravity2d`, `gravity3d`, `ambient`, `skybox`, `tag`
- **Editor:** Project Settings window, Build Settings dialog
- **Docs:** `docs/editor.md`
- **Tests:** `layers_tags_cameras` (layers and tags only)
- **Status:** Partial - the window exists with a few sections; many project-level choices live elsewhere or nowhere.
- **Exists today:** The window and file above. Settings apply to new builds and can be pushed into the open scene with a button. The file is read from the editor's working directory (`project.okayproj`).
- **Work packages:**
  1. S - Add sections for Quality presets, Input (default actions), Layers and Tags (with the layer matrix), Audio defaults. Depends on: nothing.
  2. M - Versioned file: `format=2` plus engine version stamp, unknown keys kept. Depends on: 25.3 package 1.
  3. S - Search box and "reset to default" per section. Depends on: package 1.
  4. M - Per-platform overrides (Windows, Web) feeding Build Settings. Depends on: 56 proposal.
- **Open:** `project.okayproj` is loaded from the current directory, so opening a project from elsewhere depends on the working directory; fixed buffers (`char company[128]`) truncate long names silently; possible bug: not verified.
- **Related:** 15.7, 25.3, 1.6, 12.8
- **Words:** project settings, project.okayproj, company, version, default resolution, gravity, tags, project defaults
- **Seen as:** window size ignored in the built game; settings lost when opening a project from another folder
- **Ask me like:** "add a Layers and Tags section to Project Settings"; "store quality presets in project settings"

### 25.3 Migration
- **What:** Open a project made with an older engine version and have scenes, prefabs and settings brought up to date safely.
- **Parts:**
  - 25.3.1 Scene format version - `Scene/SceneSerializer` - `kSceneFormatVersion` is 3; newer files still load with unknown records skipped and a warning; version 0 or a non-number is an error
  - 25.3.2 Save and import-settings migrations - `Core/SaveGame`, `Core/ImportSettings` - `SaveGame::SetMigration(fromVersion, fn)`; import settings upgrade old layouts on read
  - 25.3.3 Version helpers - `Core/BuildInfo` - `CompareVersions`, `IsCompatible`, `Version()` (merged in #571)
  - 25.3.4 Scene migration steps (#578) - `Scene/SceneMigration` - `MigrateToCurrent(text)` runs registered `N -> N+1` steps on the scene text and writes canonical text, `Register(step)`, `Describe()`, `MigrateThroughEngine` for upgrades that need component classes; a newer or damaged file comes back untouched
- **Code:** `Scene/SceneSerializer`, `Core/SaveGame`, `Core/ImportSettings`, `Core/BuildInfo`, `Scene/SceneMigration`
- **API:** `SceneSerializer::kSceneFormatVersion`, `SaveGame::SetMigration`, `BuildInfo::IsCompatible`, `SceneMigration::MigrateToCurrent`, `SceneMigration::Register`
- **Editor:** none
- **Docs:** `docs/scene_serializer.md`, `docs/core_tools.md`, `docs/scene_diff_merge.md`
- **Tests:** `scene_safe_load`, `structure_version`, `core_audit`, `scene_migration`
- **Status:** Partial - forward compatibility plus a scene text migration library (#578) exist; the two built-in steps (1 to 2, 2 to 3) only raise the number when nothing needs the engine, and no editor dialog or version stamp uses it.
- **Exists today:** Old scenes keep loading because the loader ignores what it does not know and defaults what is missing; nothing in the editor rewrites an old scene or tells the user it was made by an older build; `SceneMigration` can, but nothing calls it.
- **Work packages:**
  1. S - Stamp scenes and `project.okayproj` with the engine version that wrote them. Depends on: nothing.
  2. S - Register a step (and a hand-written old-format fixture in `scene_migration`) whenever `kSceneFormatVersion` is bumped; the registry already exists. Depends on: #578 (on main).
  3. M - "Upgrade project" dialog: back up first (22.4), migrate scenes and prefabs, list what changed. Depends on: #578 (on main, `SceneMigration`), 22.4 package 1.
  4. S - Warn when a scene was saved by a newer build and would lose data on save. Depends on: package 1.
- **Open:** no policy yet on how long old formats stay readable; `MigrateThroughEngine` drops records this build does not know and gives id-less objects new ids, so it is only for files from older builds.
- **Related:** 1.5, 20.4, 28.2, 35.4
- **Words:** migration, upgrade project, scene version, format version, old project, forward compatible, SetMigration, IsCompatible
- **Seen as:** old project opens with missing pieces; a newer scene loses data in an older editor
- **Ask me like:** "add an Upgrade Project command"; "stamp the engine version into saved scenes"

### 25.4 Dependency management
- **What:** Track which packages (asset packs, plugins, templates) a project uses and which versions, and install or update them.
- **Parts:**
  - 25.4.1 Marketplace downloads into a project - `editor/main.cpp`, `Platform/Workshop` - the Asset Manager window imports downloaded items into Assets; no version is recorded
  - 25.4.2 Package manifest and installer - `-` - not built
- **Code:** `editor/main.cpp`, `Platform/Workshop`
- **API:** `WorkshopItem` (Workshop service)
- **Editor:** Asset Manager window
- **Docs:** `docs/editor.md`
- **Tests:** none
- **Status:** Planned - items are copied in with no record of where they came from.
- **Exists today:** Import of a downloaded item copies files and extracts archives into the project; the `.okayasset` sidecar (title, author, kind) is only read.
- **Work packages:**
  1. M - `packages.okaypkg` manifest: name, version, source, content hash per installed package. Depends on: 20.3.
  2. M - Installer puts a package under `Packages/<name>/` and records it. Depends on: package 1.
  3. M - Package Manager window: installed, updates, remove, reinstall. Depends on: package 2.
  4. S - Engine version requirement on a package, checked with `BuildInfo::IsCompatible`. Depends on: package 1.
  5. M - Conflict report when two packages ship the same file. Depends on: 20.2.
- **Open:** where packages are hosted (Workshop, GitHub) is undecided.
- **Related:** 17.6, 20.1, 20.3, 24.4
- **Words:** package manager, dependencies, asset pack, version, update available, Marketplace import, Workshop item, manifest
- **Seen as:** I imported an asset pack and cannot tell which version it is; updating a pack overwrites my edits
- **Ask me like:** "add a package manifest for imported asset packs"

## 26. Shader & material editor (High)

### 26.1 Visual shader graphs
- **What:** Build a surface look by connecting nodes (textures, colours, maths, time) instead of writing shader code.
- **Parts:**
  - 26.1.1 Node graph editor (for gameplay logic, reusable canvas) - `editor/VisualScriptEditor.hpp`, `VisualScript/NodeGraph` - not a shader editor today
  - 26.1.2 Shader graph - `-` - not built
- **Code:** `editor/VisualScriptEditor.hpp`, `VisualScript/NodeGraph`
- **API:** none yet
- **Editor:** none yet
- **Docs:** `docs/visual_scripting.md` (logic graphs)
- **Tests:** none
- **Status:** Planned - no shader graph data model exists.
- **Exists today:** Only the logic graph editor, which could supply the canvas. All shading is hard-coded in each renderer.
- **Work packages:**
  1. M - Shader graph file format `.okayshader` (nodes, pins, outputs albedo, emissive, alpha, normal). Depends on: nothing.
  2. L - CPU evaluator in the software renderer as the reference. Depends on: package 1.
  3. L - Generate GLSL and HLSL snippets inserted into the standard shaders (GL, D3D11). Depends on: package 2.
  4. M - Editor window reusing the node canvas, with a live preview. Depends on: package 1, 26.2 package 2.
  5. M - A material can point at a shader graph asset. Depends on: package 1, 26.2.
- **Open:** four renderers (software, GL, D3D11, D3D12) must stay consistent; D3D12 has no normal, gloss or AO maps today.
- **Related:** 2.1, 2.2, 14.1, 26.3
- **Words:** shader graph, visual shader, node shader, material graph, shader nodes, procedural material
- **Seen as:** cannot make a custom effect without editing engine code
- **Ask me like:** "design a shader graph file format"; "add a fresnel node to a shader graph"

### 26.2 Material editor
- **What:** A window to design a material (colours, textures, shininess) with a live preview and reuse it on many objects.
- **Parts:**
  - 26.2.1 Material files - `Render/Material` - `.okaymat` text file: colour, emissive, specular, shininess, unlit, double-sided, texture, tiling, shader style, rim, outline, UV scroll, triplanar
  - 26.2.2 Material window - `editor/main.cpp` - albedo, emissive, specular, shininess, unlit, double-sided, texture, tiling, preview ball, Save, Apply to Selection
  - 26.2.3 Material preset in the MeshRenderer inspector - `editor/main.cpp` - save and load the renderer's surface as a `.okaymat`
  - 26.2.4 Procedural textures - `Render/ProcTexture` - `proc:checker`, `proc:brick`, `proc:wood` and 9 more, usable wherever a texture path goes
- **Code:** `Render/Material`, `Render/ProcTexture`, `Components/MeshRenderer`, `editor/main.cpp`
- **API:** `Material::SaveToFile`, `LoadFromFile`, `ApplyTo`, `FromRenderer`
- **Editor:** Material window (double-click a `.okaymat`), MeshRenderer inspector "Material preset"
- **Docs:** `docs/rendering.md`
- **Tests:** `material`, `shaders`
- **Status:** Partial - the file and window work; the window shows only part of what the file stores and the preview is a flat drawn ball.
- **Exists today:** The window (`DrawMaterialEditor`) edits 8 of the file's fields; shader style, toon bands, rim, outline, UV scroll and triplanar are in the file and the MeshRenderer inspector but not in the window. The preview ball is drawn with 2D shapes, not rendered with the engine. Normal, specular, AO maps, metallic and reflectivity are MeshRenderer fields that `.okaymat` does not store.
- **Work packages:**
  1. S - Show every field the `.okaymat` already stores in the Material window. Depends on: nothing.
  2. M - Real preview: render a sphere with the material through the software renderer into the window. Depends on: nothing (or `RenderSceneToImage` from #572).
  3. M - `.okaymat` version 2 with metallic, reflectivity, normal, specular and AO maps, texture offset. Depends on: 26.4 package 1.
  4. S - "Used by" list and Select All Using This Material. Depends on: 20.2.
  5. M - Material instances (a material overriding a base material). Depends on: package 3, 21.3 pattern.
- **Open:** preview is not what the renderer draws; materials are copied into the renderer (`ApplyTo`), not referenced, so editing the file does not update objects already using it.
- **Related:** 2.2, 15.4, 26.4, 3.5
- **Words:** material editor, okaymat, material window, albedo, emissive, shininess, material preset, Apply to Selection, proc textures
- **Seen as:** edited a material and objects did not change; preview does not look like the scene; cannot set a normal map in the material
- **Ask me like:** "make the Material window show the shader and outline fields"; "render a real preview sphere in the Material window"

### 26.3 Custom shaders
- **What:** Use your own shader code or graph on a material, on every renderer.
- **Parts:**
  - 26.3.1 Built-in shader styles - `Components/MeshRenderer`, `Render/SoftwareRenderer`, `Render/GLRenderer`, `Render/D3D11Renderer`, `Render/D3D12Renderer` - Standard, Unlit, Toon, Gradient, Fresnel, Iridescent, Hologram, Posterize, Velvet
  - 26.3.2 Renderer shader sources - `Render/GLRenderer` (GLSL 120 strings), `Render/D3D11Renderer` and `Render/D3D12Renderer` (HLSL strings compiled at startup)
  - 26.3.3 User shader files - `-` - not built
- **Code:** `Components/MeshRenderer`, `Render/SoftwareRenderer`, `Render/GLRenderer`, `Render/D3D11Renderer`, `Render/D3D12Renderer`
- **API:** `MeshRenderer::shader` (enum `Shader`)
- **Editor:** MeshRenderer inspector "Shader" selector
- **Docs:** `docs/rendering.md`
- **Tests:** `shaders`, `software_renderer`
- **Status:** Planned - user-supplied shaders do not exist; the nine built-in styles are fixed per renderer (see 26.4 for the lighting model).
- **Exists today:** A closed set of styles, each implemented once per renderer. Adding one means editing four renderers.
- **Work packages:**
  1. M - Shader style table shared by all renderers so a new built-in style is one entry plus per-renderer code. Depends on: nothing.
  2. L - Custom shader asset: fragment snippet in a small portable subset, compiled to GLSL and HLSL. Depends on: 26.1 package 3.
  3. M - Software renderer fallback: run the snippet on the CPU or fall back to Standard with a warning. Depends on: package 2.
  4. S - Shader compile errors shown in the Console with file and line (23.4). Depends on: package 2.
- **Open:** D3D12 is "v1" and opt-in; shader hot reload would need 34.3.
- **Related:** 2.1, 26.1, 34.3
- **Words:** custom shader, shader file, GLSL, HLSL, shader style, Toon, Hologram, Iridescent, MeshRenderer.shader
- **Seen as:** a style looks different between renderers; no way to add my own look
- **Ask me like:** "add a Dissolve shader style to all four renderers"

### 26.4 PBR
- **What:** A physically based lighting model (base colour, metallic, roughness, normal, ambient occlusion) so materials look right under any light.
- **Parts:**
  - 26.4.1 Surface fields - `Components/MeshRenderer` - metallic, reflectivity (sky reflection, Fresnel weighted), specular and shininess (Blinn-Phong), normal map and strength, specular/gloss map, AO map and strength, emissive, matcap
  - 26.4.2 Per-renderer support - `Render/SoftwareRenderer`, `Render/GLRenderer`, `Render/D3D11Renderer`, `Render/D3D12Renderer` - D3D12 skips normal, gloss and AO maps and PBR reflections (header says "v1")
  - 26.4.3 glTF import mapping - `Render/Gltf` - metallic-roughness factors become a Blinn-Phong glint
  - 26.4.4 Lighting and shadows - `Render/Lighting`, `Render/ShadowSetup`, `Render/Lightmap` - directional, point and spot lights, shadows (see 2.3)
- **Code:** `Components/MeshRenderer`, `Render/Lighting`, `Render/Gltf`, `Render/Lightmap`, `Render/ShadowSetup`, `Render/D3D12Renderer`
- **API:** `MeshRenderer::metallic`, `reflectivity`, `normalMap`, `specularMap`, `aoMap`, `shininess`
- **Editor:** MeshRenderer inspector
- **Docs:** `docs/rendering.md`, `docs/lighting.md`
- **Tests:** `lighting`, `light_shadows`, `material`, `post_sky`
- **Status:** Partial - metalness, reflections and maps exist; there is no roughness parameter and no image-based lighting.
- **Exists today:** "PBR-ish" Blinn-Phong with metallic and environment reflection from the procedural sky. Roughness maps and a packed metallic-roughness texture are not read.
- **Work packages:**
  1. M - Define the PBR parameter set (base colour, metallic, roughness, normal, AO, emissive) and a conversion from today's fields (shininess to roughness), with golden-image tests. Depends on: nothing.
  2. M - Roughness and metallic maps (separate or packed) in MeshRenderer, scene text and `.okaymat`. Depends on: package 1.
  3. L - Energy-conserving BRDF in the software reference renderer. Depends on: package 1.
  4. L - Same model in GLSL and HLSL (GL, D3D11). Depends on: package 3.
  5. M - glTF import fills the real metallic-roughness and occlusion maps. Depends on: package 2.
  6. L - Reflection probes or sky-based image lighting. Depends on: package 4.
- **Open:** four renderers to keep equal; D3D12 lacks maps; the software renderer is the reference.
- **Related:** 2.3, 2.2, 3.6, 26.2
- **Words:** PBR, physically based, metallic, roughness, reflectivity, normal map, AO map, specular map, matcap, albedo
- **Seen as:** metal looks like plastic; imported glTF materials look flat; D3D12 view lacks bump detail
- **Ask me like:** "add a roughness parameter and map"; "make glTF import use the metallic-roughness texture"

## 27. Optimization tools (High)

### 27.1 LOD generation
- **What:** Make cheaper copies of a mesh for when it is far away and switch between them automatically, so big scenes stay fast.
- **Parts:**
  - 27.1.1 Quadric mesh simplifier (#574) - `Optimize/MeshSimplify` - edge-collapse simplification that keeps topology, boundaries, UV seams, hard edges, colours and skin weights; `GenerateLodChain`
  - 27.1.2 LOD group data and selection (#574) - `Optimize/LodGroup` - `SelectLod` with hysteresis and cross-fade, switch distances from screen error
  - 27.1.3 Existing vertex-clustering Decimate - `Modeling/Modifiers`, `Render/Mesh` - a simple Decimate modifier (merge vertices in grid cells) on main; not topology-safe
  - 27.1.4 Mip budget (#574) - `Optimize/MipBudget` - which texture mip levels to keep resident for a memory budget
- **Code:** `Components/LODGroup`, `Render/Visibility`, `Optimize/MeshSimplify`, `Optimize/LodGroup`, `Optimize/MipBudget`, `Modeling/Modifiers`, `Render/Mesh`
- **API:** `SimplifyMesh`, `GenerateLodChain`, `LodGroup`, `SelectLod`, `MakeLodGroup`; on main: `Mesh::Decimate`, Decimate modifier
- **Editor:** LOD Group inspector (Add Component > Rendering) and distance rings in the Scene view
- **Docs:** `docs/optimization_tools.md`
- **Tests:** `optimize_simplify`, `visibility_lod`
- **Status:** Partial - `LODGroup` is a component with an Inspector, distances or screen size, Generate LODs and Scene-view rings. OpenGL, Direct3D 11, Direct3D 12 and the software renderer pick the level with `SelectLod`. The mip budget is still unused.
- **Exists today:** A Mesh Renderer with an LOD Group switches meshes by distance or screen size in every backend. Hysteresis uses the last choice. Skinned meshes stay on the full mesh. Generate builds 100/50/25/10 percent levels.
- **Work packages:**
  1. M - `LODGroup` component (levels, thresholds, fade) with serializer entry and Inspector. Depends on: #574 (on main).
  2. M - Software, GL and D3D11 visibility passes pick the level with `SelectLod` per camera and shadow pass. Depends on: package 1.
  3. S - "Generate LODs" button on a MeshRenderer: ratios 100/50/25/10 percent through `GenerateLodChain`. Depends on: package 1.
  4. M - Scene view LOD preview: tint by level, force a level, show triangle savings. Depends on: package 2.
  5. S - LOD saving shown in the Stats panel and the optimization report (27.3). Depends on: package 2.
- **Open:** simplifier ignores colour and material changes in its error; a mesh with `cornerUVs` is split into per-vertex UVs first; skinned meshes need skin data passed in by hand.
- **Related:** 2.8, 3.3, 3.5, 27.3, 23.1
- **Words:** LOD, level of detail, mesh simplify, decimate, LODGroup, switch distance, cross fade, GenerateLodChain, MeshSimplify
- **Seen as:** frame rate drops with many high-poly objects far away; objects pop when they change detail
- **Ask me like:** "add an LOD Group component and use it in the renderers"; "generate three LOD levels for the selected mesh"

### 27.2 Occlusion culling
- **What:** Do not draw things hidden behind walls or buildings, in addition to skipping what is outside the camera view.
- **Parts:**
  - 27.2.1 Frustum culling - `Render/Culling` - world-space bounding spheres tested against the camera and shadow light frustum before any draw; switch `FrustumCullingEnabled`; counters `LastCullCounters`
  - 27.2.2 Particle depth occlusion - `Render/SoftwareRenderer` - the scene depth buffer hides particles behind geometry (`SceneOcclusionDepth`)
  - 27.2.3 CPU occlusion buffer (#574) - `Optimize/OcclusionBuffer` - rasterises occluders into a depth buffer and tests bounds against it
  - 27.2.4 Portal cells (#574) - `Optimize/PortalCells` - room and portal visibility for indoor scenes
- **Code:** `Render/Culling`, `Render/SoftwareRenderer`, `Optimize/OcclusionBuffer`, `Optimize/PortalCells`
- **API:** `Frustum`, `FrustumCullingEnabled`, `LastCullCounters`; `OcclusionBuffer`, `PortalCells`
- **Editor:** Stats panel shows drawn and culled counts
- **Docs:** `docs/optimization_tools.md`
- **Tests:** `frustum_culling`, `optimize_occlusion`, `visibility_lod`
- **Status:** Partial - frustum culling works on all renderers. An Occluder component is rasterized into `OcclusionBuffer` and the visibility pass skips what it hides (Stats shows Occluded). `PortalCells` is still unused.
- **Exists today:** Without an Occluder, nothing is occlusion-culled, so old scenes are unchanged. With one, objects behind it can be skipped. The buffer is conservative and CPU-side; on a tiny scene the test can cost more than the triangles it skips.
- **Work packages:**
  1. M - `Occluder` marker (box or mesh) and `OcclusionVolume`; objects flagged Static are candidate occluders. Depends on: #574 (on main).
  2. M - Visibility pass tests candidates against `OcclusionBuffer` after frustum culling, behind a setting. Depends on: package 1.
  3. S - Stats shows "occluded" next to drawn and culled. Depends on: package 2.
  4. M - Room and portal authoring for indoor levels (`PortalCells`). Depends on: package 2.
  5. M - Scene view debug view of the occlusion buffer. Depends on: package 2.
- **Open:** CPU rasteriser only (no GPU queries); conservative, so it may draw more than needed; no components yet.
- **Related:** 2.8, 27.1, 27.3, 23.3
- **Words:** occlusion culling, frustum culling, portal, occluder, visibility, hidden objects, OcclusionBuffer, drawn culled
- **Seen as:** thousands of objects drawn behind a wall; indoor levels slow
- **Ask me like:** "turn on occlusion culling in the renderers"; "add an Occluder component"

### 27.3 Batching
- **What:** Draw many objects with few draw calls by combining static meshes and instancing repeats, and report where a scene wastes work.
- **Parts:**
  - 27.3.1 UI batching - `UI/UIBatcher` - UI draw calls become triangles in one buffer, a few Geometry calls per atlas page and clip run
  - 27.3.2 Static flag - `Scene/GameObject` - `isStatic` exists (Inspector "Static" box, saved as `static 1`) but no renderer reads it
  - 27.3.3 Batch planner and mesh merge (#574) - `Optimize/BatchPlanner` - plan instanced, static-merged and single batches, `MergeStaticMeshes`
  - 27.3.4 Scene optimization report (#574) - `Optimize/SceneOptimizationReport` - findings such as tiny draw calls, huge textures on small objects, high poly without LOD, duplicate materials, overdraw risk
- **Code:** `UI/UIBatcher`, `Scene/GameObject`, `Optimize/BatchPlanner`, `Optimize/SceneOptimizationReport`
- **API:** `ui2d::BatchPainter`; `GameObject::isStatic`; `PlanBatches`, `BuildMergedMeshes`, `MergeStaticMeshes`, `AnalyzeScene`
- **Editor:** Inspector "Static" checkbox
- **Docs:** `docs/optimization_tools.md`
- **Tests:** `ui_render_bench`, `optimize_batching`, `optimize_report`, `visibility_lod`
- **Status:** Partial - the UI is batched, and the mesh renderers merge static (and small dynamic) opaque objects that share a material via `MergeStaticMeshes`. The optimization report window is still not in the editor.
- **Exists today:** Stats shows Batches and Draws saved. Skinned, transparent, outlined, scrolling and ground-shadow fallback meshes stay one draw each. A scene with nothing safe to merge is unchanged.
- **Work packages:**
  1. S - Optimization report window: run `AnalyzeScene` on the open scene, list findings worst first, click to select. Depends on: #574 (on main).
  2. M - Draw-call and state-change counts in Stats (needs the recorder in 23.3). Depends on: 23.3 package 1.
  3. M - At Play start or Build, merge Static objects per material with `MergeStaticMeshes`. Depends on: #574 (on main).
  4. L - GPU instancing of repeated meshes in GL and D3D11. Depends on: package 2.
  5. S - Make "Static" mean something in the Inspector tooltip (batching, occluder, navmesh bake). Depends on: package 3.
- **Open:** `isStatic` is only "a hint for tools"; merged meshes need a draw path in each renderer; skinned and transparent objects are never merged.
- **Related:** 2.1, 2.8, 12.1, 23.3, 27.1
- **Words:** batching, static batching, instancing, draw calls, merge meshes, isStatic, BatchPlanner, optimization report, overdraw
- **Seen as:** too many draw calls; a scene of thousands of identical props is slow; no hint what to fix
- **Ask me like:** "add an Optimization Report window"; "batch static meshes when the game starts"

### 27.4 Streaming
- **What:** Keep only the parts of a big world near the player in memory, loading and unloading as they move.
- **Parts:**
  - 27.4.1 World streamer component - `Components/WorldStreamer` - grid of cell prefabs named `<prefix>_<x>_<z>.okayprefab`; loads cells within a ring of the camera or a named target, unloads past a larger ring (hysteresis)
  - 27.4.2 Async scene loading - `Scene/SceneManager`, `Scene/Scene` - multi-frame loads with progress and additive load/unload by handle (merged in #570)
  - 27.4.3 Asset manager async loads and hot reload - `Core/AssetManager` - background decode, `Pump`, progress (merged in #568, unused by the engine)
  - 27.4.4 Streaming grid and mip budget (#574) - `Optimize/StreamingGrid`, `Optimize/MipBudget` - load decisions with budgets, priorities by view direction, cancel and retry
- **Code:** `Components/WorldStreamer`, `Scene/SceneManager`, `Core/AssetManager`, `Optimize/StreamingGrid`, `Optimize/MipBudget`
- **API:** `WorldStreamer` (cellSize, loadRadius, unloadRadius, folder, prefix), `SceneManager` async load; `StreamingGrid`, `PlanMips`
- **Editor:** WorldStreamer Inspector section
- **Docs:** `docs/scene_management.md`, `docs/asset_manager.md`; `docs/optimization_tools.md`
- **Tests:** `worldstreamer`, `scene_load_pool`, `assetmanager`; `optimize_streaming`
- **Status:** Partial - cell streaming of prefabs works; `StreamingGrid` and `MipBudget` (budgeted, prioritised loads, #574, test `optimize_streaming`) are on main but `WorldStreamer` does not use them.
- **Exists today:** `WorldStreamer` instantiates and destroys cell prefabs on the main thread. Cells must be authored by hand as separate prefab files. Missing cell files are skipped.
- **Work packages:**
  1. M - Streamer uses `StreamingGrid` decisions and `AssetManager::LoadAsync` so a cell load never blocks a frame. Depends on: #574 (on main).
  2. M - Editor "Split scene into cells" command that writes the cell prefabs (see 45 proposal). Depends on: nothing.
  3. S - Debug overlay: grid cells coloured by state (unloaded, loading, loaded). Depends on: package 1.
  4. M - Keep navmesh and physics broadphase consistent when cells load and unload. Depends on: package 1.
  5. M - Mip streaming (`PlanMips`) tied to texture loads. Depends on: package 1.
- **Open:** a loaded cell is created in one frame (hitch risk); saves do not know about unloaded cells (see 35.3); cells are on the XZ plane only.
- **Related:** 1.4, 2.6, 20.1, 45.1, 35.3
- **Words:** streaming, world streamer, world partition, open world, cells, chunks, load radius, async load, mip streaming, StreamingGrid
- **Seen as:** hitch when walking into a new area; huge map uses too much memory; loading screen on every area change
- **Ask me like:** "make the world streamer load cells without hitches"; "split my big scene into streaming cells"

## 28. Version control integration (High)

### 28.1 Git integration
- **What:** Use git from the tools: clone a project, see what changed, commit, push, pull and switch branches without a terminal.
- **Parts:**
  - 28.1.1 Launcher git jobs - `launcher/main.cpp` - runs `git` on a background thread (needs git on the PATH) and reports a toast; works on any listed project
  - 28.1.2 Clone from GitHub - `launcher/main.cpp` - Projects page button
  - 28.1.3 Pull, Commit and Push, Publish to GitHub - `launcher/main.cpp` - project right-click menu: "Git: Pull", "Git: Commit & Push" (message dialog), "Publish to GitHub..." for projects without `.git`
  - 28.1.4 Editor Git panel - `-` - not built
- **Code:** `launcher/main.cpp`
- **API:** launcher project context menu
- **Editor:** launcher Projects page (no editor window)
- **Docs:** -
- **Tests:** none
- **Status:** Partial - the launcher drives git for whole projects; the editor shows nothing about git.
- **Exists today:** Clone, pull, commit and push, and publish are in `launcher/main.cpp` (read: `GitWorker`, `StartGit`, the "Git:" menu items). No status, no changed-file list, no branches, no `.gitignore` for new projects.
- **Work packages:**
  1. S - New projects get a `.gitignore` (editor view files, autosave and backup files, build output, `.okay/` caches) and a `.gitattributes` marking scenes as text. Depends on: nothing.
  2. M - Editor Git panel: branch name, changed files (`git status --porcelain`), badges in the Project panel. Depends on: package 1.
  3. M - Commit, Pull and Push buttons in the editor with a message box, run off the main thread. Depends on: package 2.
  4. M - Branch switcher; warn about unsaved scenes first and reload after. Depends on: package 2.
  5. L - Per-asset history and restore a file version from `git log` / `git show`. Depends on: package 2, 28.3.
- **Open:** no automated test; no conflict handling after a pull (see 28.4); shelling out to git means errors come back as text only.
- **Related:** 17.1, 28.2, 28.3, 28.4, 22.4
- **Words:** git, version control, commit, push, pull, clone, branch, GitHub, publish project, .gitignore
- **Seen as:** I want to commit from the editor; autosave files in my commits; pull leaves a broken scene
- **Ask me like:** "add a Git panel to the editor"; "generate a .gitignore for new projects"

### 28.2 Diffs
- **What:** See what changed between two versions of a scene or prefab in terms of objects and properties, not lines of text.
- **Parts:**
  - 28.2.1 Diff-friendly scene text - `Scene/SceneSerializer` - human-readable one-record-per-line text; stable object ids (`guid` records, #570); floats written with the fewest digits so saving does not drift (#575)
  - 28.2.2 Structural scene diff (#578) - `Scene/SceneDiff` - `SceneDiff::Compute(base, other)` lists added, removed and modified objects (by guid, else name path), renames, reparents, reorders, per-record and per-field changes (`Light.intensity 1 -> 2`) as text or JSON; `Apply` replays a diff; about 100 ms for 5000 objects (per docs)
  - 28.2.3 Canonical form - `Scene/SceneText` - `SceneText::Canonicalize` gives equal scenes equal text (layout, number spelling, record order, object numbering) so a re-save shows no diff; quoted strings are never touched
- **Code:** `Scene/SceneSerializer`, `Scene/SceneDiff`, `Scene/SceneText`
- **API:** `SceneDiff::Compute`, `SceneDiff::Apply`, `SceneDiff::Result::ToText`, `ToJson`, `SceneText::Canonicalize`
- **Editor:** none
- **Docs:** `docs/scene_serializer.md`, `docs/scene_diff_merge.md`
- **Tests:** `scene_ids_prefabs`, `scene_diff`, `scene_canonical`
- **Status:** Partial - the diff and canonical-form libraries are on main (#578) and tested; there is no editor window, no command line tool and no git diff driver.
- **Exists today:** `SceneDiff` and `SceneText::Canonicalize` work on the text of scene and prefab files without building a `Scene`. Plain git text diffs of `.okayscene` are still noisy until a tool calls them. Sibling order is only written when it changed (per `docs/scene_serializer.md`).
- **Work packages:**
  1. M - Command-line `okay-diff` usable as a git `textconv` or diff driver for scenes and prefabs (uses `Canonicalize` and `SceneDiff`). Depends on: #578 (on main).
  2. M - Editor "Compare with disk" and "Compare with last commit" window listing changed objects. Depends on: package 1, 28.1 package 3.
  3. S - Same engine for prefab diffs and the Project panel "what changed". Depends on: package 1.
  4. S - Editor saves scenes in canonical form so a re-save never shows a diff (check that `SceneSerializer::Serialize` output is already canonical, per docs it is). Depends on: nothing.
- **Open:** no UI and no tool call these yet; a file without ids pairs objects by name path, so a rename there looks like a removal plus an addition; component order is not saved (`docs/scene_serializer.md`), so it never shows in a diff.
- **Related:** 1.5, 21.4, 28.1, 28.4
- **Words:** scene diff, prefab diff, compare scenes, text diff, diff driver, git diff scene, changed objects
- **Seen as:** a one-object change shows hundreds of changed lines; cannot tell what a teammate changed
- **Ask me like:** "show a readable diff of two scene files"

### 28.3 Project history
- **What:** A timeline of how a project or an asset changed, with the ability to restore an older version.
- **Parts:**
  - 28.3.1 Previous-version backups - `Scene/SceneSerializer` - one `.bak` per saved scene (see 22.4)
  - 28.3.2 History from git - `-` - not built
- **Code:** `Scene/SceneSerializer`
- **API:** none yet
- **Editor:** none
- **Docs:** -
- **Tests:** none
- **Status:** Planned - no history view exists.
- **Exists today:** One previous scene version in a `.bak` file and the in-editor undo list for the current session (22.1).
- **Work packages:**
  1. M - Asset history list from `git log --follow` for the selected file. Depends on: 28.1 package 2.
  2. M - "Restore this version" of a scene or prefab, backing up the current one first. Depends on: package 1, 22.4 package 1.
  3. S - Local timeline without git using `BackupManager` snapshots. Depends on: 22.4 package 1.
  4. M - Project-level "what changed since" summary of scenes and assets. Depends on: #578 (on main, `SceneDiff`), 28.1 package 2.
- **Open:** depends on git being installed; projects without git only get local backups.
- **Related:** 22.4, 28.1, 28.2
- **Words:** project history, version history, restore version, timeline, previous version, file history
- **Seen as:** I broke a scene yesterday and need yesterday's version
- **Ask me like:** "show the history of this scene and let me restore a version"

### 28.4 Merge conflict tools
- **What:** Combine two people's edits to the same scene without losing work, and resolve conflicts object by object.
- **Parts:**
  - 28.4.1 Additive scene merge - `editor/main.cpp`, `Scene/SceneSerializer` - File > Merge Scene into Current (adds another scene's objects with an offset; not a version-control merge)
  - 28.4.2 Three-way merge (#578) - `Scene/SceneMerge` - `SceneMerge::Merge(base, ours, theirs, options)` merges changes to different objects, records or fields, with policies Mark, PreferOurs, PreferTheirs and a conflict list (field changed, deleted and edited, renamed differently, parent cycle...); the merged text always loads or falls back to ours
- **Code:** `editor/main.cpp`, `Scene/SceneSerializer`, `Scene/SceneMerge`
- **API:** `SceneSerializer::MergeFromText`, `MergeFromFile`, `SceneMerge::Merge`, `SceneMerge::Result::ReportText`; File > Merge Scene into Current
- **Editor:** File > Merge Scene into Current, Project panel "Add to Open Scene"
- **Docs:** `docs/scene_serializer.md`, `docs/scene_diff_merge.md`
- **Tests:** `scene_ids_prefabs`, `scene_merge`
- **Status:** Partial - the three-way merge library is on main (#578) and tested; there is no editor conflict window and no git merge driver (per docs: "Not included"); the only merge in the editor is the additive one.
- **Exists today:** Merging two scenes by adding objects. A git conflict inside a `.okayscene` leaves conflict markers; loading is all-or-nothing and reports a parse error (#553), so the scene will not open until fixed by hand.
- **Work packages:**
  1. S - Load error that says "conflict markers found" instead of a generic parse error. Depends on: nothing.
  2. M - `okay-merge` command for `git merge-driver` use (calls `SceneMerge::Merge`, runs `SceneMigration` first so both sides are at one version). Depends on: #578 (on main).
  3. M - Editor conflict window: per object and per field choose ours or theirs from `Result::conflicts`. Depends on: #578 (on main, `SceneMerge`), 28.2 package 2.
  4. S - Merge validator that loads the merged text into a scratch scene (`Options::validator`, the docs show how). Depends on: package 2.
- **Open:** the library only merges text of files at one format version; ours and theirs at different versions are not merged; possible duplicate naming with the additive "Merge Scene" command, rename one to avoid confusion.
- **Related:** 1.5, 28.1, 28.2
- **Words:** merge, merge conflict, three-way merge, conflict markers, merge driver, merge scene, resolve conflicts
- **Seen as:** scene will not open after a git merge; two people edited the same scene
- **Ask me like:** "resolve merge conflicts in a scene by object"

## 29. Terrain & world building tools (High)

### 29.1 Terrain sculpting
- **What:** Shape a landscape by painting hills and valleys with brushes in the Scene view, or generate it and refine it.
- **Parts:**
  - 29.1.1 Heightmap terrain - `Components/Terrain` - square grid of heights (resolution, size), rebuilt into a mesh; auto-colour by height and slope (water, sand, grass, rock, snow); texture with triplanar projection and normal map
  - 29.1.2 Sculpt brushes - `Components/Terrain`, `editor/main.cpp` - Raise/Lower, Smooth, Flatten/Set Height, Noise, Erode with radius, strength and hardness, painted in the 3D Scene view
  - 29.1.3 Whole-terrain tools - `Components/Terrain` - Generate (mountains, hills, plains, plateau, islands, ridged, canyons), hydraulic and thermal erosion, terrace, normalize, invert
  - 29.1.4 Heightmap import and export - `Components/Terrain` - grayscale PNG with black to `lowY` and white to `highY`
  - 29.1.5 Voxel terrain - `Components/VoxelTerrain`, `Components/TerrainDigger`, `Components/VoxelDigger` - marching-cubes terrain with caves and runtime digging
  - 29.1.6 Texture layers - `Components/Terrain`, `editor/LevelTools.*` - up to four ground layers (picture, tint, tiling) blended by a painted weight map; saved with the terrain (`terrainlayers`); drawn from one baked ground picture, so the software, OpenGL, Direct3D 11 and Direct3D 12 renderers all show it through the shared texture cache
  - 29.1.7 Terrain ray cast - `Components/Terrain` - `Terrain::Raycast` finds where a ray meets the height field; the sculpt, layer and foliage brushes land on the real surface of a hill
- **Code:** `Components/Terrain`, `Components/VoxelTerrain`, `Components/TerrainDigger`, `Components/VoxelDigger`, `editor/main.cpp`, `editor/LevelTools.*`
- **API:** `Terrain::RaiseAt`, `SmoothAt`, `FlattenAt`, `NoiseAt`, `Generate`, `Erode`, `ThermalErode`, `ExportHeightmap`, `ImportHeightmap`, `SampleHeight`, `SlopeAt`, `Raycast`, `AddLayer`, `RemoveLayer`, `PaintLayerAt`, `FillLayer`, `LayerWeightAt`
- **Editor:** 3D Modeling window "Terrain" tab, Terrain Inspector section (Texture layers), GameObject > Terrain
- **Docs:** `docs/modeling.md`, `docs/editor.md`, `docs/level_tools.md`
- **Tests:** `terrain`, `terrain_ops`, `voxel`, `voxel_dig`, `foliage`
- **Status:** Works - brushes and generators are in the engine and covered by tests; texture layers are covered by the `foliage` suite and checked in the real editor (software renderer); the GPU renderers read the same baked picture but were not run here.
- **Exists today:** Everything above on main; the planned item "sculpting brushes" is largely done.
- **Work packages:**
  1. M - Terrain undo per stroke (today a stroke uses the whole-scene snapshot). Depends on: 22.1 package 1.
  2. M - Large terrains: tiled terrain with neighbours stitched, with `WorldStreamer` cells (27.4). Depends on: 27.4 package 1.
  3. S - Height gizmo in the Scene view (the layer and foliage brushes already draw a ring on the surface). Depends on: nothing.
  4. M - Holes and cliffs (cut a hole for caves, sharp slopes). Depends on: nothing.
  5. M - Per-layer normal maps and a real multi-texture shader (the layers are baked into one picture, so detail is limited by its size). Depends on: 2.1.
- **Open:** the layers are baked into one ground picture (at most 2048 pixels a side), so fine texture detail over a very large terrain is limited by that size; resolution is one setting for the whole grid; there are no holes.
- **Related:** 2.6, 3.1, 29.2, 29.3, 27.4
- **Words:** terrain, sculpt, brush, heightmap, raise lower, smooth, flatten, erosion, generate landscape, voxel terrain, caves, dig, texture layers, splat, paint ground, weight map
- **Seen as:** terrain looks blocky or one colour; cannot paint a path or sand patch; huge terrain is heavy
- **Ask me like:** "paint grass and dirt on the terrain"; "make terrain strokes undoable one by one"

### 29.2 Vegetation painting
- **What:** Fill terrain with trees, rocks, bushes and grass by painting with a brush or by rules, without placing each by hand.
- **Parts:**
  - 29.2.1 Scatter button - `editor/LevelTools.*` - Terrain Inspector "Scatter props": choose prop (Tree, Pine, PalmTree, DeadTree, Bush, Rock, Mushroom, Cactus, Crystal), count (up to 5000), scale range, max slope and height band; the copies go into a Foliage layer under the terrain (one object per prop, not one per copy)
  - 29.2.2 Spline instancer - `Components/SplineMesh` - places copies of an object, prefab or mesh along a spline at a spacing (trees, lamp posts)
  - 29.2.3 Procedural prop shapes - `Render/Mesh` - `Mesh::FromNameSeeded` gives varied trees and rocks
  - 29.2.4 Foliage layer - `Components/Foliage` - one plant kind (built-in prop, mesh file or prefab) as a list of copies (position, turn, scale, lean); drawn merged into one mesh per 16 m square, so a forest is a few draw calls and whole squares out of view are skipped; saved in the scene (`foliage`); budget in triangles
  - 29.2.5 Foliage brush - `editor/LevelTools.*` - Paint, Erase (or Shift) and Rescale in the Scene view on a terrain or any surface, with radius, density, spacing, scale range, slope and height limits, follow-ground lean; one undo step per stroke
  - 29.2.6 Foliage checks - `Testing/SceneValidator` - `foliage.shape` (a plant that cannot be built), `foliage.budget` (over its triangle budget), `foliage.empty`, and missing mesh or prefab files
  - 29.2.7 Surface ray cast - `Render/SurfaceRaycast` - `RaycastSurfaces` finds the first mesh surface under a ray with no colliders needed (foliage, decals and the measuring tool use it)
- **Code:** `Components/Foliage`, `Render/SurfaceRaycast`, `editor/LevelTools.*`, `Components/SplineMesh`, `Components/Terrain`, `Render/Mesh`
- **API:** `Foliage::Paint`, `Erase`, `Rescale`, `ScatterOnTerrain`, `TerrainSurface`, `SceneSurface`, `EnsureBuilt`, `RaycastSurfaces`, `Terrain::SampleHeight`, `SlopeAt`, `SplineInstancer`, `Mesh::FromNameSeeded`
- **Editor:** GameObject > Foliage Layer, Add Component > Foliage, Foliage Inspector (brush), Terrain Inspector > Scatter props, Spline tools
- **Docs:** `docs/level_tools.md`, `docs/modeling.md`, `docs/splines.md`
- **Tests:** `foliage`, `save_coverage`, `splines`, `mesh_variant_hash`, `terrain_ops`
- **Status:** Works - the foliage layer, brush, scatter and checks are covered by the `foliage` suite and were driven with the mouse in the real editor; no grass or detail layer, no wind, no hardware instancing.
- **Exists today:** Painting, erasing, rescaling and scattering plants as data, drawn merged by chunk. The copies have no collider and do not sway.
- **Work packages:**
  1. L - Hardware instancing of foliage with distance fade (needs batching, 27.3); today a layer is merged geometry per chunk. Depends on: 27.3 package 4.
  2. M - Grass and detail layer (billboards or cross-quads) with wind (`Environment/Wind`). Depends on: package 1.
  3. S - Colliders for painted trees (a trunk capsule per copy, or on demand). Depends on: nothing.
  4. S - Several prefabs in one brush with weights. Depends on: nothing.
- **Open:** a layer holds one kind of plant (use several layers for a forest mix); the budget is triangles, not distance-faded; a prefab is flattened into one shape, so its parts share the first part's texture.
- **Related:** 2.6, 29.1, 29.3, 27.3, 2.4
- **Words:** vegetation, foliage, scatter, trees, grass, details, paint trees, instancer, props on terrain, foliage brush, erase trees, density, slope limit
- **Seen as:** I want to paint forests; scatter makes the editor slow; no grass
- **Ask me like:** "paint trees on the terrain"; "scatter grass with wind"

### 29.4 Decals and cables
- **What:** Small level-dressing pieces: a picture projected onto walls and floors, and a rope or wire that hangs between two points.
- **Parts:**
  - 29.4.1 Decal projector - `Components/Decal` - projects a picture along the object's forward axis onto the surfaces inside its box, as a thin mesh cut from the triangles it touches (follows bumps and corners, draws in every renderer); built on Project, not every frame
  - 29.4.2 Cable - `Components/Cable` - a parabola with sag between this object (or a named one) and another, laid on a Line Renderer; follows its end objects in the editor and in Play; visual only
- **Code:** `Components/Decal`, `Components/Cable`, `Render/SurfaceRaycast`, `editor/LevelTools.*`
- **API:** `Decal::Project`, `Cable::Curve`, `Cable::Rebuild`, `Cable::Length`
- **Editor:** GameObject > Decal Projector, GameObject > Cable, Add Component > Rendering, Inspector with a Project button
- **Docs:** `docs/level_tools.md`
- **Tests:** `foliage`, `save_coverage`
- **Status:** Works - both are covered by the `foliage` suite (geometry, save and load); the cable and decal were shown in the real editor.
- **Exists today:** Decals do not follow surfaces that move after projection (press Project again); cables have no physics (Joint3D is the physical one).
- **Open:** soft edge fade by angle, decals on skinned meshes, a rope that swings.
- **Related:** 29.2, 2.6, 6.1
- **Words:** decal, projector, stain, crack, bullet hole, sign, cable, rope, wire, sag, power line, hanging
- **Seen as:** need a dirt patch on a wall; wires between poles are straight lines
- **Ask me like:** "add a decal projector"; "hang a cable between two poles"

### 29.3 Procedural generation
- **What:** Generate terrain, props and layouts from rules and random seeds, repeatably.
- **Parts:**
  - 29.3.1 Terrain generators - `Components/Terrain` - seeded fractal types with domain warping, erosion, terrace
  - 29.3.2 Noise library - `Math/Noise` - `NoiseField` (Perlin-style, fractal, Worley) merged in #571
  - 29.3.3 Random and seeds - `Core/Rng`, `Core/Random` - seedable generators (merged #557, #571)
  - 29.3.4 Spline-based generation - `Components/SplineMesh` - roads, fences and pipes along curves
- **Code:** `Components/Terrain`, `Math/Noise`, `Core/Rng`, `Core/Random`, `Components/SplineMesh`
- **API:** `Terrain::Generate`, `NoiseField`, `Rng`, `Random`
- **Editor:** Terrain Inspector generate controls
- **Docs:** `docs/math_geometry.md`, `docs/modeling.md`
- **Tests:** `terrain`, `math_geometry`, `random`
- **Status:** Partial - terrain generation and the tools to build more exist; no generic rule-based generator.
- **Exists today:** Terrain creates its own noise (see `Terrain.cpp`); the shared `NoiseField` is not used by it yet (not checked in detail).
- **Work packages:**
  1. S - Terrain uses `NoiseField` so there is one noise implementation. Depends on: nothing.
  2. M - Procedural recipe asset (seed, steps) that regenerates terrain and scatter identically. Depends on: 29.2 package 1.
  3. L - Dungeon or room layout generator writing prefab placements (grid and BSP). Depends on: 21.1.
  4. M - Road and river generators using splines over terrain. Depends on: nothing.
  5. S - "Randomize seed" button with a seed shown for reproducing a result. Depends on: package 2.
- **Open:** possible duplicate: `Components/Terrain.cpp` has its own noise code next to `Math/Noise`.
- **Related:** 2.6, 29.1, 29.2, 1.1
- **Words:** procedural generation, noise, Perlin, seed, random terrain, erosion, dungeon generator, recipe
- **Seen as:** different result every time I press generate; cannot repeat a good world
- **Ask me like:** "make terrain use the shared noise library"; "generate a dungeon layout from prefabs"

## 30. Character creator (Medium)

### 30.1 Customization
- **What:** Change how a character looks (body size, colours, hair, clothes, accessories) in the editor or at run time.
- **Parts:**
  - 30.1.1 Blocky character options - `Components/Character` - height, colours for skin, shirt, pants, shoes, hair, eyes and more; 8 hair styles, 4 beard styles, shirt and leg styles, jacket, belt, gloves, hats, glasses, mask, scarf, shoulder pads
  - 30.1.2 Character inspector - `editor/main.cpp` - the Character section edits every option and rebuilds the mesh
  - 30.1.3 Part rig - `Components/Character` - `BuildParts` makes the character a hierarchy of editable part objects (tested in `character_parts`)
  - 30.1.4 Characters in the GameObject menu - `editor/main.cpp` - GameObject > Character
- **Code:** `Components/Character`, `Components/CharacterAnimBackend`, `editor/main.cpp`
- **API:** `Character` fields (`height`, `skin`, `shirt`, `hairStyle`, `beardStyle`, `hatStyle`, `glassesStyle`...), `Character::ToText`, `FromText`
- **Editor:** Character Inspector section, GameObject > Character
- **Docs:** `docs/animation.md`
- **Tests:** `character_parts`, `char_editor_parts`, `character_feel`
- **Status:** Partial - the blocky character is fully customizable; imported characters have no customization UI.
- **Exists today:** All of the above on main for the blocky character. No randomize button, no saved presets, no in-game character creation screen (not found).
- **Work packages:**
  1. S - Randomize and Reset buttons plus saved presets as small text files. Depends on: nothing.
  2. M - In-game character creator template (sliders and colour pickers bound to a `Character`, using the UI kit). Depends on: package 1, 12.7.
  3. M - Save the chosen look into the save game and the network spawn (so others see it). Depends on: 35.3.
  4. M - Customization for imported humanoids: material colour swaps and part toggles by node name. Depends on: 30.3.
- **Open:** the part rig and the single-mesh rig are two build paths for one component (see 4.9).
- **Related:** 4.9, 3.7, 5.4, 30.2, 30.3
- **Words:** character creator, customization, Character component, hair style, skin colour, clothes, avatar, blocky character, appearance
- **Seen as:** I want players to design their character; changing a colour in the Inspector does nothing in a built game
- **Ask me like:** "add a character creator screen to the game"; "save the player's look in the save file"

### 30.2 Rig generation
- **What:** Turn any character model into something that can walk and be animated without rigging it by hand.
- **Parts:**
  - 30.2.1 Auto-bind to the built-in skeleton - `Components/Character` - `BindCustomMesh` attaches each vertex to its nearest bone of the 15-bone humanoid so every built-in animation drives the model
  - 30.2.2 Humanoid bone matching - `Animation/HumanoidBones` - recognises bone names of common imported rigs (Mixamo and others) and quadruped limb names
  - 30.2.3 Make Playable Character - `Render/ModelImport` - one command makes a third-person player from a model file with foot IK and look-at IK
  - 30.2.4 Retarget - `Components/HumanoidRetarget`, `Animation/HumanoidLibrary` - built-in animation retargeted onto an imported skeleton (area 5)
- **Code:** `Components/Character`, `Animation/HumanoidBones`, `Render/ModelImport`, `Components/HumanoidRetarget`
- **API:** `Character::BindCustomMesh`, `MakePlayableCharacter`, humanoid bone map
- **Editor:** Project panel > Make Playable Character, "Rig Model (Humanoid)"
- **Docs:** `docs/animation.md`
- **Tests:** `make_playable`, `humanoid_bones`, `character_model_setup`, `humanoid_retarget`
- **Status:** Partial - auto-binding works with rigid nearest-bone weights; no smooth skin weights and no bone placement UI.
- **Exists today:** Rigid binding for unrigged meshes and name-based mapping for rigged ones. Animation and IK pieces are in Grok's area 5; do not edit them from here.
- **Work packages:**
  1. M - Smooth skin weights (blend between the two nearest bones) for auto-rigged meshes. Depends on: nothing.
  2. M - Joint placement UI: drag the 15 joints over the model before binding. Depends on: package 1.
  3. S - Report which bones matched and which were guessed, in the Console. Depends on: nothing.
  4. L - Weight painting window. Depends on: package 1, 3.1.
- **Open:** rigid weights tear at joints; animation and IK code belong to Grok's sessions.
- **Related:** 3.7, 5.4, 5.5, 30.1
- **Words:** auto rig, rig generation, BindCustomMesh, Rig Model, Make Playable, skin weights, humanoid bones, skeleton
- **Seen as:** imported model walks but elbows or knees look torn; model faces the wrong way after rigging
- **Ask me like:** "add smooth skin weights to auto-rigging"

### 30.3 Clothing
- **What:** Dress characters: change clothes and attach items, including on imported characters.
- **Parts:**
  - 30.3.1 Blocky clothing options - `Components/Character` - shirt, legs, jacket, belt, gloves, hats, glasses, mask, scarf, pads
  - 30.3.2 Sockets and equipment - `Components/Sockets`, `Components/Equipment` - items shown at named sockets (head, torso, hands) from the item's prefab; runtime only, not saved
  - 30.3.3 Clothing for imported characters - `-` - not built (no skinned clothing swap)
- **Code:** `Components/Character`, `Components/Sockets`, `Components/Equipment`
- **API:** `FindSocket`, `Equipment::EquipFrom`, `Unequip`
- **Editor:** Character Inspector, Inventory/Equipment components
- **Docs:** `docs/npcs_and_crafting.md`
- **Tests:** `inventory_equipment`, `character_parts`
- **Status:** Partial - blocky clothing and rigid attachments exist; no clothing for imported or custom-rigged bodies.
- **Exists today:** Equipment attaches the item's prefab to a socket; armor and weight numbers apply. A shirt that deforms with the body is not supported.
- **Work packages:**
  1. M - Clothing slots on a skinned model: swap or hide sub-meshes by name (hide body parts under clothes). Depends on: nothing.
  2. M - Clothing items share the character's skeleton (same bone names) and follow its pose. Depends on: package 1.
  3. S - Equipment can drive clothing slots. Depends on: package 2.
  4. M - Save and network the worn set. Depends on: 35.3.
- **Open:** no layered clothing, cloth physics (see 52).
- **Related:** 4.9, 11.1, 30.1, 30.4
- **Words:** clothing, outfit, equipment, sockets, attach, wearable, armor visual, skinned clothing
- **Seen as:** equipped armor does not show on my imported character; a hat floats
- **Ask me like:** "let equipment swap clothing meshes on an imported character"

### 30.4 Morph targets
- **What:** Reshape a face or body with sliders (smile, nose size, muscle) by blending stored shapes.
- **Parts:**
  - 30.4.1 Blend shapes - `Components/BlendShapes` - named per-vertex offset shapes mixed by weight 0..1 before skinning; imported from glTF and FBX
  - 30.4.2 Sliders - `editor/main.cpp` - Blend Shapes Inspector section: one slider per shape with a filter and Reset all
  - 30.4.3 Animating weights - `Components/ModelAnimator` - clips can animate weights (`weights.<n>` tracks)
- **Code:** `Components/BlendShapes`, `editor/main.cpp`, `Components/ModelAnimator`
- **API:** `BlendShapes::shapes`, `weight`, `Apply`
- **Editor:** Blend Shapes Inspector section
- **Docs:** `docs/animation.md`
- **Tests:** `blend_shapes`
- **Status:** Partial - imported morph targets work; authoring shapes in the editor is not possible.
- **Exists today:** Reading, mixing and animating imported shapes. No way to create a shape, mirror one or save a slider preset.
- **Work packages:**
  1. S - Save and load slider presets (named sets of weights). Depends on: nothing.
  2. M - Character creator UI uses blend shape sliders by group (face, body). Depends on: 30.1 package 2.
  3. L - Create shapes from edit mode (store the difference between two mesh states). Depends on: 3.1.
  4. M - Lip-sync driver: visemes from audio or text to weights. Depends on: package 1.
- **Open:** shapes are stored sparse and applied to the bind pose; large faces with many shapes cost CPU each frame (not measured).
- **Related:** 3.6, 5.2, 30.1
- **Words:** morph targets, blend shapes, shape keys, facial expressions, visemes, lip sync, BlendShapes, sliders
- **Seen as:** a face model has dozens of sliders and I want presets; no smile when talking
- **Ask me like:** "add lip sync from dialogue audio using blend shapes"

## 31. Localization tools (Medium)

### 31.1 Translation tables
- **What:** The tool translators and developers use to manage every piece of text in every language: keys, translations, notes, progress and spreadsheet exchange.
- **Parts:**
  - 31.1.1 String table file - `Core/Localization` - `strings.okayloc` with languages, keys, notes and per-language texts; plural forms and number, currency, percent and date formatting (merged #571)
  - 31.1.2 Localization window - `editor/LocalizationWindow.*` - grid of keys by languages with in-place editing, completion bars per language, search, filters (missing in a language, unused keys), Find in Scene, Add Dialogue and Quest Text, own undo
  - 31.1.3 CSV exchange - `editor/LocalizationWindow.*` - Export CSV and Import CSV (header row names columns, comma/semicolon/tab, BOM)
  - 31.1.4 Table tool layer (#574) - `Core/LocTable`, `Core/LocCheck`, `Core/LocScan` - cell states (translated, missing, same-as-source, placeholder mismatch, extra), CSV and JSON, placeholder and plural checker, scan for used, unused and undefined keys
  - 31.1.5 Scene text binding - `Scene/SceneLocalization` - localized text on scene objects follows the current language
- **Code:** `Core/Localization`, `Scene/SceneLocalization`, `editor/LocalizationWindow.*`, `Core/LocTable`, `Core/LocCheck`, `Core/LocScan`
- **API:** `Localization::Get`, `Localize`, `PluralText`, `FormatNumber`, `FormatDate`; script `loc`, `set_language`; `LocTable`, `LocGrid`, `LocCheck`, `LocScanProject`
- **Editor:** View > Editors & Tools > Localization; Inspector "Localized" checkbox on text fields
- **Docs:** `docs/localization.md`, `docs/localization_prefs_time.md`; `docs/localization_tools.md`
- **Tests:** `localization`, `loc_prefs_time`; `loc_tools`
- **Status:** Partial - the window with grid, CSV, missing and unused filters works; the stricter checker, JSON and key scanner (#574, test `loc_tools`) are on main but not wired to the window.
- **Exists today:** The window and file format (docs say it all in `docs/localization.md`). The old map listed "translation table editor window, missing-key report, import and export (CSV)" as missing: those are done. `docs/localization_tools.md` says the window still draws its own grid and "wiring it to LocTable ... is the next step".
- **Work packages:**
  1. M - Window uses `LocTable` for cell states (same-as-source, placeholder mismatch, extra) and a state filter. Depends on: #574 (on main).
  2. S - Checker report panel (`LocCheck`): broken placeholders and plural forms, click to jump to the key. Depends on: package 1.
  3. S - Import preview: show what an import will change before applying it. Depends on: package 1.
  4. S - JSON import and export (one file per language) for web translators. Depends on: package 1.
  5. M - Key scan uses `LocScanProject` for "used in code, scenes, dialogue" and flags undefined keys. Depends on: package 1.
  6. M - Translation memory and machine-translation suggestion hook (optional). Depends on: package 1.
- **Open:** the window and the new `LocTable` duplicate the grid logic until package 1 lands; right-to-left scripts are drawn left to right (see 31.3).
- **Related:** 1.8, 12.2, 31.2, 31.3, 10.2
- **Words:** localization, translation, string table, strings.okayloc, CSV, missing keys, unused keys, plural, translators, languages, i18n
- **Seen as:** missing translation shows the key; placeholder `{0}` lost in a translation; cannot hand translators a spreadsheet
- **Ask me like:** "show placeholder mismatches in the Localization window"; "add JSON export per language"

### 31.2 Font fallback
- **What:** Draw text in any language even when the chosen font lacks the characters, by switching to a font that has them.
- **Parts:**
  - 31.2.1 Per-language font - `Core/Localization` - `font "<path>"` per language in the table, copied by Build Game
  - 31.2.2 Runtime fallback chain - `Graphics/TtfFont` - language font, then the built-in UI font, then installed system fonts (CJK and others); one font per text; the switch is logged once
  - 31.2.3 Editor font merge - `editor/LocalizationWindow.*` - the editor UI font gets CJK glyphs so the table shows every language
  - 31.2.4 Font coverage report (#574) - `Core/LocCheck` - `LocFontCoverage`: characters no font draws, characters drawn by a fallback, texts no single font draws
- **Code:** `Graphics/TtfFont`, `Graphics/Font`, `Core/Localization`, `editor/LocalizationWindow.*`, `Core/LocCheck`
- **API:** `TtfFont::Has`, `FontForText`, `SystemFallbackFonts`; `LocFontCoverage`
- **Editor:** Localization window language bar (click to set a language font)
- **Docs:** `docs/localization.md` (Fonts)
- **Tests:** `ttf`, `text`, `localization`; `loc_tools`
- **Status:** Partial - fallback works per `docs/localization.md`; right-to-left shaping does not exist.
- **Exists today:** The chain above. A text uses one font as a whole. The dialogue box, quest tracker and toasts use an atlas with ASCII and Latin-1 only and fall back for others.
- **Work packages:**
  1. S - Show the font coverage report in the Localization window per language. Depends on: #574 (on main).
  2. M - Per-character fallback (mix fonts inside one text) for names inside other scripts. Depends on: nothing.
  3. L - Right-to-left layout and Arabic shaping (bidi). Depends on: nothing.
  4. S - Warn at Build time when a language has characters no shipped font draws. Depends on: package 1.
- **Open:** system fonts differ between machines, so a CJK game should ship its own font; RTL is drawn left to right.
- **Related:** 1.8, 2.9, 12.2, 31.1
- **Words:** font fallback, CJK, Japanese, Chinese, Korean, missing glyphs, tofu boxes, TtfFont, system fonts, right to left, Arabic
- **Seen as:** squares instead of letters in a language; Arabic text reads backwards
- **Ask me like:** "check which characters my fonts cannot draw"; "add right-to-left text layout"

### 31.3 Language previews
- **What:** See the game in another language right now, including how long or mirrored text will fit, without changing settings.
- **Parts:**
  - 31.3.1 Preview language menu - `editor/LocalizationWindow.*` - toolbar menu switches the language of the Scene and Game views and Play
  - 31.3.2 Pseudo-localize - `editor/LocalizationWindow.*` - accented, about 30 percent longer text in brackets to spot clipping and text that skips the table
  - 31.3.3 Preview model (#574) - `Core/LocPreview` - `LocPreview`, `LocPseudoText`, mirrored right-to-left text, `Bake` into a one-language table
- **Code:** `editor/LocalizationWindow.*`, `Core/LocPreview`, `Scene/SceneLocalization`
- **API:** `OKAY_UI_LANG` dev hook; `LocPreview`, `LocPseudoText`, `LocMirrorText`
- **Editor:** toolbar language menu, Localization window "Pseudo-localize"
- **Docs:** `docs/localization.md`; `docs/localization_tools.md`
- **Tests:** `localization`; `loc_tools`
- **Status:** Partial - language switch and pseudo-localization work in the editor; the `LocPreview` model with mirrored text is on main (#574) but not wired to a window.
- **Exists today:** Both preview features in the editor. The new `LocPreview` can preview without switching the running language or logging missing keys.
- **Work packages:**
  1. S - Pseudo-localize options (expansion percent, markers) in the window, using `LocPseudoOptions`. Depends on: #574 (on main).
  2. M - Mirrored preview for right-to-left languages in the Game view. Depends on: package 1, 31.2 package 3.
  3. M - "Show all strings" screenshot sheet: render every UI screen in each language to image files for review. Depends on: 33.3.
  4. S - Overflow warning when a localized text no longer fits its UI box. Depends on: package 1.
- **Open:** layout does not mirror for right-to-left languages yet.
- **Related:** 1.8, 12.1, 31.1, 31.2, 33.3
- **Words:** language preview, pseudo-localization, pseudo loc, switch language, ploc, overflow, text expansion
- **Seen as:** German text overflows buttons; I cannot see what my game looks like in Japanese
- **Ask me like:** "warn me when translated text does not fit its box"

## 32. Accessibility (High)

### 32.1 Controller navigation
- **What:** Play and use every menu with a gamepad or keyboard only, with a visible focus that moves the way the screen looks.
- **Parts:**
  - 32.1.1 UI navigation - `Components/UINavigation` - `NavigateUI` moves focus to the nearest widget in the pressed direction (arrows, WASD, D-pad), wraps at the ends, scrolls a focused widget into view, handles button, toggle, slider, dropdown, tabs and keybind fields, and keeps focus inside an open modal
  - 32.1.2 Gamepad input - `Input/SdlGamepad`, `Input/InputActions`, `Input/Input` - sticks, buttons, curves and dead zones; extended keys and contexts (merged #571)
  - 32.1.3 Focus graph (#574) - `UI/FocusGraph` - directional focus graph with tab order, wrap, traps; `BuildFocusGraph`, `IsNavFocusable`
  - 32.1.4 Cursor and touch - `Input/Cursor` - menus also work with the mouse and touch (area 8)
- **Code:** `Components/UINavigation`, `Input/SdlGamepad`, `Input/InputActions`, `UI/FocusGraph`
- **API:** `NavigateUI(scene)`, widget `IsFocused()`; `FocusGraph`, `BuildFocusGraph`
- **Editor:** none (the editor itself is not controller-navigable)
- **Docs:** `docs/input.md`, `docs/game_ui.md`; `docs/accessibility.md`
- **Tests:** `ui_nav_touch`, `input_actions`, `input_keys_gamepad`; `focus_remap`
- **Status:** Partial - directional navigation works (per code and `ui_nav_touch`); `FocusGraph` and `BuildFocusGraph` (tab order, links, traps, #574, test `focus_remap`) are on main but `NavigateUI` does not use the graph; keys are fixed.
- **Exists today:** `NavigateUI` as described, called by the player every frame. The navigation keys are fixed in code (`docs/accessibility.md`: "WASD, Enter, Space, the D-pad and A").
- **Work packages:**
  1. M - `NavigateUI` builds a `FocusGraph` so a designer can set neighbours, tab order and focus traps. Depends on: #574 (on main).
  2. S - Navigation keys and buttons come from `InputActions` (remappable, 32.4). Depends on: package 1.
  3. S - Focus ring style and focus sound in the UI theme. Depends on: 12.7.
  4. S - Scene check that every interactive widget can be reached by navigation (reports in 33.2). Depends on: package 1.
  5. M - Same navigation for world-space and HTML UI. Depends on: package 1.
- **Open:** the focus visuals depend on each widget's own `IsFocused` drawing; the graph is built from screen rectangles each time it is asked.
- **Related:** 8.2, 8.3, 12.2, 12.4, 12.7, 32.4
- **Words:** controller navigation, gamepad menus, focus, D-pad, tab order, NavigateUI, FocusGraph, keyboard navigation, menu focus
- **Seen as:** cannot reach a button with the gamepad; focus jumps to a surprising widget; focus lost after a popup closes
- **Ask me like:** "let me set the focus order of a menu"; "make menu navigation keys remappable"

### 32.2 Subtitles
- **What:** Show spoken lines and important sounds as readable text, with options the player controls (size, background, speaker names).
- **Parts:**
  - 32.2.1 Cutscene subtitle action - `Components/Sequence`, `Components/SequencePlayer` - a `subtitle` action with speaker and line; drawn as a card in the launcher look
  - 32.2.2 Dialogue text - `Components/DialogueRunner`, `Components/DialogueGraph` - the dialogue box shows speaker and line (area 10)
  - 32.2.3 Caption manager (#574) - `Core/Subtitles` - `CaptionManager` queue with priorities, timing from audio length, speaker colours from a colour-safe palette, sound and music captions, localized lines, SRT tracks; `SubtitleSettings` saved under `a11y.subtitles.*`
- **Code:** `Components/Sequence`, `Components/SequencePlayer`, `Components/DialogueRunner`, `Core/Subtitles`
- **API:** `CaptionManager::Say`, `Sound`, `Play(track)`, `SubtitleTrack::ParseSrt`, `SubtitleSettings`
- **Editor:** Timeline window (subtitle actions)
- **Docs:** `docs/cutscenes.md`; `docs/accessibility.md`
- **Tests:** `accessibility`
- **Status:** Partial - cutscenes show subtitles with their own fixed look; the shared `CaptionManager` (#574, test `accessibility`) is on main but nothing connects the Sequencer, dialogue or audio to it, and there is no widget.
- **Exists today:** The cutscene subtitle card. `docs/accessibility.md` says the Sequencer's subtitle still shows its own text rather than going through `CaptionManager`, and there is no ready-made subtitle widget.
- **Work packages:**
  1. M - Subtitle widget component drawing `CaptionManager::Lines()` with the player's settings. Depends on: #574 (on main), 12.2.
  2. S - Sequence subtitle action and dialogue lines go through `CaptionManager`. Depends on: package 1.
  3. S - Audio Source option "caption text" so a sound can show `[door slams]`. Depends on: package 1, 7.3.
  4. S - Settings screen section: size, background, speaker names, sound captions. Depends on: package 1, 12.8.
  5. M - SRT import for recorded voice-over in the Timeline. Depends on: package 1.
- **Open:** speaker colours need a colour-vision-safe palette (32.3); no sound-caption marking exists yet on audio sources.
- **Related:** 10.1, 10.2, 7.3, 31.1, 32.3
- **Words:** subtitles, captions, closed captions, CaptionManager, speaker names, SRT, sound captions, dialogue text, subtitle size
- **Seen as:** subtitles too small or hard to read; no captions for sounds; subtitles ignore the language setting
- **Ask me like:** "add a subtitle widget that follows the accessibility settings"; "caption sounds for deaf players"

### 32.3 Colorblind options
- **What:** Let players with colour vision differences read the game: filters, safe palettes and contrast, without changing the art by hand.
- **Parts:**
  - 32.3.1 Colour vision maths (#574) - `Graphics/ColorVision` - simulate and correct for protanopia, deuteranopia, tritanopia, achromatopsia on colours and images; a colour-vision-safe palette; contrast checks
  - 32.3.2 Accessibility settings (#574) - `Core/AccessibilitySettings` - `colorblind` mode and strength, high contrast, text scale, reduce motion and flashing, saved in Prefs, with change listeners
  - 32.3.3 Full-screen filter - `-` - not built (a function on colours and images only; renderers do not apply it)
- **Code:** `Graphics/ColorVision`, `Core/AccessibilitySettings`
- **API:** `SimulateColorblind`, `DaltonizeColor`, `Accessibility::FilterColor`, `TextColor`, `SetColorblind`
- **Editor:** none
- **Docs:** `docs/accessibility.md`
- **Tests:** `accessibility`
- **Status:** Partial - the colour vision maths and the accessibility settings are on main (#574, test `accessibility`); no renderer applies them ("a full-screen filter in the renderers is future work", per docs) and the editor has no preview.
- **Exists today:** `Graphics/ColorVision` and `Core/AccessibilitySettings` as data and functions on colours and images; nothing in the renderers, the editor or the player reads the settings.
- **Work packages:**
  1. M - Post-process filter in the renderers using the settings (all four renderers or software plus GL/D3D11 first). Depends on: #574 (on main).
  2. S - Game view "Simulate colour blindness" preview menu in the editor to check a scene. Depends on: package 1.
  3. S - UI theme and health bars use `Accessibility::FilterColor` or shapes plus colour. Depends on: 12.7.
  4. S - Settings screen section with a preview strip. Depends on: 12.8.
  5. M - Palette checker: report colour pairs that are hard to tell apart for a given mode. Depends on: package 2.
- **Open:** the GPU renderers do not read the settings; the speaker-colour palette in `CaptionManager` is the only consumer in the engine.
- **Related:** 2.5, 12.7, 32.2
- **Words:** colorblind, colour blind, protanopia, deuteranopia, tritanopia, daltonize, high contrast, safe palette, accessibility filter
- **Seen as:** red and green indicators look the same; team colours indistinguishable
- **Ask me like:** "add a colorblind filter to the renderers"; "preview my UI as a deuteranope in the editor"

### 32.4 Remapping
- **What:** Let players rebind every control, with conflict handling, on keyboard and gamepad.
- **Parts:**
  - 32.4.1 Input actions and persistence - `Input/InputActions` - named actions with keys, mouse buttons, gamepad buttons and sticks; bindings saved to the player's prefs; defaults restorable
  - 32.4.2 Keybind field widget - `Components/UIKeybindField` - press-to-bind field for an action or a plain key; Escape cancels, Backspace restores the default
  - 32.4.3 Gamepad and key tables - `Input/SdlGamepad`, `Input/SdlKeys` - extended keys, contexts and curves (merged #571)
  - 32.4.4 Remap model (#574) - `Input/RemapModel` - controls list, conflict detection (including four-key directions and the D-pad), `RemapBind` with cancel, swap, unbind or allow, reserved actions
- **Code:** `Input/InputActions`, `Components/UIKeybindField`, `Input/SdlGamepad`, `Input/SdlKeys`, `Input/RemapModel`
- **API:** `InputActions::Get`, `UIKeybindField` (`action`, `gamepad`, `part`), `RemapConflicts`, `RemapBind`, `RemapAllConflicts`
- **Editor:** Inspector for the Keybind Field; Project Settings has no input section yet
- **Docs:** `docs/input.md`; `docs/accessibility.md`
- **Tests:** `input_actions`, `input_keys_gamepad`; `focus_remap`
- **Status:** Partial - rebinding works through the keybind field; the conflict-aware `RemapModel` for a Controls screen is on main (#574, test `focus_remap`) but no screen or widget uses it.
- **Exists today:** Rebinding one action at a time with no conflict warning; Backspace restores one action's defaults.
- **Work packages:**
  1. M - Controls screen template built on `RemapModel`: list by category, conflict badges, swap or unbind prompt, reset all. Depends on: #574 (on main), 12.8.
  2. S - UI navigation and pause keys are actions, so they can be remapped too. Depends on: 32.1 package 2.
  3. S - Hold-or-toggle option per action (`HoldToggle`). Depends on: #574 (on main).
  4. S - Per-device bindings (keyboard, gamepad, touch) shown together. Depends on: package 1.
  5. S - Import and export of a binding profile for support and sharing. Depends on: package 1.
- **Open:** the UI navigation keys are fixed in code; mouse-button and wheel rebinding coverage is not checked.
- **Related:** 8.3, 12.3, 12.8, 32.1
- **Words:** remap, rebind controls, key bindings, controls screen, input conflicts, hold to toggle, UIKeybindField, InputActions
- **Seen as:** pressing a key already used by another action does nothing or steals it; cannot rebind the menu keys
- **Ask me like:** "build a controls screen with conflict warnings"; "add hold-to-toggle for sprint"

## 33. Automated testing (Critical)

### 33.1 Play-mode tests
- **What:** Tests that load a scene, run it for a number of frames without a window and check what happened (a player moved, a door opened).
- **Parts:**
  - 33.1.1 Play-test runner (#572) - `Testing/PlayTest` - fixed-step headless frames, fake keyboard, mouse and touch input, find objects by path, run until a condition, capture log lines (an undeclared error fails the test), event recorders
  - 33.1.2 Test registry and CLI runner (#572) - `Testing/TestRegistry`, `tests/playtest/playtest_main.cpp` - `OKAY_PLAYTEST_IN(suite, name)` tests, name filters and excludes, JUnit XML output, self-tests in `tests/playtest/selftest_playtests.cpp`
  - 33.1.3 Existing C++ unit tests - `tests/CMakeLists.txt` - 387 `add_test` lines with a small framework, many load scenes and step them in code
  - 33.1.4 Editor self-test and export check - `editor/main.cpp`, `tests/export/verify.sh` - `OkayEngine --selftest` runs the editor logic headless (Play, Stop, delete, undo); CI exports a game and checks its visual scripts
- **Code:** `Testing/PlayTest`, `Testing/TestRegistry`, `Testing/ObjectPath`, `tests/CMakeLists.txt`, `tests/playtest/playtest_main.cpp`, `editor/main.cpp`
- **API:** `PlayTest`, `OKAY_PLAYTEST_IN`, `OKAY_EXPECT`, `EventRecorder`, `EventProbe`; `okay-playtest` command
- **Editor:** none (no test runner window)
- **Docs:** `docs/automated_testing.md`; `docs/qa_workflow.md`
- **Tests:** `test_framework`-based suites in `tests/`; `playtest`, `playtest_runner`, `testing_registry`
- **Status:** Partial - the headless play-test harness and runner are on main (#572, per docs and the ctest list, code not read in depth; `playtest`, `testing_registry` and the `okay-playtest` run `playtest_runner` are ctests), but no game, sample project or editor window uses them yet beyond the framework's own self-tests.
- **Exists today:** `PlayTest`, the registry and the `okay-playtest` runner (read in `tests/CMakeLists.txt` and `docs/automated_testing.md`), plus the 387 `add_test` lines and CI. Many older tests still step a `Scene` by hand.
- **Work packages:**
  1. S - Check CI runs `playtest_runner` (it is a ctest; no workflow mentions `okay-playtest` by name) and publish the JUnit file. Depends on: #572 (on main).
  2. M - Project play-tests: a `Tests/` folder in a project holding play-test scenes and scripts, run by the editor and CI. Depends on: package 1.
  3. M - Editor "Test Runner" window: list, run one or all, jump to failing object. Depends on: package 1.
  4. S - UI and audio assertions (widget focus, clip started). Depends on: package 1.
  5. M - Networked play-tests (two instances in one process over the loopback). Depends on: package 1, area 16.
- **Open:** a script that reads the wall clock or uses threads makes a play-test non-deterministic (stated in `docs/automated_testing.md`); one `PlayTest` may be alive at a time (global state).
- **Related:** 19.2, 18.3, 33.2, 33.3, 34.1
- **Words:** play mode test, playtest, headless test, integration test, scene test, PlayTest, okay-playtest, test runner, assertions
- **Seen as:** a bug fixed twice because there was no test; cannot test a level without opening the editor
- **Ask me like:** "write a play-test that checks the door opens"; "add a Test Runner window"

### 33.2 Scene validation
- **What:** Find broken scenes before players do: missing files, missing components, bad numbers, scripts that do not compile.
- **Parts:**
  - 33.2.1 Visual-script validation at Play - `editor/EditorState.cpp`, `VisualScript/VsTypes` - `vs::ValidateSceneGraphs` runs when Play starts and prints errors and warnings to the Console
  - 33.2.2 Asset database reports - `Core/AssetDatabase` - `FindMissing`, `FindStaleReferences` (merged #568)
  - 33.2.3 Scene validator (#572) - `Testing/SceneValidator` - checks with stable codes: missing assets, prefab, no camera, duplicate names, scripts missing or not compiling, NaN and zero scale, rigidbody without collider; text or JSON report
  - 33.2.4 Scene validation (#575) - `Scene/SceneValidation` - `Scene::Validate`, `SceneSerializer::ValidateFile`: unknown components and fields, missing assets, broken parent links, duplicate guids
- **Code:** `VisualScript/VsTypes`, `editor/EditorState.cpp`, `Core/AssetDatabase`, `Testing/SceneValidator`, `Scene/SceneValidation`
- **API:** `vs::ValidateSceneGraphs`; `SceneValidator::Validate`, `ValidateFile`, `Scene::Validate`
- **Editor:** Problems panel (15.11.8) updates in edit mode; Build blocks on errors and lists warnings; Console output at Play still prints graph errors
- **Docs:** `docs/visual_scripting.md`; `docs/automated_testing.md`, `docs/scene_hierarchy_search.md`
- **Tests:** `vs_types`; `scene_validator`, `scene_hierarchy_search`, `diagnostics`
- **Status:** Works - `CollectProjectDiagnostics` feeds `SceneValidator`, `Scene::Validate` and `AssetDatabase::FindMissing` into one list, and the Problems panel and the Build gate read it.
- **Exists today:** Play-time validation of visual script graphs, the two scene validators, and the editor list that merges those plus the gameplay checks (player body, animator, navmesh, node requirements) and missing asset-index references (`asset.db.missing`). The first scan of an asset root writes a `.okaymeta` beside a file that does not have one, which is how the index sees it.
- **Work packages:**
  1. S - Decide one validator: keep `Testing/SceneValidator` (rich checks) and have `Scene::Validate` (load-level checks) feed it. Depends on: #572, #575 (both on main). Done in `CollectProjectDiagnostics` (duplicate missing-asset rows are dropped).
  2. M - Editor Validate button and window: list issues, click selects the object, severity filter. Depends on: package 1. Done (Problems, live rather than a button).
  3. S - Run validation on Play and on Build (blocking errors, warnings listed). Depends on: package 2. Build gate done; the list itself refreshes in edit mode, and Play still prints graph errors.
  4. S - CI step that validates every scene in the sample projects. Depends on: package 1.
  5. M - Project-specific rules (naming, required tags) registered by the game. Depends on: package 1.
- **Open:** no CI step walks the sample scenes.
- **Related:** 1.3, 1.5, 20.2, 23.4, 33.1
- **Words:** scene validation, validator, missing references, missing assets, missing component, validate scene, health check, pre-build check
- **Seen as:** pink or empty objects because a file is missing; game starts with a broken script; no warning before building
- **Ask me like:** "add a Validate Scene button"; "fail the build when a scene has missing assets"

### 33.3 Regression testing
- **What:** Catch changes that break something that worked: compare results with saved "golden" results, including pictures.
- **Parts:**
  - 33.3.1 Golden files and scene digest (#572) - `Testing/Regression` - compare text or binary outputs with committed golden files; a scene fingerprint with a readable diff; refresh with `OKAY_UPDATE_GOLDEN=1`
  - 33.3.2 Golden images (#572) - `Testing/Regression`, `Testing/GoldenImage` - software-rendered image compare with tolerance and a diff picture; committed goldens in `tests/golden/regression/`
  - 33.3.3 Existing golden tests - `tests/golden/`, `tests/CMakeLists.txt` - `ui_renderer_golden` compares UI output; `rig_sweep` sweeps rigs through poses
  - 33.3.4 CI jobs - `.github/workflows/ci.yml` - build and test (also builds the player and editor, runs the editor self-test and an export check), ASan and UBSan job, Windows cross-build, Windows export check, web build
- **Code:** `Testing/Regression`, `tests/golden`, `.github/workflows/ci.yml`, `.github/workflows/release.yml`
- **API:** `CompareTextToGolden`, `CheckSceneGolden`, `HashScene`, `DiffDigests`, `CompareImages`, `RenderSceneToImage`, `CheckImageGolden`
- **Editor:** none
- **Docs:** `docs/automated_testing.md`; `docs/qa_workflow.md`
- **Tests:** `ui_renderer_golden`, `rig_sweep`; `regression`
- **Status:** Partial - the golden file, scene digest and golden image helpers are on main (#572, test `regression`, goldens in `tests/golden/regression/`); only a cube and a mover world are covered, plus the UI golden test.
- **Exists today:** Five CI jobs on pushes (`build-and-test`, `sanitizers`, `windows-cross`, `windows-export`, `web-build`) plus release jobs. No picture comparison of 3D scenes.
- **Work packages:**
  1. M - Add golden tests of small 3D scenes with the software renderer using `CheckImageGolden`. Depends on: #572 (on main).
  2. M - Golden images of what the GPU renderers draw (tolerance, per-renderer goldens). Depends on: package 1.
  3. S - CI uploads diff pictures of failed goldens as artifacts. Depends on: package 1.
  4. M - Performance regression guard (see 58.3). Depends on: 58.1.
  5. S - Test-time scene digest for every sample project scene. Depends on: package 1.
- **Open:** compilers round shading slightly differently, so goldens need a tolerance; the software renderer draws mesh renderers only (no sprites, UI, particles, sky).
- **Related:** 18.3, 19.2, 33.1, 58.3
- **Words:** regression test, golden image, golden file, snapshot test, scene digest, CI, screenshot compare, sanitizers
- **Seen as:** a change breaks rendering and nobody notices; flaky pixel diffs between machines
- **Ask me like:** "add golden image tests for the 3D renderers"; "show the diff picture when a golden fails"

## 34. Live editing (High)

### 34.1 Properties during Play
- **What:** Change values while the game runs, see the effect at once, and choose to keep or throw away the change.
- **Parts:**
  - 34.1.1 Edit while playing - `editor/main.cpp` - the Inspector and Hierarchy work on the running scene; values (stats, script fields, variables) can be edited live
  - 34.1.2 Revert on Stop - `editor/EditorState.cpp` - `Play` stores the scene as text and `Stop` restores it, so Play edits are discarded
  - 34.1.3 Apply during Play for first-person hands - `Components/FirstPersonHands` - one special case that keeps an edit (`RestoreApplied`)
  - 34.1.4 Variables window - `editor/main.cpp` - live visual-script variables, editable
  - 34.1.5 Property registry (#578) - `Scene/Properties`, `Scene/PropertyRegistry`, `Scene/PropertyBuilder` - read, write and watch a component's fields by name (`Light.intensity`), with a change hook, snapshot, diff and copy; 334 properties on 26 built-in component types registered (325 stored in the scene file)
- **Code:** `editor/EditorState.*`, `editor/main.cpp`, `Components/FirstPersonHands`, `Scene/PropertyRegistry`, `Scene/Properties`, `Scene/PropertyBuilder`
- **API:** `EditorState::Play`, `Stop`, `isPlaying`; `PropertyRegistry::GetProperty`, `SetProperty`, `AddChangeHook`, snapshot and diff
- **Editor:** Inspector, Hierarchy, Variables window during Play
- **Docs:** `docs/editor.md`; `docs/properties.md`
- **Tests:** editor `--selftest` (Play and Stop restore); `property_registry`
- **Status:** Partial - editing during Play works but every change is lost on Stop, except the hands special case; the property registry (#578, test `property_registry`) is on main but the editor does not use it yet.
- **Exists today:** The comment in the self-test reads "Play / stop restores edit state (move during play is reverted)". Nothing marks a property as "changed in Play" or offers to copy it back. The property registry (#578) now has the pieces for it (a change hook with old and new values, snapshot, diff and revert by property path) but nothing in the editor calls them.
- **Work packages:**
  1. M - Record edits made in Play (object Guid, property path, old and new value) with the registry's `AddChangeHook`, and keep the list. Depends on: the property registry (on main, #578, 1.19).
  2. M - On Stop show "Keep changes from Play?" with the list; keep applies them to the edit scene by guid. Depends on: package 1, #570 (guids on main).
  3. S - Mark Play-edited fields in the Inspector (coloured) and a "Copy component" for Play. Depends on: package 1.
  4. S - Option "Apply" button per component in Play (like the hands one) generalised. Depends on: package 2.
  5. M - Undo that spans a Play session. Depends on: 22.1 package 6.
- **Open:** objects created or destroyed in Play have no edit-scene counterpart to keep changes on; the registry does not cover controllers, scripts, navigation, networking or animation and IK components yet (per `docs/properties.md`), so those edits could not be kept.
- **Related:** 15.2, 15.3, 22.1, 34.2
- **Words:** play mode changes, keep changes, tweak during play, Play edits, revert on stop, live values, tuning, Apply during Play
- **Seen as:** I tuned a value in Play and it was gone after Stop; cannot tell what I changed
- **Ask me like:** "ask me to keep changes made during Play when I press Stop"

### 34.2 Live object inspector
- **What:** Look at any object in the running game, including ones created while playing, and see its real current values.
- **Parts:**
  - 34.2.1 Live hierarchy and inspector - `editor/main.cpp` - the Hierarchy lists the live scene's objects (spawned ones too, per the object loop) and the Inspector reads their components
  - 34.2.2 Live values - `editor/main.cpp` - visual-script variables show a live value column; stat components show current/max with an editable field
  - 34.2.3 Script variable watch - `editor/main.cpp` - public script variables and overrides shown in the Inspector
  - 34.2.4 Runtime-only objects - `Scene/GameObject` - objects flagged `transient` (for example the weather rig) are never saved
- **Code:** `editor/main.cpp`, `Scene/GameObject`
- **API:** Hierarchy, Inspector, Variables window
- **Editor:** Hierarchy, Inspector, Variables during Play
- **Docs:** `docs/editor.md`
- **Tests:** none
- **Status:** Partial - runtime objects can be inspected; there is no filter for "created in Play", no pick-in-Game-view and no watch list.
- **Exists today:** The same panels as in edit mode, on the live scene. Not verified in a running editor: whether a long list of spawned objects (bullets) stays usable.
- **Work packages:**
  1. S - Hierarchy shows spawned-in-Play objects with a tag and a "created at frame" tooltip. Depends on: nothing.
  2. M - Click an object in the Game view to select and inspect it. Depends on: nothing.
  3. M - Watch window: pin properties of several objects (by `Component.property` path) and plot them over time. Depends on: the property registry (on main, #578, 1.19).
  4. S - Pause on selected object event (a simple "break when value changes"). Depends on: package 3.
  5. S - Pooled objects are grouped so bullets do not flood the list. Depends on: 21.1.
- **Open:** no per-object runtime counters; the live view relies on the same redraw as the edit view.
- **Related:** 15.1, 15.2, 21.1, 34.1
- **Words:** live inspector, runtime objects, inspect in play, spawned objects, watch window, Game view picking, live values
- **Seen as:** cannot find the bullet that is stuck; need to click an enemy in the Game view to see its health
- **Ask me like:** "click an object in the Game view to inspect it"; "add a watch window for properties during Play"

### 34.3 Hot reload
- **What:** Change a script, a texture or a plugin while the game runs and see the result without restarting.
- **Parts:**
  - 34.3.1 Plugin reload on save - `editor/main.cpp` - editor plugins reload when their file is saved (24.2)
  - 34.3.2 Script reload from file - `Components/ScriptComponent` - `ScriptComponent::Reload()` recompiles from the external file; the editor offers "Reload from disk"; open script buffers compile at Play start
  - 34.3.3 Asset hot reload (merged #568, unused) - `Core/AssetManager` - `SetHotReload` polls file time and size, re-decodes in the background and replaces the data in place (`Version()` increments)
  - 34.3.4 Database bridge - `Core/AssetDatabase` - `AttachHotReload` rehashes the file and raises a reimport signal
- **Code:** `editor/main.cpp`, `Components/ScriptComponent`, `Core/AssetManager`, `Core/AssetDatabase`
- **API:** `ScriptComponent::Reload`, `AssetManager::SetHotReload`, `SetReloadCallback`, `AssetDatabase::AttachHotReload`
- **Editor:** Script Editor "Reload from disk"; Plugins reload automatically
- **Docs:** `docs/asset_manager.md`, `docs/editor-plugins.md`
- **Tests:** `assetmanager`, `editor_plugins`
- **Status:** Partial - plugins and manual script reload exist; assets in a running scene do not reload because the engine does not load through the asset manager yet.
- **Exists today:** Script reload is a manual action; texture and mesh changes are picked up by the Project panel but the scene keeps the old data. State of running scripts is reset by a recompile (not checked in code).
- **Work packages:**
  1. S - Watch script files and recompile on save during Play, keeping public variable values. Depends on: nothing.
  2. M - Route texture, mesh and audio loads through `AssetManager` so hot reload applies. Depends on: 20.1 package 1.
  3. M - Reload materials and shader graphs live (26.2, 26.1). Depends on: package 2.
  4. S - Console message "reloaded X" with timing. Depends on: package 1.
  5. L - Native C++ plugin reload (unload, load, restore state). Depends on: 24.1 package 2.
- **Open:** in-place replacement happens inside `Pump()`, so readers must be on that thread; a failed half-written file keeps the old data and retries.
- **Related:** 13.4, 13.5, 20.4, 24.2, 24.1
- **Words:** hot reload, live reload, recompile, reload script, reload texture, file watcher, SetHotReload, edit while running
- **Seen as:** have to stop and press Play after a script edit; replaced a texture and the game still shows the old one
- **Ask me like:** "reload scripts automatically while playing"; "hot reload textures in the running scene"

## 35. Save game framework (High)

### 35.1 Save slots
- **What:** Let players save and load their progress in named slots, with a list for a Load screen.
- **Parts:**
  - 35.1.1 Whole-scene save slots - `Core/SaveGame` - `SaveGame::Save`, `Load`, `Exists`, `Delete`, `Label`, `List` in `saves/<slot>.okaygame`; atomic write, checksum, `.bak` fallback; script `save_game`, `load_game`, `has_save`, `save_label`, `delete_save`, `play_time`; visual-script SaveGame nodes
  - 35.1.2 Key-value save data - `Core/SaveData` - `Save` keys and `SaveFile` (Easy-Save-style values), per-slot copy
  - 35.1.3 Framework slots (#572) - `Core/SaveFramework` - `SaveManager` slots `<slot>.okayslot` with headers (label, scene, time, play time, thumbnail, kind, game version), listing, copy, rename and delete
  - 35.1.4 Slot menu model (#572) - `Core/SaveFramework` - `SaveMenuModel` rows with ready-made text and flags (empty, corrupt, newer version, auto, quick, can load, can save)
  - 35.1.5 Cloud sync hooks (#572) - `Core/SaveFrameworkSync` - `ISaveSync` with in-memory and folder backends; PlayFab user data and Steam Cloud calls exist elsewhere (`Platform/PlayFab/PlayFab`, `Platform/Steam/SteamService`) but nothing connects saves to them
  - 35.1.6 No-code slot calls - `Core/GameSaves` - `GameSaves::Save`, `Load`, `Continue`, `Delete`, `Exists`, `SavedText`, `SecondsSinceSave`, `AutoSave`; script `game_save`, `game_load`, `game_continue`, `game_delete_slot`, `game_slot_exists`, `game_slot_text`, `game_slot_age`, `game_slot_label`, `game_can_continue`, `game_autosave`; visual-script `Fn.game_*` nodes; Action List `game_*` actions; every failure is a `[save]` Console error
  - 35.1.7 Save Slots screen - `Scene/UITemplates` - `AddSaveSlots`: three slots with label and saved time, Save / Load / Delete, Continue, Close, status line (UI > Prebuilt Screens > Menus)
- **Code:** `Core/SaveGame`, `Core/SaveData`, `Core/SaveFramework`, `Core/SaveFrameworkScene`, `Core/SaveFrameworkSync`, `Core/GameSaves`, `Scene/UITemplates`, `Platform/PlayFab/PlayFab`, `Platform/Steam/SteamService`
- **API:** `SaveGame::Save/Load/List`, `Save::SetInt`; `SaveManager`, `SaveRequest`, `SaveMenuModel`, `ISaveSync`; `GameSaves`
- **Editor:** UI > Prebuilt Screens > Menus > Save Slots (Save / Load / Delete / Continue per slot); Add Component > Saveable and Save Settings
- **Docs:** `docs/save_games.md`; `docs/save_framework.md`
- **Tests:** `save_game`, `durable_saves`, `savedata`; `save_framework`, `save_scene`, `save_nocode`, `ui_templates`
- **Status:** Works - `SaveGame` slots are in use and tested; the framework (`SaveManager`, tests `save_framework`, `save_scene`) is now reachable without code through `GameSaves`, script builtins, visual-script nodes, Action List actions and the Save Slots screen (test `save_nocode`).
- **Exists today:** SaveGame slots with durable writes. The framework's slots are used by the no-code route (`game_*` calls and the Save Slots screen, which shows the slot label and saved time, not a thumbnail). The screen does not use `SaveMenuModel` and has three fixed slots; rename and thumbnails are not in it. Sync hooks are library code only.
- **Work packages:**
  1. S - Save Slots screen: a list bound to `SaveMenuModel` with any number of slots, rename and a thumbnail (the shipped screen has three fixed slots). Depends on: #572 (on main), 12.4.
  2. S - Screenshot thumbnail captured on save (needs 50.1). Depends on: package 1.
  3. M - Choose one system: either move SaveGame callers to the framework or keep both with clear names (two slot formats). Depends on: #572 (on main).
  4. M - Cloud sync: `ISaveSync` backends for Steam Cloud and PlayFab user data. Depends on: package 3, 49.1.
  5. S - Editor window listing saves in the project's save folder with open-folder and delete. Depends on: package 3.
- **Open:** possible duplicate: `SaveGame` (`.okaygame`) and the framework (`.okayslot`) are separate formats and separate lists; nothing converts between them (stated in `docs/save_framework.md`).
- **Related:** 1.7, 12.8, 17.10, 35.2, 49.1
- **Words:** save slot, save game, load game, Continue, SaveGame, SaveManager, slot list, load screen, cloud save, save folder
- **Seen as:** Load screen is empty; save corrupted after a crash (handled by .bak); two kinds of saves in the folder
- **Ask me like:** "make the Save Slots screen show a screenshot"; "sync saves to Steam Cloud"

### 35.2 Checkpoints
- **What:** Automatic save points (entering a level, reaching a checkpoint, timed autosave) and quick save and load.
- **Parts:**
  - 35.2.1 Auto Save component - `Components/AutoSave` - saves the whole game to a slot every N seconds and optionally when the scene starts
  - 35.2.2 Quick save, autosave ring and Continue (#572) - `Core/SaveFramework` - `SaveManager::QuickSave`, `AutoSave` rotating `autosave_1..3`, `ContinueSlot` loads the newest valid slot
  - 35.2.3 Named checkpoints and trigger (#572) - `Core/SaveFramework` - `CheckpointManager` (`Reach`, `Restore`, `RestoreLast`, keep newest N) and a `CheckpointTrigger` component that fires when the player comes within a radius or enters a trigger collider
  - 35.2.4 Save Settings component - `Core/GameSaves` - one per scene: autosave every N seconds and/or when the scene starts, into the ring of N autosave slots (skipped while a load is putting a save back); script `game_autosave`, Action List `game_autosave`
- **Code:** `Components/AutoSave`, `Core/SaveFramework`, `Core/GameSaves`
- **API:** `AutoSave` fields (`interval`, `slot`, `label`, `saveOnStart`), `SaveNow()`; `SaveConfig` fields (`autosaveInterval`, `autosaveOnSceneStart`, `autosaveSlots`); `SaveManager::QuickSave`, `AutoSave`, `CheckpointManager::Reach`, `RestoreLast`, `CheckpointTrigger`
- **Editor:** Add Component > Auto Save, Save Settings (autosave)
- **Docs:** `docs/save_games.md`; `docs/save_framework.md`
- **Tests:** `save_game`; `save_framework`; `save_nocode`
- **Status:** Partial - `AutoSave` and the framework's autosave ring (set up with Save Settings) work without code; quick save and checkpoints are on main (#572) but a `CheckpointTrigger` cannot be saved in a scene file yet.
- **Exists today:** `AutoSave` (interval 120 s by default) saving through `SaveGame`, and `SaveConfig` saving through the framework's ring. `CheckpointTrigger` still has to be attached from code (`Saveable` now has a scene-file record).
- **Work packages:**
  1. S - Scene serializer entry and Inspector for `CheckpointTrigger` (`Saveable` has both now). Depends on: #572 (on main).
  2. S - Death and respawn flow: `RestoreLast` after the player dies, as a visual-script node and script call. Depends on: package 1.
  3. S - Quick save and quick load keys through `InputActions`. Depends on: #572 (on main), 32.4.
  4. S - "Saving..." indicator widget. Depends on: 12.2.
  5. M - Autosave ring uses `AutoSave` through the framework instead of one slot. Depends on: 35.1 package 3.
- **Open:** AutoSave writes one slot, so a corrupt moment is overwritten; wait for the framework decision (35.1).
- **Related:** 22.2, 35.1, 11.6, 4.1
- **Words:** checkpoint, autosave, quick save, quick load, Continue, respawn point, AutoSave component, save point
- **Seen as:** after dying I restart the whole level; no sign the game saved
- **Ask me like:** "add a Checkpoint component"; "show a Saving icon when the game autosaves"

### 35.3 Serialization
- **What:** What gets saved and how objects find their saved state again.
- **Parts:**
  - 35.3.1 Whole-scene save - `Core/SaveGame` - every object with its components, spawned and destroyed objects, rigidbody velocities, script variables, play time (per `docs/save_games.md`)
  - 35.3.2 Inventory persistence - `Inventory/InventoryPersistence` - inventories, chests and equipment saved and restored
  - 35.3.3 Saveable state (#572) - `Core/SaveFrameworkScene` - `Saveable` component with a key, typed field helpers, `SaveSystem::CaptureScene` and `ApplyScene`, spawned and destroyed objects handled
  - 35.3.4 Stable object ids - `Scene/SceneSerializer` - persistent `GameObject::Guid()` (merged #570), usable as a save key
  - 35.3.5 Clocks, time scale and random streams - `Core/TimeSnapshot`, `Core/Countdown` - (#585) `TimeSnapshot::Capture().AddRandom("loot", rng).Write(doc.Writer("time"))` stores every `Clock`, the gameplay time scale and named random streams in a save document so a reload cannot re-roll a chest; `Cooldown` and `Countdown` have `Save/Load` through `SaveWriter`/`SaveReader` (1.1.14, 1.1.15); `Time::ElapsedTime` and `FrameCount` are deliberately not restored
  - 35.3.6 No-code Saveable - `Core/SaveFrameworkScene`, `Scene/SceneSerializer` - the `Saveable` component stored in the scene file (record `saveable`) with a save id, flags for transform / active / built-in components, a list of property-registry paths, script and visual-script variables, scene-wide Action List and visual-script variables; added and edited in the Inspector
- **Code:** `Core/SaveGame`, `Inventory/InventoryPersistence`, `Core/SaveFrameworkScene`, `Core/SaveFramework`, `Core/TimeSnapshot`, `Core/Countdown`, `Scene/SceneSerializer`
- **API:** `SaveGame::Save`; `Saveable` (`saveId`, `properties`, `saveVariables`, `saveSceneVariables`), `SaveSystem::CaptureScene`, `ApplyScene`
- **Editor:** Add Component > Saveable (keep when saving); Inspector: save id, New id, what to save, property picker
- **Docs:** `docs/save_games.md`; `docs/save_framework.md`
- **Tests:** `save_game`, `inventory_persist_craft`; `save_scene`, `save_nocode`, `time_snapshot`, `countdown`
- **Status:** Works - whole-scene saves are shipped and tested; the selective saveable-objects model has a scene-file record, an Inspector and a no-code route (tests `save_scene`, `save_nocode`).
- **Exists today:** Whole-scene save replaces the scene on load, so scripts restart and saves tie to one scene layout. Streamed worlds (27.4) are not covered.
- **Work packages:**
  1. S - Save keys use `GameObject::Guid()` (the framework doc says "GameObject has no GUID in this build"; #570 added one) instead of name paths; the Saveable's own save id is the key today.
  3. M - Saves cover streamed cells (state of unloaded cells kept as data). Depends on: 27.4 package 1.
  4. S - Pick properties by flag, not by path: save every property that is not `RuntimeOnly` (the Saveable lists paths one by one today). Depends on: the property registry (on main, #578, 1.19).
  5. S - Save diff viewer in the editor for debugging. Depends on: 28.2.
- **Open:** the two systems (whole scene vs saveable state) overlap; streamed worlds not covered.
- **Related:** 1.5, 11.1, 27.4, 35.1, 35.4
- **Words:** serialization, saveable, SaveSystem, capture scene, apply scene, persistent id, what gets saved, inventory save
- **Seen as:** an object does not come back after Load; a destroyed pickup respawns; script variables reset
- **Ask me like:** "make only marked objects save"; "save the state of unloaded cells"

### 35.4 Versioned save data
- **What:** Old saves keep working after the game changes, and saves from a newer version are refused instead of damaged.
- **Parts:**
  - 35.4.1 Save file header - `Core/SaveGame` - `okaygame <version>`, end marker and checksum; damaged or cut-off files refused; version-1 files without a checksum still load
  - 35.4.2 Migration steps - `Core/SaveGame` - `SaveGame::SetMigration(fromVersion, fn)` runs steps in order on the file text
  - 35.4.3 Newer-version refusal - `Core/SaveGame` - "saved by a newer version" is never half loaded
  - 35.4.4 Migration chain and slot flags (#572) - `Core/SaveFramework` - `SaveStore` data version plus ordered migration steps (1 to 2 to 3), `incompatible` and `fromBackup` flags in slot headers
- **Code:** `Core/SaveGame`, `Core/SaveData`, `Core/SaveFramework`
- **API:** `SaveGame::SetMigration`; `SaveStore::SetDataVersion`, `RegisterMigration`, `MigrationPath`, `SaveSlotInfo::incompatible`
- **Editor:** none
- **Docs:** `docs/save_games.md`; `docs/save_framework.md`
- **Tests:** `durable_saves`, `save_game`; `save_framework`; `save_nocode`
- **Status:** Works - header, checksum, newer-version refusal and migration hook are in the shipped code and tested; the no-code calls (`game_load`, Continue) report a newer-version, damaged or missing slot as a `[save]` Console error.
- **Exists today:** Everything in Parts 35.4.1-35.4.3.
- **Work packages:**
  1. S - Game-version string in the header (framework slot header has `gameVersion`); show it in a load screen. Depends on: #572 (on main).
  2. M - Migration helpers for common changes (rename a key, move a value) so authors do not write text code. Depends on: nothing.
  3. S - Test pack of old save files kept in the repo, loaded in CI. Depends on: nothing.
  4. S - Slot status text ("Saved by a newer version") in the Save Slots screen rows (the Console reports it today). Depends on: 35.1 package 1.
- **Open:** migrations operate on text; no tool to inspect a save.
- **Related:** 1.7, 25.3, 35.1, 22.4
- **Words:** save version, migration, newer version, incompatible save, checksum, SetMigration, old saves, damaged save
- **Seen as:** old save will not load after an update; "saved by a newer version" message
- **Ask me like:** "add a helper to rename a saved key during migration"
