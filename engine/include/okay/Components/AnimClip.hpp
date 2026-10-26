#pragma once
#include "okay/Math/Vec3.hpp"
#include <functional>
#include <string>
#include <vector>

namespace okay {

/// One keyframe of a character animation: a time (seconds) and a per-bone local
/// rotation (Euler degrees, indexed by the rig's bone order). Bones left unset are
/// zero (rest), so a keyframe only has to mention the bones it moves.
struct AnimKey {
    float time = 0.0f;
    std::vector<Vec3> pose;   // sized to the rig's bone count
    Vec3  root{0, 0, 0};      // body offset (rig units) when the clip has a root track
};

/// A named marker at a point in a clip. As the clip plays past `time`, the Character
/// fires the event (a callback + a polled queue) so gameplay can react — a footstep
/// sound on a walk cycle, a "hit" window on a punch, "spawn" on a throw. No payload
/// beyond the name; keep names single-token (no spaces) so they serialize cleanly.
struct AnimEvent {
    float time = 0.0f;
    std::string name;
};

/// A keyframed character animation clip — the easy way to make a custom animation:
/// describe a few poses over time in a tiny text format and play it by name on a
/// Character. The engine interpolates between keys each frame.
///
/// Text format (see docs/animation.md):
///   clip wave loop          # name + optional loop|once (default loop)
///   key 0.0                 # a keyframe at t=0s
///     r_uparm 0 0 -150      # <bone> <x> <y> <z>  (Euler degrees)
///   key 0.4
///     r_uparm 0 0 -150
///     r_fore  0 0 -30
///     root 0 -0.17 0        # optional: the whole body's offset (a crouch's hip drop)
/// How a clip eases between keyframes:
///  - Linear: straight component-wise lerp (the default).
///  - Smooth: ease in/out (smoothstep) for a softer, more natural motion.
///  - Step:   hold each key until the next (no interpolation — snappy / robotic).
enum class AnimInterp { Linear, Smooth, Step };

struct AnimClip {
    std::string name;
    bool loop = true;
    AnimInterp interp = AnimInterp::Linear;   ///< easing between keys
    float speed = 1.0f;                        ///< playback rate multiplier (2 = twice as fast)
    /// Loop length (seconds). When a looping clip's length is past its last key, the
    /// gap interpolates the last key back into the first, so the loop has no seam.
    /// 0 (the default) = loop at the last key, as before.
    float length = 0.0f;
    /// Movement can't interrupt this clip: a looping manual clip normally stops as
    /// soon as the character moves or jumps; an override clip keeps playing.
    bool overrideMovement = false;
    /// The keys carry a root track: each key's `root` offsets the whole body (the
    /// hips with everything on them), in the rig's units before the height scale.
    /// A walk's hip bob and a crouch's or prone's drop live here.
    bool hasRoot = false;
    std::vector<AnimKey> keys;     // kept in the order written (author them in time order)
    std::vector<AnimEvent> events; // markers fired as the clip plays (footsteps, hit windows)

    /// Length of the clip: the time of the last keyframe (0 if empty), or the loop
    /// `length` for a looping clip that sets one past its last key.
    float Duration() const {
        const float last = keys.empty() ? 0.0f : keys.back().time;
        return (loop && length > last) ? length : last;
    }

    /// Shape a 0..1 interpolation fraction by this clip's easing mode.
    float Ease(float u) const {
        if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
        switch (interp) {
            case AnimInterp::Smooth: return u * u * (3.0f - 2.0f * u);
            case AnimInterp::Step:   return 0.0f;   // hold the earlier key until the next
            case AnimInterp::Linear: default: return u;
        }
    }

    /// Register an event marker at `time` seconds (kept sorted by time).
    void AddEvent(float time, const std::string& name) {
        AnimEvent e; e.time = time; e.name = name;
        auto it = events.begin();
        while (it != events.end() && it->time < time) ++it;
        events.insert(it, e);
    }

    /// The interpolated pose at time `t` between the surrounding keyframes (each bone
    /// slerped along the short arc; clamped to the ends, or wrapping last -> first over
    /// a looping clip's `length`). Empty if the clip has no keys.
    std::vector<Vec3> Sample(float t) const;
    /// The root track at time `t`, interpolated like Sample (zero without one).
    Vec3 SampleRoot(float t) const;
    /// One bone between two Euler poses (degrees, the rig's Z-X-Y order), slerped
    /// along the short arc; the angles stay close to the plain lerp's.
    static Vec3 BlendEuler(const Vec3& a, const Vec3& b, float u);

    /// Parse one or more `clip` blocks from text. `resolveBone` maps a bone token
    /// to a rig index (or -1 to reject); `boneCount` sizes each keyframe's pose.
    /// Returns the clips found; on a malformed line returns what parsed so far and
    /// sets `err`.
    static std::vector<AnimClip> ParseAll(const std::string& text,
                                          const std::function<int(const std::string&)>& resolveBone,
                                          int boneCount,
                                          std::string* err = nullptr);

    /// Serialize clips back to the .okayanim text format (the inverse of
    /// ParseAll), so in-editor keyframing can be saved to the authored file.
    /// `boneName(i)` maps a bone index to its name; bones whose pose is (0,0,0)
    /// are omitted (ParseAll zero-initializes them), keeping the file compact.
    static std::string WriteAll(const std::vector<AnimClip>& clips,
                                const std::function<std::string(int)>& boneName);
};

} // namespace okay
