#include "okay/Core/Scheduler.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>

namespace okay {

Scheduler::Handle Scheduler::Add(Task&& t, const Options& options) {
    t.handle = m_nextHandle++;
    t.elapsed = 0.0f;
    t.unscaled = options.unscaled;
    t.owner = options.owner;
    t.tag = options.tag;
    m_tasks.push_back(std::move(t));
    return m_tasks.back().handle;
}

Scheduler::Handle Scheduler::Invoke(float delay, std::function<void()> fn) {
    return Invoke(delay, std::move(fn), Options{});
}

Scheduler::Handle Scheduler::Invoke(float delay, std::function<void()> fn, const Options& options) {
    Task t{};
    t.period = Mathf::Max(0.0f, delay);
    t.repeatsLeft = 0; // one-shot
    t.isTween = false;
    t.action = std::move(fn);
    return Add(std::move(t), options);
}

Scheduler::Handle Scheduler::InvokeRepeating(float interval, std::function<void()> fn, int repeatCount) {
    return InvokeRepeating(interval, std::move(fn), repeatCount, Options{});
}

Scheduler::Handle Scheduler::InvokeRepeating(float interval, std::function<void()> fn, int repeatCount,
                                             const Options& options) {
    Task t{};
    t.period = Mathf::Max(0.0001f, interval);
    t.repeatsLeft = repeatCount; // <0 = infinite
    t.isTween = false;
    t.action = std::move(fn);
    return Add(std::move(t), options);
}

Scheduler::Handle Scheduler::Tween(float duration, std::function<void(float)> onUpdate,
                                   std::function<void()> onComplete) {
    return Tween(duration, std::move(onUpdate), std::move(onComplete), Options{});
}

Scheduler::Handle Scheduler::Tween(float duration, std::function<void(float)> onUpdate,
                                   std::function<void()> onComplete, const Options& options) {
    Task t{};
    t.period = Mathf::Max(0.0001f, duration);
    t.repeatsLeft = 0;
    t.isTween = true;
    t.tweenUpdate = std::move(onUpdate);
    t.tweenComplete = std::move(onComplete);
    return Add(std::move(t), options);
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

template <class Pred>
int Scheduler::CancelWhere(Pred match) {
    int n = 0;
    if (m_updateDepth > 0) {
        for (Task& t : m_tasks) if (t.handle != InvalidHandle && match(t)) { t.handle = InvalidHandle; ++n; }
    } else {
        auto dead = std::remove_if(m_tasks.begin(), m_tasks.end(), [&](const Task& t) { return match(t); });
        n = (int)(m_tasks.end() - dead);
        m_tasks.erase(dead, m_tasks.end());
    }
    return n;
}

int Scheduler::CancelByTag(const std::string& tag) {
    if (tag.empty()) return 0;
    return CancelWhere([&](const Task& t) { return t.handle != InvalidHandle && t.tag == tag; });
}

int Scheduler::CancelByOwner(const void* owner) {
    if (!owner) return 0;
    return CancelWhere([&](const Task& t) { return t.handle != InvalidHandle && t.owner == owner; });
}

const Scheduler::Task* Scheduler::Find(Handle handle) const {
    if (handle == InvalidHandle) return nullptr;
    for (const Task& t : m_tasks) if (t.handle == handle) return &t;
    return nullptr;
}

bool Scheduler::IsScheduled(Handle handle) const { return Find(handle) != nullptr; }

bool Scheduler::SetPaused(Handle handle, bool paused) {
    if (handle == InvalidHandle) return false;
    for (Task& t : m_tasks) if (t.handle == handle) { t.paused = paused; return true; }
    return false;
}

bool Scheduler::IsPaused(Handle handle) const {
    const Task* t = Find(handle);
    return t && t->paused;
}

bool Scheduler::SetInterval(Handle handle, float seconds) {
    if (handle == InvalidHandle) return false;
    for (Task& t : m_tasks) {
        if (t.handle != handle) continue;
        t.period = t.isTween || t.repeatsLeft != 0 ? Mathf::Max(0.0001f, seconds) : Mathf::Max(0.0f, seconds);
        return true;
    }
    return false;
}

float Scheduler::Remaining(Handle handle) const {
    const Task* t = Find(handle);
    return t ? Mathf::Max(0.0f, t->period - t->elapsed) : -1.0f;
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

Scheduler& Scheduler::Global() {
    static Scheduler s;
    return s;
}

namespace {
// The real frame time for a scaled `dt`: undo the time scale; at scale 0 (paused)
// the only source left is Time's own record of the frame.
float UnscaledFrom(float dt) {
    const float ts = Time::TimeScale();
    return ts > 1e-6f ? dt / ts : Time::UnscaledDeltaTime();
}
std::uint64_t g_globalFrame = ~std::uint64_t(0);   // Time::FrameCount() of the last global tick
const void*   g_globalCaller = nullptr;
}

void Scheduler::TickGlobal(const void* caller, float dt) {
    const std::uint64_t frame = Time::FrameCount();
    bool newFrame = frame != g_globalFrame;
    // A host that never steps Time (frame count stuck at 0: headless tools, the
    // editor) gives no frame marker: the same scene updating again is a new frame.
    if (!newFrame && frame == 0 && caller == g_globalCaller) newFrame = true;
    if (!newFrame) return;
    g_globalFrame = frame;
    g_globalCaller = caller;
    Global().Update(dt, UnscaledFrom(dt));
}

void Scheduler::Update(float dt) { Update(dt, UnscaledFrom(dt)); }

void Scheduler::Update(float dt, float unscaledDt) {
    // Iterate by index and re-fetch the task after every callback: a callback
    // may Invoke (appending, possibly reallocating the list) or Cancel/Clear
    // (which only mark tasks dead while we're here). Each callback is moved out
    // of its task while it runs, so it stays alive whatever the list does.
    ++m_updateDepth;
    for (std::size_t i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].handle == InvalidHandle || m_tasks[i].paused) continue;
        m_tasks[i].elapsed += m_tasks[i].unscaled ? unscaledDt : dt;

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
        while (m_tasks[i].handle != InvalidHandle && !m_tasks[i].paused && m_tasks[i].elapsed >= m_tasks[i].period) {
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
