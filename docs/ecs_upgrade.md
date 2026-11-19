# Entity / component system: what exists, what was missing, what changed

Request: "Improve Entity/component system." This page was written after reading the code first (`Scene/{Scene,GameObject,
Component,Transform}`, `ComponentCache`, `ObjectPool`, `Properties`, `docs/feature_map/01-core-scene.md`, the lifecycle and
scene-graph tests). Most of what the request lists was **already there**; the work below extends it and fixes what the
measurements found. Plain guide for users: `docs/ecs_guide.md`. The separate data-only `ecs::World` (networking) is
`docs/ecs.md` and was not touched.

Written from our own reading of the code and our own measurements; no code, tests or text were taken from another engine
(CLAUDE.md 19a). Algorithms used: a never-reused serial number as a generation (handles), a remembered-slot hint with a
scan fallback (list removal), a uniform grid keyed by integer cell coordinates (radius queries), a stable sort by
(order value, creation number) (run order).

## Existing / Gap

| Area | Existing (before this change) | Gap found | What this change does |
|---|---|---|---|
| Object ids | `GameObject::InstanceId()` (never reused), `FromInstanceId`, `Guid()` saved with the scene | No handle type: scripts kept raw pointers. Destroyed objects stay readable as frozen shells, but only the newest 20,000 (`m_graveyard`); an older raw pointer then points at freed memory | `ObjectRef`, `ComponentRef<T>` (`Scene/Refs.hpp`) + `Component::ComponentId()` |
| Component ids | none (address only) | A freed component's address can be reused by a new one, so an address is not an identity | `ComponentId()` (never reused), assigned in `AddComponent` |
| Destroy during Update | `Scene::Destroy` queues, freed at the end of the frame (after LateUpdate); `DestroyImmediate` while updating only queues; components removed inside Update are nulled in the update list, not erased | Already safe; undocumented as one rule | Documented (`ecs_guide.md`), covered by the fuzz test (scripts destroying / adding / removing / reparenting inside Update, incl. removing themselves) |
| Add during Update | New components go to a pending list and join next frame (Awake then Start) | Already safe | Documented + fuzz |
| Lifecycle order | Awake, OnEnable, Start, Update, LateUpdate, FixedUpdate, OnDisable, OnDestroy; `ReleaseUnstarted` for never-started ones; OnEnable/OnDisable only alternate | Done | Left alone |
| Per-type lookup | `FindObjectsOfType<T>` cached per type, stamped with structure/component versions (`m_typeQueries`); `CachedComponents<T>` for physics | `FindObjectOfType<T>` still walked every object with `dynamic_cast` every call (1.2 ms for 10,000 objects) | Uses the cached list when fresh and builds it after a 256-object walk |
| `GetComponent<T>` | Skips the Transform, exact-type shortcut, `dynamic_cast` fallback; per object linear | A miss on an object with a few components costs a `dynamic_cast` per component (about 135 ns) | Not changed (see "Not done") |
| Update dispatch | One dense `m_active` list for the whole scene (not a walk of the object tree), sorted by `ExecutionOrder()` | Removing a component scanned the whole list: **removing every component of 10,000 objects took 327 ms** (about 33 us each, quadratic) | Each component remembers its slot in the list (checked before use); a scan only when the hint is wrong |
| Execution order | Virtual `Component::ExecutionOrder()` (IK = 100), stable sort, creation order within equals | A project could not change it without code | `ExecutionOrderSettings` ("Run early / Normal / Run late" per type), saved in `project.okayproj`, Project Settings > Run order |
| Dependencies | `RequireComponent<T,Req>`, `DisallowMultipleComponent<T>`, `GameObject::RequiredBy`, editor Add Component applies them | Done | Left alone |
| Tags / layers | `Tags`, `Layers`, `Scene::FindWithTag`, `TagIndex()` (O(1) per tag), `FindGameObjectsOnLayer`, `ProjectLists` | Done | Left alone |
| Static flag | `GameObject::isStatic`, saved (`static 1`), used by batching | Done | Left alone |
| Transform matrices | World matrix cache validated by comparing the local values (and the chain) on read (`Transform.cpp` WorldCache) | Not a dirty flag, but a read costs a compare per level, never a recompute when nothing moved | Left alone |
| Pooling | `ObjectPool` (prefab / template, Prewarm, reuse) | Components are not pooled (one allocation each) | Not changed |
| Scene queries | Find by name / path / tag / layer / guid, `FindNearest` | No "everything near a point" | `FindObjectsInRadius`, `SceneGrid` (`Scene/SceneGrid.hpp`) |
| Copy / duplicate | `Scene::Instantiate`, `Duplicate` (through the scene text: new guids, names kept, `FindNearest` resolves by-name references inside the copy) | By-GUID object references inside a copy are not remapped (unverified, see "Not done") | Not changed |
| Prefabs | `prefabSource`, `prefabOverrides`, variants, apply / revert (`PrefabTools`), branches `claude2/prefab-*` in flight | Owned by Claude 2 | Read only; nothing duplicated |
| Stats | `Scene::Stats()` (`SceneGraphStats`: counts, components by type), `EditorState::SceneLines` (one-line overlay) | No window; no time per type | Scene Stats window, `Scene::SetComponentTiming` / `LastFrameTimings` |
| Required-component error | Editor refuses removal and names the dependent | Done | Left alone |

