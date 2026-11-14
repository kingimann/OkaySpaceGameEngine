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
| 6 | **Multi-monitor list, usable bounds, DPI, refresh** (`SDL_GetNumVideoDisplays`, `GetDisplayBounds/UsableBounds/DPI/Mode`) | **Partial**: RandR 1.5 monitor list through `dlopen("libXrandr.so.2")` (primary first, name, bounds, work area from `_NET_WORKAREA`, refresh from the CRTC mode, dpi); without RandR 1.5 the one X screen. Tested on Xvfb with virtual monitors; real multi-head setups unverified | Done (Wine, single monitor only) | Missing | `tests/test_platform_x11_server.cpp` (Xvfb), `tests/test_platform_x11.cpp` (pure `BuildDisplays`); DPI maths `ScaleMath.hpp` |
| 7 | **High DPI**: `SDL_WINDOW_ALLOW_HIGHDPI`, `SDL_HINT_WINDOWS_DPI_AWARENESS`, `SDL_GL_GetDrawableSize`, `GetRendererOutputSize` | Partial: `Window::DisplayDpi()` follows `Xft.dpi` (read from the root `RESOURCE_MANAGER`, else 96) per monitor; drawable size = window size (X11 has no scale); nothing rescales fonts for you | Partial (`WM_DPICHANGED`, per-monitor; only 100% tested) | Missing | `ParseXftDpi` in `X11Logic.hpp` (pure, tested), Xvfb test sets the property and reads the dpi back |
| 8 | **OpenGL context**: `SDL_GL_SetAttribute`, `CreateContext`, `MakeCurrent`, `GetProcAddress`, `SwapWindow`, `SetSwapInterval`, `GetCurrentContext/Window` (editor, player) | Done (GLX, Mesa) | Done (WGL on Mesa under Wine; no real GPU) | Missing | `tests/test_platform_window.cpp`, `test_platform_win32.cpp` |
| 9 | **Event queue**: `SDL_PollEvent`, `PumpEvents`, `PushEvent` (all mains) | **Done**: bounded `EventQueue` in the X11 loop (all pending server events are drained first, so a burst of moves / wheel notches / resizes is merged; order of clicks and keys kept); no `PushEvent` | **Done** (Wine): `EventQueue` in the Win32 loop; 10 moves -> 1 event, 4 notches -> 1 event | Missing | `tests/test_platform_window.cpp` (Xvfb burst test), `tests/test_platform_win32.cpp` (Wine), `tests/test_platform_gaps.cpp` |
| 10 | **Window events**: close, focus gained/lost, resized, exposed | Done | Done (Wine) | Missing | `tests/test_platform_window.cpp`. Missing everywhere: moved, minimised/restored, hidden, mouse enter/leave |
| 11 | **Keyboard**: `KEYDOWN/UP`, scancodes (`SDL_GetKeyboardState`, `GetScancodeFromKey`), `GetModState`, `KeyForChar` | Done | Done (`SendInput` under Wine) | Partial (key table only) | `tests/test_platform.cpp`, `test_platform_pure.cpp`. No keycode (layout-translated) events: the apps only use scancodes |
| 12 | **Text input**: `SDL_StartTextInput/StopTextInput`, `SDL_TEXTINPUT` | Done (XIM) | Done (`WM_CHAR`, Wine) | Missing | `tests/test_platform_window.cpp`, `test_platform_win32.cpp` |
| 13 | **IME composition**: `SDL_TEXTEDITING` (launcher), candidate window position | **Partial, opt-in**: XIM preedit through the preedit-callbacks style -> `TextEditing` events (`OKAY_X11_PREEDIT=1`; off by default because an app that does not draw the composition would show nothing). The preedit buffer logic is tested; **never run against a real input method** (Xvfb has none). No candidate-window position | **Partial, UNVERIFIED**: `WM_IME_COMPOSITION` / `ENDCOMPOSITION` observed into `TextEditing` (the system IME window still shows). Only the "no IME, nothing breaks" path ran (Wine); no real IME | Missing | `tests/test_platform_x11.cpp` (`Preedit`), `tests/test_platform_win32_pure.cpp` (UTF-16 caret conversion), `test_platform_win32.cpp` |
| 14 | **Mouse**: buttons, motion, wheel (natural scroll flag), `SDL_GetMouseState`, `GetGlobalMouseState` (launcher), `GetMouseFocus` | Done (no global mouse position) | Done (Wine) | Missing | `tests/test_platform_window.cpp`. `GetGlobalMouseState` (launcher) Missing |
| 15 | **Relative mouse / warp**: `SDL_SetRelativeMouseMode`, `GetRelativeMouseState`, `WarpMouseInWindow` | Done | Done (raw input, Wine) | Missing | same |
| 16 | **Cursor visibility**: `SDL_ShowCursor` | Done | Done | Missing | `IsCursorShown` tests |
| 17 | **Cursor shapes**: `SDL_CreateSystemCursor` (ImGui sets them through its SDL backend) | **Done**: core cursor font (`XCreateFontCursor`, no Xcursor); all 11 shapes accepted by the server; what is drawn is not read back (no pointer image test) | Partial: `SetCursor` written this change, compile-checked only | Missing | `tests/test_platform_x11_server.cpp`; glyph numbers `static_assert`ed against `cursorfont.h` |
| 18 | **Clipboard**: `SDL_Get/SetClipboardText` (ImGui backend, player) | Done (selections; text plus any MIME type the app puts, e.g. `text/uri-list`, `image/png`; **no INCR**, so a payload over 256 KiB is refused; tested own-to-own only, not against other programs) | Done (Wine): `text/plain` = `CF_UNICODETEXT`, `text/uri-list` = `CF_HDROP`, other types = registered formats | Missing | `tests/test_platform_window.cpp`, `test_platform_win32.cpp`; `Exchange.hpp` provider, `Win32Exchange.hpp` layouts. UTF-8 text only (invalid text refused) |
| 19 | **Drag and drop files**: `SDL_DROPFILE`, `DROPBEGIN`, `DROPCOMPLETE` (editor, launcher) | **Done against our own source**: XDND version 5 receive (`XdndAware`, Enter incl. >3 types, Position, Drop, selection transfer, Status / Finished), `DropBegin` / `DropFile` x N / `DropComplete` with window-local point. **Unverified against a real file manager** (Nautilus, Dolphin, ...). Only `text/uri-list`; no text drops, no source side | **Done** (Wine): OLE `IDropTarget` with the drop point, `WM_DROPFILES` fallback; both give begin / files / complete. Tested with a fake `IDataObject`, never with Explorer | Missing | `tests/test_platform_x11.cpp` (state machine), `test_platform_x11_server.cpp` (Xvfb), `test_platform_win32.cpp` (Wine) |
| 20 | **Gamepads**: `SDL_GameController*`, hot-plug events (editor, player) | **Partial**: evdev pads go through a mapping table when one matches (GUID from `EVIOCGID`; our own table for Xbox 360 / One / Series, DualShock 4, DualSense, **unverified on hardware**; more lines through `EvdevAddMappings` or `$OKAY_PAD_MAPPINGS`); other pads keep the fixed standard-codes layout | Done for XInput (4 pads, hot-plug; no real pad tested); no DirectInput/HID | Missing | `tests/test_platform.cpp` (pipe-fed pads), `test_platform_gaps.cpp`; `PadMappingData.hpp` |
| 21 | **Haptics beyond rumble** (`SDL_Haptic*`), pad LEDs, gyro, touchpad | Missing | Missing | Missing | not used by the apps today |
| 22 | **Joystick API** (`SDL_Joystick*`, non-gamepad devices, wheels) | Missing | Missing | Missing | only used inside the SDL backend |
| 23 | **Audio device**: `SDL_OpenAudioDevice`, `Queue`, `Dequeue`, callbacks, device list (player, editor, launcher voice) | Done (ALSA through dlopen; tested as far as the test host allows, no sound card claim) | Partial (waveOut/waveIn written, **data path never run**: Wine had no sound card) | Missing | `tests/test_platform.cpp`, `AudioRing.hpp` tests |
| 24 | **WAV loading**: `SDL_LoadWAV` (launcher, 3 calls) | not platform (engine has WAV readers) | same | same | needs the launcher moved to our WAV reader |
| 25 | **Message boxes**: `SDL_ShowSimpleMessageBox` | **Done** (own X window with a core font and an OK button, centred on the parent; `zenity` / `kdialog` / `xmessage` when no core font; Latin-1 text only in the window; tested on Xvfb with no window manager) | Done (`MessageBoxW`, Wine) | Missing | `tests/test_platform_x11_server.cpp` (closed by Return and by a click); `Backend::ShowErrorBox`; warning/info icons Missing |
| 26 | **Paths and locale**: `SDL_GetBasePath`, `GetPrefPath`, `GetPreferredLocales` | Done | Done (Wine) | Missing | `tests/test_platform.cpp` |
| 27 | **Window icon**: `SDL_SetWindowIcon` | Done | Done | Missing | `tests/test_platform_window.cpp` |
| 28 | **Taskbar flash**: `SDL_FlashWindow` (launcher) | **Done**: `WM_HINTS` urgency + `_NET_WM_STATE_DEMANDS_ATTENTION`, cleared on focus. Hint is set and read back; what a window manager does with it is unverified | **Done** (call runs under Wine): `FlashWindowEx` until foreground; the visible flash is unverified | Missing | `Window::RequestAttention`; `tests/test_platform_x11_server.cpp`, `test_platform_win32.cpp`; sdl2 uses `SDL_FlashWindow` |
| 29 | **Win32 tray hook**: `SDL_GetWindowWMInfo` + `SDL_SetWindowsMessageHook` (launcher tray icon) | n/a | Partial (`NativeHandle()` gives the HWND; no message hook API) | n/a | the launcher would subclass the HWND itself |
| 30 | **Touch**: `SDL_FINGER*`, `SDL_GetNumTouchDevices` (editor, player) | Missing (`FingerDown/...` events exist, own never fires them) | Missing (no `WM_POINTER`) | Missing | `Platform.hpp` comment |
| 31 | **Threads, mutexes, atomics**: `SDL_CreateThread` ~70, `DetachThread` ~54, `WaitThread`, `SDL_Mutex` ~370, `SDL_Atomic*` ~30 (launcher almost only) | n/a | n/a | n/a | not a platform feature: replace with `std::thread` / `std::mutex` / `std::atomic` in the launcher (mechanical, large) |
| 32 | **2D renderer**: `SDL_Renderer`, `SDL_Texture`, `SDL_Surface`, `RenderGeometry` ... ~330 calls (editor, launcher, player, `okayui`, `SdlPainter.cpp`) | Partial (`okay::platform::Painter` software painter, player only, off) | Partial (`StretchDIBits`, same) | Missing | `tests/test_platform_painter_window.cpp` |
| 33 | **Dear ImGui backends** `imgui_impl_sdl2` + `imgui_impl_sdlrenderer2` (editor, launcher) | **Missing** (no own ImGui platform/renderer backend) | Missing | Missing | OkayUI render interface exists (`docs/own_ui_toolkit.md` 7), wired nowhere |
| 34 | **Hints and misc**: `SDL_SetHint` (render driver, audio driver, DPI), `SDL_Log`, `SDL_free`, `SDL_SaveBMP`, `SDL_GetError`, `SDL_RenderGetD3D11Device`, `SDL_VERSION*` | Partial (`Log`; no hints needed) | Partial | Missing | D3D11 device from the SDL renderer is Missing |
| 35 | **Web (Emscripten)**: `-sUSE_SDL=2` | no own backend | | | the web build stays on SDL2 or needs a browser backend |

