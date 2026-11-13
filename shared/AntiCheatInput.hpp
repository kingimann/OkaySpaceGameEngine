#pragma once
// Anti-cheat: input analysis helpers (docs/anticheat.md, "Input analysis").
//
// Pure logic, standard library only, no clock, no I/O: a game server (or a
// replay reviewer) feeds it what a client's input looked like and gets back
// SIGNALS with a weight. Nothing in here decides anything: a human who is very
// steady (rhythm-game players, a wrist rest and a good mouse) can look regular,
// and a controller's aim assist legitimately turns the camera without mouse
// input. The caller tells the analyzers what is normal for its game through the
// parameters, and the result feeds a server-side score, never a ban by itself.
//
//   * AnalyzeIntervals    turbo / macro: inter-press timing too regular, or a
//                         recorded sequence of delays replayed (a periodic pattern);
//   * AnalyzeMouse        deltas no hand can make, and cursor moves that are
//                         straight lines at constant speed (software moving the mouse);
//   * AnalyzeInputVsCamera  the camera turned without the input that explains it
//                         (aim assist hacks, camera writes), or input that did nothing;
//   * InputAnalyzer       a per-player bounded buffer of all three, evaluated on demand.
//
// Time is in milliseconds. `quantumMs` is how coarse the timestamps are (the
// server tick: 16.7 for 60 Hz, 1 for raw OS timestamps): it widens what counts as
// "identical" so a macro isn't missed and a quantized human isn't flagged.
//
// All of this can be evaded (add random jitter to a macro, interpolate with noise,
// move the camera and fake the input). It catches off-the-shelf tools that don't
// bother, which is most of them, and costs the rest effort. tests/test_anticheat_input.cpp
// pins the behaviour on synthetic human-like and macro-like traces, including how
// often the human-like ones are flagged.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace okayac {
namespace input {

struct InputSignal {
    std::string kind;     // "turbo", "macro-pattern", "mouse-impossible", "mouse-robotic", "camera-unexplained", "input-ignored"
    std::string detail;
    int weight = 0;       // 1..5: how strongly this alone points at a tool (never a verdict)
    double score = 0;     // 0..1 within the kind, for ranking
};

namespace detail {
inline double Mean(const std::vector<double>& v) {
    if (v.empty()) return 0;
    double s = 0;
    for (double x : v) s += x;
    return s / (double)v.size();
}
inline double StdDev(const std::vector<double>& v, double mean) {
    if (v.size() < 2) return 0;
    double s = 0;
    for (double x : v) s += (x - mean) * (x - mean);
    return std::sqrt(s / (double)(v.size() - 1));
}
inline double WrapDeg(double d) {   // into (-180, 180]
    d = std::fmod(d, 360.0);
    if (d > 180.0) d -= 360.0;
    if (d <= -180.0) d += 360.0;
    return d;
}
inline std::string Num(double v, int digits = 1) {
    char b[48];
    std::snprintf(b, sizeof(b), "%.*f", digits, v);
    return b;
}
} // namespace detail

// ---- Timing: turbo and macros --------------------------------------------------------
struct IntervalParams {
    double quantumMs = 1.0;
    std::size_t minEvents = 24;        // intervals needed before judging (more when the quantum is coarse)
    double maxMeanMs = 3000;           // slower than this is just "a key pressed now and then"
    double cvLimit = 0.02;             // coefficient of variation below which timing is metronomic
    int maxPeriod = 12;                // longest repeating pattern looked for
    double patternMatch = 0.9;         // fraction of intervals that must repeat one period later
};
struct IntervalResult {
    std::size_t n = 0;
    double mean = 0, stddev = 0, cv = 0;
    double exactFraction = 0;          // adjacent intervals equal within the tolerance
    int period = 0;                    // best repeating period, 0 = none
    double periodMatch = 0;
    bool metronomic = false, patterned = false;
};

// `intervalsMs`: time between consecutive presses of one key / button.
inline IntervalResult AnalyzeIntervals(const std::vector<double>& intervalsMs, const IntervalParams& p = {}) {
    IntervalResult r;
    r.n = intervalsMs.size();
    if (r.n < 3) return r;
    r.mean = detail::Mean(intervalsMs);
    r.stddev = detail::StdDev(intervalsMs, r.mean);
    r.cv = r.mean > 0 ? r.stddev / r.mean : 0;
    const double tol = p.quantumMs + 0.5;
    std::size_t eq = 0;
    for (std::size_t i = 0; i + 1 < r.n; ++i)
        if (std::fabs(intervalsMs[i] - intervalsMs[i + 1]) <= tol) ++eq;
    r.exactFraction = (double)eq / (double)(r.n - 1);
    const std::size_t need = p.quantumMs > 8 ? std::max<std::size_t>(p.minEvents, 30) : p.minEvents;
    if (r.n < need || r.mean > p.maxMeanMs || r.mean <= 0) return r;
    // Steady to within a tick, or to within 2%: no hand does that for dozens of presses.
    r.metronomic = r.stddev <= 0.5 * p.quantumMs + 1.5 || r.cv < p.cvLimit || r.exactFraction >= 0.95;
    if (!r.metronomic) {   // a recorded sequence of delays replayed
        for (int per = 2; per <= p.maxPeriod; ++per) {
            if (r.n < (std::size_t)(4 * per + 8)) break;
            std::size_t match = 0, total = r.n - (std::size_t)per;
            for (std::size_t i = 0; i < total; ++i)
                if (std::fabs(intervalsMs[i] - intervalsMs[i + per]) <= tol) ++match;
            const double m = (double)match / (double)total;
            if (m > r.periodMatch) { r.periodMatch = m; r.period = per; }
        }
        r.patterned = r.period > 0 && r.periodMatch >= p.patternMatch;
        if (!r.patterned) r.period = 0;
    }
    return r;
}

inline std::vector<double> IntervalsOf(const std::vector<double>& pressTimesMs) {
    std::vector<double> v;
    for (std::size_t i = 1; i < pressTimesMs.size(); ++i) v.push_back(pressTimesMs[i] - pressTimesMs[i - 1]);
    return v;
}

inline std::vector<InputSignal> SignalsFromIntervals(const std::string& what, const IntervalResult& r) {
    std::vector<InputSignal> out;
    if (r.metronomic)
        out.push_back({"turbo", what + ": " + std::to_string(r.n + 1) + " presses at " + detail::Num(r.mean) + " ms apart with a spread of only " +
                                    detail::Num(r.stddev, 2) + " ms (cv " + detail::Num(r.cv, 3) + ")", 3, std::min(1.0, 0.5 + 0.5 * (1.0 - r.cv / 0.02))});
    if (r.patterned)
        out.push_back({"macro-pattern", what + ": the same " + std::to_string(r.period) + "-press delay pattern repeats (" + detail::Num(r.periodMatch * 100, 0) +
                                            "% of delays match one period later) over " + std::to_string(r.n + 1) + " presses", 4, r.periodMatch});
    return out;
}

// ---- Mouse -----------------------------------------------------------------------------
struct MouseSample {
    double tMs = 0;
    double dx = 0, dy = 0;   // counts moved since the previous sample
};
struct MouseParams {
    double dpi = 800;                // the mouse's counts per inch as the game assumes
    double maxSpeedMps = 6.0;        // faster than any hand: world-class flicks peak around 3 m/s
    double minDtMs = 0.5;            // a coalesced or duplicate-timestamp sample is treated as at least this long
    std::size_t minRun = 8;          // consecutive moving samples that make a "straight, steady" run
    double maxDeviationCounts = 1.0; // how far a robotic run may stray from a straight line
    double maxSpeedCv = 0.05;        // speed spread inside a robotic run
    double minRunCounts = 3.0;       // samples smaller than this are noise, not motion
};
struct MouseResult {
    std::size_t samples = 0, impossible = 0, roboticRuns = 0;
    double maxSpeedMps = 0;
};

inline MouseResult AnalyzeMouse(const std::vector<MouseSample>& s, const MouseParams& p = {}) {
    MouseResult r;
    r.samples = s.size();
    const double metersPerCount = 0.0254 / p.dpi;
    // impossible speed
    for (std::size_t i = 1; i < s.size(); ++i) {
        const double dt = std::max(s[i].tMs - s[i - 1].tMs, p.minDtMs);
        const double mps = std::hypot(s[i].dx, s[i].dy) * metersPerCount / (dt / 1000.0);
        r.maxSpeedMps = std::max(r.maxSpeedMps, mps);
        if (mps > p.maxSpeedMps) ++r.impossible;
    }
    // straight, steady runs: software moving the cursor in even steps
    std::size_t runStart = 0;
    bool inRun = false;
    auto closeRun = [&](std::size_t end) {   // [runStart, end)
        const std::size_t n = end - runStart;
        if (n < p.minRun) return;
        // The path: how far each sample strays from the straight line start -> end.
        double px = 0, py = 0;
        std::vector<double> X, Y;
        for (std::size_t i = runStart; i < end; ++i) { px += s[i].dx; py += s[i].dy; X.push_back(px); Y.push_back(py); }
        const double len = std::hypot(px, py);
        if (len < 80.0) return;
        double dev = 0;
        for (std::size_t i = 0; i < X.size(); ++i) dev = std::max(dev, std::fabs(X[i] * py - Y[i] * px) / len);
        // The speed, smoothed over 4 samples so integer rounding of the counts doesn't read as variation.
        std::vector<double> spd;
        for (std::size_t i = runStart; i + 4 <= end; ++i) {
            double d = 0, dt = 0;
            for (std::size_t k = i; k < i + 4; ++k) { d += std::hypot(s[k].dx, s[k].dy); dt += std::max(s[k].tMs - s[k - 1].tMs, p.minDtMs); }
            spd.push_back(d / dt);
        }
        const double m = detail::Mean(spd);
        const double cv = m > 0 ? detail::StdDev(spd, m) / m : 1;
        if (dev <= p.maxDeviationCounts && cv <= p.maxSpeedCv) ++r.roboticRuns;
    };
    for (std::size_t i = 1; i < s.size(); ++i) {
        const bool moving = std::hypot(s[i].dx, s[i].dy) >= p.minRunCounts;
        if (moving && !inRun) { inRun = true; runStart = i; }
        else if (!moving && inRun) { inRun = false; closeRun(i); }
    }
    if (inRun) closeRun(s.size());
    return r;
}

inline std::vector<InputSignal> SignalsFromMouse(const MouseResult& r) {
    std::vector<InputSignal> out;
    if (r.impossible >= 1)
        out.push_back({"mouse-impossible", std::to_string(r.impossible) + " mouse moves faster than a hand can (peak " + detail::Num(r.maxSpeedMps, 1) + " m/s)",
                       r.impossible >= 3 ? 5 : 3, std::min(1.0, (double)r.impossible / 10.0)});
    if (r.roboticRuns >= 3)
        out.push_back({"mouse-robotic", std::to_string(r.roboticRuns) + " cursor runs in a perfectly straight line at constant speed", 3,
                       std::min(1.0, (double)r.roboticRuns / 10.0)});
    return out;
}

// ---- Camera against input ----------------------------------------------------------------
struct CameraSample {
    double tMs = 0;
    double yawDeg = 0, pitchDeg = 0;
};
struct CameraParams {
    double degPerCount = 0.022;      // the game's yaw degrees per mouse count at the player's sensitivity (the caller knows)
    double yawSign = 1.0;            // +1 if positive dx turns the yaw up
    double pitchSign = -1.0;         // mouse up (negative dy) usually looks up
    double toleranceDeg = 0.75;      // slack per window
    double tolerancePct = 0.2;       // plus this fraction of the expected rotation (smoothing, lag by a frame)
    double minWindowMs = 250;        // camera samples are grouped into windows at least this long
    double activeDeg = 2.0;          // a window counts only if input or camera moved at least this much
    std::size_t minWindows = 12;
    double flagFraction = 0.2;       // this share of windows unexplained makes a signal
};
struct CameraResult {
    std::size_t windows = 0, active = 0, cameraWithoutInput = 0, inputWithoutCamera = 0;
    double unexplainedDeg = 0;       // total |residual| over active windows
    double fraction = 0;             // unexplained / active
};

// `mouse` and `camera` both sorted by time. A caller whose game legitimately moves the camera
// without the mouse (a controller, aim assist, a cutscene, recoil, being knocked back) keeps
// those spans out of the trace or raises the tolerances.
inline CameraResult AnalyzeInputVsCamera(const std::vector<MouseSample>& mouse, const std::vector<CameraSample>& cam, const CameraParams& p = {}) {
    CameraResult r;
    if (cam.size() < 2) return r;
    std::size_t mi = 0;
    while (mi < mouse.size() && mouse[mi].tMs <= cam.front().tMs) ++mi;
    std::size_t start = 0;
    for (std::size_t i = 1; i < cam.size(); ++i) {
        if (cam[i].tMs - cam[start].tMs < p.minWindowMs) continue;
        double ex = 0, ey = 0;
        while (mi < mouse.size() && mouse[mi].tMs <= cam[i].tMs) { ex += mouse[mi].dx * p.degPerCount * p.yawSign; ey += mouse[mi].dy * p.degPerCount * p.pitchSign; ++mi; }
        const double ax = detail::WrapDeg(cam[i].yawDeg - cam[start].yawDeg), ay = cam[i].pitchDeg - cam[start].pitchDeg;
        ++r.windows;
        const double expected = std::hypot(ex, ey), actual = std::hypot(ax, ay);
        if (std::max(expected, actual) >= p.activeDeg) {
            ++r.active;
            const double res = std::hypot(ax - ex, ay - ey);
            if (res > p.toleranceDeg + p.tolerancePct * expected) {
                r.unexplainedDeg += res;
                if (expected < p.activeDeg * 0.5 && actual >= p.activeDeg) ++r.cameraWithoutInput;
                else if (actual < p.activeDeg * 0.25 && expected >= p.activeDeg) ++r.inputWithoutCamera;
                else { ++r.cameraWithoutInput; }   // turned by an amount the input doesn't explain
            }
        }
        start = i;
    }
    const std::size_t bad = r.cameraWithoutInput + r.inputWithoutCamera;
    r.fraction = r.active ? (double)bad / (double)r.active : 0;
    return r;
}

inline std::vector<InputSignal> SignalsFromCamera(const CameraResult& r, const CameraParams& p = {}) {
    std::vector<InputSignal> out;
    if (r.active < p.minWindows) return out;
    if ((double)r.cameraWithoutInput / (double)r.active >= p.flagFraction)
        out.push_back({"camera-unexplained", std::to_string(r.cameraWithoutInput) + " of " + std::to_string(r.active) + " windows had the camera turn by more than the mouse input explains (" +
                                                 detail::Num(r.unexplainedDeg, 0) + " degrees in all)", 4, std::min(1.0, r.fraction)});
    if ((double)r.inputWithoutCamera / (double)r.active >= p.flagFraction)
        out.push_back({"input-ignored", std::to_string(r.inputWithoutCamera) + " of " + std::to_string(r.active) + " windows had mouse input that did not move the camera", 2,
                       std::min(1.0, r.fraction)});
    return out;
}

// ---- A per-player buffer ---------------------------------------------------------------
// Feed events as they arrive; Evaluate() whenever the server wants a verdict-free opinion
// (every few seconds, or when something else looks off). Everything is bounded.
class InputAnalyzer {
public:
    IntervalParams interval;
    MouseParams mouse;
    CameraParams camera;
    std::size_t maxEvents = 2048;

