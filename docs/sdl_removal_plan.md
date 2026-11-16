# Getting the editor and launcher off SDL2: the plan

Goal (CLAUDE.md rules 17 and 19): write the replacement ourselves, keep it off by default, and flip a default only with the
owner's written approval. Never delete SDL2 before our own version is finished and has run on real Windows.
This page is the ordered list of work, with a plain-words size for each step and what to test on real Windows.
The numbers come from `docs/sdl_inventory.md` (commit `05feef3b`, regenerate it with `python3 scripts/sdl_inventory.py`).
Design and audit history of the seam: `docs/platform_layer.md`. Roadmap row: `docs/own_code_roadmap.md` (SDL2).

All sizes are **judgement, not measurements**. "Small" = an afternoon of agent work. "Medium" = a day or two. "Large" = several days.
"Very large" = weeks, or an unknown. Waiting for CI is not counted.

## Where we are

| | SDL calls left (`calls / words`) | what they are |
|---|---|---|
| player | 5 / 221 | almost only data types (`SDL_Rect`, `SDL_Color`, `Uint8`); the window, input, GL, audio and 2D drawing already go through `okay::platform` |
| launcher | 743 / 1,199 in its own files | **613 are threads, mutexes and atomics (83%)**; 57 `SDL_Renderer` drawing; 14 Dear ImGui SDL backends; about 60 window, input and other platform calls |
| editor | 229 / 556 | 126 `SDL_Renderer` drawing, 22 GL context, 25 window, 17 mouse, 17 Dear ImGui SDL backends, the rest small (stage E1, below in "Editor": `main.cpp` 194 to 154 calls, its GL context and ImGui SDL2 platform calls behind `OKAY_EDITOR_PLATFORM`) |
| engine library | 0 | outside the SDL backend files (`SdlBackend.cpp`, `SdlPainter.cpp`, `UIPainterSDL.hpp`) there is **no SDL word in code** |

Three different things keep SDL2 in the two programs, and they need three different kinds of work:

1. **Platform calls** (windows, input, GL, clipboard, audio). `okay::platform` already covers about 290 of the call sites; about 60
   have no function yet. This is mostly *moving call sites*, not new code.
2. **Drawing.** Both programs draw through `SDL_Renderer`, and Dear ImGui draws through its two SDL backends. Those backends are third-party
   code that read `SDL_Event` and `SDL_Window*`. This is the long pole.
3. **Threads.** Only the launcher. It is not platform work and **cannot use `std::thread` or `std::mutex`** (rule 21a: the Windows build
   cannot compile them in headers). It needs our own wrapper; one already exists in part (`engine/src/Core/CoreThreads.hpp`).

The inventory's section 4 ranks every GAP. The platform GAPs are small; the real work is steps 2 to 6 below.

## The steps, in order

### Step 1. Land the work that already exists

**What.** The branch `origin/claude/platform-gaps2` (tip `33da9e1d`, 2026-11-14, no pull request, contains `claude/platform-gaps`) holds about 6,500 added
lines of seam work that is not on main: X11 drag-and-drop receive, RandR monitors and `Xft.dpi`, cursor shapes, the urgency hint (taskbar flash), an
X11 message box, input-method preedit, Win32 clipboard formats, an OLE drop target, `FlashWindowEx`, IME composition events, `DisplayInfo` and
`DisplayCount`, a pad mapping table, `docs/own_platform_status.md`, and tests. A dry-run merge into `05feef3b` conflicts only in the generated
`docs/feature_map/_index_az.md`. It was **not built or run by us**; its own page says Windows ran only under Wine.
**Why first.** It is the GAP list's answer for displays, flash, cursors, IME events and drop begin / complete, and the next steps need those.
**Size.** Small to medium: merge main into it, regenerate the feature map, build, run its tests under Xvfb and Wine, get CI green, one PR.
**Real Windows.** Nothing changes by default (the own backend is off). To see it: `set OKAY_PLATFORM_BACKEND=own`, run the player (see `docs/try_it.md`
section 11), and check window, keys, mouse, text, clipboard, a file dropped from Explorer, the taskbar flash, 125% and 150% display scaling, two monitors.

### Step 2. Launcher threads, mutexes and atomics (independent: can start now, in parallel)

**Status (2026-11-16, branch `claude2/launcher-own-threads`): the code is moved, not yet merged; Windows not run.** All of the
launcher's thread, mutex and atomic calls (625 calls counted over everything the launcher reaches; 590 in `main.cpp`, 23 in
`VoiceChat.hpp`, 12 in `VersionsUI.cpp`) now go through `launcher/LauncherThreads.hpp`, opaque handles over our own
`engine/src/Core/CoreThreads.hpp` (which gained a recursive mutex, `Detach()`, thread names and atomics on Win32 `Interlocked*` or
compiler builtins). The launcher's own SDL calls went from 755 to 130. Tested by `test_core_threads` (Linux, and under
ThreadSanitizer); the launcher compiles natively and passes a MinGW syntax check. The "Real Windows" list below is still to do.

**What.** 613 calls: `SDL_CreateThread` 67, `SDL_DetachThread` 55, `SDL_WaitThread` 17, `SDL_LockMutex` 178, `SDL_UnlockMutex` 184,
`SDL_CreateMutex` 59, `SDL_DestroyMutex` 6, `SDL_Atomic*` 47. Files: `launcher/main.cpp` 578, `launcher/VoiceChat.hpp` 23, `launcher/VersionsUI.cpp` 12.
**How.** Grow the existing `okay::corethreads` shim (`Mutex`, `CondVar`, `Thread`; Win32 `CRITICAL_SECTION`, `CONDITION_VARIABLE`, `CreateThread`; POSIX on the
standard library) and move it where the launcher can include it. Three things the shim lacks:
* **A recursive mutex.** SDL's mutexes are recursive (the owning thread may lock again; the SDL docs say so). The Win32 critical section is recursive,
  but the POSIX path uses `std::mutex`, which is not. Add a recursive variant, or audit every lock site (178 of them).
