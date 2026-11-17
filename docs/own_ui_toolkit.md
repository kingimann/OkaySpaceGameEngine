# Our own UI toolkit: OkayUI replaces Dear ImGui

Part of the plan in `docs/own_code_roadmap.md` (Dear ImGui row). Section 4 holds the current HAVE and GAP table by feature family and what each gap-closing step built (sections 4.2 to 4.35); sections 1 and 3 are the earlier per-function audit (rows closed since are marked "Done in step 1").

**Decision (owner, approved): OkayUI (`okayui/`) is THE toolkit.** It slowly replaces Dear ImGui. The headless pieces that were first written as a separate library (`okay::imui`, branch `claude/own-imui-core`: a software rasteriser, a golden-image and synthetic-input test harness, behaviour tests, a fuzz test) are folded into OkayUI instead of living beside it as a second toolkit. There is exactly one own toolkit. The editor and launcher still use Dear ImGui and no ImGui call is changed by this work.

This page has: the decision and the OkayUI audit (this section), the audit of which ImGui calls our code makes (section 1), how the headless harness works (section 2), and what OkayUI still lacks (section 3). Sections 2 and 3 are rewritten as the fold-in lands; see the status line of each.

## 0. OkayUI audit (what was found before folding anything in)

Read from `okayui/` (headers, sources, backends), `tests/test_okayui.cpp` and every call site that builds an `OkayUI::Input`.

### Shape

| Piece | Size | What |
|---|---|---|
| `okayui/include/okay/UI/OkayUI.hpp` | 438 lines | The whole public API: free functions in `namespace OkayUI`, `Input`, `Theme`, `DrawData`, `Font` |
| `okayui/src/OkayUI.cpp` | 2656 lines | Widgets, layout, windows, the geometry batch |
| `okayui/src/OkayUI_FontAtlas.*` | 206 lines | The launcher look's Roboto atlas, baked with `stb_truetype` (compiled into the engine) |
| `okayui/src/OkayUI_D3D11.cpp`, `d3d11_demo/` | 291 lines | Direct3D 11 backend and a standalone demo (Windows only) |
| `tests/test_okayui.cpp` | 911 lines | One test file, not a small one: drives an SDL software renderer on an in-memory surface and reads pixels back. Covers press and release, hover, the ImGui-capture gate, checkbox, slider, radio, tabs, text field, windows (collapse, close), scrolling children with clipping, combo, drag, colour picker, tables, menus, popups, plots, drag and drop, the launcher look. |

Style: one global singleton (single UI context, state in file-scope fixed arrays), no heap and no STL in the toolkit's own code (the header says so; the `.cpp` still includes `<algorithm>`), integer widget ids hashed (FNV-1a) from labels plus an id stack, explicit-coordinate widgets plus an ImGui-style auto-layout layer on top (`Begin`/`End`, `SameLine`, `BeginChild`, `Columns`, `BeginTable`, menus, popups, trees, drag and drop). About 70 widgets. Widget interaction is hot/active (press arms on the inside, fires on release inside).

### How it renders

