# Input: actions, controllers and rebinding

Games can read raw keys (`Input.GetKey("w")`), but the better way is **actions**:
named things the player does ("Jump", "Move", "Fire"). An action listens to a
keyboard key, a mouse button and a controller button at once, and the player can
rebind it. Their bindings are saved with their prefs and come back next time.

## The built-in actions

| Action | Keyboard / mouse | Controller |
| --- | --- | --- |
| Move (direction) | W A S D, arrow keys | left stick, d-pad |
| Look (direction) | mouse movement | right stick |
| Jump | Space | A |
| Sprint | Shift | left stick click |
| Crouch | Ctrl | B |
| Fire | left mouse | RT |
| Aim | right mouse | LT |
| Interact | E | X |
| Reload | R | Y |
| Inventory | Tab | Back |
| Pause | Escape | Start |

Buttons are **down** while any of their bindings is pressed past half way (a
trigger counts as pressed at half pull). Direction actions give an x, y vector
in -1..1 with y up: sticks keep their analog tilt, keys give full steps, and the
length never goes past 1, so diagonals aren't faster.

## Reading actions in scripts

C#-style scripts (also the **Input** nodes in a visual script):

```csharp
if (Input.GetButtonDown("Jump")) Jump();
bool firing = Input.GetButton("Fire");
float throttle = Input.GetActionValue("Fire");     // 0..1, a trigger can be part way
Vector3 move = Input.GetActionVector("Move");      // x, y in -1..1
Input.Rumble(0.6f, 0.3f, 0.2f);                    // heavy motor, light motor, seconds
Input.RebindAction("Interact", "key:f");
if (Input.gamepadConnected) ShowControllerHints();
```

OkayScript:

```
if action_down("jump"): jump()
speed = action_value("fire")
mx = action_x("move")
my = action_y("move")
rumble(0.6, 0.3, 0.2)
rebind("interact", "key:f")
```

Action names are not case-sensitive.

## Bindings

A binding is written as text:

| Text | Means |
| --- | --- |
| `key:space`, `key:e`, `key:shift`, `key:up`, `key:f5`, `key:left ctrl` | a keyboard key |
| `mouse:left`, `mouse:right`, `mouse:middle` | a mouse button |
| `pad:a` `b` `x` `y` `lb` `rb` `lt` `rt` `ls` `rs` `back` `start` `up` `down` `left` `right` | a controller button (`lt` / `rt` read the trigger's pull) |
| `keys:w,s,a,d` | four keys as a direction: up, down, left, right |
| `stick:left`, `stick:right`, `dpad` | a controller stick or the d-pad as a direction |
| `pad:a@1`, `stick:left@2`, `dpad@1` | the same on another controller (0-3, default 0) |
| `mouse:delta` | mouse movement as a direction |

## Letting players rebind

Add a **UI Keybind Field** (Add Component > UI) and pick its **Input Action**.
It shows the action's current binding. The player clicks it (or selects it
with a controller and presses A) and presses the new key. Escape cancels, and
Backspace puts the action's defaults back.

- Tick **Gamepad** to make the field listen for a controller button instead. A
  controls menu usually has one keyboard field and one gamepad field per action.
- For **Move**, pick a **Direction Key** (Up, Down, Left, Right) to rebind just
  that key.

A rebind replaces the action's first binding for that device. The others stay,
so rebinding Jump to F keeps A on the controller. Only bindings the player
changed are saved.

## Keys

Keys are one `char` each. Letters, digits, punctuation, Space, Enter, Escape,
Backspace, Tab and the arrows work as before. These extended keys were added; the
runtimes feed them from SDL scancodes, so they are the same on every keyboard
layout:

| Name | Notes |
| --- | --- |
| `f1` ... `f12` | `Input::KeyF(n)` in C++ |
| `insert`, `delete`, `home`, `end`, `pageup`, `pagedown` | the navigation block |
| `lshift`, `rshift`, `lctrl`, `rctrl`, `lalt`, `ralt` | one side only |
| `shift`, `ctrl`, `alt` | true while **either** side is held |

