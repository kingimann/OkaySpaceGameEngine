#pragma once
// Frame capture for the Profiler: every OKAY_PROFILE scope (on any thread, nested), per-frame counters and
// frame boundaries are recorded into a ring buffer per thread while a capture runs, then written as Chrome
// "Trace Event Format" JSON (the format chrome://tracing, edge://tracing and Perfetto open: {"traceEvents":[...]},
// complete events ph "X" with ts/dur in microseconds, counter events ph "C", thread-name metadata ph "M").
// Written from that format's public description only. Off by default: when no capture runs, a scope costs one
// relaxed atomic read. No <mutex>/<thread> (spin locks and atomics only), so it is safe in every Windows program.
// Start one from the editor's Profiler window, the console command `trace`, or the environment variables
// OKAY_TRACE=<file> [OKAY_TRACE_FRAMES=<n>] (see docs/core_runtime_upgrade.md).
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>
#include "okay/Core/Utf8.hpp"

namespace okay {

class TraceCapture {
public:
    static constexpr std::size_t kRingEvents = 1u << 15;   // events kept per thread (32 bytes each, 1 MB); the oldest are overwritten
    static constexpr int kMaxThreads = 64;
    static constexpr int kMaxDepth = 64;

    /// Start recording. `frames` > 0 stops by itself after that many frames (Prof EndFrame calls) and, when `autoPath`
    /// is not empty, writes the file then. Restarting discards the previous capture.
    static void Start(int frames = 0, const std::string& autoPath = std::string()) {
        Stop();
        D().lock();
        for (int i = 0; i < kMaxThreads; ++i) if (Ring* r = D().rings[i].load()) r->head.store(0);
        D().epoch = Now();
        D().framesWanted = frames > 0 ? frames : 0;
        D().framesSeen = 0;
        D().autoPath = autoPath;
        D().lastWritten.clear();
        D().frameOpen = false;
        D().generation.fetch_add(1);
        D().unlock();
        D().active.store(true, std::memory_order_release);
    }
    /// Stop recording (scopes already open are dropped). The data stays until the next Start.
    static void Stop() {
        if (!D().active.exchange(false, std::memory_order_acq_rel)) return;
        while (D().inflight.load(std::memory_order_acquire) > 0) {}   // writers that saw "active" finish their event
    }
    static bool Active() { return D().active.load(std::memory_order_relaxed); }
    /// True once a capture has ended (stopped or reached its frame count) and has data.
    static bool HasData() { return !Active() && EventCount() > 0; }
    static int FramesSeen() { return D().framesSeen; }
    static int FramesWanted() { return D().framesWanted; }
    /// Path of the last file written by a frame-count stop or WriteToFile ("" if none).
    static std::string LastFile() { D().lock(); std::string s = D().lastWritten; D().unlock(); return s; }

