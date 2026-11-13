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
11. **Performance:** nothing measured on a large editor panel. The batch is fixed (262,144 vertices, 393,216 indices, plus an overlay batch of the same size); overflow is counted and reported (`DrawData::dropped`) but a full editor panel was never tried. No culling of clipped items.
12. **Accessibility, IME and right-to-left** are not started.

Then the migration itself: panels ported one at a time behind an option, with the same golden and behaviour tests run against both toolkits before the default changes. That is later stages of `docs/own_code_roadmap.md`, not this one.

## 4. Gap-closing step 1: audit refresh and the public draw list

The audit in section 1 ranked single functions. This section re-counted the whole usage by feature family, said plainly what OkayUI has and what it lacks, and built the top gap. Nothing in the editor, launcher or player calls the new code and no default changed: Dear ImGui is still what those programs run on.

### 4.1 Audit: HAVE and GAP by feature family

Method (repeatable): `grep -rhoE "ImGui::[A-Za-z0-9_]+"` over the 58 files under `editor/`, `launcher/` and `shared/` that mention ImGui, then each function was put in a family and the counts summed. 25,345 call sites to 261 functions (section 1 counted 24,862 to 259 earlier; the code grew). Counts are call sites in source, not calls per frame. Difficulty is our estimate for the OkayUI side: S is a day or less, M is a few days, L is a project of its own.

HAVE means OkayUI has an equivalent with the same job. PARTIAL names what is left out. GAP means nothing usable yet. The Status column is after this step.

