# Our own UI toolkit: OkayUI replaces Dear ImGui

Part of the plan in `docs/own_code_roadmap.md` (Dear ImGui row).

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

- Every widget appends to one fixed geometry batch: `SDL_Vertex` arrays (20,000 vertices, 30,000 32-bit indices) plus a second "overlay" batch (combo lists, tooltips, drag chip, toasts) that is appended after the main one. Quads, triangle fans for circles, rounded rectangles built from bars and corner fans. Nothing is anti-aliased.
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
| 11 | `GetColorU32` | 577 | Missing (colours are RGBA bytes in `Theme`; no packed-colour helper) |
| 12 | `Dummy` | 535 | Done |
| 13 | `PopID` | 393 | Done |
| 14 | `PushStyleColor` | 392 | Partial (8 theme colours (`Col_*`), not the full ImGuiCol set) |
| 15 | `GetWindowDrawList` | 365 | Missing (no public draw-list API for custom drawing (lines, circles, polygons, text at a point)) |
| 16 | `PushID` | 361 | Done (int and string) |
| 17 | `GetCursorScreenPos` | 346 | Missing (`GetItemRect` gives the last item only) |
| 18 | `IsKeyPressed` | 343 | Partial (nine key edges in `Input`) |
| 19 | `Separator` | 326 | Done |
| 20 | `GetContentRegionAvail` | 323 | Missing |
| 21 | `CalcTextSize` | 321 | Partial (`CalcTextWidth` (width only)) |
| 22 | `PopStyleColor` | 298 | Done |
| 23 | `TextUnformatted` | 294 | Done (`Text`) |
| 24 | `SetCursorScreenPos` | 290 | Missing |
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
| 38 | `GetTextLineHeight` | 180 | Missing |
| 39 | `EndDisabled` | 176 | Missing |
| 40 | `BeginDisabled` | 175 | Missing |
| 41 | `InvisibleButton` | 158 | Missing |
| 42 | `Spacing` | 157 | Done |
| 43 | `PopStyleVar` | 155 | Missing |
| 44 | `Text` | 153 | Done (no printf formatting: pass the formatted string) |
| 45 | `TextWrapped` | 150 | Done |
| 46 | `SetWindowFontScale` | 148 | Partial (`PushTextScale`) |
| 47 | `ColorEdit4` | 137 | Missing (`ColorEdit3` only (no alpha)) |
| 48 | `DragInt` | 125 | Done |
| 49 | `GetFontSize` | 124 | Missing |
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
| 60 | `SetCursorPosX` | 79 | Missing |
| 61 | `GetStyle` | 79 | Partial (`Style()` returns `Theme`) |
| 62 | `IsItemActivated` | 78 | Missing |
| 63 | `SetNextWindowSize` | 77 | Done (size is a `Begin` argument; `ResizableNextWindow` adds a grip) |
| 64 | `PushTextWrapPos` | 76 | Missing (`TextWrapped` only) |
| 65 | `PopTextWrapPos` | 76 | Missing |
| 66 | `GetFrameHeight` | 76 | Missing |
| 67 | `GetCursorPosX` | 76 | Missing |
| 68 | `GetItemRectMin` | 73 | Done (`GetItemRect`) |
| 69 | `SetNextWindowPos` | 71 | Done (position is a `Begin` argument; `PinNextWindow`) |
| 70 | `GetItemRectMax` | 71 | Done (`GetItemRect`) |
| 71 | `TableSetupColumn` | 70 | Partial (headers and weights in `BeginTable`, drag-resizable) |
| 72 | `DragFloat3` | 69 | Missing |
| 73 | `IsMouseClicked` | 66 | Missing (edges are computed inside, not exposed) |
| 74 | `AlignTextToFramePadding` | 65 | Done (text rows are already frame height) |
| 75 | `IsItemActive` | 64 | Missing |

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

1. **A public draw-list API** for custom drawing (about 2,000 `AddXxx` call sites plus `GetWindowDrawList` 365): rectangles, lines, circles, polylines, paths, quads, gradients, images, clip stack, text at a point. OkayUI has the internals; they need a stable public face and a cursor and measurement API (`GetCursorScreenPos`, `SetCursorScreenPos`, `GetContentRegionAvail`, `CalcTextSize`, `GetTextLineHeight`, `GetFontSize`, `GetFrameHeight`, about 1,800 call sites).
2. **Item state queries and interaction helpers:** `IsItemHovered` flags, `IsItemActive`, `IsItemActivated`, `IsItemClicked`, `InvisibleButton` (158), `IsMouseClicked`/`Down`/`Dragging`/`DoubleClicked`, `GetColorU32` (577), `SetNextItemAllowOverlap`.
3. **Style:** `PushStyleVar`/`PopStyleVar` (375), the full colour set, per-widget colours, themes; disabled blocks (`BeginDisabled`/`EndDisabled`, 351); `PushTextWrapPos`.
4. **More input:** ctrl and alt, key down state, the other keys the editor uses (about 30), middle button, mouse delta, clipboard, double click, cursor shapes, IME; keyboard and gamepad navigation, `SetKeyboardFocusHere`.
5. **Widget gaps:** `InputTextWithHint` (204), multi-line text (25), password; `DragFloat2/3/4` (164), `ColorEdit4`; `SliderFloat` with format and typed entry; `BeginCombo`/`EndCombo` form; nested menus and menu shortcut text; `BeginPopupModal`; tab items; table sorting, scrolling and frozen rows; tree node flags; `ProgressBar` with overlay is done; `SeparatorText`, `Bullet` are done.
6. **Windows:** flags (auto-resize 72, no scrollbar 47, no scroll with mouse 29, no title bar 12, no background), z-order, saved settings, window queries (`IsWindowFocused`, `IsWindowHovered`, `SetWindowFocus`), `SetNextWindowSizeConstraints`.
7. **Docking and multi-viewports** (OS windows). The editor layout depends on them. The largest single piece.
8. **Typed drag and drop payloads.**
9. **Fonts:** pushing arbitrary fonts, a monospace code font, per-window font scale.
10. **Backends:** an OpenGL path if ever needed; the Direct3D 11 backend should use the new `DrawCmd` list (clip rectangles and the atlas texture) so it matches the SDL backend; an SDL event translator for the extra input fields is the host's job today.
11. **Performance:** nothing measured on a large editor panel. The batch is fixed (20,000 vertices, 30,000 indices, plus an overlay batch of the same size); overflow now drops geometry safely but a full editor panel was never tried. No culling of clipped items.
12. **Accessibility, IME and right-to-left** are not started.

Then the migration itself: panels ported one at a time behind an option, with the same golden and behaviour tests run against both toolkits before the default changes. That is later stages of `docs/own_code_roadmap.md`, not this one.
