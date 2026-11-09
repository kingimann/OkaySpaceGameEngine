# Scene management

What the engine gives you for loading scenes, spawning objects and declaring what a
component needs. Headers: `okay/Scene/Scene.hpp`, `SceneManager.hpp`, `ObjectPool.hpp`,
`GameObject.hpp`.

## Loading scenes

The one-go calls are unchanged. `Scene::RequestLoad(path)` replaces the scene at the end of
the frame, `Scene::RequestMerge(path, offset)` adds a scene next to what is there, and
`SceneManager` keeps the build list (`AddScene`, `LoadScene`, `LoadSceneByName`,
`LoadNextScene`, `ReloadScene`).

### Over several frames (loading screens)

```cpp
Scene::AsyncLoadOptions o;
o.objectsPerFrame = 64;                       // GameObjects made per Update
o.onProgress = [&](float p) { bar.value = p; };   // 0..1 after each step
o.onDone     = [&](bool ok) { ... };          // true loaded, false failed / cancelled
int h = SceneManager::LoadSceneAsyncByName(scene, "Level2", o);   // 0 = unknown / unreadable
```

- The file is read when you call; `objectsPerFrame` objects are created at the end of each
  `Scene::Update`, so the frame loop (and a loading screen) keeps running. The parse is
  the same one a synchronous load uses.
- Nothing in the new content gets `Awake` / `OnEnable` / `Start` until **all** of it is in, and
  parent links resolve at the end, so nothing runs against a half-built hierarchy.
- `Scene::IsLoading(h)`, `IsLoaded(h)`, `LoadProgress(h)`, `AnyLoading()`; the same on
  `SceneManager` (`IsLoading(scene, h)`, ...).
- A replacing load (`additive = false`, the default) clears the scene on its first step,
  except `DontDestroyOnLoad` objects, and takes the file's sky / lighting settings.
  `SceneManager` moves `ActiveIndex()` when it finishes OK.
- An **additive** load (`additive = true`, optional `offset`) merges, like `RequestMerge`:
  the host keeps its name and settings, new top-level objects move by `offset`.
- `Scene::UnloadAdditive(h)` / `SceneManager::UnloadScene(scene, h)` removes what that load
  brought in (its top-level objects and their children, destroyed at the end of the frame).
  Called mid-load it stops the load and removes what it made (`onDone(false)`). Two
  additive loads unload independently. For a replacing load it only removes the new
  content; the old scene does not come back.
- A parse error or `Scene::Clear()` abandons loads in flight (`onDone(false)`).

### Rules for loads that overlap, fail or keep objects

- **The last replacing request wins.** `RequestLoad`, `RequestLoadText` and a replacing `BeginAsyncLoad` each cancel the
  replacing request before them that has not finished: a deferred load still waiting hears `done(false)`; an async load in
  flight is stopped, what it made is removed and its `onDone(false)` runs. (A deferred load that is requested first and an
  async one begun later in the same frame: the async one wins; it used to be the other way round.) Additive loads and
  `RequestMerge` are not cancelled by a request but go with the scene when a replacing load clears it (`onDone(false)`).
