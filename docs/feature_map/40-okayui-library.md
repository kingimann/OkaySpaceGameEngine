# 40. OkayUI standalone library

OkayUI is a small immediate-mode UI toolkit (about 3.7k lines in `okayui/`): you call `OkayUI::Button(...)` every frame and it draws and returns the click. It draws through SDL_Renderer (or a raw Direct3D 11 backend on Windows) on top of the game or of Dear ImGui.
It is what script `ui_*` calls, the F1 Test UI panel, the "Find a server" and "Matchmaking" windows and the dialogue box and quest tracker use. It is NOT the scene UI of area 12 (Canvas, `UIButton` and friends): that is a separate retained system with its own renderer.
This area stands alone (own CMake target `okay_ui`, own backends, own font atlas, own tests), so it is not folded into area 12.
OkayUI is the one own UI toolkit that is meant to replace Dear ImGui over time (owner decision, `docs/own_ui_toolkit.md`); the headless software backend and test harness of 40.6 are part of it.

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
- **API:** namespace `OkayUI`: `BeginFrame(Input)`, widgets above, `Style()`, `Input` struct (mouse, buttons, typed text, key edges for Backspace, Delete, arrows, Home, End, Enter, Escape, select-all, shift, wheel, `blocked`), `GetItemRect`, `IsItemHovered`, `WantsTextInput`, `ResetState`, `ResizableNextWindow`, `InputText` flags
- **Editor:** View > Rendering & Performance > Test UI draws the sample panel over the editor; Game view overlays
- **Docs:** `docs/game_ui.md` (Building blocks, Theming), `docs/scripting_reference.md` (OkayUI section)
- **Tests:** `okayui` (SDL software renderer, reads pixels back: widgets, windows, launcher look), `okayui_behaviour` (synthetic mouse, keys and text: button, checkbox, sliders, input text, combo, tree, scrolling child, layout, windows, popups), `okayui_fuzz`
- **Status:** Works - implemented (about 2.8k-line .cpp) and used by the player and editor; text field edits whole UTF-8 characters with caret and selection; open combos and popups block the mouse below them
- **Open:** see "Not done" in `docs/own_ui_toolkit.md` (no window z-order, no `BeginGroup`, no `##` label hiding, no public draw-list API, no cursor and content-region queries, no docking); stale header comment says "v1 scope: OkayUI::Button" while the file has about 70 widgets; header says "no STL" but the .cpp includes `<algorithm>`
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
  - 40.3.2 Draw data - `okayui/include/okay/UI/OkayUI.hpp` - `GetDrawData()` (20-byte `DrawVertex`, 32-bit indices, `DrawCmd` ranges with clip rectangle and texture, the launcher atlas pixels) and `EndFrameData()` for custom backends
  - 40.3.3 Direct3D 11 backend - `okayui/include/okay/UI/OkayUI_D3D11.hpp`, `okayui/src/OkayUI_D3D11.cpp` - `D3D11Backend::Init/Render/Shutdown`, compiled to nothing off Windows
  - 40.3.4 D3D11 demo - `okayui/d3d11_demo/main.cpp` - standalone Win32 + DX11 app, shipped in the Windows dist `Tools/` folder
  - 40.3.5 Build wiring - `okayui/CMakeLists.txt` - static lib `okay_ui` (alias `okay::ui`), built only when SDL2 is found, skipped on Emscripten/web; linked by editor, player and tests
- **Code:** `okayui/include/okay/UI/OkayUI.hpp`, `okayui/include/okay/UI/OkayUI_D3D11.hpp`, `okayui/src/OkayUI.cpp`, `okayui/src/OkayUI_D3D11.cpp`, `okayui/d3d11_demo/main.cpp`, `okayui/CMakeLists.txt`, `scripts/build-windows-dist.sh`
- **API:** `OkayUI::Input`, `BeginFrame`, `EndFrame`, `GetDrawData`, `DrawCmd`, `EndFrameData`, `OkayUI::D3D11Backend`, `OkayUI::soft::Render` (40.6)
- **Editor:** none (the editor links `okay_ui` with `OKAY_HAVE_OKAYUI=1`)
- **Docs:** -
- **Tests:** `okayui` (SDL software renderer), `okayui_soft` (the draw list through the CPU backend)
- **Status:** Partial - SDL path and the software backend are tested; the D3D11 backend and demo compile only on Windows, so nothing here could verify them (not run in this survey)
- **Open:** no test of the D3D11 path found, and it still draws only vertices and indices (no clip rectangles, no atlas texture: it should use `DrawCmd` like the SDL path); skipped in the web build (per `CMakeLists.txt` and `scripts/build-web.sh` comments)
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

