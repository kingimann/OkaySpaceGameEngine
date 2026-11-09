# Timers and tweens

Two systems schedule work over time. The `Scheduler` runs callbacks after a delay
or on an interval. The `TweenManager` animates values with easing, loops and
sequences. Each scene owns one of each (`scene.scheduler()`, `scene.tweens()`),
plus there is one process-wide scheduler that outlives scenes. Both can follow a
named clock channel (see "Clock channels"), and `Spring` (in `Math/Spring.hpp`)
covers motion that should carry its velocity instead of restarting.

Neither system is thread safe: schedule, cancel and update from the thread that
runs the scene.

## Scheduler

```cpp
Scheduler& s = scene.scheduler();
s.Invoke(2.0f, [] { /* once, after 2 s */ });
auto h = s.InvokeRepeating(0.5f, [] { /* every 0.5 s */ });   // repeatCount < 0 = forever
s.Tween(1.0f, [](float t) { /* t goes 0 -> 1 */ }, [] { /* done */ });
s.Cancel(h);
```

### Options

Pass a `Scheduler::Options` as the last argument:

| Field | Meaning |
|---|---|
| `unscaled` | Ignore the time scale and the game pause. A pause-menu timer keeps running at time scale 0. Default: scaled. |
| `tag` | A label for `CancelByTag`. |
| `owner` | Any pointer, usually `this`, for `CancelByOwner`. It is only compared, never dereferenced. |
| `clock` | A named clock channel (`"game"`, `"ui"`, `"cinematic"`, or your own) that drives this task instead of the scene's time. See "Clock channels". |
| `object` | The `GameObject` this task acts on. The task is dropped, silently, when that object is destroyed or its scene unloads. Doubles as the owner for `CancelByOwner`. See "Tasks that die with their owner". |
| `alive` | A `std::function<bool()>`: when it returns false the task is dropped, silently. For owners that are not game objects. |

```cpp
Scheduler::Options o; o.unscaled = true; o.tag = "menu";
s.InvokeRepeating(1.0f, [] { /* blinks while the game is paused */ }, -1, o);
s.CancelByTag("menu");        // returns how many it removed
s.CancelByOwner(this);
```

Per-task controls, all by handle: `SetPaused(h, bool)` / `IsPaused(h)` freeze a task
and keep its progress; `SetInterval(h, seconds)` changes a delay, interval or tween
duration (progress is kept, so a shorter interval can fire on the next update);
`Remaining(h)` returns seconds until it fires (-1 if it is gone); `IsScheduled(h)`.
`ActiveCount()` counts every task that has not finished or been cancelled, paused ones included.
By group: `CancelByTag`, `CancelByOwner`, `CancelByObject(go)`, `SetPausedByTag(tag, bool)`,
`SetPausedByOwner(owner, bool)` (each returns how many tasks it touched) and `CountByTag(tag)`.
`Clear()` cancels everything.

