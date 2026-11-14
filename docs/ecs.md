# ECS: iteration, command buffer and delta snapshots

Headers: `okay/ECS/World.hpp`, `Systems.hpp`, `NetWorld.hpp`, `SceneBridge.hpp`.

## Iterating safely

`World::each<T>` / `each<A, B>` fix the set of entities to visit when the loop
starts. Inside the callback you may:

- destroy or remove any entity or component, including ones already visited. An
  entity removed before its turn is not visited; nothing is visited twice or skipped.
  While a loop runs the pools leave holes instead of moving entries, and compact when
  the loop ends, so every component reference stays valid for the whole loop.
- add entities and components, including to the pool being iterated. They are not
  visited until the next pass.

## Command buffer

```cpp
w.each<Health>([&](ecs::Entity e, Health& h) {
    if (h.hp <= 0) {
        w.commands().destroy(e);
        Entity corpse = w.commands().create();       // id is reserved now, alive at flush
        w.commands().add<Corpse>(corpse, {});
    }
});
w.flush();                                           // apply, in recorded order
```

Commands: `create`, `destroy`, `add<T>`, `set<T>` (same as add, overwrites), `remove<T>`.
`flush()` returns the number of commands run and does nothing while an `each` loop is
active. Commands on a dead entity behave as the direct calls do (an add on a destroyed
entity stores nothing).

## Delta snapshots (NetWorld)

`snapshot()` / `apply()` (full state, legacy format) are unchanged. New API:

| Call | Side | Meaning |
| --- | --- | --- |
| `deltaSnapshot()` | server | Next tick; full packet if there is no acknowledged baseline, otherwise only components that changed (plus spawned / destroyed entities and removed components) since the acknowledged tick. |
| `ackPacket()` / `receiveAck()` | client / server | The client reports the last tick it applied. Tick 0 means "I have nothing": the next packet is full. |
| `applyPacket()` / `apply()` | client | Accepts legacy, FULL and DELTA packets. `applyPacket` returns `Applied`, `Ignored` (stale / duplicate), `NeedFull` (delta's baseline is not our tick: send `ackPacket()` again), `Malformed`. |

Wire format: legacy packets start with an entity count, which can never be `0xFFFFFFFF`,
so that value marks the versioned packets: `[u32 0xFFFFFFFF][u8 kind][u32 tick]...`
(kind 1 full, 2 delta with baseline tick and per-type changed / removed lists, 3 ack).

Decoding is hostile-input safe: a delta is fully validated (every count against the
bytes left, no trailing bytes, known type ids) before the world is touched, so a bad
packet changes nothing. The server keeps the last 64 sent states for diffing; the
acknowledged baseline is never dropped. A lost packet is repaired by the next delta
because it is always relative to the acknowledged baseline, not the previous packet.
Limit: one acknowledged baseline per `NetWorld`; for several clients use one sender
per client (or ack the slowest).
`NetWorld::apply` of a full snapshot is now O(entities) (it was O(n^2)).

## Entity ids

Ids are plain 32-bit numbers counted up from 1 and never reused, so an id kept in a variable or a save file can never name
a newer entity (`alive(id)` is the whole stale-handle check; no generation is needed). `entities()` lists the live ids in
ascending order; `each` visits a pool in the order its components were added (a removal moves the last one into the hole after
the loop), which is also the same on every run. After 2^32 - 1 creations the counter
wraps, skipping `Null` and every id that is still alive (`create()` used to return `Null` there). `ensure(id)` refuses `Null`
and the top id, which can come off the network.

## System scheduler

```cpp
ecs::SystemScheduler sched;
sched.add("input",    [](ecs::World& w, float dt) { ... });
sched.add("movement", [](ecs::World& w, float dt) { ... });
sched.run(world, dt);                         // every enabled system, in the order added, every tick
sched.setEnabled("movement", false);          // pause one
sched.remove("input");  sched.has("movement");  sched.names();   // run order
```

The order is exactly the order of `add` calls, so a tick is deterministic. A system may add, remove, clear, enable or
disable systems (itself included) while the tick runs: an added system joins at the end and first runs next tick; a removed
system that has not run yet is skipped, and one that removes itself finishes its call. This used to move the running
`std::function` out from under itself (a use after free for a small capture). Several systems may share a name; `setEnabled`
and `remove` act on all of them. Not thread safe.

## Scene bridge

`SceneBridge` mirrors entities that carry an `EcsTransform` to GameObjects made by your factory. It keeps instance ids, never
pointers, so an object the scene destroyed or freed is noticed on the next `sync` instead of being read:

- `respawnLost` (default true): a live entity whose GameObject is gone (a `Destroy` call elsewhere, a scene reload with
  `Clear()`) gets a new one from the factory on the next `sync`, snapped to the entity's pose. Set it false to leave such an
  entity without an object until it dies. To hide an entity on purpose, remove its `EcsTransform` (the object is destroyed) or
  deactivate the object instead of destroying it.
- A non-finite `EcsTransform` (a damaged snapshot) is skipped: the object keeps its last good pose.
- `interpRate` zero keeps the object where it is, negative or NaN snaps (it used to overshoot); the factor is computed with
  `expm1`, so a tiny rate times a tiny step is not rounded to nothing.
- To post an ECS or event change from a worker thread, hand it to the main thread (`JobSystem::Global().RunOnMainThread`)
  and touch the world and the bridge there. Neither is thread safe.

## What changed (core wave 6)

| Was | Now |
| --- | --- |
| `SystemScheduler::add` / `clear` from inside a system freed the closure that was running. | Changes are applied when the tick ends (see above). New: `remove`, `has`, `names`. |
| `World::create()` returned `Null` after the 32-bit counter wrapped. | The counter skips `Null` and live ids. |
| `World::entities()` came back in hash-table order (different between platforms and runs), so a loop over it and the entity list of a full `snapshot()` were not reproducible. | Ascending ids. |
| A `SceneBridge` entity whose GameObject was destroyed elsewhere stayed invisible for good (and its map entry stayed). | Made again (`respawnLost`). The lead that the bridge dereferences a freed `GameObject*` was already fixed (instance ids); a test now pins it. |
| NaN in an `EcsTransform` went straight into the Transform; a negative `interpRate` overshot. | Skipped / snapped. |

Looked at and found fine: `each` under mutation, the command buffer, holes in the pools, id reservation in the command
buffer, `ensure` for ids from the network. Still true: nothing in the engine, editor or player uses the ECS; it is opt-in.
