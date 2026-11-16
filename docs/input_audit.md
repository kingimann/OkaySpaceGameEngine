# Input audit

Branch `claude/input-upgrade`. Owner ask: "look at the input system, upgrade it and improve it".

This page says, in plain words, what happens to a key press, a mouse move, a controller stick, a touch and a
gesture on the way from the operating system to a script, a controller (the player's own movement code) and a UI
widget. For each case it says **ok**, **bug** or **limitation**, with the headless test that shows it.

Everything below was run headless on Linux (no window, no real keyboard, no real controller). The test file is
`tests/test_input_deep.cpp` (suite `input_deep`). Real keyboard layouts, real controllers and the Windows build were
**not** tried. Each "unverified" says so.

## How input travels

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

## Findings

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
| 12 | AZERTY: the key **labelled** W is "W" (bottom left on AZERTY), not the key in the W position (Z). That is what `KeyForChar` does on purpose | limitation | none (needs a real layout) | open: an option to read physical positions (ZQSD) is not built. Row in `docs/UNFINISHED.md` |
| 13 | Gamepad rumble keeps one pending request per controller; a new one replaces the old; there are no patterns | limitation | - | open |
| 14 | No gyro or touch-pad input in the platform layer | limitation | - | open |

## Cases checked and found ok

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

## Not tested here, and why (unverified)

* **Real windows and real devices.** Stuck keys when the window loses focus, key repeat, mouse capture and relative
  motion, high-DPI scaling, controller hot-plug in the real backend, and every real keyboard layout need a real
  machine. The code paths were read, not run. SDL clears its key state on focus loss, so a stuck key is not
  expected; this was not tried.
* **Input during a loading screen and in the editor Play view versus the Scene view.** The editor feeds the game
  only while it plays and only when the pointer decision says so (`DecidePlayPointer`, covered by earlier tests);
  the new tap and text-focus hooks sit inside that same branch. Not run in a real editor window.
* **The editor and player programs were not built.** The engine code and tests were. `editor/main.cpp` and
  `player/main.cpp` edits were syntax-checked only (see the report).

## Frame-rate numbers

Measured with `scripts`-free micro benchmark (the same program compiled against `origin/main` and against this
branch): 101 actions (11 built in plus 89 added with three bindings each), 4 controllers fed, one context pushed,
`-O2`, a busy shared machine (so treat the absolute numbers as rough; the ratio is what matters).

@@NUMBERS@@

`OKAY_OLD_INPUT=1` (or `InputActions::SetLegacy(true)`) brings back the old lookup and the old evaluation order
for A/B. The test `upgrade_fast_path_matches_legacy` runs 600 random frames (keys, mouse, 4 controllers, chords,
contexts, per-player actions) through both and requires every action value, vector and edge to match.

## What was built from these findings

See `docs/input.md` ("The input upgrade"): chords, wheel bindings, per-action dead zone / curve / sensitivity /
invert (saved), jump buffering and consume, double tap, hold to confirm, per-player actions, rebind profiles and presets, controller picture names by device family (Xbox, PlayStation, Switch), the
last-used device, input recording and replay, and the Input panel in the editor.

## What was written from what

Everything is new code written for this project from general knowledge: dead zone and curve maths, input
buffering, chord and double-tap timing, and the button names printed on Xbox, PlayStation and Nintendo pads are
common knowledge. Nothing was copied or ported from another engine or library, and no third-party code or SDK was
added.
