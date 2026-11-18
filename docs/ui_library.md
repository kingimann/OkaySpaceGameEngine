# UI component library

A game maker should be able to build any HUD, menu, inventory, dialogue, settings or app-style screen from ready parts.
Most parts already existed. [ui_component_audit.md](ui_component_audit.md) lists every part asked for, whether it exists
and what is missing. The library is built in slices; each slice adds the missing parts, a shipped example scene and tests.

| Slice | Content | State |
|---|---|---|
| 1 | Essentials and layout: range slider, input validation, aspect-ratio box | built (this page) |
| 2 | Game parts and data-driven glue | not started |
| 3 | App parts | not started |

## How to try slice 1

1. Open the editor. Choose **File > Examples > UI library (every in-game UI part) > ui_library_1**.
   (The menu entry is greyed out when the `examples\ui_library` folder is not next to the editor.)
2. Press **Play**. You see three cards and plain instructions on screen.
3. **Range slider.** Drag a handle of the "Price range" slider. The handle nearest to your click moves; the two handles never
   cross. The "Level range" slider uses whole numbers and keeps the handles at least 3 apart.
   Keyboard: Tab to the slider, Left and Right move the picked handle, Enter swaps to the other handle.
4. **Input validation.** Click the Email box and type `ada@`: a red ring and the words "Enter an email like name@site.com" appear.
   Finish it (`ada@lovelace.org`): the ring turns green. The Age box wants 13 to 120. The Password box wants 6 or more
   characters, and "Repeat the password" must be the same text.
5. **Aspect ratio box.** The blue frame on the right is 16:9. Make the Game view taller, wider or portrait (the editor's
   device size list in the Game view, or resize the player window): the frame stays 16:9 and stays centred.

To add the parts to your own scene: **GameObject > UI > Controls > Range Slider**, **Controls > Input Field with Validation**,
**Layout & Overlays > Aspect Ratio Box**, or select an object and use **Add Component** (UI Range Slider, UI Aspect Ratio,
UI Input Validator).

Pictures made by the headless software renderer (not a real window) are written by the test
`ui_library_shots` to `<build>/tests/ui_library/` (`range-slider.png`, `input-validation.png`, `aspect-landscape.png`,
`aspect-portrait.png`, `aspect-ultrawide.png`).

## The parts

### UI Range Slider (`Components/UIRangeSlider`)

One track, two handles: a low and a high value. Fields: `minValue`, `maxValue`, `low`, `high`, `step` (0 = smooth),
`wholeNumbers`, `minGap` (smallest distance between the handles), `showValue`, `valueFormat` (`{lo}` and `{hi}` are
replaced; empty is `{lo} - {hi}`), colours, shape, `interactable`, `focusable`.

- The pointer picks the handle nearest the press and drags it. Two handles stacked on one value can still be pulled
  apart (the side you press on decides).
- Keyboard and gamepad: Left and Right move the picked handle by `step` (or by 1 for whole numbers, or by 1/20 of the
  track); Confirm swaps handles.
- The script on the same object gets `on_change()`. Typed script API: `UIRangeSlider.SetRange(low, high)`, `SetLow`,
  `SetHigh`, `Low()`, `High()`.
- Saved as the `uirange` record; an older version that does not know it skips the record.

### UI Input Validator (`Components/UIInputValidator`)

Rules for the UI Input Field **on the same object**: `kind` (Anything, Email, Whole number, Number, Letters, Letters and
digits), `required`, `minLength`, `maxLength` (characters, not bytes), a number range, `mustMatch` (the name of another
input field that must hold the same text, for "repeat the password"), your own `message`. It draws a red ring and the
sentence under the field, and a green ring when `showOk` is on. With `quietUntilTouched` (the default) nothing shows until
the player has typed. The script gets `on_valid()` and `on_invalid()` when the answer changes. Typed script API:
`UIInputValidator.Check()`, `IsValid()`, `Error()`.
The email rule is simple on purpose: one `@`, no spaces, a dot inside the part after it. Numbers are plain digits (no
`nan`, `inf` or hex). Saved as the `uivalidate` record.

### UI Aspect Ratio (`Components/UIAspectRatio`)

A container that keeps its own box at a fixed width : height inside its UI parent (or the screen). Modes: **Fit** (the
biggest box that fits), **Fill** (the smallest box that covers), **Width controls** and **Height controls**. `alignX` and
`alignY` place the box in the spare room (0.5 is centred), `padding` keeps empty space around it. Children are laid out
inside the box, so a stretch-anchored panel under it fills the box. Saved as the `uiaspect` record.
Not yet handled: a world-space Canvas (the box uses the screen path only).

## What each file was written from

All code is our own, written for this engine. The range slider from the plain idea of a two-handle slider, the validator
from the usual form rules (length, number, email shape), the aspect box from the geometry of fitting a rectangle of a given
ratio into a box. They reuse only our helpers (`UIWidgetUtil`, `UIShape`, the UI layer table, the property registry and the
scene text format). No third-party code, fonts, images or SDKs were added.

## Tests

`ui_library` (maths, pointer, keys, rules, round trips), `ui_library_example` (loads and plays the shipped scene),
`ui_library_shots` (headless pictures), plus the existing `ui_layer_table`, `save_coverage` and `property_registry`, which
now list the new parts. Nothing in this slice was run on Windows or in a real window.

## Internal record: slices 2 to 4 (branch claude/ui-lib-4)

Kept short on purpose (internal notes, not a guide).

- **What exists:** data-driven UI in `assets/ui/*.okaydata` (77 components, 126 styles, 62 looks, 408 icons, 62 screens), built by `UIScreens`, `UIStyleLib`
  and `UIIcons`; HUD, app parts and the colour picker from slices 2 and 3; UI Gallery and the simplified GameObject > UI menu in the editor.
  Looks and styles are per screen or widget (no scene-wide theme). Counts and lists: `docs/ui_catalogue.md`
  (`python3 scripts/gen_ui_catalogue.py`, `--check`). Tests: `test_ui_library4*`, `test_ui_templates`, `test_runtime_ui`.
- **Rules the tests enforce:** every look reaches 4.5 : 1 for text, muted text and button labels (`UITheme::MakeReadable`); no look or screen sets the pixel
  font; text fits its box; a slider's value has its own space beside the track (`UISlider::ValueSpace`); no text outline or shadow unless a look sets
  `textShadow`.
- **Missing:** placeholder content on some screens (names, numbers, art); icons drawn by shapes only; real-GPU renderers not checked here (software path only).
- **Pictures:** never kept in the repository; `test_ui_library4_shots` writes to `OKAY_SHOT_DIR` (default `<build>/tests/ui_library4`).