Names are not case-sensitive and ignore spaces, `_` and `-` (`Left Shift`,
`page_down`). `key:f5`, `key:left shift` and so on work in bindings and rebind
fields. Old saved bindings are unchanged. `Input::IsKnownKeyName` (C++) tells a real name
from a typo: an unknown name such as `f13` still falls back to its first letter, as
before, instead of failing.

The editor's Play mode does not feed the new keys yet; the standalone player does.

## Input contexts

Actions can belong to a **context** so a menu can take keys away from gameplay.
An action with no context is in the base (gameplay) context, which is always
active at the bottom of a stack.

```cpp
InputActions::DefineIn("UI", "Submit", {"key:space", "pad:a"});
InputActions::DefineIn("UI", "Cancel", {"key:escape", "pad:b"});

InputActions::PushContext("UI");     // Submit is live; Jump no longer fires on Space or A
...
InputActions::PopContext("UI");      // gameplay gets its keys back
```

| Mode | While the context is on the stack |
| --- | --- |
| `Consume` (default) | every key / button its actions bind is swallowed from lower contexts; other inputs pass through |
| `Block` | lower contexts get nothing at all (a modal menu) |
| `Passthrough` | lower contexts see everything, both fire |

Contexts nest: the topmost is served first and each one sees only what the ones
above it left. An action whose context is not pushed reads 0. Swallowing happens
per input, so a UI that binds W takes W from Move but leaves A S D. A press that
a context takes over from a held key ends the old action (`Up` fires).

The pause menu is one consumer. While it is open it pushes a context named
`Pause` in Block mode, so Jump, Move and the other gameplay actions do not fire.
It pops that context when it closes, including when something else unpauses the
game. Raw keys are unchanged, so Escape still toggles the menu.
`Input::SetUICaptured` is set at the same time for code that reads the capture flag.

Raw `Input.GetKey` is **not** affected, because scripts that read raw keys do not
go through actions. Use `InputActions::KeyConsumed(key)` /
`PadButtonConsumed(button, player)` to honour the stack from raw code, or read the
action. Contexts are set up in code and are not saved with the player's bindings.
`InputActions::SetContext(action, context)` moves an existing action.

`InputActions::SetThreshold(action, 0.2)` changes the value at which a button
action counts as held (default 0.5), for example a light trigger pull.

## Controllers

Up to four controllers are read, one per player slot (0 is the first). They fill
in the order they are plugged in, and plugging one in or pulling it out mid-game
works. The first controller also presses W A S D and Space (left stick, d-pad, A),
so games written against the keyboard still play on a controller.

A binding says which controller it reads with `@n`: `pad:a@1`, `stick:left@2`,
`dpad@1`. No suffix means the first controller. Rebinding keeps the suffix, and
old saved bindings (no suffix) are unchanged.

```cpp
InputActions::Define("P2Jump", {"pad:a@1"});
```

In C++ every reader takes an optional player: `Input::GamepadAxis(1)`,
`Input::GetGamepadButton(Input::PadA, 1)`, `Input::GamepadTrigger(1, 1)`,
`Input::GamepadConnected(2)`, `Input::GamepadCount()`,
`Input::Rumble(low, high, seconds, 1)`. The runtime feeds each one with
`Input::FeedGamepadRaw(player, ...)`; `okay/Input/SdlGamepad.hpp` has
`SdlGamepadSet` to manage the slots from the SDL event loop.

### Dead zones, curves and trigger thresholds

The runtime feeds raw stick and trigger values and `Input::GamepadSettings(player)`
turns them into what games read:

```cpp
auto& cfg = Input::GamepadSettings(0);
cfg.left.deadZone  = 0.25f;                        // ignore stick drift
cfg.left.shape     = Input::StickSettings::Shape::Axial;
cfg.right.curve    = 2.0f;                         // finer aim near the centre
cfg.right.outerZone = 0.92f;                       // a worn stick that tops out at 0.92 still reads 1
cfg.triggers.deadZone  = 0.05f;
cfg.triggers.threshold = 0.3f;                     // pull needed for the LT / RT button
```

