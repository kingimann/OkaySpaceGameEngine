# UI component library

A game maker should be able to build any HUD, menu, inventory, dialogue, settings or app-style screen from ready parts.
Most parts already existed. [ui_component_audit.md](ui_component_audit.md) lists every part asked for, whether it exists
and what is missing. The library is built in slices; each slice adds the missing parts, a shipped example scene and tests.

| Slice | Content | State |
|---|---|---|
| 1 | Essentials and layout: range slider, input validation, aspect-ratio box | built (this page) |
| 2 | Game HUD parts and data-driven glue: radial menu, kill feed, ammo counter, interaction prompt, objective tracker, compass, item tooltip card, ability icon; two-way binding, conditional visibility, array to list, rich text links and icons, focus hints, hot reload | built (see "Slice 2" below) |
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

## Slice 2: game HUD parts and the data-driven glue

### How to try slice 2

1. Open the editor. **File > Examples > UI library (every in-game UI part) > ui_library_2**. (The entry is greyed out when
   `examples\ui_library` is not next to the editor. Start the editor from its install folder so the example's two small
   data files, `ui_library_items.okayitems` and `hot_reload_demo.okayui`, are found by their relative path.)
2. Press **Play**. Six cards, each with a plain hint. Everything below is on screen:
   - **Ammo, ability, prompt.** Click **Shoot** and **Reload**: the number drops, then refills from the spare rounds and the
     bullet pips follow (amber under a quarter of the magazine, red at 0). Click **Use (or press Q)**: the ring around the icon
     sweeps for 4 seconds and the seconds left show in the middle. Tick **I am near the door**: the key cap "E  Open the door"
     fades in; untick it and it fades out. That toggle is a two-way binding to the variable `near_door`.
   - **Radial menu, kill feed.** Click a wedge of the wheel: the wheel text and "You picked: ..." change. Click **New kill**: a
     line appears at the top right of the screen and fades after 12 seconds; lines with `Player` use the accent colour.
   - **Objectives, compass.** The tracker lists "Wolf trouble". **Wolf** adds one kill (shown as 1/3, 2/3), **Talk** and **Bridge**
     tick the other two, **Again** starts the quest over. Drag the heading slider: the compass strip slides.
   - **Item tooltip card.** Pick an item in the dropdown: the card shows its name in the rarity colour, the words, the weight, the
     stats and an icon tile. (The items come from `ui_library_items.okayitems`; your project's own items are not touched.)
   - **Bindings and conditions.** Drag the volume slider: the box and the "Volume is ..." line follow. Type a number in the box: the
     slider follows. Below 20 a red warning shows; turn **Mute** on and "Muted" shows instead.
   - **List, links, hot reload.** The list is filled from the array `scores`: **Add a score** adds a row, **Clear** empties it
     ("No scores yet"). Click the blue underlined "manual": "Link you clicked: manual" appears. The small coloured squares in the
     sentence are `[icon=...]` pictures (the sword and the potion take their colour from the item database).
     **Hot reload:** while the game runs, open `examples\ui_library\hot_reload_demo.okayui` in a text editor, change the words, save:
     the yellow line at the bottom of the card changes within half a second. (If you play from another folder that line stays as it was.)
3. Press **Tab**: the focus ring walks the buttons in reading order (Shift+Tab goes back). Press Q for the ability.

To add the parts to your own scene: **GameObject > UI > Game HUD** (Radial Menu, Kill Feed, Ammo Counter, Interaction Prompt,
Objective Tracker, Compass, Item Tooltip Card, Ability Icon) and **GameObject > UI > Data** (Slider Bound to a Variable, List From
an Array, Visible If, Hot Reload Watcher), or select an object and use **Add Component** (UI Bind, UI Visible If, UI List Bind,
UI Hot Reload, UI Focus Hint, UI Rich Links, and every HUD part).

Pictures made by the headless software renderer (not a real window) are written by the test `ui_library2_shots` to
`<build>/tests/ui_library2/` and copied to `docs/images/ui_library/` (`hud-*.png`, `rich-links-icons.png`,
`example2-1280x720.png`, `example2-1280x900.png`).

### The game parts

All eight share one layer (`K_GameHud`, appended to the layer table so older numbers stay) and the same box fields: `position`,
`size`, `anchor`, `background`, `accent`, `textColor`. They are written in `Components/UIGameHud.hpp`, drawn in
`UI/UIGameHudDraw.cpp`, saved as the records `uiradialmenu`, `uikillfeed`, `uiammo`, `uiprompt`, `uitracker`, `uicompass`, `uicard`,
`uiability` (an older version that does not know a record skips it).

| Part | What it does | Reads from | Script |
|---|---|---|---|
| UI Radial Menu | up to 12 wedges split equally, wedge 0 on top, clockwise; the pointer picks a wedge by direction (a dead zone in the middle); hold-to-open with `openKey` (let go to choose), click mode, or a fixed wheel (`alwaysShown`) | `items` ("A\|B\|C"); writes `chosenVar` and `<chosenVar>_text` | `on_select()`; `Open`, `Close`, `Choose(i)`, `Chosen()`, `IsOpen()` |
| UI Kill Feed | newest line on top, `maxEntries`, `lifetime`, fade; a line naming `playerName` uses the accent colour | `Push(killer, victim, weapon)`; C++ `UIKillFeed::Announce(scene, ...)`; button action `AddSampleKill` | `Push`, `Clear` |
| UI Ammo Counter | "12 / 90" with bullet pips, amber at `lowFraction` of the magazine, red when empty, RELOADING | `currentVar`/`reserveVar`, else a Ranged Weapon (the `weapon` object, else the Player), else typed numbers | `SetAmmo`, `Text()` |
| UI Interaction Prompt | key cap + word; fades in when the Player is within `range` of `target`, or while `showVar` is not 0, or `alwaysShow`; the key runs `on_interact()` | positions of the `playerTag` object and `target` | `on_interact()`; `SetAction`, `IsShowing()` |
| UI Objective Tracker | active quests with ticks and `2/5` progress; `questId` shows one; follows the Quest Log's tracked quest | the scene's Quest Log (the existing `showTracker` overlay is the plain version; turn it off to avoid two) | - |
| UI Compass | strip with N, NE, E ... and 15 degree ticks, markers for objects named in `markers` with their distance, `fieldOfView` degrees wide | `headingVar`, else the `source` object's facing, else the main camera, else `heading`; north is +Z (the Minimap's rule) | `Heading()` |
| UI Item Tooltip Card | the item's name in its rarity colour, category, description, stack, weight, use, durability, extra fields, a compare block against `equipment`; icon (the item's image, else a coloured tile with its letters) | the item database (`ItemDefinition`, the same `DescribeItem` words the inventory tooltips use); `item`, or `itemVar`; `itemsFile` adds an `.okayitems` file's items without replacing the project's | `Show(id, count)`, `Hide()` |
| UI Ability Icon | an icon (letters or `iconPath`), cooldown seconds, key, charges; drives the ring on the same object | needs a UI Radial Progress on the object (the ring: its thickness and colours are the slot's look) | `on_ability_use()`, `on_ability_ready()`; `Use`, `Reset`, `IsReady`, `SecondsLeft` |

Buttons can drive them with no script: **On Click**, Target = the object, Function = `Open`/`Close`/`Choose` (radial menu),
`AddSampleKill`/`Clear` (kill feed), `Use`/`Reset` (ability), `Shoot`/`Reload` (ammo counter; these change the typed numbers).

### The data-driven parts

| Part | What it does |
|---|---|
| UI Bind (`uidatabind`) | a UI Slider, UI Stepper, UI Toggle (on = 1), UI Dropdown (index; the picked words go to `<var>_text`), UI Range Slider (`var` low, `varHigh` high) or UI Input Field (text; a number when the text is one) writes `var` when the player changes it and shows `var` when anything else changes it. The first frame the variable wins when it exists, else the widget's value is written. Modes: two way, widget writes only, variable shows only. A change by the player is told apart from a change by the variable by comparing with the last synced value, so nothing bounces back |
| UI Visible If (`uivisible`) | `condition` is a variable or an expression: `hp < 30`, `!hasKey`, `score >= 10 && lives > 0`, `state == "menu"`, `(a \|\| b) && c`. Numbers compare as numbers, anything else as text, an unset name is false. `target` is the object to show or hide (empty = itself); `invert` flips it. Hidden means switched off, so leave it switched on in the scene. A self-hiding object cannot update itself, so the layout pass of the scene (`UpdateUILayouts`) brings it back when the condition holds; in the editor (not playing) nothing is touched |
| UI List Bind (`uilistbind`) | fills the UI List View on the same object from an array variable: a visual-script list (scene or global variable, a list of maps gives each key as `{field}`) or an Actions array (`array_push` ...). `format` fills the row's first text, any other text in the row template that contains `{...}` is filled too (`{item}`, `{index}`, `{n}`, the map keys, `{key|default}`); `emptyText` shows as the only row when the array is empty. Rows are still recycled by the list |
| UI Hot Reload (`uihotreload`) | watches `file` (markup for a UI Document, or an `.okaytheme` for the UI Theme on `target`) every `interval` seconds and applies it when its time or size changes: `Rebuild()` for markup, `LoadUIThemeAsset` + `Apply()` for a theme. The reason for a failure is kept (`LastError`) |
| UI Focus Hint (`uifocushint`) | `NavigateUI` now walks Tab and Shift+Tab through the tab order of the `FocusGraph` (lowest `tabIndex` first, 0 after them in reading order, negative skipped) and lets a widget name the object each arrow goes to (`-` keeps focus). Without hints Tab walks reading order |
| UI Rich Links (`uirichlinks`) | with Rich Text on, `[url=target]words[/url]` is drawn underlined in a link colour (an explicit `[color]` wins) and clicking it runs `on_link()` and stores the target in Prefs `ui_link`. `[icon=name]` draws one glyph-sized picture: an image path, or an item from the item database (its icon, or a coloured tile with its letter). The renderer records where each link was drawn; the click reads that |

Variables everywhere mean the same place: the scene's visual-script variables, the global ones, the Actions text variables,
the Actions number variables, then Prefs (a visual script's own per-object variables are not read; use a scene variable).

#


### What each file was written from

All code is our own, written for this engine from the plain ideas: the radial menu from the geometry of splitting a circle into equal
angles (the angle from the top, rounded to the nearest wedge), the kill feed from a list that ages, the compass from the bearing of a
direction (atan2 of east over north), the condition language from the usual infix boolean grammar (a small recursive-descent reader),
the bindings from "remember the last value each side had". They reuse only our own helpers (the UI painter and shape fills, `QuestLog`,
`ItemDatabase` and `DescribeItem`, `RangedWeapon`, `UIRadialProgress`, `UIListView`, `UIDocument`, `UITheme`, `FocusGraph`, `RichText`).
No third-party code, fonts, images or SDKs were added.

### Slice 2 tests

`ui_library2` (every part's maths, pointer, keys, rules, round trips, Tab order, hot reload on a real temp file), `ui_library2_example`
(loads and plays the shipped scene: presses its buttons, moves its slider, types in its box), `ui_library2_shots` (headless pictures
and the link click on a recorded rectangle), plus the existing `ui_layer_table`, `save_coverage` and `property_registry`, which now
list the new parts. Nothing in this slice was run on Windows or in a real window, and the editor was not built (the editor files were
syntax-checked with `g++ -fsyntax-only`).

### Not done in slice 2

The kill feed is not wired to an automatic death event (a script or `UIKillFeed::Announce` pushes lines). The radial menu has no
gamepad stick input (keys: Escape and Enter, and `Step()` from a script). The compass has no waypoint icons from the Minimap's own
blips. The item card is placeable but not wired to the inventory's hover (the inventory screens keep their own tooltip). `UI Bind`
does not write a Stat component or a networked SyncVar. Link clicks need the game to be drawing the text (they use the drawn
rectangles) and were not tried in a real window or on a scaled canvas. Hot reload watches one file per component and no folders.
Icons draw in the pixel font's cell size; with a TTF font the cell is the same size but the line's measured width ignores it.
