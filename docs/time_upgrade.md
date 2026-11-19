# Time upgrade (branch `claude/time-1`)

Owner request: "Same with time management": improve what we have, as for the entity and component system, no
second copy. This page is the audit (what exists, what the gap was, with numbers from small simulations) and the
list of what changed. For how to use it see [time_guide.md](time_guide.md); to try it see `docs/try_it.md` entry `time1`.

Everything here was written from general ideas (an accumulator, whole-nanosecond counting with a carried
fraction, a trapezoid integral of a speed that changes in a straight line, a seeded mixing function for jitter).
No code, shader text or tests from another project were used (rule 19a). Checked on Linux only. **Nothing was run on
Windows and the editor and player windows were not run** (they were syntax-checked, see "Unverified").

## What already existed (read first)

| Area | Where | What it already did |
|---|---|---|
| Global clock | `Core/Time` | `DeltaTime`, `UnscaledDeltaTime`, `ElapsedTimeD` (double), `TimeScale` (capped at 100), freeze, hit stop (8 at once), time-scale ramp, session time, fixed step values, `SmoothDelta`, `RealtimeSinceStartup` |
| Frame driver | `Core/GameLoop` | clean + clamp the frame time, hitch count, pause hooks, fixed steps (max 5, the rest dropped), alpha, debug time scale, single stepping, capture mode, record and replay, statistics |
| Clocks | `Core/Clock` | `game`, `ui`, `cinematic` and named clocks with scale, pause, ramp; scheduler tasks and tweens can follow one |
| Timers | `Core/Scheduler`, `Core/Tween` | `Invoke`, `InvokeRepeating`, `Tween`, tags, owners, object lifetime, pause, clocks, catch-up cap 4096, `Dump()`, `TimerDebug` |
| Poll timers | `Core/Countdown`, `Core/Stopwatch` | `Countdown`, `Cooldown`, `Stopwatch`, saved through the save framework |
| Save | `Core/TimeSnapshot` | clocks, scale, ramp and random streams |
| Fixed step | `Core/Time` `FixedTimestep`, `Scene::StepPhysics` | scene physics and `FixedUpdate` ran their own float accumulator (catch-up 1/3 s) |
| Pacing | `Core/FramePacing` | `FrameLimiter` (sleep then spin) existed but the player did not use it |
| Day and night | `Components/DayNightCycle` | a 24 hour clock that drove the sun and sky; `hour` saved value |
| Script API | `Script/EngineApi`, `OkayScriptVM` | `Time.*`, `hit_stop`, `slow_motion`, `ramp_timescale`, `push_pause`, `after`, `every`, `Invoke`, coroutines |
| Editor | `EditorState`, Game view | Play loop on `GameLoop`, Pause, Step, slow-motion buttons, Stats overlay with a timers box, Project Settings > Physics (steps per second) |
| Visual Script | `VisualScript/*` | On Timer, Delay, Wait, Slow Motion, Hit Stop nodes |
| Tests | `tests/test_*` | about 40 time tests: clock, game loop, frame step, capture, scheduler (4 files), timers, countdown, snapshot, loop audits |

## Gaps found, measured where it can be measured

`tests/test_time_sim.cpp` and `tests/test_time_upgrade.cpp` print every number below when run.

