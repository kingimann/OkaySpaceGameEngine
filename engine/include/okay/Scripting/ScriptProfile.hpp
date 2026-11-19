#pragma once
// ---------------------------------------------------------------------------
// ScriptProfile - per-script and per-function timing for both OkayScript
// implementations. Pure model (no engine types): a table of
// {script, function} -> calls, total ms, max ms, plus a sort for the editor's
// Profiler table and a one-line summary for the player's F3 overlay.
// Cheap when off: every scope is one relaxed atomic load. Written from scratch.
// ---------------------------------------------------------------------------
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace okay::scriptprof {

struct Row {
    std::string script, function;
    bool entry = true;                 // an engine call into the script (Start/Update/event); false = a function inside it
    long long calls = 0;
    double totalMs = 0.0, maxMs = 0.0;
    double avgMs() const { return calls > 0 ? totalMs / (double)calls : 0.0; }
};

enum class SortKey { Script, Function, Calls, Total, Avg, Max };

class Table {
public:
    void Record(const std::string& script, const std::string& fn, double ms, bool entry = true) {
        if (!(ms >= 0.0)) ms = 0.0;
        Lock l(m_flag);
        for (auto& r : m_rows)
            if (r.function == fn && r.script == script && r.entry == entry) { Add(r, ms); return; }
        Row r; r.script = script; r.function = fn; r.entry = entry; Add(r, ms);
        m_rows.push_back(std::move(r));
    }
    void Reset() { Lock l(m_flag); m_rows.clear(); }
    std::vector<Row> Rows() const { Lock l(m_flag); return m_rows; }

    static void Sort(std::vector<Row>& rows, SortKey key, bool descending) {
        auto less = [key](const Row& a, const Row& b) {
            switch (key) {
                case SortKey::Script:   return a.script != b.script ? a.script < b.script : a.function < b.function;
                case SortKey::Function: return a.function != b.function ? a.function < b.function : a.script < b.script;
                case SortKey::Calls:    return a.calls < b.calls;
                case SortKey::Total:    return a.totalMs < b.totalMs;
                case SortKey::Avg:      return a.avgMs() < b.avgMs();
                case SortKey::Max:      return a.maxMs < b.maxMs;
            }
            return false;
        };
        std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
            return descending ? less(b, a) : less(a, b);
        });
    }
    /// Time per script (functions of one script added up), biggest first.
    std::vector<Row> PerScript() const {
        std::vector<Row> out;
        for (const auto& r : Rows()) {
            if (!r.entry) continue;   // nested functions are already inside their entry call
            Row* hit = nullptr;
            for (auto& o : out) if (o.script == r.script) { hit = &o; break; }
            if (!hit) { out.push_back(Row{}); hit = &out.back(); hit->script = r.script; hit->function = "*"; }
            hit->calls += r.calls; hit->totalMs += r.totalMs; hit->maxMs = std::max(hit->maxMs, r.maxMs);
        }
        Sort(out, SortKey::Total, true);
        return out;
    }
    /// "Scripts: Player 1.20ms, Enemy 0.40ms" (top `n` scripts) for the F3 overlay.
    std::string Summary(int n = 3) const {
        auto per = PerScript();
        if (per.empty()) return "Scripts: no timings";
        std::string s = "Scripts:";
        for (int i = 0; i < (int)per.size() && i < n; ++i) {
            char b[96]; std::snprintf(b, sizeof(b), "%s %s %.2fms", i ? "," : "", per[(std::size_t)i].script.c_str(), per[(std::size_t)i].totalMs);
            s += b;
        }
        return s;
    }

private:
    struct Lock {
        std::atomic_flag& f;
        explicit Lock(std::atomic_flag& fl) : f(fl) { while (f.test_and_set(std::memory_order_acquire)) {} }
        ~Lock() { f.clear(std::memory_order_release); }
    };
    static void Add(Row& r, double ms) { ++r.calls; r.totalMs += ms; if (ms > r.maxMs) r.maxMs = ms; }
    mutable std::atomic_flag m_flag = ATOMIC_FLAG_INIT;
    std::vector<Row> m_rows;
};

inline Table& Global() { static Table t; return t; }
inline std::atomic<bool>& EnabledFlag() { static std::atomic<bool> e{false}; return e; }
inline bool Enabled() { return EnabledFlag().load(std::memory_order_relaxed); }
inline void SetEnabled(bool on) { EnabledFlag().store(on, std::memory_order_relaxed); }
/// The script whose code is running now (set by ScriptScope; read by the VMs' function timers).
inline const std::string*& CurrentScript() { thread_local const std::string* s = nullptr; return s; }

/// Times one call into a script entry point (Start/Update/event). Outermost
/// scope also names the script for the VMs' per-function timers.
class ScriptScope {
public:
    ScriptScope(const std::string& script, const char* fn) {
        if (!Enabled()) return;
        m_on = true; m_script = &script; m_fn = fn; m_prev = CurrentScript(); CurrentScript() = &script;
        m_t = std::chrono::steady_clock::now();
    }
    ~ScriptScope() {
        if (!m_on) return;
        CurrentScript() = m_prev;
        Global().Record(*m_script, m_fn, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_t).count());
    }
    ScriptScope(const ScriptScope&) = delete;
    ScriptScope& operator=(const ScriptScope&) = delete;
private:
    bool m_on = false; const std::string* m_script = nullptr; const char* m_fn = nullptr; const std::string* m_prev = nullptr;
    std::chrono::steady_clock::time_point m_t;
};

/// Times one user function inside a script (cheap when off or outside a script).
class FnScope {
public:
    explicit FnScope(const std::string& fn) {
        if (!Enabled() || !CurrentScript()) return;
        m_on = true; m_fn = &fn; m_t = std::chrono::steady_clock::now();
    }
    ~FnScope() {
        if (!m_on) return;
        Global().Record(*CurrentScript(), *m_fn, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_t).count(), false);
    }
    FnScope(const FnScope&) = delete;
    FnScope& operator=(const FnScope&) = delete;
private:
    bool m_on = false; const std::string* m_fn = nullptr; std::chrono::steady_clock::time_point m_t;
};

} // namespace okay::scriptprof
