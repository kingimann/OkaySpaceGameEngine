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

Sources are `engine/src/Scene/Properties.cpp`, `PropertyRegistry.cpp` and the built-in
registrations: `BuiltinProperties.cpp` (the core types), `BuiltinPropertiesGameplay.cpp`
(controllers, vehicles, navigation, networking) and `BuiltinPropertiesUI.cpp` (the UI widgets);
`BuiltinPropertiesShared.hpp` holds what they share.

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

// For an undo stack: the write reports the old value and what was really stored
PropertyWriteResult r = PropertyRegistry::WriteProperty(*go, "Light.intensity", PropertyValue::MakeFloat(99999.0f));
// r.ok, r.changed, r.path, r.oldValue (1), r.newValue (1000: clamped), r.error
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
Enum (`spot`, any case). An Enum written as "number name" must agree with itself:
`1 Point` is refused when option 1 is not Point (a stale text from before the options
were reordered would otherwise write the wrong value); a name that is no option any more
leaves the number to decide.

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

`SetProperty` returns only whether the write was accepted. `WriteProperty` and
`WritePropertyFromText` return a `PropertyWriteResult` instead: `ok`, `changed`, the whole
property's `path` ("Transform.position" for a `.y` path), its `oldValue`, the `newValue`
that was really stored (after clamping) and an `error`. An undo stack records that and,
to undo, writes `oldValue` back inside a `ScopedHookSuppression`; no hook is needed.

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
- Hooks run in the order they were added. A hook added while a change is announced first
  sees the next change; a hook removed while a change is announced (by itself or by an
  earlier hook) is not called for it if it has not run yet. Once `RemoveChangeHook`
  returns, no new call of that hook starts on any thread (one already running on another
  thread finishes).
- A hook may set properties, which announces those changes to every hook again. Delivery
  nested deeper than 32 levels on one thread is dropped (the writes still happen), so two
  hooks that keep undoing each other end instead of overflowing the stack.
- An exception thrown by a hook is swallowed. With no hook installed a write builds no
  `PropertyChange` at all, so a tween writing every frame pays nothing for the hook.

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

More of the builder:

- **A field of a struct member**: `AddFloat("agentRadius", &Surface::settings, &Settings::agentRadius)`
  (also `AddBool`, `AddInt`, `AddVec3`). Property names are flat: a path is
  `Component.property[.x]`, so a name may not contain `.`, `#` or white space.
- **A component that needs more than `AddComponent<C>()`** (a controller that also adds its parts):
  `.Adder([](GameObject& go) -> Component* { return PlayerController::Add(&go); })`.
- **A key binding** held in a `char` field: `.AddKey("jumpKey", &C::jumpKey)` (also for a `char` in a struct
  member). It is an Int property 0..255 holding the key code.
- **Not addable** (a Transform): `.NoAdd()`.
- A modifier called before the first property (`Tooltip` right after the constructor) is ignored
  instead of writing through an empty list, and a `Custom` with an empty getter adds nothing.
- `Register` ignores a type, and drops a property, whose name a path cannot address, and keeps the first
  of two properties with the same name.

### Adding a type to the built-in table

1. Describe it in the registration file that fits (`BuiltinProperties.cpp` core types,
   `BuiltinPropertiesGameplay.cpp` controllers, vehicles, navigation, networking,
   `BuiltinPropertiesUI.cpp` widgets), next to its neighbours: a `void RegisterXxx()` with a
   `PropertyBuilder<Xxx> b("Xxx", "recordname", "Category")`, one `Add*` per field **the scene
   record keeps** (the record name is the one `SceneSerializer` writes), and one line in the
   `Register...Properties()` function at the end of the file.
