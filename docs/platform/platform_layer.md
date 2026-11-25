# The platform layer (`okay::platform`) and our own backend

> **What this is:** Stage 1 of replacing SDL2 with our own code (see the roadmap: seam first, own backend side by side and non-default, SDL2 stays).
>
> **Status:** Reference page for this feature. Kept as written; not extended (rule 5).
>
> **Last checked:** 2026-11-20 (moved into `platform/`, paths and links checked; the text itself was not re-checked against the code).

Stage 1 of replacing SDL2 with our own code (see the roadmap: seam first, own backend side by side and
non-default, SDL2 stays). This page is the audit of what the engine takes from SDL2, what is behind the
seam now, what is not, and what was and was not tested.

Header: `engine/include/okay/Platform/Platform.hpp`. Sources: `engine/src/Platform/`. CMake: `cmake/OkayPlatform.cmake`.

Current counts, file by file, and what covers each SDL use: `docs/sdl_inventory.md` (generated). The ordered plan to get the editor and
launcher off SDL2: `docs/platform/sdl_removal_plan.md`.

## Choosing a backend

| | |
|---|---|
| `-DOKAY_PLATFORM_BACKEND=own` | **default on Linux since 2026-11-25** (round 15). `okay::platform::Default()` is our own backend. SDL2 is still linked (the sdl2 backend stays as the way back). |
| `-DOKAY_PLATFORM_BACKEND=sdl2` | default on Windows (the Win32 backend has never run on real Windows) and the web (no own backend). `Default()` is the SDL2 backend. |

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
| **Threads, mutexes, atomics** | `SDL_CreateThread` 60, `DetachThread` 47, `WaitThread` 17, `Create/Lock/UnlockMutex` ~370, `SDL_Atomic*` ~30 | launcher (downloads, chat, voice), `VoiceChat.hpp` | **not touched.** Not a platform feature in the roadmap's list. It cannot simply become `std::thread` and `std::mutex`: the Windows programs are built with MinGW's "win32" thread model, which cannot compile `<thread>`, `<mutex>`, `std::condition_variable` or `std::call_once` (`CLAUDE.md` rule 21a). It needs our own thread, mutex and condition wrapper (Win32 calls on Windows, pthreads on Linux), separately |
| **Misc** | `SDL_Log` 9, `SDL_GetBasePath` 4, `SDL_GetPrefPath`, `SDL_GetPreferredLocales`, `SDL_ShowSimpleMessageBox` 3, `SDL_SetHint` 7, `SDL_GetWindowWMInfo` + `SDL_SetWindowsMessageHook` (Windows tray icon), `SDL_sinf`-style math, `SDL_free` | all apps | not touched |
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
| window (create, size, position, fullscreen, icon, min size, flash, flags) | 49 | 84 | 37 -> 2 | 2 | 0 | 0 | 0 | HAVE. Added for the player: fullscreen/maximised at creation, `IsFullscreen`, `SetIcon`, `RefreshRateHz`. Round 6 moved the minimised / mouse-focus / input-focus queries and the tray HWND. |
| GL context | 33 | 0 | 21 -> 0 | 0 | 0 | 0 | 0 | HAVE (create, make current, swap, interval, proc address). Editor still on SDL |
| events | 30 | 18 | 24 -> 0 | 0 | 0 | 0 | 13 | HAVE (quit, window close/focus/resize, keys, text, mouse, wheel, file drop, controller hot-plug). Added: touch (`FingerDown/Motion/Up`, sdl2 only) and `wheelFlipped`. IME composition is HAVE on own Win32 (`Win32Ime.hpp`: preedit, caret, commit, cancel, focus loss, clause ranges in the model and in `Event::clauses` (empty on sdl2); own X11 has no IME, `XIMPreeditNothing`). GAP: `SDL_RENDER_*_RESET` (D3D11 device loss), Windows `SDL_SysWMEvent` |
| keyboard, text input, mouse state | 75 | 2 | 58 -> 0 | 0 | 16 -> 1 | 1 | 20 | HAVE. Added: `RelativeMouseState`, `ModState`, `KeyForChar` (layout-aware), `IsCursorShown`. `Input/SdlKeys.hpp` is replaced by `Input/PlatformKeys.hpp` |
| timers | 1 | 0 | 0 | 0 | 0 | 0 | 0 | HAVE, moved in stage 1 |
| audio device | 1 | 12 | 1 -> 0 | 0 | 0 | 0 | 0 | HAVE (queue and callback, playback and capture). `SDL_LoadWAV` was deleted on 2026-11-23 (#1187): our own WAV decoder is the only WAV reader |
| game controller | 2 | 0 | 2 -> 0 | 0 | 0 | 0 | 0 | HAVE. The editor's remaining 2 words are its `SDL_CONTROLLERDEVICEADDED/REMOVED` event checks |
| clipboard | 0 | 0 | 0 | 0 | 0 | 0 | 0 | HAVE; the apps use ImGui's clipboard, nothing calls ours yet |
| cursors (system cursor shapes) | 0 | 0 | 0 | 0 | 0 | 0 | 0 | HAVE (round 10: `CursorShape` / `Backend::SetCursorShape` on the SDL2, own X11 and own Win32 backends; tables in `Platform/CursorMap.hpp`, tested in `platform_xdnd`; Win32 part unrun on Windows) |
| message box | 2 | 0 | 4 -> 0 | 0 | 0 | 0 | 0 | Added `ShowErrorBox`: sdl2 and Win32 real; own Linux returns false and logs (no toolkit to draw one with) |
| display / DPI | 5 | 4 | 4 -> 0 | 0 | 0 | 0 | 0 | Added `SetPerMonitorDpiAware` (sdl2 hint, Win32 API; nothing elsewhere) and `Window::RefreshRateHz` (sdl2 only). First-run DPI moved (round 6). HAVE since: `Backend::DisplayAt` (bounds, work area) on SDL2 and own Win32, pure maths in `Platform/Win32Display.hpp`, used by the launcher and `OkayUIHostRun`. |
| misc runtime (init, log, paths, locales, hints, `SDL_free`, syswm) | 48 | 17 | 37 -> 3 | 2 | 0 | 25 | 5 | Added `BasePath`, `PrefPath`, `PreferredLocales`, `Log`. GAP: hints other than DPI. `SDL_SaveBMP`: own `okay::platform::WriteBmp32` (`engine/include/okay/Platform/BmpWriter.hpp`, test `ctest -R bmp_writer`, written from the BMP file format) now writes the editor's `--vs-shot` pictures and, through `EncodeBmp32Bgra/WriteBmp32Bgra` (SDL surface rows with a pitch, test `ctest -R platform_bmp_writer`), the dev tools `tools/input_shots.cpp`, `tools/project_menu_shots.cpp`, `tests/modeling_easy_shot.cpp` and `tests/modeling_advanced_shot.cpp` |
| threads, mutexes, atomics | 0 | 705 | 0 | 0 | 0 | 0 | 0 | Not a platform-layer feature. Not `std::thread` either: MinGW's win32 thread model cannot compile `<thread>` or `<mutex>` (rule 21a), so it needs our own wrapper, separately (launcher only) |
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

## Round 4: a small batch of leftover SDL calls moved (editor, player)

Found with `grep` (per-file counts are in `docs/sdl_inventory.md`, section 5): calls that were still direct SDL although the seam had the
feature. Moved, SDL2 still the default backend so nothing changes there:

* `shared/SdlClipboard.hpp` became `shared/PlatformClipboard.hpp` (Play mode and player clipboard: `SDL_GetClipboardText`, `SDL_SetClipboardText`, `SDL_free`).
* `Gestures::Tick` in the player and the editor: `SDL_GetTicks64()` / `SDL_GetTicks()` with a version `#if` became `okay::platform::TicksMs()`.
* Editor `g_lastSavedAt = SDL_GetTicks64()` became `okay::platform::TicksMs()`. It was **compared with `okay::platform::TicksMs()`** two other
  places, so on the own backend (a different zero point) the "Saved" toast would have shown or vanished at the wrong time.
* Editor `SDL_GetBasePath()` (two places: default projects folder and the launcher message files) became `Backend::BasePath()`.

Checked: `player/main.cpp` passes `g++ -fsyntax-only`. **`editor/main.cpp` was not compiled** (48,000 lines, not built here): the edits are
the five lines above and follow the player's pattern. Not moved: `SDL_ShowSimpleMessageBox` in the editor start-up check (a warning icon,
the seam's box is an error box), touch-device count, the renderer, ImGui backends, threads.

## Round 5: window-bound calls, taskbar flash, touch count, GL current state (editor, launcher)

New seam pieces (own code, written from the Microsoft `FLASHWINFO` documentation and from our own needs; nothing taken from SDL):

* `okay::platform::WrapSdl2Window(void*)`: a non-owning `Window` over an `SDL_Window*` the app made itself. It is the bridge that lets window
  calls move before windows are created by `Backend::MakeWindow`. It destroys nothing but a GL context made through it. Temporary, goes with SDL2.
* `Window::Flash(FlashMode::Stop / Briefly / UntilFocused)`. sdl2: `SDL_FlashWindow` (guarded by the SDL 2.0.16 version check). Own Windows:
  `FlashWindowEx` with the plan from `win32disp::PlanFlash` (briefly = caption and taskbar, 3 flashes; until focused = those plus
  `FLASHW_TIMERNOFG`; stop = 0). Own Linux: not done (default returns false; the urgency hint is on `claude/platform-gaps2`).
* `Backend::TouchDeviceCount()` (sdl2 only; own backends say 0 because they never produce finger events).
* `Backend::CurrentGL()` / `RestoreGL()` with an opaque `GLCurrent` (the window and context pointers of the backend): the save and
  restore the editor does around its hidden GPU-renderer context.

Moved, SDL2 still the default (behaviour identical):

* Launcher: `SDL_RestoreWindow / RaiseWindow / ShowWindow / HideWindow / MinimizeWindow`, `SDL_Get/SetWindowPosition`, `SDL_Get/SetWindowSize`,
  `SDL_SetWindowMinimumSize`, `SDL_GetWindowID`, the chat window's focus test, and both `SDL_FlashWindow` calls now go through small `lw::` helpers
  that wrap the window per call.
* Editor: window size, id and focus queries on the main window (`winSeam`), the display refresh rate (`SDL_GetWindowDisplayMode`), relative mouse
  motion (`SDL_GetRelativeMouseState` through `Backend::RelativeMouseState`), `SDL_GetNumTouchDevices`, and the hidden GL context of the GPU renderer
  (context creation, make current, save and restore, `SDL_GL_GetProcAddress`) through the wrapper and the sdl2 backend.

Left on SDL on purpose: `SDL_GetWindowFlags` minimised / hidden queries (no `Window` method yet), `SDL_GetKeyboardFocus` and friends,
`SDL_GetWindowWMInfo` and the Windows message hook (tray icon), `SDL_GetWindowDisplayIndex` + `SDL_GetDisplayDPI` for the editor's first-run UI scale
(`Window::Dpi()` rounds to a whole DPI and treats an absurd value as 96, which could differ by a quarter step in rare cases), `SDL_GetWindowFromID`
(ImGui viewports), the windows' creation, and the `SDL_Renderer`.

Tests: `test_platform_win32_pure` (flash plan), `platform_window_sdl2` (wrapper identity with SDL, non-owning, refresh rate, touch count, flash calls,
GL current save / restore through the wrapper). Try it (rule 4a): nothing visible changes; in the launcher, minimise it and let a chat message or
an update notice arrive, and the taskbar button flashes as before; in the editor, Play with mouse look and the GPU view work as before.

## Round 6: window state, focus queries, tray handle, first-run DPI (editor, launcher)

Written from our own needs and the Microsoft docs for `IsIconic`, `IsWindowVisible`, `GetFocus`, `WindowFromPoint`, `GetAncestor`, `SetForegroundWindow`
and `SetFocus`, and from the EWMH `_NET_WM_STATE_HIDDEN` state on X11 (specifications only; nothing taken from SDL).

* `Window::IsMinimized()`, `IsVisible()`, `HasMouseFocus()`, `HasInputFocus()` (same as `IsFocused()`), `SetInputFocus()` (ask for the keyboard
  without raising) and `Win32Handle()` (the HWND for the tray icon; null off Windows).
* `Backend::KeyboardFocusWindowId()`, `MouseFocusWindowId()` (0 = none of ours) and `NativeHandleFromId(id)` (window from id).
* All three backends: sdl2 (`SDL_GetWindowFlags` / `SDL_GetKeyboardFocus` / `SDL_GetMouseFocus` / `SDL_SetWindowInputFocus` / `SDL_GetWindowFromID` /
  `SDL_GetWindowWMInfo`), own Windows (`IsIconic`, `IsWindowVisible`, the window under the cursor, `SetForegroundWindow` + `SetFocus`, the HWND itself),
  own Linux (Enter/Leave events, `_NET_WM_STATE_HIDDEN`, map state, `XSetInputFocus`; `Win32Handle` null).
* Pure decisions in `okay/Platform/WindowLogic.hpp`, tested in `platform_win32_pure`: `UiScaleFromDpi` (first-run UI scale), `ShouldReclaimInputFocus`
  (the editor hands the keyboard back after a pop-out closes), `IsActiveForFrameRate`, `NeedsAttentionFlash`, `IsBackgroundWindow`.
* Moved (SDL2 still the default, behaviour the same): the editor's `SDL_GetWindowFromID` (pop-out viewports), first-run `SDL_GetDisplayDPI`,
  `SDL_GetKeyboardFocus` / `SDL_GetMouseFocus` / `SDL_SetWindowInputFocus` (twice); the launcher's `SDL_GetWindowFlags` (7 places),
  `SDL_GetKeyboardFocus` and `SDL_GetWindowWMInfo` (tray HWND, so `SDL_syswm.h` is no longer included).
* Difference: the first-run DPI now goes through `Window::Dpi()`, which is a whole number and 96 when SDL cannot tell (the old code used the float
  and left the scale alone on failure; the scale is 1.0 on a fresh install either way).
* Verified: `platform_win32_pure` and `platform_window_sdl2` / `_own` under Xvfb (no window manager there, so minimize is not exercised); the launcher
  and editor `main.cpp` compile on Linux; `OwnWin32.cpp` passes a MinGW syntax check. Unverified: the Windows paths (no Windows machine here),
  and the own-Linux minimized/mouse-focus answers under a real window manager.

## Round 7: window and platform start-up/shut-down creation moved onto the seam (editor, launcher; the player was done in stage 2)

Written from our own needs; no new platform code (the SDL2 backend already had `Init`, `MakeWindow`, `CreateGLContext`, `Shutdown`).

* Editor: `SDL_SetHint(DPI)`, `SDL_Init`, `SDL_CreateWindow` (main window and the hidden `okay-gl` GL window), `SDL_DestroyWindow` and `SDL_Quit`
  became `Backend::SetPerMonitorDpiAware`, `Init`, `MakeWindow` (the seam window now owns the SDL window; the `SDL_Window*` for the ImGui backends and
  `SDL_Renderer` comes from `NativeHandle()`), destroying the window object, and `Shutdown`. The GL context was already created through the seam.
* Launcher: the same for the main window, the chat pop-out window, start-up / early exit / shutdown, and the on-demand audio start
  (`SDL_InitSubSystem(AUDIO)` is `Init` with audio only). Display bounds queries were moved in round 5/6 (`DisplayCount/DisplayAt`).
* Both apps ask for `BackendKind::Sdl2` explicitly, so `OKAY_PLATFORM_BACKEND=own` cannot hand an SDL-only code path a window with no `SDL_Window`.
  SDL2 stays the default and the behaviour is meant to be identical. One deliberate detail: the SDL2 backend's `Init` switches text input off (the
  seam's contract), SDL2 itself starts with it on, so both apps call `StartTextInput()` right after `Init`.
* Not moved (stopped here): the `SDL_PollEvent` loops (editor 1, launcher 1) and `SDL_WINDOWPOS`/`SDL_Event` use feed Dear ImGui's `imgui_impl_sdl2`, which takes
  an `SDL_Event`; they move with an own ImGui platform backend. `SDL_Renderer` creation needs the `SDL_Window*`. The launcher's `SDL_SetWindowsMessageHook`
  (tray icon) is still SDL's. `editor/main.cpp` line 871 (`SDL_Init(SDL_INIT_EVENTS)` in the headless helper) is untouched.
* Checked: launcher and editor `main.cpp` pass `g++ -fsyntax-only` with the real include paths. Not built to executables, not run.

## Round 8: our own Dear ImGui platform feed (`shared/ImGuiPlatformFeed.hpp`), OFF by default

Written from Dear ImGui's public header (`io.Add*Event`, `ImGuiKey`, `ImGuiPlatformIO`) and the `okay::platform` seam; imgui_impl_sdl2 and SDL
code were not read or followed.

* `Translate()` (pure): platform `Event` -> list of `Action`s (key, mouse position, button, wheel, text, focus), filtered by window id.
  Key repeats are dropped (Dear ImGui repeats keys itself). `KeyToImGui()` is the key table (HID numbers, including F13-F24 and keypad `=`
  by value). Dear ImGui also wants `ImGuiMod_Ctrl/Shift/Alt/Super` as their own key events; the feed keeps a held-modifier mask so
  left + right keys of one kind work.
* `Feed` (per ImGui context; the launcher has two): `ProcessEvent`, `NewFrame` (display size and framebuffer scale, delta time never zero,
  "no mouse" when the pointer leaves and no button is held, cursor shape and hide, `WantSetMousePos` warp), clipboard and IME
  position through `ImGuiPlatformIO` callbacks that go to the seam, window DPI remembered (`Dpi()`, not applied to ImGui's scale).
  `FeedHost` hides the window and backend, so the tests use a fake one.
* Seam additions: `CursorShape` and `Backend::SetCursorShape` (SDL2 backend: system cursors; own Linux / Windows backends: not written,
  they return false), `TranslateSdl2Event()` (SDL event -> platform event with no side effects, temporary).
* Wired into `editor/main.cpp` and `launcher/main.cpp` (main window and chat window) behind `OKAY_PLATFORM_EVENTS=own`. Default
  stays `imgui_impl_sdl2`. Both apps log which one is active. In own mode the editor's multi-viewport pop-out panels are off (the feed
  does not set `PlatformHasViewports`) and there is no gamepad navigation.
* **Not moved yet:** the two `SDL_PollEvent` loops themselves. They still poll SDL because dozens of other consumers in them (`g_okayInput.Feed`,
  controller add/remove, drop files, render reset, chat window routing) read `SDL_Event`; the own feed gets each event translated.
  Moving the loops to `Backend::PollEvent` is the next step. Also not done: `SDL_CaptureMouse` (drag that leaves the window), IME
  composition display, own-backend cursor shapes (X11, Win32), touch.
* Checked: `ctest -R imgui_platform_feed` logic (built here with g++ against the pinned Dear ImGui commit; 82 checks), and `g++ -fsyntax-only`
  of `launcher/main.cpp`, `editor/main.cpp` and `SdlBackend.cpp`. Not built to executables, not run on Windows, not tried with a real window.

## Round 9: the editor and launcher event loops poll `okay::platform` (SDL2 still the default backend)

Written from the seam's own header and the OS-neutral event meaning; no SDL source was read or followed. This moves the two
big `SDL_PollEvent` loops (editor 1, launcher 1) and their small neighbours behind the seam. The own backend stays off by default and
the apps still ask for the SDL2 backend (they still draw through `SDL_Renderer` and Dear ImGui's SDL backends).

What the seam gained (each with a default, so a backend that cannot do it says so):

| API | sdl2 | own Linux / own Windows |
|---|---|---|
| `Backend::PumpEvents()` | `SDL_PumpEvents` | reads waiting input into the backend's queue; the next `PollEvent` returns it, in order, nothing lost |
| `Backend::PushEvent(Event)` | `SDL_PushEvent` for Quit, WindowClose, focus gained / lost, KeyDown / Up, TextInput, MouseMove, button down / up, wheel, DropFile | queues any event type except `NoEvent` and `Other` |
| `Event::native` | `const SDL_Event*` of the event just returned (valid until the next poll) | null |
| `Backend::ReportUnmappedEvents(bool)` and `EventType::Other` | with `true`, events the seam has no type for (pointer enter / leave, display changes, IME composition ...) come back as `Other` with `native` set, so imgui_impl_sdl2 still sees every event | nothing to report |
| `EventType::RenderTargetsReset`, `RenderDeviceReset` | `SDL_RENDER_TARGETS_RESET`, `SDL_RENDER_DEVICE_RESET` | never fired |
| `Backend::CharForKey(Key)` | inverse of `KeyForChar`: the letter, digit or space a key types on the current layout | the same (follows that backend's `KeyForChar`) |
| `Event::windowId` on `DropFile` | set | the own Windows backend does not fill it (its drop has no per-window id) |

Call sites moved:

* **Editor** (`editor/main.cpp`): the main loop is `sdlSeam->PollEvent`. Escape edge, key taps for the playing game (layout-aware through
  `CharForKey`), focus click, grabbed-pointer filter, quit / close / focus lost, controller add / remove, render resets, game text and wheel
  (the wheel keeps SDL's raw OS direction: `wheelFlipped ? -wheelY : wheelY`), touch, OkayUI demo text and Backspace, file drops. The splash's
  `SDL_PumpEvents` is `PumpEvents`; the `OKAY_UI_DROP` test hook pushes a platform `DropFile` through `PushEvent`.
* **Launcher** (`launcher/main.cpp`): the main loop, `ChatHostEvent` (routes events to the chat window by window id; the kinds the seam has
  no type for are routed by the id inside `native`), quit / close / focus gained, file drops (main window and chat window), and the verify-wait
  `SDL_PumpEvents`.
* **Dear ImGui**: `FeedImGuiEvent` in both apps takes a platform event. Default: `ImGui_ImplSDL2_ProcessEvent(event.native)`. With
  `OKAY_PLATFORM_EVENTS=own`: `Feed::ProcessEvent(event)` directly (no more `TranslateSdl2Event` in the apps; the function stays for tests).
* `shared/OkayUIHost.hpp`: `InputCollector::Feed(const okay::platform::Event&, char layoutChar)` next to the SDL overload. A test feeds both
  with the same events and compares every field of `OkayUI::Input`.

Tests: `ctest -R platform_events` (headless: round trip of every pushable event kind on each compiled-in backend, `PumpEvents` order,
`native`, unmapped events, render resets, flipped wheel, drop strings, CharForKey; SDL 2.30 sometimes hands a just-pushed event over on the
second poll, so the SDL part of the test polls up to five times), `ctest -R okayui_host` (SDL / platform parity), `ctest -R imgui_platform_feed`.

Not moved (SDL calls that remain next to the loops): `SDL_EventState(SDL_DROPFILE)` (editor), `SDL_GetGlobalMouseState` (launcher drop
position), the `SDL_PollEvent` in `editor/VsGraphShot.cpp` (a screenshot tool), the SDL window and renderer objects, Dear ImGui's SDL backends.
Windows (own or sdl2) was not run; `OwnWin32.cpp` passes `-fsyntax-only` with MinGW.

## Round 10: drops, cursors, global mouse, tray hook, the SDL second-poll bug, and the plan for `SDL_Renderer`

Written from the freedesktop XDND protocol text, RFC 2483 / RFC 3986 (URI lists, percent-encoding), the X11 cursor font list and the Win32
stock cursor ids; no SDL source was read or followed.

* **SDL 2.30.0 `SDL_PollEvent` bug (found and fixed).** On SDL 2.30.0 (the Linux package used here) `SDL_PollEvent` returns 0 once when an
  event was pushed after the previous poll emptied the queue, although `SDL_PeepEvents` already sees it; the event arrives on the call after.
  Reproduced with a 20-line program, then by the new stress test. The sdl2 backend now does `SDL_PumpEvents` + `SDL_PeepEvents(GETEVENT, 1)`
  in `PollEvent`. `tests/test_platform_events.cpp` pushes 3,000 events one at a time (push, poll, push, poll) and 3,000 more in a batch with
  `PumpEvents` calls in between, on each backend, with no retries: none lost, none reordered, mixed kinds. Real input comes through the same
  pump, so this is not known to have lost real events; it did make a poll right after `PushEvent` unreliable.
* **Drops.** Own X11: XDND version 5 (`XdndAware` on every window, Enter / Position / Leave / Drop, `XdndStatus` and `XdndFinished` answers,
  the `text/uri-list` selection; more than three types via `XdndTypeList`; an INCR transfer is refused). Paths come from
  `UriListToPaths` (`engine/src/Platform/UriList.hpp`: `file://` and `file://localhost/`, percent-decoding, other hosts and schemes and `%00`
  refused). One `DropFile` per file, `windowId` = the window it landed on. Own Windows already had `WM_DROPFILES` with a window id
  (an earlier report of mine said it had none: wrong). The OLE drop target is not written (not needed for files). `SDL_EventState(SDL_DROPFILE)`
  moved into the sdl2 backend's `Init`.
* **Cursor shapes on the own backends.** X11 font cursors (`XCreateFontCursor`; the font has no diagonal resize arrows, so those use the window
  corner cursors, and Progress is the watch like Wait), Win32 stock cursors (`LoadCursorW` + `SetCursor`, applied again on `WM_SETCURSOR`).
  The shape tables are `engine/src/Platform/CursorMap.hpp` and are tested against the numbers in the system headers.
* **`Backend::GlobalMouseState(x, y, buttons)`** (sdl2: `SDL_GetGlobalMouseState`; own X11: `XQueryPointer` on the root; own Windows:
  `GetCursorPos`). The launcher's "was the file dropped on the chat box" check uses it.
* **`Backend::SetNativeMessageHook(hook, user)`** (Windows only: sdl2 `SDL_SetWindowsMessageHook`, own Windows: called first in the window
  procedure). The launcher's tray icon now installs its hook through it. Elsewhere it returns false.
* `editor/VsGraphShot.cpp` (the `--vs-shot` screenshot tool) reads events through the seam.
* **First step of the `SDL_Renderer` plan:** the launcher makes its six kinds of RGBA texture (game covers, remote images, emoji sheet,
  animated frames, splash logo) with `okay::platform::Painter::CreateTexture` instead of `SDL_CreateRGBSurface...` +
  `SDL_CreateTextureFromSurface` + `SDL_FreeSurface` (18 SDL calls fewer).

Checked: `ctest -R "platform_events|platform_xdnd|okayui_host|imgui_platform_feed|^platform$"` on Linux (the drop and cursor tests under
Xvfb with a second X connection acting as the drag source); `OwnWin32.cpp` with MinGW `-fsyntax-only`; editor, launcher, `VsGraphShot.cpp` and
`SdlBackend.cpp` with `g++ -fsyntax-only`. **Not run:** anything on Windows (the Win32 drop, cursor, global mouse and message hook, and
the sdl2 Windows hook trampoline, which no compiler here has the SDL headers for), a real file manager drag on Linux (the test source is
written from the protocol text, so a real source may differ in detail), a window manager, Wayland, the launcher and editor with a window.

### Tray-icon callback protocol model (Round 11, partial)

* `engine/src/Platform/TrayProtocol.hpp` (header only, no Win32 includes): decodes the tray callback message (`lParam` low word = event,
  version 4 adds icon id and `wParam` position), names the events (left click, double click, right click, context menu, select, key
  select, balloon shown/hidden/timeout/clicked, popup open/close), `NeedsReAdd` for the registered "TaskbarCreated" message, and
  `ActionFor` (the launcher's rule: left/double click open, right click/context menu raise the menu). Written from the public
  Shell_NotifyIcon / WinUser.h documentation; the constants are listed in the header's top comment.
* Test: `platform_tray` (`tests/test_platform_tray.cpp`), 54 checks with hand-written message values. Runs on Linux.
* The launcher's `OkayMsgHook` (launcher/main.cpp) is NOT switched to the model yet: it is Windows-only code that no build here compiles,
  so identical behaviour could only be argued from the test, not verified. The tray-hook gap (the Windows `Shell_NotifyIcon` call itself
  and the hook trampoline) stays as before; only the decoding is now modelled and tested.

### The biggest block left: `SDL_Renderer` drawing (plan)

Measured by `docs/sdl_inventory.md` after this round: editor 126 calls, launcher 39 (of 722 / 220 reachable), plus Dear ImGui's two SDL
backends (editor 17, launcher 13) and the window objects. Threads in the launcher (625) are bigger by count but are a separate job
(`docs/platform/sdl_removal_plan.md`, step 2). Plan, in order, each step small enough for one commit and each behind the default SDL2 painter so
nothing changes unless the own backend is chosen:

1. **Textures from pixels** (launcher: done this round; editor: `SDL_CreateTexture` 8, `SDL_UpdateTexture` 8, `SDL_SetTextureBlendMode` 8,
   `SDL_SetTextureScaleMode` 8, `SDL_DestroyTexture` 6, `SDL_QueryTexture` 3 are the 3D view and thumbnail textures): move them to
   `Painter::CreateTexture` / `UpdateTexture` / `SetTextureBlendMode` / `SetTextureScaleMode` / `DestroyTexture` / `QueryTexture`. All exist.
2. **Frame calls** (editor and launcher: `SDL_RenderClear`, `SDL_RenderPresent`, `SDL_GetRendererOutputSize`, `SDL_RenderSetClipRect`,
   `SDL_RenderSetScale`, `SDL_RenderSetVSync`, `SDL_RenderDrawPoint`): `Painter::Clear`, `Present`, `OutputSize`, `SetClipRect`, `SetScale`,
   `SetVSync`, `DrawPoint` all exist. One `Painter` per renderer, made with `WrapSdl2Renderer`, kept next to the `SDL_Renderer*`.
3. **What `Painter` lacks** (from the inventory): render targets (`SDL_SetRenderTarget`, editor 3), `SDL_RenderReadPixels` (2: screenshots),
   `SDL_RenderFlush` (2: before raw GL calls), `SDL_RenderGeometryRaw` (1), `SDL_GetRendererInfo` (3: which driver), reading the draw colour,
   and the Direct3D 11 device behind the renderer (the editor's interop). Add each with a default that says no, an sdl2 implementation, a
   software-painter implementation where the software painter can do it, and a headless test against the SDL software renderer.
4. **Our own Dear ImGui renderer backend** (replaces `ImGui_ImplSDLRenderer2_*`): turns `ImDrawData` into `Painter::Geometry` + `Copy` with
   the per-command clip rectangle and texture, written from `imgui.h`'s documented draw-list structures. Compare it against the SDL
   renderer backend on the same frame with the software painter (the `okayui_painter` method).
5. **Window and renderer creation through the seam** (`SDL_CreateRenderer` editor 5, launcher 4; `SDL_CreateWindow` is already behind
   `MakeWindow`): a `Painter` that owns its window surface on the own backend (GL or software). The speed on Windows is the unknown: the
   software painter is fine for the player, but a 1080p editor needs a GPU path (OkayUI's GL and Direct3D 11 render backends exist and are
   not wired).
6. **Own Dear ImGui platform backend viewports** (the editor's pop-out panels), then delete `Event::native`, `ReportUnmappedEvents`,
   `WrapSdl2Window` and `TranslateSdl2Event` together with SDL2.

## Round 11 (branch `claude/own-platform-5`): the 2D renderer calls, our own Dear ImGui renderer, and the launcher's threads

Written from `imgui.h` (draw lists, the texture request protocol), the Win32 and POSIX thread documentation, and our own interfaces; no
SDL, imgui_impl_sdlrenderer2 or other project code was read or followed.

* **Painter additions (step 3 of the plan).** `TextureDesc::target` and `SetRenderTarget` / `GetRenderTarget` (render targets; the software
  painter swaps its frame buffer with the texture's pixels and keeps clip and scale per target), `Flush`, `GetDrawColor`, `DriverName`,
  `IsAccelerated`, `GeometryRaw` (separate position / colour / uv arrays with strides, 8-, 16- or 32-bit indices; a default builds a vertex array
  and calls `Geometry`, so every painter has it), `PainterDesc::requireAccelerated`. Tested on both painters in `okayui_painter` (154 checks).
* **The apps' renderer calls (steps 1, 2, 5).** `shared/PainterBridge.hpp` keeps one painter per `SDL_Renderer*` and has one function per thing
  the apps did (`CreateTexture`, `UpdateTexture`, `DestroyTexture`, `QueryTexture`, texture blend / filter, `Clear`, `Present`, `OutputSize`,
  `SetVSync`, `SetScale`, `SetDrawColor`, `Flush`, `ReadPixels`, `DriverName`, `IsAccelerated`, render target, `CreateRenderer`, `DestroyRenderer`).
  Editor and launcher no longer call `SDL_CreateTexture`, `SDL_UpdateTexture`, `SDL_RenderClear`, `SDL_RenderPresent`, `SDL_GetRendererOutputSize`,
  `SDL_CreateRenderer` / `SDL_DestroyRenderer`, `SDL_RenderReadPixels` and friends; the renderer is made by `CreateSdl2Painter` (accelerated, else any,
  as before; the editor keeps its three-step fallback with the driver hint). The table is never destroyed on purpose (a renderer still open at exit
  must not be freed by a static destructor after SDL is gone). The Windows-only Direct3D 11 interop in `editor/main.cpp` was converted the same way
  and **could not be compiled here** (no Windows or MinGW SDL headers).
* **Our own Dear ImGui renderer (step 4), OFF by default.** `shared/ImGuiPainterRenderer.hpp`: honours the `ImTextureData` requests (create,
  update rectangles, destroy; RGBA32 and Alpha8), draws every command with its clip rectangle and texture through `Painter::GeometryRaw`, uses
  `VtxOffset`, restores scale and clip. Switch: environment variable `OKAY_IMGUI_RENDER=own` (editor, launcher main window and chat window);
  without it `imgui_impl_sdlrenderer2` runs as before. Both apps print which one runs at start-up. In multi-window mode the editor's secondary
  windows keep their existing renderer. `ctest -R imgui_painter_render` draws a real Dear ImGui frame on the software painter and on SDL's
  software renderer: flat shapes and clip edges exact at framebuffer scale 1 and 2, whole frame (text, window, button) 0.85 levels mean difference
  between the two painters, texture protocol checked. It has **not** been run in a real editor or launcher window, nor compared with
  `imgui_impl_sdlrenderer2` on the same frame.
* **Not done from the plan:** step 5's own window surface for the own backend in the editor and launcher (they still make SDL windows, and
  `OKAY_PLATFORM_BACKEND=own` still cannot run them), and step 6 (deleting `Event::native`, `WrapSdl2Window`, `TranslateSdl2Event`). Remaining
  `SDL_*` in the apps: `SDL_Renderer` objects in function signatures and `SDL_Texture*` handle types, `SDL_RenderCopy*` / `SDL_RenderGeometry`
  drawing and `SDL_SetTextureColorMod` / alpha mod in `UIPainterSDL.hpp`, `okay::DrawSplashStyled`, `OkayUI::EndFrame(SDL_Renderer*)`, the SDL
  surfaces in `AppIcon.hpp`, and Dear ImGui's two SDL backends.
* **Threads (`docs/platform/sdl_removal_plan.md` step 2).** `shared/OkayThreads.hpp`: `okay::thr::CreateThread` / `DetachThread` / `WaitThread`, recursive
  `Mutex` (`CreateMutex` / `LockMutex` / `UnlockMutex` / `DestroyMutex`), `AtomicInt` (`AtomicSet` / `Get` / `Add` / `CAS`), with SDL's conventions
  (recursive locks, thread functions return an int, `Add` returns the old value). Win32 `CreateThread` + `CRITICAL_SECTION` on Windows, pthreads
  elsewhere, `<atomic>` only: no `<mutex>`, `<thread>`, `<condition_variable>` or `std::call_once` (rule 21a). The launcher (`main.cpp`,
  `VoiceChat.hpp`, `VersionsUI.cpp`) moved by name: 718 uses. `ctest -R okay_threads` (contention with 6 threads, exact counter, recursive locks,
  600 detached threads, return values) passes normally and under AddressSanitizer + UBSan and under ThreadSanitizer; `ctest -R voice_races` passes.
  It did not need `okay::corethreads`: that shim has a non-recursive POSIX mutex and no detach, which the launcher's 170 lock sites rely on.
  **Not run:** the launcher itself (downloads, chat, voice, update), anything on Windows (the Win32 half compiles only under MinGW `-fsyntax-only` for
  the test, the launcher was only checked on Linux), so a hang from a changed lock would show only in a real session.

## Round 12 (branch `claude/own-platform-5`): checking the round-11 moves 1:1, and the launcher and editor on the own backend

**Why a review first.** The thread move (718 uses) and the renderer bridge change what the *default* launcher and editor run, and until now nobody had run
a launcher. So before anything new:

* **Threads, mechanical review** (`scripts/review_thread_conversion.py`, ctest `thread_conversion_review`). Counts of every `okay::thr::` name per file equal
  the number of SDL uses in the last SDL commit (`tests/data/thread_conversion_baseline.json`: 58 `CreateMutex`, 65 `CreateThread`, 56 `DetachThread`,
  170 + 10 + 2 `LockMutex`, the same `UnlockMutex`, 24 `AtomicSet`, 16 `AtomicGet`, 6 `AtomicAdd`, 2 `AtomicCAS` ...), no SDL thread name is left, and with
  `--base 2eda734e` every line that holds a thread name equals the old line after the name mapping (the only edits by hand are `SDL_AtomicIncRef` / `DecRef`, whose
  result no site used, now `AtomicAdd(+1 / -1)`). Result: CLEAN, no mismatch. Read by hand: no call site uses the return value of `AtomicSet`, `AtomicAdd` or
  `AtomicCAS`; the thread name argument was only a label in SDL; `WaitThread(t, nullptr)` and `DetachThread` have SDL's meaning (checked by `okay_threads`: 24 checks, also under
  ASan, UBSan and TSan). One difference found and fixed: on Windows SDL starts threads with `_beginthreadex`, ours used a bare `CreateThread`; now `_beginthreadex`
  (MinGW `-fsyntax-only` only).
* **Renderer bridge, behaviour test** (`tests/test_painter_bridge_vs_sdl.cpp`, ctest `painter_bridge_vs_sdl`, 48 checks, 0 failures). Two identical SDL software
  renderers on surfaces; one driven with the exact old call sequences, one through the bridge. Equal afterwards: texture format, access, size, blend mode and scale mode for
  the five creation sequences the apps use (streaming 3D view, static thumbnail, nearest sprite texture, opaque FpHands texture, render target); pixels after a rectangle update with
  its own pitch and a scaled `SDL_RenderCopy`; clear colour, draw colour, output size, driver name, `ReadPixels` bytes; scale 2 + clip + `SDL_RenderGeometryRaw` with
  16-bit and 32-bit indices (the ImGui path); render target draw and copy back. Also `CreateRenderer` on a window of the sdl2 backend (dummy video driver). Not covered by a test: the
  Direct3D 11 interop, and OKAY_RENDER_DRIVER hints.
* **A real launcher run, headless.** `okayspace-launcher` (`bin/OkaySpace`) built from this branch and run on Xvfb with SDL's dummy audio
  (`scripts/headless_app_shot.sh`): on the DEFAULT backend (sdl2, imgui_impl_sdl2, imgui_impl_sdlrenderer2) it starts, draws the whole Create page, its update-check worker thread reaches
  the update server and the page shows "Update available v2.887.0" (so `CreateThread`, the mutexes and atomics, and the textures, clear, present of the bridge all ran in a real
  process). Left alone with the default settings (without `OKAY_UPDATE_ON_LAUNCH=0`) it downloaded the newest release into the build folder and tried to swap it in, which is also a real run of the
  download threads. The two pictures (made with `scripts/headless_app_shot.sh`, not kept in the repository) differ by 0 pixels at a
  4 percent threshold (RMSE 0.06 percent). Not tried: clicking, typing, the chat window, voice, a download in the UI, quitting (SDL turns SIGTERM into a close
  request, which makes the launcher ask Exit or Minimise).

**Own window surface (step 5), launcher done, editor written but not run.** With `OKAY_PLATFORM_BACKEND=own` the launcher now makes its windows on the own backend
(X11 here), uses our own Dear ImGui input feed and our own Dear ImGui renderer (both forced on for the own backend, since the SDL ones cannot see such a window) and draws
with the software painter. How, in order: `okay::platform::Default()` instead of a fixed sdl2 backend; `shared/WindowHandles.hpp` (native handle to `Window`, used by the `lw::` helpers,
registered for the main and chat window); `okay::pb::CreateRenderer` on an own-backend window asks a registered factory (`OkayUI::soft::CreateWindowPainter`) and returns the
painter's address as the "renderer" handle (never an SDL renderer: use it only with `okay::pb` and the Painter); Splash, the script UI bridge and OkayUI's end-of-frame
(`shared/OkayUIFrame.hpp`: `EndFrame` on SDL, `EndFrameData` + `FrameDrawer` on a painter) take the handle through `okay::pb::For`; `okay::SetAppIcon` is `Window::SetIcon`; the mouse
state and text-input calls are seam calls. The launcher has no direct `SDL_*` call left except `SDL_SetMainReady` and audio-format macros. The editor got the same changes (window title, show,
raise, restore, held keys, cursor visibility through the seam; feed and renderer forced; painter factory) and compiles on Linux (`-fsyntax-only` and a full build, see the run notes), and it was **built and run headless** on both backends (`bin/OkayEngine --dev` on Xvfb, `APP_ARGS=--dev scripts/headless_app_shot.sh ...`): default backend and own
backend both start, draw the full editor UI (menus, Hierarchy, Scene grid, Inspector, Console, status bar) and keep running 60 s; the own backend run logs
`platform backend: own`, own feed, own renderer and, as designed, "Multi-window panels are off" (no multi-viewport on the own feed). The two pictures (not kept in the repository; the default run happened to show the "Engine Update" dialog because its update check
had finished by the time of the picture; the own run's check had not, so the two pictures differ by that dialog, not by drawing). The own run showed 8 frames per second
(software painter on a shared machine). Not tried: any click, the GL 3D view (needs a scene with a 3D camera), play mode, the file dialog, Windows.

## Round 13: scripted input for headless app runs, and the plan for multi-window panels on the own backend

**Scripted input (`shared/TestHooks.hpp`, compiled in only with `-DOKAY_ENABLE_TEST_HOOKS=ON`; the option is off by default, refuses to configure with
`OKAY_REQUIRE_SODIUM` (a release), and nothing of it exists in a normal build).** `OKAY_TEST_SCRIPT=<file>` makes the launcher or editor push platform events
(`Backend::PushEvent`) at the times in the file, so every click, key and drop goes through the real event loop on either backend, with no xdotool. One command per line,
`<ms> <command>`: `move`, `click X Y [button]`, `down`, `up`, `wheel`, `type TEXT`, `key [ctrl+|shift+|alt+]NAME [down|up]`, `drop PATH`, `quit`, `close`, `shot PATH` (the frame just
drawn, read back before the present, so it is exactly what the window would show) and `exit [CODE]` (leave the main loop at once; the program then returns CODE). The parser and
the event each command makes are tested in `tests/test_test_hooks.cpp` (ctest `test_hooks`). `scripts/run_app_script.sh <program> <script> <outdir> [seconds] [backend...]` runs the program on a private
Xvfb once per backend, checks the exit code, and compares the pictures of the two backends (ImageMagick `compare`; pictures stay in `<outdir>`, outside the repository).

**What the scripted runs showed (Xvfb, Linux).** Launcher, scripts `click`, `type`, `drop`, `shot`, `quit`, then a click on the dialog's Exit button: both backends exited with code 0 and
the pictures were identical to 0 pixels at a 4 percent threshold (first page, Play, Settings pages, typed text in the search box, the drop hint toast, `ctrl+a` then typing replaces the text,
Backspace). Pictures taken while the toast faded differed by timing only. Editor (`--dev`): the project gate (name field, template list, Create project), the File menu and a quit request:
both backends exited with code 0; the differences left were the frame-rate counter, the console warning count (the own backend has no SDL dummy audio, so its ALSA open fails and logs one
more warning) and, before the fix below, the typed name. **Found and fixed by this:** `key ctrl+a` on the own backend did not select all, because the own feed learns held modifiers from
modifier key events, not from the `mods` field; the hook now presses and releases the modifier keys around the key (a real keyboard does), and the two backends then agree.
**Not run after that fix:** the editor (its main source file could not be rebuilt in the time and memory available; the editor runs above used the build before the fix) and anything on Windows.
The scripts and pictures are kept out of the repository.

**Multi-window panels on the own backend: plan (not built).** What the editor's "multi-window" mode needs from a platform backend, and what exists:
1. Dear ImGui's multi-viewport interface (`ImGuiPlatformIO`: `Platform_CreateWindow`, `DestroyWindow`, `ShowWindow`, `SetWindowPos/GetWindowPos`, `SetWindowSize/GetWindowSize`,
   `SetWindowFocus/GetWindowFocus`, `GetWindowMinimized`, `SetWindowTitle`, `Renderer_*`). Written from `imgui.h`'s documentation of those callbacks onto `okay::platform::Window`
   (`MakeWindow` borderless, `SetPosition/SetSize/Show/Raise/SetTitle/IsMinimized/HasInputFocus` all exist). Small and mostly mechanical: about 250 lines in `shared/ImGuiPlatformFeed.hpp`.
2. Input across windows. The feed filters events by the main window's id; it must accept every viewport's window id, translate the pointer to desktop coordinates for `AddMousePosEvent`
   (position in the window + `Window::GetPosition`, or `Backend::GlobalMouseState`, which exists) and report `io.AddMouseViewportEvent` from `Backend::MouseFocusWindowId` (exists).
   A drag that leaves the window needs a pointer capture (`SDL_CaptureMouse` equivalent): X11 `XGrabPointer` / Win32 `SetCapture`, not in the seam yet (small).
3. Drawing per viewport. Each secondary window gets its own painter (`okay::pb::CreateRenderer` on an own window already makes a software painter). A software painter's textures belong
   to that painter, so the shared ImGui atlas and the apps' textures (thumbnails, covers, the 3D view) must be copied per painter, which is what the editor's SDL path does today with
   `OkayVpTexture`; `imguirender::Render` needs the same per-painter texture map (about 150 lines) and the apps' own textures need a "shared texture" notion in the bridge.
4. Window manager behaviour that cannot be tested headless: the borderless always-on-top flag for tool windows, `_NET_WM_WINDOW_TYPE`, monitor placement and DPI per window. Xvfb has no window
   manager, so a test there only proves the plumbing. A real desktop run is required before this can be called working.
5. Order of work: (1) + (2) behind `OKAY_PLATFORM_EVENTS=own` first, a test with fake windows in `test_imgui_platform_feed`; (3); then a scripted run with `OKAY_TEST_SCRIPT` that drags a panel
   out (`down`, `move`, `up` with coordinates outside the window) under Xvfb; then a real-desktop try by the owner.
Estimated size: medium to large; the unknown is (4).

## Round 14 (branch `claude2/own-platform-launcher`): an own OpenGL painter, and the launcher on the own backend without SDL

Written from the OpenGL 3.3 core / 3.0 and GLSL 1.30 / 3.30 specifications and our own software painter's rules; no SDL renderer, Dear ImGui
backend or other project code was read or followed.

* **`okay::platform::CreateGLPainter(window, backend, desc, err)`** (`engine/src/Platform/GLPainter.cpp`, `PainterKind::OpenGL`, `Name()` "opengl").
  It creates the GL context of an own-backend window (3.3 core first; the backends fall back to the driver's legacy context; GL 3.0 is the
  minimum), takes every entry point from `Backend::GLProcAddress` (no GL header, no link-time GL; GLX on Linux, WGL on Windows) and owns the
  context. Every call becomes triangles in device pixels with the software painter's rounding (rectangles and Bresenham lines too, so the same
  pixels are covered), batched until the texture, blend mode, clip or target changes. Blend: `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` for colour and
  `ONE, ONE_MINUS_SRC_ALPHA` for alpha (the software painter's `a + d(1 - a)`); "None" disables blending. Clip = scissor. Textures RGBA8,
  nearest or linear, colour / alpha mod folded into the vertex colour, flips and rotation done on the CPU. Render targets are framebuffer objects
  (drawn top-down, so `ReadPixels` of a target needs no flip; the window's rows are flipped). Present = `SwapBuffers`; with vsync asked and no
  swap interval available it paces itself like the software painter. Each painter makes its own context current before it draws, so the
  launcher's two windows work.
* **Choice** (`shared/OwnPainter.hpp`): on the own backend the launcher's windows get the OpenGL painter; `OKAY_OWN_PAINTER=software` picks the
  software painter; when GL cannot start (no GLX, GL older than 3.0, shaders refused) the software painter is used and the reason is printed.
  The launcher asks for GL windows only on the own backend (`OwnWindowsWantGL`); the sdl2 path is unchanged. The `okay::pb` own-painter factory
  now gets `CreateRenderer`'s `PainterDesc` (vsync). The editor still registers the software painter (not changed to GL: not built or run here).
* **Test** `gl_painter` (Xvfb, Mesa llvmpipe): written first against a stub (1 check, 1 failure), then 55 checks, 0 failures. The same frame
  (fills with and without blending, a float rectangle, outlines, lines, points, clip, scale 2, nearest and linear textures scaled / flipped /
  rotated / tinted, a sub-rectangle update with its own pitch, indexed coloured and textured triangles, a texture drawn without blending, a
  render target drawn into and from) on the GL painter and on the software painter: 144 of 76,800 pixels differ by more than 8 levels, mean 0.34.
  The differences are texel-boundary ties of nearest sampling (a pixel centre exactly on a texel edge rounds to the other texel on the GPU).
  Also kind and names, refusals, two painters on two windows, resize.
* **The launcher, run (Xvfb, llvmpipe, 1280x800 screen).** `OKAY_PLATFORM_BACKEND=own` logs `own painter: opengl`, `platform backend: own`, the own
  ImGui feed and the own ImGui renderer. Its first picture equals the default (SDL) run's but for 8 pixels (none by more than 1 percent); the
  software painter's picture differs from both only by 1-level rounding (36,700 pixels, none by more than 1 percent). Real X input through
  `xdotool` (not the scripted hook): typing into the search box, `ctrl+a` then typing, Backspace, clicks on the side menu, the wheel scrolling the
  Settings page after a resize to 900x600, `ctrl+c` / `ctrl+v` in the search box: all worked. Fast key chords sent by `xdotool` within a few
  milliseconds sometimes lost one step on both backends alike (the SDL run as often as the own run), so that is not counted as an own-backend bug.
  With `SDL_VIDEODRIVER` and `SDL_AUDIODRIVER` set to drivers that do not exist the own run looks the same (one X connection, ours), and under
  gdb `SDL_InitSubSystem`, `SDL_CreateWindow` and `SDL_CreateRenderer` were never reached in a 60 s run. Idle CPU on this loaded shared machine
  was about the same for SDL, own + GL and own + software (3.5 cores of llvmpipe and other agents' load; not a usable speed measure).
* **Not done / not verified:** Windows. `GLPainter.cpp` and `OwnPainter.hpp` compile with MinGW (`-fsyntax-only`), but Wine 9.0 on this machine
  dies at start (`free(): invalid pointer`, even for `wine64 cmd /c echo`), so no Windows run of any kind; WGL (pixel format set once per window:
  the painter's second context attempt cannot change it) and real drivers are unverified. A real GPU. The launcher's chat window on the own backend
  (not opened). The editor on the GL painter. `_NET_WM_PID` is not set on own X11 windows (a tool that looks windows up by process finds none).
  The Experimental switch stays Linux only.

## Round 15 (branch `claude/no-sdl2-1`, 2026-11-25): own backend default on Linux, displays on the seam, the SDL ratchet

Written from the X Resize and Rotate extension protocol and `Xrandr.h` (monitors, outputs, CRTCs, mode lines), the Win32
documentation (`EnumDisplaySettingsW`, `GetSystemInfo`, `GlobalMemoryStatusEx`, window styles) and POSIX `sysconf`; no SDL or other project code.

* **The SDL ratchet.** `python3 scripts/sdl_inventory.py --ratchet` counts SDL calls and SDL words per file (launcher, editor, player,
  shared, engine, okayui; not tests, tools or the sdl2 backend) and fails, naming the words that rose, when any file goes above
  `tests/data/sdl_ratchet.json`. It runs as ctest `sdl_ratchet` (working tree) and in `scripts/precheck.sh` (step 4b, the pushed
  commit; a branch without the baseline passes). `--update-ratchet` writes the baseline and refuses a rise. Shown failing on purpose
  with one `SDL_Delay(1);` added to `player/main.cpp`: `player/main.cpp: calls 0 -> 1, words 197 -> 198; more of: SDL_Delay 0 -> 1`.
* **Default backend.** `OKAY_PLATFORM_BACKEND` now defaults to `own` on Linux (all three programs; the launcher and editor already
  ran on own there through Settings > Experimental) and stays `sdl2` on Windows and the web. `OKAY_PLATFORM_BACKEND=sdl2` in the
  environment goes back. The player on own uses the software painter (the OpenGL painter is not used there yet: the player's
  OpenGL 3D path makes its own hidden window's context current, and the GL painter caches which context is current).
* **Displays through the seam.** `Backend::DisplayModes(index)` (current mode first), `Window::SetBordered(on)` and
  `Window::SetFullscreenMode(mode, display)` (exclusive fullscreen with a mode change; false = not supported, the caller uses desktop
  fullscreen). sdl2: the SDL calls `shared/SdlDisplays.cpp` made. Own X11: monitors from RandR 1.5 (`libXrandr.so.2` through
  `dlopen`; the X screen without it), each with its output's modes and refresh (dot clock over total pixels, double-scan and
  interlace counted; `DisplayModeMath.hpp`), `SetBordered` through the Motif hints; no video mode change (desktop fullscreen).
  Own Win32: modes from `EnumDisplaySettingsW`, `SetBordered` by window style; no mode change. `shared/PlatformDisplays.cpp`
  (replaces `shared/SdlDisplays.cpp`) lists monitors and applies a display choice on either backend; the player and editor use it.
  Test `platform_displays` (pure: refresh from mode lines, current-first lists, Apply's steps on a fake window) and
  `platform_displays_xvfb` (own vs sdl2 on one Xvfb server: same display count, geometry and current mode). Before the change the
  own backend listed no display (`own: 0 display(s)`, 5 checks failed); after: 39 checks, 0 failures.
* **Player:** no `SDL.h`, no `SDL_MAIN_HANDLED` and no SDL type left in `player/main.cpp` (the plain `okay::ui2d` types of the same
  layout; one `SDL_Renderer*` cast for OkayUI's SDL end-of-frame on the sdl2 painter remains). `shared/Splash.hpp` uses the plain types too.
  The player ran 12 s on Xvfb on both backends with the obstacle-course scene: `2D painter: software` on own, `sdl2` with
  `OKAY_PLATFORM_BACKEND=sdl2`; the two pictures differ in 3,724 of 1,024,000 pixels by more than 3% (animation timing).
* **Launcher:** our own Dear ImGui feed and renderer are the default on every backend (`OKAY_PLATFORM_EVENTS=sdl2` /
  `OKAY_IMGUI_RENDER=sdl2` go back to imgui_impl_sdl2 / sdlrenderer2 on the sdl2 backend). The editor keeps imgui_impl_sdl2 by
  default: its multi-window panels need that backend's platform windows. Voice clips use `okay::platform::AudioSpec`
  (`okay::wav::LoadWavClip`; the dead 8/32-bit and big-endian conversion is gone, our decoder only gives S16 or F32).
* **Editor:** cores and RAM through `okay::platform::CpuCount()` / `SystemRamMB()`, the idle throttle through
  `Backend::HasPendingEvents()` (it called `SDL_PumpEvents` even on the own backend), mouse masks through `MouseMask`, no
  `SDL_SetMainReady` (the sdl2 backend's `Init` calls it) in the editor or launcher.
* **Counts** (`--ratchet`, calls / words, file by file sums): launcher 25/325 -> 13/286 (only the imgui_impl_sdl2 / sdlrenderer2
  fallback calls are left), editor 34/193 -> 23/177, player 0/197 -> 0/1, shared 26/194 -> 10/136, okayui unchanged 122/337. With everything each program reaches (`docs/sdl_inventory.md`, shared, engine and okayui code included): launcher
  98/716 -> 86/649, editor 123/611 -> 96/543, player 69/503 -> 53/255 (what the player still reaches is OkayUI's SDL code and `PainterBridge.hpp`'s SDL handle types).
* **Not verified:** nothing here ran on Windows (Win32 parts compiled with MinGW `-fsyntax-only` only); the editor was not linked
  in this round (`editor/main.cpp` compiled with `-fsyntax-only` only, 0 errors; a full `-O3` build ran out of memory here before); a real GPU; X11 video mode changes are not written.
* **How close SDL2 deletion is:** the apps' own direct SDL calls are now Dear ImGui's SDL backends (launcher fallback, editor
  default for multi-window panels), the editor's Windows render-driver hints and Direct3D 11 device query, and OkayUI's
  `SDL_Renderer` path (`okayui/src`, 122 calls, the OkayUI work). The `OKAY_NO_SDL` build of `claude/own-platform-6` (2026-11-19)
  was not brought over: it needs OkayUI to compile with no SDL types (that branch did it with no-op macros) and was never merged.

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
`WM_DROPFILES`. **Only syntax-checked with MinGW (`-fsyntax-only`) on Linux. Never run on Windows.**

Round 4 (this slice): **audio device** and the **scan code table**.

| New file | What | Written from | Tested where |
|---|---|---|---|
| `engine/src/Platform/OwnWin32Audio.cpp` | WASAPI shared mode, event driven, playback and capture, queue and callback mode, device list (index 0 is `default`, then the active endpoints by friendly name). One `CreateThread` per device owns its COM objects (no `std::thread`, rule 21a). Asks for the exact format with `AUTOCONVERTPCM` + `SRC_DEFAULT_QUALITY`; if the engine refuses and `allowChange` is set it runs at the mix format (16-bit PCM or 32-bit float only) and says so in the `got` spec. Links `ole32`. | The public Microsoft documentation of `IMMDeviceEnumerator`, `IAudioClient`, `IAudioRenderClient`, `IAudioCaptureClient` | **MinGW `-fsyntax-only` only. Never run.** |
| `engine/src/Platform/AudioPump.hpp` | The parts with no Windows in them: 100 ns time maths, period and buffer sizes, `WAVEFORMATEX(EXTENSIBLE)` field values and default speaker masks, the mix-format fallback rule, a growable byte queue with a spin flag (no mutex), what the pump does with one playback or capture buffer (underrun silence, silent capture packets, queue cap) | The WAVEFORMATEX, speaker position and `REFERENCE_TIME` documentation | `platform_audio_pump` (343 checks, also run under ASan and UBSan), on Linux |
| `engine/src/Platform/Win32Keys.hpp` | The scan code to HID key table, moved out of `OwnWin32.cpp`. Two keys were missing and are now there: the ISO key (scan 0x56) and the menu key (extended 0x5D). `Key::NonUsBackslash = 100` and `Key::Application = 101` are new (HID numbers, SDL's too). | The Microsoft scan code table (set 1) and the USB HID keyboard page | `platform_audio_pump` |
| `tools/platform_selftest/` (`okay-platform-selftest`) | A check program that uses only `okay::platform` with the own backend forced: timers, folders, GL context + version string (hidden window), a software frame shown with `PresentPixels` (colour bars, moving square, mouse cross), window size, clipboard round trip, audio devices, a 440 Hz tone through the audio device, microphone capture, first gamepad, keyboard, text, mouse and wheel events counted. Prints PASS / FAIL / NOT TESTED and saves `okay-platform-selftest.txt` beside the exe. CMake `-DOKAY_BUILD_PLATFORM_SELFTEST=ON` (off by default); `scripts/build-windows-dist.sh` copies it to `Tools/` when present. `--auto` runs headless for 2 s. An event pump check (an event pushed with `PushEvent` comes back, and `SetSize` comes back as a `WindowResized` through the OS queue). `--ci` (2026-11-23) is `--auto` for a build machine: an OpenGL context or version string, the software frame (`PresentPixels`), audio playback that takes no bytes and a microphone that captures none are NOT TESTED with the reason, not FAIL; the window, the event pump, the clipboard, the timers and a missing `glGetString` still fail for real. The pump check accepts any new size, as long as the resize event matches the size the window reports (DPI scaling). `.github/workflows/windows-tests.yml` runs `--ci` on the real Windows runner (step "Own Win32 platform backend self-test", report in the job summary). Pure parts (tone, tally, summary text, the `--ci` verdicts, the pump verdict) in `SelfTestLogic.hpp`. | General knowledge (sine oscillator, zero crossings) | `platform_selftest_logic` (42 checks); `platform_selftest_auto` under Xvfb with ALSA's null device and `platform_selftest_ci` under Xvfb with no sound device; MinGW syntax check. The Windows workflow step is added but has not run yet (it runs on the next main push or nightly) |

| `engine/src/Platform/Win32Display.hpp` + `OwnWin32.cpp` | **Per-monitor DPI and displays.** `Window::Dpi()` (GetDpiForWindow, else GetDpiForMonitor, else 96), `EventType::WindowDpiChanged` (from `WM_DPICHANGED`; the window is moved to the rectangle Windows suggests), `Backend::DisplayCount/DisplayAt` (`DisplayInfo`: bounds, usable area, DPI, refresh rate, primary; EnumDisplayMonitors), `Window::RefreshRateHz` on Win32. The sdl2 backend implements the same three queries. Pure maths: DPI to scale, rounding, rectangle from RECT, display order and point lookup. | Microsoft docs: `WM_DPICHANGED`, `GetDpiForMonitor`, `EnumDisplayMonitors`, `MONITORINFOEX` | maths: `platform_win32_pure`; the Win32 calls: MinGW syntax check only; sdl2 queries: compile check only (no display data tested) |
| `Win32Icon.hpp` | **Window icon** (`Window::SetIcon` on Win32): RGBA to BGRA, the 1-bit AND mask, a box-filter resize to the system's big and small icon sizes, `CreateIconIndirect` + `WM_SETICON`. Assumes straight (not premultiplied) alpha in the 32-bit bitmap: unverified. | Microsoft docs: `ICONINFO`, `CreateIconIndirect`, `SM_CXICON` | pixel functions: `platform_win32_pure`; the Windows calls: syntax check only |
| `Win32Ime.hpp` | **IME composition.** `EventType::TextEditing` (text being composed, caret in code points; empty = cleared) from `WM_IME_STARTCOMPOSITION / COMPOSITION / ENDCOMPOSITION`, `Backend::SetTextInputRect` (candidate window position). The finished text still arrives as `WM_CHAR` -> `TextInput`. The system's own composition window stays on. A state machine decides when an event is worth sending (no repeats, one clear). | Microsoft docs: IME messages, `ImmGetCompositionStringW`, `GCS_*` | state machine and UTF-16 to UTF-8: `platform_win32_pure`; the Imm calls: syntax check only; no real IME tried |
| `HidParser.hpp` | **Raw HID pads (parser, math).** A report descriptor parser (short and long items, push/pop, usage ranges, report ids, input fields), bit reading, axis normalisation to -32768..32767, hat to direction bits, and a pad layout (axes, hats, buttons). Its `Parse` is for hidraw; on Windows only its `NormalizeAxis` and `HatBits` are used (Windows does not hand out the raw descriptor). | USB HID 1.11 (item format) and the HID usage tables | `platform_win32_pure` with descriptors assembled byte by byte, plus 3,000 random inputs under ASan/UBSan (the fuzz found a count that allocated 4 billion entries; fixed) |
| `Win32Hid.hpp` + `OwnWin32Hid.cpp` + `OwnWin32.cpp` | **Non-XInput pads on Windows (Raw Input + `HidP_*`).** A message-only window gets `WM_INPUT` and `WM_INPUT_DEVICE_CHANGE` (`RegisterRawInputDevices` for joystick and game pad usages, `RIDEV_INPUTSINK \| RIDEV_DEVNOTIFY`). Per device: `HidP_GetCaps / GetButtonCaps / GetValueCaps` become a layout (up to 8 axes, 2 hats, 32 buttons), each report is read with `HidP_GetUsages / GetUsageValue`. XInput devices (`IG_` in the device name) are skipped; they stay on XInput. Index order: XInput slots first, then raw pads; `GamepadAdded/Removed` events for raw pads. These pads have **no built-in layout** (`HasBuiltinMapping()` false): the game reads `RawAxis / RawButton / RawHat`. Pure parts: the `IG_` rule, VID/PID from the name, caps to layout, sign extension, a logical maximum of -1 on an unsigned byte, evaluation of a report. | Microsoft docs: Raw Input, `HIDP_BUTTON_CAPS`, `HIDP_VALUE_CAPS`, `HidP_GetUsageValue`; HID 1.11 | maths: `platform_win32_pure`; Win32 side: MinGW syntax check only, **never run, no real pad tried** |
| `PadMap.hpp` + `OwnWin32Hid.cpp` | **Standard layout for raw pads, product name.** Our own text file format (`gamepad_map.okaypadmap`, `[pad]` entries with `vid`, `pid`, `name` and one line per standard control: `a = b0`, `leftx = a0`, `lefty = -a1`, `righttrigger = a5+`, `dpup = h0.up`, a button on a trigger reads 0 or 32767, an axis on a trigger turns -32768..32767 into 0..32767) and a pure evaluator: `StandardAxis`, `StandardButton`. A pad found in the file by vid/pid has `HasBuiltinMapping()` true; any other raw pad gets `GuessMap` from its axis, button and hat counts (axes 0-1 left stick, 2-3 right stick, 4-5 triggers; buttons 0-3 A B X Y, 4-5 bumpers, 6 Back, 7 Start, 8-9 stick clicks, 10 Guide; hat 0 the d-pad) and `HasBuiltinMapping()` stays false, but `Axis()` / `Button()` work from the guess. Files read in order, later wins: next to the program, the working folder, the path in `OKAY_GAMEPAD_MAP`. Bad lines are skipped and printed to stderr. Sample: `examples/input/gamepad_map.okaypadmap` (a made-up pad, no real device data; **no map database of real pads is shipped**, we do not copy anyone's). The name is `HidD_GetProductString` on the device path (opened with no access rights), cleaned (`CleanProductName`: control characters, spaces, 63 bytes at a UTF-8 boundary), else `HID game controller VID:PID`. | Own design; Microsoft docs for `HidD_GetProductString` | pure parts: `platform_win32_pure`; Win32 side: MinGW syntax check only, **never run, no real pad tried; the guess may be wrong for many pads** |
| `Win32Display.hpp` + `OwnWin32.cpp` | **Multi-monitor centring and fullscreen.** A window asked to be centred (`kCentered`) opens in the work area (task bar excluded) of the display under the mouse pointer (`DisplayForNewWindow`, `CentredRect`), and `SetPosition(kCentered, ...)` centres on the monitor the window is on; a window larger than the area starts at its top-left corner. Fullscreen covers the whole monitor the window is mostly on (`MonitorFromWindow`), leaving restores the old rectangle. The frame is measured at that monitor's DPI. | Microsoft docs (`MonitorFromWindow`, `GetMonitorInfo`, `GetCursorPos`) | pure maths in `platform_win32_pure`; Win32 side MinGW syntax check only, never run on two monitors |
| `Win32XInput.hpp` + `OwnWin32.cpp` (2026-11-22) | **XInput hot-plug and timed rumble.** Before: an XInput pad plugged in after start sent no `GamepadAdded` (the game never opened it), one pulled out sent no `GamepadRemoved`, every empty slot was asked with `XInputGetState` on every count and open call (Microsoft says asking an empty slot is slow), and `Rumble(lo, hi, ms)` ignored `ms`, so a pad kept shaking until the game sent zero. Now `SlotTracker` keeps the connected slots: they are asked on every event poll, the empty ones once a second or right after `WM_DEVICECHANGE`; the difference between two scans becomes `GamepadAdded` (device index of the slot) and `GamepadRemoved` (instance id 1 + slot), removals first; the first scan is silent (`PadSet::OpenAll` opens pads that were already there). `RumbleTimer` switches the motors off when `ms` is over (checked in `PollEvent`); a zero duration or zero strength stops at once. | Microsoft docs: `XInputGetState`, `XInputSetState`, "Getting Started With XInput"; own design | tracker and timer: `platform_win32_pure` (24 new checks, 175 -> 199; that test is also in the nightly Windows run); `OwnWin32.cpp`: MinGW `-fsyntax-only`, **never run, no real pad tried** |
| `Win32Messages.hpp` + `OwnWin32.cpp` (2026-11-22) | **Double-click count, menu-mode keys, mouse capture.** Before: every button event said `clicks = 1` (the X11 backend and SDL count double and triple clicks); pressing Alt or F10 alone put the window into menu mode, so the next key (Space, an arrow) drove the system menu and the game stopped getting keys, and Alt+letter beeped; releasing one mouse button released the capture while another was still held. Now `ClickCounter` counts with `GetDoubleClickTime` and the `SM_CXDOUBLECLK` x `SM_CYDOUBLECLK` box (time from `GetMessageTime`, safe across its 32-bit wrap); `WM_SYSCOMMAND` `SC_KEYMENU` is swallowed except Alt+Space (the window menu); `WM_MENUCHAR` answers `MNC_CLOSE`; the capture is released only when no button is left down. | Microsoft docs: `WM_SYSCOMMAND`, `WM_MENUCHAR`, `GetDoubleClickTime`, `GetMessageTime`, `GetSystemMetrics` | `platform_win32_pure` (15 new checks, 199 -> 214); `OwnWin32.cpp`: MinGW `-fsyntax-only`, **never run** |
| `Backend::IsRelativeMouse()` (new) | The editor's Play mode asks and sets the locked mouse (`SetRelativeMouse`, `IsRelativeMouse`) and warps the pointer to the Game view centre (`WarpMouse(nullptr, ...)`, the window the pointer is in) through the seam: 6 `SDL_SetRelativeMouseMode / GetRelativeMouseMode` and 2 `SDL_WarpMouseInWindow` calls. The launcher's `KeepWindowOnScreen` reads the display list and usable areas from `DisplayCount / DisplayAt` instead of `SDL_GetNumVideoDisplays / GetDisplayUsableBounds`. SDL2 backend, own Win32 and own Linux implement it. | own | compiled (g++ syntax-only: editor, launcher, platform files; MinGW for Win32); not run |
| `OwnWin32.cpp` DPI | **DPI-aware frame.** `AdjustWindowRectExForDpi` (looked up at run time, plain `AdjustWindowRectEx` before Windows 10 1607) is used at window creation (DPI of the monitor holding the requested position), `SetSize` and the minimum size. Drawable size is the client size (device pixels for a per-monitor aware window; a DPI-unaware process gets a scaled bitmap) so it needs no factor: use `Window::Dpi()` for the UI scale. | Microsoft docs: `AdjustWindowRectExForDpi`, high DPI desktop apps | `ValidDpi` in `platform_win32_pure`; the calls: MinGW syntax check only |

`OKAY_OWN_PLATFORM=1` is now accepted as a short form of `OKAY_PLATFORM_BACKEND=own` (the long variable wins when both are set; test `platform_default_env_own_short`).

Honest limits: nothing here has opened a real audio endpoint. Open questions only a Windows run answers: whether `AUTOCONVERTPCM`
is accepted for 6 and 8 channels on common drivers (the mix-format fallback covers a refusal), device-removal handling (the pump
stops and the device goes silent, it does not reopen), and latency (three periods, at least 20 ms, is a guess). `QueuedBytes`
counts only our queue, not what is already inside the engine's buffer (the Linux device does the same).
Still missing on Windows: rumble and battery for non-XInput pads, the app drawing its own IME candidate list. Nothing here has run on Windows.

### macOS (not written)

Needs Objective-C++ (Cocoa, NSOpenGLContext/Metal, CoreAudio, GameController). It cannot even be syntax-checked with the
MinGW cross-compiler, so nothing was written rather than writing code that was never compiled.

## Tests

| ctest name | What | Needs |
|---|---|---|
| `platform` | registry, key numbering vs SDL scancodes, timers (both backends), `BasePath` (equals the executable's directory), `PrefPath` (created, writable, under `$XDG_DATA_HOME`), `PreferredLocales` (own parsing of `$LANGUAGE` / `$LC_ALL` is exact), error box / DPI on own report "not supported", the US `KeyForChar` table, audio devices on both backends (queue, clear, pause, callback, capture; SDL dummy driver / ALSA `null` device), controllers enumeration, own evdev mapping fed through a pipe | nothing; audio parts print `SKIP` when no device can be opened |
| `platform_win32_pure` | Display/DPI maths, icon pixels, IME state machine, HID parser (see "Windows" above) | nothing |
| `platform_audio_pump` | Pure Windows helpers (see "Windows" above): time maths, wave formats, speaker masks, mix-format fallback, byte queue against a plain model, playback and capture buffer rules, the scan code table | nothing |
| `platform_window_sdl2`, `platform_window_own` | create / resize / title, GL clear colour read back with `glReadPixels` (and for `own`, the window's pixels read back from the X server with `XGetImage`), keyboard, modifiers, text input (incl. a non-ASCII character), mouse buttons / wheel / motion, relative mouse, clipboard (incl. a second X client reading ours and vice versa). Stage 2 added: `KeyForChar` on the US and the German layout (z and y swap places), `ModState`, `AppendExtendedKeys` (F-keys, Tab, navigation, left/right modifiers) with keys held through xdotool, `RelativeMouseState` sums (12,7 then 0,0 then -5,3), `IsCursorShown`, fullscreen at creation and toggled, maximised, `SetIcon` (valid and invalid), refresh rate, base path; on sdl2 also touch events and flipped / normal wheel events pushed as SDL would | Xvfb, `xdotool` (XTEST input). Skipped (exit 77) when either is missing |
| `platform_default_env_sdl2`, `platform_default_env_own` | `OKAY_PLATFORM_BACKEND` in the environment overrides the build's default backend (`test_platform --print-default`) | nothing |

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

## SDL_SaveBMP gap closed (own BMP writer)

`engine/include/okay/Platform/BmpWriter.hpp` writes a 32-bit bottom-up BMP (plain BI_RGB), written from the BMP file format
description: `EncodeBmp32`/`WriteBmp32` take RGBA rows, `EncodeBmp32Bgra`/`WriteBmp32Bgra` take SDL ARGB8888 surface rows with
a pitch. The four dev picture tools (`tools/input_shots.cpp`, `tools/project_menu_shots.cpp`, `tests/modeling_easy_shot.cpp`,
`tests/modeling_advanced_shot.cpp`) now call `WriteBmp32Bgra` instead of `SDL_SaveBMP`. Unlike `SDL_SaveBMP` (a V4 header
with an alpha mask), the file has no alpha mask, so viewers show it opaque; renderer screenshots have alpha 255 anyway. Test: `platform_bmp_writer` (hand-computed header and pixel bytes). The four tools were not
compiled here (no real Dear ImGui tree in this checkout): unverified.
