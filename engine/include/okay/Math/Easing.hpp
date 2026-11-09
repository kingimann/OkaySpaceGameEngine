#pragma once
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace okay {

/// Classic easing functions for animation and game feel. Each takes a normalized
/// time `t` in [0, 1] and returns the eased progress (also roughly 0..1, though
/// Back/Elastic intentionally overshoot). Pair with Lerp to animate any value.
enum class Ease {
    Linear,
    QuadIn, QuadOut, QuadInOut,
    CubicIn, CubicOut, CubicInOut,
    SineIn, SineOut, SineInOut,
    ExpoIn, ExpoOut, ExpoInOut,
    BackIn, BackOut, BackInOut,
    ElasticOut, BounceOut,
    // Added later (appended so saved numbers keep their meaning).
    QuartIn, QuartOut, QuartInOut,
    QuintIn, QuintOut, QuintInOut,
    CircIn, CircOut, CircInOut,
    ElasticIn, ElasticInOut,
    BounceIn, BounceInOut,
    Count
};

namespace Easing {

inline float Linear(float t) { return t; }

inline float QuadIn(float t)  { return t * t; }
inline float QuadOut(float t) { return t * (2.0f - t); }
inline float QuadInOut(float t) {
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
}

inline float CubicIn(float t)  { return t * t * t; }
inline float CubicOut(float t) { float f = t - 1.0f; return f * f * f + 1.0f; }
inline float CubicInOut(float t) {
    return t < 0.5f ? 4.0f * t * t * t
                    : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
}

inline float SineIn(float t)  { return 1.0f - std::cos(t * Mathf::PI * 0.5f); }
inline float SineOut(float t) { return std::sin(t * Mathf::PI * 0.5f); }
inline float SineInOut(float t) { return -0.5f * (std::cos(Mathf::PI * t) - 1.0f); }

inline float ExpoIn(float t)  { return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f)); }
inline float ExpoOut(float t) { return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }
inline float ExpoInOut(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    if (t < 0.5f) return 0.5f * std::pow(2.0f, 20.0f * t - 10.0f);
    return 1.0f - 0.5f * std::pow(2.0f, -20.0f * t + 10.0f);
}

inline float BackIn(float t) {
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    return c3 * t * t * t - c1 * t * t;
}
inline float BackOut(float t) {
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    float f = t - 1.0f;
    return 1.0f + c3 * f * f * f + c1 * f * f;
}
inline float BackInOut(float t) {
    const float c2 = 1.70158f * 1.525f;
    if (t < 0.5f) return (std::pow(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) * 0.5f;
    return (std::pow(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (t * 2.0f - 2.0f) + c2) + 2.0f) * 0.5f;
}

inline float ElasticOut(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const float c4 = (2.0f * Mathf::PI) / 3.0f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
}

inline float BounceOut(float t) {
    const float n1 = 7.5625f, d1 = 2.75f;
    if (t < 1.0f / d1)      return n1 * t * t;
    else if (t < 2.0f / d1) { t -= 1.5f / d1;  return n1 * t * t + 0.75f; }
    else if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
    else                    { t -= 2.625f / d1; return n1 * t * t + 0.984375f; }
}

inline float QuartIn(float t)  { return t * t * t * t; }
inline float QuartOut(float t) { float f = t - 1.0f; return 1.0f - f * f * f * f; }
inline float QuartInOut(float t) {
    if (t < 0.5f) return 8.0f * t * t * t * t;
    float f = t - 1.0f; return 1.0f - 8.0f * f * f * f * f;
}
inline float QuintIn(float t)  { return t * t * t * t * t; }
inline float QuintOut(float t) { float f = t - 1.0f; return 1.0f + f * f * f * f * f; }
inline float QuintInOut(float t) {
    if (t < 0.5f) return 16.0f * t * t * t * t * t;
    float f = 2.0f * t - 2.0f; return 0.5f * f * f * f * f * f + 1.0f;
}
inline float CircIn(float t)  { return 1.0f - std::sqrt(std::max(0.0f, 1.0f - t * t)); }
inline float CircOut(float t) { float f = t - 1.0f; return std::sqrt(std::max(0.0f, 1.0f - f * f)); }
inline float CircInOut(float t) {
    if (t < 0.5f) return 0.5f * (1.0f - std::sqrt(std::max(0.0f, 1.0f - 4.0f * t * t)));
    float f = -2.0f * t + 2.0f; return 0.5f * (std::sqrt(std::max(0.0f, 1.0f - f * f)) + 1.0f);
}
inline float ElasticIn(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const float c4 = (2.0f * Mathf::PI) / 3.0f;
    return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
}
inline float ElasticInOut(float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const float c5 = (2.0f * Mathf::PI) / 4.5f;
    if (t < 0.5f) return -0.5f * std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5);
    return 0.5f * std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5) + 1.0f;
}
inline float BounceIn(float t) { return 1.0f - BounceOut(1.0f - t); }
inline float BounceInOut(float t) {
    return t < 0.5f ? 0.5f * (1.0f - BounceOut(1.0f - 2.0f * t)) : 0.5f * (1.0f + BounceOut(2.0f * t - 1.0f));
}

