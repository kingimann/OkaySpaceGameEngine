#include "okay/Core/Scheduler.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/SaveFramework.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Scene/GameObject.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <utility>

namespace okay {

namespace {
// A delay: NaN and negative become 0 (due on the next update); infinity stays (never).
float SaneDelay(float v) { return v > 0.0f ? v : 0.0f; }
// An interval or duration: at least a tenth of a millisecond, so NaN and 0 cannot make a task that never ends.
float SaneInterval(float v) { return v > 0.0001f ? v : 0.0001f; }
}

template <class Map, class Key>
void Scheduler::JoinGroup(Map& map, const Key& key, Handle h) {
    Group& g = map[key];
    g.handles.push_back(h);
    ++g.live;
}

template <class Map, class Key>
void Scheduler::LeaveGroup(Map& map, const Key& key) {
    auto it = map.find(key);
    if (it == map.end()) return;
    Group& g = it->second;
    if (g.live > 0) --g.live;
    if (g.live == 0) { map.erase(it); return; }
    // A long-lived owner that keeps scheduling short timers must not collect dead handles for ever.
    if (g.handles.size() > 2 * g.live + 16)
        g.handles.erase(std::remove_if(g.handles.begin(), g.handles.end(),
                                       [&](Handle h) { return m_index.find(h) == m_index.end(); }),
                        g.handles.end());
}

template <class Map, class Key, class Fn>
int Scheduler::ForGroup(Map& map, const Key& key, Fn fn) {
    auto it = map.find(key);
    if (it == map.end()) return 0;
    const std::vector<Handle> snapshot = it->second.handles;   // fn may retire tasks and reshape the group
    int n = 0;
    for (Handle h : snapshot) {
        auto ix = m_index.find(h);
        if (ix == m_index.end()) continue;                      // finished (or retired by an earlier fn)
        fn(m_tasks[ix->second]);
        ++n;
    }
    return n;
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
    t.baseline = t.period;
    t.jitter = (options.jitter > 0.0f && std::isfinite(options.jitter)) ? (options.jitter < 0.99f ? options.jitter : 0.99f) : 0.0f;
    if (options.firstDelay >= 0.0f && std::isfinite(options.firstDelay) && !t.isTween && t.repeatsLeft != 0) {
        t.period = options.firstDelay;
        t.firstPending = true;
    } else if (t.jitter > 0.0f && std::isfinite(t.period)) {
        t.period = RollJitter(t.baseline, t.jitter);
    }
    if (options.object) {
        t.objectId = options.object->InstanceId();
        t.objectPtr = options.object;
    }
    const Handle h = t.handle;
    m_tasks.push_back(std::move(t));
    m_index.emplace(h, m_tasks.size() - 1);
    const Task& added = m_tasks.back();
    if (!added.tag.empty()) JoinGroup(m_byTag, added.tag, h);
    if (added.owner) JoinGroup(m_byOwner, added.owner, h);
    if (added.objectPtr) JoinGroup(m_byObject, added.objectPtr, h);
    return h;
}

void Scheduler::ReleasePayload(Task& t) {
    // Moved out first and destroyed last: what a callback captured may run code (a destructor
    // that cancels or schedules) once the task is already out of every table.
    auto action = std::move(t.action);
    auto update = std::move(t.tweenUpdate);
    auto complete = std::move(t.tweenComplete);
    auto alive = std::move(t.alive);
    t.action = nullptr; t.tweenUpdate = nullptr; t.tweenComplete = nullptr; t.alive = nullptr;
    std::string().swap(t.tag);
    std::string().swap(t.clock);
}

void Scheduler::Retire(std::size_t slot) {
    if (slot >= m_tasks.size()) return;
    Task& t = m_tasks[slot];
    if (t.handle == InvalidHandle) return;
    m_index.erase(t.handle);
    if (!t.tag.empty()) LeaveGroup(m_byTag, t.tag);
    if (t.owner) LeaveGroup(m_byOwner, t.owner);
    if (t.objectPtr) LeaveGroup(m_byObject, t.objectPtr);
    t.handle = InvalidHandle;
    ++m_dead;
    if (m_updateDepth > 0) m_retired.push_back(slot);   // its callbacks may be running right now: free them when the update ends
    else ReleasePayload(t);
}

void Scheduler::ReleaseRetired() {
    // By index: a destructor that runs here may schedule (the list grows) or cancel (this one grows).
    for (std::size_t k = 0; k < m_retired.size(); ++k) ReleasePayload(m_tasks[m_retired[k]]);
    m_retired.clear();
}

void Scheduler::Compact() {
    if (m_dead == 0) return;
    std::size_t w = 0;
    for (std::size_t r = 0; r < m_tasks.size(); ++r) {
        if (m_tasks[r].handle == InvalidHandle) continue;
        if (w != r) {
            m_tasks[w] = std::move(m_tasks[r]);
            m_index[m_tasks[w].handle] = w;
        }
        ++w;
    }
    m_tasks.erase(m_tasks.begin() + (std::ptrdiff_t)w, m_tasks.end());
    m_dead = 0;
}

void Scheduler::MaybeCompact() {
    if (m_updateDepth == 0 && m_dead > 32 && m_dead * 2 > m_tasks.size()) Compact();
}

Scheduler::~Scheduler() { Clear(); }

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

float Scheduler::RollJitter(float base, float jitter) {
    // splitmix64: a small, well-mixed stream that is the same on every platform.
    m_jitterState += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = m_jitterState;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z ^= z >> 31;
    const double r = (double)(z >> 11) / 9007199254740992.0;   // [0, 1)
    const double v = (double)base * (1.0 + (double)jitter * (2.0 * r - 1.0));
    return SaneInterval((float)v);
}

Scheduler::Handle Scheduler::NextFrame(std::function<void()> fn) { return NextFrame(std::move(fn), Options{}); }
Scheduler::Handle Scheduler::NextFrame(std::function<void()> fn, const Options& options) {
    const Handle h = Invoke(0.0f, std::move(fn), options);
    // From inside an update the task waits for the next update (a plain delay of 0 would run in this one).
    if (m_updateDepth > 0) if (Task* t = FindMut(h)) t->skipUntil = m_serial + 1;
    return h;
}

Scheduler::Handle Scheduler::At(double seconds, std::function<void()> fn) { return At(seconds, std::move(fn), Options{}); }
Scheduler::Handle Scheduler::At(double seconds, std::function<void()> fn, const Options& options) {
    double now;
    if (!options.clock.empty()) {
        const Clock* c = (m_clocks ? *m_clocks : ClockSet::Global()).Find(options.clock);
        now = c ? c->Elapsed() : Time::UnscaledTime();
    } else {
        now = options.unscaled ? Time::UnscaledTime() : Time::ElapsedTimeD();
    }
    const double wait = std::isfinite(seconds) ? seconds - now : 0.0;
    const float delay = wait > 0.0 ? (wait < (double)std::numeric_limits<float>::max() ? (float)wait : std::numeric_limits<float>::max()) : 0.0f;
    return Invoke(delay, std::move(fn), options);
}

void Scheduler::EndOfFrame(std::function<void()> fn) { if (fn) m_endOfFrame.push_back(std::move(fn)); }

void Scheduler::RegisterAction(const std::string& name, std::function<void()> fn) {
    if (name.empty()) return;
    if (fn) m_actions[name] = std::move(fn); else m_actions.erase(name);
}

Scheduler::Handle Scheduler::InvokeAction(float delay, const std::string& name) { return InvokeAction(delay, name, Options{}); }
Scheduler::Handle Scheduler::InvokeActionRepeating(float interval, const std::string& name, int repeatCount) {
    return InvokeActionRepeating(interval, name, repeatCount, Options{});
}

Scheduler::Handle Scheduler::InvokeAction(float delay, const std::string& name, const Options& options) {
    if (name.empty()) return InvalidHandle;
    Task t{};
    t.period = SaneDelay(delay);
    t.repeatsLeft = 0;
    t.isTween = false;
    t.actionName = name;
    t.action = [this, name] { auto it = m_actions.find(name); if (it != m_actions.end() && it->second) { auto fn = it->second; fn(); } };
    return Add(std::move(t), options);
}

Scheduler::Handle Scheduler::InvokeActionRepeating(float interval, const std::string& name, int repeatCount, const Options& options) {
    if (name.empty()) return InvalidHandle;
    Task t{};
    t.period = SaneInterval(interval);
    t.repeatsLeft = repeatCount;
    t.isTween = false;
    t.actionName = name;
    t.action = [this, name] { auto it = m_actions.find(name); if (it != m_actions.end() && it->second) { auto fn = it->second; fn(); } };
    return Add(std::move(t), options);
}

int Scheduler::SaveState(SaveWriter w) const {
    int n = 0;
    for (const Task& t : m_tasks) {
        if (t.handle == InvalidHandle || t.actionName.empty() || t.isTween) continue;
        SaveWriter tw = w.Scope("timers").Scope(std::to_string(n));
        tw.String("action", t.actionName);
        tw.Float("period", t.baseline);
        tw.Float("elapsed", (float)t.elapsed);
        tw.Float("current", t.period);
        tw.Int("repeats", t.repeatsLeft);
        tw.Float("jitter", t.jitter);
        tw.Bool("paused", t.paused);
        tw.Bool("unscaled", t.unscaled);
        if (!t.tag.empty()) tw.String("tag", t.tag);
        if (!t.clock.empty()) tw.String("clock", t.clock);
        ++n;
    }
    w.Int("timers.n", n);
    return n;
}

int Scheduler::LoadState(const SaveReader& r) {
    m_loadSkipped = 0;
    // Every named timer goes first: the saved set replaces them (plain code timers are not touched).
    std::vector<Handle> named;
    for (const Task& t : m_tasks) if (t.handle != InvalidHandle && !t.actionName.empty()) named.push_back(t.handle);
    for (Handle h : named) Cancel(h);
    int n = r.Int("timers.n", 0);
    if (n < 0) n = 0;
    if (n > 100000) n = 100000;
    int restored = 0;
    for (int i = 0; i < n; ++i) {
        const SaveReader tr = r.Scope("timers").Scope(std::to_string(i));
        const std::string name = tr.String("action", "");
        if (name.empty() || !HasAction(name)) { ++m_loadSkipped; continue; }
        Options o;
        o.tag = tr.String("tag", "");
        o.clock = tr.String("clock", "");
        o.unscaled = tr.Bool("unscaled", false);
        o.jitter = tr.Float("jitter", 0.0f);
        const int repeats = tr.Int("repeats", 0);
        const float period = tr.Float("period", 0.0f);
        const Handle h = repeats != 0 ? InvokeActionRepeating(period, name, repeats, o) : InvokeAction(period, name, o);
        if (h == InvalidHandle) { ++m_loadSkipped; continue; }
        if (Task* t = FindMut(h)) {
            const float cur = tr.Float("current", period);
            if (std::isfinite(cur) && cur > 0.0f) t->period = cur;
            const float el = tr.Float("elapsed", 0.0f);
            t->elapsed = (std::isfinite(el) && el > 0.0f) ? (double)el : 0.0;
            t->paused = tr.Bool("paused", false);
        }
        ++restored;
    }
    return restored;
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
    auto it = m_index.find(handle);
    if (it == m_index.end()) return false;
    Retire(it->second);
    MaybeCompact();
    return true;
}

int Scheduler::CancelByTag(const std::string& tag) {
    if (tag.empty()) return 0;
    const int n = ForGroup(m_byTag, tag, [&](Task& t) { Retire((std::size_t)(&t - m_tasks.data())); });
    MaybeCompact();
    return n;
}

int Scheduler::CancelByOwner(const void* owner) {
    if (!owner) return 0;
    const int n = ForGroup(m_byOwner, owner, [&](Task& t) { Retire((std::size_t)(&t - m_tasks.data())); });
    MaybeCompact();
    return n;
}

int Scheduler::CancelByObject(const GameObject* object) {
    if (!object) return 0;
    const int n = ForGroup(m_byObject, static_cast<const void*>(object),
                           [&](Task& t) { Retire((std::size_t)(&t - m_tasks.data())); });
    MaybeCompact();
    return n;
}

bool Scheduler::OwnerGone(const Task& t) {
    if (t.objectId != 0 && GameObject::FromInstanceId(t.objectId) == nullptr) return true;
    if (t.alive) {
        try { if (!t.alive()) return true; }
        catch (...) { return true; }
    }
    return false;
}

// The same for a task of the list that is about to run: the predicate runs from a local (moved
// out of its slot and back), so one that schedules (and moves the list) cannot pull itself away.
bool Scheduler::SlotOwnerGone(std::size_t slot) {
    Task& t = m_tasks[slot];
    if (t.objectId != 0 && GameObject::FromInstanceId(t.objectId) == nullptr) return true;
    if (!t.alive) return false;
    const Handle h = t.handle;
    std::function<bool()> alive = std::move(t.alive);
    bool gone = false;
    try { gone = !alive(); }
    catch (...) { gone = true; }
    if (slot < m_tasks.size() && m_tasks[slot].handle == h) m_tasks[slot].alive = std::move(alive);
    return gone;
}

int Scheduler::PurgeDeadOwners() {
    int n = 0;
    for (std::size_t i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].handle == InvalidHandle || !HasLiveness(m_tasks[i])) continue;
        if (!SlotOwnerGone(i)) continue;
        if (i < m_tasks.size() && m_tasks[i].handle != InvalidHandle) { Retire(i); ++n; }
    }
    MaybeCompact();
    return n;
}