Round 1 added the pure helpers (`PadMapping.hpp`, `EventQueue.hpp`, `Exchange.hpp`, `ScaleMath.hpp`, `tests/test_platform_gaps.cpp`).
Round 2 (this page's current state) wired them into the own backends and wrote the X11 and Win32 gaps:

* `EventQueue` in the X11 and Win32 event loops; `PadMapping` + our own table `PadMappingData.hpp` in the evdev pads; `Exchange` in the
  clipboard (any MIME type) and in drop events.
* X11: Xdnd receive, RandR 1.5 monitors, `Xft.dpi` scale, core-font cursor shapes, urgency hint, message box, opt-in XIM preedit
  (`engine/src/Platform/X11Logic.hpp` holds the pure state machines).
* Win32: OLE drop target, clipboard formats, `FlashWindowEx`, IME composition observation (`Win32Exchange.hpp`).

What was run is listed in section 5. Everything on Windows ran under Wine 9.0 at most; everything on X11 ran on Xvfb with no window manager.

## 2. What blocks the editor and the launcher moving off SDL2

| Need | Blocks editor | Blocks launcher | What is missing |
|---|---|---|---|
| Window + GL context + events | no | no | exist on X11 and Win32 (Wine); macOS missing |
| Dear ImGui platform backend | **yes** | **yes** | `imgui_impl_sdl2` takes `SDL_Event*` and `SDL_Window*`. Need our own (events, cursor shapes, clipboard, IME, mouse capture, DPI). About 500 lines, plus the mapper from `okay::platform::Event` |
| Dear ImGui renderer backend | **yes** | **yes** | `imgui_impl_sdlrenderer2` draws through `SDL_Renderer`. Need an OpenGL or software one (OkayUI has GL 3.3, ES 3.0, D3D11 and CPU backends but not for ImGui draw data) |
| `SDL_Renderer` and `SDL_Texture` in the app code | **yes** (~150 uses in editor files: viewport, thumbnails, `UIPreviewPainter`, `FpHandsWindow`) | **yes** (~40 uses: avatars, images) | Moving every one onto `Painter` or GL textures; the editor Scene view already uses GL |
| Text input and IME | partly | **yes** (`SDL_TEXTEDITING`) | both written (X11 opt-in XIM preedit, Win32 `WM_IME_COMPOSITION`), **neither run with a real IME**; no candidate-window position API |
| Gamepads and haptics | no (rumble only) | no | evdev table written and wired (our own, unverified on hardware); no real-pad test on Linux or Windows |
| Audio device | no | **yes** (voice chat, mic test) | Windows audio data path never run; macOS missing |
| Clipboard | partly | partly | MIME formats done on both; ImGui hook; no INCR on X11 |
| Cursors | **yes** (resize, text caret) | partly | done on X11 and Win32; the ImGui backend that picks the shape is missing |
| Drag and drop | **yes** (asset import) | **yes** (voice notes) | done on X11 (own source only) and Win32 (fake data object only); never tried with a real file manager / Explorer |
| High DPI | yes | yes | X11 scale is reported (`Xft.dpi`); Windows tested only at 100%; fonts and ImGui style rescale; no "scale changed" event |
| Fullscreen / multi-monitor | no | no | monitor list done (X11 tested on Xvfb virtual monitors, Windows on one monitor); exclusive mode switch and per-monitor fullscreen on X11 missing |
| Timers | no | no | done |
| Message boxes | no | no | done on X11; warning/info icons missing |
| Threads / mutexes / atomics | no | **yes** (about 450 calls) | not platform work: switch to `std::` equivalents |
| Win32 tray + flash window | no | **yes** | flash done (`RequestAttention`); message-hook API or the launcher subclasses the HWND for the tray |
| `SDL_GetGlobalMouseState` | no | **yes** (one use) | not written: global cursor position on X11 (`XQueryPointer`) and Win32 (`GetCursorPos`) |
| `SDL_LoadWAV` | no | **yes** | use the engine WAV reader |
| macOS | yes | yes | nothing beyond key tables; parked by the owner |
| Real-Windows run of everything | yes | yes | only Wine so far |

## 3. Next steps in order of value

1. Own Dear ImGui platform backend (the single biggest blocker for both apps): events, cursor shapes (done below it), clipboard
   (done), IME (written, unverified), drop (done), DPI (reported), mouse capture.
2. Launcher: replace SDL threads/mutexes/atomics with `std::`, `SDL_LoadWAV` with our reader, move the tray to Win32 calls.
3. Run the Windows backend on real Windows (windows, audio data, XInput with a pad, multi-monitor, 125-200% DPI, Explorer drag and drop,
   a real IME) and the X11 backend on a real desktop (a window manager, a file manager drop, a real input method, a second monitor).
4. Still missing in the platform layer: `XQueryPointer` / `GetCursorPos` global mouse, exclusive video modes (RandR / `ChangeDisplaySettings`),
   window events (moved, minimised, hidden, mouse enter/leave), X11 clipboard INCR, text / non-file drops, candidate-window position for the
   IME, touch, Wayland-native (X11 only; XWayland), macOS.
5. Check the evdev table against real Xbox / DualShock / DualSense pads.

## 4. Honest distance to removing SDL2

These are judgement numbers, not measurements.

* **Player**: the window, events, input, GL, audio, timers, clipboard, drop, cursors, monitors and pads now all go through the seam and
  have an own implementation on both systems. What is left is its 2D drawing (the own software painter exists, off), a real Windows run
  and a real Linux desktop run of the new X11 pieces. About 80% done (was 70%).
* **Launcher**: about 30% done (was 25%). The platform pieces it needs (IME events, drop begin/complete, flash, clipboard) now exist on
  both systems; still SDL: window creation and the main loop, `SDL_Renderer` drawing, ImGui backends, ~450 thread/mutex/atomic calls,
  tray hook, global mouse, WAV.
* **Editor**: about 20% done (was 15%). Cursor shapes, drop, monitor list, clipboard formats and IME events now exist in the layer, but
  the 19,000-line `main.cpp` still creates its window and renderer with SDL, polls SDL events, and runs on both ImGui SDL backends.
  The decisive piece (an own Dear ImGui platform and renderer backend, or the OkayUI port) is not started.
* **Whole project**: SDL2 cannot be removed until the editor and launcher are off it. The platform layer itself is now mostly written for
  X11 and Win32; what remains is the ImGui backends, the editor and launcher ports, real-hardware verification and macOS. No default
  may flip before a real Windows run and the owner's approval.

## 5. What was run for this page's claims

Round 2 (Linux x86-64, GCC, Debug):

* `test_platform_gaps` 140 checks, 0 failures (also clean under ASan + UBSan): mapping table, raw numbering, queue, exchange, DPI.
* `test_platform_x11` 93 checks, 0 failures (also clean under ASan + UBSan): Xdnd messages and state machine, `Xft.dpi`, RandR to displays,
  cursor glyphs, preedit buffer, message box layout.
* `test_platform_win32_pure` 30 checks, 0 failures: `CF_HDROP` layout and hostile blobs, file URIs, IME caret conversion.
* `test_platform` 2193 checks, 0 failures (pipe-fed evdev pads through the table).
* `test_platform_window own` 103 checks, 0 failures on Xvfb (merged burst, clipboard MIME formats between two own clients); `sdl2` 83 checks, 0 failures.
* `test_platform_x11_server` 57 checks, 0 failures on Xvfb 1024x768 (RandR 1.6, no window manager): Xdnd with our own Xlib source,
  virtual monitors, `Xft.dpi`, cursors, urgency hint, message box closed by key and click.
* `test_platform_win32` cross-built with MinGW and run under Wine 9.0 (`LANG=C.UTF-8`, Xvfb): 1307 checks, 0 failures, 3 skips (IME with a
  real IME, game pad, audio). `-fsyntax-only -Wall -Wextra` on `OwnWin32.cpp` clean.

Not run: anything on real Windows; a real window manager, file manager, input method or multi-head X setup; any real game pad; macOS.
Round 1 claims (Wine and sdl2 behaviour in the table) come from `docs/own_code_roadmap.md` and `docs/platform_layer.md`.
