# In-game UI component library: audit (what exists, what is missing)

Written 2026-11-16 for the owner's request "a full in-game UI component library". This page is about the **in-game
UI** (Canvas, UI Panel, UI Button, ... and `File > Examples > UI Gallery`). It is **not** the editor's own toolkit
(`okayui/`, the editor panels): that is a separate effort and was not touched.

Method: `scripts/find_existing.py` was run for ui / widget / button / slider and the headers were read
(`engine/include/okay/Components/UI*.hpp`, `Scene/UITemplates.hpp`, `Scene/UIGallery.hpp`), together with
`docs/feature_map/12-ingame-ui.md`, `docs/ui_runtime.md`, `docs/ui_html.md`, `docs/UNFINISHED.md` and the tests.
Each row says **exists (where)** or **gap**. "Slice N" says which slice of the library builds the gap
(slice 1 = essentials and layout, slice 2 = game parts and data-driven, slice 3 = app parts).
`New in slice 1` rows are built by this change.

The honest summary: most of the library already exists. About 55 of the 100 parts asked for are present, tested and
reachable from `GameObject > UI`. The gaps are mostly the "built on top" parts (radial menu, data table, tree,
menu bar, wizard, kill feed, ammo counter, objective tracker) and the data-driven glue (two-way binding, list
templates from an array, conditional visibility).

## Essential

| Part | Status | Where / gap |
|---|---|---|
| Label | exists | `TextRenderer` with Screen Space on; Create > UI > Basic > Text. Localised text key (`LocalizedText`) |
| Rich text (bold, italic, colour, size) | exists | `UI/RichText.hpp`, `[b] [i] [color=] [size=]`, `docs/ui_runtime.md` |
| Rich text links and inline icons | gap | no `[url]` or `[icon]` tag; slice 2 |
| Button (variants, icon, toggle) | exists | `UIButton` (toggle mode, icon, `UIStyleVariant` roles Primary/Secondary/Danger/Ghost/Outline/Pill) |
| Checkbox, toggle switch | exists | `UIToggle` Style = Checkbox / Switch |
| Radio group | exists | `UIToggle` Style = Radio + `radioGroup` |
| Slider | exists | `UISlider` (step, whole numbers, format, vertical, gradient) |
| Range slider (two handles) | **New in slice 1** | `UIRangeSlider` |
| Stepper | exists | `UIStepper` |
| Progress bar: linear, segmented, delayed damage trail | exists | `UIProgressBar` (`segments`, `trailDelay`, colour by value) |
| Progress bar: circular | exists | `UIRadialProgress` |
| Text input: single line, password, placeholder, number modes, whitelist, multiline | exists | `UIInputField` |
| Text input validation (rules, message, red ring) | **New in slice 1** | `UIInputValidator` (the field itself only filters characters) |
| Dropdown / combo | exists | `UIDropdown` (scrolls, keys) |
| Tabs | exists | `UITabs` (five styles, icons, badges) |
| Tooltip | exists | `UITooltip` (stays on screen) |
| Image | exists | `UIImage`, `UIRawImage` |
| Nine-slice panel | exists | `UIImage.nineSlice`, `UIPanel` shapes |
| Scroll view with scrollbars | exists | `UIScrollView`, `UITouchScroll` |
| Divider | exists | `UITemplates::AddDivider` (Kit) |

## Game

| Part | Status | Where / gap |
|---|---|---|
| Health and mana bars with a delayed damage trail | exists | `UIProgressBar` (`trailDelay`, `trailColor`) + `UIBarBind` (follows a variable) |
| Cooldown icon with a radial sweep | exists | `UIRadialProgress` (no ready-made "ability icon" group: slice 2) |
| Hotbar / quickslots with drag and drop | exists | `UISlotGrid`, `UIDraggable` / `UIDropTarget`, `Inventory/InventoryInput` |
| Inventory grid / list, item tooltips from the item database | partly | `GridInventoryUI`, `InventoryUI`, `UIInventoryBind`; the item tooltip card is a gap (slice 2) |
| Minimap | exists | `Minimap` (north tick, circular mask) |
| Compass strip | gap | slice 2 |
| Damage numbers / floating text | exists | `UIDamageNumbers` |
| Crosshair (styles) | exists | `Crosshair` |
| Objective tracker (HUD list) | gap | quest data exists (`QuestLog`), the on-screen tracker does not; slice 2 |
| Dialogue box: typewriter and choices | exists | `UIDialogueBox` |
| Notification / toast stack | exists | `UIToast` |
| Scoreboard / leaderboard | exists | `UITemplates::AddScoreboard`, `AddLeaderboard` (templates, not a bound component) |
| Ammo counter | gap | slice 2 |
| Interaction prompt ("Press E") | partly | `HoldPrompt` (hold ring); a plain key prompt with the bound key is a gap (slice 2) |
| Radial menu | gap | slice 2 |
| Kill feed | gap | slice 2 |
| Loading screen | exists | `LoadingScreen`, five layouts |

