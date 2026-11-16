# The platform layer (`okay::platform`) and our own backend

Stage 1 of replacing SDL2 with our own code (see the roadmap: seam first, own backend side by side and
non-default, SDL2 stays). This page is the audit of what the engine takes from SDL2, what is behind the
seam now, what is not, and what was and was not tested.

Header: `engine/include/okay/Platform/Platform.hpp`. Sources: `engine/src/Platform/`. CMake: `cmake/OkayPlatform.cmake`.

## Choosing a backend

| | |
|---|---|
| `-DOKAY_PLATFORM_BACKEND=sdl2` | default. `okay::platform::Default()` is the SDL2 backend (a thin wrapper, behaviour as before). |
| `-DOKAY_PLATFORM_BACKEND=own` | `Default()` is our own backend. SDL2 is **not** removed: the editor, launcher and player still need it for windows and `SDL_Renderer` (see "Not behind the seam yet"). |

The environment variable `OKAY_PLATFORM_BACKEND=sdl2|own` set when a program starts overrides the build's choice for that
process (a value that is neither is ignored), so one binary can be tried on either backend. It changes nothing unless set.

Both backends are built whenever they can be, and `okay::platform::Get(BackendKind::Sdl2 | Own)` returns either one, so
a test can run the same checks on each. `Default()` falls back to the other backend if the chosen one is not compiled in.

## What the audit found (origin/main before this change)

About 3,000 lines in 67 files mention SDL, in `engine/`, `editor/` (48k-line `main.cpp`), `launcher/`, `player/`, `shared/`,
`okayui/` and `tests/`. Grouped by what SDL is used *for*:

