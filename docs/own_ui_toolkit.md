# Our own UI toolkit (`okay::imui`) and the Dear ImGui audit

Part of the plan in `docs/own_code_roadmap.md` (Dear ImGui row). This page has three parts: what the editor, launcher and shared code actually call (the audit), what `okay::imui` is and how to use it, and what is still missing before it could replace Dear ImGui.

**Status: stage 1, side by side, not used by anything.** The editor and launcher still use Dear ImGui and no ImGui call was changed. `okay::imui` is a new library with its own tests. It is not the default and nothing switches to it.

## 1. Audit: which ImGui APIs we call

Scope: every `.cpp`, `.hpp` and `.h` under `editor/`, `launcher/` and `shared/` that mentions ImGui (60 files, not counting the downloaded ImGui sources and the embedded font data). Method, so it can be repeated:

```
grep -rhoE "ImGui::[A-Za-z0-9_]+" <those files> | sort | uniq -c | sort -rn
```

Totals: 24,862 call sites to 259 distinct `ImGui::` functions. The 75 most used cover 22,527 of them (90.6 percent), so the long tail is small. The numbers count call sites in source, not calls per frame.

How much of that `okay::imui` covers today, counted over the 75 most used functions: fully implemented ones account for 50.8 percent of all call sites, partly implemented ones (for example `IsItemHovered` without flags, `InputText` without hints) for another 14.4 percent. About a third of all call sites are in functions we do not have at all: tooltips (`SetTooltip`, 1,438), popups and menus (`OpenPopup`, `BeginPopup`, `BeginMenu`, `MenuItem`), `ColorEdit4`, `GetColorU32`, `GetIO`, wrapped text, disabled blocks, mouse cursors.

### Ranked list (top 75)

"Done" means `okay::imui` has an equivalent with the same job (the name may differ). "Partial" says what is left out.

| Rank | Function | Call sites | `okay::imui` |
|---|---|---|---|
| 1 | `SameLine` | 1737 | Done |
| 2 | `IsItemHovered` | 1502 | Partial (no flags) |
| 3 | `SetTooltip` | 1438 | Missing |
| 4 | `TextDisabled` | 1292 | Done |
| 5 | `DragFloat` | 1010 | Partial |
| 6 | `SmallButton` | 727 | Done |
| 7 | `MenuItem` | 722 | Missing |
| 8 | `Checkbox` | 713 | Done |
| 9 | `Button` | 704 | Done |
| 10 | `SetNextItemWidth` | 668 | Done |
| 11 | `GetColorU32` | 577 | Missing |
| 12 | `Dummy` | 535 | Done |
| 13 | `PopID` | 393 | Done |
| 14 | `PushStyleColor` | 392 | Done |
| 15 | `GetWindowDrawList` | 365 | Done |
| 16 | `PushID` | 361 | Done |
| 17 | `GetCursorScreenPos` | 346 | Done |
| 18 | `IsKeyPressed` | 343 | Partial (10 keys) |
| 19 | `Separator` | 326 | Done |
| 20 | `GetContentRegionAvail` | 323 | Done |
| 21 | `CalcTextSize` | 321 | Done |
| 22 | `PopStyleColor` | 298 | Done |
| 23 | `TextUnformatted` | 294 | Done |
| 24 | `SetCursorScreenPos` | 290 | Done |
| 25 | `TextColored` | 280 | Done |
| 26 | `CloseCurrentPopup` | 272 | Done |
| 27 | `InputText` | 248 | Partial (no hint, no multiline, no password) |
| 28 | `EndPopup` | 242 | Missing |
| 29 | `Selectable` | 223 | Done |
| 30 | `PushStyleVar` | 220 | Missing |
| 31 | `SliderFloat` | 217 | Done |
| 32 | `OpenPopup` | 211 | Missing (combo only) |
| 33 | `Combo` | 210 | Done |
| 34 | `InputTextWithHint` | 204 | Missing |
| 35 | `EndChild` | 201 | Partial (EndScrollRegion) |
| 36 | `BeginChild` | 198 | Partial (BeginScrollRegion) |
| 37 | `GetIO` | 182 | Missing (Context::in) |
| 38 | `GetTextLineHeight` | 180 | Done |
| 39 | `EndDisabled` | 176 | Missing |
| 40 | `BeginDisabled` | 175 | Missing |
| 41 | `InvisibleButton` | 158 | Missing |
| 42 | `Spacing` | 157 | Done |
| 43 | `PopStyleVar` | 155 | Missing |
| 44 | `Text` | 153 | Done |
| 45 | `TextWrapped` | 150 | Missing |
| 46 | `SetWindowFontScale` | 148 | Missing |
| 47 | `ColorEdit4` | 137 | Missing |
| 48 | `DragInt` | 125 | Missing |
| 49 | `GetFontSize` | 124 | Missing |
| 50 | `End` | 120 | Done |
| 51 | `BeginPopup` | 113 | Missing |
| 52 | `DragFloat2` | 95 | Missing |
| 53 | `EndMenu` | 94 | Missing |
| 54 | `BeginMenu` | 94 | Missing |
| 55 | `SetMouseCursor` | 91 | Missing |
| 56 | `GetFont` | 84 | Missing |
| 57 | `EndCombo` | 83 | Done |
| 58 | `BeginCombo` | 83 | Done |
| 59 | `BeginPopupModal` | 81 | Missing |
| 60 | `SetCursorPosX` | 79 | Missing |
| 61 | `GetStyle` | 79 | Partial (Context::style) |
| 62 | `IsItemActivated` | 78 | Missing |
| 63 | `SetNextWindowSize` | 77 | Done |
| 64 | `PushTextWrapPos` | 76 | Missing |
| 65 | `PopTextWrapPos` | 76 | Missing |
| 66 | `GetFrameHeight` | 76 | Done |
| 67 | `GetCursorPosX` | 76 | Missing |
| 68 | `GetItemRectMin` | 73 | Done (GetItemRect) |
| 69 | `SetNextWindowPos` | 71 | Done |
| 70 | `GetItemRectMax` | 71 | Done (GetItemRect) |
| 71 | `TableSetupColumn` | 70 | Missing |
| 72 | `DragFloat3` | 69 | Missing |
| 73 | `IsMouseClicked` | 66 | Done |
| 74 | `AlignTextToFramePadding` | 65 | Done |
| 75 | `IsItemActive` | 64 | Done |