- **A load asked for from a callback** (`onDone`, `onProgress`, a component's `Update`, `Awake`, `Start`, `OnDestroy`) is
  safe. `RequestLoad` from any of them replaces the scene at the end of the frame; `BeginAsyncLoad` from `onDone` makes the
  next level start stepping in the same frame (chained levels). Every load is found again by its handle after each
  callback, so a callback may begin, unload or cancel loads, including its own.
- **A load that fails leaves the scene as it was.** The file is read and checked in full before the scene is touched (`Clear`
  comes after). That includes the `DontDestroyOnLoad` objects, which are taken out of the scene while it is replaced: when
  the load fails they go back where they stood (object order, update order, the parent a kept child was taken off, the main
  camera). `LastLoadError()` says why; for a loading in pieces `onDone(false)` runs on the frame of the first step.
- **Order of events when a scene is replaced:** `OnDisable` then `OnDestroy` of every object of the old scene, then
  `Awake`, `OnEnable`, `Start` of the new objects (in the one frame for a one-go load; when the whole scene is in for a
  load in pieces), then `onDone(true)`. Kept objects get none of these. A one-go load and a load in pieces put the kept
  objects after the loaded ones, in the order they had.
- **Unloading an additive scene that holds the main camera:** the objects go at the end of the frame and
  `Scene::mainCamera` falls back to another enabled camera, or to null; it is never left pointing at a freed one.
- `SceneManager::ActiveIndex()` moves when a one-go load succeeds. For `LoadSceneAsync` it moves when the load is
  started and goes back to the last scene that loaded if it fails.

### A loading screen that loads in the background

`LoadingScreen` (the component behind the Loading Screen preset) loads its `targetScene` with `BeginAsyncLoad` when it
is on an object of its own (just the Transform and this component, as in the transition-scene setup): `Begin()` starts the
load, the object is marked `DontDestroyOnLoad` so the screen stays up while the old scene is replaced and the new one is
built `objectsPerFrame` objects at a time, `Progress()` is the slower of the clock (`duration`) and the real progress, and
when both are done the screen fades out over the new scene and its object is destroyed. `Loading()` and `LoadFailed()` say
where it is. A scene that cannot be loaded (unknown, unreadable, damaged) reveals the scene that was showing and keeps the
object. With `asyncLoad` off, or with other components on the object, it loads in one go after `duration` as it did before;
a screen saved by an older build has `asyncLoad` off, one made now has it on. A target that is not in the build list no
longer leaves the screen up for ever.

## Object pool

```cpp
ObjectPool bullets(scene);
bullets.SetPrefabFile("Assets/Bullet.okayprefab");   // or SetTemplate(const GameObject&)
bullets.maxSize = 64;        // most idle objects kept; more are destroyed on Release
bullets.Prewarm(32);         // idle, switched off, ready
GameObject* b = bullets.Get(muzzlePos);   // active, at the template pose (+ position)
...
bullets.Release(b);          // off + kept for the next Get
```

- Objects are cloned with the same serializer `Scene::Instantiate` uses, so prefab links and
  required components behave the same. The source is captured once; the template object may
  be destroyed afterwards.
- Reset your state in `OnEnable` (and clean up in `OnDisable`). `Release` switches the object
  off; the scene delivers `OnDisable` at the start of the next frame, and `OnEnable` when it is
  switched on again. Because that is the only reliable reset point, an object released this
  frame is **not** handed out again until the next frame (`Get` makes a new one meanwhile; an object whose
  components never started is reusable at once).
- A reused object is put back to a fresh copy of the template as far as the pool can tell: the root goes back to the
  template's local pose and has no parent, and its tag, layer and `active` flag come back; the same for every object
  below it, and the `enabled` flag of each component, as long as the hierarchy and the components still have the
  template's shape (when a child was added or removed only the root is put back). Each component then gets
  `OnPoolReuse()` to reset what only it knows: `Rigidbody2D` / `Rigidbody3D` (velocity, spin, waiting forces,
  sleep, interpolation history) and `Lifetime` (elapsed time) do, your own scripts can override it.
- A delayed `Scene::Destroy(go, seconds)` that is still waiting when the object is `Release`d is cancelled (the use it was asked for is
  over). An object that is queued for destruction, or destroyed behind the pool's back, is never handed out and is forgotten.
- `Release` returns false for an object that is not from this pool. Objects destroyed behind
  the pool's back, or a cleared scene, are forgotten (and do not count against `maxSize`). `IdleCount()`, `ActiveCount()`,
  `CreatedCount()`, `ReuseCount()` help tune the pre-warm size; `Get` and `Release` look at one or two objects, the two counts
  look at every object the pool holds once after the scene changed (`LookupCount()` counts the lookups).
- A pool is main-thread only, like the scene, and must not outlive it.
- **Spare objects are not saved.** An idle object (released, or made by `Prewarm`) is marked `transient`, so a saved scene,
  a save game and an undo snapshot do not carry the pool's spares (and nothing below them); `Get` makes it an ordinary
  object again, so a bullet in flight is saved like any other object.
- **`Shrink(keep)`** destroys idle objects until at most `keep` are left, the ones idle longest first, and returns how many
  went. Lowering `maxSize` only stops the pool keeping more; call `Shrink(maxSize)` to give back what it holds.
- **Ending an object's life: `Scene::Despawn(go)`.** It puts `go` back into the pool it came from (when it came from one that
  is still there) and otherwise does what `Destroy` does. `Lifetime` and `Projectile3D` end with it, so a pooled bullet or
  spark that runs out of time goes back to its pool instead of being freed. Call it from your own components the same way.
- **More resets on reuse** (`Component::OnPoolReuse`): `ScriptComponent` (the script is compiled afresh, so its variables are
  what they were at the start, and its `start()` runs before the next `update()`), `Projectile3D` (age, hits, bounces, the
  velocity it was made with), `TrailRenderer` (the trail is cleared), `ParticleSystem` (particles cleared, plays if it played
  when made) and `SpriteAnimator` (first frame, the clip and play state it was made with). Still each component's own job:
  `HealthStat` (health and the dead state), the character controllers' velocities, visual scripts and action lists.