/// A falloff for punches and shakes: 1 at t = 0, 0 at t = 1. `rate` <= 0 (or NaN) is a straight line;
/// a larger rate drops exponentially (a big first swing and a short tail), reaching exactly 0 at t = 1.
inline float Decay(float t, float rate) {
    if (!(t > 0.0f)) return 1.0f;
    if (t >= 1.0f) return 0.0f;
    if (!(rate > 1e-4f) || !std::isfinite(rate)) return 1.0f - t;
    const double r = rate > 60.0f ? 60.0 : (double)rate;
    const double e1 = std::exp(-r);
    return (float)((std::exp(-r * (double)t) - e1) / (1.0 - e1));
}

namespace detail {
inline float EvaluateRaw(Ease e, float t) {
    switch (e) {
        case Ease::Linear:     return Linear(t);
        case Ease::QuadIn:     return QuadIn(t);
        case Ease::QuadOut:    return QuadOut(t);
        case Ease::QuadInOut:  return QuadInOut(t);
        case Ease::CubicIn:    return CubicIn(t);
        case Ease::CubicOut:   return CubicOut(t);
        case Ease::CubicInOut: return CubicInOut(t);
        case Ease::SineIn:     return SineIn(t);
        case Ease::SineOut:    return SineOut(t);
        case Ease::SineInOut:  return SineInOut(t);
        case Ease::ExpoIn:     return ExpoIn(t);
        case Ease::ExpoOut:    return ExpoOut(t);
        case Ease::ExpoInOut:  return ExpoInOut(t);
        case Ease::BackIn:     return BackIn(t);
        case Ease::BackOut:    return BackOut(t);
        case Ease::BackInOut:  return BackInOut(t);
        case Ease::ElasticOut: return ElasticOut(t);
        case Ease::BounceOut:  return BounceOut(t);
        case Ease::QuartIn:    return QuartIn(t);
        case Ease::QuartOut:   return QuartOut(t);
        case Ease::QuartInOut: return QuartInOut(t);
        case Ease::QuintIn:    return QuintIn(t);
        case Ease::QuintOut:   return QuintOut(t);
        case Ease::QuintInOut: return QuintInOut(t);
        case Ease::CircIn:     return CircIn(t);
        case Ease::CircOut:    return CircOut(t);
        case Ease::CircInOut:  return CircInOut(t);
        case Ease::ElasticIn:  return ElasticIn(t);
        case Ease::ElasticInOut: return ElasticInOut(t);
        case Ease::BounceIn:   return BounceIn(t);
        case Ease::BounceInOut: return BounceInOut(t);
        case Ease::Count:      break;
    }
    return t;
}
} // namespace detail

