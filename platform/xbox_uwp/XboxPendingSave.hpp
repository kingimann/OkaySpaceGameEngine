#pragma once
// "Save next frame" requests that must not be lost when the app is suspended (pure, no Windows headers; tested on Linux by
// tests/test_xbox_uwp_pieces.cpp). The game asks for a save and the engine writes it at the start of the next frame; if the
// Suspending event arrives in between, the shell calls FlushForSuspend so the write happens before the deferral is completed.
//
// What it decides:
//   * Request(name, write): marks a write as pending; asking again for the same name replaces the earlier writer (one write, newest data).
//   * RunFrame(): the normal path, runs every pending write once (in the order requested). A failed write stays pending and is
//     reported; it is retried on the next frame.
//   * FlushForSuspend(nowMs, budgetMs): the Suspending path. Runs pending writes in order while the budget lasts. It does not START a write
//     whose last measured time is longer than what is left (a write that would overrun the deadline could be killed half way and corrupt
//     the file), and says so as "would miss the deadline". Skipped and failed writes stay pending and are listed honestly in the report.
// The clock and the storage are injected (the writer is a std::function), so the tests use a fake clock and a fake disk.
// Written from Microsoft's documentation of CoreApplication.Suspending and SuspendingOperation (about 5 seconds). Our own code.
// UNVERIFIED on Windows and on a console.
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace okay::xbox {

enum class PendingOutcome { Saved, Failed, WouldMissDeadline, OutOfTime };

struct PendingResult {
    std::string name;
    PendingOutcome outcome = PendingOutcome::Saved;
    std::string error;
    std::uint64_t ms = 0;
};

struct PendingReport {
    std::vector<PendingResult> results;
    int Count(PendingOutcome o) const { int n = 0; for (const auto& r : results) if (r.outcome == o) ++n; return n; }
    bool AllSaved() const { return Count(PendingOutcome::Saved) == (int)results.size(); }
    /// "settings saved (3 ms), slot1 FAILED: disk full, slot2 would miss the deadline (last write 900 ms, 400 left)".
    std::string Summary() const {
        if (results.empty()) return "nothing pending";
        std::string out;
        for (const auto& r : results) {
            if (!out.empty()) out += ", ";
            switch (r.outcome) {
                case PendingOutcome::Saved: out += r.name + " saved (" + std::to_string(r.ms) + " ms)"; break;
                case PendingOutcome::Failed: out += r.name + " FAILED: " + (r.error.empty() ? std::string("no reason given") : r.error); break;
                case PendingOutcome::WouldMissDeadline: out += r.name + " would miss the deadline (" + r.error + ")"; break;
                case PendingOutcome::OutOfTime: out += r.name + " skipped (out of time)"; break;
            }
        }
        return out;
    }
};

class PendingSaves {
public:
    using Writer = std::function<bool(std::string& error)>;

    /// Ask for a save. The same name again replaces the earlier writer and keeps its place in the order.
    void Request(const std::string& name, Writer write) {
        for (auto& p : pending_) if (p.name == name) { p.write = std::move(write); return; }
        pending_.push_back({name, std::move(write)});
    }
    bool HasPending() const { return !pending_.empty(); }
    std::size_t PendingCount() const { return pending_.size(); }
    bool IsPending(const std::string& name) const { for (const auto& p : pending_) if (p.name == name) return true; return false; }
    /// How long the last successful write of `name` took (0 when it never ran). Used to refuse a write that cannot finish in time.
    std::uint64_t LastWriteMs(const std::string& name) const { auto it = lastMs_.find(name); return it == lastMs_.end() ? 0 : it->second; }

    /// Normal frame: run everything pending. No timing is needed (`nowMs` may be null, then durations are 0).
    PendingReport RunFrame(const std::function<std::uint64_t()>& nowMs = nullptr) {
        return Run(nowMs, ~0ull, false);
    }

    /// Suspending: run pending writes inside `budgetMs` (see FlushBudgetMs in XboxSuspend.hpp).
    PendingReport FlushForSuspend(const std::function<std::uint64_t()>& nowMs, std::uint64_t budgetMs) {
        return Run(nowMs, budgetMs, true);
    }

private:
    struct Item { std::string name; Writer write; };

    PendingReport Run(const std::function<std::uint64_t()>& nowMs, std::uint64_t budgetMs, bool guard) {
        PendingReport rep;
        auto now = [&]() -> std::uint64_t { return nowMs ? nowMs() : 0; };
        const std::uint64_t t0 = now();
        std::vector<Item> list;
        list.swap(pending_);          // a writer that requests another save during the run adds to the fresh queue (next frame)
        std::vector<Item> keep;
        bool timeUp = false;
        for (auto& it : list) {
            PendingResult r; r.name = it.name;
            const std::uint64_t used = now() - t0;
            const std::uint64_t left = used >= budgetMs ? 0 : budgetMs - used;
            if (guard && (timeUp || left == 0)) {
                timeUp = true; r.outcome = PendingOutcome::OutOfTime; rep.results.push_back(r); keep.push_back(std::move(it)); continue;
            }
            const std::uint64_t last = LastWriteMs(it.name);
            if (guard && last > left) {
                r.outcome = PendingOutcome::WouldMissDeadline;
                r.error = "last write " + std::to_string(last) + " ms, " + std::to_string(left) + " left";
                rep.results.push_back(r); keep.push_back(std::move(it)); continue;
            }
            const std::uint64_t start = now();
            bool ok = false;
            try { ok = it.write ? it.write(r.error) : true; }
            catch (const std::exception& e) { ok = false; r.error = std::string("threw: ") + e.what(); }
            catch (...) { ok = false; r.error = "threw an unknown exception"; }
            r.ms = now() - start;
            if (ok) { r.outcome = PendingOutcome::Saved; r.error.clear(); lastMs_[it.name] = r.ms; }
            else { r.outcome = PendingOutcome::Failed; keep.push_back(std::move(it)); }
            rep.results.push_back(r);
        }
        // Kept items go in front of anything requested while the writers ran (older requests first); a newer writer for the same name wins.
        for (auto k = keep.rbegin(); k != keep.rend(); ++k) {
            bool replaced = false;
            for (const auto& p : pending_) if (p.name == k->name) { replaced = true; break; }
            if (!replaced) pending_.insert(pending_.begin(), std::move(*k));
        }
        return rep;
    }

    std::vector<Item> pending_;
    std::map<std::string, std::uint64_t> lastMs_;
};

} // namespace okay::xbox