* **Detach.** `SDL_DetachThread` is used 55 times (fire and forget); the shim's `Thread` only joins. Add `Detach()` (Win32 `CloseHandle`, POSIX `detach`).
* **Atomics.** `std::atomic<int>` replaces `SDL_atomic_t` (`<atomic>` does not need the thread model).
Then convert the three files mechanically. Do not use `std::mutex` or `std::thread` in anything the Windows build compiles (`scripts/branch_lint.py` fails it).
**Size.** Medium to large: the replacement is small, the 613 edits are many, and a wrong lock site is a hang that tests rarely show.
**Test headless.** A race test for the new mutex (recursive lock, contention) and for detach; the existing `test_voice_races` and the launcher tests.
**Real Windows (default backend; nothing visible should change).** Sign in; chat both ways in the launcher and in the separate chat window; a file or image
sent in chat; download an editor version with progress and cancel; start a voice call and the mic test; playtime sync; quit while a download runs
(no hang, no crash). Watch for freezes: that is how a lock mistake shows.
**Try it (rule 4a).** Invisible by design; the owner uses the launcher as usual and reports hangs.

### Step 2a. Launcher stage A: WAV reading and window calls (branch `claude2/launcher-own-wav-window`, on top of step 2)

**Status (2026-11-16): moved, not yet merged; Windows not run.** Voice messages are still read with `SDL_LoadWAV` by default (through
`shared/WavPlayback.hpp`; rule 17: our own code stays off until the owner approves). `OKAY_OWN_WAV=1` switches the launcher to our own
WAV decoder (`launcher/VoiceClipLoad.hpp` over `okay::wav`; `launcher/VoiceClipLoadSdl.hpp` picks, with the old 16-bit conversion
for the SDL path). Flipping that default later removes the launcher's remaining WAV SDL words. The main and
chat windows' show, hide, raise, restore, minimised / hidden / focused checks, size, position, id and minimum size go through a
non-owning `okay::platform::WrapSdl2Window` view (taken, code only, from `claude/sdl-inventory`, which was not merged). The launcher's own
SDL calls went from 130 to 90 (refs 458 to 379). Tested by `test_launcher_wav_window` (SDL vs our reader on every WAV in the repository and
edge files; the switch picks SDL by default and with `OKAY_OWN_WAV=0`, ours with `OKAY_OWN_WAV=1`; the window view against SDL's answers under the dummy video driver). Left for later stages: the renderer, textures and
surfaces, the ImGui backends, the event pump, display bounds, `SDL_FlashWindow` and `SDL_Quit`. Try it: `docs/try_it.md`, "Launcher:
voice clips and window calls on our own code".

### Step 2b. Launcher stage B: drawing without `SDL_Renderer`, behind a switch (branch `claude2/sdl-launcher-painter`, on top of 2a)

**Status (2026-11-16): built and tested on Linux under Xvfb; off by default; not merged; Windows not run.**
`OKAY_LAUNCHER_RENDERER=own` makes the launcher draw with our own code; anything else (no variable, `sdl`) is exactly the old path.
The launcher logs which one draws once (`launcher: renderer ...`) and, with `own`, its footer says "(own renderer: opengl)".

* **The seam** (`launcher/LauncherGfx.hpp`): the main window, the painter, image textures from RGBA pixels, begin / clear / present, and
  Dear ImGui's two backends (events in, draw data out).
  * `sdl` (`launcher/LauncherGfxSdl.cpp`): `SDL_CreateWindow`, `SDL_Renderer`, `ImGui_ImplSDL2_*` and `ImGui_ImplSDLRenderer2_*`, call for call
    what `main.cpp` did. The painter is `WrapSdl2Renderer` over that renderer, so clear, present and textures are the same SDL calls.
  * `own` (`launcher/LauncherGfx.cpp`): the window is made by `okay::platform::Default()` (sdl2, or our X11 / Win32 backend with
    `OKAY_PLATFORM_BACKEND=own`), with an OpenGL context; it draws with **our OpenGL painter** (`engine/src/Platform/GlPainter.cpp`, OpenGL 1.1
    vertex arrays, entry points from the backend, no GL link) or, when the window has no GL, the software painter (`okayui/soft`).
    ImGui is drawn by **our own renderer** (`launcher/ImGuiOwnBackend.cpp`, now `shared/ImGuiOwnRenderer.cpp`: `ImDrawData` to `Painter::Geometry` with each command's clip
    and texture; ImGui 1.92's texture requests served as painter textures) and fed by **our own platform glue** (mouse, buttons, wheel,
    keys through a table from our `Key` to `ImGuiKey`, text input on only while a text field wants it, focus, window size and
    framebuffer scale, clipboard, cursor shapes through the new `Backend::SetSystemCursor`). Written from `imgui.h` and our interfaces; not
    from ImGui's example backends or SDL.
* **Image textures.** Covers, avatars, chat images, GIF frames, the emoji sheet and the splash logo are uploaded through the painter
  (`UploadRgba`): no `SDL_Surface` or `SDL_CreateTextureFromSurface` is left in the launcher.
* **What `own` does not do yet.** The chat cannot open in its own window (it stays inside the launcher; the pop-out window is SDL code);
  the OkayUI preview backend is turned off (its input still reads SDL events); the Windows tray hook and the taskbar flash work only
  while the window is an SDL one (the default platform backend).
* **Counts** (`scripts/sdl_inventory.py`, the launcher's own directory): 90 to 64 SDL calls (379 to 320 words). Of what is left,
  18 calls are the `sdl` side in `LauncherGfxSdl.cpp`.
* **Tests.** `launcher_imgui_renderer`: our ImGui renderer against a reference rasteriser written in the test (flat and gradient shapes
  with alpha, nested and fractional clip rectangles, a user texture with tint, text from ImGui's atlas, framebuffer scale 2 with a moved
  DisplayPos): on the software painter no pixel is more than 1 level off. `launcher_imgui_renderer_gl` (Xvfb, Mesa llvmpipe): the same on
  our GL painter on an sdl2 and an own window, at most 2 levels off. `scripts/launcher_renderer_check.sh`: the launcher under Xvfb with
  both renderers (screenshots, the pixel difference, a click into the search box and typing "abc", a click on Play, and the frame time).
  Results (2026-11-16, Xvfb, Mesa llvmpipe, a shared machine with other builds running):
  * **Pictures.** The Create page with `sdl` and with `own` at 1160 x 752 differs only in the footer's "(own renderer: opengl)" label
    (4,154 of 872,320 pixels, all inside that 312 x 18 box); at 760 x 500 (footer out of view) the two are **identical** (0 pixels).
  * **Input.** The click and "abc" reached ImGui (input log), the search box shows "abc", a click on Play opens the Play page.
  * **Frame time** (building, drawing and presenting a frame on the Create page, median of 300 frames, vsync off, two runs):
    `sdl` 27.98 and 20.02 ms, `own` 19.96 and 17.24 ms (p95: `sdl` 41.4 / 35.8, `own` 40.6 / 32.2). `own` was faster in both runs here,
    but both numbers are mostly Mesa's CPU rasteriser and the machine was busy, so this says nothing yet about a real GPU on Windows:
    "faster and up to par" is **met on this Linux machine only, unverified where it matters**.
* **Not done / unverified.** Nothing ran on Windows (`main.cpp`, the seam, the GL painter and the Win32 cursor code passed a MinGW
  syntax check only). No real GPU. No IME candidate window position (`Platform_SetImeDataFn` is not set). No real desktop with a
  window manager. The default stays `sdl` until the owner approves (rule 17).

### Step 2c. Launcher: the gaps `own` left (branch `claude2/sdl-launcher-own-gaps`, on top of 2b)

**Status (2026-11-16): built and tested on Linux under Xvfb; still off by default (`OKAY_LAUNCHER_RENDERER=own`); not merged; Windows not run.**

* **Chat in its own window.** The chat window is now a second drawing seam (`CreateOwnGfx` / `CreateSdlGfx`, whichever the launcher
  uses) with its own ImGui context, so it works with `own`. Its window comes from the same `okay::platform` backend as the launcher's,
  so its events arrive through the launcher's `PollEvent`; `main.cpp` hands the ones with its window id to it (`Gfx::FeedEvent`,
  close, file drop). Textures are not shared: its images are uploaded again on its painter (render slot 1, as with SDL).
  **A real bug found and fixed:** our OpenGL painter assumed its context stayed current. With two windows the second context stayed
  current and the launcher drew into the chat window. Each GL painter now makes its own context current before a call when another
  painter's is (`engine/src/Platform/GlPainter.cpp`); a test draws two painters in turn and fails without the fix.
* **OkayUI preview.** Its input comes from `okay::platform` events (`shared/OkayUIHostPlatform.hpp`; the collector got plain
  `AddText` / `AddKey` / `AddButtonDown` / `AddWheel`, which the SDL feed now also uses), the mouse from `Backend::MouseState`, and
  with `own` its draw data goes through OkayUI's render interface onto the painter (`okayui/render`, the Painter backend). Our
  physical keys stand in for SDL's layout key codes for the shortcuts (Ctrl+A / C / X / V): same on QWERTY, by position on AZERTY.
* **Tray hook, taskbar flash, icon.** New in `okay::platform`: `Backend::SetNativeMessageHook` (sdl2: `SDL_SetWindowsMessageHook`;
  own Win32: called from the window procedure, so sent and posted messages; elsewhere false), `Window::Flash` (Cancel / Briefly /
  UntilFocused: `SDL_FlashWindow`, Win32 `FlashWindowEx`, X11 the ICCCM urgency hint, cleared after about a second for Briefly and
  when the window gets the focus) and the window icon through `Window::SetIcon` (`okay::SetAppIconOn`). The tray code takes the HWND
  of an own window from `NativeHandle()`.
* **IME candidate position.** The glue sets `Platform_SetImeDataFn` and gives the caret to `Window::SetTextInputArea`: sdl2
  `SDL_SetTextInputRect`; Win32 `ImmSetCompositionWindow` + `ImmSetCandidateWindow` (imm32.dll loaded at run time); X11 the XIM spot
  location, with over-the-spot input (`XIMPreeditPosition`) chosen when the input method offers it (`OKAY_X11_IME_SPOT=0` keeps the
  old root-window style). Xvfb has no input method server, so on X11 the call reports "not taken" there.
* **What `main.cpp` still asks SDL with `own`** (gone with `own` + `own` in step 2d). `SDL_Init(SDL_INIT_VIDEO)` / `SDL_Quit` and the DPI hint (always made); the display
  bounds in `KeepWindowOnScreen` (`SDL_GetNumVideoDisplays`, `SDL_GetDisplayUsableBounds`: work because SDL video is started; needs a
  display API in `okay::platform`); the HWND of an sdl2 window (`SDL_GetWindowWMInfo`); SDL audio for voice. None of them breaks or is
  skipped with `own`. The event loop of the `sdl` renderer is SDL's by design.
* **Counts** (`scripts/sdl_inventory.py`): the launcher's own directory 64 to 41 SDL calls (320 to 278 words); everything it reaches
  114 to 91; ImGui SDL backends 14 to 8 calls.
* **Tests (2026-11-16, Xvfb, Mesa llvmpipe).** `launcher_imgui_renderer`, `launcher_imgui_renderer_gl` (two painters on two sdl2 and
  two own windows match the reference; the IME caret), `platform_state_sdl2` / `_own` (the urgency hint read back from the X server),
  `platform_window_*`, `platform_painter_window`, `platform`, `okayui_host`: all pass. `scripts/launcher_renderer_check.sh`: the chat
  window with `sdl` and with `own` is **identical** (0 of 576,000 pixels differ), a click into it reaches its ImGui, the IME caret is
  handed to the window, the OkayUI What's New page differs only by the footer label (4,154 pixels, as the Create page); the same with
  `OKAY_PLATFORM_BACKEND=own` (our X11 window). Frame time (Create page, median of 300 frames, vsync off, busy machine): `sdl` 10.73
  and 16.11 ms, `own` 8.23 and 16.61 ms in two runs: the same within the noise here; says nothing about a real GPU.
* **Not done / unverified.** Nothing ran on Windows (the Win32 flash, IME, hook and window code passed a MinGW syntax check only). No
  real desktop: no task bar to see the flash, no input method to see the candidate window. The default stays `sdl`.

### Step 2d. Launcher: no SDL at all with `own` + `own` (branch `claude2/sdl-launcher-no-sdl`, on top of 2c)

**Status (2026-11-16): built and tested on Linux under Xvfb; still off by default; not merged; Windows not run.**
With `OKAY_LAUNCHER_RENDERER=own` **and** `OKAY_PLATFORM_BACKEND=own` the launcher makes **no SDL call at all**, from start to
exit (proof below). It logs `launcher: no SDL (own renderer, own platform backend)` once. Every other combination is as before.

* **Start and end.** `SDL_SetMainReady`, the Windows DPI hint, `SDL_Init(SDL_INIT_VIDEO)` and `SDL_Quit` now run only on the paths
  that use SDL: the `sdl` renderer, the `own` renderer on the sdl2 platform backend (that backend starts SDL itself) and a failed
  `own` start that falls back to `sdl` (it starts SDL video just before the SDL window). With `own` + `own`, per-monitor DPI
  awareness comes from `Backend::SetPerMonitorDpiAware` (Win32: `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)`, looked up
  in `user32.dll` at run time; nothing to do on Linux).
* **Displays.** A display list in `okay::platform` (`Backend::Displays`: bounds, usable area, DPI scale, primary first; sdl2, X11
  RandR 1.5 / Xinerama / root window, Win32 `EnumDisplayMonitors` + `GetDpiForMonitor`; `docs/platform_layer.md`, "Displays").
  `KeepWindowOnScreen` uses it (through `WindowReachable` / `CenterOnDisplay`) with the backend that made the window, so the sdl
  renderer still gets SDL's list.
* **Audio.** Voice capture and playback, the voice clips and the chime already opened their devices through
  `okay::platform::AudioDevice`; only the start (`SDL_InitSubSystem(SDL_INIT_AUDIO)`) was SDL's. It now starts audio on the default
  backend: SDL's audio on sdl2 (the same call as before), ALSA on our Linux backend (capture was already there), and **new
  waveOut / waveIn playback and capture on our Win32 backend** (`engine/src/Platform/OwnWin32Audio.cpp`, `winmm.dll` loaded at run
  time; MinGW syntax check only). With `own` + `own` the voice clips use our WAV decoder unless `OKAY_OWN_WAV=0` (the SDL one is
  `SDL_LoadWAV`).
* **What is left in `main.cpp`** (9 SDL calls): the start and end above (only on the SDL paths), the `sdl` renderer's event loop
  (`SDL_PollEvent`, `SDL_free` of a dropped file name: SDL's by design), and the HWND of an sdl2 window for the tray
  (`SDL_GetWindowWMInfo`, only for sdl2 windows; an own Win32 window gives its HWND directly).
* **The no-SDL proof** (`scripts/launcher_no_sdl_check.sh <launcher> <folder>`). A small library is generated from every function
  the launcher's `libSDL2` exports (`nm -D`: 839 functions here) and put in front of it with `LD_PRELOAD`; each function writes
  `SDL CALL: <name>` and ends the process (exit 97). Results (2026-11-16, Xvfb, Mesa llvmpipe):
  * control: the default `sdl` renderer stops at once (exit 97, first call `SDL_GetTicks64`), so the trap works;
  * `own` + `own`, 120 frames on the Create page, then the launcher exits by itself: **exit 0, no SDL call** (start-up and shut-down);
  * `own` + `own`, the busy run: the chat in its own window (saved at 5000,3000 on a 2400 x 1000 screen; the display list brought it
    back to 900,260), a voice clip playing, a voice call (microphone and speaker opened on ALSA's `null` device: `libasound` is
    loaded), a click into the search box and typing "abc", a click on Play: **no SDL call**, still running at the end.
  Screenshots: the Create page, the search box with "abc", the Play page, the chat window.
* **Counts** (`scripts/sdl_inventory.py`): the launcher's own directory 41 to 36 SDL calls (278 to 270 words); everything it reaches
  91 to 86. The "displays" row of the inventory went from GAP to PARTIAL (display modes are left, for the editor and player).
* **Tests.** `platform_display` (the keep-on-screen rule, `Xft.dpi` parsing, the work-area clip), `platform_display_own` (Xvfb: one
  screen; a panel, `Xft.dpi: 144` and two RandR 1.5 monitors set by the test, primary first), `platform_display_sdl2` (sdl2 and own
  report the same screen), `platform_display_own_xinerama` (two Xvfb screens joined by `+xinerama`; Xvfb puts both at 0,0, so the
  test checks the count and sizes, not a side-by-side layout); every existing `platform*`, `launcher*`, `okayui_host`, WAV and voice
  test passes; `scripts/launcher_renderer_check.sh` passes with the default and with `OKAY_PLATFORM_BACKEND=own` (same pictures as 2c:
  the chat window identical, the pages differ only by the footer label).
* **Not done / unverified.** Nothing ran on Windows: the Win32 display list, DPI awareness and waveOut / waveIn audio passed a MinGW
  syntax check only (so did `main.cpp`). No real multi-monitor desktop (the monitors were made on Xvfb). The default stays `sdl`.

### Step 3. Move the call sites the seam already covers (the "HAVE" rows)

**What.** About 290 call sites in the launcher and editor are plain SDL calls for something `okay::platform` has a function for: window size, position, show,
hide, raise, restore, fullscreen; mouse state; key state; text input; clipboard; the event loop; init and shutdown. The inventory's section 4b lists them.
**Blocker found by the inventory.** The apps keep a raw `SDL_Window*`, and every `Window` method needs a `Window` object, which only `Backend::MakeWindow` makes.
So until the apps make their windows through the seam, no `Window` method can be called on their windows (60 call sites wait for this).
**Bridge.** `okay::platform::WrapSdl2Window(void*)` (see "Step 0" below): a non-owning `Window` over a window the app made itself. It lets each call site move on
its own, in small safe edits, while the window is still created by SDL. It is temporary and goes away in step 6.
**Size.** Medium for the launcher (small edits, many places, one file other agents also edit). The editor's `main.cpp` is about 48,000 lines and edited by several
agents at once: do it in small pull requests, one region at a time, after asking who is editing it.
**Real Windows.** Behaviour must be identical on the default backend: minimise and restore the launcher and the chat window, a message arriving while minimised
flashes the taskbar, dropping a file on the chat window sends it, window position and size are remembered across a restart.

### Step 4. Drawing: our own renderer backend for Dear ImGui, and the apps' `SDL_Renderer` calls onto `okay::platform::Painter`

**What.** Launcher 57 and editor 126 `SDL_Renderer` calls, plus `ImGui_ImplSDLRenderer2_*` (launcher 7, editor 9).
**How.**
* Move the apps' own `SDL_Renderer` calls onto `Painter` (it can wrap their renderer: `WrapSdl2Renderer`). The inventory lists what `Painter` lacks: render targets (editor 3),
  raw geometry (1), flush (2), reading the draw colour (1), the Direct3D 11 device behind the renderer (1, the editor's interop), the renderer-driver choice (4).
* Write our own ImGui renderer backend: it turns Dear ImGui's draw lists (`ImDrawData`, documented in `imgui.h`) into `Painter::Geometry` and `Painter::Copy` calls with the
  clip rectangle and texture of each command. It is small (a few hundred lines). Write it from the documented `ImDrawData` structure and our `Painter` API, **not from
  `imgui_impl_sdlrenderer2.cpp`** (rule 19a).
**The hard part is speed on Windows.** Today the editor's UI is drawn by SDL's GPU renderer. Our software painter draws on the CPU; it is fine for the player and tests, but a
full editor window at 1080p on the CPU will be slow. The own GPU path needs OkayUI's OpenGL 3.3 or Direct3D 11 render backends (they exist, tested on Mesa and Wine only, wired nowhere,
never run on a real GPU) driven from the ImGui draw data. **This is the biggest unknown on the whole list: very large until measured on real Windows hardware.**
**Test headless.** Draw the same ImGui frame through the SDL renderer and through our backend onto the software painter and compare against a stated tolerance
(the same method `okayui_painter` already uses).
**Real Windows.** Look at the launcher and the editor side by side with the SDL version (fonts, colours, clipping, scroll areas, the 3D view texture), then the frame rate with
the editor's scene view open.

### Step 5. Our own Dear ImGui platform backend (events, cursor, clipboard, text input, window)

**Status (2026-11-16).** Written for the launcher in step 2b (`OKAY_LAUNCHER_RENDERER=own`) and, behind `OKAY_EDITOR_PLATFORM=own`, used by the
editor too (stage E1, section "Editor" below); the one shared copy is `shared/ImGuiOwnPlatform.*`. Not done: multi-window panels (viewports), mouse capture
while dragging. Both off by default, neither run on Windows.
**What.** Replace `ImGui_ImplSDL2_*` (launcher 7, editor 8). It feeds `okay::platform::Event` into `ImGuiIO`: mouse, wheel, keys (a table from our `Key` to `ImGuiKey`), typed text,
focus, window size and scale, the cursor shape (needs `SetCursor` from step 1), the clipboard, and drop. About 500 lines plus a test that feeds synthetic events and checks
`ImGuiIO` and the hovered widget. Write it from `imgui.h`'s documented input API, not from `imgui_impl_sdl2.cpp`.
**Open question: the editor's multi-window mode.** The editor sets `ImGuiConfigFlags_ViewportsEnable` when its "multi window" option is on, and has its own SDL code for the extra windows
(`editor/main.cpp` around line 47471). A platform backend for viewports also creates, moves and destroys those windows. Plan: ship the first version without the multi-window option on
the own backend (it stays on SDL), then do viewports as its own sub-step.
**Size.** Large (the editor's keyboard, IME and docking behaviours are where it will take time).
**Real Windows.** Type in every kind of text field, including a non-English layout and an IME; copy and paste text; the mouse cursor shapes over resize edges and text fields; drag a file
into the editor; mouse wheel and touchpad scrolling; two monitors; display scaling 125% and 150%.

### Step 6. The programs create their windows and run their event loops through the seam

**What.** `SDL_CreateWindow` (launcher 2, editor 3), the event loops (`SDL_PollEvent`), the editor's GL contexts (22 calls; needs `CurrentGLWindow` and `CurrentGLContext`, a GAP of 8 call
sites), and removing `WrapSdl2Window`. After steps 3 to 5 this is mostly deletion.
**Size.** Medium. **Real Windows.** The whole programs, on both backends, for a full session: launcher, editor, play mode, a built game.

### Step 7. Real Windows and real desktop runs (every step, not only at the end)

Nothing about the own Win32 backend has run on real Windows (Wine only). That is the largest risk in the whole plan, because a bug found at the end is expensive. So **each** step above
ends with a Windows run of its list, and the own backend gets a first real run (player, own backend, `docs/try_it.md` section 11) as soon as step 1 lands. Also needed once: a real Linux
desktop with a window manager, a file manager drop, an input method, and a second monitor. macOS is parked (rule 14); the web (Emscripten) build still uses SDL2 from Emscripten's ports and
has no own backend, so SDL2 cannot be deleted from the repository until the web path has one or the owner decides to keep it for the web.

### Step 8. Flip and delete (the owner's decision)

Only after the replacement criteria in `docs/own_code_roadmap.md` hold: make `okay::platform` own the default per platform with the owner's written approval, announce it one release ahead, delete
SDL2, the CMake `find_package(SDL2)`, the CI and release downloads, `dist/SDL2.dll` and the bundled copy one release later. The build and CI files that name SDL2 are listed in the inventory (section 6).

## Editor

The editor is the biggest SDL user left. It is built differently from the launcher, which matters for the plan:

* **Its UI is drawn by `SDL_Renderer`** (Direct3D 11 on Windows, OpenGL on Linux), through Dear ImGui's `SDL_Renderer` backend. Every
  picture a panel shows (the Scene and Game views, thumbnails, icons, previews) is an `SDL_Texture`, and on Windows the Direct3D 11
  renderer draws the views straight into SDL's device (`SDL_RenderGetD3D11Device`).
* **Its 3D views render with OpenGL on a separate, hidden 16 x 16 window** ("okay-gl") with its own GL 3.2 compatibility context and a
  24-bit depth buffer. The views render off screen there and are read back into an `SDL_Texture`. The UI never draws with that context.
* **ImGui's multi-window mode** (View > multi-window: panels dragged out become their own OS windows) uses ImGui's SDL2 platform
  backend for the windows and the editor's own `SDL_Renderer` code for drawing them.

So the main window must stay an SDL window for as long as `SDL_Renderer` draws it. That is stage E2 (below).

### The inventory (2026-11-16)

Counted with `scripts/sdl_inventory.py`'s scanner (comments and strings removed; **calls / words**, words also count types,
constants and event names), grouped by kind. Before = the launcher branch this work starts from (`e2b67cce`); after = stage E1.
`editor/` is the whole directory (including `VsGraphShot.cpp`, the `--vs-shot` screenshot tool, and, after E1, the seam's SDL side
`EditorPlatformSdl.cpp`); `main.cpp` alone shows what moved out of it.

| group | `editor/` before | `editor/` after E1 | `main.cpp` before | `main.cpp` after E1 |
|---|---:|---:|---:|---:|
| window (create, focus, show / raise, size, id, displays, DPI hint) | 27 / 49 | 20 / 41 | 25 / 43 | 13 / 18 |
| GL context (attributes, create, make current, current context, proc address) | 23 / 34 | 18 / 28 | 23 / 34 | **0 / 0** |
| events (poll, pump, push, event filter, window events) | 5 / 13 | 5 / 14 | 4 / 11 | 4 / 11 |
| file drop | 0 / 3 | 0 / 3 | 0 / 3 | 0 / 3 |
| keyboard (keys, key state, text input) | 7 / 58 | 7 / 58 | 6 / 33 | 6 / 33 |
| mouse (state, relative mode, touch) | 14 / 30 | 14 / 30 | 14 / 30 | 14 / 30 |
| cursor (show / hide, warp) | 3 / 6 | 3 / 6 | 3 / 6 | 3 / 6 |
| clipboard | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| timers | 3 / 3 | 3 / 3 | 3 / 3 | 3 / 3 |
| audio | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| threads | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| gamepad (device events) | 0 / 2 | 0 / 2 | 0 / 2 | 0 / 2 |
| Dear ImGui SDL2 platform backend (`ImGui_ImplSDL2_*`) | 8 / 10 | 8 / 10 | 4 / 5 | **0 / 0** |
| Dear ImGui `SDL_Renderer` backend (`ImGui_ImplSDLRenderer2_*`) | 9 / 11 | 9 / 11 | 5 / 6 | 5 / 6 |
| `SDL_Renderer` drawing (textures, surfaces, targets, read-back, driver hints) | 109 / 264 | 109 / 265 | 89 / 206 | 89 / 206 |
| other (init / quit, errors, base path, libc helpers, message box, BMP, version, integer types) | 24 / 78 | 24 / 79 | 18 / 62 | 17 / 61 |
| **all** | **232 / 561** | **220 / 550** | **194 / 444** | **154 / 379** |

Notes. The editor's clipboard in play mode goes through `shared/SdlClipboard.hpp` and its monitor list through `shared/SdlDisplays.cpp`
(counted under `shared/`, not here). Audio and controllers already go through `okay::platform`. The 28 calls of `EditorPlatformSdl.cpp`
are the default (`sdl`) side of the seam, on purpose, and go when SDL goes; `VsGraphShot.cpp` (27 calls) is a developer tool with its
own SDL window.

### Stage E1: the window, ImGui's platform side and the views' GL context behind a seam (branch `claude2/sdl-editor-seam`, on top of 2d)

**Status (2026-11-16): built and tested on Linux under Xvfb, without the editor; off by default; not merged; never run in a real editor
session; Windows not run.** `OKAY_EDITOR_PLATFORM=own` switches it on; anything else (no variable, `sdl`) is the old path. The editor
prints `editor: platform ...` once at start (stderr and the Console).

* **The seam** (`editor/EditorPlatform.hpp`, no SDL types): the main window, Dear ImGui's platform side (init, events, new frame,
  shut-down) and the views' hidden GL context (create, begin / end around each use, the GL function lookup, destroy).
  * `sdl` (`editor/EditorPlatformSdl.cpp`): `SDL_CreateWindow`, `ImGui_ImplSDL2_*` and `SDL_GL_*`, what `main.cpp` did. One small
    difference: creating the GL context now gives the UI's context back at once and the editor then makes the views' context current
    to load the GL functions (two more `SDL_GL_MakeCurrent` calls at start-up).
  * `own` (`editor/EditorPlatform.cpp`): ImGui is fed by **our own glue** (`shared/ImGuiOwnPlatform.*`, the launcher's, moved there so
    both programs share one copy). `main.cpp` still reads SDL's queue (its shortcuts, file drops and play-mode input read the
    `SDL_Event`), and each event is turned into an `okay::platform::Event` by the new `okay::platform::TranslateSdl2Event` before the
    glue sees it. The main window is made by okay::platform's sdl2 backend (it has to be an SDL window while `SDL_Renderer` draws it).
    **The views' GL window and context come from `okay::platform::Default()`**: SDL's by default, and **our own GLX (Linux) or WGL
    (Windows) code with `OKAY_PLATFORM_BACKEND=own`**. When `SDL_Renderer` draws with OpenGL (Linux), the UI's context is handed back
    through SDL after each use: SDL keeps its own note of which context is current and skips a switch it thinks is a no-op, so the
    seam clears that note first (the test fails without it: the UI drew nothing).