| Setting | Meaning |
| --- | --- |
| `shape` | `Radial` (default): the whole stick vector must pass the dead zone and keeps its direction. `Axial`: each axis is dead on its own, so a slightly off-axis stick snaps to straight lines |
| `deadZone` | tilt that still reads 0 (default 0.18, kept from before; clamped to 0..0.95) |
| `outerZone` | tilt that reads full strength (default 1) |
| `curve` | `out = in ^ curve`; 1 is linear, 2 is gentler near the middle |
| `triggers.deadZone`, `curve` | the same for each trigger |
| `triggers.threshold` | processed pull at which the trigger counts as a pressed button (default 0.5) |

The defaults match the old fixed behaviour. `Input::ProcessStick` and
`Input::ProcessTrigger` apply a setting to a value without feeding anything.
Values passed to the older `FeedGamepad` / `FeedGamepadExtra` are taken as given.

`Input.Rumble(low, high, seconds)` shakes the controller. `low` drives the heavy
motor and `high` the light one.

## Touch

A finger is a real touch, not a stand-in for the mouse. The player (desktop and the
web build) reads SDL finger events and keeps every finger that is down. The first
finger still moves the mouse, which is how a tap hits an ordinary button. The extra
fingers are for the controls and the gestures below.

`Input.TouchScreen` (OkayScript `is_touch_screen()`) becomes true when the machine
reports a touch screen, or when a finger first lands. It stays true for the rest
of the run.

### Gestures

| Gesture | What it is |
| --- | --- |
| Tap | a finger down and up, without moving, inside a third of a second |
| Double tap | two taps close together. The single tap is held back until it is clear this is not a double tap |
| Long press | a finger held still for half a second |
| Swipe | a quick flick. The direction is up, down, left or right |
| Pinch | two fingers moving apart or together. The scale is 1 where the pinch began |
| Rotate | two fingers twisting. The angle is in degrees, positive counter-clockwise |

OkayScript, true on the frame it happens:

```
if gesture("tap"):        set_text("Tap")
if gesture("swipe up"):   set_text(swipe_direction())
scale = pinch_scale()
angle = twist_degrees()
```

`gesture` also takes `"double tap"`, `"long press"`, `"swipe"`, `"swipe down"`,
`"swipe left"`, `"swipe right"`, `"pinch"` and `"rotate"`.

C#-style scripts use `Input.Gesture("tap")`, `Input.swipeDirection`,
`Input.pinchScale` and `Input.twistDegrees`.

A visual script has **On Tap**, **On Double Tap**, **On Long Press**, **On Swipe**
(the box can say up, down, left, right, or stay empty for any direction),
**On Pinch** and **On Two Finger Rotate**. **Get Swipe Direction**, **Get Pinch Scale**
and **Get Twist Angle** read the same values. A finger that is on a virtual stick,
a D-pad or a touch button is that control, not a swipe.

### On-screen controls

Add Component > UI, or GameObject > UI:

| Control | Drives | Notes |
| --- | --- | --- |
| Virtual Joystick | a direction action, Move by default | Fixed, or floating (it appears under the finger inside its rectangle) |
| D-Pad | a direction action, Move by default | Up, down, left, right. Diagonals can be turned off |
| Touch Button | a button action, Jump by default | Held while the finger is down. The label is drawn on it |

Each one has **Only On Touch**. On, the control is hidden and ignores the mouse
until a touch screen is in use, so a desktop game is not covered in buttons. Off,
it is always drawn, and a mouse can press it. The keyboard and controller bindings
of the same action keep working: the stick adds to Move, it does not replace W A S D.

`examples/touch_controls` is a small scene: a stick, a D-pad and a Jump button,
a marker that walks with Move, and a line of text that names the latest gesture.

## Your own actions

A game can add actions or change a built-in one's defaults, usually from a
script's start:

```csharp
Input.DefineAction("Dash", "key:q", "pad:rs");
Input.DefineDirection("Steer", "keys:i,k,j,l", "stick:right");
```

```
define_action("dash", "key:q", "pad:rs")
define_direction("steer", "keys:i,k,j,l", "stick:right")
```

