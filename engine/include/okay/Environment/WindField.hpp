#pragma once
// The wind field (docs/gameplay/wind.md): how hard and which way the air moves at any world position and time.
//
// Written from general ideas only (no code from another project):
//  - value noise on an integer lattice (a hash of the corner, smooth blend between corners): any textbook on
//    procedural noise describes it; the hash and its constants here are our own;
//  - a travelling gust: the strength is a smooth pulse train in (time - distance downwind / gust speed), so a
//    gust sweeps across the land instead of the whole scene breathing at once;
//  - blending by priority: sources are applied from the lowest priority to the highest; each either
//    replaces the wind it covers (by how deep inside it the point is) or adds to it.
//
// The maths is plain data and functions with no scene dependency, so it is unit tested without a window.
// Components (WindZone, WindDrag, WindSway, WindAmbience) only gather sources and ask this header.
#include "okay/Environment/Wind.hpp"   // WindVector(): the wind the weather makes
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace okay {

namespace windmath {

/// Smooth value noise in [-1, 1]: random values at whole-number lattice corners, blended with a smooth curve.
/// The same (x, y, z, seed) always gives the same number on every machine.
inline std::uint32_t Hash(int x, int y, int z, std::uint32_t seed) {
    std::uint32_t h = (std::uint32_t)x * 0x9E3779B1u + (std::uint32_t)y * 0x85EBCA6Bu
                    + (std::uint32_t)z * 0xC2B2AE35u + seed * 0x27D4EB2Fu + 0x165667B1u;
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 16; h *= 0x7FEB352Du;
    h ^= h >> 15;
    return h;
}
/// A lattice corner's value in [-1, 1].
inline float Corner(int x, int y, int z, std::uint32_t seed) {
    return (float)(Hash(x, y, z, seed) >> 8) * (2.0f / 16777215.0f) - 1.0f;
}
inline float Fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
/// Value noise in [-1, 1]. Not-a-number or infinite input gives 0.
inline float Noise3(float x, float y, float z, std::uint32_t seed) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return 0.0f;
    // Far away the float loses its fraction; clamp so the cell maths cannot overflow an int.
    x = std::clamp(x, -1.0e6f, 1.0e6f); y = std::clamp(y, -1.0e6f, 1.0e6f); z = std::clamp(z, -1.0e6f, 1.0e6f);
    const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    const float u = Fade(x - fx), v = Fade(y - fy), w = Fade(z - fz);
    auto L = [](float a, float b, float t) { return a + (b - a) * t; };
    const float c000 = Corner(ix, iy, iz, seed),         c100 = Corner(ix + 1, iy, iz, seed);
    const float c010 = Corner(ix, iy + 1, iz, seed),     c110 = Corner(ix + 1, iy + 1, iz, seed);
    const float c001 = Corner(ix, iy, iz + 1, seed),     c101 = Corner(ix + 1, iy, iz + 1, seed);
    const float c011 = Corner(ix, iy + 1, iz + 1, seed), c111 = Corner(ix + 1, iy + 1, iz + 1, seed);
    return L(L(L(c000, c100, u), L(c010, c110, u), v), L(L(c001, c101, u), L(c011, c111, u), v), w);
}

/// A repeating pulse in [0, 1]: smooth, one pulse per whole number of `phase`, so a gust at frequency f Hz
/// repeats exactly every 1 / f seconds. 0 = calm, 1 = full gust. The peak is narrower than the trough so
/// gusts feel like gusts and not like a gentle sine.
inline float GustPulse(float phase) {
    if (!std::isfinite(phase)) return 0.0f;
    const float s = 0.5f + 0.5f * std::sin(6.2831853f * phase - 1.5707963f);   // 0 at phase 0, 1 at phase 0.5
    return s * s * (3.0f - 2.0f * s);
}

/// A strength-over-time curve: (seconds, multiplier) keys that repeat every `length` seconds, linear between keys.
struct Curve {
    std::vector<std::pair<float, float>> keys;
    float length = 20.0f;
    bool Active() const { return !keys.empty() && length > 0.01f; }
    /// The multiplier at `t` seconds. An empty curve is 1.
    float Eval(float t) const {
        if (!Active() || !std::isfinite(t)) return 1.0f;
        std::vector<std::pair<float, float>> k = keys;
        std::stable_sort(k.begin(), k.end(), [](auto& a, auto& b) { return a.first < b.first; });
        float x = std::fmod(t, length); if (x < 0.0f) x += length;
        if (k.size() == 1) return k[0].second;
        if (x <= k.front().first) {   // between the last key (of the previous loop) and the first
            const float x0 = k.back().first - length, x1 = k.front().first;
            const float f = x1 > x0 ? (x - x0) / (x1 - x0) : 0.0f;
            return k.back().second + (k.front().second - k.back().second) * f;
        }
        for (std::size_t i = 1; i < k.size(); ++i) {
            if (x <= k[i].first) {
                const float x0 = k[i - 1].first, x1 = k[i].first;
                const float f = x1 > x0 ? (x - x0) / (x1 - x0) : 1.0f;
                return k[i - 1].second + (k[i].second - k[i - 1].second) * f;
            }
        }
        const float x0 = k.back().first, x1 = k.front().first + length;   // after the last key, back toward the first
        const float f = x1 > x0 ? (x - x0) / (x1 - x0) : 0.0f;
        return k.back().second + (k.front().second - k.back().second) * f;
    }
};

} // namespace windmath