| # | Question | Finding | Measured | Now |
|---|---|---|---|---|
| 1 | A 500 ms hitch: clamp? spiral of death? | `GameLoop` clamps to 0.25 s (the player and editor set 0.1), runs at most 5 fixed steps and drops the rest; the 60 frames after run at most 1 step each: **no spiral** | stepped 0.250 s, 5 steps, 0.150 s dropped, then max 1 step a frame | unchanged; the clamp, the catch-up and what happens to lost time are now Project Settings |
| 2 | The scene's physics hitch policy | `Scene` used a different rule (catch up 1/3 s = 16 steps in one frame) from the loop (5 steps) and neither was editable | a 500 ms frame ran **16** physics steps | `Scene::maxPhysicsCatchUp` is a project setting; policy Drop (as before) or Spread |
| 3 | Time drift: float frame step | `Time::Step(float)`: 1/60 s as a float is 9e-10 s too long; the game clock ran slow-forward | one hour of 60 fps frames added up to 3600.0002 s (+0.19 ms an hour, +19 ms in 100 hours) | `GameLoop` steps by an exact double (`Time::StepD`): after 100 hours at 144 fps the clock is **1 ns** from exact (`ElapsedTicks`) |
| 4 | Float second counter after a day | a float "seconds since start" cannot count frames after a day | its step is **7.8 ms** at 24 hours (coarser than a 144 fps frame) | `ElapsedTimeD()` and the new integer ticks; the float view stays for readouts |
| 5 | The same steps at every frame rate | scene physics steps over 10 minutes | 30, 60, 144, 240 and a jittering 20 to 120 fps all within 1 step of 30000 (off by 0, 0.8, 1, 0, 0.8) | pinned by `time_sim` |
| 6 | 1 hour and 100 hours of the loop | game time against real time and fixed steps | 1 h at 30, 60, 144 fps and variable: equal to 1e-6 s, 180000 steps (+1 at 30 fps at the start edge); 100 h at 60 fps: 18000001 steps for 18000000 expected | pinned |
| 7 | Bad numbers | NaN, negative, 1e300 frame times, time scale 0, negative, 1e30 | all finite; scale 0 stops time and steps; negative and huge scales are cut to 0 and 100 | pinned; the integer clock cannot overflow (one frame counts at most 1e9 s, the count stops at 9e18 ns) |
| 8 | Interpolation alpha | `Time::FixedAlpha()` was the alpha of the loop's own accumulator, which drove nothing in the player; physics interpolates by the scene's alpha: two different numbers | seen in code: the loop published its own value every frame | `Scene::StepPhysics` publishes its alpha (`Time::FixedAlpha() == Scene::PhysicsAlpha()` at all 2000 test frames, always in [0, 1)) |
| 9 | The fixed step value | `Time::FixedDeltaTime()` stayed 0.02 when a project set another physics rate, so the loop's clock and `FixedTimestep` disagreed with physics | read in code | `Scene::SetProjectPhysicsRate` sets it |
| 10 | `Time::InFixedStep()` | false inside a scene's `FixedUpdate` (only the loop's own steps set it) | read in code | true inside the scene's fixed steps |
| 11 | Frame rate cap | the player slept whole milliseconds, no spin, no carried deadline | a model of that formula (2 ms of work, sleeps 0.2 to 1.2 ms long): cap 30 gave 30.6, 75 gave 78.7, 120 gave 129.9, 144 gave 149.2 fps (up to +8%); not measured on a real machine | `FrameLimiter` (sleep, spin the last 1.5 ms, carried deadline): the same model gives every cap within 0.002 percent |
| 12 | vsync, focus | a cap above the refresh rate fought vsync; no power-saving mode | read in code | `FrameLimiter::EffectiveTargetFps`; "Frame rate when the window is not active" |
| 13 | Timers: created inside a callback | a timer a callback schedules is stepped by the same update when it is due (documented, four older tests rely on it). A callback that re-armed itself with a delay of 0 kept the update loop running for ever (the loop re-reads the list size) | by reading the loop; `[scheduler] a self re-arming Invoke(0) ran N times in one update` is printed by `time_upgrade` | an update steps at most `Scheduler::kMaxSpawnedPerUpdate` (100000) tasks added while it runs; `NextFrame(fn)` runs in the NEXT update even from inside a callback |
| 14 | Timers: ordering | a repeating timer that is due several times in one frame runs all of its turns before the next timer runs; same due time runs in scheduling order | read in code, pinned by test | unchanged, now **documented and tested**: same due time = scheduling order, earlier due time = same or earlier update |
| 15 | OkayScript timers | their own list: a timer cancelled by an earlier callback of the same frame still ran (the due list was already built), `CancelInvoke` could not stop a due one, a throwing callback aborted the later timers of that frame | read in code; new test `cancel in same frame` | `after`, `every`, `Invoke`, `InvokeRepeating`, `CancelInvoke`, `cancel_timers` run on a per-script `Scheduler`; a throwing callback is logged and the timer lives on |
| 16 | Timers across frame rates | a 0.1 s repeating timer, 60 s | 600 runs at 30, 60 and 240 fps, 599 at 144 fps (the float 0.1 is a hair over 0.1 s) | pinned (599 or 600) |
| 17 | Long hitch and timers | a 5 s frame | a 0.1 s timer runs 49 or 50 turns in that frame and keeps its rhythm | pinned |
| 18 | Save and load of timers | the scheduler held code and could not be saved | none saved | named timers: `RegisterAction`, `InvokeAction`, `SaveState`, `LoadState` (a timer whose action is not registered is skipped and counted) |
| 19 | Pause and time scale for each system | physics, `FixedUpdate`, animation, particles, `every/after`, scripts, `SequencePlayer` (cutscenes): scaled game time. UI animations (`UIAnimation`): scaled (freeze in a pause). Tween Animation: has an `unscaled` switch. Audio: follows no clock at all. Networking: real time | read in code | see "Not done"; the guide has a table of which time each system should use |
| 20 | Alt-tab resume spike | player: an unfocused frame with Run In Background off is a 0 s step and the first frame after refocus is normal; with it on, the measured frame is clamped like any hitch | read in code and `test_loop_audit5` | unchanged |
| 21 | Day clock | `float time` added `dt * 24 / dayLength` each frame: at 1200 s days and 144 fps the increment is 1.4e-4 h and the float quantises it; no day counter, no events | computed: a float at 12 h has a step of 9.5e-7 h | the hours are a double inside; 72.5 hours at 144 fps fired exactly 72 hour events, 3 sunrises, 3 sunsets, ended 23:31 day 4 |
| 22 | Coroutines `Wait` | a `WaitForSeconds` that is over resumes at the next frame and the overshoot is lost (as in C#): a loop of `Wait(0.1)` runs 7.5 times a second at 30 fps | read in code | not changed (see "Not done") |
| 23 | Editor Play, Pause, Step | already on `GameLoop` with tests (`frame_step`, `editor_hooks`) | existing tests | the Stats overlay shows the new time lines |

## What changed

1. **Core clock.** `Time` keeps whole nanoseconds (`ElapsedTicks`, `UnscaledTicks`, the fraction carried), `StepD(double)`,
   `DeltaTimeD`. `Time::Step(float)` still works. `GameLoop` and the clocks use exact doubles. Every `Clock` also counts
   nanoseconds and has `DeltaD()`.
2. **Clocks that follow clocks.** `Clock::SetParent`. The set ticks parents first, in any add order; a loop is cut; saved by
   `TimeSnapshot`. Slow motion ease: `Time::SlowMotion(scale, seconds, ease)`.
3. **Timers.** `Scheduler`: `NextFrame`, `At`, `EndOfFrame`, `Options::jitter`, `Options::firstDelay`, named timers that
   save, a cap on tasks spawned inside one update. OkayScript timers moved onto it.
4. **Fixed step.** `FixedTimestep::Overflow` (Drop or Spread) and `AdvanceD`; `GameLoop::Options::spreadFixedOverflow`;
   `Scene::spreadPhysicsCatchUp`, `SetProjectPhysicsCatchUp`; the scene publishes `Time::FixedAlpha` and the fixed-step flag; the project's
   physics rate sets `Time::FixedDeltaTime`.
5. **Frame pacing.** The player uses `FrameLimiter` and `EffectiveTargetFps`; power-saving cap when not active.
6. **Game features.** `DayNightCycle` is the calendar (days, seasons, speed, clock name, events, `SkipToHour`); script calls `day_number()`,
   `season()`, `clock_text()`, `skip_to_hour(h)`; `slow_motion(scale, seconds, ease)`.
7. **Editor.** Project Settings > Time (`Core/TimeSettings`, `editor/TimeSettingsUi.hpp`); Stats overlay and F3 show the fixed step, the clocks and the next timer.
8. **Docs and example.** `docs/time_guide.md`, this page, the feature map, `docs/try_it/time1-*.md`, `examples/time/time_playground.okayscene`
   (shipped by `scripts/build-windows-dist.sh`).

## Tests

- `test_time_sim` (new, 28 checks; the old float step figure is printed there too): hitches, 1 and 100 hour runs, frame rates, bad numbers, 10 minute physics step counts.
- `test_time_upgrade` (new, 244 checks): nanosecond clock (1 h, 100 h at 144 fps), clock parents, overflow policy, scheduler additions, OkayScript timers, day clock and events,
  Time settings text and clamps, frame limiter model, physics policy and alpha, overlay lines, eased slow motion at 30, 60, 144 and 7 fps.
- Also rebuilt and run, all passing: `timescale`, `time_pause`, `time_extras`, `time_snapshot`, `countdown`, `loop_audit2` to `5`, `frame_step`, `capture_mode`, `frame_record`, `frame_stats`, `frame_pacing`, `scheduler_reentry`, `scheduler_hardening`, `scheduler_owner_clock`, `scheduler_scale`, `timers_tweens`, `timers_audit`, `timers_fifth`, `script_lib`, `script_time`, `application_loop`, `tick_pacer`, `loop_player_style`, `core_features`, `core_fixes`, `stop_save_timeline_lod`, `tween`, `tween_edges`, `tween_features`, `game_state`, `timer_debug`, `vs_runtime`, `vs_types`, `fixed_step`, `game_loop`, `clock`, `daynight`, `save_scene`, `okayscript_csharp`, `editor_hooks` and `examples_work` (85 scenes, with the new `examples/time/time_playground.okayscene`). Built at -O0 on Linux.
- `test_time_upgrade` section 13 loads the shipped example scene, presses its keys with fake input and checks slow motion (blue cube 18 degrees in the slow second against 90 for the orange one), pause, sleep until 6:00 and hit stop.

## Not done (and why)

- **Audio clock.** Audio follows no clock, so there is no pitch change in slow motion and nothing reads the `audio` clock. An `AudioSource` option
  "follow clock" needs the mixer to take a speed per voice (a mixer change, not a time change).
- **UI on real time by default.** `UIAnimation` follows game time, so a UI fade freezes in a pause. Switching it to the `ui` clock changes old
  scenes whenever the time scale is not 1; it needs an owner decision and an Inspector switch (Tween Animation already has `unscaled`).
- **Visual Script timers** (Delay, Wait, On Timer) still keep their own accounting. They need a graph runner that can hold a `Scheduler` handle across
  frames and across hot reload; not started.
- **Coroutine `Wait` overshoot** is not carried (C# behaves the same).
- **Eased slow motion in Visual Script**: the node has two inputs; the eased form is script and C++ only for now (adding a node changes the generated node tables).
- **Standard clocks** `physics`, `audio`, `network` can be created (button in Project Settings) but no system reads them yet.
- **Timers saved**: only named timers; plain `Invoke` code timers cannot be saved.
- **Cross-timer ordering inside one huge frame** (two repeating timers each due many times in a 5 s frame run one timer's turns, then the other's).
- Whole-process time travel (restoring `ElapsedTime`) is still deliberately not done (see `docs/loop_and_time.md`).

## Unverified

Linux only. The player and the editor were only syntax-checked (compiled with `-fsyntax-only`), not linked or run, so the Project Settings page, the
overlay lines, the player's frame limiter and the example scene in a window are unverified. The limiter numbers come from a model with a fake clock, not from
a real machine. Windows was not run. The cross-compile rule 21a was followed (no `<mutex>`, `<thread>`, `<condition_variable>` or `call_once` was added to a header the
launcher, editor or player reach; `Scheduler`, `Time`, `Clock`, `TimeSettings` use none).
