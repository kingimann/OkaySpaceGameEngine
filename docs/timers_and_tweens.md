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
`Clear()` cancels everything. Looking a task up by handle is a linear scan, so a scheduler
holding many thousands of tasks is better cancelled by tag or owner than one handle at a time.

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
- A task's elapsed time is kept in double precision: a ten hour timer fires on time.
- A callback that throws: that task is dropped (a repeating timer would only throw again
  next frame), the other tasks of the update still run, and the first exception is
  rethrown when the update is done. The scheduler is consistent by then. The tween manager
  does the same for tweens.

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
left, paused, clock, whether its object is still alive), in the order they were scheduled.

### Lifetimes

- Tweens made with the `DOTween::` shortcuts are killed (silently, no `OnKill`)
  when their target object is destroyed, in `Scene::FlushDestroyed`.
- The colour, text and counter shortcuts find their component again on every frame,
  so removing that component mid-tween just stops the effect instead of writing into
  freed memory.
- `Scene::Clear`, a scene load and destroying a `TweenManager` silently end every
  tween in it. A tween you still hold reports `IsActive() == false` afterwards.
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

Not done (follow-ups): `Dump()` in a debug overlay panel; visual-script nodes for a named or
bezier ease and for springs; `Scheduler` lookups by handle are still a linear scan (fine
for hundreds of tasks; cancel big groups by tag or owner); a `Quat` version of
`Mathf::SmoothDamp` is `Spring::StepQuatCritical`.
