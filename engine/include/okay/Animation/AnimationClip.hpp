#pragma once
#include "okay/Animation/AnimationCurve.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include <cmath>

namespace okay {

/// A named set of animation tracks (one curve per property)
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

    /// A named moment in the clip ("footstep", "hit"). The Animator calls
    /// OnAnimationEvent(name) on its object's components when playback passes it.
    struct Event { float time = 0.0f; std::string name; };
    std::vector<Event> events;
    void AddEvent(float time, const std::string& n) {
        events.push_back({time < 0.0f ? 0.0f : time, n});
        std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.time < b.time; });
    }

    void SetCurve(const std::string& track, AnimationCurve curve) {
        if (curve.Duration() > m_length) m_length = curve.Duration();
        if (loop && !m_spanned) curve.wrap = AnimationCurve::Wrap::Loop;
        m_tracks[track] = std::move(curve);
        m_chan.valid = false;
    }

    /// Add/update a single keyframe on a track (creates the track if needed).
    /// Editors use this to "record" a value at the current time. Time-ordered
    /// adds are O(1) — importers push thousands of baked keys through here, and
    /// the old rebuild-the-whole-track-per-key made one Mixamo FBX clip take
    /// over a minute to import (the editor read as frozen).
    void AddKey(const std::string& track, float time, float value) {
        auto it = m_tracks.find(track);
        if (it == m_tracks.end()) { it = m_tracks.emplace(track, AnimationCurve{}).first; it->second.smooth = smoothKeys; m_chan.valid = false; }
        AnimationCurve& c = it->second;
        if (loop && !m_spanned) c.wrap = AnimationCurve::Wrap::Loop;
        // Appending in time order is O(1); only out-of-order keys need the replace search.
        if (c.Empty() || time > c.Keys().back().time + 1e-4f) c.AddKey(time, value);
        else c.AddOrReplaceKey(time, value, 1e-4f);
        if (c.Duration() > m_length) m_length = c.Duration();
    }
    void RemoveTrack(const std::string& track) { m_tracks.erase(track); m_chan.valid = false; Recompute(); }
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
                if (it->second.Empty()) { it = m_tracks.erase(it); m_chan.valid = false; continue; }
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
        m_spanLength = length > 0.0f ? length : 0.0f;
        m_spanned = true;
        Recompute();
    }
    /// Set the clip's length explicitly (editor "Length" field): tracks hold their
    /// first/last values instead of looping on their own spans. Keys past the new
    /// length still extend it.
    void SetLength(float length) { SpanTracks(length); }
    bool  Spanned() const { return m_spanned; }
    float SpanLength() const { return m_spanLength; }

    /// Interpolation for every track starting with `prefix`: smooth (cubic) or linear.
    void SetSmooth(const std::string& prefix, bool smooth) {
        for (auto& kv : m_tracks)
            if (kv.first.compare(0, prefix.size(), prefix) == 0) kv.second.smooth = smooth;
    }
    /// True when any track under `prefix` is smooth (false if there are none).
    bool IsSmooth(const std::string& prefix) const {
        for (const auto& kv : m_tracks)
            if (kv.first.compare(0, prefix.size(), prefix) == 0 && kv.second.smooth) return true;
        return false;
    }

    /// Keys copied out of a clip (editor copy/paste): times are relative to the
    /// earliest copied key.
    struct KeyCopy { std::string track; float time; float value; };
    /// Copy every key on tracks under `prefix` whose time is in [t0, t1].
    std::vector<KeyCopy> CopyKeys(float t0, float t1, const std::string& prefix = "") const {
        std::vector<KeyCopy> out;
        float first = 1e30f;
        for (const auto& kv : m_tracks) {
            if (kv.first.compare(0, prefix.size(), prefix) != 0) continue;
            for (const auto& k : kv.second.Keys())
                if (k.time >= t0 - 1e-4f && k.time <= t1 + 1e-4f) { out.push_back({kv.first, k.time, k.value}); first = std::min(first, k.time); }
        }
        for (auto& k : out) k.time -= first;
        return out;
    }
    /// Paste copied keys starting at `at`, replacing keys already at those times.
    void PasteKeys(const std::vector<KeyCopy>& keys, float at) {
        for (const auto& k : keys) {
            auto it = m_tracks.find(k.track);
            if (it == m_tracks.end()) {
                it = m_tracks.emplace(k.track, AnimationCurve{}).first;
                it->second.smooth = smoothKeys;
                if (loop && !m_spanned) it->second.wrap = AnimationCurve::Wrap::Loop;
                m_chan.valid = false;
            }
            it->second.AddOrReplaceKey(at + k.time, k.value, 1e-3f);
        }
        Recompute();
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

    /// The transform channels resolved to their curves once (null = no track),
    /// so sampling a pose doesn't hash track names for every bone every frame.
    struct Channels {
        const AnimationCurve* p[3] = {nullptr, nullptr, nullptr};          ///< position x/y/z
        const AnimationCurve* q[4] = {nullptr, nullptr, nullptr, nullptr}; ///< rotation qx/qy/qz/qw
        const AnimationCurve* e[3] = {nullptr, nullptr, nullptr};          ///< euler rotation x/y/z
        const AnimationCurve* s[3] = {nullptr, nullptr, nullptr};          ///< scale x/y/z
        bool anyPos = false, anyQuat = false, anyEuler = false, anyScale = false;
        /// Blend shape weight tracks ("weights.<n>"): shape index -> curve.
        std::vector<std::pair<int, const AnimationCurve*>> weights;
    };
    const Channels& Chan() const {
        if (!m_chan.valid) {
            Channels c;
            auto get = [&](const char* n) -> const AnimationCurve* {
                auto it = m_tracks.find(n);
                return it == m_tracks.end() ? nullptr : &it->second;
            };
            c.p[0] = get("position.x"); c.p[1] = get("position.y"); c.p[2] = get("position.z");
            c.q[0] = get("rotation.qx"); c.q[1] = get("rotation.qy"); c.q[2] = get("rotation.qz"); c.q[3] = get("rotation.qw");
            c.e[0] = get("rotation.x"); c.e[1] = get("rotation.y"); c.e[2] = get("rotation.z");
            c.s[0] = get("scale.x"); c.s[1] = get("scale.y"); c.s[2] = get("scale.z");
            c.anyPos = c.p[0] || c.p[1] || c.p[2];
            c.anyQuat = c.q[0] || c.q[1] || c.q[2] || c.q[3];
            c.anyEuler = c.e[0] || c.e[1] || c.e[2];
            c.anyScale = c.s[0] || c.s[1] || c.s[2];
            for (const auto& kv : m_tracks)
                if (kv.first.compare(0, 8, "weights.") == 0) {
                    int idx = std::atoi(kv.first.c_str() + 8);
                    if (idx >= 0) c.weights.push_back({idx, &kv.second});
                }
            m_chan.c = c; m_chan.valid = true;
        }
        return m_chan.c;
    }
    /// Which parts of a pose SampleTRS writes.
    enum : unsigned { kPosX = 1, kPosY = 2, kPosZ = 4, kRot = 8, kScale = 16, kAll = 31 };
    /// Sample the transform tracks at `t` into pos/rot/scl; channels without a
    /// track (or masked out) keep their incoming values. Quaternion tracks win
    /// over euler ones (imported clips key quaternions).
    void SampleTRS(float t, Vec3& pos, Quat& rot, Vec3& scl, unsigned mask = kAll) const {
        const Channels& c = Chan();
        if (c.anyPos) {
            if (c.p[0] && (mask & kPosX)) pos.x = c.p[0]->Evaluate(t);
            if (c.p[1] && (mask & kPosY)) pos.y = c.p[1]->Evaluate(t);
            if (c.p[2] && (mask & kPosZ)) pos.z = c.p[2]->Evaluate(t);
        }
        if ((mask & kScale) && c.anyScale) {
            if (c.s[0]) scl.x = c.s[0]->Evaluate(t);
            if (c.s[1]) scl.y = c.s[1]->Evaluate(t);
            if (c.s[2]) scl.z = c.s[2]->Evaluate(t);
        }
        if (!(mask & kRot)) return;
        if (c.anyQuat) {
            Quat r;
            if (SlerpQuat(c, t, r)) { rot = r; return; }
            float x = c.q[0] ? c.q[0]->Evaluate(t) : 0.0f, y = c.q[1] ? c.q[1]->Evaluate(t) : 0.0f;
            float z = c.q[2] ? c.q[2]->Evaluate(t) : 0.0f, w = c.q[3] ? c.q[3]->Evaluate(t) : 0.0f;
            float ln = std::sqrt(x * x + y * y + z * z + w * w);
            if (ln < 1e-8f) { w = 1.0f; ln = 1.0f; }
            rot = Quat{x / ln, y / ln, z / ln, w / ln};
        } else if (c.anyEuler) {
            rot = Quat::Euler(c.e[0] ? c.e[0]->Evaluate(t) : 0.0f, c.e[1] ? c.e[1]->Evaluate(t) : 0.0f,
                              c.e[2] ? c.e[2]->Evaluate(t) : 0.0f);
        }
    }

    /// Keyframe reduction on every linear track (see AnimationCurve::Reduce).
    /// Position tolerance scales with how far the track travels (so centimetre
    /// rigs aren't over- or under-reduced); scale uses `sclTol`. A linear
    /// quaternion rotation (rotation.qx/qy/qz/qw with shared key times) is
    /// reduced as one rotation: `rotTol` is then the largest allowed ANGLE in
    /// radians between the slerp of the kept keys and every dropped key. Other
    /// rotation tracks (euler, or quaternion curves that don't line up) use
    /// `rotTol` per component as before. Returns keys removed.
    std::size_t Reduce(float posTol = 0.0005f, float rotTol = 0.0005f, float sclTol = 0.0005f) {
        std::size_t removed = 0;
        bool quatDone = false;
        if (LinearQuatTracks()) { removed += ReduceQuat(rotTol); quatDone = true; }
        for (auto& kv : m_tracks) {
            AnimationCurve& c = kv.second;
            if (c.smooth || c.Count() < 3) continue;
            if (quatDone && kv.first.rfind("rotation.q", 0) == 0) continue;
            float tol = sclTol;
            if (kv.first.rfind("rotation.", 0) == 0) tol = rotTol;
            else if (kv.first.rfind("position.", 0) == 0) {
                float lo = 1e30f, hi = -1e30f;
                for (const auto& k : c.Keys()) { lo = std::fmin(lo, k.value); hi = std::fmax(hi, k.value); }
                float mag = 0.0f;
                for (const auto& k : c.Keys()) mag = std::fmax(mag, std::fabs(k.value));
                tol = posTol * std::fmax(1.0f, std::fmax(hi - lo, mag * 0.1f));
            }
            removed += c.Reduce(tol);
        }
        return removed;
    }
    /// Total keys across every track.
    std::size_t KeyCount() const { std::size_t n = 0; for (const auto& kv : m_tracks) n += kv.second.Count(); return n; }

    // ---- Clip surgery (editor tools) ----
    /// Reverse the clip in time (plays backwards). Length is preserved.
    void Reverse() {
        for (auto& kv : m_tracks) {
            AnimationCurve nc; nc.wrap = kv.second.wrap; nc.smooth = kv.second.smooth;
            for (const auto& k : kv.second.Keys()) nc.AddKey(m_length - k.time, k.value);
            kv.second = std::move(nc);
        }
        for (auto& e : events) e.time = m_length - e.time;
        std::reverse(events.begin(), events.end());
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
        std::vector<Event> kept;
        for (const auto& e : events) if (e.time >= t0 && e.time <= t1) kept.push_back({e.time - t0, e.name});
        events = std::move(kept);
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
        for (auto& e : events) e.time *= factor;
        Recompute();
    }

private:
    /// Unit quaternion from four raw key values (zero-length -> identity).
    static Quat KeyQuat(const AnimationCurve* const q[4], std::size_t k) {
        float x = q[0]->Keys()[k].value, y = q[1]->Keys()[k].value, z = q[2]->Keys()[k].value, w = q[3]->Keys()[k].value;
        float n = std::sqrt(x * x + y * y + z * z + w * w);
        if (n < 1e-8f) return Quat{0.0f, 0.0f, 0.0f, 1.0f};
        return Quat{x / n, y / n, z / n, w / n};
    }
    /// Spherical interpolation from a to b by u along the shorter arc. Written
    /// from the definition: with cos(theta) = a.b (b negated first when the dot
    /// is negative, since q and -q are the same rotation), the point at angle
    /// u*theta on the great circle is a*sin((1-u)theta)/sin(theta) +
    /// b*sin(u*theta)/sin(theta). Very small arcs fall back to a normalised
    /// lerp, which is exact to float precision there.
    static Quat SlerpShort(const Quat& a, Quat b, float u) {
        double d = (double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z + (double)a.w * b.w;
        if (d < 0.0) { b = Quat{-b.x, -b.y, -b.z, -b.w}; d = -d; }
        double wa = 1.0 - u, wb = u;
        if (d < 0.99999) {
            double th = std::acos(d > 1.0 ? 1.0 : d), s = std::sin(th);
            wa = std::sin((1.0 - u) * th) / s; wb = std::sin(u * th) / s;
        }
        double x = wa * a.x + wb * b.x, y = wa * a.y + wb * b.y, z = wa * a.z + wb * b.z, w = wa * a.w + wb * b.w;
        double n = std::sqrt(x * x + y * y + z * z + w * w);
        if (n < 1e-12) return a;
        return Quat{(float)(x / n), (float)(y / n), (float)(z / n), (float)(w / n)};
    }
    /// The four quaternion curves when they can be slerped as one rotation:
    /// all present, all linear, same key count and the same key times.
    /// Null otherwise (the caller keeps the per-component path).
    const AnimationCurve* const* LinearQuatTracks() const {
        const Channels& c = Chan();
        for (int k = 0; k < 4; ++k) if (!c.q[k] || c.q[k]->smooth) return nullptr;
        const std::size_t n = c.q[3]->Count();
        for (int k = 0; k < 3; ++k) if (c.q[k]->Count() != n) return nullptr;
        for (std::size_t i = 0; i < n; ++i) {
            float t = c.q[3]->Keys()[i].time;
            for (int k = 0; k < 3; ++k) if (std::fabs(c.q[k]->Keys()[i].time - t) > 1e-5f) return nullptr;
        }
        return c.q;
    }
    /// Linear quaternion keys: slerp the bracketing pair (glTF LINEAR rotation
    /// semantics). Smooth keys stay on the per-component monotone cubic +
    /// renormalise path, which is C1 through the keys (what "smooth" promises);
    /// changing it would alter every hand-authored clip already saved.
    bool SlerpQuat(const Channels& c, float t, Quat& out) const {
        for (int k = 0; k < 4; ++k) if (!c.q[k] || c.q[k]->smooth) return false;
        std::size_t i; float u;
        if (!c.q[3]->Locate(t, i, u)) return false;
        const std::size_t n = c.q[3]->Count();
        for (int k = 0; k < 3; ++k) {
            const auto& ks = c.q[k]->Keys();
            if (ks.size() != n || std::fabs(ks[i].time - c.q[3]->Keys()[i].time) > 1e-5f ||
                std::fabs(ks[i + 1].time - c.q[3]->Keys()[i + 1].time) > 1e-5f) return false;
        }
        out = SlerpShort(KeyQuat(c.q, i), KeyQuat(c.q, i + 1), u);
        return true;
    }
    /// Angle in radians between two unit quaternions (as rotations).
    static float QuatAngle(const Quat& a, const Quat& b) {
        double d = std::fabs((double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z + (double)a.w * b.w);
        return (float)(2.0 * std::acos(d > 1.0 ? 1.0 : d));
    }
    /// Greedy reduction of the linear quaternion rotation: extend each kept
    /// segment while the slerp between its ends stays within `tolRad` of every
    /// original key it skips. First and last keys stay; all four curves keep
    /// identical key times.
    std::size_t ReduceQuat(float tolRad) {
        const AnimationCurve* const* q = LinearQuatTracks();
        if (!q || tolRad <= 0.0f) return 0;
        const std::size_t n = q[3]->Count();
        if (n < 3) return 0;
        std::vector<Quat> qs(n); std::vector<float> ts(n);
        for (std::size_t i = 0; i < n; ++i) { qs[i] = KeyQuat(q, i); ts[i] = q[3]->Keys()[i].time; }
        std::vector<std::size_t> keep{0};
        std::size_t a = 0;
        while (a + 1 < n) {
            std::size_t b = a + 1;
            while (b + 1 < n) {
                std::size_t c = b + 1;
                float span = ts[c] - ts[a];
                bool fits = span > 0.0f;
                for (std::size_t i = a + 1; i < c && fits; ++i)
                    if (QuatAngle(SlerpShort(qs[a], qs[c], (ts[i] - ts[a]) / span), qs[i]) > tolRad) fits = false;
                if (!fits) break;
                b = c;
            }
            keep.push_back(b);
            a = b;
        }
        if (keep.size() == n) return 0;
        static const char* names[4] = {"rotation.qx", "rotation.qy", "rotation.qz", "rotation.qw"};
        for (const char* nm : names) {
            AnimationCurve& src = m_tracks[nm];
            AnimationCurve nc; nc.wrap = src.wrap; nc.smooth = src.smooth;
            for (std::size_t k : keep) nc.AddKey(src.Keys()[k].time, src.Keys()[k].value);   // raw values: file data unchanged
            src = std::move(nc);
        }
        m_chan.valid = false;
        return n - keep.size();
    }

    // Copies of a clip must rebuild their own channel table (the pointers point
    // into the source's track map).
    struct ChanCache {
        Channels c; bool valid = false;
        ChanCache() = default;
        ChanCache(const ChanCache&) {}
        ChanCache& operator=(const ChanCache&) { valid = false; return *this; }
    };
    std::unordered_map<std::string, AnimationCurve> m_tracks;
    mutable ChanCache m_chan;
    float m_length = 0.0f;
    float m_spanLength = 0.0f;   // SpanTracks: the whole clip's length
    bool  m_spanned = false;     // tracks are clamped to one clip-wide span
};

} // namespace okay
