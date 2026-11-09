# Properties (reflection)

The property registry lets code read, write and watch a component's fields **by
name**, without knowing the component's C++ type. It is the foundation that
per-property undo, changing values during Play, prefab per-field overrides,
generic inspectors and visual-script bindings are built on. Nothing in the
engine's components or the scene serializer changed to make it work: the
properties are described from outside, through the components' public fields.

Headers (under `engine/include/okay/Scene/`):

| Header | What it holds |
|---|---|
| `Properties.hpp` | `PropertyValue` (a tagged value with exact text form) and `PropertyDesc` (one property: type, hints, getter, setter, validation) |
| `PropertyRegistry.hpp` | `PropertyRegistry` (types, paths, change hook), snapshot / diff / apply / copy, `RegisterBuiltinProperties()` |
| `PropertyBuilder.hpp` | `PropertyBuilder<T>`: the short way to describe your own component |

Sources are `engine/src/Scene/Properties.cpp`, `PropertyRegistry.cpp` and
`BuiltinProperties.cpp` (the built-in registrations).

## In a few lines

```cpp
#include "okay/Scene/PropertyRegistry.hpp"
using namespace okay;

RegisterBuiltinProperties();                       // optional: lookups do it on first use

PropertyValue v = PropertyRegistry::GetProperty(*go, "Light.intensity");
PropertyRegistry::SetProperty(*go, "Light.intensity", PropertyValue::MakeFloat(2.5f));
PropertyRegistry::SetProperty(*go, "Transform.position.y", PropertyValue::MakeFloat(4.0f));
PropertyRegistry::SetPropertyFromText(*go, "Light.type", "Spot");            // an enum by name
PropertyRegistry::SetPropertyFromText(*go, "Light.color", "#FFAA33");

int hook = PropertyRegistry::AddChangeHook([](const PropertyChange& c) {
    // c.object, c.path ("Light.intensity"), c.oldValue, c.newValue
});
```

## Values: `PropertyValue`

A value is one of Bool, Int, Float, String, Vec2, Vec3, Vec4, Quat, Color, Enum
(a number plus the option's name), AssetPath (a file path) or ObjectRef (the id,
`GameObject::Guid()`, of another object; empty means none). It is plain data:
copy it, compare it, keep it. A wrong-typed read (`AsFloat()` on a Vec3) gives
the type's zero value; nothing throws.

`ToString()` and `PropertyValue::FromString(type, text, out)` convert to and
from text and **round trip exactly**:

| Type | Text |
|---|---|
| Bool | `true` / `false` (input also takes `1 0 yes no on off`, any case) |
| Int | decimal |
| Float | the shortest decimal that reads back to the same float (`0.1`, `100`, `1e-07`); `nan`, `inf`, `-inf` |
| String, AssetPath, ObjectRef | the text itself, unquoted |
| Vec2, Vec3, Vec4, Quat, Color | components separated by a space (`1 2.5 -3`); input also takes commas and one pair of brackets, and a Color takes `#RRGGBB` / `#RRGGBBAA` |
| Enum | the number, then the option name when there is one (`1 Point`) |

`FromString` returns false and leaves `out` alone for anything it cannot read in
full: empty text, extra text, a bad or out-of-range number, the wrong number of
components. It never throws. `PropertyDesc::ParseText` adds option names for an
Enum (`spot`, any case).

## Descriptors: `PropertyDesc`

Each property has a name, a type, an inspector `group`, a `tooltip`, optional
`rangeMin` / `rangeMax` / `step`, `enumOptions` (the value of option *i* is *i*),
an `assetKind` hint for AssetPath (`image`, `audio`, `model`), `flags`, a
`defaultValue` (taken from a freshly made component) and a getter and setter.

`Get` reads. `Set` checks first and only then writes, so a refused value leaves
the component untouched:

- the type must match exactly (a Float is not accepted for an Int);
- floats must be finite (no NaN or infinity);
- numbers outside the range are **clamped**, or **refused** when
  `rejectOutOfRange` is set; a vector or color is checked per component;
- a Quat is normalised (a zero quaternion is refused; a unit one is stored as it
  is);
- an Enum must name an option;
- String and AssetPath values may not hold line breaks or other control
  characters unless `multiline` is set (a record in the scene file is one line);
- an ObjectRef is empty or a valid id;
- a `ReadOnly` property, or a setter that refuses (a tag the project does not
  allow, a parent that would make a loop), returns false.

Flags: `ReadOnly`, `HideInInspector`, `RuntimeOnly` (live state such as a
velocity: not saved, left out of snapshots unless asked), `Derived` (computed
from other properties, e.g. `Transform.eulerAngles`: settable, left out of
snapshots and copies, reads back within float error) and `Structural` (changes
the hierarchy: `Transform.parent`; copies skip it unless asked).

## The registry and paths

