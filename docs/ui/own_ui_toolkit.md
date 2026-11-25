# Our own UI toolkit: OkayUI replaces Dear ImGui

> **What this is:** Part of the plan in `docs/status/own_code_roadmap.md` (Dear ImGui row).
>
> **Status:** Reference page for this feature. Kept as written; not extended (rule 5).
>
> **Last checked:** 2026-11-20 (moved into `ui/`, paths and links checked; the text itself was not re-checked against the code).

Part of the plan in `docs/status/own_code_roadmap.md` (Dear ImGui row). Section 4 holds the current HAVE and GAP table by feature family and what each gap-closing step built (sections 4.2 to 4.36); sections 1 and 3 are the earlier per-function audit (rows closed since are marked "Done in step 1").

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
| `okayui/soft/OkayUI_Soft.*` | `OkayUI::soft::Render(DrawData, Bitmap&)`: triangle rasteriser with the top-left fill rule (shared edges never draw twice), per-command scissor, source-over blending, flat colour and atlas sampling (nearest; the SDL backend filters linearly), hostile data skipped. Fast paths that give the same bytes: each row visits only the columns that can be inside, a triangle of one colour (flat, untextured or sampling one uniform texel area such as Dear ImGui's white pixel) fills each row's inside run directly, and `Bitmap::Clear` copies its first row |
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
- **Hiding a label from the display** (`"Label##id"`): OkayUI hashes the whole label (so two widgets with the same visible label still need `##id` or `PushID`) and draws and measures only the part before `##` in every labelled widget (since step 6.11; before that only `ListBox` did).
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

Then the migration itself: panels ported one at a time behind an option, with the same golden and behaviour tests run against both toolkits before the default changes. That is later stages of `docs/status/own_code_roadmap.md`, not this one.

## 4. Gap-closing step 1: audit refresh and the public draw list

The audit in section 1 ranked single functions. This section re-counted the whole usage by feature family, said plainly what OkayUI has and what it lacks, and built the top gap. Nothing in the editor, launcher or player calls the new code and no default changed: Dear ImGui is still what those programs run on.

### 4.1 Audit: HAVE and GAP by feature family

Method (repeatable): `grep -rhoE "ImGui::[A-Za-z0-9_]+"` over the 58 files under `editor/`, `launcher/` and `shared/` that mention ImGui, then each function was put in a family and the counts summed. 25,345 call sites to 261 functions (section 1 counted 24,862 to 259 earlier; the code grew). Counts are call sites in source, not calls per frame. Difficulty is our estimate for the OkayUI side: S is a day or less, M is a few days, L is a project of its own.

HAVE means OkayUI has an equivalent with the same job. PARTIAL names what is left out. GAP means nothing usable yet. The Status column is after this step.

| # | Family | Call sites | Status | What is there, what is missing | Difficulty |
|---|---|---|---|---|---|
| 1 | Item and hover queries, tooltips | 3,274 | PARTIAL | `IsItemHovered`, `Tooltip(text)`, and now `IsItemActive`, `IsItemActivated`, `IsItemDeactivated`, `IsItemClicked`. `IsItemFocused`, `IsItemVisible`, `GetItemID`, `IsAnyItemActive` (step 6). Missing: hover flags and delays, `BeginTooltip` blocks with widgets inside (26), `IsItemEdited`, `IsItemDeactivatedAfterEdit` (17) | S |
| 2 | Layout and cursor | 4,613 | PARTIAL | `SameLine`, `Dummy`, `Spacing`, `Indent`, item width, and now `GetCursorScreenPos`, `SetCursorScreenPos`, `GetCursorPos`/`SetCursorPos` (and X, Y), `GetContentRegionAvail`. `BeginGroup`/`EndGroup` and `NewLine` (steps 3 and 7). `GetContentRegionMax` (compat layer) and `CalcItemWidth` exist (step 32). Missing: nothing counted | S |
| 3 | Text | 2,851 | PARTIAL | `Text`, `TextColored`, `TextDisabled`, `TextWrapped`, `BulletText`, `SeparatorText`, `LabelText`. `PushTextWrapPos`/`PopTextWrapPos` (step 5; takes a width, not an x position). Missing: printf-style formatting (callers format first) | S |
| 4 | Draw list (custom drawing) | 2,520 | HAVE (this step) | See 4.2. Missing: `AddConcavePolyFilled` (5), `AddImageRounded` (17), rounding on chosen corners only, anti-aliasing, `AddCallback` | done |
| 5 | Buttons, checkbox, radio | 2,387 | PARTIAL | `Button`, `SmallButton`, `Checkbox`, `RadioButton`, `ToggleSwitch`, `ImageButton`, `InvisibleButton`, `ArrowButton` (a drawn triangle, step 30), `ColorButton` (a checkerboard through the alpha, step 29). Missing: explicit button size | S |
| 6 | Style and colours | 1,896 | PARTIAL (step 29) | `Style()`, `PushTextScale`, `GetColorU32`, `PushStyleVar`/`PopStyleVar` (step 20), and now the full colour set: 58 `Col_*` ids (the 8 originals and the Dear ImGui set) with theme-derived defaults, `PushStyleColor` (bytes, packed or float), `SetStyleColor`/`ResetStyleColor`, `GetStyleColorVec4`, `GetStyleColorName`, colour converters. Most ids are drawn by the widgets that use them (section 4.33 lists them). Missing: `PushFont`/`PopFont` of arbitrary fonts; ids nothing draws with yet (the navigation highlights, `BorderShadow`, the unfocused tab colours, the hovered and active separator) | M |
| 7 | Value widgets (drag, slider, input number) | 1,610 | PARTIAL (step 2) | `DragFloat`, `DragInt`, `SliderFloat`, `SliderInt`, `InputInt`, and now `DragFloat2/3/4`, `DragInt2/3/4` and `DragFloatRange2` (several drag cells in one row sharing a label; each cell scrubs alone, clamps, range cells never cross). `DragIntRange2` (step 33; same cells as `DragFloatRange2`, ints). Missing: format strings, typing into the single-value `DragFloat`/`SliderFloat` (the multi-cell ones can, step 10), a drag-speed modifier key | M |
| 8 | Window, child, scroll | 1,074 | PARTIAL (step 28) | `Begin`/`End`, `BeginChild`, resize grip, pin, `SetNextWindow*`, window queries, and now z-order with click to front, input occlusion between overlapping windows and over docked content, and the flags `NoScrollbar`, `NoBackground`, `NoCollapse`, `AlwaysAutoResize`, `NoFocusOnAppearing`, `NoBringToFront` (section 4.32). Windows begun with `Begin(title, p_open, flags)` scroll when their content overflows. Missing: `SetNextWindowSizeConstraints`, `GetScrollY`/`SetScrollY`/`SetScrollHereY` for windows, no scroll with mouse, saved window settings | M |
| 9 | Popups | 1,038 | PARTIAL (steps 12, 32) | `OpenPopup`, `BeginPopup`, `BeginPopupContextItem`, `CloseCurrentPopup`. Missing: `BeginPopupModal` (85) with dimming, `BeginPopupContextWindow`, `IsPopupOpen` (41) [Update: modal popups were step 12; now also `BeginPopupContextWindow`, `BeginPopupContextItem(id, flags)`, `IsPopupOpen(id, flags)` with `AnyPopupId` (step 32). Still missing: `GetMousePosOnOpeningCurrentPopup`, `BeginPopupEx`.] | M |
| 10 | Menus | 957 | PARTIAL | menu bar, `BeginMenu`, `MenuItem(label)`. shortcut text, checked and enabled state, `MenuSeparator` (step 11). Missing: nested sub-menus, `Shortcut` handling (the text is display only) | M |
| 11 | Mouse, keys, IO | 901 | PARTIAL (step 32) | mouse position, buttons, wheel, text, 9 key edges, and now `IsMouseDown/Clicked/Released`, `GetMousePos`, `GetMouseDelta`. Missing: Ctrl and Alt, key down, about 30 more keys, middle button, double click, drag deltas, mouse cursor shapes (92); now the middle button, `GetMouseDragDelta`, `ResetMouseDragDelta`, `MouseButton_*`, `Key_*` names, `KeyId_Slash` (step 32). | M |
| 12 | IDs | 792 | HAVE | `PushID`, `PopID`. Missing: `GetID` (11), `GetStateStorage` | S |
| 13 | Combo and selectable | 618 | HAVE (step 30) | `Combo(items)`, `Selectable`, `ListBox`, `BeginCombo`/`EndCombo` (step 9), and `Selectable` with a size, a pointer overload and flags (disabled, close the popup, span all columns, double click only; section 4.34) | done |
| 14 | Text input | 545 | PARTIAL | single line with caret, selection, UTF-8, and now IME composition (`Input::composition`, `compositionCaret`, `commit`; underlined preedit, commit, cancel, caret rectangle for a host's candidate window; step 27). `InputTextWithHint` and `SetKeyboardFocusHere` (step 4). A separate multi-line code editor widget, `OkayUI::CodeEdit` (step 25, `docs/ui/own_ui_code_editor.md`; not `InputTextMultiline` itself). `InputTextMultiline` (step 30, on `CodeEdit` without the gutter; section 4.34). Missing: password, callbacks, `InputText` flags beyond read only, enter-returns-true and select-all, a word-wrapping multi-line box | M |
| 15 | Disabled blocks | 369 | HAVE (this step) | `BeginDisabled`/`EndDisabled`/`IsDisabled` | done |
| 16 | Tables | 233 | PARTIAL (steps 14, 31, 33) | `BeginTable` with headers, weights, drag-resizable columns, `Columns`, `TableSetColumnIndex`, flags, and now sorting (header clicks, multi-key, tristate, default sort, the sort specs API), scrolling boxes (`Table_ScrollX`/`Table_ScrollY`, wheel, bars, `TableSetupScrollFreeze` for frozen rows and columns), full-height row bands and `TableSetBgColor` (section 4.35). Missing: column reordering and hiding, `TableSetupColumn` flags beyond fixed width and sorting, per-column header tooltips, context menu on the header, `TableGetHoveredColumn`, nested tables, auto-fit columns, a scrolling table inside a collapsed parent clipped by `BeginChild` gets no special care; step 33 (section 4.37): hiding columns (`Col_DefaultHide`, `Col_NoHide`, the header menu with `Table_Hideable`, `TableSetColumnEnabled`), `Col_NoResize`, `Col_NoHeaderLabel`, `TableGetHoveredColumn`, name and flag queries. Column reordering is still a GAP. | M |
| 17 | Tree nodes | 201 | HAVE (step 30) | `TreeNode`, `CollapsingHeader`, `TreeNodeEx` (default open, leaf, selected, open on arrow, open on double click, bullet, span full width, no tree push, framed), `SetNextItemOpen`, `IsItemToggledOpen` | done |
| 18 | Colour editors | 168 | HAVE (step 29) | `ColorEdit3`/`ColorEdit4` with flags (no alpha, hex, no inputs, no picker, no label), a click on the swatch opens a picker popup, `ColorButton`, `ColorPicker3`/`ColorPicker4` (saturation / value square, hue bar, alpha bar, current and original preview, hex field, R G B A cells), hex helpers. Missing: HSV input mode, a float display mode, the picker's wheel layout, copy and paste of a colour by right click | done |
| 19 | Drag and drop | 123 | PARTIAL | `DragSource(int)`, `DropTarget(int*)`, and since the later step (see the drag and drop section) typed payloads (`SetDragDropPayload`, `GetDragDropPayload`), `SetDragDropPreview(text)` and `BeginDragDropTargetCustom(rect, id)`. Missing: widgets inside a floating preview box that follows the pointer, keyboard drag sources | M |
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

Built, in new files so `OkayUI.cpp` is untouched apart from three small hooks: `OkayUI::CodeEdit` (`okayui/include/okay/UI/OkayUICodeEdit.hpp`, `okayui/src/OkayUICodeEdit.cpp`), a multi-line code editor with a line-number gutter, caret and selection (mouse and keys, word and line selection, UTF-8 safe), typing, Enter with auto-indent, Tab and Shift+Tab, clipboard through step 24, grouped undo and redo, find (next, previous, highlight all, replace), bracket matching, scrolling (wheel, scroll bars, caret kept visible), a pluggable highlighter callback with a built-in C-like and OkayScript one, read-only mode, caller error markers and breakpoint marks with a click callback, and 100,000 lines at under half a millisecond per frame. The hooks: `GetFrameInput()`, `GetKeyboardFocusId()`, `SetKeyboardFocusId(id)`. Full description, the HAVE and GAP table against what the script editor uses of ImGuiColorTextEdit, the numbers and the NOT DONE list are in `docs/ui/own_ui_code_editor.md`.

Tested: `test_okayui_codeedit.cpp` (407 checks: every editing operation, movement, UTF-8 selection edge cases, undo grouping, find, highlighting, brackets, the widget driven by synthetic mouse and keys, pixels, a golden image, a 100k-line performance check) and `test_okayui_codeedit_fuzz.cpp` (flat-string model fuzz and random mouse, wheel, keys and API calls; clean under ASan and UBSan).

Not done: IME, ligatures, multiple cursors, folding, word wrap, a code font (the cell width assumes a monospaced font; family 23), wide-character cells, autocomplete popup (the editor draws its own on the widget's geometry), a real window or GPU check. (When this step was written the script editor reached it only through `ScriptEditorWidget` with the developer-only OkayUI backend; since 2026-11-23, #1200, the script editor always uses it and ImGuiColorTextEdit is deleted) (step 27 in `docs/ui/own_ui_code_editor.md`; the host now sends Up, Down, Ctrl+C, X, V, Ctrl and the named keys). The multi-line paste named as not done in 4.28 is now possible in this widget (the single-line text field is unchanged).

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

### 4.36 Gap-closing step 32: edit tracking, item width and the window context popup (families 1, 2, 9)

Built in `okayui/src/OkayUI.cpp` and `okayui/include/okay/UI/OkayUI.hpp`, written from what the names mean (a value widget is "edited" on the frame it reports a change; "deactivated after edit" is the moment a held or focused widget is let go having changed on the way). Nothing was read or copied from another project.

- `IsItemEdited()`: true on the frame the last value widget returned true. Covered: `Checkbox`, `RadioButton`, `ToggleSwitch`, `SliderFloat/Int/2/3/4`, `VSliderFloat`, `Knob`, `InputText`, `InputTextWithHint`, `InputInt`, `InputFloat`, `Combo`, `ListBox`, `DragFloat/Int`, `DragFloat2/3/4`, `DragInt2/3/4`, `DragFloatRange2`, `ColorEdit3/4`. Each of those definitions is now a private `Name_raw` function and the public `Name` is a one-line wrapper at the end of `OkayUI.cpp` that records the edit. For a widget of several cells (`DragFloat3`, `InputInt`) "the last item" is its last cell. Not covered: the explicit-coordinate widgets (`Checkbox(id, x, y, ...)` and so on), `ColorPicker3/4` (another source file with no access to the state), `Selectable(&flag)`, `CodeEdit`.
- `IsItemDeactivatedAfterEdit()`: true on the frame the last item stops being held (or a text field loses the keyboard) if its value changed at any time while it was held or focused. A drag that scrubs for many frames answers false until it is let go; a checkbox answers true on its click frame; a click that changes nothing (a slider pressed at its end) never answers true. The remembered edit is dropped one frame after the item is neither held nor focused.
- `CalcItemWidth()`: the width the next full-width item gets (the room left on the line, or a `SetNextItemWidth` / `PushItemWidth` width when that is smaller); 0 outside a frame.
- `BeginPopupContextWindow(id)`: a right click on the current window's empty background (over the window, no item hovered or held, no window above) opens the popup at the pointer; same return value rules as `BeginPopupContextItem`. Call it after the window's widgets.

Tested: `okayui/tests/test_okayui_itemedit.cpp` (checkbox, drag field over several frames, a slider click that changes nothing then a real change, a text field typed in and left, a text field focused and left without typing, a plain text item, `CalcItemWidth` with a one-shot, a push, a huge request and a shared line, the context popup on the background, over an item and outside the window). Headless only; none of it was run in a real window.

Not done (next): `BeginTooltip`/`EndTooltip` as a scope that takes widgets (about 36 call sites; `SetTooltip(text)` is all there is), then `GetStyle` (85 uses), then the `ImVec4` and flag-enum mapping layer, a `ListClipper` for lists that are not tables, and a first port of a screen that needs these (the editor's Inspector uses `IsItemDeactivatedAfterEdit` for undo steps).

### 4.37 Gap-closing step 33: number formats and slider flags (own-code round 3)

What existed: the compat overloads `DragFloat`, `DragInt`, `SliderFloat`, `SliderInt`, `DragFloat2/3/4`, `DragFloatRange2` and `InputFloat` took the Dear ImGui `fmt` text and the slider flags and threw both away. The name report is not the gap: it counts the call and enum NAMES, which are already 100.0% (the regenerated `docs/imgui_port_report.md`: 12,739 of 12,744 type, flag and enum uses, 267 of 267 calls). The old 75.7% figure in 6.1 is from before the names layer. The gap is behaviour behind names that were accepted and ignored. This step honours the ones that the editor really uses (about 1,800 drag and slider calls with a `fmt`):

- `OkayUI::FormatNumber(out, cap, fmt, value, isInt, &decimals)`: the printf-style text. Literal text, `%%`, and at most one conversion (`d i u x X f F e E g G`, flags `-+ 0#`, width, precision, length marks skipped). It never hands the caller's text to printf: it builds its own spec, so `%s`, `%n`, `%*d`, a second conversion or a lone `%` are refused (the default text is shown and false returned). Written from what printf formats mean.
- `OkayUI::SetNextNumberFormat(fmt, noTypedInput, noRoundToFormat, fmtMax, logarithmic)` and `GetLastNumberText(cell)`: the pending format for the next Drag, Slider or InputFloat widget (used up by it; also cleared by `ResetState`).
- Honoured: the format in the drag cell text (single, vector cells, range with its own max format, `InputFloat`); a slider shows its value centred when a format is given; `Slider_NoInput` (Ctrl+click typing off); `Slider_NoRoundToFormat` (without it a slider value and a typed value are rounded to the decimals the format shows); `Slider_Logarithmic` on `SliderFloat` and `SliderInt` for a range of one sign that does not touch 0 (the handle is placed by log of the size; the sign is kept). Test-first, it found a real bug (log of a negative range gave NaN).
- Known gaps, not hidden: `Slider_Logarithmic` on drag fields and on ranges that touch or cross 0 (stays linear); `SliderFloat2/3/4` and `DragInt2/3/4` ignore `fmt`; dragging a value does not round to the format (typed and slider values do), so a tiny drag speed can show a rounded text that differs from the stored value; `%s`-style formats are refused; `InputInt` flags, `ImGuiStyleVar_GrabMinSize` (accepted, no effect) and the other flags listed in `OkayUICompat.hpp` are still ignored.

Tested: `okayui/tests/test_okayui_numfmt.cpp` (244 checks, headless, hand-worked expected texts): the formatter (rounding, flags, widths, truncation, refused formats, decimals), every widget's shown text, the picture changes with the format and not otherwise, slider NoRoundToFormat and NoInput by real clicks, typing, log slider positions for four ranges (geometric middles 10, 3.162, 2000 and -10) plus dragging past both ends, and hostile floats (infinity, NaN, 1e30) with odd formats. All 46 buildable `okayui_*` ctest suites pass (the `okayui_render_gl`, `render_bench`, `hostmode` and `port_*` ones were not built here). Off by default; nothing in the editor or launcher is wired to it.

### 4.38 Gap-closing step 34: DragIntRange2

`DragIntRange2(label, &lo, &hi, speed, minV, maxV)` in `okayui/src/OkayUI.cpp`: the integer twin of `DragFloatRange2`, reusing the same two-cell routine (low never above high, clamped when `minV < maxV`, null pointers refused, counts for `IsItemEdited`). Written from what the name means; nothing copied. Tested in `okayui/tests/test_okayui_values.cpp` (`IntRangeTests`, headless). Not run in a real window. Audit rows 2 and 7 were stale about `GetContentRegionMax` and `CalcItemWidth`, which already existed, and are corrected.

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

Result on this branch when this section was first written (numbers are in the report and change as the code does; the current counts are in 6.10):

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

### 6.7 Next slices: tooltip scope, `GetStyle`, the mapping layer, `ListClipper`

Four of the gaps listed in 6.1 and 6.2, closed in order. All are declared in `okayui/include/okay/UI/OkayUICompat.hpp` (and `OkayUIMap.hpp`), implemented in `okayui/src/OkayUI.cpp` (they need its private layout state), and tested headless by `okayui/tests/test_okayui_scopes.cpp`. OkayUI is still off by default (`OKAY_UI_BACKEND=okayui`); no golden image changed. Written from what the names mean; no Dear ImGui code was read.

1. **`BeginTooltip` / `EndTooltip`.** A tooltip that takes widgets. The widgets between the calls are laid out in the overlay at a provisional spot, with input switched off; `EndTooltip` measures their group box, moves their vertices to the final spot (right and below the cursor, flipped left / above at the display edge, as `SetTooltip` does), draws the box and moves its indices in front of the content. The surrounding cursor, group, id and width state is saved and restored, so the tooltip does not move the layout. `EndTooltip` is safe without a `BeginTooltip`; a nested one draws into the outer one; when the pointer is blocked by a window the widgets still run but nothing is shown. Limits: no scrolling child inside, and a full-width item (Separator) makes the box 480 px wide (or the display less 40).
2. **`GetStyle()`.** Returns `StyleInfo`: `Alpha`, `WindowPadding`, `FramePadding`, `ItemSpacing`, `ItemInnerSpacing`, `CellPadding`, `ScrollbarSize`, the rounding and border sizes, `FontScale` (the theme's text scale, so 2 means 16 px glyphs) and `Colors[Col_COUNT]` as float colours. It is filled on every call from OkayUI's own settings, so it follows `Style()`, `PushStyleVar` and `PushStyleColor`. It is read-only: a write is lost. `FramePadding`, `ItemInnerSpacing` and `CellPadding` are the values the layout uses (OkayUI has no separate settings for them); `Alpha` is always 1. This covers the field reads that make up the 85 uses (`FramePadding.x`, `ItemSpacing.x/y`, `ItemInnerSpacing.x`, `Colors[]`, `Alpha`, `ScrollbarSize`, `WindowPadding.x`, `CellPadding.x`); code that assigns to the style must still be edited.
3. **Mapping layer.** `OkayUIMap.hpp` (no ImGui include): `FromVec2/FromVec4/ToVec2/ToVec4` work with any struct that has `x, y(, z, w)`, `FromColor/ToColor` go between float colours and the packed colour (clamped, NaN is 0), `FlagMap` tables with `MapFlags` (reports bits it could not map) and `MapEnum`. `shared/OkayUIImGuiMap.hpp` (host side, includes `imgui.h`) holds the pairs for window flags, the `ImGuiCol_*` slots and the `ImGuiStyleVar_*` ids. Flags with no OkayUI counterpart (MenuBar, HorizontalScrollbar, NoScrollWithMouse, ...) are reported in `*unmapped`, not dropped silently. The tab slot `ImGuiCol_Tab` and the Tab selected / active names are not in the table yet (their names differ between ImGui versions and were not checked). The ImGui header is not available to the headless tests, so that file is not in CI: it was compiled against the project's ImGui and a one-off program checked a few values (see the report of this change).
4. **`ListClipper`.** `ListClipper clip; clip.Begin(count[, rowStep]); while (clip.Step()) for (i = clip.DisplayStart; i < clip.DisplayEnd; ++i) ...`. Only the visible rows (innermost clip rectangle, else the window, else the display; one row of margin each side) are laid out; the cursor jumps over the rest, so the scroll range equals the full list's. Without a row step, row 0 is shown first and measured. `ForceDisplayRangeByIndices` keeps rows in range. Rows must all have the same step. The test compares the clipped list to the unclipped one pixel for pixel at the top, after wheel scrolls and at the end, for 1,000 rows, and checks fewer than 40 rows were laid out. For tables there is already the row clipper (`SetTableRowClipping`).

The usage report was regenerated after this change: 99.2% of the `ImGui::` call sites (28,025 now, the editor grew) have an OkayUI function of that name; `GetStyle`, `BeginTooltip` and `EndTooltip` are no longer in the missing list.

How to try it (owner rule 4a): the code is in the library and not reachable from the launcher or editor yet; nothing visible changes in the Windows release. Run `ctest -R okayui_scopes` in a build with `OKAY_BUILD_TESTS=ON` (headless). The Crash Log panel (editor, View menu, with `OKAY_UI_BACKEND=okayui`) already lays out visible rows only by its own code; switching it to `ListClipper` and giving the Audio Mixer meters `BeginTooltip` was done in 6.8.

### 6.8 Crash Log on `ListClipper`, Audio Mixer meter tooltip, and four more calls

**The screens (step to the owner's rule 4a).** The editor's Crash Log (View > Debug & Data > Crash Log) and Audio Mixer (View > World & Gameplay > Audio Mixer) were already ported behind `OKAY_UI_BACKEND=okayui` (developer flag `OKAY_DEV=1`). Now the Crash Log body uses `ListClipper` instead of its own spacer arithmetic (blank lines are drawn as a one-space row so every row has the same step; the row step is measured from the first drawn row each frame and given to the clipper from the second frame), and the Audio Mixer meters show a `BeginTooltip` scope (meter name, then the dB level on its own line). Under the default Dear ImGui path nothing changed. The three Crash Log golden images are unchanged (pixel for pixel the same as the hand-made virtualisation); one new golden, `audiomixer_meter_tip`. New checks in `test_okayui_panels.cpp`: a 5,000-line report with blank lines lays out fewer than 60 rows, reaches the oldest line with the wheel, no dropped geometry; the meter tooltip appears on hover and is gone after the pointer leaves (pixel-equal to the frame before). Steps to try it, the switch, and how to tell it is on (`ui=okayui` in the startup stack line and in Help > About): `docs/try_it/12b-okayui-crash-log-audio-mixer.md`. The editor's UI backend menu text now names the three ported panels.

**New calls** (all in `OkayUICompat.hpp`, implemented in `OkayUI.cpp` / `OkayUICompat.cpp`, tested in `test_okayui_scopes.cpp`; written from what the names mean, no Dear ImGui code read):
- `FindWindowByName(title)` returns a `WindowInfo` value (`found`, `pos`, `size`, `focused`, `docked`), not a pointer into internals; `found` is false for an unknown, null or empty title. Callers that read `->Pos` / `->Size` change to `.pos` / `.size`.
- `IsWindowDocked(title)`: true when the window sits in a leaf of the dock layout (`OkayUIDock`) and does not float. (ImGui's argument-less form, "the current window", is not offered.)
- `SetNextWindowFocus()`: the next ImGui-style `Begin` takes the focus and is raised; a `Begin(nullptr)` drops the request.
- `GetMouseDragDelta(button, threshold)` / `ResetMouseDragDelta`: distance from the press once past the threshold (default 4 px), zero otherwise; only the left button is tracked.
- `IsAnyItemHovered()`: an item was under the pointer this frame so far or the frame before. It is tracked where items are placed, so it costs one hit test per item.

**Still missing (ranked, from the regenerated report; the dock calls, the internals, `Shortcut`, `SetWindowSize`, `GetWindowContentRegionMin`, `GetMousePosOnOpeningCurrentPopup`, `GetDragDropPayload` and `BeginDragDropTargetCustom` named here were built in 6.9):** `GetCurrentContext` / `SetCurrentContext` / `CreateContext` / `DestroyContext` / `NewFrame` (host-level: OkayUI has one global context; these calls belong to the Dear ImGui backend and would be deleted with it, not ported), `SetNextWindowDockID` and the `DockBuilder*` family (the OkayUI dock is its own system; the editor's layout code would be rewritten, not ported), `GetCurrentWindow` / `GetCurrentWindowRead` / `BringWindowToDisplayFront` (internals), `GetActiveID` / `ClearActiveID` / `SetItemKeyOwner` / `GetStateStorage` / `FindRenderedTextEnd` (internals), `Shortcut` (4 uses; needs a key-chord type), `SetWindowSize` (window by title; cheap, not done), `GetWindowContentRegionMin`, `GetMousePosOnOpeningCurrentPopup`, `GetDragDropPayload`, `BeginDragDropTargetCustom`. Context and dock calls are the real blockers for the editor.

**Not verified:** nothing here was run in the Windows editor or launcher; the changed `editor/main.cpp` lines are text only (menu hints) and were not compiled in this session (the editor was not built).

### 6.9 Small calls, internals and the dock builder

Written from what the names mean (no Dear ImGui code read); headless-tested in `test_okayui_internals.cpp` and `test_okayui_dockbuilder.cpp`. OkayUI is still off by default and nothing in the editor or launcher calls these yet (`docs/try_it/12c-okayui-dockbuilder.md` says so at the top). No golden image changed.

**Small calls** (`OkayUICompat.hpp`; implemented in `OkayUI.cpp` and `OkayUICompat.cpp`):
- `Shortcut(Chord(KeyId_S, Mod_Ctrl | Mod_Shift), flags)`: true on the frame the key goes down with exactly those modifiers held. Own chord encoding (`KeyChord` is an int: key id in the low byte, modifier bits above), not Dear ImGui's. Quiet while a text field has the keyboard unless `Shortcut_RouteAlways`; `Shortcut_AllowRepeat` fires on key repeat; false while an item owns the key (`SetItemKeyOwner`). It is a global shortcut: no routing by window or focus (Dear ImGui's routing has more modes).
- `SetWindowSize(title, size, cond)`: resize a window by title at any time. False for an unknown or null title or a non-finite size. It does not create a window that was never drawn.
- `GetWindowContentRegionMin()`: where content starts, in window coordinates. Not paired with `GetWindowContentRegionMax`, which stays "cursor plus the space left" from the content origin (an earlier decision, kept so existing callers do not change).
- `GetMousePosOnOpeningCurrentPopup()`: the opening position of the innermost popup being built; the pointer position outside a popup.
- `GetDragDropPayload()`: the payload of the drag in progress (a copy, `delivery` false) or null.
- `BeginDragDropTargetCustom(rect, id)`: a drop target over a rectangle you give; the `id` is accepted and unused.

**Internals** (new `OkayUIInternals.hpp`). OkayUI keeps its state in file-scope tables and has no window or context objects, so these return values, not pointers into internals:
- `GetCurrentWindow()` returns a `CurrentWindow` snapshot (valid, id, title, pos, size, focused, hovered, appearing, z). Callers that wrote `->Pos` write `.pos`. `GetCurrentWindowRead()` is the same call.
- `GetActiveID()` is the id of the pressed or dragged item. `ClearActiveID()` lets go of it and also takes the keyboard from a focused text field (the editor uses it to drop focus from a search box).
- `SetItemKeyOwner(key)` / `SetKeyOwner(key, id)` / `GetKeyOwner(key)`: minimal on purpose (`SetKeyOwner` names the owner id directly; 0 clears). The last item owns the key for this frame and the next; only `Shortcut` and `GetKeyOwner` look at it. `IsKeyPressed` still reports the raw key, because OkayUI has no global keyboard navigation that would need to be told to stay away.
- `GetStateStorage()` returns a small handle (`StateStorage`: int, bool, float and pointer values by id, per current window, a global one outside a window). 1,024 values in all; a full store drops new values and answers the default. `ResetState` empties it. It is a lookup in a fixed table, not Dear ImGui's sorted vector.

**Docking** (new `OkayUIDockBuilder.hpp`, `OkayUIDockBuilder.cpp`; all over the existing `DockLayout`, there is no second dock system). The builder keeps a plan tree of nodes with ids you choose; `DockBuilderFinish` writes the plan as a layout text blob (the layout's own save format) and loads it, so the layout's checks and repairs apply to whatever the plan holds. Calls: `DockBuilderAddNode`, `RemoveNode`, `SetNodeSize`, `SplitNode`, `DockWindow`, `Finish`, `GetNode` (returns a `DockNodeInfo` value), `SetNextWindowDockID(id, cond)` and `GetWindowDockID(window)`. Choices and differences from the Dear ImGui calls of the same names:
- `DockBuilderSplitNode(id, dir, ratio, &atDir, &opposite)`: `dir` is the side the new node goes on, `ratio` its share (kept between 0.02 and 0.98). The original id stays as the split parent; both halves get new ids, and windows already in the node go to the opposite half. The editor's `split(center, ...)` helper (which sets `from` to the opposite id) works unchanged.
- An empty node cannot exist in a `DockLayout`, so a plan node with no window at `Finish` is dropped and its sibling takes the space; its id answers `found` but not `live`. Docking a window into such an id after `Finish` waits for the next `Finish`.
- Sizes: `SetNodeSize` on the root is the area the tree fills; on a child it is a pixel size along the parent's split, turned into a ratio at `Finish` (ignored when the root has no size). After `Finish`, `SetNodeSize` on a live child moves the divider at once from the parent's last computed size (this is what the editor's animation window does).
- `SetNextWindowDockID` acts in the next `DockBegin`. `Cond_Always` re-docks every time; every other condition applies only while the window is not in the layout at all (a layout loaded from a save wins). `0` floats the window. An unknown id does nothing. One request, used up by the next `DockBegin`.
- One dock tree: `Finish` replaces the layout; windows the layout had but the plan does not name stay, floating where they were. Several dock spaces at once are not supported. `DockBuilderRemoveNode` of a root also empties the live layout.
- Not done: `DockBuilderGetCentralNode`, node flags (accepted and ignored), `DockBuilderCopy*`, `GetWindowDockNode`, node positions, `DockBuilderSetNodePos`.

**The editor's default layout, headless.** `test_okayui_dockbuilder.cpp` rebuilds the editor's `Default` preset with the same call sequence as `BuildLayoutPreset` in `editor/main.cpp` (remove, add, set size 1280 x 720, split left 0.20, right 0.42, bottom 0.22 off the remaining centre, thirteen panels docked), finishes it, draws it through `DockSpace` / `DockBegin` and checks the real `DockLayout` and its geometry: left column 255.2 px, right column 427.1 px, bottom group 157.5 px high (each within 1 px of the numbers worked out from the ratios), seven tabs along the bottom, three on the right, Scene and Game sharing the centre, only the chosen tab of each group drawing, a frame that is not one flat colour, and a save and load that returns the same text. The call sequence was copied by hand from the editor source; the editor itself was not built or run, so what the real editor does with it is unverified. Further cases: sizes in pixels, dropped empty nodes, re-finish keeps outside windows floating, removal, `SetNextWindowDockID` with every condition, bad arguments, long and odd window names, and a 4,000-call random run after which `Validate` must accept the layout.

**Mapping to the editor's calls (what a port would do):** `ImGui::DockSpace` is already `OkayUI::DockSpace`; `DockBuilder*` map as above; `SetNextWindowDockID(id, FirstUseEver)` maps one to one; `DockBuilderGetNode(id)->Windows` becomes `DockBuilderGetNode(id).windows`; the window-by-name pointer calls use `FindWindowByName` (6.8). `SaveIniSettingsToMemory` has no equivalent: the layout text is `DockSaveLayout()`.

### 6.10 Calls and names the editor and launcher still needed (unresolved-symbol count 181 to 119)

Measured first with `python3 scripts/imgui_usage_report.py` (it now also prints the distinct type count). By name, on this branch before the step: 264 distinct `ImGui::` calls (36 had no OkayUI function of that name), 26 draw-list methods (4 missing), 330 distinct types, flags and enums (141 missing): **181 unresolved symbols** over 28,025 call sites in `editor/`, `launcher/` and `shared/`. After the step: 13 calls missing, 0 draw-list methods missing, 106 types missing: **119**. Call-site coverage by name went from 99.7% to 99.9% for calls, from 80.5% to 86.5% for types, flags and enums, and from 98.7% to 100% for draw-list methods. The report counts a name as covered when OkayUI declares the same-named function or identifier, so this is a name count, not a promise that a screen ports without edits; where a call is accepted and does nothing the header comment says so, and the list below repeats it. The script's matching of `ImDrawFlags_*` was added (it could never match before).

Written from what the names mean (no Dear ImGui code or text read), declared in `OkayUICompat.hpp` and `OkayUI.hpp`, implemented in `OkayUI.cpp` (needs the private window state) and `OkayUICompat.cpp`, tested in `okayui/tests/test_okayui_calls.cpp` (105 checks, headless, three golden images). In order of how often the editor and launcher use them:

- **Names (several hundred uses).** `MouseButton_Left/Right/Middle` (Middle is accepted and never fires; before this step any button number except 1 acted like the left button, which would have made `ImGuiMouseButton_Middle` code fire on a left click, so `IsMouseDown/Clicked/Released` and `IsItemClicked` now answer false for other buttons), `Dir_*` for `ArrowButton`, `Hovered_*` and `Focused_*` flag names with `IsItemHovered(flags)`, `IsWindowHovered(flags)`, `IsWindowFocused(flags)`, `KeyId_UpArrow/DownArrow/LeftArrow/RightArrow/KeypadEnter` (aliases of existing keys). All flags are accepted; what each one changes is written at the enum (mostly nothing, because OkayUI's item hover already ignores disabled state and other active items; the hover delays are not applied).
- **Child flags (about 170 uses).** `BeginChild(id, Vec2 size, childFlags, windowFlags)`: `Child_Borders` draws the panel, `Child_AutoResizeY` makes the height follow the content one frame behind (the first frame shows one row; a positive `size.y` is the smallest height). Not done: `Child_AutoResizeX`, resizable children, `Child_FrameStyle` (accepted, ignored).
- **Draw list (31 uses).** `AddImageRounded` (17), `AddImageQuad` (5), `AddNgon` (4), `AddNgonFilled` (5), `DrawFlags_RoundCorners*`. The image calls use one textured triangle fan (centre first for the rounded one); the software test backend skips textured commands, so they are tested on the draw data: corners cut, every vertex inside the rectangle, texture coordinates linear in position, only the chosen corners rounded. Not done: corner flags for `AddRect` / `AddRectFilled`, a `Closed` flag form of `AddPolyline`.
- **Context shims (24 uses).** `CreateContext`, `DestroyContext`, `GetCurrentContext`, `SetCurrentContext`: one global context, so Create always returns the same handle; Destroy resets OkayUI's state and Get returns null until Create. `NewFrame` is not offered (call `BeginFrame` with the frame's `Input`).
- **Window calls.** `FindRenderedTextEnd` (4, the `##` suffix), `BringWindowToDisplayFront` (3, by title or from `GetCurrentWindow()`), `StyleColorsDark/Light/Classic` (5; they change only the colours, keep sizes and the text scale, and clear colours set with `SetStyleColor`; the three palettes are our own choices, golden images `calls_style_*`), `SetNextWindowBgAlpha` (2; scales the body's alpha, not the title or content), `SetNextFrameWantCaptureKeyboard` (2), `GetWindowViewport` and `SetNextWindowViewport` (one viewport; the second does nothing), `SetNextWindowCollapsed`, `SetWindowPos` (takes effect when the window is next begun, so a call inside the window moves it one frame later), `GetCursorStartPos`, `GetTreeNodeToLabelSpacing`, `FindWindowByID` (`docked` not filled), `GetTopMostPopupModal` (a yes/no, not a window), `PushItemFlag/PopItemFlag` (only `ItemFlag_Disabled` acts, as `BeginDisabled`; `MixedValue` and the others are accepted and ignored), `PushMultiItemsWidths` (pushes one width per item, the first on top, so the caller pops after each item as with Dear ImGui), `SaveIniSettingsToMemory` and `LoadIniSettingsFromMemory` (the dock layout text only, not window positions).

**Still unresolved calls (13):** `NewFrame` (4), `DockNodeGetRootNode` (2), `GetFontTexUvWhitePixel` (2, OkayUI draws flat colour from its own atlas; callers that build custom triangles with it need the draw-list calls instead), `GetPlatformIO` (2) and `UpdatePlatformWindows`, `RenderPlatformWindowsDefault` (multi-viewports), `BeginPopupEx`, `ClosePopupToLevel`, `DockContextQueueUndockWindow`, `DockNodeBeginAmendTabBar`, `DockNodeEndAmendTabBar`, `GetWindowDockNode`, `TabBarFindTabByID` (dock and tab internals, host-level code that would be rewritten, not ported). The largest unresolved types are `ImVec4` (1,053 uses, the host-side mapping layer of 6.7 covers it), `ImTextureID`, `ImGuiID`, `ImGuiWindow`, `ImGuiPayload`, `ImRect`, `ImFont` and the `TableColumnFlags`, `TabItemFlags`, `InputTextFlags`, `ColorEditFlags` and `ConfigFlags` families (full list in `docs/imgui_port_report.md`).

**Not verified.** Nothing here was run in the Windows editor or launcher, and none of it is called by them yet; OkayUI is still off by default (`OKAY_DEV=1` with `OKAY_UI_BACKEND=okayui`, Dear ImGui is the default). The tests were built and run by hand (the library sources and the few engine files they need compiled directly, then linked), not through the project's CMake and ctest in this session (the full engine build was too slow on the shared machine); the CMake line for `okayui_calls` was added but not run through ctest. Per owner rule 4a (2026-11-17) there is no `docs/try_it/` entry: there is nothing to click yet.

### 6.11 More names, the `##id` label fix (unresolved names 119 to 96)

By the same report: 13 calls and 83 types, flags and enums are still without a same-named OkayUI identifier (96 in all, was 119; call-site coverage of types and flags 86.5% to 97.5%). All tested in `test_okayui_calls.cpp` (now 125 checks), headless; nothing in the editor or launcher uses them yet.

- **`##id` labels.** Every labelled widget (button, checkbox, radio, slider, drag, input, combo, selectable, colour edit, tabs, menu items, tree nodes and the rest that draw a `label` argument) now draws and measures only the text before `##`; the id still hashes the whole label. Before, only `ListBox`, `InputText` width and a few others did, so a ported `SliderFloat("##s", ...)` printed `##s`. Test: nine widgets draw pixel for pixel the same with `Name##secret` as with `Name`, and `"##only"` is as wide as `""`. Only the three style goldens of step 6.10 changed (the slider lost its `##s` text); I looked at one. The other 18 suites that were run (including every golden suite) are unchanged.
- **Types host code spells:** `ImVec4` (a real struct with the four-float constructor, converting both ways with `Vec4`, 1,053 uses), `ImTextureID` (the `SDL_Texture*` that `AddImage` takes), `ImGuiID` (unsigned, what `GetID` returns), `ImRect` (own code: corners, size, centre, `Contains`, `Overlaps`, `Expand`; plus `GetItemImRect()`). They live in the `OkayUI` namespace so they cannot clash with Dear ImGui's own when both are compiled in.
- **Flag names.** `TableColumn_WidthStretch` (0, the default), `WidthFixed`, `NoSort`, `DefaultSort`, `PreferSortDescending`; `TabItem_SetSelected`, `Trailing`, `NoTooltip`; `StyleVar_WindowBorderSize`, `FrameBorderSize`, `ChildBorderSize` (all `StyleVar_BorderSize`, which already set the three together); `Col_TabSelected` (= `Col_TabActive`); `ColorEdit_AlphaBar` (0: the bar is on unless `NoAlphaBar`) and `ColorEdit_NoTooltip` (ignored); `TreeNode_SpanAvailWidth` (0: the default row already runs to the right edge).
- **New behaviour.** `InputText_Password` (one `*` per byte of text so caret and selection need no mapping; copy and cut are off; a multi-byte character shows as several stars), `InputText_CharsNoBlank` (typed spaces and tabs are dropped; paste is not filtered), `DrawFlags_Closed` with `DrawList::AddPolyline(points, n, col, int flags, thickness)`.

Still without a name: `ImGuiWindow` (41; OkayUI has no window objects, `CurrentWindow` and `WindowInfo` are values, so pointer-using callers must change), `ImGuiPayload` (36, `DragPayload` has other field names), `ImFont` and `ImFontConfig` (28), `ImGuiDockNode` (16), `ImGuiKey` (11), the popup flags (`AnyPopupId`, `AnyPopupLevel`, `MouseButtonRight`), `ImGuiDragDropFlags_AcceptNoDrawDefaultRect` (the outline is drawn when the target begins, before the accept flags are known), `ButtonFlags_MouseButton*`, `ConfigFlags_*` (host configuration, no meaning here), `SliderFlags_Logarithmic` and `AlwaysClamp`, `ComboFlags_HeightLarge` and `NoPreview`, `InputFlags_Repeat`, `InputTextFlags_CtrlEnterForNewLine`, keypad keys, and about 60 more with 99 uses in all (`docs/imgui_port_report.md`).

### 6.12 A real port probe: what stops `editor/ConsolePanel.cpp` from compiling against OkayUI

To find what a mechanical port still breaks on, I compiled a scratch copy of the editor's Dear ImGui Console panel (`editor/ConsolePanel.cpp`, 399 lines, not committed) with `ImGui::` replaced by `OkayUI::` (syntax check only, `g++ -fsyntax-only`, real `imgui.h` still included for its types, plus ImGui's own hook macros `IM_VEC2_CLASS_EXTRA` / `IM_VEC4_CLASS_EXTRA` so `ImVec2` and `ImVec4` convert to `Vec2` and `Vec4`). First run: 23 errors. The real gaps behind them were closed in this step, all in `OkayUI.hpp` / `OkayUICompat.hpp`:

- `Button(label, Vec2 size)` (412 call sites in the editor, launcher and shared code; size 0 fits the label or the row height, negative fills the line).
- `SameLine(offsetFromStartX, spacing)` (321 sites with a second argument).
- printf-style `Text`, `TextDisabled`, `TextWrapped`, `BulletText` and `TextColored(Vec4, fmt, ...)` (about 1,000 sites; 166 of the 181 `Text(` calls take arguments). They are templates that need one argument after the format, so `Text("100%")` still picks the plain call (a plain `const char*, ...` overload is ambiguous with it in C++).
- `BeginPopupContextItem()` with no id (keyed by the last item).

After that: 15 errors, none of them a missing OkayUI call. They are: the Dear ImGui enum types (`ImGuiCol_`, `ImGuiStyleVar_`; 10 errors, an artefact of still including `imgui.h`: a real port includes a mapping header instead, see `shared/OkayUIImGuiMap.hpp`), `GetWindowDrawList()` returning a reference where ported code writes `->` and passes an `ImDrawList*` (4 errors; the ported line changes `->` to `.`), and `GetStyle()` returning `StyleInfo` where a function takes `const ImGuiStyle&` (1 error). So the compile-level gap of this panel is now the type spelling, not missing widgets. It was **not run**: a compile check says nothing about behaviour. What does run headless is the OkayUI-native Console port (`okayui/screens`, `okayui_screens` and `okayui_panels` suites), as before. Not tried: the Project panel and any other panel.

### 6.13 The port probe, a correction to the counts, and what the probe found

**Correction.** Since 6.11 the report counted an enumerator such as `ImGuiWindowFlags_NoSavedSettings` as covered when OkayUI had any identifier spelled like the enum type (`WindowFlags`): a matching mistake that made every flag of an existing family look available. `scripts/imgui_usage_report.py` now requires an enumerator to match an enumerator. Measured again on the headers of the previous push (1ef4623b) with the fixed script: 13 calls and **110** types, flags and enums missing, **123** unresolved names, not the 96 reported in 6.11 (the 181 of 6.10 was measured with the same flaw, so it was also too low; the true start of this work was higher). After this step: 13 calls and **11** types missing, **24** unresolved names (319 of 330 distinct types, flags and enums covered; call-site coverage of types and flags 99.4%).

**The probe.** `scripts/okayui_port_probe.sh <files>` compiles Dear ImGui panels with `-fsyntax-only` against `okayui/probe/imgui.h`, a stand-in header that includes `okay/UI/OkayUIImGuiNames.hpp` (generated by `scripts/gen_okayui_imgui_names.py`, `--check` for staleness; 311 names: `ImGuiCol_Button = Col_Button`, `ImGuiWindowFlags_NoMove = Window_NoMove`, `ImVec2`, `IM_COL32`, `namespace ImGui = OkayUI`) and prints the errors by kind. With this there is no `imgui.h` in the probe, so the enum-type artefacts of 6.12 are gone. Nothing is linked or run. The names header does nothing when the real `imgui.h` is included first, and the real editor and launcher still compile with it (`launcher/main.cpp`, `editor/main.cpp`, `editor/OkayPanels.cpp`: `-fsyntax-only` with the real ImGui headers, no errors).

| Panel | Errors, first probe of this round | After |
|---|---:|---:|
| `editor/ConsolePanel.cpp` | 5 (15 with the real `imgui.h`) | 0 |
| `editor/ProjectPanelUI.cpp` (Project) | 20 | 0 |
| `editor/SceneManagerPanel.cpp` | 5 | 0 |
| `launcher/VersionsUI.cpp` (launcher page) | 27 | 0 |
| `editor/InspectorPanel.cpp` (Inspector) | 36 | 30 |

**Fixed in the compat layer (by how often the probe hit them):** `SetNextWindowPos(pos, cond, pivot)` and `BeginPopupModal(name, open, flags)` (16 + 3), `Dummy(Vec2)` (20), `GetMainViewport()->` and `Pos` / `Size` (1), `Selectable(label)` (3), `TreeNodeEx(id, flags, fmt, ...)`, `Image(ImTextureID, Vec2, ...)`, `Combo(label, int*, items, n)` and the zero-separated string form (2), `CalcTextSize(text, end, hideAfterHash)`, `Font::CalcTextSizeA`, `DrawList::AddText(font, size, ...)`, `AddRect` / `AddRectFilled` with corner flags (real: the corners are cut), `GetColorU32(ImVec4)`, `GetStyleColorVec4` returning `ImVec4`, and the **shapes** the coordinator asked for: `GetWindowDrawList()` and `GetForegroundDrawList()` return a `DrawListHandle` (a `DrawList`, so every existing `.` caller and `const DrawList&` still works, plus `->` and conversion to `ImDrawList*`), and `ImGuiStyle` is the name of `StyleInfo`, so `const ImGuiStyle& st = GetStyle();` compiles. `DragPayload` gained `Data`, `DataSize`, `IsDataType()` and `IsDelivery()` (the copies the calls hand out are filled). Added: key ids for punctuation and the keypad and the left / right modifier names, `InputText_CharsDecimal` (real), `Window_NoDecoration` (real), and accepted-and-ignored names for window, table, selectable, tab bar, button, popup, slider, combo, config and backend flags, `ImFont`, `ImFontConfig`, `ImColor`, `ImWchar` (each flag says in `OkayUICompat.hpp` whether it does anything).

**What still stops the Inspector (30 errors):** it reads Dear ImGui internals (`GImGui`, `ImGuiContext` fields `ActiveId`, `HoveredWindow`, `ImGuiWindow::Name` and `ParentWindow`, `ImGuiDockNode`). OkayUI has no window or context objects, so that code needs a rewrite against `GetCurrentWindow()`, `GetActiveID()` and `FindWindowByName()`, not a rename. Still without a name: `ImGuiWindow`, `ImGuiDockNode`, `ImGuiTabItem`, `ImGuiInputTextCallbackData`, `ImGuiPlatformIO`, `ImGuiKey_MouseWheelY`, `ImGuiAxis_Y`, `ImGuiSortDirection_Ascending`, `ImGuiTableBgTarget_CellBg`, `ImGuiStyleVar_GrabMinSize`, and the 13 calls of 6.10.

**Not verified.** The probe is a compile check: it says the code is spelled right, not that it does the right thing. Nothing was run in the editor or launcher. New tests (`okayui_names`, 18 checks, and the existing 129 `okayui_calls` checks) were built by hand and run headless with the other 19 suites; not through CMake or ctest.

### 6.14 Running the probed panels headless

6.13 only compiled the panels. This step builds each as a headless test (`okayui_port_*`, helper `okayui_port_test` in `okayui/CMakeLists.txt`): the real editor or launcher source file, compiled against OkayUI through `okayui/probe/imgui.h`, with fake engine state and stubs for what `main.cpp` provides, drawn into the software backend. Built and run by hand (g++ and a hand link, not CMake or ctest; the CMake lines are untested).

**Console (`editor/ConsolePanel.cpp` + `editor/EditorWidgets.cpp`, test `okayui_port_console`, 6 checks, golden `port_console_first`).** Five fake log lines, 4 frames, then clicks along the toolbar, typing, scrolling, 400 more lines, a blocked pointer and a 40 x 30 display: no crash, the draw data is not empty and nothing is dropped. The time stamps come from `time()`, so the test defines its own fixed `time()` (the golden would change every second otherwise). The first run showed real breakage, all in the compat layer, fixed here:
- window titles with `###id` were drawn whole (`Console  [x 1]###Console`): the title now stops at `##` like every other label;
- `BeginChild(id, ImVec2(0, -detailsHeight))` ignored the negative height (the list got four rows and the newest lines scrolled out of view): in the `Vec2` form a size of 0 is the space left and a negative size is the space left minus that much, on both axes;
- `ImVec2` is now `Vec2` itself (it has a two-float constructor and `operator[]`), so `ImVec2 pts[6]` can go to `AddPolyline(const Vec2*, ...)`; `IM_PI`, `DrawList::AddEllipse` / `AddEllipseFilled`, the Dear ImGui field names on `IO` (`MousePos`, `DeltaTime`, `ConfigFlags`, ...; `GetIO()` is not const) and a non-const `GetStyle()` (a write is still lost) were needed by `EditorWidgets.cpp`.
I looked at the picture: all five rows, the pills with counts, the search field, the category combo and the details area are where the Dear ImGui code puts them. Differences seen: the font is OkayUI's, icons are the panel's own drawing, no hover or active colours were checked.

**Project panel pieces (`editor/ProjectPanelUI.cpp`, `editor/ProjectMenuDraw.cpp`, `editor/ProjectBrowser.cpp`; test `okayui_port_project`, 16 checks, four golden images).** The name prompt (a modal popup with a text field, hint, error line and two buttons), the property rows table and the right-click context menu with a submenu, a disabled entry and shortcuts, all from hand-made data. The prompt is driven with typing and Enter: `accept` is called with the typed name plus the suffix, a refusing `accept` leaves the prompt open with its message, Escape closes it, and a 300-character name and an empty title do not crash. What running showed, fixed here:
- `ProjectMenuDraw.cpp` did not even compile (it was not in the 6.13 probe list): `BeginMenu(label, enabled)` and `BeginTable(id, n, int flags)` added;
- `MenuItem` and `BeginMenu` only worked in a menu bar, so a context menu built from them was empty (a blank popup with one line). Inside a plain popup `MenuItem` is now a selectable row that closes the popup, with the shortcut at the right and room for it, and `BeginMenu` expands its entries in place, indented (a real submenu to the right is only available in a menu bar).;
- `BeginPopupModal` drew its name as a title (`##projname`): it now hides the `##` part, and `Window_NoTitleBar` removes the title line;
- `InputText_AutoSelectAll` did not apply when the focus came from `SetKeyboardFocusHere()` (typing appended to the old name instead of replacing it): fixed.
Pictures were looked at: the prompt is centred and readable, the error is shown in red under the field, the table has the label and value columns with the orange warning row, and the context menu lines up shortcuts. Not run: the delete confirmation, the references window, the import settings editor and `ShowProperties` (they need an `okaypb::Browser` over an asset database).

**Launcher Versions page (`launcher/VersionsUI.cpp` over `VersionsManager.cpp` and `VersionsNet.cpp`; test `okayui_port_versions`, 6 checks, golden `port_versions_page`).** A temporary launcher folder with two installed versions, an offline host (the text fetch always fails, so nothing is downloaded), no-op callbacks, the launcher look. Six frames, then a grid of clicks over the whole page, wheel scrolling and typing, a blocked pointer and a 60 x 40 display: no crash, nothing dropped, `InstalledVersionsCached` finds both folders. The picture shows the real page: the section headers, the "Checking..." and "Open install folder" buttons on the right (`SameLine(offset)`), the channel combo, the explanation, the default-editor combo. Found and fixed by running it: a paragraph shown with `Text` or `TextWrapped` was double-spaced because every line took a framed widget's row height, and `Text("a\nb")` drew a single row. Now all lines of a multi-line paragraph but the last sit close together (text height + 6); a one-line text is unchanged. This moved two golden images of the earlier ported screens (`whatsnew_idle`, `whatsnew_available`: the wrapped bullet's first line is 4 px higher than its bullet, because the host draws the bullet from the old row height; the screen code was not changed). The paragraph pitch in the Versions picture is still wider than Dear ImGui's about 15 px (36 px between two lines): not understood yet, not fixed. Not run: installing, removing or updating a version, and the host callbacks.

**SceneManager panel (`editor/SceneManagerPanel.cpp`): compiles clean (6.13) but not run.** `DrawSceneManager(EditorState&)` needs a real `okay::editor::EditorState`, which owns a `Scene`, a `GameLoop`, an undo stack and the scene serializer (`editor/EditorState.cpp` is 1,658 lines over most of the engine). Hand-linking that was not feasible here (the engine is about 385 source files and the shared machine builds a file every minute or two); the panel also calls `main.cpp` functions (`RunAfterSavePrompt`, `ShowErrorPopup`, `AddRecent`, `GetSceneThumb`). It needs a CMake-built test linking the whole engine, with those four functions stubbed in the test. Not done.

### 6.15 The Inspector: the fields it reads, and the accessor layer

`editor/InspectorPanel.cpp` reads these Dear ImGui internals (all in `InspectorPanel.cpp`, found by the probe; `imgui_internal.h` is included for them):

| Read | Where | OkayUI equivalent (`OkayUIInternals.hpp`) |
|---|---|---|
| `GImGui` | line 83 | `GImGui` is a macro for `OkayUI::RefreshContext()`, which fills the one global `Context` from OkayUI's state and returns it |
| `ImGuiContext::ActiveId` | 86, 95 | `Context::ActiveId`: the item being pressed or dragged (`GetActiveID()`) |
| `ImGuiContext::ActiveIdWindow` | 86 | `Context::ActiveIdWindow`: the focused window while an item is active (a press gives its window the focus first, so it is the same window in practice; the window the press began in is not tracked) |
| `ImGuiContext::HoveredWindow` | 91 | `Context::HoveredWindow`: the topmost window under the pointer, null when none or when the pointer is blocked |
| `ImGuiWindow::Name` | 37, 38 | `WindowRef::Name`: the title the window was begun with (kept per window slot now; 63 characters) |
| `ImGuiWindow::ParentWindow` | 36 | `WindowRef::ParentWindow`: always null (a `BeginChild` region is not a window here, so the walk up ends at the window) |
| `ImGuiPayload::Data`, `IsDataType` | 101 to 203 | `DragPayload::Data`, `IsDataType` (6.13) |
| `ImGuiItemFlags_MixedValue` | 323 | `Item_MixedValue`, accepted and ignored |
Also added for it: `PushID(pointer)` (the pointer becomes the id seed; two buttons with the same label under different pointers are separate), `IsMouseHoveringRect(min, max, clip)`.

The objects are snapshots, not live handles: `ImGuiWindow` is `WindowRef`, `ImGuiContext` is `Context`, and a pointer to a window is valid until the next `RefreshContext()` (so use it inside the frame, as the Inspector does). With this the probe reports **0 errors** for `InspectorPanel.cpp` (it was 30). Tested in `okayui_names` (hovered, current and active window names and ids with two windows, a blocked pointer, `GImGui`, `PushID(&a)`). The Inspector was **not run**: it needs the scene, the selected object, the undo stack and every component editor of the engine, which cannot be hand-linked here; a CMake test with a small scene is the next step.

### 6.16 The last calls and names: implemented, and host-specific

After 6.15 the report had 13 calls and 10 types, flags and enums without a same-named OkayUI identifier (24 unresolved by the 6.13 count, which became 23 once `ImGuiWindow` was named). Now **7 calls and 5 types are left: 12 unresolved** (call-site coverage by name: calls 100.0%, types and flags 99.8%; 257 of 264 distinct calls, 325 of 330 distinct types, flags and enums).

Implemented (tested in `okayui_names`, 33 checks): `NewFrame()` (BeginFrame with the last frame's input, not a platform read), `UpdatePlatformWindows()` and `RenderPlatformWindowsDefault()` (no-ops: no extra windows), `GetPlatformIO()` (a `PlatformIO` with `Viewports.Size` = 1 and the renderer / platform callback fields stored and never called), `ClosePopupToLevel(level, restoreFocus)` (0 closes every open popup), `DockContextQueueUndockWindow(ctx, window)` (floats a docked window at its place; tested with a docked and an unknown window), `Axis_*`, `SortDirection_*`, `TableBgTarget_*` and `Popup_NoOpenOverItems` names.

**Host-specific, rewrite (not offered), with what each is for:**
- `GetFontTexUvWhitePixel` (2 uses, `SplineTools.cpp`, `UIPreviewPainter.cpp`): the white texel of Dear ImGui's font atlas, used with `PrimReserve` / `PrimVtx` to write triangles into the draw list. OkayUI has no font atlas to point into and no vertex-level draw list; those two places should use `AddConvexPolyFilled`, `AddImageQuad` or `AddImageRounded`.
- `BeginPopupEx(id, flags)` (1, `main.cpp`: the panel-tab list popup): a popup by numeric id with window flags; use `OpenPopup(name)` / `BeginPopup(name)`.
- `DockNodeGetRootNode` (2), `GetWindowDockNode` (1), `DockNodeBeginAmendTabBar` / `DockNodeEndAmendTabBar` (1 each), `TabBarFindTabByID` (1), and the type `ImGuiDockNode` (16 uses): `main.cpp`'s panel tab strip draws extra buttons inside Dear ImGui's dock-node tab bars. OkayUI's dock tabs are its own (`OkayUIDock.hpp`); this code would be rewritten against `DockLayout`, not ported.
- `ImGuiInputTextCallbackData` (2) and `InputTextFlags_Callback*`: OkayUI text fields have no callbacks.
- `ImGuiTabItem` (3, an internal type), `ImGuiKey_MouseWheelY` (5, a mouse wheel read as a key: use `GetIO().mouseWheel`), `ImGuiStyleVar_GrabMinSize` (1: no slider grab size setting).

**Counts over this round (probe and names; same script):** unresolved names 24 -> 12; probes of real panels with 0 errors: Console, EditorWidgets, Project (3 files), SceneManager, Inspector, Versions page (9 files in all, was 4 of 5 panels); run headless: Console, Project pieces, Versions page (3 tests, 28 checks, 8 golden images). Built and run by hand with g++ (not CMake or ctest); the CMake lines for the three port tests are untested.

### 6.17 More panels run headless, and the paragraph pitch solved

**The paragraph pitch (the 36 px on the Versions page).** The last line of a multi-line text was drawn centred in a full framed-widget row (31 px), so its text sat about 5 px lower than a tight pitch would put it, on top of the line gap. Now `ParaLines` draws the first line exactly like a one-line text (so a bullet or icon aligned to it stays aligned: the What's New bullet misalignment is gone, goldens `whatsnew_idle` and `whatsnew_available` regenerated and looked at) and puts every further line one text pitch (text height + 4) below the previous line's text. The Versions page paragraph is now 20 px between lines (it was 36); `port_versions_page` and `port_project_properties` regenerated.

**Project dialogs over a real asset database (`okayui_port_project2`, 16 checks, five golden images).** The delete confirmation, references window, broken-references list and "move to" picker run over a real `okaypb::Browser` and a real `okay::AssetDatabase` on a temporary project (a material, two scenes that use it, one scene that points at a missing texture); the database finds the two users and the missing file. The red Delete button is found by its colour in the picture and clicked: the confirm callback runs once, Escape and Cancel do not call it. Not golden: the Properties window (it prints times relative to now). The import settings editor is not drawn (a material has none; no image was written), and `HasImportSettings` answers true for an unindexed `.png` path (it looks at the extension only; not asserted). Found and fixed by running:
- `BeginPopupModal` ignored `SetNextWindowSize` (every dialog was 320 px wide with its text running out of the box) and left a `SetNextWindowPos(center, ...)` pending, which would have moved the next window: a modal now takes its width from `SetNextWindowSize` (the height follows the content) and consumes the position (it is always centred);
- a button label much wider than its button ("Fix stale references" in 170 px) spilled over its neighbours: it is now shortened with "..." when it is more than 16 px too wide (a clip rectangle does not reach the overlay layer that popups draw in, so clipping was not an option). Labels up to 16 px too wide are left as they were (the `primitives` golden has such a button).
Still open, seen in the pictures: a popup does not grow to its widest line (Dear ImGui's `AlwaysAutoResize` does), so long unwrapped lines run past the box.

**Scene Manager (`okayui_port_scenemanager`, 6 checks, golden `port_scenemanager_list`).** Run over a stand-in `EditorState` (`okayui/tests/shadow/EditorState.hpp`, selected by the macro `OKAYUI_PORT_STAND_IN_EDITORSTATE`, which `editor/EditorState.hpp` now honours with a three-line `#ifdef`; nothing else in the editor changed) and a temporary project with three scene files. The list shows the three scenes with tick boxes and Open / Rename / Duplicate buttons, and the explanation text wraps at the window width. A grid of clicks over the panel, typing, wheel, Play on and off, no project, a blocked pointer and a 50 x 30 display: no crash, nothing dropped. The engine pieces it needs were linked from source (`SceneManager.cpp`, `Scene.cpp`, `GameObject.cpp`, `AssetManager.cpp`, ...); the create-scene template path (which builds real scene objects and serializes them) was not driven. In the picture the em dash in "(none - the open scene)" is blank: characters the built-in font lacks are drawn by a fallback font through a texture, which the software test backend skips (a test backend limit, not a panel bug).

**Names.** `OpenPopup(id)` and `BeginPopupEx(id, flags)` by id are implemented (a popup opened by name has the same id as `GetID(name)`, so the forms mix; tested), which takes the 12 unresolved names of 6.16 to **11** (6 calls: `GetFontTexUvWhitePixel`, `DockNodeGetRootNode`, `GetWindowDockNode`, `DockNodeBegin/EndAmendTabBar`, `TabBarFindTabByID`; 5 types: `ImGuiDockNode`, `ImGuiInputTextCallbackData`, `ImGuiTabItem`, `ImGuiKey_MouseWheelY`, `ImGuiStyleVar_GrabMinSize`), all host-specific as written in 6.16.

**Inspector, Timeline and Animator windows.** Probed with the port probe: `InspectorPanel.cpp`, `TimelineWindow.cpp` and `AnimatorWindow.cpp` give 0 errors (the last two needed `ImMin` / `ImMax` / `ImClamp` / `ImLerp`, `IM_ARRAYSIZE`, `ImFont*` from `GetFont()`, `DrawList::AddText(font, size, ...)` with an `ImVec4` clip, `PushClipRect(min, max, bool)`, `TreeNode(id, fmt, ...)`, `InputInt(label, v, step, stepFast)`, `BeginDragDropTargetCustom(ImRect, id)` and `Context::HoveredIdTimer`). They are **not run**: they read `ed.scene()` and the object components, so they need a constructed `okay::Scene` (hand-linking `Scene.cpp` pulls Physics2D, audio, net and the scripting VMs: the engine library). The Hierarchy panel is a function inside `editor/main.cpp` (`DrawHierarchy`, a 50,000-line file with the whole editor), so it cannot be probed or run on its own; it has no file of its own to take.

### 6.18 Switching the editor to OkayUI at run time: what is read, what must be replaced, and the host half

**What decides it today.** `OKAY_DEV=1` (developer flag) plus `OKAY_UI_BACKEND=okayui` (or the saved setting, View > Rendering & Performance in the editor, Settings in the launcher; `shared/OkayUIBackend.hpp`, `ResolveUiBackend()` in `editor/main.cpp` line 2229, `UseOkayUIBackend()` line 2230). It only chooses, per ported panel, whether the panel draws with OkayUI **inside a Dear ImGui window** (`okayuihost::Region` rectangle, `OkayUI::EndFrame(renderer)` after ImGui's draw data). Dear ImGui still owns the window, the events, the frame and every other panel. Nothing in `main.cpp` runs without it.

**Every Dear ImGui backend call the editor makes (`editor/main.cpp`), and its replacement in the OkayUI host:**

| Call (line) | Replaced by |
|---|---|
| `ImGui::CreateContext()` (50449), `DestroyContext()` (52434) | `OkayUI::CreateContext()` / `DestroyContext()` (shims; one global context) |
| `io.ConfigFlags |= NavEnableKeyboard / DockingEnable / ViewportsEnable` (50451 to 50452, 50632, 51512 to 51514, 52284 to 52401) | accepted names, nothing reads them: no keyboard navigation, one dock space, one viewport |
| `io.Fonts->AddFontFromMemoryCompressedBase85TTF` x3, `AddFontDefault` (50474 to 50490: UI font, heading font, code font) | **a gap:** OkayUI has one active font (`UseLauncherLook`, `SetFont`, `PushFont` swaps the typeface). Per-draw heading and code fonts need `PushFont` with real TTF fonts and a size per call |
| `ImGui::StyleColorsDark/Light()` (5232) | `OkayUI::StyleColorsDark/Light()` (done in 6.10) |
| `ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)` (50625) | `okayuihost::platform::Host(painter)`: no SDL window or renderer is handed over, the loop owns an `okay::platform` window and a painter |
| `ImGui_ImplSDLRenderer2_Init(renderer)` (50626) | the `FrameDrawer` inside `Host` (uploads the glyph atlas and fallback fonts as painter textures, keeps them) |
| `SDL_PollEvent` + `ImGui_ImplSDL2_ProcessEvent(&e)` (50879, 50913) | `backend->PollEvent(e)` then `host.Feed(e)` (`okayuihost::platform::EventCollector`: mouse, wheel, text, keys, IME composition, close, resize) |
| `ImGui_ImplSDLRenderer2_NewFrame()` (51516), `ImGui_ImplSDL2_NewFrame()` (51517), `ImGui::NewFrame()` (51550) | `host.BeginFrame()` (display size from the painter, the frame's `Input`, `OkayUI::BeginFrame`) |
| `ImGui::Render()` (52280), `GetDrawData()`, `ImGui_ImplSDLRenderer2_UpdateTexture`, `RenderDrawData` (52288 to 52295) | `host.EndFrame()` (`EndFrameData`, clear, `FrameDrawer::Draw`, `Present`) |
| `UpdatePlatformWindows`, `RenderPlatformWindowsDefault`, `GetPlatformIO` renderer callbacks (50101, 50872, 52393) | no-ops / stored and never called: one window |
| `ImGui_ImplSDLRenderer2_Shutdown()`, `ImGui_ImplSDL2_Shutdown()` (52432 to 52433) | destroy the `Host` |
| `SDL_RenderPresent(renderer)` (52388), `SDL_CreateRenderer` (50389) | `painter.Present()`; the painter is made with `OkayUI::soft::CreateWindowPainter` or `okay::platform::CreateSdl2Painter` |
| `io.WantCaptureKeyboard`, `SetNextFrameWantCaptureKeyboard` | `host.WantsTextInput()`; the loop calls `Backend::StartTextInput` / `StopTextInput` and `SetTextInputRect` from it |

The launcher has the same set (lines 18230 to 18248 setup, 18404 to 18406 events, 18671 to 18673 new frame, 27087 to 27094 render, 27143 to 27144 shutdown; and a second context for the chat window at 16715 to 16830).

**The host half (`shared/OkayUIHost.hpp`, `okayuihost::platform`, switched on by `#define OKAYUI_HOST_PLATFORM`).** `EventCollector` turns `okay::platform::Event` into one frame's `OkayUI::Input` (the platform twin of the SDL `InputCollector`; key map `KeyIdOf(okay::platform::Key)`). `Host` holds a painter, a `FrameDrawer` and the collector: `Feed(event)`, `BeginFrame()`, `EndFrame()`, `WantsTextInput()`, `QuitRequested()`. It needs no SDL window and no Dear ImGui. Test `okayui_host_platform` (headless, a software bitmap painter and scripted events): a button click as separate events and as press and release inside one frame, right button ignored, typing, Backspace, Ctrl+A, Escape, a checkbox, wheel, IME composition, resize, close request, and a flood of 1,000 hostile events and a 100,000-character text input; the frame is read back from the painter (pixels not empty). **Not done:** `main.cpp` does not use it. Replacing the Dear ImGui loop needs every panel of the 50,000-line file ported first (the editor's panels still call `ImGui::` as is), and the editor's dock space (`DockBuilder` code, tab strips) rewritten against `OkayUIDock.hpp` (see below).

**Probing `editor/main.cpp` (`-fsyntax-only` against `okayui/probe`, new stand-ins for `backends/imgui_impl_sdl2.h` and `imgui_impl_sdlrenderer2.h` that only declare the calls above).** First run: 436 errors. By kind, with what was done:
- Argument shapes (fixed in the compat layer, now gone): `DragFloat2/3/4`, `DragFloatRange2`, `SliderFloat2/3/4`, `InputFloat` with `fmt` and `flags` (40); `SetNextItemOpen(open, cond)`; `IsPopupOpen(name, flags)` (any popup); `BeginPopup(name, flags)`; `SetKeyboardFocusHere(offset)`; `TreeNodeEx(label)` with one argument and `TreeNodeEx(ptr, flags, fmt, ...)` (13); `ColorButton` with a vector colour (6); `PlotLines` / `PlotHistogram` with Dear ImGui's list (4); `RadioButton(label, bool)`; `DrawList::AddText(pos, col, text, end)`; `ImColor(packed)`; `PushFont` / `PopFont`; `GetCurrentWindow()->`; `FindWindowByName(...)` used as `ImGuiWindow*` and with `->Pos`; `IsWindowDocked()`; `DockBuilderSplitNode` with an `ImGuiDir` int; `SaveIniSettingsToMemory(size_t*)`; style fields (`TabRounding`, `PopupBorderSize`, ...) and `io.MouseWheelH`, `DisplayFramebufferScale`; `ImGuiStyle::Colors` as `ImVec4`; `DrawList::AddConcavePolyFilled` (ear clipping); `ImGuiInputTextCallbackData` and the callback overloads of `InputText` (the callback is accepted and never called); `ImGui` is now a real namespace (`using namespace OkayUI`), because `ModelingHighlight.hpp` adds to it.
- Left, 284 errors, all Dear ImGui internals that OkayUI does not have and that are host-specific (the panel code reads them, so those lines are rewritten, not renamed): dock nodes (`window->DockNode`, `DockId`, `ImGuiDockNode`, `WindowClass`, `DockOrder`, `_Splitter`, `TabId`, `ImGuiTabBar`: about 80 errors in the panel-tab strip and layout code); navigation and window internals (`NavWindow` 15, `RootWindow`, `Windows`, `WasActive`, `Active`, `InnerClipRect`, `MoveWindowToFront`: about 35); draw-list channels (`ChannelsSplit`, `ChannelsSetCurrent`, `ChannelsMerge`, 12: layers inside one draw list; OkayUI has a window list and a foreground list); backend objects (`io.Fonts`, `io.InputQueueCharacters`, `io.AddKeyEvent`, `ImDrawData`, `VtxBuffer`, `RendererUserData`, `ImTextureStatus_*`, `DC`, `PlatformImeData`: about 40: these are the replaced backend); `ImGuiViewport*` and `ImGuiStorage*` taken as pointers where OkayUI hands out values.

### 6.19 Popups that grow, the import settings editor, and the item-width rule

**Popups grow to their widest line.** A popup or context menu now measures the right edge of its content (`maxRight`) and is that wide the next frame (never wider than the display less 16 px). Context menus no longer clip their shortcut column (golden `port_project_contextmenu`, `port_project_contextmenu_sub`, looked at). A popup whose width was asked for (`SetNextWindowSize` before `BeginPopupModal`) keeps that width: items that fill the width (a field with `SetNextItemWidth(-suffix)`) would otherwise make it grow every frame; the first version of this change did exactly that (the name prompt grew to the whole display), found by the golden image. Dear ImGui's `AlwaysAutoResize` modals are therefore still fixed-width here when a width was given.

**Import settings editor (`okayui_port_project2`, now 26 checks, three new goldens).** A real 16 x 16 PNG (written with `okay::ImageWrite`) and a real 200-byte WAV in the temporary project. The texture editor draws (the "Largest side" combo); on the audio file the "Mix down to one channel" checkbox is clicked, Apply and Revert become enabled, Apply is clicked and `Browser::GetImportSettings` afterwards shows the flag flipped: the settings were saved. Found by running: **an explicit item width was the whole row in OkayUI but only the field in Dear ImGui.** `SetNextItemWidth(170)` before a combo with a visible label made the box 48 px wide and put the label on top of it. For the combos (`BeginCombo`, `Combo`) an explicit width is now the box's and the label comes on top of it. The other labelled widgets (sliders, drags, input fields) still treat it as the row; their labels are mostly hidden (`##id`) where a width is set, so this was not changed. Four Console screen goldens moved by 3 px (the category combo box), looked at.

**Not done: the Create-scene template path of the Scene Manager.** `WriteTemplate` builds real `okay::Scene` objects and serializes them (`SceneSerializer::SaveToFile`); a `Scene` pulls Physics2D, audio, net and the scripting VMs, which cannot be hand-linked here. It needs the engine library in a CMake test (CI builds it).

### 6.20 Dock nodes, tab strips and the window table (round 7, step 1)

editor/main.cpp drives Dear ImGui's dock internals for its tab menu, tab icons, tab overflow list, side tabs and the "Collapse Group" entry. The probe listed about 135 errors from them. The design that answers all of it (headers `OkayUIInternals.hpp`, `OkayUIDock.hpp`, `OkayUIDockBuilder.hpp`; code `okayui/src/OkayUIDockNodes.cpp`; test `okayui_docknodes`, 65 checks):

- **One window table.** `WindowRef` entries (up to 256) live for the whole program and keep their address per window name, so `FindWindowByName("Inspector")`, `g.Windows`, a dock node's `Windows` and `GetCurrentWindow()` all give the same pointer. Fields: `Active`/`WasActive`, `DockNode`, `DockId`, `DockIsActive`, `DockTabIsVisible`, `DockOrder`, `TabId`, `DC.DockTabItemRect`, `InnerClipRect`, `DrawList`, `WindowClass`, `RootWindow` (itself) and so on. `WasActive` is "drawn this frame or last", so a window that just appeared reads as `WasActive`.
- **Dock nodes, read from the live layout.** `ImGuiDockNode` is `DockNodeRef`: `ID`, `ParentNode`, `ChildNodes[2]`, `Windows` (tab order), `TabBar` (`BarRect`, `VisibleTabId`, `SelectedTabId`, `Tabs`, `WidthAllTabsIdeal`), `HostWindow`, `Pos`, `Size`, `SizeRef`, `SplitAxis` (`Axis_X` = halves side by side), `IsLeafNode()`, `IsSplitNode()`, `IsNoTabBar()` and so on. `IsCentralNode()` is always false (no pass-through centre in OkayUI's dock). `g.DockContext.Nodes.Data[i].val_p` lists every node. `DockBuilderGetNode(id)` now returns the pointer; the old value form is `DockBuilderGetNodeInfo` (the dock-builder test was renamed).
- **Node ids.** A node from a plan keeps the plan's id; the root answers to the id given to `DockSpace(id, size, flags)`; any other node (made by dragging a window to a side) has the derived id `kDockDerivedBase + layout id`. `DockBuilderDockWindow`, `DockBuilderSetNodeSize` and `SetNextWindowDockID` accept all three. A derived id stays valid while that layout node exists.
- **Writes are applied at the next refresh or DockSpace**, by comparing with what was handed out: `node->SizeRef[axis]` (as `DockBuilderSetNodeSize`, so "Collapse Group" works), moving a window to the front of `node->Windows` (that tab becomes the chosen one: the side-tabs "move to front"), and `WindowClass.DockNodeFlagsOverrideSet |= DockNode_NoTabBar` (the node's strip is hidden: no strip is drawn, the content takes the strip's height; it is not saved in the layout text). Bad numbers (NaN, huge, negative) are refused by the layout's own limits (the random test keeps the layout valid).
- **Strip buttons.** `DockNodeBeginAmendTabBar(node)` then `SetNextItemWidth` / `TabItemButton("##x", Tab_Trailing)` puts a button at the right end of that node's strip (outside any window it opens a see-through window over the strip); `DockNodeEndAmendTabBar()` makes the strip leave that room from the next DockSpace (`SetDockTrailingReserve`), so tabs shrink instead of running under the button.
- **Hover.** Over the dock space (strips, empty nodes) `g.HoveredWindow` is the dock host entry (`##DockSpaceHost`), which is every node's `HostWindow`.
- **Not done / different:** one draw batch for all windows, so `HostWindow->DrawList` is the same list as `GetWindowDrawList()`; the tab-bar `Flags` has only `TabBar_IsFocused`; tab items have no `Flags` other than 0; an un-finished plan node read through `DockBuilderGetNode` has windows and size from the plan but no strip. None of this runs in the real editor yet (main.cpp still uses Dear ImGui); it compiles in the probe and is checked headless.

### 6.21 Draw-list channels (round 7, step 2)

`ChannelsSplit(n)`, `ChannelsSetCurrent(i)`, `ChannelsMerge()` and the `_Splitter._Count` / `_Current` fields on the window draw list (`OkayUI.cpp`, test `okayui_channels`, 54 checks). The batch stores triangles as index runs over shared vertices, so a channel is a saved run: switching saves the current run, cuts the batch back to the split point and lays the other channel's run down again; a merge lays all runs down in channel order. Each channel starts from the clip and texture state at the split and keeps its own pushes. 2 to 8 channels; a second split, a bad index or a merge without a split do nothing. A split left open is merged when its window ends and when the frame ends. The foreground list has no channels (calls accepted, no effect, `_Count` stays 1). Cost: a switch copies the run (linear in the geometry on the channel); fine for the handful of switches the visual-script editor and the dock host make, not for thousands per frame. Checked by a random run that compares the picture with the same shapes drawn in channel order.

### 6.22 Host shapes: Dear ImGui's draw data, typed characters, fonts, viewport and the small pointer forms (round 7, step 3 to 5)

What editor/main.cpp reads or writes on the Dear ImGui side that a host (or a renderer written for Dear ImGui) touches, now as OkayUI shapes (test `okayui_host_shapes`, 38 checks; the probe of editor/main.cpp is 282 -> 0 errors, see below):

- **Draw data.** `ImDrawData`, `ImDrawList`'s `VtxBuffer` / `IdxBuffer` / `CmdBuffer`, `ImDrawVert` (pos, uv, col as 0xAABBGGRR), `ImDrawIdx` (32-bit), `ImDrawCmd` (`ClipRect`, `TexRef`, `IdxOffset`, `ElemCount`, `GetTexID()`), `ImTextureData` / `ImTextureRect` / `ImTextureStatus_*`, `ImDrawCallback_ResetRenderState`. `ImDrawData* dd = GetDrawData();` converts OkayUI's own `DrawData` (one list with every vertex, index and command; the launcher look's atlas as `Textures[0]`; the cmd's `TexRef._TexData` points at it for atlas draws, `_TexID` carries the draw data's own texture value for all). So the custom renderer in main.cpp (`OkayRenderDrawData`) compiles and reads the same fields. The pointer is one static object that the next conversion rebuilds. `Render()` is `EndFrameData()`. This is a bridge for moving over, not the final path: the host half (`okayuihost::platform::Host`) draws through `FrameDrawer` and does not need it.
- **Typed characters and fed input.** `io.InputQueueCharacters` (UTF-16 units) is filled by `GetIO()` from this frame's text; removing characters from it (`resize(0)`) makes the OkayUI text fields skip this frame's typing (not the code editor). `io.AddInputCharacter(cp)` and `io.AddKeyEvent(key, down)` collect input that `NewFrame()` applies: the text, `keyDown[key]`, and the one-frame flag for Enter / Escape / Backspace / Delete / the arrows / Home / End. `NewFrame()` now clears the one-shot parts of the reused last input (typed text, key flags, wheel) first; before this it repeated them. Bad keys and lone surrogates are ignored. `io.KeySuper` exists and is false unless the host sets it.
- **io.Fonts.** `AddFontDefault`, `AddFontFromMemoryTTF`, `AddFontFromMemoryCompressedBase85TTF`, `AddFontFromFileTTF`, `Build`, `Clear`: each call is recorded (`FontRequests()`: kind, size, source, merge flag, the returned font) and answered with a stable `ImFont*` that works with `PushFont`. Since round 8 they make real fonts (6.23); before, nothing was loaded.
- **Viewport.** `ImGuiViewport* vp = GetMainViewport();` points at one persistent object (`ID`, `PlatformHandle`, `RendererUserData`, `DrawData` keep what the host wrote; the size is refreshed on each call). `GetMainViewport()` as a value still works. `PlatformIO::Renderer_CreateWindow` / `Renderer_DestroyWindow` / `Renderer_RenderWindow` and the platform ones are typed function pointers now (stored, never called: OkayUI has one viewport).
- **Pointer-like forms.** `GetStateStorage()->SetInt(...)` and `ImGuiStorage* st = GetStateStorage();` (a ring of four handle copies); `GetTopMostPopupModal()` returns a window entry (`ID` = the popup id, empty `Name`) or null; `BeginMenuBar()` returns bool; `SetWindowSize(Vec2)`, `BeginChild(unsigned id, ...)`, `ImageButton(id, tex, Vec2 ...)`, `PushClipRect(Vec2, Vec2, bool)`; `WindowRef::DC.ItemWidth` / `ItemWidthDefault` writes set the window's default value width (`SetWindowItemWidthDefault`, ended by `End`); `ImRect::ClipWith`; `IMGUI_VERSION`, `IMGUI_CHECKVERSION()`, `IM_COL32_*` shifts and white / black; style fields `ScrollbarRounding`, `IndentSpacing`, `SeparatorTextAlign`, `SeparatorTextPadding`, `DockingNodeHasCloseButton`, `ScaleAllSizes()`.
- **Probe result.** editor/main.cpp (and the .hpp files it includes) now compiles against OkayUI with the probe header with **0 errors**: 436 at the start of round 6, 284 after it, 282 at the start of round 7 (re-measured with the vendor include path), 147 after the dock and window table, 132 after channels, 0 after this step. Compiling says nothing about behaviour: nothing here runs the real editor, and the dock-node, channel and host-shape code is only exercised by the headless tests listed in 6.20 to 6.22. Round 8 then cleared the rest: every `.cpp` under editor/ and launcher/ that mentions ImGui (39 files) probes at **0 errors**, measured by `scripts/okayui_probe_all.sh` (it defines `OKAY_HAVE_OKAYUI` like the editor build, and adds the vendor, backends and soft include paths). Before round 8: launcher/main.cpp 12, UIPreviewPainter 8, SplineTools 6, DialogueWindow 5, ProblemsPanel 3, LocalizationWindow 2, ModelingEasyUi 1, NavigationWindow 1, FpHandsWindow 1 (39 errors in 9 files). Fixed by kind: `StyleInfo::DisabledAlpha`, `StateStorage::GetFloatRef`, `TableNextRow(flags, minRowHeight)`, sort-spec fields `Specs` / `SpecsCount` / `ColumnIndex` / `SortDirection`, `io.MouseClickedCount`, `DrawList::PathRect` and `_Path`, a `?:` between a draw-list handle and `nullptr`, raw geometry (`PrimReserve` / `PrimWriteVtx` / `PrimWriteIdx` / `PrimVtx` / `_VtxCurrentIdx`, `PushTexture` / `PopTexture`, `GetFontTexUvWhitePixel`), `io.AddMousePosEvent` / `AddMouseButtonEvent`, `StyleVar_GrabMinSize`, `Columns(count, id, border)`, `ColorConvertHSVtoRGB` with references, `ImLengthSqr`, `ImBezierCubicCalc`, `FLT_MIN`, `Context::OpenPopupStack` and `WindowRef::Flags` / `Rect()`, `KeyId_MouseWheelY`, `Context::PlatformImeData`.

### 6.23 Real fonts: a multi-font store (round 8)

Closes the "per-draw heading and code fonts" gap of 6.18 (test `okayui_fonts`, 76 checks).

- **`AddFontTtf(bytes, size, px, name)`** (also `AddFontTtfFile(path, px)`): registers a TrueType font from bytes (copied) at a pixel size with our own rasteriser (`okay::TtfFont`, the engine's). Returns an `OkayUI::Font*`. The same bytes registered at another size share one font (one glyph atlas). Room for 5 different font files and 32 sizes; null past that, for bytes that are no font, for a size outside 4 to 512 pixels or NaN.
- **What a user font does.** `Font::ttf` / `Font::px` are new fields. `SetFont(font)` (and so `PushFont` / `PopFont`) switches text drawing AND measuring: while it is active the text scale is set so one line is `px` pixels tall (`px / 8` times the 8-unit cell), and leaving it (another `SetFont`, `PopFont`) gives the old scale back. So `CalcTextSize`, `GetTextLineHeight`, `GetFrameHeight`, buttons, fields, sliders and trees all follow the font. A glyph the font lacks is skipped. `DrawList::AddText(font, size, ...)` draws one call with the font at `size` pixels and restores the active font after.
- **Draw data.** The glyph atlas is a fallback-font texture: `FallbackFontOf(cmd.texture)` returns the font, and a backend uploads it the way it already does for the launcher look's fallback fonts (`DrawData` is unchanged). At most 8 such atlases per frame in all (the existing limit), so five user fonts leave room for the launcher look's own fallbacks.
- **io.Fonts.** `AddFontFromMemoryTTF(bytes, size, px)`, `AddFontFromFileTTF(path, px)` and `AddFontFromMemoryCompressedBase85TTF(array, px)` now return these real fonts (and are still recorded in `FontRequests()`); `AddFontDefault` returns a copy of the default font.
- **The base-85 arrays.** `shared/RobotoFont.h` and `shared/JetBrainsMonoFont.h` hold Dear ImGui's compressed base-85 format. OkayUI has no decoder for it, on purpose: the engine dropped that decompressor (public-domain code needs the owner's approval, `docs/status/THIRD_PARTY.md`), and the same fonts ship as our own OKF1 data. So the host maps each array once: `RegisterBase85FontSource(RobotoMedium_compressed_data_base85, bytes, size)` with the bytes from `okay::EmbeddedUIFontData` (Roboto; the JetBrains Mono bytes come from the same OKF1 reader), and `AddFontFromMemoryCompressedBase85TTF(array, 18)` then gives a real font at that size. An unmapped array gives the default font. Writing our own decoder from the format and checking it against the pinned SHA-256 of the decoded bytes (`assets/fonts/embedded_fonts.sha256`) is possible and not done.
- **Not done:** font fallback chains and merge mode (`MergeMode` is recorded, not applied; the editor's `MergeLocalizationFonts` for CJK would map to the existing fallback-font mechanism), kerning, and a font atlas size choice (bake height is 48 px, like `okay::TtfFont`'s default).

### 6.24 Window mode: the plan to run the editor on OkayUI alone (round 8)

Today the editor (`editor/main.cpp`, 52,000 lines) creates the SDL window, starts Dear ImGui and its SDL and renderer backends, and draws every panel with Dear ImGui; OkayUI only draws the panels already ported, hosted inside Dear ImGui windows (`OKAY_UI_BACKEND=okayui`). **Window mode** is the next stage: OkayUI owns the window and the frame and Dear ImGui is never started. Switch (off by default, a dev switch): `OKAY_DEV=1`, `OKAY_UI_BACKEND=okayui` and **`OKAY_UI_HOST=okayui`**, all three; any other combination changes nothing (`oked::HostModeRequested`, `shared/OkayUIHostMode.hpp`, truth table in `okayui_hostmode`).

Steps, in order (numbers 1 to 10 below are the plan's; the round-10 work, called steps 6 to 9 in the round message, is described under "Round 10" after the list):
Steps, in order. Done are 1 and 2; the rest is not started:

1. **Frame loop (done).** `okayuihost::platform::FrameLoop` (`shared/OkayUIFrameLoop.hpp`, test `okayui_frameloop`): drains the backend's events into the host (4,096 per frame at most), stops on quit, begins the frame with the backend clock as OkayUI's time, runs the UI function, starts and stops the backend's text input from the keyboard focus (and gives the IME the caret rectangle), then finishes, draws through the `Painter` and presents. Tested with a fake `Backend` and a software painter: time, a click, typing, text input on and off, resize, a 10,000-event flood, hostile events and quit.
2. **Entry in main() (done).** `main()` checks the gate right after the command-line modes and before anything of the editor starts, and calls `oked::RunOkayUIHostMode` (`editor/OkayUIHostMode.cpp`): `okay::platform::Default()` backend, a 1280 x 800 window, `CreateSdl2Painter`, the launcher look, then the loop with `DrawHostModeUi`: a dock space over the display with the real Audio Mixer (the same `okayuiscreens::DrawAudioMixer` the hosted version uses) and a status panel, first-run layout built with the dock builder. Test `okayui_hostmode` runs that screen through the loop headless: gate, layout, picture, resize, closing a tab, quit. How to try it: `OKAY_DEV=1 OKAY_UI_BACKEND=okayui OKAY_UI_HOST=okayui OkayEngine.exe` (a window titled "OkaySpace Editor (OkayUI window mode)", the startup log line says window mode is on and Dear ImGui is not started). **Not run on a real window here: built and checked headless; the editor itself was not built or started in this session.**
3. **Settings and layout without ImGui (done).** `shared/OkayUIHostState.hpp` keeps what window mode remembers in one small text file (`okay_uihost.txt` in the per-user folder, or the file named by `OKAY_UI_HOST_STATE`): `okayhost 1`, `window x y w h known`, `open <name> 0|1` per panel, then `layout` and OkayUI's own `DockLayout` text to the end of the file. Everything read from it is untrusted: a wrong header or a file over 256 KB is refused; numbers outside 1 to 16,384 keep the default size; a position far away is dropped; at most 64 panel names of at most 96 bytes; the layout text goes through `DockLoadLayout`, which repairs or refuses damage (a refused layout builds the default). `Sanitize` fits the window to the display (at least 320 x 200, not bigger than the display, 120 pixels of it on screen each way, a gone monitor puts it back on the primary display). Saving is atomic (temporary file, then rename; a failed save leaves the old file) and throttled by `StateSaver` (at most every 2 seconds, only when something changed, once more when the window closes). `RunOkayUIHostMode` reads the window size and position from the platform window and the display from `DisplayAt(0)`, passes them to `WindowDesc`, and saves from the frame loop. `HostModeState` now holds a panel table (`open`, `IsOpen`, `OpenRef`) instead of two flags. Test `okayui_hoststate` (2,916 checks): round trip with odd names, CRLF, hostile text (no header, oversize, NaN, absurd numbers, 500 panels, a 5,000-byte name), 3,000 random mutations of a good file (never a crash; whatever is accepted is inside the limits), the sanitiser's cases, atomic save and its failure paths, the saver's throttle, and three simulated runs through the real frame loop (run 1 closes a panel and saves, run 2 restores the layout, the closed panel and the window, run 3 starts from a damaged file and builds the default).
4. **The game step without ImGui (done for the step itself).** What the editor's frame does to step the game is `EditorState` work (it was written ImGui-free), but it sat inside main.cpp's frame between ImGui calls. The Play-or-edit step is now `oked::StepSimulation` and the end of the frame `oked::FinishFrame` in `editor/EditorFrameTick.hpp` (header only, templated on the editor-state type; the block moved as it was). main.cpp's change is mechanical and small: the 9 lines of "if playing ... else TickGame" became one call with a hooks object (`EditorSimHooks()` next to `TickGame`: the debugger-aware tick, the Pause button, the behaviour-tree breakpoint), 14 lines in all; it compiles against the real `EditorState` (the probe instantiates the template with it). Window mode runs the same step from `editor/OkayUIHostRun.cpp` (a real `okay::editor::EditorState`, `Tick` as the game tick, `RefreshModifierStacks` after the step, the game's quit request stops Play), with the frame delta clamped (a stall or clock step is one normal frame). Test `okayui_frametick` (14 checks, a fake editor state): editing cuts the delta to 0.1 s and never calls the Play loop; Play hands the loop its tick once, follows the Pause button (a node breakpoint is released only when not paused) and passes the real delta, smoothing and refresh rate; the order of calls inside a frame; a host's own "Run N frames" hook is kept; empty hooks are safe; the quit flag stops Play only while playing, and the after-step and the services always run. **Left in main.cpp (not moved):** the rest of the per-frame work that still reads ImGui or main.cpp's globals: the Builder Mode F2 toggle (`ImGui::IsKeyPressed`), the display-settings and vsync handling (SDL window and renderer), the Inspector preview sampling (`apreview`), the modifier-stack cage flags (`g_meshEdit`), autosave and document recovery, the single-instance hand-off, and every `Draw*` call. They move with the panels in step 5. The `OkayUIHostRun.cpp` loop does not run them yet, so window mode steps the scene but does not autosave.
5. **Panels (started: Console, Scene Manager, Project window, Audio Mixer).**
   - **The real panel code, a second time.** `editor/OkayUIHostPanels.cpp` `#include`s the editor's own `EditorWidgets.cpp`, `ConsolePanel.cpp`, `SceneManagerPanel.cpp`, `ProjectPanelUI.cpp` and `ProjectMenuDraw.cpp` (unchanged except four lines in ConsolePanel.cpp: a stable window title, see below) and compiles them against OkayUI's Dear ImGui compatibility header. Every header they need is included first at global scope (so the real `EditorState`, `okaypb::Browser` and engine classes are the editor's own), and the names the sources *define* are renamed with `*_WM` macros (`DrawConsole` becomes `DrawConsole_WM`, and so on; 30 names, listed at the top of the file), including in the editor headers' declarations, so the two copies live in one program with no clash. Names the sources only *use* (`g_paused`, `SaveSettings`, `ActivateBoundConsoleSource`, `AddRecent`, ...) resolve to the editor's own. In CMake the file is its own object library with `okayui/probe` first on the include path (no Dear ImGui), linked into `okay-editor`. The same file is built by test `okayui_hostpanels`, so CI checks the technique and the link.
   - **Dock-aware `Begin`.** The panels open themselves with `ImGui::Begin(title, &open)` and close with `End()`. `OkayUI::SetDockAwareBegin(true)` (off by default, switched on by window mode, reset by `ResetState`) makes a `Begin` whose title is a window of the dock layout run as `DockBegin`, and its `End` as `DockEnd`, so the panel sits in the dock (tab, close box, drag) without being rewritten. A tab closed by the user reaches the panel's `*p_open` (a pending close is also routed), and a window the layout does not hold stays an ordinary window. Test `okayui_dockaware` (14 checks: chosen and hidden tab, closing, free windows, switch off, popups and child regions inside, unbalanced `End`, a 300-step random run).
   - **One title per window.** The dock layout names a window by its exact title; the Console's title carries the error counts ("Console  [x 3]###Console"), which would be a new window every time a count changes. `OKAYUI_STABLE_PANEL_TITLES` (defined by the wrapper) makes `ConsolePanel.cpp` use "Console###Console". `###` ids are not otherwise understood by the dock; a panel whose title changes needs the same treatment.
   - **What window mode shows.** `DrawHostModeUi` calls the panels the host wired into `HostModeState::panels` (each may be null) between `DockSpace` and the mixer: the **Project window** (written for this mode, because the editor's Project panel still lives inside main.cpp: folder list from the real `okaypb::Browser`, Up, double-click into a folder, New Folder and Rename through the real name-prompt dialog of `ProjectPanelUI.cpp`), the **Scene Manager** (`DrawSceneManager(ed)` with the real `EditorState`), the **Console** (the real panel over its own log), the Audio Mixer and the status panel. The first-run layout: Project on the left, Console along the bottom, the rest as tabs in the middle; later runs restore the saved layout (step 3). The project folder is `OKAY_UI_HOST_PROJECT`, else the current folder when it has an `Assets` folder, else the Project window and Scene Manager are not offered.
   - **Test `okayui_hostpanels`** (24 checks; the stand-in editor state and a temporary project; the real frame loop, a fake backend and a software painter): all five panels are docked, none floating, Project left and Console along the bottom; they draw (3,000+ vertices); a logged line makes the Console draw more; New Folder opens the real name prompt and typing a name plus Enter creates the folder on disk; closing the Scene Manager's tab clears its open flag, removes it from the layout and it does not come back as a floating window; the saved state lists it closed; a host with no panels wired still gets the two screens. `OKAYUI_SHOT=file.png` writes a picture of the window (looked at: the dock, the Project list, the Console rows and filters, all drawn by OkayUI).
   - **Limits, honestly.** (Round 10 changed the first two limits: see below. The Console now receives the editor's log lines, and the error, save question and Open Script calls go through `DialogService`.) The window-mode Console has its own log; `oked::wm::ConsoleLog` writes to it. `SaveSettings` is still the editor's own function. The Project window is small: no thumbnails, search, drag and drop, delete, or the full right-click menu (`okaypb::ctx`), and the Properties and import-settings dialogs are not wired to it. The classic 8x8 look is used in the headless test; the real window uses the launcher look. Not run in a real window: the editor was not built here.
   - **Limits, honestly.** The window-mode Console has its own log: code that logs through the Dear ImGui copy (`oked::ConsoleLog`) does not reach it; `oked::wm::ConsoleLog` does. The panels' calls into main.cpp (`ShowErrorPopup`, `RunAfterSavePrompt`, `OpenScriptFileInEditorAt`, `SaveSettings`) are the editor's own functions and some of them open Dear ImGui popups, which do not exist in window mode: those paths (an error after Save As, "Open Script") are not usable there yet. The Project window is small: no thumbnails, search, drag and drop, delete, or the full right-click menu (`okaypb::ctx`), and the Properties and import-settings dialogs are not wired to it. The classic 8x8 look is used in the headless test; the real window uses the launcher look. Not run in a real window: the editor was not built here.
   - Still to do in this step: Hierarchy, Inspector (`InspectorPanel.cpp`), Timeline, Animator, Navigation, Dialogue, Behavior Tree, the Script editor and the many small windows. They all probe at 0 errors against OkayUI, so each needs the same treatment (add it to the wrapper's include list and rename list, give it a hook, run it headless).
6. **Scene and Game views.** They draw the engine into a texture today and show it with `ImGui::Image`; window mode shows it with `DrawList::AddImage` / `Image` over a `Painter` texture (the soft `FrameDrawer` already maps `DrawCmd::texture` to painter textures). The picking, gizmos and overlays use the window draw list, which exists with channels (6.21).
7. **Menus, shortcuts, popups, dialogs.** The main menu bar (`DrawMainMenuBar`), `HandleShortcuts`, the context menus and modals are `BeginMenu` / `BeginPopup` calls that compile; the file dialogs (`okay::filedialog::DrawBuiltin`) and the native dialogs must be driven from the loop.
8. **Fonts.** Map the two base-85 arrays to the OKF1 bytes with `RegisterBase85FontSource` and run the editor's start-up font calls unchanged (6.23); put the localisation fonts in as fallback fonts.
9. **Floating panels and second windows.** Dear ImGui "viewports" (panels dragged out of the window, `OkayVp_*` in main.cpp) become floating windows inside the one window (the dock layout already floats windows); a second OS window is not planned.
10. **Remove the ImGui start-up for this mode.** `ImGui::CreateContext`, `ImGui_ImplSDL2_*`, `ImGui_ImplSDLRenderer2_*`, `NewFrame` / `Render` (the table in 6.18) are skipped when window mode is on; Dear ImGui is only linked, never called. When every panel is ported and the owner agrees, the default flips, and the Dear ImGui code is deleted one release later (the rule in `docs/status/own_code_roadmap.md`).

**Round 10: Scene and Game views, dialogs, Console lines, Hierarchy and Inspector (window mode).** Everything below is wired into `RunOkayUIHostMode` (`editor/OkayUIHostRun.cpp`) and shows up in window mode by itself. All of it was run headless (software painter, fake backend); the editor program itself was not built or started here (see "What ran").

- **Scene and Game views.** `editor/SceneViewCore.hpp` (no UI; written from what `DrawScene3D` and `Render3DTexture` in main.cpp do with the camera and from the engine's `RenderMeshesSS` call): `OrbitCamera` and `EditorFrame` (eye on a sphere around the target, perspective), `GameCamera` / `GameFrame` (the scene's main camera, its own projection), `FitSize` (scale 0.25 to 1, long side at most 1920, as the editor does) and `Renderer` (keeps the software rasteriser's buffers; calls `ApplySceneLight` first). `editor/OkayUIHostViews.cpp` shows the picture in two docked windows, **Scene** and **Game**: each frame the scene is rendered at the window's size, uploaded to a painter texture (`CreateTexture` / `UpdateTexture`, recreated only when the size changes) and shown with `OkayUI::Image` over a backdrop (dark grey in Scene, the camera's background colour in Game). The Scene view orbits with a drag that starts over the picture (left or right button; yaw -0.4 and pitch +0.4 degrees per pixel, pitch held to +-85) and zooms with the wheel (distance x (1 - 0.1 x wheel), 2 to 400), using the editor's own `camYaw`, `camPitch`, `camDist`, `camTarget`. **Not done:** grid, gizmos, selection outlines and picking, labels, 2D scenes (a 2D scene shows nothing), orthographic and axis views, the GPU renderers, render-scale and auto-performance settings, the Game view's resolution presets and mobile frames, and the Stats overlay: the Dear ImGui editor still has all of these. Tests: `okayui_sceneview` (21 checks: size fitting, an empty scene is transparent, a cube is drawn in the middle and not in the corner, same input gives the same pixels, orbiting changes the picture, a far camera draws fewer pixels, the game camera frame) and `okayui_hostviews` (21 checks: both tabs docked together, the Scene picture has 1,000+ lit pixels and goes out as a painter texture, the window pixels show the cube and the backdrop colour 42,45,51, a drag turns the camera, the wheel zooms, a drag starting outside does nothing, the Game tab draws through the main camera, no camera says so). `OKAYUI_SHOT=file.png` writes a picture (looked at: a lit cube in a dock).
- **Dialogs without Dear ImGui popups.** `editor/DialogService.hpp`: one interface, `ShowError`, `RunAfterSavePrompt`, `OpenScriptAt`, with a current-service slot (`SetDialogs` / `CurrentDialogs`). Two implementations. **Dear ImGui:** a 9-line block in main.cpp next to `DrawSavePrompt` (`ImGuiDialogs`) that forwards to the existing `ShowErrorPopup`, `RunAfterSavePrompt` and `OpenScriptFileInEditorAt`, plus one line in `DrawSavePrompt` that registers it. **OkayUI:** `editor/OkayUIHostDialogs.cpp`, the error modal ("Something went wrong": red title, reason, OK or Escape; also written to the Console) and the Save / Don't Save / Cancel question with the same words and rules as main.cpp: a prefab open refuses with "Go back to the scene first", one question at a time, a failed save keeps the question open and shows "Couldn't save: <why>", Save stops a running game first, Escape is Cancel. It asks the editor through a small `SaveContext` of callbacks (bound to the real `EditorState` in `OkayUIHostRun.cpp`), so it is tested without the engine. The window-mode copy of the panel sources renames `ShowErrorPopup`, `RunAfterSavePrompt` and `OpenScriptFileInEditorAt` (`*_WM`) and those forward to `CurrentDialogs()`. OkayUI popups need a layout, so `Draw` opens a bare 1x1 window to carry them. `Open Script` has no script editor in window mode yet: it is logged as a warning. Tests: `okayui_hostdialogs` (26 checks: both popups drawn and centred, OK, Escape, Cancel, Don't Save, a failing then a working Save, prefab refusal, one at a time, open script) and 5 checks in `okayui_hostpanels` (the panels' calls reach the service; with none set an error still reaches the Console).
- **The Console gets the same lines.** `ConsoleSetMirror(fn)` in `ConsolePanel.cpp/.hpp` (6 lines): a second reader of every logged line, called after the line is stored. `wm::MakeHooks` plugs `wm::ConsoleLog` into it, so every line any editor code logs through `oked::ConsoleLog` (tools, scripts, `ShowErrorPopup`) also lands in the window-mode Console with its level. The mirror is renamed away in the window-mode copy (no loop). Tests: 2 checks in `okayui_port_console` (the real `ConsoleLog` hands each line and level to the mirror, a level out of range is held to 2, none after it is unplugged) and 3 in `okayui_hostpanels` (counts of info, warning and error rise by one through a stand-in of the editor's `ConsoleLog`, stop when unplugged, and come back when `MakeHooks` runs again). The real `ConsoleLog` and the wiring were each tested, but not together in one program.
- **Hierarchy and Inspector.** `DrawHierarchy` (about 580 lines) and `DrawInspector` (about 12,000 lines of per-component drawers) are inside main.cpp and use its statics throughout, so they were **not moved** (no main.cpp change). The two window-mode windows are written in `editor/OkayUIHostObjects.cpp`. **Hierarchy:** the scene's objects as a tree (expand and collapse arrows, A-Z sort, click selects, Ctrl+click toggles, double click frames the object in the Scene view), the real search model `HierFilter` (name, `t:tag`, `c:component`, `l:layer`, `is:off`, `Parent/Child` paths) as a flat list, a `+` menu (Empty, Cube, Sprite, Camera through `EditorState::Create*`), a right-click menu (Duplicate, Delete through `EditorState`). Not done: drag and drop reparenting (`PlanDrop` / `ApplyDrop` in `InspectorModel` are ready), icons and badges, the diagnostics markers, merged-scene groups, the context menu of the Dear ImGui editor. **Inspector:** the object block (name, active, tag, layer, static ...), Transform, then a card for every component that has registered properties, drawn by the editor's own generic field code: `OkayUIHostObjects.cpp` includes `InspectorPanel.cpp` and `EditorWidgets.cpp` a second time (renamed with `*_WO`, 26 + 12 names, as the panels file does with `*_WM`) compiled against OkayUI: `PropertyField` (floats, drags, colours, enums, bools, text, asset and object fields), the multi-selection "mixed" values, the prefab "changed from" marks, undo through `WatchEditUndo`. Edits go through the property registry (`WriteField`), so undo, dirty and prefab tracking are the editor's. Not done: the hand-written drawers (curve and gradient editors, bone pickers, material previews, node editors and so on) and the component Add and Remove menus, the asset (Project selection) view, Lock Inspector. Test `okayui_hostobjects` (23 checks) runs it with the **real `EditorState`** and a real `okay::Scene` (a 3D template plus a cube and an empty object): all five tabs docked in separate places, 3+ rows listed, the Inspector names the selected object and draws 8+ property fields in 3+ cards, a click on the second row selects it and the Inspector follows, the "active" check box click flips the real object's `active` and sets `dirty` (and flips back), `+` then Empty adds an object, nothing selected says so. `OKAYUI_SHOT=file.png` writes a picture (looked at; with the 8x8 test font the narrow side panels clip their labels, the real window uses the launcher look).
- **Round 11 (window mode Scene view).** (1) The two `ImDrawCallback` errors in main.cpp were not from the probe header: they came from `OkayUI.hpp` defining `ImDrawCallback_ResetRenderState` and `ImTextureID_Invalid` (with OkayUI's own callback type) over Dear ImGui's macros in a program that includes both; both are now `#ifndef`-guarded, and `g++ -fsyntax-only editor/main.cpp` with real imgui and OkayUI headers gives no error. (2) Scene view features, in `editor/SceneViewCore.hpp` (no UI: `ToScreen`, `SegToScreen` cut at the near plane, `GroundGrid`, `PixelRay`, `Pick` by ray against every active mesh renderer's world-space triangles, `BoundsOutline`, and the gizmo maths `ClosestOnAxis`, `MoveAlong`, `ScaleFactor`, `RotateAngle`; written from standard ray and projection geometry, not moved out of main.cpp, whose gizmo code is entangled with its ImGui draw lists) and `editor/OkayUIHostViews.cpp`: a **grid** (1 unit, 5 beyond 60 units of distance; red X and blue Z axes lines; a Grid check box), **click to select** (a click that does not move picks the nearest object, Ctrl toggles, empty space clears; a drag orbits), a **selection outline** (the 12 edges of the world bounding box), and **Move / Rotate / Scale gizmos** (toolbar buttons; one handle per world axis from the object's position; dragging a handle edits the real object, one undo step via `PushUndo`, scene marked unsaved). Tests: `okayui_sceneview` 36 checks (projection, grid, pick including nearer-wins, inactive and behind, outline, gizmo maths), `okayui_hostviews` 33 checks (the same through the frame loop: grid drawn, click picks, outline and 3 handles, click on empty clears, X handle drag moves along +X only, Scale and Rotate drags, grid switch off); a picture was looked at. **Not done:** the 2D scene view; local-space gizmos, snapping, plane and centre handles, box select, picking of non-mesh objects (cameras, lights, sprites), labels; and Round 11 items (3) Project panel parity, (4) Hierarchy drag-to-reparent, the Inspector's hand-written drawers and the add/remove component menu, and (5) the menu bar with shortcuts: not started.
- **Round 12 (window mode): menu bar, Hierarchy reparenting, add and remove component.** (0) Before anything else every window-mode and port test was re-run after round 11: all 35 `okayui_*` tests of the hand-build list pass (the painter test does not run in the hand link, as before), plus `hostpanels` 29, `hostdialogs` 26, `hostviews` 33, `sceneview` 36, `hostobjects` 23 (at that moment), `hostmode` 21, `hoststate` 2,916, `frametick` 14, `dockaware` 14, `frameloop` 16, `fonts` 76, `docknodes` 65, `channels` 54, `host_shapes` 39, port `console` 8, `project` 16, `project2` 26, `scenemanager` 6. **One break found and fixed, and it is on origin/claude/okayui-next-1 too:** `test_okayui_port_versions.cpp` did not compile (a trailing `//` comment swallowed the semicolon of the per-process folder line from the hermetic change); fixed in its own commit, the test then passes (8 checks). Anyone who cherry-picked the hermetic commit needs that one-line fix. (5, part) **Menu bar:** `editor/OkayUIHostMenu.cpp` draws the real `MenuBarModel` entries (File, Edit, GameObject, View, Plugins, Help, with their shortcuts, greying and reasons) through the editor's own `MenuEntriesUI.hpp` compiled against OkayUI (the object library with `okayui/probe` first, like the other `_WM` / `_WO` files). Actions done in window mode over the real `EditorState`: New 2D / 3D (through the save question), Save (to the open path or `<project>/Assets/Scenes/<name>.okayscene`, a failure shows the error modal), Exit (`HostModeState::quit` stops the loop), Undo, Redo, Cut, Copy, Paste, Duplicate, Delete, Select All, Deselect, Create Empty / Empty Child / Empty Parent (single selection: not done), Cube, Pyramid, Sprite, Camera, Frame Selected, the View > Panels show/hide flags of the panels window mode has, Help > Shortcuts (logged). **Every other entry is shown and logs "Not available in window mode yet"** (Open, Save As, Import, Build, project items, themes, Plugins, the other Create items, overlays, rendering, layout). Shortcuts (`HandleShortcuts`): Ctrl+S, Ctrl+Z, Ctrl+Y, Ctrl+X, Ctrl+C, Ctrl+V, Ctrl+D, Ctrl+A, Delete, F; they are skipped while a text field has the keyboard and follow the entry's own greying. The dock area starts below the 24-pixel bar. Test `okayui_hostmenu` (32 checks, real `EditorState`): the six menus laid out along the top, Edit opens with 10+ entries, 20 actions on the real scene (including undo/redo and copy/paste/duplicate/delete), panel flags, a refused entry is logged, Exit, Save writes the file, Ctrl+Z/Y/D/A and Delete through real key events, a plain Z does nothing; a picture was looked at. (4, part) **Hierarchy drag-to-reparent:** drag a row onto another (top quarter = before, bottom quarter = after, middle = make it a child, below the last row = top level; the selection moves together), through the existing `PlanDrop` / `ApplyDrop`, one undo step, the world pose is kept; a drop mark and a "Move <name>" label follow the pointer. **Add Component / remove:** an Add Component button lists every registry type that has an `add` function, by category; each component card has an (x) that removes it (refused with a message when another component requires it, one undo step). Test `okayui_hostobjects` now has 36 checks (drag moves the Crate under Holder, undo puts it back, add the first listed component, remove it, undo). **Not done in this round:** the five hand-written Inspector drawers identical to the Dear ImGui ones (they are inside the 12,000-line `DrawInspector`; the generic registry card is what window mode shows), Project panel parity (thumbnails, search, delete, full context menu, dragging assets into the scene), and plan items on the settings window, layout presets, floating panels and the removal of the ImGui start-up (status below).
- **2D scene view (round 12).** `SceneViewCore.hpp` has `Renderer2D` (every active `SpriteRenderer` as a rectangle of size x scale at the object's position, turned by its Z rotation, back to front by sorting layer and order, source-over blended), `Pick2D`, `SpriteContains` and the world/pixel mapping (`Cam2D`: position and world height visible, the editor's `cameraPos` and `cameraZoom`). In window mode the Scene view switches to it when the scene is not 3D (`!EditorState::view3D`): grid every world unit, click picks the topmost sprite (Ctrl toggles, empty clears), a 4-edge outline, right-drag pans, wheel zooms; the Game view of a 2D scene looks through the main camera's position and ortho size. **Not done in 2D:** sprite textures, atlas regions and flips (flat tint only), non-sprite shapes, 2D gizmos (move/rotate/scale handles), snapping, tilemaps and UI canvases. Tests: `okayui_sceneview` 45 checks (sort order, picking, mapping, colours in the picture, rotation) and `okayui_hostviews` 39 (2D view through the frame loop: sprites counted, grid, click picks, outline, wheel zoom, right-drag pan, back to 3D); a picture was looked at.
- **Plan status in 6.24 (honest list).** Done: 1 frame loop; 2 entry in `main()`; 3 settings and layout persistence; 4 the game step; 5 panels: Console, Scene Manager, Project (small), Audio Mixer, Hierarchy, Inspector (generic cards), Scene and Game views (3D, grid, picking, gizmos), dialogs, menu bar with shortcuts; 7 menus and dialogs (partly: the actions listed above); 8 fonts (the base-85 to OKF1 mapping). Not done: 2D gizmos and textures (the 2D view itself is done, see above), Timeline / Animator / Navigation / Dialogue / Behavior Tree / Script editor windows, the settings window, layout presets, floating windows and second windows (the dock layout can float windows, nothing in window mode creates them yet), the file dialogs, and step 10 (skipping the Dear ImGui start-up in the editor's `main()` under the gate: window mode already never starts it, because `RunOkayUIHostMode` returns before main's ImGui code; the real removal is only possible when every panel is ported and the owner agrees to flip the default).
- **First-run layout.** Hierarchy and Project on the left (tabs), Inspector on the right, Console along the bottom, Scene and Game as the first tabs in the middle, then the status panel, Scene Manager and Audio Mixer. A saved layout (step 3) wins on later runs.
- **What ran, what did not.** Hand-built with g++ -O1 against the okayui objects, with the real engine linked from a prebuilt archive (`/home/user/wt-plat8/build-p8`: `libokay_engine.a`, built from a nearby revision whose engine headers differ from this branch in 20 files; the tests that need a real `Scene` pass against it, but a clean engine build from this branch was not made) and the real `EditorState.cpp`, `InspectorModel.cpp`, `PrefabEdit.cpp` compiled from this branch (the link of the Inspector test took about 3 minutes, the first compile of `EditorState.cpp` 34 seconds). **Not run:** CMake (the new test lines and the `okay_editor_wm_panels` object library with `OkayUIHostObjects.cpp` were not configured or built here; the CMake test lines are written the way the existing ones are), `okay-editor` itself, a real window, the Windows cross-build. `main.cpp` was syntax-checked (g++ -fsyntax-only) with the new block: it still shows the two earlier `ImDrawCallback` comparison errors near the end of the file that it also showed before this round's change (a quirk of that check's include order; CI builds it for real).

### 6.25 Golden tests that do not depend on the machine

A golden picture is only worth having if every machine draws the same one. What is done about it (after PR #1008's CI failed `okayui_port_versions` twice, "7,316" and then "31,181 pixels differ"):

- **The Versions page test** waits for the version lookup (a worker thread) to end before it takes the picture (`okayversionsui::IndexLoaded()` / `IndexBusy()`, new, in launcher/VersionsUI), then for the picture to stop changing; it fixes the clock (`time()` returns one value), the time zone (UTC), the locale (C), `HOME` and the working folder (a private folder per process: two copies of the test can no longer delete each other's files, which showed as a different "installed versions" list and `inst.size() == 2` failing when 16 copies ran at once), and the network (the lookup is an offline stub; no URL is ever fetched).
- **Every golden test** runs with installed fonts off (`OKAY_NO_SYSTEM_FONTS=1`, set by `OkayUITestKit.hpp`, read by the engine's `SystemFallbackFonts()`).
- **`scripts/okayui_hermetic_check.sh test_binary...`** runs golden tests under changed environments and requires the same result: normal; empty environment; other time zones and locales (Auckland / ja, Los Angeles / de); other `HOME` and working folder; the clock moved +46 days and -2.8 years (`scripts/okayui_timeshim.c`, an `LD_PRELOAD` shim); no network (`unshare -n`); a fake HTTP proxy answering every request with junk; installed fonts hidden (mount namespace). The 18 golden / port tests (calls, codeedit, color, compose, dock, drawlist, golden, panels, screens, tables, widgets, port_console, port_versions, port_project, port_project2, port_scenemanager and the two earlier) passed every variant (160 runs, none skipped) on the machine that wrote this. That machine could **not** reproduce CI's deterministic 31,181-pixel difference, so its cause is not known yet.
- **When a golden fails**, `OkayUITestKit.hpp` now prints where the pictures differ (the box and the number of differing pixels per 40 rows) and the actual picture as base-64 PNG (`grep ACTUAL_PNG_BASE64 log | cut -d' ' -f3 | base64 -d > x.png`, up to 600 KB), and with `OKAYUI_ACTUAL_DIR=folder` keeps a copy of the actual picture there. The next CI failure of any golden shows what the other machine drew.

## 7. The render interface: OkayUI on screen without a Dear ImGui backend

Own code, off by default. Without a switch the editor and launcher still draw OkayUI (and everything else) through SDL's renderer, next to Dear ImGui's SDL backend; with `OKAY_UI_OWN_RENDER=1` their OkayUI screens are drawn by the OpenGL backend below where SDL's renderer is an OpenGL one, and by the Direct3D 11 backend where it is SDL's `direct3d11` renderer, the Windows default (7.8). This section is the part that lets a later host put OkayUI's pixels on screen without any Dear ImGui backend code. (The numbered sections 5 and 6 of the draw-through-ImGui host live on another branch; this section is numbered 7 so the two cannot collide.)

### 6.26 Running the real launcher and editor on OkayUI (round 13, owner priority: "i want the launcher and editor to be running on our UI")

**What this is.** Instead of rewriting panels one by one, the SAME `launcher/main.cpp` (27,000 lines) and `editor/main.cpp` (52,000 lines) are compiled a second time against OkayUI's Dear ImGui compatibility layer: `okayui/probe/` comes FIRST on the include path, so `#include "imgui.h"`, `imgui_internal.h` and the backend headers are OkayUI's own (`okayui/probe/imgui.h` and friends; the file name says "probe" because it began as the syntax probe of 6.14). The result is a second executable from the same source, with no Dear ImGui in it. It is a dev option, OFF by default, and the normal launcher and editor are untouched.

**Switches.** CMake `-DOKAY_LAUNCHER_ON_OKAYUI=ON` builds `bin/OkaySpaceOkayUI(.exe)` (target `okayspace-launcher-okayui`); `-DOKAY_EDITOR_ON_OKAYUI=ON` builds `bin/OkayEngineOkayUI(.exe)` (target `okay-editor-okayui`, `editor/CMakeLists.txt`, the source list of `okay-editor` reused). The macro `OKAY_ON_OKAYUI` selects the adapter pieces below; `OKAY_HAVE_OKAYUI` (the hosted-in-Dear-ImGui OkayUI screens) is deliberately NOT defined in these programs: a second OkayUI frame per tick would reset the first.

**The adapter (what replaces the Dear ImGui backend calls).**
- Input: `shared/ImGuiPlatformFeed.hpp` (okay::platform events into `io.Add*Event`; with `OKAY_ON_OKAYUI` its `OwnRequested` is always true, and `ModKeyOfPair` uses OkayUI's own modifier key ids). The compat IO gained what that feed needs: `AddMouseWheelEvent`, `AddInputCharactersUTF8`, `AddFocusEvent`, `BackendPlatformName`, `MouseDrawCursor`, `WantSetMousePos`, the `Platform_*` clipboard and input-method callbacks (OkayUI's clipboard calls them when a feed installed them), `ImGuiMouseCursor` (None, Wait, Progress added), `BackendFlags_HasSetMousePos`, `Mod_Super`, the right-hand modifier keys, the modifier-flag keys, F13 to F24, punctuation and lock keys. A click whose press and release arrive in the same frame now shows its release in the next frame (before, the button stayed down). `ImGui::NewFrame()` takes the display size and the frame time the feed wrote.
- Drawing: `okayui/probe/ImGuiPainterRenderer.hpp` replaces `shared/ImGuiPainterRenderer.hpp`: `okay::imguirender::Render(painter, drawData)` draws OkayUI's finished frame through the window's `okay::platform::Painter` (the toolkit's frame drawer, `okayui/soft/OkayUI_SoftPainter.hpp`); `Init` switches the launcher look on. `OKAYUI_TRACE=1` prints the vertex counts per frame.
- Backend entry points that must still link (`ImGui_ImplSDL2_*`, `ImGui_ImplSDLRenderer2_*`): `shared/OkayUIImGuiBackendShim.cpp`, no-ops here (the feed and the painter do the work).
- `IsRectVisible`, `IsAnyMouseDown` added to the toolkit.
- `launcher/main.cpp`: 3 tiny edits (the own-backend painter factory also under `OKAY_ON_OKAYUI`; the editor lookup prefers `OkayEngineOkayUI` when this launcher is the OkayUI one; the Settings > Experimental row below). `editor/main.cpp`: none.

**Settings > Experimental row (ship).** `okay::ownswitches::Alternative()` (`shared/OwnSwitches.hpp`) describes the "launch alternative exe" action: in the normal launcher the row "Run on our own UI (restart into OkaySpaceOkayUI.exe)" starts `OkaySpaceOkayUI` and closes the launcher; in the OkayUI launcher the row is "Back to the normal UI (restart into OkaySpace.exe)". The row only shows when that program sits next to the running one. `scripts/build-windows-dist.sh` copies `OkaySpaceOkayUI.exe` (top level) and `OkayEngineOkayUI.exe` (`Tools\`) when they were built.

**What was built and run (this session).** The machine could not build the whole engine library (about 1.3 GB free memory, load average above 15), so the launcher was hand-built: `launcher/main.cpp` and the toolkit sources with g++ -O1 -g0, the launcher's own helper sources, and only the engine files it needs (fonts, images, the platform layer, the checksum code; a few engine pieces it links but never runs, such as the audio and model loaders, were stubbed in a file OUTSIDE the repository for this hand link; the real CMake target links the real library). CMake was configured with both options ON (the targets exist; configure 101 seconds) but not built. The editor: `editor/main.cpp` and every other editor source were checked with `g++ -fsyntax-only` against the compat header: 0 errors (after the additions above); the editor was NOT built or linked (it needs the whole engine), so everything under "Editor" below is `not tried`.

**How it was run.** Headless, with the scripted test hooks of `shared/TestHooks.hpp` (built with `-DOKAY_ENABLE_TEST_HOOKS`, script in `OKAY_TEST_SCRIPT`) on a private Xvfb: `OKAY_PLATFORM_BACKEND=sdl2` (the SDL window, SDL renderer geometry) and `OKAY_PLATFORM_BACKEND=own` (our own X11 window, software painter: no SDL window code runs). Pictures were looked at; none are in the repository.

**Parity list: launcher** (`works` = looked right and the clicks did what they should in the headless run; `differs` = works with a visible difference; `broken`; `not tried`)

| Page / feature | Status | Notes |
|---|---|---|
| Start-up, window, own platform backend, SDL backend | works | both ran, exit 0 |
| Fonts and look (Roboto, rounded frames, icons) | works | the launcher look; the JetBrains Mono face used for chat code is the same Roboto for now (no OKF1 mono font yet): differs |
| Sidebar, hover, page switching | works | after the one-frame-click fix |
| Create (new project, open project, recent list, search, sort) | works / dialog not tried | the New Project dialog was not reached in the run |
| Play | works | "Player runtime not found" is correct in the test folder |
| Community, Market, Downloads | works | sign-in-required and empty states |
| Versions | works | the page of 6.17 inside the real launcher |
| Friends, Servers, Forums, Achievements | works | signed-out states; signed-in content not tried (no account server in the run) |
| What's New | works | |
| Notifications popup | works | |
| Settings (Appearance and the other pages) | works | the UI-scale slider track draws short: differs |
| Settings > Experimental | not tried | row added, page not reached in the run |
| Chat window (pop-out second window, second context) | not tried | OkayUI has one context; a second window may need more work |
| Voice, game servers, downloads in progress, updater | not tried | need a network and an account |
| Drag and drop of files onto the window | not tried | |
| Text input, IME | not tried | |
| A click whose press and release arrive in the SAME frame (the test hook `click`) | broken | the widget does not react (the button is no longer stuck down, but nothing happens); a real click lasts several frames, and `down` then `up` 150 ms apart works on every page tried. The headless run was also slow (load average above 15), which makes one-frame clicks likelier; the cause is not found yet |

**Parity list: editor** (not built here: every row `not tried`; the source compiles against the layer): gate / project screen, main window and docking, menus, Hierarchy, Inspector, Scene and Game views, Console, Project, Script editor (the code widget is OkayUI's own, `ScriptEditorCodeEdit.cpp`), Visual Script graph (draw lists and channels exist), Build Settings. The first real run needs the CI job or a machine that can build the engine.

**What is NOT done yet.** The editor executable was never linked or started. The launcher's chat pop-out, the Experimental page and the New Project dialog were not exercised. Fonts: the second font face (mono) is not mapped to its own data. No CI job was added (the engine build is expensive and the job must build the whole library).

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
- **Wired in one place only** (7.8): the launcher's and editor's OkayUI frame, with `OKAY_UI_OWN_RENDER=1`, on SDL's OpenGL renderers and SDL's Direct3D 11 renderer. The player and the painter backend are not wired, and Dear ImGui is not removed. What remains for dropping it: an own platform backend for input and windows (the SDL2 row of the roadmap), porting editor and launcher panels to OkayUI, then deleting the `okay_imgui` library.

### 7.8 Wired into the host path: OkayUI's own renderer in the launcher and editor (`OKAY_UI_OWN_RENDER`)

**What it does.** The launcher and the editor end every OkayUI frame with `okay::pb::OkayUIEndFrame(renderer)` (`shared/OkayUIFrame.hpp`). Without the switch that is `OkayUI::EndFrame(SDL_Renderer*)`: OkayUI's frame becomes `SDL_RenderGeometry` calls on the same SDL renderer that Dear ImGui's SDL backend draws with. With the switch on, the frame goes through `OkayUI::render::SdlHost` (`okayui/include/okay/UI/OkayUIRenderSDL.hpp`, `okayui/render/OkayUIRender_SDL.cpp`) instead, which draws it with OkayUI's own `render::Renderer` into the same window, on top of what SDL and Dear ImGui drew, with the backend that matches SDL's renderer:

| SDL renderer | Own backend | How it gets in |
|---|---|---|
| `opengl` (context 3.3 or newer), `opengles2` (ES 3.0 or newer) | OpenGL (7.1) | `SDL_RenderFlush`, then SDL's GL context is made current (SDL activates it when a texture is created; a 1x1 texture is made and freed) |
| `direct3d11` (the Windows default for the editor and the launcher) | Direct3D 11 (7.1) | `SDL_RenderGetD3D11Device` gives SDL's device; SDL draws one fully transparent point (no pixel changes under blending) and is flushed, so its render target is bound; the bound target is checked to be the renderer's output size, and the backend draws into it |

Both backends save and restore the GL or Direct3D state they change (the Direct3D 11 one: viewports, scissor rectangles, rasteriser, blend and depth state, the pixel shader's texture and sampler slot 0, both shaders, the vertex shader's constant buffer 0, topology, index and vertex buffers, input layout), so SDL's renderer, which caches that state, carries on unchanged (checked on both: an SDL fill right after an own-renderer frame lands where it should and changes no other pixel, and SDL drawing an image right after one is drawn by the own renderer is exact). No Dear ImGui or SDL drawing code draws OkayUI's pixels on this path.

**Host images** (`OkayUI::Image`, `DrawTexture` with an `SDL_Texture*`) are drawn by the own renderer too, without reading pixels back to the CPU. A new backend call, `Backend::WrapNativeTexture`, lets a backend sample a texture the host owns without copying or changing it, and `Renderer::SetNativeImage` registers one under a key (cheap to call every frame with the same texture).
- OpenGL: `SDL_GL_BindTexture` binds the GL texture SDL keeps for the image; its name is read from `GL_TEXTURE_BINDING_2D` and wrapped. It is sampled with the filter SDL gave it (the SDL path's own sampling). Refused (the frame falls back): a YUV texture (several GL textures), a texture of another SDL renderer (SDL switched contexts), texture coordinates that are not 0 to 1 (a padded or rectangle texture), and on OpenGL ES any format but ABGR and BGR, because SDL's documentation says it may keep RGB textures with the channels swapped and swap them back in its own shader (SDL's ES renderer does that for ARGB).
- Direct3D 11: SDL2 has no call that hands out a texture's Direct3D object. So each frame SDL itself copies the image (`SDL_RenderCopy` with blending, colour and alpha modulation off, scale 1, no clip) into a render-target texture of the same size that the host keeps for that image (a "mirror", ARGB). After the flush that copy leaves the mirror bound as the render target, and its `ID3D11Texture2D` is read with `OMGetRenderTargets` and wrapped with a shader resource view of our own. One GPU copy per image per frame; a texture whose pixels change shows the new ones in the next frame (tested). Any SDL format works, YUV included (SDL converts during the copy). Up to 8192 pixels a side.
- An image not drawn for two frames is let go of (its mirror freed). `HostFrame::images` counts the images of the last frame, `SdlHost::ImageCount` what is held.

**The switch.** `OKAY_UI_OWN_RENDER=1` (also `on`, `true`, `yes`), or Settings > Experimental "OkayUI own renderer" (one row in `okay::ownswitches::Items()`, `shared/OwnSwitches.hpp`; needs OkayUI compiled in). Off by default; with it off `OkayUIEndFrame` calls `OkayUI::EndFrame(renderer)` exactly as before. It only matters where the OkayUI screens are on at all (`OKAY_UI_BACKEND=okayui`, section 5) or where OkayUI draws something anyway (a playing game's `ui_*` calls, the Test UI panel).

**When the old path still draws a frame** (`HostFrame::reason` says which; `okay::pb::OkayUILastFrame()`):
- The SDL renderer is neither OpenGL nor Direct3D 11 (`HostBackendFor`): SDL's Direct3D 9 (`direct3d`), Direct3D 12, Metal and software renderers. An OpenGL context older than 3.3 (ES 3.0), or SDL gives no Direct3D 11 device.
- A render target texture, a logical size or an offset viewport is set, or the x and y scales differ.
- A host image cannot be shared (the cases above). The whole frame then goes to `OkayUI::DrawFinishedFrame`, which keeps the draw order right.
- Direct3D 11: no render target is bound after SDL's flush, or it is not the output size.
- The backend could not be made (an entry point missing, a shader that does not build): the SDL path from then on for that renderer.

**Coordinates.** OkayUI draws in the renderer's logical pixels; the DPI scale is `SDL_RenderGetScale` and the framebuffer is `SDL_GetRendererOutputSize`, the mapping `SDL_RenderGeometry` uses (the editor sets the scale to the display's framebuffer scale). Toasts are placed in the output size, as `EndFrame(SDL_Renderer*)` places them.

**Renderer lifetime.** The own renderer keeps a backend per SDL renderer. `okay::pb::DestroyRenderer` / `Forget` (`shared/PainterBridge.hpp`) calls one hook first, while the renderer still works, and `OkayUIFrame.hpp` uses it to free that renderer's GL or Direct3D objects and its image mirrors. If a renderer is replaced without that, or SDL remakes its Direct3D device, the context or device pointer changes and the old objects are dropped without GPU calls (a small leak, no crash).

**Written from** SDL2's public header documentation (`SDL_render.h`: `SDL_RenderFlush` for mixing your own graphics calls with the renderer, renderer info, scale, logical size, viewport, clip rectangle, render target, `SDL_QueryTexture`, `SDL_GetTextureScaleMode`, `SDL_RenderCopy`, `SDL_GL_BindTexture` and its note on swapped channels; `SDL_video.h`: `SDL_GL_GetProcAddress`, `SDL_GL_GetCurrentContext`; `SDL_system.h`: `SDL_RenderGetD3D11Device`), the OpenGL and OpenGL ES specifications (`GL_VERSION`, `GL_TEXTURE_BINDING_2D`) and Microsoft's Direct3D 11 reference (`OMGetRenderTargets`, `ID3D11View::GetResource`, `ID3D11Texture2D::GetDesc`, `CreateShaderResourceView`). No code from SDL, Dear ImGui or any other project.

**Tested** by `okayui_render_sdlhost` (`okayui/tests/test_okayui_render_sdlhost.cpp`): a screen of explicit widgets and an auto-layout window, with and without a 32 x 32 image, ended through `okay::pb::OkayUIEndFrame`, read back with `SDL_RenderReadPixels` and compared with the software reference (7.1) of the same draw data (the image registered in the reference with its pixels), with the tolerance of the OpenGL backend test (mean channel difference under 0.5, under 1 percent of pixels more than 8 apart). Three ways it ran:
- **Linux** (native): SDL's `offscreen` video driver, a GL context through EGL with no display, Mesa llvmpipe 25.2 (a CPU rasteriser); renderers `opengl` (4.5 compatibility context, flavour 3.3 core) and `opengles2` (ES 3.2, flavour ES 3.0); `direct3d11` says SKIPPED. Before the change (host API stubbed): 131 checks, 12 failures; after: 135 checks, 0 failures (4 checks of the image count were added with the change).
- **Windows build run under Wine 9.0** (MinGW cross-compile, `wine64` on Xvfb, Wine's Direct3D 11 is wined3d translating to OpenGL on llvmpipe): renderers `direct3d11` and `opengl` (Wine's OpenGL on the same llvmpipe); `opengles2` SKIPPED (no ES library in Wine). Before: 132 checks, 23 failures; after: 132 checks, 0 failures. Two things were needed only for Wine and are not in the code: Wine's Windows version set to Windows 7 (`HKCU\Software\Wine` `Version` = `win7`), because Wine 9.0 does not implement `IDXGISwapChain1::SetRotation`, which SDL 2.30's Direct3D 11 renderer calls for the flip swap chain it uses on Windows 8 and newer (as Windows 7, SDL uses the older swap chain); and a local stub for the `HidP_*` / `HidD_*` functions, because this machine's MinGW headers give them C++ linkage and the engine's `OwnWin32Hid.cpp` then does not link into a test.
- **Real Windows: not run here.** The CI job "Windows export check" (a `windows-latest` runner, no GPU, Windows' software adapter) now builds and runs this test too; its result is not known when this was written.

| Frame | Renderer | Size | Largest difference | Mean | Pixels more than 8 apart |
|---|---|---|---|---|---|
| own renderer, classic look, scale 1 | all four runs | 420 x 380 | 1 | 0.129 | 0 |
| own renderer, launcher look, scale 1 | GL (Linux, Wine) | 420 x 380 | 2 | 0.007 | 0 |
| own renderer, launcher look, scale 1 | Direct3D 11 (Wine) | 420 x 380 | 209 | 0.080 | 282 (0.18%) |
| own renderer, with the image, classic look | all | 420 x 380 | 1 | 0.127 | 0 |
| own renderer, render-target image, launcher look | GL / Direct3D 11 (Wine) | 420 x 380 | 2 / 209 | 0.007 / 0.080 | 0 / 282 |
| own renderer, launcher look, scale 2 | all | 840 x 760 | 2 | 0.009 | 0 |
| switch off (old `SDL_RenderGeometry` path), launcher look | GL / Direct3D 11 (Wine) | 420 x 380 | 2 / 209 | 0.007 / 0.080 | 0 / 282 |

The Direct3D 11 outliers under Wine are the same 282 pixels on the old SDL path and on the own renderer, so they come from Wine's Direct3D-to-OpenGL translation against the software reference (launcher look only; where exactly those pixels are was not looked at; 7.5 saw the same kind of edge tie-breaking under Wine), not from the own renderer.

The test also checks: the frame really came from the own renderer (`HostPath::OwnGL` or `OwnD3D11`, spans drawn, no unresolved image), SDL drawing after an own-renderer frame and SDL drawing the same image after it, a texture whose pixels change between frames, a render target SDL drew into shown as an image, the ARGB fallback on OpenGL ES, images let go of after frames without them, the old path for a render target and for unequal scales, no GL errors, the switch values, the GL version parsing, `HostBackendFor` (Direct3D 9, Direct3D 12, Metal and software refused) and freeing the state when the renderer is destroyed.

**Not verified, said bluntly.** Nothing ran on real Windows or on any real GPU: Direct3D 11 ran only under Wine (on llvmpipe, and with the Windows 7 swap chain, not the flip swap chain SDL uses on real Windows 10 and 11), OpenGL only on llvmpipe. The editor and launcher were not built or started with the switch in this step (only the shared host code they call was run, in the test). That SDL leaves its last draw's render target bound after `SDL_RenderFlush` is how SDL 2.30 behaves (checked under Wine), not something SDL documents; if a later SDL does not, the size check sends the frame to the old path rather than drawing into the wrong target, but a target of the same size would not be caught. SDL's cached state was not read, only shown to keep working. The cost of the Direct3D 11 image copies was not measured.

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

## 12. The Hierarchy panel, drag and drop for lists, menus inside popups

The editor's most-used panel, ported the same way as sections 9 to 11: a std-only model, a Dear ImGui body drawn from it, the
OkayUI port, the editor glue behind the developer switch (`OKAY_DEV=1` and `OKAY_UI_BACKEND=okayui`; Dear ImGui stays the default),
and the two test suites. Written from what the editor's `DrawHierarchy` and its helpers draw and do, Dear ImGui's documented tree
node, menu and drag-and-drop behaviour, and measurements of both pictures; no code from any other project. This branch first merged
`claude2/perf-nav-fallback-hierarchy` (not on `main` yet), whose flat row model (`editor/HierarchyRowModel.hpp`) the port reuses.

### 12.1 What the panel does, and where each part went

| Part | Before (all in `editor/main.cpp`) | Now |
|---|---|---|
| The rows: a flat list rebuilt only when the scene, the open state or the sort change | `hierrows::Model` (the merged branch) | the same model, now shared by both paths through `HierRowsModel()`; its clip math moved to `shared/HierarchyPanelModel.hpp` (`HierVisibleRange`, `HierRevealScroll`) so both versions agree on which rows show |
| What a row shows: name cut with an ellipsis, `(3)` and `(off)`, the type icon, the prefab mark, the dim and blue text, the S L R C A P letters, the problems marker, the eye, the padlock, the band on every other row, the accent bar | `HierKindIcon`, `HierPrefabBadge`, `HierComponentBadges`, `HierEye`, `HierLock`, `DrawObjectDiagMarker` | `okaypanels::HierRow` (plain values the host works out for the rows in view only) and `HierDrawIcon`, `HierDrawPrefabMark`, `HierDrawBadges`, `HierDrawEye`, `HierDrawLock`, `HierDrawDiag`, written once against a small pen each toolkit implements, so both draw the same shapes |
| Clicks: select, Ctrl to add or remove, Shift for a range in listed order, a press on one of several selected rows decided on release, the arrow, the eye, the padlock, a double click | in the row loop | `HierarchyIntent` (`select`, `toggle`, `rangeTo`, `toggleActive`, `toggleLock`, `openId`); the range rule is `HierRange` |
| Rename | a "Rename Object" box | **in the row** in both versions of the model body (double click, F2 or the menu): Enter or leaving the field keeps it, Escape gives up, an empty name is refused. The editor's own Dear ImGui body keeps its box |
| Drag and drop: a row above, inside or below another (top 30%, middle, bottom 30%), several selected rows together, a row onto the empty space (to the top level), a file from the Project panel onto a row or the empty space | inline in the row loop | `HierDropZone`; the drop rules stay in `InspectorModel.cpp` and the editor's actions moved into `HierDropObjects`, `HierDropAssetOnRow`, `HierDropObjectsToRoot`, `HierDropAssetToRoot`, which both paths call |
| The right-click menus of a row and of the empty space, the [+] and [...] popups | `HierarchyContextModel.hpp` lists drawn by `MenuEntriesUI.hpp`, with host-drawn slots | the same lists turned into `PanelMenuEntry` data; Parent To and Rig & IK are recorded from their slot code through `menubar::SlotRecorder` and replayed with `SlotReplayer` when an item is picked; the create categories (2D Object, 3D Object, ...) open the editor's own Dear ImGui list at the pointer (see 12.7) |
| Search (`HierFilter`: words, `t:`, `c:`, `l:`, `is:`, `-word`, paths): a flat list with the parents after each name, Ctrl+click, a double click looks at it | in `DrawHierarchy` | the host builds the hit list; the panel shows `searching` rows and the footer (`HierSearchFooter`) |
| Keys: Up, Down, Left, Right, Ctrl+A, F2 while the panel has the keyboard | in `DrawHierarchy` | in both bodies, from `activeRow`; F2 starts the rename in place |
| Selecting in the Scene view scrolls the list to the row | `RevealScroll` | `revealRow` |
| Combined scenes: a header row per source scene | header rows | `HierRow::header`, `HierHeaderLabel` |
| The empty scene note | `EmptyState` | the same diamond, title and wrapped hint (`HierWrapWords`) in both |

Files: `shared/HierarchyPanelModel.hpp` (included by `EditorPanelModels.hpp`), `okayui/screens/OkayUIEditorPanels.cpp`
(`okayuiscreens::DrawHierarchy`), `editor/EditorPanelViews.cpp` (`DrawHierarchyBodyImGui`), `editor/OkayPanels.cpp`
(`DrawHierarchyOkayUI`: the window, the keyboard focus, a row dragged out of the panel handed to Dear ImGui), `editor/main.cpp`
(`DrawHierarchyWithOkayUI`: fills the rows in view, builds the menus, carries out the intent; `DrawHierarchy` calls it first when
the switch is on).

**Only the rows in view are built.** The model does not copy the scene: it gives a row count and a fill function, and a version asks
only for the rows it draws (about 20 of them in a 500-pixel panel, plus the active row for the keys), whatever the scene's size. The
editor's host fills a row (its components, prefab facts and problems) only when asked.

### 12.2 Gaps closed in OkayUI (`okayui/src/OkayUI.cpp`, `OkayUI.hpp`; tested in `okayui_hierarchy`)

- **Drag and drop for lists.** `GetDragDropPayload()` (the drag in flight, so a list can show where it would land and refuse a row
  onto itself), `BeginDragDropTarget(DragDrop_NoDefaultRect)` (a target that draws its own line instead of the outline), Escape and
  `CancelDragDrop()` end a drag without a drop, and `ResetState` now also forgets a typed drag.
- **Drags from another toolkit.** `SetExternalDragDrop(type, data, size, released)`: while a Dear ImGui panel drags something over an
  OkayUI one, the host hands it over every frame; OkayUI's targets see it even though the other toolkit owns the mouse
  (`Input::blocked`) and get it on the frame it is let go. `IsDragDropExternal()` tells the two apart. The reverse way (a row dragged
  out of the OkayUI Hierarchy onto a Dear ImGui field) is the host's: it starts an external Dear ImGui source with the same "GO_PTR"
  payload once the pointer leaves the panel.
- **Menus inside a popup.** `MenuItem` (with a shortcut, a tick and greyed-out rows) and `BeginMenu` now work in a popup body, as in
  Dear ImGui's context menus: rows one text line tall with the highlight reaching half the spacing past them, a sub-menu that opens to
  the right on hover (to the left when there is no room), a press in a sub-menu's list does not close the popup, and picking an item
  in a sub-menu closes the whole menu.
- **Menus seen in the real editor (fixed after the first run).** A sub-menu opened from a popup row now has the popup's row height
  (one text line and the item spacing, Dear ImGui's 26 pixels at the editor's size, not OkayUI's taller row) and the popup's padding and
  arrow. Every menu list (a sub-menu, a menu-bar menu) starts higher when last frame's height would run past the bottom of the window;
  one taller than the window fills it and scrolls by whole rows (three a wheel notch; the keyboard highlight is brought into view).
  Escape closes the innermost open list, then the popup (not while a text field in it has the keyboard). The editor's menu colour is
  opaque (it was 98.5%, which let the text under a menu show faintly). The editor host offers the menus a hosted panel opens as mouse
  regions (a menu hanging over the Scene view could not be hovered or clicked there) and keeps a press or the wheel over an open OkayUI
  menu from Dear ImGui (so choosing a row does not also click the Scene view). Tested in `okayui_hierarchy` (a 40-row Parent To in a
  320-pixel window) and `okayui_host`; checked in the editor under Xvfb at 1920 x 1200 and with the window at (120, 100), 1920 x 800.
- **Speed (no picture changed).** Rounded rectangle outlines take their corner points from a table worked out once (they computed 56
  sines and cosines per rectangle), and circle outlines are drawn straight from the cached unit circle as a ring (the mitre of a
  regular polygon is radial), with no sines, cosines or square roots per call. Every golden image of every OkayUI suite is unchanged.
- **A bug the tests found** (in the editor too): cutting a name to fit dropped one byte and then any continuation bytes, which left the
  first byte of a two-byte character alone before the ellipsis. `HierEllipsize` and the editor's `EllipsizeToWidth` now drop whole
  characters.

### 12.3 Tests

- `okayui_hierarchy` (`okayui/tests/test_okayui_hierarchy.cpp`, 115 checks, headless): the shared rules (the tail, header and title
  text, the footer, the drop zones, the drag label, the ellipsis with UTF-8, the range, the clip at 3,000 and 20,000 rows and a huge
  scroll, reveal, the options menu); every interaction through synthetic input on the drawn rectangles: click, Ctrl+click on and off,
  Shift+click range, a click on one of several selected rows (decided on release), the arrow folding and unfolding, the eye, the
  padlock; rename in place by double click, F2 and the menu with Enter, Escape, a click elsewhere and an empty name; the row menu
  (selects the row, a greyed-out item takes no click, the Order sub-menu, Rename), the empty-space menu with Sort A-Z, the [+] menu with
  its 3D Object sub-menu, the options; drag and drop before, inside and after a row, to the top level, Escape during a drag, a row onto
  itself, several selected rows together, a file dragged in from another toolkit onto a row and onto the empty space; the keys with and
  without the keyboard; the search (the flat list, Ctrl+click, a double click, no matches); 3,000 and 20,000 objects (about 19 rows
  asked for a frame, the wheel, reveal, a click on a far row); the empty scene and its menu; header rows; the new OkayUI pieces on their
  own (`GetDragDropPayload`, the outline left out, Escape, `CancelDragDrop`, an outside drag delivered and an outside drag the host
  stops reporting; `MenuItem` and `BeginMenu` in a popup). Six golden images: `hierarchy_tree`, `_menu`, `_search`, `_rename`,
  `_drag` (the drop outline while a row is held over another), `_empty`.
- `okayui_hierarchy_imgui` (`tests/test_okayui_hierarchy_imgui.cpp`, 20 checks): both versions on a 560 x 560 canvas with the editor's
  look; a click at the middle of each of the 15 rows selects the same object in both (the rows line up), Ctrl+click, the arrow, the eye
  and a drag of Music onto Houses do the same in both; the speed and pixel tables below; `OKAYUI_SHOT_DIR` writes the pictures and
  `OKAY_UI_PROFILE=hier<N>:<imgui|okayui>` (N = 15, 3000 or 20000) runs one side for callgrind. The comparison rig both comparison
  tests use is now `tests/okayui_compare_rig.hpp`.
- `hierarchy_context_model`: `SlotRecorder` and `SlotReplayer` (ids by place, the same label twice, greyed-out items, a closed
  sub-menu, a replay that runs exactly the picked item and nothing for another slot's id).
- `ctest -R "okayui|ui_backend|hierarchy"`: 37 of 37 pass (all OkayUI suites with their golden images, `ui_backend`,
  `scene_hierarchy_search`, `deep_hierarchy`, `hierarchy_row_model`). `editor/main.cpp` passes the syntax-only check with and without
  `OKAY_HAVE_OKAYUI`; `OkayPanels.cpp` (both ways), `ProblemsPanel.cpp` and `EditorPanelViews.cpp` were compiled.

### 12.4 Speed

Per frame: the panel built (Dear ImGui `NewFrame` to `Render`, OkayUI `BeginFrame` to `EndFrameData`) together with the test host's
model and row fills, which are the same code on both sides. Instructions: callgrind, 200 frames after a warm-up. Wall time: the median
of 150 frames, three runs on a shared machine (load 8 to 10), the range shown. The big scenes are scrolled half way down.

| Scene | Dear ImGui instructions | OkayUI instructions | Dear ImGui build | OkayUI build | Rows asked for a frame (both) |
|---|---:|---:|---:|---:|---:|
| Village (15 rows, menus' data built only when open) | 334,500 | 255,100 (24% fewer) | 128 to 135 us | 76 to 82 us | 16 |
| 3,000 objects | 347,100 | 224,200 (35% fewer) | 126 to 130 us | 70 to 76 us | 21 |
| 20,000 objects | 354,000 | 228,000 (36% fewer) | 130 to 132 us | 68 to 75 us | 21 |

Before the speed work of 12.2 the village took 341,000 instructions with OkayUI, 2% more than Dear ImGui: callgrind showed half of
OkayUI's time in rounded outlines and the eyes' circles (sines and cosines per call). The cost does not grow with the scene: 20,000
objects cost what 3,000 do. "Build + render" (with the CPU rasteriser) is also faster with OkayUI in every run (about 11 to 17 ms
against 36 to 48 ms); as in section 9, that figure is about the software rasteriser, not the toolkits.

### 12.5 Pixels

Both versions at 560 x 560, the same rasteriser. "Clearly different": a channel more than 48 apart (the gate is 12%).

| Picture | Average difference | Clearly different |
|---|---:|---:|
| The village (15 rows: icons, prefab marks, badges, an object off, a locked one, a problem) | 3.47 | 2.76% |
| Its right-click menu, with the Order sub-menu row | 4.50 | 3.56% |
| A search ("House": four matches with their parents) | 1.61 | 1.47% |
| A rename in place | 4.16 | 3.09% |
| 3,000 objects, scrolled half way | 4.21 | 3.20% |
| 20,000 objects, scrolled half way | 4.83 | 3.59% |
| An empty scene | 0.70 | 0.56% |

What brought it there: Dear ImGui's tree node geometry (the arrow at 0.7 of a text line 12 pixels in, the icon after the arrow's
42-pixel zone, the 18-pixel item on a 26-pixel row, the highlight from the indent to the edge), the menu's columns and 16 by 12 padding
and its arrow, and one pixel down for OkayUI's row text (its glyphs sit a pixel higher in the line). Pictures (not committed; re-made
with `OKAYUI_SHOT_DIR`): `<scratchpad>/shots/ohy/final/<case>-imgui.png`, `-okayui.png` and `side-<case>.png`.

### 12.6 What still differs (blunt)

- Text edges (OkayUI's baked Roboto against Dear ImGui's), and a long name is cut a few letters differently (the widths differ).
- The strip's title and the menus' text sit a pixel higher than Dear ImGui's.
- The drag preview is OkayUI's chip with the name, not Dear ImGui's tooltip box; tooltips and the hover cursor are OkayUI's.
- **Rename is in the row** in the OkayUI version (and in the model body); the editor's own Dear ImGui Hierarchy still opens its box.
- **The create categories** (2D Object, 3D Object, Player, Light, Camera, Audio, Effects, UI, in the [+] menu, the empty-space menu
  and a row's Create Child) are one row each in the OkayUI menus that opens the editor's own Dear ImGui list of that category at the
  pointer: those lists are code with a field in one of them (the volume size), not data yet. Parent To and Rig & IK are recorded and
  drawn by OkayUI.

### 12.7 Not verified

- **No real editor window.** The editor was not built (its `main.cpp` is too big for this machine); `DrawHierarchyWithOkayUI`,
  `DrawHierarchyOkayUI`, the menus converted from the editor's lists, the slot replay in the editor, the hand-over of a file dragged from
  the Project panel and of a row dragged out of the Hierarchy onto an Inspector field have never run. Windows was not built; no GPU was
  used. The refactor of the Dear ImGui `DrawHierarchy` (its drop handlers and the Save-as-Prefab box moved into functions both paths
  call, Parent To and Rig & IK written against `SlotUI`) was only syntax-checked: it is the default path, so it needs a look in the real
  editor before a release.
- The model body (`DrawHierarchyBodyImGui`) is what the comparison runs; the editor's default Hierarchy is still its own code (switching
  it to the model body is the next step, once it can be run).

### 12.8 Coverage (usage report)

| What | Before (committed report) | After |
|---|---:|---:|
| `ImGui::` calls available by name | 27,138 of 27,376 (99.1%) | 27,335 of 27,578 (99.1%) |
| Types, flags and enums available | 11,752 of 12,083 (97.3%) | 11,888 of 12,233 (97.2%) |
| Call sites in ported functions | 530 (1.9%) | 713 (2.6%) |

The totals grew because this branch merged the row-model branch and added the model body (183 calls) next to the editor's own
`DrawHierarchy` (182 calls), which stays the default: the 183 are counted as ported, but no call left the editor yet.

## 13. The ten ported panels in the real editor: same row heights, same first window sizes

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

### 13.1 Pixels in the real editor

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

### 13.2 What still differs

- Text edges (OkayUI's baked Roboto against Dear ImGui's) are most of what is left in every panel.
- Text that runs past a hosted section's right edge: Dear ImGui lets it run into the window's padding, OkayUI cuts it at the section's
  edge (the long notes under the import settings).
- Stats: the narrow scroll bar is still OkayUI's; the plot's line differs slightly.

### 13.3 Not verified

Windows (not built or run here; the measurements are from Linux under Xvfb with the software SDL renderer), other UI scales and the
Compact density in the real editor (the headless test covers the Inspector's Comfortable spacing only), a docked History or Profiler.

## 14. The Inspector, stage 1: the header card, the component bars, Transform, the Mesh Renderer's shading, Add Component

The editor's most-used panel after the Hierarchy, ported the way sections 9 to 12 port a panel: a std-only model, a Dear ImGui body
drawn from it (for the comparison test; the editor's own Dear ImGui Inspector stays the default), the OkayUI port, the editor glue
behind the developer switch (`OKAY_DEV=1` and `OKAY_UI_BACKEND=okayui`), and the two test suites. The Inspector is large (its
component code runs to about 11,000 lines of `editor/main.cpp`), so this is stage 1. Written from what the editor's `DrawInspector`,
`CompHeader` and `DragVec3Axis` draw and do, Dear ImGui's documented collapsing header, drag field, colour field and combo behaviour as
the editor uses them, and measurements of both pictures; no code from any other project.

### 14.1 What is ported, and how the rest still draws

| Part | Where it was | Now |
|---|---|---|
| The object header card: the active box (it takes the children and the whole selection), the type icon, the name, Static, Lock (an undo step), the Tag list (the built-in tags, the project's tags, the add field at the bottom; right-click a project tag to rename it or take it off the list), the Layer list (right-click a layer to rename it, or Reset to default), Save as Prefab, a file dropped on the card | `DrawInspector` | `okaypanels::InspectorHeaderModel`, `okayuiscreens::DrawInspectorHeader`, `DrawInspectorHeaderWithOkayUI` (the editor's rules for tags and layers: `Tags::Add`, `TryRenameLayer`, the tag rename across the scene with an undo step) |
| Every component's header bar: the fold arrow, the enable box, the family mark, the name (cut with an ellipsis), the problems marker, the "..." menu (also a right-click on the bar): Move Up and Down, Expand and Collapse All, Copy, Paste and Reset Values, the prefab's Revert and Apply Component, Remove Component; the Character Model note and the "Different on the selected objects" note under an open bar; a drag from the bar onto the visual script | `CompHeader` | `CompHeaderModel` (the menu is data, asked for only while it is open), `DrawComponentHeader`, `CompHeaderOkayUI`. Every component goes through `CompHeader`, the ones drawn from the property registry too, so every bar is OkayUI's |
| Transform: the header (right-click: Copy and Paste Transform Values), Position, Rotation and Scale as three drag cells each with the axis colours (fewer decimals in a narrow Inspector), "(editing N selected objects)", the camera's locked scale, Reset Pos, Reset Rot, Reset Scale, Uniform | `DrawInspector`, `DragVec3Axis` | `TransformModel`, `DrawTransform`, `DrawTransformWithOkayUI` (the euler angles kept per object, edits to every selected object, one undo step when a drag begins, the resets with their own step) |
| Mesh Renderer, its shading: the stats line, Color, Wireframe, Shader (Cel Bands, the gradient colours, the Fresnel note), Rim (Rim Power, Rim Color), Outline (width, colour), Cast and Receive Shadows, Ground Shadow (its Y and strength), Emissive, Specular (Shininess), Primitive | `DrawInspector` | `MeshRendererModel`, `DrawMeshRenderer`, `DrawMeshRendererWithOkayUI` |
| The Add Component button | `DrawInspector` | `DrawAddComponentButton`; the browser it opens is still the Dear ImGui popup |

**How the unported parts draw: each ported part is a section inside the Inspector's own Dear ImGui window.** The two ways the brief
offered were (a) the unported components keep drawing with Dear ImGui inside the OkayUI frame, or (b) the whole Inspector falls back to
Dear ImGui while an unported component is present. (b) would almost never show OkayUI: nearly every object carries a component that is
not ported yet (a collider, a rigidbody, a script, a light). (a) is what `HostSection` (section 10.3) already does for Project Settings:
the Dear ImGui window stays (the dock tab, the scroll bar, the window's own scrolling), each ported part is one placeholder item as tall
as the OkayUI part drew on the last frame, and OkayUI draws into the part of it the window shows. So the Inspector is, from top to
bottom: the play-mode note and Lock Inspector (Dear ImGui), the header card (OkayUI), the prefab banner and the UI layout card (Dear
ImGui), the Transform (OkayUI), then for each component its bar (OkayUI) and its body (Dear ImGui, but the Mesh Renderer's shading
fields are OkayUI), the divider (Dear ImGui) and Add Component (OkayUI). Every part keeps its Dear ImGui code: a part falls back to it
for any frame OkayUI cannot draw it (a Dear ImGui popup or window covers it, as for every hosted panel), and it is the default.

`HostSection` gained a `bleed`: Dear ImGui's framed headers reach half the window padding (8 pixels) past the content on both sides, so
the bars' sections are offered and clipped that much wider. The Transform's open state is the Dear ImGui header's own flag (switching
toolkits keeps it), the copy of a transform and Uniform are shared by both versions.

**Undo.** The editor records one undo step per edited field by watching Dear ImGui's active item (`WatchEditUndo`). A drag on an
OkayUI field keeps the placeholder pressed, so that works as it is; a field being typed in has the mouse up, so the watcher now also
counts "a hosted OkayUI field has the keyboard" (`OkayUIHasKeyboard`) as an active widget. The Transform still records its own step
when a drag begins, Lock, the resets, the paste and the tag rename theirs, exactly as in the Dear ImGui version.

### 14.2 Gaps closed in OkayUI (`okayui/src/OkayUI.cpp`; tested in `okayui_inspector`)

- **A popup opened from a row of an open popup** (the right-click menu of a tag in the Tag list): a press in it no longer closes the list
  (its box is added to the list's "inside" area, as a sub-menu's already was), and the list's rows under it no longer take that press
  (a widget in a popup body ignores the pointer where a deeper popup covered it on the last frame).
- **A combo's list as wide as its content**: a pushed `StyleVar_PopupWidth` wider than the box widens the list (Dear ImGui fits a combo's
  list to its content; the Tag list holds an add field and a button).
- **Popup slots**: there were 8, never freed, and every combo box takes one when it is first drawn. With the hosted panels and the
  Inspector's four combos the editor would have run out (a combo that silently never opens). Now 32, and a closed slot that has not been
  asked for longest is reused for a new id.
- **`BeginCombo` showed the `##id` part of its label** ("Shader##mesh"); seen on the first comparison picture. Fixed.
- **Drag fields like Dear ImGui's**: a double click types a number (as Ctrl+click did), and the value follows the pointer only once it
  has moved 3 pixels from the press (Dear ImGui's drag threshold: a click no longer nudges a value).
- **A text field that is not drawn gives the keyboard up** (Dear ImGui drops an active item that is not submitted): the field of a
  rename menu that closed kept the keyboard, so Escape went to it, keys went nowhere and `WantsTextInput()` stayed true.
- **Framed text fields have no border** (`StyleVar_FramedWidgets`, the look the editor's panels push): Dear ImGui draws the editor's frames
  without one (FrameBorderSize 0). Seen in the real editor's Inspector (the name field had a light outline). Every hosted panel's text
  fields change: 13 older golden images (`editorpanels_problems*`, `_tagslayers*`, `_player_rendering`, `hierarchy_*`) were re-made and
  looked at (only the outline went), and the comparisons moved the right way (Tags and Layers 4.23 to 4.19 average difference, Player
  1.16 to 1.14, the Hierarchy rename 3.09% to 2.80% clearly different).
- **A check box without a label takes no label gap** (it took 8 pixels): the header card's icon and name sat 8 pixels to the right of Dear
  ImGui's, so a narrower Inspector put Static and Lock on their own row sooner than Dear ImGui did (seen in the real editor after folding
  a component: Dear ImGui's row changed, OkayUI's did not).
- **Long lists show eight rows at a time** (the Layer list's 32, the Primitive list's 23), as Dear ImGui's combo does; the wheel scrolls
  them by rows, the list opens with the current row in view, and a thin bar shows where it is. (Written in the Inspector view, not yet a
  toolkit piece.)
- **The bar's name is cut by whole characters**: the editor's `CompHeader` dropped one byte and then any continuation bytes, which could
  leave the first byte of a two-byte character before the ellipsis (the bug section 12.2 fixed in the Hierarchy). The rule is now
  `CompHeaderTitle` in the model, used by both versions.

Two older tests changed only their input: `okayui_editor_panels` waits a third of a second between two presses on the same drag field
(they were a double click, which now types), and `okayui_screens` sends its Backspace presses one frame each (it nested a frame inside a
frame, so the search field was not drawn in the outer one and gave up the keyboard). The older golden images that changed are the 13 of
the field border above.

### 14.3 Tests

- `okayui_inspector` (`okayui/tests/test_okayui_inspector.cpp`, 119 checks, headless; the host is `okayui/tests/InspectorFixtures.hpp`
  and the parts are stacked as the editor stacks them by `InspectorStackOkayUI.hpp`): the shared rules (the bar's title cut with UTF-8,
  the shorter formats of a narrow row, the cell widths, uniform scale, the stats line, the menu head and tail, the colour bytes, the button
  width, the value column); the whole stack (the parts follow each other, the bars reach 8 pixels past the region, the cells share the
  value column); the card (the active box, typing a name, Static, Lock with its undo step, the tag list's eight rows, picking Player, a new
  tag added with Enter and the list closing, a refused tag keeping the list open with the reason, renaming a project tag from its
  right-click menu while the list stays open, a refused rename, Remove tag from list, the layer list's eight rows and the wheel, picking
  layer 4, renaming layer 5, Save as Prefab); a narrow Inspector (the name takes its row, the button the whole width); the bars (the
  arrow folds, the name part folds, the enable box does not fold, the menu's 8 rows, a greyed-out row takes no click, Move Down and its
  undo, the right-click menu, Remove Component and its undo, Collapse All, a required component's menu); the Transform (a 20-pixel drag
  on Position X with one undo step, Rotation Y, a double click and Ctrl+click typing a number, Reset Pos with its undo step, Reset Rot,
  Reset Scale, Uniform, Copy and Paste Transform Values, folding); the camera (scale locked, no Reset Scale, no Uniform); two objects
  selected (the note, a drag moving both, the mixed-values note, a change going to both); the Mesh Renderer (Wireframe, Receive Shadows,
  the shader list and Toon's Cel Bands, the Rim slider and its two rows, a colour cell drag of 12 with the threshold, a click that does
  not nudge it, Ground Shadow's rows, the Primitive list, Add Component); the toolkit pieces of 14.2 on their own. Four golden images:
  `inspector_full`, `_tag_list`, `_comp_menu`, `_multi`.
- `okayui_inspector_imgui` (`tests/test_okayui_inspector_imgui.cpp`, 17 checks): both versions of the stack from the same objects, the
  Dear ImGui one as one window with the parts in order (as the editor draws them); the same input does the same in both (drags on
  Position X, Rotation Z and Scale Y in whole pixels, with one undo step each; Reset Pos and Reset Scale; Cast Shadows and Ground Shadow;
  a bar's enable box and arrow; folding the Mesh Renderer, after which Add Component still lines up; Static); the speed and pixel tables
  below; `OKAYUI_SHOT_DIR` writes the pictures and `OKAY_UI_PROFILE=insp:<imgui|okayui>` runs one side for callgrind.
- `ctest -R "okayui|ui_backend|hierarchy|inspector"`: 41 of 41 pass (34 in a Release tree: every OkayUI suite with its golden images and the
  comparisons; 7 that need the engine, `okayui`, `ui_backend`, `inspector_model`, `scene_hierarchy_search`, `deep_hierarchy`,
  `hierarchy_context_model`, `hierarchy_row_model`, in the editor's `-O1` tree). `editor/main.cpp` passes the syntax-only check with
  and without `OKAY_HAVE_OKAYUI`; `OkayPanels.cpp` was checked both ways; the editor was built and run (14.6).

### 14.4 Speed

The sample object (a header card, the Transform, a Mesh Renderer open and three closed components, Add Component), per frame, the
parts and the test host's model filling (the same code on both sides). Instructions: callgrind, 200 frames after a warm-up. Wall time:
the median of 150 frames, three runs (load 2.6 to 6), the range shown.

| Case | Dear ImGui instructions | OkayUI instructions | Dear ImGui build | OkayUI build | Dear ImGui build + render | OkayUI build + render |
|---|---:|---:|---:|---:|---:|---:|
| The sample object | 463,300 | 324,600 (30% fewer) | 149 to 159 us | 103 to 108 us | 62.9 to 63.8 ms | 20.1 to 20.5 ms |
| Two objects selected | | | 155 to 166 us | 103 to 112 us | 63.1 to 63.4 ms | 20.0 to 20.5 ms |
| The camera (no Mesh Renderer) | | | 111 to 122 us | 48 to 57 us | 52.8 to 54.4 ms | 11.0 ms |

As before, "build + render" uses the CPU rasteriser and says more about it than about the toolkits.

### 14.5 Pixels

Both versions on a 560-pixel-wide canvas, 1480 tall, the same rasteriser; "clearly different": a channel more than 48 apart (the gate is 12%).

| Picture | Average difference | Clearly different |
|---|---:|---:|
| The sample object | 2.23 | 1.92% |
| Two objects selected | 2.51 | 2.22% |
| The camera | 1.07 | 0.91% |

What the first comparison found and what was changed for it: the "##mesh" shown after "Shader" (14.2), the colour markers (Dear ImGui's
is the left 3 pixels of the rounded cell, not a square bar), the arrow of a component that is off (grey with its name), the bars' 8
pixels past the region. The divider above Add Component stays Dear ImGui's in the editor, so the OkayUI picture leaves it out.

### 14.6 In the real editor

The editor was built (Linux, `-O1`, assertions on; `main.cpp` peaked at 5.1 GB) and run under Xvfb at 1920 x 1200 with the physics
playground, once with Dear ImGui and once with `OKAY_DEV=1 OKAY_UI_BACKEND=okayui`, driven by the same clicks (xdotool), "7 Hook"
selected. Done and looked at in both, side by side:

- typing 2.5 into Position X (a double click, select all, type, Enter), a 40-pixel drag on Position Y (7.5 to 9.5 in both), Ctrl+Z (back
  to 7.5 in both), Reset Pos (zeros in both), Ctrl+Z and Ctrl+Z again (back to 2.5, 7.5, 15 and then to the first -7, 7.5, 15 in both);
- a 40-pixel drag on Scale X (0.3 to 2.3 in both) and Ctrl+Z (back in both); Uniform ticked and the same drag (all three to 2.3 in both);
- the Mesh Renderer's enable box (off: the box empty, the name and the arrow grey, the left bar grey, in both), Ctrl+Z (on again in both);
  its "..." menu, its arrow (folded and unfolded in both), the Shader list, the Layer list, the Tag list;
- Ctrl+click of a second object: "(editing 2 selected objects)" and "Different on the selected objects: Color" in both.

The Inspector's rectangle (430 x 1130) differs clearly in 1.50% to 3.07% of its pixels between the two runs (after the fixes below), the
differences only text edges (pictures `<scratchpad>/shots/oin/s2/`, `s3/`: `im-*.png`, `ok-*.png`, `side-*.png`, `diff-*.png`).

Found in the real editor and fixed in this step: the light outline of OkayUI's text fields, the 8-pixel gap after an unlabeled check box
(so Static and Lock wrapped at a different width than Dear ImGui's), lists taller than Dear ImGui's (14.2). Found and fixed in the Dear
ImGui path: dragging a Position, Rotation or Scale number in an assertion build aborted the editor (`BeginDragDropSource` after the
row's label, which has no id; it now passes `ImGuiDragDropFlags_SourceAllowNullID`, so a drag from the label onto the visual script
works as meant; release builds without assertions only skipped it before). The OkayUI Transform got the same label drags.

The drag abort was confirmed on the unchanged starting branch
(`claude2/okayui-hierarchy-2`, built the same way): the same assertion on the first drag of Position X.

Twice a Ctrl+Z did not take in the **Dear ImGui** editor of this branch (once after Reset Pos, once after the Uniform drag; OkayUI's took
both times). Reset Pos and Ctrl+Z three times in a row on the unchanged branch's editor and on this branch's Dear ImGui editor side by side
undid every time in both, so it is not something this branch changed as far as that shows; the cause is not known (possibly the key
arriving in a frame Dear ImGui does not see it under Xvfb at about 20 FPS). Not looked into further.

### 14.7 What still differs (blunt)

- Text edges (OkayUI's baked Roboto against Dear ImGui's), and the Add Component label sits 2 pixels higher.
- **Where a list opens.** In the docked Inspector Dear ImGui opens the Shader, Layer and "..." lists to the left of the box (over the
  Scene view); OkayUI opens them under the box, kept on the screen. The "..." menu is right-aligned to the screen edge in OkayUI.
  (Stage 2, section 15.2: this is Dear ImGui's 1600-pixel monitor rectangle under Xvfb; OkayUI's combo lists now follow Dear ImGui's
  placement order.)
- The text widths differ by under a pixel ("Static" + "Lock"), so at a width right on the edge one version may wrap Static and Lock
  and the other not.
- Tooltips and the hover cursor are OkayUI's; the colour picker that opens from a swatch is OkayUI's picker, not Dear ImGui's.
- **Per-field prefab marks and the registry's field tooltips** (`FieldTip`: the blue bar and the Revert / Apply Property menu on a field
  changed from its prefab) are not drawn on the OkayUI Transform and Mesh Renderer fields. The component bar does show "changed from the
  prefab" and its Revert / Apply Component rows. (Done in stage 2, section 15.2.)
- The Mesh Renderer below Primitive (the mesh file, the texture and map fields with their pickers, drop targets and thumbnails, the mesh
  tools, the material preset) and every other component's body stay Dear ImGui; stage 2 is the asset fields (shared by many components).
  (Stage 2, section 15, ports these and six more bodies, and the generic card.)

### 14.8 Not verified

Windows (not built or run; Linux under Xvfb with the software SDL renderer), other UI scales and the Compact density in the real editor,
a docked Inspector narrower than 430 pixels, prefab instances in the OkayUI Inspector (the menu rows are written from the Dear ImGui
code and not clicked), the drag from a bar or a label onto the visual script (written, not tried), a file dropped on the OkayUI card (the
same Dear ImGui drop target on its placeholder, not tried), the Tag and Layer renames in the real editor (tested headless only).

### 14.9 Coverage (usage report)

| What | Before (committed report) | After |
|---|---:|---:|
| `ImGui::` calls available by name | 27,335 of 27,578 (99.1%) | 27,585 of 27,842 (99.1%) |
| Types, flags and enums available | 11,888 of 12,233 (97.2%) | 11,957 of 12,316 (97.1%) |
| Call sites in ported functions | 713 (2.6%) | 935 (3.4%) |

As with the Hierarchy, the totals grew because the model bodies (222 calls) sit next to the editor's own `DrawInspector`, which stays the
default.

## 15. The Inspector, stage 2: component bodies as field rows (the rest of the Mesh Renderer, Light, Camera, Rigidbody 3D, the 3D colliders, Script, and every component drawn from the property registry)

Stage 1 ported the parts every object has (section 14). Stage 2 ports the bodies of the most used components, behind the same
developer switch (`OKAY_DEV=1` and `OKAY_UI_BACKEND=okayui`); Dear ImGui stays the default and the fallback for any frame OkayUI cannot
draw a part (a Dear ImGui popup or window covers it). Written from the editor's own component code (`editor/main.cpp` DrawInspector,
`editor/InspectorPanel.cpp` PropertyField, AssetField, AssetPathPicker, PrefabFieldMark), Dear ImGui's documented widget behaviour as the
editor uses it, and measurements of both pictures; no code from any other project.

### 15.1 One model for every body: field rows

A component body is written with a small set of Dear ImGui widgets. `shared/InspectorFieldsModel.hpp` describes a body as a list of rows
of those kinds (`InspFieldList`, `InspRow`): drag fields (1 to 4 cells; the Transform's coloured cells; the generic card's "X 1.000"
cells), sliders (also logarithmic), int drags and sliders, check boxes, combos, colour fields, text fields (with a hint; with the
editor's path picker arrow), the editor's section heading, the generic card's group title, grey notes, coloured text, labels, small
buttons, buttons (the accent toggle), colour swatches, file and object fields (a button with the name, a clear button, a picker), a
thumbnail, a layer mask (a list with Everything, Nothing and a box per layer), a fold-out (Dear ImGui's TreeNode) and spacing. A row can
sit on the previous row's line (`SameLine`, a gap, an x from the region's left, or `SameLineOrWrap`: only when it fits), be greyed out,
have a fixed or "region less N" width, show "-" / "Mixed" for values that differ on the selected objects, carry a tooltip or ask the
registry for one (`tipLookup`), and be marked as changed from the prefab.

The host builds the rows every frame with pointers to the values (`InspFieldList::Drag`, `Check`, `Combo` ... one call per Dear ImGui
widget), the version edits them in place and marks the row (`changed`, `clicked`, `activated`, `axes`), and the host then does what the
Dear ImGui code does after that widget. What a version needs only now and then is asked through `InspFieldHooks`: a field's registry
tooltip (only while it is hovered), an overridden field's prefab menu (only while it is open), and what a picker lists (project files of
a kind, the scene's objects, the 32 layer names, the procedural textures with their thumbnails; asked once when the picker opens).

Both versions draw the same rows: `DrawFieldListBodyImGui` (`editor/EditorPanelViews.cpp`, for the comparison test) and
`okayuiscreens::DrawFieldList` (`okayui/screens/OkayUIEditorPanels.cpp`). The editor glue is `oked::DrawFieldListOkayUI`
(`editor/OkayPanels.cpp`): one `HostSection` per body inside the Inspector's Dear ImGui window (as in stage 1), and a Dear ImGui drop
target over each row that takes a file from the Project panel or an object from the Hierarchy (the drag is Dear ImGui's, the rows are
OkayUI's; the outline shows while a file is dragged, red when the row does not take it).

### 15.2 What is ported

| Component | How | What the body has |
|---|---|---|
| Mesh Renderer (the rest, below Primitive) | `DrawMeshRendererRestWithOkayUI` | Mesh File and Load; the texture path (the picker arrow: Choose from project / Clear; a drop from the Project panel), Browse... (the file dialog), Procedural (a grid of the procedural textures with their thumbnails), Clear, the thumbnail (right-click: Show in Project), Tiling, Offset, Filter, UV Scroll, Triplanar; Normal Map (Clear, thumbnail, Bump Strength), Reflectivity, Metallic, Specular Map, Occlusion Map (and Occlusion), Metal / Rough, Emissive Map; the counts, Subdivide, Subdiv+Smooth, Smooth, Weld, Recenter, Ground, Fit 1u, Remove; Material preset (path, Save, Load); Add Physics |
| Mesh Renderer (the shading, stage 1) | `DrawMeshRendererWithOkayUI` | now with the fields' registry tooltips and prefab marks |
| Transform (stage 1) | `DrawTransformWithOkayUI` | now with Position, Rotation and Scale's tooltips and prefab marks |
| Light | `DrawLightWithOkayUI` | Type, Use Temperature (Kelvin, its swatch, the five presets in their colours) or Color, Intensity; Ambient and Ambient Tint; Range and Falloff (point, spot, area); Spot Angle and Softness; Cast Shadows, the renderer note, the sun-shadow warning, Strength, Bias; Flicker (Amount, Speed, the three presets, the note); the aiming note; Remove |
| Camera | `DrawCameraWithOkayUI` | Projection (its width), Size, Physical Camera (Focal Length, Sensor Height, the sensor presets, Effective FOV) or Field of View (the presets, FOV Axis); Clear Flags, Background and its five swatches; Near, Far; Zoom (logarithmic) and 1x, Dutch Angle, Lens Shift, Pixel Perfect and Pixels Per Unit; Camera Shake (Amount, Angle, Frequency, Recovery, Test Shake greyed out outside Play); Viewport Rect; the Culling Mask list; Main and Depth on one line; Render Target; Add overlay camera; Remove |
| Rigidbody 3D | `DrawRigidbody3DWithOkayUI` | Body Type, Gravity Scale, Mass, Drag, Bounciness, Friction, Max Fall Speed, Freeze position X Y Z on one line, Freeze Rotation, Angular Drag, Allow Sleep, Continuous Collision, Rolling Resistance, Use Child Colliders, the parent note, Remove |
| Box, Sphere, Capsule and Mesh Collider 3D | `Draw*Collider3DWithOkayUI` (shared rows: `AddColliderRows`) | Size / Radius / Height / Axis, Offset (the coloured cells); Trigger and Layer on one line; the Physics Material fold-out (Own Friction, Friction, Friction Mix, Own Bounciness, Bounciness, Bounce Mix); Only Tag and Only Layers for a trigger; Fit to mesh and Auto Fit; the Box's Edit Collider toggle and its note; the Mesh Collider's notes, status, fallback Size and Offset, Rebuild; Remove |
| Script | `DrawScriptWithOkayUI` | Open in Script Editor; the public variables from the script (live from the open editor buffer): headers, the name on the left (coloured when it differs from the script's default) with its tooltip, the value from the name column on (a check box, three cells for a vector, a slider for a [Range], a number, a text, an object slot that takes an object dragged from the Hierarchy), and "o" to revert to the default. The script's path is the bar's title, as in Dear ImGui (the body has no path field there either) |
| Every component drawn from the property registry (the generic card) | `DrawFallbackOkayUI` (`editor/InspectorPanel.cpp`) | one row per shown property: Bool, Int, Float, String (multi-line too), Vec2/3/4 and Quat (the "X 1.000" cells, each part "-" when it differs on the selected objects), Color, Enum, AssetPath (file field, picker, clear, drop), ObjectRef (object field, picker of the scene's objects, a drop from the Hierarchy); read-only rows greyed out; group titles; "Editing N selected objects"; edits go to every selected object as in Dear ImGui (`WriteField`, `WriteFieldAxes`). In this build that is every registered component without a hand-written body: Cylinder Collider 3D, Debris Emitter, Network Object, Network Transform, Sync Var, Touch Button, Touch DPad, Virtual Joystick, and any component a plugin registers |

The other hand-written bodies (about 95 components: Particle System, Audio Source, Sprite Renderer, Terrain, Joint 3D, the 2D physics,
the character controllers, UI ...) are still Dear ImGui; each is now a list of `InspFieldList` calls next to its Dear ImGui code (the
seven above show the pattern), not new toolkit work.

**Prefab marks and tooltips (stage 1 leftovers).** Every row with a label asks `FieldTooltip` for the registry's text while it is hovered,
as `FieldTip` does. A row whose property differs from the prefab (`PrefabFieldOverridden`, only looked up for a changed component) gets
the editor's blue bar 5 pixels left of the region, and a right-click opens its menu: "Overridden from X", the differences, Revert
Property, Apply Property to Prefab (`RunPrefabFieldMenu` queues them like `PrefabFieldMark`). The same is on the Transform's three rows
and the Mesh Renderer's shading fields that call `FieldTip` in Dear ImGui. The body windows reach 6 pixels past the region so the bar is
not clipped.

**Where lists open (stage 1 leftover).** The runs under Xvfb show why Dear ImGui's lists and the "..." menu opened to the left of the
Inspector: Dear ImGui keeps a popup inside its monitor rectangle, and there that rectangle is 1600 pixels wide (the Culling Mask list and,
in stage 1, the "..." menu both end at x = 1597 = 1600 - 3, wherever they open, on a 1920-pixel screen). On a real screen Dear ImGui puts a
combo's list under its box toward the right when it fits, else above the box, else under it toward the left (its right edge on the box's),
else above toward the left. OkayUI's combo lists now try the same four places (`beginPopupBody`, a combo's slot keeps its box), and only
when none fits are they pushed inside the window as before. So in the docked Inspector both should open a list under its box on Windows;
under Xvfb they still differ for the reason above. Not checked on Windows.

### 15.3 Gaps closed (OkayUI and its host; tested in `okayui_inspector` and `okayui_host`)

- **A logarithmic slider** (`SliderFlags_Logarithmic`, the Camera's Zoom): the grab on log(v / min) / log(max / min), Ctrl+click still types.
- **A format with no number in it is shown as it is** ("-" for a value that differs on the selected objects, as Dear ImGui shows it); it
  used to fall back to "%.3f".
- **A combo's list placement** as above.
- **A fast double click at a low frame rate** (two clicks between two frames, or a second click in the frame after the first was shown):
  the host's input collector now counts presses and shows each one as an up-then-down edge (`okayuihost::InputCollector`). Before, two
  clicks 80 ms apart at about 18 frames a second merged into one long press, so a double click on a number did not start typing in 4 of
  5 tries in the real editor; after the fix 4 of 4 did.
- In the field rows (`DrawFieldList`): Dear ImGui's DragInt (a framed cell, the step kept while dragging), its SeparatorText (a 3-pixel
  rule each side of the title), the section heading (the 22-pixel face, its own line height), AlignTextToFramePadding for the script
  variables' names (OkayUI's is a no-op), its TreeNode arrow (70% of a line, the frame padding in), the colour swatch, a one-line text
  field that lets the keyboard go on Enter as Dear ImGui's does (Ctrl+Z after typing a script variable went to the field), a fold-out
  that stays open across an undo that rebuilds the component.

### 15.4 Found and fixed in the Dear ImGui editor while porting

- **Every path field opened every path field's menu.** `AssetPathPicker` used one id for all fields, so a click on the Mesh Renderer's
  texture arrow showed "Choose from project... / Clear" six times (once per path field), and the picker six search boxes. Each field now
  has its own id. Seen in the real editor (`<scratchpad>/shots/oi2/side-51-pathmenu.png`, `side-52-picker.png`).
- **The path picker's arrow sat on the label's last letters** for a labelled path field (the item rectangle includes the label); it now
  sits at the field's right end, in both versions (seen in the comparison pictures).
- **Text that ran out of the docked Inspector** (430 pixels): the Mesh Renderer's Procedural and Clear buttons now wrap to the next line
  when they do not fit (`SameLineOrWrap`), and the Mesh Collider's notes and the Rigidbody's parent note wrap. Both versions.

### 15.5 Tests

- `okayui_inspector` (177 checks, 10 golden images; 58 new checks and 6 new pictures: `inspector_light`, `_camera`, `_boxcollider`,
  `_script`, `_meshrest`, `_generic`), with the bodies from `okayui/tests/InspectorBodiesFixtures.hpp` (stand-ins for the values and
  the rows the editor's builders make): a drag on Intensity (20 pixels at 0.02), its registry tooltip asked only while hovered, Cast
  Shadows hiding its two rows, the Type list (Point hides the spot rows), the presets on one line 4 pixels apart and their clicks; the
  Zoom slider's logarithmic middle (1.41), 1x, a swatch, a greyed-out button that takes no click, Main and Depth on one line, the layer
  mask list and a box in it; the coloured cells, Trigger and Layer on one line, the fold-out (folded: its rows not drawn and the rows
  under it moved up), typing a tag; the script's name column and "region less 28" width, its "o"; the path picker (the arrow, the menu,
  the files asked for once, a pick), the procedural grid, typing a mesh path; the generic card's file field (picker, pick, clear), the
  object field's picker, a drag on one part of a vector whose other part differs; a prefab-overridden field's bar and its menu (asked
  only while open) reverting it; where a combo's list opens in the four cases. The 4 stage-1 pictures were re-made: one column of pixels
  at the Mesh Renderer's left edge (the frames' antialiased edge, no longer clipped).
- `okayui_inspector_imgui` (29 checks): the seven bodies in both versions with the same input (a check box and a preset button in the
  light, the fold-out in the collider, the freeze boxes in the rigidbody), the speed and pixel tables below;
  `OKAY_UI_PROFILE=body:<light|camera|rigidbody|boxcollider|script|meshrest|generic>:<imgui|okayui>` runs one side for callgrind.
- `okayui_host`: two clicks between two frames give two edges; a second click in the next frame too; a held press stays down.
- `ctest -R "okayui|ui_backend|hierarchy|inspector"`: see 15.9.

### 15.6 Speed

Per frame, the body alone (the rows built from the values on both sides, the same code). Instructions: callgrind, 200 frames after a
warm-up. Wall time: the median of 150 frames, three runs (load 2.9 to 3.0), the range shown.

| Body | Dear ImGui instructions | OkayUI instructions | Dear ImGui build | OkayUI build | Dear ImGui build + render | OkayUI build + render |
|---|---:|---:|---:|---:|---:|---:|
| Light | 263,700 | 197,100 (25% fewer) | 118 to 123 us | 53 to 60 us | 52.4 to 53.2 ms | 10.6 to 10.8 ms |
| Camera | 344,100 | 236,800 (31% fewer) | 143 to 176 us | 69 to 87 us | 54.5 to 61.1 ms | 12.8 to 14.0 ms |
| Rigidbody 3D | | | 101 to 106 us | 37 to 43 us | 51.3 to 52.1 ms | 7.4 to 7.7 ms |
| Box Collider 3D | | | 102 to 125 us | 39 to 51 us | 48.0 to 53.5 ms | 5.8 to 6.3 ms |
| Script | | | 96 to 114 us | 32 to 40 us | 46.7 to 52.8 ms | 4.5 to 5.2 ms |
| Mesh Renderer (the rest) | | | 115 to 140 us | 62 to 73 us | 53.6 to 60.6 ms | 12.0 to 12.7 ms |
| Generic card (an Audio Source's kinds of row) | 146,200 | 100,400 (31% fewer) | 113 to 141 us | 42 to 52 us | 48.5 to 58.7 ms | 6.2 to 7.5 ms |
| Stage 1's sample object (for comparison) | 463,400 | 325,100 (30% fewer) | 166 to 175 us | 113 to 117 us | | |

Stage 1's sample is unchanged by stage 2 (463,300 and 324,600 instructions before). "build + render" uses the CPU rasteriser.

### 15.7 Pixels

Both versions on a 560-pixel-wide canvas, the same rasteriser; "clearly different": a channel more than 48 apart (the gate is 12%).

| Picture | Average difference | Clearly different |
|---|---:|---:|
| Light | 1.83 | 1.58% |
| Camera | 2.06 | 1.69% |
| Rigidbody 3D | 1.90 | 1.44% |
| Box Collider 3D | 0.97 | 0.77% |
| Script | 0.45 | 0.37% |
| Mesh Renderer (the rest) | 2.08 | 1.64% |
| Generic card | 0.68 | 0.56% |
| Stage 1: sample, two selected, camera | 2.22, 2.50, 1.07 | 1.90%, 2.21%, 0.91% |

What the first comparison found and was changed: the section heading 5 pixels short (OkayUI's text line stayed the body face's height),
the generic card's group title (OkayUI's SeparatorText is taller and has no left rule), the rule under a heading 6 pixels too wide, the
fold-out arrow too big and too far left, the swatches too round, Dear ImGui's DragInt drawn as OkayUI's unframed one, "-" shown as a
number.

### 15.8 In the real editor

Built (Linux, `-O1`, assertions on; `main.cpp` peaked at 5.5 GB) and run under Xvfb at 1920 x 1200, two editors side by side (Dear ImGui,
and `OKAY_DEV=1 OKAY_UI_BACKEND=okayui`), on the physics playground with three objects added (a capsule collider, a mesh collider, a
script with variables; `<scratchpad>/oi2-logs/drive/extra.okayscene`), driven by the same clicks (xdotool). Three builds were run, each
after fixing what the one before showed. Pictures in `<scratchpad>/shots/oi2/`: `old/` (first build), `run2/`, `run3/` (the final
code), and `ok-8*-crop.png` (the final relink); `side-*` are the two Inspectors next to each other, `steps-*` a sequence. Done and
looked at:

- **Sun (Light)** (`old/side-01-sun.png`): a 40-pixel drag on Intensity (1.1 to 1.9 in both), Ctrl+Z (1.1 in both); Use Temperature
  (Kelvin, its swatch, the five coloured presets in both); Bulb (2700 K, an orange swatch), Ctrl+Z (6500 K), Ctrl+Z (Color again) —
  `old/steps-light1.png`, `old/steps-light2.png`.
- **Main Camera**: the whole body scrolled through (`old/side-11-camera-down.png`, `run2/side-10-camera-end.png`); the Culling Mask list
  (`old/side-13-layers.png`; where Dear ImGui's opens: 15.2); a double click on Near / Far, a typed number, Enter, Ctrl+Z, four times
  (`run2/steps-far-typing.png`: before the input fix OkayUI started typing on 1 of 5 double clicks, after it on 4 of 4; the Ctrl+Z that
  "did not take" there in both versions was xdotool's chord arriving within one frame: with the key held 0.3 s it undid 4 of 4 in both,
  `run2/steps-far-typing2.png`).
- **2 Ball (Sphere Collider 3D, Rigidbody 3D)**: a drag on Mass (1.0 to 1.9 in both), Freeze X, Ctrl+Z twice (back in both,
  `run2/steps-rb.png`); the Physics Material fold-out, Own Friction (0.5), a drag on Friction (0.68 in both), Ctrl+Z
  (`run2/steps-physmat.png`; the fold-out closed after the undo in OkayUI, fixed: `run3/steps-physmat-undo.png`).
- **1 Tower crate 1 (Box Collider 3D)**: Edit Collider on (the accent, the note) and off, in both (`run2/steps-editcol.png`).
- **T Capsule, T MeshCol, T Script, 9 Debris emitter**: their bodies (`old/side-31-capsule.png`, `old/side-32-meshcol.png`,
  `run2/side-40-script.png`, `old/side-34-debris.png`: the Debris Emitter is the generic card, row for row the same as Dear ImGui's).
- **T Script**: "o" reverts speed to 5 in both; a click on jump's slider (0.835 in Dear ImGui, 0.832 in OkayUI); typing Knight into
  title, Enter, Ctrl+Z (`run2/steps-script.png`: OkayUI's field kept the keyboard and the Ctrl+Z went to it; fixed:
  `run3/steps-script-text.png`, Hero again in both).
- **7 Hook (the Mesh Renderer's texture)**: the arrow's menu and the picker (`run2/side-51-pathmenu.png`, `run2/side-52-picker.png`: Dear
  ImGui's showed six copies, 15.4; after the fix one each: `run3/side-51-pathmenu.png`, `run3/side-52-picker.png`), stone.png picked in
  both (the path, the 40-pixel thumbnail, Tiling, Offset, Filter, UV Scroll and Triplanar appear), Ctrl+Z (gone again in both)
  — `run3/steps-tex.png`.
- **A prefab instance** (T Capsule, Save as Prefab): a drag on Position Y gives "position.y: 1 -> 1.9" and the blue bar left of the
  Position row in both (`run3/side-76-posy.png`); a right-click on Position opens the same menu in both (`run3/side-77-propmenu.png`);
  Revert Property puts 1.0 back and the bar goes, in both (`run3/side-78-reverted.png`). OkayUI also showed the row's tooltip over its own
  open menu: fixed (`ok-85-crop.png`). The Capsule's Radius gets no bar in either version: its record's fields have no names in the
  prefab's differences ("arg0: 0.5 -> 1.4", `run3/side-74-top.png`), so no label matches.

What the pictures showed and was fixed in this step (each seen in a screenshot first): the script variables' names 8 pixels above their
values and the values 16 pixels right of Dear ImGui's (SameLine's offset counts from the window's left); the Procedural and Clear
buttons cut by the window edge (both versions); the Mesh Collider's and the Rigidbody's notes cut (both); the six copies of the path menu
and picker (Dear ImGui); the path tooltip shown over its own open menu, and a field's tooltip over its prefab menu (OkayUI); the larger
thumbnail on hover drawn blank and off the screen (OkayUI; now the path only, 15.10); a double click that did not start typing (15.3);
Ctrl+Z after typing a text variable going to the field (15.3); the Physics Material fold-out closing after an undo (15.3).

### 15.9 Tests run at the end

`ctest -R "okayui|ui_backend|hierarchy|inspector"`: 41 of 41 pass (34 in a Release tree, every OkayUI suite with its golden images and
the comparisons; 7 that need the engine — `okayui`, `ui_backend`, `inspector_model`, `scene_hierarchy_search`, `deep_hierarchy`,
`hierarchy_context_model`, `hierarchy_row_model` — in the editor's `-O1` tree). `editor/main.cpp` compiled in the editor build with
`OKAY_HAVE_OKAYUI` and passes the syntax-only check without it; `OkayPanels.cpp` and `InspectorPanel.cpp` were checked both ways.
`python3 scripts/gen_feature_map.py --check` is up to date. The final comparison run gave the speed and pixel numbers above again
(within the ranges; the Light's instructions 263,700 against 197,100).

### 15.10 What still differs (blunt)

- Text edges, as in every port; a slider's grab lands a fraction of a pixel apart for the same click (0.835 against 0.832).
- **The thumbnail's larger picture on hover** is not shown in OkayUI (only the path and the hint): OkayUI cannot draw a texture above the
  windows yet (textured quads go to the main batch, under the overlay).
- **The Culling Mask list** has no "Right-click a layer to rename it" note and no rename (Dear ImGui's has both); Everything and Nothing stay
  at its top while the boxes scroll (Dear ImGui scrolls the whole list).
- **Where lists open under Xvfb** (15.2).
- The procedural grid's buttons without a thumbnail are OkayUI buttons with the name; Dear ImGui's are the same idea.
- The path field's text runs under its arrow in both versions (as before).
- A body's rows are built from the values at the start of the frame: a row that appears because of an edit (Kelvin after Use Temperature)
  shows one frame later than in Dear ImGui (one frame, not visible at 60 frames a second).
- About 95 hand-written component bodies are still Dear ImGui (15.2).
- The usage report counts only functions: the bodies above sit inside `DrawInspector`, so the "call sites in ported functions" figure
  (3.4% to 3.8%: the field-row body and the heading) does not count them.

### 15.11 Not verified

Windows (not built or run), other UI scales and the Compact density in the real editor, a prefab instance's field marks and Revert /
Apply Property in the real editor (tested headless only), dropping a file from the Project panel or an object from the Hierarchy onto an
OkayUI row (written, not tried), the Browse... file dialog, the Procedural grid with real thumbnails (headless without thumbnails only),
the Camera's physical mode, Pixel Perfect and Add overlay camera, the Mesh Collider's fallback rows, a script's object slot, multi-select
editing through the generic card in the real editor.

### 15.12 Coverage (usage report)

| What | Before (stage 1) | After |
|---|---:|---:|
| `ImGui::` calls available by name | 27,585 of 27,842 (99.1%) | 27,733 of 28,003 (99.0%) |
| Types, flags and enums available | 11,957 of 12,316 (97.1%) | 12,022 of 12,379 (97.1%) |
| Call sites in ported functions | 935 (3.4%) | 1,066 (3.8%) |

The totals grew because the field-row Dear ImGui body (`DrawFieldListBodyImGui`) sits next to the editor's own bodies, which stay the
default; the seven component bodies ported here are inside `DrawInspector` and are not counted (15.10).

## 16. The Inspector, stage 3: the 2D physics, Sprite Renderer, Audio Source, Particle System, the character controllers, the animators

Thirteen more component bodies as field rows (section 15), behind the same developer switch (`OKAY_DEV=1` and `OKAY_UI_BACKEND=okayui`);
Dear ImGui stays the default and the fallback. Written from the editor's own Dear ImGui bodies (`editor/main.cpp` DrawInspector,
PhysicsMaterialFields, TriggerFilterFields, StanceControlFields, KeyBindCombo, LayerRenameMenu), Dear ImGui's documented widget behaviour
as the editor uses it, and measurements of both pictures; no code from any other project.

### 16.1 What is ported

| Component | Builder (`editor/main.cpp`) | Notes |
|---|---|---|
| Sprite Renderer | `DrawSpriteRendererWithOkayUI` | Color, Size, the 210-wide path field (picker arrow, drop from the Project panel), "...", Texture, the thumbnail, Sorting Layer, Sort Order, Flip X / Y, Filter, the note (now wrapped in both), Remove |
| Rigidbody 2D | `DrawRigidbody2DWithOkayUI` | every row, as the Dear ImGui body (it edits the object's first Rigidbody 2D, as before) |
| Box, Circle, Capsule Collider 2D | `Draw*Collider2DWithOkayUI` | their own rows, then the 3D colliders' shared rows (`AddColliderRows`: Trigger and Layer, the Physics Material fold-out, the trigger filter), Fit to Object (with a Sprite Renderer) and Auto Fit |
| Edge, Polygon Collider 2D | `DrawPointCollider2DWithOkayUI` | the note, Offset, the shared rows, One-Way and Block Normal (Edge), a rule, "Points (N)" / "Vertices (N)", each point's two cells and "x", + Point / + Vertex |
| Audio Source | `DrawAudioSourceWithOkayUI` | Volume to Low Pass (logarithmic), Loop / Play On Awake / Stream, Beep Noise Play Stop, the path field, "...", Load, the clip note (wrapped), 3D (spatial) and its rows with the rule, the occlusion and distance-muffle rows |
| Particle System | `DrawParticleSystemWithOkayUI` | every section (Presets: the list shows "Choose a preset..."; Emission with the live count; Shape; Start; Appearance with the Sprite path; Over Lifetime; Forces; Motion; Collision; Bursts & Duration). **Not** while a colour gradient, a size curve or a speed curve is on: their editors are not field rows yet, so the body is Dear ImGui's then |
| Character Controller 2D | `DrawCharacterController2DWithOkayUI` | Mode, Movement (Sprint Key as a one-letter field), Jump with the three feel presets |
| Character Controller 3D | `DrawCharacterController3DWithOkayUI` | the "Crouch / prone" fold-out (`AddStanceRows`: the two 96-wide key lists, toggles, speeds, heights, the note), Speed, Can Jump, Jump Force |
| Sprite Animator | `DrawSpriteAnimatorWithOkayUI` | FPS, Loop, Playing, the atlas rows, the frame list (path and "X"), Add Frame, the clips (Name, X, Row, FPS, Loop, Frames), Add Clip |
| Animator | `DrawAnimatorWithOkayUI` | Clip, Loop, Playing, Speed, the Time slider with Play / Pause and "\|<", Keyframes: Record Key, one bullet per track with Clear |

New row kinds (`shared/InspectorFieldsModel.hpp`): `InspRowSeparator` (Dear ImGui's Separator), `InspRowBullet` (BulletText), a combo's own
closed text (`InspRow::preview`), and two hooks for the layer mask's rename (`layerName`, `renameLayer`).

**Counts.** `DrawInspector` has 187 component header calls (hand-written bodies, the Script and the Action List among them) and about 36
more sit in other editor files (Navigation, splines, UI inspectors, level and volume tools): about 223 hand-written bodies. **Ported to
field rows: 22** (stage 1 and 2: Transform aside, Mesh Renderer, Light, Camera, Rigidbody 3D, Box / Sphere / Capsule / Mesh Collider 3D,
Script; stage 3: the 13 above, the Particle System only without curves). **Generic card: 8** in this build (every registered component
without a hand-written body: Cylinder Collider 3D, Debris Emitter, Network Object, Network Transform, Sync Var, Touch Button, Touch DPad,
Virtual Joystick) plus any a plugin registers. **Still Dear ImGui: about 201**, among them Text and the UI components (they use the
editor's own helpers UIStyleBar, LocalizedTextField, AnchorCombo and AssetSlot, not yet rows), Terrain (brush tools), Nav Mesh Agent (its
area mask has preset buttons inside the list, a kind of row that does not exist yet), Audio Listener (no body of its own was found), the
2D Animator state machine and Model Animator.

**The generic card instead of hand-written bodies.** Looked at, not done: a hand-written body shows its own labels, ranges, tooltips,
buttons and notes; the registry card shows the property names. For none of the bodies above would the card look the same, so they were
ported by hand.

### 16.2 Stage-2 gaps closed

- **The Capsule's Radius has a prefab mark.** The prefab differences named the fields of records without a schema "arg0", "arg1"... The
  records of the 3D colliders (box, mesh, sphere, capsule, cylinder) and of the Capsule, Edge and Polygon Collider 2D now have their
  field names (`engine/src/Scene/PrefabTools.cpp` Schema), so "radius: 0.5 -> 2.5" matches the Radius row. The Dear ImGui collider bodies
  now call FieldTip on Size, Offset, Radius, Height and Axis (they had no tooltip and no mark at all). A variant file that stored an
  override by the old name ("arg0") would no longer find it (none is known; not checked on real projects).
- **The prefab menu's first line is blue** ("Overridden from X", PrefabBlue), as Dear ImGui's: `PanelMenuEntry::color`.
- **Culling Mask: right-click a layer to rename it**, with the note, the name field (Enter), Reset to default and the reason when a name
  is refused, as LayerRenameMenu. The list is as wide as the note (it ran out of the list over the Inspector in the first picture).
- **Drag and drop onto OkayUI rows in the real editor**: a picture dragged from the Project panel onto the Audio Source's path field (the
  Inspector locked, since a press in the Project panel selects the file): the blue outline while over it, the path set on release, Load
  appears, Ctrl+Z empties it, the same in both (`<scratchpad>/shots/oi3/steps-drop-file.png`). An object dragged from the Hierarchy onto
  an object field was **not** tried (the test scene has no object field).
- **Multi-object editing through the generic card in the real editor**: two Debris Emitters (Count 12 and 20): "Different on the selected
  objects: Count", "Editing 2 selected objects", Count shows "-"; a drag on Interval sets 3.5 on both (`steps-multi.png`).

### 16.3 Found and fixed while porting (seen in the pictures)

- **OkayUI: a closed fold-out's item was the whole width**, so a row put on its line (Auto Fit after a closed Physics Material, in every
  collider without a Sprite or Mesh Renderer) was drawn past the right edge and could not be seen (`side-11-edge.png`). The item is now the
  arrow and the label, as Dear ImGui's TreeNode.
- **OkayUI: a combo under an open popup took the click.** Revert Property in a prefab menu lying over the Capsule's Axis list opened the
  list instead, and the menu stayed (`ok-58.png`). `BeginCombo` now takes a press only when nothing lies over its box (`hit()`), and still
  closes its own list.
- **OkayUI: text with a line break** in a note showed a "?" for the break; it is now drawn line under line as Dear ImGui draws it.
- **OkayUI: a see-through colour's swatch** was flat; it is now Dear ImGui's light and dark check under the colour.
- **Both versions: text that ran out of the docked Inspector**: the Sprite Renderer's and the Audio Source's notes, the crouch note (now
  wrapped), and the Animator's Play and "|<" buttons after the Time slider (its width now leaves room for them, `AnimTimeRoom`).
- **Dear ImGui: the Particle System's Radius tooltip** sat outside its `if` and showed on the Shape list.

### 16.4 Tests

- `okayui_inspector` (218 checks; 7 new golden images: `inspector_sprite`, `_audio`, `_box2d`, `_edge2d`, `_particles`,
  `_controller3d`, `_animator`): the path field's width and its "..." and label on one line; the rule's place and size, a check box hiding
  two rows, the logarithmic Low Pass; the closed fold-out; a drag on one point cell; the preset list's text and a pick; the 96-wide key
  list and a pick; the bullets with Clear beside them and the Time slider leaving room for its buttons; the Culling Mask rename (a refused
  name keeps the menu open, Enter renames); the prefab menu's blue first line (pixels); a prefab menu lying over a combo (the click is the
  menu's: this check fails without the `BeginCombo` fix).
- `okayui_inspector_imgui` (36 checks): the 14 bodies drawn by both versions; tables below.
- `ctest -R "okayui|ui_backend|hierarchy|inspector"`: see 16.7.

### 16.5 Speed and pixels

Instructions (callgrind, 200 frames, per frame): Audio Source 228,966 against 166,471 (27% fewer), Particle System 268,000 against
202,900 (24% fewer), Sprite Renderer 141,800 against 101,200 (29% fewer). Wall time (median of 150 frames):

| Body | Dear ImGui build | OkayUI build | Dear ImGui build + render | OkayUI build + render | Clearly different pixels |
|---|---:|---:|---:|---:|---:|
| Sprite Renderer | 106.6 us | 41.6 us | 58.2 ms | 5.4 ms | 0.72% |
| Audio Source (3D) | 116.1 us | 65.0 us | 62.1 ms | 11.3 ms | 1.56% |
| Box Collider 2D | 84.6 us | 26.7 us | 52.7 ms | 2.8 ms | 0.36% |
| Edge Collider 2D | 101.8 us | 42.4 us | 50.1 ms | 6.1 ms | 0.96% |
| Particle System | 125.0 us | 68.5 us | 54.1 ms | 10.5 ms | 1.62% |
| Character Controller 3D | 105.1 us | 41.6 us | 51.1 ms | 5.5 ms | 1.36% |
| Animator | 100.2 us | 35.3 us | 46.4 ms | 4.8 ms | 0.81% |

The stage 1 and 2 pictures are unchanged (1.90%, 2.21%, 0.91% and 0.37% to 1.69%). The gate is 12%; every picture is under 2.3%.

### 16.6 In the real editor

Built (Linux, the editor tree) and run under Xvfb at 1920 x 1200, both toolkits side by side, on the physics playground with the stage 2
objects and seven more (`<scratchpad>/oi3-logs/drive/extra3.okayscene`: a sprite with a Rigidbody 2D, a Box Collider 2D and a Sprite
Animator; a Circle and an Edge Collider 2D; an Audio Source; a Particle System; a Character Controller 3D with an Animator; a Character
Controller 2D; a second Debris Emitter). Pictures in `<scratchpad>/shots/oi3/` (`side-*`: the two Inspectors next to each other,
`steps-*`: a sequence). Done and looked at:

- **U Sprite** (`side-01-sprite.png`, `side-02-sprite-down.png`): Sprite Renderer, Rigidbody 2D, Box Collider 2D row for row the same.
  A 40-pixel drag on Mass (1 to 3), Trigger on (Only Tag appears), Ctrl+Z twice: back in both (`steps-rb2d.png`). A double click on
  Gravity Scale, 2.5, Enter, Ctrl+Z (`steps-typing2.png`). With Ctrl+A typed by xdotool just before the digits OkayUI lost the first
  digit (0.5 instead of 2.5, `steps-typing.png`); not looked into further.
- **U Edge** (`side-11-edge.png`): the missing Auto Fit (16.3); a drag on a point, + Point, Ctrl+Z twice, the same in both
  (`steps-edge.png`).
- **U Audio**: Quieter behind walls (two sliders appear), a drag on Low Pass (470 Hz in both), Ctrl+Z twice (`steps-audio.png`); the drop
  from the Project panel (16.2).
- **U Particles**: the preset list, Fire (every value changes the same way), Ctrl+Z (`side-31-fire-down.png`, `side-32-fire-undo.png`).
  Dear ImGui's list opens left of the Inspector under Xvfb (section 15.2).
- **U Controller**: Record Key (seven tracks as bullets), Clear on rotation.z, Ctrl+Z (`steps-anim-clear.png`); the Crouch / prone
  fold-out (`side-34-record-crouch.png`: its second note line was cut in both, now wrapped).
- **Main Camera**: Culling Mask, right-click Ignore Raycast, rename it to "Raycast Off" (`side-42-rename-menu.png`, `side-43-renamed.png`).
- **Prefab instances**: the Debris Emitter (generic card) and T Capsule saved as prefabs; a drag on Count and on Radius gives the blue bar;
  the menu's first line is blue, "radius: 0.5 -> 2.5" (`ok-52c.png`, `ok-56c.png`); Revert Property puts it back (`ok-53c.png`).

Seen in the pictures and not changed: the Lock Inspector box after an undo: the Inspector can show another object (the pinned object's
address is reused when the undo rebuilds the scene; Dear ImGui and OkayUI share that code). The fold-out arrow is smaller in OkayUI.

### 16.7 Tests run at the end

`ctest -R "okayui|ui_backend|hierarchy|inspector"`: 41 of 41 pass (33 in a Release tree; the 8 that need the engine — `okayui`, `ui_backend`, `inspector_model`, `scene_hierarchy_search`, `deep_hierarchy`, `hierarchy_context_model`, `hierarchy_row_model`, `okayui_script_adapter` — in the editor's tree). The six prefab suites (`ctest -R prefab`, for the Schema change) pass. `editor/main.cpp` compiled in the editor build with `OKAY_HAVE_OKAYUI` and passes the syntax-only check without it; `OkayPanels.cpp` both ways; `InspectorPanel.cpp`, `EditorPanelViews.cpp` and `NavigationWindow.cpp` without it.

### 16.8 Not verified

Windows; other UI scales and the Compact density; an object dragged from the Hierarchy onto an OkayUI object field; the Particle System
with curves (Dear ImGui's body is drawn then, not checked in the real editor); the Character Controller 2D, the Polygon and Capsule
Collider 2D and the Sprite Animator's frame and clip lists in the real editor (headless and in the comparison only, or not at all for the
Character Controller 2D's platformer rows); Browse... file dialogs.

## 17. The Inspector, stage 4: Text and the UI components, Nav Mesh Agent, Terrain, Visual Script, the Particle System's gradient and curves

More component bodies as field rows, behind the same developer switch (`OKAY_DEV=1` and `OKAY_UI_BACKEND=okayui`); Dear ImGui stays the
default and the fallback. Written from the editor's own Dear ImGui bodies and helpers (`editor/main.cpp` DrawInspector, UIStyleBar,
AnchorCombo, AssetSlot, ShapeCombo, UIRectFields, CornerMaskEditor, ParticleGradientEditor, ParticleCurveEditor; `editor/NavigationWindow.cpp`
AreaMaskCombo; `editor/LevelTools.cpp` DrawTerrainLayersUI and DrawTerrainScatterUI; `editor/LocalizationWindow.cpp` LocalizedTextField;
`editor/UIInspectorLayout.cpp` DrawLayoutGroupFlexFields), Dear ImGui's documented widget behaviour and measurements of both pictures; no
code from any other project.

### 17.1 New row kinds (`shared/InspectorFieldsModel.hpp`)

| Kind | What it is | Drawn by both versions |
|---|---|---|
| `InspRowGradient` | a colour gradient: the check under the bar, 32 strips, a marker per stop (the selected one in the accent); a click on the bar adds a stop with the colour showing there, a drag moves it, a right-click deletes one | `InspGradientAt` (the engine's `ParticleGradient::Evaluate` rule, written again on plain floats), `InspNearestStop`, `InspAddStop`, `InspRemoveAt` |
| `InspRowCurve` | a curve of multipliers: the 64-pixel plot, the x1 line, quarter lines, the curve, a dot per key; the top frozen while a key is dragged | `InspCurveAt`, `InspCurveTop`, `InspNearestKey`, `InspAddKey` |
| `InspRowAnchor` | AnchorCombo: the anchor's name on a button with its little picture, the 3 x 3 grid and the stretch cells in a popup; Shift is reported (`axes`) so the host can move the widget onto the anchor | `InspAnchorFill`, `kInspAnchorNames` |
| `InspRowSlot` | AssetSlot: "None (Font)" or the file name on a button 28 short of the region, a small "x"; set only by a drop from the Project panel | |
| `InspRowRadio` | Dear ImGui's RadioButton (the terrain's paint layer) | |
| `InspRowRange` | DragFloatRange2 (two cells kept on their side of each other, two formats) | |
| mask presets | `InspRowMask` with `presetNames` / `presetBits`: small buttons on one line at the top of the open list instead of Everything and Nothing, `count` bits named by `items`, the closed text from `preview` (the Nav Mesh Agent's Area Mask) | |
| list headings | `InspRowCombo::heads`: the editor's section heading before an item inside the open list (the UI Button's Function: "Native (gameplay)", "Script") | |
| swatch-only colour | `InspRow::noInputs`: ColorEdit3 with NoInputs (the terrain's layer colours) | |

Also: a note can sit on a frame-tall line (`alignFrame`, the style bar's "Look"); a multi-line text takes its height in lines (`texH`); a
negative width is now the room left from where the row starts (after SameLine and indents), as Dear ImGui's SetNextItemWidth(-n) is; a group
title (SeparatorText) takes its padding and rule thickness from the host's style (`EditorMetrics::sepTextPadding`, `sepTextBorder`: the
editor's 22 / 8 / 1; it was drawn with Dear ImGui's defaults, 11 pixels shorter than the editor's).

### 17.2 What is ported

| Component | Builder | Notes |
|---|---|---|
| Text | `DrawTextWithOkayUI` | the style bar (screen text), Localized (while off), the three-line text box, the six colour swatches, every row; the Anchor grid keeps the text where it is on screen (ReanchorKeepRect), Fit to Text |
| Canvas | `DrawCanvasWithOkayUI` | every row, the resolution presets, Add Safe Area |
| UI Button | `DrawUIButtonWithOkayUI` | the style bar, Label or "Text is the child object", Target Object (an object dragged from the Hierarchy gives its name), the Function list with its headings, Amount, Anchor, Shape, the font slot, every style and behaviour row |
| UI Image | `DrawUIImageWithOkayUI` | Anchor, Pos / Size or the four margins of a stretched widget, the image slot and thumbnail, Set Native Size, Shape and the rounded-corner boxes |
| UI Slider, UI Toggle, UI Input Field | `DrawUISliderWithOkayUI`, `DrawUIToggleWithOkayUI`, `DrawUIInputFieldWithOkayUI` | every row, the style bar, Localized |
| UI Layout Group | `DrawUILayoutGroupWithOkayUI` | Direction, the flex rows, Columns, Origin, the gaps, Anchor, Arrange Now |
| Nav Mesh Agent | `NavigationWindow.cpp` DrawNavAgentRows (drawn through main.cpp's `DrawRowsOkayUI`, registered with `SetNavRowsDrawer`) | every row; Area Mask with its two presets inside the list; the Play-mode state lines and Stop / Resume |
| Terrain | `DrawTerrainWithOkayUI` and `LevelTools.cpp` AddTerrainToolRows | the terrain's rows, the sculpt brush, Generate, erosion, Normalize / Invert, heightmaps, the auto-colour layers, the texture layers (paint layer radio, name, tint, tiling, Remove, texture) and Scatter |
| Visual Script | `DrawVisualScriptWithOkayUI` | the graph's name, error or node count, Open in node editor, Reload, each declared number, bool, vector, colour, text or object variable; edits go through `ApplyVsVarEdit` (shared with Dear ImGui's body) |
| Particle System | `DrawParticleSystemWithOkayUI` | now also with a gradient and curves on: `AddGradientRows` / `AddCurveRows` (the bar or plot, Stop Color and Stop Time, Key (time, x), Smooth, the counts); the selected stop or key is kept per object and editor id, so an undo keeps it |

Still Dear ImGui by choice: a Text, UI Button, Toggle or Input Field whose Localized box is ticked (the string-table key picker is not a
row), and a Visual Script with a list, map or struct variable.

**Counts.** About 223 hand-written bodies (section 16.1). **Ported to field rows: 33** (stages 1 to 3: 22; stage 4: Text, Canvas, UI
Button, UI Image, UI Slider, UI Toggle, UI Input Field, UI Layout Group, Nav Mesh Agent, Terrain, Visual Script; the Particle System is now
whole). **Generic card: 8** (unchanged). **Still Dear ImGui: about 190.**

**What the example scenes use** (`examples/**/*.okayscene`, 70 scenes; scenes / objects): Text 31 / 57 and Visual Script 25 / 29 (both
ported now), Camera 47 / 48 and Light 19 / 19 (stage 2), then Player Controller 6 / 6, Character 6 / 6, the animation state machine 6 / 12,
Holdable Object 5 / 9, Object Holding 5 / 5, Water, Voxel Terrain, Voxel Digger, UV animation, Reflection probe, Pause Menu, Ground Shadow,
FP Hand and Flashlight 4 each: these are the next to port (the Player Controller and the Character have sub-parts with their own headers).

### 17.3 Bug fixed: Ctrl+A just before digits dropped the first digit

The host (`shared/OkayUIHost.hpp`) gathers one frame's key presses without their order. OkayUI's text field applied the typed text first and
select-all after it, so Ctrl+A and "2" arriving in one frame (xdotool, a fast typist) selected the "2" just typed, and the ".5" of the next
frame replaced it: 0.5 instead of 2.5. Select-all now comes before the text, in `InputText` and in `CodeEdit` (multi-line fields).
`okayui_behaviour` has a check that fails without the fix (two failures) and passes with it. In the real editor: double-click Box Size,
Ctrl+A, "300", Enter gives 300 in both toolkits (`<scratchpad>/shots/oi4/steps-text-edit.png`).

### 17.4 Found and fixed in the pictures

- **Both versions:** a cut "T" right of the Text component's text box (Dear ImGui drew the "Text" label past the region-wide box; it has no
  label now); cut notes in the UI Button ("…greyed out", "…component to call.") and the Terrain layers ("One ground texture today…", "The
  layers replace…"), now wrapped; the Terrain's Invert button ran out of the Inspector (Terrace, Normalize and Invert now wrap to the next
  line when they do not fit); the Terrain's "Heightmap (World Machine / Gaea / Photoshop)" heading was cut, now "Heightmap (PNG import /
  export)".
- **OkayUI:** a swatch-only colour's label was a text-line item, so a row of them drifted down; a Range row's label made the Normalize button
  sit lower; the Area Mask's "Everything" ran out of its list (the list now fits its presets); the gradient bar and curve plots were 6 pixels
  wider than the region (the right-most marker was cut); the gradient's selected stop went back to the first after an undo; the anchor grid's
  rows were closer together than Dear ImGui's.

### 17.5 Tests

- `okayui_behaviour`: the Ctrl+A check (17.3).
- `okayui_inspector` (261 checks; 5 new golden images: `inspector_text`, `_uibutton`, `_navagent`, `_curves`, `_terrain`): the style bar wraps
  inside the region with the first preset on the "Look" line; the text box is the region wide; the anchor grid opens and a pick sets the
  anchor and closes it; the Function list has 5 rows with a heading gap and a pick sets the function; the font slot leaves room for its
  "x", which clears it; the Area Mask's presets and a tick; a click on the gradient bar adds a stop with the colour showing there, a drag moves
  it, a right-click deletes it; a click on the curve plot adds a key under the pointer, a right-click deletes it; a terrain radio picks the
  layer; a range's low cell stays under the high one; the swatch-only colours share a line.
- `okayui_inspector_imgui` (50 checks): 11 more bodies in both toolkits; a gradient click and a curve click give the same stop and key in both;
  Generate is the same click in both.
- `ctest -R "okayui|ui_backend|hierarchy|inspector|prefab"`: 47 of 47 pass (one editor tree, Release with `-O1 -g0`).

### 17.6 Speed and pixels

Instructions (callgrind, 200 frames, per frame): Text 434,413 against 330,083 (24% fewer), Terrain 450,538 against 422,670 (6% fewer),
Particle System gradient and curves 311,440 against 272,948 (12% fewer), Nav Mesh Agent 166,810 against 115,337 (31% fewer). Wall time
(median of 150 frames) and pixels:

| Body | Dear ImGui build | OkayUI build | Dear ImGui build + render | OkayUI build + render | Clearly different pixels |
|---|---:|---:|---:|---:|---:|
| Text | 147.8 us | 92.2 us | 58.2 ms | 16.6 ms | 2.56% |
| Canvas | 101.3 us | 39.3 us | 48.7 ms | 7.3 ms | 1.53% |
| UI Button | 148.6 us | 97.2 us | 61.1 ms | 19.3 ms | 4.54% |
| UI Image | 113.8 us | 51.1 us | 51.1 ms | 10.1 ms | 1.25% |
| UI Slider | 129.4 us | 66.2 us | 55.6 ms | 13.6 ms | 1.86% |
| UI Toggle | 118.6 us | 54.2 us | 51.5 ms | 10.0 ms | 1.28% |
| UI Input Field | 116.7 us | 51.8 us | 52.0 ms | 10.4 ms | 1.32% |
| UI Layout Group | 88.9 us | 32.4 us | 48.5 ms | 6.9 ms | 0.81% |
| Nav Mesh Agent | 90.9 us | 33.2 us | 50.4 ms | 8.4 ms | 1.15% |
| Terrain | 145.5 us | 106.6 us | 58.7 ms | 16.5 ms | 3.90% |
| Particle System (gradient, curves) | 120.3 us | 56.2 us | 54.1 ms | 10.4 ms | 1.18% |

The gate is 12%; every picture is under it. Two are over the 4% aim's neighbourhood: the UI Button (4.54%) and the Terrain (3.90%) are
taller than the comparison canvas, so Dear ImGui adds a scroll bar there and its value column is narrower (seen side by side in
`<scratchpad>/shots/oi4/compare/`); in the real editor, where both have the scroll bar, the rows line up (17.7). The stage 1 to 3
pictures are unchanged.

### 17.7 In the real editor

Built (Linux, the editor tree) and run under Xvfb at 1920 x 1200, both toolkits side by side, on the physics playground with the stage 3
objects and twelve more (made by `<scratchpad>/oi4-logs/gen/gen_scene.cpp` with the engine's own serializer: a screen Text, a Canvas with a
Button, Image, Slider, Toggle, Input Field and Layout Group, a Nav Mesh Agent, a Terrain, a Particle System with a gradient and two curves;
and the Counter object of `examples/node_playground/variables.okayscene` for a Visual Script). Pictures in `<scratchpad>/shots/oi4/`
(`side-*`: the two Inspectors next to each other, `steps-*`: before and after). Done and looked at:

- **Text** (`side-01-text.png`, `side-02-text-down.png`): row for row the same. A 40-pixel drag on Font Size (0.1 to 4.1), double-click
  Box Size, Ctrl+A, "300", Enter, Ctrl+Z twice: the same in both (`steps-text-edit.png`). The Title preset (`side-48-title.png`: Font
  Size 5, Bold, the colour) and Ctrl+Z (`side-49-title-undo.png`). The Anchor grid (`side-05-anchor-open.png`), the middle cell: Center and
  the same new Offset in both (`side-06-anchor-center.png`), Ctrl+Z (`side-07-anchor-undo.png`).
- **UI Button, Canvas, UI Image, Slider, Toggle, Input Field, Layout Group** (`side-08`, `side-20` to `side-31`): the same rows; the
  Function list opens (only "(none)": the button's object has no script, so the headings were seen headless only).
- **Nav Mesh Agent** (`side-10-agent.png`, `side-52-mask-open.png`): the Area Mask list with its two presets; Everything, the same closed
  text in both (`side-53-everything.png`).
- **Terrain** (`side-12` to `side-14`, `side-55`, `side-56`): the wrapped buttons and notes; Add Layers (two layers), the first layer's
  round button, Ctrl+Z (`steps-terrain-layers.png`).
- **Particle System** (`side-32-fire.png`, `side-33-fire-grad.png`): a drag on the middle gradient marker and Ctrl+Z (`steps-gradient.png`);
  a click on the speed curve adds the same key in both, 0.714 x 0.629 (`side-60-curve-add.png`).
- **Visual Script** (`side-41-vscript-body.png`): a drag on the Number variable (0 to 2), Ctrl+Z (`steps-vscript.png`,
  `side-44-vs-undo-down.png`).

Seen and not changed: the OkayUI multi-line text box shows its text a few pixels higher than Dear ImGui's; OkayUI's selected radio button
is a ring with a small dot where Dear ImGui's is a filled disc; Dear ImGui's popups open left of the Inspector under Xvfb; the UI frame card
above the Transform (Dear ImGui in both) shows its selected anchor dot only in the Dear ImGui build (not looked into).

### 17.8 Not verified

Windows; other UI scales and the Compact density; the Function list's headings, the Target Object drop from the Hierarchy and the font
slot drop in the real editor; localized texts (Dear ImGui's body by design); a Visual Script in Play mode (its live variables tree) and its
vector, colour and text variables in the real editor; the Terrain's Scatter, erosion, heightmap import and export in the real editor;
Shift+click on the anchor grid.

## 18. The Inspector, stage 5: the Player Controller and its parts, Character, Holdable Object, Object Holding, the Animator state machine

The next most used bodies by example-scene count (section 17.2), as field rows behind the same developer switch (`OKAY_DEV=1` and
`OKAY_UI_BACKEND=okayui`); Dear ImGui stays the default and the fallback. Written from the editor's own Dear ImGui bodies and helpers
(`editor/main.cpp` DrawInspector, CharacterModelTools, KeyBindCombo, NaturalSpeedsButton, WalkSpeedHintLine, PlayerPartHeader;
`editor/AnimatorPreview.hpp` the preview buttons and DrawTransport) and measurements of both pictures; no code from any other project.

### 18.1 Stage 4 follow-ups

- **The gradient after an undo** (fresh pictures with the current build, `<scratchpad>/shots/oi4/steps-gradient-v2.png`): a 40-pixel drag on
  the middle marker gives 0.604 in both, Ctrl+Z gives 0.500 with the middle stop still selected in both; the right-most marker is whole.
- **The curve plot's scale label** ("x1.8") covered the first key at the top left in both toolkits. It now sits on its own text line above
  the plot's right end (the editor's ParticleCurveEditor, the comparison drawer and OkayUI's; a click on that line adds no key):
  `<scratchpad>/shots/oi4/side-64-curve-label.png`, golden `inspector_curves`.

### 18.2 What is ported

| Component | Builder (`editor/main.cpp`) | Notes |
|---|---|---|
| Player Controller | `DrawPlayerControllerWithOkayUI` | View (the change still goes through the pending switch), Use natural walk and run speeds, the character model tools |
| First-person settings | `DrawFirstPersonWithOkayUI` | every row: Feel presets, speeds, the walk-speed hint, key lists (KeyBindCombo, 96 wide), the gamepad list, stance, lean, feel, head bob, slide, dodge, traversal, animation |
| Third-person settings | `DrawThirdPersonWithOkayUI` | every row, the camera rows, the collision layer list as text |
| View toggle | `DrawPlayerViewWithOkayUI` | every row |
| Character | `DrawCharacterWithOkayUI` | the character model tools (`AddCharacterModelToolRows`), Unrig, the rig rows, the clip bindings, clothing, hair, accessories, the eleven colours, the animation, the Clips fold-out with Preview / Stop and the preview transport (`AddTransportRows`), head look, feel |
| Holdable Object | `DrawHoldableWithOkayUI` | every row, the two presets, the grips |
| Procedural Object Holding | `DrawObjectHoldingWithOkayUI` | every row, the Play-mode lines |
| Animator (state machine) | `DrawAnimStateMachineWithOkayUI` | Open Animator Graph, Reset to default, Add built-in actions, Entry, the stance and gait fold-outs, each state (name, clip, speed, loop, x, Preview / Edit clip with the transport) and its transitions fold-out (target, condition, parameter, value, blend, x, + Transition), + Add State |

A fold-out whose label has a "###id" part is kept open by that part alone (the transitions' count is in the shown text).

**Counts.** Ported to field rows: **41** (33 after stage 4, and these 8). Generic card: 8. Still Dear ImGui: **about 182**. Seen in the
pictures and not ported: Item Pickup, Hold Prompt, the First Person Hands and Hand bodies.

### 18.3 Found and fixed in the pictures

- **Both toolkits:** the state machine's rows ran out of the docked Inspector ("Add built-in actions", each state's loop and x, each
  transition's value, blend and x, the script note); Hold For Prone and the zoom FOV after their rows; the Character's "Play YOUR authored
  clips..." and "No clips yet..." notes. They now wrap: the Dear ImGui body with SameLineOrWrap (and `FieldW` / `CheckW` for a field's or a
  check box's width), the rows with `SameLineOrWrap` (which now counts a row's own width and label).
- **OkayUI:** wrapped text broke 6 pixels later than Dear ImGui's (the body's window reaches past the region for the prefab bars), so the
  walk-speed hint took two lines where Dear ImGui took three, and everything below was 36 pixels higher; wrapped notes and labels now end at
  the region's edge (`WrapAtRegion`). The Sprite Renderer's note, which ran 2 pixels past the region in its golden, now wraps inside.

### 18.4 Tests

- `okayui_inspector` (280 checks; 3 new golden images: `inspector_animsm`, `_character`, `_firstperson`): nothing in the state machine runs
  past the region; a transition's target list and a pick; the clip previews sit on their clip's line; Max Jumps on Can Jump's line; a key
  list's 9 rows and a pick; the Holdable's 14 styles.
- `okayui_inspector_imgui` (56 checks): 6 more bodies in both toolkits.
- `ctest -R "okayui|ui_backend|hierarchy|inspector|prefab"`: 47 of 47 pass. `editor/main.cpp`, `OkayPanels.cpp`, `EditorPanelViews.cpp`,
  `NavigationWindow.cpp`, `LevelTools.cpp` and `LocalizationWindow.cpp` pass the syntax-only check without `OKAY_HAVE_OKAYUI`; main.cpp
  compiled with it.

### 18.5 Speed and pixels

Instructions (callgrind, 200 frames, per frame): Character 436,718 against 383,298 (12% fewer), First-person settings 342,734 against
255,445 (25% fewer), the state machine 244,774 against 188,149 (23% fewer), Holdable Object 289,226 against 200,718 (31% fewer).

| Body | Dear ImGui build | OkayUI build | Dear ImGui build + render | OkayUI build + render | Clearly different pixels |
|---|---:|---:|---:|---:|---:|
| Procedural Object Holding | 102.8 us | 39.9 us | 51.6 ms | 8.2 ms | 1.20% |
| Holdable Object | 114.7 us | 48.9 us | 52.7 ms | 10.0 ms | 1.81% |
| Character | 139.9 us | 75.7 us | 52.6 ms | 10.9 ms | 3.36% |
| First-person settings | 123.6 us | 73.0 us | 59.9 ms | 16.0 ms | 3.63% |
| View toggle | 84.6 us | 27.5 us | 47.5 ms | 5.4 ms | 0.83% |
| Animator state machine | 104.3 us | 48.0 us | 47.7 ms | 7.3 ms | 1.61% |

Every picture is under the 12% gate and the 4% aim; the earlier ones are unchanged (the UI Button stays at 4.54%, section 17.6).

### 18.6 In the real editor

Run under Xvfb at 1920 x 1200, both toolkits side by side, on `examples/fp_hand/fp_hand_test.okayscene` (its Player has a Character, two
Animator state machines, Procedural Object Holding, a Player Controller with First- and Third-person settings; Carry Crate has a Holdable
Object). Pictures in `<scratchpad>/shots/oi5/`. Looked at:

- **Player**, every page from the top (`side-01-player.png` to `side-12-page.png`) and from the end (`side-20-end.png` to `side-45-up.png`);
  after the wrap fix the two Inspectors line up to the pixel (`side-50-fp.png`, `side-51-pc.png`).
- **First-person settings:** Arcade (every speed changes the same), a 40-pixel drag on Jump Force (10.5), Ctrl+Z twice: the same in both
  (`steps-fp-feel.png`).
- **Holdable Object** on Carry Crate: Left Grip off (two rows go), Preset: block, Ctrl+Z twice: the same in both (`steps-holdable.png`).
- **The state machine:** Idle's transitions open (`side-61-tr-open.png`), the first condition's list (`side-62-cond-open.png`), Bool false,
  Ctrl+Z, and the fold-out stays open (`steps-sm-cond.png`).

Seen and not changed: a combo's closed text that is longer than its box is cut inside the box in OkayUI and drawn under the arrow in Dear
ImGui ("Bool false", "CrouchWalk"); Dear ImGui's lists open left of the Inspector under Xvfb; Item Pickup and Hold Prompt (not ported) have
notes cut in both toolkits. (Stage 6, section 19, fixed the first and the last.)

### 18.7 Not verified

Windows; other UI scales and densities; the character model tools with a real model (Fix Facing, Turn, Fit Collider), the clip preview
transport and Edit clip in the real editor; a Player whose View is one view only; Play mode (the state machine's current state, the holding
lines); the Third-person collision layer list typed in the real editor.

## 19. The Inspector, stage 6: Item Pickup, Hold Prompt, Water, Flashlight, Pause Menu, Hand, Voxel Digger, Voxel Terrain

The next most used bodies by example-scene count (each of Water, Voxel Terrain, Voxel Digger, Pause Menu, Hand (legacy) and Flashlight is
in 4 example scenes; Item Pickup and Hold Prompt were seen in the stage 5 pictures with notes cut off), as field rows behind the same
developer switch (`OKAY_DEV=1` and `OKAY_UI_BACKEND=okayui`); Dear ImGui stays the default and the fallback. Written from the editor's own
Dear ImGui bodies (`editor/main.cpp` DrawInspector, ItemPicker; `editor/LocalizationWindow.cpp` LocalizedSwitch) and measurements of both
pictures; no code from any other project.

### 19.1 What is ported

| Component | Builder (`editor/main.cpp`) | Notes |
|---|---|---|
| Item Pickup | `DrawItemPickupWithOkayUI` | every row; the item list (`AddItemPickerRow`) is a plain list of the Item Database's names, orange with a tip when the id is not in it, "(no items yet)" when the database is empty. Dear ImGui's list has a search box; OkayUI's has not |
| Hold Prompt | `DrawHoldPromptWithOkayUI` | every row; Localized goes through `LocalizedSwitchApply` (new, shared with Dear ImGui's LocalizedSwitch) |
| Water | `DrawWaterWithOkayUI` | every row |
| Flashlight | `DrawFlashlightWithOkayUI` | every row |
| Pause Menu | `DrawPauseMenuWithOkayUI` | every row, Localized as above |
| Hand (legacy) | `DrawFirstPersonHandWithOkayUI` | every row, Reset arm pose |
| Voxel Digger | `DrawVoxelDiggerWithOkayUI` | every row; Tool Power is a plain drag in OkayUI (Dear ImGui's is logarithmic) |
| Voxel Terrain | `DrawVoxelTerrainWithOkayUI` | every row: Generate, the seed (a new whole-number row, `InputInt`, with - and +), Random, Generate / Solid Slab / Clear, the material layers and each material's row, footprints, fluid, the build line |

**Counts.** Ported to field rows: **49** (41 after stage 5, and these 8). Generic card: 8. Still Dear ImGui: **about 174**. Next by
example-scene count: the Behaviour Tree runner and Inventory (2 scenes each), First Person Hands (2), World UI 3D and Vehicle Seat (1).

### 19.2 Found and fixed

- **Notes cut off, both toolkits:** Item Pickup's, Hold Prompt's, Water's, Flashlight's, Pause Menu's, Voxel Digger's and Voxel Terrain's
  notes, the Hand's (three lines with hard breaks wider than a docked Inspector) and the Player Controller's "Mouse look + WASD..." now wrap
  at the panel edge (`WrappedNote` in Dear ImGui, wrapping notes in the rows). The digger's and terrain's button rows use SameLineOrWrap.
- **A combo's long closed text, both toolkits:** "CrouchWalk" and "Bool false" ran under the arrow in Dear ImGui and were cut mid-letter in
  OkayUI. Both now cut the text at whole characters and end it with "..." so it fits the room Dear ImGui gives it (box minus arrow minus one
  frame padding; `InspFitText` in `shared/InspectorFieldsModel.hpp`): the rows' combos, the item list, the state machine's Entry, clip,
  target and condition. OkayUI's combo now clips at the arrow, as Dear ImGui does (it clipped half a padding earlier and lost the last dot).
- **OkayUI layout:** a field after a labelled field on one line (the material row's "kg/m3" after "hard") sat 8 pixels low: the label is
  centred on the field, and the next SameLine item took the label's lowered top. The line's top is now kept for the next item (a small
  button and a text after the label still sit on the label's line, as in Dear ImGui).
- **UI Button 4.54%:** the comparison canvas was shorter than the body, so Dear ImGui added a scroll bar and narrowed its column. The tall
  bodies get a 2700-pixel canvas: the UI Button is now **1.50%**, Terrain 3.75% (was 3.90%).

### 19.3 Tests

- `okayui_inspector` (439 checks; 3 new golden images: `inspector_itempickup`, `_voxelterrain`, `_voxeldigger`): the "..." fit (whole
  UTF-8 characters); the item list's pick; the seed typed in; no row of the eight bodies runs past the region.
- `okayui_inspector_imgui` (64 checks): 8 more bodies in both toolkits.
- `ctest -R "okayui|ui_backend|hierarchy|inspector|prefab"`: 46 of 47 pass. `prefab_variants` fails one check (line 284) only when
  `TMPDIR` is a very long path (this machine's scratch folder, 96 characters); it passes with a short one and does not use any changed
  file. Syntax-only checks as in section 18.4.

### 19.4 Speed and pixels

Instructions per frame (callgrind, 200 frames): Item Pickup 142,682 against 96,940 (32% fewer), Hold Prompt 72,621 against 44,239 (39%),
Water 186,054 against 134,547 (28%), Flashlight 131,546 against 92,080 (30%), Pause Menu 174,706 against 130,787 (25%), Hand 151,096
against 102,149 (32%), Voxel Digger 312,773 against 227,657 (27%), Voxel Terrain 439,778 against 331,442 (25%).

| Body | Dear ImGui build | OkayUI build | Dear ImGui build + render | OkayUI build + render | Clearly different pixels |
|---|---:|---:|---:|---:|---:|
| Item Pickup | 105.5 us | 38.2 us | 48.9 ms | 5.5 ms | 1.12% |
| Hold Prompt | 85.3 us | 27.7 us | 47.9 ms | 3.5 ms | 0.48% |
| Water | 118.1 us | 44.2 us | 52.0 ms | 8.5 ms | 0.84% |
| Flashlight | 112.0 us | 39.6 us | 47.8 ms | 4.7 ms | 0.85% |
| Pause Menu | 125.2 us | 47.6 us | 51.9 ms | 5.8 ms | 1.05% |
| Hand (legacy) | 102.9 us | 39.5 us | 48.0 ms | 5.2 ms | 1.33% |
| Voxel Digger | 132.8 us | 74.3 us | 97.1 ms | 15.6 ms | 1.49% |
| Voxel Terrain | 162.7 us | 95.8 us | 98.3 ms | 16.0 ms | 1.67% |
| UI Button (taller canvas) | 170.4 us | 144.5 us | 101.3 ms | 23.2 ms | 1.50% |

Every picture is under 4%; the highest is Terrain at 3.75%.

### 19.5 In the real editor

Under Xvfb at 1920 x 1200, both toolkits side by side; pictures in `<scratchpad>/shots/oi6/`. Looked at:

- `examples/voxel/voxel_sandbox.okayscene`: Water (`side-10-water.png`, `side-11-water-low.png`), Terrain's Voxel Terrain and Voxel
  Digger (`side-20` to `side-25`), FPS Camera's Flashlight and Hand (`side-32-cam-c.png`), Pause Menu (`side-40-pause.png`). The same in both,
  apart from the Hand's note, which was cut in both and now wraps.
- **Character: Shirt** Long sleeve to Short sleeve, then Ctrl+Z: the same in both (`side-53-shirt-short.png`, `side-54-shirt-undo.png`).
- **First-person settings: Sprint Key** Shift to Ctrl, then Ctrl+Z: the same in both (`side-80-sprint-open.png`, `side-8x-sprint.png`).
- `examples/fp_hand/fp_hand_test.okayscene`: Block's Item Pickup (`side-92-block-b.png`; "block" is not in this project's empty Item
  Database, so it is orange), Player's Hold Prompt (`side-95-player-end.png`), Localized on and Ctrl+Z (`side-9x-holdprompt-localized.png`).
- The state machine: Idle's first condition to Bool false: "Bool fal..." and "CrouchW..." in both (`zoom-B4-ellipsis.png`).

Seen and not changed: Dear ImGui's lists still open left of their box under Xvfb (also in stage 5).

### 19.6 Not verified

Windows; other UI scales and densities; the item list with a filled Item Database in the real editor; Generate, Random and the material
rows' edits in the real editor (tried headless); Play mode for all eight; the Voxel Digger's Tool Power drag feel (plain in OkayUI).

## 20. The Inspector, stage 7: the stage 6 gaps, Stats, Inventory, Camera Follow, the World UIs, the vehicle, Joint 3D

Behind the same developer switch (`OKAY_DEV=1` and `OKAY_UI_BACKEND=okayui`); Dear ImGui stays the default and the fallback. Written from
the editor's own Dear ImGui bodies (`editor/main.cpp` DrawInspector and ItemPicker) and measurements of both pictures; the logarithmic
drag from Dear ImGui's documented behaviour (the drag moves along the range's logarithmic scale, a share of the whole range per pixel),
checked against Dear ImGui itself in a test; no code from any other project.

### 20.1 The stage 6 gaps

- **The item list has a search field** (a combo row with `search`): the list's rows whose name or id holds the typed text, any case; a
  typed text that is no id adds 'Use "<text>"', which puts that text in as the id (as the editor's ItemPicker). The field takes the
  keyboard as the list opens. The same field is in the comparison drawer.
- **Tool Power's drag is logarithmic** in OkayUI (`DragFloat` with `SliderFlags_Logarithmic`, new). Dragged in both toolkits by the same
  pixels, the values are the same to 4 decimals: 0 to 100 at 0.5 per pixel, +30 px from 1: 3.98 in both; 0.25 to 8, -40 px from 1: 0.836
  in both; Tool Power's 0 to a billion, +-60 px from 3: 3.0 in both (it barely moves; a plain drag gave 9).
- **A filled Item Database in the real editor** (7 items, loaded by the dev hook `OKAY_UI_ITEMS=1 OKAY_UI_CONTENT_DIR=<project>`): the
  Inventory's slot list on `examples/voxel/voxel_tools.okayscene`'s Player, both toolkits: the list, "pl" typed (Apple, Wood Plank and
  'Use "pl"' in both), Wood Plank picked, Ctrl+Z (`steps-itemsearch.png`, `steps-plank.png`).

### 20.2 What is ported

| Component | Builder (`editor/main.cpp`) | Notes |
|---|---|---|
| Stats | `DrawStatsWithOkayUI` | every row; the HP bar is a new row (`InspRowProgress`, Dear ImGui's ProgressBar); Max and To Next wrap when they do not fit |
| Inventory | `DrawInventoryWithOkayUI` | every row; each slot's "::" is a drag handle (a small button with `dragType`: dropped on another slot's handle, it moves there), the item list with search, the count, X |
| Camera Follow | `DrawCameraFollowWithOkayUI` | every row |
| World UI (3D label) | `DrawWorldUIWithOkayUI` | every row; a localized label keeps Dear ImGui's body (as Text, section 17) |
| World Space UI (3D) | `DrawWorldSpaceUIWithOkayUI` | every row |
| Vehicle Seat | `DrawVehicleSeatWithOkayUI` | every row |
| Vehicle Controller (Car) | `DrawVehicleWithOkayUI` | every row |
| Joint 3D | `DrawJoint3DWithOkayUI` | every row of every mode |

**Counts.** Ported to field rows: **57** (49 after stage 6, and these 8). Generic card: 8. Still Dear ImGui: **about 166**. By
example-scene count: Inventory 2 scenes, the others 1 each, and World UI (3D label) none (the "worldui3d" scene key is the World Space UI;
both are ported). Next by count: Modifiers, Behavior Tree Runner and First Person Hands (2 scenes each), then Sequence Player, NPC
Controller, Dialogue Runner, Quest Log and the debris emitter (1 each).

### 20.3 Found and fixed

- **Notes cut off, both toolkits:** the Vehicle's two-line drive note, the World Space UI's, Joint 3D's, Stats', Camera Follow's and World
  UI's now wrap (one wrapping note each).
- **Wrap decisions in whole pixels (OkayUI):** a label measured 29.48 pixels against Dear ImGui's 29 put Stats' Max on the next line in
  OkayUI only. Same-line-or-wrap now measures in whole pixels, as Dear ImGui lays items out (such a label may end under a pixel past the
  region).
- **An open list's last row was cut by its edge (OkayUI, every combo list):** the list measured its height one item spacing short
  ("Slider (prismatic)" in Joint 3D's Mode list, and a field under a short list showed through). Golden `inspector_joint3d_modes`.
- **A list's search lost the keyboard** when the frame count started again (the test kit's rigs): a list now counts as just opened then.

### 20.4 Tests

- `okayui_inspector` (573 checks; 7 new golden images: `inspector_itemsearch_open`, `_itemsearch_use`, `_stats`, `_inventory`,
  `_joint3d`, `_joint3d_modes`): the search (any case, one item found, 'Use "<text>"' and its pick); Tool Power's drag; the HP bar's size;
  the slot handle's drag puts the second slot first; no row of the eight bodies runs past the region by a pixel or more; the open list's
  last row.
- `okayui_inspector_imgui` (76 checks): the eight bodies in both toolkits; the logarithmic drag in both, four cases.
- `ctest -R "okayui|ui_backend|hierarchy|inspector|prefab"`: 46 of 47 pass; `prefab_variants` fails its long temp-path check as in
  section 19.3 (another agent has it). The six files of section 18.4 pass the syntax-only check without `OKAY_HAVE_OKAYUI`; the editor
  builds with it.

### 20.5 Speed and pixels

Instructions per frame (callgrind, 200 frames): Stats 149,153 against 113,935 (24% fewer), Inventory 134,725 against 99,892 (26%), Camera
Follow 86,879 against 54,406 (37%), World UI 230,188 against 176,941 (23%), Vehicle Seat 114,551 against 73,275 (36%), Vehicle 252,107
against 188,555 (25%), Joint 3D 171,165 against 115,757 (32%), World Space UI 91,293 against 57,160 (37%).

| Body | Dear ImGui build | OkayUI build | Dear ImGui build + render | OkayUI build + render | Clearly different pixels |
|---|---:|---:|---:|---:|---:|
| Stats | 104.0 us | 51.9 us | 52.2 ms | 8.3 ms | 0.83% |
| Inventory | 105.2 us | 56.8 us | 50.1 ms | 6.5 ms | 0.64% |
| Camera Follow | 88.9 us | 22.5 us | 45.6 ms | 2.8 ms | 0.38% |
| World UI (3D label) | 124.3 us | 51.5 us | 49.2 ms | 7.0 ms | 1.02% |
| Vehicle Seat | 94.8 us | 36.5 us | 46.9 ms | 4.4 ms | 0.65% |
| Vehicle Controller (Car) | 117.6 us | 78.5 us | 61.0 ms | 15.1 ms | 1.96% |
| Joint 3D | 104.2 us | 46.7 us | 49.4 ms | 6.8 ms | 1.26% |
| World Space UI (3D) | 85.8 us | 23.5 us | 44.8 ms | 2.8 ms | 0.80% |

The earlier bodies' numbers are unchanged; the highest of all is still Terrain at 3.75%.

### 20.6 In the real editor

Under Xvfb at 1920 x 1200, both toolkits side by side; pictures in `<scratchpad>/shots/oi7/`. **Each side-by-side is scroll-matched:**
`oi4-logs/drive/pair7.py` finds the offset that lines up the two Inspectors' rows (within 80 pixels) and shifts the OkayUI side by it;
the offsets were 0 or 1 pixel, except after the slot drag (63 pixels: Dear ImGui scrolled during the drag), and the pictures say so.
Looked at:

- `examples/vehicle/vehicle_test.okayscene`, Car: Vehicle Controller and Vehicle Seat (`side-10-car.png` to `side-14-car.png`); Follow
  Camera on (three rows appear) and Ctrl+Z (`steps-follow.png`).
- `examples/voxel/voxel_tools.okayscene`, Player's Inventory with the filled Item Database (`side-24-player-end.png`), the search
  (`steps-itemsearch.png`, `steps-plank.png`), the second slot dragged onto the first and Ctrl+Z (`steps-reorder.png`).
- `examples/physics/physics_playground.okayscene`, a rope ball's Joint 3D (`side-40-joint.png`), Mode to Hinge and Ctrl+Z
  (`side-42-hinge.png`, `crop-43.png`).
- `examples/visual_script/playground/camera.okayscene`, Main Camera's Camera Follow (`side-50-camfollow.png`), a Smoothing drag and Ctrl+Z
  (`steps-smooth.png`).
- `examples/ui_text/ui_text.okayscene`, Orc Chief's Stats (`side-60-stats.png`), a Health drag (the bar follows) and Ctrl+Z
  (`steps-health.png`); a Nameplate's World Space UI (`side-63-w3.png`), Constant size off and Ctrl+Z (`steps-constsize.png`).

Seen and not changed: the Vehicle's wrapped drive note breaks one word apart ("Space =" against "Space" / "= handbrake"); Dear ImGui's
lists still open left of their box under Xvfb.

### 20.7 Not verified

Windows; other UI scales; World UI (3D label) in the real editor (no example scene has one; compared headless); Play mode for all eight;
an Inventory with a weight limit in the real editor (compared headless).

## 21. The Inspector, stage 8: Modifiers, Behavior Tree Runner, First Person Hands; wrapped text breaks where Dear ImGui's does

Behind the same developer switch (`OKAY_DEV=1` and `OKAY_UI_BACKEND=okayui`); Dear ImGui stays the default and the fallback. Written from
the editor's own Dear ImGui bodies (`editor/main.cpp` Modifiers, `editor/BehaviorTreeWindow.cpp` DrawBehaviorTreeInspector,
`editor/FpHandsWindow.cpp` DrawFpHandsBody and its helpers) and measurements of both pictures; no code from any other project.

### 21.1 Why the Vehicle's note broke a word apart, and the fix

Measured in the comparison test (`OKAYUI_GLYPHS=1 tests/test_okayui_inspector_imgui` prints every printable character's width in both):
**Dear ImGui's glyph advances are whole pixels**, and each of the 95 is exactly OkayUI's advance rounded to the nearest pixel (a space
4.00 against 3.83, "=" 9.00 against 8.60). Over a line the roundings do not cancel: the drive note up to "Space =" is 384 pixels in Dear
ImGui and 386.09 in OkayUI, so in a docked Inspector "=" fitted in Dear ImGui and went to the next line in OkayUI. It is a rounding
difference, not the space's width alone. OkayUI's wrapping now measures each word with the advances rounded to whole pixels
(`fMeasureWhole`), so it breaks where Dear ImGui does; the drawn text is unchanged. In the real editor both now break after "Space ="
(`side-50-car-top.png`). The same fix moved three other notes to Dear ImGui's breaks (goldens `inspector_animsm`, `_joint3d`,
`_joint3d_modes`, looked at); no other picture changed by more than 0.01 in average difference.

### 21.2 What is ported

| Component | Builder | Notes |
|---|---|---|
| Modifiers | `DrawModifiersWithOkayUI` (`editor/main.cpp`) | the summary, + Add Modifier (its list fits its widest item), Apply All, each modifier's on box, its fold-out with the "..." at the right edge (Move Up, Move Down, Apply, Copy, Remove; still there when the fold-out is closed), its cost or error and every field of every type; the Boolean's cutter is a search list of the scene's objects. The fold-out's right-click menu is not a row (the "..." opens the same menu); a closed fold-out hides its cost line (Dear ImGui shows it); Array Along Curve shows up to 64 points |
| Behavior Tree Runner | `DrawBehaviorTreeRunnerRows` (`editor/BehaviorTreeWindow.cpp`) | every row and the Play-mode lines; the blackboard key is a list of the tree's keys when it has some |
| First Person Hands | `DrawFpHandsBodyOkayUI` (`editor/FpHandsWindow.cpp`) | every row: presets, the buttons, Player View, both hands, Others View, both arms, the held item's grips; the preset list has no per-preset tooltip (the help line under it says the same) |

New rows: a collapsing header (`Header` / `HeaderEnd`: Dear ImGui's CollapsingHeader, its bar reaching 8 pixels into the window's padding
as Dear ImGui's does, measured), the First Person Hands window's small section heading (`SmallSection`), a menu button (`MenuButton`),
and a small button put at the region's right edge (`alignRight`). Bodies in other files reach the row drawer through
`oked::InspectorRowsOkayUI` (`editor/NavigationWindow.hpp`).

**Counts.** Ported to field rows: **60** (57 after stage 7, and these 3). Generic card: 8. Still Dear ImGui: **about 163**. Not reached
this stage: the simple field lists (Sequence Player, NPC Controller, Dialogue Runner, Quest Log, the debris emitter and the rest).

### 21.3 Found and fixed

- **Both toolkits:** the Behavior Tree Runner's Tree field (-60) and override values (-30) left too little room for their label and buttons:
  "..." and "x" ran past the panel's edge in Dear ImGui too. Now -84 and -36. Its navigation note did not wrap in Dear ImGui; it does.
- **OkayUI:** a fold-out on a check box's line sat at the line's top (Dear ImGui centres it on the box); a combo's list was the box's width
  even when an item was wider ("Subdivision Surface" ran under the list's bar); the comparison canvas for First Person Hands is taller (Dear
  ImGui's scroll bar narrowed its column).

### 21.4 Tests

- `okayui_inspector` (688 checks; 4 new golden images `inspector_modifiers`, `_modifiers_menu`, `_fphands`, `_btrunner`; 3 updated by
  21.1): the "..." at the right edge on its fold-out's line, its menu and a pick, the "..." of a closed fold-out; the header across the
  region with its rows not indented, and folding it; no row runs past the region by a pixel or more (a header reaches into the padding).
- `okayui_inspector_imgui` (79 checks): the three bodies in both toolkits.
- `ctest -R "okayui|ui_backend|hierarchy|inspector|prefab"`: 46 of 47 pass; `prefab_variants` fails its long temp-path check (PR #995
  fixes it). The eight changed editor files pass the syntax-only check without `OKAY_HAVE_OKAYUI`; the editor builds with it.

### 21.5 Speed and pixels

Instructions per frame (callgrind, 200 frames): Modifiers 224,800 against 163,265 (27% fewer), Behavior Tree Runner 168,387 against
124,242 (26%), First Person Hands 454,207 against 378,122 (17%).

| Body | Dear ImGui build | OkayUI build | Dear ImGui build + render | OkayUI build + render | Clearly different pixels |
|---|---:|---:|---:|---:|---:|
| Modifiers | 113.7 us | 42.3 us | 47.0 ms | 4.9 ms | 1.07% |
| Behavior Tree Runner | 105.8 us | 45.2 us | 49.0 ms | 6.5 ms | 1.14% |
| First Person Hands | 165.6 us | 117.7 us | 102.7 ms | 23.2 ms | 2.36% |

### 21.6 In the real editor

Under Xvfb at 1920 x 1200, both toolkits side by side, each side-by-side scroll-matched by `oi4-logs/drive/pair8.py` (the OkayUI side
shifted to line up with Dear ImGui's rows; the shift is printed: 0 or 1 pixel, except the Behavior Tree pictures, where OkayUI's wrapped
note made its body one line taller and the shift is 18 or 19). Pictures in `<scratchpad>/shots/oi8/`. Looked at:

- `examples/modeling_tools/vertex_paint_creases.okayscene`, Creased Cube's Modifiers (`side-10-mods.png`); + Add Modifier > Mirror in both
  (`side-12-added.png`); the new Mirror's "..." menu (`menu-open.png`), Move Up and Ctrl+Z (`steps-moveup.png`); the add list
  (`add-open-v2.png`).
- `examples/behavior_trees/breakpoint_demo.okayscene`, Marker's Behavior Tree Runner (`side-20-bt.png`), + Override and Ctrl+Z
  (`steps-override.png`).
- `examples/fp_hand/fp_hand_test.okayscene`, Player's First Person Hands (`side-31-up.png`, `fph-overview.png`); the header bars measured
  edge to edge: 8 to 407 in both (`side-40-hands.png`); Left hand opened (`side-41-lh-open.png`), a Uniform scale drag and Ctrl+Z
  (`steps-handscale.png`).
- `examples/vehicle/vehicle_test.okayscene`: the drive note breaks after "Space =" in both (`side-50-car-top.png`).

### 21.7 Not verified

Windows; other UI scales; the Modifiers' Boolean cutter list and Array Along Curve in the real editor (headless only); the held item's
grip rows (shown only with the preview open or an item held); Play mode for all three; the add list after the last width change (headless
only).

## 22. The Inspector, stage 9: 62 simple bodies, the stage 8 gaps (design note)

- **Bodies.** 62 more component bodies in `editor/main.cpp` are field rows (behind `OKAY_DEV=1` and `OKAY_UI_BACKEND=okayui`). They were
  drafted from their Dear ImGui bodies by a converter script of our own (one Dear ImGui call per row kind; it refuses a body with anything
  else, or a condition that calls a helper that draws), then read and fixed by hand. Counts: field rows 122, generic card 8, still Dear
  ImGui about 101. The generic card was not used for them: the registry's labels, steps and fields differ from the hand-written bodies,
  which also carry notes and a Remove button, so it cannot look the same.
- **Stage 8 gaps.** A fold-out row can carry a menu (right-click), a row can stay shown under a closed fold-out (`keepClosed`: the
  modifier's cost line), and a combo's list rows can have tooltips (`itemTips`: the First Person Hands presets).
- **OkayUI fixes.** A tooltip is drawn at the end of the frame, on top of everything; drawn in place inside an open list it switched the
  overlay off and the list's later rows vanished. A field after something on its line ends where the value column ends, as Dear ImGui's
  relative item width does. The field rows may draw up to 2 pixels short of the Inspector padding's end on the right.
- **Editor fixes.** While an OkayUI field has the keyboard, key presses are not queued into Dear ImGui and the shortcuts stay out: at a
  low frame rate Dear ImGui handed a typed "h" out after a click, which hid the selected object. The stage 9 bodies' grey notes wrap in
  both versions (they ran past the edge in both).
- **Numbers.** Comparison fixtures for Stat: Health, Collectible, Day / Night Cycle and Top Down Shooter Controller: 23 to 37% fewer
  instructions per frame, 0.44% to 1.82% of pixels clearly different (the highest of all bodies is still Terrain at 3.75%). Compared side
  by side in the real editor under Xvfb only; not on Windows.


## 23. The Inspector stack on main (design note)

The Inspector work of sections 14 to 22 was built on older OkayUI branches (the closed PR #988, about ten releases behind). Main had
meanwhile written several of the same toolkit pieces its own way. Bringing the stack onto main kept main's version wherever both had one:

- **Kept from main:** edit tracking (`IsItemEdited`, `IsItemDeactivatedAfterEdit`), number formats (`SetNextNumberFormat`, `FormatNumber`),
  hover flags, tooltips with widgets, context popups, draw flags and their values, `GetMouseDragDelta`, `GetDragDropPayload`, the dock
  builder (`OkayUIDockBuilder`), the code editor's line commands, paragraph spacing of multi-line text (`ParaLines`), the window calls of 6.10.
  The stack's own header `OkayUIImCompat.hpp` is gone; its names are main's `OkayUICompat.hpp` names (`Child_*`, `Hovered_*`, `TabBar_*`,
  `TabItem_*`, `Table_BordersOuter`), with four tab flags added there (`TabBar_AutoSelectNewTabs`, `TabBar_NoCloseWithMiddleMouseButton`,
  `TabItem_UnsavedDocument`, `TabItem_NoReorder`, ...).
- **Taken from the stack:** framed widgets (`StyleVar_FramedWidgets`), text rows (`StyleVar_TextRowHeight`), cell, popup and inner
  spacing styles, combo list placement and 32 popup slots, whole-pixel wrapping in Dear ImGui's text rows, `SearchField`, the middle
  button, reorderable and closable tabs, hideable table columns and the outer table border (`Table_BordersOuter`), `CenterNextOnLine`,
  the end of a combo's text at its arrow (OkayUI's own combo let a long text run over the arrow and the label; one golden of main,
  `port_project_import_texture`, was made again for it).
- **Dear ImGui's look only with `StyleVar_FramedWidgets`.** Where the stack changed something main's own ports rely on, the stack's
  behaviour applies only while that style is pushed (the Inspector views push it; main's ports do not), so main's pictures stay the same:
  the flags form of `BeginChild` (Dear ImGui's: padding only for a bordered or padded child, an outline, the scroll bar only on overflow,
  auto height; otherwise main's: the filled frame, padding always), menu rows and sub-menus inside a popup (Dear ImGui's list to the right;
  otherwise main's selectable rows and sub-menus that open in place), `StyleVar_ButtonTextAlign`, a check box or radio button with no label
  taking no gap after its box, a popup kept inside the display, a pushed `StyleVar_PopupWidth` that the popup does not grow past, and the
  width of a labelled combo given by `SetNextItemWidth` (the whole row there; main's rule, the box only, otherwise). With text rows
  (`StyleVar_TextRowHeight`) a short text after a taller item on its line is centred on it.
- **Framed number fields.** The stack's `DragFloat(.., format)` overloads clashed with main's `DragFloat(.., fmt, flags)`. There is one
  public form now, main's; while `StyleVar_FramedWidgets` is pushed it draws the stack's framed field or slider (`FramedNumbersOn`,
  `FramedDragFloatN`, `FramedSliderFloat`, `FramedSliderInt`, `FramedSliderFloatN`), otherwise main's look.
- **The switch.** `okayuibackend::InspectorOn` (`shared/OkayUIBackend.hpp`): the OkayUI backend plus `OKAY_DEV` or `OKAY_UI_INSPECTOR`.
  Settings > Experimental has an "OkayUI Inspector" item (off by default) that sets `OKAY_UI_INSPECTOR=1`; it needs the "OkayUI screens"
  item too. The backend alone ("OkayUI screens") leaves the Inspector to Dear ImGui, so the existing item does not change the Inspector.
- **Edit marks.** Main's tracking with two of the stack's rules: a text field marks its own edits (with `InputText_EnterReturnsTrue` the
  Enter is not an edit, and an edit committed by Enter does not count again when the field is left), and a text field that was not drawn
  in a frame gives up the keyboard.
- **The default editor is main's.** `DrawInspector` is main's function with only the dispatch lines added (`if (UseOkayUIInspector() &&
  Draw...WithOkayUI(...))`). The stack's changes to Dear ImGui bodies (wrapped notes, `SameLineOrWrap`, fitted combo text, the curve
  editor's label line, wider fields) were not taken, so nothing changes without the switch.
- **Bodies left Dear ImGui.** Main changed the Dear ImGui bodies of the Camera, Light, Mesh Renderer, Particle System, Water, Modifiers,
  Cinemachine Brain and the First-person, Third-person and View toggle settings after the port; their OkayUI rows are out of date, so
  their dispatch was not added and their editor wrappers were removed (the rows and fixtures stay in the tests; the wrappers are on
  `claude2/okayui-inspector-9`).
- **Panels not wired.** The Hierarchy, Problems, Stats, History, Profiler, Tags and Layers, Import, Post-processing and Project Settings
  ports (sections 9 to 13) keep their models, views and tests, but the editor draws main's Dear ImGui panels: main changed most of them,
  and the stack had replaced their Dear ImGui code with re-written bodies, which would have changed the default editor.
- **Fixes for CI (PR #1094).** A table's hidden-column masks live in float state slots; a slot holding NaN or another widget's value was
  cast to `unsigned` (undefined behaviour, caught by UBSan in `okayui_fuzz`). `tblStoredMask` accepts only a whole number 0..65535 and
  treats anything else as unset, at all three places (the hidden mask, the previous mask, `TableSetColumnEnabled`). `okayui_inspector_imgui`
  took about 9 minutes (CI stops a test at 5): almost all of it was the software rasteriser drawing 58 cases x 2 sides x 150 timed frames,
  with Dear ImGui's full-panel background sampled through the font atlas pixel by pixel. The rasteriser's fast paths (row runs, one-colour
  triangles, a row-copy clear) give byte-identical pictures (all 114 compared) and the test now times 75 frames per case in a normal run
  (150 with `OKAY_UI_COMPARE_STRICT=1`, where the speed is checked; `OKAY_UI_COMPARE_FRAMES` still overrides): about 9 minutes to under
  half a minute in a Release build.
- **Hidden tabs (fix after v2.903.1.3).** The editor's `DrawInspector` runs its body whether or not `Begin` says the window shows, as
  Dear ImGui code may: ImGui then skips every item. The OkayUI sections (`HostSection`, editor/OkayPanels.cpp) did not skip: with the
  Inspector and the Hierarchy as two tabs and the Hierarchy picked, every section was drawn and offered the mouse anyway, and since a
  skipped ImGui item does not move the cursor they all landed on the same line, painted over the Hierarchy (OkayUI is flushed after
  ImGui): a component bar, "+ Add Component" over its name, the Tag / Layer / Save as Prefab row. `okayuihost::HiddenInImGui`
  (`shared/OkayUIHostImGui.hpp`) now answers "ImGui shows nothing of this window" (a tab behind another, collapsed, a hidden frame);
  `HostSection` then draws nothing and offers nothing (and hands the frame to the Dear ImGui body, which ImGui skips), `HostWindow` and
  the Console's OkayUI panel likewise on a frame ImGui keeps hidden. Shown, the card was already laid out right: the name has the room
  left of Static and Lock (they drop to the next line when narrow) and Add Component is under the last component. Tests:
  `okayui_host_layering` (two tabs either way round, side by side, a collapsed window) and `okayui_inspector` (the name, the boxes
  and Add Component apart at 260 to 900 pixels). The Hierarchy in that picture is Dear ImGui's (the OkayUI Hierarchy is not wired, see
  "Panels not wired"); it was only covered, not broken.
- **Not taken:** the navigation fallback probe index and crowd example (an unrelated engine change in the stack's base), the Hierarchy's
  flat row list in the editor, pictures under `docs/images/` (removed on main; the section texts above still name them), `docs/try_it`
  entries.

## 24. Three windows on OkayUI only: Crash Log, Audio Mixer, History (no Dear ImGui version)

The first editor windows with no Dear ImGui body at all (rule 19d: one version, no fallback). Before, the Crash Log and the Audio
Mixer had an OkayUI port behind the "OkayUI screens" switch and their Dear ImGui body next to it, drawn whenever the switch was off
or anything of Dear ImGui lay over the window; History had an unwired OkayUI port and two Dear ImGui bodies (one in `editor/main.cpp`,
one in `editor/EditorPanelViews.cpp` that only the comparison test used).

**Where the panels stand (2026-11-23, main).**

| App | Window or page | Drawn by |
|---|---|---|
| Editor | Crash Log, Audio Mixer, History | OkayUI only (this section) |
| Editor | Console | OkayUI with the "OkayUI screens" switch (on by default), Dear ImGui otherwise and while covered |
| Editor | Inspector | OkayUI sections with "OkayUI Inspector" (on by default); stale bodies and the rest Dear ImGui |
| Editor | Script Editor code area | OkayUI `CodeEdit` with `OKAY_UI_CODEEDIT=1` (developer), else the vendored ImGui editor |
| Editor | Problems, Stats, Profiler, Hierarchy, Project Settings sections, import settings, Post-processing | OkayUI ports exist (models, screens, tests, wrappers in `editor/OkayPanels.cpp`) but are not wired: Dear ImGui |
| Editor | every other window (about 40: Scene, Game, Project, Timeline, Animator, Visual Script, Lighting, Build Settings, ...) | Dear ImGui |
| Launcher | What's New, Tips | OkayUI with the switch, Dear ImGui otherwise and while an ImGui popup covers the page |
| Launcher | Versions | probed and golden-tested headless only; Dear ImGui |
| Launcher | every other page and window | Dear ImGui |

**Why these three.** They are the smallest windows with a finished OkayUI screen (`okayui/screens/OkayUIPanels.cpp`,
`OkayUIEditorPanels.cpp`) and golden tests already (`okayui_panels`: `crashlog_*`, `audiomixer_*`; `okayui_editor_panels`:
`editorpanels_history`), and each is one function of `editor/main.cpp` with no state shared with other windows.

**The gap that forced a Dear ImGui copy, closed.** OkayUI's frame is drawn after Dear ImGui's, so a hosted body was painted over every
ImGui popup, tooltip and window in front; the hosts therefore drew the Dear ImGui version while anything covered the window
(`CoveredInImGui`). Two pieces replace that:

- **`OkayUI::BeginCapture(Capture_KeepOverlay)`** (`okay/UI/OkayUICapture.hpp`): the captured windows' own shapes are cut out of
  the frame, but what they draw on the overlay layer (their popups, open lists, a modal and its dimming) stays in OkayUI's frame, on
  top of everything as before. `Capture_All` (the default) is the old behaviour the Script Editor uses. Test: `okayui_capture`
  `OverlayCanStayInTheFrame`.
- **`okayuihost::HostPanelWindow`** (`shared/OkayUIImGuiEmbed.hpp`): the ImGui window (tab, title, close box) with one placeholder
  item over its free area; the OkayUI body is drawn with that capture and its shapes are put into the window's draw list
  (`ImGuiEmbed::Append`, already used by the Script Editor), so Dear ImGui draws the panel in its own order. The body offers the
  mouse with ImGui's hover answer (no mouse while a popup or a window in front is over it); its overlays offer the mouse too. The
  panel works in any OS window of Dear ImGui's (24.1). `editor/OkayPanels.cpp` `HostWindow` now calls it (the Profiler keeps its unwired `fitBody` first frame).

**What changed in the editor.**

- `DrawAudioMixer`, `DrawCrashLog` and `DrawHistory` in `editor/main.cpp` call the OkayUI windows only; their Dear ImGui bodies are
  deleted (about 170 lines of `editor/main.cpp`). History builds its model only while the window shows and commits the pending undo step there, as the
  Dear ImGui body did.
- `OkayUIInputOn()` is also true while one of these panels is hovered or has the focus (24.1), so they get the mouse and keys even
  with "OkayUI screens" off; a press over an OkayUI overlay is kept from Dear ImGui under the same rule.
- The Audio Mixer's model fill is one function, `oked::FillAudioMixerModel` (`editor/AudioMixerModel.hpp`), used by this window and
  by window mode (`editor/OkayUIHostMode.cpp` had a copy).
- History's Dear ImGui body moved from `editor/EditorPanelViews.cpp` into `tests/test_okayui_editor_panels_imgui.cpp` as the
  reference the comparison measures against; the editor does not contain it.
- The editor compiled against OkayUI's Dear ImGui layer (`OKAY_EDITOR_ON_OKAYUI`, off) draws the three screens straight into its
  (OkayUI) windows (`PlainWindow` in `editor/OkayPanels.cpp`).

**Behaviour.** The same screens as the goldens: Crash Log (Refresh, Copy All, Open Folder, Clear with its confirmation, the reports
newest last), Audio Mixer (meters with a dB tooltip, master and group volume and mute, ducking, Reset), History (Undo, Redo, the rows,
a click jumps there). New against the old editor body: History's rows have the right-click menu (Jump to this state, Copy name) that
the port always had. Windows opened for the first time with no saved place open at 440 x 340 (times the UI scale), as the OkayUI port
did; Dear ImGui sized them to their first frame.

**Tests.** `okayui_panels_imgui` (new, `tests/test_okayui_panels_imgui.cpp`): each of the three hosted through `HostPanelWindow` in
a real Dear ImGui context, drawn in the editor's order by one software rasteriser: the picture against the old hosting (OkayUI's
frame drawn last) for all three, an ImGui window in front and an ImGui tooltip stay whole over the panel (the old hosting painted
over them) and the panel offers no mouse under them, Refresh / Clear with the modal / Delete, Reset, a History row asking for three
undos, a collapsed or closed window drawing and offering nothing. Also run: `okayui_capture`, `okayui_panels`,
`okayui_editor_panels_imgui`, `okayui_codeedit_imgui`, `okayui_host_layering`, `okayui_hostmode` (results in the PR).

**Not verified.** Not built or run on Windows; the editor program was not linked or started here (its changed files were compiled
`-fsyntax-only` with g++ and MinGW), so the three windows were not seen in a real window. Panels in another OS window: tested headless with a stand-in platform
backend only (24.1). The `OKAY_EDITOR_ON_OKAYUI` path was only compiled as far as noted in the PR.

### 24.1 Review fixes (PR #1192)

- **Any OS window.** `HostPanelWindow` no longer keeps a floating panel in the main OS window (`SetNextWindowViewport` is gone, and
  with it the frame-late `DockIsActive` test, which could pull a tab being dragged out back into the main window for a frame). In
  another OS window the body's shapes go into that window's draw list, its menus and modals onto that window's foreground list
  (`CapturedGeometry::overlayFrom` splits the two layers; `ImGuiEmbed::Append` takes a command range), and the mouse is mapped:
  `okayuihost::MouseInOtherViewport` (`shared/OkayUIHostImGui.hpp`) gives Dear ImGui's desktop position in the main window's pixels
  while the mouse is over another ImGui OS window, and the editor feeds that to OkayUI (`editor/main.cpp`, before `OkayUI::BeginFrame`).
  The body offers the mouse with ImGui's hover answer there too. Limit: OkayUI keeps a popup inside the main window's display size,
  so a menu of a panel far outside the main window may be moved toward it; a press on such a menu is also seen by Dear ImGui (only
  presses in the main window are kept from it).
- **Input only for OkayUI's own hosted pieces.** `OkayUIInputOn()` is the "OkayUI screens" backend (as on main) or
  `okayuihost::HostedInputWanted` (`shared/OkayUIHost.hpp`), taken once at the start of OkayUI's frame: a panel drawn by OkayUI only
  noted itself (`Region::NoteActive`) as hovered or focused, a hosted rectangle was hovered (`Region::AnyHovered`, the code editor's
  area too), or an OkayUI code field has the keyboard (`g_scriptCodeOwnsKeys`). The OkayUI code editor switch alone no longer turns
  the capture on for the whole editor. With the backend off, a game's `ui_*` widgets in the Game view get only text and Backspace,
  as before, except in frames where such a hosted piece is hovered or focused.
- **Selectable Crash Log.** The reports are a read-only text box again, as Dear ImGui's read-only `InputTextMultiline` was: the
  existing `OkayUI::CodeEdit` made a text box (`MakeTextBox` and `TextBoxPalette`, now public in `okay/UI/OkayUICodeEdit.hpp` and
  shared with `InputTextMultiline`), read-only, opening at the newest line; select with the mouse or keys, Ctrl+C copies through the
  host's clipboard. Golden `crashlog_reports` re-made for it.
- **Menus close when Dear ImGui takes over.** The body's OkayUI menus, lists and modals close when a Dear ImGui popup or modal opens
  over the window, when a click lands outside the body and its menus (OkayUI does not see that click: the host blocks its mouse over
  ImGui), or when the keyboard focus leaves the window.
- **Tests.** `okayui_panels_imgui`: input only while hovered or focused; the Crash Log's modal closes under an ImGui modal and when
  the focus moves; a History row menu closes on a click outside; a Crash Log in a second OS window (Dear ImGui multi-viewports with a
  stand-in platform backend, no real windows): drawn in its own draw list, its modal on that window's foreground list, offered with
  the mouse, Refresh clicked there, the mouse mapped. `okayui_capture`: `overlayFrom`. `okayui_host`: `Region::Active`. `okayui_panels`:
  the Crash Log selection, Ctrl+C through the host, read only, a reload goes back to the end.
- **Not verified.** Real OS windows (SDL's or the own backend's multi-window path), Windows, the editor program.

## 25. Problems, Stats and the Profiler on OkayUI only; the Dear ImGui reference bodies leave the editor

The same move as section 24 for three more windows: `DrawProblems` (`editor/ProblemsPanel.cpp`), `DrawStats` and `DrawProfiler`
(`editor/main.cpp`) build their model (`shared/EditorPanelModels.hpp`) and call the OkayUI window (`editor/OkayPanels.cpp`,
`okayuihost::HostPanelWindow`); their Dear ImGui bodies are deleted. Each model is filled only while the window shows, as the Dear
ImGui bodies did (the Stats frame-rate history, the Profiler's `Stat3` feed of the scene's draw counts).

**Checked against main's panels before wiring** (section 23 had left them unwired because main changed them after the port):

- **Problems**: the model's rows, filters, grouping, repeats dropped once, Fix and a click to open match main's panel. New in the
  editor: the row menu (Go to source, Fix, Copy message) the port always had. The scripted diagnostics demo (`OKAY_UI_DIAG`) takes the
  row and Fix positions from the OkayUI result (`ProblemsRects`); it no longer scrolls the list to the row first.
- **Stats**: the same counters, switches and footer. New: the census's "Copy stats" menu.
- **Profiler**: main had added a **Textures** fold-out (the texture library: summary line, the over-budget warning, the hot-reload
  note, a table of file, size, mips, MB and kind with the path as a tooltip). Added to the model (`ProfilerTextures`, filled through
  `ProfilerModel::textures`, asked only while the fold-out is open, as main read the library only then) and to the OkayUI Profiler;
  without the callback nothing changes, so the `editorpanels_profiler` goldens stay. Test: `okayui_editor_panels` (folded at first and
  not asked, opened, two rows, the budget warning, folded again and not asked).

**The reference bodies leave the editor.** `editor/EditorPanelViews.cpp` (the Dear ImGui bodies of the ported panels) is no longer in
`editor/CMakeLists.txt`: the editor called none of them any more. Only the comparison tests compile it, as the reference the ports are
measured against (`okayui_editor_panels_imgui`, `okayui_hierarchy_imgui`, `okayui_inspector_imgui`).

**Other changes.** `HostWindow` lost its `fitBody` first frame (it drew a Dear ImGui body); the six OkayUI-only windows share one
definition for both hosts (`PanelWindow`: `HostWindow` in the editor, a plain window in `OKAY_EDITOR_ON_OKAYUI`).

**Tests.** `okayui_panels_imgui` now also hosts Problems, Stats and the Profiler (each picture against the old hosting; Fix and a row
click, the Occlusion switch, Pause and the scrub position) and `okayui_editor_panels` covers the Textures fold-out.

**Hierarchy: not done in this step.** Main's `DrawHierarchy` is about 590 lines (rename in place, drag and drop with the Scene view
and the Project panel, prefab rows, diagnostic markers, the context menus); bringing the port's `HierarchyModel` and intents up to it
is a step of its own.

**Not verified.** Windows, the editor program (not linked or started here), the panels in a real window, the diagnostics demo.