    // A press (key down or button down) of `code` at tMs. Releases aren't needed.
    void Press(int code, double tMs) { Push(presses_[code], tMs); }
    void Mouse(double tMs, double dx, double dy) {
        mouse_.push_back({tMs, dx, dy});
        while (mouse_.size() > maxEvents * 4) mouse_.pop_front();
    }
    void Camera(double tMs, double yawDeg, double pitchDeg) {
        camera_.push_back({tMs, yawDeg, pitchDeg});
        while (camera_.size() > maxEvents) camera_.pop_front();
    }

    std::vector<InputSignal> Evaluate() const {
        std::vector<InputSignal> out;
        for (const auto& kv : presses_) {
            const std::vector<double> times(kv.second.begin(), kv.second.end());
            auto sig = SignalsFromIntervals("key/button " + std::to_string(kv.first), AnalyzeIntervals(IntervalsOf(times), interval));
            out.insert(out.end(), sig.begin(), sig.end());
        }
        const std::vector<MouseSample> m(mouse_.begin(), mouse_.end());
        auto ms = SignalsFromMouse(AnalyzeMouse(m, mouse));
        out.insert(out.end(), ms.begin(), ms.end());
        if (!camera_.empty()) {
            const std::vector<CameraSample> c(camera_.begin(), camera_.end());
            auto cs = SignalsFromCamera(AnalyzeInputVsCamera(m, c, camera), camera);
            out.insert(out.end(), cs.begin(), cs.end());
        }
        return out;
    }
    int TotalWeight() const { int w = 0; for (const InputSignal& s : Evaluate()) w += s.weight; return w; }
    void Clear() { presses_.clear(); mouse_.clear(); camera_.clear(); }

private:
    void Push(std::deque<double>& d, double t) {
        d.push_back(t);
        while (d.size() > maxEvents) d.pop_front();
    }
    std::map<int, std::deque<double>> presses_;
    std::deque<MouseSample> mouse_;
    std::deque<CameraSample> camera_;
};

} // namespace input
} // namespace okayac