## Measured (Release, this machine, which was shared and busy: compare the two columns, not the absolute numbers)

`tests/test_ecs_bench.cpp` (`ctest -R ecs_bench` runs it small; run `test_ecs_bench` directly for 10,000 objects, each with 3
components; change the size with `OKAY_ECS_BENCH_N`). Memory is the count of bytes asked from `operator new`.

BENCH_TABLE

## Correctness hazards looked for

| Hazard | Result |
|---|---|
| Component added or removed while a loop walks the update list | Safe: adds go to a pending list; removals null the slot, compacted at the start of the next frame |
| Object destroyed during Update | Safe: queued; freed after LateUpdate; its children go with it |
| Destroy twice / destroy a destroyed object | Ignored (queued once) |
| Dangling pointer after destroy | Raw pointers: valid for the next 20,000 destroys (frozen shell), after that freed memory. Handles: always null. |
| Component freed and its address reused | `ComponentRef` compares `ComponentId()`, not the address |
| `Components()` range-for while adding a component | Still a hazard by design (documented in the header); use `CopyComponentPointers` |
| Order of Start / Update / OnDestroy | Awake in the order added; Start after all Awakes of the batch; Update in list order; OnDestroy after the end-of-frame detach |
| Enable / disable order | OnDisable runs before OnDestroy; OnEnable before the first Start; never two in a row |
| Duplicate component rules | `DisallowMultipleComponent` returns the existing one; `RequireComponent` adds the missing sibling first |

## Tests

| Test | What it checks |
|---|---|
| `ecs_refs` | handles after remove / destroy / address reuse; run order (early / late, switching back, determinism, project file round trip); `FindObjectOfType` freshness after add / remove / destroy; removal keeps the update list right; per-type timing; radius query and grid agree with brute force |
| `ecs_fuzz` | 12 seeded runs of random create / destroy / reparent / add / remove / switch on-off, half of them with scripts doing destroy / add / remove / reparent inside Update. After every frame: parent and child agree, no cycle, ids and guids unique, components point at their object, per-type lists equal a plain walk, handles never resolve to a wrong or gone object. At the end: no leaked component (a live counter returns to zero). Each seed is run twice and the order-of-calls hash must match (deterministic). |
| `ecs_bench` | the benchmark above; asserts only the counts |

RESULTS_TABLE

## Not done (honest list)

- **Windows was not run.** Everything here was built and run on Linux only. The editor window and Project Settings section
  were only compiled for syntax (`g++ -fsyntax-only` against Dear ImGui), the editor was not built or opened.
- Script side: `ObjectRef` / `ComponentRef` are C++ types. The visual scripting and C# bindings do not expose them yet
  (VisualScript references are by guid text already).
- `GetComponent<T>` miss cost (a `dynamic_cast` per component) is unchanged. A per-type index inside each object would need
  the base-class queries (`GetComponent<Collider2D>`) to keep working, so it is not a quick change.
- Components are not pooled and objects have no small-buffer component list; the `ObjectPool` covers whole objects.
- Hot component types are not stored in per-type dense arrays: the update list is already one dense array for the scene, and
  moving components out of their own allocations would change every `Component*` in the engine.
- By-GUID references inside a duplicated object are not remapped to the copy (by-name ones are, nearest first).
- "Static" has no new behaviour; the flag already exists.
- A hierarchical dirty flag for transforms was not added: the existing cache validates by value and already skips recompute.
