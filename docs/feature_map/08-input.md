# 8. Input

One input system (owner, 2026-11-23: "combine it so you have one"): polled keyboard, mouse, gamepad and touch state, ONE
table of named actions with ONE rebinding store (the `.okayinput` asset plus the actions code defines; `InputActions`, the
typed `ActionSet` calls, local players and both Controls screens all read and change it), cursor lock, rumble and gameplay
input blocking. Code is `engine/include/okay/Input/*` (22 headers) and `engine/src/Input/*` (18 files, about 5,400 lines); `player/main.cpp` and
`editor/main.cpp` feed platform events in (`okay::platform`). Doc: `docs/engine/input.md`. Merged: #571 (F-keys, left/right modifiers, input contexts,
dead zones, several controllers; in 2.849.0) and #574 (remap model for a Controls screen; in 2.850.0). Input wiring (`claude/input-wiring-1`, test `input_wiring`): a built game loads the project's `Assets/input.okayinput`, bound gestures drive actions, mouse side buttons and the sideways wheel, the Guide button, punctuation / numpad / lock keys, eight controllers, controller toasts. The editor's Play feeds the same keys and every controller (`PlatformKeys`, `PadSet`); it does not feed the mouse side buttons or the sideways wheel yet.

## 8. Input

### 8.1 Keyboard and mouse
- **System:** Input - The engine's one input system: reads the keyboard, mouse, gamepads and touch, turns them into named actions the player can rebind (one action table, one rebinding store, for one or several local players), and plays controller rumble.
  Home: `engine/include/okay/Input/`, `engine/src/Input/`. Entry: `engine/include/okay/Input/Input.hpp`. Also called: keyboard, mouse, gamepad, GameInput, PlatformGamepad, rumble.