`PropertyRegistry` maps a component type to its properties. A type is found by
its C++ name as the serializer's `ComponentSerializerRegistry::TypeKey` writes it
(`Light`, `Rigidbody2D`), by its scene-file record name (`light`, `rigidbody2d`)
or by `std::type_index`. Matching a component on an object is by **exact type**:
a subclass of `Light` is not treated as a `Light` until it registers itself.

A path is `Component.property`:

| Path | Meaning |
|---|---|
| `Light.intensity` | the Light's intensity |
| `Transform.position.x` | one float of a vector (`.x .y .z .w`, or `.r .g .b .a` on a Color) |
| `Camera#1.depth` | the second Camera on the object (`#0`, the default, is the first) |
| `GameObject.name` | the object's own fields: `name`, `active`, `tag`, `layer`, `isStatic`, `editorLocked`, `uiDrawOrder` |

Component names and property names match in any case. `Resolve`, `TryGetProperty`
and `SetProperty` take an optional `std::string* error` that says why a path or
value was refused.

Every component also gets an `enabled` property (the scene file keeps it as a
`compoff` record). `MeshRenderer` has its own on/off flag as well, exposed as
`meshEnabled`.

## The change hook

```cpp
int id = PropertyRegistry::AddChangeHook([](const PropertyChange& c) { ... });
PropertyRegistry::RemoveChangeHook(id);
```

A `PropertyChange` carries `object`, `component`, `componentInfo`, `property`,
`path`, `componentIndex`, `oldValue` and `newValue`. The rules:

- It fires **once** for each successful write that **changed** the stored value.
  Setting the value a property already has, a clamp that lands on the current
  value, and any refused write fire nothing.
- `oldValue` and `newValue` are the **whole** property (a Vec3, not the one float
  a `.y` path changed) and `newValue` is what was actually stored (after
  clamping), so a subscriber can undo by setting `oldValue` back.
- Writing a `Derived` property announces the stored properties it changed
  instead: setting `Transform.eulerAngles` announces `Transform.rotation`.
- `SetProperty`, `SetComponentProperty`, `ApplyPropertySnapshot`, `ApplyPropertyDiff` and
  `CopyProperties` all fire it.
- `ScopedHookSuppression` silences the hook on the current thread while it lives.
  An undo system wraps its own writes in one so applying a step does not record
  another step.
- A hook may add or remove hooks and may set properties (which fires the hooks
  again). An exception thrown by a hook is swallowed.

## Snapshot, diff, apply, copy

```cpp
PropertySnapshot before = SnapshotProperties(*go);
// ... edits ...
PropertySnapshot after  = SnapshotProperties(*go);
std::vector<PropertyDiff> changes = DiffProperties(before, after);   // path, before, after, kind
ApplyPropertyDiff(*go, changes, /*forward=*/false);                          // revert exactly those properties
ApplyPropertySnapshot(*go, before);                                          // or restore everything
CopyProperties(*from, *to);                                          // values, component by component
```

A snapshot is a flat list of `{component, index, property, value}` in a stable
order (the GameObject fields, then each registered component in stored order).
By default it holds authored data only: read-only, runtime-only and derived
properties are left out (`PropertySnapshotOptions` brings them back). A diff entry is
`Changed`, `Added` or `Removed` (the last two when one side has a component the
other lacks); `ApplyPropertyDiff` applies the `Changed` ones. `PropertyCopyOptions` can skip the
Transform, include the object fields or the hierarchy, add components the target
lacks, or limit the copy to named components.

How the other systems are meant to use it:

- **Per-property undo (area 22):** subscribe to the hook, push `{object guid,
  path, oldValue, newValue}`; to undo, set `oldValue` inside a
  `ScopedHookSuppression`.
- **Live editing during Play (area 34):** snapshot when Play starts, diff when it
  stops, then keep (do nothing) or revert (`ApplyPropertyDiff(..., false)`) per property.
  Runtime-only properties (velocity) can be edited live and are never kept.
- **Prefab per-field overrides (area 21):** diff the instance against the prefab's
  snapshot and store `{path, value.ToString()}`; re-apply with
  `SetPropertyFromText`.
- **Generic inspectors and visual-script bindings:** enumerate
  `PropertyRegistry::All()`, build widgets from `type`, `group`, `tooltip`, the
  range and `enumOptions`, and write through `SetProperty`.

## Describing your own component

```cpp
struct Torch : Component {
    float brightness = 1.0f;
    enum class Mode { Off, Steady, Flicker } mode = Mode::Steady;
    Color tint;
};

PropertyBuilder<Torch>("Torch", "torch", "Gameplay")
    .AddFloat("brightness", &Torch::brightness).Range(0, 10, 0.1).Tooltip("Light output")
    .AddEnum("mode", &Torch::mode, {"Off", "Steady", "Flicker"})
    .AddColor("tint", &Torch::tint).Group("Look")
    .Commit();
```

