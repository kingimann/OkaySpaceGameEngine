# Own platform layer: honest status against SDL2

Goal: remove SDL2 (`docs/own_code_roadmap.md`, `docs/THIRD_PARTY.md`). This page lists every kind of SDL2 call the
player, editor and launcher make, and what `okay::platform` (`engine/include/okay/Platform/Platform.hpp`) gives instead on
Linux/X11, Windows (Win32) and macOS. Background and audit history: `docs/platform_layer.md`.

How to read the columns:

* **Done** = the code exists, is behind the seam, and a test that runs on this machine proves it (named in the last column).
* **Partial** = exists but a part is missing, or it was only run under Wine / Xvfb and not on the real thing.
* **Missing** = nothing written.
* Windows was only ever **compiled with MinGW and run under Wine 9.0**, never on real Windows. Everything in the Windows column
  is therefore at best "Partial (Wine)". "Done" in the Windows column means the pure logic is unit tested natively and the
  Win32 calls ran under Wine; it is **unverified on real Windows**.
* macOS: only pure helpers exist (`engine/src/Platform/MacLogic.hpp`: key table, modifier flags, wheel). No Objective-C was
  written, so every macOS cell is Missing unless it says otherwise.
* The seam also has an sdl2 backend (the default). Every row is Done there. This page is about the **own** backend.
* The sdl2 `Done` is not the point: the question is whether the apps can run with SDL2 absent.

Counts are identifier tokens from `grep -o 'SDL_[A-Za-z_0-9]*'` on this branch (types and constants count; one call can be several tokens).
Per folder: player/main.cpp 183 tokens, editor about 600 (main.cpp most), launcher about 1,000 (about 370 are mutex and thread calls),
shared/ about 90, engine/ about 700 (mostly `SdlPainter.cpp`, `UIPainterSDL.hpp`, `SdlBackend.cpp`).

## 1. Table: SDL2 call group against the own backend