* **`main.cpp` changes** (small): window creation, ImGui's platform calls, the three GL context sites and the start-up GL block go
  through the seam; the window id, focus, show and raise go through okay::platform's view of the window (`Window::Id`, `IsFocused`,
  `SetInputFocus`, `Show`, `Raise`: the same SDL calls on `sdl`).
* **What `own` does differently** (known, by design for now):
  * no multi-window panels (our glue has no platform windows): the option is ignored with `own`, panels stay inside the editor;
  * keys are physical keys (the key's place on the keyboard), not the layout's letter: ImGui's own shortcuts in text fields (Ctrl+A,
    C, V, X, Z, Y) follow the key position on AZERTY or QWERTZ (the editor's own menu shortcuts read SDL and are unchanged);
  * no mouse capture while dragging: a drag that leaves the window (a splitter, a slider) may stop at the edge;
  * where the OS reports natural scrolling as "flipped", our wheel is turned back to "up = up" (ImGui's SDL2 backend leaves it);
  * text input stays on all the time, as SDL starts it (the editor's play mode and OkayUI fields rely on that).
* **Tests** (2026-11-16, Xvfb, Mesa llvmpipe). `editor_platform` and `editor_platform_own` (`tests/test_editor_platform.cpp`, which
  links only the seam, the glue, ImGui, okay::platform and SDL, not the editor):
  * both sides: the main window (hidden, resizable, its size and id through okay::platform); the views' context is GL 3.2 or newer
    with the compatibility profile, a framebuffer object with a colour and a 24-bit depth buffer is complete, a clear reads back
    green; after it `SDL_Renderer`'s own GL context is current again and the window still draws (a red clear read back); ImGui gets a
    **real pointer move from the X server** (a warp) and queued button, wheel, key, modifier and text events; the clipboard both
    ways (ImGui to the system and back); the cursor shape ImGui asks for reaches the window; a dropped file is left to `main.cpp`
    (not eaten, not freed) and translated with its path;
  * `_own` runs the same with `OKAY_PLATFORM_BACKEND=own`: the views' window and context are our own X11 / GLX code next to
    SDL's OpenGL renderer;
  * **our glue against ImGui's SDL2 backend**: two ImGui contexts on the same window get the same SDL events (all five mouse
    buttons, whole and fractional wheel steps both ways, Ctrl / Shift / Alt / Super, letters, digits, F-keys, arrows, keypad,
    Enter, Escape, typed text with accents, the euro sign and an emoji, focus lost and gained) for 27 frames: **every frame
    identical** (mouse position, buttons, wheel, keys down, modifiers, typed characters, focus);
  * 113 checks each; a deliberate break (the context hand-back without clearing SDL's note) makes `_own` fail.
  * `editor/main.cpp` passes a syntax-only compile (it holds both sides; the switch is at run time). The new files, the moved glue and
    `SdlBackend.cpp` pass a MinGW syntax check (with Linux SDL headers: there is no MinGW SDL here).
  * The launcher after the glue moved: `launcher_imgui_renderer`, `launcher_imgui_renderer_gl`, `okayui_host`, every `platform*`
    suite and `launcher_wav_window` pass; `scripts/launcher_renderer_check.sh` gives the same results as before the move (the Create
    page differs only by the footer label, 4,154 pixels; the small window and the chat window are identical; typing, clicks and the
    IME caret reach ImGui).
* **Counts.** `editor/main.cpp` 194 to 154 SDL calls (444 to 379 words); the whole `editor/` directory 232 to 220 (the seam's `sdl` side
  holds 28). In `main.cpp` the ImGui SDL2 platform backend and the GL context calls are gone (4 and 23 to 0).
* **Not done / unverified.** No real editor session (the editor is too big to build on this machine's memory: only the syntax check).
  Nothing on Windows: the WGL context next to SDL's Direct3D 11 renderer is unrun. No multi-window panels on `own`. The default stays
  `sdl` until the owner approves (rule 17).

### Stage E2: the editor's drawing behind a seam, our own renderer as an opt-in (branch `claude2/sdl-editor-gfx`, on top of E1)

**Status (2026-11-16): built and tested on Linux under Xvfb, without the editor; off by default; not merged; never run in a real editor
session; Windows not run.** `OKAY_EDITOR_RENDERER=own` switches it on, **and only together with `OKAY_EDITOR_PLATFORM=own`** (the platform
seam then makes the main window with an OpenGL context). With the sdl platform, or with no variable, or `sdl`, the editor draws as before;
asking for `own` without the own platform is logged and ignored. The editor prints `editor: renderer ...` once at start (stderr and the
Console), next to the `editor: platform ...` line.

**The 89 `SDL_Renderer` calls of `main.cpp`, grouped by what they do** (and the 5 calls of ImGui's `SDL_Renderer` backend). Counted with
the inventory's scanner (calls only). "Left" are the calls still in `main.cpp` after E2.

| group | before | left | where they went / why they are left |
|---|---:|---:|---|
| textures: create, update, destroy, size (thumbnails, icons, previews, the 3D views, the splash logo, a surface turned into a texture) | 27 | 6 | the seam (`EditorGfx::CreateTexture / UpdateTexture / DestroyTexture / QueryTexture`, from RGBA pixels). Left: the Direct3D 11 render-target texture (1) and the multi-window panels' own copies of ImGui's textures (5) |
| blend and sampling modes of textures | 16 | 4 | folded into the texture description (`TexDesc`: blended or not, linear or nearest, streaming); one sprite filter switch is `SetTextureLinear`. Left: the same two places (2 + 2) |
| render targets | 3 | 3 | left: the Direct3D 11 "draw the views straight into SDL's texture" path (Windows, sdl only) |
| read-back (screenshots: `OKAY_UI_SHOT`, the diagnostics shot) | 4 | 0 | `EditorGfx::ReadPixels` |
| the frame: make the renderer, vsync, output size, scale, clear, present | 24 | 7 | the seam (`CreateEditorGfxFromEnv`, `SetVSync`, `OutputSize`, `BeginFrame`, `Present`). Left: the multi-window panels' own renderers (7) |
| direct drawing (draw colour, a point, flush, clip, raw geometry) | 9 | 9 | left: the Direct3D 11 path's render-target probe (6) and the multi-window panels' draw-data replay (3) |
| the Direct3D 11 device behind the renderer | 1 | 1 | left; now only asked when there is an `SDL_Renderer` (on `own` the Direct3D 11 views use their own device and read back) |
| the renderer driver hint (`SDL_HINT_RENDER_DRIVER`) and the renderer's name (`SDL_GetRendererInfo`) | 5 | 0 | the seam's sdl side (`EditorGfxSdl.cpp`), `EditorGfx::DriverName` |
| **all** | **89** | **30** | |
| Dear ImGui's `SDL_Renderer` backend (`ImGui_ImplSDLRenderer2_*`) | 5 | 0 | `EditorGfx::InitImGui / ShutdownImGui / NewFrame / RenderImGui / UpdateImGuiTexture` |

So what is left in `main.cpp` is exactly two blocks, both used only on the sdl renderer: the multi-window panels (`OkayVp*`, around line
47870; they are off with the own platform anyway) and the Direct3D 11 zero-copy views (Windows; `g_d3dDirect` is never set on `own`).

* **The seam** (`editor/EditorGfx.hpp`, no SDL types; modelled on the launcher's `LauncherGfx`): the painter that draws the main window
  (an `okay::platform::Painter`), textures from RGBA pixels, the frame (scale and clear, present, vsync, output size, read-back) and Dear
  ImGui's renderer side (init, new frame, draw, one texture request, shut-down). Every texture a panel shows is now an
  `okay::ui2d::Tex` of that painter (ImGui's `ImTextureID`): `GetThumb`, the Project panel's pictures, primitive and prefab thumbnails,
  the sprite editor, the TTF atlas of the UI preview, the first-person hands preview, the Scene / Game / Camera / Animation views.
  * `sdl` (`editor/EditorGfxSdl.cpp`): `SDL_Renderer` made exactly as `main.cpp` made it (the Direct3D 11 hint on Windows unless
    `OKAY_RENDER_DRIVER`, the fallbacks), okay::platform's sdl2 painter over it (each painter call is the SDL call `main.cpp` made:
    `SDL_CreateTexture` in ABGR8888, `SDL_UpdateTexture`, the blend and scale modes, `SDL_RenderClear`, `SDL_RenderReadPixels`, ...),
    and ImGui's `SDL_Renderer` backend.
  * `own` (`editor/EditorGfx.cpp`): **our OpenGL painter** (`engine/src/Platform/GlPainter.cpp`) on the main window's own GL context and
    **our own ImGui renderer**, moved from `launcher/ImGuiOwnBackend.cpp` to `shared/ImGuiOwnRenderer.*` so the launcher and the editor
    share one copy (the launcher keeps its names). No `SDL_Renderer`, no ImGui `SDL_Renderer` backend. OkayUI (the Test UI overlay and
    a playing game's `ui_*` widgets) is drawn on the painter through OkayUI's render interface, as the launcher does.
* **The 3D views keep the copy.** They still render in their own hidden GL context (E1's `BeginViewportGL` / `EndViewportGL`), read back
  to RGBA and upload into a painter texture. Sharing the GL texture directly would need the two contexts to share objects (made with
  sharing on, on every backend), which is a bigger change; it was measured instead. Under Xvfb with Mesa's software GL (llvmpipe), at
  1920 x 1080: read back about 1.6 to 1.7 ms, upload into the UI texture about 0.7 to 0.8 ms (medians of 15), the same on both sides (upload: sdl
  0.78 ms, own 0.71 ms). That is a CPU-only GL, so it says nothing about a real GPU; on Windows the Direct3D 11 views (sdl only) avoid the copy.
* **Other files:** `UIPreviewPainter` (the UI preview's TTF atlas and texture sizes) and `FpHandsWindow` (the hands preview) use the
  painter (`EditorPainter()`) instead of `SDL_Renderer`; `GetThumb`, `GetSceneThumb` and the item icons return painter textures.
  `EditorPlatform::CreateMainWindow` takes an `opengl` flag and `MainWindowBackend()` names the backend that made the window.
* **Tests** (2026-11-16, Xvfb, Mesa llvmpipe). `editor_gfx` and `editor_gfx_own` (`tests/test_editor_gfx.cpp`, which links only the
  two seams, the shared ImGui code, ImGui, okay::platform, the engine library for the GL painter, and SDL, not the editor):
  * the switch: the values, `own` only with the own platform (all six combinations through `CreateEditorGfxFromEnv`, with the log line);
  * the same editor-like frame through each side, each on its own platform seam: the views' hidden GL context draws a picture into a
    framebuffer object and reads it back each frame, then the UI's context is current again and it goes into a streaming, linear texture;
    a thumbnail (nearest, blended), a texture with blending off, a texture destroyed and made again at a new size, their sizes; ImGui
    shapes with alpha, a clip rectangle, text (ImGui's font atlas through the renderer's texture requests) and the four textures as images;
    then scale and clear, ImGui, read-back, present. **Checked at 14 known pixels on each side (exact colours), and sdl against own pixel by
    pixel: identical** (0 of 76,800 pixels differ). `_own` runs it with the views' GL window and context from our own X11 / GLX code.
  * A deliberate break (the views' context not handed back after use) makes every pixel check on the own side fail.
  * 166 checks each.
  * `editor_platform`, `editor_platform_own` still pass. The launcher after the renderer moved: `launcher_imgui_renderer`,
    `launcher_imgui_renderer_gl`, `okayui_host`, every `platform*` suite pass; `scripts/launcher_renderer_check.sh` gives the same numbers as
    before (the Create page differs from sdl by 4,154 pixels, the footer label; the small window and the chat window are identical; clicks,
    typing and the IME caret reach ImGui) and `scripts/launcher_no_sdl_check.sh` passes (no SDL call with own + own).
  * `editor/main.cpp` passes a syntax-only compile with Linux g++ and with MinGW (the MinGW check found one Windows-only line, fixed); the changed editor files compile; the new and changed files
    pass a MinGW syntax check (with Linux SDL headers: there is no MinGW SDL here).
* **Counts.** `editor/main.cpp` 154 to 89 SDL calls (379 to 229 words); its `SDL_Renderer` calls 89 to 30 and ImGui's
  `SDL_Renderer` backend 5 to 0. The whole `editor/` directory 220 to 161 calls (550 to 386 words): the seam's sdl side
  (`EditorGfxSdl.cpp`, 16 calls) is SDL on purpose and goes when SDL goes.
* **Not done / unverified.** No real editor session: the editor is too big to build on this machine's memory, so nothing here has drawn a
  real editor frame with `own` (only the syntax check and the test program). Nothing on Windows: our GL painter on a Windows editor window,
  WGL next to it, and the read-back views instead of the Direct3D 11 zero-copy ones are unrun. On `own` there are no multi-window panels
  and no Direct3D 11 zero-copy views. The main window is still an SDL window (`main.cpp` reads SDL's events: stage E3). The default stays
  `sdl` until the owner approves (rule 17).

### What comes next for the editor

* **E2 (the rest). Drawing without `SDL_Renderer` everywhere.** Stage E2 above did the frame, ImGui and every texture from pixels. Left:
  the Direct3D 11 "draw the views into SDL's device" path (our own Direct3D 11 painter, or read-back on Windows too; on `own` it is
  read-back today), and the multi-window panels' own renderers (with E4). Then the main window can come from `okay::platform` directly
  (our GL painter on our own window) once its events come from `Backend::PollEvent` (E3). Medium.
* **E3. The event loop and input through the seam.** `main.cpp`'s shortcuts, play-mode keyboard (`EditorPlayInput.hpp`), relative
  mouse, warp, cursor show / hide, touch, drops and controller events read `okay::platform::Event` instead of `SDL_Event`. Medium.
* **E4. Multi-window panels on `own`** (ImGui's platform windows through `okay::platform`), then the rest (`SDL_Init` / `SDL_Quit`,
  the base path, the message box, `SdlDisplays.cpp`, `SdlClipboard.hpp`). Then `own` + `own` can be proved SDL-free like the launcher.

## What can run in parallel, and the critical path

* Step 2 (threads) touches only the launcher's threading and needs nothing else. Done on `claude2/launcher-own-threads` (see Step 2); a Windows run is left.
* Steps 1 and 3 can run together (3 needs the bridge below).
* The critical path is **4 -> 5 -> 6 -> 7**: drawing and the ImGui backends, then the windows, with a real Windows run at each stage. Within it, the GPU painter for the editor is the one
  piece nobody can size yet.
* Cheap mechanical work for any idle agent (no seam needed): the 23 `SDL_sinf` / `SDL_cosf` / `SDL_sqrtf` / `SDL_floorf` calls in `okayui/src/OkayUI.cpp` become `<cmath>`; the
  `Uint8` / `Sint16` typedefs (333 words) become `<cstdint>` names; `SDL_free` / `SDL_strdup` / `SDL_zero` become libc; the SDL version checks go. (`okayui/` and the editor are edited by other agents: coordinate first.)

## Step 0: what the branch `claude/sdl-inventory` adds (in progress)

Planned and being built with tests first; this section is filled in with the final list and results when the code is done.

* The inventory and its generator (`docs/sdl_inventory.md`, `scripts/sdl_inventory.py`).
* `okay::platform::WrapSdl2Window` (the bridge for step 3) and five small seam functions that the launcher or editor use and that were missing: window state queries
  (`IsMinimized`, `IsVisible`, `HasMouseFocus`), focus queries (`KeyboardFocusWindowId`, `MouseFocusWindowId`, `Window::SetInputFocus`), `IsRelativeMouse`,
  `GlobalMouseState` and `PumpEvents`. Each on the sdl2 backend, the own X11 backend and the Win32 backend (Win32 compile-checked only).
* A handful of launcher call sites moved onto them, only where the behaviour with the default SDL backend is identical.