- **A Spawner with `usePool`** makes its copies from a pool of its own (`Spawner::Pool()`), from the named template or the
  prefab file; copies end with `Lifetime` / `Despawn` and come back. Saved as a trailing `1` on the `spawner` record.
- **Scripts:** `pool_get("Bullet.okayprefab", x, y [, z])` spawns from the scene's shared pool for that file (`ObjectPool::ForPrefab`:
  one pool per scene and file, made on first use, read once); `pool_release()` hands the script's own object back (destroys
  it when it did not come from a pool); `pool_prewarm("Bullet.okayprefab", n)` and `pool_idle("Bullet.okayprefab")`.

## RequireComponent / DisallowMultipleComponent

Register rules once at startup, before scenes load:

```cpp
RequireComponent<Turret, Gun>();          // AddComponent<Turret> adds a Gun first if there is none
RequireComponent<Gun, Rigidbody3D>();     // chains work; a loop (A needs B needs A) ends
DisallowMultipleComponent<Solo>();        // a second AddComponent<Solo> returns the existing one
```

- The rules run inside `GameObject::AddComponent<T>`, so they cover scripts, the scene loader,
  prefab instantiation and the editor's Add Component. Required components land before the
  one that needs them (so they `Awake` first). With no rules registered the cost is one flag read.
- Matching is on the exact type `T`. For uniqueness an existing component of `T` or a subclass
  counts.
- `GameObject::RequiredBy(component)` returns the component that still needs `component`
  (or nullptr), so an editor can grey out Remove. `RemoveComponent` itself does not refuse,
  so scene teardown and scripts keep working.
- `ComponentRules::Clear()` drops every rule (tests).

## Spawning and destroying objects

`Scene::Instantiate` has overloads for a world position and rotation and for a parent, and
`Scene::Duplicate(go)` copies an object next to itself under the same parent with a unique name
(`Scene::UniqueName`). `Scene::Destroy(go)` stays deferred to the end of the frame;
`Scene::DestroyImmediate(go)` is for tools and tests (from inside an `Update` it queues like
`Destroy`). An object a handler parents under a dying one during `OnDestroy` becomes a root instead
of hanging under the dead shell. Details in `scene_hierarchy_search.md`.

## Tests

`tests/test_scene_load_pool.cpp` (ctest name `scene_load_pool`); spawning: `tests/test_scene_spawn.cpp`
(`scene_spawn`); `scene_lifecycle_audit`, `object_pool_audit` and `loading_screen_async` (wave 7, below).

## What changed: scene lifecycle and pooling, wave 7 (1.4, 1.13)

Tests: `scene_lifecycle_audit` (event order, callbacks, overlapping and failing loads, kept objects, the camera of an additive
scene), `object_pool_audit`, `loading_screen_async`.

* **Fixed: a load begun from the `onDone` of another corrupted the heap.** The first step of a replacing load clears the
  scene and with it the list of loads, and the stepping loop put the load back at its old index in a list that was empty
  (any chained loading screen: level 1 done, begin level 2). The loop now finds each load again by its handle.
* **Fixed: two replacing loads begun in one frame:** the earlier one won and the later one heard `false` (the first step of
  the earlier cleared the later). The last replacing request now wins (see the rules above).
* **Fixed: a failed load moved `DontDestroyOnLoad` objects to the end of the scene,** took a kept child off a parent that
  stayed, and could swap the main camera; they now go back as they were.
* **Changed: after a load in pieces the kept objects come last,** as after a one-go load (they used to sit between the
  first objects made and the rest).
* **Added:** `Scene::Despawn`, `Scene::CancelDontDestroyOnLoad`, `ObjectPool::Shrink`, `ObjectPool::ForPrefab`, spare
  pooled objects are not saved, `OnPoolReuse` for scripts, projectiles, trails, particles and sprite animators, `Lifetime`
  and `Projectile3D` give pooled objects back, `Spawner::usePool`, the script calls `pool_get`, `pool_release`,
  `pool_prewarm`, `pool_idle`, and the Loading Screen's background load (`asyncLoad`, `objectsPerFrame`, `Loading()`,
  `LoadFailed()`).
* Checked and fine: a done callback may start a load, a component may request one from its own `Update`, the events of a
  swap come in the order above, a pool survives its scene being cleared, released twice, destroyed behind its back, and
  objects that are not its own.
* Not done: the editor has no field for `Spawner::usePool` or the Loading Screen's `asyncLoad` yet (they are saved and can be
  set from code and scripts); `HealthStat` does not reset on reuse, so a pooled enemy needs its own reset.
