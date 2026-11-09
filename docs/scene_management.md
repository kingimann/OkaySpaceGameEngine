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
(`scene_spawn`).
