# 12. In-game UI

The widgets a player sees inside a game: HUDs, menus, dialogs, loading, pause and settings screens, and in-world labels. It is a retained set of
components (a Canvas, anchored widgets, layout containers) drawn by one engine renderer that the player (SDL) and the editor preview (ImGui) share.
Covers `engine/include/okay/UI/*`, `engine/src/UI/*`, about 50 `Components/UI*` headers, `Scene/UI*` and the editor's UI authoring helpers (about 14k engine lines, plus 0.7k lines of editor helper files and the UI code inside `editor/main.cpp`).
Area 40 (OkayUI) is a different, immediate-mode toolkit; read it first if the question is about `ui_begin`, the F1 test panel or the launcher look of script UI.

## 12. In-game UI

### 12.1 Canvas, anchors, flex layout, safe area
- **What:** Where every widget sits on screen: a Canvas scales a design size (1280x720) to the window, widgets are placed by anchor presets or a general UI Frame, and containers (layout group, flex, mask, safe area) arrange children.
- **Parts:**
  - 12.1.1 Canvas and scaling - `Components/Canvas` - Constant Pixel Size or Scale With Screen Size, Screen Match (Match Width/Height blend, Expand, Shrink), sort order, master opacity, world-space switch
  - 12.1.2 Anchor presets - `Components/UIAnchor` - nine fixed points plus three stretch presets, position is an offset (or margins for stretch)
  - 12.1.3 UI Frame - `Components/UIFrame` - anchorMin/anchorMax/pivot rect that replaces the preset; widget fields stay in step with it; "Use UI Frame" and "Convert all UI" convert scenes without moving anything
  - 12.1.4 Uniform widget rect and draw order - `Components/UIElement` - UIRect and GetUIRect for any widget, the one shared UI layer table, SortUIDrawItems and the draw-order rule
  - 12.1.5 Layout Group - `Components/UILayoutGroup` - column, row, grid, and Flex Row / Flex Column with wrap, justify and align
  - 12.1.6 Flex Item - `Components/UIFlexItem` - per-child grow, shrink and basis inside a flex layout
  - 12.1.7 Mask - `Components/UIMask` - clips descendants (and their clicks) to the widget rect, nests with scroll views
  - 12.1.8 Safe Area - `Components/UISafeArea` - keeps HUD clear of the notch and home bar; the host supplies the insets
  - 12.1.9 Per-frame layout pass - `UI/UILayoutUpdate` - settles frames, fits scroll extents, re-arranges flex, rebinds list rows; shared by player and editor
  - 12.1.10 Layout tools - `Scene/UILayoutTools` - wrap in panel, fit to children, match size, stack, avoid overlap (used by the editor)
- **Code:** `Components/Canvas`, `Components/UIAnchor`, `Components/UIFrame`, `Components/UIElement`, `Components/UILayoutGroup`, `Components/UIFlexItem`, `Components/UIMask`, `Components/UISafeArea`, `Components/UIWorldContext`, `UI/UILayoutUpdate`, `Scene/UILayoutTools`, `editor/UIInspectorLayout.*`
- **API:** `Canvas::ScaleFactor(w, h)`, `ResolveAnchorRect`, `GetUIRect`, `UpdateUILayouts`, `ConvertToUIFrame`; script: `ui_set_*` by widget name (see 12.13)
- **Editor:** Inspector cards for Canvas, Layout Group, Scroll View, UI Frame, UI Flex Item, UI Mask; UI view toolbar (Align, Match size, Stack, Wrap in Panel, Fit to Children)
- **Docs:** `docs/game_ui.md` (Styling and layout tools, Phones and tablets), `docs/engine_api.md`
- **Tests:** `ui_frame`, `ui_layout_containers`, `ui_system`, `ui_layer_table`, `ui_layering`
- **Status:** Works - the components are implemented and the tests above cover anchor math, frame conversion, flex, mask clipping and the layer table
- **Open:** possible gap: UIFrame has anchors and pivot but no rotation or scale (the UI plan asked for both); aspect-ratio and responsive containers from the component-library request are not present as components
- **Related:** 12.9, 12.10, 12.11, 12.6
- **Words:** canvas, CanvasScaler, anchors, anchors (UI), anchor preset, stretch, margins, UI Frame, UIFrame, pivot, flex, flexbox, layout group, grid layout, mask, safe area, notch, reference resolution, Screen Match, Expand, Shrink, sort order
- **Seen as:** a HUD is the wrong size on another resolution; a menu is cut off on a phone; a button drifts when the window resizes; children overlap in a row or column; a scroll list shows items outside its box; text sits under the notch
- **Ask me like:** "work on 12.1.3, convert old anchored widgets to UI Frames"; "the pause menu is cropped on a portrait phone"