| Feature | SDL calls (count) | Where | Now |
|---|---|---|---|
| **Timers** | `SDL_GetTicks` 154, `SDL_GetTicks64` 6, `SDL_Delay` 18, `SDL_GetPerformanceCounter/Frequency` 23 | launcher 140, editor 32, player 13, okayui 3, shared 1 | **behind the seam** (`platform::TicksMs32/TicksMs/PerfCounter/PerfFrequency/DelayMs`) |
| **Audio device** | `SDL_OpenAudioDevice` 11, `Pause` 12, `Queue` 5, `GetQueuedAudioSize` 5, `Clear` 3, `Dequeue` 1, `Close` 8, `GetNumAudioDevices` / `GetAudioDeviceName` 2, capture + callback mode (voice chat) | player, editor, launcher (voice notes, calls, mic test, chat chime, device lists) | **behind the seam** (`AudioDevice`, queue and callback mode, playback and capture). `SDL_LoadWAV` (one WAV decode in the launcher) stays |
| **Game controller** | `SDL_GameController{Open,Close,GetAxis,GetButton,Rumble,GetJoystick}`, `SDL_JoystickInstanceID`, `SDL_NumJoysticks`, `SDL_IsGameController` | `engine/.../SdlGamepad.hpp` (now `PlatformGamepad.hpp`), editor Play mode, player | **behind the seam** (`Gamepad`, `Backend::OpenGamepad`). The hot-plug *events* (`SDL_CONTROLLERDEVICEADDED/REMOVED`) still arrive through each app's SDL event loop |
| **Windows** | `SDL_CreateWindow` 7, `DestroyWindow` 7, `Get/SetWindowSize/Position`, `Show/Hide/Raise/Minimize/Restore`, `SetWindowFullscreen`, `SetWindowIcon`, `FlashWindow`, `SetWindowMinimumSize`, `GetDisplayDPI/UsableBounds`, `SDL_WINDOW_*` flags | the three `main.cpp` files | seam API written and tested; **call sites not moved** (see below) |
| **Events, keyboard, mouse, text** | `SDL_PollEvent` 5, `SDL_PumpEvents`, `SDL_PushEvent`, `SDL_GetKeyboardState`, `SDL_GetMouseState`, relative mouse, `SDL_StartTextInput`, `SDL_ShowCursor`, `SDL_WarpMouseInWindow`; event types QUIT, WINDOWEVENT (close, focus, resize), KEYDOWN/UP, TEXTINPUT, MOUSE*, DROPFILE, FINGER*, CONTROLLERDEVICE*, RENDER_*_RESET; `SdlKeys.hpp` scancode table | same | seam API written and tested; **call sites not moved**. Touch (`SDL_FINGER*`) is not in the seam at all |
| **OpenGL context** | `SDL_GL_SetAttribute` 8, `CreateContext` 2, `MakeCurrent` 16, `GetCurrentContext/Window` 8, `DeleteContext` 2, `SDL_GL_GetProcAddress` | editor, player (`GLRenderer`) | seam API written and tested; **call sites not moved** |
| **Clipboard** | none. The editor uses ImGui's clipboard, which goes through `imgui_impl_sdl2` | editor, launcher | seam API written and tested; nothing calls it yet |
| **2D renderer** | `SDL_Renderer`, `SDL_Texture`, `SDL_Surface`: about 330 calls (`SDL_RenderCopy`, `SDL_RenderGeometry`, `SDL_UpdateTexture`, ...) | editor, player, launcher, `okayui` (whole toolkit), `UIPainterSDL.hpp`, tests (golden images use SDL's software renderer) | **not part of the seam.** This is a rendering library, not a platform one, and it needs an `SDL_Window` (or a surface) to draw to |
| **Dear ImGui backends** | `imgui_impl_sdl2` (takes `SDL_Event*` and `SDL_Window*`), `imgui_impl_sdlrenderer2` | editor, launcher | **not behind the seam.** They are third-party code that reads SDL types directly |
| **Threads, mutexes, atomics** | `SDL_CreateThread` 60, `DetachThread` 47, `WaitThread` 17, `Create/Lock/UnlockMutex` ~370, `SDL_Atomic*` ~30 | launcher (downloads, chat, voice), `VoiceChat.hpp` | **not touched.** Not a platform feature in the roadmap's list; `std::thread` would replace it, separately |
| **Misc** | `SDL_Log` 9, `SDL_GetBasePath` 4, `SDL_GetPrefPath`, `SDL_GetPreferredLocales`, `SDL_ShowSimpleMessageBox` 3, `SDL_SetHint` 7, `SDL_SaveBMP`, `SDL_GetWindowWMInfo` + `SDL_SetWindowsMessageHook` (Windows tray icon), `SDL_sinf`-style math, `SDL_free` | all apps | not touched |
| **Build** | `find_package(SDL2)`, static `SDL2::SDL2-static` option, `SDL2.dll` copy, MinGW toolchain, Emscripten `-sUSE_SDL=2` | CMake | `okay_platform` links SDL2 the same way as the apps; Emscripten uses `-sUSE_SDL=2` for the SDL backend (**not built here**) |

## What is behind the seam

`Backend` (`Platform.hpp`) has: init, timers, windows (create, size, position, title, show/hide/raise, minimize, fullscreen,
focus), an event queue, the GL context (create, make current, swap, swap interval, `GLProcAddress`), keyboard state, text input,
mouse (state, relative mode, cursor visibility, warp), clipboard text, audio devices (playback and capture, queue or
callback), and game controllers (open, axes, buttons, rumble). `Key` is the USB HID usage numbering, which is also SDL's scancode numbering.

Call sites moved in this change (each commit builds):

1. Timers everywhere (`okay::platform::TicksMs32()` is a drop-in for `SDL_GetTicks()`).
2. Audio devices: player, editor Play mode, launcher voice notes / chime / device lists, `VoiceChat.hpp` (calls and mic test).
   A voice note in a WAV layout the platform layer does not play (8-bit, 32-bit integer, big-endian) is converted to 16-bit
   once at load; SDL used to convert at the device.
3. Controllers: `Input/SdlGamepad.hpp` became `Input/PlatformGamepad.hpp` (`FeedPad`, `PadSet`; no SDL types); the editor and player open pads through the backend.

## Stage 2: the player's window, events, input and GL context moved onto the seam

Stage 1 (above) put timers, audio devices and controllers behind `okay::platform` and wrote the window, event and GL API
without using it. Stage 2 audits what is left per app and moves the smallest self-contained app, the **player**, all the way
through it. SDL2 is then one backend there. The editor and launcher are not moved yet (table below).

### Audit: SDL identifier occurrences per app and family

Counted with `grep` over `editor/`, `launcher/`, `player/`, `shared/`, `engine/` (without `engine/*/Platform/`, which is the
backend itself), `okayui/` and `tests/`; comments removed. A count is every `SDL_*` / `SDLK_*` word, not a call, so a family
that is mostly types (renderer) reads larger than the number of calls. "Before" is `origin/main` before this change.

| family | editor | launcher | player before -> after | shared | engine | okayui | tests | `okay::platform` has it? |
|---|---|---|---|---|---|---|---|---|
| window (create, size, position, fullscreen, icon, min size, flash, flags) | 49 | 84 | 37 -> 2 | 2 | 0 | 0 | 0 | HAVE. Added for the player: fullscreen/maximised at creation, `IsFullscreen`, `SetIcon`, `RefreshRateHz`. GAP: `SDL_FlashWindow` (launcher), `SDL_GetWindowFlags` minimised/mouse-focus queries (launcher) |
| GL context | 33 | 0 | 21 -> 0 | 0 | 0 | 0 | 0 | HAVE (create, make current, swap, interval, proc address). Editor still on SDL |
| events | 30 | 18 | 24 -> 0 | 0 | 0 | 0 | 13 | HAVE (quit, window close/focus/resize, keys, text, mouse, wheel, file drop, controller hot-plug). Added: touch (`FingerDown/Motion/Up`, sdl2 only) and `wheelFlipped`. GAP: `SDL_TEXTEDITING` (IME composition), `SDL_RENDER_*_RESET` (D3D11 device loss), Windows `SDL_SysWMEvent` |
| keyboard, text input, mouse state | 75 | 2 | 58 -> 0 | 0 | 16 -> 1 | 1 | 20 | HAVE. Added: `RelativeMouseState`, `ModState`, `KeyForChar` (layout-aware), `IsCursorShown`. `Input/SdlKeys.hpp` is replaced by `Input/PlatformKeys.hpp` |
| timers | 1 | 0 | 0 | 0 | 0 | 0 | 0 | HAVE, moved in stage 1 |
| audio device | 1 | 12 | 1 -> 0 | 0 | 0 | 0 | 0 | HAVE (queue and callback, playback and capture). GAP: `SDL_LoadWAV` and the format macros (launcher) |
| game controller | 2 | 0 | 2 -> 0 | 0 | 0 | 0 | 0 | HAVE. The editor's remaining 2 words are its `SDL_CONTROLLERDEVICEADDED/REMOVED` event checks |
| clipboard | 0 | 0 | 0 | 0 | 0 | 0 | 0 | HAVE; the apps use ImGui's clipboard, nothing calls ours yet |
| cursors (system cursor shapes) | 0 | 0 | 0 | 0 | 0 | 0 | 0 | GAP (nothing uses them today either; ImGui sets cursors through its SDL backend) |
| message box | 2 | 0 | 4 -> 0 | 0 | 0 | 0 | 0 | Added `ShowErrorBox`: sdl2 and Win32 real; own Linux returns false and logs (no toolkit to draw one with) |
| display / DPI | 5 | 4 | 4 -> 0 | 0 | 0 | 0 | 0 | Added `SetPerMonitorDpiAware` (sdl2 hint, Win32 API; nothing elsewhere) and `Window::RefreshRateHz` (sdl2 only). GAP: display bounds / usable area / DPI queries (launcher, editor) |
| misc runtime (init, log, paths, locales, hints, `SDL_free`, syswm) | 48 | 17 | 37 -> 3 | 2 | 0 | 25 | 5 | Added `BasePath`, `PrefPath`, `PreferredLocales`, `Log`. GAP: `SDL_GetWindowWMInfo` + `SDL_SetWindowsMessageHook` (launcher tray icon), `SDL_SaveBMP`, hints other than DPI |
| threads, mutexes, atomics | 0 | 705 | 0 | 0 | 0 | 0 | 0 | Not a platform-layer feature; `std::thread` would replace it separately (launcher only) |
| `SDL_Renderer`, textures, surfaces, colours, rects | 250 | 96 | 283 -> 280 | 48 | 92 | 107 | 101 | **Not behind the seam, and the main blocker.** A rendering library, not a platform one; needs an `SDL_Window*`. Needs our own 2D painter (see below) |
| Dear ImGui SDL backends (`imgui_impl_sdl2`, `imgui_impl_sdlrenderer2`) | 2 files | 1 file | 0 | 0 | 0 | 0 | 0 | GAP: third-party code reading `SDL_Event` / `SDL_Window*` directly (editor: 2 files, launcher: 1 file) |
| SDL_image / SDL_ttf / SDL_mixer | 0 | 0 | 0 | 0 | 0 | 0 | 0 | Not used anywhere |

Totals per app (identifier occurrences): editor 496, launcher 938 (705 of them threads and mutexes), player 471 -> 285
(280 of the 285 are `SDL_Renderer` drawing), shared 52, engine 111 -> 95, okayui 133, tests 124 -> 139 (the platform tests
push SDL events on purpose).

Why the player first: 2,795 lines in one file, no ImGui, no threads, and everything it takes from SDL other than drawing was
already in the seam or a handful of small calls. The launcher is 26,000 lines with 705 thread/mutex words and ImGui; the editor
is 48,000 lines with ImGui and 33 GL-context words.

### What was moved (player)

* `main()` opens the platform (`Backend::Init`, video + audio + controllers, or timers only for `--server`), creates the main window
  (`WindowDesc`: high-DPI, resizable, borderless, fullscreen, maximised, created hidden until the boot splash is drawn), sets the minimum size and
  the icon, and creates the hidden GL context window and context for the GPU renderer, all through `okay::platform`.
* The frame loop and the splash loop read `okay::platform::Event` instead of `SDL_Event`: quit, focus, key down (F1, F3, Tab with
  Shift, F11, Alt+Enter, Esc, Shift+Esc, Backspace), text, mouse button and wheel, touch, controller added/removed.
* Held keys (WASD through the layout, digits, arrows, modifiers, F-keys, navigation block), mouse state, relative mouse (mouse-look),
  warp, cursor visibility, window size, fullscreen toggling, refresh rate, base path, per-user save folder, OS locales, error boxes,
  logging, DPI hint, shutdown.
* The window is still an `SDL_Window` for the 2D drawing: `SDL_CreateRenderer` takes it from `Window::NativeHandle()`. That is
  the only SDL window use left in `player/main.cpp` (2 words) plus renderer calls.
* On the `own` backend the stage 2 player created its window, found there was no `SDL_Window`, said so on the log and exited with
  code 2 instead of crashing or pretending. Stage 3 (below) replaced that with a 2D painter; the exit now only happens when the
  painter cannot be made.

New API (every addition has a default, so a backend that cannot do it says so; both backends and the tests cover them):

| API | sdl2 | own Linux | own Windows |
|---|---|---|---|
| `WindowDesc::fullscreen`, `maximized`; `Window::IsFullscreen` | yes | yes (`_NET_WM_STATE`; needs a window manager to show) | yes |
| `Window::SetIcon(rgba, w, h)` | yes | yes (`_NET_WM_ICON`) | no (returns false) |
| `Window::RefreshRateHz()` | yes | no (0 = unknown; no RandR) | no (0) |
| `Backend::RelativeMouseState` | yes | yes (summed from motion events) | yes |
| `Backend::ModState`, `KeyForChar` | yes | `ModState` yes; `KeyForChar` follows the X keymap | `ModState` yes; `KeyForChar` assumes a US layout |
| `Backend::IsCursorShown` | yes | yes | yes |
| Touch events (`FingerDown/Motion/Up`) | yes | never fired | never fired |
| `Event::wheelFlipped` | yes | never set | never set |
| `ShowErrorBox` | yes | no (returns false, text goes to stderr) | yes (`MessageBoxW`) |
| `SetPerMonitorDpiAware` | yes (hint) | nothing to do | yes |
| `BasePath`, `PrefPath`, `PreferredLocales`, `Log` | SDL's | shared implementation in `Platform.cpp` (`/proc/self/exe`, `$XDG_DATA_HOME`, `$LANGUAGE`/`$LANG`) | shared implementation (Win32 calls), not run on Windows |

### Known behaviour differences (small, deliberate or unavoidable)

* Esc, Tab, F11 and Enter are now read by physical key (scancode) instead of SDL's key symbol. They differ only on a layout that has
  moved those keys. The WASD and digit feed still follows the layout (`KeyForChar`).
* The mouse wheel keeps SDL's OS "natural scrolling" meaning in the player (`wheelFlipped` lets the app undo the platform's
  "up = +y" normalisation), so scrolling is as before.