### 40.6 Headless software backend and test harness
- **What:** OkayUI's draw list rasterised on the CPU into an RGBA bitmap, plus a synthetic-input test harness, so the toolkit is tested with no window, GPU or display: behaviour tests, golden images, a sanitizer fuzz test and a demo that writes a PNG. Folded in from the first `okay::imui` core (which is gone: OkayUI is the one toolkit).
- **Parts:**
  - 40.6.1 Software rasteriser - `okayui/soft/OkayUI_Soft.hpp`, `okayui/soft/OkayUI_Soft.cpp` - `OkayUI::soft::Render(DrawData, Bitmap&)`: top-left fill rule, per-command scissor, source-over blend, flat colour and launcher atlas sampling, hostile draw data skipped
  - 40.6.2 Test kit - `okayui/tests/OkayUITestKit.hpp` - `Sim` (persistent mouse, one-shot text and key edges fed as a plain `OkayUI::Input`), `Reset()`, PNG reader, golden comparison with tolerance
  - 40.6.3 Behaviour and rasteriser tests - `okayui/tests/test_okayui_behaviour.cpp`, `okayui/tests/test_okayui_soft.cpp` - the okay::imui widget tests ported to OkayUI widgets, and exact-pixel rasteriser tests
  - 40.6.4 Golden images and demo - `okayui/tests/test_okayui_golden.cpp`, `okayui/tests/golden/`, `okayui/demo/` - five committed PNGs; `okayui_headless_demo [out.png] [classic|launcher]`
  - 40.6.5 Fuzz - `okayui/tests/test_okayui_fuzz.cpp` - seeded random input and widget trees, text-editing reference model, layout properties, batch overflow, hostile draw data; run under ASan and UBSan
- **Code:** `okayui/soft/OkayUI_Soft.hpp`, `okayui/soft/OkayUI_Soft.cpp`, `okayui/tests/OkayUITestKit.hpp`, `okayui/tests/test_okayui_soft.cpp`, `okayui/tests/test_okayui_behaviour.cpp`, `okayui/tests/test_okayui_golden.cpp`, `okayui/tests/test_okayui_fuzz.cpp`, `okayui/demo/DemoScene.hpp`, `okayui/demo/okayui_headless_demo.cpp`, `okayui/CMakeLists.txt`
- **API:** `OkayUI::soft::Render`, `OkayUI::soft::Bitmap`, `OkayUI::ResetState`, CMake option `OKAY_BUILD_OKAYUI_TESTS` (ON by default; needs `OKAY_BUILD_TESTS`), library `okay_ui_soft`; env `OKAYUI_UPDATE_GOLDEN=1`, `OKAYUI_FUZZ_ITERS`, `OKAYUI_FUZZ_SEED`
- **Editor:** none (test infrastructure; the editor, player and launcher are not wired to it)
- **Docs:** `docs/own_ui_toolkit.md`, `docs/own_code_roadmap.md`
- **Tests:** `okayui_soft`, `okayui_behaviour`, `okayui_golden`, `okayui_fuzz`, `okayui_drawlist` (draw list and cursor, 40.7)
- **Status:** Works - implemented; not run against the real editor or player. The software backend skips user and fallback-font textures and samples the atlas without filtering
- **Open:** the behaviour `okay::imui` had that OkayUI lacks is listed under "Not done" in `docs/own_ui_toolkit.md` (window z-order, key repeat, `BeginGroup`, label hiding); the ranked list of Dear ImGui calls still uncovered is in the same page
- **Related:** 40.1, 40.3, 15.1, 17.1
- **Words:** headless UI test, software UI renderer, golden image, okayui_soft, okayui_behaviour, okayui_golden, okayui_fuzz, OKAY_BUILD_OKAYUI_TESTS, Sim, ResetState, UI fuzz, replace Dear ImGui
- **Seen as:** a UI golden test differs by a few pixels; typing in a text field drops a character; a window cannot be dragged in the demo; the okayui test targets are missing from a build
- **Ask me like:** "work on 40.6.3, add a test for the table widget"; "run the OkayUI fuzz test under the sanitizers"

