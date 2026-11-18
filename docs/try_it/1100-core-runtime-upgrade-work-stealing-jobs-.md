---
id: 1100
row: | 1100 | Core runtime: frame capture, memory tracking, work-stealing jobs | Editor **View > Panels > Profiler**, button **Capture trace**; or run any program with `OKAY_TRACE` / `OKAY_MEMTRACK` set | New in this change (not yet run on Windows) |
---
## 1100. Core runtime upgrade: frame capture, memory tracking, work-stealing jobs

**What it is.** Three engine-core upgrades (details and numbers: `docs/core_runtime_upgrade.md`). You can see two of them
directly; the third (jobs) shows up as a faster `ParallelFor` and as worker tracks in the capture.

**Steps: frame capture (editor).**
1. Open the editor (`Tools\OkayEngine.exe`), open any project, press **Play**.
2. Open **View > Panels > Profiler**. At the top, under the graphs, you see a row with a number box (frames) and the
   button **Capture trace**. Leave 120 frames and click it.
3. The row changes to "Capturing: frame N of 120". After 120 frames it stops by itself, and the line
   "Last trace: <path>" appears (the editor's log folder, file `okay-trace-1.json`). The Console panel says
   "Trace saved" or the path. **Copy path** copies it.
4. Open `chrome://tracing` in Chrome or Edge (or https://ui.perfetto.dev), load the file. You should see a "main"
   track with a `Frame` bar per frame and the nested scopes inside it, and counter graphs (frame.ms, draw.objects,
   draw.triangles, particles, scene.objects).

**Steps: frame capture (any program, no UI).** Start the editor or the player with the environment variables
`OKAY_TRACE=C:\temp\trace.json` and optionally `OKAY_TRACE_FRAMES=300` (default 300). The file is written when the
frames are done.

**Steps: memory tracking.** Start the editor or player with `OKAY_MEMTRACK=1`. When the program closes, the error
output (run it from a terminal) says "memory tracking: no leaks at shutdown" or lists leaks. In the editor the Profiler
panel then has a collapsible **Allocator report** with live, reserved and peak bytes and the fragmentation of every
tagged allocator (the scene's frame arena is `scene.frame`). Without the variable nothing is tracked and nothing is printed.

**Jobs.** No switch: `JobSystem::ParallelFor` uses the new work-stealing path by default. Run
`test_jobs_bench` (build from source) to see the table; in a capture the worker threads appear as tracks
"worker N" with their `task` and `job` bars.

**Unverified.** Not run on Windows. The editor button was not compiled in the build container.