Ranks 76 to 160 (call sites): `GetMainViewport` 64, `Begin` 56, `TreePop` 54, `SetKeyboardFocusHere` 54, `IsMouseDown` 54, `SeparatorText` 52, `SetClipboardText` 50, `EndGroup` 47, `TableNextColumn` 46, `BeginGroup` 45, `IsWindowAppearing` 43, `IsMouseDragging` 42, `IsPopupOpen` 41, `GetStyleColorVec4` 39, `IsWindowHovered` 38, `IsWindowFocused` 38, `CollapsingHeader` 38, `IsMouseDoubleClicked` 37, `Unindent` 35, `IsItemClicked` 35, `GetTime` 35, `SliderInt` 34, `Indent` 33, `BulletText` 33, `BeginPopupContextItem` 33, `TableSetColumnIndex` 29, `IsMouseHoveringRect` 29, `AcceptDragDropPayload` 29, `TreeNodeEx` 28, `SetCursorPosY` 28, `ColorConvertU32ToFloat4` 28, `SetCursorPos` 27, `GetCursorPosY` 27, `TreeNode` 26, `InputInt` 26, `EndDragDropTarget` 26, `InputTextMultiline` 25, `GetWindowPos` 24, `TableNextRow` 23, `ProgressBar` 23, `ColorEdit3` 23, `IsItemDeactivated` 21, `GetFrameCount` 21, `EndTable` 21, `BeginTable` 21, `BeginDragDropTarget` 21, `EndTabItem` 20, `SetNextWindowSizeConstraints` 18, `IsMouseReleased` 18, `ArrowButton` 18, `IsItemDeactivatedAfterEdit` 17, `GetWindowSize` 17, `SetWindowFocus` 16, `SetScrollHereY` 15, `RadioButton` 15, `TableHeadersRow` 14, `SetNextItemOpen` 14, `SetDragDropPayload` 13, `IsKeyDown` 13, `GetWindowWidth` 13, `GetItemRectSize` 13, `EndTooltip` 13, `EndDragDropSource` 13, `BeginTooltip` 13, `BeginTabItem` 13, `BeginDragDropSource` 13, `Image` 12, `GetFrameHeightWithSpacing` 12, `GetContentRegionMax` 12, `CalcItemWidth` 12, `GetWindowContentRegionMax` 11, `GetTextLineHeightWithSpacing` 11, `GetID` 11, `GetCurrentContext` 11, `SetNextItemAllowOverlap` 10, `IsAnyItemActive` 9, `FindWindowByName` 9, `EndTabBar` 9, `BeginTabBar` 9, `SetItemDefaultFocus` 8, `NewLine` 8, `GetScrollY` 8, `PushFont` 7, `PopFont` 7, `GetScrollMaxY` 7.

