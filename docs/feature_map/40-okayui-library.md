# 40. OkayUI standalone library

OkayUI is a small immediate-mode UI toolkit (about 3.7k lines in `okayui/`): you call `OkayUI::Button(...)` every frame and it draws and returns the click. It draws through SDL_Renderer (or a raw Direct3D 11 backend on Windows) on top of the game or of Dear ImGui.
It is what script `ui_*` calls, the F1 Test UI panel, the "Find a server" and "Matchmaking" windows and the dialogue box and quest tracker use. It is NOT the scene UI of area 12 (Canvas, `UIButton` and friends): that is a separate retained system with its own renderer.
This area stands alone (own CMake target `okay_ui`, own backends, own font atlas, own test), so it is not folded into area 12.

## 40. OkayUI standalone library

### 40.1 Core widgets and auto-layout
- **What:** The widget set: explicit-coordinate widgets (button, checkbox, switch, slider, progress, radio, tab, text field) and an ImGui-style auto-layout API (windows, scrolling children, same-line, tables, menus, popups, trees, plots, drag and drop, colour pickers).
- **Parts:**
  - 40.1.1 Explicit-coordinate widgets - `okayui/include/okay/UI/OkayUI.hpp`, `okayui/src/OkayUI.cpp` - `Button(id, x, y, w, h, label)`, `Checkbox`, `ToggleSwitch`, `Slider`, `ProgressBar`, `RadioButton`, `Tab`, `TextField`, `Label`, `Panel`
  - 40.1.2 Windows and layout - `okayui/src/OkayUI.cpp` - `Begin/End` draggable collapsible windows, `PinNextWindow`, `BeginChild` scroll regions, `SameLine`, `Indent`, `Separator`, `Spacing`, item width, clip rects, `PushID`
  - 40.1.3 Value widgets - `okayui/src/OkayUI.cpp` - `SliderFloat/SliderInt`, `VSliderFloat`, `Knob`, `DragFloat/DragInt`, `InputText`, `InputInt`, `Combo`, `ListBox`, `ColorEdit3`, `ColorPicker3`
  - 40.1.4 Structure widgets - `okayui/src/OkayUI.cpp` - `CollapsingHeader`, `TreeNode`, `Columns`, `BeginTable` (resizable columns), menu bar, popups and context popups, `Selectable`, `Tooltip`
  - 40.1.5 Data display - `okayui/src/OkayUI.cpp` - `PlotLines`, `PlotHistogram`, `LabelText`, `Image`, `ImageButton`, bullets, wrapped and coloured text
  - 40.1.6 Drag and drop - `okayui/src/OkayUI.cpp` - `DragSource(payload)`, `DropTarget(&payload)`, `IsDragging`
- **Code:** `okayui/include/okay/UI/OkayUI.hpp`, `okayui/src/OkayUI.cpp`
- **API:** namespace `OkayUI`: `BeginFrame(Input)`, widgets above, `Style()`, `Input` struct (mouse, buttons, typed text, backspace, wheel, `blocked`)
- **Editor:** View > Rendering & Performance > Test UI draws the sample panel over the editor; Game view overlays
- **Docs:** `docs/game_ui.md` (Building blocks, Theming), `docs/scripting_reference.md` (OkayUI section)
- **Tests:** `okayui` (software renderer, reads pixels back: press and release, hover, the ImGui-capture gate)
- **Status:** Works - implemented (2.7k-line .cpp) and used by the player and editor; one headless test covers rendering and click logic
- **Open:** only one test file, and it mostly covers `Button`; text field and the layout widgets have no test of their own; stale header comment says "v1 scope: OkayUI::Button" while the file has about 70 widgets; header says "no STL" but the .cpp includes `<algorithm>`
- **Related:** 40.2, 40.4, 12.4
- **Words:** OkayUI, immediate mode, IMGUI, Button, Checkbox, Slider, TextField, Begin End window, SameLine, table, combo, tree node, popup
- **Seen as:** a button does not click in the player; a window cannot be dragged; text typed into a field goes to the game; a table column is the wrong width; a Combo list draws under other widgets
- **Ask me like:** "work on 40.1.4, add keyboard navigation to OkayUI"; "OkayUI InputText loses focus on click"

