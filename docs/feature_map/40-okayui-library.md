# 40. OkayUI standalone library

OkayUI is a small immediate-mode UI toolkit (about 3.7k lines in `okayui/`): you call `OkayUI::Button(...)` every frame and it draws and returns the click. It builds each frame as draw data that `okay::platform::Painter` draws (OkayUI::soft::FrameDrawer: the OpenGL or the software painter), or a render backend (software, OpenGL, raw Direct3D 11).
It is what script `ui_*` calls, the F1 Test UI panel, the "Find a server" and "Matchmaking" windows and the dialogue box and quest tracker use. It is NOT the scene UI of area 12 (Canvas, `UIButton` and friends): that is a separate retained system with its own renderer.
This area stands alone (own CMake target `okay_ui`, own backends, own font atlas, own tests), so it is not folded into area 12.
OkayUI is the one own UI toolkit that is meant to replace Dear ImGui over time (owner decision, `docs/ui/own_ui_toolkit.md`); the headless software backend and test harness of 40.6 are part of it.

## 40. OkayUI standalone library

### 40.1 Core widgets and auto-layout
- **System:** OkayUI - Our own UI toolkit for the launcher, editor panels and script-drawn windows (replacing Dear ImGui) - duplicate of Game UI (kept on purpose: both have buttons, tabs, tooltips and a theme, but OkayUI is for tools and Game UI for designed game menus; see 40.5)
  Home: `okayui/`, `shared/OkayUIHost.hpp`. Entry: `okayui/include/okay/UI/OkayUI.hpp`. Also called: own UI, okayui host, OkayUIHost, UI toolkit, Dear ImGui replacement, ImGui, OKAY_UI_OWN_RENDER, OKAY_UI_DEFAULT_BACKEND, OKAY_CLASSIC_UI, OkayUI designs, design tokens, appearance, appearance.json, okaytheme, OKAY_APPEARANCE_FILE. Fix: Host code moves from shared/OkayUIHost* into okayui/ (modularization plan).
- **What:** The widget set: explicit-coordinate widgets (button, checkbox, switch, slider, progress, radio, tab, text field) and an ImGui-style auto-layout API (windows, scrolling children, same-line, tables, menus, popups, trees, plots, drag and drop, colour pickers).
- **Parts:**
  - 40.1.1 Explicit-coordinate widgets - `okayui/include/okay/UI/OkayUI.hpp`, `okayui/src/OkayUI.cpp` - `Button(id, x, y, w, h, label)`, `Checkbox`, `ToggleSwitch`, `Slider`, `ProgressBar`, `RadioButton`, `Tab`, `TextField`, `Label`, `Panel`
  - 40.1.2 Windows and layout - `okayui/src/OkayUI.cpp` - `Begin/End` draggable collapsible windows, `PinNextWindow`, `BeginChild` scroll regions, `SameLine`, `Indent`, `Separator`, `Spacing`, item width, clip rects, `PushID`
  - 40.1.3 Value widgets - `okayui/src/OkayUI.cpp` - `SliderFloat/SliderInt`, `VSliderFloat`, `Knob`, `DragFloat/DragInt`, `InputText`, `InputInt`, `Combo`, `ListBox`, `ColorEdit3`, `ColorPicker3`
  - 40.1.4 Structure widgets - `okayui/src/OkayUI.cpp` - `CollapsingHeader`, `TreeNode`, `Columns`, `BeginTable` (resizable columns), menu bar, popups and context popups, `Selectable`, `Tooltip`
  - 40.1.5 Data display - `okayui/src/OkayUI.cpp` - `PlotLines`, `PlotHistogram`, `LabelText`, `Image`, `ImageButton`, bullets, wrapped and coloured text
  - 40.1.6 Drag and drop - `okayui/src/OkayUI.cpp` - `DragSource(payload)`, `DropTarget(&payload)`, `IsDragging`
- **Code:** `okayui/include/okay/UI/OkayUI.hpp`, `okayui/src/OkayUI.cpp`
- **API:** namespace `OkayUI`: `BeginFrame(Input)`, widgets above, `Style()`, `Input` struct (mouse, buttons, typed text, key edges for Backspace, Delete, arrows, Home, End, Enter, Escape, select-all, shift, wheel, `blocked`), `GetItemRect`, `IsItemHovered`, `IsItemEdited`, `IsItemDeactivatedAfterEdit`, `CalcItemWidth`, `BeginPopupContextWindow`, `WantsTextInput`, `ResetState`, `ResizableNextWindow`, `InputText` flags
- **Editor:** View > Rendering & Performance > Test UI draws the sample panel over the editor; Game view overlays
- **Docs:** `docs/ui/in_game_ui.md` (Building blocks, Theming), `docs/scripting/scripting.md` (OkayUI section)
- **Tests:** `okayui` (the software painter, reads pixels back: widgets, windows, launcher look), `okayui_behaviour` (synthetic mouse, keys and text: button, checkbox, sliders, input text, combo, tree, scrolling child, layout, windows, popups), `okayui_fuzz`, `okayui_itemedit` (edit tracking, item width, window context popup)
- **Status:** Works - implemented (about 2.8k-line .cpp) and used by the player and editor; text field edits whole UTF-8 characters with caret and selection; open combos and popups block the mouse below them
- **Open:** see "Not done" in `docs/ui/own_ui_toolkit.md` (no window z-order, no `BeginGroup`, no `##` label hiding, no public draw-list API, no cursor and content-region queries, no docking); stale header comment says "v1 scope: OkayUI::Button" while the file has about 70 widgets; header says "no STL" but the .cpp includes `<algorithm>`
- **Related:** 40.2, 40.4, 12.4
- **Words:** OkayUI, immediate mode, IMGUI, Button, Checkbox, Slider, TextField, Begin End window, SameLine, table, combo, tree node, popup
- **Seen as:** a button does not click in the player; a window cannot be dragged; text typed into a field goes to the game; a table column is the wrong width; a Combo list draws under other widgets
- **Ask me like:** "work on 40.1.4, add keyboard navigation to OkayUI"; "OkayUI InputText loses focus on click"