* `SDL_Quit()` is now `Backend::Shutdown()` (`SDL_QuitSubSystem` of what the backend started, including audio/controllers opened lazily).

### Not done (and why)

* **Editor and launcher windows, event loops and GL contexts.** Same blocker as before, plus ImGui: `imgui_impl_sdl2` and
  `imgui_impl_sdlrenderer2` need `SDL_Event` and `SDL_Window*`. The editor's window (49), GL-context (33), event (30) and
  keyboard/mouse (75) words and the launcher's windows and tray hook can be moved the same way the player was, one at a time, but the
  drawing still ends in `SDL_Renderer`.
* **The own backend could not run the player or any app yet.** (Stage 3, below, adds the 2D painter for the player; it is off and not
  ready. The editor and launcher still need it moved in, plus an own ImGui platform backend.)
* IME composition, display queries, window flashing, WAV loading, the tray-icon hook, system cursors, Wayland, touch and refresh
  rate on the own backend (see the tables).
* Windows (own and sdl2 side by side) and the Emscripten web build of the player were not built or run here; the changed Platform
  sources pass `-fsyntax-only` with MinGW; `player/main.cpp` itself was compiled on Linux only.

## Stage 3: a 2D painter, so the player can run on the own backend (own code, NOT READY, off by default)

The blocker stage 2 ended on was that every app draws its 2D through `SDL_Renderer`, which needs an SDL window. Stage 3 puts
that drawing behind `okay::platform::Painter` (`engine/include/okay/Platform/Painter.hpp`), with two implementations, and moves the
player onto it. **The software painter is not ready.** It is reachable only by running the player with `OKAY_PLATFORM_BACKEND=own`
in the environment (the own backend is itself opt-in); there is no setting, no CMake option, no menu entry. The default, the
sdl2 painter, draws exactly what the player drew before.

### Audit: what the player's 2D path calls

