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
- **Component order:** an object's components come back in the order the loader meets their records, and the writer emits the
  records in a fixed order (a chain of `GetComponent<T>()` calls), so the order the components were added in used to be lost. Now an
  object whose stored order is not the one its records load as also writes `comporder "Type" "Type" ...`, the type names
  (`ComponentSerializerRegistry::TypeKey`) of its components after the Transform in order, and gets that order back after a load, a
  clone (`Instantiate`, `Duplicate`, paste) and an undo snapshot. The record is written when
  - the order was arranged on purpose (`GameObject::MoveComponent`, `GameObject::ReorderComponents`, or a loaded `comporder`
    record: `GameObject::HasCustomComponentOrder()`), always; or
  - the object has two or more components and loading its records would give another order than the one it has (plain `AddComponent`
    calls in an order the writer does not use).
  An object that already loads in its own order writes **no** record, so a scene whose objects are all in the writer's order is
  saved exactly as before; so is an object with one component or none. Optional and backward compatible: a file without the record
  loads exactly as before; an older build reads the line as an unknown record, skips it and warns; a name the object has no
  component for is ignored, and components the record does not name follow the named ones in their loaded order. The Nth name of a
  type means the object's Nth component of that type (two scripts keep their order).
  How "would load in another order" is found without a table: the object's own records are loaded once into a scratch scene and the
  order of the components that come out is compared with the stored one (components only one of the two has are ignored). The answer
  depends on which components there are and which records they wrote, not on their values, so it is kept per (component types,
  record names): a scene of ten thousand objects of three shapes loads three scratch objects, and a clone made with `Instantiate`
  costs a lookup. An object with one component, or none, is not checked at all. Cost for the other objects: their record text is
  built in a small buffer first (one extra copy of that text) and one hash lookup.
  Prefab tools (`PrefabTools::Diff`, overrides, apply and revert) treat `comporder` like `guid` and `sibling`: it is bookkeeping, never
  listed as a difference and never an override, because an instance that gained a component would otherwise show two differences for it.
  Merging: a `comporder` line that both sides of a three-way merge changed (each added a different component to an object whose
  order is not the writer's) is one conflicting single-line record, like any other.
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
  not the default) and `lifetime seconds [elapsed]` (written only when above 0).
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

## What changed: scene serializer, fifth pass (1.3)

Test: `component_order` (extended). Touched: `SceneSerializer.cpp` only in `WriteComponents` and the code beside it (about 90 lines:
the order check above and a forward declaration of the loader), one word in the bookkeeping-record lists of `SceneSerializer.cpp` and
`PrefabTools.cpp`, one in `SceneText.cpp`, and a generation counter in `ComponentSerializer.cpp` / `GameObject.cpp` that drops the
remembered answers when `RequireComponent` rules or serializers change. The text of a scene whose objects are all in the writer's order is
unchanged.

* The order of plain `AddComponent` calls is saved (see "Component order"). `component_order` loads every one of the 120 orders of
  five components (`Mover`, `Spinner`, `Lifetime`, `SpriteRenderer`, `Rigidbody2D`) and checks the order comes back, and that a
  record is written for 119 of them and not for the writer's own.
* `SceneText` (the tolerant parser, `Canonicalize`) now lists `comporder` among the records it knows (the fourth pass had left it out, so
  an object that wrote one was kept in file order instead of the writer's); prefab tools treat it as bookkeeping (see above).
* Not changed: components that are not saved at all (a plain C++ `Behaviour` nobody registered) and the second instance of a
  single-instance type are still dropped, so their place in the order cannot be saved either.

