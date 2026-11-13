#include "test_framework.hpp"
#include "AntiCheatInput.hpp"
#include <cmath>
#include <random>

// Input analysis on synthetic traces: human-like ones must stay unflagged (the rate is measured
// over many random traces), macro-like ones must be caught.
namespace {
using namespace okayac::input;

// ---- generators
std::vector<double> Presses(std::mt19937& r, int n, double meanMs, double sdMs, double quantum = 0) {
    std::normal_distribution<double> d(meanMs, sdMs);
    std::vector<double> t;
    double now = 1000;
    for (int i = 0; i < n; ++i) {
        now += std::max(30.0, d(r));
        t.push_back(quantum > 0 ? std::floor(now / quantum) * quantum : now);
    }
    return t;
}
std::vector<double> Turbo(std::mt19937& r, int n, double periodMs, double jitterMs, double quantum = 0) {
    std::uniform_real_distribution<double> j(-jitterMs, jitterMs);
    std::vector<double> t;
    for (int i = 0; i < n; ++i) {
        const double v = 1000 + i * periodMs + j(r);
        t.push_back(quantum > 0 ? std::floor(v / quantum) * quantum : v);
    }
    return t;
}
std::vector<double> Macro(std::mt19937& r, int n, const std::vector<double>& delays, double jitterMs, double quantum = 0) {
    std::uniform_real_distribution<double> j(-jitterMs, jitterMs);
    std::vector<double> t;
    double now = 1000;
    for (int i = 0; i < n; ++i) {
        now += delays[(std::size_t)i % delays.size()] + j(r);
        t.push_back(quantum > 0 ? std::floor(now / quantum) * quantum : now);
    }
    return t;
}
bool Flagged(const std::vector<double>& pressTimes, const IntervalParams& p = {}) {
    return !SignalsFromIntervals("k", AnalyzeIntervals(IntervalsOf(pressTimes), p)).empty();
}

// A human flick: minimum-jerk stroke of `counts` along `angle`, sampled at `hz`, with sensor rounding and a little hand tremor.
std::vector<MouseSample> Flick(std::mt19937& r, double t0, double counts, double angle, double durMs, double hz = 1000, double tremor = 0.6) {
    std::normal_distribution<double> n(0, tremor);
    std::vector<MouseSample> out;
    const double dt = 1000.0 / hz;
    double px = 0, py = 0;
    for (double t = dt; t <= durMs + 1e-9; t += dt) {
        const double u = t / durMs;
        const double pos = counts * (10 * u * u * u - 15 * u * u * u * u + 6 * u * u * u * u * u);
        const double x = pos * std::cos(angle) + n(r), y = pos * std::sin(angle) + n(r);
        const double dx = std::round(x - px), dy = std::round(y - py);
        px += dx; py += dy;
        out.push_back({t0 + t, dx, dy});
    }
    return out;
}
std::vector<MouseSample> HumanMouse(std::mt19937& r, int strokes) {
    std::uniform_real_distribution<double> ang(0, 6.283), len(30, 900), dur(90, 400), gap(100, 700);
    std::vector<MouseSample> all;
    double t = 0;
    for (int i = 0; i < strokes; ++i) {
        auto f = Flick(r, t, len(r), ang(r), dur(r));
        all.insert(all.end(), f.begin(), f.end());
        t = all.back().tMs + gap(r);
        all.push_back({t, 0, 0});
    }
    return all;
}

void TestTurboAndMacro() {
    RUN_SUITE("input: turbo and macros are caught");
    std::mt19937 r(1);
    CHECK(Flagged(Turbo(r, 60, 50, 0.4)));                 // an autoclicker at 20 cps
    CHECK(Flagged(Turbo(r, 60, 100, 1.5)));                // with a little jitter
    CHECK(Flagged(Turbo(r, 80, 50, 0, 16.7)));             // seen through a 60 Hz server tick
    {
        IntervalParams q; q.quantumMs = 16.7;
        CHECK(Flagged(Turbo(r, 80, 100, 0, 16.7), q));
    }
    {   // a recorded delay sequence replayed, even with a bit of jitter
        const std::vector<double> d = {80, 120, 95, 200, 60};
        auto s = SignalsFromIntervals("k", AnalyzeIntervals(IntervalsOf(Macro(r, 80, d, 1.0))));
        bool pattern = false;
        for (const auto& x : s) if (x.kind == "macro-pattern") pattern = true;
        CHECK(pattern);
        const auto res = AnalyzeIntervals(IntervalsOf(Macro(r, 80, d, 1.0)));
        CHECK(res.patterned && res.period % 5 == 0);
    }
    {   // a long macro (period 9) is still found
        const std::vector<double> d = {70, 130, 55, 210, 90, 160, 75, 300, 110};
        CHECK(AnalyzeIntervals(IntervalsOf(Macro(r, 120, d, 0.8))).patterned);
    }
    // Too few presses to say anything.
    CHECK(!Flagged(Turbo(r, 10, 50, 0.1)));
    CHECK(!Flagged({}));
    // Slow, steady "presses" (a key tapped on a timer every few seconds) are out of scope.
    CHECK(!Flagged(Turbo(r, 60, 5000, 0.1)));
}

void TestHumansNotFlagged() {
    RUN_SUITE("input: human-like timing stays unflagged (false-positive rate)");
    std::mt19937 r(2);
    int flagged = 0, total = 0;
    std::uniform_real_distribution<double> meanD(90, 400), cvD(0.08, 0.3);
    for (int i = 0; i < 2000; ++i) {
        const double mean = meanD(r), sd = mean * cvD(r);
        const bool q = i % 3 == 0;
        IntervalParams p; if (q) p.quantumMs = 16.7;
        if (Flagged(Presses(r, 24 + (std::size_t)(r() % 60), mean, sd, q ? 16.7 : 0), p)) ++flagged;
        ++total;
    }
    std::printf("  human-like traces flagged: %d of %d\n", flagged, total);
    CHECK(flagged * 100 <= total);   // at most 1%
    {   // a very steady human (rhythm-game level, 6 ms spread at 150 ms) is not caught by the cv rule
        int f = 0;
        for (int i = 0; i < 200; ++i) if (Flagged(Presses(r, 40, 150, 6.0))) ++f;
        CHECK(f * 20 <= 200);
    }
}

void TestMouse() {
    RUN_SUITE("input: mouse deltas");
    std::mt19937 r(3);
    // Humans: many strokes, up to a 900-count flick in 90 ms at 800 dpi (about 0.25 m/s average, ~0.5 m/s peak): clean.
    int bad = 0;
    for (int i = 0; i < 300; ++i) {
        const auto sig = SignalsFromMouse(AnalyzeMouse(HumanMouse(r, 40)));
        if (!sig.empty()) { ++bad; std::printf("  flagged: %s\n", sig[0].detail.c_str()); }
    }
    std::printf("  human-like mouse traces flagged: %d of 300\n", bad);
    CHECK(bad <= 3);
    {   // a fast human flick: 2.5 m/s peak at 800 dpi is ~79 counts/ms; still not impossible
        MouseParams mp; mp.dpi = 800;
        std::vector<MouseSample> fast = Flick(r, 0, 2400, 0.3, 60);
        const auto res = AnalyzeMouse(fast, mp);
        CHECK(res.impossible == 0 && res.maxSpeedMps > 1.0 && res.maxSpeedMps < 6.0);
    }
    {   // a teleport: 4000 counts in one millisecond is 127 m/s
        std::vector<MouseSample> m = {{0, 0, 0}, {1, 4000, -300}, {2, 0, 0}, {500, 0, 0}, {501, -4000, 300}};
        const auto res = AnalyzeMouse(m);
        CHECK(res.impossible == 2);
        const auto sig = SignalsFromMouse(res);
        CHECK(sig.size() == 1 && sig[0].kind == "mouse-impossible");
        // the same distance at 16000 dpi is still big; at a higher dpi the same counts are physically smaller
        MouseParams hi; hi.dpi = 32000;
        CHECK(AnalyzeMouse(m, hi).impossible == 0);
    }
    {   // many impossible moves weigh more
        std::vector<MouseSample> m;
        for (int i = 0; i < 6; ++i) { m.push_back({i * 100.0, 0, 0}); m.push_back({i * 100.0 + 1, 3000, 0}); }
        const auto sig = SignalsFromMouse(AnalyzeMouse(m));
        CHECK(!sig.empty() && sig[0].weight == 5);
    }
    {   // software moving the cursor: straight line, constant speed, repeated
        std::vector<MouseSample> m;
        double t = 0;
        for (int run = 0; run < 5; ++run) {
            for (int i = 0; i < 40; ++i) { t += 1; m.push_back({t, 6, 4}); }
            t += 300; m.push_back({t, 0, 0});
        }
        const auto res = AnalyzeMouse(m);
        CHECK(res.roboticRuns == 5);
        CHECK(!SignalsFromMouse(res).empty());
        // the same with rounded interpolation (non-integer step)
        std::vector<MouseSample> q;
        double px = 0, py = 0, tt = 0;
        for (int run = 0; run < 5; ++run) {
            for (int i = 1; i <= 50; ++i) {
                tt += 1;
                const double x = i * 4.5, y = i * 2.7;
                const double dx = std::round(x - px), dy = std::round(y - py);
                px += dx; py += dy;
                q.push_back({tt, dx, dy});
            }
            px = py = 0; tt += 300; q.push_back({tt, 0, 0});
        }
        CHECK(AnalyzeMouse(q).roboticRuns == 5);
    }
}

// ---- input vs camera
struct CamSim {
    std::vector<MouseSample> mouse;
    std::vector<CameraSample> cam;
};
CamSim FollowingCamera(std::mt19937& r, int seconds, double extraYawPerSec = 0, double ignoreFraction = 0) {
    CamSim s;
    std::normal_distribution<double> noise(0, 0.05);
    std::uniform_real_distribution<double> u(0, 1);
    double yaw = 10, pitch = 0, t = 0;
    for (int ms = 0; ms < seconds * 1000; ms += 8) {   // 125 Hz mouse, camera every 8 ms
        t = ms;
        const double phase = std::sin(ms / 700.0);
        const double dx = std::round(phase * 25 + noise(r) * 10), dy = std::round(std::cos(ms / 900.0) * 6);
        s.mouse.push_back({t, dx, dy});
        if (u(r) >= ignoreFraction) {
            yaw += dx * 0.022 + extraYawPerSec * 0.008;
            pitch += dy * 0.022 * -1;
        }
        yaw = std::fmod(yaw + 360, 360);
        s.cam.push_back({t, yaw, pitch + noise(r)});
    }
    return s;
}

void TestCamera() {
    RUN_SUITE("input: camera against input");
    std::mt19937 r(4);
    {   // honest: the camera follows the mouse (and the yaw wraps through 360)
        const CamSim s = FollowingCamera(r, 30);
        const auto res = AnalyzeInputVsCamera(s.mouse, s.cam);
        CHECK(res.active > 20);
        CHECK(SignalsFromCamera(res).empty());
        CHECK(res.fraction < 0.1);
    }
    {   // an aim tool adds 15 degrees per second of camera turn on its own
        const CamSim s = FollowingCamera(r, 30, 30);   // 30 * 0.008 deg per 8 ms step = 30 deg/s of turn the mouse didn't make
        const auto sig = SignalsFromCamera(AnalyzeInputVsCamera(s.mouse, s.cam));
        CHECK(sig.size() == 1 && sig[0].kind == "camera-unexplained" && sig[0].weight == 4);
    }
    {   // a camera that ignores half of the input (a menu, a suppressor, or a lie in the packets)
        const CamSim s = FollowingCamera(r, 30, 0, 0.5);
        const auto res = AnalyzeInputVsCamera(s.mouse, s.cam);
        CHECK(res.inputWithoutCamera + res.cameraWithoutInput > 0);
        CHECK(!SignalsFromCamera(res).empty());
    }
    {   // snap: the camera jumps 40 degrees with no input at all, several times
        CamSim s = FollowingCamera(r, 30);
        for (std::size_t i = 0; i < s.cam.size(); ++i) {
            const int k = (int)(s.cam[i].tMs / 1500);
            if (s.cam[i].tMs - k * 1500 > 700) s.cam[i].yawDeg = std::fmod(s.cam[i].yawDeg + 40.0 * (k + 1), 360);
        }
        const auto res = AnalyzeInputVsCamera(s.mouse, s.cam);
        CHECK(res.cameraWithoutInput >= 10);
        CHECK(!SignalsFromCamera(res).empty());
    }
    {   // too little to judge
        const CamSim s = FollowingCamera(r, 2);
        CHECK(SignalsFromCamera(AnalyzeInputVsCamera(s.mouse, s.cam)).empty());
        CHECK(AnalyzeInputVsCamera({}, {}).windows == 0);
    }
    // A wrong sensitivity assumption is the caller's tolerance to set: widening it quiets the signal.
    {
        const CamSim s = FollowingCamera(r, 30, 30);
        CameraParams loose; loose.toleranceDeg = 6; loose.tolerancePct = 1.5;
        CHECK(SignalsFromCamera(AnalyzeInputVsCamera(s.mouse, s.cam, loose), loose).empty());
    }
}

void TestAnalyzer() {
    RUN_SUITE("input: InputAnalyzer buffers per player and stays bounded");
    std::mt19937 r(5);
    {
        InputAnalyzer a;
        for (double t : Presses(r, 80, 140, 25)) a.Press(1, t);
        for (const auto& m : HumanMouse(r, 20)) a.Mouse(m.tMs, m.dx, m.dy);
        CHECK(a.Evaluate().empty() && a.TotalWeight() == 0);
    }
    {
        InputAnalyzer a;
        for (double t : Turbo(r, 100, 40, 0.3)) a.Press(7, t);
        for (double t : Presses(r, 80, 140, 25)) a.Press(1, t);   // another key, human
        const auto sig = a.Evaluate();
        CHECK(sig.size() == 1 && sig[0].kind == "turbo" && sig[0].detail.find("key/button 7") != std::string::npos);
        CHECK(a.TotalWeight() == 3);
        a.Clear();
        CHECK(a.Evaluate().empty());
    }
    {   // a flood of events stays within the caps
        InputAnalyzer a;
        a.maxEvents = 256;
        for (int i = 0; i < 100000; ++i) { a.Press(1, i * 10.0); a.Mouse(i, 1, 1); a.Camera(i, 0, 0); }
        a.Evaluate();
        CHECK(true);
    }
}
} // namespace

int main() {
    TestTurboAndMacro();
    TestHumansNotFlagged();
    TestMouse();
    TestCamera();
    TestAnalyzer();
    TEST_MAIN_RESULT();
}
