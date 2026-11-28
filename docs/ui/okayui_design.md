# OkayUI designs (internal record)

Owner request, 2026-11-27: "improve our ui for okayUI so it doesnt look like imgui under a different name. imrpove it massivly",
then "i want different designs, more options", "more customiztions", "more modern and updated look editer and launcher across".
Feature map 40.2. Code: `okayui/include/okay/UI/OkayUIDesign.hpp`, `okayui/src/OkayUI_Design.cpp`.

## One source

A **design** is data: a palette for dark and/or light and a **shape** (radii, outlines or none, shadow depth, tab shape, focus
ring width, density, motion time). The person's **options** sit on top: design, light or dark, accent (the design's or any
colour), density (compact 0.75, comfortable 1, spacious 1.3), corner roundness (0 to 2 times), UI scale, text size, shadows,
reduce motion. `design::Resolve(options)` gives the **tokens** every program draws from:

- OkayUI: `design::ThemeFor(tokens)` fills `Theme` (`Style()`); `design::Set` restyles live and keeps pushed colours.
- Dear ImGui (launcher, editor): `okaytheme::ApplyDesign(style, tokens)` in `shared/OkayTheme.hpp` sets the palette tokens the
  page code uses (`kRaised`, `Ink`, `TextU32`...), the colours, rounding and borders; `ApplyDensity` scales paddings.
- The options live in one per-user file, `<config>/appearance.json` (Windows `%APPDATA%/OkaySpace/config/appearance.json`;
  `OKAY_APPEARANCE_FILE` overrides it). Programs call `design::FollowUserFile()` at start and `design::Poll(time)` each frame
  (OkayUIHost does it for OkayUI programs), so a change in one program shows in the others within half a second.
- The controls are one copy, `shared/AppearanceUI.hpp`: launcher Settings > Appearance and editor Edit > Appearance.
- Tests never follow the file, so they always draw the default design.

## Built-in designs

| Name | Schemes | Look |
|---|---|---|
| Graphite (default) | dark, light | neutral charcoal, surfaces step up in lightness per layer, no outlines on controls, iris accent, underline tabs, radius 7 / 5 / 12, medium shadows |
| Slate | dark, light | the launcher's old look kept as it was: blue-tinted surfaces, hairline outlines, pill tabs, blue accent, radius 6 / 4 / 10 |
| Midnight | dark | near-black indigo, violet accent, round (10 / 6 / 16), deep shadows, pill tabs |
| High Contrast | dark, light | black or white, 2 px outlines on everything, yellow (dark) or blue (light) accent, 3 px focus ring, no shadows, near square |
| Paper | light first, dark | warm off-white, flat (no shadows), thin warm outlines, segmented tabs, terracotta accent, radius 3 |
| Ember | dark | warm brown-black, cream text, orange accent with dark text on it, segmented tabs, borderless, radius 9 |
| Glacier | light | cool blue-grey page, white cards, navy text, cobalt accent, hairline outlines, underline tabs, radius 5 |
| Dusk | dark, light | low glare on purpose (still AA): teal-grey and sand, outlines, pill tabs, slow motion, radius 8 |
| Blossom | light | soft pastel rose-white, plum text, raspberry accent, very round (12 / 18), pill tabs |
| Void | dark | OLED pure black, near-black layers, mint accent with black text, no shadows, underline tabs |
| Aurora | dark, light | colourful: deep violet with a hot pink accent (light: lavender and magenta), deep shadows, radius 11 |

Every palette passes the checks in `test_okayui_design`: body text 7:1 and secondary text and status colours 4.5:1 on every
surface (WCAG 2.x), a label on the accent 3:1, tooltip text 7:1, placeholders 3:1; hover, press and accent-hover move visibly
from their rest colour and the selection tint is at least 31% opaque. Every pair of designs differs in colour and in shape. Names are saved in the file: never rename one.

## Tokens

Surfaces bottom-up: deep, sunken (rails, grooves, headers), base (window body), raised (cards, section headers), overlay
(popovers, menus, lists, toasts), field, control. Text, textDim, textFaint. Hairline. Accent, onAccent, accentHover,
selection (accent at 40%), focusRing. Ink (the tint for soft hovers: light on dark, dark on light). Success, warning, danger,
info. Tooltip bg and text. Radii: small (check boxes, chips, row highlights), medium (controls), large (windows, popovers).

## Widget anatomy

