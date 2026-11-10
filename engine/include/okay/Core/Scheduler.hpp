#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include "okay/Core/ClockCursor.hpp"

namespace okay {

class ClockSet;
class GameObject;

/// Time-based callbacks: one-shot delays, repeating timers, and value tweens.
/// A Scene owns one and ticks it every frame, so gameplay code can schedule
/// work without writing its own timers. Those timers die with the scene (a
/// load or Clear drops them); `Scheduler::Global()` is the one that survives.
///
/// Not thread safe: schedule, cancel and update from the thread that runs the
/// scene. Callbacks may schedule and cancel freely (see docs/timers_and_tweens.md).
/// A callback may even call Update on this scheduler: the nested update advances every other
/// task, but the task whose callback is running is not run (or stepped) again by it.
///
/// Cost: every lookup by handle (Cancel, SetPaused, IsPaused, SetInterval, Remaining,
/// IsScheduled) is O(1); CancelByTag / CancelByOwner / CancelByObject / SetPausedByTag /
/// SetPausedByOwner touch only the tasks of that group (CountByTag and ActiveCount are O(1));
/// Update walks the live tasks once. A hundred thousand timers can be cancelled one by one.
class Scheduler {
public:
    ~Scheduler();   // drops every task first, so a captured value that calls back from its destructor finds a consistent scheduler
    using Handle = std::uint64_t;
    static constexpr Handle InvalidHandle = 0;
    /// A repeating timer that is far behind (a long hitch, a suspended machine) runs
    /// at most this many times in one Update; the rest of the backlog is dropped and
    /// the timer keeps its rhythm from there.
    static constexpr int kMaxCatchUp = 4096;

    /// Optional per-task settings.
    struct Options {
        /// Ignore the time scale and the game pause (a pause-menu timer keeps running).
        bool unscaled = false;
        /// A label for CancelByTag. Empty = untagged.
        std::string tag;
        /// Whoever scheduled it, for CancelByOwner (usually `this`). Never dereferenced.
        const void* owner = nullptr;
        /// A named Clock channel ("game", "ui", "cinematic", or your own) that drives
        /// this task: it advances by what that channel advanced, so the channel's own
        /// scale and pause apply. Empty = the scene's time (or real time with
        /// `unscaled`). Needs the ClockSet ticked each frame (GameLoop does). Wins over `unscaled`.
        std::string clock{};
        /// The object this task acts on. The task is dropped, silently, as soon as that
        /// object is destroyed or its scene unloads, so a callback that captured the
        /// object (or its Transform) can never run on freed memory. Also serves as the
        /// owner for CancelByOwner when `owner` is not set. A task for an object that is
        /// already destroyed is not scheduled at all (the handle is InvalidHandle).
        const GameObject* object = nullptr;
        /// Checked once per Update before the task advances (and between the runs of a
        /// catch-up burst); when it returns false the task is dropped silently, without
        /// running. For owners that are not GameObjects (hold a std::weak_ptr and return
        /// !expired()). Keep it cheap. A predicate that throws counts as false.
        std::function<bool()> alive{};
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
    /// Cancel every task with this tag / owner / object (safe from inside a callback,
    /// the running task included). Returns how many were cancelled.
    int CancelByTag(const std::string& tag);
    int CancelByOwner(const void* owner);
    int CancelByObject(const GameObject* object);
    /// Drop every task whose object was destroyed or whose `alive` predicate says it is
    /// gone, now (Update does this lazily, task by task). Returns how many were dropped.
    int PurgeDeadOwners();

    /// Freeze / resume one task: it keeps its progress while paused.
    bool SetPaused(Handle handle, bool paused);
    bool IsPaused(Handle handle) const;
    /// Freeze / resume every task with this tag / owner. Returns how many it touched.
    int SetPausedByTag(const std::string& tag, bool paused);
    int SetPausedByOwner(const void* owner, bool paused);
    /// Change a timer's interval (a delay's wait, a tween's duration). Progress
    /// so far is kept, so a shorter interval can fire on the next Update.
    bool SetInterval(Handle handle, float seconds);
    /// Seconds until the task fires next (a tween: until it ends). -1 if the
    /// handle is not (or no longer) scheduled.
    float Remaining(Handle handle) const;
    bool  IsScheduled(Handle handle) const;

    /// Advance all tasks by `dt` seconds. Unscaled tasks use the real frame time
    /// (derived from the time scale, as the scene's tweens do). A step that is NaN,
    /// negative or infinite counts as 0. If a callback throws, that task is dropped,
    /// the other tasks of the update still run, and the first exception is rethrown
    /// when the update is done (the scheduler is consistent by then).
    void Update(float dt);
    /// Same with an explicit real frame time for the unscaled tasks.
    void Update(float dt, float unscaledDt);

    void Clear();
    /// Tasks that have not finished or been cancelled, paused ones included (and
    /// including tasks of a dead object until the next Update or PurgeDeadOwners).
    std::size_t ActiveCount() const;
    std::size_t CountByTag(const std::string& tag) const;

    /// The clocks channel tasks read (default: ClockSet::Global()). A test passes its own
    /// set and ticks it by hand. The set must outlive the scheduler's tasks.
    void SetClockSet(ClockSet* clocks) { m_clocks = clocks; }