/// One thing that blows: the global wind, or a zone. All numbers are in world units and already turned into
/// world space (see WindZone::Snapshot), so evaluating it needs no scene.
struct WindSource {
    enum class Kind { Directional = 0, Radial = 1 };
    enum class Shape { Box = 0, Sphere = 1, Cylinder = 2 };

    bool  global = false;           ///< covers the whole world (the shape is ignored)
    Kind  kind = Kind::Directional;
    int   priority = 0;             ///< higher wins; applied last
    bool  additive = false;         ///< adds to the wind below instead of replacing it where it covers
    float strength = 5.0f;          ///< metres per second
    Vec3  direction{1, 0, 0};       ///< Directional: the way the wind blows (world). Need not be unit length
    float gustAmount = 0.0f;        ///< 0..1: how far the strength swings below and above its base (1 = calm to double)
    float gustFrequency = 0.2f;     ///< gusts per second (0.2 = one every 5 s)
    float gustSpeed = 10.0f;        ///< how fast a gust travels downwind (m/s); 0 = the whole zone gusts together
    float turbulence = 0.0f;        ///< 0..1: random swirl in direction and strength, as a fraction of the strength
    float turbulenceScale = 0.15f;  ///< lattice cells per metre: smaller = bigger swirls
    std::uint32_t seed = 1;
    windmath::Curve curve;          ///< optional strength over time (empty = steady)

    // Region (ignored when `global`): centre, three unit axes and the half sizes along them.
    Shape shape = Shape::Box;
    Vec3  center{0, 0, 0};
    Vec3  axisX{1, 0, 0}, axisY{0, 1, 0}, axisZ{0, 0, 1};
    Vec3  half{2, 1.5f, 2};
    float edge = 0.3f;              ///< 0..1: how far in from the surface it takes to reach full strength (0 = hard edge)
    float falloff = 0.0f;           ///< 0..1: how much weaker it gets toward the surface (0 = same all through)

    /// The point in the region's own units: 0 at the centre, +-1 on the faces. An axis with no size gives infinity.
    Vec3 ToUnit(const Vec3& p) const {
        const Vec3 d = p - center;
        auto u = [](float v, float h) { return h > 1e-6f ? v / h : INFINITY; };   // no size on an axis: nothing is inside
        return {u(Vec3::Dot(d, axisX), half.x), u(Vec3::Dot(d, axisY), half.y), u(Vec3::Dot(d, axisZ), half.z)};
    }
    /// How deep inside: 1 at the centre, 0 on the surface, negative outside.
    float Depth(const Vec3& unit) const {
        const float ax = std::fabs(unit.x), ay = std::fabs(unit.y), az = std::fabs(unit.z);
        switch (shape) {
            case Shape::Sphere:   return 1.0f - std::sqrt(ax * ax + ay * ay + az * az);
            case Shape::Cylinder: return 1.0f - std::max(std::sqrt(ax * ax + az * az), ay);
            default:              return 1.0f - std::max(ax, std::max(ay, az));
        }
    }
    /// How much of the source reaches `p`: 0 outside, 1 from `edge` of the way in, smooth in between.
    float Coverage(const Vec3& p) const {
        if (global) return 1.0f;
        const float d = Depth(ToUnit(p));
        if (!(d > 0.0f)) return 0.0f;           // outside, on the surface, or NaN
        if (edge <= 1e-4f) return 1.0f;
        const float t = std::min(1.0f, d / edge);
        return t * t * (3.0f - 2.0f * t);
    }
    /// The wind this source alone makes at `p` at `time`, before its coverage is applied. Never NaN.
    Vec3 Raw(const Vec3& p, float time) const {
        if (!std::isfinite(time)) time = 0.0f;
        Vec3 dir;
        float depth = 1.0f;
        if (kind == Kind::Radial && !global) {
            const Vec3 r = p - center;
            const float len = r.Magnitude();
            dir = len > 1e-4f ? r / len : Vec3{0, 0, 0};
            depth = std::clamp(Depth(ToUnit(p)), 0.0f, 1.0f);
        } else {
            dir = direction.SqrMagnitude() > 1e-12f ? direction.Normalized() : Vec3{0, 0, 0};
            if (!global) depth = std::clamp(Depth(ToUnit(p)), 0.0f, 1.0f);
        }
        float s = strength * curve.Eval(time);
        // A gust travelling along the wind: the pulse moves with the air.
        float swing = 1.0f;
        if (gustAmount > 0.0f && gustFrequency > 0.0f) {
            const float along = gustSpeed > 0.01f ? Vec3::Dot(p, dir.SqrMagnitude() > 0.5f ? dir : Vec3{1, 0, 0}) / gustSpeed : 0.0f;
            const float pulse = windmath::GustPulse((time - along) * gustFrequency);
            swing = 1.0f + std::clamp(gustAmount, 0.0f, 1.0f) * (pulse * 2.0f - 1.0f);
        }
        s *= std::max(0.0f, swing);
        s *= 1.0f + (depth - 1.0f) * std::clamp(falloff, 0.0f, 1.0f);   // weaker toward the surface
        Vec3 w = dir * s;
        if (turbulence > 0.0f) {
            const float sc = turbulenceScale > 1e-4f ? turbulenceScale : 1e-4f;
            const float tx = time * 0.35f;
            const Vec3 n{windmath::Noise3(p.x * sc + tx, p.y * sc, p.z * sc, seed),
                         windmath::Noise3(p.x * sc, p.y * sc + tx, p.z * sc + 17.0f, seed + 101u) * 0.5f,   // little up and down
                         windmath::Noise3(p.x * sc - tx, p.y * sc + 31.0f, p.z * sc, seed + 202u)};
            w += n * (std::clamp(turbulence, 0.0f, 1.0f) * std::fabs(s));
        }
        if (!std::isfinite(w.x) || !std::isfinite(w.y) || !std::isfinite(w.z)) return Vec3{0, 0, 0};
        return w;
    }
};