/// Evaluate an easing by enum (handy for serialization and editor dropdowns). Input outside 0..1
/// extrapolates (Back and Elastic keep swinging); NaN counts as 0 and infinity as the nearer end,
/// and a result that overflows (ExpoIn far past 1) stops at +-1e30, so the answer is always a number.
inline float Evaluate(Ease e, float t) {
    if (!std::isfinite(t)) t = t > 0.0f ? 1.0f : 0.0f;
    const float r = detail::EvaluateRaw(e, t);
    if (std::isfinite(r)) return r;
    return std::isnan(r) ? (t > 0.0f ? 1.0f : 0.0f) : (r > 0.0f ? 1.0e30f : -1.0e30f);
}

/// Display names, in enum order ("Out Quad"...), for dropdowns.
inline const char* Name(Ease e) {
    static const char* k[] = {"Linear",
        "In Quad", "Out Quad", "In Out Quad", "In Cubic", "Out Cubic", "In Out Cubic",
        "In Sine", "Out Sine", "In Out Sine", "In Expo", "Out Expo", "In Out Expo",
        "In Back", "Out Back", "In Out Back", "Out Elastic", "Out Bounce",
        "In Quart", "Out Quart", "In Out Quart", "In Quint", "Out Quint", "In Out Quint",
        "In Circ", "Out Circ", "In Out Circ", "In Elastic", "In Out Elastic",
        "In Bounce", "In Out Bounce"};
    const int i = (int)e;
    return i >= 0 && i < (int)Ease::Count ? k[i] : "Linear";
}

/// Parse an ease name, forgivingly: "out_quad", "OutQuad", "Out Quad", "easeOutQuad"
/// and "quad_out" all work. Unknown names give `fallback`.
inline Ease FromName(const std::string& name, Ease fallback = Ease::Linear) {
    std::string k;
    for (char c : name) if (c != '_' && c != ' ' && c != '-') k += (char)std::tolower((unsigned char)c);
    if (k.rfind("ease", 0) == 0) k = k.substr(4);
    if (k.empty()) return fallback;
    if (k == "linear") return Ease::Linear;
    for (int i = 0; i < (int)Ease::Count; ++i) {
        std::string n;
        for (const char* p = Name((Ease)i); *p; ++p) if (*p != ' ') n += (char)std::tolower((unsigned char)*p);
        if (n == k) return (Ease)i;
        // "quadout" / "quadinout" (family first)
        for (const char* dir : {"inout", "in", "out"}) {
            const std::string d = dir;
            if (n.rfind(d, 0) == 0 && n.size() > d.size() && n.substr(d.size()) + d == k) return (Ease)i;
        }
    }
    return fallback;
}

/// The compact name of an ease ("OutQuad", "InOutBack"): the display name without
/// spaces. FromName reads it back, so it is a stable key for data files.
inline std::string ToName(Ease e) {
    std::string s;
    for (const char* p = Name(e); *p; ++p) if (*p != ' ') s += *p;
    return s;
}

/// A CSS-style cubic bezier curve from (0,0) to (1,1) with control points (x1,y1)
/// and (x2,y2): `t` is the time 0..1, the result the eased progress (y may leave
/// 0..1 to overshoot). x1 and x2 are limited to 0..1 so the curve is a function of
/// time. Deterministic (solved in double with Newton steps and bisection).
inline float CubicBezier(float x1, float y1, float x2, float y2, float t) {
    if (!(t > 0.0f)) return 0.0f;     // also NaN
    if (t >= 1.0f) return 1.0f;
    const double ax = x1 > 0.0f ? (x1 < 1.0f ? (double)x1 : 1.0) : 0.0;   // NaN counts as 0
    const double bx = x2 > 0.0f ? (x2 < 1.0f ? (double)x2 : 1.0) : 0.0;
    const double ay = std::isfinite(y1) ? (double)y1 : 0.0;
    const double by = std::isfinite(y2) ? (double)y2 : 1.0;
    auto X = [&](double s) { const double u = 1.0 - s; return 3.0 * u * u * s * ax + 3.0 * u * s * s * bx + s * s * s; };
    auto dX = [&](double s) { const double u = 1.0 - s; return 3.0 * u * u * ax + 6.0 * u * s * (bx - ax) + 3.0 * s * s * (1.0 - bx); };
    auto Y = [&](double s) { const double u = 1.0 - s; return 3.0 * u * u * s * ay + 3.0 * u * s * s * by + s * s * s; };
    const double target = (double)t;
    double s = target;
    bool solved = false;
    for (int i = 0; i < 8; ++i) {
        const double err = X(s) - target;
        if (std::fabs(err) < 1e-9) { solved = true; break; }
        const double d = dX(s);
        if (std::fabs(d) < 1e-7) break;
        s -= err / d;
        if (!(s >= 0.0 && s <= 1.0)) break;
    }
    if (!solved) {   // flat or wild slope: bisect, which always converges (X is monotonic)
        double lo = 0.0, hi = 1.0;
        s = target;
        for (int i = 0; i < 60; ++i) {
            if (X(s) < target) lo = s; else hi = s;
            s = 0.5 * (lo + hi);
        }
    }
    return (float)Y(s);
}