The remaining 99 functions are used fewer than 7 times each.

### Other things the audit found

- **Draw list.** `ImDrawList` is used directly a lot (the editor draws its own viewports, gizmos, node graphs and timelines). Call sites: `AddRectFilled` 471, `AddText` about 440, `AddLine` about 395, `AddCircleFilled` 286, `AddRect` 168, `AddTriangleFilled` 79, `AddCircle` 107, path building (`PathLineTo` 51, `PathStroke` 52), `PushClipRect` 34, `AddPolyline` 34, `AddImage` and `AddImageRounded` 32, `AddQuadFilled` 14, `AddRectFilledMultiColor` 13, `AddConvexPolyFilled` 8, `AddConcavePolyFilled` 5, `AddBezierCubic` 5. `okay::imui` has rect (filled, outline, rounded), line, triangle, filled circle, text, image and clip rects. Missing: circle outline, polyline and path API, quads, gradients, bezier, concave polygons, anti-aliasing, per-draw-call callbacks.
- **Docking.** 12 distinct `Dock*` functions, 20 call sites (`DockSpace`, `DockBuilder*`, `IsWindowDocked`, node queries). Few call sites, but the editor layout depends on them and they use ImGui internals (`DockBuilder*`, `DockContext*`, `DockNodeBeginAmendTabBar`).
- **Tables.** `BeginTable` 21, `TableSetupColumn` 70, `TableNextColumn` 46, `TableNextRow` 23, `TableSetColumnIndex` 29, `TableHeadersRow` 14, `TableSetupScrollFreeze` 4, `TableGetSortSpecs` 1. Flags used: row backgrounds, stretch sizing, scroll Y, inner borders, resizable.
- **Tabs.** `BeginTabBar` 9, `BeginTabItem` 13, `TabItemButton` 4. Not in `okay::imui`.
- **Drag and drop.** `BeginDragDropSource` 13, `BeginDragDropTarget` 21, a custom target 4. Not in `okay::imui`.
- **Fonts.** `io.Fonts->AddFontDefault`, `AddFontFromMemoryCompressedBase85TTF` (Roboto and JetBrains Mono, embedded), `PushFont` 7, `SetWindowFontScale` 148. `okay::imui` has one fixed-size bitmap font.
- **Style.** `PushStyleColor` 392 (colours used most: Text, TextDisabled, Button and its hover/active, ChildBg, Border, WindowBg, FrameBg), `PushStyleVar` 220 (WindowPadding, FramePadding, FrameRounding, ChildRounding, ItemSpacing, WindowRounding, ButtonTextAlign, ScrollbarSize). `okay::imui` has the colours, but no `PushStyleVar`.
- **Window flags used:** AlwaysAutoResize 72, NoScrollbar 47, NoSavedSettings 33, NoScrollWithMouse 29, NoResize 20, NoMove 18, NoTitleBar 12, NoCollapse, NoNav, NoDecoration, NoDocking, NoBackground, HorizontalScrollbar. `okay::imui` has AlwaysAutoResize, NoScrollbar, NoResize, NoMove, NoTitleBar, NoCollapse, NoBackground.
- **Input.** `io.MousePos` 120, `io.KeyCtrl` 70, `io.KeyShift` 66, `io.MouseDelta` 51, `io.MouseWheel` 32, `io.KeyAlt` 28, `io.WantTextInput` 19, `io.InputQueueCharacters` 10, `io.ConfigFlags` 9 (keyboard navigation, viewports). Keys used: Escape 48, Up/Down/Left/Right arrows, Enter, Z, F, Backspace, Space, S, V, Delete, A, C, Tab and others (about 30 distinct). Mouse: click, down, double click 37, drag 42, mouse cursor shapes 91 (mostly the hand), `SetClipboardText` 50.
- **Backends.** The editor and launcher use `ImGui_ImplSDL2` for events and `ImGui_ImplSDLRenderer2` for drawing (an SDL_Renderer backend, 4 call sites each). There is no OpenGL backend for ImGui in our code.
- **Multi-viewports** (`ImGuiConfigFlags_ViewportsEnable`, 6 mentions) are used by the editor.