### 40.2 Designs, theme and fonts
- **System:** OkayUI
- **What:** OkayUI's own design language and the ONE theme source of every program: eleven built-in designs (Graphite, the default; Slate, the launcher's old look; Midnight; High Contrast; Paper; Ember; Glacier; Dusk; Blossom; Void; Aurora), each a data set of colours (dark and/or light) and shape choices (radii, outlines or none, shadow depth, tab shape, focus ring, density, motion), with the person's options on top (light or dark, accent, density, roundness, UI scale, text size, shadows, reduce motion) saved in one per-user `appearance.json` and followed live by the launcher, the editor, the Script Editor and the browser. The widgets draw from the tokens: buttons, fields with a focus ring and placeholder, vector check / radio / switch glyphs, sliders with a thumb, combo boxes and popover lists, underline / pill / segmented tabs, a clean window header, section headers with a chevron, trees with indent guides, rounded elevated menus with right-aligned shortcuts, tooltip cards, thin overlay scroll bars, toasts; short hover / press transitions when the host has a clock. Roboto TrueType text everywhere; the 8x8 bitmap font only as the last fallback (and in the headless tests).
- **Parts:**
  - 40.2.1 Designs and options - `okayui/include/okay/UI/OkayUIDesign.hpp`, `okayui/src/OkayUI_Design.cpp` - `design::DesignAt`, `Resolve`, `ThemeFor`, `Set`, `Current`, `Revision`, `FollowUserFile`, `Poll`, `SaveUserFile`, `ParseJson` / `WriteJson`
  - 40.2.2 Theme - `okayui/src/OkayUI_Style.cpp`, `okayui/include/okay/UI/OkayUI.hpp` - `Theme` (tokens: surfaces, text, status colours, focus ring, radii, tab shape, shadow depth, motion, density), `Style()`, `SlateTheme()`, `UseLauncherLook(bool)` (TrueType text on or off, frozen name), `PushStyleColor/PopStyleColor`, `PushTextScale`
  - 40.2.3 Shapes and motion - `okayui/src/OkayUI.cpp` - `elevation`, `ringRect`, `focusRing`, `controlBox`, `popoverBox`, `comboBox`, `anim` / `stateColor` (time-based, off without a host clock or with reduce motion)
  - 40.2.4 Dear ImGui adapter and the appearance controls - `shared/OkayTheme.hpp` (`PaletteFrom`, `ApplyDesign`, `ApplyDensity`), `shared/AppearanceUI.hpp` (`okayappearance::DesignPicker`, `Customization`) - the launcher's Settings > Appearance and the editor's Edit > Appearance
  - 40.2.5 Font atlas - `okayui/src/OkayUI_FontAtlas.cpp`, `okayui/src/OkayUI_FontAtlas.hpp` - Roboto baked at 13, 16, 20, 26, 34 and 46 px into one RGBA atlas with a white patch; fallback to engine TTF fonts for Cyrillic, Greek, CJK
  - 40.2.6 Bitmap fonts - `okayui/include/okay/UI/OkayUI.hpp` - the engine's 8x8 font (the last fallback), a synthesized bold variant, `SetFont`
  - 40.2.7 Building blocks - `okayui/src/OkayUI_Toolkit.cpp` - `Heading`, `Caption`, `TextWrapped`, `PrimaryButton`, `Badge`, `Avatar`, `BeginCard/EndCard`, `ListItem`, `KeyValue`, `Spinner`, `Toast`