### 40.7 Draw list, cursor, item state and disabled blocks
- **What:** the first gap closed from the Dear ImGui usage audit: a public draw list for custom drawing (lines, shapes, curves, paths, text, clip), the cursor and measurement calls that put custom drawing inside the auto layout, `InvisibleButton` with item-state and mouse queries, and `BeginDisabled`/`EndDisabled`. Side by side with Dear ImGui; nothing in the editor or launcher calls it yet.
- **Parts:**
  - 40.7.1 Draw list - `okayui/include/okay/UI/OkayUI.hpp`, `okayui/src/OkayUI.cpp` - `GetWindowDrawList()` and `GetForegroundDrawList()`: `AddLine`, `AddRect`, `AddRectFilled`, `AddRectFilledMultiColor`, `AddCircle`, `AddCircleFilled`, `AddTriangle`, `AddQuad`, `AddPolyline` (mitred joins), `AddConvexPolyFilled`, `AddBezierCubic`, `AddText`, `AddImage`, the path calls, `PushClipRect`; packed `Color32` with `RGBA`, `GetColorU32`, `ColorMulAlpha`
  - 40.7.2 Cursor and measurement - `okayui/src/OkayUI.cpp` - `GetCursorScreenPos`, `SetCursorScreenPos`, `GetCursorPos` and `SetCursorPos` (and the X and Y forms), `GetContentRegionAvail`, `CalcTextSize`, `GetTextLineHeight`, `GetFontSize`, `GetFrameHeight`
  - 40.7.3 Item state and mouse queries - `okayui/src/OkayUI.cpp` - `InvisibleButton`, `IsItemActive`, `IsItemActivated`, `IsItemDeactivated`, `IsItemClicked`, `IsMouseDown`, `IsMouseClicked`, `IsMouseReleased`, `GetMousePos`, `GetMouseDelta`, `IsMouseHoveringRect`
  - 40.7.4 Disabled blocks - `okayui/src/OkayUI.cpp` - `BeginDisabled(bool)`, `EndDisabled`, `IsDisabled`: no hover or input and colours halfway to the panel colour for the widgets inside; a focused text field gives up the keyboard
  - 40.7.5 Draw list lab - `okayui/demo/DemoScene.hpp`, `okayui/demo/okayui_headless_demo.cpp` - a canvas page using all of the above, rendered with `okayui_headless_demo out.png lab` (or `lab-launcher`)
