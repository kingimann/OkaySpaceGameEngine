#include "okay/Core/Scheduler.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Scene/GameObject.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>

namespace okay {

namespace {
// A delay: NaN and negative become 0 (due on the next update); infinity stays (never).
float SaneDelay(float v) { return v > 0.0f ? v : 0.0f; }
// An interval or duration: at least a tenth of a millisecond, so NaN and 0 cannot make a task that never ends.
float SaneInterval(float v) { return v > 0.0001f ? v : 0.0001f; }
}

Scheduler::Handle Scheduler::Add(Task&& t, const Options& options) {
    if (options.object && options.object->IsDestroyed()) return InvalidHandle;   // its owner is already gone
    t.handle = m_nextHandle++;
    t.elapsed = 0.0;
    t.unscaled = options.unscaled;
    t.owner = options.owner ? options.owner : static_cast<const void*>(options.object);
    t.tag = options.tag;
    t.clock = options.clock;
    t.alive = options.alive;
    if (options.object) {
        t.objectId = options.object->InstanceId();
        t.objectPtr = options.object;
    }
    m_tasks.push_back(std::move(t));
    return m_tasks.back().handle;
}

Scheduler::Handle Scheduler::Invoke(float delay, std::function<void()> fn) {
    return Invoke(delay, std::move(fn), Options{});
}

Scheduler::Handle Scheduler::Invoke(float delay, std::function<void()> fn, const Options& options) {
    Task t{};
    t.period = SaneDelay(delay);
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
    t.period = SaneInterval(interval);
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
    t.period = SaneInterval(duration);
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

template <class Pred>
int Scheduler::PauseWhere(Pred match, bool paused) {
    int n = 0;
    for (Task& t : m_tasks) if (t.handle != InvalidHandle && match(t)) { t.paused = paused; ++n; }
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

int Scheduler::CancelByObject(const GameObject* object) {
    if (!object) return 0;
    return CancelWhere([&](const Task& t) { return t.handle != InvalidHandle && t.objectPtr == object; });
}

bool Scheduler::OwnerGone(const Task& t) {
    if (t.objectId != 0 && GameObject::FromInstanceId(t.objectId) == nullptr) return true;
    if (t.alive) {
        try { if (!t.alive()) return true; }
        catch (...) { return true; }
    }
    return false;
}

int Scheduler::PurgeDeadOwners() {
    return CancelWhere([&](const Task& t) { return t.handle != InvalidHandle && HasLiveness(t) && OwnerGone(t); });
}

int Scheduler::SetPausedByTag(const std::string& tag, bool paused) {
    if (tag.empty()) return 0;
    return PauseWhere([&](const Task& t) { return t.tag == tag; }, paused);
}

int Scheduler::SetPausedByOwner(const void* owner, bool paused) {
    if (!owner) return 0;
    return PauseWhere([&](const Task& t) { return t.owner == owner; }, paused);
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
        t.period = t.isTween || t.repeatsLeft != 0 ? SaneInterval(seconds) : SaneDelay(seconds);
        return true;
    }
    return false;
}

float Scheduler::Remaining(Handle handle) const {
    const Task* t = Find(handle);
    return t ? (float)std::max(0.0, (double)t->period - t->elapsed) : -1.0f;
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

std::size_t Scheduler::CountByTag(const std::string& tag) const {
    if (tag.empty()) return 0;
    std::size_t n = 0;
    for (const Task& t : m_tasks) if (t.handle != InvalidHandle && t.tag == tag) ++n;
    return n;
}

std::vector<Scheduler::TaskInfo> Scheduler::Tasks() const {
    std::vector<TaskInfo> out;
    for (const Task& t : m_tasks) {
        if (t.handle == InvalidHandle) continue;
        TaskInfo i;
        i.handle = t.handle;
        i.kind = t.isTween ? TaskInfo::Kind::Tween : t.repeatsLeft != 0 ? TaskInfo::Kind::Repeating : TaskInfo::Kind::Delay;
        i.tag = t.tag;
        i.clock = t.clock;
        i.owner = t.owner;
        i.period = t.period;
        i.remaining = (float)std::max(0.0, (double)t.period - t.elapsed);
        i.repeatsLeft = t.repeatsLeft;
        i.paused = t.paused;
        i.unscaled = t.unscaled;
        i.hasObject = t.objectId != 0;
        i.ownerAlive = !HasLiveness(t) || !OwnerGone(t);
        out.push_back(std::move(i));
    }
    return out;
}

namespace {
std::string Secs(float s) {
    char b[32];
    if (std::isinf(s)) return "never";
    std::snprintf(b, sizeof(b), "%.3g", (double)s);
    return std::string(b) + "s";
}
}

std::string Scheduler::Dump() const {
    const std::vector<TaskInfo> list = Tasks();
    std::string out = "Scheduler: " + std::to_string(list.size()) + (list.size() == 1 ? " task" : " tasks");
    for (const TaskInfo& t : list) {
        out += "\n#" + std::to_string(t.handle) + " ";
        switch (t.kind) {
            case TaskInfo::Kind::Delay:
                out += "delay " + Secs(t.period) + ", left " + Secs(t.remaining);
                break;
            case TaskInfo::Kind::Repeating:
                out += "repeat every " + Secs(t.period) + " (" +
                       (t.repeatsLeft < 0 ? std::string("forever") : std::to_string(t.repeatsLeft) + " left") +
                       "), next in " + Secs(t.remaining);
                break;
            case TaskInfo::Kind::Tween:
                out += "tween " + Secs(t.period) + ", left " + Secs(t.remaining);
                break;
        }
        if (!t.tag.empty()) out += " tag=" + t.tag;
        if (!t.clock.empty()) out += " [" + t.clock + "]";
        else if (t.unscaled) out += " [unscaled]";
        if (t.hasObject) out += t.ownerAlive ? " [object]" : " [object gone]";
        if (t.paused) out += " [paused]";
    }
    return out;
}

Scheduler& Scheduler::Global() {
    static Scheduler s;
    return s;
}

namespace {
// The real frame time for a scaled `dt`: undo the time scale; at scale 0 (paused)
// the only source left is Time's own record of the frame.
float UnscaledFrom(float dt) { return Time::UnscaledFor(dt); }
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

float ClockCursor::Take(const Clock& clock) {
    const std::uint64_t t = clock.Ticks();
    const double e = clock.Elapsed();
    if (!primed) {                   // the first look takes this frame's share, like a scene-time task would
        primed = true;
        ticks = t > 0 ? t - 1 : 0;
        elapsed = e - (double)clock.Delta();
    }
    if (t == ticks) { elapsed = e; return 0.0f; }
    if (t < ticks || e < elapsed) { ticks = t; elapsed = e; return 0.0f; }   // the clock was reset: resync
    const float d = t - ticks == 1 ? clock.Delta() : (float)(e - elapsed);   // one tick: its exact delta
    ticks = t;
    elapsed = e;
    return d > 0.0f && std::isfinite(d) ? d : 0.0f;
}

// How far a task moves this update. A channel task takes what its clock advanced
// since it last looked (also while paused, so a pause does not bank time).
float Scheduler::TaskStep(Task& t, float dt, float unscaledDt) {
    if (t.clock.empty()) return t.unscaled ? unscaledDt : dt;
    return t.cursor.Take((m_clocks ? *m_clocks : ClockSet::Global()).Get(t.clock));
}

void Scheduler::RunTask(std::size_t i, float dt, float unscaledDt) {
    if (HasLiveness(m_tasks[i]) && OwnerGone(m_tasks[i])) { m_tasks[i].handle = InvalidHandle; return; }
    const float step = TaskStep(m_tasks[i], dt, unscaledDt);
    if (m_tasks[i].paused) return;
    m_tasks[i].elapsed += (double)step;

    if (m_tasks[i].isTween) {
        const float p = Mathf::Clamp01((float)(m_tasks[i].elapsed / (double)m_tasks[i].period));
        auto update = std::move(m_tasks[i].tweenUpdate);
        if (update) update(p);
        if (m_tasks[i].handle == InvalidHandle) return;   // cancelled itself
        m_tasks[i].tweenUpdate = std::move(update);
        if (p >= 1.0f) {
            auto complete = std::move(m_tasks[i].tweenComplete);
            if (complete) complete();
            m_tasks[i].handle = InvalidHandle;
        }
        return;
    }

    // Fire as many times as elapsed allows (handles big dt spikes), up to kMaxCatchUp.
    int runs = 0;
    while (m_tasks[i].handle != InvalidHandle && !m_tasks[i].paused && m_tasks[i].elapsed >= (double)m_tasks[i].period) {
        if (runs >= kMaxCatchUp) {   // far behind: drop the backlog, keep the rhythm
            m_tasks[i].elapsed = std::fmod(m_tasks[i].elapsed, (double)m_tasks[i].period);
            break;
        }
        if (runs > 0 && HasLiveness(m_tasks[i]) && OwnerGone(m_tasks[i])) { m_tasks[i].handle = InvalidHandle; break; }
        ++runs;
        m_tasks[i].elapsed -= (double)m_tasks[i].period;
        auto action = std::move(m_tasks[i].action);
        if (action) action();
        Task& t = m_tasks[i];
        if (t.handle == InvalidHandle) break;                // cancelled itself
        t.action = std::move(action);
        if (t.repeatsLeft == 0) { t.handle = InvalidHandle; break; }     // one-shot done
        if (t.repeatsLeft > 0 && --t.repeatsLeft == 0) { t.handle = InvalidHandle; break; }
    }
}

void Scheduler::Update(float dt, float unscaledDt) {
    // A step that is not a positive finite number moves nothing (NaN would poison every
    // timer for good, a negative one would run them backwards).
    if (!(dt > 0.0f) || !std::isfinite(dt)) dt = 0.0f;
    if (!(unscaledDt > 0.0f) || !std::isfinite(unscaledDt)) unscaledDt = 0.0f;

    // Iterate by index and re-fetch the task after every callback: a callback
    // may Invoke (appending, possibly reallocating the list) or Cancel/Clear
    // (which only mark tasks dead while we're here). Each callback is moved out
    // of its task while it runs, so it stays alive whatever the list does.
    std::exception_ptr failure;
    ++m_updateDepth;
    for (std::size_t i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].handle == InvalidHandle) continue;
        try {
            RunTask(i, dt, unscaledDt);
        } catch (...) {
            // A throwing task is dropped (a repeating one would only throw again next
            // frame); the others still get their time and the first error is rethrown below.
            if (!failure) failure = std::current_exception();
            if (i < m_tasks.size()) m_tasks[i].handle = InvalidHandle;
        }
    }
    if (--m_updateDepth == 0) {   // a nested Update leaves the sweep to the outer one
        m_tasks.erase(std::remove_if(m_tasks.begin(), m_tasks.end(),
                                     [](const Task& t) { return t.handle == InvalidHandle; }),
                      m_tasks.end());
    }
    if (failure) std::rethrow_exception(failure);
}

} // namespace okay