- **What:** Polled key and mouse-button state (held, pressed this frame, released this frame), mouse position, delta and wheel, typed text, and a WASD and arrows axis, all fed by the host each frame.
- **Parts:**
  - 8.1.1 Keys - `Input/Input` - `GetKey`, `GetKeyDown`, `GetKeyUp`, `AnyKey`, `FirstKeyDown`, `KeyFromName`, `KeyName`; one `char` per key, synthetic codes for Shift, Ctrl and arrows
  - 8.1.2 Mouse - `Input/Input` - position, `MouseDelta` (first sample is not a jump), buttons 0-4 (left, right, middle and the two side buttons: `mouse:x1`, `mouse:x2`, shown as "Mouse 4" and "Mouse 5"), wheel via `FeedMouseWheel` and the sideways wheel via `FeedMouseWheelX` (`wheel:left`, `wheel:right`); the player feeds them through `InputMouseMask` / `InputMouseButton` (`Input/PlatformKeys`), the editor's Play still feeds buttons 0-2 and the up / down wheel only
  - 8.1.3 Typed text - `Input/Input` - `TypedText` and `FeedText` carry real typed characters (shift, caps, symbols) from the OS text event
  - 8.1.4 Terminal fallback - `Input/Input` - raw non-blocking console keys when there is no window; no input in a headless run
  - 8.1.5 Extended keys (#571) - `Input/Input`, `Input/PlatformKeys` - F1-F12, Insert/Delete/Home/End/PageUp/PageDown, left and right Shift, Ctrl and Alt, `Input::KeyF(n)`; `PlatformKeys` (`AppendExtendedKeys`) feeds them from physical keys through `okay::platform` (the player and the editor's Play both call it; it replaced `SdlKeys`); since the input wiring also punctuation by name (`minus`, `comma`, `leftbracket` ..., fed as the US character of the key's place), the numpad (`kp0`..`kp9`, `kpdivide`, `kpmultiply`, `kpminus`, `kpplus`, `kpperiod`, and `kpenter` as its own key that still also reads as Enter), Caps Lock, Print Screen, Scroll Lock, Pause, Num Lock and Menu, with new codes 154..175 (no existing code moved)
- **Code:** `Input/Input`, `engine/src/Input/Input.cpp`, `player/main.cpp` (SDL event feed), `editor/main.cpp` (Play-mode feed)
- **API:** `Input::GetKey`, `Input::GetKeyDown`, `Input::GetKeyUp`, `Input::AxisWASD`, `Input::MousePosition`, `Input::MouseDelta`, `Input::GetMouseButton`, `Input::MouseWheel`, `Input::TypedText`, `Input::FeedKeys`, `Input::FeedMouse`, script `Input.GetKey`, `key("space")`
- **Editor:** none (Play mode in the Game view feeds keys and mouse)
- **Docs:** `docs/engine/input.md`
- **Tests:** `transform_input`, `core_audit_input`, `input_actions`, `input_keys_gamepad`, `input_wiring`
- **Status:** Works - letters, digits, space, enter, escape, backspace, tab, shift, ctrl, arrows and (since #571) F1-F12, the navigation block and left/right modifiers (Input.hpp read; the player feeds all of them through `PlatformKeys`)
- **Open:** keys by position for AZERTY exist (`Input::SetKeysByPosition`, off by default, no Settings toggle yet); an arrow key still reads as W, A, S or D for the raw reads (`GetKey('w')`, scripts), but bindings read `Input::GetKeyGameExact` since claude/input-wiring-1, so `key:w` no longer fires on Up and the arrows bind on their own (an old asset that binds only W A S D is not migrated); a tap shorter than a frame is only caught for the main keys, not the new ones; `KeyFromName` understands exact names such as "F1" and "Alt" now, an unknown name still falls back to its first letter
- **Related:** 8.3, 8.5, 12.3, 13.4
- **Words:** Input, GetKey, GetKeyDown, keyboard, mouse, mouse look, wheel, scroll, typed text, text input, key code, KeyFromName, press any key, WASD, arrow keys, function keys, F1, modifiers, Alt
- **Seen as:** a key does nothing in the game; F-keys or Alt cannot be bound; mouse-look jumps on the first frame; typed text misses capitals or symbols; holding two keys drops one
- **Ask me like:** "let me bind F5 to quick-save"; "the camera jumps when the game starts"

### 8.2 Gamepad (SDL)
- **System:** Input
- **What:** Game controllers (read through `okay::platform`: SDL2, or our own evdev backend) are read into Input: both sticks (radial dead zone 0.18), both triggers, 14 buttons plus trigger-as-button and the Guide button (`Input::PadGuide`, `pad:guide`, named "PS", "Home", "Steam" or "Guide" by pad family; the system may keep that button for itself), and the left stick, d-pad and A also press W, A, S, D and Space so keyboard games play on a pad.
- **Parts:**
  - 8.2.1 Pad state - `Input/Input` - `GamepadAxis`, `GamepadRightAxis`, `GamepadTrigger`, `GetGamepadButton`(Down/Up), `GamepadConnected`, button names and `FirstGamepadButtonDown`
  - 8.2.2 Platform bridge - `Input/PlatformGamepad`, `Platform/Platform` - header-only `FeedPad` reads an `okay::platform::Gamepad` (no SDL types); the host picks the first controller and calls it each frame
  - 8.2.3 Multiple controllers, dead zones and curves (#571) - `Input/Input`, `Input/PlatformGamepad` - up to eight players (`kMaxGamepads` = 8, the same as `PlayerInputManager::kMaxPlayers`, so players 5 to 8 get a pad: `pad0`..`pad7`, `pad:a@7`), `GamepadSettings(player)` shape, dead zone, outer zone, curve, trigger threshold, `PadSet` (the player and the editor's Play both use it)
  - 8.2.4 Controller families and generic pads - `Input/GamepadInfo`, `Input/ControllerMapping`, `Input/PadProfiles`, `Input/PadSlots` - classifier from USB ids / name / host type (Xbox 360, One, Series, DualShock, DualSense, Switch, Steam, generic, unknown), generic layout from raw axes / buttons / hats, "Controller Setup" with a saved per-model remap, button names per family (Cross or A, Create or View), stable slots on hot-plug, connect and disconnect events shown as a toast (with no `SetToastHandler` the text goes to the host's UI through `GetScriptUI()->Toast`: OkayUI's toasts in the player and the editor's Play; pads already plugged in when the player starts are not announced), battery, lightbar, trigger rumble, touchpad and gyro where SDL exposes them; written from public USB id facts; tests use fake devices only (`controller_mapping`); try it: input playground station H
- **Code:** `Input/Input`, `Input/PlatformGamepad`, `engine/src/Platform/SdlBackend.cpp`, `engine/src/Platform/OwnLinuxEvdev.cpp`, `engine/src/Input/Input.cpp`, `player/main.cpp`, `editor/main.cpp`
- **API:** `Input::GamepadAxis`, `Input::GetGamepadButton`, `Input::PadA`, `Input::GamepadTrigger`, `Input::GamepadConnected`, `FeedPad`, `PadSet`, `platform::Gamepad`, script `Input.gamepadConnected`
- **Editor:** none (controller works in Play mode)
- **Docs:** `docs/engine/input.md`
- **Tests:** `transform_input`, `input_actions`, `input_keys_gamepad`, `platform_gamepad`, `input_wiring`
- **Status:** Works - up to eight controllers with per-player settings (`player/main.cpp` and `editor/main.cpp` use `PadSet`; tests `input_keys_gamepad`, `input_wiring` (eight fake pads joined, Guide, toasts) and, for the own evdev mapping, `platform`); more than four real controllers at once was never tried
- **Open:** Share / Misc, paddles and the touchpad click are not read (the platform layer's `GamepadButton` has no such buttons); the controller hot-plug events still come from each app's SDL event loop; no couch co-op example or settings screen for the dead zone and curve; no controller hint icons per brand (text names per family exist now, see 8.2.4; no artwork); DualSense adaptive triggers and HD haptics are not exposed (SDL has no plain call); the family tables, generic layout and Controller Setup were only run with fake devices, never with a real PS5, Xbox or other pad or on Windows
- **Related:** 8.3, 8.6, 12.4, 32.1
- **Words:** gamepad, controller, joystick, stick, dead zone, trigger, d-pad, Xbox, PlayStation, SDL_GameController, FeedGamepad, rumble, hot-plug, split screen
- **Seen as:** the character drifts with the stick untouched; the pad does nothing in the editor Play view; a second controller is ignored; triggers act as buttons too early or late
- **Ask me like:** "add a dead zone setting for the right stick"; "support two controllers for couch co-op"

### 8.3 Input actions and rebinding UI
- **System:** Input Actions - Named actions such as Jump and Fire that work with any key or button, can be rebound by the player, and support several local players (one action table, one rebinding store).
  Home: `engine/include/okay/Input/`, `engine/src/Input/`. Entry: `engine/include/okay/Input/ActionSet.hpp`. Also called: ActionSet, InputAsset, PlayerInput, InputActions, rebinding.
- **What:** Named actions ("Jump", "Move", "Fire") that read keys, mouse and controller together, with text bindings, a direction type for sticks and WASD, and player rebinding saved in Prefs.
- **Parts:**
  - 8.3.1 Bindings - `Input/InputActions` - `InputBinding` text forms `key:`, `mouse:`, `pad:`, `keys:w,s,a,d`, `stick:`, `dpad`, `mouse:delta` with display labels
  - 8.3.2 Action table - `Input/InputActions` - `Define`, `Get`, `Down`, `Up`, `Value`, `Vector`, sampled once a frame by `Tick`; built-ins Move, Look, Jump, Sprint, Crouch, Fire, Aim, Interact, Reload, Inventory, Pause
  - 8.3.3 Rebinding and saving - `Input/InputActions` - `Rebind`, `Current`, `Reset`, `Load`, `Save` under `input.<action>` Prefs keys; only changed bindings are saved
  - 8.3.9 One input system (#422 duplicate audit item 2; owner 2026-11-23) - `Input/InputRuntime`, `Input/ActionSet`, `Input/InputActions` - there is ONE action table, `InputRuntime::Actions()` (an `ActionSet`): the `.okayinput` asset in use plus the actions code defines (the built-in defaults only add what the asset lacks; a game's own `Define` replaces the asset's bindings for that name). `InputActions` is its name-based front: it keeps no actions, bindings or rebinding of its own (the old separate table, its second "legacy" evaluation and the #1139 sync code are deleted). Input contexts are the table's action maps (`PushMap` / `PopMap`, a map can consume or block). One evaluation (`ActionSet::Update`): sum or strongest binding, chords keeping their keys, the player's response, per-controller reads, on-screen contributions, hold-to-toggle, press timing; no allocation per frame. Player 0's typed set has the table's bindings (copied on every change); players 1+ keep their own rebinding. One rebinding store: `inputov.player0` (a reshaped list is one `ovlist` line; lines for another asset's actions are kept) and, for every action the name-based front can show, also the old `input.<action>` form, so earlier saves and builds still load it. Old names all forward here: `InputActions::*`, script `action*` / `rebind` / `ia_*`, engine API `Input.GetButton*` / `Input.Player*`, the "UI Keybind Field" (`ia:` or plain), both Controls screens; tests `input_one_store` (blocks 13-15: one table, the same key through every old name, a rebind seen by all and kept after a restart, the pause Block reaching the typed player)
  - 8.3.4 Keybind field - `Components/UIKeybindField` - a UI widget that shows an action's binding and listens for a new key or pad button
  - 8.3.5 Input contexts (#571) - `Input/InputActions` - `DefineIn`, `PushContext`, `PopContext` with Consume, Block and Passthrough modes so a menu takes keys from gameplay
  - 8.3.6 Remap model for a Controls screen (#574) - `Input/RemapModel` - rows with keyboard and gamepad columns, conflict detection (including four-key directions and the D-pad), `RemapBind` with cancel, swap, unbind or allow, reserved actions, on top of `InputActions`
  - 8.3.7 Typed actions, maps and local players - `Input/ActionSet`, `Input/InputAsset`, `Input/PlayerInput`, `Input/InputRuntime`, `Input/InputBuffer` - value types (button, 1D, 2D, 3D), composites (vector from four inputs, axis from two, chords), processors (deadzone, invert, scale, normalize, swizzle, smooth, curve, clamp), control schemes with auto switch, interactions with started / performed / canceled phases (press, release, hold, tap, multi-tap, repeat, chorded), action maps with priority and consume, the `.okayinput` text asset with a shipped default and a migration of the old table, local players with device pairing, runtime rebinding with conflicts and per-player overrides, input buffer and coyote time, and `ia_*` script calls; tests `action_set`, `action_phases`, `input_asset`, `input_script_api`, `action_editor`; editor: Input Actions window (author, check, live) and a section in the Input panel (`Input/ActionEditor`, `editor/InputActionsWindow.hpp`; ImGui part not run in a real window); Player Input component (`LocalPlayerInput`, Add Component, Inspector), `Input.Player*` engine-API nodes, and a Controls screen on typed actions (`UIGallery::AddControlsScreen`, `UIKeybindField` on `ia:<action>@<player>`); tests `local_player_input`, `controls_screen`; a built game uses the project's asset too: the export ships `Assets/input.okayinput` into the data folder even when no scene names it (`editor/BuildGame.cpp`) and the player calls `InputRuntime::UseProjectAsset` with that folder at start-up, as the editor's Play does with the project folder (test `input_wiring`; not run as a real exported game)
  - 8.3.10 Review fixes on the typed actions (#1225, 2026-11-24; replaces Grok's #1208) - `Input/ActionSet`, `Input/Gestures`, `Input/InputActions` - an axial dead zone processor (`axial(0.2)`, per axis, through `Input::ProcessStick`) so a forward stick with a little sideways bias does not strafe; `Buffered`, `Consume`, `DoubleTap`, `HeldFor` and `HoldReached` on each player's own `ActionSet` (`InputActions` forwards to it); `smooth` keeps one memory per controller; a player's dead zone, curve, sensitivity and invert are saved with the rebinding (`ovresp` lines, names may hold spaces); `Gestures::Bind("swipe up", "Jump")` lets a gesture press an action; a finger move after a press is reported as a move, so swipes reach the gesture reader; test `input_grok`
  - 8.3.8 Built-in controllers on actions - `Input/InputActions` - `Rebound`, `ControlKey`, `ControlKeyDown`, `ControlMouse`, `ControlMouseDown`, `ControlMove`: a controller key field reads its key until the player rebinds the matching action, then follows the action; used by the first / third person, character 2D / 3D, top-down, shooter and 2.5D controllers (2.5D layer keys follow "Move" up / down: `ControlDirKeyDown`; grab follows "Interact"), `StanceControl`, ranged and melee weapons, item pickup, vehicle seat, Free Roam (rise = Jump, Ctrl = Crouch, Sprint, Move), the Top Down Shooter dash ("Dash") and lean / prone ("LeanLeft", "LeanRight", "Prone"); Vehicle Controller driving keys use "Throttle", "Brake", "SteerLeft", "SteerRight", "Handbrake", "Boost", "GearUp", "GearDown", "Horn", "Headlights" in the "Vehicle" context (`ControlAxis`; an action in a context that is not pushed is read straight from its bindings: `DirectHeld`, `DirectDown`); the Pause Menu reads a rebound "Pause" past its own Block context (`MenuKeyDown`); the Controls screen reports a shared key only inside one context; test `controller_actions`
- **Code:** `Input/InputActions`, `engine/src/Input/InputActions.cpp`, `Components/UIKeybindField`, `Input/RemapModel`
- **API:** `InputActions::Define`, `InputActions::Down`, `InputActions::Vector`, `InputActions::Rebind`, `InputBinding::Parse`, script `Input.GetButtonDown("Jump")`, `Input.GetActionVector("Move")`, `Input.RebindAction`, `action_down`, `rebind`; "UI Keybind Field" component
- **Editor:** Add Component > UI > UI Keybind Field (Input Action, Gamepad and Direction Key fields)
- **Docs:** `docs/engine/input.md`
- **Tests:** `input_actions`, `ui_input_guard`, `input_keys_gamepad`, `focus_remap`, `controller_actions`, `input_one_store`, `input_grok`, `input_wiring`
- **Status:** Works - one action table and one rebinding store since the merge (8.3.9); 30 input, UI and controller suites green on Linux (`input_one_store` 638 checks); contexts (#571, `input_keys_gamepad`) and the remap model (#574, `focus_remap`) are on main
- **Open:** still on raw keys: builders and some UI widgets; no editor for the code-defined actions (they show in the Input panel and the Input Actions window's live view only); a typed binding the name-based front cannot show (a pad button inside the Move keys, a 1D axis, a 3D vector) is read but listed as "None" on the older `RemapModel` Controls screen; pushing the "UI" context now also turns on the asset's UI map (Navigate, Submit, Cancel take W A S D, Space, Enter and Escape from gameplay), use another context name for a menu that should only take its own keys; the merge changed the shipped asset's Move and Look stick bindings (the second dead zone and Look's curve(2) are gone, `Input` already applies the stick dead zone) and set `sum=1` on Move and Look so keys and a stick add up as the built-in controllers always read them; not run on Windows or with a real controller
- **Related:** 8.2, 8.5, 12.3, 12.8, 32.4
- **Words:** input action, InputActions, rebinding, keybind, key binding, controls menu, remap, Jump, Fire, Interact, input map, input contexts, action map, binding text
- **Seen as:** a rebound key still triggers the old action; pressing the pause key also jumps; a rebinding is forgotten after restart; the same key works for two actions
- **Ask me like:** "let players rebind Interact on keyboard and gamepad"; "a menu that stops the jump key from working"

### 8.4 Cursor and touch
- **System:** Input
- **What:** Gameplay asks for the mouse cursor to be hidden and locked (shooters) and the host applies it; finger touches are fed from SDL finger events and finger drag-scrolling is provided for scroll and list views.
- **Parts:**
  - 8.4.1 Cursor lock and visibility - `Input/Cursor` - `Cursor::Capture`, `SetLock`, `SetVisible`; the player turns it into SDL relative mouse mode and releases it on Esc, focus loss or an open menu
  - 8.4.2 Touch list - `Input/Input` - `TouchCount`, `GetTouch` with began, moved, stationary, ended phases; `PointerIsTouch`, `TouchDragging` (10 px drag threshold); `FeedTouch`
  - 8.4.3 Touch scrolling - `Components/UITouchScroll` - finger drag and fling for Scroll Views and List Views
- **Code:** `Input/Cursor`, `Input/Input`, `Components/UITouchScroll`, `player/main.cpp` (cursor and finger handling), `editor/main.cpp`
- **API:** `Cursor::Capture`, `Cursor::SetLock`, `Cursor::SetVisible`, `Cursor::IsLocked`, `Input::TouchCount`, `Input::GetTouch`, `Input::PointerIsTouch`
- **Editor:** none
- **Docs:** `docs/engine/input.md`, `docs/platform/web_mobile.md`
- **Tests:** `ui_nav_touch`, `uicapture_look`, `ui_touch_scroll`, `input_wiring`
- **Status:** Works for the cursor and single or multi touch lists (headers and the player's finger and cursor code read; touch list and drag-scroll covered by `ui_nav_touch`)
- **Open:** none of the gesture or on-screen control gaps. Tap, double tap, long press, swipe, pinch and two-finger rotate are recognised (`Input/GestureRecognizer`, `Gestures`). A gesture drives an action two ways: `Gestures::Bind(gesture, action)` from code (several actions per gesture, `Unbind`; `InputActions::Tick` applies them, not saved) or a `gesture:swipeup` binding of the action, saved with the `.okayinput` asset like any binding (test `input_wiring`). A virtual joystick, D-pad and touch button drive the action map and can hide until a touch screen is present (`Components/TouchControls`). `docs/engine/input.md` and `docs/platform/web_mobile.md` describe them.
- **Related:** 8.5, 12.9, 18.2, 37.1
- **Words:** cursor, lock cursor, hide cursor, mouse capture, relative mouse, touch, touchscreen, finger, tap, drag, fling, mobile, phone, multi-touch, gestures, pinch
- **Seen as:** the cursor stays visible during mouse-look or escapes the window; a tap does nothing on a phone; scrolling a list with a finger also presses buttons; the cursor does not come back after Esc
- **Ask me like:** "add pinch to zoom"; "put an on-screen joystick on mobile"

### 8.5 Gameplay input blocking
- **System:** Input
- **What:** One place that answers "should the world ignore this click or key right now?": pause menu, open bag, chest, crafting, a focused text field, modal, dialogue, a pointer on UI or a cutscene.
- **Parts:**
  - 8.5.1 Block masks - `Input/GameInput` - `GameInputMask` with `World` (clicks, swings, digging) and `Keys` (hotbar, bag toggle) presets; `GameInputBlocked`, `GameInputBlockReason`
  - 8.5.2 Interact key arbitration - `Input/GameInput` - `InteractClaimsKey` lets a pickup or hold-prompt drop own E before the bag toggle
  - 8.5.3 UI capture flag - `Input/Input` - `Input::UICaptured` and `SetUICaptured` freeze mouse-look and movement while a modal UI owns the pointer
- **Code:** `Input/GameInput`, `engine/src/Input/GameInput.cpp`, `Input/Input`
- **API:** `GameInputBlocked`, `GameInputMask`, `GameInputBlockReason`, `InteractClaimsKey`, `Input::UICaptured`, `Input::SetUICaptured`
- **Editor:** none
- **Docs:** -
- **Tests:** `ui_input_guard`, `uicapture_look`
- **Status:** Works - GameInput.cpp read (it queries the scene's inventory, chest, crafting, modal, dialogue and sequence components directly); covered by `ui_input_guard` and `uicapture_look`
- **Open:** no doc; the mask is a fixed list of known components, so a game's own menu must call `Input::SetUICaptured`; this is separate from input contexts (8.3.5)
- **Related:** 8.3, 11.1, 10.2, 10.1, 12.8
- **Words:** UICaptured, game input blocked, ignore clicks, world click, swallow input, input guard, modal, pause blocks input, pointer over UI
- **Seen as:** clicking a menu button also swings the weapon or digs; the player keeps walking while a bag or dialogue is open; E picks up and opens the bag at once
- **Ask me like:** "stop the player from shooting while my shop menu is open"; "why does the bag toggle when I pick something up?"

### 8.6 Rumble and haptics
- **System:** Input
- **What:** Games request controller rumble with a heavy and a light motor strength and a duration; the host plays the latest request on the connected pad.
- **Parts:**
  - 8.6.1 Rumble request - `Input/Input` - `Input::Rumble(low, high, seconds)` and `TakeRumble`
  - 8.6.2 Playback - `Input/PlatformGamepad` - `FeedPadAt` takes the request and calls `platform::Gamepad::Rumble` (SDL2 or our own evdev backend), and drops it when no pad is connected
- **Code:** `Input/Input`, `Input/PlatformGamepad`
- **API:** `Input::Rumble`, `Input::TakeRumble`, script `Input.Rumble(low, high, seconds)`, `rumble(low, high, seconds)`
- **Editor:** none
- **Docs:** `docs/engine/input.md`
- **Tests:** `input_deep`, `input_playground`
- **Status:** Works in headless tests - request, patterns (`InputHaptics`) and the vibration setting (scales every `Input::Rumble`, 0 = off) are covered; real controller playback unverified
- **Open:** no rumble on touch devices; `Input::Rumble` takes a player index since #571; no vibration slider in a Settings screen yet (the setting is `haptic_strength`, kept in Prefs)
- **Related:** 8.2, 11.7, 32.1
- **Words:** rumble, vibration, haptics, force feedback, controller shake, Rumble, low motor, high motor
- **Seen as:** the controller does not shake on a hit or explosion; vibration cannot be turned off by the player
- **Ask me like:** "shake the controller when the player takes damage"; "add a vibration on/off setting"

### 8.7 Input upgrade: timing, curves, per-player actions, recording, the Input panel
- **System:** Input
- **What:** Fixes found by the input audit (fast taps, typing moving the player, layouts without Latin letters, rebinding Move) and new features built on 8.1-8.3: chord and wheel bindings, per-action dead zone / curve / sensitivity / invert, input buffering and double tap and hold-to-confirm, per-player actions, rebind conflicts and swap, profiles and one-handed presets, controller picture names by family, the last-used device, input recording and replay, and an editor Input panel.
- **Parts:**
  - 8.7.1 Fast taps and typing - `Input/Input` - `FeedKeyTap`, `FeedMouseTap` (a press shorter than a poll still counts); `SetTextFocused`, `GetKeyGame`, `GetKeyDownGame` (a text field keeps its keys from the game); `Touch::began`
  - 8.7.2 Layout fallback - `Input/PlatformKeys` - `KeyForLetter`, `TapCharsForKey` (layouts with no Latin letters use the US key positions)
  - 8.7.3 Chords, wheel, response - `Input/InputActions` - `chord:ctrl+z`, `wheel:up`, `SetResponse`, `ShapeValue`; a held chord keeps its keys from single-key bindings
  - 8.7.4 Timing - `Input/InputActions` - `Buffered`, `Consume`, `DoubleTap`, `HeldFor`, `HoldProgress`, `HoldReached`, `Tick(dt)`
  - 8.7.5 Players, rebinding, profiles - `Input/InputActions` - `SetPerPlayer`, `Get(name, player)`, `ExportBindings`, `SaveProfile`, `ApplyPreset` (conflicts and swap are the existing `RemapConflicts` / `RemapBind`, now chord- and wheel-aware)
  - 8.7.6 Pictures and device - `Input/Input` - `LastDevice`, `GamepadFamily`, `GamepadButtonLabel`, `FamilyFromName`; `InputBinding::Label(family)`
  - 8.7.7 Recording - `Input/InputRecorder` - `StartRecording`, `StartPlayback`, `Text`; plain versioned text with a seed and a note
  - 8.7.8 Debugger - `Input/InputDebug`, `editor/InputDebugger.hpp`, `okayui/screens/OkayUIEditorPanels.cpp` (drawn by OkayUI only) - the live state and the last events; Window > Input in the editor
  - 8.7.9 Rumble patterns - `Input/Haptics` - `InputHaptics::Play("heartbeat", player)`, `SetStrength`; steps become `Input::Rumble` requests
  - 8.7.10 Speed - `Input/ActionSet` - sorted name index, no per-frame allocation (input ids instead of strings for what a map swallows, an insertion sort of the maps); `OKAY_OLD_INPUT=1` and `SetLegacy` are kept as names only, there is one evaluation (8.3.9)
- **Code:** `Input/Input`, `Input/InputActions`, `Input/InputRecorder`, `Input/InputDebug`, `Input/Haptics`, `Input/PlatformKeys`, `engine/src/Input/*.cpp`, `editor/InputDebugger.hpp`, `player/main.cpp`, `editor/main.cpp`, `examples/input/input_playground.okayscene`
- **API:** `Input::FeedKeyTap`, `Input::SetTextFocused`, `InputActions::SetResponse`, `InputActions::Buffered`, `InputActions::Consume`, `InputActions::SetPerPlayer`, `InputRecorder::StartRecording`, `InputDebug::Enable`, `InputHaptics::Play`, script `action_buffered`, `action_consume`, `action_double_tap`, `action_hold`, `set_response`, `action_label`, `input_device`, `input_record`
- **Editor:** Window > Input (live keys, mouse, controllers, actions, last events, record and replay, response curves); `examples/input/input_playground.okayscene`
- **Docs:** `docs/engine/input.md`, `docs/engine/input.md`, `docs/try_it.md` row 7x
- **Tests:** `input_deep`, `input_playground`, `input_bench`, `input_actions`, `input_keys_gamepad`
- **Status:** Works headless - 207 checks in `input_deep`, the shipped scene run by `input_playground`; not run in a real window, with a real controller or on a real keyboard layout
- **Open:** AZERTY physical positions (ZQSD) are not an option; the vibration strength is not on a Settings screen; no gyro; recording does not yet tie frames to the scene file and the game's random seed (46.1); the Input panel is drawn by OkayUI and its screen runs in the `okayui_editor_panels` test, but the editor window itself was not clicked through; the old "pressed between polls" loss is fixed only where the host forwards key events (player and editor Play)
- **Related:** 8.1, 8.2, 8.3, 8.6, 32.4, 46.1
- **Words:** input buffer, jump buffer, coyote, double tap, hold to confirm, chord, combo, Ctrl+Z, mouse wheel binding, dead zone, response curve, sensitivity, invert Y, local multiplayer, two players, controller glyphs, PlayStation buttons, record input, replay input, input debugger, one-handed, typing moves the player
- **Seen as:** a very quick tap is lost; typing in a text field also moves the player; W A S D dead on a Russian keyboard; Ctrl+Z also crouches; a rebound Move stops working; jump pressed just before landing is ignored
- **Ask me like:** "buffer the jump so it still works just before landing"; "show PlayStation button pictures when a PlayStation pad is used"; "record my input so I can replay a bug"
