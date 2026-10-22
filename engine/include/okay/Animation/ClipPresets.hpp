#pragma once
#include "okay/Animation/AnimationClip.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include <cmath>

namespace okay {

/// One-click starter animations for the editor's Animation tab. Each preset
/// writes keys around the object's CURRENT pose (so a bob starts where the
/// object already is) and replaces only the tracks it animates.
enum class ClipPreset { Bob, Hover, Spin, Spin2D, Pulse, Shake, Sway, Bounce, PopIn, SlideIn, Count };

inline const char* ClipPresetName(ClipPreset p) {
    switch (p) {
    case ClipPreset::Bob:     return "Bob";
    case ClipPreset::Hover:   return "Hover";
    case ClipPreset::Spin:    return "Spin";
    case ClipPreset::Spin2D:  return "Spin (2D)";
    case ClipPreset::Pulse:   return "Pulse";
    case ClipPreset::Shake:   return "Shake";
    case ClipPreset::Sway:    return "Sway";
    case ClipPreset::Bounce:  return "Bounce";
    case ClipPreset::PopIn:   return "Pop In";
    case ClipPreset::SlideIn: return "Slide In";
    default:                  return "?";
    }
}
inline const char* ClipPresetHint(ClipPreset p) {
    switch (p) {
    case ClipPreset::Bob:     return "Moves up and back down (loops)";
    case ClipPreset::Hover:   return "Gentle float with a slight tilt (loops)";
    case ClipPreset::Spin:    return "Full turn around the up axis (loops)";
    case ClipPreset::Spin2D:  return "Full turn around Z, for sprites (loops)";
    case ClipPreset::Pulse:   return "Grows and shrinks (loops)";
    case ClipPreset::Shake:   return "Quick side-to-side shake (plays once)";
    case ClipPreset::Sway:    return "Rocks left and right (loops)";
    case ClipPreset::Bounce:  return "Hops with squash and stretch (loops)";
    case ClipPreset::PopIn:   return "Scales up from nothing with an overshoot (plays once)";
    case ClipPreset::SlideIn: return "Slides in from the left (plays once)";
    default:                  return "";
    }
}
/// Default amount for each preset (units, degrees or scale fraction).
inline float ClipPresetDefaultAmount(ClipPreset p) {
    switch (p) {
    case ClipPreset::Pulse:  return 0.15f;
    case ClipPreset::Shake:  return 0.15f;
    case ClipPreset::Sway:   return 12.0f;
    case ClipPreset::SlideIn: return 3.0f;
    case ClipPreset::Hover:  return 0.25f;
    case ClipPreset::PopIn:  return 0.15f;
    default:                 return 0.5f;
    }
}

namespace detail {
inline void ClearGroup(AnimationClip& c, const char* prefix) {
    std::vector<std::string> names;
    for (const auto& kv : c.Tracks()) if (kv.first.rfind(prefix, 0) == 0) names.push_back(kv.first);
    for (const auto& n : names) c.RemoveTrack(n);
}
inline void KeyPos(AnimationClip& c, float t, const Vec3& p) {
    c.AddKey("position.x", t, p.x); c.AddKey("position.y", t, p.y); c.AddKey("position.z", t, p.z);
}
inline void KeyScale(AnimationClip& c, float t, const Vec3& s) {
    c.AddKey("scale.x", t, s.x); c.AddKey("scale.y", t, s.y); c.AddKey("scale.z", t, s.z);
}
inline void KeyRot(AnimationClip& c, float t, Quat q, Quat& prev, bool& havePrev) {
    // Keep consecutive keys in one hemisphere so interpolation takes the short way.
    if (havePrev && q.x * prev.x + q.y * prev.y + q.z * prev.z + q.w * prev.w < 0.0f) q = Quat{-q.x, -q.y, -q.z, -q.w};
    prev = q; havePrev = true;
    c.AddKey("rotation.qx", t, q.x); c.AddKey("rotation.qy", t, q.y);
    c.AddKey("rotation.qz", t, q.z); c.AddKey("rotation.qw", t, q.w);
}
} // namespace detail

/// Write `preset` into `clip` around the given pose. `amount` scales the motion
/// (see ClipPresetDefaultAmount), `duration` is the clip length in seconds.
inline void ApplyClipPreset(AnimationClip& clip, ClipPreset preset, const Vec3& pos, const Quat& rot,
                            const Vec3& scale, float amount, float duration) {
    using namespace detail;
    if (duration < 0.05f) duration = 0.05f;
    const float d = duration, a = amount;
    const float kPi = 3.14159265358979f;
    Quat prev; bool havePrev = false;
    auto rotTracks = [&] { ClearGroup(clip, "rotation."); };
    auto setSmooth = [&](const char* prefix, bool s) { clip.SetSmooth(prefix, s); };
    bool loop = true;
    switch (preset) {
    case ClipPreset::Bob:
        ClearGroup(clip, "position.");
        KeyPos(clip, 0.0f, pos); KeyPos(clip, d * 0.5f, pos + Vec3{0, a, 0}); KeyPos(clip, d, pos);
        break;
    case ClipPreset::Hover: {
        ClearGroup(clip, "position."); rotTracks();
        for (int i = 0; i <= 4; ++i) {
            float t = d * i / 4.0f, ph = 2.0f * kPi * i / 4.0f;
            KeyPos(clip, t, pos + Vec3{0, std::sin(ph) * a, 0});
            KeyRot(clip, t, rot * Quat::Euler(0.0f, 0.0f, std::sin(ph + kPi * 0.5f) * 4.0f), prev, havePrev);
        }
        break;
    }
    case ClipPreset::Spin:
    case ClipPreset::Spin2D: {
        rotTracks();
        const int n = 8;
        for (int i = 0; i <= n; ++i) {
            float deg = 360.0f * i / n;
            Quat q = preset == ClipPreset::Spin ? Quat::Euler(0.0f, deg, 0.0f) : Quat::Euler(0.0f, 0.0f, deg);
            KeyRot(clip, d * i / n, rot * q, prev, havePrev);
        }
        setSmooth("rotation.", false);   // constant speed
        break;
    }
    case ClipPreset::Pulse:
        ClearGroup(clip, "scale.");
        KeyScale(clip, 0.0f, scale); KeyScale(clip, d * 0.5f, scale * (1.0f + a)); KeyScale(clip, d, scale);
        break;
    case ClipPreset::Shake: {
        ClearGroup(clip, "position.");
        loop = false;
        const int n = 8;
        for (int i = 0; i <= n; ++i) {
            float fade = 1.0f - (float)i / n;
            float side = (i % 2 ? 1.0f : -1.0f) * (i == 0 || i == n ? 0.0f : 1.0f);
            KeyPos(clip, d * i / n, pos + Vec3{side * a * fade, 0, side * a * 0.35f * fade});
        }
        setSmooth("position.", false);
        break;
    }
    case ClipPreset::Sway:
        rotTracks();
        KeyRot(clip, 0.0f,      rot, prev, havePrev);
        KeyRot(clip, d * 0.25f, rot * Quat::Euler(0.0f, 0.0f,  a), prev, havePrev);
        KeyRot(clip, d * 0.75f, rot * Quat::Euler(0.0f, 0.0f, -a), prev, havePrev);
        KeyRot(clip, d,         rot, prev, havePrev);
        break;
    case ClipPreset::Bounce: {
        ClearGroup(clip, "position."); ClearGroup(clip, "scale.");
        Vec3 squash{scale.x * 1.15f, scale.y * 0.8f, scale.z * 1.15f};
        Vec3 stretch{scale.x * 0.93f, scale.y * 1.12f, scale.z * 0.93f};
        KeyPos(clip, 0.0f, pos);                     KeyScale(clip, 0.0f, squash);
        KeyPos(clip, d * 0.15f, pos + Vec3{0, a * 0.35f, 0}); KeyScale(clip, d * 0.15f, stretch);
        KeyPos(clip, d * 0.5f, pos + Vec3{0, a, 0}); KeyScale(clip, d * 0.5f, scale);
        KeyPos(clip, d * 0.85f, pos + Vec3{0, a * 0.35f, 0}); KeyScale(clip, d * 0.85f, stretch);
        KeyPos(clip, d, pos);                        KeyScale(clip, d, squash);
        break;
    }
    case ClipPreset::PopIn:
        ClearGroup(clip, "scale.");
        loop = false;
        KeyScale(clip, 0.0f, Vec3{0, 0, 0});
        KeyScale(clip, d * 0.7f, scale * (1.0f + a));
        KeyScale(clip, d, scale);
        break;
    case ClipPreset::SlideIn:
        ClearGroup(clip, "position.");
        loop = false;
        KeyPos(clip, 0.0f, pos - Vec3{a, 0, 0});
        KeyPos(clip, d, pos);
        break;
    default: break;
    }
    clip.loop = loop;
    clip.SpanTracks(d);   // other tracks longer than the preset still extend the clip
}

} // namespace okay