### 12.2 Widgets: button, image, raw image, text, panel, progress, rating
- **What:** The basic building blocks of a screen: buttons, panels, text, pictures, progress bars, rating stars and the crosshair, each with shapes, borders, shadows and state colours.
- **Parts:**
  - 12.2.1 Button - `Components/UIButton` - hover/pressed/disabled colours, shape, shadow, calls the sibling script's on_click() or a native method
  - 12.2.2 Native button actions - `Components/NativeUIActions` - Button On Click to a component method (Target, Function, Arg), e.g. Drink on a survival stat
  - 12.2.3 Text - `Components/TextRenderer` - 8x8 bitmap or TTF text, world or screen space, box with alignment, localized key, shadow, typewriter reveal
  - 12.2.4 Panel - `Components/UIPanel` - background card with gradient, border, shadow and shape
  - 12.2.5 Shapes - `UI/UIShape` - rectangle, rounded, circle, pill and polygon silhouettes with row spans and hit tests
  - 12.2.6 Image - `Components/UIImage` - texture, tint, nine-slice, fill (cooldowns), preserve aspect, circle/rounded mask
  - 12.2.7 Raw Image - `Components/UIRawImage` - texture through a UV rect: tile, scroll, mirror
  - 12.2.8 Progress bar - `Components/UIProgressBar` - fill bar with segments, trail, number format
  - 12.2.9 Radial progress - `Components/UIRadialProgress` - ring or pie, cooldowns, spinner mode
  - 12.2.10 Rating - `Components/UIRating` - clickable star rating with half stars
  - 12.2.11 Crosshair - `Components/Crosshair` - screen-centre reticle with gap, dot and outline
