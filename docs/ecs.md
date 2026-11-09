# ECS: iteration, command buffer and delta snapshots

Headers: `okay/ECS/World.hpp`, `NetWorld.hpp`, `SceneBridge.hpp`.

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
