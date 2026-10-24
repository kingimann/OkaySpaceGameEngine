# Game UI that looks like the launcher

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

Prefer the old pixel look? Call `ui_style("classic")` in a script, or start the
game with `OKAY_CLASSIC_UI=1`.

## Adding UI in the editor

The **UI** button on the Scene toolbar switches to the UI editing view. Its
**+ Add** button (and **GameObject → Create → UI**) lists everything you can add:

| Menu | What's in it |
| --- | --- |
| **Basic** / **Controls** / **HUD** / **Layout** | Single widgets: button, panel, image, raw image, text, slider, toggle, stepper, rating, tabs, input field, dropdown, progress bar, radial, minimap, crosshair, tooltip, scroll view, layout group, drag and drop. |
| **Kit (launcher look)** | Ready-styled building blocks: Card, Heading, List Row, Badge, Avatar, Divider, Primary / Secondary / Danger / Ghost buttons, Switch, Spinner and Notification. |
| **Prebuilt Screens** | Whole screens. **Menus:** Main Menu, Pause Menu, Settings, Confirm Dialog, Loading Screens (see below). **In game:** HUD, Health Bar, Dialog Box, Notification, Shop, Game Over. **Multiplayer:** Lobby, Scoreboard, Leaderboard, Matchmaking. |

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

### Styling and layout tools

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

## Phones and tablets

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

## Images

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

## Loading screens

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

## Building blocks

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

Press **F1** in a built game to see the Test UI panel and a sample **Lobby**
built from these blocks (`shared/OkayTestPanel.hpp` has the code).

## Theming

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
