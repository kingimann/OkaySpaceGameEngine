# 8. Input

Polled keyboard, mouse, gamepad and touch state, named input actions with rebinding, cursor lock, rumble and gameplay input
blocking. Code is `engine/include/okay/Input/*` (5 headers) and `engine/src/Input/*` (about 700 lines); `player/main.cpp` and
`editor/main.cpp` feed SDL events in. Doc: `docs/input.md`. Merged: #571 (F-keys, left/right modifiers, input contexts,
dead zones, up to four controllers; in 2.849.0) and #574 (remap model for a Controls screen; in 2.850.0). Nothing is open here; the editor's Play mode still feeds only the old key set and one controller.

## 8. Input

### 8.1 Keyboard and mouse
- **What:** Polled key and mouse-button state (held, pressed this frame, released this frame), mouse position, delta and wheel, typed text, and a WASD and arrows axis, all fed by the host each frame.
- **Parts:**
  - 8.1.1 Keys - `Input/Input` - `GetKey`, `GetKeyDown`, `GetKeyUp`, `AnyKey`, `FirstKeyDown`, `KeyFromName`, `KeyName`; one `char` per key, synthetic codes for Shift, Ctrl and arrows
  - 8.1.2 Mouse - `Input/Input` - position, `MouseDelta` (first sample is not a jump), buttons 0-2, wheel via `FeedMouseWheel`
  - 8.1.3 Typed text - `Input/Input` - `TypedText` and `FeedText` carry real typed characters (shift, caps, symbols) from the OS text event
  - 8.1.4 Terminal fallback - `Input/Input` - raw non-blocking console keys when there is no window; no input in a headless run
  - 8.1.5 Extended keys (#571) - `Input/Input`, `Input/PlatformKeys` - F1-F12, Insert/Delete/Home/End/PageUp/PageDown, left and right Shift, Ctrl and Alt, `Input::KeyF(n)`; `PlatformKeys` (`AppendExtendedKeys`) feeds them from physical keys through `okay::platform` (the player includes it, `editor/main.cpp` does not; it replaced `SdlKeys`)
- **Code:** `Input/Input`, `engine/src/Input/Input.cpp`, `player/main.cpp` (SDL event feed), `editor/main.cpp` (Play-mode feed)
- **API:** `Input::GetKey`, `Input::GetKeyDown`, `Input::GetKeyUp`, `Input::AxisWASD`, `Input::MousePosition`, `Input::MouseDelta`, `Input::GetMouseButton`, `Input::MouseWheel`, `Input::TypedText`, `Input::FeedKeys`, `Input::FeedMouse`, script `Input.GetKey`, `key("space")`
- **Editor:** none (Play mode in the Game view feeds keys and mouse)
- **Docs:** `docs/input.md`
- **Tests:** `transform_input`, `core_audit_input`, `input_actions`, `input_keys_gamepad`
- **Status:** Works - letters, digits, space, enter, escape, backspace, tab, shift, ctrl, arrows and (since #571) F1-F12, the navigation block and left/right modifiers (Input.hpp read; the player feeds all of them through `PlatformKeys`)
- **Open:** the editor's Play mode does not include `PlatformKeys`, so function keys, Alt and left/right modifiers do not reach a game running in the editor (per grep: only `player/main.cpp` includes it); `KeyFromName` understands exact names such as "F1" and "Alt" now, an unknown name still falls back to its first letter
- **Related:** 8.3, 8.5, 12.3, 13.4
- **Words:** Input, GetKey, GetKeyDown, keyboard, mouse, mouse look, wheel, scroll, typed text, text input, key code, KeyFromName, press any key, WASD, arrow keys, function keys, F1, modifiers, Alt
- **Seen as:** a key does nothing in the game; F-keys or Alt cannot be bound; mouse-look jumps on the first frame; typed text misses capitals or symbols; holding two keys drops one
- **Ask me like:** "let me bind F5 to quick-save"; "the camera jumps when the game starts"

### 8.2 Gamepad (SDL)
- **What:** Game controllers (read through `okay::platform`: SDL2, or our own evdev backend) are read into Input: both sticks (radial dead zone 0.18), both triggers, 14 buttons plus trigger-as-button, and the left stick, d-pad and A also press W, A, S, D and Space so keyboard games play on a pad.
- **Parts:**
  - 8.2.1 Pad state - `Input/Input` - `GamepadAxis`, `GamepadRightAxis`, `GamepadTrigger`, `GetGamepadButton`(Down/Up), `GamepadConnected`, button names and `FirstGamepadButtonDown`
  - 8.2.2 Platform bridge - `Input/PlatformGamepad`, `Platform/Platform` - header-only `FeedPad` reads an `okay::platform::Gamepad` (no SDL types); the host picks the first controller and calls it each frame
  - 8.2.3 Multiple controllers, dead zones and curves (#571) - `Input/Input`, `Input/PlatformGamepad` - up to four players (`kMaxGamepads`), `GamepadSettings(player)` shape, dead zone, outer zone, curve, trigger threshold, `PadSet` (the player uses it; the editor still calls the single-pad `FeedPad`)
- **Code:** `Input/Input`, `Input/PlatformGamepad`, `engine/src/Platform/SdlBackend.cpp`, `engine/src/Platform/OwnLinuxEvdev.cpp`, `engine/src/Input/Input.cpp`, `player/main.cpp`, `editor/main.cpp`
- **API:** `Input::GamepadAxis`, `Input::GetGamepadButton`, `Input::PadA`, `Input::GamepadTrigger`, `Input::GamepadConnected`, `FeedPad`, `PadSet`, `platform::Gamepad`, script `Input.gamepadConnected`
- **Editor:** none (controller works in Play mode)
- **Docs:** `docs/input.md`
- **Tests:** `transform_input`, `input_actions`, `input_keys_gamepad`, `platform_gamepad`
- **Status:** Works - up to four controllers with per-player settings (Input.hpp read; `player/main.cpp` uses `PadSet`; tests `input_keys_gamepad` and, for the own evdev mapping, `platform`); the editor Play view still feeds one pad
- **Open:** the editor reads only one controller through `FeedPad`; the controller hot-plug events still come from each app's SDL event loop; no couch co-op example or settings screen for the dead zone and curve; no controller hint icons per brand
- **Related:** 8.3, 8.6, 12.4, 32.1
- **Words:** gamepad, controller, joystick, stick, dead zone, trigger, d-pad, Xbox, PlayStation, SDL_GameController, FeedGamepad, rumble, hot-plug, split screen
- **Seen as:** the character drifts with the stick untouched; the pad does nothing in the editor Play view; a second controller is ignored; triggers act as buttons too early or late
- **Ask me like:** "add a dead zone setting for the right stick"; "support two controllers for couch co-op"

### 8.3 Input actions and rebinding UI
- **What:** Named actions ("Jump", "Move", "Fire") that read keys, mouse and controller together, with text bindings, a direction type for sticks and WASD, and player rebinding saved in Prefs.
- **Parts:**
  - 8.3.1 Bindings - `Input/InputActions` - `InputBinding` text forms `key:`, `mouse:`, `pad:`, `keys:w,s,a,d`, `stick:`, `dpad`, `mouse:delta` with display labels
  - 8.3.2 Action table - `Input/InputActions` - `Define`, `Get`, `Down`, `Up`, `Value`, `Vector`, sampled once a frame by `Tick`; built-ins Move, Look, Jump, Sprint, Crouch, Fire, Aim, Interact, Reload, Inventory, Pause
  - 8.3.3 Rebinding and saving - `Input/InputActions` - `Rebind`, `Current`, `Reset`, `Load`, `Save` under `input.<action>` Prefs keys; only changed bindings are saved
  - 8.3.4 Keybind field - `Components/UIKeybindField` - a UI widget that shows an action's binding and listens for a new key or pad button
  - 8.3.5 Input contexts (#571) - `Input/InputActions` - `DefineIn`, `PushContext`, `PopContext` with Consume, Block and Passthrough modes so a menu takes keys from gameplay
  - 8.3.6 Remap model for a Controls screen (#574) - `Input/RemapModel` - rows with keyboard and gamepad columns, conflict detection (including four-key directions and the D-pad), `RemapBind` with cancel, swap, unbind or allow, reserved actions, on top of `InputActions`
- **Code:** `Input/InputActions`, `engine/src/Input/InputActions.cpp`, `Components/UIKeybindField`, `Input/RemapModel`
- **API:** `InputActions::Define`, `InputActions::Down`, `InputActions::Vector`, `InputActions::Rebind`, `InputBinding::Parse`, script `Input.GetButtonDown("Jump")`, `Input.GetActionVector("Move")`, `Input.RebindAction`, `action_down`, `rebind`; "UI Keybind Field" component
- **Editor:** Add Component > UI > UI Keybind Field (Input Action, Gamepad and Direction Key fields)
- **Docs:** `docs/input.md`
- **Tests:** `input_actions`, `ui_input_guard`, `input_keys_gamepad`, `focus_remap`
- **Status:** Works - InputActions.hpp and the defaults and rebind code in InputActions.cpp read; covered by `input_actions`; contexts (#571, `input_keys_gamepad`) and the remap model (#574, `focus_remap`) are on main
- **Open:** the pause menu pushes the Pause context in Block mode while it is open (other menus still use only `Input::SetUICaptured`, 8.5); no Controls screen template yet (`RemapModel` is the data side, 32.4); action names are case-insensitive strings, with no editor to author actions; no combos or chords
- **Related:** 8.2, 8.5, 12.3, 12.8, 32.4
- **Words:** input action, InputActions, rebinding, keybind, key binding, controls menu, remap, Jump, Fire, Interact, input map, input contexts, action map, binding text
- **Seen as:** a rebound key still triggers the old action; pressing the pause key also jumps; a rebinding is forgotten after restart; the same key works for two actions
- **Ask me like:** "let players rebind Interact on keyboard and gamepad"; "a menu that stops the jump key from working"

### 8.4 Cursor and touch
- **What:** Gameplay asks for the mouse cursor to be hidden and locked (shooters) and the host applies it; finger touches are fed from SDL finger events and finger drag-scrolling is provided for scroll and list views.
- **Parts:**
  - 8.4.1 Cursor lock and visibility - `Input/Cursor` - `Cursor::Capture`, `SetLock`, `SetVisible`; the player turns it into SDL relative mouse mode and releases it on Esc, focus loss or an open menu
  - 8.4.2 Touch list - `Input/Input` - `TouchCount`, `GetTouch` with began, moved, stationary, ended phases; `PointerIsTouch`, `TouchDragging` (10 px drag threshold); `FeedTouch`
  - 8.4.3 Touch scrolling - `Components/UITouchScroll` - finger drag and fling for Scroll Views and List Views
- **Code:** `Input/Cursor`, `Input/Input`, `Components/UITouchScroll`, `player/main.cpp` (cursor and finger handling), `editor/main.cpp`
- **API:** `Cursor::Capture`, `Cursor::SetLock`, `Cursor::SetVisible`, `Cursor::IsLocked`, `Input::TouchCount`, `Input::GetTouch`, `Input::PointerIsTouch`
- **Editor:** none
- **Docs:** `docs/input.md`, `docs/web_mobile.md`
- **Tests:** `ui_nav_touch`, `uicapture_look`, `ui_touch_scroll`
- **Status:** Works for the cursor and single or multi touch lists (headers and the player's finger and cursor code read; touch list and drag-scroll covered by `ui_nav_touch`)
- **Open:** no gesture recognition (pinch, swipe, long press, rotate); `docs/web_mobile.md` still says to read `SDL_FINGER*` yourself although the player and the editor already call `Input::FeedTouch`; no on-screen virtual stick or touch buttons
- **Related:** 8.5, 12.9, 18.2, 37.1
- **Words:** cursor, lock cursor, hide cursor, mouse capture, relative mouse, touch, touchscreen, finger, tap, drag, fling, mobile, phone, multi-touch, gestures, pinch
- **Seen as:** the cursor stays visible during mouse-look or escapes the window; a tap does nothing on a phone; scrolling a list with a finger also presses buttons; the cursor does not come back after Esc
- **Ask me like:** "add pinch to zoom"; "put an on-screen joystick on mobile"

### 8.5 Gameplay input blocking
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
- **What:** Games request controller rumble with a heavy and a light motor strength and a duration; the host plays the latest request on the connected pad.
- **Parts:**
  - 8.6.1 Rumble request - `Input/Input` - `Input::Rumble(low, high, seconds)` and `TakeRumble`
  - 8.6.2 SDL playback - `Input/SdlGamepad` - `FeedSdlGamepad` forwards the request to `SDL_GameControllerRumble`, and drops it when no pad is connected
- **Code:** `Input/Input`, `Input/SdlGamepad`
- **API:** `Input::Rumble`, `Input::TakeRumble`, script `Input.Rumble(low, high, seconds)`, `rumble(low, high, seconds)`
- **Editor:** none
- **Docs:** `docs/input.md`
- **Tests:** -
- **Status:** Basic - request and playback code read; no test file covers rumble
- **Open:** no rumble on touch devices; `Input::Rumble` takes a player index since #571 (the editor feeds only one pad); no haptic pattern assets or intensity setting in the settings screen
- **Related:** 8.2, 11.7, 32.1
- **Words:** rumble, vibration, haptics, force feedback, controller shake, Rumble, low motor, high motor
- **Seen as:** the controller does not shake on a hit or explosion; vibration cannot be turned off by the player
- **Ask me like:** "shake the controller when the player takes damage"; "add a vibration on/off setting"
