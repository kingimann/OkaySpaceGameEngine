# Core assets and scene upgrade

Branch `claude/core-assets-1`. Owner request: "improve the core engine" (assets and scene part). Everything below was
checked against `origin/main` and the open `claude2/*` branches first, so existing work is extended, not copied.

Written from our own designs: FNV-1a (a public-domain hash algorithm, written out here), a least-recently-used list, and the
text record layout of our own scene format. No third-party code was read or copied.

## Existing / Gap

| Item | Existing (found in code) | Gap (what this change adds) |
| --- | --- | --- |
| 1.11 Asset manager | `Core/AssetManager`: handles, weak cache, async load, mtime+size hot reload, report (#568). Nothing called it. `AudioClip::LoadCached` has its own LRU + `TrimCache`; the picture cache (`GetCachedTexture`) is a bare map; `DataAsset::Cached` is a bare map. | Memory budget with LRU eviction (off by default), hash-checked hot reload, `Stats()`, external caches (audio, pictures) counted and trimmed by the budget, `DataAsset::Cached` is a real manager entry, `Pump()` in the editor and player loops, Console `assets`, Project panel footer readout. |
| 1.12 GUID references | `Core/AssetDatabase`: GUIDs in `.okaymeta`, move tracking, redirects, `FixStaleReferences` (rewrites paths after a tracked move), `FindMissing`. `claude2/prefab-override-by-guid` is about prefab override paths (object guids inside prefabs), not asset GUIDs: no overlap. `claude2/merge-idless-rename` is scene merge: no overlap. | `Core/SceneAssetGuids`: an `assetguid` table in the scene file, repair of paths by GUID when an asset was moved or renamed outside the editor (no redirect history), broken-reference report, editor hooks on save and load. |
| 1.13 Object pooling | `Scene/ObjectPool`, `Spawner::usePool` (saved in scenes, in the inspector), script calls `pool_get` / `pool_release` / `pool_prewarm` / `pool_idle`, Visual Script Take From Pool / Release to Pool, tests in `test_object_pool_audit.cpp`. The feature map row was stale. | One consistent dotted family `Pool.Get` / `Pool.Release` / `Pool.Prewarm` / `Pool.Idle` in OkayScript (aliases of the same builtins, tested). A dedicated Visual Script Prewarm node is NOT added (it is reachable as `Fn.pool_prewarm`); feature map row corrected. |
| 1.2 Scene robustness | `SceneSerializer::SaveToFile` already writes a temp file, checks the size, keeps `<scene>.bak` and renames over the target. Versioned format (`okayscene 3`), `Core/SceneMigration` registry with fixtures for versions 1 to 3 (`test_scene_migration.cpp`). `SceneSerializer::Validate`, `Testing/SceneValidator` (duplicate guids, broken parent links, parent cycles as a load error, missing assets). | A menu item: File > Validate Scene (lists the findings in the Console). Nothing else was missing, so no second validator or second atomic writer was written. |

## What changed

### 1. Asset manager (1.11)
- `SetBudget(bytes)` (0 = unlimited, the default: behaviour is identical to before). With a budget the manager keeps loaded
  assets and drops the least recently used unheld ones when over budget. `Pump()` skips assets used since the previous `Pump()`
  so raw pointers used in the current frame stay valid; `Trim()` and `EvictUnused()` are explicit.
- Hot reload hashes the file: same bytes with a new time are not decoded again.
- `Stats()`, `RegisterExternalCache`, `Adopt`, `Core/AssetCacheBridge.hpp` (a tiny header without thread includes).
- `DataAsset::Cached` keeps its stable `DataAsset&` and registers it; the audio decode cache and picture cache report into the
  manager. They are bridged, not moved onto handles (see the feature map "Open").
- Console `assets [stats|list|trim|evict|budget <MB>|hot on|off]`; Project panel footer line with tooltip.
- Tests: `asset_cache`.

### 2. GUID references in scenes (1.12)
- Record: `assetguid <32 hex> "<path as written>" "<project path when saved>"`, directly after the header line.
  Old files have none and load exactly as before (tested for versions 1 to 3). The loader of this build skips the record; the
  dependency scanner ignores it.
- `Stamp` writes it (and gives each referenced asset a `.okaymeta`, so the GUID lasts), `Resolve` / `RepairFile` rewrite the paths of
  moved or renamed assets (old text kept as `<scene>.bak`), `FindBroken` lists references that resolve to nothing. A path that now
  names a different asset is never hijacked.
- Editor: saving a scene under an `Assets` folder stamps it; opening one repairs it and lists repairs and broken references in the
  Console. `OKAY_SCENE_GUIDS=0` switches it off.
- Tests: `scene_guids`.

### 3. Pooling (1.13): see the table. Test: `object_pool_audit` (new suite for the `Pool.*` names).

### 4. Scene robustness: see the table. Menu: File > Validate Scene. Test: `menu_bar_model` expectations updated.

## Not done / unverified
- Windows build not run. The editor and player changes (`Pump`, Console command, Project footer, hooks, menu item) are written but
  the editor target needs SDL2 and a Windows or full Linux build; see the verification notes in the PR.
- Prefabs saved by the editor get the GUID table only through the scene hooks (the prefab save path was not wired).
- Hot reload still notices a change only when time or size changes.
- Audio and picture caches are not on handles.
- No dedicated Visual Script Prewarm node.
