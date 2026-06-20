#pragma once
#include "okay/Math/Mathf.hpp"
#include <algorithm>
#include <vector>

namespace okay {

/// A single keyframe (time in seconds, value).
struct Keyframe {
    float time = 0.0f;
    float value = 0.0f;
};

/// A piecewise curve of keyframes evaluated over time, like Unity's
/// `AnimationCurve`. Keys are kept sorted; evaluation interpolates between them.
class AnimationCurve {
public:
    enum class Wrap { Clamp, Loop, PingPong };
    Wrap wrap = Wrap::Clamp;
    bool smooth = true; // smoothstep between keys vs linear

    void AddKey(float time, float value) {
        m_keys.push_back({time, value});
        std::sort(m_keys.begin(), m_keys.end(),
                  [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
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
        // Locate the segment.
        for (std::size_t i = 0; i + 1 < m_keys.size(); ++i) {
            const Keyframe& a = m_keys[i];
            const Keyframe& b = m_keys[i + 1];
            if (t <= b.time) {
                float seg = b.time - a.time;
                float u = seg > Mathf::Epsilon ? (t - a.time) / seg : 0.0f;
                if (smooth) u = u * u * (3.0f - 2.0f * u);
                return a.value + (b.value - a.value) * u;
            }
        }
        return m_keys.back().value;
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
