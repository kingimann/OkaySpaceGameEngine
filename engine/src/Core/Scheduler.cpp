#include "okay/Core/Scheduler.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>

namespace okay {

Scheduler::Handle Scheduler::Invoke(float delay, std::function<void()> fn) {
    Task t{};
    t.handle = m_nextHandle++;
    t.elapsed = 0.0f;
    t.period = Mathf::Max(0.0f, delay);
    t.repeatsLeft = 0; // one-shot
    t.isTween = false;
    t.action = std::move(fn);
    m_tasks.push_back(std::move(t));
    return m_tasks.back().handle;
}

Scheduler::Handle Scheduler::InvokeRepeating(float interval, std::function<void()> fn, int repeatCount) {
    Task t{};
    t.handle = m_nextHandle++;
    t.elapsed = 0.0f;
    t.period = Mathf::Max(0.0001f, interval);
    t.repeatsLeft = repeatCount; // <0 = infinite
    t.isTween = false;
    t.action = std::move(fn);
    m_tasks.push_back(std::move(t));
    return m_tasks.back().handle;
}

Scheduler::Handle Scheduler::Tween(float duration, std::function<void(float)> onUpdate,
                                   std::function<void()> onComplete) {
    Task t{};
    t.handle = m_nextHandle++;
    t.elapsed = 0.0f;
    t.period = Mathf::Max(0.0001f, duration);
    t.repeatsLeft = 0;
    t.isTween = true;
    t.tweenUpdate = std::move(onUpdate);
    t.tweenComplete = std::move(onComplete);
    m_tasks.push_back(std::move(t));
    return m_tasks.back().handle;
}

bool Scheduler::Cancel(Handle handle) {
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it->handle == handle) { m_tasks.erase(it); return true; }
    }
    return false;
}

void Scheduler::Update(float dt) {
    // Iterate by index; tasks may be appended during callbacks. Completed tasks
    // are marked by clearing their handle and removed at the end.
    for (std::size_t i = 0; i < m_tasks.size(); ++i) {
        Task& t = m_tasks[i];
        if (t.handle == InvalidHandle) continue;
        t.elapsed += dt;

        if (t.isTween) {
            float p = Mathf::Clamp01(t.elapsed / t.period);
            if (t.tweenUpdate) t.tweenUpdate(p);
            if (p >= 1.0f) {
                if (t.tweenComplete) t.tweenComplete();
                t.handle = InvalidHandle;
            }
            continue;
        }

        // Fire as many times as elapsed allows (handles big dt spikes).
        while (t.handle != InvalidHandle && t.elapsed >= t.period) {
            t.elapsed -= t.period;
            if (t.action) t.action();
            if (t.repeatsLeft == 0) { t.handle = InvalidHandle; break; }     // one-shot done
            if (t.repeatsLeft > 0 && --t.repeatsLeft == 0) { t.handle = InvalidHandle; break; }
        }
    }

    m_tasks.erase(std::remove_if(m_tasks.begin(), m_tasks.end(),
                                 [](const Task& t) { return t.handle == InvalidHandle; }),
                  m_tasks.end());
}

} // namespace okay