- **Code:** `okayui/include/okay/UI/OkayUI.hpp`, `okayui/src/OkayUI.cpp`, `okayui/tests/test_okayui_drawlist.cpp`, `okayui/tests/test_okayui_fuzz.cpp`, `okayui/demo/DemoScene.hpp`, `okayui/demo/okayui_headless_demo.cpp`
- **API:** `OkayUI::DrawList`, `OkayUI::GetWindowDrawList`, `OkayUI::GetForegroundDrawList`, `OkayUI::Vec2`, `OkayUI::Color32`, `OkayUI::InvisibleButton`, `OkayUI::BeginDisabled`, `OkayUI::GetContentRegionAvail`
- **Editor:** none (library and headless demo only; the editor's Test UI and the player overlay do not show it)
- **Docs:** `docs/own_ui_toolkit.md` (section 4)
- **Tests:** `okayui_drawlist`, `okayui_fuzz`
- **Status:** Works - implemented and tested headless (pixels, goldens, fuzz); not run in the real editor or launcher. Concave polygons, rounded-corner masks, anti-aliasing, a printf-style text call and text with newlines are not covered
- **Open:** the remaining gaps are ranked in `docs/own_ui_toolkit.md` section 4; the Direct3D 11 backend still ignores clip spans, so draw-list clipping does not work there
- **Related:** 40.1, 40.6
- **Words:** draw list, GetWindowDrawList, custom drawing, canvas, InvisibleButton, BeginDisabled, cursor position, content region, CalcTextSize, polyline, bezier, replace Dear ImGui
- **Seen as:** a custom-drawn panel does not clip; a disabled widget still reacts; a translucent line is darker at its corners
- **Ask me like:** "work on 40.7.1, add AddConcavePolyFilled"; "render the draw list lab page to a PNG"

### 40.8 Opt-in OkayUI backend in the launcher and editor
- **What:** The first step that runs OkayUI inside the real apps: a setting "UI backend: Dear ImGui (default) / OkayUI (preview)" (and the env var `OKAY_UI_BACKEND`, and the CMake default `OKAY_UI_DEFAULT_BACKEND`) that makes one ported screen per app draw with OkayUI instead of Dear ImGui: the launcher's What's New page and the editor's Console panel. Everything else is still Dear ImGui, and Dear ImGui is the default.
- **Parts:**
  - 40.8.1 Backend switch - `shared/OkayUIBackend.hpp` - `Backend {ImGui, OkayUI}`, `Parse`, `Normalize`, `Name`, `Label`, `Resolve` (env over setting over build default; Dear ImGui when OkayUI is not compiled in); saved as `ui_backend=` in `launcher.cfg` and `uibackend 0|1` in `okay_settings.txt`
  - 40.8.2 Host layer - `shared/OkayUIHost.hpp` - `InputCollector` (SDL events to `OkayUI::Input`: text, Backspace, Delete, arrows, Home, End, Enter, Escape, select-all, shift, wheel) and `Region` (the rectangle a hosted screen occupies inside an ImGui window, so OkayUI owns the mouse there and ImGui everywhere else)
  - 40.8.3 Screens - `okayui/screens/OkayUIScreens.*` - `DrawWhatsNew` (version, update status and its button, release cards, earlier releases, link to all releases) and `DrawConsole` (search, category, level toggles with counts, collapse, auto-scroll, Clear on Play, Pause on error, Copy all, Save, per-row copy menu, source jump, details, Open Script); plain functions with no SDL and no ImGui
  - 40.8.4 Library additions - `okayui/src/OkayUI.cpp` - `BareNextWindow` (a window without a title bar that fills a host's rectangle), `GetScrollY`, `GetScrollMaxY`, `SetScrollY`, `CollapsingHeader(label, defaultOpen)`, `##` hidden label suffix for `InputText` and `Combo`
  - 40.8.5 App wiring - `launcher/main.cpp` (Settings > Appearance > Interface > UI backend, frame bracket, What's New branch), `editor/main.cpp` and `editor/ConsolePanel.cpp` (the "UI backend" menu entry, input and mouse ownership, `DrawConsoleOkayUI`)
- **Code:** `shared/OkayUIBackend.hpp`, `shared/OkayUIHost.hpp`, `shared/OkayUIHostImGui.hpp`, `shared/ConsoleScriptLink.hpp`, `okayui/screens/OkayUIScreens.hpp`, `okayui/screens/OkayUIScreens.cpp`, `okayui/tests/test_okayui_screens.cpp`, `okayui/tests/test_okayui_host.cpp`, `tests/test_ui_backend.cpp`, `launcher/main.cpp`, `editor/main.cpp`, `editor/ConsolePanel.cpp`, `okayui/CMakeLists.txt`, `CMakeLists.txt`
- **API:** env `OKAY_UI_BACKEND=okayui|imgui`; CMake `-DOKAY_UI_DEFAULT_BACKEND=imgui|okayui`; `okayuibackend::Resolve`; `okayuihost::InputCollector`, `okayuihost::Region`; `okayuiscreens::DrawWhatsNew`, `okayuiscreens::DrawConsole`
- **Editor:** View > Rendering & Performance > UI backend (next to VSync); with OkayUI chosen the Console panel is drawn by OkayUI inside its dock tab
- **Docs:** `docs/own_ui_toolkit.md` (section 5, with the design, what is not covered and the screenshots)
- **Tests:** `ui_backend` (parse, persist, precedence, default), `okayui_host` (events to input, region ownership, typing through OkayUI), `okayui_screens` (every control of both screens, 6 golden images)
- **Status:** Partial - both ported screens work and were run in the real windows under Xvfb; preview, off by default
- **Open:** OkayUI draws on top of every ImGui window, so a window or menu that overlaps a hosted rectangle is covered by it; one frame of mouse-ownership latency; the other pages and panels are Dear ImGui; differences from the ported pages are listed in `docs/own_ui_toolkit.md` section 5.5; not run on Windows or with a GPU
- **Related:** 40.3, 40.4, 40.6, 15.11, 17.1
- **Words:** UI backend, OKAY_UI_BACKEND, OKAY_UI_DEFAULT_BACKEND, OkayUI preview, hosted region, BareNextWindow, InputCollector, okayuiscreens, okayuihost, What's New OkayUI, Console OkayUI, replace Dear ImGui
- **Seen as:** the setting does nothing (OKAY_UI_BACKEND is set and wins); a panel is covered by the OkayUI page; typing in the OkayUI search field also triggers an editor shortcut; the What's New page looks different from the old one
- **Ask me like:** "work on 40.8.3, port the Stats panel"; "make OkayUI the default backend in this build"