/// A staircase of `steps` equal jumps. By default the value holds at 0 first and
/// reaches 1 only at the end (CSS "jump-end"); `jumpStart` jumps at once instead.
inline float Steps(int steps, float t, bool jumpStart = false) {
    if (steps < 1) steps = 1;
    if (!(t > 0.0f)) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    const double n = (double)steps;
    const double x = (double)t * n;
    const double k = jumpStart ? std::ceil(x - 1e-9) : std::floor(x + 1e-9);
    const double v = k / n;
    return (float)(v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v);
}

} // namespace Easing

/// An ease described as plain data: one of the presets, a CSS-style cubic bezier or a
/// staircase. It reads and writes a text form, so a data file or a script can name it:
/// "OutQuad" (any spelling FromName takes), "cubic-bezier(0.4, 0, 0.2, 1)", "steps(4)",
/// "steps(4, start)", and the CSS names "ease", "ease-in", "ease-out", "ease-in-out".
struct EaseCurve {
    enum class Kind { Preset, Bezier, Steps };
    Kind  kind = Kind::Preset;
    Ease  ease = Ease::Linear;           ///< Preset
    float x1 = 0.0f, y1 = 0.0f, x2 = 1.0f, y2 = 1.0f;   ///< Bezier
    int   steps = 1;                     ///< Steps
    bool  jumpStart = false;             ///< Steps

    static EaseCurve OfPreset(Ease e) { EaseCurve c; c.ease = e; return c; }
    static EaseCurve OfBezier(float x1, float y1, float x2, float y2) {
        EaseCurve c; c.kind = Kind::Bezier; c.x1 = x1; c.y1 = y1; c.x2 = x2; c.y2 = y2; return c;
    }
    static EaseCurve OfSteps(int count, bool jumpStart = false) {
        EaseCurve c; c.kind = Kind::Steps; c.steps = count < 1 ? 1 : count; c.jumpStart = jumpStart; return c;
    }

    float Evaluate(float t) const {
        switch (kind) {
            case Kind::Bezier: return Easing::CubicBezier(x1, y1, x2, y2, t);
            case Kind::Steps:  return Easing::Steps(steps, t, jumpStart);
            default:           return Easing::Evaluate(ease, t);
        }
    }

    /// The text form Parse reads back exactly.
    std::string ToString() const {
        auto num = [](float v) {   // the shortest text that reads back to the same float
            char buf[40];
            for (int prec = 1; prec <= 9; ++prec) {
                std::snprintf(buf, sizeof(buf), "%.*g", prec, (double)v);
                if (std::strtof(buf, nullptr) == v) break;
            }
            return std::string(buf);
        };
        switch (kind) {
            case Kind::Bezier:
                return "cubic-bezier(" + num(x1) + "," + num(y1) + "," + num(x2) + "," + num(y2) + ")";
            case Kind::Steps:
                return "steps(" + std::to_string(steps) + (jumpStart ? ",start)" : ")");
            default:
                return Easing::ToName(ease);
        }
    }

