# In-game UI

> **What this is:** Everything about UI inside a game: the launcher look, the runtime API, world-space and HTML UI, the component library, its audit and the quality record.
>
> **Status:** combined on 2026-11-20 from 7 older pages, one section each (listed below). Every fact was kept; text marked **Outdated** is kept for history.
>
> **Last checked:** 2026-11-20, paths and links against main.

## Contents

- [Game UI that looks like the launcher](#game-ui-that-looks-like-the-launcher) (was `docs/game_ui.md`)
- [In-game UI: looks, roles, widgets and the API](#in-game-ui-looks-roles-widgets-and-the-api) (was `docs/ui_runtime.md`)
- [UI component library](#ui-component-library) (was `docs/ui_library.md`)
- [In-game UI component library: audit (what exists, what is missing)](#in-game-ui-component-library-audit-what-exists-what-is-missing) (was `docs/ui_component_audit.md`)
- [World-Space UI (3D canvases)](#world-space-ui-3d-canvases) (was `docs/ui_world_space.md`)
- [HTML / CSS UI (UIDocument)](#html--css-ui-uidocument) (was `docs/ui_html.md`)
- [In-game UI quality](#in-game-ui-quality) (was `docs/in_game_ui_quality.md`)

## Game UI that looks like the launcher

*Was `docs/game_ui.md` until 2026-11-20.*

Game UI drawn with OkayUI (scripts' `ui_*` calls, the built-in windows like
**Find a server** and **Matchmaking**, the F1 test panel) uses the **OkaySpace
launcher's look** by default:
- the launcher's font: smooth Roboto text instead of the old 8×8 pixel font;
- its "Slate" colours: blue-tinted dark panels, hairline borders, the accent
  colour only for interactive states;
- rounded controls, pill tabs, soft window shadows and toasts.

The editor's Game view draws the same way, so what you see while testing is
what players get.

**Scene UI** gets the same look: canvas widgets (buttons, panels, text,
sliders, toggles, progress bars, inputs, dropdowns, tabs, tooltips) and HTML-style
UI documents.
- **Text font:** screen-space text uses the launcher font unless the scene picks
  another. The scene's choice lives in **View → Panels → Lighting → UI Font**: a `.ttf` /
  `.otf` file, or **Classic pixel font**.
- **Colours:** new widgets start with the launcher's colours. Saved scenes keep
  the colours they already had. To restyle existing UI in one go, open
  **View → Editors & Tools → UI Theme**, pick **OkaySpace** and click **Apply to All UI**.

More looks (twelve in all), roles such as Primary and Danger, radio buttons, windows, floating numbers
and a dialogue box are in [ui_runtime.md](in_game_ui.md#in-game-ui-looks-roles-widgets-and-the-api).

Prefer the old pixel look? Call `ui_style("classic")` in a script, or start the
game with `OKAY_CLASSIC_UI=1`.

### Adding UI in the editor

The **UI** button on the Scene toolbar switches to the UI editing view. Its
**+ Add** button (and **GameObject → UI**) lists everything you can add:

| Menu | What's in it |
| --- | --- |
| **Basic** / **Controls** / **HUD** / **Layout** | Single widgets: button, panel, image, raw image, text, slider, toggle, stepper, rating, tabs, input field, dropdown, progress bar, radial, minimap, crosshair, tooltip, scroll view, layout group, drag and drop. |
| **Kit (launcher look)** | Ready-styled building blocks: Card, Heading, List Row, Badge, Avatar, Divider, Primary / Secondary / Danger / Ghost buttons, Switch, Spinner and Notification. |
| **Prebuilt Screens** | Whole screens. **Menus:** Main Menu, Pause Menu, Settings, Confirm Dialog, Loading Screens (see below). **In game:** HUD, Health Bar, Dialog Box, Notification, Shop, Game Over. **Multiplayer:** Lobby, Scoreboard, Leaderboard, Matchmaking. |

The minimap (blips, zoom, a map you draw, the big map) is written up in
[minimap.md](../gameplay/minimap.md).

Select a panel or card first and new widgets and kit pieces go **inside** it.
Otherwise they go on the scene's Canvas, which is created if it's missing.
Everything you add is ordinary objects, so you can drag, resize, restyle or
delete each part. To reuse a screen you've customised, use **Save Selected as
UI**.

When you add several screens to one scene:
- **HUD pieces** move out of each other's way. For example, a Health Bar added
  after a HUD sits under the player card, and a Notification sits under the score.
- **Adding a centred menu** (Settings, Pause, Game Over...) hides the other
  centred menus on the Canvas, so they aren't drawn on top of each other. Turn
  them back on in the Hierarchy when you want them.

#### Styling and layout tools

- **Look** (at the top of every UI component in the Inspector): one-click
  styles in the launcher's look.
  - **Button:** Primary, Secondary, Danger, Ghost, Success, Outline, Pill, Icon.
  - **Panel:** Card, Raised, Glass, Outline, Pill, Dim overlay, Accent, Gradient.
  - **Text:** Title, Heading, Body, Caption, Muted, Number, HUD.
  - Progress bars, sliders, switches, inputs, dropdowns, tabs and radials have
    their own looks.
- **Copy style / Paste style**: copy one widget's look (colours, corners,
  borders, shadow, font) onto another, or onto the whole selection. Its position,
  size and text stay.
- **Wrap in Panel** puts the selected widgets on a new card that fits around
  them.
- **Fit to Children** shrinks or grows a card to wrap what's on it.
- With several widgets selected, the UI view's **Align** menu can also
  **Match width / height / size**, **Stack in a column / row** with a gap, and
  **Wrap in panel**.

Screens lay out against a 1280×720 canvas that **scales with the screen**, so
they keep their layout at any window size. The HUD's health bar and number
follow the `hp` variable, and the score follows `score`. The Loading Screen's
bar follows `loading` (0 to 1).

### Turning and scaling a frame

A **UI Frame** has **Rotation** and **Scale** in the Inspector, on top of its anchors.

**Rotation** is degrees clockwise around the pivot. **Scale** is a width multiplier and a height multiplier: 1 and 1 leaves the size alone, and two different numbers stretch the frame. Children turn and scale with the frame. Clicks follow the turned, scaled shape, including picking the widget in the editor.

A scene saved before these fields loads with no turn and a scale of 1.

```
ui_set_rotation("Badge", 28)
ui_set_scale("Badge", 1.45, 0.65)
ui_get_rotation("Badge")
ui_get_scale("Badge")
```

The same calls are the visual-script nodes `Fn.ui_set_rotation`, `Fn.ui_get_rotation`, `Fn.ui_set_scale` and `Fn.ui_get_scale`.

### Text fields

An **Input Field** takes typed text. Click it to focus. Typing goes in at the caret; Left and Right move it, Home and End jump to the start and end (of the line, in a multiline field). Backspace deletes the character before the caret and Delete the one after it; Backspace, Delete, Left and Right repeat while held. Enter submits (a sibling script's `on_submit`) unless the field is multiline, and Escape leaves the field.

While it is focused, Ctrl or Command with C copies the whole value, X cuts it, and V pastes. The field has no caret selection, so there is no select-all shortcut. A password field does not copy or cut. Pasted text stops at the field's character limit. Control characters are removed. A newline is kept only when the field is multiline.

The same text is what `get_clipboard_text()` and `set_clipboard_text("s")` read and write, and what the visual-script nodes **Get Clipboard Text** and **Set Clipboard Text** (under Text) use. In a built game, and in the editor's Play mode, that text is the operating-system clipboard. A test uses a memory clipboard, so it does not change the machine's clipboard.

### Tabs

**UI Tabs** (Add ▸ UI ▸ Controls ▸ Tabs, or Add Component ▸ UI Tabs) is a tab bar for
settings menus, inventories and other screens with pages. Click a tab to select
it. Each tab can name a **Page** object: the selected tab's page shows and the
others hide, along with everything parented under them. **Generate Pages**
makes and wires up one panel per tab. A sibling script's `on_change()` runs
whenever the tab changes.

- **Style**:
  - **Segmented** (the default): one control with the selected tab highlighted.
  - **Underline**: plain text tabs with a line under the selected one.
  - **Folder**: browser-style tabs. The selected tab joins the page below. Give
    the page the tab bar's *Selected Color* so they read as one piece.
  - **Pills**: separate rounded buttons with gaps between them.
  - **Vertical**: tabs stacked down the side, with the text left-aligned
    (settings sidebars).
- **Sizing**: *Equal width* shares the bar evenly. *Fit text* sizes each tab to
  its text plus **Padding**. **Spacing** sets the gap between tabs. A name that
  is too long for its tab is cut short with "...".
- **Per tab** (the row under each tab name): an **Icon** (a texture path, or
  drop one from the Project panel) drawn before the text, a **Badge** (0 = none,
  -1 = a small dot, a number shows the count, "99+" above 99) and **Enabled**.
  A disabled tab is dimmed and can't be selected. Clicks and the keys below skip
  it.
- **States and motion**: *Hover Tint* and *Pressed Tint* show under the pointer.
  With **Animate** on, the selection slides to the new tab. *Speed* sets how fast.
- **Too many tabs**: when the tabs don't fit (for example with a *Min Tab Size*
  set, or *Fit text* names that are longer than the bar), the bar scrolls. Use
  the mouse wheel, drag the tabs, or click the small arrow buttons at the ends.
  The selected tab always scrolls into view.
- **Keyboard and gamepad**: **Prev/Next Keys** (Q / E by default, any key name
  works) and the gamepad L / R shoulder buttons switch tabs while the bar is on
  screen. They don't fire while you are typing in a text field. *Wrap* goes from
  the last tab back to the first. With **Focusable** on, menu navigation can
  focus the bar, and Left / Right (A / D, the arrow keys, the D-pad) switch tabs.
  Scenes saved before these options existed load with both turned off, so their
  keys keep doing what they did.
- **Look** presets: Pills, Segmented and Minimal (segmented controls), plus
  Underline, Folder, Pill buttons and Sidebar.
- **Script**: `ui_set_tab("Tabs", 2)`, `ui_tab_value("Tabs")`,
  `ui_tab_text("Tabs")`, `ui_set_tab_badge("Tabs", 1, 3)` and
  `ui_set_tab_enabled("Tabs", 3, false)`. On the tab bar's own object you can
  use `set_tab(i)`, `tab_value()`, `set_tab_badge(i, n)` and
  `set_tab_enabled(i, on)`.

### Phones and tablets

- **The canvas fits any screen shape.** The Canvas's **Screen Match** decides
  how the 1280×720 reference layout fits a screen of another shape:
  - **Expand** (new canvases): the whole layout always fits, so a landscape menu
    stays whole on a portrait phone.
  - **Shrink**: the layout always covers the screen.
  - **Match Width or Height**: the blend slider, as before. Saved canvases keep it.
- **Safe Area.** Phones have notches, rounded corners and a home bar.
  - Click **Add Safe Area** on the Canvas, or use GameObject → UI → Safe Area.
  - Widgets inside the Safe Area stay clear of those parts of the screen. On a
    desktop the Safe Area just fills the screen.
  - Each side can be turned off, for example a background that should run under
    the notch.
- **Preview on a phone.** In the Game view, pick a phone or tablet: iPhone 16 Pro,
  16 Pro Max, 15, 13 mini, SE, Pixel 8 / 8 Pro, Galaxy S24 / S24 Ultra / A-series /
  Z Flip, OnePlus 12, Xiaomi 14, iPads, Galaxy Tab, Pixel Fold, Fire HD and more.
  - **Portrait / Landscape** flips the screen.
  - **Safe area** tints the unsafe parts.
  - The Safe Area container uses the chosen phone's insets.
- Text in the launcher font scales down with the canvas, so small screens and
  previews keep their layout.

### Images

- **UI Image** shows a picture. Pick the file with the **Image** slot (or drag
  one in from Project).
  - **Set Native Size** makes the widget the picture's own size.
  - It can be tinted, flipped, nine-sliced, partly filled (cooldowns, bars) or
    kept in proportion (**Preserve Aspect**).
  - Its **Shape** applies to the picture too: **Circle** for avatars, **Rounded**
    or **Pill** for cards.
  - The editor shows the real picture, as the game draws it. A file that can't be
    loaded says so on the widget.
- **UI Raw Image** draws a texture through a **UV rect**:
  - **Offset** picks which part shows.
  - **Size** above 1 tiles the texture (3, 2 repeats it 3 across and 2 down).
  - A negative size mirrors it.
  - **Tile to fit** repeats it at its own pixel size.
  - **Scroll / s** moves it every second: scrolling backgrounds, conveyors, skies.

### Loading screens

**Prebuilt Screens → Loading Screens** has two kinds:

- **UI you can edit**: Loading Screen, Minimal, Card, Splash (logo) and
  Cinematic. These are ordinary widgets, so you can move or restyle every part.
  Their bar follows the `loading` variable (0 to 1).
- **Automatic overlay**: a **Loading Screen** component in one of seven layouts.
  It covers the game when the scene starts (or when a script calls
  `Begin("NextLevel")`), fills its bar over **Duration**, then reveals the scene
  or loads **Load scene when done**.

The component's Inspector shows a live, animated preview. Its **Look** buttons
switch layout in one click, and set colours, bar and spinner to match. Your
text, tips and images stay.

| Layout | What it looks like |
| --- | --- |
| **Classic** | Title, tip and bar centred, at the positions you set. |
| **Minimal** | A spinner and a small title in the middle, with a thin bar. |
| **Cinematic** | Letterbox bars, the title bottom-left, the bar along the lower bar. |
| **Splash** | A big centred logo (or the title) with a thin bar under it. |
| **Card** | A raised card in the middle holding everything. |
| **Corner** | Title bottom-left, spinner bottom-right, bar along the bottom edge. |
| **Ring** | A round progress ring with the percent inside. |

More options:

- **Bar style:** Solid, Segmented, Shimmer, or Indeterminate (a sliding chunk,
  for loads with no known length).
- **Spinner style:** Dots, Arc, Bars or Pulse.
- **Subtitle:** a second line, such as the level name or chapter.
- **Logo:** an image, with an optional gentle pulse.
- **Slow zoom:** the background image slowly zooms in.
- **Next tip every N seconds:** cycles through the tips while the screen is up.

### Building blocks

Besides the usual widgets (text, buttons, checkboxes, sliders, inputs, tabs,
tables, trees, combos), there are the pieces the launcher is made of. These
work well for menus, lobbies, scoreboards and HUDs.

| Script | C++ (`OkayUI::`) | What it is |
| --- | --- | --- |
| `ui_heading("Lobby")` | `Heading` | a page title with the accent underline |
| `ui_caption("NOW PLAYING"[, r, g, b])` | `Caption` | small coloured label |
| `ui_text_dim("…")` | `TextDisabled` | secondary text |
| `ui_text_wrapped("…")` | `TextWrapped` | text that wraps to the window |
| `ui_primary_button("Play")` | `PrimaryButton` | accent call-to-action button (1 when clicked) |
| `ui_card_begin("id")` … `ui_card_end()` | `BeginCard` / `EndCard` | a raised rounded card that fits its content |
| `ui_list_item(title[, subtitle[, right[, selected]]])` | `ListItem` | avatar + title + dim subtitle (+ right text); 1 when clicked |
| `ui_badge("3 / 4"[, r, g, b])` | `Badge` | a pill tag (default: accent) |
| `ui_avatar("Maya"[, size])` | `Avatar` | a coloured circle with the initial |
| `ui_key_value("Ping", "23 ms")` | `KeyValue` | dim label left, value right |
| `ui_spinner()` | `Spinner` | a busy spinner |
| `ui_tabs("Players\|Match", tab)` | `TabBar` | pill tabs; returns the selected index |
| `ui_input("Name", text)` | `InputText` | a text field; returns the text |
| `ui_toast("Saved"[, kind[, seconds]])` | `Toast` | a pop-up in the corner; kind 0 info, 1 success, 2 error |
| `ui_spacing(h)` | `Spacing` | vertical space |
| `ui_style("launcher" \| "classic")` | `UseLauncherLook` | switch looks |

A lobby screen in a script:

```
var tab = 0;
function update(dt) {
    ui_begin("Lobby", 40, 40, 420, 520);
    ui_caption("NOW PLAYING");
    ui_heading("Star Arena");
    tab = ui_tabs("Players|Match", tab);
    if (tab == 0) {
        ui_card_begin("players");
        for (var i = 0; i < net_roster_count(); i = i + 1) {
            var id = net_roster_id(i);
            ui_list_item(net_peer_name(id), "Ready", "" + net_score(id));
        }
        ui_card_end();
    } else {
        ui_key_value("Mode", "Ranked");
        ui_key_value("Ping", "" + net_ping() + " ms");
    }
    if (ui_primary_button("Ready")) { ui_toast("You're ready", 1); }
    ui_end();
}
```

Press **F1** in a development build (Build Settings > Development build) to see the Test UI panel and a sample **Lobby**
built from these blocks (`shared/OkayTestPanel.hpp` has the code).

### Crisp at any size

The UI is laid out in reference units and drawn at the real pixel size of the screen, so it stays sharp when the
window is big, the screen is high resolution, a Scale With Screen Size canvas scales up, or you zoom the editor view.
Nothing is drawn small and then stretched.

- **Text:** the TrueType face is baked at (font size x canvas scale) pixels, anti-aliased, one cached size per step
  (every whole size up to 32 px, then about 6% steps, so dragging the zoom does not bake hundreds of sizes).
- **Shapes:** rounded boxes, pills, circles, borders and shadows were already anti-aliased. Circles, rings, triangles,
  thick lines and polygons (minimap, joystick, check marks, the crosshair ring, route lines) are now rasterised on the
  device pixel grid with analytic coverage (`MMFillCircle`, `MMDrawRing`, `MMFillTriangle`, `MMThickLine`,
  `MMFillPoly` in `UI/UIRenderer.cpp`): one pixel of soft edge at any scale, no logical-pixel blocks.
- **Pictures:** sampled linearly when drawn bigger or smaller than they are (`Painter::SetImageFilter`); the batcher's
  image atlas repeats edge pixels into its gutter and keeps pieces of a picture (9-slice, sprite sheets) from picking up
  their neighbours when scaled. Round pictures (circle, rounded corners) use enough segments for any zoom and a one-pixel
  soft edge.
- **UI filtering setting:** Smooth is the default. Pixel-art (hard shape edges, nearest-neighbour pictures, the 8x8 bitmap
  text) is the scene's UI font set to "pixel" (Lighting window, UI Font). Escape hatch if something looks wrong:
  start with the environment variable `OKAY_UI_FILTER=pixel` (hard edges and nearest pictures again) or
  `OKAY_UI_FILTER=smooth` (smooth even for a pixel-art scene).
- **Not changed:** the 8x8 bitmap font that overlays draw outside the UI pass (inventory, crafting, F1 panel) and widgets
  turned or scaled by a UI Frame (their rectangles are drawn as geometry without a soft edge).

- **UI Quality (Fast / Good / Best / Old look):** exact edge coverage, snapped borders, text on the pixel grid with
  kerning, smooth soft shadows and filtered pictures. Lighting window > UI Font > UI Quality, or the environment variables
  `OKAY_UI_QUALITY=fast|good|best|old` and `OKAY_CLASSIC_UI=1`. Full write-up, before/after numbers and what is not done:
  `docs/ui/in_game_ui.md`; example scene `examples/ui_quality/ui_quality.okayscene`.

Try it: `docs/try_it.md` crisp1. Tests: `test_ui_crisp` (needs no window; measures text, ring and picture sharpness).

### Theming

`OkayUI::Style()` returns the live theme; change colours or sizes in place.
`OkayUI::SlateTheme()` returns the launcher palette, so you can start from it.
- **Accent:** `Style().accent`.
- **Text size:** `textScale` (2 = 16 px; the font is baked at 13, 16, 20, 26, 34
  and 46 px, so those are the crispest).
- **Rounding:** `rounding` and `windowRounding`.
- **Window padding and shadow:** `windowPadding` and `windowShadow`.
- **Temporary colours:** `PushStyleColor` / `PopStyleColor`.
- **Bigger or smaller text for a few widgets:** `PushTextScale` /
  `PopTextScale`.

## In-game UI: looks, roles, widgets and the API

*Was `docs/ui_runtime.md` until 2026-11-20.*

This page covers the runtime UI added on top of [game_ui.md](in_game_ui.md#game-ui-that-looks-like-the-launcher): twelve looks you can
switch with one click, widget roles (Primary, Danger, Ghost...), radio buttons, draggable windows,
floating damage numbers, a dialogue box, the UI Gallery, and the script and C++ calls for all of it.

### What changed (plain words)

- **Twelve looks.** The UI Theme now has Dark, Light, Sci-fi and Fantasy (as before) plus **Modern Flat,
  Cartoon, Retro Pixel, Minimal, Neon, Glass, Military and Horror**. Pick one in **View > Editors & Tools >
  UI Theme**, or on a UI Theme component, and press **Apply to all UI**.
- **Roles.** Give any widget a role with the **UI Style Variant** component: Primary, Secondary, Danger,
  Ghost, Outline or Pill. A Danger button is red in every look; change the look and the roles follow.
- **Save a look as a file.** **Save this look to the project** writes `Assets/UI/<name>.okaytheme`. The
  UI Theme window lists the saved looks; click one to load it. Scripts: `ui_load_theme("Assets/UI/Forest.okaytheme")`.
- **UI Gallery.** **File > Examples > UI Gallery** opens one scene with every widget and a theme picker.
  Press Play and pick another look: the whole page restyles.
- **New widgets.** Radio buttons, a draggable window, floating damage numbers, a dialogue box with typing
  text and choices, a nameplate, an inventory screen. They are in **GameObject > UI > Prebuilt Screens**.
- **Fixes** (each has a test in `tests/test_runtime_ui.cpp`):
  - Typing in a text box no longer moves the menu focus or presses the focused button (W, A, S, D, Space
    and Enter were reaching the menu).
  - Steppers and star ratings can now be reached and changed with the keyboard and gamepad. A star
    rating that is display-only is skipped.
  - Hover and click tests follow the order the UI is drawn in (Canvas order, then layer, then hierarchy).
    Before, the last object created always won, even when a different Canvas drew on top of it.
  - New **Blocks Pointer** switch on a Panel: widgets drawn underneath cannot be hovered or clicked through
    it. Off for old scenes, on for the cards and windows the templates build.
  - Dragging a widget picks the drop target by draw order too.
  - Text inside a Scroll View, List View or Mask now scrolls and clips with it (before, labels stayed put
    while the widgets next to them scrolled away).
  - Slot grids keep the look's text colour and can show empty places blank.
  - Buttons showed a default blue in the editor's Scene and Game (edit mode) views once a theme was applied
    (their eased colour was never set), and faded in from that blue when the game started. They now show
    their own colour from the first frame.
  - Text fields counted bytes: a backspace on "é" left half a character, a length limit could cut a
    character in two, and a password box showed two stars for it. They now count whole characters.
  - Applying a theme gave every progress bar a percent label, even thin ones with no room for it, and left
    inventory slot grids in their old colours. Thin bars (under 14 px) stay clean and slot grids follow the look.
- **Saving.** Everything above is saved as optional records. A scene saved by an older version loads
  as it was, and a scene using the new parts opens in an older version without them (it skips the
  records it does not know).

### The twelve looks

| Look | Feel |
| --- | --- |
| Dark | blue-black cards, bright blue accent (default) |
| Light | warm paper, deep blue accent |
| Sci-fi | black glass, cyan outline and glow |
| Fantasy | parchment and wood, gold edge |
| Modern Flat | flat slate, no shadow, thin border |
| Cartoon | bright cream, thick dark outline, hard shadow, big corners |
| Retro Pixel | square corners, 2 px white border, hard black drop shadow, instant changes |
| Minimal | near-white, black buttons, hairline border |
| Neon | dark violet, magenta and cyan glow |
| Glass | layered glass: a faint white-tinted card with a bright rim, a soft multi-tap shadow and a glow, over a deep blue gradient backdrop with colour blobs that show through the cards. It does **not** blur what is behind it (the UI draw path cannot read the screen back), so it is called Glass, not frosted. Scenes that name Glass still load |
| Military | olive and khaki, square, gold accent |
| Horror | near-black, dried-blood red, dark red glow |

A theme holds colours (panel, accent, text, muted, button and its hover, pressed and disabled states,
danger, success, border, shadow, glow), corner radius, border width, gradient, shadow, glow, opacity,
font scale, transition speed, hover grow and an optional font file. **Apply** writes them onto the
widgets; nothing moves.

- **Per widget:** put a UI Theme on a panel and Apply: only that panel's widgets change.
- **Global:** tick **Style the whole scene** on the theme (the UI Theme window does this on the Canvas).
- **Roles:** add **UI Style Variant**; the **Role** list picks the variant.

### Widgets

| Widget | Where | Notes |
| --- | --- | --- |
| Radio buttons | UI Toggle with Style = Radio | Same **Group** name = one choice. Empty group = the radio buttons under one parent. Picking one turns the others off; clicking the picked one keeps it. |
| Window | GameObject > UI > Prebuilt Screens > Window | A title bar with **Drag the parent** and **Keep on screen** on a UI Draggable, and an x button that hides it (On Click: `HideObject`). The window carries its own Canvas one step above the page, so it and its contents draw above the page and take clicks first; dragging a window brings it in front of other windows. |
| Damage Numbers | UI Damage Numbers | `Spawn(text, kind, pos)` (canvas pixels) or `SpawnAtWorld(text, kind, point)`; the numbers are made under the scene's Canvas. Buttons can call `Hit`, `Crit`, `Heal`, `Miss`, `Info`. Numbers drift by a fixed pattern, so a replay looks the same. |
| Dialogue Box | UI Dialogue Box | Children named `Box`, `Speaker`, `Body`, `Choice1-3`, `Next`. Typing speed, skip on click or Space, up to four choices. |
| Nameplate | Prebuilt Screens > Nameplate | A plate with a name, a level pill and a health bar, bound to the character it belongs to. Make the "in the world" version (a World Space UI object that faces the camera) a child of a character (with a character selected, the menu item does it for you and puts the plate above its head): it shows that character's object name, its Stats level (level 1 without Stats) and its health (Health Stat, Stats, Survival Stats or NPC Controller), live. With no such parent it is a preview: "Goblin", "Lv 1" and the player's published health. The bindings are ordinary UI Text Bind / UI Bar Bind keys (`{owner.name}`, `Lv {owner.level\|1}`, `owner.healthFrac`), so you can edit them. The plate is 220 x 62 design pixels (name 20 px, level 15 px, all inside the card with 12 px to spare). Both texts use Best Fit, so a long name or a three-digit level shrinks to fit instead of running out of the card or the pill (down to about 5 px, and no smaller than 8 px in the pixel font: a name of more than about 45 characters, or 13 in the pixel font, still overflows, there is no ellipsis). In the world it keeps this size at any distance (World Space UI **Constant size** is on, so the text stays readable far away) and hangs above the object's point, centred on it: put the plate object at the character's head. Turn Constant size off and it scales with distance like any world UI. |
| Inventory | Prebuilt Screens > Inventory | A bag screen with a slot grid you can click. A **UI Inventory Bind** on the grid shows the real Inventory: the Player's, else the one named in **Inventory Name**, else the first in the scene. Each slot shows its item and stack size ("Potion 7"), empty places stay blank, and the grid gets as many rows as the bag has room for. `SelectedItem()` and `SelectedCount()` say what is in the clicked slot. With no Inventory in the scene the grid keeps its sample names. It shows the bag; moving items between bags is done by the Inventory itself. |
| Rich text | Text > **Rich Text** | See below. |

Already there and unchanged: buttons (hover, press, focus states), toggles and switches, slider, stepper,
rating, progress bars (linear, segmented, trail, gradient), radial progress and spinner, dropdown, input
field, scroll view with masking and scroll bars, list view, tabs, tooltip, modal, toast, context menu,
crosshair, minimap, slot grid, layout groups, UI Frame (anchors, pivot), safe area and the Canvas scaler
(Constant Pixel Size, Scale With Screen Size with Expand, Shrink or Match).

**Rich text.** Turn on **Rich Text** on a Text (in the Inspector, the **Rich Text** checkbox in the Text section: hover it for the tags; or
`ui_set_rich("Label", true)`) and write tags
in the text: `[color=#ffcc33]gold[/color]`, `[color=red]red[/color]`, `[b]bold[/b]`, `[i]italic[/i]`,
`[size=150]big[/size]` (a percent of the text's size, 25 to 400; nested sizes multiply). Colours are
`#rgb`, `#rrggbb`, `#rrggbbaa` or a name (white, black, red, green, blue, yellow, orange, gold, cyan, magenta,
purple, pink, gray). Tags nest. A tag that is not one of these is shown as plain text, and `[[` shows a `[`.
The tags take no room: wrapping, alignment, upper-casing and the typing effect all work on the plain text.
On a font file (TTF) italic really slants every glyph (the quads are sheared about the baseline) and bold
thickens the strokes with a three-tap smear; the built-in pixel font leans for italic and draws twice for bold.
It works on screen-space text and on **world text** (a Text with Screen Space off, which the 2D player and the
editor's 2D Scene view draw at its place in the world): the player draws world text through
`ui2d::DrawWorldText`, which reads the same tags, and the shadow and outline follow the runs (with the text's own
bold and italic). With Rich Text **off**, world text looks exactly as before (italic is not drawn on world text, bold
is a second copy one step to the right). With it **on**, the Text's own Italic now slants world text too. Limits for
every rich text, on screen and in the world alike: text over 2,000 bytes is not parsed but shown as written, tags
included (so a long dialogue line over 2,000 bytes shows its `[color=...]` tags; split it into shorter texts), at most
16 tags may be open at once, and nested sizes are held to 25..400 percent. **A line is as tall as the largest size on
it**: a sized run sits on the bottom of its line and pushes what follows to the right, the line below it starts under the
big run (it used to start at the plain line height and the big letters climbed into the line above, and a big run on the
first line reached above the text's top), and a line made only of small text is a small line.
**Links and pictures (UI library slice 2).** `[url=manual]read it[/url]` draws the words underlined in a link colour (a `[color]` inside wins) and, with a **UI Rich Links** component on the same object, a click runs `on_link()` on the object's script and stores the target in the Prefs value `ui_link`; the target may be about 100 characters. `[icon=Iron Sword]` draws one glyph-sized picture inline: an item from the item database (its icon image, else a coloured tile with its first letter) or an image path such as `[icon=Assets/gem.png]`. Both draw on screen-space text only, and the pictures use the cell of the pixel font. See [ui_library.md](in_game_ui.md#ui-component-library).
`TextRenderer::PixelHeight()` counts it, so centring and Best Fit see the real height. The Line
Spacing setting is added once per line, never multiplied. Text with no `[size]` lays out exactly as before. The
**width** of a line is still measured at the text's own size, so wrapping is not changed by `[size]` and a centred
line with a big run leans right. The tooltip's own `<b>` and `#rrggbb` tags are unchanged. Tests: `runtime_ui`
(screen text), `world_text_rich` (the size tag, runs, world text colours, sizes, shadow and outline, a sized run that
must not overlap the line above or reach above the text's top, and the line heights drawn and measured). World text is
drawn by the **flat (2D) player pass and the editor's 2D views only**:
a 3D scene does not draw a Text whose Screen Space is off (use a Text under a World Space UI object there); the font
is the 8x8 bitmap font unless the Text has a font file.

How to try it (world text): add a Text, turn its **Screen Space** off so it is drawn in the world, turn on **Rich Text** and type
`Hello [color=#ffcc33]gold[/color] [size=150]big[/size] [b]bold[/b]`. The Scene view and Play show the gold word, the
bigger word sitting on the same line, and the bold one; turn Rich Text off and the tags show as written.


(A headless render of what `ui2d::DrawWorldText` draws, made by test `ui_text_shots`; the sign in `examples/ui_text` is the same
kind of text. Test `world_text_rich` run with `OKAY_SHOT_DIR=<folder>` paints a smaller picture of the same kind into that
folder as `world_text_rich.png`.)

**Readable in every look.** The themes promise a contrast of at least 3 to 1 between a button's label and
what it sits on, for all 12 looks and all 7 roles. A Ghost or Outline button uses the accent for its label when
that reads, else the look's text colour (so Fantasy's gold on parchment becomes dark brown). `UITheme::Contrast`
gives the ratio; a test checks every look and role.

### Script calls (okayscript)

| Call | What it does |
| --- | --- |
| `ui_set_variant("Quit", "danger")` | Give a widget a role and style it now. Names: primary, secondary, danger, ghost, outline, pill, default (or the number). A third argument names the theme. |
| `ui_theme_preset("Neon")` | Restyle the scene with a look (name or number 1-12). A second argument names the theme. |
| `ui_load_theme("Assets/UI/Forest.okaytheme")` | Load a saved look and apply it. |
| `ui_radio_value("Quality")` | Index of the picked radio button in the group (-1 = none). |
| `ui_set_radio("High")` | Pick a radio button by object name. |
| `ui_damage_number("-25", x, y [, "crit"])` | Float a number up from a canvas point. Kinds: crit, heal, miss, info. |
| `ui_dialogue_show("Mara", "Hello", "Yes", "No")` | Show a line with up to four choices. |
| `ui_dialogue_choice()` / `ui_dialogue_open()` | Last picked choice (-1 none) / is the box up. |

### App widgets

Search bar, date and time, file picker, accordion, breadcrumbs, pagination, skeleton and bottom sheet are ordinary components. Add them from **GameObject > UI > App** (or the same names in the hierarchy's UI menu). Each one has plain-English fields in the Inspector, saves with the scene, and takes part in keyboard and gamepad navigation.

| Widget | What it does | Script |
| --- | --- | --- |
| UI Search Bar | Keeps the rows whose text contains the query. An empty query shows every row | `ui_set_search`, `ui_search_query`, `ui_search_count`. `on_change` when the text or the row changes, `on_submit` on Enter |
| UI Date and Time | Year, month, day, hour and minute. A day the month does not have is pulled back (31 February becomes the last day of February) | `ui_set_date`, `ui_set_time`, `ui_date_text`. `on_change` when the value changes. Left and right step the day |
| UI File Picker | Lists one folder, in the game. It cannot open anything outside that folder, and it does not open the computer's own file window | `ui_file_set_root`, `ui_file_open`, `ui_file_selected`. `on_change` when the folder or the file changes, `on_submit` when a file is chosen |
| UI Accordion | Headings that open and close. With "more than one" off, opening one closes the others | `ui_accordion_toggle`, `ui_accordion_open`. `on_change` when a section opens or closes |
| UI Breadcrumbs | A path such as Home / Library / Books. Choosing a crumb drops the ones after it | `ui_breadcrumb_select`, `ui_breadcrumb_path`. `on_change` when the path changes |
| UI Pagination | Previous, page numbers and next. The page stays inside 1 to the page count | `ui_set_page`, `ui_page`, `ui_page_count`. `on_change` when the page changes. Left and right move a page |
| UI Skeleton | Grey bars (and an optional circle) while something is loading | `ui_set_loading`. `on_change` when the placeholder is shown or hidden |
| UI Bottom Sheet | A panel that slides up. The handle opens and closes it | `ui_sheet_open`, `ui_sheet_close`, `ui_sheet_open_state`. `on_change` when it opens or closes. Confirm on a gamepad does the same |

The same calls are visual-script nodes named `Fn.ui_set_search` and so on. On the object itself, the nodes are `UISearchBar.SetQuery`, `UIDateTimePicker.SetDate`, `UIFilePicker.Open`, `UIAccordion.Toggle`, `UIBreadcrumbs.Select`, `UIPagination.SetPage`, `UISkeleton.SetLoading` and `UIBottomSheet.Open` / `Close`.

File > Examples > UI Gallery includes one of each, on the App card.

Events on the widget's own script: `on_click`, `on_toggle` (radio buttons too), `on_change` (sliders,
steppers, ratings, dropdowns, tabs), `on_submit` (input fields), `on_drag_start`, `on_drag`, `on_drop`,
`on_receive`, `on_tab_close`, plus the dialogue box's `on_dialogue_line`, `on_dialogue_choice` and
`on_dialogue_done`.

A button needs no script for these: set **On Click** to the object and one of the functions below.

| Object has | Functions |
| --- | --- |
| any object | `HideObject`, `ShowObject`, `ToggleObject` |
| UI Dialogue Box | `Choose` (the amount is the choice number), `Advance`, `Close` |
| UI Damage Numbers | `Hit`, `Crit`, `Heal`, `Miss`, `Info` (the amount is the number shown) |
| UI Modal | `Open`, `Close` |

### C++ calls (what visual-script nodes call)

All of these are plain functions on components, with no UI state to set up first. Headers are under
`okay/Components/` and `okay/Scene/`.

```cpp
// Looks and roles (UITheme.hpp, UIStyleVariant.hpp)
UITheme* th = UITheme::Find(scene, "");              // the scene's first theme, or by name
th->ApplyPreset((int)UITheme::Preset::Neon);          // fill the colours
th->Apply();                                          // write them onto the widgets
UITheme::PresetFromName("retro pixel");               // 7, or -1
th->ApplyVariant(go, UITheme::Variant::Danger);       // style one widget as a role
go->AddComponent<UIStyleVariant>()->variant = 3;      // keep the role for the next Apply
SaveUIThemeAsset(*th, "Assets/UI/Mine.okaytheme");    // LoadUIThemeAsset(...) reads it back

// Radio buttons (UIToggle.hpp)
toggle->Select();  toggle->RadioSelected();  toggle->RadioGroupMembers();

// Floating numbers (UIDamageNumbers.hpp)
dn->Spawn("-25", UIDamageNumbers::Kind::Crit, {640, 300});
dn->SpawnAtWorld("+40", UIDamageNumbers::Kind::Heal, {x, y, z});

// Dialogue box (UIDialogueBox.hpp)
box->Show("Mara", "Hello", {"Yes", "No"});  box->Advance();  box->Choose(1);  box->LastChoice();

// Looking at the UI (UIElement.hpp, EventSystem.hpp)
UIDrawsAbove(a, b);                                   // is a drawn over b?
UIRaycast(scene, point);                              // the topmost widget under a point
NavigateUI(scene);                                    // keyboard / gamepad menu step
```

Each one is also registered in the property registry (`UITheme`, `UIStyleVariant`, `UIDamageNumbers`,
`UIDialogueBox`, and the new fields of `UIPanel`, `UIToggle` and `UIDraggable`), so nodes that read and
write properties by name already reach every setting. See [properties.md](../engine/properties.md).

### The UI Gallery

`UIGallery::BuildGallery(scene, preset)` (in `okay/Scene/UIGallery.hpp`) builds the gallery into any
scene. File > Examples > UI Gallery does that in a fresh 2D scene. The same header has
`AddInventoryScreen`, `AddDialogueScreen`, `AddWindow`, `AddNameplate` and `ApplyThemePreset`.

#### Binding UI to the character it belongs to (`owner.` keys)

A UI Text Bind format and a UI Bar Bind variable can start with `owner.` to read the character the UI object belongs
to: the nearest object above it (its parent first) that has a Health Stat, Stats, Survival Stats, NPC Controller or
Character (`okay/Components/UIOwnerData.hpp`).

| Key | Value |
|---|---|
| `owner.name` | the character object's name |
| `owner.level` | its Stats level (nothing without Stats) |
| `owner.health`, `owner.maxHealth` | health and its maximum, from the first of Health Stat, Stats, Survival Stats, NPC Controller |
| `owner.healthFrac` | health / maximum, 0 to 1 (for a bar) |
| `owner.healthPct` | the same, 0 to 100 |

With no owner at all, a key falls back to the global variable of the same name without `owner.` (so a nameplate on its
own still follows the player's `healthFrac`). An owner that lacks the data gives nothing: a text shows its `|default`, a
bar keeps its fill. It never shows the player's value on another character. Test: `ui_nameplate_bound`.

In the editor, select the nameplate's Name label or its Level label and look at **UI Text Bind** in the Inspector (the **Format** row), or select the
Health bar and look at **UI Bar Bind** (the **Variable** row). Hover either row for the list of keys. Under them a line says
`Owner: <the character's name>`, or that there is none (then an owner key reads the variable without `owner.`, the player's).
`BuildGallery(scene, preset, true)` (File > Examples > UI Gallery (phone)) lays the same widgets in one scrolling
column on a 440 x 960 canvas inside a Safe Area, for portrait phones.

The gallery also has working pieces, not just pictures: a "Gallery Bag" Inventory bound to the Slots grid with
`UIInventoryBind`, and a **Talk** button that calls the dialogue box's **Restart** action. File > Examples
saves the scene into the open project (`Assets/Scenes`, never overwriting an existing file), so Play works at
once; with no project open it stays an unsaved scene.

### What each control does (audit)

| Piece | In the editor | Visible in Play | Test that fails if it did nothing | In the gallery |
|---|---|---|---|---|
| UITheme and 12 presets | UI Theme window, inspector | colours, shapes | `test_runtime_ui` (every role and theme pair) | yes |
| UIStyleVariant | role chips, inspector | button/panel colours | `test_runtime_ui` | yes |
| Theme files (.okaytheme) | UI Theme window | restyles the scene | `test_runtime_ui` (save, load, `ui_load_theme`) | by preset |
| UIDamageNumbers | Add Component, inspector | floating numbers | `test_runtime_ui` | yes |
| UIDialogueBox | Add Component, inspector | typed text, choices, Restart | `test_runtime_ui` | yes |
| UIInventoryBind | Add Component, inspector | slots show the Inventory | `test_runtime_ui` | yes |
| Radio toggles, draggable window | inspector | one on per group, window moves | `test_runtime_ui` | yes |
| Nameplate | UI add menu (and in the world) | name, level and health bar bound to the parent character (`owner.` keys); with no character, the player's health; every piece inside the card at any size | `test_runtime_ui` (Hit and Heal change the bar), `ui_nameplate_bound` (two characters show their own values; missing data never shows the player's; what is drawn stays inside the card at five sizes and two fonts), `ui_text_example` (the shipped sample), `ui_text_shots` (pictures) | yes, with Hit / Heal buttons |
| `ui_*` script calls | text scripts | as above | `test_runtime_ui` | n/a |

### Dropdown: long lists

A dropdown with `maxVisible` set shows that many options and scrolls the rest: mouse wheel, keys, or
dragging inside the open list. A press on an option only arms a pick. If the pointer (mouse, or a finger,
which drives the same pointer) moves more than 10 pixels, the press becomes a scroll by whole rows and the
list stays clamped; letting go never picks after a drag, even over a row. A tap (press and release on the
same row without moving) picks it. Lists that do not scroll still pick on press. No momentum.

## UI component library

*Was `docs/ui_library.md` until 2026-11-20.*

A game maker should be able to build any HUD, menu, inventory, dialogue, settings or app-style screen from ready parts.
Most parts already existed. [ui_component_audit.md](in_game_ui.md#in-game-ui-component-library-audit-what-exists-what-is-missing) lists every part asked for, whether it exists
and what is missing. The library is built in slices; each slice adds the missing parts, a shipped example scene and tests.

| Slice | Content | State |
|---|---|---|
| 1 | Essentials and layout: range slider, input validation, aspect-ratio box | built (this page) |
| 2 | Game parts and data-driven glue | not started |
| 3 | App parts | not started |

### How to try slice 1

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

### The parts

#### UI Range Slider (`Components/UIRangeSlider`)

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

#### UI Input Validator (`Components/UIInputValidator`)

Rules for the UI Input Field **on the same object**: `kind` (Anything, Email, Whole number, Number, Letters, Letters and
digits), `required`, `minLength`, `maxLength` (characters, not bytes), a number range, `mustMatch` (the name of another
input field that must hold the same text, for "repeat the password"), your own `message`. It draws a red ring and the
sentence under the field, and a green ring when `showOk` is on. With `quietUntilTouched` (the default) nothing shows until
the player has typed. The script gets `on_valid()` and `on_invalid()` when the answer changes. Typed script API:
`UIInputValidator.Check()`, `IsValid()`, `Error()`.
The email rule is simple on purpose: one `@`, no spaces, a dot inside the part after it. Numbers are plain digits (no
`nan`, `inf` or hex). Saved as the `uivalidate` record.

#### UI Aspect Ratio (`Components/UIAspectRatio`)

A container that keeps its own box at a fixed width : height inside its UI parent (or the screen). Modes: **Fit** (the
biggest box that fits), **Fill** (the smallest box that covers), **Width controls** and **Height controls**. `alignX` and
`alignY` place the box in the spare room (0.5 is centred), `padding` keeps empty space around it. Children are laid out
inside the box, so a stretch-anchored panel under it fills the box. Saved as the `uiaspect` record.
Not yet handled: a world-space Canvas (the box uses the screen path only).

### What each file was written from

All code is our own, written for this engine. The range slider from the plain idea of a two-handle slider, the validator
from the usual form rules (length, number, email shape), the aspect box from the geometry of fitting a rectangle of a given
ratio into a box. They reuse only our helpers (`UIWidgetUtil`, `UIShape`, the UI layer table, the property registry and the
scene text format). No third-party code, fonts, images or SDKs were added.

### Tests

`ui_library` (maths, pointer, keys, rules, round trips), `ui_library_example` (loads and plays the shipped scene),
`ui_library_shots` (headless pictures), plus the existing `ui_layer_table`, `save_coverage` and `property_registry`, which
now list the new parts. Nothing in this slice was run on Windows or in a real window.

### Internal record: slices 2 to 4 (branch claude/ui-lib-4)

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

## In-game UI component library: audit (what exists, what is missing)

*Was `docs/ui_component_audit.md` until 2026-11-20.*

Written 2026-11-16 for the owner's request "a full in-game UI component library". This page is about the **in-game
UI** (Canvas, UI Panel, UI Button, ... and `File > Examples > UI Gallery`). It is **not** the editor's own toolkit
(`okayui/`, the editor panels): that is a separate effort and was not touched.

Method: `scripts/find_existing.py` was run for ui / widget / button / slider and the headers were read
(`engine/include/okay/Components/UI*.hpp`, `Scene/UITemplates.hpp`, `Scene/UIGallery.hpp`), together with
`docs/feature_map/12-ingame-ui.md`, `docs/ui/in_game_ui.md`, `docs/ui/in_game_ui.md`, `docs/status/UNFINISHED.md` and the tests.
Each row says **exists (where)** or **gap**. "Slice N" says which slice of the library builds the gap
(slice 1 = essentials and layout, slice 2 = game parts and data-driven, slice 3 = app parts).
`New in slice 1` rows are built by this change.

The honest summary: most of the library already exists. About 55 of the 100 parts asked for are present, tested and
reachable from `GameObject > UI`. The gaps are mostly the "built on top" parts (radial menu, data table, tree,
menu bar, wizard, kill feed, ammo counter, objective tracker) and the data-driven glue (two-way binding, list
templates from an array, conditional visibility).

### Essential

| Part | Status | Where / gap |
|---|---|---|
| Label | exists | `TextRenderer` with Screen Space on; Create > UI > Basic > Text. Localised text key (`LocalizedText`) |
| Rich text (bold, italic, colour, size) | exists | `UI/RichText.hpp`, `[b] [i] [color=] [size=]`, `docs/ui/in_game_ui.md` |
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

### Game

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

### App

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

### Layout

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

### Data-driven

| Part | Status | Where / gap |
|---|---|---|
| Bind a widget to a variable (one way) | exists | `UITextBind`, `UIBarBind`, `UIInventoryBind` |
| Two-way binding (a slider writes the variable back) | gap | slice 2 |
| List templates repeated from an array | partly | `UIListView` binds script rows; binding to an array variable is feature map 12.13 "Open"; slice 2 |
| Conditional visibility from a value | gap | `ui_show` / `ui_hide` by script only; slice 2 |
| Localisation keys | exists | `LocalizedText` on text, button, toggle, input, tooltip; `SceneLocalization` |
| Styles and themes from a data file | exists | `UITheme` + `.okaytheme` files, `UIThemeAsset`; selectors and states are still open (12.7) |
| UI described in a text asset that loads into a Canvas | exists | `UIDocument` (line markup and HTML + CSS subset), `docs/ui/in_game_ui.md` |
| Hot reload in the editor | partly | `UIDocument.Rebuild` by button; no file watcher; slice 2 |

### Slice 1 (this change)

New: `UIRangeSlider`, `UIInputValidator`, `UIAspectRatio`, a shipped example scene
`examples/ui_library/ui_library_1.okayscene` (File > Examples > UI library), tests `ui_library`, `ui_library_example`,
`ui_library_shots`. See [ui_library.md](in_game_ui.md#ui-component-library).

Not done in slice 1 (kept honest): rich text links and icons, the FocusGraph wiring, the form group with Submit.

## World-Space UI (3D canvases)

*Was `docs/ui_world_space.md` until 2026-11-20.*

There are two ways to put UI in the 3D world:

### 1. World UI label (`WorldUI` component)
A single text label (+ optional background panel and a health/progress bar) that
floats over a 3D point — nameplates, damage numbers, quest markers, interaction
prompts. Add a **World Label (3D)** object, set its text, and it projects over its
object through the main camera (scaling with distance). It also previews in the
editor **Scene view** so you can place it while editing.

### 2. World-Space Canvas (full UI in 3D)
For a real in-world panel — a shop terminal, a sign, a floating menu — put a
**Canvas** in **World Space** and build it with the *regular* UI widgets. Every
widget type works: buttons, panels, images, sliders, toggles, steppers, progress
bars, dropdowns, text — exactly the same components you use for a HUD.

Setup:
1. Add a **Canvas**, tick **World Space** (Inspector → Canvas → World Space (3D)).
2. Position/rotate the Canvas object where you want the panel in the world.
3. Add UI widgets as children and lay them out against the canvas's
   **Design Res** (its own pixel space, default 1280×720).
4. **Pixels / Unit** sets the in-world size (larger = smaller panel). **Billboard**
   keeps it facing the camera; turn it off to use the object's orientation.

It renders (and is clickable) in the **Game view** and the built game, projected
through the scene's main camera. A main Camera must exist.

#### How it works
A world-space Canvas is projected by the shared UI layout helpers: the renderer
hands them a camera projector for the frame, widgets are laid out in design pixels,
and the canvas plane is projected to the screen (billboarded by default). Because
this rides the same `GetUIScreenRect` path every widget already uses, all widget
types come along automatically and screen-space UI is unaffected.

#### Standalone 3D widgets (`WorldSpaceUI` component)
Any single UI widget (button, text, panel, image…) becomes its own in-world 3D
object by adding the **World Space UI (3D)** component — no Canvas needed. It
renders at the object's Transform position (move it with the gizmo), keeps its
anchor and pixel offset, and its child widgets (e.g. a button's label) come along.
Create them from any **Create ▸ UI ▸ 3D UI (in-world)** menu.

- **Pixels / Unit** — design px per world unit (smaller = bigger in-world).
- **Billboard** — always face the camera.
- **Constant size** — keep a fixed on-screen size regardless of camera distance
  (it still moves in 3D, it just never shrinks or grows). **Size Scale** multiplies
  that fixed size (1.0 = the widget's authored design pixels). Great for markers,
  nameplates and waypoint icons that should stay readable at any range.

What is scaled with the projection: positions, text sizes, **panels** (size, corner radius, border, shadow, outline) and
**progress bars** (size, corner radius, segments), so a plate drawn at any distance keeps the proportions it was built with.
Text inside a world widget honours its own **Align** (left, centre, right) and **Vertical centre / bottom** inside its box, the
same as the same text on screen. (Before this, panels and bars were drawn at their design size while their places and the
text were scaled, so at any scale but 1:1 a child could stick out of its parent, and every text sat at the top-left of its box.)
Not checked and not changed: the other widget kinds (images, sliders, toggles, steppers, ratings, tabs, dropdowns, inputs, the
touch controls) draw their own sizes in world space as before.

Nameplates: **GameObject > UI > Prebuilt Screens > Nameplate (in the world)** makes one with **Constant size** on (it keeps its
220 x 62 design pixels at any distance, so the text stays readable far away) hanging above the object's point, centred on
it. Make it a child of a character, move it to the character's head, and it shows that character's name, level and health
(`owner.` keys, see [ui_runtime.md](in_game_ui.md#in-game-ui-looks-roles-widgets-and-the-api)). The sample scene `examples/ui_text` has two.

Notes / limits:
- Best results with **Billboard** on (or the panel roughly facing the camera);
  widgets are drawn as axis-aligned rects, so a steeply tilted panel in perspective
  is approximate.
- World-space canvases preview in the **Game view**; the editor **Scene view**
  shows world-UI *labels* but not full world canvases (use Play / Game view).

## HTML / CSS UI (UIDocument)

*Was `docs/ui_html.md` until 2026-11-20.*

A **UIDocument** can be authored two ways: the line-based **OkayUI** markup, or a
small **HTML + CSS** subset. Both build the *same* native widgets, so an HTML menu
renders identically in the editor, the player, and the web/mobile builds — there is
**no browser embedded**. Use whichever you prefer; the document auto-detects HTML
when its first non-space character is `<`.

### Quick example

```html
<style>
  .card   { background: #1e2433; border-radius: 8px; }
  button  { background: rgb(60,90,150); color: #ffffff; border-radius: 6px; }
  #title  { color: yellow; font-size: 5; }
</style>

<div class="card" style="left:40; top:40; width:360; height:260;">
  <h1 id="title" style="left:30; top:24;">MY GAME</h1>
  <button style="left:30; top:120; width:300; height:60;"
          onclick="load_scene('level1')">Play</button>
  <button style="left:30; top:190; width:300; height:60;"
          onclick="quit()">Quit</button>
</div>
```

### Tags → widgets

| HTML | Widget |
|------|--------|
| `<div>` `<panel>` `<section>` `<form>` `<ul>` `<nav>` | panel (container) |
| `<button>` `<a>` | button |
| `<p>` `<span>` `<h1>`–`<h6>` `<label>` `<li>` `<text>` | text |
| `<img src="...">` | image |
| `<input>` | input (or `type="range"` → slider, `type="checkbox"` → toggle) |
| `<select><option>…</select>` | dropdown |
| `<progress value="0.5">` | progress bar |

Only `<div>`-family elements nest children as widgets; text inside a `<button>`/`<p>`
becomes its label.

### CSS support

Selectors: **tag** (`button`), **class** (`.card`), **id** (`#title`), and inline
`style="..."` (highest priority). Supported properties map onto widget fields:

| CSS | Effect |
|-----|--------|
| `background` / `background-color` | fill color |
| `color` | text color |
| `width` / `height` | size (px or `%` of canvas) |
| `left` / `top` | position (px or `%`) |
| `border-radius` | rounded corners |
| `border` / `border-width`, `border-color` | outline |
| `font-size` | text/label size |
| `text-align` | left / center / right |

Colors accept `#rgb`, `#rrggbb`, `#rrggbbaa`, `rgb()/rgba()`, and a few names
(white, black, red, green, blue, yellow, gray, orange, transparent). Lengths accept
`12px` or `50%`.

### Behavior (the "JS")

`onclick`, `onchange`, `ontoggle`, `onsubmit` run **OkayScript** — the same engine
script API used everywhere (`load_scene`, `prefs_set`, `ui_set_text`, `set_active`,
`send_message`, calling your own functions, …). Because attributes are double-quoted,
use single quotes (or `&quot;`) for string arguments: `onclick="load_scene('game')"`.

Attributes: `id` names the widget (addressable by the `ui_*` script API and CSS
`#id`), `class` applies styles, `placeholder`, `value`, `min`/`max`, `checked`,
`src`, `tooltip`/`title` map to the matching widget fields.

### Notes / limits

- It's a pragmatic subset for game menus/HUDs, not a web browser — no flexbox/grid
  auto-layout (position with `left`/`top`/`width`/`height`), no media queries, no
  external CSS/JS files. For flow layout, nest under an OkayUI `layout` group.
- On the **web (WASM) build only**, if you need 100% real HTML/CSS/JS you can also
  overlay a normal DOM on top of the game canvas — the browser is already there.

## In-game UI quality

*Was `docs/in_game_ui_quality.md` until 2026-11-20.*

The owner's report: "In game ui still need perfecting, still kinda rough and pixelated."
This page says what looked rough (found by drawing a test screen and looking at it, then measuring), what was
already there, what is new, how to see the difference and what is not done. "In-game UI" means the UI that games draw
(buttons, panels, text, images, sliders, the UI library), as the player and the editor Game view show it. It does not
mean the editor's own panels.

Everything below was checked with **headless pictures and numbers from SDL's software renderer on Linux**. Nothing was
run in a real editor or player window, on Windows, on the OpenGL or Direct3D 11 drivers of SDL, or on a high-DPI screen.
Read "Untested" at the end.

### How the pictures were made

`ui_quality_shot out.png <width> <height> [old|fast|good|best] [x y w h zoom]` (target in `tests/`) draws the screen in
`tests/ui_quality_scene.hpp` (a 640x360 reference canvas, "Scale With Screen Size") through the real UI pass,
`RenderSceneUI`, on SDL's software renderer. It was drawn at 640x360 (1x), 960x540 (1.5x), 1280x720 (2x) and 1920x1080
(3x), in the Old look and in the new levels, and the corners, circles, text, shadows and pictures were magnified with
nearest-neighbour (so single pixels can be seen) and looked at. The pictures are not committed (they are written to a
temporary folder); the numbers are in `test_ui_quality`.

### Existing / Gap

| Area | Existing before this change | Gap seen in the pictures (Old look) | What changed (Good, the default) |
|---|---|---|---|
| Rounded corners, pills, circles, borders | Scan-line fills at the device resolution with one soft edge pixel (`FillUIShapeSmooth`, `FillUIRingSmooth`) | A pixel that was more than half covered was drawn fully solid and one under a fifth was dropped, so the "soft" edge was a staircase with a few stray pixels (button corners clearly stepped). The top and bottom of a circle were flat caps: only one sample per row, no vertical coverage. Mean coverage error on a 1.5x circle 0.027, worst 0.43; on a radius-12 corner 0.015 / 0.34 | Exact coverage per device pixel from 4 sub-scan-lines (Good) or 8 (Best), no rounding of edge pixels: circle 0.0012 / 0.066 (Best 0.0004 / 0.008), corner 0.0009 / 0.031 (Best 0.0003 / 0.008) |
| Shapes and borders at a fractional canvas scale | Drawn at `rect x scale`, so at 1.5x a 15 px card started half way into a device pixel and a 1 px border was 1.5 px wide | Uneven, blurred borders and edges at 1.25x, 1.5x, 1.75x | Rect edges and border widths snap to whole device pixels (`SnapToDevice`): a 1 px border at 1.5x is exactly 2 device pixels, nothing in between |
| Text: placement | Glyph quads at fractional positions, with the face baked at a whole pixel size | On a GPU driver a quad at a fractional position is resampled by the linear filter (soft stems); the whole-pixel bake size meant a 3 to 4 percent resample at fractional sizes; letters drifted ("Pl ay") | Faces baked at quarter-pixel sizes (one atlas pixel = one screen pixel, no resample), every glyph quad on a whole device pixel (Fast, Good) or on a quarter-pixel phase of a pre-rendered glyph (Best) |
| Text: weight | Plain exact-area coverage | Small text (9 to 14 px) looked thin | Stem darkening: coverage raised to a power that is 0.78 at 9 px and 1 at 24 px (`TtfCoverageGamma`): +13 to +16 percent ink on `l e o` at 10 px |
| Text: spacing | Advance widths only | No kerning: "AV", "To", "Wa" had holes | Pair kerning read from the font's `kern` table and OpenType `GPOS` kern feature (formats 1 and 2, extension lookups): AV -3.2 px, To -8.7 px at 100 px, unrelated pairs 0 |
| Soft shadows | Five stacked, hard-edged copies with whole-pixel growth | Visible rings (the card shadow looked like concentric steps); biggest step between neighbouring pixels 0.118 | One mesh of 10 outline rings with the alpha of a Gaussian-blurred edge (`0.5 erfc(d / (sigma sqrt 2))`), blended by the renderer: worst error against the Gaussian 0.026, biggest step 0.063, monotone |
| Gradients | One flat colour per row (8-bit steps) | Visible bands on slow gradients (40 to 46 over 60 rows = 7 flat strips) | Best: ordered (Bayer 4x4) dither per pixel on areas up to 30,000 device pixels; the row means follow the ramp within 0.14 level and 33 of 52 rows mix two levels. Good keeps flat rows |
| Pictures shrunk | Linear sampling at any size | A 128 px picture with fine detail shown at 36 px skipped texels (a 1 px checker flickers, mean error against the area average 36 levels) | A copy filtered to the drawn size (area average in linear light, `okay::tex` Mip.hpp, `UIDownscaleImage`) is drawn instead when the picture is shrunk by 25 percent or more: mean error 2.7 levels (Best at 128 to 61 px: 7.8, was 33) |
| Pixel-art pictures | One scene-wide switch (`uiFont "pixel"`) | n/a | Unchanged: the pixel look still samples nearest and draws hard edges (test: identical to the Old look) |
| Canvas scaling | Already drawn at physical resolution (`g_uiDeviceScale`, faces baked at logical size x scale) | n/a (this was already done by `claude/ui-crisp-1`) | Unchanged, plus the snapping above |
| Blending space | SDL / GPU blend in sRGB | Light text on dark looks a little heavier than dark text on light | Not changed (see "Not done") |
| Turned or scaled widgets (UI Frame) | Rectangle runs sent as geometry | Hard, stair-stepped outer edge (seen in the pictures) | Not changed (see "Not done") |

### The quality setting

Scene setting `uiQuality` (saved as a `uiquality N` line; a scene without the line is Good):

- **Fast (0)**: text snapped to whole device pixels; everything else as in the Old look.
- **Good (1, the default)**: Fast plus stem darkening, kerning, exact edge coverage (4 sub-scan-lines), pixel-snapped
  shapes and borders, soft shadow mesh, filtered picture downscale.
- **Best (2)**: Good plus quarter-pixel text phases, 8 sub-scan-lines, gradient dithering.
- **Old look (3)**: the drawing from before this change, for comparison. The golden-image test scenes
  (`tests/golden/ui`) are pinned to it and still match pixel for pixel.

Where to set it: the editor's **Lighting** window, **UI Font** header, **UI Quality** (saved with the scene, so the built
game matches). Without opening anything: the environment variable `OKAY_CLASSIC_UI=1` forces the Old look, and
`OKAY_UI_QUALITY=fast|good|best|old` forces a level, in the editor, the player and an exported game. The variable wins over
the scene. A project-wide default in Project Settings or Build Settings is not built (see "Not done").

### Where the code is

- `engine/src/UI/UIRenderer.cpp`: `EmitMulti`, `SnapToDevice`, `FillUIShadowSoft`, `PackCovDither`, the glyph placement in `DrawTtfText`, the picture downscale choice, `g_uiQuality`.
- `engine/include/okay/UI/UIFontPolicy.hpp`: `UIQuality`, `UIQualityFor` (environment names).
- `engine/include/okay/UI/UIImageScale.hpp`, `engine/src/UI/UIImageScale.cpp`: the size rules and the filtered copy; `Painter::LoadTextureSized` (SDL painter and `BatchPainter` implement it; other painters fall back to the full-size picture).
- `engine/src/Graphics/TtfFont.cpp`, `TtfRaster.cpp`: `ForDeviceSize`, `GlyphForPhase`, `TtfCoverageGamma`, `Face::Kerning`, `Face::Render` with a sub-pixel shift.
- `engine/include/okay/Scene/Scene.hpp` and `SceneSerializer.cpp`: the `uiQuality` setting.

Written from: the OpenType specification (`kern`, `GPOS` header, feature and lookup lists, coverage, class definitions, pair
adjustment formats 1 and 2), the definition of the complementary error function for the shadow profile, the Bayer ordered
dither matrix, and the box and Lanczos filter definitions already in `okay::tex`. No code from another project.

### Tests

`test_ui_quality` (180 checks, no window needed; `ui_quality_shot` makes the pictures): glyph weight, exact face sizes, sub-pixel phases (ink moves by k/4),
kerning pairs, picture size rules and filter against an area average in linear light, shadow profile against the
Gaussian, border snapping at 1.5x, circle and rounded-corner coverage against a 16x16 supersampled analytic shape,
glyph quads on the pixel grid, quarter-pixel text positions, gradient dither, the setting (saved, defaulted, environment
names), the batched path against the direct one, and a draw time per level. `test_ui_renderer_golden` (Old look pinned),
`test_ui_crisp`, `test_ui_render_bench` (draw calls, no allocation in steady state, time) still pass.

### Performance

Measured with `test_ui_render_bench` (500 widgets in a 10,000-object scene, 1920x1080, drawn through the batcher onto a
counting backend, so it is the engine's own UI cost with no GPU). This machine is busy, and only the UI files were
compiled with `-O2` (the rest of the engine with `-O0`), so the absolute times are far above the real optimised build
(target 1 ms, CI limit 3 ms) and are only good for comparing the levels with each other:

| Level | Median UI CPU per frame, before the speed-up | after | Triangles (before / after) |
|---|---|---|---|
| Old look | 4.24 ms | 4.26 ms | 20,554 |
| Fast | 4.27 ms | 4.33 ms | 20,554 |
| Good (default) | 6.88 ms (1.62 times the Old look; 3.8 vs 1.6 ms on CI) | 4.04 ms (0.95 times the Old look) | 64,886 / 53,594 |
| Best | 7.64 ms | 4.20 ms | 66,750 / 53,594 |

(Mixed build again: UI files `-O2`, the rest `-O0`; only the ratios mean anything.) The speed-up: flat shapes and borders are
recorded once per geometry (not colour) and replayed from a cache, the replay goes to the batcher in one call
(`Painter::FillRectsF`), edge alpha is in tenths and pixels under 3 or over 97 percent covered are dropped or merged into the
solid run. Cost: edge pixels can be 5 percent off (rounded corner error 0.0037 mean / 0.047 worst, was 0.0009 / 0.031). Best now
costs about the same as Good for shapes (its extras are the quarter-pixel text and the dithered gradients).

Draw calls stay at 2 and steady-state allocations stay at 0 (both asserted by the bench). Good sends about 2.6 times the
triangles (every partly covered edge pixel is its own small rectangle). The absolute numbers for the real `-O3` build and
for GPU drivers are **not measured** here.

The reference screen (`ui_quality_shot`, 1280x720, SDL software renderer, including renderer set-up and read-back) took
105 to 115 ms at every level: the time there is the software rasteriser, not the UI pass.

### Not done / untested

- **Untested**: Windows, a real editor or player window, SDL's OpenGL and Direct3D 11 drivers, the OpenGL and Direct3D 11
  3D renderers, high-DPI screens, the Xbox soft painter. The UI pass is one code path for all of them (it draws through
  `ui2d::Painter`), so they get the same geometry, but only SDL's software renderer was run. On that renderer the Old look's
  glyph positions were already cut to whole pixels by the renderer itself, so the sharper text mainly shows on the GPU
  drivers (unverified there); the quad-on-grid test proves where the quads are sent.
- Turned or scaled widgets (UI Frame) still have a hard stair-stepped outer edge: fixing it needs offscreen rendering or
  multi-sampling in the painter backends.
- Blending in linear light (gamma-correct) is not done: the renderers blend in sRGB. Stem darkening is a fixed
  curve, not dependent on the text colour.
- No glyph hinting (outlines are scaled exactly), no sub-pixel (LCD) text, no SDF text.
- Nine-slice panels still copy integer rects; at fractional scales a seam between pieces is possible (not seen in the pictures).
- Gradients bigger than 30,000 device pixels are not dithered; rectangle shapes with a gradient are dithered only in Best.
- "Blur behind" is not built.
- The quality setting is per scene. There is no project-wide default in Project Settings or Build Settings (use the
  environment variable, or set the scene).
- Kerning applies to the TrueType faces (the built-in Roboto and any .ttf/.otf the scene names); the 8x8 pixel font is unchanged.
- Performance: see the table above; Good is about 1.75 times the Old look in UI CPU and three times the triangles on the 500-widget screen; absolute optimised numbers are not measured.
- Kerning is applied when text is measured with the default level; a scene set to Old look but measured outside the draw pass (layout) can be a pixel or two off for kerned pairs.
