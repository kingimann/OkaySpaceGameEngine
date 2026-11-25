# Script bindings (internal design and gap audit)

Internal record (rule 5). Owner goal, 2026-11-24: "Can we get okayscript to do what c++ can". This note covers
priority 2, full engine access. Branch `claude/okayscript-bind-1`, on top of `claude/okayscript-lang-1`.

## The gap

Counted by `python3 scripts/gen_script_bindings.py --audit`, which reads `engine/include/okay/**`:

| | Before | After |
|---|---|---|
| Component classes (anything derived from `Component`) | 303 found; 87 named by a hand-written builtin | 303 reachable with `GetComponent("Name")` |
| Their own public methods | 3785 | same |
| Methods a script value can carry (numbers, text, vectors, colours, enums, objects) | 2232; at most 335 called by a hand-written builtin | 2229 bound (2313 with inherited ones); 3 excluded because they crash on bad input |
| Registry properties (`PropertyRegistry`) | not reachable from OkayScript (only `tween_property`) | every registered property of every registered component, read and set |

"Before" is an upper bound: a class counts when a builtin names it in `GetComponent<...>`, a method when a builtin
calls a function of that name anywhere.

Still out of reach (not done):
- 793 methods take or return a type a script value cannot carry: references used as out-parameters, containers
  (`std::vector`, maps), callbacks (`std::function`), raw pointers other than `GameObject*` and component pointers,
  engine structs (`RaycastHit`, `NavPath`, ...).
- 758 are static methods or engine callbacks (`Update`, `OnCollisionEnter`, ...). Static engine systems (`Physics3D`,
  `Input`, `Audio`) are still reached only through the hand-written builtins.
- `GameObject`'s own methods are not generated; objects still go through the hand-written object members.
- Methods declared inside an `#if` block are skipped (they may not exist in every build).

## Design

One layer, `okay/Scripting/ScriptBindings.hpp`:

- **Classes and methods are generated.** `scripts/gen_script_bindings.py` reads our own headers (a small declaration
  reader written for this engine, not a C++ parser; anything it cannot read for sure is skipped and counted). For every
  component class it writes one entry per public method with bindable types into
  `engine/src/Scripting/ScriptBindingsGenerated.cpp`: name, argument kinds, how many arguments are required (the rest
  have C++ defaults) and a small function that converts the arguments and calls the method. Overloads are kept;
  inherited methods are included. `--check` (ctest `script_bindings_generated`) fails when the file is stale.
  `scripts/script_bindings_exclude.txt` drops a class or `Class.Method` that must not be bound.
- **Properties come from the property registry** (`PropertyRegistry`, `BuiltinProperties*.cpp`), the same table the
  Inspector, the scene files and Visual Script use, so a script write is validated and clamped the same way and fires
  the change hook. The conversion between property values and script values is one function,
  `PropertyToScript`, now shared with Visual Script's Get/Set Property nodes (`VsBuiltins.cpp`).
- **Handles, no pointers.** A component in a script is a `VsObject` with `comp` (the class key) and `index` (which
  component of that class on the object) next to the object's instance id. Every use resolves it again. A handle to a
  destroyed object or a removed component is null: `if (c)` is false, `c == null` is true, and reading or calling it
  is the error "the Light is null (its object was destroyed or the component was removed)". Never a crash.
- **Errors say what is wrong**: "Light.intensity wants a number, got text", "Camera.KickFov(): argument 1 must be a
  number, got text", "Camera.ZoomTo() takes 2 arguments, got 0", "Camera has no method 'Fly'", "Light has no
  property 'brightness'", "type has no option 'Laser'".
- **Wiring.** `EngineHooks` (`OkayScriptVM.cpp`) sends member reads, writes and calls on a handle to the layer.
  `GetComponent` / `other.GetComponent` return a handle for every class the hand-written builtins do not know. For the
  hand-written `"@T"` references (Rigidbody, Rigidbody2D, AudioSource, renderers, Transform, ...) the hand-written
  members stay first, with their names and behaviour, and anything they do not know falls through to the layer
  (`rb.InvMass()`, `rb.continuousCollision`).

## Side effects scripts cannot reach

Review of PR #1234 (B1, B2). Before this layer a game script could touch only named save slots, the prefs and the
`net.*` builtins (which have their own rules). The generator keeps it that way: `scripts/gen_script_bindings.py`
does not bind a method when any of these holds (`DENY_*` there; `--audit` counts them, 267 now):

