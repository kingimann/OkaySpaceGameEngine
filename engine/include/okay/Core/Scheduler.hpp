#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace okay {

/// Time-based callbacks: one-shot delays, repeating timers, and value tweens.
/// A Scene owns one and ticks it every frame, so gameplay code can schedule
/// work without writing its own timers. Those timers die with the scene (a
/// load or Clear drops them); `Scheduler::Global()` is the one that survives.
class Scheduler {
public:
    using Handle = std::uint64_t;
    static constexpr Handle InvalidHandle = 0;

    /// Optional per-task settings.
    struct Options {
        /// Ignore the time scale and the game pause (a pause-menu timer keeps running).
        bool unscaled = false;
        /// A label for CancelByTag. Empty = untagged.
        std::string tag;
        /// Whoever scheduled it, for CancelByOwner (usually `this`). Never dereferenced.
        const void* owner = nullptr;
    };

    /// Run `fn` once after `delay` seconds.
    Handle Invoke(float delay, std::function<void()> fn);
    Handle Invoke(float delay, std::function<void()> fn, const Options& options);

    /// Run `fn` every `interval` seconds. `repeatCount` < 0 repeats forever.
    Handle InvokeRepeating(float interval, std::function<void()> fn, int repeatCount = -1);
    Handle InvokeRepeating(float interval, std::function<void()> fn, int repeatCount, const Options& options);

    /// Call `onUpdate(t)` each frame with t going 0->1 over `duration` seconds,
    /// then `onComplete()`. Returns a handle you can Cancel.
    Handle Tween(float duration, std::function<void(float)> onUpdate,
                 std::function<void()> onComplete = {});
    Handle Tween(float duration, std::function<void(float)> onUpdate,
                 std::function<void()> onComplete, const Options& options);

    /// Cancel a scheduled task. Returns true if it existed.
    bool Cancel(Handle handle);
    /// Cancel every task with this tag / owner (safe from inside a callback,
    /// the running task included). Returns how many were cancelled.
    int CancelByTag(const std::string& tag);
    int CancelByOwner(const void* owner);

    /// Freeze / resume one task: it keeps its progress while paused.
    bool SetPaused(Handle handle, bool paused);
    bool IsPaused(Handle handle) const;
    /// Change a timer's interval (a delay's wait, a tween's duration). Progress
    /// so far is kept, so a shorter interval can fire on the next Update.
    bool SetInterval(Handle handle, float seconds);
    /// Seconds until the task fires next (a tween: until it ends). -1 if the
    /// handle is not (or no longer) scheduled.
    float Remaining(Handle handle) const;
    bool  IsScheduled(Handle handle) const;

    /// Advance all tasks by `dt` seconds. Unscaled tasks use the real frame time
    /// (derived from the time scale, as the scene's tweens do).
    void Update(float dt);
    /// Same with an explicit real frame time for the unscaled tasks.
    void Update(float dt, float unscaledDt);

    void Clear();
    std::size_t ActiveCount() const;

    /// The process-wide scheduler. Its tasks survive scene loads, Scene::Clear
    /// and Play restarts; Scene::Update ticks it once per frame. Its callbacks
    /// must not capture pointers to scene objects (they would dangle after a
    /// load): look objects up by name or id inside the callback, or tag the task
    /// and CancelByTag it when the scene goes away.
    static Scheduler& Global();
    /// Scene::Update calls this: ticks Global() once per frame however many
    /// scenes update (or update twice) in it. `caller` is the updating scene.
    static void TickGlobal(const void* caller, float dt);

private:
    struct Task {
        Handle handle;
        float  elapsed;
        float  period;       // delay / interval / duration
        int    repeatsLeft;  // for repeating tasks (>=0), -1 = infinite
        bool   isTween;
        bool   unscaled = false;
        bool   paused = false;
        const void* owner = nullptr;
        std::string tag;
        std::function<void()>      action;
        std::function<void(float)> tweenUpdate;
        std::function<void()>      tweenComplete;
    };

    Handle Add(Task&& t, const Options& options);
    const Task* Find(Handle handle) const;
    template <class Pred> int CancelWhere(Pred match);

    std::vector<Task> m_tasks;
    Handle m_nextHandle = 1;
    // >0 while Update runs: Cancel/Clear only mark tasks dead (handle = 0) so
    // the list never shrinks under a running callback; Update sweeps them.
    int m_updateDepth = 0;
};

} // namespace okay
