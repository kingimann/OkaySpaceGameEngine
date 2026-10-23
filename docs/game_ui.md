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
| **Basic** / **Controls** / **HUD** / **Layout** | Single widgets: button, panel, image, text, slider, toggle, stepper, rating, tabs, input field, dropdown, progress bar, radial, minimap, crosshair, tooltip, scroll view, layout group, drag and drop. |
| **Kit (launcher look)** | Ready-styled building blocks: Card, Heading, List Row, Badge, Avatar, Divider, Primary / Secondary / Danger / Ghost buttons, Switch, Spinner and Notification. |
| **Prebuilt Screens** | Whole screens. **Menus:** Main Menu, Pause Menu, Settings, Confirm Dialog, Loading Screen. **In game:** HUD, Health Bar, Dialog Box, Notification, Shop, Game Over. **Multiplayer:** Lobby, Scoreboard, Leaderboard, Matchmaking. |

Select a panel or card first and new widgets and kit pieces go **inside** it.
Otherwise they go on the scene's Canvas, which is created if it's missing.
Everything you add is ordinary objects, so you can drag, resize, restyle or
delete each part. To reuse a screen you've customised, use **Save Selected as
UI**.

Screens lay out against a 1280×720 canvas that **scales with the screen**, so
they keep their layout at any window size. The HUD's health bar and number
follow the `hp` variable, and the score follows `score`. The Loading Screen's
bar follows `loading` (0 to 1).

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