### 40.2 Launcher look, theme and fonts
- **What:** The theme struct and the "launcher look": Roboto TrueType text baked at several sizes, the Slate colours, rounded controls, pill tabs and shadows, plus a classic 8x8 pixel look and launcher-style building blocks (heading, caption, card, badge, avatar, list item, key-value, spinner, toast).
- **Parts:**
  - 40.2.1 Theme and Slate palette - `okayui/src/OkayUI.cpp` - `Theme`, `Style()`, `SlateTheme()`, `UseLauncherLook(bool)`, `PushStyleColor/PopStyleColor`, `PushTextScale`
  - 40.2.2 Font atlas - `okayui/src/OkayUI_FontAtlas.cpp`, `okayui/src/OkayUI_FontAtlas.hpp` - Roboto baked at 13, 16, 20, 26, 34 and 46 px into one RGBA atlas with a white patch; fallback to engine TTF fonts for Cyrillic, Greek, CJK
  - 40.2.3 Bitmap fonts - `okayui/include/okay/UI/OkayUI.hpp` - the engine's 8x8 font, a synthesized bold variant, `SetFont`
  - 40.2.4 Launcher building blocks - `okayui/src/OkayUI.cpp` - `Heading`, `Caption`, `TextWrapped`, `PrimaryButton`, `Badge`, `Avatar`, `BeginCard/EndCard`, `ListItem`, `KeyValue`, `Spinner`, `Toast`