| # | What the apps use SDL2 for (where) | Linux/X11 own | Windows own | macOS own | Proof (file) |
|---|---|---|---|---|---|
| 1 | **Timers**: `SDL_GetTicks/64`, `SDL_Delay`, `SDL_GetPerformanceCounter/Frequency` (launcher 140, editor 32, player 13) | Done | Done (Wine) | Missing | `tests/test_platform.cpp`; call sites moved |
| 2 | **Init/quit**: `SDL_Init`, `InitSubSystem`, `Quit`, `WasInit`, `SDL_MAIN_HANDLED`, `SDL_SetMainReady`, `SDL_main` (all three mains) | Done | Done (Wine) | Missing | `Backend::Init`; `tests/test_platform.cpp` |
| 3 | **Create/destroy window**, size, position, min size, title, show/hide/raise/minimize/restore (all mains) | Done | Done (Wine) | Missing | `tests/test_platform_window.cpp`, `tests/test_platform_win32.cpp` |
| 4 | **Fullscreen** (`SDL_SetWindowFullscreen`, `_DESKTOP`), borderless, maximised | Done (`_NET_WM_STATE`; untested on a real WM) | Done (Wine) | Missing | `tests/test_platform_window.cpp`; `shared/SdlDisplays.cpp` still SDL for exclusive mode switch |
| 5 | **Exclusive video mode** (`SDL_SetWindowDisplayMode`, `GetClosestDisplayMode`) | Missing (no RandR) | Partial (modes listed, no `ChangeDisplaySettings`) | Missing | `shared/SdlDisplays.cpp` |
| 6 | **Multi-monitor list, usable bounds, DPI, refresh** (`SDL_GetNumVideoDisplays`, `GetDisplayBounds/UsableBounds/DPI/Mode`) | **Missing** (`DisplayCount` is 0, `DisplayDpi` fixed 96) | Done (Wine, single monitor only) | Missing | `tests/test_platform_seam.cpp`; DPI maths `ScaleMath.hpp` (new) |
| 7 | **High DPI**: `SDL_WINDOW_ALLOW_HIGHDPI`, `SDL_HINT_WINDOWS_DPI_AWARENESS`, `SDL_GL_GetDrawableSize`, `GetRendererOutputSize` | Partial (drawable = window size, no Xft/RandR scale) | Partial (`WM_DPICHANGED`, per-monitor; only 100% tested) | Missing | `Win32Logic.hpp` DPI maths, `ScaleMath.hpp` (new, tested) |
| 8 | **OpenGL context**: `SDL_GL_SetAttribute`, `CreateContext`, `MakeCurrent`, `GetProcAddress`, `SwapWindow`, `SetSwapInterval`, `GetCurrentContext/Window` (editor, player) | Done (GLX, Mesa) | Done (WGL on Mesa under Wine; no real GPU) | Missing | `tests/test_platform_window.cpp`, `test_platform_win32.cpp` |
| 9 | **Event queue**: `SDL_PollEvent`, `PumpEvents`, `PushEvent` (all mains) | Done (events); no `PushEvent` | Done (Wine) | Missing | `EventQueue.hpp` (new, bounded, merges moves/wheel/resize) `tests/test_platform_gaps.cpp` |
| 10 | **Window events**: close, focus gained/lost, resized, exposed | Done | Done (Wine) | Missing | `tests/test_platform_window.cpp`. Missing everywhere: moved, minimised/restored, hidden, mouse enter/leave |
| 11 | **Keyboard**: `KEYDOWN/UP`, scancodes (`SDL_GetKeyboardState`, `GetScancodeFromKey`), `GetModState`, `KeyForChar` | Done | Done (`SendInput` under Wine) | Partial (key table only) | `tests/test_platform.cpp`, `test_platform_pure.cpp`. No keycode (layout-translated) events: the apps only use scancodes |
| 12 | **Text input**: `SDL_StartTextInput/StopTextInput`, `SDL_TEXTINPUT` | Done (XIM) | Done (`WM_CHAR`, Wine) | Missing | `tests/test_platform_window.cpp`, `test_platform_win32.cpp` |
| 13 | **IME composition**: `SDL_TEXTEDITING` (launcher), candidate window position | **Missing** (`EventType::TextEditing` declared, nothing fires it) | **Missing** (no `WM_IME_*`) | Missing | `Platform.hpp`; OkayUI has the composition API (`docs/own_ui_toolkit.md` 4.31) |
| 14 | **Mouse**: buttons, motion, wheel (natural scroll flag), `SDL_GetMouseState`, `GetGlobalMouseState` (launcher), `GetMouseFocus` | Done (no global mouse position) | Done (Wine) | Missing | `tests/test_platform_window.cpp`. `GetGlobalMouseState` (launcher) Missing |
| 15 | **Relative mouse / warp**: `SDL_SetRelativeMouseMode`, `GetRelativeMouseState`, `WarpMouseInWindow` | Done | Done (raw input, Wine) | Missing | same |
| 16 | **Cursor visibility**: `SDL_ShowCursor` | Done | Done | Missing | `IsCursorShown` tests |
| 17 | **Cursor shapes**: `SDL_CreateSystemCursor` (ImGui sets them through its SDL backend) | Missing | Partial: `SetCursor` written this change, compile-checked only | Missing | `Backend::SetCursor`; sdl2 backend implements it (compiled, not run) |
| 18 | **Clipboard**: `SDL_Get/SetClipboardText` (ImGui backend, player) | Done (selections, text only) | Done (`CF_UNICODETEXT`, Wine) | Missing | `tests/test_platform_window.cpp`; in-memory provider `Exchange.hpp` (new). No image / file / HTML formats anywhere |
| 19 | **Drag and drop files**: `SDL_DROPFILE`, `DROPBEGIN`, `DROPCOMPLETE` (editor, launcher) | **Missing** (no XDND) | Done (`WM_DROPFILES`, Wine; one `DropFile` per file, no begin/complete) | Missing | `Exchange.hpp`: URI-list parser and `DropBegin/DropComplete` event builder (new, tested); not wired to X11 |
| 20 | **Gamepads**: `SDL_GameController*`, hot-plug events (editor, player) | Partial (evdev, rumble, fixed mapping; no SDL-style database) | Done for XInput (4 pads, hot-plug; no real pad tested); no DirectInput/HID | Missing | `tests/test_platform_gamepad.cpp`; mapping database parser and mapper `PadMapping.hpp` (new, tested, not yet used by evdev) |
| 21 | **Haptics beyond rumble** (`SDL_Haptic*`), pad LEDs, gyro, touchpad | Missing | Missing | Missing | not used by the apps today |
| 22 | **Joystick API** (`SDL_Joystick*`, non-gamepad devices, wheels) | Missing | Missing | Missing | only used inside the SDL backend |
| 23 | **Audio device**: `SDL_OpenAudioDevice`, `Queue`, `Dequeue`, callbacks, device list (player, editor, launcher voice) | Done (ALSA through dlopen; tested as far as the test host allows, no sound card claim) | Partial (waveOut/waveIn written, **data path never run**: Wine had no sound card) | Missing | `tests/test_platform.cpp`, `AudioRing.hpp` tests |
| 24 | **WAV loading**: `SDL_LoadWAV` (launcher, 3 calls) | not platform (engine has WAV readers) | same | same | needs the launcher moved to our WAV reader |
| 25 | **Message boxes**: `SDL_ShowSimpleMessageBox` | Partial (`ShowErrorBox` logs; no dialog) | Done (`MessageBoxW`, Wine) | Missing | `Backend::ShowErrorBox`; warning/info icons Missing |
| 26 | **Paths and locale**: `SDL_GetBasePath`, `GetPrefPath`, `GetPreferredLocales` | Done | Done (Wine) | Missing | `tests/test_platform.cpp` |
| 27 | **Window icon**: `SDL_SetWindowIcon` | Done | Done | Missing | `tests/test_platform_window.cpp` |
| 28 | **Taskbar flash**: `SDL_FlashWindow` (launcher) | Missing | Missing | Missing | |
| 29 | **Win32 tray hook**: `SDL_GetWindowWMInfo` + `SDL_SetWindowsMessageHook` (launcher tray icon) | n/a | Partial (`NativeHandle()` gives the HWND; no message hook API) | n/a | the launcher would subclass the HWND itself |
| 30 | **Touch**: `SDL_FINGER*`, `SDL_GetNumTouchDevices` (editor, player) | Missing (`FingerDown/...` events exist, own never fires them) | Missing (no `WM_POINTER`) | Missing | `Platform.hpp` comment |
| 31 | **Threads, mutexes, atomics**: `SDL_CreateThread` ~70, `DetachThread` ~54, `WaitThread`, `SDL_Mutex` ~370, `SDL_Atomic*` ~30 (launcher almost only) | n/a | n/a | n/a | not a platform feature: replace with `std::thread` / `std::mutex` / `std::atomic` in the launcher (mechanical, large) |
| 32 | **2D renderer**: `SDL_Renderer`, `SDL_Texture`, `SDL_Surface`, `RenderGeometry` ... ~330 calls (editor, launcher, player, `okayui`, `SdlPainter.cpp`) | Partial (`okay::platform::Painter` software painter, player only, off) | Partial (`StretchDIBits`, same) | Missing | `tests/test_platform_painter_window.cpp` |
| 33 | **Dear ImGui backends** `imgui_impl_sdl2` + `imgui_impl_sdlrenderer2` (editor, launcher) | **Missing** (no own ImGui platform/renderer backend) | Missing | Missing | OkayUI render interface exists (`docs/own_ui_toolkit.md` 7), wired nowhere |
| 34 | **Hints and misc**: `SDL_SetHint` (render driver, audio driver, DPI), `SDL_Log`, `SDL_free`, `SDL_SaveBMP`, `SDL_GetError`, `SDL_RenderGetD3D11Device`, `SDL_VERSION*` | Partial (`Log`; no hints needed) | Partial | Missing | D3D11 device from the SDL renderer is Missing |
| 35 | **Web (Emscripten)**: `-sUSE_SDL=2` | no own backend | | | the web build stays on SDL2 or needs a browser backend |

