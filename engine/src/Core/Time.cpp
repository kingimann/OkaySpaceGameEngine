// Time effects (hit stop, time-scale ramps), the fixed-step clock's helpers, the real
// and wall clocks and the duration formatters. The frame counters themselves stay
// header-inline in Time.hpp.
#include "okay/Core/Time.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace okay {

namespace {

constexpr int    kMaxHitStops = 8;
constexpr double kEps = 1e-7;   // 0.1 microsecond: float frame times never land closer than this to a boundary

struct Hit { double remaining; float scale; };
Hit    g_hit[kMaxHitStops];
int    g_hitN = 0;

bool   g_ramp = false;
double g_rampFrom = 1.0, g_rampTo = 1.0, g_rampDur = 0.0, g_rampT = 0.0;

Time::ClockFn g_mono = nullptr;
Time::ClockFn g_unix = nullptr;

// Started at load, so "since startup" is close to the real process start.
const std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();

float CleanScale(float s) {
    return s > 0.0f ? (s < Time::kMaxTimeScale ? s : Time::kMaxTimeScale) : 0.0f;   // NaN -> 0
}

}  // namespace

// ---- hit stop -----------------------------------------------------------------

void Time::RefreshFx() {
    float m = 1.0f;
    for (int i = 0; i < g_hitN; ++i) m = std::min(m, g_hit[i].scale);
    Time::s_hitMul = m;
    Time::s_hitCount = g_hitN;
    Time::s_fxActive = g_hitN > 0 || g_ramp;
}

void Time::HitStop(float seconds, float scale) {
    if (!(seconds > 0.0f) || !std::isfinite(seconds)) return;
    scale = CleanScale(scale);
    Hit h{(double)seconds, scale};
    if (g_hitN < kMaxHitStops) {
        g_hit[g_hitN++] = h;
    } else {
        // Full. An effect another one outlasts at the same or a stronger scale changes nothing
        // (the strongest running one decides), so it is the one to give up; the shortest-lived
        // effect is not always that one (a short freeze among long, weak slow-downs).
        auto dominates = [](const Hit& a, const Hit& b) { return a.remaining >= b.remaining && a.scale <= b.scale; };
        for (int i = 0; i < g_hitN; ++i) if (dominates(g_hit[i], h)) { RefreshFx(); return; }   // the newcomer adds nothing
        int victim = -1;
        for (int i = 0; i < g_hitN && victim < 0; ++i) if (dominates(h, g_hit[i])) victim = i;
        if (victim < 0) {   // an existing effect that another existing one covers (the shortest such)
            for (int i = 0; i < g_hitN; ++i)
                for (int j = 0; j < g_hitN; ++j)
                    if (i != j && dominates(g_hit[j], g_hit[i]) && (victim < 0 || g_hit[i].remaining < g_hit[victim].remaining)) { victim = i; break; }
        }
        if (victim < 0) {   // nothing is redundant: the shortest-lived makes room
            victim = 0;
            for (int i = 1; i < g_hitN; ++i) if (g_hit[i].remaining < g_hit[victim].remaining) victim = i;
        }
        g_hit[victim] = h;
    }
    RefreshFx();
}

float Time::HitStopRemaining() {
    double r = 0.0;
    for (int i = 0; i < g_hitN; ++i) r = std::max(r, g_hit[i].remaining);
    return (float)r;
}

void Time::CancelHitStop() { g_hitN = 0; RefreshFx(); }

// ---- ramp ---------------------------------------------------------------------

void Time::RampTimeScale(float target, float seconds) {
    target = CleanScale(target);
    if (!(seconds > 0.0f) || !std::isfinite(seconds)) {   // at once
        g_ramp = false;
        s_timeScale = target;
        RefreshFx();
        return;
    }
    g_ramp = true;
    g_rampFrom = (double)s_timeScale;
    g_rampTo = (double)target;
    g_rampDur = (double)seconds;
    g_rampT = 0.0;
    RefreshFx();
}

bool  Time::IsRamping() { return g_ramp; }
float Time::RampTarget() { return g_ramp ? (float)g_rampTo : s_timeScale; }
void  Time::CancelRamp() { g_ramp = false; RefreshFx(); }

void Time::ClearEffects() {
    g_hitN = 0;
    g_ramp = false;
    RefreshFx();
}