1. its class is a network, platform or file service: `NetworkManager`, `NetworkObject`, `NetworkSync`,
   `NetworkPlayerSpawner`, `NetValidator`, `SteamManager`, `UIFilePicker`, `UIHotReload` (networking stays reachable
   only through `net.*`);
2. a text parameter's name names a place: path, file, filename, dir, folder, url, uri, address, host, link, root
   (`ImportHeightmap(path)`, `ScriptComponent.LoadFile`, `AudioSource.OpenStream`, ...); a `dir` that is a vector is
   a direction and stays;
3. a word of its name says it: file, clipboard, overlay, cloud, export, import, server, client, relay, socket,
   connect, url, download, upload, http, workshop, lobby;
4. it is on the explicit list: methods that write a file named earlier through a property or read one
   (`NavMeshSurface.Save`, `BuilderMode.SaveModel` / `SaveMap`, `SurvivalSave.Save` / `Load`,
   `Terrain.ExportHeightmap` / `ImportHeightmap`, `VehicleController.ToFile` / `LoadPresetText` / `SavePresetText`,
   `ScriptComponent.SetPath`, `VisualScriptComponent.SetPath`).

The generator also writes these four lists into `ScriptBindingsGenerated.cpp` (`GeneratedDenyRules()`), and
`scriptbind::IsDeniedClass` / `IsDeniedMethod` apply them (same word split as the generator, `NameWords`). A language
that reaches another table as well uses them, so that table is no way around the rules: Lua drops every typed-table
(`Script/EngineApi`) type and method they deny (`UIFilePicker`, `UIHotReload`, `Animator:SetCharacterModel(path)`,
`UIBreadcrumbs:SetPath(path)`, ...; test `lua_script` part 18). The C# API is generated from this table only, so it
has the same exclusions. Not done yet: the typed language (C#-style OkayScript) and the visual-script Engine API
nodes still reach those `EngineApi` members; they can call the same two functions.

The rules are on the safe side: some harmless methods are dropped too (`Transform.Find(path)` names a child, not a
file). Argument checks in the generated conversion (B2): an enum argument must be one of its values (the range read
from the enum's declaration; a bit-flag enum any combination), and a whole-number parameter whose name is count- or
index-like (count, size, index, length, num, slot, points, width, height, ...) must be -1 to 1048576; small integer
types must fit their type. Anything else is the call's error, "LineRenderer.SetPositionCount(): argument 1 must be -1
to 1048576, got 2000000000" (`ArgIntIn`, `ArgCount`, `ArgError` in `ScriptBindings.hpp`). Tests:
`script_bindings_rules` (`scripts/tests/test_gen_script_bindings.py`: no bound method breaks a rule) and
`script_bindings` (the denied methods are not reachable; the caps and enum checks give their errors).

## What was not replaced, and why

The hand-written component members were kept: none is exactly equivalent to its generated or registry form. Examples:
`rb.AddForce(x, y, z, mode)` takes three numbers and maps mode numbers its own way; `useGravity`, `isKinematic` and
`linearVelocity` are names that are not C++ members; `mass` refuses 0 with its own message; the hand-written setters
do not clamp, the registry does. Replacing them would change what existing scripts do. The one exact duplicate found,
Visual Script's property-to-value conversion, was merged into the layer.

## Known limits

- `==` between two handles compares their objects, so two different components of one object compare equal.
- A handle names "the n-th component of this class": removing an earlier one of the same class shifts it.
- `Destroy(component)` is ignored, as it was for the hand-written references.
- Runtime errors now carry their line in the engine too ("line 12: ..."): `Limits::runtimeErrorLines` is on and
  `OkayScriptVM.cpp` prefixes the line when it reports a runtime error. This applies to every runtime error, not only
  binding errors.
- `scripts/script_bindings_exclude.txt` lists methods that crashed when the parity test called them with ordinary
  values (an index they do not check): `CompoundCollider3D.PartFrame`, `CompoundCollider3D.PartSupport`,
  `ModelAnimator.SlotOfClip`. They stay C++ only until they check their input.
- The parity test runs every const (read-only) method on a fresh component of each class that can be added. The
  non-const methods are checked for reachability only, not run: they change state.
- A script function with the same name as a builtin (`shake`, `trauma`) cannot be called as an event:
  `Runtime::Call` tries builtins first. Found while writing the tests; not changed here.
- Windows: unverified (built and tested on Linux only).