A player who rebound the action keeps their binding; the new defaults are what
Backspace on a keybind field goes back to. In C++ it is `InputActions::Define`.

## The input upgrade (audit: `docs/input_audit.md`)

Everything here is also on show in the **input playground** example (`examples/input/input_playground.okayscene`,
see `docs/try_it.md` row 7x). Old scenes and saved bindings keep working: new binding kinds are new text forms, and the
saved format for a binding is unchanged.

### Bug fixes
* A key or click that is pressed and released between two polls is no longer lost (`Input::FeedKeyTap`, `FeedMouseTap`).
* Typing in a text field no longer walks the player: while a field has the keyboard (`Input::SetTextFocused`), the game's
  reads of letters, digits, Space and arrows report nothing (`Input::GetKeyGame`, script `key()` and `axis_x()`, every
  action). Raw `Input::GetKey` is unchanged so the field still sees Backspace, Enter and the arrows.
* Rebinding a direction action (Move, Look) to a single key or button is refused instead of breaking it.
* Keyboard layouts with no Latin letters (Russian, Greek, Arabic...) fall back to the US key positions for W A S D and
  the digits (`KeyForLetter` in `PlatformKeys.hpp`).
* A touch that began and ended in one frame still reports `began` (`Input::Touch::began`).

### New bindings
| Text | Means |
| --- | --- |
| `chord:ctrl+z`, `chord:ctrl+shift+s` | two to four keys that must all be held. A held chord keeps its keys to itself: Ctrl+Z does not also Crouch (Ctrl) or fire a plain `key:z` |
| `wheel:up`, `wheel:down` | one notch of the mouse wheel this frame (weapon switch, zoom) |

### Response: dead zone, curve, sensitivity, invert
Each action can shape its reading: `InputActions::SetResponse("Look", {deadzone, curve, sensitivity, invertX, invertY})`,
OkayScript `set_response("look", 0.1, 2, 1)`. Dead zone (0..0.95) stretches the rest back to 0..1; curve is `in ^ curve`
(2 = fine aim near the centre); sensitivity multiplies after the curve and may pass 1 (look speed), invert flips a
direction's axes. The default is neutral, so old actions read exactly as before. `InputActions::ShapeValue(in, r)` gives
the curve for drawing; the editor's Input panel draws it. Saved as `inputresp.<action>`.

### Timing: buffering, double tap, hold to confirm
Time is the seconds passed to `InputActions::Tick(dt)` (`Tick()` measures it).
* `Buffered("Jump", 0.15)`: a press happened in the last 0.15 s and was not used yet; `Consume("Jump")` uses it up, so a
  jump pressed just before landing fires once, on landing, and never twice. OkayScript `action_buffered`, `action_consume`.
* `DoubleTap("Interact", 0.3)`: pressed now and also less than 0.3 s before.
* `HeldFor`, `HoldProgress("Reload", 1.0)` (0..1 for a progress ring) and `HoldReached("Reload", 1.0)` (true on the frame
  the second is reached) for hold-to-confirm.

### Several local players
`InputActions::SetPerPlayer("Jump")` makes an action also readable per controller: `Get("Jump", 2)` is the third
controller. Controller bindings follow the player (a plain `pad:a` is A on whichever controller asks); the keyboard and
mouse belong to player 0. OkayScript: `action("jump", 1)`, `action_x("move", 1)`. The plain `Get("Jump")` still reads the
first controller and the keyboard.

### Rebinding
Conflict checks and swapping are the existing remap model (`RemapConflicts("Jump", "key:e")`, `RemapBind(..., RemapResolve::Swap)`
in `RemapModel.hpp`, the data side of a Controls screen); it now understands chords and wheel bindings. OkayScript:
`action_conflicts("jump", "key:e")`, `rebind_swap("jump", "key:e")`. `ExportBindings` / `ImportBindings` write and read a versioned block (`okayinput 1`);
`SaveProfile("Player 2")` / `LoadProfile` keep named sets in Prefs. `ApplyPreset("left")` and `("right")` put every action
on one half of the keyboard (one-handed play); `Reset()` undoes them.