- Button: control fill easing to hover and pressed; outline only in designs with control outlines. PrimaryButton: accent.
- Text field: field fill, a touch lighter under the pointer; with the keyboard an accent edge and the focus ring 2 px outside;
  placeholder in textFaint.
- Check box: an ink ring round the field colour; ticked fills with the accent and a two-stroke tick in onAccent.
- Radio: ink ring; chosen is an accent disc with an onAccent dot. Switch: pill track to accent, knob with a small shadow.
- Slider: thin sunken groove, accent fill, a round thumb with a shadow that grows on hover and an accent halo while dragged.
- Combo: control box, choice clipped before a chevron; the list is a popover just under the box, the chosen row ticked.
- Tabs: underline (2 px accent bar, others dim), pill (accent fill) or segmented (raised chip in a sunken track).
- Window: shadow by depth, rounded body in a hairline, a clean header in the sunken colour, title dim when unfocused, a close
  cross on a round chip that turns the danger colour; the fold chevron shows only on hover of its corner or when folded.
- Section header (CollapsingHeader): raised band, chevron, optional accent bar (High Contrast, Paper).
- Tree: rounded hover, chevron, a faint indent guide from the node to its last child.
- Menus and popups: rounded popover with shadow and hairline; rows are inset rounded highlights; checked items get a tick;
  shortcuts right-aligned in textFaint; separators inset hairlines. Plain-text tooltips are small dark cards; tooltips with
  widgets are popovers.
- Scroll bars: a thin ink pill at rest that grows under the pointer; no track unless a colour is pushed.
- Toasts: overlay card with a status bar and dot.

## Decisions

- Layout metrics are unchanged at Comfortable density: row heights, gaps, paddings and label places are what they were for each
  text mode (TrueType or bitmap), so behaviour and layout tests hold; only drawing changed. Density scales gaps and row padding.
- The bitmap font stays only as the fallback (and for the headless tests); every program turns TrueType text on
  (`UseLauncherLook(true)`, frozen name). The old grey "classic" look and its shape code are gone; `OKAY_CLASSIC_UI` and
  `ui_style("classic")` now mean the bitmap font with the same design.
- Motion is time-based (`Theme::motionMs`) and needs the host's clock (`Input::time`); without one (tests) states switch at
  once, so goldens never depend on timing. Reduce motion sets the time to 0.
- `StyleVar_FramedWidgets` (ported editor panels that must match Dear ImGui pixel for pixel) keeps its own frame drawing; it
  takes the design's colours.
- First start with designs: the launcher's old theme maps Slate to Graphite (the new default), Midnight to Midnight, Light to
  Slate's light scheme; a non-default accent preset becomes a custom accent; the editor's light mode becomes the light scheme.

## Round 4 (2026-11-27, `docs/ui/own_ui_toolkit.md` section 34)

- Hover must read on an ordinary monitor: the hovered colour is at least 1.18:1 (luminance contrast) from the resting one for buttons,
  rows, tabs and scroll bar thumbs and 1.10:1 for fields, in every design and scheme (`launcher_theme` checks all of them). The design's
  `controlHover` is the control mixed 14% (dark) or 10% (light) toward the ink; OkayTheme's Dear ImGui colours follow the same rule.
- Rows in Dear ImGui's look (Selectable, tree nodes, popup menu rows) keep Dear ImGui's places and sizes but draw OkayUI's way: a rounded
  highlight (the small radius) that eases in, a chevron instead of a filled triangle, and a 2-pixel accent bar on a selected row.
- Data tables (striped or sortable) light the row under the pointer across all its columns.
- Dock tabs: the label is dim unless the tab is chosen or under the pointer; the close cross shows on those two only. A dragged tab
  leaves a dimmed place, follows the pointer as a raised chip, and the area it would fill is shown before release (guide tiles: five over
  the leaf, four at the dock area's edges).

- Keyboard focus (rounds 5 and 6): the focus ring goes round the focused button, check box, combo, slider or drag cell, 2 px outside;
  on a list, menu or tree row it is drawn inside the row (rows touch). A list is one Tab stop; the arrows walk its rows.
- Label column (round 6, `StyleVar_LabelColumn`, used by the editor's Inspector): labels on the left in one column, every field's box
  starting at the same x; a label longer than the column ends in "...".
- Text (round 6): every size is rasterised at the size it is drawn and placed on whole pixels, so 125% and 150% are as sharp as 100%.

## Left

CodeEdit chrome, a design editor for custom designs.