    /// What is scheduled, for a debug overlay or a test.
    struct TaskInfo {
        enum class Kind { Delay, Repeating, Tween };
        Handle handle = InvalidHandle;
        Kind kind = Kind::Delay;
        std::string tag;
        std::string clock;
        const void* owner = nullptr;
        float period = 0.0f;        ///< the delay / interval / duration
        float remaining = 0.0f;     ///< until it fires next (a tween: until it ends)
        int repeatsLeft = 0;        ///< Repeating: runs left, -1 = forever
        bool paused = false;
        bool unscaled = false;
        bool hasObject = false;     ///< bound to a GameObject (Options::object)
        bool ownerAlive = true;     ///< false: its object or `alive` says it is gone
        std::string objectName;     ///< that object's name while it is alive, else empty
        double age = 0.0;           ///< seconds the task has been running (paused time and a hold on its clock do not count)
        double overdue = 0.0;       ///< seconds past its due time without having run (paused, or its clock is not ticking); 0 = on time
    };
    /// Every active task, in the order it was scheduled.
    std::vector<TaskInfo> Tasks() const;
    /// A short multi-line text of Tasks(), one line per task, for a debug overlay:
    /// `Scheduler: 2 tasks` then `#3 repeat every 0.5s (forever), next in 0.2s tag=wave [ui] [paused]`.
    std::string Dump() const;

    /// The process-wide scheduler. Its tasks survive scene loads, Scene::Clear
    /// and Play restarts; Scene::Update ticks it once per frame. A callback that
    /// captures a scene object must say so with Options::object (the task then
    /// dies with the object); otherwise look objects up by name or id inside the
    /// callback, or tag the task and CancelByTag it when the scene goes away.
    static Scheduler& Global();
    /// Scene::Update calls this: ticks Global() once per frame however many
    /// scenes update (or update twice) in it. `caller` is the updating scene.
    static void TickGlobal(const void* caller, float dt);

private:
    struct Task {
        Handle handle;
        double elapsed;
        double age = 0.0;    // total seconds run (for the debug view)
        float  period;       // delay / interval / duration
        int    repeatsLeft;  // for repeating tasks (>=0), -1 = infinite
        bool   isTween;
        bool   unscaled = false;
        bool   paused = false;
        bool   running = false;       // one of its own callbacks is on the stack: a nested Update leaves it alone
        const void* owner = nullptr;
        std::string tag;
        std::string clock;            // channel name; empty = scene / unscaled time
        ClockCursor cursor;           // what this task has already taken from its channel
        std::uint64_t objectId = 0;   // GameObject::InstanceId() of Options::object
        const void* objectPtr = nullptr;   // identity only, never dereferenced
        std::function<bool()> alive;
        std::function<void()>      action;
        std::function<void(float)> tweenUpdate;
        std::function<void()>      tweenComplete;
    };

    // The tasks of one tag / owner / object: handles in scheduling order (it may still list
    // a few that finished; they are skipped and pruned) and how many are alive.
    struct Group {
        std::vector<Handle> handles;
        std::size_t live = 0;
    };

    Handle Add(Task&& t, const Options& options);
    const Task* Find(Handle handle) const;
    Task* FindMut(Handle handle);
    /// Take the task in `slot` out of play: it stops counting, its handle stops resolving (the
    /// slot keeps handle 0 until the list is compacted). Idempotent.
    void Retire(std::size_t slot);
    /// Free what a retired task captured (its callbacks), now or, inside an Update, when it ends.
    static void ReleasePayload(Task& t);
    void ReleaseRetired();
    void Compact();
    void MaybeCompact();
    template <class Map, class Key> void JoinGroup(Map& map, const Key& key, Handle h);
    template <class Map, class Key> void LeaveGroup(Map& map, const Key& key);
    /// Run `fn(Task&)` on every live task of a group (a snapshot: `fn` may retire tasks). Returns how many.
    template <class Map, class Key, class Fn> int ForGroup(Map& map, const Key& key, Fn fn);
    bool SlotOwnerGone(std::size_t slot);
    static bool OwnerGone(const Task& t);
    static bool HasLiveness(const Task& t) { return t.objectId != 0 || t.alive; }
    float TaskStep(Task& t, float dt, float unscaledDt);
    void RunTask(std::size_t i, float dt, float unscaledDt);

    // Tasks in scheduling order. A finished or cancelled task leaves a tombstone (handle 0)
    // that is swept out in bulk (Compact), so removing one is O(1) and slots never move while
    // an Update runs.
    std::vector<Task> m_tasks;
    std::unordered_map<Handle, std::size_t> m_index;               // live handle -> slot in m_tasks
    std::unordered_map<std::string, Group> m_byTag;
    std::unordered_map<const void*, Group> m_byOwner;
    std::unordered_map<const void*, Group> m_byObject;
    std::size_t m_dead = 0;                                        // tombstones in m_tasks
    std::vector<std::size_t> m_retired;                            // slots retired during the running Update
    Handle m_nextHandle = 1;
    ClockSet* m_clocks = nullptr;
    // >0 while Update runs: Cancel/Clear only retire tasks (handle = 0) and nothing is
    // compacted, so the list never shifts under a running callback.
    int m_updateDepth = 0;
};

} // namespace okay