### Rumble patterns
`InputHaptics::Play("heartbeat", player)` plays a short list of rumble steps (tap, buzz, pulse, heartbeat, ramp, ramp down) on a
controller, or `Play(steps, player)` with your own `{heavy, light, seconds}` steps. `InputHaptics::SetStrength(0.5)` scales every
pattern (0 = vibration off) and is kept for the player (`input.haptics`). OkayScript: `haptic("heartbeat", 0)`, `haptic_strength(0.5)`.
The strength is applied once, when the host takes the request (`Input::TakeRumble`), so a plain `Input::Rumble(...)` call obeys the
player's vibration setting too (0 = no vibration at all), not only patterns.

### Keys by position (AZERTY and other layouts)
By default a letter means the key that has that letter printed on it, so on AZERTY the "W" key is bottom left. A player can
switch to key positions: `Input::SetKeysByPosition(true)` (kept in Prefs as `input.keypos`; OkayScript `key_positions(true)`).
Then every letter and digit is read at its US place, so W A S D are the Z Q S D keys on AZERTY, under the same fingers as on a US
keyboard. `KeyForLetter` in `PlatformKeys.hpp` reads the switch, so the player host uses it with no other change. Off by default.
Not tried on a real AZERTY keyboard.

### Pictures that follow the device
`Input::LastDevice()` says whether the player last used the keyboard, mouse, a controller or touch. `Input::GamepadFamily(p)`
is Xbox, PlayStation or Switch (set from the controller's name when it is plugged in). `binding.Label(family)` prints
"Cross" or "R2" for a PlayStation pad, "B" for the Switch's right button. OkayScript: `input_device()`, `action_label("jump")`.

### Controllers: PS5, Xbox and any other pad
Written from public USB vendor / product id facts and how each maker names its pads. No mapping database text, code or
artwork from another project was used; the tables are in `engine/src/Input/GamepadInfo.cpp`.

- **Who is it** (`Input/GamepadInfo.hpp`, pure logic): `ClassifyGamepad(GamepadIdentity)` takes the USB ids, the reported name,
  the host's controller type and the raw axis / button counts, and returns the family (Xbox 360, One, Series; DualShock 3 / 4;
  DualSense and Edge; Switch Pro and Joy-Con; Steam Controller and Deck; Stadia, Luna, Shield; generic XInput; generic pad;
  flight stick; wheel; arcade stick; unknown), how it was decided (`usb id`, `sdl type`, `name`, `shape`) and what the family
  has (touchpad, gyro, lightbar, player LEDs, adaptive triggers, trigger rumble, HD haptics). An id beats a name, the host's
  type beats a name, a maker we know with a model we do not still gets that maker's layout, and anything else is a generic
  controller. Unknown pads are never ignored.
- **Plain joysticks too.** The SDL backend now also opens a device SDL has no controller layout for (a "raw" joystick), when it
  looks like a pad (sticks and buttons). Set `OKAY_GENERIC_PADS=0` to go back to the old behaviour (such devices ignored).
- **Generic layout** (`Input/ControllerMapping.hpp`, `BuildGenericMapping`): from a sample taken with nothing touched. An axis
  that rests at its minimum is a trigger; the other axes are the sticks in order; face buttons are A B X Y LB RB Back Start L3 R3
  in raw order; the d-pad is the first hat (or buttons 11 to 14 on a 15-button pad). This is a guess: pads whose real order differs
  need Controller Setup.
- **Controller Setup** (`ControllerSetup`, `PadProfiles::BeginSetup`): asks for each control in turn (both sticks, both triggers,
  A B X Y, bumpers, Back, Start, stick clicks, d-pad), reads the raw input that changed, notes an inverted axis or a trigger that
  rests low, and lets the player skip or go back. `PadProfiles::SaveSetup` keeps it under `padmap.<vendor>:<product>:aNbNhN` in
  Prefs and every pad of that model uses it from then on (also a pad the host already maps, so a wrong built-in layout can be
  overridden). While it listens the game sees a neutral pad. `ClearSaved` goes back to the built-in or generic layout.
