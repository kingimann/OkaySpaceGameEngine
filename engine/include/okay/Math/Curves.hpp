#pragma once
// Parametric curve helpers: Bezier (quadratic / cubic), Catmull-Rom, Hermite, each with
// its tangent (first derivative w.r.t. t), plus arc-length measurement and a lookup
// table for constant-speed travel along any of them.
//
// All functions are templates over the point type V: float, Vec2 or Vec3 (anything with
// V + V, V - V and V * float). `t` is NOT clamped (it extrapolates) except in
// ArcLengthTable::ParamAtLength.
#include "okay/Math/Mathf.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include <cstddef>
#include <vector>

namespace okay {

struct Curves {
    // ---- Bezier ----
    template <class V> static V QuadBezier(const V& p0, const V& p1, const V& p2, float t) {
        float u = 1.0f - t;
        return p0 * (u * u) + p1 * (2.0f * u * t) + p2 * (t * t);
    }
    template <class V> static V QuadBezierTangent(const V& p0, const V& p1, const V& p2, float t) {
        float u = 1.0f - t;
        return (p1 - p0) * (2.0f * u) + (p2 - p1) * (2.0f * t);
    }
    template <class V> static V CubicBezier(const V& p0, const V& p1, const V& p2, const V& p3, float t) {
        float u = 1.0f - t;
        return p0 * (u * u * u) + p1 * (3.0f * u * u * t) + p2 * (3.0f * u * t * t) + p3 * (t * t * t);
    }
    template <class V> static V CubicBezierTangent(const V& p0, const V& p1, const V& p2, const V& p3, float t) {
        float u = 1.0f - t;
        return (p1 - p0) * (3.0f * u * u) + (p2 - p1) * (6.0f * u * t) + (p3 - p2) * (3.0f * t * t);
    }

    // ---- Hermite: endpoints p0, p1 with tangents m0, m1 (derivative scale per unit t) ----
    template <class V> static V Hermite(const V& p0, const V& m0, const V& p1, const V& m1, float t) {
        float t2 = t * t, t3 = t2 * t;
        return p0 * (2 * t3 - 3 * t2 + 1) + m0 * (t3 - 2 * t2 + t) + p1 * (-2 * t3 + 3 * t2) + m1 * (t3 - t2);
    }
    template <class V> static V HermiteTangent(const V& p0, const V& m0, const V& p1, const V& m1, float t) {
        float t2 = t * t;
        return p0 * (6 * t2 - 6 * t) + m0 * (3 * t2 - 4 * t + 1) + p1 * (-6 * t2 + 6 * t) + m1 * (3 * t2 - 2 * t);
    }

    // ---- Catmull-Rom: passes through p1 (t=0) and p2 (t=1); p0 / p3 shape the tangents.
    // `tension` 0.5 = standard Catmull-Rom, 0 = straight lines (zero tangents).
    template <class V> static V CatmullRom(const V& p0, const V& p1, const V& p2, const V& p3, float t, float tension = 0.5f) {
        return Hermite(p1, (p2 - p0) * tension, p2, (p3 - p1) * tension, t);
    }
    template <class V> static V CatmullRomTangent(const V& p0, const V& p1, const V& p2, const V& p3, float t, float tension = 0.5f) {
        return HermiteTangent(p1, (p2 - p0) * tension, p2, (p3 - p1) * tension, t);
    }

    // ---- Arc length ----
    /// Length of the polyline through `segments` samples of f(t) on [t0, t1]
    /// (f: float -> V). Converges from below; 32 is plenty for smooth curves.
    template <class F> static float ArcLength(F&& f, float t0 = 0.0f, float t1 = 1.0f, int segments = 32) {
        if (segments < 1) segments = 1;
        float len = 0.0f;
        auto prev = f(t0);
        for (int i = 1; i <= segments; ++i) {
            auto cur = f(t0 + (t1 - t0) * ((float)i / (float)segments));
            len += Dist(prev, cur);
            prev = cur;
        }
        return len;
    }
    template <class V> static float CubicBezierLength(const V& p0, const V& p1, const V& p2, const V& p3, int segments = 32) {
        return ArcLength([&](float t) { return CubicBezier(p0, p1, p2, p3, t); }, 0.0f, 1.0f, segments);
    }
    template <class V> static float QuadBezierLength(const V& p0, const V& p1, const V& p2, int segments = 32) {
        return ArcLength([&](float t) { return QuadBezier(p0, p1, p2, t); }, 0.0f, 1.0f, segments);
    }

    /// Cumulative-length table over [0, 1] for constant-speed sampling:
    ///   ArcLengthTable tab; tab.Build([&](float t){ return CubicBezier(a,b,c,d,t); });
    ///   float t = tab.ParamAtLength(tab.TotalLength() * 0.5f);   // t of the midpoint by distance
    class ArcLengthTable {
    public:
        template <class F> void Build(F&& f, int segments = 64) {
            if (segments < 1) segments = 1;
            m_len.assign((std::size_t)segments + 1, 0.0f);
            auto prev = f(0.0f);
            for (int i = 1; i <= segments; ++i) {
                auto cur = f((float)i / (float)segments);
                m_len[(std::size_t)i] = m_len[(std::size_t)i - 1] + Dist(prev, cur);
                prev = cur;
            }
        }
        bool Empty() const { return m_len.empty(); }
        float TotalLength() const { return m_len.empty() ? 0.0f : m_len.back(); }
        /// Curve parameter t in [0, 1] whose distance from the start is `length`
        /// (clamped to [0, TotalLength]).
        float ParamAtLength(float length) const {
            if (m_len.size() < 2) return 0.0f;
            float total = m_len.back();
            if (!(length > 0.0f) || total <= 0.0f) return 0.0f;
            if (length >= total) return 1.0f;
            std::size_t lo = 0, hi = m_len.size() - 1;
            while (hi - lo > 1) {
                std::size_t mid = (lo + hi) / 2;
                if (m_len[mid] <= length) lo = mid; else hi = mid;
            }
            float span = m_len[hi] - m_len[lo];
            float f = span > 0.0f ? (length - m_len[lo]) / span : 0.0f;
            return ((float)lo + f) / (float)(m_len.size() - 1);
        }
        /// Distance from the start at parameter t (clamped to [0, 1]).
        float LengthAtParam(float t) const {
            if (m_len.size() < 2) return 0.0f;
            t = Mathf::Clamp01(t) * (float)(m_len.size() - 1);
            std::size_t i = (std::size_t)t;
            if (i >= m_len.size() - 1) return m_len.back();
            return m_len[i] + (m_len[i + 1] - m_len[i]) * (t - (float)i);
        }
    private:
        std::vector<float> m_len;
    };

private:
    static float Dist(float a, float b) { return Mathf::Abs(a - b); }
    static float Dist(const Vec2& a, const Vec2& b) { return (a - b).Magnitude(); }
    static float Dist(const Vec3& a, const Vec3& b) { return (a - b).Magnitude(); }
};

} // namespace okay