| # | Family | Call sites | Status | What is there, what is missing | Difficulty |
|---|---|---|---|---|---|
| 1 | Item and hover queries, tooltips | 3,274 | PARTIAL | `IsItemHovered`, `Tooltip(text)`, and now `IsItemActive`, `IsItemActivated`, `IsItemDeactivated`, `IsItemClicked`. `IsItemFocused`, `IsItemVisible`, `GetItemID`, `IsAnyItemActive` (step 6). Missing: hover flags and delays, `BeginTooltip` blocks with widgets inside (26), `IsItemEdited`, `IsItemDeactivatedAfterEdit` (17) | S |
| 2 | Layout and cursor | 4,613 | PARTIAL | `SameLine`, `Dummy`, `Spacing`, `Indent`, item width, and now `GetCursorScreenPos`, `SetCursorScreenPos`, `GetCursorPos`/`SetCursorPos` (and X, Y), `GetContentRegionAvail`. `BeginGroup`/`EndGroup` and `NewLine` (steps 3 and 7). Missing: `GetContentRegionMax` (25), `CalcItemWidth` | S |
| 3 | Text | 2,851 | PARTIAL | `Text`, `TextColored`, `TextDisabled`, `TextWrapped`, `BulletText`, `SeparatorText`, `LabelText`. `PushTextWrapPos`/`PopTextWrapPos` (step 5; takes a width, not an x position). Missing: printf-style formatting (callers format first) | S |
| 4 | Draw list (custom drawing) | 2,520 | HAVE (this step) | See 4.2. Missing: `AddConcavePolyFilled` (5), `AddImageRounded` (17), rounding on chosen corners only, anti-aliasing, `AddCallback` | done |
| 5 | Buttons, checkbox, radio | 2,387 | PARTIAL | `Button`, `SmallButton`, `Checkbox`, `RadioButton`, `ToggleSwitch`, `ImageButton`, `InvisibleButton`, `ArrowButton` (a drawn triangle, step 30), `ColorButton` (a checkerboard through the alpha, step 29). Missing: explicit button size | S |
| 6 | Style and colours | 1,896 | PARTIAL (step 29) | `Style()`, `PushTextScale`, `GetColorU32`, `PushStyleVar`/`PopStyleVar` (step 20), and now the full colour set: 58 `Col_*` ids (the 8 originals and the Dear ImGui set) with theme-derived defaults, `PushStyleColor` (bytes, packed or float), `SetStyleColor`/`ResetStyleColor`, `GetStyleColorVec4`, `GetStyleColorName`, colour converters. Most ids are drawn by the widgets that use them (section 4.33 lists them). Missing: `PushFont`/`PopFont` of arbitrary fonts; ids nothing draws with yet (the navigation highlights, `BorderShadow`, the unfocused tab colours, the hovered and active separator) | M |
| 7 | Value widgets (drag, slider, input number) | 1,610 | PARTIAL (step 2) | `DragFloat`, `DragInt`, `SliderFloat`, `SliderInt`, `InputInt`, and now `DragFloat2/3/4`, `DragInt2/3/4` and `DragFloatRange2` (several drag cells in one row sharing a label; each cell scrubs alone, clamps, range cells never cross). Missing: `DragIntRange2`, format strings, typing into the single-value `DragFloat`/`SliderFloat` (the multi-cell ones can, step 10), a drag-speed modifier key | M |
| 8 | Window, child, scroll | 1,074 | PARTIAL (step 28) | `Begin`/`End`, `BeginChild`, resize grip, pin, `SetNextWindow*`, window queries, and now z-order with click to front, input occlusion between overlapping windows and over docked content, and the flags `NoScrollbar`, `NoBackground`, `NoCollapse`, `AlwaysAutoResize`, `NoFocusOnAppearing`, `NoBringToFront` (section 4.32). Windows begun with `Begin(title, p_open, flags)` scroll when their content overflows. Missing: `SetNextWindowSizeConstraints`, `GetScrollY`/`SetScrollY`/`SetScrollHereY` for windows, no scroll with mouse, saved window settings | M |
| 9 | Popups | 1,038 | PARTIAL | `OpenPopup`, `BeginPopup`, `BeginPopupContextItem`, `CloseCurrentPopup`. Missing: `BeginPopupModal` (85) with dimming, `BeginPopupContextWindow`, `IsPopupOpen` (41) | M |
| 10 | Menus | 957 | PARTIAL | menu bar, `BeginMenu`, `MenuItem(label)`. shortcut text, checked and enabled state, `MenuSeparator` (step 11). Missing: nested sub-menus, `Shortcut` handling (the text is display only) | M |
| 11 | Mouse, keys, IO | 901 | PARTIAL | mouse position, buttons, wheel, text, 9 key edges, and now `IsMouseDown/Clicked/Released`, `GetMousePos`, `GetMouseDelta`. Missing: Ctrl and Alt, key down, about 30 more keys, middle button, double click, drag deltas, mouse cursor shapes (92) | M |
| 12 | IDs | 792 | HAVE | `PushID`, `PopID`. Missing: `GetID` (11), `GetStateStorage` | S |
| 13 | Combo and selectable | 618 | HAVE (step 30) | `Combo(items)`, `Selectable`, `ListBox`, `BeginCombo`/`EndCombo` (step 9), and `Selectable` with a size, a pointer overload and flags (disabled, close the popup, span all columns, double click only; section 4.34) | done |
| 14 | Text input | 545 | PARTIAL | single line with caret, selection, UTF-8, and now IME composition (`Input::composition`, `compositionCaret`, `commit`; underlined preedit, commit, cancel, caret rectangle for a host's candidate window; step 27). `InputTextWithHint` and `SetKeyboardFocusHere` (step 4). A separate multi-line code editor widget, `OkayUI::CodeEdit` (step 25, `docs/own_ui_code_editor.md`; not `InputTextMultiline` itself). `InputTextMultiline` (step 30, on `CodeEdit` without the gutter; section 4.34). Missing: password, callbacks, `InputText` flags beyond read only, enter-returns-true and select-all, a word-wrapping multi-line box | M |
| 15 | Disabled blocks | 369 | HAVE (this step) | `BeginDisabled`/`EndDisabled`/`IsDisabled` | done |
| 16 | Tables | 233 | PARTIAL (steps 14, 31) | `BeginTable` with headers, weights, drag-resizable columns, `Columns`, `TableSetColumnIndex`, flags, and now sorting (header clicks, multi-key, tristate, default sort, the sort specs API), scrolling boxes (`Table_ScrollX`/`Table_ScrollY`, wheel, bars, `TableSetupScrollFreeze` for frozen rows and columns), full-height row bands and `TableSetBgColor` (section 4.35). Missing: column reordering and hiding, `TableSetupColumn` flags beyond fixed width and sorting, per-column header tooltips, context menu on the header, `TableGetHoveredColumn`, nested tables, auto-fit columns, a scrolling table inside a collapsed parent clipped by `BeginChild` gets no special care | M |
| 17 | Tree nodes | 201 | HAVE (step 30) | `TreeNode`, `CollapsingHeader`, `TreeNodeEx` (default open, leaf, selected, open on arrow, open on double click, bullet, span full width, no tree push, framed), `SetNextItemOpen`, `IsItemToggledOpen` | done |
| 18 | Colour editors | 168 | HAVE (step 29) | `ColorEdit3`/`ColorEdit4` with flags (no alpha, hex, no inputs, no picker, no label), a click on the swatch opens a picker popup, `ColorButton`, `ColorPicker3`/`ColorPicker4` (saturation / value square, hue bar, alpha bar, current and original preview, hex field, R G B A cells), hex helpers. Missing: HSV input mode, a float display mode, the picker's wheel layout, copy and paste of a colour by right click | done |
| 19 | Drag and drop | 123 | PARTIAL | `DragSource(int)`, `DropTarget(int*)`. Missing: typed payloads, a preview while dragging, drop on custom rectangles | M |
| 20 | Tabs | 55 | PARTIAL | `TabBar(labels)`. Missing: `BeginTabBar`/`BeginTabItem` with content, closable and reorderable tabs, `TabItemButton` | M |
| 21 | Clipboard | 52 | GAP | none. Needs the clipboard in `okay::platform`, and copy, cut and paste in the text field | S |
| 22 | Docking and viewports | 36 + 6 mentions of viewport flags | PARTIAL (step 26) | `DockSpace`, `DockBegin`/`DockEnd`, dock into the left, right, top, bottom or centre of a node, splitters with minimum sizes, tabs (reorder, close, tear off), drag a title bar or tab onto drop targets with a preview, layout save and load as text, repair of damaged layouts (`okayui/include/okay/UI/OkayUIDock.hpp`, section 4.30). Floating windows now stack and take clicks in order, and docked content sits below them (step 28, section 4.32). Missing: multi-viewports (OS windows from `okay::platform`), the `DockBuilder*` call shapes, central node and node flags | L |
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

Not done: IME, ligatures, multiple cursors, folding, word wrap, a code font (the cell width assumes a monospaced font; family 23), wide-character cells, autocomplete popup (the editor draws its own on the widget's geometry), a real window or GPU check, the script editor itself still on ImGuiColorTextEdit. The multi-line paste named as not done in 4.28 is now possible in this widget (the single-line text field is unchanged).

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

Not done: column reordering, hiding and the header context menu, auto-fit by double click on a border, `TableSetColumnEnabled`, `TableGetHoveredColumn`, outer and inner vertical borders as flags, sizing policies beyond fixed and weighted, nested tables, row selection helpers, keyboard navigation, a horizontal bar for a ScrollY-only table; a frozen-column set wider than 90% of the box is dropped; the row-height memory holds the first 256 rows (later rows get a one-row band); `Selectable_SpanAllColumns` in a scrolling table is limited by the cell's clip; not run in a real window or on Windows.

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

What this says: the interface itself is cheap (validating 400,000 indices costs under 8 ms, usually 0.1 to 2; submitting a 6.6 MB frame to GL costs 6 to 14 ms of CPU, mostly the buffer copy). The CPU rasterisers are slow because each of these frames is a full-window translucent panel: that is the reference, not a backend to ship. The code editor draws only the lines in view, so 100,000 lines cost the same as 200. **OkayUI tables have no row culling**: a 5000-row table with three text columns overflows OkayUI's own batch (262,144 vertices) and the bottom rows are silently missing from the picture (`DrawData::dropped` says so and stderr gets a message); the render interface cannot fix that, a row clipper in OkayUI could.

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