New in this change (all pure logic, compiled and run on Linux, **no OS or window involved**): `PadMapping.hpp`, `EventQueue.hpp`,
`Exchange.hpp`, `ScaleMath.hpp`, tests `tests/test_platform_gaps.cpp` (123 checks, also clean under ASan and UBSan). Not used by any
backend yet except `Backend::SetCursor` (sdl2 + Win32). Wiring them in is the next step (see section 3).

## 2. What blocks the editor and the launcher moving off SDL2

| Need | Blocks editor | Blocks launcher | What is missing |
|---|---|---|---|
| Window + GL context + events | no | no | exist on X11 and Win32 (Wine); macOS missing |
| Dear ImGui platform backend | **yes** | **yes** | `imgui_impl_sdl2` takes `SDL_Event*` and `SDL_Window*`. Need our own (events, cursor shapes, clipboard, IME, mouse capture, DPI). About 500 lines, plus the mapper from `okay::platform::Event` |
| Dear ImGui renderer backend | **yes** | **yes** | `imgui_impl_sdlrenderer2` draws through `SDL_Renderer`. Need an OpenGL or software one (OkayUI has GL 3.3, ES 3.0, D3D11 and CPU backends but not for ImGui draw data) |
| `SDL_Renderer` and `SDL_Texture` in the app code | **yes** (~150 uses in editor files: viewport, thumbnails, `UIPreviewPainter`, `FpHandsWindow`) | **yes** (~40 uses: avatars, images) | Moving every one onto `Painter` or GL textures; the editor Scene view already uses GL |
| Text input and IME | partly | **yes** (`SDL_TEXTEDITING`) | IME events on X11 (XIM preedit) and Win32 (`WM_IME_COMPOSITION`) |
| Gamepads and haptics | no (rumble only) | no | evdev uses a fixed mapping; the database parser is now written but not wired; no real-pad test on Windows |
| Audio device | no | **yes** (voice chat, mic test) | Windows audio data path never run; macOS missing |
| Clipboard | partly | partly | image / file formats; ImGui hook |
| Cursors | **yes** (resize, text caret) | partly | X11 cursor shapes; Win32 shape written, unrun |
| Drag and drop | **yes** (asset import) | **yes** (voice notes) | X11 XDND; Win32 begin/complete events |
| High DPI | yes | yes | X11 scale (Xft.dpi / RandR); Windows tested only at 100%; fonts and ImGui style rescale |
| Fullscreen / multi-monitor | no | no | X11 monitor list (RandR); exclusive mode switch; tested only on one monitor |
| Timers | no | no | done |
| Message boxes | no | no | X11 dialog; warning/info icons |
| Threads / mutexes / atomics | no | **yes** (about 450 calls) | not platform work: switch to `std::` equivalents |
| Win32 tray + flash window | no | **yes** | message-hook API or the launcher subclasses the HWND; `FlashWindow` |
| `SDL_GetGlobalMouseState` | no | **yes** (one use) | global cursor position on X11 (`XQueryPointer`) and Win32 (`GetCursorPos`) |
| `SDL_LoadWAV` | no | **yes** | use the engine WAV reader |
| macOS | yes | yes | nothing beyond key tables; parked by the owner |
| Real-Windows run of everything | yes | yes | only Wine so far |