- **Code:** `okayui/src/OkayUI.cpp`, `okayui/src/OkayUI_FontAtlas.cpp`, `okayui/src/OkayUI_FontAtlas.hpp`, `okayui/include/okay/UI/OkayUI.hpp`, `shared/OkayTheme.hpp` (the ImGui twin of the palette, editor and launcher)
- **API:** `OkayUI::UseLauncherLook`, `SlateTheme`, `Style`, script `ui_style("launcher" | "classic")`, `ui_heading`, `ui_card_begin`, `ui_list_item`, `ui_toast`
- **Editor:** View > Panels > Lighting > UI Font (scene UI font, not OkayUI's); the editor Game view shows the launcher look
- **Docs:** `docs/game_ui.md` (top section, Building blocks, Theming)
- **Tests:** `okayui`, `script_ui`
- **Status:** Works - implemented; fonts need the SDL_Renderer path (the launcher look is not available on the D3D11 path)
- **Open:** possible duplication: the Slate palette lives in three places (`OkayUI::SlateTheme`, `UITemplates::slate` for scene UI, `shared/OkayTheme.hpp` for ImGui) and must be kept in step by hand
- **Related:** 40.1, 12.7, 2.9
- **Words:** launcher look, Slate, theme, Roboto, font atlas, classic look, OKAY_CLASSIC_UI, ui_style, text scale, rounding, window shadow, toast, card, badge, avatar
- **Seen as:** text looks pixelated (classic look) when it should be smooth; accent colours differ from the launcher; accented characters or CJK show as boxes; a toast is the wrong colour
- **Ask me like:** "work on 40.2.2, bake extra font sizes"; "the in-game windows should use the dark Slate theme only"

### 40.3 Backends and frame loop
- **What:** How a host feeds input, builds the frame and gets pixels: the SDL_Renderer path (one batched `SDL_RenderGeometry` call on top of ImGui) and a Windows-only raw Direct3D 11 path with its own demo app.
- **Parts:**
  - 40.3.1 Frame loop - `okayui/src/OkayUI.cpp` - `BeginFrame(input)`, widgets, `EndFrame(SDL_Renderer*)`; blend mode saved and restored; draws after ImGui so it sits on top
  - 40.3.2 Draw data - `okayui/include/okay/UI/OkayUI.hpp` - `GetDrawData()` (20-byte vertices, 32-bit indices) and `EndFrameData()` for custom backends
  - 40.3.3 Direct3D 11 backend - `okayui/include/okay/UI/OkayUI_D3D11.hpp`, `okayui/src/OkayUI_D3D11.cpp` - `D3D11Backend::Init/Render/Shutdown`, compiled to nothing off Windows
  - 40.3.4 D3D11 demo - `okayui/d3d11_demo/main.cpp` - standalone Win32 + DX11 app, shipped in the Windows dist `Tools/` folder
  - 40.3.5 Build wiring - `okayui/CMakeLists.txt` - static lib `okay_ui` (alias `okay::ui`), built only when SDL2 is found, skipped on Emscripten/web; linked by editor, player and tests
- **Code:** `okayui/include/okay/UI/OkayUI.hpp`, `okayui/include/okay/UI/OkayUI_D3D11.hpp`, `okayui/src/OkayUI.cpp`, `okayui/src/OkayUI_D3D11.cpp`, `okayui/d3d11_demo/main.cpp`, `okayui/CMakeLists.txt`, `scripts/build-windows-dist.sh`
- **API:** `OkayUI::Input`, `BeginFrame`, `EndFrame`, `GetDrawData`, `EndFrameData`, `OkayUI::D3D11Backend`
- **Editor:** none (the editor links `okay_ui` with `OKAY_HAVE_OKAYUI=1`)
- **Docs:** -
- **Tests:** `okayui` (SDL software renderer only)
- **Status:** Partial - SDL path is tested; the D3D11 backend and demo compile only on Windows, so nothing here could verify them (not run in this survey)
- **Open:** no test of the D3D11 path found; skipped in the web build (per `CMakeLists.txt` and `scripts/build-web.sh` comments)
- **Related:** 40.1, 18.1, 18.2, 2.1
- **Words:** SDL_Renderer, SDL_RenderGeometry, Direct3D 11, D3D11, DX11, okay_ui, okay::ui, draw data, EndFrame, demo, okayui_d3d11_demo
- **Seen as:** OkayUI draws under ImGui or the game; nothing draws on web; the DX11 demo opens a blank window; clicks go through to ImGui
- **Ask me like:** "work on 40.3.3, add scissor clipping to the D3D11 backend"; "does OkayUI work in the web build"

### 40.4 Hosts, script bridge and built-in windows
- **What:** The places OkayUI is wired in: game scripts' `ui_*` builtins (through a bridge), the F1 Test UI panel, the player's server and matchmaking windows and performance overlay, and the editor's Game view overlays (dialogue box, quest tracker) and input routing.
- **Parts:**
  - 40.4.1 Script bridge interface - `Scripting/ScriptUI` - the engine-side `ScriptUIBridge` the `ui_*` builtins call (SDL- and toolkit-free)
  - 40.4.2 OkayUI bridge - `shared/OkayScriptUIBridge.hpp` - implements the interface with OkayUI widgets; the host brackets each frame with BeginFrame/EndFrame
  - 40.4.3 Test UI panel - `shared/OkayTestPanel.hpp` - a panel that exercises every widget; editor View > Test UI and player F1; also a copy-paste starting point
  - 40.4.4 Player windows - `player/main.cpp` - "Find a server" (~412), "Matchmaking" (~558), "Performance (F3)" (~2519), the F1 sample lobby
  - 40.4.5 Editor hosting - `editor/main.cpp` (input routing ~735, Game view overlays ~43051, bridge install ~43910) - text and backspace routed to OkayUI, overlays drawn on the game image
  - 40.4.6 Story overlays - `Components/DialogueRunner` - dialogue box and quest tracker drawn with OkayUI in the launcher look (toast falls back to the scene's UI Toast)
- **Code:** `Scripting/ScriptUI`, `shared/OkayScriptUIBridge.hpp`, `shared/OkayTestPanel.hpp`, `player/main.cpp`, `editor/main.cpp`, `Components/DialogueRunner`
- **API:** `okay::SetScriptUI`, `okay::ScriptUIBridge`, `okay::OkayUIScriptBridge`, `okay_testui::Panel`; script `ui_begin`, `ui_end`, `ui_button`, `ui_slider`, `ui_checkbox`, `ui_switch`, `ui_progress`, `ui_text`
- **Editor:** View > Rendering & Performance > Test UI; plugin panels use the same `ui_*` calls (15.8)
- **Docs:** `docs/scripting_reference.md` (OkayUI section), `docs/game_ui.md`, `docs/dialogue_quests.md`, `docs/editor-plugins.md`
- **Tests:** `script_ui` (mock bridge), `okayui`
- **Status:** Works - bridge and panels are implemented and shared by the editor and player; only the mock-bridge path has a test
- **Open:** the web player links only the engine per the CMake comment, so `ui_*` calls there need a bridge from somewhere else (not verified); `ui_*` calls do nothing in a host that installs no bridge; the editor's plugin windows are a third use of the same bridge (15.8)
- **Related:** 13.4, 40.1, 15.8, 10.2, 17.8
- **Words:** ui_begin, ui_button, script UI, immediate mode HUD, F1 panel, Test UI, Find a server, Matchmaking, F3 performance, dialogue box, quest tracker, ScriptUIBridge
- **Seen as:** `ui_button` does nothing in a build; the F1 panel is missing; the server list window cannot be dragged; the dialogue box is hidden behind the game image; typing in a script UI input also moves the character
- **Ask me like:** "work on 40.4.2, add ui_combo to the script bridge"; "the Find a server window overlaps the HUD"

### 40.5 How OkayUI relates to scene UI
- **What:** Two separate UI systems with different jobs. Pick OkayUI for tool-like or script-drawn UI (panels, debug, lobbies, plugins); pick the scene UI (area 12) for designed menus and HUDs that artists place, save in scenes and ship in the editor preview.
- **Parts:**
  - 40.5.1 Immediate versus retained - `okayui/include/okay/UI/OkayUI.hpp` against `Components/Canvas` - OkayUI keeps no objects and is called per frame; scene UI is GameObject components saved in the scene and drawn by `UI/UIRenderer` through `UI/UIPainter`
  - 40.5.2 Separate renderers - `UI/UIRenderer` against `okayui/src/OkayUI.cpp` - no shared draw code; OkayUI batches into one SDL geometry call, the scene pass uses `ui2d::Painter`/`BatchPainter`
  - 40.5.3 Shared pieces - `Graphics/TtfFont`, `Scripting/ScriptUI`, `Components/UIDocument` - both use the engine font code; the script bridge connects script calls to OkayUI; the UI Document's "OkayUI markup" builds scene widgets, not OkayUI calls
  - 40.5.4 Look kept in step by hand - `Scene/UITemplates`, `okayui/src/OkayUI.cpp` - the launcher Slate look is implemented twice, once per system (see 40.2)
- **Code:** `UI/UIRenderer`, `UI/UIPainter`, `UI/UIBatcher`, `Components/Canvas`, `Components/UIDocument`, `Scene/UITemplates`, `Scripting/ScriptUI`, `okayui/include/okay/UI/OkayUI.hpp`
- **API:** `OkayUI::*` against `okay::ui2d::RenderSceneUI`
- **Editor:** scene UI is edited in the UI view (12.10); OkayUI has no editor, only code
- **Docs:** `docs/game_ui.md` (the page mixes both: the launcher look and Building blocks are OkayUI, Adding UI in the editor is scene UI)
- **Tests:** `okayui`, `ui_system`, `ui_renderer_golden`
- **Status:** Works - a map of how the two relate, read from the code and docs, not a feature
- **Open:** confusing names: header path `okay/UI/` holds both `okayui/include/okay/UI/OkayUI.hpp` and the engine's `UI/*` headers; "OkayUI markup" in `UIDocument` is not the OkayUI library; two UI systems each have a toast, tabs, tooltip, list and theme
- **Related:** 12, 12.5, 12.7, 12.11, 40.1
- **Words:** OkayUI versus Canvas, immediate mode versus scene UI, which UI system, UI toolkit, two UI systems, ui_ calls versus UI components
- **Seen as:** "which UI should I use"; a look changes in one system and not the other; a script UI window and a scene HUD overlap; a feature exists in OkayUI but not in the scene UI (or the reverse)
- **Ask me like:** "do I use OkayUI or a Canvas for a lobby screen"; "make the Matchmaking window a scene UI prebuilt screen"
