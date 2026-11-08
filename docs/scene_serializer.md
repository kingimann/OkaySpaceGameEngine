# Scene serializer notes

Format and loading rules are in `engine/include/okay/Scene/SceneSerializer.hpp`. This
page covers what an object round trip keeps.

- **Ids:** `guid` (see prefabs.md). Same id on reload, new id for a copy.
- **Sibling order:** objects load in file order, so a reordered family writes a
  `sibling <position>` record on each child. An untouched scene writes none.
- **Active and enabled:** `active` is saved for every object. Every component that is
  switched off gets a `compoff "<Type>" <n>` record (type name, and which of that type),
  applied after the object's components exist. A record for a component that is not
  there is ignored.
- **Component order** is not saved: components come back in the order the writer emits
  them, not the order they were added in.
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
