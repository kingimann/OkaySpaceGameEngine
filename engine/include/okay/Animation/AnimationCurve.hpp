#pragma once
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace okay {

/// A single keyframe (time in seconds, value).
struct Keyframe {
    float time = 0.0f;
    float value = 0.0f;
};

/// A piecewise curve of keyframes evaluated over time
/// `AnimationCurve`. Keys are kept sorted; evaluation interpolates between them.
class AnimationCurve {
public:
    enum class Wrap { Clamp, Loop, PingPong };
    Wrap wrap = Wrap::Clamp;
    bool smooth = true; // smooth (monotone cubic through the keys) vs linear

    void AddKey(float time, float value) {
        // Appending in time order (how importers and bakers add keys) is O(1);
        // out-of-order keys insert at their sorted spot. The old push_back +
        // full re-sort per key was O(K^2 log K) across a track — importing one
        // Mixamo clip (~700 baked keys x ~65 bones x 7 tracks) froze the editor
        // for over a minute.
        if (m_keys.empty() || time >= m_keys.back().time) {
            m_keys.push_back({time, value});
            return;
        }
        auto it = std::lower_bound(m_keys.begin(), m_keys.end(), time,
                                   [](const Keyframe& k, float t) { return k.time < t; });
        m_keys.insert(it, {time, value});
    }
    /// Add a key, replacing any existing key at (nearly) the same time — what
    /// editor "record value at playhead" tools want. O(log n) search.
    void AddOrReplaceKey(float time, float value, float eps = 1e-4f) {
        auto it = std::lower_bound(m_keys.begin(), m_keys.end(), time - eps,
                                   [](const Keyframe& k, float t) { return k.time < t; });
        if (it != m_keys.end() && std::abs(it->time - time) < eps) { it->time = time; it->value = value; return; }
        m_keys.insert(it, {time, value});
    }
    void Clear() { m_keys.clear(); }
    bool Empty() const { return m_keys.empty(); }
    const std::vector<Keyframe>& Keys() const { return m_keys; }
    std::size_t Count() const { return m_keys.size(); }
    /// Remove the key at index i (tools/editors). Out-of-range is ignored.
    void RemoveKey(std::size_t i) { if (i < m_keys.size()) m_keys.erase(m_keys.begin() + i); }

    float Duration() const { return m_keys.empty() ? 0.0f : m_keys.back().time; }

    float Evaluate(float t) const {
        if (m_keys.empty()) return 0.0f;
        if (m_keys.size() == 1) return m_keys.front().value;

        float start = m_keys.front().time, end = m_keys.back().time;
        float span = end - start;
        if (span > Mathf::Epsilon) {
            switch (wrap) {
                case Wrap::Clamp:    t = Mathf::Clamp(t, start, end); break;
                case Wrap::Loop:     t = start + Mathf::Repeat(t - start, span); break;
                case Wrap::PingPong: t = start + Mathf::PingPong(t - start, span); break;
            }
        }
        // Locate the segment by binary search — baked tracks (Mixamo etc.) carry
        // hundreds of keys and get evaluated hundreds of times per frame, so the
        // old linear scan was a real chunk of the frame on animated characters.
        auto it = std::lower_bound(m_keys.begin() + 1, m_keys.end(), t,
                                   [](const Keyframe& k, float tv) { return k.time < tv; });
        if (it == m_keys.end()) return m_keys.back().value;
        const Keyframe& a = *(it - 1);
        const Keyframe& b = *it;
        float seg = b.time - a.time;
        float u = seg > Mathf::Epsilon ? (t - a.time) / seg : 0.0f;
        if (!smooth) return a.value + (b.value - a.value) * u;
        // Monotone cubic Hermite (Fritsch-Butland tangents): the curve passes
        // smoothly THROUGH every key instead of easing to a stop at each one (the
        // old smoothstep-per-segment made dense mocap stutter 30 times a second),
        // never overshoots, and flattens only at real peaks. With two keys the
        // end tangents are 0, i.e. exactly the old ease-in/ease-out.
        const std::size_t i = (std::size_t)(it - m_keys.begin()) - 1;   // a = key i, b = key i+1
        const float s = seg > Mathf::Epsilon ? (b.value - a.value) / seg : 0.0f;
        auto tangent = [&](std::size_t k, float sRight, bool rightSide) -> float {
            // Slope at key k from its two neighbouring secants.
            if (rightSide ? (k + 1 >= m_keys.size()) : (k == 0)) return 0.0f;
            float other;
            if (rightSide) {
                const Keyframe& n = m_keys[k + 1];
                const float h = n.time - m_keys[k].time;
                other = h > Mathf::Epsilon ? (n.value - m_keys[k].value) / h : 0.0f;
            } else {
                const Keyframe& p0 = m_keys[k - 1];
                const float h = m_keys[k].time - p0.time;
                other = h > Mathf::Epsilon ? (m_keys[k].value - p0.value) / h : 0.0f;
            }
            if (sRight * other <= 0.0f) return 0.0f;       // a peak/valley (or flat): no overshoot
            return 2.0f * sRight * other / (sRight + other); // harmonic mean
        };
        const float m0 = tangent(i, s, false);       // at a: secants (i-1,i) and (i,i+1)
        const float m1 = tangent(i + 1, s, true);    // at b: secants (i,i+1) and (i+1,i+2)
        const float u2 = u * u, u3 = u2 * u;
        const float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f, h10 = u3 - 2.0f * u2 + u;
        const float h01 = -2.0f * u3 + 3.0f * u2,      h11 = u3 - u2;
        return h00 * a.value + h10 * seg * m0 + h01 * b.value + h11 * seg * m1;
    }

    /// Keyframe reduction for LINEAR curves: drop every key that the straight
    /// line between its kept neighbours reproduces within `tolerance` (checked
    /// against all the original keys it would replace, so error never piles up).
    /// First and last keys always stay. Returns how many keys were removed.
    /// Smooth (cubic) curves are left alone: removing keys would change their shape.
    std::size_t Reduce(float tolerance) {
        if (smooth || m_keys.size() < 3 || tolerance <= 0.0f) return 0;
        std::vector<Keyframe> out;
        out.reserve(m_keys.size());
        std::size_t a = 0;
        out.push_back(m_keys[0]);
        while (a + 1 < m_keys.size()) {
            // Extend the segment a -> b as far as every key in between stays on the line.
            std::size_t b = a + 1;
            while (b + 1 < m_keys.size()) {
                std::size_t c = b + 1;
                const Keyframe& ka = m_keys[a];
                const Keyframe& kc = m_keys[c];
                float span = kc.time - ka.time;
                bool fits = span > 0.0f;
                for (std::size_t i = a + 1; i < c && fits; ++i) {
                    float u = (m_keys[i].time - ka.time) / span;
                    float v = ka.value + (kc.value - ka.value) * u;
                    if (std::fabs(v - m_keys[i].value) > tolerance) fits = false;
                }
                if (!fits) break;
                b = c;
            }
            out.push_back(m_keys[b]);
            a = b;
        }
        std::size_t removed = m_keys.size() - out.size();
        m_keys.swap(out);
        return removed;
    }

    /// Convenience: a curve from a to b over `duration` seconds.
    static AnimationCurve Linear(float a, float b, float duration) {
        AnimationCurve c;
        c.smooth = false;
        c.AddKey(0.0f, a);
        c.AddKey(duration, b);
        return c;
    }

private:
    std::vector<Keyframe> m_keys;
};

} // namespace okay
