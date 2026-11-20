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
