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
    /// Interpolation for tracks AddKey creates: smooth (monotone cubic) for
    /// hand-placed keys; importers set false so baked/sampled data (FBX, glTF
    /// LINEAR) plays back exactly as authored.
    bool  smoothKeys = true;

    void SetCurve(const std::string& track, AnimationCurve curve) {
        if (curve.Duration() > m_length) m_length = curve.Duration();
        if (loop && !m_spanned) curve.wrap = AnimationCurve::Wrap::Loop;
        m_tracks[track] = std::move(curve);
    }

    /// Add/update a single keyframe on a track (creates the track if needed).
    /// Editors use this to "record" a value at the current time. Time-ordered
    /// adds are O(1) — importers push thousands of baked keys through here, and
    /// the old rebuild-the-whole-track-per-key made one Mixamo FBX clip take
    /// over a minute to import (the editor read as frozen).
    void AddKey(const std::string& track, float time, float value) {
        auto it = m_tracks.find(track);
        if (it == m_tracks.end()) { it = m_tracks.emplace(track, AnimationCurve{}).first; it->second.smooth = smoothKeys; }
        AnimationCurve& c = it->second;
        if (loop && !m_spanned) c.wrap = AnimationCurve::Wrap::Loop;
        // Appending in time order is O(1); only out-of-order keys need the replace search.
        if (c.Empty() || time > c.Keys().back().time + 1e-4f) c.AddKey(time, value);
        else c.AddOrReplaceKey(time, value, 1e-4f);
        if (c.Duration() > m_length) m_length = c.Duration();
    }
    void RemoveTrack(const std::string& track) { m_tracks.erase(track); Recompute(); }
    /// Remove the key nearest `time` (within eps) from every track whose name
    /// starts with `prefix` ("" = all tracks). Tracks left empty are dropped.
    /// Returns how many keys were removed.
    int RemoveKeysAt(float time, const std::string& prefix = "", float eps = 1e-3f) {
        int removed = 0;
        for (auto it = m_tracks.begin(); it != m_tracks.end();) {
            if (it->first.compare(0, prefix.size(), prefix) == 0) {
                const auto& ks = it->second.Keys();
                for (std::size_t i = 0; i < ks.size(); ++i)
                    if (std::fabs(ks[i].time - time) < eps) { it->second.RemoveKey(i); ++removed; break; }
                if (it->second.Empty()) { it = m_tracks.erase(it); continue; }
            }
            ++it;
        }
        if (removed) Recompute();
        return removed;
    }
    /// Move the key at `from` (within eps) to `to` on every track matching
    /// `prefix`, replacing any key already at `to`. Returns keys moved.
    int MoveKeysAt(float from, float to, const std::string& prefix = "", float eps = 1e-3f) {
        if (to < 0.0f) to = 0.0f;
        int moved = 0;
        for (auto& kv : m_tracks) {
            if (kv.first.compare(0, prefix.size(), prefix) != 0) continue;
            const auto& ks = kv.second.Keys();
            for (std::size_t i = 0; i < ks.size(); ++i)
                if (std::fabs(ks[i].time - from) < eps) {
                    float v = ks[i].value;
                    kv.second.RemoveKey(i);
                    kv.second.AddOrReplaceKey(to, v, eps);
                    ++moved;
                    break;
                }
        }
        if (moved) Recompute();
        return moved;
    }
    /// Make the clip `length` long with every track CLAMPED (held at its ends):
    /// the player wraps time over the whole clip, so tracks must not loop on
    /// their own shorter spans.
    void SpanTracks(float length) {
        for (auto& kv : m_tracks) kv.second.wrap = AnimationCurve::Wrap::Clamp;
        m_spanLength = length;
        if (length > m_length) m_length = length;
        m_spanned = true;
    }
    AnimationCurve* Track(const std::string& track) {
        auto it = m_tracks.find(track); return it == m_tracks.end() ? nullptr : &it->second;
    }
    void Recompute() {
        m_length = m_spanLength;   // a spanned clip keeps its full length (e.g. after RemoveTrack)
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

    // ---- Clip surgery (editor tools) ----
    /// Reverse the clip in time (plays backwards). Length is preserved.
    void Reverse() {
        for (auto& kv : m_tracks) {
            AnimationCurve nc; nc.wrap = kv.second.wrap; nc.smooth = kv.second.smooth;
            for (const auto& k : kv.second.Keys()) nc.AddKey(m_length - k.time, k.value);
            kv.second = std::move(nc);
        }
        Recompute();
    }

    /// Keep only [t0, t1] and shift it to start at 0 (cut a sub-clip out of a
    /// longer take). The cut boundaries are sampled so the ends hold their
    /// exact values instead of snapping to the nearest surviving key.
    void Trim(float t0, float t1) {
        if (t1 <= t0) return;
        m_spanLength = 0.0f;   // the cut defines the new length
        for (auto& kv : m_tracks) {
            AnimationCurve& c = kv.second;
            AnimationCurve nc; nc.wrap = c.wrap; nc.smooth = c.smooth;
            nc.AddKey(0.0f, c.Evaluate(t0));
            for (const auto& k : c.Keys())
                if (k.time > t0 + 1e-4f && k.time < t1 - 1e-4f) nc.AddKey(k.time - t0, k.value);
            nc.AddKey(t1 - t0, c.Evaluate(t1));
            kv.second = std::move(nc);
        }
        Recompute();
    }

    /// Stretch/compress the clip in time: factor 2 = twice as long (half speed),
    /// 0.5 = twice as fast. Key values are untouched - only their times move.
    void ScaleTime(float factor) {
        m_spanLength *= factor > 0.0f ? factor : 1.0f;
        if (factor <= 1e-4f) return;
        for (auto& kv : m_tracks) {
            AnimationCurve nc; nc.wrap = kv.second.wrap; nc.smooth = kv.second.smooth;
            for (const auto& k : kv.second.Keys()) nc.AddKey(k.time * factor, k.value);
            kv.second = std::move(nc);
        }
        Recompute();
    }

private:
    std::unordered_map<std::string, AnimationCurve> m_tracks;
    float m_length = 0.0f;
    float m_spanLength = 0.0f;   // SpanTracks: the whole clip's length
    bool  m_spanned = false;     // tracks are clamped to one clip-wide span
};

} // namespace okay