## 2. What `okay::imui` is

An immediate-mode UI core written from scratch. No Dear ImGui code was read or copied; the design follows the general immediate-mode idea (widgets are function calls made every frame, state is keyed by IDs), nothing more. It has no dependency on SDL, OpenGL or the engine.

Files (all under `shared/imui/`):

| File | What |
|---|---|
| `Imui.hpp` | The whole public API: types, draw list, font atlas, input struct, style, context, windows, layout, widgets |
| `ImuiDraw.cpp` | FNV-1a ID hashing, UTF-8 helpers, font atlas, draw list (vertex, index and command buffers) |
| `Imui.cpp` | Context and frame model, windows, layout, widgets |
| `ImuiSoft.hpp/.cpp` | Software backend: draws a draw list into an RGBA bitmap |
| `tests/` | `test_imui_core`, `test_imui_widgets`, `test_imui_golden`, `test_imui_fuzz`, the test kit, committed golden PNGs |
| `demo/` | `imui_demo`, which renders a window of widgets to a PNG |

Build: `-DOKAY_BUILD_IMUI=ON` (the default; the library, tests and demo add about half a minute to a full build). It can also be built alone: `cmake -S shared/imui -B build-imui && cmake --build build-imui`. Tests are registered with ctest as `imui_core`, `imui_widgets`, `imui_golden`, `imui_fuzz`. Sanitizers: `-DOKAY_SANITIZE=address,undefined,float-cast-overflow`.

### Frame model

```
Context* ctx = imui::CreateContext(&okay::Font8x8::Glyph, 2);   // glyph source and integer scale
imui::SetCurrentContext(ctx);
imui::Input in;                      // filled by whatever owns the window; plain data
in.mousePos = ...; in.mouseDown[0] = ...; in.text = "typed utf-8"; in.keyDown[imui::Key_Enter] = ...;
imui::NewFrame(in, displaySize);
if (imui::Begin("Window")) { imui::Button("Hi"); }
imui::End();                         // always, even when Begin returned false
const imui::DrawData& dd = imui::Render();
imui::SoftRender(dd, ctx->font, bitmap);     // or any other backend
```

- **Input** is one plain struct per frame (mouse position and three buttons, wheel, ten keys, ctrl and shift, typed UTF-8 text, delta time). Press and release edges are found by comparing with the previous frame, so a state must be held for at least one frame. Key repeat (0.4 s delay, then every 0.05 s) is done inside the library.
- **IDs.** A 32-bit FNV-1a hash seeded with the top of the ID stack. `PushID(string or int)` / `PopID()`, a `##` suffix hides part of a label from the display but not from the ID, tree nodes push their own ID. Window IDs come from the window name.
- **Hover and active.** Who is under the mouse is decided at the start of a frame from the previous frame's window rectangles, front to back (one frame of lag, the usual immediate-mode trade). A click brings a window to the front. While a popup is open only popups can be hovered, and a click outside closes it and is not passed on. One active ID at a time (the widget being pressed or dragged); a press that started elsewhere never activates a widget.
- **Layout.** A cursor per window. `SameLine`, `Spacing`, `Dummy`, `Indent`/`Unindent`, `BeginGroup`/`EndGroup` (a group is one item), `Columns(n)`/`NextColumn` (equal widths), `SetNextItemWidth`, `AlignTextToFramePadding`, `GetCursorScreenPos`/`SetCursorScreenPos`, `GetContentRegionAvail`. Auto-resizing windows measure themselves and are hidden for their first frame.
- **Draw list.** Vertex (position, uv, packed colour), 32-bit index and command buffers; a new command starts when the clip rectangle or texture changes. Rects (filled, outline, rounded), lines, triangles, circles, text, images, a clip stack. Non-finite or absurd numbers are ignored.
- **Font atlas.** There is no TTF rasteriser of our own on `main` yet: `Graphics/TtfFont.cpp` rasterises with `stb_truetype`, which is a third-party library, so `okay::imui` does not use it. The atlas is built from the engine's own `Font8x8` bitmap glyphs (`engine/src/Graphics/Font.cpp`) scaled by an integer factor (2 gives a 16 pixel line). `FontAtlas` is plain alpha pixels plus metrics, so a TTF rasteriser of ours can fill it later without changing anything else. Only printable ASCII has glyphs; anything else draws as `?`.
- **Software backend.** Triangle rasteriser with the top-left fill rule (shared edges never draw twice), per-command scissor, nearest-neighbour texture sampling, source-over alpha blending. Garbage in the draw data (NaN, infinities, out-of-range indices, bad clip rectangles) is skipped, never trusted.
- **Misuse** (an `End` too many, `PopID` with nothing pushed, `Begin` without `End`) is counted in `Context::errors` and otherwise ignored, never a crash.