## 3. Next steps in order of value

1. Wire `EventQueue.hpp` into `OwnLinuxX11.cpp` and `OwnWin32.cpp` (both keep their own ad-hoc vectors today) and make `DropBegin/DropComplete` come out of Win32.
2. X11: XDND (parse with `ParseUriList`), RandR monitor list, `Xft.dpi` scale, `XCreateFontCursor` shapes, `XQueryPointer` global mouse.
3. Feed `PadMapping.hpp` from evdev (GUID from `EVIOCGID`) with a bundled database file; XInput stays as is.
4. XIM preedit and `WM_IME_*` to `EventType::TextEditing`.
5. Own Dear ImGui platform backend (the single biggest blocker for both apps).
6. Launcher: replace SDL threads/mutexes/atomics with `std::`, `SDL_LoadWAV` with our reader, move the tray and flash code to Win32 calls.
7. Run the Windows backend on a real Windows machine (windows, audio data, XInput with a pad, multi-monitor, 125-200% DPI).

## 4. Honest distance to removing SDL2

These are judgement numbers, not measurements.

* **Player**: the window, events, input, GL, audio and timers already go through the seam. What is left is its 2D drawing (the own
  software painter exists, off) and a real Windows run. About 70% done.
* **Launcher**: about 25% done. Timers and audio devices moved; everything else is still SDL: window, events, `SDL_Renderer` drawing,
  ImGui backends, ~450 thread/mutex/atomic calls, tray hook, flash, global mouse, WAV, IME.
* **Editor**: about 15% done. Timers, audio, pads, clipboard, monitor list and part of Play-mode input moved. The 19,000-line `main.cpp`
  still creates its window and renderer with SDL, polls SDL events, and runs on both ImGui SDL backends. The decisive piece
  (an own Dear ImGui platform and renderer backend, or the OkayUI port) is not started.
* **Whole project**: SDL2 cannot be removed until the editor and launcher are off it. A rough order of magnitude is several
  more large steps (own ImGui backends, launcher thread swap, X11 gaps, a real Windows run, macOS). No default may flip before a real Windows run and the owner's approval.

## 5. What was run for this page's claims

* `tests/test_platform_gaps.cpp` (new): 123 checks, 0 failures, GCC -Wall -Wextra and ASan + UBSan, Linux.
* `x86_64-w64-mingw32-g++ -fsyntax-only` on `OwnWin32.cpp` after the `SetCursor` change: no errors. **Never run on Windows or Wine for this change.**
* The Win32 and Wine claims in the table come from `docs/own_code_roadmap.md` and `docs/platform_layer.md` (not re-run here).
* The sdl2 `SetCursor` (system cursors) compiled with `-fsyntax-only`; not run.