    // ---- recording (called by Profiler; any thread) ----
    static void Begin(const char* name) {
        if (!Active()) return;
        Guard g;
        if (!g.ok) return;
        Ring* r = MyRing();
        if (!r) return;
        if (r->gen != D().generation.load(std::memory_order_relaxed)) { r->gen = D().generation.load(); r->depth = 0; }
        if (r->depth < kMaxDepth) { r->stack[r->depth].name = Intern(name ? name : "(null)"); r->stack[r->depth].t0 = Now(); }
        ++r->depth;
    }
    static void End() {
        if (!Active()) return;
        Guard g;
        if (!g.ok) return;
        Ring* r = MyRing();
        if (!r || r->gen != D().generation.load(std::memory_order_relaxed) || r->depth <= 0) return;   // opened before the capture began
        --r->depth;
        if (r->depth >= kMaxDepth) return;
        const std::uint64_t t1 = Now();
        Push(*r, r->stack[r->depth].t0, t1 - r->stack[r->depth].t0, r->stack[r->depth].name, 0, 0.0);
    }
    /// A named number sampled now (draw calls, entities, bytes ...). Shown as a graph in the trace viewer.
    static void Counter(const char* name, double value) {
        if (!Active()) return;
        Guard g;
        if (!g.ok) return;
        if (Ring* r = MyRing()) Push(*r, Now(), 0, Intern(name ? name : "(null)"), 1, value);
    }
    static void FrameBegin() {
        if (!Active()) return;
        D().frameStart = Now();
        D().frameOpen = true;
    }
    /// Closes the frame; stops (and writes, if a path was given) when the wanted number of frames has been seen.
    static void FrameEnd() {
        if (!Active()) return;
        if (D().frameOpen) {
            Guard g;
            if (g.ok) if (Ring* r = MyRing()) {
                const std::uint64_t t1 = Now();
                Push(*r, D().frameStart, t1 - D().frameStart, Intern("Frame"), 0, 0.0);
            }
            D().frameOpen = false;
        }
        ++D().framesSeen;
        if (D().framesWanted > 0 && D().framesSeen >= D().framesWanted) {
            Stop();
            std::string path;
            D().lock(); path = D().autoPath; D().unlock();
            if (!path.empty()) WriteToFile(path);
        }
    }
    /// Name the calling thread's track in the trace ("main", "worker 2" ...). Optional; unnamed threads are "thread N".
    static void NameThisThread(const char* name) {
        Ring* r = MyRing();
        if (r) { std::snprintf(r->name, sizeof r->name, "%s", name ? name : ""); }
    }

    // ---- results ----
    static std::size_t EventCount() {
        std::size_t n = 0;
        for (int i = 0; i < kMaxThreads; ++i) if (Ring* r = D().rings[i].load()) {
            const std::uint64_t h = r->head.load(); n += h < kRingEvents ? (std::size_t)h : kRingEvents;
        }
        return n;
    }
    /// Chrome trace JSON of what was captured. Returns the number of events (not counting thread-name metadata).
    /// Call after Stop (while recording it still works but may miss events being written).
    static std::size_t ExportJson(std::string& out) {
        out = "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[";
        bool first = true;
        std::size_t events = 0;
        char b[96];
        for (int i = 0; i < kMaxThreads; ++i) {
            Ring* r = D().rings[i].load();
            if (!r) continue;
            const std::uint64_t head = r->head.load(std::memory_order_acquire);
            if (head == 0) continue;
            if (!first) out += ',';
            first = false;
            out += "{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":1,\"tid\":" + std::to_string(i + 1) + ",\"args\":{\"name\":\"";
            Escape(out, r->name[0] ? std::string(r->name) : "thread " + std::to_string(i + 1));
            out += "\"}}";
            const std::uint64_t n = head < kRingEvents ? head : kRingEvents;
            for (std::uint64_t k = 0; k < n; ++k) {
                const Ev& e = r->ev[(head - n + k) % kRingEvents];
                if (e.t0 < D().epoch) continue;
                const double ts = (double)(e.t0 - D().epoch) / 1000.0;
                out += ",{\"name\":\""; Escape(out, e.name); out += "\",\"cat\":\"okay\",";
                if (e.kind == 0) {
                    std::snprintf(b, sizeof b, "\"ph\":\"X\",\"ts\":%.3f,\"dur\":%.3f,", ts, (double)e.dur / 1000.0); out += b;
                    out += "\"pid\":1,\"tid\":" + std::to_string(i + 1) + "}";
                } else {
                    std::snprintf(b, sizeof b, "\"ph\":\"C\",\"ts\":%.3f,\"pid\":1,\"tid\":%d,\"args\":{\"value\":%.6g}}", ts, i + 1, e.value); out += b;
                }
                ++events;
            }
        }
        out += "]}";
        return events;
    }
    static bool WriteToFile(const std::string& path) {
        std::string s;
        ExportJson(s);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(s.data(), (std::streamsize)s.size());
        if (!f) return false;
        D().lock(); D().lastWritten = path; D().unlock();
        return true;
    }
    /// A default file name: <dir>/okay-trace-<n>.json with an increasing n (no clock needed).
    static std::string DefaultPath(const std::string& dir) {
        static std::atomic<int> n{0};
        std::string d = dir;
        if (!d.empty() && d.back() != '/' && d.back() != '\\') d += '/';
        return d + "okay-trace-" + std::to_string(++n) + ".json";
    }