- **Code:** `okayui/src/OkayUI_Design.cpp`, `okayui/src/OkayUI.cpp`, `okayui/src/OkayUI_Basic.cpp`, `okayui/src/OkayUI_Style.cpp`, `okayui/src/OkayUI_FontAtlas.cpp`, `okayui/include/okay/UI/OkayUIDesign.hpp`, `okayui/include/okay/UI/OkayUI.hpp`, `shared/OkayTheme.hpp`, `shared/AppearanceUI.hpp`
- **API:** `OkayUI::design::Set`, `design::FollowUserFile`, `UseLauncherLook`, `SlateTheme`, `Style`, script `ui_style("launcher" | "classic")` (classic: the bitmap font), `ui_heading`, `ui_card_begin`, `ui_list_item`, `ui_toast`
- **Editor:** Edit > Appearance (designs with live previews, options), Edit > Dark Mode / Light Mode (the design's scheme); launcher: Settings > Appearance
- **Docs:** `docs/ui/okayui_design.md` (tokens, widget anatomy, decisions), `docs/ui/in_game_ui.md` (top section, Building blocks, Theming)
- **Tests:** `okayui_design` (designs complete and readable, options, appearance.json, following the file, motion, focus ring, a gallery golden per design), `okayui_golden` and the other okayui picture tests, `launcher_theme`, `menu_bar_model`
- **Status:** Partial - round 1: tokens, eleven designs, options, the launcher and editor wiring and the core widgets are done and tested headless; tables (row hover, sticky headers), docking tabs and splitters and CodeEdit chrome still use the older shapes (round 2); OkayUI has no keyboard navigation between buttons, so the focus ring shows on text fields and drag boxes only. Unverified on Windows and on the Direct3D 11 renderer.
- **Open:** round 2: tables, docking, CodeEdit chrome, Tab-key focus navigation with the ring on every control; the editor's View > UI Scale multiplies the shared UI scale; scene UI's `UITemplates::slate` is still its own copy of the Slate colours
- **Related:** 40.1, 12.7, 2.9, 15.15.6, 17.1.7
- **Words:** design, designs, design tokens, appearance, appearance.json, Graphite, Slate, Midnight, High Contrast, Paper, Ember, Glacier, Dusk, Blossom, Void, Aurora, accent, density, roundness, reduce motion, focus ring, launcher look, theme, Roboto, font atlas, classic look, OKAY_CLASSIC_UI, ui_style, text scale, rounding, window shadow, toast, card, badge, avatar
- **Seen as:** text looks pixelated (classic look) when it should be smooth; accent colours differ from the launcher; accented characters or CJK show as boxes; a toast is the wrong colour
- **Ask me like:** "work on 40.2.2, bake extra font sizes"; "the in-game windows should use the dark Slate theme only"

### 40.3 Backends and frame loop
- **System:** OkayUI
- **What:** How a host feeds input, builds the frame and gets pixels: the painter path (`okay::pb::OkayUIEndFrame`: FrameDrawer on the window's painter; the SDL_Renderer path was deleted 2026-11-27) and a Windows-only raw Direct3D 11 path with its own demo app.
- **Parts:**
  - 40.3.1 Frame loop - `okayui/src/OkayUI.cpp` - `BeginFrame(input)`, widgets, `EndFrameData(w, h)`, then a backend draws `GetDrawData()`
  - 40.3.2 Draw data - `okayui/include/okay/UI/OkayUI.hpp` - `GetDrawData()` (20-byte `DrawVertex`, 32-bit indices, `DrawCmd` ranges with clip rectangle and texture, the launcher atlas pixels) and `EndFrameData()` for custom backends
  - 40.3.3 Direct3D 11 backend - `okayui/include/okay/UI/OkayUI_D3D11.hpp`, `okayui/src/OkayUI_D3D11.cpp` - `D3D11Backend::Init/Render/Shutdown`, compiled to nothing off Windows
  - 40.3.4 D3D11 demo - `okayui/d3d11_demo/main.cpp` - standalone Win32 + DX11 app, shipped in the Windows dist `Tools/` folder
  - 40.3.5 Build wiring - `okayui/CMakeLists.txt` - static lib `okay_ui` (alias `okay::ui`), built only when SDL2 is found, skipped on Emscripten/web; linked by editor, player and tests
  - 40.3.6 Own renderer in the host path (OpenGL and Direct3D 11) - `okayui/include/okay/UI/OkayUIRenderSDL.hpp`, `okayui/render/OkayUIRender_SDL.cpp`, `shared/OkayUIFrame.hpp` - `SdlHost`: with `OKAY_UI_OWN_RENDER=1` (Settings > Experimental "OkayUI own renderer"; on by default since 2026-11-23) the launcher's and editor's OkayUI frame is drawn by OkayUI's own backend instead of `SDL_RenderGeometry`: OpenGL inside SDL's OpenGL renderers, Direct3D 11 inside SDL's `direct3d11` renderer (the Windows default; tested only under Wine, not on Windows). SDL texture images stay on the own renderer (GL: SDL's texture shared via `SDL_GL_BindTexture`; D3D11: copied by SDL into a kept render target each frame). Other SDL renderers, a render target, a logical size or an image that cannot be shared keep the old path
- **Code:** `okayui/include/okay/UI/OkayUI.hpp`, `okayui/include/okay/UI/OkayUI_D3D11.hpp`, `okayui/src/OkayUI.cpp`, `okayui/src/OkayUI_D3D11.cpp`, `okayui/d3d11_demo/main.cpp`, `okayui/CMakeLists.txt`, `scripts/build-windows-dist.sh`, `okayui/include/okay/UI/OkayUIRenderSDL.hpp`, `okayui/render/OkayUIRender_SDL.cpp`, `shared/OkayUIFrame.hpp`
- **API:** `OkayUI::Input`, `BeginFrame`, `EndFrame`, `DrawFinishedFrame`, `GetDrawData`, `DrawCmd`, `EndFrameData`, `OkayUI::D3D11Backend`, `OkayUI::soft::Render` (40.6), `OkayUI::render::SdlHost`, `HostBackendFor`, `Renderer::SetNativeImage`, `Backend::WrapNativeTexture`, env `OKAY_UI_OWN_RENDER`
- **Editor:** none (the editor links `okay_ui` with `OKAY_HAVE_OKAYUI=1`)
- **Docs:** -
- **Tests:** `okayui` (SDL software renderer), `okayui_soft` (the draw list through the CPU backend), `okayui_render_sdlhost` (the host path with the own OpenGL renderer on SDL's OpenGL and OpenGL ES renderers, offscreen EGL on Mesa llvmpipe, against the software reference)
- **Status:** Partial - SDL path and the software backend are tested; the own OpenGL renderer in the host path is tested headless on llvmpipe only (never on a real GPU, on Windows, or in a running editor or launcher); the D3D11 backend and demo compile only on Windows, so nothing here could verify them (not run in this survey)
- **Open:** no test of the D3D11 path found, and it still draws only vertices and indices (no clip rectangles, no atlas texture: it should use `DrawCmd` like the SDL path); skipped in the web build (per `CMakeLists.txt` and `scripts/build-web.sh` comments)
- **Related:** 40.1, 18.1, 18.2, 2.1
- **Words:** SDL_Renderer, SDL_RenderGeometry, Direct3D 11, D3D11, DX11, okay_ui, okay::ui, draw data, EndFrame, demo, okayui_d3d11_demo
- **Seen as:** OkayUI draws under ImGui or the game; nothing draws on web; the DX11 demo opens a blank window; clicks go through to ImGui
- **Ask me like:** "work on 40.3.3, add scissor clipping to the D3D11 backend"; "does OkayUI work in the web build"

### 40.4 Hosts, script bridge and built-in windows
- **System:** OkayUI
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
- **Docs:** `docs/scripting/scripting.md` (OkayUI section), `docs/ui/in_game_ui.md`, `docs/gameplay/dialogue_quests.md`, `docs/editor/editor-plugins.md`
- **Tests:** `script_ui` (mock bridge), `okayui`
- **Status:** Works - bridge and panels are implemented and shared by the editor and player; only the mock-bridge path has a test
- **Open:** the web player links only the engine per the CMake comment, so `ui_*` calls there need a bridge from somewhere else (not verified); `ui_*` calls do nothing in a host that installs no bridge; the editor's plugin windows are a third use of the same bridge (15.8)
- **Related:** 13.4, 40.1, 15.8, 10.2, 17.8
- **Words:** ui_begin, ui_button, script UI, immediate mode HUD, F1 panel, Test UI, Find a server, Matchmaking, F3 performance, dialogue box, quest tracker, ScriptUIBridge
- **Seen as:** `ui_button` does nothing in a build; the F1 panel is missing; the server list window cannot be dragged; the dialogue box is hidden behind the game image; typing in a script UI input also moves the character
- **Ask me like:** "work on 40.4.2, add ui_combo to the script bridge"; "the Find a server window overlaps the HUD"

### 40.5 How OkayUI relates to scene UI
- **System:** OkayUI
- **What:** Two separate UI systems with different jobs. Pick OkayUI for tool-like or script-drawn UI (panels, debug, lobbies, plugins); pick the scene UI (area 12) for designed menus and HUDs that artists place, save in scenes and ship in the editor preview.
- **Parts:**
  - 40.5.1 Immediate versus retained - `okayui/include/okay/UI/OkayUI.hpp` against `Components/Canvas` - OkayUI keeps no objects and is called per frame; scene UI is GameObject components saved in the scene and drawn by `UI/UIRenderer` through `UI/UIPainter`
  - 40.5.2 Separate renderers - `UI/UIRenderer` against `okayui/src/OkayUI.cpp` - no shared draw code; OkayUI batches into one SDL geometry call, the scene pass uses `ui2d::Painter`/`BatchPainter`
  - 40.5.3 Shared pieces - `Graphics/TtfFont`, `Scripting/ScriptUI`, `Components/UIDocument` - both use the engine font code; the script bridge connects script calls to OkayUI; the UI Document's "OkayUI markup" builds scene widgets, not OkayUI calls
  - 40.5.4 Look kept in step by hand - `Scene/UITemplates`, `okayui/src/OkayUI.cpp` - the launcher Slate look is implemented twice, once per system (see 40.2)
- **Code:** `UI/UIRenderer`, `UI/UIPainter`, `UI/UIBatcher`, `Components/Canvas`, `Components/UIDocument`, `Scene/UITemplates`, `Scripting/ScriptUI`, `okayui/include/okay/UI/OkayUI.hpp`
- **API:** `OkayUI::*` against `okay::ui2d::RenderSceneUI`
- **Editor:** scene UI is edited in the UI view (12.10); OkayUI has no editor, only code
- **Docs:** `docs/ui/in_game_ui.md` (the page mixes both: the launcher look and Building blocks are OkayUI, Adding UI in the editor is scene UI)
- **Tests:** `okayui`, `ui_system`, `ui_renderer_golden`
- **Status:** Works - a map of how the two relate, read from the code and docs, not a feature
- **Open:** confusing names: header path `okay/UI/` holds both `okayui/include/okay/UI/OkayUI.hpp` and the engine's `UI/*` headers; "OkayUI markup" in `UIDocument` is not the OkayUI library; two UI systems each have a toast, tabs, tooltip, list and theme
- **Related:** 12, 12.5, 12.7, 12.11, 40.1
- **Words:** OkayUI versus Canvas, immediate mode versus scene UI, which UI system, UI toolkit, two UI systems, ui_ calls versus UI components
- **Seen as:** "which UI should I use"; a look changes in one system and not the other; a script UI window and a scene HUD overlap; a feature exists in OkayUI but not in the scene UI (or the reverse)
- **Ask me like:** "do I use OkayUI or a Canvas for a lobby screen"; "make the Matchmaking window a scene UI prebuilt screen"

### 40.6 Headless software backend and test harness
- **System:** OkayUI
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
- **Docs:** `docs/ui/own_ui_toolkit.md`, `docs/status/own_code_roadmap.md`
- **Tests:** `okayui_soft`, `okayui_behaviour`, `okayui_golden`, `okayui_fuzz`, `okayui_drawlist` (draw list and cursor, 40.7)
- **Status:** Works - implemented; not run against the real editor or player. The software backend skips user and fallback-font textures and samples the atlas without filtering
- **Open:** the behaviour `okay::imui` had that OkayUI lacks is listed under "Not done" in `docs/ui/own_ui_toolkit.md` (window z-order, key repeat, `BeginGroup`, label hiding); the ranked list of Dear ImGui calls still uncovered is in the same page
- **Related:** 40.1, 40.3, 15.1, 17.1
- **Words:** headless UI test, software UI renderer, golden image, okayui_soft, okayui_behaviour, okayui_golden, okayui_fuzz, OKAY_BUILD_OKAYUI_TESTS, Sim, ResetState, UI fuzz, replace Dear ImGui
- **Seen as:** a UI golden test differs by a few pixels; typing in a text field drops a character; a window cannot be dragged in the demo; the okayui test targets are missing from a build
- **Ask me like:** "work on 40.6.3, add a test for the table widget"; "run the OkayUI fuzz test under the sanitizers"

### 40.7 Draw list, cursor, item state and disabled blocks
- **System:** OkayUI
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
- **Docs:** `docs/ui/own_ui_toolkit.md` (section 4)
- **Tests:** `okayui_drawlist`, `okayui_fuzz`
- **Status:** Works - implemented and tested headless (pixels, goldens, fuzz); not run in the real editor or launcher. Concave polygons, rounded-corner masks, anti-aliasing, a printf-style text call and text with newlines are not covered
- **Open:** the remaining gaps are ranked in `docs/ui/own_ui_toolkit.md` section 4; the Direct3D 11 backend still ignores clip spans, so draw-list clipping does not work there
- **Related:** 40.1, 40.6
- **Words:** draw list, GetWindowDrawList, custom drawing, canvas, InvisibleButton, BeginDisabled, cursor position, content region, CalcTextSize, polyline, bezier, replace Dear ImGui
- **Seen as:** a custom-drawn panel does not clip; a disabled widget still reacts; a translucent line is darker at its corners
- **Ask me like:** "work on 40.7.1, add AddConcavePolyFilled"; "render the draw list lab page to a PNG"

### 40.8 Porting helpers: tooltip scope, style fields, mapping layer, list clipper
- **System:** OkayUI
- **What:** four things code ported from Dear ImGui needs: a tooltip that takes widgets (`BeginTooltip`/`EndTooltip`), `GetStyle()` with the style fields ported code reads, a mapping layer for vector and colour types and flag enums, and `ListClipper` for long lists that are not tables. Library only; nothing in the editor or launcher calls them yet.
- **Parts:**
  - 40.8.1 Tooltip scope - `okayui/src/OkayUI.cpp` - widgets laid out in the overlay, box sized to them, flips at the display edge, input off, surrounding layout untouched
  - 40.8.2 Style fields - `okayui/src/OkayUI.cpp` - `GetStyle()` returns a read-only `StyleInfo` (spacing, padding, rounding, `Colors[]`) refreshed from the live settings
  - 40.8.3 Mapping layer - `okayui/include/okay/UI/OkayUIMap.hpp`, `shared/OkayUIImGuiMap.hpp` - `FromVec4`, `ToVec4`, `FromColor`, `FlagMap`, `MapFlags`, `MapEnum`; ImGui window flag, colour and style variable tables on the host side
  - 40.8.4 List clipper - `okayui/src/OkayUI.cpp` - `ListClipper::Begin/Step/End/ForceDisplayRangeByIndices`: only visible rows are laid out, same pixels as the full list
- **Code:** `okayui/include/okay/UI/OkayUICompat.hpp`, `okayui/include/okay/UI/OkayUIMap.hpp`, `okayui/src/OkayUI.cpp`, `shared/OkayUIImGuiMap.hpp`, `okayui/tests/test_okayui_scopes.cpp`
- **API:** `OkayUI::BeginTooltip`, `OkayUI::EndTooltip`, `OkayUI::GetStyle`, `OkayUI::StyleInfo`, `OkayUI::ListClipper`, `OkayUI::MapFlags`, `OkayUI::FromVec4`
- **Editor:** none (library only)
- **Docs:** `docs/ui/own_ui_toolkit.md` (section 6.7)
- **Tests:** `okayui_scopes`
- **Status:** Works - implemented and tested headless; not run in the real editor or launcher. `GetStyle` is read-only; the ImGui tables file is not built in CI
- **Open:** assigning to the style is not supported; no scrolling child inside a tooltip; the tab colour slots are not in the ImGui table; ports that use these (Audio Mixer tooltips, Crash Log rows) are not switched over
- **Related:** 40.7
- **Words:** tooltip, BeginTooltip, GetStyle, ImVec4, flag mapping, ListClipper, clipper, long list, replace Dear ImGui
- **Seen as:** a ported tooltip with several lines draws nothing; a ported panel does not compile because of GetStyle; a 10,000-row list is slow
- **Ask me like:** "work on 40.8.4, make the clipper handle rows of different height"


### 40.9 Ported-code calls: window, frame, context, style presets and child flags
- **System:** OkayUI
- **What:** the calls and names that editor and launcher code written for Dear ImGui still needed after 40.8: collapse, background alpha and position of windows, raising a window, a modal check, a keyboard-capture override, item flags, multi-item widths, context shims, the Dark / Light / Classic colour presets, the dock layout as text, regular polygons and textured quads and rounded images on the draw list, mouse button, direction, hover, focus and child flag names, and `BeginChild` with flags (a bordered or content-sized child). Library only; nothing in the editor or launcher calls them yet.
- **Parts:**
  - 40.9.1 Window calls - `okayui/src/OkayUI.cpp` - `SetNextWindowCollapsed`, `SetNextWindowBgAlpha`, `SetWindowPos` (by title or current), `BringWindowToDisplayFront`, `FindWindowByID`, `GetTopMostPopupModal`, `SetNextFrameWantCaptureKeyboard`, `GetTreeNodeToLabelSpacing`
  - 40.9.2 Small helpers - `okayui/src/OkayUICompat.cpp` - `GetCursorStartPos`, `GetWindowViewport`, `PushItemFlag`, `PushMultiItemsWidths`, `FindRenderedTextEnd`, context shims, `StyleColorsDark/Light/Classic`, `SaveIniSettingsToMemory`, `LoadIniSettingsFromMemory`
  - 40.9.3 Draw list - `okayui/src/OkayUI.cpp` - `AddNgon`, `AddNgonFilled`, `AddImageQuad`, `AddImageRounded`, `DrawFlags`
  - 40.9.5 Type names, flag aliases, password and no-blank input, closed polylines, `##id` labels hidden - `OkayUI.hpp`, `OkayUICompat.hpp`, `OkayUI.cpp` - `ImVec4`, `ImRect`, `ImTextureID`, `ImGuiID`, `TableColumn_*`, `TabItem_*`, `InputText_Password`, `InputText_CharsNoBlank`, `DrawFlags_Closed`
  - 40.9.6 Sized buttons, `SameLine(offset, spacing)`, printf-style `Text` family, `BeginPopupContextItem()` - `OkayUI.cpp`, `OkayUICompat.hpp`
  - 40.9.7 Port probe and spellings header - `scripts/okayui_port_probe.sh`, `okayui/probe/imgui.h`, `okayui/include/okay/UI/OkayUIImGuiNames.hpp` (generated) - Dear ImGui spellings of types, flags and enumerators, `namespace ImGui = OkayUI`, `DrawListHandle`, `ImGuiStyle`
  - 40.9.8 Panels run headless and accessor layer - `okayui/tests/test_okayui_port_*.cpp`, `OkayUIInternals.hpp` (`GImGui`, `ImGuiContext`, `ImGuiWindow` values), `NewFrame`, `GetPlatformIO`, popup `MenuItem` / `BeginMenu`
  - 40.9.10 Dock-node view, window table, draw-list channels and host shapes - `okayui/src/OkayUIDockNodes.cpp`, `OkayUIInternals.hpp` (`DockNodeRef`, `WindowRef` table), `DrawList::Channels*`, `ImDrawData` bridge, `okayui/tests/test_okayui_{docknodes,channels,host_shapes}.cpp`, `docs/ui/own_ui_toolkit.md` 6.20 to 6.22
  - 40.9.13 Scene pictures and dialogs - `editor/SceneViewCore.hpp`, `editor/DialogService.hpp`, `ConsoleSetMirror`, test `okayui_sceneview` (window mode, its views, dialogs, Hierarchy and Inspector copies and tests were deleted on 2026-11-27: the whole editor is on OkayUI; `docs/ui/own_ui_toolkit.md` 33)
  - 40.9.12 The editor's game step and dock-aware windows - `editor/EditorFrameTick.hpp`, dock-aware `Begin` (`SetDockAwareBegin`), tests `okayui_{frametick,dockaware}`, `docs/ui/own_ui_toolkit.md` 6.24
  - 40.9.11 Real fonts and programs that OkayUI owns - `AddFontTtf` / `PushFont` multi-font store (`OkayUICompat.cpp`), `shared/OkayUIFrameLoop.hpp`, tests `okayui_{fonts,frameloop}`, `docs/ui/own_ui_toolkit.md` 6.23
  - 40.9.9 Platform-event host - `shared/OkayUIHost.hpp` (`okayuihost::platform`: `EventCollector`, `Host`), `docs/ui/own_ui_toolkit.md` 6.18
  - 40.9.4 Names and child flags - `okayui/include/okay/UI/OkayUICompat.hpp` - `MouseButton_*`, `Dir_*`, `Hovered_*`, `Focused_*`, `Child_*`, `KeyId_UpArrow` and friends, `IsItemHovered(flags)`, `IsWindowHovered(flags)`, `IsWindowFocused(flags)`, `BeginChild(id, size, childFlags)`
- **Code:** `okayui/include/okay/UI/OkayUICompat.hpp`, `okayui/include/okay/UI/OkayUI.hpp`, `okayui/src/OkayUI.cpp`, `okayui/src/OkayUICompat.cpp`, `okayui/tests/test_okayui_calls.cpp`, `okayui/tests/test_okayui_names.cpp`
- **API:** `OkayUI::SetNextWindowBgAlpha`, `OkayUI::SetNextWindowCollapsed`, `OkayUI::PushMultiItemsWidths`, `OkayUI::StyleColorsLight`, `OkayUI::DrawList::AddImageRounded`, `OkayUI::BeginChild`
- **Editor:** none (library only)
- **Docs:** `docs/ui/own_ui_toolkit.md` (section 6.10)
- **Tests:** `okayui_calls`, `okayui_names`, `okayui_port_console`, `okayui_port_project`, `okayui_port_versions`, `okayui_port_project2`, `okayui_port_scenemanager`, `okayui_host_platform`
- **Status:** Works - implemented and tested headless (golden images for the three colour presets); not run in the real editor or launcher. Textured draw-list calls are checked on the draw data only (the software backend skips textures)
- **Open:** `AddImageRounded` rounds corners only for images; `AddRect` has no per-corner flags and `AddPolyline` no `Closed` flag; hover delays are accepted and ignored; Middle mouse never fires; `Child_AutoResizeX` and resizable children are accepted and ignored; `NewFrame`, the platform and viewport calls and the dock node internals are not offered
- **Related:** 40.8
- **Words:** SetNextWindowBgAlpha, SetNextWindowCollapsed, PushMultiItemsWidths, StyleColorsLight, AddNgon, AddImageRounded, ChildFlags, HoveredFlags, MouseButton, context, replace Dear ImGui
- **Seen as:** a ported panel does not compile because of a missing call name; a child window with a content-sized height is the wrong size; a light theme call has no effect
- **Ask me like:** "work on 40.9.4, add Child_AutoResizeX"


### 40.10 The Inspector on OkayUI (preview, on by default)
- **System:** OkayUI
- **What:** the editor's Inspector drawn by our own toolkit instead of Dear ImGui: the object header card, the component bars and about a hundred component bodies, described once as "field rows" and drawn by OkayUI's screens. On by default since 2026-11-23 (Settings > Experimental items "OkayUI screens" and "OkayUI Inspector"; untick either to go back to Dear ImGui); every body without an up-to-date OkayUI version stays Dear ImGui's.
- **Parts:**
  - 40.10.1 Field rows - `shared/InspectorFieldsModel.hpp`, `shared/InspectorPanelModel.hpp` - one row list per body (drag, slider, check box, combo, colour, asset slot, gradient, curve, mask, list, note, button), no toolkit or engine types
  - 40.10.2 OkayUI views - `okayui/screens/OkayUIEditorPanels.cpp` - header card, component bar with its menu, rows, tooltips drawn last, sub-menu lists inside popups
  - 40.10.4 Editor hosting - `editor/main.cpp` (`UseOkayUIInspector`, the "Inspector's OkayUI parts" block before `DrawInspector`), `editor/OkayPanels.cpp`, `editor/InspectorPanel.cpp` (generic card), `editor/NavigationWindow.cpp`, `editor/BehaviorTreeWindow.cpp`, `editor/FpHandsWindow.cpp`, `editor/LevelTools.cpp`
  - 40.10.5 The switch - `shared/OkayUIBackend.hpp` (`InspectorOn`), `shared/OwnSwitches.hpp` (Settings > Experimental item "OkayUI Inspector", `OKAY_UI_INSPECTOR=1`)
  - 40.10.6 Toolkit pieces it needs - `okayui/src/OkayUI.cpp` - framed widgets (`StyleVar_FramedWidgets`, framed number fields through `FramedNumbersOn`), text rows (`StyleVar_TextRowHeight`), popup padding and width, combo list placement, 32 popup slots, Dear ImGui wrapping, `SearchField`, middle button, reorderable tabs, hideable table columns, `BeginChild(id, size, flags)` with padding and outline
- **Code:** `shared/InspectorFieldsModel.hpp`, `shared/InspectorPanelModel.hpp`, `okayui/screens/OkayUIEditorPanels.cpp`, `editor/OkayPanels.cpp`, `editor/main.cpp`, `shared/OkayUIBackend.hpp`, `shared/OwnSwitches.hpp`
- **API:** `okaypanels::InspFieldList`, `oked::DrawCompHeaderOkayUI`, `UseOkayUIInspector`, `okayuibackend::InspectorOn`
- **Editor:** on by default since 2026-11-23: Settings > Experimental "OkayUI screens" and "OkayUI Inspector" both start ticked (untick either and restart to go back; `OKAY_UI_BACKEND=imgui` also opts out). Also on with `OKAY_DEV=1` and `OKAY_UI_BACKEND=okayui`
- **Docs:** `docs/ui/own_ui_toolkit.md` (sections 14 to 23)
- **Tests:** `okayui_inspector`, `okayui_editor_panels`, `okayui_hierarchy`, `okayui_imcompat`, `ui_backend`, `own_switches`
- **Status:** Partial - built and tested headless; run under Xvfb on Linux only, never on Windows. Bodies whose Dear ImGui version changed on main after the port (Camera, Light, Mesh Renderer, Particle System, Water, Modifiers, Cinemachine Brain, First-person, Third-person and View toggle settings) stay Dear ImGui until their rows are brought up to date
- **Open:** bring the stale bodies above up to date; the Hierarchy port is in the library and its tests but not wired into the editor (main's panel changed since, and it has no editor host); no Windows run. The Crash Log, Audio Mixer, History, Problems, Stats and Profiler windows are drawn by OkayUI only, straight into their windows (`docs/ui/own_ui_toolkit.md` sections 24 and 25), and so are Project Settings' Player, Physics, Rendering, Tags and Layers and Loading screen sections, Lighting's Post-processing and the Project panel's import settings (sections 26 and 27), Keyboard Shortcuts, Net, Find in Files, Data Asset and the Plugin Manager (section 28), and Map Humanoid Bones, the Save Manager, Material, Frame time and the Input panel (section 29), and Timers and Input Actions (section 30). Since 2026-11-27 the editor has no Dear ImGui at all (section 33), so the Dear ImGui-API bodies are drawn by OkayUI too: each body that has an OkayUI-screen version is drawn twice over (two OkayUI bodies), one to be deleted per body
- **Related:** 40.4, 40.9, 15
- **Words:** Inspector, OkayUI Inspector, field rows, component body, replace Dear ImGui, preview, Experimental, OKAY_UI_INSPECTOR
- **Seen as:** the Inspector looks the same with OkayUI on; a component body is still Dear ImGui with OkayUI on; typing in an OkayUI field triggers an editor shortcut
- **Ask me like:** "work on 40.10, bring the Light body's OkayUI rows up to date"


### 40.12 The editor on OkayUI only (no Dear ImGui)
- **System:** OkayUI
- **What:** the editor's own sources, written in Dear ImGui's names, compiled only against OkayUI's Dear ImGui layer (`okayui/probe` first on the include path), so `OkayEngine(.exe)` has no Dear ImGui in it. Built as a second program on 2026-11-27 and made THE editor the same day (round 3); Dear ImGui left the repository.
- **Parts:**
  - 40.12.1 The program - `editor/CMakeLists.txt` (target `okay-editor`, `bin/OkayEngine`), `okayui/probe/imgui.h`, `okayui/probe/ImGuiPainterRenderer.hpp`, `shared/ImGuiPlatformFeed.hpp` (input, the input method's caret)
  - 40.12.2 Gaps closed for it - `okayui/src/OkayUI_Windows.cpp` (menu bar row, docking by `ConfigFlags_DockingEnable`, auto-fit axes, regions inside windows, focus of docked tabs, `IsWindowHovered` under menus and popups), `okayui/src/OkayUI_Menus.cpp` (modals outside windows, menu items as last item, popup queries, menus as popup windows of any widgets, Dear ImGui's Selectable rows, modals with a given height), `okayui/src/OkayUI.cpp` (`###` ids, clipped children in popups, `ColorEdit_HDR` and Dear ImGui's ColorEdit cells, plain sliders and drags framed, `SameLine(x)`, window stacking past 256 draw spans, top-level ids), `okayui/src/OkayUI_Trees.cpp` (Dear ImGui's tree rows), `okayui/src/OkayUI_Tabs.cpp` (nested tab bars), `okayui/src/OkayUI_DragDrop.cpp` (drag previews as tooltips), `okayui/src/OkayUI_DrawList.cpp` (the dock host's list under floating windows, line breaks, `CalcTextSizeA`), `okayui/src/OkayUICompat.cpp` (`MenuItem(bool*)`), `okayui/src/OkayUIDock.cpp` (`DockAdoptTitle`), `okayui/probe/imgui.h` (Dear ImGui's one-argument `SameLine`), `editor/ModalGuard.hpp`
- **Code:** `editor/CMakeLists.txt`, `okayui/src/OkayUI_Windows.cpp`, `okayui/src/OkayUI_Menus.cpp`, `editor/ModalGuard.hpp`, `shared/ImGuiPlatformFeed.hpp`
- **API:** `OkayUI::ClosePopup`, `OkayUI::ListOpenPopups`, `OkayUI::CurrentPopupDepth`, `OkayUI::DockAdoptTitle`, `OkayUI::DockLayout::Rename`, `OkayUI::ColorEdit_HDR`, `OkayUI::GetDockHostDrawList`, `OkayUI::MenuItem(label, shortcut, bool*)`, `OkayUI::Selectable_DontClosePopups`, `OkayUI::GetImeRect`
- **Editor:** the editor itself (`OkayEngine.exe`); panels that Dear ImGui could pull out into their own OS window are floating windows inside the main window
- **Docs:** `docs/ui/own_ui_toolkit.md` (sections 6.26, 32 and 33)
- **Tests:** `editor`, `okayui_port_modalguard`, `okayui_windows`, `okayui_widgets`, `okayui_dockaware`, `okayui_color`, `imgui_platform_feed`
- **Status:** Partial - builds and passes its self-test on Linux; run headless under Xvfb (round 2's checklist and round 3's drags, file drops and input-method text in `docs/ui/own_ui_toolkit.md` sections 32 and 33); never built with MinGW or run on Windows (changed files syntax-checked only)
- **Open:** more than one OS window (multi-window panels, `docs/status/UNFINISHED.md`); a real input method was not run (only its committed text and caret); the second copies of the Inspector bodies (40.10)
- **Related:** 40.9, 40.10, 40.11
- **Words:** editor on OkayUI, OkayEngineOkayUI (the old second program), OKAY_EDITOR_ON_OKAYUI (deleted), replace Dear ImGui, move off ImGui, no Dear ImGui
- **Seen as:** the editor shows panels floating instead of docked; a modal does not show; the menu bar is missing; a section draws outside its window; an input method's candidate list is in the wrong place
- **Ask me like:** "work on 40.12, give OkayUI a second OS window"
### 40.11 The launcher on OkayUI only (no Dear ImGui)
- **System:** OkayUI
- **What:** the launcher (`OkaySpace.exe`) is drawn by OkayUI alone since 2026-11-27: its pages, written in Dear ImGui's names, compile against OkayUI's compatibility layer, and Dear ImGui is no longer built into it. The chat pop-out is OkayUI's second context in its own window; What's New, Tips and the Browser page are OkayUI screens drawn in place.
- **Parts:**
  - 40.11.1 Several contexts - `okayui/src/OkayUI_Context.cpp`, `okayui/src/OkayUI_ContextState.hpp` - CreateContext / SetCurrentContext / DestroyContext; each file lists its per-context variables (`OKAYUI_CONTEXT_STATE`), switching exchanges them
  - 40.11.2 Dear ImGui's rules for a whole program - `okayui/src/OkayUI.cpp`, `okayui/src/OkayUI_Windows.cpp` - `UseImGuiCoordinates` (cursor from the window corner, window size of the innermost child), text rows (`StyleVar_TextRowHeight` < 0), child fill and `ChildRounding`, `GetStyle()` writes and `FontGlobalScale` taken in, wrapped `CalcTextSize` / `AddText`, `TextLink`, `ScrollTextBoxToEnd`
  - 40.11.3 The launcher's host - `launcher/main.cpp` (frame start: framed widgets and text rows pushed; `ChatHostFrame`), `launcher/CMakeLists.txt` (`okayui/probe` first on the include path), `okayui/probe/ImGuiPainterRenderer.hpp`, `shared/ImGuiPlatformFeed.hpp`
- **Code:** `okayui/src/OkayUI_Context.cpp`, `okayui/src/OkayUI_ContextState.hpp`, `okayui/src/OkayUIText.cpp`, `launcher/main.cpp`, `launcher/CMakeLists.txt`
- **API:** `OkayUI::CreateContext`, `OkayUI::SetCurrentContext`, `OkayUI::UseImGuiCoordinates`, `OkayUI::TextLink`, `OkayUI::ScrollTextBoxToEnd`
- **Docs:** `docs/ui/own_ui_toolkit.md` (section 31)
- **Tests:** `okayui_contexts`, `okayui_calls`, `launcher_page_layout`, `launcher_theme`
- **Status:** Works - built and run headless on Linux under Xvfb (every page, the chat window, scripted clicks and typing, the design picker); not built or run on Windows.
- **Open:** run on Windows; keyboard navigation of buttons (Dear ImGui's NavEnableKeyboard was off in the launcher, so nothing is lost); small drawing differences (slider, combo arrow); the editor is the editor agent's.
- **Related:** 40.9, 17.1
- **Words:** launcher, OkayUI, no Dear ImGui, contexts, chat window, compatibility layer, replace Dear ImGui
- **Seen as:** a launcher page looks different from before; the chat window does not take clicks; text in a settings row runs under its control
- **Ask me like:** "work on 40.11, the launcher's combo arrow looks different"

