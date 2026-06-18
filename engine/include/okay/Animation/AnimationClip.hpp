#pragma once
#include "okay/Animation/AnimationCurve.hpp"
#include <string>
#include <unordered_map>

namespace okay {

/// A named set of animation tracks (one curve per property), like Unity's
/// `AnimationClip`. Track names the Animator understands out of the box:
/// "position.x/y/z", "rotation.z", "scale.x/y/z".
class AnimationClip {
public:
    std::string name = "Clip";
    bool  loop = true;

    void SetCurve(const std::string& track, AnimationCurve curve) {
        if (curve.Duration() > m_length) m_length = curve.Duration();
        if (loop) curve.wrap = AnimationCurve::Wrap::Loop;
        m_tracks[track] = std::move(curve);
    }

    float Length() const { return m_length; }
    bool  HasTrack(const std::string& track) const { return m_tracks.count(track) != 0; }

    /// Evaluate a track at time t; sets `found` to whether the track exists.
    float Evaluate(const std::string& track, float t, bool& found) const {
        auto it = m_tracks.find(track);
        found = it != m_tracks.end();
        return found ? it->second.Evaluate(t) : 0.0f;
    }

    const std::unordered_map<std::string, AnimationCurve>& Tracks() const { return m_tracks; }

private:
    std::unordered_map<std::string, AnimationCurve> m_tracks;
    float m_length = 0.0f;
};

} // namespace okay
