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
  not the default) and `lifetime seconds [elapsed]` (written only when above 0). `tweenanim ... "<id>" ["<clock>" "<curve>"
  [<decay> <speedBased> "<tag>"]]`: the clock and the ease as text are written when either is set, and the three after them
  (decay of a punch or shake, duration is a speed, group label) only when one of them is set, with `"" ""` standing in for
  an empty clock and curve; a Tween Animation that sets none writes the line it always did. A decay that is not a
  positive number is written as 0. `spawner ... "<prefab>" [1]` (a trailing `1` only when `usePool` is on) and
  `loadingscreen ... "<subtitle>" "<logo>" <asyncLoad> <objectsPerFrame>` (a record without the two numbers loads with
  background loading off, so a scene saved before it keeps loading in one go).
- **Duplicate components:** `AddComponent` twice is allowed at runtime and
  `GetComponent` returns the first. Scripts, visual scripts, action lists, tweens, sync vars,
  validators, state machines, every 2D and 3D collider (a compound collider is several shapes on one
  object), audio sources, joints (`Joint2D`, `Joint3D`: one per neighbour), spawners, lights, particle systems
  (each with its own `particlesfx` / `particleswind` lines, which belong to the `particles` line before them),
  trigger zones, UI animations, sequence players and quest triggers are saved per instance, in order, with their
  own values and their own `compoff` switch. Every other type (for example `SpriteRenderer`, `TextRenderer`,
  `Camera`, `Mover`) saves only its first instance. A text with two records of one of those
  other types loads both and adds a warning (`object N has two 'X' components; only the first is saved`).
  The per-instance types are exactly those `AllowsMultiple` lists in `SceneSerializer.cpp`; to add one, make
  its writer loop over `GetComponents<T>()` (the reader already adds one component per record) and add it
  there. The trigger filter record (`trigfilter2d` / `trigfilter3d`) is still one per object and applies to
  every collider on it.
- **Key bindings in one-token records:** the records `tdctrl`, `tdctrl2d`, `charctrl2d`, `ctmctrl` and `tpsctrl` name a key
  (sprint, run, dash, fire, camera turn) as one token. Older builds wrote the raw character, which broke the record for
  space, tab and Enter (the later fields shifted by one) and read `-` ("no key") back as no key. Now a printable character
  other than `-` is written as itself, as before (so a file of letters is unchanged and older builds read it), no key is
  `-`, and every other code is written by number, `#<0..255>`: space is `#32`, `-` is `#45`, Shift is `#16`, F1 is `#128`.
  The reader takes every old file as it did (a one-byte token is the character, raw control and high bytes included) and
  the new form besides. `frctrl`, `charctrl3d` and the key=value records always wrote numbers.
- **Text that spans lines:** a quoted value may contain line breaks (the quote and the backslash are the only characters
  escaped). The key=value records read a line that ends inside a quote on to the next one until the quote closes, and so
  does the skipping of an unknown record, so a quest description typed over several lines no longer cuts its record in two.
  A quote that never closes (a damaged file) takes only its own line, as before.
- **Records this build does not know:** a field of an object (a component or a setting a newer build wrote) and a record at
  the top of the file are still skipped with a warning, and now also kept as text (`GameObject::unknownRecords`,
  `Scene::unknownRecords`) and written back by the next save, so opening a file with an older build and saving it does not
  lose them. A copy of the object (`Instantiate`, `Duplicate`, an undo snapshot) carries them. A top-level record is kept
  only by a full load and only when it starts with a record-like name; at most 256 records (65536 bytes each) are kept per
  object and per scene. The warning, and `SceneSerializer::Validate`, still name them.
- **Asset paths:** `CollectAssetPaths` lists the files of every instance of a component that can hold several (all the audio
  sources, particle systems and sequence players of an object) and the prefab of each Spawner, so Build Game copies them.
- **Saving a file:** `SaveToFile(scene, path, &error)` writes `<path>.savetmp`, checks it, and renames it over the
  target; the target is never moved or removed first. It keeps one rolling `<path>.bak` (a copy of the previous
  file), refuses a read-only target or a missing folder with a message, and leaves the old file untouched when
  anything fails.

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

## What changed: scene serializer, wave 7 (1.5)

Test: `scene_serializer_audit` (key codes, per-instance types, asset paths, floats, names, line breaks, unknown records,
merge collisions). Touched: `SceneSerializer.cpp` in the writers and readers of the five key records, of the types listed
above, of `spawner` and `loadingscreen`, `KvIn`, the unknown-record branches, `CollectAssetPaths` and the places named
below; not the validators at the end of the file.

* **Key bindings** are written so every code reads back (see "Key bindings in one-token records"). Fixed: Space, Tab, Enter
  and `-` as a sprint, run, dash, fire or camera key shifted every later field of the record or came back as no key.
* **More types are saved per instance** (joints, spawners, lights, particle systems, trigger zones, UI animations, sequence
  players, quest triggers). Older builds read a file with two records of such a type as before: both load, with the warning
  "has two ... components; only the first is saved". A second particle system written for an older build overwrites the
  first one's gradient and curves, because those lines attach to the first system there.
* **A line break inside a quoted value of a key=value record** no longer cuts the record in two: a quest description with a
  second line used to lose its objectives and rewards and left a stray line behind.
* **Unknown records are kept** (see above).
* **Asset paths** of every audio source, particle system, sequence player and the Spawner prefabs are collected (only the
  first instance counted; the Spawner prefab was missed).
* Checked and fine: every finite float (denormals, `-0`, the largest and smallest) reads back bit for bit; names, tags and
  strings with quotes, backslashes, UTF-8, tabs and line breaks; merging a scene into itself keeps every id unique and each
  child under its own parent; two objects with one `guid` load with a warning and a new id for the second.
* Not changed: a second `SpriteRenderer`, `TextRenderer` or `Camera` on one object still saves only the first; the scene text
  is still parsed twice by a full load (a throwaway pass first, so a bad file leaves the scene alone).