### Widgets (in the order of use in our code)

`Text`, `TextUnformatted`, `TextColored`, `TextDisabled`, `Button`, `SmallButton`, `Checkbox`, `SliderFloat`, `SliderInt`, `DragFloat`, `InputText` (cursor, selection, shift-select, ctrl+A, Home/End, Delete/Backspace, mouse placement and drag selection, UTF-8 aware, `EnterReturnsTrue`, `ReadOnly`, `AutoSelectAll`, max length, scrolls to keep the caret visible), `Combo` / `BeginCombo` / `EndCombo` (popup window, closes on pick or outside click), `Selectable`, `TreeNode` / `TreePop`, `BeginScrollRegion` / `EndScrollRegion` (a child with wheel scrolling and a draggable scroll bar), `Separator`, and windows (`Begin` / `End`) with title bar, drag to move, bottom-right grip to resize, collapse arrow, close button, scroll bar, z-order and minimum sizes.

### Tests

- `imui_core`: hashing, UTF-8 (valid, truncated, overlong, surrogate), atlas metrics, draw list batching and clipping, rasteriser exact pixels and the fill rule, text pixel counts against the source glyphs, textures, hostile geometry, ID stack, window move / resize / collapse / close / z-order, misuse counting.
- `imui_widgets`: synthetic mouse, key and text input across frames, asserting results and state: button (hover, press, release inside and outside, press that started elsewhere, right button), checkbox, sliders and drag, text editing (typing, deleting, caret keys, selection, ctrl+A, invalid UTF-8 and control characters dropped, multi-byte deletion, mouse placement, key repeat, focus changes, Enter, read-only, max length), combo (open, pick, close outside, close by header, popup on top, click not passed through), tree node and selectable, scroll region (wheel, clamping, thumb drag, hit testing of scrolled rows), layout rectangles (same line, indent, group, columns, widths, text alignment), child hover.
- `imui_golden`: four committed PNGs (`shared/imui/tests/golden/`), written with `okay::ImageWrite` and read back with a small inflate in the test kit: primitives, a button held, a button hovered, and the demo window after a scripted interaction. Per-channel tolerance 2 and at most 0.05 percent of pixels may differ, to survive compilers that round floats differently. `IMUI_UPDATE_GOLDEN=1` regenerates them; a mismatch writes `<name>.actual.png` into the working directory. Also checks that two runs render the same pixels and that the PNG reader round-trips.
- `imui_fuzz`: seeded random frames (random input including NaN and infinities, random windows and well-nested widget trees with hostile sizes, formats and labels), unbalanced call sequences, a text-editing reference model compared after every key, layout properties (stacked items never overlap, same-line items advance), random draw lists and corrupted buffers into the rasteriser, UTF-8 and hash on random bytes. `IMUI_FUZZ_ITERS` and `IMUI_FUZZ_SEED` scale and vary it. Run under AddressSanitizer and UBSan during development; this found two real bugs in the first version of the rasteriser (a non-finite clip rectangle and a non-finite texture coordinate converted to an int), both fixed with tests.

