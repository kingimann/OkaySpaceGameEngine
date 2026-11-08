#pragma once
#include "okay/Core/Time.hpp"
#include <chrono>
#include <functional>
#include <vector>

namespace okay {

/// Start / stop / lap timer. By default it measures real (wall-clock) seconds; with
/// Stopwatch::GameTime() it follows the game clock (Time::ElapsedTime), so it stops
/// while the game is paused. A custom time source (seconds, increasing) can also be
/// given, which is how tests drive it.
///
///     Stopwatch sw;            // not running
///     sw.Start();
///     ...
///     float secs = sw.Elapsed();
///     float lap  = sw.Lap();   // time since the previous lap (or Start)
class Stopwatch {
public:
    using Source = std::function<double()>;

    Stopwatch() : m_source(RealSource) {}
    explicit Stopwatch(Source source) : m_source(source ? std::move(source) : Source(RealSource)) {}
    /// A stopwatch on the game clock: paused games do not advance it.
    static Stopwatch GameTime() { return Stopwatch([] { return (double)Time::ElapsedTime(); }); }
    /// A stopwatch on the real clock that is already running.
    static Stopwatch StartNew() { Stopwatch s; s.Start(); return s; }

    /// Begin (or resume) counting. Does nothing if already running.
    void Start() { if (m_running) return; m_startedAt = m_source(); m_running = true; }
    /// Stop counting; Elapsed keeps its value.
    void Stop() { if (!m_running) return; m_accum += m_source() - m_startedAt; m_running = false; }
    /// Zero everything and stop.
    void Reset() { m_accum = 0.0; m_running = false; m_lastLap = 0.0; m_laps.clear(); }
    /// Zero everything and start.
    void Restart() { Reset(); Start(); }

    bool  IsRunning() const { return m_running; }
    double ElapsedSeconds() const { return m_accum + (m_running ? m_source() - m_startedAt : 0.0); }
    float  Elapsed() const { return (float)ElapsedSeconds(); }
    double ElapsedMilliseconds() const { return ElapsedSeconds() * 1000.0; }

    /// Seconds since the previous Lap (or since the start); records it.
    float Lap() {
        const double now = ElapsedSeconds();
        const float lap = (float)(now - m_lastLap);
        m_lastLap = now;
        m_laps.push_back(lap);
        return lap;
    }
    const std::vector<float>& Laps() const { return m_laps; }

private:
    static double RealSource() {
        using clk = std::chrono::steady_clock;
        return std::chrono::duration<double>(clk::now().time_since_epoch()).count();
    }
    Source m_source;
    double m_startedAt = 0.0, m_accum = 0.0, m_lastLap = 0.0;
    bool   m_running = false;
    std::vector<float> m_laps;
};

} // namespace okay