- Every widget appends to one fixed geometry batch: `SDL_Vertex` arrays (262,144 vertices, 393,216 32-bit indices since gap step 1; they were 20,000 and 30,000) plus a second "overlay" batch (combo lists, tooltips, drag chip, toasts) that is appended after the main one. Quads, triangle fans for circles, rounded rectangles built from bars and corner fans. Nothing is anti-aliased.
- The batch is split into **spans**: an index offset plus a clip rectangle on/off plus a texture. `BeginChild`, the launcher-look window frame and `PushClipRect` create spans; images and fallback-font text switch the texture.
- **SDL_Renderer backend** (`EndFrame(SDL_Renderer*)`): one `SDL_RenderGeometry` call per span under that span's clip rectangle. This is the path the player and editor use, on top of Dear ImGui's own SDL_Renderer.
- **Direct3D 11 backend** (`EndFrameData` + `GetDrawData` + `D3D11Backend`): takes only the vertex and index arrays. **It ignores spans**, so scroll-region clipping, the TrueType atlas texture (the launcher look's text and its flat shapes all sample that texture) and images are not available on that path.
- Text: classic look draws one quad per lit pixel of the engine's 8x8 bitmap font (scale 2 gives 16 px glyphs); launcher look draws one textured quad per glyph from the Roboto atlas (up to six baked sizes, one RGBA texture shared with a solid white patch so shapes and text share one draw call); characters the atlas lacks go through an engine `TtfFont` with its own texture.
- So the draw data is already a vertex, index and command (span) list, but it is only reachable through `SDL_Vertex`, `SDL_Rect` and `SDL_Texture*`, and the public `DrawData` leaves the spans out.

### Input

`OkayUI::Input` is already a plain struct: mouse position, left and right button state, wheel, typed UTF-8 text, a `backspace` edge, and `blocked` (another layer owns the mouse). Edges are found by comparing with the previous frame. It has **no** keys other than backspace, no modifiers, no middle button, no time step. The editor (`editor/main.cpp`), the player (`player/main.cpp`) and `shared/` build it with member assignment, so adding fields with defaults at the end is source compatible.

### Findings that matter for the fold-in

1. A headless run is possible without a window: OkayUI needs only the SDL headers and `SDL_sinf` style maths, plus the engine's 8x8 font and, for the launcher look, the atlas code. It does not need a video device. It is not "no SDL" like `okay::imui` was, but linking SDL for a test is already the case for `test_okayui`.
2. There is no backend-neutral command list in the public API (see D3D11 above): added in step 2 without changing what the SDL backend draws.
3. Single global context: tests must reset state between cases. There was no reset call; one is added.
4. `TextField` (and so `InputText`) appends raw bytes and Backspace removes one **byte**, so a multi-byte character is corrupted by one Backspace; control characters and invalid UTF-8 are accepted; there is no caret, selection, Left/Right/Home/End/Delete/Enter or mouse placement.
5. An open `Combo` list is drawn on top but does not block input: a click on a list row also reaches a widget under it, and a click outside the list that closes it also reaches whatever it lands on.
6. Windows have a title-bar drag, collapse caret and close button, but no resize grip, no z-order, and overlapping windows both receive the same click.
7. No way to ask for a widget's rectangle, hover or whether a text field wants the keyboard (ImGui `GetItemRectMin/Max` 144 call sites, `IsItemHovered` 1502, `io.WantTextInput` 19).
8. Time dependent drawing (`SDL_GetTicks`) exists only in toasts and the spinner; the text caret blink counts frames, so it is deterministic.

## 0.1 Relation to `okay::imui` (what was folded in)

`okay::imui` (branch `claude/own-imui-core`, draft PR #653) was a from-scratch headless core written without building on OkayUI, so the two overlapped. It had a stricter test design for the same job. This is what happened to it:

| `okay::imui` piece | Now |
|---|---|
| Triangle rasteriser (`ImuiSoft`) | `okayui/soft/OkayUI_Soft.*`, drawing `OkayUI::DrawData`; both bug fixes kept |
| Test kit (frame driver, PNG reader, golden compare) | `okayui/tests/OkayUITestKit.hpp` |
| `imui_widgets` behaviour tests | `okayui/tests/test_okayui_behaviour.cpp`, ported to the OkayUI widgets that have an equivalent |
| `imui_core` rasteriser tests | `okayui/tests/test_okayui_soft.cpp` |
| `imui_golden` and the demo | `okayui/tests/test_okayui_golden.cpp`, `okayui/demo/` |
| `imui_fuzz` | `okayui/tests/test_okayui_fuzz.cpp` (sanitizers) |
| Widgets, layout, context, ID stack, window model, font atlas, draw list (`Imui.hpp`, `Imui.cpp`, `ImuiDraw.cpp`), and the `imui_core` tests of those | Deleted: OkayUI already has the widgets, and has about 70 of them |
| `OKAY_BUILD_IMUI` | `OKAY_BUILD_OKAYUI_TESTS` |

## 1. Audit: which ImGui APIs we call

Scope: every `.cpp`, `.hpp` and `.h` under `editor/`, `launcher/` and `shared/` that mentions ImGui (60 files, not counting the downloaded ImGui sources and the embedded font data). Method, so it can be repeated:

```
grep -rhoE "ImGui::[A-Za-z0-9_]+" <those files> | sort | uniq -c | sort -rn
```

Totals: 24,862 call sites to 259 distinct `ImGui::` functions. The 75 most used cover 22,527 of them (90.6 percent), so the long tail is small. The numbers count call sites in source, not calls per frame.

How much of that OkayUI covers today, counted over the 75 most used functions: 33 of 75 are done, 16 partly done, 26 missing. Weighted by call sites (22,527 in these 75): 50.0 percent done, 30.6 percent partly done (for example `IsItemHovered` without flags, `InputText` without hints or multi-line), 19.3 percent missing. The biggest missing blocks by call sites: `GetColorU32` (577), the draw list for custom drawing (`GetWindowDrawList` 365 plus its roughly 2,000 `AddRect`, `AddLine`, `AddText` calls below), cursor access (`GetCursorScreenPos` 346, `SetCursorScreenPos` 290, `GetContentRegionAvail` 323), disabled blocks (351), `PushStyleVar`/`PopStyleVar` (375), `InvisibleButton` (158), `IsMouseClicked`, `IsItemActive`, `GetFrameHeight`, `GetTextLineHeight`. The ranks 76 and below were not counted against OkayUI.

### Ranked list (top 75)

"Done" means OkayUI has an equivalent with the same job (the name may differ). "Partial" says what is left out. Counted from `okayui/include/okay/UI/OkayUI.hpp`.

| Rank | Function | Call sites | OkayUI |
|---|---|---|---|
| 1 | `SameLine` | 1737 | Done (`SameLine(spacing)`) |
| 2 | `IsItemHovered` | 1502 | Partial (`IsItemHovered()`, no flags) |
| 3 | `SetTooltip` | 1438 | Partial (`Tooltip(text)` after the widget, no delay or flags) |
| 4 | `TextDisabled` | 1292 | Done |
| 5 | `DragFloat` | 1010 | Partial (one float, speed and clamp; no format string, no typing a value) |
| 6 | `SmallButton` | 727 | Done |
| 7 | `MenuItem` | 722 | Partial (label only: no shortcut text, no checked or enabled state) |
| 8 | `Checkbox` | 713 | Done (the label is clickable) |
| 9 | `Button` | 704 | Done (auto size; no size argument) |
| 10 | `SetNextItemWidth` | 668 | Done (also `PushItemWidth`) |
| 11 | `GetColorU32` | 577 | Done in step 1 (`GetColorU32(Col, alpha)`, `RGBA`, `ColorMulAlpha`; section 4) |
| 12 | `Dummy` | 535 | Done |
| 13 | `PopID` | 393 | Done |
| 14 | `PushStyleColor` | 392 | Partial (8 theme colours (`Col_*`), not the full ImGuiCol set) |
| 15 | `GetWindowDrawList` | 365 | Done in step 1 (`GetWindowDrawList()` and the `AddXxx` and path calls; section 4) |
| 16 | `PushID` | 361 | Done (int and string) |
| 17 | `GetCursorScreenPos` | 346 | Done in step 1 |
| 18 | `IsKeyPressed` | 343 | Partial (nine key edges in `Input`) |
| 19 | `Separator` | 326 | Done |
| 20 | `GetContentRegionAvail` | 323 | Done in step 1 |
| 21 | `CalcTextSize` | 321 | Done in step 1 (`CalcTextSize` returns width and line height) |
| 22 | `PopStyleColor` | 298 | Done |
| 23 | `TextUnformatted` | 294 | Done (`Text`) |
| 24 | `SetCursorScreenPos` | 290 | Done in step 1 (one-shot placement of the next item) |
| 25 | `TextColored` | 280 | Done (RGB bytes) |
| 26 | `CloseCurrentPopup` | 272 | Done |
| 27 | `InputText` | 248 | Partial (single line: caret, selection, UTF-8, Enter, read-only, select-all; no hint, multi-line, password, callbacks, clipboard) |
| 28 | `EndPopup` | 242 | Done |
| 29 | `Selectable` | 223 | Done (selected flag; no size or flags) |
| 30 | `PushStyleVar` | 220 | Missing (sizes live in `Theme`; no scoped override) |
| 31 | `SliderFloat` | 217 | Done (no format string, no typing a value) |
| 32 | `OpenPopup` | 211 | Done (opens at the cursor) |
| 33 | `Combo` | 210 | Done (item array) |
| 34 | `InputTextWithHint` | 204 | Missing |
| 35 | `EndChild` | 201 | Done |
| 36 | `BeginChild` | 198 | Partial (size and border; no flags) |
| 37 | `GetIO` | 182 | Partial (`Input` struct and `WantsTextInput()`) |
| 38 | `GetTextLineHeight` | 180 | Done in step 1 |
| 39 | `EndDisabled` | 176 | Done in step 1 |
| 40 | `BeginDisabled` | 175 | Done in step 1 |
| 41 | `InvisibleButton` | 158 | Done in step 1 |
| 42 | `Spacing` | 157 | Done |
| 43 | `PopStyleVar` | 155 | Missing |
| 44 | `Text` | 153 | Done (no printf formatting: pass the formatted string) |
| 45 | `TextWrapped` | 150 | Done |
| 46 | `SetWindowFontScale` | 148 | Partial (`PushTextScale`) |
| 47 | `ColorEdit4` | 137 | Missing (`ColorEdit3` only (no alpha)) |
| 48 | `DragInt` | 125 | Done |
| 49 | `GetFontSize` | 124 | Done in step 1 |
| 50 | `End` | 120 | Done |
| 51 | `BeginPopup` | 113 | Done (mouse-modal while open) |
| 52 | `DragFloat2` | 95 | Missing (single value only) |
| 53 | `EndMenu` | 94 | Done |
| 54 | `BeginMenu` | 94 | Done (menu bar menus; no nested sub-menus) |
| 55 | `SetMouseCursor` | 91 | Missing |
| 56 | `GetFont` | 84 | Partial (`GetFont()` is the bitmap font, not a TrueType handle) |
| 57 | `EndCombo` | 83 | Partial (`Combo` only, no Begin/End form) |
| 58 | `BeginCombo` | 83 | Partial (`Combo` only, no Begin/End form) |
| 59 | `BeginPopupModal` | 81 | Missing (popups block the mouse below them but there is no modal API or dimming) |
| 60 | `SetCursorPosX` | 79 | Done in step 1 |
| 61 | `GetStyle` | 79 | Partial (`Style()` returns `Theme`) |
| 62 | `IsItemActivated` | 78 | Done in step 1 |
| 63 | `SetNextWindowSize` | 77 | Done (size is a `Begin` argument; `ResizableNextWindow` adds a grip) |
| 64 | `PushTextWrapPos` | 76 | Missing (`TextWrapped` only) |
| 65 | `PopTextWrapPos` | 76 | Missing |
| 66 | `GetFrameHeight` | 76 | Done in step 1 |
| 67 | `GetCursorPosX` | 76 | Done in step 1 |
| 68 | `GetItemRectMin` | 73 | Done (`GetItemRect`) |
| 69 | `SetNextWindowPos` | 71 | Done (position is a `Begin` argument; `PinNextWindow`) |
| 70 | `GetItemRectMax` | 71 | Done (`GetItemRect`) |
| 71 | `TableSetupColumn` | 70 | Partial (headers and weights in `BeginTable`, drag-resizable) |
| 72 | `DragFloat3` | 69 | Missing |
| 73 | `IsMouseClicked` | 66 | Done in step 1 (also `IsMouseDown`, `IsMouseReleased`) |
| 74 | `AlignTextToFramePadding` | 65 | Done (text rows are already frame height) |
| 75 | `IsItemActive` | 64 | Done in step 1 (also `IsItemActivated`, `IsItemDeactivated`, `IsItemClicked`) |

Ranks 76 to 160 (call sites): `GetMainViewport` 64, `Begin` 56, `TreePop` 54, `SetKeyboardFocusHere` 54, `IsMouseDown` 54, `SeparatorText` 52, `SetClipboardText` 50, `EndGroup` 47, `TableNextColumn` 46, `BeginGroup` 45, `IsWindowAppearing` 43, `IsMouseDragging` 42, `IsPopupOpen` 41, `GetStyleColorVec4` 39, `IsWindowHovered` 38, `IsWindowFocused` 38, `CollapsingHeader` 38, `IsMouseDoubleClicked` 37, `Unindent` 35, `IsItemClicked` 35, `GetTime` 35, `SliderInt` 34, `Indent` 33, `BulletText` 33, `BeginPopupContextItem` 33, `TableSetColumnIndex` 29, `IsMouseHoveringRect` 29, `AcceptDragDropPayload` 29, `TreeNodeEx` 28, `SetCursorPosY` 28, `ColorConvertU32ToFloat4` 28, `SetCursorPos` 27, `GetCursorPosY` 27, `TreeNode` 26, `InputInt` 26, `EndDragDropTarget` 26, `InputTextMultiline` 25, `GetWindowPos` 24, `TableNextRow` 23, `ProgressBar` 23, `ColorEdit3` 23, `IsItemDeactivated` 21, `GetFrameCount` 21, `EndTable` 21, `BeginTable` 21, `BeginDragDropTarget` 21, `EndTabItem` 20, `SetNextWindowSizeConstraints` 18, `IsMouseReleased` 18, `ArrowButton` 18, `IsItemDeactivatedAfterEdit` 17, `GetWindowSize` 17, `SetWindowFocus` 16, `SetScrollHereY` 15, `RadioButton` 15, `TableHeadersRow` 14, `SetNextItemOpen` 14, `SetDragDropPayload` 13, `IsKeyDown` 13, `GetWindowWidth` 13, `GetItemRectSize` 13, `EndTooltip` 13, `EndDragDropSource` 13, `BeginTooltip` 13, `BeginTabItem` 13, `BeginDragDropSource` 13, `Image` 12, `GetFrameHeightWithSpacing` 12, `GetContentRegionMax` 12, `CalcItemWidth` 12, `GetWindowContentRegionMax` 11, `GetTextLineHeightWithSpacing` 11, `GetID` 11, `GetCurrentContext` 11, `SetNextItemAllowOverlap` 10, `IsAnyItemActive` 9, `FindWindowByName` 9, `EndTabBar` 9, `BeginTabBar` 9, `SetItemDefaultFocus` 8, `NewLine` 8, `GetScrollY` 8, `PushFont` 7, `PopFont` 7, `GetScrollMaxY` 7.

The remaining 99 functions are used fewer than 7 times each.

### Other things the audit found (and where OkayUI stands)

- **Draw list.** `ImDrawList` is used directly a lot (the editor draws its own viewports, gizmos, node graphs and timelines). Call sites: `AddRectFilled` 471, `AddText` about 440, `AddLine` about 395, `AddCircleFilled` 286, `AddRect` 168, `AddTriangleFilled` 79, `AddCircle` 107, path building (`PathLineTo` 51, `PathStroke` 52), `PushClipRect` 34, `AddPolyline` 34, `AddImage` and `AddImageRounded` 32, `AddQuadFilled` 14, `AddRectFilledMultiColor` 13, `AddConvexPolyFilled` 8, `AddConcavePolyFilled` 5, `AddBezierCubic` 5. **OkayUI draws all of its own widgets through internal helpers (rectangle, rounded rectangle, line, triangle, circle fan, gradient quad, text, textured quad, clip rectangles) but exposes none of them as a public draw-list API.** Public today: `Label(x, y, text)`, `Panel`, `DrawTexture`, `Image`, `PushClipRect`/`PopClipRect`. Missing publicly: the whole `AddXxx` and path API, circle outlines, polylines, quads, bezier, concave polygons, anti-aliasing, callbacks. This is the largest single block for the editor's custom drawing.
- **Docking.** 12 distinct `Dock*` functions, 20 call sites (`DockSpace`, `DockBuilder*`, `IsWindowDocked`, node queries). Few call sites, but the editor layout depends on them and they use ImGui internals. OkayUI: nothing (windows float, drag by the title bar, collapse, close, and optionally resize).
- **Tables.** `BeginTable` 21, `TableSetupColumn` 70, `TableNextColumn` 46, `TableNextRow` 23, `TableSetColumnIndex` 29, `TableHeadersRow` 14, `TableSetupScrollFreeze` 4, `TableGetSortSpecs` 1. OkayUI: `BeginTable` with header row, starting column weights and drag-resizable columns; missing: sorting, scrolling and frozen rows, row background flags, `TableSetColumnIndex`.
- **Tabs.** `BeginTabBar` 9, `BeginTabItem` 13, `TabItemButton` 4. OkayUI: `TabBar(labels, count, &current)`; missing: tab items with content scopes, closable and reorderable tabs.
- **Drag and drop.** `BeginDragDropSource` 13, `BeginDragDropTarget` 21, a custom target 4. OkayUI: `DragSource(int)`/`DropTarget(int*)`; missing: typed payloads, preview tooltips.
- **Fonts.** `io.Fonts->AddFontDefault`, `AddFontFromMemoryCompressedBase85TTF` (Roboto and JetBrains Mono, embedded), `PushFont` 7, `SetWindowFontScale` 148. OkayUI: the engine's 8x8 bitmap font, a bold variant, the launcher look's Roboto atlas (six baked sizes) with fallback to engine TrueType fonts for missing characters, `PushTextScale`; missing: loading and pushing arbitrary fonts, a monospace code font.
- **Style.** `PushStyleColor` 392, `PushStyleVar` 220 (WindowPadding, FramePadding, FrameRounding, ChildRounding, ItemSpacing, WindowRounding, ButtonTextAlign, ScrollbarSize). OkayUI: `Theme` (mutable) and `PushStyleColor` for eight colours; no `PushStyleVar`.
- **Window flags used:** AlwaysAutoResize 72, NoScrollbar 47, NoSavedSettings 33, NoScrollWithMouse 29, NoResize 20, NoMove 18, NoTitleBar 12, NoCollapse, NoNav, NoDecoration, NoDocking, NoBackground, HorizontalScrollbar. OkayUI has no flags argument: every window has a title bar, a collapse caret and optional close button; `PinNextWindow` is NoMove, `ResizableNextWindow` is the opposite of NoResize; no auto-resize, no flags for the rest, no saved settings.
- **Input.** `io.MousePos` 120, `io.KeyCtrl` 70, `io.KeyShift` 66, `io.MouseDelta` 51, `io.MouseWheel` 32, `io.KeyAlt` 28, `io.WantTextInput` 19, `io.InputQueueCharacters` 10, `io.ConfigFlags` 9. OkayUI `Input`: mouse position, left and right buttons, wheel, typed text, and key edges for Backspace, Delete, Left, Right, Home, End, Enter, Escape, select-all, plus shift held; `WantsTextInput()`. Missing: ctrl and alt, middle button, mouse delta, key down state, about 30 other keys, clipboard (`SetClipboardText` 50), double click, drag queries, mouse cursor shapes (91).
- **Backends.** The editor and launcher use `ImGui_ImplSDL2` for events and `ImGui_ImplSDLRenderer2` for drawing. OkayUI draws through SDL_Renderer (the same call ImGui's backend uses, so it composes on top of ImGui), a Direct3D 11 backend (Windows, no clipping or textures yet), and the CPU software backend for tests. No OpenGL backend.
- **Multi-viewports** (`ImGuiConfigFlags_ViewportsEnable`, 6 mentions) are used by the editor. OkayUI: none.

## 2. The headless harness in OkayUI

OkayUI can now be run and tested with no window, no GPU and no display. The pieces:

| File | What |
|---|---|
| `okayui/include/okay/UI/OkayUI.hpp` | `GetDrawData()` returns `DrawVertex` and `DrawCmd` lists: the index buffer split into ranges that share a clip rectangle and a texture (the data `EndFrame(SDL_Renderer*)` already walked internally, now public), plus the launcher look's atlas pixels |
| `okayui/soft/OkayUI_Soft.*` | `OkayUI::soft::Render(DrawData, Bitmap&)`: triangle rasteriser with the top-left fill rule (shared edges never draw twice), per-command scissor, source-over blending, flat colour and atlas sampling (nearest; the SDL backend filters linearly), hostile data skipped |
| `okayui/tests/OkayUITestKit.hpp` | `Sim` frame driver (a persistent mouse, one-shot text, key edges and wheel, all fed to `OkayUI::BeginFrame` as a plain `OkayUI::Input`), `Reset()`, PNG reader with our own inflate, golden comparison |
| `okayui/tests/test_okayui_soft.cpp` | rasteriser: exact pixels, fill rule, scissor, atlas, hostile draw data, text lit-pixel counts, a clipped child |
| `okayui/tests/test_okayui_behaviour.cpp` | 137 checks over button, checkbox and radio, sliders and drag, input text, combo, tree node and selectable, scrolling child, layout, windows (move, collapse, close, pin, resize), popups |
| `okayui/tests/test_okayui_golden.cpp` | five committed PNGs in `okayui/tests/golden/` (explicit widgets, a button held and hovered, the demo window in the classic and the launcher look), determinism, PNG round trip |
| `okayui/tests/test_okayui_fuzz.cpp` | see below |
| `okayui/demo/` | `okayui_headless_demo [out.png] [classic|launcher]` renders a window of widgets after a scripted interaction |

OkayUI keeps its state in one global context, so every test starts with `OkayUI::ResetState()` (forget window positions, open flags, scroll offsets, popups, focus, carets, toasts, theme and font). The harness is deterministic: the caret blink counts frames and `Theme::caretBlink = false` makes it steady; nothing reads the clock except toasts and the spinner, which the tests avoid.

Frame model for a test:

```
OkayUI::ResetState();
OkayUI::Input in;  in.mouseX = ...; in.mouseDown = ...; in.text = "typed utf-8"; in.left = true;   // plain data
OkayUI::BeginFrame(in);
if (OkayUI::Begin("Window", 20, 20, 300, 200)) { OkayUI::Button("Hi"); }
OkayUI::End();
OkayUI::EndFrameData();                              // merge the overlay batch, finalize interaction
OkayUI::soft::Render(OkayUI::GetDrawData(), bitmap); // or any other backend
```

Build and run: `-DOKAY_BUILD_OKAYUI_TESTS=ON` (the default; needs `OKAY_BUILD_TESTS` and SDL2 for linking, no display). Tests are registered with ctest as `okayui_soft`, `okayui_behaviour`, `okayui_golden`, `okayui_fuzz` next to the older SDL-renderer test `okayui`; ctest sets `SDL_VIDEODRIVER=dummy`. Goldens: per-channel tolerance 2 and at most 0.05 percent of pixels may differ; `OKAYUI_UPDATE_GOLDEN=1` regenerates them and a mismatch writes `<name>.actual.png` into the working directory. `demo_window_launcher.png` was regenerated once when the own TrueType rasterizer (#655) replaced stb_truetype: the TrueType atlas has slightly different glyph anti-aliasing coverage (5324 pixels, all text edge pixels; layout, colours and widgets unchanged, checked side by side), and the output is identical across runs. Regenerate a golden only for such a deliberate, reviewed rasterizer change, never to hide a layout difference. Fuzz: `OKAYUI_FUZZ_ITERS` and `OKAYUI_FUZZ_SEED`; run it under sanitizers with `-DOKAY_SANITIZE=address,undefined,float-cast-overflow`.

### The fuzz test

Seeded random frames with NaN and infinite input and junk bytes; random nested widget trees over about 40 OkayUI widgets (windows with hostile positions and sizes, children, tables, popups, menus, combos, drag and drop); unbalanced call sequences; a reference model of text editing with caret and selection; typed junk into a small buffer; layout properties (stacked items never overlap, same-line items advance); a frame too big for the fixed batch; hostile raw draw data into the rasteriser. The two rasteriser bugs the first version found stay fixed with tests (a non-finite clip rectangle and a non-finite texture coordinate converted to an int; the clip rectangle is an int now and is handled in 64-bit arithmetic). Clean under AddressSanitizer, UBSan and float-cast-overflow on five seeds of 4,000 frames.

### What the fold-in fixed in OkayUI

Shown failing before the fix (test or sanitizer report first): the combo click-through, clicks on scrolled-out rows, label clicks, window resize, the same-line overlap, the overlay merge overflow, and the NaN conversions. The text field rewrite was written together with its tests, and the text and combo drawing changes were checked by comparing pixels.

- `TextField`/`InputText`: caret, selection, Left, Right, Home, End, Delete, Enter, Escape, select-all, mouse placement and drag selection; edits whole UTF-8 characters (Backspace used to delete one byte and corrupt multi-byte text); control characters and invalid UTF-8 are dropped; flags `InputText_EnterReturnsTrue`, `ReadOnly`, `AutoSelectAll`.
- An open combo list or popup takes the mouse from widgets under it, and the click that closes it is not passed on (it used to click both).
- Widgets scrolled out of a child region no longer take clicks where they cannot be seen.
- Clicking the label of a checkbox, radio button or switch toggles it.
- Opt-in resizable windows (`ResizableNextWindow`).
- A short item after a taller one on the same line let the next line start under the short one and overlap the tall one.
- When the fixed batch was full, the overlay merge appended indices for vertices that did not fit.
- `PushClipRect`, the scroll bar id and `ColorEdit3` converted NaN or negative floats to integers (undefined behaviour).
- Classic-look text is one quad per horizontal run of lit pixels instead of per pixel: identical pixels (checked against the SDL software renderer before and after), about a third of the vertices, so a screen of text no longer overflows the 20,000 vertex batch. Combo list rows are opaque.
- New API: `GetItemRect`, `IsItemHovered`, `WantsTextInput`, `ResetState`, `ResizableNextWindow`, `Theme::caretBlink`, the `DrawVertex`/`DrawCmd` draw list.

### Not done (behaviour `okay::imui` had, or the tests expected, that OkayUI still lacks)

- **Key repeat.** `okay::imui` repeated held keys inside the library. OkayUI's `Input` carries key edges and has no time step, so the host decides repeat (SDL's repeat events work).
- **Window z-order and click to front.** Windows draw in call order and two overlapping windows both see the mouse (the one called last wins the click only by arming order). There is no bring-to-front on click.
- **`BeginGroup`/`EndGroup`, `GetContentRegionAvail`, `GetCursorScreenPos`, `AlignTextToFramePadding`** (the last is moot, text rows are frame height). The layout tests use `GetItemRect` instead.
- **Hiding a label from the display** (`"Label##id"`): OkayUI draws the whole label and hashes it; two widgets with the same label need `PushID`. Only `ListBox` strips a `##` suffix.
- **Active and focus queries** for non-text widgets (`IsItemActive`, `GetActiveId`): the behaviour tests check results and pixels instead.
- **Window resize by default**, and a **minimum size**, are opt-in (`ResizableNextWindow`), because existing windows have fixed sizes.
- **Hover with a one frame lag.** OkayUI tests overlay and clip rectangles against the previous frame's rectangles, so the first frame after a popup opens is not yet covered.
- **Tests of `imui` internals** (hashing, UTF-8 helpers, atlas metrics, draw list batching) went with the deleted code; OkayUI's own helpers are covered through the widgets and the fuzz test.
- **Anti-aliasing, linear filtering in the software backend, user textures and fallback-font text** are not rendered by the software backend (they need a GPU texture): commands with such a texture are skipped.
- **Wiring into hosts:** the editor, player and launcher were not changed; nothing here was run in the real app.

## 3. What is missing before OkayUI could replace Dear ImGui

Ordered by how much of our Dear ImGui code each item blocks (call counts from section 1).

1. **(Done in step 1, see section 4.)** **A public draw-list API** for custom drawing (about 2,000 `AddXxx` call sites plus `GetWindowDrawList` 365): rectangles, lines, circles, polylines, paths, quads, gradients, images, clip stack, text at a point. OkayUI has the internals; they need a stable public face and a cursor and measurement API (`GetCursorScreenPos`, `SetCursorScreenPos`, `GetContentRegionAvail`, `CalcTextSize`, `GetTextLineHeight`, `GetFontSize`, `GetFrameHeight`, about 1,800 call sites).
2. **(Partly done in step 1: `IsItemActive`, `IsItemActivated`, `IsItemClicked`, `InvisibleButton`, the mouse queries.)** **Item state queries and interaction helpers:** `IsItemHovered` flags, `IsItemActive`, `IsItemActivated`, `IsItemClicked`, `InvisibleButton` (158), `IsMouseClicked`/`Down`/`Dragging`/`DoubleClicked`, `GetColorU32` (577), `SetNextItemAllowOverlap`.
3. **(Disabled blocks done in step 1.)** **Style:** `PushStyleVar`/`PopStyleVar` (375), the full colour set, per-widget colours, themes; disabled blocks (`BeginDisabled`/`EndDisabled`, 351); `PushTextWrapPos`.
4. **More input:** ctrl and alt, key down state, the other keys the editor uses (about 30), middle button, mouse delta, clipboard, double click, cursor shapes, IME; keyboard and gamepad navigation, `SetKeyboardFocusHere`.
5. **Widget gaps:** `InputTextWithHint` (204), multi-line text (25), password; `DragFloat2/3/4` (164), `ColorEdit4`; `SliderFloat` with format and typed entry; `BeginCombo`/`EndCombo` form; nested menus and menu shortcut text; `BeginPopupModal`; tab items; table sorting, scrolling and frozen rows; tree node flags; `ProgressBar` with overlay is done; `SeparatorText`, `Bullet` are done.
6. **(Mostly done in step 28, section 4.32.)** **Windows:** flags (auto-resize 72, no scrollbar 47, no scroll with mouse 29, no title bar 12, no background), z-order, saved settings, window queries (`IsWindowFocused`, `IsWindowHovered`, `SetWindowFocus`), `SetNextWindowSizeConstraints`.
7. **Docking and multi-viewports** (OS windows). The editor layout depends on them. The largest single piece.
8. **Typed drag and drop payloads.**
9. **Fonts:** pushing arbitrary fonts, a monospace code font, per-window font scale.
10. **Backends:** an OpenGL path if ever needed; the Direct3D 11 backend should use the new `DrawCmd` list (clip rectangles and the atlas texture) so it matches the SDL backend; an SDL event translator for the extra input fields is the host's job today.
11. **Performance:** nothing measured on a large editor panel. The batch is fixed (262,144 vertices, 393,216 indices, plus an overlay batch of the same size); overflow is counted and reported (`DrawData::dropped`) but a full editor panel was never tried. Table rows outside the clip are culled (4.35, row clipper); other clipped items are not.
12. **Accessibility, IME and right-to-left** are not started.

Then the migration itself: panels ported one at a time behind an option, with the same golden and behaviour tests run against both toolkits before the default changes. That is later stages of `docs/own_code_roadmap.md`, not this one.

## 4. Gap-closing step 1: audit refresh and the public draw list

The audit in section 1 ranked single functions. This section re-counted the whole usage by feature family, said plainly what OkayUI has and what it lacks, and built the top gap. Nothing in the editor, launcher or player calls the new code and no default changed: Dear ImGui is still what those programs run on.

### 4.1 Audit: HAVE and GAP by feature family

Method (repeatable): `grep -rhoE "ImGui::[A-Za-z0-9_]+"` over the 58 files under `editor/`, `launcher/` and `shared/` that mention ImGui, then each function was put in a family and the counts summed. 25,345 call sites to 261 functions (section 1 counted 24,862 to 259 earlier; the code grew). Counts are call sites in source, not calls per frame. Difficulty is our estimate for the OkayUI side: S is a day or less, M is a few days, L is a project of its own.

HAVE means OkayUI has an equivalent with the same job. PARTIAL names what is left out. GAP means nothing usable yet. The Status column is after this step.

| # | Family | Call sites | Status | What is there, what is missing | Difficulty |
|---|---|---|---|---|---|
| 1 | Item and hover queries, tooltips | 3,274 | PARTIAL (step 32) | `IsItemHovered`, `Tooltip(text)`, and now `IsItemActive`, `IsItemActivated`, `IsItemDeactivated`, `IsItemClicked`. `IsItemFocused`, `IsItemVisible`, `GetItemID`, `IsAnyItemActive` (step 6). Missing: hover flags and delays, `BeginTooltip` blocks with widgets inside (26), `IsItemEdited`, `IsItemDeactivatedAfterEdit` (17); hover flags (disabled, popup, held item, overlapped, rect only, any window, delays, stationary), `BeginTooltip` blocks with widgets, `IsItemEdited`, `IsItemDeactivatedAfterEdit` (step 32, section 4.36). Still missing: `GetStateStorage`, `SetItemKeyOwner`. | S |
| 2 | Layout and cursor | 4,613 | PARTIAL | `SameLine`, `Dummy`, `Spacing`, `Indent`, item width, and now `GetCursorScreenPos`, `SetCursorScreenPos`, `GetCursorPos`/`SetCursorPos` (and X, Y), `GetContentRegionAvail`. `BeginGroup`/`EndGroup` and `NewLine` (steps 3 and 7). Missing: `GetContentRegionMax` (25), `CalcItemWidth` | S |
| 3 | Text | 2,851 | PARTIAL | `Text`, `TextColored`, `TextDisabled`, `TextWrapped`, `BulletText`, `SeparatorText`, `LabelText`. `PushTextWrapPos`/`PopTextWrapPos` (step 5; takes a width, not an x position). Missing: printf-style formatting (callers format first) | S |
| 4 | Draw list (custom drawing) | 2,520 | HAVE (this step) | See 4.2. Missing: `AddConcavePolyFilled` (5), `AddImageRounded` (17), rounding on chosen corners only, anti-aliasing, `AddCallback` | done |
| 5 | Buttons, checkbox, radio | 2,387 | PARTIAL | `Button`, `SmallButton`, `Checkbox`, `RadioButton`, `ToggleSwitch`, `ImageButton`, `InvisibleButton`, `ArrowButton` (a drawn triangle, step 30), `ColorButton` (a checkerboard through the alpha, step 29). Missing: explicit button size | S |
| 6 | Style and colours | 1,896 | PARTIAL (step 29) | `Style()`, `PushTextScale`, `GetColorU32`, `PushStyleVar`/`PopStyleVar` (step 20), and now the full colour set: 58 `Col_*` ids (the 8 originals and the Dear ImGui set) with theme-derived defaults, `PushStyleColor` (bytes, packed or float), `SetStyleColor`/`ResetStyleColor`, `GetStyleColorVec4`, `GetStyleColorName`, colour converters. Most ids are drawn by the widgets that use them (section 4.33 lists them). Missing: `PushFont`/`PopFont` of arbitrary fonts; ids nothing draws with yet (the navigation highlights, `BorderShadow`, the unfocused tab colours, the hovered and active separator) | M |
| 7 | Value widgets (drag, slider, input number) | 1,610 | PARTIAL (step 2) | `DragFloat`, `DragInt`, `SliderFloat`, `SliderInt`, `InputInt`, and now `DragFloat2/3/4`, `DragInt2/3/4` and `DragFloatRange2` (several drag cells in one row sharing a label; each cell scrubs alone, clamps, range cells never cross). Missing: `DragIntRange2`, format strings, typing into the single-value `DragFloat`/`SliderFloat` (the multi-cell ones can, step 10), a drag-speed modifier key | M |
| 8 | Window, child, scroll | 1,074 | PARTIAL (step 28) | `Begin`/`End`, `BeginChild`, resize grip, pin, `SetNextWindow*`, window queries, and now z-order with click to front, input occlusion between overlapping windows and over docked content, and the flags `NoScrollbar`, `NoBackground`, `NoCollapse`, `AlwaysAutoResize`, `NoFocusOnAppearing`, `NoBringToFront` (section 4.32). Windows begun with `Begin(title, p_open, flags)` scroll when their content overflows. Missing: `SetNextWindowSizeConstraints`, `GetScrollY`/`SetScrollY`/`SetScrollHereY` for windows, no scroll with mouse, saved window settings | M |
| 9 | Popups | 1,038 | PARTIAL (steps 12, 32) | `OpenPopup`, `BeginPopup`, `BeginPopupContextItem`, `CloseCurrentPopup`. Missing: `BeginPopupModal` (85) with dimming, `BeginPopupContextWindow`, `IsPopupOpen` (41) [Update: modal popups were step 12; now also `BeginPopupContextWindow`, `BeginPopupContextItem(id, flags)`, `IsPopupOpen(id, flags)` with `AnyPopupId` (step 32). Still missing: `GetMousePosOnOpeningCurrentPopup`, `BeginPopupEx`.] | M |
| 10 | Menus | 957 | PARTIAL | menu bar, `BeginMenu`, `MenuItem(label)`. shortcut text, checked and enabled state, `MenuSeparator` (step 11). Missing: nested sub-menus, `Shortcut` handling (the text is display only) | M |
| 11 | Mouse, keys, IO | 901 | PARTIAL (step 32) | mouse position, buttons, wheel, text, 9 key edges, and now `IsMouseDown/Clicked/Released`, `GetMousePos`, `GetMouseDelta`. Missing: Ctrl and Alt, key down, about 30 more keys, middle button, double click, drag deltas, mouse cursor shapes (92); now the middle button, `GetMouseDragDelta`, `ResetMouseDragDelta`, `MouseButton_*`, `Key_*` names, `KeyId_Slash` (step 32). | M |
| 12 | IDs | 792 | HAVE | `PushID`, `PopID`. Missing: `GetID` (11), `GetStateStorage` | S |
| 13 | Combo and selectable | 618 | HAVE (step 30) | `Combo(items)`, `Selectable`, `ListBox`, `BeginCombo`/`EndCombo` (step 9), and `Selectable` with a size, a pointer overload and flags (disabled, close the popup, span all columns, double click only; section 4.34) | done |
| 14 | Text input | 545 | PARTIAL | single line with caret, selection, UTF-8, and now IME composition (`Input::composition`, `compositionCaret`, `commit`; underlined preedit, commit, cancel, caret rectangle for a host's candidate window; step 27). `InputTextWithHint` and `SetKeyboardFocusHere` (step 4). A separate multi-line code editor widget, `OkayUI::CodeEdit` (step 25, `docs/own_ui_code_editor.md`; not `InputTextMultiline` itself). `InputTextMultiline` (step 30, on `CodeEdit` without the gutter; section 4.34). Missing: password, callbacks, `InputText` flags beyond read only, enter-returns-true and select-all, a word-wrapping multi-line box | M |
| 15 | Disabled blocks | 369 | HAVE (this step) | `BeginDisabled`/`EndDisabled`/`IsDisabled` | done |
| 16 | Tables | 233 | PARTIAL (steps 14, 31, 33) | `BeginTable` with headers, weights, drag-resizable columns, `Columns`, `TableSetColumnIndex`, flags, and now sorting (header clicks, multi-key, tristate, default sort, the sort specs API), scrolling boxes (`Table_ScrollX`/`Table_ScrollY`, wheel, bars, `TableSetupScrollFreeze` for frozen rows and columns), full-height row bands and `TableSetBgColor` (section 4.35). Missing: column reordering and hiding, `TableSetupColumn` flags beyond fixed width and sorting, per-column header tooltips, context menu on the header, `TableGetHoveredColumn`, nested tables, auto-fit columns, a scrolling table inside a collapsed parent clipped by `BeginChild` gets no special care; step 33 (section 4.37): hiding columns (`Col_DefaultHide`, `Col_NoHide`, the header menu with `Table_Hideable`, `TableSetColumnEnabled`), `Col_NoResize`, `Col_NoHeaderLabel`, `TableGetHoveredColumn`, name and flag queries. Column reordering is still a GAP. | M |
| 17 | Tree nodes | 201 | HAVE (step 30) | `TreeNode`, `CollapsingHeader`, `TreeNodeEx` (default open, leaf, selected, open on arrow, open on double click, bullet, span full width, no tree push, framed), `SetNextItemOpen`, `IsItemToggledOpen` | done |
| 18 | Colour editors | 168 | HAVE (step 29) | `ColorEdit3`/`ColorEdit4` with flags (no alpha, hex, no inputs, no picker, no label), a click on the swatch opens a picker popup, `ColorButton`, `ColorPicker3`/`ColorPicker4` (saturation / value square, hue bar, alpha bar, current and original preview, hex field, R G B A cells), hex helpers. Missing: HSV input mode, a float display mode, the picker's wheel layout, copy and paste of a colour by right click | done |
| 19 | Drag and drop | 123 | PARTIAL | `DragSource(int)`, `DropTarget(int*)`. Missing: typed payloads, a preview while dragging, drop on custom rectangles | M |
| 20 | Tabs | 55 | PARTIAL (steps 17, 33) | `TabBar(labels)`, `BeginTabBar` / `BeginTabItem` with bodies, closable tabs, `TabItemButton`, trailing tabs (step 17), and now `BeginTabBar(id, flags)` with `Reorderable` (drag a tab along the bar), `AutoSelectNewTabs`, middle click closes a closable tab, an unsaved-document dot (section 4.37). Missing: an overflow strip or tab-list popup (tabs past the bar are cut off), tab tooltips | M |
| 21 | Clipboard | 52 | GAP | none. Needs the clipboard in `okay::platform`, and copy, cut and paste in the text field | S |
| 22 | Docking and viewports | 36 + 6 mentions of viewport flags | PARTIAL (steps 26, 34) | `DockSpace`, `DockBegin`/`DockEnd`, dock into the left, right, top, bottom or centre of a node, splitters with minimum sizes, tabs (reorder, close, tear off), drag a title bar or tab onto drop targets with a preview, layout save and load as text, repair of damaged layouts (`okayui/include/okay/UI/OkayUIDock.hpp`, section 4.30). Floating windows now stack and take clicks in order, and docked content sits below them (step 28, section 4.32). Missing: multi-viewports (OS windows from `okay::platform`), the `DockBuilder*` call shapes, central node and node flags; dock layouts written as code: `DockBuilderAddNode`, `SplitNode`, `DockWindow`, `Finish` and a pure `DockBuilderTree` that builds into the dock model (step 34, section 4.38). Still missing: `SetNextWindowDockID`, `GetWindowDockNode`, undock by id, ini save in memory, multi-viewports. | L |
| 23 | Fonts, IME, emoji | 150 `SetWindowFontScale`, 7 `PushFont`, 26 IME mentions | PARTIAL | the 8x8 font, a bold variant, the launcher look's Roboto atlas with engine TrueType fallback. IME composition input and the caret rectangle for the candidate window (API only; step 27, section 4.31). Missing: arbitrary fonts and a monospace code font, merged icon fonts, the operating-system side of IME (no host code anywhere sends `Input::composition` yet), wide-character cells, colour emoji | L |
| 24 | Images, plots, progress | 3 plots, 16 images | HAVE (step 30) | `PlotLines`, `PlotHistogram` (and with options: ring offset, overlay, size, getter, hover highlight and tooltip), `Image`, `ProgressBar` (and with a size and an indeterminate bar). The software test backend does not draw textures | done |
| 25 | Backends | not counted | PARTIAL | SDL_Renderer, Direct3D 11 (ignores clip spans and textures), CPU software. No OpenGL, no Direct3D 12 | M |

Reading the table: the biggest single block that was missing outright was custom drawing (family 4), then cursor and measurement (family 2), then item and mouse queries (family 1), then style (family 6), then vector value fields (family 7). The Inspector alone calls `DragFloat2/3` about 170 times, so family 7 is the best next step after this one: it is mostly composition of what `DragFloat` already does. Docking (family 22) and IME (family 23) were the two real obstacles; both now exist headless (sections 4.30 and 4.31), but no host feeds IME events and nothing in the editor uses either, so the editor still cannot leave Dear ImGui.

### 4.2 What was built

All of it is in `okayui/include/okay/UI/OkayUI.hpp` and `okayui/src/OkayUI.cpp`, side by side with the existing widgets, nothing existing was changed in behaviour except that a disabled block is respected.

- **Draw list.** `GetWindowDrawList()` and `GetForegroundDrawList()` return a `DrawList` with `AddLine`, `AddRect`, `AddRectFilled`, `AddRectFilledMultiColor`, `AddCircle`, `AddCircleFilled`, `AddTriangle`, `AddTriangleFilled`, `AddQuad`, `AddQuadFilled`, `AddPolyline`, `AddConvexPolyFilled`, `AddBezierCubic`, `AddText`, `AddImage`, `PushClipRect`/`PopClipRect`, and the path calls (`PathClear`, `PathLineTo`, `PathArcTo`, `PathStroke`, `PathFillConvex`). Colours are a packed `Color32` (`RGBA(r, g, b, a)`, `GetColorU32(Col)`, `ColorMulAlpha`). Rules that differ from what a port might assume: outlines are drawn inside the given bounds, polylines use mitred joins (a translucent stroke is not darker at its corners), the foreground list goes into the overlay batch and is never clipped, and a draw list call is never dimmed by `BeginDisabled`. Rectangle and circle outlines sit inside the bounds, but polyline, triangle and quad outlines are centred on their path. Non-finite or huge numbers are ignored, not drawn.
- **Cursor and measurement.** `GetCursorScreenPos`, `SetCursorScreenPos` (the next item goes exactly there, the layout then continues below it), `GetCursorPos`, `SetCursorPos`, `GetCursorPosX/Y`, `SetCursorPosX/Y`, `GetContentRegionAvail`, `CalcTextSize`, `GetTextLineHeight`, `GetFontSize`, `GetFrameHeight`, and the `...WithSpacing` forms.
- **Item state.** `InvisibleButton`, `IsItemActive`, `IsItemActivated`, `IsItemDeactivated`, `IsItemClicked(button)`, `IsMouseDown/Clicked/Released(button)`, `GetMousePos`, `GetMouseDelta`, `IsMouseHoveringRect`. The item queries work after the widgets that register an id (the list is in the header); after any other item they answer false instead of reporting the previous item.
- **Disabled blocks.** `BeginDisabled(bool)`, `EndDisabled`, `IsDisabled`. Widgets inside take no input and draw with their colours halfway to the panel colour (alpha is kept, so the stacked layers of one widget do not blend through each other). A text field that has the keyboard gives it up.
- **Reachable.** The "Draw list lab" page in `okayui/demo/DemoScene.hpp` uses all of it (a canvas drawn with the draw list and made interactive with `InvisibleButton`; clicks place markers, a drag draws a stroke with a foreground rubber band; a gauge built with the path calls; a right-aligned label placed with `CalcTextSize` and `SetCursorScreenPos`; a locked block built with `BeginDisabled`). Run it with `okayui_headless_demo lab.png lab` (or `lab-launcher`).

### 4.3 How it was tested

- `okayui_drawlist` (new, `okayui/tests/test_okayui_drawlist.cpp`): pixel checks through the software backend for every primitive (exact pixel counts for rectangles and lines, areas for circles, a hexagon and a half disc; a translucent closed polyline has one single colour on every stroke pixel, which proves the mitred joins do not overlap), the path API, `AddText` against `Label` pixel for pixel, clip rectangles, foreground against window list ordering, hostile numbers (NaN, infinity, huge, negative: nothing is drawn), measurement and cursor values against the real layout, `InvisibleButton` and the item-state edges (active, activated, deactivated, clicked), mouse queries, and disabled blocks (every widget family ignores input, dimming checked per channel and the face stays opaque, nesting, unbalanced calls). Two golden images of the lab page in `okayui/tests/golden/`: `drawlist_lab.png` (classic look) and `drawlist_lab_launcher.png`.
- `okayui_fuzz`: the random widget tree now also calls every draw list function with hostile numbers (up to 300 points per polyline, null pointers, random counts), the path calls, hostile cursor setters, `InvisibleButton` with hostile sizes, the item and mouse queries, and unbalanced `BeginDisabled`/`EndDisabled`. The draw data is checked for consistency every frame.

### 4.4 Not done, said bluntly

- **Nothing in the editor or launcher uses any of this.** The editor's custom drawing (about 2,500 call sites) still calls `ImDrawList`. A port needs a small adapter (a `Vec2` and colour conversion), which was not written because it would be unused code until the first panel is ported.
- **The lab page is only reachable headlessly** (`okayui_headless_demo`). The editor's "Test UI" and the player overlay were not extended: they are big files that could not be built and run in this step, and an untested edit to them would break the rule that new controls are observed.
- **The Direct3D 11 backend ignores clip spans** (the old `OkayUI::D3D11Backend`; the new one in section 7 draws a scissor rectangle per span), so a clipped draw list is not clipped there. The Windows path is also untested here. The SDL_Renderer path honours clipping by construction but was not run on a display in this step.
- **The software backend cannot show `AddImage`** (no textures), and has no anti-aliasing: diagonal lines and circle edges are hard-edged, unlike the GPU path with linear filtering. Pixel tests assert shapes and counts with tolerances, not a match with any GPU output.
- **Concave polygons** are drawn as a fan and so wrong; there is no ear clipping. `AddConcavePolyFilled` (5 call sites) and `AddImageRounded` (17) are not provided. Rounded rectangles round all four corners or none.
- **Text in the draw list is one line**, with no newline handling, and no per-call font.
- **`BeginDisabled` does not cancel a drag that is already in progress**, and the widgets that do not go through the common hit test (a popup body already open, the window title bar) are not dimmed.
- **The vertex batch is bigger but still fixed**: 262,144 vertices and 393,216 indices for each of the main and overlay layers (it was 20,000 and 30,000). Overflow is no longer silent: `DrawData::dropped` and `DroppedGeometry()` count the shapes that did not fit, and a message goes to stderr for the first five frames it happens. A frame of 50,000 vertices is drawn completely (tested); 70,000 shapes (280,000 vertices) lose the last ones and say so. The 128 draw spans (clip and texture changes) per frame are still a fixed limit and are not reported. Not measured against a real editor panel.
- **Polylines are capped at 256 points** and the path at 256, `AddBezierCubic` at 255 segments; longer input is refused, not split.
- **The cursor queries return whatever the layout holds.** If an earlier call put a non-finite number in the layout (for example `Dummy(INFINITY, 0)`, which OkayUI has always accepted), `GetCursorScreenPos` returns it; `GetContentRegionAvail` and `CalcTextSize` never return a negative or NaN.
- **The other families in 4.1 are not started**: vector drag fields, `PushStyleVar`, modal popups, window flags, `InputTextWithHint`, multi-line text, clipboard, tab items, nested menus, docking, IME.

### 4.5 Follow-up fixes in the same branch

- **Text field UTF-8 corruption (found by the fuzz test, fixed).** `OKAYUI_FUZZ_SEED=4 OKAYUI_FUZZ_ITERS=4000` left a lead byte followed by an ASCII byte in the buffer. Root cause: the field keeps three byte offsets between frames (caret, selection anchor, first shown character). An edit below the first shown character (a selection from the left that had scrolled out of view, replaced by typed multi-byte text) changed the characters under that offset, so it pointed into the middle of a new character; a click then put the caret there and Delete cut the character in half. Fix: the three offsets are snapped back to a character boundary every frame (also after edits, and when the caller rewrites the buffer between frames). A second hole in the same area: a caller buffer that is full and not terminated got its last byte overwritten with NUL, which could cut a multi-byte character in half; an incomplete trailing sequence is now removed. Typed or pasted text that does not fit is dropped whole, as before, and invalid sequences are never inserted. Regression tests: `Utf8BufferTests` in `okayui/tests/test_okayui_behaviour.cpp` (deterministic; 4 of its checks fail on the unfixed field), plus the fuzz scenario. Fuzz clean under AddressSanitizer, UBSan and float-cast-overflow for seeds 1 to 8 at 4,000 iterations.
- **Batch overflow no longer silent.** See the vertex batch bullet in 4.4. Test: `LargeDrawListTests` in `okayui/tests/test_okayui_drawlist.cpp` (50,000 vertices drawn exactly, 100,000 across both layers, 70,000 shapes reported as dropped), and the fuzz scenario now fills the real limit.


### 4.6 Gap-closing step 2: vector value fields (family 7)

Built: `DragFloat2/3/4`, `DragInt2/3/4`, `DragFloatRange2` (`okayui/src/OkayUI.cpp`, one shared routine). A row of 2 to 4 cells with a 4 px gap, one label to the right of the last cell, each cell its own id (label id plus cell number), so scrubbing one cell never moves another. `minV < maxV` clamps every cell; the range widget keeps the low cell at or below the high cell. A NaN or infinite speed or value changes nothing. Null pointers are refused. Cells narrower than 64 px show one decimal.

Tested (`okayui/tests/test_okayui_values.cpp`, 24 checks, headless): each cell alone, clamping at both ends, two and four cells, integers with clamping, the range rule from both sides, NaN speed, null pointers, a long label. The fuzz test now also draws the four new shapes with hostile numbers.

Not done: typing a number into a cell (double click), `SliderFloat2/3`, `InputFloat`, a speed modifier key, per-cell colour labels (X, Y, Z), and the editor's Inspector does not use these (the editor is still on Dear ImGui, and OkayUI stays off).

### 4.7 Gap-closing step 3: sliders in a row, float field, small widgets

Built: `SliderFloat2/3/4` (cells in one row, one label), `InputFloat` (the float twin of `InputInt`: [-] and [+] steppers; a NaN or infinite step, or one that would overflow the value, changes nothing), `ArrowButton` (left, right, up, down), `ColorButton` (a clickable swatch), `ColorEdit4` (swatch with the alpha on its left half plus four sliders) and `NewLine`. Families 2, 5, 7 and 18.

Tested in `okayui/tests/test_okayui_values.cpp` (37 checks in total, headless) and drawn with hostile numbers by the fuzz test.

Not done: `BeginGroup`/`EndGroup` (since built, section 4.11), typing into `InputInt`/`InputFloat` (since built, section 4.20), `ColorPicker4`, `SliderInt2/3`, arrow buttons with a drawn triangle (they use the font glyphs `<`, `>`, `^`, `v`).

### 4.8 Gap-closing step 4: placeholder text and keyboard focus (family 14)

Built: `InputTextWithHint` (a dim placeholder while the field is empty; it never touches the buffer) and `SetKeyboardFocusHere` (the next text field takes the keyboard, caret at the end). 207 and 54 call sites in the editor and launcher.

Tested (`test_okayui_values.cpp`, 43 checks in total): the placeholder is drawn only while empty (pixel count in the field), typing after `SetKeyboardFocusHere` reaches the field with no click, a null hint and a null buffer are refused; the fuzz test draws both with hostile buffers.

Not done: multi-line text, password fields, text callbacks, the hint while the field is focused and empty is shown (same as Dear ImGui) but with no separate colour setting.

### 4.9 Gap-closing step 5: text wrap (family 3)

Built: `PushTextWrapPos(width)` and `PopTextWrapPos()`. While on, `Text`, `TextColored` and `TextDisabled` break at word boundaries (`TextWrapped` now shares the same routine). Width 0 wraps at the right edge of the window, a positive width wraps there or at the edge, whichever is nearer. **Difference from Dear ImGui:** it takes a width, not an x position, and it does not nest (Pop turns it off). A forgotten Pop is cleared at the start of the next frame; NaN and negative widths mean the right edge.

Tested (47 checks in `test_okayui_values.cpp`): both text calls wrap onto several lines, the text after Pop is one line again, a missing Pop does not leak, hostile widths; the fuzz test leaves a wrap open on purpose. Golden-image (15) and draw-list (184) suites unchanged.

Not done: wrapping `BulletText` and widgets' own labels, a wrap position measured from the cursor.

### 4.10 Gap-closing step 6: item queries (family 1)

Built: `IsItemFocused`, `GetItemID`, `IsAnyItemActive`, `IsItemVisible` (the last item against the current clip rectangle). Tested in `test_okayui_values.cpp` (55 checks in total): ids are non-zero and different per widget, focus follows a click into a text field and drops on a click elsewhere, a held press is "any active", an item outside a small clip is not visible. Behaviour (149), fuzz (12), golden (15), drawlist (184) and soft (18) suites unchanged.

Not done: `IsItemEdited` and `IsItemDeactivatedAfterEdit` (need every widget to record that it changed), hover flags and delays, tooltips with widgets in them.

### 4.11 Gap-closing step 7: groups (family 2)

Built: `BeginGroup` / `EndGroup`. Everything placed in between is one item: `GetItemRect`, `IsItemHovered` and `SameLine` refer to the whole block (columns of widgets side by side). Nesting up to 8 deep; an empty group is a zero-size item; an unmatched `EndGroup` is ignored; a missing one is cleared at the start of the next frame. 92 call sites in the editor and launcher.

Tested (63 checks in `test_okayui_values.cpp`): the group starts where its first item starts and covers both stacked buttons, `SameLine` after it goes next to its top, nested groups, empty and unmatched groups, hover on any part. Fuzz (12, now drawing random nested groups), behaviour (149), golden (15), drawlist (184), soft (18) unchanged.

Not done: `GetContentRegionMax`, `CalcItemWidth`, group-wide `IsItemActive`.

### 4.12 Gap-closing step 8: tree node flags (family 17)

Built: `TreeNodeEx(label, flags)` with `TreeNode_DefaultOpen`, `TreeNode_Leaf` (no arrow, never toggles, returns true and indents until `TreePop` like Dear ImGui's leaf) and `TreeNode_Selected` (accent highlight); `SetNextItemOpen(bool)` (forces the next tree node or collapsing header once); `IsItemToggledOpen()` (the last node was opened or closed by a click this frame). `TreeNode` is `TreeNodeEx(label, 0)`.

Tested (70 checks in `test_okayui_values.cpp`): default closed, default open, a leaf returns true, a click opens and then closes, the toggle is reported, a forced open and close, a leaf click never toggles. Fuzz draws random flags and forced states; behaviour 149, golden 15, drawlist 184, soft 18 unchanged.

Not done: open on arrow only, bullet nodes, full-width selection flag.

### 4.13 Gap-closing step 9: combo with custom content (family 13)

Built: `BeginCombo(label, preview)` / `EndCombo()`. The box shows `preview`; while open, whatever is drawn between the two (usually `Selectable` rows that call `CloseCurrentPopup`) fills a popup under the box, as wide as the box (popups can now have a width: `PopupSlot::w`, 0 keeps the old 160 px). Clicking the box again, or outside the list, closes it; `GetItemRect` right after `BeginCombo` is the box. A null preview draws nothing. 86 call sites.

Tested (80 checks in `test_okayui_values.cpp`): closed until clicked, opens on click, rows sit under the box in order, picking a row sets the value and closes, the box toggles, a click outside closes without changing the value. Fuzz draws random content in a combo; behaviour 149, golden 15, drawlist 184, soft 18 unchanged. A test mistake (reading the item rect from inside the open popup) found that `GetItemRect` was returning the popup's rect, which is why it now returns the box.

Not done: selectable size and flags, keyboard navigation of the list, a height limit with scrolling.

### 4.14 Gap-closing step 10: typing a number into a drag or slider cell (family 7)

Built: Ctrl+click on a cell of `DragFloat2/3/4`, `DragInt2/3/4`, `DragFloatRange2` or `SliderFloat2/3/4` turns it into a text box with the whole value selected. Enter or a click elsewhere commits (clamped to the cell's range, rounded for integers, the range widget keeps low <= high); Escape cancels; text that is not a finite number keeps the old value. `Input` gained `ctrl` (Ctrl held); hosts do not set it yet, so the editor and launcher integration (`shared/OkayUIHost.hpp`) is still to do.

A bug found while testing: the press that starts the edit also reached the text box's own click-to-place-caret code, which overwrote the select-all, so typed text was appended to the old value. Forced focus (`SetKeyboardFocusHere` and this) is now applied after the click handling.

Tested (91 checks in `test_okayui_values.cpp`): typed value, clamp, Escape, non-number, integer rounding and clamp, slider cell, plain click still scrubs, click-away commits. Behaviour 149, fuzz 12, golden 15, drawlist 184, soft 18 unchanged.

Not done: a double click; the host sets `ctrl`. (The single-value `DragFloat`, `DragInt`, `SliderFloat` and `SliderInt` take Ctrl+click too; this line used to say they did not.)

Step 10b: the single-value `DragFloat`, `DragInt`, `SliderFloat` and `SliderInt` take Ctrl+click typing too (same rules; `SliderInt` rounds). 99 checks in `test_okayui_values.cpp`; other suites unchanged. Still not done: the host does not set `Input::ctrl` yet.

### 4.15 Gap-closing step 11: menu items (family 10)

Built: `MenuItem(label, shortcut, selected, enabled)` (right-aligned dim shortcut text, shown only when it fits; a small filled square for a checked item; a disabled item is dim, ignores clicks and keeps the menu open) and `MenuSeparator()`. 957 menu call sites in the editor.

Tested (109 checks in `test_okayui_values.cpp`): a normal item with a shortcut clicks and closes the menu, a disabled item does nothing, a checked item still clicks, the item below a separator, the shortcut pixels are drawn, null labels and calls outside a menu are harmless. Fuzz draws random items, shortcuts, states and separators; behaviour 149, golden 15, drawlist 184, soft 18 unchanged.

Not done: nested sub-menus, acting on the shortcut keys (the text is for display), keyboard navigation of menus.

### 4.16 Gap-closing step 12: modal popups (family 9)

Built: `BeginPopupModal(name, bool* open)` / `IsPopupOpen(name)` plus `Input::displayW/H`. The dialog is centred in the display, the area behind is dimmed and inert, a click outside does not close it, Escape closes it, and an optional close button clears `*open`.

Tested (121 checks in `test_okayui_values.cpp`): opens and reports open, centred position, background pixel darker on every colour channel, outside click ignored, widgets behind inert, Escape and the close button close it, null names harmless. Fuzz and the other suites unchanged.

Not done: the host must set `Input::displayW/H` and `ctrl` (shared/OkayUIHost.hpp), a sizing option, keyboard navigation inside the dialog.

### 4.17 Gap-closing step 13: nested sub-menus (family 10)

Built: `BeginMenu` inside a `BeginMenu` block is a sub-menu. It is a row with an arrow; the list opens to its right while the pointer is on the row or on the list (up to four levels deep, a fifth is refused), picking any item runs it and closes the whole chain, a press outside closes everything, and a press inside a deeper list does not count as "outside" for the lists above it. Each level is closed with `EndMenu()` like the first.

Tested (133 checks in `test_okayui_values.cpp`): closed until the row is pointed at, opens on hover, stays open while the pointer is in its list, a second level opens, a deep item runs and closes every level, leaving a sub-menu closes just it, an outside press closes all, a plain item beside it still works, a null label is harmless. Fuzz now nests sub-menus up to five levels with null labels; behaviour 149, fuzz 12, golden 15, drawlist 184, soft 18 unchanged.

Not done: keyboard navigation (arrow keys, Enter), a hover delay before opening, flipping to the left near the display edge, acting on the shortcut keys.

### 4.18 Gap-closing step 14: flag-style tables (family 4)

Built: `BeginTable(id, columns, flags)` with `TableSetupColumn(label, columnFlags, widthOrWeight)`, `TableHeadersRow()`, `TableSetColumnIndex(column)` (cells in any order, false for a missing column), `TableGetColumnIndex()` / `TableGetRowIndex()`, flags `Table_RowBg` (faint band on every second row), `Table_BordersInnerH` (a hairline between rows) and `Table_NoResize`, and `Col_WidthFixed` (pixels; the other columns share the rest by weight). The old `BeginTable(id, n, headers[, weights])` still works. Borders and resize handles now start at the table's own top edge (before, a table without headers had them reach one row above it). The editor uses these calls about 20 times.

Tested (141 checks in `test_okayui_values.cpp`): cells land in their columns, also when set out of order, the fixed column keeps 100 px and the weighted ones split the rest 1:3 (95 and 285 px), a second row lands below the first, a missing column and calls outside a table are harmless, dragging a border resizes unless `Table_NoResize`, the band shows in the pixels. Older suites unchanged.

Not done: sorting (clickable headers), hiding columns, scrolling a table with a fixed header, a band that follows a tall row (it is one text row tall), sizing a stretch column by content.

### 4.19 Gap-closing step 15: keyboard navigation of menus (family 10)

Built: while a menu is open, Down and Up move a highlight over its enabled items (wrapping; disabled items and separators are skipped), Enter runs the highlighted item and closes the whole chain, Right (or Enter) opens a highlighted sub-menu, Left closes a sub-menu, Escape closes the innermost open menu, and the mouse moves the same highlight so the two never disagree. Only the innermost open menu reacts to the keys. New `Input::up` / `Input::down` (key edges like the others). A sub-menu opened from the keyboard no longer closes under a parked pointer (leaving a sub-menu with the mouse only closes it while the mouse is moving).

Tested (154 checks in `test_okayui_values.cpp`): Down highlights the first item and Enter runs it, a disabled row is skipped, Up from nothing wraps to the last, Right/Down/Enter inside a sub-menu runs its item and closes everything, Left closes just the sub-menu, Escape closes inner then outer, keys do nothing with no menu open, Enter with no highlight runs nothing. Fuzz now also sends random Up/Down edges.

Not done: Left/Right moving between the menus of a menu bar, typing a letter to jump to an item, the editor host sending Up/Down (it does not set any key edges yet).

### 4.20 Gap-closing step 16: typing into InputInt and InputFloat (family 7)

Built: a plain click on the value box of `InputInt` or `InputFloat` starts typing (no Ctrl needed, unlike the drag and slider boxes); Enter or clicking away applies, Escape cancels, text that is not a number leaves the value alone, `InputInt` rounds and clamps to the int range, `InputFloat` ignores `nan`, `inf` and numbers that do not fit a float. The `-` / `+` steppers work as before. This was the last number widget that could only be changed with its buttons.

Tested (164 checks in `test_okayui_values.cpp`): typed `42`, a rounded `-7.6`, `x` unchanged, `99999999999` clamped to 2147483647, floats `3.25` and `-0.5`, `nan` and `1e50` unchanged, a stepper still moves the value, Escape cancels a half-typed edit.

Not done: auto-repeat when a stepper is held down, a thousands separator, undo inside the edit, the host setting `ctrl` for the drag and slider boxes (shared/OkayUIHost.hpp is not on this branch).

### 4.21 Gap-closing step 17: tab bars with tab bodies (family 15)

Built: `BeginTabBar(id)` / `EndTabBar()`, `BeginTabItem(label, bool* open = nullptr, flags)` / `EndTabItem()` and `TabItemButton(label, flags)`; flags `Tab_SetSelected`, `Tab_Trailing` (right-aligned) and `Tab_NoTooltip` (accepted; there are no tab tooltips). The first tab is chosen at the start, a click chooses another, only the chosen tab's `BeginTabItem` returns true so its body is the only one that runs, `Tab_SetSelected` chooses a tab, a small x appears when a `bool*` is given (clicking it clears the bool and the first tab takes over), and `GetItemRect()` right after a tab call is that header. The choice is remembered per bar id. The existing array-style `TabBar(labels, count, &current)` is unchanged. The editor uses `BeginTabBar` / `BeginTabItem` about 20 times.

Tested (`test_okayui_values.cpp`): the first tab is chosen at the start with only its body running, headers sit left to right with the trailing button at the right end, a click switches the body, `Tab_SetSelected` wins while it is passed and the choice sticks without it, the x closes a tab and the first takes over, the trailing button reports its click, calls outside a bar are harmless. Fuzz now throws random tab bars at it (null ids and labels, nested bars, random flags, closing boxes).

Not done: reordering by dragging, scrolling or shrinking tabs that do not fit (they run past the right edge), a drop-down list of all tabs, keyboard switching with Ctrl+Tab.

### 4.22 Gap-closing step 18: tooltips and small helpers (families 5, 16)

Built: `SetTooltip(fmt, ...)` (printf-style, always shown: the caller decides, usually `if (IsItemHovered())`; about 1500 calls in the editor), tooltips of several lines ("\n"), and every tooltip box now stays inside the display (`Input::displayW/H`) by flipping to the left of the cursor or above it; `TextUnformatted(text, end)`; `GetItemRectMin/Max/Size`; `GetTime()` and `GetFrameCount()` (new `Input::time`, otherwise frames / 60); `IsMouseDoubleClicked()` (second press within 0.3 s and 6 px, a third press is a new first press); `IsMouseDragging(button, threshold)` (left button held and moved more than the threshold, default 4 px); and `AlignTextToFramePadding()`, which does nothing because text in a row is already vertically centred.

Tested (`test_okayui_values.cpp`): the item rectangle corner helpers equal `GetItemRect`, `SetTooltip` draws a box, a second line makes it taller, near the right edge it flips to the left, double click true for two quick presses and false for a third, a slow second press and a far one, dragging true only after the threshold and only for the left button, time and frame count advance, null arguments are harmless.

Not done: `BeginTooltip` / `EndTooltip` (tooltips with widgets inside), a hover delay, wrapping a long tooltip line, the other 120 or so ImGui names the editor uses that OkayUI does not have (the biggest: `IsKeyPressed` 356 calls, `PushStyleVar` / `PopStyleVar` 384, `GetIO` 190, `SetWindowFontScale` 150, `SetMouseCursor` 92, `SetNextWindowSize/Pos` 154).

### 4.23 Gap-closing step 19: named key state (family 16)

Built: `Input::keyDown[KeyId_COUNT]` (the host says which named keys are held: Tab, arrows, PageUp/Down, Home/End, Insert, Delete, Backspace, Space, Enter, Escape, A to Z, 0 to 9, F1 to F12, Ctrl, Shift, Alt) and `IsKeyDown(key)`, `IsKeyPressed(key, repeat = true)` (true on the frame the key goes down; with `repeat`, again every 0.05 s once held for 0.5 s) and `IsKeyReleased(key)`. Ctrl and Shift are also down while `Input::ctrl` / `Input::shift` are. An id out of range is "not down". The editor calls `IsKeyPressed` about 356 times and `IsKeyDown` 13 times.

Tested (`test_okayui_values.cpp`): idle keys are not down, a key reports pressed only on the frame it goes down, a held key is not pressed again for the first half second and then repeats several times a second (and never repeats with `repeat` off), released on the frame it goes up, ids out of range, Ctrl and Shift following the older flags. Fuzz sends random key states.

Not done: key chords (`IsKeyChordPressed`), mouse buttons as keys, text-input keys that come with a character, the host (shared/OkayUIHost.hpp, not on this branch) filling `keyDown` from SDL scancodes, `GetIO().KeyCtrl` style access.

### 4.24 Gap-closing step 20: style variables (family 18)

Built: `PushStyleVar(StyleVar, float | Vec2)` / `PopStyleVar(count)`. They nest (32 deep; a deeper push is ignored), more pops than pushes do nothing, and pushes left unpopped are undone at the start of the next frame. Supported: `StyleVar_ItemSpacing` (x, y gap between widgets), `StyleVar_WindowPadding` (x), `StyleVar_FrameRounding` and `StyleVar_ChildRounding` (corner radius), `StyleVar_WindowRounding`, `StyleVar_BorderSize` (stands in for the window, frame and child border sizes). Accepted without effect so ported code compiles: `FramePadding`, `ButtonTextAlign`, `SelectableTextAlign`, `ItemInnerSpacing`, `ScrollbarSize`, `Alpha`. The editor calls `PushStyleVar` about 226 times (WindowPadding 64, FramePadding 40, ChildRounding 34, FrameRounding 33, ItemSpacing 24).

Tested (`test_okayui_values.cpp`): a pushed `ItemSpacing` of 30 px really puts the next widgets 30 px apart and the pop brings the normal gap back, rounding, padding and border are changed inside and restored after, ignored kinds, extra pops, negative counts and a stack deeper than 32 are harmless, and a push left open does not reach the next frame.

Not done: `Alpha` (would need every draw call to fade), `FramePadding` and the text-alignment variables (would need every widget to read them), `ScrollbarSize`, per-axis `WindowPadding` (OkayUI has one value), `PushStyleColor` (there is `Style()` to set colours).

### 4.25 Gap-closing step 21: ImGui-style windows and window queries (families 11, 16)

Built: `SetNextWindowPos(pos, cond)`, `SetNextWindowSize(size, cond)` and the `Begin(title, p_open, flags)` that takes them (first-use defaults 60, 60 and 320 x 240; the user can drag it by the title bar and resize it by the corner grip); conditions `Cond_Always` (applied every frame, so the window cannot be moved or resized), `Cond_Once` / `Cond_FirstUseEver` (only the first time the window exists) and `Cond_Appearing` (each time it is drawn again after a frame without it); flags `Window_NoResize` and `Window_NoMove`; and the window queries `GetWindowPos`, `GetWindowSize`, `GetWindowWidth`, `GetWindowHeight`, `IsWindowHovered`, `IsWindowFocused` (the last window clicked), `IsWindowAppearing`, `SetWindowFocus(title)`. The editor calls `SetNextWindowPos/Size` about 154 times and the window queries about 140 times. The old `Begin(title, x, y, w, h, p_open)` is unchanged.

Tested (`test_okayui_values.cpp`): the defaults, `FirstUseEver` spent by an existing window, `Always` winning every frame, a new window taking its first-use values, hover following the pointer, click-to-focus across two windows and `SetWindowFocus`, appearing after a frame without the window (and `Cond_Appearing` placing it), `Window_NoMove` surviving a drag, a null title, queries outside a window answering zeros. Fuzz throws random positions, sizes, conditions, flags and nested `Begin` calls at it.

Not done: (since step 28, section 4.32, `IsWindowHovered` checks for a window in front and the flags exist; this paragraph is what step 21 left) docking (since step 26 there is a separate dock layer, section 4.30; `Window_NoTitleBar` exists now too), `SetNextWindowSizeConstraints`, a pivot for `SetNextWindowPos`, windows inside windows.

### 4.26 Gap-closing step 22: font scale, mouse cursor and frame facts (family 16)

Built: `SetWindowFontScale(scale)` (multiplies the text size, and with it the row heights, of the current window until its `End`; kept between 0.25 and 8, NaN ignored, a window left open never keeps its scale into the next frame); `SetMouseCursor(MouseCursor)` / `GetMouseCursor()` (the shape the UI wants this frame, reset to the arrow at the start of each frame; the host reads it after the frame and shows the matching system cursor); and `GetIO()` with the pointer, pointer delta, wheel, display size, Ctrl/Shift/Alt, the frame time (`Input::time` when the host gives it) and `wantCaptureMouse` (the pointer is over a window, or a widget is being pressed) and `wantCaptureKeyboard` / `wantTextInput` (a text field has focus), so a host knows not to also give the mouse or keyboard to a game. The editor uses these about 150 (`SetWindowFontScale`), 92 (`SetMouseCursor`) and 190 (`GetIO`) times.

Tested (`test_okayui_values.cpp`): double scale doubles the text but not the row padding, it ends with the window and does not touch the next window, NaN is ignored and 1000 is clamped, outside a window it is harmless, the cursor starts as the arrow each frame and the last one set wins and junk becomes the arrow, the IO fields follow the inputs, the mouse is wanted over either window and not over empty space, the keyboard is wanted exactly while a text field has focus and Escape ends it. Fuzz sends random scales, cursors and IO reads.

Not done: the host (shared/OkayUIHost.hpp, not on this branch) mapping `MouseCursor` to SDL system cursors, automatic cursor changes over text fields and resize grips, `wantCaptureMouse` knowing whether another window covers the pointer, the rest of the ImGui IO fields (`KeyMods`, `MouseClicked`, font atlas settings).

### 4.27 Gap-closing step 23: typed drag and drop (family 12)

Built: the ImGui way of dragging things around: `BeginDragDropSource` / `SetDragDropPayload(type, data, size)` / `SetDragDropPreview(text)` / `EndDragDropSource` and `BeginDragDropTarget` / `AcceptDragDropPayload(type, flags)` / `EndDragDropTarget`. The source starts a drag once the pointer has moved a few pixels while pressed on the item before it; the payload is a byte copy of up to 256 bytes with a type name of up to 32 characters; the target outlines the item while a drag with a payload is over it, hands the payload over on the frame the button is released (`delivery` true) or on every hover frame with `DragDrop_AcceptBeforeDelivery`, a mismatched type is not accepted and a null type takes anything. The older int-payload `DragSource` / `DropTarget` keep working and `IsDragging()` covers both. The editor calls these about 90 times (asset paths, object pointers, reorder rows).

Tested (`test_okayui_values.cpp`): a small move does not start a drag, a real drag starts and shows the preview, the target sees the payload while hovering with `AcceptBeforeDelivery` and gets it once on release, a mismatched type is refused and a null type is accepted, releasing over nothing delivers nothing, a payload of 300 bytes is refused, calls outside a drag are harmless. Fuzz throws random sources, targets, sizes, types and out-of-order calls at it.

Not done: widgets drawn inside the source block are drawn in the window (not in a floating box that follows the pointer; use `SetDragDropPreview` instead), drag sources from the keyboard, dropping onto a window title, `AcceptNoDrawDefaultRect` and the other accept flags, several drags at once, a payload bigger than 256 bytes (pass an index or an id).

### 4.28 Gap-closing step 24: clipboard and copy / cut / paste in text fields (family 6)

Built: `SetClipboardText` / `GetClipboardText` (up to 4095 bytes, never cut inside a UTF-8 character; mirrored to the system clipboard when a window system is running, kept inside OkayUI when there is none, so it works in tests and on servers) and `Input::copy` / `cut` / `paste` (the host sends them for Ctrl+C / X / V). A focused text field copies its selection, cuts it (not when read-only) and pastes at the caret or over the selection; new lines, tabs and carriage returns in pasted text become spaces because the field is one line; what does not fit the buffer is dropped whole characters at a time; pasting an empty clipboard changes nothing. The editor calls `SetClipboardText` about 50 times.

Tested (`test_okayui_values.cpp`): the plain clipboard (set, read, clear), copy leaves the field alone, paste at the end, cut empties the field, copy with no selection keeps the clipboard, paste replaces a selection and flattens lines, a read-only field copies but cannot cut or paste, a paste longer than the buffer is cut at the end, a 6000-byte clipboard is cut at a character boundary, an empty clipboard pastes nothing. Fuzz sends random copy / cut / paste edges.

Not done: the host (shared/OkayUIHost.hpp, not on this branch) turning Ctrl+C / X / V into these edges, multi-line paste (there is no multi-line field), undo of a paste, images or other clipboard formats.

### 4.29 Gap-closing step 25: a code editor widget (family 14, multi-line text; also ImGuiColorTextEdit)

Built, in new files so `OkayUI.cpp` is untouched apart from three small hooks: `OkayUI::CodeEdit` (`okayui/include/okay/UI/OkayUICodeEdit.hpp`, `okayui/src/OkayUICodeEdit.cpp`), a multi-line code editor with a line-number gutter, caret and selection (mouse and keys, word and line selection, UTF-8 safe), typing, Enter with auto-indent, Tab and Shift+Tab, clipboard through step 24, grouped undo and redo, find (next, previous, highlight all, replace), bracket matching, scrolling (wheel, scroll bars, caret kept visible), a pluggable highlighter callback with a built-in C-like and OkayScript one, read-only mode, caller error markers and breakpoint marks with a click callback, and 100,000 lines at under half a millisecond per frame. The hooks: `GetFrameInput()`, `GetKeyboardFocusId()`, `SetKeyboardFocusId(id)`. Full description, the HAVE and GAP table against what the script editor uses of ImGuiColorTextEdit, the numbers and the NOT DONE list are in `docs/own_ui_code_editor.md`.

Tested: `test_okayui_codeedit.cpp` (407 checks: every editing operation, movement, UTF-8 selection edge cases, undo grouping, find, highlighting, brackets, the widget driven by synthetic mouse and keys, pixels, a golden image, a 100k-line performance check) and `test_okayui_codeedit_fuzz.cpp` (flat-string model fuzz and random mouse, wheel, keys and API calls; clean under ASan and UBSan).

Not done: IME, ligatures, multiple cursors, folding, word wrap, a code font (the cell width assumes a monospaced font; family 23), wide-character cells, autocomplete popup (the editor draws its own on the widget's geometry), a real window or GPU check, the script editor reaches it only through `ScriptEditorWidget` with the developer-only OkayUI backend (step 27 in `docs/own_ui_code_editor.md`; the default is still ImGuiColorTextEdit; the host now sends Up, Down, Ctrl+C, X, V, Ctrl and the named keys). The multi-line paste named as not done in 4.28 is now possible in this widget (the single-line text field is unchanged).

### 4.30 Gap-closing step 26: docking (family 22)

Built, in new files so `OkayUI.cpp` is touched in five small places: `okayui/include/okay/UI/OkayUIDock.hpp`, `okayui/src/OkayUIDockLayout.cpp` (the data model) and `okayui/src/OkayUIDock.cpp` (drawing and mouse). Off by default and not wired into any program.

The model, `DockLayout`, knows nothing about drawing or input. It is a binary tree of nodes (a split of two children with a direction and a ratio, or a leaf holding tabs) plus a list of floating windows; every window name is in exactly one place. Operations: `Dock(window, leaf, Center | Left | Right | Top | Bottom)` (a side splits the leaf and gives the new half to the window; a centre adds a tab), `DockRoot` (a strip along a side of the whole dock space), `Float`, `Close`, `SetActive`, `MoveTab`, `SetRatio`, `SetSplitterPos`, `SetMinSize`. A leaf that loses its last tab disappears and its sibling takes the place of their parent. `Compute(area, tabH)` gives each node a rectangle; a child never gets less than its minimum size (64 px wide, 40 px plus the tab strip high, or what a window asked for) unless the area cannot hold both, then the space is shared in proportion to the minimums, never negative and never overlapping. `Validate` checks the invariants (a valid binary tree with unique ids and consistent parent links, no empty leaf except an empty root, no window twice or missing, ratios finite in (0, 1), the chosen tab in range, node and window counts within 256 and 128). `Save` writes a line-based text blob (`okaydock 1`, `root`, `node ... split|leaf`, `float ...`; names escaped; floats with nine digits so the text round-trips exactly) and `Load` reads one back. `Load` and `Repair` rebuild the layout from whatever they are given: broken links, cycles, repeated names, NaN and out-of-range numbers and empty leaves are dropped or clamped, a window found in a broken part is kept floating, and the number of fixes is returned; only a blob without the header is refused (the layout is left alone).

The immediate-mode layer drives one layout kept in the OkayUI context (`GetDockLayout()`): call `DockSpace(area)` once a frame before the windows (the area is cut to the display), then `DockBegin(title, &open, flags)` / `DockEnd()` in place of `Begin` / `End`. A window the layout has not seen starts floating. A docked window has no title bar of its own and fills its node's content area (below a tab strip drawn with the existing `Tab` widget plus a close box); only the chosen tab of a node returns true from `DockBegin`. Dragging a tab inside its strip reorders, dragging it out of the strip tears it off into a floating window that follows the pointer; dragging a floating window by its title bar (or the torn-off one) shows drop targets under the pointer: the centre or the tab strip of a node joins its tabs, the outer quarter of a node docks to that side, a 10 px band along the edge of the dock space docks against the whole tree, with a translucent preview of the result and guide boxes, and releasing docks. Splitters drag with the resize cursor and respect the minimum sizes. Clicking the close box removes the window from the layout and sets its `*open` false. `DockSaveLayout()` / `DockLoadLayout(blob)` wrap `Save` / `Load`; `GetDockDrop()` says what a drag in progress would do. The dock ignores the pointer where a floating window was drawn the frame before.

Hooks in existing files: `Window_NoTitleBar` (new `WindowFlags` value, honoured by `Begin(title, p_open, flags)`: no title bar, collapse, close or drag handle) with a `g_noTitleNext` flag in `OkayUI.cpp`; the window slot table grew from 16 to 64 entries (docked windows use slots too); `ResetState` calls `DockReset()`.

Tested: `test_okayui_dock.cpp` (356 checks: every operation on the model and the tree shape it gives, collapse when a leaf empties, no-op cases, limits, geometry and minimum sizes and tiny or NaN areas, save and load round trip as exact text, a dozen kinds of damaged blob, `Repair` of a deliberately broken layout; then the dock space driven with synthetic mouse frames: content rectangles, tab choice, dock by dragging a title bar onto the left of a node, onto its centre and onto the bottom edge, no dock on a short click or a release over nothing, tab click, reorder, close, tear off and drop, splitter drag held at the minimum on both sides and at a window's own minimum, cursors, save and load through the UI, `ResetState`, and two golden images `dock_layout.png` and `dock_drop_preview.png`) and `test_okayui_dock_fuzz.cpp` (random sequences of dock, dock to the root, float, close, choose tab, reorder, resize, compute, save and load on the model; random and byte-damaged blobs; random mouse frames, layout edits and blob loads against the dock space; invariants: the tree validates after every step, the set of windows is exactly what the operations say, leaves stay inside the area and never overlap, a loaded layout saves and loads to identical text, nothing is lost from a blob; clean under ASan and UBSan, a few hundred thousand checks).

Not done: multi-viewports (windows outside the main window need OS windows from `okay::platform`); the `DockBuilder*` call shapes (the layout API is its own); a central node, node flags, auto-hide tab bars; z-order: done in step 28 (section 4.32: floating windows stack and take clicks in order, and docked content sits below them); plain `Begin` windows cannot dock (use `DockBegin`); windows are keyed by title, so a renamed window is a new one (a hidden `##id` suffix is not shown in tabs); a tab strip with too many tabs shrinks them instead of scrolling; a window's minimum size is set with `DockLayout::SetMinSize`, `DockBegin` does not derive it; docked content has no scroll bar of its own (use `BeginChild`); the layout blob stores floating positions but not window contents; the editor's saved layout format is not read; not run in a real window, with a GPU or on Windows; the editor does not use any of it.

### 4.31 Gap-closing step 27: text input composition (family 23, and family 14)

Built: three fields on `Input` (`composition`, `compositionCaret`, `commit`), the calls `GetComposition()`, `GetImeRect()` and `SetImeRect()`, and support in `TextField` / `InputText` and `OkayUI::CodeEdit`. API only; there is no operating-system code, so a host has to forward its input method's events (SDL's text-editing event, Win32 `WM_IME_COMPOSITION`) into `Input` and read the rectangle back.

The preedit is a state: while the user composes, the host passes the current text every frame in `Input::composition` (UTF-8) and the caret inside it as a byte offset in `compositionCaret` (-1: at the end). Null or empty ends the composition; with no commit that is a cancel and the text was never touched. The finished text comes once in `Input::commit` and is inserted like typed text, replacing a selection. While composing, the focused field shows the preedit underlined (with a faint wash) at the caret, in place of a selection (the selected text is hidden, not deleted, so a cancel leaves it), and draws its caret inside the preedit; Backspace, Delete, the arrows, Home, End, Enter, Escape, select-all and clipboard keys are left to the input method and do nothing. `GetComposition()` returns the cleaned preedit: printable valid UTF-8 only, at most 256 bytes cut at a whole character, a caret that fell inside a character moved to its start. `GetImeRect()` gives the caret rectangle of the focused text input in screen pixels (kept inside the field) and the preedit's span, valid for the frame just built; the host puts the system candidate window there. `CodeEdit` does the same (the preedit in cell units, the rest of the line shifted right, the view scrolled to keep the preedit's caret visible, `ImeCaretRect()`, `ImeCompositionRect()`, `Composing()`); a commit drops newlines and tabs and turns invalid bytes into `?` like typed text, and undoes in one step. Read-only fields and editors ignore composition and commits; an unfocused field ignores both. A commit that does not fit the buffer is cut at a whole character.

Tested: `test_okayui_compose.cpp` (cleaning of the preedit: control and invalid bytes, surrogates and overlong forms, 256-byte cap, caret snapping inside a 3-byte character; text field: preedit not in the buffer, underline pixels present and gone on cancel, commit, commit with the preedit still sent, commit in the middle, replace a selection, cancel keeping it, partial selection, keys ignored while composing and working again after, read-only and unfocused, buffer limit by whole characters, invalid and cut bytes in a commit, the caret rectangle with and without composition and for a long preedit; `CodeEdit`: the same plus undo of a commit in one step, multi-line selection replaced, newline in a commit dropped, scrolling; two golden images `compose_field.png` and `compose_code.png`; a fuzz of random preedits, carets, commits, text, keys and read-only flips against both widgets checking the text stays valid UTF-8 and the rectangle finite; clean under ASan and UBSan).

Not done: the host side (nothing sends `Input::composition` yet and nothing moves a candidate window), the selected segment inside a preedit (SDL gives a length; only the caret is kept), reconversion and surrounding-text queries, wide-character cells in `CodeEdit` (the preedit is measured and rounded up to cells; CJK text is not laid out as double-width), right-to-left and combining marks, composition while the field is scrolled so far that the caret is out of view in a read-only state, a check with a real input method or on Windows.

### 4.32 Gap-closing step 28: window stacking and window flags (families 8, 22, 11)

Built. Hooks in `okayui/src/OkayUI.cpp` (the stacking has to reorder the draw batch, which lives there); `okayui/src/OkayUIDock.cpp` passes `Window_NoBringToFront` for docked windows.

Stacking. Every window slot has a stamp (`WinSlot::z`): higher draws in front. A new window, or one drawn again after a frame without it, gets the top stamp and focus unless it has `Window_NoFocusOnAppearing`; a left press on the topmost window under the pointer (a window with a higher stamp covering the pointer does not count) focuses it and gives it the top stamp; `SetWindowFocus(title)` does the same. Windows begun this frame register their rectangles, last frame's rectangles stand in for the ones not begun yet, and `hit()` (which every widget calls) returns false where a window with a higher stamp than the widget's own window covers the pointer. So a click never reaches the widgets, title bar, close button, collapse caret, resize grip, scroll bar or text of a window behind it, nor docked content under a floating window; `IsWindowHovered` agrees (it is false when a window in front covers the pointer) and so does the dock's own title-bar drag start. Docked windows have stamp 0 (`Window_NoBringToFront`): below every floating window, never raised by a click. Widgets with explicit coordinates outside any window are not occluded (they are the HUD layer, as before).

The picture follows the stamps. Each window's geometry is an index range of the draw batch; at the end of the frame (`reorderWindows`, before the overlay is merged) the windows' ranges are put in stamp order, with the free geometry between windows keeping its place in the call order, and the clip spans are rebuilt for each range. A frame whose stamps agree with the call order is left byte for byte as it was (so every existing golden image is unchanged), and if reordering would need more than 256 spans the frame keeps the call order (the span table grew from 128 to 256 entries). The vertices are not moved, only the index order, so the Direct3D 11 backend (indices only) and the SDL backend (spans) both get the new order.

Flags (OkayUI's own values, not Dear ImGui's numbers): `Window_NoScrollbar`, `Window_NoBackground` (no body and border; the title bar stays unless `Window_NoTitleBar`), `Window_NoCollapse` (no caret, the whole strip drags), `Window_AlwaysAutoResize` (the size is the content's size as measured on the previous frame, widest item and lowest item plus padding; the first frame only measures and is dropped from the batch; full-width widgets keep a window at the width it started with, text and labelled buttons shrink it; there is no resize grip), `Window_NoFocusOnAppearing`, `Window_NoBringToFront`. Windows begun with `Begin(title, p_open, flags)` (not the older `Begin(title, x, y, w, h)`) now scroll: when the content went past the window's bottom edge last frame (the bottom padding may be eaten before it counts) the content is clipped, the wheel scrolls it while the pointer is over the window and no scrolling child took the wheel, and a bar at the right edge drags (not with `NoScrollbar`, which keeps the wheel). The older `Begin` never scrolled and still does not, so existing windows and goldens are unchanged. New in the header: `OkayUI::GetID(label)` (the id a widget of that label would get in the current ID scope).

Tested: `test_okayui_windows.cpp` (89 checks: a window in front hides the button of the one behind, a click on the uncovered part raises it and focus follows, the same spot then reaches the button; the picture after a raise equals the picture of calling the windows in the other order from the start, pixel for pixel, in the classic and launcher looks, with the command list tiling the index buffer; dragging a title bar through a window in front; `NoBackground`, `NoCollapse`; scrolling with and without the bar, the wheel stopping at the end and not outside the window, the thumb drag, a window that fits never scrolling, a scrolling child taking the wheel first; `AlwaysAutoResize` (first frame not shown, size follows content up and down, bottom and width equal the content plus padding); `NoFocusOnAppearing`, `NoBringToFront`; docked content under a floating window, closed and reopened; `IsWindowHovered` through windows and `SetWindowFocus`) and `test_okayui_windows_fuzz.cpp` (random windows with random flags, some docked, shown and hidden, random moves, presses, releases and wheel over 300 frames per round, 120 rounds by default: a button only fires in the window the test's own model says was topmost under the pointer at the press, at most one floating window is hovered, the draw list is valid every frame, the software rasteriser is run now and then; about 155,000 checks, clean under ASan and UBSan with 200 rounds).

Not done: windows inside windows (a `Begin` inside another closes the first); the one-frame lag when a window appears or moves (windows begun earlier in the same frame cannot know it covers them yet; the fuzz skips those frames); a widget that hangs out of a small window is drawn and clicked over what is below the window (the window does not clip in the classic look; this is how it always was); `CodeEdit` scroll bars test the pointer directly and do not know about windows in front (that file was left alone); `SetNextWindowSizeConstraints`; window scroll calls (`SetScrollY`, `SetScrollHereY`); saved window settings; a modal popup blocks input to everything by its own rule and does not interact with the stamps; not run in a real window, with a GPU or on Windows.

### 4.33 Gap-closing step 29: the full colour set and the colour widgets (families 6, 18, 5)

Built. Hooks in `okayui/src/OkayUI.cpp`: the colour table and the push / pop / set functions replace the eight-colour stack (`themeColor`, `ColorSave`, `PushStyleColor`), `pick(id, default)` calls in the widgets listed below, the colour row (`ColorButton`, `ColorEdit3`, `ColorEdit4`) rewritten, the old `ColorPicker3` removed. New file: `okayui/src/OkayUIColor.cpp` (the picker, built on the public calls only).

The colours. `Col` now has 58 ids: the original eight (`Col_Bg`, `Col_BgHover`, `Col_BgDown`, `Col_Border`, `Col_Text`, `Col_Panel`, `Col_Track`, `Col_Accent`; the first three are also `Col_Button`, `Col_ButtonHovered`, `Col_ButtonActive`) and the Dear ImGui set: `TextDisabled`, `WindowBg`, `ChildBg`, `PopupBg`, `BorderShadow`, `FrameBg` / `Hovered` / `Active`, `TitleBg` / `Active` / `Collapsed`, `MenuBarBg`, `ScrollbarBg` / `Grab` / `GrabHovered` / `GrabActive`, `CheckMark`, `SliderGrab` / `Active`, `Header` / `Hovered` / `Active`, `Separator` and its two states, `ResizeGrip` and its two states, `Tab` / `Hovered` / `Active` / `Unfocused` / `UnfocusedActive`, `DockingPreview`, `DockingEmptyBg`, `PlotLines` / `Hovered`, `PlotHistogram` / `Hovered`, `TableHeaderBg`, `TableBorderStrong`, `TableBorderLight`, `TableRowBg`, `TableRowBgAlt`, `TextSelectedBg`, `DragDropTarget`, `NavHighlight`, `NavWindowingHighlight`, `NavWindowingDimBg`, `ModalWindowDimBg`. A colour that was never set follows the base theme field it comes from (`Col_TextDisabled` follows `Style().textDim`, `Col_FrameBg` follows `Style().field`, `Col_TitleBg*` follows `titleBg`, and so on; `GetStyleColorVec4` answers the classic look's value); a set one (`PushStyleColor`, in bytes, packed or as a float vector, or `SetStyleColor` until `ResetStyleColor`) overrides it, and a pop gives back whatever was there (set or following). `Theme` got `colors[64][4]` and `colorSet`. Also `GetStyleColorName`, `ColorConvertU32ToFloat4`, `ColorConvertFloat4ToU32`, `ColorConvertRGBtoHSV`, `ColorConvertHSVtoRGB`. With every colour unset every widget draws the pixels it drew before (all goldens are unchanged).

Which widget draws with which colour (everything else is stored and read back but no widget uses it yet): `TextDisabled` (dim text everywhere, `TextDisabled()`, placeholders, shortcuts, chevrons), `WindowBg` (window bodies, `Panel`), `ChildBg`, `PopupBg` (popup bodies, menu rows), `FrameBg` / `Hovered` / `Active` (text fields: idle, pointer over, focused), `TitleBg` (window title bars), `TitleBgActive` (the focused window), `TitleBgCollapsed`, `MenuBarBg`, `ScrollbarBg` and the three grab colours (child regions, window scroll bars, table bars), `CheckMark` (checkbox and radio), `SliderGrab` / `SliderGrabActive` (slider handles), `Header*` (collapsing headers, tree nodes, selectables, menu rows, sortable table headers), `Separator`, `ResizeGrip*` (the window grip: idle, hot and dragged), `Tab` / `TabHovered` / `TabActive` (the `Tab` widget and so the dock's tab strips), `DockingPreview`, `DockingEmptyBg`, `PlotLines` and `PlotHistogram` and their hovered colours (the new plots; the old plain calls use the first two only), `TableHeaderBg`, `TableBorderStrong` (column borders), `TableBorderLight` (the hairlines between rows), `TableRowBg` / `TableRowBgAlt`, `TextSelectedBg`, `DragDropTarget`, `ModalWindowDimBg`, and the original eight as before. Stored only: `BorderShadow`, `SeparatorHovered` / `Active` (no separator is interactive), `TabUnfocused` / `UnfocusedActive`, the four `Nav*` and `NavWindowing*` colours (there is no keyboard navigation of widgets). In the launcher look many widgets draw translucent overlays by default (a faint white on hover); those overlays are what a set `Header*` or `Tab*` colour replaces, and `GetStyleColorVec4` still reports the classic value for an unset colour.

Colour widgets. `ColorButton` draws the colour over a checkerboard where the alpha is below 1 (`ColorEdit_AlphaPreviewHalf`: left half with the alpha, right half opaque). `ColorEdit3` / `ColorEdit4` (one row: swatch, channel sliders, label) take flags `NoAlpha`, `NoPicker`, `NoInputs`, `NoLabel`, `Hex` (a text field `#RRGGBB` or `#RRGGBBAA` in place of the sliders; a typed colour applies once it is a whole colour); clicking the swatch opens a popup under it holding a `ColorPicker4` (`NoPicker` turns that off). `ColorPicker4` / `ColorPicker3` (flags also `NoSidePreview`, `NoAlphaBar`): a saturation / value square, a hue bar, an alpha bar (checkerboard under a colour-to-clear gradient), a preview strip with the current colour on the left and the colour the picker appeared with on the right (click the right half to go back), a hex field, and R G B A cells (0 to 255, drag or Ctrl+click to type). It keeps hue and saturation between frames, so a grey or black colour does not lose them, and it does not write the floats at all on a frame where nothing was edited; an alpha edit leaves RGB untouched. `ColorToHex` and `ColorFromHex` are public. The window draw list now draws where the widgets around it do, so a draw-list picture inside a popup is in the popup's layer.

Tested: `test_okayui_color.cpp` (9,514 checks): the ids and names, defaults following the base fields, every id pushed and popped back to its default, nesting, packed and float overloads with NaN and infinite components, set and reset, unknown ids, an unbalanced push undone at the next frame, the launcher look clearing set colours; the conversions on a grid and out of range; hex round trips and refused texts; about 25 pixel tests, one per wired colour or family (title bars idle, focused, collapsed; field idle, hovered, focused and its selection; check mark and radio; slider grab idle and held; headers idle, hovered and held, tree node and selectable hover, separator; scroll bar track, thumb and hovered thumb; child, popup and the modal dim; tabs idle, hovered, selected; menu bar, table header, banded rows, row and column borders, plots, the resize grip; dock preview and empty dock; the drop target outline); `ColorEdit` (checkerboard, no alpha, no inputs, hex typing, the popup opening, dragging inside it, closing it by a click outside, no picker); the picker (square corners, clamping, hue bar, grey keeping its hue, alpha bar, original preview, hex typing and garbage, golden `colorpicker.png`); hostile input (null pointers, NaN, infinities, huge flags, outside any window); a fuzz of random flags, floats and mouse over a picker, two editors and a button in both looks; clean under ASan and UBSan.

Not done: an HSV or float display mode for the cells, the picker's wheel layout, right-click copy and paste of a colour, drag and drop of a colour, a `ColorEdit` with a custom format; colours for widgets that do not exist yet; `PushFont`; the stored-only ids above; not run in a real window or on Windows.

### 4.34 Gap-closing step 30: multi-line text, plots, tree and selectable flags, progress (families 14, 24, 17, 13, 5)

Built, in new files except small edits: `okayui/src/OkayUIText.cpp` (`InputTextMultiline`), `okayui/src/OkayUIPlots.cpp` (the plots with options); in `okayui/src/OkayUI.cpp` the `TreeNodeEx`, `Selectable`, `ProgressBar` and `ArrowButton` bodies.

`InputTextMultiline(label, buf, cap, size, flags)`: a text box over the caller's buffer, built on `CodeEdit` with the gutter, highlighting and auto-indent off and a palette taken from the theme (`FrameBg`, `Text`, `TextSelectedBg`, the scroll bar colours). Enter breaks the line, Tab inserts a tab character, selection by mouse and keys, copy, cut, paste, undo and redo, wheel and bars, IME composition, `InputText_ReadOnly`. The caller's buffer is the truth: if it changes behind the box's back the box takes the new text; the buffer is rewritten only when the user edits; an edit that does not fit is cut at a whole character (the undo history restarts then). The editor objects live in a table of 16 keyed by label (the least recently drawn is forgotten). The last item after the call is the box, so `GetItemRect` and `IsItemHovered` work; the label is drawn to its right. `InputTextWithHint` was already there. `ArrowButton` now draws a triangle instead of a character, so it looks the same in the launcher font.

Plots with options: `PlotOptions { valuesOffset, overlay, scaleMin, scaleMax, size }` and `PlotLines` / `PlotHistogram` over an array or a getter. The offset rotates a ring buffer, the overlay is centred at the top, values outside the scale are clamped, NaN and infinities leave a gap, more than 4,096 points are drawn from every n-th value. Hovering highlights the point or bar (`Col_PlotLinesHovered` / `Col_PlotHistogramHovered`), shows a tooltip `index: value` and returns the index into the data (-1 for none). The plain calls (`scaleMin`, `scaleMax`, `height`) are unchanged. `ProgressBar(fraction, size, overlay)`: a size, and a negative fraction is an indeterminate bar sweeping with `GetTime()`.

Tree nodes: `TreeNode_OpenOnArrow` (only the arrow toggles; `IsItemClicked` still sees a click on the label), `TreeNode_OpenOnDoubleClick` (alone: a double click toggles; with `OpenOnArrow`: the arrow also takes a single click), `TreeNode_Bullet`, `TreeNode_SpanFullWidth` (the row, its hover and selection highlight and its click area start at the window's content edge under the indent), `TreeNode_NoTreePushOnOpen` (no indent, no `TreePop`), `TreeNode_Framed` (a `Col_Header` background). Selectable: `Selectable(label, selected, flags, size)` and `Selectable(label, bool* p_selected, flags, size)` with `Selectable_Disabled`, `Selectable_ClosePopup`, `Selectable_SpanAllColumns` (in a table, the highlight and hit area span the row; the text stays in its cell), `Selectable_DoubleClickOnly`; `size.x` <= 0 is the width left, `size.y` <= 0 one row. A selectable does not close a popup unless asked (that is how it was, so the flag is opt-in, the reverse of Dear ImGui's default).

Tested: `test_okayui_widgets.cpp` (14,484 checks): the multi-line box (no gutter, typing, Enter, Backspace, a caller change taking effect, select all, copy, paste, capacity cut at 7 of 8 bytes and at a whole character, an unterminated buffer, read only, two boxes with their own focus, a long text scrolling and clipped, hostile arguments, golden `multiline.png`), hint shown only while empty, arrows pointing four ways and clicking, progress bar size, indeterminate sweep staying inside the bar, selectable size and each flag (a popup closing and staying open), each tree flag, plots (hover index and highlight, ring offset moving the picture, getter, histogram bar height, gaps and clamping, 200,000 points, all-NaN, null and negative counts), and a fuzz of random buffers, capacities with slack bytes behind them (a write past the capacity would show), keys, flags and sizes against all of it in both looks; clean under ASan and UBSan.

Not done: password mode and `InputText` callbacks, word wrap in the multi-line box, a code font (the box uses the UI font; columns line up only with a monospaced one), `EnterReturnsTrue` and `AutoSelectAll` for the multi-line box, `PlotLines` with a `stride`, `Selectable_AllowOverlap`, `SpanAllColumns` highlights beyond the cell in a scrolling table (the cell's clip applies), `TreeNode_AllowOverlap`; not run in a real window or on Windows.

### 4.35 Gap-closing step 31: tables, step three (family 16)

Built, in `okayui/src/OkayUI.cpp` where the table state lives (new `TblStore` memory; rewritten `BeginTable`, `TableNextRow`, `TableNextColumn`, `TableHeadersRow`, `EndTable`); the declarations are in `okayui/include/okay/UI/OkayUI.hpp` next to the existing table calls, which behave as before for a table without the new flags except that a row band now has the height the row had last frame.

Sorting (`Table_Sortable`, `Table_SortMulti`, `Table_SortTristate`; column flags `Col_NoSort`, `Col_DefaultSort`, `Col_PreferSortDescending`): `TableHeadersRow` makes the headers clickable (a hover and press wash; a click on a column's resize border does not count) and draws a triangle on each sort key, with its order digit when there are several. A plain click makes the column the only key (ascending, or descending with `PreferSortDescending`), clicking the only key again flips it and, with tristate, a third click removes it; Ctrl+click with `SortMulti` adds a key (up to four) or flips it, and removes it on the third click with tristate. The table never sorts anything itself: `TableGetSortSpecs()` gives `{specs[], specsCount, specsDirty}` (each `{columnUserId, columnIndex, sortOrder, descending}`; the user id is the fourth argument of `TableSetupColumn`), valid until `EndTable`; the caller sorts when `specsDirty` and clears it. `Col_DefaultSort` columns are the keys the first time a table is seen, dirty on that first frame. The keys and the dirty flag live per table id (8 tables), not in the data. Outside a sortable table the call gives null.

Scrolling (`Table_ScrollX`, `Table_ScrollY`, `BeginTable(id, n, flags, outerSize, innerWidth)`, `TableSetupScrollFreeze(cols, rows)`): the table is one item of the outer size (a ScrollY table with no height asked takes the height left, at least four rows; a sideways-only one takes last frame's content height plus the bar). The content is clipped to the box and shifted by the scroll, with a vertical bar and a horizontal bar when needed; the wheel over the box scrolls it (Shift+wheel for sideways, or the plain wheel in a sideways-only box; a scrolling child inside still takes the wheel first, and a table that takes it keeps the window from scrolling too); `TableGetScrollX/Y` and `TableSetScroll`. Frozen rows (the header row counts as one) stay at the top, frozen columns stay at the left, each in its own clip; body rows far outside the box are "culled" to one clip so a 300-row table makes a few dozen draw commands, not hundreds. Widgets in scrolled cells hit-test through the clip, so a button half under the frozen header is only clickable where it shows. With `Table_ScrollX` the columns keep their pixel widths (fixed columns exactly; stretch columns share `innerWidth`, or the box width); a border drag then grows the column on its left.

Row backgrounds: `Table_RowBg` alternates `Col_TableRowBg` (clear unless set) on even rows and `Col_TableRowBgAlt` on odd rows, each band as tall as that row was last frame (one text row on the first frame; before this a taller row got a one-row band), and `TableSetBgColor(TableBg_RowBg0 | RowBg1 | CellBg, color, column)` paints the row or a cell; call it right after `TableNextRow` / `TableSetColumnIndex` and before the widgets, which then draw on top (the two row targets just paint, one over the other in call order).

Tested: `test_okayui_tables.cpp` (19,427 checks): unsorted start, ascending, descending, replacing by another column, the data coming out in the order the specs say, no re-sort on later frames, the arrow drawn on the sorted column only and different for the two directions, tristate cycle, multi-key add, flip and replace (and Ctrl doing nothing without `SortMulti`), default sort and descending preference on the first frame, `NoSort`, a non-sortable table giving null specs and ignoring clicks, a border click not sorting, two tables keeping their own keys; a 40-row ScrollY box (box size and the item after it, bar drawn, nothing past the box, wheel scrolling, header unchanged and body moved, stop at the end, wheel outside the box ignored, thumb drag, a button half under the header clickable only where it shows), a frozen first column and header with sideways scroll (the frozen column and header do not move, the rest does, nothing past the box, limits, a click after scrolling), 300 rows in a frozen scrolling box (fewer than 120 draw commands, tiling the index buffer), row bands (even, odd, replaced, full height of an 80 px row, the next row's start, a cell colour and its untouched neighbour), goldens `table_scroll.png` and `table_scroll_launcher.png`, and a fuzz of 60 rounds in both looks with hostile arguments (NaN and huge sizes, widths and scroll, 20 columns asked of 16, an unclosed table) checking the draw list stays valid and finite; clean under ASan and UBSan.

Row clipper (a later change to step 31): a table row wholly outside what can be seen still lays out (its cells are entered, so row heights, column widths, ids and the scroll limits stay exact) but emits no geometry: no quads, glyphs, textured quads or triangles while `g_cullRow` is set, which `TableNextRow` sets from the row's screen top and its remembered height. The band tested is the scrolling box's body below the frozen rows, else the enclosing clip rectangle, else the display; below the band is certain, above it allows four row heights of slack for a row taller than its remembered height. Frozen rows, the header, overlay content (popups, tooltips) and every row of a table with no clip and no display size are never culled. `TableRowIsVisible()` tells a caller its row was skipped, so expensive cell content can be skipped too (the cell still has to be entered); `SetTableRowClipping(false)` turns it off for comparison. Before this a 5,000-row three-text-column ScrollY table filled the 262,144-vertex batch and silently lost shapes (1,081,824 in the test table with row bands and borders); now it emits about as many vertices as the rows on screen, `DrawData::dropped` is 0 at the top, in the middle and at the end, and the picture is pixel-identical to the unclipped one (compared at three scroll positions in both looks, plus the two existing table goldens unchanged). Tested in `TestRowClipper` in `okayui/tests/test_okayui_tables.cpp`. Not done: a plain `ListClipper` for content that is not a table (a long list of `Selectable`s still emits every item), culling by column, and a row taller than four times its remembered height above the box on its first frame.

Not done: column reordering, hiding and the header context menu, auto-fit by double click on a border, `TableSetColumnEnabled`, `TableGetHoveredColumn`, outer and inner vertical borders as flags, sizing policies beyond fixed and weighted, nested tables, row selection helpers, keyboard navigation, a horizontal bar for a ScrollY-only table; a frozen-column set wider than 90% of the box is dropped; the row-height memory holds the first 256 rows (later rows get a one-row band); `Selectable_SpanAllColumns` in a scrolling table is limited by the cell's clip; not run in a real window or on Windows.

### 4.36 Gap-closing step 32: the ImGui-shaped names (families 1, 11, 8, 9, 16, 20, and the type coverage of section 6)

Goal: a panel written for an immediate-mode toolkit with ImGui-shaped calls should port with its structure unchanged. Section 6.1 measured the types, flags and enums as the real blocker (75.7% of their uses had an OkayUI name). This step adds the names and the calls that take them. Written from how the calls are used in our own editor and from general knowledge of how such toolkits behave; nothing is copied or ported from Dear ImGui or any other project (CLAUDE.md rule 19a).

New header `okayui/include/okay/UI/OkayUIImCompat.hpp`, definitions in `okayui/src/OkayUI.cpp` (they need the context's private state). Each enum says in the header which flags are really honoured and which are only accepted so the call compiles.

- **Mouse.** `Input::middleDown` (the host calls `OkayUIHost::Build` and the editor copies only some fields of its result, so NO real window forwards the middle button yet: it works in the headless tests only; the editor and launcher call sites still need a `middleDown` argument), `MouseButton_Left/Right/Middle`; `IsMouseDown`, `IsMouseClicked`, `IsMouseReleased`, `IsMouseDragging` and `IsItemClicked` now take the middle button too; `GetMouseDragDelta(button, threshold)` and `ResetMouseDragDelta`.
- **Hover and focus flags.** `IsItemHovered(flags)`, `IsWindowHovered(flags)`, `IsWindowFocused(flags)`, `IsAnyItemHovered`. Honoured: `AllowWhenDisabled`, `AllowWhenBlockedByPopup`, `AllowWhenBlockedByActiveItem`, `AllowWhenOverlapped`, `RectOnly`, `AnyWindow`, `Stationary`, `DelayShort` (0.15 s), `DelayNormal` (0.4 s), `ForTooltip`. `ChildWindows` / `RootWindow` are the same as none because a child region is part of its window here. The flagged calls are false while another item is held, as in toolkits of this kind; the plain `IsItemHovered()` is unchanged.
- **Child windows.** `BeginChild(id, Vec2 size, childFlags, windowFlags)`: `Borders` (the panel behind it; without it there is none), `AutoResizeY` (last frame's content height plus the padding), `FrameStyle`, negative sizes. `AlwaysUseWindowPadding`, `ResizeX/Y`, `AutoResizeX`, `NavFlattened` are accepted and do nothing.
- **Edit marks.** `IsItemEdited`, `IsItemDeactivatedAfterEdit` (slider, drag, check box, radio, combo, number and text fields, colour editors): every value widget reports its change through one small function, and the mark lives until the item is let go (or, for a text field, loses the keyboard).
- **Item width.** `CalcItemWidth` (what is left, or `SetNextItemWidth` / `PushItemWidth`).
- **Tooltips with widgets in them.** `BeginTooltip` / `EndTooltip` / `BeginItemTooltip`: an overlay box that follows the pointer, flips to stay in the display, is sized from last frame's content, and never takes the pointer (it registers no blocking overlay, so the item under the pointer stays hovered).
- **Popups.** `BeginPopupContextWindow(id, popupFlags)`, `BeginPopupContextItem(id, popupFlags)` (left, right or middle button, `NoOpenOverItems`), `IsPopupOpen(id, flags)` with `AnyPopupId`.
- **Draw flags.** `DrawList::AddRect(min, max, col, rounding, drawFlags, thickness)` and `AddRectFilled(..., rounding, drawFlags)` with `RoundCornersTop / Bottom / Left / Right / the four single corners / None` (the corners are drawn as a path), `AddNgon`, `AddNgonFilled`; `DrawFlags_Closed`.
- **Names only.** `Key_*` (spelled as ImGui code spells them, on `KeyId`), `Dir_*`, `ID`, `TextureID`, `PopupFlags`, `ButtonFlags`, `FocusedFlags`, plus a `KeyId_Slash` (so the code editor's Ctrl+/ has a key).

What the usage report says after this step (`python3 scripts/imgui_usage_report.py`, by name, so generous): types, flags and enums 80.4% (the figure on this branch's base; section 6.1 had 75.7% before other work) to 97.3% of their uses; `ImGui::` calls 98.8% to 99.1%; draw-list methods 98.7% to 99.1%. The report script now also maps `ImVec4`, `ImTextureID`, `ImDrawFlags_*` and OkayUI's short spellings `Col_*`, `Table_*`, `Tab_*` for the table and tab flag families, which it did not before. What is still missing is listed in the report: it is now mostly Dear ImGui's own internal types (`ImGuiWindow`, `ImGuiPayload`, `ImRect`, `ImFont`, `ImGuiContext`, `ImGuiDockNode`), `GetStyle` (87 uses of a struct OkayUI does not mirror), the context and viewport calls, `SetNextWindowDockID` / `GetWindowDockNode` (the editor's own dock bookkeeping), and a handful of flags (`InputTextFlags_Password` and the callbacks, `SliderFlags_Logarithmic`, `ListClipper`).

Two behaviours worth knowing: a child begun through the new `BeginChild(id, size, childFlags)` does not count the gap after its last item as content (the one-call `BeginChild` still does, so existing screens and goldens are unchanged), which keeps a child that just fits from growing a scroll bar; and `PlotLines` / `PlotHistogram` now hide everything after `##` in their label, as every other widget does (the Stats port found it: the label `##fps` was being drawn next to the plot).

Tested: `okayui/tests/test_okayui_imcompat.cpp` (114 checks, headless, synthetic input, three golden pictures): the names and their relations, the middle button, drag deltas with the threshold and the reset, every honoured hover flag (disabled, held item, delay and stationary timing in frames, leaving resets the delay), window hover and focus flags, child borders / auto resize / negative sizes / hostile numbers, edit marks for a slider, a check box, a drag field that was not moved, and a text field (typing, then losing the keyboard), `CalcItemWidth` with and without overrides, a tooltip with widgets (drawn near the pointer, not nestable, harmless unbalanced end), context popups (right and left button, `NoOpenOverItems`, closing by a click away) and the rounded-corner and n-gon pixels.

### 4.37 Gap-closing step 33: tabs and tables, the rest of what the editor calls (families 20 and 16)

Tabs (`OkayUI.cpp`, next to `BeginTabBar`): `BeginTabBar(id, flags)` with `TabBarFlags_Reorderable` (drag a tab along the bar and it swaps with the tab under the pointer; the order and the widths are remembered per bar, so tabs are placed from last frame's widths, one frame late for a new tab), `AutoSelectNewTabs` (a tab that appears after the bar's first frame becomes the chosen one), `NoCloseWithMiddleMouseButton`; a closable tab closes with a middle click; `TabItem_UnsavedDocument` draws a dot; the other flags are accepted. Tabs that disappear leave no gap. The ordering rule is a small public pure function, `TabDragTarget(widths, n, from, left, gap, mouseX)`, tested alone and through a bar.

Tables (flag-style tables, `BeginTable(id, n, flags)` + `TableSetupColumn`): column flags `Col_NoResize` (the borders on either side cannot be dragged), `Col_DefaultHide`, `Col_NoHide`, `Col_NoHeaderLabel`, `Col_WidthStretch` (the default, accepted); `Table_Hideable` (a right click on the header row opens a menu with a check box per column; a `NoHide` column and the last visible one are locked); `TableSetColumnEnabled`, `TableGetColumnIsEnabled`, `TableGetColumnCount`, `TableGetColumnName`, `TableGetColumnFlags`, `TableGetHoveredColumn`. The hidden set is remembered per table. A hidden column has no width, header or border, the resize handle next to it works on the nearest visible column, `TableNextColumn` / `TableSetColumnIndex` return false for it, and anything drawn in its cell lands far off screen (so it is never seen or hit). Accepted and doing nothing: `Table_Resizable` (columns are resizable unless `Table_NoResize`, the reverse of the usual default), `Table_Reorderable`, the border and sizing-policy flags, `Col_NoReorder`, `Col_NoClip`, `Col_IndentEnable`.

Tested in `test_okayui_imcompat.cpp` (and the fuzz, see below): hide from code and show again (the layout comes back to the pixel), the third column taking the hidden one's place, a locked column refusing, hiding every column refused, a column that starts hidden, the header menu opening on a right click, `NoResize` against a normal border drag, hostile column numbers.

Not done: column reordering by dragging a header (the flag is accepted), auto-fit by double click on a border, tab overflow (a scroll strip or a tab-list popup: tabs past the bar's width are cut off), tab tooltips.

### 4.38 Gap-closing step 34: dock layouts written as code (family 22)

`okayui/include/okay/UI/OkayUIDockBuilder.hpp`, `okayui/src/OkayUIDockBuilder.cpp`. The editor builds its layout presets with a builder: make a root node, split it, name the new parts, dock windows into them (`BuildLayoutPreset` in `editor/main.cpp`). OkayUI's dock had no such way in. Written from that usage and from OkayUI's own `DockLayout`:

- `DockBuilderTree` (pure, no drawing): `AddNode`, `RemoveNode`, `SetNodeSize`, `SplitNode(node, dir, ratio, &side, &opposite)`, `DockWindow`, `GetNode`, `Validate`, and `Build(root, DockLayout&)` which turns the tree into the dock model: windows of a leaf in tab order with the first chosen, splits with their sides and ratios, parts with no windows dropped (the model has no empty leaves), and the result passes `DockLayout::Validate`.
- The same calls over one tree in the context under the usual names: `DockBuilderAddNode`, `DockBuilderRemoveNode`, `DockBuilderSetNodeSize`, `DockBuilderSplitNode`, `DockBuilderDockWindow`, `DockBuilderGetNode`, `DockBuilderFinish(root)` (builds into `GetDockLayout()`, so the next `DockSpace` shows it).

How it differs, plainly: node ids are plain integers (`GetID("name")` is the usual way to pick the root's); `SplitNode` makes two new leaves and moves what the node held into the opposite one, so the id you split stays the container (the common `split(from, dir, ratio)` helper that writes the opposite id back into `from` works as it does elsewhere); node sizes are kept but do not change the result (ratios do the work); there are no dock node flags; the editor's own dock bookkeeping (`SetNextWindowDockID`, `GetWindowDockNode`, undocking by id, ini save in memory) is NOT covered. Save and restore of a layout was already there (`DockSaveLayout`, `DockLoadLayout`; section 4.30).

Tested in `okayui/tests/test_okayui_dockbuilder.cpp` (52 checks, one golden picture, `docs/images/okayui/dockbuilder_tall_preset.png`): the tree (split a leaf, split a split, windows moving, removing a leaf collapses its parent, bad arguments, NaN and out-of-range ratios), building (sides, ratios checked against the computed geometry, tab order and the chosen tab, an empty side dropped, an empty tree, an unknown root leaving the target alone, a Save and Load round trip with no repairs), a preset in the shape of the editor's "Tall" layout written with the context calls and drawn through `DockSpace` / `DockBegin` (the chosen tabs answer true, the content areas are where the preset says), and a fuzz of 40 seeds of random operations that must keep the tree and the built layout valid.

### 4.39 Gap-closing step 35: code editor line commands (family 14)

`OkayUI::CodeEdit` already covered everything the script editor asks of its old widget (`docs/own_ui_code_editor.md`). Three editing commands that code editors have and the "NOT DONE" list named were added: `ToggleLineComment(prefix)` (comment or uncomment the lines the selection touches, at the smallest indent, blank lines left alone, the caret stays on the same text), `MoveLines(down)` and `DuplicateLines()`; each is one undo step. Keys, only while the widget has the keyboard: Ctrl+/ toggles, Alt+Up / Alt+Down move, Ctrl+Shift+D duplicates (`KeyId_Slash` is new, and `shared/OkayUIHost.hpp` maps SDL's slash key to it). Tested in `test_okayui_codeedit.cpp` (`LineCommandTests`; the file now has 445 checks): comment and uncomment round trips, mixed lines, a selection ending at a line start, read only, moving a block, the first and last line, duplicating, undo and redo of each, and the keys through `Draw`. Not done: moved lines do not take breakpoints with them; word wrap, folding, multiple cursors, auto-close brackets stay on the list in `docs/own_ui_code_editor.md`.

### 4.40 Gap-closing step 36: the Stats window, ported (tables, plots, check boxes, tooltips)

**Superseded (merge into `claude2/okayui-editor-panels-2`, 2026-11-16):** the editor's Stats window had two OkayUI ports, this one and the one in section 9 (a shared model with the Dear ImGui body, compared in `okayui_editor_panels_imgui`: 38% fewer instructions by callgrind, 5.65% of pixels clearly different against the editor's own theme and font, and run in CI). The section 9 port is kept because it is the better measured one; this section's screen (`okayuiscreens::DrawStats(StatsModel ...)` in `OkayUIPanels`), its tests, goldens, comparison tool and pictures were removed. `CollectStatRows` stays: the section 9 model is filled from it. The toolkit pieces of this step (tooltips, hover delays, the plot label fix) all stay.

The editor's Stats window (View menu, `DrawStats` in `editor/main.cpp`) is now also drawn by OkayUI behind the developer-only opt-in backend (section 5): the big frame-rate readout coloured by band, the frame time and average / minimum, an auto-scaled plot of the last 120 frames with a 60 fps guide, the scene counts in a four-column grid, and the culling and batching switches with their hover help. Dear ImGui is still the default and still draws it unless the switch is on.

- `okayui/screens/OkayUIPanels.{hpp,cpp}`: `okayuiscreens::DrawStats(StatsModel, x, y, w, h)` plus pure helpers `FpsBand`, `HistoryStats`, `PlotScale`, `FormatCount`. Written from what the editor's window shows (its fields, thresholds and colours), using OkayUI's plot with a ring offset, a table, check boxes and the new `BeginTooltip` and hover delay flags.
- `editor/OkayPanels.{hpp,cpp}`: `DrawStatsOkayUI` hosts it exactly like the Crash Log and the Audio Mixer (the ImGui window stays as the dock tab and the close box; OkayUI gets the free area as one rectangle; with an ImGui popup over the window the ImGui version is drawn that frame).
- `editor/main.cpp`: `CollectStatRows` now builds the count rows once for both versions, so they cannot drift; `DrawStats` calls the OkayUI one first when the backend is on. Nothing else in the ImGui window changed.

Differences from the ImGui window, measured on the side-by-side picture: the rows of the count grid are about 31 pixels apart against 19 in the ImGui window (OkayUI's table cells are one full widget row high), so the OkayUI window needs about 40 pixels more height before the switches are on screen, and it scrolls when it is shorter; the readout is OkayUI's 2x text, not a separate heading font; the plot is OkayUI's (hover highlight and a tooltip on a point, which the ImGui plot does not have); the two hover helps use a tooltip box that appears after a short delay instead of at once; the "moved to the Lighting tab" line is not tinted. Tested: `test_okayui_panels.cpp` now has 103 checks (was 74): the pure helpers (bands, history statistics, plot scale, count format, with NaN and infinity), the layout, each switch editing only its own value, the hover help appearing after the delay and not before, the three frame-rate bands, an empty history and no switches, hostile numbers, a short window that scrolls, and two goldens (`stats_default`, `stats_tooltip`). Not verified: a real editor window (nothing here ran the editor; `editor/OkayPanels.cpp` and `editor/main.cpp` were only compiled with `-fsyntax-only` against the real headers); Windows.

(Removed with this port, see the note above.) How the comparison picture was made: `tools/okayui_stats_compare.cpp` drew the SAME ImGui calls (copied there with fixed data, in ImGui's default dark style and built-in font, not the editor's skin) and the real OkayUI screen side by side, both headless. Pictures: `docs/images/okayui/stats_side_by_side.png` (ImGui left, OkayUI right), `stats_okayui.png`, `stats_okayui_tooltip.png`. These are headless renders, not screenshots of the editor. The other pictures of this step are `tabs_reordered.png`, `table_hide_menu.png`, `children_tooltip_shapes.png` and `dockbuilder_tall_preset.png` (the goldens of the tests, launcher look, headless).

### 4.41 Test results and how close Dear ImGui is to removal (steps 32 to 36)

Run on Linux, Release, GCC, headless (`SDL_VIDEODRIVER=dummy`), after this step: the 22 OkayUI test programs that were built (`soft`, `behaviour`, `golden`, `drawlist`, `painter`, `values`, `codeedit`, `dock`, `compose`, `windows`, `color`, `widgets`, `tables`, `render`, `screens`, `host`, `compat`, `panels`, `imcompat`, `dockbuilder`, `script_adapter` and `fuzz`) all pass with 0 failures. The long ones: `compose` 26,370 checks, `tables` 19,486, `widgets` 14,484, `color` 9,514, `render` 8,566. `dock_fuzz` (318,334 checks), `windows_fuzz` (155,198), `codeedit_fuzz` (36) and `render_gl` (272, on a software GL) also pass. The fuzz test gained four operations that call the new API with hostile flags and sizes; they found one real crash in this step's own code (`TableGetHoveredColumn` before the table's columns were set up), now fixed. Under AddressSanitizer and UndefinedBehaviorSanitizer (the OkayUI sources and three test programs compiled with `-fsanitize=address,undefined`, linked with the normal engine library, run by hand, not by CI) `fuzz` (four seeds, up to 1,500 iterations), `imcompat`, `dockbuilder` and `codeedit` are clean. Not run: the Windows build, the Direct3D 11 check, the web build, the editor and the launcher (only `-fsyntax-only` of `editor/main.cpp` and `editor/OkayPanels.cpp` with `OKAY_HAVE_OKAYUI`), `render_bench`.

How close is Dear ImGui to removal? Not close. By name, the editor, launcher and shared code now find an OkayUI name for 99.1% of their `ImGui::` call sites and 97.3% of the types, flags and enums they use (`docs/imgui_port_report.md`; the report counts names, not behaviour, and several of the new flags are accepted and do nothing, each said in its header). That measures what a port would have to rename, not how much runs. What runs on OkayUI today, behind the developer switch: four editor windows (Console, Crash Log, Audio Mixer, Stats), the launcher pages What's New and Tips, and the script editor's code widget. The four editor windows hold 232 of about 27,300 `ImGui::` call sites (0.8%). Dear ImGui still draws everything else, still owns the windows, the editor's docking, the viewports, the fonts and the platform backend (SDL2), and is the default in every build. Between here and removal: port the other ~220 editor screens (the Inspector alone is more than a fifth of the call sites), the editor's own dock bookkeeping, `GetStyle`, internals such as `ImRect` and `ImGuiPayload`, host-side IME, a code font, multi-viewports, column reordering and tab overflow; then own window and platform code to replace SDL2; then a long run in real windows on Windows. This step moved that by one screen and removed some of the reasons a port would fail to compile.

## 5. Wiring OkayUI into the real apps (opt-in second backend)

**Developer-only.** Owner rule: no own-code replacement is turned on until it is declared ready and bug free. So this switch is not offered to users. In a normal run Dear ImGui is the only UI: the settings rows below are not shown, and a stale `ui_backend=okayui` (launcher.cfg) or `uibackend 1` (okay_settings.txt) left in a saved file is ignored. The rows appear, and the saved value is honoured, only with the developer flag `OKAY_DEV` set (the switch the editor already uses for its launcher gate and its "Test UI" menu entry; the launcher reads the same variable). `OKAY_UI_BACKEND` (an explicit developer act, also used by the tests) and the CMake option `OKAY_UI_DEFAULT_BACKEND` keep working without it.

Sections 0 to 4 built and tested OkayUI on its own. This step is the first one that runs it inside the launcher and the editor, as a second UI backend next to Dear ImGui: off by default, chosen by a setting, and used only for the screens that have an OkayUI port. Dear ImGui is still what both apps are built on and what every other page and panel uses.

### 5.1 Audit: where OkayUI was already hosted, and what that left out

Method: `grep -rn OkayUI` over `editor/`, `launcher/`, `player/` and `shared/`, then reading each host's frame loop.

| Host | What it did before this step | What was missing for a real screen |
|---|---|---|
| Editor (`editor/main.cpp`) | One `OkayUI::BeginFrame` before the scene ticks and one `OkayUI::EndFrame(renderer)` after ImGui's draw data, through the same `SDL_Renderer`. Used by a playing game's script `ui_*` calls (via `OkayScriptUIBridge`), by the built-in dialogue box and quest tracker, and by the F-key "Test UI" demo panel (only with `OKAY_DEV` set). Launcher look on, unless `OKAY_CLASSIC_UI` is set | The input it built was text and Backspace only (no arrows, Delete, Home, End, Enter, Escape, select-all, shift, wheel, right button); the mouse was `blocked` whenever ImGui wanted it, so no OkayUI widget inside an ImGui window could ever be clicked; nothing ran OkayUI for the editor's own panels |
| Launcher (`launcher/main.cpp`) | Nothing. No `okay_ui` link, no frame, no input; the launcher has its own second SDL window (the chat window) with its own ImGui context | Everything: the CMake link, a frame bracket in the main loop, input, a settings switch, a page to show |
| Player (`player/main.cpp`) | OkayUI for in-game UI only (no ImGui at all) | Nothing to wire: the player has no Dear ImGui UI to replace |
| `tests/test_okayui.cpp` | An SDL software renderer on an in-memory surface, pixel reads | Does not cover a hosted region or the new screens (the headless harness of section 2 does) |

What the frame plumbing already gave us, and was reused unchanged: OkayUI and ImGui share one `SDL_Renderer`; OkayUI's `EndFrame` saves and restores the renderer's blend mode; the editor already starts SDL text input when OkayUI wants it.

### 5.2 Design, and why it is the least risky one

Options considered:

1. **A separate window** for OkayUI screens: its own SDL window, renderer and event routing (the launcher's chat window shows what that costs: a second ImGui context, focus handling, DPI and drop handling). Rejected: most plumbing, least like the finished product.
2. **A full-window OkayUI pass** that replaces ImGui for a whole app: not possible while the other 99% of the screens are ImGui.
3. **An overlay pass inside the same renderer, filling a rectangle that an ImGui window reserves** (chosen). The ImGui page or panel stays (it is the dock tab, the sidebar entry, the title and the close box); inside it, one `InvisibleButton` takes the free area, and OkayUI draws its screen into exactly that rectangle. No new window, no new renderer, no second event loop, and it works with ImGui docking.

How a frame runs with the OkayUI backend on:

```
events:  ImGui_ImplSDL2_ProcessEvent(e)  and  okayuihost::InputCollector::Feed(e)
frame:   ImGui::NewFrame()  ->  OkayUI::BeginFrame(collector.Build(mouse, blocked, keysOk))
page:    ImGui reserves a rectangle (InvisibleButton)  ->  Region::Offer(rect, IsItemHovered)
         ->  okayuiscreens::DrawWhatsNew / DrawConsole(model, rect)  ->  actions run in the app
end:     ImGui::Render + RenderDrawData  ->  OkayUI::EndFrame(renderer)    (OkayUI on top)
```

Mouse ownership: OkayUI's frame starts before the panels are submitted, so it uses last frame's answer: `blocked = io.WantCaptureMouse && !region.Owns(mouse)`, where the region is the rectangle the page offered last frame and ImGui said was hovered (`IsItemHovered` is false while another window or a popup covers it). One frame of latency; a region not offered for a frame is forgotten. Keyboard ownership: OkayUI gets typed text and key edges only while one of its text fields has focus and no ImGui item is active; at the end of the frame the app tells ImGui (`PlatformImeData.WantTextInput`, `SetNextFrameWantCaptureKeyboard`) that the keyboard is taken, so single-key editor shortcuts and text of other widgets stay out. The part that is the same for both apps lives in `shared/OkayUIHost.hpp` (`InputCollector`, `Region`); the screens in `okayui/screens/` have no SDL and no ImGui.

### 5.3 What was built

- **The switch.** `shared/OkayUIBackend.hpp` (std only): `Backend {ImGui, OkayUI}`, `Parse`, `Name`, `Label`, `Normalize`, `Resolve`. Precedence: the environment variable `OKAY_UI_BACKEND` (`okayui` or `imgui`), then the saved setting (only with `OKAY_DEV`; otherwise ignored), then the build default. The build default is Dear ImGui; `-DOKAY_UI_DEFAULT_BACKEND=okayui` flips it (CMake cache option, validated). A build with no OkayUI compiled in answers Dear ImGui whatever is asked.
- **Where to change it (developer flag `OKAY_DEV` only; hidden in normal runs).** Launcher: Settings > Appearance > Interface > UI backend, a combo of "Dear ImGui (default)" and "OkayUI (preview)", saved as `ui_backend=` in `launcher.cfg`, applies at once. Editor: View > Rendering & Performance > UI backend (next to VSync), saved as `uibackend` in `okay_settings.txt`, applies at once. When `OKAY_UI_BACKEND` is set the control is greyed and says so.
- **Launcher port: the What's New page** (`okayuiscreens::DrawWhatsNew`). Same data and actions as the ImGui page: "You're on v...", the update status line with its colour, and the one button that fits the state (Check for updates, disabled while a check runs; Install now; Restart now), the newest release in a card with the Latest pill, earlier releases as folding headers (the first one open), the built-in highlights when no notes were fetched, and "View all releases on GitHub". The data comes from the same launcher state (`GetState`, `g_upAvailable`, `g_newsHistory`) and the actions call the same functions (`StartUpdateCheck`, `BeginUpdatedRestart`, `OpenExternal`).
- **Editor port: the Console panel** (`okayuiscreens::DrawConsole`, hosted by `DrawConsoleOkayUI` in `editor/ConsolePanel.cpp`). Same model (`okay::ConsoleModel`), same data: search text, category dropdown, Messages, Warnings, Errors toggles with counts, Collapse duplicates, Auto-scroll, Clear on Play, Pause on error, Clear, Copy all, Save to console_log.txt, per-row right-click Copy Entry and Copy All Visible, click a row to select it and to jump to its source object or asset, a details area with the full message, severity, time, Copy and Open Script (with the line). "Open Script" is found by the same function the ImGui panel now uses (`okayconsole::ScriptLink` in `shared/ConsoleScriptLink.hpp`).
- **OkayUI additions the screens needed** (all small, side by side with what existed): `BareNextWindow()` (a window with no title bar, no frame, no padding and no dragging: just a layout region that clips to the host's rectangle), `GetScrollY`, `GetScrollMaxY`, `SetScrollY` for the innermost child (applied once the content is measured), `CollapsingHeader(label, defaultOpen)`, and the `##` convention for `InputText` and `Combo` labels (what follows `##` is an id only, not shown). Existing windows are unchanged.
- **Popups stay correct.** OkayUI is flushed after ImGui, so it would paint over a menu or modal that opens above a hosted screen. `okayuihost::PopupCoversRect` (`shared/OkayUIHostImGui.hpp`) asks ImGui whether an open popup overlaps the hosted rectangle (a modal always does); while it does, the app draws that screen's Dear ImGui version instead (both apps kept it), and OkayUI takes over again when the popup closes. Seen in the real windows: the launcher's "Software Update" dialog after pressing Check for updates, and the editor's "Save before playing?" dialog. The same holds for ordinary windows: `okayuihost::WindowInFrontCovers` asks whether a floating Dear ImGui window that ImGui draws in front of the host (later in its window list, or a tooltip) overlaps the hosted rectangle, and `CoveredInImGui` combines both answers. The editor's hosted panels, the Project Settings, Lighting and Import sections and the Console use it. Before this, a History window lying behind Project Settings painted its Undo/Redo row over Project Settings' first line, and a window squeezed against the left edge painted a column of first letters down it. Checked by `tests/test_okayui_host_layering.cpp` (headless Dear ImGui: a dock space, floating windows on top of each other, a tooltip, a popup).
- **A click is never lost.** `InputCollector` remembers a mouse button that went down and up between two frames and reports it down for one frame (found in the real window: an `xdotool` click was dropped before this).
- **CMake.** The launcher now links `okay_ui_screens` (and so `okay_ui`) when SDL2 is present and defines `OKAY_HAVE_OKAYUI`; `okayui/` is added when only the launcher is built. New cache option `OKAY_UI_DEFAULT_BACKEND`.

### 5.4 How it was tested

Headless (run by `ctest`, no window or GPU; every suite also passes with `SDL_VIDEODRIVER=dummy`):

- `ui_backend` (`tests/test_ui_backend.cpp`): parse (names in any case, spacing, numbers; nothing else), names and labels, the default is Dear ImGui, precedence (environment over setting over build default, bad values ignored at each level), a build without OkayUI always answers Dear ImGui, and developer-only behaviour (a stale saved `okayui` is ignored without `OKAY_DEV` and honoured with it, `OKAY_UI_BACKEND` works either way, the settings rows are visible only with the flag and only where OkayUI is compiled in), and persistence: the launcher's `ui_backend=` line and the editor's `uibackend` integer are written and read back through the same functions the apps use, "never chosen" writes nothing and survives, a damaged line does not pick a backend.
- `okayui_host` (`okayui/tests/test_okayui_host.cpp`, 29 checks): SDL events to `OkayUI::Input` (text, every key edge, select-all with Ctrl or Command, shift, key repeat, wheel and flipped wheel, a flood of text), keys dropped and not kept when OkayUI may not have them, a click that goes down and up within one frame, the hosted `Region` (inside and outside the rectangle, covered by another window, forgotten after a frame without a panel), and an end-to-end run through OkayUI: a blocked press does not focus a field, a real click does, typed text, Backspace and Escape edit and release it.
- `okayui_screens` (`okayui/tests/test_okayui_screens.cpp`, 83 checks): the library additions; What's New in every state (idle, available, installed with and without restart, checking, failed, up to date): the right button shows, does the right thing once, and a disabled button does nothing; the GitHub link; earlier releases fold and unfold; long notes and many releases drop no geometry; the page clips to its rectangle. Console: every control (Clear, search typed through the keyboard, category dropdown, the three level toggles, collapse, auto-scroll, Clear on Play, Pause on error, Copy all, Save, row select, the source jump, per-entry Copy, Open Script with its line), a hidden control when the host did not supply its callback, 5,000 lines (only the visible rows are laid out, nothing dropped, auto-scroll follows the newest, the wheel scrolls up and stays there), hostile messages. Six golden images in the launcher look in `okayui/tests/golden/`: `whatsnew_idle`, `whatsnew_available`, `whatsnew_placeholder`, `console_mixed`, `console_filtered`, `console_empty`.
- The earlier OkayUI suites (`okayui_soft`, `okayui_behaviour`, `okayui_golden`, `okayui_drawlist`, `okayui_fuzz`, `okayui`) and the editor self-test (`editor`) still pass with the library changes. The fuzz test was not re-run under the sanitizers for this step.

### 5.5 Differences from the Dear ImGui screens, and what is not done (blunt)

What the ported screens do differently or not at all:

- **What's New.** The version-and-status card puts its button under the text, not at the right edge. Earlier-release headers use OkayUI's chevron style. No hover tooltips. Everything else (version, status colours, the one button per state and its disabled state, Latest pill, notes, earlier releases with the first one open, highlights when nothing was fetched, the GitHub link) is the same data and the same calls.
- **Console.** The three severity filters are checkboxes with counts, not coloured pills; Collapse duplicates, Auto-scroll, Clear on Play, Pause on error, Copy all and Save are always-visible controls, not a "..." popup menu; the search field has the label "Search" at its right instead of a hint inside the field; there is no tab icon. A row is one line and is cut at the panel's right edge (no horizontal scroll); the Dear ImGui panel does the same. The dock tab title with the error and warning count still comes from the ImGui window (it is the tab).
- **Text input.** Clipboard paste into the search field does not work (OkayUI has no clipboard: family 21 in 4.1). Ctrl+A, Left, Right, Home, End, Delete, Backspace, Enter and Escape do. No IME composition.
- **Layering.** OkayUI draws on top of every ImGui window. Popups are handled (5.3), but a floating ImGui window that is not a popup (a panel dragged over the Console, a tooltip) is painted over by the hosted rectangle.
- **One frame of latency** for who owns the mouse (5.2), so the first frame after a window moves or a popup closes can send a click to ImGui.
- **Single-key editor shortcuts** are kept away from a focused OkayUI field by telling ImGui the keyboard is taken (`WantTextInput`), the way an ImGui field does it. That path was run once in the real editor (typing "stop" and Ctrl+A, Backspace in the search field changed nothing in the scene); it is not covered by a test, because it needs ImGui's context.
- **Only one hosted rectangle per app is tracked** (`Region` holds one). A second hosted panel in the same frame would need a list.
- **Not done at all:** the other launcher pages and editor panels; the Stats panel (the other candidate: it needs `PlotLines` styling and the editor's frame history, so it was not ported); the player (it has no Dear ImGui UI to replace); Windows and macOS (neither was built or run); a real GPU (every run below used the software renderer under Xvfb); high-DPI (OkayUI draws in window pixels like the existing overlay; the editor scales text with its UI scale, the launcher with its own); the Direct3D 11 backend (it ignores clip spans, section 4.4, so a hosted screen would not clip there: the hosts use the SDL_Renderer path only).
- **Dear ImGui is still the default** in every build, and in a normal run (no `OKAY_DEV`, no `OKAY_UI_BACKEND`) the only UI, whatever a settings file says. Flipping the build default (`-DOKAY_UI_DEFAULT_BACKEND=okayui`) is possible and untested in a shipped build.

### 5.6 Run in the real windows

(Taken before the developer-only rule: those runs used `OKAY_DEV=1` in the editor and a `ui_backend=okayui` line in the launcher, which is now equivalent to `OKAY_DEV=1` there. The rule itself was re-checked in the real windows; see 5.7.)

Both apps were built (Release, GCC, Linux, software renderer) and run under Xvfb (1600x900) with openbox, driven with `xdotool` (clicks, typed keys, wheel), screenshots with `import`. Files are in `scratchpad/shots116/` of the session that made this step (not committed: they are large):

- Launcher before (Dear ImGui): `launcher_whatsnew_before.png`. After (OkayUI, `ui_backend=okayui`): `launcher_whatsnew_okayui.png`; wheel scrolling inside the page: `launcher_whatsnew_okayui_scrolled.png`; "Check for updates" clicked: the button turns to "Checking..." and the status line follows, and the ImGui "Software Update" dialog is correctly on top (`launcher_whatsnew_okayui_check.png`, `..._check_after.png`), then back on the OkayUI page with the notes fetched from the update server (`launcher_whatsnew_okayui_after_dialog.png`). Settings: the new row (`launcher_settings_uibackend.png`, `..._open.png`), switched to Dear ImGui live and saved as `ui_backend=imgui` (`launcher_settings_uibackend_open.png`, `launcher_whatsnew_switched_to_imgui.png`), and the environment variable winning with the control greyed and a note (`launcher_settings_env_override.png`).
- Editor before (Dear ImGui Console): `editor_console_imgui.png`. After (OkayUI, `uibackend 1`): `editor_console_okayui.png` (same three lines after Play and Stop); typing "stop" into the search field filters to the one line (`editor_console_okayui_search.png`); Ctrl+A and Backspace clear it, a click selects a row and shows its details (`editor_console_okayui_selected.png`); the "Save before playing?" dialog over the Console draws correctly because the panel falls back to Dear ImGui while it is open (`editor_console_popup_fallback.png`), and OkayUI is back after Cancel (`editor_console_after_popup.png`); the menu entry (`editor_uibackend_menu.png`); switched live to Dear ImGui, saved as `uibackend 0` (`editor_console_switched_to_imgui.png`).
- Not exercised in the real editor: warnings and errors in the OkayUI Console (the session only produced info lines; the headless tests cover them), Open Script, Copy and Save (they need a script error, the clipboard and a file; covered by the headless tests with fake callbacks), and the other launcher states (Install now, Restart now, a failed check) which need a newer published version.

### 5.7 Developer-only rule in the real windows

Both apps, Xvfb with openbox, software renderer. Without `OKAY_DEV`, with `ui_backend=okayui` and `uibackend 1` left in the saved files: the launcher's Settings > Appearance has no UI backend row, the editor's View > Rendering & Performance has no UI backend entry, and What's New and the Console are the Dear ImGui ones. With `OKAY_DEV=1`: both entries are there and the saved value is honoured. Screenshots: `scratchpad/shots116/devonly_*.png` of the session that made this step.

## 6. Porting panels: the usage report, the gap, and the first ports

Goal of this step: measure what the editor and the launcher need from Dear ImGui against what OkayUI offers, close the gap for the smallest screens, and port real ones behind the opt-in backend (section 5). Dear ImGui is still the default everywhere; nothing was removed.

### 6.1 The usage report

`python3 scripts/imgui_usage_report.py` writes `docs/imgui_port_report.md` (regenerate it after changing screens or the OkayUI headers; there is no staleness check). It reads `editor/` (without the vendored code editor), `launcher/` and `shared/`, ignores comments and string literals, counts every `ImGui::Call(`, every ImGui type, flag and enum name (`ImVec4`, `ImGuiCol_*`, `ImGuiChildFlags_*`, ...) and every draw-list method (`AddRectFilled`, ...), and checks each against the names declared in `okayui/include/okay/UI/*.hpp`. A "screen" is a top-level function; the calls between its first line and the next top-level function belong to it.

How to read the numbers: coverage is by name. A call counts as available when OkayUI declares a function of that name. That is generous: OkayUI takes `float` or `OkayUI::Vec2` where ImGui takes `ImVec2`, takes colours as bytes where ImGui takes `ImVec4`, has eight theme colours where ImGui has about fifty, and has its own flag enums. The report therefore lists types, flags and enums separately, and they are the honest measure of the work left in a mechanical port.

Result on this branch (numbers are in the report and change as the code does):

| What | Call sites | Available by name |
|---|---:|---:|
| `ImGui::` calls (261 distinct) | about 26,000 | 98.8% (it was 97.9% before the helper calls of 6.2) |
| ImGui types, flags and enums (320 distinct) | about 11,800 | 75.7% |
| Draw-list methods (26 distinct) | about 2,300 | 98.7% |

The missing calls, ranked by use (full table in the report): `GetStyle` 85 (OkayUI's `Style()` is a different struct, so ported code has to be edited), `IsItemDeactivatedAfterEdit` 18, `CalcItemWidth` 12, `FindWindowByName` 11 and `GetCurrentContext` 11 (internals; host code), `EndTooltip` 10 (OkayUI has `SetTooltip`, not a tooltip scope), `BeginPopupContextWindow` 6, then the docking calls (`SetNextWindowDockID`, `DockBuilder*`) and the context calls. The missing types are led by `ImVec4` (1,022 uses, colours), `ImGuiMouseButton_*`, `ImGuiCol_*` (theming through `PushStyleColor` with ImGui's many colour slots), `ImGuiChildFlags_*`, `ImGuiHoveredFlags_*` and `ImGuiTableColumnFlags_*`.

Per screen: of 223 screens with at least 15 calls, 99 are fully covered by call name, but only 33 are also at least 90% covered by type and flag. Weighted by call count (a few giant functions dominate: the inspector alone is more than a fifth of all call sites) 26% of the call sites sit in screens whose calls are fully covered, and 40% in screens with at least 90% of calls and 75% of types and flags covered. These are measured counts, not a promise that those screens port without edits.

The ten smallest screens (30 calls or more, to leave out helper widgets) that are fully or almost fully coverable are listed in the report. They are mostly in the launcher's chat and versions pages and the editor's dialogue, timeline and plugin menus.

### 6.2 What was added to OkayUI (new files)

`okayui/include/okay/UI/OkayUICompat.hpp` and `okayui/src/OkayUICompat.cpp`, chosen from the ranked list and built only on OkayUI's public API: `GetMainViewport` (position 0, the display size the host gave; one viewport), `GetContentRegionMax` and `GetWindowContentRegionMax` (the far corner relative to the window, measured from the cursor), `SetScrollHereY` (keeps a line in view: ratio 1 after the last line is the end), `GetScrollX` and `SetScrollX` (always 0: there is no horizontal scrolling), `SetNextWindowSizeConstraints` (the minimum becomes the resize grip's minimum; the maximum is not enforced), and the accepted-and-ignored `SetItemDefaultFocus` and `SetNextItemAllowOverlap`. After rebasing onto the colour and table steps (4.33, 4.35) the compat header no longer defines `Vec4`, the colour conversions (`ColorConvert*`, now with pointer arguments as in OkayUI.hpp), `GetStyleColorVec4`, `GetID` or `TableSetupScrollFreeze`: OkayUI.hpp has real versions (the frozen rows and columns now work). Test: `okayui/tests/test_okayui_compat.cpp` (674 checks: the conversions in OkayUI.hpp over a colour grid, hostile floats, the viewport, scroll to the end and to a row, constraints with a drag on the grip).

One hook in a file another change is editing: `okayui/src/OkayUI.cpp` got a single function, `float GetChildViewHeight()` next to `SetScrollY`, because `SetScrollHereY` needs the height of the current child and the child stack is private there. Two lines in `okayui/CMakeLists.txt` add the new source files and the two new tests.

Not filled because they need more than a thin wrapper: `GetStyle`, `IsItemDeactivatedAfterEdit` (needs edit tracking inside the widgets), `CalcItemWidth` (the width stack is private), the tooltip scope (`BeginTooltip` and `EndTooltip`), `BeginPopupContextWindow`, the docking builder, and the ImGui context calls.

### 6.3 Screens ported

All three follow section 5: a plain function in `okayui/screens/OkayUIPanels.*` over the data and a rectangle, drawn into a bare OkayUI window; the host keeps its Dear ImGui window (tab, title, close box) and hands the free area over as one rectangle. They run only with the opt-in backend (`OKAY_UI_BACKEND=okayui`, developer flag) and switch live like What's New and the Console. If an ImGui popup covers the window, the Dear ImGui version is drawn that frame.

- **Tips** (launcher page). Tip of the day with Next tip, category pills, search (Ctrl+F focuses it), the tips in one or two columns. The tips and categories moved into `shared/LauncherTips.hpp`, which both versions read, so the words cannot drift. Host code: a branch before the Dear ImGui one in `launcher/main.cpp`.
- **Crash Log** (editor window, View menu). Refresh, Copy All, Open Folder, Clear with a confirmation dialog, the path, the empty message, and the reports in a scrolling body that opens on the newest line. The reports can be megabytes, so only the visible rows are laid out; the row spacing is measured from the rows. Host code: `editor/OkayPanels.cpp` (new), and `DrawCrashLog` in `editor/main.cpp` now keeps a revision number and calls it first.
- **Audio Mixer** (editor window). Output meters with a dB tooltip, master and group volume and mute, ducking options shown only when ducking is on, Reset. It edits `okay::AudioMixer` in place through pointers, as the ImGui window does. Host code: `DrawAudioMixerOkayUI` in `editor/OkayPanels.cpp`.

Tests: `okayui/tests/test_okayui_panels.cpp`, 74 checks, headless, every button, field and toggle found by its rectangle in the screen's result and used with synthetic input: the filter logic; pills, search (typing, no match, clearing), Next tip wrapping, the Ctrl+F request; empty model and bad state; Crash Log buttons calling the host, the virtualised body (about 20 of 401 rows laid out, the last line on screen, a reload goes back to the end), the confirmation (cancel keeps, delete clears), odd text (blank lines, no final newline, a 5,000 character line, invalid bytes); mixer sliders and mutes changing only their own value, ducking rows appearing and disappearing, Reset, a short window that scrolls to reach Reset, missing pointers. Six golden images in `okayui/tests/golden/` (`tips_all`, `tips_nomatch`, `crashlog_empty`, `crashlog_reports`, `crashlog_confirm`, `audiomixer_default`). The earlier OkayUI suites (screens, host, behaviour, golden, dock, codeedit, values, compose) still pass.

### 6.4 Differences from the Dear ImGui versions, and what was not verified (blunt)

- **Tips.** No bulb icon; the tip-of-the-day card is the same size as the others. Cards are OkayUI cards, not ImGui child windows.
- **Crash Log.** The report text is not selectable with the mouse (OkayUI has no read-only multi-line selection); Copy All is the way to copy. A line longer than the window is cut at the edge (no horizontal scroll). The path is shown after the buttons as before.
- **Audio Mixer.** The meters are drawn rectangles (same colours: green, red at or above full scale), not progress bars; sliders show no number text; the "Duck level" labels sit to the right of their sliders.
- **Not verified:** none of this was run in a real window or on a real GPU. The editor and launcher were not built or run: the changed `editor/main.cpp`, `editor/OkayPanels.cpp` and `launcher/main.cpp` were checked with the compiler's syntax-only pass against the same headers (with `OKAY_HAVE_OKAYUI`), and every OkayUI piece was built and tested headless. Without OkayUI compiled in, the launcher already failed to compile the What's New branch's condition (`okayuihost` is only declared when OkayUI is); the Tips branch has the same condition. Windows and macOS were not built.
- **Dear ImGui is still the default.** The ported function bodies are 173 of about 26,000 `ImGui::` call sites (0.7%), plus the two launcher pages. This step proves the pattern and measures the road; it does not move the editor.

### 6.5 Estimate: how much could move with today's API

By name, 98.8% of the call sites have an OkayUI function of that name. The measured, more honest figures: 26% of the call sites are in screens that are fully covered by call name, and 40% are in screens with at least 90% of calls and 75% of types and flags covered. My estimate (not measured): about half of the editor and launcher code by call sites could move with today's API plus a thin layer that maps ImGui's flag and colour enums and `ImVec4` onto OkayUI's, which would raise the second figure towards the first. The rest is blocked by the real gaps: editor docking layout built with `DockBuilder*` (the OkayUI dock is its own system and the layout code would be rewritten, not ported), `GetStyle` (85 uses of a struct OkayUI does not mirror), edit tracking and tooltip scopes, per-draw fonts (`AddText` with an `ImFont`), multi-viewports, and, below all of it, the SDL2 window and platform backend, which the editor and launcher still use directly.

## 7. The render interface: OkayUI on screen without a Dear ImGui backend

Own code, off by default, wired into nothing: the editor and launcher still draw OkayUI (and everything else) through Dear ImGui's SDL backends. This section is the part that lets a later host put OkayUI's pixels on screen without any Dear ImGui backend code. (The numbered sections 5 and 6 of the draw-through-ImGui host live on another branch; this section is numbered 7 so the two cannot collide.)

### 7.1 What exists

| Piece | File | What it is |
|---|---|---|
| Contract, validator, front end | `okayui/include/okay/UI/OkayUIRender.hpp`, `okayui/render/OkayUIRender.cpp` | `render::Backend` (what a backend implements), `render::Validate` (checks a frame), `render::Renderer` (the one front end every host calls) |
| Software backend (the reference) | `okayui/render/OkayUIRender_Soft.cpp` | CPU, into an RGBA bitmap, using the same pixel routines as `OkayUI::soft::Render` and the software painter |
| Painter backend | `okayui/render/OkayUIRender_Painter.cpp` | through `okay::platform::Painter`: the sdl2 painter or the own software painter |
| OpenGL backend | `okayui/render/OkayUIRender_GL.cpp` | OpenGL 3.3 core or OpenGL ES 3.0; entry points through a get-proc function like `okay::GLRenderer::LoadGL`, no link-time GL |
| Direct3D 11 backend | `okayui/render/OkayUIRender_D3D11.cpp` | one scissor rectangle per span; Windows only (compiles to a stub elsewhere) |
| Factories | `okayui/include/okay/UI/OkayUIRenderBackends.hpp` | `CreateSoftBackend`, `CreatePainterBackend`, `CreateGLBackend`, `CreateD3D11Backend` |

Library `okay_ui_render` (CMake, `okayui/CMakeLists.txt`); only its tests and two tools link it.

Use, per frame: `OkayUI::BeginFrame(input); ...widgets...; OkayUI::EndFrameData(w, h);` then `renderer.Render(OkayUI::GetDrawData(), {fbWidth, fbHeight, dpi})`. The host clears the target before and presents after. Host images are registered once with `renderer.SetImage(key, w, h, rgba)` under the pointer it gives to `OkayUI::Image` / `DrawTexture` / `DrawList::AddImage`.

### 7.2 The contract (full text in the header)

- **Data.** OkayUI's `DrawData` as it is: 20-byte vertices (float position, RGBA8 straight alpha, float uv), 32-bit indices, and spans in draw order (clip rectangle or none, texture key or none). Window stacking has already reordered the index buffer (`reorderWindows`) before `GetDrawData`, so spans are ascending and adjacent; a backend must not depend on that and the validator only reports when it is not so.
- **Coordinates and DPI.** Logical pixels, top-left origin; the framebuffer is `dpi` times larger. The renderer converts each clip to framebuffer pixels (floor of the low edge, ceil of the high edge, clamped to the target) and drops spans that end up empty, so a backend always receives a non-empty in-range scissor. OkayUI itself knows nothing of DPI: text is baked at fixed sizes and is magnified, not re-rasterised, at `dpi` above 1.
- **Colour.** Vertex colour times texel, then source-over with straight alpha (`rgb = src * a + dst * (1 - a)`, `alpha = a + dst.a * (1 - a)`). No premultiplication, no sRGB conversion (the GL backend switches `FRAMEBUFFER_SRGB` off while it draws and puts it back).
- **Textures.** RGBA8, top row first. Key = `DrawData::atlasTexture` (launcher-look glyph atlas, linear), `FallbackFontOf(key)` (a fallback font's atlas, re-uploaded when `TtfFont::Generation()` changes), or a host image. An unknown key skips that span and is counted (`Stats::unresolvedSpans`). The main glyph atlas is baked once; `Renderer::InvalidateAtlas()` is the hook if that ever changes.
- **Frame.** `BeginFrame` / `Draw` / `EndFrame` once per OkayUI frame. The GL and Direct3D 11 backends save and restore the state they change (viewport, scissor, blend, program, bound texture and buffers, depth, cull, stencil, input assembler and shader state).

### 7.3 The validator

`render::Validate(DrawData, fbW, fbH, dpi)` never reads past what the counts describe and never trusts a value. Fatal (the frame is refused, nothing drawn): bad target size or DPI (outside 1..16384 pixels, 0.25..16), negative or oversize counts, a null pointer with a count, a vertex stride other than 20, a nonsensical atlas size. Repairable (the renderer drops just the bad parts, `Stats::repaired`): an index count not a multiple of 3, indices out of range, NaN / infinite / above 1e6 vertex positions (the triangle is dropped; a NaN uv is zeroed), spans outside the index buffer (offset or count negative or overflowing, clamped in 64-bit maths), spans not a multiple of 3, clip rectangles with a negative size or overflowing. Warnings (legal, OkayUI never does them): spans not ascending, indices no span draws, an unresolvable texture key, `DrawData::dropped` above zero. `Options::strict` turns every problem into a refusal.

### 7.4 What was tested, and where

Everything here ran in this repository's sandbox; there is no GPU and no Windows machine in it.

| Backend | Ran on | How |
|---|---|---|
| Software | the CPU | `test_okayui_render` (8,566 checks): byte-identical to `OkayUI::soft::Render` on all 20 scenes that old rasteriser can draw; golden images (the committed `primitives`, `demo_window`, `demo_window_launcher`, `drawlist_lab*` with nearest atlas sampling, plus six new `render_*.png` with linear sampling); exact clip rectangles, DPI 1.5 and 2 (flat areas of the 2x picture equal the 1x picture, 0 mismatches over 785,000 pixels), alpha, host images, repair of damaged frames, 4,000 randomly damaged frames |
| Painter, own software painter | the CPU | same test: **byte-identical** to the reference on all 23 scenes (max difference 0) |
| Painter, sdl2 painter | SDL's software renderer on a surface, `SDL_VIDEODRIVER=dummy` | same test: **not identical**. Mean channel difference 0.4 to 3.1, largest single-channel differences 180 to 255 on a few thousand edge pixels (text and thin lines: SDL rasterises geometry its own way, flat areas are within 1 level because SDL truncates where the others round). Asserted: mean under 4. No GPU `SDL_Renderer` was run |
| OpenGL 3.3 core | Mesa llvmpipe 25.2 (CPU) through EGL, surfaceless / pbuffer, FBO read-back | `test_okayui_render_gl` (272 checks), see 7.5 |
| OpenGL ES 3.0 | Mesa llvmpipe (ES 3.2 context) through EGL | the same test; same numbers |
| Direct3D 11 | `x86_64-w64-mingw32-g++`: compiled clean (`-Wall -Wextra`), linked against `d3d11` and `d3dcompiler_47`. **Run under Wine 9.0 (wine64) on Xvfb** with wined3d translating to OpenGL on llvmpipe: device created, HLSL compiled by Wine's `d3dcompiler_47`, all 23 scenes drawn and read back | `okayui_render_dump` writes the scenes and the reference, `okayui_render_d3d11_check` draws them (7.5). Also the HLSL text compiled to SPIR-V with `glslangValidator -D` for both entry points, and the four GLSL shaders validated with `glslangValidator` |

Not a real D3D11 driver: Wine's D3D11 is a translation on top of OpenGL, so it shows that the backend's calls, shaders, states and read-back work, not how a Windows GPU driver rasterises.

To repeat the Wine run: build `okayui_render_dump` on Linux, run it on an empty directory, build `okayui_render_d3d11_check` for Windows (CMake with MinGW; or by hand with `OkayUIRender.cpp`, `OkayUIRender_D3D11.cpp` and a stub `OkayUI::FallbackFontOf` returning null), then `WINEPREFIX=... xvfb-run wine64 okayui_render_d3d11_check.exe Z:/dir hardware`.

### 7.5 Image differences against the software reference

Per-channel differences over the whole frame, 23 scenes (`okayui/tests/RenderScenes.hpp`): explicit-coordinate widgets, the scripted demo window, the draw-list lab, the colour picker, a scrolling table with frozen headers, plots, three stacked windows with translucent shapes on top (the reordered draw list), nested clipped children, host images with tint and alpha, rounded rectangles and gradients, a code editor view, each in the classic and the launcher look, plus launcher-look text in scripts the launcher font lacks (a fallback-font atlas).

| Backend | Largest difference | Worst mean | Pixels more than 8 apart | Notes |
|---|---|---|---|---|
| Own software painter | 0 | 0 | 0 | same rasteriser |
| OpenGL 3.3 core, llvmpipe | 237 | 0.180 | 6 in the worst scene (0.002 percent) | classic-look scenes are one level off on about a third of their pixels (blend rounding, 1 of 255); the few large differences are single pixels on exactly tied triangle edges |
| OpenGL ES 3.0, llvmpipe | 237 | 0.180 | 6 | identical to core |
| OpenGL, DPI 1.5 and 2 | 0 | 0 | 0 | on the hand-made clip/alpha frame; demo window at DPI 2: max 222, mean 0.011, 8 pixels |
| Direct3D 11 under Wine (the check tool returns failure for a mean above 0.5: `lab` 0.52 and `code_view` 2.85 do) | 255 | 2.85 (code view, classic look) | up to 15,191 (lab) and 4,902 (code view) | the large counts are single-pixel flips of 1x1 and 2x2 quads whose vertices are at y = n + 0.5, so pixel centres sit exactly on an edge (checked for one such pixel: the quad's top edge). The software and OpenGL runs resolve those ties one way, Wine's D3D-to-GL translation the other. Whether a real Windows driver (D3D11 requires the top-left rule) agrees with the reference there is not known |
| sdl2 painter | 255 | 3.09 | 13 to 23,517 | see the table in 7.4 |

Scenes with launcher-look text through the atlas agree to mean 0.01 (GL) because the atlas is sampled linearly in both; with `Options::atlasLinear = false` the software backend draws what `OkayUI::soft::Render` and the old goldens drew.

### 7.6 Performance

`test_okayui_render_bench`, release `-O1`, 1280 x 800, the median of several frames, on a shared 4-core machine that other jobs were using (load average about 6), so read these as orders of magnitude. CPU milliseconds. "Build" is OkayUI building the frame; "valid" is `Validate` (indices and vertices scanned); upload is vertex plus index data. GL is Mesa llvmpipe: "submit" is `Renderer::Render` returning (CPU side, includes the buffer upload), "+ finish" adds `glFinish`, which waits for llvmpipe's CPU rasteriser; there is no GPU time to report. The Direct3D 11 backend was not timed (Wine's translation says nothing about Windows).

| Scene | Vertices / indices / spans | Build | Valid | Software | Painter (soft) | Painter (sdl2 soft) | GL core submit (+ finish) |
|---|---|---|---|---|---|---|---|
| Code editor, 100,000-line file | 8,480 / 12,480 / 5 | 0.1 | 0.03 | 420-660 | 420-470 | 50-60 | 0.7 (55-80) |
| Table, 5000 rows x 1 | 106,164 / 159,006 / 4 | 1.6 | 0.6 | 560 | 410 | 48 | 2.5 (54) |
| Table, 5000 rows x 3 | 262,144 / 393,006 / 3, **10,520 shapes dropped** | 5.8 | 2-8 | 390-510 | 400-580 | 50 | 6-14 (62-83) |
| Plot, 20,000 points | 20,600 / 30,690 / 2 | 0.2 | 0.08 | 450-480 | 430-450 | 60-73 | 1.7 (73) |

What this says: the interface itself is cheap (validating 400,000 indices costs under 8 ms, usually 0.1 to 2; submitting a 6.6 MB frame to GL costs 6 to 14 ms of CPU, mostly the buffer copy). The CPU rasterisers are slow because each of these frames is a full-window translucent panel: that is the reference, not a backend to ship. The code editor draws only the lines in view, so 100,000 lines cost the same as 200. **OkayUI tables now cull rows outside the clip** (4.35, row clipper): a 5000-row table with three text columns used to overflow OkayUI's own batch (262,144 vertices) and lose the bottom rows from the picture; it now emits only the rows in view and `DrawData::dropped` stays 0. Content that is not a table (a long list of `Selectable`s) still has no culling.

### 7.7 Known differences and gaps, said bluntly

- **No real GPU was used.** OpenGL ran on llvmpipe only; nothing ran on a hardware driver (no hardware driver of any vendor). Direct3D 11 ran only under Wine on top of llvmpipe; it has never run on Windows.
- **Tie breaking on exact pixel-centre edges is not the same everywhere** (7.5). OkayUI's classic look puts small quads at half-pixel coordinates; a backend may draw or skip the pixel on the edge.
- **Direct3D 11 HLSL** was compiled by Wine's `d3dcompiler_47` and parsed by glslang, not by the Windows shader compiler.
- **The old `OkayUI::D3D11Backend`** (`okayui/src/OkayUI_D3D11.cpp`) is unchanged and still ignores clip spans and textures (the gap in 4.4); the new backend does not. Nothing switches over: no host uses either.
- **DPI above 1 magnifies text**, it does not re-rasterise it; the DPI-aware font path is not built.
- **Target formats.** The GL backend draws into whatever framebuffer is bound, assuming a linear RGBA8-like target; a multisampled or sRGB target was not tested (sRGB write conversion is switched off). Direct3D 11 read-back understands RGBA8 and BGRA8 targets without multisampling only.
- **Texture size.** The front end refuses above 16384; the OpenGL backend does not query `GL_MAX_TEXTURE_SIZE` (a larger texture than the driver takes is a GL error and a blank texture), Direct3D 11 refuses above 8192. There is no packer for host images (one texture each).
- **Missing:** Metal, Vulkan, WebGL / OpenGL ES 2, Direct3D 12 backends; a window or swap chain (the host owns those: this is where `okay::platform` and the editor and launcher port come in); multi-viewport rendering; premultiplied-alpha targets; a GPU timer.
- **Not wired anywhere.** No editor, launcher or player code calls any of this, and Dear ImGui is not removed. What remains for dropping it: an own platform backend for input and windows (the SDL2 row of the roadmap), porting editor and launcher panels to OkayUI, then deleting the `okay_imgui` library.

## 9. Three editor panels: Problems, Stats, History

Same pattern as sections 5 and 6: the Dear ImGui window stays (dock tab, title, close box) and hands its free area to an OkayUI
screen; Dear ImGui is still the default and the OkayUI version is used only with the developer switch (`OKAY_UI_BACKEND=okayui`,
or View > Rendering & Performance > UI backend with `OKAY_DEV=1`). A panel falls back to its Dear ImGui version for any frame an
ImGui popup covers it. Written from what the editor's own panels show and do, from Dear ImGui's documented behaviour where the
OkayUI gaps needed it, and from our own measurements; no code from any other project. (Section 8 is the launcher's ports, on the
`claude2/okayui-port-next` branch.)

### 9.1 Why these three

From `docs/imgui_port_report.md`: all three are fully or almost fully covered by OkayUI names (Problems 95.9% before the split, its
one missing call `GetStyle`; Stats and History 100%), and they are panels people keep open: Problems is the list of what is wrong in
the scene (the status bar and the build gate point at it), Stats is the frame-rate and census panel, History is the undo list. Each
also needed a different OkayUI piece (a filtered, grouped list with buttons in rows; a plot and a table; selectable rows with a drawn
rail), so between them they test more of the toolkit than three similar panels would.

### 9.2 The shared model and the two versions

| Piece | File | What it is |
|---|---|---|
| Model | `shared/EditorPanelModels.hpp` | std only: `ProblemsModel`, `StatsModel`, `HistoryModel`, the state each keeps, the intent each returns (what the person asked for), and the rules both versions must agree on: the filter and grouping (`GroupProblems`), group headers, tooltips, number format (`FormatStatNumber`), FPS summary and colour, history rows (`HistoryRows`) |
| Dear ImGui bodies | `editor/EditorPanelViews.cpp` | what `DrawProblems`, `DrawStats` and `DrawHistory` drew, now drawn from the model; only Dear ImGui and the model header, so the comparison test compiles it as is |
| OkayUI ports | `okayui/screens/OkayUIEditorPanels.cpp` | `okayuiscreens::DrawProblems`, `DrawStats`, `DrawHistory`: same model, same intent, plus the rectangles they drew (for the tests) |
| Editor glue | `editor/ProblemsPanel.cpp`, `editor/main.cpp` (`DrawStats`, `DrawHistory`), `editor/OkayPanels.cpp` | builds the model from the editor's state, calls one version, carries out the intent (open the object or script, apply the fix with undo, undo or redo n steps, copy to the clipboard) |

The right-click menus are new in both versions: a Problems row has Go to source, Fix (when it has one) and Copy message; the Stats
census has Copy stats; a History row has Jump to this state and Copy name. The Problems demo (`OKAY_UI_DIAG=1`) now opens and fixes
its row through the same intent instead of queuing mouse clicks at screen positions.

### 9.3 Gaps closed in OkayUI (all tested in `okayui_editor_panels`)

- **Text rows** (`StyleVar_TextRowHeight`, OkayUI's own): a plain text line can take one text line's height instead of a frame's,
  as in Dear ImGui; a short text line after a taller item on its line is centred on it. This was the largest visible difference
  (every OkayUI text line was a button tall).
- **`ButtonTextAlign`** (x), **`CellPadding`** (x across; y above and below, then the row ends without the item spacing),
  **`FramePadding`** (y, for check and radio boxes only), all accepted-and-ignored before.
- **`BeginChild(id, size, ChildFlags)`** with `ChildFlags_Borders`, `ChildFlags_AlwaysUseWindowPadding` and
  `ChildFlags_AutoResizeY`: Dear ImGui's default of no padding without a frame, room for the scrollbar only when the content
  overflowed, and a child as tall as its content. (The usage report counts the ImGui names now: 172 type uses.)
- **`##` hidden** on button and plot labels (a pill labelled `4##pe` showed `4##pe`).
- **Bug fixed:** a host's `PushTextScale` around `Begin` / `End` grew the text a step every frame (`End` restored the window's
  base scale but did not forget it, and the next `BeginFrame` restored that scaled value as the new base). The editor's hosted
  panels push the UI scale this way, so any UI scale other than 1 would have grown them.
- **Also taken from `claude2/okayui-port-next`, unchanged** so the two branches merge cleanly: `Button(label, size)`, the
  right-click menu that opens only where a left click would land, several hosted rectangles in `okayuihost::Region` (three hosted
  panels can be open at once), the ASCII glyph table, and the cached circle and corner points.

### 9.4 Tests

- `okayui_editor_panels` (`okayui/tests/test_okayui_editor_panels.cpp`, 102 checks, headless): the model rules (sorting, grouping,
  duplicates, filter, tooltip and copy text, number format, FPS summary, history rows with missing names); the gaps of 9.3; Problems
  (rows, Fix buttons, the three pills, typing, Backspace and select-all in the search, the right-click menu's three entries and that
  a menu click does not also open the row under it, a click outside closes it, 600 rows scrolled with the wheel, hostile text and a
  filter without a terminator); Stats (the plot and the 60 FPS line, the four switches through their pointers, a null switch is not
  drawn, Copy stats from the right-click menu, a short panel scrolls to the footer, no history); History (Undo, Redo, a click on a
  row jumps that many steps, the current row does nothing, the right-click menu, disabled buttons, the empty state, 200 states
  scrolled). Seven golden images in `okayui/tests/golden/editorpanels_*.png`.
- `okayui_editor_panels_imgui` (`tests/test_okayui_editor_panels_imgui.cpp`): both versions headless on a 560 x 420 canvas in the
  editor's panel colour, Dear ImGui with the editor's font (Roboto Medium 18) and the Slate theme and spacing of `ApplyTheme`.
  Both pictures go through the same rasteriser (OkayUI's software render backend; Dear ImGui's vertices and its font texture are
  handed to it unchanged). It checks that the same click asks for the same thing (row 2 of Problems opens model row 2 in both, the
  sixth History row undoes three steps in both) and that the two layouts put those rows within a few pixels of each other, prints
  the speed and pixel tables below, and writes the pictures with `OKAYUI_SHOT_DIR=<dir>`.
- All the other OkayUI suites (golden images unchanged), `okayui_host` (the several-rectangles Region) and `ui_backend` pass.
  `editor/main.cpp` passes the compiler's syntax-only check with and without `OKAY_HAVE_OKAYUI`; `OkayPanels.cpp`,
  `ProblemsPanel.cpp` and `EditorPanelViews.cpp` were compiled.

### 9.5 Speed

Per frame, the panel only ("build": Dear ImGui `NewFrame` to `Render`, OkayUI `BeginFrame` to `EndFrameData`), with the sample data
of `okayui/tests/EditorPanelFixtures.hpp`, Linux, GCC, Release. Instructions are counted with callgrind
(`OKAY_UI_PROFILE=<panel>:<imgui|okayui>`, 200 frames after a warm-up; they do not depend on how busy the machine is). Wall time is
the median of 300 frames, three runs on a shared and busy machine (4 cores, load about 7), the range of the three medians shown.

| Panel | Dear ImGui instructions | OkayUI instructions | Dear ImGui build | OkayUI build | Faster? |
|---|---:|---:|---:|---:|---|
| Problems | 163,000 | 119,000 (27% fewer) | 51 to 65 us | 31 to 35 us | met |
| Stats | 195,000 | 152,000 (22% fewer) | 43 to 47 us | 18 to 21 us | met |
| History | 118,000 | 80,000 (32% fewer) | 40 to 47 us | 14 to 16 us | met |

Before the profiling fixes OkayUI was slower on two of the three by instructions (Problems 203,000, Stats 221,000, History 110,000):
callgrind showed glyph lookups (a binary search per character), the fallback-font check decoding every string, and a sine and
cosine per rounded corner and circle. The ASCII table, the ASCII fast path and the cached corner and circle points (9.3) fixed
that; every existing golden image is unchanged.

"Build + render" (the panel plus rasterising the frame on the CPU) is also printed by the test: Dear ImGui 15 to 20 ms, OkayUI 3 to
9 ms. That figure is about the software rasteriser, not about the toolkits: Dear ImGui draws every filled shape through its font
texture's white pixel, so every pixel of its window background is a texture lookup on the CPU, where OkayUI draws plain colour. On a
GPU that difference does not exist, and no GPU was used.

### 9.6 Pixels

Both versions at 560 x 420, same rectangle, same rasteriser. "Average difference" is the mean absolute difference per colour
channel (0 to 255); "clearly different" is the share of pixels where some channel differs by more than 48.

| Panel | Before the fixes: average, clearly different | After: average, clearly different |
|---|---:|---:|
| Problems | 12.0, 11.1% | 9.2, 8.2% |
| Stats | 10.1, 7.3% | 5.7, 4.4% |
| History | 7.7, 8.3% | 6.4, 5.5% |

What was fixed to get there: text lines one line tall (9.3), Dear ImGui's 12 and 8 pixel spacing, the editor's 18-pixel text
(`kEditorTextScale`, pushed by the host on top of the UI scale), one-pixel dividers in place of OkayUI's taller separator, Fix
buttons one text line tall, the severity pills filled with the accent when on and their counts at the right, no padding in the
scrolling lists, the plot on a rounded field with the frame padding, no lines between the census columns and the editor's cell
padding, frame-tall check boxes, the History rows 26 pixels apart. The test fails above 12% clearly different.

What still differs (blunt):
- **Text** is OkayUI's own rasteriser (Roboto baked at 20 px, drawn at 18) against Dear ImGui's (Roboto at 18 with 2x
  oversampling): glyph edges differ everywhere there is text, which is most of the remaining difference.
- **Problems.** The search field has no magnifier icon or clear button (the editor's Dear ImGui field has both; the comparison test
  uses a plain field on both sides), its hint starts 4 pixels further left; the right-click menu is OkayUI's popup (taller rows,
  slightly see-through).
- **Stats.** The big number is drawn by scaling (22/18), not by a second font.
- **History.** The selected row has OkayUI's accent tint with a bar on the left (over the rail's dot), not Dear ImGui's flat header
  colour.
- **All three.** One frame of latency for who owns the mouse (5.2); no hover cursor change; the right-click menu's look.

Pictures: `scratchpad/shots/oep/<panel>-imgui.png` and `-okayui.png` in the session that made this step (not committed: they are
the test's output, re-made with `OKAYUI_SHOT_DIR`).

### 9.7 Not verified

- **No real editor window.** The editor was not built (its `main.cpp` is too large for this machine's memory); it was checked with
  the syntax-only pass, and the panels were tested headless. The host code (`OkayPanels.cpp`) has never run.
- **Windows** was not built or run. No GPU was used.
- The comparison's Dear ImGui side uses a plain search field and the editor's theme values copied into the test (they would need
  the same change if `ApplyTheme` changes).

### 9.8 Coverage (usage report, before and after this step)

| What | Before | After |
|---|---:|---:|
| `ImGui::` calls available by name | 27,038 of 27,356 (98.8%) | 27,052 of 27,370 (98.8%) |
| Types, flags and enums available | 9,712 of 12,082 (80.4%) | 9,883 of 12,075 (81.8%) |
| Call sites in ported functions | 182 (0.7%) | 313 (1.1%) |

(The "before" column is the report regenerated on `main` at the start of this step, not the committed one, which was older.) The
type coverage rose because of the `ChildFlags_*` names of 9.3.

## 10. Three more editor panels: Tags and Layers, Physics, Profiler; the search field, menus and selected rows

Same method as section 9: a std-only model in `shared/EditorPanelModels.hpp`, the Dear ImGui body in `editor/EditorPanelViews.cpp`
(the editor itself calls it), the OkayUI port in `okayui/screens/OkayUIEditorPanels.*`, and the two test suites. Dear ImGui stays the
default; the ports are used only with the developer switch (`OKAY_DEV=1` and `OKAY_UI_BACKEND=okayui`). Written from what the editor's
panels show and do and from Dear ImGui's documented behaviour; no code from any other project. This branch also merged
`claude/own-ui-3` (section 4.36 onwards): its ImGui-shaped names (`OkayUIImCompat.hpp`: `ChildFlags`, `BeginChild(id, size, childFlags)`,
`IsItemEdited`, `IsItemDeactivatedAfterEdit`, the table flags) are used here instead of second copies.

### 10.1 Why these three

From the usage report, each fully covered by OkayUI names, each a screen people use, and each needing a kind of widget the first
six ports did not:

| Panel | Where | What it needed |
|---|---|---|
| Tags and Layers | Edit > Project Settings..., "Tags and Layers" | text fields in rows that commit on Enter or when left after an edit (`IsItemDeactivatedAfterEdit`, 32 layer fields), framed child lists, a rename popup, a section hosted inside part of an ImGui window |
| Physics | Edit > Project Settings..., "Physics" | formatted drag fields (`Gravity 2D/3D`, `Steps per second` shown as `%.0f`), a combo, a framed integer slider, check boxes, rows that appear with the quality |
| Profiler | View > Panels > Profiler | two plots with overlay text and labelled guide lines, a framed slider showing "frame N", progress bars with text, wrapped text after a label, a scrolling table with a header row, outer border, row bands and indented, red hot rows |

Not chosen: the Stats window (already ported in section 9; see 4.40 for the duplicate removed in the merge), the Console filters (the
Console is ported), the Lighting window and Build Settings (much larger, and they mostly repeat widgets these three cover).

### 10.2 Gaps closed in OkayUI (`okayui/src/OkayUI.cpp`, `OkayUI.hpp`; all tested in `okayui_editor_panels`)

- **Search field** (`SearchField`): the editor's field, a magnifier at the left (the text starts after it) and a clear cross at the
  right while it has text; the cross empties it and gives up the keyboard. Problems uses it, and the comparison now uses the editor's
  own field on the Dear ImGui side.
- **Menus like Dear ImGui's**: `StyleVar_PopupPadding` and `StyleVar_PopupWidth` (OkayUI's own) size a popup; with a pushed padding the
  popup is exactly as tall as its content; popups now stay inside the display. The panels' `MenuLook` uses the editor's popup colour,
  16 and 12 pixels of padding and rows one text line plus the spacing tall. All four right-click menus (Problems, Stats, History) use it.
- **Selected rows**: `Selectable_Header` draws Dear ImGui's flat, square header colours (selected, hovered, held) with the label half the
  spacing in. History's current row uses it.
- **Formatted fields**: `DragFloat/2/3(..., format)` and framed `SliderInt / SliderFloat(..., format)` draw Dear ImGui's frame with the
  value centred in a printf format (one numeric conversion; anything else falls back), `StyleVar_ItemInnerSpacing` (x) spaces their cells.
- **Edit tracking** (own-ui-3's `IsItemDeactivatedAfterEdit`, two fixes): with `InputText_EnterReturnsTrue` the Enter is not counted as
  an edit and commits the text (a later loss of the keyboard does not commit it again), and a field that loses the keyboard reports once.
  Text fields keep their caret state in 64 slots recycled least recently used (it was 32 and then a stateless fallback, which typed
  backwards in the 33rd field).
- **Layout**: `StyleVar_FramePadding` (y) now sets every frame's height (Dear ImGui's frame height); `SameLine` then `SetCursorPosX`
  stays on the line (the next line used to start below the short item); `CenterNextOnLine` centres a small button on a frame-tall row;
  bordered children (`ChildFlags_Borders`) draw Dear ImGui's one-pixel outline and take a pushed `StyleVar_WindowPadding` (x and y);
  `Table_BordersOuter` draws its frame; wrapped text in Dear ImGui's text rows is one text line per line and continues at the left edge.
- **Fixes**: "##" was shown in check box, slider and drag labels; a long Combo item ran over the arrow and the label.
- **Speed**: shapes wholly outside the current clip are not emitted (a 32-row list showing seven, a section scrolled out of view); the
  wrap measures each word once. No older golden image changed; the table clipper test now expects no more geometry with the clipper
  on, and nothing dropped without it.

### 10.3 Hosting a section inside an ImGui window

Tags and Layers and Physics are sections of the Project Settings window, under collapsing headers. `HostSection` in
`editor/OkayPanels.cpp` reserves a placeholder as tall as the OkayUI body was on the last frame, offers OkayUI only the part the ImGui
window shows (and clips to it, so a section scrolled half out of view draws and takes clicks only in its visible half), and offers the
open rename popup too. The editor carries out the intents as before (validation, saving the project, undo for a tag rename, the
console lines).

### 10.4 Tests

- `okayui_editor_panels` (181 checks, was 102): every gap of 10.2 (search field typing and clearing; deactivation after an edit by a
  click elsewhere, another field, Escape and Enter; 70 fields keeping their caret; formatted drags, clamping, Ctrl+click typing; the framed
  slider; the menu look and header colours; centring, the framed child, a host clip around a bare window); Tags and Layers (Strict, Add
  them, Remove, Add tag with Enter, Reset, Reset all, a layer field committed once when left, the field showing the host's answer, the
  rename popup opening with the name, asking for the rename, closing on the host's word; hostile buffers and long names); Physics (a
  drag of 20 pixels at 0.05 each, the smoothing and continuous boxes, the slider, Apply, the quality list hiding the solver rows, missing
  pointers, the step-rate clamp); Profiler (the rules: row order, hot rows, diagnosis, CSV, bar text; Pause starting the scrub at the
  newest frame, the slider, Copy CSV, an empty profile). Five new golden images (`editorpanels_tagslayers`, `_tagslayers_rename`,
  `_physics`, `_profiler`, `_profiler_paused`); the seven older `editorpanels_*` images were re-made for the new frame height, search
  field, menus and selected row.
- `okayui_editor_panels_imgui`: the three new panels (Tags and Layers and Profiler on a 560 x 780 canvas, they are tall) and the
  Problems right-click menu, drawn by both toolkits from the same model; the same click asks for the same thing in both (Remove on
  the second tag, Add tag, Apply, Pause), a 10-pixel drag moves the step rate in both.
- All other OkayUI suites and `ui_backend` pass (30 of 30 with `ctest -R "okayui|ui_backend"`). `editor/main.cpp` passes the syntax-only
  check with and without `OKAY_HAVE_OKAYUI`; `OkayPanels.cpp` and `EditorPanelViews.cpp` were compiled.

### 10.5 Speed and pixels

Instructions per frame (callgrind, `OKAY_UI_PROFILE=<panel>:<imgui|okayui>`, 200 frames after a warm-up, panel build only); wall time:
median of 150 frames, three runs on a shared machine, the range shown. "Clearly different": share of pixels where a channel differs by
more than 48 (the gate is 12%).

| Panel | Dear ImGui instructions | OkayUI instructions | Dear ImGui build | OkayUI build | Average difference | Clearly different |
|---|---:|---:|---:|---:|---:|---:|
| Tags and Layers | 262,000 | 245,000 (6% fewer) | 90 to 98 us | 48 to 57 us | 5.04 | 3.73% |
| Physics | 122,000 | 79,000 (36% fewer) | 43 to 73 us | 18 to 28 us | 5.67 | 4.81% |
| Profiler | 548,000 | 428,000 (22% fewer) | 141 to 145 us | 85 to 89 us | 10.98 | 7.93% |
| Problems (now with the search field on both sides) | 168,000 | 128,000 (24% fewer) | 73 to 91 us | 36 to 57 us | 7.20 | 6.82% |
| Problems' right-click menu (picture only) | | | | | 7.36 | 6.71% |
| Stats | 195,000 | 122,000 (38% fewer) | 57 to 79 us | 21 to 32 us | 7.45 | 5.65% |
| History | 118,000 | 82,000 (31% fewer) | 42 to 61 us | 14 to 19 us | 3.48 | 2.88% |

"Build + render" (the panel plus the CPU rasteriser) is also faster with OkayUI for all six in all three runs. Pictures:
`<scratchpad>/shots/oe2/<panel>-imgui.png`, `-okayui.png` and `side-<panel>.png` in the session that made this step (not committed;
re-made with `OKAYUI_SHOT_DIR`).

Tags and Layers first counted more instructions than Dear ImGui (324,000 against 262,000): it drew all 32 layer fields where seven show.
Culling shapes outside the clip and the cheaper wrap brought it to 247,000; after the merge (own-ui-3's edit marks) it was 261,500, and
the child outline drawn as edges and fixed arcs (it was a stroked path with a sine per corner point, and before that a fill over a fill)
brought it to 245,000. Its "build + render" time on the CPU rasteriser was slower than Dear ImGui's until the outline stopped filling the
whole child twice. As in section 9, "build + render" is about the software rasteriser, not about the toolkits.

### 10.6 What still differs (blunt)

- Text edges everywhere (OkayUI's baked Roboto against Dear ImGui's): most of the remaining difference.
- Check boxes: OkayUI fills the box with the accent and draws a white tick; Dear ImGui draws an accent tick on the field colour.
- The Physics Quality box: OkayUI's chevron, not Dear ImGui's square arrow button.
- The Profiler table: header row a little taller, number columns a few pixels narrower (their widths are measured from the content,
  not Dear ImGui's proportional sizing).
- Tags and Layers: the tag list does not show a scroll bar where Dear ImGui does (four rows just fit; Dear ImGui counts the gap after
  the last row), so the Rename and Remove buttons sit 10 pixels further right.
- The Profiler's diagnosis and the physics rows come out the same; tooltips and the hover cursor are OkayUI's.

### 10.7 Not verified

- No real editor window: the editor was not built (its `main.cpp` is too big for this machine) and nothing ran the editor; the host code
  (`HostSection`, the Profiler window) has never run. Windows was not built; no GPU was used.
- The Project Settings sections with OkayUI under a real ImGui window scroll were tested only with the headless host clip test.

### 10.8 Coverage (usage report)

| What | Before this step (committed report) | After (with own-ui-3 merged) |
|---|---:|---:|
| `ImGui::` calls available by name | 27,052 of 27,370 (98.8%) | 27,133 of 27,371 (99.1%) |
| Types, flags and enums available | 9,883 of 12,075 (81.8%) | 11,752 of 12,083 (97.3%) |
| Call sites in ported functions | 313 (1.1%) | 441 (1.6%) |

Most of the rise in types and flags is own-ui-3's names; this step added `IsItemDeactivatedAfterEdit` usage (now available) and the
three ported functions (128 call sites).

## 11. Three more: Import settings, Lighting > Post-processing, Project Settings > Player and Rendering; Dear ImGui's check boxes, combos and scroll bars

Same method as sections 9 and 10: a std-only model in `shared/EditorPanelModels.hpp`, the Dear ImGui body in `editor/EditorPanelViews.cpp`
(the editor calls it; Dear ImGui stays the default), the OkayUI port in `okayui/screens/OkayUIEditorPanels.*`, hosted with `HostSection` in
`editor/OkayPanels.cpp`, and the two test suites. Used only with the developer switch (`OKAY_DEV=1` and `OKAY_UI_BACKEND=okayui`). Written
from what the editor's panels show and do, Dear ImGui's documented behaviour, and pixel measurements of Dear ImGui's output in the comparison
test; no code from any other project.

### 11.1 Why these three

| Panel | Where | Why |
|---|---|---|
| Import settings | the Inspector, for a picture, model or sound clicked in the Project panel (`editor/ProjectPanelUI.cpp`) | seen every time an asset is picked; 33 calls, 100% covered by name; combos, a formatted drag (`%.4f`, `%.0f deg`), check boxes, disabled Apply and Revert, a failure popup |
| Lighting > Post-processing | View > Panels > Lighting, first section (`editor/main.cpp` `DrawLookSections`) | the most used part of the Lighting window; 41 calls, 100% covered; a combo with a list of presets, framed sliders with formats, three-cell sliders (`SliderFloat3` with a format, new here), rows that appear with their switch, wrapped notes |
| Project Settings > Player and Rendering | Edit > Project Settings, the two sections open at the top | with Physics and Tags and Layers (section 10) four of the window's five sections are now ported; text fields, `InputInt` with its step buttons (new look here), a slider, a check box, a button |

Not chosen: Build Settings (much larger, and it repeats these widgets), Preferences (menu items, not a panel), the Animation clip list
(inside the Animator window, which uses canvas and drag-drop calls OkayUI only half has), the rest of the Lighting window (Fog has
`ColorEdit3`, whose look is still OkayUI's own; Weather has the editor's asset slot).

### 11.2 Gaps closed in OkayUI (`okayui/src/OkayUI.cpp`, `OkayUI.hpp`; all tested in `okayui_editor_panels`)

- **Check boxes like Dear ImGui's** (`StyleVar_FramedWidgets`, OkayUI's own, off by default): a rounded frame in `Col_FrameBg`, the accent at
  45% once ticked, and a tick in `Col_CheckMark` (the editor's is the light accent, not the text colour; measured from Dear ImGui's output). The
  launcher look's filled box with a white tick stays when the style is not pushed.
- **The combo arrow**: with the same style a combo is a field in `Col_FrameBg` with a square button as tall as the field at its right end in
  `Col_Button` and a filled triangle in the text colour; the text starts `FramePadding.x` in (the x of a pushed `FramePadding` is now kept).
- **Scroll bars**: `StyleVar_ScrollbarSize` (accepted and ignored before) sets the width of a child's bar and of a scrolling table's vertical bar.
  When pushed the bar sits inside a framed child's or a bordered table's one-pixel outline, its grab is 2 pixels inside it on every side, and
  the child's content is cut where the bar starts, as in Dear ImGui (a row wider than the list is no longer drawn under the bar).
- **The tag list's scroll bar**: the editor's version steps its list clipper by its row height, so four tags overflow the list by a few pixels
  and Dear ImGui shows a bar; the port now counts the content the same way, so it shows the bar too and the Rename and Remove buttons sit where
  Dear ImGui's do.
- **Table column widths**: the Profiler's number columns are their widest text plus the cell padding on both sides (10 and 10; it was 14 in
  all), which is what Dear ImGui's proportional sizing gives there (measured: within 2 pixels per column).
- **`InputInt` with the framed look**: a field with the number `FramePadding.x` in and cut at the field's edge, two square step buttons
  `ItemInnerSpacing.x` apart, the label after them. **Bug fixed**: `InputInt` drew the `##` part of its label (in both looks).
- **`SliderFloat2` / `SliderFloat3` with a format**: two or three framed slider cells sharing a label.
- **The editor's dim text colour** (0.70, 0.74, 0.82) is pushed by the panels (it is brighter than the launcher's).
- **Speed**: a text field wholly outside the clip that neither has the keyboard nor is about to get it is not worked out at all (its caret,
  text and box). A focused field scrolled out of view still takes typing (tested).

No golden image outside the `editorpanels_*` ones changed. Those were re-made: the panels now push the framed widgets, the 14-pixel scroll bar
and the dim text colour.

### 11.3 Tags and Layers: from 6% to 23% fewer instructions

Callgrind showed the 32 layer fields costing about 2,500 instructions each per frame although only seven are in view: the earlier step stopped
emitting their shapes but still worked out each field's caret, text widths and hint. With the out-of-clip shortcut above Tags and Layers went
from 245,000 to 201,700 instructions a frame (Dear ImGui: 262,300), 23% fewer. Its picture did not change.

### 11.4 Tests

- `okayui_editor_panels` (251 checks, was 181): the framed check box (frame and tint colours, the tick's pixels, a click), the framed combo (the
  arrow button's and the field's colours, the triangle, picking the third row), `ScrollbarSize` (the content width with and without overflow
  and without the push, the grab inside the outline, a wide row cut at the bar), the framed `InputInt` (no `##` drawn, both steppers),
  `SliderFloat3` with a format (a click on the second cell changes only it), a focused field scrolled out of view still typing; Import settings
  (the rules `ImportSame` and `ImportSizeIndex`; Apply ignored while nothing changed, enabled after a change, Revert; the normals list hiding
  the smoothing row; a 10-pixel scale drag at 0.01 a pixel; the failure popup; a texture with the sampler rows and its size list; a sound; a
  file from a newer editor; no settings); Post-processing (the preset list and the preset it asks for, Reset, Bloom hiding its four rows, the
  exposure slider, the tonemap list, depth of field showing its rows, no values); Player and Rendering (typing in Version, the Width + and
  Height - buttons, Ambient, Skybox, Apply, missing pointers, an unterminated buffer). Six new golden images: `editorpanels_import`,
  `_import_fail`, `_import_texture`, `_post`, `_post_presets`, `_player_rendering`.
- `okayui_editor_panels_imgui` (38 checks): the four new bodies drawn by both toolkits from the same model; the same click asks for the same
  thing in both (Import animations then Apply, Bloom off, the exposure slider's left end, Width +, Skybox and Apply to current scene).
- `ctest -R "okayui|ui_backend"`: 31 of 31 pass. `editor/main.cpp` passes the syntax-only check with and without `OKAY_HAVE_OKAYUI`;
  `OkayPanels.cpp`, `EditorPanelViews.cpp` and `ProjectPanelUI.cpp` were compiled.

### 11.5 Speed

Instructions per frame (callgrind, `OKAY_UI_PROFILE=<panel>:<imgui|okayui>`, 200 frames after a warm-up, the panel's build only; they do not
depend on how busy the machine is). Wall time: the median of 150 frames, three runs on a shared machine (load about 6 to 9), the range shown.

| Panel | Dear ImGui instructions | OkayUI instructions | Dear ImGui build | OkayUI build |
|---|---:|---:|---:|---:|
| Import settings (a model) | 96,800 | 59,600 (38% fewer) | 30 to 32 us | 7 to 11 us |
| Post-processing | 402,400 | 273,400 (32% fewer) | 96 to 102 us | 45 to 54 us |
| Player | 63,500 | 37,300 (41% fewer) | 10 to 19 us | 4 to 5 us |
| Rendering | 45,800 | 20,100 (56% fewer) | 9 to 21 us | 3 to 4 us |
| Tags and Layers | 262,300 | 201,700 (23% fewer; was 6%) | 74 to 89 us | 30 to 36 us |
| Problems | 167,700 | 129,000 (23% fewer) | 61 to 68 us | 39 to 43 us |
| Stats | 195,400 | 122,600 (37% fewer) | 39 to 55 us | 16 to 20 us |
| History | 118,100 | 81,900 (31% fewer) | 28 to 37 us | 14 us |
| Physics | 122,500 | 76,400 (38% fewer) | 37 to 41 us | 14 to 15 us |
| Profiler | 548,400 | 427,300 (22% fewer) | 132 to 144 us | 68 to 83 us |

"Build + render" (the panel and the CPU rasteriser) is faster with OkayUI for all ten in all three runs; as before, that figure is about the
software rasteriser, not the toolkits.

### 11.6 Pixels

Both versions on a 560-pixel-wide canvas (420 tall; 780 for Tags and Layers and the Profiler; 1240 for Post-processing), same rasteriser.
"Clearly different": the share of pixels where a channel differs by more than 48 (the gate is 12%).

| Panel | Average difference | Clearly different | Before this step |
|---|---:|---:|---:|
| Import settings (a model) | 4.40 | 3.75% | (new) |
| Import settings (a texture, picture only) | 1.67 | 1.43% | (new) |
| Import settings (a sound, picture only) | 4.43 | 3.76% | (new) |
| Post-processing | 4.95 | 4.22% | (new) |
| Player | 1.16 | 0.84% | (new) |
| Rendering | 0.62 | 0.53% | (new) |
| Physics | 4.21 | 3.53% | 4.81% |
| Tags and Layers | 4.23 | 3.33% | 3.73% |
| Profiler | 10.18 | 7.44% | 7.93% |
| Problems | 7.36 | 6.72% | 6.82% |
| Problems' right-click menu | 7.54 | 6.63% | 6.71% |
| Stats | 7.85 | 5.76% | 5.65% |
| History | 3.52 | 2.89% | 2.88% |

Stats and History moved by about 0.1 point the wrong way: the brighter dim text (the editor's colour) makes their glyph edges differ a little
more. Pictures: `<scratchpad>/shots/oe3/final/<panel>-imgui.png`, `-okayui.png` and `side-<panel>.png` in the session that made this step (not
committed; re-made with `OKAYUI_SHOT_DIR`).

### 11.7 What still differs (blunt)

- Text edges everywhere (OkayUI's baked Roboto against Dear ImGui's): most of what is left.
- Disabled buttons (Import settings' Apply and Revert): OkayUI fades them halfway to the panel colour; Dear ImGui at 60% opacity, a little
  brighter.
- The Look Preset list, the import failure popup and the other popups are OkayUI's (the editor's colour and padding, but OkayUI's placement).
- Stats: the editor's window puts its 14-pixel scroll bar in the window's right padding, outside the area a host gives OkayUI; the port keeps
  OkayUI's narrow bar inside its area.
- Profiler: the header row is a little taller; Post-processing: the Reset button is cut at the panel's edge where Dear ImGui lets it run past.
- A ticked check box's frame tint is the accent at 45%, measured; a hovered ticked box is 55%, a guess.

### 11.8 Not verified

- No real editor window: the editor was not built (its `main.cpp` is too big for this machine) and nothing ran it; the new host code
  (`HostSection` for four more sections, `ProjectPanelUI.cpp`'s copy to and from the engine's import settings) has never run. Windows was not
  built; no GPU was used.
- Inside the Project panel's "Import Settings" popup the Dear ImGui version is always used: `HostSection` falls back while an ImGui popup covers
  the window, and that window is the popup.

### 11.9 Coverage (usage report)

| What | Before this step (committed report) | After |
|---|---:|---:|
| `ImGui::` calls available by name | 27,133 of 27,371 (99.1%) | 27,138 of 27,376 (99.1%) |
| Types, flags and enums available | 11,752 of 12,083 (97.3%) | 11,752 of 12,083 (97.3%) |
| Call sites in ported functions | 441 (1.6%) | 530 (1.9%) |

The call count rose by 5 because the Post-processing body has its own tooltip and renderer-note helpers (5 calls) while the editor keeps its
own for the Sky section.

## 12. The ten ported panels in the real editor: same row heights, same first window sizes

The editor was built (Linux, `-O1`, assertions on) and run under Xvfb at 1920 x 1200 with a scratch project, once with Dear ImGui and once
with `OKAY_UI_BACKEND=okayui`, driven by the same recorded clicks; each panel's rectangle was compared pixel by pixel ("clearly different":
some channel more than 48 apart). Before this step four panels were far off: Project Settings 15 to 17%, History 10.7%, the Profiler 10%,
the Inspector's sound import settings 8.6 to 9.3%, and Stats 8.4%. What caused it and what changed (written from measurements of Dear
ImGui's output in the editor and in the comparison test; no code from any other project):

- **Hosted sections were 8 pixels too tall each.** A section (Project Settings' Player, Physics, Rendering, Tags and Layers; the import
  settings; Post-processing) reported its height including the item spacing after its last row, and the host's placeholder item then got
  Dear ImGui's spacing again. `ContentHeight` now leaves the last spacing out. Project Settings lost 8 pixels per section and its
  headers line up with Dear ImGui's.
- **The host's spacing.** The panels used the editor's default spacing (items 12 and 8 apart, frames 8 pixels above and below the text,
  labels 8 after the field). The Inspector pushes its own (Appearance > Density; Comfortable is 10 and 12, frames 9 and 8, and a value
  column of its own width), so the sound import panel's rows came out 4 pixels closer each. The panels now take `EditorMetrics` (item
  spacing, frame padding, inner spacing, default field width) that the host copies from `ImGui::GetStyle()` and `CalcItemWidth()` just
  before it draws; the defaults are the editor's style, so the tests and goldens did not move. The tag list's row height now follows them
  too (it was a fixed number).
- **History and the Profiler opened at a fixed 440 x 340.** Dear ImGui fits a new window (one with no saved place) to what its first frame
  holds, and does not show that frame. The OkayUI host now draws the Dear ImGui body in exactly that frame, so the window opens at the size
  the Dear ImGui version opens at; OkayUI draws from the next frame. A window that has a saved place, or is docked, is not touched.
- **Stats.** The FPS number now sits at the top of a frame-tall row as Dear ImGui's does (the row was 4 pixels taller, pushing the plot,
  table and switches down), and the census columns are placed as Dear ImGui's proportional table places them, measured at two widths: the
  first column's text at the left edge, each later column 20 pixels (twice the cell padding) after the previous one's share, the shares
  splitting what is left by the weights 1, 0.8, 1, 0.8. The `editorpanels_stats` golden was re-made for this (looked at side by side: the
  number is at the body size, the plot 4 pixels higher, the right-hand columns further right); no other golden changed.

### 12.1 Pixels in the real editor

| Panel (shot) | Rectangle | Before | After |
|---|---|---:|---:|
| Problems | 1232 x 130 | 1.34% | 1.34% |
| Problems, search typed | 1232 x 130 | 2.51% | 2.51% |
| Stats | 430 x 550 | 8.42% | 5.02% |
| Stats, a switch toggled | 430 x 550 | 8.20% | 5.03% |
| History | 440 x 335 | 10.72% | 0.93% |
| Profiler | 440 x 335 | 10.24% | 1.09% |
| Profiler, paused | 440 x 335 | 10.50% | 1.07% |
| Lighting > Post-processing | 430 x 465 | 4.96% | 4.36% |
| Post-processing, Bloom ticked | 430 x 465 | 4.06% | 2.84% |
| Import settings, a picture | 430 x 430 | 3.80% | 2.19% |
| Import settings, a model | 430 x 430 | 6.47% | 3.09% |
| Import settings, a sound | 430 x 430 | 8.61% | 4.60% |
| Import settings, a sound, changed | 430 x 430 | 9.26% | 4.72% |
| Project Settings | 460 x 975 | 17.32% | 3.20% |
| Project Settings, edited | 460 x 975 | 16.98% | 3.45% |
| Project Settings, Tags and Layers open | 460 x 975 | 14.97% | 3.92% |

Every panel is now under 8%. The Dear ImGui pictures of both runs are the same to within 0.7% (the FPS numbers and the plot change). The
Profiler figure is weak: in both versions the Profiler's first window is a strip about 25 pixels wide behind History (Dear ImGui fits it to
a body whose items all stretch to the window's width, so it fits it to almost nothing), so the rectangle mostly holds History. The OkayUI
window now opens the same way; that the Dear ImGui Profiler opens so narrow is a separate thing to fix in the editor. Pictures:
`<scratchpad>/shots/vpa/after/{im,ok}-<shot>.png` and `diff-<shot>.png` in the session that made this step (not committed).

In the comparison test (`okayui_editor_panels_imgui`) Stats went from 5.76% to 4.19%; the other panels did not change. New there: every
hosted section's height is checked against the Dear ImGui body's (from its first row to the bottom of its last), with the editor's spacing
and with the Inspector's, to within one pixel; and a picture of the sound import panel with the Inspector's spacing (1.37%).

### 12.2 What still differs

- Text edges (OkayUI's baked Roboto against Dear ImGui's) are most of what is left in every panel.
- Text that runs past a hosted section's right edge: Dear ImGui lets it run into the window's padding, OkayUI cuts it at the section's
  edge (the long notes under the import settings).
- Stats: the narrow scroll bar is still OkayUI's; the plot's line differs slightly.

### 12.3 Not verified

Windows (not built or run here; the measurements are from Linux under Xvfb with the software SDL renderer), other UI scales and the
Compact density in the real editor (the headless test covers the Inspector's Comfortable spacing only), a docked History or Profiler.
