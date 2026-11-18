# Asset manager

`okay::AssetManager` (`okay/Core/AssetManager.hpp`) is an opt-in central registry for
files loaded from disk. The existing caches now report into it: `DataAsset::Cached` is registered as a
real entry, and the audio decode cache and the picture cache (used by every renderer) are bridged
(see "Memory budget" below). The editor and the player call `Pump()` once a frame.

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
case on Windows only (`Normalize(path, foldCase)` exposes the policy). The file is opened
with the normalized path in its original case. Symlinks are not resolved, and relative
paths are relative to the working directory (the key does not include it).

## Loaders
`RegisterLoader<T>(name, load, sizeOf)` registers a function returning `shared_ptr<T>`
(null plus an error string on failure). It may run on the worker thread. `T` must be
move-assignable. `RegisterDefaultLoaders()` registers Image, AudioClip, Mesh, DataAsset
and Material by calling their existing load functions (`Image::Load`, `AudioClip::Load`,
`modeling::LoadMeshAsset`, `DataAsset::Load`, `Material::LoadFromFile`). A plain
`AssetManager` starts with no loaders; `Instance()` has the defaults.

Failed loads are never cached: the next `Get` tries the file again.

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
callback (`SetReloadCallback`) fires. A failed re-decode (file missing or half written)
keeps the old data and retries on the next change. The in-place assignment happens
inside `Pump()`, so code that reads an asset should run on that same thread.

## Report and thread safety
`Report()` lists every cached asset (type, bytes, reference count, reload count) with
per-type totals; `ToString()` formats it. Byte counts come from each loader's `sizeOf`.
All manager methods are safe to call from any thread; `Pump()` is meant for one
(main) thread.

## Not covered
GPU textures and other renderer resources are not tracked here; only CPU-side assets.
`DataAsset::Cached` is unchanged: it already retries files that failed to open.

## Memory budget, hash-checked hot reload and stats (added with core assets upgrade)
- `SetBudget(bytes)`: 0 (default) means unlimited and nothing changes. With a budget the manager keeps loaded
  assets alive after their last handle and drops the least recently used ones nobody holds when the total is over.
  A held handle is never evicted. `Pump()` only evicts assets not used since the previous `Pump()` (so a raw pointer
  used this frame stays valid); `Trim()` evicts at once; `EvictUnused()` drops everything only the cache holds.
- External caches (`RegisterExternalCache`, or `RegisterAssetCache` from `Core/AssetCacheBridge.hpp`) count towards the
  total and are trimmed by the budget: the audio decode cache (`AudioClip::TrimCache`) and the picture cache (pictures
  read from files are dropped and read again when next drawn; pictures registered by name, `@...`, stay).
- `DataAsset::Cached` keeps its stable `DataAsset&`, and registers it with `Adopt` as `data:<path>`. Hot reload
  (when on) re-reads it in place; the memory budget never evicts it (scripts hold references and may have unsaved `Set`s).
- Hot reload: a changed time or size is a hint; the file is then hashed (FNV-1a 64) and a rewrite with the same
  bytes is not decoded again (`Stats().reloadsSkippedSameHash`). Assets loaded while hot reload was off have no
  hash and re-decode on the first change. A content change with an unchanged time and size is not noticed.
- `Stats()` / `AssetStats::ToString()`: hits, misses, loads, failures, evictions, reloads, bytes, per-cache sizes.
- Console command `assets`: `assets` (stats), `assets list`, `assets trim`, `assets evict`, `assets budget <MB>`
  (0 = unlimited), `assets hot on|off`. The editor's Project panel footer shows the one-line readout with a tooltip.
- Tests: `asset_cache` (tests/test_asset_cache.cpp).