## 3. Relation to OkayUI, and what is missing before it could replace Dear ImGui

**OkayUI already exists** (`okayui/`, area 40 of the feature map). It is also our own immediate-mode toolkit, about 3.5k lines, and it already has far more widgets than `okay::imui`: tables with resizable columns, menus, popups and context popups, tooltips, collapsing headers, list boxes, colour pickers, plots, drag and drop, wrapped text, several fonts through its own atlas. It draws through SDL_Renderer or Direct3D 11, ships in the player and the editor, and has one small test. This audit was written against ImGui's call sites and this core was written first without building on OkayUI, so the two overlap. What `okay::imui` adds is a clean headless design (no SDL, input as plain structs, a software backend, golden and behaviour tests, fuzzing) and much stricter tests of the widgets it has. What it lacks is nearly everything OkayUI already has. The owner should decide whether the two merge (the cheapest route is probably to give OkayUI this kind of headless test harness and port the missing ImGui calls into it, using `okay::imui` as the reference for the test style), or whether `okay::imui` grows and OkayUI is retired. This stage does not touch OkayUI.

The list below is what `okay::imui` itself would still need. Plainly: most of it. It is a core, not a replacement. In rough order of how much of our code is blocked:

Plainly: most of it. `okay::imui` is a core, not a replacement. In rough order of how much of our code is blocked:

1. **Popups, menus and tooltips.** `OpenPopup`, `BeginPopup`, `BeginPopupModal`, context-menu popups, `BeginMenu`, `MenuItem`, menu bars, `SetTooltip`. About 3,000 call sites in the ranked list alone. Only the combo's popup exists.
2. **Text drawing details.** Wrapped text (`TextWrapped`, `PushTextWrapPos`), bullet text, `SeparatorText`, formatted labels with colours, `SetWindowFontScale`, several fonts and sizes, non-ASCII glyphs, a TTF rasteriser of our own (today only bitmap glyphs), kerning, shaping and right-to-left text.
3. **Tables** (columns with headers, resizing, sorting, scrolling, row backgrounds), **tabs**, **collapsing headers**, **list boxes**, **tree node flags**, **progress bars**, **radio buttons**, **plots**, `ColorEdit`/`ColorPicker`, multi-line `InputText`, hint and password text, `DragInt` / `DragFloat2/3/4`, `SliderFloat2/3`, sliders with keyboard entry.
4. **Docking** and **multiple viewports** (OS windows). The editor layout depends on them. This is the largest single piece.
5. **Drag and drop** between widgets and windows.
6. **Navigation** with keyboard and gamepad, focus handling (`SetKeyboardFocusHere`), tab order, clipboard (`SetClipboardText`, copy and paste in text fields), IME, mouse cursor shapes, double click, right and middle click handling.
7. **Style**: `PushStyleVar`, per-widget colours for all states, themes. `OkayTheme.hpp` would need a port.
8. **Draw list** extras: circle outlines, polylines and the path API, quads, gradients, beziers, concave polygons, anti-aliased edges and lines, rounded images, callbacks.
9. **Disabled blocks**, `BeginChild` with flags and borders, `SetNextWindowFocus`, `IsWindowFocused/Hovered` flags, window settings saved to disk (`NoSavedSettings` is used 33 times), `SameLine` variants, `GetWindowWidth`, `Image`/`ImageButton` with GPU textures.
10. **GPU backends.** Only the software backend exists. Drawing it in the editor needs an SDL_Renderer backend (what we use today with `ImGui_ImplSDLRenderer2`), and later an OpenGL one, plus an SDL input translator that fills `imui::Input`.
11. **Performance.** Nothing was measured; there is no vertex culling, no clipped-item skipping, and `std::string` formatting per `Text` call. A large editor panel has not been tried.
12. **Accessibility, IME and right-to-left** are not started.

Then the migration itself: a seam so panels are written against `okay::ui`, panel-by-panel ports behind an option, and the same golden and behaviour tests run against both toolkits before the default changes. That is later stages of `docs/own_code_roadmap.md`, not this one.