Call it from startup code (not from a static initializer in a file nothing else
references: the linker can drop that). `Add*` take a pointer to a public field
(it may live in a base class); the modifiers (`Group`, `Tooltip`, `Range`,
`RejectOutOfRange`, `ReadOnly`, `Hidden`, `RuntimeOnly`, `Derived`, `Structural`,
`Multiline`, `Default`) apply to the property added last, and `Section("name")`
sets the group for the ones that follow. An integer field starts with the limits
of its own type. For a value that is not one plain field use
`Custom(name, type, getter, setter)` or `CustomEnum`. `Commit()` also reads the
defaults from a freshly made component and registers the type (registering the
same name again replaces it). `RegisterBuiltinProperties()` does exactly this for
the built-ins and is safe to call from several threads: the work runs once.

## What is registered

336 properties on 26 types (the number after the slash counts the runtime-only,
read-only and derived ones, which the scene file does not keep):

| Type | Properties | Notes |
|---|---|---|
| GameObject | 7 | name, active, tag, layer, isStatic, editorLocked, uiDrawOrder |
| Transform | 4 / 6 | position, rotation, scale, parent; derived: eulerAngles, worldPosition |
| Camera | 29 | projection, lens, physical camera, clear, viewport, framing, shake |
| Light | 17 | type, color, intensity, range, spot, temperature, shadows, flicker |
| SpriteRenderer | 12 | |
| MeshRenderer | 38 | surface settings: shader, material, textures, rim, outline, shadows |
| Rigidbody2D | 11 / 14 | runtime: velocity, angularVelocity, sleeping |
| Rigidbody3D | 15 / 19 | runtime: velocity, angularVelocity, sleeping, groundedOnTerrain |
| BoxCollider2D, CircleCollider2D, CapsuleCollider2D | 8, 8, 9 | |
| EdgeCollider2D, PolygonCollider2D | 8, 6 | |
| BoxCollider3D, SphereCollider3D, CapsuleCollider3D, CylinderCollider3D, MeshCollider3D | 8, 8, 10, 10, 8 | |
| AudioSource | 13 | |
| ParticleSystem | 52 | everything except the gradient and curves |
| Mover, Spinner, Lifetime, CameraFollow | 3, 3, 2, 4 | every component also has `enabled`; Mover and Spinner have a `space` (Local, World or Self) |
| Canvas | 13 | |
| UIPanel | 21 | |

Deliberately **not** properties of these types: mesh geometry (primitive,
edited vertices, modifier results: setting them would throw edits away),
`MeshRenderer.matcap` and `meshVariant`; the particle colour gradient and the
size and speed curves, and the edge and polygon collider point lists (lists of
keys or points, edited in tools); the loaded audio clip data and playback state;
`Camera.ignoreObject` and other runtime pointers; the physics integrator's
internal state.

**Not registered yet:** the other component types (controllers, the other UI
widgets, scripts, navigation, networking ...). Animation and IK components are
left to the animation work. They can register themselves with the builder.

## What the tests check

`tests/test_property_registry.cpp`:

- every value type's text round trip (including thousands of random float bit
  patterns), and a long list of bad inputs that must be refused without touching
  the output;
- validation: types, clamp and reject, NaN, quaternions, enums, control characters;
- paths, sub-fields, text input, multiple components of one type, error messages;
- the change hook (once, old and new, no-ops, sub-fields, derived properties,
  suppression, nesting, removal, a throwing hook);
- snapshot, diff, revert and redo, copy (with and without options);
- **every registered property** on every type: get, set the same value (no
  change, no hook), set a different value, read it back, round trip through text,
  and refusal of a wrong type;
- **a cross-check against the scene serializer:** for each type, every authored
  property is set to a non-default value through the registry, the scene is
  serialized and loaded into a fresh scene, and each property must read back the
  same. It runs twice: once with all properties of a type set together, and once
  one property at a time. All 325 stored properties pass. The skip list for
  "properties the serializer does not store" is empty: only read-only,
  runtime-only and derived properties are skipped, by their flags;
- a third-party component through the builder; and concurrent first use of the
  registry from several threads.

What the cross-check showed about the file format (kept here so nobody has to
rediscover it):

- **Precision:** the scene writer prints a float with the fewest digits that read
  back as the same value, so a float such as `1234567.0` or `0.123456789` survives a
  save unchanged (older builds printed six digits and rounded; old files still load).
  The cross-check test still allows a relative error of 1e-5; text from `ToString()`
  itself is exact.
- **Conditional records:** some properties are written only together with
  another. Alone they are lost on save: `toonBands`, `gradientTop`,
  `gradientBottom` need a non-Standard `shader`; `rimPower`, `rimColor` need
  `rimStrength` above 0; `outlineWidth`, `outlineColor` need `outline` on;
  `normalStrength` needs a `normalMap`; `aoStrength` needs an `aoMap`.
- **Alpha:** `Light.ambientColor` and the MeshRenderer colors `emissive`,
  `gradientTop`, `gradientBottom`, `rimColor`, `outlineColor` are saved as red,
  green and blue only; alpha reads back as 1.
- **Edge and polygon colliders** do not keep `autoFit`, so it is not registered
  for them.
