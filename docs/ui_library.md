# UI component library

A game maker should be able to build any HUD, menu, inventory, dialogue, settings or app-style screen from ready parts.
Most parts already existed. [ui_component_audit.md](ui_component_audit.md) lists every part asked for, whether it exists
and what is missing. The library is built in slices; each slice adds the missing parts, a shipped example scene and tests.

| Slice | Content | State |
|---|---|---|
| 1 | Essentials and layout: range slider, input validation, aspect-ratio box | built (this page) |
| 2 | Game HUD parts and data-driven glue: radial menu, kill feed, ammo counter, interaction prompt, objective tracker, compass, item tooltip card, ability icon; two-way binding, conditional visibility, array to list, rich text links and icons, focus hints, hot reload | built (see "Slice 2" below) |
| 3 | App parts: menu bar, toolbar, tree view, sortable data table, wizard, form group; colour picker upgrade (hue / saturation square, value and alpha sliders) | built (see "Slice 3" below) |
| 4 | The scale of choice, as data: a style system, 54 themes, 371 vector icons, 56 ready-made screens with a Starter Pack, a searchable UI Gallery, and a simpler GameObject > UI menu | built (see "Slice 4" below; numbers in [ui_catalogue.md](ui_catalogue.md)) |

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
   - **Radial menu, kill feed.** Click a wedge of the wheel: the wedge under the pointer lights up, the last pick stays green and "You picked" shows its name. Click **New kill**: a
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
     the two yellow lines at the bottom of the card change within half a second. (If you play from another folder that line stays as it was.)
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

