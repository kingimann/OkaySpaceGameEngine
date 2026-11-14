# Asset manager

`okay::AssetManager` (`okay/Core/AssetManager.hpp`) is an opt-in central registry for
files loaded from disk. Nothing in the engine uses it yet; existing loading paths
(`DataAsset::Cached`, `AudioClip::LoadCached`, the renderers' own loads) are unchanged.
Wiring consumers onto it is left for a later change.

```cpp
auto& assets = okay::AssetManager::Instance();      // default loaders registered
auto img = assets.Get<okay::Image>("art/../art/hero.png");   // sync, cached
if (img) use(*img);

auto clip = assets.LoadAsync<okay::AudioClip>("sfx/boom.wav",
    [](okay::AssetHandle<okay::AudioClip> h, const std::string& err) { /* main thread */ });
// every frame, on the main thread:
assets.Pump();
float p = assets.Progress();                         // 1 when nothing is loading
```

## Handles and reference counting
`AssetHandle<T>` is a cheap copyable reference. `Ready()` / `operator bool` say the
asset is usable; `Get()` is null until then. The asset is freed when the last handle
is dropped (the cache only holds weak references), and loading it again re-reads the
file. Cache slots are keyed by asset type and canonical path, so the same file can be
held as two types.

## Path normalization
`AssetManager::Normalize` makes one key per file: `\` becomes `/`, repeated slashes
collapse, `.` and `..` resolve lexically (leading `..` of a relative path is kept, `..`
above the root of an absolute path is dropped), no trailing slash. Case is folded to lower
case on Windows only (`#ifdef _WIN32`; `Normalize(path, foldCase)` exposes both policies, so `Data/x` and `data/x`
are two files on Linux and macOS). Windows prefixes stay in the key: `\\server\share\x` becomes `//server/share/x`
(not the same file as `/server/share/x`) and `\\?\C:\x` becomes `//?/C:/x`. **Only the cache key is normalized.**
The loader is handed the caller's own path string, exactly as written (so a network share or a long-path
`\\?\` path opens as it always did). Two spellings of a file share one entry, and the first caller's spelling is
the one that is opened. Symlinks are not resolved, and relative
paths are relative to the working directory (the key does not include it).

## Loaders
`RegisterLoader<T>(name, load, sizeOf)` registers a function returning `shared_ptr<T>`
(null plus an error string on failure). It may run on the worker thread. `T` must be
move-assignable. `RegisterDefaultLoaders()` registers Image, AudioClip, Mesh, DataAsset
and Material by calling their existing load functions (`Image::Load`, `AudioClip::Load`,
`modeling::LoadMeshAsset`, `DataAsset::Load`, `Material::LoadFromFile`). A plain
`AssetManager` starts with no loaders; `Instance()` has the defaults.

Failed loads are never cached: the next `Get` tries the file again.

`Evict<T>(path)` forgets one cache slot, so the next `Get` decodes the file again (handles that already exist keep
the old asset alive and valid). `Adopt<T>(path, shared_ptr<T>)` puts an asset the caller decoded itself into the
cache as a ready entry; if a live entry for that file already exists, that one wins and its handle is returned.

## The old caches now live here
There is one cache for these loaders; each keeps its call signature and behaviour:

| Entry point | Manager type | What stays the same |
|---|---|---|
| `DataAsset::Cached`, `Reload`, `Invalidate`, `InvalidateAll` | `DataAsset` | One object per path, kept for the life of the process; edits with `Set` stay; `Reload` and `Invalidate` change the object in place so a held reference stays valid; a missing file gives an empty asset to `Set` into that fills in place when the file appears (that object is then adopted into the manager) |
| `AudioClip::LoadCached(path, rate, error, downmix)` | an internal `SharedClip` type; the key is `<path>\n:rate=N[,mono]` | The same `shared_ptr` for the same file, rate and mono; a rate is its own entry; failures are not cached |
| `LootTable::Cached` / `Forget` | `LootTable` | Loaded once per path; a file that fails to load is remembered as an empty table until `Forget`; `Forget` now also evicts the manager slot |
| `RecipeBook::Cached` / `Forget` | `RecipeBook` | Same as the loot table |

Each of these keeps a handle to its assets for the life of the process (as the old caches never freed an asset), so
the weak manager cache does not free them. Differences you can see: two spellings of one file (`a/./b.png` and
`a/b.png`) now share one entry, the usage `Report()` lists these assets, and a manager user who calls `Get<DataAsset>`
sees the same object `DataAsset::Cached` returns (an `Invalidate` empties it for them too). The audio entries have no
file name of their own in the key, so hot reload skips them. Tests: `asset_cache_unify` (written first and run
against the old code, 35 checks, then against the new), plus `core_data`, `dataasset`, `audio_formats`,
`audio_stereo`, `durable_saves`, `item_loot_recipes`.

## Async loading
`LoadAsync` queues a decode on one background thread and returns a handle that is
`Loading()`. Callbacks always run from `Pump()` on the main thread, with no manager lock
held, so they may call back into the manager. Duplicate requests share one decode, and
a sync `Get` of a file being decoded waits for that decode instead of repeating it.
`PendingCount()` and `Progress()` describe the current batch.

## Hot reload
`SetHotReload(true, intervalSeconds)` makes `Pump()` compare each loaded file's mtime and
size. A change re-decodes on the worker, and `Pump()` moves the result into the existing
object, so handles and pointers stay valid; `Version()` increments and the reload
callback (`SetReloadCallback`) fires. A loader can be registered with a fourth argument, `canReload(const T&)`; hot
reload skips an entry for which it returns false. `DataAsset` uses it: after a script's `Set()` or `Remove()` the asset
is left alone (the edit is not thrown away when the file changes on disk) until it is saved or loaded again. Hot reload
is off by default. A failed re-decode (file missing or half written)
keeps the old data and retries on the next change. The in-place assignment happens
inside `Pump()`, so code that reads an asset should run on that same thread.

## Report and thread safety
`Report()` lists every cached asset (type, bytes, reference count, reload count) with
per-type totals; `ToString()` formats it. Byte counts come from each loader's `sizeOf`.
All manager methods are safe to call from any thread; `Pump()` is meant for one
(main) thread.

## Not covered
GPU textures and other renderer resources are not tracked here; only CPU-side assets.
Not moved: `WeaponBook::Cached` (its `Load` fills its own cache as a side effect), the software renderer's texture
caches (`GetCachedTexture`, `GetCachedMips`: pointers into one map, `@name` and `proc:` pictures, the mip chains),
the painter's UI textures and the renderers' GPU uploads. A DataAsset reference taken for a missing file does not
fill if someone else loaded that file through the manager first (the two objects stay separate).