2. Give every number a range (`Range(min, max, step)`; refuse instead of clamp with
   `RejectOutOfRange()`), every enum its option names in enum order, and a tooltip where the
   name does not explain itself. Leave out what the file does not keep (runtime pointers,
   caches, lists such as a dropdown's options); a registered field that is not saved fails
   the cross-check below.
3. Key bindings (`char` fields holding a key code) use `.AddKey("sprintKey", &C::sprintKey)`: an Int
   0..255. Pass `true` as the last argument when the scene record writes the key as one raw
   character (`tdctrl`, `tdctrl2d`, `charctrl2d`, `ctmctrl`, `tpsctrl`): white space and `-` cannot
   be stored there and are refused.
4. Run `property_registry`. The tests visit every registered property: get, set the same value
   (no change), set a different value, text round trip, wrong type refused, the scene file
   round trip (all together and one at a time), the edges of every range and every enum option,
   and that each default is accepted unchanged. A property the file writes only together with
   another one goes into `Companions()` in the test (the record names the writer needs).
5. Add the type's name to `expected[]` in `TestRegistryBasics` and update the counts in this page.

## What is registered

1358 properties on 85 types (a number before a slash counts the authored ones; the rest are
runtime-only, read-only or derived and the scene file does not keep them). Every type also
has `enabled`.

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
| Mover, Spinner, Lifetime, CameraFollow | 3, 3, 2, 4 | Mover and Spinner have a `space` (Local, World or Self) |
| Canvas | 13 | |
| UIPanel | 21 | |
| PlayerController | 2 | `viewMode` (First, Third, Both): changing it adds and removes the parts below, with their settings |
| PlayerView | 11 | the first / third person toggle: key, pad button, blend, eye offset |
| FirstPersonController, ThirdPersonController | 73, 91 | movement, look, head bob (first person), camera orbit and spring arm (third), stance, lean, feel, slide, vault / mantle, dive, dodge |
| CharacterController2D, CharacterController3D | 23, 11 | the no-code character movement (top-down or platformer, jump feel, wall jump) |
| TopDownController, TopDownController2D, FreeRoamController, ClickToMoveController | 16, 26, 15, 30 | |
| ThirdPersonShooterController, TopDownShooterController, Platformer25DController | 47, 49, 58 | movement, camera, aim and fire, ammo, dash, depth layers, grab |
| VehicleController, VehicleController2D | 25, 11 | the suspension fields are saved only while `suspension` is on |
| Projectile3D, Spawner, MusicPlayer | 9, 13, 6 | the playlist `tracks` is a list, not a property |
| NavMeshAgent, NavMeshObstacle, NavMeshSurface | 15, 3, 14 | the surface's bake settings are flat (`agentRadius`, `maxSlope`, `cellSize` ...) |
| NetworkObject, NetworkSync, NetworkTransform, SyncVar, NetworkPlayerSpawner | 4, 6, 1, 3, 7 | `NetworkObject.netId` refuses a `/` |
| UIButton, TextRenderer, UIImage, UISlider, UIToggle, UIProgressBar, UIInputField, UIStepper, UIDropdown | 41, 32, 22, 24, 14, 22, 14, 16, 16 | position, size, anchor, colours, shape, shadow, border, behaviour switches |
| Crosshair, UIRadialProgress, UIRating, UIScrollView, UILayoutGroup, UIFrame, UIFlexItem, UIRawImage, UIKeybindField, UIDraggable | 25, 14, 10, 12, 12, 6, 5, 11, 18, 7 | the other UI widgets and layout parts that save plain fields (not the tabs, whose lists are not properties) |
| Collectible, DamageOnTouch, Teleporter, JumpPad, TriggerZone, FollowTarget2D | 7, 7, 5, 6, 7, 4 | the no-code gameplay components; `TriggerZone.action` is an enum |
| Destructible, Draggable, DropZone, Flashlight | 15, 10, 2, 7 | |
| Joint2D, Joint3D | 16, 14 | `mode` (Distance, Spring, Pin, Hinge); the motor spins either way |
| DayNightCycle, Water | 16, 16 | `DayNightCycle.time` is 0..24 hours; `Water.color` is red, green and blue (its alpha is `opacity`) |

How the new types are described, so a tool can rely on it:

- **Key bindings** are Int properties 0..255 holding the key code the controller uses (`16` is
  Shift, `17` Ctrl, `0` none; see `Input.hpp`). Five scene records (`tdctrl`, `tdctrl2d`, `charctrl2d`,
  `ctmctrl`, `tpsctrl`) write a key as one raw character, so a code that is white space or `-`
  is refused there (it could not be saved).
- **Settings in a struct** are flattened: `ThirdPersonController.dodgeKey`, `.dodgeImpulse` ...
  for `dodge.key` and `dodge.impulse`; `NavMeshSurface.agentRadius` for `settings.agentRadius`.
- **Integer enums** (`TopDownController2D.faceMode`, `TopDownShooterController.aimMode`,
  `TextRenderer.align`, `UIButton.transitionEase`) are Enum properties with option names; mouse and
  gamepad button numbers are Int properties with the range of valid buttons (`-1` = none where the
  component says so).
- Ranges are wide enough for every legitimate setup and tight enough to refuse nonsense: speeds
  and times are not negative, angles stay within a turn, the navigation bake settings use the limits the
  bake itself applies (cell size at least 0.05, slope at most 89 degrees), a UI colour is 0..1.
  `UIDropdown.value` may be -1 (nothing chosen).
- A property whose meaning depends on another (`UISlider.value` against `minValue` and `maxValue`)
  is held to its own range only; the widget clamps it when it draws.

Deliberately **not** properties of these types: mesh geometry (primitive,
edited vertices, modifier results: setting them would throw edits away),
`MeshRenderer.matcap` and `meshVariant`; the particle colour gradient and the
size and speed curves, and the edge and polygon collider point lists (lists of
keys or points, edited in tools); the loaded audio clip data and playback state;
`Camera.ignoreObject` and other runtime pointers; the physics integrator's
internal state; lists (`UIDropdown.options`, `MusicPlayer.tracks`, `NetworkManager.spawnablePrefabs`);
the localisation key of a widget (`loc`, its own `loc` record); and every field the scene record
does not keep: the first-person controller's pitch limits, `lockCursor`, `showBody`, `yaw` and `pitch`,
the swimming, fall-reset, step offset and slope settings of the first and third person and the 3D
character controllers, the transient state of a button or a dropdown (`open`, `focused`). Registering
one of those would break the rule that every authored property survives Save and Load, which the tests
check.

**Not registered yet:** scripts (`ScriptComponent` and `VisualScriptComponent` keep their
source and a free-form `fields` table, not named properties), `NetworkManager` and the anti-cheat
settings (record plus lists), the UI tabs, toast, tooltip, theme, context menu, list view, slot grid
and colour picker, the minimap (markers are lists), `PauseMenu`, `HoldPrompt`, `LoadingScreen`, `Stats`,
`ItemPickup`, inventory, crafting, dialogue, quest and survival components, terrain, voxel and spline
components, ragdoll, the tilemap, and the sequence and cinematic components. Animation and IK components
are left to the animation work. They can all register themselves with the builder.

## What the tests check

`tests/test_property_registry.cpp`:

- every value type's text round trip (including thousands of random float bit
  patterns), and a long list of bad inputs that must be refused without touching
  the output;
