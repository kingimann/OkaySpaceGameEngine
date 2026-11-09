# Scene serializer notes

Format and loading rules are in `engine/include/okay/Scene/SceneSerializer.hpp`. This
page covers what an object round trip keeps.

- **Ids:** `guid` (see prefabs.md). Same id on reload, new id for a copy.
- **Prefab files:** `nestedprefab`, `variant`, `override`, `unset`, `remove` and `under` are prefab-file
  records (prefab_system.md). A build that does not know them skips them with a warning.
- **Sibling order:** objects load in file order, so a reordered family writes a
  `sibling <position>` record on each child. An untouched scene writes none.
- **Active and enabled:** `active` is saved for every object. Every component that is
  switched off gets a `compoff "<Type>" <n>` record (type name, and which of that type),
  applied after the object's components exist. A record for a component that is not
  there is ignored.
- **Component order:** components of an object that nobody arranged come back in the order the writer emits
  them (a fixed chain of `GetComponent<T>()` calls), as always. An object whose components were arranged on
  purpose (`GameObject::MoveComponent`, `GameObject::ReorderComponents`, or a loaded `comporder` record: see
  `GameObject::HasCustomComponentOrder()`) also writes `comporder "Type" "Type" ...`, the type names
  (`ComponentSerializerRegistry::TypeKey`) of its components after the Transform in order, and gets that order
  back after a load, a clone (`Instantiate`) and an undo snapshot. Optional and backward compatible: a file without the
  record loads exactly as before; an older build reads the line as an unknown record, skips it and warns; a name the
  object has no component for is ignored, and components the record does not name follow the named ones in their loaded
  order. The Nth name of a type means the object's Nth component of that type (two scripts keep their order).
  Only an explicit arrangement is recorded, not the order of `AddComponent` calls: that would need a table of the
  writer's emit rank per type (or a trial load of every object), and a record on every multi-component object would
  make each component add or remove edit one line that two branches both change, which the three-way merge cannot
  combine. A record written only for arranged objects keeps an untouched scene's text identical to before.
- **Runtime-only objects:** an object with `transient` set is never written, and neither is
  anything below it (a child saved without its parent would load as a loose root). The same holds for
  a clone made with `Instantiate` / `Duplicate`.
- **Numbers:** floats are written with the fewest digits that read back the same. The reader also
  takes `nan` and `inf`, so a file saved with one in it still opens. A `transform` record with a
  non-finite or all-zero part is reset to position 0, no rotation, scale 1 (a warning is reported).
- **Layer:** a `layer` outside 0-31 is stored modulo 32 (what the renderers and the collision matrix
  always used) and reported as a warning (`BadLayer` in `Validate`).
- **Optional trailing fields** (older files omit them; an older build reads the extra number as an
  unknown field, skips it and warns): `mover x y z [space]`, `spinner x y z [space]` (0 local, 1 world, 2 self; written only when
  not the default) and `lifetime seconds [elapsed]` (written only when above 0). `tweenanim ... "<id>" ["<clock>" "<curve>"
  [<decay> <speedBased> "<tag>"]]`: the clock and the ease as text are written when either is set, and the three after them
  (decay of a punch or shake, duration is a speed, group label) only when one of them is set, with `"" ""` standing in for
  an empty clock and curve; a Tween Animation that sets none writes the line it always did. A decay that is not a
  positive number is written as 0.
- **Duplicate components:** `AddComponent` twice is allowed at runtime and
  `GetComponent` returns the first. Scripts, visual scripts, action lists, tweens, sync vars,
  validators and state machines are saved per instance. Every other type, including
  two colliders of the same type, saves only its first instance. A text with two records of one of those types loads both and adds a
  warning (`object N has two 'X' components; only the first is saved`).

## Component serializer registry

`ComponentSerializerRegistry` (`okay/Scene/ComponentSerializer.hpp`) maps a record name
to a writer and a reader, so a new component does not add to the big if/else chain.
Registered components are written after the chain's, and the reader is tried only for a
field the chain does not know, so the format is unchanged. `trailrenderer` was moved
onto it as the first user. To add one:

```cpp
ComponentSerializerRegistry::Entry e;
e.name = "mything";                                   // one token, not a name the chain uses
e.write = [](const GameObject& go, std::ostream& out) { /* "  mything ...\n" or nothing */ };
e.read  = [](GameObject& go, std::istream& in) { /* rest of the line; AddComponent */ };
ComponentSerializerRegistry::Register(e);
```

Register from startup code or from `detail::RegisterBuiltinComponentSerializers` in
SceneSerializer.cpp. A static initializer in a file nothing else references can be
dropped by the linker, so prefer one of those two places. The record must stay on one
line.
