# Objects and components, in plain words

An **object** (GameObject) is an empty box with a name. A **component** is a piece you put in the box that gives it a
job: a sprite draws a picture, a Rigidbody makes it fall, your own script makes it do something. Every object always
has one **Transform** (where it is, how it is turned, how big it is, who its parent is). The scene owns all the objects.

This page is what to know to use them safely. The measurements and the list of what was and was not changed are in
`docs/ecs_upgrade.md`. The older "World" (a second, data-only system for networking) is in `docs/ecs.md`.

## Making and finding things

```cpp
GameObject* enemy = scene.CreateGameObject("Enemy");
Health* hp = enemy->AddComponent<Health>();      // adds one and returns it
Health* same = enemy->GetComponent<Health>();    // finds the first one on this object
enemy->GetOrAddComponent<Rigidbody2D>();          // find it, or add it if missing

scene.FindObjectOfType<Player>();                 // the first Player anywhere in the scene (fast when asked often)
scene.FindObjectsOfType<Enemy>();                 // all of them; kept up to date, a repeated call is a copy
scene.FindWithTag("Boss");                        // tags: see docs/layers_tags_cameras.md
FindObjectsInRadius(scene, point, 5.0f);          // everything within 5 units of a point, nearest first
```

For many questions per frame ("which of 5,000 enemies are near the player, for each of 200 bullets") build a `SceneGrid`
once: `SceneGrid grid; grid.Build(scene, 5.0f);` then `grid.Query(point, 5.0f)`. It is a snapshot: build it again after things
moved a lot (`grid.IsStale(scene)` tells you an object was created, destroyed or reparented, not that something moved).

## The life of a component

Awake (right after it is added) then, at the start of the next frame, OnEnable and Start, then every frame Update and
LateUpdate (and FixedUpdate in the physics step), OnDisable when it is switched off, and OnDestroy when it goes. Switching
an object or a component off and on again gives OnDisable then OnEnable, never two of the same in a row. A component that was
never started does not get OnDestroy (it gets `ReleaseUnstarted`).

## Destroying things safely

`scene.Destroy(object)` does not free the object at once. It is queued, and the scene frees it at the end of the frame, after
every Update and LateUpdate has run. So you can destroy things inside Update, inside another object's Update, even the object
that is running, and nothing breaks. After the frame ends the object is gone: its components are freed and its memory is
a frozen empty shell, so an old raw pointer reads "destroyed" (`IsDestroyed()`) rather than crashing. Still, **do not keep raw
pointers** for longer than one function. Keep a handle.

`RemoveComponent` is immediate (the component is gone when it returns), and is also safe to call from inside Update, even for
the component that is running; just do not touch `this` after it.

### Handles that turn into null: `ObjectRef` and `ComponentRef<T>`

```cpp
ObjectRef target = enemy;                 // from a GameObject*
ComponentRef<Health> hp = enemy->GetComponent<Health>();
...
if (GameObject* e = target.Get()) { ... }     // null once the enemy was destroyed
if (Health* h = hp.Get()) { h->Hit(3); }      // null once removed or destroyed
if (!target.Alive()) { /* gone, or Destroy was called this frame */ }
```

They hold two numbers that are never used again, so a new object that happens to land at the same memory address is never
mistaken for the old one. Copy them, put them in a list, use `ObjectRef` as a key in `std::unordered_map`. `Get()` costs a lookup
(a hash and a short scan), so use the pointer inside the function once you have checked it.

Between `Destroy` and the end of the frame the object is still there: `Get()` still returns it, `IsDestroyPending()` is true and
`Alive()` is false. The next frame `Get()` is null.

## Components that need each other

`RequireComponent<Rigidbody2D, Collider2D>()` (once, at startup) says "every Rigidbody2D needs a Collider2D". Adding the
Rigidbody2D adds the Collider2D first when it is missing, in code and in the editor's Add Component menu.
`DisallowMultipleComponent<Camera>()` says "one per object". The editor refuses to remove a component another one needs, and
tells you which one needs it (`GameObject::RequiredBy`).

## Who runs first: Run early / Normal / Run late

Most components do not care. When one must read input before the rest, or must see what everything else did, set it in
**Edit > Project Settings > Run order**: pick the component type and press "Run early" or "Run late" (or set it to Normal
again). In code: `ExecutionOrderSettings::Set("Lifetime", RunWhen::Late)`. The setting is saved in `project.okayproj`
(`run_order_<Type>=early|late`). Inside one group components run in the order they were added, so a frame is the same every time.
A component can also return a number from `ExecutionOrder()` (IK returns 100); the project setting wins over it.

## Seeing what is in the scene: Scene Stats

**View > Scene Stats window** shows how many objects and components there are, components by type, how many are waiting to
start or to be destroyed. Tick "Measure time per type", press Play, and the table gets the milliseconds each type spent in
Update last frame, with its share. It is off by default because it reads the clock around each call.

## Rules of thumb

- Do not put thousands of objects in the scene when one object with a list will do (a particle system, a tilemap).
- Ask the scene for things once per frame, not once per object per frame, or use a `SceneGrid`.
- Do not rely on the order objects were created in. Use Run order when order matters.
- Keep `ObjectRef` / `ComponentRef` instead of raw pointers anywhere longer than one function.