- **Names and pictures by family** (`PadButtonName(button, family)`, `PadProfiles::ButtonName(slot, button)`): Cross / Circle /
  Square / Triangle, Create (DualSense) / Share (DualShock 4), Options, L1 / R1 / L2 / R2, L3 / R3 on PlayStation pads;
  A B X Y, View / Menu (One, Series), Back / Start (360), LB / RB / LT / RT on Xbox pads; B A Y X, - / +, L / R, ZL / ZR on Nintendo
  pads (the right button is A). `PadExtraName` gives PS / Home / Guide, Share / Capture, Touchpad, Mute. The old
  `InputBinding::Label(family)` is unchanged; the richer names are what `input_button_name("jump")` prints, and `PadProfiles::BindingLabel(binding)` is what the Controls screen model (`RemapRows`, gamepad column) and the UI keybind field now use, so rebinding shows Cross on a DualSense and A on an Xbox pad (no controller plugged in: the old Xbox-style names). No glyph artwork is
  drawn yet: only text names (so nothing needs a licence).
- **Slots on hot-plug** (`PadSet`, `PadSlotAllocator`): a pad takes its old slot back when that is free, else the first free one;
  a pad that did not fit is tried again when a slot frees; plain joysticks are opened after real gamepads so they never take
  player 1. `PadSet::Swap(a, b)` / script `pad_swap(a, b)` exchanges two players' controllers. Each connect and disconnect
  becomes a `PadEvent` (`PadProfiles::PopEvent`, script `pad_event()`) and calls the toast hook
  `PadProfiles::SetToastHandler(fn)` with text like "Controller 1 connected: DualSense Wireless Controller".
- **Extras through the platform layer** (`platform::Gamepad`, optional, default "none"): USB ids, host type, raw axes / buttons /
  hats, battery level (`pad_battery`), lightbar colour (`pad_lightbar(slot, r, g, b)`), player LED number (set automatically to the
  slot), trigger motors (`pad_trigger_rumble`, or `PadProfiles::SetMirrorTriggers(true)` to add them to every rumble), touchpad
  finger (`pad_touch`) and gyro (`pad_gyro`, read only while a script asks each frame). The SDL backend implements them for SDL
  2.0.14 and newer. **Not exposed:** DualSense adaptive triggers and HD haptics (SDL has only a raw-report call for them, which
  we do not use), the touchpad click and mute button as separate inputs, and the Edge back paddles; wired / Bluetooth is only
  visible as "wired" in the battery level. The own evdev and Win32 backends return "unknown" for all of this.
- **OkayScript:** `pad_name`, `pad_family`, `pad_ids`, `pad_mapping` ("built-in", "generic", "user"), `pad_info`, `pad_raw`,
  `pad_battery`, `pad_button_name("a", slot)`, `input_button_name("jump")`, `pad_event()`, `pad_setup_start / active / prompt /
  skip / back / cancel / save / clear`, `pad_lightbar`, `pad_trigger_rumble`, `pad_swap`, `pad_touch`, `pad_gyro`.
- **Pictures** (software renders, not a real window or pad): `docs/images/input/` (`glyph-sheet-*`, `controller-station-*` and `rebinding-*` for DualSense, Xbox Series, Switch Pro and an unknown generic pad, drawn by `tools/input_shots.cpp` with ImGui from the same engine data the Controller station shows). The **Controller** station (H) is in the input playground.
- **Checked with fake devices only:** `tests/test_controller_mapping.cpp` (classifier table, generic layout, trigger range, dead
  zone, Setup, saved round trip, names, slots, toasts) and the station in `tests/test_input_playground.cpp`. No real PS5, Xbox or
  other pad, and nothing on Windows, has been run.

### Typed actions, maps, interactions and local players (the `.okayinput` system)
Existing: `InputActions` (named actions, contexts, chords, response, timing, per-player) and `RemapModel` stay as they are and keep
working. Gap this fills: typed values, composites, a processor chain, control schemes with auto switch, interaction phases,
an asset file, action maps with priority, a local-player manager and runtime rebinding with per-player saving. It is a layer next to
`InputActions`, not a second copy of it: `MigrateInputActions()` builds the new form from the old table. Written from the general
idea of action maps, in our own words and format; no code, text or tests from another project.

