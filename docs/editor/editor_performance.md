# Editor speed: seeing it, measuring it, tuning it

> **What this is:** The editor shows how long each frame takes and where the time goes, and has a few switches to make it cheaper.
>
> **Status:** Internal record (audit, plan or status): read it as a dated record, newest facts in the code.
>
> **Last checked:** 2026-11-20 (moved into `editor/`, paths and links checked; the text itself was not re-checked against the code).

The editor shows how long each frame takes and where the time goes, and has a few switches to make it cheaper.
Everything here is **off by default** except the numbers you turn on yourself. Settings are remembered in
`okay_settings.txt` next to the editor.

## See the numbers

**View > Rendering > Frame-time overlay** opens a small window:

- FPS, average frame time, the 95th percentile (19 frames in 20 are faster than this), the worst frame and the "1% low".
- A graph of the last 120 frames.
- Where the frame goes, averaged over the last 30 frames, biggest first:
  3D view render (the GPU draw, or the CPU raster; on the read-back path this includes the wait for the GPU),
  3D view upload (CPU picture to texture), viewport gizmos and overlays, panels (Hierarchy, Inspector, Project ...),
  ImGui draw lists and submit, Present (swap, GPU wait, vsync), Play mode (scripts, physics, animation), the limiter's
  own sleep, and "not timed" (reading events and waits between scopes). The parts add up to the frame time.
- **Copy report** puts the same numbers, plus the renderer name, view size and which switches are on, on the clipboard as text.

The window times only while it is open (the profiler is off otherwise), so it costs nothing when closed.
**View > Rendering > Profiler window** opens the bigger profiler (pause, scrub, per-section min / average / max).
The numbers come from `OKAY_PROFILE` scopes, so a new panel shows up under "Panels" as `UI.<name>`.
`OKAY_PERF_DUMP=1` still prints the same sections to the console every two seconds.

## Switches (View > Rendering)

| Switch | What it does | Default |
|---|---|---|
| Frame rate limit | Sleeps out the rest of each frame (60, 90, 120, 144, 240). Frees the GPU and CPU; with VSync on it is not needed. | Off |
| Slow down when idle | When nothing is clicked or typed for 0.75 s and the game is not playing, drops to the idle rate (15 / 20 / 30 / 60 FPS). Any input brings full speed back on the same frame. | Off, 30 FPS |
| Viewport resolution | Draws the 3D view at 100 / 75 / 50 / 35 % and stretches it (same setting as Lighting > Rendering > Render scale). | 100 % |
| Performance mode (Scene view) | The Scene view only (the Game view and builds keep the scene's own look): no ambient occlusion, bloom, FXAA or supersampling, render scale at most 75 %, and while the Game panel is closed one shadow cascade at 1024 texels at most. | Off |

Why the shadow change waits for the Game panel to close: the Direct3D renderer reallocates its shadow texture whenever the
size changes, and the Scene and Game views render in the same frame, so different sizes would reallocate twice a frame.

## How it is built

- `editor/EditorPerf.hpp` holds the logic without any window or GPU code: `Break` (a profiler frame split by self time into
  sections), `Stats` (uses `okay::FrameStats`, the same window the game loop uses), `TargetFps` and `SleepSeconds` (the
  limiter and idle decisions), `EffectiveScale` and `RenderSize`, and `Cheaper` (the performance-mode preset). Tests:
  `tests/test_editor_perf.cpp`.
- `editor/main.cpp`: `DrawPerfOverlay`, the menu items in `DrawRenderingMenuItems`, a `PerfModeGuard` that puts the renderer switches back
  after the Scene view is drawn, and the sleep just before the profiler's frame ends (it shows as "Frame limiter sleep").
- Written from the plain definitions (frame rate is 1000 divided by milliseconds; a percentile is read off a sorted list; a limiter sleeps
  out the rest of a frame budget). Nothing is taken from another project.
- Earlier editor speed work that this builds on: the Scene view renders straight into the texture the panel shows on Direct3D 11
  (no read-back), the 1920-pixel cap on the offscreen buffer, auto performance, and the software renderer work in `docs/engine/perf_log.md`.

See `docs/try_it.md` ("Editor frame-time overlay") for the owner steps in the Windows release.