- validation: types, clamp and reject, NaN, quaternions, enums, control characters;
- paths, sub-fields, text input, multiple components of one type, error messages;
- the change hook (once, old and new, no-ops, sub-fields, derived properties,
  suppression, nesting, removal, a throwing hook), and the audit cases: a hook removed or
  added by another hook of the same change, hooks that write what they were told about
  (delivery stops at 32 levels), `WriteProperty` old and new values;
- the type table: unaddressable names, duplicate property names, replacement by name and by type,
  odd lookup names, registering and looking up from several threads, builder modifiers before the
  first property;
- text coercion per type: ints, floats, bools, vectors, colours, enums (a name that contradicts its
  number), strings, object references (no loops, no unknown objects), quaternions;
- snapshot, diff, revert and redo, copy (with and without options);
- **every registered property** on every type: get, set the same value (no
  change, no hook), set a different value, read it back, round trip through text,
  and refusal of a wrong type; every descriptor has an addressable name, a sane range, option
  names, and a default it accepts unchanged;
- **a cross-check against the scene serializer:** for each type, every authored
  property is set to a non-default value through the registry, the scene is
  serialized and loaded into a fresh scene, and each property must read back the
  same. It runs twice: once with all properties of a type set together, and once
  one property at a time. All 1349 stored properties pass. The skip list for
  "properties the serializer does not store" is empty: only read-only,
  runtime-only and derived properties are skipped, by their flags;
- **the edges through the scene file:** the lowest and highest value of every range, 0 and -1 for
  every integer, every enum option, both bool values and awkward strings (quotes, backslashes, a
  leading space, UTF-8, 3000 characters) are set, saved, loaded and read back (4246 values);
- the controllers (view mode adds and removes the parts, key codes, the dodge struct), navigation,
  networking and the UI widgets;
- a third-party component through the builder; concurrent first use of the registry; and four
  threads writing, adding and removing hooks and looking up at once.

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
  `normalStrength` needs a `normalMap`; `aoStrength` needs an `aoMap`; the
  vehicle's `rideHeight`, `springStrength`, `springDamping`, `suspensionTravel`, `wheelBase`,
  `trackWidth`, `bodyLean`, `maxTilt` and `tiltSmooth` need `suspension` on.
- **Alpha:** `Light.ambientColor`, the MeshRenderer colors `emissive`,
  `gradientTop`, `gradientBottom`, `rimColor`, `outlineColor`, `Flashlight.color` and the five
  `DayNightCycle` colours are saved as red, green and blue only; alpha reads back as 1. `Water.color`
  is the same, and in the component its alpha always follows `opacity`, so the property reads and
  writes red, green and blue and keeps alpha 1.
- **Edge and polygon colliders** do not keep `autoFit`, so it is not registered
  for them.
- **Raw-character keys:** the records `tdctrl`, `tdctrl2d`, `charctrl2d`, `ctmctrl` and `tpsctrl` write a
  key as one character, so Space and `-` cannot be bound there (`AddKey(..., true)` refuses them).

## What changed

Audit of the registry (each with a failing test first):

- A hook removed by an earlier hook of the same change still ran; the announcement now checks that a
  hook is still subscribed before each call.
- A hook that kept writing the property it was told about (or two hooks undoing each other)
  recursed until the stack ended; delivery nests at most 32 levels per thread.
- `Register` accepted names no path can reach (empty, `.`, `#`, white space) and kept duplicate
  property names; they are dropped now.
- Enum text like `1 Orthographic` took the number even when the name was another option; it is
  refused (a stale text would have written the wrong value).
- The builder wrote through `back()` of an empty list when a modifier came before the first
  property (a crash), and an empty `Custom` getter threw when the value was read.
- Name lookups use hash tables instead of three scans, and a write nobody listens to builds no
  `PropertyChange`.
- No bug was found in the clamping of ranges, the text round trips, the thread safety of the table,
  or the scene round trip: 4246 edge values and every enum option survive the scene file.

New: `PropertyRegistry::WriteProperty` and `WritePropertyFromText` (old and stored value),
`PropertyBuilder::Adder`, `AddKey` and the nested-field overloads, the controllers, navigation, networking and
UI widgets, no-code gameplay, joints and environment components listed above (59 new types, 1022 new
properties: 336 on 26 types became 1358 on 85), and the three registration files.