Cost: every lookup by handle (`Cancel`, `SetPaused`, `IsPaused`, `SetInterval`, `Remaining`,
`IsScheduled`) is O(1), `ActiveCount` and `CountByTag` are O(1), and `CancelByTag`, `CancelByOwner`,
`CancelByObject`, `SetPausedByTag` and `SetPausedByOwner` touch only the tasks of that group, so a
hundred thousand timers can be cancelled one by one (an object that dies cancels its own with
`CancelByOwner(this)` without scanning everyone else's). Tasks still run in the order they were
scheduled. What a cancelled or finished task captured is freed at once (inside an update: when that
update ends), and a captured value whose destructor calls back into the scheduler is fine.

A repeating timer given `repeatCount` 0 behaves like 1 (it runs once); pass a negative
count for "forever". The smallest interval or tween duration is 0.1 ms.

Cancelling is safe from inside a callback, including cancelling the running task
and its siblings. Tasks added by a callback run in the same update when they are due.

### Scaled and unscaled time

`Scene::Update` receives the scaled frame time. The scheduler recovers the real
frame time as `dt / timeScale` (or `Time::UnscaledDeltaTime()` while the scale is 0,
which is what `Game::SetPaused` sets). You can also pass it yourself with
`Update(dt, unscaledDt)`.

### Clock channels

Scaled and unscaled are only two kinds of time. A pause menu wants gameplay timers
frozen and UI tweens running; a cutscene should not slow down with the game; a
"stunned" status wants its own speed. `Core/Clock.hpp` names those as channels
(`"game"`, `"ui"`, `"cinematic"`, or any name you make up), each with its own scale
and pause. A task or tween can follow one:

```cpp
Scheduler::Options o; o.clock = "ui";
scene.scheduler().InvokeRepeating(0.5f, [] { /* blinks while the game is paused */ }, -1, o);
DOTween::Move(go, {0, 2, 0}, 0.4f)->SetClock("ui");          // a menu slide
Clock::Get("game").SetScale(0.5f);                           // only "game" tasks slow down
Clock::Get("enemies").Pause();                               // a custom channel: freeze one group
```

A channel task advances by what its clock advanced since the task last looked, so the
channel's scale and pause apply, and a task that is paused (or a clock that is held)
banks no time. Asking twice between two clock ticks gives nothing the second time, so
updating a scene twice in one frame does not run its channel timers twice. The first
update after scheduling takes that frame's share, as a scene-time task would. A clock
that was reset (a new Play session) resyncs instead of running backwards. `clock`
wins over `unscaled`; an unknown name creates a channel on real time, like `Clock::Get`.

The channels only move when the `ClockSet` is ticked, and `GameLoop` does that every
frame. A host with its own loop (the player, the editor's Play mode) is covered too: a
scheduler or tween manager that follows a channel of the process-wide set ticks it itself,
once per `Time::FrameCount()`, and skips a frame that was already ticked (so it never
counts twice). Where `Time` is never stepped there is no frame time to follow and channel
tasks stand still. For tests, give a scheduler (`SetClockSet`) or a `TweenManager`
(`SetClockSet`) a `ClockSet` of its own, tick it with fake frame times and nothing
depends on the wall clock; a set you pass is never ticked for you.

### Global scheduler

Scene timers die with their scene: `Scene::Clear` and every scene load drop them.
`Scheduler::Global()` is process-wide and survives both, so use it for things like
a session timer, an autosave, or a delayed scene change.

```cpp
Scheduler::Options o; o.tag = "autosave";
Scheduler::Global().InvokeRepeating(60.0f, [] { /* write an autosave */ }, -1, o);
```

It is ticked exactly once per frame, at the end of `Scene::Update`, however many
scenes update in that frame (or if one updates twice). The frame is told apart by
`Time::FrameCount()`; a host that never steps `Time` (frame count stays 0) is
treated as a new frame whenever the same scene updates again.

**A global callback that captures a scene object must say so** with `Options::object`
(see the next section). Without it, the pointer dangles after a load. The other safe
ways are to look the object up by name or id inside the callback, or to tag the task
and `CancelByTag` it when the scene goes away.

### Tasks that die with their owner

```cpp
Scheduler::Options o; o.object = door;                 // door is a GameObject*
Scheduler::Global().Invoke(5.0f, [door] { door->active = false; }, o);   // safe: gone with the door

auto token = std::make_shared<int>();                  // any other kind of owner
Scheduler::Options w; w.alive = [weak = std::weak_ptr<int>(token)] { return !weak.expired(); };
```

At the start of every update, before a task advances, the scheduler checks its owner:
a destroyed object, a scene load that freed it, or an `alive` predicate that returns
false (or throws) drops the task without running it. The check costs one registry lookup per
bound task per update. `ActiveCount()` still counts such a task until that update;
`PurgeDeadOwners()` removes them at once and returns how many it dropped. Scheduling a
task for an object that is already destroyed returns `Scheduler::InvalidHandle` and
schedules nothing. The scene's own scheduler accepts `object` too (a destroyed object
ends its tweens at the next update), and the visual-script `tween_move`, `tween_scale`,
`shake`, `punch_scale`, `tween_rotate`, `tween_color`, `fade` and `flash` instructions
use it.

### Edge cases and limits

- Delays: NaN and negative become 0 (due on the next update); infinity never fires.
  Intervals and tween durations are at least 0.1 ms, so NaN or 0 cannot make a task
  that never ends.
- A frame time that is NaN, negative or infinite counts as 0, so it cannot poison every
  timer. The same goes for the tween engine and the plain `Tween` struct.
- A repeating timer that is far behind (a long hitch, a suspended machine) runs at most
  `Scheduler::kMaxCatchUp` (4096) times in one update; the rest of the backlog is dropped
  and the timer keeps its rhythm. A normal catch-up still delivers every missed run.
- A task's elapsed time is kept in double precision: a ten hour timer fires on time. So is a tween's
  timeline (see "Tweens that run for hours").
- A callback that throws: that task is dropped (a repeating timer would only throw again
  next frame), the other tasks of the update still run, and the first exception is
  rethrown when the update is done. The scheduler is consistent by then. The tween manager
  does the same for tweens.
- A callback that calls `Update` on the scheduler that is running it: the nested update advances
  every other task, but the task whose callback is on the stack is skipped (it is neither stepped nor
  run again, so it keeps its period and its repeats). The same goes for its `alive` predicate.

## Tweens

```cpp
DOTween::Move(go, {0, 3, 0}, 0.5f)->SetEase(Ease::BackOut).SetLoops(-1, LoopType::Yoyo);
scene.tweens().To([&]{ return v; }, [&](float x){ v = x; }, 10.0f, 1.0f);
```

### Callback order

Within one update, in this order: `OnStart` (once, when the delay ends), `OnUpdate`,
`OnStepComplete` (once per loop finished), `OnComplete`. After the update that
completes an auto-kill tween, `OnKill` follows.

### Re-entrancy

Callbacks may call `Kill`, `Complete`, `Restart`, `Rewind`, `Goto` or `Play` on the
tween that is firing them. When a callback does, the rest of that update is dropped:
a killed tween fires nothing more (no `OnStepComplete` or `OnComplete` after
`Kill()` in `OnUpdate`), and a restarted one starts clean (`Restart()` in `OnUpdate`
on the last frame cancels that completion). If a callback calls `Complete()`, the
step callbacks still owed are fired by that call, so no loop is counted twice or lost.

`Kill(true)` completes the tween first and fires `OnComplete` exactly once; it never
fires `OnKill` twice, even when `OnComplete` itself calls `Kill(true)`. `Complete()`
on a tween already at its end does nothing. On an endless tween, `Complete()` renders
the exact end of the current loop (a yoyo loop that runs back lands on its start
value) and fires `OnComplete` once.

A callback may call `Update` on the manager that is running it (a loading screen that pumps frames, a test that
steps the world). The nested update advances every other tween, but the tween whose callback is on the stack,
and the sequence that holds it, are left to the outer update: no callback runs inside itself (a zero step
used to recurse until the stack was gone), and `OnComplete` still fires once (the nested update used to finish and
kill the tween before the outer render got to it). The scheduler does the same for its tasks.

### Loops, ends and big steps

Yoyo and restart loops land exactly on the end values (the final frame applies the
exact end, not a nearly-there approximation). `PlayBackwards` ends exactly on the start
value and pauses. `SetLoops(0)` (or setting `loops = 0` directly) plays once.

A frame hitch spanning several loops fires `OnStepComplete` once per loop crossed.
For absurd steps the calls in one update are capped at 4096, and non-finite frame
times (NaN, infinity) are ignored.

A duration of zero or less is treated as an instant (very short) tween: it applies
its end value and completes on the next update. `Goto(seconds)` counts from the end
of the delay.

### Time scale

Tweens follow the time scale; `SetSpeed` multiplies it for one tween;
`SetUpdate(true)` makes a tween ignore it (menus and pause screens).
`TweenManager::Update(dt, unscaledDt)` takes both. `SetClock("ui")` makes a tween follow
a named clock channel instead (see "Clock channels" above): the channel's own scale and
pause apply, and the tween keeps no time while it is paused. The clock wins over
`SetUpdate`. Only the outermost tween of a sequence reads a clock; the tweens inside
take their time from the sequence. `TweenManager::SetClockSet(&set)` points a manager at
a `ClockSet` of its own, which is how tests drive it with fake frame times.

**A pause menu.** `PauseAll(true)` pauses every tween except the ones that do not follow the game's time, and
`PlayAll()` resumes them (a tween that was playing backwards goes on backwards). A tween keeps playing through
`PauseAll(true)` when it ignores the time scale (`SetUpdate(true)`), or when it follows a clock channel that runs
on real time, is not held by the game pause and is not paused itself: `"ui"` and a channel of your own. A
tween on `"game"` (the game's own time), on `"cinematic"` (stands still with the game), on a channel that is
paused, and one on scene time is paused. A tween with a channel follows the channel only, so `SetUpdate(true)`
does not exempt it (a tween on `"game"` that also ignores the time scale is paused). `PauseAll()` with no
argument pauses everything. A tween paused this way stays paused when its channel runs again; `PlayAll()` is
what resumes it.

### Eases

`Ease` is an enum of presets (`QuadOut`, `BackInOut`, `BounceOut`...). Their names read
and write as text for data files and scripts: `Easing::ToName(Ease::QuadOut)` is
`"OutQuad"` and `Easing::FromName` takes that, `"out_quad"`, `"Out Quad"`,
`"easeOutQuad"` and `"quad_out"` back (unknown text gives the fallback you pass).

Beyond the presets, an ease can be a curve you describe:

```cpp
tween->SetEaseBezier(0.4f, 0.0f, 0.2f, 1.0f);       // CSS cubic-bezier(0.4, 0, 0.2, 1)
tween->SetEaseSteps(4);                             // a staircase of 4 jumps (SetEaseSteps(4, true): jump first)
tween->SetEaseCurve([](float t) { return t * t; }); // any function of time 0..1
tween->SetEaseSpec("cubic-bezier(0.4,0,0.2,1)");    // the same from text; also "steps(4)", "ease-in-out", "OutBack"
```

`SetEase` puts a preset back. An `AnimationCurve` plugs in the same way:
`tween->SetEaseCurve([curve](float t) { return curve.Evaluate(t); })`.
`Easing::CubicBezier(x1, y1, x2, y2, t)` and
`Easing::Steps(n, t, jumpStart)` are plain functions too, and `EaseCurve` holds one
as data with `Parse(text)` / `ToString()` that read back exactly. The y values of a
bezier may leave 0..1 to overshoot; the x values are limited to 0..1.

### Tweening any property

```cpp
DOTween::Property(go, "Light.intensity", PropertyValue::MakeFloat(3.0f), 0.5f);
DOTween::Property(go, "Transform.position", PropertyValue::MakeVec3({0, 2, 0}), 1.0f)->SetEase(Ease::BackOut);
DOTween::Property(go, "SpriteRenderer.color", PropertyValue::MakeColor(Color::Red), 0.3f);
DOTween::Property(go, "Transform.rotation", PropertyValue::MakeQuat(Quat::Euler(0, 90, 0)), 1.0f);
```

The path is any `PropertyRegistry` path (docs/properties.md): `Float`, `Int` (rounded),
`Vec2`, `Vec3`, `Vec4`, `Color` and `Quat` (slerp) properties tween, a path to one vector
component (`Transform.scale.y`) is a Float, and `to` must have the property's type (an
`Int` is accepted for a `Float`). The start value is read when the tween starts, so
`SetRelative`, `From`, delays, loops and callbacks work as for any tween. Every frame
writes through `PropertyRegistry::SetProperty`, so the property's own range clamps apply and
the change hook fires. The path is resolved again each frame: a removed component just
stops the effect. Scripts have it as `tween_prop("Light.intensity", 3, 0.5[, ease][, "done"])`.
When the path cannot be tweened (unknown, wrong type, a String or Enum)
the result is an inactive tween (`IsActive() == false`) and the optional last argument
says why. The tween is killed with `go`.

### Springs

A tween has a start, an end and a time. When a target keeps moving (a follow camera, a
health bar chasing its value, a UI element tracking the cursor) a spring is the better
fit: it carries its velocity, so a new target bends the motion instead of restarting it.
`Math/Spring.hpp`:

```cpp
float x = 0, v = 0;                                           // keep these between frames
Spring::Step(x, v, target, /*stiffness*/ 120.0f, /*damping*/ 14.0f, dt);   // rings a little
Spring::StepCritical(x, v, target, /*smoothTime*/ 0.25f, dt);               // no overshoot

Vec3 p, pv;  Spring::StepCriticalVec3(p, pv, targetPos, 0.2f, dt);
Quat q; Vec3 w;  Spring::StepQuatCritical(q, w, targetRot, 0.3f, dt);       // shortest arc
Spring::StepAngle(deg, degVel, targetDeg, 100.0f, 20.0f, dt);               // 350 -> 10 goes up through 360

Spring s; s.SetCritical(0.2f); s.target = 3.0f; s.Update(dt); use(s.position);   // state in one object
```

The steps use the closed-form solution of the spring equation, so the result is the same
for one step of 2 s or two hundred of 0.01 s, and a huge hitch just lands on the target (a
stiff spring integrated frame by frame blows up on a long frame). Damping equal to
`2 * sqrt(stiffness)` is critical; less rings, more is sluggish. `Mathf::SmoothDamp` and
`Vec2/Vec3::SmoothDamp` are the older approximation of the critical case and still work.
A step that is zero, negative, NaN or infinite changes nothing. They are pure functions,
deterministic and safe from any thread.

### Tweens that run for hours

The timeline of a tween is kept in double precision (`ElapsedD()`, `FullDurationD()`, `RenderAt(double)`),
and the loop number and the place inside the loop are worked out from it. A loading spinner or a
bobbing coin that has been looping for ten hours still moves at the same pace as in its first
minute, and its turning points are exactly the end values. (A float timeline gets coarser the
longer it runs: past about nine hours a 0.1 s step rounds to 0.1016 s.) A tween with a delay
completes at exactly delay plus its length: the end is "the time reached the full length", not a
comparison of two floats that could miss by one rounding step. The plain `Tween` struct keeps its
elapsed time in double too. The timeline stops at 1e9 seconds (31 years); an endless tween sent past
it stays a number.

### Sequences

```cpp
auto seq = scene.tweens().Sequence();
seq->Append(a).Join(b).AppendInterval(0.2f).AppendCallback(done);   // after, with, pause, call
seq->Prepend(intro);                          // plays before everything already in it
seq->JoinCallback([] { /* when the last appended tween starts */ });
seq->SetLoops(3, LoopType::Yoyo).OnStepComplete(...).OnComplete(...);   // loops, yoyo, step callbacks work as for any tween
seq->Length();                                // seconds of one loop
seq->Goto(1.5f); seq->Rewind(); seq->Complete();
```

A sequence is a tween: `SetLoops`, `SetDelay`, `SetSpeed`, `SetEase`, `Pause`, `Kill(true)`, `Goto`, ids and
tags all work on it, and a sequence can hold sequences. In a Yoyo loop the children run backwards on the
way back and the sequence ends exactly on its start; callbacks added with `AppendCallback` /
`InsertCallback` / `JoinCallback` fire on the way forward only.

Ending a sequence (`Kill`, a manager `Clear`, a scene load, its object dying, a callback that throws) ends the
tweens it holds too, silently (no `OnKill` for the children; the sequence's own `OnKill` fires on `Kill`). A
handle you kept to one of them reports `IsActive() == false` and cannot restart or write into the scene any more
(it used to stay "active" for ever, and `Restart()` on it wrote into objects a load had freed). Nested sequences
with loops of every kind render the same at time T whether played there frame by frame or reached with
`Goto(T)` (`timers_fifth` block 5 checks 600 random shapes).

### Speed based tweens

```cpp
DOTween::Move(go, target, 3.0f)->SetSpeedBased();     // 3 units a second: 6 units away takes 2 s
```

`SetSpeedBased()` makes `duration` a speed. The length is worked out when the tween starts (after its
delay) from the distance between the start and end values (`ValueTween`: moves, scales, UI positions and
sizes, colours, `DOTween::Property` floats and vectors), so `Duration()` reads the speed until then.
Speed zero, negative, NaN or tiny means an instant tween, and a distance of zero too. Tweens that cannot
measure a distance (rotations, punches, shakes, jumps, paths, quaternion tweens) and tweens inside a
sequence read `duration` as seconds. Script: `tween_speed_based(id)`.

### Aiming a running tween somewhere else

```cpp
auto t = scene.tweens().ToVec3([&] { return p; }, [&](const Vec3& v) { p = v; }, target, 1.0f);
t->ChangeEnd(newTarget);     // same start, new end, progress kept
```

`ValueTween::ChangeEnd` takes a float, `Vec2`, `Vec3`, `Color` or the four-value array. The start stays
where it was read; a relative tween takes the new value as an offset again; `From()` tweens move the start of
the motion. A value that is not a number leaves that part where it was. The motion jumps onto the new line at
the current progress. When the target keeps moving and you want the motion to bend instead, use a `Spring`.

### Tweens for values you own

`TweenManager::ToVec2`, `ToVec3`, `ToColor` (every channel, alpha too) and `ToQuat` (slerp, the short way
round) tween a value through your own getter and setter, next to `To` for floats. `ToQuat` with
`SetRelative()` applies the end rotation on top of the start. They support delay, loops, yoyo, ease, `From`,
`SetRelative` and the rest like any tween, and a null getter or setter is harmless.

### Tags

`SetTag("menu")` labels a tween next to its id. An id names one tween for scripts; a tag groups many:
`TweenManager::KillByTag(tag[, complete])`, `PauseByTag`, `PlayByTag`, `CompleteByTag`, `WithTag`, `CountByTag`,
and `Rewind(id)` next to the by-id calls. Script: `tween_tag(id, "menu")`, `tween_kill_tag("menu"[, complete])`,
`tween_pause_tag`, `tween_play_tag`, `tween_complete_tag`, `tween_rewind_id("name")`.

### Punch and shake with a falloff

The punch and shake tweens fade out in a straight line. `SetDecay(rate)` swaps that for an exponential
falloff (`Easing::Decay(t, rate)`: 1 at the start, exactly 0 at the end; 4 is lively, 8 is snappy), so the
motion is quiet sooner and the tail short:

```cpp
DOTween::PunchScale(go, {0.3f, 0.3f, 0}, 0.5f, 8)->SetDecay(5.0f);
DOTween::ShakePosition(hud, 0.4f, {6, 6, 0}, 14)->SetDecay(8.0f);
```

Script: `tween_decay(id, rate)`.

### Riding a spline

`DOTween::AlongSpline(go, splineObject, duration, orient)` moves `go` along the `Spline` component on
`splineObject` at constant speed (world position; `orient` also faces along the curve with the spline's up).
It supports ease, loops and yoyo, finds the spline again every frame (removing it just stops the effect) and
dies with `go`. For plain waypoints without a Spline component use `DOTween::Path`.

### Callbacks that change the sequence they run in

A callback may add to the sequence that is running it (`Append`, `AppendCallback`...), or
`Complete` / `Restart` / `Kill` that sequence. The list moving under the running loop,
and a second render started by the callback overwriting the first one's state, are both
handled: what the callback appended starts at the end as usual, and each callback fires
once.

If a tween callback throws, that tween is ended, the other tweens of the update still
run, and the first exception is rethrown when the update is done (same rule as the
scheduler).

### Debug view

`scene.scheduler().Dump()` and `scene.tweens().Dump()` return a short multi-line text
for a debug overlay, one line per task:

```
Scheduler: 2 tasks
#3 repeat every 0.5s (forever), next in 0.2s tag=wave [ui]
#4 tween 2s, left 1.5s [object] [paused]
Tweens: 1 (1 playing)
#2 value "fade" 0.25/1s loop 1/1 [ui]
```

`Scheduler::Tasks()` gives the same as a list of `TaskInfo` (kind, tag, remaining, repeats
left, paused, clock, whether its object is still alive, its object's name, how long it has run, how far it is
overdue), in the order they were scheduled. `TweenManager::Infos()` is the same for tweens.

`Core/TimerDebug.hpp` turns both into the lines an overlay or a console prints: counts by kind, the next
task due, tasks that are overdue (paused or on a clock that does not tick) or whose owner is gone, groups by
tag, owner (an object's name) and tween id, tweens that finished but are kept (auto kill off), the
longest-running tasks and tweens.

```cpp
for (const std::string& line : TimerDebug::BuildLines(&scene.scheduler(), &scene.tweens()))
    DrawLabel(line);
//   Scheduler: 12 tasks (7 delays, 3 repeating, 2 tweens), 2 paused
//     next due in 0.25s (#7), 1 overdue
//     tags: wave x5, ui x3, (none) x4
//     owners: Boss x4, 0x5612a0 x2
//     oldest: #3 repeat every 0.5s [wave] running 2m05s
//   Tweens: 4 (3 playing, 1 paused), 1 done and kept
//     ids: fade x2, (none) x2
//     oldest: #2 value "fade" running 35s, loop 3/forever

DebugOverlay::AddLine([&] { return TimerDebug::Summary(&scene.scheduler(), &scene.tweens()); });
//   Timers: 12 (2 paused, next 0.25s) | Tweens: 4 (3 playing)
```

`TimerDebug::Capture(...)` gives the numbers (`SchedulerStats`, `TweenStats`) if you want to draw your own.
`TimerDebug::Options` limits the groups and oldest entries listed. Ties are broken by name and handle, so the
text is the same every run.

Cost: a capture walks every task and tween once (`Tasks()` / `Infos()`). `Summary` and any capture with
`showGroups` false and `topGroups` 0 do not build the tag, owner and id tables, and the `oldest` list is picked
with a partial sort that copies only `topOldest` entries, so a one-line summary is cheap enough to evaluate every
frame. With 10 000 tasks and 5 000 tweens (a release build) `Summary` went from 7.8 ms to 2.5 ms and `BuildLines`
from 8.5 ms to 3.4 ms; what is left is copying the task and tween lists, so keep the full `BuildLines` for a panel you
open and refresh it a few times a second, and call `Summary` as often as you like.

### Lifetimes

- Tweens made with the `DOTween::` shortcuts are killed (silently, no `OnKill`)
  when their target object is destroyed, in `Scene::FlushDestroyed`.
- The colour, text and counter shortcuts find their component again on every frame,
  so removing that component mid-tween just stops the effect instead of writing into
  freed memory.
- `Scene::Clear`, a scene load and destroying a `TweenManager` silently end every
  tween in it (and the tweens inside a sequence). A tween you still hold reports `IsActive() == false`
  afterwards and every call on it does nothing.
- `Scene::Clear()` called from a timer or tween callback (the scene is walking its lists) is queued and
  happens at the end of the frame, so a timer or tween the callback creates after the call goes with the old
  scene: its handle is valid for the rest of the frame and gone after it. To start fresh work in the new world,
  `RequestLoad` and schedule from the loaded scene (or from `Scheduler::Global()`, which a clear does not touch).
- A tween made with `TweenManager::To` or `Virtual` writes through your own
  functions and has no target object. If those functions touch a `GameObject`,
  set `SetTarget(go)` so the tween dies with it.

### TweenAnimation component

The inspector tween (`Components/TweenAnimation.hpp`) has every setting of a code tween:
type, end value, relative, from, duration, delay, ease, loops and loop type (restart,
yoyo, incremental), play on start, auto kill, ignore time scale, id (for
`tween_play_id`). Two fields were added: `clock` (a channel name) and `easeCurve` (an ease
as text, e.g. `cubic-bezier(0.4,0,0.2,1)` or `steps(4)`; it replaces `ease` when it
parses). Both are saved in the scene file as an optional tail after the id (nothing is
written while they are empty, so scenes without them are byte for byte as before, and an
older engine reads a newer scene and ignores the tail). Every float of the component
round trips exactly. Removing the component (or its object) now ends its tween.

Three more fields drive the tools the code tweens already had: `speedBased` (the duration is a speed in units
per second: move, scale, UI move and size, fade and colour; the others read it as seconds), `decay` (how a punch or
shake dies away: 0 is the straight fade, 4 lively, 8 snappy; punch and shake types only) and `tag` (a group label
for `TweenManager::KillByTag`, `PauseByTag`, `PlayByTag` and the script `tween_kill_tag` family). They are saved
after the clock and the curve as ` "<clock>" "<curve>" <decay> <speedBased> "<tag>"`, and only when one of the three is
set (a scene that sets none is byte for byte what it was before, and a file with only the clock and the curve loads
with the defaults). An engine that stops after the curve reads the numbers as an unknown field, warns and steps over
the rest of the line, so a newer scene still loads there. The record is not in the property registry
(`BuiltinProperties.cpp`): the Inspector draws it by hand.

The Inspector shows both (editor hooks): under **Ease** there is a **Curve** text field and a
**Clock** combo.

- **Curve** takes what `EaseCurve::Parse` takes: `cubic-bezier(0.4, 0, 0.2, 1)`, `steps(4)`,
  `steps(4, start)`, `ease`, `ease-in`, `ease-out`, `ease-in-out`, or a preset name (`OutBack`). While the text
  parses it is stored in `easeCurve` and replaces the preset; the **Ease** combo then reads "Custom curve" and
  the small drawing at the end of its row is the curve you typed (a staircase is drawn as steps, a bezier
  that overshoots is kept inside the box). Text that does not parse is shown in red under the field and
  the old value stays; when the field loses focus the stored text is shown again. Empty goes back to the
  preset. A scene that already holds text the parser refuses shows "Not an ease: the Ease above is used".
- **Clock** is "Scene time" (empty `clock`), then every channel of `ClockSet::Global().Names()` (`game`,
  `ui`, `cinematic`, plus any channel a game made), then a "custom channel..." box with a **Use** button for
  a name that does not exist yet (`Clock::Get` makes it on real time the first time something reads it). A
  custom name already on the component is listed too. The channel's scale and pause apply, and it wins
  over **Ignore time scale**.
- The editor's **Preview** button plays the tween on real time whatever its clock is: there are no
  running clocks while editing, so a "ui" or "cinematic" tween would otherwise not move.
- Scripts: `tween_clock` and `tween_prop` are listed in the Script Editor's API reference and its
  autocomplete next to the other `tween_*` builtins, and the reference now says that the ease argument
  takes curve text.

## What changed

Bugs fixed (each with a regression test):

- `Scheduler`: a NaN delay, interval or duration made a task that never fired and never
  went away; now NaN counts as 0 (delay) or the 0.1 ms minimum. A NaN, negative or
  infinite frame time poisoned every timer for good (or, for infinity, looped forever);
  now it counts as 0. A repeating timer with a tiny interval ran billions of times after
  a huge step (`Update(1e30)` hung); now capped at `kMaxCatchUp` runs per update. A
  callback that threw left the update bracket open, so cancelled tasks were never freed
  and every later `Cancel` only marked; now the update is exception safe (see "Edge
  cases and limits"). Elapsed time was a float, so a ten hour timer fired about 6% late;
  it is a double now.
- `TweenManager::Update`: a callback that moved a tween into a sequence (`Append`,
  `Join`, `Insert`) shifted the list and made the next tween skip a frame.
- Tweens: a NaN or infinite duration, speed or `Goto` time poisoned the timeline
  (an undefined cast and a tween that never ended); a throwing callback ended the
  update for all later tweens and left the tween throwing every frame.
- `TweenSequence`: appending to a sequence from a callback while it restarted a loop, or
  appending a callback from a sequence callback, used freed memory (found by a randomised
  test under valgrind). A callback that called `Complete()` on its own sequence fired
  itself again and left the sequence half complete (its `OnComplete` fired twice).
- `TweenAnimation`: removing the component left its (possibly endless) tween running.
- Visual-script `tween_move`, `tween_scale`, `shake`, `punch_scale`, `tween_rotate`,
  `tween_color`, `fade` and `flash` kept writing into a destroyed object's components;
  they now end with the object.
- `Tween` (the plain struct in Easing.hpp): a NaN or negative step and a NaN duration
  are harmless; the `DOTween::` shortcuts that cannot make a tween (no object, no
  component) now return a tween with `IsActive() == false` instead of one that claims to
  be active and never runs.

Added:

- Scheduler: `Options::clock`, `Options::object`, `Options::alive`, `CancelByObject`,
  `PurgeDeadOwners`, `SetPausedByTag`, `SetPausedByOwner`, `CountByTag`, `Tasks()`,
  `Dump()`, `SetClockSet`, `kMaxCatchUp`.
- Tweens: `SetClock`, `SetEaseBezier`, `SetEaseSteps`, `SetEaseCurve`, `SetEaseSpec`,
  `Eased`, `TweenManager::SetClockSet`, `TweenManager::Dump`, `DOTween::Property`.
- Easing: `Easing::ToName`, `Easing::CubicBezier`, `Easing::Steps`, `EaseCurve`.
- `Spring` (`Math/Spring.hpp`): `Step`, `StepCritical`, `StepVec2/3`, `StepCriticalVec2/3`,
  `StepAngle`, `StepQuat`, `StepQuatCritical`, and the `Spring` state object.
- `Core/ClockCursor.hpp` (what channel tasks and tweens use to follow a clock).
- TweenAnimation: `clock` and `easeCurve`, saved in the scene file.
- Scripts: `tween_clock(id, channel)`, `tween_prop(path, to, dur)`, and `tween_ease` (and the
  ease argument of the `tween_*` builtins) accept curve text.

Not done (follow-ups, second pass): `Dump()` in a debug overlay panel (done in the fourth pass, see below);
visual-script nodes for a named or bezier ease and for springs; a `Quat` version of `Mathf::SmoothDamp` is
`Spring::StepQuatCritical`.

### Fourth pass (scale, long runs, sequencing, juice, debug view)

Bugs fixed (each with a regression test; the tests that fail on the code before the change are noted):

- `Scheduler`: every lookup by handle was a linear scan and every cancel erased from the middle of a
  vector, so cancelling 20 000 timers one by one took 5 s and the time grew with the square of the count
  (10 000 took 1.7 s). Handles now resolve through an index and removal leaves a tombstone that is swept
  in bulk; tags, owners and objects have their own group tables, so a group is cancelled, paused or
  counted by touching only its members. `scheduler_scale` (100 000 timers, by handle, by owner, by tag).
- `Scheduler`: a captured value whose destructor called back into the scheduler (an RAII timer handle
  that cancels in its destructor, a callback object that schedules) ran while the task list was being
  torn down: destroying a scheduler that held such a task aborted with a double free, and `Clear()` lost
  the cancel and the new timer that destructor asked for. Tasks now leave every table first and their
  callbacks are freed last. `scheduler_scale` blocks 13 and 14.
- `Tweener`: a tween whose delay did not fit a float (0.1, 0.3, 0.7, ...) could never complete: the end
  test compared `time - delay` with `duration * loops` in float and missed by one rounding step, so the
  tween froze one hair short of its end, never fired `OnComplete` and was never removed (30 of 175
  delay / length / loop combinations in the test). The end is now "the time reached the full length".
  `tween_long_run` block 6.
- `Tweener`: the timeline was a float, so a tween that had run for hours drifted: after ten hours of 0.1 s
  frames an endless loop was 41 s behind (a 0.1 s step rounds to 0.1016 s), a yoyo no longer turned on
  its end values, and an endless loop sent far out (5e8 s into a 0.1 ms loop) stopped moving (the loop
  counter is capped at two billion; the phase now comes from the double time). `tween_long_run`.
  The plain `Tween` struct had the same drift (an hour long tween was 0.3 s off at its half way point).
- Tweens wrote NaN into objects: an end value that is NaN or infinite, an ease curve that returned NaN,
  a start value that already was NaN, and non-finite punch, shake, jump, rotation and path settings all
  ended up in the transform on every frame. They are now ignored (that part stays where it was) and a
  bad curve counts as a straight line. `timers_audit` block 1, `tween_sequencing` block 26.
- `Easing::Evaluate` returned NaN or infinity for NaN or infinite input and for inputs far outside 0..1
  (`ExpoIn` past 13 overflows); it now treats NaN as 0, infinity as the nearer end and stops overflow at
  plus or minus 1e30. `CubicBezier` with a NaN control point x counts it as 0. `timers_audit` block 12.
- `Spring::Step` with a damping far above critical used the difference of two nearly equal rates for the
  slow decay and got it wrong (a 4% error in the test, worse for stiffer settings); it now uses
  `w0^2 / (a + b)`. `timers_audit` block 16.
- `TweenManager::Add` of a tween that another manager owned left it in both lists, so it was advanced
  twice a frame; it now leaves the first manager. `PlayAll()` (the end of a pause menu) turned a tween that
  was playing backwards round; it now only resumes. `timers_audit` block 19, `tween_sequencing` block 21.
- Script `after(NaN, "fn")` and `every(NaN, "fn")` made a timer that never fired and never went away, and
  a NaN frame time stopped every script timer; NaN counts as 0 (after) or the shortest interval (every).

Added:

- Scheduler: O(1) handle lookups and group operations (see above), `TaskInfo::objectName`, `age`, `overdue`.
- `Core/TimerDebug.hpp`: `TimerDebug::BuildLines`, `Summary`, `Capture`, `FormatSeconds` (see "Debug view");
  `TweenManager::Infos()`.
- Tweens: `ElapsedD`, `FullDurationD`, `RenderAt(double)`, `SetSpeedBased`, `SetTag`, `SetDecay`,
  `ValueTween::ChangeEnd`, `TweenManager::ToVec2/ToVec3/ToColor/ToQuat`, `WithTag`, `KillByTag`, `PauseByTag`,
  `PlayByTag`, `CompleteByTag`, `CountByTag`, `Rewind(id)`, `TweenSequence::Prepend`, `JoinCallback`, `Length`,
  `DOTween::AlongSpline`, `Easing::Decay`.
- Scripts: `tween_tag`, `tween_speed_based`, `tween_decay`, `tween_kill_tag`, `tween_pause_tag`,
  `tween_play_tag`, `tween_complete_tag`, `tween_rewind_id`, `seq_prepend`, `seq_join_callback`.

Checked and fine: owners destroyed from inside a callback, the scene cleared from inside a scheduler or
tween callback, a callback that updates its own manager, zero-length and negative durations with endless
loops, `INT_MAX` loops, a TweenAnimation saved while playing and loaded again (settings round trip; the copy
plays on from the saved pose), a TweenAnimation record full of NaN and infinity (the scene still loads),
springs at stiffness zero or below and at critical damping, the clock cursor after a reset. Pinned by
`timers_audit`.

Not done: `TweenAnimation` has no fields for `decay`, speed based or a tag (they would need an extension of the
scene record and an Inspector row; the code tweens have them); a `Scheduler` still must be used from one thread;
a callback that calls `Update` on the scheduler that is running it consumes the running task's period without
running it (nothing in the engine does this); visual-script nodes for the new tweens.

### Fifth pass (nested updates, pause menus, sequence handles, TweenAnimation tools, nodes)

Bugs fixed (each with a regression test; `timers_fifth` unless noted, and the blocks fail on the code before the change):

- `Scheduler`: a callback that called `Update` on the scheduler running it used up the running task's period (and one
  repeat of a counted timer) without running it, and stepped a tween task a second time without calling its callbacks.
  The nested update now skips the task whose callback is on the stack (a `running` flag on the task). Block 1.
- `TweenManager::PauseAll(true)` spared only `SetUpdate(true)` tweens, so a tween on a real-time clock channel ("ui", or a
  channel of your own) froze with the pause menu it was meant to animate, while a tween on `"game"` with
  `SetUpdate(true)` kept running with the game paused. The rule is now "does not follow the game's time" (see "A pause
  menu" under "Time scale"). Block 2.
- `TweenManager::Update` called from a tween's own callback re-entered that tween: its `OnUpdate` ran again inside itself
  (for ever with a zero step: the stack overflowed) and an `OnStepComplete` that updated the manager finished and killed
  the tween before its `OnComplete` fired. The tween (or the sequence) whose callback is on the stack is skipped by the
  nested update. Block 10.
- Ending a `TweenSequence` (`Kill`, a manager `Clear`, a scene load, its object dying, a throwing callback) left the tweens
  inside it "active" for ever, and a handle you held to one of them could `Restart()` and write into a scene that a load
  had freed. They end with the sequence now, silently. Block 4.
- Scripts: `tween_loops(id, nan)` looped for ever on some machines and `tween_loops(id, 1000000000000)` became a negative
  count (also "for ever") because a float far out of range was cast to int, which is undefined; the same cast fed the
  vibrato of `tween_punch_*` / `tween_shake*` and the jumps of `tween_jump`. A NaN is the default (once, or the usual
  vibrato) and a huge count stops at two billion. The `ease_in` ... `ease_bounce` functions returned NaN for a NaN `t`;
  they return the ease of 0 like `Easing::Evaluate`. Block 6.
- Shake tweens: the step number was an int fed with `vibrato * duration`, which a vibrato near `INT_MAX` on a long shake
  pushed far past int range; it is capped at 1e8 steps.
- `TimerDebug::Summary` (made for a one-line overlay that is evaluated every frame) and a `BuildLines` that hides the groups
  still built the tag, owner and id tables and sorted a copy of every task and tween to pick the three oldest. With 10 000
  tasks and 5 000 tweens that was 7.8 ms a call (measured before the change; 2.5 ms after). Tables are only built when
  asked for and the oldest come from a partial sort that copies only the ones it shows. Block 7.
- `Easing::Decay`'s header comment said "a big first swing"; a larger rate makes the motion quiet sooner.

Added:

- `TweenAnimation`: `speedBased`, `decay` and `tag`, saved as an optional tail of the `tweenanim` record (see "TweenAnimation
  component"). Block 3 (round trip of every combination with the clock and curve tail, older files, a newer build's extra
  fields, odd values, and the fields reaching the tween).
- Script `ease_curve(t, "OutBack" | "cubic-bezier(...)" | "steps(4)")`.
- Visual-script nodes `EaseCurve`, `SpringStep`, `TweenMove`, `TweenMoveSpeed`, `TweenKillTag`, `TweenPauseTag`,
  `TweenPlayTag` (see docs/visual_scripting.md; `vs_tween`).

Checked and fine (pinned by `timers_fifth`): nested sequences with loops of every kind (200 random shapes, three times
each: the state at time T from playing and from `Goto(T)` agree, and every one completes exactly once), twenty thousand
objects with two timers and a tween each destroyed in one frame (each task asks its `alive` predicate once per update; the
tweens end with the objects at once, the timers go on the next update without running; no per-object scans), a
`Scene::Clear` asked for in a callback (queued to the end of the frame, so what the callback creates after the call goes
with the old scene and its handles report inactive; documented under "Lifetimes"), a process-wide timer bound to an object of
a cleared scene (dropped without running), and a scene load requested from a callback with a timer scheduled after it.
Springs with a NaN, zero or infinite quaternion, angle or target stay finite (`StepQuat`, `StepAngle`); a spring
fed values near the float limit (about 3e38) can overflow its velocity to infinity.

Not done: Inspector rows for the three new `TweenAnimation` fields and palette entries, help text and code generation for the
seven new nodes are editor changes (the page lists them as node types; the editor's node list does not show them yet);
there is no script builtin for a spring (it needs state; the node keeps it); `TweenAnimation` is not in the property
registry; `TweenManager::OfTarget` / `IsTweening` / `WithId` / `WithTag` scan every tween (fine for hundreds, slow if a
script calls `is_tweening()` on thousands of objects every frame); a `Scheduler` still must be used from one thread.