Counted in the files the player's drawing runs through, on `origin/main` before this change (comments removed). `main.cpp` is
`player/main.cpp`; `UIPainterSDL` is `engine/include/okay/UI/UIPainterSDL.hpp` (the player's UI pass painter); `OkayUI` is
`okayui/src/OkayUI.cpp` (`EndFrame(SDL_Renderer*)`).

| SDL call | main.cpp | Splash.hpp | ScriptUIBridge | UIPainterSDL | OkayUI | now |
|---|---|---|---|---|---|---|
| `SDL_SetRenderDrawBlendMode` / `GetRenderDrawBlendMode` | 16 / 1 | 1 | | | 2 / 1 | `Painter::SetBlendMode` / `GetBlendMode` |
| `SDL_SetRenderDrawColor` | 15 | 2 | | 1 | | `SetDrawColor` |
| `SDL_RenderFillRect` / `FillRectF` | 14 | 0 / 1 | | 1 | | `FillRect` / `FillRectF` |
| `SDL_RenderDrawRect`, `DrawLine`, `DrawLineF`, `DrawPoint` | 4, 2, 0, 0 | 0, 0, 2, 0 | | 1, 1, 0, 1 | | `DrawRect`, `DrawLine`, `DrawLineF`, `DrawPoint` |
| `SDL_RenderClear` | 2 | | | | | `Clear` |
| `SDL_RenderSetClipRect`, `IsClipEnabled`, `GetClipRect`, `SetScale` | 2, 0, 0, 0 | | | 1, 1, 1, 1 | 4, 1, 1, 0 | `SetClipRect`, `IsClipEnabled`, `GetClipRect`, `SetScale` |
| `SDL_RenderGeometry` | 2 | 4 | | 1 | 3 | `Geometry` (indices optional) |
| `SDL_RenderCopy`, `CopyF`, `CopyEx`, `CopyExF` | 6, 0, 0, 1 | 0, 1, 0, 0 | | 1, 1, 1, 1 | | `Copy`, `CopyF`, `CopyEx`, `CopyExF` |
| `SDL_CreateTexture` / `CreateTextureFromSurface` / `CreateRGBSurfaceWithFormatFrom` / `FreeSurface` | 1 / 1 / 1 / 1 | | 1 | 3 | 1 | `CreateTexture(desc, rgba)` |
| `SDL_UpdateTexture`, `DestroyTexture`, `QueryTexture` | 1, 3, 4 | 0, 0, 1 | 1 | 3, 2, 2 | 1 | `UpdateTexture`, `DestroyTexture`, `QueryTexture` |
| `SDL_SetTextureBlendMode`, `ScaleMode`, `ColorMod`, `AlphaMod` | 4, 1, 2, 6 | | 1 | 3, 2, 1, 1 | 1, 1, 0, 0 | the same four setters |
| `SDL_RenderPresent`, `RenderSetVSync`, `GetRendererOutputSize` | 2, 1, 13 | 1 | | | 0, 0, 1 | `Present`, `SetVSync`, `OutputSize` |
| `SDL_CreateRenderer`, `DestroyRenderer` | 2, 1 | | | | | `CreateSdl2Painter` (or the software painter) |

The table is the whole 2D surface the player uses (about thirty calls, most of them one-liners). What a painter has to
do for the player: fill rects and draw outlines and lines with alpha blending and a clip rectangle; copy textures (RGBA8) scaled, flipped, rotated, tinted
(colour and alpha modulation) with nearest or linear sampling; draw coloured and textured triangles with a clip; present a frame and
pace it. `ui2d::Painter` (the interface the in-game UI pass already drew through) was exactly this set minus clear, present, output size, the draw
blend mode, float rect fill and line, and texture creation and upload, so the new interface extends it instead of repeating it.

### The interface and its two implementations

| | sdl2 painter | software painter (own) |
|---|---|---|
| Where | `engine/src/Platform/SdlPainter.cpp` | `okayui/soft/OkayUI_SoftPainter.cpp`, the pixel routines in `OkayUI_SoftRaster.hpp` (shared with the draw-list rasteriser of `OkayUI::soft::Render`) |
| Made by | `CreateSdl2Painter(window)`; `WrapSdl2Renderer(SDL_Renderer*)` for the editor and launcher splash and script bridge; `CreateSdl2SurfacePainter(w, h)` (SDL's software renderer on a surface, for tests) | `OkayUI::soft::CreateWindowPainter(window)`; `CreateBitmapPainter(w, h)` (headless, for tests and tools) |
| Draws with | `SDL_Renderer`, every method one SDL call | the CPU into an RGBA8 frame buffer that follows the window's size |
| Present | `SDL_RenderPresent` | `Window::PresentPixels`: X11 `XPutImage` (the visual's channel masks, `XSync` after, no shared memory), Win32 `StretchDIBits` (BGRX, top-down) |
| Frame pacing | the display (vsync) | with vsync asked for, waits for the next period of `Window::RefreshRateHz()` (60 Hz when unknown); `SetVSync` returns false because it is paced, not display-synced |
| Textures | `SDL_Texture`, RGBA | a pixel buffer with blend mode, scale mode (nearest or bilinear), colour and alpha modulation; unknown or freed pointers are ignored |

Shared by both, in `engine/src/Platform/Painter.cpp`: the path-keyed texture cache (`LoadTexture`, a miss is remembered), the TrueType glyph
atlas upload (`FontAtlas`) and the UI batcher's atlas pages (`UploadAtlas`).

OkayUI: the player keeps `OkayUI::EndFrame(SDL_Renderer*)` on the sdl2 painter. On a painter without an `SDL_Renderer` it calls
`EndFrameData(w, h)` (which now also builds the toast messages, as `EndFrame` did with the renderer's size) and `OkayUI::soft::FrameDrawer`
draws `GetDrawData()` through the painter interface: the glyph atlas and fallback-font atlases become painter textures and image
commands pass through as painter textures. `FrameDrawer` works on either painter, so the two can be compared.

The player (`player/main.cpp`): no `SDL_Renderer`, `SDL_Texture` or `SDL_Surface` is left in it (280 words became 0); SDL still supplies the `SDL_Rect`,
`SDL_Color`, `SDL_Vertex` data types, which have the same layout as the `ui2d` ones (`static_assert`ed). The texture cache the player kept
by hand moved into the painter. `shared/Splash.hpp` and `shared/OkayScriptUIBridge.hpp` draw through the painter; the editor and launcher keep
calling `DrawSplashStyled(SDL_Renderer*, ...)`, which wraps their renderer in the sdl2 painter.

### What differs on the software painter (known, not fixed)

* No GPU: everything is CPU work. A full-window frame of the 3D view plus UI is fast enough for a test run and slow in a heavy scene.
* Textures are sampled nearest or bilinear. No mip-maps and no anti-aliasing of triangle edges or rotated sprites, so rotated and minified images alias.
* Rounding: alpha blending and modulation round to nearest, SDL's software renderer truncates: up to 1 level of difference per channel.
* A rotated `Copy` samples each pixel centre (inverse mapping); SDL's renderers rotate differently at the edges.
* Texture size is limited to 16384 x 16384. The software painter has no render targets.
* OkayUI fallback fonts and `OkayUI::Image` work through the painter; a texture made by another painter is ignored.
* Not fixed here: X11 shared memory (`XShm`; plain `XPutImage` is used), damage tracking (the whole frame is sent each frame),
  16-bit and indexed X visuals (`PresentPixels` returns false), a big-endian X server, HiDPI on the own backend, Win32 run time (only
  syntax-checked with MinGW).

### Tests

| ctest name | What | Needs |
|---|---|---|
| `okayui_painter` | the same drawing on the software painter and on SDL's software renderer through the sdl2 painter, pixels checked against numbers worked out by hand: clear, rects (int and float), blend / replace, clip (also empty), outline, lines, points, 1:1 / scaled / sub-rect / flipped copies, colour and alpha modulation, texture blend modes, partial updates, flat and textured triangles, vertex colour, clip on geometry, gradient, logical scale; software only: bilinear, rotation, hostile input (NaN, out-of-range indices, huge rects, freed pointers). Then OkayUI's frame through `FrameDrawer` on both painters against the committed golden image (classic look: identical to the draw-list rasteriser) and, in the launcher look, against the rasteriser with a stated tolerance | nothing |
| `platform_painter_window` | the software painter on a real own-backend window: what it drew equals what the X server holds (`XGetImage`), through a resize and a frame smaller than the window; `PresentPixels` refuses bad input; the sdl2 painter and the software painter refuse the wrong kind of window; the pacing never runs faster than 60 Hz | Xvfb (skipped, exit 77, without) |

### What was verified (stage 3)

Linux x86-64, Release build, Xvfb 1024x768x24 + openbox, own `HOME`, Mesa software GL (`LIBGL_ALWAYS_SOFTWARE=1`), SDL's **software**
renderer for the sdl2 runs (`SDL_RENDER_DRIVER=software`, so there is no GPU on either side), xdotool input. "before" is the unchanged
`origin/main` `okay-player`, "after" is this branch with the sdl2 painter, "own" is this branch with `OKAY_PLATFORM_BACKEND=own`
(the software painter). The same scripted session in every run: start, a click, F3 (OkayUI performance panel), F1, a held `W`, F11
fullscreen and back, Esc. Scenes: `examples/node_playground/flow.okayscene`, `examples/visual_script/playground/gl_animation.okayscene`
and `gl_inventory.okayscene`, and a hand-made 3D scene (perspective camera, light, floor, two cubes, a sphere, an overlay text), run
again with the OkayUI test panels and toasts (`OKAY_TESTUI=1 OKAY_DEV=1`) and with scripted mouse and keyboard input on those panels.

* **sdl2 painter, before vs after: identical.** Whole-screen screenshots of the start, click, held key, fullscreen and windowed
  frames are pixel-identical (0 differing pixels) on the 3D scene, `gl_animation` and `gl_inventory`; on `flow` the same holds for the
  start and click frames. The only differences are in the live frame-statistics text of the F3 / F1 panels (frame rate digits; 170 to
  4,945 pixels inside the panel). With the OkayUI test panels open the remaining difference is the same statistics text
  (346 pixels). Window title, size, exit code 0 on Esc: identical.
* **The player runs on the own backend.** Log line `2D painter: software`, the scene loads, the game runs, input reaches it
  (mouse, drag, text typed into an OkayUI field, F3, F11), exit code 0 on Esc, in every scene above. In a scripted session on the
  OkayUI test panels the own run and the sdl2 run end in the same state (Settings tab open, Hard selected, volume slider dragged, name
  typed). Toasts, the F3 panel, the glyph atlas text, rounded panels with shadows, the 3D frame copy and sprite/text overlays all
  draw. The screenshots are not committed (they are 1024x768 whole-screen captures).
* **own vs sdl2 (software renderer) differences, honestly.** The 3D frame and the sky gradient are identical (0 of 226,200 pixels
  differ in a background crop of `flow`; on the 3D scene the interior of every face is identical). What differs: (1) text and
  UI shapes drawn as triangles: 0.1 to 0.9 % of the screen (842 pixels on `gl_animation`, 2,115 on the 3D scene, 6,855 on `flow`;
  max channel difference up to 170) because SDL's software renderer places triangle vertices differently (about one pixel right/down
  of ours and with dropped pixels at some glyph and rounded-rectangle edges; our picture has no such artefacts, a GPU renderer
  would sample pixel centres like ours does); (2) the F3 panel contents differ in the numbers and in one extra statistics line that
  depends on timing; (3) on the 3D scene 510 pixels on the silhouette edges of the cube, sphere and floor: those come from the
  GL renderer's edge smoothing, which runs on the own backend's GLX context here and the sdl2 backend's context there (cause not
  isolated; the painter only copies the finished frame, which its tests pin pixel-exact).
* **Speed (software only, no GPU).** The F3 panel on `flow`: about 36 frames/s on own against about 44 on sdl2 with SDL's software renderer
  (this machine was also running other work, so treat it as a rough ratio). A 960x600 window needs a few milliseconds per full-window layer.
* **Unit and integration tests** (`ctest -R "okayui|platform"`, 12 tests, all pass in a real CMake build): see the table above, plus the
  existing okayui tests (soft, behaviour, golden, drawlist, fuzz) unchanged by the shared-rasteriser refactor. The painter test ran
  clean under AddressSanitizer + UBSan (128 checks). `x86_64-w64-mingw32-g++ -fsyntax-only -Wall -Wextra -Wpedantic` is clean on
  `OwnWin32.cpp` (including `PresentPixels`), `Painter.cpp` and the software painter.
* **Not verified:** a real GPU or real desktop (only Xvfb + openbox), Windows at run time (`StretchDIBits` never ran), HiDPI,
  typing in a non-ASCII layout, the editor and launcher splash screens (their call sites compile through the wrapper but the
  apps were not built or run), the web build, and long sessions (memory growth of the frame buffer was not measured).

## Stage 4: window state, focus and pointer questions, and a bridge for windows the apps make themselves

`docs/sdl_inventory.md` (generated from the source by `scripts/sdl_inventory.py`) counts what the launcher, editor and player still ask SDL directly, and
`docs/sdl_removal_plan.md` is the plan built on it. This stage adds the smallest of the functions the seam was missing and the one structural blocker the
inventory found: **the apps keep a raw `SDL_Window*`, and every `Window` method needs a `Window` object, which only `Backend::MakeWindow` makes.** So no
`Window` method could be called on the launcher's or the editor's windows at all (60 call sites wait for that). `WrapSdl2Window` is the bridge.

Written from the OS documentation (the Xlib manual, the ICCCM and EWMH property names, the Win32 documentation) and from what the apps ask. No SDL source and
no other project's code was read or followed (CLAUDE.md rule 19a). Nothing is switched on by default: the sdl2 backend answers with the SDL call the app made
before, and the own backends are still opt-in.

| API (`Platform.hpp`) | The app asked | sdl2 | own Linux (X11) | own Windows (Win32, compile-checked only) |
|---|---|---|---|---|
| `WrapSdl2Window(void*)` | (no SDL word) | a non-owning `Window` over an `SDL_Window*`; destroying it never destroys the SDL window; a GL context made through it is its own | n/a | n/a |
| `Window::IsMinimized()` | `SDL_GetWindowFlags & SDL_WINDOW_MINIMIZED` (launcher, 6 sites) | the flag | the window manager's word, read from the server when asked: ICCCM `WM_STATE` = IconicState, or EWMH `_NET_WM_STATE_HIDDEN`. With no window manager (Xvfb) nothing minimises a window | `IsIconic` |
| `Window::IsVisible()` | `... & SDL_WINDOW_HIDDEN` (launcher) | no HIDDEN flag. Under X11 SDL also sets HIDDEN on a window the window manager minimised (seen with the test's fake window manager), so a minimised window reports not visible there | tracked: false after `Hide()` or when created hidden, true after `Show()` or `Restore()`; a minimised window stays visible | `IsWindowVisible` (a minimised window keeps `WS_VISIBLE`) |
| `Window::HasMouseFocus()` | `... & SDL_WINDOW_MOUSE_FOCUS` (launcher) | the flag | `EnterNotify` / `LeaveNotify` (current once events are read) | the window under the cursor (`WindowFromPoint`) |
| `Window::SetInputFocus()` | `SDL_SetWindowInputFocus` (editor, 2) | same call; true on success | `XSetInputFocus`; false when the server refuses (the window is not mapped) | `SetForegroundWindow` + `SetFocus`; true when the window has the focus afterwards |
| `Backend::KeyboardFocusWindowId()` | `SDL_GetKeyboardFocus` (launcher 1, editor 1) | the window's id, 0 for none | `FocusIn` / `FocusOut` (already tracked) | `GetFocus` if it is one of ours |
| `Backend::MouseFocusWindowId()` | `SDL_GetMouseFocus` (editor) | same | `EnterNotify` / `LeaveNotify` | `WindowFromPoint` + its root window |
| `Backend::IsRelativeMouse()` | `SDL_GetRelativeMouseMode` (editor, 2) | same | what `SetRelativeMouse` last set | same |
| `Backend::GlobalMouseState(x, y)` | `SDL_GetGlobalMouseState` (launcher) | same call. Measured under Xvfb: it follows the pointer only over SDL's windows and only after events were read (position and buttons were stale until `SDL_PumpEvents`) | `XQueryPointer` on the root window: live, anywhere on the desktop | `GetCursorPos`; buttons by `GetAsyncKeyState`, swapped when the user swapped the mouse buttons |
| `Backend::PumpEvents()` | `SDL_PumpEvents` (launcher, editor) | same | reads what the server sent into the queue (`PollEvent` still returns every event) | the message loop of `PollEvent`, without taking an event |

What each answer needs: the window manager's and the pointer's answers (`IsMinimized`, `HasMouseFocus`, both focus ids, and on sdl2 `GlobalMouseState`) are current once the app has read its
events (`PollEvent` or `PumpEvents`), as with SDL. `Raise()` does not promise the keyboard focus on sdl2 (measured: after another window took and lost it, SDL did not give it back); `SetInputFocus()` does.

### Call sites moved (launcher only; `editor/main.cpp` is edited by other agents and was not touched)

Five sites in `launcher/main.cpp`, each with the behaviour of the default sdl2 backend: the chat window's "is it focused" and "is it minimised" asks (through a `WrapSdl2Window` view kept
in `ChatWindowHost::pw`), the "does any of our windows have the keyboard focus" ask that marks a conversation as being looked at, the event pump while the start-up sign-in check runs, and the pointer
position on the whole desktop when a file is dropped on the chat panel. The launcher's windows are SDL windows, so these asks go to the **sdl2** backend explicitly (`SdlSeam()`), whatever
`OKAY_PLATFORM_BACKEND` says (the own backend does not know SDL's windows). The inventory counts this change as -5 SDL calls in the launcher (743 to 738 calls; the numbers in `docs/sdl_inventory.md`
are pinned to the commit before this change).

### Tests

`test_platform_state` (ctest `platform_state_sdl2`, `platform_state_own`; Xvfb and xdotool, skipped without them): hide, show, created hidden; the pointer moved in and out by xdotool for
`HasMouseFocus` and `MouseFocusWindowId`; `PumpEvents` alone makes the pointer state current and loses no event; `GlobalMouseState` against xdotool positions and buttons;
`KeyboardFocusWindowId`, `SetInputFocus` between two windows, a hidden window refused; `IsRelativeMouse` through `SetRelativeMouse`; `IsMinimized` with the test playing the window manager
(`tests/platform_fakewm.cpp` sets `_NET_WM_STATE_HIDDEN` and `WM_STATE` through dlopen'd libX11, because Xvfb has no window manager); on sdl2, a raw `SDL_CreateWindow` window wrapped with `WrapSdl2Window`
and compared with `SDL_GetWindowFlags` in every state, and the wrapper's destruction leaving the window alive. The test was written first and failed (see below), then the code was written.

### What was verified (stage 4)

Where: Ubuntu 24.04, g++ 13.3.0, SDL 2.30.0 (`libsdl2-2.0-0 2.30.0+dfsg-1ubuntu3.1`), Xvfb 1024x768x24 **with no window manager**, xdotool 3.20160805.1. One
compile at a time (a Release tree for the runs, an `-O1` tree for the sanitizers). Nothing here ran on Windows or on a real desktop.

| What was run | Result |
|---|---|
| Tests first: `d38725ff` holds the test and the new functions as do-nothing defaults | `test_platform_state` fails on purpose: sdl2 **20 of 57** checks, own **19 of 57** |
| First run with the code (`29efc1a9`) | own 57 checks, **0 failures**; sdl2 **5 of 77 failed** (see below: all five are what SDL does on X11, not the new code) |
| After the test and comment fixes (`02fa0a34`): `ctest -R platform_state` | `platform_state_sdl2` **76 checks, 0 failures**; `platform_state_own` **58 checks, 0 failures**. Three more runs of each, the same: no flakiness seen |
| The whole platform set in the same tree: `ctest -R '^(platform\|platform_default_env_.*\|platform_window_.*\|platform_state_.*)$'` (the five older tests and the two new ones; `platform_painter_window` and `platform_gamepad` were not built in this small tree) | 7 of 7 passed |
| ASan + UBSan (`-fsanitize=address,undefined -fno-sanitize-recover=undefined`, `-O1`) on `test_platform_state` | Both backends: no ASan or UBSan error, 76 / 0 and 58 / 0 with `ASAN_OPTIONS=detect_leaks=0`. With leak detection on, own is clean; sdl2 reports one leak of 1032 bytes made inside libdbus, called from `SDL_Init` (SDL's own start-up; no function of ours is on that stack) |
| MinGW `x86_64-w64-mingw32-g++-win32` (GCC 13, the "win32" thread model the Windows build uses), `-std=c++17 -fsyntax-only -Wall -Wextra -Wpedantic -DOKAY_PLATFORM_HAVE_OWN` | `OwnWin32.cpp` and `Platform.cpp`: no warning, no error. Syntax only: nothing was linked, **none of the Win32 code ran** |
| `launcher/main.cpp` with `g++ -std=c++17 -fsyntax-only` and the launcher's include paths, before and after the five conversions | No error either way (about 6 s each). A control with a made-up function name at a converted line did fail, so the check does reach the converted lines. **The launcher was not built, linked or run** |
| `python3 scripts/tests/test_sdl_inventory.py` (ctest `sdl_inventory_script`) | 12 tests, OK |

The five sdl2 failures of the first run, and what SDL 2.30.0 really does (found with a small probe program against SDL under Xvfb; the test now expects exactly this):
* `SDL_GetGlobalMouseState` gives the old position and button state until events have been read (`SDL_PumpEvents`), and it does not follow the pointer outside SDL's own windows
  (2 checks: the button mask while a button is held, the position over bare desktop). The own backends ask the X server directly and are exact.
* `SDL_RaiseWindow` alone does not give the keyboard focus back to a window after another window took it and lost it; `SDL_SetWindowInputFocus` does (2 checks: the focus id, `IsFocused()`).
  The test now calls `SetInputFocus()` first, which is what the editor calls.
* SDL on X11 also sets `SDL_WINDOW_HIDDEN` on a window the window manager minimised, so on sdl2 a minimised window is not "visible" (1 check). The own backends keep it visible;
  `IsVisible()` of a minimised window is therefore not promised (see the header comment).

**CI caveat.** No CI job installs xdotool (the "Build & test (Ubuntu)" job installs neither Xvfb nor xdotool; the "own-stack" job installs Xvfb only, and it picks up every `test_platform*` target by name,
so it builds this test too). So `platform_state_*`, like the older `platform_window_*`, reports "skipped" (exit 77) in CI. The numbers above are from a run by hand on a machine that has both.
Adding `xvfb xdotool` to the "Build & test (Ubuntu)" job's `apt-get install` line in `.github/workflows/ci.yml` would make CI run them; that was not done here.

### Known differences and what is not done

* The own X11 `IsMinimized()` reads the window manager's properties; a window manager that sets neither `WM_STATE` nor `_NET_WM_STATE_HIDDEN` on a minimised window is not noticed.
  A window hidden by another client (not by `Hide()`) is not noticed by `IsVisible()` either (it tracks what the app asked, as `SDL_WINDOW_HIDDEN` does).
* The own X11 `PumpEvents()` does not poll game-pad hot-plug (that is done in `PollEvent`).
* Not written (still GAPs in the inventory): the monitor list and DPI, the flash request, cursor shapes, IME composition events, drop begin and complete, the current GL window and context, `SDL_GetWindowWMInfo`
  for the tray icon, exclusive video modes, render targets. `claude/platform-gaps2` has versions of some of them (see the inventory, section 4).
* `WrapSdl2Window` is transitional: it goes away when the launcher and editor create their windows with `Backend::MakeWindow` (step 6 of `docs/sdl_removal_plan.md`).

## Not behind the seam yet (stage 1 list; see stage 2 above for what changed)

The window, event loop and GL context of the editor, launcher and player still call SDL directly. Moving them needs more than
a search and replace: `SDL_Renderer` (330 calls, the whole `okayui` toolkit, every golden-image test) and Dear ImGui's
`imgui_impl_sdl2` / `imgui_impl_sdlrenderer2` both require an `SDL_Window*`, so a window that is not an SDL window cannot
draw anything in those apps today. The next steps are an own software/GL 2D painter behind `UIPainter` and an own ImGui
platform backend (the roadmap's ImGui item); only then can `OKAY_PLATFORM_BACKEND=own` drive the apps' windows. Until then
`own` changes the timers, audio devices and controllers the apps use, and is the full platform for the tests and for new
code that does not need `SDL_Renderer`.

## The own backend

### Linux (written, tested under Xvfb)

* **Windowing: X11 loaded with `dlopen("libX11.so.6")`, nothing linked.** `libX11` is not a build dependency of this repo
  (SDL2 loads it at run time itself), so linking it would add one to every Linux build. The X11/GLX header files are used
  for types and constants only (`check_include_file_cxx`); without them the own backend builds with no windowing and says so.
  Raw Xlib rather than XCB: XCB would have no text-input story without more libraries (`xkbcommon`), and Xlib has
  `XFilterEvent`/`Xutf8LookupString` for input methods.
* Window create, destroy, resize (+ events), title (`_NET_WM_NAME`, UTF-8), position, min size, show/hide, raise (+ input
  focus, so it works with no window manager), minimize, fullscreen (`_NET_WM_STATE`, needs a window manager), `WM_DELETE_WINDOW`.
* Events: keys with the physical (evdev -> HID) layout and auto-repeat detection, text input through an input method with a
  keysym fallback, mouse buttons (incl. double-click counts), wheel, motion, relative mouse (grab + warp), cursor hiding.
* **GLX** (`libGL.so.1` through `dlopen`): FBConfig visual, `glXCreateContextAttribsARB` (3.2 compatibility profile, as the
  engine asks) with the legacy path as fallback, swap, swap interval, `glXGetProcAddressARB`.
* Monotonic timers (`CLOCK_MONOTONIC`), `nanosleep`.
* **Clipboard**: owns the `CLIPBOARD` selection and answers `SelectionRequest` while events are polled; reads other clients' text via `ConvertSelection`.
* **Audio: ALSA through `dlopen("libasound.so.2")`**, no ALSA headers (only `snd_pcm_open/set_params/writei/readi/recover` are
  declared by hand). One thread per device, queue and callback mode, playback and capture, device listing. PulseAudio is
  not used: ALSA's `default` device is routed to PulseAudio / PipeWire on desktops that run them. `OKAY_ALSA_DEVICE` overrides the device name.
* **Controllers: evdev** (`/dev/input/event*`, kernel header `linux/input.h` only): detection by `BTN_GAMEPAD` + `ABS_X`,
  Xbox-style mapping, normalised axes, triggers from axes or `BTN_TL2/TR2`, d-pad from hat or buttons, force-feedback rumble, hot-plug rescan once a second.

Not written for Linux: Wayland (X11 only; under Wayland it runs through XWayland), XDND file drop (`EventType::DropFile`
never fires), touch, DPI / multi-monitor queries, window icons, IME composition display, PulseAudio-native or PipeWire-native audio.

### Windows (compiles, untested)

`OwnWin32.cpp`: Win32 window and input, `WM_CHAR` text, WGL context, Win32 clipboard, `QueryPerformanceCounter`, XInput pads,
`WM_DROPFILES`. **Only syntax-checked with MinGW (`-fsyntax-only`) on Linux. Never run on Windows.** No audio device
(`OpenAudio` returns null).

### macOS (not written)

Needs Objective-C++ (Cocoa, NSOpenGLContext/Metal, CoreAudio, GameController). It cannot even be syntax-checked with the
MinGW cross-compiler, so nothing was written rather than writing code that was never compiled.

## Tests

| ctest name | What | Needs |
|---|---|---|
| `platform` | registry, key numbering vs SDL scancodes, timers (both backends), `BasePath` (equals the executable's directory), `PrefPath` (created, writable, under `$XDG_DATA_HOME`), `PreferredLocales` (own parsing of `$LANGUAGE` / `$LC_ALL` is exact), error box / DPI on own report "not supported", the US `KeyForChar` table, audio devices on both backends (queue, clear, pause, callback, capture; SDL dummy driver / ALSA `null` device), controllers enumeration, own evdev mapping fed through a pipe | nothing; audio parts print `SKIP` when no device can be opened |
| `platform_window_sdl2`, `platform_window_own` | create / resize / title, GL clear colour read back with `glReadPixels` (and for `own`, the window's pixels read back from the X server with `XGetImage`), keyboard, modifiers, text input (incl. a non-ASCII character), mouse buttons / wheel / motion, relative mouse, clipboard (incl. a second X client reading ours and vice versa). Stage 2 added: `KeyForChar` on the US and the German layout (z and y swap places), `ModState`, `AppendExtendedKeys` (F-keys, Tab, navigation, left/right modifiers) with keys held through xdotool, `RelativeMouseState` sums (12,7 then 0,0 then -5,3), `IsCursorShown`, fullscreen at creation and toggled, maximised, `SetIcon` (valid and invalid), refresh rate, base path; on sdl2 also touch events and flipped / normal wheel events pushed as SDL would | Xvfb, `xdotool` (XTEST input). Skipped (exit 77) when either is missing |
| `platform_default_env_sdl2`, `platform_default_env_own` | `OKAY_PLATFORM_BACKEND` in the environment overrides the build's default backend (`test_platform --print-default`) | nothing |
| `platform_state_sdl2`, `platform_state_own` | stage 4: window state (`IsMinimized`, `IsVisible`, `HasMouseFocus`, `SetInputFocus`), focus and pointer queries (`KeyboardFocusWindowId`, `MouseFocusWindowId`, `IsRelativeMouse`, `GlobalMouseState`, `PumpEvents`), and on sdl2 `WrapSdl2Window` compared with `SDL_GetWindowFlags`; the test plays the window manager (`tests/platform_fakewm.cpp`) | Xvfb, `xdotool`. Skipped (exit 77) when either is missing |

Also `platform_gamepad`: the controller -> `Input` bridge (`PlatformGamepad.hpp`) driven by a fake `Gamepad`.

Run just these: `ctest -R platform`. `tests/run_under_xvfb.sh <program> [args]` starts a private Xvfb on a free display (and exits 77 when Xvfb is missing).

## What has and has not been verified

### Stage 2 (the player on the seam)

Linux x86-64, Xvfb 1024x768x24 + openbox, own `HOME`, Mesa software rendering (`SDL_RENDER_DRIVER=software`), xdotool input.
The "before" player is the unchanged `origin/main` `player/main.cpp` built against the same libraries; "after" is this branch.
Both ran `examples/node_playground/flow.okayscene` with the same scripted session (start, F3 stats overlay, F1 test panel,
mouse click on the Match and Settings tabs, typing, wheel, held W, F11 fullscreen and back, Esc):

* window title, size (960x600) and position, exit code 0 on Esc, the save folder created, the dev log lines: identical.
* screenshots of the start frame, the held-key frame, the F11 fullscreen frame (window becomes 1024x768 at 0,0) and the return to
  windowed (960x600 at the same place): pixel-identical. The panels that show live frame statistics (F3, F1) differ only in the fps
  numbers and the spinner; a click on the Settings tab switches the panel the same way in both.
* `--server` (headless, no `DISPLAY`): identical log, same exit.
* `OKAY_PLATFORM_BACKEND=own`: the player gets through platform init, base path, user folder, locales, anti-cheat decision, scene
  load and creates its X11 window, then stops with "the player draws through SDL_Renderer, which needs the sdl2 platform backend ... has no 2D painter yet" and exit code 2.
  It does not get further: there is nothing to draw 2D on an own window yet.
* `ctest -R platform`: `platform`, `platform_window_sdl2`, `platform_window_own`, `platform_default_env_*`, `platform_gamepad` pass.

Not verified in stage 2: typing into an OkayUI text field in the running player (the scene has none; the text event path is covered by
`platform_window_*`), touch input on a real touch screen, natural-scrolling mice, an AZERTY keyboard in the running player (`KeyForChar`
is tested on the German layout), fullscreen on a real desktop other than openbox, Windows and web builds, and `-DOKAY_PLATFORM_BACKEND=own` as a *build* (the run-time
environment variable was used instead; the build option only changes which backend `Default()` returns).

### Stage 1

Verified (Linux x86-64, Xvfb 1024x768x24, Mesa llvmpipe software GL, xdotool/XTEST for input):
the whole `platform`, `platform_window_sdl2` and `platform_window_own` tests pass, repeatedly; the GL clear colour is read
back with `glReadPixels` on both backends and, for `own`, from the X server's copy of the window (`XGetImage`); keys, modifiers,
ASCII and German-layout text, mouse, wheel, relative motion and the clipboard between two X clients all arrive as events through
the real server input path. `Win32` backend: `x86_64-w64-mingw32-g++ -fsyntax-only -Wall -Wextra -Wpedantic` is clean.
The editor, launcher and player sources were compile-checked (`-fsyntax-only`) after each change; the engine library was
not fully built and linked in that session (the machine was shared and a full build takes hours), so the three apps
were **not linked or run** on top of the new layer.

Not verified: real audio hardware (only SDL's dummy driver and ALSA's `null` device), real game controllers (no
`/dev/input` here; the evdev mapping is tested through a pipe, rumble and hot-plug are not), Wayland, window-manager behaviour
(fullscreen, minimise, the frame offset of window positions: Xvfb has no window manager), HiDPI, multi-monitor, a real input method
(the text path is tested with the keysym fallback), Windows at run time, Emscripten, a static-SDL Windows build.
