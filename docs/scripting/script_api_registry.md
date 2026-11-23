# One table of engine functions for every script language

> **What this is:** how script functions reach the engine, and how to add one. A function is written once, in the
> shared table, and OkayScript, the typed language (okayscript-next / C#) and the visual-script Engine API nodes all
> use that one copy.
>
> **Status:** first batch moved (20 functions, branch `claude2/one-script-api`, 2026-11-22). The rest of each
> language's functions are still in their own lists; see "What is still in the old lists".
>
> **Last checked:** 2026-11-22.

## Why

Before this, each script language had its own hand-written list of engine functions:

- OkayScript: the builtin map in `engine/src/Scripting/OkayScriptVM.cpp` (`b["name"] = lambda`).
- The typed language: `engine/src/Script/EngineApi.cpp` (the visual-script Engine API nodes and the C# editor
  help are made from it too).

So every feature was bound twice, and the two copies drifted apart. Grok's duplicate audit on #422 (item 3) found
examples: `AddTorque` existed only in the typed language, the two save APIs disagreed, and `Input.GetKey` in the
typed language ignored "a text field has the keyboard" while OkayScript's `key()` respected it.

## How it works

The table is `engine/include/okay/Scripting/ScriptApiRegistry.hpp`. The engine's own entries are in
`engine/src/Scripting/ScriptApiCore.cpp`. Each entry has:

| Field | Meaning |
|---|---|
| `name`, `aliases` | the OkayScript names (`has_save`, also `save_exists`) |
| `typedType`, `typedName` | where the typed language finds it (`SaveGame` / `Exists`) |
| `kind` | `Function` (a static call, `Input.GetKey("w")`), `Method` (on a component, `rb.AddTorque(t)`), `StaticProperty` (`Time.time`) |
| `params` | name, type and, for an optional one, its default (`slot`, String, default `"save1"`) |
| `ret` | the result type: Void, Bool, Int, Float, String or Vector3 |
| `fn` | the one C++ function: `VsValue fn(GameObject* self, std::vector<VsValue>& args)` |
| `doc`, `pure`, `checkLiteral`, `typedMinParams`, `boolAsNumber` | the help text, "a value node in a visual script", a compile-time check of literal arguments (key names), the fewest arguments the typed language takes, and "OkayScript answers 1 / 0 instead of true / false" |

Each language turns its own values into the declared types before `fn` runs, so `fn` always gets exactly one
argument per parameter, of the declared kind.

- **OkayScript** (`OkayScriptVM.cpp`, `InstallSharedApis`) puts every entry into its builtin map first, under the
  name and the aliases. A Vector3 parameter takes a vector or three numbers (`add_force(0, 5, 0)`). A left-out
  optional argument gets its default. At the end of `RegisterBuiltins`, `CheckSharedApis` makes sure no
  hand-written function in the map replaced a shared one.
- **The typed language** (`EngineApi.cpp`, `AddSharedBindings`) adds every entry to its type, one overload per
  argument count from `typedMinParams` to all. The visual-script nodes come from the same type, so a graph gets the
  function too.

**Errors.** `fn` reports a problem by throwing `ScriptApiError("'Crate' has no Rigidbody ...")`. The typed language
stops with it as a runtime error at the line of the call (a visual script shows it on the node). OkayScript writes
it to the Console once as a warning and the call gives an empty answer (0, false, "" or a zero vector), because old
OkayScript functions failed quietly and existing scripts expect to keep running.

## How to add a function

1. Add one entry in `RegisterCoreScriptApis` in `engine/src/Scripting/ScriptApiCore.cpp` (or call
   `AddScriptApi(entry)` from an engine module). Use the helpers there: `Fn`, `Method`, `Prop`, `P`, `Opt`.

   ```cpp
   add(Fn("set_group_volume", "Audio", "SetGroupVolume", {P("group", T::String), P("volume", T::Float)}, T::Void,
       [](GameObject*, Args& a) { AudioMixer::groupVolume[GroupArg(a)] = Mathf::Clamp(a[1].AsFloat(), 0.0f, 1.0f); return V(); },
       "Set one group's volume, 0 to 1 (\"Music\", \"SFX\", \"UI\" or \"Voice\")."));
   ```

2. Do **not** also write it in `OkayScriptVM.cpp` or `EngineApi.cpp`. If you do, the duplicate check logs an error
   and `script_api_parity` fails.
3. If the entry has a parameter name the parity test has not seen, give it a sample value in `SampleFor` in
   `tests/test_script_api_parity.cpp`. That test calls every entry from both languages and checks they give the same
   answer and do the same thing.
4. Regenerate the Engine API page: `OKAY_WRITE_API_DOCS=1 ./tests/test_vs_types` from the build folder.

An OkayScript-only function from a module can still use the short form `AddScriptApi("name", fn)`: it gets the
arguments as the script passed them and is not added to the typed language.

## What moved (the first batch)

| OkayScript | Typed language | Notes |
|---|---|---|
| `key`, `key_down`, `key_up` | `Input.GetKey`, `GetKeyDown`, `GetKeyUp` | Both now use `Input::KeyFromName` names and the "game" key reads (a typing key is off while a text field has the keyboard). In the typed language `"up"` is now the up arrow (it was W), and more names work (`f5`, `pageup`, `alt`...). |
| `action`, `action_down`, `action_up`, `action_value` | `Input.GetButton`, `GetButtonDown`, `GetButtonUp`, `GetActionValue` | The typed language gains the optional player argument. An unknown action name is now a warning in OkayScript (it was a silent false). |
| `add_force`, `add_impulse`, `add_torque` | `Rigidbody.AddForce`, `AddImpulse`, `AddTorque` | `add_torque` is new in OkayScript. A 2D body works from both languages (a 2D body takes the Z part of a torque). |
| `save_game`, `load_game`, `has_save` (`save_exists`), `delete_save`, `save_label`, `play_time` | `SaveGame.Save`, `Load`, `Exists`, `Delete`, `Label`, `playTime` | OkayScript keeps its `"save1"` default slot and its 1 / 0 answers. `save_exists` was written twice in OkayScript (once for the key/value file store, once as `has_save`); the second always won, so the first was dead code and is gone. |
| `set_group_volume`, `group_volume` | `Audio.SetGroupVolume`, `GetGroupVolume` | An unknown group is now a warning in OkayScript (it was ignored). |
| `time`, `frame_count` | `Time.time`, `Time.frameCount` | |

## What is still in the old lists

Everything else. Roughly:

- **OkayScript only** (about 1,300 names in `OkayScriptVM.cpp`): movement and transform helpers (`move`, `pos_x`...),
  2D physics helpers, UI, tweens and sequences, networking, inventory, dialogue, the slot details (`save_slots`,
  `save_valid`, `save_date`...), the GameSaves functions (`game_save`, `game_load`, `game_continue`...), the key/value
  stores (`save` / `load`, `prefs_*`), maths, text, JSON, and the Unity-style component calls
  (`GetComponent<Rigidbody>().AddTorque(...)` inside OkayScript, which has its own force-mode handling).
- **Typed language only** (`EngineApi.cpp`): GameObject, Transform, Vector3, the rest of Rigidbody (velocity, mass,
  AddForceAtPosition), the UI component types, the other Input members (GetAxis, mouse buttons, local players,
  gestures), the rest of Time, Debug, the Audio master volume, Localization, Sequence, PostProcess / Fog / Weather /
  Wind, navigation, dialogue / story / quest, behaviour trees, splines, line and trail renderers, Animator, IK, Mathf.
- **Visual-script builtins** (`engine/src/VisualScript/VsBuiltins.cpp`) call OkayScript's map by name, so a moved
  function reaches a graph through both routes; that file has no copies of its own to move.

Many of these exist in both lists under different shapes (`axis_x()` / `Input.GetAxis("Horizontal")`, `velocity_x()`
/ `rb.velocity`). They move in later batches, one feature at a time, each with its old copies deleted in the same
change.
