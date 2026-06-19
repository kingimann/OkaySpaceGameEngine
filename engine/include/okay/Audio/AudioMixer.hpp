#pragma once
#include "okay/Math/Vec3.hpp"

namespace okay {

class Scene;

/// Mixes all active AudioSources in a scene into a mono float buffer. Backends
/// (e.g. an SDL audio device) call Render() to pull audio; tests call it
/// directly. Assumes sources share the output sample rate.
struct AudioMixer {
    /// Sum active sources into `out` (length `frames`), clamped to [-1, 1], and
    /// advance each source's playhead. Returns the number of sources mixed.
    /// The final mix is scaled by `masterVolume` (and silenced when `muted`).
    static int Render(Scene& scene, float* out, int frames);

    /// Global output level [0..1] and mute, for settings menus.
    static inline float masterVolume = 1.0f;
    static inline bool  muted = false;

    /// World-space listener position for 3D (spatial) sources — set this each
    /// frame to the camera/player position so distance attenuation works.
    static inline Vec3 listener = Vec3::Zero;
    static void SetListener(const Vec3& p) { listener = p; }
};

} // namespace okay