    /// OKAY_TRACE=<file> starts a capture at the first frame; OKAY_TRACE_FRAMES=<n> (default 300) says how many frames.
    /// Called by Profiler::BeginFrame; does its work once.
    static void InitFromEnvironment() {
        static const bool once = [] {
            const char* p = std::getenv("OKAY_TRACE");
            if (p && *p) {
                const char* f = std::getenv("OKAY_TRACE_FRAMES");
                int frames = f ? std::atoi(f) : 300;
                if (frames < 1) frames = 300;
                Start(frames, p);
            }
            return true;
        }();
        (void)once;
    }

private:
    struct Ev { std::uint64_t t0 = 0, dur = 0; const char* name = nullptr; std::uint32_t kind = 0; std::uint32_t pad = 0; double value = 0.0; };
    struct Open { const char* name; std::uint64_t t0; };
    struct Ring {
        std::atomic<std::uint64_t> head{0};
        std::vector<Ev> ev = std::vector<Ev>(kRingEvents);
        Open stack[kMaxDepth];
        int depth = 0;
        std::uint64_t gen = 0;
        char name[32] = {0};
    };
    struct Data {
        std::atomic<bool> active{false};
        std::atomic<int> inflight{0};
        std::atomic<std::uint64_t> generation{1};
        std::atomic<bool> spin{false};
        std::atomic<Ring*> rings[kMaxThreads];
        std::atomic<int> nextRing{0};
        std::uint64_t epoch = 0, frameStart = 0;
        bool frameOpen = false;
        int framesWanted = 0, framesSeen = 0;
        std::string autoPath, lastWritten;
        std::unordered_set<std::string> names;
        Data() { for (auto& r : rings) r.store(nullptr); }
        void lock() { while (spin.exchange(true, std::memory_order_acquire)) {} }
        void unlock() { spin.store(false, std::memory_order_release); }
    };
    struct Guard {   // counts a writer in flight, so Stop can wait for it
        bool ok;
        Guard() { D().inflight.fetch_add(1, std::memory_order_acq_rel); ok = D().active.load(std::memory_order_acquire); }
        ~Guard() { D().inflight.fetch_sub(1, std::memory_order_acq_rel); }
    };
    static Data& D() { static Data* d = new Data; return *d; }   // never destroyed: threads may still record at exit
    static std::uint64_t Now() {
        return (std::uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    static Ring* MyRing() {
        static thread_local Ring* mine = nullptr;
        if (mine) return mine;
        const int idx = D().nextRing.fetch_add(1);
        if (idx >= kMaxThreads) return nullptr;
        Ring* r = new Ring;
        std::snprintf(r->name, sizeof r->name, idx == 0 ? "main" : "thread %d", idx + 1);
        D().rings[idx].store(r);
        mine = r;
        return r;
    }
    static void Push(Ring& r, std::uint64_t t0, std::uint64_t dur, const char* name, std::uint32_t kind, double value) {
        const std::uint64_t h = r.head.load(std::memory_order_relaxed);
        Ev& e = r.ev[h % kRingEvents];
        e.t0 = t0; e.dur = dur; e.name = name; e.kind = kind; e.value = value;
        r.head.store(h + 1, std::memory_order_release);
    }
    // Stable copy of a name: scope names may be temporaries. A small per-thread cache keeps the common case lock free.
    static const char* Intern(const char* name) {
        struct Slot { const char* key = nullptr; const char* val = nullptr; };
        static thread_local Slot cache[64];
        Slot& c = cache[((std::uintptr_t)name >> 3) & 63];
        if (c.key == name && c.val && std::strcmp(c.val, name) == 0) return c.val;
        D().lock();
        const char* v = D().names.insert(name).first->c_str();
        D().unlock();
        c.key = name; c.val = v;
        return v;
    }
    static void Escape(std::string& out, const std::string& raw) {
        const std::string s = utf8::Sanitize(raw);
        for (unsigned char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; } else out += (char)c;
            }
        }
    }
};

} // namespace okay
