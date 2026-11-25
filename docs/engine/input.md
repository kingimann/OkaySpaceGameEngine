# Input

> **What this is:** Where the input code is, what is missing, and the input audit.
>
> **Status:** combined on 2026-11-20 from 2 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
>
> **Last checked:** 2026-11-23, after the input systems were combined into one (section below).

## Contents

- [One input system](#one-input-system) (2026-11-23)
- [Input wiring](#input-wiring) (2026-11-24)
- [Input (internal record)](#input-internal-record) (was `docs/input.md`)
- [Input audit](#input-audit) (was `docs/input_audit.md`)

## One input system

The owner, 2026-11-23: "you have 3 different input systems, combine it so you have one". The feature map listed them as
**Input** (the devices), **Input Actions** (the typed `ActionSet`, the `.okayinput` asset, local players) and **Simple Input
Actions** (the older `InputActions` table the built-in controllers read). PR #1139 had linked the last two for 11 actions;
now they are one.

**The one system.**
- Devices: `Input` (keyboard, mouse, gamepads, touch). Unchanged; it was already the only device state.
- Actions: one table, `InputRuntime::Actions()`, an `ActionSet`. It is the asset in use (the shipped default, a project's
  `Assets/input.okayinput`, or `ia_load`) plus the actions code defines with `InputActions::Define` / `DefineIn`. A built-in
  default (Jump, Move, Dig, Prone, the Vehicle keys ...) only adds what the asset lacks; a game's own `Define` replaces the
  asset's bindings for that name. Every action is in it once.
- One evaluation: `ActionSet::Update`. It took over what only the old table did: input contexts are action maps (a map can
  consume its inputs or block every map below it: `PushMap` / `PopMap`), a held chord keeps its keys from single keys, an
  action can add its bindings up (`sum`, asset option `sum=1`), the player's response (dead zone, curve, sensitivity, invert)
  runs as processors, per-controller reads (`perPlayer`), on-screen contributions, hold-to-toggle, and press timing for
  buffering, double tap and hold to confirm. No allocation per frame (`input_bench`).
- One rebinding store: the table's overrides, saved in `inputov.player0` (`ov` lines; a list whose shape changed is one
  `ovlist` line; lines for actions of another asset are kept as they were). For every action the name-based front can show,
  the same store is also written in the old per-action form `input.<action>`, so earlier saves and builds still read it.
  On load an action with an `input.<action>` pref takes it; the rest come from `inputov.player0`.
- Local players: player 0's typed set has the table's bindings (copied whenever the table changes), so a rebind of either is
  the same rebind. Players 1+ keep their own rebinding (`inputov.player<N>`), as before. Contexts pushed with
  `InputActions::PushContext` reach every player's set too (the pause menu's Block stops the typed readers).

**Old names, all kept, and where they go now.**

| Old name | Now |
|---|---|
| `InputActions::Get / Down / Up / Value / Vector / Buffered / DoubleTap / HoldReached ...` | read the table's action |
| `InputActions::Define / DefineIn / SetContext / SetThreshold / SetPerPlayer / SetResponse` | change the table (and every player's set) |
| `InputActions::Rebind / Reset / ImportBindings / LoadProfile / ApplyPreset`, `RemapBind` (old Controls screen) | the table's rebinding, saved at once |
| `InputActions::All / Find` | a read-only view of the table (a binding it cannot show is `Kind::None`) |
| `InputActions::PushContext / PopContext / KeyConsumed` | the table's maps (`PushMap`, `PopMap`, `Consumed`) |
| `InputActions::SetLegacy`, `OKAY_OLD_INPUT=1` | kept as names only: there is one evaluation |
| script `action*`, `rebind`, `reset_bindings`, `input_preset`, engine API `Input.GetButton*` | `InputActions`, so the table |
| script `ia_*`, engine API `Input.Player*`, "UI Keybind Field" with `ia:<action>@<player>`, Controls screen (`UIGallery::AddControlsScreen`) | player sets; player 0 = the table's bindings, `ia_save` = the one store |
| Prefs `input.<action>`, `inputov.player<N>`, `inputresp.<action>`, `inputprofile.<name>` | read as before; `input.<action>` and `inputov.player0` are written together from the one store |
| `.okayinput` files | load unchanged; new optional action option `sum=1` |

**Deleted.** The old table's own action list, binding store, context stack, evaluation, chord and claim code and its second
"legacy" evaluation (all in `engine/src/Input/InputActions.cpp`, now a front over the table), and PR #1139's linking code
(`InputActions::SyncFrom`, `LinkJoined`, `SaveWithStore`, `InputRuntime::LinkedSet`, the push and pull between two stores).
`VoxelDigger::ActionRebound` forwards to `InputActions::Rebound` instead of a copy of it. The kind names of bindings moved to
`ActionSet` (`BindingKindName`, `ParseBindingKind`) from `InputAsset.cpp`.

**What changed for players and games (on purpose).**
- The shipped asset's Move and Look sticks lost their second dead zone (`deadzone(0.18,1)`; `Input` already applies the
  stick dead zone) and Look's `curve(2)`, and Move and Look add their bindings up (`sum=1`), as the built-in controllers
  always read them. Projects with their own `input.okayinput` keep what they wrote.
- Pushing the "UI" context also turns on the asset's UI map: Navigate, Submit and Cancel then take W A S D, Space, Enter and
  Escape from gameplay. A menu that should take only its own keys uses another context name.
- A typed rebind is the rebind: what `ia_rebind` allows (a lone key on Look) every reader gets. `InputActions::Rebind` still
  refuses a lone key for a direction action.
- One save saves everything: an `InputActions` rebind (saved at once) also saves an earlier unsaved `ia_rebind` of player 0.

**Not done.** `drivectl::ControlScheme` (`Components/VehicleControlScheme.hpp`, vehicle round 2) is a separate binding,
rebinding and save format for vehicles; nothing uses it outside its test (the Vehicle Controller reads the "Vehicle"
actions of the one table). It should become Vehicle actions in the table before it is wired in. Build Mode's
`BuildBindings` and the controllers' own key fields are per-component key settings that follow actions where a
`ControlKey` call exists (unchanged). Not run on Windows, in a real editor window or with a real controller.

Written for this engine from its own code (no other project's code or text).

## Input wiring

2026-11-24, branch `claude/input-wiring-1`, test `input_wiring` (`tests/test_input_wiring.cpp`, one section per item). Seven
pieces existed but were not connected. Nothing here was run on Windows, in a real window or with a real device.

1. **A built game loads the project's input asset.** `player/main.cpp` calls `InputRuntime::UseProjectAsset(baseDir)` (the
   game's data folder) before `InputActions::Load()`, the call the editor's Play makes with the project folder. The export
   (`editor/BuildGame.cpp`) adds `Assets/input.okayinput` (`InputRuntime::kProjectAssetRef`) to the files it ships, so asset
   stripping does not drop it. Before this an exported game always ran on the default asset. Not checked: a real export
   run end to end; the web build's preloaded files.
2. **Gestures drive actions.** `InputActions::Tick` calls `Gestures::ApplyBound()`, which presses the actions bound with
   `Gestures::Bind` in the action table and in every local player's set, once per `Gestures::Tick` (a second action tick
   with no new gesture tick does nothing). `Bind` adds (several actions per gesture; the same pair twice is one binding;
   it used to replace), `Unbind` removes. Code binds are not saved. For a saved one there is a binding path,
   `gesture:<name>` (`gesture:tap`, `gesture:swipeup` ...; `InputBinding::Kind::Gesture`), written in the `.okayinput`
   asset like any binding. The Input Actions window has no gesture picker; the path is typed.
3. **Mouse.** `Input` has five buttons (`kMouseButtons`: 3 and 4 are the side buttons) and a sideways wheel
   (`MouseWheelX`, `FeedMouseWheelX`). Bindings: `mouse:x1`, `mouse:x2` ("Mouse 4", "Mouse 5"), `wheel:left`,
   `wheel:right`. The player feeds them (`InputMouseMask`, `InputMouseButton` in `PlatformKeys.hpp`); the Keybind Field
   captures a side button. The editor's Play builds its own mask and still feeds three buttons and no sideways wheel
   (the editor is Claude 2's lane). The input recorder keeps the side buttons (they are in the mask) but not the
   sideways wheel.
4. **Guide button.** `Input::PadGuide` (16, after RT; the old ids did not move), `pad:guide` (also `pad:home`), fed by
   `FeedPadAt` from `platform::GamepadButton::Guide` or a mapping's `LBtGuide`. Names: "PS", "Home", "Steam", "Guide".
   Steam, the Xbox bar and PlayStation's own menu can take the button before a game sees it. Share / Misc, paddles and
   the touchpad click are not in the platform layer, so they were not added.
5. **Keys.** Punctuation has names (`minus`, `equals`, `leftbracket`, `rightbracket`, `backslash`, `semicolon`,
   `apostrophe`, `grave`, `comma`, `period`, `slash`) and is fed as the US character of the key's place. New codes
   154..175: numpad 0-9, divide, multiply, minus, plus, Enter, period, Caps Lock, Print Screen, Scroll Lock, Pause, Num
   Lock, Menu. No existing code moved, so saved bindings load as before. Numpad Enter is fed as itself and as Enter:
   `key:kpenter` is the numpad key alone, `key:enter` is still either, and a "press a key" field gets the numpad key.
   While a text field has focus the numpad's digits and signs are typing keys. **Arrows.** The feeds (`AppendHeldMainKeys`, `TapCharsForKey`) give an arrow as itself only; `Input::FeedKeys` then adds its W, A, S or D letter for the raw reads (`GetKey('w')`, `AxisWASD`, script `key("w")`), so old games and the 14 raw reads in the engine keep walking on the arrows. A letter held only because of its arrow is marked, and the bindings read `Input::GetKeyGameExact`: `key:w` no longer fires on Up, so the arrows and W A S D can be bound to different actions. The default assets already bind the arrows on their own. An older asset that binds only W A S D no longer moves on the arrows through that binding (it is not migrated); the input recorder records the arrow alone and adds the letter on replay.
6. **Eight controllers.** `Input::kMaxGamepads` is 8, the same as `PlayerInputManager::kMaxPlayers`. The literal fours
   tied to it follow: `pad0`..`pad7` (`ia_join`), `pad:a@7`, the Player Input component's `controller` (0..7), the
   per-controller Smooth memory of a processor. Saved files do not change. More than four real pads was never tried.
7. **Controller toasts.** With no `PadProfiles::SetToastHandler`, a connect or disconnect notice goes to
   `GetScriptUI()->Toast` (OkayUI's toasts in the player and the editor's Play; the path quest notices use). Pads
   already plugged in when the player starts are opened before the UI is installed, so they are not announced.

## Input (internal record)

*Was `docs/input.md` until 2026-11-20.*

Not a guide. Where things are and what is missing; the details are in the headers and `docs/feature_map/08-input.md`.

### What exists
- Raw input and actions: `engine/include/okay/Input/Input.hpp`, `InputActions.hpp` (contexts, chords, response, timing, per player), `RemapModel.hpp`, `InputRecorder`, `InputDebug`, `Haptics`.
- Controllers: `GamepadInfo` (family from USB ids / name / host type), `ControllerMapping` (generic layout, Controller Setup, saved mapping), `PadProfiles`, `PadSlots`, `PlatformGamepad.hpp` (`PadSet`), optional `platform::Gamepad` extras (SDL backend: ids, raw axes, battery, lightbar, trigger rumble, touchpad, gyro). `OKAY_GENERIC_PADS=0` stops opening unmapped joysticks.
- Typed actions: `ActionSet` (value types, composites, processors, schemes, interactions with phases, maps with priority / consume, rebinding overrides), `InputAsset` (.okayinput text, default asset, migration of `InputActions`), `PlayerInput` (local players), `InputRuntime` + `ia_*` script calls, `InputBuffer` (buffer, coyote time), `ActionEditor` (logic of the editor window).
- Components / UI / scripts: `LocalPlayerInput` (Add Component > Player Input; record `playerinput`), `Input.Player*` / `Input.SetActionMapEnabled` / `Input.MyPlayer` engine-API nodes and `ia_my_player()` (palette: Gamepad & Controls > Local Players), `UIGallery::AddControlsScreen` (UI > Prebuilt Screens > Controls; `UIKeybindField` on "ia:<action>@<player>").
- Editor: `editor/InputActionsWindow.hpp` (View > Editors & Tools > Input Actions), `editor/InputDebugger.hpp` (Input panel). New Project writes `Assets/input.okayinput`.
- Playground station H (Controller) in `examples/input/input_playground.okayscene`; `examples/input/default.okayinput` equals the default asset (a test checks it).
- Tests: `controller_mapping`, `action_set`, `action_phases`, `input_asset`, `input_script_api`, `action_editor`, `local_player_input`, `controls_screen`, `input_autoload`, `input_playground`, plus the older input suites.

- Built-in controllers on actions: `InputActions::Rebound`, `ControlKey`, `ControlKeyDown`, `ControlMouse`, `ControlMouseDown`, `ControlMove`. A controller's key field reads that key until the player rebinds the matching action ("Jump", "Sprint", "Crouch", "Move", "Aim", "Fire", "Reload", "Interact"); then a field still on the action's default key follows the action (all its bindings, pads too) and the old key stops. A field set to another key stays that key. Used by First / Third Person, Character Controller 2D / 3D, Top Down (3D and 2D), Third Person Shooter, Top Down Shooter, Platformer 2.5D, `StanceControl`, Ranged Weapon, Melee Attacker, Item Pickup and Vehicle Seat. Test `controller_actions`.
- Part 2 (same rule): Free Roam (rise = "Jump", Ctrl = "Crouch", boost = "Sprint", "Move"; its `downKey` C has no action and stays C). Platformer 2.5D movement ("Move"), layer keys (W / S follow "Move" up and down: `ControlDirKeyDown`) and grab (E follows "Interact"). New actions "Prone", "LeanLeft", "LeanRight" and "Dash" for the first / third person, shooter, stance, character 3D, top-down shooter and 2.5D controllers. They start with no binding of their own: their stock keys (Z, Q, E, Space) are Interact's and Jump's or keys games often give their own actions, and the Controls screen would report a clash. The controller keeps Z / Q / E / Space until the player binds them (`ControlKey(action, key, stockKey)`).
- Vehicle Controller (both models) and Vehicle Controller 2D: new actions "Throttle" (W, Up), "Brake" (S, Down), "SteerLeft" (A, Left), "SteerRight" (D, Right), "Handbrake" (Space), "Boost" (Shift), "GearUp" (X), "GearDown" (Z), "Horn" (H), "Headlights" (L), all in the "Vehicle" context, keyboard only (the controller's own pad fields are unchanged). The older model's axis is `InputActions::ControlAxis`. A context action is sampled by `Tick` only while its context is pushed; otherwise `ControlKey` reads it straight from its bindings (`DirectHeld`, `DirectDown`), so nothing has to push "Vehicle". The Controls screen (`RemapConflicts`, `RemapAllConflicts`) now reports a shared key only between actions of one context, so W can be Move and Throttle.
- Pause Menu: `InputActions::MenuKeyDown("Pause", toggleKey)`. Escape until "Pause" is rebound; then its bindings, read straight from the devices, so the same key also closes the menu while its Block context swallows every action.

### What is missing
Controllers still on raw keys: the Vehicle Controller's recover, camera, look-back and separate-reverse keys and every vehicle pad field, Free Roam's C descend key and right-mouse look, dodge keys, throw (Procedural Object Holding), builders (Block / Structure Builder, Builder Mode, Build Mode), diggers other than VoxelDigger, Flashlight / Player View / inventory / crafting toggle keys, Boat Controller, Click To Move, UI widgets, visual-script and OkayScript key nodes. A lean key shared with a use prompt is checked against the controller's own key field, not the rebound key. See also the Input row in `docs/status/UNFINISHED.md`: the exported player does not load a project `input.okayinput` yet (the editor's Play does: `InputRuntime::UseProjectAsset`), touch leaf inputs, per-binding interactions, adaptive triggers / HD haptics. Everything was only run with fake devices; the ImGui drawing was never run in a real editor window; nothing on Windows.

### Pictures
Not stored in the repository. `tools/input_shots.sh <imgui folder> [output folder]` draws them headless with ImGui (output defaults to `$OKAY_SHOT_DIR`).

## Input audit

*Was `docs/input_audit.md` until 2026-11-20.*

Branch `claude/input-upgrade`. Owner ask: "look at the input system, upgrade it and improve it".

This page says, in plain words, what happens to a key press, a mouse move, a controller stick, a touch and a
gesture on the way from the operating system to a script, a controller (the player's own movement code) and a UI
widget. For each case it says **ok**, **bug** or **limitation**, with the headless test that shows it.

Everything below was run headless on Linux (no window, no real keyboard, no real controller). The test file is
`tests/test_input_deep.cpp` (suite `input_deep`). Real keyboard layouts, real controllers and the Windows build were
**not** tried. Each "unverified" says so.

### How input travels

1. The window system gives the player (`player/main.cpp`) or the editor (`editor/main.cpp`) events and the state of
   the keyboard, mouse and controllers.
2. Once per frame the host turns that into the engine's input state: `Input::FeedKeys` (one `char` per held key),
   `Input::FeedMouse`, `FeedGamepadRaw` (sticks, triggers and buttons through the dead zones), `FeedTouch`, wheel
   and typed text.
3. `InputActions::Tick()` runs once, after every device is fed. It turns the state into action values (Jump, Move,
   Fire...), applies contexts, on-screen controls and hold-to-toggle, and stores one value per action.
4. Then the scene updates. A script reads either raw input (`key("w")`, `mouse_x()`) or actions
   (`action("jump")`, `action_x("move")`). The built-in controllers read raw input. UI widgets read the mouse and
   the typed text.

So a press is seen in the same frame it was polled: **no extra frame of delay** (the player order is: events,
feed, Tick, then scene update; read from `player/main.cpp`, not measured with a real device).

### Findings

| # | What | Verdict | Test (`tests/test_input_deep.cpp`) | Status |
|---|---|---|---|---|
| 1 | A key or click that goes down and up between two polls (a hitch, a very quick tap) never reached the game, because the host polls the held set once per frame | bug | `input_deep_tap_between_polls`, `input_deep_click_between_polls` | **fixed**: hosts forward the KeyDown / MouseButtonDown event (`Input::FeedKeyTap`, `FeedMouseTap`); the tap counts for one frame and shows as released the next |
| 2 | Rebinding **Move** (a direction) to one key replaced its four-key binding with a key that has no direction, so Move read (0, 0) for ever | bug | `input_deep_rebind_direction_action` | **fixed**: a direction action refuses a binding that cannot point |
| 3 | Typing in a text field also walked the player and fired actions: scripts read raw keys and actions, and nothing told them a field had the keyboard | bug | `input_deep_typing_does_not_move_player` | **fixed**: `Input::SetTextFocused` (the player and editor set it from "a UI input field has focus"); the game's key reads (`GetKeyGame`, script `key()`, `axis_x`, actions) ignore typing keys while it is on, the field itself still sees raw keys |
| 4 | Layouts with no Latin letters (Russian, Greek, Arabic...) answer "unknown" for the letter W, so W A S D were dead in the player and the editor Play view | bug | `input_deep_layout_without_latin_letters` | **fixed**: those layouts fall back to the US key position (`KeyForLetter`). Latin layouts are unchanged. Real layouts not tried |
| 5 | A touch that began and ended in one frame only showed phase `Ended`, so a script waiting for `Began` missed it | bug | `input_deep_quick_touch_tap_still_began` | **fixed**: `Touch::began` stays true for that frame. The gesture recogniser was not changed |
| 6 | Ctrl+Z (or Ctrl+anything) also made the player crouch, and Z also fired a plain Z binding | gap | `input_deep_chord_wins` | **fixed**: chord bindings (`chord:ctrl+z`) exist and a held chord keeps its keys to itself |
| 7 | The mouse wheel could not be an action binding, so wheel weapon-switch could not be rebound | gap | `input_deep_wheel_binding` | **fixed**: `wheel:up`, `wheel:down` |
| 8 | `Look` (mouse) is clamped to length 1, so a 100 pixel flick in one frame reads the same as 20 pixels | limitation | `limitation_look_flick_is_clamped` | per-action **sensitivity** can now pass 1 (`InputActions::SetResponse`); the raw mouse delta is intact |
| 9 | Action edges (`Down`, `Up`) are per frame. A fixed-step reader could miss a press (no step that frame) or see it twice | limitation | `upgrade_jump_buffer_consume` | `InputActions::Buffered` / `Consume` hold a press until it is used. Only `Volume` overrides `FixedUpdate` today, so nothing in the engine is hit |
| 10 | Per frame work: 100 actions, 4 controllers, a context: about 294 allocations and 19.9 microseconds a frame; every `InputActions::Get("Name")` lowercased and scanned the whole list (2.1 microseconds) | cost | `upgrade_fast_path_matches_legacy` (same results as the old path) | **fixed**, numbers below |
| 11 | W A S D typed for a controller by the runtime made the "last device" look like the keyboard | gap found while building glyphs | `upgrade_glyphs_follow_active_device` | handled |
| 12 | AZERTY: the key **labelled** W is "W" (bottom left on AZERTY), not the key in the W position (Z). That is what `KeyForChar` does on purpose | limitation | none (needs a real layout) | open: an option to read physical positions (ZQSD) is not built. Row in `docs/status/UNFINISHED.md` |
| 13 | Gamepad rumble had no patterns (a game had to time `Input::Rumble` calls itself) and no player setting | gap | `upgrade_haptic_patterns` | **built**: `InputHaptics` (tap, buzz, pulse, heartbeat, ramp, ramp down) and a vibration strength setting; one pending request per controller is still how the runtime plays them |
| 14 | No gyro or touch-pad input in the platform layer | limitation | - | open |

### Cases checked and found ok

All in `tests/test_input_deep.cpp` as `audit_*` cases.

| Case | Result |
|---|---|
| One key, one `Down`, then held, then one `Up` | ok (`audit_key_edge_once`) |
| Two keys on one action (Q and Z for Dash) | ok: held while either is held, one `Down`, one `Up` (`audit_two_keys_one_action`) |
| Shift+W: Sprint and Move together | ok. The generic Shift follows either side (`audit_modifier_combos`) |
| WASD and arrows together, opposite keys, diagonals | ok: no double speed, the diagonal has length 1 (`audit_move_vector`) |
| Radial versus axial dead zone, full tilt, trigger threshold | ok (`audit_deadzones`) |
| Trigger as a button and as a value, d-pad as a button and as a direction, controller 2 does not press player 1's Jump | ok (`audit_triggers_dpad_pads`) |
| Pulling a controller out mid-press | ok: nothing sticks, `Up` fires once (`audit_pad_unplug_releases`) |
| Mouse delta on the first sample, wheel notches added up and cleared next frame | ok (`audit_mouse_wheel_and_delta`) |
| Several fingers, drag test, a lifted finger stays one frame | ok (`audit_touch_multi_finger`) |
| Contexts: a menu eating Space, a modal blocking the game | ok (`audit_contexts`, `audit_pause_context_blocks_gameplay`) |
| Hold-to-toggle for Sprint | ok (`audit_hold_to_toggle`) |
| Bindings saved by earlier versions still load, reset removes them | ok (`audit_saved_bindings_stay_readable`) |
| The same 100 ms press at 30, 60, 144 and 240 frames per second: one `Down`, one `Up`, the same hold time | ok (`upgrade_edges_independent_of_frame_rate`) |

### Not tested here, and why (unverified)

* **Real windows and real devices.** Stuck keys when the window loses focus, key repeat, mouse capture and relative
  motion, high-DPI scaling, controller hot-plug in the real backend, and every real keyboard layout need a real
  machine. The code paths were read, not run. SDL clears its key state on focus loss, so a stuck key is not
  expected; this was not tried.
* **Input during a loading screen and in the editor Play view versus the Scene view.** The editor feeds the game
  only while it plays and only when the pointer decision says so (`DecidePlayPointer`, covered by earlier tests);
  the new tap and text-focus hooks sit inside that same branch. Not run in a real editor window.
* **The editor and player programs were not built.** The engine code and tests were. `editor/main.cpp` and
  `player/main.cpp` edits were syntax-checked only (see the report).

### Frame-rate numbers

Measured with a small benchmark (`tests/test_input_bench.cpp` on this branch; the same program was also compiled against `origin/main` for the "before" column): 101 actions (11 built in plus 89 added with three bindings each), 4 controllers fed, one context pushed,
`-O2`, a busy shared machine (so treat the absolute numbers as rough; the ratio is what matters).

| Per frame (feed keys, mouse, 4 controllers, `Tick`, 4 reads) | before (`origin/main`) | after | after with `OKAY_OLD_INPUT=1` |
|---|---|---|---|
| With a context pushed | 14.2 to 20.9 microseconds, 294 allocations | 4.4 microseconds, 0 allocations | 10.5 microseconds, 0 allocations |
| No context (the usual case) | 14.0 to 14.8 microseconds, 292 allocations | 2.7 to 3.0 microseconds, 0 allocations | 9.4 microseconds, 0 allocations |
| `InputActions::Get("Action77")` (lookup by name) | 2104 nanoseconds | 38 nanoseconds | about 2100 to 3600 nanoseconds |
| `Input::GetKey('e')` | 2.9 nanoseconds | 1.3 nanoseconds | the same |

(Three runs each; the "before" spread is the machine being busy. 5000 frames per run after 200 warm-up frames.)
Where the time went before: the held-key table was a hash map copied on every `FeedKeys` (many small allocations),
every action lookup lower-cased the name and every action's name, and with a context pushed each binding built a
temporary list of the inputs it listens to. After: a fixed table, a name index, and no temporary lists. Note that
`OKAY_OLD_INPUT=1` brings back the old lookup and evaluation order but not the old key table, so it is not a
byte-for-byte copy of the old speed.

**Outdated (2026-11-23):** there is one evaluation now (see "One input system"); `OKAY_OLD_INPUT=1` and `SetLegacy` only
record a flag. Before: `OKAY_OLD_INPUT=1` (or `InputActions::SetLegacy(true)`) brought back the old lookup and the old evaluation order
for A/B. The test `upgrade_fast_path_matches_legacy` runs 600 random frames (keys, mouse, 4 controllers, chords,
contexts, per-player actions) through both and requires every action value, vector and edge to match.

### What was built from these findings

See `docs/engine/input.md` ("The input upgrade"): chords, wheel bindings, per-action dead zone / curve / sensitivity /
invert (saved), jump buffering and consume, double tap, hold to confirm, rumble patterns, per-player actions, rebind profiles and presets, controller picture names by device family (Xbox, PlayStation, Switch), the
last-used device, input recording and replay, and the Input panel in the editor.

### What was written from what

Everything is new code written for this project from general knowledge: dead zone and curve maths, input
buffering, chord and double-tap timing, and the button names printed on Xbox, PlayStation and Nintendo pads are
common knowledge. Nothing was copied or ported from another engine or library, and no third-party code or SDK was
added.
