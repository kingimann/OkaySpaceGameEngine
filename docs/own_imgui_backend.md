# Our own Dear ImGui backend (design and first slice)

Part of the Dear ImGui and SDL2 rows of `docs/own_code_roadmap.md`. Dear ImGui is still the editor's and launcher's UI; this is the
piece that lets it run without SDL: `imgui_impl_sdl2` + `imgui_impl_sdlrenderer2` replaced by code of ours over
`okay::platform` (events, clipboard, cursor, text input, time) and `okay::platform::Painter` (drawing).
**Off by default** (`-DOKAY_OWN_IMGUI=ON`, then `OKAY_IMGUI_BACKEND=own`). It is wired into exactly one place, the editor's
`--vs-shot` tool, because that is the only ImGui program that needs no window of its own.

## What the apps call today

From a search of `editor/` and `launcher/` (the calls the two SDL backends make for us):

| Call | Editor | Launcher |
|---|---|---|
| `ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)` / `ImGui_ImplSDLRenderer2_Init(renderer)` | `main.cpp`, `VsGraphShot.cpp` | `main.cpp` twice (main window and the chat host window) |
| `ImGui_ImplSDL2_ProcessEvent(&e)` in the app's `SDL_PollEvent` loop | 1 (+ the shot tool) | 2 |
| `ImGui_ImplSDLRenderer2_NewFrame()` + `ImGui_ImplSDL2_NewFrame()` | yes | yes |
| `ImGui_ImplSDLRenderer2_RenderDrawData(draw data, renderer)` | yes (+ `UpdateTexture`, called by the app for new font atlases) | yes |
| Shutdown pair | yes | yes |
| ImGui configuration | NavEnableKeyboard, DockingEnable, **ViewportsEnable** (multi-window) under a setting, `DisplayFramebufferScale` used for `SDL_RenderSetScale` | none of those |

What the SDL backends do behind those calls (the job list this design covers): keyboard keys and modifiers, text input,
mouse position, buttons and wheel, focus changes, the clipboard, the mouse cursor shape and hiding, text-input start/stop
for the IME, display size and DPI scale, delta time, gamepad navigation, multi-viewports, and for the renderer: converting the
draw lists to triangles with clip rectangles and the font texture.

## The design

Three layers, so the testable part needs no Dear ImGui at all:

1. **`shared/ImGuiOwnInput.hpp/.cpp`, plain logic.** `InputTranslator` (platform event to a neutral list of key / modifier /
   mouse / wheel / text / focus operations, modifiers first, position before a button, modifiers re-sent after a focus loss),
   the key table (`UiKey`, generated from one macro list that the adapter reuses for `ImGuiKey`), mouse button numbering,
   `ComputeDisplay` (display size and framebuffer scale), `FrameTimer`, `MapCursor` (ImGui's cursor numbers to
   `okay::platform::CursorShape`, -1 = hidden), `ClipboardCache` (ImGui's "pointer valid until the next call, null on failure"),
   `TextInputGate`, and the draw helpers (clip rectangle rounded outwards, `ImDrawVert` to `ui2d::Vertex`, indices, alpha-only
   texture to RGBA).
2. **`shared/ImGuiOwnBackend.hpp/.cpp`, the adapter.** `OwnBackend::Init / ProcessEvent / NewFrame / RenderDrawData / Shutdown`.
   It sets `io.BackendFlags` (mouse cursors, vertex offset, textures), the platform clipboard functions, feeds `io.Add*Event`,
   sets `DisplaySize`, `DisplayFramebufferScale`, `DeltaTime`, switches the cursor and the text input when they change, handles
   Dear ImGui 1.92's texture requests (`ImTextureData` create / update / destroy through painter textures, alpha-only atlas
   expanded to white + alpha), and draws every command through `Painter::Geometry` with its clip rectangle and the framebuffer
   scale. `window` and `backend` may be null: then it is headless (size from the painter, no clipboard or cursor).
3. **`okay::platform` additions.** `CursorShape` and `Backend::SetCursorShape` (the nine shapes ImGui and OkayUI use):
   real on the sdl2 backend (system cursors) and the own Windows backend (`IDC_*` cursors, shown through `WM_SETCURSOR`); the own
   Linux backend returns false (no X cursor font code yet) and keeps the arrow.

## Wiring

`editor/VsGraphShot.cpp` (`OkayEngine --vs-shot <dir>`): built with `-DOKAY_OWN_IMGUI=ON` and run with `OKAY_IMGUI_BACKEND=own`, it
draws the Visual Script editor through the own backend into a bitmap painter (no SDL window, no renderer) and writes **PNG**
files; the log says `[okay] imgui backend: own`. Without the switch it is the SDL path and BMP files, unchanged. The editor window
and the launcher are **not** wired: both create `SDL_Window`s and poll `SDL_Event`s, so moving them means moving their windows
onto `okay::platform` first (the SDL window plan, `docs/sdl_removal_plan.md`).

## Tests

* `imgui_own_input` (`tests/test_imgui_own_input.cpp`, 64 checks, no Dear ImGui; clean under ASan + UBSan): key mapping and
  uniqueness, translator ordering and filtering, display, timer, cursor, clipboard, text-input gate, clip, vertex, index and
  alpha conversion, with values worked out by hand.
* `imgui_own_backend` (`tests/test_imgui_own_backend.cpp`, 18 checks): the **real Dear ImGui 1.92.9 WIP** (the pinned commit's
  sources, compiled into the test), no window and no SDL. Platform events go in through `ProcessEvent`; a window with an input
  field and a button is built; the draw data goes out through the backend into a test painter that rasterises with OkayUI's
  triangle routine. Verified: the font texture is created through the painter and destroyed on shutdown; the window face is
  drawn and the outside untouched; typed text (accents) and Backspace edit the focused field; Ctrl and the wheel arrive; a
  click (move, press, release in separate frames) presses the button; the button face and glyph pixels exist; focus loss
  clears the mouse. It needs the Dear ImGui sources: it uses the editor's `okay_imgui` target, or the copy fetched for
  `test_vs_editor_model`; without either it is not built.

## What is not done (said plainly)

* **Not run in a window by anyone**, and the `--vs-shot` own path was only compile-checked (the editor was not built here).
* Multi-viewports (`ViewportsEnable`, the editor's multi-window setting): none. This is the largest gap: the own backend
  has no platform windows per viewport, so the editor would lose its pop-out windows (or keep the SDL backend for that setting).
* Gamepad navigation, touch and pen (mouse source), IME composition and the candidate window position (the platform layer has no
  rectangle call), `io.WantSetMousePos` (warping the mouse for keyboard navigation), mouse capture while dragging outside the
  window, the cursor shapes on own Linux, the window DPI on Linux, per-monitor DPI changes (the app must re-scale fonts: use
  `OwnBackend::ContentScale`).
* The editor's and launcher's `SDL_Renderer` code around ImGui (`SDL_RenderSetScale`, the editor's viewport texture copy,
  `RenderFlush`), their SDL windows and event loops, and the second ImGui context of the launcher's chat host window.
* Speed: the draw path converts every vertex each frame on the CPU and draws with the software painter; a real editor frame was not measured.