/// The whole field.
namespace WindField {

/// Combine `sources` over `base` (the weather's wind). Sources apply from the lowest priority to the highest
/// (ties keep the list order); each one blends in by its coverage: replacing what is below, or adding to it.
inline Vec3 Evaluate(const std::vector<WindSource>& sources, const Vec3& base, const Vec3& p, float time) {
    Vec3 out = base;
    if (!std::isfinite(out.x) || !std::isfinite(out.y) || !std::isfinite(out.z)) out = Vec3{0, 0, 0};
    if (sources.empty()) return out;
    // Walk in priority order without re-sorting a caller's list on every call: the prepared list is already sorted,
    // and for a hand-made list this is a stable pass per distinct priority (few in practice).
    bool sorted = true;
    for (std::size_t i = 1; i < sources.size(); ++i) if (sources[i].priority < sources[i - 1].priority) { sorted = false; break; }
    std::vector<std::size_t> order;
    if (!sorted) {
        order.resize(sources.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return sources[a].priority < sources[b].priority; });
    }
    for (std::size_t n = 0; n < sources.size(); ++n) {
        const WindSource& s = sources[sorted ? n : order[n]];
        const float c = s.Coverage(p);
        if (!(c > 0.0f)) continue;
        const Vec3 w = s.Raw(p, time);
        if (s.additive) out += w * c;
        else out = out + (w - out) * c;
    }
    return out;
}

// ---- The scene's current field (filled by the components, once a frame) ----

/// The sources of every live wind component, sorted by priority. Set by WindField::Publish.
inline std::vector<WindSource>& Sources() { static std::vector<WindSource> s; return s; }
/// Seconds the wind has been running (advances every scene update).
inline float& Clock() { static float t = 0.0f; return t; }
/// True when at least one wind component exists.
inline bool Active() { return !Sources().empty(); }
/// Replace the published sources (sorted here by priority, ties keep the order given).
inline void Publish(std::vector<WindSource> s) {
    std::stable_sort(s.begin(), s.end(), [](const WindSource& a, const WindSource& b) { return a.priority < b.priority; });
    Sources() = std::move(s);
}

/// The wind (metres per second) at a world position now: weather wind + every wind component.
inline Vec3 At(const Vec3& p) { return Evaluate(Sources(), WindVector(), p, Clock()); }
inline Vec3 At(const Vec3& p, float time) { return Evaluate(Sources(), WindVector(), p, time); }
/// Strength (speed) at a position now.
inline float SpeedAt(const Vec3& p) { return At(p).Magnitude(); }

/// How far a thing that leans in the wind bends: a unit-ish "lean" (0 = none, 1 = a strong wind of `fullAt`
/// metres per second) so trees, flags and grass all scale the same way. Never above 1.5 so a hurricane does not
/// fold a tree in half.
inline float Lean(float speed, float fullAt = 12.0f) {
    if (!(speed > 0.0f) || !std::isfinite(speed) || fullAt <= 0.0f) return 0.0f;
    return std::min(1.5f, speed / fullAt);
}

} // namespace WindField

} // namespace okay