## App

| Part | Status | Where / gap |
|---|---|---|
| Modal dialog / confirm / alert | exists | `UIModal`, `AddConfirmDialog` |
| Popup / context menu | exists | `UIContextMenu` |
| Menu bar, toolbar | gap | slice 3 |
| List view | exists | `UIListView` (virtual rows) |
| Tree view | gap | slice 3 |
| Data table with sortable columns | gap | slice 3 |
| Accordion / collapsible | exists | `UIAccordion` |
| Breadcrumbs, pagination, skeleton, bottom sheet | exists | `UIBreadcrumbs`, `UIPagination`, `UISkeleton`, `UIBottomSheet` |
| Wizard / stepper pages | gap | `UITabs` pages can be driven by script; a Next/Back wizard with a progress line is slice 3 |
| Date and time picker | exists | `UIDateTimePicker` |
| Colour picker | partly | `UIColorPicker` is a swatch strip; a hue/saturation picker is slice 3 |
| Search box with suggestions | exists | `UISearchBar` |
| File picker | exists | `UIFilePicker` |
| Form with validation | **New in slice 1** (validation) | `UIInputValidator`; a form group with a Submit button that checks all fields is slice 3 |
| Settings screen (graphics, audio, controls with rebinding) | exists | `AddSettings`, `SettingsScreen`, `UIKeybindField` |

## Layout

| Part | Status | Where / gap |
|---|---|---|
| Horizontal / vertical / grid layouts | exists | `UILayoutGroup` |
| Flow (wrapping rows) | exists | `UILayoutGroup` Flex Row / Column with Wrap, `UIFlexItem` grow and shrink |
| Spacing, padding, margins | exists | `UILayoutGroup.spacing/padding`, stretch anchor margins |
| Anchors, responsive scaling, phone and tablet sizes | exists | `UIAnchor`, `Canvas` ScaleWithScreenSize, the editor's device sizes |
| Aspect-ratio container | **New in slice 1** | `UIAspectRatio` |
| Safe area | exists | `UISafeArea` |
| Z-order and layers | exists | `Canvas.sortOrder`, `uiDrawOrder`, the shared layer table (`UI/UIRenderer`) |
| Focus navigation order for gamepad and keyboard | partly | `NavigateUI` works by position; `UI/FocusGraph` (tab order, links, traps) is written but not used by `NavigateUI` (feature map 12.12); slice 2 |

## Data-driven

| Part | Status | Where / gap |
|---|---|---|
| Bind a widget to a variable (one way) | exists | `UITextBind`, `UIBarBind`, `UIInventoryBind` |
| Two-way binding (a slider writes the variable back) | gap | slice 2 |
| List templates repeated from an array | partly | `UIListView` binds script rows; binding to an array variable is feature map 12.13 "Open"; slice 2 |
| Conditional visibility from a value | gap | `ui_show` / `ui_hide` by script only; slice 2 |
| Localisation keys | exists | `LocalizedText` on text, button, toggle, input, tooltip; `SceneLocalization` |
| Styles and themes from a data file | exists | `UITheme` + `.okaytheme` files, `UIThemeAsset`; selectors and states are still open (12.7) |
| UI described in a text asset that loads into a Canvas | exists | `UIDocument` (line markup and HTML + CSS subset), `docs/ui_html.md` |
| Hot reload in the editor | partly | `UIDocument.Rebuild` by button; no file watcher; slice 2 |

## Slice 1 (this change)

New: `UIRangeSlider`, `UIInputValidator`, `UIAspectRatio`, a shipped example scene
`examples/ui_library/ui_library_1.okayscene` (File > Examples > UI library), tests `ui_library`, `ui_library_example`,
`ui_library_shots`. See [ui_library.md](ui_library.md).

Not done in slice 1 (kept honest): rich text links and icons, the FocusGraph wiring, the form group with Submit.
