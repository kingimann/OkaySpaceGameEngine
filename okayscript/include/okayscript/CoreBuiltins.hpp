#pragma once
// The builtins every OkayScript program has, shared by the engine's OkayScriptVM and the standalone okayscript CLI so
// both answer the same: maths (abs, sin, cos, sqrt, floor, min, max, tan, pow, round, ceil, sign, atan2, clamp, lerp,
// len, hypot, atan, asin, acos, log, exp, wrap, ping_pong, smoothstep, clamp01, inverse_lerp) and conversions
// (to_num / num, to_str / str). Moved from the engine (OkayScriptVM_Network.cpp, OkayScriptVM_Math.cpp) with their
// rules unchanged: missing arguments read as 0 (or the usual default), never an error, and `len(x, y)` is the length
// of the vector (x, y), not a count (use count(), .Length or .Count for that).
// They are raw builtins (Runtime.hpp): `b` is BasicRuntime<V>::builtins or the engine's table of the same type.
#include <cmath>
#include <string>
#include <vector>

namespace okayscript {
namespace detail {
namespace m {   // the float maths the engine's Mathf does, with the same results
inline float Abs(float v) { return std::fabs(v); }
inline float Sqrt(float v) { return std::sqrt(v); }
inline float Sin(float r) { return std::sin(r); }
inline float Cos(float r) { return std::cos(r); }
inline float Floor(float v) { return std::floor(v); }
inline float Ceil(float v) { return std::ceil(v); }
inline float Round(float v) { return std::round(v); }
inline float Min(float a, float b) { return a < b ? a : b; }
inline float Max(float a, float b) { return a > b ? a : b; }
inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Clamp01(float v) { return Clamp(v, 0.0f, 1.0f); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * Clamp01(t); }
} // namespace m
} // namespace detail

/// abs, sin, cos, sqrt, floor, min, max, tan, pow, round, ceil, sign, atan2, clamp, lerp, len, hypot, atan, asin,
/// acos, log, exp, wrap, ping_pong, smoothstep, clamp01, inverse_lerp.
template <class V, class Table> void InstallMathBuiltins(Table& b) {
    namespace m = detail::m;
    b["abs"]   = [](std::vector<V>& a) { return V{m::Abs(a.empty() ? 0 : a[0].AsFloat())}; };
    b["sin"]   = [](std::vector<V>& a) { return V{m::Sin(a.empty() ? 0 : a[0].AsFloat())}; };
    b["cos"]   = [](std::vector<V>& a) { return V{m::Cos(a.empty() ? 0 : a[0].AsFloat())}; };
    b["sqrt"]  = [](std::vector<V>& a) { return V{m::Sqrt(a.empty() ? 0 : a[0].AsFloat())}; };
    b["floor"] = [](std::vector<V>& a) { return V{m::Floor(a.empty() ? 0 : a[0].AsFloat())}; };
    b["min"]   = [](std::vector<V>& a) { return V{m::Min(a.size() > 0 ? a[0].AsFloat() : 0, a.size() > 1 ? a[1].AsFloat() : 0)}; };
    b["max"]   = [](std::vector<V>& a) { return V{m::Max(a.size() > 0 ? a[0].AsFloat() : 0, a.size() > 1 ? a[1].AsFloat() : 0)}; };
    b["tan"]   = [](std::vector<V>& a) { return V{std::tan(a.empty() ? 0 : a[0].AsFloat())}; };
    b["pow"]   = [](std::vector<V>& a) { return V{std::pow(a.size() > 0 ? a[0].AsFloat() : 0, a.size() > 1 ? a[1].AsFloat() : 1)}; };
    b["round"] = [](std::vector<V>& a) { return V{m::Round(a.empty() ? 0 : a[0].AsFloat())}; };
    b["ceil"]  = [](std::vector<V>& a) { return V{m::Ceil(a.empty() ? 0 : a[0].AsFloat())}; };
    b["sign"]  = [](std::vector<V>& a) { float v = a.empty() ? 0 : a[0].AsFloat(); return V{v > 0 ? 1.0f : v < 0 ? -1.0f : 0.0f}; };
    b["atan2"] = [](std::vector<V>& a) { return V{std::atan2(a.size() > 0 ? a[0].AsFloat() : 0, a.size() > 1 ? a[1].AsFloat() : 0)}; };
    b["clamp"] = [](std::vector<V>& a) {
        float v = a.size() > 0 ? a[0].AsFloat() : 0, lo = a.size() > 1 ? a[1].AsFloat() : 0, hi = a.size() > 2 ? a[2].AsFloat() : 1;
        return V{m::Clamp(v, lo, hi)};
    };
    b["lerp"]  = [](std::vector<V>& a) {
        float x = a.size() > 0 ? a[0].AsFloat() : 0, y = a.size() > 1 ? a[1].AsFloat() : 0, t = a.size() > 2 ? a[2].AsFloat() : 0;
        return V{m::Lerp(x, y, t)};
    };
    b["len"]   = [](std::vector<V>& a) { float x = a.size() > 0 ? a[0].AsFloat() : 0, y = a.size() > 1 ? a[1].AsFloat() : 0; return V{m::Sqrt(x * x + y * y)}; };
    b["hypot"] = [](std::vector<V>& a) { float x = a.size() > 0 ? a[0].AsFloat() : 0, y = a.size() > 1 ? a[1].AsFloat() : 0; return V{m::Sqrt(x * x + y * y)}; };
    b["atan"]  = [](std::vector<V>& a) { return V{std::atan(a.empty() ? 0 : a[0].AsFloat())}; };
    b["asin"]  = [](std::vector<V>& a) { return V{std::asin(a.empty() ? 0 : a[0].AsFloat())}; };
    b["acos"]  = [](std::vector<V>& a) { return V{std::acos(a.empty() ? 0 : a[0].AsFloat())}; };
    b["log"]   = [](std::vector<V>& a) { return V{std::log(a.empty() ? 1 : a[0].AsFloat())}; };
    b["exp"]   = [](std::vector<V>& a) { return V{std::exp(a.empty() ? 0 : a[0].AsFloat())}; };
    // Wrap a value into [lo, hi) — angles, looping indices, scrolling.
    b["wrap"]  = [](std::vector<V>& a) {
        float v = a.size() > 0 ? a[0].AsFloat() : 0, lo = a.size() > 1 ? a[1].AsFloat() : 0, hi = a.size() > 2 ? a[2].AsFloat() : 1;
        float span = hi - lo;
        if (span <= 0.0f) return V{lo};
        float r = std::fmod(v - lo, span);
        if (r < 0) r += span;
        return V{lo + r};
    };
    // Triangle wave 0..len..0 — patrols, breathing scale, bobbing.
    b["ping_pong"] = [](std::vector<V>& a) {
        float t = a.size() > 0 ? a[0].AsFloat() : 0, len = a.size() > 1 ? a[1].AsFloat() : 1;
        if (len <= 0.0f) return V{0.0f};
        float m = std::fmod(t, 2.0f * len);
        if (m < 0) m += 2.0f * len;
        return V{m <= len ? m : 2.0f * len - m};
    };
    // Smooth (ease in/out) interpolation between a and b.
    b["smoothstep"] = [](std::vector<V>& a) {
        float x = a.size() > 0 ? a[0].AsFloat() : 0, y = a.size() > 1 ? a[1].AsFloat() : 1, t = a.size() > 2 ? a[2].AsFloat() : 0;
        t = m::Clamp01(t);
        t = t * t * (3.0f - 2.0f * t);
        return V{m::Lerp(x, y, t)};
    };
    // Clamp to [0,1] (Mathf.Clamp01).
    b["clamp01"] = [](std::vector<V>& a) {
        return V{m::Clamp01(a.empty() ? 0.0f : a[0].AsFloat())};
    };
    // Where v lies between a and b, as 0..1 (Mathf.InverseLerp).
    b["inverse_lerp"] = [](std::vector<V>& a) {
        float lo = a.size() > 0 ? a[0].AsFloat() : 0, hi = a.size() > 1 ? a[1].AsFloat() : 1,
              v  = a.size() > 2 ? a[2].AsFloat() : 0;
        if (hi - lo == 0.0f) return V{0.0f};
        return V{m::Clamp01((v - lo) / (hi - lo))};
    };
}

/// to_num / num (text to a number, 0 when it is not one: "12abc" reads 12) and to_str / str.
template <class V, class Table> void InstallConversionBuiltins(Table& b) {
    b["to_num"] = [](std::vector<V>& a) {
        if (a.empty()) return V{0.0f};
        try { return V{std::stof(a[0].AsString())}; } catch (...) { return V{0.0f}; }
    };
    b["to_str"] = [](std::vector<V>& a) { return V{a.empty() ? std::string{} : a[0].AsString()}; };
    b["num"] = b["to_num"];
    b["str"] = b["to_str"];
}

} // namespace okayscript
