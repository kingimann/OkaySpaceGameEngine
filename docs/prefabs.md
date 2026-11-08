# Prefabs and overrides

A prefab is a `.okayprefab` file: one object and its children in the scene text
format. `SceneSerializer::InstantiateFromFile` makes an instance and remembers the
file in `GameObject::prefabSource` (instance root only).

## Overrides

An instance can differ from its prefab. Those differences are kept as data in
`GameObject::prefabOverrides`: a list of `{ object, line }`.

- `object` is the depth-first index of the object inside the prefab (0 is the root).
- `line` is one whole serialized record, for example `sprite 64 1 0 0 1 1 1` or
  `tag "limb"`. An override replaces the prefab's record of the same name (the Nth
  one of that name, for objects that hold several colliders or scripts). If the
  prefab has no such record the line is added, which is how an added component or a
  disabled one (`compoff`) is kept.

They are saved with the scene as `override <object> "<line>"` records on the root, and
`Instantiate` (duplicate, paste) copies them.

```cpp
SceneSerializer::CapturePrefabOverrides(*instance);            // fill prefabOverrides from the diff
// ... the prefab file is edited and saved ...
GameObject* fresh = SceneSerializer::ReapplyPrefab(scene, *instance);
```

`CapturePrefabOverrides` compares the instance with the prefab file, so call it while
the file still matches what the instance was made from; if the prefab was edited first,
the edit reads as an override. `ReapplyPrefab` uses the stored list only. It builds a new
instance from the current file with the overrides swapped in, gives it the old
instance's name, parent, sibling position and object ids (matched by depth-first
position and name), and queues the old one for destruction (an ordinary `Destroy`, gone
at the end of the frame). On any error it returns nullptr and the instance is untouched.
`InstantiatePrefab(scene, path, overrides)` is the same without an existing instance.

## Limits

- The unit of an override is a whole record, not one number inside it: overriding a
  sprite's colour keeps the instance's whole `sprite` record, size and glyph included.
- A component or child the instance lost is not tracked; reapplying brings it back.
- Objects added under an instance that are not in the prefab are not carried over by
  `ReapplyPrefab`.
- An override naming an object the prefab no longer has is dropped, with a warning.
- Nothing in the editor calls these yet; the editor's own Apply / Revert / Reload are
  unchanged.

## Object ids

Every object has a persistent id, `GameObject::Guid()` (32 hex characters), saved as a
`guid` record and found again with `Scene::FindByGuid`. It is the same after a save and
load. Instantiate, paste and merge give the copy a new id, and a scene file with no
`guid` records (older files) gets fresh ids when it loads. A repeated or malformed id in
a file is replaced, with a warning. Prefab files carry no ids.