int Scheduler::SetPausedByTag(const std::string& tag, bool paused) {
    if (tag.empty()) return 0;
    return ForGroup(m_byTag, tag, [&](Task& t) { t.paused = paused; });
}

int Scheduler::SetPausedByOwner(const void* owner, bool paused) {
    if (!owner) return 0;
    return ForGroup(m_byOwner, owner, [&](Task& t) { t.paused = paused; });
}

const Scheduler::Task* Scheduler::Find(Handle handle) const {
    if (handle == InvalidHandle) return nullptr;
    auto it = m_index.find(handle);
    return it == m_index.end() ? nullptr : &m_tasks[it->second];
}

Scheduler::Task* Scheduler::FindMut(Handle handle) {
    return const_cast<Task*>(static_cast<const Scheduler*>(this)->Find(handle));
}

bool Scheduler::IsScheduled(Handle handle) const { return Find(handle) != nullptr; }

bool Scheduler::SetPaused(Handle handle, bool paused) {
    Task* t = FindMut(handle);
    if (!t) return false;
    t->paused = paused;
    return true;
}

bool Scheduler::IsPaused(Handle handle) const {
    const Task* t = Find(handle);
    return t && t->paused;
}

bool Scheduler::SetInterval(Handle handle, float seconds) {
    Task* t = FindMut(handle);
    if (!t) return false;
    t->period = t->isTween || t->repeatsLeft != 0 ? SaneInterval(seconds) : SaneDelay(seconds);
    t->baseline = t->period;
    t->firstPending = false;
    return true;
}

