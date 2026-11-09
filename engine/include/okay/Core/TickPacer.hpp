#pragma once
// Fixed-rate pacing for a headless loop such as the dedicated server's tick: "run one tick,
// then sleep until the next one is due, and if the machine fell far behind, forget the
// backlog instead of running ticks flat out".
//
//     TickPacer pacer(30.0);                       // 30 ticks a second, 4 ticks of lag tolerated
//     pacer.Start(NowNs());                        // the monotonic clock, in nanoseconds
//     while (running) {
//         Time::Step(1.0f / 30.0f);                // the game advances one tick of time per tick
//         scene.Update(Time::DeltaTime());
//         sleepNs = pacer.FinishTick(NowNs());     // how long to sleep before the next tick
//         if (sleepNs > 0) Sleep(sleepNs);
//     }
//
// This is the arithmetic DedicatedServer's loop has always used, pulled out as a class that
// takes the time as a number, so it can be tested with a made-up clock: no sleeping, no real
// time. The game itself advances by one tick of simulated time per tick, never by the real
// elapsed time, so a long stall (a disk hiccup, a suspended VM) costs the server some game
// time instead of one giant step; GameLoop's own accumulator is for a loop whose frames are
// measured and whose steps are owed, which a fixed-rate server is not.
//
// Rules, with `tick` = one tick in nanoseconds:
//   - the next tick is due `tick` after the previous one was due (not after it finished),
//     so the rate does not drift;
//   - a tick more than `maxLagTicks` late is not caught up: the schedule restarts from now
//     (Resyncs()). Up to that many late ticks run back to back, without a sleep;
//   - a tick is never scheduled more than `maxLagTicks` ticks ahead of the clock it was given:
//     if the clock jumps backwards (a wall clock that was set back, a bad test) the schedule
//     restarts one tick from now instead of sleeping for the size of the jump (BackJumps()).
//     The steady clock the server uses never does this, and a sleep that wakes a little early
//     is nowhere near the limit; the guard is for whoever feeds the pacer another source.
//
// Header only, plain C++17, no threads, no allocation.
#include <cstdint>

namespace okay {

class TickPacer {
public:
    explicit TickPacer(double hz = 30.0, int maxLagTicks = 4) {
        SetRate(hz);
        SetMaxLagTicks(maxLagTicks);
    }

    /// Ticks a second, kept within [1, 1000]. A value that is not a number is 30.
    void SetRate(double hz) {
        if (!(hz == hz)) hz = 30.0;
        m_hz = hz < 1.0 ? 1.0 : (hz > 1000.0 ? 1000.0 : hz);
        m_tickNs = TickNsFor(m_hz);
    }
    double Rate() const { return m_hz; }
    /// One tick in nanoseconds, truncated the way std::chrono does it (DedicatedServer's value).
    std::int64_t TickNs() const { return m_tickNs; }
    static std::int64_t TickNsFor(double hz) { return (std::int64_t)((1.0 / hz) * 1e9); }

    /// Ticks of lateness tolerated before the schedule restarts (at least 1).
    void SetMaxLagTicks(int n) { m_maxLag = n < 1 ? 1 : n; }
    int  MaxLagTicks() const { return m_maxLag; }

    /// The loop starts at `nowNs` (any monotonic nanosecond count): the first tick is due now.
    void Start(std::int64_t nowNs) {
        m_next = nowNs;
        m_ticks = 0; m_resyncs = 0; m_backJumps = 0;
        m_started = true;
    }
    bool Started() const { return m_started; }

    /// The tick that was running has finished at `nowNs`. Returns how long to sleep before the
    /// next one, in nanoseconds (0: it is due already, run it at once). Starts the pacer at
    /// `nowNs` if Start was never called.
    std::int64_t FinishTick(std::int64_t nowNs) {
        if (!m_started) Start(nowNs);
        ++m_ticks;
        m_next += m_tickNs;
        if (m_next < nowNs - m_tickNs * (std::int64_t)m_maxLag) {         // far behind: forget the backlog
            m_next = nowNs;
            ++m_resyncs;
        } else if (m_next > nowNs + m_tickNs * (std::int64_t)m_maxLag) {   // the clock went backwards
            m_next = nowNs + m_tickNs;
            ++m_backJumps;
        }
        const std::int64_t wait = m_next - nowNs;
        return wait > 0 ? wait : 0;
    }

    /// Ticks finished since Start, times the schedule was restarted for being too late, and
    /// times it was restarted because the clock went backwards.
    std::uint64_t Ticks() const { return m_ticks; }
    std::uint64_t Resyncs() const { return m_resyncs; }
    std::uint64_t BackJumps() const { return m_backJumps; }
    /// How far behind schedule the last FinishTick left the loop, in ticks (0 when on time).
    double LagTicks(std::int64_t nowNs) const {
        return nowNs > m_next && m_tickNs > 0 ? (double)(nowNs - m_next) / (double)m_tickNs : 0.0;
    }

private:
    double m_hz = 30.0;
    std::int64_t m_tickNs = 33333333;
    std::int64_t m_next = 0;
    int m_maxLag = 4;
    std::uint64_t m_ticks = 0, m_resyncs = 0, m_backJumps = 0;
    bool m_started = false;
};

}  // namespace okay
