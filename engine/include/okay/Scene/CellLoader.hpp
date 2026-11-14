#pragma once
// Cell loaders: WHEN and in WHAT ORDER queued cells get loaded (docs/foundations/world_streaming.md).
//
// The streamer decides which cells are wanted and how urgent each is (a score, lower = sooner). A
// loader holds that queue and, once per frame in Pump(), calls the streamer's load function for the
// cells it allows. The loader never touches the scene itself, so the same streamer runs with
//   * SyncCellLoader     - everything queued is loaded at once, nearest first (tools, tests, a loading screen), or
//   * BudgetedCellLoader - as many cells as fit in a time slice per frame (at least one, so a slow cell
//                          still finishes), nearest first.
// Granularity is one cell: a cell is loaded whole or not at all. Neither loader uses threads.
#include "okay/Core/TimeBudget.hpp"
#include "okay/Scene/WorldGrid.hpp"
#include <cstddef>
#include <functional>
#include <map>

namespace okay::worldpart {

enum class LoadStatus {
    Done,     ///< the cell is loaded: remove it from the queue
    Retry,    ///< not now (objects still being destroyed): keep it queued, try again next Pump
    Failed,   ///< give up on it: remove it from the queue
};
using CellLoadFn = std::function<LoadStatus(const CellCoord&)>;

class ICellLoader {
public:
    virtual ~ICellLoader() = default;
    /// Queue a cell. Queueing one that is already queued only updates its score. Lower score = sooner;
    /// a NaN score counts as the largest.
    virtual void Enqueue(const CellCoord& cell, float score) = 0;
    /// Take a cell out of the queue. True if it was queued.
    virtual bool Cancel(const CellCoord& cell) = 0;
    virtual bool Queued(const CellCoord& cell) const = 0;
    virtual std::size_t Pending() const = 0;
    virtual void Clear() = 0;
    /// Run one frame of loading: call `load` for queued cells, lowest score first (ties by coordinate),
    /// each at most once per Pump. Returns how many times `load` was called. `load` may Cancel the cell it
    /// was given, but must not Enqueue.
    virtual int Pump(const CellLoadFn& load) = 0;
};

/// Shared queue. A subclass says how many cells one Pump may start.
class QueuedCellLoader : public ICellLoader {
public:
    void Enqueue(const CellCoord& cell, float score) override;
    bool Cancel(const CellCoord& cell) override { return m_queue.erase(cell) > 0; }
    bool Queued(const CellCoord& cell) const override { return m_queue.count(cell) != 0; }
    std::size_t Pending() const override { return m_queue.size(); }
    void Clear() override { m_queue.clear(); }
    int Pump(const CellLoadFn& load) override;
    /// The queue in loading order (for an overlay and tests).
    std::vector<CellCoord> Order() const;
protected:
    virtual void BeginPump() {}
    /// May another cell start? Called before each one.
    virtual bool Allow(int startedSoFar) = 0;
private:
    std::map<CellCoord, float> m_queue;
};

class SyncCellLoader : public QueuedCellLoader {
protected:
    bool Allow(int) override { return true; }
};

class BudgetedCellLoader : public QueuedCellLoader {
public:
    /// `seconds` of loading per Pump (not a finite number >= 0 reads as 0: one cell per Pump).
    /// `clock` returns seconds on a monotonic clock; empty = Time::RealtimeSinceStartup. `maxPerPump`
    /// caps the cells per Pump whatever the time (0 = no cap). At least one cell is started per Pump
    /// while any is queued.
    explicit BudgetedCellLoader(double seconds = 0.002, TimeBudget::Source clock = TimeBudget::Source(), int maxPerPump = 0)
        : m_budget(seconds, std::move(clock)), m_max(maxPerPump < 0 ? 0 : maxPerPump) {}
    void SetSeconds(double s) { m_budget.SetSeconds(s); }
    double Seconds() const { return m_budget.Seconds(); }
    void SetMaxPerPump(int n) { m_max = n < 0 ? 0 : n; }
protected:
    void BeginPump() override { m_budget.Begin(); }
    bool Allow(int started) override { return (m_max == 0 || started < m_max) && m_budget.Allow(1); }
private:
    TimeBudget m_budget;
    int m_max = 0;
};

} // namespace okay::worldpart
