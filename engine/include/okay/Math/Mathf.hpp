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

    /// Where `value` lies between a and b, as a 0..1 fraction (inverse of Lerp).
    static float InverseLerp(float a, float b, float value) {
        if (Approximately(a, b)) return 0.0f;
        return Clamp01((value - a) / (b - a));
    }

    /// Hermite smoothstep between edges.
    static float SmoothStep(float from, float to, float t) {
        t = Clamp01((t - from) / (to - from > 0 ? (to - from) : 1.0f));
        return t * t * (3.0f - 2.0f * t);
    }

    /// Shortest signed difference between two angles (degrees), in [-180, 180].
    static float DeltaAngle(float current, float target) {
        float d = Repeat(target - current, 360.0f);
        if (d > 180.0f) d -= 360.0f;
        return d;
    }

    /// Lerp between angles taking the shortest path (degrees).
    static float LerpAngle(float a, float b, float t) {
        return a + DeltaAngle(a, b) * Clamp01(t);
    }

    /// Critically-damped spring toward a target (Unity's SmoothDamp).
    static float SmoothDamp(float current, float target, float& velocity,
                            float smoothTime, float deltaTime,
                            float maxSpeed = Infinity) {
        smoothTime = Max(0.0001f, smoothTime);
        float omega = 2.0f / smoothTime;
        float x = omega * deltaTime;
        float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        float change = current - target;
        float maxChange = maxSpeed * smoothTime;
        change = Clamp(change, -maxChange, maxChange);
        float temp = (velocity + omega * change) * deltaTime;
        velocity = (velocity - omega * temp) * exp;
        float output = (current - change) + (change + temp) * exp;
        // Prevent overshoot.
        if ((target - current > 0.0f) == (output > target)) {
            output = target;
            velocity = (output - target) / deltaTime;
        }
        return output;
    }
};

} // namespace okay
