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

Every palette passes the checks in `test_okayui_design`: body text 7:1 and secondary text and status colours 4.5:1 on every
surface (WCAG 2.x), a label on the accent 3:1, tooltip text 7:1, placeholders 3:1. Names are saved in the file: never rename one.

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

## Left for round 2

Tables (row hover, sticky headers), docking tabs and splitters, CodeEdit chrome, Tab-key keyboard navigation so the focus ring
shows on every control (OkayUI has keyboard focus only for text fields today), a design editor for custom designs.
