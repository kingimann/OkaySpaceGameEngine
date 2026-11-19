#pragma once
// Opt-in memory tracking for the runtime allocators (FrameArena, PoolAllocator, StackAllocator).
// Off by default and free when off (one relaxed atomic read). Turn it on with the environment variable
// OKAY_MEMTRACK=1, the console command `mem on`, or MemTrack::Enable(true) before the allocators are
// tagged. Then every allocator that was given a tag (SetTag("scene.cams")) is listed with its live
// bytes, peak, number of allocations and fragmentation, and a leak report is printed at shutdown.
// Written from the problem statement only (no other project's code): a registry of stats callbacks
// behind a spin lock, plus a record of what each retired allocator still held. See docs/core_runtime_upgrade.md.
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace okay {

/// Counters shared by FrameArena, PoolAllocator and StackAllocator.
struct AllocStats {
    std::size_t used = 0;            // arena/stack: bytes handed out since the last Reset; pool: bytes of live objects
    std::size_t reserved = 0;        // bytes held from the system
    std::size_t limit = 0;           // 0 = unlimited
    std::size_t highWater = 0;       // the largest `used` ever seen
    std::size_t live = 0;            // arena: allocations since the last Reset; pool: live objects
    std::uint64_t allocations = 0;   // successful allocations, ever
    std::uint64_t failures = 0;      // refused: over the limit, a bad alignment, or out of memory
    std::uint64_t resets = 0;        // arena Reset() calls
};

class MemTrack {
public:
    enum class Kind { Arena, Pool, Stack };

    /// One tag's numbers (all allocators that share the tag, live and retired, added up).
    struct TagReport {
        std::string tag;
        Kind kind = Kind::Arena;
        int allocators = 0;              // live allocators with this tag
        std::size_t usedBytes = 0;       // now
        std::size_t reservedBytes = 0;   // now
        std::size_t peakBytes = 0;       // largest high water of any one allocator, live or retired
        std::uint64_t allocations = 0;
        std::uint64_t failures = 0;
        /// Share of the reserved memory that is not in use: 0 = full, 1 = all empty (0 when nothing is reserved).
        double fragmentation = 0.0;
        /// Pool objects (or stack bytes) still alive when their allocator was destroyed, summed.
        std::size_t leakedObjects = 0;
        std::size_t leakedBytes = 0;
    };

    using StatsFn = std::function<AllocStats()>;

    /// Held by an allocator. Ends the tracking when destroyed.
    class Registration {
    public:
        Registration() = default;
        Registration(const Registration&) = delete;
        Registration& operator=(const Registration&) = delete;
        Registration(Registration&& o) noexcept : m_id(o.m_id) { o.m_id = 0; }
        Registration& operator=(Registration&& o) noexcept {
            if (this != &o) { Finish(); m_id = o.m_id; o.m_id = 0; }
            return *this;
        }
        ~Registration() { Finish(); }
        bool Active() const { return m_id != 0; }
        /// Take the allocator's final numbers (call it while the allocator is still intact), note leaks, stop tracking.
        void Finish() { if (m_id) { MemTrack::Retire(m_id); m_id = 0; } }
    private:
        friend class MemTrack;
        std::uint64_t m_id = 0;
    };

    static bool Enabled() { return Flag().load(std::memory_order_relaxed); }
    /// Turn tracking on or off. Allocators tagged while it was off stay untracked. Turning it on also arranges
    /// the leak report at exit (ReportAtExit).
    static void Enable(bool on) { InitEnv(); SetFlag(on); if (on) ReportAtExit(true); }
    /// Read OKAY_MEMTRACK once (any value other than "", "0" turns tracking on). Called by Track.
    static void InitEnv() {
        static const bool once = [] {
            const char* v = std::getenv("OKAY_MEMTRACK");
            if (v && *v && !(v[0] == '0' && !v[1])) { SetFlag(true); ReportAtExit(true); }
            return true;
        }();
        (void)once;
    }

    /// Start tracking an allocator. Returns an inactive registration when tracking is off or the tag is empty.
    static Registration Track(const char* tag, Kind kind, StatsFn fn) {
        InitEnv();
        Registration r;
        if (!Enabled() || !tag || !*tag || !fn) return r;
        Lock();
        Entry e; e.id = ++State().nextId; e.tag = tag; e.kind = kind; e.fn = std::move(fn);
        r.m_id = e.id;
        State().live.push_back(std::move(e));
        Unlock();
        return r;
    }

    /// Every tag seen so far, sorted by tag.
    static std::vector<TagReport> Snapshot() {
        std::vector<TagReport> out;
        Lock();
        for (const Record& rec : State().retired) {
            TagReport& t = Slot(out, rec.tag, rec.kind);
            t.peakBytes = rec.peak > t.peakBytes ? rec.peak : t.peakBytes;
            t.allocations += rec.allocations; t.failures += rec.failures;
            t.leakedObjects += rec.leakObjects; t.leakedBytes += rec.leakBytes;
        }
        for (const Entry& e : State().live) {
            const AllocStats s = e.fn();
            TagReport& t = Slot(out, e.tag, e.kind);
            ++t.allocators;
            t.usedBytes += s.used; t.reservedBytes += s.reserved;
            t.peakBytes = s.highWater > t.peakBytes ? s.highWater : t.peakBytes;
            t.allocations += s.allocations; t.failures += s.failures;
        }
        Unlock();
        for (TagReport& t : out)
            t.fragmentation = t.reservedBytes ? 1.0 - (double)(t.usedBytes < t.reservedBytes ? t.usedBytes : t.reservedBytes) / (double)t.reservedBytes : 0.0;
        std::sort(out.begin(), out.end(), [](const TagReport& a, const TagReport& b) { return a.tag < b.tag; });
        return out;
    }

    /// A text table of Snapshot().
    static std::string Report() {
        if (!Enabled() && Snapshot().empty()) return "memory tracking is off (set OKAY_MEMTRACK=1 or run `mem on`)\n";
        std::string s = "tag                        kind   allocs   used      reserved  peak      frag   allocs-total\n";
        char b[256];
        for (const TagReport& t : Snapshot()) {
            std::snprintf(b, sizeof b, "%-26s %-6s %6d %9zu %9zu %9zu %5.1f%% %llu\n", t.tag.c_str(), KindName(t.kind), t.allocators,
                          t.usedBytes, t.reservedBytes, t.peakBytes, t.fragmentation * 100.0, (unsigned long long)t.allocations);
            s += b;
        }
        return s;
    }

    /// What is still alive that should not be: pool objects and stack bytes held by allocators that were destroyed
    /// with them, and by pool / stack allocators that still exist. Arenas are never leaks (a Reset frees them). Empty text = no leaks.
    static std::string LeakReport() {
        std::string s;
        char b[256];
        for (const TagReport& t : Snapshot()) {
            std::size_t objs = t.leakedObjects, bytes = t.leakedBytes;
            if (t.kind != Kind::Arena) bytes += t.usedBytes;
            if (objs == 0 && bytes == 0) continue;
            std::snprintf(b, sizeof b, "leak: %s (%s): %zu object(s) from destroyed allocators, %zu byte(s) in use\n", t.tag.c_str(), KindName(t.kind), objs, bytes);
            s += b;
        }
        return s;
    }

    /// Print the leak report to stderr when the process exits (once registered; default on whenever tracking is enabled).
    static void ReportAtExit(bool on) {
        State().atExit.store(on, std::memory_order_relaxed);
        static const bool reg = [] { std::atexit([] {
            if (!State().atExit.load(std::memory_order_relaxed)) return;
            const std::string r = LeakReport();
            if (!r.empty()) std::fputs(("memory tracking: leaks at shutdown\n" + r).c_str(), stderr);
            else if (Enabled()) std::fputs("memory tracking: no leaks at shutdown\n", stderr);
        }); return true; }();
        (void)reg;
    }

    /// Forget everything (tests).
    static void Reset() { Lock(); State().retired.clear(); State().live.clear(); Unlock(); }

private:
    struct Entry { std::uint64_t id = 0; std::string tag; Kind kind = Kind::Arena; StatsFn fn; };
    struct Record { std::string tag; Kind kind = Kind::Arena; std::size_t peak = 0, leakObjects = 0, leakBytes = 0; std::uint64_t allocations = 0, failures = 0; };
    struct Data {
        std::atomic<bool> lock{false}, atExit{false};
        std::uint64_t nextId = 0;
        std::vector<Entry> live;
        std::vector<Record> retired;
    };
    static Data& State() { static Data* d = new Data; return *d; }   // never destroyed: allocators may outlive static destruction
    static std::atomic<bool>& Flag() { static std::atomic<bool> f{false}; return f; }
    static void SetFlag(bool on) { Flag().store(on, std::memory_order_relaxed); }
    static void Lock() { while (State().lock.exchange(true, std::memory_order_acquire)) {} }
    static void Unlock() { State().lock.store(false, std::memory_order_release); }
    static const char* KindName(Kind k) { return k == Kind::Arena ? "arena" : (k == Kind::Pool ? "pool" : "stack"); }
    static TagReport& Slot(std::vector<TagReport>& v, const std::string& tag, Kind k) {
        for (TagReport& t : v) if (t.tag == tag) return t;
        v.emplace_back(); v.back().tag = tag; v.back().kind = k;
        return v.back();
    }
    static void Retire(std::uint64_t id) {
        Lock();
        auto& live = State().live;
        for (std::size_t i = 0; i < live.size(); ++i) {
            if (live[i].id != id) continue;
            const AllocStats s = live[i].fn();
            Record r; r.tag = live[i].tag; r.kind = live[i].kind; r.peak = s.highWater; r.allocations = s.allocations; r.failures = s.failures;
            if (r.kind == Kind::Pool) { r.leakObjects = s.live; r.leakBytes = s.used; }
            else if (r.kind == Kind::Stack) { r.leakBytes = s.used; }
            State().retired.push_back(std::move(r));
            live.erase(live.begin() + (std::ptrdiff_t)i);
            break;
        }
        Unlock();
    }
};

} // namespace okay
