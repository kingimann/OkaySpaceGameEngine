#pragma once
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

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

/// Evaluate an easing by enum (handy for serialization and editor dropdowns).
inline float Evaluate(Ease e, float t) {
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

} // namespace Easing

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
    float Update(float dt) { m_elapsed += dt; return Value(); }

    /// Current interpolated value (clamped to the [from, to] endpoints).
    float Value() const {
        float t = duration > 0.0f ? Mathf::Clamp01(m_elapsed / duration) : 1.0f;
        return from + (to - from) * Easing::Evaluate(ease, t);
    }

    bool  Done() const { return m_elapsed >= duration; }
    float Progress() const { return duration > 0.0f ? Mathf::Clamp01(m_elapsed / duration) : 1.0f; }
    void  Reset() { m_elapsed = 0.0f; }

private:
    float m_elapsed = 0.0f;
};

} // namespace okay
