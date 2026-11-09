#pragma once
// A slice of time for work that must not blow a frame: build a path field, stream a chunk of the map,
// load the next batch of a save, bake a navmesh tile. Do as much as fits in the budget this frame, carry on
// next frame, and never stall the game on one long step.
//
//     TimeBudget budget(0.002);                    // 2 ms a frame
//     void Update(float) {
//         budget.Begin();
//         while (!tiles.empty() && budget.Allow()) Bake(tiles.pop());     // at least one a frame, so slow tiles still finish
//     }
//
//     // or hand it the step: it runs until the work says "done" or the budget is spent
//     budget.Begin();
//     const int ran = budget.Run([&] { return gen.Step(); });             // Step() returns false when there is nothing left
//
// Allow() is true for the first `minItems` calls after Begin() whatever the time (a step that alone takes longer
// than the budget still gets done, one per frame, instead of never), and after that only while the budget is not
// spent. The clock is Time::RealtimeSinceStartup() (monotonic; a test replaces it with Time::SetClockSources) or a
// function you give, so a test is deterministic. A budget that is not a finite number >= 0 counts as 0 (only the
// guaranteed items run), and a clock that goes backwards or returns NaN counts as no time passing, never as time
// owed. Not thread safe: one budget per thread. Header only.
#include "okay/Core/Time.hpp"
#include <climits>
#include <cmath>
#include <functional>
#include <utility>

namespace okay {

class TimeBudget {
public:
    using Source = std::function<double()>;

    /// `seconds` of work per Begin(); `source` returns seconds on any monotonic clock (default
    /// Time::RealtimeSinceStartup).
    explicit TimeBudget(double seconds = 0.002, Source source = Source()) : m_source(std::move(source)) { SetSeconds(seconds); }

    double Seconds() const { return m_seconds; }
    void   SetSeconds(double seconds) { m_seconds = (seconds > 0.0 && std::isfinite(seconds)) ? seconds : 0.0; }

    /// Start a slice (once a frame, before the work).
    void Begin() { m_start = Now(); m_used = 0; }

    /// May one more item of work start? True for the first `minItems` calls after Begin() (>= 0), then while the
    /// time spent is under the budget. Each true answer counts as an item (Used()).
    bool Allow(int minItems = 1) {
        if (m_used < minItems || Elapsed() < m_seconds) { if (m_used < INT_MAX) ++m_used; return true; }
        return false;
    }
    /// Call `step()` while Allow() says so, until it returns false (nothing left to do). Returns how many steps ran
    /// (a step that returned false counts: it did run).
    template <class F> int Run(F&& step, int minItems = 1) {
        int n = 0;
        while (Allow(minItems)) {
            ++n;
            if (!step()) break;
        }
        return n;
    }

    /// Seconds since Begin() (0 for a clock that went backwards), and what is left of the budget (never negative).
    double Elapsed() const {
        const double d = Now() - m_start;
        return (d > 0.0 && std::isfinite(d)) ? d : 0.0;
    }
    double Remaining() const { const double r = m_seconds - Elapsed(); return r > 0.0 ? r : 0.0; }
    bool   Exceeded() const { return Elapsed() >= m_seconds; }
    /// Items Allow() has let through since Begin().
    int    Used() const { return m_used; }

private:
    double Now() const { return m_source ? m_source() : Time::RealtimeSinceStartup(); }
    Source m_source;
    double m_seconds = 0.0;
    double m_start = 0.0;
    int    m_used = 0;
};

}  // namespace okay