- **Value types** (`Input/ActionSet.hpp`): `ValueType::Button`, `Axis1D`, `Vector2`, `Vector3`; `InputValue {x, y, z}`.
- **Bindings**: a single input (the `InputBinding` text: `key:space`, `pad:rt`, `stick:left`, `dpad`, `mouse:delta`, `chord:ctrl+z` ...) or a
  composite: `AddVector2(up, down, left, right)`, `AddVector3(...)`, `AddAxis(negative, positive)`, `AddChord({modifier..., input})`.
  Composite vectors are kept to length 1 (a diagonal is not faster). The binding with the largest value drives the action.
- **Processors** (a chain on a binding and on the action, in the order written): `deadzone(min[,max])`, `invert(xyz)`, `scale(a[,b[,c]])`,
  `normalize`, `swizzle(yxz)`, `smooth(seconds)`, `curve(power)`, `clamp(min,max)`. Text round-trips (`ParseProcessors`).
- **Control schemes**: `AddScheme({"Gamepad", keyboard, mouse, gamepad, touch})`, each binding has a `group`; only the active scheme's bindings
  (and group-less ones) read. `SetAutoSwitch(true)` follows `Input::LastDevice()`; a set paired with `SetPad(n)` reads controller n and only
  that controller's activity switches it.
- **Interactions and phases**: `press`, `release`, `press_release`, `hold(s)`, `tap(s)`, `multitap(count,gap,tapTime)`, `repeat(every,first)`,
  `chorded(Action)`. Each gives Started / Performed / Canceled (`Started(name)`, `Performed(name)`, `Canceled(name)`, `PhaseOf`, `Progress`
  for a hold bar, `Events()` and `Subscribe(fn)`). With no interaction a button is started and performed when pressed and canceled when
  released; a value is started when it leaves zero, performed each time it changes and canceled at zero.
- **Gameplay timing helpers** (`Input/InputBuffer.hpp`): `InputBuffer` (a press is remembered for a moment) and `CoyoteTime` (can still jump just
  after leaving a ledge). Header-only; the game passes the frame time.
- **Action maps**: `AddMap(name, priority, consume, enabled)`, `SetMapEnabled`. Enabled maps run from the highest priority down; a map with
  `consume` swallows the exact inputs its bindings use from every map below it (the UI map eats Space so Jump does not fire).
- **The asset** (`Input/InputAsset.hpp`): text file `.okayinput` (format in the header). `SerializeInputAsset`, `ParseInputAsset` (errors name the
  line and change nothing), `LoadInputAsset`, `SaveInputAsset`. `DefaultInputAsset()` is the default for new projects (Gameplay: Move, Look, Jump,
  Sprint, Crouch, Fire, Aim, Interact, Reload, Inventory, Pause; UI map, off, consuming: Navigate, Submit, Cancel; schemes Keyboard+Mouse, Gamepad,
  Touch); the same text ships as `examples/input/default.okayinput` (a test keeps them equal). `MigrateInputActions()` turns the old table into an
  asset (contexts become maps, the response becomes processors); a test compares its readings with `InputActions` for the same keys.
- **Local players** (`Input/PlayerInput.hpp`): `PlayerInputManager` gives each player their own copy of the asset, one device (keyboard, or
  controller n, or `Any` for a single player with auto switch), their own scheme and their own saved overrides. `Join`, `PollJoin` (press any
  button), `Leave`, `Update`.
- **Runtime rebinding**: `Rebind(action, binding, part, path)`, `Conflicts(...)` (same map, bindings that can be active together),
  `ExportOverrides` / `ImportOverrides` / `ResetOverrides`, and `SaveOverrides(key)` / `LoadOverrides(key)` in Prefs (`inputov.<key>`; players use
  `inputov.player<N>`). The asset text keeps the authored bindings; overrides are separate.
