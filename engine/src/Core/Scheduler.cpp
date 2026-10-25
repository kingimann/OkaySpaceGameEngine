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
    if (handle == InvalidHandle) return false;
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        if (it->handle != handle) continue;
        if (m_updateDepth > 0) it->handle = InvalidHandle;   // swept at the end of Update
        else m_tasks.erase(it);
        return true;
    }
    return false;
}

void Scheduler::Clear() {
    if (m_updateDepth > 0) { for (Task& t : m_tasks) t.handle = InvalidHandle; }
    else m_tasks.clear();
}

std::size_t Scheduler::ActiveCount() const {
    std::size_t n = 0;
    for (const Task& t : m_tasks) if (t.handle != InvalidHandle) ++n;
    return n;
}

void Scheduler::Update(float dt) {
    // Iterate by index and re-fetch the task after every callback: a callback
    // may Invoke (appending, possibly reallocating the list) or Cancel/Clear
    // (which only mark tasks dead while we're here). Each callback is moved out
    // of its task while it runs, so it stays alive whatever the list does.
    ++m_updateDepth;
    for (std::size_t i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].handle == InvalidHandle) continue;
        m_tasks[i].elapsed += dt;

        if (m_tasks[i].isTween) {
            const float p = Mathf::Clamp01(m_tasks[i].elapsed / m_tasks[i].period);
            auto update = std::move(m_tasks[i].tweenUpdate);
            if (update) update(p);
            if (m_tasks[i].handle == InvalidHandle) continue;   // cancelled itself
            m_tasks[i].tweenUpdate = std::move(update);
            if (p >= 1.0f) {
                auto complete = std::move(m_tasks[i].tweenComplete);
                if (complete) complete();
                m_tasks[i].handle = InvalidHandle;
            }
            continue;
        }

        // Fire as many times as elapsed allows (handles big dt spikes).
        while (m_tasks[i].handle != InvalidHandle && m_tasks[i].elapsed >= m_tasks[i].period) {
            m_tasks[i].elapsed -= m_tasks[i].period;
            auto action = std::move(m_tasks[i].action);
            if (action) action();
            Task& t = m_tasks[i];
            if (t.handle == InvalidHandle) break;                // cancelled itself
            t.action = std::move(action);
            if (t.repeatsLeft == 0) { t.handle = InvalidHandle; break; }     // one-shot done
            if (t.repeatsLeft > 0 && --t.repeatsLeft == 0) { t.handle = InvalidHandle; break; }
        }
    }
    if (--m_updateDepth > 0) return;   // a nested Update leaves the sweep to the outer one

    m_tasks.erase(std::remove_if(m_tasks.begin(), m_tasks.end(),
                                 [](const Task& t) { return t.handle == InvalidHandle; }),
                  m_tasks.end());
}

} // namespace okay
