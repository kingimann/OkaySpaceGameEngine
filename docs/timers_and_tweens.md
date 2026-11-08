# Timers and tweens

Two systems schedule work over time. The `Scheduler` runs callbacks after a delay
or on an interval. The `TweenManager` animates values with easing, loops and
sequences. Each scene owns one of each (`scene.scheduler()`, `scene.tweens()`),
plus there is one process-wide scheduler that outlives scenes.

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

Cancelling is safe from inside a callback, including cancelling the running task
and its siblings. Tasks added by a callback run in the same update when they are due.

### Scaled and unscaled time

`Scene::Update` receives the scaled frame time. The scheduler recovers the real
frame time as `dt / timeScale` (or `Time::UnscaledDeltaTime()` while the scale is 0,
which is what `Game::SetPaused` sets). You can also pass it yourself with
`Update(dt, unscaledDt)`.

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

**Global callbacks must not capture pointers to scene objects.** After a load
those objects are gone and the pointer dangles. Look the object up by name inside
the callback, or tag the task and `CancelByTag` it when the scene goes away.

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
`TweenManager::Update(dt, unscaledDt)` takes both.

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
