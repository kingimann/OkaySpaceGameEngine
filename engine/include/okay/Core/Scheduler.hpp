#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace okay {

/// Time-based callbacks: one-shot delays, repeating timers, and value tweens.
/// A Scene owns one and ticks it every frame, so gameplay code can schedule
/// work without writing its own timers (think Unity's Invoke/InvokeRepeating).
class Scheduler {
public:
    using Handle = std::uint64_t;
    static constexpr Handle InvalidHandle = 0;

    /// Run `fn` once after `delay` seconds.
    Handle Invoke(float delay, std::function<void()> fn);

    /// Run `fn` every `interval` seconds. `repeatCount` < 0 repeats forever.
    Handle InvokeRepeating(float interval, std::function<void()> fn, int repeatCount = -1);

    /// Call `onUpdate(t)` each frame with t going 0->1 over `duration` seconds,
    /// then `onComplete()`. Returns a handle you can Cancel.
    Handle Tween(float duration, std::function<void(float)> onUpdate,
                 std::function<void()> onComplete = {});

    /// Cancel a scheduled task. Returns true if it existed.
    bool Cancel(Handle handle);

    /// Advance all tasks by `dt` seconds.
    void Update(float dt);

    void Clear() { m_tasks.clear(); }
    std::size_t ActiveCount() const { return m_tasks.size(); }

private:
    struct Task {
        Handle handle;
        float  elapsed;
        float  period;       // delay / interval / duration
        int    repeatsLeft;  // for repeating tasks (>=0), -1 = infinite
        bool   isTween;
        std::function<void()>      action;
        std::function<void(float)> tweenUpdate;
        std::function<void()>      tweenComplete;
    };

    std::vector<Task> m_tasks;
    Handle m_nextHandle = 1;
};

} // namespace okay