float Scheduler::Remaining(Handle handle) const {
    const Task* t = Find(handle);
    return t ? (float)std::max(0.0, (double)t->period - t->elapsed) : -1.0f;
}

void Scheduler::Clear() {
    if (m_updateDepth > 0) {   // a callback cleared everything: retire what is left, the update sweeps
        for (std::size_t i = 0; i < m_tasks.size(); ++i) Retire(i);
        return;
    }
    // Everything leaves the tables first and is destroyed last, so what a callback captured may
    // call back into the scheduler from its destructor.
    std::vector<Task> old;
    old.swap(m_tasks);
    m_index.clear(); m_byTag.clear(); m_byOwner.clear(); m_byObject.clear();
    m_retired.clear();
    m_dead = 0;
}

std::size_t Scheduler::ActiveCount() const { return m_tasks.size() - m_dead; }

std::size_t Scheduler::CountByTag(const std::string& tag) const {
    if (tag.empty()) return 0;
    auto it = m_byTag.find(tag);
    return it == m_byTag.end() ? 0 : it->second.live;
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
        if (t.objectId != 0)
            if (const GameObject* go = GameObject::FromInstanceId(t.objectId)) i.objectName = go->name;
        i.age = t.age;
        i.action = t.actionName;
        i.overdue = std::max(0.0, t.elapsed - (double)t.period);
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

float ClockCursor::Take(std::uint64_t t, double e, float delta) {
    if (!primed) {                   // the first look takes this frame's share, like a scene-time task would
        primed = true;
        ticks = t > 0 ? t - 1 : 0;
        elapsed = e - (double)delta;
    }
    if (t == ticks) { elapsed = e; return 0.0f; }
    if (t < ticks || e < elapsed) { ticks = t; elapsed = e; return 0.0f; }   // the clock was reset: resync
    const float d = t - ticks == 1 ? delta : (float)(e - elapsed);           // one tick: its exact delta
    ticks = t;
    elapsed = e;
    return d > 0.0f && std::isfinite(d) ? d : 0.0f;
}

// How far a task moves this update. A channel task takes what its clock advanced
// since it last looked (also while paused, so a pause does not bank time).
float Scheduler::TaskStep(Task& t, float dt, float unscaledDt) {
    if (t.clock.empty()) return t.unscaled ? unscaledDt : dt;
    const Clock& c = (m_clocks ? *m_clocks : ClockSet::Global()).Get(t.clock);
    return t.cursor.Take(c.Ticks(), c.Elapsed(), c.Delta());
}

void Scheduler::RunTask(std::size_t i, float dt, float unscaledDt) {
    // A callback (or the alive predicate) of this very task called Update: the nested update must not
    // step it again. Its callbacks are moved out of the slot while they run, so running it from here would
    // use up its period (and a repeat) without calling anything.
    if (m_tasks[i].running) return;
    if (m_tasks[i].skipUntil > m_serial) return;   // NextFrame from inside an update: not before the next one
    struct Busy {
        Scheduler& s; std::size_t slot; Handle handle;
        Busy(Scheduler& sch, std::size_t i) : s(sch), slot(i), handle(sch.m_tasks[i].handle) { s.m_tasks[slot].running = true; }
        ~Busy() { if (slot < s.m_tasks.size() && s.m_tasks[slot].handle == handle) s.m_tasks[slot].running = false; }
    } busy(*this, i);
    if (HasLiveness(m_tasks[i]) && SlotOwnerGone(i)) { Retire(i); return; }
    if (m_tasks[i].handle == InvalidHandle) return;   // the predicate cancelled its own task
    const float step = TaskStep(m_tasks[i], dt, unscaledDt);
    if (m_tasks[i].paused) return;
    m_tasks[i].elapsed += (double)step;
    m_tasks[i].age += (double)step;

    if (m_tasks[i].isTween) {
        const float p = Mathf::Clamp01((float)(m_tasks[i].elapsed / (double)m_tasks[i].period));
        auto update = std::move(m_tasks[i].tweenUpdate);
        if (update) update(p);
        if (m_tasks[i].handle == InvalidHandle) return;   // cancelled itself
        m_tasks[i].tweenUpdate = std::move(update);
        if (p >= 1.0f) {
            auto complete = std::move(m_tasks[i].tweenComplete);
            if (complete) complete();
            Retire(i);
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
        if (runs > 0 && HasLiveness(m_tasks[i]) && SlotOwnerGone(i)) { Retire(i); break; }
        if (m_tasks[i].handle == InvalidHandle) break;
        ++runs;
        m_tasks[i].elapsed -= (double)m_tasks[i].period;
        auto action = std::move(m_tasks[i].action);
        if (action) action();
        Task& t = m_tasks[i];
        if (t.handle == InvalidHandle) break;                // cancelled itself
        t.action = std::move(action);
        if (t.repeatsLeft == 0) { Retire(i); break; }     // one-shot done
        if (t.repeatsLeft > 0 && --t.repeatsLeft == 0) { Retire(i); break; }
        if (t.firstPending) { t.firstPending = false; t.period = t.baseline; }   // the wait before the first run is over
        if (t.jitter > 0.0f) t.period = RollJitter(t.baseline, t.jitter);   // the next wait is its own random length
    }
}

void Scheduler::Update(float dt, float unscaledDt) {
    // A step that is not a positive finite number moves nothing (NaN would poison every
    // timer for good, a negative one would run them backwards).
    if (!(dt > 0.0f) || !std::isfinite(dt)) dt = 0.0f;
    if (!(unscaledDt > 0.0f) || !std::isfinite(unscaledDt)) unscaledDt = 0.0f;

    // A host that does not run GameLoop (the player, the editor's Play mode) never ticked the
    // process-wide clocks: do it, once per Time frame, when something here follows a channel.
    // A frame GameLoop or the host already ticked is skipped.
    if (!m_clocks)
        for (const Task& t : m_tasks)
            if (!t.clock.empty() && t.handle != InvalidHandle) { ClockSet::Global().TickOncePerFrame(); break; }

    // Iterate by index and re-fetch the task after every callback: a callback
    // may Invoke (appending, possibly reallocating the list) or Cancel/Clear
    // (which only mark tasks dead while we're here). Each callback is moved out
    // of its task while it runs, so it stays alive whatever the list does.
    std::exception_ptr failure;
    ++m_updateDepth;
    // Tasks a callback adds are stepped in this same update when they are due (documented, and tests rely on it).
    // To keep a callback that re-arms itself with no delay from holding the update for ever, an update steps
    // at most kMaxSpawnedPerUpdate tasks that were added while it ran; the rest wait for the next update.
    if (m_updateDepth == 1) ++m_serial;
    const std::size_t limit = m_tasks.size() + (std::size_t)kMaxSpawnedPerUpdate;
    for (std::size_t i = 0; i < m_tasks.size() && i < limit; ++i) {
        if (m_tasks[i].handle == InvalidHandle) continue;
        try {
            RunTask(i, dt, unscaledDt);
        } catch (...) {
            // A throwing task is dropped (a repeating one would only throw again next
            // frame); the others still get their time and the first error is rethrown below.
            if (!failure) failure = std::current_exception();
            Retire(i);
        }
    }
    // Free what the tasks that finished or were cancelled captured, still inside the update so
    // a destructor that calls back in sees the same rules as a callback does.
    if (m_updateDepth == 1) {
        // End of this update: what EndOfFrame() queued, in the order it was queued (one that queues another
        // runs it in the next update, so a loop of them cannot hold this one up).
        std::vector<std::function<void()>> eof;
        eof.swap(m_endOfFrame);
        for (auto& fn : eof) {
            try { if (fn) fn(); } catch (...) { if (!failure) failure = std::current_exception(); }
        }
        ReleaseRetired();
    }
    --m_updateDepth;
    if (m_updateDepth == 0 && m_dead > 32 && m_dead * 4 > m_tasks.size()) Compact();   // a nested Update leaves it to the outer one
    if (failure) std::rethrow_exception(failure);
}

} // namespace okay
