#pragma once
#include <cmath>
#include <algorithm>

namespace okay {

/// Scalar math helpers, mirroring Unity's `Mathf` static class.
struct Mathf {
    static constexpr float PI       = 3.14159265358979323846f;
    static constexpr float Deg2Rad  = PI / 180.0f;
    static constexpr float Rad2Deg  = 180.0f / PI;
    static constexpr float Epsilon  = 1e-6f;
    static constexpr float Infinity = 3.402823466e+38f;

    static float Abs(float v)             { return std::fabs(v); }
    static float Sqrt(float v)            { return std::sqrt(v); }
    static float Sin(float r)             { return std::sin(r); }
    static float Cos(float r)             { return std::cos(r); }
    static float Tan(float r)             { return std::tan(r); }
    static float Atan2(float y, float x)  { return std::atan2(y, x); }
    static float Pow(float b, float e)    { return std::pow(b, e); }
    static float Floor(float v)           { return std::floor(v); }
    static float Ceil(float v)            { return std::ceil(v); }
    static float Round(float v)           { return std::round(v); }
    static float Sign(float v)            { return v >= 0.0f ? 1.0f : -1.0f; }

    static float Min(float a, float b)    { return a < b ? a : b; }
    static float Max(float a, float b)    { return a > b ? a : b; }

    static float Clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    static float Clamp01(float v) { return Clamp(v, 0.0f, 1.0f); }

    static float Lerp(float a, float b, float t) {
        return a + (b - a) * Clamp01(t);
    }
    static float LerpUnclamped(float a, float b, float t) {
        return a + (b - a) * t;
    }

    static float MoveTowards(float current, float target, float maxDelta) {
        if (Abs(target - current) <= maxDelta) return target;
        return current + Sign(target - current) * maxDelta;
    }

    static bool Approximately(float a, float b) {
        return Abs(b - a) < Max(Epsilon * Max(Abs(a), Abs(b)), Epsilon);
    }

    static float Repeat(float t, float length) {
        return Clamp(t - Floor(t / length) * length, 0.0f, length);
    }

    static float PingPong(float t, float length) {
        t = Repeat(t, length * 2.0f);
        return length - Abs(t - length);
    }
};

} // namespace okay
