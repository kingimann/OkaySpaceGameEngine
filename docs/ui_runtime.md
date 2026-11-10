# In-game UI: looks, roles, widgets and the API

This page covers the runtime UI added on top of [game_ui.md](game_ui.md): twelve looks you can
switch with one click, widget roles (Primary, Danger, Ghost...), radio buttons, draggable windows,
floating damage numbers, a dialogue box, the UI Gallery, and the script and C++ calls for all of it.

## What changed (plain words)

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

## The twelve looks

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
| Glass | see-through cards with a bright edge and a soft shadow, so the picture behind shows through. It does **not** blur what is behind it (the UI draw path cannot read the screen back), and it is not called frosted for that reason |
| Military | olive and khaki, square, gold accent |
| Horror | near-black, dried-blood red, dark red glow |

A theme holds colours (panel, accent, text, muted, button and its hover, pressed and disabled states,
danger, success, border, shadow, glow), corner radius, border width, gradient, shadow, glow, opacity,
font scale, transition speed, hover grow and an optional font file. **Apply** writes them onto the
widgets; nothing moves.

- **Per widget:** put a UI Theme on a panel and Apply: only that panel's widgets change.
- **Global:** tick **Style the whole scene** on the theme (the UI Theme window does this on the Canvas).
- **Roles:** add **UI Style Variant**; the **Role** list picks the variant.

## Widgets

| Widget | Where | Notes |
| --- | --- | --- |
| Radio buttons | UI Toggle with Style = Radio | Same **Group** name = one choice. Empty group = the radio buttons under one parent. Picking one turns the others off; clicking the picked one keeps it. |
| Window | GameObject > UI > Prebuilt Screens > Window | A title bar with **Drag the parent** and **Keep on screen** on a UI Draggable, and an x button that hides it (On Click: `HideObject`). The window carries its own Canvas one step above the page, so it and its contents draw above the page and take clicks first; dragging a window brings it in front of other windows. |
| Damage Numbers | UI Damage Numbers | `Spawn(text, kind, pos)` (canvas pixels) or `SpawnAtWorld(text, kind, point)`; the numbers are made under the scene's Canvas. Buttons can call `Hit`, `Crit`, `Heal`, `Miss`, `Info`. Numbers drift by a fixed pattern, so a replay looks the same. |
| Dialogue Box | UI Dialogue Box | Children named `Box`, `Speaker`, `Body`, `Choice1-3`, `Next`. Typing speed, skip on click or Space, up to four choices. |
| Nameplate | Prebuilt Screens > Nameplate | A plate with name, level and health bar. The "in the world" version follows a 3D object and faces the camera. |
| Inventory | Prebuilt Screens > Inventory | A bag screen with a slot grid you can click. A **UI Inventory Bind** on the grid shows the real Inventory: the Player's, else the one named in **Inventory Name**, else the first in the scene. Each slot shows its item and stack size ("Potion 7"), empty places stay blank, and the grid gets as many rows as the bag has room for. `SelectedItem()` and `SelectedCount()` say what is in the clicked slot. With no Inventory in the scene the grid keeps its sample names. It shows the bag; moving items between bags is done by the Inventory itself. |
| Rich text | Text > **Rich Text** | See below. |

Already there and unchanged: buttons (hover, press, focus states), toggles and switches, slider, stepper,
rating, progress bars (linear, segmented, trail, gradient), radial progress and spinner, dropdown, input
field, scroll view with masking and scroll bars, list view, tabs, tooltip, modal, toast, context menu,
crosshair, minimap, slot grid, layout groups, UI Frame (anchors, pivot), safe area and the Canvas scaler
(Constant Pixel Size, Scale With Screen Size with Expand, Shrink or Match).

**Rich text.** Turn on **Rich Text** on a screen-space Text (or `ui_set_rich("Label", true)`) and write tags
in the text: `[color=#ffcc33]gold[/color]`, `[color=red]red[/color]`, `[b]bold[/b]`, `[i]italic[/i]`. Colours are
`#rgb`, `#rrggbb`, `#rrggbbaa` or a name (white, black, red, green, blue, yellow, orange, gold, cyan, magenta,
purple, pink, gray). Tags nest. A tag that is not one of these is shown as plain text, and `[[` shows a `[`.
The tags take no room: wrapping, alignment, upper-casing and the typing effect all work on the plain text.
Bold doubles the glyphs (every font); italic leans the built-in pixel font only, a font file ignores it. World
(3D) text does not read tags. The tooltip's own `<b>` and `#rrggbb` tags are unchanged.

**Readable in every look.** The themes promise a contrast of at least 3 to 1 between a button's label and
what it sits on, for all 12 looks and all 7 roles. A Ghost or Outline button uses the accent for its label when
that reads, else the look's text colour (so Fantasy's gold on parchment becomes dark brown). `UITheme::Contrast`
gives the ratio; a test checks every look and role.

## Script calls (okayscript)

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

## C++ calls (what visual-script nodes call)

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
write properties by name already reach every setting. See [properties.md](properties.md).

## The UI Gallery

`UIGallery::BuildGallery(scene, preset)` (in `okay/Scene/UIGallery.hpp`) builds the gallery into any
scene. File > Examples > UI Gallery does that in a fresh 2D scene. The same header has
`AddInventoryScreen`, `AddDialogueScreen`, `AddWindow`, `AddNameplate` and `ApplyThemePreset`.