- **Code:** `Components/UIButton`, `Components/NativeUIActions`, `Components/TextRenderer`, `Components/UIPanel`, `UI/UIShape`, `Components/UIImage`, `Components/UIRawImage`, `Components/UIProgressBar`, `Components/UIRadialProgress`, `Components/UIRating`, `Components/Crosshair`
- **API:** `UIButton`, `UIPanel`, `TextRenderer`, `UIImage`, `UIRawImage`, `UIProgressBar`, `UIRadialProgress`, `UIRating`, `Crosshair`; script: `ui_set_text`, `ui_get_text`, `ui_clicked`, `ui_set_progress`, `ui_set_fill`, `ui_set_interactable`
- **Editor:** GameObject > Create > UI (and the UI view's + Add menu); Inspector headers UI Button, UI Panel, UI Image, UI Raw Image, Text, UI Progress Bar, UI Radial Progress, UI Rating, Crosshair
- **Docs:** `docs/game_ui.md` (Images, Building blocks), `docs/engine_api.md`
- **Tests:** `ui` (tests/test_ui.cpp), `ui_extra`, `uiwidgets`, `uishape`, `text`, `minimap`, `ui_renderer_golden`
- **Status:** Works - implemented per the headers; tests cover hit testing, serialization of every style field, nine-slice, fill, shapes, text options
- **Open:** rich text tags (bold, italic, colour, size, icon) from the UI plan: none found in `TextRenderer`; no rating or progress "data source" beyond the Bar Bind (12.13)
- **Related:** 12.3, 12.7, 12.11, 2.9, 1.8
- **Words:** button, panel, label, text, TextRenderer, image, UIImage, raw image, nine-slice, 9-slice, progress bar, health bar, radial, cooldown, rating, stars, crosshair, reticle, shape, rounded corners, pill, shadow, gradient
- **Seen as:** a button does nothing on click; text is blurry or too small; an image shows a red box or "can't load"; a bar does not fill; corners are square when they should be round; text does not update when the language changes
- **Ask me like:** "work on 12.2.8, segmented progress bars"; "the health bar should follow the hp variable"

### 12.3 Input widgets: slider, toggle, input field, dropdown, stepper, keybind
- **What:** Controls the player changes: sliders, checkboxes and switches, text boxes, dropdown lists, numeric steppers, "press a key to bind" fields and a swatch colour picker.
- **Parts:**
  - 12.3.1 Slider - `Components/UISlider` - drag to a value in a range, whole-number mode, step, format, on_change()
  - 12.3.2 Toggle - `Components/UIToggle` - checkbox or sliding switch with label, on_toggle()
  - 12.3.3 Input field - `Components/UIInputField` - OS text input, caret, placeholder, number or password mode, character whitelist, multiline, on_submit()
  - 12.3.4 Dropdown - `Components/UIDropdown` - header plus open option list, swallows the click under the list
  - 12.3.5 Stepper - `Components/UIStepper` - [-] value [+] with step and range
  - 12.3.6 Keybind field - `Components/UIKeybindField` - listen for a key, Escape cancels, Backspace resets, saves to Prefs
  - 12.3.7 Colour picker - `Components/UIColorPicker` - swatch strip built from child buttons and a preview panel
- **Code:** `Components/UISlider`, `Components/UIToggle`, `Components/UIInputField`, `Components/UIDropdown`, `Components/UIStepper`, `Components/UIKeybindField`, `Components/UIColorPicker`, `editor/UIInspectorOverlay.*`
- **API:** `UISlider`, `UIToggle`, `UIInputField`, `UIDropdown`, `UIStepper`, `UIKeybindField`, `UIColorPicker`; script handlers `on_change`, `on_toggle`, `on_submit`, `on_keybind_change`; `ui_slider_value`, `ui_set_slider`, `ui_toggle_value`, `ui_dropdown_value`
- **Editor:** Inspector headers for each (UI Slider, UI Toggle, UI Input Field, UI Dropdown, UI Stepper, UI Color Picker); Create > UI > Controls
- **Docs:** `docs/game_ui.md`, `docs/engine_api.md`
- **Tests:** `ui` (tests/test_ui.cpp), `ui_extra`, `uiwidgets`, `ui_overlays`, `ui_input_guard`, `uishape`
- **Status:** Works - implemented and tested except the colour picker, which has no test of its own
- **Open:** from the component-library request, none found as components: radio group, search bar, date picker, time picker, file picker, virtual joystick or D-pad; double-click, long-press and swipe gestures are not in the UI components
- **Related:** 12.2, 12.4, 12.12, 8.3
- **Words:** slider, volume slider, checkbox, toggle, switch, input field, text box, password, dropdown, combo, option list, stepper, keybind, rebind button, colour picker, swatch
- **Seen as:** a slider will not drag or jumps; typing does nothing or opens game input; Enter does not submit; a dropdown list draws under other widgets or lets clicks through; the bind field does not remember its key
- **Ask me like:** "work on 12.3.3, add a multiline text area"; "add radio buttons"

### 12.4 Lists, tabs, modals, context menu, tooltips, scroll views
- **What:** Bigger interface pieces: scrolling regions, long virtual lists, tab bars, dialogs that block input, right-click menus, tooltips, toasts, slot grids and drag and drop.
- **Parts:**
  - 12.4.1 List View - `Components/UIListView` - virtualized rows from a template (10,000 rows cost like 10), items or script binding, rows never saved
  - 12.4.2 Tabs - `Components/UITabs` - five styles, per-tab icon/badge/enabled, scrolling overflow, prev/next keys, pages, script control
  - 12.4.3 Modal - `Components/UIModal` - dimmed backdrop, blocks other widgets and focus, stack, Escape or gamepad B closes, on_modal_close()
  - 12.4.4 Context menu - `Components/UIContextMenu` - right-click items, separators, onSelect and on_context_select()
  - 12.4.5 Tooltip - `Components/UITooltip`, `UI/TooltipPlace` - hover delay, keeps the box on screen (flips and clamps)
  - 12.4.6 Scroll view - `Components/UIScrollView` - vertical and horizontal, scrollbars, wheel, Shift+wheel, measured or fixed content
  - 12.4.7 Toast - `Components/UIToast` - host plus UIToastItem cards (info, success, warning, error) stacking with a slide
  - 12.4.8 Slot grid - `Components/UISlotGrid` - rows by columns of button slots with selection (no inventory logic)
  - 12.4.9 UI drag and drop - `Components/UIDraggable` - UIDraggable and UIDropTarget with on_drop() and on_receive(); the world-sprite twin is `Components/Draggable` (Draggable and DropZone)
  - 12.4.10 Inventory screen input - `engine/src/UI/InventoryInput.cpp` - hotbar and grid layout, hit testing and the drag state machine shared by player and editor (header is `Inventory/InventoryInput`, belongs to 11.1)
- **Code:** `Components/UIListView`, `Components/UITabs`, `Components/UIModal`, `Components/UIContextMenu`, `Components/UITooltip`, `UI/TooltipPlace`, `Components/UIScrollView`, `Components/UIToast`, `Components/UISlotGrid`, `Components/UIDraggable`, `Components/Draggable`, `engine/src/UI/InventoryInput.cpp`, `editor/UIInspectorOverlay.*`
- **API:** `UIListView`, `UITabs` (Select, Layout), `UIModal::Open/Close`, `UIContextMenu`, `UITooltip`, `PlaceTooltip`, `UIScrollView`, `UIToast`; script `ui_set_tab`, `ui_tab_value`, `ui_set_tab_badge`, `ui_drop_source`, `ui_drop_target`
- **Editor:** Inspector headers (UI Tabs with Generate Pages, UI Scroll View, UI List View, UI Modal, UI Context Menu, UI Tooltip, UI Draggable, UI Drop Target, UI Slot Grid); Create > UI > Controls and Layout
- **Docs:** `docs/game_ui.md` (Tabs section), `docs/engine_api.md`
- **Tests:** `ui_tabs`, `ui_overlays`, `ui_layout_containers`, `ui_extra`, `ui_nav_touch`, `ui_input_guard`, `item_dragdrop` (world drag and drop)
- **Status:** Works - modals, context menu, keybind, virtual list, horizontal scroll and the new tabs came with the UI plan P1 PRs (ROADMAP: #381, #386, #391); toast and slot grid have no tests of their own
- **Open:** the old row listed "2-D navigation (UI plan P0-P4)": navigation, touch and modals are on main (see 12.12), so only the later plan phases remain (12.7, 12.10, 12.13); missing from the library request: accordion, breadcrumbs, pagination, skeleton loader, bottom sheet/drawer, data table, tree view, carousel, radial menu, kill feed
- **Related:** 12.12, 12.5, 11.1, 12.7
- **Words:** list view, scroll view, scroll, tabs, tab bar, modal, dialog, popup, context menu, right-click menu, tooltip, toast, notification, slot grid, drag and drop, drop target
- **Seen as:** a long list is slow or drops rows; tabs do not switch pages or overflow; a dialog lets clicks through to widgets behind it; Escape does not close a modal; a tooltip runs off the screen or never shows; scrolling with a finger does not work
- **Ask me like:** "work on 12.4.1, bind a leaderboard list to an array"; "tooltips get cut off at the screen edge"

### 12.5 HTML UI
- **What:** A whole menu or HUD written as text and turned into real widget objects: either a line-based markup (called OkayUI markup here) or a small HTML plus CSS subset, with reusable styles and custom widgets.
- **Parts:**
  - 12.5.1 Line markup - `Components/UIDocument` - one widget per indented line, `style` classes, `define` custom widgets with parameters, data binding with `{key}`
  - 12.5.2 HTML and CSS mode - `Components/UIDocument` - tags to widgets, tag/class/id/inline selectors, a CSS property subset, onclick/onchange scripts (auto-detected when the text starts with `<`)
  - 12.5.3 Diagnostics and rebuild - `Components/UIDocument` - unknown types and keys reported with line numbers; Rebuild replaces generated widgets
- **Code:** `Components/UIDocument`, `editor/main.cpp` (UI Document inspector, ~34885-34936)
- **API:** `UIDocument::markup`, `UIDocument::Rebuild`; script handlers written inline as OkayScript
- **Editor:** Inspector header "UI Document" (markup editor and Rebuild)
- **Docs:** `docs/ui_html.md`
- **Tests:** `uidocument`
- **Status:** Works - parser and builder are in the engine and covered by `uidocument` (markup, CSS, defines, binding, round trip)
- **Open:** no flexbox or grid, no media queries, no external CSS/JS files (per `docs/ui_html.md`); possible naming clash: the markup is called "OkayUI markup" but builds scene widgets, not the OkayUI toolkit of area 40; the UI plan asked for a UIDocument-to-hierarchy round trip in the editor (not found)
- **Related:** 12.7, 12.13, 40.5
- **Words:** UIDocument, HTML UI, CSS, UXML, USS, markup, UI toolkit, define, style, onclick, template
- **Seen as:** a menu written as text looks different from the HTML; a CSS rule does nothing; a custom widget does not repeat; the document rebuilds and loses hand edits
- **Ask me like:** "work on 12.5.2, add flex layout to the HTML mode"

### 12.6 World-space UI
- **What:** UI that lives in the 3D world: floating labels and health bars over objects, a full Canvas drawn on a plane in the world, and single widgets placed as 3D objects.
- **Parts:**
  - 12.6.1 World label - `Components/WorldUI` - text with optional panel and bar anchored to a world point, scales with distance (nameplates, damage numbers, markers)
  - 12.6.2 World-space Canvas - `Components/Canvas`, `Components/UIWorldContext` - "World Space" switch, design resolution, pixels per unit, billboard; every widget projects through the camera
  - 12.6.3 Standalone 3D widget - `Components/WorldSpaceUI` - any widget becomes its own in-world object, optional constant on-screen size
- **Code:** `Components/WorldUI`, `Components/WorldSpaceUI`, `Components/UIWorldContext`, `Components/Canvas`
- **API:** `WorldUI`, `WorldSpaceUI`, `UIWorldCtx`, `GetUIScreenRect`
- **Editor:** Create > UI > 3D UI (in-world); Inspector headers World UI (3D label), World Space UI (3D), Canvas > World Space
- **Docs:** `docs/ui_world_space.md`
- **Tests:** `worldcanvas`, `worldtoscreen`, `ui_renderer_options`
- **Status:** Works - implemented; `worldcanvas` checks widgets projecting through the camera context
- **Open:** per `docs/ui_world_space.md`: steeply tilted panels draw as axis-aligned rects (approximate); the Scene view shows world labels but not full world canvases (use the Game view)
- **Related:** 12.1, 12.11, 4.8
- **Words:** world space UI, 3D UI, nameplate, health bar over head, floating text, damage numbers, billboard, world canvas, in-world menu, terminal
- **Seen as:** a nameplate does not follow its object; in-world text is mirrored or too big far away; a button in a world panel cannot be clicked; the panel does not show in the Scene view
- **Ask me like:** "work on 12.6.1, floating damage numbers"; "the in-world shop panel is not clickable"

### 12.7 Themes and the launcher-matching UI kit
- **What:** The Slate look shared with the launcher, one-click style presets for each widget, copy and paste of a widget's look, ready-made kit pieces and prebuilt screens (main menu, pause, settings, HUD, lobby, shop, loading), and a skin component.
- **Parts:**
  - 12.7.1 Slate palette and kit - `Scene/UITemplates` - Card, Heading, List Row, Badge, Avatar, Divider, button kinds, Switch, Spinner, Notification
  - 12.7.2 Prebuilt screens - `Scene/UITemplates` - Main Menu, Pause, Settings, Confirm Dialog, HUD, Health Bar, Dialog Box, Shop, Game Over, Lobby, Scoreboard, Leaderboard, Matchmaking, five loading screens; each lands on one shared Canvas
  - 12.7.3 Style presets and copy/paste style - `Scene/UIStyles` - per-component preset chips, Copy style / Paste style
  - 12.7.4 UI Theme component - `Components/UITheme` - four built-in looks (Dark, Light, Sci-fi, Fantasy) plus custom palette baked onto child widgets on request
  - 12.7.5 UI font - `Components/TextRenderer`, `editor/main.cpp` (Lighting window, UI Font) - launcher Roboto by default, scene-chosen TTF/OTF, or classic pixel font
  - 12.7.6 UI Theme window - `editor/main.cpp` (~42418-42667) - reusable styles applied across widgets, "Apply to All UI"
- **Code:** `Scene/UITemplates`, `Scene/UIStyles`, `Components/UITheme`, `editor/main.cpp` (`DrawUIThemer`, `DrawUIWidgetItems`)
- **API:** `UITemplates::AddMainMenu/AddPauseMenu/AddSettings/AddHUD/AddShop...`, `UIStyles::ApplyButton/ApplyPanel...`, `UITheme::Apply`; script `apply_ui_theme`, `set_ui_theme`, `ui_style("classic")`
- **Editor:** GameObject > Create > UI (Kit, Prebuilt Screens), View > Editors & Tools > UI Theme, "Look" chips at the top of each UI inspector, Save Selected as UI
- **Docs:** `docs/game_ui.md` (Adding UI in the editor, Theming)
- **Tests:** `ui_templates`, `ui_tabs`, `uicapture_look`
- **Status:** Partial - kit, screens and presets are implemented and tested; UITheme has no test and is a one-shot skin, not the stylesheet system the UI plan describes
- **Open:** "full component library" (see the missing list in 12.3 and 12.4); stylesheet assets with selectors, hover/pressed/focused/disabled states and transitions (UI plan P2); a real blur behind panels is not possible with the painter (per the `UITheme` header)
- **Related:** 12.2, 12.8, 12.10, 40.2
- **Words:** theme, skin, Slate, launcher look, UI kit, template, prebuilt screen, main menu, HUD, preset, style, copy style, Look chips, UITheme, UI font, dark mode
- **Seen as:** a new widget does not match the launcher colours; a prebuilt screen overlaps another; "Apply to All UI" changes too much; the font looks pixelated or wrong; centred menus draw on top of each other
- **Ask me like:** "work on 12.7.4, stylesheet assets with hover states"; "add a Settings screen to this scene"

### 12.8 Loading, pause and settings screens
- **What:** Drop-in screens: a loading overlay with seven layouts, a pause menu (Resume, Restart, Main Menu, Quit, volume sliders) and a settings screen bound to audio, window and render quality.
- **Parts:**
  - 12.8.1 Loading screen - `Components/LoadingScreen` - show-on-start or scene transition, progress, fade, rotating tips, bar and spinner styles, logo, slow zoom
  - 12.8.2 Loading layouts - `Components/LoadingScreenLayout` - one draw-command list (rect, gradient, image, text, arc) drawn identically by the player and the editor preview
  - 12.8.3 Pause menu - `Components/PauseMenu` - Escape toggle, freezes time, free cursor, builds editable UI objects in the scene, keyboard and gamepad
  - 12.8.4 Settings screen - `Components/SettingsScreen` - Music, Effects, Monitor, Display mode, Resolution, Refresh rate, VSync, Quality; Apply then "Keep these settings?" for 10 seconds; Text size, Colour filter, High contrast, Reduce motion, Reduce flashing, Subtitles, Sound captions, Hold to toggle and Aim assist bound to `Accessibility` (applied live, saved to Prefs on Done); Done returns to the pause menu
- **Code:** `Components/LoadingScreen`, `Components/LoadingScreenLayout`, `Components/PauseMenu`, `Components/SettingsScreen`, `editor/main.cpp` (Loading Screen and Pause Menu inspectors ~31719, ~29517)
- **API:** `LoadingScreen::Begin("Scene")`, `PauseMenu`, `SettingsScreen::AnyOpen`, `GameInputBlocked`
- **Editor:** Inspector headers Loading Screen (live animated preview, Look buttons) and Pause Menu; Create > UI > Prebuilt Screens > Menus and Loading Screens; Localized switch (Localization window)
- **Docs:** `docs/game_ui.md` (Loading screens)
- **Tests:** `loading_screen`, `pausemenu`, `pause_menu`, `ui_input_guard`, `ui_templates`, `accessibility_wired`
- **Status:** Works - implemented and tested (headless loading logic, pause freeze and quit flag, pause card at three aspects)
- **Open:** no known gaps; save-slot UI components (area 35) are not here
- **Related:** 12.7, 7.4, 1.8, 35
- **Words:** loading screen, splash, pause menu, settings screen, options menu, resume, quit, tips, progress bar, main menu
- **Seen as:** the pause menu does not freeze the game or free the mouse; the settings sliders do nothing; the loading screen never ends or flashes; the pause card is cut off on a tall screen
- **Ask me like:** "work on 12.8.1, add a video background to the loading screen"; "Escape does not open the pause menu"

### 12.9 Mobile scaling
- **What:** Making UI fit phones and tablets: screen-shape matching, safe area, touch scrolling and the Game view's device emulator.
- **Parts:**
  - 12.9.1 Screen match modes - `Components/Canvas` - Expand keeps a landscape layout whole on a portrait phone
  - 12.9.2 Safe area insets - `Components/UISafeArea` - the host sets `UISafeInsets()` (the editor from the emulated device, a mobile build from the OS); the per-frame layout pass in `UI/UILayoutUpdate` is shared by player and editor
  - 12.9.3 Touch scrolling - `Components/UITouchScroll` - finger drag and fling in scroll and list views
  - 12.9.4 Device emulator - `editor/main.cpp` (Mobile emulator ~41427-41507, `DrawDeviceBody`, `DrawDeviceScreenOverlay`) - phone and tablet presets with notch, corners, home bar; Rotate and Safe-area tint
- **Code:** `Components/Canvas`, `Components/UISafeArea`, `Components/UITouchScroll`, `UI/UILayoutUpdate`, `editor/main.cpp`
- **API:** `UISafeInsets()`, `Canvas::screenMatch`, `UITouchScroll::Step`
- **Editor:** Game view resolution/device menu, Portrait/Landscape, Safe area toggle; Canvas "Add Safe Area"
- **Docs:** `docs/game_ui.md` (Phones and tablets), `docs/web_mobile.md`
- **Tests:** `ui_nav_touch`, `ui_frame`, `ui_input_guard`
- **Status:** Works - match modes, safe area and touch scroll are implemented; touch tap and fling are covered by `ui_nav_touch`
- **Open:** no on-screen controls (virtual joystick, D-pad, touch buttons) found; no test for `UITouchScroll` by name
- **Related:** 12.1, 8.4, 18.2
- **Words:** mobile, phone, tablet, safe area, notch, portrait, landscape, touch, emulator, device frame, resolution
- **Seen as:** UI is tiny or cropped on a phone; buttons sit under the notch; finger scrolling does not work; the device preview shows the wrong insets
- **Ask me like:** "work on 12.9.4, add more phone presets"; "add a virtual joystick for mobile"

### 12.10 UI editing in the editor
- **What:** How designers build UI inside the editor: a UI-only Scene view mode and a dockable UI tab, click-drag handles, alignment tools, UI inspectors and a preview that draws through the same renderer as the game.
- **Parts:**
  - 12.10.1 UI view and tab - `editor/main.cpp` (`DrawViewport`, ~41508-42171) - "UI" toolbar button, View > UI Editing Mode and UI Editor (separate tab), frame against the Game view resolution
  - 12.10.2 UI overlay and handles - `editor/main.cpp` (`DrawUIOverlay` ~37449-38642) - clickable and draggable widgets, resize handles, smart guides, grid and Subdivide snap, off-screen outline
  - 12.10.3 Align and distribute - `editor/main.cpp` (~38073-38200), `Scene/UILayoutTools` - Align, Distribute, Match size, Stack, Wrap in Panel, Fit to Children
  - 12.10.4 Preview painter - `editor/UIPreviewPainter.*` - ui2d::Painter over an ImGui draw list, so the preview equals the built game
  - 12.10.5 Layout inspectors - `editor/UIInspectorLayout.*` - flex fields, scroll axis, UI Frame, Flex Item, Mask, List View, "Use UI Frame" and "Convert all UI"
  - 12.10.6 Overlay-widget inspectors - `editor/UIInspectorOverlay.*` - UI Modal, UI Context Menu, UI Keybind Field and their Create > UI builders
  - 12.10.7 Create > UI menus - `editor/main.cpp` (`DrawUIWidgetItems` ~5389-5550, menu ~6092-6248) - single widgets, kit, prebuilt screens, 3D UI
  - 12.10.8 Widget inspectors - `editor/main.cpp` (~34698-36498) - Canvas, Event System, UI Button, Panel, Input Field, Dropdown, Slider, Tabs and the rest
- **Code:** `editor/UIPreviewPainter.*`, `editor/UIInspectorLayout.*`, `editor/UIInspectorOverlay.*`, `editor/main.cpp`, `Scene/UILayoutTools`, `Scene/UIStyles`
- **API:** menu items GameObject > Create > UI, View > Editors & Tools > UI Editing Mode, UI Editor (separate tab), UI Theme
- **Editor:** Scene view (UI button, "..." menu toggle for the overlay), UI tab, Inspector
- **Docs:** `docs/editor.md` (UI editing), `docs/game_ui.md`
- **Tests:** `ui_frame` (undo through `editor/EditorState.cpp`), `ui_renderer_options`, `editor` (selftest)
- **Status:** Partial - the UI view, handles, tools and inspectors exist; items the UI plan P4 asked for are not all there
- **Open:** UI plan P4: anchor preset picker and pivot handles, UI prefabs with variants, UIDocument-to-hierarchy round trip, a `--selftest` that creates and saves a UI prefab (none found); the editor's Scene view shows no full world-space canvases
- **Related:** 15.3, 15.2, 12.1, 12.7, 12.11
- **Words:** UI editing mode, UI tab, UI editor, UI overlay, handles, smart guides, snap, subdivide, align, distribute, Create > UI, UIPreviewPainter, UI inspector
- **Seen as:** a widget cannot be dragged or resized in the UI view; the preview differs from the game; the UI is invisible in the Scene view; alignment tools are greyed out; undo of a UI move misbehaves
- **Ask me like:** "work on 12.10.2, anchor handles in the UI view"; "the editor UI preview does not match Play"

### 12.11 UI renderer, painters and batching
- **What:** The single sorted UI pass that draws every screen-space widget, the small painter interface it draws through, and a batcher that turns draw calls into a few GPU geometry calls.
- **Parts:**
  - 12.11.1 Scene UI pass - `UI/UIRenderer` - `RenderSceneUI(scene, painter, w, h, options)` sorts by canvas, draw order and hierarchy; options for edit-time resting state and world labels
  - 12.11.2 Painter interface - `UI/UIPainter` - SDL-free drawing surface (rects, lines, textures, geometry, clip, colour mods)
  - 12.11.3 SDL painter - `UI/UIPainterSDL` - straight pass-through to SDL_Renderer with texture cache
  - 12.11.4 Batch painter - `UI/UIBatcher` - one vertex buffer and atlas, few draw calls (target: 500 widgets under 1 ms and 20 calls)
  - 12.11.5 Drawing helpers - `UI/UIRenderer` - FillUIShape, rings, shadows, TTF text, minimap raster helpers shared with inventory and loading overlays
  - 12.11.6 Golden scenes - `tests/ui_golden_scenes.hpp` - scenes drawn and compared with `tests/golden/ui/*.png`
- **Code:** `UI/UIRenderer`, `UI/UIPainter`, `UI/UIPainterSDL`, `UI/UIBatcher`, `UI/UIShape`, `tests/ui_golden_scenes.hpp`, `editor/UIPreviewPainter.*`
- **API:** `okay::ui2d::RenderSceneUI`, `ui2d::Painter`, `ui2d::SDLPainter`, `ui2d::BatchPainter::Flush`, `UIRenderOptions`
- **Editor:** Game view and Scene UI preview draw through it (via `UIPreviewPainter`)
- **Docs:** -
- **Tests:** `ui_renderer_golden`, `ui_render_bench`, `ui_renderer_options`, `ui_layering`, `ui_layer_table`, `uishape`
- **Status:** Works - UI plan P0 (shared layer table, renderer moved out of the player, batching) is on main per ROADMAP #361; benchmark and golden tests exist
- **Open:** the player (`player/main.cpp`) still draws other overlays itself (inventory, crafting, F1 panel); no GPU blur; `UIPainterSDL` is header-only so only SDL hosts link it
- **Related:** 12.10, 12.2, 2.9, 40.3
- **Words:** UI renderer, RenderSceneUI, painter, batcher, draw calls, atlas, golden image, layer table, UI performance, SDL painter
- **Seen as:** UI is slow with many widgets; a widget draws in the wrong order; UI looks different in editor and player; text is not drawn; a golden image test fails
- **Ask me like:** "work on 12.11.4, UI draw call count"; "the editor preview and the player draw the pause menu differently"

### 12.12 UI input, focus and navigation
- **What:** How the pointer, keyboard, gamepad and fingers reach widgets: top-most hit testing, hover and selection, 2-D focus movement, modal input blocking and the "game input blocked" guard.
- **Parts:**
  - 12.12.1 Event System - `Components/EventSystem` - UIRaycast, Hovered, Pressed, Selected, IsPointerOverUI
  - 12.12.2 Navigation - `Components/UINavigation` - nearest widget in the pressed direction, wrap, value widgets keep Left/Right, focus scrolls into view
  - 12.12.3 Touch input - `Components/UITouchScroll` - finger drag, fling, tap fires on lift
  - 12.12.4 Input guards - `Components/UIModal`, `Components/SettingsScreen` - modals and open screens block game input (Input::UICaptured, GameInputBlocked)
  - 12.12.5 Hit testing contract - `Components/UIWorldContext`, `Components/UIElement` - one widget rect used for draw, click and drag in screen and world space
  - 12.12.6 Focus graph - `UI/FocusGraph` - (#574) directional focus with tab order, explicit links, wrap and focus traps; `BuildFocusGraph(scene, graph)` fills it from the visible, focusable widgets; not used by `NavigateUI` (32.1)
- **Code:** `Components/EventSystem`, `Components/UINavigation`, `Components/UITouchScroll`, `Components/UIModal`, `Components/SettingsScreen`, `Components/UIWorldContext`, `UI/FocusGraph`
- **API:** `NavigateUI`, `UIRaycast`, `SceneEventSystem`, `EventSystem::IsPointerOverUI`, `Input::UICaptured`
- **Editor:** Event System inspector header; the UI view uses the same rects for click-to-select
- **Docs:** -
- **Tests:** `ui_nav_touch`, `ui_overlays`, `ui_input_guard`, `uicapture_look`, `ui_system`, `focus_remap`
- **Status:** Works - navigation, touch and focus rules are implemented and tested; a focus graph with tab order, links and traps (`UI/FocusGraph`, `BuildFocusGraph` in `UINavigation.hpp`) merged in #574 but `NavigateUI` does not use it
- **Open:** FocusGraph (#574, 32.1) is not used by `NavigateUI` or the widgets yet (the accessibility text size, colour filter and high contrast are applied by the UI renderer, 32.3); remapping all UI actions and screen-reader hints belong to area 32; no gesture recognisers (double-click, long press, swipe)
- **Related:** 8.3, 8.4, 32.1, 12.4
- **Words:** event system, hover, pressed, selected, gamepad navigation, focus, D-pad, keyboard menu navigation, touch, tap, pointer over UI, input capture, UICaptured
- **Seen as:** gamepad cannot move through a menu; focus ring stays on a hidden widget; clicks on the HUD also fire in the world; the camera keeps turning while a menu is open; the wrong widget gets the click
- **Ask me like:** "work on 12.12.2, explicit neighbour links for navigation"; "my character shoots when I click a button"

### 12.13 Data binding, UI animation and script control
- **What:** Making UI react without code or from scripts: bars and labels that follow variables, intro/idle/outro animations, and script calls that read and change widgets by name.
- **Parts:**
  - 12.13.1 Bar bind - `Components/UIBarBind` - progress bar or radial fill follows a visual-script variable or a Prefs value
  - 12.13.2 Text bind - `Components/UITextBind` - `format` with `{key}` placeholders filled each frame, `{score|0}` defaults
  - 12.13.3 UI Animation - `Components/UIAnimation` - fade, slide, pop, drop intros, pulse/bob/sway/blink/glow loops, outro that can hide the object
  - 12.13.4 Script control by name - `Scripting/OkayScriptVM` - `ui_set_text`, `ui_set_progress`, `ui_show`, `ui_hide`, `ui_play`, `ui_clicked`, drop helpers, `tween_ui_move`, `tween_ui_size`
  - 12.13.5 Typed scripts and graph nodes - `Script/EngineApi` - UI component members reachable from the typed language and from Engine API nodes; the visual node Show/Hide UI
- **Code:** `Components/UIBarBind`, `Components/UITextBind`, `Components/UIAnimation`, `Scripting/OkayScriptVM`, `Script/EngineApi`
- **API:** `UIBarBind`, `UITextBind`, `UIAnimation::Play/PlayOut/Stop`, script `ui_*`, `tween_ui_*`
- **Editor:** Inspector headers UI Text Bind, UI Bar Bind, UI Animation; the prebuilt HUD wires hp, score and loading variables
- **Docs:** `docs/game_ui.md`, `docs/scripting_reference.md` (UI section), `docs/engine_api.md`
- **Tests:** `ui_animation`, `ui` (bar bind, text bind), `uidocument` (binding), `ui_extra` (UI script API, tweens)
- **Status:** Partial - bindings, animation and name-based script calls work; the generic script API from the UI plan (P3) is not there
- **Open:** UI plan P3 not found: `ui_create`, `ui_destroy`, `ui_find`, generic `ui_get/ui_set(prop)`, events `on_hover_enter`, `on_hover_exit`, `on_value_changed`, two-way binding to SyncVars, list/array binding, visual nodes On Value Changed, Set Progress, Focus, Create Widget; a settings menu built purely from nodes
- **Related:** 14.4, 13.7, 1.2, 12.4
- **Words:** data binding, bind, UIBarBind, UITextBind, UIAnimation, fade in, slide in, pulse, ui_set_text, ui_show, HUD variable, score label, health bar bind
- **Seen as:** a score label stays at its first value; a health bar does not follow hp; an intro animation snaps; a script cannot find a widget by name
- **Ask me like:** "work on 12.13.4, add ui_create and ui_destroy"; "bind a leaderboard to a player-score array"
