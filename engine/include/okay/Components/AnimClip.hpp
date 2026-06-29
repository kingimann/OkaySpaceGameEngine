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
struct AnimClip {
    std::string name;
    bool loop = true;
    std::vector<AnimKey> keys;     // kept in the order written (author them in time order)
    std::vector<AnimEvent> events; // markers fired as the clip plays (footsteps, hit windows)

    /// Length of the clip (time of the last keyframe), or 0 if empty.
    float Duration() const { return keys.empty() ? 0.0f : keys.back().time; }

    /// The interpolated pose at time `t` (component-wise lerp between the
    /// surrounding keyframes; clamped to the ends). Empty if the clip has no keys.
    std::vector<Vec3> Sample(float t) const;

    /// Parse one or more `clip` blocks from text. `resolveBone` maps a bone token
    /// to a rig index (or -1 to reject); `boneCount` sizes each keyframe's pose.
    /// Returns the clips found; on a malformed line returns what parsed so far and
    /// sets `err`.
    static std::vector<AnimClip> ParseAll(const std::string& text,
                                          const std::function<int(const std::string&)>& resolveBone,
                                          int boneCount,
                                          std::string* err = nullptr);
};

} // namespace okay