    /// Read the text form. False (and `out` untouched) for text it does not know.
    static bool Parse(const std::string& text, EaseCurve& out) {
        std::string k;
        for (char c : text) if (c != ' ' && c != '\t') k += (char)std::tolower((unsigned char)c);
        if (k.empty()) return false;
        struct Css { const char* name; float x1, y1, x2, y2; };
        static const Css css[] = {{"ease", 0.25f, 0.1f, 0.25f, 1.0f}, {"ease-in", 0.42f, 0.0f, 1.0f, 1.0f},
                                  {"ease-out", 0.0f, 0.0f, 0.58f, 1.0f}, {"ease-in-out", 0.42f, 0.0f, 0.58f, 1.0f}};
        for (const Css& c : css)
            if (k == c.name) { out = OfBezier(c.x1, c.y1, c.x2, c.y2); return true; }
        auto args = [&](const char* head, std::vector<std::string>& parts) {
            const std::string h = head;
            if (k.compare(0, h.size(), h) != 0 || k.back() != ')') return false;
            const std::string inner = k.substr(h.size(), k.size() - h.size() - 1);
            std::string cur;
            for (char c : inner) { if (c == ',') { parts.push_back(cur); cur.clear(); } else cur += c; }
            parts.push_back(cur);
            return true;
        };
        auto number = [](const std::string& s, float& v) {
            if (s.empty()) return false;
            char* end = nullptr;
            const float f = std::strtof(s.c_str(), &end);
            if (end != s.c_str() + s.size() || !std::isfinite(f)) return false;
            v = f;
            return true;
        };
        std::vector<std::string> p;
        if (args("cubic-bezier(", p)) {
            float v[4];
            if (p.size() != 4) return false;
            for (int i = 0; i < 4; ++i) if (!number(p[(std::size_t)i], v[i])) return false;
            out = OfBezier(v[0], v[1], v[2], v[3]);
            return true;
        }
        p.clear();
        if (args("steps(", p)) {
            float n = 0.0f;
            if (p.empty() || p.size() > 2 || !number(p[0], n) || n < 1.0f || n > 10000.0f || n != std::floor(n)) return false;
            bool start = false;
            if (p.size() == 2) {
                if (p[1] == "start" || p[1] == "jump-start") start = true;
                else if (p[1] != "end" && p[1] != "jump-end") return false;
            }
            out = OfSteps((int)n, start);
            return true;
        }
        const Ease e = Easing::FromName(text, Ease::Count);
        if (e == Ease::Count) return false;
        out = OfPreset(e);
        return true;
    }
};

/// A one-shot float tween: animates from `from` to `to` over `duration` seconds
/// using an easing curve. Advance it with Update(dt) and read Value().
struct Tween {
    float from = 0.0f;
    float to = 1.0f;
    float duration = 1.0f;
    Ease  ease = Ease::Linear;

    Tween() = default;
    Tween(float from, float to, float duration, Ease ease = Ease::Linear)
        : from(from), to(to), duration(duration), ease(ease) {}

    /// Advance time and return the current value.
    /// A step that is NaN, negative or infinite counts as 0 (it must not poison the tween for good).
    float Update(float dt) { if (dt > 0.0f && std::isfinite(dt)) m_elapsed += (double)dt; return Value(); }

    /// Current interpolated value (clamped to the [from, to] endpoints).
    float Value() const {
        return from + (to - from) * Easing::Evaluate(ease, Progress());
    }

    bool  Done() const { return !(duration > 0.0f) || m_elapsed >= (double)duration; }
    float Progress() const { return duration > 0.0f ? Mathf::Clamp01((float)(m_elapsed / (double)duration)) : 1.0f; }
    void  Reset() { m_elapsed = 0.0; }

private:
    double m_elapsed = 0.0;   // double: a long tween (an hour-long countdown) keeps its precision
};

} // namespace okay