// Integrate (frozen ? 0 : scale * hitMultiplier) over `dt` real seconds, in segments
// that end where a hit stop expires or the ramp finishes. Inside a segment the hit
// multiplier is constant and the scale is a straight line, so the integral is exact.
double Time::StepEffects(float unscaledDt) {
    const double dt = (double)unscaledDt;
    double t = 0.0, acc = 0.0;
    int guard = 0;
    while (t < dt - kEps && guard++ < 4 * kMaxHitStops + 8) {
        double seg = dt - t;
        double mult = 1.0;
        for (int i = 0; i < g_hitN; ++i) {
            if (g_hit[i].remaining > kEps) {
                seg = std::min(seg, g_hit[i].remaining);
                mult = std::min(mult, (double)g_hit[i].scale);
            }
        }
        double s0, s1;
        if (g_ramp) {
            const double left = g_rampDur - g_rampT;
            seg = std::min(seg, std::max(left, 0.0));
            const double k0 = g_rampDur > 0.0 ? g_rampT / g_rampDur : 1.0;
            const double k1 = g_rampDur > 0.0 ? (g_rampT + seg) / g_rampDur : 1.0;
            s0 = g_rampFrom + (g_rampTo - g_rampFrom) * k0;
            s1 = g_rampFrom + (g_rampTo - g_rampFrom) * k1;
        } else {
            s0 = s1 = (double)s_timeScale;
        }
        if (!s_frozen) acc += seg * mult * 0.5 * (s0 + s1);
        for (int i = 0; i < g_hitN; ++i) g_hit[i].remaining -= seg;
        if (g_ramp) {
            g_rampT += seg;
            s_timeScale = CleanScale((float)s1);
            if (g_rampT >= g_rampDur - kEps) { s_timeScale = CleanScale((float)g_rampTo); g_ramp = false; }
        }
        t += seg;
        if (seg <= 0.0 && !g_ramp) break;   // nothing left to cross
    }
    // Drop the hit stops that ended.
    int w = 0;
    for (int i = 0; i < g_hitN; ++i) if (g_hit[i].remaining > kEps) g_hit[w++] = g_hit[i];
    g_hitN = w;
    RefreshFx();
    return acc;
}

// ---- real / wall clocks ---------------------------------------------------------

void Time::SetClockSources(ClockFn monotonic, ClockFn unixSeconds) {
    g_mono = monotonic;
    g_unix = unixSeconds;
}

double Time::RealtimeSinceStartup() {
    if (g_mono) return g_mono();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_start).count();
}

double Time::UnixSeconds() {
    if (g_unix) return g_unix();
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// ---- formatting ---------------------------------------------------------------

namespace {
// Whole seconds (rounded down, magnitude only) and a sign; non-finite is "--".
bool Split(double seconds, bool& neg, long long& whole, double& frac) {
    if (!std::isfinite(seconds)) return false;
    neg = seconds < 0.0;
    double a = std::fabs(seconds);
    if (a > 1e15) a = 1e15;
    whole = (long long)std::floor(a);
    frac = a - (double)whole;
    return true;
}
}  // namespace

std::string Time::FormatDuration(double seconds) {
    bool neg; long long whole; double frac;
    if (!Split(seconds, neg, whole, frac)) return "--";
    const long long d = whole / 86400, h = (whole / 3600) % 24, m = (whole / 60) % 60, s = whole % 60;
    char buf[64];
    if (d > 0)       std::snprintf(buf, sizeof buf, "%lldd %02lldh %02lldm %02llds", d, h, m, s);
    else if (h > 0)  std::snprintf(buf, sizeof buf, "%lldh %02lldm %02llds", h, m, s);
    else if (m > 0)  std::snprintf(buf, sizeof buf, "%lldm %02llds", m, s);
    else             std::snprintf(buf, sizeof buf, "%llds", s);
    // "-0s" would be silly: a negative that rounds down to nothing reads "0s".
    return (neg && whole > 0 ? "-" : "") + std::string(buf);
}

std::string Time::FormatClock(double seconds, bool millis) {
    bool neg; long long whole; double frac;
    if (!Split(seconds, neg, whole, frac)) return "--:--";
    const long long h = whole / 3600, m = (whole / 60) % 60, s = whole % 60;
    int ms = (int)std::floor(frac * 1000.0 + 1e-6);
    if (ms > 999) ms = 999;
    char buf[64];
    if (h > 0) std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", h, m, s);
    else       std::snprintf(buf, sizeof buf, "%02lld:%02lld", m, s);
    std::string out = buf;
    if (millis) { char tail[16]; std::snprintf(tail, sizeof tail, ".%03d", ms); out += tail; }
    return (neg && (whole > 0 || (millis && ms > 0)) ? "-" : "") + out;
}

}  // namespace okay