- **Scripts** (`Input/InputRuntime.hpp`, ticked from `InputActions::Tick`): `ia_use_default()`, `ia_load(path)`, `ia_join("keyboard"|"pad0"..|"any")`,
  `ia_poll_join()`, `ia_x/ia_y/ia_z/ia_value(action[,player])`, `ia_down/ia_pressed/ia_released`, `ia_started/ia_performed/ia_canceled`,
  `ia_progress`, `ia_phase`, `ia_event()` (one "Jump:performed" per call), `ia_map(name,on)`, `ia_map_enabled`, `ia_scheme()`, `ia_set_scheme`,
  `ia_rebind`, `ia_conflicts`, `ia_reset_bindings`, `ia_save`, `ia_label(action)` (names follow the connected controller), `ia_players`, `ia_leave`.
- **Tests** (headless, real `Input` fed by hand): `action_set`, `action_phases`, `input_asset`, `input_script_api`. Pictures (software renders by `tools/input_shots.cpp`):
  `docs/images/input/action-set-*`, `action-phases-*`, `input-players-*`, `glyph-sheet-*`, `rebinding-*`, `controller-station-*` (each at 1280x720 and 1920x1080).
- **Editor:** **View > Editors & Tools > Input Actions** (`editor/InputActionsWindow.hpp`) authors an asset: schemes, maps, actions with a type, bindings
  (single, 2D vector, 3D vector, 1D axis, chord) with group and processors, interactions, a Check tab (`ActionEditor::Validate`), a Live tab, Load / Save /
  New from default / Migrate Input Actions / Use in game. **View > Panels > Input** gained a "Controllers and typed actions" section (controller family,
  ids, mapping source, battery; every local player's actions with value, phase and the binding that drives it; "active" = held steady, no phase change).
  The logic is `Input/ActionEditor.hpp` (tested headless: `action_editor`); the ImGui drawing was only compile-checked, never run in a real editor window.
  Pictures: `docs/images/input/input-actions-window-{action,check,live}-<size>.png`, `input-panel-<size>.png` and `controllers-typed-actions-<size>.png` are drawn with
  the REAL ImGui code of those two headers in a headless software rasteriser (`tools/input_shots.cpp`, built by hand with `tools/input_shots.sh`; ImGui's default
  look and scalable font, not the editor's own skin; 1280x720 and 1920x1080). They are not screenshots of the running editor.
- **Not done yet:** the Inspector Player Input component, visual-script nodes for the `ia_*` calls, a ready in-game rebinding screen for this system (the existing `RemapModel` / keybind field cover `InputActions`),
  a hook that copies the default asset into new projects, touch scheme bindings (the scheme exists, no touch leaf inputs yet), and
  per-binding interactions (interactions are per action).

### Recording and replay (`InputRecorder`)
`InputRecorder::StartRecording()` ... `StopRecording()`, then `StartPlayback(InputRecorder::Text())`. It works inside
`InputActions::Tick`: recording copies the keys, mouse, wheel and every controller; playback puts the recorded frame back
over what the host fed and rebuilds the press and release edges. The text is plain and versioned (`okayinputrec 1`) and
carries a seed and a note so a bug report can say how to replay it. OkayScript: `input_record("start")`, `("stop")`,
`("play")`, `("status")`. Not yet: tying frames to the scene file and the game's random seed automatically (feature map
46.1 stays Planned for that part).

### The Input panel (editor)
**View > Panels > Input** (the Window menu in the editor): live keys, mouse, controllers and non-zero actions, the last
events (`Key Space down`, `Pad 2 A down`, `Action Jump down`), record / replay buttons and, per action, sliders for dead
zone, curve, sensitivity and invert with the curve drawn. It only shows input while the game plays.

### Speed
The action list is looked up through a hash map (it was a scan with two string copies per action), the per-frame
evaluation allocates nothing, and the held-key table is a fixed array (it was copied as a hash map every frame).
`OKAY_OLD_INPUT=1` restores the old lookup and evaluation order for comparison. Numbers: `docs/input_audit.md`.