`ui_library2` (295 checks: every part's maths, pointer, keys, rules, round trips, Tab order, hot reload on a real temp file), `ui_library2_example`
(74 checks: loads and plays the shipped scene: clicks its buttons, moves its slider, types in its box), `ui_library2_shots` (19 checks: headless pictures
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

Found while testing the examples: `test_examples_work` fails any example scene without a main camera (Play would show nothing). The
slice 1 example had none, so both builders now add one (`EnsureSceneCamera`) and `ui_library_1.okayscene` was written again. The same
branch fix belongs on the slice 1 branch (`claude/ui-lib-1`).

## Slice 3: app parts

### How to try slice 3

1. Open the editor. **File > Examples > UI library (every in-game UI part) > ui_library_3**. (Greyed out when `examples\ui_library` is not
   next to the editor.)
2. Press **Play**. Everything below is on screen; the card "What you chose" (top right) shows what each part picked.
   - **Menu bar** (top). Click **File**, then **Open**: "Menu: File/Open". Slide over **Edit** while a menu is open and it switches.
     **Export (soon)** is greyed out. Escape or a click elsewhere closes. Keys while open: Left, Right, Up, Down, Enter.
   - **Toolbar** (under the title). **B**, **I**, **U** are toggles that stay pressed; **L**, **C**, **R** are one-of-a-group.
   - **Tree view.** Click an arrow to open or close a row; click a name to select it; then Up, Down, Left, Right, Home, End.
   - **Data table.** Click a header to sort (again to flip). Click a row to select it: the same person stays selected through a sort.
     **Add a row** adds made-up rows; scroll with the wheel. Click the table first to use the keys.
   - **Wizard and form group.** **Next** is refused while the step's form has a problem (red sentences show). Name and Email on step 1,
     Age (13 to 120) on step 2, then **Finish**. Step 3 greets you by the name you typed (a variable, shown by a UI Text Bind).
   - **Colour picker, advanced.** Click a swatch, drag in the square (hue across, saturation down), drag the brightness and
     see-through sliders. **Advanced on / off** hides and shows them.

To add the parts to your own scene: **GameObject > UI > App** (Menu Bar, Toolbar, Tree View, Data Table, Wizard, Form Group), the
**Color Picker** entry has an **Advanced** tick in its Inspector, or select an object and use **Add Component**.

Pictures (headless software renders, not a real window) are written by `ui_library3_shots` to `<build>/tests/ui_library3/` and copied to
`docs/images/ui_library/` (`menubar-open.png`, `toolbar.png`, `toolbar-labels.png`, `tree.png`, `table.png`, `form-errors.png`,
`wizard.png`, `color-advanced.png`, `example3-1280x720.png`, `example3-1280x900.png`).

### What was there and what is new

| Part | Before | Slice 3 |
|---|---|---|
| Context menu, tabs, accordion, breadcrumbs, pagination, list view, search bar, date and time picker, input validator | existed | unchanged (the input validator now also serves the form group) |
| Colour picker | a swatch strip and a chip (`UIColorPicker`) | extended in place: `advanced` adds the hue / saturation square (`UIColorSquare`) and two real `UISlider` children (value, alpha); off by default, so old pickers look the same |
| Menu bar, toolbar, tree view, data table, wizard, form group | missing | new: `UIMenuBar`, `UIToolbar`, `UITreeView`, `UIDataTable`, `UIWizard`, `UIFormGroup` |

All new parts share one base (`UIAppBase`, a rectangle with `position`, `size`, `anchor`, three colours, `fontScale`, `interactable`), one
layer (`K_AppParts`, appended to the layer table; a menu bar's open dropdown has its own `K_AppPopup` layer so it lands over everything),
the files `Components/UIAppParts.hpp` (the parts), `UI/UIAppPartsDraw.cpp` (drawing), `Components/UIAppParts.cpp` (button actions), and the
records `uimenubar`, `uitoolbar`, `uitree`, `uitable`, `uiwizard`, `uiform`, `uicolorsquare` (plus extra fields on `uicolorpicker`; an
older version that does not know a record skips it, an older file loads with the defaults). Each is in the property registry, so the
Inspector shows every field with its tooltip, and each has a typed script API (`UIMenuBar`, `UIToolbar`, `UITreeView`, `UIDataTable`,
`UIWizard`, `UIFormGroup`, and `SetAdvanced`, `SetHSVA`, `Hex` on `UIColorPicker`).

Text formats (kept as one readable line so a scene file stays easy to read and to diff):

| Part | Format |
|---|---|
| Menu bar | `File:New\|Open@Ctrl+O\|-\|~Export\|Quit;Edit:Undo\|Redo` menus split by `;`, entries by `\|`, `-` a line, `~` greyed, `@Ctrl+O` a shortcut |
| Toolbar | `New@N\|Open\|-\|Bold*\|Left=\|Center=` `-` a separator, `*` a toggle, `=` one-of-a-group, `@N` an icon (a letter, or an image path); `states` keeps `1` for each item that is on |
| Tree view | `World\|>Forest\|>>Wolf den\|>Castle` each leading `>` is one level deeper; `expanded` lists the open rows ("0,1") |
| Data table | columns `Name:150\|Class\|Level` (a number after a colon is the width), rows `Ava\|Mage\|42;Bo\|Rogue\|17` |
| Wizard | `steps` `Account\|Profile\|Done`; `pages` one object name per step; `validate` one entry per step split by `;`: a Form Group object name, or `if:<condition>` (the UI Visible If language) |
| Form group | `Name:text\|Email:email\|Age:int:13..120\|Password:text:min6\|Repeat:text:=Password\|Nick:text:?` kinds text email int number letters alnum; options `?` optional, `minN`, `maxN`, `a..b` a number range, `=Label` must match |

### The parts

| Part | What it does | Variables it writes | Events |
|---|---|---|---|
| UI Menu Bar | bar of menus; a click drops one down, sliding over another name switches, entries can be greyed, have a shortcut or be a line; keys Left, Right, Up, Down, Enter, Escape | `chosenVar` (entry number) and `<chosenVar>_text` ("File/Open") | `on_select` |
| UI Toolbar | buttons with an icon (letter or image) and a label (`showLabels`), toggles, radio runs, separators, greyed buttons | `chosenVar` (label pressed last); with `varPrefix`, `<prefix><label>` is 1 or 0 for each toggle and radio | `on_select` |
| UI Tree View | rows that open and close, one selected row, the open rows kept in `expanded`, scrolling (wheel and keep-selection-visible), only visible rows drawn; keys Up, Down, Left, Right, Home, End, Enter | `selectedVar` and `<selectedVar>_text` (the path) | `on_select`, `on_activate`, `on_expand`, `on_collapse` |
| UI Data Table | columns with widths, header-click sort (numbers as numbers, words ignoring capitals, numbers first, ties keep the written order), selection that follows the row through a sort, zebra rows, scroll bar, keys Up, Down, PageUp, PageDown, Home, End; row virtualisation: a draw visits only the rows in view (tested with 10000 rows) | `selectedVar` and `<selectedVar>_text` (the first cell) | `on_select`, `on_sort`, `on_activate` |
| UI Wizard | numbered step strip, Back and Next (Finish on the last), a step may refuse Next until its form is valid or a condition holds, a page object per step that shows only on its step, progress | `progressVar` (0 to 100), `stepVar` | `on_next`, `on_back`, `on_invalid`, `on_finish` |
| UI Form Group | makes one UI Input Field and UI Input Validator child per field (named `<group>_<label>`), lays them under their labels, optional Submit button; `Submit()` shows every problem or fires `on_submit` | `valuesVar` + label: each answer as it is typed; `validVar`: 1 or 0 | `on_submit`, `on_invalid` |
| UI Color Picker (advanced) | the swatch strip as before, plus a square (hue across, saturation down), a value slider and an alpha slider; a swatch click loads its numbers, an edit makes a colour of your own | - | `on_change` |

Buttons can drive them with no script (**On Click**, Target = the object): menu bar `Open`, `Close`; tree `ExpandAll`, `CollapseAll`, `Select`;
table `SortBy` (Amount = column), `ClearSort`, `Select`, `AddSampleRow`; wizard `Next`, `Back`, `Restart`; form group `Submit`, `Reset`;
colour picker `ToggleAdvanced`.

An open menu takes the click that lands on it, so a table or tree under the dropdown does not also select a row (a "claim" flag the parts
share; the menu bar runs first for that reason).

### What each file was written from

All code is our own, written for this engine from the plain ideas: the menu bar and toolbar from lists of labelled boxes with hit tests
on their rectangles, the tree view from the outline idea (a depth number per row, visible rows are those whose parents are all open),
the table from a stable sort over precomputed keys and a row window computed from the scroll offset, the wizard from a step counter
with a gate, the form from the same validator rules as slice 1, and the colour maths (`Render/ColorHSV.hpp`) from the definition of the
HSV hexcone (six 60 degree sectors). They reuse only our own helpers (`UIVars`, `UIInputField`, `UIInputValidator`, `UIButton`,
`UISlider`, the UI painter and shape fills, the property registry and the scene text format). No third-party code, fonts, images or SDKs
were added. The drawing helpers (box, frame, text, clipping) are small copies of the slice 2 ones.

### Slice 3 tests

`ui_library3` (335 checks: every part's maths, parsing, pointer, keys, variables, events, native actions, claim of a click by an open menu, 3000 row tree and
10000 row table, save and load round trips including an older colour picker line), `ui_library3_example` (87 checks: loads and plays the shipped scene:
menu, toolbar, tree, table, a wizard through all three steps, the colour square), `ui_library3_shots` (11 checks: headless pictures), plus the existing
`ui_layer_table`, `save_coverage`, `property_registry` and `examples_work`, which now list the new parts and the scene.

### Not done in slice 3

- **Gamepad and Tab focus.** The new parts are not in the focus ring that `NavigateUI` walks; keys work only after a click on the part
  (menu bar: while it is open). Wiring them in (the slice 1 range slider shows how) is open.
- **No scroll bar dragging** in the tree and table (the wheel, the keys and keep-selection-visible work; the bar only shows where you are).
- **Table:** no column resize or reorder, no multi-select, no editing of cells, and rows and cells cannot contain `;` or `|` when typed as
  text (a script that builds rows has the same limit, because the rows are one saved string).
- **Tree view:** node names cannot contain `|`; no drag to reorder, no icons, no check boxes.
- **Menu bar:** no sub-menus, no check marks, no Alt+letter opening; shortcuts are shown but not bound to keys.
- **Toolbar:** horizontal only; no tooltips; an icon is one or two letters or an image path.
- **Form group:** one text box per field (no drop-downs, check boxes or password dots yet: set the field's `contentType` on the child by hand);
  the labels are drawn to the left of the boxes only.
- **Colour picker:** the colour picker has no registry entry (it still saves through its older record); the hex number is not typed in, only read
  (`Hex()` from a script); the value and alpha sliders are plain sliders (no gradient behind them).
- **Layout:** the parts use fixed pixel boxes like the other app widgets; the example scene is laid out for 1280 x 720 and scaled by the canvas.
- Nothing in this slice was run on Windows or in a real window. The editor files were syntax-checked with `g++ -fsyntax-only` (the whole `main.cpp` too); the editor was not linked or run.

## Slice 4: the scale of choice

Owner request: "keep expanding the UI library, the UI should have thousands of UI and options to choose from", then "GameObject > UI: clean
that up" and "massively improve the prebuilt screens and UI, organise them better into sections". The answer is data, not thousands of classes:
a small set of readers, and text files in `assets/ui/` that the engine embeds at build time (`engine/CMakeLists.txt`), so every program has them
and a new look is a few lines.

**The numbers** (counted by `python3 scripts/gen_ui_catalogue.py` from those files, and checked against what the engine loads by the test
`ui_library4`; see [ui_catalogue.md](ui_catalogue.md) for the rules): 77 components, 111 named styles, 54 looks, 371 icons (348 drawn from
shapes and 23 made by turning or mirroring another), 56 screens in 9 sections (34 described in data, 22 older ones still built by their C++
function) with 88 layout variants. The style axes (14 shapes, 12 fills, 9 borders, 9 effects, 5 sizes, 4 densities, 17 state looks) can be mixed
freely, which is 13,608 shape/fill/border/effect looks (272,160 with size and density): that is a computed product of the axes, not a count of
hand-made styles. A "variant" means a distinct selectable look or layout; renamed copies are not counted (a test checks that no two styles are
the same mix).

**Styles** (`ui_styles.okaydata`, `okay/Scene/UIStyleLib.hpp`). A style is a name for a mix of shape (square, rounded, pill, squircle,
chamfered, bevelled, notched, slanted, tab, hexagon, trapezoid, circle...), fill (solid, gradient, glass, ghost, tint, inset, gloss, dark,
light...), border (hairline to thick, double, glow), effect (soft, hard and long shadow, glow, neon, emboss, lift) and four state looks (hover,
pressed, disabled, focus), plus a size (XS to XL) and a density (compact to spacious). `UIStyleVariant` (the "UI Style Variant" component, which
already gave a widget a role) now also carries `style`, `styleSize` and `styleDensity`; the style is applied to buttons, panels, text fields,
dropdowns, bars, toggles, sliders, tabs and layout groups, in the colours of that widget's own look (Slate when it has none).
Script: `ui_set_style("Play", "Neon Pill")`. Not offered because the renderer cannot draw them: stripe, noise and image fills, dashed borders.

**Looks** (`ui_themes.okaydata`). There is **no global theme**: a look belongs to one screen or one widget, so a retro menu, a sci-fi HUD and a
cartoon popup can live in the same scene (the example `ui_mix_styles.okayscene`). 54 looks: the twelve built-in ones of the UI Theme component plus 42
defined in data (light, dark, retro pixel, sci-fi, fantasy, military, cartoon, minimal, neon, wood, stone, metal, horror, kids, mobile, console, high
contrast...). A look bundles colours, the name of a font (only fonts we already ship; today every look draws with the built-in font, because a font file
path would have to exist in the player's project), corner radius, spacing, hover and animation speed, and sound names.
`UIStyleLib::ApplyLook(root, "Neon", "Glass", "l", "compact")` (script `ui_set_look("Pause Menu", "Neon")`) puts a look and an optional style on that
object and everything under it, and stores the choice on it (UI Style Variant: `themeName` is the look, plus `style`, `styleSize`, `styleDensity`), so it
is saved and nothing else changes. Screens from the library are built with their own look (`UIScreens::Build(scene, id, variant, look, style)`), the Gallery
has a Look and Style picker on every screen card, GameObject > UI > Menus & Screens uses the pair picked there, and the Inspector of a Style Variant has
Look, Apply to this screen, Apply to selection, Randomise, and Save/Load preset (a small `.okaylook` file in Assets/UI; an old `.okaytheme` file can also be
loaded there and used as a look). Every look is checked for readable text (contrast at least 4.5 on the panel, 3 on a button). Sounds: the names are stored
and read with `ui_look_sound("Neon", "click")`; no sound files are shipped and nothing plays by itself yet.

**Removed in this slice (it was only ever on this branch, never released): the scene-wide theme switch.** No `SetTheme`, no theme picker, no `ui.theme` in
Prefs. A script that still calls `ui_set_theme(...)` keeps running: it logs one warning line and does nothing. Old projects still load: the UI Theme
component, `.okaytheme` files and `ui_theme_preset` / `ui_load_theme` are unchanged, and a scene that has a UI Theme keeps its look (the new library just
never creates one).

**Icons** (`ui_icons.okaydata`, `okay/UI/UIIcons.hpp`). Each icon is a list of simple shapes in a 24 x 24 box (circle, ring, ellipse, rectangle,
rounded rectangle, line, polygon, polyline, arc, star, regular polygon; a leading `-` cuts a shape out). The picture for a size is made by
sampling each pixel 4 x 4 against the shapes, so the same icon at the same size always gives the same pixels, on every machine (tests), and
looks crisp at any size. Use one as `icon:heart` in an Image's texture or a Button's icon, or `[icon=icon:heart]` in rich text. All icons were
drawn for this engine; no outside artwork or font was used.

**Screens** (`ui_screens.okaydata`, `okay/Scene/UIScreens.hpp`). A screen is a few lines of a short layout language (panel, text, button, bar,
slider, toggle, dropdown, input, tabs, icon, `rep` for repeated items) and one builder turns it into real objects under the Canvas. Buttons carry
their actions as data (`act="swap:main_menu>settings"`, `quit`, `scene:Level1`, `theme:Neon`, `volume`...), which become a small script on the
button, so a screen works with nothing written by hand. Tabs switch their pages with no script. Variants move the card (Centered, Left side,
Right side) or add a dimmed backdrop. **Starter Pack** builds Main Menu, Settings, HUD, Pause and Game Over already joined (Play opens the HUD,
Pause opens the pause menu, Give up opens Game Over...). The old fixed-size Main Menu, Pause Menu, Settings, Game Over, Confirm Dialog and
Shop were replaced by data screens (the old functions stay in `UITemplates` and are still used by the older screens and by the tests).
Every data screen is tested to build, to stay on screen and inside its card at 1280 x 720, 1024 x 768, 2560 x 1080, 1920 x 1080 and (when it says
it fits) 720 x 1280 and 1080 x 1920, to save and load back to the same text, to have a script on every button that has an action, and to have no
two buttons or fields overlapping.

**GameObject > UI** (`editor/UIMenuModel.hpp`, `editor/main.cpp`). Eight groups with plain names (Basics, Input, Layout, Game, Menus & Screens,
Data & Lists, Windows & Pop-ups, Ready-made Pieces) and a last entry **Browse All UI...**; every item has a one-sentence tip, the common ones
come first, and a Canvas is made automatically when a widget needs it. The menu is data, tested in `menu_bar_model` (no duplicates, a tip on
every item, 8 groups) and `ui_library4` (every screen of the registry is in the menu once, every item of the old menu still has a home). See
[editor_menus.md](editor_menus.md) for before and after.

**UI Gallery** (`editor/UIGalleryWindow.cpp`; the list and the search rules are in `okay/Scene/UIGalleryIndex.hpp` and are tested). One window
for every component, style, theme, icon and screen: search, filter by kind, category, tag and theme, preview, and add with one click (a screen
in a chosen layout and theme, the Starter Pack, a style on the selected object, a theme on the scene, an icon on the selected Image or Button).
The previews are drawn from the real widgets of a temporary scene and from the style data; they are close pictures, not the renderer's pixels.

**What each new file was written from.** The style, theme and screen readers from the usual ideas of design tokens and declarative layout, the
icon rasteriser from plain geometry (distance to a segment, even-odd polygon fill, supersampling), the data files from scratch. No other
project's code, themes, icons or fonts were used or copied.

**Tests.** `ui_library4` (data reader, 111 styles, 54 looks and theme switching with an override, 371 icons and their pixels, every screen at
all the sizes, the menu, the gallery search and the catalogue numbers), `ui_library4_example` (loads and plays both new example scenes: a theme
click, and the whole Starter Pack flow), `ui_library4_shots` (the pictures), and the updated `menu_bar_model`.
