#pragma once
#include "okay/Animation/AnimationCurve.hpp"
#include <string>
#include <unordered_map>
#include <cmath>

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

    /// Add/update a single keyframe on a track (creates the track if needed).
    /// Editors use this to "record" a value at the current time.
    void AddKey(const std::string& track, float time, float value) {
        AnimationCurve& c = m_tracks[track];
        if (loop) c.wrap = AnimationCurve::Wrap::Loop;
        // Replace an existing key at (nearly) the same time, else add a new one.
        bool replaced = false;
        AnimationCurve nc; nc.wrap = c.wrap; nc.smooth = c.smooth;
        for (const auto& k : c.Keys()) {
            if (std::abs(k.time - time) < 1e-4f) { nc.AddKey(time, value); replaced = true; }
            else nc.AddKey(k.time, k.value);
        }
        if (!replaced) nc.AddKey(time, value);
        c = std::move(nc);
        if (c.Duration() > m_length) m_length = c.Duration();
    }
    void RemoveTrack(const std::string& track) { m_tracks.erase(track); Recompute(); }
    AnimationCurve* Track(const std::string& track) {
        auto it = m_tracks.find(track); return it == m_tracks.end() ? nullptr : &it->second;
    }
    void Recompute() {
        m_length = 0.0f;
        for (auto& kv : m_tracks) if (kv.second.Duration() > m_length) m_length = kv.second.Duration();
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
