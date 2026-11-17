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
| player | 0 / 2 in `player/` (was 5 / 199) | none on its own path: with `OKAY_PLATFORM_BACKEND=own` the player makes **no SDL call** (proved under Xvfb, section "Player" below); the 2 words left are the casts on the default path's SDL display and OkayUI calls |
| launcher | 743 / 1,199 in its own files | **613 are threads, mutexes and atomics (83%)**; 57 `SDL_Renderer` drawing; 14 Dear ImGui SDL backends; about 60 window, input and other platform calls |
| editor | 229 / 556 | 126 `SDL_Renderer` drawing, 22 GL context, 25 window, 17 mouse, 17 Dear ImGui SDL backends, the rest small (stages E1 to E3, below in "Editor": `main.cpp` 194 to 50 calls; with all three switches `own` the editor makes **no SDL call** on Linux, proved under Xvfb) |
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
  real editor frame with `own` (only the syntax check and the test program). (Since stage E3 the whole editor has run with `own` + `own` under Xvfb: see below.) Nothing on Windows: our GL painter on a Windows editor window,
  WGL next to it, and the read-back views instead of the Direct3D 11 zero-copy ones are unrun. On `own` there are no multi-window panels
  and no Direct3D 11 zero-copy views. The main window is still an SDL window (`main.cpp` reads SDL's events: stage E3). The default stays
  `sdl` until the owner approves (rule 17).

### Stage E3: the main window and its events with no SDL (branch `claude2/sdl-editor-no-sdl`, on top of E2)

**Status (2026-11-16): built and run as the whole editor on Linux under Xvfb (software OpenGL), with an SDL trap: no SDL call; off by
default; not merged; Windows not run.** With `OKAY_EDITOR_PLATFORM=own` and `OKAY_EDITOR_RENDERER=own` the main window is made by
`okay::platform::Default()` and the editor reads its events from that backend instead of SDL's queue. With `OKAY_PLATFORM_BACKEND=own` as
well, the editor makes **no SDL call at all** (no `SDL_Init`, no `SDL_Quit`) and says `editor: no SDL (own platform, own renderer, own
platform backend)` at start. Every other combination is unchanged; the default (no variable) makes the same SDL calls as before.

* **The seam** (`editor/EditorPlatform.*`): on `own`, `CreateMainWindow(..., opengl = true)` makes the window with `Default()` (it starts
  that backend's video when it is our own) and `EventsFromPlatform()` turns true; `ProcessEvent(const okay::platform::Event&)` feeds ImGui.
  The views' GL context is handed back to the main window's own context (`Window::MakeCurrent`) instead of through SDL. On `own` with
  `SDL_Renderer` (E1 / E2 with `OKAY_EDITOR_RENDERER` unset) nothing changed: SDL window, SDL queue.
* **`main.cpp`** (every direct SDL use the own path would reach):
  * the event loop: a second loop over `Backend::PollEvent` with the same handling as the SDL one (Esc edge, focus on click, ImGui,
    quit and window close, controllers added and removed, focus lost, Play text, wheel and touch, OkayUI text and Backspace, file
    drops; the drop import is one shared lambda); the editor's own test drop (`OKAY_UI_DROP`) is queued for that loop instead of
    `SDL_PushEvent`;
  * keyboard state for Play (`IsKeyDown`, `KeyForChar` for the layout's letters), mouse state and buttons (`MouseState`, `MouseMask`),
    relative mouse mode, its motion and the getter (`SetRelativeMouse`, `RelativeMouseState`, `IsRelativeMouse`), cursor show / hide
    (`ShowCursor`), warp (`WarpMouse`), text input (`StartTextInput`), pumping events on the splash (`PumpEvents`), the "no window has
    the keyboard" focus check (`KeyboardFocusWindowId`, `MouseFocusWindowId`), touch-device count (`TouchDeviceCount`, new): all go to
    the main window's backend (`EdInput()`, which is okay::platform's sdl2 backend on the default path: the same SDL calls as before);
  * window: title, size, restore / raise / show and the refresh rate through `okay::platform::Window`; the icon through
    `SetAppIconOn`; the first-run UI scale from `Backend::Displays()` (the display under the window and its scale);
  * timers: `SDL_GetTicks64` to `okay::platform::TicksMs` (the gesture clock and "saved" time);
  * the game's display settings in Play: the monitor list and window modes through `shared/PlatformDisplays.hpp` (new; one mode per
    monitor, its desktop size, as okay::platform has no list of display modes yet, and exclusive fullscreen is desktop fullscreen);
  * the Play clipboard through `shared/PlatformClipboard.hpp` (new) and OkayUI's text fields through `OkayUI::SetSystemClipboard` (new);
  * the executable's folder (`Backend::BasePath`) and the "open it from the launcher" message box (`Backend::ShowErrorBox`);
  * start and end: `SetPerMonitorDpiAware` and `Backend::Init` instead of `SDL_SetMainReady` / `SDL_SetHint` / `SDL_Init`; no `SDL_Quit`.
* **New in `okay::platform`** (each with its SDL2, X11 and Win32 side):
  * `Backend::TouchDeviceCount()`: sdl2 `SDL_GetNumTouchDevices`; own X11 and Win32 say 0, because they deliver no finger events;
  * file drops on X11 (they were missing: the own Linux backend never sent `DropFile`): the XDND protocol, version 5, written from
    freedesktop.org's description (XdndAware on every window; Enter / Position / Status / Leave / Drop / Finished; the file list as
    `text/uri-list`, parsed by `own::PathsFromUriList`: local `file:` URIs, percent-decoded); sdl2 and Win32 already had drops;
  * `Window::RefreshRateHz()` on X11 (RandR: the mode of the CRTC under the window, pixel clock / (horizontal total x vertical total))
    and on Win32 (`EnumDisplaySettingsW` for the window's monitor); sdl2 already had it;
  * `Backend::ShowErrorBox` on X11: a small box of our own (core X drawing on its own connection, title, lines of text, an OK button;
    Return, Escape or the close button end it); it only logged before. sdl2 and Win32 already had real boxes.
* **What the own path does differently** (known): display modes (the game's resolution list shows the desktop size only; exclusive
  fullscreen is desktop fullscreen; fixed on `claude2/sdl-player-no-sdl`, below); no touch input on the own backends; the editor's "open from the launcher" box has the error look on
  every backend; on X11 the refresh rate is whatever RandR reports (Xvfb reports none: 0, so Play's frame pacing falls back as it does
  when SDL reports none). Still no multi-window panels (E4; done on `claude2/sdl-editor-viewports`).
* **Proof: `scripts/editor_no_sdl_check.sh`** (the launcher's check made for the editor). A trap library defines every function
  libSDL2 exports (839 here); each one prints `SDL CALL: <name>` and exits with 97; it is preloaded in front of libSDL2.
  Run on 2026-11-16 with the full editor (`-O1`), opening `examples/physics/physics_playground.okayscene` on a 1600 x 900 Xvfb:
  1. control, the default editor under the trap: stops at its first call (`SDL_SetMainReady`), exit 97: the trap works;
  2. own x 3, clean run: the scene opens, the editor runs 15 s, then its window gets `WM_DELETE_WINDOW` (what a close button sends):
     **exit 0, no SDL call** from start to exit;
  3. own x 3, busy run: a click in the Scene view, a click on a ball, a Hierarchy row, typing `crate` into the Hierarchy search, the
     2D / 3D switch, a right-mouse-button fly with the mouse moved and W held, Ctrl+P (Play), the Game view's "Capture mouse"
     (relative mouse mode) with the mouse moved, Esc (relative mode off, pointer parked), Ctrl+P (Stop): **no SDL call**, still running;
  4. the default editor through the same steps, without the trap, for the pictures. Pixels that differ, default against own, on the
     1,440,000-pixel screen: start 546, after the clicks and typing 564, before the fly 305, after the fly
     1,063, playing 7,369, captured 55,495, stopped 3,749 (the frame-rate numbers in the menu bar and the
     status bar, the text caret, and a playing physics scene that is a few frames apart; "captured" also has the checkbox's tooltip
     on `own` only, where the pointer is held in the window). The fly changed about 78,000 pixels of the
     view on both; Play changed about 720,000 on both. The pointer while captured: `own` holds it in the
     middle of the window (640, 400: our X11 relative mode warps it back there), SDL leaves it where it was (XInput2 raw motion);
     after Esc `own` puts it in the middle of the Game view (552, 349), as the editor asks. Under a heavy machine load (5 to 6 frames a
     second) one run of the default editor missed the checkbox click and the Stop key: the steps are timed, not synchronised.
     (The screenshots are kept outside the repository.)
* **Tests** (2026-11-16, Xvfb). `platform_editor_calls` (no display: the uri-list parser, the display-settings helper on a fake window
  in each mode, the clipboard helper, the touch default; 22 checks) and `platform_editor_calls_own` (our X11 backend: this test plays an
  XDND drag source on its own connection and drops two files, one with a space, then a drag with no files (refused, finished empty);
  the refresh rate against RandR's own `XRRConfigCurrentRate`; the message box shown in a child process and closed once with Return
  and once with the close button; 14 checks). Passing after the change: `editor_platform`, `editor_platform_own`, `editor_gfx`, `editor_gfx_own`,
  `launcher_imgui_renderer`, `launcher_imgui_renderer_gl`, `okayui_host`, `launcher_join`, `launcher_versions`, `launcher_wav_window`,
  every `platform*` suite (25 of 25), and `scripts/launcher_no_sdl_check.sh` (ALL PASS). `editor/main.cpp`, the seam, the two new
  headers, `OkayUI.cpp`, `SdlBackend.cpp` and `OwnWin32.cpp` pass a MinGW syntax check (with Linux SDL headers). The X11 message box was
  also looked at in a screenshot (the editor's "open it from the launcher" text, OK button).
* **Counts** (`scripts/sdl_inventory.py`, calls / words): `editor/main.cpp` **89 / 229 to 50 / 160**; the whole `editor/` directory
  161 / 386 to 122 / 317. The 50 left in `main.cpp` are the default path's own SDL: the SDL event loop (kept unchanged), the start
  and end, the base path and the message box on the SDL side, the first-run DPI on the SDL side, the multi-window panels' renderers and
  the Direct3D 11 views (both `SDL_Renderer` only), and the self-test's keyboard.
* **Not done / unverified.** Windows: nothing of E3 has run there (the new Win32 refresh rate and the changed files pass a MinGW syntax
  check); our Win32 backend drawing the editor with WGL, its events and drops in a real session are unrun. A real X11 desktop (a window
  manager, a real file manager's drag, a real monitor's refresh rate) is unrun: Xvfb has no window manager and reports no refresh rate.
  No multi-window panels on `own` (E4). The default stays `sdl` until the owner approves (rule 17).

### Stage E4: multi-window panels on our own code (branch `claude2/sdl-editor-viewports`, on top of `claude2/sdl-touch-tests`)

**Status (2026-11-17): built and run as the whole editor on Linux under Xvfb (software OpenGL), with the SDL trap: no SDL call; off by
default; not merged; Windows not run (MinGW syntax check only).** With the three switches (`OKAY_EDITOR_PLATFORM=own`,
`OKAY_EDITOR_RENDERER=own`, `OKAY_PLATFORM_BACKEND=own`; also with only the first two, on okay::platform's sdl2 backend) a panel dragged
out of the editor becomes its own OS window, as on the default editor. `OKAY_EDITOR_OWN_VIEWPORTS=0` keeps the panels inside. The log
line says `...; panel windows on)`.

* **Platform side** (`shared/ImGuiOwnPlatform.*`, `ImGuiPlatformGlue::EnableViewports`): Dear ImGui's `Platform_CreateWindow` /
  `DestroyWindow` / `ShowWindow` / `SetWindowPos` / `GetWindowPos` / `SetWindowSize` / `GetWindowSize` / `GetWindowFramebufferScale` /
  `SetWindowFocus` / `GetWindowFocus` / `GetWindowMinimized` / `SetWindowTitle` / `SetWindowAlpha` on `okay::platform` windows made by
  the main window's backend (hidden until ImGui shows them; borderless, no task-bar button, topmost and "do not take the focus" from
  the viewport's flags). The main viewport is tied to the main window. The monitor list comes from `Backend::Displays()` (read again
  once a second). Events of every panel window reach ImGui: mouse positions in desktop coordinates (the new `Event::screenX / screenY`,
  so a drag that leaves a window or moves it stays exact), keys, text, focus, and the OS asking to close or resize a panel window
  (`PlatformRequestClose` / `PlatformRequestResize`; a decorated panel window moved by a window manager sets `PlatformRequestMove`).
  The pointer leaving every window is not reported while a button is held (dragging a panel out goes past the edge).
  **Mouse-hovered viewport: not reported** (`ImGuiBackendFlags_HasMouseHoveredViewport` off): our backends cannot let the pointer pass
  through the panel being dragged (`NoInputs`), and while a button is held the OS keeps sending the mouse to the window it was pressed
  in, so ImGui's own rule (rectangles and focus order) is used.
* **Renderer side** (`shared/ImGuiOwnRenderer.*`, `ImGuiPainterRenderer::EnableViewports`): `Renderer_RenderWindow` points our GL
  painter at the panel's window (new `Painter::SetTarget`) **with the main window's GL context** (new
  `Window::MakeCurrentWithContextOf`: X11 `glXMakeCurrent` on the panel window, Win32 the owner's pixel format + `wglMakeCurrent`, sdl2
  `SDL_GL_MakeCurrent`), clears it and draws its draw data; `Renderer_SwapBuffers` swaps it and points the painter back at the main
  window. One context, so every texture works in a panel window: ImGui's font, the Project panel's thumbnails, the Scene and Game views.
* **okay::platform additions** (each with its X11, Win32 and sdl2 side): `Window::MakeCurrentWithContextOf`, `Window::SetOpacity`
  (`_NET_WM_WINDOW_OPACITY`, a layered window, `SDL_SetWindowOpacity`), `WindowDesc::skipTaskbar / topmost / noActivate`
  (`_NET_WM_STATE_SKIP_TASKBAR` / `_ABOVE` / `_NET_WM_USER_TIME` 0; `WS_EX_TOOLWINDOW` / `WS_EX_TOPMOST` / `SW_SHOWNA`;
  `SDL_WINDOW_SKIP_TASKBAR` / `ALWAYS_ON_TOP`), `Event::hasScreenPos / screenX / screenY` (X11 `x_root / y_root`, Win32
  `ClientToScreen`, sdl2 window position + x / y). **Win32 change:** `Window::GetPosition / SetPosition` and `WindowDesc::x / y` are now
  the client area's corner, as SDL and our X11 code give it (they were the frame's corner; the mouse and ImGui's panels need the
  client area). Written from the GLX 1.3, WGL and EWMH documents and imgui.h's "Multi-Viewport Support" comments; nothing copied
  from ImGui's example backends.
* **The default (SDL) editor, fixed on the way:** its panel windows showed the editor's pictures as empty boxes (SDL textures cannot
  cross renderers; seen in the screenshots below). `OkayVp_RenderViaMain` (`editor/main.cpp`) now draws each panel window with the
  main renderer into an off-screen target, reads it back and shows that in the panel window (falls back to the old drawing if the
  main renderer cannot). Also the Project panel's type filter is as wide as its longest choice ("All types" was cut to "All typ").
* **Tests** (2026-11-17, Xvfb, Mesa llvmpipe). `editor_viewports_own`, `editor_viewports_sdl2`, `editor_viewports_own_2mon`
  (`tests/test_editor_viewports.cpp`: the shared glue and renderer, ImGui, okay::platform, the GL painter; no editor): the backend
  flags; the monitor list against `Displays()` (two monitors on the `_2mon` entry); a panel outside the main window gets its own window
  at the place and size asked; its pixels read back from ITS drawable (200 x 150) hold the panel's box and a texture made on the main
  window; the main window keeps its picture; a real pointer warp from the X server into the panel window gives ImGui the right desktop
  position and hover; injected clicks press a button in the panel; "crate" typed into a field there, then Backspace; a title-bar drag
  with desktop positions moves the window by exactly the drag; ImGui moving and sizing the panel moves and sizes the OS window (the
  same window, not made again); the OS sizing the window reaches ImGui; the panel moved over the main window merges and its window goes;
  the OS close request closes the panel; shut-down removes every hook. 57 checks (61 with two monitors), all passing. A deliberate break (the renderer not pointing the
  painter at the panel window) fails. Still passing: every `platform*` suite, `editor_platform(_own)`, `editor_gfx(_own)`,
  `launcher_imgui_renderer(_gl)`, `launcher_join`, `launcher_versions`, `launcher_wav_window`, `okayui_host` (33 of 33);
  `scripts/editor_no_sdl_check.sh` and `scripts/launcher_no_sdl_check.sh`: ALL PASS.
* **The real editor** (full build, `-O1 -g0`, 1920 x 1200 Xvfb, the physics example): with the three switches the Project tab dragged
  out of the editor became its own window (one panel window made, log `editor-input: panel window made`), it was moved and resized
  (the same window), its search took "stone" (1 item) and "cube" (2 items), it was dragged back onto the bottom tab bar (ImGui's docking
  targets showed) and docked again (its window closed, the filter kept). The Scene panel dragged out also works, the 3D view drawn in its
  window. **Under the SDL trap** (the same drag out, typing, drag back, then the window's close request): no SDL call, exit 0. The default
  SDL editor did the same steps; its panel window showed empty boxes for the thumbnails (fixed above; see the "after" screenshots).
  Screenshots kept outside the repository.
* **Not done / unverified.** Windows: nothing of E4 has run there (WGL with one context on two windows, `SW_SHOWNA`, the client-area
  position change, layered-window opacity: MinGW syntax check only). A real X11 desktop with a window manager (placement, focus on
  showing, opacity with a compositor) is unrun: Xvfb has none. `NoFocusOnClick` (a panel that should not take the focus when
  clicked) is not done. The default stays `sdl` until the owner approves (rule 17).

## Player, display modes and a build with no SDL (branch `claude2/sdl-player-no-sdl`, on top of E3)

**Status (2026-11-16): built and run on Linux under Xvfb; nothing changes by default; not merged; Windows not run.**

### Player: no SDL call on the own backend

With `OKAY_PLATFORM_BACKEND=own` the player makes **no SDL call at all**, from start to exit. Nothing changes without it.

* **What moved.** The five SDL calls left in `player/main.cpp`: the touch-screen count (`Backend::TouchDeviceCount`), the gesture clock
  (`okay::platform::TicksMs`), the "no window" message (`Backend::LastError`), the game clipboard (`shared/PlatformClipboard.hpp`; on the
  sdl2 backend these are the same SDL calls as before) and OkayUI's text-field clipboard (`OkayUI::SetSystemClipboard`, on own only).
  The 2D types are `okay::ui2d`'s (`Rect`, `RGBA`, `Vertex`: the same layout as SDL's), so the player includes no SDL header. Its monitor
  list and display modes come from `okay::platform` on an own window (below); an sdl2 window keeps `shared/SdlDisplays` as before.
* **Shared code it reached.** `shared/Splash.hpp` and `shared/OkayScriptUIBridge.hpp` include no SDL any more (the splash's unused
  `SDL_Renderer` overloads are gone). `okayui/src/OkayUI.cpp` uses `<cmath>` instead of `SDL_sinf` / `SDL_cosf` / `SDL_sqrtf` /
  `SDL_floorf` (they were the player's first SDL calls on own, through the F3 overlay) and `okay::ui2d` types; its `SDL_Renderer` path
  (`EndFrame(SDL_Renderer*)`) and SDL clipboard are now behind `#ifdef OKAY_PLATFORM_HAVE_SDL2`.
* **A bug found and fixed (both backends).** The window's close button did nothing while the GPU renderer was on: SDL (and our
  backends) send Quit only when the last window closes, and the GPU renderer keeps a hidden helper window. The player now ends on its
  game window's `WindowClose`.
* **Proof: `scripts/player_no_sdl_check.sh <okay-player> <scene> <out>`** (the launcher's and editor's trap: a library that defines
  every function libSDL2 exports, 839 here, preloaded in front of it; each writes `SDL CALL: <name>` and exits 97). Run on
  `examples/vehicle/vehicle_test.okayscene`, the way `OkaySpacePlayer.exe <scene>` runs it:
  1. control, the default player under the trap: stops at its first call (`SDL_SetHint`), exit 97;
  2. own: 15 seconds of play (W held, Shift+W, A, D, Space, the mouse moved, left and right clicks, the wheel, F3 on and off, F11 to
     fullscreen and back, S, typing), then the window's close button: **exit 0, no SDL call**;
  3. the default player through the same steps, for the pictures (both walk the character; the pictures differ where the software
     painter and SDL's renderer differ and where the two runs are a few frames apart).
  Before this branch the own run stopped at `SDL_GetNumTouchDevices` (the first frame), then at `SDL_cosf` (F3).

### Display modes in `okay::platform`

New (each with its sdl2, X11 and Win32 side; `docs/platform_layer.md`, "Display modes"):

* `Backend::DisplayModes(display)`: every mode a display lists (width, height, refresh), largest first, no repeats;
  `Backend::CurrentDisplayMode(display)`;
* `Window::DisplayIndex()`: which display (index in `Displays()`) the window is on;
* `Window::SetFullscreenMode(mode)`: exclusive fullscreen at the listed mode closest to `mode` (`ClosestVideoMode`: the same size
  with the nearest rate, else the smallest mode that holds it, else the largest); `SetFullscreen(false)` and destroying the window put
  the display's own mode back; `Window::FullscreenMode()`.
* sdl2: SDL's mode list, `SDL_GetWindowDisplayIndex`, `SDL_GetClosestDisplayMode` + `SDL_SetWindowDisplayMode` +
  `SDL_SetWindowFullscreen(SDL_WINDOW_FULLSCREEN)`. X11: the RandR modes of the outputs of the CRTC that shows the display
  (`XRRGetOutputInfo`), switched with `XRRSetCrtcConfig` (the screen is made larger first when the mode does not fit,
  `XRRSetScreenSize`), the window moved over the CRTC; libXrandr loaded at run time. Win32: `EnumDisplaySettingsW` on the monitor's
  device, `MonitorFromWindow`, `ChangeDisplaySettingsExW(..., CDS_FULLSCREEN)` and `ChangeDisplaySettingsExW(device, null)` to undo;
  written from Microsoft's documentation, **MinGW syntax check only**.
* **Used by** `shared/PlatformDisplays.hpp`: the game's resolution list on own windows now lists the display's modes (it showed the
  desktop size only) and "Fullscreen" switches the mode (it was desktop fullscreen). That is the player's list on `own` and the
  editor's Play list with the three editor switches.
* **Tests** (`test_platform_display`): the pure rules (`SortedVideoModes`, `ClosestVideoMode`, `DisplayIndexForRect`), and
  `platform_display_modes_own` / `_sdl2` under Xvfb: the test adds four RandR modes itself (800x600 at 60 and 75 Hz, 640x480 at 60,
  1024x768 at 50), checks the list (with Xvfb's own 1024x768), the current mode, the window's display, a switch to 800x600@75 (the
  CRTC really shows 800 x 600; on own the window covers it), 700x500 (picks 800x600@75), 1024x768@50, back with
  `SetFullscreen(false)`, and a switch undone by destroying the window. Not tested: a mode larger than the screen (Xvfb's screen
  cannot grow past its start size), a real monitor, Windows.

### A build with no SDL: `-DOKAY_WITH_SDL=OFF`

`OKAY_WITH_SDL` is a new CMake option, **ON by default** (the default build is unchanged: SDL is found and linked as before and every
switch keeps its SDL default). With `OFF`:

* SDL2 is not looked for or linked (`find_package(SDL2)` is skipped in `cmake/OkayPlatform.cmake`, `okayui/`, `launcher/`, `editor/`,
  `player/`); `OKAY_PLATFORM_HAVE_SDL2` is not defined.
* Left out of the build: `SdlBackend.cpp`, `SdlPainter.cpp` (its no-SDL stubs moved to `Painter.cpp`), `LauncherGfxSdl.cpp`,
  `EditorPlatformSdl.cpp`, `EditorGfxSdl.cpp`, `shared/SdlDisplays.cpp`, `editor/VsGraphShot.cpp` (`--vs-shot` says it needs SDL) and
  Dear ImGui's `imgui_impl_sdl2.cpp` / `imgui_impl_sdlrenderer2.cpp`.
* The switches' defaults become our own code: `OKAY_PLATFORM_BACKEND` (the own backend is the default whatever it says),
  `OKAY_LAUNCHER_RENDERER`, `OKAY_EDITOR_PLATFORM`, `OKAY_EDITOR_RENDERER` (there is no sdl side to pick).
* Code that exists only on the SDL path is compiled out with `#ifdef OKAY_PLATFORM_HAVE_SDL2`: SDL's start and end, the SDL event
  loops of the launcher and the editor, the editor's SDL_Renderer view targets (Direct3D 11 zero-copy) and multi-window panels, the
  SDL message box, SDL's clipboard, the HWND of an SDL window, OkayUI's `EndFrame(SDL_Renderer*)` and SDL event feed, the self-test's
  SDL keyboard, the voice clips' `SDL_LoadWAV` path (our WAV decoder is then the only one). `SDL_Window*` / `SDL_Renderer*` /
  `SDL_Texture*` stay in `editor/main.cpp` as opaque pointer types (forward-declared, never handed to SDL) so the shared code paths
  did not have to be split; `OKAY_BUILD_TESTS` must be OFF (many tests compare against SDL; configuring says so).
* **Run (2026-11-16, Linux, Xvfb, Mesa llvmpipe; `scripts/no_sdl_build_check.sh <bin> <scene> <out>`)**, the launcher, the player and
  the editor (`-O1 -g0`) built with `-DOKAY_WITH_SDL=OFF -DOKAY_BUILD_TESTS=OFF`, run with **no switch set**:
  * `ldd`: none of the three loads an SDL library (`libsodium`, `libstdc++`, `libm`, `libgcc_s`, `libc` only);
  * launcher: says `launcher: no SDL (own renderer, own platform backend)`; the Create page, a click into the search box and "abc",
    a click on Play (the Play page opens), exits by itself after 1,500 frames: exit 0 (median frame 8.1 ms);
  * player: `platform=own`, the software painter; W, the mouse, F3; the close button: exit 0;
  * editor: `editor: no SDL ...`, `--open` the physics example; a click in the Scene view, a Hierarchy row, "crate" typed into the
    search, Ctrl+P (Play: the scene runs in the Game view) and Ctrl+P (Stop); the close button: exit 0.
  Screenshots kept outside the repository. MinGW: every changed file passes a syntax check both with SDL (Linux SDL headers) and
  without (`OKAY_PLATFORM_HAVE_SDL2` undefined), including `editor/main.cpp` and `launcher/main.cpp`.
* **What blocks dropping SDL for real** (not done here, and the reason it stays a developer build): nothing of it has run on Windows
  (the Win32 backend's windows, WGL, input, audio, display modes); the own renderers have not been compared with SDL's on a real GPU
  ("faster and up to par" is unmeasured where it matters); no multi-window panels on own (E4); the web build has no own backend
  (`OKAY_WITH_SDL=OFF` refuses Emscripten). (Touch on the own backends and the tests without SDL: done on `claude2/sdl-touch-tests`,
  next section; touch on Windows and on a real touch screen is unverified.)

### Touch on the own backends and tests without SDL (branch `claude2/sdl-touch-tests`, on top of this one)

**Status (2026-11-17): built and run on Linux (Xvfb); not merged; the Windows part compiled only (MinGW syntax check).**

* **Touch** (`docs/platform_layer.md`, "Touch"): both own backends now give `FingerDown` / `FingerMotion` / `FingerUp` in the sdl2
  backend's shape (finger id, 0..1 position, and a new `pressure`), and `TouchDeviceCount` is real. X11: XInput 2.2 touch events
  (libXi loaded at run time), the first finger also drives the mouse as SDL does (`Event::fromTouch` marks those mouse events);
  `OKAY_X11_TOUCH=0` turns it off. Win32: `WM_POINTER*` for touch pointers, `WM_TOUCH` on Windows 7, `GetPointerDevices` for the
  count. Tested: the shared translator and the XI2 translation with built events (`platform_touch`), and under Xvfb that the backend
  really holds the window's touch selection and that the mouse still works (`platform_touch_x11`, `_off`). **Not tested: a real touch
  event from an X server (Xvfb has no touch device), a touch screen, Windows.**
* **A tap shorter than one frame** (#888 finding 132, all backends): `Input::FeedTouch` keeps it `Began` for that frame and `Ended`
  the next, so on-screen buttons see it (`touch_controls`: 0 of 10 quick taps pressed the button before, 10 of 10 after).
* **Tests with `OKAY_WITH_SDL=OFF`.** The configure error that forced `OKAY_BUILD_TESTS=OFF` is gone. What needed SDL and why:
  * the platform tests' own copies of SDL headers were found with `__has_include(<SDL.h>)`; they now also need
    `OKAY_PLATFORM_HAVE_SDL2`, so with OFF they build without their sdl2 parts;
  * `tests/CMakeLists.txt` no longer looks for SDL2 with OFF, and runs the `platform_*` entries only for the backends that exist;
  * three tests had an SDL-only part inside an otherwise SDL-free test: `okayui_painter` (the sdl2 painter comparison),
    `launcher_imgui_renderer` (the OkayUI host's SDL feed against its platform feed) and `own_stack` (the default backend is `own`
    with OFF). Those parts are now compiled only with SDL; the rest runs.
  * **Left out with OFF, because they exist to drive or compare with SDL (19):** the golden images drawn on SDL's software renderer
    (`ui_renderer_golden`, `ui_widgets_more_shots`, `ui_text_shots`, `ui_focus_render`, `accessibility_render`, `display_screen`);
    OkayUI's `SDL_Renderer` test (`okayui`) and the OkayUI host's SDL event feed (`okayui_host`); `voice_races` (the launcher's voice
    session header is SDL); `launcher_wav_window` (compares with `SDL_LoadWAV` and SDL windows); `editor_platform`, `editor_platform_own`,
    `editor_gfx`, `editor_gfx_own` (one binary that holds the editor's SDL side and ImGui's SDL2 backends next to ours); and the sdl2
    entries of the platform tests (`platform_default_env_sdl2`, `platform_window_sdl2`, `platform_state_sdl2`, `platform_display_sdl2`,
    `platform_display_modes_sdl2`).
  * **Run (2026-11-17, Linux, Xvfb; `-DOKAY_WITH_SDL=OFF -DOKAY_BUILD_TESTS=ON -DOKAY_BUILD_PLAYER=ON`, built in groups of ten with
    `ninja -j1` and each group deleted after its tests, since disk was short):** 788 tests, **782 passed, 0 failed, 6 skipped**. The
    skipped six need things this machine does not have, not SDL: five SQL checks (`anticheat_sql`, `blocks_sql`, `chat_receipts_sql`,
    `member_moderation_sql`, `reports_sql`: no PostgreSQL) and `sign_windows` (Windows signing tools). Every one of the 758 programs
    built; `ldd` found no SDL library in any of them. On the way: three tests (`ssao_math`, `d3d_post_hlsl`, `project_state`) failed
    only because ccache's base-directory setting made `__FILE__` relative (they read source files next to themselves); built
    without it they pass.

### Counts (`scripts/sdl_inventory.py`, calls / words)

| | before (`93ae3909`) | after |
|---|---|---|
| player, `player/` | 5 / 199 | **0 / 2** |
| player, everything it reaches | 68 / 404 | 37 / 114 |
| launcher, `launcher/` | 36 / 270 | 36 / 97 (the `Uint32` typedefs became `<cstdint>`; the calls are the default path's, now behind `#ifdef`) |
| launcher, everything it reaches | 86 / 505 | 63 / 245 |
| editor, `editor/` | 122 / 317 | 122 / 296 (the same: the default path keeps its SDL calls, behind `#ifdef`) |
| editor, everything it reaches | 188 / 577 | 165 / 469 |
| `okayui/` (not tests) | 44 / 144 | 21 / 79 |
| all programs and shared code (no tests, no sdl2 backend) | 232 / 1,058 | 204 / 580 |

The sdl2 backend grew (199 / 481 to 214 / 506): the display-mode calls.

### What comes next for the editor

* **E2 (the rest). Drawing without `SDL_Renderer` everywhere.** Stage E2 above did the frame, ImGui and every texture from pixels. Left:
  the Direct3D 11 "draw the views into SDL's device" path (our own Direct3D 11 painter, or read-back on Windows too; on `own` it is
  read-back today), and the multi-window panels' own renderers (with E4). Then the main window can come from `okay::platform` directly
  (our GL painter on our own window) once its events come from `Backend::PollEvent` (E3). Medium.
* **E3. Done on its branch** (section above): with the three switches the editor makes no SDL call (Linux, Xvfb). Left for it: a run on
  Windows and on a real X11 desktop. (Display modes in `okay::platform`, the game's resolution list and exclusive fullscreen: done on
  `claude2/sdl-player-no-sdl`, section "Player, display modes and a build with no SDL".)
* **E4. Done on its branch** (section "Stage E4" above): multi-window panels on `own` (Linux, Xvfb; Windows unrun). Then the default path's own SDL (its event
  loop, `SDL_Init` / `SDL_Quit`, the base path, the message box, `SdlDisplays.cpp`, `SdlClipboard.hpp`) can go once the owner switches
  the default.

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
